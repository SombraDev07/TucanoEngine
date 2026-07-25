#include "Common.hlsl"

struct GpuHeightQuery {
	float worldX, worldZ;
	float height;
	float pad;
};

// b0 is the root 32-bit-constants slot in the shared compute root signature, so the constant buffer
// binds at b1 (the root CBV) — matching CellCull.hlsl / InstanceCull.hlsl.
cbuffer QueryCB : register(b1) {
	float2 InvWorldSize; // (res-1)/(res*worldSize): maps world → normalized texel-grid coordinate
	float2 TexelOffset;  // 0.5/res: shifts onto the texel CENTRE the linear sampler interpolates from
	uint   HeightmapIndex;
	uint   QueryCount;
};

RWStructuredBuffer<GpuHeightQuery> Queries : register(u0);
Texture2D bindlessTex[] : register(t0, space0);
SamplerState s0 : register(s0);

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
	if (dtid.x >= QueryCount) return;

	GpuHeightQuery q = Queries[dtid.x];
	// Match the CPU heightmap's (res-1)-interval convention so the GPU sample lands on the same point
	// the mesh and physics read — without this the two disagree by up to a texel, which is metres on
	// steep terrain.
	float2 uv = float2(q.worldX, q.worldZ) * InvWorldSize + TexelOffset;
	q.height = bindlessTex[NonUniformResourceIndex(HeightmapIndex)].SampleLevel(s0, uv, 0).r;
	Queries[dtid.x] = q;
}
