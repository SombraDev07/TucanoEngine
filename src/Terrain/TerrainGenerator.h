#pragma once

#include "Core/TypeSystem/ReflectionMacros.h"
#include "Terrain/Heightmap.h"

#include <glm/glm.hpp>

#include <memory>
#include <random>
#include <vector>

namespace tucano::terrain {

// What a terrain is made of, before anyone sculpts it (F-01).
//
// Reflected, which buys the same three things it bought the sky and the grading: a panel nobody
// writes UI for, a block the `.tuscene` saves, and a value that survives closing the editor. The
// nine numbers below *are* the terrain — the heightmap is derived from them — so saving them is
// saving the landscape, at a few hundred bytes instead of a few megabytes.
//
// Sculpting (F-02) is what breaks that equivalence: the moment somebody raises a hill by hand the
// heightmap stops being a function of these parameters and has to be stored as data. That is a
// different task and a different format; this one covers the generated landscape.
struct TUCANO_TYPE() TerrainGenParams {
	TUCANO_FIELD(.label = "Resolution",
	             .tooltip = "Height samples per side. Doubling this costs four times the memory and "
	                        "the generation time",
	             .category = "Terrain", .minValue = 64.0f, .maxValue = 4096.0f, .step = 64.0f)
	uint32_t resolution = 512;

	TUCANO_FIELD(.label = "World size", .tooltip = "Metres the terrain spans, edge to edge",
	             .category = "Terrain", .minValue = 64.0f, .maxValue = 16384.0f, .step = 64.0f)
	float worldSize = 1024.0f;

	TUCANO_FIELD(.label = "Seed",
	             .tooltip = "Same seed and same settings give the same landscape, on any machine",
	             .category = "Terrain", .step = 1.0f)
	uint32_t seed = 42;

	TUCANO_FIELD(.label = "Octaves",
	             .tooltip = "Layers of noise summed together; each adds finer detail and costs another "
	                        "pass over every sample",
	             .category = "Noise", .minValue = 1.0f, .maxValue = 12.0f, .step = 1.0f)
	uint32_t octaves = 6;

	TUCANO_FIELD(.label = "Persistence",
	             .tooltip = "How much each octave contributes next to the one before it. Low is smooth "
	                        "and rolling, high is rough",
	             .category = "Noise", .minValue = 0.0f, .maxValue = 1.0f, .step = 0.01f)
	float persistence = 0.5f;

	TUCANO_FIELD(.label = "Lacunarity", .tooltip = "How fast the frequency climbs per octave",
	             .category = "Noise", .minValue = 1.0f, .maxValue = 4.0f, .step = 0.05f)
	float lacunarity = 2.0f;

	TUCANO_FIELD(.label = "Base frequency",
	             .tooltip = "Scale of the largest shapes. High turns mountains into hills",
	             .category = "Noise", .minValue = 0.25f, .maxValue = 32.0f, .step = 0.25f)
	float baseFrequency = 4.0f;

	TUCANO_FIELD(.label = "Amplitude", .tooltip = "Metres from valley floor to peak",
	             .category = "Height", .minValue = 1.0f, .maxValue = 2048.0f, .step = 1.0f)
	float baseAmplitude = 128.0f;

	TUCANO_FIELD(.label = "Base height", .tooltip = "Vertical offset of the whole terrain, in metres",
	             .category = "Height", .minValue = -1000.0f, .maxValue = 1000.0f, .step = 1.0f)
	float baseHeight = 0.0f;
};

class TerrainGenerator {
public:
	static std::shared_ptr<Heightmap> generate(rhi::Device& device, const TerrainGenParams& params);

	static std::vector<float> generateFbm(const TerrainGenParams& params);
	static std::vector<float> generateRidged(const TerrainGenParams& params);

private:
	static void initPermutation(std::vector<int>& perm, uint32_t seed);
	static float fade(float t);
	static float lerp(float a, float b, float t);
	static float grad(int hash, float x, float y);
	static float noise2D(float x, float y, const std::vector<int>& perm);
	static float fbm2D(float x, float y, const TerrainGenParams& params, const std::vector<int>& perm);
	static float ridged2D(float x, float y, const TerrainGenParams& params, const std::vector<int>& perm);
};

} // namespace tucano::terrain
