#pragma once

#include <lumora/Api.h>
#include <lumora/Animation/Easing.h>
#include <lumora/Animation/Motion.h>
#include <lumora/Animation/Spring.h>
#include <lumora/Reactive/Effect.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Tracking.h>

#include <functional>
#include <unordered_map>
#include <utility>

namespace lumora {

/// `delay` waits before the curve starts; `onComplete` fires once at the target.
struct TweenConfig {
    float                 duration = 0.3f;
    EasingFn              easing   = ease::CubicInOut;
    float                 delay    = 0.f;
    std::function<void()> onComplete{};
};

namespace detail {

template<typename T>
struct TweenState final : RefCounted {
    using Vec = typename AnimTraits<T>::Vec;

    Vec   from{};
    Vec   to{};
    float elapsed = 0.f;
    bool  active  = false;
};

/// Cancel token + clock for an imperative animation; a signal has one driver, a new one cancels the prior.
struct ImperativeDriver final : RefCounted {
    bool  cancelled = false;
    float elapsed   = 0.f;
};

using ImperativeRegistry = std::unordered_map<const void*, Ref<ImperativeDriver>>;

LUMORA_API ImperativeRegistry& CurrentImperativeRegistry();

inline Ref<ImperativeDriver> BeginImperative(const void* key) {
    auto& reg = CurrentImperativeRegistry();
    if (auto it = reg.find(key); it != reg.end() && it->second) {
        it->second->cancelled = true;
    }
    auto driver = MakeRef<ImperativeDriver>();
    reg[key]    = driver;
    return driver;
}

inline void EndImperative(const void* key, const ImperativeDriver* driver) {
    auto& reg = CurrentImperativeRegistry();
    if (auto it = reg.find(key); it != reg.end() && it->second.Get() == driver) {
        reg.erase(it);
    }
}

template<Animatable T>
Motion<T> TweenFrom(Reactive<T> target, T seed, bool watch, TweenConfig cfg) {
    using A = AnimTraits<T>;

    if (!Owner::Current()) {
        detail::WarnAnimationNoOwner("Tween");
    }

    auto      state = detail::MakeMotionState<T>(A::ToVec(seed), -1.f);
    auto      run  = MakeRef<detail::TweenState<T>>();
    Signal<T> out{seed};

    if (watch) {
        Effect([state, run, target] {
            const auto next = A::ToVec(target.Get());
            Untracked([&] {
                run->from        = state->pos;
                run->to          = next;
                run->elapsed     = 0.f;
                run->active      = true;
                state->targetVec = next;
                state->idle      = false;
            });
        });
    }

    OnFrame([state, run, out, cfg](float dt) {
        if (!run->active) {
            return;
        }
        if (ReducedMotion()) {
            state->pos  = run->to;
            run->active = false;
            state->idle = true;
            out         = A::FromVec(state->pos);
            if (cfg.onComplete) cfg.onComplete();
            return;
        }
        run->elapsed += detail::FrameDelta(dt);
        const float local = run->elapsed - cfg.delay;
        if (local < 0.f) {
            return;
        }
        if (cfg.duration <= 0.f || local >= cfg.duration) {
            state->pos  = run->to;
            run->active = false;
            state->idle = true;
            out         = A::FromVec(state->pos);
            if (cfg.onComplete) cfg.onComplete();
            return;
        }
        const float eased = cfg.easing ? cfg.easing(local / cfg.duration) : local / cfg.duration;
        state->pos        = A::Add(run->from, A::Scale(A::Sub(run->to, run->from), eased));
        out               = A::FromVec(state->pos);
    });

    return Motion<T>{std::move(out), std::move(state)};
}

}

/// Eases toward `target` over `duration`, re-tweening from the current value when the target changes. Seeds at the
/// target's current value (no animation on mount).
template<Animatable T>
[[nodiscard]] Motion<T> Tween(Reactive<T> target, TweenConfig cfg = {}) {
    const T seed  = Untracked([&] { return target.Get(); });
    const bool watch = !target.IsStatic();
    return detail::TweenFrom<T>(std::move(target), seed, watch, std::move(cfg));
}

/// Overload with a starting value: starts at `from` and tweens to `target` on mount, even when `target` is static.
template<Animatable T>
[[nodiscard]] Motion<T> Tween(Reactive<T> target, T from, TweenConfig cfg = {}) {
    return detail::TweenFrom<T>(std::move(target), from, true, std::move(cfg));
}

/// Tween a user-owned Signal toward `to`, cancelling any prior animation on it. Needs a current Owner
/// (not a raw event handler).
template<Animatable T>
void AnimateTo(Signal<T> sig, T to, TweenConfig cfg = {}) {
    using A = AnimTraits<T>;

    if (!Owner::Current()) {
        detail::WarnAnimationNoOwner("AnimateTo");
        return;
    }
    if (ReducedMotion() || cfg.duration <= 0.f) {
        sig = to;
        if (cfg.onComplete) cfg.onComplete();
        return;
    }

    const void* key    = sig.Raw();
    auto        driver = detail::BeginImperative(key);
    const auto  fromV  = A::ToVec(sig.Peek());
    const auto  toV    = A::ToVec(to);

    OnFrame([sig, driver, fromV, toV, cfg, key](float dt) {
        if (driver->cancelled) {
            return;
        }
        driver->elapsed += detail::FrameDelta(dt);
        const float local = driver->elapsed - cfg.delay;
        if (local < 0.f) {
            return;
        }
        if (local >= cfg.duration) {
            sig               = A::FromVec(toV);
            driver->cancelled = true;
            detail::EndImperative(key, driver.Get());
            if (cfg.onComplete) cfg.onComplete();
            return;
        }
        const float eased = cfg.easing ? cfg.easing(local / cfg.duration) : local / cfg.duration;
        sig               = A::FromVec(A::Add(fromV, A::Scale(A::Sub(toV, fromV), eased)));
    });

    OnCleanup([driver, key] { detail::EndImperative(key, driver.Get()); });
}

/// Spring a user-owned Signal toward `to`; same rules as AnimateTo.
template<Animatable T>
void SpringTo(Signal<T> sig, T to, SpringConfig cfg = {}) {
    using A = AnimTraits<T>;

    if (!Owner::Current()) {
        detail::WarnAnimationNoOwner("SpringTo");
        return;
    }
    if (ReducedMotion()) {
        sig = to;
        return;
    }

    const void* key    = sig.Raw();
    auto        driver = detail::BeginImperative(key);
    auto        state  = detail::MakeMotionState<T>(A::ToVec(sig.Peek()), cfg.restThreshold);
    state->targetVec   = A::ToVec(to);
    state->idle        = false;

    OnFrame([sig, driver, state, cfg, key](float dt) {
        if (driver->cancelled) {
            return;
        }
        dt = detail::FrameDelta(dt);
        if (dt <= 0.f) {
            return;
        }
        const bool done = detail::StepSpring<T>(*state, cfg, dt);
        sig             = A::FromVec(state->pos);
        if (done) {
            driver->cancelled = true;
            detail::EndImperative(key, driver.Get());
        }
    });

    OnCleanup([driver, key] { detail::EndImperative(key, driver.Get()); });
}

}
