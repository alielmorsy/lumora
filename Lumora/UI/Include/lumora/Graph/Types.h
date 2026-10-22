#pragma once

#include <lumora/Api.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Paint/DrawTypes.h>
#include <lumora/Widgets/Props.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace lumora
{
    class Paint2D;
}

namespace lumora::nodegraph
{
    struct Theme;
    class Controller;

    /// A node type's identity: a 64-bit hash of its source string, with the string retained by the
    /// Registry so serialization round-trips readable names. Value 0 is the "no type" sentinel.
    struct LUMORA_API NodeTypeId
    {
        std::uint64_t value = 0;

        constexpr NodeTypeId() = default;
        explicit NodeTypeId(std::string_view source);

        [[nodiscard]] constexpr bool Valid() const noexcept { return value != 0; }
        [[nodiscard]] constexpr bool operator==(const NodeTypeId&) const noexcept = default;
    };

    /// A pin's data type, hashed from a domain-supplied string ("float3", "Texture2D", "exec"). The
    /// graph compares these for equality and never interprets them.
    struct LUMORA_API PinTypeId
    {
        std::uint64_t value = 0;

        constexpr PinTypeId() = default;
        explicit PinTypeId(std::string_view source);

        [[nodiscard]] constexpr bool Valid() const noexcept { return value != 0; }
        [[nodiscard]] constexpr bool operator==(const PinTypeId&) const noexcept = default;
    };

    /// Generational node handle so a stale link reference is detectable.
    struct GraphNodeId
    {
        std::uint32_t index = 0;
        std::uint32_t gen   = 0;

        [[nodiscard]] constexpr bool Valid() const noexcept { return gen != 0; }
        [[nodiscard]] constexpr bool operator==(const GraphNodeId&) const noexcept = default;
    };

    struct LinkId
    {
        std::uint32_t index = 0;
        std::uint32_t gen   = 0;

        [[nodiscard]] constexpr bool Valid() const noexcept { return gen != 0; }
        [[nodiscard]] constexpr bool operator==(const LinkId&) const noexcept = default;
    };

    /// A frame handle. Frame ids are never reused within a Model, so undo can re-insert a frame under
    /// the id later edits still refer to.
    struct FrameId
    {
        std::uint32_t value = 0;

        [[nodiscard]] constexpr bool Valid() const noexcept { return value != 0; }
        [[nodiscard]] constexpr bool operator==(const FrameId&) const noexcept = default;
    };

    /// A corner marker on a node header; `text` shows as a tooltip while the pointer is over it.
    struct Badge
    {
        Color       color = Color::Transparent;
        std::string text;
    };

    /// "no pin" for a PinRowCell slot that carries only one side.
    inline constexpr std::uint16_t NoPin = 0xFFFF;

    enum class PinDir : std::uint8_t { Input, Output };

    /// Pin glyph. A domain distinguishes exec flow from data by shape rather than colour alone.
    enum class PinShape : std::uint8_t { Circle, Diamond, Arrow, Square };

    /// One endpoint: which node, which pin slot within its type, and which side.
    struct PinRef
    {
        GraphNodeId   node{};
        std::uint16_t slot = 0;
        PinDir        dir  = PinDir::Input;

        [[nodiscard]] constexpr bool operator==(const PinRef&) const noexcept = default;
    };

    struct PinDesc
    {
        std::string name;
        PinTypeId   type{};
        PinDir      dir   = PinDir::Input;
        /// Accept more than one link. Inputs default to single-connect (a new link replaces the old);
        /// outputs are normally set multi by the domain.
        bool        multi = false;
        PinShape    shape = PinShape::Circle;
    };

    /// What a CustomCell is handed to draw itself. `rect` is layer-space with the camera already
    /// applied, and `zoom` is the factor every length the cell invents must be multiplied by.
    struct CellPaintCtx
    {
        Paint2D&     painter;
        Rect         rect{};
        float        zoom = 1.f;
        GraphNodeId  node{};
        bool         hovered = false;
        const Theme* theme = nullptr;
        /// The cell sits on a compact node: labels are hidden and a value field takes the whole row.
        bool         compact = false;
    };

