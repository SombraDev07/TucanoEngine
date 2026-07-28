#include "Common.hlsl"

struct VegInstance {
	float4x4 worldMatrix;
	float3 center;       float radius;
	uint   typeId;
	float  windFlex;
	float  windHeight;
	float  windPhase;
};

struct VegDrawArgs {
	uint IndexCountPerInstance;
	uint InstanceCount;
	uint StartIndexLocation;
	int  BaseVertexLocation;
	uint StartInstanceLocation;
};

cbuffer VegFrameCB : register(b1) {
	float4 windDirection;
	float  time;
	float  strength;
	float  gustStrength;
	float  turbulence;
	uint   instanceCount;
	uint   enableHiZ;
	float  screenWidth;
	float  screenHeight;

	float4 frustumPlanes[6];
	float4 observer;
	float  maxDistance;
	float  nearPlane;
	float  densityScale;
	float  ditherFrame;
	uint   enableLODCrossFade;
	float  crossFadeWidth;
	uint2  _pad1;
	float4 lodDistances;
	float4x4 viewProj;
};

StructuredBuffer<VegInstance> InputInstances : register(t0);
Texture2D<float> HiZ : register(t1);
RWStructuredBuffer<VegInstance> OutputInstances : register(u0);
RWStructuredBuffer<uint> VisibleIndices[3] : register(u1);
RWStructuredBuffer<VegDrawArgs> Args[3] : register(u4);

float2 windWave(float2 pos, float t, float phase) {
	float2 d = float2(0, 0);
	d.x = sin(pos.x * 0.3 + t * 1.7 + phase) * cos(pos.y * 0.2 + t * 1.1);
	d.y = cos(pos.x * 0.25 + t * 1.5 + phase) * sin(pos.y * 0.35 + t * 1.3);
	return d * 0.5;
}

float4x4 buildWindMatrix(VegInstance inst) {
	float flex = inst.windFlex * strength;
	float h = inst.windHeight * flex;

	float3 pos = inst.worldMatrix[3].xyz;
	float2 w = windWave(pos.xz, time, inst.windPhase) * h;
	w += windDirection.xz * sin(time * 2.0 + pos.x * 0.15) * h * 0.3;

	float4x4 m = inst.worldMatrix;
	m[3].x += w.x * (1.0 + gustStrength * 0.5);
	m[3].z += w.y * (1.0 + gustStrength * 0.5);

	float bend = h * 0.15 * strength;
	m[0].x += bend * windDirection.x;
	m[0].z += bend * windDirection.z;
	m[1].x += bend * windDirection.x * 0.5;
	m[1].z += bend * windDirection.z * 0.5;

	return m;
}

bool frustumCull(float3 center, float radius) {
	[unroll]
	for (int i = 0; i < 6; ++i) {
		float4 p = frustumPlanes[i];
		if (dot(p.xyz, center) + p.w < -radius) return false;
	}
	float cullDist = lodDistances.w > 0.0 ? lodDistances.w : maxDistance;
	float3 toEye = observer.xyz - center;
	return dot(toEye, toEye) <= cullDist * cullDist;
}

bool hiZOccluded(float3 center, float radius) {
	if (enableHiZ == 0) return false;
	float4 clip = mul(viewProj, float4(center, 1.0));
	if (clip.w <= 0.0) return false;
	float invW = rcp(clip.w);
	float3 ndc = clip.xyz * invW;
	float radNdc = (radius / max(clip.w, 1e-3)) * 2.0 + 0.1;
	float nearestZ = ndc.z - radNdc * 0.5;
	if (ndc.x < -1.0 - radNdc || ndc.x > 1.0 + radNdc ||
	    ndc.y < -1.0 - radNdc || ndc.y > 1.0 + radNdc) return false;

	float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;
	float2 ext = float2(radNdc, radNdc) * 0.55;
	float2 uvMin = saturate(uv - ext);
	float2 uvMax = saturate(uv + ext);
	uint2 dim;
	HiZ.GetDimensions(dim.x, dim.y);
	if (dim.x == 0 || dim.y == 0) return false;
	uint2 p0 = uint2(uvMin * float2(dim));
	uint2 p1 = uint2(uvMax * float2(dim));
	p1 = max(p1, p0 + 1);
	float zOcc = 1.0;
	[unroll] for (uint yy = 0; yy < 2; ++yy) {
		[unroll] for (uint xx = 0; xx < 2; ++xx) {
			uint2 p = uint2(lerp(float2(p0), float2(min(p1, dim - 1)), float2(xx, yy)));
			zOcc = min(zOcc, HiZ.Load(int3(p, 0)));
		}
	}
	return (zOcc > 1e-5 && zOcc < nearestZ - 1e-3);
}

