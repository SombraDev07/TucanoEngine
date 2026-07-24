#include "Terrain/TerrainComponent.h"

#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>

#include <algorithm>

namespace tucano::terrain {

TerrainComponent::TerrainComponent(rhi::Device& device, std::shared_ptr<Heightmap> heightmap,
                                   std::shared_ptr<Material> material)
    : m_heightmap(std::move(heightmap)), m_material(std::move(material)) {
	if (!m_material) {
		m_material = std::make_shared<Material>();
		m_material->name = "Terrain";
		m_material->baseColorFactor = {0.35f, 0.50f, 0.22f, 1.0f};
		m_material->roughnessFactor = 0.85f;
		m_material->metallicFactor = 0.0f;
	}
	generateMesh(device, 256);
}

void TerrainComponent::generateMesh(rhi::Device& device, uint32_t meshResolution) {
	m_meshResolution = meshResolution;
	auto& hm = *m_heightmap;
	float ws = hm.worldSize();

	std::vector<Vertex> verts;
	std::vector<uint32_t> indices;

	verts.reserve(size_t(meshResolution + 1) * size_t(meshResolution + 1));

	for (uint32_t z = 0; z <= meshResolution; ++z) {
		for (uint32_t x = 0; x <= meshResolution; ++x) {
			float fx = float(x) / float(meshResolution);
			float fz = float(z) / float(meshResolution);
			float wx = fx * ws;
			float wz = fz * ws;
			float h = hm.sampleHeight(wx, wz);
			glm::vec3 n = hm.sampleNormal(wx, wz);

			Vertex v{};
			v.position = {wx, h, wz};
			v.normal = n;
			v.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
			v.uv = {wx * 0.03125f, wz * 0.03125f};
			v.color = {1.0f, 1.0f, 1.0f, 1.0f};
			verts.push_back(v);
		}
	}

	for (uint32_t z = 0; z < meshResolution; ++z) {
		for (uint32_t x = 0; x < meshResolution; ++x) {
			uint32_t i0 = z * (meshResolution + 1) + x;
			uint32_t i1 = i0 + 1;
			uint32_t i2 = i0 + (meshResolution + 1);
			uint32_t i3 = i2 + 1;
			indices.push_back(i0);
			indices.push_back(i2);
			indices.push_back(i1);
			indices.push_back(i1);
			indices.push_back(i2);
			indices.push_back(i3);
		}
	}

	SubMesh sub{};
	sub.indexCount = static_cast<uint32_t>(indices.size());
	sub.materialIndex = 0;
	sub.aabbMin = {0.0f, hm.minHeight(), 0.0f};
	sub.aabbMax = {ws, hm.maxHeight(), ws};

	m_mesh = Mesh::create(device, verts, indices, {sub});
}

void TerrainComponent::regenerateMesh(rhi::Device& device) {
	generateMesh(device, m_meshResolution);
}

void TerrainComponent::createPhysicsBody(physics::PhysicsWorld& physWorld) {
	if (!m_physicsBodyId.IsInvalid()) {
		return;
	}

	auto& hm = *m_heightmap;
	uint32_t res = hm.resolution();

	JPH::HeightFieldShapeSettings hfSettings;
	hfSettings.mOffset = JPH::Vec3(0.0f, 0.0f, 0.0f);
	hfSettings.mScale = JPH::Vec3(hm.worldSize() / float(res - 1), 1.0f, hm.worldSize() / float(res - 1));
	hfSettings.mSampleCount = res;

	std::vector<float> heightSamples(res * res);
	for (uint32_t z = 0; z < res; ++z) {
		for (uint32_t x = 0; x < res; ++x) {
			heightSamples[size_t(z) * res + size_t(x)] = hm.sampleHeightNearest(int(x), int(z));
		}
	}
	hfSettings.mHeightSamples = JPH::Array<float>(heightSamples.data(), heightSamples.data() + heightSamples.size());

	JPH::ShapeSettings::ShapeResult result = hfSettings.Create();
	if (!result.IsValid()) {
		return;
	}

	JPH::BodyCreationSettings bodySettings(result.Get(), JPH::RVec3(m_worldPosition.x, m_worldPosition.y, m_worldPosition.z),
	                                       JPH::Quat::sIdentity(), JPH::EMotionType::Static, physics::Layers::STATIC);

	JPH::Body* body = physWorld.bodyInterface().CreateBody(bodySettings);
	if (body) {
		m_physicsBodyId = body->GetID();
	}
}

void TerrainComponent::removePhysicsBody(physics::PhysicsWorld& physWorld) {
	if (!m_physicsBodyId.IsInvalid()) {
		physWorld.removeBody(m_physicsBodyId);
		m_physicsBodyId = JPH::BodyID();
	}
}

RenderObject TerrainComponent::createRenderObject() const {
	RenderObject obj;
	obj.name = "Terrain";
	obj.mesh = m_mesh;
	obj.materials = {m_material};
	obj.transform.translation = m_worldPosition;
	obj.worldMatrix = glm::translate(glm::mat4(1.0f), m_worldPosition);
	return obj;
}

} // namespace tucano::terrain
