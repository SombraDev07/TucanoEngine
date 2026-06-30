// Copyright 2026 TucanoEngine / B3DFramework — MIT License
//
// TerrainDepth.bsl
// ----------------
// Terrain depth-only pass for shadow maps and depth pre-pass.
//
// Reads height from the heightmap texture and outputs only SV_Depth.
// No texturing or lighting.
//
// Inspired by Dagor Engine's heightmap_depth.dshl (MIT).

Parameters:
{
	Texture2D gHeightmapTex;

	// Shared uniform buffers (same layout as TerrainHeightmap.bsl)
	uniform gTerrainFrameParams
	{
		float4 gHeightmapScaleOfs;
		float4 gHeightScaleOffset;
		float4 gMorphParams;
		float4 gVTexParams;
		float3 gCameraPos;
	};

	uniform gTerrainLightParams
	{
		float4x4 gWorldViewProj;
		float4   gSunDirection;
		float4   gSunColor;
		float4   gAmbientColor;
	};
}

Technique DepthPass:
{
	Raster =
	{
		CullMode = Back;
		DepthBias = 2;         // slight bias to avoid self-shadowing
		SlopeScaledDepthBias = 2.0;
	}

	Depth =
	{
		DepthEnable = true;
		DepthWrite  = true;
		Compare     = Less;
	}

	ColorWrite = false;
}

Vertex:
{
	Input  = { float2 localXZ : POSITION; }
	// Per-instance (from instance buffer)
	Input1 = {
		float  originX    : TEXCOORD0;
		float  originZ    : TEXCOORD1;
		float  patchSize  : TEXCOORD2;
		uint   morphFlags : TEXCOORD3;
	}

	Output = { float4 svPos : SV_Position; }

	HLSLBegin:

	float2 worldXZ = float2(input1.originX, input1.originZ) + input.localXZ * input1.patchSize;

	float2 hmUV = saturate((worldXZ - gHeightmapScaleOfs.zw) * gHeightmapScaleOfs.xy);
	float rawH  = gHeightmapTex.SampleLevel(LinearClamp, hmUV, 0).r;
	float height = gHeightScaleOffset.y + rawH * gHeightScaleOffset.x;

	// LOD morph (same as colour pass — avoids shadow cracking between LODs)
	float dist   = length(worldXZ - gCameraPos.xz);
	float morphT = saturate((dist - gMorphParams.x) * gMorphParams.z);

	float2 coarseXZ = round(worldXZ / (input1.patchSize * 2.0)) * (input1.patchSize * 2.0);
	float2 coarseUV = saturate((coarseXZ - gHeightmapScaleOfs.zw) * gHeightmapScaleOfs.xy);
	float  coarseH  = gHeightmapTex.SampleLevel(LinearClamp, coarseUV, 0).r;
	float  coarseHeight = gHeightScaleOffset.y + coarseH * gHeightScaleOffset.x;

	float morphEnable = (float)((input1.morphFlags & 1u) != 0);
	height = lerp(height, coarseHeight, morphT * morphEnable);

	float3 worldPos = float3(worldXZ.x, height, worldXZ.y);
	output.svPos = mul(float4(worldPos, 1.0), gWorldViewProj);

	HLSLEnd
}

Fragment:
{
	// Empty pixel shader — depth writes only
}
