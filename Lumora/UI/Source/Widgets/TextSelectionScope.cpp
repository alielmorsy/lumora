#include <lumora/Widgets/TextSelectionScope.h>

#include "Input/Focus.h"
#include "Input/Interaction.h"
#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Runtime/AppCtx.h"
#include "Runtime/ChildOrder.h"
#include "Runtime/ContextMenuHost.h"
#include "Runtime/Instantiate.h"
#include "Runtime/Paint.h"
#include "Style/StyleResolver.h"
#include "Widgets/TextEmit.h"
#include "Widgets/Widget.h"

#include <lumora/Clipboard.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Paint/Canvas.h>
#include <lumora/Text/TextMeasurer.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lumora
{
    namespace
    {
        /// One selectable leaf under a scope: enough to hit-test and read its text without resolving the node twice.
        struct Leaf
        {
            NodeId        id{};
            std::uint64_t key = 0;
        };

        /// Every Text leaf under `root` sharing `selection`, in tree order. Rebuilt per gesture event, not cached:
        /// the set changes as a virtualized list scrolls, and a stale cache selects a row that no longer exists.
        void CollectLeaves(NodeArena& arena, NodeId root, const TextSelection* selection,
                           std::vector<Leaf>& out)
        {
            for (NodeId child = arena.Resolve(root).firstChild; child.Valid();)
            {
                const LiveNode& n = arena.Resolve(child);
                if (n.kind == LiveKind::Text && n.payload.Valid())
                {
                    const TextPayload& t = arena.PayloadOf<TextPayload>(child);
                    if (t.selection.Get() == selection)
                    {
                        out.push_back(Leaf{child, t.selectionKey});
                    }
                }
                CollectLeaves(arena, child, selection, out);
                child = n.nextSibling;
            }
        }

        /// Byte offset in `leaf` nearest `surface`. Above the leaf resolves to its start, below to its end, so
        /// dragging past a row selects through it.
        [[nodiscard]] std::size_t OffsetIn(NodeArena& arena, const TextMeasurer& fonts, NodeId leaf,
                                           Vec2 surface)
        {
            const LiveNode&    n = arena.Resolve(leaf);
            const TextPayload& t = arena.PayloadOf<TextPayload>(leaf);
            const Rect         painted = detail::PaintedRect(arena, leaf, n);
            if (surface.y < painted.y)
            {
                return 0;
            }
            if (surface.y > painted.y + painted.height)
            {
                return t.text.size();
            }
            const TextRunGeometry run = TextRunGeometryOf(n, t);
            const Vec2            offset = detail::PaintOffset(arena, leaf);
            const Vec2 rel{surface.x - (run.pen.x + offset.x), surface.y - (run.pen.y + offset.y)};
            return fonts.CaretAtPoint(t.text, t.font, t.size, t.flow, run.width, rel).byteOffset;
        }

        /// Distance of `p` outside `r` (0 inside). Vertical dominates so a sideways drag stays on its row.
        [[nodiscard]] float Distance(const Rect& r, Vec2 p)
        {
            const float dx = std::max({r.x - p.x, p.x - (r.x + r.width), 0.f});
            const float dy = std::max({r.y - p.y, p.y - (r.y + r.height), 0.f});
            return dy * 1024.f + dx;
        }

        /// Selection position under `surface`: the containing leaf, else the nearest (a drag keeps tracking off the text).
        [[nodiscard]] std::optional<TextPos> PosAt(NodeArena& arena, const TextMeasurer* fonts,
                                                   const std::vector<Leaf>& leaves, Vec2 surface)
        {
            if (!fonts || leaves.empty())
            {
                return std::nullopt;
            }
            const Leaf* best = nullptr;
            float       bestDistance = 0.f;
            for (const Leaf& leaf : leaves)
            {
                const Rect  painted = detail::PaintedRect(arena, leaf.id, arena.Resolve(leaf.id));
                const float distance = Distance(painted, surface);
                if (!best || distance < bestDistance)
                {
                    best = &leaf;
                    bestDistance = distance;
                }
                if (distance == 0.f)
                {
                    break;
                }
            }
            return TextPos{best->key, OffsetIn(arena, *fonts, best->id, surface)};
        }

        enum class CharClass : std::uint8_t { Space, Word, Punct };

        [[nodiscard]] CharClass ClassOf(char c) noexcept
        {
            const auto u = static_cast<unsigned char>(c);
            if (u == ' ' || u == '\t' || u == '\n' || u == '\r')
            {
                return CharClass::Space;
            }
            if (u >= 0x80 || (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
                u == '_')
            {
                return CharClass::Word;
            }
            return CharClass::Punct;
        }

        /// Same-class byte run around `offset` (double-click word). Bytes >= 0x80 are all word bytes, so multi-byte
        /// code points never split.
        [[nodiscard]] std::pair<std::size_t, std::size_t> WordAt(std::string_view text, std::size_t offset)
        {
            if (text.empty())
            {
                return {0, 0};
            }
            const std::size_t at = std::min(offset, text.size() - 1);
            const CharClass   cls = ClassOf(text[at]);
            std::size_t       begin = at;
            std::size_t       end = at + 1;
            while (begin > 0 && ClassOf(text[begin - 1]) == cls)
            {
                --begin;
            }
            while (end < text.size() && ClassOf(text[end]) == cls)
            {
                ++end;
            }
            return {begin, end};
        }

        /// Mark the subtree for repaint: the selection is not a bound prop, so nothing else raises the flag.
        void MarkSubtreeDirty(NodeArena& arena, NodeId root)
        {
            LiveNode& n = arena.Resolve(root);
            n.MarkDirty(Dirty::Repaint);
            for (NodeId child = n.firstChild; child.Valid();)
            {
                const NodeId next = arena.Resolve(child).nextSibling;
                MarkSubtreeDirty(arena, child);
                child = next;
            }
        }

        /// The selected text. The caller's `textRange` is authoritative when rows can be unmounted; the mounted-leaf
        /// fallback is exact only for a non-virtualized scope.
        [[nodiscard]] std::string SelectedText(NodeArena& arena, NodeId root,
                                               const TextSelectionScopePayload& p)
        {
            if (!p.selection || !p.selection->HasSelection())
            {
                return {};
            }
            const TextPos lo = p.selection->Min();
            const TextPos hi = p.selection->Max();
            if (p.textRange)
            {
                return p.textRange(lo, hi);
            }

            std::vector<Leaf> leaves;
            CollectLeaves(arena, root, p.selection.Get(), leaves);
            std::sort(leaves.begin(), leaves.end(), [](const Leaf& a, const Leaf& b) { return a.key < b.key; });

            std::string out;
            for (const Leaf& leaf : leaves)
            {
                const TextPayload& t = arena.PayloadOf<TextPayload>(leaf.id);
                const auto         range = p.selection->RangeIn(leaf.key, t.text.size());
                if (!range)
                {
                    continue;
                }
                if (!out.empty())
                {
                    out += p.separator;
                }
                out.append(t.text, range->first, range->second - range->first);
            }
            return out;
        }

        /// The Ctrl+A extent, preferring the caller's answer over what is mounted.
        [[nodiscard]] std::optional<std::pair<TextPos, TextPos>> FullRange(NodeArena& arena, NodeId root,
                                                                          const TextSelectionScopePayload& p)
        {
            if (p.fullRange)
            {
                return p.fullRange();
            }
            std::vector<Leaf> leaves;
            CollectLeaves(arena, root, p.selection.Get(), leaves);
            if (leaves.empty())
            {
                return std::nullopt;
            }
            const auto ends = std::minmax_element(leaves.begin(), leaves.end(),
                                                  [](const Leaf& a, const Leaf& b) { return a.key < b.key; });
            const TextPayload& last = arena.PayloadOf<TextPayload>(ends.second->id);
            return std::pair{TextPos{ends.first->key, 0}, TextPos{ends.second->key, last.text.size()}};
        }

        class TextSelectionScopeWidget final : public TypedWidget<TextSelectionScopeCfg>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "TextSelectionScope"; }

            void Build(AppCtx& ctx, TextSelectionScopeCfg& cfg, NodeId id, Owner&) override
            {
                const std::uint32_t slot = ctx.arena.Pool<TextSelectionScopePayload>(LiveKind::TextSelectionScope)
                                              .Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::TextSelectionScope, slot};
                ctx.arena.Resolve(id).style.axis = FlexAxis::Column;
                ctx.arena.Resolve(id).style.anchor = cfg.anchor;

                TextSelectionScopePayload& payload = ctx.arena.PayloadOf<TextSelectionScopePayload>(id);
                payload.selection = cfg.selection;
                payload.textRange = cfg.textRange;
                payload.fullRange = cfg.fullRange;
                payload.separator = cfg.separator;

                if (ctx.style)
                {
                    BindPositionProps(ctx.arena, id, cfg.position);
                    BindInteractProps<TextSelectionScopePayload>(ctx, id, cfg.on,
                                                                 &TextSelectionScopePayload::on);
                    style::StyleEngine* eng = ctx.style;
                    style::BindClass(ctx, id, cfg,
                        [eng](NodeArena& a, NodeId n, const style::ComputedStyle& cs,
                              const TextSelectionScopeCfg& c)
                        {
                            style::ApplyBoxLike<TextSelectionScopePayload>(
                                a, n, cs, c, &TextSelectionScopePayload::style,
                                &TextSelectionScopePayload::view, &TextSelectionScopePayload::transform,
                                FlexAxis::Column, eng->NodeState(n));
                            style::EnrolBoxAnimator<TextSelectionScopePayload,
                                                    &TextSelectionScopePayload::style,
                                                    &TextSelectionScopePayload::transform>(*eng, a, n);
                        });
                }
                else
                {
                    BindLayoutProps(ctx.arena, id, cfg.layout);
                    BindStyleProps<TextSelectionScopePayload>(ctx.arena, id, cfg.style,
                                                              &TextSelectionScopePayload::style);
                    BindViewProps<TextSelectionScopePayload>(ctx.arena, id, cfg.view,
                                                             &TextSelectionScopePayload::view);
                    BindTransformProps<TextSelectionScopePayload>(ctx.arena, id, cfg.transform,
                                                                  &TextSelectionScopePayload::transform);
                    BindPositionProps(ctx.arena, id, cfg.position);
                    BindInteractProps<TextSelectionScopePayload>(ctx, id, cfg.on,
                                                                 &TextSelectionScopePayload::on);
                }

                // The scope holds keyboard focus for the group so Ctrl+C survives the origin row being unmounted by a
                // scroll. Click-only: Tab should not stop on a log view.
                InteractState& on = ctx.arena.PayloadOf<TextSelectionScopePayload>(id).on;
                if (on.focusPolicy == FocusPolicy::None)
                {
                    on.focusPolicy = FocusPolicy::Click;
                }
            }

            void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override
            {
                const LiveNode&                  n = ctx.arena->Resolve(id);
                const TextSelectionScopePayload& p = ctx.arena->PayloadOf<TextSelectionScopePayload>(id);
                const StyleState&                s = p.style;
                const bool hovering = p.on.hovered && s.hoverFill.a > 0;
                if (s.fill.a == 0 && s.border.width <= 0.f && !hovering)
                {
                    return;
                }
                canvas.DrawRect({.bounds = n.solved,
                                 .fill = hovering ? s.hoverFill : s.fill,
                                 .radius = s.radius,
                                 .borderColor = s.border.color,
                                 .borderWidth = s.border.width,
                                 .borderRadius = s.radius,
                                 .borderDash = s.border.DashLength()});
            }

            [[nodiscard]] HitTestBehavior HitTest(const EventContext& ctx, Vec2) const override
            {
                return ctx.Payload<TextSelectionScopePayload>().on.hitThrough ? HitTestBehavior::Ignore
                                                                              : HitTestBehavior::Opaque;
            }

            void OnEvent(EventContext& ctx, Event& ev) override
            {
                if (ctx.phase == RoutePhase::Capture)
                {
                    return;
                }
                TextSelectionScopePayload& p = ctx.Payload<TextSelectionScopePayload>();
                if (!p.selection || p.on.disabled)
                {
                    DispatchInteract(ctx, ev, p.on);
                    return;
                }
                switch (ev.Type)
                {
                    case EventType::MouseDown:
                    case EventType::MouseMove:
                    case EventType::MouseUp:
                        HandlePointer(ctx, p, ev);
                        break;
                    case EventType::KeyDown:
                        HandleKey(ctx, p, ev);
                        break;
                    case EventType::FocusLost:
                        // Keep the selection readable after focus moves away; only Escape and the next press clear it.
                        break;
                    default:
                        break;
                }
                if (!ev.Consumed)
                {
                    DispatchInteract(ctx, ev, p.on);
                }
            }

        private:
            static void Commit(EventContext& ctx, std::uint64_t before, const TextSelection& selection)
            {
                if (selection.Epoch() != before)
                {
                    MarkSubtreeDirty(*ctx.arena, ctx.node);
                }
            }

            void HandlePointer(EventContext& ctx, TextSelectionScopePayload& p, Event& ev)
            {
                TextSelection&      selection = *p.selection;
                const std::uint64_t before = selection.Epoch();

                switch (ev.Type)
                {
                    case EventType::MouseDown:
                    {
                        std::vector<Leaf> leaves;
                        CollectLeaves(*ctx.arena, ctx.node, p.selection.Get(), leaves);
                        const std::optional<TextPos> pos = PosAt(*ctx.arena, ctx.fonts, leaves, ev.Position);

                        if (ev.Button == PointerButton::Right)
                        {
                            const bool inside = pos && selection.HasSelection() && *pos >= selection.Min() &&
                                                *pos <= selection.Max();
                            if (!inside)
                            {
                                selection.Clear();
                            }
                            if (ctx.state && ctx.state->menu)
                            {
                                ctx.state->menu->Open(ev.Position, BuildMenu(ctx, p));
                            }
                            ev.Consume();
                            break;
                        }
                        if (ev.Button != PointerButton::Left || !pos)
                        {
                            break;
                        }

                        const float now = ctx.state ? ctx.state->clock : 0.f;
                        const bool  repeat = (now - p.lastClickTime) < kDoubleClickSeconds &&
                                            std::abs(ev.Position.x - p.lastClickPos.x) < kDoubleClickSlop &&
                                            std::abs(ev.Position.y - p.lastClickPos.y) < kDoubleClickSlop;
                        p.clickCount = repeat ? p.clickCount + 1 : 1;
                        p.lastClickTime = now;
                        p.lastClickPos = ev.Position;

                        if (p.clickCount >= 3)
                        {
                            SelectWholeLeaf(*ctx.arena, ctx.node, selection, *pos);
                        }
                        else if (p.clickCount == 2)
                        {
                            SelectWord(*ctx.arena, ctx.node, selection, *pos);
                        }
                        else if (Has(ev.Mods, KeyMods::Shift))
                        {
                            selection.ExtendTo(*pos);
                        }
                        else
                        {
                            selection.Begin(*pos);
                        }
                        p.dragging = p.clickCount == 1;
                        if (p.dragging)
                        {
                            ctx.Capture();
                        }
                        ev.Consume();
                        break;
                    }
                    case EventType::MouseMove:
                        if (p.dragging)
                        {
                            std::vector<Leaf> leaves;
                            CollectLeaves(*ctx.arena, ctx.node, p.selection.Get(), leaves);
                            if (const std::optional<TextPos> pos =
                                    PosAt(*ctx.arena, ctx.fonts, leaves, ev.Position))
                            {
                                selection.ExtendTo(*pos);
                            }
                            ev.Consume();
                        }
                        break;
                    case EventType::MouseUp:
                        if (p.dragging)
                        {
                            p.dragging = false;
                            ctx.ReleaseCapture();
                            ev.Consume();
                        }
                        break;
                    default:
                        break;
                }
                Commit(ctx, before, selection);
            }

            void HandleKey(EventContext& ctx, TextSelectionScopePayload& p, Event& ev)
            {
                TextSelection&      selection = *p.selection;
                const std::uint64_t before = selection.Epoch();
                const bool          ctrl = Has(ev.Mods, KeyMods::Ctrl) || Has(ev.Mods, KeyMods::Super);

                switch (ev.KeyCode)
                {
                    case Key::C:
                        if (ctrl)
                        {
                            Copy(ctx, p);
                            ev.Consume();
                        }
                        break;
                    case Key::A:
                        if (ctrl)
                        {
                            if (const auto range = FullRange(*ctx.arena, ctx.node, p))
                            {
                                selection.Set(range->first, range->second);
                            }
                            ev.Consume();
                        }
                        break;
                    case Key::Escape:
                        if (selection.HasSelection())
                        {
                            selection.Clear();
                            ev.Consume();
                        }
                        break;
                    default:
                        break;
                }
                Commit(ctx, before, selection);
            }

            static void Copy(const EventContext& ctx, const TextSelectionScopePayload& p)
            {
                if (!ctx.clip)
                {
                    return;
                }
                std::string text = SelectedText(*ctx.arena, ctx.node, p);
                if (!text.empty())
                {
                    ctx.clip->SetText(std::move(text));
                }
            }

            static void SelectWord(NodeArena& arena, NodeId root, TextSelection& selection, TextPos at)
            {
                std::vector<Leaf> leaves;
                CollectLeaves(arena, root, &selection, leaves);
                for (const Leaf& leaf : leaves)
                {
                    if (leaf.key != at.key)
                    {
                        continue;
                    }
                    const TextPayload& t = arena.PayloadOf<TextPayload>(leaf.id);
                    const auto [begin, end] = WordAt(t.text, at.offset);
                    selection.Set(TextPos{at.key, begin}, TextPos{at.key, end});
                    return;
                }
            }

            static void SelectWholeLeaf(NodeArena& arena, NodeId root, TextSelection& selection, TextPos at)
            {
                std::vector<Leaf> leaves;
                CollectLeaves(arena, root, &selection, leaves);
                for (const Leaf& leaf : leaves)
                {
                    if (leaf.key != at.key)
                    {
                        continue;
                    }
                    const TextPayload& t = arena.PayloadOf<TextPayload>(leaf.id);
                    selection.Set(TextPos{at.key, 0}, TextPos{at.key, t.text.size()});
                    return;
                }
            }

            /// Copy / Select All, deferred via stable handles: the menu drains on Tick with no EventContext, and this
            /// scope may be unmounted.
            [[nodiscard]] static std::vector<ContextMenuItem> BuildMenu(const EventContext& ctx,
                                                                        const TextSelectionScopePayload& p)
            {
                NodeArena*    arena = ctx.arena;
                Clipboard*    clip = ctx.clip;
                const NodeRef self{*ctx.arena, ctx.node};
                const bool    hasSelection = p.selection->HasSelection();

                auto copy = [arena, clip, self]
                {
                    const NodeId id = self.Resolve(*arena);
                    if (!id.Valid() || !clip)
                    {
                        return;
                    }
                    std::string text = SelectedText(*arena, id, arena->PayloadOf<TextSelectionScopePayload>(id));
                    if (!text.empty())
                    {
                        clip->SetText(std::move(text));
                    }
                };
                auto selectAll = [arena, self]
                {
                    const NodeId id = self.Resolve(*arena);
                    if (!id.Valid())
                    {
                        return;
                    }
                    TextSelectionScopePayload& payload = arena->PayloadOf<TextSelectionScopePayload>(id);
                    if (!payload.selection)
                    {
                        return;
                    }
                    if (const auto range = FullRange(*arena, id, payload))
                    {
                        payload.selection->Set(range->first, range->second);
                        MarkSubtreeDirty(*arena, id);
                    }
                };

                return {
                    {.label = "Copy", .shortcut = "Ctrl+C", .enabled = hasSelection, .onClick = copy},
                    {.label = "Select All", .shortcut = "Ctrl+A", .onClick = selectAll},
                };
            }
        };

        void EnsureKind()
        {
            static const bool registered = []
            {
                KindRegistry::Register(LiveKind::TextSelectionScope, MakeRef<TextSelectionScopeWidget>());
                return true;
            }();
            (void)registered;
        }
    } // namespace

    namespace detail
    {
        Ref<Node> BuildTextSelectionScope(TextSelectionScopeCfg cfg)
        {
            EnsureKind();
            return MakeBlueprint(LiveKind::TextSelectionScope, std::move(cfg));
        }
    } // namespace detail
} // namespace lumora
