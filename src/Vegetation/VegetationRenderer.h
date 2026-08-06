#pragma once

#include "Vegetation/VegetationGPU.h"
#include "Vegetation/VegetationSystem.h"
#include "Vegetation/VegetationMeshes.h"
#include "Vegetation/WindSystem.h"
#include "Vegetation/LODManager.h"
#include "Vegetation/BillboardBaker.h"
#include "Vegetation/VegetationInteraction.h"
#include "Vegetation/SeasonSystem.h"
#include "Vegetation/GrowthSystem.h"
#include "Renderer/Scene.h"
#include "RHI/RHI.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <vector>
#include <cstring>
#include <array>
#include <string>
#include <algorithm>
#include <limits>

#ifndef TUCANO_SHADER_DIR
#define TUCANO_SHADER_DIR "shaders"
#endif

namespace tucano::veg {

/// Drawable LOD bands only (Full / Simplified / Billboard). Culled never allocates a buffer.
static constexpr uint32_t kMaxLODs = 3;
static constexpr uint32_t kMaxGpuTypes = 16;

struct VegTypeBatch {
	std::array<std::shared_ptr<rhi::Buffer>, rhi::kMaxFramesInFlight> instanceRing;
	std::shared_ptr<rhi::Buffer> instanceOut;
	std::array<std::shared_ptr<rhi::Buffer>, kMaxLODs> visibleBuffers;
	std::array<std::shared_ptr<rhi::Buffer>, kMaxLODs> argsBuffers;
	std::array<std::array<std::shared_ptr<rhi::Buffer>, kMaxLODs>, rhi::kMaxFramesInFlight> argsInitRing;
	std::array<std::shared_ptr<rhi::Buffer>, rhi::kMaxFramesInFlight> frameCBRing;
	uint32_t capacity = 0;
	uint32_t instanceCount = 0;
	uint32_t indexCount = 6;
	std::shared_ptr<Mesh> mesh;
	std::shared_ptr<Mesh> meshLOD1;
	std::shared_ptr<Mesh> meshBillboard;
	glm::vec4 baseColor{0.25f, 0.55f, 0.18f, 1.0f};
	float metallic = 0.0f;
	float roughness = 0.92f;
	float alphaCutoff = 0.35f;
	uint32_t albedoTexIndex = 0;
	uint32_t impostorAtlasIndex = 0;
	bool castShadows = true;
	float shadowDistance = 50.0f;
	bool twoSided = true;
};

class VegetationRenderer {
public:
	VegetationRenderer(rhi::Device& device, uint32_t maxInstances = 65536)
	    : m_device(device), m_capacity(maxInstances) {
		m_defaultGrass = makeProceduralGrassMesh(m_device);
		m_defaultGrassLOD1 = makeProceduralGrassLOD1Mesh(m_device);
		m_defaultPlant = makeProceduralPlantMesh(m_device);
		m_billboardQuad = makeBillboardQuadMesh(m_device);

		rhi::TextureDesc hizDummy{};
		hizDummy.width = 1;
		hizDummy.height = 1;
		hizDummy.format = rhi::Format::R32_FLOAT;
		hizDummy.usage = rhi::TextureUsage::ShaderResource;
		hizDummy.debugName = "VegHiZDummy";
		float one = 1.0f;
		m_dummyHiZ = m_device.createTexture(hizDummy, &one, sizeof(float));
	}

	bool createPipeline(std::shared_ptr<rhi::RootSignature> computeRootSig) {
		m_rootSig = computeRootSig;
		rhi::ComputePipelineDesc cdesc{};
		cdesc.rootSignature = m_rootSig;
		const std::string path = std::string(TUCANO_SHADER_DIR) + "/VegetationCull_CSMain.cso";
		cdesc.cs = rhi::ShaderBytecode::loadFromFile(path);
		m_computePSO = m_device.createComputePipeline(cdesc);
		return m_computePSO != nullptr;
	}

