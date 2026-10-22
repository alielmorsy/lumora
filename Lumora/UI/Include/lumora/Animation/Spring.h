#pragma once

#include <lumora/Animation/Motion.h>
#include <lumora/Reactive/Effect.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Tracking.h>

#include <cmath>
#include <utility>

namespace lumora {

/// Spring parameters; defaults match react-spring (omega ~ 13, zeta ~ 0.99). `restThreshold < 0` uses the
/// trait default; `clamp` stops at the first target crossing (no overshoot).
struct SpringConfig {
    float stiffness     = 170.f;
    float damping       = 26.f;
    float mass          = 1.f;
    float restThreshold = -1.f;
    bool  clamp         = false;
};

namespace spring {
inline constexpr SpringConfig Gentle{120.f, 14.f, 1.f};
inline constexpr SpringConfig Wobbly{180.f, 12.f, 1.f};
inline constexpr SpringConfig Stiff{210.f, 20.f, 1.f};
inline constexpr SpringConfig Slow{80.f, 26.f, 1.f};
}

namespace detail {

/// Closed-form damped-spring step by exactly `dt` about `targetVec`: frame-rate independent, stable across hitches.
template<typename T>
inline void AdvanceSpring(MotionState<T>& s, const SpringConfig& cfg, float dt) {
    using A   = AnimTraits<T>;
    using Vec = typename A::Vec;

    const float mass = cfg.mass < 1e-4f ? 1e-4f : cfg.mass;
    if (cfg.stiffness <= 0.f) {
        s.pos = s.targetVec;
        s.vel = Vec{};
        return;
    }

    const float omega = std::sqrt(cfg.stiffness / mass);
    const float zeta  = cfg.damping / (2.f * std::sqrt(cfg.stiffness * mass));

    const Vec p = A::Sub(s.pos, s.targetVec);
    const Vec v = s.vel;

    Vec newP{};
    Vec newV{};

    if (zeta < 1.f - 1e-3f) {
        // Under-damped.
        const float wd = omega * std::sqrt(1.f - zeta * zeta);
        const float e  = std::exp(-zeta * omega * dt);
        const float cs = std::cos(wd * dt);
        const float sn = std::sin(wd * dt);

        const Vec posOsc = A::Add(A::Scale(p, cs), A::Scale(A::Add(v, A::Scale(p, zeta * omega)), sn / wd));
        newP             = A::Scale(posOsc, e);

        const Vec velOsc = A::Sub(A::Scale(v, cs),
                                  A::Scale(A::Add(A::Scale(v, zeta * omega), A::Scale(p, omega * omega)), sn / wd));
        newV = A::Scale(velOsc, e);
    } else if (zeta <= 1.f + 1e-3f) {
        // Critically damped.
        const float e   = std::exp(-omega * dt);
        const Vec   vop = A::Add(v, A::Scale(p, omega)); // v + ω·p
        newP            = A::Scale(A::Add(p, A::Scale(vop, dt)), e);
        newV            = A::Scale(A::Sub(v, A::Scale(vop, omega * dt)), e);
    } else {
        // Over-damped: two distinct real roots.
        const float r   = std::sqrt(zeta * zeta - 1.f);
        const float za  = -omega * (zeta - r);
        const float zb  = -omega * (zeta + r);
        const float ea  = std::exp(za * dt);
        const float eb  = std::exp(zb * dt);
        const float inv = 1.f / (zb - za);

        const Vec zbpv = A::Sub(A::Scale(p, zb), v); // zb·p - v
        const Vec zapv = A::Sub(A::Scale(p, za), v); // za·p - v

        newP = A::Scale(A::Sub(A::Scale(zbpv, ea), A::Scale(zapv, eb)), inv);
        newV = A::Scale(A::Sub(A::Scale(zbpv, za * ea), A::Scale(zapv, zb * eb)), inv);
    }

    s.pos = A::Add(newP, s.targetVec);
    s.vel = newV;
}

/// Advance one frame; true once snapped to rest (also on non-finite blow-up or a clamped crossing).
template<typename T>
inline bool StepSpring(MotionState<T>& s, const SpringConfig& cfg, float dt) {
    using A = AnimTraits<T>;

    const auto before = A::Sub(s.pos, s.targetVec);
    AdvanceSpring<T>(s, cfg, dt);
    const auto after = A::Sub(s.pos, s.targetVec);

    const bool overshoot  = cfg.clamp && Dot<T>(before, after) < 0.f;
    const bool nonFinite  = !std::isfinite(A::Length2(s.pos)) || !std::isfinite(A::Length2(s.vel));

    if (overshoot || nonFinite || AtRest(s)) {
        s.pos  = s.targetVec;
        s.vel  = typename A::Vec{};
        s.idle = true;
        return true;
    }
    return false;
}

template<typename T>
inline Ref<MotionState<T>> MakeMotionState(const typename AnimTraits<T>::Vec& seed, float restThreshold) {
    auto state           = MakeRef<MotionState<T>>();
    state->pos           = seed;
    state->targetVec     = seed;
    state->restThreshold = restThreshold >= 0.f ? restThreshold : AnimTraits<T>::DefaultRestThreshold();
    state->idle          = true;
    return state;
}

template<typename T>
inline void DriveSpring(Ref<MotionState<T>> state, Signal<T> out, SpringConfig cfg) {
    using A = AnimTraits<T>;
    OnFrame([state = std::move(state), out = std::move(out), cfg](float dt) {
        if (state->idle) {
            return;
        }
        if (ReducedMotion()) {
            state->pos  = state->targetVec;
            state->vel  = typename A::Vec{};
            state->idle = true;
            out         = A::FromVec(state->pos);
            return;
        }
        dt = detail::FrameDelta(dt);
        if (dt <= 0.f) {
            return;
        }
        StepSpring<T>(*state, cfg, dt);
        out = A::FromVec(state->pos);
    });
}

template<typename T>
inline void WatchSpringTarget(Ref<MotionState<T>> state, Reactive<T> target) {
    Effect([state = std::move(state), target = std::move(target)] {
        const auto next = AnimTraits<T>::ToVec(target.Get());
        Untracked([&] {
            state->targetVec = next;
            state->idle      = false;
        });
    });
}

}

/// Springs toward `target`, keeping velocity across re-targets (interruptible). Seeds at the target's
/// current value (no jump on mount).
template<Animatable T>
[[nodiscard]] Motion<T> Spring(Reactive<T> target, SpringConfig cfg = {}) {
    using A = AnimTraits<T>;

    if (!Owner::Current()) {
        detail::WarnAnimationNoOwner("Spring");
    }

    const T   seed = Untracked([&] { return target.Get(); });
    auto      state = detail::MakeMotionState<T>(A::ToVec(seed), cfg.restThreshold);
    Signal<T> out{seed};

    if (!target.IsStatic()) {
        detail::WatchSpringTarget<T>(state, std::move(target));
    }
    detail::DriveSpring<T>(state, out, cfg);

    return Motion<T>{std::move(out), std::move(state)};
}

/// Overload with a starting value: snaps to `from`, then springs to `target`.
template<Animatable T>
[[nodiscard]] Motion<T> Spring(Reactive<T> target, T from, SpringConfig cfg = {}) {
    using A = AnimTraits<T>;

    if (!Owner::Current()) {
        detail::WarnAnimationNoOwner("Spring");
    }

    auto      state = detail::MakeMotionState<T>(A::ToVec(from), cfg.restThreshold);
    Signal<T> out{from};

    detail::WatchSpringTarget<T>(state, std::move(target));
    detail::DriveSpring<T>(state, out, cfg);

    return Motion<T>{std::move(out), std::move(state)};
}

}
