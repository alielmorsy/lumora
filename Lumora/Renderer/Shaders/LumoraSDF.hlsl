// LumoraSDF.hlsl - anti-aliased analytic 2D shapes (circle / ring / arc / pie); one quad per shape, like LumoraRect.
// `uv` is the shape-local position in px, already rotated on the CPU so the bisector points +Y (Inigo Quilez's sdArc/sdPie
// frame), so no orientation lives in the shader. The first 20 bytes of the push block match the rect/textured base layout.

#define SHAPE_CIRCLE 0u // filled disc:      length(p) - r
#define SHAPE_RING   1u // stroked circle:   abs(length(p) - r) - halfWidth   (full donut, no seam)
#define SHAPE_ARC    2u // ring segment:     IQ sdArc, round caps
#define SHAPE_PIE    3u // filled wedge:     IQ sdPie

struct PushConstants {
    float2 scale;          // 2 / surfaceSize                                (offset 0)
    float2 translate;      // -1, -1                                         (offset 8)
    uint   shapeKind;      // SHAPE_* (shares the 16-byte 'mode' slot)       (offset 16)
    float  radius;         // disc / centerline radius (px)                  (offset 20)
    float  halfWidth;      // ring / arc stroke half-thickness (px)          (offset 24)
    uint   pad0;           // keeps the next float2 8-aligned                (offset 28)
    float2 apertureSinCos; // (sin, cos) of the HALF aperture; arc / pie     (offset 32)
    float2 halfSize;       // quad half-extent in local px (>= reach + AA)   (offset 40)
    // Shared analytic-clip tail (offset 48), identical to the other Lumora shaders.
    float2 clipCenter;     // rrect centre (surface px)                      (offset 48)
    float2 clipHalf;       // rrect half-extent (px)                         (offset 56)
    float  clipRadius;     // corner radius (px)                             (offset 64)
    uint   clipMode;       // 0 none / 1 rrect / 2 mask                      (offset 68)
    float2 clipRot;        // (cos,sin) of clip orientation                  (offset 72)
};
[[vk::push_constant]] PushConstants gPush;

// Arbitrary-shape clip coverage mask (clipMode 2); 1×1 white when None/rrect so it is always valid.
[[vk::combinedImageSampler]] [[vk::binding(0, 0)]] Texture2D    gClipMask;
[[vk::combinedImageSampler]] [[vk::binding(0, 0)]] SamplerState gClipSampler;

struct VSInput {
    [[vk::location(0)]] float2 pos : POSITION;
    [[vk::location(1)]] float2 uv  : TEXCOORD0; // shape-local, pre-rotated (px)
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

// All SDFs are in the canonical frame: shape points +Y, mirror-symmetric across the Y axis.
float sdCircle(float2 p, float r) { return length(p) - r; }

float sdRing(float2 p, float r, float halfWidth) { return abs(length(p) - r) - halfWidth; }

// IQ sdArc - round-capped ring segment. sc = (sin, cos) of the half aperture, ra = centerline radius,
// rb = cap half-thickness.
float sdArc(float2 p, float2 sc, float ra, float rb) {
    p.x = abs(p.x);
    float d = (sc.y * p.x > sc.x * p.y) ? length(p - sc * ra) : abs(length(p) - ra);
    return d - rb;
}

// IQ sdPie - filled wedge. c = (sin, cos) of the half aperture, r = radius.
float sdPie(float2 p, float2 c, float r) {
    p.x = abs(p.x);
    float l = length(p) - r;
    float m = length(p - c * clamp(dot(p, c), 0.0, r));
    return max(l, m * sign(c.y * p.x - c.x * p.y));
}

// Axis-aligned rounded box SDF, for the shared analytic clip.
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

float Sdf(float2 p) {
    switch (gPush.shapeKind) {
        case SHAPE_RING: return sdRing(p, gPush.radius, gPush.halfWidth);
        case SHAPE_ARC:  return sdArc(p, gPush.apertureSinCos, gPush.radius, gPush.halfWidth);
        case SHAPE_PIE:  return sdPie(p, gPush.apertureSinCos, gPush.radius);
        default:         return sdCircle(p, gPush.radius);
    }
}

float4 PSMain(VSOutput input) : SV_Target {
    float dist = Sdf(input.local);
    float aa   = max(fwidth(dist), 1e-4);
    float cov  = 1.0 - smoothstep(-aa, aa, dist);
    // Straight alpha; the pipeline blend (SrcAlpha / OneMinusSrcAlpha) does the over-composite.
    return float4(input.col.rgb, input.col.a * cov * ClipCoverage(input.pos.xy));
}
