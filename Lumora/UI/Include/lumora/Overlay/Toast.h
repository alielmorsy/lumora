#pragma once

#include <lumora/Animation/Keyframes.h>
#include <lumora/Elem.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Foundation/Position.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/ControlFlow.h>
#include <lumora/Widgets/Layouts.h>
#include <lumora/Widgets/Overlay.h>
#include <lumora/Widgets/Text.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace lumora
{
    struct ToastItem
    {
        std::uint64_t id = 0;
        std::string   text;
        Color         accent = Color::Rgba(86, 156, 255, 255);
        float         ttl = 3.f; ///< seconds before auto-dismiss (0 = sticky)
    };

    enum class Corner : std::uint8_t { TopLeft, TopRight, BottomLeft, BottomRight };

    /// A viewport-corner notification stack, driven by an app-owned signal. Mount once near the root;
    /// push by appending to `items` from anywhere (each entry auto-dismisses after its ttl).
    struct ToasterCfg
    {
        Signal<std::vector<ToastItem>> items;
        Corner corner = Corner::BottomRight;
        float  gap = 8.f;
        float  margin = 16.f;
        float  width = 280.f;
        Color  cardFill = Color::Rgba(34, 36, 43, 245);
        Color  textColor = Color::Rgba(232, 234, 240, 255);
    };

    [[nodiscard]] inline Elem Toaster(ToasterCfg cfg)
    {
        Inset inset;
        switch (cfg.corner)
        {
            case Corner::TopLeft: inset = {.left = cfg.margin, .top = cfg.margin}; break;
            case Corner::TopRight: inset = {.top = cfg.margin, .right = cfg.margin}; break;
            case Corner::BottomLeft: inset = {.left = cfg.margin, .bottom = cfg.margin}; break;
            case Corner::BottomRight: inset = {.right = cfg.margin, .bottom = cfg.margin}; break;
        }

        Signal<std::vector<ToastItem>> items = cfg.items;
        return Portal(Layer::Toast, [items, cfg, inset]() -> Elem {
            ColumnCfg col;
            col.layout            = {.gap = cfg.gap};
            col.position.position = Position::Fixed;
            col.position.inset    = inset;
            return Column(std::move(col),
                Each(items, &ToastItem::id, [items, cfg](const ToastItem& t, int) -> Elem {
                    if (t.ttl > 0.f)
                    {
                        Delay(t.ttl, [items, id = t.id] {
                            items.Update([id](std::vector<ToastItem>& v) {
                                std::erase_if(v, [id](const ToastItem& x) { return x.id == id; });
                            });
                        });
                    }
                    return Box(BoxCfg{
                                   .layout = {.width = Fixed(cfg.width), .padding = Spacing::All(12.f)},
                                   .style  = {.fill = cfg.cardFill, .radius = 9.f,
                                              .border = {.width = 1.f, .color = t.accent}},
                               },
                               Text(t.text, {.size = 13.f, .color = cfg.textColor}));
                }));
        });
    }
}
