#include "Pulse.h"

#include <SampleHost.h>

#include <algorithm>
#include <cstdlib>
#include <string_view>

namespace pulse {

Elem Panel(Str title, Str caption, std::string extraClass, Children children)
{
    return Column({.className = "panel " + extraClass},
        Row({.className = "panel-head"},
            Text(std::move(title), {.className = "panel-title"}),
            Spacer(),
            Text(std::move(caption), {.className = "panel-caption"})),
        Column({.className = "panel-body"}, std::move(children)));
}

namespace {

Elem Overview(FeedPtr f)
{
    return Row({.className = "page"},
        Column({.className = "col-main"},
            MetricGrid(f),
            Panel("Traffic stream", "3 series x 600 samples, hover for values", "grow", {StreamChart(f, -1.f)}),
            Row({.className = "bottom-row"},
                Panel("Activity", "events per period", "grow", {Heatmap(f, -1.f)}),
                Panel("Leaderboard", "throughput by region", "race-panel", {BarRace(f)}))),
        Column({.className = "col-side"},
            Panel("Vitals", "springs toward each sample", "vitals", {GaugePanel(f)}),
            Panel("Event stream", Format("{} rows", f->eventCount), "grow", {EventLog(f)})));
}

Elem Detail(FeedPtr f)
{
    return Row({.className = "page"},
        Column({.className = "col-main"},
            Panel("Traffic stream", "full resolution", "grow", {StreamChart(f, -1.f)}),
            Panel("Activity", "30 periods x 7 days", "heat-panel", {Heatmap(f, -1.f)})),
        Column({.className = "col-wide"},
            Panel("Metrics", "live values, 12 feeds", "", {MetricTable(f)}),
            Panel("Event stream", Format("{} rows", f->eventCount), "grow", {EventLog(f)})));
}

class PulseApp {
public:
    PulseApp(bool light, int tab, double speed) : m_light(light), m_tab(tab), m_speed(speed) {}

    Elem Render()
    {
        FeedPtr f = std::make_shared<Feed>();
        f->dark = !m_light;
        f->tab = m_tab;
        f->speed = m_speed;
        OnFrame([f](float dt) { f->Step(dt); });

        const Signal<bool> dark = f->dark;
        const Signal<int> tab = f->tab;
        return Stack({.layout = {.width = Expand, .height = Expand},
                      .anchor = true,
                      .className = [dark] { return std::string(dark() ? "app dark" : "app light"); }},
            Column({.layout = {.width = Expand, .height = Expand}},
                Header(f),
                // Show remounts the page on every switch, so its entry keyframe replays.
                Show([tab] { return tab() == 0; }, [f] { return Overview(f); }),
                Show([tab] { return tab() == 1; }, [f] { return Detail(f); })),
            ParticleOverlay(f));
    }

private:
    bool m_light = false;
    int  m_tab   = 0;
    double m_speed = 1.0;
};

} // namespace

} // namespace pulse

int lumora::platform::Main(const lumora::platform::LaunchArgs& args)
{
    const int argc = args.argc;
    char** const argv = args.argv;

    // `--light`, `--detail` and `--speed N` pick the starting theme, tab and data rate (scripted runs).
    bool   light = false;
    int    tab   = 0;
    double speed = 1.0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        light = light || a == "--light";
        tab   = a == "--detail" ? 1 : tab;
        if (a == "--speed" && i + 1 < argc)
            speed = std::clamp(std::atof(argv[++i]), 1.0, 10.0);
    }
    return lumora::samples::RunSample({.title  = "Pulse",
                                       .width  = 1600,
                                       .height = 960,
                                       .root   = [light, tab, speed] { return lumora::Elem(pulse::PulseApp{light, tab, speed}); },
                                       .css    = pulse::kStylesheet},
                                      argc, argv);
}
