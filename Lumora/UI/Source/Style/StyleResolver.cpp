#include "Style/StyleResolver.h"

#include <string>

namespace lumora::style
{
    void ApplyLayout(NodeArena& arena, NodeId id, const ComputedStyle& cs, const LayoutProps& layout)
    {
        LayoutStyle& ls = arena.Resolve(id).style;
        ls.width = cs.Get("width"_sid) ? ToSizeHint(*cs.Get("width"_sid), Wrap) : layout.width.Get();
        ls.height = cs.Get("height"_sid) ? ToSizeHint(*cs.Get("height"_sid), Wrap) : layout.height.Get();
        ls.minWidth = cs.Get("min-width"_sid) ? ToSizeHint(*cs.Get("min-width"_sid), Wrap) : Wrap;
        ls.minHeight = cs.Get("min-height"_sid) ? ToSizeHint(*cs.Get("min-height"_sid), Wrap) : Wrap;
        ls.maxWidth = cs.Get("max-width"_sid) ? ToSizeHint(*cs.Get("max-width"_sid), Wrap) : Wrap;
        ls.maxHeight = cs.Get("max-height"_sid) ? ToSizeHint(*cs.Get("max-height"_sid), Wrap) : Wrap;
        {
            const SpacingSplit s = ToPaddingSplit(cs, layout.padding);
            ls.padding = s.px;
            ls.paddingPct = s.pct;
        }
        {
            const SpacingSplit s = ToMarginSplit(cs, layout.margin);
            ls.margin = s.px;
            ls.marginPct = s.pct;
        }
    }

    void ApplySwitch(NodeArena& arena, NodeId id, const ComputedStyle& cs, const SwitchCfg& cfg)
    {
        ApplyLayout(arena, id, cs, cfg.layout);
        SwitchPayload& sp = arena.PayloadOf<SwitchPayload>(id);
        if (const CssValue* v = cs.Get("background-color"_sid)) sp.activeColor = ToColor(*v, cfg.activeColor.Get());
        else if (const CssValue* v2 = cs.Get("color"_sid)) sp.activeColor = ToColor(*v2, cfg.activeColor.Get());
        else sp.activeColor = cfg.activeColor.Get();
        arena.Resolve(id).MarkDirty(Dirty::All);
    }

    void ApplyCheckbox(NodeArena& arena, NodeId id, const ComputedStyle& cs, const CheckboxCfg& cfg)
    {
        ApplyLayout(arena, id, cs, cfg.layout);
        arena.Resolve(id).MarkDirty(Dirty::All);
    }

    void ApplyText(NodeArena& arena, NodeId id, const ComputedStyle& cs, const TextStyleCfg& cfg)
    {
        LiveNode& node = arena.Resolve(id);
        LayoutStyle& ls = node.style;
        TextPayload& tp = arena.PayloadOf<TextPayload>(id);

        ls.width = cs.Get("width"_sid) ? ToSizeHint(*cs.Get("width"_sid), Wrap) : cfg.layout.width.Get();
        ls.height = cs.Get("height"_sid) ? ToSizeHint(*cs.Get("height"_sid), Wrap) : cfg.layout.height.Get();
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

        tp.color = cs.Get("color"_sid) ? ToColor(*cs.Get("color"_sid), cfg.color.Get()) : cfg.color.Get();

        tp.size = cs.Get("font-size"_sid) ? ToPx(*cs.Get("font-size"_sid), cfg.size.Get()) : cfg.size.Get();

        if (const CssValue* v = cs.Get("font-family"_sid))
        {
            if (v->kind == ValueKind::List && !v->list.empty()) tp.font.family = v->list.front().text;
            else tp.font.family = v->text;
        }
        else tp.font.family = cfg.font.family;

        if (const CssValue* v = cs.Get("font-weight"_sid))
        {
            if (v->kind == ValueKind::Number) tp.font.bold = v->num >= 600.f;
            else tp.font.bold = KeywordIs(*v, "bold"_sid) || KeywordIs(*v, "bolder"_sid);
        }
        else tp.font.bold = cfg.font.bold;

        if (const CssValue* v = cs.Get("font-style"_sid))
            tp.font.italic = KeywordIs(*v, "italic"_sid) || KeywordIs(*v, "oblique"_sid);
        else tp.font.italic = cfg.font.italic;

        if (const CssValue* v = cs.Get("text-decoration"_sid))
        {
            bool strike = false;
            const auto scan = [&](const CssValue& c) { if (KeywordIs(c, "line-through"_sid)) strike = true; };
            if (v->kind == ValueKind::List)
                for (const CssValue& c : v->list) scan(c);
            else scan(*v);
            tp.font.strike = strike;
        }
        else tp.font.strike = cfg.font.strike;

        tp.font.lineHeight = cs.Get("line-height"_sid)
                                 ? ToNumber(*cs.Get("line-height"_sid), cfg.font.lineHeight)
                                 : cfg.font.lineHeight;

        tp.flow.align = cs.Get("text-align"_sid) ? ToAlign(*cs.Get("text-align"_sid), cfg.flow.align) : cfg.flow.align;

        if (const CssValue* v = cs.Get("white-space"_sid))
            tp.flow.wrap = !(KeywordIs(*v, "nowrap"_sid) || KeywordIs(*v, "pre"_sid));
        else tp.flow.wrap = cfg.flow.wrap;

        if (const CssValue* v = cs.Get("text-overflow"_sid)) tp.flow.ellipsis = KeywordIs(*v, "ellipsis"_sid);
        else tp.flow.ellipsis = cfg.flow.ellipsis;

        if (const CssValue* v = cs.Get("-webkit-line-clamp"_sid)) tp.flow.maxLines = static_cast<int>(
            ToNumber(*v, 0.f));
        else if (const CssValue* v2 = cs.Get("max-lines"_sid)) tp.flow.maxLines = static_cast<int>(ToNumber(*v2, 0.f));
        else tp.flow.maxLines = cfg.flow.maxLines;

        ++tp.contentEpoch; // size/font/flow feed measurement - invalidate the measure + glyph caches
        node.MarkDirty(Dirty::All);
    }

