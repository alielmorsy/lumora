// LumoraSDF.wgsl - anti-aliased analytic 2D shapes (circle / ring / arc / pie); one quad per shape. Port of LumoraSDF.hlsl.
// `uv` is the shape-local position in px, pre-rotated on the CPU so the bisector points +Y (IQ sdArc/sdPie frame).

const SHAPE_CIRCLE: u32 = 0u; // filled disc
const SHAPE_RING: u32 = 1u;   // stroked circle (full donut, no seam)
const SHAPE_ARC: u32 = 2u;    // ring segment, round caps
const SHAPE_PIE: u32 = 3u;    // filled wedge

struct PushConstants {
    scale: vec2<f32>,          // 2 / surfaceSize                     (offset 0)
    translate: vec2<f32>,      // -1, -1                              (offset 8)
    shapeKind: u32,            // SHAPE_*                             (offset 16)
    radius: f32,               // disc / centerline radius (px)       (offset 20)
    halfWidth: f32,            // ring / arc stroke half-thickness    (offset 24)
    pad0: u32,                 //                                     (offset 28)
    apertureSinCos: vec2<f32>, // (sin, cos) of the HALF aperture     (offset 32)
    halfSize: vec2<f32>,       // quad half-extent in local px        (offset 40)
    // Shared analytic-clip tail (offset 48).
    clipCenter: vec2<f32>,     // rrect centre (surface px)           (offset 48)
    clipHalf: vec2<f32>,       // rrect half-extent (px)              (offset 56)
    clipRadius: f32,           // corner radius (px)                  (offset 64)
    clipMode: u32,             // 0 none / 1 rrect / 2 mask           (offset 68)
    clipRot: vec2<f32>,        // (cos,sin) of clip orientation       (offset 72)
};
@group(1) @binding(0) var<uniform> gPush: PushConstants;

// Clip coverage mask (clipMode 2); 1x1 white otherwise.
@group(0) @binding(0) var gClipMask: texture_2d<f32>;
@group(0) @binding(1) var gClipSampler: sampler;

struct VSInput {
    @location(0) pos: vec2<f32>,
    @location(1) uv: vec2<f32>,  // shape-local, pre-rotated (px)
    @location(2) col: vec4<f32>, // fill (straight alpha)
};

struct VSOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) local: vec2<f32>,
    @location(1) col: vec4<f32>,
};

@vertex
fn VSMain(input: VSInput) -> VSOutput {
    var o: VSOutput;
    let p = input.pos * gPush.scale + gPush.translate;
    o.pos = vec4<f32>(p.x, -p.y, 0.0, 1.0); // Vulkan NDC -> WebGPU NDC
    o.local = input.uv;
    o.col = input.col;
    return o;
}

fn sdCircle(p: vec2<f32>, r: f32) -> f32 { return length(p) - r; }

fn sdRing(p: vec2<f32>, r: f32, halfWidth: f32) -> f32 { return abs(length(p) - r) - halfWidth; }

// IQ sdArc. sc = (sin, cos) of the half aperture, ra = centerline radius, rb = cap half-thickness.
fn sdArc(pIn: vec2<f32>, sc: vec2<f32>, ra: f32, rb: f32) -> f32 {
    let p = vec2<f32>(abs(pIn.x), pIn.y);
    let d = select(abs(length(p) - ra), length(p - sc * ra), sc.y * p.x > sc.x * p.y);
    return d - rb;
}

// IQ sdPie. c = (sin, cos) of the half aperture, r = radius.
fn sdPie(pIn: vec2<f32>, c: vec2<f32>, r: f32) -> f32 {
    let p = vec2<f32>(abs(pIn.x), pIn.y);
    let l = length(p) - r;
    let m = length(p - c * clamp(dot(p, c), 0.0, r));
    return max(l, m * sign(c.y * p.x - c.x * p.y));
}

// Axis-aligned rounded box SDF, for the shared analytic clip.
fn sdRoundedBox(p: vec2<f32>, hs: vec2<f32>, rIn: f32) -> f32 {
    let r = min(rIn, min(hs.x, hs.y));
    let q = abs(p) - hs + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2<f32>(0.0))) - r;
}

fn ClipCoverage(fragPx: vec2<f32>) -> f32 {
    if (gPush.clipMode == 1u) {
        let d = fragPx - gPush.clipCenter;
        let p = vec2<f32>(gPush.clipRot.x * d.x + gPush.clipRot.y * d.y,
                         -gPush.clipRot.y * d.x + gPush.clipRot.x * d.y);
        let dist = sdRoundedBox(p, gPush.clipHalf, gPush.clipRadius);
        let aa = max(fwidth(dist), 1e-4);
        return 1.0 - smoothstep(-aa, aa, dist);
    }
    if (gPush.clipMode == 2u) {
        let uv = (fragPx - (gPush.clipCenter - gPush.clipHalf)) / (2.0 * gPush.clipHalf);
        return textureSample(gClipMask, gClipSampler, uv).r;
    }
    return 1.0;
}

fn Sdf(p: vec2<f32>) -> f32 {
    switch (gPush.shapeKind) {
        case SHAPE_RING: { return sdRing(p, gPush.radius, gPush.halfWidth); }
        case SHAPE_ARC: { return sdArc(p, gPush.apertureSinCos, gPush.radius, gPush.halfWidth); }
        case SHAPE_PIE: { return sdPie(p, gPush.apertureSinCos, gPush.radius); }
        default: { return sdCircle(p, gPush.radius); }
    }
}

@fragment
fn PSMain(input: VSOutput) -> @location(0) vec4<f32> {
    let dist = Sdf(input.local);
    let aa = max(fwidth(dist), 1e-4);
    let cov = 1.0 - smoothstep(-aa, aa, dist);
    // Straight alpha; the pipeline blend does the over-composite.
    return vec4<f32>(input.col.rgb, input.col.a * cov * ClipCoverage(input.pos.xy));
}
