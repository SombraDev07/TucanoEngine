#include "Common.hlsl"

struct VSOut {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

cbuffer PostCB : register(b1) {
  float4 params; // x=exposureOverride (or 0=use tex), y=bloomStrength, z=aoRadius/power, w=mode
  float4 texelSize;
  uint4 texIds; // src, bloom/aux, depth/exposure, normal
  float4 gtaoParams; // x=radius, y=thickness, z=power, w=frames
};

Texture2D bindlessHeap[] : register(t0, space0);
SamplerState samp : register(s0);

VSOut VSMain(uint id : SV_VertexID) {
  VSOut o;
  float2 uv = float2((id << 1) & 2, id & 2);
  o.uv = uv;
  o.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}

float4 PSTonemap(VSOut input) : SV_Target {
  Texture2D srcTex = bindlessHeap[NonUniformResourceIndex(texIds.x)];
  Texture2D bloomTex = bindlessHeap[NonUniformResourceIndex(texIds.y)];
  float3 hdr = srcTex.Sample(samp, input.uv).rgb;
  float3 bloom = bloomTex.Sample(samp, input.uv).rgb;
  hdr += bloom * params.y;

  float exposure = params.x;
  if (texIds.z != 0u && texIds.z != 0xFFFFFFFFu) {
    Texture2D expTex = bindlessHeap[NonUniformResourceIndex(texIds.z)];
    exposure = max(expTex.Load(int3(0, 0, 0)).r, 1e-4);
  }
  hdr *= exposure;
  float3 color = acesTonemap(hdr);
  color = pow(saturate(color), 1.0 / 2.2);
  return float4(color, 1.0);
}

float4 PSBloomDown(VSOut input) : SV_Target {
  Texture2D srcTex = bindlessHeap[NonUniformResourceIndex(texIds.x)];
  float2 t = texelSize.xy;
  // Karis average (stable bright bloom)
  float3 c0 = srcTex.Sample(samp, input.uv + float2(-t.x, -t.y)).rgb;
  float3 c1 = srcTex.Sample(samp, input.uv + float2(t.x, -t.y)).rgb;
  float3 c2 = srcTex.Sample(samp, input.uv + float2(-t.x, t.y)).rgb;
  float3 c3 = srcTex.Sample(samp, input.uv + float2(t.x, t.y)).rgb;
  float3 c = (c0 + c1 + c2 + c3) * 0.25;
  float luma = max(dot(c, float3(0.2126, 0.7152, 0.0722)), 1e-4);
  float knee = 1.0;
  float soft = saturate((luma - 1.0) / (4.0 * knee + 1e-4));
  soft = soft * soft * (3.0 - 2.0 * soft);
  float contrib = max(luma - 0.8, 0.0) / (luma + 1e-4);
  c *= lerp(contrib, 1.0, soft * 0.35);
  return float4(c, 1.0);
}

float4 PSBloomUp(VSOut input) : SV_Target {
  Texture2D lowTex = bindlessHeap[NonUniformResourceIndex(texIds.x)];
  Texture2D highTex = bindlessHeap[NonUniformResourceIndex(texIds.y)];
  float2 t = texelSize.xy;
  // 9-tap tent upsample from coarser mip
  float3 up = 0;
  up += lowTex.Sample(samp, input.uv + float2(-t.x, -t.y)).rgb;
  up += lowTex.Sample(samp, input.uv + float2(0, -t.y)).rgb * 2;
  up += lowTex.Sample(samp, input.uv + float2(t.x, -t.y)).rgb;
  up += lowTex.Sample(samp, input.uv + float2(-t.x, 0)).rgb * 2;
  up += lowTex.Sample(samp, input.uv).rgb * 4;
  up += lowTex.Sample(samp, input.uv + float2(t.x, 0)).rgb * 2;
  up += lowTex.Sample(samp, input.uv + float2(-t.x, t.y)).rgb;
  up += lowTex.Sample(samp, input.uv + float2(0, t.y)).rgb * 2;
  up += lowTex.Sample(samp, input.uv + float2(t.x, t.y)).rgb;
  up *= (1.0 / 16.0);
  float3 hi = highTex.Sample(samp, input.uv).rgb;
  return float4(hi + up, 1.0);
}

// ---- GTAO (horizon-based, view-space) ----
float3 reconstructViewPos(float2 uv, float depth, float4x4 invViewProj) {
  float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
  clip.y = -clip.y;
  float4 world = mul(invViewProj, clip);
  world.xyz /= world.w;
  return world.xyz; // world ≈ view-offset for AO differences
}

float4 PSGTAO(VSOut input) : SV_Target {
  uint depthId = (texIds.z == 0xFFFFFFFFu || texIds.z >= 8192u) ? 0u : texIds.z;
  uint normalId = (texIds.w == 0xFFFFFFFFu || texIds.w >= 8192u) ? 0u : texIds.w;
  if (depthId == 0) {
    return 1.0;
  }
  Texture2D depthTex = bindlessHeap[NonUniformResourceIndex(depthId)];
  Texture2D normalTex = bindlessHeap[NonUniformResourceIndex(normalId)];
  float depth = depthTex.SampleLevel(samp, input.uv, 0).r;
  if (depth <= 1e-4 || depth >= 0.9995) {
    return 1.0;
  }

  float3 n = normalize(normalTex.SampleLevel(samp, input.uv, 0).xyz * 2.0 - 1.0);
  // params from gtaoParams; fallback radius via params.z
  float radius = max(gtaoParams.x, 0.35);
  float thickness = max(gtaoParams.y, 0.15);
  float power = max(gtaoParams.z, 1.25);
  float2 pixel = texelSize.xy;

  // Spatial noise rotation (Interleaved Gradient Noise)
  float noise = frac(52.9829189 * frac(dot(input.position.xy, float2(0.06711056, 0.00583715))));
  float rot = noise * 6.2831853;

  const int dirs = 4;
  const int steps = 4;
  float occlusion = 0.0;

  [unroll] for (int d = 0; d < dirs; ++d) {
    float ang = rot + (d / (float)dirs) * 6.2831853;
    float2 dir = float2(cos(ang), sin(ang));
    float hNeg = -1.0;
    float hPos = -1.0;

    [unroll] for (int s = 1; s <= steps; ++s) {
      float t = (s + noise) / (float)steps;
      float2 offset = dir * (radius * t) * pixel * (80.0 / max(depth * 200.0, 1.0));

      float2 uvP = saturate(input.uv + offset);
      float2 uvN = saturate(input.uv - offset);
      float dP = depthTex.SampleLevel(samp, uvP, 0).r;
      float dN = depthTex.SampleLevel(samp, uvN, 0).r;

      float3 nP = normalize(normalTex.SampleLevel(samp, uvP, 0).xyz * 2.0 - 1.0);
      float3 nN = normalize(normalTex.SampleLevel(samp, uvN, 0).xyz * 2.0 - 1.0);

      // Depth horizon angles vs center
      float dzP = (depth - dP) * 40.0;
      float dzN = (depth - dN) * 40.0;
      float distP = length(float2(offset * float2(1.0 / pixel.x, 1.0 / pixel.y))) * 0.01 + 1e-4;
      float distN = distP;
      float cosP = saturate(dzP / distP);
      float cosN = saturate(dzN / distN);

      // Thickness attenuates distant samples behind surfaces
      float attP = saturate(1.0 - abs(dzP) / (thickness * 8.0));
      float attN = saturate(1.0 - abs(dzN) / (thickness * 8.0));
      float nAttP = saturate(dot(n, nP) * 0.5 + 0.5);
      float nAttN = saturate(dot(n, nN) * 0.5 + 0.5);

      hPos = max(hPos, cosP * attP * nAttP);
      hNeg = max(hNeg, cosN * attN * nAttN);
    }

    // Integrate horizon (approx GTAO)
    float aoSlice = 1.0 - 0.5 * (saturate(hPos) + saturate(hNeg));
    occlusion += aoSlice;
  }

  float ao = saturate(occlusion / (float)dirs);
  ao = pow(ao, power);
  ao = lerp(1.0, ao, saturate(params.z));
  return float4(ao, ao, ao, 1.0);
}

// Bilateral / edge-aware blur
float4 PSGTAOBlur(VSOut input) : SV_Target {
  uint aoId = (texIds.x == 0xFFFFFFFFu || texIds.x >= 8192u) ? 0u : texIds.x;
  uint depthId = (texIds.z == 0xFFFFFFFFu || texIds.z >= 8192u) ? 0u : texIds.z;
  uint normalId = (texIds.w == 0xFFFFFFFFu || texIds.w >= 8192u) ? 0u : texIds.w;
  Texture2D aoTex = bindlessHeap[NonUniformResourceIndex(aoId)];
  Texture2D depthTex = bindlessHeap[NonUniformResourceIndex(depthId)];
  Texture2D normalTex = bindlessHeap[NonUniformResourceIndex(normalId)];
  float centerD = depthTex.SampleLevel(samp, input.uv, 0).r;
  float3 centerN = normalize(normalTex.SampleLevel(samp, input.uv, 0).xyz * 2.0 - 1.0);
  float2 t = texelSize.xy * (params.w > 0.5 ? float2(1, 0) : float2(0, 1)); // direction

  float aoSum = 0.0;
  float wSum = 0.0;
  [unroll] for (int i = -4; i <= 4; ++i) {
    float2 uv = input.uv + t * (float)i;
    float ao = aoTex.SampleLevel(samp, uv, 0).r;
    float d = depthTex.SampleLevel(samp, uv, 0).r;
    float3 n = normalize(normalTex.SampleLevel(samp, uv, 0).xyz * 2.0 - 1.0);
    float wSpatial = exp(-0.35 * abs((float)i));
    float wDepth = exp(-abs(centerD - d) * 80.0);
    float wNormal = pow(saturate(dot(centerN, n)), 8.0);
    float w = wSpatial * wDepth * wNormal;
    aoSum += ao * w;
    wSum += w;
  }
  float outAo = aoSum / max(wSum, 1e-4);
  return float4(outAo, outAo, outAo, 1.0);
}

// Legacy entry name kept for CMake / older PSOs
float4 PSAO(VSOut input) : SV_Target {
  return PSGTAO(input);
}
