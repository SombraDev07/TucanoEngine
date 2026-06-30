//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
//
// Fase 1: Implementação mínima de TerrainErosion que compila.
// A erosão procedural GPU é uma feature de Fase posterior (roadmap Fase 6).
// Mantemos a API do header intacta para compatibilidade futura.
#include "Terrain/B3DTerrainErosion.h"
#include "Terrain/B3DHeightmapData.h"
#include "Debug/B3DDebug.h"

namespace b3d
{
	B3D_LOG_CATEGORY_STATIC(LogTerrain, Log)

	bool TerrainErosion::Init(const Settings& /*settings*/, const HeightmapData& /*heightmap*/)
	{
		B3D_LOG(Log, LogTerrain,
			"TerrainErosion: GPU erosion is not yet implemented (Fase 6). Returning false.");
		return false;
	}

	void TerrainErosion::Close() { }

	void TerrainErosion::Run(render::GpuCommandBuffer& /*commandBuffer*/) { }

	bool TerrainErosion::DownloadResult(HeightmapData& /*outHeightmap*/) { return false; }
}