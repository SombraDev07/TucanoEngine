#include "Common.hlsl"
#include "Atmosphere.hlsl"

// AAA volumetric clouds (Nubis / Schneider + Hillaire-style lighting):
//  - Precomputed 3D Perlin-Worley base (128^3) + Worley detail (32^3) noise, tileable.
//  - Spherical cloud shell around the planet (horizon curvature).
//  - Two-stage density (cheap base -> detail erosion), height gradients by cloud type.
//  - Energy-conserving integration (Frostbite), 3-octave multi-scattering,
//    dual-lobe HG phase, Beer-powder, animated IGN dither + temporal reprojection.
// Pass layout kept from before: half-res march -> temporal -> weather map -> composite.

struct VSOut {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

cbuffer CloudCB : register(b1) {
  float4x4 invViewProj;
  float4x4 viewProj;
  float4x4 prevViewProj;
  float4 cameraPos;        // xyz cam, w = frame index (dither animation)
  float4 screenSize;       // xy full px, zw 1/full
  float4 sunDirIntensity;  // xyz = sun→scene, w = intensity
  float4 sunColor;
  float4 cloudShape;       // coverage, density, altitude (m), thickness (m)
  float4 cloudAnim;        // time, windX, windZ, storminess
  float4 cloudQuality;     // shadowStrength, temporalAlpha, godRayStrength, flags
                           // flags: bit0 clouds, bit1 shadows, bit2 godrays, bit3 weather
  uint4 texIds0;           // depth, hdrSrc, cloudHistory, weatherPrev
  uint4 texIds1;           // weatherCurr, cloudBuffer, noiseBase3D, noiseDetail3D
  float4 ambientSky;       // approx sky/ambient radiance
};

Texture2D bindlessHeap[] : register(t0, space0);
Texture3D bindlessHeap3D[] : register(t0, space2);
SamplerState samp : register(s0);

uint safeId(uint id) { return id == 0xffffffffu ? 0u : id; }

// ---------------------------------------------------------------- constants

static const float kPlanetRadius = 6360000.0;   // m
static const float kMaxMarchDist = 34000.0;     // cap on in-layer march distance
static const float kHorizonFade = 42000.0;      // distance fade of the layer
static const int kMarchSteps = 64;
static const float kBaseNoiseScale = 1.0 / 7500.0; // base volume tiles every 7.5 km
static const float kDetailNoiseScale = 1.0 / 360.0; // detail tiles every 360 m
static const float kExtinction = 0.045;         // m^-1 at density 1 (scaled by density param)
static const float kCloudAlbedo = 0.96;
static const float kSunScale = 1.35;            // couples cloud sun energy to scene sun units

// ---------------------------------------------------------------- helpers

float cremap(float x, float a, float b, float c, float d) {
  return c + saturate((x - a) / max(b - a, 1e-5)) * (d - c);
}

// Animated interleaved gradient noise (per-pixel, per-frame dither).
float ign(float2 px, float frame) {
  px += (frame % 64.0) * float2(5.588238, 5.588238);
  return frac(52.9829189 * frac(0.06711056 * px.x + 0.00583715 * px.y));
}

// Returns entry/exit along ray for sphere at `center`, radius r; (-1,-1) if missed.
float2 raySphere(float3 ro, float3 rd, float3 center, float r) {
  float3 oc = ro - center;
  float b = dot(oc, rd);
  float c = dot(oc, oc) - r * r;
  float disc = b * b - c;
  if (disc < 0.0) {
    return float2(-1.0, -1.0);
  }
  float s = sqrt(disc);
  return float2(-b - s, -b + s);
}

float hgPhase(float cosTh, float g) {
  float g2 = g * g;
  return (1.0 - g2) / (4.0 * PI * pow(abs(1.0 + g2 - 2.0 * g * cosTh), 1.5));
}

// Dual-lobe HG: strong forward (silver lining) + mild backscatter. e attenuates per MS octave.
float phaseDual(float cosTh, float e) {
  return lerp(hgPhase(cosTh, -0.2 * e), hgPhase(cosTh, 0.72 * e), 0.62);
}

// ---------------------------------------------------------------- weather

// Evolving 2D weather field. Returns: x = coverage [0,1], y = cloud type (0 stratus → 1 cumulonimbus).
float2 weatherAt(float2 worldXZ) {
  float timeSec = cloudAnim.x;
  float2 windXZ = cloudAnim.yz;
  float coverage = cloudShape.x;
  float storminess = cloudAnim.w;

  float2 p = (worldXZ + windXZ * timeSec * 28.0) * 0.000065;
  float base = cloudFbm(float3(p.x, 0.0, p.y));
  float fronts = cloudFbm(float3(p.y * 0.31 + 17.3, 0.7, p.x * 0.27 - timeSec * 0.0015));
  float n = lerp(base, fronts, 0.3 + storminess * 0.25);

  // Wide soft threshold: cores reach full coverage, edges feather out.
  float cov = smoothstep(1.0 - coverage - 0.28, 1.0 - coverage + 0.1, n);
  cov *= lerp(0.72, 1.0, saturate(coverage * 1.2));
  cov = saturate(cov * (0.9 + storminess * 0.35));

  float t = cloudFbm(float3(p.x * 1.9 + 41.7, 0.35, p.y * 1.6 + 9.2));
  float type = saturate(cremap(t, 0.35, 0.7, 0.0, 1.0));
  type = saturate(lerp(type, 1.0, storminess * 0.55));
  return float2(cov, type);
}

// Stratus / cumulus / cumulonimbus vertical profiles blended by type (Nubis).
float densityHeightGradient(float hf, float type) {
  float stratus = cremap(hf, 0.0, 0.06, 0.0, 1.0) * cremap(hf, 0.14, 0.28, 1.0, 0.0);
  float cumulus = cremap(hf, 0.0, 0.14, 0.0, 1.0) * cremap(hf, 0.42, 0.85, 1.0, 0.0);
  float cumulon = cremap(hf, 0.0, 0.09, 0.0, 1.0) * cremap(hf, 0.7, 1.0, 1.0, 0.0);
  float lowMix = lerp(stratus, cumulus, saturate(type * 2.0));
  return lerp(lowMix, cumulon, saturate(type * 2.0 - 1.0));
}

// ---------------------------------------------------------------- density

float heightFrac(float3 p) {
  float3 center = float3(cameraPos.x, -kPlanetRadius, cameraPos.z);
  // Using camera-centered planet keeps precision; altitude relative to local ground.
  float r = length(p - center);
  return (r - (kPlanetRadius + cloudShape.z)) / max(cloudShape.w, 20.0);
}

// Two-stage cloud density. `cheap` skips the detail erosion (shadow rays / empty-space skip).
float sampleCloud(float3 p, float hf, float2 weather, bool cheap) {
  if (weather.x <= 0.015 || hf <= 0.0 || hf >= 1.0) {
    return 0.0;
  }
  float timeSec = cloudAnim.x;
  float2 windXZ = cloudAnim.yz;
  float3 wp = p;
  wp.xz += windXZ * timeSec * 18.0;   // advection
  wp.xz += windXZ * hf * 600.0;       // wind shear with height

  Texture3D noiseBase = bindlessHeap3D[NonUniformResourceIndex(safeId(texIds1.z))];
  float4 nb = noiseBase.SampleLevel(samp, wp * kBaseNoiseScale, 0);
  float lowFbm = nb.g * 0.625 + nb.b * 0.25 + nb.a * 0.125;
  float base = saturate(cremap(nb.r, lowFbm - 1.0, 1.0, 0.0, 1.0));
  base = saturate(cremap(base, 0.45, 0.95, 0.0, 1.0)); // expand contrast of the PW field
  base *= densityHeightGradient(hf, weather.y);

  float cov = weather.x;
  base = saturate(cremap(base, saturate(1.0 - cov), 1.0, 0.0, 1.0));
  if (cheap || base <= 1e-4) {
    return base;
  }

  Texture3D noiseDetail = bindlessHeap3D[NonUniformResourceIndex(safeId(texIds1.w))];
  float3 nd = noiseDetail.SampleLevel(samp, wp * kDetailNoiseScale, 0).rgb;
  float dFbm = nd.r * 0.625 + nd.g * 0.25 + nd.b * 0.125;
  // Wispy erosion at cloud base, billowy at the top.
  float dMod = lerp(dFbm, 1.0 - dFbm, saturate(hf * 6.0));
  return saturate(cremap(base, dMod * 0.28, 1.0, 0.0, 1.0));
}

// Optical depth toward the sun: 5 growing steps + one long-range tap. Reuses view-sample weather.
float sunOpticalDepth(float3 p, float3 sunL, float2 weather) {
  float thickness = max(cloudShape.w, 20.0);
  float od = 0.0;
  float stepL = thickness * 0.07;
  float3 sp = p;
  [unroll]
  for (int j = 0; j < 5; ++j) {
    float len = stepL * (1.0 + float(j) * 0.6);
    sp += sunL * len;
    od += sampleCloud(sp, heightFrac(sp), weather, j > 1) * len;
  }
  float3 fp = p + sunL * thickness * 0.85;
  od += sampleCloud(fp, heightFrac(fp), weather, true) * thickness * 0.3;
  return od;
}

// ---------------------------------------------------------------- march

float4 marchClouds(float3 ro, float3 rd, float maxT, float2 pixel) {
  float densityScale = cloudShape.y;
  float altitude = cloudShape.z;
  float thickness = max(cloudShape.w, 20.0);
  float sunI = sunDirIntensity.w;
  float3 sunL = normalize(-sunDirIntensity.xyz);

  float3 center = float3(cameraPos.x, -kPlanetRadius, cameraPos.z);
  float rBot = kPlanetRadius + altitude;
  float rTop = rBot + thickness;
  float rCam = length(ro - center);

  // March segment through the shell (camera below / inside / above).
  float2 tB = raySphere(ro, rd, center, rBot);
  float2 tT = raySphere(ro, rd, center, rTop);
  float t0;
  float t1;
  if (rCam < rBot) {
    t0 = tB.y;
    t1 = tT.y;
  } else if (rCam < rTop) {
    t0 = 0.0;
    t1 = (tB.x > 0.0) ? tB.x : tT.y;
  } else {
    if (tT.x < 0.0) {
      return float4(0, 0, 0, 1);
    }
    t0 = tT.x;
    t1 = (tB.x > 0.0) ? tB.x : tT.y;
  }
  // Planet occlusion (rays below the horizon).
  float2 tG = raySphere(ro, rd, center, kPlanetRadius);
  if (tG.x > 0.0) {
    t1 = min(t1, tG.x);
  }
  t0 = max(t0, 0.0);
  t1 = min(t1, maxT);
  t1 = min(t1, t0 + kMaxMarchDist);
  if (t1 <= t0) {
    return float4(0, 0, 0, 1);
  }

  int steps = int(lerp(40.0, float(kMarchSteps), saturate((t1 - t0) / 14000.0)));
  float dt = (t1 - t0) / float(steps);
  float t = t0 + dt * ign(pixel, cameraPos.w);

  float cosTh = dot(rd, sunL);
  // Precompute 3 multi-scattering octaves of the phase (eccentricity attenuates: c^o).
  float ph0 = phaseDual(cosTh, 1.0);
  float ph1 = phaseDual(cosTh, 0.5);
  float ph2 = phaseDual(cosTh, 0.25);

  float day = smoothstep(-0.05, 0.2, sunL.y);
  float3 sunRad = sunColor.rgb * (sunI * kSunScale * day);
  float3 ambTop = ambientSky.rgb * 2.4 + sunRad * 0.045;
  float3 ambBot = ambientSky.rgb * 1.1 + float3(0.02, 0.025, 0.035) * day;

  float transmittance = 1.0;
  float3 scatter = float3(0, 0, 0);
  float sigmaScale = kExtinction * densityScale;

  [loop]
  for (int i = 0; i < steps; ++i) {
    float3 p = ro + rd * t;
    float hf = heightFrac(p);
    float2 weather = weatherAt(p.xz);
    float dens = sampleCloud(p, hf, weather, false);
    if (dens > 1e-3) {
      float sigmaT = max(dens * sigmaScale, 1e-6);
      float od = sunOpticalDepth(p, sunL, weather) * sigmaScale;

      // Beer-powder on the primary octave (Schneider), plain Beer on the rest.
      float beer0 = exp(-od) * (1.0 - 0.4 * exp(-2.0 * od - dens * 4.0));
      float sunTerm = beer0 * ph0
                    + 0.7 * exp(-od * 0.4) * ph1
                    + 0.45 * exp(-od * 0.15) * ph2;

      float3 ambient = lerp(ambBot, ambTop, saturate(hf)) * (0.6 + 0.4 * exp(-od * 0.5));
      float3 S = (sunRad * sunTerm + ambient) * (sigmaT * kCloudAlbedo);

      float stepT = exp(-sigmaT * dt);
      scatter += transmittance * (S - S * stepT) / sigmaT; // Frostbite analytic integration
      transmittance *= stepT;
      if (transmittance < 0.008) {
        break;
      }
    }
    t += dt;
  }

  // Fade the layer into the distance (blends with atmosphere instead of hard cutoff).
  float fade = exp(-t0 / kHorizonFade) * saturate(1.0 - (t0 + 2000.0) / (kMaxMarchDist * 3.2));
  scatter *= fade;
  transmittance = lerp(1.0, transmittance, fade);
  return float4(scatter, saturate(transmittance));
}

// ---------------------------------------------------------------- passes

VSOut VSMain(uint id : SV_VertexID) {
  VSOut o;
  float2 uv = float2((id << 1) & 2, id & 2);
  // Match DeferredLighting / PostFX: no V flip (D3D NDC Y-up + this fullscreen triangle).
  o.uv = uv;
  o.position = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}

float3 reconstructWorld(float2 uv, float depth) {
  float2 ndc = uv * float2(2, -2) + float2(-1, 1);
  float4 clip = float4(ndc, depth, 1);
  float4 w = mul(invViewProj, clip);
  return w.xyz / max(w.w, 1e-6);
}

// Half-res cloud buffer: rgb = inscatter, a = transmittance
float4 PSCloudMarch(VSOut input) : SV_Target {
  uint flags = (uint)cloudQuality.w;
  if ((flags & 1u) == 0u) {
    return float4(0, 0, 0, 1);
  }
  Texture2D depthTex = bindlessHeap[NonUniformResourceIndex(safeId(texIds0.x))];
  float depth = depthTex.SampleLevel(samp, input.uv, 0).r;

  float2 ndc = input.uv * float2(2, -2) + float2(-1, 1);
  float4 wFar = mul(invViewProj, float4(ndc, 1.0, 1.0));
  float3 dir = normalize(wFar.xyz / max(wFar.w, 1e-6) - cameraPos.xyz);

  float maxT = 1e8;
  if (depth > 1e-4) {
    float3 worldPos = reconstructWorld(input.uv, depth);
    maxT = length(worldPos - cameraPos.xyz);
  }
#ifdef CLOUD_DEBUG
  // Diagnostic: raw base-noise slice + weather coverage + density mid-layer.
  float3 dp = cameraPos.xyz + dir * ((cloudShape.z + cloudShape.w * 0.5) / max(dir.y, 0.05));
  Texture3D noiseBase = bindlessHeap3D[NonUniformResourceIndex(safeId(texIds1.z))];
  float4 nb = noiseBase.SampleLevel(samp, dp * kBaseNoiseScale, 0);
  float2 wtr = weatherAt(dp.xz);
  float dens = sampleCloud(dp, heightFrac(dp), wtr, false);
  return float4(nb.r, wtr.x, dens * 4.0, 0.0);
#endif
  return marchClouds(cameraPos.xyz, dir, maxT, input.position.xy);
}

float4 PSCloudTemporal(VSOut input) : SV_Target {
  Texture2D currTex = bindlessHeap[NonUniformResourceIndex(safeId(texIds1.y))];
  Texture2D histTex = bindlessHeap[NonUniformResourceIndex(safeId(texIds0.z))];
  Texture2D depthTex = bindlessHeap[NonUniformResourceIndex(safeId(texIds0.x))];

  float4 curr = currTex.SampleLevel(samp, input.uv, 0);
  float depth = depthTex.SampleLevel(samp, input.uv, 0).r;
  float3 worldPos;
  if (depth > 1e-4) {
    worldPos = reconstructWorld(input.uv, depth);
  } else {
    float2 ndc = input.uv * float2(2, -2) + float2(-1, 1);
    float4 w = mul(invViewProj, float4(ndc, 1, 1));
    worldPos = w.xyz / max(w.w, 1e-6);
  }

  float4 prevClip = mul(prevViewProj, float4(worldPos, 1));
  float2 histUV = prevClip.xy / max(prevClip.w, 1e-6);
  histUV = histUV * float2(0.5, -0.5) + 0.5;

  float alpha = cloudQuality.y; // temporal blend toward history
  bool valid = all(histUV > 0.0) && all(histUV < 1.0) && prevClip.w > 0.0;
  if (!valid) {
    return curr;
  }
  float4 hist = histTex.SampleLevel(samp, histUV, 0);
  // Neighborhood clamp (cross taps)
  float2 px = float2(screenSize.z, screenSize.w) * 2.0;
  float4 n0 = currTex.SampleLevel(samp, input.uv + float2(px.x, 0), 0);
  float4 n1 = currTex.SampleLevel(samp, input.uv - float2(px.x, 0), 0);
  float4 n2 = currTex.SampleLevel(samp, input.uv + float2(0, px.y), 0);
  float4 n3 = currTex.SampleLevel(samp, input.uv - float2(0, px.y), 0);
  float4 cmin = min(curr, min(min(n0, n1), min(n2, n3)));
  float4 cmax = max(curr, max(max(n0, n1), max(n2, n3)));
  hist = clamp(hist, cmin - 0.03, cmax + 0.03);
  return lerp(curr, hist, alpha);
}

// Weather map RT: R = coverage, G = cloud type (shadows + rain coupling + debug).
float4 PSWeatherMap(VSOut input) : SV_Target {
  float2 worldXZ = cameraPos.xz + (input.uv * 2.0 - 1.0) * 4000.0;
  float2 w = weatherAt(worldXZ);
  Texture2D prevW = bindlessHeap[NonUniformResourceIndex(safeId(texIds0.w))];
  float2 windUV = cloudAnim.yz * 0.0004;
  float2 prev = prevW.SampleLevel(samp, input.uv - windUV, 0).rg;
  float2 outW = lerp(w, prev, 0.82);
  return float4(outW.x, outW.y, 0, 1);
}

float cloudShadowAt(float3 worldPos) {
  float altitude = cloudShape.z;
  float thickness = max(cloudShape.w, 20.0);
  float densityScale = cloudShape.y;
  float3 sunL = normalize(-sunDirIntensity.xyz);
  if (sunL.y < 0.02) {
    return 0.0; // night / below horizon — no hard cloud shadow
  }
  // Project up to the layer along the sun (planar approx is fine near the camera).
  float tHit = max((altitude - worldPos.y) / max(sunL.y, 1e-3), 0.0);
  float3 p = worldPos + sunL * tHit;
  float2 weather = weatherAt(p.xz);
  float od = 0.0;
  float stepL = thickness / 5.0;
  [unroll]
  for (int i = 0; i < 5; ++i) {
    od += sampleCloud(p, heightFrac(p), weather, true) * stepL;
    p += sunL * stepL;
  }
  float shadow = 1.0 - exp(-od * kExtinction * densityScale * 1.5);

  // Weather map soft widening
  float2 uv = (worldPos.xz - cameraPos.xz) / 4000.0 * 0.5 + 0.5;
  if (all(uv > 0.0) && all(uv < 1.0)) {
    Texture2D weatherTex = bindlessHeap[NonUniformResourceIndex(safeId(texIds1.x))];
    float wcov = weatherTex.SampleLevel(samp, uv, 0).r;
    shadow = max(shadow, wcov * cloudShape.x * 0.45);
  }
  return saturate(shadow);
}

float3 godRays(float2 uv, float3 base) {
  float strength = cloudQuality.z;
  if (strength < 1e-3) {
    return base;
  }
  float3 sunL = normalize(-sunDirIntensity.xyz);
  float3 sunWorld = cameraPos.xyz + sunL * 5000.0;
  float4 clip = mul(viewProj, float4(sunWorld, 1));
  if (clip.w < 0.0) {
    return base;
  }
  float2 sunUV = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
  Texture2D cloudTex = bindlessHeap[NonUniformResourceIndex(safeId(texIds1.y))];
  float3 accum = 0;
  const int k = 12;
  [unroll]
  for (int i = 0; i < k; ++i) {
    float t = float(i) / float(k - 1);
    float2 suv = lerp(uv, sunUV, t * 0.65);
    float4 c = cloudTex.SampleLevel(samp, suv, 0);
    float densityProxy = saturate(1.0 - c.a) * 0.65 + length(c.rgb) * 0.05;
    accum += densityProxy * sunColor.rgb * (sunDirIntensity.w * 0.04);
  }
  float2 d = uv - sunUV;
  float fall = saturate(1.0 - length(d) * 1.2);
  return base + accum / float(k) * strength * fall * fall;
}

// Full-res composite onto HDR
float4 PSCloudComposite(VSOut input) : SV_Target {
  Texture2D hdrTex = bindlessHeap[NonUniformResourceIndex(safeId(texIds0.y))];
  Texture2D cloudTex = bindlessHeap[NonUniformResourceIndex(safeId(texIds1.y))];
  Texture2D depthTex = bindlessHeap[NonUniformResourceIndex(safeId(texIds0.x))];

  float3 hdr = hdrTex.SampleLevel(samp, input.uv, 0).rgb;
  float4 cloud = cloudTex.SampleLevel(samp, input.uv, 0);
  float depth = depthTex.SampleLevel(samp, input.uv, 0).r;

  float3 color = hdr * cloud.a + cloud.rgb;

  uint flags = (uint)cloudQuality.w;
  bool doShadow = (flags & 2u) != 0;
  bool doGod = (flags & 4u) != 0;

  if (doShadow && depth > 1e-4) {
    float3 worldPos = reconstructWorld(input.uv, depth);
    float sh = cloudShadowAt(worldPos);
    color *= lerp(1.0, 1.0 - sh * 0.85, cloudQuality.x);
  }

  if (doGod) {
    color = godRays(input.uv, color);
  }
  return float4(max(color, 0.0), 1.0);
}
