#pragma once

// Sky, sun, moon, stars and the air between them — pulled out of RendererSettings (E-01).
//
// Two reasons for the move, and neither is tidiness:
//
// 1. **A serialisation boundary.** A scene wants to save what time of day it is. It does not want
//    to save whether meshlets are on — that is a property of the machine drawing the scene, not of
//    the scene. While these fields sat in the same struct as `enableVisibilityBuffer` there was no
//    honest way to write "the environment" to a file. E-02 depends on this split existing.
//
// 2. **The panel stops being hand-written.** The old Environment panel wrote a widget per field by
//    hand, so it covered 46 of RendererSettings' 67 fields and drifted further with every field
//    added — that is where the 21 unreachable settings came from. Reflected, a field added here
//    shows up in the editor with its range and tooltip and cannot be forgotten.

#include "Core/TypeSystem/ReflectionMacros.h"

#include <glm/glm.hpp>

#include <string>

namespace tucano {

struct TUCANO_TYPE() SkyParams {
	// ── Atmosphere ───────────────────────────────────────────────────────────
	TUCANO_FIELD(.label = "Enabled", .tooltip = "Master switch for the sky and aerial perspective",
	             .category = "Atmosphere")
	bool enableAtmosphere = true;

	TUCANO_FIELD(.label = "Bruneton model",
	             .tooltip = "Physically-based precomputed scattering. Off falls back to the cheaper "
	                        "artistic Nishita sky",
	             .category = "Atmosphere")
	bool useBrunetonAtmosphere = true;

	TUCANO_FIELD(.label = "Drives sun",
	             .tooltip = "Time of day aims the directional light. Off leaves the sun wherever the "
	                        "scene put it, which is what you want when a scene lights itself",
	             .category = "Atmosphere")
	bool atmosphereDrivesSun = true;

	TUCANO_FIELD(.label = "Time of day",
	             .tooltip = "0 = midnight, 0.25 = sunrise, 0.5 = noon, 0.75 = sunset",
	             .category = "Atmosphere", .minValue = 0.0f, .maxValue = 1.0f, .step = 0.001f)
	float timeOfDay = 0.38f;

	TUCANO_FIELD(.label = "Turbidity", .tooltip = "1 = clear alpine air, 8 = hazy city",
	             .category = "Atmosphere", .minValue = 1.0f, .maxValue = 10.0f, .step = 0.05f)
	float turbidity = 2.8f;

	// ── Height fog ───────────────────────────────────────────────────────────
	// Distinct from the Fog panel: that one is the froxel volumetric pass. This is the exponential
	// height fog folded into the sky pass, and the two are added, not alternatives.
	TUCANO_FIELD(.label = "Density",
	             .tooltip = "Exponential height fog in the sky pass. The Fog panel is the separate "
	                        "volumetric (froxel) pass — the two add together",
	             .category = "Height fog", .minValue = 0.0f, .maxValue = 0.1f, .step = 0.0005f)
	float fogDensity = 0.012f;

	TUCANO_FIELD(.label = "Height", .tooltip = "Metres over which height fog falls off",
	             .category = "Height fog", .minValue = 0.0f, .maxValue = 400.0f, .step = 1.0f)
	float fogHeight = 40.0f;

	// ── Wind ─────────────────────────────────────────────────────────────────
	TUCANO_FIELD(.label = "Wind",
	             .tooltip = "World-space wind. Moves clouds, rain streaks and vegetation",
	             .category = "Wind", .step = 0.01f)
	glm::vec3 wind = {0.2f, 0.0f, 0.05f};

	// ── Place and date ───────────────────────────────────────────────────────
	// These are not decoration: they decide where the sun rises, which constellations are up, and
	// what phase the moon is in.
	TUCANO_FIELD(.label = "Latitude",
	             .tooltip = "Degrees. Drives the sun's arc and the sidereal rotation of the stars",
	             .category = "Place and date", .minValue = -90.0f, .maxValue = 90.0f, .step = 0.1f)
	float latitudeDeg = -23.55f; // Sao Paulo

	TUCANO_FIELD(.label = "Day of year",
	             .tooltip = "Drives the season and the lunar phase, since phase is elongation from "
	                        "the sun. 156 lands on a full moon in this model",
	             .category = "Place and date", .minValue = 1.0f, .maxValue = 365.0f, .step = 1.0f)
	float dayOfYear = 156.0f;

	// ── Moon ─────────────────────────────────────────────────────────────────
	TUCANO_FIELD(.label = "Enabled", .category = "Moon")
	bool enableMoon = true;

	TUCANO_FIELD(.label = "Moonlight",
	             .tooltip = "Peak intensity at full moon and zenith. Two orders of magnitude under "
	                        "the sun on purpose: real moonlight is ~400,000x weaker, but a night "
	                        "that dark is unplayable. This is the dial for how far to cheat",
	             .category = "Moon", .minValue = 0.0f, .maxValue = 0.5f, .step = 0.001f)
	float moonIntensity = 0.045f;

	TUCANO_FIELD(.label = "Disc brightness", .tooltip = "How bright the moon's disc itself reads",
	             .category = "Moon", .minValue = 0.0f, .maxValue = 10.0f, .step = 0.05f)
	float moonDiscBrightness = 2.5f;

	TUCANO_FIELD(.label = "Angular radius",
	             .tooltip = "Degrees. The real moon is 0.26; going bigger is the oldest cheat in "
	                        "landscape art and it reads well",
	             .category = "Moon", .minValue = 0.05f, .maxValue = 3.0f, .step = 0.01f)
	float moonAngularRadiusDeg = 0.5f;

	// ── Stars ────────────────────────────────────────────────────────────────
	TUCANO_FIELD(.label = "Enabled", .category = "Stars")
	bool enableStars = true;

	TUCANO_FIELD(.label = "Intensity", .category = "Stars", .minValue = 0.0f, .maxValue = 3.0f,
	             .step = 0.02f)
	float starIntensity = 1.0f;

	TUCANO_FIELD(.label = "Twinkle", .tooltip = "Atmospheric scintillation",
	             .category = "Stars", .minValue = 0.0f, .maxValue = 1.0f, .step = 0.01f)
	float starTwinkle = 0.35f;

	TUCANO_FIELD(.label = "Star size",
	             .tooltip = "Angular radius in degrees before the sub-pixel clamp kicks in",
	             .category = "Stars", .minValue = 0.005f, .maxValue = 0.5f, .step = 0.005f)
	float starSizeDeg = 0.055f;

	TUCANO_FIELD(.label = "Purkinje shift",
	             .tooltip = "Blend toward blue in dim areas, as night vision does. 0 disables it",
	             .category = "Stars", .minValue = 0.0f, .maxValue = 1.0f, .step = 0.01f)
	float purkinjeStrength = 0.75f;

	// Changing this needs Renderer::buildStarCatalogTextures() to run, which the Sky panel does when
	// it sees the value move. Reflected anyway: a path nobody can see is a path nobody can fix.
	TUCANO_FIELD(.label = "Star catalogue",
	             .tooltip = "Bright-star list, relative to the engine assets directory. Falls back "
	                        "to the procedural star field when it cannot be read",
	             .category = "Stars", .assetKind = "text")
	std::string starCatalogPath = "Sky/bright_stars.txt";
};

} // namespace tucano
