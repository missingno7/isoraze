#pragma once
#include "dobject.h"

class DCoreActor;

// Runtime identity and lifecycle, independent of the native controller class.
// Transform and receiver state belong to actor, never to the source event node.
class DGameplayEntity : public DObject
{
	DECLARE_CLASS(DGameplayEntity, DObject)
	HAS_OBJECT_POINTERS
public:
	TObjPtr<DCoreActor*> actor, sourceNode, target, owner, damageSource;
	int identity = 0, sourceIndex = -1, archetype = 0;
	int deathBinding = -1, pickupBinding = -1;
	bool enemy = false, deathEmitted = false, pickupEmitted = false, killCredited = false;
	bool componentTick = false; // Imported controller scheduled by the level, outside native world lists.
	bool deathRequested = false; // CON can count a freeze before choosing its final state.
	void Serialize(FSerializer& arc) override;
};
