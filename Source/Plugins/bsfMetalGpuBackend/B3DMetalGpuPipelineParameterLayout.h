//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "B3DMetalPrerequisites.h"
#include "GpuBackend/B3DGpuPipelineParameterLayout.h"

namespace b3d
{
	namespace render
	{
		class MetalGpuDevice;

		/** @addtogroup MetalGpuBackend
		 *  @{
		 */

		/**
		 * Describes a single binding within a Metal argument buffer in C++-visible form. Used by
		 * @c MetalGpuParameters to look up which argument-buffer slot, resource usage, and stage-mask
		 * should be applied when making resources resident on a command encoder.
		 */
		struct MetalArgumentBufferBinding
		{
			/** Engine slot index; what BSL/B3D callers pass to @c SetUniformBuffer / @c SetSampledTexture etc. */
			u32 Slot = 0;
			/**
			 * Argument-buffer index. Metal argument buffers use a single flat namespace per set, shared
			 * across buffers, textures, and samplers. This is the index programmed into the
			 * @c MTLArgumentEncoder descriptor and used for @c setBuffer/setTexture/setSamplerState
			 * @c atIndex: — it must match the SPIRV-Cross @c msl_buffer / @c msl_texture / @c msl_sampler
			 * value we feed to the shader compiler for the same @c (set, slot) pair.
			 */
			u32 ArgIndex = 0;
			/** Engine parameter type: distinguishes uniform/storage buffer vs texture vs sampler. */
			GpuParameterType Type = GpuParameterType::Unknown;
			/** Metal object type (GPOT_*) for the binding; drives read/write usage flags. */
			GpuParameterObjectType ObjectType = GPOT_UNKNOWN;
			/** Array length of this slot; 1 for scalar bindings. */
			u32 ArraySize = 1;
			/** Mask of @c GpuProgramStageBit values indicating which shader stages reference this binding. */
			u32 StageMask = 0;
		};

		/**
		 * Holds meta-data about a single GPU parameter set. Stores the @c MTLArgumentDescriptor list
		 * describing the set's argument buffer so individual @c MetalGpuParameters instances can each
		 * mint their own @c MTLArgumentEncoder.
		 *
		 * The base class normalizes parameter-description uniforms into @c mUniformsPerType; we walk that
		 * array, build an @c MTLArgumentDescriptor per entry (one descriptor per engine slot), then use a
		 * transient encoder at construction time to read back the argument-buffer size and alignment that
		 * every parameter set sharing this layout will need.
		 *
		 * Encoders are @b not shared between parameter sets: @c MTLArgumentEncoder holds per-instance
		 * state (the currently-bound argument buffer + offset), so concurrent writes from different
		 * fibers/threads to parameter sets that share a layout would corrupt bindings. Each
		 * @c MetalGpuParameters calls @c NewArgumentEncoder() at init time for a private encoder.
		 *
		 * Metal argument encoders are stage-agnostic — the same encoded buffer may be referenced by the
		 * vertex, fragment, and compute stages. The per-binding @c StageMask is consulted separately when
		 * calling @c useResource:usage:stages: so the driver can make each resource resident only on the
		 * stages that actually read it.
		 */
		class MetalGpuPipelineParameterSetLayout : public GpuPipelineParameterSetLayout
		{
		public:
			MetalGpuPipelineParameterSetLayout(MetalGpuDevice& gpuDevice, const GpuProgramParameterDescription& parameterDescription);
			~MetalGpuPipelineParameterSetLayout() override;

			/** Total size (in bytes) of the argument buffer for this set, taken from the encoder's encodedLength. */
			u64 GetArgumentBufferSize() const { return mArgumentBufferSize; }

			/** Required alignment (in bytes) for the argument buffer's base offset. */
			u32 GetArgumentBufferAlignment() const { return mArgumentBufferAlignment; }

			/** Returns the full list of bindings this set exposes, in the order they were registered. */
			const TArray<MetalArgumentBufferBinding>& GetBindings() const { return mBindings; }

