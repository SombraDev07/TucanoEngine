#include "Common.hlsl"

// Resolve VisBuffer → GBuffer MRTs using bindless materials.
struct DrawMaterialGPU {
  float4 baseColorFactor;
  float4 materialParams;
  float4 emissiveFactor;
  uint4 textureIndices;
  float4 materialExt;
  uint4 textureIndices2;
  float4 fuzzColor;
};

cbuffer Phase3CB : register(b1) {
  float4x4 invViewProj;
  float4 cameraPos;
  float4 screenSize;
  float4 params;
  float4 volumeOriginExtent;
  float4 iblParams;
  uint4 texIds0; // visId, visUv, visNormal, depth
  uint4 texIds1;
  uint4 texIds2;
};

Texture2D bindlessHeap[] : register(t0, space0);
SamplerState samp : register(s0);
StructuredBuffer<DrawMaterialGPU> Materials : register(t0, space1);

struct VSOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

VSOut VSMain(uint vid : SV_VertexID) {
  VSOut o;
  float2 p = float2((vid << 1) & 2, vid & 2);
  o.uv = p * 0.5;
  o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}

struct GBufferOut {
  float4 albedo : SV_Target0;
  float4 normal : SV_Target1;
  float4 orm : SV_Target2;
  float4 emissive : SV_Target3;
  float depthColor : SV_Target4;
};

GBufferOut PSResolve(VSOut input) {
  Texture2D visId = bindlessHeap[NonUniformResourceIndex(texIds0.x)];
  Texture2D visUv = bindlessHeap[NonUniformResourceIndex(texIds0.y)];
  Texture2D visNrm = bindlessHeap[NonUniformResourceIndex(texIds0.z)];
  Texture2D depthTex = bindlessHeap[NonUniformResourceIndex(texIds0.w)];

  uint2 pixel = uint2(input.uv * screenSize.xy);
  uint id = asuint(visId.Load(int3(pixel, 0)).r);
  GBufferOut o;
  if (id == 0) {
    o.albedo = 0;
    o.normal = float4(0.5, 0.5, 1, 0);
    o.orm = float4(1, 1, 0, 0.04);
    o.emissive = 0;
    o.depthColor = 0;
    return o;
  }

  uint matId = id >> 16;
  DrawMaterialGPU mat = Materials[matId];
  float2 uv = visUv.SampleLevel(samp, input.uv, 0).xy;
  float3 n = visNrm.SampleLevel(samp, input.uv, 0).xyz * 2.0 - 1.0;

  Texture2D albedoMap = bindlessHeap[NonUniformResourceIndex(mat.textureIndices.x)];
  Texture2D normalMap = bindlessHeap[NonUniformResourceIndex(mat.textureIndices.y)];
  Texture2D ormMap = bindlessHeap[NonUniformResourceIndex(mat.textureIndices.z)];
  Texture2D emissiveMap = bindlessHeap[NonUniformResourceIndex(mat.textureIndices.w)];

  float4 albedo = albedoMap.SampleLevel(samp, uv, 0) * mat.baseColorFactor;
  float3 nm = normalMap.SampleLevel(samp, uv, 0).xyz * 2.0 - 1.0;
  if (length(nm - float3(0, 0, 1)) > 0.01) {
    // Approximate TBN from screen derivatives of world normal — keep geo normal if flat
    n = normalize(n);
  }
  float3 orm = ormMap.SampleLevel(samp, uv, 0).rgb;
  float3 emissive = emissiveMap.SampleLevel(samp, uv, 0).rgb * mat.emissiveFactor.rgb;

  o.albedo = float4(albedo.rgb, 1);
  o.normal = float4(n * 0.5 + 0.5, 0);
  o.orm = float4(orm.r * mat.materialParams.z, orm.g * mat.materialParams.y, orm.b * mat.materialParams.x,
                 mat.emissiveFactor.w);
  o.emissive = float4(emissive, mat.materialExt.x);
  o.depthColor = depthTex.SampleLevel(samp, input.uv, 0).r;
  return o;
}
