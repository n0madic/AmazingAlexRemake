// st::WorldContactListener — the simulation world's contact callbacks (docs/04 §5, docs/03 §3): balloon
// pops, dart stabs, the scissors / bumper / helicopter / boxing-glove collision handlers, the collision
// sounds, the piggy-bank break and the contact goals (types 1 and 2). Internal to aa_sim: PhysicsWorld
// installs it over the session's SimulationContext.
#pragma once

#include "aa/sim/simulation.h"

#include <Box2D/Box2D.h>

#include <memory>

namespace aa::sim {

std::unique_ptr<b2ContactListener> makeWorldContactListener(SimulationContext& context);

}  // namespace aa::sim
