#include "Terrain/MaterialNodeEditor.h"

#include <imgui.h>
#include <imgui_node_editor.h>

#include <filesystem>
#include <algorithm>

namespace tucano::terrain {

namespace ed = ax::NodeEditor;
namespace fs = std::filesystem;

// ── Helpers ───────────────────────────────────────────
static ed::PinId MkPin(int n, int p) {
	uint64_t v = (static_cast<uint64_t>(n) << 32) | static_cast<uint32_t>(p);
	return ed::PinId(reinterpret_cast<void*>(static_cast<uintptr_t>(v)));
}
static ImColor PinCol(MaterialPinType t) {
	switch (t) {
	case MaterialPinType::Float: return ImColor(120,200,120); case MaterialPinType::Float2: return ImColor(120,200,200);
	case MaterialPinType::Float3: return ImColor(200,200,120); case MaterialPinType::Float4: return ImColor(180,180,200);
	case MaterialPinType::Sampler: return ImColor(200,120,200); default: return ImColor(150,150,150);
	}
}
static ImColor CatCol(MaterialNodeCategory cat) {
	switch (cat) {
	case MaterialNodeCategory::Output: return ImColor(107,55,148); case MaterialNodeCategory::Texture: return ImColor(200,85,55);
	case MaterialNodeCategory::Constant: return ImColor(80,155,80); case MaterialNodeCategory::Math: return ImColor(55,110,185);
	default: return ImColor(120,120,130);
	}
}

// ── Graph data methods ────────────────────────────────
int MaterialGraphData::addNode(const std::string& type, const std::string& name,
                                MaterialNodeCategory cat, float x, float y) {
	MaterialGraphNode n; n.id=nextId++; n.type=type; n.name=name; n.category=cat; n.posX=x; n.posY=y;
	if (type=="Constant1")      { n.outputs={{nextId++,"",MaterialPinType::Float,false}}; n.value="0.5"; }
	else if (type=="Constant3") { n.outputs={{nextId++,"RGB",MaterialPinType::Float3,false}}; n.value="0.5,0.5,0.5"; }
	else if (type=="Constant4") { n.outputs={{nextId++,"RGBA",MaterialPinType::Float4,false}}; n.value="1,1,1,1"; }
	else if (type=="Multiply"||type=="Add"||type=="Subtract"||type=="Divide") {
		n.inputs={{nextId++,"A",MaterialPinType::Float4,true},{nextId++,"B",MaterialPinType::Float4,true}};
		n.outputs={{nextId++,"",MaterialPinType::Float4,false}};
	} else if (type=="Lerp") {
		n.inputs={{nextId++,"A",MaterialPinType::Float4,true},{nextId++,"B",MaterialPinType::Float4,true},{nextId++,"Alpha",MaterialPinType::Float,true}};
		n.outputs={{nextId++,"",MaterialPinType::Float4,false}};
	} else if (type=="TextureSample") {
		n.inputs={{nextId++,"UV",MaterialPinType::Float2,true}};
		n.outputs={{nextId++,"RGBA",MaterialPinType::Float4,false},{nextId++,"R",MaterialPinType::Float,false},{nextId++,"G",MaterialPinType::Float,false},{nextId++,"B",MaterialPinType::Float,false},{nextId++,"A",MaterialPinType::Float,false}};
	} else if (type=="MaterialOutput") {
		n.inputs={{nextId++,"Base Color",MaterialPinType::Float3,true},{nextId++,"Normal",MaterialPinType::Float3,true},{nextId++,"Roughness",MaterialPinType::Float,true},{nextId++,"Metallic",MaterialPinType::Float,true},{nextId++,"Emissive",MaterialPinType::Float3,true},{nextId++,"AO",MaterialPinType::Float,true},{nextId++,"Opacity",MaterialPinType::Float,true}};
	}
	nodes.push_back(n); return n.id;
}
int MaterialGraphData::addLink(int fn,int fp,int tn,int tp){links.push_back({nextId++,fn,fp,tn,tp});return nextId-1;}
void MaterialGraphData::removeNode(int id){nodes.erase(std::remove_if(nodes.begin(),nodes.end(),[id](auto&n){return n.id==id;}),nodes.end());links.erase(std::remove_if(links.begin(),links.end(),[id](auto&l){return l.fromNode==id||l.toNode==id;}),links.end());}
void MaterialGraphData::removeLink(int id){links.erase(std::remove_if(links.begin(),links.end(),[id](auto&l){return l.id==id;}),links.end());}

MaterialGraphData MaterialGraphData::createDefault() {
	MaterialGraphData g; int o=g.addNode("MaterialOutput","Material Output",MaterialNodeCategory::Output,500,80);
	int c=g.addNode("Constant3","Base Color",MaterialNodeCategory::Constant,100,80); g.nodes.back().value="0.18,0.40,0.10";
	int r=g.addNode("Constant1","Roughness",MaterialNodeCategory::Constant,100,180); g.nodes.back().value="0.6";
	int m=g.addNode("Constant1","Metallic",MaterialNodeCategory::Constant,100,270); g.nodes.back().value="0.0";
	int t=g.addNode("TextureSample","Ground Albedo",MaterialNodeCategory::Texture,300,80);
	g.addLink(c,g.nodes[1].outputs[0].id,o,g.nodes[0].inputs[0].id);
	g.addLink(r,g.nodes[2].outputs[0].id,o,g.nodes[0].inputs[2].id);
	g.addLink(m,g.nodes[3].outputs[0].id,o,g.nodes[0].inputs[3].id);
	return g;
}
std::string MaterialGraphData::generateHLSL() const {
	int o=-1; for(auto&n:nodes) if(n.type=="MaterialOutput"){o=n.id;break;} if(o<0) return "// No output\n";
	return "float4 PSMain():SV_Target{float3 a=float3("+getNodeValue(o,"Base Color")+");return float4(a,1);}\n";
}
std::string MaterialGraphData::getNodeValue(int nid,const std::string& pn)const{
	for(auto&n:nodes){if(n.id!=nid)continue;if(!n.value.empty())return n.value;
		for(auto&l:links) for(auto&p:n.inputs) if(p.name==pn&&l.toNode==nid&&l.toPin==p.id) return getNodeValue(l.fromNode,"");}
	for(auto&n:nodes) if(n.id==nid&&!n.value.empty()) return n.value; return "0.5";
}

// ── Asset Browser ─────────────────────────────────────
struct AssetBrowser {
	std::string m_root = "Assets";
	std::string m_currentPath;
	std::vector<std::string> m_folders;
	std::vector<std::string> m_textures;
	bool m_needScan = true;

