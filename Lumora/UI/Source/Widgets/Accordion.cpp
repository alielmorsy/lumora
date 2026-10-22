#include <lumora/Widgets/Accordion.h>

#include <lumora/Reactive/Computed.h>
#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/ControlFlow.h>
#include <lumora/Widgets/Layouts.h>
#include <lumora/Widgets/Misc.h>
#include <lumora/Widgets/Svg.h>
#include <lumora/Widgets/Text.h>

#include <string>
#include <utility>

namespace lumora {

bool Accordion::IsOpen() const {
    return m_cfg.open ? m_cfg.open->Get() : m_ownOpen();
}

void Accordion::Toggle() {
    if (m_cfg.disabled) return;

    // Peek: runs in an event handler, where a tracked read would subscribe the wrong owner.
    const bool next = m_cfg.open ? !m_cfg.open->Get() : !m_ownOpen.Peek();

    if (m_cfg.open) m_cfg.open->Set(next);
    else m_ownOpen = next;

    if (m_cfg.onToggle) m_cfg.onToggle(next);
}

Elem Accordion::Render() {
    // Render() runs once at mount: read state through reactive props, not inline.
    auto isOpen = [this] { return IsOpen(); };

    RowCfg header;
    header.layout    = {.width = Expand, .cross = Align::Center};
    header.className = Reactive<std::string>(
        [this, isOpen]
        {
            std::string cls = "accordion-header";
            if (isOpen()) cls += " open";
            if (m_cfg.disabled) cls += " disabled";
            return cls;
        });
    if (!m_cfg.disabled)
    {
        header.on.onClick     = [this] { Toggle(); };
        header.on.focusPolicy = FocusPolicy::Tab;
        // A Tab stop must be keyboard-activatable: Enter and Space toggle.
        header.on.onKeyDown   = [this](KeyInfo key)
        {
            if (key.key != Key::Enter && key.key != Key::Space)
            {
                return false;
            }
            Toggle();
            return true;
        };
    }

    // Icon paths given: Svg with reactive `src` (SvgCfg has no className). Otherwise a Box
    // painted by the stylesheet, with `.accordion-caret.open` as the state hook.
    const bool hasIcons = !m_cfg.caretOpenIcon.IsStatic() || !m_cfg.caretOpenIcon.Get().empty();

    Elem caret = hasIcons
        ? Svg({.src = Str([this, isOpen]
                          { return isOpen() ? m_cfg.caretOpenIcon.Get()
                                            : m_cfg.caretClosedIcon.Get(); }),
               .layout = {.width = Fixed(m_cfg.caretSize), .height = Fixed(m_cfg.caretSize)}})
        : Box({.className = Reactive<std::string>(
                   [isOpen] { return std::string(isOpen() ? "accordion-caret open" : "accordion-caret"); })});

    Elem title = Text(m_cfg.title, {.className = std::string("accordion-title")});

    Elem headerRow = m_cfg.adornment.RawNode() != nullptr
        ? Row(std::move(header), std::move(caret), std::move(title), Spacer(), std::move(m_cfg.adornment))
        : Row(std::move(header), std::move(caret), std::move(title), Spacer());

    ColumnCfg root;
    root.layout    = m_cfg.layout;
    root.style     = m_cfg.style;
    root.className = m_cfg.className;

    return Column(std::move(root), std::move(headerRow),
                  Show(Reactive<bool>(isOpen),
                       [body = std::move(m_body)]() -> Elem
                       {
                           // Built inside Show's scope: a closed section costs nothing and reopening resets row state.
                           return body ? body() : Elem{};
                       }));
}

Elem Foldout(AccordionCfg cfg, Children children) {
    return Accordion(std::move(cfg),
                     [items = std::move(children)]() -> Elem
                     { return Column({.layout = {.width = Expand}, .className = std::string("accordion-body")},
                                     items); });
}

} // namespace lumora
