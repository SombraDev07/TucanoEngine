#include "Common.hlsl"

// Real DDGI-lite: probe grid with octahedral irradiance.
// Rays: WorldSDF sphere-trace (daGI2-class) with depth-buffer fallback + temporal blend.
cbuffer Phase3CB : register(b1) {
  float4x4 invViewProj;
  float4x4 viewProj;
  float4 cameraPos;
  float4 screenSize; // w, h, 1/w, 1/h
  float4 params;     // intensity, thickness, maxSteps, giTier
  float4 volumeOriginExtent; // xyz origin, w horizontal extent
  float4 iblParams;          // x=maxMip, y=exposure, z=probeY, w=temporalAlpha
  uint4 texIds0;             // hdr, depth, normal, atlasIn
  uint4 texIds1;
  uint4 texIds2;             // pref, visId, worldSdf, worldSh
  float4 sdfC0;
  float4 sdfC1;
  float4 sdfC2;
  float4 sdfC3;
  float4x4 prevViewProj;
  uint4 texIds3;
  float4 probePosIntensity[4];
  float4 probeBoxMin[4];
  float4 probeBoxMax[4];
};

Texture2D bindlessHeap[] : register(t0, space0);
SamplerState samp : register(s0);
RWTexture2D<float4> atlasOut : register(u0);

static const uint kProbes = 8;
static const uint kOcta = 16;
static const float kSdfRes = 64.0;

float3 worldToClip01(float3 world) {
  float4 clip = mul(viewProj, float4(world, 1.0));
  float3 ndc = clip.xyz / max(clip.w, 1e-5);
  return float3(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5, ndc.z);
}

float4 sdfCascade(int c) {
  if (c == 0) return sdfC0;
  if (c == 1) return sdfC1;
  if (c == 2) return sdfC2;
  return sdfC3;
}

float sampleWorldSdf(float3 worldPos) {
  if (texIds2.z == 0) {
    return 1e4;
  }
  Texture2D sdfTex = bindlessHeap[NonUniformResourceIndex(texIds2.z)];
  [unroll] for (int c = 0; c < 4; ++c) {
    float4 casc = sdfCascade(c);
    float3 local = (worldPos - casc.xyz) / max(casc.w, 1e-3);
    if (all(local > 0.02) && all(local < 0.98)) {
      float2 uv = float2((float(c) + local.x) * 0.25, local.z + local.y / kSdfRes);
      return sdfTex.SampleLevel(samp, uv, 0).r;
    }
  }
  return 1e4;
}

