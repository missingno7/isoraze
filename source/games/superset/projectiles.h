#pragma once
#include "vectors.h"
namespace Blood { class DBloodActor; }
class DCoreActor;
class FSerializer;
namespace Superset
{
int BloodDifficulty();
bool FireBloodMissile(Blood::DBloodActor* source, double xyoff, double zoff, const DVector3& direction, int type, Blood::DBloodActor*& result);
void TickBloodProjectiles();
bool Ignite(DCoreActor* target, DCoreActor* source, int clocks);
void TickStatusEffects();
void SerializeProjectileOptions(FSerializer& arc);
}
