#pragma once

#include "units.h"

namespace Duke3d { class DDukeActor; }

namespace Superset
{
// Blood already distinguishes these health-damage responses. Status changes
// and impulses are separate operations; Heat does not implicitly ignite.
enum class DamageKind { Impact, Heat, Physical, Blast, Asphyxiation, Spirit, Electric };

struct Damage
{
	Units::Health amount;
	DamageKind kind;
};

bool BloodDamageKind(int nativeKind, DamageKind& kind);
bool NativeBloodDamageKind(DamageKind kind, int& nativeKind);
Damage BloodTineDamage(bool playerTarget);

// The first Duke receiver handles physical hits through the native onHit path.
// Unsupported categories return false instead of acquiring invented reactions.
bool PrepareDukeHit(Duke3d::DDukeActor* hitter, Damage damage);
bool AccumulateDukeHit(Duke3d::DDukeActor* target, Duke3d::DDukeActor* hitter);
void FinishDukeHit(Duke3d::DDukeActor* target);
}
