#pragma once

// MaterialAsset — a material as a **file**, rather than as something a mesh importer left behind.
//
// B-05 of the roadmap, and step 3 of the Definition of Done ("create a material, adjust PBR, assign
// it to the mesh"). Until now a `Material` only existed inside a loaded model: two objects could
// not share one, editing it could not be saved, and there was nothing to assign.
//
// This is the *authoring* form. `Material` is the runtime one, and the difference is deliberate:
//   - a runtime material holds `shared_ptr<Texture>` — pointers to GPU objects that mean nothing
//     on disk and are rebuilt every run;
//   - an asset holds `AssetGuid` — identity that survives the texture being renamed, saved, moved
//     between machines and cooked.
// `toRuntime` is the one place that resolves one into the other, so nothing else has to know that
// two representations exist.
//
// Stored as `.tumat`, which is plain JSON written by the reflection serializer. It gets a `.tumeta`
// sidecar like any other source asset — a native format could carry its own id instead, but that
// would be a third way of answering "what is this asset's GUID", and two is already one too many.

#include "Core/AssetGuid.h"
#include "Core/TypeSystem/ReflectionMacros.h"

#include <glm/glm.hpp>

#include <string>

namespace tucano {

struct TUCANO_TYPE() MaterialAsset {
	TUCANO_FIELD(.label = "Name", .category = "Material")
	std::string name = "Material";

	// ── Surface ──
	TUCANO_FIELD(Color, .label = "Base color",
	             .tooltip = "Multiplied with the albedo texture when there is one",
	             .category = "Surface")
	glm::vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};

	TUCANO_FIELD(.label = "Metallic",
	             .tooltip = "0 is dielectric, 1 is raw metal; values in between are not physical",
	             .category = "Surface", .minValue = 0.0f, .maxValue = 1.0f)
	float metallicFactor = 1.0f;

	TUCANO_FIELD(.label = "Roughness", .tooltip = "Microfacet roughness of the surface",
	             .category = "Surface", .minValue = 0.0f, .maxValue = 1.0f)
	float roughnessFactor = 1.0f;

	TUCANO_FIELD(.label = "Ambient occlusion",
	             .tooltip = "Scales how much the baked AO map darkens indirect light",
	             .category = "Surface", .minValue = 0.0f, .maxValue = 1.0f)
	float aoFactor = 1.0f;

	TUCANO_FIELD(.label = "Reflectance",
	             .tooltip = "Dielectric F0. 0.5 is the 4% default most surfaces want",
	             .category = "Surface", .minValue = 0.0f, .maxValue = 1.0f)
	float reflectance = 0.5f;

	// ── Clearcoat ──
	TUCANO_FIELD(.label = "Clearcoat",
	             .tooltip = "Strength of the second specular lobe: car paint, lacquer, varnish",
	             .category = "Clearcoat", .minValue = 0.0f, .maxValue = 1.0f)
	float clearcoat = 0.0f;

	TUCANO_FIELD(.label = "Clearcoat roughness", .tooltip = "Roughness of the coat layer only",
	             .category = "Clearcoat", .minValue = 0.0f, .maxValue = 1.0f)
	float clearcoatRoughness = 0.1f;

	// ── Sheen ──
	TUCANO_FIELD(.label = "Fuzz", .tooltip = "Cloth sheen amount; 0 disables the lobe",
	             .category = "Sheen", .minValue = 0.0f, .maxValue = 1.0f)
	float fuzz = 0.0f;

	TUCANO_FIELD(Color, .label = "Fuzz color", .tooltip = "Tint of the sheen lobe",
	             .category = "Sheen")
	glm::vec3 fuzzColor{1.0f, 1.0f, 1.0f};

	// ── Emissive ──
	TUCANO_FIELD(Color, .label = "Emissive",
	             .tooltip = "Radiance added on top of shading; not tone mapped away at 1.0",
	             .category = "Emissive")
	glm::vec3 emissiveFactor{0.0f, 0.0f, 0.0f};

	// ── Detail ──
	TUCANO_FIELD(.label = "Detail scale",
	             .tooltip = "UV multiplier for the detail maps. 0 turns detail off",
	             .category = "Detail", .minValue = 0.0f, .maxValue = 64.0f)
	float detailScale = 0.0f;

	// ── Alpha ──
	TUCANO_FIELD(.label = "Alpha mask", .tooltip = "Cutout rather than blended transparency",
	             .category = "Alpha")
	bool alphaMask = false;

	TUCANO_FIELD(.label = "Alpha cutoff", .tooltip = "Only read when Alpha mask is on",
	             .category = "Alpha", .minValue = 0.0f, .maxValue = 1.0f)
	float alphaCutoff = 0.5f;

	// ── Textures ──
	// By identity, never by path: a texture renamed after the material was authored must not
	// silently unbind. An invalid GUID means "no texture", which is the honest default.
	TUCANO_FIELD(.label = "Albedo", .category = "Textures", .assetKind = "texture")
	asset::AssetGuid albedo;

	TUCANO_FIELD(.label = "Normal", .category = "Textures", .assetKind = "texture")
	asset::AssetGuid normal;

	TUCANO_FIELD(.label = "Metallic/roughness", .category = "Textures", .assetKind = "texture")
	asset::AssetGuid metallicRoughness;

	TUCANO_FIELD(.label = "Ambient occlusion", .category = "Textures", .assetKind = "texture")
	asset::AssetGuid ao;

	TUCANO_FIELD(.label = "Emissive map", .category = "Textures", .assetKind = "texture")
	asset::AssetGuid emissive;
};

} // namespace tucano