	void scan() {
		m_folders.clear(); m_textures.clear();
		std::error_code ec;
		std::string scanPath = m_currentPath.empty() ? m_root : m_currentPath;
		if (!fs::exists(scanPath, ec)) { m_currentPath = m_root; scanPath = m_root; }
		for (auto& entry : fs::directory_iterator(scanPath, ec)) {
			if (entry.is_directory(ec)) m_folders.push_back(entry.path().filename().string());
			else if (entry.is_regular_file(ec)) {
				auto ext = entry.path().extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
				if (ext == ".png" || ext == ".jpg" || ext == ".dds" || ext == ".tga" || ext == ".hdr")
					m_textures.push_back(entry.path().filename().string());
			}
		}
		std::sort(m_folders.begin(), m_folders.end());
		std::sort(m_textures.begin(), m_textures.end());
		m_needScan = false;
	}

	void navigate(const std::string& folder) {
		m_currentPath = m_currentPath.empty() ? m_root + "/" + folder : m_currentPath + "/" + folder;
		m_needScan = true;
	}
	void goUp() {
		if (m_currentPath.empty()) return;
		auto pos = m_currentPath.rfind('/');
		m_currentPath = (pos != std::string::npos) ? m_currentPath.substr(0, pos) : "";
		if (m_currentPath == m_root) m_currentPath = "";
		m_needScan = true;
	}
	void goRoot() { m_currentPath = ""; m_needScan = true; }
};

// ── Editor ────────────────────────────────────────────

void MaterialNodeEditor::open(const MaterialGraphData& g) { m_graph=g; m_open=true; }
void MaterialNodeEditor::close() { m_open=false; if(m_ctx){ed::DestroyEditor(m_ctx);m_ctx=nullptr;} }

void MaterialNodeEditor::render() {
	if (!m_open) return;

	ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);
	ImGui::SetNextWindowSize(vp->WorkSize);
	ImGuiWindowFlags wflags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
	                           ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	                           ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
	if (!ImGui::Begin("##MaterialEditorFull", &m_open, wflags)) {
		ImGui::End(); ImGui::PopStyleVar(2); return;
	}
	ImGui::PopStyleVar(2);

