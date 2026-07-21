// Contact shadows — short screen-space ray march for fine contact detail CSM misses.
// Fullscreen pass; darkens HDR where contact occlusion is found.

cbuffer ContactCB : register(b1) {
  float4x4 invViewProj;
  float4 cameraPos;
  float4 screenSize; // w,h,1/w,1/h
  float4 params;     // rayLengthMeters, steps, thickness, intensity
};

Texture2D depthTex : register(t0);
Texture2D hdrTex : register(t1);
SamplerState linearSamp : register(s0);

struct VSOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID) {
  VSOut o;
  o.uv = float2((id << 1) & 2, id & 2);
  o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}

float3 reconstructWorld(float2 uv, float depth) {
  float4 clip = float4(uv * 2 - 1, depth, 1);
  clip.y = -clip.y;
  float4 world = mul(invViewProj, clip);
  return world.xyz / world.w;
}

float4 PSMain(VSOut input) : SV_Target {
  float depth = depthTex.Sample(linearSamp, input.uv).r;
  float3 hdr = hdrTex.Sample(linearSamp, input.uv).rgb;
  if (depth >= 0.9999) {
    return float4(hdr, 1);
  }

  float3 worldPos = reconstructWorld(input.uv, depth);
  float3 viewDir = normalize(cameraPos.xyz - worldPos);
  // March toward light-ish up-bias for contact under objects (cheap approximation).
  float3 rayDir = normalize(viewDir + float3(0, 0.35, 0));

  float rayLen = params.x;
  int steps = (int)params.y;
  float thickness = params.z;
  float intensity = params.w;

  float occlusion = 0.0;
  for (int i = 1; i <= steps; ++i) {
    float t = (float(i) / float(steps)) * rayLen;
    float3 samplePos = worldPos + rayDir * t;
    // Project sample back to UV via depth comparison in screen space.
    float2 uv = input.uv + float2(rayDir.x, -rayDir.z) * (t * screenSize.z * 40.0);
    if (any(uv < 0) || any(uv > 1))
      break;
    float sampleDepth = depthTex.Sample(linearSamp, uv).r;
    float3 sampleWorld = reconstructWorld(uv, sampleDepth);
    float delta = length(sampleWorld - samplePos);
    if (delta < thickness && sampleDepth < depth) {
      occlusion = 1.0 - float(i) / float(steps);
      break;
    }
  }

  hdr *= saturate(1.0 - occlusion * intensity);
  return float4(hdr, 1);
}
