#include "Runtime/DebugOverlay.h"

#include "Input/Interaction.h"
#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Node/Payloads.h"
#include "Node/View.h"
#include "Runtime/AppCtx.h"
#include "Runtime/Paint.h"
#include "Widgets/TextEmit.h"
#include "Widgets/Widget.h"

#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Paint/Canvas.h>
#include <lumora/Paint/DrawTypes.h>

#include <array>
#include <cmath>
#include <format>
#include <string>
#include <vector>

// DevTools-style inspector: the walk draws only a faint depth-tinted skeleton per node, and the rich detail (box model,
// spacing, gaps, badges, glyph borders) is concentrated on the hovered node. The dense per-node layers are flags, off by default.

namespace lumora {

namespace {

// Skeleton outline colors cycle by tree depth; low-alpha, 1px.
constexpr std::array<Color, 6> kDepth = {{
    {0, 150, 255, 90},
    {90, 200, 120, 90},
    {245, 170, 70, 90},
    {235, 110, 220, 90},
    {60, 210, 220, 90},
    {250, 210, 90, 90},
}};

constexpr Color kMarginFill = {240, 160, 60, 85};
constexpr Color kPadFill    = {90, 200, 120, 95};
constexpr Color kGapFill    = {170, 110, 230, 110};
constexpr Color kClipLine   = {0, 210, 220, 200};
constexpr Color kGlyphLine  = {235, 90, 220, 150};
constexpr Color kFocusLine  = {255, 90, 90, 255};
constexpr Color kChildLine  = {255, 255, 255, 75};
constexpr Color kTagBg      = {30, 30, 36, 225};
constexpr Color kTagText    = {235, 235, 240, 255};
constexpr Color kEdgeText   = {25, 22, 18, 255};
constexpr Color kTipBg      = {18, 19, 24, 235};
constexpr Color kTipText    = {236, 238, 245, 255};
constexpr Color kTipDim     = {150, 156, 168, 255};

constexpr float kEdgeFont = 10.f;
constexpr float kTipFont  = 11.f;

struct Overlay {
    NodeArena&        arena;
    Canvas&           canvas;
    PaintContext      pctx;
    DebugOverlayFlags flags;
    Vec2              surface;
    std::vector<Rect> glyphScratch;
};

[[nodiscard]] std::string Num(float v) {
    return std::to_string(static_cast<int>(std::lround(v)));
}

void Stroke(Canvas& c, const Rect& r, Color col, float width) {
    if (r.width <= 0.f || r.height <= 0.f) {
        return;
    }
    c.DrawRect(RectCmd{.bounds = r, .borderColor = col, .borderWidth = width});
}

void Fill(Canvas& c, const Rect& r, Color col) {
    if (r.width <= 0.f || r.height <= 0.f) {
        return;
    }
    c.DrawRect(RectCmd{.bounds = r, .fill = col});
}

/// Fills the band between an outer rect and a contained inner rect as four edge strips, so tints never double up.
void FillRing(Canvas& c, const Rect& outer, const Rect& inner, Color col) {
    Fill(c, Rect{outer.x, outer.y, outer.width, inner.y - outer.y}, col);
    Fill(c, Rect{outer.x, inner.Bottom(), outer.width, outer.Bottom() - inner.Bottom()}, col);
    Fill(c, Rect{outer.x, inner.y, inner.x - outer.x, inner.height}, col);
    Fill(c, Rect{inner.Right(), inner.y, outer.Right() - inner.Right(), inner.height}, col);
}

void DrawText(Canvas& c, PaintContext& pctx, float surfaceW, Vec2 pos, std::string_view text, float size,
              Color col) {
    const Rect box{pos.x, pos.y, surfaceW, size * 2.f};
    EmitText(c, pctx, box, Spacing{}, text, FontProps{}, size, TextFlowProps{}, col, nullptr, Align::Start,
             Align::Start, false);
}

void Label(Overlay& o, Vec2 pos, std::string_view text, float size, Color col) {
    DrawText(o.canvas, o.pctx, o.surface.x, pos, text, size, col);
}

/// Small dark chip with a short caption, so a number stays readable over a colourful UI.
void Chip(Overlay& o, Vec2 at, const std::string& text, Color textCol) {
    const float w = static_cast<float>(text.size()) * kEdgeFont * 0.6f + 6.f;
    const Rect  bg{at.x, at.y, w, kEdgeFont + 4.f};
    Fill(o.canvas, bg, kTagBg);
    Label(o, {at.x + 3.f, at.y + 2.f}, text, kEdgeFont, textCol);
}

[[nodiscard]] const char* AxisName(FlexAxis a) {
    switch (a) {
        case FlexAxis::Row:    return "Row";
        case FlexAxis::Column: return "Col";
        case FlexAxis::Stack:  return "Stk";
        case FlexAxis::Grid:   return "Grd";
    }
    return "?";
}

[[nodiscard]] char AlignInitial(Align a) {
    switch (a) {
        case Align::Start:   return 'S';
        case Align::Center:  return 'C';
        case Align::End:     return 'E';
        case Align::Stretch: return 'X';
        case Align::Between:  return 'B';
        case Align::Around:   return 'A';
        case Align::Evenly:   return 'V';
        case Align::Auto:     return '-';
    }
    return '?';
}

[[nodiscard]] std::string HintStr(const SizeHint& h) {
    switch (h.kind) {
        case SizeHint::Kind::Fixed:   return Num(h.value);
        case SizeHint::Kind::Percent: return Num(h.value) + "%";
        case SizeHint::Kind::Expand:  return "grow";
        case SizeHint::Kind::Wrap:    return "wrap";
    }
    return "?";
}

[[nodiscard]] bool Clips(NodeArena& arena, const LiveNode& n) {
    if (n.kind == LiveKind::Scroll) {
        return true;
    }
    const ViewState* v = ViewOf(arena, n);
    return v && v->clip;
}

[[nodiscard]] bool IsContainer(LiveKind k) {
    return k == LiveKind::Box || k == LiveKind::Row || k == LiveKind::Column || k == LiveKind::Stack
        || k == LiveKind::Scroll || k == LiveKind::Grid || k == LiveKind::Cell
        || k == LiveKind::VirtualList;
}

void DrawGaps(Overlay& o, const LiveNode& n) {
    if (n.style.gap <= 0.f || (n.style.axis != FlexAxis::Row && n.style.axis != FlexAxis::Column)) {
        return;
    }
    const LiveNode* prev = nullptr;
    for (NodeId c = n.firstChild; c.Valid(); c = o.arena.Resolve(c).nextSibling) {
        const LiveNode& ch = o.arena.Resolve(c);
        if (ch.style.position != Position::Flow) {
            continue;
        }
        if (prev) {
            if (n.style.axis == FlexAxis::Row) {
                const float x0 = prev->solved.Right();
                Fill(o.canvas, Rect{x0, n.solved.y, ch.solved.Left() - x0, n.solved.height}, kGapFill);
            } else {
                const float y0 = prev->solved.Bottom();
                Fill(o.canvas, Rect{n.solved.x, y0, n.solved.width, ch.solved.Top() - y0}, kGapFill);
            }
        }
        prev = &ch;
    }
}

void DrawAxisTag(Overlay& o, const LiveNode& n) {
    if (!IsContainer(n.kind)) {
        return;
    }
    const std::string tag = std::string(AxisName(n.style.axis)) + ' ' + AlignInitial(n.style.main) + '/'
                          + AlignInitial(n.style.cross);
    Chip(o, n.solved.Position(), tag, kTagText);
}

void DrawGlyphBorders(Overlay& o, const LiveNode& n) {
    if (n.kind != LiveKind::Text) {
        return;
    }
    TextPayload& t        = o.arena.PayloadOf<TextPayload>(n.id);
    const bool   wrapToBox = t.flow.wrap && n.style.width.kind != SizeHint::Kind::Wrap;
    o.glyphScratch.clear();
    CollectTextGlyphRects(o.pctx, n.solved, n.style.padding, t.text, t.font, t.size, t.flow, &t,
                          Align::Start, Align::Start, wrapToBox, o.glyphScratch);
    for (const Rect& g : o.glyphScratch) {
        Stroke(o.canvas, g, kGlyphLine, 1.f);
    }
}

void DrawBoxModelFills(Overlay& o, const LiveNode& n) {
    const Rect content = n.solved;
    FillRing(o.canvas, n.style.margin.Outset(content), content, kMarginFill);
    FillRing(o.canvas, content, n.style.padding.Inset(content), kPadFill);
}

void Walk(Overlay& o, NodeId id, int depth) {
    if (!id.Valid()) {
        return;
    }
    const LiveNode& n = o.arena.Resolve(id);

    if (IsTransparent(n.kind)) {
        for (NodeId c = n.firstChild; c.Valid(); c = o.arena.Resolve(c).nextSibling) {
            Walk(o, c, depth);
        }
        return;
    }
    if (!Visible(o.arena, n)) {
        return;
    }

    if (HasFlag(o.flags, DebugOverlayFlags::Boxes)) {
        Stroke(o.canvas, n.solved, kDepth[static_cast<std::size_t>(depth) % kDepth.size()], 1.f);
    }
    if (HasFlag(o.flags, DebugOverlayFlags::ClipRegions) && Clips(o.arena, n)) {
        Stroke(o.canvas, n.solved, kClipLine, 2.f);
    }
    // Opt-in dense layers - the whole tree's heavy detail at once.
    if (HasFlag(o.flags, DebugOverlayFlags::PaddingMargin)) {
        DrawBoxModelFills(o, n);
    }
    if (HasFlag(o.flags, DebugOverlayFlags::Gaps)) {
        DrawGaps(o, n);
    }
    if (HasFlag(o.flags, DebugOverlayFlags::AxisHints)) {
        DrawAxisTag(o, n);
    }
    if (HasFlag(o.flags, DebugOverlayFlags::GlyphBorders)) {
        DrawGlyphBorders(o, n);
    }

    for (NodeId c = n.firstChild; c.Valid(); c = o.arena.Resolve(c).nextSibling) {
        Walk(o, c, depth + 1);
    }
}

/// Centres a spacing value in its band so the values on the focused node do not stack on the edges.
void DrawSpacingBand(Overlay& o, const Rect& content, const Spacing& s, bool padding) {
    const Vec2 cc  = content.Center();
    const auto put = [&](float v, Vec2 mid) {
        if (v <= 0.f) {
            return;
        }
        const std::string text = Num(v);
        const float       w    = static_cast<float>(text.size()) * kEdgeFont * 0.6f;
        Label(o, {mid.x - w * 0.5f, mid.y - kEdgeFont * 0.5f}, text, kEdgeFont, kEdgeText);
    };
    if (padding) {
        const Rect pin = s.Inset(content);
        put(s.top, {cc.x, content.Top() + s.top * 0.5f});
        put(s.bottom, {cc.x, pin.Bottom() + s.bottom * 0.5f});
        put(s.left, {content.Left() + s.left * 0.5f, cc.y});
        put(s.right, {pin.Right() + s.right * 0.5f, cc.y});
    } else {
        const Rect out = s.Outset(content);
        put(s.top, {cc.x, out.Top() + s.top * 0.5f});
        put(s.bottom, {cc.x, content.Bottom() + s.bottom * 0.5f});
        put(s.left, {out.Left() + s.left * 0.5f, cc.y});
        put(s.right, {content.Right() + s.right * 0.5f, cc.y});
    }
}

void GlyphBordersSubtree(Overlay& o, NodeId id) {
    if (!id.Valid()) {
        return;
    }
    const LiveNode& n = o.arena.Resolve(id);
    if (!IsTransparent(n.kind) && !Visible(o.arena, n)) {
        return;
    }
    DrawGlyphBorders(o, n);
    for (NodeId c = n.firstChild; c.Valid(); c = o.arena.Resolve(c).nextSibling) {
        GlyphBordersSubtree(o, c);
    }
}

void DrawFocus(Overlay& o, NodeId id) {
    const LiveNode& n       = o.arena.Resolve(id);
    const Rect      content = n.solved;

    DrawBoxModelFills(o, n);
    DrawGaps(o, n);

    // Direct in-flow children, faintly, so the container's layout reads.
    for (NodeId c = n.firstChild; c.Valid(); c = o.arena.Resolve(c).nextSibling) {
        const LiveNode& ch = o.arena.Resolve(c);
        if (!IsTransparent(ch.kind) && ch.style.position == Position::Flow) {
            Stroke(o.canvas, ch.solved, kChildLine, 1.f);
        }
    }

    if (HasFlag(o.flags, DebugOverlayFlags::GlyphBorders)) {
        GlyphBordersSubtree(o, id);
    }

    Stroke(o.canvas, n.style.margin.Outset(content), kMarginFill.WithAlpha(160), 1.f);
    Stroke(o.canvas, content, kFocusLine, 2.f);

    DrawSpacingBand(o, content, n.style.padding, true);
    DrawSpacingBand(o, content, n.style.margin, false);

    const std::string dim = Num(content.width) + " x " + Num(content.height);
    float             by  = content.Top() - (kEdgeFont + 5.f);
    if (by < 0.f) {
        by = content.Top() + 2.f;
    }
    Chip(o, {content.x, by}, dim, kTagText);
}

void DrawTooltip(Overlay& o, NodeId id, Vec2 pointer) {
    const LiveNode& n = o.arena.Resolve(id);

    const char* type = "node";
    if (Widget* w = KindRegistry::For(n.kind)) {
        type = w->Name();
    }
    int depth = 0;
    for (NodeId a = n.parent; a.Valid(); a = o.arena.Resolve(a).parent) {
        ++depth;
    }
    const Spacing& p = n.style.padding;
    const Spacing& m = n.style.margin;

    const std::array<std::string, 8> lines = {
        std::string(type) + "  #" + Num(static_cast<float>(id.value)),
        "x " + Num(n.solved.x) + "   y " + Num(n.solved.y),
        "w " + Num(n.solved.width) + "   h " + Num(n.solved.height),
        "pad  " + Num(p.top) + " " + Num(p.right) + " " + Num(p.bottom) + " " + Num(p.left),
        "mar  " + Num(m.top) + " " + Num(m.right) + " " + Num(m.bottom) + " " + Num(m.left),
        std::string(AxisName(n.style.axis)) + "  main " + AlignInitial(n.style.main) + "  cross "
            + AlignInitial(n.style.cross) + "  gap " + Num(n.style.gap),
        "size " + HintStr(n.style.width) + " x " + HintStr(n.style.height),
        "children " + Num(static_cast<float>(n.childCount)) + "   depth " + Num(static_cast<float>(depth)),
    };

    std::size_t widest = 0;
    for (const std::string& l : lines) {
        widest = std::max(widest, l.size());
    }
    const float lh = kTipFont + 4.f;
    const float tw = static_cast<float>(widest) * kTipFont * 0.56f + 14.f;
    const float th = static_cast<float>(lines.size()) * lh + 9.f;

    Vec2 pos = pointer + Vec2{16.f, 16.f};
    pos.x    = std::min(pos.x, std::max(0.f, o.surface.x - tw));
    pos.y    = std::min(pos.y, std::max(0.f, o.surface.y - th));

    o.canvas.DrawRect(RectCmd{.bounds = Rect{pos.x, pos.y, tw, th}, .fill = kTipBg, .radius = 4.f});
    Stroke(o.canvas, Rect{pos.x, pos.y, tw, th}, kFocusLine.WithAlpha(160), 1.f);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const Color col = (i == 0) ? kTipText : kTipDim;
        Label(o, {pos.x + 7.f, pos.y + 5.f + static_cast<float>(i) * lh}, lines[i], kTipFont, col);
    }
}

// Control panel: each toggleable layer is a clickable checkbox row whose swatch doubles as the colour key. Geometry is a
// pure function of the surface size (top-right), shared by paint and hit-test so a click lands on the row it sits over.
struct CtrlRow {
    DebugOverlayFlags flag;
    const char*       label;
    Color             swatch;
};

constexpr std::array<CtrlRow, 7> kControls = {{
    {DebugOverlayFlags::Boxes, "boxes", {0, 150, 255, 220}},
    {DebugOverlayFlags::HoverInspector, "hover inspect", kFocusLine},
    {DebugOverlayFlags::ClipRegions, "clip / scroll", kClipLine},
    {DebugOverlayFlags::PaddingMargin, "padding / margin", kPadFill},
    {DebugOverlayFlags::Gaps, "gaps", kGapFill},
    {DebugOverlayFlags::AxisHints, "axis hints", {245, 200, 80, 220}},
    {DebugOverlayFlags::GlyphBorders, "glyph borders", kGlyphLine},
}};

constexpr float kCtrlRowH  = kTipFont + 8.f;
constexpr float kCtrlPad   = 8.f;
constexpr float kCtrlTitle = kTipFont + 8.f;
constexpr float kCtrlWidth = 172.f;

struct CtrlLayout {
    Rect                               panel;
    std::array<Rect, kControls.size()> rows{};
};

[[nodiscard]] CtrlLayout ControlLayout(Vec2 surface) {
    const float h = kCtrlTitle + kCtrlRowH * static_cast<float>(kControls.size()) + kCtrlPad;
    CtrlLayout  L{};
    L.panel = Rect{std::max(0.f, surface.x - kCtrlWidth - 8.f), 8.f, kCtrlWidth, h};
    float y = L.panel.y + kCtrlTitle;
    for (std::size_t i = 0; i < kControls.size(); ++i) {
        L.rows[i] = Rect{L.panel.x + kCtrlPad, y, kCtrlWidth - kCtrlPad * 2.f, kCtrlRowH};
        y += kCtrlRowH;
    }
    return L;
}

} // namespace

