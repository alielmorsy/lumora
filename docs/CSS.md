# Lumora CSS

Lumora supports styling widgets with CSS loaded from external `.css` files (or in-memory strings).
CSS is a **layer on top of** the inline C++ prop API - inline props remain the origin-0 defaults and
CSS overrides them per property, reverting cleanly when a rule stops matching.

This document is the authoritative reference for what is supported. Anything not listed here is not
implemented.

---

## 1. Getting started

```cpp
// Load a stylesheet on the Lumora runtime (read through the engine asset loader, applied to every
// mounted app):
lumora->LoadStylesheet("assets/theme.css");

// Give widgets classes. The className is reactive, so toggling it re-resolves style:
Box({.className = "card"}, Text("Hello"));
Box({.className = [&] { return selected() ? "card active" : "card"; }});
```

```css
/* assets/theme.css */
.card {
    background-color: #14151A;
    border-radius: 8px;
    padding: 16px;
    transition: background-color 0.2s ease;
}
.card:hover { background-color: #1E2028; }
.card.active { border: 2px solid #4C8DFF; }
```

**Classes only.** There are no id selectors (`#foo`) and no type selectors (`div`) - style is applied
via the `.className` field on a widget's config.

**Precedence.** When a CSS rule targets a property, CSS wins; otherwise the widget's inline prop value
is used. When a class stops matching, the property reverts to the inline value. (Because a CSS rule is
the sole writer of the properties it targets, do not also drive the *same* property with a bound inline
signal - the CSS value will win.)

---

## 2. Selectors

| Selector | Example | Supported |
|---|---|---|
| Class | `.card` | ✅ |
| Compound (AND) | `.card.active` | ✅ |
| Descendant | `.list .item` | ✅ |
| Child | `.menu > .item` | ✅ |
| Pseudo-class | `.btn:hover` | ✅ (see §7) |
| Universal | `*` | ✅ |
| Root | `:root` | ✅ (matches the app root; used for global custom properties) |
| Selector list | `.a, .b` | ✅ |
| Id | `#id` | ❌ |
| Type / element | `div` | ❌ |
| Sibling | `.a + .b`, `.a ~ .b` | ❌ |
| Attribute | `[data-x]` | ❌ |
| Pseudo-element | `::before`, `::after` | ❌ |
| `:not()`, `:nth-child()`, … | | ❌ |

### Nesting

Native CSS nesting is supported and flattened at parse time. `&` refers to the parent selector; a
nested selector without `&` is an implicit descendant.

```css
.card {
    background-color: #14151A;
    &:hover { background-color: #1E2028; }   /* → .card:hover */
    & > .title { font-weight: bold; }        /* → .card > .title */
    .badge { color: #4C8DFF; }               /* → .card .badge (implicit descendant) */
}
```

### Cascade & specificity

The winning declaration for each property is chosen by, in order:

1. `!important` beats normal.
2. Higher **specificity** - with classes only, specificity is the total count of classes +
   pseudo-classes in the selector (`.a.b:hover` = 3 beats `.a` = 1).
3. Later **source order** wins ties (and later-loaded stylesheets win over earlier ones).

```css
.btn { color: #888 !important; } /* wins over any non-!important rule regardless of specificity */
```

---

## 3. Property reference

Properties apply per widget kind. "Box-like" = `Box`, `Row`, `Column`, `Stack`, `Grid`, `Cell`,
`Scroll`.

### Layout (box-like, Text, Button)

