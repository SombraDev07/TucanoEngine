#pragma once

#include "Terrain/Heightmap.h"
#include "Renderer/Mesh.h"
#include "Renderer/Scene.h"
#include "Renderer/Material.h"
#include "Physics/PhysicsWorld.h"

#include <glm/glm.hpp>

#include <memory>

namespace tucano::terrain {

class TerrainComponent {
public:
	TerrainComponent(rhi::Device& device, std::shared_ptr<Heightmap> heightmap,
	                 std::shared_ptr<Material> material = nullptr);

	void generateMesh(rhi::Device& device, uint32_t meshResolution);
	void regenerateMesh(rhi::Device& device);

	void createPhysicsBody(physics::PhysicsWorld& physWorld);
	void removePhysicsBody(physics::PhysicsWorld& physWorld);

	RenderObject createRenderObject() const;

	std::shared_ptr<Heightmap> heightmap() const { return m_heightmap; }
	std::shared_ptr<Mesh> mesh() const { return m_mesh; }
	JPH::BodyID physicsBodyId() const { return m_physicsBodyId; }

	glm::vec3 worldPosition() const { return m_worldPosition; }
	void setWorldPosition(const glm::vec3& pos) { m_worldPosition = pos; }

private:
	std::shared_ptr<Heightmap> m_heightmap;
	std::shared_ptr<Mesh> m_mesh;
	std::shared_ptr<Material> m_material;
	glm::vec3 m_worldPosition{0.0f};
	uint32_t m_meshResolution = 0;
	JPH::BodyID m_physicsBodyId;
};

} // namespace tucano::terrain
