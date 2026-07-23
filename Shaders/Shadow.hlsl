struct VSInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float4 tangent : TANGENT;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
  uint4 boneIndices : BLENDINDICES;
  float4 boneWeights : BLENDWEIGHT;
};

struct VSOut {
  float4 position : SV_Position;
};

cbuffer RootConsts : register(b0) {
  float4x4 lightViewProj;
  float4x4 world;
};

// Same layout as ObjectCB in GBuffer.hlsl — only skinInfo is read here, but the preceding fields
// must be declared so the offsets line up.
cbuffer ObjectCB : register(b2) {
  float4x4 worldInvTranspose;
  float4 baseColorFactor;
  float4 materialParams;
  float4 emissiveFactor;
  uint4 textureIndices;
  float4 materialExt;
  uint4 textureIndices2;
  float4 fuzzColor;
  uint4 skinInfo;          // x = first matrix in the palette, y = bone count (0 = rigid)
};

StructuredBuffer<float4x4> SkinMatrices : register(t0, space1);

VSOut VSMain(VSInput input) {
  VSOut o;

  // Skin before projecting into light space, otherwise an animated character casts the shadow of
  // its bind pose — the mesh bends on screen while its shadow stays rigid.
  float3 localPos = input.position;
  if (skinInfo.y > 0) {
    float weightSum = dot(input.boneWeights, float4(1, 1, 1, 1));
    if (weightSum > 1e-5) {
      float3 skinned = 0;
      [unroll]
      for (int i = 0; i < 4; ++i) {
        float w = input.boneWeights[i];
        if (w <= 0.0) continue;
        uint bone = min(input.boneIndices[i], skinInfo.y - 1);
        skinned += w * mul(SkinMatrices[skinInfo.x + bone], float4(localPos, 1.0)).xyz;
      }
      localPos = skinned / weightSum;
    }
  }

  float4 wp = mul(world, float4(localPos, 1.0));
  o.position = mul(lightViewProj, wp);
  return o;
}

float4 PSMain(VSOut input) : SV_Target {
  return float4(input.position.z, 0, 0, 1);
}
