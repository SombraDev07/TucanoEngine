#pragma once

#include "ECS/World.h"
#include "Physics/PhysicsWorld.h"
#include "Renderer/Scene.h"

namespace tucano::ecs {

void syncPhysicsToTransforms(physics::PhysicsWorld& phys, World& world);
// alpha in [0,1] interpolates between the previous and current fixed-step physics states.
void syncTransformsToScene(World& world, tucano::Scene& scene, float alpha = 1.0f);

} // namespace tucano::ecs
