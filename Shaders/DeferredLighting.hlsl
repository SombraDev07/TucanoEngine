#include "Common.hlsl"

struct VSOut {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

cbuffer FrameCB : register(b1) {
  float4x4 invViewProj;
  float4 cameraPos;
  float4 sunDirectionIntensity;
  float4 sunColor;
  float4 ambientColor;
  float4x4 lightViewProj[4];
  float4 cascadeSplits;
  float4 flags; // x=shadows, y=ibl, z=ao, w=esm
  float4 screenSize;
  float4 iblParams;     // maxMip, exposure, _, _
  float4 shadowParams;  // x=pcssLightSize, y=esmK, z=octaEnable, w=pcssEnable
  uint4 texIds0;        // albedo, normal, orm, emissive
  uint4 texIds1;        // depth, shadowCSM, brdf, irradiance
  uint4 texIds2;        // prefiltered, ao, octaAtlas, _
  uint4 texIds3;        // vsmPhysical, vsmPageTable, rtShadowMask, _
  float4 vsmMeta;       // x=enable, y=pagesPerAxis, z=physicalGrid, w=unused
};

cbuffer LightCB : register(b2) {
  uint lightCount;
  uint octaCount;
  uint spotCount;
  uint _pad0;
  float4 lightPosType[16];       // xyz, w=type (1 point, 2 spot)
  float4 lightColorIntensity[16];
  float4 lightRangeParams[16];   // range, innerCos, outerCos, shadowSlot+1 (0=none)
  float4 lightDirection[16];
  float4 octaPosRange[8];        // xyz pos, w range
  float4 octaUv[8];              // xy tile offset, z tile scale, w atlas texels
  float4 spotUv[4];              // xy tile offset, z tile scale, w atlas texels
  float4x4 spotViewProj[4];
};

Texture2D bindlessHeap[] : register(t0, space0);
SamplerState samp : register(s0);
SamplerState shadowSamp : register(s1);

VSOut VSMain(uint id : SV_VertexID) {
  VSOut o;
  float2 uv = float2((id << 1) & 2, id & 2);
  o.uv = uv;
  o.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}

float3 reconstructWorldPos(float2 uv, float depth) {
  float4 clip = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0);
  float4 world = mul(invViewProj, clip);
  return world.xyz / max(world.w, 1e-6);
}

float shadowCompare(float compare, float d) {
  if (flags.w > 0.5) {
    float k = max(shadowParams.y, 1.0);
    return saturate(exp(-k * (compare - d)));
  }
  return (compare <= d) ? 1.0 : 0.0;
}

float sampleShadowPCF(Texture2D shadowMap, float2 uv, float2 atlasLo, float2 atlasHi, float compare,
                      float2 texel, float radius) {
  float shadow = 0.0;
  float wsum = 0.0;
  int taps = radius > 1.5 ? 2 : 1;
  [loop] for (int x = -taps; x <= taps; ++x) {
    [loop] for (int y = -taps; y <= taps; ++y) {
      float2 suv = uv + float2(x, y) * texel * max(radius, 1.0);
      if (any(suv < atlasLo) || any(suv > atlasHi)) {
        continue;
      }
      float d = shadowMap.Sample(shadowSamp, suv).r;
      float w = 1.0;
      shadow += shadowCompare(compare, d) * w;
      wsum += w;
    }
  }
  return wsum > 0 ? shadow / wsum : 1.0;
}

float2 vogelDisk(int i, int n) {
  float r = sqrt((float(i) + 0.5) / float(n));
  float theta = float(i) * 2.399963229728653;
  return float2(cos(theta), sin(theta)) * r;
}

float2 encodeOcta(float3 n) {
  n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-6);
  float2 enc = n.xy;
  if (n.z < 0.0) {
    enc = (1.0 - abs(enc.yx)) * float2(enc.x >= 0.0 ? 1.0 : -1.0, enc.y >= 0.0 ? 1.0 : -1.0);
  }
  return enc * 0.5 + 0.5;
}

