#pragma once

#include <lumora/Dock/Dock.h>
#include <lumora/Reactive/Computed.h>
#include <lumora/Reactive/Format.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Widgets.h>

#include <string>

namespace gallery {

using namespace lumora;

// Theme state shared by the shell and the Theming section.
struct Theme {
    Signal<bool> dark{true};
    Signal<int>  accent{0};
};

inline constexpr const char* kAccents[] = {"blue", "violet", "green", "amber"};

// Titled card wrapping a demo.
Elem Card(Str title, Str caption, Children children);
// Section header: title + one-line description.
Elem SectionHeader(Str title, Str description);
Elem Label(Str text, std::string className = "label");

Elem ButtonsSection(Theme theme = {});
Elem InputsSection(Theme theme = {});
Elem ListsSection(Theme theme = {});
Elem LayoutSection(Theme theme = {});
Elem ThemingSection(Theme theme = {});

extern const char* const kStylesheet;

}