    /// Press opens a gesture; a cell that consumes it receives Drag and Release until the button goes
    /// up. DoubleClick arrives on its own after the second press of a pair.
    enum class CellEvent : std::uint8_t { Press, Drag, Release, DoubleClick };

    /// What a CustomCell is handed on a pointer event. Canvas-space, because a cell reasons about its
    /// own geometry, not the screen. `pointer.delta` is in layer pixels, so a scrub reads the same
    /// speed at every zoom.
    struct CellInputCtx
    {
        GraphNodeId node{};
        Vec2        canvasPos{};
        PointerInfo pointer{};
        Rect        rect{};
        CellEvent   event = CellEvent::Press;
        float       zoom = 1.f;
        /// The graph the cell lives in, for EditText / OpenChoices.
        Controller* controller = nullptr;
        /// The pointer in the surface space Lumora popovers are positioned in.
        Vec2        surfacePos{};
        /// Mirrors CellPaintCtx::compact, so a field hit-tests the same value rect it painted.
        bool        compact = false;
    };

    struct Cell;

    /// A row carrying up to one input pin on the left and one output pin on the right. The slots index
    /// NodeTypeDesc::pins. Each side paints its PinDesc::name next to its pin, dimmed while unconnected.
    struct PinRowCell
    {
        std::uint16_t input  = NoPin;
        std::uint16_t output = NoPin;
    };

    struct LabelCell
    {
        std::string text;
        bool        bold = false;
    };

    /// A label plus a live value pulled from the domain each paint.
    struct TextCell
    {
        std::string                             label;
        std::function<std::string(GraphNodeId)> value;
    };

    /// A preview. The handle is minted by the host (a render target, a baked thumbnail); the graph
    /// never interprets it. An invalid handle draws nothing.
    struct ImageCell
    {
        std::function<ImageHandle(GraphNodeId)> image;
        float                                   aspect = 1.f;
    };

    struct SpacerCell
    {
        float height = 4.f;
    };

    /// The escape hatch: a caller that wants something the vocabulary lacks draws it and handles its
    /// own input, inside a clipped rect, at the right zoom. `input` returns true to consume.
    struct CustomCell
    {
        float                              height = 24.f;
        std::function<void(CellPaintCtx&)> paint;
        std::function<bool(CellInputCtx&)> input;
        /// A pin drawn on this row, like PinRowCell's. While an input pin here has a link the graph
        /// paints only `label` (the pin's name when `label` is empty) and stops routing input to the
        /// cell - a value field hides behind its wire.
        std::uint16_t                      pin = NoPin;
        std::string                        label;
        /// Shares the extra height of a node resized taller than its content, split equally among the
        /// stretch cells. Not inside a GroupCell with a fixed or drag-resized height, nor a closed one.
        /// With no stretch cell the extra height is empty space at the bottom of the body.
        bool                               stretch = false;
    };

    /// A collapsible, optionally drag-resizable run of cells under a caret (a node preview is a GroupCell holding an ImageCell).
    struct GroupCell
    {
        std::string       title;
        bool              defaultOpen = true;
        bool              resizable   = false;
        /// Authored height in canvas units; 0 sizes from the children.
        float             height      = 0.f;
        std::vector<Cell> children;
    };

    struct Cell : std::variant<PinRowCell, LabelCell, TextCell, ImageCell, SpacerCell, CustomCell, GroupCell>
    {
        using variant::variant;
    };

    /// A node type: what its instances look like and what pins they carry. Cells live here rather than
    /// on the instance, so they may hold std::function freely and never need serializing.
    struct NodeTypeDesc
    {
        std::string          title;
        /// Header tint; Transparent takes Theme::NodeHeader.
        Color                accent = Color::Transparent;
        std::vector<PinDesc> pins;
        std::vector<Cell>    body;
        float                minWidth    = 150.f;
        bool                 collapsible = true;
        /// Search palette grouping and extra match terms. `category` is a `/`-separated path
        /// ("Math/Trigonometry"); the palette and the "Add node" menu cascade through it.
        std::string              category;
        std::vector<std::string> keywords;
        /// No header bar: a rounded body with `symbol` ("+", "sin", "lerp") centred large over it, pin
        /// labels hidden and value fields narrowed. Defaults to Theme::CompactWidth wide and never
        /// collapses. Node::title, when set, replaces the symbol.
        bool                     compact = false;
        std::string              symbol;
    };

