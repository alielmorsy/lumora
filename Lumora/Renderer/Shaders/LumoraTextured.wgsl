// LumoraTextured.wgsl - glyph + image quads. Port of LumoraTextured.hlsl.
// `mode` selects an R8 coverage mask tinted by the vertex colour (text) or a straight RGBA sample modulated by it.

struct PushConstants {
    scale: vec2<f32>,
    translate: vec2<f32>,
    mode: u32, // 0 = R8 coverage mask, 1 = RGBA sample
    _r0: f32,  // rect params, unused (one shared 80-byte layout)
    _r1: f32,
    _r2: u32,
    _halfSize: vec2<f32>,
    _pad0: vec2<f32>,
    // Shared analytic-clip tail (offset 48).
    clipCenter: vec2<f32>, // rrect centre (surface px)
    clipHalf: vec2<f32>,   // rrect half-extent (px)
    clipRadius: f32,       // corner radius (px)
    clipMode: u32,         // 0 none / 1 rrect / 2 mask
    clipRot: vec2<f32>,    // (cos,sin) of clip orientation
};
@group(1) @binding(0) var<uniform> gPush: PushConstants;

@group(0) @binding(0) var gTex: texture_2d<f32>;
@group(0) @binding(1) var gSampler: sampler;

// Clip coverage mask (clipMode 2); 1x1 white otherwise.
@group(0) @binding(2) var gClipMask: texture_2d<f32>;
@group(0) @binding(3) var gClipSampler: sampler;

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

struct VSInput {
    @location(0) pos: vec2<f32>,
    @location(1) uv: vec2<f32>,
    @location(2) col: vec4<f32>,
};

struct VSOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) col: vec4<f32>,
};

@vertex
fn VSMain(input: VSInput) -> VSOutput {
    var o: VSOutput;
    let p = input.pos * gPush.scale + gPush.translate;
    o.pos = vec4<f32>(p.x, -p.y, 0.0, 1.0); // Vulkan NDC -> WebGPU NDC
    o.uv = input.uv;
    o.col = input.col;
    return o;
}

@fragment
fn PSMain(input: VSOutput) -> @location(0) vec4<f32> {
    let clip = ClipCoverage(input.pos.xy);
    if (gPush.mode == 0u) {
        let a = textureSample(gTex, gSampler, input.uv).r; // coverage mask
        return vec4<f32>(input.col.rgb, input.col.a * a * clip);
    }
    let c = textureSample(gTex, gSampler, input.uv) * input.col; // RGBA, modulated
    return vec4<f32>(c.rgb, c.a * clip);
}
