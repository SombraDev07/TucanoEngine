#include "RHI/DX12/DX12Device.h"
#include "RHI/DX12/PipelineCache.h"
#include "Platform/FileSystem.h"

namespace tucano::rhi {

ShaderBytecode ShaderBytecode::loadFromFile(const std::string& path) {
  ShaderBytecode bc;
  bc.data = tucano::readFileBytes(path);
  return bc;
}

std::shared_ptr<RootSignature> DX12Device::createRootSignature(bool allowInputLayout) {
  // Root layout (1.1):
  // 0: 32-bit constants (32)
  // 1: CBV b1
  // 2: CBV b2
  // 3: unbounded SRV table t0 space0 (bindless textures; space2 aliases the same heap as Texture3D)
  // 4: sampler table s0
  // 5: SRV table t0.. space1 (structured buffers: materials, meshlets, …)
  D3D12_DESCRIPTOR_RANGE1 ranges[4]{};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = UINT_MAX;
  ranges[0].BaseShaderRegister = 0;
  ranges[0].RegisterSpace = 0;
  ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;

  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
  ranges[1].NumDescriptors = 8;
  ranges[1].BaseShaderRegister = 0;
  ranges[1].RegisterSpace = 0;
  ranges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
  ranges[1].OffsetInDescriptorsFromTableStart = 0;

  ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[2].NumDescriptors = 16;
  ranges[2].BaseShaderRegister = 0;
  ranges[2].RegisterSpace = 1;
  ranges[2].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
  ranges[2].OffsetInDescriptorsFromTableStart = 0;

  // Texture3D alias of the bindless heap (same table offset, different register space).
  ranges[3] = ranges[0];
  ranges[3].RegisterSpace = 2;

  D3D12_ROOT_PARAMETER1 params[6]{};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[0].Constants.Num32BitValues = 32;
  params[0].Constants.ShaderRegister = 0;
  params[0].Constants.RegisterSpace = 0;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[1].Descriptor.ShaderRegister = 1;
  params[1].Descriptor.RegisterSpace = 0;
  params[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;

  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[2].Descriptor.ShaderRegister = 2;
  params[2].Descriptor.RegisterSpace = 0;
  params[2].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;

  const D3D12_DESCRIPTOR_RANGE1 bindlessRanges[2] = {ranges[0], ranges[3]};
  params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[3].DescriptorTable.NumDescriptorRanges = 2;
  params[3].DescriptorTable.pDescriptorRanges = bindlessRanges;

  params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[4].DescriptorTable.NumDescriptorRanges = 1;
  params[4].DescriptorTable.pDescriptorRanges = &ranges[1];

  params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[5].DescriptorTable.NumDescriptorRanges = 1;
  params[5].DescriptorTable.pDescriptorRanges = &ranges[2];

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC vdesc{};
  vdesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  vdesc.Desc_1_1.NumParameters = 6;
  vdesc.Desc_1_1.pParameters = params;
  vdesc.Desc_1_1.Flags = allowInputLayout ? D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
                                          : D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ComPtr<ID3DBlob> sig;
  ComPtr<ID3DBlob> err;
  HRESULT hr = D3D12SerializeVersionedRootSignature(&vdesc, &sig, &err);
  if (FAILED(hr)) {
    const char* msg = err ? static_cast<const char*>(err->GetBufferPointer()) : "serialize root sig 1.1 failed";
    throw std::runtime_error(msg);
  }

  auto out = std::make_shared<DX12RootSignature>();
  throwIfFailed(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&out->rs)),
                "CreateRootSignature");
  return out;
}

