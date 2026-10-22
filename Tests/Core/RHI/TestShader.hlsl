struct VSOutput {
    float4 pos : SV_Position;
};

VSOutput VSMain(uint vid : SV_VertexID) {
    VSOutput o;
    float2 uv = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

struct VSInputAttr {
    float2 pos : POSITION;
};

VSOutput VSMainWithAttr(VSInputAttr input) {
    VSOutput o;
    o.pos = float4(input.pos, 0.0, 1.0);
    return o;
}

float4 PSMain(VSOutput input) : SV_Target {
    return float4(1.0, 0.0, 0.0, 1.0);
}
