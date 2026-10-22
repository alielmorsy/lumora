#include <lumora/FromChars.h>
#include <lumora/Graph/Fields.h>
#include <lumora/Graph/Graph.h>
#include <lumora/Input/Event.h>
#include <lumora/Paint/Paint2D.h>
#include <lumora/Ref.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <string_view>
#include <utility>

namespace lumora::nodegraph
{
    namespace
    {
        constexpr float kLabelShare = 0.4f;
        constexpr float kBoxInset = 0.12f;
        constexpr float kScrubThreshold = 2.f;

        struct Scrub : RefCounted
        {
            int   component = -1;
            float start = 0.f;
            float travel = 0.f;
            bool  begun = false;
        };

        struct NumberSpec
        {
            std::function<float(GraphNodeId, int)>       get;
            std::function<void(GraphNodeId, int, float)> set;
            std::function<void(GraphNodeId, EditPhase)>  onEdit;
            float                                        speed = 0.01f;
            float                                        min = 0.f;
            float                                        max = 0.f;
            int                                          decimals = 3;
            int                                          count = 1;
            bool                                         labelled = false;
        };

        /// The value area of a row: the right share when labelled, else the whole row, inset
        /// vertically by a fraction so the box looks the same at every zoom. Works in any space.
        [[nodiscard]] Rect ValueRect(const Rect& row, bool labelled) noexcept
        {
            const float labelWidth = labelled ? row.width * kLabelShare : 0.f;
            const float inset = row.height * kBoxInset;
            return Rect{row.x + labelWidth, row.y + inset, row.width - labelWidth,
                        row.height - inset * 2.f};
        }

        [[nodiscard]] Rect ComponentRect(const Rect& value, int index, int count) noexcept
        {
            const float gap = value.width * 0.02f;
            const float width = (value.width - gap * static_cast<float>(count - 1)) / static_cast<float>(count);
            return Rect{value.x + (width + gap) * static_cast<float>(index), value.y, width, value.height};
        }

        /// The component under `point`, or -1 outside the value area.
        [[nodiscard]] int ComponentAt(const Rect& value, int count, Vec2 point) noexcept
        {
            if (!value.Contains(point) || value.width <= 0.f)
            {
                return -1;
            }
            const int index = static_cast<int>((point.x - value.x) / value.width * static_cast<float>(count));
            return std::clamp(index, 0, count - 1);
        }

        [[nodiscard]] const Theme& ThemeOf(const CellPaintCtx& ctx)
        {
            static const Theme fallback{};
            return ctx.theme != nullptr ? *ctx.theme : fallback;
        }

        [[nodiscard]] float TextTop(const Rect& rect, float size) noexcept
        {
            return rect.y + (rect.height - size * 1.25f) * 0.5f;
        }

        void PaintLabel(CellPaintCtx& ctx, std::string_view label)
        {
            if (label.empty() || ctx.compact)
            {
                return;
            }
            const Theme& theme = ThemeOf(ctx);
            const float  size = theme.CellTextSize * ctx.zoom;
            const Rect   area{ctx.rect.x, ctx.rect.y, ctx.rect.width * kLabelShare, ctx.rect.height};
            ctx.painter.PushClip(area);
            ctx.painter.DrawString(label, Vec2{area.x, TextTop(area, size)}, theme.TextDim, size);
            ctx.painter.PopClip();
        }

        void PaintBox(CellPaintCtx& ctx, const Rect& box)
        {
            const Theme& theme = ThemeOf(ctx);
            ctx.painter.FillRect(box, box.Contains(ctx.painter.Mouse()) ? theme.FieldHover : theme.FieldBg,
                                 3.f * ctx.zoom);
        }

        void PaintBoxText(CellPaintCtx& ctx, const Rect& box, std::string_view text)
        {
            const Theme& theme = ThemeOf(ctx);
            const float  size = theme.CellTextSize * ctx.zoom;
            ctx.painter.PushClip(box);
            ctx.painter.DrawString(text, Vec2{box.x + 4.f * ctx.zoom, TextTop(box, size)}, theme.Text, size);
            ctx.painter.PopClip();
        }

