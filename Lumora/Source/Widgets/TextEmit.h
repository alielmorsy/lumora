#pragma once

#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Widgets/Props.h>

#include <string_view>
#include <vector>

namespace lumora {

class Canvas;
struct PaintContext;
struct TextPayload;
struct LiveNode;

/// Where a Text node's run starts and what width it was shaped against. Selection painting and hit-testing
/// must use these, not a second guess, so they match EmitText's pen.
struct TextRunGeometry {
    Vec2           pen{};
    AvailableSpace width{};
};

/// Run geometry for a live Text node, matching EmitText's Align::Start path: pen at the padded top-left,
/// shaped against the box width only when the wrap-aware re-measure says this node reflows into its box.
[[nodiscard]] TextRunGeometry TextRunGeometryOf(const LiveNode& node, const TextPayload& text);

/// Lay out `text` via the context's measurer and draw it in `rect` (top-left + padding), wrapping at the
/// content width. Coverage glyphs take `tint`; colour glyphs (emoji) draw as-is. With a `cache` (Text node)
/// glyph geometry is memoised until content epoch or wrap width change; otherwise rebuilt into shared scratch.
/// A no-op without a measurer or for empty text.
void EmitText(Canvas& canvas, PaintContext& ctx, const Rect& rect, const Spacing& pad,
              std::string_view text, const FontProps& font, float size, const TextFlowProps& flow,
              Color tint, TextPayload* cache, Align hAlign = Align::Start, Align vAlign = Align::Start,
              bool wrapToBox = false);

/// Lay text out as EmitText would and append each glyph's final surface-space rect to `out` instead of
/// drawing (debug overlay borders). Pass the same arguments as the widget's Paint hands EmitText.
void CollectTextGlyphRects(PaintContext& ctx, const Rect& rect, const Spacing& pad,
                           std::string_view text, const FontProps& font, float size,
                           const TextFlowProps& flow, TextPayload* cache, Align hAlign, Align vAlign,
                           bool wrapToBox, std::vector<Rect>& out);

} // namespace lumora
