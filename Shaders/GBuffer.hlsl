#include "Common.hlsl"

struct VSInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float4 tangent : TANGENT;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
};

struct VSOutput {
  float4 position : SV_Position;
  float3 worldPos : TEXCOORD0;
  float3 normal : TEXCOORD1;
  float3 tangent : TEXCOORD2;
  float3 bitangent : TEXCOORD3;
  float2 uv : TEXCOORD4;
  float4 color : COLOR;
};

cbuffer RootConsts : register(b0) {
  float4x4 viewProj;
  float4x4 world;
};

cbuffer ObjectCB : register(b2) {
  float4x4 worldInvTranspose;
  float4 baseColorFactor;
  float4 materialParams;   // metallic, roughness, ao, alphaCutoff
  float4 emissiveFactor;   // rgb, dielectricF0
  uint4 textureIndices;    // bindless: albedo, normal, orm, emissive
  float4 materialExt;      // clearcoat, clearcoatRoughness, 0, 0
};

// Unbounded heap — root table bound once to bindless base (index 0).
Texture2D bindlessHeap[] : register(t0, space0);
SamplerState samp : register(s0);

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
  return o;
}

GBufferOut PSMain(VSOutput input) {
  Texture2D albedoMap = bindlessHeap[NonUniformResourceIndex(textureIndices.x)];
  Texture2D normalMap = bindlessHeap[NonUniformResourceIndex(textureIndices.y)];
  Texture2D ormMap = bindlessHeap[NonUniformResourceIndex(textureIndices.z)];
  Texture2D emissiveMap = bindlessHeap[NonUniformResourceIndex(textureIndices.w)];

  float4 albedo = albedoMap.Sample(samp, input.uv) * baseColorFactor * input.color;
  if (materialParams.w > 0.0 && albedo.a < materialParams.w) {
    discard;
  }

  float3 n = normalize(input.normal);
  float3 nm = normalMap.Sample(samp, input.uv).xyz * 2.0 - 1.0;
  if (length(nm - float3(0, 0, 1)) > 0.01) {
    float3x3 tbn = float3x3(normalize(input.tangent), normalize(input.bitangent), n);
    n = normalize(mul(nm, tbn));
  }

  float3 orm = ormMap.Sample(samp, input.uv).rgb;
  float ao = saturate(orm.r * materialParams.z);
  float roughness = saturate(max(orm.g * materialParams.y, 0.04));
  float metallic = saturate(orm.b * materialParams.x);
  float dielectricF0 = emissiveFactor.a;
  if (dielectricF0 < 1e-4) {
    dielectricF0 = 0.04;
  }
  float3 emissive = emissiveMap.Sample(samp, input.uv).rgb * emissiveFactor.rgb;
  float clearcoat = materialExt.x;

  GBufferOut o;
  o.albedo = float4(max(albedo.rgb, float3(0.02, 0.02, 0.02)), 1.0);
  o.normal = float4(n * 0.5 + 0.5, 1.0);
  o.orm = float4(ao, roughness, metallic, dielectricF0);
  o.emissive = float4(emissive, clearcoat);
  o.linearDepth = input.position.z;
  return o;
}
