#ifndef TUCANO_ATMOSPHERE_HLSL
#define TUCANO_ATMOSPHERE_HLSL

// Artistic Rayleigh/Mie sky (Nishita-inspired, single-scatter approx).
// viewDir / sunDir: world-space unit vectors; sunDir points FROM sun toward scene (same as lighting).
// Cloud density/slab helpers shared with Clouds.hlsl (CloudSystem owns volumetric rendering).

float3 atmosphereSky(float3 viewDir, float3 sunToScene, float turbidity, float sunIntensity) {
  float3 V = normalize(viewDir);
  float3 L = normalize(-sunToScene); // toward sun
  float cosZenith = saturate(V.y);
  float cosSun = saturate(L.y);

  float3 betaR = float3(5.8e-3, 1.35e-2, 3.31e-2) * lerp(0.7, 1.6, saturate((turbidity - 1.0) * 0.15));
  float betaM = 2.1e-2 * saturate(turbidity * 0.12);

  float Kr = 1.0 / (cosZenith + 0.15 * pow(93.885 - degrees(acos(clamp(cosZenith, -1.0, 1.0))), -1.253) + 1e-3);
  float3 rayleigh = betaR * Kr;

  float g = clamp(0.76 - turbidity * 0.02, 0.0, 0.95);
  float cosTheta = dot(V, L);
  float miePhase = (1.0 - g * g) / max(pow(1.0 + g * g - 2.0 * g * cosTheta, 1.5), 1e-4);
  float3 mie = betaM * miePhase * Kr * 0.35;

  float day = smoothstep(-0.08, 0.18, cosSun);
  float dusk = smoothstep(-0.05, 0.12, cosSun) * (1.0 - smoothstep(0.12, 0.45, cosSun));
  float3 daySky = rayleigh * 18.0 + mie * sunIntensity * 0.08;
  float3 duskSky = float3(0.55, 0.22, 0.08) * (0.4 + rayleigh.b * 8.0) + mie * 0.5;
  float3 nightSky = float3(0.01, 0.015, 0.04) * (0.4 + cosZenith);

  float3 sky = lerp(nightSky, daySky, day);
  sky = lerp(sky, duskSky, dusk * 0.85);

  float sunDisk = pow(saturate(cosTheta), lerp(512.0, 2048.0, saturate(cosSun)));
  sky += sunDisk * float3(1.0, 0.92, 0.75) * sunIntensity * day * 2.5;

  if (V.y < 0.0) {
    float3 ground = float3(0.06, 0.055, 0.04) * (0.15 + day * 0.5);
    sky = lerp(sky, ground, saturate(-V.y * 2.0));
  }
  return max(sky, 0.0);
}

float atmosphereFogFactor(float viewDist, float fogDensity, float height, float fogHeight) {
  float d = max(fogDensity, 0.0);
  if (d < 1e-5) {
    return 0.0;
  }
  float hFalloff = exp(-max(height, 0.0) / max(fogHeight, 1.0));
  return saturate(1.0 - exp(-viewDist * d * hFalloff));
}

float3 atmosphereFogColor(float3 sunToScene, float turbidity, float sunIntensity) {
  float3 L = normalize(-sunToScene);
  float day = smoothstep(-0.05, 0.2, L.y);
  float3 fog = lerp(float3(0.02, 0.025, 0.04), float3(0.55, 0.62, 0.72), day);
  fog = lerp(fog, float3(0.7, 0.35, 0.15), saturate(1.0 - L.y) * day * 0.5);
  return fog * (0.35 + sunIntensity * 0.04) * lerp(0.8, 1.2, saturate(turbidity * 0.1));
}

float cloudHash(float3 p) {
  p = frac(p * 0.3183099 + 0.1);
  p *= 17.0;
  return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float cloudNoise(float3 x) {
  float3 i = floor(x);
  float3 f = frac(x);
  f = f * f * (3.0 - 2.0 * f);
  return lerp(
      lerp(lerp(cloudHash(i + float3(0, 0, 0)), cloudHash(i + float3(1, 0, 0)), f.x),
           lerp(cloudHash(i + float3(0, 1, 0)), cloudHash(i + float3(1, 1, 0)), f.x), f.y),
      lerp(lerp(cloudHash(i + float3(0, 0, 1)), cloudHash(i + float3(1, 0, 1)), f.x),
           lerp(cloudHash(i + float3(0, 1, 1)), cloudHash(i + float3(1, 1, 1)), f.x), f.y),
      f.z);
}

float cloudFbm(float3 p) {
  float a = 0.5;
  float s = 0.0;
  float w = 0.0;
  [unroll]
  for (int i = 0; i < 5; ++i) {
    s += a * cloudNoise(p);
    w += a;
    p = p * 2.03 + float3(1.7, 9.2, 3.1);
    a *= 0.5;
  }
  return s / max(w, 1e-3);
}

float cloudDensityAt(float3 pos, float coverage, float densityScale, float cloudBottom, float cloudTop) {
  float h = saturate((pos.y - cloudBottom) / max(cloudTop - cloudBottom, 1.0));
  float hShape = saturate(1.0 - abs(h * 2.0 - 1.0));
  hShape = hShape * hShape * (3.0 - 2.0 * hShape);

  float3 p = pos * 0.0035;
  float n = cloudFbm(p);
  float detail = cloudFbm(p * 3.1 + 17.0);
  n = lerp(n, 1.0 - detail, 0.35);

  float c = saturate(coverage);
  float d = saturate(n - (1.0 - c)) / max(c, 0.08);
  d = saturate(d * hShape * densityScale);
  return d;
}

bool cloudIntersectSlab(float3 ro, float3 rd, float y0, float y1, out float tEnter, out float tExit) {
  tEnter = 0.0;
  tExit = 0.0;
  if (abs(rd.y) < 1e-5) {
    if (ro.y < y0 || ro.y > y1) {
      return false;
    }
    tEnter = 0.05;
    tExit = 4000.0;
    return true;
  }
  float t0 = (y0 - ro.y) / rd.y;
  float t1 = (y1 - ro.y) / rd.y;
  float tNear = min(t0, t1);
  float tFar = max(t0, t1);
  if (tFar < 0.0) {
    return false;
  }
  tEnter = max(tNear, 0.05);
  tExit = min(tFar, 6000.0);
  return tExit > tEnter;
}

#endif
