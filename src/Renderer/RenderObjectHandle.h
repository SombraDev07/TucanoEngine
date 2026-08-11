#pragma once

#include <cstdint>

// A reference to a `RenderObject` that survives other objects being removed (C-09).
//
// Its own header, like `LightType.h`, so the ECS can name one without including the whole renderer:
// `RenderObjectComponent` holds a handle, and `Scene.h` drags in Mesh, Material and the RHI.
//
// Packed index | generation, exactly like `ecs::Entity`, so the vocabulary is the one the codebase
// already reads: 20 bits of index (1M live objects) and 12 of generation. The generation wraps at
// 4096 reuses of the *same* slot, which is the bound the ECS already accepts — a handle held across
// that many recycles would validate against the wrong object.
//
// **Why this exists.** `RenderObjectComponent` used to hold a raw index into `Scene::objects`, and
// that vector was compacted in four places (streaming cell unload, terrain tile release, the
// Outliner's delete, and a scene reload). After any of them every index above the removed one named
// a different object — and the syncs only `continue` on an out-of-range index, so an entity would
// quietly start driving somebody else's geometry instead of failing.

namespace tucano {

using RenderObjectHandle = uint32_t;

// Generations start at 1 (see `Scene::ObjectSlot`), so a zero-initialised handle is invalid too —
// which matters because the ECS zeroes the memory of a component created with `createWith`.
inline constexpr RenderObjectHandle kInvalidRenderObject = 0xFFFFFFFFu;
inline constexpr uint32_t kRenderObjectIndexBits = 20;
inline constexpr uint32_t kRenderObjectIndexMask = (1u << kRenderObjectIndexBits) - 1u;

inline uint32_t renderObjectIndex(RenderObjectHandle h) { return h & kRenderObjectIndexMask; }
inline uint32_t renderObjectGeneration(RenderObjectHandle h) { return h >> kRenderObjectIndexBits; }
inline RenderObjectHandle makeRenderObjectHandle(uint32_t index, uint32_t generation) {
  return (index & kRenderObjectIndexMask) | (generation << kRenderObjectIndexBits);
}

} // namespace tucano
