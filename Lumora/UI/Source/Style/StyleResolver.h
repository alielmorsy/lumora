#pragma once

#include "Style/Cascade.h"
#include "Style/ComputedStyle.h"
#include "Style/Convert.h"
#include "Style/Keyframes.h"
#include "Style/Resolve.h"
#include "Style/StyleEngine.h"
#include "Style/Transition.h"

#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Instantiate.h" // ResolveCrossAlign

#include <lumora/Reactive/Effect.h>
#include <lumora/Widgets/Controls.h> // ButtonCfg
#include <lumora/Widgets/Input.h>    // InputCfg
#include <lumora/Widgets/Text.h>     // TextCfg

#include <string>
#include <utility>

namespace lumora::style
{
    /// Per-node style-resolution effect; sole writer of the node's styleable fields. Re-runs on sheet generation, className,
    /// ancestor class epochs and pseudo-state; `apply` writes the CSS value or the inline cfg (origin-0). `Cfg` needs reactive `.className`;
    /// an optional reactive `.inlineCss` declaration string beats the rules (except `!important`).
    template <typename Cfg, typename Apply>
    void BindClass(AppCtx& ctx, NodeId id, const Cfg& cfg, Apply apply)
    {
        StyleEngine* eng = ctx.style;
        if (!eng) return;
        NodeArena* arena = &ctx.arena;

        // Marked before any child is instantiated so a descendant's ParentInherited walk can tell "resolves, not published yet"
        // from "never resolves".
        eng->NodeState(id).participates = true;

        Effect([arena, eng, id, cfg, apply = std::move(apply)]
        {
            eng->Generation()(); // dep: hot-reload / SetStylesheet
            eng->MediaState()(); // dep: @media breakpoint crossing
            const std::string& cls = cfg.className.Get(); // dep: className (if reactive)

            CssNodeState& st = eng->NodeState(id);
            std::vector<StringId> parsed = SplitClasses(cls);
            if (parsed != st.classes)
            {
                st.classes = std::move(parsed);
                st.classEpoch.Write(st.classEpoch.Peek() + 1); // notify descendants (combinator reactivity)
            }

            if (FrameCounters* counters = eng->Counters())
            {
                ++counters->cascadeCalls;
            }
            const std::vector<const Rule*> mediaRules = eng->PassingMediaRules();
            ComputedStyle cs = Cascade(id.value, eng->MakeOps(*arena), eng->Index(), mediaRules);

            if constexpr (requires { cfg.inlineCss.Get(); })
            {
                const std::string& inl = cfg.inlineCss.Get(); // dep: inlineCss (if reactive)
                if (inl != st.inlineSource)
                {
                    st.inlineSource = inl;
                    st.inlineDecls = inl.empty() ? std::vector<Declaration>{} : ParseInlineDeclarations(inl);
                }
                ApplyDeclarations(cs, st.inlineDecls);
            }

            // Pipeline: vars -> font-size/units -> inheritance, all before apply.
            const InheritedStyle* parentInh = eng->ParentInherited(*arena, id); // subscribes to parent inheritEpoch
            VarMap vars = parentInh ? parentInh->vars : eng->RootVars();
            for (const auto& [name, val] : cs.customProps) vars[name] = val; // node's own --x override
            ResolveVars(cs, vars);
            const float fontSize = ResolveFontSize(cs, parentInh, eng->RootFontSize());
            const Vec2 vp = UsesViewportUnits(cs) ? eng->Viewport()() : eng->Viewport().Peek();
            ComputeUnits(cs, ComputeContext{fontSize, eng->RootFontSize(), vp});
            InheritedStyle out = ApplyInheritance(cs, parentInh, vars);

            apply(*arena, id, cs, cfg);
            st.resolved = true;
            st.current = std::move(cs);

            // Publish inherited output for descendants; equality-gated so we only wake them on a change.
            if (!(out == st.inheritedOut))
            {
                st.inheritedOut = std::move(out);
                st.inheritEpoch.Write(st.inheritEpoch.Peek() + 1);
            }
        });

        // Mirror the node's disabled prop into its :disabled pseudo signal (separate effect: the resolver reads, this writes).
        // Cfgs with an interaction group hold it there, leaf controls at top level, so match on `.Get()` not the name.
        // A static prop is written once instead: an effect per node that can never re-run is pure overhead.
        const auto mirrorDisabled = [eng, id](const auto& d)
        {
            if (d.IsStatic())
            {
                if (d.Get())
                {
                    eng->SetPseudo(id, Pseudo::Disabled, true);
                }
                return;
            }
            Effect([eng, id, d] { eng->SetPseudo(id, Pseudo::Disabled, d.Get()); });
        };
        if constexpr (requires { cfg.on.disabled.Get(); })
        {
            mirrorDisabled(cfg.on.disabled);
        }
        else if constexpr (requires { cfg.disabled.Get(); })
        {
            mirrorDisabled(cfg.disabled);
        }

        OnCleanup([eng, id] { eng->ForgetNode(id); });
    }

