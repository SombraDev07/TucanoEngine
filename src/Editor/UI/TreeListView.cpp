#include "Editor/UI/TreeListView.h"
#include "Editor/UI/Fonts.h"
#include "Editor/UI/Icons.h"
#include "Editor/UI/Widgets.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>

namespace tucano::editor::ui {

// ── Tree shape ──────────────────────────────────────────────────────────────

void TreeListView::setRoot(std::vector<Item> items) {
	m_root = std::move(items);
	// Selection is kept, but ids that no longer exist are dropped — otherwise a deleted object stays
	// "selected" invisibly and the next Delete acts on nothing.
	m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(),
	                                 [this](uint64_t id) { return find(id) == nullptr; }),
	                  m_selection.end());
	if (m_renamingId != 0 && find(m_renamingId) == nullptr) {
		m_renamingId = 0;
	}
}

namespace {

TreeListView::Item* findIn(std::vector<TreeListView::Item>& items, uint64_t id) {
	for (TreeListView::Item& item : items) {
		if (item.id == id) return &item;
		if (TreeListView::Item* hit = findIn(item.children, id)) return hit;
	}
	return nullptr;
}

} // namespace

TreeListView::Item* TreeListView::find(uint64_t id) {
	return id == 0 ? nullptr : findIn(m_root, id);
}

const TreeListView::Item* TreeListView::find(uint64_t id) const {
	return const_cast<TreeListView*>(this)->find(id);
}

// ── Selection ───────────────────────────────────────────────────────────────

bool TreeListView::isSelected(uint64_t id) const {
	return std::find(m_selection.begin(), m_selection.end(), id) != m_selection.end();
}

void TreeListView::select(uint64_t id, bool additive) {
	if (id == 0) return;
	if (!additive) {
		m_selection.clear();
		m_selection.push_back(id);
	} else {
		const auto it = std::find(m_selection.begin(), m_selection.end(), id);
		if (it != m_selection.end()) {
			m_selection.erase(it);
		} else {
			m_selection.push_back(id);
		}
	}
	m_selectionAnchor = id;
	if (onSelectionChanged) onSelectionChanged();
}

void TreeListView::clearSelection() {
	if (m_selection.empty()) return;
	m_selection.clear();
	m_selectionAnchor = 0;
	if (onSelectionChanged) onSelectionChanged();
}

void TreeListView::applyRangeSelection(uint64_t anchor, uint64_t target) {
	// Range is over what the user can *see*, not over the tree: shift-clicking across a collapsed
	// branch must not silently select its hidden contents.
	const auto a = std::find(m_visibleOrder.begin(), m_visibleOrder.end(), anchor);
	const auto b = std::find(m_visibleOrder.begin(), m_visibleOrder.end(), target);
	if (a == m_visibleOrder.end() || b == m_visibleOrder.end()) {
		select(target, false);
		return;
	}
	auto first = a;
	auto last = b;
	if (first > last) std::swap(first, last);

	m_selection.clear();
	for (auto it = first; it <= last; ++it) {
		m_selection.push_back(*it);
	}
	if (onSelectionChanged) onSelectionChanged();
}

// ── Expansion ───────────────────────────────────────────────────────────────

bool TreeListView::isExpanded(uint64_t id) const {
	return std::find(m_collapsed.begin(), m_collapsed.end(), id) == m_collapsed.end();
}

void TreeListView::setExpanded(uint64_t id, bool expanded, bool recursive) {
	const auto it = std::find(m_collapsed.begin(), m_collapsed.end(), id);
	if (expanded) {
		if (it != m_collapsed.end()) m_collapsed.erase(it);
	} else if (it == m_collapsed.end()) {
		m_collapsed.push_back(id);
	}

	if (recursive) {
		if (Item* item = find(id)) {
			for (const Item& child : item->children) {
				setExpanded(child.id, expanded, true);
			}
		}
	}
}

void TreeListView::expandAll() { m_collapsed.clear(); }

void TreeListView::collapseAll() {
	m_collapsed.clear();
	// Walk the whole tree once rather than recursing through setExpanded per node.
	const std::function<void(const std::vector<Item>&)> visit = [&](const std::vector<Item>& items) {
		for (const Item& item : items) {
			if (!item.children.empty()) {
				m_collapsed.push_back(item.id);
				visit(item.children);
			}
		}
	};
	visit(m_root);
}

void TreeListView::revealItem(uint64_t id) {
	// Walk down from the root recording the path, then open every node on it.
	std::vector<uint64_t> path;
	const std::function<bool(std::vector<Item>&)> walk = [&](std::vector<Item>& items) {
		for (Item& item : items) {
			if (item.id == id) return true;
			path.push_back(item.id);
			if (walk(item.children)) return true;
			path.pop_back();
		}
		return false;
	};
	if (walk(m_root)) {
		for (uint64_t ancestor : path) {
			setExpanded(ancestor, true);
		}
	}
}

// ── Rename ──────────────────────────────────────────────────────────────────

void TreeListView::beginRename(uint64_t id) {
	const Item* item = find(id);
	if (item == nullptr || item->isHeader) return;
	m_renamingId = id;
	m_renameBuffer = item->label;
	m_renameFocusPending = true;
}

// ── Drawing ─────────────────────────────────────────────────────────────────

