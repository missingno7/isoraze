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
#include "games/superset/damage.h"
#include "v_draw.h"
#include "v_text.h"

CVAR(Bool, mashup_combat, false, 0)

namespace Mashup
{
namespace
{
bool inputProbeActive = false;
int inputProbeVectors = 0;
int inputProbeTicks = 0, inputProbeFireTicks = 0;
int inputProbeVectorTypes[Blood::kVectorMax] {};
int weaponProbeWeapon = 0, weaponProbeAmmo = 0, weaponProbeBefore = 0, weaponProbeVector = 0;
short inputProbeAmmo[Duke3d::MAX_WEAPONS] {};

Blood::DBloodPlayer* Secondary(DCorePlayer* player)
{
	auto duke = dyn_cast<Duke3d::DDukePlayer>(player);
	return duke ? dyn_cast<Blood::DBloodPlayer>(duke->bloodCombat) : nullptr;
}

Duke3d::DDukePlayer* Owner(DCoreActor* actor)
{
	// Identity must remain stable while an emitted projectile/effect outlives
	// the selected weapon mode. Ordinary Blood never enters Duke player lookup.
	if (!isDuke() || isRR()) return nullptr;
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
	// Duke player velocity is already world displacement per simulation step.
	// Native Blood projectiles inherit half this vector when they are emitted.
	actor->vel = duke->vel;
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

CCMD(superset_weapon_input_check)
{
	if (!Superset::IsActive() || !CombatEnabled() || gamestate != GS_LEVEL) return;
	auto duke = Duke3d::getPlayer(myconnectindex);
	auto blood = Secondary(duke);
	if (!blood) return;
	auto body = duke->GetActor();
	Duke3d::DDukeActor* target = nullptr;
	Duke3d::DukeStatIterator it(MAXSTATUS - 2);
	while (auto actor = it.Next()) if (actor->spr.hitag == 31019) { target = actor; break; }
	if (argv.argc() == 4 && !stricmp(argv[1], "start"))
	{
		if (target) target->Destroy();
		weaponProbeWeapon = !stricmp(argv[2], "shotgun") ? Blood::kWeapShotgun : Blood::kWeapTommyGun;
		weaponProbeAmmo = weaponProbeWeapon == Blood::kWeapShotgun ? 2 : 3;
		bool alternate = !stricmp(argv[3], "alternate");
		weaponProbeVector = weaponProbeWeapon == Blood::kWeapShotgun ?
			(alternate ? Blood::kVectorShellAP : Blood::kVectorShell) : (alternate ? Blood::kVectorTommyAP : Blood::kVectorTommyRegular);
		weaponProbeBefore = blood->ammoCount[weaponProbeAmmo];
		inputProbeActive = true;
		inputProbeVectors = inputProbeTicks = inputProbeFireTicks = 0;
		memset(inputProbeVectorTypes, 0, sizeof(inputProbeVectorTypes));
		memcpy(inputProbeAmmo, duke->ammo_amount, sizeof(inputProbeAmmo));
		auto pos = DVector3(blood->GetActor()->spr.pos.XY(), blood->zWeapon) + body->spr.Angles.Yaw.ToVector() * 32;
		target = Duke3d::CreateActor(body->sector(), pos, PClass::FindActor("DukeLizTrooper"),
			0, DVector2(3, 3), nullAngle, 0, 0, body, STAT_MISC);
		if (target)
		{
			ChangeActorStat(target, MAXSTATUS - 2);
			target->spr.cstat = CSTAT_SPRITE_BLOCK_HITSCAN | CSTAT_SPRITE_YCENTER | CSTAT_SPRITE_INVISIBLE;
			target->spr.hitag = 31019;
			target->flags2 |= SFLAG2_NODAMAGEPUSH;
			target->flags3 |= SFLAG3_NOHITJIBS;
			target->spr.extra = 5000;
		}
		return;
	}
	int spent = weaponProbeBefore - blood->ammoCount[weaponProbeAmmo];
	bool ammo = spent > 0 && inputProbeVectors == spent * (weaponProbeWeapon == Blood::kWeapShotgun ? 16 : 1);
	// Native weapons may switch automatically after firing their last round.
	bool vectors = inputProbeActive && inputProbeFireTicks > 0 &&
		inputProbeVectors > 0 && inputProbeVectorTypes[weaponProbeVector] == inputProbeVectors;
	bool damage = target && target->hitextra > 0 && target->attackertype == Duke3d::DukeShotSparkClass && target->GetHitOwner() == body;
	bool nativeAmmo = true;
	for (unsigned i = 0; i < countof(inputProbeAmmo); ++i) nativeAmmo &= duke->ammo_amount[i] >= inputProbeAmmo[i];
	Printf("Superset weapon input check: vectors=%s, Blood ammo=%s, Duke hit=%s, Duke ammo=%s (weapon=%d spent=%d vectors=%d hits=%d).\n",
		vectors ? "PASS" : "FAIL", ammo ? "PASS" : "FAIL", damage ? "PASS" : "FAIL", nativeAmmo ? "PASS" : "FAIL",
		weaponProbeWeapon, spent, inputProbeVectors, target ? int(target->hitextra) : -1);
	inputProbeActive = false;
	if (target) target->Destroy();
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
	// Keep unsupported missile/throwable weapons outside the active set until
	// their world adapters exist. Selection and QAV transitions remain native.
	int weapon = blood->cmd.ucmd.getNewWeapon();
	bool supported = weapon == Blood::kWeapPitchFork || (Superset::IsActive() &&
		(weapon == Blood::kWeapShotgun || weapon == Blood::kWeapTommyGun || weapon == Blood::kWeapSpraycan));
	if (weapon && !supported && weapon != WeaponSel_Next && weapon != WeaponSel_Prev)
		blood->cmd.ucmd.setNewWeapon(Blood::kWeapNone);
	// Thrown cans need thing physics and blast/environment adapters first.
	if (Superset::IsActive() && blood->curWeapon == Blood::kWeapSpraycan) blood->cmd.ucmd.actions &= ~SB_ALTFIRE;
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
	Superset::CollectBloodPickups(player);
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
	if (Superset::IsActive())
	{
		const char* name = blood->curWeapon == Blood::kWeapShotgun ? "Shotgun" : blood->curWeapon == Blood::kWeapTommyGun ? "Tommy gun" :
			blood->curWeapon == Blood::kWeapSpraycan ? "Spray can (primary)" : "Pitchfork";
		FString line = FStringf("Blood %s", name);
		if (blood->weaponAmmo >= 0) line.AppendFormat("   Ammo %d", blood->ammoCount[blood->weaponAmmo]);
		DrawText(twod, SmallFont, CR_WHITE, 6, 150, line.GetChars(), DTA_VirtualWidth, 320, DTA_VirtualHeight, 200, DTA_KeepRatio, true, TAG_DONE);
		if (Superset::HasBloodMovement(player))
		{
			line.Format("Armor: Body %d  Fire %d  Spirit %d", blood->armor[0] >> 4, blood->armor[1] >> 4, blood->armor[2] >> 4);
			DrawText(twod, SmallFont, CR_WHITE, 6, 158, line.GetChars(), DTA_VirtualWidth, 320, DTA_VirtualHeight, 200, DTA_KeepRatio, true, TAG_DONE);
		}
	}
	return true;
}

bool FireBloodVector(DCoreActor* source, double offset, double zoffset, DVector3 direction, int type, double range)
{
	if (!CombatEnabled()) return false;
	auto owner = Owner(source);
	if (!owner) return false;
	if (inputProbeActive) ++inputProbeVectors;
	if (inputProbeActive && type >= 0 && type < Blood::kVectorMax) ++inputProbeVectorTypes[type];
	// Never pass a Duke actor through Blood's typed HitInfo/trigger/damage path.
	bool physical = type == Blood::kVectorTine || (Superset::IsActive() && (type == Blood::kVectorShell ||
		type == Blood::kVectorShellAP || type == Blood::kVectorTommyRegular || type == Blood::kVectorTommyAP));
	if (!physical || direction.XY().LengthSquared() == 0) return true;
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
	auto attackClass = type == Blood::kVectorTine ? Duke3d::DukeMeleeAttackClass : Duke3d::DukeShotSparkClass;
	auto hitter = Duke3d::CreateActor(hit.hitSector, hit.hitpos, attackClass,
		-15, DVector2(0, 0), source->spr.Angles.Yaw, 0, 0, body, STAT_PROJECTILE);
	if (!hitter) return true;
	auto target = dyn_cast<Duke3d::DDukeActor>(hit.hitActor);
	if (!Superset::PrepareDukeHit(hitter, Superset::BloodVectorDamage(type, target && target->isPlayer())))
	{
		hitter->Destroy();
		return true;
	}
	if (target)
	{
		if (!isaccessswitch(target->spr.spritetexture()))
		{
			bool flesh = Duke3d::badguy(target) || target->isPlayer();
			Duke3d::checkhitsprite(target, hitter);
			Duke3d::checkhitswitch(owner, nullptr, target);
			if (flesh)
			{
				auto& impact = vector.surfHit[Blood::kSurfFlesh];
				if (impact.fxSnd >= 0) Blood::sfxPlay3DSound(hit.hitpos, impact.fxSnd, hit.hitSector);
				if (Superset::IsActive())
				{
					if (impact.fx2 >= 0) Blood::gFX.fxSpawnActor(impact.fx2, hit.hitSector, hit.hitpos - direction);
					if (impact.fx3 >= 0) Blood::gFX.fxSpawnActor(impact.fx3, hit.hitSector, hit.hitpos - direction);
				}
			}
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
	else if (type != Blood::kVectorTine && hit.hitpos.Z <= getceilzofslopeptr(hit.hitSector, hit.hitpos) + .01)
		Duke3d::checkhitceiling(hit.hitSector);
	hitter->Destroy();
	return true;
}
}
