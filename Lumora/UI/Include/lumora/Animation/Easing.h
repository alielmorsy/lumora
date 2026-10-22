#pragma once

#include <cmath>
#include <numbers>
namespace lumora
{
    /// Maps normalized time `t` in [0,1] to eased progress. Back/Elastic deliberately overshoot [0,1].
    using EasingFn = float (*)(float);

    namespace ease
    {
        inline constexpr float kPi = std::numbers::pi_v<float>;

        inline constexpr float Linear(float t) noexcept { return t; }

        inline float QuadIn(float t) noexcept { return t * t; }
        inline float QuadOut(float t) noexcept { return 1.f - (1.f - t) * (1.f - t); }

        inline float QuadInOut(float t) noexcept
        {
            return t < 0.5f ? 2.f * t * t : 1.f - 0.5f * (-2.f * t + 2.f) * (-2.f * t + 2.f);
        }

        inline float CubicIn(float t) noexcept { return t * t * t; }

        inline float CubicOut(float t) noexcept
        {
            const float f = 1.f - t;
            return 1.f - f * f * f;
        }

        inline float CubicInOut(float t) noexcept
        {
            return t < 0.5f ? 4.f * t * t * t : 1.f - 0.5f * std::pow(-2.f * t + 2.f, 3.f);
        }

        inline float QuartIn(float t) noexcept { return t * t * t * t; }

        inline float QuartOut(float t) noexcept
        {
            const float f = 1.f - t;
            return 1.f - f * f * f * f;
        }

        inline float QuartInOut(float t) noexcept
        {
            return t < 0.5f ? 8.f * t * t * t * t : 1.f - 0.5f * std::pow(-2.f * t + 2.f, 4.f);
        }

        inline float QuintIn(float t) noexcept { return t * t * t * t * t; }

        inline float QuintOut(float t) noexcept
        {
            const float f = 1.f - t;
            return 1.f - f * f * f * f * f;
        }

        inline float QuintInOut(float t) noexcept
        {
            return t < 0.5f ? 16.f * t * t * t * t * t : 1.f - 0.5f * std::pow(-2.f * t + 2.f, 5.f);
        }

        inline float SineIn(float t) noexcept { return 1.f - std::cos((t * kPi) * 0.5f); }
        inline float SineOut(float t) noexcept { return std::sin((t * kPi) * 0.5f); }
        inline float SineInOut(float t) noexcept { return -(std::cos(kPi * t) - 1.f) * 0.5f; }

        inline float ExpoIn(float t) noexcept { return t <= 0.f ? 0.f : std::pow(2.f, 10.f * t - 10.f); }
        inline float ExpoOut(float t) noexcept { return t >= 1.f ? 1.f : 1.f - std::pow(2.f, -10.f * t); }

        inline float ExpoInOut(float t) noexcept
        {
            if (t <= 0.f) return 0.f;
            if (t >= 1.f) return 1.f;
            return t < 0.5f ? 0.5f * std::pow(2.f, 20.f * t - 10.f) : 1.f - 0.5f * std::pow(2.f, -20.f * t + 10.f);
        }

        inline float CircIn(float t) noexcept { return 1.f - std::sqrt(1.f - t * t); }

        inline float CircOut(float t) noexcept
        {
            const float f = t - 1.f;
            return std::sqrt(1.f - f * f);
        }

        inline float CircInOut(float t) noexcept
        {
            if (t < 0.5f)
            {
                return 0.5f * (1.f - std::sqrt(1.f - 4.f * t * t));
            }
            const float f = -2.f * t + 2.f;
            return 0.5f * (std::sqrt(1.f - f * f) + 1.f);
        }

        inline float BackIn(float t) noexcept
        {
            constexpr float c1 = 1.70158f;
            constexpr float c3 = c1 + 1.f;
            return c3 * t * t * t - c1 * t * t;
        }

        inline float BackOut(float t) noexcept
        {
            constexpr float c1 = 1.70158f;
            constexpr float c3 = c1 + 1.f;
            const float f = t - 1.f;
            return 1.f + c3 * f * f * f + c1 * f * f;
        }

        inline float BackInOut(float t) noexcept
        {
            constexpr float c1 = 1.70158f;
            constexpr float c2 = c1 * 1.525f;
            if (t < 0.5f)
            {
                const float f = 2.f * t;
                return 0.5f * (f * f * ((c2 + 1.f) * f - c2));
            }
            const float f = 2.f * t - 2.f;
            return 0.5f * (f * f * ((c2 + 1.f) * f + c2) + 2.f);
        }

        inline float ElasticIn(float t) noexcept
        {
            if (t <= 0.f) return 0.f;
            if (t >= 1.f) return 1.f;
            constexpr float c4 = (2.f * kPi) / 3.f;
            return -std::pow(2.f, 10.f * t - 10.f) * std::sin((t * 10.f - 10.75f) * c4);
        }

        inline float ElasticOut(float t) noexcept
        {
            if (t <= 0.f) return 0.f;
            if (t >= 1.f) return 1.f;
            constexpr float c4 = (2.f * kPi) / 3.f;
            return std::pow(2.f, -10.f * t) * std::sin((t * 10.f - 0.75f) * c4) + 1.f;
        }

        inline float ElasticInOut(float t) noexcept
        {
            if (t <= 0.f) return 0.f;
            if (t >= 1.f) return 1.f;
            constexpr float c5 = (2.f * kPi) / 4.5f;
            if (t < 0.5f)
            {
                return -0.5f * std::pow(2.f, 20.f * t - 10.f) * std::sin((20.f * t - 11.125f) * c5);
            }
            return 0.5f * std::pow(2.f, -20.f * t + 10.f) * std::sin((20.f * t - 11.125f) * c5) + 1.f;
        }

        inline float BounceOut(float t) noexcept
        {
            constexpr float n1 = 7.5625f;
            constexpr float d1 = 2.75f;
            if (t < 1.f / d1)
            {
                return n1 * t * t;
            }
            if (t < 2.f / d1)
            {
                t -= 1.5f / d1;
                return n1 * t * t + 0.75f;
            }
            if (t < 2.5f / d1)
            {
                t -= 2.25f / d1;
                return n1 * t * t + 0.9375f;
            }
            t -= 2.625f / d1;
            return n1 * t * t + 0.984375f;
        }

        inline float BounceIn(float t) noexcept { return 1.f - BounceOut(1.f - t); }

        inline float BounceInOut(float t) noexcept
        {
            return t < 0.5f ? 0.5f * (1.f - BounceOut(1.f - 2.f * t)) : 0.5f * (1.f + BounceOut(2.f * t - 1.f));
        }
    }
}
