// OITResolve.hlsl — Order-Independent Transparency resolve pass.
//
// Sorts each pixel's linked list by depth and composites front-to-back
// using standard over operator. The result is blended on top of the
// opaque scene.
//
// Based on Esoterica's OIT implementation.

#ifndef OIT_RESOLVE_HLSL
#define OIT_RESOLVE_HLSL

struct OITNode
{
	float4 color;
	float  depth;
	uint   next;
	uint   _pad0;
	uint   _pad1;
};

StructuredBuffer<uint>      g_OITHeadPointer : register(t0);
StructuredBuffer<OITNode>   g_OITNodes       : register(t1);

RWTexture2D<float4>         g_OITOutput      : register(u0); // output blend target

cbuffer OITResolveCB : register(b0)
{
	uint g_OITMaxNodes;
	uint g_OITWidth;
	uint g_OITHeight;
	uint g_OITMaxSortCount; // max fragments to sort per pixel (e.g. 32)
};

// Insertion sort on a small array (fast for typical 1-8 fragments per pixel)
void InsertionSort(inout uint indices[32], inout float depths[32], uint count)
{
	for (uint i = 1; i < count && i < 32; ++i)
	{
		uint keyIdx = indices[i];
		float keyDepth = depths[i];
		int j = int(i) - 1;
		while (j >= 0 && depths[j] > keyDepth)
		{
			indices[j + 1] = indices[j];
			depths[j + 1] = depths[j];
			--j;
		}
		indices[j + 1] = keyIdx;
		depths[j + 1] = keyDepth;
	}
}

[numthreads(8, 8, 1)]
void OITResolveCS(uint3 dtid : SV_DispatchThreadID)
{
	if (dtid.x >= g_OITWidth || dtid.y >= g_OITHeight)
		return;

	uint flatIndex = dtid.y * g_OITWidth + dtid.x;

	// Collect linked list into a local array
	uint indices[32];
	float depths[32];
	uint count = 0;

	uint nodeIdx = g_OITHeadPointer[flatIndex];
	while (nodeIdx != 0xFFFFFFFF && count < g_OITMaxSortCount)
	{
		OITNode node = g_OITNodes[nodeIdx];
		indices[count] = nodeIdx;
		depths[count] = node.depth;
		++count;
		nodeIdx = node.next;
	}

	// Sort front-to-back (ascending depth)
	InsertionSort(indices, depths, count);

	// Composite front-to-back using over operator
	float4 accum = float4(0, 0, 0, 0);
	for (uint i = 0; i < count; ++i)
	{
		OITNode node = g_OITNodes[indices[i]];
		float alpha = node.color.a;
		// over: result = src + dst * (1 - src_alpha)
		accum.rgb += node.color.rgb * (1.0 - accum.a);
		accum.a   += alpha * (1.0 - accum.a);

		if (accum.a >= 0.995)
			break; // early out when nearly opaque
	}

	g_OITOutput[dtid.xy] = accum;
}

#endif // OIT_RESOLVE_HLSL
