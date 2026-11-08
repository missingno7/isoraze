#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif
#include "../duke/src/duke3d.h"
#include "../duke/src/dukeactor.h"
#include "../blood/src/blood.h"
#include "duke_projectiles.h"
#include "superset.h"
#include "damage.h"
#include "vm.h"
#include "buildtiles.h"
#include "c_dispatch.h"

namespace Superset
{
using Duke3d::DDukeActor;
namespace
{
constexpr int ProbeTag = 31034;
bool traceProjectile = false, watchDeathCommit = false;
int deathProbeIndex = -1;
}

void FinishDukeProjectileStep()
{
	if (!IsBloodMap()) return;
	// Foreign attacks run after Blood's normal actor phase. Commit their native
	// status transfers/removals before the frame can be serialized or collected.
	Blood::actPostProcess();
	if (deathProbeIndex >= 0)
	{
		bool committed = true;
		Blood::BloodSpriteIterator it;
		while (auto actor = it.Next()) if (actor->GetIndex() == deathProbeIndex)
			committed = actor->xspr.health == 0 && actor->spr.statnum != Blood::kStatDude && !(actor->spr.flags & 32);
		Printf("Superset Duke projectile death commit: %s.\n", committed ? "PASS" : "FAIL");
		deathProbeIndex = -1;
	}
}
bool IsBloodDukeProjectile(Duke3d::DDukeActor* actor)
{
	return IsBloodMap() && actor && actor->supersetForeignProjectile && actor->GetClass() == Duke3d::DukeFirelaserClass;
}

bool IsDukeProjectileEffect(Duke3d::DDukeActor* actor)
{
	return IsBloodMap() && actor && actor->supersetForeignFx && actor->GetClass() == PClass::FindActor("DukeFirelaserTrail");
}

bool BloodDukeProjectileZ(Duke3d::DDukeActor* actor)
{
	if (!IsBloodDukeProjectile(actor)) return false;
	CollisionBase ceiling, floor;
	auto cstat = actor->spr.cstat;
	actor->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	// Keep Blood's depth and room-link ranges. Sky planes still absorb a shot.
	Blood::GetZRangeAtXYZ(actor->spr.pos.plusZ(-1), actor->sector(), &actor->ceilingz, &ceiling,
		&actor->floorz, &floor, .25, CLIPMASK0, Blood::PARALLAXCLIP_CEILING | Blood::PARALLAXCLIP_FLOOR);
	actor->spr.cstat = cstat;
	return true;
}

bool BloodDukeProjectileLink(Duke3d::DDukeActor* actor, double z)
{
	if (!IsBloodDukeProjectile(actor)) return false;
	double previous = actor->spr.pos.Z;
	actor->spr.pos.Z = z;
	if (Blood::CheckLink(static_cast<DCoreActor*>(actor)))
	{
		BloodDukeProjectileZ(actor);
		return true;
	}
	actor->spr.pos.Z = previous;
	return false;
}

int MoveDukeProjectile(Duke3d::DDukeActor* actor, const DVector3& change, unsigned cliptype, CollisionBase& hit)
{
	bool foreign = IsBloodDukeProjectile(actor);
	auto source = foreign ? actor->supersetAttackSource.Get() : nullptr;
	auto cstat = source ? source->spr.cstat : ESpriteFlags(0);
	// Blood's missile environment excludes its shooter from movement queries.
	// The projectile itself stays nonblocking to Blood's typed collision paths.
	if (source) source->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	auto previousPose = actor->opos;
	HitInfoBase swept {};
	if (foreign && change.Z != 0)
	{
		// Duke clips XY at the previous Z, then advances Z. A descending shot
		// can enter a short Blood collider between those checks. Retain the
		// native mover and use the engine's 3D segment for missed sprite contacts.
		auto step = change * .5;
		hitscan(actor->spr.pos, actor->sector(), step, swept, cliptype, step.Length());
	}
	Duke3d::Collision nativeHit {};
	int result = Duke3d::movesprite_ex(actor, change, cliptype, nativeHit);
	hit = nativeHit;
	if (result == kHitNone && swept.hitActor)
	{
		actor->spr.pos = swept.hitpos;
		// A room-link move may have backed up the destination pose. This earlier
		// contact belongs to the original segment, including its interpolation.
		actor->opos = previousPose;
		if (swept.hitSector && swept.hitSector != actor->sector()) ChangeActorSect(actor, swept.hitSector);
		result = hit.setSprite(swept.hitActor);
	}
	if (source) source->spr.cstat = cstat;
	return result;
}

Duke3d::DDukeActor* SpawnDukeProjectile(DCoreActor* source, PClassActor* type,
	const DVector3& position, const DVector3& velocity)
{
	// More projectile families need their own native contact/effect adapters.
	auto bloodSource = dyn_cast<Blood::DBloodActor>(source);
	if (!IsBloodMap() || !source || !source->sector() || (source->ObjectFlags & OF_EuthanizeMe) ||
		type != Duke3d::DukeFirelaserClass || !bloodSource || velocity.isZero()) return nullptr;
	for (double value : {position.X, position.Y, position.Z, velocity.X, velocity.Y, velocity.Z})
		if (!std::isfinite(value)) return nullptr;
	// Shared displacement must fit signed Q18 clipmove encoding.
	if (abs(velocity.X) >= 8192 || abs(velocity.Y) >= 8192) return nullptr;
	auto spawnSector = source->sector();
	updatesectorz(position, &spawnSector);
	if (!spawnSector) return nullptr;
	double scale = bloodSource->IsPlayerActor() ? .109375 : .28125; // Native shootprojectile1 profiles.
	auto actor = Duke3d::CreateActor(spawnSector, position, type, -127, DVector2(scale, scale),
		velocity.XY().Angle(), Units::ToDukeProjectileSpeed(velocity.XY().Length()),
		Units::ToDukeProjectileSpeed(velocity.Z), nullptr, STAT_PROJECTILE);
	if (!actor) return nullptr;
	actor->supersetForeignProjectile = true;
	actor->supersetAttackSource = source;
	GC::WriteBarrier(actor, source);
	actor->spr.extra += Duke3d::krand() & 7; // Native shootprojectile1 damage variation.
	actor->spr.cstat = CSTAT_SPRITE_YCENTER;
	actor->clipdist = 1;
	Duke3d::CallInitialize(actor, nullptr);
	BloodDukeProjectileZ(actor);
	return actor;
}

DEFINE_ACTION_FUNCTION(DCoreActor, SpawnSharedDukeProjectile)
{
	PARAM_SELF_PROLOGUE(DCoreActor);
	PARAM_CLASS(type, DDukeActor);
	PARAM_FLOAT(x); PARAM_FLOAT(y); PARAM_FLOAT(z);
	PARAM_FLOAT(vx); PARAM_FLOAT(vy); PARAM_FLOAT(vz);
	ACTION_RETURN_POINTER(SpawnDukeProjectile(self, static_cast<PClassActor*>(type), {x, y, z}, {vx, vy, vz}));
}

DEFINE_ACTION_FUNCTION(DDukeActor, SharedProjectileMedium)
{
	PARAM_SELF_PROLOGUE(Duke3d::DDukeActor);
	if (!IsBloodDukeProjectile(self)) ACTION_RETURN_INT(self->sector()->lotag);
	if (self->sector()->hasX() && self->sector()->xs().Underwater) ACTION_RETURN_INT(2);
	auto link = dyn_cast<Blood::DBloodActor>(self->sector()->upperLink.Get());
	ACTION_RETURN_INT(link && link->spr.type == Blood::kMarkerUpWater ? 1 : 0);
}

DEFINE_ACTION_FUNCTION(DDukeActor, InheritProjectileEnvironment)
{
	PARAM_SELF_PROLOGUE(Duke3d::DDukeActor);
	PARAM_OBJECT(parent, Duke3d::DDukeActor);
	if (IsBloodDukeProjectile(parent) && self->GetClass() == PClass::FindActor("DukeFirelaserTrail"))
	{
		self->supersetForeignFx = true;
		self->supersetAttackSource = parent->supersetAttackSource;
		GC::WriteBarrier(self, self->supersetAttackSource.Get());
		self->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	}
	return 0;
}

DEFINE_ACTION_FUNCTION(DDukeActor, SharedProjectileContact)
{
	PARAM_SELF_PROLOGUE(Duke3d::DDukeActor);
	PARAM_POINTER(hit, CollisionBase);
	if (!IsBloodDukeProjectile(self) || !hit || hit->type == kHitNone) ACTION_RETURN_BOOL(false);
	auto source = dyn_cast<Blood::DBloodActor>(self->supersetAttackSource.Get());
	if (traceProjectile) Printf("Superset projectile contact trace: kind=%d actor=%d z=%.3f floor=%.3f ceiling=%.3f.\n",
		hit->type, hit->type == kHitSprite && hit->hitActor ? hit->hitActor->spr.type : -1,
		self->spr.pos.Z, self->floorz, self->ceilingz);
	if (hit->type == kHitSprite)
	{
		if (hit->hitActor != source) ApplyDukeHealthHit(hit->hitActor, self, source);
		if (auto target = dyn_cast<Blood::DBloodActor>(hit->hitActor); watchDeathCommit && target &&
			target->spr.intowner == ProbeTag && target->xspr.health == 0)
		{
			deathProbeIndex = target->GetIndex(); watchDeathCommit = false;
		}
	}
	else if (hit->type == kHitWall && hit->hitWall && hit->hitWall->hasX() && hit->hitWall->xw().triggerVector)
	{
		// Blood owns wall switches and events; never invoke Duke wall/tile logic.
		Blood::trTriggerWall(hit->hitWall, Blood::kCmdWallImpact, source);
	}
	self->Destroy();
	ACTION_RETURN_BOOL(true);
}

namespace
{
Blood::DBloodActor* ProbeTarget()
{
	Blood::BloodSpriteIterator it;
	while (auto actor = it.Next()) if (actor->spr.intowner == ProbeTag) return actor;
	return nullptr;
}
void ClearProbe()
{
	if (auto actor = ProbeTarget())
	{
		actor->spr.intowner = -1; // A new fixture can be created before gPost runs.
		actor->spr.flags &= ~32;
		Blood::seqKill(actor); Blood::evKillActor(actor);
		actor->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
		Blood::actPostSprite(actor, Blood::kStatFree);
	}
	Duke3d::DukeSpriteIterator it;
	while (auto actor = it.Next())
		if (IsBloodDukeProjectile(actor) || IsDukeProjectileEffect(actor)) actor->Destroy();
}
DDukeActor* VMSpawn(DCoreActor* source, const DVector3& pos, const DVector3& velocity)
{
	auto function = PClass::FindFunction("CoreActor", "SpawnSharedDukeProjectile");
	if (!function) return nullptr;
	void* result = nullptr;
	VMReturn ret; ret.PointerAt(&result);
	VMValue params[] = { source, Duke3d::DukeFirelaserClass, pos.X, pos.Y, pos.Z, velocity.X, velocity.Y, velocity.Z };
	VMCall(function, params, 8, &ret, 1);
	return static_cast<DDukeActor*>(result);
}

void CheckEnvironment(Blood::DBloodPlayer* player)
{
	auto source = player->GetActor();
	auto ownSector = source->sector();
	auto pos = DVector3(source->spr.pos.XY(), player->zView);
	sectortype* other = nullptr;
	DVector2 otherXY;
	for (auto& candidate : sector)
		if (&candidate != ownSector && !candidate.upperLink && !candidate.lowerLink && candidate.walls.Size() &&
			candidate.floorz - candidate.ceilingz > 64)
		{
			DVector2 center(0, 0);
			for (auto& wal : candidate.walls) center += wal.pos;
			center /= candidate.walls.Size();
			if (inside(center.X, center.Y, &candidate)) { other = &candidate; otherXY = center; break; }
		}
	bool links = false, water = false, slowdown = false, wallEvent = false;
	Blood::XSECTOR testSector {};
	auto previousXSector = ownSector->_xs;
	if (!previousXSector) ownSector->_xs = &testSector;
	auto actor = VMSpawn(source, pos, {0, 0, 2});
	if (actor && other)
	{
		auto upper = Blood::actSpawnSprite(ownSector, pos.plusZ(1), Blood::kStatMarker, false);
		auto xy = otherXY;
		auto lowerPos = DVector3(xy, (getceilzofslopeptr(other, xy) + getflorzofslopeptr(other, xy)) * .5);
		auto lower = Blood::actSpawnSprite(other, lowerPos, Blood::kStatMarker, false);
		if (upper && lower)
		{
			upper->spr.type = Blood::kMarkerUpLink; lower->spr.type = Blood::kMarkerLowLink;
			upper->SetOwner(lower); lower->SetOwner(upper);
			auto previousUpper = ownSector->upperLink.Get(), previousLower = other->lowerLink.Get();
			ownSector->upperLink = upper; other->lowerLink = lower;
			CollisionBase hit;
			BloodDukeProjectileZ(actor);
			MoveDukeProjectile(actor, {0, 0, 4}, CLIPMASK1, hit);
			links = hit.type == kHitNone && actor->sector() == other && (actor->spr.pos - lowerPos.plusZ(1)).Length() < .01;
			MoveDukeProjectile(actor, {0, 0, -4}, CLIPMASK1, hit);
			// The intervening native clipmove quantizes a fractional marker center.
			DVector2 quantizedLower(trunc(lowerPos.X * 16) / 16, trunc(lowerPos.Y * 16) / 16);
			auto expectedReturn = pos + quantizedLower - lowerPos.XY();
			links &= hit.type == kHitNone && actor->sector() == ownSector && (actor->spr.pos - expectedReturn).Length() < .01 &&
				(actor->opos - actor->spr.pos).Length() < .01;
			auto medium = [&]()
			{
				auto function = PClass::FindFunction("DukeActor", "SharedProjectileMedium");
				if (!function) return -1;
				int result = -1; VMReturn ret; ret.IntAt(&result);
				VMValue params[] = {actor}; VMCall(function, params, 1, &ret, 1);
				return result;
			};
			int oldTag = ownSector->lotag, oldWater = ownSector->xs().Underwater;
			ownSector->lotag = 2; ownSector->xs().Underwater = false;
			water = medium() == 0; // Blood sector type 2 is not Duke underwater.
			upper->spr.type = Blood::kMarkerUpWater; water &= medium() == 1;
			upper->spr.type = Blood::kMarkerUpLink;
			ownSector->xs().Underwater = true; water &= medium() == 2;
			ownSector->upperLink = previousUpper; other->lowerLink = previousLower;
			if (links)
			{
				actor->flags2 |= SFLAG2_UNDERWATERSLOWDOWN;
				actor->vel = DVector3(8, 0, 0); actor->spr.Angles.Yaw = source->spr.Angles.Yaw;
				auto before = actor->spr.pos;
				auto vector = actor->spr.Angles.Yaw.ToVector() * 2;
				DVector2 expected(trunc(before.X * 16) / 16 + floor(vector.X * 16) / 16,
					trunc(before.Y * 16) / 16 + floor(vector.Y * 16) / 16);
				Duke3d::CallTick(actor);
				slowdown = (actor->spr.pos.XY() - expected).Length() < .01;
			}
			ownSector->lotag = oldTag; ownSector->xs().Underwater = oldWater;
		}
		if (upper) upper->Destroy(); if (lower) lower->Destroy();
	}
	ownSector->_xs = previousXSector;
	if (actor) actor->Destroy();
	// A temporary XWALL on a plain wall cannot alter an existing wall event queue.
	walltype* probeWall = nullptr;
	DAngle heading;
	DVector3 wallStart;
	double distance = 1e10;
	auto cstat = source->spr.cstat; source->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	for (int n = 0; n < 32; ++n)
	{
		auto angle = DAngle::fromDeg(n * 11.25); HitInfoBase hit {};
		hitscan(pos, ownSector, DVector3(angle.ToVector() * 1024, 0), hit, CLIPMASK1);
		double length = (hit.hitpos.XY() - pos.XY()).Length();
		if (hit.hitWall && !hit.hitWall->hasX() && length < distance)
		{
			probeWall = hit.hitWall; distance = length; heading = angle;
			wallStart = hit.hitpos - DVector3(angle.ToVector() * 8, 0);
		}
	}
	source->spr.cstat = cstat;
	if (probeWall)
	{
		Blood::XWALL fixture {};
		fixture.triggerVector = true;
		auto oldType = probeWall->type;
		probeWall->type = 0; probeWall->_xw = &fixture;
		actor = VMSpawn(source, wallStart, DVector3(heading.ToVector() * Units::FromDukeProjectileSpeed(52.5), 0));
		if (actor)
		{
			for (int n = 0; n < 12 && !(actor->ObjectFlags & OF_EuthanizeMe); ++n) Duke3d::CallTick(actor);
			wallEvent = fixture.state == 1 && fixture.busy == 65536 && (actor->ObjectFlags & OF_EuthanizeMe);
			actor->Destroy();
		}
		probeWall->_xw = nullptr; probeWall->type = oldType;
	}
	Duke3d::DukeSpriteIterator effects;
	while (auto effect = effects.Next()) if (IsDukeProjectileEffect(effect)) effect->Destroy();
	Printf("Superset Duke projectile environment: links=%s, water roles=%s, slowdown=%s, Blood wall events=%s.\n",
		links ? "PASS" : "FAIL", water ? "PASS" : "FAIL", slowdown ? "PASS" : "FAIL", wallEvent ? "PASS" : "FAIL");
}
}

CCMD(superset_spawn_duke_projectile)
{
	if (!IsBloodMap() || gamestate != GS_LEVEL || numplayers != 1) return;
	if (argv.argc() != 2 || stricmp(argv[1], "laser")) { Printf("superset_spawn_duke_projectile laser\n"); return; }
	auto player = Blood::getPlayer(myconnectindex);
	auto source = player->GetActor();
	auto angle = player->getPitchWithView();
	auto direction = source->spr.Angles.Yaw.ToVector() * angle.Cos();
	auto actor = VMSpawn(source, DVector3(source->spr.pos.XY(), player->zView),
		DVector3(direction, angle.Sin()) * Units::FromDukeProjectileSpeed(52.5));
	Printf("Superset Duke laser spawn: %s.\n", actor ? "PASS" : "FAIL");
}

CCMD(superset_duke_projectile_check)
{
	if (!IsBloodMap() || gamestate != GS_LEVEL || numplayers != 1 || argv.argc() != 2) return;
	auto player = Blood::getPlayer(myconnectindex);
	auto source = player->GetActor();
	auto pos = DVector3(source->spr.pos.XY(), player->zView);
	if (!stricmp(argv[1], "environment")) { CheckEnvironment(player); return; }
	if (!stricmp(argv[1], "guard")) { player->godMode = true; return; }
	if (!stricmp(argv[1], "watchdeath")) { watchDeathCommit = true; deathProbeIndex = -1; return; }
	if (!stricmp(argv[1], "trace")) { traceProjectile = true; return; }
	if (!stricmp(argv[1], "notrace")) { traceProjectile = false; return; }
	if (!stricmp(argv[1], "arena"))
	{
		// Combat fixtures need clearance beyond the narrow starting coffin.
		// Select an existing open, dry room; do not alter map geometry or scale.
		double top, bottom; Blood::GetActorExtents(source, &top, &bottom);
		double feet = bottom - source->spr.pos.Z, eye = source->spr.pos.Z - player->zView;
		auto cstat = source->spr.cstat; source->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
		bool found = false;
		for (auto& candidate : sector)
		{
			if (candidate.walls.Size() < 4 || candidate.lotag || candidate.upperLink || candidate.lowerLink ||
				(candidate.hasX() && (candidate.xs().Underwater || candidate.xs().Depth))) continue;
			DVector2 center(0, 0);
			for (auto& wal : candidate.walls) center += wal.pos;
			center /= candidate.walls.Size();
			if (!inside(center.X, center.Y, &candidate)) continue;
			double floor = getflorzofslopeptr(&candidate, center), ceiling = getceilzofslopeptr(&candidate, center);
			if (floor - ceiling < bottom - top + 16) continue;
			DVector3 origin(center, floor - feet - eye);
			bool clear = true;
			for (int n = 0; n < 8; ++n)
			{
				HitInfoBase hit {}; auto direction = DAngle::fromDeg(n * 45).ToVector();
				hitscan(origin, &candidate, DVector3(direction * 1024, 0), hit, CLIPMASK1);
				if (!hit.hitWall || (hit.hitpos.XY() - center).Length() < 128) { clear = false; break; }
			}
			if (!clear) continue;
			source->spr.pos = DVector3(center, floor - feet);
			ChangeActorSect(source, &candidate); source->vel = {};
			source->backuploc(); Blood::playerResetInertia(player);
			found = true; break;
		}
		source->spr.cstat = cstat;
		Printf("Superset Duke projectile arena: %s.\n", found ? "PASS" : "FAIL");
		return;
	}
	if (!stricmp(argv[1], "clear")) { ClearProbe(); return; }
	if (!stricmp(argv[1], "release")) { if (auto target = ProbeTarget()) target->spr.flags &= ~32; return; }
	if (!stricmp(argv[1], "accelerate"))
	{
		Duke3d::DukeSpriteIterator it;
		while (auto actor = it.Next()) if (IsBloodDukeProjectile(actor))
		{
			double speed = DVector2(actor->vel.X, actor->vel.Z).Length();
			if (speed > 0) actor->vel *= 52.5 / speed;
		}
		return;
	}
	if (!stricmp(argv[1], "wall"))
	{
		HitInfoBase hit {}; auto heading = source->spr.Angles.Yaw.ToVector();
		auto cstat = source->spr.cstat; source->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
		hitscan(pos, source->sector(), DVector3(heading * 1024, 0), hit, CLIPMASK1);
		source->spr.cstat = cstat;
		auto actor = hit.hitWall ? VMSpawn(source, hit.hitpos - DVector3(heading * 8, 0),
			DVector3(heading * Units::FromDukeProjectileSpeed(52.5), 0)) : nullptr;
		Printf("Superset Duke projectile wall seed: %s.\n", actor ? "PASS" : "FAIL");
		return;
	}
	if (!stricmp(argv[1], "faceclear"))
	{
		double distance = 0;
		auto cstat = source->spr.cstat;
		source->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
		for (int n = 0; n < 4; ++n)
		{
			auto angle = DAngle::fromDeg(n * 90);
			HitInfoBase hit {};
			hitscan(pos, source->sector(), DVector3(angle.ToVector() * 1024, 0), hit, CLIPMASK1);
			double length = (hit.hitpos.XY() - pos.XY()).Length();
			if (hit.hitWall && length > distance) { distance = length; source->spr.Angles.Yaw = angle; }
		}
		source->spr.cstat = cstat;
		player->InitAngles(); source->backuploc();
		Printf("Superset Duke projectile clear path: %s (%.2f).\n", distance > 80 ? "PASS" : "FAIL", distance);
		return;
	}
	if (!stricmp(argv[1], "target") || !stricmp(argv[1], "thing") || !stricmp(argv[1], "killtarget"))
	{
		ClearProbe();
		bool thing = !stricmp(argv[1], "thing");
		auto target = thing ? Blood::actSpawnThing(source->sector(), source->spr.pos + source->spr.Angles.Yaw.ToVector() * 64,
			Blood::kThingTNTBarrel) : Blood::actSpawnDude(source, Blood::kDudeZombieAxeNormal, 64);
		if (target)
		{
			double top, bottom; Blood::GetActorExtents(target, &top, &bottom);
			target->spr.pos.Z += getflorzofslopeptr(target->sector(), target->spr.pos) - bottom;
			target->spr.intowner = ProbeTag;
			target->xspr.health = !stricmp(argv[1], "killtarget") ? 1 : thing ? 400 : 16000;
			target->spr.flags |= 32; // Hold only until the release phase.
			target->backuploc();
		}
		Printf("Superset Duke projectile target: %s.\n", target ? "PASS" : "FAIL");
		return;
	}
	if (!stricmp(argv[1], "seed") || !stricmp(argv[1], "fast") || !stricmp(argv[1], "floor") || !stricmp(argv[1], "offscreen"))
	{
		bool floor = !stricmp(argv[1], "floor");
		auto target = ProbeTarget();
		auto direction = DVector3(source->spr.Angles.Yaw.ToVector(), 0);
		// Aim from Caleb's actual eye, including down toward a short object on
		// a lower step. Moving the muzzle to the target's height can bury it.
		if (target)
		{
			double top, bottom; Blood::GetActorExtents(target, &top, &bottom);
			double aimZ = target->spr.statnum == Blood::kStatThing ? top + (bottom - top) * .25 : (top + bottom) * .5;
			direction = (DVector3(target->spr.pos.XY(), aimZ) - pos).Unit();
			if (traceProjectile) Printf("Superset projectile aim trace: top=%.3f bottom=%.3f muzzle=%.3f aim=%.3f.\n", top, bottom, pos.Z, aimZ);
		}
		auto velocity = floor ? DVector3(0, 0, 64) : direction *
			(!stricmp(argv[1], "seed") ? .5 : Units::FromDukeProjectileSpeed(52.5));
		auto actor = VMSpawn(source, pos, velocity);
		if (!stricmp(argv[1], "seed"))
		{
			source->spr.Angles.Pitch = -direction.Pitch();
			player->InitAngles(); source->backuploc();
		}
		if (!stricmp(argv[1], "offscreen"))
		{
			source->spr.Angles.Yaw += DAngle180;
			player->InitAngles(); source->backuploc();
		}
		Printf("Superset Duke projectile flight seed: %s.\n", actor ? "PASS" : "FAIL");
		return;
	}
	if (!stricmp(argv[1], "player"))
	{
		auto attacker = ProbeTarget();
		if (!attacker) return;
		player->godMode = false;
		player->armor[0] = 3200;
		auto start = DVector3(attacker->spr.pos.XY(), player->zView);
		auto direction = (pos - start).Unit();
		Printf("Superset Duke projectile enemy source: %s.\n",
			VMSpawn(attacker, start, direction * Units::FromDukeProjectileSpeed(52.5)) ? "PASS" : "FAIL");
		return;
	}
	if (!stricmp(argv[1], "status"))
	{
		int projectiles = 0, effects = 0;
		bool valid = true;
		Duke3d::DukeSpriteIterator it;
		while (auto actor = it.Next())
		{
			if (IsBloodDukeProjectile(actor)) ++projectiles;
			else if (IsDukeProjectileEffect(actor)) ++effects;
			else continue;
			valid &= !(actor->spr.cstat & CSTAT_SPRITE_BLOCK_ALL) && actor->supersetAttackSource != nullptr;
		}
		Printf("Superset Duke projectile status: %s (projectiles=%d trails=%d).\n", valid ? "PASS" : "FAIL", projectiles, effects);
	}
}
}