uint safeBindless(uint i) {
  // Slot 0 is the permanent null SRV; treat ~0 and OOB as null.
  return (i == 0xFFFFFFFFu || i >= 8192u) ? 0u : i;
}

float sampleVSM(float3 worldPos) {
  uint physId = safeBindless(texIds3.x);
  uint tableId = safeBindless(texIds3.y);
  if (physId == 0 || tableId == 0) {
    return 1.0;
  }
  Texture2D phys = bindlessHeap[NonUniformResourceIndex(physId)];
  Texture2D table = bindlessHeap[NonUniformResourceIndex(tableId)];
  float4 lightClip = mul(lightViewProj[0], float4(worldPos, 1.0));
  float3 ndc = lightClip.xyz / max(lightClip.w, 1e-6);
  float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
  if (any(uv < 0.001) || any(uv > 0.999)) {
    return 1.0;
  }
  float pages = max(vsmMeta.y, 1.0);
  float grid = max(vsmMeta.z, 1.0);
  float2 pageF = uv * pages;
  int2 page = int2(floor(pageF));
  uint2 tableSize;
  table.GetDimensions(tableSize.x, tableSize.y);
  if (tableSize.x == 0 || tableSize.y == 0) {
    return 1.0;
  }
  page = clamp(page, int2(0, 0), int2(tableSize) - 1);
  float physIdx = table.Load(int3(page, 0)).r;
  if (physIdx < 0.0) {
    return 1.0;
  }
  float2 local = frac(pageF);
  float2 physXY = float2(fmod(physIdx, grid), floor(physIdx / grid));
  float2 puv = (physXY + local) / grid;
  float compare = ndc.z - 0.0015;
  float d = phys.Sample(shadowSamp, puv).r;
  return shadowCompare(compare, d);
}

float sampleShadow(float3 worldPos, float viewDepth) {
  uint shadowId = safeBindless(texIds1.y);
  if (shadowId == 0) {
    return 1.0;
  }
  Texture2D shadowMap = bindlessHeap[NonUniformResourceIndex(shadowId)];
  int cascade = 0;
  if (viewDepth > cascadeSplits.x) cascade = 1;
  if (viewDepth > cascadeSplits.y) cascade = 2;
  if (viewDepth > cascadeSplits.z) cascade = 3;

  float4 lightClip = mul(lightViewProj[cascade], float4(worldPos, 1.0));
  float3 ndc = lightClip.xyz / max(lightClip.w, 1e-6);
  float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
  float2 atlasOffset = float2(cascade % 2, cascade / 2) * 0.5;
  uv = atlasOffset + uv * 0.5;
  float2 lo = atlasOffset + 0.0015;
  float2 hi = atlasOffset + 0.5 - 0.0015;
  if (any(uv < lo) || any(uv > hi)) {
    return 1.0;
  }

  float compare = ndc.z - (0.0012 + 0.0008 * cascade);
  float2 texel = 1.0 / 2048.0;
  float radius = 1.0;

  // PCSS: Vogel blocker search → penumbra → adaptive PCF
  if (shadowParams.w > 0.5) {
    float lightSize = max(shadowParams.x, 0.01);
    float search = lerp(2.0, 8.0, saturate(cascade / 3.0));
    float blocker = 0.0;
    float bcount = 0.0;
    const int kBlockers = 16;
    [unroll] for (int i = 0; i < kBlockers; ++i) {
      float2 vogel = vogelDisk(i, kBlockers) * texel * search;
      float2 suv = uv + vogel;
      if (any(suv < lo) || any(suv > hi)) {
        continue;
      }
      float d = shadowMap.Sample(shadowSamp, suv).r;
      if (d < compare) {
        blocker += d;
        bcount += 1.0;
      }
    }
    if (bcount > 0.5) {
      float avgBlocker = blocker / bcount;
      float penumbra = saturate((compare - avgBlocker) * lightSize / max(avgBlocker, 1e-4));
      radius = lerp(1.0, 4.5, penumbra);
    } else {
      return 1.0;
    }
  }

  float csm = sampleShadowPCF(shadowMap, uv, lo, hi, compare, texel, radius);
  // Near-field VSM override (cascade 0 footprint) when pages are mapped.
  if (vsmMeta.x > 0.5 && cascade == 0 && safeBindless(texIds3.x) != 0 && safeBindless(texIds3.y) != 0) {
    float v = sampleVSM(worldPos);
    // Blend toward VSM where pages exist (sampleVSM returns 1 if unmapped)
    csm = min(csm, v);
  }
  return csm;
}

