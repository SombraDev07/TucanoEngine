// Camera-facing billboard impostors for vegetation LOD2.
// Mesh is a unit quad in XY; VS rebuilds world corners from instance center + camera basis.
// UV optionally samples a yaw-ring atlas cell.

struct InstanceGpu {
  float4x4 transform;
  float3 center;
  float radius;
  uint materialId;
  uint lodMask;
  uint2 _pad;
};

struct VSInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float4 tangent : TANGENT;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
  uint4 boneIndices : BLENDINDICES;
  float4 boneWeights : BLENDWEIGHT;
};

struct VSOutput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
  float2 atlasUV : TEXCOORD2;
  float4 color : COLOR;
  nointerpolation uint useAtlas : TEXCOORD1;
};

cbuffer RootConsts : register(b0) {
  float4x4 viewProj;
  float4x4 camBasis; // cols: right.xyz, up.xyz
};

cbuffer ObjectCB : register(b2) {
  float4x4 worldInvTranspose;
  float4 baseColorFactor;
  float4 materialParams; // metallic, roughness, ao, alphaCutoff
  float4 emissiveFactor;
  uint4 textureIndices;  // x = atlas
  float4 materialExt;    // x = viewsPerType, y = atlasGrid, z = invAtlas, w = cellUV
  uint4 textureIndices2;
  float4 fuzzColor;
  uint4 skinInfo;
};

StructuredBuffer<InstanceGpu> Instances : register(t1, space1);
StructuredBuffer<uint> VisibleInstances : register(t2, space1);
Texture2D bindlessHeap[] : register(t0, space0);
SamplerState samp : register(s0);

uint safeIdx(uint i) { return (i < 8192u) ? i : 0u; }

VSOutput VSMain(VSInput input, uint iid : SV_InstanceID) {
  VSOutput o;
  const uint idx = VisibleInstances[iid];
  const float4x4 world = Instances[idx].transform;
  float3 center = world[3].xyz;
  float scaleX = length(world[0].xyz);
  float scaleY = length(world[1].xyz);
  float halfW = max(scaleX, Instances[idx].radius) * 0.55;
  float halfH = max(scaleY, Instances[idx].radius) * 1.1;

  float3 camRight = normalize(camBasis[0].xyz);
  float3 camUp = normalize(camBasis[1].xyz);

  float2 corner = input.position.xy;
  float3 wp = center + camRight * corner.x * halfW + camUp * (corner.y + 0.5) * halfH;
  o.position = mul(viewProj, float4(wp, 1.0));
  o.uv = input.uv;
  o.color = input.color;
  o.useAtlas = 0;
  o.atlasUV = input.uv;

  if (textureIndices.x > 0) {
    float views = max(materialExt.x, 1.0);
    float grid = max(materialExt.y, 1.0);
    float cell = materialExt.w > 0.0 ? materialExt.w : (1.0 / grid);
    uint typeId = Instances[idx].materialId;
    uint gx = typeId % uint(grid);
    uint gy = typeId / uint(grid);
    float3 camFwd = normalize(cross(camUp, camRight));
    float yaw = atan2(camFwd.x, camFwd.z);
    float uView = frac(yaw * (0.5 / 3.14159265) + 0.5);
    uint view = min(uint(uView * views), uint(views) - 1u);
    float slice = 1.0 / views;
    float2 localUV = float2((float(view) + input.uv.x) * slice, input.uv.y);
    o.atlasUV = float2((float(gx) + localUV.x) * cell, (float(gy) + localUV.y) * cell);
    o.useAtlas = 1;
  }
  return o;
}

struct GBufferOut {
  float4 albedo : SV_Target0;
  float4 normal : SV_Target1;
  float4 orm : SV_Target2;
  float4 emissive : SV_Target3;
  float linearDepth : SV_Target4;
};

GBufferOut PSMain(VSOutput input) {
  float4 albedo = baseColorFactor * input.color;

  if (input.useAtlas != 0 && textureIndices.x > 0) {
    float4 tex = bindlessHeap[NonUniformResourceIndex(safeIdx(textureIndices.x))]
                     .SampleLevel(samp, input.atlasUV, 0);
    albedo *= tex;
  } else {
    float2 d = abs(input.uv - 0.5) * 2.0;
    float sil = saturate(1.0 - length(float2(d.x * 0.85, d.y)));
    albedo.a *= sil * sil;
  }

  if (materialParams.w > 0.0 && albedo.a < materialParams.w) discard;

  // A constant normal made every impostor in the field shade identically, so the LOD2 band read
  // as a flat sheet of colour next to the lit LOD1 geometry behind it. Bend the normal outward
  // across the quad instead: the card stands in for a roughly cylindrical plant, so the left and
  // right edges should face away from the camera and the top should tip upward.
  const float2 bend = input.uv * float2(2.0, -2.0) + float2(-1.0, 1.0); // -1..1, y up
  const float3 camRight = normalize(camBasis[0].xyz);
  const float3 camUp = normalize(camBasis[1].xyz);
  const float3 camFwd = normalize(cross(camUp, camRight));
  float3 n = normalize(-camFwd * 0.75 + camRight * bend.x * 0.6 + camUp * (0.35 + bend.y * 0.25));

  GBufferOut o;
  o.albedo = float4(max(albedo.rgb, float3(0.02, 0.02, 0.02)), 0.05);
  o.normal = float4(n * 0.5 + 0.5, 0.0);
  o.orm = float4(1.0, saturate(materialParams.y), saturate(materialParams.x), 0.04);
  o.emissive = float4(emissiveFactor.rgb, 0.0);
  o.linearDepth = input.position.z;
  return o;
}
