#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif
#include "../duke/src/duke3d.h"
#include "../duke/src/dukeactor.h"
#include "../duke/src/concmd.h"
#include "../blood/src/blood.h"
#include "duke_pickups.h"
#include "duke_weapons.h"
#include "superset.h"
#include "mashup_resources.h"
#include "c_dispatch.h"
#include "gamestate.h"
#include "vm.h"

namespace Superset
{
namespace
{
struct PickupType { const char* name; const char* actor; const char* texture; };
constexpr PickupType pickups[] = {
	{"pistol", "DukeFirstgunSprite", "FIRSTGUNSPRITE"},
	{"shotgun", "DukeShotgunSprite", "SHOTGUNSPRITE"},
	{"chaingun", "DukeChaingunSprite", "CHAINGUNSPRITE"},
	{"bullets", "DukeAmmo", "AMMO"},
	{"bulletbox", "DukeAmmoLots", "AMMOLOTS"},
	{"shells", "DukeShotgunammo", "SHOTGUNAMMO"},
	{"battery", "DukeBatteryAmmo", "BATTERYAMMO"}
};
const PickupType* FindType(PClass* type)
{
	for (auto& entry : pickups) if (type == PClass::FindActor(entry.actor)) return &entry;
	return nullptr;
}
Duke3d::DDukeActor* VMSpawnPickup(DCoreActor* source, PClassActor* type, const DVector3& position)
{
	auto function = PClass::FindFunction("CoreActor", "SpawnSharedDukePickup");
	if (!function) return nullptr;
	void* actor = nullptr;
	VMReturn ret; ret.PointerAt(&actor);
	VMValue params[] = {source, type, position.X, position.Y, position.Z};
	VMCall(function, params, 5, &ret, 1);
	return static_cast<Duke3d::DDukeActor*>(actor);
}

class PickupConServices final : public Duke3d::ConExecutionServices
{
public:
	bool AllowCommand(int opcode, const int* operands) const override
	{
		using namespace Duke3d;
		if ((opcode == concmd_addammo || opcode == concmd_addweapon) && !DukeWeaponSupported(operands[0])) return false;
		switch (opcode)
		{
		// Native control flow, stock pickup predicates, inventory and feedback.
		// Map operations and arbitrary actor commands need their own adapters.
		case concmd_fall: case concmd_ifmove: case concmd_ifp:
		case concmd_nullop: case concmd_ifcount: case concmd_ifpdistl:
		case concmd_ifcanseetarget: case concmd_addammo: case concmd_addweapon:
		case concmd_quote: case concmd_ifspawnedby: case concmd_state:
		case concmd_ends: case concmd_enda: case concmd_leftbrace:
		case concmd_rightbrace: case concmd_else: case concmd_break:
		case concmd_ifactor: case concmd_globalsound: case concmd_palfrom:
		case concmd_ifrespawn: case concmd_killit: case concmd_ifgotweaponce:
		case concmd_ifrnd:
			return true;
		default: return false;
		}
	}
	void UnsupportedCommand(Duke3d::DDukeActor* actor, int opcode) override
	{
		actor->supersetConBlocked = true;
		Printf("Superset: %s needs an adapter for CON command %d in this map.\n", actor->GetClass()->TypeName.GetChars(), opcode);
	}
} conServices;
}

void InitBloodMapDukePickups()
{
	if (!IsBloodMap()) return;
	for (auto& entry : pickups)
		Duke3d::BindSecondaryDukeActor(PClass::FindActor(entry.actor), Mashup::DukeTileNumber(Mashup::DukeTexture(entry.texture)));
	// These classes are referenced by stock pickup predicates, not ticked here.
	Duke3d::BindSecondaryDukeActor(PClass::FindActor("DukePigcop"), Mashup::DukeTileNumber(Mashup::DukeTexture("PIGCOP")));
	Duke3d::BindSecondaryDukeActor(PClass::FindActor("DukeAtomicHealth"), Mashup::DukeTileNumber(Mashup::DukeTexture("ATOMICHEALTH")));
}

bool IsBloodDukePickup(Duke3d::DDukeActor* actor)
{
	return IsBloodMap() && actor && actor->supersetForeignFx && FindType(actor->GetClass());
}

bool BloodDukePickupEnvironment(Duke3d::DDukeActor* actor, int& medium)
{
	if (!IsBloodDukePickup(actor)) return false;
	// Native makeitfall/fall retain their algorithms. Supply generic collision
	// results and Blood medium instead of Duke's typed actors and sector tags.
	// Refresh every step so Blood's moving surfaces remain authoritative.
	CollisionBase ceiling, floor;
	getzrange(actor->spr.pos.plusZ(-1), actor->sector(), &actor->ceilingz, ceiling, &actor->floorz, floor, 7.9375, CLIPMASK0);
	auto upper = dyn_cast<Blood::DBloodActor>(actor->sector()->upperLink.Get());
	medium = actor->sector()->hasX() && actor->sector()->xs().Underwater ? 2 :
		upper && upper->spr.type == Blood::kMarkerUpWater ? 1 : 0;
	return true;
}

Duke3d::DDukeActor* SpawnBloodMapDukePickup(DCoreActor* source, PClassActor* type, const DVector3& position)
{
	if (!IsBloodMap() || numplayers != 1 || !source || !FindType(type) || !Duke3d::SecondaryDukeActorInfo(type)) return nullptr;
	auto sector = source->sector();
	updatesector(position, &sector);
	if (!sector) return nullptr;
	auto actor = Duke3d::CreateActor(sector, position, type, 0, DVector2(0, 0), nullAngle, 0, 0, nullptr, STAT_MISC);
	if (!actor) return nullptr;
	actor->supersetForeignFx = true;
	actor->attackertype = type; // Native map-placed pickup, including CON ifspawnedby.
	Duke3d::CallInitialize(actor, nullptr);
	ChangeActorStat(actor, MAXSTATUS - 3);
	actor->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	actor->backuploc();
	return actor;
}

void TickBloodMapDukePickups()
{
	if (!IsBloodMap() || numplayers != 1) return;
	Duke3d::DDukePlayer* player = nullptr;
	Duke3d::DukeSpriteIterator it;
	while (auto item = it.Next())
	{
		if (!IsBloodDukePickup(item) || item->supersetConBlocked) continue;
		auto blood = Blood::getPlayer(myconnectindex);
		if (!player) player = PrepareBloodMapDukePlayer(blood);
		int beforeAmmo = 0, beforeWeapons = 0;
		for (int n = 0; n <= Duke3d::CHAINGUN_WEAPON; ++n) { beforeAmmo += player->ammo_amount[n]; beforeWeapons += player->gotweapon[n]; }
		double distance;
		Duke3d::findplayer(item, &distance);
		int kill = 0;
		Duke3d::execute(item, player, distance, &kill, &conServices);
		int afterAmmo = 0, afterWeapons = 0;
		for (int n = 0; n <= Duke3d::CHAINGUN_WEAPON; ++n) { afterAmmo += player->ammo_amount[n]; afterWeapons += player->gotweapon[n]; }
		if (afterAmmo != beforeAmmo || afterWeapons != beforeWeapons) blood->pickupEffect = 30;
		if (kill == 1) item->Destroy();
		else ++item->counter; // Native move() advances this even when ammo is full.
	}
}

DEFINE_ACTION_FUNCTION(DCoreActor, SpawnSharedDukePickup)
{
	PARAM_SELF_PROLOGUE(DCoreActor); PARAM_CLASS(type, Duke3d::DDukeActor);
	PARAM_FLOAT(x); PARAM_FLOAT(y); PARAM_FLOAT(z);
	ACTION_RETURN_POINTER(SpawnBloodMapDukePickup(self, static_cast<PClassActor*>(type), {x, y, z}));
}

CCMD(superset_spawn_duke_pickup)
{
	if (!IsBloodMap() || gamestate != GS_LEVEL) return;
	const PickupType* type = nullptr;
	if (argv.argc() == 2) for (auto& entry : pickups) if (!stricmp(argv[1], entry.name)) type = &entry;
	if (!type)
	{
		Printf("Duke pickups:"); for (auto& entry : pickups) Printf(" %s", entry.name); Printf("\n");
		return;
	}
	auto source = Blood::getPlayer(myconnectindex)->GetActor();
	auto pos = source->spr.pos + source->spr.Angles.Yaw.ToVector() * 72;
	auto sector = source->sector(); updatesector(pos, &sector);
	if (!sector) return;
	pos.Z = getflorzofslopeptr(sector, pos);
	Printf("Spawned Duke %s: %s.\n", type->name, VMSpawnPickup(source, PClass::FindActor(type->actor), pos) ? "PASS" : "FAIL");
}

CCMD(superset_duke_pickup_check)
{
	if (!IsBloodMap() || gamestate != GS_LEVEL || argv.argc() != 2) return;
	auto player = PrepareBloodMapDukePlayer(Blood::getPlayer(myconnectindex));
	if (!stricmp(argv[1], "environment"))
	{
		auto source = Blood::getPlayer(myconnectindex)->GetActor();
		auto pos = source->spr.pos + source->spr.Angles.Yaw.ToVector() * 72;
		pos.Z = getflorzofslopeptr(source->sector(), pos) - 64;
		auto item = VMSpawnPickup(source, PClass::FindActor("DukeAmmo"), pos);
		bool pass = item != nullptr;
		if (item)
		{
			auto sector = item->sector();
			auto previousXS = sector->_xs;
			auto previousUpper = sector->upperLink.Get();
			int previousTag = sector->lotag;
			Blood::XSECTOR fixture {};
			sector->_xs = &fixture; sector->upperLink = nullptr; sector->lotag = 2;
			item->spr.pos = pos; item->vel.Zero();
			Duke3d::fall(item, myconnectindex);
			pass &= fabs(item->vel.Z - Duke3d::gs.gravity) < 1e-6 && fabs(item->spr.pos.Z - pos.Z - Duke3d::gs.gravity) < 1e-6;
			fixture.Underwater = true;
			item->spr.pos = pos; item->vel.Zero();
			Duke3d::fall(item, myconnectindex);
			pass &= fabs(item->vel.Z - Duke3d::gs.gravity / 6) < 1e-6 && fabs(item->spr.pos.Z - pos.Z - Duke3d::gs.gravity / 6) < 1e-6;
			sector->_xs = previousXS; sector->upperLink = previousUpper; sector->lotag = previousTag;
			item->Destroy();
		}
		Printf("Superset Duke pickup environment: %s (native fall, Blood dry type 2 and underwater gravity).\n", pass ? "PASS" : "FAIL");
		return;
	}
	if (!stricmp(argv[1], "init"))
	{
		Duke3d::resetweapons(player);
		for (int n = 1; n <= Duke3d::CHAINGUN_WEAPON; ++n) { player->ammo_amount[n] = 0; player->gotweapon[n] = false; }
		player->curr_weapon = Duke3d::KNEE_WEAPON;
		player->kickback_pic = player->okickback_pic = 0;
	}
	if (!stricmp(argv[1], "full") || !stricmp(argv[1], "empty"))
		for (int n = 1; n <= Duke3d::CHAINGUN_WEAPON; ++n)
			player->ammo_amount[n] = !stricmp(argv[1], "full") ? Duke3d::gs.max_ammo_amount[n] : 0;
	if (!stricmp(argv[1], "frail"))
	{
		Blood::getPlayer(myconnectindex)->GetActor()->xspr.health = 1;
		PrepareBloodMapDukePlayer(Blood::getPlayer(myconnectindex));
	}
	Printf("Superset Duke pickup state: ammo=%d/%d/%d weapons=%d/%d/%d mode=%d.\n",
		player->ammo_amount[1], player->ammo_amount[2], player->ammo_amount[3],
		player->gotweapon[1], player->gotweapon[2], player->gotweapon[3], player->supersetWeaponMode);
}
}