void PaintDebugOverlay(AppCtx& ctx, NodeId root, const InteractionState& interaction, Canvas& canvas,
                       Vec2 surfaceSize, DebugOverlayFlags flags) {
    if (!root.Valid() || flags == DebugOverlayFlags::None) {
        return;
    }
    Overlay o{.arena   = ctx.arena,
              .canvas  = canvas,
              .pctx    = PaintContext{.arena = &ctx.arena, .fonts = ctx.measurer},
              .flags   = flags,
              .surface = surfaceSize};

    Walk(o, root, 0);

    if (HasFlag(flags, DebugOverlayFlags::HoverInspector)) {
        const NodeId hovered = interaction.hovered.Resolve(o.arena);
        if (hovered.Valid()) {
            DrawFocus(o, hovered);
            DrawTooltip(o, hovered, interaction.pointerPos);
        }
    }
}

Rect DebugControlsBounds(Vec2 surfaceSize) noexcept {
    return ControlLayout(surfaceSize).panel;
}

DebugOverlayFlags HitTestDebugControls(Vec2 surfaceSize, Vec2 point) noexcept {
    const CtrlLayout L = ControlLayout(surfaceSize);
    for (std::size_t i = 0; i < kControls.size(); ++i) {
        if (L.rows[i].Contains(point)) {
            return kControls[i].flag;
        }
    }
    return DebugOverlayFlags::None;
}

