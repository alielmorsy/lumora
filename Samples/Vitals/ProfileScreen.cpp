#include "Vitals.h"

#include <format>

namespace vitals {

namespace {

Elem GoalSlider(AppPtr m, const char* label, Signal<double> value, double lo, double hi, double step,
                std::function<std::string(double)> fmt)
{
    const Signal<bool> dark = m->dark;
    const Signal<int> accent = m->accent;
    return Column({.className = "goal"},
        Row({.className = "goal-head"},
            Text(label, {.className = "goal-label"}),
            Spacer(),
            Text(Str([value, fmt] { return fmt(value()); }), {.className = "goal-value"})),
        Slider({.value = value, .min = lo, .max = hi, .step = step,
                .layout = {.width = Expand},
                .fill = [dark, accent] { return ThemePalette(dark(), accent()).primary; },
                .className = "slider"}));
}

Elem Segment(AppPtr m, const char* label, Ico icon, bool darkValue)
{
    const Signal<bool> dark = m->dark;
    return Row({.layout = {.width = Expand},
                .on = {.onClick = [dark, darkValue] { dark = darkValue; }},
                .className = [dark, darkValue] { return std::string(dark() == darkValue ? "segment on" : "segment"); }},
        IconView(m, icon, 18.f, [dark, darkValue](const Palette& p) { return dark.Peek() == darkValue ? p.onPrimaryContainer : p.muted; }),
        Text(label, {.className = "segment-text"}));
}

Elem Swatch(AppPtr m, int i)
{
    const Signal<int> accent = m->accent;
    return Box({.on = {.onClick = [accent, i] { accent = i; }},
                .className = [accent, i] { return std::string(accent() == i ? "swatch on" : "swatch"); }},
        Box({.className = "swatch-dot a" + std::to_string(i)},
            Show([accent, i] { return accent() == i; }, [m] {
                return IconView(m, Ico::Check, 20.f, [](const Palette&) { return Color::White; });
            })));
}

} // namespace

Elem ProfileScreen(AppPtr m)
{
    const Vitals* raw = m.get();
    const Signal<float> weight = m->weight;
    return Scroll({.layout = {.width = Expand, .height = Expand}, .className = "scroll"},
        Column({.className = m->tabDir > 0 ? "page enter-right" : "page enter-left"},
            Card(m, "", {}, {
                Row({.className = "profile-head"},
                    Box({.className = "avatar large"}, Text("SR", {.className = "avatar-text large"})),
                    Column({.className = "profile-text"},
                        Text("Sam Rivera", {.className = "profile-name"}),
                        Text("Member since March 2023", {.className = "caption"}))),
                Row({.className = "mini-stats"},
                    Column({.className = "mini-stat"},
                        Text(Str([raw, weight] { return WeightText(*raw, weight()); }), {.className = "mini-value"}),
                        Text("Weight", {.className = "mini-label"})),
                    Column({.className = "mini-stat"},
                        Text(Str([raw] { return raw->imperial() ? std::string("5 ft 10 in") : std::string("178 cm"); }), {.className = "mini-value"}),
                        Text("Height", {.className = "mini-label"})),
                    Column({.className = "mini-stat"},
                        Text("34", {.className = "mini-value"}),
                        Text("Age", {.className = "mini-label"})))}),

            Text("Daily goals", {.className = "section-title pad"}),
            Card(m, "", {}, {
                GoalSlider(m, "Steps", m->stepGoal, 2000.0, 20000.0, 500.0, [](double v) { return Thousands(v); }),
                GoalSlider(m, "Move", m->moveGoal, 200.0, 1200.0, 10.0, [](double v) { return std::format("{:.0f} kcal", v); }),
                Row({.className = "goal-head"},
                    Column({.className = "goal-text"},
                        Text("Water", {.className = "goal-label"}),
                        Text("Glasses of 250 ml", {.className = "caption"})),
                    Spacer(),
                    NumberField({.value = m->waterGoal, .min = 4.0, .max = 12.0, .step = 0.05, .snap = 1.0, .decimals = 0,
                                 .label = "Glasses", .className = "number"}))}),

            Text("Appearance", {.className = "section-title pad"}),
            Card(m, "", {}, {
                Row({.className = "segments"},
                    Segment(m, "Light", Ico::Sun, false),
                    Segment(m, "Dark", Ico::Moon, true)),
                Row({.className = "swatches"}, Swatch(m, 0), Swatch(m, 1), Swatch(m, 2), Swatch(m, 3)),
                Row({.className = "setting"},
                    Column({.className = "goal-text"},
                        Text("Imperial units", {.className = "goal-label"}),
                        Text("Miles, pounds and feet", {.className = "caption"})),
                    Spacer(),
                    Switch({.on = m->imperial, .className = "switch"}))}),
            Box({.className = "page-end"})));
}

} // namespace vitals
