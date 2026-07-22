# IBL / HDRI

Drop a lat-long `.hdr` here as `default.hdr`.

Runtime cooks irradiance + GGX prefiltered mips via `createIBLFromHDRIFile`.
If missing, the engine falls back to procedural sky IBL.
