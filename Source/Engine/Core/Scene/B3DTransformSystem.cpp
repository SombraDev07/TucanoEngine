//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "Scene/B3DTransformSystem.h"
#include "Scene/B3DSceneObjectFragments.h"

namespace b3d::ecs
{
	void TransformSystem::Update(Registry& registry)
	{
		PropagateDirtyFlags(registry);
		ComputeWorldTransforms(registry);
	}

	void TransformSystem::PropagateDirtyFlags(Registry& registry)
	{
		auto* dirtyStorage = registry.TryGetStorage<TransformDirty>();
		if(dirtyStorage == nullptr || dirtyStorage->Size() == 0)
			return;

		// Walk the dirty storage by packed index. AddTag appends newly tagged children
		// to the end of the packed array, so advancing the index naturally performs a
		// BFS without any temporary allocation.
		for(u64 entityIndex = 0; entityIndex < dirtyStorage->Size(); entityIndex++)
		{
			Entity current = (*dirtyStorage)[entityIndex];

			if(!registry.HasAllOf<Children>(current))
				continue;

			const Children& children = registry.GetComponents<Children>(current);
			for(Entity child : children.Entities)
				registry.AddTag<TransformDirty>(child);
		}
	}

	void TransformSystem::ComputeWorldTransforms(Registry& registry)
	{
		auto* dirtyStorage = registry.TryGetStorage<TransformDirty>();
		if(dirtyStorage == nullptr || dirtyStorage->Size() == 0)
			return;

		// Sort dirty entities by hierarchy depth ascending (parents first).
		// Sorts the packed entity array in-place — O(D log D) where D = dirty count.
		dirtyStorage->Sort([&registry](Entity lhs, Entity rhs)
		{
			return registry.GetComponents<HierarchyDepth>(lhs).Depth < registry.GetComponents<HierarchyDepth>(rhs).Depth;
		});

		// Iterate in TransformDirty storage order so we follow the depth sort above.
		auto view = registry.CreateView<TransformDirty, LocalTransform, WorldTransform, Parent>();
		view.SetLeadingType<TransformDirty>();

		// NOTE: This loop could be faster by reducing the loop dependencies, which currently block out of order execution.
		// We should split the loop into higher level loop per depth layer. This way each "depth layer loop" can operate on
		// its entities completely independently. One easy improvement here would be to do a sort on the transforms based
		// on the hierarchy depth, this way entities on the same layer remain close in cache.
		for(auto entity : view)
		{
			const LocalTransform& localTransform = view.Get<LocalTransform>(entity);
			WorldTransform& worldTransform = view.Get<WorldTransform>(entity);
			const Parent& parent = view.Get<Parent>(entity);

			if(parent.Entity == kNullEntity)
			{
				// Root entity — world transform equals local transform
				worldTransform = WorldTransform(localTransform);
			}
			else
			{
				// Compose with parent's already-computed world transform
				const WorldTransform& parentWorldTransform = registry.GetComponents<WorldTransform>(parent.Entity);
				worldTransform = WorldTransform(localTransform);
				worldTransform.MakeWorld(parentWorldTransform);
			}
		}

		registry.ClearStorage<TransformDirty>();
	}
} // namespace b3d::ecs
