// Amplification + Mesh shader path for meshlets (VisBuffer ID).
struct MeshletGPU {
  float3 center;
  float radius;
  float3 coneAxis;
  float coneCutoff;
  uint indexOffset;
  uint indexCount;
  uint materialIndex;
  uint pad1;
};

struct Payload {
  uint meshletIndex;
  uint materialIndex;
  uint indexOffset;
  uint indexCount;
};

cbuffer RootConsts : register(b0) {
  float4x4 viewProj;
  float4x4 world;
};

cbuffer ObjectCB : register(b2) {
  float4x4 worldInvTranspose;
  float4 _u0, _u1, _u2;
  uint4 _u3;
  float4 _u4;
  uint4 _u5;
  float4 _u6;
};

StructuredBuffer<MeshletGPU> Meshlets : register(t0, space1);
StructuredBuffer<uint> Indices : register(t1, space1);
StructuredBuffer<float4> Positions : register(t2, space1);
StructuredBuffer<float2> UVs : register(t3, space1);
StructuredBuffer<float4> Normals : register(t4, space1);

[NumThreads(1, 1, 1)]
void ASMain(uint dtid : SV_DispatchThreadID) {
  Payload p;
  MeshletGPU m = Meshlets[dtid];
  p.meshletIndex = dtid;
  p.materialIndex = m.materialIndex;
  p.indexOffset = m.indexOffset;
  p.indexCount = m.indexCount;
  DispatchMesh(1, 1, 1, p);
}

struct MSOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
  float3 normal : TEXCOORD1;
  nointerpolation uint matId : TEXCOORD2;
};

[OutputTopology("triangle")]
[NumThreads(128, 1, 1)]
void MSMain(uint gtid : SV_GroupThreadID, in payload Payload p, out vertices MSOut verts[128],
            out indices uint3 tris[126]) {
  uint vCount = min(p.indexCount, 128u);
  uint pCount = min(p.indexCount / 3u, 126u);
  SetMeshOutputCounts(vCount, pCount);

  if (gtid < vCount) {
    uint idx = Indices[p.indexOffset + gtid];
    float3 local = Positions[idx].xyz;
    float4 wp = mul(world, float4(local, 1.0));
    MSOut o;
    o.pos = mul(viewProj, wp);
    o.uv = UVs[idx];
    o.normal = normalize(mul((float3x3)worldInvTranspose, Normals[idx].xyz));
    o.matId = p.materialIndex;
    verts[gtid] = o;
  }
  if (gtid < pCount) {
    uint base = gtid * 3;
    tris[gtid] = uint3(base + 0, base + 1, base + 2);
  }
}

struct PSOut {
  uint id : SV_Target0;
  float2 uv : SV_Target1;
  float4 normal : SV_Target2;
  float depth : SV_Target3;
};

PSOut PSMain(MSOut input, uint primID : SV_PrimitiveID) {
  PSOut o;
  o.id = ((_u3.x + input.matId) << 16) | (primID & 0xFFFFu);
  o.uv = input.uv;
  o.normal = float4(input.normal * 0.5 + 0.5, 0);
  o.depth = input.pos.z;
  return o;
}
