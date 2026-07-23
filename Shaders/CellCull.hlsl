// WM-4: GPU cell culling.
//
// One thread per cell. Each tests its AABB against the six frustum planes and the distance cap,
// and — if visible — appends its index and chosen LOD to a compact output list via an atomic
// counter. This is a direct transliteration of src/World/FrustumCull.cpp; the planes are extracted
// on the CPU (once per frame, cheaper than per-thread) and passed in, so the only thing that can
// diverge between CPU and GPU is this AABB-plane test. A parity self-test asserts it does not.
//
// The plane convention is the engine's: inward normals, so a box is outside a plane only when its
// positive vertex is behind it. Depth is zero-to-one (D3D), matching extractFrustum.

struct CellBox {
  float3 boundsMin;
  uint id;         // packed into two uints below in the buffer; see CellGpu
  float3 boundsMax;
  uint pad;
};

struct CellGpu {
  float3 boundsMin;
  uint idLo;       // low 32 bits of the CellId key
  float3 boundsMax;
  uint idHi;       // high 32 bits of the CellId key
};

struct VisibleCellGpu {
  uint idLo;
  uint idHi;
  uint lod;
  float distance;
};

// b0 is the root 32-bit-constants slot in the shared compute root signature, so the constant
// buffer binds at b1 (the root CBV), matching MeshletCull.hlsl.
cbuffer CullCB : register(b1) {
  float4 frustumPlanes[6]; // xyz = inward unit normal, w = signed distance
  float4 observer;         // xyz = LOD reference point
  uint cellCount;
  float lodStep;
  uint maxLod;
  float maxDistance;       // 0 disables the far cap
};

StructuredBuffer<CellGpu> Cells : register(t0);
RWStructuredBuffer<VisibleCellGpu> VisibleCells : register(u0);
RWStructuredBuffer<uint> VisibleCounter : register(u1); // element 0 is the atomic count

float boxDistance(float3 bmin, float3 bmax, float3 p) {
  // Distance from p to the box; zero inside. Matches WorldCell::distanceTo.
  float3 d = max(max(bmin - p, 0.0.xxx), p - bmax);
  return length(d);
}

bool aabbInFrustum(float3 bmin, float3 bmax) {
  [unroll] for (int i = 0; i < 6; ++i) {
    float3 n = frustumPlanes[i].xyz;
    // Positive vertex: the corner furthest along the inward normal.
    float3 pv = float3(n.x >= 0.0 ? bmax.x : bmin.x, n.y >= 0.0 ? bmax.y : bmin.y,
                       n.z >= 0.0 ? bmax.z : bmin.z);
    if (dot(n, pv) + frustumPlanes[i].w < 0.0) return false;
  }
  return true;
}

uint selectLod(float distance) {
  if (lodStep <= 0.0) return 0u;
  uint lod = uint(max(distance, 0.0) / lodStep);
  return min(lod, maxLod);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
  uint i = tid.x;
  if (i >= cellCount) return;

  CellGpu cell = Cells[i];
  float dist = boxDistance(cell.boundsMin, cell.boundsMax, observer.xyz);

  if (maxDistance > 0.0 && dist > maxDistance) return;
  if (!aabbInFrustum(cell.boundsMin, cell.boundsMax)) return;

  uint slot;
  InterlockedAdd(VisibleCounter[0], 1u, slot);

  VisibleCellGpu v;
  v.idLo = cell.idLo;
  v.idHi = cell.idHi;
  v.lod = selectLod(dist);
  v.distance = dist;
  VisibleCells[slot] = v;
}
