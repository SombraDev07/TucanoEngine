#pragma once

// Cell identity for the world partition.
//
// ─── Why a flat morton key and not a pointer octree ───────────────────────────────────────────
//
// The roadmap specifies a Sparse Voxel Octree. This is a sparse 3D partition with the same
// coverage, but stored as a flat hash keyed by an interleaved (morton) code instead of a tree of
// nodes. The reason is that the tree's only real advantage — hierarchical culling, descend and
// reject whole subtrees — is never used by the design that sits on top of it: WM-4 culls a FLAT
// array of every cell in one compute dispatch. Paying for pointer chasing, cache misses and
// subtree locking to build a hierarchy nobody walks is a bad trade.
//
// What a flat morton key buys instead:
//   * O(1) lookup of any cell at any level, with no traversal.
//   * Trivial sharding: the key's low bits pick a lock, so N threads touch N shards without
//     contending. A tree needs either a global lock or hand-over-hand locking.
//   * Serialization is the key itself — no node graph to rebuild on load.
//   * Neighbour queries are integer arithmetic on the decoded coordinate.
//
// The hierarchy is not lost. Levels still exist (a cell at level L has a parent at L-1, obtained
// by shifting the coordinate right by one), which is what HLOD needs. What is dropped is
// materialising that hierarchy as nodes.

#include <cstdint>
#include <functional>

namespace tucano::world {

/// Maximum subdivision level. Level 0 is the whole world; each level halves the cell edge.
/// 15 levels over a 64 km root gives a 2 m finest cell, far below anything streaming needs.
inline constexpr uint32_t kMaxLevel = 15;

/// Coordinates are stored biased so negative world positions work without a sign bit.
/// 20 bits per axis leaves room for level in the top 4 bits of a 64-bit key.
inline constexpr uint32_t kCoordBits = 20;
inline constexpr int32_t kCoordBias = 1 << (kCoordBits - 1); // 524288

/// Spreads the low 20 bits of `v` so that they occupy every third bit.
/// Interleaving the three axes this way is what makes nearby cells land on nearby keys, which in
/// turn keeps hash buckets and disk reads spatially coherent.
constexpr uint64_t spreadBits3(uint64_t v) {
  v &= 0xFFFFFull;
  v = (v | (v << 32)) & 0x001F00000000FFFFull;
  v = (v | (v << 16)) & 0x001F0000FF0000FFull;
  v = (v | (v << 8)) & 0x100F00F00F00F00Full;
  v = (v | (v << 4)) & 0x10C30C30C30C30C3ull;
  v = (v | (v << 2)) & 0x1249249249249249ull;
  return v;
}

/// Inverse of spreadBits3.
constexpr uint64_t compactBits3(uint64_t v) {
  v &= 0x1249249249249249ull;
  v = (v | (v >> 2)) & 0x10C30C30C30C30C3ull;
  v = (v | (v >> 4)) & 0x100F00F00F00F00Full;
  v = (v | (v >> 8)) & 0x001F0000FF0000FFull;
  v = (v | (v >> 16)) & 0x001F00000000FFFFull;
  v = (v | (v >> 32)) & 0xFFFFFull;
  return v;
}

/// A cell's address: a level plus integer coordinates within that level.
/// Comparable, hashable, and round-trips through a single uint64.
struct CellId {
  int32_t x = 0;
  int32_t y = 0;
  int32_t z = 0;
  uint32_t level = 0;

  constexpr bool operator==(const CellId& o) const {
    return x == o.x && y == o.y && z == o.z && level == o.level;
  }
  constexpr bool operator!=(const CellId& o) const { return !(*this == o); }

  /// Packs into a single 64-bit key: 4 bits of level, 60 bits of interleaved coordinate.
  /// Two cells at different levels can share a coordinate, so the level must be part of the key.
  constexpr uint64_t key() const {
    const uint64_t bx = uint64_t(uint32_t(x + kCoordBias)) & 0xFFFFFull;
    const uint64_t by = uint64_t(uint32_t(y + kCoordBias)) & 0xFFFFFull;
    const uint64_t bz = uint64_t(uint32_t(z + kCoordBias)) & 0xFFFFFull;
    const uint64_t morton = spreadBits3(bx) | (spreadBits3(by) << 1) | (spreadBits3(bz) << 2);
    return (uint64_t(level & 0xF) << 60) | (morton & 0x0FFFFFFFFFFFFFFFull);
  }

  static constexpr CellId fromKey(uint64_t k) {
    CellId id;
    id.level = uint32_t(k >> 60) & 0xF;
    const uint64_t morton = k & 0x0FFFFFFFFFFFFFFFull;
    id.x = int32_t(compactBits3(morton)) - kCoordBias;
    id.y = int32_t(compactBits3(morton >> 1)) - kCoordBias;
    id.z = int32_t(compactBits3(morton >> 2)) - kCoordBias;
    return id;
  }

  /// The cell one level coarser that contains this one. Level 0 is its own parent.
  constexpr CellId parent() const {
    if (level == 0) return *this;
    // Arithmetic shift keeps negatives correct: -1 >> 1 is -1, which is the cell containing it.
    return CellId{x >> 1, y >> 1, z >> 1, level - 1};
  }

  /// One of the eight children, indexed by a 3-bit mask (bit0 = +x, bit1 = +y, bit2 = +z).
  constexpr CellId child(uint32_t octant) const {
    return CellId{(x << 1) | int32_t(octant & 1u), (y << 1) | int32_t((octant >> 1) & 1u),
                  (z << 1) | int32_t((octant >> 2) & 1u), level + 1};
  }
};

} // namespace tucano::world

template <>
struct std::hash<tucano::world::CellId> {
  size_t operator()(const tucano::world::CellId& id) const noexcept {
    // The morton key is already well distributed spatially, but its low bits change fastest along
    // x, so mix before handing it to a power-of-two bucket count.
    uint64_t h = id.key();
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    return size_t(h);
  }
};
