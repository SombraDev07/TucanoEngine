#include "Platform/Input.h"
#include "Platform/Window.h"
#include "Renderer/Renderer.h"
#include "Renderer/Scene.h"
#include "Renderer/DevTexture.h"
#include "Runtime/DebugUI.h"
#include "Runtime/Screenshot.h"
#include "Terrain/Heightmap.h"
#include "Terrain/TerrainGenerator.h"
#include "Terrain/TerrainComponent.h"
#include "Terrain/HeightmapBrush.h"
#include "Physics/PhysicsWorld.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>

using namespace tucano;

static bool raycastHeightmap(const Camera& cam, float ndcX, float ndcY,
                             const terrain::Heightmap& hm, float& outX, float& outZ) {
	glm::mat4 invVP = glm::inverse(cam.viewProj());
	glm::vec4 nearP = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
	glm::vec4 farP  = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
	nearP /= nearP.w;
	farP  /= farP.w;

	glm::vec3 origin = glm::vec3(nearP);
	glm::vec3 dir = glm::normalize(glm::vec3(farP) - glm::vec3(nearP));
	float maxDist = glm::length(glm::vec3(farP) - glm::vec3(nearP));

	float ws = hm.worldSize();
	float step = 1.0f;
	float t = 0.0f;
	bool below = false;

	for (; t < maxDist; t += step) {
		glm::vec3 p = origin + dir * t;
		if (p.x < 0.0f || p.x >= ws || p.z < 0.0f || p.z >= ws) continue;
		float h = hm.sampleHeight(p.x, p.z);
		if (p.y < h) { below = true; break; }
	}

	if (!below) return false;

	float lo = t - step;
	float hi = t;
	for (int i = 0; i < 8; ++i) {
		float mid = (lo + hi) * 0.5f;
		glm::vec3 p = origin + dir * mid;
		float h = hm.sampleHeight(p.x, p.z);
		if (p.y < h) hi = mid;
		else lo = mid;
	}

	float hitT = (lo + hi) * 0.5f;
	glm::vec3 hitP = origin + dir * hitT;
	outX = hitP.x;
	outZ = hitP.z;
	return outX >= 0.0f && outX < ws && outZ >= 0.0f && outZ < ws;
}

static void projectToScreen(const Camera& cam, float wx, float wy, float wz,
                            float& sx, float& sy, float screenW, float screenH) {
	glm::vec4 clip = cam.viewProj() * glm::vec4(wx, wy, wz, 1.0f);
	if (glm::abs(clip.w) < 1e-6f) return;
	float ndcX = clip.x / clip.w;
	float ndcY = clip.y / clip.w;
	sx = (ndcX * 0.5f + 0.5f) * screenW;
	sy = (1.0f - (ndcY * 0.5f + 0.5f)) * screenH;
}

