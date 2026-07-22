#pragma once

#include "Core/JobSystem.h"
#include "ECS/EntityManager.h"

#include <functional>
#include <string>

namespace tucano::ecs {

struct QueryDesc {
  std::vector<std::string> componentsRW; // read-write (obrigatórios)
  std::vector<std::string> componentsRO; // read-only (obrigatórios)
  std::vector<std::string> componentsRQ; // must-exist (não acessados)
  std::vector<std::string> componentsNO; // must-NOT-exist
};

using QueryId = uint32_t;

// Vista de um chunk durante a query; ponteiros SoA crus + índice da entidade corrente.
class ComponentAccessor {
public:
  uint32_t count() const { return m_chunk->count; }
  const Entity* entities() const { return m_chunk->entities.data(); }
  uint32_t current() const { return m_current; }

  void* columnById(uint32_t comp) const {
    const int s = m_arch->slot(comp);
    return s >= 0 ? m_arch->chunks[m_chunkIdx].data.get() + m_arch->offsets[size_t(s)] : nullptr;
  }
  template <typename T>
  T* column() const {
    return static_cast<T*>(columnById(componentId<T>()));
  }
  // Componente da entidade corrente (callbacks por entidade).
  template <typename T>
  T& get() const {
    return column<T>()[m_current];
  }

private:
  friend class QueryManager;
  EntityManager::Archetype* m_arch = nullptr;
  EntityManager::Chunk* m_chunk = nullptr;
  uint32_t m_chunkIdx = 0;
  uint32_t m_current = 0;
};

class QueryManager {
public:
  explicit QueryManager(EntityManager& em) : m_em(em) {}

  QueryId registerQuery(const QueryDesc& desc);

  // Callback por entidade (assinatura do guia).
  void performQuery(QueryId id, const std::function<void(Entity, ComponentAccessor&)>& cb);
  // Callback por chunk (caminho rápido).
  void performQueryChunks(QueryId id, const std::function<void(ComponentAccessor&)>& cb);
  // Multi-thread: chunks distribuídos pelo JobSystem. O callback deve ser thread-safe.
  void performQueryMT(QueryId id, core::JobSystem& jobs,
                      const std::function<void(Entity, ComponentAccessor&)>& cb);

private:
  struct Query {
    std::vector<uint32_t> required; // RW + RO + RQ
    std::vector<uint32_t> excluded;
    uint64_t bloom = 0;
    uint64_t signature = 0;        // dedup: hash de required+excluded
    std::vector<uint32_t> matched; // archetype ids
    uint32_t archSeen = 0;         // archetypeVersion já processada
  };
  void refresh(Query& q);
  static bool matches(const EntityManager::Archetype& a, const Query& q);

  EntityManager& m_em;
  std::vector<Query> m_queries;
  std::unordered_map<uint64_t, QueryId> m_bySignature;
};

} // namespace tucano::ecs
