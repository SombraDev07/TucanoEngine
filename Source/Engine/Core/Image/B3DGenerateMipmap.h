//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DPrerequisites.h"
#include "Image/B3DPixelData.h"
#include "Threading/B3DAsyncOp.h"

namespace b3d
{
	struct MipMapGenOptions;

	/**
	 * GPU-accelerated mip-map chain generation. Produces the full mip chain for a 2D surface using a compute shader that
	 * downsamples each level from the previous one.
	 */
	class B3D_EXPORT GpuGenerateMipmap
	{
	public:
		/**
		 * Generates the full mip chain for @p source (including mip 0) on the GPU. The returned operation completes with the
		 * chain - mip 0 first, each level in @p source's pixel format - once the GPU has finished and every level has been
		 * read back to CPU memory. On failure (no active GPU device or the shader is unavailable) it completes with an empty
		 * vector.
		 *
		 * Do not block on the returned operation from the render thread, since that thread owns (and drives) the callback.
		 */
		static TAsyncOp<Vector<TShared<PixelData>>> Generate(const TShared<PixelData>& source, const MipMapGenOptions& options);
	};
} // namespace b3d
