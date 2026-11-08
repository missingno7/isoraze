// The combined game's frontend. Native world adapters are composed here;
// they do not own the combined game's identity, resource banks or save schema.
#include "superset.h"
#include "units.h"
#include "gamestruct.h"
#include "gamecontrol.h"
#include "mashup_resources.h"
#include "m_argv.h"
#include "c_dispatch.h"
#include "c_cvars.h"
#include "cmdlib.h"
#include "filesystem.h"
#include "md5.h"
#include "version.h"
#include "gamestate.h"
#include "savegamehelp.h"
#include "duke_effects.h"
#include "duke_projectiles.h"
#include "duke_sounds.h"
#include "duke_weapons.h"
#include "duke_pickups.h"
#include "arsenal.h"
#include "level.h"
#include "projectiles.h"
#include "mashup_player.h"
#include "coreplayer.h"
#include "build.h"
#include <memory>
#include <utility>

namespace Duke3d
{
::GameInterface* CreateInterface();
void InitAnimationTables();
void RegisterClasses();
void InitSecondaryDukeProgram(const char*);
void SerializeSecondaryDukeProgram(FSerializer&);
const char* SecondaryDukeProgramFingerprint();
}
namespace Blood { ::GameInterface* CreateInterface(); }
EXTERN_CVAR(Bool, mashup_combat)
CUSTOM_CVAR(Int, superset_weapon_profile, 0, CVAR_ARCHIVE) // 0: character default, 1: Blood, 2: Duke.
{
	if (self < 0 || self > 2) self = clamp(int(self), 0, 2);
}

namespace Superset
{
namespace
{
bool active = false;
bool calebPlayer = false;
bool bloodMap = false;
FString bloodDataPath, dukeDataPath;
constexpr int SaveSchema = 2;
constexpr int RulesRevision = 17;
constexpr const char* SaveSignature = GAMENAME ".Superset";

FString FingerprintPrimaryArchive()
{
	FileReader reader;
	if (!reader.OpenFile(fileSystem.GetResourceFileFullName(1)))
		I_FatalError("Superset: cannot fingerprint primary game archive");
	MD5Context hash;
	uint8_t buffer[16384];
	FileReader::Size length;
	while ((length = reader.Read(buffer, sizeof(buffer))) > 0)
		hash.Update(buffer, unsigned(length));
	uint8_t digest[16];
	hash.Final(digest);
	FString result;
	for (auto byte : digest) result.AppendFormat("%02x", byte);
	return result;
}

class CombinedGameInterface final : public ::GameInterface
{
	std::unique_ptr<::GameInterface> world { bloodMap ? Blood::CreateInterface() : Duke3d::CreateInterface() };
	FString primaryFingerprint;

public:
	const char* Name() override { return "Superset"; }
	FSavegameInfo GetSaveSig() override { return { SaveSignature, SaveSchema, SaveSchema }; }

	void LoadSecondaryResources() override
	{
		world->LoadSecondaryResources();
		if (bloodMap) Mashup::LoadDukeResources(dukeDataPath.GetChars());
		else Mashup::LoadBloodResources(bloodDataPath.GetChars());
		primaryFingerprint = FingerprintPrimaryArchive();
	}

	void InitSecondarySounds() override
	{
		world->InitSecondarySounds();
		Mashup::InitBloodSounds();
		if (bloodMap) Duke3d::InitSecondaryDukeSounds();
	}
	void PrepareSecondarySoundNames() override
	{
		world->PrepareSecondarySoundNames();
		if (bloodMap) Duke3d::PrepareSecondaryDukeSoundNames();
	}

