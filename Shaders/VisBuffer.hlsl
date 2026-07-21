#include "Common.hlsl"

// Visibility ID pass: pack materialIndex in low 16 bits
cbuffer RootConsts : register(b0) {
  float4x4 viewProj;
  float4x4 world;
};

cbuffer ObjectCB : register(b2) {
  float4x4 worldInvTranspose;
  float4 baseColorFactor;
  float4 materialParams;
  float4 emissiveFactor;
  uint4 textureIndices; // x = materialId
};

struct VSInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float4 tangent : TANGENT;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
};

struct VSOutput {
  float4 position : SV_Position;
};

VSOutput VSMain(VSInput input) {
  VSOutput o;
  float4 wp = mul(world, float4(input.position, 1.0));
  o.position = mul(viewProj, wp);
  return o;
}

uint PSMain(VSOutput input, uint primID : SV_PrimitiveID) : SV_Target {
  uint matId = textureIndices.x & 0xFFFFu;
  uint tri = primID & 0xFFFFu;
  return (matId << 16) | tri;
}
