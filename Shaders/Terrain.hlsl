#include "Common.hlsl"

#define kMaxLayers 8

Texture2D bindlessTex[] : register(t0, space0);
SamplerState s0 : register(s0);

struct TerrainVSInput {
	float3 position : POSITION;
	float3 normal   : NORMAL;
	float2 uv       : TEXCOORD;
};

struct TerrainVSOutput {
	float4 position      : SV_POSITION;
	float3 worldPos      : WORLD_POS;
	float3 worldNormal   : WORLD_NORMAL;
	float3 tangent       : TANGENT;
	float3 bitangent     : BITANGENT;
	float2 uv            : TEXCOORD;
	float4 color         : COLOR;
};

cbuffer RootConsts : register(b0) {
	float4x4 viewProj;
	float4x4 world;
};

struct MaterialLayerGpu {
	float  slopeMin;
	float  slopeMax;
	float  heightMin;
	float  heightMax;
	float  uvScale;
	float  roughness;
	float  metallic;
	float  normalStrength;
	float  albedoR;
	float  albedoG;
	float  albedoB;
	uint   triplanar;
	uint   textureIndex;
	float  pad[2];
};

cbuffer MaterialLayerCB : register(b2) {
	uint   layerCount;
	float  heightScale;
	float  heightBias;
	float  padCB;
	MaterialLayerGpu layers[kMaxLayers];
};

TerrainVSOutput VSMain(TerrainVSInput input) {
	TerrainVSOutput o;
	float4 worldPos = mul(float4(input.position, 1.0), world);
	o.position = mul(worldPos, viewProj);
	o.worldPos = worldPos.xyz;
	float3 worldNormal = normalize(mul(float4(input.normal, 0.0), world).xyz);
	o.worldNormal = worldNormal;
	float3 c1 = cross(worldNormal, float3(0, 1, 0));
	float3 c2 = cross(worldNormal, float3(1, 0, 0));
	o.tangent = length(c1) > length(c2) ? normalize(c1) : normalize(c2);
	o.bitangent = normalize(cross(worldNormal, o.tangent));
	o.uv = input.uv;
	o.color = float4(1, 1, 1, 1);
	return o;
}

float4 sampleTriplanar(uint texIdx, float3 worldPos, float3 worldNormal, float uvScale) {
	float3 weights = abs(worldNormal);
	float wsum = weights.x + weights.y + weights.z;
	weights /= max(wsum, 0.001);

	float2 uvX = worldPos.zy * uvScale;
	float2 uvY = worldPos.xz * uvScale;
	float2 uvZ = worldPos.xy * uvScale;

	float4 cx = texIdx != 0 ? bindlessTex[NonUniformResourceIndex(texIdx)].Sample(s0, uvX) : float4(0,0,0,0);
	float4 cy = texIdx != 0 ? bindlessTex[NonUniformResourceIndex(texIdx)].Sample(s0, uvY) : float4(0,0,0,0);
	float4 cz = texIdx != 0 ? bindlessTex[NonUniformResourceIndex(texIdx)].Sample(s0, uvZ) : float4(0,0,0,0);

	return cx * weights.x + cy * weights.y + cz * weights.z;
}

struct PSOutput {
	float4 albedo   : SV_Target0;
	float4 normal   : SV_Target1;
	float4 orm      : SV_Target2;
	float4 emissive : SV_Target3;
	float4 depthCol : SV_Target4;
};

PSOutput PSMain(TerrainVSOutput input) {
	PSOutput o;
	float3 N = normalize(input.worldNormal);
	float slope = 1.0 - abs(N.y);
	float normalizedHeight = input.worldPos.y * heightScale + heightBias;

	float3 blendedAlbedo = float3(0, 0, 0);
	float  blendedRoughness = 0;
	float  blendedMetallic = 0;
	float  totalWeight = 0;

	for (uint i = 0; i < layerCount && i < kMaxLayers; ++i) {
		MaterialLayerGpu layer = layers[i];

		float slopeMatch = smoothstep(layer.slopeMin - 0.05, layer.slopeMin, slope)
		                 * (1.0 - smoothstep(layer.slopeMax, layer.slopeMax + 0.05, slope));
		float heightMatch = smoothstep(layer.heightMin - 0.05, layer.heightMin, normalizedHeight)
		                  * (1.0 - smoothstep(layer.heightMax, layer.heightMax + 0.05, normalizedHeight));
		float weight = slopeMatch * heightMatch;
		if (weight < 0.001) continue;

		float3 albedo = float3(layer.albedoR, layer.albedoG, layer.albedoB);
		if (layer.textureIndex != 0) {
			if (layer.triplanar != 0) {
				float4 tex = sampleTriplanar(layer.textureIndex, input.worldPos, N, layer.uvScale);
				albedo *= tex.rgb;
			} else {
				float4 tex = bindlessTex[NonUniformResourceIndex(layer.textureIndex)].Sample(s0, input.uv * layer.uvScale);
				albedo *= tex.rgb;
			}
		}

		blendedAlbedo += albedo * weight;
		blendedRoughness += layer.roughness * weight;
		blendedMetallic += layer.metallic * weight;
		totalWeight += weight;
	}

	if (totalWeight < 0.001) {
		blendedAlbedo = float3(0.5, 0.5, 0.5);
		blendedRoughness = 0.8;
		blendedMetallic = 0;
	} else {
		blendedAlbedo /= totalWeight;
		blendedRoughness /= totalWeight;
		blendedMetallic /= totalWeight;
	}

	o.albedo   = float4(blendedAlbedo, 1.0);
	o.normal   = float4(N * 0.5 + 0.5, 0.0);
	o.orm      = float4(blendedMetallic, blendedRoughness, 1.0, 0.04);
	o.emissive = float4(0, 0, 0, 0);
	o.depthCol = float4(0, 0, 0, 0);
	return o;
}
