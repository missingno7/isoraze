// Runtime checks for the shared state used by the Blood/Duke integration.
// These deliberately mix real actor classes in one engine status list, just
// as Blood items and Duke sector effectors share the status number 3.

#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif

#include "../games/duke/src/duke3d.h"
#include "../games/duke/src/dukeactor.h"
#include "../games/blood/src/blood.h"
#include "c_dispatch.h"
#include "gamestate.h"
#include "savegamehelp.h"
#include "mashup_resources.h"

namespace Mashup
{
void DrawWeaponResourcePreview()
{
	if (auto qav = Blood::getQAV(Blood::kQAVFORKIDLE); qav && qav->duration > 0)
		qav->Draw(PlayClock % qav->duration, 0, 0, 0, false, 1.);
}
}

CCMD(mashup_resource_check)
{
	bool resources = Mashup::CheckBloodResources();
	bool sequence = false, weapon = false;
	bool spriteSave = false;
	if (Mashup::HasBloodResources())
	{
		spritetype sprite {}, restored {};
		sprite.setspritetexture(Mashup::BloodTile(3870));
		FRazeSerializer writer;
		if (writer.OpenWriter(false))
		{
			Serialize(writer, "sprite", sprite, nullptr);
			unsigned length;
			const char* json = writer.GetOutput(&length);
			FRazeSerializer reader;
			if (reader.OpenReader(json, length))
			{
				Serialize(reader, "sprite", restored, nullptr);
				spriteSave = restored.spritetexture() == sprite.spritetexture() && restored.picnum > INT16_MAX;
				reader.Close();
			}
		}
		if (auto seq = Blood::getSequence(12032))
			sequence = seq->nFrames > 0 && Mashup::IsBloodTexture(Blood::seqGetTexture(&seq->frames[0]));
		if (auto qav = Blood::getQAV(Blood::kQAVFORKIDLE))
		{
			weapon = qav->nFrames > 0 && qav->duration > 0;
			bool hasTile = false;
			for (int f = 0; f < qav->nFrames; ++f)
				for (auto& tile : qav->frames[f].tiles)
					if (tile.texid.isValid())
					{
						hasTile = true;
						weapon &= Mashup::IsBloodTexture(tile.texid);
					}
			weapon &= hasTile;
		}
	}
	Printf("Mashup resource check: isolation/colors=%s, Caleb SEQ=%s, weapon QAV=%s, sprite save=%s.\n",
		resources ? "PASS" : "FAIL", sequence ? "PASS" : "FAIL", weapon ? "PASS" : "FAIL", spriteSave ? "PASS" : "FAIL");
}

namespace
{
bool CheckSectorSave()
{
	sectortype source {}, defaults {}, restored {};
	Blood::XSECTOR xs {};
	source.lockinfo = 0x5a;
	source.shadedsector = 0xa5;
	source.baseFloor = 123;
	source.velFloor = 4;
	source._xs = &xs;
	xs.Underwater = 1;
	xs.rxID = 77;

	FRazeSerializer writer;
	if (!writer.OpenWriter(false)) return false;
	Serialize(writer, "sector", source, &defaults);
	// A complete save contains an object table. This isolated sector only has
	// null actor references, so supply the empty table explicitly.
	writer.BeginArray("objects");
	writer.EndArray();
	unsigned length;
	const char* json = writer.GetOutput(&length);
	FRazeSerializer reader;
	if (!reader.OpenReader(json, length)) return false;
	reader.ReadObjects(false);
	Serialize(reader, "sector", restored, &defaults);
	reader.Close();
	return restored.hasX() && restored.xs().Underwater && restored.xs().rxID == 77 &&
		restored.baseFloor == 123 && restored.velFloor == 4 &&
		(!isDukeEngine() || (restored.lockinfo == 0x5a && restored.shadedsector == 0xa5));
}

template<class TActor>
bool CheckIterators(sectortype* sect, int stat, TActor* first, TActor* second)
{
	TStatIterator<TActor> bystat(stat);
	bool ok = bystat.Peek() == first && bystat.Peek() == first;
	ok &= bystat.Next() == first;
	ok &= bystat.Peek() == second && bystat.Next() == second;
	ok &= bystat.Peek() == nullptr && bystat.Next() == nullptr;
	bystat.Reset(stat);
	ok &= bystat.Next() == first && bystat.Next() == second && bystat.Next() == nullptr;

	TSectIterator<TActor> bysector(sect);
	ok &= bysector.Peek() == first && bysector.Peek() == first;
	ok &= bysector.Next() == first;
	ok &= bysector.Peek() == second && bysector.Next() == second;
	ok &= bysector.Peek() == nullptr && bysector.Next() == nullptr;
	bysector.Reset(sect);
	ok &= bysector.Next() == first && bysector.Next() == second && bysector.Next() == nullptr;

	// Full-world iteration must neither expose the other family nor miss the
	// test actors when they are surrounded by actors from the other game.
	TSpriteIterator<TActor> all;
	int found = 0;
	while (auto actor = all.Next())
	{
		ok &= actor->IsKindOf(RUNTIME_CLASS(TActor));
		if (actor == first || actor == second) found++;
	}
	ok &= found == 2;
	all.Reset();
	found = 0;
	while (auto actor = all.Next())
		if (actor == first || actor == second) found++;
	return ok && found == 2;
}

// Destruction unlinks every test actor before the temporary sector goes away.
struct TestActors
{
	sectortype sect {};
	TArray<DCoreActor*> actors;
	int oldtimer = leveltimer;

