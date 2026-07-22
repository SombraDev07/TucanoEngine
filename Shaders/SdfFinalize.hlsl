// Finalize a world-SDF cascade on the GPU: read the JFA seed volume + per-voxel aux (albedo/inside),
// compute signed distance + an SH1 irradiance approximation, and write into the 2D atlas. The SDF
// gradient (normal) is taken directly from the voxel→seed direction, so no neighbour re-sample.

cbuffer FinalizeCB : register(b0) { // root 32-bit constants (≤16)
  float4 uSunDirIntensity; // xyz = sun→scene, w = intensity
  uint uRes;               // cascade resolution (64)
  uint uCascade;           // atlas column
  float uVoxelSize;
  float uExt;              // cascade extent
};

Texture3D<uint> SeedFinal : register(t0);
Texture3D<float4> AuxVol : register(t1); // rgb = albedo, a = inside mask
RWTexture2D<float> SdfOut : register(u0);
RWTexture2D<float4> ShOut : register(u1);

static const uint kEmpty = 0xFFFFFFFFu;

[numthreads(4, 4, 4)]
void CSMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= uRes || id.y >= uRes || id.z >= uRes) {
    return;
  }

  const uint seed = SeedFinal.Load(int4(id, 0));
  const float4 aux = AuxVol.Load(int4(id, 0));
  const float inside = aux.a;

  float dist;
  float3 normal;
  if (seed == kEmpty) {
    dist = uExt;
    normal = float3(0, 1, 0);
  } else {
    const uint sx = seed % uRes;
    const uint sy = (seed / uRes) % uRes;
    const uint sz = seed / (uRes * uRes);
    const float3 toVoxel = float3(id) - float3(float(sx), float(sy), float(sz));
    dist = length(toVoxel) * uVoxelSize;
    normal = (length(toVoxel) > 1e-4f) ? normalize(toVoxel) : float3(0, 1, 0);
  }
  if (inside > 0.5f) {
    dist = -max(dist, uVoxelSize * 0.25f);
    normal = -normal;
  }

  // SH1 irradiance (matches the previous CPU path).
  const float3 L = normalize(uSunDirIntensity.xyz);
  const float sunI = max(uSunDirIntensity.w, 0.0f);
  const float3 albedo = aux.rgb;
  const float ndotl = max(0.0f, dot(normal, -L));
  const float sky = 0.06f + 0.04f * max(0.0f, normal.y);
  const float L0 = sky + sunI * 0.05f * (inside > 0.5f ? 0.25f : 1.0f);
  const float3 L1 = (-L) * (sunI * 0.18f * (0.35f + 0.65f * ndotl)) * albedo;

  const uint2 atlas = uint2(uCascade * uRes + id.x, id.y + id.z * uRes);
  SdfOut[atlas] = dist;
  ShOut[atlas] = float4(L1, L0 * (0.7f + 0.3f * (albedo.x + albedo.y + albedo.z) / 3.0f));
}
