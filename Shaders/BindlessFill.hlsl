#ifdef TUCANO_SPIRV
[[vk::binding(0, 4)]] RWTexture2D<float4> OutTex : register(u0);
#else
RWTexture2D<float4> OutTex : register(u0);
#endif

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
  uint w, h;
  OutTex.GetDimensions(w, h);
  if (id.x >= w || id.y >= h) {
    return;
  }
  bool checker = ((id.x / 8) + (id.y / 8)) % 2 == 0;
  OutTex[id.xy] = checker ? float4(0.95, 0.25, 0.2, 1) : float4(0.15, 0.55, 0.95, 1);
}
