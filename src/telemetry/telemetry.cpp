#include "telemetry.hpp"

#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_exporter_factory.h"
#include "opentelemetry/sdk/resource/semantic_conventions.h"
#include "opentelemetry/sdk/trace/batch_span_processor.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_options.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/sdk/trace/sampler.h"
#include "opentelemetry/sdk/trace/simple_processor_factory.h"
#include "opentelemetry/sdk/trace/span_data.h"
#include "opentelemetry/sdk/trace/tracer_context.h"
#include "opentelemetry/sdk/trace/tracer_context_factory.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/context.h"
#include "opentelemetry/trace/propagation/http_trace_context.h"
#include "opentelemetry/trace/provider.h"

#include <ctime>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>

namespace trace = opentelemetry::trace;
namespace trace_sdk = opentelemetry::sdk::trace;
namespace otlp = opentelemetry::exporter::otlp;
namespace nostd = opentelemetry::nostd;
namespace resource = opentelemetry::sdk::resource;

// 自定义采样器：用于过滤掉不需要的 Span (例如高频但无关紧要的函数)
class ignore_sampler : public opentelemetry::sdk::trace::Sampler
{
public:
    explicit ignore_sampler(std::vector<std::string> ignored_names)
        : ignored_names_(std::move(ignored_names))
    {
    }

    // 采样决策逻辑：在 Span 创建前调用
    opentelemetry::sdk::trace::SamplingResult ShouldSample(
            const opentelemetry::trace::SpanContext & /*parent_context*/,
            opentelemetry::trace::TraceId /*trace_id*/,
            opentelemetry::nostd::string_view name,
            opentelemetry::trace::SpanKind /*span_kind*/,
            const opentelemetry::common::KeyValueIterable & /*attributes*/,
            const opentelemetry::trace::SpanContextKeyValueIterable & /*links*/
    ) noexcept override
    {
        std::string name_str(name.data(), name.size());
        // 遍历过滤列表，如果 Span 名称包含任意一个关键词，则丢弃
        for (const auto &ignored : ignored_names_) {
            if (name_str.find(ignored) != std::string::npos) {
                // Decision::DROP 表示完全丢弃该 Span，不记录也不导出
                return {.decision = opentelemetry::sdk::trace::Decision::DROP,
                        .attributes = {},
                        .trace_state = {}};
            }
        }
        // 默认行为：记录并采样 (RECORD_AND_SAMPLE)
        return {.decision = opentelemetry::sdk::trace::Decision::RECORD_AND_SAMPLE,
                .attributes = {},
                .trace_state = {}};
    }

    [[nodiscard]] opentelemetry::nostd::string_view GetDescription() const noexcept override
    {
        return "IgnoreSampler";
    }

private:
    std::vector<std::string> ignored_names_;
};

// 自定义 Recordable 包装器：用于拦截 Span 的属性设置，支持手动标记丢弃 Span
class wrapper_recordable : public opentelemetry::sdk::trace::Recordable
{
public:
    explicit wrapper_recordable(std::unique_ptr<opentelemetry::sdk::trace::Recordable> inner)
        : inner_(std::move(inner))
    {
    }

    void SetIdentity(
            const opentelemetry::trace::SpanContext &span_context,
            opentelemetry::trace::SpanId parent_span_id
    ) noexcept override
    {
        span_context_ = span_context;
        parent_span_id_ = parent_span_id;
        inner_->SetIdentity(span_context, parent_span_id);
    }

    void SetAttribute(
            opentelemetry::nostd::string_view key,
            const opentelemetry::common::AttributeValue &value
    ) noexcept override
    {
        // 拦截 "manual_drop" 属性，如果设置了该属性，则标记为需要丢弃
        if (key == "manual_drop") {
            should_drop_ = true;
        }
        inner_->SetAttribute(key, value);
    }

    void AddEvent(
            opentelemetry::nostd::string_view name,
            opentelemetry::common::SystemTimestamp timestamp,
            const opentelemetry::common::KeyValueIterable &attributes
    ) noexcept override
    {
        inner_->AddEvent(name, timestamp, attributes);
    }

