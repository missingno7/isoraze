// Retain Blood's controller/physics algorithms and expose their map-dependent
// operations. Only the world pawn blocks collision; the Blood state actor is
// an implementation detail of the native controller, never a second collider.
#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif
#include "../duke/src/duke3d.h"
#include "../duke/src/dukeactor.h"
#include "../blood/src/blood.h"
#include "superset.h"
#include "mashup_player.h"
#include "mashup_resources.h"
#include "units.h"
#include "damage.h"
#include "projectiles.h"
#include "enemies.h"
#include "status.h"
#include "c_dispatch.h"

namespace Blood { void SerializeSequences(FSerializer& arc); void SerializeEvents(FSerializer& arc); }

namespace Superset
{
namespace
{
int sequenceClock = -1;

void AdvanceBloodSequences()
{
	if (sequenceClock == PlayClock) return;
	Blood::evProcess(PlayClock);
	Blood::seqProcess(Units::ClocksPerStep);
	PrepareBloodWorldObjects();
	sequenceClock = PlayClock;
}

Blood::DBloodActor* FindDiagnosticFx()
{
	Blood::BloodStatIterator it(Blood::kStatFX);
	while (auto actor = it.Next()) if (actor->spr.intowner == 31018) return actor;
	return nullptr;
}
struct MovementProbe
{
	bool active = false, moved = false, jumped = false, crouched = false;
	bool oneCollider = true, clock = true;
	bool jumpInput = false, crouchInput = false;
	int minHealth = 65535;
	int ticks = 0, lastClock = 0;
	DVector3 start;
	double eye = 0;
} probe;

auto NativePlayer(DCorePlayer* player)
{
	return static_cast<Blood::DBloodPlayer*>(Mashup::GetBloodPlayer(player->pnum));
}

void HideStateActor(Blood::DBloodActor* actor)
{
	actor->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	actor->spr.cstat |= CSTAT_SPRITE_INVISIBLE | CSTAT_SPRITE_YCENTER;
}

double FeetZ(Blood::DBloodActor* actor)
{
	double top, bottom;
	Blood::GetActorExtents(actor, &top, &bottom);
	return bottom;
}

void Publish(Duke3d::DDukePlayer* duke, Blood::DBloodPlayer* blood)
{
	auto body = duke->GetActor();
	auto state = blood->GetActor();
	if (body->sector() != state->sector()) ChangeActorSect(body, state->sector());
	body->spr.pos = DVector3(state->spr.pos.XY(), FeetZ(state));
	body->spr.Angles = state->spr.Angles;
	body->spr.setspritetexture(state->spr.spritetexture());
	body->spr.scale = state->spr.scale;
	body->spr.cstat &= ~CSTAT_SPRITE_YCENTER;
	if (state->xspr.health == 0) body->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	body->clipdist = state->clipdist;
	body->viewzoffset = blood->zView - body->spr.pos.Z;
	body->vel = state->vel;
	body->vel.X = state->vel.XY().Length(); // Duke uses this field as scalar speed.
	duke->vel = state->vel;
	duke->ViewAngles = blood->ViewAngles;
	duke->RollVel = blood->RollVel;
	duke->opyoff = blood->obobHeight;
	duke->pyoff = blood->bobHeight;
	duke->setCursector(state->sector());
	duke->psectlotag = state->sector()->lotag;
	duke->on_ground = state->xspr.height == 0;
	duke->truefz = getflorzofslopeptr(state->sector(), state->spr.pos);
	duke->truecz = getceilzofslopeptr(state->sector(), state->spr.pos);
	body->floorz = FeetZ(state) + state->xspr.height;
	body->ceilingz = duke->truecz;
	// Temporary legacy-health facade. Preserve fractional native Blood damage;
	// native Duke hits are imported as deltas, never by replacing Blood health.
	body->spr.extra = max(0, (int(state->xspr.health) + 15) / 16);
	duke->bloodPublishedHealth = body->spr.extra;
	duke->last_extra = body->spr.extra;
	HideStateActor(state);
}

void ImportWorldChanges(Duke3d::DDukePlayer* duke, Blood::DBloodPlayer* blood)
{
	auto body = duke->GetActor();
	auto state = blood->GetActor();
	// Transports and moving geometry retain the map's native implementation.
	// Convert displacement back to Blood's center-based pose and camera inertia.
	auto delta = body->spr.pos - DVector3(state->spr.pos.XY(), FeetZ(state));
	if (!delta.isZero() || body->sector() != state->sector())
	{
		auto oldpos = state->spr.pos;
		state->spr.pos += delta;
		if (state->sector() != body->sector()) ChangeActorSect(state, body->sector());
		Blood::playerCorrectInertia(blood, oldpos);
	}
	state->spr.Angles = body->spr.Angles;
	if (body->hitextra >= 0) Duke3d::fi.ifhitbyweapon(body);
	int healthDelta = body->spr.extra - duke->bloodPublishedHealth;
	state->xspr.health = clamp(int(state->xspr.health) + int(Units::FromDukeHealth(healthDelta)), 0, 65535);
	duke->bloodPublishedHealth = body->spr.extra;
}

class DukePlayerWorld final : public Blood::PlayerWorldServices
{
public:
	bool useRequested = false;
	bool NoClip() const override { return Duke3d::ud.clipping != 0; }
	bool IsUnderwater(sectortype* sector) const override { return sector->lotag == ST_2_UNDERWATER; }
	int SurfaceType(CollisionBase& hit) const override
	{
		FTextureID texture = FNullTextureID();
		if (hit.type == kHitSector) texture = hit.hitSector->floortexture;
		else if (hit.type == kHitWall) texture = hit.hitWall->walltexture;
		else if (hit.type == kHitSprite) texture = hit.hitActor->spr.spritetexture();
		if (Mashup::IsBloodTexture(texture)) return Blood::tileGetSurfType(hit);
		// The two games' numeric surface IDs have different meanings. Translate
		// known materials explicitly; do not turn e.g. a Duke sky into Blood water.
		switch (tilesurface(texture))
		{
		case TSURF_METALDUCTS: return Blood::kSurfMetal;
		case TSURF_MAGMA:
		case TSURF_PURPLELAVA: return Blood::kSurfLava;
		default: return 0;
		}
	}
	int CheckRoomLink(Blood::DBloodActor*) override { return 0; } // Duke transports run in the world phase.
	void MapCollision(CollisionBase& hit) const override { MapBloodCollision(hit); }
	void UseAction(Blood::DBloodPlayer*) override { useRequested = true; }
	void CollectPickups(Blood::DBloodPlayer* player) override
	{
		Blood::CheckPickUp(player, this);
	}
	bool CanPickUp(Blood::DBloodActor* item) const override { return BloodPickupSupported(item->spr.type); }
	void PickupTaken(Blood::DBloodPlayer*, Blood::DBloodActor* item) override
	{
		if (!Blood::actCheckRespawn(item)) Blood::actPostSprite(item, Blood::kStatFree);
	}
	void PlayerDied(Blood::DBloodPlayer* player) override
	{
		// Duke maps have no Blood death trigger network. The shared controller
		// retains Blood's death sequence; only its world pawn loses collision.
		Duke3d::getPlayer(player->pnum)->GetActor()->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	}
	void RestartPlayer(Blood::DBloodPlayer*) override { gameaction = ga_autoloadgame; }
};
}

bool ApplyCalebDamage(DCorePlayer* player, Damage damage, DCoreActor* source)
{
	if (!HasBloodMovement(player) || damage.amount < 0 || damage.amount > INT_MAX) return false;
	int nativeKind;
	if (!NativeBloodDamageKind(damage.kind, nativeKind)) return false;
	auto duke = static_cast<Duke3d::DDukePlayer*>(player);
	auto blood = NativePlayer(player);
	ImportWorldChanges(duke, blood);
	auto state = blood->GetActor();
	if ((state->spr.flags & 32) || state->xspr.locked || state->xspr.health == 0) return true;
	// actDamageDude normally applies this before playerDamageSprite. The state
	// actor deliberately is not in Blood's map-owned dude list.
	int factor = blood->pDudeInfo->damageVal[nativeKind];
	if (!factor) return true;
	int amount = MulScale(int(damage.amount), factor, 8);
	DukePlayerWorld world;
	Blood::playerDamageSprite(dyn_cast<Blood::DBloodActor>(source), blood, Blood::DAMAGE_TYPE(nativeKind), amount, &world);
	if (state->xspr.health == 0)
		duke->wackedbyactor = dyn_cast<Duke3d::DDukeActor>(source);
	Publish(duke, blood);
	return true;
}

void ApplyCalebImpulse(DCorePlayer* player, const DVector2& impulse)
{
	if (!HasBloodMovement(player)) return;
	auto blood = NativePlayer(player);
	blood->GetActor()->vel += impulse;
	Publish(static_cast<Duke3d::DDukePlayer*>(player), blood);
}

void CollectBloodPickups(DCorePlayer* player)
{
	if (!IsActive() || !Mashup::IsCombatPlayer(player)) return;
	auto blood = NativePlayer(player);
	if (!blood || blood->GetActor()->xspr.health == 0) return;
	DukePlayerWorld world;
	world.CollectPickups(blood);
	if (!HasBloodMovement(player))
	{
		// Native Duke movement still owns its integer health facade in this
		// profile. Blood pickup amounts/caps remain native Blood values.
		auto duke = static_cast<Duke3d::DDukePlayer*>(player);
		duke->GetActor()->spr.extra = blood->GetActor()->xspr.health >> 4;
	}
}

bool HasBloodMovement(DCorePlayer* player)
{
	auto duke = dyn_cast<Duke3d::DDukePlayer>(player);
	return CalebSelected() && duke && duke->bloodMovement && duke->bloodCombat != nullptr;
}

bool PrepareBloodMovement(DCorePlayer* player)
{
	if (!CalebSelected()) return false;
	auto duke = static_cast<Duke3d::DDukePlayer*>(player);
	auto blood = NativePlayer(player);
	if (!blood) return false;
	auto body = duke->GetActor();
	auto state = blood->GetActor();
	blood->pDudeInfo = &Blood::dudeInfo[Blood::kDudePlayer1 + player->pnum - Blood::kDudeBase];
	if (!duke->bloodMovement)
	{
		double feet = body->spr.pos.Z;
		// The Duke loader starts the first frame at eye height; its native input
		// normally performs this conversion. That input path is replaced here.
		if (PlayClock == 0 && body->viewzoffset == 0) feet += Duke3d::gs.playerheight;
		Blood::playerSetRace(blood, Blood::kModeHuman);
		state->spr.flags = 15;
		state->vel.Zero();
		state->spr.Angles = body->spr.Angles;
		Blood::seqSpawn(blood->pDudeInfo->seqStartID, state, -1);
		HideStateActor(state);
		state->spr.pos = DVector3(body->spr.pos.XY(), state->spr.pos.Z + feet - FeetZ(state));
		state->xspr.health = max(0, int(body->spr.extra)) << 4;
		state->xspr.height = 0;
		blood->InitAngles();
		Blood::playerResetInertia(blood);
		duke->bloodMovement = true;
		Publish(duke, blood);
		body->backuploc();
		duke->InitAngles();
		Printf("Superset player: Caleb; native Blood movement, radius %.2f, standing eye %.4f above feet.\n",
			state->clipdist, FeetZ(state) - blood->zView);
	}
	else ImportWorldChanges(duke, blood);
	bool underwater = body->sector()->lotag == ST_2_UNDERWATER;
	state->xspr.medium = underwater ? Blood::kMediumWater : Blood::kMediumNormal;
	blood->isUnderwater = underwater;
	if (underwater) blood->posture = Blood::kPostureSwim;
	else if (blood->posture == Blood::kPostureSwim) blood->posture = Blood::kPostureStand;
	return true;
}

bool TickBloodMovement(DCorePlayer* player)
{
	if (!HasBloodMovement(player)) return false;
	auto duke = static_cast<Duke3d::DDukePlayer*>(player);
	auto blood = NativePlayer(player);
	auto state = blood->GetActor();
	auto body = duke->GetActor();
	Blood::viewBackupView(blood);
	++duke->player_par;
	Duke3d::ticktransporterhold_d(duke);
	duke->on_warping_sector = 0;
	if ((duke->fist_incs && Duke3d::endoflevel(duke)) ||
		(duke->timebeforeexit > 1 && body->spr.extra > 0 && Duke3d::timedexit(duke))) return true;
	if (duke->newOwner || Duke3d::ud.cameraactor)
	{
		blood->cmd.ucmd.vel.Zero();
		blood->cmd.ucmd.ang.Zero();
	}
	DukePlayerWorld world;
	auto flags = body->spr.cstat;
	body->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	Blood::playerProcess(blood, &world);
	// Native enemy SEQ attacks must see the single physical player collider.
	body->spr.cstat = flags;
	AdvanceBloodSequences();
	body->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	HideStateActor(state);
	Blood::actProcessDudeMovement(state, &world);
	body->spr.cstat = flags;
	// Duke's checksectors consumes the queued use action once, after publishing
	// the new pose. All sector effects remain on the Duke side of this boundary.
	if (world.useRequested) duke->cmd.ucmd.actions |= SB_OPEN;
	else duke->cmd.ucmd.actions &= ~SB_OPEN;
	Publish(duke, blood);
	Duke3d::tickaccessswitch_d(duke);
	if (probe.active)
	{
		probe.jumpInput |= !!(blood->cmd.ucmd.actions & SB_JUMP);
		probe.crouchInput |= !!(blood->cmd.ucmd.actions & SB_CROUCH);
		probe.minHealth = min(probe.minHealth, int(state->xspr.health));
		if (probe.ticks) probe.clock &= PlayClock - probe.lastClock == Units::ClocksPerStep;
		probe.lastClock = PlayClock;
		++probe.ticks;
		probe.moved |= (state->spr.pos.XY() - probe.start.XY()).Length() > 0.25;
		probe.jumped |= probe.start.Z - FeetZ(state) > 4;
		probe.crouched |= blood->posture == Blood::kPostureCrouch;
		probe.oneCollider &= !(state->spr.cstat & CSTAT_SPRITE_BLOCK_ALL) &&
			!!(body->spr.cstat & CSTAT_SPRITE_BLOCK) && body->clipdist == state->clipdist;
	}
	return true;
}

void FinishBloodMovement(DCorePlayer* player)
{
	if (!HasBloodMovement(player)) return;
	auto duke = static_cast<Duke3d::DDukePlayer*>(player);
	auto blood = NativePlayer(player);
	ImportWorldChanges(duke, blood);
	Publish(duke, blood);
}

void TickSharedObjects()
{
	if (!IsActive()) return;
	Mashup::RefreshBloodPlayerStates();
	AdvanceBloodSequences();
	TickBloodEnemies();
	TickBloodProjectiles();
	TickStatusEffects();
	TickFreezeEffects();
	Blood::gFX.fxProcess();
	Blood::actPostProcess();
	PublishBloodEnemies();
}

void ClearBloodControllerState()
{
	Blood::seqKillAll();
	Blood::evClear();
	Blood::actClearPost();
	sequenceClock = -1;
}

void SerializeBloodControllerState(FSerializer& arc)
{
	if (arc.BeginObject("superset.blood_controller"))
	{
		SerializeProjectileOptions(arc);
		Blood::SerializeSequences(arc);
		Blood::SerializeEvents(arc);
		arc.EndObject();
	}
	if (arc.isReading())
		for (auto player : PlayerArray)
			if (auto duke = dyn_cast<Duke3d::DDukePlayer>(player); duke && duke->bloodMovement && duke->bloodCombat)
			{
				auto blood = NativePlayer(player);
				blood->pDudeInfo = &Blood::dudeInfo[blood->GetActor()->spr.type - Blood::kDudeBase];
				Blood::playerSetRace(blood, blood->lifeMode);
				HideStateActor(blood->GetActor());
			}
}

CCMD(superset_movement_check)
{
	if (!CalebSelected() || !HasBloodMovement(PlayerArray[myconnectindex]))
	{
		Printf("Superset movement check: requires -superset-player caleb in a level.\n");
		return;
	}
	auto player = NativePlayer(PlayerArray[myconnectindex]);
	auto state = player->GetActor();
	if (argv.argc() > 1 && !stricmp(argv[1], "start"))
	{
		probe = {};
		probe.active = true;
		probe.start = DVector3(state->spr.pos.XY(), FeetZ(state));
		probe.eye = FeetZ(state) - player->zView;
		return;
	}
	bool nativeProfile = probe.eye > 55. && probe.eye < 58. && state->clipdist == 12.;
	bool inputs = probe.ticks > 0 && probe.moved && probe.jumped && probe.crouched;
	if (!inputs || !nativeProfile)
		Printf("Caleb movement detail: ticks=%d moved=%d jumped=%d crouched=%d initial eye=%.4f radius=%.2f input=%d/%d minhealth=%d posture=%d height=%.2f.\n",
			probe.ticks, probe.moved, probe.jumped, probe.crouched, probe.eye, state->clipdist, probe.jumpInput, probe.crouchInput,
			probe.minHealth, player->posture, state->xspr.height);
	Printf("Superset movement check: native profile=%s, move/jump/crouch=%s, one collider=%s, clock=%s.\n",
		nativeProfile ? "PASS" : "FAIL", inputs ? "PASS" : "FAIL", probe.oneCollider ? "PASS" : "FAIL", probe.clock ? "PASS" : "FAIL");
	probe.active = false;
}

CCMD(superset_fx_check)
{
	if (!IsActive() || gamestate != GS_LEVEL || argv.argc() < 2) return;
	auto fx = FindDiagnosticFx();
	if (!stricmp(argv[1], "seed"))
	{
		if (fx) Blood::gFX.destroy(fx);
		auto body = PlayerArray[myconnectindex]->actor;
		fx = Blood::gFX.fxSpawnActor(Blood::FX_43, body->sector(), body->getPosWithOffsetZ());
		if (fx)
		{
			fx->spr.intowner = 31018;
			fx->basePoint = fx->spr.pos;
			fx->vel.X = .03125;
			Blood::evPostActor(fx, 480, Blood::kCallbackRemove);
		}
		return;
	}
	if (!stricmp(argv[1], "clear")) { if (fx) Blood::gFX.destroy(fx); return; }
	bool pass = !stricmp(argv[1], "expired") ? !fx : fx && (fx->spr.pos.XY() - fx->basePoint.XY()).Length() > .1;
	Printf("Superset FX %s check: %s.\n", argv[1], pass ? "PASS" : "FAIL");
}

CCMD(superset_incoming_check)
{
	auto duke = dyn_cast<Duke3d::DDukePlayer>(PlayerArray[myconnectindex]);
	if (gamestate != GS_LEVEL || !duke || !HasBloodMovement(duke)) return;
	auto blood = NativePlayer(duke);
	auto state = blood->GetActor();
	auto body = duke->GetActor();
	if (argv.argc() > 1)
	{
		if (!stricmp(argv[1], "seed"))
		{
			blood->armor[0] = 1600; blood->armor[1] = 800; blood->armor[2] = 400;
			return;
		}
		if (!stricmp(argv[1], "clear"))
		{
			memset(blood->armor, 0, sizeof(blood->armor));
			return;
		}
		if (!stricmp(argv[1], "saved") || !stricmp(argv[1], "restored"))
		{
			bool pass = blood->armor[0] == 1600 && blood->armor[1] == 800 && blood->armor[2] == 400 &&
				state->xspr.health > 0 && body->spr.extra > 0 && !!(body->spr.cstat & CSTAT_SPRITE_BLOCK);
			Printf("Superset incoming %s check: %s.\n", argv[1], pass ? "PASS" : "FAIL");
			memset(blood->armor, 0, sizeof(blood->armor));
			return;
		}
		if (!stricmp(argv[1], "dead"))
		{
			bool pass = state->xspr.health == 0 && body->spr.extra == 0 && blood->deathTime > 0 &&
				!(body->spr.cstat & CSTAT_SPRITE_BLOCK_ALL) && state->spr.type == Blood::kDudePlayer1 + duke->pnum && Blood::seqGetStatus(state) < 0;
			Printf("Superset incoming dead check: %s.\n", pass ? "PASS" : "FAIL");
			return;
		}
	}
	auto source = Duke3d::CreateActor(body->sector(), body->spr.pos, PClass::FindActor("DukeLizTrooper"),
		0, DVector2(1, 1), nullAngle, 0, 0, nullptr, STAT_MISC);
	auto shot = Duke3d::CreateActor(body->sector(), body->spr.pos, Duke3d::DukeShotSparkClass,
		0, DVector2(1, 1), nullAngle, 0, 0, source, STAT_MISC);
	if (!source || !shot)
	{
		if (source) source->Destroy();
		if (shot) shot->Destroy();
		Printf("Superset incoming check: FAIL (test actors).\n");
		return;
	}
	ChangeActorStat(source, MAXSTATUS - 2);
	ChangeActorStat(shot, MAXSTATUS - 2);
	bool blastDeath = argv.argc() > 1 && !stricmp(argv[1], "die_blast");
	if (argv.argc() > 1 && (!stricmp(argv[1], "die") || blastDeath))
	{
		state->xspr.health = 5 * 16;
		memset(blood->armor, 0, sizeof(blood->armor));
		memset(blood->damageControl, 0, sizeof(blood->damageControl));
		blood->godMode = false;
		Duke3d::ud.god = 0;
		Publish(duke, blood);
		if (blastDeath)
		{
			body->attackertype = Duke3d::DukeRadiusExplosionClass;
			body->hitextra = 10;
			body->SetHitOwner(source);
			Duke3d::fi.ifhitbyweapon(body);
		}
		else
		{
			shot->spr.extra = 10;
			Duke3d::checkhitsprite(body, shot);
		}
		bool pass = state->xspr.health == 0 && body->spr.extra == 0 && duke->wackedbyactor == source &&
			Blood::playerSeqPlaying(blood, blastDeath ? 2 : 1) && !(body->spr.cstat & CSTAT_SPRITE_BLOCK_ALL);
		Printf("Superset incoming %s check: %s.\n", blastDeath ? "blastdeath" : "death", pass ? "PASS" : "FAIL");
		source->Destroy(); shot->Destroy();
		return;
	}
	int oldArmor[3], oldControls[Blood::kDamageMax];
	memcpy(oldArmor, blood->armor, sizeof(oldArmor));
	memcpy(oldControls, blood->damageControl, sizeof(oldControls));
	auto oldHealth = state->xspr.health;
	auto oldVelocity = state->vel;
	auto oldGod = blood->godMode;
	auto oldDukeGod = Duke3d::ud.god;
	auto oldPain = blood->painEffect;
	auto oldType = body->attackertype;
	auto oldOwner = body->GetHitOwner();
	auto oldAngle = body->hitang;
	auto oldPending = body->hitextra;
	auto reset = [&]()
	{
		state->xspr.health = 1600;
		state->vel.Zero();
		memset(blood->armor, 0, sizeof(blood->armor));
		memset(blood->damageControl, 0, sizeof(blood->damageControl));
		blood->godMode = false;
		Duke3d::ud.god = 0;
		body->hitextra = -1;
		shot->supersetHitDamage = -1;
		shot->spr.extra = 10;
		Publish(duke, blood);
	};
	reset();
	Duke3d::checkhitsprite(body, shot);
	bool physical = state->xspr.health == 1440 && body->spr.extra == 90 && body->hitextra == -1;
	FinishBloodMovement(duke);
	physical &= state->xspr.health == 1440;
	reset(); blood->armor[0] = 3200;
	Duke3d::checkhitsprite(body, shot);
	bool armor = state->xspr.health == 1580 && blood->armor[0] == 3060 && body->spr.extra == 99;
	bool impulse = fabs(state->vel.X - 1.25) < 1e-6;
	reset(); blood->armor[1] = 3200;
	body->attackertype = Duke3d::DukeRadiusExplosionClass;
	body->hitextra = 10;
	body->hitang = nullAngle;
	body->SetHitOwner(source);
	bool blast = Duke3d::fi.ifhitbyweapon(body) == 2 && state->xspr.health == 1580 && blood->armor[1] == 3060 && body->hitextra == -1;
	impulse &= fabs(state->vel.X - 2.5) < 1e-6;
	reset(); blood->damageControl[Blood::kDamageBullet] = 1; blood->armor[0] = 3200;
	Duke3d::checkhitsprite(body, shot);
	bool immunity = state->xspr.health == 1600 && blood->armor[0] == 3200 && fabs(state->vel.X - 1.25) < 1e-6;
	reset(); Duke3d::ud.god = 1;
	Duke3d::checkhitsprite(body, shot);
	bool god = state->xspr.health == 1600 && state->vel.isZero();
	reset();
	ApplyCalebDamage(duke, {160, DamageKind::Electric}, source);
	bool resistance = state->xspr.health == 1420;
	reset();
	PrepareDukeHit(shot, {136, DamageKind::Physical});
	Duke3d::checkhitsprite(body, shot);
	bool fractions = state->xspr.health == 1464 && body->spr.extra == 92;
	DamageKind mapped;
	bool mapping = DukeDamageKind(Duke3d::DukeShotSparkClass, mapped) && mapped == DamageKind::Physical &&
		DukeDamageKind(Duke3d::DukeRadiusExplosionClass, mapped) && mapped == DamageKind::Blast &&
		!DukeDamageKind(Duke3d::DukeFreezeBlastClass, mapped) && !DukeDamageKind(Duke3d::DukeShrinkSparkClass, mapped) &&
		!DukeDamageKind(Duke3d::DukeGrowSparkClass, mapped) && !DukeDamageKind(static_cast<PClassActor*>(state->GetClass()), mapped);
	reset(); blood->armor[0] = 3200;
	auto laser = Duke3d::CreateActor(body->sector(), body->spr.pos, Duke3d::DukeFirelaserClass,
		0, DVector2(1, 1), nullAngle, 0, 0, source, STAT_MISC);
	bool laserArmor = false;
	if (laser)
	{
		laser->spr.extra = 10;
		Duke3d::checkhitsprite(body, laser);
		laserArmor = state->xspr.health == 1580 && blood->armor[0] == 3060 && blood->armor[1] == 0;
		laser->Destroy();
	}
	state->xspr.health = oldHealth; state->vel = oldVelocity;
	blood->godMode = oldGod; Duke3d::ud.god = oldDukeGod; blood->painEffect = oldPain;
	memcpy(blood->armor, oldArmor, sizeof(oldArmor));
	memcpy(blood->damageControl, oldControls, sizeof(oldControls));
	body->attackertype = oldType; body->SetHitOwner(oldOwner); body->hitang = oldAngle; body->hitextra = oldPending;
	Publish(duke, blood);
	Printf("Superset incoming check: physical=%s, armor=%s, blast=%s, immunity=%s, god=%s, resistance=%s, fractions=%s, impulse=%s, laser=%s, mapping=%s.\n",
		physical ? "PASS" : "FAIL", armor ? "PASS" : "FAIL", blast ? "PASS" : "FAIL", immunity ? "PASS" : "FAIL",
		god ? "PASS" : "FAIL", resistance ? "PASS" : "FAIL", fractions ? "PASS" : "FAIL", impulse ? "PASS" : "FAIL", laserArmor ? "PASS" : "FAIL", mapping ? "PASS" : "FAIL");
	source->Destroy(); shot->Destroy();
}

// Deliberately restricted to the generated fixture. Positioning is test setup;
// doors, switches, lifts and transports still run through input/world ticks.
CCMD(superset_navigation)
{
	if (!IsActive() || sector.Size() != 12 || sector[0].hitag != 32123 || argv.argc() < 2)
	{
		Printf("Superset navigation: requires the generated navigation fixture.\n");
		return;
	}
	auto duke = static_cast<Duke3d::DDukePlayer*>(PlayerArray[myconnectindex]);
	auto body = duke->GetActor();
	if (!stricmp(argv[1], "pose") && argv.argc() == 3)
	{
		int n = atoi(argv[2]);
		if (n < 0 || n >= int(sector.Size()) || n == 1 || n == 10) return;
		auto sec = &sector[n];
		auto first = sec->walls.Data();
		DVector3 feet((first[0].pos.X + first[1].pos.X) / 2., 128., sec->floorz);
		if (n == 0) feet.X = first[1].pos.X - 32.;
		if (n == 9) feet.Y = 24.;
		ChangeActorSect(body, sec);
		body->spr.pos = feet;
		body->spr.Angles.Yaw = DAngle::fromDeg(n == 9 ? -90. : 0.);
		body->spr.Angles.Pitch = nullAngle;
		body->vel.Zero();
		duke->vel.Zero();
		duke->setCursector(sec);
		duke->on_ground = 1;
		duke->jumping_counter = 0;
		duke->toggle_key_flag = 0;
		if (HasBloodMovement(duke))
		{
			auto blood = NativePlayer(duke);
			auto state = blood->GetActor();
			ChangeActorSect(state, sec);
			state->spr.pos = DVector3(feet.XY(), state->spr.pos.Z + feet.Z - FeetZ(state));
			state->spr.Angles = body->spr.Angles;
			state->vel.Zero();
			state->xspr.height = 0;
			blood->InitAngles();
			Blood::playerResetInertia(blood);
			Publish(duke, blood);
			state->backuploc();
		}
		body->backuploc();
		duke->InitAngles();
		return;
	}
	if (!stricmp(argv[1], "key")) { duke->got_access |= 1; return; }
	if (!stricmp(argv[1], "check") && argv.argc() == 7)
	{
		int expectedSector = atoi(argv[3]);
		if (expectedSector < 0 || expectedSector >= int(sector.Size())) return;
		auto sec = &sector[expectedSector];
		bool pass = body->sector() == sec && body->spr.extra > 0 &&
			fabs(sec->floorz - atof(argv[4])) < .1 && fabs(sec->ceilingz - atof(argv[5])) < .1;
		if (!stricmp(argv[6], "cooldown")) pass &= duke->transporter_hold == 0 && !duke->on_warping_sector;
		if (!stricmp(argv[6], "ground")) pass &= fabs(body->spr.pos.Z - sec->floorz) < 1.;
		if (!stricmp(argv[6], "locked")) pass &= sector[10].ceilingz == 128 && duke->access_incs == 0;
		if (!stricmp(argv[6], "key")) pass &= duke->got_access == 0 && duke->access_incs == 0 && sector[10].ceilingz == 0;
		if (!stricmp(argv[6], "clock")) { pass &= duke->timebeforeexit < 26 * 8 && duke->timebeforeexit > 1; duke->timebeforeexit = 0; }
		Printf("Superset navigation %s: %s (sector=%d floor=%.2f ceiling=%.2f feet=%.2f health=%d hold=%d warp=%d access=%d keys=%d).\n",
			argv[2], pass ? "PASS" : "FAIL", sectindex(body->sector()), sec->floorz, sec->ceilingz,
			body->spr.pos.Z, body->spr.extra, duke->transporter_hold, duke->on_warping_sector, duke->access_incs, duke->got_access);
	}
}
}
