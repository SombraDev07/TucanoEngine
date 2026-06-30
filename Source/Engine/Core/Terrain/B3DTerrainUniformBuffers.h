//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DPrerequisites.h"
#include "Renderer/B3DRendererMaterial.h"
#include "Renderer/B3DGpuUniformBuffer.h"
#include "GpuBackend/B3DGpuParameter.h"
#include "GpuBackend/B3DGpuBuffer.h"
#include "Math/B3DVector2.h"
#include "Math/B3DVector3.h"
#include "Math/B3DVector4.h"
#include "Math/B3DMatrix4.h"

namespace b3d
{
	namespace render
	{
		/**
		 * Uniform buffer for per-frame terrain constants.
		 * Shared between the heightmap VS and the virtual-texture PS.
		 */
		B3D_UNIFORM_BUFFER_BEGIN(TerrainFrameParamsDef)
			B3D_UNIFORM_BUFFER_MEMBER(Vector4, gHeightmapScaleOfs)   // (.xy=1/worldSize, .zw=worldOffset)
			B3D_UNIFORM_BUFFER_MEMBER(Vector4, gHeightScaleOffset)    // (.x=heightScale, .y=heightMin, .zw=unused)
			B3D_UNIFORM_BUFFER_MEMBER(Vector4, gMorphParams)          // (.x=morphStart, .y=morphEnd, .z=1/morphRange, .w=unused)
			B3D_UNIFORM_BUFFER_MEMBER(Vector4, gVTexParams)           // (.x=tilesPerMip, .y=tileTexelSize, .z=atlasInvSize, .w=mipCount)
			B3D_UNIFORM_BUFFER_MEMBER(Vector3, gCameraPos)
		B3D_UNIFORM_BUFFER_END

		extern TerrainFrameParamsDef gTerrainFrameParams;

		/**
		 * Uniform buffer for per-patch constants.
		 * Uploaded once per draw call (or via push constants on Vulkan).
		 */
		B3D_UNIFORM_BUFFER_BEGIN(TerrainPatchParamsDef)
			B3D_UNIFORM_BUFFER_MEMBER(Vector4, gPatchOriginSize)  // (.x=originX, .y=originZ, .z=patchSize, .w=lodLevel)
		B3D_UNIFORM_BUFFER_END

		extern TerrainPatchParamsDef gTerrainPatchParams;

		/**
		 * Uniform buffer for per-frame lighting constants used by the terrain pixel shader.
		 */
		B3D_UNIFORM_BUFFER_BEGIN(TerrainLightParamsDef)
			B3D_UNIFORM_BUFFER_MEMBER(Matrix4, gWorldViewProj)
			B3D_UNIFORM_BUFFER_MEMBER(Vector4, gSunDirection)    // world-space, pointing toward light
			B3D_UNIFORM_BUFFER_MEMBER(Vector4, gSunColor)
			B3D_UNIFORM_BUFFER_MEMBER(Vector4, gAmbientColor)
		B3D_UNIFORM_BUFFER_END

		extern TerrainLightParamsDef gTerrainLightParams;
	} // namespace render
} // namespace b3d