void PaintDebugControls(Canvas& canvas, const TextMeasurer* fonts, Vec2 surfaceSize,
                        DebugOverlayFlags flags, DebugOverlayFlags hovered) {
    PaintContext     pctx{.fonts = fonts};
    const CtrlLayout L = ControlLayout(surfaceSize);

    canvas.DrawRect(RectCmd{.bounds = L.panel, .fill = kTipBg, .radius = 4.f});
    Stroke(canvas, L.panel, kFocusLine.WithAlpha(140), 1.f);
    DrawText(canvas, pctx, surfaceSize.x, {L.panel.x + kCtrlPad, L.panel.y + 5.f}, "Lumora debug  (F12)",
             kTipFont, kTipText);

    for (std::size_t i = 0; i < kControls.size(); ++i) {
        const CtrlRow& c   = kControls[i];
        const Rect     row = L.rows[i];
        const bool     on  = HasFlag(flags, c.flag);
        if (c.flag == hovered) {
            Fill(canvas, row, Color{255, 255, 255, 22});
        }
        const float boxY = row.y + (row.height - kTipFont) * 0.5f;
        const Rect  box{row.x + 2.f, boxY, kTipFont, kTipFont};
        if (on) {
            Fill(canvas, box, c.swatch);
        }
        Stroke(canvas, box, on ? c.swatch : kTipDim, 1.f);
        DrawText(canvas, pctx, surfaceSize.x, {box.Right() + 8.f, boxY - 1.f}, c.label, kTipFont,
                 on ? kTipText : kTipDim);
    }
}

