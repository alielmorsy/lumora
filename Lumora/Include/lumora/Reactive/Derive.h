#pragma once

#include <lumora/Detail/Reactive.h>
#include <lumora/Reactive/Computed.h>
#include <lumora/Reactive/Reactive.h>

#include <functional>
#include <utility>

namespace lumora {

/// Memoized projection of one reactive source (Signal, Computed or thunk): tracks exactly that source even if `fn` reads
/// other state. Free-function spelling of Signal::Map.
template<typename Source, typename Fn>
[[nodiscard]] auto Derive(Source source, Fn fn) {
    return Computed([source = std::move(source), fn = std::move(fn)]() mutable {
        decltype(auto) value = detail::ReadReactive(source);
        detail::UntrackedScope shield;
        return std::invoke(fn, value);
    });
}

/// Memoized combination of two sources: both tracked, `fn`'s own reads shielded.
template<typename A, typename B, typename Fn>
[[nodiscard]] auto Combine(A a, B b, Fn fn) {
    return Computed([a = std::move(a), b = std::move(b), fn = std::move(fn)]() mutable {
        decltype(auto) va = detail::ReadReactive(a);
        decltype(auto) vb = detail::ReadReactive(b);
        detail::UntrackedScope shield;
        return std::invoke(fn, va, vb);
    });
}

template<typename A, typename B, typename C, typename Fn>
[[nodiscard]] auto Combine(A a, B b, C c, Fn fn) {
    return Computed([a = std::move(a), b = std::move(b), c = std::move(c), fn = std::move(fn)]() mutable {
        decltype(auto) va = detail::ReadReactive(a);
        decltype(auto) vb = detail::ReadReactive(b);
        decltype(auto) vc = detail::ReadReactive(c);
        detail::UntrackedScope shield;
        return std::invoke(fn, va, vb, vc);
    });
}

}
