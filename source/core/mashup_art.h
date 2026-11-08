#pragma once

#include "resourcefile.h"
#include "buildtiles.h"
#include "m_swap.h"
#include "cmdlib.h"
#include <memory>
#include <vector>

namespace Mashup
{
// Both games use the same ART layout. Private banks keep borrowed pixel storage
// alive without installing either game's tiles in the other map's namespace.
struct PrivateArtBank
{
	struct Tile
	{
		const uint8_t* pixels = nullptr;
		unsigned width = 0, height = 0;
		uint32_t animation = 0;
	};

	std::vector<std::unique_ptr<FileSys::FileData>> artFiles;
	TArray<Tile> tiles;

	void ReadArt(FileSys::FileData data)
	{
		auto word = [&](size_t offset) { uint16_t v; memcpy(&v, data.bytes() + offset, 2); return LittleShort(v); };
		auto dword = [&](size_t offset) { uint32_t v; memcpy(&v, data.bytes() + offset, 4); return LittleLong(v); };
		if (data.size() < 16 || dword(0) != 1) I_FatalError("Superset: invalid ART header");
		unsigned first = dword(8), last = dword(12);
		if (first > last || last >= MAXUSERTILES) I_FatalError("Superset: invalid ART tile range");
		size_t count = last - first + 1, offset = 16 + count * 8;
		if (offset > data.size()) I_FatalError("Superset: truncated ART metadata");
		if (tiles.Size() <= last) tiles.Resize(last + 1);
		for (unsigned i = 0; i < count; ++i)
		{
			unsigned width = word(16 + i * 2), height = word(16 + count * 2 + i * 2);
			size_t size = size_t(width) * height;
			if (size > data.size() - offset) I_FatalError("Superset: truncated ART pixels");
			tiles[first + i] = { size ? data.bytes() + offset : nullptr, width, height, dword(16 + count * 4 + i * 4) };
			offset += size;
		}
		auto buffer = std::make_unique<FileSys::FileData>();
		*buffer = std::move(data);
		artFiles.push_back(std::move(buffer));
	}
};
}
