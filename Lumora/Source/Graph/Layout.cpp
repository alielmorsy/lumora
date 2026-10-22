#include "Graph/Layout.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace lumora::nodegraph
{
    namespace
    {
        constexpr float kMinTangent = 24.f;
        constexpr float kMaxTangent = 160.f;
        constexpr int   kFlattenSegments = 16;
        constexpr float kPinGrabScreenPx = 6.f;
        constexpr float kLinkGrabScreenPx = 6.f;
        constexpr float kResizeGrabScreenPx = 4.f;
        /// Canvas units: covers the three-line grip Paint draws in the corner.
        constexpr float kCornerGrip = 12.f;

        [[nodiscard]] int CountGroups(const Cell& cell)
        {
            if (const auto* group = std::get_if<GroupCell>(&cell))
            {
                int n = 1;
                for (const Cell& child : group->children)
                {
                    n += CountGroups(child);
                }
                return n;
            }
            return 0;
        }

        /// The cell plus every cell nested under it, for pre-order body indices.
        [[nodiscard]] int CountCells(const Cell& cell)
        {
            int n = 1;
            if (const auto* group = std::get_if<GroupCell>(&cell))
            {
                for (const Cell& child : group->children)
                {
                    n += CountCells(child);
                }
            }
            return n;
        }

        /// Flag every pin slot a cell (or a group's descendant) places on a row.
        void MarkNamed(const Cell& cell, std::vector<bool>& named)
        {
            const auto mark = [&named](std::uint16_t slot)
            {
                if (slot < named.size())
                {
                    named[slot] = true;
                }
            };
            if (const auto* row = std::get_if<PinRowCell>(&cell))
            {
                mark(row->input);
                mark(row->output);
            }
            else if (const auto* custom = std::get_if<CustomCell>(&cell))
            {
                mark(custom->pin);
            }
            else if (const auto* group = std::get_if<GroupCell>(&cell))
            {
                for (const Cell& child : group->children)
                {
                    MarkNamed(child, named);
                }
            }
        }

        /// Height accounting and cell emission for one node's body. Group indices are assigned in
        /// pre-order and must be assigned identically by the measuring pass and the emitting pass,
        /// which is why both go through NextIndex/CountGroups rather than a shared mutable counter.
        struct Builder
        {
            const Node&  node;
            const Theme& theme;
            NodeGeom&    out;
            float        inner = 0.f;
            float        cursorY = 0.f;
            int          bodyIndex = 0;
            /// Extra height each CustomCell::stretch cell takes; 0 under a fixed-height group.
            float        stretch = 0.f;

            [[nodiscard]] bool Open(int groupIndex) const
            {
                return groupIndex < 32 && (node.openGroups & (1u << groupIndex)) != 0u;
            }

            [[nodiscard]] bool Fixed(const GroupCell& group, int groupIndex) const
            {
                return (groupIndex < 4 && node.groupHeight[groupIndex] > 0.f) || group.height > 0.f;
            }

            /// Stretch cells that can grow: not under a closed or fixed-height group.
            [[nodiscard]] int CountStretch(const std::vector<Cell>& cells, int& groupIndex) const
            {
                int count = 0;
                for (const Cell& cell : cells)
                {
                    if (const auto* group = std::get_if<GroupCell>(&cell))
                    {
                        const int index = groupIndex++;
                        if (!Open(index) || Fixed(*group, index))
                        {
                            groupIndex = index + CountGroups(cell);
                            continue;
                        }
                        count += CountStretch(group->children, groupIndex);
                        continue;
                    }
                    if (const auto* custom = std::get_if<CustomCell>(&cell); custom != nullptr && custom->stretch)
                    {
                        ++count;
                    }
                }
                return count;
            }

            [[nodiscard]] float LeafHeight(const Cell& cell) const
            {
                if (const auto* image = std::get_if<ImageCell>(&cell))
                {
                    const float aspect = image->aspect > 0.f ? image->aspect : 1.f;
                    return inner / aspect;
                }
                if (const auto* spacer = std::get_if<SpacerCell>(&cell))
                {
                    return spacer->height;
                }
                if (const auto* custom = std::get_if<CustomCell>(&cell))
                {
                    return custom->height + (custom->stretch ? stretch : 0.f);
                }
                return theme.CellHeight;
            }

            /// The vertical budget a group's children get: the node's drag-resize if any, else the
            /// authored height, else the natural sum. Zero when the group is closed.
            [[nodiscard]] float GroupBody(const GroupCell& group, int groupIndex) const
            {
                if (!Open(groupIndex))
                {
                    return 0.f;
                }
                if (groupIndex < 4 && node.groupHeight[groupIndex] > 0.f)
                {
                    return node.groupHeight[groupIndex];
                }
                if (group.height > 0.f)
                {
                    return group.height;
                }
                float total = 0.f;
                int   nested = groupIndex + 1;
                for (const Cell& child : group.children)
                {
                    total += Measure(child, nested);
                    nested += CountGroups(child);
                }
                return total;
            }

            [[nodiscard]] float Measure(const Cell& cell, int groupIndex) const
            {
                if (const auto* group = std::get_if<GroupCell>(&cell))
                {
                    return theme.CellHeight + GroupBody(*group, groupIndex);
                }
                return LeafHeight(cell);
            }

            void Emit(const std::vector<Cell>& cells, int depth, int& groupIndex)
            {
                for (const Cell& cell : cells)
                {
                    const int ordinal = bodyIndex++;
                    if (const auto* group = std::get_if<GroupCell>(&cell))
                    {
                        const int index = groupIndex++;
                        out.cells.push_back(
                            CellGeom{.rect = Rect{out.rect.x + theme.CellPadding, cursorY, inner,
                                                  theme.CellHeight},
                                     .cell = &cell,
                                     .groupIndex = index,
                                     .depth = depth,
                                     .bodyIndex = ordinal});
                        cursorY += theme.CellHeight;

                        if (!Open(index))
                        {
                            groupIndex = index + CountGroups(cell);
                            bodyIndex = ordinal + CountCells(cell);
                            continue;
                        }
                        const float budget = GroupBody(*group, index);
                        const float start = cursorY;
                        const float shared = stretch;
                        if (Fixed(*group, index))
                        {
                            stretch = 0.f;
                        }
                        Emit(group->children, depth + 1, groupIndex);
                        stretch = shared;
                        cursorY = start + budget;
                        continue;
                    }

                    const float h = LeafHeight(cell);
                    out.cells.push_back(CellGeom{
                        .rect = Rect{out.rect.x + theme.CellPadding, cursorY, inner, h},
                        .cell = &cell,
                        .groupIndex = -1,
                        .depth = depth,
                        .bodyIndex = ordinal});
                    cursorY += h;
                }
            }
        };

        /// Pins the emitted cells carry, centred on their rows; flags each placed slot in `claimed`.
        void PlaceCellPins(NodeGeom& geom, const NodeTypeDesc& desc, const Theme& theme, std::vector<bool>& claimed)
        {
            const auto place = [&](std::uint16_t slot, float centreY, bool left)
            {
                const PinDesc& pin = desc.pins[slot];
                claimed[slot] = true;
                geom.pins.push_back(PinGeom{
                    .centre = Vec2{left ? geom.rect.x - theme.PinInset : geom.rect.Right() + theme.PinInset, centreY},
                    .slot = slot,
                    .dir = pin.dir,
                    .type = pin.type,
                    .shape = pin.shape});
            };
            for (const CellGeom& cellGeom : geom.cells)
            {
                const float centreY = cellGeom.rect.y + cellGeom.rect.height * 0.5f;
                if (const auto* custom = std::get_if<CustomCell>(cellGeom.cell);
                    custom != nullptr && custom->pin < desc.pins.size())
                {
                    place(custom->pin, centreY, desc.pins[custom->pin].dir == PinDir::Input);
                    continue;
                }
                const auto* row = std::get_if<PinRowCell>(cellGeom.cell);
                if (row == nullptr)
                {
                    continue;
                }
                if (row->input != NoPin && row->input < desc.pins.size())
                {
                    place(row->input, centreY, true);
                }
                if (row->output != NoPin && row->output < desc.pins.size())
                {
                    place(row->output, centreY, false);
                }
            }
        }

        /// The y of a group's closing edge: the bottom of its last cell (the first cell at or above its own depth ends the run).
        [[nodiscard]] float GroupBottom(const NodeGeom& geom, int caretIndex) noexcept
        {
            const int depth = geom.cells[caretIndex].depth;
            float bottom = geom.cells[caretIndex].rect.Bottom();
            for (int i = caretIndex + 1; i < static_cast<int>(geom.cells.size()); ++i)
            {
                if (geom.cells[i].depth <= depth)
                {
                    break;
                }
                bottom = geom.cells[i].rect.Bottom();
            }
            return bottom;
        }

        [[nodiscard]] float PointToSegment(Vec2 a, Vec2 b, Vec2 p) noexcept
        {
            const Vec2  ab = b - a;
            const float len2 = ab.LengthSquared();
            if (len2 <= 0.f)
            {
                return (p - a).Length();
            }
            const Vec2  ap = p - a;
            const float t = std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.f, 1.f);
            return (p - (a + ab * t)).Length();
        }

        [[nodiscard]] Vec2 CubicAt(Vec2 p0, Vec2 c0, Vec2 c1, Vec2 p1, float t) noexcept
        {
            const float u = 1.f - t;
            return p0 * (u * u * u) + c0 * (3.f * u * u * t) + c1 * (3.f * u * t * t)
                 + p1 * (t * t * t);
        }

        [[nodiscard]] Rect Union(const Rect& a, const Rect& b) noexcept
        {
            const float x = std::min(a.x, b.x);
            const float y = std::min(a.y, b.y);
            return Rect{x, y, std::max(a.Right(), b.Right()) - x,
                        std::max(a.Bottom(), b.Bottom()) - y};
        }
    } // namespace

    ResizeEdges NodeResizeAt(const NodeGeom& geom, const Theme& theme, Vec2 canvasPos, float zoom)
    {
        if (!geom.rect.Contains(canvasPos))
        {
            return {};
        }
        const float zone = std::max(theme.ResizeZone, kResizeGrabScreenPx / (zoom > 0.f ? zoom : 1.f));
        const float grip = std::max(zone, kCornerGrip);
        const float right = geom.rect.Right() - canvasPos.x;
        const float bottom = geom.rect.Bottom() - canvasPos.y;
        if (!geom.collapsed && right <= grip && bottom <= grip)
        {
            return ResizeEdges{.width = true, .height = true};
        }
        return ResizeEdges{.width = right <= zone, .height = !geom.collapsed && bottom <= zone};
    }

    void Layout::Rebuild(const Model& model, const Registry& reg, const Theme& theme, const BodyFn& bodyOf)
    {
        m_geom.assign(model.SlotCount(), NodeGeom{});
        m_gen.assign(model.SlotCount(), 0u);
        m_hasBounds = false;
        m_bounds = Rect{};

        for (std::uint32_t slot = 0; slot < model.SlotCount(); ++slot)
        {
            const GraphNodeId id = model.At(slot);
            if (!id.Valid())
            {
                continue;
            }
            const Node&         node = *model.Get(id);
            const NodeTypeDesc* desc = reg.Find(node.type);
            if (desc == nullptr)
            {
                continue;
            }
            m_gen[slot] = id.gen;

            const std::vector<Cell>* overrideBody = bodyOf ? bodyOf(id) : nullptr;
            const std::vector<Cell>& body = overrideBody != nullptr ? *overrideBody : desc->body;
            const bool               collapsed = node.collapsed && !desc->compact;

            NodeGeom&   geom = m_geom[slot];
            const float width = node.width > 0.f ? node.width : DefaultWidth(*desc, theme);
            geom.compact = desc->compact;
            geom.rect = Rect{node.position.x, node.position.y, width, theme.HeaderHeight};
            geom.header = desc->compact ? Rect{node.position.x, node.position.y, width, 0.f} : geom.rect;
            if (desc->compact)
            {
                geom.symbol = geom.rect;
            }

            std::vector<bool> named(desc->pins.size(), overrideBody == nullptr);
            if (overrideBody != nullptr)
            {
                for (const Cell& cell : body)
                {
                    MarkNamed(cell, named);
                }
            }

            geom.collapsed = collapsed;
            geom.contentHeight = theme.HeaderHeight;
            std::vector<bool> claimed(desc->pins.size(), false);
            bool              stacked = false;
            if (!collapsed)
            {
                const auto emit = [&](float stretch)
                {
                    geom.cells.clear();
                    geom.pins.clear();
                    claimed.assign(claimed.size(), false);
                    Builder builder{node, theme, geom};
                    builder.inner = std::max(width - theme.CellPadding * 2.f, 1.f);
                    builder.cursorY = node.position.y + theme.HeaderHeight;
                    builder.stretch = stretch;
                    int groupIndex = 0;
                    builder.Emit(body, 0, groupIndex);

                    const float bodyHeight = builder.cursorY - (node.position.y + theme.HeaderHeight);
                    geom.body = Rect{node.position.x, node.position.y + theme.HeaderHeight, width, bodyHeight};
                    geom.rect.height = theme.HeaderHeight + bodyHeight;
                    PlaceCellPins(geom, *desc, theme, claimed);
                };
                emit(0.f);

                for (std::size_t index = 0; index < claimed.size(); ++index)
                {
                    stacked = stacked || (named[index] && !claimed[index]);
                }
                geom.contentHeight = geom.rect.height + (stacked ? theme.CellHeight : 0.f);

                if (const float extra = node.height - geom.contentHeight; extra > 0.f)
                {
                    const Builder counter{node, theme, geom};
                    int           groupIndex = 0;
                    if (const int stretchCells = counter.CountStretch(body, groupIndex); stretchCells > 0)
                    {
                        emit(extra / static_cast<float>(stretchCells));
                    }
                    else
                    {
                        geom.body.height += extra;
                        geom.rect.height += extra;
                    }
                }
            }

            if (collapsed)
            {
                int         inputs = 0;
                int         outputs = 0;
                const float step =
                    theme.HeaderHeight / static_cast<float>(std::ranges::count(named, true) + 1);
                for (std::uint16_t index = 0; index < desc->pins.size(); ++index)
                {
                    if (!named[index])
                    {
                        continue;
                    }
                    const PinDesc& pin = desc->pins[index];
                    const bool     input = pin.dir == PinDir::Input;
                    const int      ordinal = input ? inputs++ : outputs++;
                    geom.pins.push_back(PinGeom{
                        .centre = Vec2{input ? geom.rect.x - theme.PinInset
                                             : geom.rect.Right() + theme.PinInset,
                                       geom.rect.y + step * static_cast<float>(ordinal + 1)},
                        .slot = index,
                        .dir = pin.dir,
                        .type = pin.type,
                        .shape = pin.shape});
                }
            }
            else if (stacked)
            {
                const float stackY = geom.rect.Bottom() + theme.CellHeight * 0.5f;
                for (std::uint16_t index = 0; index < desc->pins.size(); ++index)
                {
                    if (claimed[index] || !named[index])
                    {
                        continue;
                    }
                    const PinDesc& pin = desc->pins[index];
                    geom.pins.push_back(PinGeom{
                        .centre = Vec2{pin.dir == PinDir::Input ? geom.rect.x - theme.PinInset
                                                                : geom.rect.Right() + theme.PinInset,
                                       stackY},
                        .slot = index,
                        .dir = pin.dir,
                        .type = pin.type,
                        .shape = pin.shape});
                }
                geom.rect.height += theme.CellHeight;
                geom.body.height += theme.CellHeight;
            }

            Rect withPins = geom.rect;
            for (const PinGeom& pin : geom.pins)
            {
                withPins = Union(withPins, Rect{pin.centre.x - theme.PinRadius,
                                                pin.centre.y - theme.PinRadius,
                                                theme.PinRadius * 2.f, theme.PinRadius * 2.f});
            }
            m_bounds = m_hasBounds ? Union(m_bounds, withPins) : withPins;
            m_hasBounds = true;
        }
    }

    const NodeGeom* Layout::Of(GraphNodeId id) const
    {
        if (!id.Valid() || id.index >= m_geom.size() || m_gen[id.index] != id.gen)
        {
            return nullptr;
        }
        return &m_geom[id.index];
    }

    Rect Layout::Bounds() const { return m_bounds; }

    Rect Layout::BoundsOf(std::span<const GraphNodeId> ids) const
    {
        Rect result{};
        bool any = false;
        for (const GraphNodeId id : ids)
        {
            const NodeGeom* geom = Of(id);
            if (geom == nullptr)
            {
                continue;
            }
            result = any ? Union(result, geom->rect) : geom->rect;
            any = true;
        }
        return result;
    }

    bool Layout::PinPos(GraphNodeId id, std::uint16_t slot, PinDir dir, Vec2& out) const
    {
        const NodeGeom* geom = Of(id);
        if (geom == nullptr)
        {
            return false;
        }
        for (const PinGeom& pin : geom->pins)
        {
            if (pin.slot == slot && pin.dir == dir)
            {
                out = pin.centre;
                return true;
            }
        }
        return false;
    }

    void Layout::Visible(const Model& model, const Rect& canvasWindow,
                         std::vector<GraphNodeId>& out) const
    {
        out.clear();
        for (std::uint32_t slot = 0; slot < model.SlotCount(); ++slot)
        {
            const GraphNodeId id = model.At(slot);
            const NodeGeom*   geom = Of(id);
            if (geom != nullptr && geom->rect.Intersects(canvasWindow))
            {
                out.push_back(id);
            }
        }
    }

    Layout::Hit Layout::Probe(const Model& model, const Theme& theme, Vec2 canvasPos,
                              float zoom) const
    {
        const float scale = zoom > 0.f ? zoom : 1.f;
        const float pinGrab = theme.PinRadius + kPinGrabScreenPx / scale;
        const float resizeGrab = kResizeGrabScreenPx / scale;

        for (std::uint32_t slot = model.SlotCount(); slot-- > 0;)
        {
            const GraphNodeId id = model.At(slot);
            const NodeGeom* geom = Of(id);
            if (geom == nullptr)
            {
                continue;
            }

            for (const PinGeom& pin : geom->pins)
            {
                if ((canvasPos - pin.centre).Length() <= pinGrab)
                {
                    return Hit{.what = Hit::What::Pin,
                               .node = id,
                               .pinSlot = pin.slot,
                               .pinDir = pin.dir};
                }
            }

            const ResizeEdges edges = NodeResizeAt(*geom, theme, canvasPos, zoom);
            if (edges.width && edges.height)
            {
                return Hit{.what = Hit::What::NodeResize, .node = id, .resize = edges};
            }

            for (int index = 0; index < static_cast<int>(geom->cells.size()); ++index)
            {
                const CellGeom& cellGeom = geom->cells[index];
                const auto*     group = std::get_if<GroupCell>(cellGeom.cell);
                if (group == nullptr || !group->resizable)
                {
                    continue;
                }
                const float bottom = GroupBottom(*geom, index);
                if (std::fabs(canvasPos.y - bottom) <= resizeGrab
                    && canvasPos.x >= geom->rect.x && canvasPos.x <= geom->rect.Right())
                {
                    return Hit{.what = Hit::What::GroupResize,
                               .node = id,
                               .groupIndex = cellGeom.groupIndex,
                               .cellIndex = index};
                }
            }

            if (!geom->rect.Contains(canvasPos))
            {
                continue;
            }

            if (edges.width || edges.height)
            {
                return Hit{.what = Hit::What::NodeResize, .node = id, .resize = edges};
            }

            if (geom->header.Contains(canvasPos))
            {
                return Hit{.what = Hit::What::Header, .node = id};
            }

            for (int index = 0; index < static_cast<int>(geom->cells.size()); ++index)
            {
                const CellGeom& cellGeom = geom->cells[index];
                if (!cellGeom.rect.Contains(canvasPos))
                {
                    continue;
                }
                if (cellGeom.groupIndex >= 0
                    && canvasPos.x <= cellGeom.rect.x + theme.CellHeight)
                {
                    return Hit{.what = Hit::What::GroupCaret,
                               .node = id,
                               .groupIndex = cellGeom.groupIndex,
                               .cellIndex = index};
                }
                return Hit{.what = Hit::What::Cell, .node = id, .cellIndex = index};
            }

            return Hit{.what = Hit::What::Body, .node = id};
        }

        const float linkGrab = theme.LinkWidth * 0.5f + kLinkGrabScreenPx / scale;
        for (std::uint32_t slot = 0; slot < model.LinkSlotCount(); ++slot)
        {
            const LinkId id = model.LinkAt(slot);
            if (!id.Valid())
            {
                continue;
            }
            const Link& link = *model.Get(id);
            Vec2        from{};
            Vec2        to{};
            if (!PinPos(link.from.node, link.from.slot, link.from.dir, from)
                || !PinPos(link.to.node, link.to.slot, link.to.dir, to))
            {
                continue;
            }
            if (DistanceToLink(from, to, canvasPos) <= linkGrab)
            {
                return Hit{.what = Hit::What::Link, .link = id};
            }
        }

        return Hit{};
    }

    void Layout::LinkPath(Vec2 from, Vec2 to, Vec2& c0, Vec2& c1)
    {
        const float reach =
            std::clamp(std::fabs(to.x - from.x) * 0.5f, kMinTangent, kMaxTangent);
        c0 = Vec2{from.x + reach, from.y};
        c1 = Vec2{to.x - reach, to.y};
    }

    float Layout::DistanceToLink(Vec2 from, Vec2 to, Vec2 p)
    {
        Vec2 c0{};
        Vec2 c1{};
        LinkPath(from, to, c0, c1);

        float best = (p - from).Length();
        Vec2  previous = from;
        for (int i = 1; i <= kFlattenSegments; ++i)
        {
            const Vec2 current =
                CubicAt(from, c0, c1, to, static_cast<float>(i) / kFlattenSegments);
            best = std::min(best, PointToSegment(previous, current, p));
            previous = current;
        }
        return best;
    }
} // namespace lumora::nodegraph