| Property | Values | Notes |
|---|---|---|
| `width`, `height` | `<len>`, `%`, `auto` | `%` resolves in the flex solver (lag-free) |
| `min-width`, `min-height`, `max-width`, `max-height` | `<len>`, `%` | |
| `padding` | 1–4 `<len>`/`%` | shorthand: `all` \| `v h` \| `t h b` \| `t r b l` |
| `margin` | 1–4 `<len>`/`%` | same shorthand |
| `padding-top/right/bottom/left`, `margin-top/right/bottom/left` | `<len>`/`%` | override one side; cascade against the shorthand like real CSS |
| `gap`, `row-gap`, `column-gap` | `<len>`, `%` | |
| `justify-content` | `flex-start`/`start`, `center`, `flex-end`/`end`, `space-between`, `space-around`, `space-evenly`, `stretch` | main-axis alignment |
| `align-items` | same keyword set | cross-axis alignment |
| `flex-direction` | `row`, `column` | flips a Row↔Column container only (never Stack/Grid) |
| `display` | `none`, `flex`, `block`, `inline-block`, `grid` | see below - sets the inner layout mode |
| `visibility` | `visible`, `hidden`, `collapse` | `hidden`/`collapse` hide the node |
| `overflow` | `hidden`, `clip`, `scroll`, `auto`, `visible` | non-`visible` clips the subtree |

`display`: `none` hides the node; `flex`/`inline-flex` is the default flexbox mode; `block`/`inline-block`
routes the container to normal flow (children stack, take the content width, honour margins); `grid`
applies to a `Grid` widget only (see the Grid section) - on any other widget it is a no-op (a plain `Box`
cannot become a grid).

### Flex item / container (box-like)

| Property | Values | Notes |
|---|---|---|
| `flex-grow` | `<number>` | main-axis grow weight (same channel as an `Expand` size) |
| `flex-shrink` | `<number>` | default `0` (rigid, overflow - not crush); set `> 0` to opt an element into shrinking |
| `flex-basis` | `<len>`, `%`, `auto` | initial main size before grow/shrink |
| `flex` | `none` \| `auto` \| `<grow> [<shrink>] [<basis>]` | shorthand expanding the three above |
| `flex-wrap` | `nowrap`, `wrap`, `wrap-reverse` | multi-line wrapping |
| `align-self` | `auto`, `flex-start`/`start`, `center`, `flex-end`/`end`, `stretch` | per-item cross override |
| `align-content` | same keyword set + `space-between`/`around`/`evenly` | multi-line cross distribution (with wrap) |
| `order` | `<number>` | flex layout order (does **not** change paint/hit-test order - use `z-index`) |

### Position (box-like except `Scroll`)

| Property | Values | Notes |
|---|---|---|
| `position` | `static`, `relative`, `absolute`, `fixed`, `sticky` | `absolute`/`fixed` leave the flow; `relative` stays in flow but becomes a containing block (no offset shift); `sticky` → treated as `relative` |
| `top`, `right`, `bottom`, `left` | `<len>`, `auto` | insets, applied when `position` is not in-flow; `auto` uses the containing block's static position |
| `inset` | 1–4 `<len>`/`auto` | shorthand for the four edges (`all` \| `v h` \| `t h b` \| `t r b l`) |
| `z-index` | `<number>` | paint priority within siblings |

### Grid (the `Grid` / `Cell` widgets)

| Property | Values | Notes |
|---|---|---|
| `grid-template-columns`, `grid-template-rows` | track list of `<n>fr`, `<len>`, `repeat(n, …)` | on a `Grid` |
| `grid-auto-rows` | one track size | size for implicit rows past the template |
| `gap`, `row-gap`, `column-gap` | `<len>` (`gap` accepts `<row> <col>`) | grid track gaps (px) |
| `grid-column`, `grid-row` | `<line>`, `<start> / <end>`, `<start> / span <n>`, `span <n>` | on a `Cell` (1-based CSS lines) |

### Paint (box-like)

| Property | Values | Notes |
|---|---|---|
| `background-color` / `background` | `<color>` | |
| `border-radius` | `<len>` | |
| `opacity` | `<number 0–1>` | |
| `border` | `<len> <style> <color>` | shorthand for all sides (solid or dashed) |
| `border-width` | `<len>{1,4}` | 1 to 4 values (top, right, bottom, left) |
| `border-color` | `<color>{1,4}` | 1 to 4 values (top, right, bottom, left) |
| `border-style` | `solid`, `dashed`, `none` | 1 to 4 values |
| `border-top`, `border-right`, `border-bottom`, `border-left` | `<len> <style> <color>` | per-side shorthand |
| `border-top-width`, `border-right-width`, `border-bottom-width`, `border-left-width` | `<len>` | per-side width |
| `border-top-color`, `border-right-color`, `border-bottom-color`, `border-left-color` | `<color>` | per-side color |
| `border-top-style`, `border-right-style`, `border-bottom-style`, `border-left-style` | `solid`, `dashed`, `none` | per-side style |
| `transform` | `translate(<len>[, <len>])`, `translateX/Y(<len>)`, `scale(<n>[, <n>])`, `scaleX/Y(<n>)`, `rotate(<angle>)`, `none` | paint-only, layout box unchanged; see below |
| `transform-origin` | `left`/`center`/`right`, `top`/`center`/`bottom`, `<pct>` (1 or 2 values) | default `center`; px not supported |

