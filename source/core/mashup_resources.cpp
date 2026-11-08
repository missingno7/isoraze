#include "mashup_resources.h"
#include "resourcefile.h"
#include "texturemanager.h"
#include "image.h"
#include "bitmap.h"
#include "tilesetbuilder.h"
#include "buildtiles.h"
#include "palettecontainer.h"
#include "gamecontrol.h"
#include "palette.h"
#include "m_argv.h"
#include "m_swap.h"
#include "m_crc32.h"
#include "cmdlib.h"
#include "c_dispatch.h"
#include "v_draw.h"
#include "raze_sound.h"
#include "md5.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace Mashup
{
namespace
{
struct ArtTile
{
	const uint8_t* pixels = nullptr;
	unsigned width = 0, height = 0;
	uint32_t animation = 0;
};

struct BloodResourceBank
{
	std::unique_ptr<FileSys::FResourceFile> directory, resources, sounds;
	// FBuildTexture borrows pixel memory. Keep the buffers at stable addresses.
	std::vector<std::unique_ptr<FileSys::FileData>> artFiles;
	TArray<ArtTile> tiles;
	FRemapTable imagePalette;
	int translations[5][15] {};
	int firstTexture = 0;
	int primaryFirst = firstarttile, primaryLast = maxarttile;
	int primaryGame = g_gameType, primaryShades = numshades;
	int primaryLumps = fileSystem.GetNumEntries();
	TMap<int, FSoundID> soundIds;
	TArray<FString> soundSamples;
	uint32_t primaryPaletteCRC = 0;
	FString fingerprint;

	void BuildFingerprint()
	{
		// Hash the resolved resource names and bytes, not installation paths.
		// Archive entries also name the loose overrides that this bank accepts.
		// Include archive headers/resource-ID tables as well as resolved bytes.
		// SEQ/QAV lookup uses IDs directly, while art/palettes/sounds can resolve
		// loose files by name. Both kinds of dependency must affect the digest.
		std::vector<std::string> names { "blood.rff", "sounds.rff", "surface.dat", "shade.dat" };
		for (auto archive : { resources.get(), sounds.get() })
			for (int i = 0; i < archive->EntryCount(); ++i)
				names.emplace_back(archive->getName(i));
		for (unsigned i = 0; i < 128; ++i)
		{
			FStringf name("tiles%03u.art", i);
			if (directory->FindEntry(name.GetChars()) >= 0) names.emplace_back(name.GetChars());
		}
		std::sort(names.begin(), names.end());
		names.erase(std::unique(names.begin(), names.end()), names.end());
		MD5Context hash;
		for (const auto& name : names)
		{
			auto data = Read(name.c_str());
			hash.Update(reinterpret_cast<const uint8_t*>(name.c_str()), unsigned(name.size() + 1));
			uint8_t size[8];
			for (unsigned i = 0; i < 8; ++i) size[i] = uint8_t(uint64_t(data.size()) >> (i * 8));
			hash.Update(size, sizeof(size));
			hash.Update(data.bytes(), unsigned(data.size()));
		}
		uint8_t digest[16];
		hash.Final(digest);
		for (auto byte : digest) fingerprint.AppendFormat("%02x", byte);
	}

	FileSys::FileData Read(const char* name)
	{
		int entry = directory->FindEntry(name);
		if (entry >= 0) return directory->Read(entry);
		entry = sounds->FindEntry(name);
		if (entry >= 0) return sounds->Read(entry);
		entry = resources->FindEntry(name);
		return entry >= 0 ? resources->Read(entry) : FileSys::FileData();
	}

	std::unique_ptr<FileSys::FResourceFile> OpenArchive(const char* name)
	{
		int entry = directory->FindEntry(name);
		if (entry < 0) I_FatalError("Blood/Duke: missing %s", name);
		auto reader = directory->GetEntryReader(entry);
		return std::unique_ptr<FileSys::FResourceFile>(FileSys::FResourceFile::OpenResourceFile(name, reader, true));
	}

	void ReadArt(FileSys::FileData data)
	{
		auto word = [&](size_t offset) { uint16_t v; memcpy(&v, data.bytes() + offset, 2); return LittleShort(v); };
		auto dword = [&](size_t offset) { uint32_t v; memcpy(&v, data.bytes() + offset, 4); return LittleLong(v); };
		if (data.size() < 16 || dword(0) != 1) I_FatalError("Blood/Duke: invalid ART header");
		unsigned first = dword(8), last = dword(12);
		if (first > last || last >= MAXUSERTILES) I_FatalError("Blood/Duke: invalid ART tile range");
		size_t count = last - first + 1, offset = 16 + count * 8;
		if (offset > data.size()) I_FatalError("Blood/Duke: truncated ART metadata");
		if (tiles.Size() <= last) tiles.Resize(last + 1);
		for (unsigned i = 0; i < count; ++i)
		{
			unsigned width = word(16 + i * 2), height = word(16 + count * 2 + i * 2);
			size_t size = size_t(width) * height;
			if (size > data.size() - offset) I_FatalError("Blood/Duke: truncated ART pixels");
			tiles[first + i] = { size ? data.bytes() + offset : nullptr, width, height, dword(16 + count * 4 + i * 4) };
			offset += size;
		}
		auto buffer = std::make_unique<FileSys::FileData>();
		*buffer = std::move(data);
		artFiles.push_back(std::move(buffer));
	}

	void LoadPalettes()
	{
		const char* palettes[] = { "blood.pal", "water.pal", "beast.pal", "sewer.pal", "invuln1.pal" };
		const char* lookups[] = { "normal.plu", "saturate.plu", "beast.plu", "tommy.plu", "spider3.plu",
			"gray.plu", "grayish.plu", "spider1.plu", "spider2.plu", "flame.plu", "cold.plu", "p1.plu", "p2.plu", "p3.plu", "p4.plu" };
		for (unsigned p = 0; p < 5; ++p)
		{
			auto palette = Read(palettes[p]);
			if (palette.size() < 768) I_FatalError("Blood/Duke: missing or truncated %s", palettes[p]);
			for (unsigned i = 0; i < 256; ++i)
			{
				if (p == 0)
				{
					imagePalette.Palette[i] = i == 255 ? PalEntry(0) : PalEntry(255, palette.bytes()[i * 3], palette.bytes()[i * 3 + 1], palette.bytes()[i * 3 + 2]);
					imagePalette.Remap[i] = i == 0 ? 255 : i == 255 ? 0 : i;
				}
			}
			for (unsigned l = 0; l < 15; ++l)
			{
				auto lookup = Read(lookups[l]);
				if (lookup.size() != 64 * 256) I_FatalError("Blood/Duke: missing or invalid %s", lookups[l]);
				FRemapTable remap;
				for (unsigned i = 0; i < 256; ++i)
				{
					unsigned raw = i == 0 ? 255 : i == 255 ? 0 : i;
					unsigned color = lookup.bytes()[raw];
					remap.Remap[i] = i;
					remap.Palette[i] = i == 0 ? PalEntry(0) : PalEntry(255, palette.bytes()[color * 3], palette.bytes()[color * 3 + 1], palette.bytes()[color * 3 + 2]);
				}
				translations[p][l] = GPalette.StoreTranslation(TRANSLATION_Internal, &remap).index();
			}
		}
	}

	void Load(const char* path)
	{
		primaryPaletteCRC = Bcrc32(GPalette.BaseColors, sizeof(GPalette.BaseColors), 0);
		bool isDirectory = false;
		if (!DirEntryExists(path, &isDirectory)) I_FatalError("Blood/Duke: data path does not exist: %s", path);
		FString root = isDirectory ? FString(path) : ExtractFilePath(path);
		directory.reset(FileSys::FResourceFile::OpenDirectory(root.GetChars()));
		if (!directory) I_FatalError("Blood/Duke: cannot open data directory: %s", root.GetChars());
		resources = OpenArchive("blood.rff");
		sounds = OpenArchive("sounds.rff");
		if (!resources || !sounds) I_FatalError("Blood/Duke: cannot open Blood RFF archives");
		BuildFingerprint();
		LoadPalettes();
		for (unsigned i = 0; i < 128; ++i)
		{
			FStringf name("tiles%03u.art", i);
			int entry = directory->FindEntry(name.GetChars());
			if (entry >= 0) ReadArt(directory->Read(entry));
		}
		if (tiles.Size() <= 3870 || !tiles[3870].pixels) I_FatalError("Blood/Duke: missing Caleb ART tiles");
		firstTexture = TexMan.NumTextures();
		auto surface = Read("surface.dat"), shade = Read("shade.dat");
		for (unsigned i = 0; i < tiles.Size(); ++i)
		{
			auto& tile = tiles[i];
			FStringf name("mashup/blood/tile%05u", i);
			FGameTexture* texture;
			if (!tile.pixels) texture = MakeGameTexture(TexMan.GetGameTexture(FNullTextureID())->GetTexture(), name.GetChars(), ETextureType::Null);
			else
			{
				int left = int8_t(tile.animation >> 8), top = int8_t(tile.animation >> 16);
				auto image = new FBuildTexture("mashup/blood/", i, tile.pixels, &imagePalette, tile.width, tile.height, left, top);
				texture = MakeGameTexture(new FImageTexture(image), name.GetChars(), ETextureType::Any);
				texture->SetOffsets(left, top);
			}
			auto id = TexMan.AddGameTexture(texture, true);
			if (id.GetIndex() != firstTexture + int(i)) I_FatalError("Blood/Duke: nonconsecutive texture IDs");
			auto& info = AccessExtInfo(id);
			info = {};
			info.tiletovox = -1;
			info.picanm = tileConvertAnimFormat(tile.animation);
			if (i < surface.size()) info.surftype = surface.bytes()[i];
			if (i < shade.size()) info.tileshade = shade.bytes()[i];
			texture->SetDisableFullbright(true); // Duke's fullbright palette does not describe Blood indices.
		}
		Printf("Blood/Duke resource bank: %u tiles, 5 palettes, 15 lookups; Duke world remains authoritative.\n", tiles.Size());
	}
};

std::unique_ptr<BloodResourceBank> bank;
}

CVAR(Bool, mashup_resource_preview, false, 0)

void InitBloodResources()
{
	if (!Args->CheckParm("-bloodduke")) return;
	const char* path = Args->CheckValue("-bloodduke");
	if (!path || !isDuke() || isRR()) I_FatalError("Use -bloodduke <Blood data directory> with Duke Nukem 3D.");
	LoadBloodResources(path);
}

void LoadBloodResources(const char* path)
{
	if (bank) I_FatalError("Blood/Duke: resource bank already initialized");
	bank = std::make_unique<BloodResourceBank>();
	bank->Load(path);
}

const char* BloodResourceFingerprint() { return bank ? bank->fingerprint.GetChars() : ""; }

bool HasBloodResources() { return bank != nullptr; }

void InitBloodSounds()
{
	if (!bank) return;
	for (auto archive : { bank->resources.get(), bank->sounds.get() })
		for (int i = 0; i < archive->EntryCount(); ++i)
		{
			const char* dot = strrchr(archive->getName(i), '.');
			if (!dot || stricmp(dot + 1, "sfx")) continue;
			auto data = archive->Read(i);
			if (data.size() < 21) I_FatalError("Blood/Duke: truncated SFX metadata: %s", archive->getName(i));
			auto integer = [&](size_t offset) { int32_t value; memcpy(&value, data.bytes() + offset, 4); return LittleLong(value); };
			char sample[9] {};
			// RFF SFX records store the raw name as a variable-length string.
			memcpy(sample, data.bytes() + 20, min(size_t(8), data.size() - 20));
			FStringf name("%s.raw", sample);
			auto raw = bank->Read(name.GetChars());
			if (!raw.size()) continue;
			int resource = archive->GetEntryResourceID(i);
			FStringf logical("mashup/blood/sfx%08d", resource);
			// Negative synthetic lump IDs are private to ReadBloodSound. They never
			// enter the mounted filesystem or Duke's resource-ID sound namespace.
			int lump = INT_MIN + int(bank->soundSamples.Size());
			bank->soundSamples.Push(name);
			auto id = soundEngine->AddSoundLump(logical.GetChars(), lump, 0, -1, 6);
			auto sound = soundEngine->GetWritableSfx(id);
			int format = integer(12);
			sound->RawRate = format >= 9 && format <= 12 ? 44100 : format >= 5 && format <= 8 ? 22050 : 11025;
			sound->bLoadRAW = sound->bExternal = true;
			sound->LoopStart = integer(16);
			int pitch = integer(4), volume = integer(0);
			if (pitch != 65536) sound->DefPitch = pitch / 65536.f;
			if (volume != 80) sound->UserVal = volume;
			sound->Rolloff.RolloffType = ROLLOFF_Doom;
			sound->Rolloff.MinDistance = 170;
			sound->Rolloff.MaxDistance = 850;
			bank->soundIds.Insert(resource, id);
		}
	soundEngine->HashSounds();
	Printf("Blood/Duke sound bank: %u sample definitions registered in a separate namespace.\n", bank->soundSamples.Size());
}

FSoundID BloodSound(int id)
{
	if (!bank) return soundEngine->FindSoundByResID(id);
	auto sound = bank->soundIds.CheckKey(id);
	return sound ? *sound : NO_SOUND;
}

bool ReadBloodSound(int lump, TArray<uint8_t>& output)
{
	if (!bank) return false;
	unsigned index = unsigned(lump) - unsigned(INT_MIN);
	if (index >= bank->soundSamples.Size()) return false;
	auto data = bank->Read(bank->soundSamples[index].GetChars());
	output.Resize(unsigned(data.size()));
	if (data.size()) memcpy(output.Data(), data.data(), data.size());
	return true;
}

FileReader OpenBloodResource(int id, const char* extension)
{
	if (!bank)
	{
		int entry = fileSystem.FindResource(id, extension);
		return entry < 0 ? FileReader() : fileSystem.OpenFileReader(entry);
	}
	for (auto archive : { bank->resources.get(), bank->sounds.get() })
		for (int i = archive->EntryCount() - 1; i >= 0; --i)
		{
			const char* dot = strrchr(archive->getName(i), '.');
			if (archive->GetEntryResourceID(i) == id && dot && !stricmp(dot + 1, extension))
			{
				// Use the already opened container, including native RFF decryption.
				// A READER_NEW would try reopening its logical name in the process cwd.
				auto data = archive->Read(i);
				FileReader reader;
				reader.OpenMemoryArray(data);
				return reader;
			}
		}
	return FileReader();
}

FTextureID BloodTile(int tile)
{
	if (!bank) return tileGetTextureID(tile);
	if (unsigned(tile) < bank->tiles.Size()) return FSetTextureID(bank->firstTexture + tile);
	return FNullTextureID();
}

bool IsBloodTexture(FTextureID texture)
{
	return bank && unsigned(texture.GetIndex() - bank->firstTexture) < bank->tiles.Size();
}

bool PickBloodTexture(FGameTexture* texture, int translation, TexturePick& pick)
{
	if (!IsBloodTexture(texture->GetID())) return false;
	bool ownTranslation = false;
	for (auto& palette : bank->translations)
		for (int entry : palette) ownTranslation |= entry == translation;
	pick.texture = texture;
	pick.tintFlags = -1;
	pick.tintColor = pick.basepalTint = 0xffffff;
	if (ownTranslation || IsLuminosityTranslation(translation))
	{
		pick.translation = translation;
		return true;
	}
	int palette = translation == 0 ? 0 : GetTranslationType(translation) - Translation_Remap;
	int lookup = GetTranslationIndex(translation);
	if (unsigned(palette) >= 5) palette = 0;
	if (unsigned(lookup) >= 15) lookup = 0;
	pick.translation = bank->translations[palette][lookup];
	return true;
}

bool CheckBloodResources()
{
	if (!bank) return false;
	// CON compilation runs after texture loading and identifies Plutonium Pak,
	// World Tour or shareware from the script. These are legitimate Duke changes.
	constexpr int gameMask = ~(GAMEFLAG_PLUTOPAK | GAMEFLAG_WORLDTOUR | GAMEFLAG_SHAREWARE);
	bool isolated = firstarttile == bank->primaryFirst && maxarttile == bank->primaryLast &&
		(g_gameType & gameMask) == (bank->primaryGame & gameMask) && numshades == bank->primaryShades && fileSystem.GetNumEntries() == bank->primaryLumps &&
		Bcrc32(GPalette.BaseColors, sizeof(GPalette.BaseColors), 0) == bank->primaryPaletteCRC;
	if (!isolated) Printf("Blood/Duke isolation detail: tiles %d/%d -> %d/%d; game %d -> %d; shades %d -> %d; lumps %d -> %d; palette %u -> %u.\n",
		bank->primaryFirst, bank->primaryLast, firstarttile, maxarttile, bank->primaryGame, g_gameType,
		bank->primaryShades, numshades, bank->primaryLumps, fileSystem.GetNumEntries(), bank->primaryPaletteCRC,
		Bcrc32(GPalette.BaseColors, sizeof(GPalette.BaseColors), 0));
	auto texture = TexMan.GetGameTexture(BloodTile(3870));
	spritetype sprite {};
	sprite.setspritetexture(BloodTile(3870));
	bool colors = true;
	TexturePick luminosityPick;
	int luminosity = MakeLuminosityTranslation(0, 0, 255).index();
	if (!PickBloodTexture(texture, luminosity, luminosityPick) || luminosityPick.translation != luminosity) return false;
	auto& tile = bank->tiles[3870];
	// Verify the native RGBA decoder against the source palette for every pixel,
	// including Blood's opaque index 0 and transparent index 255.
	for (int p = 0; p < 5; ++p)
		for (int l = 0; l < 15; ++l)
		{
			int translation = bank->translations[p][l];
			TexturePick pick;
			if (!PickBloodTexture(texture, TRANSLATION(Translation_Remap + p, l).index(), pick) || pick.translation != translation)
			{
				Printf("Blood/Duke palette selection mismatch: palette=%d lookup=%d expected=%d actual=%d.\n", p, l, translation, pick.translation);
				return false;
			}
			if (!PickBloodTexture(texture, translation, pick) || pick.translation != translation) return false;
			auto remap = GPalette.GetTranslation(GetTranslationType(translation), GetTranslationIndex(translation));
			if (!remap) return false;
			auto bitmap = texture->GetTexture()->GetBgraBitmap(remap->Palette);
			for (unsigned x = 0; x < tile.width; ++x)
				for (unsigned y = 0; y < tile.height; ++y)
				{
					unsigned raw = tile.pixels[x * tile.height + y];
					unsigned index = raw == 255 ? 0 : raw == 0 ? 255 : raw;
					auto pixel = bitmap.GetPixels() + y * bitmap.GetPitch() + x * 4;
					colors &= !memcmp(pixel, &remap->Palette[index], 4);
				}
		}
	return isolated && colors && sprite.spritetexture() == BloodTile(3870) &&
		BloodTile(3870) != tileGetTextureID(3870) && texture->GetTexelHeight() == 117 &&
		OpenBloodResource(12032, "SEQ").isOpen() && OpenBloodResource(1, "QAV").isOpen();
}

void DrawResourcePreview()
{
	if (!bank || !mashup_resource_preview) return;
	DrawTexture(twod, TexMan.GetGameTexture(BloodTile(3870)), 12, 12,
		DTA_ScaleX, .6, DTA_ScaleY, .6, DTA_TopLeft, true,
		DTA_TranslationIndex, bank->translations[0][0], DTA_FullscreenScale, FSMode_Fit320x200, TAG_DONE);
	extern void DrawWeaponResourcePreview();
	DrawWeaponResourcePreview();
}
}
