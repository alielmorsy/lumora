#include "Graph/Paint.h"
#include "Graph/Input.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace lumora::nodegraph
{
    namespace
    {
        constexpr float kMinorGridZoomCutoff = 0.5f;
        // Tool chrome: the grid stays a screen-space hairline at every zoom.
        constexpr float kGridLineThickness = 1.f;
        constexpr int   kMaxGridLines = 400;

        [[nodiscard]] Color PinColour(const State& state, PinTypeId type)
        {
            if (state.policy.pinColor)
            {
                return state.policy.pinColor(type);
            }
            return state.theme.PinDefault;
        }

        // Paint2D has no text measurer: widths are estimated per code point, so right-aligned labels are off by a few px.
        [[nodiscard]] float EstimateWidth(std::string_view text, float size) noexcept
        {
            const auto glyphs = std::ranges::count_if(text, [](char c)
                                                      { return (static_cast<unsigned char>(c) & 0xC0u) != 0x80u; });
            return size * 0.56f * static_cast<float>(glyphs);
        }

        [[nodiscard]] float TextTop(const Rect& rect, float size) noexcept
        {
            return rect.y + (rect.height - size * 1.25f) * 0.5f;
        }

        void PaintGrid(const State& state, Paint2D& painter)
        {
            const Theme& theme = state.theme;
            const float  zoom = state.camera.Zoom();
            const Rect   window = state.camera.VisibleCanvas(state.viewport);

            const bool  minor = zoom >= kMinorGridZoomCutoff;
            const float step = minor ? theme.GridSpacing : theme.GridSpacing * theme.GridMajorEvery;
            if (step <= 0.f)
            {
                return;
            }

            const float startX = std::floor(window.x / step) * step;
            const float startY = std::floor(window.y / step) * step;
            const int   countX = std::min(static_cast<int>(window.width / step) + 2, kMaxGridLines);
            const int   countY = std::min(static_cast<int>(window.height / step) + 2, kMaxGridLines);
            const float major = theme.GridSpacing * theme.GridMajorEvery;

            for (int i = 0; i < countX; ++i)
            {
                const float canvasX = startX + static_cast<float>(i) * step;
                const bool  isMajor = std::fabs(std::fmod(canvasX, major)) < 0.01f;
                const Vec2  top = state.camera.ToLayer(Vec2{canvasX, window.y}, state.viewport);
                painter.FillRect(Rect{top.x, state.viewport.y, kGridLineThickness,
                                      state.viewport.height},
                                 isMajor ? theme.GridMajor : theme.GridMinor);
            }
            for (int i = 0; i < countY; ++i)
            {
                const float canvasY = startY + static_cast<float>(i) * step;
                const bool  isMajor = std::fabs(std::fmod(canvasY, major)) < 0.01f;
                const Vec2  left = state.camera.ToLayer(Vec2{window.x, canvasY}, state.viewport);
                painter.FillRect(Rect{state.viewport.x, left.y, state.viewport.width,
                                      kGridLineThickness},
                                 isMajor ? theme.GridMajor : theme.GridMinor);
            }
        }

        struct Tooltip
        {
            bool        show = false;
            std::string text;
            Vec2        at{};
        };

        [[nodiscard]] Color WithAlpha(Color colour, std::uint8_t alpha) noexcept
        {
            colour.a = alpha;
            return colour;
        }

        /// `layerWidth` is the final stroke width in layer pixels, zoom already applied.
        void PaintLinkCurve(const State& state, Paint2D& painter, Vec2 fromCanvas, Vec2 toCanvas,
                            Color colour, float layerWidth)
        {
            Vec2 c0{};
            Vec2 c1{};
            Layout::LinkPath(fromCanvas, toCanvas, c0, c1);

            const Rect& vp = state.viewport;
            painter.DrawBezier(state.camera.ToLayer(fromCanvas, vp), state.camera.ToLayer(c0, vp),
                               state.camera.ToLayer(c1, vp), state.camera.ToLayer(toCanvas, vp),
                               colour, std::max(layerWidth, 1.f), LineCap::Round);
        }

        void PaintFrames(const State& state, Paint2D& painter)
        {
            const Theme& theme = state.theme;
            const float  zoom = state.camera.Zoom();
            const Rect   window = state.camera.VisibleCanvas(state.viewport);
            constexpr float kGrip = 10.f;

            for (const Frame& frame : state.model.Frames())
            {
                if (!frame.rect.Intersects(window))
                {
                    continue;
                }
                const Color base = frame.color.a > 0 ? frame.color : theme.FrameDefault;
                const Rect  rect = state.camera.ToLayer(frame.rect, state.viewport);
                const Rect  header{rect.x, rect.y, rect.width,
                                  std::min(theme.FrameHeaderHeight * zoom, rect.height)};
                const float radius = theme.NodeRadius * zoom;

                painter.FillRect(rect, WithAlpha(base, 36), radius);
                painter.PushClip(rect, radius);
                painter.FillRect(header, WithAlpha(base, 120));
                painter.PopClip();
                painter.StrokeRect(rect, WithAlpha(base, 170), std::max(zoom, 1.f), radius);

                painter.PushClip(header);
                painter.DrawString(frame.title,
                                   Vec2{header.x + theme.CellPadding * zoom,
                                        header.y + (header.height - theme.TitleTextSize * zoom * 1.25f) * 0.5f},
                                   theme.Text, theme.TitleTextSize * zoom, true);
                painter.PopClip();

                painter.FillTriangle(Vec2{rect.Right(), rect.Bottom() - kGrip},
                                     Vec2{rect.Right(), rect.Bottom()},
                                     Vec2{rect.Right() - kGrip, rect.Bottom()}, WithAlpha(base, 200));
            }
        }

        void PaintLinks(const State& state, Paint2D& painter, const ViewCfg& cfg)
        {
            const float zoom = state.camera.Zoom();
            for (std::uint32_t slot = 0; slot < state.model.LinkSlotCount(); ++slot)
            {
                const LinkId id = state.model.LinkAt(slot);
                if (!id.Valid())
                {
                    continue;
                }
                const Link& edge = *state.model.Get(id);
                Vec2        from{};
                Vec2        to{};
                if (!state.layout.PinPos(edge.from.node, edge.from.slot, edge.from.dir, from)
                    || !state.layout.PinPos(edge.to.node, edge.to.slot, edge.to.dir, to))
                {
                    continue;
                }
                const bool selected = state.selectedLink == id;
                Color      colour = state.theme.LinkDefault;
                if (const NodeTypeDesc* desc = state.registry.Find(state.model.Get(edge.from.node)->type);
                    desc != nullptr && edge.from.slot < desc->pins.size())
                {
                    colour = PinColour(state, state.ShownType(edge.from, desc->pins[edge.from.slot].type));
                }
                if (cfg.linkColor)
                {
                    colour = cfg.linkColor(id).value_or(colour);
                }
                const float width = cfg.linkWidth ? cfg.linkWidth(id).value_or(state.theme.LinkWidth)
                                                  : state.theme.LinkWidth;
                PaintLinkCurve(state, painter, from, to,
                               selected ? state.theme.NodeSelected : colour,
                               width * (selected ? zoom * 1.6f : zoom));
            }
        }

        void PaintPin(const State& state, Paint2D& painter, const PinGeom& pin, GraphNodeId node, bool connected)
        {
            const float zoom = state.camera.Zoom();
            const Vec2  centre = state.camera.ToLayer(pin.centre, state.viewport);
            const float radius = std::max(state.theme.PinRadius * zoom, 1.5f);
            const Color colour = PinColour(state, state.ShownType(PinRef{node, pin.slot, pin.dir}, pin.type));

            switch (pin.shape)
            {
            case PinShape::Circle:
                connected ? painter.FillCircle(centre, radius, colour)
                          : painter.StrokeCircle(centre, radius, colour, std::max(zoom, 1.f));
                break;
            case PinShape::Square:
                painter.FillRect(Rect{centre.x - radius, centre.y - radius, radius * 2.f,
                                      radius * 2.f},
                                 colour);
                break;
            case PinShape::Diamond:
            {
                const Vec2 pts[4] = {{centre.x, centre.y - radius},
                                     {centre.x + radius, centre.y},
                                     {centre.x, centre.y + radius},
                                     {centre.x - radius, centre.y}};
                painter.FillPolygon(pts, colour);
                break;
            }
            case PinShape::Arrow:
            {
                const Vec2 pts[3] = {{centre.x - radius, centre.y - radius},
                                     {centre.x + radius, centre.y},
                                     {centre.x - radius, centre.y + radius}};
                painter.FillPolygon(pts, colour);
                break;
            }
            }
        }

        /// PinDesc::name after ViewCfg::pinLabel.
        [[nodiscard]] std::string PinLabel(const State& state, GraphNodeId id, const NodeTypeDesc& desc, std::uint16_t slot)
        {
            const PinDesc& pin = desc.pins[slot];
            if (state.cfg.pinLabel)
            {
                if (std::optional<std::string> label = state.cfg.pinLabel(PinRef{id, slot, pin.dir}))
                {
                    return std::move(*label);
                }
            }
            return pin.name;
        }

        void PaintPinLabels(const State& state, Paint2D& painter, const PinRowCell& row, const Rect& rect,
                            GraphNodeId id, const NodeTypeDesc& desc)
        {
            const Theme& theme = state.theme;
            const float  size = theme.CellTextSize * state.camera.Zoom();
            const float  y = TextTop(rect, size);
            if (row.input < desc.pins.size())
            {
                if (const std::string name = PinLabel(state, id, desc, row.input); !name.empty())
                {
                    painter.DrawString(name, Vec2{rect.x, y},
                                       state.InputLinked(id, row.input) ? theme.Text : theme.TextDim, size);
                }
            }
            if (row.output < desc.pins.size())
            {
                if (const std::string name = PinLabel(state, id, desc, row.output); !name.empty())
                {
                    painter.DrawString(name, Vec2{rect.Right() - EstimateWidth(name, size), y},
                                       state.OutputLinked(id, row.output) ? theme.Text : theme.TextDim, size);
                }
            }
        }

        void PaintCell(State& state, Paint2D& painter, const CellGeom& geom, GraphNodeId id, bool hovered,
                       const NodeTypeDesc& desc, bool compact)
        {
            const Theme& theme = state.theme;
            const float  zoom = state.camera.Zoom();
            const Rect   rect = state.camera.ToLayer(geom.rect, state.viewport);

            if (const auto* row = std::get_if<PinRowCell>(geom.cell))
            {
                if (!compact)
                {
                    PaintPinLabels(state, painter, *row, rect, id, desc);
                }
                return;
            }
            if (const auto* label = std::get_if<LabelCell>(geom.cell))
            {
                painter.DrawString(label->text, Vec2{rect.x, rect.y + rect.height * 0.25f},
                                   theme.Text, theme.CellTextSize * zoom, label->bold);
                return;
            }
            if (const auto* text = std::get_if<TextCell>(geom.cell))
            {
                painter.DrawString(text->label, Vec2{rect.x, rect.y + rect.height * 0.25f},
                                   theme.TextDim, theme.CellTextSize * zoom);
                if (text->value)
                {
                    const std::string value = text->value(id);
                    painter.DrawString(value, Vec2{rect.Center().x, rect.y + rect.height * 0.25f},
                                       theme.Text, theme.CellTextSize * zoom);
                }
                return;
            }
            if (const auto* image = std::get_if<ImageCell>(geom.cell))
            {
                if (image->image)
                {
                    const ImageHandle handle = image->image(id);
                    if (handle.Valid())
                    {
                        painter.DrawImage(handle, rect, 1.f, theme.NodeRadius * zoom * 0.5f);
                    }
                }
                return;
            }
            if (const auto* group = std::get_if<GroupCell>(geom.cell))
            {
                const float caret = theme.CellHeight * zoom * 0.5f;
                const bool  open = geom.groupIndex < 32
                               && (state.model.Get(id)->openGroups & (1u << geom.groupIndex)) != 0u;
                const Vec2  origin{rect.x + caret * 0.4f, rect.y + rect.height * 0.5f};
                // std::array: a C array cannot be initialised from a ternary, and FillPolygon takes a span.
                const std::array<Vec2, 3> pts =
                    open ? std::array<Vec2, 3>{{{origin.x - caret * 0.4f, origin.y - caret * 0.25f},
                                                {origin.x + caret * 0.4f, origin.y - caret * 0.25f},
                                                {origin.x, origin.y + caret * 0.35f}}}
                         : std::array<Vec2, 3>{{{origin.x - caret * 0.25f, origin.y - caret * 0.4f},
                                                {origin.x + caret * 0.35f, origin.y},
                                                {origin.x - caret * 0.25f, origin.y + caret * 0.4f}}};
                painter.FillPolygon(pts, theme.TextDim);
                painter.DrawString(group->title, Vec2{rect.x + caret, rect.y + rect.height * 0.25f},
                                   theme.TextDim, theme.CellTextSize * zoom);
                return;
            }
            if (const auto* custom = std::get_if<CustomCell>(geom.cell))
            {
                if (custom->pin != NoPin && state.InputLinked(id, custom->pin))
                {
                    const std::string text = !custom->label.empty() || custom->pin >= desc.pins.size()
                                                 ? custom->label
                                                 : PinLabel(state, id, desc, custom->pin);
                    if (!compact)
                    {
                        painter.DrawString(text, Vec2{rect.x, TextTop(rect, theme.CellTextSize * zoom)},
                                           theme.Text, theme.CellTextSize * zoom);
                    }
                    return;
                }
                if (custom->paint)
                {
                    painter.PushClip(rect);
                    CellPaintCtx cellCtx{.painter = painter,
                                         .rect = rect,
                                         .zoom = zoom,
                                         .node = id,
                                         .hovered = hovered,
                                         .theme = &theme,
                                         .compact = compact};
                    custom->paint(cellCtx);
                    painter.PopClip();
                }
                return;
            }
        }

        /// `edge` is the run the stripes split: the header on a normal node, the whole body on a
        /// compact one, which always stripes down its left edge.
        void PaintStripes(const State& state, Paint2D& painter, const ViewCfg& cfg, GraphNodeId id,
                          const Rect& rect, const Rect& edge, StripeAxis axis, float radius)
        {
            if (!cfg.nodeStripes)
            {
                return;
            }
            const std::vector<Color> colours = cfg.nodeStripes(id);
            if (colours.empty())
            {
                return;
            }
            const float thickness = state.theme.StripeThickness * state.camera.Zoom();
            const float count = static_cast<float>(colours.size());
            painter.PushClip(rect, radius);
            for (std::size_t i = 0; i < colours.size(); ++i)
            {
                const float t0 = static_cast<float>(i) / count;
                const float t1 = static_cast<float>(i + 1) / count;
                const Rect  band =
                    axis == StripeAxis::Horizontal
                        ? Rect{edge.x + edge.width * t0, edge.y, edge.width * (t1 - t0), thickness}
                        : Rect{edge.x, edge.y + edge.height * t0, thickness, edge.height * (t1 - t0)};
                painter.FillRect(band, colours[i]);
            }
            painter.PopClip();
        }

        /// Hovering a resize strip (right edge, bottom edge, or the corner grip for both) lights it; a
        /// hovered or selected node shows a three-line grip in its bottom-right corner.
        void PaintResizeAffordance(const State& state, Paint2D& painter, const NodeGeom& geom, const Rect& rect,
                                   bool selected)
        {
            const Theme& theme = state.theme;
            const float  zoom = state.camera.Zoom();
            const Vec2   mouse = painter.Mouse();
            if (!selected && !rect.Contains(mouse))
            {
                return;
            }
            const ResizeEdges edges = NodeResizeAt(geom, theme, state.ToCanvas(mouse), zoom);
            const float       line = 2.f * std::max(zoom, 1.f);
            const float       inset = theme.NodeRadius * zoom;
            if (edges.width)
            {
                painter.FillRect(Rect{rect.Right() - line, rect.y + inset, line, rect.height - inset * 2.f},
                                 theme.NodeSelected);
            }
            if (edges.height)
            {
                painter.FillRect(Rect{rect.x + inset, rect.Bottom() - line, rect.width - inset * 2.f, line},
                                 theme.NodeSelected);
            }
            const Color grip = edges.width || edges.height ? theme.NodeSelected : theme.TextDim;
            const float step = 3.5f * zoom;
            const float width = std::max(zoom, 1.f);
            const Vec2  corner{rect.Right() - 2.f * zoom, rect.Bottom() - 2.f * zoom};
            for (int i = 1; i <= 3; ++i)
            {
                const float reach = step * static_cast<float>(i);
                painter.DrawLine(Vec2{corner.x - reach, corner.y}, Vec2{corner.x, corner.y - reach}, grip, width);
            }
        }

        void PaintBadge(const State& state, Paint2D& painter, const ViewCfg& cfg, GraphNodeId id,
                        const Rect& header, Tooltip& tooltip)
        {
            if (!cfg.nodeBadge)
            {
                return;
            }
            const std::optional<Badge> badge = cfg.nodeBadge(id);
            if (!badge)
            {
                return;
            }
            const float zoom = state.camera.Zoom();
            const float radius = std::max(state.theme.BadgeRadius * zoom, 2.f);
            const Vec2  centre{header.Right(), header.y};
            painter.FillCircle(centre, radius + std::max(zoom, 1.f), state.theme.NodeBg);
            painter.FillCircle(centre, radius, badge->color);
            if (!badge->text.empty() && (painter.Mouse() - centre).Length() <= radius + 3.f)
            {
                tooltip = Tooltip{.show = true, .text = badge->text, .at = centre};
            }
        }

        void PaintNode(State& state, Paint2D& painter, GraphNodeId id, const ViewCfg& cfg, Tooltip& tooltip)
        {
            const NodeGeom* geom = state.layout.Of(id);
            const Node*     node = state.model.Get(id);
            if (geom == nullptr || node == nullptr)
            {
                return;
            }
            const NodeTypeDesc* desc = state.registry.Find(node->type);
            if (desc == nullptr)
            {
                return;
            }

            const Theme& theme = state.theme;
            const float  zoom = state.camera.Zoom();
            const Rect   rect = state.camera.ToLayer(geom->rect, state.viewport);
            const Rect   header = state.camera.ToLayer(geom->header, state.viewport);
            const float  radius = (geom->compact ? theme.NodeRadius * 2.f : theme.NodeRadius) * zoom;
            const bool   selected = std::ranges::find(state.selection, id) != state.selection.end();

            Color headerColour = desc->accent.a > 0 ? desc->accent : theme.NodeHeader;
            if (cfg.nodeAccent)
            {
                headerColour = cfg.nodeAccent(id).value_or(headerColour);
            }

            // Opaque whatever the theme says: a translucent body lets frames and links show through.
            painter.FillRect(rect, WithAlpha(theme.NodeBg, 255), radius);
            if (geom->compact)
            {
                PaintStripes(state, painter, cfg, id, rect, rect, StripeAxis::Vertical, radius);
            }
            else
            {
                painter.FillRect(header, WithAlpha(headerColour, 255), radius);
                PaintStripes(state, painter, cfg, id, rect, header, cfg.stripeAxis, radius);
            }
            painter.StrokeRect(rect, theme.NodeBorder, std::max(zoom, 1.f), radius);

            if (geom->compact)
            {
                const std::string& symbol = node->title.empty() ? desc->symbol : node->title;
                const Rect         band = state.camera.ToLayer(geom->symbol, state.viewport);
                const float        size = theme.TitleTextSize * 1.5f * zoom;
                painter.PushClip(band);
                painter.DrawString(symbol, Vec2{band.Center().x - EstimateWidth(symbol, size) * 0.5f, TextTop(band, size)},
                                   theme.Text, size, true);
                painter.PopClip();
            }
            else
            {
                painter.DrawString(node->title.empty() ? desc->title : node->title,
                                   Vec2{header.x + theme.CellPadding * zoom,
                                        header.y + header.height * 0.25f},
                                   theme.Text, theme.TitleTextSize * zoom, true);
            }

            if (!geom->cells.empty())
            {
                painter.PushClip(rect, radius);
                for (const CellGeom& cell : geom->cells)
                {
                    const bool hovered =
                        state.camera.ToLayer(cell.rect, state.viewport).Contains(painter.Mouse());
                    PaintCell(state, painter, cell, id, hovered, *desc, geom->compact);
                }
                painter.PopClip();
            }

            if (selected)
            {
                painter.StrokeRect(rect, theme.NodeSelected, std::max(2.f * zoom, 1.5f), radius);
            }
            PaintResizeAffordance(state, painter, *geom, rect, selected);

            for (const PinGeom& pin : geom->pins)
            {
                const bool connected =
                    pin.dir == PinDir::Input
                        ? state.model.LinkOnInput(PinRef{id, pin.slot, PinDir::Input}).Valid()
                        : false;
                PaintPin(state, painter, pin, id, connected);
            }

            PaintBadge(state, painter, cfg, id, geom->compact ? rect : header, tooltip);
        }

        void PaintCascade(const State& state, Paint2D& painter)
        {
            const Popup&    popup = state.popup;
            const Theme&    theme = state.theme;
            constexpr float kPad = 8.f;
            constexpr float kRadius = 6.f;
            const float     textY = (kPopupRowHeight - kPopupTextSize * 1.25f) * 0.5f;

            for (int column = 0; column < static_cast<int>(popup.path.size()); ++column)
            {
                const Rect         rect = CascadeColumnRect(state, column);
                const PaletteTree& node = CascadeNode(popup, column);
                const int          categories = static_cast<int>(node.children.size());
                const int          count = CascadeEntries(node);

                painter.FillRect(rect, theme.PopupBg, kRadius);
                painter.StrokeRect(rect, theme.NodeBorder, 1.f, kRadius);
                painter.PushClip(rect, kRadius);

                float top = rect.y + 2.f;
                if (column == 0)
                {
                    painter.DrawString("Search...", Vec2{rect.x + kPad, rect.y + textY + 2.f}, theme.TextDim,
                                       kPopupTextSize);
                    painter.FillRect(Rect{rect.x, rect.y + kPopupRowHeight + 1.f, rect.width, 1.f}, theme.NodeBorder);
                    top += kPopupRowHeight;
                }
                if (count == 0)
                {
                    painter.DrawString("Nothing to add", Vec2{rect.x + kPad, top + textY}, theme.TextDim, kPopupTextSize);
                }

                for (int line = 0; line < kCascadeRows; ++line)
                {
                    const int entry = popup.columnScroll[column] + line;
                    if (entry >= count)
                    {
                        break;
                    }
                    const Rect row{rect.x + 2.f, top + kPopupRowHeight * static_cast<float>(line), rect.width - 4.f,
                                   kPopupRowHeight};
                    if (entry == popup.path[column])
                    {
                        painter.FillRect(row, theme.PopupHighlight, 4.f);
                    }
                    const Vec2 textPos{row.x + kPad, row.y + textY};
                    if (entry < categories)
                    {
                        painter.DrawString(node.children[entry].name, textPos, theme.Text, kPopupTextSize);
                        const float s = kPopupRowHeight * 0.16f;
                        const Vec2  c{row.Right() - kPad, row.y + row.height * 0.5f};
                        painter.FillTriangle(Vec2{c.x - s, c.y - s * 1.2f}, Vec2{c.x + s * 0.6f, c.y},
                                             Vec2{c.x - s, c.y + s * 1.2f}, theme.TextDim);
                        continue;
                    }
                    painter.DrawString(popup.items[node.items[entry - categories]].title, textPos, theme.Text,
                                       kPopupTextSize);
                }
                painter.PopClip();
            }
        }

        void PaintPopup(const State& state, Paint2D& painter)
        {
            const Popup& popup = state.popup;
            if (!popup.open)
            {
                return;
            }
            if (popup.Cascading())
            {
                PaintCascade(state, painter);
                return;
            }
            const Theme&    theme = state.theme;
            const Rect      rect = PopupRect(state);
            constexpr float kPad = 8.f;
            constexpr float kRadius = 6.f;
            const float     textY = (kPopupRowHeight - kPopupTextSize * 1.25f) * 0.5f;

            painter.FillRect(rect, theme.PopupBg, kRadius);
            painter.StrokeRect(rect, theme.NodeBorder, 1.f, kRadius);
            painter.PushClip(rect, kRadius);

            const Vec2 queryPos{rect.x + kPad, rect.y + textY + 2.f};
            if (popup.query.empty())
            {
                painter.DrawString("Search...", queryPos, theme.TextDim, kPopupTextSize);
            }
            else
            {
                painter.DrawString(popup.query + "|", queryPos, theme.Text, kPopupTextSize);
            }
            painter.FillRect(Rect{rect.x, rect.y + kPopupRowHeight + 1.f, rect.width, 1.f},
                             theme.NodeBorder);

            if (popup.rows.empty())
            {
                painter.DrawString("No matches",
                                   Vec2{rect.x + kPad, rect.y + kPopupRowHeight + textY + 2.f},
                                   theme.TextDim, kPopupTextSize);
            }

            const bool grouped = std::ranges::any_of(popup.rows, [](const PopupRow& row)
                                                     { return row.item < 0; });
            for (int line = 0; line < kPopupVisibleRows; ++line)
            {
                const int index = popup.scroll + line;
                if (index >= static_cast<int>(popup.rows.size()))
                {
                    break;
                }
                const PopupRow& row = popup.rows[index];
                const Rect      rowRect{rect.x + 2.f,
                                   rect.y + kPopupRowHeight * static_cast<float>(line + 1) + 2.f,
                                   rect.width - 4.f, kPopupRowHeight};
                const Vec2      textPos{rowRect.x + kPad, rowRect.y + textY};
                if (row.item < 0)
                {
                    painter.DrawString(row.header, textPos, theme.TextDim, kPopupTextSize - 1.f, true);
                    continue;
                }
                if (index == popup.highlight)
                {
                    painter.FillRect(rowRect, theme.PopupHighlight, 4.f);
                }
                const PopupItem& item = popup.items[row.item];
                painter.DrawString(item.title, Vec2{textPos.x + (grouped ? kPad : 0.f), textPos.y},
                                   theme.Text, kPopupTextSize);
                if (!popup.query.empty() && !item.category.empty())
                {
                    painter.DrawString(item.category, Vec2{rowRect.x + rowRect.width * 0.62f, textPos.y},
                                       theme.TextDim, kPopupTextSize - 1.f);
                }
            }
            painter.PopClip();
        }

        void PaintTooltip(const State& state, Paint2D& painter, const Tooltip& tooltip)
        {
            if (!tooltip.show)
            {
                return;
            }
            constexpr float kSize = 12.f;
            // Width estimated from the byte count (no text measurer in Paint2D).
            const float width = kSize * 0.6f * static_cast<float>(tooltip.text.size()) + 18.f;
            const float height = kSize * 1.25f + 10.f;
            const Rect& vp = state.viewport;
            Rect        box{tooltip.at.x - width * 0.5f, tooltip.at.y - height - 6.f, width, height};
            box.x = std::max(std::min(box.x, vp.Right() - width), vp.x);
            if (box.y < vp.y)
            {
                box.y = tooltip.at.y + 10.f;
            }
            painter.FillRect(box, state.theme.TooltipBg, 6.f);
            painter.DrawString(tooltip.text, Vec2{box.x + 9.f, box.y + 5.f}, state.theme.TooltipText,
                               kSize);
        }

        void PaintOverlays(const State& state, Paint2D& painter)
        {
            const Theme& theme = state.theme;
            const float  zoom = state.camera.Zoom();

            if (state.tool == Tool::DragLink && state.pendingFrom.node.Valid())
            {
                Vec2 from{};
                if (state.layout.PinPos(state.pendingFrom.node, state.pendingFrom.slot,
                                        state.pendingFrom.dir, from))
                {
                    PaintLinkCurve(state, painter, from, state.pendingToCanvas,
                                   state.pendingLegal ? theme.NodeSelected : theme.LinkInvalid,
                                   theme.LinkWidth * zoom);
                }
            }

            if (state.tool == Tool::BoxSelect)
            {
                const Rect box = state.camera.ToLayer(state.boxSelect, state.viewport);
                painter.FillRect(box, theme.BoxSelect);
                // Tool chrome: the marquee border is deliberately not scaled.
                painter.StrokeRect(box, theme.NodeSelected, 1.f);
            }
        }
    } // namespace

    void PaintGraph(State& state, Paint2D& painter, const ViewCfg& cfg)
    {
        state.viewport = painter.Bounds();
        state.surfaceToLayer = painter.Bounds().Position() - painter.SurfaceBounds().Position();
        state.EnsureLayout();

        painter.FillRect(state.viewport, state.theme.Background);

        if (cfg.showGrid)
        {
            PaintGrid(state, painter);
        }

        PaintFrames(state, painter);
        PaintLinks(state, painter, cfg);

        Tooltip                  tooltip;
        std::vector<GraphNodeId> visible;
        state.layout.Visible(state.model, state.camera.VisibleCanvas(state.viewport), visible);
        // Frames, then links, then each node whole (body, cells, selection outline, pins) in stacking
        // order, so a later node covers everything of an earlier one.
        for (const GraphNodeId id : visible)
        {
            PaintNode(state, painter, id, cfg, tooltip);
        }

        PaintOverlays(state, painter);
        PaintPopup(state, painter);
        PaintTooltip(state, painter, tooltip);

        if (cfg.showDebug)
        {
            char line[128] = {};
            std::snprintf(line, sizeof(line), "nodes %u  visible %zu  links %u  zoom %.2f",
                          state.model.NodeCount(), visible.size(), state.model.LinkCount(),
                          static_cast<double>(state.camera.Zoom()));
            painter.DrawString(line, Vec2{state.viewport.x + 8.f, state.viewport.y + 8.f},
                               state.theme.TextDim, 11.f);
        }
    }
} // namespace lumora::nodegraph
