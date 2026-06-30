//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
//
// Terrain.exe — exemplo de terreno AAA para a Tucano Engine / B3DFramework.
//
// Fase 1: valida a fundação do sistema de terrain (HeightmapData, TerrainLodGrid,
// culling, queries) instanciando TerrainSystem, e renderiza o terreno visualmente
// via um mesh CPU gerado a partir do heightmap procedural, usando um Material PBR
// padrão com a textura de chão GridPattern. O céu usa um cubemap HDRI estático
// (mesmo approach do Physic.exe — sem compute passes por frame, sem lag) e a
// iluminação direcional permanece funcionando. Câmera fly (CameraFlyer).

#include "B3DApplication.h"
#include "B3DEntry.h"
#include "Resources/B3DResources.h"
#include "Resources/B3DBuiltinResources.h"
#include "Material/B3DMaterial.h"
#include "Components/B3DCamera.h"
#include "Components/B3DRenderable.h"
#include "Components/B3DLight.h"
#include "Components/B3DSkybox.h"
#include "GUI/B3DGUIWidget.h"
#include "GUI/B3DGUIPanel.h"
#include "GUI/B3DGUILayoutY.h"
#include "GUI/B3DGUILabel.h"
#include "GpuBackend/B3DRenderWindow.h"
#include "Scene/B3DSceneObject.h"
#include "Mesh/B3DMesh.h"
#include "Mesh/B3DMeshData.h"
#include "GpuBackend/B3DVertexDescription.h"
#include "Math/B3DMath.h"
#include "Platform/B3DCursor.h"
#include "Input/B3DInput.h"
#include "Terrain/B3DTerrainSystem.h"
#include "Terrain/B3DHeightmapData.h"

#include "B3DExampleFramework.h"
#include "B3DCameraFlyer.h"

#include <cmath>

namespace b3d
{
	constexpr float GROUND_PLANE_SCALE = 50.0f;

	constexpr uint32_t kTerrainDim = 129;
	constexpr float    kTerrainWorldSize = 1000.0f;
	constexpr float    kTerrainHeightScale = 100.0f;
	constexpr float    kTerrainCellSize = kTerrainWorldSize / (float)(kTerrainDim - 1);

	u32 kWindowWidth = 1280;
	u32 kWindowHeight = 720;

	static TArray<uint16_t> GenerateProceduralHeightmap()
	{
		TArray<uint16_t> data;
		data.Resize(kTerrainDim * kTerrainDim);

		for (uint32_t z = 0; z < kTerrainDim; ++z)
		{
			for (uint32_t x = 0; x < kTerrainDim; ++x)
			{
				const float fx = (float)x / (float)(kTerrainDim - 1);
				const float fz = (float)z / (float)(kTerrainDim - 1);

				float h = std::sin(fx * 6.28318f * 1.5f) * std::cos(fz * 6.28318f * 1.5f);
				h += 0.4f * std::sin(fx * 6.28318f * 3.0f + 0.5f);
				h += 0.3f * std::cos(fz * 6.28318f * 4.0f - 1.0f);

				const float dx = fx - 0.5f, dz = fz - 0.5f;
				const float r2 = dx * dx + dz * dz;
				h += 0.6f * std::exp(-r2 * 10.0f);

				const float height01 = 0.4f + 0.2f * h;
				data[z * kTerrainDim + x] = (uint16_t)(Math::Clamp01(height01) * 65535.0f);
			}
		}
		return data;
	}

