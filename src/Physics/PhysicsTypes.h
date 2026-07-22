#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

namespace tucano::physics {

enum class MotionType : uint8_t {
  Static    = 0,
  Dynamic   = 1,
  Kinematic = 2,
};

namespace Layers {
  static constexpr JPH::ObjectLayer STATIC     = 0;
  static constexpr JPH::ObjectLayer DYNAMIC    = 1;
  static constexpr JPH::ObjectLayer CHARACTER  = 2;
  static constexpr JPH::ObjectLayer NUM_LAYERS = 3;
}

namespace BroadPhaseLayers {
  static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
  static constexpr JPH::BroadPhaseLayer MOVING(1);
  static constexpr JPH::uint            NUM_LAYERS(2);
}

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
  BPLayerInterfaceImpl() {
    mObjectToBroadPhase[Layers::STATIC]    = BroadPhaseLayers::NON_MOVING;
    mObjectToBroadPhase[Layers::DYNAMIC]   = BroadPhaseLayers::MOVING;
    mObjectToBroadPhase[Layers::CHARACTER] = BroadPhaseLayers::MOVING;
  }
  JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
    return mObjectToBroadPhase[inLayer];
  }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
    return inLayer == BroadPhaseLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
  }
#endif
private:
  JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
    (void)a; (void)b;
    return true;
  }
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  bool ShouldCollide(JPH::ObjectLayer a, JPH::BroadPhaseLayer b) const override {
    switch (a) {
      case Layers::STATIC:    return b == BroadPhaseLayers::MOVING;
      case Layers::DYNAMIC:
      case Layers::CHARACTER: return true;
      default:                return false;
    }
  }
};

} // namespace tucano::physics
