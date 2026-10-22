#include <lumora/MountedApp.h>

#include <lumora/Input/Event.h>
#include <lumora/Paint/ImageProvider.h>
#include <lumora/Reactive/Context.h>
#include <lumora/Reactive/Effect.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Tracking.h>
#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/Layouts.h>
#include <lumora/Widgets/MenuBar.h>

#include "Input/Dispatch.h"
#include "Input/FocusTraversal.h"
#include "Input/Interaction.h"
#include "Layout/AspectRatio.h"
#include "Layout/LayoutBridge.h"
#include "Node/NodeArena.h"
#include "Reactive/ReactiveGraph.h"
#include "Runtime/AppCtx.h"
#include "Runtime/ChildOrder.h"
#include "Runtime/ContextMenuHost.h"
#include "Runtime/ContextMenuOverlay.h"
#include "Runtime/DebugOverlay.h"
#include "Runtime/DragHost.h"
#include "Runtime/FocusRing.h"
#include "Runtime/Instantiate.h"
#include "Runtime/MenuBarView.h"
#include "Runtime/OverlayRoot.h"
#include "Runtime/Paint.h"
#include "Style/StyleEngine.h"
#include "Widgets/VirtualListRuntime.h"

#include <optional>
#include <string>
#include <utility>

namespace lumora
{
    struct MountedApp::Impl
    {
        Ref<ReactiveGraph> graph = MakeRef<ReactiveGraph>();
        NodeArena arena;
        LayoutTree layout;
        style::StyleEngine styleEngine;         // declared before owner so it outlives the scope's effects
        std::optional<ContextMenuHost> menu;    // ditto
        std::optional<OverlayRoot> overlay;     // ditto
        std::optional<MenuBarController> menuBar; // ditto
        std::optional<DragHost> drag;           // ditto
        std::optional<Owner> owner;
        AppCtx ctx;
        NodeId root;
        float appliedSurfaceW = 0.f;
        float appliedSurfaceH = 0.f;
        Elem blueprint;
        InteractionState interaction;
        // Double-buffered: a frame starts at the host's first Dispatch, not Tick (dispatch -> tick -> layout -> paint). `live` accumulates;
        // Paint publishes it into `published` and clears it, so Counters() always describes one whole completed frame.
        FrameCounters live;
        FrameCounters published;

        // Retained recording plus the frame-wide inputs not expressed as node dirt, which must be compared instead: window size, glyph atlas size,
        // DPI, focus ring and debug overlay (drawn outside the retained tree), and the cursor for immediate-mode Painters.
        PaintCache    paint;
        PaintReuse    reuse = PaintReuse::Subtree;
        bool          forceRecord = true;
        Vec2          paintedSurface{};
        Vec2          paintedAtlas[2]{};
        std::uint64_t paintedFontGeneration = 0;
        float         paintedContentScale = 0.f;
        Vec2          paintedPointer{};
        NodeId        paintedFocus{};
        bool          paintedFocusVisible = false;

        /// Whether anything outside the dirty bits invalidates the retained recording. `commit` latches `forceRecord` and re-arms the tracked
        /// inputs; without it the answer is a pure peek (used for the canvas-wide skip before any app has drawn).
        [[nodiscard]] bool NeedsFullRecord(Vec2 surface, bool commit)
        {
            bool full = forceRecord || !paint.primed;
            if (commit)
            {
                forceRecord = false;
            }

            const auto moved = [&full, commit](Vec2& stored, const Vec2 now)
            {
                if (stored.x != now.x || stored.y != now.y)
                {
                    if (commit)
                    {
                        stored = now;
                    }
                    full = true;
                }
            };
            moved(paintedSurface, surface);
            if (ctx.measurer)
            {
                // Atlas growth renormalises cached glyph rects, so retained glyph quads would sample the wrong texels.
                moved(paintedAtlas[0], ctx.measurer->AtlasSize(AtlasKind::Coverage));
                moved(paintedAtlas[1], ctx.measurer->AtlasSize(AtlasKind::Color));
                if (const std::uint64_t generation = ctx.measurer->Generation();
                    generation != paintedFontGeneration)
                {
                    if (commit)
                    {
                        paintedFontGeneration = generation;
                    }
                    full = true;
                }
            }
            if (ctx.contentScale != paintedContentScale)
            {
                if (commit)
                {
                    paintedContentScale = ctx.contentScale;
                }
                full = true;
            }
            // The focus ring is one rect painted after the tree, so no node owns its state.
            if (interaction.focused.id != paintedFocus || interaction.focusVisible != paintedFocusVisible)
            {
                if (commit)
                {
                    paintedFocus = interaction.focused.id;
                    paintedFocusVisible = interaction.focusVisible;
                }
                full = true;
            }
            // Only when the app has a Painter: a cursor-reading custom paint cannot announce a cursor move, but repainting
            // on every move would otherwise waste most of the win.
            if (ctx.hasImmediatePainters)
            {
                moved(paintedPointer, ctx.pointerPos);
            }
            return full;
        }

