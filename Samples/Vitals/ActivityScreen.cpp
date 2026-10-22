#include "Vitals.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace vitals {

namespace {

constexpr const char* kDays[7] = {"F", "S", "S", "M", "T", "W", "T"};
constexpr const char* kDayNames[7] = {"Friday", "Saturday", "Sunday", "Monday", "Tuesday", "Wednesday", "Today"};

Elem WeekChart(AppPtr m)
{
    auto surf = std::make_shared<Rect>();
    const Signal<int> sel = m->selectedDay;
    const Motion<float> grow = Spring<float>(1.f, 0.f, {.stiffness = 70.f, .damping = 15.f});
    const Motion<float> selPos = Spring<float>([sel] { return static_cast<float>(sel()); }, {.stiffness = 300.f, .damping = 28.f});

    InteractProps on;
    on.onPress = [surf, sel](PointerInfo info) {
        if (surf->width <= 0.f)
            return;
        const int i = static_cast<int>((info.position.x - surf->x) / (surf->width / 7.f));
        sel = std::clamp(i, 0, 6);
    };

    return Painter({.width = Expand, .height = Fixed(190.f),
                    .onPaint = [m, surf, grow, selPos, sel](Paint2D& p) {
                        *surf = p.SurfaceBounds();
                        const Rect b = p.Bounds();
                        const float slot = b.width / 7.f, barW = std::min(28.f, slot * 0.56f);
                        const float plotH = b.height - 26.f, maxV = 14000.f;
                        const float goal = static_cast<float>(m->stepGoal.Peek());
                        const float gy = b.y + plotH - goal / maxV * plotH;
                        // Highlight column slides to the selected day.
                        p.FillRect({b.x + slot * selPos.Peek() + 3.f, b.y, slot - 6.f, b.height}, m->pal.cardVar, 14.f);
                        for (float x = b.x; x < b.x + b.width; x += 9.f)
                            p.DrawLine({x, gy}, {std::min(x + 5.f, b.x + b.width), gy}, m->pal.muted.WithAlpha(120), 1.2f);
                        p.DrawString("goal", {b.x + 4.f, gy - 17.f}, m->pal.muted, 11.f);
                        for (int i = 0; i < 7; ++i) {
                            const float raw = i == 6 ? m->steps.Peek() : m->week[static_cast<std::size_t>(i)];
                            const float h = std::max(raw * grow.Peek() / maxV * plotH, barW);
                            const float x = b.x + slot * (static_cast<float>(i) + 0.5f) - barW * 0.5f;
                            const bool active = sel.Peek() == i;
                            const Color c = active ? m->pal.primary : (raw >= goal ? m->pal.primary.WithAlpha(150) : m->pal.primaryContainer);
                            p.FillRect({x, b.y + plotH - h, barW, h}, c, barW * 0.5f);
                            p.DrawString(kDays[i], {b.x + slot * (static_cast<float>(i) + 0.5f) - 4.f, b.y + plotH + 6.f},
                                         active ? m->pal.onSurface : m->pal.muted, 12.f, active);
                        }
                    },
                    .on = std::move(on),
                    .repaint = m->frame});
}

Elem WeekCard(AppPtr m)
{
    const Signal<int> sel = m->selectedDay;
    const Vitals* raw = m.get();
    const Signal<float> steps = m->steps;
    // Today's bar tracks the live step count.
    auto dayValue = [raw, sel, steps] { return sel() == 6 ? steps() : raw->week[static_cast<std::size_t>(sel())]; };
    return Card(m, "", {}, {
        CardHeader(m, Ico::Activity, [](const Palette& p) { return p.primary; }, "Steps this week",
                   Str([raw, steps] {
                       float sum = steps();
                       for (int i = 0; i < 6; ++i) sum += raw->week[static_cast<std::size_t>(i)];
                       return "Avg " + Thousands(sum / 7.f);
                   })),
        Row({.className = "big-row"},
            Text(Str([dayValue] { return Thousands(dayValue()); }), {.className = "big-number"}),
            Text(Str([sel] { return std::string("steps  ") + kDayNames[sel()]; }), {.className = "big-unit"})),
        WeekChart(m),
        Row({.className = "mini-stats"},
            Column({.className = "mini-stat"},
                Text(Str([raw, dayValue] { return Distance(*raw, dayValue() * 0.00072f); }), {.className = "mini-value"}),
                Text("Distance", {.className = "mini-label"})),
            Column({.className = "mini-stat"},
                Text(Str([raw, dayValue] { return std::format("{:.0f}", raw->MoveKcal(dayValue())); }), {.className = "mini-value"}),
                Text("Calories", {.className = "mini-label"})),
            Column({.className = "mini-stat"},
                Text(Str([dayValue] { return std::format("{:.0f} min", dayValue() / 160.f); }), {.className = "mini-value"}),
                Text("Active", {.className = "mini-label"})))});
}

Elem WorkoutRow(AppPtr m, const Workout& w)
{
    const Vitals* raw = m.get();
    const float km = w.km;
    const std::string sub = std::format("{} . {} min", w.when, w.minutes);
    return Row({.className = w.when == "Just now" ? "workout-row fresh" : "workout-row"},
        Box({.className = "workout-icon"}, IconView(m, w.icon, 22.f, [](const Palette& p) { return p.onPrimaryContainer; })),
        Column({.className = "workout-text"},
            Text(w.title, {.className = "workout-title"}),
            Text(sub, {.className = "workout-sub"})),
        Spacer(),
        Column({.className = "workout-side"},
            Text(std::format("{} kcal", w.kcal), {.className = "workout-kcal"}),
            Text(Str([raw, km] { return km > 0.f ? Distance(*raw, km) : std::string("indoor"); }), {.className = "workout-sub"})));
}

} // namespace

Elem ActivityScreen(AppPtr m)
{
    return Scroll({.layout = {.width = Expand, .height = Expand}, .className = "scroll"},
        Column({.className = m->tabDir > 0 ? "page enter-right" : "page enter-left"},
            Row({.className = "streak"},
                Box({.className = "streak-icon"}, IconView(m, Ico::Flame, 22.f, [](const Palette& p) { return p.onPrimary; })),
                Column({.className = "streak-text"},
                    Text("12-day streak", {.className = "streak-title"}),
                    Text("Step goal hit 12 days running", {.className = "streak-sub"})),
                Spacer(),
                Text("Best 21", {.className = "streak-best"})),
            WeekCard(m),
            Row({.className = "section-head"},
                Text("Recent workouts", {.className = "section-title"}),
                Spacer(),
                Text(Str([w = m->workouts] { return std::format("{} this week", w().size()); }), {.className = "section-trailing"})),
            Card(m, "list", {}, {Each(m->workouts, &Workout::id, [m](const Workout& w) { return WorkoutRow(m, w); })}),
            Box({.className = "page-end"})));
}

} // namespace vitals
