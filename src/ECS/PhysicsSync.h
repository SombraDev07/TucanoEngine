#pragma once

#include "ECS/World.h"
#include "Physics/PhysicsWorld.h"

namespace tucano::ecs {

void syncPhysicsToTransforms(physics::PhysicsWorld& phys, World& world);
void syncTransformsToScene(World& world, Scene& scene);

} // namespace tucano::ecs