        [[nodiscard]] bool TreeWantsRepaint() const
        {
            const LiveNode* rootNode = const_cast<NodeArena&>(arena).TryResolve(root);
            return rootNode != nullptr && rootNode->IsSubtreeDirty(Dirty::Repaint);
        }

        void PublishFrame()
        {
            published = live;
            live.Reset();
        }

        Impl(Elem elem, const TextMeasurer* measurer, AssetLoader assets, ImageProvider* images)
            : ctx{arena, *graph, &layout, measurer}
        {
            ctx.assets = assets ? assets : &DefaultFileLoad;
            ctx.images = images;
            ctx.style  = &styleEngine; // seed before Instantiate so styled widgets wire their resolver
            // One counter block for the whole app, reached through AppCtx and the Measure/Paint contexts.
            ctx.counters = &live;
            graph->SetCounters(&live);
            styleEngine.SetCounters(&live);
            GraphScope ambient(*graph);
            owner.emplace();
            // One animation driver for the whole app (nodes with an active transition or @keyframes run); registered once, not per styled container.
            styleEngine.AttachDriver(*graph, *owner, arena);
            menu.emplace();
            overlay.emplace();
            menuBar.emplace();
            drag.emplace();
            ctx.overlay          = &*overlay;
            interaction.menu     = &*menu;
            interaction.overlays = &*overlay;
            interaction.drag     = &*drag;
            interaction.style    = &styleEngine;

            // Publish the menubar controller to the user tree (Use(MenuBarCtx)).
            owner->ProvideContext(
                MakeRef<detail::ContextValue<MenuBarController*>>(MenuBarCtx.Id(), &*menuBar));

            // Shell: a Column of [menubar strip, content box] so the bar reserves a fixed strip, then one full-surface layer per Layer
            // (ascending paint order) and the context-menu overlay on top. The content box keeps the root's original full-surface containing block.
            blueprint = Stack(StackCfg{.layout = {.width = Expand, .height = Expand}},
                              Column(ColumnCfg{.layout = {.width = Expand, .height = Expand}},
                                     BuildMenuBarView(&*menuBar),
                                     Box(BoxCfg{.layout = {.width = Expand, .height = Expand}})), // Content (filled below)
                              BuildOverlayLayer(),  // Drawer
                              BuildOverlayLayer(),  // Modal
                              BuildOverlayLayer(),  // Popover
                              BuildOverlayLayer(),  // Menu
                              BuildOverlayLayer(),  // Tooltip
                              BuildOverlayLayer(),  // Toast
                              BuildOverlayLayer(),  // Drag
                              BuildContextMenuOverlay(&*menu),
                              BuildDragOverlay(&*drag));
            if (blueprint.RawNode())
            {
                root = Instantiate(ctx, *blueprint.RawNode(), NodeId{}, *owner);

                // Record the layer nodes BEFORE mounting user content so portals the content declares (e.g. a Toaster) find their layer at build time.
                // The content box is the Column's second child (after the menubar view).
                static constexpr Layer kLayers[] = {Layer::Drawer,  Layer::Modal,   Layer::Popover,
                                                    Layer::Menu,     Layer::Tooltip, Layer::Toast,
                                                    Layer::Drag};
                const NodeId column     = arena.Resolve(root).firstChild;
                const NodeId menuBarNode = arena.Resolve(column).firstChild;
                const NodeId contentBox = arena.Resolve(menuBarNode).nextSibling;
                NodeId c = column;
                for (const Layer ly : kLayers)
                {
                    c = arena.Resolve(c).nextSibling;
                    if (c.Valid())
                    {
                        overlay->SetLayerNode(ly, c);
                    }
                }

                // Mount the user root into the content box.
                if (contentBox.Valid() && elem.RawNode())
                {
                    const NodeId userRoot = Instantiate(ctx, *elem.RawNode(), contentBox, *owner);
                    AppendChild(arena, contentBox, userRoot);
                }
            }
        }

        ~Impl()
        {
            GraphScope ambient(*graph);
            owner.reset();
        }
    };