### Text

| Property | Values | Notes |
|---|---|---|
| `color` | `<color>` | inherited |
| `font-size` | `<len>` | inherited |
| `font-family` | name or `"quoted name"` | inherited |
| `font-weight` | `bold`, `normal`, `<number>` | `≥ 600` → bold; inherited |
| `font-style` | `italic`, `oblique`, `normal` | inherited |
| `text-decoration` | `line-through`, `none` | |
| `line-height` | `<number>` | inherited |
| `text-align` | `left`/`start`, `center`, `right`/`end` | inherited |
| `white-space` | `nowrap`, `pre`, `normal` | non-normal disables wrapping |
| `text-overflow` | `ellipsis`, `clip` | |
| `-webkit-line-clamp` / `max-lines` | `<number>` | 0 = unlimited |

### Button

`background-color`, `color`, `border-radius`, plus the Layout properties. An unset `background-color`
falls back to the button's variant style.

### Switch

`background-color` (or `color`) styles the checked-track colour; plus Layout properties.

### Checkbox

`color` styles label text, `background-color` styles the checked box fill, and `border-color` styles the box outline; plus Layout properties.

---

## 4. Values & units

### Lengths

| Unit | Meaning |
|---|---|
| `px` | pixels |
| `%` | percentage - layout props resolve in the solver; paint props resolve against the box |
| `em` | multiples of the node's font-size |
| `rem` | multiples of the root font-size (default 16) |
| `vw`, `vh` | percent of viewport width / height (re-resolves on resize) |
| `fr` | grid flexible track fraction (`grid-template-columns`/`rows` only) |
| `pt`, `pc`, `in`, `cm`, `mm` | absolute, converted to px |

Unitless numbers are used for `opacity`, `flex-grow`, `line-height`, `z-index`, `font-weight`, etc.
Time (`s`, `ms`) is used in `transition`/`animation`. Angles (`deg`, `rad`, `turn`) parse but are only
consumed where a property uses them.

### Colors

- Hex: `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`
- Functions: `rgb()`, `rgba()`, `hsl()`, `hsla()`
- Named colours (`red`, `white`, `dodgerblue`, …)
- `transparent`
- `currentColor` (resolves to a fallback in this version)

### Case sensitivity

Property names and keyword values are case-insensitive (`Center` == `center`). **Class names and
custom-property names are case-sensitive** (`.Card` ≠ `.card`, `--Brand` ≠ `--brand`).

---

## 5. Custom properties (variables)

Define with `--name` and read with `var(--name[, fallback])`. Custom properties cascade and are
inherited, so a value set on an ancestor (or `:root`) is visible to descendants.

```css
:root {
    --brand: #4C8DFF;
    --gap: 12px;
}
.panel {
    --brand: #FF6B4C;              /* overrides :root for this subtree */
    gap: var(--gap);
}
.panel .cta {
    background-color: var(--brand);          /* → #FF6B4C (from .panel) */
    color: var(--missing, #FFFFFF);          /* → fallback, --missing is undefined */
}
```

`var()` is resolved recursively (a variable may reference another) and is cycle-guarded (a circular
reference yields an invalid value rather than hanging).

---

## 6. Inheritance

These properties inherit from the nearest ancestor that set them (via CSS): `color`, `font-family`,
`font-size`, `font-weight`, `font-style`, `line-height`, `text-align`. Containers pass inherited values
through to their descendants even though they don't render text themselves.

