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
  auto hm = Heightmap::createFromData(m_device, res, data->worldSize, data->heights);

  // A lean per-tile mesh sampled from the heightmap. Local space [0,worldSize]²; the tile is placed
  // by the object's world matrix, matching TerrainComponent's convention so both can coexist.
  const uint32_t mr = m_settings.meshResolution;
  std::vector<Vertex> verts;
  std::vector<uint32_t> indices;
  verts.reserve(size_t(mr + 1) * (mr + 1));
  for (uint32_t z = 0; z <= mr; ++z) {
    for (uint32_t x = 0; x <= mr; ++x) {
      const float fx = float(x) / float(mr);
      const float fz = float(z) / float(mr);
      const float wx = fx * data->worldSize;
      const float wz = fz * data->worldSize;
      Vertex v{};
      v.position = {wx, hm->sampleHeight(wx, wz), wz};
      v.normal = hm->sampleNormal(wx, wz);
      v.tangent = {1.0f, 0.0f, 0.0f, 1.0f};
      v.uv = {wx * 0.05f, wz * 0.05f};
      v.color = {1.0f, 1.0f, 1.0f, 1.0f};
      verts.push_back(v);
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

  SubMesh sub{};
  sub.indexCount = uint32_t(indices.size());
  sub.materialIndex = 0;
  // A real AABB, or the frustum cull hides the tile everywhere it is not at the origin — the exact
  // bug that made streamed props invisible before.
  sub.aabbMin = {0.0f, hm->minHeight(), 0.0f};
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
  // The heightmap is in tile-local coordinates: shift the query into the tile before sampling.
  const float localX = worldX - tile.worldOrigin.x;
  const float localZ = worldZ - tile.worldOrigin.z;
  outHeight = tile.heightmap->sampleHeight(localX, localZ);
  return true;
}

} // namespace tucano::terrain
