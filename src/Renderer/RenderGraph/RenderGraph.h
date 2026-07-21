#pragma once

#include "RHI/RHI.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tucano {

using RGHandle = uint32_t;
inline constexpr RGHandle kRGInvalid = ~0u;

enum class RGUsage : uint32_t {
  None = 0,
  ShaderRead = 1,
  ShaderWrite = 2,
  RenderTarget = 4,
  DepthWrite = 8,
  UnorderedAccess = 16,
  IndirectArgs = 32,
};

inline RGUsage operator|(RGUsage a, RGUsage b) {
  return static_cast<RGUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool any(RGUsage a, RGUsage b) {
  return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

enum class RGPackerMode : uint32_t {
  GreedyScanline = 0, // production
  Baseline = 1,       // no alias (debug)
  Boxing = 2,         // experimental
};

// Incremental compilation stages (daFrameGraph-inspired subset).
enum class RGCompileStage : uint32_t {
  NameResolution = 0,
  DependencyData,
  Validation,
  IRBuild,
  PassColoring,
  NodeScheduling,
  BarrierScheduling,
  StateDeltas,
  AutoResolution,
  ResourceScheduling,
  HistoryUpdate,
  Count,
};

struct RGTextureDesc {
  uint32_t width = 1;
  uint32_t height = 1;
  rhi::Format format = rhi::Format::R8G8B8A8_UNORM;
  rhi::TextureUsage usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::RenderTarget;
  std::string name;
};

class RenderGraph;

struct RGPassBuilder {
  explicit RGPassBuilder(RenderGraph& graph, uint32_t passIndex) : graph(graph), passIndex(passIndex) {}
  RenderGraph& graph;
  uint32_t passIndex = 0;
  bool enabled = true;
  bool asyncHint = false;

  void read(RGHandle h, RGUsage usage = RGUsage::ShaderRead);
  void write(RGHandle h, RGUsage usage = RGUsage::RenderTarget);
  RGHandle create(const RGTextureDesc& desc);
};

class RenderGraph {
public:
  using SetupFn = std::function<void(RGPassBuilder&)>;
  using ExecuteFn = std::function<void(rhi::CommandList&, RenderGraph&)>;

  void reset();
  void setPackerMode(RGPackerMode mode) { m_packer = mode; }
  RGPackerMode packerMode() const { return m_packer; }

  RGHandle importTexture(const std::string& name, rhi::Texture* texture);
  RGHandle addTransient(const RGTextureDesc& desc);
  void addPass(const std::string& name, SetupFn setup, ExecuteFn execute);

  void compile(rhi::Device& device);
  void execute(rhi::CommandList& cmd);
  void exportGraphviz(const std::string& path) const;

  rhi::Texture* getTexture(RGHandle h) const;
  uint32_t passCount() const { return static_cast<uint32_t>(m_passes.size()); }
  uint64_t aliasedBytes() const { return m_aliasedBytes; }
  uint32_t barrierInsertions() const { return m_barrierInsertions; }
  bool stageDirty(RGCompileStage s) const {
    return m_stageDirty[static_cast<uint32_t>(s)];
  }

private:
  friend struct RGPassBuilder;

  struct Resource {
    std::string name;
    RGTextureDesc desc{};
    rhi::Texture* imported = nullptr;
    std::shared_ptr<rhi::Texture> owned;
    int firstPass = -1;
    int lastPass = -1;
    int aliasOf = -1;
    uint64_t byteSize = 0;
  };

  struct Access {
    RGHandle handle = kRGInvalid;
    RGUsage usage = RGUsage::None;
  };

  struct Pass {
    std::string name;
    SetupFn setup;
    ExecuteFn execute;
    bool enabled = true;
    bool asyncHint = false;
    std::vector<Access> reads;
    std::vector<Access> writes;
    int color = 0; // async coloring
  };

  rhi::ResourceState stateForUsage(RGUsage usage) const;
  void applyTransitions(rhi::CommandList& cmd, const Pass& pass);
  void runStage(RGCompileStage stage, rhi::Device& device);
  void packGreedyScanline();
  void packBaseline();
  void packBoxing();
  void markAllStagesDirty();

  std::vector<Resource> m_resources;
  std::vector<Pass> m_passes;
  std::unordered_map<std::string, RGHandle> m_imports;
  uint64_t m_aliasedBytes = 0;
  uint32_t m_barrierInsertions = 0;
  bool m_compiled = false;
  RGPackerMode m_packer = RGPackerMode::GreedyScanline;
  bool m_stageDirty[static_cast<uint32_t>(RGCompileStage::Count)]{};
};

} // namespace tucano