```css
.article { color: #DDDDDD; font-size: 15px; }   /* every Text inside inherits these */
```

Two keywords are supported on the inherited properties:

- `inherit` - force the parent's computed value.
- `initial` - reset to the widget's own default.

```css
.article .note { color: inherit; }   /* explicitly take the article's colour */
```

---

## 7. Pseudo-classes

| Pseudo-class | Fires when |
|---|---|
| `:hover` | the pointer is over the node |
| `:active` | the node is pressed (mouse down) |
| `:focus` | the node has keyboard focus |
| `:disabled` | the node's `disabled` prop is set |
| `:checked` | a Switch/Checkbox is on |

They re-resolve reactively, including through combinators - hovering an ancestor restyles descendants:

```css
.btn { background-color: #2A2C34; }
.btn:hover { background-color: #3A3C44; }
.btn:active { transform: scale(0.97); }
.field:focus { border-color: #4C8DFF; }
.toggle:checked { background-color: #34C759; }
.menu:hover .item { color: #FFFFFF; }   /* ancestor hover restyles the descendant */
```

---

## 8. Transitions

Animate a property change over time. Supported on the box-like paint properties: `background-color`,
`opacity`, `border-radius`, `transform` (and `all`).

```css
transition: <property> <duration> [<timing-function>] [<delay>][, …];
```

```css
.card {
    background-color: #14151A;
    transition: background-color 0.2s ease-in-out, transform 0.15s ease-out 0.05s;
}
.card:hover { background-color: #1E2028; transform: translateY(-2px); }
```

**Timing functions:** `linear`, `ease`, `ease-in`, `ease-out`, `ease-in-out`, `step-start`, `step-end`,
`cubic-bezier(x1, y1, x2, y2)`, `steps(n [, start|end])`.

A transition retargets smoothly if interrupted mid-flight, and snaps instantly when reduced-motion is
enabled.

---

## 9. Animations (`@keyframes`)

Define keyframes and drive them with the `animation` shorthand. Supported on the box-like paint
properties: `background-color`, `opacity`, `border-radius`, `transform`.

```css
@keyframes pulse {
    from { opacity: 1; }
    50%  { opacity: 0.4; }
    to   { opacity: 1; }
}
@keyframes slide-in {
    from { transform: translateX(-20px); opacity: 0; }
    to   { transform: translateX(0);     opacity: 1; }
}

.badge  { animation: pulse 1.2s ease-in-out infinite; }
.drawer { animation: slide-in 0.25s ease-out forwards; }
```

```css
animation: <name> <duration> [<timing-function>] [<delay>]
           [<iteration-count>|infinite] [<direction>] [<fill-mode>] [paused];
```

- **iteration-count:** a number, or `infinite`.
- **direction:** `normal`, `reverse`, `alternate`, `alternate-reverse`.
- **fill-mode:** `none`, `forwards`, `backwards`, `both`.
- `paused` freezes the animation.

Keyframe offsets accept `from`, `to`, and percentages (`0%`, `50%`, `100%`). When both an animation and
a transition target the same property, the animation wins.

---

## 10. Media queries

Rules inside a passing `@media` block apply; the tree re-resolves only when a breakpoint is crossed
(not on every resize pixel).

```css
@media (max-width: 600px) {
    .sidebar { display: none; }
    .content { padding: 8px; }
}
@media (min-width: 600px) and (max-width: 1200px) {
    .grid { gap: 16px; }
}
```

Supported features: `min-width`, `max-width`, `min-height`, `max-height`, combined with `and`.

---

## 11. C++ API

The public entry points live on the `Lumora` runtime (it owns the asset loader and every mounted app):

```cpp
lumora->LoadStylesheet("assets/theme.css");   // read through the asset loader, apply to every app
lumora->SetStylesheetSource(cssString);        // apply an in-memory CSS string
lumora->ReloadStylesheet("assets/theme.css");  // re-read + re-apply (asset hot-reload)
```

Any widget config exposes a reactive class list:

```cpp
struct BoxCfg { …; Reactive<std::string> className{}; };
```

