#include "Terrain/Heightmap.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace tucano::terrain {

std::shared_ptr<Heightmap> Heightmap::createFlat(rhi::Device& device, uint32_t resolution, float worldSize, float height) {
	auto hm = std::shared_ptr<Heightmap>(new Heightmap());
	hm->m_resolution = resolution;
	hm->m_worldSize = worldSize;
	hm->m_data.resize(size_t(resolution) * size_t(resolution), height);
	hm->m_minHeight = height;
	hm->m_maxHeight = height;
	hm->uploadToGPU(device);
	return hm;
}

std::shared_ptr<Heightmap> Heightmap::createFromData(rhi::Device& device, uint32_t resolution, float worldSize, const std::vector<float>& data) {
	if (data.size() != size_t(resolution) * size_t(resolution)) {
		throw std::runtime_error("Heightmap::createFromData: data size mismatch");
	}
	auto hm = std::shared_ptr<Heightmap>(new Heightmap());
	hm->m_resolution = resolution;
	hm->m_worldSize = worldSize;
	hm->m_data = data;
	hm->recomputeBounds();
	hm->uploadToGPU(device);
	return hm;
}

static constexpr uint32_t kHtmapMagic = 0x504D5448; // 'HTMP'
static constexpr uint32_t kHtmapVersion = 1;

std::shared_ptr<Heightmap> Heightmap::loadFromFile(rhi::Device& device, const std::string& path) {
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		throw std::runtime_error("Heightmap::loadFromFile: cannot open " + path);
	}

	uint32_t magic = 0, version = 0;
	uint32_t resolution = 0;
	float worldSize = 0.0f;
	float minH = 0.0f, maxH = 0.0f;

	file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
	file.read(reinterpret_cast<char*>(&version), sizeof(version));
	file.read(reinterpret_cast<char*>(&resolution), sizeof(resolution));
	file.read(reinterpret_cast<char*>(&worldSize), sizeof(worldSize));
	file.read(reinterpret_cast<char*>(&minH), sizeof(minH));
	file.read(reinterpret_cast<char*>(&maxH), sizeof(maxH));

	if (magic != kHtmapMagic) {
		throw std::runtime_error("Heightmap::loadFromFile: invalid magic");
	}
	if (version != kHtmapVersion) {
		throw std::runtime_error("Heightmap::loadFromFile: unsupported version");
	}

	std::vector<float> data(size_t(resolution) * size_t(resolution));
	file.read(reinterpret_cast<char*>(data.data()), data.size() * sizeof(float));

	if (file.fail()) {
		throw std::runtime_error("Heightmap::loadFromFile: read error");
	}

	auto hm = std::shared_ptr<Heightmap>(new Heightmap());
	hm->m_resolution = resolution;
	hm->m_worldSize = worldSize;
	hm->m_minHeight = minH;
	hm->m_maxHeight = maxH;
	hm->m_data = std::move(data);
	hm->uploadToGPU(device);
	return hm;
}

bool Heightmap::saveToFile(const Heightmap& hm, const std::string& path) {
	std::ofstream file(path, std::ios::binary);
	if (!file.is_open()) {
		return false;
	}

	uint32_t magic = kHtmapMagic;
	uint32_t version = kHtmapVersion;
	uint32_t resolution = hm.m_resolution;
	float worldSize = hm.m_worldSize;
	float minH = hm.m_minHeight;
	float maxH = hm.m_maxHeight;

	file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
	file.write(reinterpret_cast<const char*>(&version), sizeof(version));
	file.write(reinterpret_cast<const char*>(&resolution), sizeof(resolution));
	file.write(reinterpret_cast<const char*>(&worldSize), sizeof(worldSize));
	file.write(reinterpret_cast<const char*>(&minH), sizeof(minH));
	file.write(reinterpret_cast<const char*>(&maxH), sizeof(maxH));
	file.write(reinterpret_cast<const char*>(hm.m_data.data()), hm.m_data.size() * sizeof(float));

	return file.good();
}

