#include "telemetry_internal.hpp"

#include "opentelemetry/metrics/provider.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"

#include <array>
#include <string_view>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace trace = opentelemetry::trace;
namespace metrics_sdk = opentelemetry::sdk::metrics;

namespace telemetry_internal
{

auto global_meter_provider() -> std::shared_ptr<metrics_sdk::MeterProvider> &
{
    static std::shared_ptr<metrics_sdk::MeterProvider> provider;
    return provider;
}

auto global_tracer() -> opentelemetry::nostd::shared_ptr<trace::Tracer> &
{
    static opentelemetry::nostd::shared_ptr<trace::Tracer> tracer;
    return tracer;
}

auto service_name() -> std::string &
{
    static std::string name = "telemetry_demo";
    return name;
}

#ifdef __linux__
scoped_thread_affinity::scoped_thread_affinity(const std::vector<int> &cpus)
{
    if (cpus.empty()) {
        return;
    }

    if (pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &original_cpuset_) != 0) {
        return;
    }

    cpu_set_t new_cpuset;
    CPU_ZERO(&new_cpuset);
    bool has_valid_cpu = false;
    for (int cpu : cpus) {
        if (cpu >= 0 && cpu < CPU_SETSIZE) {
            CPU_SET(cpu, &new_cpuset);
            has_valid_cpu = true;
        }
    }

    if (has_valid_cpu &&
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &new_cpuset) == 0) {
        restore_affinity_ = true;
    }
}

scoped_thread_affinity::~scoped_thread_affinity()
{
    if (restore_affinity_) {
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &original_cpuset_);
    }
}
#endif

auto host_name() -> std::string
{
#ifdef _WIN32
    std::array<char, MAX_COMPUTERNAME_LENGTH + 1> buffer{};
    auto size = static_cast<DWORD>(buffer.size());
    if (GetComputerNameA(buffer.data(), &size) != 0 && size > 0) {
        return { buffer.data(), size };
    }
#else
    std::array<char, 256> buffer{};
    if (gethostname(buffer.data(), buffer.size()) == 0 && buffer.front() != '\0') {
        buffer.back() = '\0';
        return buffer.data();
    }
#endif
    return "unknown";
}

auto metadata_to_map(const grpc::ServerContext &context) -> std::map<std::string, std::string>
{
    // gRPC metadata 的底层存储受 ServerContext 生命周期约束；
    // 这里复制成稳定的 map，后续 trace 提取逻辑就不需要知道 gRPC 的容器细节。
    std::map<std::string, std::string> metadata;
    for (const auto &entry : context.client_metadata()) {
        metadata.emplace(
                std::string(entry.first.data(), entry.first.size()),
                std::string(entry.second.data(), entry.second.size())
        );
    }
    return metadata;
}

auto short_function_name(std::string_view function_name) -> std::string_view
{
    const auto params_pos = function_name.find('(');
    if (params_pos != std::string_view::npos) {
        function_name = function_name.substr(0, params_pos);
    }

    const auto scope_pos = function_name.rfind("::");
    if (scope_pos != std::string_view::npos) {
        function_name = function_name.substr(scope_pos + 2);
    }

    const auto space_pos = function_name.rfind(' ');
    if (space_pos != std::string_view::npos) {
        function_name = function_name.substr(space_pos + 1);
    }

    return function_name;
}

auto format_span_name(const std::source_location &source) -> std::string
{
    // 对外暴露的是“在这里进入了一个 RPC handler”的定位信息；
    // 用 source_location 统一生成名字，可以保留 trace_span 既有的可读性，同时避免业务层手写 span 名称。
    return std::string(source.file_name()) + ":" + std::to_string(source.line()) + ", " +
           std::string(short_function_name(source.function_name()));
}

map_text_carrier::map_text_carrier(const std::map<std::string, std::string> &m) : read_map(&m)
{
}

map_text_carrier::map_text_carrier(std::map<std::string, std::string> &m) : write_map(&m)
{
}

auto map_text_carrier::Get(opentelemetry::nostd::string_view key
) const noexcept -> opentelemetry::nostd::string_view
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

void map_text_carrier::Set(
        opentelemetry::nostd::string_view key,
        opentelemetry::nostd::string_view value
) noexcept
{
    if (write_map != nullptr) {
        (*write_map)[std::string(key)] = std::string(value);
    }
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

} // namespace telemetry_internal
