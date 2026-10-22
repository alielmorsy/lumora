#include "Pulse.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace pulse {

namespace {

constexpr const char* kSeriesNames[kSeriesCount] = {"Requests (k)", "Latency (ms)", "Errors (/s)"};
constexpr const char* kDays[kHeatRows] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

Color Mix(Color a, Color b, float t)
{
    t = std::clamp(t, 0.f, 1.f);
    auto l = [t](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(x + (y - x) * t); };
    return {l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), l(a.a, b.a)};
}

// ponytail: Paint2D has no text measuring, so right-aligned labels use an average glyph width.
float ApproxWidth(std::string_view s, float size) { return static_cast<float>(s.size()) * size * 0.56f; }

void PaintStream(Paint2D& p, Feed& f)
{
    const Palette& pal = Colors(f.dark.Peek());
    const Rect b = p.Bounds();
    const Rect plot{b.x + 44.f, b.y + 30.f, b.width - 56.f, b.height - 54.f};
    if (plot.width <= 0.f || plot.height <= 0.f)
        return;
    const float top = f.chartMax;
    const float bottom = plot.y + plot.height;
    const float stepX = plot.width / static_cast<float>(kSeriesLen - 1);
    auto yOf = [&](float v) { return bottom - v / top * plot.height; };

    // Horizontal gridlines + y labels.
    for (int g = 0; g <= 5; ++g) {
        const float v = top * static_cast<float>(g) / 5.f;
        const float y = yOf(v);
        p.DrawLine({plot.x, y}, {plot.x + plot.width, y}, g == 0 ? pal.muted.WithAlpha(120) : pal.grid, 1.f);
        const std::string label = std::format("{:.0f}", v);
        p.DrawString(label, {plot.x - 8.f - ApproxWidth(label, 11.f), y - 8.f}, pal.muted, 11.f);
    }
    // Vertical gridlines scroll with the data; one every 60 samples (2 s of sim time).
    const int phase = static_cast<int>(f.samples % 60);
    for (int i = kSeriesLen - 1 - phase; i >= 0; i -= 60) {
        const float x = plot.x + stepX * static_cast<float>(i);
        p.DrawLine({x, plot.y}, {x, bottom}, pal.grid, 1.f);
        const double ago = static_cast<double>(kSeriesLen - 1 - i) / 30.0;
        const std::string label = ago < 0.01 ? std::string("now") : std::format("-{:.0f}s", ago);
        p.DrawString(label, {x - ApproxWidth(label, 11.f) * 0.5f, bottom + 6.f}, pal.muted, 11.f);
    }

    // Series: gradient areas back to front, then lines on top.
    static std::vector<Vec2> pts[kSeriesCount];
    static std::vector<PaintVertex> tris;
    for (int s = kSeriesCount - 1; s >= 0; --s) {
        const auto& data = f.series[static_cast<std::size_t>(s)];
        auto& line = pts[s];
        line.resize(data.size());
        const float x0 = plot.x + plot.width - stepX * static_cast<float>(data.size() - 1);
        for (std::size_t i = 0; i < data.size(); ++i)
            line[i] = {x0 + stepX * static_cast<float>(i), yOf(data[i])};
        const Color c = pal.accents[static_cast<std::size_t>(s)];
        const Color hi = c.WithAlpha(s == 0 ? 90 : 60), lo = c.WithAlpha(0);
        tris.clear();
        for (std::size_t i = 0; i + 1 < line.size(); ++i) {
            const Vec2 a = line[i], d = line[i + 1];
            tris.insert(tris.end(), {PaintVertex{a, hi}, PaintVertex{d, hi}, PaintVertex{{d.x, bottom}, lo},
                                     PaintVertex{a, hi}, PaintVertex{{d.x, bottom}, lo}, PaintVertex{{a.x, bottom}, lo}});
        }
        p.FillVertices(tris);
    }
    for (int s = kSeriesCount - 1; s >= 0; --s)
        p.DrawPolyline(pts[s], pal.accents[static_cast<std::size_t>(s)], s == 0 ? 2.25f : 1.75f, false, LineJoin::Round);

    // Live heads.
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(f.wallTime) * 4.f);
    for (int s = 0; s < kSeriesCount; ++s) {
        if (pts[s].empty())
            continue;
        const Color c = pal.accents[static_cast<std::size_t>(s)];
        p.FillCircle(pts[s].back(), 6.f + pulse * 5.f, c.WithAlpha(static_cast<std::uint8_t>(80 - pulse * 60)));
        p.FillCircle(pts[s].back(), 3.5f, c);
    }

    // Legend.
    float lx = plot.x;
    for (int s = 0; s < kSeriesCount; ++s) {
        const auto& data = f.series[static_cast<std::size_t>(s)];
        const Color c = pal.accents[static_cast<std::size_t>(s)];
        p.FillRect({lx, b.y + 8.f, 10.f, 10.f}, c, 3.f);
        const std::string text = std::format("{}  {:.1f}", kSeriesNames[s], data.empty() ? 0.f : data.back());
        p.DrawString(text, {lx + 16.f, b.y + 4.f}, pal.text, 12.f);
        lx += ApproxWidth(text, 12.f) + 40.f;
    }

    // Crosshair + tooltip.
    const Vec2 m = p.Mouse();
    if (m.x >= plot.x && m.x <= plot.x + plot.width && m.y >= plot.y && m.y <= bottom) {
        const int idx = std::clamp(static_cast<int>(std::lround((m.x - plot.x) / stepX)), 0, kSeriesLen - 1);
        const float x = plot.x + stepX * static_cast<float>(idx);
        p.DrawLine({x, plot.y}, {x, bottom}, pal.text.WithAlpha(110), 1.f);
        p.DrawLine({plot.x, m.y}, {plot.x + plot.width, m.y}, pal.text.WithAlpha(50), 1.f);
        const float tipW = 168.f, tipH = 20.f + 18.f * kSeriesCount;
        float tx = x + 14.f;
        if (tx + tipW > plot.x + plot.width)
            tx = x - 14.f - tipW;
        const float ty = std::clamp(m.y - tipH * 0.5f, plot.y, bottom - tipH);
        p.FillRect({tx, ty, tipW, tipH}, pal.surface.WithAlpha(240), 8.f);
        p.StrokeRect({tx, ty, tipW, tipH}, pal.grid, 1.f, 8.f);
        const double ago = static_cast<double>(kSeriesLen - 1 - idx) / 30.0;
        p.DrawString(std::format("t -{:.2f}s", ago), {tx + 10.f, ty + 4.f}, pal.muted, 11.f);
        for (int s = 0; s < kSeriesCount; ++s) {
            const auto& data = f.series[static_cast<std::size_t>(s)];
            const int off = idx - (kSeriesLen - static_cast<int>(data.size()));
            if (off < 0)
                continue;
            const float v = data[static_cast<std::size_t>(off)];
            const Color c = pal.accents[static_cast<std::size_t>(s)];
            p.FillCircle({x, yOf(v)}, 7.f, c.WithAlpha(60));
            p.FillCircle({x, yOf(v)}, 4.f, c);
            const float rowY = ty + 22.f + 18.f * static_cast<float>(s);
            p.FillCircle({tx + 14.f, rowY + 8.f}, 4.f, c);
            p.DrawString(std::format("{:<13} {:6.1f}", kSeriesNames[s], v), {tx + 24.f, rowY}, pal.text, 12.f);
        }
    }
}

