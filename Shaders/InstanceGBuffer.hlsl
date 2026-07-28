// Instanced g-buffer draw for vegetation / debris clouds (WM-6 + Veg-P1).
// Supports alpha-test foliage cards and optional bindless albedo.

struct InstanceGpu {
  float4x4 transform;
  float3 center;
  float radius;
  uint materialId;
  uint lodMask;
  uint2 _pad;
};

struct VSInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float4 tangent : TANGENT;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
  uint4 boneIndices : BLENDINDICES;
  float4 boneWeights : BLENDWEIGHT;
};

struct VSOutput {
  float4 position : SV_Position;
  float3 worldPos : TEXCOORD0;
  float3 normal : TEXCOORD1;
  float2 uv : TEXCOORD4;
  float4 color : COLOR;
};

cbuffer RootConsts : register(b0) {
  float4x4 viewProj;
  float4x4 _worldUnused;
};

cbuffer ObjectCB : register(b2) {
  float4x4 worldInvTranspose;
  float4 baseColorFactor;
  float4 materialParams; // metallic, roughness, ao, alphaCutoff
  float4 emissiveFactor;
  uint4 textureIndices;  // x = albedo bindless index (0 = none)
  float4 materialExt;
  uint4 textureIndices2;
  float4 fuzzColor;
  uint4 skinInfo;
};

StructuredBuffer<InstanceGpu> Instances : register(t1, space1);
StructuredBuffer<uint> VisibleInstances : register(t2, space1);

Texture2D bindlessHeap[] : register(t0, space0);
SamplerState samp : register(s0);

struct GBufferOut {
  float4 albedo : SV_Target0;
  float4 normal : SV_Target1;
  float4 orm : SV_Target2;
  float4 emissive : SV_Target3;
  float linearDepth : SV_Target4;
};

uint safeIdx(uint i) {
  return (i < 8192u) ? i : 0u;
}

float4 sampleBindless(uint idx, float2 uv) {
  return bindlessHeap[NonUniformResourceIndex(safeIdx(idx))].SampleLevel(samp, uv, 0);
}

VSOutput VSMain(VSInput input, uint iid : SV_InstanceID) {
  VSOutput o;
  const uint idx = VisibleInstances[iid];
  const float4x4 world = Instances[idx].transform;

  const float4 wp = mul(world, float4(input.position, 1.0));
  o.worldPos = wp.xyz;
  o.position = mul(viewProj, wp);
  o.normal = normalize(mul((float3x3)world, input.normal));
  o.uv = input.uv;
  o.color = input.color;
  return o;
}

GBufferOut PSMain(VSOutput input, bool isFrontFace : SV_IsFrontFace) {
  float4 albedoSample = float4(1, 1, 1, 1);
  if (textureIndices.x > 0) {
    albedoSample = sampleBindless(textureIndices.x, input.uv);
  }

  float4 albedo = albedoSample * baseColorFactor * input.color;

  // Procedural card: tip soft-fade (UV.y=0 is tip) + slight horizontal edge soft.
  if (textureIndices.x == 0) {
    float tip = saturate(input.uv.y);
    float edge = saturate(1.0 - abs(input.uv.x - 0.5) * 1.8);
    albedo.a *= tip * tip * edge;
  }

  if (materialParams.w > 0.0 && albedo.a < materialParams.w) {
    discard;
  }

  float3 albedoRgb = max(albedo.rgb, float3(0.02, 0.02, 0.02));
  float roughness = saturate(max(materialParams.y, 0.04));
  float metallic = saturate(materialParams.x);
  float ao = saturate(materialParams.z);
  float dielectricF0 = emissiveFactor.a > 1e-4 ? emissiveFactor.a : 0.04;

  float3 n = normalize(input.normal);
  if (!isFrontFace) n = -n;

  GBufferOut o;
  o.albedo = float4(albedoRgb, 0.05);
  o.normal = float4(n * 0.5 + 0.5, 0.0);
  o.orm = float4(ao, roughness, metallic, dielectricF0);
  o.emissive = float4(emissiveFactor.rgb, 0.0);
  o.linearDepth = input.position.z;
  return o;
}
