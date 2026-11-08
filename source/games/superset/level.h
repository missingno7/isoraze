#pragma once
#include "vectors.h"
#include "units.h"
#include <functional>
class DCoreActor;
class DGameplayEntity;
class FSerializer;
class PClassActor;
struct sectortype;
struct tspritetype;
namespace Duke3d { class DDukeActor; struct ConExecutionServices; }

namespace Superset
{
enum class Archetype { None, BloodAxeZombie, DukePigcop, BloodShotgun, DukeShotgun };
enum class Medium { Air, WaterSurface, Underwater };
struct Environment { Medium medium = Medium::Air; double floorZ = 0, ceilingZ = 0, floorVelocity = 0; bool imported = false; };
struct TargetInfo
{
	DCoreActor* actor = nullptr;
	int identity = 0;
	Units::Health health = 0;
	DVector3 aim;
	double radius = 0, height = 0;
	bool enemy = false, alive = false;
};
void InitRuntimeLevel();
void ImportRuntimeLevel(bool bloodSource); // Format selection belongs only to this import boundary.
DCoreActor* ImportRuntimeObject(DCoreActor* source);
void ClearRuntimeLevel();
void SerializeRuntimeLevel(FSerializer& arc);
Environment LevelEnvironment(sectortype* sector);
bool ActivateLevelSector(sectortype* sector, DCoreActor* activator, bool use = true);
bool EntityDied(DCoreActor* actor, DCoreActor* killer);
bool RequestDukeDeath(Duke3d::DDukeActor* actor);
void FinishDukeLifecycle(Duke3d::DDukeActor* actor);
bool EntityPickedUp(DCoreActor* actor, DCoreActor* collector);
bool IsRuntimeActor(DCoreActor* actor);
DCoreActor* ResolveGameplayActor(DCoreActor* actor);
bool RuntimeTarget(DCoreActor* actor, TargetInfo& result);
void ForEachRuntimeTarget(const std::function<void(const TargetInfo&)>& visitor);
DCoreActor* SpawnRuntimeEntity(Archetype type, sectortype* sector, const DVector3& feet, DAngle angle);
void TickRuntimeLevel();
bool AnimateRuntimeEntity(tspritetype* sprite, const DVector2& view, double fraction);
Duke3d::ConExecutionServices* RuntimeConServices(Duke3d::DDukeActor* actor);
bool RuntimeDukeEnvironment(Duke3d::DDukeActor* actor, int& medium);
int ActorMedium(Duke3d::DDukeActor* actor, sectortype* sector);
bool SpawnRuntimeDukeObject(Duke3d::DDukeActor* source, PClassActor* type, Duke3d::DDukeActor*& result);
bool IsRuntimeDukeEffect(Duke3d::DDukeActor* actor);
}
