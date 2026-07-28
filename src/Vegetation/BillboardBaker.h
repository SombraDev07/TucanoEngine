#pragma once

#include "RHI/RHI.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <memory>
#include <vector>
#include <string>
#include <cstring>

namespace tucano::veg {

struct BillboardAtlasEntry {
	std::string typeName;
	uint32_t typeId = 0;
	uint32_t x = 0;
	uint32_t y = 0;
	uint32_t size = 64;
	uint32_t viewsPerType = 8;
	float verticalFOV = glm::radians(30.0f);
};

struct BillboardAtlas {
	static constexpr uint32_t kAtlasSize = 1024;
	static constexpr uint32_t kMaxEntries = 256;

	uint32_t registerType(uint32_t typeId, const std::string& name, uint32_t viewCount = 8) {
		if (m_entries.size() >= kMaxEntries) return UINT32_MAX;

		BillboardAtlasEntry entry;
		entry.typeId = typeId;
		entry.typeName = name;
		entry.viewsPerType = viewCount;

		uint32_t gridSize = uint32_t(std::ceil(std::sqrt(float(kMaxEntries))));
		uint32_t cellSize = kAtlasSize / gridSize;

		uint32_t idx = uint32_t(m_entries.size());
		entry.size = cellSize;
		entry.x = (idx % gridSize) * cellSize;
		entry.y = (idx / gridSize) * cellSize;

		m_entries.push_back(entry);
		return idx;
	}

	const BillboardAtlasEntry* find(uint32_t typeId) const {
		for (auto& e : m_entries)
			if (e.typeId == typeId) return &e;
		return nullptr;
	}

	struct AtlasGPU {
		float invAtlasSize = 1.0f / kAtlasSize;
		float padding[3] = {0,0,0};
	};

	AtlasGPU gpuConstants() const {
		return {1.0f / kAtlasSize};
	}

	const BillboardAtlasEntry* entry(size_t i) const {
		return i < m_entries.size() ? &m_entries[i] : nullptr;
	}
	size_t entryCount() const { return m_entries.size(); }

private:
	std::vector<BillboardAtlasEntry> m_entries;
};

struct ImpostorView {
	glm::mat4 viewMatrix{1.0f};
	glm::mat4 projMatrix{1.0f};
	float yaw = 0;
	float pitch = 0.35f;
};

class BillboardBaker {
public:
	BillboardBaker() = default;

	void configure(uint32_t resolution = 64, uint32_t views = 8) {
		m_resolution = resolution;
		m_views = views;
		generateViews();
	}

	void generateViews() {
		m_viewMatrices.clear();
		for (uint32_t i = 0; i < m_views; ++i) {
			float yaw = float(i) / float(m_views) * 2.0f * 3.14159265f;
			ImpostorView v;
			v.yaw = yaw;
			v.pitch = 0.35f;

			glm::vec3 camPos(
				std::sin(yaw) * std::cos(v.pitch) * 3.0f,
				std::sin(v.pitch) * 3.0f,
				std::cos(yaw) * std::cos(v.pitch) * 3.0f
			);

			v.viewMatrix = glm::lookAtLH(camPos, glm::vec3(0), glm::vec3(0, 1, 0));
			v.projMatrix = glm::perspectiveLH_ZO(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);

			m_viewMatrices.push_back(v);
		}
	}

	uint32_t selectedView(float cameraYaw) const {
		float bestDot = -1;
		uint32_t bestView = 0;
		glm::vec3 camDir(std::sin(cameraYaw), 0, std::cos(cameraYaw));

		for (uint32_t i = 0; i < m_views; ++i) {
			float viewYaw = m_viewMatrices[i].yaw;
			glm::vec3 viewDir(std::sin(viewYaw), 0, std::cos(viewYaw));
			float d = glm::dot(camDir, viewDir);
			if (d > bestDot) { bestDot = d; bestView = i; }
		}
		return bestView;
	}

	const std::vector<ImpostorView>& views() const { return m_viewMatrices; }
	uint32_t resolution() const { return m_resolution; }
	uint32_t viewCount() const { return m_views; }

	struct BillboardVertex {
		glm::vec3 position;
		glm::vec2 uv;
	};

	static std::vector<BillboardVertex> makeQuad(const glm::vec3& center, float size,
	                                             const glm::vec3& camRight, const glm::vec3& camUp) {
		glm::vec3 halfRight = camRight * size * 0.5f;
		glm::vec3 halfUp = camUp * size * 0.5f;

		std::vector<BillboardVertex> verts = {
			{center - halfRight - halfUp, {0, 1}},
			{center + halfRight - halfUp, {1, 1}},
			{center + halfRight + halfUp, {1, 0}},
			{center - halfRight + halfUp, {0, 0}},
		};
		return verts;
	}

private:
	uint32_t m_resolution = 64;
	uint32_t m_views = 8;
	std::vector<ImpostorView> m_viewMatrices;
};

} // namespace tucano::veg
