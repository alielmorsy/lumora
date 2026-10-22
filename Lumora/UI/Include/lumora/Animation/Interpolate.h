#pragma once

#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Vec2.h>

#include <cmath>
#include <concepts>
#include <cstdint>

namespace lumora {

/// Customization point: the float-promoted vector space the animation core integrates in. Left undefined
/// so an unsupported type fails the Animatable concept instead of picking a wrong default.
template<typename T>
struct AnimTraits;

template<>
struct AnimTraits<float> {
    using Vec = float;

    static constexpr Vec   ToVec(float v) noexcept { return v; }
    static constexpr float FromVec(Vec v) noexcept { return v; }
    static constexpr Vec   Add(Vec a, Vec b) noexcept { return a + b; }
    static constexpr Vec   Sub(Vec a, Vec b) noexcept { return a - b; }
    static constexpr Vec   Scale(Vec a, float s) noexcept { return a * s; }
    static constexpr float Length2(Vec a) noexcept { return a * a; }
    static constexpr float DefaultRestThreshold() noexcept { return 0.001f; }
};

template<>
struct AnimTraits<Vec2> {
    using Vec = Vec2;

    static constexpr Vec   ToVec(Vec2 v) noexcept { return v; }
    static constexpr Vec2  FromVec(Vec v) noexcept { return v; }
    static constexpr Vec   Add(Vec a, Vec b) noexcept { return a + b; }
    static constexpr Vec   Sub(Vec a, Vec b) noexcept { return a - b; }
    static constexpr Vec   Scale(Vec a, float s) noexcept { return a * s; }
    static constexpr float Length2(Vec a) noexcept { return a.LengthSquared(); }
    static constexpr float DefaultRestThreshold() noexcept { return 0.01f; }
};

/// Colour animates as four floats in 0..255 (straight gamma-space RGBA), quantized round-half-up + clamp like
/// Color's blends so a settled value equals the target. Velocity must be float: uint8 would stall slow fades.
template<>
struct AnimTraits<Color> {
    struct Vec {
        float r = 0.f, g = 0.f, b = 0.f, a = 0.f;
    };

    static Vec ToVec(Color c) noexcept {
        return {static_cast<float>(c.r), static_cast<float>(c.g), static_cast<float>(c.b), static_cast<float>(c.a)};
    }
    static Color FromVec(Vec v) noexcept {
        return {Quantize(v.r), Quantize(v.g), Quantize(v.b), Quantize(v.a)};
    }
    static Vec   Add(Vec a, Vec b) noexcept { return {a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a}; }
    static Vec   Sub(Vec a, Vec b) noexcept { return {a.r - b.r, a.g - b.g, a.b - b.b, a.a - b.a}; }
    static Vec   Scale(Vec a, float s) noexcept { return {a.r * s, a.g * s, a.b * s, a.a * s}; }
    static float Length2(Vec a) noexcept { return a.r * a.r + a.g * a.g + a.b * a.b + a.a * a.a; }
    static constexpr float DefaultRestThreshold() noexcept { return 0.4f; }

private:
    static std::uint8_t Quantize(float x) noexcept {
        if (x < 0.f) x = 0.f;
        if (x > 255.f) x = 255.f;
        return static_cast<std::uint8_t>(x + 0.5f);
    }
};

template<typename T>
concept Animatable = requires(T value, typename AnimTraits<T>::Vec v) {
    { AnimTraits<T>::ToVec(value) } -> std::same_as<typename AnimTraits<T>::Vec>;
    { AnimTraits<T>::FromVec(v) } -> std::convertible_to<T>;
    AnimTraits<T>::Add(v, v);
    AnimTraits<T>::Sub(v, v);
    AnimTraits<T>::Scale(v, 1.f);
    { AnimTraits<T>::Length2(v) } -> std::convertible_to<float>;
    { AnimTraits<T>::DefaultRestThreshold() } -> std::convertible_to<float>;
};

/// Linear interpolation in vector space; `t` is unclamped so Back/Elastic overshoot works.
template<Animatable T>
[[nodiscard]] T Lerp(const T& a, const T& b, float t) {
    using A = AnimTraits<T>;
    const auto va = A::ToVec(a);
    const auto vb = A::ToVec(b);
    return A::FromVec(A::Add(va, A::Scale(A::Sub(vb, va), t)));
}

}