    /// Apply the box property surface (layout, paint, visibility/clip, transform translate) onto a container-like node; CSS
    /// wins, inline cfg is origin-0. Percent padding/margin/gap go to the layout header's percent fields.
    template <typename Payload, typename Cfg>
    void ApplyBoxLike(NodeArena& arena, NodeId id, const ComputedStyle& cs, const Cfg& cfg,
                      StyleState Payload::* stylePtr, ViewState Payload::* viewPtr,
                      TransformState Payload::* transPtr, FlexAxis baseAxis, CssNodeState& css)
    {
        LiveNode& node = arena.Resolve(id);
        LayoutStyle& ls = node.style;
        Payload& pl = arena.PayloadOf<Payload>(id);
        StyleState& ss = pl.*stylePtr;
        ViewState& vs = pl.*viewPtr;
        TransformState& ts = pl.*transPtr;
        // What was on screen before this resolve: seeds a Motion created now, so a transition a class toggle introduces still
        // animates from the old value.
        const StyleState prevStyle = ss;
        const TransformState prevTransform = ts;

        ls.width = cs.Get("width"_sid) ? ToSizeHint(*cs.Get("width"_sid), Wrap) : cfg.layout.width.Get();
        ls.height = cs.Get("height"_sid) ? ToSizeHint(*cs.Get("height"_sid), Wrap) : cfg.layout.height.Get();
        ls.minWidth = cs.Get("min-width"_sid) ? ToSizeHint(*cs.Get("min-width"_sid), Wrap) : cfg.layout.minWidth;
        ls.minHeight = cs.Get("min-height"_sid) ? ToSizeHint(*cs.Get("min-height"_sid), Wrap) : cfg.layout.minHeight;
        ls.maxWidth = cs.Get("max-width"_sid) ? ToSizeHint(*cs.Get("max-width"_sid), Wrap) : cfg.layout.maxWidth;
        ls.maxHeight = cs.Get("max-height"_sid) ? ToSizeHint(*cs.Get("max-height"_sid), Wrap) : cfg.layout.maxHeight;

        // aspect-ratio is not a solver input; decide here which axis depends on the other, because the post-solve pass
        // overwrites the dependent one with a Fixed px and would read it back as authored.
        ls.aspectRatio = cs.Get("aspect-ratio"_sid) ? ToAspectRatio(*cs.Get("aspect-ratio"_sid)) : 0.f;
        if (ls.aspectRatio > 0.f)
        {
            const bool widthAuto = ls.width.kind == SizeHint::Kind::Wrap;
            const bool heightAuto = ls.height.kind == SizeHint::Kind::Wrap;
            ls.aspectPin = heightAuto
                               ? AspectPin::Height
                               : widthAuto
                               ? AspectPin::Width
                               : AspectPin::None;
            arena.TrackAspect(id);
        }
        else
        {
            ls.aspectPin = AspectPin::None;
        }

        {
            const SpacingSplit s = ToPaddingSplit(cs, cfg.layout.padding);
            ls.padding = s.px;
            ls.paddingPct = s.pct;
        }
        {
            const SpacingSplit s = ToMarginSplit(cs, cfg.layout.margin);
            ls.margin = s.px;
            ls.marginPct = s.pct;
        }
        {
            const CssValue* g = cs.Get("gap"_sid);
            if (!g) g = cs.Get("row-gap"_sid);
            if (!g) g = cs.Get("column-gap"_sid);
            const GapSplit gs = g ? ToGapSplit(*g, 0.f) : GapSplit{cfg.layout.gap.Get(), 0.f};
            ls.gap = gs.px;
            ls.gapPct = gs.pct;
        }

        ls.main = cs.Get("justify-content"_sid)
                      ? ToAlign(*cs.Get("justify-content"_sid), cfg.layout.main)
                      : cfg.layout.main;
        const Align crossFb = ResolveCrossAlign(node.kind, cfg.layout.cross);
        ls.cross = cs.Get("align-items"_sid) ? ToAlign(*cs.Get("align-items"_sid), crossFb) : crossFb;
        // flex-direction flips only a Row/Column base (never Stack/Grid); reverts to `baseAxis` when the rule stops matching.
        if (const CssValue* v = cs.Get("flex-direction"_sid); v && (baseAxis == FlexAxis::Row || baseAxis ==
            FlexAxis::Column))
            ls.axis = ToFlexAxis(*v, baseAxis);
        else
            ls.axis = baseAxis;

        // Lumora is rigid by default (flex-shrink 0 overflows, never crushes); CSS flex-shrink opts in.
        {
            float grow = cfg.layout.flexWeight, shrink = cfg.layout.flexShrink;
            SizeHint basis = cfg.layout.flexBasis;
            if (const CssValue* v = cs.Get("flex"_sid))
            {
                const FlexShorthand f = ToFlex(*v);
                grow = f.grow;
                shrink = f.shrink;
                basis = f.basis;
            }
            if (const CssValue* v = cs.Get("flex-grow"_sid)) grow = ToNumber(*v, grow);
            if (const CssValue* v = cs.Get("flex-shrink"_sid)) shrink = ToNumber(*v, shrink);
            if (const CssValue* v = cs.Get("flex-basis"_sid)) basis = ToSizeHint(*v, basis);
            ls.flexWeight = grow;
            ls.flexShrink = shrink;
            ls.flexBasis = basis;
        }
        ls.wrap = cs.Get("flex-wrap"_sid) ? ToFlexWrap(*cs.Get("flex-wrap"_sid), cfg.layout.wrap) : cfg.layout.wrap;
        ls.alignSelf = cs.Get("align-self"_sid)
                           ? ToAlign(*cs.Get("align-self"_sid), cfg.layout.alignSelf)
                           : cfg.layout.alignSelf;
        ls.alignContent = cs.Get("align-content"_sid)
                              ? ToAlign(*cs.Get("align-content"_sid), cfg.layout.alignContent)
                              : cfg.layout.alignContent;
        ls.order = cs.Get("order"_sid)
                       ? static_cast<int>(ToNumber(*cs.Get("order"_sid), static_cast<float>(cfg.layout.order)))
                       : cfg.layout.order;

        // Position/insets/z-index: CSS wins, inline PositionProps is origin-0. Only cfgs carrying PositionProps (Box/Grid/Cell)
        // can be positioned via CSS.
        if constexpr (requires { cfg.position; })
        {
            const CssValue* p = cs.Get("position"_sid);
            ls.position = p ? ToPosition(*p, cfg.position.position) : cfg.position.position;
            if (p && (KeywordIs(*p, "relative"_sid) || KeywordIs(*p, "sticky"_sid)))
                ls.anchor = true; // establishes a containing block for absolute descendants (no offset shift)

            // Insets: when an edge is absent LEAVE what BindPositionProps wrote (inline inset, `offset`, reactive per-edge signal);
            // overwriting it would wipe animated insets.
            const CssValue* shb = cs.Get("inset"_sid);
            const std::vector<CssValue>* shl =
                (shb && shb->kind == ValueKind::List && !shb->comma) ? &shb->list : nullptr;
            const auto shorthandEdge = [&](int side) -> const CssValue*
            {
                if (shl && !shl->empty())
                {
                    const int n = static_cast<int>(shl->size());
                    const int idx = n == 1
                                        ? 0
                                        : n == 2
                                        ? (side % 2 == 0 ? 0 : 1) // {v, h}
                                        : n == 3
                                        ? (side == 0 ? 0 : side == 2 ? 2 : 1) // {t, h, b}
                                        : side; // {t, r, b, l}
                    return idx < n ? &(*shl)[idx] : nullptr;
                }
                return (shb && shb->kind != ValueKind::List) ? shb : nullptr; // single value → all edges
            };
            const auto applyEdge = [&](StringId longhand, int side, float& dst)
            {
                if (const CssValue* v = cs.Get(longhand)) dst = ToInsetEdge(*v, dst);
                else if (const CssValue* v = shorthandEdge(side)) dst = ToInsetEdge(*v, dst);
            };
            applyEdge("top"_sid, 0, ls.inset.top);
            applyEdge("right"_sid, 1, ls.inset.right);
            applyEdge("bottom"_sid, 2, ls.inset.bottom);
            applyEdge("left"_sid, 3, ls.inset.left);
            if (const CssValue* v = cs.Get("z-index"_sid)) ls.z = static_cast<int>(ToNumber(
                *v, static_cast<float>(ls.z)));
        }

        // grid-column/grid-row: ignored unless the parent is a Grid. Base is the inline Cell placement (origin-0) or auto,
        // rebuilt each run so a removed rule reverts.
        {
            GridItemPlacement place{};
            if constexpr (requires { cfg.col; cfg.row; cfg.colSpan; cfg.rowSpan; })
                place = GridItemPlacement{
                    static_cast<std::int16_t>(cfg.col), static_cast<std::int16_t>(cfg.row),
                    static_cast<std::int16_t>(cfg.colSpan < 1 ? 1 : cfg.colSpan),
                    static_cast<std::int16_t>(cfg.rowSpan < 1 ? 1 : cfg.rowSpan)
                };
            if (const CssValue* v = cs.Get("grid-column"_sid))
            {
                const GridLine g = ToGridLine(*v);
                place.col = static_cast<std::int16_t>(g.start);
                place.colSpan = static_cast<std::int16_t>(g.span < 1 ? 1 : g.span);
            }
            if (const CssValue* v = cs.Get("grid-row"_sid))
            {
                const GridLine g = ToGridLine(*v);
                place.row = static_cast<std::int16_t>(g.start);
                place.rowSpan = static_cast<std::int16_t>(g.span < 1 ? 1 : g.span);
            }
            ls.placement = place;
        }

        if (const CssValue* v = cs.Get("background-color"_sid)) ss.fill = ToColor(*v, Color::Transparent);
        else if (const CssValue* v2 = cs.Get("background"_sid)) ss.fill = ToColor(*v2, Color::Transparent);
        else ss.fill = cfg.style.fill.Get();
        ss.hoverFill = cfg.style.hoverFill.Get();
        ss.radius = cs.Get("border-radius"_sid) ? ToPx(*cs.Get("border-radius"_sid), 0.f) : cfg.style.radius.Get();
        ss.opacity = cs.Get("opacity"_sid) ? ToNumber(*cs.Get("opacity"_sid), 1.f) : cfg.style.opacity.Get();
        ss.border = ToBorder(cs, cfg.style.border);

        bool visible = cfg.view.visible.Get();
        ls.display = DisplayMode::Flex; // reset each run so removing a `display:block` class reverts to flex
        if (const CssValue* v = cs.Get("display"_sid))
        {
            if (KeywordIs(*v, "none"_sid)) visible = false;
            else if (KeywordIs(*v, "block"_sid) || KeywordIs(*v, "inline-block"_sid))
                ls.display = DisplayMode::Block; // routes to Masharif normal-flow (see MasharifBackend::ApplyStyle)
            // flex / inline-flex / grid keep Flex - grid layout is carried by the Grid widget's FlexAxis::Grid
        }
        if (const CssValue* v = cs.Get("visibility"_sid))
        {
            if (KeywordIs(*v, "hidden"_sid) || KeywordIs(*v, "collapse"_sid)) visible = false;
            else if (KeywordIs(*v, "visible"_sid)) visible = true;
        }
        vs.visible = visible;
        if (const CssValue* v = cs.Get("overflow"_sid))
            vs.clip = KeywordIs(*v, "hidden"_sid) || KeywordIs(*v, "clip"_sid) || KeywordIs(*v, "scroll"_sid) ||
                KeywordIs(*v, "auto"_sid);
        else vs.clip = cfg.view.clip;

        if (const CssValue* v = cs.Get("transform"_sid))
        {
            const TransformValue tr = ToTransform(*v);
            ts.x = tr.x;
            ts.y = tr.y;
            ts.scaleX = tr.scaleX;
            ts.scaleY = tr.scaleY;
            ts.rotate = tr.rotate;
        }
        else
        {
            ts.x = cfg.transform.x.Get();
            ts.y = cfg.transform.y.Get();
            ts.scaleX = ts.scaleY = 1.f;
            ts.rotate = 0.f;
        }
        {
            const Vec2 o = cs.Get("transform-origin"_sid) ? ToTransformOrigin(*cs.Get("transform-origin"_sid), {0.5f, 0.5f})
                                                          : Vec2{0.5f, 0.5f};
            ts.originX = o.x;
            ts.originY = o.y;
        }

        // Transitions/@keyframes: parse the specs; StyleEngine's single driver advances runs. The transition intercept starts a
        // run from the displayed value when a transitioned target changes.
        const CssValue* transition = cs.Get("transition"_sid);
        const CssValue* animation = cs.Get("animation"_sid);
        if (!css.motion && (transition || animation))
        {
            css.motion = std::make_unique<CssNodeState::Motion>();
            if (css.resolved)
            {
                StyleState seedStyle = prevStyle;
                TransformState seedTransform = prevTransform;
                InterceptBox(css.motion->transitions, seedStyle, seedTransform);
            }
        }
        if (css.motion)
        {
            ParseTransitions(transition ? *transition : CssValue{}, css.motion->transitions);
            InterceptBox(css.motion->transitions, ss, ts);
            ParseAnimation(animation ? *animation : CssValue{}, css.motion->anim);
        }

        node.MarkDirty(Dirty::All);
    }

