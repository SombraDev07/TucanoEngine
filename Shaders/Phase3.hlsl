#include "Common.hlsl"

cbuffer Phase3CB : register(b1) {
  float4x4 invViewProj;
  float4 cameraPos;
  float4 screenSize; // w, h, 1/w, 1/h
  float4 params;     // intensity, thickness, maxSteps, giTier
  float4 volumeOriginExtent;
  float4 iblParams;
};

Texture2D depthTex : register(t0);
Texture2D normalTex : register(t1);
Texture2D hdrTex : register(t2);
Texture2D ormTex : register(t3);
Texture2D albedoTex : register(t4);
Texture2D giHistory : register(t5); // unused for feedback — bind black/clear
Texture2D ssrOrAtlas : register(t6);
Texture2D brdfLUT : register(t7);
Texture2D prefilteredTex : register(t8);
Texture2D<uint> visIdTex : register(t9);
SamplerState samp : register(s0);

struct VSOut {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID) {
  VSOut o;
  float2 uv = float2((id << 1) & 2, id & 2);
  o.uv = uv;
  o.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}

float3 reconstructWorld(float2 uv, float depth) {
  float4 clip = float4(uv * float2(2, -2) + float2(-1, 1), depth, 1);
  float4 w = mul(invViewProj, clip);
  return w.xyz / max(w.w, 1e-6);
}

float3 samplePrefiltered(float3 r, float roughness) {
  float mip = roughness * iblParams.x;
  return prefilteredTex.SampleLevel(samp, dirToLatLong(r), mip).rgb;
}

// Stable screen-space GI: fixed-offset hemisphere taps (no temporal feedback).
float4 PSSSGI(VSOut input) : SV_Target {
  float depth = depthTex.SampleLevel(samp, input.uv, 0).r;
  if (depth <= 1e-4) {
    return 0;
  }
  float3 n = normalize(normalTex.SampleLevel(samp, input.uv, 0).xyz * 2.0 - 1.0);
  float3 worldPos = reconstructWorld(input.uv, depth);
  float3 albedo = albedoTex.SampleLevel(samp, input.uv, 0).rgb;

  // Fixed pattern in screen space — stable under camera motion (no random/temporal).
  static const float2 offsets[8] = {
      float2(1, 0),  float2(-1, 0), float2(0, 1),  float2(0, -1),
      float2(0.7, 0.7), float2(-0.7, 0.7), float2(0.7, -0.7), float2(-0.7, -0.7)};

  float3 accum = 0;
  float wsum = 0;
  float radius = 24.0 * screenSize.z; // ~24 px
  [unroll] for (int i = 0; i < 8; ++i) {
    float2 uv = input.uv + offsets[i] * radius * (1.0 + (i & 1));
    if (any(uv < 0.002) || any(uv > 0.998)) {
      continue;
    }
    float d = depthTex.SampleLevel(samp, uv, 0).r;
    if (d <= 1e-4) {
      continue;
    }
    float3 p = reconstructWorld(uv, d);
    float3 l = p - worldPos;
    float dist = length(l);
    if (dist < 1e-3 || dist > 4.0) {
      continue;
    }
    l /= dist;
    float ndotl = saturate(dot(n, l));
    if (ndotl <= 0.0) {
      continue;
    }
    // Only accept samples closer to camera than a thickness band (avoid sky bleed).
    float thickness = params.y * 40.0 + 0.02;
    if (d > depth + thickness) {
      continue;
    }
    float3 col = hdrTex.SampleLevel(samp, uv, 0).rgb;
    col = min(col, 4.0); // kill fireflies
    float w = ndotl / (1.0 + dist);
    accum += col * w;
    wsum += w;
  }

  float3 gi = (wsum > 1e-4) ? (accum / wsum) : 0;
  gi *= albedo * params.x * 0.35;
  gi = min(gi, 1.5);
  return float4(max(gi, 0), 1);
}

