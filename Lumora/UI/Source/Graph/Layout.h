#pragma once

#include <lumora/Graph/Model.h>
#include <lumora/Graph/Types.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace lumora::nodegraph
{
    /// The per-instance body override ViewCfg::nodeBody supplies; null or returning null takes the type body.
    using BodyFn = std::function<const std::vector<Cell>*(GraphNodeId)>;

    /// A node's width when Node::width is 0, and the floor a resize stops at. A compact node defaults to
    /// Theme::CompactWidth unless its type asks for less.
    [[nodiscard]] inline float DefaultWidth(const NodeTypeDesc& desc, const Theme& theme) noexcept
    {
        return desc.compact ? std::min(desc.minWidth, theme.CompactWidth) : desc.minWidth;
    }

    /// One laid-out cell, in canvas space. `cell` points into the type descriptor, so a NodeGeom is
    /// only valid while the Registry entry it was built from is alive.
    struct CellGeom
    {
        Rect        rect{};
        const Cell* cell = nullptr;
        /// Index among the type's GroupCells in pre-order, or -1 when this cell is not a group caret.
        int         groupIndex = -1;
        /// Nesting depth; a group's children are deeper than its caret. GroupBottom walks this.
        int         depth = 0;
        /// Pre-order index of this cell in the effective body, closed groups' children counted.
        int         bodyIndex = 0;
    };

    struct PinGeom
    {
        Vec2          centre{};
        std::uint16_t slot = 0;
        PinDir        dir = PinDir::Input;
        PinTypeId     type{};
        PinShape      shape = PinShape::Circle;
    };

    /// Everything the paint and input passes need about one node, in canvas units. `cells` holds only
    /// visible cells - a closed group's children are absent rather than flagged.
    struct NodeGeom
    {
        Rect                  rect{};
        /// Zero height on a compact node, which has no header bar and so never hits as Header.
        Rect                  header{};
        Rect                  body{};
        /// Compact nodes only: the band above the rows the symbol is centred in.
        Rect                  symbol{};
        bool                  compact = false;
        std::vector<CellGeom> cells;
        /// Only the pins the effective body places; a pin absent here is hidden.
        std::vector<PinGeom>  pins;
        /// Header only: Node::height is ignored and there is no bottom resize edge.
        bool                  collapsed = false;
        /// The rect height the content alone needs; `rect` is taller when Node::height asks for more.
        float                 contentHeight = 0.f;
    };

    /// Which node edges a resize grab at a point drags. Both on the bottom-right corner grip.
    struct ResizeEdges
    {
        bool width = false;
        bool height = false;
    };

    /// The resize edges of `geom` under `canvasPos`: right / bottom strips Theme::ResizeZone thick (at
    /// least 4 screen px), the corner grip a little larger. None outside the rect; no height when collapsed.
    [[nodiscard]] ResizeEdges NodeResizeAt(const NodeGeom& geom, const Theme& theme, Vec2 canvasPos, float zoom);

    /// Derives geometry from the model and answers hit queries. Pure: no fonts, no paint, no camera.
    /// A node's width comes from its own override or its type's minWidth - text is never measured, so
    /// a node never resizes itself when a label changes under it.
    class Layout
    {
    public:
        /// Recompute every node's geometry, indexed by node slot (a stale GraphNodeId is rejected by generation).
        /// `body` overrides a node's cells (ViewCfg::nodeBody); with an override, pins no cell names are left out.
        void Rebuild(const Model& model, const Registry& reg, const Theme& theme, const BodyFn& body = {});

        [[nodiscard]] const NodeGeom* Of(GraphNodeId id) const;

        /// Union of every node rect inflated by its pins, so a fit frames the pins too. Default-
        /// constructed when the graph has no nodes.
        [[nodiscard]] Rect Bounds() const;
        [[nodiscard]] Rect BoundsOf(std::span<const GraphNodeId> ids) const;

        /// Canvas-space centre of one pin. False when the node or slot is unknown.
        [[nodiscard]] bool PinPos(GraphNodeId id, std::uint16_t slot, PinDir dir, Vec2& out) const;

        /// Live nodes whose rect intersects `canvasWindow`, in back-to-front order. Clears `out`.
        void Visible(const Model& model, const Rect& canvasWindow, std::vector<GraphNodeId>& out) const;

        struct Hit
        {
            enum class What : std::uint8_t
            { None, Pin, GroupCaret, GroupResize, NodeResize, Header, Cell, Body, Link };

            What          what = What::None;
            GraphNodeId   node{};
            LinkId        link{};
            std::uint16_t pinSlot = NoPin;
            PinDir        pinDir = PinDir::Input;
            int           groupIndex = -1;
            int           cellIndex = -1;
            /// NodeResize only: the edges the grab drags.
            ResizeEdges   resize{};
        };

        /// What is under `canvasPos`; `zoom` converts screen-space grab tolerances to canvas units.
        /// Priority per node: pin, corner grip, group/node resize edge, header, cell, body; links only if no node claims the point.
        [[nodiscard]] Hit Probe(const Model& model, const Theme& theme, Vec2 canvasPos, float zoom) const;

        /// Cubic control points for a link: horizontal tangents whose reach grows with the span, so
        /// short links stay taut and long ones bow.
        static void LinkPath(Vec2 from, Vec2 to, Vec2& c0, Vec2& c1);
        /// Shortest distance from `p` to the link curve, by flattening to 16 segments.
        static float DistanceToLink(Vec2 from, Vec2 to, Vec2 p);

    private:
        std::vector<NodeGeom>      m_geom;
        std::vector<std::uint32_t> m_gen;
        Rect                       m_bounds{};
        bool                       m_hasBounds = false;
    };
} // namespace lumora::nodegraph