Loading (or reloading) a stylesheet re-resolves every app's tree, so hot-reload is a matter of calling
`ReloadStylesheet` (e.g. from the asset pipeline's on-change callback). `MountedApp::SetStylesheetSource`
exists as the internal per-app hook that `Lumora` fans the parsed source into.

---

## 12. Not supported / limitations

- **Selectors:** ids, type/element selectors, sibling combinators (`+`, `~`), attribute selectors,
  pseudo-elements (`::before`/`::after`), functional pseudo-classes (`:not`, `:nth-child`, …).
- **Transitions & `@keyframes`** apply to box-like paint properties (`background-color`, `opacity`,
  `border-radius`, `transform`). Animating text/button-specific or layout properties is not wired.
- **`transform`** is paint-only, like the web: the element and its subtree are drawn through a layer
  transform and hit-testing maps the pointer through its inverse, but the layout box never changes.
  Components always compose as translate, then rotate, then scale about `transform-origin`, whatever
  their source order (repeated functions accumulate). Not supported: `matrix()`, `skew()`, 3D functions,
  px `transform-origin`. A CSS `transform` rule replaces an inline `transform`/translate prop on the same node.
  Focus rings and anchored popups follow the translate only.
- A finished animation without `forwards`/`both` fill holds its last frame until the node next
  re-resolves.
- `%` on paint-time properties (e.g. a `%` padding read by text insets) resolves to 0 in the painter,
  though layout still honours it.
- `display` cannot change a widget's *kind*: `display: grid` only works on a `Grid` widget (which owns the
  track payload) - a plain `Box` cannot become a grid. `display: block` routes to normal flow, and
  `flex-direction` still flips a Row↔Column flex container.
- **Normal flow (`display: block`)** is approximate: margins do not collapse, and the first child's top
  margin folds into the sibling gap rather than offsetting the child. Use `gap`/`padding` for reliable spacing.
- **`position`**: `sticky` is treated as `relative`; `relative` establishes a containing block but does
  **not** offset the box (no `top`/`left` shift on an in-flow element). Percent insets are not modelled
  (an `auto` fallback). `Scroll` cannot be positioned via CSS.
- **Grid CSS**: `%`/`auto`/`min-content`/`max-content` tracks are approximated (`%` → `value/100`fr,
  the rest → `1fr`); `grid-auto-flow` other than `row` is not implemented.
- `order` changes flex layout position only, not paint/hit-test order (that stays tree order; use `z-index`).
- Driving one property via both a bound inline signal and a CSS rule is unsupported (CSS is the sole
  writer of matched properties).
- No `calc()`, gradients, or `box-shadow`.

---

## 13. Architecture (for maintainers)

The engine lives in `Lumora/Source/Style/` (+ public headers in
`Lumora/Include/lumora/Style/`):

- `Tokenizer` / `Parser` / `ValueParse` - CSS text → `Stylesheet` AST (`CssValue`, `Rule`, `Selector`,
  `MediaGroup`, `KeyframesDef`).
- `Matcher` / `Cascade` - selector matching (right-to-left, backtracking) + specificity/order cascade,
  producing a `ComputedStyle`.
- `Resolve` - `var()` substitution, `em/rem/vw/vh` computation, inheritance.
- `Convert` - CSS value → engine types (`Color`, `SizeHint`, `Align`, `Spacing`, `Border`).
- `Transition` / `Keyframes` - timing functions + per-node animation drivers over `OnFrame`.
- `StyleEngine` - per-app owner of the parsed sheet, node state, generation/viewport/media signals.
- `StyleResolver` - the per-node reactive effect that cascades, resolves, and applies (the sole writer
  of styleable fields), plus the transition/animation frame driver.

Each node's styling is a fine-grained reactive effect: it re-runs only when something it depends on
changes (its `className`, an ancestor's classes, a pseudo-state, a `var()` source, the viewport
breakpoint, or a stylesheet reload), then writes the changed fields and marks the node dirty. Layout
properties feed the Masharif flex solver; percentages are handed to the solver natively so there is no
one-frame lag.

Tests: `Lumora/Tests/Style*Tests.cpp` (parser, matcher, apply, pseudo, inherit/vars/media,
transitions, keyframes).