	void ensureTypeBatch(uint32_t typeId, uint32_t typeCapacity) {
		if (typeId >= kMaxGpuTypes) return;
		if (m_batches.size() <= typeId) m_batches.resize(typeId + 1);
		auto& b = m_batches[typeId];
		if (b.instanceRing[0] && b.capacity >= typeCapacity) return;

		b.capacity = std::max(typeCapacity, 4096u);

		for (uint32_t f = 0; f < rhi::kMaxFramesInFlight; ++f) {
			rhi::BufferDesc instDesc{};
			instDesc.size = uint64_t(b.capacity) * sizeof(gpu::VegInstance);
			instDesc.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::Upload;
			instDesc.stride = sizeof(gpu::VegInstance);
			instDesc.debugName = "VegIn_T" + std::to_string(typeId) + "_F" + std::to_string(f);
			b.instanceRing[f] = m_device.createBuffer(instDesc, nullptr);

			rhi::BufferDesc frameDesc{};
			frameDesc.size = sizeof(gpu::VegFrameConstants);
			frameDesc.usage = rhi::BufferUsage::Constant | rhi::BufferUsage::Upload;
			frameDesc.debugName = "VegCB_T" + std::to_string(typeId) + "_F" + std::to_string(f);
			b.frameCBRing[f] = m_device.createBuffer(frameDesc, nullptr);

			for (uint32_t lod = 0; lod < kMaxLODs; ++lod) {
				rhi::BufferDesc argsInitDesc{};
				argsInitDesc.size = sizeof(gpu::VegDrawArgs);
				argsInitDesc.usage = rhi::BufferUsage::Upload;
				argsInitDesc.debugName = "VegArgsInit_T" + std::to_string(typeId) + "_L" +
				                         std::to_string(lod) + "_F" + std::to_string(f);
				b.argsInitRing[f][lod] = m_device.createBuffer(argsInitDesc, nullptr);
			}
		}

		rhi::BufferDesc outDesc{};
		outDesc.size = uint64_t(b.capacity) * sizeof(gpu::VegInstance);
		outDesc.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::UnorderedAccess;
		outDesc.stride = sizeof(gpu::VegInstance);
		outDesc.debugName = "VegOut_T" + std::to_string(typeId);
		b.instanceOut = m_device.createBuffer(outDesc, nullptr);

		for (uint32_t lod = 0; lod < kMaxLODs; ++lod) {
			rhi::BufferDesc visDesc{};
			visDesc.size = uint64_t(b.capacity) * sizeof(uint32_t);
			visDesc.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::UnorderedAccess;
			visDesc.stride = sizeof(uint32_t);
			visDesc.debugName = "VegVis_T" + std::to_string(typeId) + "_L" + std::to_string(lod);
			b.visibleBuffers[lod] = m_device.createBuffer(visDesc, nullptr);

			rhi::BufferDesc argsDesc{};
			argsDesc.size = sizeof(gpu::VegDrawArgs);
			argsDesc.usage = rhi::BufferUsage::Structured | rhi::BufferUsage::UnorderedAccess |
			                 rhi::BufferUsage::Indirect;
			argsDesc.stride = sizeof(gpu::VegDrawArgs);
			argsDesc.debugName = "VegArgs_T" + std::to_string(typeId) + "_L" + std::to_string(lod);
			b.argsBuffers[lod] = m_device.createBuffer(argsDesc, nullptr);
		}
	}

	uint32_t frameSlot() const { return m_device.frameIndex() % rhi::kMaxFramesInFlight; }

