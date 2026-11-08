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
#include <memory>
#include <utility>

namespace Duke3d { ::GameInterface* CreateInterface(); }
EXTERN_CVAR(Bool, mashup_combat)

namespace Superset
{
namespace
{
bool active = false;
bool calebPlayer = false;
FString bloodDataPath;
constexpr int SaveSchema = 2;
constexpr int RulesRevision = 5;
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
	std::unique_ptr<::GameInterface> world { Duke3d::CreateInterface() };
	FString primaryFingerprint;

public:
	const char* Name() override { return "Superset"; }
	FSavegameInfo GetSaveSig() override { return { SaveSignature, SaveSchema, SaveSchema }; }

	void LoadSecondaryResources() override
	{
		world->LoadSecondaryResources();
		Mashup::LoadBloodResources(bloodDataPath.GetChars());
		primaryFingerprint = FingerprintPrimaryArchive();
	}

	void InitSecondarySounds() override
	{
		world->InitSecondarySounds();
		Mashup::InitBloodSounds();
	}

	void app_init() override
	{
		world->app_init();
		Printf("Superset frontend: independent identity/configuration/saves; Duke map adapter.\n");
		Printf("Superset controller: %s. Mixed inventory and full damage normalization remain pending.\n",
			CalebSelected() ? "Caleb (native Blood movement)" : "Duke (native Duke movement)");
	}

	void WriteSavegameMetadata(FSerializer& arc) override
	{
		if (arc.BeginObject("Superset"))
		{
			int rules = RulesRevision, adapter = world->GetSaveSig().currentsavever;
			FString mapRules = "duke", blood = Mashup::BloodResourceFingerprint();
			arc("rules_revision", rules)("map_rules", mapRules)("map_adapter_version", adapter)
				("duke_archive_md5", primaryFingerprint)("blood_bank_md5", blood).EndObject();
		}
	}

	bool ValidateSavegameMetadata(FSerializer& arc, bool formenu) override
	{
		bool matches = false;
		if (arc.BeginObject("Superset"))
		{
			int rules = -1, adapter = -1;
			FString mapRules, duke, blood;
			arc("rules_revision", rules)("map_rules", mapRules)("map_adapter_version", adapter)
				("duke_archive_md5", duke)("blood_bank_md5", blood).EndObject();
			matches = rules == RulesRevision && mapRules.Compare("duke") == 0 &&
				adapter == world->GetSaveSig().currentsavever &&
				primaryFingerprint.IsNotEmpty() && duke == primaryFingerprint &&
				blood.IsNotEmpty() && blood.Compare(Mashup::BloodResourceFingerprint()) == 0;
		}
		if (!matches && !formenu)
			Printf("Superset: save requires a matching rules revision, map adapter and Duke/Blood content.\n");
		return matches;
	}

	void SerializeGameState(FSerializer& arc) override
	{
		// Keep this optional experiment reproducible across a save/load. The
		// controller and inventory selections will replace it as they land.
		if (arc.BeginObject("superset.gamestate"))
		{
			bool pitchforkPrototype = mashup_combat;
			int controller = calebPlayer ? 1 : 0;
			arc("pitchfork_prototype", pitchforkPrototype)("player_controller", controller).EndObject();
			if (arc.isReading())
			{
				mashup_combat = pitchforkPrototype;
				calebPlayer = controller == 1;
			}
		}
		world->SerializeGameState(arc);
		SerializeBloodControllerState(arc);
	}

	// Bootstrap delegation preserves the native map behavior. Shared gameplay
	// services will replace individual boundaries incrementally. Only this
	// adapter currently advances the world clock; a second full ticker is not run.
	void FinalizeSetup() override { world->FinalizeSetup(); }
	void LoadTextureInfo(TilesetBuildInfo& info) override { world->LoadTextureInfo(info); }
	void SetupSpecialTextures(TilesetBuildInfo& info) override { world->SetupSpecialTextures(info); }
	void loadPalette() override { world->loadPalette(); }
	void FreeLevelData() override { ClearBloodControllerState(); world->FreeLevelData(); }
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
	void ErrorCleanup() override { world->ErrorCleanup(); }
	void Startup() override { world->Startup(); }
	void DrawBackground() override { world->DrawBackground(); }
	void Render() override { world->Render(); }
	void Ticker() override { world->Ticker(); }
	int GetPlayerChecksum(int pnum) override { return world->GetPlayerChecksum(pnum); }
	const char* CheckCheatMode() override { return world->CheckCheatMode(); }
	const char* GenericCheat(int player, int cheat) override { return world->GenericCheat(player, cheat); }
	void NextLevel(MapRecord* map, int skill) override { world->NextLevel(map, skill); }
	void NewGame(MapRecord* map, int skill, bool special) override { world->NewGame(map, skill, special); }
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
		{ world->processSprites(sprites, view, angle, frac); }
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
		if (Args->CheckParm("-blooddata")) I_FatalError("Use -blooddata with the -superset frontend.");
		return;
	}
	if (!(mapGameFlags & GAMEFLAG_DUKE) || (mapGameFlags & GAMEFLAG_RRALL))
		I_FatalError("Superset: the bootstrap currently requires Duke Nukem 3D map data (-iwad duke3d.grp).");
	if (Args->CheckParm("-bloodduke"))
		I_FatalError("Superset: use -blooddata <Blood directory>, without the legacy -bloodduke option.");
	const char* path = Args->CheckValue("-blooddata");
	if (!path) I_FatalError("Superset: specify -blooddata <Blood data directory or BLOOD.RFF>.");
	bloodDataPath = path;
	const char* character = Args->CheckValue("-superset-player");
	if (character && stricmp(character, "duke") && stricmp(character, "caleb"))
		I_FatalError("Superset: -superset-player must be duke or caleb.");
	calebPlayer = character && !stricmp(character, "caleb");
}

bool IsActive() { return active; }
bool CalebSelected() { return active && calebPlayer; }
::GameInterface* CreateInterface() { return new CombinedGameInterface; }

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
	changed.Substitute(Mashup::BloodResourceFingerprint(), "different-blood-data");
	manifest &= !validates(changed.GetChars(), changed.Len()) && !validates("{}", 2);
	auto headerResult = [](const FString& data)
	{
		FileReader reader;
		reader.OpenMemory(data.GetChars(), data.Len());
		return G_ValidateSavegame(reader, nullptr, true);
	};
	FString nativeGame(json, length);
	nativeGame.Substitute(SaveSignature, SAVESIG_DN3D);
	bool saveIsolation = headerResult(nativeGame) == 0 && headerResult(changed) == -2;
	// Sixteen 17/16-point hits must lose 17 HP, including every fraction.
	auto health = Units::FromDukeHealth(100);
	for (int i = 0; i < 16; ++i) health -= Units::FromBloodHealth(17);
	bool units = Units::HealthPoints(health) == 83. &&
		Units::FromSimulationSteps(30) == Units::ClockRate && Units::Seconds(Units::ClockRate) == 1.;
	bool identity = currentGame.Compare("Superset") == 0 && strcmp(gi->Name(), "Superset") == 0 &&
		strcmp(gi->GetSaveSig().savesig, SaveSignature) == 0 && Mashup::HasBloodResources();
	Printf("Superset check: identity/config=%s, content manifest=%s, save isolation=%s, shared units=%s.\n",
		identity ? "PASS" : "FAIL", manifest ? "PASS" : "FAIL", saveIsolation ? "PASS" : "FAIL", units ? "PASS" : "FAIL");
}
}