    void ApplyButton(NodeArena& arena, NodeId id, const ComputedStyle& cs, const ButtonCfg& cfg)
    {
        LiveNode& node = arena.Resolve(id);
        LayoutStyle& ls = node.style;
        ButtonPayload& bp = arena.PayloadOf<ButtonPayload>(id);

        ls.width = cs.Get("width"_sid) ? ToSizeHint(*cs.Get("width"_sid), Wrap) : cfg.layout.width.Get();
        ls.height = cs.Get("height"_sid) ? ToSizeHint(*cs.Get("height"_sid), Wrap) : cfg.layout.height.Get();
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

        if (const CssValue* v = cs.Get("background-color"_sid)) bp.fill = ToColor(*v, cfg.fill.Get());
        else if (const CssValue* v2 = cs.Get("background"_sid)) bp.fill = ToColor(*v2, cfg.fill.Get());
        else bp.fill = cfg.fill.Get();

        bp.color = cs.Get("color"_sid) ? ToColor(*cs.Get("color"_sid), cfg.color.Get()) : cfg.color.Get();
        bp.radius = cs.Get("border-radius"_sid)
                        ? ToPx(*cs.Get("border-radius"_sid), cfg.radius.Get())
                        : cfg.radius.Get();

        node.MarkDirty(Dirty::All);
    }

    void ApplyInput(NodeArena& arena, NodeId id, const ComputedStyle& cs, const InputCfg& cfg)
    {
        ApplyLayout(arena, id, cs, cfg.layout);

        InputPayload& ip = arena.PayloadOf<InputPayload>(id);
        StyleState& ss = ip.style;

        if (const CssValue* v = cs.Get("background-color"_sid)) ss.fill = ToColor(*v, cfg.style.fill.Get());
        else if (const CssValue* v2 = cs.Get("background"_sid)) ss.fill = ToColor(*v2, cfg.style.fill.Get());
        else ss.fill = cfg.style.fill.Get();

        ss.radius = cs.Get("border-radius"_sid)
                        ? ToPx(*cs.Get("border-radius"_sid), cfg.style.radius.Get())
                        : cfg.style.radius.Get();
        ss.opacity = cs.Get("opacity"_sid)
                         ? ToNumber(*cs.Get("opacity"_sid), cfg.style.opacity.Get())
                         : cfg.style.opacity.Get();
        ss.border = ToBorder(cs, cfg.style.border);

        ip.color = cs.Get("color"_sid) ? ToColor(*cs.Get("color"_sid), cfg.color.Get()) : cfg.color.Get();

        arena.Resolve(id).MarkDirty(Dirty::All);
    }

    void ApplyGrid(NodeArena& arena, NodeId id, const ComputedStyle& cs, const GridCfg& cfg)
    {
        GridPayload& gp = arena.PayloadOf<GridPayload>(id);

        gp.columns = cs.Get("grid-template-columns"_sid)
                         ? ToTrackList(*cs.Get("grid-template-columns"_sid))
                         : cfg.columns.tracks;
        gp.rows =
            cs.Get("grid-template-rows"_sid) ? ToTrackList(*cs.Get("grid-template-rows"_sid)) : cfg.rows.tracks;

        // Grid gaps are px only. `gap` sets both axes (accepts `<row> <col>`); row-gap/column-gap override one axis. Inline
        // GridCfg is origin-0.
        gp.colGap = cfg.colGap >= 0.f ? cfg.colGap : cfg.gap;
        gp.rowGap = cfg.rowGap >= 0.f ? cfg.rowGap : cfg.gap;
        if (const CssValue* v = cs.Get("gap"_sid))
        {
            if (v->kind == ValueKind::List && !v->comma && v->list.size() >= 2)
            {
                gp.rowGap = ToGapSplit(v->list[0], gp.rowGap).px;
                gp.colGap = ToGapSplit(v->list[1], gp.colGap).px;
            }
            else
            {
                const float g = ToGapSplit(*v, gp.rowGap).px;
                gp.rowGap = gp.colGap = g;
            }
        }
        if (const CssValue* v = cs.Get("row-gap"_sid)) gp.rowGap = ToGapSplit(*v, gp.rowGap).px;
        if (const CssValue* v = cs.Get("column-gap"_sid)) gp.colGap = ToGapSplit(*v, gp.colGap).px;

        if (const CssValue* v = cs.Get("grid-auto-rows"_sid))
        {
            const std::vector<TrackSize> t = ToTrackList(*v);
            gp.autoRows = t.empty() ? cfg.autoRows : t.front();
        }
        else
        {
            gp.autoRows = cfg.autoRows;
        }

        arena.Resolve(id).MarkDirty(Dirty::All);
    }
} // namespace lumora::style
