#pragma once

#include "ECS/ComponentTypes.h"

#include <memory>
#include <span>
#include <vector>

namespace tucano::ecs {

// Archetype/chunk storage (Dagor-style): entidades com o mesmo conjunto de componentes vivem
// juntas em chunks SoA de capacidade power-of-2; add/remove de componente migra a entidade.
class EntityManager {
public:
  static constexpr uint32_t kChunkBytes = 16384;

  struct Chunk {
    std::unique_ptr<uint8_t[]> data; // SoA: componente k em [offsets[k], offsets[k] + size*capacity)
    std::vector<Entity> entities;    // slot → entity
    uint32_t count = 0;
  };

  struct Archetype {
    std::vector<uint32_t> comps;   // ids ordenados
    std::vector<uint32_t> offsets; // byte offset do array SoA de cada componente no chunk
    std::vector<Chunk> chunks;
    uint64_t bloom = 0;
    uint32_t entitySize = 0; // soma dos tamanhos
    uint32_t capacity = 1;   // entidades por chunk (power of 2)
    uint32_t firstNonFull = 0;

    int slot(uint32_t comp) const; // índice em comps ou -1
    void* column(uint32_t chunkIdx, int compSlot) {
      return chunks[chunkIdx].data.get() + offsets[size_t(compSlot)];
    }
    uint32_t compSize(int compSlot) const;
  };

  EntityManager();

  Entity create(std::span<const uint32_t> comps);
  Entity create(std::initializer_list<uint32_t> comps) {
    return create(std::span<const uint32_t>(comps.begin(), comps.size()));
  }
  void destroy(Entity e);
  bool alive(Entity e) const;

  void* get(Entity e, uint32_t comp);
  void* add(Entity e, uint32_t comp); // migra archetype; retorna ptr (zerado se novo)
  void remove(Entity e, uint32_t comp);
  bool has(Entity e, uint32_t comp) const;

  template <typename T>
  T* get(Entity e) {
    return static_cast<T*>(get(e, componentId<T>()));
  }
  template <typename T>
  T* add(Entity e, const T& v = {}) {
    auto* p = static_cast<T*>(add(e, componentId<T>()));
    if (p) {
      *p = v;
    }
    return p;
  }

  uint32_t liveCount() const { return m_liveCount; }
  const std::vector<Archetype>& archetypes() const { return m_archetypes; }
  std::vector<Archetype>& archetypes() { return m_archetypes; }
  // Muda sempre que um archetype novo aparece (queries recacheiam a lista de matches).
  uint32_t archetypeVersion() const { return uint32_t(m_archetypes.size()); }

private:
  struct Record {
    uint32_t arch = 0;
    uint32_t chunk = 0;
    uint32_t index = 0;
    uint16_t gen = 0;
    bool alive = false;
  };

  uint32_t getOrCreateArchetype(std::span<const uint32_t> sortedComps);
  // Aloca um slot no archetype (novo chunk se preciso); retorna chunk/index.
  void allocSlot(uint32_t archId, Entity e, uint32_t& outChunk, uint32_t& outIndex);
  // Swap-remove do slot; conserta o record da entidade movida.
  void freeSlot(uint32_t archId, uint32_t chunkIdx, uint32_t index);
  Record* recordOf(Entity e);
  const Record* recordOf(Entity e) const;

  std::vector<Archetype> m_archetypes;
  std::unordered_map<uint64_t, uint32_t> m_archByHash;
  std::vector<Record> m_records;
  std::vector<uint32_t> m_freeIndices;
  uint32_t m_liveCount = 0;
};

} // namespace tucano::ecs
