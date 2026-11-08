#pragma once

struct GameInterface;
class DCorePlayer;
class FSerializer;

namespace Superset
{
// Frontend identity is independent of the map adapter's legacy game flags.
void Configure(int mapGameFlags);
bool IsActive();
bool CalebSelected();
bool HasBloodMovement(DCorePlayer* player);
bool PrepareBloodMovement(DCorePlayer* player);
bool TickBloodMovement(DCorePlayer* player);
void FinishBloodMovement(DCorePlayer* player);
void TickSharedObjects();
void SerializeBloodControllerState(FSerializer& arc);
void ClearBloodControllerState();
::GameInterface* CreateInterface();
}
