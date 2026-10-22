#pragma once

#include <lumora/Input/Event.h>
#include <lumora/Widgets/ContextMenu.h>

#include "Graph/State.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace lumora::nodegraph
{
    /// The tool state machine; positions are LAYER space (convert with State::surfaceToLayer).
    /// Pan is Alt/Space+drag or `middleButton`; the dispatcher drops middle MouseDown, so a widget never gets it.
    void OnPress(Controller& controller, Vec2 layerPos, KeyMods mods, bool middleButton = false);
    void OnMove(Controller& controller, Vec2 layerPos, Vec2 layerDelta, KeyMods mods);
    void OnRelease(Controller& controller, Vec2 layerPos, KeyMods mods);
    /// Wheel; zoom anchors on the last OnMove position because onScroll carries none.
    void OnScroll(Controller& controller, float notches);
    /// True when the key was consumed; return it from onKeyDown so unbound keys still get focus traversal.
    bool OnKey(Controller& controller, KeyInfo key);
    /// Only Space matters - it latches the pan modifier.
    void OnKeyUp(Controller& controller, KeyInfo key);
    void OnDoubleClick(Controller& controller, Vec2 layerPos);

    /// The right-click menu: add node (registry-driven), plus the selection verbs. With
    /// ViewCfg::searchPalette a right-click on empty canvas opens the palette and returns nothing.
    [[nodiscard]] std::vector<ContextMenuItem> BuildMenu(Controller& controller, const ViewCfg& cfg,
                                                         Vec2 layerPos);

    bool OnText(Controller& controller, std::string_view text);

    /// An external drag over the canvas: ViewCfg::dropAccepts at the last known pointer.
    [[nodiscard]] bool DropAccepts(Controller& controller, const DragPayload& payload);
    /// Hand a drop at `layerPos` to ViewCfg::onDrop when nothing on the canvas is under it.
    void OnDrop(Controller& controller, const DragPayload& payload, Vec2 layerPos);

    /// The palette, filtered to types that can take a link from `from` when `from.node` is valid.
    void OpenPaletteFor(Controller& controller, Vec2 canvasPos, PinRef from);

    inline constexpr float kPopupWidth = 260.f;
    inline constexpr float kPopupRowHeight = 22.f;
    inline constexpr float kPopupTextSize = 12.f;
    inline constexpr int   kPopupVisibleRows = 14;
    inline constexpr float kCascadeWidth = 200.f;
    inline constexpr int   kCascadeRows = 16;

    /// Layer-space popup rect: a query line over the visible rows, kept inside the viewport.
    [[nodiscard]] Rect PopupRect(const State& state);
    [[nodiscard]] int PopupRowAt(const State& state, Vec2 layerPos);

    /// Group popup items by their `/`-separated category paths. Segments are trimmed and empty ones
    /// skipped; categories and items keep first-appearance order; uncategorised items sit at the root.
    [[nodiscard]] PaletteTree BuildCategoryTree(std::span<const PopupItem> items);

    [[nodiscard]] const PaletteTree& CascadeNode(const Popup& popup, int column);
    [[nodiscard]] int CascadeEntries(const PaletteTree& node);
    /// Layer-space rect of one open cascade column. Column 0 carries the query line on top.
    [[nodiscard]] Rect CascadeColumnRect(const State& state, int column);

    struct CascadeHit
    {
        int  column = -1;
        /// Entry index in that column, or -1 over its query line or padding.
        int  entry = -1;
    };

    /// The cascade column and entry under `layerPos`; column -1 when outside every column.
    [[nodiscard]] CascadeHit CascadeAt(const State& state, Vec2 layerPos);

    /// Close the inline editor and hand State::editValue to onCommit. A no-op when none is open, so
    /// the blur that follows an Enter cannot commit twice.
    void CommitText(State& state);
    void CancelText(State& state);

    enum class FramePart : std::uint8_t { None, Header, Corner };

    struct FrameHit
    {
        FramePart part = FramePart::None;
        FrameId   frame{};
    };

    /// Frame header or resize corner under `canvasPos`, topmost first. Frame bodies never hit.
    [[nodiscard]] FrameHit ProbeFrame(const State& state, Vec2 canvasPos);
} // namespace lumora::nodegraph
