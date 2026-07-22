#ifndef TUCANO_BRUNETON_ATMOSPHERE_HLSL
#define TUCANO_BRUNETON_ATMOSPHERE_HLSL

// Runtime sampling for Bruneton Precomputed Atmospheric Scattering LUTs.
// Based on Eric Bruneton 2017: https://github.com/ebruneton/precomputed_atmospheric_scattering (BSD-3-Clause)
// Scattering atlas: Y packs r-layers (muSize * rSize).

#ifndef BRUNETON_SCAT_W
#define BRUNETON_SCAT_W 64
#define BRUNETON_SCAT_MU 64
#define BRUNETON_SCAT_R 16
#define BRUNETON_SCAT_MU_S 16
#define BRUNETON_SCAT_NU 4
#endif

float brunetonRayleighPhase(float nu) {
  return 3.0 / (16.0 * 3.14159265) * (1.0 + nu * nu);
}

float brunetonMiePhase(float g, float nu) {
  float g2 = g * g;
  float k = 3.0 / (8.0 * 3.14159265) * (1.0 - g2) / (2.0 + g2);
  return k * (1.0 + nu * nu) / pow(max(1.0 + g2 - 2.0 * g * nu, 1e-4), 1.5);
}

float3 brunetonWorldToPlanet(float3 worldPosMeters, float bottomRadiusKm) {
  float3 pKm = worldPosMeters * 0.001;
  return pKm + float3(0.0, bottomRadiusKm, 0.0);
}

float2 brunetonTransmittanceUv(float r, float mu, float bottom, float top) {
  float H = sqrt(max(top * top - bottom * bottom, 0.0));
  float rho = sqrt(max(r * r - bottom * bottom, 0.0));
  float discriminant = r * r * (mu * mu - 1.0) + top * top;
  float d = max(-r * mu + sqrt(max(discriminant, 0.0)), 0.0);
  float d_min = top - r;
  float d_max = rho + H;
  float x_mu = (d - d_min) / max(d_max - d_min, 1e-5);
  float x_r = rho / max(H, 1e-5);
  return float2(saturate(x_mu), saturate(x_r));
}

float3 brunetonSampleTransmittance(Texture2D transTex, SamplerState s, float r, float mu, float bottom,
                                   float top) {
  float2 uv = brunetonTransmittanceUv(r, mu, bottom, top);
  return transTex.SampleLevel(s, uv, 0).rgb;
}

float2 brunetonScatteringUv(float r, float mu, float mu_s, float nu, float bottom, float top, float layer,
                            float u_mu) {
  float u_mu_s;
  {
    float H = sqrt(max(top * top - bottom * bottom, 0.0));
    float rho = sqrt(max(r * r - bottom * bottom, 0.0));
    float d = max(-r * mu_s + sqrt(max(r * r * (mu_s * mu_s - 1.0) + top * top, 0.0)), 0.0);
    float d_min = top - r;
    float d_max = rho + H;
    u_mu_s = saturate((d - d_min) / max(d_max - d_min, 1e-5));
  }
  float u_nu = saturate(0.5 + 0.5 * nu);
  float u = (u_nu * float(BRUNETON_SCAT_NU - 1) + u_mu_s) / float(BRUNETON_SCAT_NU);
  float v = (layer * float(BRUNETON_SCAT_MU) + saturate(u_mu) * float(BRUNETON_SCAT_MU - 1) + 0.5) /
            float(BRUNETON_SCAT_MU * BRUNETON_SCAT_R);
  return float2(saturate(u), saturate(v));
}

float4 brunetonSampleScattering(Texture2D scatTex, SamplerState s, float r, float mu, float mu_s, float nu,
                                float bottom, float top) {
  float H = sqrt(max(top * top - bottom * bottom, 0.0));
  float rho = sqrt(max(r * r - bottom * bottom, 0.0));
  float u_r = saturate(rho / max(H, 1e-5));

  float rmu = r * mu;
  float disc = rmu * rmu - r * r + bottom * bottom;
  float u_mu;
  if (rmu < 0.0 && disc >= 0.0) {
    float d = -rmu - sqrt(max(disc, 0.0));
    u_mu = 0.5 - 0.5 * (d / max(rho, 1e-5));
  } else {
    float d = -rmu + sqrt(max(rmu * rmu - r * r + top * top, 0.0));
    u_mu = 0.5 + 0.5 * ((d - (top - r)) / max((rho + H) - (top - r), 1e-5));
  }
  u_mu = saturate(u_mu);

  float layerF = u_r * float(BRUNETON_SCAT_R - 1);
  float layer0 = floor(layerF);
  float layer1 = min(layer0 + 1.0, float(BRUNETON_SCAT_R - 1));
  float2 uv0 = brunetonScatteringUv(r, mu, mu_s, nu, bottom, top, layer0, u_mu);
  float2 uv1 = brunetonScatteringUv(r, mu, mu_s, nu, bottom, top, layer1, u_mu);
  float4 s0 = scatTex.SampleLevel(s, uv0, 0);
  float4 s1 = scatTex.SampleLevel(s, uv1, 0);
  return lerp(s0, s1, saturate(frac(layerF)));
}

