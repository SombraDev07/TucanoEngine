// Copyright 2026 TucanoEngine / B3DFramework — MIT License
//
// TerrainHeightmap.bsl
// --------------------
// Geo-Clipmap terrain vertex + pixel shader.
//
// Technique: LodGrid Geo-Clipmap with vertex-shader morph between LOD rings.
// Texturing:  Virtual Texture (indirection + atlas) sampled in pixel shader.
//
// Inspired by Dagor Engine heightmap shaders (MIT):
//   prog/gameLibs/landMesh/shaders/  (lmesh_*.dshl, heightmap_blend.dshl)
//   prog/gameLibs/landMesh/vtex.hlsli
//
// Ported & adapted for the B3D BSL shader language.

Parameters:
{
	// --- Heightmap ---
	Texture2D gHeightmapTex;          // R16_UNORM heightmap
	float4    gHeightScaleOffset;     // (heightScale, heightMin, 0, 0)
	float4    gHeightmapScaleOfs;     // (1/worldSizeX, 1/worldSizeZ, worldOfsX, worldOfsZ)

	// --- Virtual Texture ---
	Texture2D     gIndirectionTex;    // RG16_UINT: atlas slot XY per virtual page
	Texture2DArray gAtlasAlbedo;      // RGBA8 atlas
	Texture2DArray gAtlasNormal;      // RG16F or BC5 atlas
	Texture2DArray gAtlasRoughAO;     // RG8 atlas
	float4    gVTexParams;            // (tilesPerMip, tileTexelSize, atlasInvSize, mipCount)

	// --- Per-patch constants (set via uniform buffer / push constants) ---
	float4    gPatchOriginSize;       // (originX, originZ, patchSize, lodLevel)
	float4    gMorphParams;           // (morphStart, morphEnd, morphInvRange, 0)
	float3    gCameraPos;
	float4x4  gWorldViewProj;
	float3    gSunDir;
	float4    gSunColor;
	float4    gAmbientColor;
}

//===========================================================================
// VERTEX SHADER
//===========================================================================
Vertex:
{
	// Input: normalised [0,1] grid coordinates for a kPatchDim × kPatchDim quad-grid.
	// These are baked into a shared index/vertex buffer and reused for all patches.
	Input = { float2 localXZ : POSITION; }

	Output =
	{
		float4 svPos    : SV_Position;
		float3 worldPos : TEXCOORD0;   // world-space position (x, height, z)
		float2 vtexUV   : TEXCOORD1;   // virtual texture UV (0..1 over terrain)
		float  morphT   : TEXCOORD2;   // morph blend factor (0=fine, 1=coarse)
	}

	HLSLBegin:

	// -----------------------------------------------------------------------
	// Derive world-space XZ from patch constants + local grid coordinate.
	// Corresponds to Dagor's patch origin / size encoding in LodGridPatchParams.
	// -----------------------------------------------------------------------
	float patchSize = gPatchOriginSize.z;
	float2 worldXZ  = float2(gPatchOriginSize.x, gPatchOriginSize.y)
	                  + input.localXZ * patchSize;

	// -----------------------------------------------------------------------
	// Sample heightmap — fine LOD
	// -----------------------------------------------------------------------
	float2 hmUV = (worldXZ - gHeightmapScaleOfs.zw) * gHeightmapScaleOfs.xy;
	hmUV        = saturate(hmUV);

	float rawFine   = gHeightmapTex.SampleLevel(LinearClamp, hmUV, 0).r;
	float heightFine = gHeightScaleOffset.y + rawFine * gHeightScaleOffset.x;

	// -----------------------------------------------------------------------
	// LOD morph — interpolate toward coarser LOD.
	// Snaps localXZ to the next-coarser 2× grid and samples there.
	// Mirrors the morph logic in Dagor's hmap_vs.hlsl.
	// -----------------------------------------------------------------------
	float distToCam  = length(worldXZ - gCameraPos.xz);
	float morphT     = saturate((distToCam - gMorphParams.x) * gMorphParams.z);

	// Coarse LOD snaps to a 2× coarser grid
	float2 coarseLocalXZ = round(input.localXZ * 0.5) * 2.0;
	float2 coarseWorldXZ = float2(gPatchOriginSize.x, gPatchOriginSize.y)
	                       + coarseLocalXZ * patchSize;
	float2 coarseUV      = saturate((coarseWorldXZ - gHeightmapScaleOfs.zw) * gHeightmapScaleOfs.xy);
	float  rawCoarse     = gHeightmapTex.SampleLevel(LinearClamp, coarseUV, 0).r;
	float  heightCoarse  = gHeightScaleOffset.y + rawCoarse * gHeightScaleOffset.x;

	float height = lerp(heightFine, heightCoarse, morphT);

	float3 worldPos = float3(worldXZ.x, height, worldXZ.y);
	output.svPos    = mul(float4(worldPos, 1.0), gWorldViewProj);
	output.worldPos = worldPos;
	output.morphT   = morphT;

	// Virtual texture UV (normalised 0..1 over the entire terrain)
	output.vtexUV = hmUV;

	HLSLEnd
}

