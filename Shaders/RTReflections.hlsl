// DXR 1.1 Ray Query — 1-bounce specular reflections into SSR buffer (rgb + alpha confidence).
// Energy is kept conservative: no sunIntensity blow-up, low confidence (no denoiser yet).

cbuffer RTCB : register(b1) {
  float4x4 invViewProj;
  float4 cameraPos;
  float4 screenSize;
  float4 sunDirIntensity;
  float4 params; // x=bias, y=tmax, z=roughCutoff, w=skyTint strength
  uint4 texIds;  // depth, normal, orm, albedo
  float4 skyTint;
};

RaytracingAccelerationStructure SceneAS : register(t0, space1);
Texture2D bindlessHeap[] : register(t0, space0);
RWTexture2D<float4> outRefl : register(u0);
SamplerState linearSamp : register(s0);

float3 reconstructWorld(float2 uv, float depth) {
  float4 clip = float4(uv * float2(2, -2) + float2(-1, 1), depth, 1);
  float4 world = mul(invViewProj, clip);
  return world.xyz / max(world.w, 1e-6);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
  uint2 size = uint2(screenSize.xy);
  if (any(dtid.xy >= size)) {
    return;
  }
  float2 uv = (float2(dtid.xy) + 0.5) * float2(screenSize.z, screenSize.w);

  Texture2D depthTex = bindlessHeap[NonUniformResourceIndex(texIds.x)];
  Texture2D normalTex = bindlessHeap[NonUniformResourceIndex(texIds.y)];
  Texture2D ormTex = bindlessHeap[NonUniformResourceIndex(texIds.z)];
  Texture2D albedoTex = bindlessHeap[NonUniformResourceIndex(texIds.w)];

  float depth = depthTex.SampleLevel(linearSamp, uv, 0).r;
  if (depth <= 0.0001 || depth >= 0.9999) {
    outRefl[dtid.xy] = 0;
    return;
  }

  float4 orm = ormTex.SampleLevel(linearSamp, uv, 0);
  float roughness = orm.g;
  float metallic = orm.b;
  float roughCut = max(params.z, 0.05);
  // Only glossy / metal — stone Sponza stays on IBL (avoids firefly carpet).
  if (roughness > roughCut && metallic < 0.35) {
    outRefl[dtid.xy] = 0;
    return;
  }

  float3 n = normalize(normalTex.SampleLevel(linearSamp, uv, 0).xyz * 2.0 - 1.0);
  float3 worldPos = reconstructWorld(uv, depth);
  float3 V = normalize(cameraPos.xyz - worldPos);
  float3 R = reflect(-V, n);

  float bias = max(params.x, 0.002);
  RayDesc ray;
  ray.Origin = worldPos + n * bias;
  ray.Direction = R;
  ray.TMin = 0.0;
  ray.TMax = max(params.y, 1.0);

  RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_NON_OPAQUE> q;
  q.TraceRayInline(SceneAS, RAY_FLAG_NONE, 0xFF, ray);
  q.Proceed();

  if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
    // Miss: do not inject sky sparkles — Compose/IBL already handle environment.
    outRefl[dtid.xy] = 0;
    return;
  }

  float front = q.CommittedTriangleFrontFace() ? 1.0 : 0.2;
  // Normalize sun: intensity can be 4–8; never multiply raw into specular buffer.
  float sunN = saturate(sunDirIntensity.w * 0.12);
  float ndl = saturate(0.2 + 0.55 * front * max(normalize(-sunDirIntensity.xyz).y, 0.0));
  float3 alb = albedoTex.SampleLevel(linearSamp, uv, 0).rgb;
  float3 hitCol = lerp(float3(0.35, 0.33, 0.30), alb, 0.4) * (0.22 + ndl * sunN);

  float t = q.CommittedRayT();
  hitCol *= rcp(1.0 + t * 0.06);
  hitCol = min(hitCol, 0.85.xxx);

  float conf = saturate(1.0 - roughness * 1.6) * lerp(0.25, 0.55, metallic);
  conf *= saturate(1.0 - t / max(params.y, 1.0));
  conf *= 0.4;
  outRefl[dtid.xy] = float4(hitCol, conf);
}
