#pragma once

#include "units.h"
#include "vectors.h"

namespace Duke3d { class DDukeActor; }
class DCoreActor;
class DCorePlayer;
class PClassActor;

namespace Superset
{
// Blood already distinguishes these health-damage responses. Status changes
// and impulses are separate operations; Heat does not implicitly ignite.
enum class DamageKind { Impact, Heat, Physical, Blast, Asphyxiation, Spirit, Electric, Cold };

struct Damage
{
	Units::Health amount;
	DamageKind kind;
};

bool BloodDamageKind(int nativeKind, DamageKind& kind);
bool NativeBloodDamageKind(DamageKind kind, int& nativeKind);
Damage BloodTineDamage(bool playerTarget);
Damage BloodVectorDamage(int vectorType, bool playerTarget);

// Health only: no implicit impulse, ignition, freezing or shrinking. True means
// the receiver handled the request, including native immunity/rejection.
// Native Blood accepts its seven categories and retains native resistance.
// Cold health damage needs an explicit native Blood mapping. Optional applied
// measures accepted Q4 health damage, including Duke's deferred native queue.
bool ApplyHealthDamage(DCoreActor* target, Damage damage, DCoreActor* source, Units::Health* applied = nullptr);
bool ReadDukeHitDamage(Duke3d::DDukeActor* hitter, Damage& damage);
bool ApplyDukeHealthHit(DCoreActor* target, Duke3d::DDukeActor* hitter, DCoreActor* source);

// Duke receivers accept shared health categories through the native onHit path.
// These payloads carry no weapon-specific status reaction or implicit impulse.
bool PrepareDukeHit(Duke3d::DDukeActor* hitter, Damage damage, bool impulse = true);
bool ApplyDukeHealthDamage(Duke3d::DDukeActor* target, Damage damage, DCoreActor* source);
bool AccumulateDukeHit(Duke3d::DDukeActor* target, Duke3d::DDukeActor* hitter);
void FinishDukeHit(Duke3d::DDukeActor* target);

bool DukeDamageKind(PClassActor* type, DamageKind& kind);
bool ApplyCalebDamage(DCorePlayer* player, Damage damage, DCoreActor* source);
void ApplyCalebImpulse(DCorePlayer* player, const DVector2& impulse);
bool ReceiveCalebHit(Duke3d::DDukeActor* target, Duke3d::DDukeActor* hitter);
bool ConsumeCalebHit(Duke3d::DDukeActor* target);
}
