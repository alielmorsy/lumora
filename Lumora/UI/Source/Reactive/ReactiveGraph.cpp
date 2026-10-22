#include "ReactiveGraph.h"

#include <lumora/Detail/Reactive.h>
#include <lumora/Reactive/Context.h>
#include <lumora/Reactive/Effect.h>
#include <lumora/Reactive/Owner.h>

#include <lumora/log/Log.h>

#include <algorithm>
#include <atomic>
#include <cstddef>

LUMORA_LOG_CATEGORY("Lumora")

namespace lumora {

void ReactiveGraph::Tick(float deltaSeconds) {
    GraphScope ambient(*this);
    // RAII: a throwing frame callback would otherwise leave batch depth above zero, and no effect would ever flush again.
    detail::BeginBatch(); // coalesce a frame's writes into a single effect flush
    struct BatchScope {
        ~BatchScope() { detail::EndBatch(); } // flush effects woken this frame
    } batchScope;

    DrainStructuralEdits(); // pending splices/disposes from the previous frame's dispatch

    // Fixed-timestep
    constexpr float kFixedStep      = 1.f / 60.f;
    constexpr float kMaxAccumulator = 0.1f;
    m_frameAccumulator += deltaSeconds;
    if (m_frameAccumulator > kMaxAccumulator) {
        m_frameAccumulator = kMaxAccumulator;
    }

    // Indexed, each callback held by value: a callback may register another (Tween, Timeline, Spring), reallocating the vector.
    while (m_frameAccumulator >= kFixedStep) {
        m_frameAccumulator -= kFixedStep;
        if (m_counters) {
            ++m_counters->framePasses;
        }
        const std::size_t registered = m_frameCallbacks.size();
        for (std::size_t i = 0; i < registered; ++i) {
            const Ref<FrameCallback> callback = m_frameCallbacks[i];
            if (callback->alive) {
                if (m_counters) {
                    ++m_counters->frameCallbacksInvoked;
                }
                callback->fn(kFixedStep);
            }
        }
    }
    const std::size_t beforeReap = m_frameCallbacks.size();
    std::erase_if(m_frameCallbacks, [](const Ref<FrameCallback>& callback) {
        return !callback->alive;
    });
    if (m_counters) {
        m_counters->frameCallbacksReaped += static_cast<std::uint32_t>(beforeReap - m_frameCallbacks.size());
        m_counters->frameCallbacksAlive = static_cast<std::uint32_t>(m_frameCallbacks.size());
    }

    auto deferred = std::move(m_deferred);
    m_deferred.clear();
    if (m_counters) {
        m_counters->deferredRun += static_cast<std::uint32_t>(deferred.size());
    }
    for (auto& fn : deferred) {
        fn();
    }
}

void ReactiveGraph::ApplyStructuralEdit(std::function<void()> edit) {
    // Also queue behind pending edits: a host write between dispatch and Tick would otherwise apply now
    // and then be overwritten by an older edit drained after it.
    if (m_deferDepth > 0 || !m_structuralEdits.empty()) {
        m_structuralEdits.push_back(std::move(edit));
    } else {
        edit();
    }
}

void ReactiveGraph::DrainStructuralEdits() {
    // An edit may queue further edits while it runs (nested control flow); loop until quiet.
    // The drain itself is unguarded, so edits run immediately as they dequeue.
    while (!m_structuralEdits.empty()) {
        auto batch = std::move(m_structuralEdits);
        m_structuralEdits.clear();
        if (m_counters) {
            m_counters->structuralEdits += static_cast<std::uint32_t>(batch.size());
        }
        for (auto& edit : batch) {
            edit();
        }
    }
}

Ref<FrameCallback> ReactiveGraph::AddFrameCallback(FrameFn fn) {
    auto callback = MakeRef<FrameCallback>();
    callback->fn = std::move(fn);
    m_frameCallbacks.push_back(callback);
    return callback;
}


Owner::Owner() : m_graph(ReactiveGraph::Ambient()) {
    if (m_graph) {
        m_parent = m_graph->CurrentOwner();
        m_graph->SetCurrentOwner(this);
        m_pushed = true;
    } else {
        LOG_WARN("Owner created with no ambient ReactiveGraph; it will never be current");
    }
}

Owner::Owner(DormantTag) noexcept {}

Owner::Owner(Owner* parent, DormantTag) noexcept
    : m_graph(ReactiveGraph::Ambient()), m_parent(parent) {}

Owner::~Owner() {
    Dispose();
    if (m_pushed && m_graph) {
        m_graph->SetCurrentOwner(m_parent);
    }
}

Owner* Owner::Current() noexcept {
    ReactiveGraph* graph = ReactiveGraph::Ambient();
    return graph ? graph->CurrentOwner() : nullptr;
}

void Owner::Own(Ref<detail::ReactiveNode> node) {
    node->SetOwner(this);
    m_owned.push_back(std::move(node));
}

void Owner::AddCleanup(CleanupFn fn) {
    m_cleanups.push_back(std::move(fn));
}

void Owner::Dispose() {
    for (const auto& node : m_owned) {
        node->Dispose();
    }
    m_owned.clear();
    for (auto it = m_cleanups.rbegin(); it != m_cleanups.rend(); ++it) {
        (*it)();
    }
    m_cleanups.clear();
    m_contexts.clear();
}

void Owner::ProvideContext(Ref<detail::ContextValueBase> value) {
    m_contexts.push_back(std::move(value));
}

detail::ContextValueBase* Owner::FindContext(ContextId id) const noexcept {
    for (const Owner* scope = this; scope != nullptr; scope = scope->m_parent) {
        for (auto it = scope->m_contexts.rbegin(); it != scope->m_contexts.rend(); ++it) {
            if ((*it)->Id() == id) {
                return it->Get();
            }
        }
    }
    return nullptr;
}

namespace detail {

ContextId AcquireContextId() noexcept {
    static std::atomic<std::uint32_t> s_next{1};
    return ContextId{s_next.fetch_add(1, std::memory_order_relaxed)};
}

} // namespace detail

Owner::Scope::Scope(Owner& owner) noexcept
    : m_graph(ReactiveGraph::Ambient()), m_previous(nullptr) {
    if (m_graph) {
        m_previous = m_graph->CurrentOwner();
        m_graph->SetCurrentOwner(&owner);
    }
}

Owner::Scope::~Scope() noexcept {
    if (m_graph) {
        m_graph->SetCurrentOwner(m_previous);
    }
}


void Effect(CleanupFn fn) {
    auto node = MakeRef<detail::ReactiveNode>();
    node->MakeEffect();
    node->SetComputation(std::move(fn));
    node->RunInitial();

    if (Owner* owner = Owner::Current()) {
        owner->Own(std::move(node));
    } else {
        node->Dispose();
    }
}

void OnMount(CleanupFn fn) {
    if (!Owner::Current()) {
        return;
    }
    detail::UntrackedScope shield;
    fn();
}

void OnCleanup(CleanupFn fn) {
    if (Owner* owner = Owner::Current()) {
        owner->AddCleanup(std::move(fn));
    }
}

void OnFrame(FrameFn fn) {
    ReactiveGraph* graph = ReactiveGraph::Ambient();
    Owner* owner = Owner::Current();
    if (!graph || !owner) {
        return;
    }
    Ref<FrameCallback> callback = graph->AddFrameCallback(std::move(fn));
    owner->AddCleanup([callback] { callback->alive = false; });
}

} // namespace lumora
