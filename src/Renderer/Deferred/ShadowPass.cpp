#include "Renderer/Deferred/ShadowPass.h"
#include "Renderer/Shadows/ToroidalShadows.h"

// Shadow rendering is orchestrated by Renderer; this module owns toroidal atlas math.
namespace tucano::shadow_pass {

using Atlas = ToroidalShadowAtlas;

} // namespace tucano::shadow_pass
