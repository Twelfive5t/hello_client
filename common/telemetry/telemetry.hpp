#pragma once

#include <cstdint>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/client_interceptor.h>
#include <map>
#include <memory>
#include <source_location>
#include <string>
#include <vector>

// ---- 配置类型 ---------------------------------------------------------------

// Span 类型，影响 Jaeger 等后端的可视化颜色和布局。
enum class span_kind : std::uint8_t { INTERNAL, CLIENT, SERVER };

struct telemetry_config {
    std::string service_name = "telemetry_demo";
    std::string service_instance_id; // Empty defaults to auto-generated
    std::string endpoint = "localhost:4317";
    std::string version = "0.0.1";
    std::string environment = "test";
    std::vector<std::string> ignored_spans;

    // 后台线程配置 (解决 gRPC 线程继承 RT 亲和性的问题)
    std::vector<int> background_cpu_affinity = {
        0
    }; // 指定后台线程绑定的 CPU 核，为空则不修改 (默认绑定到核 0)
};

// ---- 生命周期 ---------------------------------------------------------------

// 初始化 Tracer，支持传入配置。
void init_tracer(const telemetry_config &config = telemetry_config{});
void cleanup_tracer();

// ---- Trace Context ---------------------------------------------------------

// 将当前活跃 Span 的 Trace Context 序列化为 W3C traceparent/tracestate header，
// 用于注入到对外 RPC 调用（如 gRPC client metadata）中。
auto get_trace_headers() -> std::map<std::string, std::string>;

// ---- gRPC Metrics ----------------------------------------------------------

// gRPC 服务端的请求级 metrics 适合放在 server 构建阶段统一挂载；
// 业务 handler 不再关心指标对象、方法名解析或状态码属性组装。
// 注意：gRPC server interceptor 必须在 ServerBuilder 阶段挂载，init_tracer() 无法替代这一步。
void install_grpc_server_metrics(grpc::ServerBuilder &builder);

// gRPC 客户端的请求级 metrics 适合在 channel 构建阶段统一挂载；
// 业务调用方不再关心指标对象、方法名解析或状态码属性组装。
void install_grpc_client_metrics(
        std::vector<std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>>
                &interceptors
);

// ---- Trace Span ------------------------------------------------------------

class trace_span
{
public:
    /// 创建一个新的根 Span（默认 internal，可指定 client/server）。
    /// 默认名字来自调用点的 source_location，避免继续依赖宏来拼文件和函数名。
    explicit trace_span(
            span_kind kind = span_kind::INTERNAL,
            std::source_location source = std::source_location::current()
    );
    /// gRPC handler 的常见入口：从 ServerContext 提取远端 Context，并创建 server span。
    explicit trace_span(
            const grpc::ServerContext &context,
            std::source_location source = std::source_location::current()
    );
    /// 从传入载体（如 gRPC metadata map）中提取远端 Context，并以其为父 Span 创建子 Span
    trace_span(
            const std::string &str,
            const std::map<std::string, std::string> &carrier,
            span_kind kind = span_kind::SERVER
    );
    ~trace_span();

    // 禁用拷贝，防止 Span 意外共享导致生命周期混乱
    trace_span(const trace_span &) = delete;
    auto operator=(const trace_span &) -> trace_span & = delete;

    // 允许移动，支持所有权转移
    trace_span(trace_span &&) noexcept;
    auto operator=(trace_span &&) noexcept -> trace_span &;

    auto before(const std::string &str) -> void;
    auto add_event(const std::string &name) -> void;
    auto after() -> void;
    auto discard() -> void;

private:
    class impl;
    std::unique_ptr<impl> impl_; // 使用 unique_ptr 管理 Pimpl，更高效且语义明确
};