std::shared_ptr<RootSignature> DX12Device::createComputeRootSignature() {
  // Root layout 1.1 (bindless-unified with graphics):
  // 0: 32-bit constants (16)
  // 1: CBV b1
  // 2: unbounded SRV table t0 (bindless heap base 0)
  // 3: UAV table u0..7 (bind at concrete UAV heap index)
  // 4: Sampler table s0..
  D3D12_DESCRIPTOR_RANGE1 ranges[3]{};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = UINT_MAX;
  ranges[0].BaseShaderRegister = 0;
  ranges[0].RegisterSpace = 0;
  ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
  ranges[0].OffsetInDescriptorsFromTableStart = 0;

  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = 8;
  ranges[1].BaseShaderRegister = 0;
  ranges[1].RegisterSpace = 0;
  ranges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
  ranges[1].OffsetInDescriptorsFromTableStart = 0;

  ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
  ranges[2].NumDescriptors = 8;
  ranges[2].BaseShaderRegister = 0;
  ranges[2].RegisterSpace = 0;
  ranges[2].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
  ranges[2].OffsetInDescriptorsFromTableStart = 0;

  D3D12_ROOT_PARAMETER1 params[6]{};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[0].Constants.Num32BitValues = 16;
  params[0].Constants.ShaderRegister = 0;
  params[0].Constants.RegisterSpace = 0;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[1].Descriptor.ShaderRegister = 1;
  params[1].Descriptor.RegisterSpace = 0;
  params[1].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;

  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[2].DescriptorTable.NumDescriptorRanges = 1;
  params[2].DescriptorTable.pDescriptorRanges = &ranges[0];

  params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[3].DescriptorTable.NumDescriptorRanges = 1;
  params[3].DescriptorTable.pDescriptorRanges = &ranges[1];

  params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[4].DescriptorTable.NumDescriptorRanges = 1;
  params[4].DescriptorTable.pDescriptorRanges = &ranges[2];

  // 5: root SRV for RaytracingAccelerationStructure (t0, space1)
  params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[5].Descriptor.ShaderRegister = 0;
  params[5].Descriptor.RegisterSpace = 1;
  params[5].Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC vdesc{};
  vdesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
  vdesc.Desc_1_1.NumParameters = 6;
  vdesc.Desc_1_1.pParameters = params;
  vdesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ComPtr<ID3DBlob> sig;
  ComPtr<ID3DBlob> err;
  HRESULT hr = D3D12SerializeVersionedRootSignature(&vdesc, &sig, &err);
  if (FAILED(hr)) {
    const char* msg = err ? static_cast<const char*>(err->GetBufferPointer()) : "serialize compute root sig 1.1 failed";
    throw std::runtime_error(msg);
  }

  auto out = std::make_shared<DX12RootSignature>();
  out->isCompute = true;
  throwIfFailed(m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&out->rs)),
                "CreateComputeRootSignature");
  return out;
}