    /// Advance one node's transitions and @keyframes animation, writing interpolated paint fields; animation wins over a
    /// transition on the same field. Member pointers are template arguments so StyleEngine holds it as a bare function pointer.
    /// Returns whether to keep driving the node.
    template <typename Payload, StyleState Payload::* StylePtr, TransformState Payload::* TransPtr>
    bool AdvanceBoxNode(NodeArena& arena, NodeId id, CssNodeState& state, const KeyframesDef* keyframes, float dt)
    {
        LiveNode* node = arena.TryResolve(id);
        if (!node || !node->payload.Valid())
        {
            return false;
        }
        if (!state.motion)
        {
            return false;
        }
        Payload& payload = arena.PayloadOf<Payload>(id);
        AdvanceResult result = AdvanceBox(state.motion->transitions, payload.*StylePtr, payload.*TransPtr, dt);
        if (state.motion->anim.spec.enabled && keyframes)
        {
            result |= AdvanceAnimation(state.motion->anim, *keyframes, payload.*StylePtr, payload.*TransPtr, dt);
        }
        if (result.wrote)
        {
            node->MarkDirty(Dirty::Style | Dirty::Repaint);
        }
        return result.active;
    }

    /// Enrol a box-like node with the animation driver, only when this resolve actually started a run. Call from the apply
    /// lambda right after ApplyBoxLike (the only place a run can begin).
    template <typename Payload, StyleState Payload::* StylePtr, TransformState Payload::* TransPtr>
    void EnrolBoxAnimator(StyleEngine& engine, NodeArena& arena, NodeId id)
    {
        CssNodeState& state = engine.NodeState(id);
        if (!state.motion || state.motion->transitions.driverRegistered)
        {
            return; // nothing declared, or already being driven
        }
        if (!AnyActive(state.motion->transitions) && !state.motion->anim.spec.enabled)
        {
            return; // nothing to advance
        }
        state.motion->transitions.driverRegistered = true;
        engine.Enrol(id, arena.GenerationOf(id), &AdvanceBoxNode<Payload, StylePtr, TransPtr>);
    }

