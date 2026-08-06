#include "Common.hlsl"
#include "Atmosphere.hlsl"

// Screen-space ocean.
//
// The pass runs after lighting, over the whole screen, and shades every pixel whose view ray
// crosses the water plane in front of whatever geometry the g-buffer recorded there. That includes
// sky pixels, so the ocean reaches the horizon instead of stopping at the edge of the seabed mesh.
//
// Reflection is screen-space ray-marched against the depth buffer and falls back to the analytic
// sky on a miss, so the water mirrors real scene geometry and the sun. Refraction reads the lit
// HDR buffer through a normal-driven offset and is attenuated by Beer-Lambert absorption over the
// actual underwater path length. Rain, when the weather system reports it, adds impact ripples,
// raises microfacet roughness (which is what breaks the mirror in a downpour) and thickens foam.

struct VSOut {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

cbuffer WaterCB : register(b1) {
  float4x4 invViewProj;
  float4x4 viewProj;
  float4 cameraPos;         // xyz, time
  float4 screenSize;        // w, h, 1/w, 1/h
  float4 waveParams0;       // amplitude, frequency, speed, choppy
  float4 waveParams1;       // waveCount, foamAmount, sssIntensity, shoreHardness
  float4 waterColor;        // rgb scattering tint, fogDensity
  float4 windLevel;         // wind.x, wind.z, storminess, waterLevel
  float4 sunParams;         // sunToScene.xyz, turbidity
  float4 sunColorIntensity; // sunColor.rgb, sunIntensity
  float4 waterMisc;         // normalStrength, reflectionStrength, roughness, detailFadeDistance
  float4 ssrParams;         // steps, maxDistance, edgeFade, refractionStrength
  float4 absorptionParams;  // absorption.rgb, _
  float4 rainParams;        // intensity, rippleStrength, rippleScale, rippleSpeed
  float4 rainParams2;       // rainRoughness, rainFoam, enableSSR, _
  float4 iblParams;         // prefilteredMaxMip, iblExposure, _, _
  uint4 texIndices;         // depthIdx, hdrIdx, weatherIdx, prefilteredIdx
};

Texture2D bindlessHeap[] : register(t0, space0);
SamplerState linearSamp : register(s0);

static const float kSkyDepth = 1e-4; // DepthColor clears to 0; sky stays there

VSOut VSMain(uint id : SV_VertexID) {
  VSOut o;
  o.uv = float2((id << 1) & 2, id & 2);
  o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}

float3 reconstructWorldPos(float2 uv, float depth) {
  float4 clip = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0);
  float4 world = mul(invViewProj, clip);
  return world.xyz / max(world.w, 1e-6);
}

/// Point-samples the depth target. SSR needs exact texels: a filtered fetch blends the depth of
/// a silhouette with the depth of whatever is behind it and reports a surface that is not there.
float loadDepth(float2 uv) {
  int2 px = int2(clamp(uv, 0.0, 1.0) * screenSize.xy);
  px = clamp(px, int2(0, 0), int2(screenSize.xy) - 1);
  return bindlessHeap[texIndices.x].Load(int3(px, 0)).r;
}

/// Environment fallback for the reflection ray. Prefers the prefiltered IBL probe, which is the
/// same lit environment the rest of the renderer shades against — the analytic sky is a different
/// model from the one the sky pass draws (Bruneton), and mirroring it puts a bright band along the
/// horizon that does not match the sky above it. Falls back to the analytic sky with no probe.
float3 sampleEnvironment(float3 dir, float roughness) {
  const uint id = texIndices.w;
  if (id != 0) {
    const float mip = saturate(roughness) * iblParams.x;
    return bindlessHeap[id].SampleLevel(linearSamp, dirToLatLong(dir), mip).rgb * iblParams.y;
  }
  return atmosphereSky(dir, sunParams.xyz, sunParams.w, sunColorIntensity.w);
}

float2 hash22(float2 p) {
  float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
  p3 += dot(p3, p3.yzx + 33.33);
  return frac((p3.xx + p3.yz) * p3.zy);
}

