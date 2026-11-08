#pragma once
#include "vectors.h"
#include <functional>

class DCoreActor;
class DCorePlayer;
class PClassActor;
struct HitInfoBase;
namespace Duke3d { class DDukeActor; class DDukePlayer; }
namespace Blood { class DBloodPlayer; struct PlayerWeaponServices; }

namespace Superset
{
// Geometry and native target state consumed by Duke's existing aim algorithm.
// Artwork repeats are not a cross-game measure of shrinking or body height.
struct DukeAimCandidate
{
	DCoreActor* actor;
	bool enemy, player, alive, shrinkRejected, frozen;
	double sightOffset;
};
bool IsDukeWeaponActor(Duke3d::DDukeActor* actor);
bool DukeWeaponSupported(int weapon);
bool IsBloodDirectAimTarget(DCoreActor* actor);
void ForEachBloodAimTarget(int group, const std::function<void(const DukeAimCandidate&)>& visitor);
Blood::PlayerWeaponServices* BloodMapWeaponServices(Blood::DBloodPlayer* player);
bool DrawBloodMapDukeWeapons(Blood::DBloodPlayer* player, double fraction);
// -2: native weapon family; -1: foreign weapon without ammunition.
int BloodMapForeignWeaponAmmo(Blood::DBloodPlayer* player);
void ClearBloodMapDukeWeapons();
void ResetBloodMapDukeWeapons();
bool IsDukeWeaponEffect(Duke3d::DDukeActor* actor);
bool SpawnDukeWeaponEffect(Duke3d::DDukeActor* source, PClassActor* type, Duke3d::DDukeActor*& result);
}
namespace Duke3d
{
DCoreActor* AimHitscanTarget(DDukeActor* actor, DDukeActor* weapon, double angle, bool* direct);
}
