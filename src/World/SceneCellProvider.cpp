#include "World/SceneCellProvider.h"

#include "AssetPipeline/GLTFLoader.h"

#include <meshoptimizer.h>

#include "Physics/PhysicsWorld.h"
#include "Renderer/DevTexture.h"
#include "Renderer/Material.h"
#include "Renderer/Mesh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace tucano::world {
namespace {

/// The name we stamp on every object we create, carrying its id. release() finds its own objects
/// by this rather than by array index, because unrelated cells unloading shifts every index.
std::string objectName(uint64_t id, const std::string& base) {
  return "wm#" + std::to_string(id) + ":" + base;
}

uint64_t idFromName(const std::string& name) {
  if (name.rfind("wm#", 0) != 0) return 0;
  const size_t colon = name.find(':', 3);
  if (colon == std::string::npos) return 0;
  return std::strtoull(name.substr(3, colon - 3).c_str(), nullptr, 10);
}

constexpr uint8_t kDeltaMoved = 1;
constexpr uint8_t kDeltaDestroyed = 2;

/// Combines a cell key and a layer into one map key, the same mix CellPersistenceStore uses.
uint64_t liveKey(const CellId& id, WorldLayer layer) {
  uint64_t h = id.key();
  h ^= (uint64_t(layer) + 0x9E3779B97F4A7C15ull) + (h << 6) + (h >> 2);
  return h;
}

CpuMesh buildCubeCpu() {
  const float s = 0.5f;
  std::vector<Vertex> verts;
  std::vector<uint32_t> indices;
  auto face = [&](glm::vec3 n, glm::vec3 t, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3) {
    const uint32_t base = uint32_t(verts.size());
    verts.push_back({p0, n, {t.x, t.y, t.z, 1}, {1, 1}, {1, 1, 1, 1}});
    verts.push_back({p1, n, {t.x, t.y, t.z, 1}, {0, 1}, {1, 1, 1, 1}});
    verts.push_back({p2, n, {t.x, t.y, t.z, 1}, {0, 0}, {1, 1, 1, 1}});
    verts.push_back({p3, n, {t.x, t.y, t.z, 1}, {1, 0}, {1, 1, 1, 1}});
    indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
  };
  face({0, 1, 0}, {1, 0, 0}, {-s, s, -s}, {s, s, -s}, {s, s, s}, {-s, s, s});
  face({0, -1, 0}, {1, 0, 0}, {-s, -s, s}, {s, -s, s}, {s, -s, -s}, {-s, -s, -s});
  face({0, 0, 1}, {1, 0, 0}, {-s, -s, s}, {s, -s, s}, {s, s, s}, {-s, s, s});
  face({0, 0, -1}, {-1, 0, 0}, {s, -s, -s}, {-s, -s, -s}, {-s, s, -s}, {s, s, -s});
  face({1, 0, 0}, {0, 0, -1}, {s, -s, s}, {s, -s, -s}, {s, s, -s}, {s, s, s});
  face({-1, 0, 0}, {0, 0, 1}, {-s, -s, -s}, {-s, -s, s}, {-s, s, s}, {-s, s, -s});
  return {std::move(verts), std::move(indices)};
}

CpuMesh buildSphereCpu() {
  const int segments = 16;
  std::vector<Vertex> verts;
  std::vector<uint32_t> indices;
  for (int y = 0; y <= segments; ++y) {
    const float v = float(y) / float(segments);
    const float phi = v * 3.14159265f;
    for (int x = 0; x <= segments; ++x) {
      const float u = float(x) / float(segments);
      const float theta = u * 6.2831853f;
      const glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi),
                        std::sin(phi) * std::sin(theta));
      verts.push_back({n * 0.5f, n, {1, 0, 0, 1}, {u, v}, {1, 1, 1, 1}});
    }
  }
  for (int y = 0; y < segments; ++y) {
    for (int x = 0; x < segments; ++x) {
      const uint32_t a = uint32_t(y * (segments + 1) + x);
      const uint32_t b = a + uint32_t(segments + 1);
      indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
    }
  }
  return {std::move(verts), std::move(indices)};
}

