#ifndef TUCANO_COMMON_HLSL
#define TUCANO_COMMON_HLSL

static const float PI = 3.14159265359;
static const float INV_PI = 0.31830988618;

float3 fresnelSchlick(float cosTheta, float3 f0) {
  return f0 + (1.0 - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 fresnelSchlickRoughness(float cosTheta, float3 f0, float roughness) {
  return f0 + (max((float3)(1.0 - roughness), f0) - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float distributionGGX(float3 n, float3 h, float roughness) {
  float a = max(roughness * roughness, 1e-4);
  float a2 = a * a;
  float nDotH = max(dot(n, h), 0.0);
  float nDotH2 = nDotH * nDotH;
  float denom = (nDotH2 * (a2 - 1.0) + 1.0);
  return a2 / (PI * denom * denom + 1e-7);
}

float geometrySchlickGGX(float nDotV, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;
  return nDotV / (nDotV * (1.0 - k) + k + 1e-7);
}

float geometrySmith(float3 n, float3 v, float3 l, float roughness) {
  float nDotV = max(dot(n, v), 0.0);
  float nDotL = max(dot(n, l), 0.0);
  return geometrySchlickGGX(nDotV, roughness) * geometrySchlickGGX(nDotL, roughness);
}

// Burley diffuse (Disney)
float3 diffuseBurley(float3 albedo, float roughness, float nDotV, float nDotL, float lDotH) {
  float fd90 = 0.5 + 2.0 * lDotH * lDotH * roughness;
  float lightScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - nDotL, 5.0);
  float viewScatter = 1.0 + (fd90 - 1.0) * pow(1.0 - nDotV, 5.0);
  return albedo * INV_PI * lightScatter * viewScatter;
}

// Multi-scatter energy compensation (Filament / Google approx)
float3 getEnergyCompensation(float3 f0, float2 dfg) {
  return 1.0 + f0 * (1.0 / max(dfg.x, 1e-4) - 1.0);
}

float specularAO(float ao, float nDotV, float roughness) {
  return saturate(pow(nDotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

float3 evaluateDirectBRDF(float3 albedo, float3 f0, float metallic, float roughness, float3 n, float3 v,
                          float3 l, float3 radiance) {
  float3 h = normalize(v + l);
  float nDotL = max(dot(n, l), 0.0);
  float nDotV = max(dot(n, v), 0.0);
  float lDotH = max(dot(l, h), 0.0);
  if (nDotL <= 0.0) {
    return 0;
  }

  float NDF = distributionGGX(n, h, roughness);
  float G = geometrySmith(n, v, l, roughness);
  float3 F = fresnelSchlick(lDotH, f0);
  float3 spec = (NDF * G * F) / max(4.0 * nDotV * nDotL, 1e-4);
  float3 kD = (1.0 - F) * (1.0 - metallic);
  float3 diffuse = diffuseBurley(albedo, roughness, nDotV, nDotL, lDotH);
  return (kD * diffuse + spec) * radiance * nDotL;
}

float2 dirToLatLong(float3 d) {
  d = normalize(d);
  float u = atan2(d.z, d.x) * INV_PI * 0.5 + 0.5;
  float v = asin(clamp(d.y, -1.0, 1.0)) * INV_PI + 0.5;
  return float2(u, 1.0 - v);
}

float3 acesTonemap(float3 x) {
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

#endif
