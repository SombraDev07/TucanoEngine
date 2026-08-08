#include "Editor/PropertyGrid.h"

#include "Core/TypeSystem/TypeInfo.h"
#include "Core/TypeSystem/TypeRegistry.h"
#include "Editor/TypeEditingRules.h"
#include "Editor/UI/Fonts.h"
#include "Editor/UI/Icons.h"
#include "Editor/UI/Style.h"
#include "Editor/UndoStack.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace tucano::editor {
namespace {

// Rows are a two-column table rather than plain ImGui widgets: a property grid is read by scanning
// the name column, and ImGui's default label-on-the-right puts the names in a ragged edge.
constexpr const char* kTableId = "##propertyGrid";

bool hasRange(const PropertyInfo& p) { return p.meta.maxValue > p.meta.minValue; }

// Undo entries are named after the label the user sees, not the C++ member.
std::string undoName(const PropertyInfo& p) { return p.displayLabel(); }

// `assetKind` is a string in the metadata rather than an enum so that a struct can be annotated
// without including the editor. An unrecognised name falls back to Any — a picker showing too much
// is recoverable, one showing nothing looks broken.
ui::AssetPicker::Kind assetKindFromName(const char* name) {
	if (name == nullptr) return ui::AssetPicker::Kind::Any;
	const std::string_view kind(name);
	if (kind == "mesh") return ui::AssetPicker::Kind::Mesh;
	if (kind == "texture") return ui::AssetPicker::Kind::Texture;
	if (kind == "hdri") return ui::AssetPicker::Kind::Hdri;
	if (kind == "scene") return ui::AssetPicker::Kind::Scene;
	if (kind == "text") return ui::AssetPicker::Kind::Text;
	return ui::AssetPicker::Kind::Any;
}

} // namespace

bool PropertyGrid::visible(const PropertyInfo& property) const {
	if (m_filter.empty()) return true;
	// Matching the category too keeps a whole group reachable by typing its name.
	return m_filter.matches(property.displayLabel()) || m_filter.matches(property.name) ||
	       m_filter.matches(property.meta.category);
}

bool PropertyGrid::drawFilterBox(float width) {
	m_filter.setHint("Filter properties...");
	return m_filter.draw("##propertyFilter", width);
}

// ── Value editors ───────────────────────────────────────────────────────────

bool PropertyGrid::drawScalar(const PropertyInfo& property, void* instance) {
	ImGui::SetNextItemWidth(-1.0f);
	bool changed = false;

	switch (property.coreType) {
		case CoreType::Bool: {
			bool& value = property.valueIn<bool>(instance);
			const bool before = value;
			if (ImGui::Checkbox("##v", &value)) {
				if (m_undo) m_undo->pushValue(undoName(property), &value, before, value);
				// A click is a whole gesture; nothing after it should merge into the same step.
				if (m_undo) m_undo->breakMerge();
				changed = true;
			}
			break;
		}
		case CoreType::Int32: {
			int32_t& value = property.valueIn<int32_t>(instance);
			const int32_t before = value;
			// A slider when the author gave bounds, a drag otherwise: an unbounded slider is a lie
			// about the valid range.
			const bool edited =
			    hasRange(property)
			        ? ImGui::SliderInt("##v", &value, static_cast<int>(property.meta.minValue),
			                           static_cast<int>(property.meta.maxValue))
			        : ImGui::DragInt("##v", &value, property.meta.step > 0.0f ? property.meta.step : 1.0f);
			if (edited) {
				if (m_undo) m_undo->pushValue(undoName(property), &value, before, value);
				changed = true;
			}
			break;
		}
		case CoreType::UInt32: {
			uint32_t& value = property.valueIn<uint32_t>(instance);
			const uint32_t before = value;
			int shown = static_cast<int>(value);
			const bool edited =
			    hasRange(property)
			        ? ImGui::SliderInt("##v", &shown, static_cast<int>(property.meta.minValue),
			                           static_cast<int>(property.meta.maxValue))
			        : ImGui::DragInt("##v", &shown, 1.0f, 0, INT32_MAX);
			if (edited) {
				value = static_cast<uint32_t>(shown < 0 ? 0 : shown);
				if (m_undo) m_undo->pushValue(undoName(property), &value, before, value);
				changed = true;
			}
			break;
		}
		case CoreType::Float: {
			float& value = property.valueIn<float>(instance);
			const float before = value;
			const bool edited =
			    hasRange(property)
			        ? ImGui::SliderFloat("##v", &value, property.meta.minValue, property.meta.maxValue)
			        : ImGui::DragFloat("##v", &value,
			                           property.meta.step > 0.0f ? property.meta.step : 0.01f);
			if (edited) {
				if (m_undo) m_undo->pushValue(undoName(property), &value, before, value);
				changed = true;
			}
			break;
		}
		case CoreType::Double: {
			double& value = property.valueIn<double>(instance);
			const double before = value;
			float shown = static_cast<float>(value);
			if (ImGui::DragFloat("##v", &shown,
			                     property.meta.step > 0.0f ? property.meta.step : 0.01f)) {
				value = static_cast<double>(shown);
				if (m_undo) m_undo->pushValue(undoName(property), &value, before, value);
				changed = true;
			}
			break;
		}
		case CoreType::String: {
			std::string& value = property.valueIn<std::string>(instance);
			const std::string before = value;
			// A string that names a file gets a picker: typing a path is how you end up with a
			// missing asset and no feedback.
			if (property.meta.assetKind != nullptr && property.meta.assetKind[0] != '\0') {
				changed = drawAssetPath(property, instance);
				break;
			}
			char buffer[512];
			std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
			if (ImGui::InputText("##v", buffer, sizeof(buffer))) {
				value = buffer;
				if (m_undo) m_undo->pushValue(undoName(property), &value, before, value);
				changed = true;
			}
			break;
		}
		default:
			ImGui::TextDisabled("unsupported");
			break;
	}
	return changed;
}