// ── Wave field ───────────────────────────────────────
//
// A sum of directional sine waves. The height and its analytic XZ derivatives come out of the
// same loop, so the normal is exact for the surface actually being drawn rather than a
// finite-difference guess. Gerstner horizontal displacement is folded in as a crest-sharpening
// term, which is what feeds whitecap detection.

struct WaveField {
  float height;
  float2 slope;   // dh/dx, dh/dz
  float crest;    // 0..1 sharpness, drives foam
};

WaveField evaluateWaves(float2 xz, float time, float detailFade) {
  WaveField w;
  w.height = 0.0;
  w.slope = float2(0.0, 0.0);
  w.crest = 0.0;

  const float amp0 = waveParams0.x;
  const float freq0 = waveParams0.y;
  const float speed = waveParams0.z;
  const float choppy = waveParams0.w;
  const int count = clamp(int(waveParams1.x), 1, 12);
  const float2 windDir = normalize(windLevel.xy + float2(1e-5, 1e-5));
  const float storminess = windLevel.z;

  float ampSum = 1e-5;
  for (int i = 0; i < count; ++i) {
    const float fi = float(i) + 1.0;

    // Amplitude falls and frequency rises geometrically — a compact approximation of an ocean
    // spectrum. Octaves past the fourth are surface detail and fade out with distance so the
    // far field does not alias into a shimmering mess.
    const float amp = amp0 * pow(0.72, fi - 1.0);
    const float freq = freq0 * pow(1.62, fi - 1.0);
    const float octaveFade = fi <= 3.0 ? 1.0 : detailFade;
    if (octaveFade <= 0.001) continue;

    // Spread successive octaves off the wind axis so the field is not a set of parallel bars.
    const float spread = (0.55 + storminess * 0.35) * sin(fi * 2.399);
    const float2 dir = normalize(float2(
        windDir.x * cos(spread) - windDir.y * sin(spread),
        windDir.x * sin(spread) + windDir.y * cos(spread)));

    // Deep-water dispersion: phase speed of a wave of wavenumber k is sqrt(g/k).
    const float phaseSpeed = sqrt(9.81 / max(freq, 1e-3)) * speed * 0.12;
    const float phase = dot(dir, xz) * freq + time * phaseSpeed * freq;

    const float s = sin(phase);
    const float c = cos(phase);
    const float a = amp * octaveFade;

    w.height += a * s;
    w.slope += dir * (a * freq * c);
    // Crest term: peaks where the wave is near its maximum and steep.
    w.crest += a * saturate(s) * saturate(abs(c) * choppy * 2.0);
    ampSum += a;
  }

  w.crest = saturate(w.crest / ampSum);
  return w;
}

// ── Rain ripples ─────────────────────────────────────
//
// One impact per grid cell per period, at a random position and a random phase. The expanding
// ring is a damped sine in the radial distance, so the perturbation is a real outward-travelling
// wavefront rather than a static bump texture.
float2 rainRippleSlope(float2 xz, float time, float scale, float speed) {
  const float2 p = xz * scale;
  const float2 cell = floor(p);
  const float2 f = p - cell;

  float2 slope = float2(0.0, 0.0);
  [unroll]
  for (int dy = -1; dy <= 1; ++dy) {
    [unroll]
    for (int dx = -1; dx <= 1; ++dx) {
      const float2 neighbour = float2(float(dx), float(dy));
      const float2 rnd = hash22(cell + neighbour);
      const float2 d = f - (neighbour + rnd);
      const float r = length(d);
      if (r > 1.4) continue;

      // age in 0..1 across one impact period, offset per cell so drops are not synchronised
      const float age = frac(time * speed * (0.6 + rnd.y * 0.5) + rnd.x);
      const float radius = age * 1.25;
      const float envelope = smoothstep(0.0, 0.06, age) * (1.0 - smoothstep(0.35, 1.0, age));
      if (envelope <= 0.001) continue;

      const float band = r - radius;
      const float ring = sin(band * 38.0) * exp(-abs(band) * 14.0) * envelope;
      slope += (d / max(r, 1e-4)) * ring;
    }
  }
  return slope;
}

