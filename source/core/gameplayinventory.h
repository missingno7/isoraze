#pragma once
#include "dobject.h"

// One arsenal, shared by native controller views. These namespaces deliberately
// retain distinct ammunition and ownership; index translation belongs to the
// weapon definitions, not to reconciliation between two inventories.
class DGameplayInventory : public DObject
{
	DECLARE_CLASS(DGameplayInventory, DObject)
public:
	short dukeAmmo[17] {};
	bool dukeWeapons[17] {};
	int bloodAmmo[12] {};
	bool bloodWeapons[14] {};
	int selectedWeapon = 0;
	int pendingWeapon = 0;
	TArray<int> weaponBanks[12]; // Empty means the definition order for this bank.
	void Serialize(FSerializer& arc) override;
};
