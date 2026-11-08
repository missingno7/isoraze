#pragma once
#include "vectors.h"
namespace Blood { class DBloodActor; struct ProjectileWorldServices; }
class DCoreActor;
class FSerializer;
namespace Superset
{
int BloodDifficulty();
void BindProjectileEnvironment(bool bloodSource);
Blood::ProjectileWorldServices* ProjectileEnvironment();
void TickProjectileObjects();
void TickProjectileExplosions();
void TickProjectileAttachments();
void ObserveProjectileImpact(Blood::DBloodActor* actor, DCoreActor* target);
void ObserveProjectileSpawn(Blood::DBloodActor* actor);
bool Ignite(DCoreActor* target, DCoreActor* source, int clocks);
void TickStatusEffects();
void SerializeProjectileOptions(FSerializer& arc);
}
