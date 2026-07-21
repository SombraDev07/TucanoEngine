// DDGI atlas update — matches Phase3CB layout (b1)
cbuffer Phase3CB : register(b1) {
  float4x4 invViewProj;
  float4 cameraPos;
  float4 screenSize;
  float4 params;
  float4 volumeOriginExtent;
  float4 iblParams;
};

Texture2D hdrTex : register(t0);
Texture2D depthTex : register(t1);
Texture2D normalTex : register(t2);
Texture2D atlasIn : register(t3);
SamplerState samp : register(s0);
RWTexture2D<float4> atlasOut : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
  uint2 dim;
  atlasOut.GetDimensions(dim.x, dim.y);
  if (id.x >= dim.x || id.y >= dim.y) {
    return;
  }

  uint cell = 8;
  uint probesX = 8;
  uint px = id.x / cell;
  uint py = id.y / cell;
  float2 cellUV = (float2(px, py) + 0.5) / float2(probesX, probesX);

  float3 origin = volumeOriginExtent.xyz;
  float extent = max(volumeOriginExtent.w, 1.0);
  float3 probePos = origin + float3(cellUV.x, 0.35, cellUV.y) * extent;

  float3 toP = probePos - cameraPos.xyz;
  float2 uv = saturate(float2(toP.x, -toP.y) * 0.05 + 0.5);

  float3 sampleCol = 0;
  float wsum = 0;
  [unroll] for (int y = -2; y <= 2; ++y) {
    [unroll] for (int x = -2; x <= 2; ++x) {
      float2 suv = uv + float2(x, y) * screenSize.zw * 8.0;
      if (any(suv < 0) || any(suv > 1)) {
        continue;
      }
      float d = depthTex.SampleLevel(samp, suv, 0).r;
      if (d <= 1e-4) {
        continue;
      }
      sampleCol += hdrTex.SampleLevel(samp, suv, 0).rgb;
      wsum += 1.0;
    }
  }
  if (wsum > 0) {
    sampleCol /= wsum;
  } else {
    sampleCol = float3(0.04, 0.05, 0.07);
  }

  float3 prev = atlasIn.SampleLevel(samp, (float2(id.xy) + 0.5) / float2(dim), 0).rgb;
  float3 outC = lerp(prev, sampleCol * params.x, 0.25);
  atlasOut[id.xy] = float4(outC, 1);
}
