# Text Measurement & Rendering

How a `Text` node goes from a UTF-8 string to shaped, atlased glyph quads on
screen. Covers `Widgets/Text.cpp` (the widget) and `Text/Source/FontSystem.cpp`
(the headless text engine), plus the seam types in between
(`Text/Glyphs.h`, `Text/TextMeasurer.h`, `Node/Payloads.h`,
`Widgets/TextEmit.cpp`).

## Pipeline overview

```
Build          Measure                          Paint
─────          ───────                          ─────
TextWidget::   TextWidget::Measure               TextWidget::Paint
Build            └─ FontSystem::Measure            └─ EmitText
  binds text/      └─ GetLayout (cache)               └─ ResolveRun
  size/color/         └─ BuildLayout                     ├─ BuildGlyphCache
  flow onto             (decode → bidi → break            │   └─ GetLayout (same cache)
  TextPayload            → itemize → shape →               │   └─ EnsureRastered
                          line-break → reorder             │       └─ RasterGlyph (FreeType)
                          → ellipsis → align)               │           └─ GlyphAtlas::Add
                        └─ EnsureRastered                  └─ EmitCachedGlyphs
                            (pre-warm atlas)                   └─ Canvas::DrawGlyphs
```

`FontSystem` is the only concrete `TextMeasurer`. It owns FreeType (rasterising),
HarfBuzz (shaping), SheenBidi (bidi) and libunibreak (UAX#14 line breaks), and
exposes exactly three things to the rest of the engine (`Text/Glyphs.h`):
`GlyphQuad`, `CachedGlyph`, `AtlasView`. Nothing above the seam ever sees a
font, a face, or a glyph index - that's invariant 5 called out in `Glyphs.h`.

## Build: `TextWidget::Build`

`Text(content, cfg)` produces a blueprint carrying `TextData{content, cfg}`.
At instantiate, `TextWidget::Build` allocates a `TextPayload` and wires the
reactive props onto it:

- `content` → `TextPayload::text`, bumps `contentEpoch` (`Dirty::Layout`).
- If a `StyleResolver` is present, size/color/font/flow are the resolver's
  job (`style::ApplyText`) - Text.cpp only binds interaction handlers and the
  `className` hook in that path (sole-writer rule).
- Without a resolver, Text.cpp binds `size`/`color`/`layout` itself directly
  onto the payload, still bumping `contentEpoch` on a size change (size feeds
  shaping) but not on a color change (`Dirty::Style` only - color is a paint
  concern, not a layout one).

`font` and `flow` are copied once at bind time; they are not reactive props.

## Measure: `TextWidget::Measure`

```cpp
Vec2 Measure(const MeasureContext& ctx, NodeId id, AvailableSpace width, AvailableSpace) override
```

Text is `ContentSized()` and `ReflowsWidth()`: it sizes itself from its
content and that size depends on the horizontal constraint (wrapping). The
vertical `AvailableSpace` argument is unused - text never wraps against a
height constraint.

Per-node memoisation happens here before ever calling into `FontSystem`:

```cpp
if (payload.measuredValid && payload.measuredEpoch == payload.contentEpoch
    && payload.measuredWidth == width)
{
    return payload.measuredSize; // skip the shaping call entirely
}
```

`measuredEpoch`/`contentEpoch` catch text or size changes; `measuredWidth`
catches a change in wrap constraint (e.g. the parent resized). A layout pass
that re-visits this node for an unrelated reason (a sibling reordered, the
node moved) hits this cache and never touches HarfBuzz/FreeType.

On a miss, `FontSystem::Measure` is called and the result is stored back onto
the payload.

## `FontSystem::Measure` → layout cache

```cpp
Vec2 Measure(text, font, size, flow, width) const override
{
    const Layout& layout = GetLayout(text, font, size, flow, width);
    EnsureRastered(layout, RasterPx(size)); // pre-warm the atlas
    return layout.size;
}
```

`GetLayout` is a second, engine-wide cache keyed on
`(text, resolvedFace, px, wrapWidth, maxLines, align, wrap, ellipsis, lineHeight)`
(`Impl::CacheKey`/`CacheKeyView`). It exists because the *same* run gets
measured and then re-measured for glyph building (`BuildGlyphCache` below)
inside the same frame - the transparent hash/equal (`is_transparent`) lets a
lookup probe with a borrowed `string_view` and avoid allocating a `std::string`
on the hot path; only a genuine miss allocates the owning `CacheKey`.

A cache miss runs the full shaping pipeline: `BuildLayout`.

## `BuildLayout`: the shaping pipeline

Eight stages, each building on the last, all operating over the string's
Unicode code points (not bytes, not UTF-16):

**0. Resolve face + decode.** `Resolve(props)` picks a `FaceId` from the
family/bold/italic slot table (falling back to the default family, then any
registered face). `DecodeUtf8` turns the UTF-8 byte string into a code-point
array plus a code-point→byte-offset map (`cpBytes`), so later stages can work
in code-point indices and only convert back to bytes when needed.

**1. Bidi levels - SheenBidi.** One `SBAlgorithm`/`SBParagraph` over the
whole run assigns each code point a UBA embedding level (even = LTR, odd =
RTL), base direction defaulting to LTR.

**2. Line-break opportunities - libunibreak (UAX#14).**
`set_linebreaks_utf32` marks each code point `NOBREAK` / `ALLOWBREAK` /
`MUSTBREAK` (the latter covers explicit `\n`). This is Unicode's line-breaking
algorithm, not just whitespace splitting - it knows about hyphens, CJK,
no-break spaces, etc.

**3. Itemization.** The code-point stream is split into `Run`s, each uniform
in three things at once: bidi level, HarfBuzz script, and *covering face*
(`CoveringFace`: the primary face if it has a glyph for the code point, else
the first fallback face that does - this is how mixed script / emoji text
picks the right font per character). A run boundary is forced whenever any of
the three changes. Script is treated with the usual "common/inherited
characters extend the current run" rule (so a run of digits or punctuation
between two runs of Arabic doesn't get its own tiny run).

**4. Shaping - HarfBuzz, per run.** Each run is shaped independently
(`hb_shape`) at `px*64` scale (26.6 fixed point) with its own direction
(derived from the run's bidi level) and script. Output glyph advances are
accumulated per **code point** (`advByCp`, summing multiple glyphs that share
a cluster) - this is what step 5 line-breaks against, so line breaking is
shaping-aware (ligatures/kerning affect where a line wraps).

**5. Greedy line breaking.** Walks code points in logical order, accumulating
width from `advByCp`. When wrap is enabled and width exceeds `maxWidth`, it
breaks at the last seen `ALLOWBREAK`/`MUSTBREAK` position (`lastBreak`), or
mid-run if none exists (an unbreakable overlong token isn't further split -
this engine doesn't do letter-by-letter emergency breaking). Explicit
`MUSTBREAK` always closes a line immediately. If `flow.maxLines > 0` the line
list is truncated (with a `truncated` flag for the ellipsis stage).

**6. Per-line assembly + UBA rule L2 visual reorder.** For each logical line,
glyphs are grouped into `Seg`s (contiguous glyph ranges within one run that
fall in that line's code-point range). Then the classic UBA reorder: from the
highest level down to the lowest odd level, reverse maximal runs of segments
whose level is `>= L`. This turns a logical (reading-order) sequence of
segments into visual (left-to-right on screen) order without needing to
re-shape anything - each segment already carries its own internal glyph
order from HarfBuzz. The pen then walks visual order left→right, laying out
`PositionedGlyph{faceIndex, glyph, x, y=baseline, advance}` - coordinates are
pen-relative, origin 0 (not tied to any node's box yet).

Line vertical metrics: `Metrics(primary, px)` reads `ascender`/`height` from
FreeType (only for non-color/scalable faces - a bitmap-strike emoji face has
no usable scalable metrics and falls back to px-based numbers). `lineAdv`
applies `FontProps::lineHeight` as a multiplier over natural line height;
`halfLeading` centers the natural glyph box within the (possibly larger)
line box.

**7. Ellipsis.** Only on a truncated last line, only if `flow.ellipsis` is
set and the face has a `U+2026` glyph. Pops glyphs off the back of the line
while `width + ellipsisAdvance` still exceeds the limit, then appends the
ellipsis glyph. Best-effort visual-tail trim, not clean word-boundary
elision.

**8. Alignment + assembly.** `boxW` is the wrap width if definite, else the
widest built line (shrink-to-fit natural width). Each line's glyphs get
`AlignOffset(flow.align, lineWidth, boxW)` added to `x` (`Start`→0,
`Center`/`End` computed, anything else → 0). Final `layout.size` is
`{maxLineW, ceil(lineCount * lineAdv)}` - the height is rounded *up* so an
integer-snapped box always contains the last line's descenders.

## Rasterization: atlas + glyph cache

`EnsureRastered(layout, px)` walks every glyph in a built layout and, for any
`(faceIndex, glyph, px)` triple not already in `glyphInfo`, calls
`RasterGlyph`. This runs during **measure**, not paint - so by the time paint
happens the atlas is already populated and paint never blocks on FreeType
(comment: "a grow never moves an existing slot", so pixel rects handed out
during measure stay valid even if the atlas grows later for other glyphs).

`RasterGlyph`:
- Color-bitmap (emoji) faces: `FT_HAS_COLOR` picks the nearest fixed strike
  size to the requested px and records a `scale` factor (native strike height
  → requested px) since these can't be rasterised at an arbitrary size.
  Scalable faces just call `FT_Set_Pixel_Sizes`.
- `FT_Load_Glyph(FT_LOAD_RENDER | (color ? FT_LOAD_COLOR : 0))` rasterises.
- BGRA color bitmaps are premultiplied by FreeType; they're un-premultiplied
  into straight RGBA before being packed (`convertScratch` scratch buffer).
- Coverage (grayscale) bitmaps go straight into the R8 atlas.
- Empty glyphs (space) get a `GlyphInfo` with `w=h=0` - recorded so
  `EnsureRastered` doesn't re-attempt them, but contribute no atlas slot.

Two `GlyphAtlas` planes exist: `coverage` (R8, 1024×256 initial, grows by
doubling height) and `color` (RGBA8888, 512×128 initial). Both are simple
shelf packers (`Reserve`): glyphs are placed left-to-right on a shelf of
`_rowHeight` = tallest glyph so far on that shelf, wrapping to a new shelf
when a row is full, growing (doubling height, never width) when the whole
plane is full. Slots are keyed by `GlyphKey(faceIndex, glyphIndex, px)` and
**never move** once assigned - growth only appends zeroed rows below -
which is why cached pixel rects survive atlas growth. `version` bumps on
every new glyph or grow so the painter knows to re-mirror the CPU buffer to a
GPU texture; `[dirtyY0, dirtyY1)` tracks the minimal row span to re-upload,
consumed (reset) each time `View()` is called.

## Logical vs. physical pixels (DPI)

Shaping/layout run entirely in **logical** pixels - `FontSystem::Measure`
returns logical sizes and a paragraph wraps identically at any DPI. Glyphs
rasterise at **physical** resolution for crispness: `RasterPx(size) =
round(size * scale)` where `scale` is set via `SetContentScale`. Changing the
scale clears `glyphInfo` and resets both atlases (`Reset()`, not
reconstruction - keeps dimensions, zeros pixels) and bumps `generation`.
`TextPayload::glyphsGen` is checked against `FontSystem::Generation()` at
paint time so any node's cached glyph quads rebuild after a DPI change.

`BuildGlyphCache`'s output `CachedGlyph::local` rects are stored in **logical**
pixels: bitmap metrics come back from FreeType in physical pixels
(rasterised at `RasterPx`), so they're divided by `scale` before being
stored (`s = gi.scale / _impl->scale` - `gi.scale` handles the color-strike
native→requested factor, `_impl->scale` undoes the physical rasterisation).
The painter's base layer scales logical→physical again at draw time, landing
the already-physical-resolution glyph 1:1 on screen.

## Paint: `TextWidget::Paint` → `EmitText`

```cpp
void Paint(PaintContext& ctx, NodeId id, Canvas& canvas) override
{
    const bool wrap = t.flow.wrap;
    const bool wrapToBox = wrap && n.wrapInBox;
    if (!wrap) canvas.PushClip(n.solved);
    EmitText(canvas, ctx, n.solved, n.style.padding, t.text, t.font, t.size, t.flow,
             t.color, &t, Align::Start, Align::Start, wrapToBox);
    if (!wrap) canvas.PopClip();
}
```

`wrapInBox` (on the `LiveNode` header, set by the layout bridge) distinguishes
two situations that both produce a content-sized box:

- The node was measured against a **definite** width and actually wraps -
  the solved box *is* the intended wrap width, so paint should shape against
  it too (`wrapToBox = true`), or measure and paint would disagree on where
  lines break.
- The node sits under a shrink-to-fit parent with no real width constraint
  (`wrapInBox = false`) - it stays single-line even if `flow.wrap` is set,
  because wrapping into its own content-collapsed box would word-wrap a
  heading against its own natural width, which is never wanted.

Non-wrapping text is clipped to the solved box (`PushClip`) so an overlong
label is cut at the edge instead of spilling into siblings; wrapping text
isn't clipped (its box already reflects the wrapped extent).

`EmitText` is shared by every text-bearing widget (Text, Button labels,
Input), not just `TextWidget` - that's why it takes an explicit `cache`
(nullable) rather than assuming a `TextPayload`-owning node: label-only
callers pass `nullptr` and rebuild into shared per-frame scratch
(`ctx.coverageCacheScratch`/`colorCacheScratch`) instead of memoising.

### `ResolveRun`: pen + alignment + glyph cache

`ResolveRun` (private to `TextEmit.cpp`, shared by `EmitText` and the debug
overlay's `CollectTextGlyphRects`) does the actual per-frame work:

1. Compute `shapeWidth`: `Definite(maxWidth)` if `wrapToBox`, else
   `MaxContent()` - must match whatever `Measure` was called with, or the
   glyph cache key and the box size disagree and glyphs would be shaped
   against the wrong constraint.
2. Horizontal alignment (`hAlign != Start`, e.g. a centered Button label):
   measures the run's natural width (another `FontSystem::Measure` call -
   hits the layout cache from step 1's measure, so this doesn't re-shape) and
   offsets the pen by the slack. `Start` (the common Text/Input path) skips
   this entirely.
3. Glyph cache: if `cache` is provided and its
   `(glyphsEpoch, glyphsWidth, glyphsGen)` signature matches
   `(contentEpoch, shapeWidth, FontSystem::Generation())`, the cached
   `coverageGlyphs`/`colorGlyphs` vectors are reused verbatim - no call into
   `FontSystem` at all. Otherwise `BuildGlyphCache` is called (which itself
   hits the `GetLayout` cache from measure, then rasterises anything new via
   `EnsureRastered`) and the result is stored back on the cache along with
   the fresh signature.
4. Vertical alignment (`vAlign != Start`, e.g. Button label centering):
   computed against the run's real **ink box** (min glyph top → max glyph
   bottom across both planes), not the font's line box - a font reserves
   ascent space above the glyphs that a short/lowercase label never fills, so
   line-box centering would look visibly low. This requires the glyphs to
   already be built (step 3), so it's resolved last.

### Drawing: `EmitCachedGlyphs`

Each `CachedGlyph` is pen-relative and stored against the *atlas pixel*
rect, not normalised UVs - because the atlas can grow between when a glyph
was cached and when it's drawn. `EmitCachedGlyphs` re-derives the UV
(`slot / atlasSize`) every draw and adds the resolved pen to get the final
surface-space `dst` rect, producing a `GlyphQuad{dst, uv}` consumed by
`Canvas::DrawGlyphs`.

Coverage glyphs (the R8 mask, tinted by the run's `color`) get their final
screen origin **snapped to a whole physical pixel**
(`round(x * scale) / scale`) - required because the coverage atlas is
sampled with a linear filter, so a 1:1 texel-to-pixel map needs an
integer-pixel origin; snapping has to happen *after* the pen (with its
possibly-fractional alignment offset) is folded in, which is why the raw
`CachedGlyph::local` rect is deliberately left unsnapped and the emit pass
snaps `local + pen` instead. Color glyphs (emoji) are left subpixel for
smooth resampling - an emoji doesn't need texel-exact edges the way a thin
glyph stroke does.

## Cache summary

| Cache | Keyed by | Invalidated by | Lives on |
|---|---|---|---|
| `TextPayload` measure memo | `contentEpoch`, `measuredWidth` | text/size change, wrap-width change | per-node (`TextPayload`) |
| `Impl::layoutCache` (shaped layout) | text, face, px, wrap/align/maxLines/ellipsis/lineHeight | new key (unbounded - no eviction currently) | `FontSystem::Impl`, engine-wide |
| `Impl::glyphInfo` (rasterised glyph metrics) | `(faceIndex, glyphIndex, px)` | content-scale change (cleared wholesale) | `FontSystem::Impl`, engine-wide |
| `TextPayload` glyph-quad memo | `glyphsEpoch`, `glyphsWidth`, `glyphsGen` | text/size/wrap-width change, DPI generation bump | per-node (`TextPayload`) |
| `GlyphAtlas` pixel slots | `GlyphKey` | never (slots are append-only; `Reset()` on DPI change clears everything) | `FontSystem::Impl`, engine-wide |

Three distinct epoch/generation counters exist for a reason: `contentEpoch`
gates the *measure* result (does the text need reshaping at all),
`glyphsEpoch`/`glyphsWidth` gates the *paint-cache* independently (a node
can be re-measured without its wrap width changing, so its glyph quads stay
valid), and `generation` is orthogonal to both - it only fires on a DPI
change and forces every node's glyph cache to rebuild even though nothing
about the text or layout changed.

## Coordinate systems, end to end

1. **Shaping** (`BuildLayout`): pen-relative, origin 0, logical px. HarfBuzz
   works in 26.6 fixed point internally; everything here is `float` logical
   px after the `/64.f` conversion.
2. **`CachedGlyph::local`**: still pen-relative, origin 0, logical px -
   dividing out FreeType's physical rasterisation scale.
3. **Emit** (`EmitCachedGlyphs`): pen-relative rect + resolved pen (box
   origin + padding + alignment offset) → surface-space logical px, with
   coverage glyphs snapped to whole physical pixels.
4. **Painter base layer** (outside this doc's scope): scales the logical
   surface-space rect to physical pixels for the final draw.

Splitting logical/physical this way is what lets a paragraph's line breaks
stay identical across a DPI change while still rasterising crisp,
native-resolution glyphs.
