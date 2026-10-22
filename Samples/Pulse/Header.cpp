#include "Pulse.h"

#include <format>

namespace pulse {

namespace {

// Label over a number that tweens toward each committed value.
Elem Counter(const char* label, Signal<float> source, std::function<std::string(float)> fmt)
{
    const Motion<float> shown = Tween<float>(source, {.duration = 0.45f, .easing = ease::CubicOut});
    return Column({.className = "counter"},
        Text(label, {.className = "counter-label"}),
        Text(Str([shown, fmt] { return fmt(shown()); }), {.className = "counter-value"}));
}

Elem TabButton(FeedPtr f, int index, const char* label)
{
    const Signal<int> tab = f->tab;
    return Button({.label = label,
                   .onClick = [tab, index] { tab = index; },
                   .variant = ButtonVariant::Ghost,
                   .className = [tab, index] { return std::string(tab() == index ? "tab active" : "tab"); }});
}

Elem StatusChips(FeedPtr f)
{
    const Signal<bool> paused = f->paused;
    const Signal<int>  alerts = f->alerts;
    const Signal<double> speed = f->speed;
    return Row({.className = "chips"},
        Row({.className = [paused] { return std::string(paused() ? "status idle" : "status live"); }},
            Box({.className = "status-dot"}),
            Text(Str([paused] { return std::string(paused() ? "PAUSED" : "LIVE"); }), {.className = "status-text"})),
        Row({.className = [alerts] { return std::string(alerts() > 2 ? "status alert" : "status calm"); }},
            Box({.className = "status-dot"}),
            Text(Format("{} alerts", alerts), {.className = "status-text"})),
        Row({.className = "status rate"},
            Text(Str([speed] { return std::format("{:.0f}x feed", speed()); }), {.className = "status-text"})));
}

Elem Controls(FeedPtr f)
{
    const Signal<bool> paused = f->paused;
    const Signal<bool> dark = f->dark;
    return Row({.className = "controls"},
        Button({.label = Str([paused] { return std::string(paused() ? "Resume" : "Pause"); }),
                .onClick = [paused] { paused.Toggle(); },
                .className = [paused] { return std::string(paused() ? "btn primary paused" : "btn primary"); }}),
        Column({.className = "speed"},
            Row({.className = "speed-head"},
                Text("Data rate", {.className = "mini-label"}),
                Spacer(),
                Text(Str([s = f->speed] { return std::format("{:.0f}x", s()); }), {.className = "mini-value"})),
            Slider({.value = f->speed, .min = 1.0, .max = 10.0, .step = 1.0,
                    .layout = {.width = Fixed(150.f)}, .className = "slider"})),
        Row({.className = "theme"},
            Text(Str([dark] { return std::string(dark() ? "Dark" : "Light"); }), {.className = "mini-label"}),
            Switch({.on = dark, .className = "switch"})));
}

} // namespace

Elem Header(FeedPtr f)
{
    const Signal<std::uint64_t> frame = f->frame;
    return Row({.className = "header"},
        Row({.className = "brand"},
            Box({.className = "logo"}, Box({.className = "logo-core"})),
            Column({.className = "brand-text"},
                Text("Pulse", {.className = "brand-title"}),
                Text("Realtime telemetry", {.className = "brand-sub"}))),
        Row({.className = "tabs"}, TabButton(f, 0, "Overview"), TabButton(f, 1, "Detail")),
        StatusChips(f),
        Spacer(),
        Counter("EVENTS", f->totalEvents, [](float v) { return Thousands(v); }),
        Counter("EVENTS / S", f->throughput, [](float v) { return std::format("{:.1f}", v); }),
        Counter("REVENUE", f->revenue, [](float v) { return "$" + Thousands(v); }),
        Controls(f),
        Column({.className = "clock"},
            Text(Str([f, frame] { frame(); return ClockString(f->wallTime); }), {.className = "clock-time"}),
            Text(Str([f, frame] { frame(); return std::format("frame {}", frame()); }), {.className = "clock-sub"})));
}

} // namespace pulse
