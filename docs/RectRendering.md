# Rectangle Rendering in Lumora

How a single UI rectangle travels from a `RectCmd` to lit pixels on the framebuffer, and every mechanism that touches it along the way: the batch record, the vertex quad, the push-constant block, the signed-distance field in the pixel shader, anti-aliasing, border compositing, and the three layers of clipping (scissor, analytic rounded-rect, mask).

Scope is **rectangles only**. Shapes (`AddShape`), glyph runs (`AddGlyphs`), images (`AddImage`), and raw triangles (`AddTriangles`) share the same batch/clip machinery but use different pipelines and are out of scope here.

The two files that own this path:

- `Lumora/Renderer/Source/BatchBuilder.cpp` - CPU side. Turns a `RectCmd` into a `Batch` + four vertices + six indices.
- `Lumora/Renderer/Shaders/LumoraRect.hlsl` - GPU side. One quad in, an anti-aliased rounded rect (fill + inner border) out, with coverage multiplied by the active clip.

---

## 1. The core idea

A Lumora rectangle is **not** a mesh of the rounded shape. It is always exactly one axis-aligned quad (4 verts, 6 indices). All the geometry - corner rounding, the border ring, the anti-aliased edge - is reconstructed **per pixel** in the fragment shader from a signed-distance field (SDF).

Consequences:

- A rect with a background fill *and* an outline border is **one draw call**. No second pass, no second geometry.
- Rects never merge with each other into a shared batch. Each rect carries its own radius/border in push constants, so `AddRect` pushes a **dedicated batch** every time (unlike glyphs, which bulk-append into one batch via `OpenOrExtend`).
- Rounding and border thickness cost nothing in vertices - they are pure shader math.

---

## 2. CPU side - `BatchBuilder::AddRect`

```cpp
void BatchBuilder::AddRect(const RectCmd& rect);
```

Given `rect.bounds` (a `Rect` = x, y, width, height in layout px), `rect.fill`, `rect.radius`, `rect.borderWidth`, `rect.borderColor`:

### 2.1 Half-extent

```cpp
const float hx = dst.width  * 0.5f;
const float hy = dst.height * 0.5f;
```

`hx, hy` are the rect half-size. These become both the vertex `uv` corners (`±hx, ±hy`) and the shader's `halfSize` - the SDF measures distance from the rect centre, so the shader needs to know where the edges are.

### 2.2 The dedicated batch

```cpp
_batches.push_back(Batch{
    .firstIndex      = <current index count>,
    .indexCount      = 6,
    .scissor         = _clips.back().scissor,   // hardware scissor for this rect
    .clip            = _clips.back().ref,        // analytic/mask clip descriptor
    .pipeline        = PipelineKind::Rect,
    .textureKey      = 0,
    .rectHalf        = {hx, hy},
    .rectRadius      = rect.radius,
    .rectBorderWidth = rect.borderWidth,
    .rectBorderColor = Pack(rect.borderColor, l.opacity),
});
```

Note the batch is pushed **unconditionally** - no `OpenOrExtend` - because the per-rect `rectHalf/rectRadius/rectBorderWidth/rectBorderColor` fields are what the renderer feeds into push constants, and two different rects can never share those. That's the reason each rect is its own draw.

The `rect*` batch fields map one-to-one onto the shader's push block (§4): `rectHalf → halfSize`, `rectRadius → radius`, `rectBorderWidth → borderWidth`, `rectBorderColor → borderColor`. `scissor` becomes the Vulkan scissor rect; `clip` becomes the analytic-clip tail of the push block.

### 2.3 The quad - position vs. uv

```cpp
const std::uint32_t base = _verts.size();
_verts.push_back({.pos = Apply(l, {dst.x,             dst.y             }), .uv = {-hx, -hy}, .rgba = fill});
_verts.push_back({.pos = Apply(l, {dst.x + dst.width, dst.y             }), .uv = { hx, -hy}, .rgba = fill});
_verts.push_back({.pos = Apply(l, {dst.x + dst.width, dst.y + dst.height}), .uv = { hx,  hy}, .rgba = fill});
_verts.push_back({.pos = Apply(l, {dst.x,             dst.y + dst.height}), .uv = {-hx,  hy}, .rgba = fill});
```

