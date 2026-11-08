#pragma once
#include "vectors.h"
#include "raze_sound.h"

struct sectortype;
namespace Duke3d
{
void PrepareSecondaryDukeSoundNames();
void InitSecondaryDukeSounds();
bool ReadSecondaryDukeSound(int lump, TArray<uint8_t>& data);
bool IsSecondaryDukeSound(FSoundID sound);
FSoundID ResolveDukeSound(FSoundID sound);
int DukeSoundResource(FSoundID sound);
bool UpdateSecondaryDukeSoundPosition(int type, const void* source, const float* point,
	int flags, FSoundID sound, FVector3* position, FVector3* velocity);
void S_ConfigureSound(FSoundID sound, int minpitch, int maxpitch, int flags, int distance, float volume);
}
namespace Superset
{
void DukeSoundView(DVector3* position, DAngle* angle, sectortype** sector);
bool DukeSoundUnderwater();
}
