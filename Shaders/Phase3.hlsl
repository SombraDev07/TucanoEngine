#include "Common.hlsl"

cbuffer Phase3CB : register(b1) {
  float4x4 invViewProj;
  float4x4 viewProj;
  float4 cameraPos;
  float4 screenSize; // w, h, 1/w, 1/h
  float4 params;     // intensity, thickness, maxSteps, giTier
  float4 volumeOriginExtent;
  float4 iblParams; // x=maxMip, y=exposure, z=probeY, w=temporalAlpha
  uint4 texIds0; // depth, normal, hdr, orm
  uint4 texIds1; // albedo, giHistory, ssrOrAtlas, brdf
  uint4 texIds2; // prefiltered, visId, worldSdf, worldSh
  float4 sdfC0;
  float4 sdfC1;
  float4 sdfC2;
  float4 sdfC3;
  float4x4 prevViewProj;
  uint4 texIds3; // prevDepth, hiz0, hiz2, probeAtlas
  float4 probePosIntensity[4];
  float4 probeBoxMin[4];
  float4 probeBoxMax[4];
};

Texture2D bindlessHeap[] : register(t0, space0);
SamplerState samp : register(s0);

Texture2D DepthTex() { return bindlessHeap[NonUniformResourceIndex(texIds0.x)]; }
Texture2D NormalTex() { return bindlessHeap[NonUniformResourceIndex(texIds0.y)]; }
Texture2D HdrTex() { return bindlessHeap[NonUniformResourceIndex(texIds0.z)]; }
Texture2D OrmTex() { return bindlessHeap[NonUniformResourceIndex(texIds0.w)]; }
Texture2D AlbedoTex() { return bindlessHeap[NonUniformResourceIndex(texIds1.x)]; }
Texture2D GiHistoryTex() { return bindlessHeap[NonUniformResourceIndex(texIds1.y)]; }
Texture2D SsrOrAtlasTex() { return bindlessHeap[NonUniformResourceIndex(texIds1.z)]; }
Texture2D BrdfTex() { return bindlessHeap[NonUniformResourceIndex(texIds1.w)]; }
Texture2D PrefTex() { return bindlessHeap[NonUniformResourceIndex(texIds2.x)]; }
Texture2D PrevDepthTex() { return bindlessHeap[NonUniformResourceIndex(texIds3.x)]; }
Texture2D HiZ0Tex() { return bindlessHeap[NonUniformResourceIndex(texIds3.y)]; }
Texture2D HiZ2Tex() { return bindlessHeap[NonUniformResourceIndex(texIds3.z)]; }
Texture2D ProbeAtlasTex() { return bindlessHeap[NonUniformResourceIndex(texIds3.w)]; }

float3 boxProjectDir(float3 worldPos, float3 reflDir, float3 probePos, float3 bmin, float3 bmax) {
  float3 t1 = (bmin - worldPos) / (reflDir + 1e-5);
  float3 t2 = (bmax - worldPos) / (reflDir + 1e-5);
  float3 tmax = max(t1, t2);
  float tFar = min(min(tmax.x, tmax.y), tmax.z);
  tFar = max(tFar, 0.05);
  float3 hit = worldPos + reflDir * tFar;
  return normalize(hit - probePos);
}

