#include "Common.hlsl"

#define kRingCount 8
#define kRingSize  128
#define kRingVerts ((kRingSize + 1) * (kRingSize + 1))
#define kMorphW    2.0

struct TerrainVertexOut {
	float posX, posY, posZ;
	float nrmX, nrmY, nrmZ;
	float uvU, uvV;
};

struct ClipmapRing {
	float   scale;
	float   pad0;
	float   worldOriginX;
	float   worldOriginZ;
};

cbuffer ClipmapCB : register(b0) {
	float2      InvWorldSize;
	uint        HeightmapIndex;
	float       HeightScale;
	float       BaseScale;
	float       MorphWidth;
	uint        HoleMaskIndex;
	float       PadCB;
	ClipmapRing Rings[kRingCount];
};

RWStructuredBuffer<TerrainVertexOut> OutputVerts : register(u0);
Texture2D bindlessTex[] : register(t0, space0);
SamplerState s0 : register(s0);

float sampleHM(uint idx, float2 uv) {
	return bindlessTex[NonUniformResourceIndex(idx)].SampleLevel(s0, uv, 0).r * HeightScale;
}

float2 worldToUV(float wx, float wz) {
	return float2(wx, wz) * InvWorldSize;
}

bool isHole(float2 worldXZ) {
	if (HoleMaskIndex == 0 || HoleMaskIndex == 0xFFFFFFFFu) return false;
	float2 uv = worldToUV(worldXZ.x, worldXZ.y);
	float mask = bindlessTex[NonUniformResourceIndex(HoleMaskIndex)].SampleLevel(s0, uv, 0).r;
	return mask < 0.5;
}

void computeMorph(float wx, float wz, uint ringIdx, uint vx, uint vz, inout float h) {
	if (ringIdx >= kRingCount - 1) return;

	float edgeDist = float(min(min(vx, kRingSize - vx), min(vz, kRingSize - vz)));
	float morphW = MorphWidth > 0.0 ? MorphWidth : kMorphW;
	if (edgeDist >= morphW) return;

	float morph = 1.0 - edgeDist / morphW;
	float coarseScale = BaseScale * exp2(float(ringIdx + 1));
	float cx = floor(wx / coarseScale) * coarseScale + coarseScale * 0.5;
	float cz = floor(wz / coarseScale) * coarseScale + coarseScale * 0.5;
	float coarseH = sampleHM(HeightmapIndex, worldToUV(cx, cz));
	h = lerp(h, coarseH, morph);
}

[numthreads(256, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
	uint linearIdx = dtid.x;
	if (linearIdx >= kRingCount * kRingVerts) return;

	uint ringIdx = linearIdx / kRingVerts;
	uint rem = linearIdx - ringIdx * kRingVerts;
	uint vz = rem / (kRingSize + 1);
	uint vx = rem - vz * (kRingSize + 1);

	ClipmapRing ring = Rings[ringIdx];

	float fx = float(vx) / float(kRingSize);
	float fz = float(vz) / float(kRingSize);
	float wx = ring.worldOriginX + fx * ring.scale * float(kRingSize);
	float wz = ring.worldOriginZ + fz * ring.scale * float(kRingSize);

	if (isHole(float2(wx, wz))) {
		TerrainVertexOut v;
		v.posX = wx; v.posY = -99999; v.posZ = wz;
		v.nrmX = 0; v.nrmY = 1; v.nrmZ = 0;
		v.uvU = 0; v.uvV = 0;
		OutputVerts[linearIdx] = v;
		return;
	}

	float2 uv = worldToUV(wx, wz);
	float h = sampleHM(HeightmapIndex, uv);

	computeMorph(wx, wz, ringIdx, vx, vz, h);

	float ts = ring.scale;
	float2 uvR = uv + float2(ts * InvWorldSize.x, 0);
	float2 uvU = uv + float2(0, ts * InvWorldSize.y);
	float hR = sampleHM(HeightmapIndex, uvR);
	float hU = sampleHM(HeightmapIndex, uvU);

	float3 gradX = float3(ts, hR - h, 0);
	float3 gradZ = float3(0, hU - h, ts);
	float3 normal = normalize(cross(gradZ, gradX));

	TerrainVertexOut v;
	v.posX = wx; v.posY = h; v.posZ = wz;
	v.nrmX = normal.x; v.nrmY = normal.y; v.nrmZ = normal.z;
	v.uvU = wx * 0.05f; v.uvV = wz * 0.05f;
	OutputVerts[linearIdx] = v;
}