			/**
			 * B2: bindings pre-grouped by @c (MTLResourceUsage, MTLRenderStages) for the render path
			 * and by @c MTLResourceUsage alone for the compute path.
			 *
			 * One bucket collects every binding that shares usage + stage mask so
			 * @c useResources:count:usage:stages: (render) or @c useResources:count:usage: (compute)
			 * can mark the whole group resident in a single Metal call at draw/dispatch time, instead
			 * of the N @c useResource: calls the phase-1 path paid. @c Usage is precomputed from the
			 * binding's storage-writability; @c RenderStages is zero on compute buckets and unused.
			 *
			 * Cardinality in practice is small (@<= 4 render buckets, @<= 2 compute buckets) because
			 * the axes are tiny: usage is Read or ReadWrite, and shader stages are Vertex, Fragment,
			 * or Vertex|Fragment. Samplers are filtered out at bucket-build time.
			 */
			struct ArgumentBindingBucket
			{
#ifdef __OBJC__
				MTLResourceUsage Usage = MTLResourceUsageRead;
				MTLRenderStages RenderStages = (MTLRenderStages)0;
#else
				// Raw integer storage when the header is included from plain C++ TUs that cannot see
				// the Metal types. The @c .mm translation unit casts these to their Metal types.
				u64 Usage = 0;
				u64 RenderStages = 0;
#endif
				/** Indices into @c mBindings for every binding in this bucket. Samplers are pre-filtered out. */
				TArray<u16> BindingIndices;
			};

			/** Render-path buckets — see @c ArgumentBindingBucket. */
			const TArray<ArgumentBindingBucket>& GetRenderBuckets() const { return mRenderBuckets; }

			/** Compute-path buckets — see @c ArgumentBindingBucket. */
			const TArray<ArgumentBindingBucket>& GetComputeBuckets() const { return mComputeBuckets; }

			/**
			 * Returns the union of @c GpuProgramStageBit values referencing any binding in this set.
			 *
			 * Precomputed at construction so @c MetalGpuCommandBuffer does not need to re-walk every
			 * binding on each @c SetGpuParameterSet to decide which shader stages should receive the
			 * argument buffer. Consumed by @c AttachArgumentBufferToRenderEncoder /
			 * @c AttachArgumentBufferToComputeEncoder at bind time.
			 */
			u32 GetCombinedStageMask() const { return mCombinedStageMask; }

			/**
			 * Resolves an engine @c (type, slot) pair to its argument-buffer index within this set. Returns
			 * @c ~0u if no binding of that type exists at @p slot. Metal argument buffers use a single flat
			 * index space per set — this function is the authoritative mapping that both @c SetX calls and
			 * the SPIRV-Cross-emitted MSL agree on.
			 */
			u32 GetArgumentBufferIndex(GpuParameterType type, u32 slot) const;

#ifdef __OBJC__
			/**
			 * Mints a fresh @c MTLArgumentEncoder from this layout's descriptor list. Each
			 * @c MetalGpuParameters instance calls this once at @c Initialize() so writes to per-set
			 * argument buffers don't share a single encoder's current-buffer state across threads. May
			 * return @c nil when the set contains no bindings.
			 */
			id<MTLArgumentEncoder> NewArgumentEncoder() const;
#endif

		private:
			struct Impl;

			MetalGpuDevice& mGpuDevice;
			TUnique<Impl> mImpl;
			TArray<MetalArgumentBufferBinding> mBindings;
			u64 mArgumentBufferSize = 0;
			u32 mArgumentBufferAlignment = 16;

			// Union of stage masks across every binding in mBindings. Computed once after mBindings is
			// finalized so command-buffer bind paths can read the stage subset the set touches without
			// re-walking the bindings. See GetCombinedStageMask.
			u32 mCombinedStageMask = 0;

			// B2: precomputed bucket lists. Finalized in the ctor once @c mBindings is complete so
			// the command-buffer bind path reads a ready-made grouping at draw / dispatch time.
			TArray<ArgumentBindingBucket> mRenderBuckets;
			TArray<ArgumentBindingBucket> mComputeBuckets;
		};

		/** Holds meta-data about a set of GPU parameters used by a single pipeline state. */
		class MetalGpuPipelineParameterLayout : public GpuPipelineParameterLayout
		{
		public:
			MetalGpuPipelineParameterLayout(MetalGpuDevice& gpuDevice, const GpuPipelineParameterLayoutCreateInformation& createInformation);
			~MetalGpuPipelineParameterLayout() = default;
		};

		/** @} */
	} // namespace render
} // namespace b3d
