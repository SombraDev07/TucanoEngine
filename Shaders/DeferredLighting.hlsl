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
  float4 flags; // shadows, ibl, ao, bloom
  float4 screenSize;
  float4 iblParams; // maxMip, exposure, unused, unused
};

cbuffer LightCB : register(b2) {
  uint lightCount;
  uint3 _pad0;
  float4 lightPosType[16];       // xyz, w=type (1 point, 2 spot)
  float4 lightColorIntensity[16];
  float4 lightRangeParams[16];   // range, innerCos, outerCos, _
  float4 lightDirection[16];
};

Texture2D albedoTex : register(t0);
Texture2D normalTex : register(t1);
Texture2D ormTex : register(t2);
Texture2D emissiveTex : register(t3);
Texture2D depthTex : register(t4);
Texture2D shadowMap : register(t5);
Texture2D brdfLUT : register(t6);
Texture2D irradianceTex : register(t7);
Texture2D prefilteredTex : register(t8);
Texture2D aoTex : register(t9);
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

float sampleShadow(float3 worldPos, float viewDepth) {
  int cascade = 0;
  if (viewDepth > cascadeSplits.x) cascade = 1;
  if (viewDepth > cascadeSplits.y) cascade = 2;
  if (viewDepth > cascadeSplits.z) cascade = 3;

  float4 lightClip = mul(lightViewProj[cascade], float4(worldPos, 1.0));
  float3 ndc = lightClip.xyz / lightClip.w;
  float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
  float2 atlasOffset = float2(cascade % 2, cascade / 2) * 0.5;
  uv = atlasOffset + uv * 0.5;
  // Inset to avoid atlas seam flicker between cascades
  float2 lo = atlasOffset + 0.0015;
  float2 hi = atlasOffset + 0.5 - 0.0015;
  if (any(uv < lo) || any(uv > hi)) {
    return 1.0;
  }

  // Fixed-kernel PCF — variable PCSS radius caused wild flicker in shadows
  // Optional ESM-style soft compare when flags.w > 0.5 (exponential shadow maps)
  float compare = ndc.z - (0.0012 + 0.0008 * cascade);
  float2 texel = 1.0 / 2048.0;
  float shadow = 0.0;
  [unroll] for (int x = -2; x <= 2; ++x) {
    [unroll] for (int y = -2; y <= 2; ++y) {
      float d = shadowMap.Sample(shadowSamp, uv + float2(x, y) * texel).r;
      if (flags.w > 0.5) {
        float esm = saturate(exp(-80.0 * (compare - d)));
        shadow += esm;
      } else {
        shadow += (compare <= d) ? 1.0 : 0.0;
      }
    }
  }
  return shadow / 25.0;
}

float3 sampleIrradiance(float3 n) {
  return irradianceTex.SampleLevel(samp, dirToLatLong(n), 0).rgb;
}

float3 samplePrefiltered(float3 r, float roughness) {
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
  // Attenuate base by coat Fresnel energy
  lo *= (1.0 - clearcoat * Fc);
  lo += (coatSpec * clearcoat) * radiance * nDotL * shadow;
  return lo;
}

