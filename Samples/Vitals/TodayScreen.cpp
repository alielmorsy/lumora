#include "Vitals.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace vitals {

namespace {

Elem StatChip(AppPtr m, Ico icon, std::function<Color(const Palette&)> tint, Str text)
{
    return Row({.className = "chip"}, IconView(m, icon, 16.f, std::move(tint)), Text(std::move(text), {.className = "chip-text"}));
}

Elem LegendRow(AppPtr m, std::function<Color(const Palette&)> tint, const char* label, Str value)
{
    return Row({.className = "legend-row"},
        Painter({.width = Fixed(10.f), .height = Fixed(10.f),
                 .onPaint = [m, tint](Paint2D& p) { p.FillCircle({p.Bounds().x + 5.f, p.Bounds().y + 5.f}, 5.f, tint(m->pal)); },
                 .repaint = m->frame}),
        Column({.className = "legend-text"},
            Text(label, {.className = "legend-label"}),
            Text(std::move(value), {.className = "legend-value"})));
}

Elem RingsCard(AppPtr m)
{
    const Signal<float> steps = m->steps, ex = m->exerciseMin, stand = m->standHours;
    const Signal<double> moveGoal = m->moveGoal;
    const Vitals* raw = m.get();
    return Card(m, "", [m] { m->SetTab(1); }, {
        Row({.className = "rings-row"},
            RingChart(m, 148.f),
            Column({.className = "legend"},
                LegendRow(m, [](const Palette& p) { return p.move; }, "Move",
                          Str([raw, steps, moveGoal] { return std::format("{:.0f} / {:.0f} kcal", raw->MoveKcal(steps()), moveGoal()); })),
                LegendRow(m, [](const Palette& p) { return p.exercise; }, "Exercise",
                          Str([ex] { return std::format("{:.0f} / 30 min", ex()); })),
                LegendRow(m, [](const Palette& p) { return p.stand; }, "Stand",
                          Str([stand] { return std::format("{:.0f} / 12 hr", stand()); }))))});
}

Elem StepsCard(AppPtr m)
{
    const Signal<float> steps = m->steps;
    const Signal<double> goal = m->stepGoal;
    const Motion<float> shown = Tween<float>(steps, {.duration = 0.9f, .easing = ease::CubicOut});
    return Card(m, "", [m] { m->SetTab(1); }, {
        CardHeader(m, Ico::Steps, [](const Palette& p) { return p.primary; }, "Steps",
                   Str([goal] { return "Goal " + Thousands(goal()); })),
        Row({.className = "big-row"},
            Text(Str([shown] { return Thousands(shown()); }), {.className = "big-number"}),
            Text("steps", {.className = "big-unit"})),
        ProgressBar([steps, goal] { return steps() / static_cast<float>(goal()); }, "primary"),
        Text(Str([steps, goal] {
                 const double left = goal() - steps();
                 return left > 0 ? Thousands(left) + " steps to your daily goal" : std::string("Daily goal reached. Nice work!");
             }),
             {.className = "caption"})});
}

void PaintHeartChart(Paint2D& p, const Vitals& m)
{
    const Rect b = p.Bounds();
    const Rect plot{b.x, b.y + 6.f, b.width - 30.f, b.height - 12.f};
    const float lo = 50.f, hi = 110.f;
    auto yOf = [&](float v) { return plot.y + plot.height - (v - lo) / (hi - lo) * plot.height; };
    for (float g : {60.f, 80.f, 100.f}) {
        const float y = yOf(g);
        for (float x = plot.x; x < plot.x + plot.width; x += 8.f)
            p.DrawLine({x, y}, {std::min(x + 4.f, plot.x + plot.width), y}, m.pal.outline, 1.f);
        p.DrawString(std::format("{:.0f}", g), {plot.x + plot.width + 6.f, y - 8.f}, m.pal.muted, 11.f);
    }
    const auto& h = m.hrHistory;
    const float step = plot.width / static_cast<float>(h.size() - 2);
    std::vector<Vec2> pts(h.size());
    for (std::size_t i = 0; i < h.size(); ++i)
        pts[i] = {plot.x + (static_cast<float>(i) - m.hrPhase) * step, yOf(h[i])};
    const float bottom = plot.y + plot.height;
    std::vector<PaintVertex> tris;
    const Color top = m.pal.heart.WithAlpha(70), clear = m.pal.heart.WithAlpha(0);
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const Vec2 a = pts[i], c = pts[i + 1];
        tris.insert(tris.end(), {PaintVertex{a, top}, PaintVertex{c, top}, PaintVertex{{c.x, bottom}, clear},
                                 PaintVertex{a, top}, PaintVertex{{c.x, bottom}, clear}, PaintVertex{{a.x, bottom}, clear}});
    }
    p.PushClip(plot);
    p.FillVertices(tris);
    p.DrawPolyline(pts, m.pal.heart, 2.2f, false, LineJoin::Round, LineCap::Round);
    p.PopClip();
    const Vec2 head = pts.back();
    const float beat = std::fmod(static_cast<float>(m.time) * m.heartRate.Peek() / 60.f, 1.f);
    p.FillCircle({std::min(head.x, plot.x + plot.width), head.y}, 5.f + beat * 9.f,
                 m.pal.heart.WithAlpha(static_cast<std::uint8_t>((1.f - beat) * 90.f)));
    p.FillCircle({std::min(head.x, plot.x + plot.width), head.y}, 4.5f, m.pal.heart);
}

