#pragma once
class DCorePlayer;
namespace Blood { class DBloodPlayer; }
namespace Duke3d { class DDukePlayer; }
namespace Superset
{
void BindInventory(DCorePlayer* owner, Blood::DBloodPlayer* blood, Duke3d::DDukePlayer* duke);
void ResetArsenalNewGame();
// Stable definition IDs, independent of native slot numbers and packet banks.
int SharedWeaponId(bool blood, int nativeWeapon);
int RequestedWeaponFamily(); // -1 until the starting loadout is imported.
void PrepareArsenalInput(DCorePlayer* owner, Blood::DBloodPlayer* blood, Duke3d::DDukePlayer* duke);
void ApplyArsenalSelection(DCorePlayer* owner, Blood::DBloodPlayer* blood, Duke3d::DDukePlayer* duke);
void WeaponPickup(DCorePlayer* view, bool blood, int nativeWeapon, bool newlyOwned);
void RequestLegacyWeaponFamily(bool blood);
}