CpuMesh buildPlaneCpu() {
  const float s = 0.5f;
  std::vector<Vertex> verts = {
      {{-s, 0, -s}, {0, 1, 0}, {1, 0, 0, 1}, {0, 0}, {1, 1, 1, 1}},
      {{s, 0, -s}, {0, 1, 0}, {1, 0, 0, 1}, {1, 0}, {1, 1, 1, 1}},
      {{s, 0, s}, {0, 1, 0}, {1, 0, 0, 1}, {1, 1}, {1, 1, 1, 1}},
      {{-s, 0, s}, {0, 1, 0}, {1, 0, 0, 1}, {0, 1}, {1, 1, 1, 1}},
  };
  std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
  return {std::move(verts), std::move(indices)};
}

/// Uploads CPU geometry with an accurate AABB.
///
/// The bounds are computed, never defaulted: a submesh left with a zero AABB is a degenerate box at
/// the origin, and frustum culling then rejects it everywhere except the origin — geometry that
/// exists, uploads, and never appears on screen.
std::shared_ptr<Mesh> uploadCpuMesh(rhi::Device& device, const CpuMesh& cpu) {
  if (cpu.verts.empty() || cpu.idx.empty()) return nullptr;
  glm::vec3 lo(1e30f), hi(-1e30f);
  for (const Vertex& v : cpu.verts) {
    lo = glm::min(lo, v.position);
    hi = glm::max(hi, v.position);
  }
  SubMesh sub{};
  sub.indexCount = uint32_t(cpu.idx.size());
  sub.aabbMin = lo;
  sub.aabbMax = hi;
  return Mesh::create(device, cpu.verts, cpu.idx, {sub});
}

} // namespace

SceneCellProvider::SceneCellProvider(rhi::Device& device, Scene& scene, std::string worldRoot)
    : m_device(device), m_scene(scene), m_worldRoot(std::move(worldRoot)) {}

SceneCellProvider::~SceneCellProvider() = default;

const CpuMesh& SceneCellProvider::cpuPrimitive(CellObjectKind kind) {
  const uint32_t key = uint32_t(kind);
  auto it = m_cpuPrimitives.find(key);
  if (it != m_cpuPrimitives.end()) return it->second;

  CpuMesh cpu;
  switch (kind) {
    case CellObjectKind::Sphere: cpu = buildSphereCpu(); break;
    case CellObjectKind::Plane: cpu = buildPlaneCpu(); break;
    default: cpu = buildCubeCpu(); break;
  }
  return m_cpuPrimitives.emplace(key, std::move(cpu)).first->second;
}

std::shared_ptr<Mesh> SceneCellProvider::meshFor(CellObjectKind kind) {
  const uint32_t key = uint32_t(kind);
  auto it = m_meshCache.find(key);
  if (it != m_meshCache.end()) return it->second;

  auto mesh = uploadCpuMesh(m_device, cpuPrimitive(kind));
  m_meshCache[key] = mesh;
  return mesh;
}

std::shared_ptr<Mesh> SceneCellProvider::buildMerged(const std::vector<const CellObject*>& objects,
                                                     uint32_t lod) {
  CpuMesh merged;
  for (const CellObject* o : objects) {
    const CpuMesh& src = cpuPrimitive(o->kind);
    Transform xf;
    xf.translation = o->position;
    xf.rotation = o->rotation;
    xf.scale = o->scale * o->size;
    const glm::mat4 m = xf.matrix();
    // Normals need the inverse-transpose, or non-uniform scale (which every landmark has) would
    // shear them and the merged cell would light wrongly.
    const glm::mat3 nrm = glm::mat3(glm::transpose(glm::inverse(m)));

    const uint32_t base = uint32_t(merged.verts.size());
    for (const Vertex& v : src.verts) {
      Vertex out = v;
      out.position = glm::vec3(m * glm::vec4(v.position, 1.0f));
      out.normal = glm::normalize(nrm * v.normal);
      merged.verts.push_back(out);
    }
    for (uint32_t i : src.idx) merged.idx.push_back(base + i);
  }
  if (merged.verts.empty()) return nullptr;

  // Simplify harder the further out the cell is. The triangle saving is a bonus; the object-count
  // collapse above is the part that matters for the descriptor ceiling.
  const float keep = lod >= 3 ? 0.12f : (lod == 2 ? 0.25f : 0.5f);
  const size_t target = std::max<size_t>(size_t(double(merged.idx.size()) * double(keep)) / 3 * 3, 12);
  if (target < merged.idx.size()) {
    std::vector<uint32_t> simplified(merged.idx.size());
    const size_t n = meshopt_simplify(simplified.data(), merged.idx.data(), merged.idx.size(),
                                      &merged.verts[0].position.x, merged.verts.size(),
                                      sizeof(Vertex), target, 0.05f, 0, nullptr);
    if (n >= 3) {
      simplified.resize(n);
      merged.idx = std::move(simplified);
    }
  }

  ++m_hlodBuilds;
  return uploadCpuMesh(m_device, merged);
}

