#pragma once
class DCoreActor;
namespace Superset
{
// Effects are separate requests from health damage. Native adapters may retain
// dedicated implementations (e.g. Duke CON frozen states).
bool IsFrozen(DCoreActor* actor);
bool Extinguish(DCoreActor* actor);
bool Freeze(DCoreActor* actor, DCoreActor* source, int clocks);
bool Thaw(DCoreActor* actor);
bool Shatter(DCoreActor* actor, DCoreActor* source);
void TickFreezeEffects();
}
