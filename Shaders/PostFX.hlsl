#include "Common.hlsl"

struct VSOut {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

cbuffer PostCB : register(b1) {
  float4 params; // exposure, bloomStrength, aoBias, mode
  float4 texelSize;
};

Texture2D srcTex : register(t0);
Texture2D bloomTex : register(t1);
Texture2D depthTex : register(t2);
Texture2D normalTex : register(t3);
SamplerState samp : register(s0);

VSOut VSMain(uint id : SV_VertexID) {
  VSOut o;
  float2 uv = float2((id << 1) & 2, id & 2);
  o.uv = uv;
  o.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}

float4 PSTonemap(VSOut input) : SV_Target {
  float3 hdr = srcTex.Sample(samp, input.uv).rgb;
  float3 bloom = bloomTex.Sample(samp, input.uv).rgb;
  hdr += bloom * params.y;
  hdr *= params.x;
  float3 color = acesTonemap(hdr);
  color = pow(saturate(color), 1.0 / 2.2);
  return float4(color, 1.0);
}

float4 PSBloomDown(VSOut input) : SV_Target {
  float3 c = 0;
  float2 t = texelSize.xy;
  c += srcTex.Sample(samp, input.uv + float2(-t.x, -t.y)).rgb;
  c += srcTex.Sample(samp, input.uv + float2(t.x, -t.y)).rgb;
  c += srcTex.Sample(samp, input.uv + float2(-t.x, t.y)).rgb;
  c += srcTex.Sample(samp, input.uv + float2(t.x, t.y)).rgb;
  return float4(c * 0.25, 1.0);
}

float4 PSBloomUp(VSOut input) : SV_Target {
  float3 c = srcTex.Sample(samp, input.uv).rgb;
  float3 b = bloomTex.Sample(samp, input.uv).rgb;
  return float4(lerp(c, b, 0.6), 1.0);
}

float4 PSAO(VSOut input) : SV_Target {
  float depth = depthTex.SampleLevel(samp, input.uv, 0).r;
  if (depth <= 1e-4 || depth >= 0.999) {
    return 1.0;
  }
  float3 n = normalize(normalTex.SampleLevel(samp, input.uv, 0).xyz * 2.0 - 1.0);
  float occlusion = 0.0;
  const int samples = 8;
  // Smaller radius — large AO taps flicker badly in soft shadows
  float2 t = texelSize.xy * 1.25;
  static const float2 offsets[8] = {
      float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1),
      float2(0.7, 0.7), float2(-0.7, 0.7), float2(0.7, -0.7), float2(-0.7, -0.7)};
  [unroll] for (int i = 0; i < samples; ++i) {
    float2 uv = input.uv + offsets[i] * t;
    float sd = depthTex.SampleLevel(samp, uv, 0).r;
    if (sd <= 1e-4) {
      continue;
    }
    float3 sn = normalize(normalTex.SampleLevel(samp, uv, 0).xyz * 2.0 - 1.0);
    float diff = max(depth - sd, 0.0) * 12.0;
    float nd = max(dot(n, sn), 0.0);
    occlusion += saturate(diff) * (1.0 - nd);
  }
  float ao = 1.0 - saturate(occlusion / samples) * (params.z * 0.55);
  ao = lerp(1.0, ao, 0.85); // keep some fill in shadows
  return float4(ao, ao, ao, 1.0);
}
