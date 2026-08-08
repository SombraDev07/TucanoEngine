#pragma once

#include "Editor/EditorTool.h"

#include <memory>

// SceneTool — the editor's level-editing workspace, and the home of the panels that used to float
// loose in the shell.
//
// P2-06 of the roadmap: the Outliner, Inspector, Content Browser, Console, Environment, Tools,
// Animation and Stats panels were top-level ImGui windows sharing one global dockspace. As tool
// windows they now live inside this tool's own dockspace, which means their arrangement is saved
// with the tool, they cannot be dragged out into an unrelated workspace, and opening a different
// tool swaps the whole set out instead of piling on top of it.
//
// The panels themselves were not rewritten — they work. This is a change of ownership, not of
// content: each ToolWindow body calls straight into the existing panel's draw(ctx).

namespace tucano {
class Scene;
class Renderer;
class Camera;
struct RendererSettings;
} // namespace tucano

namespace tucano::editor {

struct EditorContext;

class SceneTool final : public EditorTool {
public:
	SceneTool();
	~SceneTool() override;

	const char* toolTypeName() const override { return "Scene"; }
	const char* icon() const override;

	void onInitialize() override;
	void setupDefaultLayout(uint32_t dockspaceId, float width, float height) override;

	// The tool draws whatever this points at; the host owns it and refreshes the per-frame stats.
	// Null is tolerated — the panels say "no scene" rather than crashing.
	void setContext(EditorContext* context) { m_context = context; }
	EditorContext* context() const { return m_context; }

private:
	void drawInspector(EditorContext& context);
	void drawWater(EditorContext& context);
	void drawFog(EditorContext& context);

	struct Panels;
	std::unique_ptr<Panels> m_panels;
	EditorContext* m_context = nullptr;
};

} // namespace tucano::editor