int main(int argc, char** argv) {
	(void)argc; (void)argv;

	try {
		Window window({1920, 1080, "Tucano — TerrainLab (sculpt tools)"});
		auto device = rhi::Device::create(true);
		auto swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), true);
		auto renderer = std::make_unique<Renderer>(*device, window.width(), window.height());
		auto physWorld = std::make_unique<physics::PhysicsWorld>(4096);

		{
			auto& s = renderer->settings();
			renderer->clouds().enabled = false;
			s.enableVSM = false;
			s.enableAsyncCompute = false;
			s.enableVoxelGI = false;
			s.enableRTShadows = false;
			s.enableRTReflections = false;
			s.enableOctahedralPointShadows = false;
			s.enableVisibilityBuffer = false;
			s.enableGpuMeshletCull = false;
			s.enableMeshShaders = false;
			s.enableMeshlets = false;
			s.enableHiZOcclusion = false;
			s.sky.timeOfDay = 0.38f;
			s.sky.fogDensity = 0.008f;
			s.sky.fogHeight = 80.0f;
			renderer->rain().enabled = false;
		}

		terrain::TerrainGenParams params;
		params.resolution = 512;
		params.worldSize = 1024.0f;
		params.octaves = 6;
		params.persistence = 0.5f;
		params.baseFrequency = 4.0f;
		params.baseAmplitude = 128.0f;
		params.baseHeight = 0.0f;
		params.seed = 42;

		auto hm = terrain::TerrainGenerator::generate(*device, params);

		auto mat = std::make_shared<Material>();
		mat->name = "Terrain";
		mat->baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f};
		mat->albedo = devtex::defaultFloor(*device);
		mat->normal = devtex::defaultNormal(*device);
		mat->metallicFactor = 0.0f;

		terrain::TerrainComponent terrainComp(*device, hm, mat);
		terrainComp.createPhysicsBody(*physWorld);

		terrain::BrushSystem brushes;
		brushes.setMaxUndoDepth(256);

		Input input(window.handle());
		DebugUI ui;
		ui.init(window, *device);

		Scene scene;
		scene.objects.push_back(terrainComp.createRenderObject());
		scene.addDirectional(glm::normalize(glm::vec3(-0.35f, -1.0f, -0.2f)), {1.0f, 0.96f, 0.85f}, 8.0f);
		scene.camera.setPerspective(glm::radians(65.0f), window.aspect(), 0.5f, 8000.0f);
		scene.camera.setPosition({512.0f, 80.0f, 400.0f});
		scene.camera.lookAt({512.0f, 20.0f, 612.0f});

		int brushToolIdx = 0;
		float brushRadius = 20.0f;
		float brushStrength = 15.0f;
		const char* toolNames[] = {"Raise", "Lower", "Smooth", "Flatten", "Noise"};
		bool sculptActive = false;
		float cursorX = 0, cursorZ = 0;
		bool cursorValid = false;

		device->setDeviceLostCallback([&]() {
			Camera cam = scene.camera;
			RendererSettings s = renderer->settings();
			ui.shutdown();
			renderer.reset();
			swapChain.reset();
			swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), true);
			renderer = std::make_unique<Renderer>(*device, window.width(), window.height());
			renderer->settings() = s;
			scene.camera = cam;
			ui.init(window, *device);
		});

		window.setResizeCallback([&](uint32_t w, uint32_t h) {
			if (!swapChain || !renderer) return;
			swapChain->resize(w, h);
			renderer->resize(w, h);
			scene.camera.setPerspective(glm::radians(65.0f), window.aspect(), 0.5f, 8000.0f);
		});

		int frame = 0;
		bool needsRegen = false;

		while (!window.shouldClose()) {
			window.pollEvents();
			input.beginFrame();
			ui.beginFrame();

			terrain::BrushTool currentTool = static_cast<terrain::BrushTool>(brushToolIdx);

			// ── Mouse raycast ──
			cursorValid = false;
			sculptActive = false;
			{
				double mx, my;
				glfwGetCursorPos(window.handle(), &mx, &my);
				float ndcX = float(mx) / float(window.width()) * 2.0f - 1.0f;
				float ndcY = 1.0f - float(my) / float(window.height()) * 2.0f;

				float hitX, hitZ;
				cursorValid = raycastHeightmap(scene.camera, ndcX, ndcY, *hm, hitX, hitZ);
				if (cursorValid) {
					cursorX = hitX;
					cursorZ = hitZ;

					bool blocked = ui.wantCaptureMouse() || ui.wantCaptureKeyboard();
					if (!blocked && (input.mouseDown(GLFW_MOUSE_BUTTON_LEFT) || input.mousePressed(GLFW_MOUSE_BUTTON_LEFT))) {
						sculptActive = true;
						brushes.applyStroke(*hm, hitX, hitZ, brushRadius, brushStrength, currentTool);
						needsRegen = true;
					}
				}
			}

			// ── Undo/Redo ──
			if (!ui.wantCaptureKeyboard()) {
				bool ctrl = input.keyDown(GLFW_KEY_LEFT_CONTROL) || input.keyDown(GLFW_KEY_RIGHT_CONTROL);
				if (ctrl && input.keyPressed(GLFW_KEY_Z)) {
					if (input.keyDown(GLFW_KEY_LEFT_SHIFT) || input.keyDown(GLFW_KEY_RIGHT_SHIFT)) {
						brushes.redo(*hm);
					} else {
						brushes.undo(*hm);
					}
					needsRegen = true;
				}
			}

			if (needsRegen) {
				hm->uploadToGPU(*device);
				terrainComp.regenerateMesh(*device);
				scene.objects[0].mesh = terrainComp.mesh();
				needsRegen = false;
			}

			// ── Fly camera ──
			if (!ui.wantCaptureKeyboard()) {
				float dx = 0, dy = 0;
				input.mouseDelta(dx, dy);
				glm::vec3 move(0);
				if (input.keyDown(GLFW_KEY_W)) move.z += 1;
				if (input.keyDown(GLFW_KEY_S)) move.z -= 1;
				if (input.keyDown(GLFW_KEY_A)) move.x -= 1;
				if (input.keyDown(GLFW_KEY_D)) move.x += 1;
				if (input.keyDown(GLFW_KEY_E)) move.y += 1;
				if (input.keyDown(GLFW_KEY_Q)) move.y -= 1;
				float speed = input.keyDown(GLFW_KEY_LEFT_SHIFT) ? 80.0f : 25.0f;
				bool look = input.mouseDown(GLFW_MOUSE_BUTTON_RIGHT) && !ui.wantCaptureMouse();
				scene.camera.fly(1.0f / 60.0f, move * speed, look ? dx * 0.0025f : 0.0f, look ? -dy * 0.0025f : 0.0f);

				if (input.keyPressed(GLFW_KEY_ESCAPE)) break;
				if (input.keyPressed(GLFW_KEY_F12)) {
					beginScreenshot(*device, *device->beginFrame(), swapChain->backBuffer());
				}
			}

			// ── ImGui brush panel ──
			ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
			ImGui::SetNextWindowSize(ImVec2(280, 240), ImGuiCond_Once);
			if (ImGui::Begin("Terrain Sculpt", nullptr,
			                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::Text("Brush Tool");
				ImGui::Combo("##tool", &brushToolIdx, toolNames, 5);
				ImGui::SliderFloat("Radius", &brushRadius, 2.0f, 100.0f, "%.0f");
				ImGui::SliderFloat("Strength", &brushStrength, 1.0f, 50.0f, "%.1f");
				ImGui::Separator();
				ImGui::Text("Undo: %zu | Redo: %zu", brushes.undoCount(), brushes.redoCount());
				ImGui::Text("Ctrl+Z=Undo  Ctrl+Shift+Z=Redo");
				if (sculptActive) {
					ImGui::TextColored(ImVec4(1, 1, 0, 1), "SCULPTING");
				}
				if (cursorValid) {
					ImGui::Text("Cursor: %.0f, %.0f", cursorX, cursorZ);
				}
			}
			ImGui::End();

			// ── Brush cursor overlay (world-space ring projected on terrain) ──
			if (cursorValid) {
				auto* dl = ImGui::GetForegroundDrawList();
				float cx = cursorX, cz = cursorZ;

				const int kSegments = 48;
				ImVec2 pts[kSegments];
				int validPts = 0;
				for (int i = 0; i < kSegments; ++i) {
					float angle = float(i) / float(kSegments) * 6.2831853f;
					float wx = cx + std::cos(angle) * brushRadius;
					float wz = cz + std::sin(angle) * brushRadius;
					float h = hm->sampleHeight(wx, wz);
					float sx, sy;
					projectToScreen(scene.camera, wx, h + 0.15f, wz, sx, sy, float(window.width()), float(window.height()));
					if (sx >= -100 && sx < float(window.width()) + 100 && sy >= -100 && sy < float(window.height()) + 100) {
						pts[validPts++] = ImVec2(sx, sy);
					}
				}
				if (validPts >= 3) {
					dl->AddConvexPolyFilled(pts, validPts, IM_COL32(255, 255, 255, 40));
					dl->AddPolyline(pts, validPts, IM_COL32(255, 255, 200, 180), ImDrawFlags_Closed, 2.0f);
				}

				float centerH = hm->sampleHeight(cx, cz);
				float csx, csy;
				projectToScreen(scene.camera, cx, centerH + 0.2f, cz, csx, csy, float(window.width()), float(window.height()));
				dl->AddCircleFilled(ImVec2(csx, csy), 4.0f, IM_COL32(255, 255, 180, 255));

				float innerR = brushRadius * 0.2f;
				ImVec2 innerPts[kSegments];
				int innerValid = 0;
				for (int i = 0; i < kSegments; ++i) {
					float angle = float(i) / float(kSegments) * 6.2831853f;
					float wx = cx + std::cos(angle) * innerR;
					float wz = cz + std::sin(angle) * innerR;
					float h = hm->sampleHeight(wx, wz);
					float sx, sy;
					projectToScreen(scene.camera, wx, h + 0.15f, wz, sx, sy, float(window.width()), float(window.height()));
					if (sx >= -100 && sx < float(window.width()) + 100 && sy >= -100 && sy < float(window.height()) + 100) {
						innerPts[innerValid++] = ImVec2(sx, sy);
					}
				}
				if (innerValid >= 3) {
					dl->AddPolyline(innerPts, innerValid, IM_COL32(255, 255, 200, 120), ImDrawFlags_Closed, 1.5f);
				}
			}

			float fps = renderer->lastFrameMs() > 0.0f ? 1000.0f / renderer->lastFrameMs() : 0.0f;
			ui.drawPerfHud(renderer->lastFrameMs(), renderer->drawCalls(), window.width(), window.height());
			ui.drawWeatherAndLights(renderer->rain(), renderer->clouds(), scene, renderer->settings());

			if (frame % 120 == 0) {
				window.setTitle("Tucano TerrainLab | " + std::to_string(int(fps)) +
				                " FPS | LMB=sculpt RMB=look Ctrl+Z=undo");
			}

			input.endFrame();
			physWorld->step(1.0f / 60.0f);

			auto* cmd = device->beginFrame();
			auto& bb = swapChain->backBuffer();
			renderer->render(cmd, bb, scene);
			ui.endFrame(*cmd, bb);

			cmd->transition(bb, rhi::ResourceState::Present);
			device->endFrame(*swapChain);
			++frame;
		}

		terrainComp.removePhysicsBody(*physWorld);
		device->waitIdle();
		ui.shutdown();
		return 0;
	} catch (const std::exception& ex) {
		std::cerr << "Fatal: " << ex.what() << "\n";
		return 1;
	}
}
