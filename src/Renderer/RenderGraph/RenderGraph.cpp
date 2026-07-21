#include "Renderer/RenderGraph/RenderGraph.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace tucano {

void RGPassBuilder::read(RGHandle h, RGUsage usage) {
  graph.m_passes[passIndex].reads.push_back({h, usage});
}

void RGPassBuilder::write(RGHandle h, RGUsage usage) {
  graph.m_passes[passIndex].writes.push_back({h, usage});
}

RGHandle RGPassBuilder::create(const RGTextureDesc& desc) { return graph.addTransient(desc); }

void RenderGraph::markAllStagesDirty() {
  for (auto& d : m_stageDirty) {
    d = true;
  }
}

void RenderGraph::reset() {
  m_resources.clear();
  m_passes.clear();
  m_imports.clear();
  m_aliasedBytes = 0;
  m_barrierInsertions = 0;
  m_compiled = false;
  markAllStagesDirty();
}

RGHandle RenderGraph::importTexture(const std::string& name, rhi::Texture* texture) {
  if (auto it = m_imports.find(name); it != m_imports.end()) {
    return it->second;
  }
  Resource r;
  r.name = name;
  r.imported = texture;
  if (texture) {
    r.desc.width = texture->width();
    r.desc.height = texture->height();
    r.desc.format = texture->format();
  }
  const RGHandle h = static_cast<RGHandle>(m_resources.size());
  m_resources.push_back(std::move(r));
  m_imports[name] = h;
  markAllStagesDirty();
  return h;
}

RGHandle RenderGraph::addTransient(const RGTextureDesc& desc) {
  Resource r;
  r.name = desc.name.empty() ? ("transient_" + std::to_string(m_resources.size())) : desc.name;
  r.desc = desc;
  r.byteSize = uint64_t(desc.width) * desc.height * 8ull;
  const RGHandle h = static_cast<RGHandle>(m_resources.size());
  m_resources.push_back(std::move(r));
  markAllStagesDirty();
  return h;
}

void RenderGraph::addPass(const std::string& name, SetupFn setup, ExecuteFn execute) {
  Pass p;
  p.name = name;
  p.setup = std::move(setup);
  p.execute = std::move(execute);
  m_passes.push_back(std::move(p));
  const uint32_t idx = static_cast<uint32_t>(m_passes.size() - 1);
  RGPassBuilder builder(*this, idx);
  if (m_passes[idx].setup) {
    m_passes[idx].setup(builder);
  }
  m_passes[idx].enabled = builder.enabled;
  m_passes[idx].asyncHint = builder.asyncHint;
  markAllStagesDirty();
}

rhi::Texture* RenderGraph::getTexture(RGHandle h) const {
  if (h >= m_resources.size()) {
    return nullptr;
  }
  const Resource& r = m_resources[h];
  if (r.aliasOf >= 0) {
    return getTexture(static_cast<RGHandle>(r.aliasOf));
  }
  if (r.imported) {
    return r.imported;
  }
  return r.owned.get();
}

rhi::ResourceState RenderGraph::stateForUsage(RGUsage usage) const {
  if (any(usage, RGUsage::UnorderedAccess)) {
    return rhi::ResourceState::UnorderedAccess;
  }
  if (any(usage, RGUsage::RenderTarget)) {
    return rhi::ResourceState::RenderTarget;
  }
  if (any(usage, RGUsage::DepthWrite)) {
    return rhi::ResourceState::DepthWrite;
  }
  if (any(usage, RGUsage::IndirectArgs)) {
    return rhi::ResourceState::IndirectArgument;
  }
  return rhi::ResourceState::ShaderResource;
}

void RenderGraph::packBaseline() {
  // No aliasing — each transient keeps its own allocation.
  for (auto& r : m_resources) {
    r.aliasOf = -1;
  }
  m_aliasedBytes = 0;
}

