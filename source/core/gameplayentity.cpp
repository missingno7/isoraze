#include "coreactor.h"
#include "gameplayentity.h"
#include "serializer.h"

IMPLEMENT_CLASS(DGameplayEntity, false, true)
IMPLEMENT_POINTERS_START(DGameplayEntity)
IMPLEMENT_POINTER(actor)
IMPLEMENT_POINTER(sourceNode)
IMPLEMENT_POINTER(target)
IMPLEMENT_POINTER(owner)
IMPLEMENT_POINTER(damageSource)
IMPLEMENT_POINTERS_END

void DGameplayEntity::Serialize(FSerializer& arc)
{
	Super::Serialize(arc);
	arc("actor", actor)("source_node", sourceNode)("identity", identity)
		("source_index", sourceIndex)("archetype", archetype)
		("death_binding", deathBinding)("pickup_binding", pickupBinding)
		("enemy", enemy)("death_emitted", deathEmitted)("pickup_emitted", pickupEmitted)("kill_credited", killCredited);
	arc("component_tick", componentTick)("death_requested", deathRequested);
	arc("target", target)("owner", owner)("damage_source", damageSource);
}