        [[nodiscard]] std::string Format(float value, int decimals)
        {
            return std::format("{:.{}f}", value, std::clamp(decimals, 0, 9));
        }

        [[nodiscard]] bool Parse(std::string_view text, float& out)
        {
            while (!text.empty() && text.front() == ' ')
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && text.back() == ' ')
            {
                text.remove_suffix(1);
            }
            if (!text.empty() && text.front() == '+')
            {
                text.remove_prefix(1);
            }
            const auto [end, error] = FromChars(text.data(), text.data() + text.size(), out);
            return error == std::errc{} && end == text.data() + text.size() && !text.empty();
        }

        void Notify(const std::function<void(GraphNodeId, EditPhase)>& onEdit, GraphNodeId node, EditPhase phase)
        {
            if (onEdit)
            {
                onEdit(node, phase);
            }
        }

        void Repaint(const CellInputCtx& ctx)
        {
            if (ctx.controller != nullptr)
            {
                ctx.controller->Repaint();
            }
        }

        void PaintNumbers(const NumberSpec& spec, CellPaintCtx& ctx)
        {
            const Rect value = ValueRect(ctx.rect, spec.labelled && !ctx.compact);
            for (int i = 0; i < spec.count; ++i)
            {
                const Rect box = ComponentRect(value, i, spec.count);
                PaintBox(ctx, box);
                if (spec.get)
                {
                    PaintBoxText(ctx, box, Format(spec.get(ctx.node, i), spec.decimals));
                }
            }
        }

        bool NumberInput(const NumberSpec& spec, Scrub& scrub, CellInputCtx& ctx)
        {
            const Rect value = ValueRect(ctx.rect, spec.labelled && !ctx.compact);
            switch (ctx.event)
            {
            case CellEvent::Press:
            {
                const int component = ComponentAt(value, spec.count, ctx.canvasPos);
                if (component < 0)
                {
                    return false;
                }
                scrub.component = component;
                scrub.start = spec.get ? spec.get(ctx.node, component) : 0.f;
                scrub.travel = 0.f;
                scrub.begun = false;
                return true;
            }
            case CellEvent::Drag:
            {
                if (scrub.component < 0 || !spec.set)
                {
                    return true;
                }
                const float fine = Has(ctx.pointer.modifiers, KeyMods::Shift) ? 0.1f : 1.f;
                scrub.travel += ctx.pointer.delta.x * fine;
                if (!scrub.begun)
                {
                    if (std::fabs(scrub.travel) < kScrubThreshold)
                    {
                        return true;
                    }
                    scrub.begun = true;
                    Notify(spec.onEdit, ctx.node, EditPhase::Begin);
                }
                spec.set(ctx.node, scrub.component,
                         std::clamp(scrub.start + scrub.travel * spec.speed, spec.min, spec.max));
                Repaint(ctx);
                return true;
            }
            case CellEvent::Release:
                if (scrub.begun)
                {
                    Notify(spec.onEdit, ctx.node, EditPhase::Commit);
                }
                scrub.component = -1;
                scrub.begun = false;
                return true;
            case CellEvent::DoubleClick:
            {
                const int component = ComponentAt(value, spec.count, ctx.canvasPos);
                if (component < 0 || ctx.controller == nullptr || !spec.set)
                {
                    return false;
                }
                const GraphNodeId node = ctx.node;
                Controller*       controller = ctx.controller;
                controller->EditText(
                    ComponentRect(value, component, spec.count),
                    Format(spec.get ? spec.get(node, component) : 0.f, spec.decimals),
                    [spec, node, component, controller](std::string_view text)
                    {
                        float parsed = 0.f;
                        if (!Parse(text, parsed))
                        {
                            return;
                        }
                        Notify(spec.onEdit, node, EditPhase::Begin);
                        spec.set(node, component, std::clamp(parsed, spec.min, spec.max));
                        Notify(spec.onEdit, node, EditPhase::Commit);
                        controller->Repaint();
                    });
                return true;
            }
            }
            return false;
        }

