#pragma once
#include "vectors.h"
class DCoreActor;
class PClassActor;
namespace Duke3d { class DDukeActor; }
namespace Superset
{
void InitBloodMapDukePickups();
bool IsBloodDukePickup(Duke3d::DDukeActor* actor);
bool BloodDukePickupEnvironment(Duke3d::DDukeActor* actor, int& medium);
void TickBloodMapDukePickups();
Duke3d::DDukeActor* SpawnBloodMapDukePickup(DCoreActor* source, PClassActor* type, const DVector3& position);
}
