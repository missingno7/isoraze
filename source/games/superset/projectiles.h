#pragma once
#include "vectors.h"
#include "textureid.h"
namespace Blood { class DBloodActor; struct ProjectileWorldServices; }
class DCoreActor;
class DCorePlayer;
class FSerializer;
namespace Superset
{
int BloodDifficulty();
int BloodImpactSurface(FTextureID texture);
void BindProjectileEnvironment(bool bloodSource);
Blood::ProjectileWorldServices* ProjectileEnvironment();
void TickProjectileObjects();
void TickThingObjects();
bool TryUseBloodThing(DCorePlayer* player);
void TickProjectileExplosions();
void TickProjectileAttachments();
void ObserveProjectileImpact(Blood::DBloodActor* actor, DCoreActor* target);
void ObserveProjectileSpawn(Blood::DBloodActor* actor);
bool Ignite(DCoreActor* target, DCoreActor* source, int clocks);
void TickStatusEffects();
void SerializeProjectileOptions(FSerializer& arc);
}
