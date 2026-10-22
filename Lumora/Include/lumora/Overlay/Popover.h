#pragma once

#include <lumora/Elem.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Overlay/Positioner.h>
#include <lumora/Widgets/Anchor.h>
#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/Overlay.h>
#include <lumora/Widgets/Props.h>

#include <functional>
#include <utility>
#include <vector>

namespace lumora
{
    /// Anchored, light-dismiss base for floating surfaces; gate with Show/Presence. The adaptive positioner places the panel
    /// against `.anchor`, flipping/shifting to stay on-screen.
    struct PopoverCfg
    {
        Anchor                  anchor;                  ///< the trigger (bind via .ref on a widget)
        Side                    side  = Side::Bottom;
        Align3                  align = Align3::Start;
        float                   offset = 6.f;            ///< gap between trigger and panel
        std::vector<Middleware> middleware;              ///< empty → {Flip, Shift{8}, Size}
        Layer                   layer = Layer::Popover;
        bool                    lightDismiss  = true;
        bool                    escapeDismiss = true;
        bool                    pointerTransparent = false; ///< panel passes events through (tooltips)
        std::function<void(DismissReason)> onDismiss{};
        LayoutProps             layout{.padding = Spacing::All(6.f)}; ///< panel sizing / padding
        StyleProps              style{};                 ///< panel surface (fill / radius / border)
        Spacing                 viewportPad = Spacing::All(8.f);
    };

    /// A floating panel anchored to `cfg.anchor`, holding `content`.
    template<typename... Kids>
    [[nodiscard]] Elem Popover(PopoverCfg cfg, Kids&&... content)
    {
        BoxCfg box;
        box.layout            = cfg.layout;
        box.style             = cfg.style;
        box.position.position = Position::Absolute; // the positioner writes its inset each frame
        box.on.hitThrough     = cfg.pointerTransparent;
        Elem panel = Box(std::move(box), std::forward<Kids>(content)...);

        std::vector<Middleware> mw = std::move(cfg.middleware);
        if (mw.empty())
        {
            mw.push_back(Flip());
            mw.push_back(Shift(ShiftCfg{.padding = 8.f}));
            mw.push_back(Size(SizeCfg{}));
        }

        detail::PortalConfig pc;
        pc.layer                       = cfg.layer;
        pc.options.lightDismiss        = cfg.lightDismiss;
        pc.options.escapeDismiss       = cfg.escapeDismiss;
        pc.options.pointerTransparent  = cfg.pointerTransparent;
        pc.options.onDismiss           = std::move(cfg.onDismiss);
        pc.build                 = [panel = std::move(panel)]() -> Elem { return panel; };
        pc.positioner            = PortalPositioner{
            .anchor      = cfg.anchor.State(),
            .base        = {cfg.side, cfg.align},
            .offset      = cfg.offset,
            .middleware  = std::move(mw),
            .viewportPad = cfg.viewportPad,
        };
        return Elem(detail::BuildPortal(std::move(pc)));
    }
}
