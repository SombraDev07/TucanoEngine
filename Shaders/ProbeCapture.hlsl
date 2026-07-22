#include "Common.hlsl"

// Probe cube-face color capture (unlit albedo + cheap sun term).
struct VSInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float4 tangent : TANGENT;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
};

struct VSOut {
  float4 position : SV_Position;
  float3 worldNormal : TEXCOORD0;
  float2 uv : TEXCOORD1;
  float4 color : COLOR;
};

cbuffer RootConsts : register(b0) {
  float4x4 viewProj;
  float4x4 world;
};

cbuffer ObjectCB : register(b2) {
  float4x4 worldInvTranspose;
  float4 baseColorFactor;
  float4 materialParams;
  float4 emissiveFactor;
  uint4 textureIndices;
  float4 materialExt;
  uint4 textureIndices2;
  float4 fuzzColor;
};

cbuffer ProbeCaptureCB : register(b1) {
  float4 sunDirIntensity; // xyz dir (to light), w intensity
  float4 ambientColor;
};

Texture2D bindlessHeap[] : register(t0, space0);
SamplerState samp : register(s0);

VSOut VSMain(VSInput input) {
  VSOut o;
  float4 wp = mul(world, float4(input.position, 1.0));
  o.position = mul(viewProj, wp);
  o.worldNormal = normalize(mul((float3x3)worldInvTranspose, input.normal));
  o.uv = input.uv;
  o.color = input.color;
  return o;
}

float4 PSMain(VSOut input) : SV_Target {
  Texture2D albedoMap = bindlessHeap[NonUniformResourceIndex(textureIndices.x)];
  float4 albedo = albedoMap.Sample(samp, input.uv) * baseColorFactor * input.color;
  if (materialParams.w > 0.0 && albedo.a < materialParams.w) {
    discard;
  }
  float3 n = normalize(input.worldNormal);
  float3 L = normalize(sunDirIntensity.xyz);
  float ndotl = saturate(dot(n, L));
  float3 lit = albedo.rgb * (ambientColor.rgb + sunDirIntensity.w * ndotl);
  lit += emissiveFactor.rgb;
  return float4(max(lit, 0), 1);
}
