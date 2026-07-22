#include "ECS/TemplateManager.h"

#include <algorithm>

namespace tucano::ecs {

bool TemplateManager::registerTemplate(const core::JsonValue& tmpl, std::string* err) {
  if (!tmpl.isObject()) {
    if (err) {
      *err = "template must be a JSON object";
    }
    return false;
  }
  const auto* nameV = tmpl.find("name");
  if (!nameV || !nameV->isString()) {
    if (err) {
      *err = "template missing 'name'";
    }
    return false;
  }
  Template t;
  t.name = nameV->asString();
  if (const auto* p = tmpl.find("parent"); p && p->isString()) {
    t.parent = p->asString();
  }
  if (const auto* c = tmpl.find("components"); c && c->isObject()) {
    t.components = *c;
  } else {
    t.components.type = core::JsonValue::Type::Object;
  }
  m_templates[t.name] = std::move(t);
  return true;
}

bool TemplateManager::loadFromString(std::string_view json, std::string* err) {
  core::JsonValue root;
  if (!core::JsonValue::parse(json, root, err)) {
    return false;
  }
  if (root.isArray()) {
    for (const auto& v : root.arr) {
      if (!registerTemplate(v, err)) {
        return false;
      }
    }
    return true;
  }
  return registerTemplate(root, err);
}

void TemplateManager::resolve(const Template& t, std::vector<uint32_t>& comps,
                              std::vector<std::vector<const core::JsonValue*>>& props,
                              std::vector<uint32_t>& seen) const {
  // Evita ciclos de herança.
  const uint32_t nameHash = uint32_t(std::hash<std::string>{}(t.name));
  if (std::find(seen.begin(), seen.end(), nameHash) != seen.end()) {
    return;
  }
  seen.push_back(nameHash);

  if (!t.parent.empty()) {
    auto it = m_templates.find(t.parent);
    if (it != m_templates.end()) {
      resolve(it->second, comps, props, seen);
    }
  }
  auto& reg = ComponentRegistry::instance();
  for (const auto& kv : t.components.obj) {
    const uint32_t id = reg.find(kv.first);
    if (id == kInvalidEntity) {
      continue; // componente desconhecido — ignora
    }
    auto existing = std::find(comps.begin(), comps.end(), id);
    if (existing != comps.end()) {
      // Empilha os props do filho depois dos do parent (aplicados em ordem, merge por campo).
      props[size_t(existing - comps.begin())].push_back(&kv.second);
    } else {
      comps.push_back(id);
      props.push_back({&kv.second});
    }
  }
}

Entity TemplateManager::instantiate(EntityManager& em, std::string_view name) {
  auto it = m_templates.find(std::string(name));
  if (it == m_templates.end()) {
    return kInvalidEntity;
  }
  std::vector<uint32_t> comps;
  std::vector<std::vector<const core::JsonValue*>> props;
  std::vector<uint32_t> seen;
  resolve(it->second, comps, props, seen);

  const Entity e = em.create(std::span<const uint32_t>(comps.data(), comps.size()));
  auto& reg = ComponentRegistry::instance();
  for (size_t i = 0; i < comps.size(); ++i) {
    const auto& desc = reg.desc(comps[i]);
    if (!desc.applyJson) {
      continue;
    }
    void* dst = em.get(e, comps[i]);
    for (const core::JsonValue* p : props[i]) {
      desc.applyJson(dst, *p); // parent primeiro, filho depois (merge por campo)
    }
  }
  return e;
}

} // namespace tucano::ecs
