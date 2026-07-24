#pragma once

#include "Terrain/Heightmap.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace tucano::terrain {

enum class BrushTool : uint32_t {
	Raise = 0,
	Lower = 1,
	Smooth = 2,
	Flatten = 3,
	Noise = 4,
};

struct BrushStroke {
	uint32_t centerX, centerZ;
	float worldX, worldZ;
	float radius;
	float strength;
	BrushTool tool;
	std::vector<float> previousHeights;
	std::vector<uint32_t> affectedIndices;
};

class BrushSystem {
public:
	BrushSystem();

	void applyStroke(Heightmap& hm, float worldX, float worldZ, float radius, float strength, BrushTool tool);
	void undo(Heightmap& hm);
	void redo(Heightmap& hm);
	bool canUndo() const { return !m_undoStack.empty(); }
	bool canRedo() const { return !m_redoStack.empty(); }
	size_t undoCount() const { return m_undoStack.size(); }
	size_t redoCount() const { return m_redoStack.size(); }

	void setMaxUndoDepth(uint32_t depth) { m_maxDepth = depth; }

	void applyCrater(Heightmap& hm, float worldX, float worldZ, float radius, float depth);

private:
	void executeBrush(Heightmap& hm, BrushStroke& stroke, bool forward);
	void pushUndo(BrushStroke stroke);
	float falloff(float dist, float radius) const;
	float applyRaise(float h, float amount) const;
	float applySmooth(float center, float neighbor, float amount) const;
	float applyFlatten(float center, float h, float amount) const;
	float applyNoise(float h, float amount, int x, int z, uint32_t seed) const;

	std::vector<BrushStroke> m_undoStack;
	std::vector<BrushStroke> m_redoStack;
	uint32_t m_maxDepth = 32;
	uint32_t m_noiseSeed = 0;
};

} // namespace tucano::terrain
