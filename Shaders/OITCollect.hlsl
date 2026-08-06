// OITCollect.hlsl — Order-Independent Transparency collection pass.
//
// Renders transparent geometry into a per-pixel linked list. Each pixel shader
// invocation allocates a node via atomic counter and links it into the head
// pointer array.
//
// Resources (bound by the renderer):
//   StructuredBuffer<uint>  g_OITHeadPointer  — per-pixel head pointer (RW)
//   RWStructuredBuffer<OITNode> g_OITNodes     — linked-list node pool
//   RWBuffer<uint>          g_OITCounter       — atomic allocator counter
//
// Based on Esoterica's OIT implementation.

#ifndef OIT_COLLECT_HLSL
#define OIT_COLLECT_HLSL

#include "Common.hlsl"

struct OITNode
{
	float4 color;      // RGB = accumulated color, A = alpha
	float  depth;
	uint   next;       // index of next node in linked list, 0xFFFFFFFF = end
	uint   _pad0;
	uint   _pad1;
};

// Bound via root constants / bindless
RWStructuredBuffer<uint>   g_OITHeadPointer : register(u0, space1);
RWStructuredBuffer<OITNode> g_OITNodes      : register(u1, space1);
RWBuffer<uint>             g_OITCounter     : register(u2, space1);

cbuffer OITCollectCB : register(b0)
{
	uint  g_OITMaxNodes;
	float g_OITAlphaThreshold; // discard fragments below this alpha
	uint  g_OITScreenWidth;
	uint  g_OITScreenHeight;
};

struct OITVSOutput
{
	float4 position : SV_POSITION;
	float3 worldPos : TEXCOORD0;
	float3 normal   : TEXCOORD1;
	float2 uv       : TEXCOORD2;
	float4 color    : COLOR0;  // pre-lit color from material
};

// ── Vertex shader (pass-through, transform by object matrix) ──

cbuffer OITObjectCB : register(b1)
{
	float4x4 g_OITModel;
};

OITVSOutput OITCollectVS(uint vertexId : SV_VertexID)
{
	// Uses standard vertex buffer bound by the renderer.
	// For compressed vertices, include DecompressVertex.hlsl and decode here.
	return (OITVSOutput)0;
}

// ── Pixel shader — insert fragment into linked list ──

float4 OITCollectPS(OITVSOutput input) : SV_TARGET
{
	float alpha = input.color.a;
	if (alpha < g_OITAlphaThreshold)
		discard;

	// Allocate a node from the pool
	uint nodeIndex;
	InterlockedAdd(g_OITCounter[0], 1, nodeIndex);
	if (nodeIndex >= g_OITMaxNodes)
		discard; // pool exhausted

	// Fill the node
	OITNode node;
	node.color = float4(input.color.rgb * alpha, alpha);
	node.depth = input.position.z;
	node.next  = 0xFFFFFFFF;
	node._pad0 = 0;
	node._pad1 = 0;

	g_OITNodes[nodeIndex] = node;

	// Link into head pointer using atomic exchange
	uint2 pixel = uint2(input.position.xy);
	uint flatIndex = pixel.y * g_OITScreenWidth + pixel.x;
	uint prevHead;
	InterlockedExchange(g_OITHeadPointer[flatIndex], nodeIndex, prevHead);
	g_OITNodes[nodeIndex].next = prevHead;

	return float4(0, 0, 0, 0); // discard to render target (we only use UAV)
}

#endif // OIT_COLLECT_HLSL
