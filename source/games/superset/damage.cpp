#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif
#include "../duke/src/duke3d.h"
#include "../duke/src/dukeactor.h"
#include "../blood/src/blood.h"
#include "damage.h"
#include "superset.h"
#include "enemies.h"
#include "level.h"
#include "mashup_player.h"
#include "c_dispatch.h"
#include "vm.h"

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
	return BloodVectorDamage(Blood::kVectorTine, playerTarget);
}

Damage BloodVectorDamage(int vectorType, bool playerTarget)
{
	Damage damage {-1, DamageKind::Physical};
	if (vectorType < 0 || vectorType >= Blood::kVectorMax) return damage;
	auto& vector = Blood::gVectorData[vectorType];
	if (!BloodDamageKind(vector.dmgType, damage.kind)) return damage;
	// Native actFireVector uses Q4 points and halves tine damage to non-players.
	damage.amount = Units::FromBloodHealth(vector.dmg << (vectorType == Blood::kVectorTine && !playerTarget ? 3 : 4));
	return damage;
}

static bool ApplyHealthDamageImpl(DCoreActor* target, Damage damage, DCoreActor* source)
{
	if (!IsActive() || !target || (target->ObjectFlags & OF_EuthanizeMe) ||
		(source && (source->ObjectFlags & OF_EuthanizeMe)) ||
		// Match the existing Duke hit payload range; do not feed arbitrary int64
		// amounts into native signed-int resistance, armor and death arithmetic.
		damage.amount < 0 || damage.amount > Units::FromDukeHealth(INT16_MAX) ||
		unsigned(damage.kind) > unsigned(DamageKind::Cold)) return false;
	if (auto native = dyn_cast<Blood::DBloodActor>(target))
	{
		auto nativeSource = BloodState(source);
		int kind;
		if (!native->hasX() || !NativeBloodDamageKind(damage.kind, kind)) return false;
		if (native->spr.statnum == Blood::kStatDude)
		{
			if (!native->IsDudeActor()) return false;
		}
		else if (native->spr.statnum != Blood::kStatThing ||
			native->spr.type < Blood::kThingBase || native->spr.type >= Blood::kThingMax) return false;
		// A zero request must not retarget AI or acquire Tesla/heat reactions.
		if (damage.amount)
		{
			native->supersetDamageSource = source && !nativeSource ? source : nullptr;
			GC::WriteBarrier(native, native->supersetDamageSource.Get());
			Blood::actDamageSprite(nativeSource, native, Blood::DAMAGE_TYPE(kind), int(damage.amount));
		}
		return true;
	}
	if (HasBloodEnemy(target)) return ApplyBloodEnemyDamage(target, damage, source);
	auto duke = dyn_cast<Duke3d::DDukeActor>(target);
	if (!duke) return false;
	if (duke->isPlayer())
	{
		auto player = Duke3d::getPlayer(duke->PlayerIndex());
		if (HasBloodMovement(player)) return ApplyCalebDamage(player, damage, source);
	}
	return ApplyDukeHealthDamage(duke, damage, source);
}

bool ApplyHealthDamage(DCoreActor* target, Damage damage, DCoreActor* source, Units::Health* applied)
{
	// Damage-dependent ammo/healing needs the accepted amount, including Duke's
	// deferred hit queue. Measuring only CON health here would report zero.
	auto remaining = [&]() -> Units::Health
	{
		auto blood = dyn_cast<Blood::DBloodActor>(target);
		if (!blood && HasBloodEnemy(target)) blood = BloodState(target);
		if (auto duke = dyn_cast<Duke3d::DDukeActor>(target))
		{
			if (duke->isPlayer() && HasBloodMovement(Duke3d::getPlayer(duke->PlayerIndex()))) blood = BloodState(target);
			if (blood && blood->hasX()) return max(0, int(blood->xspr.health));
			return max<Units::Health>(0, Units::FromDukeHealth(duke->spr.extra - max(0, int(duke->hitextra))) - duke->supersetDamageRemainder);
		}
		if (blood && blood->hasX()) return max(0, int(blood->xspr.health));
		return 0;
	};
	auto before = applied ? remaining() : 0;
	bool accepted = ApplyHealthDamageImpl(target, damage, source);
	if (applied) *applied = accepted ? max<Units::Health>(0, before - remaining()) : 0;
	return accepted;
}