    void AddLink(
            const opentelemetry::trace::SpanContext &span_context,
            const opentelemetry::common::KeyValueIterable &attributes
    ) noexcept override
    {
        inner_->AddLink(span_context, attributes);
    }

    void SetStatus(
            opentelemetry::trace::StatusCode code,
            opentelemetry::nostd::string_view description
    ) noexcept override
    {
        inner_->SetStatus(code, description);
    }

    void SetName(opentelemetry::nostd::string_view name) noexcept override
    {
        inner_->SetName(name);
    }

    void SetTraceFlags(opentelemetry::trace::TraceFlags flags) noexcept override
    {
        inner_->SetTraceFlags(flags);
    }

    void SetSpanKind(opentelemetry::trace::SpanKind span_kind) noexcept override
    {
        inner_->SetSpanKind(span_kind);
    }

    void SetResource(const opentelemetry::sdk::resource::Resource &resource) noexcept override
    {
        inner_->SetResource(resource);
    }

    void SetStartTime(opentelemetry::common::SystemTimestamp start_time) noexcept override
    {
        inner_->SetStartTime(start_time);
    }

    void SetDuration(std::chrono::nanoseconds duration) noexcept override
    {
        inner_->SetDuration(duration);
    }

    void SetInstrumentationScope(
            const opentelemetry::sdk::instrumentationscope::InstrumentationScope
                    &instrumentation_scope
    ) noexcept override
    {
        inner_->SetInstrumentationScope(instrumentation_scope);
    }

    explicit operator opentelemetry::sdk::trace::SpanData *() const override
    {
        return inner_->operator opentelemetry::sdk::trace::SpanData *();
    }

    [[nodiscard]] std::unique_ptr<opentelemetry::sdk::trace::Recordable> release_inner()
    {
        return std::move(inner_);
    }

    [[nodiscard]] bool should_drop() const
    {
        return should_drop_;
    }

    [[nodiscard]] opentelemetry::trace::SpanId get_span_id() const
    {
        return span_context_.span_id();
    }

    [[nodiscard]] opentelemetry::trace::SpanId get_parent_span_id() const
    {
        return parent_span_id_;
    }

private:
    std::unique_ptr<opentelemetry::sdk::trace::Recordable> inner_;
    bool should_drop_ = false;
    opentelemetry::trace::SpanContext span_context_ =
            opentelemetry::trace::SpanContext::GetInvalid();
    opentelemetry::trace::SpanId parent_span_id_;
};

// 自定义 SpanProcessor：用于处理整棵 Span 树的丢弃逻辑
// 如果父 Span 被标记为丢弃，则其所有子 Span 也应该被丢弃
class subtree_discard_span_processor : public opentelemetry::sdk::trace::SpanProcessor
{
public:
    explicit subtree_discard_span_processor(
            std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor> next
    )
        : next_(std::move(next))
    {
    }

    std::unique_ptr<opentelemetry::sdk::trace::Recordable> MakeRecordable() noexcept override
    {
        return next_->MakeRecordable();
    }

    // Span 开始时调用：记录活跃的 Span ID
    void OnStart(
            opentelemetry::sdk::trace::Recordable &recordable,
            const opentelemetry::trace::SpanContext &parent_context
    ) noexcept override
    {
        next_->OnStart(recordable, parent_context);

        auto *wrapper = dynamic_cast<wrapper_recordable *>(&recordable);
        std::string span_id = to_hex(wrapper->get_span_id());

        std::lock_guard<std::mutex> lock(mutex_);
        active_spans_.insert(span_id);
    }

