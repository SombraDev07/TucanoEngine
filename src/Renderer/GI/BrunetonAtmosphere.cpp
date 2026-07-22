#include "Renderer/GI/BrunetonAtmosphere.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace tucano {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Earth parameters (Bruneton demo defaults), length unit = 1 km.
struct AtmParams {
  float bottomRadius = 6360.f;
  float topRadius = 6420.f;
  glm::vec3 solarIrradiance{1.474f, 1.8504f, 1.91198f}; // relative RGB (demo-ish)
  glm::vec3 rayleighScattering{5.802e-3f, 13.558e-3f, 33.1e-3f}; // 1/km
  float mieScattering = 3.996e-3f;
  float mieExtinction = 4.440e-3f;
  float mieG = 0.8f;
  glm::vec3 absorptionExtinction{0.650e-3f, 1.881e-3f, 0.085e-3f}; // ozone-ish RGB
  glm::vec3 groundAlbedo{0.1f, 0.1f, 0.1f};
};

float clampCosine(float mu) { return std::clamp(mu, -1.f, 1.f); }
float clampDistance(float d) { return std::max(d, 0.f); }
float clampRadius(float r, float bottom, float top) { return std::clamp(r, bottom, top); }

float safeSqrt(float a) { return std::sqrt(std::max(a, 0.f)); }

float distanceToTopAtmosphereBoundary(float r, float mu, float top) {
  const float discriminant = r * r * (mu * mu - 1.f) + top * top;
  return clampDistance(-r * mu + safeSqrt(discriminant));
}

float distanceToBottomAtmosphereBoundary(float r, float mu, float bottom) {
  const float discriminant = r * r * (mu * mu - 1.f) + bottom * bottom;
  return clampDistance(-r * mu - safeSqrt(discriminant));
}

bool rayIntersectsGround(float r, float mu, float bottom) {
  return mu < 0.f && r * r * (mu * mu - 1.f) + bottom * bottom >= 0.f;
}

float rayleighDensity(float h) { return std::exp(-h / 8.f); }     // scale height 8 km
float mieDensity(float h) { return std::exp(-h / 1.2f); }        // 1.2 km
float ozoneDensity(float h) {
  // Bruneton-style tent around ~25 km
  return std::max(0.f, 1.f - std::abs(h - 25.f) / 15.f);
}

glm::vec3 computeOpticalLengthToTop(float r, float mu, const AtmParams& p, int samples = 40) {
  const float dx = distanceToTopAtmosphereBoundary(r, mu, p.topRadius) / float(samples);
  glm::vec3 od(0);
  for (int i = 0; i <= samples; ++i) {
    const float d_i = float(i) * dx;
    const float r_i = safeSqrt(d_i * d_i + 2.f * r * mu * d_i + r * r);
    const float h = std::max(0.f, r_i - p.bottomRadius);
    const float w = (i == 0 || i == samples) ? 0.5f : 1.f;
    od += w * (p.rayleighScattering * rayleighDensity(h) +
               glm::vec3(p.mieExtinction) * mieDensity(h) +
               p.absorptionExtinction * ozoneDensity(h));
  }
  return od * dx;
}

glm::vec3 computeTransmittanceToTop(float r, float mu, const AtmParams& p) {
  return glm::exp(-computeOpticalLengthToTop(r, mu, p));
}

// Texture unit conversions (Bruneton mapping, Earth-tuned).
float getRFromUnit(float u, float bottom, float top) {
  return std::sqrt(u * u * (top * top - bottom * bottom) + bottom * bottom);
}
float getMuFromUnit(float u, float r, float bottom, float top) {
  const float H = safeSqrt(top * top - bottom * bottom);
  const float rho = safeSqrt(r * r - bottom * bottom);
  float d = 0.f;
  if (u < 0.5f) {
    d = u * 2.f * (H - rho);
    return (d == 0.f) ? -1.f : ((H * H - rho * rho - d * d) / (2.f * r * d));
  }
  d = (u * 2.f - 1.f) * (H + rho) + rho;
  return (H * H - rho * rho - d * d) / (2.f * r * std::max(d, 1e-6f));
}

