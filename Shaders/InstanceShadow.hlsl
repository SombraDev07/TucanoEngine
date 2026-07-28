// Instanced shadow caster for vegetation / debris clouds.
// Same instance lookup as InstanceGBuffer; PS writes depth and alpha-tests foliage cards.

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

struct VSOut {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
};

cbuffer RootConsts : register(b0) {
  float4x4 lightViewProj;
  float4x4 _worldUnused;
};

cbuffer ObjectCB : register(b2) {
  float4x4 worldInvTranspose;
  float4 baseColorFactor;
  float4 materialParams; // metallic, roughness, ao, alphaCutoff
  float4 emissiveFactor;
  uint4 textureIndices;
  float4 materialExt;
  uint4 textureIndices2;
  float4 fuzzColor;
  uint4 skinInfo;
};

StructuredBuffer<InstanceGpu> Instances : register(t1, space1);
StructuredBuffer<uint> VisibleInstances : register(t2, space1);

Texture2D bindlessHeap[] : register(t0, space0);
SamplerState samp : register(s0);

uint safeIdx(uint i) {
  return (i < 8192u) ? i : 0u;
}

VSOut VSMain(VSInput input, uint iid : SV_InstanceID) {
  VSOut o;
  const uint idx = VisibleInstances[iid];
  const float4x4 world = Instances[idx].transform;
  float4 wp = mul(world, float4(input.position, 1.0));
  o.position = mul(lightViewProj, wp);
  o.uv = input.uv;
  o.color = input.color;
  return o;
}

float4 PSMain(VSOut input) : SV_Target {
  float alpha = baseColorFactor.a * input.color.a;
  if (textureIndices.x > 0) {
    alpha *= bindlessHeap[NonUniformResourceIndex(safeIdx(textureIndices.x))]
                 .SampleLevel(samp, input.uv, 0).a;
  } else {
    float tip = saturate(input.uv.y);
    float edge = saturate(1.0 - abs(input.uv.x - 0.5) * 1.8);
    alpha *= tip * tip * edge;
  }
  if (materialParams.w > 0.0 && alpha < materialParams.w) {
    discard;
  }
  return float4(input.position.z, 0, 0, 1);
}
