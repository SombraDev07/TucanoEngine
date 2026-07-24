#include "Common.hlsl"

struct GpuHeightQuery {
	float worldX, worldZ;
	float height;
	float pad;
};

cbuffer QueryCB : register(b0) {
	float2 InvWorldSize;
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
	float2 uv = float2(q.worldX, q.worldZ) * InvWorldSize;
	q.height = bindlessTex[NonUniformResourceIndex(HeightmapIndex)].SampleLevel(s0, uv, 0).r;
	Queries[dtid.x] = q;
}
