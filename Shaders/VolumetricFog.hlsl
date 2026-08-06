// Froxel volumetric fog.
//
// Two compute passes over a camera-fitted 3D grid:
//   CSInject     — per froxel: density from height fog x noise, then the light reaching it
//                  (sun through the cascaded shadow map, phase-weighted, plus ambient).
//                  Temporally blended with the previous frame's volume.
//   CSIntegrate  — per column: front-to-back march accumulating in-scattering and transmittance.
//
// The lighting pass then samples the integrated volume at each pixel's froxel and applies
// `color * transmittance + scattering`. Because density and lighting are evaluated in the volume
// rather than per pixel, the fog is shadowed and scatters anisotropically — which is what turns
// a flat distance tint into light shafts.
//
// Slices are distributed by `depth = maxDistance * pow(slice / numSlices, depthPower)`, so the
// near field (where fog structure actually reads) gets most of the resolution.

cbuffer FogCB : register(b1) {
  float4x4 invViewProj;
  float4x4 prevViewProj;      // reprojection of the history volume
  float4 cameraPos;           // xyz, time
  float4 volumeSize;          // x,y,z froxel counts, w = depthPower
  float4 fogMedium;           // density, baseHeight, heightFalloff, albedo
  float4 fogPhase;            // anisotropy, maxDistance, temporalBlend, shadowStrength
  float4 fogLight;            // sunIntensity, ambientIntensity, noiseStrength, noiseScale
  float4 fogWind;             // xyz wind direction * speed, w = frame jitter
  float4 scatterColor;        // rgb tint, w unused
  float4 sunDirectionIntensity; // xyz sun->scene, w intensity
  float4 sunColor;            // rgb, w unused
  float4 ambientColor;        // rgb, w unused
  float4x4 lightViewProj[4];
  float4 cascadeSplits;
  uint4 fogTexIds;            // shadowCSM, _, _, _
};

Texture2D bindlessHeap[] : register(t0, space0);
SamplerState linearSamp : register(s0);

RWTexture3D<float4> ScatterVolume : register(u0);
RWTexture3D<float4> HistoryVolume : register(u1);
RWTexture3D<float4> IntegratedVolume : register(u2);

static const float kPi = 3.14159265;

uint safeTex(uint id) { return id < 100000u ? id : 0u; }

// ── Froxel <-> view depth ────────────────────────────

float sliceToDepth(float slice) {
  const float n = max(volumeSize.z, 1.0);
  return fogPhase.y * pow(saturate(slice / n), max(volumeSize.w, 1.0));
}

// ── Noise ────────────────────────────────────────────