    MountedApp::MountedApp(Elem root, const TextMeasurer* measurer, AssetLoader assets,
                           ImageProvider* images)
        : m_impl(std::make_unique<Impl>(std::move(root), measurer, assets, images))
    {
    }

    MountedApp::~MountedApp() = default;

    void MountedApp::SetInteractionEnv(const TextMeasurer* fonts, Clipboard* clip)
    {
        m_impl->interaction.fonts = fonts;
        m_impl->interaction.clip = clip;
    }

    void MountedApp::SetContentScale(float scale) { m_impl->ctx.contentScale = scale > 0.f ? scale : 1.f; }

    void MountedApp::SetStylesheetSource(std::string_view css)
    {
        GraphScope ambient(*m_impl->graph);
        m_impl->styleEngine.LoadSource(css);
        m_impl->forceRecord = true; // a whole-sheet swap can move any node's visual
    }

    MenuBarController& MountedApp::MenuBar() { return *m_impl->menuBar; }

    void MountedApp::SetDragPreview(std::function<Elem(const DragPayload&)> builder)
    {
        m_impl->drag->SetPreview(std::move(builder));
    }

    bool MountedApp::Dispatch(const Event& event)
    {
        if (!m_impl->root.Valid())
        {
            return false;
        }
        // Escape closes an open menu first, then the topmost Escape-eligible overlay, whichever node holds focus.
        if (event.Type == EventType::KeyDown && event.KeyCode == Key::Escape)
        {
            // A drag outranks both: abandoning it must not also close the menu or dialog it was dragged over.
            if (m_impl->drag && m_impl->drag->CurrentPhase() != DragHost::Phase::Idle)
            {
                GraphScope ambient(*m_impl->graph);
                DeferStructuralScope defer(*m_impl->graph);
                Batch([this] { m_impl->drag->Clear(m_impl->arena, m_impl->interaction); });
                return true;
            }
            if (m_impl->menu && m_impl->menu->IsOpen())
            {
                m_impl->menu->RequestClose();
                return true;
            }
            if (m_impl->overlay && m_impl->overlay->DismissTopmostEscapable())
            {
                return true; // onDismiss runs at Flush (after Tick)
            }
        }
        GraphScope ambient(*m_impl->graph);
        DeferStructuralScope defer(*m_impl->graph);
        Event ev = event;
        // Batched: one handler writing several signals would otherwise drive a full effect flush each. Structural edits still defer to the next Tick.
        Batch([&] { DispatchEvent(m_impl->arena, m_impl->root, m_impl->interaction, ev); });
        return ev.Consumed;
    }

    bool MountedApp::DropExternal(const DragPayload& payload, Vec2 logicalPos)
    {
        if (!m_impl->root.Valid())
        {
            return false;
        }
        GraphScope           ambient(*m_impl->graph);
        DeferStructuralScope defer(*m_impl->graph);
        bool                 handled = false;
        Batch([&]
        {
            handled = DispatchDropExternal(m_impl->arena, m_impl->root, m_impl->interaction, payload, logicalPos);
        });
        return handled;
    }

    void MountedApp::Tick(float deltaSeconds)
    {
        // Dispatcher clock; advanced here (not from an OnFrame) because dispatch runs between ticks and needs the current value.
        m_impl->interaction.clock += deltaSeconds;

        GraphScope ambient(*m_impl->graph);
        // One batch across the pump: the drains below run deferred onDismiss handlers whose writes would otherwise each trigger a full flush.
        Batch([this, deltaSeconds]
        {
            m_impl->graph->Tick(deltaSeconds);
            // Drain deferred menu + overlay dismissals at a safe point, never inside dispatch.
            m_impl->menu->Drain();
            m_impl->overlay->Flush();
        });
    }

    const FrameCounters& MountedApp::Counters() const noexcept { return m_impl->published; }

    void MountedApp::RunScoped(const std::function<void()>& fn)
    {
        if (!fn)
        {
            return;
        }
        GraphScope ambient(*m_impl->graph);
        Owner::Scope owned(*m_impl->owner); // so an Effect/OnFrame created here has the app's lifetime
        DeferStructuralScope defer(*m_impl->graph);
        Batch([&fn] { fn(); });
    }

