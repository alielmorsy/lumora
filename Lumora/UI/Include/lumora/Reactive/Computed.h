#pragma once

#include <lumora/Detail/Reactive.h>
#include <lumora/Reactive/Signal.h>

#include <concepts>
#include <optional>
#include <utility>

namespace lumora
{
    namespace detail
    {
        /// Computed storage: observes its sources and is itself a source for downstream observers. Internal.
        template <typename T>
        class ComputedNode final : public ReactiveNode
        {
        public:
            std::optional<T> cached;
        };
    }

    /// Lazy derived value: invalidation only marks it stale, recompute happens on the next read; dependencies are re-tracked
    /// every run. Copies share state. Create under an ambient ReactiveGraph, otherwise it computes once untracked and never updates.
    template <typename T>
    class Computed
    {
    public:
        template <typename Fn>
            requires std::invocable<Fn> && std::convertible_to<std::invoke_result_t<Fn>, T>
        explicit Computed(Fn fn) : m_state(MakeRef<detail::ComputedNode<T>>())
        {
            detail::ComputedNode<T>* state = m_state.Get();
            state->SetComputation([state, fn = std::move(fn)]() mutable
            {
                T next = fn();
                if constexpr (std::equality_comparable<T>)
                {
                    if (state->cached && *state->cached == next)
                    {
                        return; // value unchanged: leave observers Clean (equality gate propagates)
                    }
                }
                state->cached = std::move(next);
                state->MarkValueChanged();
            });
            state->RunInitial(); // eager first compute
        }

        /// Read and auto-track. Recomputes first if stale.
        const T& operator()() const
        {
            m_state->UpdateIfNecessary();
            detail::Track(*m_state);
            return *m_state->cached;
        }

        /// Read without subscribing. Still recomputes if stale.
        [[nodiscard]] const T& Peek() const
        {
            m_state->UpdateIfNecessary();
            return *m_state->cached;
        }

        template <typename Fn>
            requires std::invocable<Fn, const T&>
        auto Map(Fn fn) const;

    private:
        Ref<detail::ComputedNode<T>> m_state;
    };

    template <typename Fn>
        requires std::invocable<Fn>
    Computed(Fn) -> Computed<std::invoke_result_t<Fn>>;

    template <typename T>
    template <typename Fn>
        requires std::invocable<Fn, const T&>
    auto Signal<T>::Map(Fn fn) const
    {
        auto source = *this;
        return lumora::Computed([source, fn = std::move(fn)]() mutable
        {
            const T& value = source();
            detail::UntrackedScope shield;
            return std::invoke(fn, value);
        });
    }

    template <typename T>
    template <typename Fn>
        requires std::invocable<Fn, const T&>
    auto Computed<T>::Map(Fn fn) const
    {
        auto source = *this;
        return lumora::Computed([source, fn = std::move(fn)]() mutable
        {
            const T& value = source();
            detail::UntrackedScope shield;
            return std::invoke(fn, value);
        });
    }
}