bool TreeListView::itemVisible(const Item& item, const Filter* filter) const {
	if (filter == nullptr || filter->empty()) return true;
	if (filter->matches(item.label)) return true;
	// A parent that does not match is kept when a descendant does — otherwise the match cannot be
	// reached and the filter appears to have found nothing.
	for (const Item& child : item.children) {
		if (itemVisible(child, filter)) return true;
	}
	return false;
}

void TreeListView::handleRowInteraction(Item& item) {
	if (item.isHeader) return;

	const ImGuiIO& io = ImGui::GetIO();
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
		if (io.KeyShift && m_selectionAnchor != 0) {
			applyRangeSelection(m_selectionAnchor, item.id);
		} else {
			select(item.id, io.KeyCtrl);
		}
	}

	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
		if (onActivated) onActivated(item.id);
	}

	if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
		// Right-clicking outside the selection moves it, matching every file manager.
		if (!isSelected(item.id)) select(item.id, false);
		ImGui::OpenPopup("##treeContext");
	}
	if (ImGui::BeginPopup("##treeContext")) {
		if (onContextMenu) {
			onContextMenu(item.id);
		} else {
			if (ImGui::MenuItem(TUCANO_ICON_RENAME_BOX "  Rename", "F2")) beginRename(item.id);
		}
		ImGui::EndPopup();
	}

	if (onDragPayload && ImGui::BeginDragDropSource()) {
		onDragPayload(item.id);
		ImGui::TextUnformatted(item.label.c_str());
		ImGui::EndDragDropSource();
	}

	if (!item.tooltip.empty()) {
		itemTooltip("%s", item.tooltip.c_str());
	}
}

void TreeListView::drawItem(Item& item, int depth, const Filter* filter) {
	if (!itemVisible(item, filter)) return;

	ImGui::PushID(static_cast<int>(item.id));

	const bool hasChildren = !item.children.empty();
	// While filtering, branches open themselves — the user is looking for something, not navigating.
	const bool filtering = filter != nullptr && !filter->empty();
	const bool expanded = filtering || isExpanded(item.id);

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
	                           ImGuiTreeNodeFlags_FramePadding;
	if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	if (isSelected(item.id)) flags |= ImGuiTreeNodeFlags_Selected;
	if (item.isHeader) flags |= ImGuiTreeNodeFlags_DefaultOpen;

	if (hasChildren) ImGui::SetNextItemOpen(expanded);

	// The label is drawn manually after the node so the icon can carry its own colour; the node
	// itself gets an empty label so ImGui reserves the row without painting text over ours.
	const bool nodeOpen = ImGui::TreeNodeEx("##row", flags, "%s", "");

	// Record before any early-out, so shift-range covers exactly the rows on screen.
	if (!item.isHeader) m_visibleOrder.push_back(item.id);

	if (hasChildren && ImGui::IsItemToggledOpen()) {
		setExpanded(item.id, !isExpanded(item.id));
	}
	handleRowInteraction(item);

	// Row content, drawn over the node's rect.
	ImGui::SameLine(0.0f, 0.0f);
	if (item.icon != nullptr) {
		ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(item.iconColor));
		ImGui::TextUnformatted(item.icon);
		ImGui::PopStyleColor();
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
	}

	if (m_renamingId == item.id) {
		char buffer[256];
		std::snprintf(buffer, sizeof(buffer), "%s", m_renameBuffer.c_str());
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		if (m_renameFocusPending) {
			ImGui::SetKeyboardFocusHere();
			m_renameFocusPending = false;
		}
		const bool committed =
		    ImGui::InputText("##rename", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue);
		m_renameBuffer = buffer;

		// Escape wins over focus-loss: cancelling must not commit what was typed so far.
		if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			m_renamingId = 0;
		} else if (committed || ImGui::IsItemDeactivatedAfterEdit() ||
		           (!ImGui::IsItemActive() && !m_renameFocusPending)) {
			if (!m_renameBuffer.empty() && m_renameBuffer != item.label) {
				item.label = m_renameBuffer;
				if (onRenamed) onRenamed(item.id, m_renameBuffer);
			}
			m_renamingId = 0;
		}
	} else {
		const Color color = item.isHeader ? Style::kAccent0 : item.labelColor;
		ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(color));
		if (item.isHeader) {
			ScopedFont f(Font::SmallBold);
			ImGui::TextUnformatted(item.label.c_str());
		} else {
			ImGui::TextUnformatted(item.label.c_str());
		}
		ImGui::PopStyleColor();
	}

	if (nodeOpen && hasChildren) {
		if (expanded) {
			for (Item& child : item.children) {
				drawItem(child, depth + 1, filter);
			}
		}
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void TreeListView::draw(const Filter* filter) {
	m_visibleOrder.clear();

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
	                    ImVec2(ImGui::GetStyle().FramePadding.x, 3.0f * m_rowSpacing));
	for (Item& item : m_root) {
		drawItem(item, 0, filter);
	}
	ImGui::PopStyleVar();

	// F2 renames the single selected row — the shortcut every file manager and editor uses.
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !isRenaming() &&
	    m_selection.size() == 1 && ImGui::IsKeyPressed(ImGuiKey_F2)) {
		beginRename(m_selection.front());
	}

	// Enter activates, matching the double-click path.
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !isRenaming() &&
	    m_selection.size() == 1 && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
		if (onActivated) onActivated(m_selection.front());
	}
}

} // namespace tucano::editor::ui