    // Span 结束时调用：决定是立即导出、缓存等待父 Span 决定、还是丢弃
    void OnEnd(std::unique_ptr<opentelemetry::sdk::trace::Recordable> &&recordable
    ) noexcept override
    {
        auto *wrapper = dynamic_cast<wrapper_recordable *>(recordable.get());

        std::string span_id = to_hex(wrapper->get_span_id());
        std::string parent_span_id = to_hex(wrapper->get_parent_span_id());

        bool should_drop = wrapper->should_drop();

        std::lock_guard<std::mutex> lock(mutex_);
        active_spans_.erase(span_id);

        // 如果当前 Span 被标记为丢弃，则丢弃它及其所有已缓存的子 Span
        if (should_drop) {
            drop_subtree(span_id);
            return;
        }

        // 如果父 Span 还在活跃列表中，说明当前 Span 是某个未完成 Span 的子节点
        // 将其缓存起来，等待父 Span 结束时一起处理
        if (active_spans_.contains(parent_span_id)) {
            pending_children_[parent_span_id].push_back(std::move(recordable));
        } else {
            // 如果没有父 Span 或父 Span 已经结束（不活跃），则直接导出当前 Span
            // 并递归导出其所有缓存的子 Span
            next_->OnEnd(std::move(recordable));
            export_subtree(span_id);
        }
    }

    bool ForceFlush(
            std::chrono::microseconds timeout = std::chrono::microseconds::max()
    ) noexcept override
    {
        return next_->ForceFlush(timeout);
    }

    bool Shutdown(
            std::chrono::microseconds timeout = std::chrono::microseconds::max()
    ) noexcept override
    {
        return next_->Shutdown(timeout);
    }

private:
    std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor> next_;
    std::mutex mutex_;
    std::set<std::string> active_spans_;
    std::map<std::string, std::vector<std::unique_ptr<opentelemetry::sdk::trace::Recordable>>>
            pending_children_;

    [[nodiscard]] static std::string to_hex(const opentelemetry::trace::SpanId &span_id)
    {
        static constexpr size_t span_id_hex_len = 16;
        std::array<char, span_id_hex_len> buf{};
        span_id.ToLowerBase16(buf);
        return {buf.data(), buf.size()};
    }

    // 递归丢弃指定 Span ID 的所有子 Span
    void drop_subtree(const std::string &span_id) // NOLINT(misc-no-recursion)
    {
        auto it = pending_children_.find(span_id);
        if (it != pending_children_.end()) {
            for (auto &child : it->second) {
                auto *wrapper = dynamic_cast<wrapper_recordable *>(child.get());
                drop_subtree(to_hex(wrapper->get_span_id()));
            }
            pending_children_.erase(it);
        }
    }

    // 递归导出指定 Span ID 的所有子 Span
    void export_subtree(const std::string &span_id) // NOLINT(misc-no-recursion)
    {
        auto it = pending_children_.find(span_id);
        if (it != pending_children_.end()) {
            auto children = std::move(it->second);
            pending_children_.erase(it);

            for (auto &child : children) {
                auto *wrapper = dynamic_cast<wrapper_recordable *>(child.get());
                std::string child_id = to_hex(wrapper->get_span_id());

                next_->OnEnd(std::move(child));

                if (!child_id.empty()) {
                    export_subtree(child_id);
                }
            }
        }
    }
};

// 自定义 Exporter 包装器：在导出前检查 Span 是否被标记为丢弃
class filtering_exporter : public opentelemetry::sdk::trace::SpanExporter
{
public:
    explicit filtering_exporter(std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> exporter)
        : exporter_(std::move(exporter))
    {
    }

    std::unique_ptr<opentelemetry::sdk::trace::Recordable> MakeRecordable() noexcept override
    {
        return std::make_unique<wrapper_recordable>(exporter_->MakeRecordable());
    }

    opentelemetry::sdk::common::ExportResult Export(
            const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::trace::Recordable>>
                    &spans
    ) noexcept override
    {
        std::vector<std::unique_ptr<opentelemetry::sdk::trace::Recordable>> valid_spans;
        valid_spans.reserve(spans.size());

        // 遍历所有待导出的 Span，过滤掉被标记为丢弃的 Span
        for (auto &span : spans) {
            auto *wrapper = dynamic_cast<wrapper_recordable *>(span.get());
            if (!wrapper->should_drop()) {
                // 如果不需要丢弃，则提取内部真实的 Recordable 对象
                valid_spans.push_back(wrapper->release_inner());
            }
        }

        // 如果没有有效 Span，直接返回成功
        if (valid_spans.empty()) {
            return opentelemetry::sdk::common::ExportResult::kSuccess;
        }

        // 将过滤后的有效 Span 传递给真实的 Exporter
        return exporter_->Export(
                opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::trace::Recordable>>(
                        valid_spans.data(), valid_spans.size()
                )
        );
    }