	void syncMeshesFromSystem(const VegetationSystem& sys) {
		const uint32_t n = std::min(sys.typeCount(), kMaxGpuTypes);
		const uint32_t perType = std::max(4096u, m_capacity / std::max(1u, n));
		for (uint32_t i = 0; i < n; ++i) {
			ensureTypeBatch(i, perType);
			auto& b = m_batches[i];
			auto* t = sys.type(i);
			if (!t) continue;

			if (!t->meshPath.empty() && (!b.mesh || m_loadedPath[i] != t->meshPath)) {
				std::string err;
				auto mesh = loadVegetationMesh(m_device, t->meshPath, &err);
				if (mesh) {
					b.mesh = mesh;
					m_loadedPath[i] = t->meshPath;
				}
			}

			if (!b.mesh) {
				const bool grassLike = t->proceduralKind == 1 ||
				                       (t->proceduralKind == 0 &&
				                        (t->name.find("Grass") != std::string::npos ||
				                         t->name.find("grass") != std::string::npos ||
				                         t->name.find("Leaf") != std::string::npos ||
				                         t->name.find("leaf") != std::string::npos));
				b.mesh = grassLike ? m_defaultGrass : m_defaultPlant;
				b.meshLOD1 = grassLike ? m_defaultGrassLOD1 : m_defaultPlant;
				m_loadedPath[i].clear();
			}
			if (!b.meshLOD1) b.meshLOD1 = b.mesh;
			if (!b.meshBillboard) b.meshBillboard = m_billboardQuad;

			if (b.mesh) {
				b.indexCount = b.mesh->indexCount();
				if (b.indexCount == 0 && !b.mesh->submeshes().empty())
					b.indexCount = b.mesh->submeshes()[0].indexCount;
			}

			LODRange range;
			range.distance0 = t->lodDistance0;
			range.distance1 = t->lodDistance1;
			range.distance2 = t->lodDistance2;
			range.cullDistance = t->cullDistance;
			LODManager::instance().setPerTypeLODRanges(i, range);

			b.castShadows = t->castShadows;
			b.shadowDistance = t->shadowDistance;
			b.twoSided = true;
			b.alphaCutoff = t->meshPath.empty() ? 0.35f : 0.45f;

			// Season tint drives ObjectCB baseColor (GPU param, no per-instance rewrite).
			auto& season = SeasonSystem::instance();
			Season cur = season.currentSeason();
			Season next = Season((int(cur) + 1) % 4);
			glm::vec3 col = season.blendColor(cur, next, season.seasonProgress(), i, false);
			b.baseColor = glm::vec4(col, 1.0f);
			b.alphaCutoff = std::clamp(b.alphaCutoff + season.getLeafDrop(i) * 0.35f, 0.05f, 0.95f);
		}

		ensureImpostorAtlas(n);
	}

	/// (Re)bakes the LOD2 impostor atlas whenever the set of meshes changes — a type gaining its
	/// real mesh after a load has to reach the distant silhouettes too.
	///
	/// The bake is deliberately untinted: InstanceBillboard.hlsl multiplies the atlas sample by
	/// baseColorFactor, so the season tint is applied at draw time for free. Baking it in instead
	/// would make the atlas depend on a value that drifts every frame, and a rebake allocates a
	/// megabytes-large texture — the whole frame would be spent re-baking foliage.
	void ensureImpostorAtlas(uint32_t typeCount) {
		std::vector<ImpostorSource> sources;
		sources.reserve(typeCount);
		for (uint32_t i = 0; i < typeCount && i < m_batches.size(); ++i) {
			sources.push_back(
			    ImpostorSource::fromMesh(m_batches[i].mesh.get(), glm::vec4(1.0f)));
		}
		if (sources.empty()) return;

		// Signature over the geometry alone, so an unchanged frame does no work.
		uint64_t signature = 1469598103934665603ull;
		auto mix = [&signature](uint64_t v) {
			signature ^= v;
			signature *= 1099511628211ull;
		};
		mix(uint64_t(sources.size()));
		for (const auto& s : sources) {
			mix(reinterpret_cast<uint64_t>(s.positions));
		}
		if (m_impostorAtlas && signature == m_impostorSignature) return;

		auto baked = bakeImpostorAtlas(m_device, sources, 8, 1024);
		if (!baked) return; // keep the previous atlas rather than dropping to an untextured LOD2

		m_impostorAtlas = std::move(baked);
		m_impostorSignature = signature;
		m_impostorAtlasTypes = typeCount;
		m_impostorAtlasIndex = m_impostorAtlas->bindlessIndex();
		for (auto& b : m_batches) {
			b.impostorAtlasIndex = m_impostorAtlasIndex;
		}
	}

	bool loadMeshForType(uint32_t typeId, const std::string& path, std::string* error = nullptr) {
		if (typeId >= kMaxGpuTypes) {
			if (error) *error = "typeId out of GPU range";
			return false;
		}
		ensureTypeBatch(typeId, std::max(4096u, m_capacity / 4));
		auto mesh = loadVegetationMesh(m_device, path, error);
		if (!mesh) return false;
		m_batches[typeId].mesh = mesh;
		m_batches[typeId].indexCount = mesh->indexCount();
		if (m_batches[typeId].indexCount == 0 && !mesh->submeshes().empty())
			m_batches[typeId].indexCount = mesh->submeshes()[0].indexCount;
		m_loadedPath[typeId] = path;
		if (auto* t = VegetationSystem::instance().typeMutable(typeId)) t->meshPath = path;
		return true;
	}

