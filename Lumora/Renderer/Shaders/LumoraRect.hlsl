// LumoraRect.hlsl - anti-aliased SDF rounded-rect (fill + inner border); one quad per rect.
// `uv` is the rect-local centred position in px (corners at +-halfSize); radius/border/half extent ride in push constants.
// The first 20 bytes of the push block are shared with LumoraTextured (one base layout).

struct PushConstants {
    float2 scale;       // 2 / surfaceSize
    float2 translate;   // -1, -1  (top-left pixel origin → Vulkan NDC)
    uint   mode;        // unused by the rect shader (shared offset with LumoraTextured)
    float  radius;      // corner radius (px)
    float  borderWidth; // inner border thickness (px); 0 → no border
    uint   borderColor; // packed R,G,B,A (low→high byte)
    float2 halfSize;    // rect half-extent (px)
    float  borderDash;  // dash length (px); 0 → solid stroke
    float  _pad0;       // pad to the 16-aligned clip tail
    // Shared analytic-clip tail (offset 48): an oriented rounded box the fragment multiplies coverage by.
    float2 clipCenter;  // rrect centre (surface px)
    float2 clipHalf;    // rrect half-extent (px)
    float  clipRadius;  // corner radius (px)
    uint   clipMode;    // 0 none / 1 rrect / 2 mask
    float2 clipRot;     // (cos,sin) of clip orientation
};
[[vk::push_constant]] PushConstants gPush;

// Arbitrary-shape clip coverage mask (clipMode 2). The painter binds a 1x1 white texture otherwise, so it is always valid.
[[vk::combinedImageSampler]] [[vk::binding(0, 0)]] Texture2D    gClipMask;
[[vk::combinedImageSampler]] [[vk::binding(0, 0)]] SamplerState gClipSampler;

struct VSInput {
    [[vk::location(0)]] float2 pos : POSITION;
    [[vk::location(1)]] float2 uv  : TEXCOORD0; // rect-local centred position (px)
    [[vk::location(2)]] float4 col : COLOR0;    // fill (straight alpha)
};

struct VSOutput {
    float4 pos   : SV_Position;
    float2 local : TEXCOORD0;
    float4 col   : COLOR0;
};

VSOutput VSMain(VSInput input) {
    VSOutput o;
    o.pos   = float4(input.pos * gPush.scale + gPush.translate, 0.0, 1.0);
    o.local = input.uv;
    o.col   = input.col;
    return o;
}

// Signed distance to a rounded box centred at the origin (negative inside).
float sdRoundedBox(float2 p, float2 half, float r) {
    r = min(r, min(half.x, half.y));
    float2 q = abs(p) - half + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float4 UnpackRgba(uint c) {
    return float4(float(c & 0xFFu), float((c >> 8) & 0xFFu),
                  float((c >> 16) & 0xFFu), float((c >> 24) & 0xFFu)) / 255.0;
}

// Clip coverage at a framebuffer pixel (`fragPx` = SV_Position.xy): 1 when unclipped; AA-ramped rounded/oriented box for
// analytic clips; sampled R8 mask for arbitrary shapes (clipCenter/clipHalf carry its bbox).
float ClipCoverage(float2 fragPx) {
    if (gPush.clipMode == 1u) {
        float2 d = fragPx - gPush.clipCenter;
        float2 p = float2(gPush.clipRot.x * d.x + gPush.clipRot.y * d.y,
                         -gPush.clipRot.y * d.x + gPush.clipRot.x * d.y);
        float  dist = sdRoundedBox(p, gPush.clipHalf, gPush.clipRadius);
        float  aa   = max(fwidth(dist), 1e-4);
        return 1.0 - smoothstep(-aa, aa, dist);
    }
    if (gPush.clipMode == 2u) {
        float2 uv = (fragPx - (gPush.clipCenter - gPush.clipHalf)) / (2.0 * gPush.clipHalf);
        return gClipMask.Sample(gClipSampler, uv).r;
    }
    return 1.0;
}

float4 PSMain(VSOutput input) : SV_Target {
    float dist  = sdRoundedBox(input.local, gPush.halfSize, gPush.radius);
    float aa    = max(fwidth(dist), 1e-4);
    float outer = 1.0 - smoothstep(-aa, aa, dist); // coverage of the rounded shape

    float4 fill = input.col;
    float  fa   = fill.a * outer;

    float  ba   = 0.0;
    float3 brgb = float3(0.0, 0.0, 0.0);
    if (gPush.borderWidth > 0.0) {
        float  inner  = 1.0 - smoothstep(-aa, aa, dist + gPush.borderWidth);
        float  ring   = saturate(outer - inner);
        float4 border = UnpackRgba(gPush.borderColor);

        // Dashes run along the side the pixel belongs to (the axis closer to its half extent). A true perimeter walk needs
        // arc-length parameterisation; this is two compares with a phase break at each corner.
        if (gPush.borderDash > 0.0) {
            float2 n      = abs(input.local) / max(gPush.halfSize, float2(1e-4, 1e-4));
            float  along  = (n.x > n.y) ? input.local.y : input.local.x;
            float  period = gPush.borderDash * 2.0; // equal dash and gap
            float  phase  = frac(along / period);
            float  w      = max(fwidth(along) / period, 1e-4);
            // Ramp both edges of the ON half so a dash end is anti-aliased like every other edge here.
            ring *= smoothstep(-w, w, phase) * (1.0 - smoothstep(0.5 - w, 0.5 + w, phase));
        }

        ba   = border.a * ring;
        brgb = border.rgb;
    }

    // Straight-alpha border over fill; the blend state does the over-composite.
    float  outA   = ba + fa * (1.0 - ba);
    float3 outRgb = outA > 1e-5 ? (brgb * ba + fill.rgb * fa * (1.0 - ba)) / outA : fill.rgb;
    return float4(outRgb, outA * ClipCoverage(input.pos.xy));
}