// camera / view / sun in planet space (km), sunDir toward sun.
float3 brunetonGetSkyLuminance(Texture2D transTex, Texture2D scatTex, SamplerState s, float3 camera,
                               float3 viewRay, float3 sunDir, float bottom, float top, float mieG,
                               float exposure, out float3 transmittance) {
  float3 view = normalize(viewRay);
  float3 sun = normalize(sunDir);
  transmittance = float3(1, 1, 1);

  // Below horizon: dark ground tint (sky pass only)
  if (view.y < -0.02) {
    float day = smoothstep(-0.05, 0.2, sun.y);
    transmittance = float3(0, 0, 0);
    return float3(0.02, 0.025, 0.03) * (0.2 + day * 0.5) * exposure * 0.02;
  }

  float r = length(camera);
  r = clamp(r, bottom + 1e-3, top - 1e-3);
  float3 cam = camera * (r / max(length(camera), 1e-6));

  float mu = dot(cam, view) / r;
  float mu_s = dot(cam, sun) / r;
  float nu = dot(view, sun);
  mu = clamp(mu, -1.0, 1.0);
  mu_s = clamp(mu_s, -1.0, 1.0);
  nu = clamp(nu, -1.0, 1.0);

  bool intersectsGround = mu < 0.0 && (r * r * (mu * mu - 1.0) + bottom * bottom) >= 0.0;
  transmittance = intersectsGround ? float3(0, 0, 0) : brunetonSampleTransmittance(transTex, s, r, mu, bottom, top);

  float4 scat = brunetonSampleScattering(scatTex, s, r, mu, mu_s, nu, bottom, top);
  float3 rayleigh = max(scat.rgb, 0.0);
  float3 mie = max(scat.aaa, 0.0) * float3(1.0, 0.9, 0.75);

  float3 solar = float3(1.474, 1.8504, 1.91198);
  // Clamp Mie phase so the forward lobe near a low sun doesn't blow into a giant white disc.
  float miePh = min(brunetonMiePhase(mieG, nu), 6.0);
  float3 radiance = (rayleigh * brunetonRayleighPhase(nu) + mie * miePh) * solar;
  radiance = min(radiance, 8.0); // cap in-scatter energy before exposure

  // Small physical sun disk (transmittance reddens it near the horizon → golden look).
  float cosSunAngular = cos(0.0075);
  float sunDisk = smoothstep(cosSunAngular, cosSunAngular + 0.00015, nu);
  float3 sunColor = float3(0, 0, 0);
  if (!intersectsGround && sun.y > -0.05) {
    float3 sunT = brunetonSampleTransmittance(transTex, s, r, mu_s, bottom, top);
    sunColor = sunDisk * sunT * solar * 12.0;
  }

  // Physically-flavored fallback so the sky is never black; warms toward the sun near horizon.
  float day = smoothstep(-0.03, 0.18, sun.y);
  float lowSun = 1.0 - smoothstep(0.0, 0.35, sun.y); // 1 at horizon, 0 high
  float3 zenith = float3(0.12, 0.30, 0.68);
  float3 horiz = float3(0.55, 0.62, 0.80);
  // Golden band: warm ramp toward the sun azimuth near the horizon
  float sunProximity = pow(saturate(nu * 0.5 + 0.5), 4.0);
  float3 goldenHoriz = lerp(horiz, float3(1.0, 0.55, 0.22), lowSun * sunProximity);
  float3 fallback = lerp(zenith, goldenHoriz, pow(saturate(1.0 - view.y), 2.2)) * day;

  float lum = dot(radiance, float3(0.2126, 0.7152, 0.0722));
  float3 sky = radiance * max(exposure, 1.0);
  // Blend physical LUT with fallback (LUT alone is under-scaled here).
  sky = lerp(fallback, sky, saturate(lum * exposure * 6.0));
  sky = max(sky, fallback * 0.6);
  sky += sunColor; // sun disk on top (already exposure-independent scale)
  return max(sky, 0.0);
}

float3 brunetonAerialPerspective(Texture2D transTex, Texture2D scatTex, SamplerState s, float3 camera,
                                 float3 worldPoint, float3 sunDir, float3 inColor, float bottom, float top,
                                 float mieG, float exposure) {
  float3 view = worldPoint - camera;
  float distKm = length(view);
  // Near field (SkyLab props / ground): do not crush albedo — AE was blowing the plate white.
  if (distKm < 0.5) { // < 500 m
    return inColor;
  }
  view /= max(distKm, 1e-6);
  float3 sun = normalize(sunDir);
  float r = clamp(length(camera), bottom + 1e-3, top - 1e-3);
  float3 cam = camera * (r / max(length(camera), 1e-6));
  float mu = clamp(dot(cam, view) / r, -1.0, 1.0);
  float mu_s = clamp(dot(cam, sun) / r, -1.0, 1.0);
  float nu = clamp(dot(view, sun), -1.0, 1.0);

  float3 tView = brunetonSampleTransmittance(transTex, s, r, abs(mu), bottom, top);
  // Soft optical fade with distance (km)
  float3 transmittance = exp(-distKm * (float3(0.005, 0.01, 0.025) + (1.0 - tView) * 0.02));

  float4 scat = brunetonSampleScattering(scatTex, s, r, mu, mu_s, nu, bottom, top);
  float3 solar = float3(1.474, 1.8504, 1.91198);
  float3 inscatter =
      (max(scat.rgb, 0.0) * brunetonRayleighPhase(nu) + max(scat.aaa, 0.0) * brunetonMiePhase(mieG, nu)) * solar;
  float fade = saturate((distKm - 0.5) / 20.0);
  inscatter *= fade * max(exposure, 1.0) * 0.25;
  return inColor * transmittance + inscatter;
}

#endif
