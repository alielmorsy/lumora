#include "Vitals.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace vitals {

namespace {

Palette MakePalette(bool dark, Color primary, Color container, Color onContainer)
{
    Palette p{};
    if (dark) {
        p.bg = 0x0f1115_rgb; p.card = 0x1a1d24_rgb; p.cardVar = 0x262a33_rgb;
        p.onSurface = 0xe6e8ee_rgb; p.muted = 0x9aa0ad_rgb; p.outline = 0x2e333d_rgb;
        p.onPrimary = 0x11141a_rgb;
        p.move = 0xff4f7b_rgb; p.exercise = 0xa4ec3c_rgb; p.stand = 0x3fe3f0_rgb;
        p.water = 0x6aa8ff_rgb; p.heart = 0xff5c6e_rgb;
        p.stages = {0xffa766_rgb, 0x6fd0ff_rgb, 0x8197ff_rgb, 0x6a5ae0_rgb};
    } else {
        p.bg = 0xf3f5f9_rgb; p.card = 0xffffff_rgb; p.cardVar = 0xeceff4_rgb;
        p.onSurface = 0x1a1c22_rgb; p.muted = 0x5f6472_rgb; p.outline = 0xdde1e8_rgb;
        p.onPrimary = 0xffffff_rgb;
        p.move = 0xf0285a_rgb; p.exercise = 0x58c40e_rgb; p.stand = 0x12b4d4_rgb;
        p.water = 0x2f7cf6_rgb; p.heart = 0xe8384e_rgb;
        p.stages = {0xf4893d_rgb, 0x2fb3f0_rgb, 0x5d79f5_rgb, 0x4635b8_rgb};
    }
    p.primary = primary;
    p.primaryContainer = container;
    p.onPrimaryContainer = onContainer;
    return p;
}

} // namespace

const Palette& ThemePalette(bool dark, int accent)
{
    // Rows: accent; columns: primary, container, on-container. Must match the CSS accent blocks.
    static const std::array<Palette, 8> kAll = {
        MakePalette(false, 0x1b8a5a_rgb, 0xc9f0d9_rgb, 0x00391f_rgb),
        MakePalette(false, 0x3a6ff7_rgb, 0xdbe4ff_rgb, 0x0b2a6b_rgb),
        MakePalette(false, 0x7a4fd6_rgb, 0xebddff_rgb, 0x2b0f61_rgb),
        MakePalette(false, 0xe0603a_rgb, 0xffdbcf_rgb, 0x5a1a06_rgb),
        MakePalette(true, 0x6fdba3_rgb, 0x1f4d36_rgb, 0xc9f0d9_rgb),
        MakePalette(true, 0xa9c1ff_rgb, 0x2a3f78_rgb, 0xdbe4ff_rgb),
        MakePalette(true, 0xcdb6ff_rgb, 0x4a3383_rgb, 0xebddff_rgb),
        MakePalette(true, 0xffb59d_rgb, 0x7a2f17_rgb, 0xffdbcf_rgb),
    };
    return kAll[static_cast<std::size_t>((dark ? 4 : 0) + std::clamp(accent, 0, 3))];
}

Color Mix(Color a, Color b, float t)
{
    t = std::clamp(t, 0.f, 1.f);
    auto l = [t](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(std::lround(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t));
    };
    return {l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), l(a.a, b.a)};
}

std::string Thousands(double v)
{
    std::string digits = std::to_string(static_cast<long long>(std::llround(v)));
    std::string out;
    const int n = static_cast<int>(digits.size());
    for (int i = 0; i < n; ++i) {
        out += digits[static_cast<std::size_t>(i)];
        if ((n - 1 - i) % 3 == 0 && i != n - 1)
            out += ',';
    }
    return out;
}

std::string Distance(const Vitals& m, float km)
{
    return m.imperial() ? std::format("{:.1f} mi", km * 0.621371f) : std::format("{:.1f} km", km);
}

std::string WeightText(const Vitals& m, float kg)
{
    return m.imperial() ? std::format("{:.1f} lb", kg * 2.20462f) : std::format("{:.1f} kg", kg);
}