bool PropertyGrid::drawVector(const PropertyInfo& property, void* instance, int components) {
	// glm vectors are contiguous floats, which is what lets one path serve vec2/3/4 and colour.
	float* values = static_cast<float*>(property.addressIn(instance));
	float before[4] = {values[0], components > 1 ? values[1] : 0.0f, components > 2 ? values[2] : 0.0f,
	                   components > 3 ? values[3] : 0.0f};

	ImGui::SetNextItemWidth(-1.0f);
	bool edited = false;

	if (property.coreType == CoreType::Color) {
		// A colour is picked, not typed. Alpha only when the property actually carries it.
		edited = components >= 4 ? ImGui::ColorEdit4("##v", values, ImGuiColorEditFlags_Float)
		                         : ImGui::ColorEdit3("##v", values, ImGuiColorEditFlags_Float);
	} else if (hasRange(property)) {
		edited = ImGui::SliderScalarN("##v", ImGuiDataType_Float, values, components,
		                              &property.meta.minValue, &property.meta.maxValue, "%.3f");
	} else {
		const float speed = property.meta.step > 0.0f ? property.meta.step : 0.01f;
		edited = ImGui::DragScalarN("##v", ImGuiDataType_Float, values, components, speed);
	}

	if (!edited) return false;

	if (m_undo) {
		// One step for the whole vector: dragging X, Y and Z must not cost three Ctrl+Z.
		UndoStack::Compound compound(*m_undo, undoName(property));
		static const char* kSuffix[4] = {".x", ".y", ".z", ".w"};
		for (int i = 0; i < components; ++i) {
			m_undo->pushValue(undoName(property) + kSuffix[i], &values[i], before[i], values[i]);
		}
	}
	return true;
}

void PropertyGrid::setAssetRoot(std::string root) {
	if (root == m_assetRoot) return;
	m_assetRoot = std::move(root);
	for (auto& entry : m_assetPickers) entry.second.setRoot(m_assetRoot);
}

