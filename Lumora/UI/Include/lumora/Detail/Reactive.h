#pragma once

#include <lumora/Api.h>
#include <lumora/Foundation/Fn.h>
#include <lumora/Ref.h>

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace lumora {

class Owner;
class ReactiveGraph;

}

namespace lumora::detail {

using VoidFn = MoveFn<void()>;

/// Glitch-resolution state, ordered Clean < Check < Dirty: a write marks direct observers Dirty and
/// transitive ones Check; a pull recomputes only nodes a value actually reached.
enum class NodeColor : std::uint8_t { Clean, Check, Dirty };

/// The reactive graph node: a signal source (no `compute`), a Computed, or an Effect (`effect` flag, queued when non-Clean).
/// Edges are intrusive and bidirectional: observers hold a Ref to each source, sources hold raw back-pointers
/// (observers always unlink before dying). Re-runs diff the source list, so stable deps touch no heap.
class LUMORA_API ReactiveNode : public RefCounted {
public:
    ReactiveNode();
    ~ReactiveNode() override;

    ReactiveNode(const ReactiveNode&)            = delete;
    ReactiveNode& operator=(const ReactiveNode&) = delete;

    /// Install the tracked body; a node without one is a pure source.
    void SetComputation(VoidFn run) { m_run = std::move(run); }

    /// Mark as Effect: scheduled on the flush queue when it leaves Clean (a Computed recomputes only when pulled).
    void MakeEffect() noexcept { m_effect = true; }

    void RunInitial();

    void Execute();

    /// Mark observers Dirty/Check and schedule any effect reached. Called by a source on write.
    void NotifySource();

    void UpdateIfNecessary();

    /// Sever every source edge. Idempotent.
    void Dispose();

    void AddSource(ReactiveNode& source);

    [[nodiscard]] bool Alive() const noexcept { return m_alive; }
    [[nodiscard]] bool HasSources() const noexcept { return !m_sources.empty(); }

    /// Stamped by Owner::Own(); Execute() re-establishes it so Use()/Provide() resolve on re-runs.
    void SetOwner(Owner* owner) noexcept { m_owner = owner; }

    /// Called by a Computed's recompute when the value changed, so the pull walk marks observers Dirty.
    void MarkValueChanged() noexcept { m_valueChanged = true; }

private:
    friend LUMORA_API void Track(ReactiveNode& source) noexcept;
    friend LUMORA_API void Enqueue(ReactiveNode& node);
    friend LUMORA_API void FlushEffects();

    void MarkStale(NodeColor state);
    void TruncateSourcesFrom(std::uint32_t index);
    void UnlinkSource(std::uint32_t index);

    VoidFn         m_run;
    Owner*         m_owner = nullptr;
    Ref<ReactiveGraph> m_graph;

    std::vector<Ref<ReactiveNode>> m_sources;     ///< what this observer read last run (strong)
    std::vector<std::uint32_t>     m_sourceSlots; ///< our index inside each source's observer list
    std::vector<ReactiveNode*>     m_observers;   ///< who depends on this source (weak back-refs)
    std::vector<std::uint32_t>     m_observerSlots;///< each observer's index inside its source list

    std::uint32_t m_track = 0; ///< diff cursor into m_sources during a run

    NodeColor m_state = NodeColor::Clean;
    bool      m_alive        = true;
    bool      m_running      = false;
    bool      m_effect       = false;
    bool      m_queued       = false;
    bool      m_valueChanged = false;
};

/// Subscribe the running observer to `source`; no-op outside reactive execution or in an UntrackedScope.
LUMORA_API void Track(ReactiveNode& source) noexcept;

LUMORA_API void Enqueue(ReactiveNode& node);

/// Open/close a batch: writes coalesce, effects flush when the outermost batch ends. Re-entrant.
LUMORA_API void BeginBatch() noexcept;
LUMORA_API void EndBatch();

/// Drain the flush queue now (each effect in its own graph). Re-entrant-safe.
LUMORA_API void FlushEffects();

/// RAII: reads in this scope do not subscribe the enclosing effect/computed.
class LUMORA_API UntrackedScope {
public:
    UntrackedScope();
    ~UntrackedScope();
    UntrackedScope(const UntrackedScope&)            = delete;
    UntrackedScope& operator=(const UntrackedScope&) = delete;

private:
    ReactiveGraph* m_graph;
};

}