bool HealHealth(DCoreActor* target, Units::Health amount, Units::Health cap)
{
	if (!IsActive() || !target || amount <= 0 || amount > Units::FromDukeHealth(INT16_MAX) || cap <= 0 || cap > Units::FromDukeHealth(INT16_MAX)) return false;
	auto duke = dyn_cast<Duke3d::DDukeActor>(target);
	auto blood = BloodState(target);
	if (duke && duke->isPlayer() && !HasBloodMovement(Duke3d::getPlayer(duke->PlayerIndex()))) blood = nullptr;
	if (blood && blood->hasX())
	{
		int before = blood->xspr.health;
		if (before <= 0 || before >= cap) return false;
		blood->xspr.health = int(min<Units::Health>(before + amount, cap));
		return true;
	}
	if (!duke || duke->spr.extra <= 0) return false;
	auto health = Units::FromDukeHealth(duke->spr.extra) - duke->supersetDamageRemainder;
	if (health >= cap) return false;
	health = min(health + amount, cap);
	duke->spr.extra = int((health + Units::HealthPerPoint - 1) / Units::HealthPerPoint);
	duke->supersetDamageRemainder = int(Units::FromDukeHealth(duke->spr.extra) - health);
	return true;
}

bool ReadDukeHitDamage(Duke3d::DDukeActor* hitter, Damage& damage)
{
	if (!IsActive() || !hitter || (hitter->ObjectFlags & OF_EuthanizeMe)) return false;
	if (hitter->supersetHitDamage >= 0)
	{
		damage = { hitter->supersetHitDamage, DamageKind(hitter->supersetHitKind) };
		return damage.amount <= INT_MAX && unsigned(damage.kind) <= unsigned(DamageKind::Cold);
	}
	if (!DukeDamageKind(static_cast<PClassActor*>(hitter->GetClass()), damage.kind)) return false;
	damage.amount = Units::FromDukeHealth(max(0, int(hitter->spr.extra)));
	return true;
}

bool ApplyDukeHealthHit(DCoreActor* target, Duke3d::DDukeActor* hitter, DCoreActor* source)
{
	Damage damage;
	return ReadDukeHitDamage(hitter, damage) && ApplyHealthDamage(target, damage, source);
}

DEFINE_ACTION_FUNCTION(DCoreActor, ApplySharedHealthDamage)
{
	PARAM_SELF_PROLOGUE(DCoreActor);
	PARAM_INT(kind);
	PARAM_INT(q4Amount);
	PARAM_OBJECT(source, DCoreActor);
	ACTION_RETURN_BOOL(ApplyHealthDamage(self, { q4Amount, DamageKind(kind) }, source));
}

DEFINE_ACTION_FUNCTION(DCoreActor, ApplySharedDukeHealthHit)
{
	PARAM_SELF_PROLOGUE(DCoreActor);
	PARAM_OBJECT(hitter, Duke3d::DDukeActor);
	PARAM_OBJECT(source, DCoreActor);
	ACTION_RETURN_BOOL(ApplyDukeHealthHit(self, hitter, source));
}

bool PrepareDukeHit(Duke3d::DDukeActor* hitter, Damage damage, bool impulse)
{
	if (unsigned(damage.kind) > unsigned(DamageKind::Cold) || damage.amount < 0 ||
		damage.amount > Units::FromDukeHealth(INT16_MAX)) return false;
	hitter->supersetHitDamage = damage.amount;
	hitter->supersetHitKind = uint8_t(damage.kind);
	hitter->supersetDamageOnly = !impulse;
	hitter->spr.extra = int(damage.amount / Units::HealthPerPoint);
	return true;
}

