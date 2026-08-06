#pragma once

// .tuasset v2 — Native Tucano Asset binary format
//
// Layout:
//   [Header 128B] [Dependency Table] [Chunk Table] [Metadata JSON] [Data Chunks]
//
// Chunk-based design allows:
//   - Streaming: load only needed chunks (e.g., skip mips for distant objects)
//   - Draco: DRCV/DRCI chunks for 10-20x mesh compression
//   - ZSTD: transparent per-chunk compression
//   - Incremental: add chunks without rewriting entire file
//
// Design goals:
//   - Single binary blob per asset — no sidecar files
//   - Dependency graph encoded in-header for fast scanning
//   - Editor metadata separate from runtime data
//   - CRC32 integrity check
//   - Deterministic GUID from source path (same path = same GUID)

#include <cstdint>
#include <string>
#include <vector>

namespace tucano::asset {

inline constexpr uint32_t kTuasMagic     = 0x53415554; // "TUAS"
inline constexpr uint32_t kTuasVersion   = 2;
inline constexpr uint32_t kTuasHeaderSize = 128;

// Chunk types for the data section
enum class ChunkType : uint32_t {
	Meta     = 0x4154454D, // "META" — editor metadata JSON
	Vert     = 0x54524556, // "VERT" — uncompressed vertex buffer
	Indx     = 0x58444E49, // "INDX" — uncompressed index buffer
	Mshl     = 0x4C48534D, // "MSHL" — meshlet data
	DRCV     = 0x56435244, // "DRCV" — Draco-compressed vertices+normals+uvs
	DRCI     = 0x49435244, // "DRCI" — Draco-compressed indices
	Tex0     = 0x30584554, // "TEX0" — texture mip 0
	Tex1     = 0x31584554, // "TEX1" — texture mip 1
	Tex2     = 0x32584554, // "TEX2" — texture mip 2
	Tex3     = 0x33584554, // "TEX3" — texture mip 3
	TexN     = 0x4E584554, // "TEXN" — remaining mips packed
	Matl     = 0x4C54414D, // "MATL" — material parameters
	Skel     = 0x4C454B53, // "SKEL" — skeleton bone data
	Anim     = 0x4D494E41, // "ANIM" — animation curves
	Audio    = 0x49445541, // "AUDI" — audio PCM data
};

enum class AssetType : uint32_t {
	Unknown    = 0,
	Mesh       = 1,
	Texture    = 2,
	Material   = 3,
	Scene      = 4,
	Animation  = 5,
	Skeleton   = 6,
	Audio      = 7,
	Shader     = 8,
	Font       = 9,
};

enum class AssetFlags : uint32_t {
	None       = 0,
	Compressed = 1u << 0,  // data blob is zstd compressed
	Streamable = 1u << 1,  // supports partial loading
	HasLods    = 1u << 2,  // mesh has LOD chain
	Cooked     = 1u << 3,  // shipping-ready, metadata stripped
	DracoEnc   = 1u << 4,  // uses DRCV/DRCI chunks
	HasNormals = 1u << 5,  // VERT chunk carries a normal array after the positions
	HasUVs     = 1u << 6,  // VERT chunk carries a texcoord array after the normals
};

inline AssetFlags operator|(AssetFlags a, AssetFlags b) { return AssetFlags(uint32_t(a) | uint32_t(b)); }
inline bool operator&(AssetFlags a, AssetFlags b) { return (uint32_t(a) & uint32_t(b)) != 0; }

struct AssetGuid {
	uint64_t hi = 0;
	uint64_t lo = 0;

	bool valid() const { return hi != 0 || lo != 0; }
	bool operator==(const AssetGuid& o) const { return hi == o.hi && lo == o.lo; }
	bool operator!=(const AssetGuid& o) const { return !(*this == o); }
	bool operator<(const AssetGuid& o) const { return hi < o.hi || (hi == o.hi && lo < o.lo); }

