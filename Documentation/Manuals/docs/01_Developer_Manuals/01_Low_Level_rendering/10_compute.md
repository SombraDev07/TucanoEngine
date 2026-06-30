---
title: Compute
---

Compute GPU programs are not meant to be used for drawing/rendering, but instead for arbitrary computations. In order to execute a compute program you must bind a **GpuComputePipelineState** to a **GpuCommandBuffer**. After it is bound you must call @b3d::render::GpuCommandBuffer::DispatchCompute().

Compute GPU programs are executed in one or multiple thread groups. Each thread group has one or multiple threads, as defined in the GPU program code itself. Thread groups and threads can be organized in one, two or three dimensions, depending on what is most relevant to the data being processed. **GpuCommandBuffer::DispatchCompute()** expects the number of thread-groups to launch as parameters.

Compute work is frequently run off the render thread — image processing during asset import is a typical example — so the example below drives it from a custom worker [GPU work context](../Low_Level_rendering/gpuWorkContext), which owns and waits for its own GPU work. The parameter set is allocated from that context's pool. On the render thread you would instead use the renderer's context, retrieved through @b3d::render::Renderer::GetGpuContext.

~~~~~~~~~~~~~{.cpp}
GpuDevice& gpuDevice = GetApplication().GetPrimaryGpuDevice();

// A worker context for this thread's compute work
TShared<GpuWorkContext> gpuContext = GpuWorkContext::Create(gpuDevice);

// Create a compute pipeline state with your compute program
GpuComputePipelineStateCreateInformation pipelineCreateInformation;
pipelineCreateInformation.Program = myComputeProgram;
TShared<GpuComputePipelineState> computePipelineState = gpuDevice.CreateGpuComputePipelineState(pipelineCreateInformation);

// Allocate a GPU parameter set for binding resources (set 0) from the context's pool
TShared<GpuParameterSet> parameterSet = gpuContext->GetParameterSetPool().Create(computePipelineState->GetParameterLayout()->GetSet(0), 0);

// Set parameters (buffers, textures, etc.)
parameterSet->SetStorageBuffer("inputBuffer", myInputBuffer);
parameterSet->SetStorageBuffer("outputBuffer", myOutputBuffer);

// Record the dispatch into a compute command buffer
GpuCommandBufferPoolCreateInformation poolCreateInformation =
	GpuCommandBufferPoolCreateInformation::CreateForThisThread(GQT_COMPUTE);
TShared<GpuCommandBufferPool> commandBufferPool = gpuDevice.CreateGpuCommandBufferPool(poolCreateInformation);

TShared<GpuCommandBuffer> commandBuffer = commandBufferPool->Create(GpuCommandBufferCreateInformation::Create("Compute"));

// Bind the pipeline state and parameters
commandBuffer->SetGpuComputePipelineState(computePipelineState);
commandBuffer->SetGpuParameterSet(parameterSet);

// Execute a GPU program with 32x32 thread-groups
commandBuffer->DispatchCompute(32, 32, 1);
commandBuffer->End();
~~~~~~~~~~~~~

# Submitting compute work
Like all GPU commands, a compute dispatch only executes once its command buffer is submitted through a context. Submit it with @b3d::GpuWorkContext::SubmitCommandBuffer:

~~~~~~~~~~~~~{.cpp}
gpuContext->SubmitCommandBuffer(commandBuffer);

// Block until the GPU finishes, so the results can be read back
gpuContext->WaitAndReclaim();

B3D_LOG(Info, LogRenderBackend, "Compute dispatch completed with 32x32 thread groups");
~~~~~~~~~~~~~

The explicit @b3d::GpuWorkContext::WaitAndReclaim is only needed when you must read the results back before the context is destroyed — a worker context's destructor performs the same wait when it goes out of scope.
