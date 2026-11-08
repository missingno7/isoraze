#pragma once
#include "vectors.h"
class DCoreActor;
struct CollisionBase;
struct tspritetype;
namespace Blood { class DBloodActor; }
namespace Duke3d { class DDukeActor; }
namespace Superset
{
struct Damage;
bool ApplyBloodEnemyDamage(DCoreActor* actor, Damage damage, DCoreActor* source);
Blood::DBloodActor* BloodState(DCoreActor* actor);
void MapBloodCollision(CollisionBase& hit);
void TickBloodEnemies();
void PublishBloodEnemies();
void PrepareBloodWorldObjects();
void BloodEnemyHit(Duke3d::DDukeActor* actor, Duke3d::DDukeActor* hitter);
bool AnimateBloodEnemy(tspritetype* sprite, Duke3d::DDukeActor* actor, const DVector2& view);
bool HasBloodEnemy(DCoreActor* actor);
void ApplyBloodVectorImpulse(DCoreActor* actor, int vector, const DVector3& direction);
}
