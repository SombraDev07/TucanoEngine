#pragma once

// Writes a minimal but valid glTF 2.0 file with a skin and an animation, so the importer can be
// tested without shipping a binary asset. Two joints in a chain, the child rotating 90 degrees
// over one second, plus a single skinned triangle.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fixture {

inline void appendFloats(std::vector<uint8_t>& buf, const float* v, size_t n) {
  const size_t offset = buf.size();
  buf.resize(offset + n * sizeof(float));
  std::memcpy(buf.data() + offset, v, n * sizeof(float));
}

inline void appendUShorts(std::vector<uint8_t>& buf, const uint16_t* v, size_t n) {
  const size_t offset = buf.size();
  buf.resize(offset + n * sizeof(uint16_t));
  std::memcpy(buf.data() + offset, v, n * sizeof(uint16_t));
}

/// Writes `dir/skinned.gltf` and `dir/skinned.bin`. Returns the .gltf path.
inline std::string writeSkinnedGltf(const std::string& dir) {
  std::vector<uint8_t> bin;

  // 0: inverse bind matrices — identity for the root, translate(0,-2,0) for the child, so the
  //    child's bind pose sits 2 units up.
  const float ibm[32] = {
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -2, 0, 1,
  };
  const size_t ibmOffset = bin.size();
  appendFloats(bin, ibm, 32);

  // 1: animation input (keyframe times)
  const float times[3] = {0.0f, 0.5f, 1.0f};
  const size_t timeOffset = bin.size();
  appendFloats(bin, times, 3);

  // 2: animation output — rotation about Z: 0, 45, 90 degrees (xyzw).
  const float rots[12] = {
      0.0f, 0.0f, 0.0f, 1.0f,
      0.0f, 0.0f, 0.38268343f, 0.92387953f,
      0.0f, 0.0f, 0.70710678f, 0.70710678f,
  };
  const size_t rotOffset = bin.size();
  appendFloats(bin, rots, 12);

  // 3: mesh positions (one triangle)
  const float pos[9] = {0, 0, 0, 1, 0, 0, 0, 2, 0};
  const size_t posOffset = bin.size();
  appendFloats(bin, pos, 9);

  // 4: joints (ushort4 per vertex) — first two vertices on the root, the tip on the child.
  const uint16_t joints[12] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0};
  const size_t jointOffset = bin.size();
  appendUShorts(bin, joints, 12);
  while (bin.size() % 4 != 0) bin.push_back(0); // keep the next accessor 4-byte aligned

  // 5: weights (float4 per vertex)
  const float weights[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  const size_t weightOffset = bin.size();
  appendFloats(bin, weights, 12);

  // 6: indices
  const uint16_t idx[3] = {0, 1, 2};
  const size_t idxOffset = bin.size();
  appendUShorts(bin, idx, 3);

  const std::string binPath = dir + "/skinned.bin";
  const std::string gltfPath = dir + "/skinned.gltf";

  if (FILE* f = std::fopen(binPath.c_str(), "wb")) {
    std::fwrite(bin.data(), 1, bin.size(), f);
    std::fclose(f);
  }

  char json[4096];
  std::snprintf(json, sizeof(json), R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 3] } ],
  "nodes": [
    { "name": "Root",  "children": [1], "translation": [0, 0, 0] },
    { "name": "Child", "translation": [0, 2, 0] },
    { "name": "Unused" },
    { "name": "MeshNode", "mesh": 0, "skin": 0 }
  ],
  "skins": [ { "joints": [0, 1], "inverseBindMatrices": 0, "skeleton": 0 } ],
  "meshes": [ {
    "primitives": [ {
      "attributes": { "POSITION": 3, "JOINTS_0": 4, "WEIGHTS_0": 5 },
      "indices": 6
    } ]
  } ],
  "animations": [ {
    "name": "Bend",
    "channels": [ { "sampler": 0, "target": { "node": 1, "path": "rotation" } } ],
    "samplers": [ { "input": 1, "output": 2, "interpolation": "LINEAR" } ]
  } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 2, "type": "MAT4" },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "SCALAR", "min": [0.0], "max": [1.0] },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,2,0] },
    { "bufferView": 4, "componentType": 5123, "count": 3, "type": "VEC4" },
    { "bufferView": 5, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 6, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": %zu, "byteLength": 128 },
    { "buffer": 0, "byteOffset": %zu, "byteLength": 12 },
    { "buffer": 0, "byteOffset": %zu, "byteLength": 48 },
    { "buffer": 0, "byteOffset": %zu, "byteLength": 36 },
    { "buffer": 0, "byteOffset": %zu, "byteLength": 24 },
    { "buffer": 0, "byteOffset": %zu, "byteLength": 48 },
    { "buffer": 0, "byteOffset": %zu, "byteLength": 6 }
  ],
  "buffers": [ { "uri": "skinned.bin", "byteLength": %zu } ]
})",
                ibmOffset, timeOffset, rotOffset, posOffset, jointOffset, weightOffset, idxOffset,
                bin.size());

  if (FILE* f = std::fopen(gltfPath.c_str(), "wb")) {
    std::fwrite(json, 1, std::strlen(json), f);
    std::fclose(f);
  }
  return gltfPath;
}

} // namespace fixture
