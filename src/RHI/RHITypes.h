#pragma once

#include <cstdint>
#include <string>

namespace tucano::rhi {

inline constexpr uint32_t kMaxFramesInFlight = 4;
inline constexpr uint32_t kFrameLatency = 2;
// Flip model: frames-in-flight + 1 back buffer (Dagor-style).
inline constexpr uint32_t kBackBufferCount = kMaxFramesInFlight + 1;
inline constexpr uint32_t kDynamicBufferDiscardCount = kMaxFramesInFlight;

enum class Format : uint32_t {
  Unknown = 0,
  R8G8B8A8_UNORM,
  R8G8B8A8_UNORM_SRGB,
  B8G8R8A8_UNORM,
  R16G16B16A16_FLOAT,
  R11G11B10_FLOAT,
  R32_FLOAT,
  R16_FLOAT,
  R32G32B32A32_FLOAT,
  R32G32_FLOAT,
  R32G32B32_FLOAT,
  R10G10B10A2_UNORM,
  R32_UINT,
  D32_FLOAT,
  D24_UNORM_S8_UINT,
};

enum class ResourceState : uint32_t {
  Common,
  Present,
  RenderTarget,
  DepthWrite,
  DepthRead,
  ShaderResource,
  UnorderedAccess,
  CopySrc,
  CopyDst,
  VertexBuffer,
  IndexBuffer,
  ConstantBuffer,
  IndirectArgument,
  AccelerationStructure,
};

enum class BufferUsage : uint32_t {
  Vertex = 1u << 0,
  Index = 1u << 1,
  Constant = 1u << 2,
  Structured = 1u << 3,
  Upload = 1u << 4,
  Readback = 1u << 5,
  Indirect = 1u << 6,
  Dynamic = 1u << 7, // multi-backing discard ring
  UnorderedAccess = 1u << 8,
  AccelerationStructure = 1u << 9,
};

inline BufferUsage operator|(BufferUsage a, BufferUsage b) {
  return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool any(BufferUsage a, BufferUsage b) {
  return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

enum class TextureUsage : uint32_t {
  ShaderResource = 1u << 0,
  RenderTarget = 1u << 1,
  DepthStencil = 1u << 2,
  UnorderedAccess = 1u << 3,
};

inline TextureUsage operator|(TextureUsage a, TextureUsage b) {
  return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool any(TextureUsage a, TextureUsage b) {
  return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

enum class ShaderStage : uint32_t {
  Vertex,
  Pixel,
  Compute,
};

enum class CullMode : uint32_t { None, Back, Front };
enum class CompareOp : uint32_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
enum class BlendMode : uint32_t { Opaque, AlphaBlend, Additive };
enum class Filter : uint32_t { Point, Linear, Anisotropic };
enum class AddressMode : uint32_t { Wrap, Clamp, Mirror, Border };
enum class PrimitiveTopology : uint32_t { TriangleList, TriangleStrip, LineList };

struct BufferDesc {
  uint64_t size = 0;
  BufferUsage usage = BufferUsage::Vertex;
  uint32_t stride = 0; // structured / typed UAV element stride (0 = raw bytes)
  std::string debugName;
};

struct TextureDesc {
  uint32_t width = 1;
  uint32_t height = 1;
  uint32_t depth = 1;
  uint32_t mipLevels = 1;
  uint32_t arraySize = 1;
  Format format = Format::R8G8B8A8_UNORM;
  TextureUsage usage = TextureUsage::ShaderResource;
  bool isCube = false;
  std::string debugName;
};

struct SamplerDesc {
  Filter filter = Filter::Anisotropic;
  AddressMode addressU = AddressMode::Wrap;
  AddressMode addressV = AddressMode::Wrap;
  AddressMode addressW = AddressMode::Wrap;
  float maxAnisotropy = 8.0f;
  CompareOp compare = CompareOp::Never;
  bool comparison = false;
};

struct Viewport {
  float x = 0, y = 0, width = 0, height = 0, minDepth = 0, maxDepth = 1;
};

struct Scissor {
  int32_t left = 0, top = 0, right = 0, bottom = 0;
};

struct ClearValue {
  float color[4]{0, 0, 0, 1};
  float depth = 1.0f;
  uint8_t stencil = 0;
};

} // namespace tucano::rhi
