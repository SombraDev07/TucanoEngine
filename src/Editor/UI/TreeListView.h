#pragma once

#include "Editor/UI/Style.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Hierarchical list with selection, filtering and inline rename — the widget the Outliner and the
// Content Browser are both built on.
//
// Derived from Esoterica (MIT) — Code/EngineTools/Widgets/TreeListView.{h,cpp}
//
// Esoterica models an item as a class you subclass, overriding a dozen virtuals. Tucano models it
// as data plus callbacks: the tree is rebuilt from whatever owns the real state (the scene, the
// asset registry), so there is no second hierarchy to keep in sync — the classic source of "the
// outliner disagrees with the world".
//
//   TreeListView tree;
//   tree.onActivated = [&](uint64_t id) { focusCamera(id); };
//   tree.setRoot(buildItemsFromScene(scene));   // rebuild when the scene changes
//   tree.draw(&filter);                          // every frame
//
// Selection, expansion and the rename in progress survive setRoot(): they are keyed by id, so
// rebuilding the tree does not throw away what the user was doing.

namespace tucano::editor::ui {

class Filter;

class TreeListView {
public:
	struct Item {
		// Stable across rebuilds — selection and expansion are keyed by it. An entity id or an asset
		// GUID; 0 is reserved for "none".
		uint64_t id = 0;
		std::string label;
		const char* icon = nullptr; // TUCANO_ICON_*, or null
		Color iconColor = Style::kText;
		Color labelColor = Style::kText;
		std::string tooltip;
		// Headers are non-selectable grouping rows, drawn in the accent colour.
		bool isHeader = false;
		// Opaque hook so the caller can get from a row back to its own object without a lookup.
		void* userData = nullptr;
		std::vector<Item> children;
	};

	// Replaces the tree. Selection/expansion/rename state is preserved by id.
	void setRoot(std::vector<Item> items);
	// Null when the id is not in the current tree.
	Item* find(uint64_t id);
	const Item* find(uint64_t id) const;

	// Draws the rows. `filter` may be null; when set, rows that do not match are hidden, but a
	// non-matching parent of a match is kept (otherwise the match is unreachable).
	void draw(const Filter* filter = nullptr);

	// ── Selection ────────────────────────────────────────────────────────────
	const std::vector<uint64_t>& selection() const { return m_selection; }
	bool isSelected(uint64_t id) const;
	// `additive` toggles instead of replacing — the Ctrl-click behaviour, exposed for code paths
	// that select from outside the tree (picking in the viewport, for instance).
	void select(uint64_t id, bool additive = false);
	void clearSelection();

	// ── Expansion ────────────────────────────────────────────────────────────
	void setExpanded(uint64_t id, bool expanded, bool recursive = false);
	bool isExpanded(uint64_t id) const;
	void expandAll();
	void collapseAll();
	// Opens every ancestor of `id` so a selection made elsewhere becomes visible.
	void revealItem(uint64_t id);

	// ── Rename ───────────────────────────────────────────────────────────────
	// Starts inline editing. Commits on Enter or focus loss, cancels on Escape.
	void beginRename(uint64_t id);
	bool isRenaming() const { return m_renamingId != 0; }

	// ── Callbacks ────────────────────────────────────────────────────────────
	std::function<void(uint64_t)> onActivated;              // double-click or Enter
	std::function<void(uint64_t)> onContextMenu;            // right-click; caller draws the items
	std::function<void(uint64_t, const std::string&)> onRenamed;
	std::function<void()> onSelectionChanged;
	// Called while a row is dragged, to fill the ImGui drag payload. Rows are drag sources only
	// when this is set.
	std::function<void(uint64_t)> onDragPayload;

	// Row height multiplier, for denser or roomier lists.
	void setRowSpacing(float scale) { m_rowSpacing = scale; }

private:
	struct Row {
		Item* item;
		int depth;
	};

	void drawItem(Item& item, int depth, const Filter* filter);
	bool itemVisible(const Item& item, const Filter* filter) const;
	void handleRowInteraction(Item& item);
	void applyRangeSelection(uint64_t anchor, uint64_t target);
	void collectVisible(std::vector<Item*>& out, std::vector<Item>& items, const Filter* filter) const;

	std::vector<Item> m_root;
	std::vector<uint64_t> m_selection;
	std::vector<uint64_t> m_collapsed; // expansion is opt-out: a fresh tree reads as open
	uint64_t m_renamingId = 0;
	uint64_t m_selectionAnchor = 0;
	std::string m_renameBuffer;
	bool m_renameFocusPending = false;
	float m_rowSpacing = 1.0f;
	// Rows drawn this frame in visual order — what shift-range selection walks.
	std::vector<uint64_t> m_visibleOrder;
};

} // namespace tucano::editor::ui
