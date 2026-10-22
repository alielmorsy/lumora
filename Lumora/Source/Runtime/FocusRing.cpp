#include "Runtime/FocusRing.h"

#include "Input/Interaction.h"
#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Node/View.h"
#include "Runtime/AppCtx.h"
#include "Runtime/ChildOrder.h"
#include "Runtime/Paint.h"
#include "Widgets/Widget.h"

#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Paint/Canvas.h>
#include <lumora/Paint/DrawTypes.h>

namespace lumora
{
    namespace
    {
        constexpr float kRingWidth  = 2.f;
        constexpr float kRingOffset = 2.f; ///< how far outside the widget the ring sits
    } // namespace

    void PaintFocusRing(AppCtx& ctx, const InteractionState& s, Canvas& canvas)
    {
        if (!s.focusVisible)
        {
            return;
        }
        const NodeId id = s.focused.Resolve(ctx.arena);
        if (!id.Valid())
        {
            return;
        }
        const LiveNode& n = ctx.arena.Resolve(id);
        if (n.solved.width <= 0.f || n.solved.height <= 0.f || !Visible(ctx.arena, n))
        {
            return;
        }

        PaintContext      pctx{.arena = &ctx.arena, .fonts = ctx.measurer};
        Widget::RingShape shape{};
        if (Widget* widget = KindRegistry::For(n.kind))
        {
            shape = widget->FocusRingShape(pctx, id);
        }

        // The ring pass has no layers of its own: add the emit walk's scroll-offset/transform translates to the solved rect.
        const Vec2  paint = detail::PaintOffset(ctx.arena, id);
        const float inset = shape.inset - kRingOffset; // negative grows the ring outward
        const Rect  box{n.solved.x + paint.x + inset, n.solved.y + paint.y + inset,
                        n.solved.width - 2.f * inset, n.solved.height - 2.f * inset};

        // If the widget is scrolled out of a Scroll/VirtualList viewport, clip the ring to it or skip it when not intersecting.
        bool hasClip = false;
        Rect clipRect{};
        for (NodeId a = n.parent; a.Valid(); a = ctx.arena.Resolve(a).parent)
        {
            const LiveNode& anc = ctx.arena.Resolve(a);
            if (anc.kind == LiveKind::Scroll || anc.kind == LiveKind::VirtualList)
            {
                const Vec2 ancPaint = detail::PaintOffset(ctx.arena, a);
                const Rect viewport{anc.solved.x + ancPaint.x, anc.solved.y + ancPaint.y,
                                    anc.solved.width, anc.solved.height};
                if (!box.Intersects(viewport))
                {
                    return;
                }
                hasClip  = true;
                clipRect = viewport;
                break;
            }
        }

        if (hasClip)
        {
            canvas.PushClip(clipRect);
        }

        canvas.DrawRect(RectCmd{.bounds       = box,
                                .borderColor  = kFocusRing.WithAlpha(200),
                                .borderWidth  = kRingWidth,
                                .borderRadius = shape.radius + kRingOffset});

        if (hasClip)
        {
            canvas.PopClip();
        }
    }
} // namespace lumora
