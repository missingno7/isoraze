#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif
#include "../duke/src/duke3d.h"
#include "../duke/src/dukeactor.h"
#include "../blood/src/blood.h"
#include "superset.h"
#include "projectiles.h"
#include "level.h"
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

class CommonProjectileWorld : public Blood::ProjectileWorldServices
{
public:
	DCoreActor* CollisionBody(Blood::DBloodActor* source) override
	{
		if (auto body = Mashup::WorldSoundActor(source)) return body;
		return ResolveGameplayActor(source);
	}
	DCoreActor* CollisionOwner(Blood::DBloodActor* missile) override
	{
		return CollisionBody(missile->GetOwner());
	}
};
class BloodProjectileWorld final : public CommonProjectileWorld
{
public:
	void VerticalRange(const DVector3& pos, sectortype* sector, double* ceiling, CollisionBase& ceilingHit,
		double* floor, CollisionBase& floorHit, double radius) override
	{
		Blood::GetZRangeAtXYZ(pos, sector, ceiling, &ceilingHit, floor, &floorHit, radius, CLIPMASK0);
	}
	void CheckRoomLink(Blood::DBloodActor* actor) override { Blood::CheckLink(actor); }
	bool WallImpact(Blood::DBloodActor* actor, walltype* wall) override { return Blood::actMissileWallImpact(actor, wall); }
	void ExplosionWallImpact(Blood::DBloodActor* actor, walltype* wall) override { Blood::trTriggerWall(wall, Blood::kCmdWallImpact, actor->GetOwner()); }
};
class DukeProjectileWorld final : public CommonProjectileWorld
{
public:
	void VerticalRange(const DVector3& pos, sectortype* sector, double* ceiling, CollisionBase& ceilingHit,
		double* floor, CollisionBase& floorHit, double radius) override
	{
		getzrange(pos, sector, ceiling, ceilingHit, floor, floorHit, radius, CLIPMASK0);
	}
	void CheckRoomLink(Blood::DBloodActor*) override
	{
		// Duke transporter algorithms still need a common-object environment
		// service. Never interpret their sectors as Blood room links.
	}
	bool WallImpact(Blood::DBloodActor*, walltype*) override { return false; }
	void ExplosionWallImpact(Blood::DBloodActor*, walltype*) override {}
	bool NonBlockingMissiles() const override { return true; }
};
BloodProjectileWorld bloodEnvironment;
DukeProjectileWorld dukeEnvironment;
Blood::ProjectileWorldServices* projectileEnvironment = nullptr;
}