	static HMesh BuildTerrainMesh(const HeightmapData& heightmap)
	{
		const uint32_t dim = heightmap.GetWidthSamples();
		const uint32_t vertexCount = dim * dim;
		const uint32_t indexCount = (dim - 1) * (dim - 1) * 6;

		Vector<VertexElement> elements =
		{
			VertexElement(VET_FLOAT3, VES_POSITION, 0, 0, 0, 0),
			VertexElement(VET_FLOAT3, VES_NORMAL,   0, 0, 0, 12),
			VertexElement(VET_FLOAT4, VES_TANGENT,  0, 0, 0, 24),
			VertexElement(VET_FLOAT2, VES_TEXCOORD, 0, 0, 0, 40),
		};
		auto vertexDesc = B3DMakeShared<VertexDescription>(elements);

		TShared<MeshData> meshData = B3DMakeShared<MeshData>(vertexCount, indexCount, vertexDesc, IT_32BIT);

		auto posIter = meshData->GetVec3DataIter(VES_POSITION);
		const float cellSize = heightmap.GetCellSize();
		const Vector2 worldOffset = heightmap.GetWorldOffset();
		for (uint32_t z = 0; z < dim; ++z)
		{
			for (uint32_t x = 0; x < dim; ++x)
			{
				const float worldX = worldOffset.X + (float)x * cellSize;
				const float worldZ = worldOffset.Y + (float)z * cellSize;
				const float worldY = heightmap.GetHeightAtWorld(worldX, worldZ);
				posIter.AddValue(Vector3(worldX, worldY, worldZ));
			}
		}

		auto normIter = meshData->GetVec3DataIter(VES_NORMAL);
		for (uint32_t z = 0; z < dim; ++z)
		{
			for (uint32_t x = 0; x < dim; ++x)
			{
				const float worldX = worldOffset.X + (float)x * cellSize;
				const float worldZ = worldOffset.Y + (float)z * cellSize;

				float h;
				Vector3 n;
				if (heightmap.GetHeightAndNormal(Vector2(worldX, worldZ), h, n))
					normIter.AddValue(n);
				else
					normIter.AddValue(Vector3::kUnitY);
			}
		}

		auto tanIter = meshData->GetVec4DataIter(VES_TANGENT);
		for (uint32_t i = 0; i < vertexCount; ++i)
			tanIter.AddValue(Vector4(1.0f, 0.0f, 0.0f, 1.0f));

		auto uvIter = meshData->GetVec2DataIter(VES_TEXCOORD);
		const float uvScale = kTerrainWorldSize * 0.5f;
		for (uint32_t z = 0; z < dim; ++z)
		{
			for (uint32_t x = 0; x < dim; ++x)
			{
				const float u = (float)x * cellSize / uvScale;
				const float v = (float)z * cellSize / uvScale;
				uvIter.AddValue(Vector2(u, v));
			}
		}

		// Indices — two triangles per quad. Winding CW from above (tl, tr, br / tl, br, bl)
		// so the front face points +Y (up) and is visible from above with CULL_COUNTERCLOCKWISE.
		uint32_t* indices = meshData->GetIndices32();
		uint32_t idx = 0;
		for (uint32_t z = 0; z < dim - 1; ++z)
		{
			for (uint32_t x = 0; x < dim - 1; ++x)
			{
				const uint32_t tl = z * dim + x;
				const uint32_t tr = tl + 1;
				const uint32_t bl = tl + dim;
				const uint32_t br = bl + 1;

				indices[idx++] = tl; indices[idx++] = tr; indices[idx++] = br;
				indices[idx++] = tl; indices[idx++] = br; indices[idx++] = bl;
			}
		}

		return Mesh::Create(meshData, MeshFlag::Static, DOT_TRIANGLE_LIST);
	}

