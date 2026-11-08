// Explicit boundary between Blood weapon state and the Duke world simulation.
#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif

#include "../games/duke/src/duke3d.h"
#include "../games/duke/src/dukeactor.h"
#include "../games/blood/src/blood.h"
#include "mashup_player.h"
#include "mashup_resources.h"
#include "c_dispatch.h"
#include "texturemanager.h"
#include "raze_sound.h"
#include "games/superset/superset.h"

CVAR(Bool, mashup_combat, false, 0)

namespace Mashup
{
namespace
{
bool inputProbeActive = false;
int inputProbeVectors = 0;
int inputProbeTicks = 0, inputProbeFireTicks = 0;
short inputProbeAmmo[Duke3d::MAX_WEAPONS] {};

Blood::DBloodPlayer* Secondary(DCorePlayer* player)
{
	auto duke = dyn_cast<Duke3d::DDukePlayer>(player);
	return duke ? dyn_cast<Blood::DBloodPlayer>(duke->bloodCombat) : nullptr;
}

Duke3d::DDukePlayer* Owner(DCoreActor* actor)
{
	if (!CombatEnabled()) return nullptr;
	for (int i = connecthead; i >= 0; i = connectpoint2[i])
	{
		auto duke = Duke3d::getPlayer(i);
		auto blood = Secondary(duke);
		if (blood && blood->GetActor() == actor) return duke;
	}
	return nullptr;
}

void SyncPose(Duke3d::DDukePlayer* duke, Blood::DBloodPlayer* blood)
{
	auto body = duke->GetActor();
	auto actor = blood->GetActor();
	if (actor->sector() != body->sector()) ChangeActorSect(actor, body->sector());
	// Match the existing physical camera. Native Caleb movement/clearance is a
	// separate adapter; the combat actor never advances its own physical position.
	blood->zView = body->getOffsetZ() + duke->pyoff;
	actor->spr.pos = DVector3(body->spr.pos.XY(), blood->zView + blood->pPosture[blood->lifeMode][blood->posture].eyeAboveZ);
	actor->spr.Angles = body->spr.Angles;
	actor->spr.Angles.Pitch = duke->getPitchWithView();
	blood->slope = actor->spr.Angles.Pitch.Tan();
	blood->zWeapon = blood->zView + 4;
	blood->aim = DVector3(actor->spr.Angles.Yaw.ToVector(), blood->slope);
	actor->xspr.health = max(0, int(body->spr.extra)) << 4;
	blood->isUnderwater = body->sector()->lotag == ST_2_UNDERWATER;
	blood->pDudeInfo = &Blood::dudeInfo[Blood::kDudePlayer1 + blood->pnum - Blood::kDudeBase];
}
}

CCMD(mashup_combat_input_check)
{
	if (!Mashup::CombatEnabled() || gamestate != GS_LEVEL) return;
	auto duke = Duke3d::getPlayer(myconnectindex);
	if (argv.argc() > 1 && !stricmp(argv[1], "start"))
	{
		inputProbeActive = true;
		inputProbeVectors = 0;
		inputProbeTicks = inputProbeFireTicks = 0;
		memcpy(inputProbeAmmo, duke->ammo_amount, sizeof(inputProbeAmmo));
		return;
	}
	// A moving floor can carry the player over a pickup while Fire is held (the
	// stock E2L1 start does this). Permit inventory gains, but never consumption.
	bool noConsumption = true;
	for (unsigned i = 0; i < countof(inputProbeAmmo); ++i) noConsumption &= duke->ammo_amount[i] >= inputProbeAmmo[i];
	bool passed = inputProbeActive && inputProbeFireTicks > 0 && inputProbeVectors > 0 && duke->kickback_pic == 0 && noConsumption;
	inputProbeActive = false;
	if (!passed)
	{
		auto blood = Secondary(duke);
		Printf("Blood input detail: ticks=%d fire ticks=%d vectors=%d weapon=%d qav=%d timer=%d.\n", inputProbeTicks, inputProbeFireTicks,
			inputProbeVectors, blood ? blood->curWeapon : -1, blood ? blood->weaponQav : -1, blood ? blood->weaponTimer : -1);
	}
	Printf("Mashup combat input check: Blood fire=%s, Duke ammunition=%s.\n",
		passed ? "PASS" : "FAIL", noConsumption ? "PASS" : "FAIL");
}

CCMD(mashup_combat_check)
{
	if (!Mashup::CombatEnabled() || gamestate != GS_LEVEL)
	{
		Printf("Mashup combat check: requires -bloodduke and +mashup_combat 1 in a single-player level.\n");
		return;
	}
	auto duke = Duke3d::getPlayer(myconnectindex);
	if (!Mashup::IsCombatPlayer(duke)) return;
	auto blood = static_cast<Blood::DBloodPlayer*>(Mashup::GetBloodPlayer(myconnectindex));
	auto body = duke->GetActor();
	auto command = blood->cmd;
	int originalDamage = body->hitextra;
	auto originalFlags = body->spr.cstat;
	bool routing = Blood::getPlayer(myconnectindex) == blood && PlayerArray[myconnectindex] == duke &&
		blood->actor != body && !(blood->actor->spr.cstat & CSTAT_SPRITE_BLOCK_ALL);
	auto position = blood->GetActor()->spr.pos.plusZ(blood->zWeapon - blood->GetActor()->spr.pos.Z);
	position += body->spr.Angles.Yaw.ToVector() * 12;
	auto target = Duke3d::CreateActor(body->sector(), position, Duke3d::DukeShotSparkClass, 0,
		DVector2(1, 1), nullAngle, 0, 0, nullptr, STAT_MISC);
	bool hits = false, ownership = false;
	if (target)
	{
		target->spr.setspritetexture(Mashup::BloodTile(3870));
		target->spr.cstat = CSTAT_SPRITE_BLOCK_HITSCAN | CSTAT_SPRITE_YCENTER;
		target->hitextra = 0;
		Blood::playerReset(blood);
		blood->cmd.ucmd.actions = SB_FIRE;
		blood->cmd.ucmd.setNewWeapon(Blood::kWeapNone);
		for (int tick = 0; tick < 90 && target->hitextra == 0; ++tick) Blood::WeaponProcess(blood);
		hits = target->hitextra > 0 && target->attackertype == Duke3d::DukeMeleeAttackClass;
		ownership = target->GetHitOwner() == body;
		target->Destroy();
	}
	Blood::playerReset(blood);
	blood->cmd = command;
	bool self = body->hitextra == originalDamage && body->spr.cstat == originalFlags;
	bool samples = true;
	TArray<int> soundResources;
	soundResources.Push(Blood::gVectorData[Blood::kVectorTine].surfHit[Blood::kSurfFlesh].fxSnd);
	if (auto qav = Blood::getQAV(Blood::kQAVPFORK))
		for (int frame = 0; frame < qav->nFrames; ++frame)
			if (int resource = qav->frames[frame].sound.sound; resource > 0)
				soundResources.Push(resource);
	for (int resource : soundResources)
	{
		auto id = Mashup::BloodSound(resource);
		auto sample = soundEngine->GetSfx(id);
		TArray<uint8_t> raw;
		samples &= id.isvalid() && id != soundEngine->FindSoundByResID(resource) && sample && sample->bLoadRAW &&
			Mashup::ReadBloodSound(sample->lumpnum, raw) && raw.Size() > 8;
		if (samples && SoundEnabled())
		{
			Blood::sfxPlay3DSound(blood->GetActor(), resource, 26, 0);
			samples &= soundEngine->GetSfx(id)->data.isValid();
		}
	}
	Printf("Mashup combat check: player routing=%s, native QAV damage=%s, Duke ownership=%s, self protection=%s, sound bank=%s.\n",
		routing ? "PASS" : "FAIL", hits ? "PASS" : "FAIL", ownership ? "PASS" : "FAIL", self ? "PASS" : "FAIL", samples ? "PASS" : "FAIL");
}

bool CombatEnabled()
{
	return (mashup_combat || Superset::CalebSelected()) && HasBloodResources() && isDuke() && !isRR() && numplayers == 1;
}

bool IsCombatPlayer(DCorePlayer* player)
{
	return CombatEnabled() && Secondary(player) != nullptr;
}

DCoreActor* WorldSoundActor(DCoreActor* source)
{
	auto player = Owner(source);
	return player ? player->GetActor() : nullptr;
}

DCorePlayer* GetBloodPlayer(int player)
{
	if (!isDuke() || unsigned(player) >= MAXPLAYERS) return nullptr;
	return Secondary(PlayerArray[player]);
}

void ResetCombatPlayers()
{
	if (!isDuke()) return;
	for (auto player : PlayerArray)
		if (auto duke = dyn_cast<Duke3d::DDukePlayer>(player); duke && duke->bloodCombat)
		{
			if (auto blood = Secondary(duke)) Blood::seqKill(blood->GetActor());
			duke->bloodCombat->Destroy();
			duke->bloodCombat = nullptr;
			duke->bloodMovement = false;
		}
}

void PrepareCombat(DCorePlayer* player)
{
	if (!CombatEnabled()) return;
	auto duke = dyn_cast<Duke3d::DDukePlayer>(player);
	if (!duke || !duke->GetActor() || !duke->GetActor()->sector()) return;
	auto blood = Secondary(duke);
	if (!blood)
	{
		blood = Create<Blood::DBloodPlayer>(duke->pnum);
		blood->Clear();
		blood->pnum = duke->pnum;
		Blood::playerReset(blood);
		// This actor supplies Blood-specific weapon origins and ownership only.
		// It is invisible, nonblocking and outside all Blood/Duke simulation lists.
		blood->actor = Blood::actSpawnSprite(duke->GetActor()->sector(), duke->GetActor()->spr.pos, MAXSTATUS - 1, true);
		blood->GetActor()->spr.cstat = CSTAT_SPRITE_INVISIBLE | CSTAT_SPRITE_YCENTER;
		blood->GetActor()->spr.type = Blood::kDudePlayer1 + duke->pnum;
		blood->GetActor()->spr.setspritetexture(BloodTile(3870));
		blood->GetActor()->spr.scale = DVector2(37. / 64., 37. / 64.);
		duke->bloodCombat = blood;
		GC::WriteBarrier(duke, blood);
	}
	if (!Superset::PrepareBloodMovement(duke)) SyncPose(duke, blood);
	blood->cmd = duke->cmd;
	if (duke->newOwner || Duke3d::ud.cameraactor) blood->cmd.ucmd.actions &= ~(SB_FIRE | SB_ALTFIRE);
	if (inputProbeActive)
	{
		++inputProbeTicks;
		if (blood->cmd.ucmd.actions & (SB_FIRE | SB_ALTFIRE)) ++inputProbeFireTicks;
	}
	// Only the pitchfork has a world adapter in this increment. Retain the native
	// raise/lower, primary/alternate fire and QAV callback state machine.
	int weapon = blood->cmd.ucmd.getNewWeapon();
	if (weapon && weapon != Blood::kWeapPitchFork && weapon != WeaponSel_Next && weapon != WeaponSel_Prev)
		blood->cmd.ucmd.setNewWeapon(Blood::kWeapNone);
	duke->quick_kick = duke->last_quick_kick = duke->knee_incs = 0;
	duke->kickback_pic = duke->okickback_pic = 0;
	duke->cmd.ucmd.actions &= ~(SB_QUICK_KICK | SB_HOLSTER);
}

void TickCombat(DCorePlayer* player)
{
	if (!IsCombatPlayer(player)) return;
	if (Superset::HasBloodMovement(player))
	{
		Superset::FinishBloodMovement(player);
		return;
	}
	auto duke = static_cast<Duke3d::DDukePlayer*>(player);
	auto blood = Secondary(duke);
	SyncPose(duke, blood);
	Blood::WeaponProcess(blood);
	if ((blood->cmd.ucmd.actions & SB_HOLSTER) && blood->curWeapon) Blood::WeaponLower(blood);
}

bool DrawCombat(DCorePlayer* player, double interpolation)
{
	if (!IsCombatPlayer(player)) return false;
	auto duke = static_cast<Duke3d::DDukePlayer*>(player);
	if (duke->over_shoulder_on || duke->newOwner || Duke3d::ud.cameraactor || duke->GetActor()->spr.extra <= 0) return true;
	auto blood = Secondary(duke);
	Blood::WeaponDraw(blood, duke->GetActor()->spr.shade, 160, 200, 0, nullAngle);
	return true;
}

bool FireBloodVector(DCoreActor* source, double offset, double zoffset, DVector3 direction, int type, double range)
{
	auto owner = Owner(source);
	if (!owner) return false;
	if (inputProbeActive) ++inputProbeVectors;
	// Never pass a Duke actor through Blood's typed HitInfo/trigger/damage path.
	if (type != Blood::kVectorTine || direction.XY().LengthSquared() == 0) return true;
	auto& vector = Blood::gVectorData[type];
	if (range < 0) range = vector.fMaxDist();
	direction /= direction.XY().Length();
	auto body = owner->GetActor();
	auto origin = source->spr.pos.plusZ(zoffset);
	origin += DVector2(-source->spr.Angles.Yaw.Sin(), source->spr.Angles.Yaw.Cos()) * offset;
	THitInfo<DCoreActor> hit {};
	auto flags = body->spr.cstat;
	body->spr.cstat &= ~CSTAT_SPRITE_BLOCK_ALL;
	hitscan(origin, source->sector(), direction * 1024., hit, CLIPMASK1);
	body->spr.cstat = flags;
	if (!hit.hitSector || (range > 0 && (hit.hitpos.XY() - origin.XY()).Length() >= range)) return true;
	auto hitter = Duke3d::CreateActor(hit.hitSector, hit.hitpos, Duke3d::DukeMeleeAttackClass,
		-15, DVector2(0, 0), source->spr.Angles.Yaw, 0, 0, body, STAT_PROJECTILE);
	if (!hitter) return true;
	// Blood tines do half base damage to non-player targets (Q4 health). Duke's
	// hit interface uses integer health, so round down to its representable unit.
	hitter->spr.extra = vector.dmg / 2;
	if (auto target = dyn_cast<Duke3d::DDukeActor>(hit.hitActor))
	{
		if (!isaccessswitch(target->spr.spritetexture()))
		{
			bool flesh = Duke3d::badguy(target) || target->isPlayer();
			Duke3d::checkhitsprite(target, hitter);
			Duke3d::checkhitswitch(owner, nullptr, target);
			if (flesh)
				Blood::sfxPlay3DSound(hit.hitpos, vector.surfHit[Blood::kSurfFlesh].fxSnd, hit.hitSector);
		}
	}
	else if (hit.hitWall)
	{
		auto wall = hit.hitWall;
		if ((wall->cstat & CSTAT_WALL_BOTTOM_SWAP) && wall->twoSided() && hit.hitpos.Z >= wall->nextSector()->floorz)
			wall = wall->nextWall();
		if (!isaccessswitch(wall->walltexture))
		{
			Duke3d::checkhitwall(hitter, wall, hit.hitpos);
			Duke3d::checkhitswitch(owner, wall, nullptr);
		}
	}
	hitter->Destroy();
	return true;
}
}
