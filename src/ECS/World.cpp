#include "ECS/World.h"

#include "ECS/AuthoringComponents.h"

namespace tucano::ecs {

World::World() {
  // Garante que os componentes-core existam antes de qualquer create/query.
  registerCoreComponents();
  // Os de autoria vêm junto: um mundo sem eles não pode ser salvo nem editado, e deixar a chamada
  // para o chamador é a mesma armadilha da CP-20b.
  registerAuthoringComponents();
}

} // namespace tucano::ecs
