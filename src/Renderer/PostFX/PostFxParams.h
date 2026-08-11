#pragma once

// Tonemapping, bloom, ambient occlusion and exposure — pulled out of RendererSettings (E-04).
//
// Same move E-01 made with the sky and E-05 with the clouds, and for the same reason: **a scene
// wants to save how it is graded.** Bloom strength and an exposure target are decisions somebody
// makes about how the game looks; they are not properties of the machine drawing it. While they sat
// in the same struct as `enableMeshShaders` there was no honest way to write them to a file, so
// every one of them died with the process.
//
// ## Why not "serialise the non-advanced half of RendererSettings"
//
// That was the cheaper idea on the roadmap, and it is wrong. `.advanced` answers *do I hide this
// from an artist?* — not *does this belong to the scene?* The two disagree on real fields:
//
//   * `enableRTShadows` / `enableRTReflections` are not advanced, and are described as
//     "auto-enabled when the device has it". Writing them into a scene means a scene authored on a
//     DXR machine opens with ray tracing requested on one without it.
//   * `giTier` is not advanced, and is literally how much of the GI budget to spend.
//   * `shadowMapSize` is not advanced, and doubling it costs four times the memory.
//
// A filter would have written all three into every scene file. Moving the fields that genuinely
// belong to the scene is more work and says what it means.

#include "Core/TypeSystem/ReflectionMacros.h"

namespace tucano {

struct TUCANO_TYPE() PostFxParams {
	// ── Tonemap and bloom ────────────────────────────────────────────────────
	TUCANO_FIELD(.label = "Tonemap",
	             .tooltip = "HDR to display. Off shows raw linear values and looks blown out — a debugging view",
	             .category = "Post processing")
	bool enableTonemap = true;

	TUCANO_FIELD(.label = "Bloom", .category = "Post processing")
	bool enableBloom = true;

	TUCANO_FIELD(.label = "Bloom strength", .category = "Post processing", .minValue = 0.0f,
	             .maxValue = 2.0f, .step = 0.01f)
	float bloomStrength = 0.28f;

	// ── Ambient occlusion ────────────────────────────────────────────────────
	TUCANO_FIELD(.label = "Ambient occlusion", .tooltip = "GTAO", .category = "Post processing")
	bool enableAO = true;

	TUCANO_FIELD(.label = "AO radius", .tooltip = "World-space metres sampled around each pixel",
	             .category = "Post processing", .minValue = 0.1f, .maxValue = 4.0f, .step = 0.05f)
	float aoRadius = 0.9f;

	TUCANO_FIELD(.label = "AO intensity", .category = "Post processing", .minValue = 0.0f,
	             .maxValue = 3.0f, .step = 0.05f)
	float aoIntensity = 1.0f;

	// ── Exposure ─────────────────────────────────────────────────────────────
	TUCANO_FIELD(.label = "Auto exposure",
	             .tooltip = "Adapts to scene brightness, as an eye does. Off holds a fixed exposure",
	             .category = "Exposure")
	bool enableAutoExposure = true;

	TUCANO_FIELD(.label = "Target",
	             .tooltip = "Middle-grey the auto exposure aims for. 0.18 is the photographic standard",
	             .category = "Exposure", .minValue = 0.01f, .maxValue = 1.0f, .step = 0.01f)
	float exposureTarget = 0.18f;

	TUCANO_FIELD(.label = "Adaptation",
	             .tooltip = "How fast the eye adjusts. Low is slow and cinematic, high snaps",
	             .category = "Exposure", .minValue = 0.01f, .maxValue = 1.0f, .step = 0.01f)
	float exposureAdapt = 0.1f;

	TUCANO_FIELD(.label = "Minimum",
	             .tooltip = "Floor on auto exposure; stops a dark room from being lifted to daylight",
	             .category = "Exposure", .minValue = 0.001f, .maxValue = 4.0f, .step = 0.01f)
	float exposureMin = 0.08f;

	TUCANO_FIELD(.label = "Maximum",
	             .tooltip = "Ceiling on auto exposure; stops a bright sky from being crushed",
	             .category = "Exposure", .minValue = 0.01f, .maxValue = 32.0f, .step = 0.05f)
	float exposureMax = 4.0f;
};

} // namespace tucano
