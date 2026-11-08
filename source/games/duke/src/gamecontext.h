#pragma once
#include "gamecontrol.h"
#include "zstring.h"

class Quotes;
class FSerializer;
struct MapRecord;
struct VolumeRecord;
struct ClusterDef;

namespace Duke3d
{
class DDukePlayer;

// Native Duke rules can own a program while another adapter owns the map.
// A zero override keeps every original Duke/RR frontend on its existing flags.
inline int secondaryGameFlags = 0;
inline bool HasSecondaryDukeContext() { return secondaryGameFlags != 0; }
inline int DukeGameFlags() { return secondaryGameFlags ? secondaryGameFlags : ::g_gameType; }
inline void AddDukeGameFlags(int flags)
{
	if (secondaryGameFlags) secondaryGameFlags |= flags;
	else ::g_gameType |= flags;
}
inline bool isNamWW2GI() { return DukeGameFlags() & (GAMEFLAG_NAM | GAMEFLAG_NAPALM | GAMEFLAG_WW2GI); }
inline bool isWW2GI() { return DukeGameFlags() & GAMEFLAG_WW2GI; }
inline bool isDuke() { return DukeGameFlags() & GAMEFLAG_DUKE; }
inline bool isRR() { return DukeGameFlags() & GAMEFLAG_RRALL; }
inline bool isRRRA() { return DukeGameFlags() & GAMEFLAG_RRRA; }
inline bool isRoute66() { return DukeGameFlags() & GAMEFLAG_ROUTE66; }
inline bool isWorldTour() { return DukeGameFlags() & GAMEFLAG_WORLDTOUR; }
inline bool isPlutoPak() { return DukeGameFlags() & GAMEFLAG_PLUTOPAK; }
inline bool isVacation() { return DukeGameFlags() & GAMEFLAG_DUKEVACA; }
inline bool isShareware() { return DukeGameFlags() & GAMEFLAG_SHAREWARE; }
inline bool isDukeLike() { return DukeGameFlags() & (GAMEFLAG_NAM | GAMEFLAG_NAPALM | GAMEFLAG_WW2GI | GAMEFLAG_DUKE); }
inline bool isDukeEngine() { return DukeGameFlags() & (GAMEFLAG_NAM | GAMEFLAG_NAPALM | GAMEFLAG_WW2GI | GAMEFLAG_DUKE | GAMEFLAG_RRALL); }

void InitSecondaryDukeProgram(const char* rootCon);
DDukePlayer* SecondaryDukePlayer(int index);
void SerializeSecondaryDukeProgram(FSerializer& arc);
const char* SecondaryDukeProgramFingerprint();
int FindConSource(const char* name);
FString ReadConSource(int index);
const char* ConSourceName(int index);
const char* SecondaryConRoot();
Quotes& DukeQuotes();
VolumeRecord* ConVolume(int index);
ClusterDef* ConCluster(int index);
MapRecord* ConMap(int volume, int level, bool create = false);
void ConLevelNumber(MapRecord* map, int number);
void ConSkillName(int index, const FString& name);
TArray<FString>& ConSpecialMusic();
void DefineConSound(unsigned index, const char* filename, int ps, int pe, int priority, int flags, int distance, float volume);
}
