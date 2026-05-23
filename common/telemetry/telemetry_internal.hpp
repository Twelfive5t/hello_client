#pragma once

#include "opentelemetry/context/propagation/global_propagator.h"
#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"
#include "opentelemetry/sdk/trace/exporter.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/sampler.h"
#include "opentelemetry/trace/tracer.h"
#include "telemetry.hpp"

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif
#include <map>
#include <memory>
#include <source_location>
#include <string>
#include <vector>

namespace telemetry_internal
{

#ifdef __linux__
class scoped_thread_affinity
{
public:
    explicit scoped_thread_affinity(const std::vector<int> &cpus);
    ~scoped_thread_affinity();

    scoped_thread_affinity(const scoped_thread_affinity &) = delete;
    auto operator=(const scoped_thread_affinity &) -> scoped_thread_affinity & = delete;
    scoped_thread_affinity(scoped_thread_affinity &&) = delete;
    auto operator=(scoped_thread_affinity &&) -> scoped_thread_affinity & = delete;

private:
    cpu_set_t original_cpuset_{};
    bool restore_affinity_ = false;
};
#endif

struct map_text_carrier : public opentelemetry::context::propagation::TextMapCarrier {
    const std::map<std::string, std::string> *read_map = nullptr;
    std::map<std::string, std::string> *write_map = nullptr;

    explicit map_text_carrier(const std::map<std::string, std::string> &m);
    explicit map_text_carrier(std::map<std::string, std::string> &m);

    [[nodiscard]] opentelemetry::nostd::string_view Get(opentelemetry::nostd::string_view key
    ) const noexcept override;

    void Set(
            opentelemetry::nostd::string_view key,
            opentelemetry::nostd::string_view value
    ) noexcept override;
};

auto global_meter_provider() -> std::shared_ptr<opentelemetry::sdk::metrics::MeterProvider> &;
auto global_tracer() -> opentelemetry::nostd::shared_ptr<opentelemetry::trace::Tracer> &;
void reset_grpc_metric_instruments();
auto host_name() -> std::string;
auto service_name() -> std::string &;
auto metadata_to_map(const grpc::ServerContext &context) -> std::map<std::string, std::string>;
auto format_span_name(const std::source_location &source) -> std::string;
auto to_otel_kind(span_kind kind) -> opentelemetry::trace::SpanKind;
auto make_ignore_sampler(std::vector<std::string> ignored_names
) -> std::unique_ptr<opentelemetry::sdk::trace::Sampler>;
auto make_subtree_discard_batch_processor(
        std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> exporter
) -> std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>;

} // namespace telemetry_internal
