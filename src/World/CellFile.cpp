#include "World/CellFile.h"

#include "Core/Json.h"

#include <cstdio>
#include <sstream>

namespace tucano::world {
namespace {

void appendNum(std::string& out, float v) {
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%.6g", double(v));
  out += buf;
}

void appendVec3(std::string& out, const glm::vec3& v) {
  out += '[';
  appendNum(out, v.x);
  out += ',';
  appendNum(out, v.y);
  out += ',';
  appendNum(out, v.z);
  out += ']';
}

void appendQuat(std::string& out, const glm::quat& q) {
  // Stored xyzw. The order is written down here because glm's constructor takes wxyz — the exact
  // mismatch that silently corrupts rotations if a reader assumes the other convention.
  out += '[';
  appendNum(out, q.x);
  out += ',';
  appendNum(out, q.y);
  out += ',';
  appendNum(out, q.z);
  out += ',';
  appendNum(out, q.w);
  out += ']';
}

void appendEscaped(std::string& out, const std::string& s) {
  out += '"';
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  out += '"';
}

glm::vec3 readVec3(const core::JsonValue* v, const glm::vec3& def) {
  if (!v || !v->isArray() || v->arr.size() < 3) return def;
  return glm::vec3(v->arr[0].asFloat(def.x), v->arr[1].asFloat(def.y), v->arr[2].asFloat(def.z));
}

glm::quat readQuat(const core::JsonValue* v, const glm::quat& def) {
  if (!v || !v->isArray() || v->arr.size() < 4) return def;
  // xyzw on disk → glm's wxyz constructor.
  return glm::quat(v->arr[3].asFloat(def.w), v->arr[0].asFloat(def.x), v->arr[1].asFloat(def.y),
                   v->arr[2].asFloat(def.z));
}

} // namespace

const char* cellObjectKindName(CellObjectKind kind) {
  switch (kind) {
    case CellObjectKind::Cube: return "cube";
    case CellObjectKind::Sphere: return "sphere";
    case CellObjectKind::Plane: return "plane";
    case CellObjectKind::Gltf: return "gltf";
    default: return "cube";
  }
}

const char* cellColliderKindName(CellColliderKind kind) {
  switch (kind) {
    case CellColliderKind::Box: return "box";
    case CellColliderKind::Sphere: return "sphere";
    default: return "none";
  }
}

std::string CellFile::toJson() const {
  std::string out;
  out.reserve(objects.size() * 160 + 128);
  out += "{\"version\":1,\"cell\":[";
  out += std::to_string(id.x);
  out += ',';
  out += std::to_string(id.y);
  out += ',';
  out += std::to_string(id.z);
  out += ',';
  out += std::to_string(id.level);
  out += "],\"layer\":";
  out += std::to_string(layer);
  out += ",\"objects\":[";

  for (size_t i = 0; i < objects.size(); ++i) {
    const CellObject& o = objects[i];
    if (i) out += ',';
    out += "{\"kind\":";
    appendEscaped(out, cellObjectKindName(o.kind));
    out += ",\"name\":";
    appendEscaped(out, o.name);
    if (o.kind == CellObjectKind::Gltf) {
      out += ",\"path\":";
      appendEscaped(out, o.path);
    }
    out += ",\"pos\":";
    appendVec3(out, o.position);
    out += ",\"rot\":";
    appendQuat(out, o.rotation);
    out += ",\"scale\":";
    appendVec3(out, o.scale);
    out += ",\"color\":";
    appendVec3(out, o.baseColor);
    out += ",\"metallic\":";
    appendNum(out, o.metallic);
    out += ",\"roughness\":";
    appendNum(out, o.roughness);
    out += ",\"size\":";
    appendNum(out, o.size);
    out += ",\"collider\":";
    appendEscaped(out, cellColliderKindName(o.collider));
    out += '}';
  }
  out += "]}";
  return out;
}

bool CellFile::fromJson(const std::string& text, CellFile& out) {
  core::JsonValue root;
  if (!core::JsonValue::parse(text, root, nullptr) || !root.isObject()) return false;

  CellFile parsed; // build into a temporary so a malformed file cannot half-overwrite the target

  if (const core::JsonValue* cell = root.find("cell");
      cell && cell->isArray() && cell->arr.size() >= 4) {
    parsed.id.x = cell->arr[0].asInt();
    parsed.id.y = cell->arr[1].asInt();
    parsed.id.z = cell->arr[2].asInt();
    parsed.id.level = uint32_t(cell->arr[3].asInt());
  }
  if (const core::JsonValue* l = root.find("layer")) parsed.layer = uint32_t(l->asInt());

  const core::JsonValue* objs = root.find("objects");
  if (objs && objs->isArray()) {
    parsed.objects.reserve(objs->arr.size());
    for (const core::JsonValue& jo : objs->arr) {
      if (!jo.isObject()) continue;
      CellObject o;
      const std::string kind = jo.find("kind") ? jo.find("kind")->asString("cube") : "cube";
      if (kind == "sphere") o.kind = CellObjectKind::Sphere;
      else if (kind == "plane") o.kind = CellObjectKind::Plane;
      else if (kind == "gltf") o.kind = CellObjectKind::Gltf;
      else o.kind = CellObjectKind::Cube;

      if (const core::JsonValue* n = jo.find("name")) o.name = n->asString();
      if (const core::JsonValue* p = jo.find("path")) o.path = p->asString();
      o.position = readVec3(jo.find("pos"), glm::vec3(0.0f));
      o.rotation = readQuat(jo.find("rot"), glm::quat(1, 0, 0, 0));
      o.scale = readVec3(jo.find("scale"), glm::vec3(1.0f));
      o.baseColor = readVec3(jo.find("color"), glm::vec3(0.8f));
      if (const core::JsonValue* m = jo.find("metallic")) o.metallic = m->asFloat(0.0f);
      if (const core::JsonValue* r = jo.find("roughness")) o.roughness = r->asFloat(0.7f);
      if (const core::JsonValue* s = jo.find("size")) o.size = s->asFloat(1.0f);
      if (const core::JsonValue* c = jo.find("collider")) {
        const std::string col = c->asString("none");
        if (col == "box") o.collider = CellColliderKind::Box;
        else if (col == "sphere") o.collider = CellColliderKind::Sphere;
        else o.collider = CellColliderKind::None;
      }
      parsed.objects.push_back(std::move(o));
    }
  }

  out = std::move(parsed);
  return true;
}

bool CellFile::save(const std::string& path) const {
  const std::string text = toJson();
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const size_t written = std::fwrite(text.data(), 1, text.size(), f);
  std::fclose(f);
  return written == text.size();
}

bool CellFile::load(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size <= 0) {
    std::fclose(f);
    return false;
  }
  std::string text(static_cast<size_t>(size), '\0');
  const size_t read = std::fread(text.data(), 1, text.size(), f);
  std::fclose(f);
  if (read != text.size()) return false;
  return fromJson(text, *this);
}

std::string cellFilePath(const std::string& worldRoot, const CellId& id, uint32_t layer) {
  std::ostringstream ss;
  ss << worldRoot << "/cells/L" << id.level << '_' << id.x << '_' << id.y << '_' << id.z << '_'
     << layer << ".tcell";
  return ss.str();
}

} // namespace tucano::world