void PaintPerfOverlay(Canvas& canvas, const TextMeasurer* fonts, Vec2 surfaceSize, const PerfStats& stats) {
    PaintContext pctx{.fonts = fonts};

    constexpr float pad   = 8.f;
    constexpr float lineH = kTipFont + 4.f;
    constexpr float width = 132.f;
    constexpr int   kLines = 5; // CPU, RAM, style, measure, paint
    const float     h      = pad * 2.f + lineH * static_cast<float>(kLines);
    const Rect      panel{std::max(0.f, surfaceSize.x - width - 8.f), 8.f, width, h};

    canvas.DrawRect(RectCmd{.bounds = panel, .fill = kTipBg, .radius = 4.f});
    Stroke(canvas, panel, kTipDim.WithAlpha(140), 1.f);

    const double      ramMB   = static_cast<double>(stats.ramBytes) / (1024.0 * 1024.0);
    const std::string cpu     = std::format("CPU {:.1f}%", stats.cpuPercent);
    const std::string ram     = std::format("RAM {:.1f} MB", ramMB);
    const std::string style   = std::format("style   {:.2f} ms", stats.styleMs);
    const std::string measure = std::format("measure {:.2f} ms", stats.measureMs);
    const std::string paint   = std::format("paint   {:.2f} ms", stats.paintMs);

    const float x = panel.x + pad;
    float       y = panel.y + pad;
    for (const std::string& line : {cpu, ram, style, measure, paint}) {
        DrawText(canvas, pctx, surfaceSize.x, {x, y}, line, kTipFont, kTipText);
        y += lineH;
    }
}

} // namespace lumora
