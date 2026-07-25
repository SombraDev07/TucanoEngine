#include "AssetPipeline/TucanoAsset.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>

namespace tucano::asset {

// ── GUID (FNV1a) ─────────────────────────────────────

static uint64_t fnv1a64(const std::string& data) {
	uint64_t hash = 0xcbf29ce484222325ULL;
	for (auto c : data) { hash ^= uint64_t(uint8_t(c)); hash *= 0x100000001b3ULL; }
	return hash;
}

AssetGuid AssetGuid::fromPath(const std::string& path) {
	AssetGuid g;
	g.hi = fnv1a64(path);
	g.lo = fnv1a64(std::string(path.rbegin(), path.rend()));
	return g;
}

std::string AssetGuid::toString() const {
	std::ostringstream ss;
	ss << std::hex << std::setfill('0') << std::setw(16) << hi << std::setw(16) << lo;
	return ss.str();
}

// ── CRC32 ─────────────────────────────────────────────

static const uint32_t kCrcTable[256] = {
	0x00000000,0x77073096,0xEE0E612C,0x990951BA,0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,
	0x0EDB8832,0x79DCB8A4,0xE0D5E91E,0x97D2D988,0x09B64C2B,0x7EB17CBD,0xE7B82D07,0x90BF1D91,
	0x1DB71064,0x6AB020F2,0xF3B97148,0x84BE41DE,0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,
	0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,
	0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,
	0x35B5A8FA,0x42B2986C,0xDBBBC9D6,0xACBCF940,0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,
	0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,0x21B4F4B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,
	0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,0x2F6F7C7B,0x58684C11,0xC1611DAB,0xB6662D3D,
	0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102E,0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,
	0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,0x7F6A0DBB,0x086D3D2D,0x91646C97,0xE6635C01,
	0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,
	0x65B0D9C6,0x12B7E950,0x8BBEB8EA,0xFCB9887C,0x62DD1DDF,0x15DA2D49,0x8CD37CF3,0xFBD44C65,
	0x4DB26158,0x3AB551CE,0xA3BC0074,0xD4BB30E2,0x4ADFA541,0x3DD895D7,0xA4D1C46D,0xD3D6F4FB,
	0x4369E96A,0x346ED9FC,0xAD678846,0xDA60B8D0,0x44042D73,0x33031DE5,0xAA0A4C5F,0xDD0D7CC9,
	0x5005713C,0x270241AA,0xBE0B1010,0xC90C2086,0x5768B525,0x206F85B3,0xB966D409,0xCE61E49F,
	0x5EDEF90E,0x29D9C998,0xB0D09822,0xC7D7A8B4,0x59B33D17,0x2EB40D81,0xB7BD5C3B,0xC0BA6CAD,
	0xEDB88320,0x9ABFB3B6,0x03B6E20C,0x74B1D29A,0xEAD54739,0x9DD277AF,0x04DB2615,0x73DC1683,
	0xE3630B12,0x94643B84,0x0D6D6A3E,0x7A6A5AA8,0xE40ECF0B,0x9309FF9D,0x0A00AE27,0x7D079EB1,
	0xF00F9344,0x8708A3D2,0x1E01F268,0x6906C2FE,0xF762575D,0x806567CB,0x196C3671,0x6E6B06E7,
	0xFED41B76,0x89D32BE0,0x10DA7A5A,0x67DD4ACC,0xF9B9DF6F,0x8EBEEFF9,0x17B7BE43,0x60B08ED5,
	0xD6D6A3E8,0xA1D1937E,0x38D8C2C4,0x4FDFF252,0xD1BB67F1,0xA6BC5767,0x3FB506DD,0x48B2364B,
	0xD80D2BDA,0xAF0A1B4C,0x36034AF6,0x41047A60,0xDF60EFC3,0xA867DF55,0x316E8EEF,0x4669BE79,
	0xCB61B38C,0xBC66831A,0x256FD2A0,0x5268E236,0xCC0C7795,0xBB0B4703,0x220216B9,0x5505262F,
	0xC5BA3BBE,0xB2BD0B28,0x2BB45A92,0x5CB30A04,0xC2D7FFA7,0xB5D0CF31,0x2CD99E8B,0x5BDEAE1D,
	0x9B64C2B0,0xEC63F226,0x756AA39C,0x026D930A,0x9C0906A9,0xEB0E363F,0x72076785,0x05005713,
	0x95BF4A82,0xE2B87A14,0x7BB12BAE,0x0CB61B38,0x92D28E9B,0xE5D5BE0D,0x7CDCEFB7,0x0BDBDF21,
	0x86D3D2D4,0xF1D4E242,0x68DDB3F8,0x1FDA836E,0x81BE16CD,0xF6B9265B,0x6FB077E1,0x18B74777,
	0x88085AE6,0xFF0F6A70,0x66063BCA,0x11010B5C,0x8F659EFF,0xF862AE69,0x616BFFD3,0x166CCF45,
	0xA00AE278,0xD70DD2EE,0x4E048354,0x3903B3C2,0xA7672661,0xD06016F7,0x4969474D,0x3E6E77DB,
	0xAED16A4A,0xD9D65ADC,0x40DF0B66,0x37D83BF0,0xA9BCAE53,0xDEBB9EC5,0x47B2CF7F,0x30B5FFE9,
	0xBDBDF21C,0xCABAC28A,0x53B39330,0x24B4A3A6,0xBAD03605,0xCDD70693,0x54DE5729,0x23D967BF,
	0xB3667A2E,0xC4614AB8,0x5D681B02,0x2A6F2B94,0xB40BBE37,0xC30C8EA1,0x5A05DF1B,0x2D02EF8D,
};

uint32_t crc32(const void* data, size_t size) {
	uint32_t crc = 0xFFFFFFFF;
	const auto* p = static_cast<const uint8_t*>(data);
	for (size_t i = 0; i < size; ++i) crc = (crc >> 8) ^ kCrcTable[(crc ^ p[i]) & 0xFF];
	return crc ^ 0xFFFFFFFF;
}

// ── Writer ────────────────────────────────────────────

TucanoAssetWriter::TucanoAssetWriter(AssetType type, const AssetGuid& guid, const std::string& sourcePath) {
	m_header = {};
	m_header.magic = kTuasMagic;
	m_header.version = kTuasVersion;
	m_header.guid = guid;
	m_header.type = type;
	m_header.sourcePathHash = uint32_t(fnv1a64(sourcePath));
}

void TucanoAssetWriter::addDependency(const AssetGuid& guid, AssetType type) {
	m_deps.push_back({guid, type});
}

void TucanoAssetWriter::setMetadata(const std::string& json) {
	m_metadata = json;
	if (!json.empty()) {
		ChunkEntry c;
		c.type = ChunkType::Meta;
		c.size = uint32_t(json.size());
		c.offset = 0; // computed in write()
		m_chunks.insert(m_chunks.begin(), c); // metadata always first
	}
}

void TucanoAssetWriter::addChunk(ChunkType type, const void* data, uint32_t size, bool /*compressZstd*/) {
	ChunkEntry c;
	c.type = type;

	std::vector<uint8_t> chunkData;
	chunkData.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
	c.size = uint32_t(chunkData.size());

	m_dataBuffer.insert(m_dataBuffer.end(), chunkData.begin(), chunkData.end());
	m_chunks.push_back(c);
}

bool TucanoAssetWriter::write(const std::string& filePath) {
	m_header.metadataSize = uint32_t(m_metadata.size());
	m_header.dependencyCount = uint32_t(m_deps.size());
	m_header.chunkCount = uint32_t(m_chunks.size());
	m_header.uncompressedSize = uint64_t(m_dataBuffer.size());
	m_header.timestamp = uint64_t(std::chrono::system_clock::now().time_since_epoch().count());

	// Compute layout
	// [Header 128] [Dependencies] [ChunkTable] [Metadata] [Data]
	uint64_t offset = kTuasHeaderSize;
	uint64_t depsSize = m_deps.size() * sizeof(AssetDependency);
	uint64_t chunkTableSize = m_chunks.size() * sizeof(ChunkEntry);
	uint64_t metaOffset = offset + depsSize + chunkTableSize;
	uint64_t dataOffset = metaOffset + m_metadata.size();

	// Update chunk offsets
	for (auto& c : m_chunks)
		c.offset = dataOffset + (&c - m_chunks.data()) * 0; // will be set below

	uint64_t currentDataOffset = dataOffset;
	for (auto& c : m_chunks) {
		c.offset = currentDataOffset;
		currentDataOffset += c.size;
	}

	m_header.compressedSize = currentDataOffset - dataOffset;

	// Build buffer
	TucanoAssetHeader crcHeader = m_header;
	crcHeader.crc32 = 0;

	std::vector<uint8_t> buffer(kTuasHeaderSize, 0);
	std::memcpy(buffer.data(), &crcHeader, sizeof(crcHeader));

	// Dependencies
	buffer.insert(buffer.end(),
		reinterpret_cast<const uint8_t*>(m_deps.data()),
		reinterpret_cast<const uint8_t*>(m_deps.data()) + depsSize);

	// Chunk table
	buffer.insert(buffer.end(),
		reinterpret_cast<const uint8_t*>(m_chunks.data()),
		reinterpret_cast<const uint8_t*>(m_chunks.data()) + chunkTableSize);

	// Metadata
	buffer.insert(buffer.end(), m_metadata.begin(), m_metadata.end());

	// Data chunks
	buffer.insert(buffer.end(), m_dataBuffer.begin(), m_dataBuffer.end());

	// CRC
	m_header.crc32 = crc32(buffer.data(), buffer.size());
	std::memcpy(buffer.data(), &m_header, sizeof(m_header));

	std::ofstream file(filePath, std::ios::binary);
	if (!file.is_open()) return false;
	file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
	return file.good();
}

// ── Reader ────────────────────────────────────────────

bool TucanoAssetReader::read(const std::string& filePath, TucanoAssetHeader& header,
                              std::vector<AssetDependency>& deps, std::string& metadata,
                              std::vector<ChunkEntry>& chunks) {
	std::ifstream file(filePath, std::ios::binary);
	if (!file.is_open()) return false;

	file.read(reinterpret_cast<char*>(&header), sizeof(header));
	if (header.magic != kTuasMagic || header.version < 2) return false;

	// Verify CRC
	uint32_t storedCrc = header.crc32;
	header.crc32 = 0;
	file.seekg(0, std::ios::end);
	std::vector<uint8_t> buffer(file.tellg());
	file.seekg(0, std::ios::beg);
	file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
	header.crc32 = storedCrc;
	std::memcpy(buffer.data(), &header, sizeof(header));

	if (crc32(buffer.data(), buffer.size()) != storedCrc) return false;

	// Dependencies
	deps.resize(header.dependencyCount);
	if (header.dependencyCount > 0) {
		size_t off = kTuasHeaderSize;
		std::memcpy(deps.data(), buffer.data() + off, header.dependencyCount * sizeof(AssetDependency));
	}

	// Chunk table
	size_t tableOff = kTuasHeaderSize + header.dependencyCount * sizeof(AssetDependency);
	chunks.resize(header.chunkCount);
	if (header.chunkCount > 0) {
		std::memcpy(chunks.data(), buffer.data() + tableOff, header.chunkCount * sizeof(ChunkEntry));
	}

	// Metadata (first chunk is always Meta)
	size_t metaOff = tableOff + header.chunkCount * sizeof(ChunkEntry);
	metadata.assign(buffer.begin() + metaOff, buffer.begin() + metaOff + header.metadataSize);

	return true;
}

bool TucanoAssetReader::readHeader(const std::string& filePath, TucanoAssetHeader& header) {
	std::ifstream file(filePath, std::ios::binary);
	if (!file.is_open()) return false;
	file.read(reinterpret_cast<char*>(&header), sizeof(header));
	return header.magic == kTuasMagic;
}

bool TucanoAssetReader::readChunk(const std::string& filePath, const ChunkEntry& chunk,
                                   std::vector<uint8_t>& outData) {
	std::ifstream file(filePath, std::ios::binary);
	if (!file.is_open()) return false;

	TucanoAssetHeader hdr;
	file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
	if (hdr.magic != kTuasMagic) return false;

	outData.resize(chunk.size);
	file.seekg(chunk.offset);
	file.read(reinterpret_cast<char*>(outData.data()), chunk.size);
	return file.good();
}

} // namespace tucano::asset
