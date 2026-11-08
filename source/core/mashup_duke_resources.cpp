#include "mashup_resources.h"
#include "mashup_art.h"
#include "texturemanager.h"
#include "image.h"
#include "bitmap.h"
#include "tilesetbuilder.h"
#include "palettecontainer.h"
#include "palette.h"
#include "gamecontrol.h"
#include "sc_man.h"
#include "md5.h"
#include "m_crc32.h"

namespace Mashup
{
namespace
{
constexpr const char* Prefix = "mashup/duke/";
struct DukeResourceBank : PrivateArtBank
{
	std::unique_ptr<FileSys::FResourceFile> archive;
	FRemapTable imagePalette;
	int firstTexture = 0, translations[7][256] {};
	int primaryFirst = firstarttile, primaryLast = maxarttile;
	int primaryGame = g_gameType, primaryShades = numshades;
	int primaryLumps = fileSystem.GetNumEntries();
	uint32_t primaryPaletteCRC = Bcrc32(GPalette.BaseColors, sizeof(GPalette.BaseColors), 0);
	FString fingerprint;
	FString soundDefinitions;

	FileSys::FileData Read(const char* name)
	{
		int entry = archive->FindEntry(name);
		return entry < 0 ? FileSys::FileData() : archive->Read(entry);
	}

	void LoadPalettes()
	{
		auto palette = Read("palette.dat"), lookup = Read("lookup.dat");
		if (palette.size() < 770 + 256 || lookup.size() < 1)
			I_FatalError("Superset: Duke palette.dat/lookup.dat is missing or truncated.");
		unsigned shades = palette.bytes()[768] | (palette.bytes()[769] << 8);
		if (shades < 2 || palette.size() < 770 + shades * 256)
			I_FatalError("Superset: invalid Duke shade table.");
		unsigned count = lookup.bytes()[0];
		size_t offset = 1 + count * 257;
		if (lookup.size() < offset + 5 * 768)
			I_FatalError("Superset: truncated Duke lookup tables/base palettes.");
		uint8_t remaps[256][256];
		bool present[256] {};
		present[0] = true;
		for (unsigned p = 0; p < 256; ++p)
			for (unsigned i = 0; i < 256; ++i) remaps[p][i] = uint8_t(i);
		for (unsigned n = 0; n < count; ++n)
		{
			unsigned p = lookup.bytes()[1 + n * 257];
			present[p] = true;
			memcpy(remaps[p], lookup.bytes() + 2 + n * 257, 256);
		}
		PalEntry palettes[7][256];
		for (unsigned p = 0; p < 7; ++p)
			for (unsigned i = 0; i < 256; ++i)
			{
				auto bytes = p == 0 || p == 6 ? palette.bytes() : lookup.bytes() + offset + (p - 1) * 768;
				unsigned r = p == 6 ? 2 : 0, b = p == 6 ? 0 : 2;
				palettes[p][i] = PalEntry(255, uint8_t(bytes[i * 3 + r] << 2), uint8_t(bytes[i * 3 + 1] << 2), uint8_t(bytes[i * 3 + b] << 2));
				if (p == 0)
				{
					imagePalette.Palette[i] = i == 255 ? PalEntry(0) : palettes[0][i];
					imagePalette.Remap[i] = i == 0 ? 255 : i == 255 ? 0 : i;
				}
			}
		for (unsigned p = 0; p < 7; ++p)
		{
			for (unsigned l = 0; l < 256; ++l)
			{
				if (!present[l]) { translations[p][l] = translations[p][0]; continue; }
				FRemapTable remap;
				for (unsigned i = 0; i < 256; ++i)
				{
					unsigned raw = i == 0 ? 255 : i == 255 ? 0 : i;
					unsigned color = palette.bytes()[770 + remaps[l][raw]];
					remap.Remap[i] = uint8_t(i);
					remap.Palette[i] = i == 0 ? PalEntry(0) : palettes[p][color];
				}
				translations[p][l] = GPalette.StoreTranslation(TRANSLATION_Internal, &remap).index();
			}
		}
	}