    void MountedApp::Layout(float width, float height)
    {
        m_impl->appliedSurfaceW = width;
        m_impl->appliedSurfaceH = height;
        // Publish the viewport before the solve so @media / vw / vh re-resolve this same frame.
        {
            GraphScope ambient(*m_impl->graph);
            m_impl->styleEngine.SetViewport({width, height});
        }
        m_impl->menu->SetSurface({width, height});
        m_impl->overlay->SetSurface({width, height});
        lumora::Layout(m_impl->ctx, m_impl->root, width, height);
        // Passes reading solved rects: VirtualList windows, anchored positioners, anchor mirror. One GraphScope + batch + structural deferral,
        // so a nested Show/Each reconcile cannot free a slot being walked and N moved anchors cost one effect flush.
        {
            GraphScope ambient(*m_impl->graph);
            DeferStructuralScope defer(*m_impl->graph);
            Batch([&]
            {
                if (RunVirtualLists(m_impl->ctx))
                {
                    lumora::Layout(m_impl->ctx, m_impl->root, width, height);
                }
                // Before positioners: an aspect-ratio node's height is what a surface anchored below it is placed against.
                if (RunAspectRatios(m_impl->ctx))
                {
                    lumora::Layout(m_impl->ctx, m_impl->root, width, height);
                }
                if (m_impl->overlay->RunPositioners(m_impl->arena))
                {
                    lumora::Layout(m_impl->ctx, m_impl->root, width, height);
                }
                m_impl->arena.RefreshAnchorRects();
            });
        }
        // One-shot autofocus: after the first solve (rects exist, so scroll-into-view and ring placement are right), if nothing in the scope is focused.
        if (m_impl->ctx.pendingAutofocus.Valid() && (m_impl->ctx.pendingAutofocusList.empty() ||
                                                     m_impl->ctx.pendingAutofocusList.back() != m_impl->ctx.pendingAutofocus))
        {
            m_impl->ctx.pendingAutofocusList.push_back(m_impl->ctx.pendingAutofocus);
        }
        for (const NodeId id : m_impl->ctx.pendingAutofocusList)
        {
            if (!m_impl->arena.TryResolve(id))
            {
                continue;
            }
            const NodeId scope = detail::FindEnclosingScope(m_impl->arena, id);
            const NodeId curFocus = m_impl->interaction.focused.Resolve(m_impl->arena);
            const bool hasFocusInScope = scope.Valid() && curFocus.Valid() &&
                                         detail::IsInScope(m_impl->arena, curFocus, scope);
            if (!hasFocusInScope)
            {
                GraphScope ambient(*m_impl->graph);
                DeferStructuralScope defer(*m_impl->graph);
                RequestFocus(m_impl->arena, m_impl->interaction, NodeRef{m_impl->arena, id},
                             /*viaKeyboard=*/true);
            }
        }
        m_impl->ctx.pendingAutofocusList.clear();
        m_impl->ctx.pendingAutofocus = NodeId{};
    }

    void MountedApp::SetPaintReuse(const PaintReuse mode)
    {
        if (m_impl->reuse != mode)
        {
            m_impl->reuse = mode;
            m_impl->forceRecord = true; // never trust a recording made under a different policy
        }
    }

    PaintReuse MountedApp::GetPaintReuse() const noexcept { return m_impl->reuse; }

    void MountedApp::Paint(Canvas& canvas)
    {
        Impl& app = *m_impl;
        app.ctx.pointerPos = app.interaction.pointerPos;

        if (app.reuse == PaintReuse::Off)
        {
            lumora::Paint(app.ctx, app.root, canvas);
            lumora::PaintFocusRing(app.ctx, app.interaction, canvas);
            app.PublishFrame();
            return;
        }

        const bool full = app.NeedsFullRecord(Vec2{app.appliedSurfaceW, app.appliedSurfaceH},
                                              /*commit=*/true);
        if (!full && !app.TreeWantsRepaint())
        {
            // Nothing wants redrawing: replay the retained recording without walking a node. (Skipping the canvas's vertex batches
            // is the runtime's call; see MountedApp::WantsRepaint.)
            ++app.live.paintFrameReused;
            app.live.paintRecordedBytes = app.paint.current.Size();
            app.paint.current.Replay(canvas, &app.live);
            lumora::PaintFocusRing(app.ctx, app.interaction, canvas);
            app.PublishFrame();
            return;
        }

        lumora::Record(app.ctx, app.root, app.paint,
                               app.reuse == PaintReuse::Subtree && !full);
        app.paint.current.Replay(canvas, &app.live);
        // Outside the recording: the ring belongs to no node, and re-recording one rect beats giving the tree a dependency on interaction state.
        lumora::PaintFocusRing(app.ctx, app.interaction, canvas);

        // Frame boundary: publish this frame's counters and start a fresh block.
        app.PublishFrame();
    }