float fogHash(float3 p) {
  p = frac(p * 0.3183099 + 0.1);
  p *= 17.0;
  return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float fogNoise(float3 x) {
  float3 i = floor(x);
  float3 f = frac(x);
  f = f * f * (3.0 - 2.0 * f);
  return lerp(lerp(lerp(fogHash(i + float3(0, 0, 0)), fogHash(i + float3(1, 0, 0)), f.x),
                   lerp(fogHash(i + float3(0, 1, 0)), fogHash(i + float3(1, 1, 0)), f.x), f.y),
              lerp(lerp(fogHash(i + float3(0, 0, 1)), fogHash(i + float3(1, 0, 1)), f.x),
                   lerp(fogHash(i + float3(0, 1, 1)), fogHash(i + float3(1, 1, 1)), f.x), f.y),
              f.z);
}

float fogFbm(float3 p) {
  float a = 0.5, s = 0.0, w = 0.0;
  [unroll]
  for (int i = 0; i < 3; ++i) {
    s += a * fogNoise(p);
    w += a;
    p = p * 2.07 + float3(3.1, 1.7, 9.2);
    a *= 0.5;
  }
  return s / max(w, 1e-3);
}

// ── Medium ───────────────────────────────────────────

/// Extinction at a world position: exponential height falloff, optionally broken up by drifting
/// 3D noise so the medium is not a perfectly uniform slab.
float mediumDensity(float3 worldPos) {
  const float heightAbove = max(worldPos.y - fogMedium.y, 0.0);
  float d = fogMedium.x * exp(-heightAbove / max(fogMedium.z, 0.1));

  const float noiseStrength = fogLight.z;
  if (noiseStrength > 0.001) {
    const float3 p = (worldPos + fogWind.xyz * cameraPos.w) / max(fogLight.w, 1.0);
    // Remap to keep the mean density: noise thins as much as it thickens.
    const float n = fogFbm(p) * 2.0 - 1.0;
    d *= saturate(1.0 + n * noiseStrength);
  }
  return max(d, 0.0);
}

/// Henyey-Greenstein. cosTheta is measured between the view ray and the direction light travels.
float phaseHG(float cosTheta, float g) {
  const float g2 = g * g;
  const float denom = 1.0 + g2 - 2.0 * g * cosTheta;
  return (1.0 - g2) / (4.0 * kPi * max(pow(abs(denom), 1.5), 1e-4));
}

// ── Sun shadowing of the medium ──────────────────────

/// Cascaded shadow lookup, single tap. The volume is coarse and temporally filtered, so PCF here
/// would cost more than it shows.
float fogShadow(float3 worldPos, float viewDepth) {
  const uint shadowId = safeTex(fogTexIds.x);
  if (shadowId == 0) return 1.0;

  int cascade = 0;
  if (viewDepth > cascadeSplits.x) cascade = 1;
  if (viewDepth > cascadeSplits.y) cascade = 2;
  if (viewDepth > cascadeSplits.z) cascade = 3;

  const float4 lightClip = mul(lightViewProj[cascade], float4(worldPos, 1.0));
  const float3 ndc = lightClip.xyz / max(lightClip.w, 1e-6);
  float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
  const float2 atlasOffset = float2(cascade % 2, cascade / 2) * 0.5;
  uv = atlasOffset + uv * 0.5;
  const float2 lo = atlasOffset + 0.0015;
  const float2 hi = atlasOffset + 0.5 - 0.0015;
  if (any(uv < lo) || any(uv > hi)) return 1.0;

  const float compare = ndc.z - (0.002 + 0.0012 * cascade);
  const float d = bindlessHeap[NonUniformResourceIndex(shadowId)].SampleLevel(linearSamp, uv, 0).r;
  return compare <= d ? 1.0 : 0.0;
}

// ── Pass 1: inject ───────────────────────────────────

[numthreads(8, 8, 4)]
void CSInject(uint3 id : SV_DispatchThreadID) {
  const uint3 dims = uint3(volumeSize.xyz);
  if (any(id >= dims)) return;

  // Jitter the slice so successive frames sample different depths inside the same froxel; the
  // temporal blend below then integrates them, which is what removes the visible slice banding.
  const float jitter = fogWind.w;
  const float2 uv = (float2(id.xy) + 0.5) / float2(dims.xy);
  const float depthNear = sliceToDepth(float(id.z) + jitter);

  // World position of this froxel centre: unproject the pixel direction, then walk along the ray.
  //
  // Slice depth is a distance ALONG THE RAY, not along the view axis. The lighting pass looks the
  // volume up with `length(worldPos - cameraPos)`, so the two have to agree — measuring one in
  // view-space Z and the other along the ray put every off-centre froxel at the wrong depth.
  const float4 clipNear = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
  float4 wNear = mul(invViewProj, clipNear);
  wNear /= max(wNear.w, 1e-6);
  const float3 rayDir = normalize(wNear.xyz - cameraPos.xyz);
  const float3 worldPos = cameraPos.xyz + rayDir * depthNear;

  const float density = mediumDensity(worldPos);
  const float extinction = density;
  const float3 scatteringCoeff = density * fogMedium.w * scatterColor.rgb;

  // Sun
  const float3 sunToScene = normalize(sunDirectionIntensity.xyz);
  const float phase = phaseHG(dot(rayDir, sunToScene), fogPhase.x);
  const float shadow = lerp(1.0, fogShadow(worldPos, depthNear), saturate(fogPhase.w));
  const float3 sunRadiance =
      sunColor.rgb * sunDirectionIntensity.w * fogLight.x * shadow * phase;

  // Ambient. Isotropic, so no phase term.
  //
  // Deliberately NOT divided by 4*pi: ambientColor is already the integrated ambient radiance the
  // rest of the renderer shades with, not a per-steradian quantity. Normalising it again made the
  // in-scattering negligible next to the extinction, so far and shadowed fog absorbed everything
  // and contributed nothing — it came out as a black band rather than haze.
  const float3 ambient = ambientColor.rgb * fogLight.y;

  float4 current = float4(scatteringCoeff * (sunRadiance + ambient), extinction);

  // Temporal reprojection: find where this froxel was last frame and blend. Rejects samples that
  // fall outside the volume, which is what stops smearing at the screen edges.
  const float blend = saturate(fogPhase.z);
  if (blend > 0.001) {
    const float4 prevClip = mul(prevViewProj, float4(worldPos, 1.0));
    if (prevClip.w > 1e-4) {
      const float2 prevUV = prevClip.xy / prevClip.w * float2(0.5, -0.5) + 0.5;
      // Ray distance from last frame's camera, to match how the slice index is defined. The
      // camera barely moves between frames, so reusing this frame's origin is accurate enough
      // and avoids carrying a second camera position through the constant buffer.
      const float prevDepth = distance(worldPos, cameraPos.xyz);
      const float prevSlice =
          pow(saturate(prevDepth / max(fogPhase.y, 1e-3)), 1.0 / max(volumeSize.w, 1.0)) *
          volumeSize.z;
      if (all(prevUV >= 0.0) && all(prevUV <= 1.0) && prevSlice >= 0.0 && prevSlice < volumeSize.z) {
        const int3 prevCoord = int3(prevUV * dims.xy, prevSlice);
        const float4 history = HistoryVolume[prevCoord];
        // A history entry of exactly zero means "never written" (first frame after a resize).
        if (any(history != 0.0)) current = lerp(current, history, blend);
      }
    }
  }

  ScatterVolume[id] = current;
}

// ── Pass 2: integrate ────────────────────────────────

[numthreads(8, 8, 1)]
void CSIntegrate(uint3 id : SV_DispatchThreadID) {
  const uint3 dims = uint3(volumeSize.xyz);
  if (any(id.xy >= dims.xy)) return;

  float3 accumScattering = 0.0;
  float accumTransmittance = 1.0;
  float prevDepth = 0.0;

  for (uint z = 0; z < dims.z; ++z) {
    const uint3 coord = uint3(id.xy, z);
    const float4 slice = ScatterVolume[coord];

    const float depth = sliceToDepth(float(z) + 1.0);
    const float thickness = max(depth - prevDepth, 1e-4);
    prevDepth = depth;

    const float extinction = max(slice.a, 1e-7);
    const float sliceTransmittance = exp(-extinction * thickness);

    // Energy-conserving slice integration (Hillaire): the analytic integral of in-scattering
    // across the slice, rather than a point sample multiplied by thickness. Without it the fog
    // brightness depends on how the slices happen to be distributed.
    const float3 sliceScattering = (slice.rgb - slice.rgb * sliceTransmittance) / extinction;

    accumScattering += accumTransmittance * sliceScattering;
    accumTransmittance *= sliceTransmittance;

    IntegratedVolume[coord] = float4(accumScattering, accumTransmittance);
  }
}
