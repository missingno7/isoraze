#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif
#include "../duke/src/duke3d.h"
#include "../duke/src/dukeactor.h"
#include "../blood/src/blood.h"
#include "damage.h"
#include "mashup_player.h"
#include "c_dispatch.h"

namespace Duke3d { void TickActor(DDukeActor*); }

namespace Superset
{
bool BloodDamageKind(int nativeKind, DamageKind& kind)
{
	switch (nativeKind)
	{
	case Blood::kDamageFall: kind = DamageKind::Impact; break;
	case Blood::kDamageBurn: kind = DamageKind::Heat; break;
	case Blood::kDamageBullet: kind = DamageKind::Physical; break;
	case Blood::kDamageExplode: kind = DamageKind::Blast; break;
	case Blood::kDamageDrown: kind = DamageKind::Asphyxiation; break;
	case Blood::kDamageSpirit: kind = DamageKind::Spirit; break;
	case Blood::kDamageTesla: kind = DamageKind::Electric; break;
	default: return false;
	}
	return true;
}

bool NativeBloodDamageKind(DamageKind kind, int& nativeKind)
{
	for (int n = 0; n < Blood::kDamageMax; ++n)
	{
		DamageKind mapped;
		if (BloodDamageKind(n, mapped) && mapped == kind) { nativeKind = n; return true; }
	}
	return false;
}

Damage BloodTineDamage(bool playerTarget)
{
	auto& vector = Blood::gVectorData[Blood::kVectorTine];
	Damage damage {-1, DamageKind::Physical};
	if (!BloodDamageKind(vector.dmgType, damage.kind)) return damage;
	// Native actFireVector uses Q4 points and halves tine damage to non-players.
	damage.amount = Units::FromBloodHealth(vector.dmg << (playerTarget ? 4 : 3));
	return damage;
}

bool PrepareDukeHit(Duke3d::DDukeActor* hitter, Damage damage)
{
	if (damage.kind != DamageKind::Physical || damage.amount < 0 ||
		damage.amount > Units::FromDukeHealth(INT16_MAX)) return false;
	hitter->supersetHitDamage = damage.amount;
	hitter->spr.extra = int(damage.amount / Units::HealthPerPoint);
	return true;
}

bool AccumulateDukeHit(Duke3d::DDukeActor* target, Duke3d::DDukeActor* hitter)
{
	if (hitter->supersetHitDamage < 0) return false;
	// Only reached after native self-hit, size and friendly-fire rejection.
	// Integer CON health remains a facade: carry every unrepresentable fraction
	// to this receiver's next accepted hit rather than dropping it per tine.
	auto amount = hitter->supersetHitDamage + target->supersetDamageRemainder;
	target->supersetDamageRemainder = uint8_t(amount % Units::HealthPerPoint);
	target->supersetPendingDamage = true;
	// The native -1 means "no pending hit", not one point of damage resistance.
	target->hitextra = max(0, int(target->hitextra)) + int(amount / Units::HealthPerPoint);
	return true;
}

void FinishDukeHit(Duke3d::DDukeActor* target)
{
	if (!target->supersetPendingDamage) return;
	target->supersetPendingDamage = false;
	if (target->spr.extra <= 0)
	{
		// Blood/common zero health is lethal. Native enemy CON tests health < 0;
		// player CON subtracts one before testing, so its lethal facade stays 0.
		target->spr.extra = target->isPlayer() ? 0 : -1;
		target->supersetDamageRemainder = 0;
	}
}

namespace
{
constexpr int ProbeStat = MAXSTATUS - 2;
constexpr int ProbeTag = 31017;

Duke3d::DDukeActor* ProbeTarget(Duke3d::DDukeActor* owner)
{
	auto target = Duke3d::CreateActor(owner->sector(), owner->spr.pos,
		PClass::FindActor("DukeLizTrooper"), 0, DVector2(1, 1), nullAngle, 0, 0, owner, STAT_MISC);
	if (target)
	{
		ChangeActorStat(target, ProbeStat);
		target->spr.cstat = CSTAT_SPRITE_INVISIBLE;
		target->flags3 |= SFLAG3_NOHITJIBS;
		target->flags2 |= SFLAG2_NODAMAGEPUSH;
		target->spr.extra = 100;
		target->spr.hitag = ProbeTag;
	}
	return target;
}

bool Hit(Duke3d::DDukeActor* target, Duke3d::DDukeActor* hitter)
{
	if (!PrepareDukeHit(hitter, BloodTineDamage(false))) return false;
	Duke3d::checkhitsprite(target, hitter);
	return Duke3d::fi.ifhitbyweapon(target) == 0;
}

bool NativeDeath(Duke3d::DDukeActor* owner, Duke3d::DDukeActor* hitter)
{
	TArray<Duke3d::DDukeActor*> previous;
	Duke3d::DukeSpriteIterator before;
	while (auto actor = before.Next()) previous.Push(actor);
	auto target = ProbeTarget(owner);
	if (!target) return false;
	target->spr.extra = 8;
	PrepareDukeHit(hitter, BloodTineDamage(false));
	Duke3d::checkhitsprite(target, hitter);
	ChangeActorStat(target, STAT_ACTOR);
	// Let the enemy's actual CON consume the hit and choose its death sequence.
	Duke3d::TickActor(target);
	bool pass = !(target->ObjectFlags & OF_EuthanizeMe) && !target->supersetPendingDamage &&
		(target->curAction->name == FName("ATROOPDYING") || target->curAction->name == FName("ATROOPSUFFERING"));
	// Remove the test enemy and any native drops/gibs created synchronously.
	Duke3d::DukeSpriteIterator after;
	while (auto actor = after.Next()) if (!previous.Contains(actor)) actor->Destroy();
	return pass;
}
}

CCMD(superset_damage_check)
{
	if (!Mashup::CombatEnabled() || gamestate != GS_LEVEL) return;
	auto owner = Duke3d::getPlayer(myconnectindex)->GetActor();
	auto hitter = Duke3d::CreateActor(owner->sector(), owner->spr.pos, Duke3d::DukeMeleeAttackClass,
		0, DVector2(1, 1), nullAngle, 0, 0, owner, STAT_MISC);
	if (!hitter) { Printf("Superset damage check: FAIL (no hitter).\n"); return; }
	ChangeActorStat(hitter, ProbeStat);
	if (argv.argc() > 1)
	{
		Duke3d::DDukeActor* saved = nullptr;
		Duke3d::DukeStatIterator it(ProbeStat);
		while (auto actor = it.Next())
			if (actor->spr.hitag == ProbeTag) { saved = actor; break; }
		if (!stricmp(argv[1], "seed"))
		{
			if (saved) saved->Destroy();
			saved = ProbeTarget(owner);
			if (saved) Hit(saved, hitter);
		}
		else if (!stricmp(argv[1], "clear")) { if (saved) saved->Destroy(); }
		else if (!stricmp(argv[1], "saved"))
		{
			bool pass = saved && saved->spr.extra == 92 && saved->supersetDamageRemainder == 8 &&
				Hit(saved, hitter) && saved->spr.extra == 83 && saved->supersetDamageRemainder == 0;
			Printf("Superset damage save check: %s.\n", pass ? "PASS" : "FAIL");
			if (saved) saved->Destroy();
		}
		hitter->Destroy();
		return;
	}
	auto target = ProbeTarget(owner);
	if (!target) { hitter->Destroy(); Printf("Superset damage check: FAIL (no target).\n"); return; }
	bool nativeDefaults = hitter->supersetHitDamage == -1 && target->supersetHitDamage == -1 &&
		target->supersetDamageRemainder == 0 && !target->supersetPendingDamage;
	bool categories = true;
	for (int n = 0; n < Blood::kDamageMax; ++n)
	{
		DamageKind kind;
		int restored = -1;
		categories &= BloodDamageKind(n, kind) && NativeBloodDamageKind(kind, restored) && restored == n;
	}
	DamageKind unused;
	categories &= !BloodDamageKind(-1, unused) && !BloodDamageKind(Blood::kDamageMax, unused);
	bool fractions = Hit(target, hitter) && target->spr.extra == 92 && target->supersetDamageRemainder == 8;
	fractions &= Hit(target, hitter) && target->spr.extra == 83 && target->supersetDamageRemainder == 0;
	bool ownership = target->GetHitOwner() == owner && target->attackertype == Duke3d::DukeMeleeAttackClass;
	// Rejected hits must not donate a fraction to a subsequent accepted hit.
	PrepareDukeHit(hitter, BloodTineDamage(false));
	target->spr.cstat = CSTAT_SPRITE_ALIGNMENT_WALL;
	target->spr.hitag = 0;
	ChangeActorStat(target, STAT_DEFAULT);
	Duke3d::checkhitsprite(target, hitter);
	bool rejection = target->hitextra == -1 && target->supersetDamageRemainder == 0;
	target->spr.cstat = CSTAT_SPRITE_INVISIBLE;
	target->spr.hitag = ProbeTag;
	ChangeActorStat(target, ProbeStat);
	hitter->SetOwner(target);
	Duke3d::checkhitsprite(target, hitter);
	rejection &= target->hitextra == -1 && target->supersetDamageRemainder == 0;
	hitter->SetOwner(owner);
	// Retain native attack modifiers without losing half-point damage.
	target->spr.extra = 100;
	hitter->flags4 |= SFLAG4_DOUBLEHITDAMAGE;
	bool modifier = Hit(target, hitter) && target->spr.extra == 83 && target->supersetDamageRemainder == 0;
	hitter->flags4 &= ~SFLAG4_DOUBLEHITDAMAGE;
	target->spr.extra = 8;
	bool lethal = Hit(target, hitter) && target->spr.extra == -1 && target->supersetDamageRemainder == 0;
	PrepareDukeHit(hitter, BloodTineDamage(false));
	Duke3d::checkhitsprite(target, hitter);
	lethal &= Duke3d::fi.ifhitbyweapon(target) == -1 && !target->supersetPendingDamage && target->supersetDamageRemainder == 0;
	target->spr.extra = 8;
	PrepareDukeHit(hitter, {Units::FromDukeHealth(8), DamageKind::Physical});
	Duke3d::checkhitsprite(target, hitter);
	lethal &= Duke3d::fi.ifhitbyweapon(target) == 0 && target->spr.extra == -1;
	// A native hit without a shared payload follows its original arithmetic.
	target->spr.extra = 100;
	hitter->supersetHitDamage = -1;
	hitter->spr.extra = 8;
	Duke3d::checkhitsprite(target, hitter);
	bool native = nativeDefaults && target->hitextra == 7 && Duke3d::fi.ifhitbyweapon(target) == 0 && target->spr.extra == 93;
	bool death = NativeDeath(owner, hitter);
	Printf("Superset damage check: categories=%s, fractions=%s, native ownership=%s, rejection=%s, modifiers=%s, lethal=%s, native route=%s, CON death=%s.\n",
		categories ? "PASS" : "FAIL", fractions ? "PASS" : "FAIL", ownership ? "PASS" : "FAIL", rejection ? "PASS" : "FAIL",
		modifier ? "PASS" : "FAIL", lethal ? "PASS" : "FAIL", native ? "PASS" : "FAIL", death ? "PASS" : "FAIL");
	target->Destroy();
	hitter->Destroy();
}
}
