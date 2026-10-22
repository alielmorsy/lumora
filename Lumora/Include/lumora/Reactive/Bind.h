#pragma once

#include <lumora/Reactive/Signal.h>

#include <concepts>
#include <utility>

namespace lumora {

namespace detail {

/// Erased two-way endpoint behind Bind<T>. Internal.
template<typename T>
class BindSource : public RefCounted {
public:
    [[nodiscard]] virtual T Get() const = 0;
    virtual void Set(T value) = 0;
};

template<typename T>
class SignalBindingOf final : public BindSource<T> {
public:
    explicit SignalBindingOf(Signal<T> signal) : m_signal(std::move(signal)) {}

    [[nodiscard]] T Get() const override { return m_signal(); }
    void Set(T value) override { m_signal = std::move(value); }

private:
    Signal<T> m_signal;
};

template<typename T, typename GetFn, typename SetFn>
class AccessorBindingOf final : public BindSource<T> {
public:
    AccessorBindingOf(GetFn get, SetFn set) : m_get(std::move(get)), m_set(std::move(set)) {}

    [[nodiscard]] T Get() const override { return static_cast<T>(m_get()); }
    void Set(T value) override { m_set(std::move(value)); }

private:
    mutable GetFn m_get;
    SetFn m_set;
};

}

/// Two-way binding handed to editable widgets (Switch, Checkbox, Input, Slider); unlike Reactive<T> it writes back.
/// Constructible from a Signal<T> or a {getter, setter} pair.
template<typename T>
class Bind {
public:
    Bind(Signal<T> signal) : m_source(MakeRef<detail::SignalBindingOf<T>>(std::move(signal))) {}

    template<typename GetFn, typename SetFn>
    requires std::invocable<GetFn>
          && std::convertible_to<std::invoke_result_t<GetFn>, T>
          && std::invocable<SetFn, T>
    Bind(GetFn get, SetFn set)
        : m_source(MakeRef<detail::AccessorBindingOf<T, std::decay_t<GetFn>, std::decay_t<SetFn>>>(
              std::move(get), std::move(set))) {}

    /// Read the current value; auto-tracks inside an effect/computed.
    [[nodiscard]] T operator()() const { return m_source->Get(); }
    [[nodiscard]] T Get() const { return m_source->Get(); }

    /// Write-back invoked by the widget on a user gesture.
    void Set(T value) const { m_source->Set(std::move(value)); }

private:
    Ref<detail::BindSource<T>> m_source;
};

}
