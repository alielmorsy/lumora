#pragma once

#include <lumora/Reactive/Computed.h>
#include <lumora/Reactive/Signal.h>

#include <concepts>
#include <utility>

namespace lumora {

template<typename F, typename T>
concept SignalSource = std::invocable<F> &&
                       std::convertible_to<std::invoke_result_t<F>, T>;

namespace detail {

/// Erased reactive input of a Reactive<T> prop; Get() auto-tracks inside an effect/computed and is called only when
/// binding or patching. Internal.
template<typename T>
class ReactiveSource : public RefCounted {
public:
    [[nodiscard]] virtual T Get() const = 0;
};

template<typename T>
class SignalSourceOf final : public ReactiveSource<T> {
public:
    explicit SignalSourceOf(Signal<T> signal) : m_signal(std::move(signal)) {}

    [[nodiscard]] T Get() const override { return m_signal(); }

private:
    Signal<T> m_signal;
};

template<typename T>
class ComputedSourceOf final : public ReactiveSource<T> {
public:
    explicit ComputedSourceOf(Computed<T> computed) : m_computed(std::move(computed)) {}

    [[nodiscard]] T Get() const override { return m_computed(); }

private:
    Computed<T> m_computed;
};

template<typename T, typename Fn>
class GetterSourceOf final : public ReactiveSource<T> {
public:
    explicit GetterSourceOf(Fn fn) : m_fn(std::move(fn)) {}

    [[nodiscard]] T Get() const override { return static_cast<T>(m_fn()); }

private:
    mutable Fn m_fn;
};

/// Read a reactive-or-plain argument to its value: Signal/Computed (operator()) read tracked, Reactive/Bind (Get()) read
/// through, else passthrough. The read runs at call time, so inside a binding effect the dependency is captured.
template<typename A>
[[nodiscard]] decltype(auto) ReadReactive(const A& a) {
    if constexpr (requires { a(); }) {
        return a();
    } else if constexpr (requires { a.Get(); }) {
        return a.Get();
    } else {
        return (a);
    }
}

}

/// The "value or reactive source" prop type: absorbs a plain T, Signal<T>, Computed<T> or lambda. The runtime checks
/// IsStatic() to decide whether to wire a patching effect.
/// Tagged, not a variant: m_value holds the constant (or caches the last read), m_source is null for statics so constants never allocate.
template<typename T>
class Reactive {
public:
    Reactive() = default;

    Reactive(T value) : m_value(std::move(value)) {}

    Reactive(Signal<T> signal) : m_source(MakeRef<detail::SignalSourceOf<T>>(std::move(signal))) {}

    Reactive(Computed<T> computed) : m_source(MakeRef<detail::ComputedSourceOf<T>>(std::move(computed))) {}

    template<typename Fn>
    requires SignalSource<Fn, T>
          && (!std::same_as<std::decay_t<Fn>, Reactive>)
          && (!std::same_as<std::decay_t<Fn>, Signal<T>>)
          && (!std::same_as<std::decay_t<Fn>, Computed<T>>)
    Reactive(Fn fn) : m_source(MakeRef<detail::GetterSourceOf<T, std::decay_t<Fn>>>(std::move(fn))) {}

    /// Current value. Auto-tracks reactive sources when read inside an effect/computed.
    [[nodiscard]] const T& Get() const {
        if (m_source) {
            m_value = m_source->Get();
        }
        return m_value;
    }

    /// True when this prop is a plain constant (set once, no patching effect needed).
    [[nodiscard]] bool IsStatic() const noexcept { return m_source.Get() == nullptr; }

private:
    mutable T m_value{};
    Ref<detail::ReactiveSource<T>> m_source;
};

}
