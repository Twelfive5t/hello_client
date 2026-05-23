#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_metric_exporter_options.h"
#include "opentelemetry/metrics/noop.h"
#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_factory.h"
#include "opentelemetry/sdk/metrics/export/periodic_exporting_metric_reader_options.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/resource/semantic_conventions.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/tracer_context.h"
#include "opentelemetry/sdk/trace/tracer_context_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/sdk/version/version.h"
#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"
#include "opentelemetry/trace/provider.h"
#include "telemetry_internal.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string_view>
#include <vector>

namespace trace = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace metrics = opentelemetry::metrics;
namespace metrics_sdk = opentelemetry::sdk::metrics;
namespace otlp = opentelemetry::exporter::otlp;
namespace resource = opentelemetry::sdk::resource;

namespace
{

using telemetry_internal::global_meter_provider;
using telemetry_internal::global_tracer;
using telemetry_internal::host_name;
using telemetry_internal::make_ignore_sampler;
using telemetry_internal::make_subtree_discard_batch_processor;
using telemetry_internal::map_text_carrier;
using telemetry_internal::reset_grpc_metric_instruments;
using telemetry_internal::service_name;
#ifdef __linux__
using telemetry_internal::scoped_thread_affinity;
#endif

} // namespace

void init_tracer(const telemetry_config &config)
{
    service_name() = config.service_name.empty() ? "telemetry_demo" : config.service_name;

#ifdef __linux__
    // 创建 gRPC Exporter 时，其内部后台线程会继承当前线程亲和性；
    // 用作用域对象保证 exporter 初始化异常时也能恢复调用线程原设置。
    scoped_thread_affinity exporter_thread_affinity(config.background_cpu_affinity);
#endif

    // 1. 创建 Exporter: 负责将 Trace 数据发送到后端 (如 Jaeger, Zipkin, OTel Collector)
    // 这里使用 OTLP gRPC Exporter，它是 OpenTelemetry 的标准协议
    // 并使用 FilteringExporter 进行包装，以支持在导出阶段过滤掉被标记为丢弃的 Span
    opentelemetry::exporter::otlp::OtlpGrpcExporterOptions opts;
    opts.endpoint = config.endpoint;

    // 创建 Exporter 时会初始化 gRPC 及其 event_engine 线程
    auto exporter = otlp::OtlpGrpcExporterFactory::Create(opts);

    // 2. 创建 Processor: 负责处理 Span (如批量发送，减少网络开销)
    // BatchSpanProcessor 会在后台缓冲 Span，并批量发送给 Exporter
    // 使用 SubtreeDiscardSpanProcessor 包装 BatchSpanProcessor，以支持整棵树的丢弃逻辑
    auto processor = make_subtree_discard_batch_processor(std::move(exporter));

    std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>> processors;
    processors.push_back(std::move(processor));

    // 3. 创建 Resource: 描述产生 Trace 的实体 (如服务名、版本、环境)
    // 这些信息会附加到所有的 Span 上，方便在 UI 上筛选和定位
    std::string service_instance_id = config.service_instance_id;
    if (service_instance_id.empty()) {
        // 如果未指定实例ID，则自动生成：ServiceName + 时间戳
        auto t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        errno_t err = localtime_s(&tm, &t);
        if (err != 0) {
            std::tm *fallback = std::localtime(&t);
            if (fallback != nullptr) {
                tm = *fallback;
            } else {
                tm = {};
            }
        }
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "_%Y%m%d_%H%M%S");
        service_instance_id = service_name() + oss.str();
    }

    resource::ResourceAttributes attributes = {
        { resource::SemanticConventions::kServiceName, service_name() },
        { resource::SemanticConventions::kServiceInstanceId, service_instance_id },
        { resource::SemanticConventions::kServiceVersion, config.version },
        { resource::SemanticConventions::kDeploymentEnvironment, config.environment },
        { resource::SemanticConventions::kHostName, host_name() }
    };
    auto resource = opentelemetry::sdk::resource::Resource::Create(attributes);

    // 4. 创建 TracerProvider: 管理 Tracer 的生命周期和配置
    // 使用自定义的 IgnoreSampler，根据配置的忽略列表在 Span 创建前进行过滤
    auto sampler = make_ignore_sampler(config.ignored_spans);
    std::unique_ptr<opentelemetry::sdk::trace::TracerContext> context =
            opentelemetry::sdk::trace::TracerContextFactory::Create(
                    std::move(processors), resource, std::move(sampler)
            );
    std::shared_ptr<opentelemetry::trace::TracerProvider> provider =
            trace_sdk::TracerProviderFactory::Create(std::move(context));

    // 5. 设置全局 TracerProvider: 让后续代码可以通过 Provider::GetTracerProvider() 获取
    trace::Provider::SetTracerProvider(provider);
    global_tracer() = provider->GetTracer(service_name(), OPENTELEMETRY_SDK_VERSION);

    // 5. 初始化 Metrics Provider：通过 OTLP gRPC 周期性导出指标（默认 15s/次）
    opentelemetry::exporter::otlp::OtlpGrpcMetricExporterOptions metric_options;
    metric_options.endpoint = config.endpoint;

    auto metric_exporter = otlp::OtlpGrpcMetricExporterFactory::Create(metric_options);
    metrics_sdk::PeriodicExportingMetricReaderOptions metric_reader_options{};
    metric_reader_options.export_interval_millis =
            std::chrono::milliseconds(15000); // 15s export interval
    metric_reader_options.export_timeout_millis =
            std::chrono::milliseconds(5000); // 5s export timeout

    auto metric_reader = metrics_sdk::PeriodicExportingMetricReaderFactory::Create(
            std::move(metric_exporter), metric_reader_options
    );

    auto meter_provider = std::make_shared<metrics_sdk::MeterProvider>(
            std::make_unique<metrics_sdk::ViewRegistry>(), resource
    );
    meter_provider->AddMetricReader(
            std::shared_ptr<metrics_sdk::MetricReader>(std::move(metric_reader))
    );
    global_meter_provider() = meter_provider;
    metrics::Provider::SetMeterProvider(
            std::static_pointer_cast<metrics::MeterProvider>(meter_provider)
    );

    // 6. 设置全局 Propagator: 负责跨进程/跨服务传递 Trace Context (如 TraceId, SpanId)
    // HttpTraceContext 支持 W3C Trace Context 标准，用于在 HTTP Header 中传递上下文
    opentelemetry::context::propagation::GlobalTextMapPropagator::SetGlobalPropagator(
            opentelemetry::nostd::shared_ptr<
                    opentelemetry::context::propagation::TextMapPropagator>(
                    new opentelemetry::trace::propagation::HttpTraceContext()
            )
    );
}

void cleanup_tracer()
{
    if (global_meter_provider()) {
        constexpr auto cleanup_timeout =
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds(3)
                ); // 3s flush deadline
        global_meter_provider()->ForceFlush(cleanup_timeout);
        reset_grpc_metric_instruments();
        // 不显式调用 Shutdown()：reset() 后 SetMeterProvider(noop) 会释放最后一个引用，
        // 触发析构函数完成唯一的 Shutdown，避免二次调用引发 "Shutdown can only be invoked once" 警告。
        global_meter_provider().reset();
    }

    metrics::Provider::SetMeterProvider(
            opentelemetry::nostd::shared_ptr<metrics::MeterProvider>(new metrics::NoopMeterProvider)
    );

    std::shared_ptr<trace::TracerProvider> none;
    global_tracer() = opentelemetry::nostd::shared_ptr<trace::Tracer>();
    trace::Provider::SetTracerProvider(none);
}

auto get_trace_headers() -> std::map<std::string, std::string>
{
    std::map<std::string, std::string> result;
    auto propagator =
            opentelemetry::context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
    map_text_carrier mc(result);
    propagator->Inject(mc, opentelemetry::context::RuntimeContext::GetCurrent());
    return result;
}