    bool MountedApp::WantsRepaint()
    {
        if (m_impl->reuse == PaintReuse::Off)
        {
            return true;
        }
        // Mirror the cursor first: this runs BEFORE any app paints, and a moved cursor is an input NeedsFullRecord compares.
        m_impl->ctx.pointerPos = m_impl->interaction.pointerPos;
        return m_impl->NeedsFullRecord(Vec2{m_impl->appliedSurfaceW, m_impl->appliedSurfaceH},
                                       /*commit=*/false) ||
            m_impl->TreeWantsRepaint();
    }

    void MountedApp::NoteFrameReused()
    {
        ++m_impl->live.paintFrameReused;
        ++m_impl->live.paintCanvasReused;
        m_impl->PublishFrame();
    }

    void MountedApp::FocusNode(const Anchor& anchor)
    {
        const Ref<detail::AnchorState>& st = anchor.State();
        if (!st || !st->id.Valid() || st->isPointer)
        {
            return;
        }
        NodeArena& arena = m_impl->arena;
        if (!arena.TryResolve(st->id) || arena.GenerationOf(st->id) != st->gen)
        {
            return; // unbound or stale handle
        }
        GraphScope ambient(*m_impl->graph);
        DeferStructuralScope defer(*m_impl->graph);
        NodeRef ref;
        ref.id  = st->id;
        ref.gen = st->gen;
        RequestFocus(arena, m_impl->interaction, ref, /*viaKeyboard=*/true);
    }

    void MountedApp::ClearFocus()
    {
        GraphScope ambient(*m_impl->graph);
        DeferStructuralScope defer(*m_impl->graph);
        RequestFocus(m_impl->arena, m_impl->interaction, NodeRef{}, /*viaKeyboard=*/false);
    }

    bool MountedApp::MoveFocus(Direction dir)
    {
        if (!m_impl->root.Valid())
        {
            return false;
        }
        EventContext base{};
        base.arena = &m_impl->arena;
        base.state = &m_impl->interaction;
        base.fonts = m_impl->interaction.fonts;
        base.clip  = m_impl->interaction.clip;
        GraphScope ambient(*m_impl->graph);
        DeferStructuralScope defer(*m_impl->graph);
        return FocusTraversal::MoveDirectional(base, m_impl->interaction, m_impl->root, dir);
    }

    void MountedApp::FocusNext()
    {
        if (!m_impl->root.Valid())
        {
            return;
        }
        EventContext base{};
        base.arena = &m_impl->arena;
        base.state = &m_impl->interaction;
        base.fonts = m_impl->interaction.fonts;
        base.clip  = m_impl->interaction.clip;
        GraphScope ambient(*m_impl->graph);
        DeferStructuralScope defer(*m_impl->graph);
        FocusTraversal::Move(base, m_impl->interaction, m_impl->root, FocusDir::Forward);
    }

    void MountedApp::FocusPrev()
    {
        if (!m_impl->root.Valid())
        {
            return;
        }
        EventContext base{};
        base.arena = &m_impl->arena;
        base.state = &m_impl->interaction;
        base.fonts = m_impl->interaction.fonts;
        base.clip  = m_impl->interaction.clip;
        GraphScope ambient(*m_impl->graph);
        DeferStructuralScope defer(*m_impl->graph);
        FocusTraversal::Move(base, m_impl->interaction, m_impl->root, FocusDir::Backward);
    }

    bool MountedApp::ActivateFocused()
    {
        if (!m_impl->root.Valid())
        {
            return false;
        }
        const NodeId focusedId = m_impl->interaction.focused.Resolve(m_impl->arena);
        if (!focusedId.Valid())
        {
            return false;
        }
        InteractState* on = detail::InteractOf(m_impl->arena, m_impl->arena.Resolve(focusedId));
        if (!on || on->disabled)
        {
            return false;
        }
        GraphScope ambient(*m_impl->graph);
        DeferStructuralScope defer(*m_impl->graph);
        if (on->onClick)
        {
            on->onClick();
            return true;
        }
        if (on->onKeyDown)
        {
            return on->onKeyDown({Key::Enter, KeyMods::None});
        }
        return false;
    }

    void MountedApp::PaintDebugOverlay(Canvas& canvas, Vec2 surfaceSize, DebugOverlayFlags flags)
    {
#ifndef NDEBUG
        lumora::PaintDebugOverlay(m_impl->ctx, m_impl->root, m_impl->interaction, canvas,
                                          surfaceSize, flags);
        // The overlay draws straight onto the canvas after Paint, so force the next frame to record: it must be rebuilt while on, and the frame that turns it off must lose it.
        m_impl->forceRecord = true;
#else
        (void)canvas;
        (void)surfaceSize;
        (void)flags;
#endif
    }
} // namespace lumora