// True octahedral point atlas (linear depth vs range).
float sampleOctaShadow(float3 worldPos, uint slot) {
  uint atlasId = safeBindless(texIds2.z);
  if (shadowParams.z < 0.5 || slot >= octaCount || atlasId == 0) {
    return 1.0;
  }
  Texture2D atlas = bindlessHeap[NonUniformResourceIndex(atlasId)];
  float3 L = octaPosRange[slot].xyz;
  float range = max(octaPosRange[slot].w, 0.5);
  float3 dir = worldPos - L;
  float dist = length(dir);
  if (dist >= range) {
    return 1.0;
  }
  dir /= max(dist, 1e-4);
  float2 local = encodeOcta(dir);
  if (any(local < 0.01) || any(local > 0.99)) {
    return 1.0;
  }

  float2 tileOff = octaUv[slot].xy;
  float tileScale = octaUv[slot].z;
  float2 uv = tileOff + local * tileScale;

  float atlasSize = max(octaUv[slot].w, 1.0);
  float2 texel = 1.0 / atlasSize;
  float compare = saturate(dist / range) - 0.0025;
  float2 lo = tileOff + 0.002;
  float2 hi = tileOff + tileScale - 0.002;
  return sampleShadowPCF(atlas, uv, lo, hi, compare, texel, 1.25);
}

float sampleSpotShadow(float3 worldPos, uint slot) {
  uint atlasId = safeBindless(texIds2.z);
  if (slot >= spotCount || atlasId == 0) {
    return 1.0;
  }
  Texture2D atlas = bindlessHeap[NonUniformResourceIndex(atlasId)];
  float4 clip = mul(spotViewProj[slot], float4(worldPos, 1.0));
  float3 ndc = clip.xyz / max(clip.w, 1e-6);
  float2 local = ndc.xy * float2(0.5, -0.5) + 0.5;
  if (any(local < 0.02) || any(local > 0.98) || ndc.z < 0.0 || ndc.z > 1.0) {
    return 1.0;
  }
  float2 tileOff = spotUv[slot].xy;
  float tileScale = spotUv[slot].z;
  float2 uv = tileOff + local * tileScale;
  float atlasSize = max(spotUv[slot].w, 1.0);
  float2 texel = 1.0 / atlasSize;
  float compare = ndc.z - 0.002;
  float2 lo = tileOff + 0.002;
  float2 hi = tileOff + tileScale - 0.002;
  return sampleShadowPCF(atlas, uv, lo, hi, compare, texel, 1.1);
}

float3 sampleIrradiance(float3 n) {
  uint id = safeBindless(texIds1.w);
  if (id == 0) {
    return 0;
  }
  Texture2D irradianceTex = bindlessHeap[NonUniformResourceIndex(id)];
  return irradianceTex.SampleLevel(samp, dirToLatLong(n), 0).rgb;
}

float3 samplePrefiltered(float3 r, float roughness) {
  uint id = safeBindless(texIds2.x);
  if (id == 0) {
    return 0;
  }
  Texture2D prefilteredTex = bindlessHeap[NonUniformResourceIndex(id)];
  float mip = roughness * iblParams.x;
  return prefilteredTex.SampleLevel(samp, dirToLatLong(r), mip).rgb;
}

