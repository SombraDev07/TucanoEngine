#include "ECS/QueryManager.h"

#include <algorithm>
#include <stdexcept>

namespace tucano::ecs {

QueryId QueryManager::registerQuery(const QueryDesc& desc) {
  auto& reg = ComponentRegistry::instance();
  Query q;
  auto addReq = [&](const std::vector<std::string>& names) {
    for (const auto& n : names) {
      const uint32_t id = reg.find(n);
      if (id == kInvalidEntity) {
        throw std::runtime_error("ECS query: unknown component '" + n + "'");
      }
      q.required.push_back(id);
      q.bloom |= componentBloomBits(id);
    }
  };
  addReq(desc.componentsRW);
  addReq(desc.componentsRO);
  addReq(desc.componentsRQ);
  for (const auto& n : desc.componentsNO) {
    const uint32_t id = reg.find(n);
    if (id != kInvalidEntity) {
      q.excluded.push_back(id);
    }
  }
  std::sort(q.required.begin(), q.required.end());
  q.required.erase(std::unique(q.required.begin(), q.required.end()), q.required.end());
  std::sort(q.excluded.begin(), q.excluded.end());

  // Assinatura para dedup: chamadas repetidas com o mesmo filtro reusam a mesma query cacheada.
  uint64_t sig = 1469598103934665603ull;
  auto mix = [&](uint32_t v) {
    sig ^= v;
    sig *= 1099511628211ull;
  };
  for (uint32_t c : q.required) {
    mix(c);
  }
  mix(0xFFFFFFFFu); // separador required|excluded
  for (uint32_t c : q.excluded) {
    mix(c);
  }
  q.signature = sig;
  if (auto it = m_bySignature.find(sig); it != m_bySignature.end()) {
    return it->second;
  }
  const QueryId id = QueryId(m_queries.size());
  m_queries.push_back(std::move(q));
  m_bySignature.emplace(sig, id);
  return id;
}

bool QueryManager::matches(const EntityManager::Archetype& a, const Query& q) {
  // Bloom pré-filtro O(1); depois verificação exata.
  if ((a.bloom & q.bloom) != q.bloom) {
    return false;
  }
  for (uint32_t c : q.required) {
    if (a.slot(c) < 0) {
      return false;
    }
  }
  for (uint32_t c : q.excluded) {
    if (a.slot(c) >= 0) {
      return false;
    }
  }
  return true;
}

void QueryManager::refresh(Query& q) {
  const uint32_t version = m_em.archetypeVersion();
  if (q.archSeen == version) {
    return;
  }
  const auto& archs = m_em.archetypes();
  for (uint32_t i = q.archSeen; i < version; ++i) {
    if (matches(archs[i], q)) {
      q.matched.push_back(i);
    }
  }
  q.archSeen = version;
}

void QueryManager::performQuery(QueryId id, const std::function<void(Entity, ComponentAccessor&)>& cb) {
  Query& q = m_queries[id];
  refresh(q);
  auto& archs = m_em.archetypes();
  for (uint32_t ai : q.matched) {
    auto& a = archs[ai];
    for (uint32_t ci = 0; ci < a.chunks.size(); ++ci) {
      auto& chunk = a.chunks[ci];
      if (chunk.count == 0) {
        continue;
      }
      ComponentAccessor acc;
      acc.m_arch = &a;
      acc.m_chunk = &chunk;
      acc.m_chunkIdx = ci;
      for (uint32_t i = 0; i < chunk.count; ++i) {
        acc.m_current = i;
        cb(chunk.entities[i], acc);
      }
    }
  }
}

void QueryManager::performQueryChunks(QueryId id, const std::function<void(ComponentAccessor&)>& cb) {
  Query& q = m_queries[id];
  refresh(q);
  auto& archs = m_em.archetypes();
  for (uint32_t ai : q.matched) {
    auto& a = archs[ai];
    for (uint32_t ci = 0; ci < a.chunks.size(); ++ci) {
      if (a.chunks[ci].count == 0) {
        continue;
      }
      ComponentAccessor acc;
      acc.m_arch = &a;
      acc.m_chunk = &a.chunks[ci];
      acc.m_chunkIdx = ci;
      cb(acc);
    }
  }
}

void QueryManager::performQueryMT(QueryId id, core::JobSystem& jobs,
                                  const std::function<void(Entity, ComponentAccessor&)>& cb) {
  Query& q = m_queries[id];
  refresh(q);
  auto& archs = m_em.archetypes();
  // Achata a lista de chunks para dividir entre os workers.
  struct Item {
    EntityManager::Archetype* arch;
    uint32_t chunkIdx;
  };
  std::vector<Item> items;
  for (uint32_t ai : q.matched) {
    auto& a = archs[ai];
    for (uint32_t ci = 0; ci < a.chunks.size(); ++ci) {
      if (a.chunks[ci].count > 0) {
        items.push_back({&a, ci});
      }
    }
  }
  jobs.parallelFor(0, uint32_t(items.size()), 1, [&](uint32_t begin, uint32_t end) {
    for (uint32_t it = begin; it < end; ++it) {
      auto& a = *items[it].arch;
      auto& chunk = a.chunks[items[it].chunkIdx];
      ComponentAccessor acc;
      acc.m_arch = &a;
      acc.m_chunk = &chunk;
      acc.m_chunkIdx = items[it].chunkIdx;
      for (uint32_t i = 0; i < chunk.count; ++i) {
        acc.m_current = i;
        cb(chunk.entities[i], acc);
      }
    }
  });
}

} // namespace tucano::ecs