void BindProjectileEnvironment(bool bloodSource)
{
	// Source format is decoded once by the frontend; object updates use only
	// the bound environment contract, with no per-map missile implementation.
	projectileEnvironment = bloodSource ? static_cast<Blood::ProjectileWorldServices*>(&bloodEnvironment) : &dukeEnvironment;
}
Blood::ProjectileWorldServices* ProjectileEnvironment()
{
	return IsActive() ? projectileEnvironment : nullptr;
}
void ObserveProjectileSpawn(Blood::DBloodActor* actor)
{
	if (probeActive && actor->spr.type == Blood::kMissileFlameSpray) ++probeShots;
}
void ObserveProjectileImpact(Blood::DBloodActor* actor, DCoreActor* target)
{
	if (probeActive && actor->spr.type == Blood::kMissileFlameSpray && target == ProbeTarget()) ++probeHits;
}
void TickProjectileObjects()
{
	auto world = ProjectileEnvironment();
	Blood::BloodStatIterator it(Blood::kStatProjectile);
	while (auto actor = it.Next()) Blood::actProcessMissile(actor, world);
}
void TickProjectileExplosions()
{
	Blood::actProcessExplosions(ProjectileEnvironment());
}
void TickProjectileAttachments()
{
	Blood::actCheckFlares();
	// Commit detach/removal before the next save or collector pass.
	Blood::actPostProcess();
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

// Integration fixtures use real runtime actors and native weapon callbacks.
// They deliberately avoid holding targets in a status that suppresses damage.
CCMD(superset_projectile_check)
{
	if (!IsActive() || gamestate != GS_LEVEL || numplayers != 1 || argv.argc() < 2) return;
	constexpr int targetTag = 31041, flightTag = 31042;
	auto source = BloodState(PlayerArray[myconnectindex]->GetActor());
	if (!source) return;
	if (!stricmp(argv[1], "equip"))
	{
		auto pos = source->spr.pos;
		pos.Z = getflorzofslopeptr(source->sector(), pos);
		Blood::actSpawnPickup(source->sector(), pos, Blood::kItemWeaponFlarePistol);
		Blood::actSpawnPickup(source->sector(), pos, Blood::kItemAmmoFlares);
		return;
	}
	if (!stricmp(argv[1], "isolate"))
	{
		Blood::BloodStatIterator blood(Blood::kStatDude);
		while (auto actor = blood.Next()) if (!actor->IsPlayerActor())
		{
			Blood::seqKill(actor); Blood::evKillActor(actor);
			actor->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
			Blood::actPostSprite(actor, Blood::kStatFree);
		}
		Duke3d::DukeSpriteIterator duke;
		while (auto actor = duke.Next()) if (Duke3d::badguy(actor)) actor->Destroy();
		return;
	}
	if (!stricmp(argv[1], "target") && argv.argc() == 3)
	{
		auto type = !stricmp(argv[2], "blood") ? Archetype::BloodAxeZombie : Archetype::DukePigcop;
		auto pos = source->spr.pos + source->spr.Angles.Yaw.ToVector() * 96;
		auto sector = source->sector(); updatesector(pos, &sector);
		DCoreActor* target = nullptr;
		if (sector)
		{
			pos.Z = getflorzofslopeptr(sector, pos);
			int monsters = Duke3d::ud.monsters_off; Duke3d::ud.monsters_off = 0;
			target = SpawnRuntimeEntity(type, sector, pos, source->spr.Angles.Yaw + DAngle180);
			Duke3d::ud.monsters_off = monsters;
		}
		if (target)
		{
			target->spr.intowner = targetTag;
			if (auto blood = dyn_cast<Blood::DBloodActor>(target)) blood->xspr.health = 16000;
			if (auto duke = dyn_cast<Duke3d::DDukeActor>(target)) duke->spr.extra = 1000;
		}
		Printf("Superset shared projectile target: %s (%s).\n", target ? "PASS" : "FAIL", argv[2]);
		return;
	}
	if (!stricmp(argv[1], "flight"))
	{
		auto player = Blood::getPlayer(myconnectindex);
		auto heading = source->spr.Angles.Yaw.ToVector();
		auto missile = Blood::actSpawnMissile(source, source->sector(),
			DVector3(source->spr.pos.XY() + heading * 16, player->zWeapon), DVector3(heading, 0), Blood::kMissileFlareRegular);
		if (missile) { missile->spr.intowner = flightTag; missile->vel = DVector3(heading * .25, 0); }
		Printf("Superset shared projectile flight: %s.\n", missile ? "PASS" : "FAIL");
		return;
	}
	if (!stricmp(argv[1], "kill") || !stricmp(argv[1], "destroy"))
	{
		ForEachGameplayTarget([&](const TargetInfo& target)
		{
			if (target.actor->spr.intowner != targetTag) return;
			if (!stricmp(argv[1], "kill")) ApplyHealthDamage(target.actor, {100000, DamageKind::Physical}, source);
			else
			{
				if (auto blood = dyn_cast<Blood::DBloodActor>(target.actor)) { Blood::seqKill(blood); Blood::evKillActor(blood); }
				target.actor->Destroy();
			}
		});
		return;
	}
	if (stricmp(argv[1], "clear")) return;
	Blood::BloodSpriteIterator it;
	while (auto actor = it.Next()) if (actor->GetOwner() == source &&
		(actor->spr.statnum == Blood::kStatProjectile || actor->spr.statnum == Blood::kStatFlare || actor->spr.statnum == Blood::kStatExplosion))
	{
		Blood::seqKill(actor); Blood::evKillActor(actor);
		actor->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
		Blood::actPostSprite(actor, Blood::kStatFree);
	}
}

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
