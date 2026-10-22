#pragma once

#include <lumora/Ref.h>
#include <lumora/Detail/Node.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Foundation/NodeId.h>
#include <lumora/Foundation/Vec2.h>

#include "Input/Focus.h"
#include "Input/Interaction.h"
#include "Node/Kind.h"
#include "Runtime/ChildOrder.h"

#include <utility>

namespace lumora
{
    struct AppCtx;
    class Owner;
    class Canvas;
    struct MeasureContext;
    struct PaintContext;

    /// A per-kind behaviour object: one stateless instance per LiveKind, held in the KindRegistry. Per-node state
    /// lives in the payload pools, never in the Widget.
    class Widget : public RefCounted
    {
    public:
        ~Widget() override = default;

        [[nodiscard]] virtual const char* Name() const noexcept = 0;

        /// Allocate the live payload and bind the blueprint config; runs once per node under the node's owner scope.
        virtual void Instantiate(AppCtx& ctx, detail::Node& blueprint, NodeId id, Owner& owner) = 0;

        /// Whether this kind sizes from content (the bridge calls Measure and pins a Wrap axis). Default false.
        [[nodiscard]] virtual bool ContentSized() const noexcept { return false; }

        /// Whether this content-sized kind reflows within a constrained width: if true the bridge re-measures it against
        /// the width its parent allots after the first solve. Default false.
        [[nodiscard]] virtual bool ReflowsWidth() const noexcept { return false; }

        /// The leaf's natural size for the given available space. Only consulted when ContentSized().
        virtual Vec2 Measure(const MeasureContext&, NodeId, AvailableSpace width, AvailableSpace height)
        {
            (void)width;
            (void)height;
            return {};
        }

        /// Draw the node's own visual onto `canvas`; the emit walk descends into children separately. Default: nothing.
        virtual void Paint(PaintContext&, NodeId, Canvas&)
        {
        }

        /// Draw chrome on top of the children (e.g. scrollbars), in the node's untranslated space: after the children,
        /// inside the outer transform/opacity layer but outside any scroll-offset layer. Default inert.
        virtual void PaintOverlay(PaintContext&, NodeId, Canvas&)
        {
        }

        /// Hit-target mode. Default Opaque: any node with a non-empty solved rect is a target; Ignore opts out.
        [[nodiscard]] virtual HitTestBehavior HitTest(const EventContext&, Vec2) const
        {
            return HitTestBehavior::Opaque;
        }

        /// A routed input event reached this node (capture/target/bubble). Fire the payload's handlers; ev.Consume() stops the route. Default inert.
        virtual void OnEvent(EventContext&, Event&)
        {
        }

        /// Whether this node takes keyboard focus on click. Default: whatever the node's InteractState declares (set
        /// `.on.focusPolicy`); kinds without an InteractState are never focusable.
        [[nodiscard]] virtual bool WantsFocus(const EventContext& ctx) const
        {
            const InteractState* on = detail::InteractOf(*ctx.arena, ctx.Node());
            return on && IsFocusable(on->focusPolicy, on->disabled);
        }

        /// Tab-ring participation. Default derives from WantsFocus (click-focusable means tabIndex 0); controls override
        /// to surface their author-set tabIndex / focus policy.
        struct FocusPolicyResult
        {
            int  tabIndex  = 0;
            bool isTabStop = false;
        };
        [[nodiscard]] virtual FocusPolicyResult TabPolicy(const EventContext& ctx) const
        {
            if (const InteractState* on = detail::InteractOf(*ctx.arena, ctx.Node()))
            {
                return {on->tabIndex, IsTabStop(on->focusPolicy, on->tabIndex, on->disabled)};
            }
            return {0, WantsFocus(ctx)};
        }

        /// Focus ring geometry in the node's solved space: an inset (negative grows the ring) and a corner radius.
        /// Default: tight to the solved rect, square; controls override to match their chrome.
        struct RingShape
        {
            float inset  = 0.f;
            float radius = 0.f;
        };
        [[nodiscard]] virtual RingShape FocusRingShape(const PaintContext&, NodeId) const { return {}; }
    };

    /// Base for config-carrying widgets: performs the blueprint downcast once and hands the concrete
    /// Cfg to Build, so a concrete widget never writes a static_cast.
    template <typename Cfg>
    class TypedWidget : public Widget
    {
        void Instantiate(AppCtx& ctx, detail::Node& blueprint, NodeId id, Owner& owner) final
        {
            Build(ctx, static_cast<detail::TypedNode<Cfg>&>(blueprint).config, id, owner);
        }

    protected:
        virtual void Build(AppCtx& ctx, Cfg& config, NodeId id, Owner& owner) = 0;
    };

    namespace detail
    {
        /// Stamp a transient blueprint node for `kind` carrying `cfg`; the instantiation walk casts it back to
        /// TypedNode<Cfg>, safe because only this Cfg's builder stamps that kind id.
        template <typename Cfg>
        Ref<Node> MakeBlueprint(LiveKind kind, Cfg cfg)
        {
            return MakeRef<TypedNode<Cfg>>(static_cast<KindId>(kind), std::move(cfg));
        }
    } // namespace detail

    /// Default font size for label-only controls (Button/Checkbox/Input): measure + paint share it.
    inline constexpr float kControlFontSize = 14.f;

    /// Framework accent: focus rings and control chrome key off this blue (Color::Blue's value, a literal to stay constexpr).
    inline constexpr Color kAccent{0, 123, 255, 255};
    inline constexpr Color kFocusRing{0, 123, 255, 255};
} // namespace lumora
