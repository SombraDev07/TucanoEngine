#include "Common/SkyScene.h"
#include "Editor/EditorShell.h"
#include "Editor/EditorContext.h"
#include "Editor/OutlinerPanel.h"
#include "Editor/InspectorPanel.h"
#include "Editor/ContentBrowser.h"
#include "Editor/ConsolePanel.h"
#include "Editor/EnvironmentPanel.h"
#include "Editor/ToolsPanel.h"
#include "Editor/AnimationPanel.h"
#include "Editor/StatsPanel.h"
#include "Platform/Input.h"
#include "Platform/Window.h"
#include "Renderer/Renderer.h"
#include "Runtime/DebugUI.h"
#include "Runtime/Screenshot.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <algorithm>

using namespace tucano;

// Simple ray-AABB intersection for object picking.
static int pickObject(const Scene& scene, const glm::vec3& rayO, const glm::vec3& rayD) {
	int best = -1;
	float bestT = 1e30f;
	for (size_t i = 0; i < scene.objects.size(); ++i) {
		const auto& obj = scene.objects[i];
		if (!obj.visible || !obj.mesh) continue;

		// Use a simple bounding sphere around the object's world position.
		const glm::vec3 center = obj.transform.translation;
		const float radius = 1.5f; // approximate

		const glm::vec3 oc = rayO - center;
		const float b = glm::dot(oc, rayD);
		const float c = glm::dot(oc, oc) - radius * radius;
		const float disc = b * b - c;
		if (disc < 0.0f) continue;

		float t = -b - std::sqrt(disc);
		if (t < 0.0f) t = -b + std::sqrt(disc);
		if (t > 0.0f && t < bestT) {
			bestT = t;
			best = static_cast<int>(i);
		}
	}
	return best;
}

