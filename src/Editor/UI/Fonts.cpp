#include "Editor/UI/Fonts.h"
#include "Editor/UI/Icons.h"

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace tucano::editor {
namespace {

constexpr size_t kSizeCount = static_cast<size_t>(FontSize::Count);
constexpr size_t kStyleCount = static_cast<size_t>(FontStyle::Count);
constexpr size_t kFontCount = static_cast<size_t>(Font::Count);

bool g_ready = false;
ImFont* g_fonts[kFontCount] = {};

// Sizes the editor is designed around. Tiny is for dense tables and status strips, Large for titles.
constexpr float kSizes[kSizeCount] = {12.0f, 14.0f, 16.0f, 20.0f};

constexpr const char* kStyleFiles[kStyleCount] = {
    "Roboto-Regular.ttf",
    "Roboto-Italic.ttf",
    "Roboto-Bold.ttf",
    "Roboto-BoldItalic.ttf",
};

// The glyph range handed to ImGui must outlive the atlas build, so it cannot be a local.
// One contiguous range covers the whole Material Design set.
const ImWchar kIconRange[] = {static_cast<ImWchar>(TUCANO_ICONRANGE_MIN),
                              static_cast<ImWchar>(TUCANO_ICONRANGE_MAX), 0};

// ImGui's default range stops at U+00FF, which drops the punctuation UI text actually uses — an
// em-dash or an ellipsis then renders as '?'. Latin-1 is kept for accented characters (the editor's
// own strings are Portuguese), plus the punctuation block.
const ImWchar kTextRange[] = {
    0x0020, 0x00FF, // Basic Latin + Latin-1 Supplement (á é ç ã õ ...)
    0x2010, 0x2027, // hyphens, en/em dash, quotes, ellipsis
    0x20A0, 0x20BF, // currency symbols
    0x2190, 0x21FF, // arrows
    0,
};

std::string assetPath(const char* file) {
	return std::string(TUCANO_ENGINE_ASSETS_DIR) + "/Fonts/" + file;
}

// Font enum is laid out size-major, style-minor — keep index math in one place.
constexpr size_t fontIndex(FontSize size, FontStyle style) {
	return static_cast<size_t>(size) * kStyleCount + static_cast<size_t>(style);
}

} // namespace

bool fontsReady() { return g_ready; }

float fontSize(FontSize s) {
	const auto i = static_cast<size_t>(s);
	return i < kSizeCount ? kSizes[i] : kSizes[static_cast<size_t>(FontSize::Medium)];
}

ImFont* font(Font f) {
	const auto i = static_cast<size_t>(f);
	if (i < kFontCount && g_fonts[i] != nullptr) {
		return g_fonts[i];
	}
	// Never hand back null: callers push fonts unconditionally, and a null push asserts inside ImGui.
	return ImGui::GetIO().FontDefault != nullptr ? ImGui::GetIO().FontDefault : ImGui::GetFont();
}

ScopedFont::ScopedFont(Font f) { ImGui::PushFont(font(f)); }
ScopedFont::~ScopedFont() { ImGui::PopFont(); }

bool buildFonts(float dpiScale) {
	if (ImGui::GetCurrentContext() == nullptr) {
		return false;
	}

	const std::string iconPath = assetPath("MaterialDesignIcons.ttf");
	std::error_code ec;
	bool haveAll = std::filesystem::exists(iconPath, ec);
	for (const char* file : kStyleFiles) {
		haveAll = haveAll && std::filesystem::exists(assetPath(file), ec);
	}
	if (!haveAll) {
		// Not fatal: ImGui falls back to its built-in font. Say so loudly, because the symptom
		// otherwise is "the editor looks wrong" with no explanation.
		std::printf("[editor] fontes nao encontradas em %s — usando a fonte padrao do ImGui\n",
		            TUCANO_ENGINE_ASSETS_DIR "/Fonts");
		return false;
	}

	ImGuiIO& io = ImGui::GetIO();
	const float scale = dpiScale > 0.0f ? dpiScale : 1.0f;

	for (size_t s = 0; s < kSizeCount; ++s) {
		const float px = kSizes[s] * scale;
		for (size_t st = 0; st < kStyleCount; ++st) {
			ImFontConfig textConfig;
			textConfig.SizePixels = px;

			ImFont* face =
			    io.Fonts->AddFontFromFileTTF(assetPath(kStyleFiles[st]).c_str(), px, &textConfig, kTextRange);
			if (face == nullptr) {
				std::printf("[editor] falha ao carregar %s\n", kStyleFiles[st]);
				return false;
			}

			// Merge the icons into *this* face, so an icon can appear inline in a label of any
			// size/weight without the caller switching fonts mid-string.
			ImFontConfig iconConfig;
			iconConfig.MergeMode = true;
			iconConfig.SizePixels = px;
			// Icon glyphs sit on a different baseline than the text ones; without the nudge they ride
			// high next to a label. Scales with the face so it stays proportional.
			iconConfig.GlyphOffset = ImVec2(0.0f, px * 0.125f);
			if (io.Fonts->AddFontFromFileTTF(iconPath.c_str(), 0.0f, &iconConfig, kIconRange) == nullptr) {
				std::printf("[editor] falha ao fundir os icones em %s\n", kStyleFiles[st]);
				return false;
			}

			g_fonts[fontIndex(static_cast<FontSize>(s), static_cast<FontStyle>(st))] = face;
		}
	}

	io.FontDefault = font(Font::Default);
	g_ready = true;
	return true;
}

} // namespace tucano::editor
