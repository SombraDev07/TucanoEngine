#pragma once

#include <string>
#include <vector>

// Native OS dialogs — file open/save, folder pick, message box.
//
// Derived from Esoterica (MIT) — Code/EngineTools/Core/SystemDialogs.{h,cpp}
//
// Deliberately not an ImGui file browser. People already know their own file picker: it has their
// recent places, their network drives, their search, and it types paths the way they expect. An
// in-engine browser is a worse copy that has to be maintained forever.
//
// These calls **block** until the user answers — that is what a native modal is. Nothing else in the
// editor renders meanwhile, which is fine for a deliberate File menu action and wrong for anything
// on a hot path.
//
//   const std::string path = openFileDialog("Open Scene", {{"Scenes", "*.scn;*.gltf"}});
//   if (!path.empty()) { load(path); }

namespace tucano::editor {

struct FileFilter {
	const char* label;   // "Scenes"
	const char* pattern; // "*.scn;*.gltf"
};

// Empty string means the user cancelled — the only failure a caller needs to distinguish.
std::string openFileDialog(const char* title, const std::vector<FileFilter>& filters = {},
                           const std::string& startDirectory = {});

// Empty vector means cancelled.
std::vector<std::string> openFilesDialog(const char* title, const std::vector<FileFilter>& filters = {},
                                         const std::string& startDirectory = {});

// The extension of the first filter is appended when the user types a bare name.
std::string saveFileDialog(const char* title, const std::vector<FileFilter>& filters = {},
                           const std::string& defaultName = {}, const std::string& startDirectory = {});

std::string pickFolderDialog(const char* title, const std::string& startDirectory = {});

// Blocking OS message box. For things that must be seen even if the editor's own UI is broken —
// a failed startup, a lost device. Ordinary messages belong in a toast or a DialogManager modal.
void showErrorBox(const char* title, const char* message);

} // namespace tucano::editor