int main(int argc, char** argv) {
	std::string screenshotPath;
	double fpsTestSeconds = 0.0;
	int maxFrames = -1;
	bool forceVsync = true;
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--screenshot" && i + 1 < argc) screenshotPath = argv[++i];
		else if (a == "--fpstest" && i + 1 < argc) fpsTestSeconds = std::stod(argv[++i]);
		else if (a == "--frames" && i + 1 < argc) maxFrames = std::stoi(argv[++i]);
		else if (a == "--novsync") forceVsync = false;
	}

	try {
		Window window({1920, 1080, "Tucano Editor"});
		auto device = rhi::Device::create(true);
		auto swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), forceVsync);
		auto renderer = std::make_unique<Renderer>(*device, window.width(), window.height());
		skylab::configureCleanRenderer(*renderer);

		Input input(window.handle());
		DebugUI ui;
		ui.init(window, *device);

		Scene scene;
		skylab::buildCleanScene(*device, scene);
		scene.camera.setPerspective(glm::radians(65.0f), window.aspect(), 0.2f, 4000.0f);

		// ── Editor ──
		editor::EditorShell shell;
		editor::EditorContext ctx;
		ctx.scene = &scene;
		ctx.renderer = renderer.get();
		ctx.settings = &renderer->settings();
		ctx.camera = &scene.camera;
		ctx.logInfo("Tucano Editor started.");
		ctx.logInfo("Renderer: Direct3D 12, " + std::to_string(window.width()) + "x" + std::to_string(window.height()));

		editor::OutlinerPanel outliner;
		editor::InspectorPanel inspector;
		editor::ContentBrowser contentBrowser;
		editor::ConsolePanel console;
	editor::EnvironmentPanel environment;
	editor::ToolsPanel tools;
	editor::StatsPanel stats;
	// editor::AnimationPanel animation;  // FIXME: crash on startup

		shell.onNewScene = [&]() { ctx.logInfo("New Scene (not implemented)."); };
		shell.onOpenScene = [&]() { ctx.logInfo("Open Scene (not implemented)."); };
		shell.onSaveScene = [&]() { ctx.logInfo("Save Scene (not implemented)."); };
		shell.onImportAsset = [&]() { ctx.logInfo("Import Asset (not implemented)."); };

		// ── TDR recovery ──
		device->setDeviceLostCallback([&]() {
			const Camera cam = scene.camera;
			const RendererSettings settings = renderer->settings();
			ui.shutdown();
			renderer.reset();
			swapChain.reset();
			swapChain = device->createSwapChain(window.nativeHandle(), window.width(), window.height(), forceVsync);
			renderer = std::make_unique<Renderer>(*device, window.width(), window.height());
			renderer->settings() = settings;
			skylab::buildCleanScene(*device, scene);
			scene.camera = cam;
			ctx.renderer = renderer.get();
			ctx.settings = &renderer->settings();
			ui.init(window, *device);
			ctx.logWarn("Device lost — recovered.");
		});

		window.setResizeCallback([&](uint32_t w, uint32_t h) {
			if (!swapChain || !renderer) return;
			swapChain->resize(w, h);
			renderer->resize(w, h);
			ctx.viewportW = w;
			ctx.viewportH = h;
			scene.camera.setPerspective(glm::radians(65.0f), window.aspect(), 0.2f, 4000.0f);
		});

		shell.init("EditorLayout.ini");

		int frame = 0;
		bool shotDone = screenshotPath.empty();
		std::vector<double> frameSamples;
		std::chrono::steady_clock::time_point lastFrameStart{};

		while (!window.shouldClose() && !shell.quitRequested()) {
			const auto frameStart = std::chrono::steady_clock::now();
			if (fpsTestSeconds > 0.0 && lastFrameStart.time_since_epoch().count() != 0) {
				frameSamples.push_back(std::chrono::duration<double, std::milli>(frameStart - lastFrameStart).count());
			}
			lastFrameStart = frameStart;

			window.pollEvents();
			input.beginFrame();
			ui.beginFrame();

			// ── Editor UI ──
			shell.beginFrame();

			if (shell.isVisible(editor::Panel::Outliner)) {
				shell.panel(editor::Panel::Outliner, [&]() { outliner.draw(ctx); });
			}
			if (shell.isVisible(editor::Panel::Inspector)) {
				shell.panel(editor::Panel::Inspector, [&]() { inspector.draw(ctx); });
			}
			if (shell.isVisible(editor::Panel::ContentBrowser)) {
				shell.panel(editor::Panel::ContentBrowser, [&]() { contentBrowser.draw(ctx); });
			}
			if (shell.isVisible(editor::Panel::Console)) {
				shell.panel(editor::Panel::Console, [&]() { console.draw(ctx); });
			}
			if (shell.isVisible(editor::Panel::Environment)) {
				shell.panel(editor::Panel::Environment, [&]() { environment.draw(ctx); });
			}
			if (shell.isVisible(editor::Panel::Tools)) {
				shell.panel(editor::Panel::Tools, [&]() { tools.draw(ctx); });
			}
			if (shell.isVisible(editor::Panel::Stats)) {
				shell.panel(editor::Panel::Stats, [&]() { stats.draw(ctx); });
			}
			// Animation panel (disabled - crash fix pending)
			/*
			{
				ImGui::SetNextWindowSize(ImVec2(360, 400), ImGuiCond_FirstUseEver);
				if (ImGui::Begin("Animation", nullptr)) {
					animation.draw(ctx);
				}
				ImGui::End();
			}
			*/

			shell.endFrame();

			// ── Viewport object picking ──
			if (!ImGui::GetIO().WantCaptureMouse && !ui.gizmoHovered()) {
				if (input.mousePressed(GLFW_MOUSE_BUTTON_LEFT)) {
					float mx = 0, my = 0;
					input.mousePosition(mx, my);
					glm::vec3 rayO, rayD;
					scene.camera.screenToWorldRay(mx, my,
					                              static_cast<float>(ctx.viewportW),
					                              static_cast<float>(ctx.viewportH), rayO, rayD);
					int picked = pickObject(scene, rayO, rayD);
					if (picked >= 0) {
						ctx.selectedObject = picked;
						ctx.logInfo("Selected: " + scene.objects[picked].name);
					} else {
						ctx.selectedObject = -1;
					}
				}
			}

			// ── Transform gizmo on selected object ──
			if (ctx.selectedObject >= 0 && static_cast<size_t>(ctx.selectedObject) < scene.objects.size()) {
				auto& obj = scene.objects[ctx.selectedObject];
				glm::mat4 model = obj.transform.matrix();
				if (ui.drawTransformGizmo(scene.camera.view(), scene.camera.proj(), model,
				                          DebugUI::GizmoOp::Translate, true, 0.0f,
				                          ctx.viewportW, ctx.viewportH)) {
					// Gizmo is being dragged - update transform
					obj.transform.translation = glm::vec3(model[3]);
				}
			}

			// Status bar
			{
				const float fps = renderer->lastFrameMs() > 0 ? 1000.0f / renderer->lastFrameMs() : 0;
				char buf[128];
				snprintf(buf, sizeof(buf), "%.0f FPS | %u draws | %ux%u",
				         fps, renderer->drawCalls(), ctx.viewportW, ctx.viewportH);
				shell.setStatus(buf);
			}

			// ── 3D viewport controls ──
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
				const float speed = input.keyDown(GLFW_KEY_LEFT_SHIFT) ? 40.0f : 12.0f;
				const bool look = input.mouseDown(GLFW_MOUSE_BUTTON_RIGHT) && !ui.wantCaptureMouse();
				scene.camera.fly(1.0f / 60.0f, move * speed,
				                 look ? dx * 0.0025f : 0.0f,
				                 look ? -dy * 0.0025f : 0.0f);
			}

			if (!ui.wantCaptureKeyboard() && input.keyPressed(GLFW_KEY_F12)) {
				screenshotPath = screenshotPath.empty() ? "editor_capture.png" : screenshotPath;
				shotDone = false;
				ctx.logInfo("Screenshot: " + screenshotPath);
			}

			input.endFrame();

			// ── Render ──
			auto* cmd = device->beginFrame();
			auto& bb = swapChain->backBuffer();
			renderer->render(cmd, bb, scene);
			ui.endFrame(*cmd, bb);

			// Screenshot
			ScreenshotPending shot;
			if (!shotDone && frame >= 5) {
				shot = beginScreenshot(*device, *cmd, bb);
			}
			cmd->transition(bb, rhi::ResourceState::Present);
			device->endFrame(*swapChain);
			if (shot.impl) {
				device->waitIdle();
				finalizeScreenshot(shot, screenshotPath);
				shotDone = true;
				std::cout << "Saved " << screenshotPath << "\n";
			}

			++frame;
			if (maxFrames >= 0 && frame >= maxFrames) break;
			if (fpsTestSeconds > 0.0 && frameSamples.size() > 8) {
				double total = 0.0;
				for (double v : frameSamples) total += v;
				if (total >= fpsTestSeconds * 1000.0) break;
			}
		}

		if (fpsTestSeconds > 0.0 && !frameSamples.empty()) {
			std::sort(frameSamples.begin(), frameSamples.end());
			auto pct = [&](double p) { return frameSamples[std::min(frameSamples.size() - 1, size_t(p * frameSamples.size()))]; };
			double mean = 0;
			for (double v : frameSamples) mean += v;
			mean /= frameSamples.size();
			std::cout << "fpstest: frames=" << frameSamples.size() << " mean=" << mean
			          << "ms (" << (1000.0 / mean) << " FPS) median=" << pct(0.50)
			          << "ms p95=" << pct(0.95) << "ms\n";
		}

		device->waitIdle();
		shell.shutdown();
		ui.shutdown();
		return 0;
	} catch (const std::exception& ex) {
		std::cerr << "Fatal: " << ex.what() << "\n";
		return 1;
	}
}
