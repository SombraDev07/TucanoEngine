#include "Vegetation/VegetationDispatch.h"
#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/DX12CommandList.h"
#include "RHI/DX12/DX12Resource.h"
#include "RHI/RHI.h"

namespace tucano::veg {

void VegDispatch::recordDispatch(rhi::Device& device, rhi::CommandList& cmd,
                                 VegetationRenderer& veg, const glm::mat4& viewProj,
                                 const glm::vec3& cameraPos, float maxDist, rhi::Texture* hiZ,
                                 uint32_t screenW, uint32_t screenH) {
	if (veg.instanceCount() == 0 || !veg.computePSO() || !veg.rootSig()) return;

	auto& dev = static_cast<rhi::DX12Device&>(device);
	auto asDxBuf = [](rhi::Buffer& b) -> rhi::DX12Buffer& {
		return static_cast<rhi::DX12Buffer&>(b);
	};
	auto asDxTex = [](rhi::Texture& t) -> rhi::DX12Texture& {
		return static_cast<rhi::DX12Texture&>(t);
	};

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

		D3D12_CPU_DESCRIPTOR_HANDLE srvs[2]{};
		srvs[0] = asDxBuf(*inBuf).srvCpu;
		if (hizTex) {
			srvs[1] = asDxTex(*hizTex).srvCpu;
			cmd.setComputeRootSrvTable(2, dev.writeSrvTable(srvs, 2));
		} else {
			cmd.setComputeRootSrvTable(2, dev.writeSrvTable(srvs, 1));
		}

		D3D12_CPU_DESCRIPTOR_HANDLE uavs[8]{};
		uavs[0] = asDxBuf(*batch->instanceOut).uavCpu;
		for (uint32_t lod = 0; lod < veg.lodCount(); ++lod) {
			uavs[1 + lod] = asDxBuf(*batch->visibleBuffers[lod]).uavCpu;
			uavs[4 + lod] = asDxBuf(*batch->argsBuffers[lod]).uavCpu;
		}
		cmd.setComputeRootUavTable(3, dev.writeUavTable(uavs, 1 + veg.lodCount() * 2));

		uint32_t groups = (batch->instanceCount + 63) / 64;
		cmd.dispatch(groups, 1, 1);
	}
}

} // namespace tucano::veg