bool ApplyDukeHealthDamage(Duke3d::DDukeActor* target, Damage damage, DCoreActor* source)
{
	if (HasBloodEnemy(target)) return ApplyBloodEnemyDamage(target, damage, source);
	if (!target || target->spr.extra <= 0 || damage.amount < 0 ||
		damage.amount > Units::FromDukeHealth(INT16_MAX)) return false;
	if (damage.kind == DamageKind::Heat)
	{
		double factor = target->FloatVar(FName("supersetheatfactor"));
		if (!std::isfinite(factor) || factor < 0 || factor > 256) return false;
		damage.amount = Units::Health(damage.amount * factor);
		if (damage.amount == 0) return true;
	}
	auto owner = dyn_cast<Duke3d::DDukeActor>(ResolveAttackInstigator(source));
	if (!owner) owner = dyn_cast<Duke3d::DDukeActor>(Mashup::WorldSoundActor(source));
	auto hitter = Duke3d::CreateActor(target->sector(), target->spr.pos, static_cast<PClassActor*>(RUNTIME_CLASS(Duke3d::DDukeActor)),
		0, DVector2(0, 0), nullAngle, 0, 0, owner, STAT_MISC);
	if (!hitter) return false;
	ChangeActorStat(hitter, MAXSTATUS - 2);
	hitter->supersetAttackSource = source; GC::WriteBarrier(hitter, source);
	hitter->flags3 |= SFLAG3_NOHITJIBS;
	bool prepared = PrepareDukeHit(hitter, damage, false);
	if (prepared) Duke3d::checkhitsprite(target, hitter);
	hitter->Destroy();
	return prepared;
}