//===========================================================================
// PIXEL SHADER
//===========================================================================
Fragment:
{
	Input =
	{
		float3 worldPos : TEXCOORD0;
		float2 vtexUV   : TEXCOORD1;
		float  morphT   : TEXCOORD2;
	}

	Output = { float4 color : SV_Target0; }

	HLSLBegin:

	// -----------------------------------------------------------------------
	// Virtual Texture lookup
	// Mirrors Dagor vtex.hlsli / clipmap tile sampling.
	//
	// 1. Compute which mip tile the pixel falls in.
	// 2. Read indirection texture to get the physical atlas slot.
	// 3. Sample the atlas with an adjusted UV inside the tile.
	// -----------------------------------------------------------------------
	float mipLevel  = log2(max(length(ddx(input.vtexUV)), length(ddy(input.vtexUV)))
	                       * gVTexParams.x) + 0.5;
	mipLevel        = clamp(mipLevel, 0, gVTexParams.w - 1);
	int   mipFloor  = (int)floor(mipLevel);
	float mipFrac   = mipLevel - mipFloor;

	// Sample two consecutive mip levels and blend
	float4 albedo0 = SampleVTex(gAtlasAlbedo, gIndirectionTex, input.vtexUV, mipFloor,   gVTexParams);
	float4 albedo1 = SampleVTex(gAtlasAlbedo, gIndirectionTex, input.vtexUV, mipFloor+1, gVTexParams);
	float4 albedo  = lerp(albedo0, albedo1, mipFrac);

	float2 normalXY = SampleVTex(gAtlasNormal, gIndirectionTex, input.vtexUV, mipFloor, gVTexParams).rg;
	normalXY        = normalXY * 2.0 - 1.0;
	float3 N        = normalize(float3(normalXY.x, sqrt(max(0, 1 - dot(normalXY, normalXY))), normalXY.y));

	// -----------------------------------------------------------------------
	// Simple lighting
	// -----------------------------------------------------------------------
	float NdotL  = saturate(dot(N, -gSunDir));
	float3 lit   = albedo.rgb * (gSunColor.rgb * NdotL + gAmbientColor.rgb);

	output.color = float4(lit, 1.0);

	HLSLEnd
}

//===========================================================================
// Helper: sample virtual texture atlas via indirection
// Adapted from Dagor vtex.hlsli (MIT)
//===========================================================================
HLSL:
{
	float4 SampleVTex(Texture2DArray atlas, Texture2D indirection,
		float2 vtexUV, int mip, float4 vTexParams)
	{
		// Tile coordinates at this mip
		float mipScale  = pow(2.0, mip);
		float2 tileUV   = vtexUV * vTexParams.x / mipScale;  // vTexParams.x = tilesPerMip at mip0
		int2   tileCoord = (int2)floor(tileUV);
		float2 inTileUV  = frac(tileUV);

		// Remap into tile with border
		float borderFrac = 4.0 / vTexParams.y;               // vTexParams.y = tileTexelSize
		inTileUV = inTileUV * (1.0 - 2.0*borderFrac) + borderFrac;

		// Read indirection (physical atlas slot)
		int2 physSlot = (int2)indirection.Load(int3(tileCoord, 0)).rg;

		// Compute atlas UV
		float invAtlas   = vTexParams.z;                      // 1 / atlasSize
		float2 atlasBase = (float2)physSlot * vTexParams.y * invAtlas;
		float2 atlasUV   = atlasBase + inTileUV * vTexParams.y * invAtlas;

		return atlas.Sample(LinearClamp, float3(atlasUV, 0));
	}
}
