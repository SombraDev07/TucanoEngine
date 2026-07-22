// Luminance histogram + adaptive exposure (GPU)

cbuffer ExposureCB : register(b1) {
  float4 screenSize;   // xy=wh, zw=1/wh
  float4 adaptParams;  // x=minEv, y=maxEv, z=adaptSpeed, w=targetLum
  uint4 texIds;        // x=hdrSrvIndex
};

Texture2D bindlessHeap[] : register(t0, space0);
SamplerState samp : register(s0);

// Build/clear bind histogram at u0. Reduce binds histogram u0 + exposure u1 (contiguous UAV table).
RWTexture2D<uint> histogramUAV : register(u0);
RWTexture2D<float> exposureUAV : register(u1);

float luma(float3 c) {
  return dot(c, float3(0.2126, 0.7152, 0.0722));
}

uint lumaToBin(float l) {
  float logL = log2(max(l, 1e-5));
  float t = saturate((logL + 10.0) / 20.0);
  return (uint)(t * 255.0 + 0.5);
}

[numthreads(64, 1, 1)]
void CSClearHistogram(uint3 id : SV_DispatchThreadID) {
  if (id.x < 256) {
    histogramUAV[uint2(id.x, 0)] = 0;
  }
}

[numthreads(8, 8, 1)]
void CSBuildHistogram(uint3 id : SV_DispatchThreadID) {
  uint2 dim = uint2(screenSize.xy);
  if (id.x >= dim.x || id.y >= dim.y) {
    return;
  }
  if (((id.x | id.y) & 3u) != 0u) {
    return;
  }
  Texture2D hdr = bindlessHeap[NonUniformResourceIndex(texIds.x)];
  float2 uv = (float2(id.xy) + 0.5) * screenSize.zw;
  float3 c = hdr.SampleLevel(samp, uv, 0).rgb;
  uint bin = lumaToBin(luma(c));
  InterlockedAdd(histogramUAV[uint2(bin, 0)], 1);
}

[numthreads(1, 1, 1)]
void CSReduceExposure(uint3 id : SV_DispatchThreadID) {
  uint total = 0;
  [unroll] for (uint i = 0; i < 256; ++i) {
    total += histogramUAV[uint2(i, 0)];
  }
  if (total < 16) {
    exposureUAV[uint2(0, 0)] = max(exposureUAV[uint2(0, 0)], 1.0);
    return;
  }

  uint skip = total / 20;
  uint acc = 0;
  uint lo = 0;
  uint hi = 255;
  for (uint i = 0; i < 256; ++i) {
    acc += histogramUAV[uint2(i, 0)];
    if (acc >= skip) {
      lo = i;
      break;
    }
  }
  acc = 0;
  for (int j = 255; j >= 0; --j) {
    acc += histogramUAV[uint2(j, 0)];
    if (acc >= skip) {
      hi = (uint)j;
      break;
    }
  }

  double sumLog = 0.0;
  uint sumW = 0;
  for (uint b = lo; b <= hi; ++b) {
    uint c = histogramUAV[uint2(b, 0)];
    float logL = (b / 255.0) * 20.0 - 10.0;
    sumLog += (double)c * (double)logL;
    sumW += c;
  }
  float avgLog = sumW > 0 ? (float)(sumLog / (double)sumW) : 0.0;
  float avgLum = exp2(avgLog);
  float target = max(adaptParams.w, 0.05);
  float exposure = target / max(avgLum, 1e-4);

  float minE = adaptParams.x > 0 ? adaptParams.x : 0.08;
  float maxE = adaptParams.y > 0 ? adaptParams.y : 4.0;
  exposure = clamp(exposure, minE, maxE);

  float prev = exposureUAV[uint2(0, 0)];
  if (prev < 1e-5) {
    prev = exposure;
  }
  float speed = saturate(adaptParams.z);
  exposureUAV[uint2(0, 0)] = lerp(prev, exposure, speed);
}
