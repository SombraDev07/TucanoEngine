// Port of FidelityFX skydomeproc.hlsl (Preetham analytic sky model) to BSL.
// Copyright (c) 2024 Advanced Micro Devices, Inc. (original HLSL). MIT-licensed.
// Original sky model: Simon Wallner (2011), based on Preetham et al.
// "A Practical Analytic Model for Daylight".
//
// Three dispatch modes selected via the SKY_PASS variation:
//   ENVIRONMENT  - generates the HDR environment cubemap from the Preetham model
//   IRRADIANCE   - convolves the environment cubemap into a diffuse irradiance cubemap
//   PREFILTERED  - importance-samples the environment cubemap into a prefiltered radiance cubemap

#include "$ENGINE$\SkyProceduralCommon.bslinc"

shader SkyProcedural
{
	featureset = HighEnd;

	mixin SkyProceduralBuffers;

	variations
	{
		SKY_PASS = { 0, 1, 2 };
	};

	code
	{
		// ----- outputs / inputs per pass -----
		#if SKY_PASS == 0
			RWTexture2DArray<float4> gEnvironmentCube;
		#elif SKY_PASS == 1
			TextureCube gEnvironmentCube;
			SamplerState gLinearWrapSamp;
			RWTexture2DArray<float4> gIrradianceCube;
		#elif SKY_PASS == 2
			TextureCube gEnvironmentCube;
			StructuredBuffer<float4> gSampleDirections;
			SamplerState gLinearWrapSamp;
			RWTexture2DArray<float4> gPrefilteredCube;
		#endif

		// ----- Preetham constants (verbatim from skydomeproc.hlsl) -----
		static const float e = 2.71828182845904523536028747135266249775724709369995957;
		static const float pi = 3.141592653589793238462643383279502884197169;

		static const float3 lambda = float3(680E-9, 550E-9, 450E-9);
		static const float3 totalRayleigh = float3(5.804542996261093E-6, 1.3562911419845635E-5, 3.0265902468824876E-5);

		static const float v = 4.0;
		static const float3 K = float3(0.686, 0.678, 0.666);
		static const float3 MieConst = float3(1.8399918514433978E14, 2.7798023919660528E14, 4.0790479543861094E14);

		static const float cutoffAngle = 1.6110731556870734;
		static const float steepness = 1.5;
		static const float EE = 1000.0;

		static const float3 up = float3(0.0, 1.0, 0.0);

		static const float n = 1.0003;
		static const float N = 2.545E25;
		static const float rayleighZenithLength = 8.4E3;
		static const float mieZenithLength = 1.25E3;
		static const float sunAngularDiameterCos = 0.999956676946448443553574619906976478926848692873900859324;

		static const float THREE_OVER_SIXTEENPI = 0.05968310365946075;
		static const float ONE_OVER_FOURPI = 0.07957747154594767;

		float sunIntensity(float zenithAngleCos)
		{
			zenithAngleCos = clamp(zenithAngleCos, -1.0, 1.0);
			return EE * max(0.0, 1.0 - pow(e, -((cutoffAngle - acos(zenithAngleCos)) / steepness)));
		}

		float3 totalMie(float t)
		{
			float c = (0.2 * t) * 10E-18;
			return 0.434 * c * MieConst;
		}

		float rayleighPhase(float cosTheta)
		{
			return THREE_OVER_SIXTEENPI * (1.0 + pow(cosTheta, 2.0));
		}

		float hgPhase(float cosTheta, float g)
		{
			float g2 = pow(g, 2.0);
			float inverse = 1.0 / pow(abs(1.0 - 2.0 * g * cosTheta + g2), 1.5);
			return ONE_OVER_FOURPI * ((1.0 - g2) * inverse);
		}

		float3 getDirection(uint face, float x, float y)
		{
			float3 direction = 0.0;
			switch (face)
			{
			case 0: direction = float3(1.0, y, -x); break;
			case 1: direction = float3(-1.0, y, x); break;
			case 2: direction = float3(x, 1.0, -y); break;
			case 3: direction = float3(x, -1.0, y); break;
			case 4: direction = float3(x, y, 1.0); break;
			case 5: direction = float3(-x, y, -1.0); break;
			}
			return normalize(direction);
		}

		float3 getSkyColor(float3 direction)
		{
			float vSunE = sunIntensity(dot(gSunDirection.xyz, up));
			float vSunfade = 1.0 - clamp(1.0 - exp(gSunDirection.y), 0.0, 1.0);

			float rayleighCoefficient = gRayleigh - (1.0 * (1.0 - vSunfade));

			float3 vBetaR = totalRayleigh * rayleighCoefficient;
			float3 vBetaM = totalMie(gTurbidity) * gMieCoefficient;

			float zenithAngle = acos(max(0.0, dot(up, direction)));
			float inverse = 1.0 / (cos(zenithAngle) + 0.15 * pow(abs(93.885 - ((zenithAngle * 180.0) / pi)), -1.253));
			float sR = rayleighZenithLength * inverse;
			float sM = mieZenithLength * inverse;

			float3 Fex = exp(-(vBetaR * sR + vBetaM * sM));

			float cosTheta = dot(direction, gSunDirection.xyz);

			float rPhase = rayleighPhase(cosTheta * 0.5 + 0.5);
			float3 betaRTheta = vBetaR * rPhase;

			float mPhase = hgPhase(cosTheta, gMieDirectionalG);
			float3 betaMTheta = vBetaM * mPhase;

			float3 Lin = pow(abs(vSunE * ((betaRTheta + betaMTheta) / (vBetaR + vBetaM)) * (1.0 - Fex)), float3(1.5, 1.5, 1.5));
			Lin *= lerp(float3(1.0, 1.0, 1.0),
				pow(vSunE * ((betaRTheta + betaMTheta) / (vBetaR + vBetaM)) * Fex, float3(0.5, 0.5, 0.5)),
				clamp(pow(1.0 - dot(up, gSunDirection.xyz), 5.0), 0.0, 1.0));

			float3 L0 = float3(0.1, 0.1, 0.1) * Fex;

			float sundisk = smoothstep(sunAngularDiameterCos, sunAngularDiameterCos + 0.00002, cosTheta);
			L0 += (vSunE * 19000.0 * Fex) * sundisk;

			float3 texColor = (Lin + L0) * 0.04 + float3(0.0, 0.0003, 0.00075);

			float3 color = (log2(2.0 / pow(gLuminance, 4.0))) * texColor;
			float3 retColor = pow(abs(color), (1.0 / (1.2 + (1.2 * vSunfade))));
			return retColor;
		}

		#if SKY_PASS == 1 || SKY_PASS == 2
		float4 sampleEnvironmentCube(float3 n)
		{
			return gEnvironmentCube.SampleLevel(gLinearWrapSamp, n, 0);
		}
		#endif

		#if SKY_PASS == 1
		float3 getIrradiance(float3 direction)
		{
			float3 irradiance = 0.0;
			float3 right = cross(up, direction);
			float3 localUp = cross(direction, right);

			float sampleDelta = 0.025;
			float nrSamples = 0.0;
			for (float phi = 0.0; phi < 2.0 * pi; phi += sampleDelta)
			{
				for (float theta = 0.0; theta < 0.5 * pi; theta += sampleDelta)
				{
					float3 tangentSample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
					float3 sampleVec = tangentSample.x * right + tangentSample.y * localUp + tangentSample.z * direction;
					irradiance += sampleEnvironmentCube(sampleVec).rgb * cos(theta) * sin(theta);
					nrSamples++;
				}
			}
			irradiance = pi * irradiance * (1.0 / float(nrSamples));
			return irradiance;
		}
		#endif

		[numthreads(8, 8, 1)]
		void csmain(uint3 dtID : SV_DispatchThreadID)
		{
			if (dtID.x >= gCubeFaceSize || dtID.y >= gCubeFaceSize)
				return;

			float invSize = 1.0 / float(gCubeFaceSize);
			float2 uv = float2(dtID.xy) * invSize;

			#if SKY_PASS == 0
				float3 dir = getDirection(gCubeFaceIndex, 2 * uv.x - 1, 1 - 2 * uv.y);
				gEnvironmentCube[uint3(dtID.x, dtID.y, gCubeFaceIndex)] = float4(getSkyColor(dir), 1.0);
			#elif SKY_PASS == 1
				float3 dir = getDirection(gCubeFaceIndex, 2 * uv.x - 1, 1 - 2 * uv.y);
				gIrradianceCube[uint3(dtID.x, dtID.y, gCubeFaceIndex)] = float4(getIrradiance(dir), 1.0);
			#elif SKY_PASS == 2
				float3 N = getDirection(gCubeFaceIndex, 2 * uv.x - 1, 1 - 2 * uv.y);
				float3 R = N;
				float3 V = R;

				float3 prefilteredColor = 0.0;
				float totalWeight = 0.0;

				float3 localUp = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
				float3 tangent = normalize(cross(localUp, N));
				float3 bitangent = cross(N, tangent);
				float3x3 tangentToWorld = float3x3(tangent, bitangent, N);

				for (uint i = 0u; i < gSampleCount; ++i)
				{
					float3 H = mul(gSampleDirections[i].xyz, tangentToWorld);
					float3 L = normalize(2.0 * dot(V, H) * H - V);
					float NdotL = max(dot(N, L), 0.0);
					if (NdotL > 0.0)
					{
						prefilteredColor += sampleEnvironmentCube(L).rgb * NdotL;
						totalWeight += NdotL;
					}
				}
				prefilteredColor = prefilteredColor / totalWeight;
				gPrefilteredCube[uint3(dtID.x, dtID.y, gCubeFaceIndex)] = float4(prefilteredColor, 1.0);
			#endif
		}
	};
};