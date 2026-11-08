#pragma once

#include "files.h"
#include "textureid.h"

class FGameTexture;
class FSoundID;
struct TexturePick;

// The secondary game's resources never enter the primary game's tile or lump namespace.
namespace Mashup
{
void LoadDukeResources(const char* path);
bool HasDukeResources();
const char* DukeResourceFingerprint();
FileReader OpenDukeResource(const char* name);
FString DukeSoundDefinitions();
FTextureID DukeTile(int tile);
FTextureID DukeTexture(const char* name);
int DukeTileNumber(FTextureID texture);
bool IsDukeTexture(FTextureID texture);
bool PickDukeTexture(FGameTexture* texture, int translation, TexturePick& pick);
bool CheckDukeResources();
void InitBloodResources();
void LoadBloodResources(const char* path);
const char* BloodResourceFingerprint();
void InitBloodSounds();
FSoundID BloodSound(int id);
bool ReadBloodSound(int lump, TArray<uint8_t>& data);
bool HasBloodResources();
FileReader OpenBloodResource(int id, const char* extension);
FTextureID BloodTile(int tile);
bool IsBloodTexture(FTextureID texture);
bool PickBloodTexture(FGameTexture* texture, int translation, TexturePick& pick);
bool CheckBloodResources();
void DrawResourcePreview();
}