void getRMuFromTransmittanceUv(float u, float v, float bottom, float top, float& r, float& mu) {
  // Inverse of GetTransmittanceTextureUvFromRMu (Bruneton)
  const float x_mu = u;
  const float x_r = v;
  const float H = safeSqrt(top * top - bottom * bottom);
  const float rho = H * x_r;
  r = safeSqrt(rho * rho + bottom * bottom);
  const float d_min = top - r;
  const float d_max = rho + H;
  const float d = d_min + x_mu * (d_max - d_min);
  mu = (d == 0.f) ? 1.f : (H * H - rho * rho - d * d) / (2.f * r * d);
  mu = clampCosine(mu);
}

glm::vec2 transmittanceUvFromRMu(float r, float mu, float bottom, float top) {
  const float H = safeSqrt(top * top - bottom * bottom);
  const float rho = safeSqrt(std::max(r * r - bottom * bottom, 0.f));
  const float d = distanceToTopAtmosphereBoundary(r, mu, top);
  const float d_min = top - r;
  const float d_max = rho + H;
  const float x_mu = (d - d_min) / std::max(d_max - d_min, 1e-6f);
  const float x_r = rho / H;
  return {x_mu, x_r};
}

glm::vec3 sampleTransmittanceTable(const std::vector<glm::vec4>& table, float r, float mu, float bottom,
                                   float top) {
  const glm::vec2 uv = transmittanceUvFromRMu(r, mu, bottom, top);
  const float x = std::clamp(uv.x, 0.f, 1.f) * float(BrunetonAtmosphere::kTransW - 1);
  const float y = std::clamp(uv.y, 0.f, 1.f) * float(BrunetonAtmosphere::kTransH - 1);
  const int x0 = int(x);
  const int y0 = int(y);
  const int x1 = std::min(x0 + 1, BrunetonAtmosphere::kTransW - 1);
  const int y1 = std::min(y0 + 1, BrunetonAtmosphere::kTransH - 1);
  const float fx = x - float(x0);
  const float fy = y - float(y0);
  auto at = [&](int ix, int iy) {
    return glm::vec3(table[size_t(iy * BrunetonAtmosphere::kTransW + ix)]);
  };
  const glm::vec3 a = glm::mix(at(x0, y0), at(x1, y0), fx);
  const glm::vec3 b = glm::mix(at(x0, y1), at(x1, y1), fx);
  return glm::mix(a, b, fy);
}

float rayleighPhase(float nu) {
  constexpr float k = 3.f / (16.f * kPi);
  return k * (1.f + nu * nu);
}

float miePhase(float g, float nu) {
  const float g2 = g * g;
  const float k = 3.f / (8.f * kPi) * (1.f - g2) / (2.f + g2);
  return k * (1.f + nu * nu) / std::pow(1.f + g2 - 2.f * g * nu, 1.5f);
}

// Single-scatter integral along view ray (Bruneton ComputeSingleScattering).
void computeSingleScattering(float r, float mu, float mu_s, float nu, const AtmParams& p,
                             const std::vector<glm::vec4>& transmittance, glm::vec3& rayleigh,
                             glm::vec3& mie) {
  rayleigh = glm::vec3(0);
  mie = glm::vec3(0);
  const bool ground = rayIntersectsGround(r, mu, p.bottomRadius);
  const float tMax = ground ? distanceToBottomAtmosphereBoundary(r, mu, p.bottomRadius)
                            : distanceToTopAtmosphereBoundary(r, mu, p.topRadius);
  constexpr int samples = 16;
  const float dx = tMax / float(samples);
  for (int i = 0; i <= samples; ++i) {
    const float d_i = float(i) * dx;
    const float r_i = clampRadius(safeSqrt(d_i * d_i + 2.f * r * mu * d_i + r * r), p.bottomRadius, p.topRadius);
    const float mu_s_i = clampCosine((r * mu_s + d_i * nu) / r_i);
    const float h = std::max(0.f, r_i - p.bottomRadius);

    glm::vec3 t_cam = sampleTransmittanceTable(transmittance, r, mu, p.bottomRadius, p.topRadius);
    // Approximate segment transmittance via table ratio
    const glm::vec3 t_sample =
        sampleTransmittanceTable(transmittance, r_i, mu_s_i, p.bottomRadius, p.topRadius);
    // Transmittance camera → sample: T(r,mu)/T(r_i, mu_i) with mu_i = (r*mu+d)/r_i
    const float mu_i = clampCosine((r * mu + d_i) / r_i);
    const glm::vec3 t_to_sample =
        sampleTransmittanceTable(transmittance, r, mu, p.bottomRadius, p.topRadius) /
        glm::max(sampleTransmittanceTable(transmittance, r_i, mu_i, p.bottomRadius, p.topRadius),
                 glm::vec3(1e-6f));
    const glm::vec3 t_sun = t_sample;
    const float w = (i == 0 || i == samples) ? 0.5f : 1.f;

    if (!rayIntersectsGround(r_i, mu_s_i, p.bottomRadius)) {
      rayleigh += w * t_to_sample * t_sun * rayleighDensity(h);
      mie += w * t_to_sample * t_sun * mieDensity(h);
    }
  }
  rayleigh *= p.rayleighScattering * dx;
  mie *= p.mieScattering * dx;
}

} // namespace

