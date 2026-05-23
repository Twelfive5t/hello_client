#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/batch_span_processor_options.h"
#include "opentelemetry/sdk/trace/exporter.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/recordable.h"
#include "opentelemetry/sdk/trace/sampler.h"
#include "opentelemetry/sdk/trace/span_data.h"
#include "telemetry_internal.hpp"

#include <array>
#include <mutex>
#include <set>

namespace trace_sdk = opentelemetry::sdk::trace;

namespace
{

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
                return { .decision = opentelemetry::sdk::trace::Decision::DROP,
                         .attributes = {},
                         .trace_state = {} };
            }
        }
        // 默认行为：记录并采样 (RECORD_AND_SAMPLE)
        return { .decision = opentelemetry::sdk::trace::Decision::RECORD_AND_SAMPLE,
                 .attributes = {},
                 .trace_state = {} };
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
        if (wrapper == nullptr) {
            return;
        }
        std::string span_id = to_hex(wrapper->get_span_id());
        std::string parent_span_id = to_hex(wrapper->get_parent_span_id());

        std::lock_guard<std::mutex> lock(mutex_);
        active_spans_.insert(span_id);
        active_children_[parent_span_id].insert(span_id);
        if (dropped_spans_.contains(parent_span_id)) {
            mark_active_subtree_dropped(span_id);
        }
    }

    // Span 结束时调用：决定是立即导出、缓存等待父 Span 决定、还是丢弃
    void OnEnd(std::unique_ptr<opentelemetry::sdk::trace::Recordable> &&recordable
    ) noexcept override
    {
        auto *wrapper = dynamic_cast<wrapper_recordable *>(recordable.get());
        if (wrapper == nullptr) {
            next_->OnEnd(std::move(recordable));
            return;
        }

        std::string span_id = to_hex(wrapper->get_span_id());
        std::string parent_span_id = to_hex(wrapper->get_parent_span_id());

        bool should_drop = wrapper->should_drop();
        std::vector<std::unique_ptr<opentelemetry::sdk::trace::Recordable>> ready_to_export;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_spans_.erase(span_id);
            remove_active_link(span_id, parent_span_id);

            // 如果当前 Span 被标记为丢弃，则丢弃它及其所有已缓存的子 Span
            if (should_drop || dropped_spans_.contains(span_id)) {
                mark_active_subtree_dropped(span_id);
                drop_subtree(span_id);
                dropped_spans_.erase(span_id);
                return;
            }

            // 如果父 Span 还在活跃列表中，说明当前 Span 是某个未完成 Span 的子节点
            // 将其缓存起来，等待父 Span 结束时一起处理
            if (active_spans_.contains(parent_span_id)) {
                pending_children_[parent_span_id].push_back(std::move(recordable));
                return;
            }

            // 如果没有父 Span 或父 Span 已经结束（不活跃），则当前 Span 和缓存子树已经可以导出。
            // 下游 processor 可能也会加锁或做较重的工作，因此只在本地锁内搬移所有权，实际导出放到锁外。
            ready_to_export.push_back(std::move(recordable));
            collect_subtree(span_id, ready_to_export);
        }

        for (auto &ready : ready_to_export) {
            next_->OnEnd(std::move(ready));
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
    std::set<std::string> dropped_spans_;
    std::map<std::string, std::set<std::string>> active_children_;
    std::map<std::string, std::vector<std::unique_ptr<opentelemetry::sdk::trace::Recordable>>>
            pending_children_;

    [[nodiscard]] static std::string to_hex(const opentelemetry::trace::SpanId &span_id)
    {
        static constexpr size_t span_id_hex_len = 16; // SpanId is 8 bytes → 16 hex chars
        std::array<char, span_id_hex_len> buf{};
        span_id.ToLowerBase16(buf);
        return { buf.data(), buf.size() };
    }

    // 递归丢弃指定 Span ID 的所有子 Span
    void drop_subtree(const std::string &span_id) // NOLINT(misc-no-recursion)
    {
        auto it = pending_children_.find(span_id);
        if (it != pending_children_.end()) {
            for (auto &child : it->second) {
                auto *wrapper = dynamic_cast<wrapper_recordable *>(child.get());
                if (wrapper == nullptr) {
                    continue;
                }
                const auto child_id = to_hex(wrapper->get_span_id());
                mark_active_subtree_dropped(child_id);
                drop_subtree(child_id);
            }
            pending_children_.erase(it);
        }
    }

    void mark_active_subtree_dropped(const std::string &span_id) // NOLINT(misc-no-recursion)
    {
        dropped_spans_.insert(span_id);

        auto it = active_children_.find(span_id);
        if (it == active_children_.end()) {
            return;
        }

        for (const auto &child_id : it->second) {
            mark_active_subtree_dropped(child_id);
        }
    }

    void remove_active_link(const std::string &span_id, const std::string &parent_span_id)
    {
        auto children = active_children_.find(parent_span_id);
        if (children != active_children_.end()) {
            children->second.erase(span_id);
            if (children->second.empty()) {
                active_children_.erase(children);
            }
        }
    }

    // 递归收集指定 Span ID 的所有子 Span；调用方必须持有 mutex_。
    // NOLINTNEXTLINE(misc-no-recursion)
    void collect_subtree(
            const std::string &span_id,
            std::vector<std::unique_ptr<opentelemetry::sdk::trace::Recordable>> &ready_to_export
    )
    {
        auto it = pending_children_.find(span_id);
        if (it != pending_children_.end()) {
            auto children = std::move(it->second);
            pending_children_.erase(it);

            for (auto &child : children) {
                auto *wrapper = dynamic_cast<wrapper_recordable *>(child.get());
                std::string child_id;
                if (wrapper != nullptr) {
                    child_id = to_hex(wrapper->get_span_id());
                }

                ready_to_export.push_back(std::move(child));

                if (!child_id.empty()) {
                    collect_subtree(child_id, ready_to_export);
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

        for (const auto &span : spans) {
            if (dynamic_cast<wrapper_recordable *>(span.get()) == nullptr) {
                return exporter_->Export(spans);
            }
        }

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

} // namespace

namespace telemetry_internal
{

auto make_ignore_sampler(std::vector<std::string> ignored_names
) -> std::unique_ptr<opentelemetry::sdk::trace::Sampler>
{
    return std::make_unique<ignore_sampler>(std::move(ignored_names));
}

auto make_subtree_discard_batch_processor(
        std::unique_ptr<opentelemetry::sdk::trace::SpanExporter> exporter
) -> std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>
{
    auto filtering_exp = std::make_unique<filtering_exporter>(std::move(exporter));

    trace_sdk::BatchSpanProcessorOptions options{};
    // options.max_queue_size = 10000;                                // 调大队列以容纳测试的所有 Span
    // options.schedule_delay_millis = std::chrono::milliseconds(100); // 加快发送频率
    auto batch_processor =
            trace_sdk::BatchSpanProcessorFactory::Create(std::move(filtering_exp), options);
    return std::make_unique<subtree_discard_span_processor>(std::move(batch_processor));
}

} // namespace telemetry_internal
