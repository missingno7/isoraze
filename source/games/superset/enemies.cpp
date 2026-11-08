#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif
#include "../duke/src/duke3d.h"
#include "../duke/src/dukeactor.h"
#include "../blood/src/blood.h"
#include "enemies.h"
#include "superset.h"
#include "damage.h"
#include "projectiles.h"
#include "status.h"
#include "mashup_player.h"
#include "mashup_resources.h"
#include "c_dispatch.h"

namespace Superset
{
namespace
{
bool watchBurn = false;
bool watchDeath = false;
int deathIndex = -1, deathClock = -1;
double Feet(Blood::DBloodActor* state)
{
	double top, bottom;
	Blood::GetActorExtents(state, &top, &bottom);
	return bottom;
}

void Hide(Blood::DBloodActor* state)
{
	state->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	state->spr.cstat |= CSTAT_SPRITE_INVISIBLE | CSTAT_SPRITE_YCENTER;
}

void Publish(Duke3d::DDukeActor* body, Blood::DBloodActor* state)
{
	if (watchBurn && state->supersetEnemyController && state->spr.type == Blood::kDudeBurningZombieAxe && state->xspr.health > 0)
	{
		Printf("Superset enemy native burn transformation: PASS.\n");
		watchBurn = false;
	}
	if (watchDeath && deathClock < 0 && state->supersetEnemyController && !state->xspr.health && Blood::seqGetStatus(state) >= 0)
	{
		deathIndex = state->GetIndex(); deathClock = PlayClock;
	}
	if (body->sector() != state->sector()) ChangeActorSect(body, state->sector());
	body->spr.pos = DVector3(state->spr.pos.XY(), Feet(state));
	body->spr.Angles = state->spr.Angles;
	if (!IsFrozen(body)) body->spr.setspritetexture(state->spr.spritetexture());
	body->spr.scale = state->spr.scale;
	body->spr.shade = state->spr.shade;
	body->spr.pal = IsFrozen(body) ? 1 : state->spr.pal;
	body->spr.cstat = state->spr.cstat & ~(CSTAT_SPRITE_INVISIBLE | CSTAT_SPRITE_YCENTER | CSTAT_SPRITE_BLOCK_ALL);
	bool alive = state->IsDudeActor() && state->xspr.health > 0 && state->spr.statnum == Blood::kStatDude;
	if (alive) body->spr.cstat |= CSTAT_SPRITE_BLOCK_ALL;
	body->clipdist = state->clipdist;
	body->vel = DVector3(state->vel.XY().Length(), 0, state->vel.Z);
	body->floorz = getflorzofslopeptr(state->sector(), state->spr.pos);
	body->ceilingz = getceilzofslopeptr(state->sector(), state->spr.pos);
	body->spr.extra = body->supersetBloodHealth = alive ? (int(state->xspr.health) + 15) / 16 : -1;
	Hide(state);
}

void ConfigureDifficulty()
{
	Blood::gGameOptions.nDifficulty = BloodDifficulty();
	for (int type : {Blood::kDudeZombieAxeNormal, Blood::kDudeBurningZombieAxe})
	{
		auto& info = Blood::dudeInfo[type - Blood::kDudeBase];
		for (int n = 0; n < Blood::kDamageMax; ++n)
			info.damageVal[n] = MulScale(Blood::DudeDifficulty[BloodDifficulty()], info.startDamage[n], 8);
	}
}

bool ColdAmount(Duke3d::DDukeActor* body, Units::Health amount, int& scaled)
{
	double factor = body->FloatVar(FName("supersetcoldfactor"));
	if (amount < 0 || amount > INT_MAX || !std::isfinite(factor) || factor < 0 || factor > 256 || amount * factor > INT_MAX) return false;
	scaled = int(amount * factor);
	return true;
}

void NativeDamage(Duke3d::DDukeActor* body, int kind, int amount, DCoreActor* source, int factor = -1)
{
	auto state = BloodState(body);
	state->supersetDamageSource = source;
	GC::WriteBarrier(state, source);
	Blood::actDamageSprite(BloodState(source), state, Blood::DAMAGE_TYPE(kind), amount, factor);
	if (!state->xspr.health) Thaw(body);
	Publish(body, state);
}

bool DamageEnemy(Duke3d::DDukeActor* body, Damage damage, DCoreActor* source)
{
	auto state = BloodState(body);
	int kind;
	if (!state || !state->IsDudeActor() || !state->xspr.health || damage.amount < 0 || damage.amount > INT_MAX) return false;
	ConfigureDifficulty();
	if (damage.kind == DamageKind::Cold)
	{
		int amount;
		if (!ColdAmount(body, damage.amount, amount)) return false;
		// No native Cold table exists. Use its independent factor and the ordinary
		// hit reaction, without borrowing Bullet resistance, ignition or Tesla stun.
		if (amount) NativeDamage(body, Blood::kDamageBullet, amount, source, 256);
	}
	else
	{
		if (!NativeBloodDamageKind(damage.kind, kind)) return false;
		NativeDamage(body, kind, int(damage.amount), source);
	}
	return true;
}

void FreezerHit(Duke3d::DDukeActor* body, Units::Health amount, DCoreActor* source)
{
	auto state = BloodState(body);
	int scaled;
	if (!state || !state->IsDudeActor() || !state->xspr.health || IsFrozen(body) || !ColdAmount(body, amount, scaled) || !scaled) return;
	ConfigureDifficulty();
	bool freeze = scaled >= int(state->xspr.health) && body->IntVar(FName("supersetfreezable"));
	// Native Duke thaws at zero but still lives; Blood requires positive health
	// to run its AI. Retain the minimum Q4 quantum, not a healed health point.
	if (freeze) scaled = max(0, int(state->xspr.health) - 1);
	if (scaled) NativeDamage(body, Blood::kDamageBullet, scaled, source, 256);
	if (freeze) Freeze(body, source, int(Units::FromSimulationSteps(138))); // Stock THAWTIME.
	Publish(body, state);
}

class DukeEnemyWorld final : public Blood::MovementWorldServices
{
public:
	bool NoClip() const override { return false; }
	bool IsUnderwater(sectortype* sector) const override { return sector->lotag == ST_2_UNDERWATER; }
	int SurfaceType(CollisionBase& hit) const override
	{
		FTextureID texture = FNullTextureID();
		if (hit.type == kHitSector) texture = hit.hitSector->floortexture;
		else if (hit.type == kHitWall) texture = hit.hitWall->walltexture;
		else if (hit.type == kHitSprite && hit.hitActor) texture = hit.hitActor->spr.spritetexture();
		if (Mashup::IsBloodTexture(texture)) return Blood::tileGetSurfType(hit);
		switch (tilesurface(texture))
		{
		case TSURF_METALDUCTS: return Blood::kSurfMetal;
		case TSURF_MAGMA: case TSURF_PURPLELAVA: return Blood::kSurfLava;
		default: return 0;
		}
	}
	int CheckRoomLink(Blood::DBloodActor*) override { return 0; }
	void MapCollision(CollisionBase& hit) const override { MapBloodCollision(hit); }
	DCoreActor* CollisionOwner(Blood::DBloodActor* state) const override { return state->supersetWorldActor; }
};
}

Blood::DBloodActor* BloodState(DCoreActor* actor)
{
	if (!IsActive() || !actor) return nullptr;
	if (auto native = dyn_cast<Blood::DBloodActor>(actor)) return native;
	if (auto duke = dyn_cast<Duke3d::DDukeActor>(actor))
	{
		if (duke->supersetBloodState) return dyn_cast<Blood::DBloodActor>(duke->supersetBloodState.Get());
		if (duke->isPlayer())
			if (auto player = static_cast<Blood::DBloodPlayer*>(Mashup::GetBloodPlayer(duke->PlayerIndex()))) return player->GetActor();
	}
	return nullptr;
}

bool HasBloodEnemy(DCoreActor* actor)
{
	auto duke = dyn_cast<Duke3d::DDukeActor>(actor);
	if (!IsActive() || !duke) return false;
	auto type = PClass::FindActor("SupersetBloodEnemy");
	return type && duke->IsKindOf(type);
}

bool ApplyBloodEnemyDamage(DCoreActor* actor, Damage damage, DCoreActor* source)
{
	if (!HasBloodEnemy(actor)) return false;
	return DamageEnemy(static_cast<Duke3d::DDukeActor*>(actor), damage, source);
}

void MapBloodCollision(CollisionBase& hit)
{
	if (hit.type != kHitSprite) return;
	if (auto state = BloodState(hit.hitActor)) hit.setSprite(state);
	else hit.setNone(); // Engine geometry still blocks; no foreign Blood trigger cast.
}

void ApplyBloodVectorImpulse(DCoreActor* actor, int type, const DVector3& direction)
{
	auto state = BloodState(actor);
	if (!state || !state->IsDudeActor() || type < 0 || type >= Blood::kVectorMax) return;
	int mass = Blood::getDudeInfo(state->spr.type)->mass;
	if (mass <= 0) return;
	auto impulse = direction * (double(Blood::gVectorData[type].impulse) / (mass * 1024));
	if (state->IsPlayerActor())
	{
		if (Duke3d::ud.god) return;
		ApplyCalebImpulse(Duke3d::getPlayer(state->spr.type - Blood::kDudePlayer1), impulse.XY());
	}
	else state->vel += impulse;
}

void BloodEnemyHit(Duke3d::DDukeActor* body, Duke3d::DDukeActor* hitter)
{
	if (!HasBloodEnemy(body) || !hitter || (!(hitter->flags3 & SFLAG3_CANHURTSHOOTER) && hitter->GetOwner() == body)) return;
	if (hitter->flags2 & SFLAG2_FREEZEDAMAGE)
	{
		if (Duke3d::gs.freezerhurtowner || hitter->GetOwner() != body)
		{
			if (auto state = BloodState(body); state && state->IsDudeActor() && state->xspr.health && !IsFrozen(body))
				state->vel += hitter->spr.Angles.Yaw.ToVector() * max(0, int(hitter->spr.extra)) * .25;
			FreezerHit(body, Units::FromDukeHealth(max(0, int(hitter->spr.extra))), hitter->GetOwner());
		}
		return;
	}
	DamageKind kind;
	if (hitter->supersetHitDamage >= 0) kind = DamageKind(hitter->supersetHitKind);
	else if (!DukeDamageKind(static_cast<PClassActor*>(hitter->GetClass()), kind)) return;
	if ((kind == DamageKind::Physical || kind == DamageKind::Blast) && Shatter(body, hitter->GetOwner())) return;
	auto amount = hitter->supersetHitDamage >= 0 ? hitter->supersetHitDamage : Units::FromDukeHealth(max(0, int(hitter->spr.extra)));
	DamageEnemy(body, {amount, kind}, hitter->GetOwner());
	if (hitter->supersetHitDamage < 0 && !hitter->supersetDamageOnly)
		if (auto state = BloodState(body)) state->vel += hitter->spr.Angles.Yaw.ToVector() * Units::HealthPoints(amount) * .25;
}

void TickBloodEnemies()
{
	if (!IsActive()) return;
	ConfigureDifficulty();
	Duke3d::DukeSpriteIterator bodies;
	while (auto body = bodies.Next())
	{
		if (!HasBloodEnemy(body)) continue;
		auto state = BloodState(body);
		if (!state) { body->Destroy(); continue; }
		// Bit 32 also marks a deferred status transfer (e.g. dude -> corpse),
		// not just deletion. Preserve the controller and its native death SEQ.
		if (state->spr.flags & 32) continue;
		if (watchDeath && state->GetIndex() == deathIndex && PlayClock > deathClock && !state->xspr.health && Blood::seqGetStatus(state) >= 0)
		{
			Printf("Superset enemy native death sequence: PASS.\n"); watchDeath = false;
		}
		auto delta = body->spr.pos - DVector3(state->spr.pos.XY(), Feet(state));
		state->spr.pos += delta;
		if (state->sector() != body->sector()) ChangeActorSect(state, body->sector());
		state->spr.Angles.Yaw = body->spr.Angles.Yaw;
		int healthDelta = body->spr.extra - body->supersetBloodHealth;
		if (healthDelta) state->xspr.health = clamp(int(state->xspr.health) + healthDelta * 16, 0, 65535);
		if (body->hitextra >= 0)
		{
			DamageKind kind;
			int damage = body->hitextra;
			body->hitextra = -1;
			if (DukeDamageKind(body->attackertype, kind))
			{
				if ((kind == DamageKind::Physical || kind == DamageKind::Blast) && Shatter(body, body->GetHitOwner())) continue;
				DamageEnemy(body, {Units::FromDukeHealth(damage), kind}, body->GetHitOwner());
			}
		}
		Hide(state);
		state->interpolated = false;
	}
	DukeEnemyWorld world;
	Blood::BloodStatIterator orphans(Blood::kStatDude);
	while (auto state = orphans.Next())
		if (state->supersetEnemyController && !state->supersetWorldActor && !(state->spr.flags & 32))
		{
			Blood::seqKill(state); Blood::evKillActor(state); state->Destroy();
		}
	Blood::actProcessDudes(&world);
	Blood::gFrameCount = int(PlayClock / Units::ClocksPerStep);
	Blood::aiProcessDudes();
}

void PublishBloodEnemies()
{
	if (!IsActive()) return;
	Blood::BloodSpriteIterator states;
	while (auto state = states.Next())
	{
		if (!state->supersetEnemyController) continue;
		auto body = dyn_cast<Duke3d::DDukeActor>(state->supersetWorldActor.Get());
		if (!body || body->supersetBloodState != state)
		{
			Blood::seqKill(state); Blood::evKillActor(state); state->Destroy();
			continue;
		}
		body->backuploc();
		Publish(body, state);
	}
}

void PrepareBloodWorldObjects()
{
	if (!IsActive()) return;
	Blood::BloodSpriteIterator it;
	while (auto state = it.Next())
	{
		if (state->supersetWorldActor) Hide(state);
		else if (state->spr.statnum == Blood::kStatThing || state->spr.statnum == Blood::kStatProjectile)
			state->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	}
}

bool AnimateBloodEnemy(tspritetype* sprite, Duke3d::DDukeActor* body, const DVector2& view)
{
	if (!HasBloodEnemy(body)) return false;
	auto state = BloodState(body);
	if (!state) return true;
	if (IsFrozen(body)) sprite->pal = 10; // Blood cold.plu, not Duke's palette 1.
	else sprite->setspritetexture(state->spr.spritetexture());
	Blood::viewApplySpriteRotation(sprite, body, view);
	return true;
}

CCMD(superset_spawn_enemy)
{
	if (!IsActive() || gamestate != GS_LEVEL) return;
	if (argv.argc() != 2 || stricmp(argv[1], "zombie")) { Printf("Blood enemies: zombie\n"); return; }
	auto player = Duke3d::getPlayer(myconnectindex);
	auto source = BloodState(player->GetActor());
	if (!source) return;
	THitInfo<DCoreActor> hit {};
	auto flags = player->GetActor()->spr.cstat;
	player->GetActor()->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	hitscan(player->GetActor()->getPosWithOffsetZ(), player->GetActor()->sector(), DVector3(source->spr.Angles.Yaw.ToVector() * 1024., 0), hit, CLIPMASK1);
	player->GetActor()->spr.cstat = flags;
	if (!hit.hitSector || (hit.hitpos.XY() - source->spr.pos.XY()).Length() < 128.) { Printf("Not enough clear space ahead.\n"); return; }
	ConfigureDifficulty();
	auto state = Blood::actSpawnDude(source, Blood::kDudeZombieAxeNormal, 96.);
	if (!state) return;
	state->spr.pos.Z += getflorzofslopeptr(state->sector(), state->spr.pos) - Feet(state);
	state->spr.Angles.Yaw += DAngle180;
	auto body = Duke3d::CreateActor(state->sector(), DVector3(state->spr.pos.XY(), Feet(state)), PClass::FindActor("SupersetBloodEnemy"),
		0, state->spr.scale, state->spr.Angles.Yaw, 0, 0, nullptr, STAT_ACTOR);
	if (!body) { Blood::seqKill(state); state->Destroy(); return; }
	body->supersetBloodState = state;
	state->supersetWorldActor = body;
	state->supersetEnemyController = true;
	GC::WriteBarrier(body, state); GC::WriteBarrier(state, body);
	Publish(body, state);
	body->backuploc(); state->backuploc();
	Level.addKillCount();
	Printf("Spawned native Blood axe zombie.\n");
}

CCMD(superset_enemy_check)
{
	if (!IsActive() || gamestate != GS_LEVEL) return;
	if (argv.argc() == 2 && !stricmp(argv[1], "guard")) { Duke3d::ud.god = 1; return; }
	if (argv.argc() == 2 && !stricmp(argv[1], "watchburn")) { watchBurn = true; return; }
	if (argv.argc() == 2 && !stricmp(argv[1], "watchdeath")) { watchDeath = true; deathIndex = deathClock = -1; return; }
	if (argv.argc() == 2 && (!stricmp(argv[1], "remove") || !stricmp(argv[1], "expire")))
	{
		Duke3d::DukeSpriteIterator bodies;
		while (auto body = bodies.Next()) if (HasBloodEnemy(body))
		{
			if (!stricmp(argv[1], "remove")) body->Destroy();
			else if (auto state = BloodState(body)) Blood::evPostActor(state, 0, Blood::kCallbackRemove);
		}
		return;
	}
	Duke3d::DukeSpriteIterator it;
	while (auto body = it.Next()) if (auto state = BloodState(body); body->supersetBloodState && state)
		Printf("Superset enemy: type=%d health=%d AI=%d target=%d collider=%d/%d sequence=%d playerhealth=%d.\n", state->spr.type, state->xspr.health,
			state->xspr.aiState ? state->xspr.aiState->stateType : -1, state->GetTarget() != nullptr,
			!!(body->spr.cstat & CSTAT_SPRITE_BLOCK_ALL), !!(state->spr.cstat & CSTAT_SPRITE_BLOCK_ALL), Blood::seqGetID(state),
			Duke3d::getPlayer(myconnectindex)->GetActor()->spr.extra);
}
}
