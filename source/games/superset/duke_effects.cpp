#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif
#include "../duke/src/duke3d.h"
#include "../duke/src/dukeactor.h"
#include "../blood/src/blood.h"
#include "duke_effects.h"
#include "duke_projectiles.h"
#include "duke_weapons.h"
#include "superset.h"
#include "mashup_resources.h"
#include "savegamehelp.h"
#include "c_dispatch.h"
#include "gamestate.h"
#include "buildtiles.h"

namespace Superset
{
namespace
{
void MarkEffects() { GC::MarkArray(Duke3d::spriteq, 1024); }
bool Supported(Duke3d::DDukeActor* actor)
{
	return actor && ((actor->supersetForeignFx && actor->IsKindOf(PClass::FindClass("DukeBloodSplat1"))) ||
		IsBloodDukeProjectile(actor) || IsDukeProjectileEffect(actor) || IsDukeWeaponEffect(actor));
}
Blood::DBloodActor* MissileTarget()
{
	Blood::BloodSpriteIterator it;
	while (auto actor = it.Next()) if (actor->spr.intowner == 31032) return actor;
	return nullptr;
}
void ClearMissileTarget()
{
	auto target = MissileTarget();
	if (!target) return;
	Blood::BloodStatIterator it(Blood::kStatFlare);
	while (auto flare = it.Next()) if (flare->GetTarget() == target)
	{
		Blood::evKillActor(flare); Blood::seqKill(flare); flare->Destroy();
	}
	Blood::evKillActor(target); Blood::seqKill(target); target->Destroy();
}
}

void InitDukeEffects()
{
	GC::AddMarkerFunc(MarkEffects);
}
void ClearDukeEffects()
{
	for (auto& entry : Duke3d::spriteq) entry = nullptr;
	Duke3d::spriteqloc = 0;
}
void SerializeDukeEffects(FSerializer& arc)
{
	if (arc.BeginObject("superset.duke_effects"))
	{
		// Native Initialize/insertspriteq still owns insertion and eviction.
		// The host adapter supplies this component's save/GC/map lifetime.
		arc("capacity", Duke3d::spriteqamount)("position", Duke3d::spriteqloc).Array("queue", Duke3d::spriteq, 1024).EndObject();
		if (arc.isReading())
		{
			Duke3d::spriteqamount = clamp(Duke3d::spriteqamount, 0, 1024);
			Duke3d::spriteqloc = Duke3d::spriteqamount ? clamp(Duke3d::spriteqloc, 0, Duke3d::spriteqamount - 1) : 0;
		}
	}
}
void BackupDukeEffects()
{
	Duke3d::DukeSpriteIterator it;
	while (auto actor = it.Next()) if (Supported(actor)) actor->backuploc();
}
void TickDukeEffects()
{
	Duke3d::DukeSpriteIterator it;
	while (auto actor = it.Next())
	{
		if (!Supported(actor)) continue;
		if (!actor->sector() || actor->spr.scale.X == 0 || (actor->flags2 & SFLAG2_DIENOW)) actor->Destroy();
		else
		{
			// Only component adapters with safe world/contact boundaries run here.
			Duke3d::CallTick(actor);
			// Native laser trails die after Animate marks them seen. Give an unseen
			// foreign trail one render opportunity too, then retire it when culled.
			// counter is unused by this native trail class and already serializes.
			if (!(actor->ObjectFlags & OF_EuthanizeMe) && IsDukeProjectileEffect(actor) && ++actor->counter >= 2) actor->Destroy();
			if (actor->flags2 & SFLAG2_DIENOW) actor->Destroy();
		}
	}
}
bool DrawDukeEffect(tspritetype* sprite, double fraction)
{
	auto actor = dyn_cast<Duke3d::DDukeActor>(sprite->ownerActor);
	if (!IsBloodMap() || !Supported(actor)) return false;
	if (cl_interpolate)
	{
		sprite->pos = actor->interpolatedpos(fraction);
		sprite->Angles.Yaw = actor->interpolatedyaw(fraction);
	}
	Duke3d::CallAnimate(actor, sprite);
	return true;
}

CCMD(superset_spawn_duke_effect)
{
	if (!IsBloodMap() || gamestate != GS_LEVEL || numplayers != 1) return;
	if (argv.argc() != 2 || stricmp(argv[1], "splat")) { Printf("superset_spawn_duke_effect splat\n"); return; }
	auto body = PlayerArray[myconnectindex]->GetActor();
	HitInfoBase hit {};
	auto direction = body->spr.Angles.Yaw.ToVector();
	auto flags = body->spr.cstat;
	body->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	hitscan(body->getPosWithOffsetZ(), body->sector(), DVector3(direction * 1024, 0), hit, CLIPMASK1);
	body->spr.cstat = flags;
	if (!hit.hitWall || !hit.hitSector || (hit.hitpos.XY() - body->spr.pos.XY()).Length() > 128)
	{
		Printf("Superset: face a wall within 128 units to place the native Duke decal.\n");
		return;
	}
	auto type = PClass::FindActor("DukeBloodSplat1");
	auto actor = Duke3d::CreateActor(hit.hitSector, hit.hitpos + DVector3(-direction * .125, 16), type,
		0, DVector2(0, 0), hit.hitWall->delta().Angle() + DAngle90, 0, 0, nullptr, STAT_MISC);
	if (!actor) return;
	actor->supersetForeignFx = true;
	Duke3d::CallInitialize(actor, nullptr);
	actor->backuploc();
	Printf("Superset native Duke decal placed.\n");
}

CCMD(superset_duke_effect_check)
{
	if (!IsBloodMap() || gamestate != GS_LEVEL) return;
	if (argv.argc() > 1 && !stricmp(argv[1], "target"))
	{
		ClearMissileTarget();
		auto body = Blood::getPlayer(myconnectindex)->GetActor();
		auto target = Blood::actSpawnDude(body, Blood::kDudeZombieAxeNormal, 40);
		if (target)
		{
			double top, bottom;
			Blood::GetActorExtents(target, &top, &bottom);
			target->spr.pos.Z += getflorzofslopeptr(target->sector(), target->spr.pos) - bottom;
			// Fixture control: retain native collision/damage, hold the brain in
			// place and leave enough health to inspect a real flare's ignition.
			target->spr.flags |= 32;
			target->spr.intowner = 31032;
			target->xspr.health = 16000;
			target->backuploc();
		}
		Printf("Superset native flare target setup: %s.\n", target ? "PASS" : "FAIL");
		return;
	}
	if (argv.argc() > 1 && !stricmp(argv[1], "cleartarget")) { ClearMissileTarget(); return; }
	if (argv.argc() > 1 && !stricmp(argv[1], "targetresume"))
	{
		if (auto target = MissileTarget()) target->spr.flags &= ~32;
		return;
	}
	if (argv.argc() > 1 && !stricmp(argv[1], "facewall"))
	{
		auto player = PlayerArray[myconnectindex];
		auto body = player->GetActor();
		double nearest = 128;
		DAngle facing = body->spr.Angles.Yaw;
		bool found = false;
		auto flags = body->spr.cstat;
		body->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
		for (int n = 0; n < 16; ++n)
		{
			auto angle = DAngle::fromDeg(n * 22.5);
			HitInfoBase hit {};
			hitscan(body->getPosWithOffsetZ(), body->sector(), DVector3(angle.ToVector() * 1024, 0), hit, CLIPMASK1);
			double distance = (hit.hitpos.XY() - body->spr.pos.XY()).Length();
			if (hit.hitWall && hit.hitSector && distance > 1 && distance < nearest)
			{
				nearest = distance; facing = angle; found = true;
			}
		}
		body->spr.cstat = flags;
		if (found)
		{
			body->spr.Angles.Yaw = facing;
			player->InitAngles();
			body->backuploc();
		}
		Printf("Superset Duke effect wall setup: %s.\n", found ? "PASS" : "FAIL");
		return;
	}
	if (argv.argc() > 1 && !stricmp(argv[1], "native"))
	{
		auto player = Blood::getPlayer(myconnectindex);
		auto body = player->GetActor();
		double top, bottom;
		Blood::GetActorExtents(body, &top, &bottom);
		double eye = bottom - player->zView;
		bool profile = body->clipdist == 12 && eye > 55 && eye < 58;
		Printf("Superset native Blood player check: %s (radius=%.2f eye=%.4f).\n", profile ? "PASS" : "FAIL", body->clipdist, eye);
		auto thing = Blood::actSpawnThing(body->sector(), body->spr.pos, Blood::kThingTNTBarrel);
		bool valid = thing && (thing->spr.cstat & CSTAT_SPRITE_BLOCK_HITSCAN) && thing->xspr.health == 400;
		if (thing) thing->Destroy();
		Printf("Superset native Blood thing check: %s (TNT barrel health/hitscan blocking).\n", valid ? "PASS" : "FAIL");
		return;
	}
	int count = 0, maxCounter = 0;
	bool valid = true;
	bool clear = argv.argc() > 1 && !stricmp(argv[1], "clear");
	Duke3d::DukeSpriteIterator it;
	while (auto actor = it.Next())
	{
		if (!Supported(actor)) continue;
		if (clear) { actor->Destroy(); continue; }
		++count;
		maxCounter = max(maxCounter, actor->counter);
		valid &= !(actor->spr.cstat & CSTAT_SPRITE_BLOCK_ALL) && Mashup::IsDukeTexture(actor->spr.spritetexture());
	}
	if (clear) { ClearDukeEffects(); return; }
	Printf("Superset Duke effect check: %s (actors=%d counter=%d queue=%d/%d).\n", valid && count > 0 && maxCounter > 0 ? "PASS" : "FAIL",
		count, maxCounter, Duke3d::spriteqloc, Duke3d::spriteqamount);
}

CCMD(superset_duke_resource_check)
{
	Printf("Superset Duke resource check: isolation/colors/aliases/private files=%s.\n", Mashup::CheckDukeResources() ? "PASS" : "FAIL");
}
}
