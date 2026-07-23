#pragma once

// Celestial bodies: sun and moon positions, sidereal star-field orientation, and the bright-star
// catalogue that feeds the night sky.
//
// Coordinate conventions used throughout:
//   * Equatorial frame — x toward the vernal equinox, z toward the north celestial pole.
//     Star catalogues are expressed here and it does not move, which is what lets the whole
//     catalogue be baked once and rotated with a single matrix.
//   * World frame — the engine's left-handed space: +x east, +y up, +z north.
//
// The bodies use a low-precision model in the spirit of Meeus' "low accuracy" formulae. Structure
// is correct — the synodic month falls out of the sidereal month rather than being hardcoded, and
// lunar phase follows from elongation rather than being an independent dial — but periodic terms
// (evection, variation, nutation) are omitted. Expect the moon to be a few degrees off a real
// ephemeris. That is invisible in a game and keeps this to one cheap function per frame.

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace tucano {

/// Observer's place and moment. Everything else is derived from this.
struct SkyEpoch {
  /// Apparent solar time: 0 = midnight, 0.5 = noon, whatever the date. Longitude is absent on
  /// purpose — with a local-solar clock it is already accounted for, and adding it would break
  /// the guarantee that 0.5 puts the sun overhead.
  float timeOfDay = 0.5f;
  float dayOfYear = 172.0f; ///< 0..365. 172 is the June solstice.
  float latitudeDeg = -23.5f;
};

struct CelestialState {
  glm::vec3 sunDir{0.0f, -1.0f, 0.0f};  ///< From the sun toward the scene (matches light direction).
  glm::vec3 moonDir{0.0f, -1.0f, 0.0f}; ///< From the moon toward the scene.

  /// 0 = new, 0.5 = full, 1 = new again. The visible phase, driving the disc's terminator.
  float moonPhase = 0.5f;
  /// Fraction of the disc that is lit, (1 - cos elongation) / 2. Drives how much light we get.
  float moonIllumination = 1.0f;

  /// World → equatorial. Applied to a view ray, it lands in the frame the catalogue lives in, so
  /// the star field needs no per-star work at runtime.
  glm::mat3 worldToEquatorial{1.0f};
};

CelestialState computeCelestial(const SkyEpoch& epoch);

/// One catalogue entry, as stored on disk.
struct CatalogStar {
  float raHours = 0.0f;
  float decDegrees = 0.0f;
  float magnitude = 0.0f;
  float colorIndexBV = 0.0f;
};

/// Parses the text catalogue at `path`. Returns an empty vector when the file is missing or has no
/// usable rows; the caller falls back to the procedural star field.
std::vector<CatalogStar> loadStarCatalog(const std::string& path);

/// Converts a Johnson B-V colour index to linear RGB, by way of an effective temperature
/// (Ballesteros' formula) and a blackbody fit. Hot stars come out blue, cool ones orange.
glm::vec3 starColorFromBV(float bv);

/// Radiance multiplier for an apparent magnitude, normalised so magnitude 0 gives 1.0.
/// The scale is logarithmic and inverted: five magnitudes are exactly a factor of 100.
float starRadianceFromMagnitude(float magnitude);

/// GPU-side layout for one star. Direction is pre-computed in the equatorial frame so the shader
/// only does a dot product.
struct StarGPU {
  glm::vec3 direction{0.0f, 0.0f, 1.0f};
  float radiance = 1.0f;
  glm::vec3 color{1.0f};
  float twinkleSeed = 0.0f;
};

/// A catalogue binned into an equatorial lat-long grid, so a shader can find the handful of stars
/// near a view direction without walking the whole list.
struct StarGrid {
  static constexpr uint32_t kCellsU = 128; ///< around the equator
  static constexpr uint32_t kCellsV = 64;  ///< pole to pole

  std::vector<StarGPU> stars;         ///< reordered so each cell's stars are contiguous
  std::vector<glm::uvec2> cellRanges; ///< kCellsU * kCellsV entries of (firstStar, count)
};

/// Bins `catalog` into the grid. Stars are sorted brightest-first inside each cell so the shader
/// can stop early on a budget without dropping the stars that matter.
StarGrid buildStarGrid(const std::vector<CatalogStar>& catalog);

} // namespace tucano
