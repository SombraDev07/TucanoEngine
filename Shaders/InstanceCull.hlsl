// WM-6: GPU-driven instance cloud culling.
//
// One thread per instance. Each tests its bounding sphere against the six frustum planes and the
// distance cap, and — if visible — appends its index to a compact list AND increments the instance
// count of a DrawIndexedInstanced argument block. The result is a single ExecuteIndirect that draws
// exactly the visible instances of one mesh, however many thousands the cell holds. That is the
// answer to the 4096-draw ceiling that motivated WM-5 and this phase: a field of grass or rocks
// costs ONE draw, not one per blade.
//
// Like CellCull.hlsl (WM-4) this is a transliteration of a CPU reference — cullInstancesCPU in
// src/World/InstanceCloud.cpp — so the two can be run on identical input and compared. The only
// thing that can diverge is the sphere-plane test here; a parity gate asserts it does not.
//
// The plane convention is the engine's: inward unit normals, zero-to-one depth (D3D), matching
// extractFrustum(). A sphere is outside plane i only when its centre sits behind the plane by more
// than its radius — the sphere analogue of CellCull's positive-vertex box test.

struct InstanceGpu {
  float4x4 transform; // world matrix, read by the draw VS, not by this cull
  float3 center;      // world-space bounding-sphere centre
  float radius;       // world-space bounding-sphere radius (mesh radius × max scale)
  uint materialId;
  uint lodMask;       // bit i set => this instance may render at LOD i
  uint2 _pad;
};

// D3D12_DRAW_INDEXED_ARGUMENTS, element 0 patched in place: instanceCount is grown atomically.
struct DrawArgs {
  uint indexCountPerInstance;
  uint instanceCount;
  uint startIndex;
  uint baseVertex;
  uint startInstance;
};

// b0 is the root 32-bit-constants slot in the shared compute root signature, so the constant buffer
// binds at b1, exactly like CellCull.hlsl / MeshletCull.hlsl.
cbuffer InstanceCullCB : register(b1) {
  float4 frustumPlanes[6]; // xyz = inward unit normal, w = signed distance
  float4 observer;         // xyz = LOD / distance reference point
  uint instanceCount;
  float lodStep;           // metres per LOD band; 0 disables LOD (all visible => LOD 0)
  uint maxLod;
  float maxDistance;       // 0 disables the far cap
};

StructuredBuffer<InstanceGpu> Instances : register(t0);
RWStructuredBuffer<uint> VisibleInstances : register(u0); // compacted original indices
RWStructuredBuffer<DrawArgs> Args : register(u1);         // element 0, instanceCount incremented

bool sphereInFrustum(float3 c, float r) {
  [unroll] for (int i = 0; i < 6; ++i) {
    if (dot(frustumPlanes[i].xyz, c) + frustumPlanes[i].w < -r) return false;
  }
  return true;
}

uint selectLod(float dist) {
  if (lodStep <= 0.0) return 0u;
  uint lod = uint(max(dist, 0.0) / lodStep);
  return min(lod, maxLod);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
  uint i = tid.x;
  if (i >= instanceCount) return;

  InstanceGpu inst = Instances[i];
  float dist = distance(inst.center, observer.xyz);

  if (maxDistance > 0.0 && dist > maxDistance) return;
  if (!sphereInFrustum(inst.center, inst.radius)) return;

  // An instance whose lodMask forbids its distance band is skipped. Default mask (~0u) never is,
  // so a cloud that does not care about LOD masks behaves exactly as pure frustum culling.
  uint lod = selectLod(dist);
  if ((inst.lodMask & (1u << lod)) == 0u) return;

  uint slot;
  InterlockedAdd(Args[0].instanceCount, 1u, slot);
  VisibleInstances[slot] = i;
}
