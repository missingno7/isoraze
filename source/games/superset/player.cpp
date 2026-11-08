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
#include "c_dispatch.h"

namespace Blood { void SerializeSequences(FSerializer& arc); }

namespace Superset
{
namespace
{
struct MovementProbe
{
	bool active = false, moved = false, jumped = false, crouched = false;
	bool oneCollider = true, clock = true;
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
	void UseAction(Blood::DBloodPlayer*) override { useRequested = true; }
	void CollectPickups(Blood::DBloodPlayer*) override
	{
		// Duke map pickups still execute their native touch handlers. Independent
		// mixed pickup/inventory definitions are a subsequent gameplay milestone.
	}
};
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
	if (duke->newOwner || Duke3d::ud.cameraactor)
	{
		blood->cmd.ucmd.vel.Zero();
		blood->cmd.ucmd.ang.Zero();
	}
	DukePlayerWorld world;
	auto flags = body->spr.cstat;
	body->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	Blood::playerProcess(blood, &world);
	Blood::seqProcess(4);
	HideStateActor(state);
	Blood::actProcessDudeMovement(state, &world);
	body->spr.cstat = flags;
	// Duke's checksectors consumes the queued use action once, after publishing
	// the new pose. All sector effects remain on the Duke side of this boundary.
	if (world.useRequested) duke->cmd.ucmd.actions |= SB_OPEN;
	else duke->cmd.ucmd.actions &= ~SB_OPEN;
	Publish(duke, blood);
	if (probe.active)
	{
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

void ClearBloodControllerState() { Blood::seqKillAll(); }

void SerializeBloodControllerState(FSerializer& arc)
{
	if (arc.BeginObject("superset.blood_controller"))
	{
		Blood::SerializeSequences(arc);
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
		Printf("Caleb movement detail: ticks=%d moved=%d jumped=%d crouched=%d initial eye=%.4f radius=%.2f.\n",
			probe.ticks, probe.moved, probe.jumped, probe.crouched, probe.eye, state->clipdist);
	Printf("Superset movement check: native profile=%s, move/jump/crouch=%s, one collider=%s, clock=%s.\n",
		nativeProfile ? "PASS" : "FAIL", inputs ? "PASS" : "FAIL", probe.oneCollider ? "PASS" : "FAIL", probe.clock ? "PASS" : "FAIL");
	probe.active = false;
}
}
