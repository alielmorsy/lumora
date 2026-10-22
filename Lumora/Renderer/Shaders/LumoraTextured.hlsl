// LumoraTextured.hlsl - glyph + image quads.
// One combined image sampler at set 0 / binding 0. `mode` selects an R8 coverage mask tinted by the vertex colour (text) or a
// straight RGBA sample modulated by it (emoji/images).

struct PushConstants {
    float2 scale;
    float2 translate;
    uint   mode; // 0 = R8 coverage mask, 1 = RGBA sample
    float  _r0;  // rect params, unused by Textured (kept for one shared 80-byte layout)
    float  _r1;
    uint   _r2;
    float2 _halfSize;
    float2 _pad0;
    // Shared analytic-clip tail (offset 48).
    float2 clipCenter; // rrect centre (surface px)
    float2 clipHalf;   // rrect half-extent (px)
    float  clipRadius; // corner radius (px)
    uint   clipMode;   // 0 none / 1 rrect / 2 mask
    float2 clipRot;    // (cos,sin) of clip orientation
};
[[vk::push_constant]] PushConstants gPush;

[[vk::combinedImageSampler]] [[vk::binding(0, 0)]] Texture2D    gTex;
[[vk::combinedImageSampler]] [[vk::binding(0, 0)]] SamplerState gSampler;

// Arbitrary-shape clip coverage mask at binding 1 (clipMode 2); 1×1 white when None/rrect.
[[vk::combinedImageSampler]] [[vk::binding(1, 0)]] Texture2D    gClipMask;
[[vk::combinedImageSampler]] [[vk::binding(1, 0)]] SamplerState gClipSampler;

float sdRoundedBox(float2 p, float2 half, float r) {
    r = min(r, min(half.x, half.y));
    float2 q = abs(p) - half + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

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

struct VSInput {
    [[vk::location(0)]] float2 pos : POSITION;
    [[vk::location(1)]] float2 uv  : TEXCOORD0;
    [[vk::location(2)]] float4 col : COLOR0;
};

struct VSOutput {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
    float4 col : COLOR0;
};

VSOutput VSMain(VSInput input) {
    VSOutput o;
    o.pos = float4(input.pos * gPush.scale + gPush.translate, 0.0, 1.0);
    o.uv  = input.uv;
    o.col = input.col;
    return o;
}

float4 PSMain(VSOutput input) : SV_Target {
    float clip = ClipCoverage(input.pos.xy);
    if (gPush.mode == 0u) {
        float a = gTex.Sample(gSampler, input.uv).r; // coverage mask
        return float4(input.col.rgb, input.col.a * a * clip);
    }
    float4 c = gTex.Sample(gSampler, input.uv) * input.col; // RGBA, modulated
    return float4(c.rgb, c.a * clip);
}