Two distinct coordinate channels ride on each vertex:

| Channel | Meaning | Space |
|---------|---------|-------|
| `pos`   | Where the corner lands on screen | Surface px, with the layer transform folded in via `Apply` |
| `uv`    | Rect-local **centred** position | Rect px, corners at `±half`, centre at `(0,0)` |

`uv` is the input the SDF measures from. It deliberately does **not** carry the layer transform - the distance field is evaluated in the rect's own undistorted local frame, so a rotated/scaled layer moves the quad on screen (`pos`) but the rounded corners stay geometrically correct in local space (`uv`). Rotation of the *clip* is handled separately (§6.2).

Winding: two triangles `(0,1,2)` and `(0,2,3)` - standard quad.

### 2.4 `Apply` - the layer transform

```cpp
Vec2 BatchBuilder::Apply(const Layer& l, Vec2 p) const noexcept {
    return {
        l.transform[0]*p.x + l.transform[3]*p.y + l.transform[6],
        l.transform[1]*p.x + l.transform[4]*p.y + l.transform[7]
    };
}
```

`l.transform` is a **column-major 3×3** affine matrix (index = `col*3 + row`), composed as layers are pushed (`PushLayer` does `Mul3(parent, child)`). Columns 0 and 1 are the 2×2 linear part; column 2 (`[6],[7]`) is the translation. The base layer is seeded with `contentScale` on the diagonal in `Begin`, so DPI scaling is just the root layer transform.

### 2.5 Colour packing

```cpp
std::uint32_t BatchBuilder::Pack(Color c, float opacity) const noexcept {
    const std::uint32_t a = MulU8(c.a, opacity);              // premultiply layer opacity into alpha
    return  r | (g << 8) | (b << 16) | (a << 24);              // low→high byte = R,G,B,A
}
```

Fill and border colours are packed to `0xAABBGGRR` byte order. Layer opacity is folded into the alpha channel here on the CPU (`MulU8` rounds and clamps). The vertex `rgba` is the fill; `rectBorderColor` is the packed border colour, unpacked again in the shader by `UnpackRgba`.

---

## 3. Vertex layout reaching the shader

`VSInput` in the shader:

```hlsl
[[vk::location(0)]] float2 pos : POSITION;   // surface px (transform already applied on CPU)
[[vk::location(1)]] float2 uv  : TEXCOORD0;  // rect-local centred position (±half)
[[vk::location(2)]] float4 col : COLOR0;     // fill, straight alpha
```

`VSMain` does only the surface-px → Vulkan NDC map and passes `uv`/`col` through:

```hlsl
o.pos   = float4(input.pos * gPush.scale + gPush.translate, 0.0, 1.0);
o.local = input.uv;
o.col   = input.col;
```

With `scale = 2 / surfaceSize` and `translate = (-1,-1)`, a surface pixel `(0,0)` maps to NDC `(-1,-1)` (Vulkan top-left) and `(surfaceW, surfaceH)` to `(+1,+1)`. Z is `0.0` (UI is orthographic, depth unused). `o.local` is the interpolated rect-local position - this is what the pixel shader plugs into the SDF.

---

## 4. Push constants (`PushConstants gPush`)

```hlsl
struct PushConstants {
    float2 scale;        // 2 / surfaceSize
    float2 translate;    // -1,-1
    uint   mode;         // unused by rect (shared offset with LumoraTextured)
    float  radius;       // corner radius (px)
    float  borderWidth;  // inner border thickness (px); 0 → no border
    uint   borderColor;  // packed RGBA
    float2 halfSize;     // rect half-extent (px)
    float2 _pad0;        // pad to 16-aligned clip tail
    // --- shared analytic-clip tail, offset 48 ---
    float2 clipCenter;   // clip rrect centre (surface px)
    float2 clipHalf;     // clip rrect half-extent (px)
    float  clipRadius;   // clip corner radius (px)
    uint   clipMode;     // 0 none / 1 rrect / 2 mask
    float2 clipRot;      // (cos,sin) of clip orientation
};
```

Two deliberate layout contracts:

