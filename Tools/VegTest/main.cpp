// Gate for the vegetation LOD2 impostor bake.
//
// The bake used to paint fixed ellipses, so every plant looked the same at distance no matter what
// its mesh was. This checks the opposite property: two different meshes must produce two different
// silhouettes, the silhouette must track the mesh's real proportions, and rotating the yaw must
// change the image. Runs headless — bakeImpostorPixels needs no device.
//
//   TucanoVegTest [--dump atlas.png]

#include "Vegetation/VegetationMeshes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

// TucanoRuntime already links the stb_image_write implementation (Runtime/StbImpl.cpp), so this
// only needs the declarations.
#include <stb_image_write.h>

using namespace tucano::veg;

namespace {

int g_failures = 0;

void check(bool cond, const std::string& what) {
	std::cout << (cond ? "  [ok]   " : "  [FAIL] ") << what << "\n";
	if (!cond) ++g_failures;
}

struct Geometry {
	std::vector<glm::vec3> positions;
	std::vector<uint32_t> indices;
};

/// Narrow, tall crossed cards — the shape of a grass tuft.
Geometry makeGrassGeometry() {
	Geometry g;
	const float hw = 0.06f, h = 0.55f;
	auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
		const uint32_t base = uint32_t(g.positions.size());
		g.positions.insert(g.positions.end(), {a, b, c, d});
		g.indices.insert(g.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
	};
	quad({-hw, 0, 0}, {hw, 0, 0}, {hw * 0.3f, h, 0}, {-hw * 0.3f, h, 0});
	quad({0, 0, -hw}, {0, 0, hw}, {0, h, hw * 0.3f}, {0, h, -hw * 0.3f});
	return g;
}

/// A squat, wide box — deliberately nothing like the grass tuft.
Geometry makeBushGeometry() {
	Geometry g;
	const float r = 0.5f, h = 0.6f;
	const glm::vec3 c[8] = {{-r, 0, -r}, {r, 0, -r}, {r, 0, r},  {-r, 0, r},
	                        {-r, h, -r}, {r, h, -r}, {r, h, r},  {-r, h, r}};
	const int faces[6][4] = {{0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1},
	                         {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}};
	for (auto& f : faces) {
		const uint32_t base = uint32_t(g.positions.size());
		for (int k = 0; k < 4; ++k) g.positions.push_back(c[f[k]]);
		g.indices.insert(g.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
	}
	return g;
}

struct SliceStats {
	float coverage = 0.0f;  // fraction of the slice with alpha > 0
	float meanWidth = 0.0f; // mean opaque run width, in slice widths
	float topRow = 1.0f;    // highest covered row, 0 = very top of the cell
	float bottomRow = 0.0f; // lowest covered row
};

SliceStats measureSlice(const std::vector<uint8_t>& px, uint32_t atlasSize, uint32_t x0,
                        uint32_t y0, uint32_t w, uint32_t h) {
	SliceStats s;
	uint32_t covered = 0, rowsWithAny = 0;
	float widthSum = 0.0f;
	bool anyAtAll = false;
	for (uint32_t y = 0; y < h; ++y) {
		uint32_t rowCovered = 0;
		for (uint32_t x = 0; x < w; ++x) {
			const size_t i = ((size_t(y0 + y) * atlasSize) + x0 + x) * 4;
			if (px[i + 3] > 32) ++rowCovered;
		}
		if (rowCovered > 0) {
			++rowsWithAny;
			widthSum += float(rowCovered) / float(w);
			const float v = float(y) / float(h);
			if (!anyAtAll) { s.topRow = v; anyAtAll = true; }
			s.bottomRow = v;
		}
		covered += rowCovered;
	}
	s.coverage = float(covered) / float(w * h);
	s.meanWidth = rowsWithAny ? widthSum / float(rowsWithAny) : 0.0f;
	return s;
}

/// Mean absolute alpha difference between two slices, 0..1.
float sliceDifference(const std::vector<uint8_t>& px, uint32_t atlasSize, uint32_t ax, uint32_t ay,
                      uint32_t bx, uint32_t by, uint32_t w, uint32_t h) {
	long long diff = 0;
	for (uint32_t y = 0; y < h; ++y) {
		for (uint32_t x = 0; x < w; ++x) {
			const size_t ia = ((size_t(ay + y) * atlasSize) + ax + x) * 4 + 3;
			const size_t ib = ((size_t(by + y) * atlasSize) + bx + x) * 4 + 3;
			diff += std::abs(int(px[ia]) - int(px[ib]));
		}
	}
	return float(diff) / (255.0f * float(w) * float(h));
}

} // namespace

int main(int argc, char** argv) {
	std::cout << std::unitbuf;
	std::string dumpPath;
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--dump" && i + 1 < argc) dumpPath = argv[++i];
	}