Elem HeartCard(AppPtr m)
{
    const Signal<float> hr = m->heartRate;
    return Card(m, "", {}, {
        Row({.className = "card-head"},
            Box({.className = "heart-pulse"}, IconView(m, Ico::Heart, 20.f, [](const Palette& p) { return p.heart; })),
            Text("Heart rate", {.className = "card-title"}),
            Spacer(),
            Row({.className = "live-pill"}, Box({.className = "live-dot"}), Text("LIVE", {.className = "live-text"}))),
        Row({.className = "big-row"},
            Text(Str([hr] { return std::format("{:.0f}", hr()); }), {.className = "big-number"}),
            Text("bpm", {.className = "big-unit"}),
            Spacer(),
            Column({.className = "hr-side"},
                Text("Resting 58", {.className = "caption"}),
                Text("Range 54-131", {.className = "caption"}))),
        Painter({.width = Expand, .height = Fixed(110.f),
                 .onPaint = [m](Paint2D& p) { PaintHeartChart(p, *m); },
                 .repaint = m->frame})});
}

Elem Glass(AppPtr m, int i)
{
    const Signal<int> water = m->water;
    const Motion<float> level = Spring<float>([water, i] { return water() > i ? 100.f : 0.f; }, {.stiffness = 120.f, .damping = 16.f});
    return Column({.layout = {.width = Expand},
                   .on = {.onClick = [water, i] { water = water.Peek() == i + 1 ? i : i + 1; }},
                   .className = "glass"},
        Box({.layout = {.width = Expand, .height = [level] { return Percent(std::clamp(level(), 0.f, 100.f)); }},
             .className = "glass-fill"}));
}

Elem WaterCard(AppPtr m)
{
    const Signal<int> water = m->water;
    const Signal<double> goal = m->waterGoal;
    const Computed<std::vector<int>> slots([goal] {
        std::vector<int> v(static_cast<std::size_t>(std::clamp(goal(), 4.0, 12.0)));
        for (std::size_t i = 0; i < v.size(); ++i)
            v[i] = static_cast<int>(i);
        return v;
    });
    return Card(m, "", {}, {
        CardHeader(m, Ico::Water, [](const Palette& p) { return p.water; }, "Water",
                   Str([water, goal] { return std::format("{} / {} ml", Thousands(water() * 250), Thousands(goal() * 250)); })),
        Row({.className = "glasses"}, Each(slots, [m](const int& i) { return Glass(m, i); })),
        Text(Str([water, goal] {
                 const int left = static_cast<int>(goal()) - water();
                 return left > 0 ? std::format("Tap a glass to log it. {} to go.", left) : std::string("Hydration goal met.");
             }),
             {.className = "caption"})});
}

} // namespace

Elem TodayScreen(AppPtr m)
{
    const Signal<float> steps = m->steps;
    const Vitals* raw = m.get();
    const Signal<float> ex = m->exerciseMin;
    return Scroll({.layout = {.width = Expand, .height = Expand}, .className = "scroll"},
        Column({.className = m->tabDir > 0 ? "page enter-right" : "page enter-left"},
            Column({.className = "hello"},
                Text("Good morning, Sam", {.className = "hello-title"}),
                Text(Str([raw, steps, g = m->moveGoal] {
                         return std::format("You're {:.0f}% of the way to your move goal.",
                                            std::min(100.f, raw->MoveKcal(steps()) / static_cast<float>(g()) * 100.f));
                     }),
                     {.className = "hello-sub"})),
            Row({.className = "chips"},
                StatChip(m, Ico::Pin, [](const Palette& p) { return p.primary; }, Str([raw, steps] { return Distance(*raw, steps() * 0.00072f); })),
                StatChip(m, Ico::Flame, [](const Palette& p) { return p.move; }, Str([raw, steps] { return std::format("{:.0f} kcal", raw->MoveKcal(steps())); })),
                StatChip(m, Ico::Bolt, [](const Palette& p) { return p.exercise; }, Str([ex] { return std::format("{:.0f} min", ex()); })),
                StatChip(m, Ico::Stairs, [](const Palette& p) { return p.stand; }, "9 floors")),
            RingsCard(m),
            StepsCard(m),
            HeartCard(m),
            WaterCard(m),
            Box({.className = "page-end"})));
}

} // namespace vitals