bool AccumulateDukeHit(Duke3d::DDukeActor* target, Duke3d::DDukeActor* hitter)
{
	if (IsRuntimeActor(target))
	{
		auto source = hitter->supersetAttackSource ? hitter->supersetAttackSource.Get() : hitter->GetOwner();
		target->gameplayEntity->damageSource = ResolveGameplayActor(source);
		GC::WriteBarrier(target->gameplayEntity, target->gameplayEntity->damageSource.Get());
	}
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

bool DukeDamageKind(PClassActor* type, DamageKind& kind)
{
	if (!type || !type->IsDescendantOf(RUNTIME_CLASS(Duke3d::DDukeActor))) return false;
	auto defaults = static_cast<Duke3d::DDukeActor*>(GetDefaultByType(type));
	if ((defaults->flags2 & SFLAG2_FREEZEDAMAGE) || type->IsDescendantOf(Duke3d::DukeShrinkSparkClass) ||
		type->IsDescendantOf(Duke3d::DukeGrowSparkClass)) return false;
	if (defaults->flags2 & SFLAG2_EXPLOSIVE) kind = DamageKind::Blast;
	else if (type->IsDescendantOf(Duke3d::DukeShotSparkClass) || type->IsDescendantOf(Duke3d::DukeMeleeAttackClass) ||
		type->IsDescendantOf(Duke3d::DukeFirelaserClass)) kind = DamageKind::Physical;
	else return false; // Flames and mod-specific attacks need explicit mappings.
	return true;
}

namespace
{
void DukeImpulse(Duke3d::DDukePlayer* player, DAngle angle, Units::Health amount, const Duke3d::DDukeActor* defaults)
{
	// Both native horizontal movers consume world displacement once per step.
	// Apply impulse independently of armor, resistance or health damage.
	double scale = defaults->flags2 & SFLAG2_DOUBLEDMGTHRUST ? .25 : .125;
	ApplyCalebImpulse(player, angle.ToVector() * Units::HealthPoints(amount) * scale);
}
}

bool ReceiveCalebHit(Duke3d::DDukeActor* target, Duke3d::DDukeActor* hitter)
{
	if (!target->isPlayer()) return false;
	auto player = Duke3d::getPlayer(target->PlayerIndex());
	if (!HasBloodMovement(player)) return false;
	Damage damage;
	if (!ReadDukeHitDamage(hitter, damage)) return false;
	if (Duke3d::ud.god) return true;
	if (!ApplyHealthDamage(target, damage, hitter->GetOwner())) return false;
	if (!hitter->supersetDamageOnly) DukeImpulse(player, hitter->spr.Angles.Yaw, damage.amount, hitter);
	return true;
}

bool ConsumeCalebHit(Duke3d::DDukeActor* target)
{
	auto player = Duke3d::getPlayer(target->PlayerIndex());
	if (!HasBloodMovement(player)) return false;
	DamageKind kind;
	if (!DukeDamageKind(target->attackertype, kind)) return false;
	auto amount = Units::FromDukeHealth(target->hitextra);
	// Remove the native queue before importing other world changes, so the
	// shared receiver cannot recursively consume this same hit.
	target->hitextra = -1;
	if (Duke3d::ud.god) return true;
	ApplyCalebDamage(player, {amount, kind}, target->GetHitOwner());
	DukeImpulse(player, target->hitang, amount, target->attackerDefaults());
	return true;
}

// Exercises the public VM boundary against live native Blood receivers. These
// fixtures never run a second world ticker or leave a foreign collision body.
CCMD(superset_health_check)
{
	if (!IsBloodMap() || gamestate != GS_LEVEL || numplayers != 1) return;
	auto player = Blood::getPlayer(myconnectindex);
	auto owner = player->GetActor();
	auto spawn = [&]() { return Blood::actSpawnDude(owner, Blood::kDudeZombieAxeNormal, 40); };
	auto remove = [](Blood::DBloodActor* actor)
	{
		if (actor)
		{
			Blood::seqKill(actor); Blood::evKillActor(actor);
			actor->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
			// Native deaths may already be in gPost. Update that queue instead of
			// destroying an actor that the end-of-step transfer still references.
			Blood::actPostSprite(actor, Blood::kStatFree);
		}
	};
	auto hitter = Duke3d::CreateActor(owner->sector(), owner->spr.pos, Duke3d::DukeShotSparkClass,
		0, DVector2(0, 0), nullAngle, 0, 0, nullptr, STAT_MISC);
	if (!hitter) { Printf("Superset shared Blood health check: FAIL (no hit payload).\n"); return; }
	ChangeActorStat(hitter, MAXSTATUS - 2);
	auto vmHit = [&](DCoreActor* target)
	{
		auto function = PClass::FindFunction("CoreActor", "ApplySharedDukeHealthHit");
		if (!function) return false;
		int result = 0;
		VMValue params[] = { target, hitter, owner };
		VMReturn ret; ret.IntAt(&result);
		VMCall(function, params, 3, &ret, 1);
		return result != 0;
	};
	auto vmHealth = [&](DCoreActor* target, Damage damage)
	{
		auto function = PClass::FindFunction("CoreActor", "ApplySharedHealthDamage");
		if (!function) return false;
		int result = 0;
		VMValue params[] = { target, int(damage.kind), int(damage.amount), owner };
		VMReturn ret; ret.IntAt(&result);
		VMCall(function, params, 4, &ret, 1);
		return result != 0;
	};
	const int fraction = MulScale(136, Blood::getDudeInfo(Blood::kDudeZombieAxeNormal)->damageVal[Blood::kDamageBullet], 8);
	if (argv.argc() > 1)
	{
		Blood::DBloodActor* saved = nullptr;
		Blood::BloodSpriteIterator it;
		while (auto actor = it.Next()) if (actor->spr.intowner == 31033) { saved = actor; break; }
		PrepareDukeHit(hitter, {136, DamageKind::Physical}, false);
		if (!stricmp(argv[1], "seed"))
		{
			if (saved) saved->spr.flags &= ~32; // Fixture hold, not a queued transfer.
			remove(saved); saved = spawn();
			if (saved)
			{
				saved->spr.intowner = 31033; saved->xspr.health = 16000;
				vmHit(saved); saved->spr.flags |= 32;
				saved->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
			}
		}
		else if (!stricmp(argv[1], "saved"))
		{
			bool pass = saved && saved->xspr.health == 16000 - fraction;
			if (saved)
			{
				saved->spr.flags &= ~32;
				pass &= vmHit(saved) && saved->xspr.health == 16000 - fraction * 2 && saved->GetTarget() == owner;
			}
			Printf("Superset shared Blood health save check: %s.\n", pass ? "PASS" : "FAIL");
			remove(saved);
		}
		else if (!stricmp(argv[1], "clear")) { if (saved) saved->spr.flags &= ~32; remove(saved); }
		hitter->Destroy();
		return;
	}
	TArray<Blood::DBloodActor*> previous;
	Blood::BloodSpriteIterator before;
	while (auto actor = before.Next()) previous.Push(actor);
	auto target = spawn();
	if (!target) { hitter->Destroy(); Printf("Superset shared Blood health check: FAIL (no target).\n"); return; }
	target->xspr.health = 16000;
	hitter->spr.extra = 9;
	bool hit = vmHit(target) && target->xspr.health == 16000 - MulScale(144,
		Blood::getDudeInfo(target->spr.type)->damageVal[Blood::kDamageBullet], 8) && target->GetTarget() == owner;
	target->xspr.health = 16000;
	PrepareDukeHit(hitter, {136, DamageKind::Physical}, false);
	bool fractions = vmHit(target) && vmHit(target) && target->xspr.health == 16000 - fraction * 2;
	bool categories = true;
	for (int kind = 0; kind < Blood::kDamageMax; ++kind)
	{
		DamageKind shared;
		BloodDamageKind(kind, shared);
		target->xspr.health = 16000;
		int expected = MulScale(128, Blood::getDudeInfo(target->spr.type)->damageVal[kind], 8);
		categories &= vmHealth(target, {128, shared}) && target->xspr.health == 16000 - expected;
	}
	bool electric = target->dudeExtra.teslaHit != 0;
	bool separate = target->xspr.burnTime == 0; // Heat did not request ignition.
	auto velocity = target->vel;
	vmHit(target);
	separate &= target->vel == velocity;
	target->xspr.health = 16000;
	target->spr.flags |= 32;
	bool blocked = vmHit(target) && target->xspr.health == 16000;
	target->spr.flags &= ~32;
	target->xspr.locked = 1;
	blocked &= vmHit(target) && target->xspr.health == 16000;
	target->xspr.locked = 0;
	target->dudeExtra.teslaHit = 0;
	bool rejection = ApplyHealthDamage(target, {0, DamageKind::Electric}, owner) && !target->dudeExtra.teslaHit &&
		!ApplyHealthDamage(target, {-1, DamageKind::Physical}, owner) &&
		!ApplyHealthDamage(target, {Units::FromDukeHealth(INT16_MAX) + 1, DamageKind::Physical}, owner) &&
		!ApplyHealthDamage(target, {Units::Health(INT_MAX) + 1, DamageKind::Physical}, owner) &&
		!ApplyHealthDamage(target, {128, DamageKind(255)}, owner) &&
		!ApplyHealthDamage(target, {128, DamageKind::Cold}, owner) && target->xspr.health == 16000;
	// Foreign attackers no longer need a Blood shadow actor. The native
	// receiver still applies its resistance, and retains the real core source.
	bool foreignSource = ApplyHealthDamage(target, {128, DamageKind::Physical}, hitter) &&
		target->xspr.health == 16000 - MulScale(128, Blood::getDudeInfo(target->spr.type)->damageVal[Blood::kDamageBullet], 8) &&
		target->supersetDamageSource == hitter;
	rejection &= foreignSource;
	DamageKind kind;
	rejection &= !DukeDamageKind(Duke3d::DukeFreezeBlastClass, kind) &&
		!DukeDamageKind(Duke3d::DukeShrinkSparkClass, kind) && !DukeDamageKind(Duke3d::DukeGrowSparkClass, kind);
	bool blast = DukeDamageKind(Duke3d::DukeRPGClass, kind) && kind == DamageKind::Blast;
	int armor = player->armor[0], health = owner->xspr.health;
	player->armor[0] = 3200;
	bool armored = ApplyHealthDamage(owner, {128, DamageKind::Physical}, owner) &&
		int(owner->xspr.health) < health && health - int(owner->xspr.health) < 128 && player->armor[0] < 3200;
	owner->xspr.health = health; player->armor[0] = armor;
	auto thing = Blood::actSpawnThing(owner->sector(), owner->spr.pos, Blood::kThingTNTBarrel);
	bool things = thing && ApplyHealthDamage(thing, {16, DamageKind::Physical}, owner) && thing->xspr.health < 400;
	remove(thing);
	auto kills = Level.kills;
	target->xspr.health = 1;
	bool death = vmHit(target) && target->xspr.health == 0 && Blood::seqGetStatus(target) >= 0 &&
		Level.kills.got == kills.got + 1 && Level.kills.player[myconnectindex] == kills.player[myconnectindex] + 1;
	Level.kills = kills;
	Printf("Superset shared Blood health check: Duke hit/VM=%s, Q4=%s, categories/resistance=%s, electric=%s, separate effects=%s, blocked=%s, rejection=%s, blast mapping=%s, armor=%s, things=%s, native death/credit=%s.\n",
		hit ? "PASS" : "FAIL", fractions ? "PASS" : "FAIL", categories ? "PASS" : "FAIL", electric ? "PASS" : "FAIL",
		separate ? "PASS" : "FAIL", blocked ? "PASS" : "FAIL", rejection ? "PASS" : "FAIL", blast ? "PASS" : "FAIL",
		armored ? "PASS" : "FAIL", things ? "PASS" : "FAIL", death ? "PASS" : "FAIL");
	// Death callbacks may create native drops or gibs synchronously.
	Blood::BloodSpriteIterator after;
	while (auto actor = after.Next()) if (!previous.Contains(actor)) remove(actor);
	hitter->Destroy();
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

CCMD(superset_receiver_check)
{
	if (!IsDukeMap() || gamestate != GS_LEVEL || numplayers != 1) return;
	auto source = Duke3d::getPlayer(myconnectindex)->GetActor();
	bool pass = true;
	for (const char* name : {"DukePigCop", "DukeLizTrooper", "DukeLizMan", "DukeOctabrain"})
	{
		auto target = Duke3d::CreateActor(source->sector(), source->spr.pos, PClass::FindActor(name),
			0, DVector2(1, 1), nullAngle, 0, 0, source, STAT_MISC);
		if (!target) { pass = false; continue; }
		ChangeActorStat(target, ProbeStat);
		target->spr.cstat = CSTAT_SPRITE_INVISIBLE;
		target->spr.extra = 100;
		TargetInfo info;
		bool found = false;
		ForEachGameplayTarget([&](const TargetInfo& candidate) { found |= candidate.actor == target && candidate.alive; });
		pass &= !target->gameplayEntity && GameplayTarget(target, info) && info.alive && found;
		for (int kind = 0; kind <= int(DamageKind::Cold); ++kind)
		{
			Units::Health applied = -1;
			pass &= ApplyHealthDamage(target, {24, DamageKind(kind)}, source, &applied) && applied == 24;
			Duke3d::fi.ifhitbyweapon(target);
			pass &= Units::FromDukeHealth(target->spr.extra) - target->supersetDamageRemainder == 1600 - 24 * (kind + 1);
			pass &= !target->supersetBurnTime && !target->supersetFreezeTime && target->spr.pal != 1;
		}
		target->Destroy();
	}
	Printf("Superset generic receivers: %s (four native enemy classes outside import registry, eight categories, exact receipts, no implicit status).\n", pass ? "PASS" : "FAIL");
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
