#pragma once

#include <imgui.h>
#include <imgui_node_editor.h>

#include <string>
#include <vector>

namespace tucano::terrain {

// ── Pin type system ──────────────────────────────────
enum class MaterialPinType : int {
	Float, Float2, Float3, Float4,
	Color, Sampler, Bool, Int
};

struct MaterialPin {
	int id;
	std::string name;
	MaterialPinType type;
	bool isInput;
};

// ── Node categories ───────────────────────────────────
enum class MaterialNodeCategory : int {
	Output,    // Material Output
	Texture,   // Texture Sample
	Constant,  // Scalar, Vector, Color constants
	Parameter, // Scalar/Vector param
	Math,      // Add, Multiply, Lerp, etc
	Utility,   // Time, coordinates, etc
};

struct MaterialGraphNode {
	int id;
	std::string type;
	std::string name;
	MaterialNodeCategory category = MaterialNodeCategory::Math;
	std::vector<MaterialPin> inputs;
	std::vector<MaterialPin> outputs;
	float posX, posY;
	std::string value;   // for constant/param nodes
	std::string paramName;
};

struct MaterialGraphLink {
	int id;
	int fromNode, fromPin;
	int toNode, toPin;
};

// ── Graph data ────────────────────────────────────────
class MaterialGraphData {
public:
	std::vector<MaterialGraphNode> nodes;
	std::vector<MaterialGraphLink> links;
	int nextId = 1;

	int addNode(const std::string& type, const std::string& name,
	            MaterialNodeCategory cat, float x, float y);
	int addLink(int fromNode, int fromPin, int toNode, int toPin);
	void removeNode(int nodeId);
	void removeLink(int linkId);
	std::string generateHLSL() const;
	static MaterialGraphData createDefault();

private:
	std::string getNodeValue(int nodeId, const std::string& pinName) const;
};

// ── Material Node Builder ─────────────────────────────
class MaterialNodeBuilder {
public:
	void Begin(int nodeId, const char* title, const ImVec4& headerColor, MaterialNodeCategory cat);
	void End();

	void BeginInput(ax::NodeEditor::PinId id, MaterialPinType type, const char* label);
	void EndInput();

	void BeginOutput(ax::NodeEditor::PinId id, MaterialPinType type, const char* label);
	void EndOutput();

	void Middle();

	// Widget within the body (value display, slider, etc)
	void Widget(const char* fmt, ...);

private:
	enum class Stage { Invalid, Begin, Header, Content, Input, Output, Middle, End };
	bool setStage(Stage s);

	void pin(ax::NodeEditor::PinId id, ax::NodeEditor::PinKind kind, MaterialPinType type, const char* label);

	Stage        m_stage = Stage::Invalid;
	ImU32        m_headerColor = 0;
	ImVec2       m_nodeMin{}, m_nodeMax{};
	ImVec2       m_headerMin{}, m_headerMax{};
	ImVec2       m_contentMin{}, m_contentMax{};
	ImVec2       m_pinIconSize{};
	bool         m_hasHeader = false;

	static ImColor PinColor(MaterialPinType t);
	static ImU32   CategoryAlphaColor(MaterialNodeCategory cat);
};

// ── Editor window ─────────────────────────────────────
class MaterialNodeEditor {
public:
	void open(const MaterialGraphData& graph);
	void close();
	bool isOpen() const { return m_open; }
	void render();

private:
	MaterialGraphData m_graph;
	bool m_open = false;
	ax::NodeEditor::EditorContext* m_ctx = nullptr;
};

} // namespace tucano::terrain
