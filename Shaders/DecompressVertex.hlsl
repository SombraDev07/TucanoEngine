// Decompress DXR2 COMPRESSED1 vertex data in shaders.
// Include this from any HLSL file that needs to decode compressed vertices.

#ifndef DECOMPRESS_VERTEX_HLSL
#define DECOMPRESS_VERTEX_HLSL

// ── Position: 16-bit unsigned offset from anchor, scaled by 2^exponent ──

float3 DecompressPosition(
	int anchorX, int anchorY, int anchorZ, int exponent,
	uint compressedX, uint compressedY, uint compressedZ)
{
	float scale = exp2(float(exponent));
	float3 anchor = float3(float(anchorX), float(anchorY), float(anchorZ));
	float3 offset = float3(
		float(compressedX) / 65535.0,
		float(compressedY) / 65535.0,
		float(compressedZ) / 65535.0);
	return anchor + offset * scale;
}

// ── Normal: 16-bit signed normalized ──

float3 DecompressNormal(int nx, int ny, int nz)
{
	float3 n = float3(
		float(nx) / 32767.0,
		float(ny) / 32767.0,
		float(nz) / 32767.0);
	return normalize(n);
}

#endif // DECOMPRESS_VERTEX_HLSL
