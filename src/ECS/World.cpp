#include "ECS/World.h"

namespace tucano::ecs {

World::World() {
  // Garante que os componentes-core existam antes de qualquer create/query.
  registerCoreComponents();
}

} // namespace tucano::ecs