	static AssetGuid fromPath(const std::string& path);
	std::string toString() const;
};

struct AssetDependency {
	AssetGuid guid;
	AssetType type = AssetType::Unknown;
};

// 128-byte header — always at offset 0
struct TucanoAssetHeader {
	uint32_t magic         = kTuasMagic;
	uint32_t version       = kTuasVersion;
	AssetGuid guid{};
	AssetType type         = AssetType::Unknown;
	uint32_t flags         = 0;
	uint32_t crc32         = 0;               // CRC32 of [header+0..EOF], with this field zeroed
	uint32_t metadataSize  = 0;               // JSON metadata chunk size
	uint32_t dependencyCount = 0;
	uint32_t chunkCount    = 0;               // number of data chunks
	uint64_t compressedSize = 0;              // total compressed size (0 = uncompressed)
	uint64_t uncompressedSize = 0;            // total uncompressed size
	uint64_t timestamp     = 0;               // build time (for incremental cook)
	uint64_t sourceHash    = 0;               // xxHash64 of source file
	uint32_t sourcePathHash = 0;              // fnv1a of original source path
	uint32_t _reserved     = 0;
	uint8_t  _padding[40]  = {0};
};
static_assert(sizeof(TucanoAssetHeader) == 128, "Header must be exactly 128 bytes");

// Each chunk points to a region in the data section
struct ChunkEntry {
	uint64_t offset = 0;     // absolute offset from file start
	uint32_t size   = 0;     // chunk size in bytes
	ChunkType type  = ChunkType::Meta;
	uint32_t _pad   = 0;     // alignment to 24 bytes
};
static_assert(sizeof(ChunkEntry) == 24, "ChunkEntry must be 24 bytes");

// ── Type-specific data layouts ───────────────────────

// Header of the VERT (uncompressed) and DRCV (Draco) mesh chunks.
//   VERT payload: [MeshAssetData][positions f32x3][normals f32x3?][uvs f32x2?]
//                 — the optional arrays are present per AssetFlags::HasNormals / HasUVs.
//   DRCV payload: [MeshAssetData][uint64 dracoByteCount][draco blob]
//   INDX payload: [uint32 indices] (uncompressed layout only)
struct MeshAssetData {
	uint32_t vertexCount = 0;
	uint32_t indexCount = 0;
	uint32_t submeshCount = 0;
	uint32_t meshletCount = 0;
};

// Texture data
struct TextureAssetData {
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t mipCount = 0;
	uint32_t format = 0;   // RHITypes::Format
	// Followed by: [pixels] or [compressed data]
};

// Material data
struct MaterialAssetData {
	float baseColor[4]{1,1,1,1};
	float emissive[4]{0,0,0,0};
	float metallic = 0.0f;
	float roughness = 0.5f;
	float ao = 1.0f;
	float reflectance = 0.5f;
	float clearcoat = 0.0f;
	float clearcoatRoughness = 0.1f;
	float fuzz = 0.0f;
	float detailScale = 0.0f;
	float alphaCutoff = 0.5f;
	uint32_t flags = 0;  // hasNormalMap, hasEmissive, etc.
	// Followed by: GUIDs for texture references (albedo, normal, ORM, emissive, detail)
};

// ── Reader / Writer ──────────────────────────────────

class TucanoAssetWriter {
public:
	TucanoAssetWriter(AssetType type, const AssetGuid& guid, const std::string& sourcePath);

	void addDependency(const AssetGuid& guid, AssetType type);
	void setMetadata(const std::string& json);
	/// ORs `flags` into the header's flag word (see AssetFlags).
	void addFlags(uint32_t flags);
	void addChunk(ChunkType type, const void* data, uint32_t size, bool compressZstd = false);

	bool write(const std::string& filePath);

private:
	TucanoAssetHeader m_header{};
	std::vector<AssetDependency> m_deps;
	std::string m_metadata;
	std::vector<ChunkEntry> m_chunks;
	std::vector<uint8_t> m_dataBuffer;
	uint64_t m_dataOffset = 0;
	int m_metaChunkIndex = -1; // index into m_chunks of the Meta entry, -1 when absent
};

class TucanoAssetReader {
public:
	static bool read(const std::string& filePath, TucanoAssetHeader& header,
	                 std::vector<AssetDependency>& deps, std::string& metadata,
	                 std::vector<ChunkEntry>& chunks);

	static bool readHeader(const std::string& filePath, TucanoAssetHeader& header);
	static bool readChunk(const std::string& filePath, const ChunkEntry& chunk,
	                      std::vector<uint8_t>& outData);
};

uint32_t crc32(const void* data, size_t size);

} // namespace tucano::asset

// Hash support
namespace std {
template<> struct hash<tucano::asset::AssetGuid> {
	size_t operator()(const tucano::asset::AssetGuid& g) const {
		return size_t(g.hi) ^ size_t(g.lo);
	}
};
}
