#pragma once

#include <lumora/Api.h>
#include <lumora/AssetLoader.h>
#include <lumora/Debug/DebugOverlay.h>
#include <lumora/Debug/FrameCounters.h>
#include <lumora/Elem.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Input/DragDrop.h>
#include <lumora/Paint/PaintReuse.h>
#include <lumora/Widgets/Anchor.h>

#include <functional>
#include <memory>

namespace lumora {

class Canvas;
class DisplayList;
class TextMeasurer;
class Clipboard;
class MenuBarController;
class ImageProvider;
class HostTree;
struct Event;

/// One mounted Lumora tree: owns its reactive graph, node arena, layout tree and root scope. Created by the Lumora
/// runtime, never constructed by application code. Per-app state is behind a pimpl.
class LUMORA_API MountedApp {
public:
    /// `assets` is the file reader handed to the build walk (null = DefaultFileLoad). Must be supplied at construction:
    /// the user root is instantiated in the constructor.
    MountedApp(Elem root, const TextMeasurer* measurer, AssetLoader assets = nullptr,
               ImageProvider* images = nullptr);
    ~MountedApp();

    MountedApp(const MountedApp&)            = delete;
    MountedApp& operator=(const MountedApp&) = delete;

    /// Seed the dispatcher's environment: font seam (caret hit-testing) and clipboard. Called by the runtime after mounting.
    void SetInteractionEnv(const TextMeasurer* fonts, Clipboard* clip);

    /// Route one input event (capture, target, bubble); true if consumed. Run before Tick so handler writes land in the same frame.
    bool Dispatch(const Event& event);

    /// Deliver an external drop at `logicalPos` (surface space) to the innermost widget whose `dropAccepts` takes `payload`.
    bool DropExternal(const DragPayload& payload, Vec2 logicalPos);

    /// Move keyboard focus to the node bound to `anchor` (via `.ref`), showing the focus ring. No-op if unbound, stale or a
    /// pointer anchor. ClearFocus drops focus.
    void FocusNode(const Anchor& anchor);
    void ClearFocus();

    /// Move focus in the given direction; true if it moved.
    bool MoveFocus(Direction dir);

    /// Advance focus to the next stop in the active focus scope (equivalent to Tab).
    void FocusNext();

    /// Move focus to the previous stop in the active focus scope (equivalent to Shift+Tab).
    void FocusPrev();

    /// Activate the focused node (onClick / Enter); true if an action ran.
    bool ActivateFocused();

    /// True while the focused node edits text; a host polls it per frame to show/hide the soft keyboard (Window::SetTextInputActive).
    [[nodiscard]] bool WantsTextInput() const;

    /// Builder for the drag ghost, keyed on the payload the source published; one per app. Unset draws a small default marker.
    void SetDragPreview(std::function<Elem(const DragPayload&)> builder);

    /// Pump the app's reactive graph for one frame: drain deferred edits, fire OnFrame, flush.
    void Tick(float deltaSeconds);

    /// OS DPI scale, mirrored into the paint context so vector widgets pick device-space flatten density. Layout stays logical.
    void SetContentScale(float scale);

    /// Apply a stylesheet from an in-memory CSS string (parse, install, re-resolve). The public entry is Lumora::LoadStylesheet.
    void SetStylesheetSource(std::string_view css);

    /// Solve layout against a surface of `width` x `height` logical pixels.
    void Layout(float width, float height);

    /// Emit the tree's geometry onto `canvas`; the caller brackets the frame. Under the default PaintReuse::Subtree it is
    /// incremental: clean subtrees are republished from last frame's recording and a frame with no dirt is re-presented.
    void Paint(Canvas& canvas);

    /// Same decisions as Paint, but appends this frame's recording (plus the focus ring) to `out` instead of drawing; the
    /// runtime replays the whole frame list later. paintReplayed then counts the records appended.
    void RecordInto(DisplayList& out);

    /// Whether this app would draw anything different from last frame. False means its retained recording is current, so a
    /// host may skip the frame (Canvas::ReuseFrame); only sound when NO app on the canvas has anything to add. Cheap, non-destructive.
    [[nodiscard]] bool WantsRepaint();

    /// Tell this app its frame was re-presented from the canvas's retained geometry, so counters stay accurate. Call instead of Paint.
    void NoteFrameReused();

    /// How much of the previous frame's paint this app may keep (default Subtree). Off bisects a suspected reuse bug: right
    /// under Off but wrong under Subtree means a missing MarkDirty(Dirty::Repaint).
    void SetPaintReuse(PaintReuse mode);
    [[nodiscard]] PaintReuse GetPaintReuse() const noexcept;

    /// Draw the debug inspector overlay over the painted tree (no-op in release). Called right after Paint.
    void PaintDebugOverlay(Canvas& canvas, Vec2 surfaceSize, DebugOverlayFlags flags);

    /// Work done by the last completed frame, counted at each phase's choke points; read after Paint.
    [[nodiscard]] const FrameCounters& Counters() const noexcept;

    /// Run `fn` with this app's graph ambient, root owner current and writes batched. Host code outside the frame methods
    /// needs this to create or write signals (no ambient graph: no subscriptions; no owner: Effect/OnFrame dropped).
    void RunScoped(const std::function<void()>& fn);

    /// Imperative node tree rooted at the app's root node, for script renderers. Created on first call; lives as long as the app.
    [[nodiscard]] HostTree& Host();

    /// The auto-mounted menubar model; the runtime pins its bar to the window top and provides it via MenuBarCtx.
    [[nodiscard]] MenuBarController& MenuBar();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
