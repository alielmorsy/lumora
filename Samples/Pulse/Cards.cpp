#include "Pulse.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace pulse {

namespace {

std::string FormatValue(float v, int decimals)
{
    if (decimals == 0)
        return Thousands(v);
    return std::format("{:.{}f}", v, decimals);
}

// Area + line + glowing head for one history buffer.
void DrawSpark(Paint2D& p, const std::vector<float>& hist, Color color, float time, const Palette& pal)
{
    const Rect b = p.Bounds();
    if (hist.size() < 2 || b.width <= 0.f)
        return;
    auto [lo, hi] = std::ranges::minmax(hist);
    const float span = std::max(hi - lo, 1e-3f);
    lo -= span * 0.15f;
    const float range = span * 1.3f;
    const float stepX = b.width / static_cast<float>(kSparkLen - 1);
    const float x0 = b.x + b.width - stepX * static_cast<float>(hist.size() - 1);
    const float bottom = b.y + b.height;

    p.DrawLine({b.x, bottom - 0.5f}, {b.x + b.width, bottom - 0.5f}, pal.grid, 1.f);

    std::vector<Vec2> pts(hist.size());
    for (std::size_t i = 0; i < hist.size(); ++i)
        pts[i] = {x0 + stepX * static_cast<float>(i), b.y + b.height - (hist[i] - lo) / range * b.height};

    std::vector<PaintVertex> tris;
    tris.reserve(pts.size() * 6);
    const Color top = color.WithAlpha(110), clear = color.WithAlpha(0);
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const Vec2 a = pts[i], c = pts[i + 1];
        tris.push_back({a, top});
        tris.push_back({c, top});
        tris.push_back({{c.x, bottom}, clear});
        tris.push_back({a, top});
        tris.push_back({{c.x, bottom}, clear});
        tris.push_back({{a.x, bottom}, clear});
    }
    p.FillVertices(tris);
    p.DrawPolyline(pts, color, 1.75f, false, LineJoin::Round, LineCap::Round);

    const Vec2 head = pts.back();
    const float pulse = 0.5f + 0.5f * std::sin(time * 5.f);
    p.FillCircle(head, 5.f + pulse * 3.f, color.WithAlpha(static_cast<std::uint8_t>(70 - pulse * 50)));
    p.FillCircle(head, 3.f, color);
}

Elem MetricCard(FeedPtr f, int i)
{
    Metric& m = f->metrics[static_cast<std::size_t>(i)];
    const Motion<float> shown = Tween<float>(m.shown, {.duration = 0.45f, .easing = ease::CubicOut});
    const Signal<float> delta = m.delta;
    const int decimals = m.decimals;
    const std::string dot = "metric-dot c" + std::to_string(i % 6);
    OnCleanup([f, i] { f->cardRects[static_cast<std::size_t>(i)] = {}; }); // no bursts from unmounted cards

    return Column({.className = "card metric"},
        Row({.className = "metric-head"},
            Box({.className = dot}),
            Text(m.name, {.className = "metric-name"}),
            Spacer(),
            Row({.className = [delta] { return std::string(delta() >= 0.f ? "delta up" : "delta down"); }},
                Text(Str([delta] { return std::format("{}{:.2f}%", delta() >= 0.f ? "+" : "", delta()); }),
                     {.className = "delta-text"}))),
        Row({.className = "metric-value-row"},
            Text(Str([shown, decimals] { return FormatValue(shown(), decimals); }), {.className = "metric-value"}),
            Text(m.unit, {.className = "metric-unit"})),
        Painter({.onPaint = [f, i](Paint2D& p) {
                     ProfScope prof(Prof::Spark);
                     f->cardRects[static_cast<std::size_t>(i)] = p.SurfaceBounds();
                     const Palette& pal = Colors(f->dark.Peek());
                     DrawSpark(p, f->metrics[static_cast<std::size_t>(i)].hist, pal.accents[static_cast<std::size_t>(i % 6)],
                               static_cast<float>(f->wallTime), pal);
                 },
                 .repaint = f->frame}));
}

} // namespace

Elem MetricGrid(FeedPtr f)
{
    std::vector<Elem> cards;
    for (int i = 0; i < kMetricCount; ++i)
        cards.push_back(MetricCard(f, i));
    return Grid({.columns = 6, .rows = 2, .gap = 12.f, .layout = {.width = Expand, .height = Fixed(244.f)}},
                std::move(cards));
}

// Detail tab: every metric as a live row.
Elem MetricTable(FeedPtr f)
{
    std::vector<int> ids(kMetricCount);
    for (int i = 0; i < kMetricCount; ++i)
        ids[static_cast<std::size_t>(i)] = i;
    return Column({.className = "table"},
        Row({.className = "table-head"},
            Text("Metric", {.layout = {.width = Grow(3.f)}, .className = "tcol"}),
            Text("Value", {.layout = {.width = Grow(2.f)}, .className = "tcol tval"}),
            Text("Change", {.layout = {.width = Grow(2.f)}, .className = "tcol tval"}),
            Text("Trend", {.layout = {.width = Grow(2.f)}, .className = "tcol tval"})),
        Each(ids, [f](const int& i, int) {
            Metric& m = f->metrics[static_cast<std::size_t>(i)];
            const Signal<float> shown = m.shown, delta = m.delta;
            const int dec = m.decimals;
            const std::string unit = m.unit;
            return Row({.className = i % 2 ? "table-row odd" : "table-row"},
                Row({.className = "tname"},
                    Box({.className = "metric-dot c" + std::to_string(i % 6)}),
                    Text(m.name, {.className = "trow-name"})),
                Text(Str([shown, dec, unit] { return FormatValue(shown(), dec) + " " + unit; }), {.layout = {.width = Grow(2.f)}, .className = "tval tnum"}),
                Text(Str([delta] { return std::format("{}{:.2f}%", delta() >= 0.f ? "+" : "", delta()); }),
                     {.layout = {.width = Grow(2.f)}, .className = Str([delta] { return std::string(delta() >= 0.f ? "tval tup" : "tval tdown"); })}),
                Painter({.width = Grow(2.f), .height = Fixed(26.f),
                         .onPaint = [f, i](Paint2D& p) {
                             ProfScope prof(Prof::Spark);
                             const Palette& pal = Colors(f->dark.Peek());
                             DrawSpark(p, f->metrics[static_cast<std::size_t>(i)].hist,
                                       pal.accents[static_cast<std::size_t>(i % 6)], static_cast<float>(f->wallTime), pal);
                         },
                         .repaint = f->frame}));
        }));
}

} // namespace pulse
