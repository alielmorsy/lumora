#include <lumora/Widgets/Image.h>

#include <lumora/Paint/Canvas.h>
#include <lumora/Paint/ImageProvider.h>

#include "Layout/Measure.h"
#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h"
#include "Runtime/Paint.h"
#include "Style/StyleResolver.h"
#include "Widgets/ImageFit.h"
#include "Widgets/Widget.h"

#include <string>
#include <utility>

#include "lumora/Reactive/Owner.h"

namespace lumora {

namespace {

/// Reconcile the payload's resolved handle with its current src/handle, touching provider refcounts
/// only when the wanted key changes. Idempotent - safe to call every measure and every paint.
void ResolveImage(ImageProvider* provider, NodeArena& arena, NodeId id) {
    ImagePayload& p = arena.PayloadOf<ImagePayload>(id);
    const std::string want = p.handle.Valid() ? std::string{} : p.src;
    if (want != p.heldSrc) {
        if (!p.heldSrc.empty() && provider) {
            provider->Release(p.heldSrc);
        }
        p.heldSrc  = want;
        p.acquired = (!want.empty() && provider) ? provider->Acquire(want) : ImageHandle{};
    }
    p.resolved = p.handle.Valid() ? p.handle : p.acquired;
}

/// The slice of ImageCfg the style resolver re-applies; its effect holds this for the node's lifetime.
struct ImageStyleCfg {
    LayoutProps           layout;
    Reactive<float>       opacity;
    Reactive<float>       radius;
    Reactive<std::string> className;
    Reactive<std::string> inlineCss;
    struct {
        Reactive<bool> disabled;
    } on;
};

class ImageWidget final : public TypedWidget<ImageCfg> {
    [[nodiscard]] const char* Name() const noexcept override { return "Image"; }
    [[nodiscard]] bool        ContentSized() const noexcept override { return true; }

    void Build(AppCtx& ctx, ImageCfg& cfg, NodeId id, Owner& owner) override {
        const std::uint32_t slot = ctx.arena.Pool<ImagePayload>(LiveKind::Image).Alloc();
        ctx.arena.Resolve(id).payload = PayloadRef{LiveKind::Image, slot};
        BindProp(ctx.arena, id, cfg.fit,
                 [](NodeArena& a, NodeId n, const ImageFit& v) { a.PayloadOf<ImagePayload>(n).fit = v; },
                 Dirty::Repaint);

        BindProp(ctx.arena, id, cfg.src,
                 [](NodeArena& a, NodeId n, const std::string& v) { a.PayloadOf<ImagePayload>(n).src = v; },
                 Dirty::Layout);
        BindProp(ctx.arena, id, cfg.handle,
                 [](NodeArena& a, NodeId n, const ImageHandle& v) { a.PayloadOf<ImagePayload>(n).handle = v; },
                 Dirty::Layout);
        if (ctx.style) {
            // Sole writer of size, spacing, opacity and radius: CSS wins, the inline cfg is origin-0. BindLayoutProps still
            // seeds the structural fields once; width/height are left to the resolver so two writers never race.
            LayoutProps seed = cfg.layout;
            seed.width       = Wrap;
            seed.height      = Wrap;
            BindLayoutProps(ctx.arena, id, seed);
            style::BindClass(ctx, id, ImageStyleCfg{cfg.layout, cfg.opacity, cfg.radius, cfg.className, cfg.inlineCss, {cfg.on.disabled}},
                             [](NodeArena& a, NodeId n, const style::ComputedStyle& cs, const ImageStyleCfg& c) {
                                 style::ApplyLayout(a, n, cs, c.layout);
                                 ImagePayload& p = a.PayloadOf<ImagePayload>(n);
                                 p.opacity = cs.Get("opacity"_sid) ? style::ToNumber(*cs.Get("opacity"_sid), c.opacity.Get()) : c.opacity.Get();
                                 p.radius  = cs.Get("border-radius"_sid) ? style::ToPx(*cs.Get("border-radius"_sid), c.radius.Get()) : c.radius.Get();
                                 a.Resolve(n).MarkDirty(Dirty::All);
                             });
        } else {
            BindProp(ctx.arena, id, cfg.opacity,
                     [](NodeArena& a, NodeId n, const float& v) { a.PayloadOf<ImagePayload>(n).opacity = v; },
                     Dirty::Style);
            BindProp(ctx.arena, id, cfg.radius,
                     [](NodeArena& a, NodeId n, const float& v) { a.PayloadOf<ImagePayload>(n).radius = v; },
                     Dirty::Style);
            BindLayoutProps(ctx.arena, id, cfg.layout);
        }
        BindViewProps<ImagePayload>(ctx.arena, id, cfg.view, &ImagePayload::view);
        BindInteractProps<ImagePayload>(ctx, id, cfg.on, &ImagePayload::on);

        // Registered after the arena's node-Free cleanup, so LIFO runs first: the payload is still
        // alive to read heldSrc.
        ImageProvider* provider = ctx.images;
        NodeArena*     arena    = &ctx.arena;
        owner.AddCleanup([provider, arena, id] {
            ImagePayload& p = arena->PayloadOf<ImagePayload>(id);
            if (!p.heldSrc.empty() && provider) {
                provider->Release(p.heldSrc);
            }
        });
    }

    Vec2 Measure(const MeasureContext& ctx, NodeId id, AvailableSpace, AvailableSpace) override {
        if (!ctx.arena) {
            return {};
        }
        ResolveImage(ctx.images, *ctx.arena, id);
        const ImageHandle h = ctx.arena->PayloadOf<ImagePayload>(id).resolved;
        return h.Valid() ? Vec2{h.width, h.height} : Vec2{};
    }

    void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override {
        if (!ctx.arena) {
            return;
        }
        ResolveImage(ctx.images, *ctx.arena, id);
        const ImagePayload& p = ctx.arena->PayloadOf<ImagePayload>(id);
        if (!p.resolved.Valid()) {
            return;
        }
        const Rect content = ctx.arena->Resolve(id).solved;
        const Rect dst      = ImageFitRect(p.fit, content, {p.resolved.width, p.resolved.height});
        const bool clip     = ImageFitClips(p.fit);
        if (clip) {
            canvas.PushClip(content, p.radius);
        }
        canvas.DrawImage(p.resolved, dst, p.opacity, p.radius);
        if (clip) {
            canvas.PopClip();
        }
    }

    void OnEvent(EventContext& ctx, Event& ev) override {
        DispatchInteract(ctx, ev, ctx.Payload<ImagePayload>().on);
    }
};

void EnsureKind() {
    static const bool registered = [] {
        KindRegistry::Register(LiveKind::Image, MakeRef<ImageWidget>());
        return true;
    }();
    (void)registered;
}

} // namespace

Elem Image(ImageCfg cfg) {
    EnsureKind();
    return Elem(detail::MakeBlueprint(LiveKind::Image, std::move(cfg)));
}

} // namespace lumora