float3 sampleReflectionProbe(float3 worldPos, float3 reflDir, float roughness) {
  if (texIds3.w == 0) {
    return 0;
  }
  Texture2D atlas = ProbeAtlasTex();
  float3 best = 0;
  float wsum = 0;
  const float probeW = 0.25; // 4 probes side-by-side
  float maxMip = max(probeBoxMax[0].w, 1.0);
  float mip = roughness * maxMip;
  [unroll] for (int i = 0; i < 4; ++i) {
    float inten = probePosIntensity[i].w;
    if (inten < 1e-4) {
      continue;
    }
    float3 ppos = probePosIntensity[i].xyz;
    float3 bmin = probeBoxMin[i].xyz;
    float3 bmax = probeBoxMax[i].xyz;
    float3 local = (worldPos - bmin) / max(bmax - bmin, 1e-3);
    float inside = all(local > -0.05) && all(local < 1.05) ? 1.0 : 0.0;
    float distW = rcp(1.0 + distance(worldPos, ppos) * 0.07);
    float w = inten * distW * lerp(0.12, 1.0, inside);
    if (w < 1e-3) {
      continue;
    }
    float3 dir = boxProjectDir(worldPos, reflDir, ppos, bmin, bmax);
    float2 ll = dirToLatLong(dir);
    // Stay inside this probe's atlas strip (avoid bleed)
    float2 uv = float2(saturate(ll.x) * probeW + float(i) * probeW, saturate(ll.y));
    // Inset slightly from strip edges
    uv.x = float(i) * probeW + 0.002 + saturate(ll.x) * (probeW - 0.004);
    best += atlas.SampleLevel(samp, uv, mip).rgb * w;
    wsum += w;
  }
  return (wsum > 1e-4) ? (best / wsum) : 0;
}

float sampleHiZDepth(float2 uv, float stridePx) {
  if (texIds3.y == 0) {
    return DepthTex().SampleLevel(samp, uv, 0).r;
  }
  // Coarse pyramid when stride is large (real Hi-Z mips from previous frame)
  if (stridePx > 24.0 && texIds3.z != 0) {
    return HiZ2Tex().SampleLevel(samp, uv, 0).r;
  }
  if (stridePx > 10.0) {
    return HiZ0Tex().SampleLevel(samp, uv, 0).r;
  }
  return DepthTex().SampleLevel(samp, uv, 0).r;
}

float4 sdfCascade(int c) {
  if (c == 0) return sdfC0;
  if (c == 1) return sdfC1;
  if (c == 2) return sdfC2;
  return sdfC3;
}

float3 sampleWorldSh(float3 worldPos, float3 n) {
  if (texIds2.w == 0) {
    return 0;
  }
  Texture2D shTex = bindlessHeap[NonUniformResourceIndex(texIds2.w)];
  const float kSdfRes = 64.0;
  [unroll] for (int c = 0; c < 4; ++c) {
    float4 casc = sdfCascade(c);
    float3 local = (worldPos - casc.xyz) / max(casc.w, 1e-3);
    if (all(local > 0.02) && all(local < 0.98)) {
      float2 uv = float2((float(c) + local.x) * 0.25, local.z + local.y / kSdfRes);
      float4 sh = shTex.SampleLevel(samp, uv, 0);
      float3 L1 = sh.rgb;
      float L0 = sh.a;
      return max(L0 + max(dot(L1, normalize(n)), 0.0), 0.0).xxx * float3(1.0, 0.97, 0.92)
           + L1 * 0.15; // retain weak tinted directional bleed
    }
  }
  return 0;
}

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
  return PrefTex().SampleLevel(samp, dirToLatLong(r), mip).rgb;
}

