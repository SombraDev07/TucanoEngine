#pragma once

// Draco mesh compression wrapper — encodes/decodes mesh geometry
// for .tuasset DRCV (Draco Vertices) and DRCI (Draco Indices) chunks.
//
// Compression ratio: 10-20x with controlled quantization loss.
// Encode offline (asset cook), decode runtime (GPU upload).

#include <cstdint>
#include <vector>

namespace tucano::asset {

struct MeshDecompressed {
	std::vector<float> positions;  // xyz interleaved
	std::vector<float> normals;    // xyz interleaved (if present)
	std::vector<float> uvs;        // uv interleaved (if present)
	std::vector<uint32_t> indices;
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
};

class DracoMesh {
public:
	// Encode mesh geometry with Draco compression.
	// quantizationBits: 10 = visually lossless, 8 = slight loss, 6 = aggressive
	// Returns compressed vertex + index data pairs.
	static bool encode(
		const float* positions, uint32_t vertexCount,
		const float* normals,
		const float* uvs,
		const uint32_t* indices, uint32_t indexCount,
		int quantizationBits,
		std::vector<uint8_t>& outDracoData);

	// Decode Draco-compressed mesh data into usable float/int buffers.
	static bool decode(
		const uint8_t* dracoData, size_t dracoSize,
		MeshDecompressed& outMesh);
};

} // namespace tucano::asset