	/// Builds the per-type GPU instance arrays and uploads them into this frame's ring slot.
	///
	/// The build is cached. Rebuilding every frame meant walking every cell in radius, allocating
	/// a vector per type and composing three matrices plus four system lookups per plant — with a
	/// scattered field that dominated the whole frame while producing identical data. Wind is
	/// applied in the cull shader, not here, so a static camera over a static field genuinely has
	/// nothing to recompute.
	///
	/// Invalidated by: the system's revision (anything placed, erased or scattered), camera
	/// movement past a threshold (which changes both the visible set and the interaction bend),
	/// and a slow periodic refresh so growth and destruction still land.
	void uploadFromSystem(const VegetationSystem& sys, const glm::vec3& cameraPos, float radius,
	                      float deltaSeconds = 0.0f) {
		syncMeshesFromSystem(sys);
		m_activeTypeCount = std::min(sys.typeCount(), kMaxGpuTypes);

		constexpr float kCameraEpsilon = 0.25f;    // metres
		constexpr float kRefreshInterval = 0.25f;  // seconds between growth/destruction catch-ups

		const bool cameraMoved =
		    glm::distance(cameraPos, m_lastUploadCamera) > kCameraEpsilon;
		const bool revisionChanged = sys.revision() != m_lastUploadRevision;
		const bool radiusChanged = radius != m_lastUploadRadius;

		// The catch-up exists only for growth and destruction, which mutate instance transforms
		// without touching the system's revision. When neither has any registered state — the
		// common case — there is nothing to catch up to and the rebuild is pure waste.
		//
		// It is timed in seconds, not frames: a frame counter fires more often the faster the
		// renderer runs, which is exactly backwards. At 220 FPS a 15-frame period meant fourteen
		// full rebuilds per second, and a rebuild is the single most expensive thing here.
		const bool dynamicsActive = GrowthSystem::instance().instanceCount() > 0 ||
		                            DestructionSystem::instance().hasDestroyed();
		m_secondsSinceRebuild += std::max(deltaSeconds, 0.0f);
		const bool periodic = dynamicsActive && m_secondsSinceRebuild >= kRefreshInterval;

		const bool rebuild =
		    cameraMoved || revisionChanged || radiusChanged || periodic || !m_haveCachedInstances;

		if (!rebuild) {
			// Nothing changed, but the ring rotates: only the slots not yet holding this data
			// need the copy.
			uploadCachedToRing();
			return;
		}

		m_lastUploadCamera = cameraPos;
		m_lastUploadRevision = sys.revision();
		m_lastUploadRadius = radius;
		m_secondsSinceRebuild = 0.0f;
		m_haveCachedInstances = true;
		m_dirtyRingSlots = (1u << rhi::kMaxFramesInFlight) - 1u;
		m_cachedInstances.resize(kMaxGpuTypes);

		for (uint32_t typeId = 0; typeId < m_activeTypeCount; ++typeId) {
			auto& b = m_batches[typeId];
			if (!b.instanceRing[0]) continue;

			auto insts = sys.queryVisibleByType(cameraPos, radius, typeId);
			std::vector<gpu::VegInstance>& gpuInsts = m_cachedInstances[typeId];
			gpuInsts.clear();
			gpuInsts.reserve(insts.size());

			for (auto& i : insts) {
				gpu::VegInstance gi{};
				glm::mat4 T = glm::translate(glm::mat4(1.0f), i.position);
				glm::mat4 R = glm::rotate(glm::mat4(1.0f), i.rotation, glm::vec3(0, 1, 0));
				glm::mat4 S = glm::scale(glm::mat4(1.0f), i.scale);
				gi.worldMatrix = T * R * S;
				gi.center = i.position;
				gi.radius = glm::length(i.scale) * 0.5f;
				gi.typeId = i.typeId;
				if (auto* t = sys.type(i.typeId)) {
					gi.windFlex = t->windFlexibility;
					gi.windHeight = t->windHeight;
				}
				gi.windPhase = i.rotation;

				float bend = VegetationInteraction::instance().computeBendAmount(
				    i.position, gi.windFlex, gi.windHeight);
				if (bend > 0.001f) {
					float angle = VegetationInteraction::instance().computeBendDirection(i.position);
					glm::mat4 bendM = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 1, 0));
					glm::mat4 tiltM = glm::rotate(glm::mat4(1.0f), bend * 0.5f, glm::vec3(1, 0, 0));
					gi.worldMatrix = T * bendM * tiltM * R * S;
				}

				const uint32_t sid = i.instanceId;
				float growthScale = GrowthSystem::instance().getScale(sid);
				if (growthScale != 1.0f) {
					gi.worldMatrix = gi.worldMatrix * glm::scale(glm::mat4(1.0f), glm::vec3(growthScale));
				}
				if (DestructionSystem::instance().isDestroyed(sid)) {
					gi.worldMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
				}

				gpuInsts.push_back(gi);
			}

