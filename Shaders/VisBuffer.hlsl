#include "Common.hlsl"

// VisBuffer ID + UV + normal for deferred material resolve.
struct MeshletGPU {
  float3 center;
  float radius;
  float3 coneAxis;
  float coneCutoff;
  uint indexOffset;
  uint indexCount;
  uint materialIndex;
  uint pad1;
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
  uint4 textureIndices; // x = global material base for this object
  float4 materialExt;
  uint4 textureIndices2;
  float4 fuzzColor;
};

StructuredBuffer<MeshletGPU> Meshlets : register(t0, space1);

struct VSInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float4 tangent : TANGENT;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
  uint instanceID : SV_InstanceID;
};

struct VSOutput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
  float3 normal : TEXCOORD1;
  nointerpolation uint matId : TEXCOORD2;
};

struct PSOut {
  uint id : SV_Target0;
  float2 uv : SV_Target1;
  float4 normal : SV_Target2;
  float depthColor : SV_Target3;
};

VSOutput VSMain(VSInput input) {
  VSOutput o;
  float4 wp = mul(world, float4(input.position, 1.0));
  o.position = mul(viewProj, wp);
  o.uv = input.uv;
  o.normal = normalize(mul((float3x3)worldInvTranspose, input.normal));
  uint localMat = 0;
  if (textureIndices.y != 0) {
    localMat = Meshlets[input.instanceID].materialIndex;
  }
  o.matId = (textureIndices.x + localMat) & 0xFFFFu;
  return o;
}

PSOut PSMain(VSOutput input, uint primID : SV_PrimitiveID) {
  PSOut o;
  o.id = (input.matId << 16) | (primID & 0xFFFFu);
  o.uv = input.uv;
  o.normal = float4(input.normal * 0.5 + 0.5, 0);
  o.depthColor = input.position.z;
  return o;
}