        [[nodiscard]] Cell NumberCell(std::string label, std::uint16_t pin, NumberSpec spec)
        {
            spec.labelled = !label.empty();
            const Ref<Scrub> scrub = MakeRef<Scrub>();

            CustomCell cell;
            cell.height = Theme{}.CellHeight;
            cell.pin = pin;
            cell.label = label;
            cell.paint = [spec, label](CellPaintCtx& ctx)
            {
                PaintLabel(ctx, label);
                PaintNumbers(spec, ctx);
            };
            cell.input = [spec, scrub](CellInputCtx& ctx) { return NumberInput(spec, *scrub, ctx); };
            return Cell{std::move(cell)};
        }

        /// Press captures inside the value area; the click lands on release, still inside it.
        [[nodiscard]] bool Clicked(const Rect& value, CellInputCtx& ctx)
        {
            return ctx.event == CellEvent::Release && value.Contains(ctx.canvasPos);
        }
    } // namespace

    Cell FloatField(FloatFieldCfg cfg)
    {
        NumberSpec spec;
        if (cfg.get)
        {
            spec.get = [get = std::move(cfg.get)](GraphNodeId node, int) { return get(node); };
        }
        if (cfg.set)
        {
            spec.set = [set = std::move(cfg.set)](GraphNodeId node, int, float value) { set(node, value); };
        }
        spec.onEdit = std::move(cfg.onEdit);
        spec.speed = cfg.speed;
        spec.min = cfg.min;
        spec.max = cfg.max;
        spec.decimals = cfg.decimals;
        spec.count = 1;
        return NumberCell(std::move(cfg.label), cfg.pin, std::move(spec));
    }

    Cell VecField(int components, VecFieldCfg cfg)
    {
        NumberSpec spec;
        spec.get = std::move(cfg.get);
        spec.set = std::move(cfg.set);
        spec.onEdit = std::move(cfg.onEdit);
        spec.speed = cfg.speed;
        spec.min = cfg.min;
        spec.max = cfg.max;
        spec.decimals = cfg.decimals;
        spec.count = std::clamp(components, 1, 4);
        return NumberCell(std::move(cfg.label), cfg.pin, std::move(spec));
    }

    Cell ColorSwatch(ColorSwatchCfg cfg)
    {
        const bool labelled = !cfg.label.empty();
        CustomCell cell;
        cell.height = Theme{}.CellHeight;
        cell.pin = cfg.pin;
        cell.label = cfg.label;
        cell.paint = [label = cfg.label, get = cfg.get, labelled](CellPaintCtx& ctx)
        {
            PaintLabel(ctx, label);
            const Rect box = ValueRect(ctx.rect, labelled && !ctx.compact);
            const Theme& theme = ThemeOf(ctx);
            ctx.painter.FillRect(box, get ? get(ctx.node) : Color::Transparent, 3.f * ctx.zoom);
            ctx.painter.StrokeRect(box, box.Contains(ctx.painter.Mouse()) ? theme.NodeSelected : theme.NodeBorder,
                                   1.f, 3.f * ctx.zoom);
        };
        cell.input = [onEdit = std::move(cfg.onEdit), labelled](CellInputCtx& ctx)
        {
            const Rect value = ValueRect(ctx.rect, labelled && !ctx.compact);
            if (ctx.event == CellEvent::Press)
            {
                return value.Contains(ctx.canvasPos);
            }
            if (Clicked(value, ctx) && onEdit)
            {
                onEdit(ctx.node, ctx.surfacePos);
            }
            return ctx.event == CellEvent::Release;
        };
        return Cell{std::move(cell)};
    }

    Cell EnumField(EnumFieldCfg cfg)
    {
        const bool labelled = !cfg.label.empty();
        CustomCell cell;
        cell.height = Theme{}.CellHeight;
        cell.pin = cfg.pin;
        cell.label = cfg.label;
        cell.paint = [label = cfg.label, options = cfg.options, get = cfg.get, labelled](CellPaintCtx& ctx)
        {
            PaintLabel(ctx, label);
            const Rect box = ValueRect(ctx.rect, labelled && !ctx.compact);
            PaintBox(ctx, box);
            const int current = get ? get(ctx.node) : -1;
            if (current >= 0 && current < static_cast<int>(options.size()))
            {
                PaintBoxText(ctx, Rect{box.x, box.y, box.width - box.height, box.height}, options[current]);
            }
            const float s = box.height * 0.18f;
            const Vec2  c{box.Right() - box.height * 0.5f, box.y + box.height * 0.5f};
            ctx.painter.FillTriangle(Vec2{c.x - s, c.y - s * 0.5f}, Vec2{c.x + s, c.y - s * 0.5f},
                                     Vec2{c.x, c.y + s * 0.7f}, ThemeOf(ctx).TextDim);
        };
        cell.input = [options = std::move(cfg.options), set = std::move(cfg.set),
                      onEdit = std::move(cfg.onEdit), labelled](CellInputCtx& ctx)
        {
            const Rect value = ValueRect(ctx.rect, labelled && !ctx.compact);
            if (ctx.event == CellEvent::Press)
            {
                return value.Contains(ctx.canvasPos);
            }
            if (Clicked(value, ctx) && ctx.controller != nullptr && set)
            {
                const GraphNodeId node = ctx.node;
                Controller*       controller = ctx.controller;
                controller->OpenChoices(value, options,
                                        [set, onEdit, node, controller](int index)
                                        {
                                            Notify(onEdit, node, EditPhase::Begin);
                                            set(node, index);
                                            Notify(onEdit, node, EditPhase::Commit);
                                            controller->Repaint();
                                        });
            }
            return ctx.event == CellEvent::Release;
        };
        return Cell{std::move(cell)};
    }

    Cell ToggleField(ToggleFieldCfg cfg)
    {
        const bool labelled = !cfg.label.empty();
        CustomCell cell;
        cell.height = Theme{}.CellHeight;
        cell.pin = cfg.pin;
        cell.label = cfg.label;
        cell.paint = [label = cfg.label, get = cfg.get, labelled](CellPaintCtx& ctx)
        {
            PaintLabel(ctx, label);
            const Rect   value = ValueRect(ctx.rect, labelled && !ctx.compact);
            const Rect   box{value.x, value.y, value.height, value.height};
            const Theme& theme = ThemeOf(ctx);
            PaintBox(ctx, box);
            ctx.painter.StrokeRect(box, theme.NodeBorder, 1.f, 3.f * ctx.zoom);
            if (get && get(ctx.node))
            {
                const float inset = box.height * 0.25f;
                ctx.painter.FillRect(Rect{box.x + inset, box.y + inset, box.width - inset * 2.f,
                                          box.height - inset * 2.f},
                                     theme.NodeSelected, 2.f * ctx.zoom);
            }
        };
        cell.input = [get = std::move(cfg.get), set = std::move(cfg.set), onEdit = std::move(cfg.onEdit),
                      labelled](CellInputCtx& ctx)
        {
            const Rect value = ValueRect(ctx.rect, labelled && !ctx.compact);
            const Rect box{value.x, value.y, value.height, value.height};
            if (ctx.event == CellEvent::Press)
            {
                return box.Contains(ctx.canvasPos);
            }
            if (Clicked(box, ctx) && get && set)
            {
                Notify(onEdit, ctx.node, EditPhase::Begin);
                set(ctx.node, !get(ctx.node));
                Notify(onEdit, ctx.node, EditPhase::Commit);
                Repaint(ctx);
            }
            return ctx.event == CellEvent::Release;
        };
        return Cell{std::move(cell)};
    }
} // namespace lumora::nodegraph