			b.instanceCount = uint32_t(std::min(gpuInsts.size(), size_t(b.capacity)));
		}

		uploadCachedToRing();
	}

	/// Copies the cached instance arrays into the current frame's upload buffer, but only while
	/// that slot still holds stale data — after kMaxFramesInFlight frames every slot is current
	/// and a still camera costs nothing at all.
	void uploadCachedToRing() {
		const uint32_t slot = frameSlot();
		const uint32_t bit = 1u << slot;
		if ((m_dirtyRingSlots & bit) == 0) return;

		for (uint32_t typeId = 0; typeId < m_activeTypeCount && typeId < m_batches.size(); ++typeId) {
			auto& b = m_batches[typeId];
			if (!b.instanceRing[slot] || b.instanceCount == 0) continue;
			if (typeId >= m_cachedInstances.size()) continue;
			void* mapped = b.instanceRing[slot]->mapped();
			if (!mapped) continue;
			std::memcpy(mapped, m_cachedInstances[typeId].data(),
			            size_t(b.instanceCount) * sizeof(gpu::VegInstance));
		}
		m_dirtyRingSlots &= ~bit;
	}

	void writeFrameConstants(uint32_t typeId, const glm::mat4& viewProj, const glm::vec3& cameraPos,
	                         float maxDist, bool enableHiZ = false, uint32_t screenW = 1,
	                         uint32_t screenH = 1) {
		if (typeId >= m_batches.size()) return;
		auto& wind = WindSystem::instance();
		auto& lodMgr = LODManager::instance();
		const auto& range = lodMgr.rangeForType(typeId);
		gpu::VegFrameConstants fc{};
		fc.windDirection = glm::vec4(wind.params().direction, 0);
		fc.time = wind.time();
		fc.strength = wind.params().strength;
		fc.gustStrength = wind.params().gustStrength;
		fc.turbulence = wind.params().turbulence;
		fc.instanceCount = m_batches[typeId].instanceCount;
		fc.enableHiZ = enableHiZ ? 1u : 0u;
		fc.screenWidth = float(std::max(1u, screenW));
		fc.screenHeight = float(std::max(1u, screenH));

		fc.maxDistance = maxDist;
		fc.nearPlane = 0.1f;
		fc.observer = glm::vec4(cameraPos, 0);
		fc.densityScale = lodMgr.config().globalDensityScale;
		fc.ditherFrame = float(lodMgr.dither().frameCounter);
		fc.enableLODCrossFade = lodMgr.config().enableDitherFade ? 1u : 0u;
		fc.crossFadeWidth = lodMgr.config().crossFadeWidth;
		fc.lodDistances = {range.distance0, range.distance1, range.distance2, range.cullDistance};
		fc.viewProj = viewProj;

		auto row = [](const glm::mat4& m, int i) {
			return glm::vec4(m[0][i], m[1][i], m[2][i], m[3][i]);
		};
		auto normalizePlane = [](const glm::vec4& p) {
			const float len = glm::length(glm::vec3(p));
			return len > 1e-8f ? p / len : p;
		};
		const glm::vec4 r0 = row(viewProj, 0);
		const glm::vec4 r1 = row(viewProj, 1);
		const glm::vec4 r2 = row(viewProj, 2);
		const glm::vec4 r3 = row(viewProj, 3);
		fc.frustumPlanes[0] = normalizePlane(r3 + r0);
		fc.frustumPlanes[1] = normalizePlane(r3 - r0);
		fc.frustumPlanes[2] = normalizePlane(r3 + r1);
		fc.frustumPlanes[3] = normalizePlane(r3 - r1);
		fc.frustumPlanes[4] = normalizePlane(r2);
		fc.frustumPlanes[5] = normalizePlane(r3 - r2);

		std::memcpy(m_batches[typeId].frameCBRing[frameSlot()]->mapped(), &fc, sizeof(fc));
	}

	void resetArgs(uint32_t typeId) {
		if (typeId >= m_batches.size()) return;
		auto& b = m_batches[typeId];
		const uint32_t slot = frameSlot();
		auto indexCountForLod = [&](uint32_t lod) -> uint32_t {
			if (lod == 2 && b.meshBillboard) {
				uint32_t n = b.meshBillboard->indexCount();
				return n ? n : 6;
			}
			if (lod == 1 && b.meshLOD1) {
				uint32_t n = b.meshLOD1->indexCount();
				return n ? n : b.indexCount;
			}
			return b.indexCount > 0 ? b.indexCount : 6;
		};
		for (uint32_t lod = 0; lod < kMaxLODs; ++lod) {
			gpu::VegDrawArgs initArgs{};
			initArgs.indexCountPerInstance = indexCountForLod(lod);
			initArgs.instanceCount = 0;
			std::memcpy(b.argsInitRing[slot][lod]->mapped(), &initArgs, sizeof(initArgs));
		}
	}

	uint32_t activeTypeCount() const { return m_activeTypeCount; }
	uint32_t lodCount() const { return kMaxLODs; }
	uint32_t instanceCount() const {
		uint32_t n = 0;
		for (uint32_t i = 0; i < m_activeTypeCount && i < m_batches.size(); ++i)
			n += m_batches[i].instanceCount;
		return n;
	}
	uint32_t instanceCount(uint32_t typeId) const {
		return typeId < m_batches.size() ? m_batches[typeId].instanceCount : 0;
	}

	VegTypeBatch* batch(uint32_t typeId) {
		return typeId < m_batches.size() ? &m_batches[typeId] : nullptr;
	}
	const VegTypeBatch* batch(uint32_t typeId) const {
		return typeId < m_batches.size() ? &m_batches[typeId] : nullptr;
	}

	std::shared_ptr<rhi::PipelineState> computePSO() const { return m_computePSO; }
	std::shared_ptr<rhi::RootSignature> rootSig() const { return m_rootSig; }

	/// Fill scene.instanceClouds for every type×LOD that has a mesh.
	void submitClouds(std::vector<InstanceCloudRender>& out) const {
		for (uint32_t typeId = 0; typeId < m_activeTypeCount && typeId < m_batches.size(); ++typeId) {
			const auto& b = m_batches[typeId];
			if (!b.mesh || b.instanceCount == 0) continue;
			for (uint32_t lod = 0; lod < kMaxLODs; ++lod) {
				InstanceCloudRender cloud;
				cloud.instanceBuffer = b.instanceOut.get();
				cloud.visibleBuffer = b.visibleBuffers[lod].get();
				cloud.argsBuffer = b.argsBuffers[lod].get();
				if (lod == 2 && b.meshBillboard) {
					cloud.mesh = b.meshBillboard.get();
					cloud.billboard = true;
					cloud.albedoTexIndex = b.impostorAtlasIndex ? b.impostorAtlasIndex : b.albedoTexIndex;
					cloud.billboardViews = 8;
					cloud.billboardGrid = kImpostorGrid;
				} else if (lod == 1 && b.meshLOD1) {
					cloud.mesh = b.meshLOD1.get();
					cloud.albedoTexIndex = b.albedoTexIndex;
				} else {
					cloud.mesh = b.mesh.get();
					cloud.albedoTexIndex = b.albedoTexIndex;
				}
				cloud.baseColor = b.baseColor;
				cloud.metallic = b.metallic;
				cloud.roughness = b.roughness + lod * 0.02f;
				cloud.alphaCutoff = b.alphaCutoff;
				// VegetationType::shadowDistance was editable in the tool but had no effect. A
				// LOD band whose near edge is already past it lies entirely outside shadow
				// range, so drop the whole band — the cull runs per band, not per instance, so
				// this is the level at which the setting can be honoured.
				const auto& lodRange = LODManager::instance().rangeForType(typeId);
				const float bandStart =
				    lod == 0 ? 0.0f : (lod == 1 ? lodRange.distance0 : lodRange.distance1);
				cloud.castShadows = b.castShadows && lod < 2 && bandStart < b.shadowDistance;
				cloud.twoSided = b.twoSided;
				out.push_back(cloud);
			}
		}
	}

	BillboardAtlas& atlas() { return m_atlas; }
	rhi::Texture* dummyHiZ() const { return m_dummyHiZ.get(); }

	std::shared_ptr<rhi::Buffer> instanceInBuffer(uint32_t typeId) const {
		if (typeId >= m_batches.size()) return nullptr;
		return m_batches[typeId].instanceRing[frameSlot()];
	}
	std::shared_ptr<rhi::Buffer> frameCB(uint32_t typeId) const {
		if (typeId >= m_batches.size()) return nullptr;
		return m_batches[typeId].frameCBRing[frameSlot()];
	}
	std::shared_ptr<rhi::Buffer> argsInit(uint32_t typeId, uint32_t lod) const {
		if (typeId >= m_batches.size()) return nullptr;
		return m_batches[typeId].argsInitRing[frameSlot()][std::min(lod, kMaxLODs - 1u)];
	}

	std::shared_ptr<rhi::Buffer> instanceOutBuffer() const {
		return m_batches.empty() ? nullptr : m_batches[0].instanceOut;
	}
	std::shared_ptr<rhi::Buffer> visibleBuffer(uint32_t lod) const {
		if (m_batches.empty()) return nullptr;
		return m_batches[0].visibleBuffers[std::min(lod, kMaxLODs - 1u)];
	}
	std::shared_ptr<rhi::Buffer> argsBuffer(uint32_t lod) const {
		if (m_batches.empty()) return nullptr;
		return m_batches[0].argsBuffers[std::min(lod, kMaxLODs - 1u)];
	}
	std::shared_ptr<rhi::Buffer> windCB() const { return frameCB(0); }
	std::shared_ptr<rhi::Buffer> cullCB() const { return frameCB(0); }