1. **First 20 bytes** (`scale`, `translate`, `mode`) are shared byte-for-byte with `LumoraTextured`, so the painter keeps **one base pipeline layout** across the rect and textured pipelines. `mode` is dead weight for rects - it only exists to preserve that shared offset.
2. **The clip tail starts at offset 48** and is also shared across shaders. `_pad0` exists purely to push `clipCenter` to that 16-byte-aligned boundary. This is what lets `ClipCoverage` be copy-identical between the rect and other Lumora shaders.

The renderer fills `radius/borderWidth/borderColor/halfSize` from the batch's `rect*` fields, and `clip*` from the batch's `clip` (`ClipRef`) descriptor.

---

## 5. Pixel shader - building the rect from an SDF

### 5.1 Signed distance to a rounded box

```hlsl
float sdRoundedBox(float2 p, float2 half, float r) {
    r = min(r, min(half.x, half.y));         // radius can't exceed the smaller half-extent
    float2 q = abs(p) - half + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}
```

Standard Inigo-Quilez rounded-box SDF. `p` is the rect-local pixel position (`input.local`). Returns **negative inside**, `0` on the edge, positive outside. The `r = min(...)` clamp means an over-large `radius` degrades gracefully to a capsule/circle rather than inverting.

### 5.2 Anti-aliasing via `fwidth`

```hlsl
float dist  = sdRoundedBox(input.local, gPush.halfSize, gPush.radius);
float aa    = max(fwidth(dist), 1e-4);
float outer = 1.0 - smoothstep(-aa, aa, dist);   // coverage 1 inside → 0 outside
```

`fwidth(dist)` = screen-space rate of change of the distance field ≈ how many world units one pixel spans at this fragment. Using it as the `smoothstep` half-width gives a **~1px** AA ramp regardless of scale/zoom - the edge stays crisp whether the rect is tiny or fills the screen. The `max(…, 1e-4)` guards the degenerate zero-derivative case. `outer` is the fill coverage of the rounded silhouette.

### 5.3 Border ring

```hlsl
float ba = 0.0; float3 brgb = 0;
if (gPush.borderWidth > 0.0) {
    float  inner  = 1.0 - smoothstep(-aa, aa, dist + gPush.borderWidth);  // shape shrunk inward
    float  ring   = saturate(outer - inner);                              // annulus = outer minus inner
    float4 border = UnpackRgba(gPush.borderColor);
    ba   = border.a * ring;
    brgb = border.rgb;
}
```

The border is an **inner** border: `dist + borderWidth` shifts the SDF so `inner` is the coverage of a rect inset by `borderWidth`. `outer - inner` is the ring between the true edge and the inset edge - the border band, AA'd on both sides for free. `borderWidth = 0` skips the whole block (no border).

### 5.4 Border-over-fill composite (straight alpha)

```hlsl
float  outA   = ba + fa * (1.0 - ba);
float3 outRgb = outA > 1e-5 ? (brgb*ba + fill.rgb*fa*(1.0 - ba)) / outA : fill.rgb;
return float4(outRgb, outA * ClipCoverage(input.pos.xy));
```

Where `fa = fill.a * outer`. This is a straight-alpha **over** composite of the border on top of the fill, done inside the shape so the single fragment already carries the combined colour. The division by `outA` un-premultiplies back to straight alpha (the pipeline blend state expects straight alpha and does the final over-composite against the framebuffer). The guard returns plain fill colour where total coverage is ~0 to avoid a divide-by-zero.

Final alpha is multiplied by `ClipCoverage` - this is where clipping enters the pixel.

---

## 6. Clipping - three cooperating layers

Clipping in Lumora is **defence in depth**: a cheap conservative hardware reject, plus an exact per-pixel coverage term. A rect gets both.

### 6.1 Layer 1 - the hardware scissor (`ClipToScissor`)

```cpp
ScissorRect BatchBuilder::ClipToScissor(const Rect& bounds);
```

Every clip push resolves to an axis-aligned integer scissor rectangle, stored on the batch and set as the Vulkan scissor before the draw. It is a **conservative** bound - it never clips too little, may clip too much only in the sense of being an AABB around a rotated shape (the analytic term below cleans that up).

Steps:

1. Transform all **four** corners of `bounds` through the current layer transform (`Apply`) - not just min/max, because a rotated layer turns the rect into a diamond whose AABB needs all corners.
2. If the layer's off-diagonal terms `transform[1]` or `transform[3]` are non-zero, set `_sawSkewedClip = true` - a flag telling the painter the scissor AABB over-draws a rotated/sheared clip (the analytic clip is what actually trims it to shape).
3. Take the min/max of the four transformed corners → floating-point AABB.
4. **Intersect** with the parent scissor (`_clips.back().scissor`): `ix0 = max(floor(minX), parent.x0)`, etc. Clips nest by intersection, so a child can only ever shrink the visible region.
5. `floor` the mins and `ceil` the maxes → integer pixels that fully contain the AABB (conservative - never crops a covered pixel).
6. Empty result (`ix1 <= ix0`) collapses to a zero-size scissor `{ix0, iy0, 0, 0}` → nothing draws.

This alone is enough for a plain axis-aligned, non-rounded clip: the scissor is *exact* in that case and no analytic term is needed.

### 6.2 Layer 2 - the analytic rounded-rect clip (`ComputeClip`)

```cpp
ClipState BatchBuilder::ComputeClip(const Rect& bounds, float radius);
```

When a clip has **rounded corners** or is **rotated**, the axis-aligned scissor can't represent it, so the shader is handed an oriented rounded box to multiply coverage by (`clipMode = 1`). `ComputeClip` builds that descriptor:

- `out.scissor = ClipToScissor(bounds)` - always compute the conservative AABB first.
- **Mask parent short-circuit**: if the parent clip is a `Mask`, its intersection with an rrect isn't an rrect, so the child just inherits the mask and only tightens the scissor. (True mask∩shape is a documented follow-up.)
- **Decompose the 2×2** into per-axis scale `sx, sy` and an orthogonality test `dotAxes`:
  - `sx = length(col0)`, `sy = length(col1)`.
  - `ortho = |dot(col0, col1)| <= 1e-4 * (sx*sy + eps)` - true for rotation+scale, false for a shear.
  - `rotated = transform[1] != 0 || transform[3] != 0`.
- **Emit an oriented rrect** (`ClipMode::Rrect`) when `(radius > 0 || rotated) && ortho && sx,sy > eps`:
  - `center` = transformed rect centre (surface px).
  - `halfExtent = {bounds.w*0.5*sx, bounds.h*0.5*sy}` - scaled half-size.
  - `radius = min(radius*0.5*(sx+sy), min(halfX, halfY))` - scaled corner radius, clamped.
  - `rot = {transform[0]/sx, transform[1]/sx}` = `(cos, sin)` of the layer's orientation.
  - If the parent already had a non-None analytic clip, sets `_sawUnsupportedClipCombo = true` - two independent rrect clips can't fold into one rrect, so the inner shape wins for now (mask fallback is the eventual fix).
