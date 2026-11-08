#pragma once

#include "vectors.h"

class DCorePlayer;
class DCoreActor;

namespace Mashup
{
// The Duke pawn is the physical player; the secondary Blood player owns combat state.
bool CombatEnabled();
bool IsCombatPlayer(DCorePlayer* player);
DCoreActor* WorldSoundActor(DCoreActor* source);
DCorePlayer* GetBloodPlayer(int player);
void PrepareCombat(DCorePlayer* player);
void RefreshBloodPlayerStates();
void TickCombat(DCorePlayer* player);
bool DrawCombat(DCorePlayer* player, double interpolation);
void ResetCombatPlayers();
bool FireBloodVector(DCoreActor* source, double offset, double zoffset, DVector3 direction, int type, double range);
}