std::shared_ptr<PipelineState> DX12Device::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
  auto out = std::make_shared<DX12PipelineState>();
  auto* rs = static_cast<DX12RootSignature*>(desc.rootSignature.get());

  if (desc.useMeshPipeline) {
    // Manual PSO stream for AS+MS+PS (no d3dx12 dependency).
    struct alignas(void*) Stream {
      struct alignas(void*) {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
        ID3D12RootSignature* ptr = nullptr;
      } rootSig;
      struct alignas(void*) {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS;
        D3D12_SHADER_BYTECODE bc{};
      } as;
      struct alignas(void*) {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS;
        D3D12_SHADER_BYTECODE bc{};
      } ms;
      struct alignas(void*) {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
        D3D12_SHADER_BYTECODE bc{};
      } ps;
      struct alignas(void*) {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND;
        D3D12_BLEND_DESC desc{};
      } blend;
      struct alignas(void*) {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK;
        UINT mask = UINT_MAX;
      } sampleMask;
      struct alignas(void*) {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER;
        D3D12_RASTERIZER_DESC desc{};
      } rast;
      struct alignas(void*) {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL;
        D3D12_DEPTH_STENCIL_DESC desc{};
      } depth;
      struct alignas(void*) {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS;
        D3D12_RT_FORMAT_ARRAY rt{};
      } rts;
      struct alignas(void*) {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT;
        DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
      } dsv;
      struct alignas(void*) {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC;
        DXGI_SAMPLE_DESC desc{1, 0};
      } sample;
      struct alignas(void*) {
        D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY;
        D3D12_PRIMITIVE_TOPOLOGY_TYPE topo = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      } topo;
    } stream{};

    stream.rootSig.ptr = rs->rs.Get();
    stream.as.bc = {desc.as.data.data(), desc.as.data.size()};
    stream.ms.bc = {desc.ms.data.data(), desc.ms.data.size()};
    stream.ps.bc = {desc.ps.data.data(), desc.ps.data.size()};
    stream.blend.desc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    stream.rast.desc.FillMode = D3D12_FILL_MODE_SOLID;
    stream.rast.desc.CullMode = toD3D(desc.cullMode);
    stream.rast.desc.DepthClipEnable = TRUE;
    stream.depth.desc.DepthEnable = desc.depthEnable ? TRUE : FALSE;
    stream.depth.desc.DepthWriteMask = desc.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    stream.depth.desc.DepthFunc = toD3D(desc.depthFunc);
    stream.rts.rt.NumRenderTargets = static_cast<UINT>(desc.rtvFormats.size());
    for (size_t i = 0; i < desc.rtvFormats.size(); ++i) {
      stream.rts.rt.RTFormats[i] = toDxgi(desc.rtvFormats[i]);
    }
    stream.dsv.fmt = desc.dsvFormat != Format::Unknown ? toDxgi(desc.dsvFormat) : DXGI_FORMAT_UNKNOWN;
    stream.sample.desc.Count = desc.sampleCount;

    D3D12_PIPELINE_STATE_STREAM_DESC sd{};
    sd.SizeInBytes = sizeof(stream);
    sd.pPipelineStateSubobjectStream = &stream;
    ComPtr<ID3D12Device2> dev2;
    throwIfFailed(m_device.As(&dev2), "QI Device2 for mesh PSO");
    throwIfFailed(dev2->CreatePipelineState(&sd, IID_PPV_ARGS(&out->pso)), "CreateMeshPipelineState");
    return out;
  }

  D3D12_INPUT_ELEMENT_DESC layout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  uint64_t key = PipelineCache::hashBytes(desc.vs.data.data(), desc.vs.data.size());
  key = PipelineCache::hashCombine(key, PipelineCache::hashBytes(desc.ps.data.data(), desc.ps.data.size()));
  key = PipelineCache::hashCombine(key, static_cast<uint64_t>(desc.rtvFormats.size()));
  for (Format fmt : desc.rtvFormats) {
    key = PipelineCache::hashCombine(key, static_cast<uint64_t>(fmt));
  }
  key = PipelineCache::hashCombine(key, static_cast<uint64_t>(desc.dsvFormat));
  key = PipelineCache::hashCombine(key, static_cast<uint64_t>(desc.blend));
  key = PipelineCache::hashCombine(key, static_cast<uint64_t>(desc.cullMode));
  key = PipelineCache::hashCombine(key, static_cast<uint64_t>(desc.depthFunc));
  key = PipelineCache::hashCombine(key, desc.depthEnable ? 1ull : 0ull);
  key = PipelineCache::hashCombine(key, desc.depthWrite ? 1ull : 0ull);
  key = PipelineCache::hashCombine(key, desc.wireframe ? 1ull : 0ull);
  key = PipelineCache::hashCombine(key, desc.useInputLayout ? 1ull : 0ull);
  key = PipelineCache::hashCombine(key, static_cast<uint64_t>(desc.sampleCount));
  key = PipelineCache::hashCombine(key, static_cast<uint64_t>(desc.topology));

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
  pso.pRootSignature = rs->rs.Get();
  pso.VS = {desc.vs.data.data(), desc.vs.data.size()};
  pso.PS = {desc.ps.data.data(), desc.ps.data.size()};
  std::vector<uint8_t> cachedBlob;
  if (m_pipelineCache) {
    cachedBlob = m_pipelineCache->find(key);
    if (!cachedBlob.empty()) {
      pso.CachedPSO.pCachedBlob = cachedBlob.data();
      pso.CachedPSO.CachedBlobSizeInBytes = cachedBlob.size();
    }
  }
  pso.BlendState.AlphaToCoverageEnable = FALSE;
  pso.BlendState.IndependentBlendEnable = FALSE;
  for (uint32_t i = 0; i < 8; ++i) {
    auto& rt = pso.BlendState.RenderTarget[i];
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (desc.blend == BlendMode::AlphaBlend) {
      rt.BlendEnable = TRUE;
      rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
      rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
      rt.BlendOp = D3D12_BLEND_OP_ADD;
      rt.SrcBlendAlpha = D3D12_BLEND_ONE;
      rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
      rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    } else if (desc.blend == BlendMode::Additive) {
      rt.BlendEnable = TRUE;
      rt.SrcBlend = D3D12_BLEND_ONE;
      rt.DestBlend = D3D12_BLEND_ONE;
      rt.BlendOp = D3D12_BLEND_OP_ADD;
      rt.SrcBlendAlpha = D3D12_BLEND_ONE;
      rt.DestBlendAlpha = D3D12_BLEND_ONE;
      rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    } else if (desc.blend == BlendMode::Min) {
      // Depth-as-color targets (shadow atlases): keep the nearest depth regardless of draw order.
      rt.BlendEnable = TRUE;
      rt.SrcBlend = D3D12_BLEND_ONE;
      rt.DestBlend = D3D12_BLEND_ONE;
      rt.BlendOp = D3D12_BLEND_OP_MIN;
      rt.SrcBlendAlpha = D3D12_BLEND_ONE;
      rt.DestBlendAlpha = D3D12_BLEND_ONE;
      rt.BlendOpAlpha = D3D12_BLEND_OP_MIN;
    } else {
      rt.BlendEnable = FALSE;
    }
  }
  pso.SampleMask = UINT_MAX;
  pso.RasterizerState.FillMode = desc.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
  pso.RasterizerState.CullMode = toD3D(desc.cullMode);
  pso.RasterizerState.FrontCounterClockwise = FALSE;
  pso.RasterizerState.DepthClipEnable = TRUE;
  pso.DepthStencilState.DepthEnable = desc.depthEnable ? TRUE : FALSE;
  pso.DepthStencilState.DepthWriteMask = desc.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
  pso.DepthStencilState.DepthFunc = toD3D(desc.depthFunc);
  if (desc.useInputLayout) {
    pso.InputLayout = {layout, _countof(layout)};
  } else {
    pso.InputLayout = {nullptr, 0};
  }
  pso.PrimitiveTopologyType = toD3DType(desc.topology);
  pso.NumRenderTargets = static_cast<UINT>(desc.rtvFormats.size());
  for (size_t i = 0; i < desc.rtvFormats.size(); ++i) {
    pso.RTVFormats[i] = toDxgi(desc.rtvFormats[i]);
  }
  pso.DSVFormat = desc.dsvFormat != Format::Unknown ? toDxgi(desc.dsvFormat) : DXGI_FORMAT_UNKNOWN;
  pso.SampleDesc.Count = desc.sampleCount;

  HRESULT hr = m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&out->pso));
  if (FAILED(hr) && pso.CachedPSO.pCachedBlob) {
    // Stale cache entry — rebuild
    pso.CachedPSO = {};
    hr = m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&out->pso));
  }
  throwIfFailed(hr, "CreateGraphicsPipelineState");

  if (m_pipelineCache) {
    ComPtr<ID3DBlob> blob;
    if (SUCCEEDED(out->pso->GetCachedBlob(&blob)) && blob) {
      m_pipelineCache->store(key, blob->GetBufferPointer(), blob->GetBufferSize());
    }
  }
  return out;
}