    bool Shutdown(
            std::chrono::microseconds timeout = std::chrono::microseconds::max()
    ) noexcept override
    {
        return exporter_->Shutdown(timeout);
    }

private:
    std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> exporter_;
};

void init_tracer(const telemetry_config &config)
{
    // 1. 创建 Exporter: 负责将 Trace 数据发送到后端 (如 Jaeger, Zipkin, OTel Collector)
    // 这里使用 OTLP gRPC Exporter，它是 OpenTelemetry 的标准协议
    // 并使用 FilteringExporter 进行包装，以支持在导出阶段过滤掉被标记为丢弃的 Span
    opentelemetry::exporter::otlp::OtlpGrpcExporterOptions opts;
    opts.endpoint = config.endpoint;

    auto exporter = otlp::OtlpGrpcExporterFactory::Create(opts);
    auto filtering_exp = std::unique_ptr<opentelemetry::sdk::trace::SpanExporter>(
            new filtering_exporter(std::move(exporter))
    );

    // 2. 创建 Processor: 负责处理 Span (如批量发送，减少网络开销)
    // BatchSpanProcessor 会在后台缓冲 Span，并批量发送给 Exporter
    // 使用 SubtreeDiscardSpanProcessor 包装 BatchSpanProcessor，以支持整棵树的丢弃逻辑
    trace_sdk::BatchSpanProcessorOptions options{};
    auto batch_processor =
            trace_sdk::BatchSpanProcessorFactory::Create(std::move(filtering_exp), options);
    auto processor = std::make_unique<subtree_discard_span_processor>(std::move(batch_processor));

    std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>> processors;
    processors.push_back(std::move(processor));

    // 3. 创建 Resource: 描述产生 Trace 的实体 (如服务名、版本、环境)
    // 这些信息会附加到所有的 Span 上，方便在 UI 上筛选和定位
    std::string service_instance_id = config.service_instance_id;
    if (service_instance_id.empty()) {
        // 如果未指定实例ID，则自动生成：ServiceName + 时间戳
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        std::ostringstream oss;
        oss << std::put_time(&tm, "_%Y%m%d_%H%M%S");
        service_instance_id = config.service_name + oss.str();
    }

    resource::ResourceAttributes attributes = {
            {resource::SemanticConventions::kServiceName, config.service_name},
            {resource::SemanticConventions::kServiceInstanceId, service_instance_id},
            {resource::SemanticConventions::kServiceVersion, config.version},
            {resource::SemanticConventions::kDeploymentEnvironment, config.environment},
            {resource::SemanticConventions::kHostName, "test"}
    }; // 实际项目中可获取真实 Hostname
    auto resource = opentelemetry::sdk::resource::Resource::Create(attributes);

    // 4. 创建 TracerProvider: 管理 Tracer 的生命周期和配置
    auto sampler = std::make_unique<ignore_sampler>(config.ignored_spans);
    std::unique_ptr<opentelemetry::sdk::trace::TracerContext> context =
            opentelemetry::sdk::trace::TracerContextFactory::Create(
                    std::move(processors), resource, std::move(sampler)
            );
    std::shared_ptr<opentelemetry::trace::TracerProvider> provider =
            trace_sdk::TracerProviderFactory::Create(std::move(context));

    // 5. 设置全局 TracerProvider
    trace::Provider::SetTracerProvider(provider);

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
    std::shared_ptr<trace::TracerProvider> none;
    trace::Provider::SetTracerProvider(none);
}

// ---------------------------------------------------------------------------
// 共用内部载体辅助类：将 std::map 适配为 OTel TextMapCarrier
// ---------------------------------------------------------------------------
namespace
{
struct map_text_carrier : public opentelemetry::context::propagation::TextMapCarrier {
    const std::map<std::string, std::string> *read_map = nullptr;
    std::map<std::string, std::string> *write_map = nullptr;

