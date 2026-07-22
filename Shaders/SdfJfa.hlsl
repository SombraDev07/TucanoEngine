// GPU 3D Jump-Flood Algorithm for the world SDF (replaces the CPU JFA that cost ~80 ms/cascade).
// Each voxel stores the packed index of the nearest surface seed; one dispatch per JFA step,
// ping-ponging between two R32_UINT volumes. Distance is derived from the seed coordinates.

cbuffer JfaCB : register(b0) { // root 32-bit constants
  uint uStep;     // jump distance this pass
  uint uRes;      // cascade resolution (64)
  uint uPad0;
  uint uPad1;
};

Texture3D<uint> SeedIn : register(t0);
RWTexture3D<uint> SeedOut : register(u0);

static const uint kEmpty = 0xFFFFFFFFu;

float seedDistSq(uint3 voxel, uint seed, uint res) {
  const uint sx = seed % res;
  const uint sy = (seed / res) % res;
  const uint sz = seed / (res * res);
  const float3 d = float3(voxel) - float3(float(sx), float(sy), float(sz));
  return dot(d, d);
}

[numthreads(4, 4, 4)]
void CSMain(uint3 id : SV_DispatchThreadID) {
  if (id.x >= uRes || id.y >= uRes || id.z >= uRes) {
    return;
  }
  uint best = SeedIn.Load(int4(id, 0));
  float bestD = (best == kEmpty) ? 1e30f : seedDistSq(id, best, uRes);

  [unroll] for (int dz = -1; dz <= 1; ++dz)
    [unroll] for (int dy = -1; dy <= 1; ++dy)
      [unroll] for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0 && dz == 0) {
          continue;
        }
        const int3 n = int3(id) + int3(dx, dy, dz) * int(uStep);
        if (n.x < 0 || n.y < 0 || n.z < 0 || n.x >= int(uRes) || n.y >= int(uRes) || n.z >= int(uRes)) {
          continue;
        }
        const uint s = SeedIn.Load(int4(n, 0));
        if (s == kEmpty) {
          continue;
        }
        const float d = seedDistSq(id, s, uRes);
        if (d < bestD) {
          bestD = d;
          best = s;
        }
      }
  SeedOut[id] = best;
}
