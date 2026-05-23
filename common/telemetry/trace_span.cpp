#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/trace/context.h"
#include "telemetry_internal.hpp"

namespace trace = opentelemetry::trace;
namespace nostd = opentelemetry::nostd;

using telemetry_internal::format_span_name;
using telemetry_internal::global_tracer;
using telemetry_internal::map_text_carrier;
using telemetry_internal::metadata_to_map;
using telemetry_internal::to_otel_kind;

// trace_span 的 PIMPL 实现：持有 OTel Span 和 Scope，负责 Span 的创建、激活和结束
class trace_span::impl
{
public:
    explicit impl(const std::string &str, opentelemetry::trace::SpanKind kind)
        // trace::Scope 是 RAII 对象且不可移动，必须在初始化列表中构造
        // StartSpan: 开始一个新的 Span
        // WithActiveSpan: 将该 Span 设为当前线程的活跃 Span，作用域结束时自动恢复上一个 Span
        : span_(make_root_span(str, kind)),
          outer_scope_(global_tracer()->WithActiveSpan(span_)) // NOLINT
    {
        before(str);
    }

    impl(const std::string &str,
         const std::map<std::string, std::string> &carrier,
         opentelemetry::trace::SpanKind kind)
        : span_(make_span_from_carrier(str, carrier, kind)),
          outer_scope_(global_tracer()->WithActiveSpan(span_)) // NOLINT
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

    auto add_event(const std::string &name) -> void
    {
        span_->AddEvent(name);
    }

    auto after() -> void
    {
        if (ended_) {
            return;
        }
        span_->End();
        ended_ = true;
    }

    auto discard() -> void
    {
        span_->SetAttribute("manual_drop", true);
    }

private:
    static auto make_root_span(const std::string &str, opentelemetry::trace::SpanKind kind)
            -> nostd::shared_ptr<opentelemetry::trace::Span>
    {
        opentelemetry::trace::StartSpanOptions options;
        options.kind = kind;
        return global_tracer()->StartSpan(str, {}, options);
    }

    static auto make_span_from_carrier(
            const std::string &str,
            const std::map<std::string, std::string> &carrier,
            opentelemetry::trace::SpanKind kind
    ) -> nostd::shared_ptr<opentelemetry::trace::Span>
    {
        auto propagator =
                opentelemetry::context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
        map_text_carrier mc(carrier);
        auto current_ctx = opentelemetry::context::RuntimeContext::GetCurrent();
        auto remote_ctx = propagator->Extract(mc, current_ctx);
        auto parent_span = opentelemetry::trace::GetSpan(remote_ctx);
        opentelemetry::trace::StartSpanOptions options;
        options.parent = parent_span->GetContext();
        options.kind = kind;
        return global_tracer()->StartSpan(str, {}, options);
    }

    nostd::shared_ptr<opentelemetry::trace::Span> span_;
    trace::Scope outer_scope_;
    bool ended_ = false;
};

auto trace_span::before(const std::string &str) -> void
{
    if (!impl_) {
        return;
    }
    impl_->before(str);
}

auto trace_span::add_event(const std::string &name) -> void
{
    if (!impl_) {
        return;
    }
    impl_->add_event(name);
}

auto trace_span::after() -> void
{
    if (!impl_) {
        return;
    }
    impl_->after();
}

auto trace_span::discard() -> void
{
    if (!impl_) {
        return;
    }
    impl_->discard();
}

trace_span::trace_span(span_kind kind, std::source_location source)
    : impl_(std::make_unique<impl>(format_span_name(source), to_otel_kind(kind)))
{
}

trace_span::trace_span(const grpc::ServerContext &context, std::source_location source)
    : impl_(std::make_unique<impl>(
              format_span_name(source),
              metadata_to_map(context),
              trace::SpanKind::kServer
      ))
{
}

trace_span::trace_span(
        const std::string &str,
        const std::map<std::string, std::string> &carrier,
        span_kind kind
)
    : impl_(std::make_unique<impl>(str, carrier, to_otel_kind(kind)))
{
}

trace_span::~trace_span() = default;

trace_span::trace_span(trace_span &&) noexcept = default;
auto trace_span::operator=(trace_span &&) noexcept -> trace_span & = default;