	const uint32_t atlasSize = 1024;
	const uint32_t views = 8;
	const uint32_t cell = atlasSize / kImpostorGrid;
	const uint32_t sliceW = cell / views;

	const Geometry grass = makeGrassGeometry();
	const Geometry bush = makeBushGeometry();

	std::vector<ImpostorSource> sources(3);
	sources[0].positions = &grass.positions;
	sources[0].indices = &grass.indices;
	sources[0].baseColor = {0.25f, 0.55f, 0.18f, 1.0f};
	sources[1].positions = &bush.positions;
	sources[1].indices = &bush.indices;
	sources[1].baseColor = {0.30f, 0.42f, 0.16f, 1.0f};
	// Third type intentionally has no geometry: must fall back, not crash or produce nothing.
	sources[2].baseColor = {0.35f, 0.50f, 0.20f, 1.0f};

	std::cout << "== impostor bake (" << atlasSize << "px atlas, " << kImpostorGrid << "x"
	          << kImpostorGrid << " grid, " << views << " views, " << sliceW << "x" << cell
	          << " per slice) ==\n";

	const std::vector<uint8_t> px = bakeImpostorPixels(sources, views, atlasSize);
	check(px.size() == size_t(atlasSize) * atlasSize * 4, "atlas buffer is the requested size");
	if (px.size() != size_t(atlasSize) * atlasSize * 4) return 1;

	const SliceStats grassStats = measureSlice(px, atlasSize, 0, 0, sliceW, cell);
	const SliceStats bushStats = measureSlice(px, atlasSize, cell, 0, sliceW, cell);
	const SliceStats emptyStats = measureSlice(px, atlasSize, 2 * cell, 0, sliceW, cell);

	check(grassStats.coverage > 0.02f,
	      "grass slice has coverage (" + std::to_string(grassStats.coverage) + ")");
	check(bushStats.coverage > 0.02f,
	      "bush slice has coverage (" + std::to_string(bushStats.coverage) + ")");
	check(emptyStats.coverage > 0.02f, "geometry-less type still gets a fallback silhouette");

	// The grass tuft is 0.55 m tall and 0.12 m wide; the bush is 0.6 m tall and 1.0 m wide. If
	// the bake follows the mesh, the bush must come out far wider relative to its height.
	check(bushStats.meanWidth > grassStats.meanWidth * 2.0f,
	      "silhouette follows mesh proportions: bush mean width " +
	          std::to_string(bushStats.meanWidth) + " > 2x grass " +
	          std::to_string(grassStats.meanWidth));

	// Anchoring: both plants stand on the ground, so coverage must reach the bottom of the cell.
	check(grassStats.bottomRow > 0.9f,
	      "grass is anchored to the base of the cell (bottom row " +
	          std::to_string(grassStats.bottomRow) + ")");
	check(bushStats.bottomRow > 0.9f, "bush is anchored to the base of the cell");

	// The bush is squat, so it must not reach as high in the UV box as the tall thin grass.
	check(bushStats.topRow > grassStats.topRow,
	      "squat bush occupies less of the cell height than the tall grass (" +
	          std::to_string(bushStats.topRow) + " vs " + std::to_string(grassStats.topRow) + ")");

	// Different meshes must not produce the same image — that was the old bug.
	const float acrossTypes = sliceDifference(px, atlasSize, 0, 0, cell, 0, sliceW, cell);
	check(acrossTypes > 0.05f,
	      "grass and bush bake to different silhouettes (mean alpha delta " +
	          std::to_string(acrossTypes) + ")");

	// Rotating the yaw must change the image. Compared against view 1 (45 deg), not view 2: the
	// crossed grass cards are symmetric under 90 deg, so views 0 and 2 are identical by
	// construction and would prove nothing either way.
	const float acrossViews = sliceDifference(px, atlasSize, 0, 0, sliceW, 0, sliceW, cell);
	check(acrossViews > 0.01f,
	      "yaw views differ for the grass tuft (mean alpha delta " +
	          std::to_string(acrossViews) + ")");

	// Nothing outside the used cells should have been written.
	bool cleanOutside = true;
	for (uint32_t y = cell; y < atlasSize && cleanOutside; ++y) {
		for (uint32_t x = 0; x < atlasSize; ++x) {
			if (px[(size_t(y) * atlasSize + x) * 4 + 3] != 0) { cleanOutside = false; break; }
		}
	}
	check(cleanOutside, "bake stays inside the rows it claims");

	if (!dumpPath.empty()) {
		stbi_write_png(dumpPath.c_str(), int(atlasSize), int(atlasSize), 4, px.data(),
		               int(atlasSize * 4));
		std::cout << "wrote " << dumpPath << "\n";
	}

	std::cout << "\n" << (g_failures == 0 ? "VEGETATION IMPOSTORS: PASS" : "VEGETATION IMPOSTORS: FAIL")
	          << " (" << g_failures << " failure(s))\n";
	return g_failures == 0 ? 0 : 1;
}