	void app_init() override
	{
		world->app_init();
		InitRuntimeLevel();
		if (const char* weapons = Args->CheckValue("-superset-weapons"))
			superset_weapon_profile = !stricmp(weapons, "blood") ? 1 : !stricmp(weapons, "duke") ? 2 : 0;
		if (bloodMap)
		{
			// Bind native actor classes and their private program without starting Duke's world.
			Duke3d::RegisterClasses();
			Duke3d::InitSecondaryDukeProgram(Args->CheckValue("-dukecon"));
			InitBloodMapDukePickups();
			InitDukeEffects();
		}
		Printf("Superset frontend: independent identity/configuration/saves; %s map adapter.\n", bloodMap ? "Blood" : "Duke");
		Printf("Superset controller: %s. Shared weapon banks and imported entity lifecycle; other inventory components remain native.\n",
			CalebSelected() ? "Caleb (native Blood movement)" : "Duke (native Duke movement)");
	}

	void WriteSavegameMetadata(FSerializer& arc) override
	{
		if (arc.BeginObject("Superset"))
		{
			int rules = RulesRevision, adapter = world->GetSaveSig().currentsavever;
			FString mapRules = bloodMap ? "blood" : "duke";
			FString secondary = bloodMap ? Mashup::DukeResourceFingerprint() : Mashup::BloodResourceFingerprint();
			arc("rules_revision", rules)("map_rules", mapRules)("map_adapter_version", adapter)
				(bloodMap ? "blood_archive_md5" : "duke_archive_md5", primaryFingerprint)
				(bloodMap ? "duke_bank_md5" : "blood_bank_md5", secondary);
			if (bloodMap)
			{
				FString program = Duke3d::SecondaryDukeProgramFingerprint();
				arc("duke_program_md5", program);
			}
			arc.EndObject();
		}
	}

	bool ValidateSavegameMetadata(FSerializer& arc, bool formenu) override
	{
		bool matches = false;
		if (arc.BeginObject("Superset"))
		{
			int rules = -1, adapter = -1;
			FString mapRules, primary, secondary;
			arc("rules_revision", rules)("map_rules", mapRules)("map_adapter_version", adapter)
				(bloodMap ? "blood_archive_md5" : "duke_archive_md5", primary)
				(bloodMap ? "duke_bank_md5" : "blood_bank_md5", secondary);
			matches = rules == RulesRevision && mapRules.Compare(bloodMap ? "blood" : "duke") == 0 &&
				adapter == world->GetSaveSig().currentsavever &&
				primaryFingerprint.IsNotEmpty() && primary == primaryFingerprint &&
				secondary.IsNotEmpty() && secondary.Compare(bloodMap ? Mashup::DukeResourceFingerprint() : Mashup::BloodResourceFingerprint()) == 0;
			if (bloodMap)
			{
				FString program; arc("duke_program_md5", program);
				matches &= program.IsNotEmpty() && program.Compare(Duke3d::SecondaryDukeProgramFingerprint()) == 0;
			}
			arc.EndObject();
		}
		if (!matches && !formenu)
			Printf("Superset: save requires a matching rules revision, map adapter and Duke/Blood content.\n");
		return matches;
	}

	void SerializeGameState(FSerializer& arc) override
	{
		// Character and requested weapon family are independent saved choices.
		// The player also saves its applied family while an action delays handoff.
		if (arc.BeginObject("superset.gamestate"))
		{
			bool pitchforkPrototype = mashup_combat;
			int controller = calebPlayer ? 1 : 0;
			int weapons = clamp(int(superset_weapon_profile), 0, 2);
			arc("pitchfork_prototype", pitchforkPrototype)("player_controller", controller)("weapon_profile", weapons).EndObject();
			if (arc.isReading())
			{
				mashup_combat = pitchforkPrototype;
				calebPlayer = controller == 1;
				superset_weapon_profile = clamp(weapons, 0, 2);
			}
		}
		world->SerializeGameState(arc);
		if (bloodMap)
		{
			SerializeDukeEffects(arc);
			Duke3d::SerializeSecondaryDukeProgram(arc);
		}
		else SerializeBloodControllerState(arc);
		SerializeRuntimeLevel(arc);
	}

