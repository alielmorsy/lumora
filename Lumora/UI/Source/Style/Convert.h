#pragma once

#include "Style/ComputedStyle.h"

#include "Node/LayoutStyle.h"                    // FlexAxis, DisplayMode, FlexWrap
#include <lumora/Foundation/Position.h> // Position, Inset

#include <lumora/StringId.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Geometry.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Widgets/Grid.h> // TrackSize
#include <lumora/Widgets/Props.h> // Border

#include <string>
#include <vector>

namespace lumora::style
{
    /// Split a className string on whitespace into interned class ids (case-sensitive, per CSS).
    [[nodiscard]] std::vector<StringId> SplitClasses(const std::string& s);

    // CssValue -> layout/paint value converters; em/rem/vw/vh are expected to be collapsed to px already (see Resolve).

    [[nodiscard]] Color ToColor(const CssValue& v, Color fb);
    [[nodiscard]] float ToNumber(const CssValue& v, float fb);
    [[nodiscard]] float ToPx(const CssValue& v, float fb);
    [[nodiscard]] SizeHint ToSizeHint(const CssValue& v, SizeHint fb);
    [[nodiscard]] Align ToAlign(const CssValue& v, Align fb);
    [[nodiscard]] FlexAxis ToFlexAxis(const CssValue& v, FlexAxis fb);
    [[nodiscard]] Border ToBorder(const ComputedStyle& cs, Border fb);

    /// static/relative/sticky -> Flow (no offset; the caller sets `anchor`), absolute -> Absolute, fixed -> Fixed.
    [[nodiscard]] Position ToPosition(const CssValue& v, Position fb);

    /// Inset edge -> px, or NaN (auto) for `auto`; percent insets are unsupported and give `fbAuto`.
    [[nodiscard]] float ToInsetEdge(const CssValue& v, float fbAuto);

    [[nodiscard]] FlexWrap ToFlexWrap(const CssValue& v, FlexWrap fb);

    /// `aspect-ratio` -> width / height; 0 for `auto` or unreadable. Accepts both spellings the value parser can emit for `16 / 9`.
    [[nodiscard]] float ToAspectRatio(const CssValue& v);

    /// True only for `dashed`; every other keyword reads as solid (`none` is a zero border-width).
    [[nodiscard]] bool ToBorderDashed(const CssValue& v, bool fb);

    /// The three components of the `flex` shorthand, already expanded. `basis` Wrap == auto.
    struct FlexShorthand
    {
        float grow = 0.f;
        float shrink = 0.f;
        SizeHint basis = Wrap;
    };

    /// Expand a `flex` shorthand value: `none`→0/0/auto, `auto`→1/1/auto, `<n>`→n/1/0, `<g> <s>`→g/s/auto,
    /// `<g> <s> <basis>` (or `<g> <basis>`)→as written.
    [[nodiscard]] FlexShorthand ToFlex(const CssValue& v);

    /// `<n>fr` or a bare number -> fr, px -> px, `repeat(n, ...)` expanded; `%` approximates `value/100` fr and
    /// auto/min-content/max-content give 1fr. Empty when no tracks.
    [[nodiscard]] std::vector<TrackSize> ToTrackList(const CssValue& v);

    /// One axis of grid placement (`grid-column`/`grid-row`): `start` is a 0-based track (-1 == auto-flow), `span` >= 1.
    struct GridLine
    {
        int start = -1;
        int span = 1;
    };

    [[nodiscard]] GridLine ToGridLine(const CssValue& v);

    /// A spacing value split into px edges and percent edges (a percent edge carries its magnitude in `pct`, 0 in `px`).
    struct SpacingSplit
    {
        Spacing px{};
        Spacing pct{};
    };

    [[nodiscard]] SpacingSplit ToSpacingSplit(const CssValue& v, Spacing fbPx);

    /// `padding` / `margin` plus their per-side longhands; a longhand present in `cs` overrides its side.
    [[nodiscard]] SpacingSplit ToPaddingSplit(const ComputedStyle& cs, Spacing fbPx);
    [[nodiscard]] SpacingSplit ToMarginSplit(const ComputedStyle& cs, Spacing fbPx);

    struct GapSplit
    {
        float px = 0.f;
        float pct = 0.f;
    };

    [[nodiscard]] GapSplit ToGapSplit(const CssValue& v, float fbPx);

    struct TransformValue
    {
        float x = 0.f;
        float y = 0.f;
        float scaleX = 1.f;
        float scaleY = 1.f;
        float rotate = 0.f; ///< radians
    };

    /// Parse a `transform` value: translate/translateX/Y, scale/scaleX/Y, rotate. Components compose as translate, rotate, scale
    /// regardless of source order; repeated functions multiply (scale) or add (translate, rotate).
    [[nodiscard]] TransformValue ToTransform(const CssValue& v);

    /// Parse `transform-origin` into box fractions (keywords and percentages; anything else keeps the fallback).
    [[nodiscard]] Vec2 ToTransformOrigin(const CssValue& v, Vec2 fb);

    /// Keywords are interned lowercased, so the compare is case-insensitive.
    [[nodiscard]] bool KeywordIs(const CssValue& v, StringId kw);
} // namespace lumora::style