float4 PSMain(VSOut input) : SV_Target {
  float4 albedoA = albedoTex.Sample(samp, input.uv);
  float3 albedo = albedoA.rgb;
  float3 nSample = normalTex.Sample(samp, input.uv).xyz;
  float depth = depthTex.Sample(samp, input.uv).r;
  if (depth <= 0.0001) {
    // Environment background
    float2 ndc = input.uv * float2(2, -2) + float2(-1, 1);
    float4 clip = float4(ndc, 1.0, 1.0);
    float4 w = mul(invViewProj, clip);
    float3 dir = normalize(w.xyz / max(w.w, 1e-6) - cameraPos.xyz);
    float3 env = samplePrefiltered(dir, 0.0) * iblParams.y;
    return float4(env, 1.0);
  }

  float3 n = normalize(nSample * 2.0 - 1.0);
  float4 orm = ormTex.Sample(samp, input.uv);
  float4 emissiveSample = emissiveTex.Sample(samp, input.uv);
  float3 emissive = emissiveSample.rgb;
  float clearcoat = emissiveSample.a;

  float3 worldPos = reconstructWorldPos(input.uv, depth);
  float3 v = normalize(cameraPos.xyz - worldPos);
  float ao = orm.r;
  float roughness = max(orm.g, 0.04);
  float metallic = orm.b;
  if (flags.z > 0.5) {
    ao *= aoTex.Sample(samp, input.uv).r;
  }

  // reflectance from GBuffer orm.a (dielectric F0), metals use albedo
  float dielectricF0 = max(orm.a, 0.02);
  float3 f0 = lerp(dielectricF0.xxx, albedo, metallic);
  float nDotV = max(dot(n, v), 0.0);
  float3 lo = 0;
  float clearcoatRoughness = lerp(0.05, 0.5, roughness);

  // Directional sun
  {
    float3 l = normalize(-sunDirectionIntensity.xyz);
    float3 radiance = sunColor.rgb * sunDirectionIntensity.w;
    float shadow = 1.0;
    if (flags.x > 0.5) {
      shadow = sampleShadow(worldPos, length(cameraPos.xyz - worldPos));
    }
    lo += evaluateDirectBRDF(albedo, f0, metallic, roughness, n, v, l, radiance) * shadow;
    lo = evaluateClearcoat(lo, clearcoat, clearcoatRoughness, n, v, l, radiance, shadow);
  }

  // Local lights (point + spot)
  [loop] for (uint i = 0; i < lightCount; ++i) {
    float3 lpos = lightPosType[i].xyz;
    float type = lightPosType[i].w;
    float3 toLight = lpos - worldPos;
    float dist = length(toLight);
    float3 l = toLight / max(dist, 1e-4);
    float range = lightRangeParams[i].x;
    float atten = saturate(1.0 - (dist / max(range, 1e-3)));
    atten *= atten;

    if (type > 1.5) {
      // Spot
      float3 spotDir = normalize(lightDirection[i].xyz);
      float cosTheta = dot(-l, spotDir);
      float inner = lightRangeParams[i].y;
      float outer = lightRangeParams[i].z;
      float spot = saturate((cosTheta - outer) / max(inner - outer, 1e-4));
      atten *= spot * spot;
    }

    float3 radiance = lightColorIntensity[i].rgb * lightColorIntensity[i].a * atten;
    lo += evaluateDirectBRDF(albedo, f0, metallic, roughness, n, v, l, radiance);
    lo = evaluateClearcoat(lo, clearcoat, clearcoatRoughness, n, v, l, radiance, 1.0);
  }

  // Image-based lighting (split-sum)
  float3 ambient = ambientColor.rgb * albedo * ao;
  if (flags.y > 0.5) {
    float3 r = reflect(-v, n);
    float2 dfg = brdfLUT.SampleLevel(samp, float2(nDotV, roughness), 0).rg;
    float3 F = fresnelSchlickRoughness(nDotV, f0, roughness);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 irradiance = sampleIrradiance(n);
    float3 diffuse = kD * irradiance * albedo;
    float3 prefiltered = samplePrefiltered(r, roughness);
    float3 specular = prefiltered * (F * dfg.x + dfg.y);
    specular *= getEnergyCompensation(f0, dfg);
    float sao = specularAO(ao, nDotV, roughness);
    ambient = (diffuse * ao + specular * sao) * iblParams.y;

    if (clearcoat > 1e-4) {
      float2 dfgC = brdfLUT.SampleLevel(samp, float2(nDotV, clearcoatRoughness), 0).rg;
      float Fc = fresnelSchlickRoughness(nDotV, 0.04, clearcoatRoughness).x;
      float3 coat = samplePrefiltered(r, clearcoatRoughness) * (0.04 * dfgC.x + dfgC.y);
      ambient = ambient * (1.0 - clearcoat * Fc) + coat * clearcoat * sao;
    }
  }

  return float4(lo + ambient + emissive, 1.0);
}
