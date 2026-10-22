#include "Pulse.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace pulse {

namespace {

constexpr float kPi = 3.14159265f;

struct GaugeSpec {
    const char* name;
    const char* unit;
    float       lo, hi;
    int         accent;
};

constexpr GaugeSpec kSpecs[kGaugeCount] = {
    {"CPU", "%", 0.f, 100.f, 0},
    {"Memory", "%", 0.f, 100.f, 3},
    {"Heart rate", "bpm", 40.f, 180.f, 2},
    {"Network", "k/s", 0.f, 140.f, 5},
};

Vec2 Polar(Vec2 c, float r, float a) { return {c.x + std::cos(a) * r, c.y + std::sin(a) * r}; }

void PaintGauge(Paint2D& p, const Feed& f, int i, float value)
{
    const Palette& pal = Colors(f.dark.Peek());
    const GaugeSpec& s = kSpecs[i];
    const Rect b = p.Bounds();
    const Vec2 c{b.x + b.width * 0.5f, b.y + b.height * 0.54f};
    const float r = std::min(b.width, b.height) * 0.38f;
    const float t = std::clamp((value - s.lo) / (s.hi - s.lo), 0.f, 1.f);
    // 270 degree dial, gap at the bottom; angles are y-down, so it fills clockwise from lower left.
    const float a0 = kPi * 0.75f, sweep = kPi * 1.5f;
    Color col = pal.accents[static_cast<std::size_t>(s.accent)];
    if (t > 0.85f)
        col = pal.accents[2];

    for (int k = 0; k <= 30; ++k) {
        const float a = a0 + sweep * static_cast<float>(k) / 30.f;
        const bool major = k % 5 == 0;
        const bool lit = static_cast<float>(k) / 30.f <= t;
        p.DrawLine(Polar(c, r + 10.f, a), Polar(c, r + (major ? 17.f : 13.f), a),
                   lit ? col.WithAlpha(major ? 230 : 150) : pal.grid, major ? 2.f : 1.f);
    }
    p.StrokeArc(c, r, a0, a0 + sweep, pal.grid, 10.f);
    p.StrokeArc(c, r, a0, a0 + sweep * t, col.WithAlpha(60), 18.f);
    p.StrokeArc(c, r, a0, a0 + sweep * t, col, 10.f);
    const Vec2 tip = Polar(c, r, a0 + sweep * t);
    p.FillCircle(tip, 7.f, pal.surface);
    p.FillCircle(tip, 4.5f, col);

    // Heart rate: rings expand on every beat.
    if (i == 2) {
        const float ph = f.beatPhase - std::floor(f.beatPhase);
        p.StrokeCircle(c, r * 0.35f + ph * r * 0.5f, col.WithAlpha(static_cast<std::uint8_t>((1.f - ph) * 150.f)), 2.f);
        const float beat = std::exp(-ph * 9.f);
        p.FillCircle(c, r * (0.16f + beat * 0.06f), col.WithAlpha(static_cast<std::uint8_t>(40 + beat * 60)));
    }

    const std::string num = std::format("{:.0f}", value);
    const float size = r * 0.48f;
    p.DrawString(num, {c.x - static_cast<float>(num.size()) * size * 0.28f, c.y - size * 0.75f}, pal.text, size, true);
    p.DrawString(s.unit, {c.x - static_cast<float>(std::string_view(s.unit).size()) * 3.2f, c.y + size * 0.45f}, pal.muted, 11.f);
}

Elem Gauge(FeedPtr f, int i)
{
    const Motion<float> value = Spring<float>(f->gauges[static_cast<std::size_t>(i)], {.stiffness = 90.f, .damping = 14.f});
    return Column({.className = "gauge"},
        Painter({.onPaint = [f, i, value](Paint2D& p) { ProfScope prof(Prof::Gauge); PaintGauge(p, *f, i, value.Peek()); }, .repaint = f->frame}),
        Text(kSpecs[i].name, {.className = "gauge-label"}));
}

} // namespace

Elem GaugePanel(FeedPtr f)
{
    std::vector<Elem> cells;
    for (int i = 0; i < kGaugeCount; ++i)
        cells.push_back(Gauge(f, i));
    return Grid({.columns = 2, .rows = 2, .gap = 8.f, .layout = {.width = Expand, .height = Expand}}, std::move(cells));
}

} // namespace pulse
