#include "Runtime/ControlRuntime.h"

#include "Layout/LayoutBridge.h"
#include "Node/ControlState.h"
#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Reactive/ReactiveGraph.h"
#include "Runtime/AppCtx.h"
#include "Runtime/BuilderProbe.h"
#include "Runtime/Instantiate.h"
#include "Runtime/OverlayRoot.h"
#include "Widgets/Widget.h"

#include <lumora/Detail/Node.h>
#include <lumora/Detail/Reactive.h>
#include <lumora/Elem.h>
#include <lumora/Reactive/Effect.h>
#include <lumora/Reactive/Owner.h>

#include <lumora/log/Log.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

LUMORA_LOG_CATEGORY (
"Lumora"
)

namespace lumora
{
    // Child-list splicing: control nodes are transparent, so the runtime (not the instantiation walk) links their children.

    void UnlinkChild(NodeArena& arena, NodeId parentId, NodeId childId)
    {
        LiveNode& child = arena.Resolve(childId);
        const NodeId prev = child.prevSibling;
        const NodeId next = child.nextSibling;

        LiveNode& parent = arena.Resolve(parentId);
        if (prev.Valid())
        {
            arena.Resolve(prev).nextSibling = next;
        }
        else
        {
            parent.firstChild = next;
        }
        if (next.Valid())
        {
            arena.Resolve(next).prevSibling = prev;
        }
        else
        {
            parent.lastChild = prev;
        }
        if (parent.childCount > 0)
        {
            --parent.childCount;
        }
        child.prevSibling = NodeId{};
        child.nextSibling = NodeId{};
        parent.MarkDirty(Dirty::Repaint); // the emitted child list shrank: the parent must re-record it
    }

    void SetChildOrder(NodeArena& arena, NodeId parentId, std::span<const NodeId> order)
    {
        LiveNode& parent = arena.Resolve(parentId);
        parent.firstChild = NodeId{};
        parent.lastChild = NodeId{};
        parent.childCount = 0;

        NodeId prev{};
        for (const NodeId childId : order)
        {
            LiveNode& child = arena.Resolve(childId);
            child.parent = parentId;
            child.prevSibling = prev;
            child.nextSibling = NodeId{};
            if (prev.Valid())
            {
                arena.Resolve(prev).nextSibling = childId;
            }
            else
            {
                arena.Resolve(parentId).firstChild = childId;
            }
            prev = childId;
            ++arena.Resolve(parentId).childCount;
        }
        arena.Resolve(parentId).lastChild = prev;
        arena.Resolve(parentId).MarkDirty(Dirty::Repaint);
    }

    namespace
    {
        /// Runs a body builder under `scope` (so Provide()/lifecycle bind there), shielding the controlling effect from its reads.
        Elem RunBuilder(Owner& scope, const std::function<Elem()>& builder)
        {
            Owner::Scope active(scope);
            detail::BuilderProbeScope probe(ReactiveGraph::Ambient());

            Elem elem = builder ? builder() : Elem{};

#ifndef NDEBUG
            if (probe.TrackedAnything())
            {
                LOG_WARN("a Show/Each builder read a signal directly; read it through a child widget's "
                    "Reactive prop instead, or the whole branch rebuilds on every change");
            }
#endif
            return elem;
        }
    } // namespace

    MountedBody MountBody(AppCtx& ctx, NodeId controlId, const std::function<Elem()>& builder)
    {
        Owner* parentScope = ctx.arena.Resolve(controlId).scope;
        auto scope = std::make_unique<Owner>(parentScope, Dormant);

        Elem elem = RunBuilder(*scope, builder);
        detail::Node* node = elem.RawNode();
        if (!node)
        {
            // Empty body: nothing to instantiate, so drop the scope now instead of parking an empty Owner.
            return MountedBody{nullptr, NodeId{}};
        }

        NodeId root = Instantiate(ctx, *node, controlId, *scope);
        return MountedBody{std::move(scope), root};
    }

    namespace
    {
        // Payload pools relocate on Alloc and MountBody allocates, so reconcilers re-resolve via PayloadOf around
        // every build step instead of holding a State&.

