#pragma once

#include <lumora/Debug/FrameCounters.h>
#include <lumora/Detail/Reactive.h>
#include <lumora/Reactive/Tracking.h>

#include <cstddef>
#include <functional>
#include <vector>

namespace lumora {

class Owner;

class FrameCallback final : public RefCounted {
public:
    FrameFn fn;
    bool    alive = true;
};

/// One reactive universe, owned by a MountedApp; ambient via GraphScope during Tick / Dispatch / Mount.
/// Single-threaded by contract: everything touching a graph runs on the thread that ticks it.
class ReactiveGraph final : public RefCounted {
public:
    [[nodiscard]] static ReactiveGraph* Ambient() noexcept { return s_ambient; }

    /// Fires frame callbacks, compacts dead ones, then drains the deferred queue.
    void Tick(float deltaSeconds);

    void PushNode(detail::ReactiveNode* node) { m_nodeStack.push_back(node); }
    void PopNode() { m_nodeStack.pop_back(); }
    [[nodiscard]] detail::ReactiveNode* CurrentNode() const noexcept {
        return m_nodeStack.empty() ? nullptr : m_nodeStack.back();
    }

    [[nodiscard]] Owner* CurrentOwner() const noexcept { return m_currentOwner; }
    void SetCurrentOwner(Owner* owner) noexcept { m_currentOwner = owner; }

    [[nodiscard]] Ref<FrameCallback> AddFrameCallback(FrameFn fn);

    /// Attach the owning app's per-frame counter block. Null (default) costs one branch per counted path.
    void SetCounters(FrameCounters* counters) noexcept { m_counters = counters; }
    [[nodiscard]] FrameCounters* Counters() const noexcept { return m_counters; }

    /// Registered frame callbacks, dead ones included; lets a test assert a finished tween leaves none behind.
    [[nodiscard]] std::size_t FrameCallbackCount() const noexcept { return m_frameCallbacks.size(); }

    /// Queue work to run at the next Tick. Reserved for async producers; nothing enqueues here yet.
    void Defer(std::function<void()> fn) { m_deferred.push_back(std::move(fn)); }

    /// Apply a structural live-tree edit (Show/Each/Memo splice or dispose): immediately unless a dispatch/paint walk
    /// is in progress, else queued for the next Tick. Makes disposing a subtree from inside its own effect safe.
    void ApplyStructuralEdit(std::function<void()> edit);

    /// Open/close a region where structural edits defer (a walk iterating the live tree). Re-entrant; see DeferStructuralScope.
    void BeginDeferStructural() noexcept { ++m_deferDepth; }
    void EndDeferStructural() noexcept { --m_deferDepth; }
    [[nodiscard]] bool DeferringStructural() const noexcept { return m_deferDepth > 0; }

private:
    friend class GraphScope;

    void DrainStructuralEdits();

    std::vector<detail::ReactiveNode*> m_nodeStack;
    Owner* m_currentOwner = nullptr;
    std::vector<Ref<FrameCallback>> m_frameCallbacks;
    std::vector<std::function<void()>> m_deferred;
    std::vector<std::function<void()>> m_structuralEdits;
    int m_deferDepth = 0;
    float m_frameAccumulator = 0.f; // leftover real time between fixed 1/60 s animation steps
    FrameCounters* m_counters = nullptr;

    inline static thread_local ReactiveGraph* s_ambient = nullptr;
};

/// RAII guard pairing Begin/EndDeferStructural: edits queue and drain at the next Tick instead of mutating mid-walk.
class DeferStructuralScope {
public:
    explicit DeferStructuralScope(ReactiveGraph& graph) noexcept : m_graph(graph) {
        m_graph.BeginDeferStructural();
    }
    ~DeferStructuralScope() noexcept { m_graph.EndDeferStructural(); }

    DeferStructuralScope(const DeferStructuralScope&) = delete;
    DeferStructuralScope& operator=(const DeferStructuralScope&) = delete;

private:
    ReactiveGraph& m_graph;
};

/// RAII: makes a graph ambient for the current scope; restores the previous one on exit.
class GraphScope {
public:
    explicit GraphScope(ReactiveGraph& graph) noexcept : m_previous(ReactiveGraph::s_ambient) {
        ReactiveGraph::s_ambient = &graph;
    }
    ~GraphScope() noexcept { ReactiveGraph::s_ambient = m_previous; }

    GraphScope(const GraphScope&) = delete;
    GraphScope& operator=(const GraphScope&) = delete;

private:
    ReactiveGraph* m_previous;
};

} // namespace lumora
