// Gate for the backend-agnostic RHI abstraction (MASTER_ROADMAP Track A0).
//
// The claim A0 makes is "nothing in the tucano::rhi interface leaks a DX12 concept, so a second
// backend can be added as a peer". Compiling NullDevice.cpp proves the *header* is clean. This gate
// proves the *contract* is usable: it drives a Device that owns no GPU through the same calls the
// renderer makes — create resources, record a frame, upload, present — and checks the state the
// interface promises comes back.
//
// Runs headless on any machine (no adapter, no swapchain, no window), which is also why the Null
// backend exists: CI and asset tooling can hold a Device without a GPU.
//
//   TucanoRHITest
//
// Exit code is the failure count, so CI can gate on it.

#include "RHI/Null/NullDevice.h"
#include "RHI/RHI.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace tucano;

namespace {

int g_failures = 0;

void check(bool cond, const std::string& what) {
	std::cout << (cond ? "  [ok]   " : "  [FAIL] ") << what << "\n";
	if (!cond) ++g_failures;
}

} // namespace

int main() {
	std::cout << "TucanoRHITest — Null backend / RHI abstraction gate\n\n";

	std::cout << "Device\n";
	auto device = rhi::createNullDevice();
	check(device != nullptr, "createNullDevice() returns a Device with no adapter present");
	if (!device) return 1;
	check(device->nativeDevice() == nullptr, "nativeDevice() is null (no backend handle to leak)");
	check(!device->isDeviceLost(), "device reports healthy");

	std::cout << "\nBuffers\n";
	{
		const uint32_t seed[4] = {1u, 2u, 3u, 4u};
		rhi::BufferDesc desc;
		desc.size = sizeof(seed);
		desc.usage = rhi::BufferUsage::Structured;
		desc.stride = sizeof(uint32_t);
		desc.debugName = "GateBuffer";

		auto buffer = device->createBuffer(desc, seed);
		check(buffer != nullptr, "createBuffer() with initial data");
		check(buffer->size() == desc.size, "size() round-trips the desc");

		// The mapped pointer is the contract renderer code relies on for per-frame ring writes,
		// so the Null backend backs it with real memory rather than returning null.
		auto* mapped = static_cast<const uint32_t*>(buffer->mapped());
		check(mapped != nullptr, "mapped() gives a writable pointer for CPU-visible buffers");
		check(mapped && std::memcmp(mapped, seed, sizeof(seed)) == 0, "initial data landed in the buffer");

		const uint32_t patch[2] = {42u, 43u};
		device->uploadBuffer(*buffer, patch, sizeof(patch), sizeof(uint32_t));
		check(mapped && mapped[0] == 1u && mapped[1] == 42u && mapped[2] == 43u && mapped[3] == 4u,
		      "uploadBuffer() honours the byte offset and leaves neighbours intact");

		// Out-of-range uploads must be rejected, not silently scribble past the allocation.
		const uint32_t overflow[4] = {9u, 9u, 9u, 9u};
		device->uploadBuffer(*buffer, overflow, sizeof(overflow), desc.size - sizeof(uint32_t));
		check(mapped && mapped[0] == 1u && mapped[3] == 4u, "out-of-range uploadBuffer() is a no-op");

		check(buffer->srvIndex() == 0xFFFFFFFFu && buffer->uavIndex() == 0xFFFFFFFFu,
		      "bindless indices report 'unbound' rather than a bogus slot");
	}

	std::cout << "\nTextures / samplers\n";
	{
		rhi::TextureDesc desc;
		desc.width = 256;
		desc.height = 128;
		desc.format = rhi::Format::R16G16B16A16_FLOAT;
		desc.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
		desc.debugName = "GateTexture";

		auto texture = device->createTexture(desc);
		check(texture != nullptr, "createTexture()");
		check(texture && texture->width() == 256 && texture->height() == 128, "extent round-trips the desc");
		check(texture && texture->format() == rhi::Format::R16G16B16A16_FLOAT, "format round-trips the desc");
		check(device->createSampler({}) != nullptr, "createSampler()");
	}

	std::cout << "\nPipelines\n";
	{
		auto rootSig = device->createRootSignature();
		auto computeSig = device->createComputeRootSignature();
		check(rootSig != nullptr && computeSig != nullptr, "root signatures (graphics + compute)");

		// Empty bytecode: the point is that pipeline creation is expressible without naming DXIL.
		rhi::GraphicsPipelineDesc gfx;
		gfx.rootSignature = rootSig;
		check(device->createGraphicsPipeline(gfx) != nullptr, "createGraphicsPipeline()");

		rhi::ComputePipelineDesc cs;
		cs.rootSignature = computeSig;
		check(device->createComputePipeline(cs) != nullptr, "createComputePipeline()");
	}

	std::cout << "\nSwapChain + frame loop\n";
	{
		auto swapChain = device->createSwapChain(nullptr, 1280, 720, false);
		check(swapChain != nullptr, "createSwapChain() without an HWND");
		if (swapChain) {
			check(swapChain->width() == 1280 && swapChain->height() == 720, "swapchain extent");
			swapChain->resize(1920, 1080);
			check(swapChain->width() == 1920 && swapChain->height() == 1080, "resize() rebuilds the back buffer");
			check(swapChain->backBuffer().width() == 1920, "backBuffer() follows the resize");

			// Drive several frames so frameIndex() is shown to cycle over the in-flight window —
			// the invariant every per-frame ring allocator in the renderer is built on.
			std::vector<uint32_t> indices;
			const uint32_t kFrames = rhi::kMaxFramesInFlight * 2;
			for (uint32_t i = 0; i < kFrames; ++i) {
				check(swapChain->tryAcquireFrame(), "tryAcquireFrame() frame " + std::to_string(i));
				auto* cmd = device->beginFrame();
				if (!cmd) {
					check(false, "beginFrame() returned a command list");
					break;
				}
				indices.push_back(device->frameIndex());

				rhi::Viewport vp;
				vp.width = 1920.0f;
				vp.height = 1080.0f;
				cmd->setViewport(vp);
				cmd->setDescriptorHeap();
				const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
				cmd->clearRenderTarget(swapChain->backBuffer(), clear);
				cmd->transition(swapChain->backBuffer(), rhi::ResourceState::RenderTarget);
				cmd->draw(3, 1);

				device->endFrame(*swapChain);
				swapChain->present();
			}

			check(device->frameFenceValue() == kFrames, "frameFenceValue() advances once per endFrame()");
			bool wrapped = indices.size() == kFrames && indices.front() == indices[rhi::kMaxFramesInFlight];
			check(wrapped, "frameIndex() cycles over kMaxFramesInFlight");
		}
		device->waitIdle();
	}

	std::cout << "\n" << (g_failures == 0 ? "PASS" : "FAIL") << " — " << g_failures << " failure(s)\n";
	return g_failures;
}