uint selectLOD(float distance) {
	float d1 = lodDistances.y;
	float d2 = lodDistances.z;
	float cull = lodDistances.w > 0.0 ? lodDistances.w : maxDistance;
	if (distance > cull) return 3;
	if (distance > d2) return 2;
	if (distance > d1) return 1;
	return 0;
}

float computeDensityScale(float distance) {
	float cull = lodDistances.w > 0.0 ? lodDistances.w : maxDistance;
	float t = saturate((distance - cull * 0.3) / (cull * 0.7));
	return lerp(densityScale, densityScale * 0.2, t);
}

static const uint kDitherMatrix[16] = {
	 0,  8,  2, 10,
	12,  4, 14,  6,
	 3, 11,  1,  9,
	15,  7, 13,  5
};

bool ditherPass(float alpha, uint2 pixel) {
	uint idx = (pixel.y & 3) * 4 + (pixel.x & 3);
	uint matrixIdx = (kDitherMatrix[idx] + uint(ditherFrame)) & 15;
	float threshold = float(matrixIdx) / 16.0;
	return alpha > threshold;
}

float lodBoundary(uint lod) {
	if (lod == 0) return lodDistances.y;
	if (lod == 1) return lodDistances.z;
	return lodDistances.w > 0.0 ? lodDistances.w : maxDistance;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
	uint i = tid.x;
	bool inRange = i < instanceCount;

	VegInstance inst = (VegInstance)0;
	uint writeLod = 3;
	bool keep = false;

	if (inRange) {
		inst = InputInstances[i];
		float4x4 world = buildWindMatrix(inst);
		inst.worldMatrix = world;
		OutputInstances[i] = inst;

		float3 center = inst.center;
		float r = inst.radius;

		if (frustumCull(center, r) && !hiZOccluded(center, r)) {
			float dist = length(observer.xyz - center);
			uint lod = selectLOD(dist);
			if (lod < 3) {
				float dens = computeDensityScale(dist);
				bool densOk = true;
				if (dens < 1.0) {
					float rnd = frac(sin(dot(float2(float(i), dist), float2(12.9898, 78.233))) * 43758.5453);
					densOk = rnd <= dens;
				}
				if (densOk) {
					float crossFade = 1.0;
					if (enableLODCrossFade) {
						float nextDist = lodBoundary(lod);
						float fadeRange = max(crossFadeWidth * nextDist, 1.0);
						crossFade = saturate((nextDist - dist) / fadeRange);
					}
					uint destLod = lod;
					if (crossFade > 0.01 && crossFade < 0.99) {
						if (!ditherPass(crossFade, tid.xy)) {
							destLod = min(lod + 1, 2);
						}
					}
					writeLod = min(destLod, 2);
					keep = true;
				}
			}
		}
	}

	// Wave-level compaction: one InterlockedAdd per LOD per wave instead of per lane.
	[unroll]
	for (uint lodCand = 0; lodCand < 3; ++lodCand) {
		bool match = keep && writeLod == lodCand;
		uint cnt = WaveActiveCountBits(match);
		if (cnt == 0) continue;
		uint prefix = WavePrefixCountBits(match);
		uint base = 0;
		if (WaveIsFirstLane()) {
			InterlockedAdd(Args[lodCand][0].InstanceCount, cnt, base);
		}
		base = WaveReadLaneFirst(base);
		if (match) {
			VisibleIndices[lodCand][base + prefix] = i;
		}
	}
}
