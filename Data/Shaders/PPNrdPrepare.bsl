#include "$ENGINE$\PPBase.bslinc"
#include "$ENGINE$\PerCameraData.bslinc"

shader PPNrdPrepareNormalRoughness
{
	mixin PPBase;

	code
	{
		SamplerState gInputSamp;
		Texture2D gNormalsTex;
		Texture2D gRoughMetalTex;
		
		float4 fsmain(VStoFS input) : SV_Target0
		{
			float3 normal = gNormalsTex.Sample(gInputSamp, input.uv0).xyz;
			// Normal is stored as worldNormal * 0.5 + 0.5, convert back to [-1, 1]
			normal = normal * 2.0f - 1.0f;
			
			// Roughness is in R channel of RoughMetalTex
			float roughness = gRoughMetalTex.Sample(gInputSamp, input.uv0).r;
			
			// NRD expects: RGB = world-space normal (signed), A = linear roughness
			// Pack normal as * 0.5 + 0.5 for RGBA8_UNORM encoding (NRD normal encoding = 2 = R10G10B10A2)
			return float4(normal * 0.5f + 0.5f, roughness);
		}	
	};
};

shader PPNrdPrepareViewZ
{
	mixin PPBase;
	mixin PerCameraData;

	code
	{
		SamplerState gInputSamp;
		Texture2D gDepthTex;
		
		float4 fsmain(VStoFS input) : SV_Target0
		{
			float deviceZ = gDepthTex.Sample(gInputSamp, input.uv0).r;
			// convertFromDeviceZ returns linear view-space Z (positive)
			float viewZ = convertFromDeviceZ(deviceZ);
			return float4(viewZ, 0.0f, 0.0f, 1.0f);
		}	
	};
};