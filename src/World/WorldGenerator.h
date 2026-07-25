#pragma once

// Procedural world baker.
//
// Writes a world out as one `.tcell` file per (cell, layer) so the streaming pipeline has something
// real to read off disk. Procedural on purpose for the first real test: it generates in seconds,
// needs no art, and — the important part — it isolates streaming bugs from asset bugs. When
// something fails to appear, there is no question of whether the mesh import was at fault.
//
// The content is deliberately layered the way a real world is: Gameplay gets the large landmarks
// you can collide with, Detail gets small scattered props. That makes the per-layer radius from
// WM-7 visible — fly out and the small stuff thins out before the landmarks do.

#include "World/CellFile.h"
#include "World/WorldGrid.h"

#include <cstdint>
#include <string>

namespace tucano::world {

struct WorldGenSettings {
  std::string outputRoot = "world";
  uint32_t level = 10;      ///< cell level to bake at
  int32_t extentCells = 16; ///< half-width in cells, so the world is (2N+1)^2 cells on the XZ plane
  uint32_t seed = 1337;

  /// Objects per cell, per layer. Gameplay is sparse (landmarks), Detail is dense (props).
  uint32_t gameplayPerCell = 3;
  uint32_t visualPerCell = 5;
  uint32_t detailPerCell = 12;

  /// Vertical band objects are scattered through, around y = 0.
  float heightSpread = 1.5f;

  /// glTF prop scattered through the Visual layer, as a path relative to the world root.
  ///
  /// When `writePropAsset` is set the baker writes a small model there itself, so the streaming
  /// path can be exercised end to end without depending on any art. Point it at a real asset (and
  /// clear the flag) to stream actual content instead.
  std::string propGltfPath = "props/prop.gltf";
  bool writePropAsset = true;
  /// How many of each cell's Visual objects are glTF instances rather than primitives.
  uint32_t gltfPerCell = 1;
};

struct WorldGenStats {
  uint32_t cellsWritten = 0;
  uint32_t filesWritten = 0;
  uint32_t objectsWritten = 0;
  uint64_t bytesWritten = 0;
};

/// Generates the world and writes it under `settings.outputRoot`. Creates the directory tree if
/// needed. Returns false only if the output directory could not be created.
bool generateWorld(const WorldGenSettings& settings, const WorldGrid& grid, WorldGenStats& stats);

} // namespace tucano::world
