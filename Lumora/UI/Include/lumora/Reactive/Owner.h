#pragma once

#include <lumora/Api.h>
#include <lumora/Detail/Reactive.h>
#include <lumora/Reactive/Tracking.h>

#include <cstdint>
#include <vector>

namespace lumora {

struct DormantTag {};
inline constexpr DormantTag Dormant{};

/// Unique identity of a Context<T> key object.
enum class ContextId : std::uint32_t {};

namespace detail {

[[nodiscard]] LUMORA_API ContextId AcquireContextId() noexcept;

/// Base of a provided context value, owned by the providing Owner; typed storage is ContextValue<T>. Internal.
class ContextValueBase : public RefCounted {
public:
    explicit ContextValueBase(ContextId id) noexcept : m_id(id) {}

    [[nodiscard]] ContextId Id() const noexcept { return m_id; }

private:
    ContextId m_id;
};

}

/// A reactive scope owning effects and cleanup hooks. Dispose() severs owned effects and runs cleanups in LIFO order.
/// "Current" is per ambient ReactiveGraph, not global (Mount, Show and Each establish it). Non-copyable, non-movable.
class LUMORA_API Owner {
public:
    /// Becomes the current owner of the ambient graph immediately.
    Owner();

    /// Construct without becoming current. Activate temporarily via Owner::Scope.
    explicit Owner(DormantTag) noexcept;

    /// Dormant child scope of `parent`: Show/Each keep branch/row scopes in a payload (non-stack lifetime), so it neither
    /// pushes onto the ambient stack nor touches it on disposal, yet `Use()` resolves providers through `parent`. Activate via Owner::Scope to build.
    Owner(Owner* parent, DormantTag) noexcept;

    ~Owner();

    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
    Owner(Owner&&) = delete;
    Owner& operator=(Owner&&) = delete;

    /// The innermost live Owner of the ambient graph, or nullptr.
    [[nodiscard]] static Owner* Current() noexcept;

    /// The enclosing Owner, or nullptr at the root scope.
    [[nodiscard]] Owner* Parent() const noexcept { return m_parent; }

    /// Attach a provided context value to this scope (called by Provide()).
    void ProvideContext(Ref<detail::ContextValueBase> value);

    /// Nearest value for `id`: scans this scope's providers newest-first, then parents.
    [[nodiscard]] detail::ContextValueBase* FindContext(ContextId id) const noexcept;

    /// Take ownership of a node; node->Dispose() runs when this Owner disposes.
    void Own(Ref<detail::ReactiveNode> node);

    /// Register a cleanup hook (LIFO order on dispose).
    void AddCleanup(CleanupFn fn);

    /// Dispose all owned effects, then run cleanups in reverse order. Idempotent.
    void Dispose();

    /// RAII guard that temporarily makes a (typically dormant) Owner the current one on
    /// the ambient graph.
    class LUMORA_API Scope {
    public:
        explicit Scope(Owner& owner) noexcept;
        ~Scope() noexcept;
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        ReactiveGraph* m_graph;
        Owner* m_previous;
    };

private:
    std::vector<Ref<detail::ReactiveNode>>      m_owned;
    std::vector<CleanupFn>                       m_cleanups;
    std::vector<Ref<detail::ContextValueBase>>   m_contexts;
    Ref<ReactiveGraph> m_graph;
    Owner* m_parent = nullptr;
    bool m_pushed = false;
};

}
