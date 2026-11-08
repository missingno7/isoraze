#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif
#include "../duke/src/duke3d.h"
#include "../duke/src/dukeactor.h"
#include "../blood/src/blood.h"
#include "duke_sounds.h"
#include "superset.h"
#include "mashup_resources.h"
#include "c_dispatch.h"
#include "gamestate.h"
#include "vm.h"

namespace Superset
{
void DukeSoundView(DVector3* position, DAngle* angle, sectortype** sector)
{
	// Match the native Blood listener; Duke sources use its existing distance
	// and visibility rules without installing a second listener or sound engine.
	auto player = Blood::getPlayer(myconnectindex);
	auto actor = player ? player->GetActor() : nullptr;
	if (position) *position = actor ? actor->spr.pos : DVector3{};
	if (angle) *angle = actor ? actor->spr.Angles.Yaw : nullAngle;
	if (sector) *sector = actor ? actor->sector() : nullptr;
}
bool DukeSoundUnderwater()
{
	auto player = Blood::getPlayer(myconnectindex);
	return player && player->isUnderwater;
}

namespace
{
Blood::DBloodActor* SoundBloodSource()
{
	Blood::BloodSpriteIterator it;
	while (auto actor = it.Next()) if (actor->spr.intowner == 31034) return actor;
	return nullptr;
}
Duke3d::DDukeActor* SoundDukeSource()
{
	Duke3d::DukeSpriteIterator it;
	while (auto actor = it.Next()) if (actor->spr.intowner == 31035) return actor;
	return nullptr;
}
FSoundChan* FindChannel(DCoreActor* actor, FSoundID sound)
{
	FSoundChan* result = nullptr;
	soundEngine->EnumerateChannels([&](FSoundChan* channel)
	{
		if (channel->SourceType == SOURCE_Actor && channel->Source == actor && channel->OrgID == sound) result = channel;
		return false;
	});
	return result;
}
int ScriptSound(const char* method)
{
	auto function = PClass::FindFunction("SupersetDukeSoundFixture", method);
	if (!function) return 0;
	int result = 0; VMReturn ret; ret.IntAt(&result);
	VMCall(function, nullptr, 0, &ret, 1);
	return result;
}
bool SourcesPlaying()
{
	auto native = SoundBloodSource(); auto foreign = SoundDukeSource();
	if (!native || !foreign) return false;
	auto a = FindChannel(native, Duke3d::DukeSoundByResource(GENERIC_AMBIENCE1));
	auto b = FindChannel(foreign, Duke3d::DukeSoundByResource(GENERIC_AMBIENCE10));
	return a && b && a->SysChannel && b->SysChannel && (a->ChanFlags & CHANF_LOOP) && (b->ChanFlags & CHANF_LOOP);
}
}

CCMD(superset_duke_sound_check)
{
	if (!IsBloodMap() || gamestate != GS_LEVEL) return;
	if (!SoundEnabled()) { Printf("Superset Duke sound check: FAIL (sound disabled).\n"); return; }
	const char* operation = argv.argc() > 1 ? argv[1] : "playing";
	auto body = Blood::getPlayer(myconnectindex)->GetActor();
	auto loop = Duke3d::DukeSoundByResource(GENERIC_AMBIENCE1);
	if (!stricmp(operation, "definitions"))
	{
		auto pistol = Duke3d::DukeSoundByResource(PISTOL_FIRE);
		auto primary = ::S_FindSound("PISTOL_FIRE");
		auto primaryDefinition = soundEngine->GetSfx(primary);
		auto sfx = soundEngine->GetSfx(pistol);
		TArray<uint8_t> bytes;
		auto reader = Mashup::OpenDukeResource("pistol.voc"); auto expected = reader.Read();
		bool raw = sfx && Duke3d::ReadSecondaryDukeSound(sfx->lumpnum, bytes) && bytes.Size() == expected.size() &&
			bytes.Size() > 26 && !memcmp(bytes.Data(), expected.data(), expected.size());
		bool names = primary != pistol && primaryDefinition && primaryDefinition->ResourceId == PISTOL_FIRE &&
			primaryDefinition->Volume == .25f && primaryDefinition->lumpnum == fileSystem.CheckNumForFullName("collision.wav") &&
			Duke3d::ResolveDukeSound(primary) == pistol && FSoundID::fromInt(ScriptSound("Pistol")) == primary &&
			Duke3d::ResolveDukeSound(::S_FindSound("CRANEGRAB")) == Duke3d::DukeSoundByResource(DUKE_GRUNT) &&
			Duke3d::DukeSoundByResource(9999) == NO_SOUND &&
			Duke3d::ResolveDukeSound(FSoundID::fromInt(ScriptSound("Pistol"))) == pistol &&
			FSoundID::fromInt(ScriptSound("NumericPistol")) == pistol;
		bool properties = sfx && sfx->ResourceId == -1 && !sfx->bLoadRAW &&
			sfx->Rolloff.MinDistance == 144 && sfx->Rolloff.MaxDistance == 1088 &&
			soundEngine->GetSfx(Duke3d::DukeSoundByResource(POOLBALLHIT))->NearLimit == 2;
		bool decode = soundEngine->GetMSLength(pistol) > 0 && soundEngine->GetSfx(pistol)->data.isValid();
		bool play = Duke3d::S_PlayActorSound(PISTOL_FIRE, body) == 0 && FindChannel(body, pistol);
		Duke3d::S_StopSound(PISTOL_FIRE, body);
		Printf("Superset Duke sound definitions: private bytes=%s, script names/numeric IDs=%s, native properties=%s, VOC decode=%s, native playback=%s.\n",
			raw ? "PASS" : "FAIL", names ? "PASS" : "FAIL", properties ? "PASS" : "FAIL", decode ? "PASS" : "FAIL", play ? "PASS" : "FAIL");
		return;
	}
	if (!stricmp(operation, "seed"))
	{
		auto native = Blood::actSpawnThing(body->sector(), body->spr.pos + DVector3(8, 0, 0), Blood::kThingTNTBarrel);
		auto type = PClass::FindActor("SupersetDukeSoundFixture");
		auto foreign = type ? Duke3d::CreateActor(body->sector(), body->spr.pos + DVector3(-8, 0, 0), type,
			0, DVector2(1, 1), nullAngle, 0, 0, nullptr, STAT_MISC) : nullptr;
		if (native) native->spr.intowner = 31034;
		if (foreign)
		{
			foreign->spr.intowner = 31035; foreign->supersetForeignFx = true;
			Duke3d::CallInitialize(foreign, nullptr); foreign->backuploc();
		}
		bool play = native && foreign && Duke3d::S_PlayActorSound(GENERIC_AMBIENCE1, native) == 0 && SourcesPlaying();
		Printf("Superset Duke sound sources: %s (native Blood actor and original Duke script playback).\n", play ? "PASS" : "FAIL");
		return;
	}
	auto native = SoundBloodSource(); auto foreign = SoundDukeSource();
	if (!native || !foreign) { Printf("Superset Duke sound check: FAIL (missing sources).\n"); return; }
	if (!stricmp(operation, "move"))
	{
		bool moved = SourcesPlaying();
		for (DCoreActor* actor : {static_cast<DCoreActor*>(native), static_cast<DCoreActor*>(foreign)})
		{
			auto id = Duke3d::DukeSoundByResource(actor == native ? GENERIC_AMBIENCE1 : GENERIC_AMBIENCE10);
			auto channel = FindChannel(actor, id);
			if (!channel) { moved = false; continue; }
			FVector3 before, after, velocity;
			soundEngine->CalcPosVel(channel, &before, &velocity);
			actor->spr.pos.X += 4; actor->backuploc();
			soundEngine->CalcPosVel(channel, &after, &velocity);
			moved &= abs(after.X - before.X - 4) < .001 && velocity.isZero();
			auto oldFlags = channel->ChanFlags;
			channel->ChanFlags |= CHANF_LISTENERZ;
			soundEngine->CalcPosVel(channel, &after, &velocity);
			moved &= abs(after.Y - GetSoundPos(body->spr.pos).Y) < .001;
			channel->ChanFlags = oldFlags;
		}
		Printf("Superset Duke sound movement: %s (native spatial callback follows both actor families).\n", moved ? "PASS" : "FAIL");
		return;
	}
	if (!stricmp(operation, "isolation"))
	{
		Blood::sfxPlay3DSound(native, 9999, 77);
		auto primaryId = soundEngine->FindSoundByResID(9999);
		auto primary = FindChannel(native, primaryId);
		bool isolated = primary && FindChannel(native, loop);
		if (primary)
		{
			int channel = primary->EntChannel; float pitch = primary->Pitch;
			Duke3d::S_StopSound(loop, native, channel);
			isolated &= FindChannel(native, primaryId) && FindChannel(native, loop);
			Duke3d::S_ChangeSoundPitch(loop, native, 1200);
			auto changed = FindChannel(native, loop); auto unchanged = FindChannel(native, primaryId);
			isolated &= changed && unchanged && changed->Pitch == 2.f && unchanged->Pitch == pitch;
			Duke3d::S_StopSound(loop, native);
			isolated &= FindChannel(native, primaryId) && !Duke3d::S_CheckAnyActorSoundPlaying(native) &&
				!Duke3d::S_CheckActorSoundPlaying(native, 9999);
			Duke3d::S_PlayActorSound(loop, native);
			Blood::sfxKill3DSound(native);
			isolated &= !FindChannel(native, primaryId) && FindChannel(native, loop);
		}
		Printf("Superset Duke sound isolation: %s (Blood playback and both families' stop/query/pitch operations).\n", isolated ? "PASS" : "FAIL");
		return;
	}
	if (!stricmp(operation, "water"))
	{
		auto player = Blood::getPlayer(myconnectindex); bool old = player->isUnderwater;
		player->isUnderwater = true;
		bool valid = Duke3d::S_PlayActorSound(SHOTGUN_FIRE, native) == 0;
		auto channel = FindChannel(native, Duke3d::DukeSoundByResource(SHOTGUN_FIRE));
		valid &= channel && abs(channel->Pitch - .64f) < .0001f && Duke3d::DukeSoundResource(Duke3d::DukeSoundByResource(RPG_EXPLODE)) == RPG_EXPLODE;
		Duke3d::S_StopSound(SHOTGUN_FIRE, native); player->isUnderwater = old;
		Printf("Superset Duke sound water: %s (native Blood medium supplies Duke pitch rules).\n", valid ? "PASS" : "FAIL");
		return;
	}
	if (!stricmp(operation, "clear"))
	{
		native->Destroy(); foreign->Destroy();
		bool clear = !FindChannel(native, loop) && !FindChannel(foreign, Duke3d::DukeSoundByResource(GENERIC_AMBIENCE10));
		Printf("Superset Duke sound cleanup: %s (native actor destruction stops both loops).\n", clear ? "PASS" : "FAIL");
		return;
	}
	Printf("Superset Duke sound playing: %s (both saved sources retain backend loop channels).\n", SourcesPlaying() ? "PASS" : "FAIL");
}
}
