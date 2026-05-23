#include "opentelemetry/common/key_value_iterable_view.h"
#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/metrics/provider.h"
#include "telemetry.hpp"
#include "telemetry_internal.hpp"

#include <array>
#include <chrono>
#include <grpcpp/create_channel.h>
#include <grpcpp/support/client_interceptor.h>
#include <mutex>
#include <string_view>

namespace metrics = opentelemetry::metrics;
namespace nostd = opentelemetry::nostd;

using telemetry_internal::service_name;

namespace
{

struct server_metric_instruments {
    metrics::MeterProvider *provider = nullptr;
    nostd::unique_ptr<metrics::Counter<std::uint64_t>> request_counter;
    nostd::unique_ptr<metrics::Histogram<double>> request_duration_histogram;
};

struct client_metric_instruments {
    metrics::MeterProvider *provider = nullptr;
    nostd::unique_ptr<metrics::Counter<std::uint64_t>> request_counter;
    nostd::unique_ptr<metrics::Histogram<double>> request_duration_histogram;
};

auto server_metrics_mutex() -> std::mutex &
{
    static std::mutex mutex;
    return mutex;
}

auto client_metrics_mutex() -> std::mutex &
{
    static std::mutex mutex;
    return mutex;
}

auto server_metrics_instruments() -> server_metric_instruments &
{
    static server_metric_instruments instruments;
    auto provider = metrics::Provider::GetMeterProvider();
    if (instruments.provider != provider.get()) {
        // Instrument 需要跟随当前全局 MeterProvider。否则 init/cleanup 后，
        // 进程级静态缓存可能继续写入旧 provider 或 noop provider。
        auto meter = provider->GetMeter(service_name(), "1.0.0");
        instruments.provider = provider.get();
        instruments.request_counter = meter->CreateUInt64Counter(
                "rpc.server.requests",
                "Total number of gRPC requests handled by the server",
                "{request}"
        );
        instruments.request_duration_histogram = meter->CreateDoubleHistogram(
                "rpc.server.duration", "End-to-end gRPC server request latency", "ms"
        );
    }
    return instruments;
}

auto client_metrics_instruments() -> client_metric_instruments &
{
    static client_metric_instruments instruments;
    auto provider = metrics::Provider::GetMeterProvider();
    if (instruments.provider != provider.get()) {
        // Instrument 需要跟随当前全局 MeterProvider。否则 init/cleanup 后，
        // 进程级静态缓存可能继续写入旧 provider 或 noop provider。
        auto meter = provider->GetMeter(service_name(), "1.0.0");
        instruments.provider = provider.get();
        instruments.request_counter = meter->CreateUInt64Counter(
                "rpc.client.requests",
                "Total number of outgoing gRPC requests made by the client",
                "{request}"
        );
        instruments.request_duration_histogram = meter->CreateDoubleHistogram(
                "rpc.client.duration", "End-to-end gRPC client request latency", "ms"
        );
    }
    return instruments;
}

// 解析后的 RPC 方法信息：短服务名（去掉包前缀）+ 方法名
struct rpc_method_parts {
    std::string service_name;
    std::string method_name;
};

// 从 "/ServerMessages.ServerMessagesService/CheckOnline" 形式的全路径中提取短服务名和方法名
auto parse_rpc_method(std::string_view full_method_name) -> rpc_method_parts
{
    if (!full_method_name.empty() && full_method_name.front() == '/') {
        full_method_name.remove_prefix(1);
    }

    const auto slash_pos = full_method_name.rfind('/');
    const auto service_name = full_method_name.substr(0, slash_pos);
    const auto method_name = slash_pos == std::string_view::npos
                                     ? std::string_view{}
                                     : full_method_name.substr(slash_pos + 1);
    const auto dot_pos = service_name.rfind('.');
    const auto short_service_name =
            dot_pos == std::string_view::npos ? service_name : service_name.substr(dot_pos + 1);

    return { .service_name = std::string(short_service_name),
             .method_name = std::string(method_name) };
}

// 记录单次 RPC 调用的计数和耗时，附带 rpc.system/service/method/status_code 属性
auto record_server_rpc_metrics(
        std::chrono::steady_clock::time_point started_at,
        std::string_view service_name,
        std::string_view method_name,
        const grpc::Status &status
) -> void
{
    const opentelemetry::nostd::string_view otel_service_name(
            service_name.data(), service_name.size()
    );
    const opentelemetry::nostd::string_view otel_method_name(
            method_name.data(), method_name.size()
    );
    const auto duration_millis = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started_at
    );
    const std::array<
            std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>,
            4>
            attributes = { {
                    { "rpc.system", "grpc" },
                    { "rpc.service", otel_service_name },
                    { "rpc.method", otel_method_name },
                    { "rpc.grpc.status_code", static_cast<std::int64_t>(status.error_code()) },
            } };
    const opentelemetry::common::KeyValueIterableView attributes_view{ attributes };
    const auto &metric_attributes =
            static_cast<const opentelemetry::common::KeyValueIterable &>(attributes_view);