float4 PSSSGI(VSOut input) : SV_Target {
  float depth = DepthTex().SampleLevel(samp, input.uv, 0).r;
  if (depth <= 1e-4) {
    return 0;
  }
  float3 n = normalize(NormalTex().SampleLevel(samp, input.uv, 0).xyz * 2.0 - 1.0);
  float3 worldPos = reconstructWorld(input.uv, depth);
  float3 albedo = AlbedoTex().SampleLevel(samp, input.uv, 0).rgb;

  static const float2 offsets[8] = {
      float2(1, 0),  float2(-1, 0), float2(0, 1),  float2(0, -1),
      float2(0.7, 0.7), float2(-0.7, 0.7), float2(0.7, -0.7), float2(-0.7, -0.7)};

  float3 accum = 0;
  float wsum = 0;
  float radius = 24.0 * screenSize.z;
  [unroll] for (int i = 0; i < 8; ++i) {
    float2 uv = input.uv + offsets[i] * radius * (1.0 + (i & 1));
    if (any(uv < 0.002) || any(uv > 0.998)) {
      continue;
    }
    float d = DepthTex().SampleLevel(samp, uv, 0).r;
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
    float thickness = params.y * 40.0 + 0.02;
    if (d > depth + thickness) {
      continue;
    }
    float3 col = HdrTex().SampleLevel(samp, uv, 0).rgb;
    col = min(col, 4.0);
    float w = ndotl / (1.0 + dist);
    accum += col * w;
    wsum += w;
  }

  float3 gi = (wsum > 1e-4) ? (accum / wsum) : 0;
  gi *= albedo * params.x * 0.35;
  gi = min(gi, 1.5);

  // Temporal accumulate with camera motion vectors (reproject via prevViewProj).
  float4 prevClip = mul(prevViewProj, float4(worldPos, 1.0));
  float3 prevNdc = prevClip.xyz / max(prevClip.w, 1e-5);
  float2 histUV = prevNdc.xy * float2(0.5, -0.5) + 0.5;
  float2 motion = input.uv - histUV;
  float motionPx = length(motion * screenSize.xy);

  float3 hist = 0;
  float valid = 0.0;
  if (all(histUV > 0.002) && all(histUV < 0.998) && texIds3.x != 0) {
    hist = GiHistoryTex().SampleLevel(samp, histUV, 0).rgb;
    float prevD = PrevDepthTex().SampleLevel(samp, histUV, 0).r;
    float depthErr = abs(prevNdc.z - prevD);
    float depthTol = max(params.y * 80.0, 0.002) + abs(prevNdc.z) * 0.02;
    valid = depthErr < depthTol ? 1.0 : 0.0;
  } else if (all(histUV > 0.002) && all(histUV < 0.998)) {
    hist = GiHistoryTex().SampleLevel(samp, histUV, 0).rgb;
    valid = 0.5; // no prev depth — soft accept
  }

  // Neighborhood clamp against current spatial result
  float3 nmin = gi * 0.7 - 0.01;
  float3 nmax = gi * 1.4 + 0.02;
  hist = clamp(hist, nmin, nmax);

  float alpha = saturate(iblParams.w);
  if (alpha < 1e-3) {
    alpha = 0.12;
  }
  // Faster converge on camera motion; full replace on depth reject
  alpha = lerp(alpha, saturate(0.08 + motionPx * 0.02), 0.65);
  alpha = lerp(1.0, alpha, valid);
  float histLum = dot(hist, float3(0.299, 0.587, 0.114));
  float curLum = dot(gi, float3(0.299, 0.587, 0.114));
  float reject = saturate(abs(histLum - curLum) * 2.0);
  alpha = lerp(alpha, 1.0, reject * (1.0 - valid * 0.5));
  gi = lerp(hist, gi, alpha);
  return float4(max(gi, 0), 1);
}

