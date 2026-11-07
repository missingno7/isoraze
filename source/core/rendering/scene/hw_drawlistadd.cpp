// 
//---------------------------------------------------------------------------
//
// Copyright(C) 2000-2016 Christoph Oelckers
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//--------------------------------------------------------------------------
//

#include "hw_dynlightdata.h"
#include "hw_cvars.h"
#include "hw_lightbuffer.h"
#include "hw_drawstructs.h"
#include "hw_drawinfo.h"
#include "hw_material.h"
#include "build.h"
#include "gamefuncs.h"
#include "hw_isometric.h"
#include "hw_isovisibility.h"

EXTERN_CVAR(Bool, gl_seamless)

//==========================================================================
//
// 
//
//==========================================================================

void HWDrawInfo::AddWall(HWWall *wall)
{
	if (deferIsometricGeometry)
	{
		auto copy = (HWWall*)RenderDataAllocator.Alloc(sizeof(HWWall));
		*copy = *wall;
		deferredIsometricWalls.Push(copy);
		return;
	}
	if (Viewpoint.Isometric) CollectIsometricOccluder(*wall);
	if (IsometricCutawayWall(*this, *wall))
	{
		wall->flags |= HWWall::HWF_CUTAWAY;
		++cutawayWalls;
		if (!(wall->flags & HWWall::HWF_TRANSLUCENT))
		{
			// Keep the solid exterior in its normal depth pass. Only the soft
			// opening edge is blended, sorted together with the existing sprites.
			auto edge = drawlists[GLDL_TRANSLUCENT].NewWall();
			*edge = *wall;
			edge->flags |= HWWall::HWF_TRANSLUCENT | HWWall::HWF_CUTAWAYEDGE;
			edge->RenderStyle = STYLE_Translucent;
			edge->ViewDistance = (Viewpoint.Pos.XY() - DVector2(
				(wall->glseg.x1 + wall->glseg.x2) * 0.5f,
				(wall->glseg.y1 + wall->glseg.y2) * 0.5f)).LengthSquared();
		}
	}
	if (wall->flags & HWWall::HWF_TRANSLUCENT)
	{
		auto newwall = drawlists[GLDL_TRANSLUCENT].NewWall();
		*newwall = *wall;
	}
	else
	{
		int list;

		if (wall->type != RENDERWALL_M2S) list = GLDL_PLAINWALLS;
		else if (wall->Sprite == nullptr) list = GLDL_MASKEDWALLS;
		else if (wall->glseg.x1 == wall->glseg.x2) list = GLDL_MASKEDWALLSV;
		else if (wall->glseg.y1 == wall->glseg.y2) list = GLDL_MASKEDWALLSH;
		else list = wall->walldist? GLDL_MASKEDWALLSD : GLDL_MASKEDWALLSS;
		auto newwall = drawlists[list].NewWall();
		*newwall = *wall;
	}
}

//==========================================================================
//
// 
//
//==========================================================================

void HWDrawInfo::AddMirrorSurface(HWWall *w)
{
	w->type = RENDERWALL_MIRRORSURFACE;
	auto newwall = drawlists[GLDL_TRANSLUCENTBORDER].NewWall();
	*newwall = *w;

	// Invalidate vertices to allow setting of texture coordinates
	newwall->vertcount = 0;

	FVector3 v = newwall->glseg.Normal();
	auto tcs = newwall->tcs;
	tcs[HWWall::LOLFT].u = tcs[HWWall::LORGT].u = tcs[HWWall::UPLFT].u = tcs[HWWall::UPRGT].u = v.X;
	tcs[HWWall::LOLFT].v = tcs[HWWall::LORGT].v = tcs[HWWall::UPLFT].v = tcs[HWWall::UPRGT].v = v.Z;
	newwall->MakeVertices(this, false);
	newwall->dynlightindex = -1; // the environment map should not be affected by lights.
}

//==========================================================================
//
// FDrawInfo::AddFlat
//
// Checks texture, lighting and translucency settings and puts this
// plane in the appropriate render list.
//
//==========================================================================

void HWDrawInfo::AddFlat(HWFlat *flat)
{
	if (deferIsometricGeometry)
	{
		auto copy = (HWFlat*)RenderDataAllocator.Alloc(sizeof(HWFlat));
		*copy = *flat;
		deferredIsometricFlats.Push(copy);
		return;
	}
	int list;
	bool slopespr = false;
	bool translucent = flat->RenderStyle != LegacyRenderStyles[STYLE_Translucent] ||
		flat->alpha < 1.f - FLT_EPSILON || checkTranslucentReplacement(flat->texture->GetID(), flat->palette);
	flat->cutawayPass = 0;
	if (IsometricCutawayFlat(*this, *flat))
	{
		flat->cutawayPass = translucent ? 3 : 1;
		++cutawayFlats;
		slopespr = flat->Sprite || (flat->sec->floorstat & CSTAT_SECTOR_SLOPE);
		if (!translucent)
		{
			auto edge = drawlists[GLDL_TRANSLUCENT].NewFlat(slopespr);
			*edge = *flat;
			edge->cutawayPass = 2;
			edge->RenderStyle = STYLE_Translucent;
		}
	}

	if (translucent)
	{
		// translucent portals go into the translucent border list.
		list = flat->Sprite || flat->cutawayPass ? GLDL_TRANSLUCENT : GLDL_TRANSLUCENTBORDER;
		if (!flat->cutawayPass) slopespr = !!(flat->Sprite);
	}
	else
	{
		list = flat->Sprite ? ((flat->Sprite->clipdist & TSPR_SLOPESPRITE)? GLDL_MASKEDSLOPEFLATS : GLDL_MASKEDFLATS) : GLDL_PLAINFLATS;
	}
	auto newflat = drawlists[list].NewFlat(slopespr);
	*newflat = *flat;
}


//==========================================================================
//
// 
//
//==========================================================================
void HWDrawInfo::AddSprite(HWSprite *sprite, bool translucent)
{
	int list;
	if (translucent || sprite->modelframe == 0) list = GLDL_TRANSLUCENT;
	else list = GLDL_MODELS;

	auto newsprt = drawlists[list].NewSprite();
	*newsprt = *sprite;
}

