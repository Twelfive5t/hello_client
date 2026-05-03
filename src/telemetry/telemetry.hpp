#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#define FILE_LINE (__FILE__ + std::string(":") + std::to_string(__LINE__))
#define FILE_LINE_FUNC                                                                             \
    (__FILE__ + std::string(":") + std::to_string(__LINE__) + ", " + std::string(__FUNCTION__))

#define TRACE_POINT trace_span trace_point_instance(FILE_LINE_FUNC);
#define TRACE_POINT_CLIENT trace_span trace_point_instance(FILE_LINE_FUNC, span_kind::CLIENT);
#define TRACE_POINT_ADD trace_point_instance.add_event(FILE_LINE_FUNC);
#define TRACE_POINT_ADD_MSG(msg) trace_point_instance.add_event(msg);
#define TRACE_POINT_DISCARD trace_point_instance.discard();
// 从传入的 key-value 载体（如 gRPC server metadata map）提取远端 Context，作为当前 Span 的父 Span
#define TRACE_POINT_WITH_CONTEXT(carrier)                                                          \
    trace_span trace_point_instance(FILE_LINE_FUNC, (carrier), span_kind::SERVER);

// Span 类型，影响 Jaeger 等后端的可视化颜色和布局
enum class span_kind : std::uint8_t { INTERNAL, CLIENT, SERVER };

struct telemetry_config {
    std::string service_name = "telemetry_demo";
    std::string service_instance_id; // Empty defaults to auto-generated
    std::string endpoint = "localhost:4317";
    std::string version = "0.0.1";
    std::string environment = "test";
    std::vector<std::string> ignored_spans;

    // 后台线程配置 (解决 gRPC 线程继承 RT 亲和性的问题)
    std::vector<int> background_cpu_affinity = {0
    }; // 指定后台线程绑定的 CPU 核，为空则不修改 (默认绑定到核 0)
};

// 初始化 Tracer，支持传入配置
void init_tracer(const telemetry_config &config = telemetry_config{});
void cleanup_tracer();

// 将当前活跃 Span 的 Trace Context 序列化为 W3C traceparent/tracestate header，
// 用于注入到对外 RPC 调用（如 gRPC client metadata）中。
auto get_trace_headers() -> std::map<std::string, std::string>;

class trace_span
{
public:
    /// 创建一个新的根 Span（默认 internal，可指定 client/server）
    explicit trace_span(const std::string &str = FILE_LINE, span_kind kind = span_kind::INTERNAL);
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
