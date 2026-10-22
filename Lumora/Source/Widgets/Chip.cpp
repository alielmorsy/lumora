#include <lumora/Widgets/Chip.h>

#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/Layouts.h>
#include <lumora/Widgets/Text.h>

#include <utility>
#include <vector>

namespace lumora
{
    namespace
    {
        constexpr float kDot        = 7.f;
        constexpr float kPadH       = 8.f;
        constexpr float kGap        = 5.f;
        constexpr float kRemoveSize = 12.f;

        /// Filled is the colour itself, hollow is a ring of it (tellable apart at 7 px).
        [[nodiscard]] Elem Dot(Color color, bool filled)
        {
            return Box(BoxCfg{
                .layout = {.width = Fixed(kDot), .height = Fixed(kDot)},
                .style  = {.fill   = filled ? color : Color::Transparent,
                           .radius = kDot * 0.5f,
                           .border = {filled ? 0.f : 1.f, color}},
            });
        }

        [[nodiscard]] Elem Remove(std::function<void()> onRemove, Color color, Elem icon)
        {
            return Box(BoxCfg{
                           .layout = {.width  = Fixed(kRemoveSize),
                                      .height = Fixed(kRemoveSize),
                                      .main   = Align::Center,
                                      .cross  = Align::Center},
                           .style  = {.radius = kRemoveSize * 0.5f},
                           .on     = {.onClick = std::move(onRemove)},
                       },
                       // The text fallback exists only because the library has no icon set.
                       icon ? std::move(icon)
                            : Text("\xC3\x97", {.size = 11.f, .color = color})); // U+00D7
        }
    } // namespace

    Elem Chip(ChipCfg cfg)
    {
        std::vector<Elem> kids;
        if (cfg.dotColor.a != 0)
        {
            kids.push_back(Dot(cfg.dotColor, cfg.dotFilled));
        }
        kids.push_back(Text(std::move(cfg.label), {.size = cfg.fontSize, .color = cfg.textColor}));
        if (cfg.onRemove)
        {
            kids.push_back(Remove(std::move(cfg.onRemove), cfg.textColor, std::move(cfg.removeIcon)));
        }

        LayoutProps layout = cfg.layout;
        layout.height      = Fixed(cfg.height);
        layout.padding     = Spacing::Symmetric(0.f, kPadH);
        layout.gap         = kGap;
        layout.cross       = Align::Center;

        return Row(RowCfg{
                       .layout    = std::move(layout),
                       .style     = cfg.style,
                       .on        = {.onClick = std::move(cfg.onClick), .disabled = std::move(cfg.disabled)},
                       .className = std::move(cfg.className),
                   },
                   std::move(kids));
    }
} // namespace lumora