        void ReconcileShow(AppCtx& ctx, NodeId id, bool show)
        {
            std::unique_ptr<Owner> oldScope;
            {
                ShowState& st = ctx.arena.PayloadOf<ShowState>(id);
                if (st.hasMounted && st.showing == show)
                {
                    return;
                }
                if (st.mounted.Valid())
                {
                    UnlinkChild(ctx.arena, id, st.mounted);
                }
                oldScope = std::move(st.scope);
                st.mounted = NodeId{};
                st.showing = show;
                st.hasMounted = true;
            }
            oldScope.reset(); // dispose the previous branch (cleanups + frees) before building the next

            std::function < Elem() > builder;
            {
                ShowState& st = ctx.arena.PayloadOf<ShowState>(id);
                builder = show ? st.config.build : st.config.fallback;
            }

            MountedBody body = MountBody(ctx, id, builder);
            if (body.root.Valid())
            {
                AppendChild(ctx.arena, id, body.root);
            }
            {
                ShowState& st = ctx.arena.PayloadOf<ShowState>(id);
                st.scope = std::move(body.scope);
                st.mounted = body.root;
            }
            MarkStructureDirty(ctx.arena, id);
        }

        void ReconcileEach(AppCtx& ctx, NodeId id, std::vector<detail::EachRow> desired)
        {
            std::vector<EachRowEntry> oldRows;
            {
                EachState& st = ctx.arena.PayloadOf<EachState>(id);
                oldRows = std::move(st.rows);
                st.rows.clear();
            }

            // First occurrence wins on a duplicate key.
            std::unordered_map<std::uint64_t, std::size_t> byKey;
            byKey.reserve(oldRows.size());
            for (std::size_t i = 0; i < oldRows.size(); ++i)
            {
                byKey.emplace(oldRows[i].key, i);
            }
            std::vector<bool> reused(oldRows.size(), false);

            std::vector<EachRowEntry> newRows;
            newRows.reserve(desired.size());
            for (detail::EachRow& d : desired)
            {
                const auto it = byKey.find(d.key);
                if (it != byKey.end() && !reused[it->second])
                {
                    reused[it->second] = true;
                    newRows.push_back(std::move(oldRows[it->second]));
                }
                else
                {
                    MountedBody body = MountBody(ctx, id, d.build);
                    newRows.push_back(EachRowEntry{d.key, std::move(body.scope), body.root});
                }
            }

            for (std::size_t i = 0; i < oldRows.size(); ++i)
            {
                if (!reused[i])
                {
                    if (oldRows[i].root.Valid())
                    {
                        UnlinkChild(ctx.arena, id, oldRows[i].root);
                    }
                    oldRows[i].scope.reset();
                }
            }

            std::vector<NodeId> order;
            order.reserve(newRows.size());
            for (const EachRowEntry& row : newRows)
            {
                if (row.root.Valid())
                {
                    order.push_back(row.root);
                }
            }
            SetChildOrder(ctx.arena, id, order);

            {
                EachState& st = ctx.arena.PayloadOf<EachState>(id);
                st.rows = std::move(newRows);
            }
            MarkStructureDirty(ctx.arena, id);
        }

        // Transparent hosts: no solver node, no visual; each overrides only Build to wire the effect driving its reconciler.

        class ShowWidget final : public TypedWidget<detail::ShowConfig>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "Show"; }

            void Build(AppCtx& ctx, detail::ShowConfig& config, NodeId id, Owner& owner) override
            {
                const std::uint32_t slot = ctx.arena.Pool<ShowState>(LiveKind::Show).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Show, slot};
                ctx.arena.PayloadOf<ShowState>(id).config = config;

                // Dispose the mounted branch on scope teardown; runs (LIFO) before the node's own Free, so the payload still resolves.
                NodeArena* arena = &ctx.arena;
                owner.AddCleanup([arena, id]
                {
                    if (LiveNode* node = arena->TryResolve(id); node && node->payload.Valid())
                    {
                        arena->PayloadOf<ShowState>(id).scope.reset();
                    }
                });