	void Load(const char* path)
	{
		FileReader input;
		if (!input.OpenFile(path)) I_FatalError("Superset: cannot open Duke archive %s", path);
		MD5Context hash;
		uint8_t buffer[16384];
		FileReader::Size length;
		while ((length = input.Read(buffer, sizeof(buffer))) > 0) hash.Update(buffer, unsigned(length));
		input.Seek(0, FileReader::SeekSet);
		archive.reset(FileSys::FResourceFile::OpenResourceFile(path, input, true));
		if (!archive) I_FatalError("Superset: unsupported Duke archive %s", path);
		LoadPalettes();
		for (unsigned i = 0; i < 128; ++i)
		{
			FStringf name("tiles%03u.art", i);
			int entry = archive->FindEntry(name.GetChars());
			if (entry >= 0) ReadArt(archive->Read(entry));
		}
		if (tiles.Size() <= 2299 || !tiles[2296].pixels)
			I_FatalError("Superset: Duke ART tiles are missing.");
		firstTexture = TexMan.NumTextures();
		for (unsigned i = 0; i < tiles.Size(); ++i)
		{
			auto& tile = tiles[i];
			FStringf name("%stile%05u", Prefix, i);
			FGameTexture* texture;
			if (!tile.pixels) texture = MakeGameTexture(TexMan.GetGameTexture(FNullTextureID())->GetTexture(), name.GetChars(), ETextureType::Null);
			else
			{
				int left = int8_t(tile.animation >> 8), top = int8_t(tile.animation >> 16);
				auto image = new FBuildTexture(Prefix, i, tile.pixels, &imagePalette, tile.width, tile.height, left, top);
				texture = MakeGameTexture(new FImageTexture(image), name.GetChars(), ETextureType::Any);
				texture->SetOffsets(left, top);
			}
			auto id = TexMan.AddGameTexture(texture, true);
			if (id.GetIndex() != firstTexture + int(i)) I_FatalError("Superset: nonconsecutive Duke texture IDs.");
			auto& info = AccessExtInfo(id);
			info = {};
			info.tiletovox = -1;
			info.picanm = tileConvertAnimFormat(tile.animation);
			texture->SetDisableFullbright(true); // Blood's indexed fullbright mask is not Duke's.
		}
		// Read the engine's original alias definitions without installing Duke's
		// filter into Blood's filesystem, and use the same native alias parser.
		FileReader engineReader;
		if (!engineReader.OpenFile(fileSystem.GetResourceFileFullName(0))) I_FatalError("Superset: cannot open engine resource archive.");
		std::unique_ptr<FileSys::FResourceFile> engine(FileSys::FResourceFile::OpenResourceFile("raze.pk3", engineReader, true));
		int entry = engine ? engine->FindEntry("filter/dukelike/texnames.txt") : -1;
		if (entry < 0) I_FatalError("Superset: engine Duke texture names are missing.");
		auto aliases = engine->Read(entry);
		hash.Update(aliases.bytes(), unsigned(aliases.size()));
		FScanner scanner;
		scanner.OpenMem("Superset Duke TEXNAMES", reinterpret_cast<const char*>(aliases.bytes()), int(aliases.size()));
		LoadTextureAliases(scanner, firstTexture, int(tiles.Size()) - 1, Prefix);
		entry = engine->FindEntry("filter/dukelike/sndinfo.txt");
		if (entry < 0) I_FatalError("Superset: engine Duke sound names are missing.");
		auto sounds = engine->Read(entry);
		soundDefinitions = FString(reinterpret_cast<const char*>(sounds.data()), sounds.size());
		hash.Update(sounds.bytes(), unsigned(sounds.size()));
		uint8_t digest[16];
		hash.Final(digest);
		for (auto byte : digest) fingerprint.AppendFormat("%02x", byte);
		Printf("Superset Duke resource bank: %u private tiles; Blood map resources unchanged.\n", tiles.Size());
	}
};
std::unique_ptr<DukeResourceBank> bank;
}

void LoadDukeResources(const char* path)
{
	if (bank) I_FatalError("Superset: Duke resource bank already initialized.");
	bank = std::make_unique<DukeResourceBank>();
	bank->Load(path);
}
bool HasDukeResources() { return bank != nullptr; }
FString DukeSoundDefinitions() { return bank ? bank->soundDefinitions : FString(); }
const char* DukeResourceFingerprint() { return bank ? bank->fingerprint.GetChars() : ""; }
FileReader OpenDukeResource(const char* name)
{
	FileReader reader;
	if (bank)
	{
		auto data = bank->Read(name);
		if (data.size()) reader.OpenMemoryArray(data);
	}
	return reader;
}
FTextureID DukeTile(int tile)
{
	if (bank && unsigned(tile) < bank->tiles.Size()) return FSetTextureID(bank->firstTexture + tile);
	return FNullTextureID();
}
FTextureID DukeTexture(const char* name)
{
	FString qualified = Prefix;
	qualified += name;
	return bank ? TexMan.CheckForTexture(qualified.GetChars(), ETextureType::Any) : FNullTextureID();
}
bool IsDukeTexture(FTextureID texture)
{
	return bank && unsigned(texture.GetIndex() - bank->firstTexture) < bank->tiles.Size();
}
bool PickDukeTexture(FGameTexture* texture, int translation, TexturePick& pick)
{
	if (!IsDukeTexture(texture->GetID())) return false;
	pick.texture = texture;
	pick.tintFlags = -1;
	pick.tintColor = pick.basepalTint = 0xffffff;
	bool own = false;
	for (auto& palette : bank->translations)
		for (int entry : palette) own |= entry == translation;
	if (own || IsLuminosityTranslation(translation)) { pick.translation = translation; return true; }
	int base = translation == 0 ? 0 : GetTranslationType(translation) - Translation_Remap;
	int lookup = GetTranslationIndex(translation);
	// Blood and Duke palette IDs are different vocabularies. Only their normal
	// and underwater base roles coincide; other view effects need explicit hooks.
	base = base == 1 ? 1 : 0;
	if (unsigned(lookup) >= 256) lookup = 0;
	pick.translation = bank->translations[base][lookup];
	return true;
}
bool CheckDukeResources()
{
	if (!bank) return false;
	bool isolated = firstarttile == bank->primaryFirst && maxarttile == bank->primaryLast && g_gameType == bank->primaryGame &&
		numshades == bank->primaryShades && fileSystem.GetNumEntries() == bank->primaryLumps &&
		Bcrc32(GPalette.BaseColors, sizeof(GPalette.BaseColors), 0) == bank->primaryPaletteCRC;
	auto tile = DukeTile(2296);
	spritetype sprite {};
	sprite.setspritetexture(tile);
	TexturePick pick;
	auto texture = TexMan.GetGameTexture(tile);
	bool palette = PickDukeTexture(texture, 0, pick) && pick.translation == bank->translations[0][0];
	if (!palette) return false;
	auto& art = bank->tiles[2296];
	for (auto& base : bank->translations)
		for (int l = 0; l < 256; ++l)
		{
			if (l && base[l] == base[0]) continue;
			if (!PickDukeTexture(texture, base[l], pick) || pick.translation != base[l]) return false;
			auto remap = GPalette.GetTranslation(GetTranslationType(base[l]), GetTranslationIndex(base[l]));
			if (!remap) return false;
			auto bitmap = texture->GetTexture()->GetBgraBitmap(remap->Palette);
			for (unsigned x = 0; x < art.width; ++x)
				for (unsigned y = 0; y < art.height; ++y)
				{
					unsigned raw = art.pixels[x * art.height + y];
					unsigned index = raw == 255 ? 0 : raw == 0 ? 255 : raw;
					palette &= !memcmp(bitmap.GetPixels() + y * bitmap.GetPitch() + x * 4, &remap->Palette[index], 4);
				}
		}
	int luminosity = MakeLuminosityTranslation(0, 0, 255).index();
	palette &= PickDukeTexture(texture, luminosity, pick) && pick.translation == luminosity;
	return isolated && palette && sprite.spritetexture() == tile && tile != tileGetTextureID(2296) &&
		DukeTexture("BLOODSPLAT1") == tile && OpenDukeResource("game.con").isOpen();
}
}
