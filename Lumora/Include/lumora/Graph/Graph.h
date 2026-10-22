#pragma once

// Graph/Graph.h - domain-agnostic node graph widget. One Painter draws everything in canvas space through a camera
// transform (the only way zoom stays correct on a translate-only element system). The domain supplies node types;
// entry points are Type(), Restore() and AddNode().

#include <lumora/Api.h>
#include <lumora/Ref.h>
#include <lumora/Elem.h>
#include <lumora/Graph/Camera.h>
#include <lumora/Graph/Model.h>
#include <lumora/Graph/Types.h>
#include <lumora/Reactive/Signal.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lumora::nodegraph
{
    struct State;
    struct ViewCfg;

    class Controller;

    namespace detail
    {
        [[nodiscard]] State&       Access(Controller& controller);
        [[nodiscard]] const State& Access(const Controller& controller);
    }

    /// Owns the graph workspace and exposes the pointer's edits. Copies share one State.
    class LUMORA_API Controller
    {
    public:
        Controller();
        ~Controller();
        Controller(const Controller&);
        Controller(Controller&&) noexcept;
        Controller& operator=(const Controller&);
        Controller& operator=(Controller&&) noexcept;

        /// Bumped by every edit, camera move and selection change; feed it to the Painter's `repaint` prop.
        [[nodiscard]] Signal<std::uint64_t>& Revision();

        [[nodiscard]] Registry&    Types();
        [[nodiscard]] Theme&       Visuals();
        [[nodiscard]] LinkPolicy&  Policy();
        [[nodiscard]] Camera&      View();
        [[nodiscard]] const Model& Data() const;

        NodeTypeId Type(std::string_view id, NodeTypeDesc desc);

        // Edits: each opens its own transaction unless one is open.
        GraphNodeId AddNode(NodeTypeId type, Vec2 canvasPos);
        void        RemoveNode(GraphNodeId id);
        void        RemoveSelection();
        /// Connect, replacing whatever occupies a single-connect input. Returns an invalid id when
        /// CanConnect refuses.
        LinkId Connect(PinRef from, PinRef to);
        void   Disconnect(LinkId id);
        void   MoveNodes(std::span<const GraphNodeId> ids, Vec2 canvasDelta);
        void   SetNodeWidth(GraphNodeId id, float width);
        /// Undoable. Width clamps to minWidth (CompactWidth if compact); height to content height, stored 0 at or below it.
        /// A 0 component restores that axis to auto; a collapsed node keeps its height.
        void   SetNodeSize(GraphNodeId id, Vec2 size);
        void   SetCollapsed(GraphNodeId id, bool collapsed);
        void   SetGroupOpen(GraphNodeId id, int group, bool open);
        /// Drag-resize a group. Indices past the stored four are ignored.
        void   SetGroupHeight(GraphNodeId id, int group, float height);
        /// Copy these nodes plus every link whose BOTH endpoints are in the set, offset by `offset`.
        std::vector<GraphNodeId> Duplicate(std::span<const GraphNodeId> ids, Vec2 offset);
        /// Re-key the host payload (Node::userData); recorded for undo/redo. Assign it from onNodeAdded, inside the add's transaction.
        void   SetUserData(GraphNodeId id, std::uint64_t userData);
        /// Override the type's title (or a compact node's symbol); empty restores it. Undoable.
        void   SetNodeTitle(GraphNodeId id, std::string_view title);

        /// Wrap the selection in a frame padded by Theme::FramePadding. Invalid when nothing live is
        /// selected.
        FrameId AddFrame(std::string_view title = "Frame", Color color = Color::Transparent);
        /// A frame over an explicit canvas rect; members are the nodes whose centre falls inside.
        FrameId AddFrame(const Rect& canvasRect, std::string_view title,
                         Color color = Color::Transparent);
        /// Members stay where they are.
        void    RemoveFrame(FrameId id);
        void    SetFrameTitle(FrameId id, std::string_view title);
        void    SetFrameColor(FrameId id, Color color);
        /// Reshape without touching membership; pair with RefreshFrameMembers when the gesture ends.
        void    SetFrameRect(FrameId id, const Rect& canvasRect);
        /// Move the frame and every live member by `canvasDelta`.
        void    MoveFrame(FrameId id, Vec2 canvasDelta);
        /// Members := live nodes whose centre lies inside the frame rect.
        void    RefreshFrameMembers(FrameId id);
        /// Drop `id` from every frame that holds it, as one undoable edit.
        void    RemoveFromFrame(GraphNodeId id);
        [[nodiscard]] std::span<const Frame> Frames() const;

        // Overlays: drawn on the canvas, so they share its focus and zoom.
        /// Open the search palette with the node-to-add landing at `canvasPos`.
        void OpenPalette(Vec2 canvasPos);
        void ClosePopup();
        [[nodiscard]] bool PopupOpen() const;
        /// One-line text editor over `canvasRect` (a real lumora::Input in a Popover-layer portal, camera-scaled).
        /// Enter/focus loss commits, Escape cancels, pan/zoom commits; `initial` starts selected.
        void EditText(const Rect& canvasRect, std::string initial,
                      std::function<void(std::string_view)> onCommit,
                      std::function<void()> onCancel = {});
        [[nodiscard]] bool EditingText() const;
        /// A filterable pick list under `canvasAnchor` (an EnumField dropdown). `onPick` receives the
        /// index into `options`.
        void OpenChoices(const Rect& canvasAnchor, std::vector<std::string> options,
                         std::function<void(int)> onPick);

        [[nodiscard]] std::span<const GraphNodeId> Selection() const;
        void Select(std::span<const GraphNodeId> ids, bool additive = false);
        void ToggleSelected(GraphNodeId id);
        void ClearSelection();
        [[nodiscard]] bool IsSelected(GraphNodeId id) const;
        void SelectLink(LinkId id);
        [[nodiscard]] LinkId SelectedLink() const;

        /// Layer-space rect the graph occupies; written every paint. Call it yourself only when headless.
        void SetViewport(const Rect& layerRect);
        [[nodiscard]] const Rect& Viewport() const;
        /// A canvas rect in the surface space Lumora popovers are positioned in - anchor a picker over
        /// a cell with it. Valid once the widget has painted.
        [[nodiscard]] Rect CanvasToSurface(const Rect& canvasRect) const;
        /// Canvas rect of one body row of `node` (ViewCfg::nodeBody, else the type body). `cellIndex` counts cells in
        /// pre-order (each GroupCell once, then its children). Empty when unknown, collapsed, out of range or in a closed group.
        [[nodiscard]] Rect CellRect(GraphNodeId node, std::uint16_t cellIndex) const;
        /// Frame everything / just the selection. A no-op when there is nothing to frame.
        void FitAll();
        void FitSelection();
        /// Repaint for host state a cell reads (a TextCell value, a CustomCell paint) that the graph
        /// cannot see change.
        void Repaint();

        void Begin(std::string_view label);
        void Commit();
        void Abort();
        void Undo();
        void Redo();
        [[nodiscard]] bool CanUndo() const;
        [[nodiscard]] bool CanRedo() const;

        [[nodiscard]] std::vector<std::byte> Serialize() const;
        /// Replace the graph from `data`. False on a malformed blob; nodes of unregistered types are
        /// dropped along with their links. Clears undo history on success.
        bool Restore(std::span<const std::byte> data);

        /// Host hooks, assigned by the owning panel; the graph only reports.
        std::function<void()>            onSelectionChanged;
        std::function<void(GraphNodeId)> onNodeAdded;
        std::function<void(GraphNodeId)> onNodeRemoved;
        std::function<void(LinkId)>      onLinkAdded;
        std::function<void(LinkId)>      onLinkRemoved;
        /// Double-click a node body: drill in.
        std::function<void(GraphNodeId)> onNodeActivated;
        /// Anything structural changed - for a host dirty flag.
        std::function<void()>            onModelChanged;
        /// A resize drag (right edge, bottom edge or corner grip) was released and committed with a
        /// changed size (not fired on Escape).
        std::function<void(GraphNodeId)> onNodeResized;

    private:
        Ref<State> m_state;

        friend State&       detail::Access(Controller&);
        friend const State& detail::Access(const Controller&);
    };

    /// ViewCfg::nodeStripes layout: Horizontal = columns along the header top; Vertical = rows down its left edge.
    enum class StripeAxis : std::uint8_t { Horizontal, Vertical };

    struct ViewCfg
    {
        Theme theme{};
        bool  showGrid = true;
        bool  showDebug = false;
        /// Right-click on empty canvas: what "Add node" offers. Null offers every registered type.
        /// The search palette uses it as its base list too.
        std::function<std::vector<NodeTypeId>()> paletteFilter;

        /// Per-instance visuals. Each is optional; null or nullopt falls back to the type / theme.
        std::function<std::optional<Color>(GraphNodeId)> nodeAccent;
        std::function<std::vector<Color>(GraphNodeId)>   nodeStripes;
        StripeAxis                                       stripeAxis = StripeAxis::Horizontal;
        std::function<std::optional<Color>(LinkId)>      linkColor;
        /// Canvas units, replacing Theme::LinkWidth for that link.
        std::function<std::optional<float>(LinkId)>      linkWidth;
        std::function<std::optional<Badge>(GraphNodeId)> nodeBadge;

        /// Tab, Space or right-click on empty canvas opens the search palette instead of the "Add node" menu; releasing a
        /// dragged link on empty canvas opens it filtered to compatible types. Space then no longer latches pan.
        bool searchPalette = false;
        /// Narrow what the palette offers. `dragged.node` is invalid unless a link drag opened it.
        std::function<bool(NodeTypeId, PinRef dragged)> paletteAccepts;
        /// Offer "Add frame" for a selection, "Rename / Delete frame" on a frame header and "Remove
        /// from frame" on a framed node in the right-click menu.
        bool frameVerbs = false;

        /// Per-instance body replacing NodeTypeDesc::body. The vector must outlive the next Controller::Repaint(). Pins no
        /// cell names are not drawn, hit-tested or connectable.
        std::function<const std::vector<Cell>*(GraphNodeId)> nodeBody;
        /// Colour-only pin type override: the pin glyph and the default colour of links leaving it.
        /// Legality still uses PinDesc::type and LinkPolicy. Call Controller::Repaint() after changing it.
        std::function<std::optional<PinTypeId>(PinRef)> pinType;
        /// An external drag (Lumora DragPayload) over the canvas. `canvasPos` is the last known pointer.
        std::function<bool(const DragPayload&, Vec2 canvasPos)> dropAccepts;
        /// The drop, delivered only when it lands on empty canvas (no node, pin or link under it).
        std::function<void(const DragPayload&, Vec2 canvasPos)> onDrop;
        /// Per-instance pin label, painted wherever PinDesc::name would be (PinRowCell labels, a linked
        /// CustomCell with an empty `label`). nullopt keeps PinDesc::name. Call Controller::Repaint()
        /// after changing it.
        std::function<std::optional<std::string>(PinRef)> pinLabel;
    };

    /// The widget. Fills its parent; place it inside a dock panel.
    [[nodiscard]] LUMORA_API Elem NodeGraph(Controller& controller, ViewCfg cfg = {});

    // Search: pure, so a host can rank with the palette's rules.
    /// Case-insensitive subsequence score of `query` in `text`; -1 when it is not a subsequence. Word
    /// starts, runs and a match at the very start score higher. An empty query scores 0.
    [[nodiscard]] LUMORA_API int FuzzyScore(std::string_view query, std::string_view text);
    /// Best of title, keywords and category, with the title weighted highest. -1 when none match.
    [[nodiscard]] LUMORA_API int ScoreType(std::string_view query, const NodeTypeDesc& desc);
    /// Types matching `query`, best first; ties keep `types` order.
    [[nodiscard]] LUMORA_API std::vector<NodeTypeId> RankTypes(const Registry& reg,
                                                               std::span<const NodeTypeId> types,
                                                               std::string_view query);
    /// The first pin slot on a node of `type` that LinkPolicy lets `dragged` connect to (either
    /// direction), or NoPin.
    [[nodiscard]] LUMORA_API std::uint16_t FirstCompatiblePin(const Model& model, const Registry& reg,
                                                              const LinkPolicy& policy, PinRef dragged,
                                                              NodeTypeId type);
    /// The subset of `types` with at least one pin compatible with `dragged`, in order.
    [[nodiscard]] LUMORA_API std::vector<NodeTypeId> CompatibleTypes(const Model& model,
                                                                     const Registry& reg,
                                                                     const LinkPolicy& policy,
                                                                     PinRef dragged,
                                                                     std::span<const NodeTypeId> types);
}