	void SetUpScene()
	{
		TArray<uint16_t> heightmapRaw = GenerateProceduralHeightmap();

		TerrainSystem::StartUp();
		TerrainSystem::Settings terrainSettings;
		terrainSettings.mHeightmapPath = "";
		terrainSettings.mCellSizeMeters = kTerrainCellSize;
		terrainSettings.mHeightMin = 0.0f;
		terrainSettings.mHeightScale = kTerrainHeightScale;
		terrainSettings.mWorldOffset = Vector2(-kTerrainWorldSize * 0.5f, -kTerrainWorldSize * 0.5f);
		TerrainSystem::Instance().Init(terrainSettings);

		HeightmapData heightmap;
		heightmap.Init(heightmapRaw.Data(), kTerrainDim, kTerrainDim,
			kTerrainCellSize, 0.0f, kTerrainHeightScale, terrainSettings.mWorldOffset);

		HTexture groundAlbedo = ExampleFramework::LoadTexture(ExampleTexture::GridPattern2, ExampleTextureType::Default);
		HShader shader = GetBuiltinResources().GetBuiltinShader(BuiltinShader::Standard);

		HMaterial terrainMaterial = Material::Create(shader);
		terrainMaterial->SetTexture("gAlbedoTex", groundAlbedo);
		terrainMaterial->SetVec2("gUVTile", Vector2::kOne * kTerrainWorldSize * 0.5f);

		HMesh terrainMesh = BuildTerrainMesh(heightmap);

		HSceneObject terrainSO = SceneObject::Create("Terrain");
		HRenderable terrainRenderable = terrainSO->AddComponent<Renderable>();
		terrainRenderable->SetMesh(terrainMesh);
		terrainRenderable->SetMaterial(terrainMaterial);

		HSceneObject sunSO = SceneObject::Create("Sun");
		sunSO->SetRotation(Quaternion(Degree(-45.0f), Degree(30.0f), Degree(0.0f)));

		HLight sunLight = sunSO->AddComponent<Light>();
		sunLight->SetType(LightType::Directional);
		sunLight->SetColor(Color(1.0f, 0.95f, 0.85f, 1.0f));
		sunLight->SetIntensity(5.0f);
		sunLight->SetCastsShadow(true);

		// Câmera fly — WASD para mover, botão direito do mouse para olhar
		HSceneObject sceneCameraSO = SceneObject::Create("SceneCamera");
		TShared<RenderWindow> window = GetApplication().GetPrimaryWindow();

		HCamera sceneCamera = sceneCameraSO->AddComponent<Camera>();
		sceneCamera->GetViewport()->SetTarget(window);
		sceneCamera->SetNearClipDistance(0.5f);
		sceneCamera->SetFarClipDistance(5000.0f);
		sceneCamera->SetAspectRatio(kWindowWidth / (float)kWindowHeight);

		sceneCameraSO->AddComponent<CameraFlyer>();

		// Câmera perto do terreno, olhando para baixo
		sceneCameraSO->SetPosition(Vector3(0.0f, 50.0f, 80.0f));
		sceneCameraSO->SetRotation(Quaternion(Degree(-25.0f), Degree(0.0f), Degree(0.0f)));

		HTexture skyCubemap = ExampleFramework::LoadTexture(ExampleTexture::EnvironmentDaytime, ExampleTextureType::HDRI);

		HSceneObject skyboxSO = SceneObject::Create("Skybox");
		HSkybox skybox = skyboxSO->AddComponent<Skybox>();
		skybox->SetTexture(skyCubemap);

		// Cursor visivel — CameraFlyer gerencia hide/show ao segurar botão direito
		GetInput().OnButtonUp.Connect([=](const ButtonEvent& ev)
		{
			if (ev.ButtonCode == ButtonCode::Escape)
				GetApplication().NotifyQuitRequested();
		});

		HSceneObject guiSO = SceneObject::Create("GUI");
		HGUIWidget gui = guiSO->AddComponent<GUIWidget>(sceneCamera);
		GUIPanel* mainPanel = gui->GetPanel();
		GUILayoutY* vertLayout = GUILayoutY::Create();

		HString titleString(u8"Tucano Engine - Terrain AAA (Fase 1)");
		HString infoString(u8"Heightmap procedural 129x129, LOD grid + culling validados");
		HString controlsString(u8"WASD para mover, Botão Direito do Mouse para olhar, ESC para sair");

		vertLayout->AddNewElement<GUILabel>(titleString);
		vertLayout->AddNewElement<GUILabel>(infoString);
		vertLayout->AddNewElement<GUILabel>(controlsString);

		mainPanel->AddElement(vertLayout);
	}
} // namespace b3d

int B3DMain()
{
	using namespace b3d;

	VideoMode videoMode(kWindowWidth, kWindowHeight);
	Application::StartUp(videoMode, "Tucano Terrain", false);

	ExampleFramework::LoadPackages();
	ExampleFramework::SetupInputConfig();

	SetUpScene();

	Application::Instance().RunMainLoop();

	if (TerrainSystem::IsStarted())
	{
		TerrainSystem::Instance().Shutdown();
		TerrainSystem::ShutDown();
	}

	Application::ShutDown();
	return 0;
}