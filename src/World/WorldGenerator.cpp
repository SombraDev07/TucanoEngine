#include "World/WorldGenerator.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>

namespace tucano::world {
namespace {

/// Deterministic per-cell seed. Hashing the cell key rather than advancing one global generator
/// means a cell's contents depend only on WHERE it is, never on the order cells were baked in — so
/// regenerating a world, or baking only part of it, always yields the same result.
uint32_t cellSeed(const CellId& id, uint32_t layer, uint32_t worldSeed) {
  uint64_t h = id.key() ^ (uint64_t(layer) << 56) ^ (uint64_t(worldSeed) << 24);
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdull;
  h ^= h >> 29;
  h *= 0xc4ceb9fe1a85ec53ull;
  h ^= h >> 32;
  return uint32_t(h);
}

/// Writes a small two-node glTF (a box and a smaller box above it) plus its binary buffer.
///
/// Two nodes on purpose: a single-node model would not catch the mistake of dropping a part's local
/// transform, which is the thing that makes a multi-part asset collapse into itself.
bool writePropGltf(const std::string& dir) {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) return false;

  // A unit box: 24 vertices (per-face normals), 36 indices.
  const float h = 0.5f;
  struct V { float px, py, pz, nx, ny, nz; };
  std::vector<V> verts;
  std::vector<uint16_t> idx;
  auto face = [&](float nx, float ny, float nz, const float c[12]) {
    const uint16_t base = uint16_t(verts.size());
    for (int i = 0; i < 4; ++i) verts.push_back({c[i * 3], c[i * 3 + 1], c[i * 3 + 2], nx, ny, nz});
    const uint16_t q[6] = {base, uint16_t(base + 1), uint16_t(base + 2),
                           base, uint16_t(base + 2), uint16_t(base + 3)};
    idx.insert(idx.end(), q, q + 6);
  };
  const float top[12]    = {-h, h, -h,  h, h, -h,  h, h,  h, -h, h,  h};
  const float bottom[12] = {-h,-h,  h,  h,-h,  h,  h,-h, -h, -h,-h, -h};
  const float front[12]  = {-h,-h,  h,  h,-h,  h,  h, h,  h, -h, h,  h};
  const float back[12]   = { h,-h, -h, -h,-h, -h, -h, h, -h,  h, h, -h};
  const float right[12]  = { h,-h,  h,  h,-h, -h,  h, h, -h,  h, h,  h};
  const float left[12]   = {-h,-h, -h, -h,-h,  h, -h, h,  h, -h, h, -h};
  face(0, 1, 0, top);
  face(0, -1, 0, bottom);
  face(0, 0, 1, front);
  face(0, 0, -1, back);
  face(1, 0, 0, right);
  face(-1, 0, 0, left);

  std::vector<uint8_t> bin;
  auto append = [&bin](const void* d, size_t n) {
    const auto* b = static_cast<const uint8_t*>(d);
    bin.insert(bin.end(), b, b + n);
  };
  const size_t posOff = 0;
  for (const V& v : verts) append(&v.px, sizeof(float) * 3);
  const size_t nrmOff = bin.size();
  for (const V& v : verts) append(&v.nx, sizeof(float) * 3);
  const size_t idxOff = bin.size();
  append(idx.data(), idx.size() * sizeof(uint16_t));

  const std::string binName = "prop.bin";
  if (FILE* f = std::fopen((dir + "/" + binName).c_str(), "wb")) {
    std::fwrite(bin.data(), 1, bin.size(), f);
    std::fclose(f);
  } else {
    return false;
  }

  char json[3072];
  std::snprintf(json, sizeof(json), R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1] } ],
  "nodes": [
    { "name": "PropBase", "mesh": 0, "translation": [0, 0.5, 0], "scale": [1.6, 1.0, 1.6] },
    { "name": "PropTop",  "mesh": 0, "translation": [0, 2.0, 0], "scale": [0.8, 1.4, 0.8] }
  ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1 }, "indices": 2 } ] } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": %zu, "type": "VEC3", "min": [%g,%g,%g], "max": [%g,%g,%g] },
    { "bufferView": 1, "componentType": 5126, "count": %zu, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5123, "count": %zu, "type": "SCALAR" }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": %zu, "byteLength": %zu },
    { "buffer": 0, "byteOffset": %zu, "byteLength": %zu },
    { "buffer": 0, "byteOffset": %zu, "byteLength": %zu }
  ],
  "buffers": [ { "uri": "%s", "byteLength": %zu } ]
})",
                verts.size(), -h, -h, -h, h, h, h, verts.size(), idx.size(),
                posOff, verts.size() * sizeof(float) * 3,
                nrmOff, verts.size() * sizeof(float) * 3,
                idxOff, idx.size() * sizeof(uint16_t),
                binName.c_str(), bin.size());

  if (FILE* f = std::fopen((dir + "/prop.gltf").c_str(), "wb")) {
    std::fwrite(json, 1, std::strlen(json), f);
    std::fclose(f);
    return true;
  }
  return false;
}