    std::lock_guard<std::mutex> lock(server_metrics_mutex());
    auto &instruments = server_metrics_instruments();
    instruments.request_counter->Add(1, metric_attributes);
    instruments.request_duration_histogram->Record(
            duration_millis.count(), metric_attributes, opentelemetry::context::Context{}
    );
}

auto record_client_rpc_metrics(
        std::chrono::steady_clock::time_point started_at,
        std::string_view service_name,
        std::string_view method_name,
        const grpc::Status &status
) -> void
{
    const opentelemetry::nostd::string_view otel_service_name(
            service_name.data(), service_name.size()
    );
    const opentelemetry::nostd::string_view otel_method_name(
            method_name.data(), method_name.size()
    );
    const auto duration_millis = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started_at
    );
    const std::array<
            std::pair<opentelemetry::nostd::string_view, opentelemetry::common::AttributeValue>,
            4>
            attributes = { {
                    { "rpc.system", "grpc" },
                    { "rpc.service", otel_service_name },
                    { "rpc.method", otel_method_name },
                    { "rpc.grpc.status_code", static_cast<std::int64_t>(status.error_code()) },
            } };
    const opentelemetry::common::KeyValueIterableView attributes_view{ attributes };
    const auto &metric_attributes =
            static_cast<const opentelemetry::common::KeyValueIterable &>(attributes_view);

    std::lock_guard<std::mutex> lock(client_metrics_mutex());
    auto &instruments = client_metrics_instruments();
    instruments.request_counter->Add(1, metric_attributes);
    instruments.request_duration_histogram->Record(
            duration_millis.count(), metric_attributes, opentelemetry::context::Context{}
    );
}

// gRPC 服务端拦截器：在每个 RPC 调用的 PRE_SEND_STATUS 阶段记录请求计数和耗时
class grpc_server_metrics_interceptor final : public grpc::experimental::Interceptor
{
public:
    explicit grpc_server_metrics_interceptor(grpc::experimental::ServerRpcInfo *info)
        : started_at_(std::chrono::steady_clock::now())
    {
        const auto rpc = parse_rpc_method(info->method());
        service_name_ = rpc.service_name;
        method_name_ = rpc.method_name;
    }

    void Intercept(grpc::experimental::InterceptorBatchMethods *methods) override
    {
        if (methods->QueryInterceptionHookPoint(
                    grpc::experimental::InterceptionHookPoints::PRE_SEND_STATUS
            ) &&
            !metrics_recorded_) {
            record_server_rpc_metrics(
                    started_at_, service_name_, method_name_, methods->GetSendStatus()
            );
            metrics_recorded_ = true;
        }

        methods->Proceed();
    }

private:
    std::chrono::steady_clock::time_point started_at_;
    std::string service_name_;
    std::string method_name_;
    bool metrics_recorded_ = false;
};