float4 PSSSR(VSOut input) : SV_Target {
  float depth = DepthTex().SampleLevel(samp, input.uv, 0).r;
  if (depth <= 1e-4) {
    return 0;
  }
  float roughness = OrmTex().SampleLevel(samp, input.uv, 0).g;
  if (roughness > 0.55) {
    return 0;
  }
  float3 n = normalize(NormalTex().SampleLevel(samp, input.uv, 0).xyz * 2.0 - 1.0);
  float3 worldPos = reconstructWorld(input.uv, depth);
  float3 v = normalize(cameraPos.xyz - worldPos);
  float3 r = reflect(-v, n);
  float3 albedo = AlbedoTex().SampleLevel(samp, input.uv, 0).rgb;
  float metallic = OrmTex().SampleLevel(samp, input.uv, 0).b;
  // Wet floors / puddles: push mirror quality (more steps, thinner reject)
  float puddleBias = saturate(n.y * 4.0 - 2.8) * saturate(1.0 - roughness * 3.5);
  float3 f0 = lerp(lerp(0.04, 0.02, puddleBias), albedo, metallic);
  float nDotV = max(dot(n, v), 0.0);
  float3 F = fresnelSchlickRoughness(nDotV, f0, roughness);

  float2 startUV = input.uv;
  float2 dirSS = float2(r.x, -r.y);
  float dirLen = length(dirSS);
  if (dirLen < 1e-4) {
    return 0;
  }
  dirSS /= dirLen;

  float thickness = max(params.y, 0.0015) * lerp(1.0, 0.55, puddleBias);
  float2 hitUV = startUV;
  int hitStep = -1;

  // Hierarchical march using real Hi-Z pyramid mips, then binary refine.
  const int coarseSteps = min(max((int)params.z, 16), 48) + (int)(puddleBias * 16.0);
  float stride = lerp(12.0, 4.0, saturate(1.0 - roughness * 2.0 + puddleBias * 0.5));
  float2 uv = startUV;
  float prevD = depth;
  [loop] for (int i = 1; i <= coarseSteps; ++i) {
    uv += dirSS * screenSize.zw * stride;
    if ((i & 3) == 0) {
      stride *= 1.35;
    }
    if (any(uv < 0.005) || any(uv > 0.995)) {
      break;
    }
    float d = sampleHiZDepth(uv, stride);
    if (d <= 1e-4) {
      prevD = d;
      continue;
    }
    if (d < depth - thickness && d < prevD - thickness * 0.25) {
      hitStep = i;
      hitUV = uv;
      break;
    }
    prevD = d;
  }

  float3 probeSpec = sampleReflectionProbe(worldPos, r, roughness);
  float3 iblSpec = samplePrefiltered(r, roughness);
  float3 envSpec = (any(probeSpec > 0.001)) ? lerp(iblSpec, probeSpec, 0.65) : iblSpec;

  if (hitStep < 0) {
    float3 miss = envSpec * F * params.x * 0.22;
    return float4(max(miss, 0), 0.12 * saturate(1.0 - roughness * 2.0));
  }

  float2 lo = hitUV - dirSS * screenSize.zw * max(stride * 0.5, 1.0);
  float2 hi = hitUV;
  [unroll] for (int b = 0; b < 6; ++b) {
    float2 mid = 0.5 * (lo + hi);
    float dMid = DepthTex().SampleLevel(samp, mid, 0).r;
    float dHi = DepthTex().SampleLevel(samp, hi, 0).r;
    if (dMid < dHi - thickness * 0.5) {
      hi = mid;
    } else {
      lo = mid;
    }
  }
  hitUV = hi;
  float edge = saturate(1.0 - max(abs(hitUV.x - 0.5), abs(hitUV.y - 0.5)) * 2.2);
  float3 hitCol = min(HdrTex().SampleLevel(samp, hitUV, 0).rgb, 8.0);
  float hit = edge * saturate(1.0 - roughness * 1.35) * saturate(1.0 - float(hitStep) / float(coarseSteps));

  float3 color = lerp(envSpec * F * 0.25, hitCol * F, saturate(hit)) * params.x * lerp(0.55, 0.95, puddleBias);
  return float4(max(color, 0), max(hit, lerp(0.08, 0.35, puddleBias)));
}

float4 PSCompose(VSOut input) : SV_Target {
  float3 base = HdrTex().SampleLevel(samp, input.uv, 0).rgb;
  float depth = DepthTex().SampleLevel(samp, input.uv, 0).r;
  if (depth <= 1e-4) {
    return float4(base, 1);
  }
  float3 gi = GiHistoryTex().SampleLevel(samp, input.uv, 0).rgb;
  float4 ssr = SsrOrAtlasTex().SampleLevel(samp, input.uv, 0);
  float tier = params.w;
  float3 outCol = base;
  if (tier >= 0.5) {
    outCol += min(gi, 1.0) * 0.65;
    // Soft WorldSDF SH ambient (all GI tiers)
    float3 worldPos = reconstructWorld(input.uv, depth);
    float3 albedo = AlbedoTex().SampleLevel(samp, input.uv, 0).rgb;
    outCol += sampleWorldSh(worldPos, normalize(NormalTex().SampleLevel(samp, input.uv, 0).xyz * 2.0 - 1.0)) *
              albedo * params.x * 0.25;
  }
  outCol += ssr.rgb * ssr.a;
  outCol = max(outCol, 0);
  return float4(outCol, 1);
}