	// Bootstrap delegation preserves the native map behavior. Shared gameplay
	// services will replace individual boundaries incrementally. Only this
	// adapter currently advances the world clock; a second full ticker is not run.
	void FinalizeSetup() override { world->FinalizeSetup(); }
	void LoadTextureInfo(TilesetBuildInfo& info) override { world->LoadTextureInfo(info); }
	void SetupSpecialTextures(TilesetBuildInfo& info) override { world->SetupSpecialTextures(info); }
	void loadPalette() override { world->loadPalette(); if (bloodMap) Duke3d::InitAnimationTables(); }
	void FreeLevelData() override
	{
		ClearRuntimeLevel();
		if (bloodMap) { ClearBloodMapDukeWeapons(); ClearDukeEffects(); } else ClearBloodControllerState();
		world->FreeLevelData();
	}
	bool GenerateSavePic() override { return world->GenerateSavePic(); }
	void PlayHudSound() override { world->PlayHudSound(); }
	void MenuOpened() override { world->MenuOpened(); }
	void MenuClosed() override { world->MenuClosed(); }
	void MenuSound(EMenuSounds snd) override { world->MenuSound(snd); }
	bool CanSave() override { return world->CanSave(); }
	bool StartGame(FNewGameStartup& startup) override { return world->StartGame(startup); }
	double SmallFontScale() override { return world->SmallFontScale(); }
	void DrawPlayerSprite(const DVector2& origin, bool onteam) override { world->DrawPlayerSprite(origin, onteam); }
	void SetAmbience(bool on) override { world->SetAmbience(on); }
	void ExitFromMenu() override { world->ExitFromMenu(); }
	void UpdateSounds() override { world->UpdateSounds(); }
	void ErrorCleanup() override { if (bloodMap) ClearDukeEffects(); world->ErrorCleanup(); }
	void Startup() override { world->Startup(); }
	void DrawBackground() override { world->DrawBackground(); }
	void Render() override { world->Render(); }
	void Ticker() override
	{
		int before = PlayClock;
		if (bloodMap) BackupDukeEffects();
		world->Ticker();
		if (bloodMap && PlayClock != before)
		{
			TickDukeEffects();
			TickBloodMapDukePickups();
			TickStatusEffects();
		}
		if (PlayClock != before) TickRuntimeLevel();
		if (bloodMap && PlayClock != before) FinishDukeProjectileStep();
	}
	int GetPlayerChecksum(int pnum) override { return world->GetPlayerChecksum(pnum); }
	const char* CheckCheatMode() override { return world->CheckCheatMode(); }
	const char* GenericCheat(int player, int cheat) override { return world->GenericCheat(player, cheat); }
	void NextLevel(MapRecord* map, int skill) override { ClearRuntimeLevel(); world->NextLevel(map, skill); if (!bloodMap) Mashup::PrepareCombat(PlayerArray[myconnectindex]); ImportRuntimeLevel(bloodMap); }
	void NewGame(MapRecord* map, int skill, bool special) override { ClearRuntimeLevel(); ResetArsenalNewGame(); if (bloodMap) ResetBloodMapDukeWeapons(); world->NewGame(map, skill, special); if (!bloodMap) Mashup::PrepareCombat(PlayerArray[myconnectindex]); ImportRuntimeLevel(bloodMap); }
	void LevelCompleted(MapRecord* map, int skill) override { world->LevelCompleted(map, skill); }
	bool DrawAutomapPlayer(const DVector2& mxy, const DVector2& cpos, DAngle cang,
		const DVector2& xydim, double zoom, double frac) override
		{ return world->DrawAutomapPlayer(mxy, cpos, cang, xydim, zoom, frac); }
	DAngle playerPitchMin() override { return world->playerPitchMin(); }
	DAngle playerPitchMax() override { return world->playerPitchMax(); }
	void ToggleThirdPerson() override { world->ToggleThirdPerson(); }
	void SwitchCoopView() override { world->SwitchCoopView(); }
	void ToggleShowWeapon() override { world->ToggleShowWeapon(); }
	void processSprites(tspriteArray& sprites, const DVector3& view, DAngle angle, double frac) override
	{
		world->processSprites(sprites, view, angle, frac);
		for (unsigned i = 0; i < sprites.Size(); ++i) AnimateRuntimeEntity(sprites.get(i), view.XY(), frac);
	}
	void UpdateCameras(double ratio) override { world->UpdateCameras(ratio); }
	void EnterPortal(DCoreActor* viewer, int type) override { world->EnterPortal(viewer, type); }
	void LeavePortal(DCoreActor* viewer, int type) override { world->LeavePortal(viewer, type); }
	bool GetGeoEffect(GeoEffect* effect, sectortype* viewsector) override { return world->GetGeoEffect(effect, viewsector); }
	int Voxelize(int sprite) override { return world->Voxelize(sprite); }
	void AddExcludedEpisode(const FString& episode) override { world->AddExcludedEpisode(episode); }
	int GetCurrentSkill() override { return world->GetCurrentSkill(); }
	bool IsQAVInterpTypeValid(const FString& type) override { return world->IsQAVInterpTypeValid(type); }
	void AddQAVInterpProps(int id, const FString& type, bool loopable,
		const TMap<int, TArray<int>>&& ignore) override { world->AddQAVInterpProps(id, type, loopable, std::move(ignore)); }
	void RemoveQAVInterpProps(int id) override { world->RemoveQAVInterpProps(id); }
	bool WantEscape() override { return world->WantEscape(); }
	void StartSoundEngine() override { world->StartSoundEngine(); }
	void doPlayerMovement() override
	{
		if (CalebSelected()) ::GameInterface::doPlayerMovement();
		else world->doPlayerMovement();
	}
};
}

void Configure(int mapGameFlags)
{
	active = Args->CheckParm("-superset") != 0;
	if (!active)
	{
		if (Args->CheckParm("-blooddata") || Args->CheckParm("-dukedata")) I_FatalError("Use secondary game data with the -superset frontend.");
		return;
	}
	bloodMap = (mapGameFlags & GAMEFLAG_BLOOD) != 0;
	if ((!bloodMap && !(mapGameFlags & GAMEFLAG_DUKE)) || (mapGameFlags & GAMEFLAG_RRALL))
		I_FatalError("Superset: select Duke Nukem 3D or Blood map data with -iwad.");
	if (Args->CheckParm("-bloodduke"))
		I_FatalError("Superset: use -blooddata <Blood directory>, without the legacy -bloodduke option.");
	const char* path = Args->CheckValue(bloodMap ? "-dukedata" : "-blooddata");
	if (!path) I_FatalError("Superset: specify %s for this map adapter.", bloodMap ? "-dukedata <Duke GRP archive>" : "-blooddata <Blood directory or BLOOD.RFF>");
	if (bloodMap) dukeDataPath = path; else bloodDataPath = path;
	const char* character = Args->CheckValue("-superset-player");
	if (character && stricmp(character, "duke") && stricmp(character, "caleb"))
		I_FatalError("Superset: -superset-player must be duke or caleb.");
	calebPlayer = character ? !stricmp(character, "caleb") : bloodMap;
	if (bloodMap && !calebPlayer) I_FatalError("Superset: Duke in Blood maps needs the reverse character adapter. Select -superset-player caleb for now.");
	const char* weapons = Args->CheckValue("-superset-weapons");
	if (Args->CheckParm("-superset-weapons") && (!weapons || (stricmp(weapons, "auto") && stricmp(weapons, "blood") && stricmp(weapons, "duke"))))
		I_FatalError("Superset: -superset-weapons must be auto, blood or duke.");
}

bool IsActive() { return active; }
bool IsBloodMap() { return active && bloodMap; }
bool IsDukeMap() { return active && !bloodMap; }
bool CalebSelected() { return active && calebPlayer; }
bool BloodWeaponsRequested()
{
	if (int family = RequestedWeaponFamily(); family >= 0) return family != 0;
	int profile = clamp(int(superset_weapon_profile), 0, 2);
	return active && (profile == 1 || (profile == 0 && (calebPlayer || mashup_combat)));
}
::GameInterface* CreateInterface() { return new CombinedGameInterface; }

CCMD(superset_weapons)
{
	if (!IsActive()) return;
	if (argv.argc() == 2)
	{
		if (!stricmp(argv[1], "auto")) superset_weapon_profile = 0;
		else if (!stricmp(argv[1], "blood")) superset_weapon_profile = 1;
		else if (!stricmp(argv[1], "duke")) superset_weapon_profile = 2;
		else { Printf("superset_weapons auto|blood|duke\n"); return; }
		RequestLegacyWeaponFamily(superset_weapon_profile == 1 || (superset_weapon_profile == 0 && (calebPlayer || mashup_combat)));
	}
	Printf("Superset weapon family requested: %s.\n", BloodWeaponsRequested() ? "Blood" : "Duke");
}

CCMD(superset_check)
{
	if (!IsActive())
	{
		Printf("Superset check: requires the -superset frontend.\n");
		return;
	}
	FSerializer writer;
	writer.OpenWriter(false);
	int version = SaveSchema;
	writer("Save Version", version).AddString("Engine", SaveSignature);
	gi->WriteSavegameMetadata(writer);
	unsigned length;
	const char* json = writer.GetOutput(&length);
	auto validates = [](const char* data, size_t size)
	{
		FSerializer reader;
		return reader.OpenReader(data, size) && gi->ValidateSavegameMetadata(reader, true);
	};
	bool manifest = validates(json, length);
	FString changed(json, length);
	changed.Substitute(bloodMap ? Mashup::DukeResourceFingerprint() : Mashup::BloodResourceFingerprint(), "different-secondary-data");
	manifest &= !validates(changed.GetChars(), changed.Len()) && !validates("{}", 2);
	FString otherMap(json, length);
	otherMap.Substitute(bloodMap ? "\"blood\"" : "\"duke\"", bloodMap ? "\"duke\"" : "\"blood\"");
	manifest &= !validates(otherMap.GetChars(), otherMap.Len());
	auto headerResult = [](const FString& data)
	{
		FileReader reader;
		reader.OpenMemory(data.GetChars(), data.Len());
		return G_ValidateSavegame(reader, nullptr, true);
	};
	FString nativeGame(json, length);
	nativeGame.Substitute(SaveSignature, bloodMap ? SAVESIG_BLD : SAVESIG_DN3D);
	bool saveIsolation = headerResult(nativeGame) == 0 && headerResult(changed) == -2;
	// Sixteen 17/16-point hits must lose 17 HP, including every fraction.
	auto health = Units::FromDukeHealth(100);
	for (int i = 0; i < 16; ++i) health -= Units::FromBloodHealth(17);
	bool units = Units::HealthPoints(health) == 83. &&
		Units::FromDukeProjectileSpeed(52.5) == 26.25 && Units::ToDukeProjectileSpeed(26.25) == 52.5 &&
		Units::FromSimulationSteps(30) == Units::ClockRate && Units::Seconds(Units::ClockRate) == 1.;
	bool identity = currentGame.Compare("Superset") == 0 && strcmp(gi->Name(), "Superset") == 0 &&
		strcmp(gi->GetSaveSig().savesig, SaveSignature) == 0 && (bloodMap ? Mashup::HasDukeResources() : Mashup::HasBloodResources());
	Printf("Superset check: identity/config=%s, content manifest=%s, save isolation=%s, shared units=%s.\n",
		identity ? "PASS" : "FAIL", manifest ? "PASS" : "FAIL", saveIsolation ? "PASS" : "FAIL", units ? "PASS" : "FAIL");
}
}
