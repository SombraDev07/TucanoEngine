#include "Common.hlsl"

// True octahedral omnidirectional shadow map (single 2D tile per light).
// Vertex octa-projects direction from light; stores linear depth (0..1 by range).

struct VSInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float4 tangent : TANGENT;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
};

struct VSOut {
  float4 position : SV_Position;
  float depth01 : TEXCOORD0;
};

cbuffer RootConsts : register(b0) {
  float4x4 world;
  float4 lightPosRange; // xyz = light, w = range
  float4 _pad0;
  float4 _pad1;
  float4 _pad2;
};

float2 encodeOcta(float3 n) {
  n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-6);
  float2 enc = n.xy;
  if (n.z < 0.0) {
    float2 mirrored = (1.0 - abs(enc.yx)) * float2(enc.x >= 0.0 ? 1.0 : -1.0, enc.y >= 0.0 ? 1.0 : -1.0);
    enc = mirrored;
  }
  return enc * 0.5 + 0.5;
}

VSOut VSMain(VSInput input) {
  VSOut o;
  float4 wp = mul(world, float4(input.position, 1.0));
  float3 L = wp.xyz - lightPosRange.xyz;
  float dist = length(L);
  float3 dir = L / max(dist, 1e-5);
  float2 octa = encodeOcta(dir);
  // Clip XY from octa UV; Z = linear depth in [0,1]
  // Flip Y so clip+Y maps to texture UV.y=0 (D3D).
  float z = saturate(dist / max(lightPosRange.w, 1e-3));
  o.position = float4(octa.x * 2.0 - 1.0, 1.0 - octa.y * 2.0, z, 1.0);
  o.depth01 = z;
  return o;
}

float4 PSMain(VSOut input) : SV_Target {
  return float4(input.depth01, 0, 0, 1);
}
