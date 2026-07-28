// Continuous-LOD geometry clipmap (clean-room, after Losasso & Hoppe 2004 + Asirvatham & Hoppe's
// GPU variant, GPU Gems 2). Written from the published technique, not ported from any engine.
//
// The idea: render a set of nested grid rings centred on the camera. The innermost ring (level 0) is
// a small, dense grid; each outer level doubles the grid spacing and the covered extent, so detail
// falls off with distance continuously. To keep the whole thing from popping as the camera moves,
// each ring MORPHS its outer band toward the next-coarser grid: a vertex near the ring's edge slides
// onto the position the coarse ring would put it, so the fine ring's outer edge coincides exactly
// with the coarse ring's inner edge — no crack, no snap.
//
// There is no vertex buffer: the grid is generated from SV_VertexID, and an index buffer supplies the
// triangle connectivity (a full grid for level 0, a hollow ring for the rest, the hole covered by the
// finer level inside it). Height comes from a bindless heightmap the CPU points us at — a static one
// for now, the streamed toroidal window later. Output matches GBuffer.hlsl so it feeds the same
// deferred lighting.

#define kGridN 64u
#define kGridStride (kGridN + 1u)

struct VSOutput {
	float4 position : SV_Position;
	float3 worldPos : TEXCOORD0;
	float3 normal   : TEXCOORD1;
	float2 uv       : TEXCOORD2;
};

cbuffer RootConsts : register(b0) {
	float4x4 ViewProj;
	// level: x = grid spacing (m), yz = ring world origin (corner), w = half the ring extent (m)
	float4 Level;
	// morph: x = morph start (0..1 of the ring half-extent), y = 1/heightmapWorldSize,
	//        z = heightScale, w = asfloat(heightmap bindless index)
	float4 Morph;
	// world: xy = heightmap world-space min corner, zw = camera XZ
	float4 World;
};

// Material virtual texture parameters. VtEnabled 0 = solid slope colour (VT off).
cbuffer VtCB : register(b2) {
	uint  VtEnabled;
	uint  AtlasIndex;
	uint  PageTableIndex;
	uint  PageTableRes;
	float VtWorldMinX;
	float VtWorldMinZ;
	float VtWorldSize;
	float PageWorldSize;
	float PageSizeF;   // atlas page edge in texels (incl. border)
	float BorderF;
	float CoreF;
	float AtlasSizeF;
	uint  MaxMip;      // coarsest mip the VT holds
	uint  _vtpad0;
	uint  _vtpad1;
	uint  _vtpad2;
};

Texture2D bindlessHeap[] : register(t0, space0);
SamplerState samp : register(s0);

// Virtual-texture lookup: world XZ → virtual page → page table → physical atlas. A page not resident
// (or outside the VT world) falls back to a flat colour so the terrain still reads as land.
float3 vtAlbedo(float2 wxz) {
	float2 local = wxz - float2(VtWorldMinX, VtWorldMinZ);
	float2 vuv = local / VtWorldSize;
	int2 vp = int2(floor(vuv * float(PageTableRes)));       // finest-resolution virtual page cell
	if (any(vp < int2(0, 0)) || any(vp >= int2(PageTableRes, PageTableRes))) return float3(0.24, 0.28, 0.16);

	// The page table stores the best resident page per finest cell: (slotX, slotY, mip, valid).
	float4 pt = bindlessHeap[NonUniformResourceIndex(PageTableIndex)].Load(int3(vp, 0));
	if (pt.w < 0.5) return float3(0.26, 0.30, 0.18); // no resident page covers this yet

	float mip = pt.z;
	float pageWorld = PageWorldSize * exp2(mip);
	float2 pageOrigin = floor(local / pageWorld) * pageWorld; // world corner of the resident page
	float2 inPage = (local - pageOrigin) / pageWorld;         // [0,1) within it
	float2 atlasTexel = pt.xy * PageSizeF + BorderF.xx + inPage * CoreF;
	float2 atlasUV = atlasTexel / AtlasSizeF;
	float3 sampled = bindlessHeap[NonUniformResourceIndex(AtlasIndex)].SampleLevel(samp, atlasUV, 0).rgb;
	return sampled;
}

float sampleHeight(float2 worldXZ) {
	float2 uv = (worldXZ - World.xy) * Morph.y;
	uint idx = asuint(Morph.w);
	return bindlessHeap[NonUniformResourceIndex(idx)].SampleLevel(samp, uv, 0).r * Morph.z;
}

