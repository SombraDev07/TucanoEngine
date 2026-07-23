// GPU gate for WM-4 cell culling.
//
// Creates a headless device with the D3D12 debug layer ON, dispatches CellCull.hlsl over a known
// cell set, reads the result back, and asserts it matches the CPU reference (WorldCuller) exactly.
// The debug layer turns any descriptor/state mistake into a printed error instead of a silent
// zero, which is precisely what a compute-cull needs during bring-up.

#include "RHI/RHI.h"
#include "World/FrustumCull.h"
#include "World/GpuCellCuller.h"
#include "World/WorldGrid.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

using namespace tucano;
using namespace tucano::world;

namespace {

int g_failures = 0;

void check(const std::string& label, bool ok) {
  std::printf(ok ? "  OK   %s\n" : "  FAIL %s\n", label.c_str());
  if (!ok) ++g_failures;
}

std::string shaderPath(const char* name) { return std::string(TUCANO_SHADER_DIR) + "/" + name; }

/// Builds a deterministic field of cells and a camera. Returned arrays are parallel.
void buildScene(std::vector<CellId>& ids, std::vector<glm::vec3>& mins,
                std::vector<glm::vec3>& maxs, glm::mat4& viewProj, glm::vec3& eye) {
  WorldGrid grid(WorldGridDesc{glm::vec3(0.0f), 65536.0f, 10});
  const float cs = grid.cellSize(10);
  for (int z = -6; z <= 40; ++z)
    for (int y = -6; y <= 6; ++y)
      for (int x = -20; x <= 20; ++x) {
        const CellId id{x, y, z, 10};
        glm::vec3 bmin, bmax;
        grid.boundsOf(id, bmin, bmax);
        ids.push_back(id);
        mins.push_back(bmin);
        maxs.push_back(bmax);
      }
  eye = glm::vec3(cs * 0.5f, cs * 0.5f, -cs);
  const glm::mat4 view = glm::lookAtLH(eye, eye + glm::vec3(0, 0, 1), glm::vec3(0, 1, 0));
  const glm::mat4 proj = glm::perspectiveLH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.5f, 5000.0f);
  viewProj = proj * view;
}

} // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("=== Tucano World Machine — WM-4 GPU cull gate ===\n\n");

  std::unique_ptr<rhi::Device> device;
  try {
    device = rhi::Device::create(/*enableDebugLayer=*/true);
  } catch (const std::exception& e) {
    std::printf("  SKIP no D3D12 device (%s)\n", e.what());
    return 0; // headless CI without a GPU is not a failure of this code
  }
  check("headless device created with the debug layer", device != nullptr);

  std::shared_ptr<rhi::RootSignature> root;
  std::shared_ptr<rhi::PipelineState> pso;
  try {
    root = device->createComputeRootSignature();
    rhi::ComputePipelineDesc pd;
    pd.rootSignature = root;
    pd.cs = rhi::ShaderBytecode::loadFromFile(shaderPath("CellCull_CSMain.cso"));
    pso = device->createComputePipeline(pd);
  } catch (const std::exception& e) {
    std::printf("  FAIL could not build the cull pipeline (%s)\n", e.what());
    return 1;
  }
  check("cull compute pipeline built", pso != nullptr);

  std::vector<CellId> ids;
  std::vector<glm::vec3> mins, maxs;
  glm::mat4 viewProj;
  glm::vec3 eye;
  buildScene(ids, mins, maxs, viewProj, eye);

  CullConfig cfg;
  cfg.lodStep = 200.0f;
  cfg.maxLod = 4;
  cfg.maxDistance = 3000.0f;

  WorldCuller cpuCuller;
  std::vector<VisibleCell> cpu;
  cpuCuller.cullBoxes(ids, mins, maxs, viewProj, eye, cfg, cpu);
  check("the CPU reference finds a non-trivial visible set", cpu.size() > 100 && cpu.size() < ids.size());

  GpuCellCuller gpuCuller(*device, *root, *pso);
  std::vector<VisibleCell> gpu = gpuCuller.cull(ids, mins, maxs, viewProj, eye, cfg);
  std::printf("  cells=%zu cpuVisible=%zu gpuVisible=%zu\n", ids.size(), cpu.size(), gpu.size());
  check("the GPU produced a visible set", !gpu.empty());

  // Compare as (cell → lod) maps.
  std::unordered_map<uint64_t, uint32_t> cpuMap, gpuMap;
  for (const auto& v : cpu) cpuMap[v.id.key()] = v.lod;
  for (const auto& v : gpu) gpuMap[v.id.key()] = v.lod;

  int missingOnGpu = 0, extraOnGpu = 0, lodMismatch = 0;
  for (const auto& [key, lod] : cpuMap) {
    auto it = gpuMap.find(key);
    if (it == gpuMap.end()) ++missingOnGpu;
    else if (it->second != lod) ++lodMismatch;
  }
  for (const auto& [key, lod] : gpuMap) {
    if (cpuMap.find(key) == cpuMap.end()) ++extraOnGpu;
  }

  check("GPU visible set equals the CPU set (no cell missing)", missingOnGpu == 0);
  check("GPU visible set equals the CPU set (no extra cell)", extraOnGpu == 0);
  check("GPU assigns the same LOD as the CPU", lodMismatch == 0);

  // Run it a second time to prove the buffers are reusable across dispatches without leaking state.
  std::vector<VisibleCell> gpu2 = gpuCuller.cull(ids, mins, maxs, viewProj, eye, cfg);
  check("a repeated cull gives the same count", gpu2.size() == gpu.size());

  device->waitIdle();
  std::printf("\n=== failures: %d ===\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
