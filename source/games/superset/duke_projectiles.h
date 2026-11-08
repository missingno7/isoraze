#pragma once
#include "vectors.h"
class DCoreActor;
class PClassActor;
struct CollisionBase;
namespace Duke3d { class DDukeActor; }
namespace Superset
{
bool IsBloodDukeProjectile(Duke3d::DDukeActor* actor);
bool IsDukeProjectileEffect(Duke3d::DDukeActor* actor);
bool BloodDukeProjectileZ(Duke3d::DDukeActor* actor);
bool BloodDukeProjectileLink(Duke3d::DDukeActor* actor, double z);
void FinishDukeProjectileStep();
int MoveDukeProjectile(Duke3d::DDukeActor* actor, const DVector3& change, unsigned cliptype, CollisionBase& hit);
// Position is in world coordinates; velocity is world displacement per 30 Hz step.
Duke3d::DDukeActor* SpawnDukeProjectile(DCoreActor* source, PClassActor* type,
	const DVector3& position, const DVector3& velocity);
}