std::shared_ptr<Material> SceneCellProvider::materialFor(const CellObject& o) {
  // Quantize the appearance into a key. Two objects whose colour differs in the fourth decimal are
  // the same material for every practical purpose, and collapsing them is what keeps the descriptor
  // heap from filling.
  auto q = [](float v) { return uint64_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
  const uint64_t key = (q(o.baseColor.r) << 40) | (q(o.baseColor.g) << 32) |
                       (q(o.baseColor.b) << 24) | (q(o.metallic) << 16) | (q(o.roughness) << 8);

  auto it = m_materialCache.find(key);
  if (it != m_materialCache.end()) return it->second;

  auto mat = std::make_shared<Material>();
  mat->baseColorFactor = glm::vec4(o.baseColor, 1.0f);
  mat->metallicFactor = o.metallic;
  mat->roughnessFactor = o.roughness;
  // A material with no albedo/normal bound samples a null texture and renders pure black — which
  // is what made the first flythrough look empty even though the objects were there. The engine's
  // dev textures are shared singletons, so this costs nothing per material.
  mat->albedo = devtex::defaultAlbedo(m_device);
  // Emissive as well as lit. A streaming test has to make "did this object arrive?" unambiguous,
  // and geometry that depends on the sun angle to be visible answers that question badly.
  mat->emissiveFactor = o.baseColor * 0.9f;
  mat->normal = devtex::defaultNormal(m_device);
  m_materialCache[key] = mat;
  return mat;
}

const std::vector<SceneCellProvider::GltfPart>* SceneCellProvider::gltfFor(
    const std::string& path) {
  auto it = m_gltfCache.find(path);
  if (it != m_gltfCache.end()) return it->second.empty() ? nullptr : &it->second;

  // Resolve relative to the world root first, then as given. A cell file should be able to name
  // "props/rock.gltf" and have it mean something next to the world it belongs to.
  std::string resolved = m_worldRoot + "/" + path;
  if (!std::filesystem::exists(resolved)) resolved = path;

  std::vector<GltfPart> parts;
  Scene tmp;
  if (std::filesystem::exists(resolved) && loadGLTFScene(m_device, resolved, tmp)) {
    parts.reserve(tmp.objects.size());
    for (RenderObject& ro : tmp.objects) {
      if (!ro.mesh) continue;
      GltfPart part;
      part.mesh = ro.mesh;
      part.materials = ro.materials;
      part.local = ro.worldMatrix;
      parts.push_back(std::move(part));
    }
    ++m_gltfLoads;
  } else {
    std::printf("[world] glTF not found or failed to load: %s\n", path.c_str());
  }

  // Cache even on failure, so a bad path costs one parse attempt rather than one per instance.
  auto& stored = m_gltfCache[path];
  stored = std::move(parts);
  return stored.empty() ? nullptr : &stored;
}

bool SceneCellProvider::readBytes(const CellId& id, WorldLayer layer, std::vector<uint8_t>& out) {
  // Background thread: filesystem only, no engine state touched.
  const std::string path = cellFilePath(m_worldRoot, id, uint32_t(layer));

  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    // A cell with no file for this layer is normal — the baker only writes layers that have
    // content. Report it as "no bytes", which the scheduler treats as an empty layer.
    ++m_missingFiles;
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size <= 0) {
    std::fclose(f);
    return false;
  }
  out.resize(static_cast<size_t>(size));
  const size_t read = std::fread(out.data(), 1, out.size(), f);
  std::fclose(f);
  if (read != out.size()) {
    out.clear();
    return false;
  }
  return true;
}

