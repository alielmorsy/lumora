#include "Vitals.h"

#include <SampleHost.h>

#include <algorithm>
#include <cstdlib>
#include <string_view>

namespace vitals {

namespace {

class VitalsApp {
public:
    VitalsApp(int tab, bool dark, int accent, bool sheet) : m_tab(tab), m_dark(dark), m_accent(accent), m_sheet(sheet) {}

    Elem Render()
    {
        AppPtr m = std::make_shared<Vitals>();
        m->tab = m_tab;
        m->dark = m_dark;
        m->accent = m_accent;
        m->sheetOpen = m_sheet;
        m->pal = ThemePalette(m_dark, m_accent);
        OnFrame([m](float dt) { m->Step(dt); });

        const Signal<int> tab = m->tab;
        const Signal<bool> dark = m->dark;
        const Signal<int> accent = m->accent;

        // Show remounts a page on every switch, so its entry keyframe replays.
        std::vector<Elem> pages;
        Elem (*const builders[])(AppPtr) = {TodayScreen, ActivityScreen, SleepScreen, ProfileScreen};
        for (int i = 0; i < 4; ++i)
            pages.push_back(Show([tab, i] { return tab() == i; }, [m, b = builders[i]] { return b(m); }));

        return Stack({.layout = {.width = Expand, .height = Expand},
                      .anchor = true,
                      .className = [dark, accent] {
                          return std::string(dark() ? "app dark a" : "app light a") + std::to_string(accent());
                      }},
            Column({.layout = {.width = Expand, .height = Expand}},
                TopBar(m),
                Column({.className = "content"}, std::move(pages)),
                BottomNav(m)),
            Fab(m),
            Snackbar(m),
            BottomSheet(m));
    }

private:
    int  m_tab = 0;
    bool m_dark = false;
    int  m_accent = 0;
    bool m_sheet = false;
};

} // namespace

} // namespace vitals

int lumora::platform::Main(const lumora::platform::LaunchArgs& args)
{
    const int argc = args.argc;
    char** const argv = args.argv;

    // Startup flags for scripted screenshots: --tab N, --dark, --accent N, --sheet.
    int  tab = 0, accent = 0;
    bool dark = false, sheet = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--tab" && i + 1 < argc)
            tab = std::clamp(std::atoi(argv[++i]), 0, 3);
        else if (a == "--accent" && i + 1 < argc)
            accent = std::clamp(std::atoi(argv[++i]), 0, 3);
        dark = dark || a == "--dark";
        sheet = sheet || a == "--sheet";
    }
    return lumora::samples::RunSample({.title  = "Vitals",
                                       .width  = 430,
                                       .height = 900,
                                       .root   = [=] { return lumora::Elem(vitals::VitalsApp{tab, dark, accent, sheet}); },
                                       .css    = vitals::kStylesheet},
                                      argc, argv);
}
