struct VSInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float4 tangent : TANGENT;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
};

struct VSOut {
  float4 position : SV_Position;
};

cbuffer RootConsts : register(b0) {
  float4x4 lightViewProj;
  float4x4 world;
};

VSOut VSMain(VSInput input) {
  VSOut o;
  float4 wp = mul(world, float4(input.position, 1.0));
  o.position = mul(lightViewProj, wp);
  return o;
}

float4 PSMain(VSOut input) : SV_Target {
  return float4(input.position.z, 0, 0, 1);
}