// ── Screen-space reflection ──────────────────────────
//
// Marches the reflection ray in world space and projects each sample to the screen, which keeps
// the step length uniform in world units — a UV-space march bunches samples near the horizon,
// exactly where an ocean needs them spread out. Returns false when the ray leaves the screen,
// runs out of budget, or only ever passes behind geometry.
bool traceReflection(float3 origin, float3 dir, float maxDistance, int steps, out float3 hitColor,
                     out float confidence) {
  hitColor = float3(0, 0, 0);
  confidence = 0.0;

  const float stepLen = maxDistance / float(steps);
  float t = stepLen * 1.5; // start off the surface to avoid self-intersection
  float prevT = t;
  bool prevBehind = false;

  for (int i = 0; i < steps; ++i) {
    const float3 samplePos = origin + dir * t;
    const float4 clip = mul(viewProj, float4(samplePos, 1.0));
    if (clip.w <= 1e-4) return false;

    const float3 ndc = clip.xyz / clip.w;
    const float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0)) return false;

    const float sceneDepth = loadDepth(uv);
    if (sceneDepth > kSkyDepth) {
      const float3 scenePos = reconstructWorldPos(uv, sceneDepth);
      const float rayDist = distance(origin, samplePos);
      const float sceneDist = distance(origin, scenePos);

      // Ray is behind the recorded surface: crossing happened between prevT and t.
      if (rayDist > sceneDist) {
        // Refine by bisection so the hit lands on the surface rather than a step past it.
        float lo = prevBehind ? t - stepLen : prevT;
        float hi = t;
        [unroll]
        for (int r = 0; r < 5; ++r) {
          const float mid = (lo + hi) * 0.5;
          const float3 mp = origin + dir * mid;
          const float4 mc = mul(viewProj, float4(mp, 1.0));
          const float2 muv = mc.xy / mc.w * float2(0.5, -0.5) + 0.5;
          const float md = loadDepth(muv);
          const float3 msp = reconstructWorldPos(muv, md);
          if (md > kSkyDepth && distance(origin, mp) > distance(origin, msp)) hi = mid;
          else lo = mid;
        }

        const float3 finalPos = origin + dir * hi;
        const float4 fc = mul(viewProj, float4(finalPos, 1.0));
        const float2 fuv = saturate(fc.xy / fc.w * float2(0.5, -0.5) + 0.5);
        const float fd = loadDepth(fuv);
        const float3 fsp = reconstructWorldPos(fuv, fd);

        // Thickness reject: a hit far behind the recorded surface means the ray passed through
        // a thin object and the "hit" is really the empty space behind it.
        if (abs(distance(origin, finalPos) - distance(origin, fsp)) > stepLen * 2.5) return false;

        hitColor = bindlessHeap[texIndices.y].SampleLevel(linearSamp, fuv, 0).rgb;

        // Fade out at the screen border (no data past it) and as the ray runs long.
        const float2 edge = min(fuv, 1.0 - fuv) / max(ssrParams.z, 1e-3);
        confidence = saturate(min(edge.x, edge.y)) * (1.0 - saturate(hi / maxDistance));
        return confidence > 0.001;
      }
      prevBehind = false;
    }

    prevT = t;
    // Geometric step growth: dense near the surface where reflections read as contact, coarse
    // far away where a missed intersection is invisible anyway.
    t += stepLen * (1.0 + float(i) * 0.08);
  }
  return false;
}

