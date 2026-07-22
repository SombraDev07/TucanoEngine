// DXR 1.1 Ray Query — short contact rays toward the sun; darkens HDR in-place style (UAV = dst).

cbuffer RTCB : register(b1) {
  float4x4 invViewProj;
  float4 cameraPos;
  float4 screenSize;
  float4 sunDirIntensity;
  float4 params; // x=rayLen, y=intensity, z=bias, w=unused
  uint4 texIds;  // depth, hdrSrc, _, _
};

RaytracingAccelerationStructure SceneAS : register(t0, space1);
Texture2D bindlessHeap[] : register(t0, space0);
RWTexture2D<float4> outHdr : register(u0);
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
  Texture2D hdrTex = bindlessHeap[NonUniformResourceIndex(texIds.y)];
  float depth = depthTex.SampleLevel(linearSamp, uv, 0).r;
  float3 hdr = hdrTex.SampleLevel(linearSamp, uv, 0).rgb;
  if (depth >= 0.9999) {
    outHdr[dtid.xy] = float4(hdr, 1);
    return;
  }

  float3 worldPos = reconstructWorld(uv, depth);
  float3 rayDir = normalize(-sunDirIntensity.xyz);
  float bias = max(params.z, 0.001);
  float rayLen = max(params.x, 0.05);

  RayDesc ray;
  ray.Origin = worldPos + rayDir * bias;
  ray.Direction = rayDir;
  ray.TMin = 0.0;
  ray.TMax = rayLen;

  RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES |
           RAY_FLAG_CULL_NON_OPAQUE>
      q;
  q.TraceRayInline(SceneAS, RAY_FLAG_NONE, 0xFF, ray);
  q.Proceed();

  float occlusion = 0.0;
  if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
    float t = q.CommittedRayT();
    occlusion = saturate(1.0 - t / rayLen);
  }
  hdr *= saturate(1.0 - occlusion * params.y);
  outHdr[dtid.xy] = float4(hdr, 1);
}