	~TestActors()
	{
		for (auto actor : actors) actor->Destroy();
		leveltimer = oldtimer;
	}

	template<class TActor>
	TActor* Add(int stat, PClass* type = RUNTIME_CLASS(TActor))
	{
		auto actor = static_cast<TActor*>(InsertActor(type, &sect, stat, true));
		actors.Push(actor);
		return actor;
	}
};
}

CCMD(mashup_runtime_check)
{
	if (gamestate != GS_LEVEL || (!isDukeEngine() && !isBlood()))
	{
		Printf("Load a Duke or Blood level before running mashup_runtime_check.\n");
		return;
	}

	int stat = MAXSTATUS - 1;
	while (stat >= 0 && statList[stat].firstEntry) stat--;
	if (stat < 0)
	{
		Printf("Mashup runtime check: no empty status list available.\n");
		return;
	}

	const int oldcount = Numsprites;
	bool dukepass, bloodpass, corepass, sectorpass, removalpass;
	{
		TestActors test;
		// Put a foreign actor at both ends, and between each matching actor.
		test.Add<DCoreActor>(stat);
		auto d0 = test.Add<Duke3d::DDukeActor>(stat);
		auto b0 = test.Add<Blood::DBloodActor>(stat);
		auto pawnclass = PClass::FindActor("DukePlayerPawn");
		auto d1 = test.Add<Duke3d::DDukeActor>(stat, pawnclass);
		auto b1 = test.Add<Blood::DBloodActor>(stat);
		test.Add<DCoreActor>(stat);

		dukepass = CheckIterators(&test.sect, stat, d0, d1);
		bloodpass = CheckIterators(&test.sect, stat, b0, b1);
		corepass = true;
		TStatIterator<DCoreActor> core(stat);
		for (auto actor : test.actors) corepass &= core.Next() == actor;
		corepass &= core.Next() == nullptr;
		TSectIterator<DCoreActor> coresect(&test.sect);
		for (auto actor : test.actors) corepass &= coresect.Next() == actor;
		corepass &= coresect.Next() == nullptr;

		Blood::XSECTOR xs {};
		test.sect.lockinfo = 0x5a;
		test.sect.shadedsector = 0xa5;
		test.sect.hitagactor = d0;
		sectorpass = !test.sect.hasX();
		test.sect._xs = &xs;
		test.sect.upperLink = b0;
		test.sect.lowerLink = b1;
		test.sect.baseFloor = 123;
		test.sect.velFloor = 4;
		sectorpass &= test.sect.hasX() && test.sect.lockinfo == 0x5a &&
			test.sect.shadedsector == 0xa5 && test.sect.hitagactor == d0;
		test.sect.hitagactor = d1;
		sectorpass &= test.sect._xs == &xs && test.sect.upperLink == b0 &&
			test.sect.lowerLink == b1 && test.sect.baseFloor == 123 && test.sect.velFloor == 4;

		// Both iterator types must cache the next link before returning an actor.
		Duke3d::DukeStatIterator ds(stat);
		Blood::BloodSectIterator bs(&test.sect);
		removalpass = ds.Next() == d0 && bs.Next() == b0;
		d0->Destroy();
		b0->Destroy();
		test.actors.Delete(1, 2);
		removalpass &= ds.Peek() == d1 && ds.Next() == d1 && ds.Next() == nullptr;
		removalpass &= bs.Peek() == b1 && bs.Next() == b1 && bs.Next() == nullptr;
	}
	const bool clean = Numsprites == oldcount && statList[stat].firstEntry == nullptr &&
		statList[stat].lastEntry == nullptr;
	const bool savepass = CheckSectorSave();
	Printf("Mashup runtime check: Duke=%s, Blood=%s, core=%s, sector state=%s, sector save=%s, removal=%s, cleanup=%s.\n",
		dukepass ? "PASS" : "FAIL", bloodpass ? "PASS" : "FAIL", corepass ? "PASS" : "FAIL",
		sectorpass ? "PASS" : "FAIL", savepass ? "PASS" : "FAIL",
		removalpass ? "PASS" : "FAIL", clean ? "PASS" : "FAIL");
}
