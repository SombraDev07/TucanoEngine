// Build Hi-Z pyramid: mip0 copy from depthColor; subsequent mips = max of 2x2.
cbuffer HiZCB : register(b1) {
  uint2 srcSize;
  uint2 dstSize;
  uint mip;
  uint3 pad;
};

Texture2D<float> SrcDepth : register(t0);
RWTexture2D<float> DstDepth : register(u0);

[numthreads(8, 8, 1)]
void CSCopy(uint3 id : SV_DispatchThreadID) {
  if (id.x >= dstSize.x || id.y >= dstSize.y) {
    return;
  }
  DstDepth[id.xy] = SrcDepth.Load(int3(id.xy, 0));
}

[numthreads(8, 8, 1)]
void CSReduce(uint3 id : SV_DispatchThreadID) {
  if (id.x >= dstSize.x || id.y >= dstSize.y) {
    return;
  }
  uint2 s = id.xy * 2;
  float d0 = SrcDepth.Load(int3(s, 0));
  float d1 = SrcDepth.Load(int3(min(s + uint2(1, 0), srcSize - 1), 0));
  float d2 = SrcDepth.Load(int3(min(s + uint2(0, 1), srcSize - 1), 0));
  float d3 = SrcDepth.Load(int3(min(s + uint2(1, 1), srcSize - 1), 0));
  // Reverse-Z aware: farther = larger depth with zero-to-one LH; occluder = min (closer to cam = smaller z)
  DstDepth[id.xy] = min(min(d0, d1), min(d2, d3));
}
