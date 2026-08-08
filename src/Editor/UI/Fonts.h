#pragma once

#include <cstdint>

// Editor font atlas — Roboto in four sizes and four weights, with Material Design icon glyphs
// merged into every one of them.
//
// Derived from Esoterica (MIT) — Code/Base/Imgui/ImguiFont.h + ImguiSystem.cpp
//
// Why a matrix instead of one font: a single size reads as a debug overlay, not a tool. Panel
// headers want Bold, a hint under a field wants Small, a property table wants Medium, a title wants
// Large. Every combination carries the icons, so an icon can sit inline in any label at any size:
//
//   ImGui::PushFont(editor::font(editor::Font::SmallBold));
//   ImGui::Text(TUCANO_ICON_ALERT " %d warnings", count);
//   ImGui::PopFont();
//
// or, scoped:
//
//   { editor::ScopedFont f(editor::Font::LargeBold); ImGui::TextUnformatted("Inspector"); }
//
// The icon codepoints run past U+FFFF, so the build defines IMGUI_USE_WCHAR32. Without it ImGui's
// glyph index is 16-bit, most of the table is unreachable, and icons render as nothing at all —
// silently, which is why it is called out here and in Icons.h.

struct ImFont;

namespace tucano::editor {

enum class FontSize : uint8_t { Tiny = 0, Small, Medium, Large, Count };
enum class FontStyle : uint8_t { Regular = 0, Italic, Bold, BoldItalic, Count };

// Every size/style permutation, so callers name one thing instead of two.
enum class Font : uint8_t {
	Tiny, TinyItalic, TinyBold, TinyBoldItalic,
	Small, SmallItalic, SmallBold, SmallBoldItalic,
	Medium, MediumItalic, MediumBold, MediumBoldItalic,
	Large, LargeItalic, LargeBold, LargeBoldItalic,
	Count,
	Default = Medium,
};

// Builds the atlas. Call after ImGui::CreateContext() and before the renderer backend uploads its
// font texture. Returns false if the font assets are missing, in which case ImGui keeps its built-in
// font and the editor still runs — just without icons or weights.
//
// `dpiScale` multiplies the pixel size every face is rasterised at.
bool buildFonts(float dpiScale = 1.0f);

// True once buildFonts() has succeeded — lets UI code decide whether an icon will actually draw.
bool fontsReady();

// Never null once the context exists: falls back to ImGui's default font, so callers can push
// unconditionally instead of guarding every call site.
ImFont* font(Font f);

// Pixel size a face was rasterised at (before DPI scaling), for laying out around text.
float fontSize(FontSize s);

// PushFont/PopFont as a scope guard — the manual pair is easy to leak on an early return.
class ScopedFont {
public:
	explicit ScopedFont(Font f);
	~ScopedFont();
	ScopedFont(const ScopedFont&) = delete;
	ScopedFont& operator=(const ScopedFont&) = delete;
};

} // namespace tucano::editor
