// DXR 1.1 Ray Query — hard shadow mask (half-res), sun direction only.

cbuffer RTCB : register(b1) {
  float4x4 invViewProj;
  float4 cameraPos;
  float4 screenSize;          // w, h, 1/w, 1/h of full-res
  float4 sunDirIntensity;     // xyz = light travel dir (from sun toward scene), w = intensity
  float4 params;              // x = bias, y = tmax, z = halfResScale, w = unused
  uint4 texIds;               // depth, normal, _, _
};

RaytracingAccelerationStructure SceneAS : register(t0, space1);
Texture2D bindlessHeap[] : register(t0, space0);
RWTexture2D<float> outShadow : register(u0);
SamplerState linearSamp : register(s0);

float3 reconstructWorld(float2 uv, float depth) {
  float4 clip = float4(uv * float2(2, -2) + float2(-1, 1), depth, 1);
  float4 world = mul(invViewProj, clip);
  return world.xyz / max(world.w, 1e-6);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
  uint2 outSize;
  outShadow.GetDimensions(outSize.x, outSize.y);
  if (any(dtid.xy >= outSize)) {
    return;
  }

  float2 uv = (float2(dtid.xy) + 0.5) * float2(screenSize.z, screenSize.w) * max(params.z, 1.0);
  // half-res: params.z = 2 → uv covers full screen
  uv = (float2(dtid.xy) + 0.5) / float2(outSize);

  Texture2D depthTex = bindlessHeap[NonUniformResourceIndex(texIds.x)];
  Texture2D normalTex = bindlessHeap[NonUniformResourceIndex(texIds.y)];
  float depth = depthTex.SampleLevel(linearSamp, uv, 0).r;
  if (depth >= 0.9999) {
    outShadow[dtid.xy] = 1.0;
    return;
  }

  float3 n = normalize(normalTex.SampleLevel(linearSamp, uv, 0).xyz * 2.0 - 1.0);
  float3 worldPos = reconstructWorld(uv, depth);
  float3 L = normalize(-sunDirIntensity.xyz);
  if (dot(n, L) <= 0.0) {
    outShadow[dtid.xy] = 0.0;
    return;
  }

  float bias = max(params.x, 0.001);
  float3 origin = worldPos + n * bias;
  float tmax = max(params.y, 1.0);

  RayDesc ray;
  ray.Origin = origin;
  ray.Direction = L;
  ray.TMin = 0.0;
  ray.TMax = tmax;

  RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
           RAY_FLAG_CULL_NON_OPAQUE>
      q;
  q.TraceRayInline(SceneAS, RAY_FLAG_NONE, 0xFF, ray);
  q.Proceed();

  float shadow = (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) ? 0.0 : 1.0;
  outShadow[dtid.xy] = shadow;
}