    /// Maps a node type id to its descriptor, retaining the source string so serialization
    /// round-trips readable names, and registration order so a palette lists types the way the domain
    /// declared them.
    class LUMORA_API Registry
    {
    public:
        /// Register or replace. Replacing keeps the type's original position in All().
        NodeTypeId Register(std::string_view id, NodeTypeDesc desc);

        [[nodiscard]] const NodeTypeDesc* Find(NodeTypeId id) const;
        [[nodiscard]] std::string_view    SourceOf(NodeTypeId id) const;
        [[nodiscard]] std::span<const NodeTypeId> All() const;

        /// Pin descriptor by slot, or null when the type is unknown or the slot out of range - the
        /// single lookup every legality and layout query funnels through.
        [[nodiscard]] const PinDesc* Pin(NodeTypeId type, std::uint16_t slot) const;

    private:
        std::unordered_map<std::uint64_t, NodeTypeDesc> m_types;
        std::unordered_map<std::uint64_t, std::string>  m_sources;
        std::vector<NodeTypeId>                         m_order;
    };

    /// Colours and metrics for the graph chrome. Metrics are canvas units and get multiplied by the
    /// camera's zoom at draw time. Defaults are the neutral dark palette DockTheme uses.
    struct Theme
    {
        Color Background   = Color::Rgba(30, 33, 39, 255);
        Color GridMinor    = Color::Rgba(42, 46, 54, 255);
        Color GridMajor    = Color::Rgba(52, 57, 67, 255);
        Color NodeBg       = Color::Rgba(40, 44, 52, 255);
        Color NodeHeader   = Color::Rgba(48, 53, 63, 255);
        Color NodeBorder   = Color::Rgba(58, 64, 74, 255);
        Color NodeSelected = Color::Rgba(97, 175, 239, 255);
        Color Text         = Color::Rgba(220, 223, 228, 255);
        Color TextDim      = Color::Rgba(130, 137, 150, 255);
        Color PinDefault   = Color::Rgba(150, 158, 172, 255);
        Color LinkDefault  = Color::Rgba(150, 158, 172, 255);
        Color LinkInvalid  = Color::Rgba(224, 108, 117, 255);
        Color BoxSelect    = Color::Rgba(97, 175, 239, 45);

        float NodeRadius     = 6.f;
        float HeaderHeight   = 26.f;
        float CellHeight     = 22.f;
        float CellPadding    = 6.f;
        float PinRadius      = 5.f;
        float PinInset       = 8.f;
        float LinkWidth      = 2.f;
        float TitleTextSize  = 13.f;
        float CellTextSize   = 12.f;
        float GridSpacing    = 24.f;
        float GridMajorEvery = 5.f;
        /// Position quantisation in canvas units; 0 disables snapping.
        float SnapSize       = 0.f;

        Color FrameDefault   = Color::Rgba(97, 175, 239, 255);
        Color FieldBg        = Color::Rgba(26, 28, 33, 255);
        Color FieldHover     = Color::Rgba(34, 37, 44, 255);
        Color PopupBg        = Color::Rgba(32, 33, 38, 245);
        Color PopupHighlight = Color::Rgba(97, 175, 239, 90);
        Color TooltipBg      = Color::Rgba(32, 33, 38, 235);
        Color TooltipText    = Color::Rgba(235, 236, 240, 255);

        float FrameHeaderHeight = 28.f;
        /// Space a frame created around a selection leaves on every side.
        float FramePadding      = 24.f;
        float StripeThickness   = 4.f;
        float BadgeRadius       = 5.f;

        /// Default width of a NodeTypeDesc::compact node.
        float CompactWidth = 96.f;
        /// Thickness of the drag-to-resize strips along a node's right edge (header included, width)
        /// and bottom edge (height).
        float ResizeZone   = 6.f;
    };
}
