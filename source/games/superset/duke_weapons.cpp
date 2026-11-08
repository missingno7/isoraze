#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif
#include "../duke/src/duke3d.h"
#include "../duke/src/dukeactor.h"
#include "../blood/src/blood.h"
#include "duke_weapons.h"
#include "superset.h"
#include "damage.h"
#include "mashup_resources.h"
#include "vm.h"
#include "c_dispatch.h"
#include "gamestate.h"

namespace Superset
{
using Duke3d::DDukeActor;
namespace
{
Blood::DBloodActor* WeaponSource(Duke3d::DDukeActor* actor)
{
	return IsDukeWeaponActor(actor) ? static_cast<Blood::DBloodActor*>(actor->supersetAttackSource.Get()) : nullptr;
}
void PublishWeaponBody(Blood::DBloodPlayer* blood, Duke3d::DDukePlayer* duke)
{
	auto source = blood->GetActor(); auto actor = duke->GetActor();
	double top, bottom; Blood::GetActorExtents(source, &top, &bottom);
	if (actor->sector() != source->sector()) ChangeActorSect(actor, source->sector());
	actor->backuploc();
	actor->spr.pos = {source->spr.pos.XY(), bottom};
	actor->spr.Angles = source->spr.Angles;
	actor->viewzoffset = blood->zView - bottom;
	actor->oviewzoffset = actor->viewzoffset;
	// This integer facade supplies native alive/dead predicates, not health
	// authority. A positive Q4 fraction must still be an alive native player.
	actor->spr.extra = max(0, (int(source->xspr.health) + 15) >> 4);
	actor->spr.shade = source->spr.shade;
	actor->spr.cstat = CSTAT_SPRITE_INVISIBLE;
	actor->supersetAttackSource = source; GC::WriteBarrier(actor, source);
	duke->PrevViewAngles = blood->PrevViewAngles; duke->ViewAngles = blood->ViewAngles;
	duke->PrevLerpAngles = blood->PrevLerpAngles; duke->CameraAngles = blood->CameraAngles;
	duke->cmd = blood->cmd; duke->lastcmd = blood->lastcmd;
	duke->cursector = source->sector(); duke->psectlotag = blood->isUnderwater ? ST_2_UNDERWATER : 0;
	duke->on_ground = source->xspr.height == 0;
	duke->pyoff = duke->opyoff = 0;
}
class BloodMapDukeWeapons final : public Blood::PlayerWeaponServices
{
public:
	bool UseBloodWeapons() const override { return false; }
	void ProcessWeapons(Blood::DBloodPlayer* blood) override
	{
		auto duke = Duke3d::SecondaryDukePlayer(blood->pnum);
		PublishWeaponBody(blood, duke);
		if (blood->GetActor()->xspr.health <= 0) return;
		// CON can assign a weapon directly, bypassing the ordinary selector.
		// Unsupported attacks must not enter Duke's typed map-contact path.
		if (!DukeWeaponSupported(duke->curr_weapon))
		{
			duke->curr_weapon = Duke3d::KNEE_WEAPON;
			duke->kickback_pic = duke->okickback_pic = 0;
			duke->last_weapon = duke->wantweaponfire = -1;
		}
		if (BloodWeaponsRequested())
		{
			duke->cmd.ucmd.actions &= ~(SB_FIRE | SB_ALTFIRE | SB_HOLSTER | SB_QUICK_KICK);
			duke->cmd.ucmd.setNewWeapon(0);
		}
		auto source = blood->GetActor(); auto flags = source->spr.cstat;
		source->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
		Duke3d::hud_weapon_input(duke);
		Duke3d::tickweapons_d(duke, false);
		source->spr.cstat = flags;
		duke->GetActor()->spr.cstat = CSTAT_SPRITE_INVISIBLE;
	}
} weaponServices;

void CaptureStartingWeaponFamily(Duke3d::DDukePlayer* duke)
{
	if (duke->supersetWeaponMode >= 0) return;
	duke->supersetWeaponMode = 1;
	duke->supersetForeignStartWithWeapons = !BloodWeaponsRequested();
}

void ClearDukeWeaponLoadout(Duke3d::DDukePlayer* duke)
{
	for (int weapon = 0; weapon < Duke3d::MAX_WEAPONS; ++weapon)
	{
		duke->ammo_amount[weapon] = 0;
		duke->gotweapon[weapon] = weapon == Duke3d::KNEE_WEAPON;
	}
	duke->curr_weapon = Duke3d::KNEE_WEAPON;
	duke->kickback_pic = duke->okickback_pic = 0;
}
}

void ApplyDukeMapStartingLoadout(Duke3d::DDukePlayer* duke)
{
	if (IsDukeMap() && BloodWeaponsRequested()) ClearDukeWeaponLoadout(duke);
}

bool IsDukeWeaponActor(Duke3d::DDukeActor* actor)
{
	return IsBloodMap() && actor && actor->isPlayer() && unsigned(actor->PlayerIndex()) < MAXPLAYERS &&
		Duke3d::SecondaryDukePlayer(actor->PlayerIndex())->GetActor() == actor &&
		dyn_cast<Blood::DBloodActor>(actor->supersetAttackSource.Get());
}
bool DukeWeaponSupported(int weapon) { return unsigned(weapon) <= Duke3d::CHAINGUN_WEAPON; }
bool IsBloodDirectAimTarget(DCoreActor* actor)
{
	auto blood = dyn_cast<Blood::DBloodActor>(actor);
	return blood && (blood->spr.statnum == Blood::kStatDude || blood->spr.statnum == Blood::kStatThing);
}
void ForEachBloodAimTarget(int group, const std::function<void(const DukeAimCandidate&)>& visitor)
{
	if (group != 0 && group != 2) return;
	auto source = Blood::getPlayer(myconnectindex);
	Blood::BloodStatIterator it(Blood::kStatDude);
	while (auto actor = it.Next())
	{
		if (actor == source->GetActor() || !actor->IsDudeActor() || !actor->hasX() ||
			(actor->spr.flags & 32) || !(actor->spr.flags & 8) ||
			(!Blood::gGameOptions.bFriendlyFire && Blood::IsTargetTeammate(source, actor))) continue;
		bool player = actor->IsPlayerActor();
		if ((group == 0) != player) continue;
		visitor({actor, !player, player, actor->xspr.health > 0, false, false,
			-actor->spr.scale.Y * Blood::getDudeInfo(actor->spr.type)->aimHeight});
	}
}

Blood::PlayerWeaponServices* BloodMapWeaponServices(Blood::DBloodPlayer* blood)
{
	if (!IsBloodMap() || numplayers != 1) return nullptr;
	auto duke = Duke3d::SecondaryDukePlayer(blood->pnum);
	bool wanted = !BloodWeaponsRequested();
	CaptureStartingWeaponFamily(duke);
	bool current = duke->supersetWeaponMode == 0;
	if (wanted != current && blood->GetActor()->xspr.health > 0)
	{
		bool ready = current ? !duke->kickback_pic && !duke->rapid_fire_hold && !duke->quick_kick && !duke->knee_incs : Blood::WeaponCanStow(blood);
		if (ready)
		{
			if (current)
			{
				blood->weaponQav = Blood::kQAVNone; blood->weaponTimer = blood->qavTimer = 0; blood->qavLoop = false;
				if (!blood->newWeapon) blood->newWeapon = duke->supersetStowedBloodWeapon;
			}
			else
			{
				duke->supersetStowedBloodWeapon = blood->curWeapon;
				blood->newWeapon = blood->nextWeapon = Blood::kWeapNone;
				if (blood->curWeapon) Blood::WeaponLower(blood);
				duke->weapon_pos = duke->oweapon_pos = 10; duke->last_weapon = -1; duke->holster_weapon = 0;
			}
			duke->supersetWeaponMode = wanted ? 0 : 1; current = wanted;
			duke->interface_toggle_flag = 0;
		}
		else if (!current)
		{
			blood->cmd.ucmd.actions &= ~(SB_FIRE | SB_ALTFIRE | SB_HOLSTER);
			blood->cmd.ucmd.setNewWeapon(0);
		}
	}
	if (!current) return nullptr;
	PrepareBloodMapDukePlayer(blood);
	return &weaponServices;
}

Duke3d::DDukePlayer* PrepareBloodMapDukePlayer(Blood::DBloodPlayer* blood)
{
	if (!IsBloodMap() || numplayers != 1) return nullptr;
	auto duke = Duke3d::SecondaryDukePlayer(blood->pnum);
	CaptureStartingWeaponFamily(duke);
	if (!duke->GetActor())
	{
		auto source = blood->GetActor();
		auto actor = Duke3d::CreateActor(source->sector(), source->spr.pos, Duke3d::DukePlayerBaseClass,
			0, DVector2(1, 1), source->spr.Angles.Yaw, 0, 0, nullptr, STAT_MISC);
		ChangeActorStat(actor, MAXSTATUS - 2);
		duke->actor = actor; GC::WriteBarrier(duke, actor);
		actor->spr.yint = blood->pnum;
		if (!duke->supersetForeignWeaponsInitialized)
		{
			Duke3d::resetweapons(duke); duke->wantweaponfire = -1;
			if (!duke->supersetForeignStartWithWeapons)
				ClearDukeWeaponLoadout(duke);
			duke->supersetForeignWeaponsInitialized = true;
		}
	}
	PublishWeaponBody(blood, duke);
	return duke;
}

bool DrawBloodMapDukeWeapons(Blood::DBloodPlayer* blood, double fraction)
{
	if (!IsBloodMap()) return false;
	auto duke = Duke3d::SecondaryDukePlayer(blood->pnum);
	if (duke->supersetWeaponMode != 0 || !duke->GetActor()) return false;
	Duke3d::fi.displayweapon(duke, fraction);
	return true;
}
int BloodMapForeignWeaponAmmo(Blood::DBloodPlayer* blood)
{
	if (!IsBloodMap()) return -2;
	auto duke = Duke3d::SecondaryDukePlayer(blood->pnum);
	if (duke->supersetWeaponMode != 0 || !duke->GetActor()) return -2;
	return duke->curr_weapon == Duke3d::KNEE_WEAPON ? -1 : duke->ammo_amount[duke->curr_weapon];
}
void ClearBloodMapDukeWeapons()
{
	for (int i = 0; i < MAXPLAYERS; ++i)
	{
		auto player = Duke3d::SecondaryDukePlayer(i);
		if (!player) continue;
		if (auto actor = player->GetActor()) actor->Destroy();
		player->actor = nullptr;
		player->kickback_pic = player->okickback_pic = 0;
	}
}

void ResetBloodMapDukeWeapons()
{
	ClearBloodMapDukeWeapons();
	for (int i = 0; i < MAXPLAYERS; ++i)
	{
		auto player = Duke3d::SecondaryDukePlayer(i);
		if (!player) continue;
		player->supersetWeaponMode = -1;
		player->supersetForeignWeaponsInitialized = false;
		player->supersetForeignStartWithWeapons = false;
	}
}
bool IsDukeWeaponEffect(DDukeActor* actor)
{
	return IsBloodMap() && actor && actor->supersetForeignFx && actor->IsKindOf(Duke3d::DukeShellClass);
}
bool SpawnDukeWeaponEffect(DDukeActor* source, PClassActor* type, DDukeActor*& result)
{
	// Melee attacks create a transient hit actor before requesting their smoke.
	// Handle that actor's spawns here too, so unsupported FX cannot leak into
	// Blood's native actor lists. Only shells have a safe foreign ticker yet.
	auto player = IsDukeWeaponActor(source) ? source : source->GetOwner();
	if (!IsDukeWeaponActor(player)) return false;
	result = nullptr;
	if (source != player || !type->IsDescendantOf(Duke3d::DukeShellClass)) return true;
	result = Duke3d::CreateActor(source->sector(), source->spr.pos, type, 0, DVector2(0, 0), nullAngle, 0, 0, source, STAT_MISC);
	if (result)
	{
		result->supersetForeignFx = true;
		result->supersetAttackSource = source->supersetAttackSource;
		GC::WriteBarrier(result, result->supersetAttackSource.Get());
		Duke3d::CallInitialize(result, source);
		result->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	}
	return true;
}

DEFINE_ACTION_FUNCTION(DDukeActor, AimHitscan)
{
	PARAM_SELF_PROLOGUE(Duke3d::DDukeActor);
	PARAM_OBJECT(weapon, Duke3d::DDukeActor); PARAM_FLOAT(angle);
	bool direct = false;
	auto target = Duke3d::AimHitscanTarget(self, weapon, angle, &direct);
	if (numret > 0) ret[0].SetPointer(target);
	if (numret > 1) ret[1].SetInt(direct);
	return min(numret, 2);
}
DEFINE_ACTION_FUNCTION(DDukeActor, HitscanAimOffset)
{
	PARAM_SELF_PROLOGUE(Duke3d::DDukeActor); PARAM_OBJECT(target, DCoreActor);
	if (auto blood = dyn_cast<Blood::DBloodActor>(target); IsDukeWeaponActor(self) && blood && blood->IsDudeActor())
		ACTION_RETURN_FLOAT(blood->spr.scale.Y * Blood::getDudeInfo(blood->spr.type)->aimHeight);
	auto duke = dyn_cast<Duke3d::DDukeActor>(target);
	ACTION_RETURN_FLOAT(duke ? duke->spr.scale.X * int(TexMan.GetGameTexture(duke->spr.spritetexture())->GetDisplayHeight()) * .5 + duke->FloatVar(FName("sparkoffset")) : 0);
}
DEFINE_ACTION_FUNCTION(DDukeActor, TraceSharedHitscan)
{
	PARAM_SELF_PROLOGUE(Duke3d::DDukeActor);
	PARAM_FLOAT(x); PARAM_FLOAT(y); PARAM_FLOAT(z);
	PARAM_FLOAT(dx); PARAM_FLOAT(dy); PARAM_FLOAT(dz); PARAM_POINTER(hit, HitInfoBase);
	auto source = WeaponSource(self); if (!source) ACTION_RETURN_BOOL(false);
	Blood::VectorScan(source, 0, z - source->spr.pos.Z, {dx, dy, dz}, 0, 1);
	*hit = Blood::gHitInfo;
	ACTION_RETURN_BOOL(true);
}
DEFINE_ACTION_FUNCTION(DDukeActor, SharedHitscanContact)
{
	PARAM_SELF_PROLOGUE(Duke3d::DDukeActor); PARAM_POINTER(hit, HitInfoBase); PARAM_OBJECT(spark, Duke3d::DDukeActor);
	auto source = WeaponSource(self); if (!source) ACTION_RETURN_BOOL(false);
	if (hit->hitWall && hit->hitWall->hasX() && hit->hitWall->xw().triggerVector)
		Blood::trTriggerWall(hit->hitWall, Blood::kCmdWallImpact, source);
	if (auto target = dyn_cast<Blood::DBloodActor>(hit->hitActor); target && target->hasX() && target->xspr.Vector)
		Blood::trTriggerSprite(target, Blood::kCmdSpriteImpact, source);
	if (hit->hitActor) ApplyDukeHealthHit(hit->hitActor, spark, source);
	spark->Destroy();
	ACTION_RETURN_BOOL(true);
}

CCMD(superset_duke_weapon_check)
{
	if (!IsBloodMap() || gamestate != GS_LEVEL) return;
	auto blood = Blood::getPlayer(myconnectindex);
	auto duke = Duke3d::SecondaryDukePlayer(myconnectindex);
	auto actor = duke->GetActor();
	if (argv.argc() > 1 && !stricmp(argv[1], "give"))
	{
		for (int weapon : {Duke3d::PISTOL_WEAPON, Duke3d::SHOTGUN_WEAPON, Duke3d::CHAINGUN_WEAPON})
		{
			duke->gotweapon[weapon] = true; duke->ammo_amount[weapon] = Duke3d::gs.max_ammo_amount[weapon];
		}
	}
	if (argv.argc() > 1 && !stricmp(argv[1], "target"))
	{
		Blood::BloodSpriteIterator it;
		while (auto old = it.Next()) if (old->spr.intowner == 31036) { Blood::evKillActor(old); Blood::seqKill(old); old->Destroy(); }
		auto target = Blood::actSpawnDude(blood->GetActor(), Blood::kDudeZombieAxeNormal, 64);
		if (target)
		{
			double top, bottom; Blood::GetActorExtents(target, &top, &bottom);
			target->spr.pos.Z += getflorzofslopeptr(target->sector(), target->spr.pos) - bottom;
			target->spr.intowner = 31036; target->xspr.health = 16000;
			target->xspr.Vector = true; target->xspr.triggerOnce = true;
			Blood::aiNewState(target, &Blood::genIdle);
			target->backuploc();
		}
		Printf("Superset Duke weapon target: %s.\n", target ? "PASS" : "FAIL");
	}
	if (argv.argc() > 1 && !stricmp(argv[1], "clear"))
	{
		Blood::BloodSpriteIterator it;
		while (auto target = it.Next()) if (target->spr.intowner == 31036) { Blood::evKillActor(target); Blood::seqKill(target); target->Destroy(); }
	}
	Printf("Superset Duke weapon state: mode=%d weapon=%d ammo=%d frame=%d body=%d source=%d primary=%s.\n",
		duke->supersetWeaponMode, duke->curr_weapon, duke->ammo_amount[duke->curr_weapon], duke->kickback_pic,
		actor != nullptr, actor && WeaponSource(actor) == blood->GetActor(), PlayerArray[myconnectindex]->GetClass()->TypeName.GetChars());
}
}