private:
	rhi::Device& m_device;
	uint32_t m_capacity = 65536;
	uint32_t m_activeTypeCount = 0;

	std::vector<VegTypeBatch> m_batches;
	std::array<std::string, kMaxGpuTypes> m_loadedPath{};
	std::shared_ptr<rhi::RootSignature> m_rootSig;
	std::shared_ptr<rhi::PipelineState> m_computePSO;
	std::shared_ptr<Mesh> m_defaultGrass;
	std::shared_ptr<Mesh> m_defaultGrassLOD1;
	std::shared_ptr<Mesh> m_defaultPlant;
	std::shared_ptr<Mesh> m_billboardQuad;
	std::shared_ptr<rhi::Texture> m_dummyHiZ;
	std::shared_ptr<rhi::Texture> m_impostorAtlas;
	uint32_t m_impostorAtlasTypes = 0;
	uint32_t m_impostorAtlasIndex = 0;
	uint64_t m_impostorSignature = 0;

	// Instance-build cache — see uploadFromSystem.
	std::vector<std::vector<gpu::VegInstance>> m_cachedInstances;
	glm::vec3 m_lastUploadCamera{std::numeric_limits<float>::max()};
	uint64_t m_lastUploadRevision = 0;
	float m_lastUploadRadius = -1.0f;
	float m_secondsSinceRebuild = 0.0f;
	uint32_t m_dirtyRingSlots = 0; // bit per ring slot still holding stale instance data
	bool m_haveCachedInstances = false;
	BillboardAtlas m_atlas;
};

} // namespace tucano::veg
