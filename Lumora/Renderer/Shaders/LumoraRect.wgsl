// LumoraRect.wgsl - anti-aliased SDF rounded-rect (fill + inner border); one quad per rect. Port of LumoraRect.hlsl.
// `uv` is the rect-local centred position in px (corners at +-halfSize).

struct PushConstants {
    scale: vec2<f32>,     // 2 / surfaceSize
    translate: vec2<f32>, // -1, -1
    mode: u32,            // unused (shared offset with LumoraTextured)
    radius: f32,          // corner radius (px)
    borderWidth: f32,     // inner border thickness (px); 0 -> no border
    borderColor: u32,     // packed R,G,B,A (low->high byte)
    halfSize: vec2<f32>,  // rect half-extent (px)
    borderDash: f32,      // dash length (px); 0 -> solid stroke
    _pad0: f32,
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
    @location(1) uv: vec2<f32>,  // rect-local centred position (px)
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

// Signed distance to a rounded box centred at the origin (negative inside).
fn sdRoundedBox(p: vec2<f32>, hs: vec2<f32>, rIn: f32) -> f32 {
    let r = min(rIn, min(hs.x, hs.y));
    let q = abs(p) - hs + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2<f32>(0.0))) - r;
}

fn UnpackRgba(c: u32) -> vec4<f32> {
    return vec4<f32>(f32(c & 0xFFu), f32((c >> 8u) & 0xFFu),
                     f32((c >> 16u) & 0xFFu), f32((c >> 24u) & 0xFFu)) / 255.0;
}

// 1 when unclipped; AA-ramped oriented rounded box for analytic clips; sampled R8 mask for arbitrary shapes.
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
    let dist = sdRoundedBox(input.local, gPush.halfSize, gPush.radius);
    let aa = max(fwidth(dist), 1e-4);
    let outer = 1.0 - smoothstep(-aa, aa, dist); // coverage of the rounded shape

    let fill = input.col;
    let fa = fill.a * outer;

    var ba = 0.0;
    var brgb = vec3<f32>(0.0);
    if (gPush.borderWidth > 0.0) {
        let inner = 1.0 - smoothstep(-aa, aa, dist + gPush.borderWidth);
        var ring = clamp(outer - inner, 0.0, 1.0);
        let border = UnpackRgba(gPush.borderColor);

        // Dashes run along the side the pixel belongs to (the axis closer to its half extent).
        if (gPush.borderDash > 0.0) {
            let n = abs(input.local) / max(gPush.halfSize, vec2<f32>(1e-4, 1e-4));
            let along = select(input.local.x, input.local.y, n.x > n.y);
            let period = gPush.borderDash * 2.0; // equal dash and gap
            let phase = fract(along / period);
            let w = max(fwidth(along) / period, 1e-4);
            // Ramp both edges of the ON half so a dash end is anti-aliased.
            ring *= smoothstep(-w, w, phase) * (1.0 - smoothstep(0.5 - w, 0.5 + w, phase));
        }

        ba = border.a * ring;
        brgb = border.rgb;
    }

    // Straight-alpha border over fill; the blend state does the over-composite.
    let outA = ba + fa * (1.0 - ba);
    let outRgb = select(fill.rgb, (brgb * ba + fill.rgb * fa * (1.0 - ba)) / outA, outA > 1e-5);
    return vec4<f32>(outRgb, outA * ClipCoverage(input.pos.xy));
}
