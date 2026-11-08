#pragma once

#include <limits.h>
#include "textures.h"
#include "image.h"
#include "i_time.h"
#include "intvec.h"
#include "name.h"
#include "tiletexture.h"
#include "maptypes.h"
#include "texinfo.h"
#include "texturemanager.h"

// all that's left here is the wrappers that need to go away.


inline const FTextureID spritetypebase::spritetexture() const
{
	// Sprite art can belong to a secondary bank outside the legacy map tiles.
	unsigned index = unsigned(firstarttile) + unsigned(picnum);
	if (picnum < 0 || index >= unsigned(TexMan.NumTextures())) return FNullTextureID();
	return FSetTextureID(index);
}

inline void spritetypebase::setspritetexture(FTextureID tex)
{
	picnum = tex.isValid() ? tex.GetIndex() - firstarttile : -1;
}

//[[deprecated]]
inline FGameTexture* tileGetTexture(int tile, bool animate = false)
{
	auto texid = tileGetTextureID(tile);
	if (animate) tileUpdatePicnum(texid);
	return TexMan.GetGameTexture(texid);
}
