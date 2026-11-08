#pragma once

struct GameInterface;
class DCorePlayer;
class FSerializer;

namespace Superset
{
// Frontend identity is independent of the map adapter's legacy game flags.
void Configure(int mapGameFlags);
bool IsActive();
bool IsBloodMap();
bool IsDukeMap();
bool CalebSelected();
bool BloodWeaponsRequested();
bool HasBloodMovement(DCorePlayer* player);
bool PrepareBloodMovement(DCorePlayer* player);
bool TickBloodMovement(DCorePlayer* player);
void FinishBloodMovement(DCorePlayer* player);
void TickSharedObjects();
bool BloodPickupSupported(int type);
void CollectBloodPickups(DCorePlayer* player);
void SerializeBloodControllerState(FSerializer& arc);
void ClearBloodControllerState();
::GameInterface* CreateInterface();
}
