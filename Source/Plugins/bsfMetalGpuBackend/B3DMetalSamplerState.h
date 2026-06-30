//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DMetalPrerequisites.h"
#include "GpuBackend/B3DSamplerState.h"

namespace b3d
{
	namespace render
	{
		class MetalGpuDevice;

		/** @addtogroup MetalGpuBackend
		 *  @{
		 */

		/**	Metal implementation of a sampler state. Owns a @c MTLSamplerState created from the engine descriptor. */
		class MetalSamplerState : public SamplerState
		{
		public:
			MetalSamplerState(MetalGpuDevice& gpuDevice, const SamplerStateCreateInformation& createInformation);
			~MetalSamplerState();

			void Initialize() override;

#ifdef __OBJC__
			/** Returns the underlying MTLSamplerState. May be nil before Initialize() has been called. */
			id<MTLSamplerState> GetMetalSampler() const;
#endif

		private:
			struct Impl;

			MetalGpuDevice& mGpuDevice;
			TUnique<Impl> mImpl;
		};

		/** @} */
	} // namespace render
} // namespace b3d
