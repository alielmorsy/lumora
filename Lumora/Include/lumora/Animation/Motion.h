#pragma once

#include <lumora/Api.h>
#include <lumora/Animation/Interpolate.h>
#include <lumora/Reactive/Signal.h>

#include <lumora/Ref.h>

#include <utility>

namespace lumora {

/// Global controls. Scale 0 pauses, <1 slows, >1 speeds up. Reduced motion resolves animations instantly.
LUMORA_API void               SetTimeScale(float scale) noexcept;
[[nodiscard]] LUMORA_API float TimeScale() noexcept;
LUMORA_API void               SetReducedMotion(bool enabled) noexcept;
[[nodiscard]] LUMORA_API bool ReducedMotion() noexcept;

namespace detail {

/// Debug warning for an animation created with no current Owner (its OnFrame/Effect would never fire).
LUMORA_API void WarnAnimationNoOwner(const char* what);

/// Shared integrator state: position/velocity in the trait's vector space, target, and an idle flag.
template<typename T>
struct MotionState final : RefCounted {
    using Vec = typename AnimTraits<T>::Vec;

    Vec   pos{};
    Vec   vel{};
    Vec   targetVec{};
    float restThreshold = AnimTraits<T>::DefaultRestThreshold();
    bool  idle          = true;
};

/// Apply the time scale and clamp the frame delta so a long hitch does not teleport the animation.
[[nodiscard]] inline float FrameDelta(float dt) noexcept {
    dt *= TimeScale();
    constexpr float kMaxDelta = 0.1f;
    if (dt > kMaxDelta) dt = kMaxDelta;
    if (dt < 0.f) dt = 0.f;
    return dt;
}

/// Inner product via trait ops; detects equilibrium crossing for clamped springs.
template<typename T>
[[nodiscard]] inline float Dot(typename AnimTraits<T>::Vec a, typename AnimTraits<T>::Vec b) noexcept {
    using A = AnimTraits<T>;
    return 0.5f * (A::Length2(a) + A::Length2(b) - A::Length2(A::Sub(a, b)));
}

template<typename T>
[[nodiscard]] inline bool AtRest(const MotionState<T>& s) noexcept {
    using A = AnimTraits<T>;
    const float thr2 = s.restThreshold * s.restThreshold;
    return A::Length2(A::Sub(s.pos, s.targetVec)) <= thr2 && A::Length2(s.vel) <= thr2;
}

}

/// Read-only reactive view of an animated value; drops into any Reactive<T> prop. Copies share state.
template<typename T>
class Motion {
public:
    Motion() = default;
    Motion(Signal<T> out, Ref<detail::MotionState<T>> state) noexcept
        : m_out(std::move(out)), m_state(std::move(state)) {}

    /// Tracked read: subscribes the reading effect/computed.
    const T& operator()() const { return m_out(); }

    /// Current value without subscribing.
    [[nodiscard]] const T& Peek() const noexcept { return m_out.Peek(); }

    [[nodiscard]] bool IsAnimating() const noexcept { return m_state && !m_state->idle; }

private:
    Signal<T>                   m_out{};
    Ref<detail::MotionState<T>> m_state{};
};

}
