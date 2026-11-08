#pragma once
#include "vectors.h"
class FSerializer;
struct tspritetype;
namespace Superset
{
void InitDukeEffects();
void ClearDukeEffects();
void SerializeDukeEffects(FSerializer& arc);
void BackupDukeEffects();
void TickDukeEffects();
bool DrawDukeEffect(tspritetype* sprite, double fraction, const DVector2& view, DAngle angle);
}
