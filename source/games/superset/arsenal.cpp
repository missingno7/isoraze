#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif
#include "../duke/src/duke3d.h"
#include "../duke/src/dukeactor.h"
#include "../blood/src/blood.h"
#include "arsenal.h"
#include "superset.h"
#include "duke_weapons.h"
#include "mashup_player.h"
#include "c_cvars.h"
#include "c_dispatch.h"
#include "gamestate.h"

// 0: manual, 1: newly acquired weapons, 2: every eligible weapon pickup.
CUSTOM_CVAR(Int, superset_pickup_switch, 1, CVAR_ARCHIVE)
{
	if (self < 0 || self > 2) self = clamp(int(self), 0, 2);
}

namespace Superset
{
namespace
{
struct WeaponDefinition { int id; const char* name; bool blood; int native, bank; };
constexpr WeaponDefinition weapons[] = {
	{1,"duke.knee",false,0,1}, {2,"duke.pistol",false,1,2},
	{3,"duke.shotgun",false,2,3}, {4,"duke.chaingun",false,3,4},
	{5,"duke.rpg",false,4,5}, {6,"duke.pipebomb",false,5,6},
	{7,"duke.shrinker",false,6,7}, {8,"duke.devastator",false,7,8},
	{9,"duke.tripbomb",false,8,9}, {10,"duke.freezer",false,9,10},
	{11,"duke.remote",false,10,6}, {12,"duke.expander",false,11,7},
	{13,"duke.flamethrower",false,12,11},
	{33,"blood.pitchfork",true,1,1}, {34,"blood.flaregun",true,2,2},
	{35,"blood.shotgun",true,3,3}, {36,"blood.tommygun",true,4,4},
	{37,"blood.napalm",true,5,5}, {38,"blood.dynamite",true,6,6},
	{39,"blood.spraycan",true,7,7}, {40,"blood.tesla",true,8,8},
	{41,"blood.lifeleech",true,9,9}, {42,"blood.voodoo",true,10,10},
	{43,"blood.proximity",true,11,11}, {44,"blood.remote",true,12,12},
	{45,"blood.beast",true,13,1}
};
const WeaponDefinition* Definition(int id)
{
	for (auto& weapon : weapons) if (weapon.id == id) return &weapon;
	return nullptr;
}
bool Available(const WeaponDefinition& weapon, DCorePlayer* owner, Blood::DBloodPlayer* blood, Duke3d::DDukePlayer* duke)
{
	if (weapon.blood)
		// Beast hands belong to the native transformation, not normal weapon
		// cycling (their native weaponModes entry has no selectable modes).
		return weapon.native != Blood::kWeapBeast && blood->WeaponOwnership()[weapon.native] && (owner == blood || weapon.native == Blood::kWeapPitchFork ||
			weapon.native == Blood::kWeapShotgun || weapon.native == Blood::kWeapTommyGun || weapon.native == Blood::kWeapSpraycan);
	if (!duke->WeaponOwnership()[weapon.native] || (owner != duke && !DukeWeaponSupported(weapon.native))) return false;
	// The detonator is an action on deployed bombs, not a free starting gun.
	if (weapon.native == Duke3d::HANDREMOTE_WEAPON && !duke->hbomb_on) return false;
	return true;
}
TArray<int> Bank(DGameplayInventory* inventory, int bank)
{
	if (inventory->weaponBanks[bank - 1].Size()) return inventory->weaponBanks[bank - 1];
	TArray<int> result;
	for (auto& weapon : weapons) if (weapon.bank == bank) result.Push(weapon.id);
	return result;
}
bool DukeCanStow(Duke3d::DDukePlayer* duke)
{
	return !duke->kickback_pic && !duke->rapid_fire_hold && !duke->quick_kick && !duke->knee_incs;
}
void Request(DGameplayInventory* inventory, int id)
{
	if (Definition(id)) inventory->pendingWeapon = id;
}
}

int SharedWeaponId(bool blood, int nativeWeapon)
{
	for (auto& weapon : weapons) if (weapon.blood == blood && weapon.native == nativeWeapon) return weapon.id;
	return 0;
}

int RequestedWeaponFamily()
{
	if (!IsActive() || !PlayerArray[myconnectindex]) return -1;
	auto inventory = PlayerArray[myconnectindex]->sharedInventory.Get();
	if (!inventory) return -1;
	auto weapon = Definition(inventory->pendingWeapon ? inventory->pendingWeapon : inventory->selectedWeapon);
	return weapon ? int(weapon->blood) : -1;
}

void PrepareArsenalInput(DCorePlayer* owner, Blood::DBloodPlayer* blood, Duke3d::DDukePlayer* duke)
{
	if (!IsActive() || !owner->sharedInventory) return;
	auto inventory = owner->sharedInventory.Get();
	if (!inventory->selectedWeapon || (!inventory->pendingWeapon && duke->supersetWeaponMode >= 0))
	{
		bool useBlood = duke->supersetWeaponMode < 0 ? BloodWeaponsRequested() : duke->supersetWeaponMode != 0;
		inventory->selectedWeapon = SharedWeaponId(useBlood, useBlood ? (blood->curWeapon ? blood->curWeapon : Blood::kWeapPitchFork) : duke->curr_weapon);
	}
	int input = owner->cmd.ucmd.getNewWeapon();
	owner->cmd.ucmd.setNewWeapon(0);
	TArray<int> choices;
	if (input >= 1 && input <= 12) choices = Bank(inventory, input);
	else if (input == WeaponSel_Next || input == WeaponSel_Prev || input == WeaponSel_Alt)
	{
		for (int bank = 1; bank <= 12; ++bank) for (int id : Bank(inventory, bank)) choices.Push(id);
	}
	if (choices.Size())
	{
		int current = inventory->pendingWeapon ? inventory->pendingWeapon : inventory->selectedWeapon;
		int start = -1;
		for (unsigned i = 0; i < choices.Size(); ++i) if (choices[i] == current) { start = int(i); break; }
		int direction = input == WeaponSel_Prev ? -1 : 1;
		if (start < 0) start = direction > 0 ? -1 : 0;
		for (unsigned step = 1; step <= choices.Size(); ++step)
		{
			int index = (start + direction * int(step) + int(choices.Size()) * 2) % int(choices.Size());
			auto weapon = Definition(choices[index]);
			if (weapon && Available(*weapon, owner, blood, duke)) { Request(inventory, weapon->id); break; }
		}
	}
}

void ApplyArsenalSelection(DCorePlayer* owner, Blood::DBloodPlayer* blood, Duke3d::DDukePlayer* duke)
{
	if (!IsActive() || !owner->sharedInventory) return;
	auto inventory = owner->sharedInventory.Get();
	auto weapon = Definition(inventory->pendingWeapon);
	if (!weapon) return;
	if (!Available(*weapon, owner, blood, duke)) { inventory->pendingWeapon = 0; return; }
	if (duke->supersetWeaponMode != int(weapon->blood)) return;
	int current = weapon->blood ? blood->curWeapon : duke->curr_weapon;
	if (current == weapon->native)
	{
		inventory->selectedWeapon = weapon->id;
		inventory->pendingWeapon = 0;
		return;
	}
	if (weapon->blood) blood->newWeapon = weapon->native; // Native QAV owns safe completion.
	else if (DukeCanStow(duke)) Duke3d::fi.addweapon(duke, weapon->native, true);
	// Release pending attacks; native reload/throw callbacks still complete.
	owner->cmd.ucmd.actions &= ~(SB_FIRE | SB_ALTFIRE | SB_HOLSTER | SB_QUICK_KICK);
}

void WeaponPickup(DCorePlayer* view, bool blood, int nativeWeapon, bool newlyOwned)
{
	if (!IsActive() || !view || !view->sharedInventory || superset_pickup_switch == 0 ||
		(superset_pickup_switch == 1 && !newlyOwned)) return;
	Request(view->sharedInventory, SharedWeaponId(blood, nativeWeapon));
}

void RequestLegacyWeaponFamily(bool blood)
{
	if (!IsActive() || !PlayerArray[myconnectindex]) return;
	auto inventory = PlayerArray[myconnectindex]->sharedInventory.Get();
	if (!inventory) return;
	auto owner = PlayerArray[myconnectindex];
	auto duke = dyn_cast<Duke3d::DDukePlayer>(owner);
	if (!duke) duke = Duke3d::SecondaryDukePlayer(owner->pnum);
	auto bloodPlayer = dyn_cast<Blood::DBloodPlayer>(owner);
	if (!bloodPlayer) bloodPlayer = static_cast<Blood::DBloodPlayer*>(Mashup::GetBloodPlayer(owner->pnum));
	if (duke && bloodPlayer)
	{
		int native = blood ? (bloodPlayer->curWeapon ? bloodPlayer->curWeapon : duke->supersetStowedBloodWeapon) : duke->curr_weapon;
		int previous = SharedWeaponId(blood, native);
		if (previous && (blood ? inventory->bloodWeapons[native] : inventory->dukeWeapons[native])) { Request(inventory, previous); return; }
	}
	for (auto& weapon : weapons)
		if (weapon.blood == blood && (blood ? inventory->bloodWeapons[weapon.native] : inventory->dukeWeapons[weapon.native]))
		{
			Request(inventory, weapon.id);
			return;
		}
}

CCMD(superset_weapon_bank)
{
	if (!IsActive() || gamestate != GS_LEVEL || argv.argc() < 2) return;
	int bank = atoi(argv[1]);
	if (bank < 1 || bank > 12) return;
	auto inventory = PlayerArray[myconnectindex]->sharedInventory.Get();
	if (!inventory) return;
	if (argv.argc() > 2)
	{
		TArray<int> order;
		for (int arg = 2; arg < argv.argc(); ++arg)
		{
			const WeaponDefinition* found = nullptr;
			for (auto& weapon : weapons) if (!stricmp(argv[arg], weapon.name)) found = &weapon;
			if (!found || order.Find(found->id) < order.Size()) { Printf("Invalid or repeated weapon: %s\n", argv[arg]); return; }
			order.Push(found->id);
		}
		inventory->weaponBanks[bank - 1] = std::move(order);
	}
	Printf("Superset bank %d:", bank);
	for (int id : Bank(inventory, bank)) if (auto weapon = Definition(id)) Printf(" %s", weapon->name);
	Printf("\n");
}

CCMD(superset_arsenal_check)
{
	if (!IsActive() || gamestate != GS_LEVEL) return;
	auto owner = PlayerArray[myconnectindex];
	auto blood = dyn_cast<Blood::DBloodPlayer>(owner);
	if (!blood) blood = static_cast<Blood::DBloodPlayer*>(Mashup::GetBloodPlayer(owner->pnum));
	auto duke = dyn_cast<Duke3d::DDukePlayer>(owner);
	if (!duke && blood) duke = PrepareBloodMapDukePlayer(blood);
	if (!blood || !duke || !owner->sharedInventory) { Printf("Superset arsenal: FAIL (missing views).\n"); return; }
	auto inventory = owner->sharedInventory.Get();
	if (argv.argc() == 2 && !stricmp(argv[1], "give"))
	{
		inventory->dukeAmmo[Duke3d::SHOTGUN_WEAPON] = inventory->bloodAmmo[2] = 20;
		inventory->dukeWeapons[Duke3d::SHOTGUN_WEAPON] = inventory->bloodWeapons[Blood::kWeapShotgun] = true;
	}
	bool alias = blood->sharedInventory == inventory && duke->sharedInventory == inventory &&
		&blood->AmmoAmounts()[2] == &inventory->bloodAmmo[2] && &duke->AmmoAmounts()[2] == &inventory->dukeAmmo[2];
	Printf("Superset arsenal: %s (one inventory, selected=%d pending=%d, Duke shells=%d Blood shells=%d).\n",
		alias ? "PASS" : "FAIL", inventory->selectedWeapon, inventory->pendingWeapon, inventory->dukeAmmo[2], inventory->bloodAmmo[2]);
}

void BindInventory(DCorePlayer* owner, Blood::DBloodPlayer* blood, Duke3d::DDukePlayer* duke)
{
	if (!IsActive() || !owner || !blood || !duke) return;
	auto inventory = owner->sharedInventory.Get();
	if (!inventory)
	{
		inventory = Create<DGameplayInventory>();
		// One-time import of native starting loadouts. Once bound, all accesses
		// below return references into this object, never synchronized copies.
		memcpy(inventory->bloodAmmo, blood->AmmoAmounts(), sizeof(inventory->bloodAmmo));
		memcpy(inventory->bloodWeapons, blood->WeaponOwnership(), sizeof(inventory->bloodWeapons));
		memcpy(inventory->dukeAmmo, duke->AmmoAmounts(), sizeof(inventory->dukeAmmo));
		memcpy(inventory->dukeWeapons, duke->WeaponOwnership(), sizeof(inventory->dukeWeapons));
		inventory->dukeWeapons[Duke3d::KNEE_WEAPON] = true;
	}
	for (auto player : {owner, static_cast<DCorePlayer*>(blood), static_cast<DCorePlayer*>(duke)})
	{
		player->sharedInventory = inventory;
		GC::WriteBarrier(player, inventory);
	}
}

void ResetArsenalNewGame()
{
	for (auto player : PlayerArray) if (player) player->sharedInventory = nullptr;
}
}
