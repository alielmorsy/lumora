#pragma once

#include <lumora/Api.h>
#include <lumora/Elem.h>
#include <lumora/Reactive/Bind.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Widgets/Children.h>
#include <lumora/Widgets/Props.h>

#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace lumora {

struct AccordionCfg {
    /// The header text. Reactive, so a section can count what it holds without being rebuilt.
    Str title{};

    /// Trailing header content - a badge, a count, a reset affordance. Built once, like any children.
    Elem adornment{};

    /// The two caret icons by path; leave empty for a plain CSS marker (`.accordion-caret` / `.accordion-caret.open`).
    /// Paths, not an Elem or a rotation: SvgCfg has no className, but `SvgCfg::src` is a reactive Str, so swapping the source
    /// is the only way a caret follows the open state.
    Str caretOpenIcon{};
    Str caretClosedIcon{};
    float caretSize = 10.f;

    /// Initial state. Only read when `open` is unbound; with `open` bound, the caller owns the state.
    bool defaultOpen = true;

    /// Externally owned open state, for when it must outlive the widget (remembered per type, single-open groups). Unset means
    /// the widget owns its own Signal<bool>. `std::optional` because Bind<T> has no default constructor.
    std::optional<Bind<bool>> open{};

    /// Fired after the state flips, with the new value. For persisting expansion, not for driving it.
    std::function<void(bool)> onToggle{};

    bool disabled = false;

    LayoutProps           layout{};
    StyleProps            style{};
    Str                   className{};  ///< the root; header and body carry `-header` / `-body`
};

/// A collapsible titled section (Column + header Row + `Show`, not a new node kind). The body is a builder, not an Elem: a
/// closed section must not pay to build its contents, and Show disposes its scope on close.
class LUMORA_API Accordion {
public:
    explicit Accordion(AccordionCfg cfg, std::function<Elem()> body)
        : m_cfg(std::move(cfg)), m_body(std::move(body)) {}

    [[nodiscard]] Elem Render();

private:
    [[nodiscard]] bool IsOpen() const;
    void               Toggle();

    AccordionCfg          m_cfg;
    std::function<Elem()> m_body;

    /// Used only when `m_cfg.open` is unbound; seeded at construction so the first paint is right.
    Signal<bool> m_ownOpen{m_cfg.defaultOpen};
};

/// A titled section wrapping already-built children; use the `Accordion` class directly when the body is expensive and
/// should not be built while collapsed.
[[nodiscard]] LUMORA_API Elem Foldout(AccordionCfg cfg, Children children);

}
