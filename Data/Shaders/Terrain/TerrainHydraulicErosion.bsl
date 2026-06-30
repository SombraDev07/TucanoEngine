// Copyright 2026 TucanoEngine / B3DFramework — MIT License
//
// TerrainHydraulicErosion.bsl
// ---------------------------
// Compute shader for GPU hydraulic and thermal terrain erosion.
//
// Algorithm: "Fast Hydraulic Erosion Simulation and Visualization on GPU"
//   Mei & Decaudin, Pacific Graphics 2007.
//
// Each invocation processes one texel (cell) of the terrain.
// Group size: 8×8 threads.
//
// Pass structure (controlled by gIteration):
//   Phase 0  — Add rainfall to water
//   Phase 1  — Compute outflow flux (pipe model)
//   Phase 2  — Update water depth + velocity from flux
//   Phase 3  — Erosion and deposition
//   Phase 4  — Sediment transport
//   Phase 5  — Evaporation
//   Phase 6  — Thermal erosion (talus)

Parameters:
{
	// RW textures (UAV)
	RWTexture2D<float>  gHeightmap;
	RWTexture2D<float>  gWater;
	RWTexture2D<float>  gSediment;
	RWTexture2D<float2> gFlowMap;
	RWTexture2D<float>  gTempTex;

	uniform gErosionParams
	{
		float  gRainfallAmount;
		float  gEvaporationRate;
		float  gErosionCapacity;
		float  gErosionDeposit;
		float  gErosionRate;
		float  gMinWaterToErode;
		float  gTalusAngle;
		float  gThermalRate;
		int2   gResolution;
		uint   gIteration;
	};
}

Technique Default:
{
	// Compute technique — no raster/depth states
}

Compute:
{
	HLSLBegin:

	[numthreads(8, 8, 1)]
	void main(uint3 dispatchThreadID : SV_DispatchThreadID)
	{
		int2 id = (int2)dispatchThreadID.xy;
		if (any(id >= gResolution)) return;

		// Cell size in "world" units (1 unit per texel)
		static const float kCellSize = 1.0;
		// Pipe cross-section
		static const float kPipeArea = 1.0;
		// Time step
		static const float kDT = 0.02;
		// Gravity
		static const float kG = 9.8;

		// Neighbours: L, R, B, T
		int2 nL = clamp(id + int2(-1,  0), int2(0,0), gResolution - 1);
		int2 nR = clamp(id + int2( 1,  0), int2(0,0), gResolution - 1);
		int2 nB = clamp(id + int2( 0, -1), int2(0,0), gResolution - 1);
		int2 nT = clamp(id + int2( 0,  1), int2(0,0), gResolution - 1);

		float h  = gHeightmap[id];
		float w  = gWater[id];
		float s  = gSediment[id];
		float2 v = gFlowMap[id];

		uint phase = gIteration % 7;

		if (phase == 0)
		{
			// ---- Rainfall ----
			gWater[id] = w + gRainfallAmount * kDT;
		}
		else if (phase == 1)
		{
			// ---- Outflow flux (pipe model) ----
			// Total height (terrain + water) at each neighbour
			float hl  = gHeightmap[nL] + gWater[nL];
			float hr  = gHeightmap[nR] + gWater[nR];
			float hb  = gHeightmap[nB] + gWater[nB];
			float ht  = gHeightmap[nT] + gWater[nT];
			float hc  = h + w;

			// Old flux stored in gTempTex (pack 4 values; simplified to 2D velocity here)
			// For simplicity, use velocity components as proxy flux
			float fL = max(0.0, v.x + kDT * kPipeArea * kG * (hc - hl) / kCellSize);
			float fR = max(0.0, -v.x + kDT * kPipeArea * kG * (hc - hr) / kCellSize);
			float fB = max(0.0, v.y + kDT * kPipeArea * kG * (hc - hb) / kCellSize);
			float fT = max(0.0, -v.y + kDT * kPipeArea * kG * (hc - ht) / kCellSize);

			// Scale to conserve water
			float K = min(1.0, w * kCellSize * kCellSize / ((fL + fR + fB + fT) * kDT + 1e-6));
			gFlowMap[id] = float2(K * (fL - fR), K * (fB - fT));
		}
		else if (phase == 2)
		{
			// ---- Update water from flux divergence ----
			float2 vL = gFlowMap[nL];
			float2 vR = gFlowMap[nR];
			float2 vB = gFlowMap[nB];
			float2 vT = gFlowMap[nT];

			// Inflow to this cell from neighbours
			float inflow = max(0.0, -vL.x) + max(0.0, vR.x)
			             + max(0.0, -vB.y) + max(0.0, vT.y);
			// Outflow from this cell
			float outflow = max(0.0, v.x) + max(0.0, -v.x)
			              + max(0.0, v.y) + max(0.0, -v.y);

			float dW = kDT * (inflow - outflow) / (kCellSize * kCellSize);
			gWater[id] = max(0.0, w + dW);

			// Velocity from flux
			float2 vel;
			vel.x = 0.5 * ((max(0.0, -vL.x) - max(0.0, v.x)) + (max(0.0, vR.x) - max(0.0, -v.x)));
			vel.y = 0.5 * ((max(0.0, -vB.y) - max(0.0, v.y)) + (max(0.0, vT.y) - max(0.0, -v.y)));
			gTempTex[id] = length(vel); // store speed for erosion
		}
		else if (phase == 3)
		{
			// ---- Erosion and deposition ----
			float speed  = gTempTex[id];
			float slope  = abs(h - gHeightmap[nR]) + abs(h - gHeightmap[nT]);
			slope *= 0.5;

			float sedCapacity = gErosionCapacity * slope * speed;
			float eDep = max(0.0, s - sedCapacity) * gErosionDeposit * kDT;
			float eEro = max(0.0, sedCapacity - s) * gErosionRate   * kDT;

			if (w < gMinWaterToErode) { eDep = 0; eEro = 0; }

			gHeightmap[id] = h - eEro + eDep;
			gSediment[id]  = s + eEro - eDep;
		}
		else if (phase == 4)
		{
			// ---- Sediment transport (advection) ----
			// Simple semi-Lagrangian: backtrack along velocity
			float2 vel = gFlowMap[id];
			float2 prevUV = float2(id) - vel * kDT / kCellSize;
			int2   pi = clamp((int2)prevUV, int2(0,0), gResolution - 1);
			gSediment[id] = gSediment[pi];
		}
		else if (phase == 5)
		{
			// ---- Evaporation ----
			gWater[id] = w * (1.0 - gEvaporationRate * kDT);
		}
		else // phase == 6
		{
			// ---- Thermal erosion (talus) ----
			float maxDrop = kCellSize * tan(gTalusAngle);
			float dHL = h - gHeightmap[nL];
			float dHR = h - gHeightmap[nR];
			float dHB = h - gHeightmap[nB];
			float dHT = h - gHeightmap[nT];

			float overflow = 0.0;
			overflow += max(0.0, dHL - maxDrop);
			overflow += max(0.0, dHR - maxDrop);
			overflow += max(0.0, dHB - maxDrop);
			overflow += max(0.0, dHT - maxDrop);

			gHeightmap[id] = h - overflow * gThermalRate * 0.25;
		}
	}

	HLSLEnd
}
