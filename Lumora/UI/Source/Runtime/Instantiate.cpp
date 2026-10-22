#include "Runtime/Instantiate.h"

#include "Layout/LayoutBridge.h"
#include "Node/KindDescriptor.h"
#include "Node/NodeArena.h"
#include "Runtime/AppCtx.h"
#include "Runtime/BuilderProbe.h"
#include "Widgets/Widget.h"

#include <lumora/Detail/Node.h>
#include <lumora/Elem.h>
#include <lumora/Reactive/Owner.h>

#include <lumora/log/Log.h>

LUMORA_LOG_CATEGORY("Lumora")

namespace lumora
{
    static_assert(static_cast<detail::KindId>(LiveKind::Component) == detail::ComponentKind,
                  "ComponentKind must equal the LiveKind::Component enum value");

    namespace
    {
        NodeId InstantiateNode(AppCtx& ctx, detail::Node& blueprint, NodeId parentId, Owner& owner);

        /// Built-in erased-class-component kind. Transparent: owns no solver node; splices its Render() subtree under the nearest opaque ancestor.
        class ComponentWidget final : public Widget
        {
        public:
            [[nodiscard]] const char* Name() const noexcept override { return "Component"; }

            void Instantiate(AppCtx& ctx, detail::Node& blueprint, NodeId id, Owner& owner) override
            {
                auto& component = static_cast<detail::ComponentNode&>(blueprint);
                detail::ErasedComponent erased = component.erased;

                // Allocate the payload before anything can bail out, so a refused mount is still a well-formed empty Component node.
                const std::uint32_t payloadSlot = ctx.arena.Pool<ComponentPayload>(LiveKind::Component).Alloc();
                ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Component, payloadSlot};
                {
                    ComponentPayload& empty = ctx.arena.PayloadOf<ComponentPayload>(id);
                    empty.instance = nullptr;
                    empty.event    = nullptr;
                }

                // Copyable: each mount copies and the blueprint keeps the prototype. Non-copyable: this mount takes the one instance,
                // and a second mount has nothing to run and is refused here.
                if (erased.instance == nullptr)
                {
                    LOG_ERROR("a non-copyable Component blueprint was instantiated twice; the second mount "
                              "renders nothing -- build it inside the Show/Accordion body or make the class "
                              "copy-constructible");
                    return;
                }
                if (erased.clone != nullptr)
                {
                    erased.instance = erased.clone(erased.instance);
                }
                else
                {
                    component.erased.instance = nullptr; // ownership handed to `owner`; the blueprint won't free it
                }

                // Stash the instance + event thunk so the dispatcher can deliver OnEvent; the instance outlives the payload (its destroy cleanup runs before Free).
                ComponentPayload& cp = ctx.arena.PayloadOf<ComponentPayload>(id);
                cp.instance = erased.instance;
                cp.event = erased.event;

                // Registered first, runs last on disposal: the subtree's effects (which reference member signals) are severed by then.
                if (erased.destroy)
                {
                    owner.AddCleanup([erased] { erased.destroy(erased.instance); });
                }

                // Render() runs once; its subtree instantiates under this scope so member-signal effects keep patching.
                if (erased.render)
                {
                    Elem child;
                    {
                        detail::BuilderProbeScope probe(ReactiveGraph::Ambient());
                        child = erased.render(erased.instance);
#ifndef NDEBUG
                        if (probe.TrackedAnything())
                        {
                            LOG_WARN("a Component Render() read a signal directly; Render runs once, so the "
                                "read never updates - wrap it in USE(...) or pass the signal into the prop");
                        }
#endif
                    }
                    if (detail::Node* childNode = child.RawNode())
                    {
                        const NodeId childId = InstantiateNode(ctx, *childNode, id, owner);
                        AppendChild(ctx.arena, id, childId);
                    }
                }

                // Registered last, runs first on disposal while the instance is alive.
                if (erased.unmount)
                {
                    owner.AddCleanup([erased] { erased.unmount(erased.instance); });
                }

                if (erased.mount)
                {
                    erased.mount(erased.instance);
                }
            }
        };

        NodeId InstantiateNode(AppCtx& ctx, detail::Node& blueprint, NodeId parentId, Owner& owner)
        {
            Owner::Scope active(owner); // prop-binding effects and Use() resolve against this scope

            const auto kind = static_cast<LiveKind>(blueprint.Kind());

            NodeId id;
            {
                LiveNode& node = ctx.arena.Alloc(kind);
                id = node.id;
                node.parent = parentId;
                node.scope = &owner;
            }

            // Storage reclamation is an owner cleanup, so it runs after the owner has severed this subtree's effects.
            NodeArena* arena = &ctx.arena;
            owner.AddCleanup([arena, id] { arena->Free(id); });

            // Create the solver node with the live node and release it on disposal. Transparent kinds own none; a fresh node is layout-dirty.
            if (ctx.layout && !IsTransparent(kind))
            {
                const LayoutHandle handle = ctx.layout->Attach();
                LiveNode& node = ctx.arena.Resolve(id);
                node.layout = handle;
                node.MarkDirty(Dirty::Layout);
                LayoutTree* tree = ctx.layout;
                owner.AddCleanup([tree, handle] { tree->Detach(handle); });
            }

            if (Widget* widget = KindRegistry::For(kind))
            {
                widget->Instantiate(ctx, blueprint, id, owner);
            }

            for (const Ref<detail::Node>& child : blueprint.Children())
            {
                if (!child)
                {
                    continue;
                }
                const NodeId childId = InstantiateNode(ctx, *child, id, owner);
                AppendChild(ctx.arena, id, childId);
            }

            return id;
        }

        void RegisterBuiltinKinds()
        {
            KindRegistry::Register(LiveKind::Component, MakeRef<ComponentWidget>());
        }
    } // namespace

    NodeId Instantiate(AppCtx& ctx, detail::Node& blueprint, NodeId parent, Owner& owner)
    {
        [[maybe_unused]] static const bool registered = (RegisterBuiltinKinds(), true);
        return InstantiateNode(ctx, blueprint, parent, owner);
    }

    void AppendChild(NodeArena& arena, NodeId parentId, NodeId childId)
    {
        LiveNode& parent = arena.Resolve(parentId);
        LiveNode& child = arena.Resolve(childId);

        child.parent = parentId;
        child.prevSibling = parent.lastChild;
        child.nextSibling = NodeId{};

        if (parent.lastChild.Valid())
        {
            arena.Resolve(parent.lastChild).nextSibling = childId;
        }
        else
        {
            parent.firstChild = childId;
        }
        parent.lastChild = childId;
        ++parent.childCount;
        // The parent owns the byte range the child's commands land in, so it must re-record to make room.
        parent.MarkDirty(Dirty::Repaint);
        child.MarkDirty(Dirty::Repaint);
    }
} // namespace lumora