bool PropertyGrid::drawAssetPath(const PropertyInfo& property, void* instance) {
	std::string& value = property.valueIn<std::string>(instance);

	auto found = m_assetPickers.find(&property);
	if (found == m_assetPickers.end()) {
		ui::AssetPicker picker;
		picker.setRoot(m_assetRoot);
		picker.setKind(assetKindFromName(property.meta.assetKind));
		found = m_assetPickers.emplace(&property, std::move(picker)).first;
	}

	ui::AssetPicker& picker = found->second;
	// The value can change under us — a different object selected, a scene reloaded — so the
	// picker follows the property rather than the other way round.
	if (picker.path() != value) picker.setPath(value);
	picker.setDisabled(m_readOnly || property.meta.readOnly);

	if (!picker.draw("##asset")) return false;

	const std::string before = value;
	value = picker.path();
	if (m_undo) {
		m_undo->pushValue(undoName(property), &value, before, value);
		// Picking is a discrete act, not a drag: two picks in a row are two undo steps.
		m_undo->breakMerge();
	}
	return true;
}

bool PropertyGrid::drawQuat(const PropertyInfo& property, void* instance) {
	// Nobody authors a rotation as four numbers. The stored value stays a quaternion — only the row
	// is Euler degrees, converted on the way in and out.
	glm::quat& value = property.valueIn<glm::quat>(instance);
	const glm::quat before = value;

	const ImGuiID id = ImGui::GetID("##q");
	float euler[3];
	if (m_eulerOwner == id) {
		euler[0] = m_euler[0];
		euler[1] = m_euler[1];
		euler[2] = m_euler[2];
	} else {
		const glm::vec3 radians = glm::eulerAngles(value);
		// The +0.0f folds negative zero away: an unrotated object reads "0.0°, -0.0°, 0.0°" without
		// it, which looks like a bug in the transform and is only a sign bit.
		euler[0] = glm::degrees(radians.x) + 0.0f;
		euler[1] = glm::degrees(radians.y) + 0.0f;
		euler[2] = glm::degrees(radians.z) + 0.0f;
	}

	ImGui::SetNextItemWidth(-1.0f);
	const float speed = property.meta.step > 0.0f ? property.meta.step : 0.5f;
	const bool edited = ImGui::DragScalarN("##q", ImGuiDataType_Float, euler, 3, speed, nullptr, nullptr, "%.1f°");

	if (ImGui::IsItemActive()) {
		m_eulerOwner = id;
		m_euler[0] = euler[0];
		m_euler[1] = euler[1];
		m_euler[2] = euler[2];
	} else if (m_eulerOwner == id) {
		m_eulerOwner = 0;
	}

	if (!edited) return false;

	value = glm::quat(glm::radians(glm::vec3(euler[0], euler[1], euler[2])));

	if (m_undo) {
		// The step records the quaternion, because that is what undo has to put back — restoring
		// angles would re-round-trip and land somewhere slightly different.
		UndoStack::Compound compound(*m_undo, undoName(property));
		static const char* kSuffix[4] = {".x", ".y", ".z", ".w"};
		for (int i = 0; i < 4; ++i) {
			m_undo->pushValue(undoName(property) + kSuffix[i], &value[i], before[i], value[i]);
		}
	}
	return true;
}

