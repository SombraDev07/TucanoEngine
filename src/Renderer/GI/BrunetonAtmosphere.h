#pragma once

#include "RHI/RHI.h"

#include <memory>

namespace tucano {

// Eric Bruneton — Precomputed Atmospheric Scattering (EGSR 2008 / 2017 reimplementation).
// https://github.com/ebruneton/precomputed_atmospheric_scattering
// BSD-3-Clause. CPU bake of transmittance / scattering / irradiance LUTs for DX12.

class BrunetonAtmosphere {
public:
  void init(rhi::Device& device);

  bool ready() const { return m_ready; }

  rhi::Texture* transmittance() const { return m_transmittance.get(); }
  rhi::Texture* scattering() const { return m_scattering.get(); } // 2D atlas: depth packed in Y
  rhi::Texture* irradiance() const { return m_irradiance.get(); }

  // Planet radii in km (length unit = 1 km).
  float bottomRadiusKm() const { return m_bottomRadius; }
  float topRadiusKm() const { return m_topRadius; }
  float sunAngularRadius() const { return m_sunAngularRadius; }
  float mieG() const { return m_mieG; }
  float exposure() const { return m_exposure; }

  // Atlas layout for scattering sample
  static constexpr int kTransW = 256;
  static constexpr int kTransH = 64;
  static constexpr int kScatNu = 4;
  static constexpr int kScatMuS = 16;
  static constexpr int kScatMu = 64;
  static constexpr int kScatR = 16;
  static constexpr int kScatW = kScatNu * kScatMuS; // 64
  static constexpr int kScatH = kScatMu;            // 64 per layer
  static constexpr int kIrrW = 64;
  static constexpr int kIrrH = 16;

private:
  void bakeCpu(rhi::Device& device);

  std::shared_ptr<rhi::Texture> m_transmittance;
  std::shared_ptr<rhi::Texture> m_scattering;
  std::shared_ptr<rhi::Texture> m_irradiance;
  float m_bottomRadius = 6360.0f;
  float m_topRadius = 6420.0f;
  float m_sunAngularRadius = 0.00935f / 2.0f;
  float m_mieG = 0.8f;
  float m_exposure = 14.0f;
  bool m_ready = false;
};

} // namespace tucano
