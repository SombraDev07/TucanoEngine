#include "Terrain/HeightmapBrush.h"

#include <algorithm>
#include <cmath>

namespace tucano::terrain {

BrushSystem::BrushSystem() = default;

float BrushSystem::falloff(float dist, float radius) const {
	if (dist >= radius) return 0.0f;
	float t = 1.0f - dist / radius;
	return t * t * (3.0f - 2.0f * t);
}

float BrushSystem::applyRaise(float h, float amount) const {
	return h + amount;
}

float BrushSystem::applySmooth(float /*center*/, float neighbor, float amount) const {
	return amount;
}

float BrushSystem::applyFlatten(float center, float h, float amount) const {
	return h + (center - h) * amount;
}

float BrushSystem::applyNoise(float h, float amount, int x, int z, uint32_t seed) const {
	float n = std::sin(float(x * 7 + z * 13 + seed)) * std::cos(float(x * 11 - z * 3 + seed * 2));
	return h + n * amount * 0.3f;
}

void BrushSystem::applyStroke(Heightmap& hm, float worldX, float worldZ, float radius, float strength, BrushTool tool) {
	uint32_t res = hm.resolution();
	float ws = hm.worldSize();
	float texelSize = ws / float(res);

	int radiusPx = std::max(1, int(radius / texelSize));
	int cx = int(worldX / texelSize);
	int cz = int(worldZ / texelSize);

	BrushStroke stroke;
	stroke.worldX = worldX;
	stroke.worldZ = worldZ;
	stroke.radius = radius;
	stroke.strength = strength;
	stroke.tool = tool;

	for (int dz = -radiusPx; dz <= radiusPx; ++dz) {
		for (int dx = -radiusPx; dx <= radiusPx; ++dx) {
			int px = cx + dx;
			int pz = cz + dz;
			if (px < 0 || px >= int(res) || pz < 0 || pz >= int(res)) continue;

			float dist = std::sqrt(float(dx * dx + dz * dz)) * texelSize;
			float f = falloff(dist, radius);
			if (f <= 0.0f) continue;

			float oldH = hm.sampleHeightNearest(px, pz);
			stroke.previousHeights.push_back(oldH);
			stroke.affectedIndices.push_back(pz * res + px);
		}
	}

	executeBrush(hm, stroke, true);
	pushUndo(std::move(stroke));
}

void BrushSystem::executeBrush(Heightmap& hm, BrushStroke& stroke, bool forward) {
	uint32_t res = hm.resolution();
	float ws = hm.worldSize();
	float texelSize = ws / float(res);

	int radiusPx = std::max(1, int(stroke.radius / texelSize));
	int cx = int(stroke.worldX / texelSize);
	int cz = int(stroke.worldZ / texelSize);

	size_t idx = 0;
	for (int dz = -radiusPx; dz <= radiusPx; ++dz) {
		for (int dx = -radiusPx; dx <= radiusPx; ++dx) {
			int px = cx + dx;
			int pz = cz + dz;
			if (px < 0 || px >= int(res) || pz < 0 || pz >= int(res)) continue;

			float dist = std::sqrt(float(dx * dx + dz * dz)) * texelSize;
			float f = falloff(dist, stroke.radius);
			if (f <= 0.0f) continue;

			float amount = f * stroke.strength * (forward ? 1.0f : -1.0f);

			if (forward) {
				stroke.previousHeights.push_back(hm.sampleHeightNearest(px, pz));
			}

			float h = stroke.previousHeights[idx];
			float newH = h;

			switch (stroke.tool) {
			case BrushTool::Raise:
				newH = applyRaise(h, amount);
				break;
			case BrushTool::Lower:
				newH = applyRaise(h, -amount);
				break;
			case BrushTool::Smooth: {
				float sum = h; int count = 1;
				for (int ndz = -1; ndz <= 1; ++ndz) {
					for (int ndx = -1; ndx <= 1; ++ndx) {
						if (ndx == 0 && ndz == 0) continue;
						int nx = px + ndx, nz = pz + ndz;
						if (nx >= 0 && nx < int(res) && nz >= 0 && nz < int(res)) {
							sum += hm.sampleHeightNearest(nx, nz);
							++count;
						}
					}
				}
				float avg = sum / float(count);
				newH = h + (avg - h) * amount;
				break;
			}
			case BrushTool::Flatten: {
				float centerH = hm.sampleHeightNearest(cx, cz);
				newH = applyFlatten(centerH, h, amount);
				break;
			}
			case BrushTool::Noise:
				newH = applyNoise(h, amount, px, pz, ++m_noiseSeed);
				break;
			}

			hm.setHeight(uint32_t(px), uint32_t(pz), newH);
			++idx;
		}
	}
}

void BrushSystem::pushUndo(BrushStroke stroke) {
	m_undoStack.push_back(std::move(stroke));
	while (m_undoStack.size() > m_maxDepth) {
		m_undoStack.erase(m_undoStack.begin());
	}
	m_redoStack.clear();
}

void BrushSystem::undo(Heightmap& hm) {
	if (m_undoStack.empty()) return;
	auto stroke = std::move(m_undoStack.back());
	m_undoStack.pop_back();

	for (size_t i = 0; i < stroke.affectedIndices.size(); ++i) {
		uint32_t idx = stroke.affectedIndices[i];
		uint32_t px = idx % hm.resolution();
		uint32_t pz = idx / hm.resolution();
		float oldH = stroke.previousHeights[i];
		hm.setHeight(px, pz, oldH);
	}

	m_redoStack.push_back(std::move(stroke));
}

void BrushSystem::redo(Heightmap& hm) {
	if (m_redoStack.empty()) return;
	auto stroke = std::move(m_redoStack.back());
	m_redoStack.pop_back();

	executeBrush(hm, stroke, true);

	m_undoStack.push_back(std::move(stroke));
}

void BrushSystem::applyCrater(Heightmap& hm, float worldX, float worldZ, float radius, float depth) {
	uint32_t res = hm.resolution();
	float ws = hm.worldSize();
	float texelSize = ws / float(res);

	int radiusPx = std::max(1, int(radius / texelSize));
	int cx = int(worldX / texelSize);
	int cz = int(worldZ / texelSize);

	BrushStroke stroke;
	stroke.worldX = worldX;
	stroke.worldZ = worldZ;
	stroke.radius = radius;
	stroke.strength = depth;
	stroke.tool = BrushTool::Raise;

	float rimHeight = depth * 0.15f;

	for (int dz = -radiusPx; dz <= radiusPx; ++dz) {
		for (int dx = -radiusPx; dx <= radiusPx; ++dx) {
			int px = cx + dx;
			int pz = cz + dz;
			if (px < 0 || px >= int(res) || pz < 0 || pz >= int(res)) continue;

			float dist = std::sqrt(float(dx * dx + dz * dz)) * texelSize;
			if (dist >= radius) continue;

			float t = dist / radius;
			float craterShape = std::cos(t * 1.5707963f);
			float rimFactor = std::exp(-t * t * 4.0f) * (1.0f - t) * 0.3f;
			float amount = -depth * craterShape + rimHeight * rimFactor;

			float oldH = hm.sampleHeightNearest(px, pz);
			stroke.previousHeights.push_back(oldH);
			stroke.affectedIndices.push_back(pz * res + px);
			hm.setHeight(uint32_t(px), uint32_t(pz), oldH + amount);
		}
	}

	pushUndo(std::move(stroke));
}

} // namespace tucano::terrain