bool PropertyGrid::drawEnum(const PropertyInfo& property, void* instance) {
	const TypeInfo* enumType = TypeRegistry::instance().find(property.typeId);
	int32_t& value = property.valueIn<int32_t>(instance);

	if (enumType == nullptr || !enumType->isEnum()) {
		// Registered as an enum but the type is missing: show the number rather than pretending.
		ImGui::SetNextItemWidth(-1.0f);
		const int32_t before = value;
		if (ImGui::DragInt("##v", reinterpret_cast<int*>(&value))) {
			if (m_undo) m_undo->pushValue(undoName(property), &value, before, value);
			return true;
		}
		return false;
	}

	const EnumConstant* current = enumType->findEnumConstant(value);
	const char* preview = current != nullptr ? current->label : "(unknown)";

	ImGui::SetNextItemWidth(-1.0f);
	bool changed = false;
	if (ImGui::BeginCombo("##v", preview)) {
		for (size_t i = 0; i < enumType->enumConstantCount; ++i) {
			const EnumConstant& constant = enumType->enumConstants[i];
			const bool selected = constant.value == value;
			if (ImGui::Selectable(constant.label, selected)) {
				const int32_t before = value;
				value = static_cast<int32_t>(constant.value);
				if (m_undo) {
					m_undo->pushValue(undoName(property), &value, before, value);
					m_undo->breakMerge();
				}
				changed = true;
			}
			if (selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	return changed;
}

bool PropertyGrid::drawStruct(const PropertyInfo& property, void* instance, int depth) {
	const TypeInfo* nested = TypeRegistry::instance().find(property.typeId);
	if (nested == nullptr) {
		ImGui::TextDisabled("unregistered type");
		return false;
	}

	// Nested structs are rows that expand, not a second grid: one table keeps the value column
	// aligned all the way down, which is the whole reason a grid is readable.
	//
	// Open by default. Closed reads as tidier and is worse: a Transform collapsed to a single arrow
	// hides position, rotation and scale — the three most-edited values in the editor — behind a
	// click, and nothing on the row says what is inside.
	bool changed = false;
	if (ImGui::TreeNodeEx("##nested",
	                      ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen)) {
		for (size_t i = 0; i < nested->propertyCount; ++i) {
			// The nested type owns its own rules, evaluated against the nested instance — a rule on
			// Material must not be asked about a field of Transform.
			changed |= drawProperty(*nested, nested->properties[i], property.addressIn(instance),
			                        depth + 1);
		}
		ImGui::TreePop();
	}
	return changed;
}

bool PropertyGrid::drawArray(const TypeInfo& owner, const PropertyInfo& property, void* instance,
                             int depth) {
	bool changed = false;
	char label[64];
	std::snprintf(label, sizeof(label), "%u items##array", property.arrayCount);

	if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth)) {
		// A fixed-size array is a run of elements; each gets a row named by index, so a wrong value
		// can be reported as "element 3" rather than "somewhere in the array".
		PropertyInfo element = property;
		element.coreType = property.elementType;
		element.arrayCount = 0;
		element.size = property.arrayCount > 0 ? property.size / property.arrayCount : property.size;

		for (uint32_t i = 0; i < property.arrayCount; ++i) {
			ImGui::PushID(static_cast<int>(i));
			char name[32];
			std::snprintf(name, sizeof(name), "[%u]", i);
			element.name = name;
			element.meta.label = name;
			element.offset = property.offset + i * element.size;
			// Still owned by the same type. The synthesised "[0]" name matches no rule, which is
			// correct — a rule targets a property, not one slot inside it.
			changed |= drawProperty(owner, element, instance, depth + 1);
			ImGui::PopID();
		}
		ImGui::TreePop();
	}
	return changed;
}

bool PropertyGrid::drawValue(const PropertyInfo& property, void* instance) {
	switch (property.coreType) {
		case CoreType::Vec2:
			return drawVector(property, instance, 2);
		case CoreType::Vec3:
			return drawVector(property, instance, 3);
		case CoreType::Vec4:
			return drawVector(property, instance, 4);
		case CoreType::Quat:
			return drawQuat(property, instance);
		case CoreType::Color:
			// Colours are stored as vec3 or vec4; the size tells which.
			return drawVector(property, instance, property.size >= sizeof(float) * 4 ? 4 : 3);
		case CoreType::Enum:
			return drawEnum(property, instance);
		default:
			return drawScalar(property, instance);
	}
}

// ── Rows ────────────────────────────────────────────────────────────────────

bool PropertyGrid::drawProperty(const TypeInfo& owner, const PropertyInfo& property, void* instance,
                                int depth) {
	if (property.meta.transient || !visible(property)) return false;

	// Rules that depend on the value being edited, not on its declaration (P4-03). Looked up per
	// row rather than cached: the condition reads sibling fields, so the answer changes the moment
	// the user ticks the box next to it.
	const TypeEditingRules* rules = TypeEditingRules::find(owner.id);
	if (rules != nullptr && rules->hidden(property, instance) == RuleState::Yes) return false;
	const bool lockedByRule =
	    rules != nullptr && rules->locked(property, instance) == RuleState::Yes;

	ImGui::PushID(property.name);
	ImGui::TableNextRow();

	// Name column.
	ImGui::TableSetColumnIndex(0);
	if (depth > 0) ImGui::Indent(ImGui::GetStyle().IndentSpacing * 0.5f * depth);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(property.displayLabel());
	if (property.meta.tooltip != nullptr && property.meta.tooltip[0] != '\0') {
		ui::itemTooltip("%s", property.meta.tooltip);
	}
	if (depth > 0) ImGui::Unindent(ImGui::GetStyle().IndentSpacing * 0.5f * depth);

	// Value column.
	ImGui::TableSetColumnIndex(1);
	const bool disabled = m_readOnly || property.meta.readOnly || lockedByRule;
	if (disabled) ImGui::BeginDisabled();

	bool changed = false;
	if (property.coreType == CoreType::Struct) {
		changed = drawStruct(property, instance, depth);
	} else if (property.coreType == CoreType::Array) {
		changed = drawArray(owner, property, instance, depth);
	} else {
		changed = drawValue(property, instance);
	}

	// The gesture ends when the widget is released, not when the value stops changing — that is what
	// separates one drag from the next in the undo history.
	if (m_undo != nullptr && ImGui::IsItemDeactivatedAfterEdit()) {
		m_undo->breakMerge();
	}

	if (disabled) ImGui::EndDisabled();
	ImGui::PopID();

	if (changed && onChanged) onChanged(property, instance);
	return changed;
}

bool PropertyGrid::categoryHasRows(std::string_view category, const TypeInfo& type,
                                   const void* instance) const {
	const TypeEditingRules* rules = TypeEditingRules::find(type.id);
	for (size_t i = 0; i < type.propertyCount; ++i) {
		const PropertyInfo& property = type.properties[i];
		const char* propertyCategory =
		    property.meta.category != nullptr ? property.meta.category : "";
		if (std::string_view(propertyCategory) != category) continue;
		if (property.meta.transient || !visible(property)) continue;
		if (rules != nullptr && rules->hidden(property, instance) == RuleState::Yes) continue;
		return true;
	}
	return false;
}

bool PropertyGrid::drawCategory(const char* category, const TypeInfo& type, void* instance) {
	bool changed = false;
	for (size_t i = 0; i < type.propertyCount; ++i) {
		const PropertyInfo& property = type.properties[i];
		const char* propertyCategory =
		    property.meta.category != nullptr ? property.meta.category : "";
		if (std::string_view(propertyCategory) != std::string_view(category)) continue;
		changed |= drawProperty(type, property, instance, 0);
	}
	return changed;
}

bool PropertyGrid::draw(const TypeInfo& type, void* instance) {
	if (instance == nullptr || type.propertyCount == 0) {
		ImGui::TextDisabled("Nothing to edit.");
		return false;
	}

	// Categories in declaration order, not alphabetical: the author grouped them in the order they
	// wanted them read.
	std::vector<std::string_view> categories;
	for (size_t i = 0; i < type.propertyCount; ++i) {
		const char* category = type.properties[i].meta.category;
		const std::string_view view = category != nullptr ? category : "";
		bool seen = false;
		for (const std::string_view& existing : categories) {
			if (existing == view) {
				seen = true;
				break;
			}
		}
		if (!seen) categories.push_back(view);
	}

	bool changed = false;
	const ImGuiTableFlags tableFlags =
	    ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
	    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;

	for (const std::string_view& category : categories) {
		// A header with nothing under it is noise, and with a filter typed it is worse than noise:
		// narrowing to one property used to leave all eight group headings on screen, each of them
		// empty. Rules make the same thing happen without a filter.
		if (!categoryHasRows(category, type, instance)) continue;

		const bool named = !category.empty();
		if (named) {
			// Categories collapse: a type with 30 properties is unusable as one flat list.
			ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(Style::kAccent0));
			const bool open = ImGui::CollapsingHeader(std::string(category).c_str(),
			                                          ImGuiTreeNodeFlags_DefaultOpen);
			ImGui::PopStyleColor();
			if (!open) continue;
		}

		const std::string tableName = std::string(kTableId) + std::string(category);
		if (ImGui::BeginTable(tableName.c_str(), 2, tableFlags)) {
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, m_labelFraction);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f - m_labelFraction);
			changed |= drawCategory(named ? std::string(category).c_str() : "", type, instance);
			ImGui::EndTable();
		}
	}
	return changed;
}

} // namespace tucano::editor