// 拦截器工厂：为每个 RPC 创建一个 grpc_server_metrics_interceptor 实例
class grpc_server_metrics_interceptor_factory final
    : public grpc::experimental::ServerInterceptorFactoryInterface
{
public:
    grpc::experimental::Interceptor *CreateServerInterceptor(grpc::experimental::ServerRpcInfo *info
    ) override
    {
        // 这里选择 server 级 interceptor，而不是 handler 内显式埋点，
        // 是因为 method/status/生命周期这些信息本来就由 gRPC runtime 持有。
        // 把它们留在 server 构建层统一处理，更接近主流 middleware 写法，也更稳。
        // gRPC 这里的工厂接口固定返回裸指针，所有权随后由 runtime 接管。
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        return std::make_unique<grpc_server_metrics_interceptor>(info).release();
    }
};

// gRPC 客户端拦截器：在每个 RPC 调用的 POST_RECV_STATUS 阶段记录请求计数和耗时
class grpc_client_metrics_interceptor final : public grpc::experimental::Interceptor
{
public:
    explicit grpc_client_metrics_interceptor(grpc::experimental::ClientRpcInfo *info)
        : started_at_(std::chrono::steady_clock::now())
    {
        const auto rpc = parse_rpc_method(info->method());
        service_name_ = rpc.service_name;
        method_name_ = rpc.method_name;
    }

    void Intercept(grpc::experimental::InterceptorBatchMethods *methods) override
    {
        if (methods->QueryInterceptionHookPoint(
                    grpc::experimental::InterceptionHookPoints::POST_RECV_STATUS
            ) &&
            !metrics_recorded_) {
            record_client_rpc_metrics(
                    started_at_, service_name_, method_name_, *methods->GetRecvStatus()
            );
            metrics_recorded_ = true;
        }

        methods->Proceed();
    }

private:
    std::chrono::steady_clock::time_point started_at_;
    std::string service_name_;
    std::string method_name_;
    bool metrics_recorded_ = false;
};

// 拦截器工厂：为每个 RPC 创建一个 grpc_client_metrics_interceptor 实例
class grpc_client_metrics_interceptor_factory final
    : public grpc::experimental::ClientInterceptorFactoryInterface
{
public:
    grpc::experimental::Interceptor *CreateClientInterceptor(grpc::experimental::ClientRpcInfo *info
    ) override
    {
        // gRPC 这里的工厂接口固定返回裸指针，所有权随后由 runtime 接管。
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        return std::make_unique<grpc_client_metrics_interceptor>(info).release();
    }
};

} // namespace

namespace telemetry_internal
{

void reset_grpc_metric_instruments()
{
    {
        std::lock_guard<std::mutex> lock(server_metrics_mutex());
        auto &instruments = server_metrics_instruments();
        instruments.provider = nullptr;
        instruments.request_duration_histogram.reset();
        instruments.request_counter.reset();
    }

    {
        std::lock_guard<std::mutex> lock(client_metrics_mutex());
        auto &instruments = client_metrics_instruments();
        instruments.provider = nullptr;
        instruments.request_duration_histogram.reset();
        instruments.request_counter.reset();
    }
}

} // namespace telemetry_internal

void install_grpc_server_metrics(grpc::ServerBuilder &builder)
{
    std::vector<std::unique_ptr<grpc::experimental::ServerInterceptorFactoryInterface>>
            interceptor_creators;
    interceptor_creators.push_back(std::make_unique<grpc_server_metrics_interceptor_factory>());
    builder.experimental().SetInterceptorCreators(std::move(interceptor_creators));
}

auto create_grpc_client_channel_with_metrics(
        const std::string &target,
        const std::shared_ptr<grpc::ChannelCredentials> &credentials,
        const grpc::ChannelArguments &arguments
) -> std::shared_ptr<grpc::Channel>
{
    std::vector<std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>>
            interceptor_creators;
    interceptor_creators.push_back(std::make_unique<grpc_client_metrics_interceptor_factory>());
    return grpc::experimental::CreateCustomChannelWithInterceptors(
            target, credentials, arguments, std::move(interceptor_creators)
    );
}
