# Sponza Gold Standard — Tucano Engine

**Milestone:** Phase 2 Sponza AAA gate  
**Date:** 2026-07-21  
**GPU:** AMD Radeon RX 6700  

## How to run

```powershell
.\scripts\dev_env.ps1
cmake --preset=windows-release
cmake --build --preset=windows-release

# Copy assets next to the exe (or run from repo with relative path)
copy Assets\Sponza\* build\windows-release\Samples\SponzaViewer\Assets\Sponza\
.\build\windows-release\Samples\SponzaViewer\SponzaViewer.exe --scene Assets/Sponza/Sponza.gltf
```

Controls: RMB+WASD fly, Shift sprint, 1 shadows, 2 IBL, 3 bloom, 4 AO, F12 screenshot.

## Scene setup

- Asset: Khronos glTF-Sample-Models Sponza (`Assets/Sponza/`)
- 1 directional sun + 4 interior point lights
- Deferred PBR G-Buffer → lighting → CSM → IBL → AO → bloom → ACES tonemap

## Official capture

`captures/g7_sponza.png` — atrium with banners, sun shaft on floor, PBR materials, shadows.