void RenderGraph::packGreedyScanline() {
  m_aliasedBytes = 0;
  for (size_t i = 0; i < m_resources.size(); ++i) {
    auto& a = m_resources[i];
    a.aliasOf = -1;
    if (a.imported || a.firstPass < 0) {
      continue;
    }
    for (size_t j = 0; j < i; ++j) {
      auto& b = m_resources[j];
      if (b.imported || b.aliasOf >= 0) {
        continue;
      }
      if (a.desc.width != b.desc.width || a.desc.height != b.desc.height || a.desc.format != b.desc.format) {
        continue;
      }
      // Non-overlapping lifetimes → share storage.
      if (a.firstPass > b.lastPass || b.firstPass > a.lastPass) {
        a.aliasOf = static_cast<int>(j);
        m_aliasedBytes += a.byteSize ? a.byteSize : uint64_t(a.desc.width) * a.desc.height * 8ull;
        break;
      }
    }
  }
}

void RenderGraph::packBoxing() {
  // Experimental: prefer aliasing largest-first among non-overlapping.
  packBaseline();
  std::vector<size_t> order(m_resources.size());
  for (size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  std::sort(order.begin(), order.end(), [&](size_t i, size_t j) {
    return m_resources[i].byteSize > m_resources[j].byteSize;
  });
  m_aliasedBytes = 0;
  for (size_t oi = 0; oi < order.size(); ++oi) {
    auto& a = m_resources[order[oi]];
    if (a.imported || a.firstPass < 0 || a.aliasOf >= 0) {
      continue;
    }
    for (size_t oj = 0; oj < oi; ++oj) {
      auto& b = m_resources[order[oj]];
      if (b.imported || b.aliasOf >= 0) {
        continue;
      }
      if (a.desc.width != b.desc.width || a.desc.height != b.desc.height || a.desc.format != b.desc.format) {
        continue;
      }
      if (a.firstPass > b.lastPass || b.firstPass > a.lastPass) {
        a.aliasOf = static_cast<int>(order[oj]);
        m_aliasedBytes += a.byteSize;
        break;
      }
    }
  }
}

void RenderGraph::runStage(RGCompileStage stage, rhi::Device& device) {
  const uint32_t si = static_cast<uint32_t>(stage);
  if (!m_stageDirty[si] && m_compiled) {
    return;
  }

  switch (stage) {
  case RGCompileStage::NameResolution:
    // Names already assigned at create/import.
    break;
  case RGCompileStage::DependencyData:
    for (auto& r : m_resources) {
      r.firstPass = -1;
      r.lastPass = -1;
    }
    for (size_t pi = 0; pi < m_passes.size(); ++pi) {
      if (!m_passes[pi].enabled) {
        continue;
      }
      auto touch = [&](RGHandle h) {
        if (h >= m_resources.size()) {
          return;
        }
        auto& r = m_resources[h];
        if (r.firstPass < 0) {
          r.firstPass = static_cast<int>(pi);
        }
        r.lastPass = static_cast<int>(pi);
      };
      for (const auto& a : m_passes[pi].reads) {
        touch(a.handle);
      }
      for (const auto& a : m_passes[pi].writes) {
        touch(a.handle);
      }
    }
    break;
  case RGCompileStage::Validation:
    for (const auto& p : m_passes) {
      if (!p.enabled) {
        continue;
      }
      for (const auto& a : p.reads) {
        if (a.handle >= m_resources.size()) {
          throw std::runtime_error("RenderGraph: invalid read handle in " + p.name);
        }
      }
      for (const auto& a : p.writes) {
        if (a.handle >= m_resources.size()) {
          throw std::runtime_error("RenderGraph: invalid write handle in " + p.name);
        }
      }
    }
    break;
  case RGCompileStage::IRBuild:
    // Pass list is the IR for this engine.
    break;
  case RGCompileStage::PassColoring:
    for (auto& p : m_passes) {
      p.color = p.asyncHint ? 1 : 0;
    }
    break;
  case RGCompileStage::NodeScheduling:
    // Topological order == declaration order for now.
    break;
  case RGCompileStage::BarrierScheduling:
    // Barriers inserted at execute via applyTransitions.
    break;
  case RGCompileStage::StateDeltas:
    break;
  case RGCompileStage::AutoResolution:
    break;
  case RGCompileStage::ResourceScheduling:
    switch (m_packer) {
    case RGPackerMode::Baseline:
      packBaseline();
      break;
    case RGPackerMode::Boxing:
      packBoxing();
      break;
    case RGPackerMode::GreedyScanline:
    default:
      packGreedyScanline();
      break;
    }
    // Allocate only when a pass with a real execute callback writes the resource.
    // (Bookkeeping/alias demo passes use empty execute and must not leak descriptors.)
    for (size_t ri = 0; ri < m_resources.size(); ++ri) {
      auto& r = m_resources[ri];
      if (r.imported || r.aliasOf >= 0 || r.firstPass < 0 || r.owned) {
        continue;
      }
      bool neededByExecute = false;
      for (const auto& p : m_passes) {
        if (!p.enabled || !p.execute) {
          continue;
        }
        for (const auto& a : p.writes) {
          if (a.handle == ri) {
            neededByExecute = true;
            break;
          }
        }
        if (neededByExecute) {
          break;
        }
      }
      if (!neededByExecute) {
        continue;
      }
      rhi::TextureDesc td{};
      td.width = r.desc.width;
      td.height = r.desc.height;
      td.format = r.desc.format;
      td.usage = r.desc.usage;
      td.debugName = r.name;
      r.owned = device.createTexture(td, nullptr, 0);
    }
    break;
  case RGCompileStage::HistoryUpdate:
    break;
  default:
    break;
  }
  m_stageDirty[si] = false;
}

void RenderGraph::compile(rhi::Device& device) {
  for (uint32_t s = 0; s < static_cast<uint32_t>(RGCompileStage::Count); ++s) {
    runStage(static_cast<RGCompileStage>(s), device);
  }
  m_compiled = true;
}

void RenderGraph::applyTransitions(rhi::CommandList& cmd, const Pass& pass) {
  auto touch = [&](const Access& a) {
    rhi::Texture* tex = getTexture(a.handle);
    if (!tex) {
      return;
    }
    cmd.transition(*tex, stateForUsage(a.usage));
    ++m_barrierInsertions;
  };
  for (const auto& a : pass.reads) {
    touch(a);
  }
  for (const auto& a : pass.writes) {
    touch(a);
  }
}

void RenderGraph::execute(rhi::CommandList& cmd) {
  if (!m_compiled) {
    throw std::runtime_error("RenderGraph::execute called before compile");
  }
  m_barrierInsertions = 0;
  for (auto& pass : m_passes) {
    if (!pass.enabled) {
      continue;
    }
    applyTransitions(cmd, pass);
    if (pass.execute) {
      pass.execute(cmd, *this);
    }
  }
}

void RenderGraph::exportGraphviz(const std::string& path) const {
  std::ofstream out(path);
  out << "digraph RenderGraph {\n  rankdir=LR;\n";
  for (size_t i = 0; i < m_passes.size(); ++i) {
    const auto& p = m_passes[i];
    out << "  p" << i << " [label=\"" << p.name << (p.enabled ? "" : " (off)")
        << (p.asyncHint ? "\\nasync" : "") << "\"];\n";
  }
  for (size_t i = 0; i < m_resources.size(); ++i) {
    const auto& r = m_resources[i];
    out << "  r" << i << " [shape=box,label=\"" << r.name;
    if (r.aliasOf >= 0) {
      out << "\\nalias->" << r.aliasOf;
    }
    out << "\"];\n";
  }
  for (size_t pi = 0; pi < m_passes.size(); ++pi) {
    for (const auto& a : m_passes[pi].reads) {
      out << "  r" << a.handle << " -> p" << pi << " [color=blue];\n";
    }
    for (const auto& a : m_passes[pi].writes) {
      out << "  p" << pi << " -> r" << a.handle << " [color=red];\n";
    }
  }
  out << "}\n";
}

} // namespace tucano
