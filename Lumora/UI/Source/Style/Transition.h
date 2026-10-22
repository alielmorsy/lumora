#pragma once

#include <lumora/Animation/Easing.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Style/CssValue.h>

#include <cstdint>

namespace lumora
{
    struct StyleState;
    struct TransformState;
} // namespace lumora

namespace lumora::style
{
    /// A CSS timing function; keyword curves reuse the ease:: library, cubic-bezier and steps carry their own parameters
    /// (evaluated by Eval()).
    struct TimingFn
    {
        enum class Kind : std::uint8_t { Fn, CubicBezier, Steps } kind = Kind::Fn;
        EasingFn fn = ease::CubicInOut;
        float bez[4] = {0.25f, 0.1f, 0.25f, 1.f};
        int steps = 1;
        enum class StepPos : std::uint8_t { Start, End } stepPos = StepPos::End;

        [[nodiscard]] float Eval(float t) const;
    };

    struct TransitionSpec
    {
        bool enabled = false;
        float duration = 0.f; ///< seconds
        float delay = 0.f;    ///< seconds
        TimingFn timing{};
    };

    namespace detail
    {
        struct ScalarTrans
        {
            float displayed = 0.f, from = 0.f, to = 0.f, elapsed = 0.f;
            bool active = false;
            bool init = false;
        };

        struct ColorTrans
        {
            Color displayed{}, from{}, to{};
            float elapsed = 0.f;
            bool active = false;
            bool init = false;
        };
    } // namespace detail

    /// Per-node transition state for box-like paint fields (fill/opacity/border-radius/transform); layout and text
    /// transitions are unsupported.
    struct TransitionRuntime
    {
        TransitionSpec fillSpec, opacitySpec, radiusSpec, transformSpec;
        detail::ColorTrans fill;
        detail::ScalarTrans opacity, radius, transX, transY, scaleX, scaleY, rotate;
        bool driverRegistered = false;
    };

    /// Parse a `transition` value into per-field specs (fields not named reset; honours `all`). Safe on invalid values.
    void ParseTransitions(const CssValue& transition, TransitionRuntime& rt);

    /// Call after apply wrote targets into `ss`/`ts`: starts a run from the displayed value for each changed target and
    /// writes the displayed value back so it does not snap.
    void InterceptBox(TransitionRuntime& rt, StyleState& ss, TransformState& ts);

    /// `wrote` and `active` differ: a run's final step writes the target then reports finished, so keying repaint off
    /// `active` drops the last frame.
    struct AdvanceResult
    {
        bool wrote = false;  ///< a displayed value was written: the node needs Style | Repaint
        bool active = false; ///< at least one run is still going: keep driving this node

        [[nodiscard]] constexpr explicit operator bool() const noexcept { return active; }

        constexpr AdvanceResult& operator|=(const AdvanceResult& other) noexcept
        {
            wrote |= other.wrote;
            active |= other.active;
            return *this;
        }
    };

    /// Advance active runs by `dt` seconds and write the displayed values into `ss`/`ts`.
    [[nodiscard]] AdvanceResult AdvanceBox(TransitionRuntime& rt, StyleState& ss, TransformState& ts, float dt);

    /// True while any of this node's per-field runs is still going. Cheap enough to ask at enrol time.
    [[nodiscard]] bool AnyActive(const TransitionRuntime& rt) noexcept;
} // namespace lumora::style
