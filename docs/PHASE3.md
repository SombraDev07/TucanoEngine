# Phase 3 — RenderGraph + GI + Meshlets + VisBuffer

## Delivered

| Item | Status |
|------|--------|
| 3.1 RenderGraph | Passes, import/transient, lifetime aliasing analysis, graphviz export (`captures/render_graph.dot`) |
| 3.2 GI | SSGI + DDGI atlas (raster/screen update) + tiers Off/Low/Med/High |
| 3.3 SSR | Hi-Z-ish screen march + IBL fallback; DXR stub (logged unavailable) |
| 3.4 Meshlets | meshoptimizer clusters + CPU frustum cull + packed IB draws |
| 3.5 VisBuffer | R32_UINT ID pass + resolve overwrite toggle |
| 3.6 Integration | Wired in `Renderer::render`, SponzaViewer hotkeys |

## Hotkeys (SponzaViewer)

- `1` shadows · `2` IBL · `3` bloom · `4` AO
- `5` cycle GI tier · `6` VisBuffer · `7` meshlets · `8` SSR
- `F12` screenshot

## Notes

- RX 6700: no HW RT — reflections are SSR + IBL only.
- Mesh shaders not used; meshlets draw via classic IA + culled index ranges.
- Async compute: `asyncHint` recorded on SSGI pass; execution remains on the graphics queue.
