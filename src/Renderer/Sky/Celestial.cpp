#include "Renderer/Sky/Celestial.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

namespace tucano {
namespace {

constexpr float kDeg2Rad = 0.01745329251f;
constexpr float kObliquityDeg = 23.4392911f; ///< Earth's axial tilt at J2000.

/// Days per sidereal month — the Moon's orbit relative to the fixed stars.
constexpr float kSiderealMonth = 27.321661f;
/// Days per sidereal year.
constexpr float kSiderealYear = 365.256363f;
/// The Moon's orbital inclination to the ecliptic.
constexpr float kMoonInclinationDeg = 5.145f;

/// Ecliptic longitude/latitude to an equatorial unit vector.
/// Equatorial basis: x = vernal equinox, z = north celestial pole.
glm::vec3 eclipticToEquatorial(float lonRad, float latRad) {
  const float eps = kObliquityDeg * kDeg2Rad;
  const float cosLat = std::cos(latRad);
  const glm::vec3 ecl{cosLat * std::cos(lonRad), cosLat * std::sin(lonRad), std::sin(latRad)};
  // Rotate about the x axis by the obliquity: the ecliptic plane tilts into the equatorial one.
  return glm::vec3{ecl.x, ecl.y * std::cos(eps) - ecl.z * std::sin(eps),
                   ecl.y * std::sin(eps) + ecl.z * std::cos(eps)};
}

glm::vec3 raDecToEquatorial(float raHours, float decDegrees) {
  const float ra = raHours * 15.0f * kDeg2Rad;
  const float dec = decDegrees * kDeg2Rad;
  const float cosDec = std::cos(dec);
  return glm::vec3{cosDec * std::cos(ra), cosDec * std::sin(ra), std::sin(dec)};
}

} // namespace

CelestialState computeCelestial(const SkyEpoch& epoch) {
  CelestialState out;

  // Days elapsed since the epoch's start of year, with the fractional day from the clock. Only
  // differences matter here, so any consistent origin works.
  const float days = epoch.dayOfYear + epoch.timeOfDay;

  // ── Sun ────────────────────────────────────────────
  // Apparent ecliptic longitude, measured from the vernal equinox. Day 80 is roughly the March
  // equinox, where the longitude is zero by definition.
  const float sunLon = ((days - 80.0f) / kSiderealYear) * glm::two_pi<float>();
  const glm::vec3 sunEq = eclipticToEquatorial(sunLon, 0.0f); // the Sun defines the ecliptic

  // ── Moon ───────────────────────────────────────────
  // The Moon runs around the ecliptic once per sidereal month. Because the Sun is also moving, the
  // elongation between them cycles more slowly — that difference IS the synodic month (29.53 d),
  // and it emerges here rather than being written down.
  const float moonLon = (days / kSiderealMonth) * glm::two_pi<float>();
  // Node regression is ignored; a fixed node is enough to keep the moon off the ecliptic plane.
  const float moonLat = kMoonInclinationDeg * kDeg2Rad * std::sin(moonLon * 1.1f);
  const glm::vec3 moonEq = eclipticToEquatorial(moonLon, moonLat);

  const float elongation = moonLon - sunLon;
  out.moonIllumination = 0.5f * (1.0f - std::cos(elongation));
  // Phase as a 0..1 cycle: 0 and 1 are new, 0.5 is full. Sign carries waxing vs waning, which the
  // shader needs to put the terminator on the correct limb.
  out.moonPhase = std::fmod(elongation / glm::two_pi<float>() + 10.0f, 1.0f);

  // ── Observer's horizon frame ───────────────────────
  // timeOfDay is apparent solar time — what a sundial reads — because that is the contract the
  // rest of the engine assumes: 0 is midnight, 0.5 is noon, and the sun must be overhead there
  // whatever the date. So derive sidereal time FROM the sun rather than the other way round.
  //
  // Hour angle is by definition LST - RA, and the sun's hour angle is fixed by the clock, so
  // LST = sunRA + sunHourAngle. The four-minutes-a-day sidereal drift is not written down
  // anywhere here: it emerges because the sun's own right ascension advances through the year,
  // carrying LST with it while the stars stay put. That is exactly why constellations are
  // seasonal.
  const float sunRA = std::atan2(sunEq.y, sunEq.x);
  const float sunHourAngle = (epoch.timeOfDay - 0.5f) * glm::two_pi<float>();
  const float lstRad = sunRA + sunHourAngle;
  const float latRad = epoch.latitudeDeg * kDeg2Rad;

  const float sinLat = std::sin(latRad);
  const float cosLat = std::cos(latRad);
  const float sinLst = std::sin(lstRad);
  const float cosLst = std::cos(lstRad);

  // The horizon basis written in equatorial coordinates. The zenith leans away from the pole by
  // the observer's latitude, which is the whole reason stars circle the pole at the poles and rise
  // vertically at the equator.
  const glm::vec3 up{cosLat * cosLst, cosLat * sinLst, sinLat};
  const glm::vec3 north{-sinLat * cosLst, -sinLat * sinLst, cosLat};
  const glm::vec3 east{-sinLst, cosLst, 0.0f};

  // Equatorial → world (+x east, +y up, +z north): project onto that basis.
  const glm::mat3 equatorialToWorld{
      glm::vec3{east.x, up.x, north.x},
      glm::vec3{east.y, up.y, north.y},
      glm::vec3{east.z, up.z, north.z},
  };
  out.worldToEquatorial = glm::transpose(equatorialToWorld); // orthonormal, so transpose inverts

  // Both bodies point FROM the body TOWARD the scene, matching how directional lights are stored.
  out.sunDir = -glm::normalize(equatorialToWorld * sunEq);
  out.moonDir = -glm::normalize(equatorialToWorld * moonEq);
  return out;
}

std::vector<CatalogStar> loadStarCatalog(const std::string& path) {
  std::vector<CatalogStar> stars;
  std::ifstream file(path);
  if (!file) {
    return stars;
  }

  std::string line;
  size_t lineNumber = 0;
  while (std::getline(file, line)) {
    ++lineNumber;
    if (line.empty() || line[0] == '#') continue;

    std::istringstream row(line);
    std::string name;
    CatalogStar s{};
    if (!(row >> name >> s.raHours >> s.decDegrees >> s.magnitude >> s.colorIndexBV)) {
      std::cout << "[Stars] skipping malformed row " << lineNumber << " of " << path << "\n";
      continue;
    }
    stars.push_back(s);
  }
  return stars;
}

glm::vec3 starColorFromBV(float bv) {
  // Ballesteros (2012): a closed form for effective temperature from B-V, good across the range
  // real stars occupy.
  const float b = std::clamp(bv, -0.4f, 2.0f);
  const float t = 4600.0f * (1.0f / (0.92f * b + 1.7f) + 1.0f / (0.92f * b + 0.62f));

  // Blackbody to linear RGB, normalised so the result stays near unit brightness. This is the
  // usual piecewise fit; precision beyond "blue stars look blue" buys nothing at these sizes.
  const float x = std::clamp(t, 1000.0f, 40000.0f) / 100.0f;
  glm::vec3 c{1.0f};
  c.r = x <= 66.0f ? 1.0f : std::clamp(1.292936f * std::pow(x - 60.0f, -0.1332047f), 0.0f, 1.0f);
  c.g = x <= 66.0f ? std::clamp(0.3900816f * std::log(x) - 0.6318414f, 0.0f, 1.0f)
                   : std::clamp(1.129891f * std::pow(x - 60.0f, -0.0755148f), 0.0f, 1.0f);
  if (x >= 66.0f) {
    c.b = 1.0f;
  } else if (x <= 19.0f) {
    c.b = 0.0f;
  } else {
    c.b = std::clamp(0.5432068f * std::log(x - 10.0f) - 1.19625408f, 0.0f, 1.0f);
  }

  // sRGB-ish curve out, linear in. Keep some floor so no channel goes fully black.
  c = glm::vec3{std::pow(c.r, 2.2f), std::pow(c.g, 2.2f), std::pow(c.b, 2.2f)};
  return glm::max(c, glm::vec3(0.05f));
}

float starRadianceFromMagnitude(float magnitude) {
  // m2 - m1 = -2.5 log10(F2 / F1). Five magnitudes is a factor of 100 exactly.
  return std::pow(10.0f, -0.4f * magnitude);
}

StarGrid buildStarGrid(const std::vector<CatalogStar>& catalog) {
  StarGrid grid;
  const uint32_t cellCount = StarGrid::kCellsU * StarGrid::kCellsV;
  grid.cellRanges.assign(cellCount, glm::uvec2{0u, 0u});
  if (catalog.empty()) {
    return grid;
  }

  struct Binned {
    uint32_t cell;
    StarGPU star;
    float magnitude;
  };

  std::vector<Binned> binned;
  binned.reserve(catalog.size());
  for (size_t i = 0; i < catalog.size(); ++i) {
    const CatalogStar& s = catalog[i];
    StarGPU gpu{};
    gpu.direction = raDecToEquatorial(s.raHours, s.decDegrees);
    gpu.radiance = starRadianceFromMagnitude(s.magnitude);
    gpu.color = starColorFromBV(s.colorIndexBV);
    // A per-star offset so neighbours do not scintillate in lockstep.
    gpu.twinkleSeed = static_cast<float>(i) * 2.399963f;

    // Bin on the equatorial direction. The grid is uniform in longitude and in sin(latitude), so
    // cells near the poles do not collapse to slivers.
    const float lon = std::atan2(gpu.direction.y, gpu.direction.x); // -pi..pi
    const float u = (lon / glm::two_pi<float>()) + 0.5f;
    const float v = gpu.direction.z * 0.5f + 0.5f; // sin(dec) mapped to 0..1
    const uint32_t cu = std::min(StarGrid::kCellsU - 1u,
                                 static_cast<uint32_t>(u * static_cast<float>(StarGrid::kCellsU)));
    const uint32_t cv = std::min(StarGrid::kCellsV - 1u,
                                 static_cast<uint32_t>(v * static_cast<float>(StarGrid::kCellsV)));

    binned.push_back(Binned{cv * StarGrid::kCellsU + cu, gpu, s.magnitude});
  }

  // Cell-major, then brightest-first within a cell. A shader that runs out of budget then drops
  // the faintest stars in that patch of sky rather than an arbitrary set.
  std::sort(binned.begin(), binned.end(), [](const Binned& a, const Binned& b) {
    if (a.cell != b.cell) return a.cell < b.cell;
    return a.magnitude < b.magnitude;
  });

  grid.stars.reserve(binned.size());
  for (uint32_t i = 0; i < binned.size(); ++i) {
    const uint32_t cell = binned[i].cell;
    if (grid.cellRanges[cell].y == 0u) {
      grid.cellRanges[cell].x = i;
    }
    ++grid.cellRanges[cell].y;
    grid.stars.push_back(binned[i].star);
  }
  return grid;
}

} // namespace tucano