void Heightmap::uploadToGPU(rhi::Device& device) {
	rhi::TextureDesc desc{};
	desc.width = m_resolution;
	desc.height = m_resolution;
	desc.format = rhi::Format::R16_FLOAT;
	desc.usage = rhi::TextureUsage::ShaderResource;
	desc.debugName = "Heightmap";

	std::vector<uint16_t> halfData(m_data.size());
	for (size_t i = 0; i < m_data.size(); ++i) {
		float f = m_data[i];
		uint32_t u = *reinterpret_cast<uint32_t*>(&f);
		uint16_t sign = uint16_t((u >> 16) & 0x8000);
		int32_t exp = int32_t((u >> 23) & 0xFF) - 127;
		uint32_t mant = (u & 0x7FFFFF) >> 13;
		if (exp > 15) {
			halfData[i] = sign | 0x7C00;
		} else if (exp < -14) {
			halfData[i] = sign;
		} else if (exp < -4) {
			mant = (mant | 0x800000) >> (1 - exp - 14);
			halfData[i] = sign | uint16_t(mant);
		} else {
			halfData[i] = sign | uint16_t((exp + 15) << 10) | uint16_t(mant);
		}
	}

	m_gpuTexture = device.createTexture(desc, halfData.data(), m_resolution * sizeof(uint16_t));
	m_dirty = false;
}

float Heightmap::sampleHeight(float worldX, float worldZ) const {
	float fx = worldX / m_worldSize * float(m_resolution - 1);
	float fz = worldZ / m_worldSize * float(m_resolution - 1);

	int x0 = int(glm::clamp(fx, 0.0f, float(m_resolution - 1)));
	int z0 = int(glm::clamp(fz, 0.0f, float(m_resolution - 1)));
	int x1 = glm::min(x0 + 1, int(m_resolution - 1));
	int z1 = glm::min(z0 + 1, int(m_resolution - 1));

	float tx = fx - float(x0);
	float tz = fz - float(z0);

	float h00 = m_data[size_t(z0) * m_resolution + size_t(x0)];
	float h10 = m_data[size_t(z0) * m_resolution + size_t(x1)];
	float h01 = m_data[size_t(z1) * m_resolution + size_t(x0)];
	float h11 = m_data[size_t(z1) * m_resolution + size_t(x1)];

	return glm::mix(glm::mix(h00, h10, tx), glm::mix(h01, h11, tx), tz);
}

glm::vec3 Heightmap::sampleNormal(float worldX, float worldZ) const {
	float ts = texelSize();
	float hL = sampleHeight(worldX - ts, worldZ);
	float hR = sampleHeight(worldX + ts, worldZ);
	float hD = sampleHeight(worldX, worldZ - ts);
	float hU = sampleHeight(worldX, worldZ + ts);

	return glm::normalize(glm::vec3(0.5f * (hL - hR) / ts, 1.0f, 0.5f * (hD - hU) / ts));
}

float Heightmap::sampleHeightNearest(int x, int z) const {
	x = glm::clamp(x, 0, int(m_resolution - 1));
	z = glm::clamp(z, 0, int(m_resolution - 1));
	return m_data[size_t(z) * m_resolution + size_t(x)];
}

void Heightmap::setHeight(uint32_t x, uint32_t z, float h) {
	if (x >= m_resolution || z >= m_resolution) return;
	m_data[size_t(z) * m_resolution + size_t(x)] = h;
	m_dirty = true;
	m_minHeight = glm::min(m_minHeight, h);
	m_maxHeight = glm::max(m_maxHeight, h);
}

uint32_t Heightmap::bindlessIndex() const {
	if (m_gpuTexture) {
		return m_gpuTexture->bindlessIndex();
	}
	return ~0u;
}

void Heightmap::recomputeBounds() {
	m_minHeight = std::numeric_limits<float>::max();
	m_maxHeight = -std::numeric_limits<float>::max();
	for (float h : m_data) {
		m_minHeight = glm::min(m_minHeight, h);
		m_maxHeight = glm::max(m_maxHeight, h);
	}
}

} // namespace tucano::terrain