void BrunetonAtmosphere::bakeCpu(rhi::Device& device) {
  AtmParams p;
  p.bottomRadius = m_bottomRadius;
  p.topRadius = m_topRadius;
  p.mieG = m_mieG;

  std::cout << "[Bruneton] Baking transmittance " << kTransW << "x" << kTransH << "...\n";
  std::vector<glm::vec4> transmittance(size_t(kTransW * kTransH));
  for (int j = 0; j < kTransH; ++j) {
    for (int i = 0; i < kTransW; ++i) {
      const float u = (float(i) + 0.5f) / float(kTransW);
      const float v = (float(j) + 0.5f) / float(kTransH);
      float r, mu;
      getRMuFromTransmittanceUv(u, v, p.bottomRadius, p.topRadius, r, mu);
      const glm::vec3 T = computeTransmittanceToTop(r, mu, p);
      transmittance[size_t(j * kTransW + i)] = glm::vec4(T, 1.f);
    }
  }

  std::cout << "[Bruneton] Baking single scattering atlas " << kScatW << "x" << (kScatH * kScatR) << "...\n";
  std::vector<glm::vec4> scattering(size_t(kScatW * kScatH * kScatR), glm::vec4(0));
  // Mapping mirrors Bruneton GetRMuMuSNuFromScatteringTextureUvwz (simplified Earth).
  for (int ir = 0; ir < kScatR; ++ir) {
    const float z = (float(ir) + 0.5f) / float(kScatR);
    const float H = safeSqrt(p.topRadius * p.topRadius - p.bottomRadius * p.bottomRadius);
    const float rho = H * z;
    const float r = safeSqrt(rho * rho + p.bottomRadius * p.bottomRadius);
    for (int imu = 0; imu < kScatMu; ++imu) {
      const float y = (float(imu) + 0.5f) / float(kScatMu);
      // Split mu ground / sky like Bruneton
      float mu;
      if (y < 0.5f) {
        const float d = 1.f - y * 2.f;
        const float d2 = d * d;
        const float disc = rho * rho + d2 * (p.bottomRadius * p.bottomRadius - r * r);
        mu = disc > 0.f ? clampCosine((-rho - safeSqrt(disc)) / (d * r)) : -1.f;
      } else {
        const float d = (y - 0.5f) * 2.f;
        mu = clampCosine((H * H - rho * rho - d * d * (H + rho) * (H + rho)) /
                         (2.f * r * std::max(d * (H + rho), 1e-4f)));
      }
      for (int is = 0; is < kScatMuS; ++is) {
        for (int inu = 0; inu < kScatNu; ++inu) {
          const float x_mu_s = (float(is) + 0.5f) / float(kScatMuS);
          const float x_nu = (float(inu) + 0.5f) / float(kScatNu);
          // mu_s mapping (Bruneton)
          const float d_min = p.topRadius - r;
          const float d_max = rho + H;
          float A = (d_max - d_min) * float(kScatMuS - 1) / float(kScatMuS);
          float a = 1.f - std::exp(-(p.bottomRadius / H)); // approx
          float D = (A - a * d_min) / (1.f - a);
          float d = D * std::exp(a * std::log(1.f - x_mu_s * (1.f - a))) + (d_min - D);
          float mu_s = (d == 0.f) ? 1.f : clampCosine((H * H - rho * rho - d * d) / (2.f * r * d));
          // nu from [-1,1]
          float nu = clampCosine(-1.f + x_nu * 2.f);
          // Clamp nu domain for (mu, mu_s)
          const float xi = mu * mu_s;
          nu = clampCosine(std::clamp(nu, xi - safeSqrt((1 - mu * mu) * (1 - mu_s * mu_s)),
                                      xi + safeSqrt((1 - mu * mu) * (1 - mu_s * mu_s))));

          glm::vec3 rayleigh, mie;
          computeSingleScattering(r, mu, mu_s, nu, p, transmittance, rayleigh, mie);
          // Pack: RGB = Rayleigh (+ tiny multi stub), A = Mie.r (phase applied at runtime)
          const glm::vec3 multi = rayleigh * 0.15f; // cheap multi-scatter energy (not full Bruneton orders)
          const int ix = inu * kScatMuS + is;
          const int iy = ir * kScatH + imu;
          scattering[size_t(iy * kScatW + ix)] = glm::vec4(rayleigh + multi, mie.r);
        }
      }
    }
    if ((ir % 4) == 0) {
      std::cout << "[Bruneton] scattering layer " << ir << "/" << kScatR << "\n";
    }
  }

  std::cout << "[Bruneton] Baking irradiance " << kIrrW << "x" << kIrrH << "...\n";
  std::vector<glm::vec4> irradiance(size_t(kIrrW * kIrrH), glm::vec4(0));
  for (int j = 0; j < kIrrH; ++j) {
    for (int i = 0; i < kIrrW; ++i) {
      const float u = (float(i) + 0.5f) / float(kIrrW);
      const float v = (float(j) + 0.5f) / float(kIrrH);
      const float H = safeSqrt(p.topRadius * p.topRadius - p.bottomRadius * p.bottomRadius);
      const float rho = H * v;
      const float r = safeSqrt(rho * rho + p.bottomRadius * p.bottomRadius);
      const float mu_s = clampCosine(2.f * u - 1.f);
      // Integrate hemisphere approx (coarse)
      glm::vec3 E(0);
      constexpr int N = 8;
      for (int iy = 0; iy < N; ++iy) {
        for (int ix = 0; ix < N; ++ix) {
          const float phi = (float(ix) + 0.5f) / float(N) * 2.f * kPi;
          const float cosTheta = (float(iy) + 0.5f) / float(N);
          const float sinTheta = safeSqrt(1.f - cosTheta * cosTheta);
          const float mu = cosTheta;
          const float nu = mu * mu_s + sinTheta * safeSqrt(std::max(0.f, 1.f - mu_s * mu_s)) * std::cos(phi);
          glm::vec3 rayleigh, mie;
          computeSingleScattering(r, mu, mu_s, nu, p, transmittance, rayleigh, mie);
          E += (rayleigh * rayleighPhase(nu) + mie * miePhase(p.mieG, nu)) * cosTheta;
        }
      }
      E *= (2.f * kPi) / float(N * N);
      irradiance[size_t(j * kIrrW + i)] = glm::vec4(E, 1.f);
    }
  }

  auto uploadRGBA32 = [&](uint32_t w, uint32_t h, const std::vector<glm::vec4>& src, const char* name) {
    rhi::TextureDesc d{};
    d.width = w;
    d.height = h;
    d.format = rhi::Format::R32G32B32A32_FLOAT;
    d.usage = rhi::TextureUsage::ShaderResource;
    d.debugName = name;
    return device.createTexture(d, src.data(), w * 4 * sizeof(float));
  };

  m_transmittance = uploadRGBA32(kTransW, kTransH, transmittance, "BrunetonTransmittance");
  m_scattering = uploadRGBA32(kScatW, uint32_t(kScatH * kScatR), scattering, "BrunetonScatteringAtlas");
  m_irradiance = uploadRGBA32(kIrrW, kIrrH, irradiance, "BrunetonIrradiance");
  std::cout << "[Bruneton] LUTs ready (Earth, single-scatter + multi stub)\n";
}

void BrunetonAtmosphere::init(rhi::Device& device) {
  if (m_ready) {
    return;
  }
  bakeCpu(device);
  m_ready = true;
}

} // namespace tucano
