# Third-party: Eric Bruneton — Precomputed Atmospheric Scattering

Source: https://github.com/ebruneton/precomputed_atmospheric_scattering  
Docs: https://ebruneton.github.io/precomputed_atmospheric_scattering/  
License: BSD-3-Clause (see upstream LICENSE)

Tucano integrates a DX12/HLSL adaptation:
- CPU bake of transmittance / single-scattering / irradiance LUTs (`BrunetonAtmosphere.cpp`)
- Runtime sampling in `Shaders/BrunetonAtmosphere.hlsl` (sky + aerial perspective)
- Not a line-by-line OpenGL port of `model.cc`; equations and Earth parameters follow Bruneton 2008/2017.