void PaintHeat(Paint2D& p, Feed& f)
{
    const Palette& pal = Colors(f.dark.Peek());
    const Rect b = p.Bounds();
    const float labelW = 34.f, gap = 3.f;
    const float cw = (b.width - labelW - gap * (kHeatCols - 1)) / kHeatCols;
    const float ch = (b.height - 16.f - gap * (kHeatRows - 1)) / kHeatRows;
    if (cw <= 0.f || ch <= 0.f)
        return;
    float peak = 0.2f;
    for (float v : f.heat)
        peak = std::max(peak, v);
    const Color hot = pal.accents[1];
    const Vec2 m = p.Mouse();
    int hoverCol = -1, hoverRow = -1;
    for (int c = 0; c < kHeatCols; ++c) {
        for (int r = 0; r < kHeatRows; ++r) {
            const float v = f.heat[static_cast<std::size_t>(c * kHeatRows + r)] / peak;
            const Rect cell{b.x + labelW + c * (cw + gap), b.y + r * (ch + gap), cw, ch};
            p.FillRect(cell, Mix(pal.track, hot, std::sqrt(v)), 3.f);
            if (cell.Contains(m)) {
                hoverCol = c;
                hoverRow = r;
            }
        }
    }
    // The live column breathes.
    const float glow = 0.5f + 0.5f * std::sin(static_cast<float>(f.wallTime) * 3.f);
    const Rect live{b.x + labelW + (kHeatCols - 1) * (cw + gap) - 2.f, b.y - 2.f, cw + 4.f, kHeatRows * (ch + gap) - gap + 4.f};
    p.StrokeRect(live, hot.WithAlpha(static_cast<std::uint8_t>(90 + glow * 140)), 1.5f, 4.f);

    for (int r = 0; r < kHeatRows; ++r)
        p.DrawString(kDays[r], {b.x, b.y + r * (ch + gap) + ch * 0.5f - 7.f}, pal.muted, 11.f);
    p.DrawString("30 periods ago", {b.x + labelW, b.y + b.height - 14.f}, pal.muted, 10.f);
    p.DrawString("now", {b.x + b.width - 20.f, b.y + b.height - 14.f}, pal.muted, 10.f);

    if (hoverCol >= 0) {
        const Rect cell{b.x + labelW + hoverCol * (cw + gap), b.y + hoverRow * (ch + gap), cw, ch};
        p.StrokeRect(cell, pal.text, 1.5f, 3.f);
        const std::string tip = std::format("{} / P-{}: {:.0f}%", kDays[hoverRow], kHeatCols - 1 - hoverCol,
                                            f.heat[static_cast<std::size_t>(hoverCol * kHeatRows + hoverRow)] / peak * 100.f);
        const float w = ApproxWidth(tip, 12.f) + 16.f;
        float tx = std::min(cell.x, b.x + b.width - w);
        const float ty = cell.y > b.y + 30.f ? cell.y - 28.f : cell.y + ch + 4.f;
        p.FillRect({tx, ty, w, 22.f}, pal.surface.WithAlpha(245), 6.f);
        p.StrokeRect({tx, ty, w, 22.f}, pal.grid, 1.f, 6.f);
        p.DrawString(tip, {tx + 8.f, ty + 3.f}, pal.text, 12.f);
    }
}

} // namespace

