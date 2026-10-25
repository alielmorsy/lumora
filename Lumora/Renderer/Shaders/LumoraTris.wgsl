// LumoraTris.wgsl - per-vertex-coloured triangle list (Canvas custom paint). Port of LumoraTris.hlsl.

struct PushConstants {
    scale: vec2<f32>,
    translate: vec2<f32>,
    mode: u32, // unused; shared 80-byte layout
    _r0: f32,
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

// Clip coverage mask (clipMode 2); 1x1 white otherwise.
@group(0) @binding(0) var gClipMask: texture_2d<f32>;
@group(0) @binding(1) var gClipSampler: sampler;

struct VSInput {
    @location(0) pos: vec2<f32>,
    @location(2) col: vec4<f32>,
};

struct VSOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) col: vec4<f32>,
};

@vertex
fn VSMain(input: VSInput) -> VSOutput {
    var o: VSOutput;
    let p = input.pos * gPush.scale + gPush.translate;
    o.pos = vec4<f32>(p.x, -p.y, 0.0, 1.0); // Vulkan NDC -> WebGPU NDC
    o.col = input.col;
    return o;
}

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

@fragment
fn PSMain(input: VSOutput) -> @location(0) vec4<f32> {
    return vec4<f32>(input.col.rgb, input.col.a * ClipCoverage(input.pos.xy));
}
