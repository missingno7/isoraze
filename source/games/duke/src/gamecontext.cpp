#include "duke3d.h"
#include "dukeactor.h"
#include "mashup_resources.h"
#include "m_argv.h"
#include "md5.h"
#include "serializer.h"
#include "c_dispatch.h"
#include "filesystem.h"
#include "raze_sound.h"
#include "gi.h"
#include <memory>

extern TArray<TPointer<MapRecord>> mapList;
extern TArray<VolumeRecord> volumes;
extern TArray<ClusterDef> clusters;

namespace Duke3d
{
void SerializeGameVars(FSerializer& arc);
namespace
{
struct ConSource { FString name, text; };
struct ConSound
{
	FString filename;
	int ps, pe, priority, flags, distance;
	float volume;
};
TArray<ConSource> sources;
TMap<int, VolumeRecord> volumes;
TMap<int, ClusterDef> clusters;
TMap<int, MapRecord> maps;
TMap<int, ConSound> sounds;
FString skillNames[MAXSKILLS], conDirectory, conRoot, programFingerprint;
TArray<FString> music;
std::unique_ptr<Quotes> quotes;
TObjPtr<DDukePlayer*> players[MAXPLAYERS];
void MarkPlayers()
{
	GC::MarkArray(players, MAXPLAYERS);
	for (int i = 0; i < iGameVarCount; ++i) if (!(aGameVars[i].dwFlags & (GAMEVAR_FLAG_PLONG | GAMEVAR_FLAG_PFUNC)))
	{
		// Per-player/actor entries store an array index in the lValue union.
		if (!(aGameVars[i].dwFlags & GAMEVAR_FLAG_USER_MASK)) aGameVars[i].lValue.Mark();
		aGameVars[i].initValue.Mark(); aGameVars[i].defaultValue.Mark();
	}
}

FString Fingerprint()
{
	MD5Context hash;
	for (auto& source : sources)
	{
		hash.Update(reinterpret_cast<const uint8_t*>(source.name.GetChars()), source.name.Len() + 1);
		hash.Update(reinterpret_cast<const uint8_t*>(source.text.GetChars()), source.text.Len() + 1);
	}
	uint8_t digest[16]; hash.Final(digest);
	FString result;
	for (auto byte : digest) result.AppendFormat("%02x", byte);
	return result;
}
}

DDukePlayer* SecondaryDukePlayer(int index)
{
	return unsigned(index) < MAXPLAYERS ? players[index].Get() : nullptr;
}

int FindConSource(const char* filename)
{
	if (!HasSecondaryDukeContext()) return fileSystem.FindFile(filename);
	FString path = filename; FixPathSeperator(path);
	FString name = path; name.ToLower();
	for (unsigned i = 0; i < sources.Size(); ++i) if (sources[i].name == name) return -int(i) - 2;
	FileReader reader;
	if (conDirectory.IsNotEmpty()) reader.OpenFile((conDirectory + path).GetChars());
	if (!reader.isOpen()) reader = Mashup::OpenDukeResource(name.GetChars());
	if (!reader.isOpen()) return -1;
	auto data = reader.Read();
	sources.Push({ name, FString(static_cast<const char*>(data.data()), data.size()) });
	return -int(sources.Size()) - 1;
}

FString ReadConSource(int index)
{
	return index >= 0 ? GetStringFromLump(index) : sources[-index - 2].text;
}

const char* ConSourceName(int index)
{
	if (index >= 0) return fileSystem.GetFileFullName(index);
	return index <= -2 && unsigned(-index - 2) < sources.Size() ? sources[-index - 2].name.GetChars() : "<missing>";
}

const char* SecondaryConRoot() { return conRoot.GetChars(); }
const char* SecondaryDukeProgramFingerprint() { return programFingerprint.GetChars(); }
Quotes& DukeQuotes() { return HasSecondaryDukeContext() ? *quotes : ::quoteMgr; }

VolumeRecord* ConVolume(int index)
{
	if (!HasSecondaryDukeContext()) return MustFindVolume(index);
	auto& volume = volumes[index]; volume.index = index; return &volume;
}
ClusterDef* ConCluster(int index)
{
	if (!HasSecondaryDukeContext()) return MustFindCluster(index);
	auto& cluster = clusters[index]; cluster.index = index; return &cluster;
}
MapRecord* ConMap(int volume, int level, bool create)
{
	if (!HasSecondaryDukeContext())
	{
		auto map = FindMapByIndexOnly(volume, level);
		return map || !create ? map : AllocateMap();
	}
	int index = makelevelnum(volume, level);
	return create ? &maps[index] : maps.CheckKey(index);
}
void ConLevelNumber(MapRecord* map, int number)
{
	if (HasSecondaryDukeContext()) map->levelNumber = number;
	else SetLevelNum(map, number);
}
void ConSkillName(int index, const FString& name)
{
	if (unsigned(index) >= MAXSKILLS) I_FatalError("CON skill index out of range: %d", index);
	(HasSecondaryDukeContext() ? skillNames[index] : gSkillNames[index]) = name;
}
TArray<FString>& ConSpecialMusic() { return HasSecondaryDukeContext() ? music : specialmusic; }
void DefineConSound(unsigned index, const char* filename, int ps, int pe, int priority, int flags, int distance, float volume)
{
	if (!HasSecondaryDukeContext()) { S_DefineSound(index, filename, ps, pe, priority, flags, distance, volume); return; }
	sounds.Insert(index, {filename, ps, pe, priority, flags, distance, volume});
}

void InitSecondaryDukeProgram(const char* rootCon)
{
	if (!Mashup::HasDukeResources() || HasSecondaryDukeContext()) I_FatalError("Invalid secondary Duke program startup");
	const int primaryFlags = ::g_gameType, primaryInfoFlags = gameinfo.gametype;
	const auto primaryMapCount = mapList.Size(), primaryVolumeCount = ::volumes.Size(), primaryClusterCount = ::clusters.Size();
	const auto primarySoundCount = soundEngine->GetNumSounds();
	DCorePlayer* primaryPlayers[MAXPLAYERS];
	for (int i = 0; i < MAXPLAYERS; ++i) primaryPlayers[i] = PlayerArray[i];
	FString primarySkills[MAXSKILLS], primaryQuotes[MAXQUOTES];
	for (int i = 0; i < MAXSKILLS; ++i) primarySkills[i] = gSkillNames[i];
	for (int i = 0; i < MAXQUOTES; ++i) primaryQuotes[i] = ::quoteMgr.GetRawQuote(i);
	secondaryGameFlags = GAMEFLAG_DUKE;
	quotes = std::make_unique<Quotes>();
	conRoot = "game.con";
	if (rootCon && *rootCon)
	{
		FString path = rootCon; FixPathSeperator(path);
		conRoot = ExtractFileBase(path.GetChars(), true); conDirectory = ExtractFilePath(path.GetChars());
		if (conDirectory.IsEmpty()) conDirectory = "./";
	}
	GC::AddMarkerFunc(MarkPlayers);
	for (int i = 0; i < MAXPLAYERS; ++i)
	{
		players[i] = Create<DDukePlayer>(i);
		GC::WriteBarrier(players[i]);
	}
	loadcons();
	bool isolated = ::g_gameType == primaryFlags && gameinfo.gametype == primaryInfoFlags &&
		mapList.Size() == primaryMapCount && ::volumes.Size() == primaryVolumeCount && ::clusters.Size() == primaryClusterCount &&
		soundEngine->GetNumSounds() == primarySoundCount;
	for (int i = 0; i < MAXPLAYERS; ++i) isolated &= PlayerArray[i] == primaryPlayers[i];
	for (int i = 0; i < MAXSKILLS; ++i) isolated &= gSkillNames[i] == primarySkills[i];
	for (int i = 0; i < MAXQUOTES; ++i) isolated &= primaryQuotes[i].Compare(::quoteMgr.GetRawQuote(i)) == 0;
	if (!isolated) I_FatalError("Superset: secondary CON compilation changed the map adapter's state");
	programFingerprint = Fingerprint();
	Printf("Superset Duke program: native CON compiler, %u private sources, %u maps, %u sounds.\n", sources.Size(), maps.CountUsed(), sounds.CountUsed());
	Printf("Superset Duke program isolation: flags/players/map catalog/skills/quotes/sound registry=PASS.\n");
}

void SerializeSecondaryDukeProgram(FSerializer& arc)
{
	if (HasSecondaryDukeContext() && arc.BeginObject("superset.duke_program"))
	{
		arc.Array("players", players, MAXPLAYERS);
		SerializeGameVars(arc);
		DukeQuotes().Serialize(arc);
		arc.EndObject();
	}
}

// Diagnostics exercise the existing compiler/VM with an optional test CON.
// No world events are dispatched automatically until their adapters exist.
CCMD(superset_duke_program_check)
{
	if (!isDuke()) return;
	// Native FinalizeSetup clears header/actor strengths after copying them to
	// Duke class defaults. Normalize a copy; the secondary program still owns
	// those values until actor registration has its own adapter.
	TArray<int> normalized = ScriptCode;
	normalized[0] = 0;
	for (const auto& info : gs.actorinfo) if (info.scriptaddress) normalized[info.scriptaddress] = 0;
	MD5Context hash;
	hash.Update(reinterpret_cast<const uint8_t*>(normalized.Data()), normalized.Size() * sizeof(int));
	uint8_t digest[16]; hash.Final(digest);
	FString codeHash;
	for (auto byte : digest) codeHash.AppendFormat("%02x", byte);
	Printf("Duke CON program: normalized_code=%s words=%u moves=%u actions=%u ais=%u vars=%d flags=%x health=%d ammo=%d gravity=%.6f queue=%d.\n",
		codeHash.GetChars(), ScriptCode.Size(), moves.Size(), actions.Size(), ais.Size(), iGameVarCount,
		DukeGameFlags(), gs.max_player_health, gs.max_ammo_amount[1], gs.gravity, spriteqamount);
	if (!HasSecondaryDukeContext() || argv.argc() < 2) return;
	const int global = GetGameID("SUPERSET_PROBE_GLOBAL"), perPlayer = GetGameID("SUPERSET_PROBE_PLAYER");
	if (global < 0 || perPlayer < 0) { Printf("Duke CON fixture: FAIL (missing test variables).\n"); return; }
	if (!stricmp(argv[1], "event")) OnEvent(EVENT_INIT, 0);
	if (!stricmp(argv[1], "mutate"))
	{
		SetGameVarID(global, 999, nullptr, 0); SetGameVarID(perPlayer, 888, nullptr, 0);
		getPlayer(0)->ammo_amount[1] = 77;
	}
	const bool distinct = getPlayer(0) != PlayerArray[0] && getPlayer(0)->GetActor() == nullptr &&
		getPlayer(1) != getPlayer(0) && getPlayer(1) != PlayerArray[1];
	Printf("Duke CON fixture: global=%d player0=%d player1=%d ammo=%d private_players=%s map=%s.\n",
		GetGameVarID(global, nullptr, 0).safeValue(), GetGameVarID(perPlayer, nullptr, 0).safeValue(),
		GetGameVarID(perPlayer, nullptr, 1).safeValue(), getPlayer(0)->ammo_amount[1], distinct ? "PASS" : "FAIL",
		currentLevel ? currentLevel->labelName.GetChars() : "none");
}
}
