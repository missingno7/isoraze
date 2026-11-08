#include "gameplayinventory.h"
#include "serializer.h"
#include "vm.h"

IMPLEMENT_CLASS(DGameplayInventory, false, false)

void DGameplayInventory::Serialize(FSerializer& arc)
{
	Super::Serialize(arc);
	arc.Array("duke_ammo", dukeAmmo, 17).Array("duke_weapons", dukeWeapons, 17)
		.Array("blood_ammo", bloodAmmo, 12).Array("blood_weapons", bloodWeapons, 14)
		("selected_weapon", selectedWeapon)("pending_weapon", pendingWeapon);
	for (int bank = 0; bank < 12; ++bank)
		arc(FStringf("bank_%d", bank + 1).GetChars(), weaponBanks[bank]);
}

DEFINE_FIELD(DGameplayInventory, dukeAmmo)
DEFINE_FIELD(DGameplayInventory, dukeWeapons)
DEFINE_FIELD(DGameplayInventory, bloodAmmo)
DEFINE_FIELD(DGameplayInventory, bloodWeapons)
DEFINE_FIELD(DGameplayInventory, selectedWeapon)
DEFINE_FIELD(DGameplayInventory, pendingWeapon)