bool SceneCellProvider::deserialize(const CellId& id, WorldLayer layer,
                                    const std::vector<uint8_t>& bytes, CellContent& out) {
  // Background thread: pure parsing. The parsed records are handed to upload() through userData;
  // no GPU or Scene access happens here.
  const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  auto* file = new CellFile();
  if (!CellFile::fromJson(text, *file)) {
    delete file;
    return false;
  }
  file->id = id;
  file->layer = uint32_t(layer);

  out.userData = file;
  out.cpuBytes = bytes.size() + file->objects.size() * sizeof(CellObject);
  out.gpuBytes = 0; // filled in by upload once the objects exist
  return true;
}

bool SceneCellProvider::upload(const CellId& id, WorldLayer layer, CellContent& content) {
  // Calling thread: owns the device and the Scene. This is the only stage allowed to touch either.
  auto* file = static_cast<CellFile*>(content.userData);
  if (!file) return true; // empty layer

  auto live = std::make_unique<LoadedCellContent>();
  live->id = id;
  live->layer = uint32_t(layer);
  live->objectIds.reserve(file->objects.size());

  // ── HLOD ──
  // Beyond the first distance band the whole layer becomes ONE merged, simplified object instead of
  // dozens. Object count is what exhausts the per-frame descriptor heap, so this is the change that
  // actually lets the load radius grow.
  if (content.lod > 0) {
    std::vector<const CellObject*> mergeable;
    for (const CellObject& o : file->objects) {
      if (o.kind != CellObjectKind::Gltf) mergeable.push_back(&o);
    }
    if (!mergeable.empty()) {
      if (auto mesh = buildMerged(mergeable, content.lod)) {
        RenderObject hro;
        hro.mesh = mesh;
        live->mergedMesh = mesh; // kept so release() can retire it instead of freeing it outright
        hro.materials.push_back(materialFor(*mergeable.front()));
        hro.transform = {}; // geometry is already in world space
        hro.worldMatrix = glm::mat4(1.0f);

        const uint64_t hid = m_nextObjectId++;
        hro.name = objectName(hid, "hlod");
        live->objectIds.push_back(hid);
        // Index 0xFFFFFFFF marks "this is not one authored object" — persistence must not try to
        // diff a merged blob against a single authored transform.
        live->cellIndices.push_back(0xFFFFFFFFu);
        live->authored.push_back(Transform{});
        live->partLocal.push_back(glm::mat4(1.0f));
        m_scene.objects.push_back(std::move(hro));
      }
    }
    // glTF instances still come through individually; they are one per cell, so they do not drive
    // the object count, and merging them would need their CPU geometry which the loader discards.
    for (const CellObject& o : file->objects) {
      if (o.kind != CellObjectKind::Gltf) continue;
      Transform gxf;
      gxf.translation = o.position;
      gxf.rotation = o.rotation;
      gxf.scale = o.scale * o.size;
      const std::vector<GltfPart>* proto = gltfFor(o.path);
      if (!proto) continue;
      for (const GltfPart& part : *proto) {
        RenderObject gro;
        gro.mesh = part.mesh;
        gro.materials = part.materials.empty()
                            ? std::vector<std::shared_ptr<Material>>{materialFor(o)}
                            : part.materials;
        gro.transform = gxf;
        gro.worldMatrix = gxf.matrix() * part.local;
        const uint64_t gid = m_nextObjectId++;
        gro.name = objectName(gid, o.name);
        live->objectIds.push_back(gid);
        live->cellIndices.push_back(uint32_t(&o - file->objects.data()));
        live->authored.push_back(gxf);
        live->partLocal.push_back(part.local);
        m_scene.objects.push_back(std::move(gro));
      }
    }

    delete file;
    auto* hlodRaw = live.release();
    m_live[liveKey(id, layer)] = hlodRaw;
    content.userData = hlodRaw;
    content.gpuBytes = uint64_t(static_cast<LoadedCellContent*>(content.userData)->objectIds.size()) * 256;
    return true;
  }

  for (const CellObject& o : file->objects) {
    Transform xf;
    xf.translation = o.position;
    xf.rotation = o.rotation;
    xf.scale = o.scale * o.size;

    // A glTF instance expands into one RenderObject per part of the model, all sharing the same
    // cached meshes. Everything downstream — ids, persistence, colliders — treats them like any
    // other object, which is why the part's local matrix has to be carried alongside.
    if (o.kind == CellObjectKind::Gltf) {
      const std::vector<GltfPart>* proto = gltfFor(o.path);
      if (!proto) continue;
      for (const GltfPart& part : *proto) {
        RenderObject gro;
        gro.mesh = part.mesh;
        gro.materials = part.materials.empty() ? std::vector<std::shared_ptr<Material>>{materialFor(o)}
                                               : part.materials;
        gro.transform = xf;
        gro.worldMatrix = xf.matrix() * part.local;

        const uint64_t gid = m_nextObjectId++;
        gro.name = objectName(gid, o.name);
        live->objectIds.push_back(gid);
        live->cellIndices.push_back(uint32_t(&o - file->objects.data()));
        live->authored.push_back(xf);
        live->partLocal.push_back(part.local);
        m_scene.objects.push_back(std::move(gro));
      }
      continue;
    }

    RenderObject ro;
    ro.mesh = meshFor(o.kind);
    if (!ro.mesh) continue;

    ro.materials.push_back(materialFor(o));

    ro.transform = xf;
    ro.worldMatrix = ro.transform.matrix();

    const uint64_t objId = m_nextObjectId++;
    ro.name = objectName(objId, o.name);
    live->objectIds.push_back(objId);
    // Index within the authored file, plus the authored transform. Together these are what makes a
    // persistence delta meaningful: the index survives a reload, and the transform is the baseline
    // a change is measured against.
    live->cellIndices.push_back(uint32_t(&o - file->objects.data()));
    live->authored.push_back(ro.transform);
    live->partLocal.push_back(glm::mat4(1.0f));

    // Collision, for the objects whose cell file asks for it. Static bodies: streamed scenery does
    // not simulate, it is simulated AGAINST. The half-extent comes from the object's scale, since
    // the shared unit mesh carries no size of its own.
    if (m_physics && o.collider != CellColliderKind::None) {
      const glm::vec3 half = ro.transform.scale * o.size * 0.5f;
      JPH::BodyID body;
      if (o.collider == CellColliderKind::Sphere) {
        body = m_physics->createStaticSphere(std::max(half.x, std::max(half.y, half.z)),
                                             ro.transform.translation);
      } else {
        // Jolt rejects a box thinner than its convex radius, so never let an axis collapse.
        const glm::vec3 safeHalf = glm::max(half, glm::vec3(0.06f));
        body = m_physics->createStaticBox(safeHalf, ro.transform.translation, ro.transform.rotation);
      }
      if (!body.IsInvalid()) {
        live->bodyIds.push_back(body.GetIndexAndSequenceNumber());
        ++m_liveBodies;
      }
    }

    m_scene.objects.push_back(std::move(ro));
  }

  // The parsed file has served its purpose; the live handles replace it.
  delete file;
  auto* liveRaw = live.release();
  m_live[liveKey(id, layer)] = liveRaw;
  content.userData = liveRaw;
  // A rough VRAM figure. The meshes are shared, so the honest per-cell cost is the per-object
  // constants, not a whole vertex buffer each.
  content.gpuBytes = uint64_t(static_cast<LoadedCellContent*>(content.userData)->objectIds.size()) * 256;
  return true;
}

