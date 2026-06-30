//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DPrerequisites.h"
#include "GpuBackend/B3DGpuBuffer.h"
#include "Image/B3DTexture.h"

namespace b3d
{
	class HeightmapData;
	namespace render { class GpuCommandBuffer; }

	/**
	 * GPU-accelerated terrain erosion system.
	 *
	 * Implements two erosion algorithms on the GPU via compute shaders:
	 *
	 * 1. **Hydraulic erosion** (TerrainHydraulicErosion.bsl):
	 *    Simulates water flow over the terrain. Each pass:
	 *    - Adds rainfall to a water layer.
	 *    - Distributes water to neighbouring cells (pipe model).
	 *    - Erodes material where water flows fast (erosion = velocity × K_c).
	 *    - Deposits eroded material where velocity drops.
	 *    - Evaporates excess water.
	 *    After N iterations, the heightmap gains realistic river valleys and fan deposits.
	 *
	 * 2. **Thermal erosion** (inline in hydraulic CS):
	 *    Relaxes terrain to eliminate slopes exceeding the talus angle.
	 *    Each pass moves material from steep cells to adjacent shallower ones.
	 *    Produces natural scree/talus formations.
	 *
	 * The input heightmap is stored in a RW Texture2D (R16F) so the GPU
	 * can both read and write it in-place.
	 *
	 * Outputs:
	 *   - Modified heightmap (R16F) — ready for upload to the CPU HeightmapData.
	 *   - Flow map (RG16F) — can be used for artistic effects (wet streaks, etc.)
	 *
	 * Reference technique inspired by:
	 *   Mei & Decaudin (2007) "Fast Hydraulic Erosion Simulation and Visualization on GPU"
	 *   Dagor Engine proc terrain research notes (no equivalent code — original).
	 */
	class B3D_EXPORT TerrainErosion
	{
	public:
		struct Settings
		{
			// ---- Hydraulic ----
			float mRainfallAmount    = 0.012f;  // water added per iteration
			float mEvaporationRate   = 0.015f;  // water removed per iteration
			float mErosionCapacity   = 5.0f;    // K_c: how much sediment water carries
			float mErosionDeposit    = 0.3f;    // K_d: deposition speed
			float mErosionRate       = 0.3f;    // K_s: erosion speed
			float mMinWaterToErode   = 0.001f;  // water threshold below which no erosion
			uint32_t mHydraulicIterations = 50;

			// ---- Thermal ----
			float mTalusAngle      = 0.6f;     // max slope (radians) before material slides
			float mThermalRate     = 0.5f;     // fraction of excess to move per iteration
			uint32_t mThermalIterations = 20;

			// ---- Resolution ----
			uint32_t mResolution = 1024; // must match HeightmapData dimensions
		};

		TerrainErosion() = default;
		~TerrainErosion() { Close(); }

		TerrainErosion(const TerrainErosion&) = delete;
		TerrainErosion& operator=(const TerrainErosion&) = delete;

		/**
		 * Initialises the erosion system and allocates GPU buffers.
		 * Uploads the input heightmap to GPU.
		 */
		bool Init(const Settings& settings, const HeightmapData& heightmap);

		/** Releases all GPU resources. */
		void Close();

		/**
		 * Runs one full erosion pass (all iterations for both algorithms).
		 * Must be called on the render thread.
		 *
		 * @param commandBuffer Compute command buffer to record into.
		 */
		void Run(render::GpuCommandBuffer& commandBuffer);

		/**
		 * Downloads the eroded heightmap from GPU and writes it to @p outHeightmap.
		 * Call after Run() and GPU completion to get the CPU result.
		 */
		bool DownloadResult(HeightmapData& outHeightmap);

		/** Returns the GPU-side heightmap texture (R16F, UAV-enabled). */
		HTexture GetHeightmapGpuTexture() const { return mHeightmapTex; }

		/** Returns the GPU-side water flow map (RG16F). */
		HTexture GetFlowMapTexture() const { return mFlowMapTex; }

		bool IsInitialised() const { return mIsInitialised; }

	private:
		void RunHydraulicPass(render::GpuCommandBuffer& commandBuffer, uint32_t iteration);
		void RunThermalPass(render::GpuCommandBuffer& commandBuffer, uint32_t iteration);

		Settings               mSettings;

		// GPU textures (all UAV-enabled)
		HTexture               mHeightmapTex;    // R16F — current heightmap
		HTexture               mWaterTex;        // R16F — water depth
		HTexture               mSedimentTex;     // R16F — suspended sediment
		HTexture               mFlowMapTex;      // RG16F — water velocity (Vx, Vz)
		HTexture               mTempTex;         // R16F — scratch buffer

		// CPU readback
		TShared<GpuBuffer>     mReadbackBuffer;
		float                  mHeightMin   = 0.0f;
		float                  mHeightScale = 1.0f;
		uint32_t               mResolution  = 0;

		bool                   mIsInitialised = false;
	};
}
