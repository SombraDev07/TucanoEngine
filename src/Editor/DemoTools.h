#pragma once

#include <memory>

// Two throwaway tools that exercise the EditorTool contract end to end.
//
// They exist because the per-tool dockspace only proves itself with more than one tool open: two
// instances arranging their windows differently, keeping those arrangements across a tab switch,
// and not stealing each other's panels. A single tool would look identical to the old flat model.
//
// Replaced by the real Scene and Material tools; until then this is what `--tools-demo` shows and
// what the checkpoint screenshots.

namespace tucano::editor {

class EditorTool;

// Scene-shaped: outliner on the left, inspector on the right, console along the bottom.
std::unique_ptr<EditorTool> makeSceneDemoTool();

// Material-shaped, and deliberately a different arrangement: preview over properties on one side,
// node graph filling the rest. `documentPath` gives the instance its own identity and layout.
std::unique_ptr<EditorTool> makeMaterialDemoTool(const char* documentPath);

} // namespace tucano::editor