    /// Layout-header size/spacing from CSS-or-inline, shared by the leaf-widget resolvers (no child layout to align).
    void ApplyLayout(NodeArena& arena, NodeId id, const ComputedStyle& cs, const LayoutProps& layout);

    /// The slice of TextCfg the resolver re-applies; its effect holds this for the node's lifetime, so handlers and the selection
    /// handle (bound once onto the payload) stay out of it.
    struct TextStyleCfg
    {
        Reactive<float> size;
        Reactive<Color> color;
        FontProps font;
        TextFlowProps flow;
        LayoutProps layout;
        Reactive<std::string> className;
        Reactive<std::string> inlineCss;
        struct
        {
            Reactive<bool> disabled;
        } on;
    };

    /// Text has no StyleState: writes the TextPayload (colour/font/flow) + layout header.
    void ApplyText(NodeArena& arena, NodeId id, const ComputedStyle& cs, const TextStyleCfg& cfg);

    /// Button: writes its payload fields (fill/color/radius) + layout.
    void ApplyButton(NodeArena& arena, NodeId id, const ComputedStyle& cs, const ButtonCfg& cfg);

    /// Input: writes InputPayload (fill/radius/border/opacity, text/caret colour) + layout; no ViewState/TransformState, so
    /// ApplyBoxLike is unusable.
    void ApplyInput(NodeArena& arena, NodeId id, const ComputedStyle& cs, const InputCfg& cfg);

    /// Switch: layout + the checked-track colour (background-color / color → activeColor).
    void ApplySwitch(NodeArena& arena, NodeId id, const ComputedStyle& cs, const SwitchCfg& cfg);

    /// Checkbox: layout only.
    void ApplyCheckbox(NodeArena& arena, NodeId id, const ComputedStyle& cs, const CheckboxCfg& cfg);

    /// Grid container: writes GridPayload track templates/gaps/auto-rows from CSS, falling back to the inline GridCfg
    /// (origin-0). Call after ApplyBoxLike; `grid-column`/`grid-row` on children land via ApplyBoxLike.
    void ApplyGrid(NodeArena& arena, NodeId id, const ComputedStyle& cs, const GridCfg& cfg);
} // namespace lumora::style
