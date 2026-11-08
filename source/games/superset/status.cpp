#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif
#include "../duke/src/duke3d.h"
#include "../duke/src/dukeactor.h"
#include "../blood/src/blood.h"
#include "../blood/src/endgame.h"
#include "status.h"
#include "superset.h"
#include "enemies.h"
#include "units.h"
#include "damage.h"
#include "projectiles.h"
#include "mashup_player.h"
#include "c_dispatch.h"
#include "savegamemanager.h"

namespace Superset
{
namespace
{
int flightIndex = -1, flightStart = 0, flightSequence = -1, flightFrame = -1, flightFrameTime = -1;
bool sawFlight = false, flightPaused = true;
}
bool IsFrozen(DCoreActor* actor)
{
	if (!IsActive() || !actor) return false;
	if (auto state = dyn_cast<Blood::DBloodActor>(actor)) actor = state->supersetWorldActor;
	auto body = dyn_cast<Duke3d::DDukeActor>(actor);
	return body && body->supersetFreezeTime > 0;
}

bool Freeze(DCoreActor* actor, DCoreActor* source, int clocks)
{
	if (!HasBloodEnemy(actor) || clocks <= 0) return false;
	auto body = static_cast<Duke3d::DDukeActor*>(actor);
	auto state = BloodState(body);
	if (!state || !state->IsDudeActor() || !state->xspr.health || state->spr.statnum != Blood::kStatDude ||
		!body->IntVar(FName("supersetfreezable"))) return false;
	// Duke's already-frozen projectile gate does not refresh the thaw timer.
	if (IsFrozen(body)) return true;
	Extinguish(body);
	body->spr.setspritetexture(state->spr.spritetexture());
	body->supersetFreezeTime = clocks;
	if (auto world = Mashup::WorldSoundActor(source)) source = world;
	body->supersetFreezeSource = source;
	GC::WriteBarrier(body, source);
	state->vel.SetXY(DVector2(0, 0)); // Retain gravity and moving-surface physics.
	body->spr.pal = 1; // Native Duke projectile/autoaim identity; artwork uses Blood cold.plu.
	Duke3d::S_PlayActorSound(SOMETHINGFROZE, body);
	return true;
}

bool Extinguish(DCoreActor* actor)
{
	if (!IsActive() || !actor) return false;
	if (HasBloodEnemy(actor))
	{
		auto state = BloodState(actor);
		if (!state || !state->IsDudeActor() || !state->xspr.health) return false;
		state->xspr.burnTime = 0;
		state->SetBurnSource(nullptr);
		Blood::evKillActor(state, Blood::kCallbackFXFlameLick);
		// The shared Blood receiver currently supports the axe zombie. Its
		// burning AI is a separate native type, not a consequence of burnTime.
		// Restore its ordinary brain without healing or resetting its target.
		if (state->spr.type == Blood::kDudeBurningZombieAxe)
		{
			state->spr.type = Blood::kDudeZombieAxeNormal;
			Blood::aiNewState(state, &Blood::zombieAGoto);
		}
		return true;
	}
	if (auto duke = dyn_cast<Duke3d::DDukeActor>(actor))
	{
		duke->supersetBurnTime = duke->supersetBurnFxTime = 0;
		duke->supersetBurnSource = nullptr;
		return true;
	}
	return false;
}

bool Thaw(DCoreActor* actor)
{
	if (!HasBloodEnemy(actor) || !IsFrozen(actor)) return false;
	auto body = static_cast<Duke3d::DDukeActor*>(actor);
	body->supersetFreezeTime = 0;
	body->supersetFreezeSource = nullptr;
	if (auto state = BloodState(body)) body->spr.pal = state->spr.pal;
	return true;
}

bool Shatter(DCoreActor* actor, DCoreActor* source)
{
	if (!HasBloodEnemy(actor) || !IsFrozen(actor)) return false;
	auto body = static_cast<Duke3d::DDukeActor*>(actor);
	auto state = BloodState(body);
	if (!state || !state->IsDudeActor() || !state->xspr.health) return false;
	// Native Duke frozen death: one kill, ice fragments, no ordinary flesh death.
	Blood::AddKill(BloodState(source), state);
	Duke3d::spriteglass(body, 30);
	Duke3d::S_PlayActorSound(GLASS_BREAKING, body);
	Blood::seqKill(state);
	Blood::evKillActor(state);
	state->Destroy();
	body->Destroy();
	return true;
}

void TickFreezeEffects()
{
	Duke3d::DukeSpriteIterator it;
	while (auto body = it.Next())
	{
		if (!body->supersetFreezeTime) continue;
		auto state = BloodState(body);
		if (!state || !state->IsDudeActor() || !state->xspr.health)
		{
			Thaw(body);
			continue;
		}
		if (body->GetIndex() == flightIndex)
		{
			if (PlayClock < flightStart) flightIndex = -1; // A map/load ended the observation.
			else
			{
				auto sequence = Blood::GetInstance(state);
				flightPaused &= sequence && sequence->nSeqID == flightSequence && sequence->frameIndex == flightFrame && sequence->timeCounter == flightFrameTime;
				sawFlight |= state->xspr.height > 1;
				if (sawFlight && !state->xspr.height && state->vel.Z == 0)
				{
					Printf("Superset frozen gravity: %s (native rise/landing with paused SEQ).\n", flightPaused ? "PASS" : "FAIL");
					flightIndex = -1;
				}
			}
		}
		if (body->supersetFreezeTime <= Units::ClocksPerStep) Thaw(body);
		else body->supersetFreezeTime -= Units::ClocksPerStep;
	}
}

CCMD(superset_freeze_check)
{
	if (!IsDukeMap() || gamestate != GS_LEVEL || argv.argc() != 2) return;
	auto player = Duke3d::getPlayer(myconnectindex);
	if (!stricmp(argv[1], "launch")) { Duke3d::shoot(player->GetActor(), Duke3d::DukeFreezeBlastClass); return; }
	if (!stricmp(argv[1], "quiet"))
	{
		// Fixture boundary: remove leftover ricochets and pre-freeze queued hits
		// before measuring a vulnerable player's health during the frozen hold.
		Duke3d::DukeSpriteIterator bolts;
		while (auto bolt = bolts.Next()) if (bolt->IsKindOf(Duke3d::DukeFreezeBlastClass)) bolt->Destroy();
		player->GetActor()->hitextra = -1;
		player->GetActor()->supersetPendingDamage = false;
		player->GetActor()->supersetDamageRemainder = 0;
		return;
	}
	if (!stricmp(argv[1], "vulnerable")) { Duke3d::ud.god = 0; return; }
	Duke3d::DukeSpriteIterator it;
	Duke3d::DDukeActor* body = nullptr;
	while (auto candidate = it.Next()) if (HasBloodEnemy(candidate)) { body = candidate; break; }
	if (!body) return;
	auto state = BloodState(body);
	if (!state) return;
	if (!stricmp(argv[1], "freeze")) { Freeze(body, player->GetActor(), int(Units::FromSimulationSteps(138))); return; }
	if (!stricmp(argv[1], "thaw")) { Thaw(body); return; }
	if (!stricmp(argv[1], "ignite")) { Ignite(body, player->GetActor(), 2400); return; }
	if (!stricmp(argv[1], "toss"))
	{
		flightIndex = body->GetIndex(); flightStart = PlayClock;
		auto sequence = Blood::GetInstance(state);
		flightSequence = sequence ? sequence->nSeqID : -1;
		flightFrame = sequence ? sequence->frameIndex : -1;
		flightFrameTime = sequence ? sequence->timeCounter : -1;
		sawFlight = false; flightPaused = true;
		state->vel.Z -= 8;
		return;
	}
	if (!stricmp(argv[1], "burn"))
	{
		Freeze(body, player->GetActor(), int(Units::FromSimulationSteps(138)));
		Ignite(body, player->GetActor(), 2400);
		ApplyDukeHealthDamage(body, {int(state->xspr.health) - 1, DamageKind::Heat}, player->GetActor());
		return;
	}
	if (!stricmp(argv[1], "responses"))
	{
		int health = state->xspr.health;
		body->FloatVar(FName("supersetcoldfactor")) = 0;
		bool pass = ApplyBloodEnemyDamage(body, {16, DamageKind::Cold}, player->GetActor()) && state->xspr.health == health;
		pass &= Freeze(body, player->GetActor(), 552) && state->xspr.health == health;
		pass &= Thaw(body);
		body->FloatVar(FName("supersetcoldfactor")) = .5;
		pass &= ApplyBloodEnemyDamage(body, {8, DamageKind::Cold}, player->GetActor()) && state->xspr.health == health - 4 && !IsFrozen(body);
		body->IntVar(FName("supersetfreezable")) = false;
		pass &= !Freeze(body, player->GetActor(), 552);
		body->FloatVar(FName("supersetcoldfactor")) = 1;
		body->IntVar(FName("supersetfreezable")) = true;
		Printf("Superset freeze responses: %s (Cold factor, Q4 health, independent status, freeze capability).\n", pass ? "PASS" : "FAIL");
		return;
	}
	Printf("Superset freeze: remaining=%d health=%d sequence=%d frame=%d timer=%d.\n", body->supersetFreezeTime, state->xspr.health,
		Blood::seqGetID(state), Blood::seqGetStatus(state), state->xspr.stateTimer);
}
}
