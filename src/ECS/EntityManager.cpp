#include "ECS/EntityManager.h"

#include <algorithm>
#include <cstring>

namespace tucano::ecs {
namespace {

uint64_t hashComps(std::span<const uint32_t> comps) {
  uint64_t h = 1469598103934665603ull;
  for (uint32_t c : comps) {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}

} // namespace

int EntityManager::Archetype::slot(uint32_t comp) const {
  auto it = std::lower_bound(comps.begin(), comps.end(), comp);
  if (it != comps.end() && *it == comp) {
    return int(it - comps.begin());
  }
  return -1;
}

uint32_t EntityManager::Archetype::compSize(int compSlot) const {
  return ComponentRegistry::instance().desc(comps[size_t(compSlot)]).size;
}

EntityManager::EntityManager() {
  getOrCreateArchetype({}); // archetype 0: entidades vazias
}

uint32_t EntityManager::getOrCreateArchetype(std::span<const uint32_t> sortedComps) {
  const uint64_t h = hashComps(sortedComps);
  auto it = m_archByHash.find(h);
  if (it != m_archByHash.end()) {
    return it->second; // caso comum: nenhuma alocação
  }
  Archetype a;
  a.comps.assign(sortedComps.begin(), sortedComps.end());
  auto& reg = ComponentRegistry::instance();
  a.entitySize = 0;
  for (uint32_t c : a.comps) {
    a.bloom |= componentBloomBits(c);
    a.entitySize += reg.desc(c).size;
  }
  a.capacity = 1024;
  if (a.entitySize > 0) {
    a.capacity = 1;
    while (a.capacity * a.entitySize * 2 <= kChunkBytes && a.capacity < 1024) {
      a.capacity <<= 1;
    }
  }
  a.offsets.resize(a.comps.size());
  uint32_t off = 0;
  for (size_t i = 0; i < a.comps.size(); ++i) {
    a.offsets[i] = off;
    off += reg.desc(a.comps[i]).size * a.capacity;
  }
  const uint32_t id = uint32_t(m_archetypes.size());
  m_archetypes.push_back(std::move(a));
  m_archByHash.emplace(h, id);
  return id;
}

void EntityManager::allocSlot(uint32_t archId, Entity e, uint32_t& outChunk, uint32_t& outIndex) {
  auto& a = m_archetypes[archId];
  uint32_t ci = a.firstNonFull;
  while (ci < a.chunks.size() && a.chunks[ci].count >= a.capacity) {
    ++ci;
  }
  if (ci >= a.chunks.size()) {
    Chunk c;
    const uint32_t bytes = a.entitySize * a.capacity;
    if (bytes > 0) {
      c.data = std::make_unique<uint8_t[]>(bytes);
    }
    c.entities.resize(a.capacity, kInvalidEntity);
    a.chunks.push_back(std::move(c));
    ci = uint32_t(a.chunks.size() - 1);
  }
  a.firstNonFull = ci;
  auto& chunk = a.chunks[ci];
  const uint32_t idx = chunk.count++;
  chunk.entities[idx] = e;
  // Zera os componentes do slot novo.
  for (size_t k = 0; k < a.comps.size(); ++k) {
    const uint32_t sz = a.compSize(int(k));
    std::memset(chunk.data.get() + a.offsets[k] + size_t(idx) * sz, 0, sz);
  }
  outChunk = ci;
  outIndex = idx;
}

void EntityManager::freeSlot(uint32_t archId, uint32_t chunkIdx, uint32_t index) {
  auto& a = m_archetypes[archId];
  auto& chunk = a.chunks[chunkIdx];
  const uint32_t last = chunk.count - 1;
  if (index != last) {
    // Move o último slot para o buraco e conserta o record da entidade movida.
    for (size_t k = 0; k < a.comps.size(); ++k) {
      const uint32_t sz = a.compSize(int(k));
      uint8_t* base = chunk.data.get() + a.offsets[k];
      std::memcpy(base + size_t(index) * sz, base + size_t(last) * sz, sz);
    }
    const Entity moved = chunk.entities[last];
    chunk.entities[index] = moved;
    if (auto* r = recordOf(moved)) {
      r->index = index;
    }
  }
  chunk.entities[last] = kInvalidEntity;
  --chunk.count;
  if (chunkIdx < a.firstNonFull) {
    a.firstNonFull = chunkIdx;
  }
}

EntityManager::Record* EntityManager::recordOf(Entity e) {
  const uint32_t idx = entityIndex(e);
  if (idx >= m_records.size()) {
    return nullptr;
  }
  Record& r = m_records[idx];
  if (!r.alive || r.gen != entityGeneration(e)) {
    return nullptr;
  }
  return &r;
}

const EntityManager::Record* EntityManager::recordOf(Entity e) const {
  return const_cast<EntityManager*>(this)->recordOf(e);
}

Entity EntityManager::create(std::span<const uint32_t> comps) {
  // Ordena+dedup num buffer de stack no caso comum (evita alocação de heap por create).
  uint32_t archId;
  if (comps.size() <= 16) {
    uint32_t buf[16];
    uint32_t n = 0;
    for (uint32_t c : comps) {
      buf[n++] = c;
    }
    std::sort(buf, buf + n);
    n = uint32_t(std::unique(buf, buf + n) - buf);
    archId = getOrCreateArchetype(std::span<const uint32_t>(buf, n));
  } else {
    std::vector<uint32_t> sorted(comps.begin(), comps.end());
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    archId = getOrCreateArchetype(std::span<const uint32_t>(sorted.data(), sorted.size()));
  }

  uint32_t idx;
  if (!m_freeIndices.empty()) {
    idx = m_freeIndices.back();
    m_freeIndices.pop_back();
  } else {
    idx = uint32_t(m_records.size());
    if (m_records.size() == m_records.capacity()) {
      m_records.reserve(std::max<size_t>(1024, m_records.size() * 2)); // amortiza realocações
    }
    m_records.push_back({});
  }
  Record& r = m_records[idx];
  const Entity e = makeEntity(idx, r.gen);
  r.arch = archId;
  r.alive = true;
  allocSlot(archId, e, r.chunk, r.index);
  ++m_liveCount;
  return e;
}

void EntityManager::destroy(Entity e) {
  Record* r = recordOf(e);
  if (!r) {
    return;
  }
  freeSlot(r->arch, r->chunk, r->index);
  r->alive = false;
  r->gen = uint16_t((r->gen + 1) & 0xFFF); // 12-bit generation wrap
  m_freeIndices.push_back(entityIndex(e));
  --m_liveCount;
}

bool EntityManager::alive(Entity e) const { return recordOf(e) != nullptr; }

void* EntityManager::get(Entity e, uint32_t comp) {
  Record* r = recordOf(e);
  if (!r) {
    return nullptr;
  }
  auto& a = m_archetypes[r->arch];
  const int s = a.slot(comp);
  if (s < 0) {
    return nullptr;
  }
  return static_cast<uint8_t*>(a.column(r->chunk, s)) + size_t(r->index) * a.compSize(s);
}

bool EntityManager::has(Entity e, uint32_t comp) const {
  const Record* r = recordOf(e);
  return r && m_archetypes[r->arch].slot(comp) >= 0;
}

void* EntityManager::add(Entity e, uint32_t comp) {
  Record* r = recordOf(e);
  if (!r) {
    return nullptr;
  }
  {
    auto& a = m_archetypes[r->arch];
    const int s = a.slot(comp);
    if (s >= 0) {
      return static_cast<uint8_t*>(a.column(r->chunk, s)) + size_t(r->index) * a.compSize(s);
    }
  }
  // Migra para o archetype com o componente extra.
  const uint32_t srcArch = r->arch;
  std::vector<uint32_t> comps = m_archetypes[srcArch].comps;
  comps.insert(std::upper_bound(comps.begin(), comps.end(), comp), comp);
  const uint32_t dstArch = getOrCreateArchetype(std::move(comps));

  const uint32_t srcChunk = r->chunk;
  const uint32_t srcIndex = r->index;
  uint32_t dstChunk, dstIndex;
  allocSlot(dstArch, e, dstChunk, dstIndex);

  auto& src = m_archetypes[srcArch];
  auto& dst = m_archetypes[dstArch];
  for (size_t k = 0; k < src.comps.size(); ++k) {
    const int ds = dst.slot(src.comps[k]);
    const uint32_t sz = src.compSize(int(k));
    std::memcpy(static_cast<uint8_t*>(dst.column(dstChunk, ds)) + size_t(dstIndex) * sz,
                static_cast<uint8_t*>(src.column(srcChunk, int(k))) + size_t(srcIndex) * sz, sz);
  }
  freeSlot(srcArch, srcChunk, srcIndex);
  r->arch = dstArch;
  r->chunk = dstChunk;
  r->index = dstIndex;
  const int ns = dst.slot(comp);
  return static_cast<uint8_t*>(dst.column(dstChunk, ns)) + size_t(dstIndex) * dst.compSize(ns);
}

void EntityManager::remove(Entity e, uint32_t comp) {
  Record* r = recordOf(e);
  if (!r) {
    return;
  }
  auto& srcA = m_archetypes[r->arch];
  if (srcA.slot(comp) < 0) {
    return;
  }
  const uint32_t srcArch = r->arch;
  std::vector<uint32_t> comps = srcA.comps;
  comps.erase(std::find(comps.begin(), comps.end(), comp));
  const uint32_t dstArch = getOrCreateArchetype(std::move(comps));

  const uint32_t srcChunk = r->chunk;
  const uint32_t srcIndex = r->index;
  uint32_t dstChunk, dstIndex;
  allocSlot(dstArch, e, dstChunk, dstIndex);

  auto& src = m_archetypes[srcArch];
  auto& dst = m_archetypes[dstArch];
  for (size_t k = 0; k < dst.comps.size(); ++k) {
    const int ss = src.slot(dst.comps[k]);
    const uint32_t sz = dst.compSize(int(k));
    std::memcpy(static_cast<uint8_t*>(dst.column(dstChunk, int(k))) + size_t(dstIndex) * sz,
                static_cast<uint8_t*>(src.column(srcChunk, ss)) + size_t(srcIndex) * sz, sz);
  }
  freeSlot(srcArch, srcChunk, srcIndex);
  r->arch = dstArch;
  r->chunk = dstChunk;
  r->index = dstIndex;
}

} // namespace tucano::ecs
