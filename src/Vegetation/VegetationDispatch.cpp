#include "Vegetation/VegetationDispatch.h"
#include "RHI/RHI.h"

namespace tucano::veg {

void VegDispatch::recordDispatch(rhi::Device& device, rhi::CommandList& cmd,
                                 VegetationRenderer& veg, const glm::mat4& viewProj,
                                 const glm::vec3& cameraPos, float maxDist, rhi::Texture* hiZ,
                                 uint32_t screenW, uint32_t screenH) {
	if (veg.instanceCount() == 0 || !veg.computePSO() || !veg.rootSig()) return;

	cmd.setRootSignature(*veg.rootSig());
	cmd.setDescriptorHeap();
	cmd.setPipeline(*veg.computePSO());

	rhi::Texture* hizTex = hiZ ? hiZ : veg.dummyHiZ();
	if (hizTex) {
		cmd.transition(*hizTex, rhi::ResourceState::ShaderResource);
	}

	for (uint32_t typeId = 0; typeId < veg.activeTypeCount(); ++typeId) {
		auto* batch = veg.batch(typeId);
		if (!batch || batch->instanceCount == 0 || !batch->instanceOut) continue;

		auto inBuf = veg.instanceInBuffer(typeId);
		auto cb = veg.frameCB(typeId);
		if (!inBuf || !cb) continue;

		veg.writeFrameConstants(typeId, viewProj, cameraPos, maxDist, hiZ != nullptr, screenW,
		                        screenH);
		veg.resetArgs(typeId);

		for (uint32_t lod = 0; lod < veg.lodCount(); ++lod) {
			auto init = veg.argsInit(typeId, lod);
			cmd.copyBuffer(*batch->argsBuffers[lod], 0, *init, 0, sizeof(gpu::VegDrawArgs));
		}

		cmd.transition(*batch->instanceOut, rhi::ResourceState::UnorderedAccess);
		for (uint32_t lod = 0; lod < veg.lodCount(); ++lod) {
			cmd.transition(*batch->visibleBuffers[lod], rhi::ResourceState::UnorderedAccess);
			cmd.transition(*batch->argsBuffers[lod], rhi::ResourceState::UnorderedAccess);
		}

		cmd.setComputeRootCBV(1, *cb);

		// t0 = instance input (structured buffer), t1 = Hi-Z (texture): one table, two kinds.
		rhi::ResourceView srvs[] = {rhi::ResourceView::srv(*inBuf),
		                            hizTex ? rhi::ResourceView::srv(*hizTex) : rhi::ResourceView{}};
		cmd.setComputeRootSrvTable(2, device.writeResourceTable({srvs, hizTex ? 2u : 1u}));

		rhi::Buffer* uavs[8]{};
		uavs[0] = batch->instanceOut.get();
		for (uint32_t lod = 0; lod < veg.lodCount(); ++lod) {
			uavs[1 + lod] = batch->visibleBuffers[lod].get();
			uavs[4 + lod] = batch->argsBuffers[lod].get();
		}
		cmd.setComputeRootUavTable(3, device.writeBufferUavTable({uavs, 1 + veg.lodCount() * 2}));

		uint32_t groups = (batch->instanceCount + 63) / 64;
		cmd.dispatch(groups, 1, 1);
	}
}

} // namespace tucano::veg
