#pragma once

#include <lumora/Elem.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Foundation/Position.h>
#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/Layouts.h>
#include <lumora/Widgets/Overlay.h>
#include <lumora/Widgets/Props.h>

#include <cstdint>
#include <functional>
#include <utility>

namespace lumora
{
    enum class Edge : std::uint8_t { Left, Right, Top, Bottom };

    /// Edge-anchored panel with a dimming scrim; gate with Show. Scrim click or Escape fires onDismiss.
    struct DrawerCfg
    {
        Edge        edge = Edge::Right;
        float       size = 320.f;          ///< width for Left/Right, height for Top/Bottom
        Color       scrim = Color::Rgba(0, 0, 0, 110);
        bool        showScrim = true;
        bool        escapeDismiss = true;
        bool        scrimDismiss = true;
        StyleProps  style{.fill = Color::Rgba(26, 28, 34, 255)}; ///< panel surface
        LayoutProps layout{.padding = Spacing::All(16.f)};       ///< panel content padding
        std::function<void(DismissReason)> onDismiss{};
    };

    template<typename... Kids>
    [[nodiscard]] Elem Drawer(DrawerCfg cfg, Kids&&... body)
    {
        BoxCfg panel;
        panel.style             = cfg.style;
        panel.layout            = cfg.layout;
        panel.position.position = Position::Fixed; // pinned to the viewport edge
        switch (cfg.edge)
        {
            case Edge::Right:
                panel.layout.width = Fixed(cfg.size); panel.layout.height = Expand;
                panel.position.inset = {.top = 0.f, .right = 0.f};
                break;
            case Edge::Left:
                panel.layout.width = Fixed(cfg.size); panel.layout.height = Expand;
                panel.position.inset = {.left = 0.f, .top = 0.f};
                break;
            case Edge::Top:
                panel.layout.width = Expand; panel.layout.height = Fixed(cfg.size);
                panel.position.inset = {.left = 0.f, .top = 0.f};
                break;
            case Edge::Bottom:
                panel.layout.width = Expand; panel.layout.height = Fixed(cfg.size);
                panel.position.inset = {.left = 0.f, .right = 0.f, .bottom = 0.f};
                break;
        }
        panel.on.onClick = [] {}; // swallow clicks so they don't dismiss via the scrim
        Elem panelElem = Box(std::move(panel), std::forward<Kids>(body)...);

        const auto onDismiss = cfg.onDismiss;
        const bool scrimDismiss = cfg.scrimDismiss;

        StackCfg scrim;
        scrim.layout     = {.width = Expand, .height = Expand};
        scrim.style.fill = cfg.showScrim ? cfg.scrim : Color::Transparent;
        scrim.on.onClick = [onDismiss, scrimDismiss] {
            if (scrimDismiss && onDismiss) onDismiss(DismissReason::LightDismiss);
        };
        Elem content = Stack(std::move(scrim), std::move(panelElem));

        detail::PortalConfig pc;
        pc.layer                   = Layer::Drawer;
        pc.options.escapeDismiss   = cfg.escapeDismiss;
        pc.options.capturesPointer = true;
        pc.options.onDismiss       = std::move(cfg.onDismiss);
        pc.build                   = [content = std::move(content)]() -> Elem { return content; };
        return Elem(detail::BuildPortal(std::move(pc)));
    }
}
