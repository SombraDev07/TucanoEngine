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
  float4 color : COLOR;
  float2 uv : TEXCOORD0;
};

#ifdef TUCANO_SPIRV
struct PushConstants {
  float4 tint;
  uint texIndex;
};
[[vk::push_constant]] PushConstants g_push;
#define tint g_push.tint
#define texIndex g_push.texIndex
[[vk::binding(0, 0)]] Texture2D bindlessHeap[] : register(t0, space0);
[[vk::binding(0, 1)]] SamplerState samp : register(s0);
#else
cbuffer Constants : register(b0) {
  float4 tint;
  uint texIndex;
};
Texture2D bindlessHeap[] : register(t0, space0);
SamplerState samp : register(s0);
#endif

VSOutput VSMain(VSInput input) {
  VSOutput o;
  o.position = float4(input.position, 1.0);
  o.color = input.color * tint;
  o.uv = input.uv;
  return o;
}

float4 PSMain(VSOutput input) : SV_Target {
  float4 tex = bindlessHeap[texIndex].Sample(samp, input.uv);
  return lerp(input.color, tex, 0.85);
}