float Vitals::Rand(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }

Vitals::Vitals()
{
    pal = ThemePalette(false, 0);
    week = {7820.f, 11240.f, 9310.f, 5480.f, 12960.f, 10150.f, 6240.f};

    float hr = 72.f;
    for (int i = 0; i < 90; ++i) {
        hr = std::clamp(hr + Rand(-2.5f, 2.5f), 62.f, 88.f);
        hrHistory.push_back(hr);
    }

    workouts = std::vector<Workout>{
        {1, Ico::Run, "Morning run", "Today, 7:05 AM", 32, 312, 5.1f},
        {2, Ico::Lift, "Strength training", "Yesterday, 6:30 PM", 45, 268, 0.f},
        {3, Ico::Bike, "Outdoor cycle", "Tue, 5:50 PM", 58, 486, 18.4f},
        {4, Ico::Swim, "Pool swim", "Mon, 7:15 AM", 40, 352, 1.6f},
        {5, Ico::Run, "Interval run", "Sun, 8:20 AM", 28, 330, 4.6f},
    };

    // A realistic night: ~7h40 in four cycles, deep early, REM late.
    struct Planned { Stage stage; float len; };
    const Planned plan[] = {
        {Stage::Awake, 8}, {Stage::Light, 22}, {Stage::Deep, 48}, {Stage::Light, 18}, {Stage::Rem, 14},
        {Stage::Light, 26}, {Stage::Deep, 36}, {Stage::Light, 20}, {Stage::Rem, 24}, {Stage::Awake, 4},
        {Stage::Light, 34}, {Stage::Deep, 18}, {Stage::Light, 22}, {Stage::Rem, 32}, {Stage::Light, 38},
        {Stage::Awake, 6}, {Stage::Light, 18}, {Stage::Rem, 40}, {Stage::Light, 26}, {Stage::Awake, 10},
    };
    float t = 0.f;
    for (const auto& [stage, len] : plan) {
        night.push_back({stage, t, len});
        t += len;
    }
}

void Vitals::SetTab(int t)
{
    if (t == tab.Peek())
        return;
    tabDir = t > tab.Peek() ? 1 : -1;
    sheetOpen = false;
    tab = t;
}

void Vitals::Snack(std::string text, std::function<void()> undoFn)
{
    snackText = std::move(text);
    undo = std::move(undoFn);
    snackUntil = time + 3.2;
    snackShown = true;
}

void Vitals::Step(float dt)
{
    time += dt;

    // Ease the painter palette toward the theme so canvases fade with the CSS transitions.
    const Palette& target = ThemePalette(dark.Peek(), accent.Peek());
    const float k = 1.f - std::exp(-dt * 9.f);
    Color* cur = &pal.bg;
    const Color* dst = &target.bg;
    for (std::size_t i = 0; i < sizeof(Palette) / sizeof(Color); ++i)
        cur[i] = Mix(cur[i], dst[i], k);

    // Heart rate: new sample every 0.25 s, readout follows the newest sample.
    hrAcc += dt;
    while (hrAcc >= 0.25) {
        hrAcc -= 0.25;
        const float last = hrHistory.back();
        const float pull = (72.f - last) * 0.08f;
        hrHistory.erase(hrHistory.begin());
        hrHistory.push_back(std::clamp(last + pull + Rand(-2.2f, 2.2f), 58.f, 96.f));
        heartRate = std::round(hrHistory.back());
    }
    hrPhase = static_cast<float>(hrAcc / 0.25);

    // Steps arrive in small bursts, like a pedometer sync.
    stepAcc += dt;
    if (stepAcc >= 1.4) {
        stepAcc = 0.0;
        const float add = std::round(Rand(4.f, 22.f));
        steps = steps.Peek() + add;
        week[6] = steps.Peek();
    }

    if (sheetCloseAt >= 0.0 && time >= sheetCloseAt) {
        sheetCloseAt = -1.0;
        sheetOpen = false;
    }
    if (snackShown.Peek() && time >= snackUntil)
        snackShown = false;

    frame = frame.Peek() + 1;
}

} // namespace vitals