CellObject makeObject(std::mt19937& rng, const glm::vec3& cellMin, float cellSize, float heightSpread,
                      WorldLayer layer, uint32_t index, const std::string& gltfPath,
                      bool asGltf) {
  std::uniform_real_distribution<float> unit(0.0f, 1.0f);
  std::uniform_real_distribution<float> signed1(-1.0f, 1.0f);

  CellObject o;
  o.position = cellMin + glm::vec3(unit(rng) * cellSize, signed1(rng) * heightSpread,
                                   unit(rng) * cellSize);

  // Each layer gets a distinct look so a screenshot immediately shows WHICH layers are resident —
  // that is what makes per-layer streaming visible rather than a number in a log.
  switch (layer) {
    case WorldLayer::Gameplay:
      // Tall landmarks: easy to see from far away, and the thing you would collide with.
      o.kind = CellObjectKind::Cube;
      o.size = 1.0f;
      o.scale = glm::vec3(2.0f + unit(rng) * 2.0f, 6.0f + unit(rng) * 10.0f, 2.0f + unit(rng) * 2.0f);
      o.baseColor = glm::vec3(0.65f, 0.22f, 0.18f); // red-ish
      o.roughness = 0.8f;
      // Only landmarks are solid. Giving every layer a collider would fill the physics world with
      // scenery nobody can touch, and it is precisely why Gameplay gets the widest stream radius.
      o.collider = CellColliderKind::Box;
      break;
    case WorldLayer::Visual:
      if (asGltf && !gltfPath.empty()) {
        // A real asset instance, streamed like everything else. Its scale is modest so a prop reads
        // as a prop next to the landmarks rather than competing with them.
        o.kind = CellObjectKind::Gltf;
        o.path = gltfPath;
        o.size = 1.0f;
        o.scale = glm::vec3(1.5f + unit(rng) * 1.5f);
        o.baseColor = glm::vec3(0.75f, 0.65f, 0.4f);
        o.roughness = 0.6f;
        break;
      }
      o.kind = CellObjectKind::Sphere;
      o.size = 1.0f;
      o.scale = glm::vec3(1.5f + unit(rng) * 2.0f);
      o.baseColor = glm::vec3(0.2f, 0.45f, 0.7f); // blue-ish
      o.metallic = 0.15f;
      o.roughness = 0.35f;
      break;
    case WorldLayer::Audio:
      // Audio has no visual of its own in a rendering test, so give it a small marker — its real
      // job here is to exercise a third independent layer through the pipeline.
      o.kind = CellObjectKind::Cube;
      o.size = 1.0f;
      o.scale = glm::vec3(0.6f);
      o.baseColor = glm::vec3(0.85f, 0.75f, 0.2f); // yellow
      break;
    default: // Detail
      o.kind = CellObjectKind::Cube;
      o.size = 1.0f;
      o.scale = glm::vec3(0.4f + unit(rng) * 0.6f);
      o.baseColor = glm::vec3(0.25f, 0.55f, 0.25f); // green
      o.roughness = 0.9f;
      break;
  }

  // A yaw-only rotation: enough to prove the quaternion survives the round trip without making the
  // scene look chaotic.
  const float yaw = unit(rng) * 6.2831853f;
  o.rotation = glm::quat(std::cos(yaw * 0.5f), 0.0f, std::sin(yaw * 0.5f), 0.0f);

  o.name = std::string(cellObjectKindName(o.kind)) + "_" + std::to_string(index);
  return o;
}

} // namespace

bool generateWorld(const WorldGenSettings& settings, const WorldGrid& grid, WorldGenStats& stats) {
  stats = {};

  std::error_code ec;
  std::filesystem::create_directories(settings.outputRoot + "/cells", ec);
  if (ec) return false;

  std::string gltfPath = settings.propGltfPath;
  if (settings.writePropAsset && !gltfPath.empty()) {
    const std::filesystem::path rel(gltfPath);
    const std::string dir = (std::filesystem::path(settings.outputRoot) / rel.parent_path()).string();
    if (!writePropGltf(dir)) gltfPath.clear(); // no asset, no glTF instances — primitives still work
  }

  const uint32_t perLayer[kLayerCount] = {settings.gameplayPerCell, settings.visualPerCell,
                                          settings.detailPerCell / 3, settings.detailPerCell};
  const float cellSize = grid.cellSize(settings.level);

  for (int32_t z = -settings.extentCells; z <= settings.extentCells; ++z) {
    for (int32_t x = -settings.extentCells; x <= settings.extentCells; ++x) {
      // A single-cell-thick slab in Y. The partition is fully 3D, but a flat world is what makes a
      // flythrough readable — vertical worlds are a separate test.
      const CellId id{x, 0, z, settings.level};
      glm::vec3 bmin, bmax;
      grid.boundsOf(id, bmin, bmax);

      bool wroteAny = false;
      for (uint32_t l = 0; l < kLayerCount; ++l) {
        const uint32_t count = perLayer[l];
        if (count == 0) continue;

        std::mt19937 rng(cellSeed(id, l, settings.seed));
        CellFile file;
        file.id = id;
        file.layer = l;
        file.objects.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
          // The first few Visual objects of each cell are glTF instances; the rest stay primitives,
          // so one flythrough exercises both paths side by side.
          const bool asGltf = WorldLayer(l) == WorldLayer::Visual && i < settings.gltfPerCell;
          file.objects.push_back(makeObject(rng, bmin, cellSize, settings.heightSpread,
                                            WorldLayer(l), i, gltfPath, asGltf));
        }

        const std::string path = cellFilePath(settings.outputRoot, id, l);
        if (!file.save(path)) continue;

        wroteAny = true;
        ++stats.filesWritten;
        stats.objectsWritten += uint32_t(file.objects.size());
        stats.bytesWritten += std::filesystem::file_size(path, ec);
      }
      if (wroteAny) ++stats.cellsWritten;
    }
  }
  return true;
}

} // namespace tucano::world
