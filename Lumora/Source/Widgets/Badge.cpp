#include <lumora/Widgets/Badge.h>

#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/Text.h>

#include <utility>

namespace lumora
{
    namespace
    {
        constexpr float kPadH = 5.f;

        struct Palette
        {
            Color fill;
            Color text;
        };

        /// Low-alpha accent washes: a saturated block at this size reads as an error.
        [[nodiscard]] Palette PaletteFor(BadgeVariant v)
        {
            switch (v)
            {
                case BadgeVariant::Info:    return {Color::Rgba(58, 122, 210, 60), Color::Rgba(150, 192, 255, 255)};
                case BadgeVariant::Success: return {Color::Rgba(64, 160, 96, 60), Color::Rgba(140, 220, 165, 255)};
                case BadgeVariant::Warn:    return {Color::Rgba(200, 150, 50, 60), Color::Rgba(240, 200, 120, 255)};
                case BadgeVariant::Danger:  return {Color::Rgba(200, 70, 70, 60), Color::Rgba(250, 160, 160, 255)};
                case BadgeVariant::Neutral:
                default:                    return {Color::Rgba(70, 74, 84, 200), Color::Rgba(200, 204, 212, 255)};
            }
        }
    } // namespace

    Elem Badge(BadgeCfg cfg)
    {
        const Palette pal   = PaletteFor(cfg.variant);
        const Reactive<Color> fill = Reactive<Color>([f = cfg.fill, palFill = pal.fill] {
            const Color c = f.Get();
            return c.a != 0 ? c : palFill;
        });
        const Reactive<Color> color = Reactive<Color>([c = cfg.color, palText = pal.text] {
            const Color col = c.Get();
            return col.a != 0 ? col : palText;
        });

        LayoutProps layout = cfg.layout;
        layout.height      = Fixed(cfg.height);
        layout.padding     = Spacing::Symmetric(0.f, kPadH);
        layout.main        = Align::Center;
        layout.cross       = Align::Center;

        return Box(BoxCfg{
                       .layout    = std::move(layout),
                       .style     = {.fill = fill, .radius = cfg.radius},
                       .className = std::move(cfg.className),
                   },
                   Text(std::move(cfg.label), {.size = cfg.fontSize, .color = color}));
    }
} // namespace lumora