std::shared_ptr<PipelineState> DX12Device::createComputePipeline(const ComputePipelineDesc& desc) {
  auto out = std::make_shared<DX12PipelineState>();
  out->isCompute = true;
  auto* rs = static_cast<DX12RootSignature*>(desc.rootSignature.get());

  uint64_t key = PipelineCache::hashBytes(desc.cs.data.data(), desc.cs.data.size());
  key = PipelineCache::hashCombine(key, 0xC0C0C0C0ull);

  D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
  pso.pRootSignature = rs->rs.Get();
  pso.CS = {desc.cs.data.data(), desc.cs.data.size()};
  std::vector<uint8_t> cachedBlob;
  if (m_pipelineCache) {
    cachedBlob = m_pipelineCache->find(key);
    if (!cachedBlob.empty()) {
      pso.CachedPSO.pCachedBlob = cachedBlob.data();
      pso.CachedPSO.CachedBlobSizeInBytes = cachedBlob.size();
    }
  }
  HRESULT hr = m_device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&out->pso));
  if (FAILED(hr) && pso.CachedPSO.pCachedBlob) {
    pso.CachedPSO = {};
    hr = m_device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&out->pso));
  }
  throwIfFailed(hr, "CreateComputePipelineState");
  if (m_pipelineCache) {
    ComPtr<ID3DBlob> blob;
    if (SUCCEEDED(out->pso->GetCachedBlob(&blob)) && blob) {
      m_pipelineCache->store(key, blob->GetBufferPointer(), blob->GetBufferSize());
    }
  }
  return out;
}

} // namespace tucano::rhi
