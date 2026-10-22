#include "Vitals.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace vitals {

namespace {

constexpr const char* kStageNames[4] = {"Awake", "REM", "Light", "Deep"};
constexpr float kBedMinute = 23.f * 60.f + 10.f; // 11:10 PM

std::string ClockAt(float minutesAfterBed)
{
    const int t = static_cast<int>(kBedMinute + minutesAfterBed) % (24 * 60);
    const int h = t / 60, mm = t % 60;
    return std::format("{}:{:02} {}", h % 12 == 0 ? 12 : h % 12, mm, h < 12 ? "AM" : "PM");
}

std::string Hm(float minutes)
{
    const int t = static_cast<int>(std::lround(minutes));
    return std::format("{}h {:02}m", t / 60, t % 60);
}

float NightLength(const Vitals& m) { return m.night.back().start + m.night.back().len; }

void PaintStages(Paint2D& p, const Vitals& m, float reveal)
{
    const Rect b = p.Bounds();
    const float labelW = 46.f, axisH = 22.f;
    const Rect plot{b.x + labelW, b.y + 4.f, b.width - labelW - 4.f, b.height - axisH - 4.f};
    const float rowH = plot.height / 4.f, barH = std::min(18.f, rowH * 0.62f);
    const float total = NightLength(m);
    auto xOf = [&](float minute) { return plot.x + minute / total * plot.width; };
    auto yOf = [&](Stage s) { return plot.y + rowH * (static_cast<float>(s) + 0.5f); };

    for (int r = 0; r < 4; ++r) {
        const float y = plot.y + rowH * (static_cast<float>(r) + 0.5f);
        p.DrawLine({plot.x, y}, {plot.x + plot.width, y}, m.pal.outline, 1.f);
        p.DrawString(kStageNames[r], {b.x, y - 8.f}, m.pal.muted, 11.f);
    }
    // Hour ticks on the axis.
    for (float hour = 0.f; hour <= 8.f; hour += 2.f) {
        const float minute = (24.f * 60.f - kBedMinute) + hour * 60.f; // midnight + n hours
        if (minute > total)
            break;
        const int h = static_cast<int>(hour);
        const std::string label = std::format("{} AM", h == 0 ? 12 : h);
        p.DrawString(label, {xOf(minute) - 14.f, plot.y + plot.height + 6.f}, m.pal.muted, 11.f);
    }

    p.PushClip({b.x, b.y, labelW + plot.width * reveal + 2.f, b.height});
    for (std::size_t i = 0; i < m.night.size(); ++i) {
        const SleepSeg& s = m.night[i];
        const Color c = m.pal.stages[static_cast<std::size_t>(s.stage)];
        const float x0 = xOf(s.start), x1 = xOf(s.start + s.len);
        if (i > 0) {
            const SleepSeg& prev = m.night[i - 1];
            p.DrawLine({x0, yOf(prev.stage)}, {x0, yOf(s.stage)}, Mix(m.pal.stages[static_cast<std::size_t>(prev.stage)], c, 0.5f).WithAlpha(150), 2.f);
        }
        p.FillRect({x0, yOf(s.stage) - barH * 0.5f, std::max(x1 - x0, 3.f), barH}, c, 6.f);
    }
    p.PopClip();

    // Hover scrub: time and stage under the pointer.
    const Vec2 mouse = p.Mouse();
    if (mouse.x >= plot.x && mouse.x <= plot.x + plot.width && mouse.y >= b.y && mouse.y <= b.y + b.height) {
        const float minute = (mouse.x - plot.x) / plot.width * total;
        const auto it = std::ranges::find_if(m.night, [minute](const SleepSeg& s) { return minute < s.start + s.len; });
        if (it != m.night.end()) {
            p.DrawLine({mouse.x, plot.y}, {mouse.x, plot.y + plot.height}, m.pal.onSurface.WithAlpha(120), 1.5f);
            const std::string tip = std::format("{}  {}", ClockAt(minute), kStageNames[static_cast<int>(it->stage)]);
            const float w = static_cast<float>(tip.size()) * 6.6f + 16.f;
            const float tx = std::clamp(mouse.x - w * 0.5f, b.x, b.x + b.width - w);
            p.FillRect({tx, b.y, w, 22.f}, m.pal.onSurface, 11.f);
            p.DrawString(tip, {tx + 8.f, b.y + 3.f}, m.pal.card, 12.f);
        }
    }
}

Elem StageRow(AppPtr m, Stage s, float minutes, float total)
{
    const int idx = static_cast<int>(s);
    return Row({.className = "stage-row"},
        Painter({.width = Fixed(12.f), .height = Fixed(12.f),
                 .onPaint = [m, idx](Paint2D& p) { p.FillRect(p.Bounds(), m->pal.stages[static_cast<std::size_t>(idx)], 4.f); },
                 .repaint = m->frame}),
        Text(kStageNames[idx], {.className = "stage-name"}),
        ProgressBar(minutes / total, "stage s" + std::to_string(idx)),
        Text(Hm(minutes), {.className = "stage-time"}));
}

Elem TimeCard(AppPtr m, Ico icon, const char* label, std::string value, std::string sub)
{
    return Card(m, "half", {}, {
        Row({.className = "card-head"},
            IconView(m, icon, 20.f, [](const Palette& p) { return p.primary; }),
            Text(label, {.className = "card-title"})),
        Text(std::move(value), {.className = "time-value"}),
        Text(std::move(sub), {.className = "caption"})});
}

} // namespace

Elem SleepScreen(AppPtr m)
{
    const float total = NightLength(*m);
    std::array<float, 4> per{};
    for (const SleepSeg& s : m->night)
        per[static_cast<std::size_t>(s.stage)] += s.len;
    const float asleep = total - per[0];
    // Tween seeds at its target, so drive the left-to-right reveal from 0 explicitly.
    Signal<float> revealSig{0.f};
    AnimateTo(revealSig, 1.f, {.duration = 1.1f, .easing = ease::CubicOut, .delay = 0.15f});

    std::vector<Elem> rows;
    for (int i : {3, 2, 1, 0})
        rows.push_back(StageRow(m, static_cast<Stage>(i), per[static_cast<std::size_t>(i)], total));

    return Scroll({.layout = {.width = Expand, .height = Expand}, .className = "scroll"},
        Column({.className = m->tabDir > 0 ? "page enter-right" : "page enter-left"},
            Card(m, "", {}, {
                Row({.className = "score-row"},
                    ScoreRing(m, 84.f, 128.f),
                    Column({.className = "score-text"},
                        Text("Restful night", {.className = "score-title"}),
                        Text(Hm(asleep) + " asleep", {.className = "score-sub"}),
                        Row({.className = "tag"}, Text("+18 min vs. 30-day avg", {.className = "tag-text"}))))}),
            Row({.className = "pair"},
                TimeCard(m, Ico::Moon, "Bedtime", ClockAt(0.f), "Target 11:00 PM"),
                TimeCard(m, Ico::Sun, "Wake up", ClockAt(total), "Alarm 7:00 AM")),
            Card(m, "", {}, {
                CardHeader(m, Ico::Sleep, [](const Palette& p) { return p.stages[2]; }, "Sleep stages", Hm(total) + " in bed"),
                Painter({.width = Expand, .height = Fixed(176.f),
                         .onPaint = [m, revealSig](Paint2D& p) { PaintStages(p, *m, revealSig.Peek()); },
                         .repaint = m->frame}),
                Column({.className = "stage-list"}, std::move(rows))}),
            Box({.className = "page-end"})));
}

} // namespace vitals
