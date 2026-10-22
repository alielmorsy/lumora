#pragma once

#include <lumora/Detail/Reactive.h>

#include <functional>
#include <utility>

namespace lumora {

using CleanupFn = MoveFn<void()>;
using FrameFn   = MoveFn<void(float)>;

/// Run `fn` untracked: reads inside do not subscribe the enclosing effect/computed. Returns fn's result.
template<typename Fn>
decltype(auto) Untracked(Fn&& fn) {
    detail::UntrackedScope shield;
    return std::forward<Fn>(fn)();
}

/// Group writes: effects flush once after `fn` returns (outermost batch only); a write outside any batch flushes immediately. Re-entrant.
template<typename Fn>
void Batch(Fn&& fn) {
    detail::BeginBatch();
    struct Guard {
        ~Guard() { detail::EndBatch(); }
    } guard;
    std::forward<Fn>(fn)();
}

}
