// LumoraMask.wgsl - rasterizes a clip/fill path's antialiased coverage into an R8 mask in a pre-pass.
// Coverage rides vertex alpha; rgb is ignored. Port of LumoraMask.hlsl.

struct PushConstants {
    scale: vec2<f32>,     // 2 / bboxSize
    translate: vec2<f32>, // -1 - 2*bboxOrigin/bboxSize
    mode: u32,            // unused; shares the base 20-byte layout
};
@group(0) @binding(0) var<uniform> gPush: PushConstants;

struct VSInput {
    @location(0) pos: vec2<f32>,
    @location(2) col: vec4<f32>,
};

struct VSOutput {
    @builtin(position) pos: vec4<f32>,
    @location(0) cov: f32,
};

@vertex
fn VSMain(input: VSInput) -> VSOutput {
    var o: VSOutput;
    let p = input.pos * gPush.scale + gPush.translate;
    o.pos = vec4<f32>(p.x, -p.y, 0.0, 1.0); // Vulkan NDC -> WebGPU NDC
    o.cov = input.col.a;
    return o;
}

@fragment
fn PSMain(input: VSOutput) -> @location(0) vec4<f32> {
    // Max blend unions overlapping coverage; only .r is stored (R8).
    return vec4<f32>(input.cov, 0.0, 0.0, 1.0);
}
