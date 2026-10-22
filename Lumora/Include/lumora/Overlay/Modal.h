#pragma once

#include <lumora/Elem.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/Layouts.h>
#include <lumora/Widgets/Overlay.h>
#include <lumora/Widgets/Props.h>

#include <functional>
#include <utility>

namespace lumora
{
    /// Modal dialog: a scrim capturing input plus a centred dialog; gate with Show/Presence. Scrim click or Escape fires
    /// onDismiss; clicks on the dialog do not.
    struct ModalCfg
    {
        Color       scrim = Color::Rgba(0, 0, 0, 120);
        bool        escapeDismiss = true;
        bool        scrimDismiss  = true;
        LayoutProps layout{.padding = Spacing::All(20.f)}; ///< dialog sizing / padding
        StyleProps  style{.fill = Color::Rgba(28, 30, 36, 255), .radius = 14.f}; ///< dialog surface
        std::function<void(DismissReason)> onDismiss{};
    };

    template<typename... Kids>
    [[nodiscard]] Elem Modal(ModalCfg cfg, Kids&&... body)
    {
        BoxCfg dialogBox;
        dialogBox.layout    = cfg.layout;
        dialogBox.style     = cfg.style;
        dialogBox.on.onClick = [] {}; // swallow clicks so they don't reach the scrim and dismiss
        Elem dialog = Box(std::move(dialogBox), std::forward<Kids>(body)...);

        const auto onDismiss = cfg.onDismiss;
        const bool scrimDismiss = cfg.scrimDismiss;

        StackCfg scrim;
        scrim.layout     = {.width = Expand, .height = Expand, .main = Center, .cross = Center};
        scrim.style.fill = cfg.scrim;
        scrim.on.onClick = [onDismiss, scrimDismiss] {
            if (scrimDismiss && onDismiss) onDismiss(DismissReason::LightDismiss);
        };
        Elem content = Stack(std::move(scrim), std::move(dialog));

        detail::PortalConfig pc;
        pc.layer                  = Layer::Modal;
        pc.options.escapeDismiss  = cfg.escapeDismiss;
        pc.options.capturesPointer = true;
        pc.options.onDismiss      = std::move(cfg.onDismiss);
        pc.build                  = [content = std::move(content)]() -> Elem { return content; };
        return Elem(detail::BuildPortal(std::move(pc)));
    }
}
