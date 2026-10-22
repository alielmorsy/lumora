#pragma once

#include <lumora/Graph/Graph.h>

#include "Graph/History.h"
#include "Graph/Layout.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lumora::nodegraph
{
    /// Which gesture is in flight. Pan is Alt/Space+drag; the dispatcher drops middle MouseDown.
    enum class Tool : std::uint8_t
    {
        Idle, PanCamera, DragNodes, DragLink, BoxSelect, ResizeNode, ResizeGroup,
        DragFrame, ResizeFrame, CellCapture
    };

    /// One row of the popup list: a category header (`item` < 0) or an index into Popup::items.
    struct PopupRow
    {
        int         item = -1;
        std::string header;
    };

    struct PopupItem
    {
        std::string              title;
        std::string              category;
        std::vector<std::string> keywords;
        NodeTypeId               type{};
    };

    /// One category of the cascading palette. A column lists `children` (sub-categories) first, then
    /// `items` (indices into Popup::items), each in first-appearance order.
    struct PaletteTree
    {
        std::string              name;
        std::vector<PaletteTree> children;
        std::vector<int>         items;
    };

    /// The canvas-drawn pick list behind both the search palette and EnumField dropdowns. Layer
    /// space, unscaled: it is tool chrome, not canvas content.
    struct Popup
    {
        bool                     open = false;
        Vec2                     layerPos{};
        Vec2                     canvasPos{};
        std::string              query;
        std::vector<PopupItem>   items;
        std::vector<PopupRow>    rows;
        int                      highlight = -1;
        int                      scroll = 0;
        /// Palette only: the pin a link drag was released from.
        PinRef                   from{};
        std::function<void(int)> onPick;

        /// The search palette (not an EnumField list): an empty query shows the category cascade.
        bool             palette = false;
        PaletteTree      tree;
        /// Cascade: path[k] is the highlighted entry of column k (-1 none); column k + 1 is open when
        /// path[k] names a sub-category and path has a k + 1 entry. The last column is the active one.
        std::vector<int> path;
        /// Cascade: first visible entry per column, parallel to `path`.
        std::vector<int> columnScroll;

        [[nodiscard]] bool Cascading() const { return palette && query.empty(); }
    };

    struct TextEdit
    {
        bool                                  active = false;
        Rect                                  canvasRect{};
        std::function<void(std::string_view)> onCommit;
        std::function<void()>                 onCancel;
    };

    /// The whole workspace. One per Controller family; copies of a Controller share this.
    struct State : RefCounted
    {
        Model      model;
        Registry   registry;
        Theme      theme;
        LinkPolicy policy;
        Camera     camera;
        Layout     layout;
        History    history;
        /// The mounted widget's config, so input can reach the per-view flags.
        ViewCfg    cfg;

        Popup    popup;
        TextEdit textEdit;
        /// What the portal-hosted Input reads: open flag, surface rect (unscaled), zoom, and text.
        Signal<bool>        editOpen{false};
        Signal<Rect>        editRect{Rect{}};
        Signal<float>       editZoom{1.f};
        Signal<std::string> editValue{std::string{}};
        FrameId  activeFrame{};
        Rect     frameOrigin{};
        /// Tool::CellCapture: the cell that took the press, and its node and canvas rect.
        const CustomCell*   captureCell = nullptr;
        GraphNodeId         captureNode{};
        Rect                captureRect{};

        /// The value behind the Signal, so a bump is a plain increment rather than a read.
        Signal<std::uint64_t> revision{0};
        std::uint64_t         revisionValue = 0;

        std::vector<GraphNodeId> selection;
        LinkId                   selectedLink{};
        /// Ctrl+C / Ctrl+X hold handles here; Ctrl+V routes them through Controller::Duplicate. Dead
        /// handles are skipped at paste time rather than pruned on every edit.
        std::vector<GraphNodeId> clipboard;

        /// Paint2D::Bounds() from the last paint - layer space.
        Rect viewport{};
        /// Bounds().Position() - SurfaceBounds().Position(): add to a surface-space pointer to get
        /// layer space. Non-zero only under an ancestor Scroll or transform.
        Vec2 surfaceToLayer{};
        /// Last layer-space pointer position. onScroll carries only a float, so wheel zoom anchors
        /// on whatever onMove last reported.
        Vec2 lastPointerLayer{};

        bool layoutDirty = true;

        Tool                     tool = Tool::Idle;
        Vec2                     dragOriginCanvas{};
        Rect                     boxSelect{};
        std::vector<GraphNodeId> dragNodes;
        /// Drag motion not yet applied: with SnapSize > 0 a node only moves in whole grid steps, so sub-step
        /// pointer deltas accumulate here instead of being rounded away one event at a time.
        Vec2                     dragCarry{};
        std::vector<GraphNodeId> selectionBeforeBox;
        PinRef                   pendingFrom{};
        Vec2                     pendingToCanvas{};
        bool                     pendingLegal = false;
        GraphNodeId              resizeNode{};
        int                      resizeGroup = -1;
        float                    resizeOrigin = 0.f;
        /// Tool::ResizeNode: the edges being dragged and the node's rect size when the drag began.
        ResizeEdges              resizeEdges{};
        Vec2                     resizeStart{};
        /// Frames following the gesture's nodes, with their rects when it began (FollowFrames).
        std::vector<std::pair<FrameId, Rect>> followFrames;
        bool                     spaceHeld = false;

        /// Repaint only.
        void Touch()
        {
            revision = ++revisionValue;
        }

        /// Geometry changed too.
        void Invalidate()
        {
            layoutDirty = true;
            Touch();
        }

        void EnsureLayout()
        {
            if (!layoutDirty)
            {
                return;
            }
            layout.Rebuild(model, registry, theme, cfg.nodeBody);
            layoutDirty = false;
        }

        /// Drop selection entries whose handles have died. Called after every structural edit.
        void PruneSelection()
        {
            const std::size_t before = selection.size();
            std::erase_if(selection, [this](GraphNodeId id) { return !model.Alive(id); });
            if (!model.Alive(selectedLink))
            {
                selectedLink = LinkId{};
            }
            if (selection.size() != before)
            {
                Touch();
            }
        }

        /// An input pin that currently has a link - the test that hides a value field behind its wire.
        [[nodiscard]] bool InputLinked(GraphNodeId node, std::uint16_t slot) const
        {
            const Node*    instance = model.Get(node);
            const PinDesc* pin = instance != nullptr ? registry.Pin(instance->type, slot) : nullptr;
            return pin != nullptr && pin->dir == PinDir::Input
                && model.LinkOnInput(PinRef{node, slot, PinDir::Input}).Valid();
        }

        [[nodiscard]] bool OutputLinked(GraphNodeId node, std::uint16_t slot) const
        {
            for (std::uint32_t i = 0; i < model.LinkSlotCount(); ++i)
            {
                const LinkId id = model.LinkAt(i);
                if (id.Valid() && model.Get(id)->from == PinRef{node, slot, PinDir::Output})
                {
                    return true;
                }
            }
            return false;
        }

        /// The pin is placed by the node's effective body. Always true without a ViewCfg::nodeBody,
        /// since the type body stacks every unnamed pin.
        [[nodiscard]] bool PinShown(PinRef pin)
        {
            if (!cfg.nodeBody)
            {
                return true;
            }
            EnsureLayout();
            Vec2 centre{};
            return layout.PinPos(pin.node, pin.slot, pin.dir, centre);
        }

        [[nodiscard]] PinTypeId ShownType(PinRef pin, PinTypeId declared) const
        {
            return cfg.pinType ? cfg.pinType(pin).value_or(declared) : declared;
        }

        /// A custom cell that takes pointer input right now: it has a handler and no wire hides it.
        [[nodiscard]] bool CellLive(GraphNodeId node, const CustomCell& cell) const
        {
            return cell.input && (cell.pin == NoPin || !InputLinked(node, cell.pin));
        }

        [[nodiscard]] Vec2 ToCanvas(Vec2 layerPos) const
        {
            return camera.ToCanvas(layerPos, viewport);
        }

        [[nodiscard]] Vec2 SurfaceToCanvas(Vec2 surfacePos) const
        {
            return camera.ToCanvas(surfacePos + surfaceToLayer, viewport);
        }
    };

    /// Put `node` in the topmost frame whose rect contains `canvasPoint`, unless some frame already
    /// holds it. Recorded into the open transaction (or its own).
    void JoinFrameAt(State& state, GraphNodeId node, Vec2 canvasPoint);

    /// The end of a node drag: with `alt`, dragged nodes whose centre left a frame leave it and a
    /// frameless node lying fully inside a frame joins it; then every frame holding a dragged member
    /// grows to enclose all its members plus Theme::FramePadding. Frames never shrink here.
    void SettleFrames(State& state, std::span<const GraphNodeId> dragged, bool alt);

    /// Start of a drag or resize: remember every frame holding one of `nodes` and its current rect.
    void CaptureFrames(State& state, std::span<const GraphNodeId> nodes);
    /// Every move of that gesture: each captured frame becomes its captured rect grown to enclose its
    /// members plus Theme::FramePadding (and the header on top) - or, with `alt`, just its captured rect.
    /// Recorded into the open transaction, so the gesture stays one undo step.
    void FollowFrames(State& state, bool alt);

    namespace detail
    {
        [[nodiscard]] inline State& Access(Controller& controller) { return *controller.m_state; }
        [[nodiscard]] inline const State& Access(const Controller& controller)
        {
            return *controller.m_state;
        }
    } // namespace detail
} // namespace lumora::nodegraph
