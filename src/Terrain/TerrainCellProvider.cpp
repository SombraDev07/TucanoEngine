#include "Terrain/TerrainCellProvider.h"

#include "Physics/PhysicsWorld.h"
#include "Renderer/DevTexture.h"
#include "Renderer/Material.h"
#include "Renderer/Mesh.h"
#include "Terrain/TerrainComponent.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

namespace tucano::terrain {
namespace {

// A stable object-name stamp, so release() finds exactly the tile it created even after unrelated
// tiles shifted the scene array — the same identity trick SceneCellProvider uses.
std::string tileName(uint64_t id) { return "terrain#" + std::to_string(id); }

uint64_t nameStamp(const std::string& name) {
  if (name.rfind("terrain#", 0) != 0) return 0;
  return std::strtoull(name.c_str() + 8, nullptr, 10);
}

// Classic Perlin helpers, matching TerrainGenerator's math so the streamed terrain looks like the
// editor's — but sampled at ABSOLUTE world coordinates, which is what makes neighbouring tiles seam.
float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
float lerpf(float a, float b, float t) { return a + t * (b - a); }
float grad(int hash, float x, float y) {
  int h = hash & 3;
  float u = h < 2 ? x : y;
  float v = h < 2 ? y : x;
  return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}
float noise2D(float x, float y, const std::vector<int>& perm) {
  int xi = int(std::floor(x)) & 255;
  int yi = int(std::floor(y)) & 255;
  float xf = x - std::floor(x);
  float yf = y - std::floor(y);
  float u = fade(xf);
  float v = fade(yf);
  int aa = perm[perm[xi] + yi];
  int ab = perm[perm[xi] + yi + 1];
  int ba = perm[perm[xi + 1] + yi];
  int bb = perm[perm[xi + 1] + yi + 1];
  return lerpf(lerpf(grad(aa, xf, yf), grad(ba, xf - 1.0f, yf), u),
               lerpf(grad(ab, xf, yf - 1.0f), grad(bb, xf - 1.0f, yf - 1.0f), u), v);
}

} // namespace

TerrainCellProvider::TerrainCellProvider(rhi::Device& device, Scene& scene,
                                         const world::WorldGrid& grid,
                                         const TerrainStreamSettings& settings)
    : m_device(device), m_scene(scene), m_grid(grid), m_settings(settings) {
  // The permutation table for the fbm, built once and shared by every worker thread. It is written
  // here and only read afterwards, so no synchronisation is needed across the streaming workers.
  m_perm.resize(512);
  for (int i = 0; i < 256; ++i) m_perm[i] = i;
  std::mt19937 rng(m_settings.seed);
  std::shuffle(m_perm.begin(), m_perm.begin() + 256, rng);
  for (int i = 0; i < 256; ++i) m_perm[i + 256] = m_perm[i];

  // One shared terrain material for every tile. A per-tile material would exhaust the descriptor
  // heap exactly the way per-object materials did before SceneCellProvider started sharing them.
  m_material = std::make_shared<Material>();
  m_material->name = "StreamedTerrain";
  m_material->baseColorFactor = {0.34f, 0.46f, 0.24f, 1.0f};
  m_material->roughnessFactor = 0.9f;
  m_material->metallicFactor = 0.0f;
  // Bind the dev textures, or the deferred path samples null and the terrain renders pure black —
  // the same trap SceneCellProvider hit. Emissive as well, so tiles are visible regardless of the
  // sun angle in a headless streaming gate.
  m_material->albedo = devtex::defaultAlbedo(m_device);
  m_material->normal = devtex::defaultNormal(m_device);
  m_material->emissiveFactor = glm::vec3(0.10f, 0.16f, 0.08f);
}

TerrainCellProvider::~TerrainCellProvider() = default;

float TerrainCellProvider::fbm(float worldX, float worldZ) const {
  float value = 0.0f;
  float freq = m_settings.baseFrequency;
  float amp = 1.0f;
  float maxValue = 0.0f;
  for (uint32_t i = 0; i < m_settings.octaves; ++i) {
    value += noise2D(worldX * freq, worldZ * freq, m_perm) * amp;
    maxValue += amp;
    freq *= m_settings.lacunarity;
    amp *= m_settings.persistence;
  }
  return (maxValue > 0.0f ? value / maxValue : 0.0f) * m_settings.heightScale;
}

float TerrainCellProvider::referenceHeight(float worldX, float worldZ) const {
  return fbm(worldX, worldZ);
}

bool TerrainCellProvider::readBytes(const world::CellId& id, world::WorldLayer layer,
                                    std::vector<uint8_t>& out) {
  if (layer != m_settings.layer) return false; // terrain streams on exactly one layer

  // Terrain is a 2D field draped in 3D cells. Emit a tile only for the cell band that straddles the
  // ground plane, so a column of stacked cells produces ONE tile, not one per altitude.
  glm::vec3 mn, mx;
  m_grid.boundsOf(id, mn, mx);
  if (!(mn.y <= 0.0f && 0.0f < mx.y)) return false;

  // No disk for procedural terrain: the "bytes" are the tile's footprint, which deserialize turns
  // into a height field. A disk-backed build would read a .r16 tile here instead — same pipeline.
  out.resize(sizeof(float) * 3);
  const float params[3] = {mn.x, mn.z, mx.x - mn.x};
  std::memcpy(out.data(), params, sizeof(params));
  return true;
}

bool TerrainCellProvider::deserialize(const world::CellId& /*id*/, world::WorldLayer /*layer*/,
                                      const std::vector<uint8_t>& bytes, world::CellContent& out) {
  if (bytes.size() < sizeof(float) * 3) return false;
  float params[3];
  std::memcpy(params, bytes.data(), sizeof(params));
  const float originX = params[0];
  const float originZ = params[1];
  const float worldSize = params[2];

  const uint32_t res = m_settings.heightmapResolution;
  auto* data = new TileData();
  data->worldOrigin = glm::vec3(originX, 0.0f, originZ);
  data->worldSize = worldSize;
  data->heights.resize(size_t(res) * res);

  // World-continuous sampling: texel (tx,tz) maps to an absolute world coordinate, so the last row
  // of one tile is the first row of the next — identical coordinate, identical height, no seam.
  for (uint32_t tz = 0; tz < res; ++tz) {
    for (uint32_t tx = 0; tx < res; ++tx) {
      const float wx = originX + float(tx) / float(res - 1) * worldSize;
      const float wz = originZ + float(tz) / float(res - 1) * worldSize;
      data->heights[size_t(tz) * res + tx] = fbm(wx, wz);
    }
  }

  out.userData = data;
  out.cpuBytes = data->heights.size() * sizeof(float);
  return true;
}

bool TerrainCellProvider::upload(const world::CellId& id, world::WorldLayer /*layer*/,
                                 world::CellContent& content) {
  auto* data = static_cast<TileData*>(content.userData);
  if (!data) return false;

  const uint32_t res = m_settings.heightmapResolution;

  // Composite mode: the tile feeds the clipmap's window instead of becoming its own mesh. No GPU
  // heightmap, no scene object — just blit the heights and keep them for a window rebuild on recentre.
  if (m_compositeMode) {
    blitTileToComposite(data->worldOrigin, data->worldSize, data->heights, res);
    LiveTile tile;
    tile.worldOrigin = data->worldOrigin;
    tile.worldSize = data->worldSize;
    tile.physicsBody = 0xFFFFFFFFu;
    tile.lod = content.lod;
    tile.heightRes = res;
    tile.cpuHeights = std::move(data->heights);
    ++m_tilesBuilt;
    m_live[id.key()] = std::move(tile);
    m_compositeDirty = true;
    content.gpuBytes = 0;
    delete data;
    content.userData = nullptr;
    return true;
  }

  auto hm = Heightmap::createFromData(m_device, res, data->worldSize, data->heights);

  // A lean per-tile mesh sampled from the heightmap. Local space [0,worldSize]²; the tile is placed
  // by the object's world matrix, matching TerrainComponent's convention so both can coexist.
  //
  // Per-tile LOD: the scheduler set content.lod from this tile's distance to the observer (via the
  // desc's lodDistances, the same WM-5 band mechanism that reloads a cell when its band changes). A
  // coarser level halves the mesh grid, so a distant tile costs a quarter of the triangles a near
  // one does — the point of the whole exercise.
  const uint32_t mr = std::max(4u, m_settings.meshResolution >> content.lod);
  std::vector<Vertex> verts;
  std::vector<uint32_t> indices;
  verts.reserve(size_t(mr + 3) * (mr + 3));

  auto pushVertex = [&](float wx, float wz, float yOverride, bool useOverride) {
    Vertex v{};
    const float h = hm->sampleHeight(wx, wz);
    v.position = {wx, useOverride ? yOverride : h, wz};
    v.normal = hm->sampleNormal(wx, wz);
    v.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
    v.uv = {wx * 0.05f, wz * 0.05f};
    v.color = {1.0f, 1.0f, 1.0f, 1.0f};
    verts.push_back(v);
  };

  for (uint32_t z = 0; z <= mr; ++z) {
    for (uint32_t x = 0; x <= mr; ++x) {
      pushVertex(float(x) / float(mr) * data->worldSize, float(z) / float(mr) * data->worldSize, 0.0f,
                 false);
    }
  }
  for (uint32_t z = 0; z < mr; ++z) {
    for (uint32_t x = 0; x < mr; ++x) {
      const uint32_t i0 = z * (mr + 1) + x;
      const uint32_t i1 = i0 + 1;
      const uint32_t i2 = i0 + (mr + 1);
      const uint32_t i3 = i2 + 1;
      indices.push_back(i0);
      indices.push_back(i2);
      indices.push_back(i1);
      indices.push_back(i1);
      indices.push_back(i2);
      indices.push_back(i3);
    }
  }

  // Skirts: a vertical curtain around the tile border, dropped `skirtDepth` below the edge. Where a
  // near (dense) tile meets a far (coarse) one their edge vertices do not line up — a T-junction that
  // would show as a thin crack to the sky. The skirt fills that crack with geometry behind it,
  // invisible from above, so no VT/stitching is needed for a clean result.
  if (m_settings.skirtDepth > 0.0f) {
    const float ws = data->worldSize;
    const float sd = m_settings.skirtDepth;
    // Each edge: walk its mr segments, emitting a quad (top edge → dropped skirt) per segment.
    auto edgeSkirt = [&](auto worldAt) {
      for (uint32_t i = 0; i < mr; ++i) {
        const auto [ax, az] = worldAt(i);
        const auto [bx, bz] = worldAt(i + 1);
        const float ah = hm->sampleHeight(ax, az);
        const float bh = hm->sampleHeight(bx, bz);
        const uint32_t base = uint32_t(verts.size());
        pushVertex(ax, az, ah, true);        // top a
        pushVertex(bx, bz, bh, true);        // top b
        pushVertex(ax, az, ah - sd, true);   // bottom a
        pushVertex(bx, bz, bh - sd, true);   // bottom b
        // Two triangles, wound so the curtain faces outward on all four sides (cull is None anyway).
        indices.insert(indices.end(), {base, base + 2, base + 1, base + 1, base + 2, base + 3});
      }
    };
    edgeSkirt([&](uint32_t i) { return std::pair<float, float>{float(i) / mr * ws, 0.0f}; });     // z=0
    edgeSkirt([&](uint32_t i) { return std::pair<float, float>{float(i) / mr * ws, ws}; });        // z=max
    edgeSkirt([&](uint32_t i) { return std::pair<float, float>{0.0f, float(i) / mr * ws}; });      // x=0
    edgeSkirt([&](uint32_t i) { return std::pair<float, float>{ws, float(i) / mr * ws}; });        // x=max
  }

  SubMesh sub{};
  sub.indexCount = uint32_t(indices.size());
  sub.materialIndex = 0;
  // A real AABB, or the frustum cull hides the tile everywhere it is not at the origin — the exact
  // bug that made streamed props invisible before.
  sub.aabbMin = {0.0f, hm->minHeight() - m_settings.skirtDepth, 0.0f};
  sub.aabbMax = {data->worldSize, hm->maxHeight(), data->worldSize};
  auto mesh = Mesh::create(m_device, verts, indices, {sub});

  const uint64_t objId = m_nextObjectId++;
  RenderObject ro;
  ro.name = tileName(objId);
  ro.mesh = mesh;
  ro.materials = {m_material};
  ro.transform.translation = data->worldOrigin;
  ro.worldMatrix = glm::translate(glm::mat4(1.0f), data->worldOrigin);
  m_scene.objects.push_back(std::move(ro));

  LiveTile tile;
  tile.objectId = objId;
  tile.heightmap = hm;
  tile.mesh = mesh;
  tile.worldOrigin = data->worldOrigin;
  tile.worldSize = data->worldSize;
  tile.physicsBody = 0xFFFFFFFFu;
  tile.lod = content.lod;
  tile.vertexCount = uint32_t(verts.size());

  // Optional collision, reusing the terrain module's Jolt heightfield builder. The TerrainComponent
  // is transient — we only need it to create the body; it survives in the physics world after the
  // component is gone, and release() removes it by id.
  if (m_settings.buildPhysics && m_physics) {
    TerrainComponent comp(m_device, hm, m_material);
    comp.setWorldPosition(data->worldOrigin);
    comp.createPhysicsBody(*m_physics);
    const JPH::BodyID bid = comp.physicsBodyId();
    if (!bid.IsInvalid()) tile.physicsBody = bid.GetIndexAndSequenceNumber();
  }

  m_residentVerts += verts.size();
  ++m_tilesBuilt;
  m_live[id.key()] = std::move(tile);

  content.gpuBytes = uint64_t(verts.size()) * sizeof(Vertex);
  delete data;
  content.userData = nullptr;
  return true;
}

void TerrainCellProvider::release(const world::CellId& id, world::WorldLayer /*layer*/,
                                  world::CellContent& /*content*/) {
  auto it = m_live.find(id.key());
  if (it == m_live.end()) return;
  LiveTile& tile = it->second;

  // Composite mode owns no scene object or physics body — the tile only fed the window. Dropping it
  // leaves its heights in the window until the next recentre rebuilds it, which is fine: unloaded far
  // terrain lingering a moment is invisible next to the camera.
  if (m_compositeMode) {
    m_live.erase(it);
    return;
  }

  if (tile.physicsBody != 0xFFFFFFFFu && m_physics) {
    m_physics->removeBody(JPH::BodyID(tile.physicsBody));
  }

  const uint64_t target = tile.objectId;
  m_scene.objects.erase(std::remove_if(m_scene.objects.begin(), m_scene.objects.end(),
                                       [target](const RenderObject& ro) {
                                         return nameStamp(ro.name) == target;
                                       }),
                        m_scene.objects.end());

  m_residentVerts -= std::min<uint64_t>(m_residentVerts, tile.mesh ? tile.mesh->vertexCount() : 0);

  // A per-tile mesh has exactly one owner, so dropping it here would free its GPU buffers while the
  // GPU might still be drawing last frame. Retire it, like WM-5's merged meshes.
  if (tile.mesh) {
    m_retired.push_back(std::move(tile.mesh));
    while (m_retired.size() > kRetireDepth) m_retired.erase(m_retired.begin());
  }
  m_live.erase(it);
}

world::CellId TerrainCellProvider::cellCovering(float worldX, float worldZ) const {
  // The ground-band cell whose footprint contains (worldX, worldZ). Level is the stream level; the
  // band index is the one straddling y=0, found by asking the grid which cell holds that point.
  return m_grid.cellAt(glm::vec3(worldX, 0.0f, worldZ), m_grid.desc().streamLevel);
}

bool TerrainCellProvider::sampleHeight(float worldX, float worldZ, float& outHeight) const {
  const auto it = m_live.find(cellCovering(worldX, worldZ).key());
  if (it == m_live.end()) return false;
  const LiveTile& tile = it->second;
  if (!tile.heightmap) return false; // composite mode keeps no per-tile GPU heightmap
  // The heightmap is in tile-local coordinates: shift the query into the tile before sampling.
  const float localX = worldX - tile.worldOrigin.x;
  const float localZ = worldZ - tile.worldOrigin.z;
  outHeight = tile.heightmap->sampleHeight(localX, localZ);
  return true;
}

std::array<uint32_t, 8> TerrainCellProvider::tilesPerLod() const {
  std::array<uint32_t, 8> hist{};
  for (const auto& [key, tile] : m_live) ++hist[std::min<uint32_t>(tile.lod, 7)];
  return hist;
}

bool TerrainCellProvider::tileLodAt(float worldX, float worldZ, uint32_t& outLod,
                                    uint32_t& outVerts) const {
  const auto it = m_live.find(cellCovering(worldX, worldZ).key());
  if (it == m_live.end()) return false;
  outLod = it->second.lod;
  outVerts = it->second.vertexCount;
  return true;
}

std::shared_ptr<Heightmap> TerrainCellProvider::tileHeightmapAt(float worldX, float worldZ,
                                                                glm::vec3& outOrigin) const {
  const auto it = m_live.find(cellCovering(worldX, worldZ).key());
  if (it == m_live.end()) return nullptr;
  outOrigin = it->second.worldOrigin;
  return it->second.heightmap;
}

// ── Composite window (Phase 2) ──

void TerrainCellProvider::enableComposite(rhi::Device& device, float worldSize, uint32_t resolution) {
  m_compositeMode = true;
  m_compositeWorldSize = worldSize;
  m_compositeRes = resolution;
  m_compositeTexel = worldSize / float(resolution);
  m_compositeData.assign(size_t(resolution) * resolution, 0.0f);
  m_compositeMin = glm::vec2(-worldSize * 0.5f); // recentred on the observer by the first update

  rhi::TextureDesc td{};
  td.width = resolution;
  td.height = resolution;
  td.format = rhi::Format::R32_FLOAT;
  td.usage = rhi::TextureUsage::ShaderResource;
  td.debugName = "TerrainCompositeWindow";
  // Created once so its bindless index is stable — the clipmap caches it. Re-uploads go into this
  // same texture (uploadTexture), never a fresh one, or the index would change every frame.
  m_compositeTex = device.createTexture(td, m_compositeData.data(),
                                        resolution * uint32_t(sizeof(float)));
  m_compositeDirty = false;
}

uint32_t TerrainCellProvider::compositeBindlessIndex() const {
  return m_compositeTex ? m_compositeTex->bindlessIndex() : 0u;
}

void TerrainCellProvider::blitTileToComposite(const glm::vec3& origin, float tileSize,
                                              const std::vector<float>& heights, uint32_t heightRes) {
  if (m_compositeData.empty() || heights.empty()) return;
  const int R = int(m_compositeRes);

  // Composite texels overlapping the tile's world footprint.
  const int cx0 = std::max(0, int(std::floor((origin.x - m_compositeMin.x) / m_compositeTexel)));
  const int cz0 = std::max(0, int(std::floor((origin.z - m_compositeMin.y) / m_compositeTexel)));
  const int cx1 = std::min(R - 1, int(std::ceil((origin.x + tileSize - m_compositeMin.x) / m_compositeTexel)));
  const int cz1 = std::min(R - 1, int(std::ceil((origin.z + tileSize - m_compositeMin.y) / m_compositeTexel)));

  const float hs = float(heightRes - 1);
  for (int cz = cz0; cz <= cz1; ++cz) {
    for (int cx = cx0; cx <= cx1; ++cx) {
      const float wx = m_compositeMin.x + float(cx) * m_compositeTexel;
      const float wz = m_compositeMin.y + float(cz) * m_compositeTexel;
      const float lx = wx - origin.x;
      const float lz = wz - origin.z;
      if (lx < 0.0f || lz < 0.0f || lx > tileSize || lz > tileSize) continue;

      // Bilinear sample of the tile's heights at the local position.
      const float fx = lx / tileSize * hs;
      const float fz = lz / tileSize * hs;
      const int x0 = std::min(int(fx), int(heightRes) - 1);
      const int z0 = std::min(int(fz), int(heightRes) - 1);
      const int x1 = std::min(x0 + 1, int(heightRes) - 1);
      const int z1 = std::min(z0 + 1, int(heightRes) - 1);
      const float tx = fx - float(x0);
      const float tz = fz - float(z0);
      const float h00 = heights[size_t(z0) * heightRes + x0];
      const float h10 = heights[size_t(z0) * heightRes + x1];
      const float h01 = heights[size_t(z1) * heightRes + x0];
      const float h11 = heights[size_t(z1) * heightRes + x1];
      const float h = glm::mix(glm::mix(h00, h10, tx), glm::mix(h01, h11, tx), tz);
      m_compositeData[size_t(cz) * R + cx] = h;
    }
  }
}

void TerrainCellProvider::updateComposite(rhi::Device& device, const glm::vec2& observerXZ) {
  if (!m_compositeMode) return;

  // Recentre when the observer drifts past a quarter of the window from its centre. Recentre snaps to
  // the texel grid so the clipmap's world→uv mapping stays stable, then rebuilds the window from every
  // resident tile's kept heights.
  const glm::vec2 center = m_compositeMin + glm::vec2(m_compositeWorldSize * 0.5f);
  if (glm::length(observerXZ - center) > m_compositeWorldSize * 0.25f) {
    glm::vec2 newMin = observerXZ - glm::vec2(m_compositeWorldSize * 0.5f);
    newMin.x = std::floor(newMin.x / m_compositeTexel) * m_compositeTexel;
    newMin.y = std::floor(newMin.y / m_compositeTexel) * m_compositeTexel;
    m_compositeMin = newMin;
    std::fill(m_compositeData.begin(), m_compositeData.end(), 0.0f);
    for (const auto& [key, tile] : m_live) {
      if (!tile.cpuHeights.empty()) {
        blitTileToComposite(tile.worldOrigin, tile.worldSize, tile.cpuHeights, tile.heightRes);
      }
    }
    m_compositeDirty = true;
  }

  if (m_compositeDirty && m_compositeTex) {
    device.uploadTexture(*m_compositeTex, m_compositeData.data(), m_compositeRes, m_compositeRes,
                         m_compositeRes * uint32_t(sizeof(float)));
    m_compositeDirty = false;
  }
}

} // namespace tucano::terrain
