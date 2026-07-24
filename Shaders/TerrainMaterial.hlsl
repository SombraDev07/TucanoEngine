#include "Common.hlsl"

#define kAtlasSize    4096.0
#define kTileTexels   128.0
#define kTilesPerSide (kAtlasSize / kTileTexels)

Texture2D bindlessTex[] : register(t0, space0);
SamplerState s0 : register(s0);

cbuffer FillCB : register(b0) {
	float2 InvWorldSize;
	uint   HeightmapIndex;
	uint   AtlasUavIndex;
	float  TileWorldSize;
	float3 Pad;
};

RWTexture2D<float4> AtlasUav : register(u0);

[numthreads(8, 8, 1)]
void CSFillAtlas(uint3 dtid : SV_DispatchThreadID) {
	uint2 pixel = dtid.xy;
	if (pixel.x >= uint(kAtlasSize) || pixel.y >= uint(kAtlasSize)) return;

	uint tileX = pixel.x / uint(kTileTexels);
	uint tileY = pixel.y / uint(kTileTexels);
	uint lx = pixel.x - tileX * uint(kTileTexels);
	uint ly = pixel.y - tileY * uint(kTileTexels);

	float wx = (float(tileX) + (float(lx) + 0.5) / kTileTexels) * TileWorldSize;
	float wz = (float(tileY) + (float(ly) + 0.5) / kTileTexels) * TileWorldSize;

	float2 uv = float2(wx, wz) * InvWorldSize;
	float h = bindlessTex[NonUniformResourceIndex(HeightmapIndex)].SampleLevel(s0, uv, 0).r;

	float2 uvR = uv + float2(0.01 * InvWorldSize.x, 0);
	float2 uvU = uv + float2(0, 0.01 * InvWorldSize.y);
	float hR = bindlessTex[NonUniformResourceIndex(HeightmapIndex)].SampleLevel(s0, uvR, 0).r;
	float hU = bindlessTex[NonUniformResourceIndex(HeightmapIndex)].SampleLevel(s0, uvU, 0).r;
	float slope = 1.0 - abs(hR - hU) * 10.0;

	float3 grass = float3(0.22, 0.42, 0.14);
	float3 rock  = float3(0.45, 0.40, 0.35);
	float3 dirt  = float3(0.35, 0.25, 0.15);

	float rockBlend  = smoothstep(0.4, 0.7, slope);
	float dirtBlend  = smoothstep(0.1, 0.3, slope) * (1.0 - rockBlend);
	float3 albedo = grass * (1.0 - rockBlend - dirtBlend) + dirt * dirtBlend + rock * rockBlend;

	float shade = 0.7 + 0.3 * slope;
	albedo *= shade;

	AtlasUav[pixel] = float4(albedo, 1.0);
}