	// ── Top bar ────────────────────────────────────
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10,6));
	ImGui::BeginChild("##topbar", ImVec2(0, 38), ImGuiChildFlags_Border);
	ImGui::Text("MATERIAL EDITOR");
	ImGui::SameLine(ImGui::GetContentRegionAvail().x - 260);
	if (ImGui::Button("Back to Main", ImVec2(100,0))) { close(); }
	ImGui::SameLine();
	if (ImGui::Button("Compile", ImVec2(70,0))) { /* compile */ }
	ImGui::SameLine();
	if (ImGui::Button("Save", ImVec2(60,0))) { /* save */ }
	ImGui::EndChild();
	ImGui::PopStyleVar();

	// ── Main split: graph | sidebar ────────────────
	ImGui::Columns(2, "##matcolumns", false);
	ImGui::SetColumnWidth(0, ImGui::GetContentRegionAvail().x - 270);
	ImGui::SetColumnWidth(1, 270);

	// ── Left: Node graph ───────────────────────────
	if (!m_ctx) m_ctx = ed::CreateEditor();
	ed::SetCurrentEditor(m_ctx);

	auto& s = ed::GetStyle();
	s.NodeRounding = 4.0f; s.NodeBorderWidth = 1.0f;
	s.PinRadius = 4.5f; s.PinBorderWidth = 1.0f;

	ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.12f,0.12f,0.14f,1));
	ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.25f,0.25f,0.28f,1));
	ed::PushStyleColor(ed::StyleColor_PinRect, ImVec4(0.18f,0.18f,0.20f,1));

	ed::Begin("##graph", ImVec2(ImGui::GetColumnWidth(0), ImGui::GetContentRegionAvail().y - 32));

	for (auto& node : m_graph.nodes) {
		ed::BeginNode(node.id);
		ImGui::BeginGroup();
		auto hdr = CatCol(node.category);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(hdr.Value.x, hdr.Value.y, hdr.Value.z, 1));
		ImGui::TextUnformatted(node.name.c_str());
		ImGui::PopStyleColor();
		ImGui::Separator();
		for (auto& pin : node.inputs) {
			ed::BeginPin(MkPin(node.id, pin.id), ed::PinKind::Input);
			ImGui::Text("  %s", pin.name.c_str());
			ed::EndPin();
		}
		for (auto& pin : node.outputs) {
			ed::BeginPin(MkPin(node.id, pin.id), ed::PinKind::Output);
			ImGui::Text("%s  ", pin.name.c_str());
			ed::EndPin();
		}
		ImGui::EndGroup();
		ed::EndNode();
	}

	for (auto& l : m_graph.links)
		ed::Link(ed::LinkId(reinterpret_cast<void*>(static_cast<uintptr_t>(l.id))),
		         MkPin(l.fromNode, l.fromPin), MkPin(l.toNode, l.toPin));

	if (ed::BeginCreate()) {
		ed::PinId a, b;
		if (ed::QueryNewLink(&a, &b) && a != b && ed::AcceptNewItem()) {
			uintptr_t ap = reinterpret_cast<uintptr_t>(a.AsPointer());
			uintptr_t bp = reinterpret_cast<uintptr_t>(b.AsPointer());
			m_graph.addLink((int)(ap>>32), (int)(ap&0xFFFFFFFF), (int)(bp>>32), (int)(bp&0xFFFFFFFF));
		}
		ed::EndCreate();
	}
	if (ed::BeginDelete()) {
		ed::LinkId lid; while (ed::QueryDeletedLink(&lid) && ed::AcceptDeletedItem()) m_graph.removeLink((int)reinterpret_cast<uintptr_t>(lid.AsPointer()));
		ed::NodeId nid; while (ed::QueryDeletedNode(&nid) && ed::AcceptDeletedItem()) m_graph.removeNode((int)reinterpret_cast<uintptr_t>(nid.AsPointer()));
		ed::EndDelete();
	}

	ed::End();
	ed::PopStyleColor(3);
	ed::SetCurrentEditor(nullptr);

	// ── Add node bar (bottom-left) ──────────────────
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
	ImGui::Text("Nodes:");
	ImGui::SameLine();
	if (ImGui::SmallButton("+Const3")) m_graph.addNode("Constant3","Color",MaterialNodeCategory::Constant,500,300);
	ImGui::SameLine();
	if (ImGui::SmallButton("+Scalar")) m_graph.addNode("Constant1","Scalar",MaterialNodeCategory::Constant,500,340);
	ImGui::SameLine();
	if (ImGui::SmallButton("+Mul")) m_graph.addNode("Multiply","Multiply",MaterialNodeCategory::Math,600,300);
	ImGui::SameLine();
	if (ImGui::SmallButton("+Add")) m_graph.addNode("Add","Add",MaterialNodeCategory::Math,600,340);
	ImGui::SameLine();
	if (ImGui::SmallButton("+Lerp")) m_graph.addNode("Lerp","Lerp",MaterialNodeCategory::Math,600,380);
	ImGui::SameLine();
	if (ImGui::SmallButton("+Tex")) m_graph.addNode("TextureSample","Texture Sample",MaterialNodeCategory::Texture,500,380);

	ImGui::NextColumn();

	// ── Right: Asset Browser ────────────────────────
	static AssetBrowser browser;
	if (browser.m_needScan) browser.scan();

	ImGui::BeginChild("##assets", ImVec2(0, 0), ImGuiChildFlags_Border);
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,1,0.5f));
	ImGui::Text("ASSET BROWSER");
	ImGui::PopStyleColor();
	ImGui::Separator();

	// Breadcrumb
	if (ImGui::SmallButton("Root")) browser.goRoot();
	if (!browser.m_currentPath.empty()) {
		ImGui::SameLine();
		if (ImGui::SmallButton("<")) browser.goUp();
		ImGui::SameLine();
		ImGui::Text("%s", browser.m_currentPath.c_str());
	} else {
		ImGui::SameLine();
		ImGui::Text("Assets");
	}
	ImGui::Separator();

	// Folders
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.8f,0.3f,1));
	if (ImGui::TreeNodeEx("Folders", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::PopStyleColor();
		for (auto& f : browser.m_folders) {
			if (ImGui::Selectable(f.c_str())) browser.navigate(f);
		}
		ImGui::TreePop();
	} else ImGui::PopStyleColor();

	ImGui::Separator();

	// Textures
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f,0.8f,0.5f,1));
	if (ImGui::TreeNodeEx("Textures", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::PopStyleColor();
		float availW = ImGui::GetContentRegionAvail().x;
		int cols = std::max(1, (int)(availW / 90));
		int i = 0;
		for (auto& tx : browser.m_textures) {
			if (i > 0 && i % cols != 0) ImGui::SameLine();
			ImGui::BeginGroup();
			ImGui::Button("TEX", ImVec2(72, 56));
			ImGui::TextWrapped("%s", tx.c_str());
			ImGui::EndGroup();
			i++;
		}
		ImGui::TreePop();
	} else ImGui::PopStyleColor();

	ImGui::EndChild();

	ImGui::Columns(1);
	ImGui::End();
}

} // namespace tucano::terrain
