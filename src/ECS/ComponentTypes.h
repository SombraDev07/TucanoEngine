#pragma once

#include "Core/Json.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace tucano::ecs {

// Entity: uint32 compacto — index (20 bits) | generation (12 bits).
using Entity = uint32_t;
inline constexpr Entity kInvalidEntity = 0xFFFFFFFFu;
inline constexpr uint32_t kEntityIndexBits = 20;
inline constexpr uint32_t kEntityIndexMask = (1u << kEntityIndexBits) - 1u;
inline uint32_t entityIndex(Entity e) { return e & kEntityIndexMask; }
inline uint32_t entityGeneration(Entity e) { return e >> kEntityIndexBits; }
inline Entity makeEntity(uint32_t index, uint32_t gen) {
  return (index & kEntityIndexMask) | (gen << kEntityIndexBits);
}

using JsonApplyFn = void (*)(void* dst, const core::JsonValue& props);

struct ComponentDesc {
  std::string name;
  uint32_t size = 0;
  uint32_t align = 8;
  JsonApplyFn applyJson = nullptr; // optional: fill component from template JSON
};

// Global registry: name → index estável durante a execução.
class ComponentRegistry {
public:
  static ComponentRegistry& instance();

  uint32_t registerComponent(const char* name, uint32_t size, uint32_t align,
                             JsonApplyFn applyJson = nullptr);
  uint32_t find(std::string_view name) const; // kInvalidEntity se não existe
  const ComponentDesc& desc(uint32_t id) const { return m_types[id]; }
  uint32_t count() const { return uint32_t(m_types.size()); }

private:
  std::vector<ComponentDesc> m_types;
  std::unordered_map<std::string, uint32_t> m_byName;
};

// Bloom filter de 64 bits: 2 bits por componente (pré-filtro O(1) por archetype).
inline uint64_t componentBloomBits(uint32_t id) {
  const uint32_t h = id * 0x9E3779B1u;
  return (1ull << (h & 63u)) | (1ull << ((h >> 8) & 63u));
}

template <typename T>
struct ComponentTag {
  static inline uint32_t id = kInvalidEntity;
};

template <typename T>
uint32_t registerComponent(const char* name, JsonApplyFn applyJson = nullptr) {
  static_assert(std::is_trivially_copyable_v<T>, "ECS components must be trivially copyable");
  if (ComponentTag<T>::id == kInvalidEntity) {
    ComponentTag<T>::id =
        ComponentRegistry::instance().registerComponent(name, sizeof(T), alignof(T), applyJson);
  }
  return ComponentTag<T>::id;
}

template <typename T>
uint32_t componentId() {
  return ComponentTag<T>::id;
}

#define TC_REGISTER_COMPONENT(Name, Type) \
  const uint32_t kComp_##Name = ::tucano::ecs::registerComponent<Type>(#Name)

} // namespace tucano::ecs