                // The first run mounts the initial branch synchronously; later reconciles are deferred to the next Tick. Pin the slot
                // generation at queue time and skip a stale reconcile: the node may be disposed and its slot recycled before the drain.
                AppCtx* ctxPtr = &ctx;
                Effect([ctxPtr, id]
                {
                    const bool show = ctxPtr->arena.PayloadOf<ShowState>(id).config.cond.Get();
                    const std::uint32_t gen = ctxPtr->arena.GenerationOf(id);
                    ctxPtr->graph.ApplyStructuralEdit([ctxPtr, id, gen, show]
                    {
                        if (ctxPtr->arena.GenerationOf(id) == gen) ReconcileShow(*ctxPtr, id, show);
                    });
                });
            }
        };

        class EachWidget final : public TypedWidget<detail::EachConfig>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "Each"; }

            void Build(AppCtx& ctx, detail::EachConfig& config, NodeId id, Owner& owner) override
            {
                const std::uint32_t slot = ctx.arena.Pool<EachState>(LiveKind::Each).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Each, slot};
                ctx.arena.PayloadOf<EachState>(id).config = config;

                NodeArena* arena = &ctx.arena;
                owner.AddCleanup([arena, id]
                {
                    if (LiveNode* node = arena->TryResolve(id); node && node->payload.Valid())
                    {
                        for (EachRowEntry& row : arena->PayloadOf<EachState>(id).rows)
                        {
                            row.scope.reset();
                        }
                    }
                });

                // Generation-guarded like Show: the node may be disposed and recycled before the deferred reconcile runs.
                AppCtx* ctxPtr = &ctx;
                Effect([ctxPtr, id]
                {
                    std::vector<detail::EachRow> rows = ctxPtr->arena.PayloadOf<EachState>(id).config.snapshot();
                    const std::uint32_t gen = ctxPtr->arena.GenerationOf(id);
                    ctxPtr->graph.ApplyStructuralEdit(
                        [ctxPtr, id, gen, rows = std::move(rows)]() mutable
                        {
                            if (ctxPtr->arena.GenerationOf(id) == gen) ReconcileEach(*ctxPtr, id, std::move(rows));
                        });
                });
            }
        };

        class MemoWidget final : public TypedWidget<detail::MemoConfig>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "Memo"; }

            void Build(AppCtx& ctx, detail::MemoConfig& config, NodeId id, Owner& owner) override
            {
                const std::uint32_t slot = ctx.arena.Pool<MemoState>(LiveKind::Memo).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Memo, slot};

                std::function < Elem() > builder;
                {
                    MemoState& st = ctx.arena.PayloadOf<MemoState>(id);
                    st.config = config;
                    builder = st.config.build;
                }

                NodeArena* arena = &ctx.arena;
                owner.AddCleanup([arena, id]
                {
                    if (LiveNode* node = arena->TryResolve(id); node && node->payload.Valid())
                    {
                        arena->PayloadOf<MemoState>(id).scope.reset();
                    }
                });

                // Built once; Memo has no reactive trigger, rebuilds come through MemoReconcile when a parent re-instantiates it.
                MountedBody body = MountBody(ctx, id, builder);
                if (body.root.Valid())
                {
                    AppendChild(ctx.arena, id, body.root);
                }
                {
                    MemoState& st = ctx.arena.PayloadOf<MemoState>(id);
                    st.scope = std::move(body.scope);
                    st.mounted = body.root;
                }
                MarkStructureDirty(ctx.arena, id);
            }
        };

        // Transparent like Memo (built once; open/close lives in the wrapping Show/Presence), but the content is spliced under an
        // overlay layer node so paint and hit-test escape the declaring ancestor's clip and z-order. Its scope still parents to the declaring owner.

        class PortalWidget final : public TypedWidget<detail::PortalConfig>
        {
            [[nodiscard]] const char* Name() const noexcept override { return "Portal"; }

            void Build(AppCtx& ctx, detail::PortalConfig& config, NodeId id, Owner& owner) override
            {
                const std::uint32_t slot = ctx.arena.Pool<PortalState>(LiveKind::Portal).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Portal, slot};

                std::function < Elem() > builder;
                Layer layer;
                PortalOptions options;
                {
                    PortalState& st = ctx.arena.PayloadOf<PortalState>(id);
                    st.config = config;
                    st.overlay = ctx.overlay;
                    builder = st.config.build;
                    layer = st.config.layer;
                    options = st.config.options;
                }

                const NodeId layerNode = ctx.overlay ? ctx.overlay->LayerNode(layer) : NodeId{};

                MountedBody body = MountBody(ctx, id, builder);

                NodeId splicedParent;
                if (body.root.Valid())
                {
                    if (layerNode.Valid())
                    {
                        AppendChild(ctx.arena, layerNode, body.root);
                        splicedParent = layerNode;
                    }
                    else
                    {
                        // No overlay root (headless/test): render inline at the declaration site.
                        AppendChild(ctx.arena, id, body.root);
                        splicedParent = id;
                        if (!ctx.overlay)
                        {
                            LOG_WARN("Portal mounted without an overlay root; rendering inline");
                        }
                    }
                }

                std::uint64_t entryId = 0;
                if (ctx.overlay && body.root.Valid() && layerNode.Valid())
                {
                    entryId = ctx.overlay->NextId();
                    PortalEntry entry;
                    entry.id = entryId;
                    entry.seq = ctx.overlay->NextSeq();
                    entry.layer = layer;
                    entry.content = body.root;
                    entry.layerNode = layerNode;
                    entry.options = std::move(options);
                    ctx.overlay->Register(std::move(entry));
                }

                {
                    PortalState& st = ctx.arena.PayloadOf<PortalState>(id);
                    st.scope = std::move(body.scope);
                    st.content = body.root;
                    st.layerNode = splicedParent;
                    st.entryId = entryId;
                }

                // Register anchored placement for the content root, if the sugar (Popover etc.) asked for one.
                if (ctx.overlay && body.root.Valid() && layerNode.Valid())
                {
                    PortalState& st = ctx.arena.PayloadOf<PortalState>(id);
                    if (st.config.positioner)
                    {
                        PositionerEntry pe;
                        pe.anchor = st.config.positioner->anchor;
                        pe.panel = body.root;
                        pe.base = st.config.positioner->base;
                        pe.offset = st.config.positioner->offset;
                        pe.middleware = st.config.positioner->middleware;
                        pe.viewportPad = st.config.positioner->viewportPad;
                        pe.onResult = st.config.positioner->onResult;
                        ctx.overlay->AddPositioner(std::move(pe));
                    }
                }

                // Dispose + unsplice on declaring-scope teardown; runs first under LIFO so the payload still resolves.
                NodeArena* arena = &ctx.arena;
                owner.AddCleanup([arena, id]
                {
                    LiveNode* node = arena->TryResolve(id);
                    if (!node || !node->payload.Valid())
                    {
                        return;
                    }
                    PortalState& st = arena->PayloadOf<PortalState>(id);
                    if (st.overlay && st.content.Valid())
                    {
                        st.overlay->RemovePositioner(st.content);
                    }
                    if (st.content.Valid() && st.layerNode.Valid())
                    {
                        UnlinkChild(*arena, st.layerNode, st.content);
                        MarkStructureDirty(*arena, st.content);
                    }
                    if (st.overlay && st.entryId != 0)
                    {
                        st.overlay->Remove(st.entryId);
                    }
                    st.scope.reset();
                });

                if (body.root.Valid())
                {
                    MarkStructureDirty(ctx.arena, body.root);
                }
            }
        };
    } // namespace

    void EnsureControlKinds()
    {
        static const bool registered = []
        {
            KindRegistry::Register(LiveKind::Show, MakeRef<ShowWidget>());
            KindRegistry::Register(LiveKind::Each, MakeRef<EachWidget>());
            KindRegistry::Register(LiveKind::Memo, MakeRef<MemoWidget>());
            KindRegistry::Register(LiveKind::Portal, MakeRef<PortalWidget>());
            return true;
        }();
        (void)registered;
    }

    bool MemoReconcile(AppCtx& ctx, NodeId memoNode, detail::MemoConfig next)
    {
        {
            MemoState& st = ctx.arena.PayloadOf<MemoState>(memoNode);
            if (st.config.args && next.args && st.config.args->Equals(*next.args))
            {
                return false;
            }
        }

        std::unique_ptr<Owner> oldScope;
        {
            MemoState& st = ctx.arena.PayloadOf<MemoState>(memoNode);
            if (st.mounted.Valid())
            {
                UnlinkChild(ctx.arena, memoNode, st.mounted);
            }
            oldScope = std::move(st.scope);
            st.mounted = NodeId{};
            st.config = std::move(next);
        }
        oldScope.reset();

        std::function < Elem() > builder;
        {
            MemoState& st = ctx.arena.PayloadOf<MemoState>(memoNode);
            builder = st.config.build;
        }

        MountedBody body = MountBody(ctx, memoNode, builder);
        if (body.root.Valid())
        {
            AppendChild(ctx.arena, memoNode, body.root);
        }
        {
            MemoState& st = ctx.arena.PayloadOf<MemoState>(memoNode);
            st.scope = std::move(body.scope);
            st.mounted = body.root;
        }
        MarkStructureDirty(ctx.arena, memoNode);
        return true;
    }

    namespace detail
    {
        Ref<Node> BuildShow(ShowConfig cfg)
        {
            EnsureControlKinds();
            return MakeRef<TypedNode<ShowConfig>>(static_cast<KindId>(LiveKind::Show), std::move(cfg));
        }

        Ref<Node> BuildEach(EachConfig cfg)
        {
            EnsureControlKinds();
            return MakeRef<TypedNode<EachConfig>>(static_cast<KindId>(LiveKind::Each), std::move(cfg));
        }

        Ref<Node> BuildMemo(MemoConfig cfg)
        {
            EnsureControlKinds();
            return MakeRef<TypedNode<MemoConfig>>(static_cast<KindId>(LiveKind::Memo), std::move(cfg));
        }

        Ref<Node> BuildPortal(PortalConfig cfg)
        {
            EnsureControlKinds();
            return MakeRef<TypedNode<PortalConfig>>(static_cast<KindId>(LiveKind::Portal), std::move(cfg));
        }
    } // namespace detail
} // namespace lumora
