#pragma once

#include <lumora/Detail/Reactive.h>

#include <concepts>
#include <utility>

namespace lumora {

template<typename T> class Computed;

namespace detail {

/// Signal storage: a pure source node carrying the value. Internal.
template<typename T>
class SignalNode final : public ReactiveNode {
public:
    explicit SignalNode(T initial) : value(std::move(initial)) {}

    T value;
};

}

/// A reactive value handle; copies share state and mutators are const so by-value lambda captures can write.
/// operator()() inside an Effect/Computed subscribes, Peek() does not. Writes are equality-gated; effects re-run on the
/// next flush (immediately for a standalone write, coalesced inside a Batch).
template<typename T>
class Signal {
public:
    explicit Signal(T initial = T{}) : m_state(MakeRef<detail::SignalNode<T>>(std::move(initial))) {}

    /// Read and auto-track.
    const T& operator()() const {
        detail::Track(*m_state);
        return m_state->value;
    }

    /// Read without subscribing.
    [[nodiscard]] const T& Peek() const noexcept { return m_state->value; }

    /// Opaque identity of the shared state, stable while any copy lives; keys per-signal animation drivers. Never dereference.
    [[nodiscard]] const void* Raw() const noexcept { return m_state.Get(); }

    /// Write. Dropped when the value is unchanged (operator==).
    const Signal& operator=(T value) const {
        if constexpr (std::equality_comparable<T>) {
            if (m_state->value == value) {
                return *this;
            }
        }
        m_state->value = std::move(value);
        m_state->NotifySource();
        return *this;
    }

    /// In-place mutation + notify. Use for non-comparable payloads (vector, map, ...).
    void Update(std::invocable<T&> auto fn) const {
        fn(m_state->value);
        m_state->NotifySource();
    }

    void Toggle() const requires std::same_as<T, bool> {
        m_state->value = !m_state->value;
        m_state->NotifySource();
    }

    const Signal& operator+=(auto&& v) const requires requires(T& t) { t += v; } {
        m_state->value += std::forward<decltype(v)>(v);
        m_state->NotifySource();
        return *this;
    }
    const Signal& operator-=(auto&& v) const requires requires(T& t) { t -= v; } {
        m_state->value -= std::forward<decltype(v)>(v);
        m_state->NotifySource();
        return *this;
    }
    const Signal& operator*=(auto&& v) const requires requires(T& t) { t *= v; } {
        m_state->value *= std::forward<decltype(v)>(v);
        m_state->NotifySource();
        return *this;
    }

    /// Single-source projection: the Computed tracks exactly this signal even if `fn` reads other state.
    template<typename Fn>
    requires std::invocable<Fn, const T&>
    auto Map(Fn fn) const;

private:
    Ref<detail::SignalNode<T>> m_state;
};

template<typename T>
Signal(T) -> Signal<T>;

}