    explicit map_text_carrier(const std::map<std::string, std::string> &m) : read_map(&m)
    {
    }
    explicit map_text_carrier(std::map<std::string, std::string> &m) : write_map(&m)
    {
    }

    [[nodiscard]] opentelemetry::nostd::string_view Get(opentelemetry::nostd::string_view key
    ) const noexcept override
    {
        if (read_map == nullptr) {
            return "";
        }
        auto it = read_map->find(std::string(key));
        if (it != read_map->end()) {
            return it->second;
        }
        return "";
    }

    void Set(
            opentelemetry::nostd::string_view key,
            opentelemetry::nostd::string_view value
    ) noexcept override
    {
        if (write_map != nullptr) {
            (*write_map)[std::string(key)] = std::string(value);
        }
    }
};
} // namespace

// ---------------------------------------------------------------------------
// get_trace_headers——将当前 context 序列化为 W3C header map
// ---------------------------------------------------------------------------
auto get_trace_headers() -> std::map<std::string, std::string>
{
    std::map<std::string, std::string> result;
    auto propagator =
            opentelemetry::context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
    map_text_carrier mc(result);
    propagator->Inject(mc, opentelemetry::context::RuntimeContext::GetCurrent());
    return result;
}

namespace
{
auto get_tracer() -> nostd::shared_ptr<trace::Tracer>
{
    auto provider = trace::Provider::GetTracerProvider();
    return provider->GetTracer("telemetry_demo", OPENTELEMETRY_SDK_VERSION);
}

auto to_otel_kind(span_kind kind) -> opentelemetry::trace::SpanKind
{
    switch (kind) {
    case span_kind::CLIENT:
        return opentelemetry::trace::SpanKind::kClient;
    case span_kind::SERVER:
        return opentelemetry::trace::SpanKind::kServer;
    default:
        return opentelemetry::trace::SpanKind::kInternal;
    }
}
} // namespace

class trace_span::impl
{
public:
    explicit impl(const std::string &str, opentelemetry::trace::SpanKind kind)
        // trace::Scope 是 RAII 对象且不可移动，必须在初始化列表中构造
        // StartSpan: 开始一个新的 Span
        // WithActiveSpan: 将该 Span 设为当前线程的活跃 Span，作用域结束时自动恢复上一个 Span
        : span_(make_root_span(str, kind)),
          outer_scope_(get_tracer()->WithActiveSpan(span_)) // NOLINT
    {
        before(str);
    }

    ~impl()
    {
        after();
    }

    impl(const impl &) = delete;
    auto operator=(const impl &) -> impl & = delete;
    impl(impl &&) = delete;
    auto operator=(impl &&) -> impl & = delete;

    auto before(const std::string & /*str*/) -> void
    {
    }

    auto add_event(const std::string &name) const -> void
    {
        span_->AddEvent(name);
    }

    auto after() const -> void
    {
        span_->End();
    }

    auto discard() const -> void
    {
        span_->SetAttribute("manual_drop", true);
    }

private:
    static auto make_root_span(const std::string &str, opentelemetry::trace::SpanKind kind)
            -> nostd::shared_ptr<opentelemetry::trace::Span>
    {
        opentelemetry::trace::StartSpanOptions options;
        options.kind = kind;
        return get_tracer()->StartSpan(str, {}, options);
    }

    nostd::shared_ptr<opentelemetry::trace::Span> span_;
    trace::Scope outer_scope_;
};

auto trace_span::before(const std::string &str) -> void
{
    impl_->before(str);
}

auto trace_span::add_event(const std::string &name) -> void
{
    impl_->add_event(name);
}

auto trace_span::after() -> void
{
    impl_->after();
}

auto trace_span::discard() -> void
{
    impl_->discard();
}

trace_span::trace_span(const std::string &str, span_kind kind)
    : impl_(std::make_unique<impl>(str, to_otel_kind(kind)))
{
}
trace_span::~trace_span() = default;

trace_span::trace_span(trace_span &&) noexcept = default;
auto trace_span::operator=(trace_span &&) noexcept -> trace_span & = default;
