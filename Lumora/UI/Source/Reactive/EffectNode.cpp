#include "ReactiveGraph.h"

#include <lumora/Detail/Reactive.h>

#include <lumora/log/Log.h>

#include <cstddef>
#include <vector>

LUMORA_LOG_CATEGORY("Lumora")

namespace lumora::detail {

namespace {

[[nodiscard]] constexpr std::uint8_t Rank(NodeColor c) noexcept {
    return static_cast<std::uint8_t>(c);
}

// Flush coordinator: the queue and batch depth are thread-global, so a signal shared between two apps on one thread
// invalidates effects in both. The queue owns Refs: a queued effect may be disposed before the flush reaches it.

thread_local int                              g_batchDepth = 0;
thread_local bool                             g_flushing   = false;
thread_local std::vector<Ref<ReactiveNode>>   g_queue;

/// Ceiling on one flush: effects that write each other's sources would otherwise refill the queue forever (silent hang).
constexpr std::size_t kMaxFlushSteps = 1u << 16;

void ScheduleAutoFlush() {
    if (g_batchDepth == 0) {
        FlushEffects();
    }
}

} // namespace

void Enqueue(ReactiveNode& node) {
    if (!node.m_queued) {
        node.m_queued = true;
        g_queue.emplace_back(&node);
    }
}

void BeginBatch() noexcept { ++g_batchDepth; }

void EndBatch() {
    if (--g_batchDepth == 0) {
        FlushEffects();
    }
}

void FlushEffects() {
    if (g_flushing) {
        return; // a write fired inside the flush; the running loop drains the new work
    }
    g_flushing = true;

    // Unwinds on a throwing effect body too: the queue must not stay latched (nothing would ever
    // flush again) and every node left in it must be re-queueable, or its later writes go unnoticed.
    struct FlushGuard {
        ~FlushGuard() {
            for (const Ref<ReactiveNode>& queued : g_queue) {
                queued->m_queued = false;
            }
            g_queue.clear();
            g_flushing = false;
        }
    } guard;

    for (std::size_t i = 0; i < g_queue.size(); ++i) {
        if (i == kMaxFlushSteps) {
            LOG_ERROR("Reactive flush hit its {}-step ceiling with {} node(s) still queued: effects are "
                      "writing each other's sources. Dropping the rest of this flush.",
                      kMaxFlushSteps, g_queue.size() - i);
            break;
        }
        const Ref<ReactiveNode> node = g_queue[i]; // by value: the flush can grow (and reallocate) g_queue
        node->m_queued = false;
        if (node->m_alive) {
            if (const ReactiveGraph* graph = node->m_graph.Get()) {
                if (FrameCounters* counters = graph->Counters()) {
                    ++counters->effectsFlushed;
                }
            }
            node->UpdateIfNecessary();
        }
    }
}

void Track(ReactiveNode& source) noexcept {
    ReactiveGraph* graph = ReactiveGraph::Ambient();
    if (!graph) {
        return;
    }
    ReactiveNode* observer = graph->CurrentNode();
    if (!observer || observer == &source) {
        return;
    }
    observer->AddSource(source);
}


ReactiveNode::ReactiveNode() : m_graph(ReactiveGraph::Ambient()) {}

ReactiveNode::~ReactiveNode() {
    // Destruction means refcount hit zero, so no observer still references us - only our own source
    // edges remain to sever (we hold their Refs, so they are alive for the unlink).
    if (!m_sources.empty()) {
        TruncateSourcesFrom(0);
    }
}

void ReactiveNode::AddSource(ReactiveNode& source) {
    // Reading the same source twice in a row is the common idiom (`sig(); sig();`) - collapse it.
    if (m_track > 0 && m_sources[m_track - 1].Get() == &source) {
        return;
    }
    if (m_track < m_sources.size()) {
        if (m_sources[m_track].Get() == &source) {
            ++m_track; // unchanged dependency at this position: reuse the edge, touch no heap
            return;
        }
        // The dependency set diverged here; drop the stale tail, then append the new edge.
        TruncateSourcesFrom(m_track);
    }

    const auto srcIndex = static_cast<std::uint32_t>(m_sources.size());
    const auto obsIndex = static_cast<std::uint32_t>(source.m_observers.size());
    m_sources.emplace_back(&source);
    m_sourceSlots.push_back(obsIndex);
    source.m_observers.push_back(this);
    source.m_observerSlots.push_back(srcIndex);
    ++m_track;
}

void ReactiveNode::UnlinkSource(std::uint32_t index) {
    ReactiveNode*       src    = m_sources[index].Get();
    const std::uint32_t obsIdx = m_sourceSlots[index];

    const auto last = static_cast<std::uint32_t>(src->m_observers.size()) - 1;
    if (obsIdx != last) {
        ReactiveNode*       moved       = src->m_observers[last];
        const std::uint32_t movedSrcIdx = src->m_observerSlots[last];
        src->m_observers[obsIdx]            = moved;
        src->m_observerSlots[obsIdx]        = movedSrcIdx;
        moved->m_sourceSlots[movedSrcIdx]   = obsIdx; // repair the moved observer's back-reference
    }
    src->m_observers.pop_back();
    src->m_observerSlots.pop_back();
}

void ReactiveNode::TruncateSourcesFrom(std::uint32_t index) {
    for (auto j = static_cast<std::uint32_t>(m_sources.size()); j-- > index;) {
        UnlinkSource(j);
    }
    m_sources.resize(index);
    m_sourceSlots.resize(index);
}

void ReactiveNode::RunInitial() {
    m_state = NodeColor::Dirty;
    Execute();
}

void ReactiveNode::Execute() {
    if (!m_alive || m_running || !m_run) {
        m_state = NodeColor::Clean;
        return;
    }
    const Ref<ReactiveNode> self(this);

    // Both scopes unwind if the body throws: leaking `m_running` leaves the node inert, and leaking the observer stack
    // makes later signal reads subscribe this dead node instead of the real observer.
    struct RunScope {
        ReactiveNode& node;
        ~RunScope() {
            node.TruncateSourcesFrom(node.m_track); // sever dependencies not re-read this run
            node.m_running = false;
            node.m_state   = NodeColor::Clean;
        }
    } runScope{*this};

    m_running      = true;
    m_track        = 0;
    m_valueChanged = false;

    if (m_graph) {
        if (FrameCounters* counters = m_graph->Counters()) {
            ++counters->effectRuns;
        }
        GraphScope ambient(*m_graph);
        Owner* previousOwner = m_graph->CurrentOwner();
        m_graph->PushNode(this);
        struct StackScope {
            ReactiveGraph& graph;
            Owner*         previous;
            ~StackScope() {
                graph.PopNode();
                graph.SetCurrentOwner(previous);
            }
        } stackScope{*m_graph, previousOwner};
        if (m_owner) {
            m_graph->SetCurrentOwner(m_owner);
        }
        m_run();
    } else {
        m_run();
    }
}

void ReactiveNode::MarkStale(NodeColor state) {
    if (Rank(m_state) >= Rank(state)) {
        return;
    }
    const bool wasClean = (m_state == NodeColor::Clean);
    m_state = state;
    if (wasClean) {
        for (ReactiveNode* observer : m_observers) {
            observer->MarkStale(NodeColor::Check);
        }
        // An effect re-runs from the queue; the running guard drops a node's write to a source it itself reads (re-entrant self-trigger).
        if (m_effect && m_alive && !m_running) {
            Enqueue(*this);
        }
    }
}

void ReactiveNode::NotifySource() {
    for (ReactiveNode* observer : m_observers) {
        observer->MarkStale(NodeColor::Dirty);
    }
    ScheduleAutoFlush();
}

void ReactiveNode::UpdateIfNecessary() {
    if (m_state == NodeColor::Clean) {
        return;
    }
    if (m_state == NodeColor::Check) {
        for (const Ref<ReactiveNode>& source : m_sources) {
            source->UpdateIfNecessary();
            if (m_state == NodeColor::Dirty) {
                break; // a source's recompute painted us Dirty - no need to poll the rest
            }
        }
    }
    if (m_state == NodeColor::Dirty) {
        Execute();
        if (m_valueChanged) {
            for (ReactiveNode* observer : m_observers) {
                if (Rank(observer->m_state) < Rank(NodeColor::Dirty)) {
                    observer->m_state = NodeColor::Dirty;
                }
            }
        }
    }
    m_state = NodeColor::Clean;
}

void ReactiveNode::Dispose() {
    if (!m_alive) {
        return;
    }
    m_alive = false;
    TruncateSourcesFrom(0);
    m_run   = nullptr;
    m_owner = nullptr;
    m_state = NodeColor::Clean;
    // A node disposed while queued keeps its slot (the queue holds a Ref, so the slot is safe); drop
    // the latch so a resurrected-then-rewritten source can still schedule it.
    m_queued = false;
}


UntrackedScope::UntrackedScope() : m_graph(ReactiveGraph::Ambient()) {
    if (m_graph) {
        m_graph->PushNode(nullptr);
    }
}

UntrackedScope::~UntrackedScope() {
    if (m_graph) {
        m_graph->PopNode();
    }
}

} // namespace lumora::detail