void SceneCellProvider::release(const CellId& id, WorldLayer layer, CellContent& content) {
  auto* live = static_cast<LoadedCellContent*>(content.userData);
  if (!live) return;

  // Remove exactly the objects this cell created. Matching by the stamped id rather than by index
  // is what makes this correct when other cells have unloaded in the meantime and shifted the array.
  if (!live->objectIds.empty()) {
    std::vector<uint64_t> ids = live->objectIds;
    std::sort(ids.begin(), ids.end());

    auto owned = [&ids](const RenderObject& ro) {
      const uint64_t id = idFromName(ro.name);
      return id != 0 && std::binary_search(ids.begin(), ids.end(), id);
    };
    // Frees the slots instead of compacting the array (C-09). Compacting used to shift every index
    // above the removed ones, and anything holding one — an entity's `RenderObjectComponent` — began
    // driving a different object without a word.
    m_scene.removeObjectsIf(owned);
  }

  // A merged mesh is owned by this cell alone. Retire it rather than letting the last reference die
  // here, or its GPU buffers vanish under a frame the GPU has not finished.
  if (live->mergedMesh) {
    m_retiredMeshes.push_back(std::move(live->mergedMesh));
    while (m_retiredMeshes.size() > kRetireDepth) m_retiredMeshes.pop_front();
  }

  // Physics first: a body outliving its cell is invisible until the body budget runs out.
  if (m_physics) {
    for (uint32_t raw : live->bodyIds) {
      m_physics->removeBody(JPH::BodyID(raw));
      if (m_liveBodies > 0) --m_liveBodies;
    }
  }

  m_live.erase(liveKey(id, layer));
  delete live;
  content.userData = nullptr;
  content.cpuBytes = 0;
  content.gpuBytes = 0;
}

