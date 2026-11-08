#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif
#include "../duke/src/duke3d.h"
#include "../duke/src/dukeactor.h"
#include "../blood/src/blood.h"
#include "superset.h"
#include "mashup_player.h"
#include "c_dispatch.h"

namespace Superset
{
namespace
{
struct PickupDefinition { const char* name; int type; };
constexpr PickupDefinition pickups[] = {
	{"flaregun", Blood::kItemWeaponFlarePistol}, {"flares", Blood::kItemAmmoFlares},
	{"shotgun", Blood::kItemWeaponSawedoff}, {"tommy", Blood::kItemWeaponTommygun},
	{"spraycan", Blood::kItemWeaponSprayCan}, {"sprayfuel", Blood::kItemAmmoSprayCan},
	{"napalm", Blood::kItemWeaponNapalmLauncher}, {"gasoline", Blood::kItemAmmoGasolineCan},
	{"tnt", Blood::kItemWeaponTNT}, {"tntbundle", Blood::kItemAmmoTNTBundle}, {"tntbox", Blood::kItemAmmoTNTBox},
	{"proximity", Blood::kItemAmmoProxBombBundle}, {"remote", Blood::kItemAmmoRemoteBombBundle},
	{"tesla", Blood::kItemWeaponTeslaCannon}, {"teslacharge", Blood::kItemAmmoTeslaCharge},
	{"lifeleech", Blood::kItemWeaponLifeLeech}, {"souls", Blood::kItemAmmoTrappedSoul},
	{"voodoo", Blood::kItemWeaponVoodooDoll}, {"voodooammo", Blood::kItemAmmoVoodooDoll},
	{"shells", Blood::kItemAmmoSawedoffFew}, {"shellbox", Blood::kItemAmmoSawedoffBox},
	{"bullets", Blood::kItemAmmoTommygunFew}, {"drum", Blood::kItemAmmoTommygunDrum},
	{"medpouch", Blood::kItemHealthMedPouch}, {"essence", Blood::kItemHealthLifeEssense},
	{"lifeseed", Blood::kItemHealthLifeSeed}, {"redpotion", Blood::kItemHealthRedPotion},
	{"basicarmor", Blood::kItemArmorBasic}, {"bodyarmor", Blood::kItemArmorBody},
	{"firearmor", Blood::kItemArmorFire}, {"spiritarmor", Blood::kItemArmorSpirit}, {"superarmor", Blood::kItemArmorSuper}
};

int PickupType(const char* name)
{
	for (auto& pickup : pickups) if (!stricmp(name, pickup.name)) return pickup.type;
	return -1;
}

Blood::DBloodActor* FindPickupProbe()
{
	Blood::BloodStatIterator it(Blood::kStatItem);
	while (auto item = it.Next()) if (item->spr.intowner == 31020) return item;
	return nullptr;
}

Blood::DBloodActor* SpawnAhead(DCorePlayer* player, int type, bool flat = false)
{
	auto body = player->actor;
	auto direction = body->spr.Angles.Yaw.ToVector();
	THitInfo<DCoreActor> hit {};
	auto flags = body->spr.cstat;
	body->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	hitscan(body->getPosWithOffsetZ(), body->sector(), DVector3(direction * 1024., 0), hit, CLIPMASK1);
	body->spr.cstat = flags;
	double distance = hit.hitSector ? min(64., (hit.hitpos.XY() - body->spr.pos.XY()).Length() - 16.) : 64.;
	if (distance < (flat ? 56. : 16.)) return nullptr;
	auto position = body->spr.pos + direction * distance;
	auto sector = body->sector();
	updatesector(position, &sector);
	if (!sector) return nullptr;
	position.Z = getflorzofslopeptr(sector, position);
	if (flat && fabs(position.Z - body->spr.pos.Z) > 8.) return nullptr;
	return Blood::actSpawnPickup(sector, position, type);
}

struct InventorySnapshot
{
	int ammo[12], weapons[14], armor[3], health, weapon;
	bool active = false;
} snapshot;
}

bool BloodPickupSupported(int type)
{
	for (auto& pickup : pickups) if (pickup.type == type) return true;
	return false;
}

CCMD(superset_spawn_pickup)
{
	if (!IsActive() || gamestate != GS_LEVEL) return;
	int type = argv.argc() == 2 ? PickupType(argv[1]) : -1;
	if (type < 0)
	{
		Printf("Blood pickups:");
		for (auto& pickup : pickups) Printf(" %s", pickup.name);
		Printf("\n");
		return;
	}
	auto player = PlayerArray[myconnectindex];
	if (!IsBloodMap() && !Mashup::GetBloodPlayer(player->pnum))
	{
		Printf("The player inventory is not initialized yet.\n");
		return;
	}
	if (SpawnAhead(player, type)) Printf("Spawned Blood %s.\n", argv[1]);
	else Printf("Not enough room ahead for a pickup.\n");
}

CCMD(superset_inventory_check)
{
	if (!IsDukeMap() || gamestate != GS_LEVEL || argv.argc() < 2) return;
	auto duke = Duke3d::getPlayer(myconnectindex);
	auto blood = static_cast<Blood::DBloodPlayer*>(Mashup::GetBloodPlayer(myconnectindex));
	if (!blood || !Mashup::IsCombatPlayer(duke)) return;
	auto state = blood->GetActor();
	auto body = duke->GetActor();
	if (!stricmp(argv[1], "init"))
	{
		Blood::playerReset(blood);
		state->xspr.health = 800; body->spr.extra = duke->last_extra = duke->bloodPublishedHealth = 50;
		return;
	}
	if (!stricmp(argv[1], "place") && argv.argc() == 3)
	{
		int type = PickupType(argv[2]);
		if (type < 0) return;
		if (auto old = FindPickupProbe()) old->Destroy();
		auto angle = body->spr.Angles.Yaw;
		for (double turn : {0., 180., 90., -90.})
		{
			body->spr.Angles.Yaw = angle + DAngle::fromDeg(turn);
			state->spr.Angles.Yaw = body->spr.Angles.Yaw;
			duke->InitAngles(); blood->InitAngles();
			duke->vel.Zero(); state->vel.Zero(); body->vel.Zero();
			if (auto item = SpawnAhead(duke, type, true))
			{
				item->spr.intowner = 31020;
				Printf("Superset inventory placed %s: PASS.\n", argv[2]);
				return;
			}
		}
		Printf("Superset inventory placed %s: FAIL (clear floor required).\n", argv[2]);
		return;
	}
	if (!stricmp(argv[1], "collected") && argv.argc() == 3)
	{
		bool pass = !FindPickupProbe();
		int type = PickupType(argv[2]);
		switch (type)
		{
		case Blood::kItemWeaponSawedoff: pass &= blood->WeaponOwnership()[3] && blood->AmmoAmounts()[2] == 8 && blood->curWeapon == 3; break;
		case Blood::kItemWeaponTommygun: pass &= blood->WeaponOwnership()[4] && blood->AmmoAmounts()[3] == 50 && blood->curWeapon == 4; break;
		case Blood::kItemHealthLifeEssense: pass &= state->xspr.health == 1120 && body->spr.extra == 70; break;
		case Blood::kItemHealthMedPouch: pass &= state->xspr.health == 1600 && body->spr.extra == 100; break;
		case Blood::kItemArmorBody: pass &= blood->armor[0] == 1600 && blood->armor[1] == 0 && blood->armor[2] == 0; break;
		case Blood::kItemArmorSuper: pass &= blood->armor[0] == 3200 && blood->armor[1] == 3200 && blood->armor[2] == 3200; break;
		default: pass = false; break;
		}
		Printf("Superset inventory collected %s: %s (weapon=%d shells=%d bullets=%d health=%d armor=%d/%d/%d).\n",
			argv[2], pass ? "PASS" : "FAIL", blood->curWeapon, blood->AmmoAmounts()[2], blood->AmmoAmounts()[3], state->xspr.health,
			blood->armor[0], blood->armor[1], blood->armor[2]);
		return;
	}
	if (!stricmp(argv[1], "full"))
	{
		bool pass = FindPickupProbe() && state->xspr.health == 1600;
		Printf("Superset inventory full check: %s.\n", pass ? "PASS" : "FAIL");
		return;
	}
	if (!stricmp(argv[1], "snapshot"))
	{
		for (int i = 0; i < 12; ++i) snapshot.ammo[i] = blood->AmmoAmounts()[i];
		for (int i = 0; i < 14; ++i) snapshot.weapons[i] = blood->WeaponOwnership()[i];
		for (int i = 0; i < 3; ++i) snapshot.armor[i] = blood->armor[i];
		snapshot.health = state->xspr.health; snapshot.weapon = blood->curWeapon; snapshot.active = true;
		return;
	}
	if (!stricmp(argv[1], "clear"))
	{
		Blood::playerReset(blood);
		if (auto item = FindPickupProbe()) item->Destroy();
		return;
	}
	if (!stricmp(argv[1], "saved"))
	{
		bool pass = snapshot.active && snapshot.health == state->xspr.health && snapshot.weapon == blood->curWeapon && FindPickupProbe();
		for (int i = 0; i < 12; ++i) pass &= snapshot.ammo[i] == blood->AmmoAmounts()[i];
		for (int i = 0; i < 14; ++i) pass &= snapshot.weapons[i] == blood->WeaponOwnership()[i];
		for (int i = 0; i < 3; ++i) pass &= snapshot.armor[i] == blood->armor[i];
		Printf("Superset inventory save check: %s.\n", pass ? "PASS" : "FAIL");
	}
}
}
