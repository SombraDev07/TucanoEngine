#pragma once

#include "RHI/RHI.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace tucano::rhi {

inline void vkCheck(VkResult result, const char* what) {
  if (result == VK_ERROR_DEVICE_LOST) {
    // RADV GPUVM / amdgpu MODE1 reset: further submits wedge gnome-shell and look like a reboot.
    std::fprintf(stderr, "[Vulkan] VK_ERROR_DEVICE_LOST at %s — aborting to avoid more GPU submits\n", what);
    std::fflush(stderr);
    std::abort();
  }
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(what) + " failed (VkResult " + std::to_string(static_cast<int>(result)) +
                             ")");
  }
}

inline VkFormat toVk(Format format) {
  switch (format) {
  case Format::R8G8B8A8_UNORM:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case Format::R8G8B8A8_UNORM_SRGB:
    return VK_FORMAT_R8G8B8A8_SRGB;
  case Format::B8G8R8A8_UNORM:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case Format::R16G16B16A16_FLOAT:
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case Format::R11G11B10_FLOAT:
    return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
  case Format::R32_FLOAT:
    return VK_FORMAT_R32_SFLOAT;
  case Format::R16_FLOAT:
    return VK_FORMAT_R16_SFLOAT;
  case Format::R32G32B32A32_FLOAT:
    return VK_FORMAT_R32G32B32A32_SFLOAT;
  case Format::R32G32_FLOAT:
    return VK_FORMAT_R32G32_SFLOAT;
  case Format::R32G32B32_FLOAT:
    return VK_FORMAT_R32G32B32_SFLOAT;
  case Format::R10G10B10A2_UNORM:
    return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
  case Format::R32_UINT:
    return VK_FORMAT_R32_UINT;
  case Format::D32_FLOAT:
    return VK_FORMAT_D32_SFLOAT;
  case Format::D24_UNORM_S8_UINT:
    return VK_FORMAT_D24_UNORM_S8_UINT;
  default:
    return VK_FORMAT_UNDEFINED;
  }
}

inline VkCullModeFlags toVk(CullMode mode) {
  switch (mode) {
  case CullMode::None:
    return VK_CULL_MODE_NONE;
  case CullMode::Front:
    return VK_CULL_MODE_FRONT_BIT;
  case CullMode::Back:
  default:
    return VK_CULL_MODE_BACK_BIT;
  }
}

inline VkCompareOp toVk(CompareOp op) {
  switch (op) {
  case CompareOp::Never:
    return VK_COMPARE_OP_NEVER;
  case CompareOp::Less:
    return VK_COMPARE_OP_LESS;
  case CompareOp::Equal:
    return VK_COMPARE_OP_EQUAL;
  case CompareOp::LessEqual:
    return VK_COMPARE_OP_LESS_OR_EQUAL;
  case CompareOp::Greater:
    return VK_COMPARE_OP_GREATER;
  case CompareOp::NotEqual:
    return VK_COMPARE_OP_NOT_EQUAL;
  case CompareOp::GreaterEqual:
    return VK_COMPARE_OP_GREATER_OR_EQUAL;
  case CompareOp::Always:
    return VK_COMPARE_OP_ALWAYS;
  }
  return VK_COMPARE_OP_LESS;
}

inline VkPrimitiveTopology toVk(PrimitiveTopology topology) {
  switch (topology) {
  case PrimitiveTopology::LineList:
    return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  case PrimitiveTopology::TriangleStrip:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  case PrimitiveTopology::TriangleList:
  default:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  }
}

inline std::string spirvEntryPoint(std::span<const uint8_t> bytes) {
  if (bytes.size() < 20 || (bytes.size() % 4) != 0) {
    return "main";
  }
  const auto* words = reinterpret_cast<const uint32_t*>(bytes.data());
  if (words[0] != 0x07230203u) {
    return "main";
  }
  const uint32_t n = static_cast<uint32_t>(bytes.size() / 4);
  for (uint32_t i = 5; i < n;) {
    const uint32_t inst = words[i];
    const uint32_t op = inst & 0xFFFFu;
    const uint32_t len = inst >> 16;
    if (len == 0 || i + len > n) {
      break;
    }
    if (op == 15 && len >= 4) { // OpEntryPoint
      return reinterpret_cast<const char*>(&words[i + 3]);
    }
    i += len;
  }
  return "main";
}

inline uint32_t spirvVertexLocationMask(std::span<const uint8_t> bytes) {
  uint32_t mask = 0;
  if (bytes.size() < 20 || (bytes.size() % 4) != 0) {
    return 0x7Fu;
  }
  const auto* words = reinterpret_cast<const uint32_t*>(bytes.data());
  if (words[0] != 0x07230203u) {
    return 0x7Fu;
  }
  const uint32_t n = static_cast<uint32_t>(bytes.size() / 4);
  uint32_t inputIds[64]{};
  uint32_t nInput = 0;
  for (uint32_t i = 5; i < n;) {
    const uint32_t inst = words[i];
    const uint32_t op = inst & 0xFFFFu;
    const uint32_t len = inst >> 16;
    if (len == 0 || i + len > n) {
      break;
    }
    if (op == 59 && len >= 4 && words[i + 3] == 1 && nInput < 64) { // OpVariable Input
      inputIds[nInput++] = words[i + 2];
    }
    i += len;
  }
  for (uint32_t i = 5; i < n;) {
    const uint32_t inst = words[i];
    const uint32_t op = inst & 0xFFFFu;
    const uint32_t len = inst >> 16;
    if (len == 0 || i + len > n) {
      break;
    }
    if (op == 71 && len >= 4 && words[i + 2] == 30) { // OpDecorate Location
      const uint32_t id = words[i + 1];
      const uint32_t loc = words[i + 3];
      for (uint32_t k = 0; k < nInput; ++k) {
        if (inputIds[k] == id && loc < 32) {
          mask |= (1u << loc);
          break;
        }
      }
    }
    i += len;
  }
  return mask ? mask : 0x7Fu;
}

inline VkPipelineColorBlendAttachmentState toVkBlend(BlendMode mode) {
  VkPipelineColorBlendAttachmentState b{};
  b.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                     VK_COLOR_COMPONENT_A_BIT;
  if (mode == BlendMode::Opaque) {
    return b;
  }
  b.blendEnable = VK_TRUE;
  b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  b.colorBlendOp = VK_BLEND_OP_ADD;
  b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  b.alphaBlendOp = VK_BLEND_OP_ADD;
  if (mode == BlendMode::Additive) {
    b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  } else if (mode == BlendMode::Min) {
    b.colorBlendOp = VK_BLEND_OP_MIN;
    b.alphaBlendOp = VK_BLEND_OP_MIN;
    b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
  }
  return b;
}

} // namespace tucano::rhi