float3 evaluateClearcoat(float3 lo, float clearcoat, float clearcoatRoughness, float3 n, float3 v, float3 l,
                         float3 radiance, float shadow) {
  if (clearcoat <= 1e-4) {
    return lo;
  }
  float3 h = normalize(v + l);
  float nDotL = max(dot(n, l), 0.0);
  float nDotV = max(dot(n, v), 0.0);
  float Fc = fresnelSchlick(max(dot(h, v), 0.0), 0.04).x;
  float NDF = distributionGGX(n, h, clearcoatRoughness);
  float G = geometrySmith(n, v, l, clearcoatRoughness);
  float coatSpec = (NDF * G * Fc) / max(4.0 * nDotV * nDotL, 1e-4);
  lo *= (1.0 - clearcoat * Fc);
  lo += (coatSpec * clearcoat) * radiance * nDotL * shadow;
  return lo;
}

float4 PSMain(VSOut input) : SV_Target {
  Texture2D depthTex = bindlessHeap[NonUniformResourceIndex(safeBindless(texIds1.x))];
  float depth = depthTex.SampleLevel(samp, input.uv, 0).r;
  if (depth <= 0.0001) {
    float3 env = ambientColor.rgb;
    if (flags.y > 0.5) {
      float2 ndc = input.uv * float2(2, -2) + float2(-1, 1);
      float4 clip = float4(ndc, 1.0, 1.0);
      float4 w = mul(invViewProj, clip);
      float3 dir = normalize(w.xyz / max(w.w, 1e-6) - cameraPos.xyz);
      env = samplePrefiltered(dir, 0.0) * iblParams.y;
    }
    return float4(env, 1.0);
  }

  Texture2D albedoTex = bindlessHeap[NonUniformResourceIndex(safeBindless(texIds0.x))];
  Texture2D normalTex = bindlessHeap[NonUniformResourceIndex(safeBindless(texIds0.y))];
  Texture2D ormTex = bindlessHeap[NonUniformResourceIndex(safeBindless(texIds0.z))];
  Texture2D emissiveTex = bindlessHeap[NonUniformResourceIndex(safeBindless(texIds0.w))];

  float4 albedoA = albedoTex.SampleLevel(samp, input.uv, 0);
  float3 albedo = albedoA.rgb;
  float clearcoatRoughness = saturate(max(albedoA.a, 0.05));
  float4 nSample4 = normalTex.SampleLevel(samp, input.uv, 0);
  float3 nSample = nSample4.xyz;
  float fuzz = saturate(nSample4.a);

  float3 n = normalize(nSample * 2.0 - 1.0);
  float4 orm = ormTex.SampleLevel(samp, input.uv, 0);
  float4 emissiveSample = emissiveTex.SampleLevel(samp, input.uv, 0);
  float3 emissive = emissiveSample.rgb;
  float clearcoat = emissiveSample.a;

  float3 worldPos = reconstructWorldPos(input.uv, depth);
  float3 v = normalize(cameraPos.xyz - worldPos);
  float ao = orm.r;
  float roughness = max(orm.g, 0.04);
  float metallic = orm.b;
  if (flags.z > 0.5) {
    uint aoId = safeBindless(texIds2.y);
    if (aoId != 0) {
      Texture2D aoTex = bindlessHeap[NonUniformResourceIndex(aoId)];
      ao *= aoTex.SampleLevel(samp, input.uv, 0).r;
    }
  }

  float dielectricF0 = max(orm.a, 0.02);
  float3 f0 = lerp(dielectricF0.xxx, albedo, metallic);
  float nDotV = max(dot(n, v), 0.0);
  float3 lo = 0;
  float3 fuzzCol = lerp(albedo, float3(1, 1, 1), 0.35);

  {
    float3 l = normalize(-sunDirectionIntensity.xyz);
    float3 radiance = sunColor.rgb * sunDirectionIntensity.w;
    float shadow = 1.0;
    if (flags.x > 0.5) {
      shadow = sampleShadow(worldPos, length(cameraPos.xyz - worldPos));
      uint rtId = safeBindless(texIds3.z);
      if (rtId != 0) {
        Texture2D rtMask = bindlessHeap[NonUniformResourceIndex(rtId)];
        shadow = min(shadow, rtMask.SampleLevel(samp, input.uv, 0).r);
      }
    }
    lo += evaluateDirectBRDF(albedo, f0, metallic, roughness, n, v, l, radiance) * shadow;
    lo = evaluateClearcoat(lo, clearcoat, clearcoatRoughness, n, v, l, radiance, shadow);
    lo += evaluateFuzz(fuzzCol, fuzz, n, v, l, radiance) * shadow;
  }

  uint lc = min(lightCount, 16u);
  [loop] for (uint i = 0; i < lc; ++i) {
    float3 lpos = lightPosType[i].xyz;
    float type = lightPosType[i].w;
    float3 toLight = lpos - worldPos;
    float dist = length(toLight);
    float3 l = toLight / max(dist, 1e-4);
    float range = lightRangeParams[i].x;
    float atten = saturate(1.0 - (dist / max(range, 1e-3)));
    atten *= atten;

    if (type > 1.5) {
      float3 spotDir = normalize(lightDirection[i].xyz);
      float cosTheta = dot(-l, spotDir);
      float inner = lightRangeParams[i].y;
      float outer = lightRangeParams[i].z;
      float spot = saturate((cosTheta - outer) / max(inner - outer, 1e-4));
      atten *= spot * spot;
    }

    float shadow = 1.0;
    float slotF = lightRangeParams[i].w;
    if (flags.x > 0.5 && slotF > 0.5) {
      if (type < 1.5) {
        shadow = sampleOctaShadow(worldPos, (uint)(slotF - 0.5));
      } else {
        shadow = sampleSpotShadow(worldPos, (uint)(slotF - 0.5));
      }
    }

    float3 radiance = lightColorIntensity[i].rgb * lightColorIntensity[i].a * atten;
    lo += evaluateDirectBRDF(albedo, f0, metallic, roughness, n, v, l, radiance) * shadow;
    lo = evaluateClearcoat(lo, clearcoat, clearcoatRoughness, n, v, l, radiance, shadow);
    lo += evaluateFuzz(fuzzCol, fuzz, n, v, l, radiance) * shadow;
  }

  float3 ambient = ambientColor.rgb * albedo * ao;
  if (flags.y > 0.5) {
    uint brdfId = safeBindless(texIds1.z);
    float2 dfg = float2(1, 0);
    if (brdfId != 0) {
      Texture2D brdfLUT = bindlessHeap[NonUniformResourceIndex(brdfId)];
      dfg = brdfLUT.SampleLevel(samp, float2(nDotV, roughness), 0).rg;
    }
    float3 F = fresnelSchlickRoughness(nDotV, f0, roughness);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 irradiance = sampleIrradiance(n);
    float3 diffuse = kD * irradiance * albedo;
    float3 r = reflect(-v, n);
    float3 prefiltered = samplePrefiltered(r, roughness);
    float3 specular = prefiltered * (F * dfg.x + dfg.y);
    specular *= getEnergyCompensation(f0, dfg);
    float sao = specularAO(ao, nDotV, roughness);
    ambient = (diffuse * ao + specular * sao) * iblParams.y;

    if (clearcoat > 1e-4) {
      float2 dfgC = dfg;
      if (brdfId != 0) {
        Texture2D brdfLUT = bindlessHeap[NonUniformResourceIndex(brdfId)];
        dfgC = brdfLUT.SampleLevel(samp, float2(nDotV, clearcoatRoughness), 0).rg;
      }
      float Fc = fresnelSchlickRoughness(nDotV, 0.04, clearcoatRoughness).x;
      float3 coat = samplePrefiltered(r, clearcoatRoughness) * (0.04 * dfgC.x + dfgC.y);
      ambient = ambient * (1.0 - clearcoat * Fc) + coat * clearcoat * sao;
    }
    if (fuzz > 1e-4) {
      ambient += fuzz * fuzzCol * irradiance * ao * 0.35 * iblParams.y;
    }
  }

  return float4(lo + ambient + emissive, 1.0);
}