float3 sampleWorldSh(float3 worldPos, float3 n) {
  if (texIds2.w == 0) {
    return 0;
  }
  Texture2D shTex = bindlessHeap[NonUniformResourceIndex(texIds2.w)];
  [unroll] for (int c = 0; c < 4; ++c) {
    float4 casc = sdfCascade(c);
    float3 local = (worldPos - casc.xyz) / max(casc.w, 1e-3);
    if (all(local > 0.02) && all(local < 0.98)) {
      float2 uv = float2((float(c) + local.x) * 0.25, local.z + local.y / kSdfRes);
      float4 sh = shTex.SampleLevel(samp, uv, 0);
      float3 L1 = sh.rgb;
      float L0 = sh.a;
      return max(L0 + max(dot(L1, normalize(n)), 0.0), 0.0).xxx * float3(1.0, 0.97, 0.92) + L1 * 0.15;
    }
  }
  return 0;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
  Texture2D hdrTex = bindlessHeap[NonUniformResourceIndex(texIds0.x)];
  Texture2D depthTex = bindlessHeap[NonUniformResourceIndex(texIds0.y)];
  Texture2D atlasIn = bindlessHeap[NonUniformResourceIndex(texIds0.w)];

  uint2 dim;
  atlasOut.GetDimensions(dim.x, dim.y);
  if (id.x >= dim.x || id.y >= dim.y) {
    return;
  }

  const uint probesX = kProbes;
  const uint octa = kOcta;
  uint px = id.x / octa;
  uint pz = id.y / octa;
  uint lx = id.x % octa;
  uint lz = id.y % octa;

  float2 octaUV = (float2(lx, lz) + 0.5) / float(octa);
  float3 rayDir = decodeOctaYUp(octaUV);

  float3 origin = volumeOriginExtent.xyz;
  float extent = max(volumeOriginExtent.w, 1.0);
  float probeY = iblParams.z;
  float3 probePos = origin + float3((float(px) + 0.5) / probesX, probeY, (float(pz) + 0.5) / probesX) * extent;

  float3 probeClip = worldToClip01(probePos);
  if (all(probeClip.xy > 0.01) && all(probeClip.xy < 0.99)) {
    float d = depthTex.SampleLevel(samp, probeClip.xy, 0).r;
    if (d > 1e-4) {
      float4 cw = mul(invViewProj, float4(probeClip.xy * float2(2, -2) + float2(-1, 1), d, 1));
      float3 hit = cw.xyz / max(cw.w, 1e-6);
      probePos.y = max(probePos.y, hit.y + 0.35);
    }
  }

  float3 radiance = float3(0.04, 0.05, 0.07);
  float hitW = 0.0;
  float thickness = max(params.y * 20.0, 0.02);
  int maxSteps = clamp((int)params.z, 16, 48);

  // ---- WorldSDF sphere trace ----
  float3 pos = probePos + rayDir * 0.05;
  float traveled = 0.0;
  const float maxDist = extent * 1.25;
  [loop] for (int s = 0; s < maxSteps; ++s) {
    float sd = sampleWorldSdf(pos);
    if (sd < 0.08) {
      float3 c01 = worldToClip01(pos);
      Texture2D nrmTex = bindlessHeap[NonUniformResourceIndex(texIds0.z)];
      float3 n = float3(0, 1, 0);
      if (all(c01.xy > 0.002) && all(c01.xy < 0.998)) {
        n = normalize(nrmTex.SampleLevel(samp, c01.xy, 0).xyz * 2.0 - 1.0);
      }
      float3 sh = sampleWorldSh(pos, n);
      float3 col = sh;
      if (all(c01.xy > 0.002) && all(c01.xy < 0.998)) {
        float3 screenCol = hdrTex.SampleLevel(samp, c01.xy, 0).rgb;
        col = lerp(sh, min(screenCol, 6.0), 0.65);
      }
      float w = saturate(-dot(n, rayDir));
      radiance = col * max(w, 0.2);
      hitW = 1.0;
      break;
    }
    float step = clamp(sd, 0.05, extent * 0.08);
    pos += rayDir * step;
    traveled += step;
    if (traveled > maxDist) {
      break;
    }
  }

  // ---- Depth-buffer fallback if SDF miss ----
  if (hitW < 0.5) {
    float stepLen = extent / 48.0;
    pos = probePos + rayDir * (stepLen * 0.5);
    [loop] for (int i = 0; i < maxSteps; ++i) {
      pos += rayDir * stepLen;
      float3 c01 = worldToClip01(pos);
      if (any(c01.xy < 0.002) || any(c01.xy > 0.998) || c01.z < 0.0 || c01.z > 1.0) {
        break;
      }
      float sceneD = depthTex.SampleLevel(samp, c01.xy, 0).r;
      if (sceneD <= 1e-4) {
        continue;
      }
      if (c01.z > sceneD + thickness) {
        float3 col = hdrTex.SampleLevel(samp, c01.xy, 0).rgb;
        col = min(col, 6.0);
        Texture2D nrmTex = bindlessHeap[NonUniformResourceIndex(texIds0.z)];
        float3 n = normalize(nrmTex.SampleLevel(samp, c01.xy, 0).xyz * 2.0 - 1.0);
        float w = saturate(-dot(n, rayDir));
        radiance = col * max(w, 0.15);
        hitW = 1.0;
        break;
      }
      stepLen *= 1.045;
    }
  }

  if (hitW < 0.5) {
    radiance *= (0.55 + 0.45 * saturate(rayDir.y * 0.5 + 0.5));
    radiance += sampleWorldSh(probePos + rayDir * (extent * 0.35), rayDir) * 0.35;
  }

  float3 prev = atlasIn.Load(int3(id.xy, 0)).rgb;
  float alpha = saturate(iblParams.w);
  if (alpha < 1e-3) {
    alpha = 0.12;
  }
  float3 outC = lerp(prev, radiance * params.x, alpha);
  atlasOut[id.xy] = float4(max(outC, 0), 1);
}
