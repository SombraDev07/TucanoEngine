#include "Common.hlsl"

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
  float4 materialExt;      // clearcoat, clearcoatRoughness, fuzz, detailScale
  uint4 textureIndices2;   // detailAlbedo, detailNormal, _, _
  float4 fuzzColor;        // rgb, _
  uint4 skinInfo;          // x = first matrix in the palette, y = bone count (0 = rigid)
};

// Frame-wide skinning palette: world x inverseBindPose per bone, packed for all skinned objects.
StructuredBuffer<float4x4> SkinMatrices : register(t0, space1);

// Unbounded heap — root table bound once to bindless base (index 0 = null SRV).
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
  // SampleLevel avoids anisotropic footprint blow-ups on dense meshes / extreme UVs.
  return bindlessHeap[NonUniformResourceIndex(safeIdx(idx))].SampleLevel(samp, uv, 0);
}

VSOutput VSMain(VSInput input) {
  VSOutput o;

  // Linear blend skinning. Weights summing to zero means this vertex belongs to a rigid mesh, and
  // the branch is uniform across the whole draw, so the cost on static geometry is negligible.
  float3 localPos = input.position;
  float3 localNrm = input.normal;
  float3 localTan = input.tangent.xyz;

  if (skinInfo.y > 0) {
    float weightSum = dot(input.boneWeights, float4(1, 1, 1, 1));
    if (weightSum > 1e-5) {
      float3 skinnedPos = 0;
      float3 skinnedNrm = 0;
      float3 skinnedTan = 0;

      [unroll]
      for (int i = 0; i < 4; ++i) {
        float w = input.boneWeights[i];
        if (w <= 0.0) continue;
        // Clamp so a malformed index can't read another object's slice of the palette.
        uint bone = min(input.boneIndices[i], skinInfo.y - 1);
        float4x4 m = SkinMatrices[skinInfo.x + bone];

        skinnedPos += w * mul(m, float4(localPos, 1.0)).xyz;
        // Normals and tangents are directions: no translation, hence the 3x3 part only.
        skinnedNrm += w * mul((float3x3)m, localNrm);
        skinnedTan += w * mul((float3x3)m, localTan);
      }

      localPos = skinnedPos / weightSum;
      localNrm = skinnedNrm;
      localTan = skinnedTan;
    }
  }

  float4 wp = mul(world, float4(localPos, 1.0));
  o.worldPos = wp.xyz;
  o.position = mul(viewProj, wp);
  float3 n = normalize(mul((float3x3)worldInvTranspose, localNrm));
  float3 t = normalize(mul((float3x3)world, localTan));
  o.normal = n;
  o.tangent = t;
  o.bitangent = normalize(cross(n, t) * input.tangent.w);
  o.uv = input.uv;
  o.color = input.color;
  return o;
}

GBufferOut PSMain(VSOutput input) {
  float4 albedo = sampleBindless(textureIndices.x, input.uv) * baseColorFactor * input.color;
  if (materialParams.w > 0.0 && albedo.a < materialParams.w) {
    discard;
  }

  float detailScale = materialExt.w;
  if (detailScale > 1e-4) {
    float2 duv = input.uv * detailScale;
    float4 detailA = sampleBindless(textureIndices2.x, duv);
    albedo.rgb = lerp(albedo.rgb, albedo.rgb * detailA.rgb * 2.0, saturate(detailScale * 0.05));
  }

  float3 n = normalize(input.normal);
  float3 nm = sampleBindless(textureIndices.y, input.uv).xyz * 2.0 - 1.0;
  if (length(nm - float3(0, 0, 1)) > 0.01) {
    float3x3 tbn = float3x3(normalize(input.tangent), normalize(input.bitangent), n);
    n = normalize(mul(nm, tbn));
  }
  if (detailScale > 1e-4) {
    float2 duv = input.uv * detailScale;
    float3 dn = sampleBindless(textureIndices2.y, duv).xyz * 2.0 - 1.0;
    if (length(dn - float3(0, 0, 1)) > 0.01) {
      float3x3 tbn = float3x3(normalize(input.tangent), normalize(input.bitangent), n);
      float3 nd = normalize(mul(dn, tbn));
      n = normalize(lerp(n, nd, 0.35));
    }
  }

  float3 orm = sampleBindless(textureIndices.z, input.uv).rgb;
  float ao = saturate(orm.r * materialParams.z);
  float roughness = saturate(max(orm.g * materialParams.y, 0.04));
  float metallic = saturate(orm.b * materialParams.x);
  float dielectricF0 = emissiveFactor.a;
  if (dielectricF0 < 1e-4) {
    dielectricF0 = 0.04;
  }
  float3 emissive = sampleBindless(textureIndices.w, input.uv).rgb * emissiveFactor.rgb;
  float clearcoat = materialExt.x;
  float clearcoatRoughness = saturate(max(materialExt.y, 0.05));
  float fuzz = saturate(materialExt.z);

  GBufferOut o;
  o.albedo = float4(max(albedo.rgb, float3(0.02, 0.02, 0.02)), clearcoatRoughness);
  o.normal = float4(n * 0.5 + 0.5, fuzz);
  o.orm = float4(ao, roughness, metallic, dielectricF0);
  o.emissive = float4(emissive, clearcoat);
  o.linearDepth = input.position.z;
  return o;
}