VSOutput VSMain(uint vid : SV_VertexID) {
	uint gx = vid % kGridStride;
	uint gz = vid / kGridStride;
	float2 grid = float2(gx, gz);

	float spacing = Level.x;
	float2 origin = Level.yz;
	float2 fineXZ = origin + grid * spacing;

	// Morph the outer band toward the coarse (2× spacing) grid. distNorm is 0 at the ring centre and
	// 1 at its outer edge; the morph ramps in over [morphStart, 1].
	float2 camXZ = World.zw;
	float2 d = abs(fineXZ - camXZ);
	float distNorm = max(d.x, d.y) / max(Level.w, 1e-3);
	float alpha = saturate((distNorm - Morph.x) / max(1.0 - Morph.x, 1e-3));

	float coarseStep = spacing * 2.0;
	float2 coarseXZ = round(fineXZ / coarseStep) * coarseStep;
	float2 mXZ = lerp(fineXZ, coarseXZ, alpha);

	float h = sampleHeight(mXZ);

	// Normal from central-ish differences at the ring's own spacing.
	float e = spacing;
	float hR = sampleHeight(mXZ + float2(e, 0));
	float hL = sampleHeight(mXZ - float2(e, 0));
	float hU = sampleHeight(mXZ + float2(0, e));
	float hD = sampleHeight(mXZ - float2(0, e));
	float3 normal = normalize(float3(hL - hR, 2.0 * e, hD - hU));

	VSOutput o;
	float3 wp = float3(mXZ.x, h, mXZ.y);
	o.worldPos = wp;
	o.position = mul(ViewProj, float4(wp, 1.0));
	o.normal = normal;
	o.uv = mXZ * 0.05;
	return o;
}

struct GBufferOut {
	float4 albedo    : SV_Target0;
	float4 normal    : SV_Target1;
	float4 orm       : SV_Target2;
	float4 emissive  : SV_Target3;
	float  linearDepth : SV_Target4;
};

GBufferOut PSMain(VSOutput input) {
	float3 n = normalize(input.normal);
	float3 albedo;
	if (VtEnabled != 0) {
		// Material comes from the virtual texture — unique, high-density, VRAM-bounded.
		albedo = vtAlbedo(input.worldPos.xz);
	} else {
		// Fallback: a cheap slope/height tint (VT off).
		float slope = saturate(1.0 - n.y);
		float3 grass = float3(0.30, 0.42, 0.20);
		float3 rock = float3(0.34, 0.31, 0.28);
		albedo = lerp(grass, rock, smoothstep(0.35, 0.75, slope));
	}

	GBufferOut o;
	o.albedo = float4(max(albedo, 0.02.xxx), 0.05);
	o.normal = float4(n * 0.5 + 0.5, 0.0);
	o.orm = float4(1.0, 0.92, 0.0, 0.04);
	o.emissive = float4(0.0.xxx, 0.0);
	o.linearDepth = input.position.z;
	return o;
}

// VT feedback (Phase 2b): each visible pixel reports the exact virtual page + mip it wants, chosen
// from the screen-space derivative of its world position so one atlas texel lands on ~one pixel.
// The CPU reads this back and streams precisely those pages — occlusion- and angle-aware, unlike the
// clipmap-coverage request. Packed: (1<<31) | (mip << 24) | (pageY << 12) | pageX; bit 31 marks a
// real request so undrawn pixels (the RT is cleared to 0) are not mistaken for page (0,0,0).
uint PSFeedback(VSOutput input) : SV_Target0 {
	float2 wxz = input.worldPos.xz - float2(VtWorldMinX, VtWorldMinZ);
	if (any(wxz < 0.0) || any(wxz >= VtWorldSize)) return 0u;

	// Desired mip: log2 of world-per-pixel over world-per-texel at mip 0.
	float2 dwdx = ddx(wxz);
	float2 dwdy = ddy(wxz);
	float worldPerPixel = max(length(dwdx), length(dwdy));
	float texelWorld0 = PageWorldSize / CoreF;
	float mipF = max(0.0, log2(max(worldPerPixel / max(texelWorld0, 1e-4), 1.0)));
	uint mip = min((uint)(mipF + 0.5), MaxMip);

	float pageWorld = PageWorldSize * exp2((float)mip);
	uint px = (uint)floor(wxz.x / pageWorld);
	uint py = (uint)floor(wxz.y / pageWorld);
	return 0x80000000u | ((mip & 0x7F) << 24) | ((py & 0xFFF) << 12) | (px & 0xFFF);
}
