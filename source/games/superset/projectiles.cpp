#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif
#include "../duke/src/duke3d.h"
#include "../duke/src/dukeactor.h"
#include "../blood/src/blood.h"
#include "superset.h"
#include "projectiles.h"
#include "damage.h"
#include "enemies.h"
#include "status.h"
#include "mashup_player.h"
#include "c_cvars.h"
#include "serializer.h"
#include "c_dispatch.h"

namespace Duke3d { void TickActor(DDukeActor*); }

// Independent native source profile until a common difficulty policy is chosen.
CVAR(Int, superset_blood_difficulty, 2, CVAR_ARCHIVE)

namespace Superset
{
namespace
{
int probeShots = 0, probeHits = 0, probeAmmo = 0, probeBurnTime = 0, probeHealth = 0;
bool probeActive = false;
constexpr int ProbeTag = 31023;
Duke3d::DDukeActor* ProbeTarget()
{
	Duke3d::DukeStatIterator it(MAXSTATUS - 2);
	while (auto actor = it.Next()) if (actor->spr.hitag == ProbeTag) return actor;
	return nullptr;
}

class DukeProjectileWorld final : public Blood::ProjectileWorldServices
{
public:
	DCoreActor* CollisionOwner(Blood::DBloodActor* missile) override
	{
		auto owner = missile->GetOwner();
		if (auto body = Mashup::WorldSoundActor(owner)) return body;
		return owner;
	}
	void CheckRoomLink(Blood::DBloodActor*) override
	{
		// Blood room links do not describe Duke map transports. Projectile
		// transport variants need their own environment implementation.
	}
};

void FlameImpact(Blood::DBloodActor* flame, DCoreActor* target)
{
	if (!target) return;
	if (probeActive && target == ProbeTarget()) ++probeHits;
	auto source = flame->GetOwner();
	int clocks = (4 + clamp(int(superset_blood_difficulty), 0, 4)) * Units::ClocksPerStep;
	Ignite(target, source, clocks);
	if (auto duke = dyn_cast<Duke3d::DDukeActor>(target); duke && Duke3d::badguy(duke))
		ApplyHealthDamage(duke, {8, DamageKind::Heat}, source);
}
}

bool FireBloodMissile(Blood::DBloodActor* source, double xyoff, double zoff, const DVector3& direction, int type, Blood::DBloodActor*& result)
{
	if (!IsDukeMap()) return false;
	// Do not fall through into Blood's typed collision path in a mixed world.
	result = nullptr;
	if (type != Blood::kMissileFlameSpray || direction.isZero()) return true;
	auto body = Mashup::WorldSoundActor(source);
	if (!body) return true;
	auto& info = Blood::missileInfo[type - Blood::kMissileBase];
	double distance = info.fClipDist() + source->clipdist;
	auto forward = source->spr.Angles.Yaw.ToVector();
	auto origin = source->spr.pos.plusZ(zoff) + (source->spr.Angles.Yaw + DAngle90).ToVector() * xyoff;
	THitInfo<DCoreActor> hit {};
	auto flags = body->spr.cstat;
	body->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	hitscan(origin, body->sector(), DVector3(forward * distance, 0), hit, CLIPMASK0, distance * 4, true);
	body->spr.cstat = flags;
	int hitCode = -1;
	if (hit.hitActor) hitCode = 3;
	else if (hit.hitWall)
	{
		hitCode = 0;
		if (hit.hitWall->twoSided())
		{
			double ceil, floor;
			calcSlope(hit.hitWall->nextSector(), hit.hitpos, &ceil, &floor);
			if (hit.hitpos.Z > ceil && hit.hitpos.Z < floor) hitCode = 4;
		}
	}
	else if (hit.hitSector) hitCode = 1 + (origin.Z < hit.hitpos.Z);
	bool immediate = hitCode == 3 || hitCode == 0;
	auto pos = origin + forward * distance;
	if (hitCode >= 0) pos.SetXY(hit.hitpos.XY() - forward * (immediate ? 1. : info.fClipDist() * 2));
	auto sector = body->sector();
	updatesector(pos, &sector);
	if (!sector) return true;
	result = Blood::actSpawnMissile(source, sector, pos, direction, type);
	if (result) result->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	if (result && probeActive) ++probeShots;
	if (result && immediate) FlameImpact(result, hit.hitActor);
	return true;
}

void TickBloodProjectiles()
{
	if (!IsDukeMap()) return;
	DukeProjectileWorld world;
	Blood::BloodStatIterator it(Blood::kStatProjectile);
	while (auto actor = it.Next())
	{
		if ((actor->spr.flags & 32) || actor->spr.type != Blood::kMissileFlameSpray) continue;
		actor->backuploc();
		THitInfo<DCoreActor> hit {};
		// Retain the native missile mover's cstat during its own query, while
		// keeping foreign projectiles out of Duke's typed actor touch path.
		actor->spr.cstat |= CSTAT_SPRITE_BLOCK;
		int code = Blood::actMoveMissile(actor, hit, &world);
		actor->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
		if (code == 3) FlameImpact(actor, hit.hitActor);
	}
}

bool Ignite(DCoreActor* target, DCoreActor* source, int clocks)
{
	if (!IsActive() || !target || clocks <= 0) return false;
	if (HasBloodEnemy(target))
	{
		auto state = BloodState(target);
		if (!state || !state->IsDudeActor() || !state->xspr.health) return false;
		// Fire contact requests thaw separately from the attack's health damage.
		Thaw(target);
		if (!state->xspr.burnTime) Blood::evPostActor(state, 0, Blood::kCallbackFXFlameLick);
		Blood::actBurnSprite(BloodState(source), state, min(clocks, 2400));
		state->supersetDamageSource = Mashup::WorldSoundActor(source) ? Mashup::WorldSoundActor(source) : source;
		GC::WriteBarrier(state, state->supersetDamageSource.Get());
		return true;
	}
	if (auto duke = dyn_cast<Duke3d::DDukeActor>(target))
	{
		if (auto body = Mashup::WorldSoundActor(source)) source = body;
		if (!Duke3d::badguy(duke) || duke->spr.extra <= 0 || !duke->IntVar(FName("supersetburnable"))) return false;
		if (duke->supersetBurnTime == 0) duke->supersetBurnFxTime = 0;
		duke->supersetBurnTime = min(duke->supersetBurnTime + min(clocks, 2400), 2400);
		duke->supersetBurnSource = source;
		GC::WriteBarrier(duke, source);
		return true;
	}
	// Blood enemies/things require their environment-aware receiver and object
	// ticks first. Do not install native damage/trigger callbacks prematurely.
	return false;
}

void TickStatusEffects()
{
	Duke3d::DukeSpriteIterator it;
	while (auto actor = it.Next())
	{
		if (actor->supersetBurnTime <= 0) continue;
		if (actor->spr.extra <= 0 || !Duke3d::badguy(actor))
		{
			actor->supersetBurnTime = actor->supersetBurnFxTime = 0;
			actor->supersetBurnSource = nullptr;
			continue;
		}
		// Blood's ordinary burning tick: four clocks and eight Q4 health units.
		actor->supersetBurnTime = max(0, actor->supersetBurnTime - int(Units::ClocksPerStep));
		ApplyHealthDamage(actor, {8, DamageKind::Heat}, actor->supersetBurnSource);
		actor->supersetBurnFxTime -= Units::ClocksPerStep;
		if (actor->supersetBurnFxTime <= 0)
		{
			// Duke enemy vel.X is forward speed, unlike Blood's world vector.
			Blood::fxSpawnFlameLick(actor, DVector3(actor->spr.Angles.Yaw.ToVector() * actor->vel.X, actor->vel.Z));
			// Native callbacks are scheduled five clocks apart and execute on
			// four-clock updates, so each callback actually runs eight clocks later.
			actor->supersetBurnFxTime = 8;
		}
		if (!actor->supersetBurnTime) actor->supersetBurnSource = nullptr;
	}
}

int BloodDifficulty() { return clamp(int(superset_blood_difficulty), 0, 4); }

CCMD(superset_blood_combat_check)
{
	if (!IsBloodMap() || gamestate != GS_LEVEL || numplayers != 1 || argv.argc() != 2) return;
	if (!stricmp(argv[1], "blast"))
	{
		TargetInfo selected;
		ForEachGameplayTarget([&](const TargetInfo& target)
		{
			if (target.alive && !dyn_cast<Blood::DBloodActor>(target.actor) && target.identity > selected.identity) selected = target;
		});
		auto bomb = selected.actor ? Blood::actSpawnThing(selected.actor->sector(), selected.aim, Blood::kThingArmedTNTBundle) : nullptr;
		if (bomb)
		{
			bomb->SetOwner(Blood::getPlayer(myconnectindex)->GetActor());
			Blood::actExplodeSprite(bomb);
		}
		Printf("Superset Blood explosion fixture: %s.\n", bomb ? "PASS" : "FAIL");
		return;
	}
	if (!stricmp(argv[1], "isolate"))
	{
		// Stock innocents remain with -nomonsters and can consume Voodoo's
		// native sequential area budget. Isolate test aiming from those actors.
		Blood::BloodStatIterator it(Blood::kStatDude);
		while (auto actor = it.Next()) if (!actor->IsPlayerActor())
		{
			Blood::seqKill(actor); Blood::evKillActor(actor);
			actor->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
			Blood::actPostSprite(actor, Blood::kStatFree);
		}
		return;
	}
	if (stricmp(argv[1], "clear")) return;
	// Regression cleanup isolates ongoing ignition from repeated flame contacts.
	// Use the native post queue: sequence callbacks may already have posted it.
	auto source = Blood::getPlayer(myconnectindex)->GetActor();
	Blood::BloodStatIterator it(Blood::kStatProjectile);
	while (auto actor = it.Next()) if (actor->GetOwner() == source)
	{
		Blood::seqKill(actor); Blood::evKillActor(actor);
		actor->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
		Blood::actPostSprite(actor, Blood::kStatFree);
	}
}

void SerializeProjectileOptions(FSerializer& arc)
{
	int difficulty = clamp(int(superset_blood_difficulty), 0, 4);
	arc("blood_difficulty", difficulty);
	if (arc.isReading()) superset_blood_difficulty = clamp(difficulty, 0, 4);
}

CCMD(superset_flame_check)
{
	if (!IsDukeMap() || gamestate != GS_LEVEL || argv.argc() < 2) return;
	auto duke = Duke3d::getPlayer(myconnectindex);
	auto blood = static_cast<Blood::DBloodPlayer*>(Mashup::GetBloodPlayer(myconnectindex));
	if (!blood) return;
	auto body = duke->GetActor();
	auto target = ProbeTarget();
	if (!stricmp(argv[1], "seed"))
	{
		if (target) target->Destroy();
		auto pos = DVector3(body->spr.pos.XY(), blood->zWeapon) + body->spr.Angles.Yaw.ToVector() * 80;
		target = Duke3d::CreateActor(body->sector(), pos, PClass::FindActor("DukeLizTrooper"),
			0, DVector2(1, 1), nullAngle, 0, 0, body, STAT_MISC);
		if (target)
		{
			ChangeActorStat(target, MAXSTATUS - 2);
			target->spr.cstat = CSTAT_SPRITE_BLOCK_ALL | CSTAT_SPRITE_YCENTER;
			target->spr.hitag = ProbeTag;
			target->flags3 |= SFLAG3_NOHITJIBS;
			target->spr.extra = probeHealth = 200;
		}
		probeShots = probeHits = 0;
		probeAmmo = blood->AmmoAmounts()[6];
		probeActive = true;
		Printf("Superset flame setup: %s (weapon=%d fuel=%d).\n", target && blood->curWeapon == Blood::kWeapSpraycan ? "PASS" : "FAIL", blood->curWeapon, probeAmmo);
		return;
	}
	if (!stricmp(argv[1], "fired"))
	{
		int spent = probeAmmo - blood->AmmoAmounts()[6];
		bool input = probeShots > 0 && spent == probeShots * 4;
		bool hits = target && probeHits > 0 && target->supersetBurnTime > 0 && target->supersetBurnSource == body && target->GetHitOwner() == body;
		bool impulse = target && target->vel.isZero() && target->spr.Angles.Yaw == nullAngle;
		if (target) Duke3d::fi.ifhitbyweapon(target);
		bool health = target && target->spr.extra < probeHealth && target->spr.extra > 0;
		probeBurnTime = target ? target->supersetBurnTime : 0;
		probeHealth = target ? target->spr.extra : 0;
		Printf("Superset flame fired: input=%s, ignition=%s, health=%s, impulse=%s (shots=%d hits=%d spent=%d burn=%d health=%d).\n",
			input ? "PASS" : "FAIL", hits ? "PASS" : "FAIL", health ? "PASS" : "FAIL", impulse ? "PASS" : "FAIL",
			probeShots, probeHits, spent, probeBurnTime, probeHealth);
		probeActive = false;
		return;
	}
	if (!stricmp(argv[1], "clear"))
	{
		if (target) target->Destroy();
		Blood::BloodStatIterator it(Blood::kStatProjectile);
		while (auto actor = it.Next()) actor->Destroy();
		return;
	}
	if (!stricmp(argv[1], "saved"))
	{
		int missiles = 0;
		Blood::BloodStatIterator it(Blood::kStatProjectile);
		while (auto actor = it.Next()) if (actor->spr.type == Blood::kMissileFlameSpray &&
			actor->GetOwner() == blood->GetActor() && !actor->vel.isZero()) ++missiles;
		// Restored live flames may extend the burn again before this check.
		bool pass = target && probeBurnTime > 0 && target->supersetBurnTime > 0 && target->supersetBurnTime <= 2400 &&
			target->supersetBurnSource == body && target->spr.extra == probeHealth && missiles > 0 && superset_blood_difficulty == 2;
		Printf("Superset flame save check: %s. (moving missiles=%d burn=%d)\n", pass ? "PASS" : "FAIL", missiles, target ? target->supersetBurnTime : 0);
		return;
	}
	if (!stricmp(argv[1], "ownership"))
	{
		bool pass = target && target->supersetBurnSource == body && target->GetHitOwner() == body &&
			Mashup::WorldSoundActor(blood->GetActor()) == body;
		Printf("Superset flame ownership check: %s.\n", pass ? "PASS" : "FAIL");
		return;
	}
	if (!stricmp(argv[1], "motion"))
	{
		auto velocity = blood->GetActor()->vel;
		bool pass = velocity.XY().Length() > .01 && (velocity - duke->vel).Length() < 1e-6;
		Printf("Superset flame motion check: %s.\n", pass ? "PASS" : "FAIL");
		return;
	}
	if (!stricmp(argv[1], "expired"))
	{
		int missiles = 0;
		Blood::BloodStatIterator it(Blood::kStatProjectile);
		while (auto actor = it.Next()) if (actor->spr.type == Blood::kMissileFlameSpray) ++missiles;
		bool pass = target && target->supersetBurnTime == 0 && !target->supersetBurnSource && missiles == 0;
		if (target) Duke3d::fi.ifhitbyweapon(target);
		pass &= target && target->spr.extra < probeHealth;
		Printf("Superset flame expiry check: %s.\n", pass ? "PASS" : "FAIL");
		return;
	}
	if (!stricmp(argv[1], "responses") && target)
	{
		target->spr.extra = 200; target->hitextra = -1; target->supersetDamageRemainder = 0;
		auto factor = target->FloatVar(FName("supersetheatfactor"));
		auto burnable = target->IntVar(FName("supersetburnable"));
		target->FloatVar(FName("supersetheatfactor")) = .5;
		ApplyDukeHealthDamage(target, {32, DamageKind::Heat}, body);
		Duke3d::fi.ifhitbyweapon(target);
		bool resistance = target->spr.extra == 199;
		target->FloatVar(FName("supersetheatfactor")) = 0;
		ApplyDukeHealthDamage(target, {160, DamageKind::Heat}, body);
		Duke3d::fi.ifhitbyweapon(target);
		bool immune = target->spr.extra == 199;
		target->IntVar(FName("supersetburnable")) = 0;
		bool refused = !Ignite(target, body, 120) && target->supersetBurnTime == 0;
		target->IntVar(FName("supersetburnable")) = 1;
		bool independent = Ignite(target, body, 120);
		target->FloatVar(FName("supersetheatfactor")) = factor;
		target->IntVar(FName("supersetburnable")) = burnable;
		target->supersetBurnTime = 0; target->supersetBurnSource = nullptr;
		target->spr.extra = 1;
		ApplyDukeHealthDamage(target, {16, DamageKind::Heat}, body);
		ChangeActorStat(target, STAT_ACTOR);
		Duke3d::TickActor(target);
		bool death = !(target->ObjectFlags & OF_EuthanizeMe) && target->spr.extra < 0 &&
			(target->curAction->name == FName("ATROOPDYING") || target->curAction->name == FName("ATROOPSUFFERING"));
		Printf("Superset flame responses: resistance=%s, immunity=%s, ignition gate=%s, separate effect=%s, CON death=%s.\n",
			resistance ? "PASS" : "FAIL", immune ? "PASS" : "FAIL", refused ? "PASS" : "FAIL", independent ? "PASS" : "FAIL", death ? "PASS" : "FAIL");
		target->Destroy();
	}
}
}
