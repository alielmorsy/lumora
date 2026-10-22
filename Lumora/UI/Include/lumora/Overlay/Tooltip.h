#pragma once

#include <lumora/Elem.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Overlay/Popover.h>
#include <lumora/Overlay/Positioner.h>
#include <lumora/Widgets/Anchor.h>
#include <lumora/Widgets/Text.h>

#include <string>
#include <utility>

namespace lumora
{
    /// A hover/focus hint: pointer-transparent, non-dismissible, in the Tooltip layer above everything. Gate with Show on a
    /// hover signal (Delay for open/close delays).
    struct TooltipCfg
    {
        Anchor      anchor;
        std::string text;
        Side        side  = Side::Top;
        float       fontSize = 12.f;
        Color       fill  = Color::Rgba(32, 33, 38, 235);
        Color       textColor = Color::Rgba(235, 236, 240, 255);
        float       radius = 6.f;
        float       offset = 6.f;
    };

    [[nodiscard]] inline Elem Tooltip(TooltipCfg cfg)
    {
        PopoverCfg pc;
        pc.anchor             = cfg.anchor;
        pc.side               = cfg.side;
        pc.align              = Align3::Center;
        pc.offset             = cfg.offset;
        pc.layer              = Layer::Tooltip;
        pc.lightDismiss       = false;
        pc.escapeDismiss      = false;
        pc.pointerTransparent = true;
        pc.layout             = LayoutProps{.padding = Spacing::Symmetric(5.f, 9.f)};
        pc.style              = StyleProps{.fill = cfg.fill, .radius = cfg.radius};
        return Popover(std::move(pc), Text(cfg.text, {.size = cfg.fontSize, .color = cfg.textColor}));
    }
}