LoadedCellContent* SceneCellProvider::findLive(const CellId& id, WorldLayer layer) {
  auto it = m_live.find(liveKey(id, layer));
  return it == m_live.end() ? nullptr : it->second;
}

const LoadedCellContent* SceneCellProvider::findLive(const CellId& id, WorldLayer layer) const {
  auto it = m_live.find(liveKey(id, layer));
  return it == m_live.end() ? nullptr : it->second;
}

RenderObject* SceneCellProvider::findObject(uint64_t runtimeId) {
  for (RenderObject& ro : m_scene.objects) {
    if (idFromName(ro.name) == runtimeId) return &ro;
  }
  return nullptr;
}

const RenderObject* SceneCellProvider::findObject(uint64_t runtimeId) const {
  for (const RenderObject& ro : m_scene.objects) {
    if (idFromName(ro.name) == runtimeId) return &ro;
  }
  return nullptr;
}

bool SceneCellProvider::moveObject(const CellId& id, WorldLayer layer, uint32_t cellIndex,
                                   const glm::vec3& delta) {
  LoadedCellContent* live = findLive(id, layer);
  if (!live) return false;
  for (size_t i = 0; i < live->cellIndices.size(); ++i) {
    if (live->cellIndices[i] != cellIndex) continue;
    RenderObject* ro = findObject(live->objectIds[i]);
    if (!ro) return false;
    ro->transform.translation += delta;
    ro->worldMatrix = ro->transform.matrix() * live->partLocal[i];
    return true;
  }
  return false;
}

bool SceneCellProvider::objectPosition(const CellId& id, WorldLayer layer, uint32_t cellIndex,
                                       glm::vec3& out) const {
  const LoadedCellContent* live = findLive(id, layer);
  if (!live) return false;
  for (size_t i = 0; i < live->cellIndices.size(); ++i) {
    if (live->cellIndices[i] != cellIndex) continue;
    const RenderObject* ro = findObject(live->objectIds[i]);
    if (!ro) return false;
    out = ro->transform.translation;
    return true;
  }
  return false;
}