// SSR: only output hit radiance; misses return 0 (IBL already in deferred lighting).
float4 PSSSR(VSOut input) : SV_Target {
  float depth = depthTex.SampleLevel(samp, input.uv, 0).r;
  if (depth <= 1e-4) {
    return 0;
  }
  float roughness = ormTex.SampleLevel(samp, input.uv, 0).g;
  if (roughness > 0.45) {
    return 0;
  }
  float3 n = normalize(normalTex.SampleLevel(samp, input.uv, 0).xyz * 2.0 - 1.0);
  float3 worldPos = reconstructWorld(input.uv, depth);
  float3 v = normalize(cameraPos.xyz - worldPos);
  float3 r = reflect(-v, n);
  float3 albedo = albedoTex.SampleLevel(samp, input.uv, 0).rgb;
  float metallic = ormTex.SampleLevel(samp, input.uv, 0).b;
  float3 f0 = lerp(0.04, albedo, metallic);
  float nDotV = max(dot(n, v), 0.0);
  float3 F = fresnelSchlickRoughness(nDotV, f0, roughness);

  // March in screen space along projected reflection (stable step count).
  float2 startUV = input.uv;
  float3 endWorld = worldPos + r * 3.0;
  float4 endClip = mul(invViewProj, float4(endWorld, 1)); // wrong matrix — use NDC delta from r instead
  // Use fixed pixel steps along reflection projected onto screen via view-ish approximation:
  float2 dirSS = float2(r.x, -r.y);
  float dirLen = length(dirSS);
  if (dirLen < 1e-4) {
    return 0;
  }
  dirSS /= dirLen;

  float hit = 0;
  float3 hitCol = 0;
  const int steps = min((int)params.z, 32);
  float2 uv = startUV;
  float prevDepth = depth;
  [loop] for (int i = 1; i <= steps; ++i) {
    uv += dirSS * screenSize.zw * 8.0;
    if (any(uv < 0.01) || any(uv > 0.99)) {
      break;
    }
    float d = depthTex.SampleLevel(samp, uv, 0).r;
    if (d <= 1e-4) {
      continue;
    }
    // Crossing: scene depth becomes in front of marched ray approx
    if (d < prevDepth - params.y && d < depth - params.y) {
      float edge = saturate(1.0 - max(abs(uv.x - 0.5), abs(uv.y - 0.5)) * 2.0);
      hitCol = min(hdrTex.SampleLevel(samp, uv, 0).rgb, 6.0);
      hit = edge * saturate(1.0 - roughness * 1.5) * saturate(1.0 - float(i) / float(steps));
      break;
    }
    prevDepth = d;
  }

  if (hit < 0.05) {
    return 0;
  }
  float3 color = hitCol * F * params.x * 0.5;
  return float4(max(color, 0), hit);
}

float4 PSCompose(VSOut input) : SV_Target {
  float3 base = hdrTex.SampleLevel(samp, input.uv, 0).rgb;
  float depth = depthTex.SampleLevel(samp, input.uv, 0).r;
  if (depth <= 1e-4) {
    return float4(base, 1);
  }
  float3 gi = giHistory.SampleLevel(samp, input.uv, 0).rgb;
  float4 ssr = ssrOrAtlas.SampleLevel(samp, input.uv, 0);
  float tier = params.w;
  float3 outCol = base;
  if (tier >= 0.5) {
    outCol += min(gi, 1.0) * 0.65;
  }
  // Only add SSR where we actually hit
  outCol += ssr.rgb * ssr.a;
  outCol = max(outCol, 0);
  return float4(outCol, 1);
}

float4 PSDDGI(VSOut input) : SV_Target {
  float depth = depthTex.SampleLevel(samp, input.uv, 0).r;
  if (depth <= 1e-4) {
    return 0;
  }
  float3 n = normalize(normalTex.SampleLevel(samp, input.uv, 0).xyz * 2.0 - 1.0);
  float3 worldPos = reconstructWorld(input.uv, depth);
  float3 origin = volumeOriginExtent.xyz;
  float extent = max(volumeOriginExtent.w, 1.0);
  float3 uvw = saturate((worldPos - origin) / extent);
  float2 atlasUV = uvw.xz * 0.875 + 0.0625;
  float3 irr = ssrOrAtlas.SampleLevel(samp, atlasUV, 0).rgb;
  float3 albedo = albedoTex.SampleLevel(samp, input.uv, 0).rgb;
  float3 gi = irr * albedo * saturate(n.y * 0.5 + 0.5) * params.x * 0.25;
  return float4(min(max(gi, 0), 1.0), 1);
}

float4 PSVisAlbedo(VSOut input) : SV_Target {
  uint2 pixel = uint2(input.uv * screenSize.xy);
  uint id = visIdTex.Load(int3(pixel, 0)).r;
  float3 albedo = float3(0.55, 0.52, 0.48);
  if (id != 0) {
    float h = frac(float(id) * 0.6180339887);
    albedo = lerp(float3(0.2, 0.2, 0.2), float3(0.9, 0.85, 0.75), h);
  }
  return float4(albedo, 1);
}

float4 PSVisNormal(VSOut input) : SV_Target {
  float depth = depthTex.Sample(samp, input.uv).r;
  if (depth <= 1e-4) {
    return float4(0.5, 0.5, 1, 1);
  }
  float3 p = reconstructWorld(input.uv, depth);
  float3 px = reconstructWorld(input.uv + float2(screenSize.z, 0),
                               depthTex.Sample(samp, input.uv + float2(screenSize.z, 0)).r);
  float3 py = reconstructWorld(input.uv + float2(0, screenSize.w),
                               depthTex.Sample(samp, input.uv + float2(0, screenSize.w)).r);
  float3 n = normalize(cross(px - p, py - p));
  return float4(n * 0.5 + 0.5, 1);
}

float4 PSVisORM(VSOut input) : SV_Target {
  return float4(1.0, 0.7, 0.0, 0.04);
}

float4 PSVisEmissive(VSOut input) : SV_Target {
  return float4(0, 0, 0, 0);
}
