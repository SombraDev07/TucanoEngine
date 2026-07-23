#ifndef TUCANO_CELESTIAL_HLSL
#define TUCANO_CELESTIAL_HLSL

// Moon disc and star field. Everything here runs only where the sky is visible, so the cost is
// paid on background pixels and nowhere else.
//
// All direction vectors are world space (+x east, +y up, +z north) and unit length. Directions
// named *ToScene point FROM the body toward the scene, matching how the engine stores directional
// lights, so `-moonToScene` is the direction you look to see the moon.

// ── Moon ─────────────────────────────────────────────

/// Radiance of the lunar disc along `V`, or zero off the disc.
///
/// `angularRadius` is the moon's apparent radius in radians (the real one is ~0.0045, a touch
/// over a quarter degree — the same as the sun's, which is why eclipses fit).
///
/// The phase is not painted on: the disc is shaded as a sphere lit from the sun's direction, so
/// the terminator lands where the geometry puts it and tracks the sun automatically.
float3 moonDisc(float3 V, float3 moonToScene, float3 sunToScene, float angularRadius,
                float3 moonColor, float brightness) {
  float3 toMoon = normalize(-moonToScene);
  float cosAngle = dot(V, toMoon);
  float cosEdge = cos(angularRadius);
  if (cosAngle <= cosEdge) {
    return 0.0;
  }

  // Position within the disc, normalised so the limb sits at radius 1.
  float3 up = abs(toMoon.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
  float3 T = normalize(cross(up, toMoon));
  float3 B = cross(toMoon, T);
  float sinR = max(sin(angularRadius), 1e-6);
  float2 p = float2(dot(V, T), dot(V, B)) / sinR;
  float r2 = saturate(dot(p, p));

  // Surface normal of the sphere at that point, facing back toward the viewer at the centre.
  float3 N = normalize(T * p.x + B * p.y - toMoon * sqrt(max(1.0 - r2, 0.0)));
  float3 L = normalize(-sunToScene);
  float3 viewToward = -V;

  float nDotL = dot(N, L);
  float nDotV = max(dot(N, viewToward), 1e-4);

  // Lommel-Seeliger rather than Lambert. Regolith backscatters hard, which is why a full moon
  // reads as a flat bright disc instead of a shaded ball — Lambert would darken the limb visibly
  // and look wrong.
  float lit = saturate(nDotL);
  float reflectance = lit > 0.0 ? lit / (lit + nDotV) : 0.0;

  // Earthshine: sunlight bounced off Earth faintly filling the unlit side. Strongest near new
  // moon, when Earth is "full" as seen from there.
  float earthshine = 0.018 * saturate(1.0 - lit) * saturate(1.0 - dot(L, toMoon) * 0.5);

  // Soften the limb over roughly a pixel so the edge does not stair-step.
  float edge = 1.0 - smoothstep(0.94, 1.0, sqrt(r2));

  return moonColor * brightness * (reflectance + earthshine) * edge;
}

/// Wide, faint halo around the moon — moonlight scattered by the atmosphere. Sold separately from
/// the disc because it must survive being much larger than the disc itself.
float3 moonGlow(float3 V, float3 moonToScene, float3 moonColor, float brightness) {
  float cosAngle = saturate(dot(V, normalize(-moonToScene)));
  float glow = pow(cosAngle, 900.0) * 0.35 + pow(cosAngle, 60.0) * 0.02;
  return moonColor * brightness * glow;
}

// ── Stars ────────────────────────────────────────────

float celestialHash(float3 p) {
  p = frac(p * 0.3183099 + float3(0.71, 0.113, 0.419));
  p *= 17.0;
  return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

/// Twinkle. Real scintillation comes from turbulence in the air column, so it is strongest near
/// the horizon where the line of sight passes through the most atmosphere, and nearly absent at
/// the zenith. Seeding per star keeps neighbours from pulsing together.
float starTwinkle(float seed, float time, float altitude, float amount) {
  float airmass = 1.0 / max(saturate(altitude) + 0.08, 0.08);
  float t = sin(time * 6.1 + seed) * 0.6 + sin(time * 11.3 + seed * 1.7) * 0.4;
  return 1.0 + t * amount * saturate(airmass * 0.22);
}

/// Cheap star field with no data behind it: hash the view direction on a grid and keep the cells
/// that clear a threshold. Used when no catalogue is loaded.
float3 proceduralStars(float3 V, float density, float time, float amount) {
  float3 grid = V * 340.0;
  float3 cell = floor(grid);
  float h = celestialHash(cell);
  if (h < 1.0 - density) {
    return 0.0;
  }

  // Jitter the star inside its cell so the field does not read as a lattice.
  float3 jitter = float3(celestialHash(cell + 11.0), celestialHash(cell + 23.0),
                         celestialHash(cell + 37.0)) - 0.5;
  float3 center = (cell + 0.5 + jitter * 0.8) / 340.0;
  float d2 = dot(V - center, V - center) * (340.0 * 340.0);
  float profile = exp(-d2 * 5.5);

  float brightness = pow(celestialHash(cell + 57.0), 6.0) * 1.4 + 0.05;
  float tint = celestialHash(cell + 91.0);
  float3 color = lerp(float3(0.75, 0.82, 1.0), float3(1.0, 0.86, 0.7), tint);
  return color * profile * brightness * starTwinkle(h * 100.0, time, V.y, amount);
}

/// Star field from the catalogue.
///
/// `worldToEq` rotates the view ray into the equatorial frame the catalogue lives in — that single
/// matrix is the sidereal rotation, so the whole sky turns without touching per-star data.
///
/// The grid is uniform in longitude and in sin(declination); cells stay roughly equal-area instead
/// of collapsing into slivers at the poles. Only the 3x3 neighbourhood is walked, which is enough
/// because a star's disc is far smaller than a cell.
float3 catalogStars(float3 V, float3x3 worldToEq, Texture2D cellTex, Texture2D dataTex,
                    uint dataWidth, float baseSigma, float pixelAngle, float time, float twinkle) {
  const uint kCellsU = 128u;
  const uint kCellsV = 64u;

  float3 E = normalize(mul(worldToEq, V));

  float lon = atan2(E.y, E.x);
  float u = lon * 0.15915494 + 0.5; // /(2pi) + 0.5
  float v = E.z * 0.5 + 0.5;
  int cu = int(clamp(u * kCellsU, 0.0, kCellsU - 1.0));
  int cv = int(clamp(v * kCellsV, 0.0, kCellsV - 1.0));

  // Never let a star land inside a pixel: sub-pixel points alias into a crawling mess when the
  // camera turns. Clamp the width and let brightness carry the difference instead.
  float sigma = max(baseSigma, pixelAngle * 0.85);
  float energyScale = (baseSigma * baseSigma) / (sigma * sigma);

  float3 sum = 0.0;
  for (int dv = -1; dv <= 1; ++dv) {
    int nv = cv + dv;
    if (nv < 0 || nv >= int(kCellsV)) continue;
    for (int du = -1; du <= 1; ++du) {
      int nu = (cu + du + int(kCellsU)) % int(kCellsU); // longitude wraps

      float4 range = cellTex.Load(int3(nu, nv, 0));
      uint first = uint(range.x);
      uint count = uint(range.y);

      for (uint i = 0; i < count; ++i) {
        uint idx = (first + i) * 2u;
        int3 c0 = int3(int(idx % dataWidth), int(idx / dataWidth), 0);
        uint idx1 = idx + 1u;
        int3 c1 = int3(int(idx1 % dataWidth), int(idx1 / dataWidth), 0);

        float4 dirRad = dataTex.Load(c0);   // xyz = equatorial direction, w = radiance
        float4 colSeed = dataTex.Load(c1);  // rgb = colour, a = twinkle seed

        float cosD = dot(E, dirRad.xyz);
        // Small-angle: the squared angle is ~2(1 - cos). Avoids an acos per star.
        float d2 = max(2.0 * (1.0 - cosD), 0.0);
        float g = exp(-0.5 * d2 / (sigma * sigma));
        if (g < 1e-4) continue;

        sum += colSeed.rgb * dirRad.w * g * energyScale *
               starTwinkle(colSeed.a, time, V.y, twinkle);
      }
    }
  }
  return sum;
}

// ── Night-vision response ────────────────────────────

/// Purkinje shift. Below about 0.01 cd/m2 the cones stop contributing and rods take over: colour
/// vision fades and the peak sensitivity slides toward blue. That is the real reason night reads
/// as blue — moonlight itself is reflected sunlight and very nearly white, so tinting the light
/// source blue is the wrong place to do it. Applying it here, keyed on luminance, means bright
/// things in a night scene keep their colour while the dim surroundings desaturate.
float3 purkinjeShift(float3 color, float strength) {
  if (strength <= 0.0) {
    return color;
  }

  float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
  // Scotopic luminance weights rods differently: they are more sensitive to blue-green, blind to
  // deep red.
  float scotopic = dot(color, float3(0.09, 0.55, 0.36));

  // Full rod vision below ~0.002, full cone vision above ~0.06, mesopic in between.
  float rods = 1.0 - smoothstep(0.002, 0.06, luma);
  rods *= saturate(strength);

  float3 nightColor = scotopic * float3(0.72, 0.92, 1.35);
  return lerp(color, nightColor, rods);
}

#endif // TUCANO_CELESTIAL_HLSL