float4 PSDDGI(VSOut input) : SV_Target {
  float depth = DepthTex().SampleLevel(samp, input.uv, 0).r;
  if (depth <= 1e-4) {
    return float4(GiHistoryTex().SampleLevel(samp, input.uv, 0).rgb, 1);
  }
  float3 n = normalize(NormalTex().SampleLevel(samp, input.uv, 0).xyz * 2.0 - 1.0);
  float3 worldPos = reconstructWorld(input.uv, depth);
  float3 origin = volumeOriginExtent.xyz;
  float extent = max(volumeOriginExtent.w, 1.0);
  float3 local = (worldPos - origin) / extent;
  float2 grid = local.xz * 8.0 - 0.5;
  int2 g0 = clamp(int2(floor(grid)), int2(0, 0), int2(6, 6));
  float2 f = saturate(grid - float2(g0));

  const float octa = 16.0;
  const float atlas = 128.0;
  float2 octaUV = encodeOctaYUp(n);

  float3 irr = 0;
  float wsum = 0;
  [unroll] for (int iz = 0; iz < 2; ++iz) {
    [unroll] for (int ix = 0; ix < 2; ++ix) {
      int2 gp = g0 + int2(ix, iz);
      float2 baseUV = (float2(gp) * octa + octaUV * octa) / atlas;
      float3 sampleIrr = SsrOrAtlasTex().SampleLevel(samp, baseUV, 0).rgb;
      float w = (ix ? f.x : (1.0 - f.x)) * (iz ? f.y : (1.0 - f.y));
      // Trilinear-ish in Y via sky/floor weight
      float probeY = iblParams.z;
      float dy = abs(worldPos.y - (origin.y + probeY * extent));
      w *= rcp(1.0 + dy * 0.35);
      irr += sampleIrr * w;
      wsum += w;
    }
  }
  irr = (wsum > 1e-4) ? (irr / wsum) : 0;
  float3 albedo = AlbedoTex().SampleLevel(samp, input.uv, 0).rgb;
  float3 ddgi = irr * albedo * params.x * 0.4;
  // WorldSDF SH1 fill for DDGI holes / soft ambient
  float3 shFill = sampleWorldSh(worldPos, n) * albedo * params.x * 0.4;
  float3 base = GiHistoryTex().SampleLevel(samp, input.uv, 0).rgb;
  return float4(min(max(base + ddgi + shFill, 0), 2.0), 1);
}

float4 PSVisAlbedo(VSOut input) : SV_Target {
  uint2 pixel = uint2(input.uv * screenSize.xy);
  uint id = (uint)bindlessHeap[NonUniformResourceIndex(texIds2.y)].Load(int3(pixel, 0)).r;
  float3 albedo = float3(0.55, 0.52, 0.48);
  if (id != 0) {
    float h = frac(float(id) * 0.6180339887);
    albedo = lerp(float3(0.2, 0.2, 0.2), float3(0.9, 0.85, 0.75), h);
  }
  return float4(albedo, 1);
}

float4 PSVisNormal(VSOut input) : SV_Target {
  float depth = DepthTex().Sample(samp, input.uv).r;
  if (depth <= 1e-4) {
    return float4(0.5, 0.5, 1, 1);
  }
  float3 p = reconstructWorld(input.uv, depth);
  float3 px = reconstructWorld(input.uv + float2(screenSize.z, 0),
                               DepthTex().Sample(samp, input.uv + float2(screenSize.z, 0)).r);
  float3 py = reconstructWorld(input.uv + float2(0, screenSize.w),
                               DepthTex().Sample(samp, input.uv + float2(0, screenSize.w)).r);
  float3 n = normalize(cross(px - p, py - p));
  return float4(n * 0.5 + 0.5, 1);
}

float4 PSVisORM(VSOut input) : SV_Target {
  return float4(1.0, 0.7, 0.0, 0.04);
}

float4 PSVisEmissive(VSOut input) : SV_Target {
  return float4(0, 0, 0, 0);
}
