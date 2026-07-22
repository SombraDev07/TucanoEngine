// Compact sparse DrawIndexedArgs (instanceCount 0/1) → dense list + atomic counter.
struct DrawIndexedArgs {
  uint indexCountPerInstance;
  uint instanceCount;
  uint startIndexLocation;
  int baseVertexLocation;
  uint startInstanceLocation;
};

cbuffer CompactCB : register(b1) {
  uint srcCount;
  uint srcOffset;
  uint clearOnly;
  uint pad;
};

StructuredBuffer<DrawIndexedArgs> SrcArgs : register(t0);
RWStructuredBuffer<DrawIndexedArgs> DstArgs : register(u0);
RWStructuredBuffer<uint> Counter : register(u1); // single uint at [0]

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
  if (clearOnly != 0) {
    if (id.x == 0) Counter[0] = 0;
    return;
  }
  uint i = id.x;
  if (i >= srcCount) {
    return;
  }
  DrawIndexedArgs a = SrcArgs[srcOffset + i];
  if (a.instanceCount == 0) {
    return;
  }
  uint dst;
  InterlockedAdd(Counter[0], 1, dst);
  a.instanceCount = 1;
  DstArgs[dst] = a;
}
