#pragma once

#include <lumora/Reactive/Owner.h>

#include <cassert>
#include <utility>

namespace lumora {

/// Typed injection key for passing values down the component tree. Each instance owns a unique ContextId: declare one
/// `inline Context<T> XCtx;` per key. Non-copyable (the object is the identity).
template<typename T>
class Context {
public:
    Context() noexcept : m_id(detail::AcquireContextId()) {}

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    [[nodiscard]] ContextId Id() const noexcept { return m_id; }

private:
    ContextId m_id;
};

namespace detail {

/// Typed provider slot living on the providing Owner. Internal (Provide() writes, Use() reads).
template<typename T>
class ContextValue final : public ContextValueBase {
public:
    ContextValue(ContextId id, T value) : ContextValueBase(id), m_value(std::move(value)) {}

    [[nodiscard]] const T& Value() const noexcept { return m_value; }

private:
    T m_value;
};

}

/// Provide a value to all descendants of the current Owner scope (stores a copy; Signal/Computed handles share state).
template<typename T>
void Provide(Context<T>& context, T value) {
    Owner* owner = Owner::Current();
    assert(owner != nullptr && "Provide() called outside an Owner scope");
    if (owner) {
        owner->ProvideContext(MakeRef<detail::ContextValue<T>>(context.Id(), std::move(value)));
    }
}

/// Nearest provided value up the Owner chain; asserts without a matching Provide().
template<typename T>
[[nodiscard]] const T& Use(Context<T>& context) {
    Owner* owner = Owner::Current();
    assert(owner != nullptr && "Use() called outside an Owner scope");
    detail::ContextValueBase* slot = owner ? owner->FindContext(context.Id()) : nullptr;
    assert(slot != nullptr && "Use() called without a matching Provide()");
    return static_cast<detail::ContextValue<T>*>(slot)->Value();
}

}