float4 PSMain(VSOut input) : SV_Target0 {
  const float2 uv = input.uv;
  const float3 sceneColor = bindlessHeap[texIndices.y].SampleLevel(linearSamp, uv, 0).rgb;

  const float depth = loadDepth(uv);
  const bool isSky = depth <= kSkyDepth;

  const float3 camPos = cameraPos.xyz;
  const float time = cameraPos.w;
  const float waterLevel = windLevel.w;

  // The near-plane reconstruction gives the view ray for every pixel, sky included.
  const float3 rayDir = normalize(reconstructWorldPos(uv, 0.0) - camPos);
  const float sceneDist = isSky ? 1e9 : distance(reconstructWorldPos(uv, depth), camPos);

  // ── Underwater ───────────────────────────────────
  if (camPos.y < waterLevel) {
    const float3 absorb = absorptionParams.rgb;
    const float dist = min(sceneDist, 400.0);
    const float3 transmittance = exp(-absorb * dist);
    const float3 deep = waterColor.rgb * sunColorIntensity.rgb * 0.25;
    // Light reaching this depth from the surface above.
    const float surfaceFade = saturate(exp(-(waterLevel - camPos.y) * 0.06));
    return float4(lerp(deep * surfaceFade, sceneColor * transmittance + deep * surfaceFade,
                       exp(-dist * waterColor.a)),
                  1.0);
  }

  // ── Ray/plane intersection ───────────────────────
  // Only downward rays can reach the surface from above it.
  if (rayDir.y > -1e-4) return float4(sceneColor, 1.0);

  float t = (waterLevel - camPos.y) / rayDir.y;
  if (t <= 0.0 || t >= sceneDist) return float4(sceneColor, 1.0);

  const float detailFade =
      saturate(1.0 - t / max(waterMisc.w, 1.0));

  // One refinement against the displaced surface: solve for the height at the flat hit, then
  // re-intersect. Enough for the wave slopes this field produces, and it keeps the silhouette
  // of large swells correct against the buoys.
  float3 P = camPos + rayDir * t;
  WaveField wave = evaluateWaves(P.xz, time, detailFade);
  t = (waterLevel + wave.height - camPos.y) / rayDir.y;
  if (t <= 0.0 || t >= sceneDist) return float4(sceneColor, 1.0);
  P = camPos + rayDir * t;
  wave = evaluateWaves(P.xz, time, detailFade);
  P.y = waterLevel + wave.height;

  // ── Surface normal ───────────────────────────────
  const float rain = saturate(rainParams.x);
  float2 slope = wave.slope * waterMisc.x;

  if (rain > 0.001) {
    const float2 ripple = rainRippleSlope(P.xz, time, rainParams.z, rainParams.w);
    slope += ripple * rainParams.y * rain * detailFade;
  }

  const float3 N = normalize(float3(-slope.x, 1.0, -slope.y));
  const float3 V = -rayDir;
  const float3 L = normalize(-sunParams.xyz); // sunParams.xyz points from sun toward the scene
  const float NoV = saturate(dot(N, V));

  // Rain roughens the surface, which is what visually separates a downpour from a calm day:
  // the mirror breaks up and the sun highlight spreads into a broad sheen.
  const float roughness = saturate(waterMisc.z + rain * rainParams2.x);

  // ── Reflection ───────────────────────────────────
  float3 R = reflect(rayDir, N);
  R.y = abs(R.y); // never reflect below the horizon: there is nothing down there to sample

  const float turbidity = sunParams.w;
  const float sunIntensity = sunColorIntensity.w;
  float3 reflection = sampleEnvironment(R, roughness);

  if (rainParams2.z > 0.5) {
    float3 ssrColor;
    float ssrConfidence;
    if (traceReflection(P, R, ssrParams.y, int(ssrParams.x), ssrColor, ssrConfidence)) {
      // A rough surface scatters the reflection, so trust the mirror-direction hit less.
      ssrConfidence *= saturate(1.0 - roughness * 3.0);
      reflection = lerp(reflection, ssrColor, ssrConfidence);
    }
  }
  reflection *= waterMisc.y;

  // ── Sun specular ─────────────────────────────────
  // Explicit GGX lobe on top of the sky's own sun disc. Without it the surface reads as painted
  // plastic: the analytic sky alone has no view-dependent highlight tied to the wave normals.
  const float3 H = normalize(L + V);
  const float NoL = saturate(dot(N, L));
  const float NoH = saturate(dot(N, H));
  const float VoH = saturate(dot(V, H));
  // At the horizon NoV goes to zero. The separable-Smith form divided by 4·NoL·NoV then blows up
  // into a white band along the skyline, so use the height-correlated visibility term, which
  // already contains the 1/(4·NoL·NoV) factor and stays bounded.
  const float NoVs = max(NoV, 1e-3);
  const float NoLs = max(NoL, 1e-3);
  const float a = max(roughness * roughness, 1e-3);
  const float a2 = a * a;
  const float dDenom = NoH * NoH * (a2 - 1.0) + 1.0;
  const float D = a2 / max(3.14159265 * dDenom * dDenom, 1e-7);
  const float lambdaV = NoLs * sqrt(NoVs * NoVs * (1.0 - a2) + a2);
  const float lambdaL = NoVs * sqrt(NoLs * NoLs * (1.0 - a2) + a2);
  const float Vis = 0.5 / max(lambdaV + lambdaL, 1e-6);
  const float3 F0 = float3(0.02, 0.02, 0.02);
  const float3 Fspec = F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
  float3 specular = D * Vis * Fspec * NoL * sunColorIntensity.rgb * sunIntensity;

  // ── Refraction + absorption ──────────────────────
  const float pathLength = max(sceneDist - t, 0.0);

  // Offset scales with how much water is between the surface and the bottom: a shallow puddle
  // barely bends, a deep channel bends a lot. Without the scale the whole image smears.
  const float2 refractOffset =
      slope * ssrParams.w * saturate(pathLength * 0.25) * float2(1.0, -1.0);
  float2 refractUV = saturate(uv + refractOffset);

  // Reject the offset when it lands on something in front of the water, which is what produces
  // the halo of foreground geometry bleeding into the surface.
  const float refractDepth = loadDepth(refractUV);
  if (refractDepth > kSkyDepth) {
    const float refractDist = distance(reconstructWorldPos(refractUV, refractDepth), camPos);
    if (refractDist < t) refractUV = uv;
  } else {
    refractUV = uv; // sampled the sky through the water: not a valid refraction
  }

  const float3 refractedScene = bindlessHeap[texIndices.y].SampleLevel(linearSamp, refractUV, 0).rgb;

  // Beer-Lambert over the real underwater path, plus the in-scattered body colour that gives
  // deep water its blue instead of leaving it black.
  const float3 transmittance = exp(-absorptionParams.rgb * min(pathLength, 200.0));
  const float3 scatterColor = waterColor.rgb * sunColorIntensity.rgb * sunIntensity * 0.12;
  float3 refraction = refractedScene * transmittance + scatterColor * (1.0 - transmittance);

  // Subsurface glow where a thin, backlit wave crest is between the camera and the sun.
  const float sss = pow(saturate(dot(V, -L)), 4.0) * saturate(wave.height * 0.8 + 0.4) *
                    waveParams1.z * saturate(1.0 - NoV);
  refraction += waterColor.rgb * sunColorIntensity.rgb * sss * sunIntensity * 0.5;

  // ── Cloud shadow ─────────────────────────────────
  float cloudShadow = 1.0;
  if (texIndices.z != 0) {
    const float2 weatherUV = (P.xz + float2(2000.0, 2000.0)) * 0.00025;
    cloudShadow = lerp(1.0, bindlessHeap[texIndices.z].SampleLevel(linearSamp, weatherUV, 0).r, 0.5);
  }
  specular *= cloudShadow;
  refraction *= lerp(0.75, 1.0, cloudShadow);

  // ── Fresnel composite ────────────────────────────
  const float3 fresnel = F0 + (1.0 - F0) * pow(1.0 - NoV, 5.0);
  float3 result = lerp(refraction, reflection, saturate(fresnel)) + specular;

  // ── Foam ─────────────────────────────────────────
  // Whitecaps on steep crests, a band in shallow water, and extra coverage under rain. The
  // crest term is squared so foam sits on the peaks instead of washing over whole swells.
  const float crestFoam = smoothstep(0.55, 0.95, wave.crest) * waveParams1.y;
  const float shoreFoam =
      (1.0 - smoothstep(0.0, max(waveParams1.w, 0.01) * 1.5, pathLength)) * waveParams1.y * 0.8;
  const float rainFoam = rain * rainParams2.y;
  const float foam = saturate(crestFoam + shoreFoam + rainFoam) * detailFade;

  const float3 foamColor = sunColorIntensity.rgb * sunIntensity * 0.35 * cloudShadow;
  result = lerp(result, foamColor, foam);

  // Distance fade into the sky so the ocean does not end in a hard line at the far plane.
  const float horizonFade = saturate(1.0 - t / 3500.0);
  result = lerp(sampleEnvironment(rayDir, 0.0), result, horizonFade);

  return float4(result, 1.0);
}