- **Otherwise** (plain axis-aligned box, or a shape it can't represent like a shear) `out.ref = parent.ref` - inherit the ancestor's analytic clip so a square child keeps a rounded ancestor's corners, and rely on the scissor for its own bound.

The resulting `ClipRef` (`mode`, `center`, `halfExtent`, `radius`, `rot`) lands in the batch's `clip` and flows into the shader's `clip*` push constants.

#### Shader side - `ClipCoverage` mode 1

```hlsl
if (gPush.clipMode == 1u) {
    float2 d = fragPx - gPush.clipCenter;
    float2 p = float2( clipRot.x*d.x + clipRot.y*d.y,     // rotate frag into the clip's local frame
                      -clipRot.y*d.x + clipRot.x*d.y);
    float  dist = sdRoundedBox(p, clipHalf, clipRadius);
    float  aa   = max(fwidth(dist), 1e-4);
    return 1.0 - smoothstep(-aa, aa, dist);               // same AA ramp as the rect itself
}
```

`fragPx` is `SV_Position.xy` (the framebuffer pixel). The fragment is rotated into the clip's local frame by the inverse rotation, then the **same** `sdRoundedBox` + `fwidth` AA machinery produces a 0..1 coverage. This is why a rotated or rounded clip edge is anti-aliased, not stair-stepped - the scissor AABB lets those pixels through, and this term feathers them to the true oriented shape.

### 6.3 Layer 3 - arbitrary mask (`clipMode = 2`, `PushClipMask`)

For clip shapes that aren't a rounded box at all, `PushClipMask(maskId, bounds)` records a `ClipMode::Mask` with the mask's surface-px bbox as `center`/`halfExtent`. Because the bbox is already in surface px (the caller resolved it), it intersects the parent scissor **directly**, without running the layer transform through `ClipToScissor`.

Shader side:

```hlsl
if (gPush.clipMode == 2u) {
    float2 uv = (fragPx - (clipCenter - clipHalf)) / (2.0 * clipHalf);  // bbox → 0..1
    return gClipMask.Sample(gClipSampler, uv).r;                        // R8 coverage
}
```

The coverage mask texture is **baked into the descriptor layout**:

```hlsl
[[vk::combinedImageSampler]] [[vk::binding(0,0)]] Texture2D    gClipMask;
[[vk::combinedImageSampler]] [[vk::binding(0,0)]] SamplerState gClipSampler;
```

The painter binds a **1×1 white texture** when the clip is None/rrect, so the sampler is always valid but only actually read in mode 2. (Per project convention, samplers are always baked into the layout - you won't see them bound per-draw.)

### 6.4 `clipMode = 0` - no clip

`ClipCoverage` returns `1.0`; the rect is bounded by the scissor alone.

---

## 6.5 `ClipCoverage` - where it lives and when it activates

`ClipCoverage(float2 fragPx)` runs **once per fragment, on every rect**, always. It is called unconditionally on the last line of `PSMain`:

```hlsl
return float4(outRgb, outA * ClipCoverage(input.pos.xy));
```

`input.pos.xy` is `SV_Position.xy` - the fragment's framebuffer pixel. The function branches on `gPush.clipMode`, so "activation" is entirely a matter of **what value the renderer wrote into that push constant** for this rect's batch. There is no separate enable/disable - an unclipped rect simply runs the `clipMode == 0` path and gets `1.0`.

### 6.5.1 The activation chain - from widget tree to fragment

A rect only gets a non-trivial `ClipCoverage` because a **clipping ancestor was on the canvas clip stack** when its `DrawRect` was recorded. Nothing on the rect itself requests clipping. The chain:

1. **Paint walk** (`Runtime/Paint.cpp`, `EmitNode`). A node with a `ViewState` whose `clip == true` (or any `Scroll`, unconditionally) calls `canvas.PushClip(ClipBox(n, xf), RadiusOf(...))` **before descending to its children**, and `canvas.PopClip()` after. Leaf controls (Button, Text, …) have no `ViewState`, never push a clip, and inherit whatever is on the stack.
2. **BatchBuilder** turns that into stack state: `PushClip → ComputeClip` produces a `ClipState { scissor, ref }` and pushes it onto `_clips`. `ref` (a `ClipRef`) is the descriptor with `mode`/`center`/`halfExtent`/`radius`/`rot`/`maskId`.
3. **`AddRect`** snapshots the *current top of the clip stack* into the batch at record time:
   ```cpp
   .scissor = _clips.back().scissor,
   .clip    = _clips.back().ref,
   ```
   So the clip is frozen onto the batch - the rect "remembers" which clip was active when it was drawn, regardless of what happens to the stack afterward.
4. **Renderer** (batch consumer) copies `batch.clip` into the shader's `clip*` push constants (`clipMode`, `clipCenter`, `clipHalf`, `clipRadius`, `clipRot`) and sets `batch.scissor` as the Vulkan scissor, then issues the draw.
5. **Fragment** runs `ClipCoverage(SV_Position.xy)`, which reads those push constants and returns the coverage the final alpha is multiplied by.

### 6.5.2 Which branch runs, and when

`ClipMode` is decided on the CPU in `ComputeClip`/`PushClipMask` and reaches the shader as `gPush.clipMode`:

| `clipMode` | Set by | When | Shader does |
|-----------|--------|------|-------------|
| `0` None  | default `ClipRef`; `ComputeClip` else-branch on a plain axis-aligned box | no clipping ancestor, or the clip is an exact axis-aligned non-rounded box (scissor already exact) | returns `1.0` - scissor is the only bound |
| `1` Rrect | `ComputeClip` when `(radius > 0 \|\| rotated) && ortho` | a clipping ancestor with rounded corners, or under a rotated layer | rebuilds the oriented rounded box via `sdRoundedBox`, AA-ramps it with `fwidth`, returns 0..1 |
| `2` Mask  | `PushClipMask(maskId, bounds)` with `maskId != 0` | an arbitrary (non-box) clip shape | samples `gClipMask` R8 coverage at the bbox-relative uv |

Key subtlety: a **plain rounded background** (a Box with `radius` but `clip == false`) does **not** activate `ClipCoverage` mode 1 - the *rect's own* rounding is done by the rect SDF in `PSMain` (§5), not by the clip term. Mode 1 activates only when a node with `clip == true` (or a rotated layer) makes the ambient clip un-representable by the scissor. The two rounded-box evaluations are independent: one shapes the rect (`halfSize`/`radius` push constants), the other crops it (`clip*` push constants).

### 6.5.3 Why it's always called (no dynamic branch to skip it)

The unconditional call keeps the pipeline and push layout uniform: every rect uses the same shader, the same descriptor set (the mask sampler is always bound - 1×1 white when unused, §6.3), and the same push block. Cost of the `clipMode == 0` path is a single `uint` compare and an early `return 1.0`, so there is no reason to specialize a "no-clip" pipeline variant. Activation is pure data (`clipMode` + the `clip*` tail), never a pipeline switch.

---

## 7. End-to-end summary

1. **`RectCmd`** arrives with bounds, fill, radius, borderWidth, borderColor.
2. **`AddRect`** packs colours (layer opacity folded into alpha), pushes a dedicated `Batch` carrying the scissor, clip descriptor, and per-rect radius/border, then emits one quad: `pos` = transformed corners, `uv` = rect-local `±half`.
3. **Scissor** (`ClipToScissor`) gives a conservative integer AABB, intersected down the clip stack; **`ComputeClip`** additionally produces an oriented-rounded-box or mask clip descriptor when the scissor can't express the shape exactly.
4. Renderer sets the Vulkan scissor, fills push constants (`halfSize/radius/borderWidth/borderColor` + `clip*`), binds the mask texture (real or 1×1 white), draws 6 indices on the `Rect` pipeline.
5. **`VSMain`** maps corners to Vulkan NDC, passes `local`/`col` through.
6. **`PSMain`** rebuilds the rounded box from the SDF, AA-feathers the edge with `fwidth`, composites the inner border over the fill in straight alpha, and multiplies final alpha by `ClipCoverage` (rounded/oriented box, or sampled mask, or 1).
7. The pipeline blend state does the final straight-alpha over-composite against the framebuffer.

---

## 8. Key invariants & gotchas

- **One rect = one draw.** Rects never batch-merge; radius/border live in push constants that differ per rect.
- **`uv` is transform-free by design.** The SDF must run in the rect's undistorted local frame. Layer transform lives only in `pos` (and, for clip orientation, in `clipRot`).
- **Scissor is conservative, analytic clip is exact.** For a rotated/rounded clip the scissor deliberately over-draws (AABB) and `ClipCoverage` trims the surplus with an AA ramp. `_sawSkewedClip` flags the over-draw case.
- **Radius auto-clamps** in `sdRoundedBox` (`r = min(r, min(half.x, half.y))`) - an over-large radius becomes a stadium/circle, never inverts.
- **Border is inner-only**, realized as `outer - inner` of two SDFs; `borderWidth = 0` skips it.
- **Push layout is a cross-shader contract:** first 20 bytes shared with `LumoraTextured`, clip tail at offset 48. `mode` and `_pad0` exist solely to hold those offsets.
- **Mask sampler is always bound** (1×1 white when unused) because it's baked into the descriptor layout.
- **Mask ∩ analytic shape is not yet exact** - a mask parent forces children to inherit the mask and only tighten the scissor; two independent rrect clips collapse to the inner one (`_sawUnsupportedClipCombo`). Both are noted follow-ups.
