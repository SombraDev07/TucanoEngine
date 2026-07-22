#include "Common.hlsl"

// Convert cube-face atlas (6 tiles in a row) → lat-long strip for one probe.
cbuffer ProbeConvertCB : register(b1) {
  uint probeIndex;
  uint faceSize;
  uint latWidth;
  uint latHeight;
  uint maxProbes;
  uint faceAtlasId;
  uint _pad0;
  uint _pad1;
  float4 skyTint; // fallback sky when face clear/empty
};

Texture2D bindlessHeap[] : register(t0, space0);
SamplerState samp : register(s0);
RWTexture2D<float4> atlasOut : register(u0);

float3 sampleCubeFace(float3 dir) {
  float3 a = abs(dir);
  float2 uv;
  uint face;
  if (a.x >= a.y && a.x >= a.z) {
    face = dir.x > 0 ? 0 : 1;
    float sc = dir.x > 0 ? -dir.z : dir.z;
    float tc = -dir.y;
    uv = float2(sc, tc) / max(a.x, 1e-5) * 0.5 + 0.5;
  } else if (a.y >= a.x && a.y >= a.z) {
    face = dir.y > 0 ? 2 : 3;
    float sc = dir.x;
    float tc = dir.y > 0 ? dir.z : -dir.z;
    uv = float2(sc, tc) / max(a.y, 1e-5) * 0.5 + 0.5;
  } else {
    face = dir.z > 0 ? 4 : 5;
    float sc = dir.z > 0 ? dir.x : -dir.x;
    float tc = -dir.y;
    uv = float2(sc, tc) / max(a.z, 1e-5) * 0.5 + 0.5;
  }
  uv = saturate(uv);
  float2 atlasUV = float2((float(face) + uv.x) / 6.0, uv.y);
  Texture2D faces = bindlessHeap[NonUniformResourceIndex(faceAtlasId)];
  float3 c = faces.SampleLevel(samp, atlasUV, 0).rgb;
  // Empty/cleared sky pixels (~clear color)
  if (dot(c, 1) < 1e-4) {
    float up = saturate(dir.y * 0.5 + 0.5);
    c = lerp(skyTint.rgb * 0.35, skyTint.rgb, up);
  }
  return c;
}

float3 latLongToDir(float u, float v) {
  float theta = (u * 2.0 - 1.0) * PI;
  float phi = (0.5 - v) * PI;
  float cp = cos(phi);
  return normalize(float3(cp * cos(theta), sin(phi), cp * sin(theta)));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= latWidth || id.y >= latHeight) {
    return;
  }
  float u = (float(id.x) + 0.5) / float(latWidth);
  float v = (float(id.y) + 0.5) / float(latHeight);
  float3 dir = latLongToDir(u, v);
  float3 col = sampleCubeFace(dir);
  uint dstX = probeIndex * latWidth + id.x;
  atlasOut[uint2(dstX, id.y)] = float4(col, 1);
}
