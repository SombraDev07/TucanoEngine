#pragma once

#include "Core/Json.h"
#include "ECS/EntityManager.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace tucano::ecs {

// Templates de entidade (JSON) com herança de parent. Resolve os componentes de um template
// e aplica os props via ComponentDesc::applyJson (registrado por componente).
class TemplateManager {
public:
  // Registra um template a partir de um objeto JSON {name, parent?, components:{...}}.
  bool registerTemplate(const core::JsonValue& tmpl, std::string* err = nullptr);
  // Carrega múltiplos templates de um texto JSON (array ou objeto único).
  bool loadFromString(std::string_view json, std::string* err = nullptr);

  bool has(std::string_view name) const { return m_templates.count(std::string(name)) > 0; }

  // Instancia um template: cria a entidade com os componentes resolvidos e aplica os props.
  Entity instantiate(EntityManager& em, std::string_view name);

private:
  struct Template {
    std::string name;
    std::string parent;
    core::JsonValue components; // objeto: nome do componente → props
  };
  // Achata a cadeia de herança em comps + lista de props por componente (parent → filho,
  // aplicados em ordem; o filho sobrescreve campo a campo, não o bloco inteiro).
  void resolve(const Template& t, std::vector<uint32_t>& comps,
               std::vector<std::vector<const core::JsonValue*>>& props, std::vector<uint32_t>& seen) const;

  std::unordered_map<std::string, Template> m_templates;
};

} // namespace tucano::ecs