bool SceneCellProvider::captureDelta(const CellId&, WorldLayer, const CellContent& content,
                                     CellDelta& out) {
  const auto* live = static_cast<const LoadedCellContent*>(content.userData);
  if (!live) return false;

  // Record only what actually differs from the authored state. A cell the player walked through
  // without touching must produce an empty delta and cost nothing.
  std::vector<uint8_t> buf;
  uint32_t changed = 0;
  auto put = [&buf](const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    buf.insert(buf.end(), b, b + n);
  };

  for (size_t i = 0; i < live->objectIds.size(); ++i) {
    const RenderObject* ro = findObject(live->objectIds[i]);
    const uint32_t cellIndex = live->cellIndices[i];

    if (!ro) {
      // Gone from the scene: gameplay destroyed it. That absence has to persist, or the object
      // would resurrect on every revisit.
      const uint8_t flags = kDeltaDestroyed;
      put(&cellIndex, sizeof(cellIndex));
      put(&flags, sizeof(flags));
      ++changed;
      continue;
    }

    const Transform& authored = live->authored[i];
    const Transform& now = ro->transform;
    const bool moved = glm::distance(authored.translation, now.translation) > 1e-4f ||
                       glm::distance(authored.scale, now.scale) > 1e-4f ||
                       std::fabs(glm::dot(authored.rotation, now.rotation)) < 0.99999f;
    if (!moved) continue;

    const uint8_t flags = kDeltaMoved;
    put(&cellIndex, sizeof(cellIndex));
    put(&flags, sizeof(flags));
    put(&now.translation, sizeof(glm::vec3));
    put(&now.rotation, sizeof(glm::quat));
    put(&now.scale, sizeof(glm::vec3));
    ++changed;
  }

  if (changed == 0) return false;

  out.bytes.clear();
  out.bytes.reserve(buf.size() + sizeof(uint32_t));
  const auto* c = reinterpret_cast<const uint8_t*>(&changed);
  out.bytes.insert(out.bytes.end(), c, c + sizeof(changed));
  out.bytes.insert(out.bytes.end(), buf.begin(), buf.end());
  return true;
}

void SceneCellProvider::applyDelta(const CellId&, WorldLayer, CellContent& content,
                                   const CellDelta& delta) {
  auto* live = static_cast<LoadedCellContent*>(content.userData);
  if (!live || delta.bytes.size() < sizeof(uint32_t)) return;

  const uint8_t* cur = delta.bytes.data();
  const uint8_t* end = cur + delta.bytes.size();
  uint32_t count = 0;
  std::memcpy(&count, cur, sizeof(count));
  cur += sizeof(count);

  std::vector<uint64_t> destroyed;
  for (uint32_t i = 0; i < count; ++i) {
    if (cur + sizeof(uint32_t) + 1 > end) return; // truncated; stop rather than read garbage
    uint32_t cellIndex = 0;
    std::memcpy(&cellIndex, cur, sizeof(cellIndex));
    cur += sizeof(cellIndex);
    const uint8_t flags = *cur++;

    // Map the file index back to the object just created for it.
    size_t slot = SIZE_MAX;
    for (size_t k = 0; k < live->cellIndices.size(); ++k) {
      if (live->cellIndices[k] == cellIndex) {
        slot = k;
        break;
      }
    }

    if (flags == kDeltaDestroyed) {
      if (slot != SIZE_MAX) destroyed.push_back(live->objectIds[slot]);
      continue;
    }

    if (cur + sizeof(glm::vec3) + sizeof(glm::quat) + sizeof(glm::vec3) > end) return;
    Transform t;
    std::memcpy(&t.translation, cur, sizeof(glm::vec3));
    cur += sizeof(glm::vec3);
    std::memcpy(&t.rotation, cur, sizeof(glm::quat));
    cur += sizeof(glm::quat);
    std::memcpy(&t.scale, cur, sizeof(glm::vec3));
    cur += sizeof(glm::vec3);

    if (slot == SIZE_MAX) continue; // the file changed since the delta was recorded; skip it
    if (RenderObject* ro = findObject(live->objectIds[slot])) {
      ro->transform = t;
      ro->worldMatrix = t.matrix() * live->partLocal[slot];
    }
  }

  if (!destroyed.empty()) {
    std::sort(destroyed.begin(), destroyed.end());
    m_scene.removeObjectsIf([&destroyed](const RenderObject& ro) {
      const uint64_t rid = idFromName(ro.name);
      return rid != 0 && std::binary_search(destroyed.begin(), destroyed.end(), rid);
    });
  }
}

size_t SceneCellProvider::liveObjectCount() const {
  size_t n = 0;
  for (const RenderObject& ro : m_scene.objects) {
    if (idFromName(ro.name) != 0) ++n;
  }
  return n;
}

} // namespace tucano::world
