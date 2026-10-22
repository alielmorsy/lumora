// LumoraMask.hlsl - rasterizes a clip/fill path's antialiased coverage into an R8 mask in a pre-pass.
// Coverage triangles (interior alpha 1 + a 1px feather ribbon) render with Max blend into an R8 target sized to the path's
// bbox; the main pass multiplies the sampled mask into fragment coverage. Coverage rides vertex alpha; rgb is ignored.

struct PushConstants {
    float2 scale;     // 2 / bboxSize (surface px → target NDC)
    float2 translate; // -1 - 2*bboxOrigin/bboxSize
    uint   mode;      // unused; shares the base 20-byte layout
};
[[vk::push_constant]] PushConstants gPush;

struct VSInput {
    [[vk::location(0)]] float2 pos : POSITION;
    [[vk::location(1)]] float2 uv  : TEXCOORD0;
    [[vk::location(2)]] float4 col : COLOR0;
};

struct VSOutput {
    float4 pos : SV_Position;
    float  cov : TEXCOORD0;
};

VSOutput VSMain(VSInput input) {
    VSOutput o;
    o.pos = float4(input.pos * gPush.scale + gPush.translate, 0.0, 1.0);
    o.cov = input.col.a;
    return o;
}

float4 PSMain(VSOutput input) : SV_Target {
    // Max blend unions overlapping coverage without conflation; only .r is stored (R8).
    return float4(input.cov, 0.0, 0.0, 1.0);
}
