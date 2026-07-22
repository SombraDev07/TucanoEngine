#include "ECS/ComponentTypes.h"

#include <stdexcept>

namespace tucano::ecs {

ComponentRegistry& ComponentRegistry::instance() {
  static ComponentRegistry s;
  return s;
}

uint32_t ComponentRegistry::registerComponent(const char* name, uint32_t size, uint32_t align,
                                              JsonApplyFn applyJson) {
  auto it = m_byName.find(name);
  if (it != m_byName.end()) {
    return it->second; // idempotent
  }
  ComponentDesc d;
  d.name = name;
  d.size = size;
  d.align = align;
  d.applyJson = applyJson;
  const uint32_t id = uint32_t(m_types.size());
  m_types.push_back(std::move(d));
  m_byName.emplace(name, id);
  return id;
}

uint32_t ComponentRegistry::find(std::string_view name) const {
  auto it = m_byName.find(std::string(name));
  return it != m_byName.end() ? it->second : kInvalidEntity;
}

} // namespace tucano::ecs
