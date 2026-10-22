#pragma once

#include "Style/Transition.h" // TimingFn

#include <lumora/Style/CssValue.h>
#include <lumora/Style/Stylesheet.h> // KeyframesDef

#include <cstdint>

namespace lumora
{
    struct StyleState;
    struct TransformState;
} // namespace lumora

namespace lumora::style
{
    /// A parsed `animation` shorthand for one node (single animation; comma-lists take the first).
    struct AnimationSpec
    {
        bool enabled = false;
        StringId name{};
        float duration = 0.f;  ///< seconds
        float delay = 0.f;     ///< seconds
        float iterations = 1.f; ///< -1 == infinite
        TimingFn timing{};
        enum class Direction : std::uint8_t { Normal, Reverse, Alternate, AlternateReverse } direction =
            Direction::Normal;
        enum class Fill : std::uint8_t { None, Forwards, Backwards, Both } fill = Fill::None;
        bool paused = false;
    };

    struct AnimationRuntime
    {
        AnimationSpec spec;
        float elapsed = 0.f;
        bool wasEnabled = false; ///< to reset elapsed when an animation (re)starts

        /// Cached @keyframes block for `spec.name`; re-resolved when the name or sheet generation changes (hot reload can
        /// invalidate the pointer).
        const KeyframesDef* resolved = nullptr;
        StringId resolvedName{};
        std::uint64_t resolvedGen = 0;
        bool resolvedValid = false;
    };

    /// Parse the `animation` shorthand into `rt.spec` (disabled when the value is empty/`none`).
    void ParseAnimation(const CssValue& animation, AnimationRuntime& rt);

    /// Advance the animation by `dt` seconds, writing animated paint fields into `ss`/`ts`. The finishing step can still
    /// write (forwards fill), so AdvanceResult reports "wrote" and "active" separately.
    [[nodiscard]] AdvanceResult AdvanceAnimation(AnimationRuntime& rt, const KeyframesDef& kf, StyleState& ss,
                                                 TransformState& ts, float dt);
} // namespace lumora::style
