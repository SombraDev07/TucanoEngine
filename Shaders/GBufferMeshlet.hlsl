#include "Common.hlsl"

// GPU-driven GBuffer: SV_InstanceID == StartInstanceLocation == meshlet slot.
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

struct DrawMaterialGPU {
  float4 baseColorFactor;
  float4 materialParams;
  float4 emissiveFactor;
  uint4 textureIndices;
  float4 materialExt;
  uint4 textureIndices2;
  float4 fuzzColor;
};

cbuffer RootConsts : register(b0) {
  float4x4 viewProj;
  float4x4 world;
};

cbuffer ObjectCB : register(b2) {
  float4x4 worldInvTranspose;
  float4 _u0, _u1, _u2;
  uint4 _u3;
  float4 _u4;
  uint4 _u5;
  float4 _u6;
};

Texture2D bindlessHeap[] : register(t0, space0);
SamplerState samp : register(s0);
StructuredBuffer<DrawMaterialGPU> Materials : register(t0, space1);
StructuredBuffer<MeshletGPU> Meshlets : register(t1, space1);

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
  float3 worldPos : TEXCOORD0;
  float3 normal : TEXCOORD1;
  float3 tangent : TEXCOORD2;
  float3 bitangent : TEXCOORD3;
  float2 uv : TEXCOORD4;
  float4 color : COLOR;
  nointerpolation uint matIndex : TEXCOORD5;
};

struct GBufferOut {
  float4 albedo : SV_Target0;
  float4 normal : SV_Target1;
  float4 orm : SV_Target2;
  float4 emissive : SV_Target3;
  float linearDepth : SV_Target4;
};

VSOutput VSMain(VSInput input) {
  VSOutput o;
  float4 wp = mul(world, float4(input.position, 1.0));
  o.worldPos = wp.xyz;
  o.position = mul(viewProj, wp);
  float3 n = normalize(mul((float3x3)worldInvTranspose, input.normal));
  float3 t = normalize(mul((float3x3)world, input.tangent.xyz));
  o.normal = n;
  o.tangent = t;
  o.bitangent = normalize(cross(n, t) * input.tangent.w);
  o.uv = input.uv;
  o.color = input.color;
  o.matIndex = Meshlets[input.instanceID].materialIndex;
  return o;
}

GBufferOut PSMain(VSOutput input) {
  DrawMaterialGPU mat = Materials[input.matIndex];
  Texture2D albedoMap = bindlessHeap[NonUniformResourceIndex(mat.textureIndices.x)];
  Texture2D normalMap = bindlessHeap[NonUniformResourceIndex(mat.textureIndices.y)];
  Texture2D ormMap = bindlessHeap[NonUniformResourceIndex(mat.textureIndices.z)];
  Texture2D emissiveMap = bindlessHeap[NonUniformResourceIndex(mat.textureIndices.w)];

  float4 albedo = albedoMap.Sample(samp, input.uv) * mat.baseColorFactor * input.color;
  if (mat.materialParams.w > 0.0 && albedo.a < mat.materialParams.w) {
    discard;
  }

  float3 n = normalize(input.normal);
  float3 nm = normalMap.Sample(samp, input.uv).xyz * 2.0 - 1.0;
  if (length(nm - float3(0, 0, 1)) > 0.01) {
    float3x3 tbn = float3x3(normalize(input.tangent), normalize(input.bitangent), n);
    n = normalize(mul(nm, tbn));
  }

  float3 orm = ormMap.Sample(samp, input.uv).rgb;
  float metallic = orm.b * mat.materialParams.x;
  float roughness = orm.g * mat.materialParams.y;
  float ao = orm.r * mat.materialParams.z;
  float3 emissive = emissiveMap.Sample(samp, input.uv).rgb * mat.emissiveFactor.rgb;

  GBufferOut o;
  o.albedo = float4(albedo.rgb, 1);
  o.normal = float4(n * 0.5 + 0.5, 0);
  o.orm = float4(ao, roughness, metallic, mat.emissiveFactor.w);
  o.emissive = float4(emissive, mat.materialExt.x);
  o.linearDepth = input.position.z;
  return o;
}