Elem StreamChart(FeedPtr f, float height)
{
    return Painter({.width = Expand, .height = height > 0.f ? Fixed(height) : Expand,
                    .onPaint = [f](Paint2D& p) { ProfScope prof(Prof::Stream); PaintStream(p, *f); },
                    .repaint = f->frame});
}

Elem Heatmap(FeedPtr f, float height)
{
    return Painter({.width = Expand, .height = height > 0.f ? Fixed(height) : Expand,
                    .onPaint = [f](Paint2D& p) { ProfScope prof(Prof::Heat); PaintHeat(p, *f); },
                    .repaint = f->frame});
}

// Bars that slide to their new rank: each row is absolutely placed and springs its top inset.
Elem BarRace(FeedPtr f)
{
    constexpr float kRowH = 26.f;
    std::vector<Elem> rows;
    for (int i = 0; i < kRaceCount; ++i) {
        const Signal<int> rank = f->raceRank[static_cast<std::size_t>(i)];
        const Signal<float> share = f->raceShare[static_cast<std::size_t>(i)];
        const Motion<float> top = Spring<float>([rank] { return static_cast<float>(rank()) * kRowH; },
                                                {.stiffness = 120.f, .damping = 18.f});
        const Motion<float> width = Tween<float>(share, {.duration = 0.5f, .easing = ease::CubicOut});
        rows.push_back(Row({.layout = {.width = Expand, .height = Fixed(kRowH - 6.f)},
                            .position = {.position = Position::Absolute, .insetLeft = 0.f, .insetTop = top, .insetRight = 0.f},
                            .className = "race-row"},
            Text(Str([rank] { return std::format("#{}", rank() + 1); }), {.className = "race-rank"}),
            Text(f->raceNames[static_cast<std::size_t>(i)], {.className = "race-name"}),
            Row({.className = "race-track"},
                Box({.layout = {.width = [width] { return Percent(std::max(width(), 2.f)); }, .height = Expand},
                     .className = "race-fill c" + std::to_string(i % 6)})),
            Text(Str([v = f->raceValue[static_cast<std::size_t>(i)]] { return Thousands(v()); }),
                 {.className = "race-value"})));
    }
    return Box({.layout = {.width = Expand, .height = Fixed(kRowH * kRaceCount)}, .anchor = true}, std::move(rows));
}

} // namespace pulse
