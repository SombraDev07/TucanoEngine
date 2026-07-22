// GPU meshlet frustum + cone + Hi-Z occlusion → sparse DrawIndexedArgs (instanceCount 0/1).
struct MeshletGPU {
  float3 center;
  float radius;
  float3 coneAxis;
  float coneCutoff;
  uint indexOffset;
  uint indexCount;
  uint materialIndex;
  uint pad1;
};

struct DrawIndexedArgs {
  uint indexCountPerInstance;
  uint instanceCount;
  uint startIndexLocation;
  int baseVertexLocation;
  uint startInstanceLocation;
};

cbuffer CullCB : register(b1) {
  float4x4 viewProj;
  float4x4 world;
  float4 camPos;
  uint meshletCount;
  uint argsOffset;
  uint enableHiZ;
  uint hizMipCount;
  float2 screenSize;
  float2 pad2;
};

StructuredBuffer<MeshletGPU> Meshlets : register(t0);
Texture2D<float> HiZ : register(t1);
RWStructuredBuffer<DrawIndexedArgs> OutArgs : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
  uint i = id.x;
  if (i >= meshletCount) {
    return;
  }

  MeshletGPU m = Meshlets[i];
  float3 scale = float3(length(world[0].xyz), length(world[1].xyz), length(world[2].xyz));
  float radius = m.radius * max(scale.x, max(scale.y, scale.z));
  float4 cw = mul(world, float4(m.center, 1.0));
  float4 clip = mul(viewProj, cw);

  uint visible = 0;
  float3 ndc = 0;
  float nearestZ = 0;
  if (clip.w > 0.0) {
    float invW = rcp(clip.w);
    ndc = clip.xyz * invW;
    float padNdc = (radius / max(clip.w, 1e-3)) * 2.0 + 0.15;
    nearestZ = ndc.z - padNdc * 0.5;
    if (ndc.x >= -1.0 - padNdc && ndc.x <= 1.0 + padNdc && ndc.y >= -1.0 - padNdc &&
        ndc.y <= 1.0 + padNdc && ndc.z >= -padNdc && ndc.z <= 1.0 + padNdc) {
      visible = 1;
      if (m.coneCutoff >= 0.0) {
        float3 axisWorld = normalize(mul((float3x3)world, m.coneAxis));
        float3 dir = normalize(cw.xyz - camPos.xyz);
        if (-dot(dir, axisWorld) > m.coneCutoff) {
          visible = 0;
        }
      }
    }
  }

  if (visible && enableHiZ != 0) {
    float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
    float radNdc = (radius / max(clip.w, 1e-3));
    float2 ext = float2(radNdc, radNdc) * 0.55;
    float2 uvMin = saturate(uv - ext);
    float2 uvMax = saturate(uv + ext);
    uint2 dim;
    HiZ.GetDimensions(dim.x, dim.y);
    uint2 p0 = uint2(uvMin * float2(dim));
    uint2 p1 = uint2(uvMax * float2(dim));
    p1 = max(p1, p0 + 1);
    float zOcc = 1.0;
    [unroll] for (uint yy = 0; yy < 2; ++yy) {
      [unroll] for (uint xx = 0; xx < 2; ++xx) {
        uint2 p = uint2(lerp(float2(p0), float2(min(p1, dim - 1)), float2(xx, yy)));
        zOcc = min(zOcc, HiZ.Load(int3(p, 0)));
      }
    }
    if (zOcc > 1e-5 && zOcc < nearestZ - 1e-3) {
      visible = 0;
    }
  }

  DrawIndexedArgs args;
  args.indexCountPerInstance = m.indexCount;
  args.instanceCount = visible;
  args.startIndexLocation = m.indexOffset;
  args.baseVertexLocation = 0;
  args.startInstanceLocation = argsOffset + i;
  OutArgs[argsOffset + i] = args;
}
