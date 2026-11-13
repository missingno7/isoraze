// IsoRaze's first scene-building experiment. Uses the existing hardware backend.
// This mode deliberately remains diagnostic until per-surface visibility exists.

#include "hw_isometric.h"
#include "hw_isovisibility.h"
#include "hw_drawinfo.h"
#include "hw_drawstructs.h"
#include "hw_sections.h"
#include "hw_clock.h"
#include "hw_portal.h"
#include "hw_clipper.h"
#include "automap.h"
#include "flatvertices.h"
#include "hw_lightbuffer.h"
#include "gamestruct.h"
#include "buildtiles.h"
#include "c_dispatch.h"
#include "gamestate.h"
#include "coreactor.h"
#include "texturemanager.h"
#include "i_time.h"
#include "bitmap.h"
#include "hw_renderstate.h"
#include "hw_viewpointbuffer.h"

#include <cmath>
#include <map>
#include <vector>

CUSTOM_CVARD(Bool, iso_debug, false, 0, "experimental isometric scene with an optional player-eye mask")
{
	if (self) Printf("IsoRaze experimental view: iso_debug_visibility toggles the player-eye mask.\n");
}
EXTERN_CVAR(Bool, iso_debug_visibility_horizontal)
CVARD(Float, iso_debug_yaw, 45.f, 0, "diagnostic isometric camera yaw in degrees")
CVARD(Float, iso_debug_pitch, 55.f, 0, "diagnostic isometric camera downward pitch in degrees")
CVARD(Float, iso_debug_scale, 640.f, 0, "diagnostic orthographic view height in world units")
CVARD(Bool, iso_debug_eye_lighting, true, 0, "use player-eye distance for isometric shade/fog; disable for comparison")
CVARD(Bool, iso_debug_boundary_walls, true, 0, "show candidate boundary wall exteriors without adding neighboring regions")
CVARD(Bool, iso_debug_cutaway_view, false, 0, "highlight cutaway fragments in magenta")
CVARD(Bool, iso_debug_cutaway, true, 0, "soft player-centred cutaway of foreground walls and floors")
CVARD(Float, iso_debug_cutaway_radius, 64.f, 0, "outer cutaway radius in world units; follows orthographic zoom")
CVARD(Float, iso_debug_cutaway_inner, 0.5f, 0, "fully clear inner cutaway radius as a fraction of its outer radius")

namespace
{
	struct SceneStats
	{
		DVector3 eye{};
		int sector = -1;
		unsigned sections = 0, walls = 0, sprites = 0, boundarySides = 0, cutawayWalls = 0, cutawayFlats = 0;
		uint32_t candidateHash = 0;
		FVector4 cutawayTop{}, cutawayBottom{};
		float cutawayStrength = 0.f;
	} lastScene;

	float CameraValue(float value, float fallback, float minimum, float maximum)
	{
		return std::isfinite(value) ? clamp(value, minimum, maximum) : fallback;
	}

	// Selection uses the opaque texels of the actual animated billboard. The
	// much larger shader capsule only controls the appearance AFTER selection.
	struct AlphaMask
	{
		bool solid = false;
		std::vector<FVector4> spans; // normalized left, top, right, bottom
	};
	std::map<std::pair<FGameTexture*, int>, AlphaMask> cutawayMasks;

	const AlphaMask& CutawayMask(FGameTexture* texture, float threshold)
	{
		int minimum = clamp(int(ceil(threshold * 255.f)), 1, 255);
		auto key = std::make_pair(texture, minimum);
		auto found = cutawayMasks.find(key);
		if (found != cutawayMasks.end()) return found->second;
		auto& mask = cutawayMasks[key];
		if (!texture || texture->isHardwareCanvas()) return mask;
		auto bitmap = texture->GetTexture()->GetBgraBitmap(nullptr);
		int width = bitmap.GetWidth(), height = bitmap.GetHeight();
		if (!bitmap.GetPixels() || width <= 0 || height <= 0) return mask;
		mask.solid = true;
		for (int y = 0; y < height; ++y)
		{
			auto row = bitmap.GetPixels() + y * bitmap.GetPitch();
			int start = -1;
			for (int x = 0; x <= width; ++x)
			{
				bool opaque = x < width && row[x * 4 + 3] >= minimum;
				if (x < width && !opaque) mask.solid = false;
				if (opaque && start < 0) start = x;
				if (!opaque && start >= 0)
				{
					mask.spans.push_back({ float(start) / width, float(y) / height,
						float(x) / width, float(y + 1) / height });
					start = -1;
				}
			}
		}
		if (mask.solid) mask.spans = { { 0, 0, 1, 1 } };
		return mask;
	}

	struct CoverageVertex { double v[5]; }; // sprite x/y, relative depth, surface u/v
	struct CoveragePolygon
	{
		CoverageVertex vertices[32]; // triangle plus 13 clips, including coincident boundary vertices
		int count = 0;
	};

	CoveragePolygon ClipCoverage(const CoveragePolygon& input, int axis, double bound, double sign)
	{
		CoveragePolygon output;
		if (!input.count) return output;
		auto previous = input.vertices[input.count - 1];
		double before = (previous.v[axis] - bound) * sign;
		for (int i = 0; i < input.count; ++i)
		{
			auto current = input.vertices[i];
			double after = (current.v[axis] - bound) * sign;
			if ((before >= 0) != (after >= 0))
			{
				double t = before / (before - after);
				CoverageVertex crossing;
				for (int j = 0; j < 5; ++j) crossing.v[j] = previous.v[j] + t * (current.v[j] - previous.v[j]);
				output.vertices[output.count++] = crossing;
			}
			if (after >= 0) output.vertices[output.count++] = current;
			previous = current;
			before = after;
		}
		return output;
	}

	CoveragePolygon CoverageRectangle(CoveragePolygon polygon, const FVector4& rect, int axis = 0)
	{
		polygon = ClipCoverage(polygon, axis, rect.X, 1);
		polygon = ClipCoverage(polygon, axis + 1, rect.Y, 1);
		polygon = ClipCoverage(polygon, axis, rect.Z, -1);
		return ClipCoverage(polygon, axis + 1, rect.W, -1);
	}

	bool HasCoverage(const CoveragePolygon& polygon)
	{
		double area = 0;
		for (int i = 0; i < polygon.count; ++i)
		{
			auto& a = polygon.vertices[i];
			auto& b = polygon.vertices[(i + 1) % polygon.count];
			area += a.v[0] * b.v[1] - a.v[1] * b.v[0];
		}
		return fabs(area) > 1.e-10;
	}

	bool SurfaceHasCoverage(const CoveragePolygon& polygon, FGameTexture* texture,
		float threshold, bool clampX, bool clampY)
	{
		if (!texture) return true; // solid color / sky-transition geometry
		const auto& mask = CutawayMask(texture, threshold);
		if (mask.solid) return true;
		if (mask.spans.empty()) return false;
		double minU = FLT_MAX, minV = FLT_MAX, maxU = -FLT_MAX, maxV = -FLT_MAX;
		for (int i = 0; i < polygon.count; ++i)
		{
			minU = min(minU, polygon.vertices[i].v[3]); maxU = max(maxU, polygon.vertices[i].v[3]);
			minV = min(minV, polygon.vertices[i].v[4]); maxV = max(maxV, polygon.vertices[i].v[4]);
		}
		int firstU = clampX ? 0 : int(floor(minU)), lastU = clampX ? 0 : int(floor(maxU));
		int firstV = clampY ? 0 : int(floor(minV)), lastV = clampY ? 0 : int(floor(maxV));
		for (int y = firstV; y <= lastV; ++y)
			for (int x = firstU; x <= lastU; ++x)
				for (auto rect : mask.spans)
				{
					if (clampX && rect.X == 0) rect.X = float(min(minU, 0.));
					if (clampX && rect.Z == 1) rect.Z = float(max(maxU, 1.));
					if (clampY && rect.Y == 0) rect.Y = float(min(minV, 0.));
					if (clampY && rect.W == 1) rect.W = float(max(maxV, 1.));
					rect.X += x; rect.Z += x; rect.Y += y; rect.W += y;
					if (rect.Z < minU || rect.X > maxU || rect.W < minV || rect.Y > maxV) continue;
					if (HasCoverage(CoverageRectangle(polygon, rect, 3))) return true;
				}
		return false;
	}

	bool TriangleCoversSprite(HWDrawInfo& di, const FFlatVertex* vertices,
		FGameTexture* texture, float threshold, bool clampX, bool clampY)
	{
		auto& origin = di.cutawaySpriteOrigin;
		auto& right = di.cutawaySpriteRight;
		auto& down = di.cutawaySpriteDown;
		double determinant = double(right.X) * down.Y - double(right.Y) * down.X;
		if (fabs(determinant) < 1.e-8) return false;
		CoveragePolygon polygon;
		polygon.count = 3;
		for (int i = 0; i < 3; ++i)
		{
			const auto& v = vertices[i];
			FVector4 point{ v.x, v.z, v.y, 1.f }, view;
			di.VPUniforms.mViewMatrix.multMatrixPoint(&point.X, &view.X);
			double dx = double(view.X) - origin.X, dy = double(view.Y) - origin.Y;
			double x = (dx * down.Y - dy * down.X) / determinant;
			double y = (right.X * dy - right.Y * dx) / determinant;
			polygon.vertices[i] = { { x, y, view.Z - (origin.Z + right.Z * x + down.Z * y), v.u, v.v } };
		}
		// Orthographic view rays point towards +view-Z. Compare at the SAME
		// screen position, not against an endpoint or an extended body capsule.
		polygon = ClipCoverage(polygon, 2, 0.01, 1);
		polygon = CoverageRectangle(polygon, { 0, 0, 1, 1 });
		if (!HasCoverage(polygon)) return false;
		for (auto& span : di.cutawaySpriteSpans)
		{
			auto overlap = CoverageRectangle(polygon, span);
			if (HasCoverage(overlap) && SurfaceHasCoverage(overlap, texture, threshold, clampX, clampY)) return true;
		}
		return false;
	}

	void SelectIsometricCutaways(HWDrawInfo& di, tspritetype* playerSprite)
	{
		if (!di.deferIsometricGeometry) return;
		cutawayMasks.clear();
		for (auto& list : di.drawlists)
			for (auto sprite : list.sprites)
			{
				// Shadows and attached effects can share the actor. Only the
				// original player tsprite defines the obstruction silhouette.
				if (!playerSprite || sprite->Sprite != playerSprite ||
					sprite->modelframe != 0 || !sprite->texture) continue;
				FVector3 vertices[4], view[3];
				sprite->CalculateVertices(&di, vertices, &di.Viewpoint.Pos);
				for (int i = 0; i < 3; ++i)
				{
					FVector4 point{ vertices[i].X, vertices[i].Y, vertices[i].Z, 1.f }, transformed;
					di.VPUniforms.mViewMatrix.multMatrixPoint(&point.X, &transformed.X);
					view[i] = { transformed.X, transformed.Y, transformed.Z };
				}
				di.cutawaySpriteOrigin = view[0];
				di.cutawaySpriteRight = view[1] - view[0];
				di.cutawaySpriteDown = view[2] - view[0];
				if (sprite->ul == sprite->ur || sprite->vt == sprite->vb) continue;
				for (auto rect : CutawayMask(sprite->texture, sprite->alphaThreshold).spans)
				{
					float x1 = (rect.X - sprite->ul) / (sprite->ur - sprite->ul);
					float x2 = (rect.Z - sprite->ul) / (sprite->ur - sprite->ul);
					float y1 = (rect.Y - sprite->vt) / (sprite->vb - sprite->vt);
					float y2 = (rect.W - sprite->vt) / (sprite->vb - sprite->vt);
					di.cutawaySpriteSpans.Push({ min(x1, x2), min(y1, y2), max(x1, x2), max(y1, y2) });
				}
			}
		di.deferIsometricGeometry = false;
		for (auto wall : di.deferredIsometricWalls) di.AddWall(wall);
		for (auto flat : di.deferredIsometricFlats) di.AddFlat(flat);
		di.deferredIsometricWalls.Clear();
		di.deferredIsometricFlats.Clear();
		cutawayMasks.clear();
	}

	std::vector<uint32_t> MappingFlags()
	{
		std::vector<uint32_t> result;
		for (unsigned i = 0; i < sector.Size(); ++i)
		{
			result.push_back(sector[i].exflags & SECTOREX_SEEN);
			result.push_back(show2dsector[i]);
			CoreSectIterator actors(i);
			while (auto actor = actors.Next())
			{
				result.push_back(actor->GetIndex());
				result.push_back(actor->spr.cstat2 & CSTAT2_SPRITE_MAPPED);
			}
		}
		for (unsigned i = 0; i < wall.Size(); ++i) result.push_back(show2dwall[i]);
		return result;
	}
}

bool render_isometric_debug_enabled()
{
	return iso_debug && isDuke();
}

void SetupIsometricDebugView(FRenderViewpoint& vp)
{
	// Capture the observer before moving the presentation camera out of the world.
	vp.VisibilityPos = vp.Pos;
	vp.VisibilitySector = vp.SectCount;
	vp.Isometric = true;
	vp.OrthographicHeight = CameraValue(iso_debug_scale, 640.f, 64.f, 4096.f);
	auto yaw = DAngle::fromDeg(CameraValue(iso_debug_yaw, 45.f, -36000.f, 36000.f));
	auto pitch = DAngle::fromDeg(CameraValue(iso_debug_pitch, 55.f, 20.f, 75.f));
	double distance = vp.OrthographicHeight * 2.;
	auto offset = yaw.ToVector() * (distance * pitch.Cos());
	vp.Pos.X -= offset.X;
	vp.Pos.Y += offset.Y;
	vp.Pos.Z += distance * pitch.Sin();
	vp.HWAngles.Yaw = FAngle::fromDeg(yaw.Degrees() - 90.);
	vp.HWAngles.Pitch = FAngle::fromDeg(pitch.Degrees());
	vp.HWAngles.Roll = FAngle::fromDeg(0.);
	vp.RotAngle = yaw.BAMs();
	vp.ViewVector = DAngle::fromDeg(-yaw.Degrees()).ToVector();
	vp.TanCos = vp.ViewVector.X;
	vp.TanSin = vp.ViewVector.Y;
}

void SetupIsometricCutaway(HWDrawInfo& di)
{
	auto& uniforms = di.VPUniforms;
	uniforms.mCutawayTop = {};
	uniforms.mCutawayBottom = {};
	uniforms.mCutawayParams = {};
	static DCoreActor* previousActor = nullptr;
	static uint64_t previousTime = 0;
	static float amount = 0.f;
	auto now = I_GetTimeNS();
	auto actor = di.Viewpoint.CameraActor;
	if (!di.Viewpoint.Isometric || !actor)
	{
		// Security-camera textures also call SetupView. They must not reset
		// the main view's transition on every frame.
		return;
	}
	if (actor != previousActor)
	{
		previousActor = actor;
		previousTime = now;
		amount = 0.f;
	}
	float step = float(now - previousTime) / 150000000.f;
	previousTime = now;
	amount = clamp(amount + (iso_debug_cutaway ? step : -step), 0.f, 1.f);
	if (amount <= 0.f) return;
	if (!TexMan.GetGameTexture(actor->spr.spritetexture())) return;

	double height;
	double offset = actor->GetOffsetAndHeight(height);
	height = clamp(height, 8., 192.);
	auto pos = actor->interpolatedpos(di.Viewpoint.TicFrac);
	float bottom = float(-pos.Z - offset + actor->spr.yoffset * actor->spr.scale.Y);
	FVector4 point{ float(pos.X), bottom + float(height), float(-pos.Y), 1.f };
	uniforms.mViewMatrix.multMatrixPoint(&point.X, &uniforms.mCutawayTop.X);
	point.Y = bottom;
	uniforms.mViewMatrix.multMatrixPoint(&point.X, &uniforms.mCutawayBottom.X);
	uniforms.mCutawayTop.W = CameraValue(iso_debug_cutaway_radius, 64.f, 8.f, 256.f);
	uniforms.mCutawayBottom.W = CameraValue(iso_debug_cutaway_inner, 0.5f, 0.05f, 0.95f);
	uniforms.mCutawayParams.Y = amount * amount * (3.f - 2.f * amount);
	uniforms.mCutawayParams.Z = iso_debug_cutaway_view ? 1.f : 0.f;
}

bool IsometricCutawayWall(HWDrawInfo& di, const HWWall& wall)
{
	if (di.cutawayVP[0] < 0 || di.cutawaySpriteSpans.Size() == 0 ||
		wall.type == RENDERWALL_FOGBOUNDARY || wall.type == RENDERWALL_MIRRORSURFACE) return false;
	FFlatVertex corners[4];
	for (int i = 0; i < 4; ++i)
	{
		int side = i >= 2;
		corners[i].Set(side ? wall.glseg.x2 : wall.glseg.x1,
			(i == 1 || i == 2) ? wall.ztop[side] : wall.zbottom[side],
				side ? wall.glseg.y2 : wall.glseg.y1,
				wall.texture ? wall.tcs[i].u : 0.f, wall.texture ? wall.tcs[i].v : 0.f);
	}
	float threshold = wall.texture ? wall.texture->alphaThreshold : 0.f;
	for (int i = 1; i < 3; ++i)
	{
		FFlatVertex triangle[] = { corners[0], corners[i], corners[i + 1] };
		if (TriangleCoversSprite(di, triangle, wall.texture, threshold,
			wall.flags & HWWall::HWF_CLAMPX, wall.flags & HWWall::HWF_CLAMPY)) return true;
	}
	return false;
}

bool IsometricCutawayFlat(HWDrawInfo& di, HWFlat& flat)
{
	if (di.cutawayVP[0] < 0 || di.cutawaySpriteSpans.Size() == 0 ||
		(!flat.Sprite && (flat.plane != 0 || flat.stack))) return false;
	flat.MakeVertices(&di);
	const auto* vertices = screen->mVertexData->GetBuffer(flat.vertindex);
	bool covers = false;
	for (int i = 0; i + 2 < flat.vertcount && !covers; i += 3)
		covers = TriangleCoversSprite(di, vertices + i, flat.texture, flat.texture->alphaThreshold,
			flat.Sprite != nullptr, flat.Sprite != nullptr);
	if (!covers) return false;
	if (!flat.Sprite)
	{
		double sumx = 0., sumy = 0.;
		for (int i = 0; i < flat.vertcount; ++i) { sumx += vertices[i].x; sumy += vertices[i].y; }
		flat.depth = float((sumx / flat.vertcount - di.Viewpoint.Pos.X) * di.Viewpoint.TanCos +
			(sumy / flat.vertcount - di.Viewpoint.Pos.Y) * di.Viewpoint.TanSin);
		if (flat.sec->floorstat & CSTAT_SECTOR_SLOPE)
		{
			// Reuse the existing sloped-surface sorter for the blended edge.
			flat.slopeindex = di.SlopeSpriteVertices.Reserve(flat.vertcount);
			flat.slopecount = flat.vertcount;
			for (int i = 0; i < flat.vertcount; ++i)
				di.SlopeSpriteVertices[flat.slopeindex + i] = vertices[i];
		}
	}
	return true;
}

void RenderIsometricCutawayCoverage(HWDrawInfo& di, FRenderState& state)
{
	state.mCutawayCoverageTexture = nullptr;
	if (di.outer || (di.cutawayWalls == 0 && di.cutawayFlats == 0)) return;

	// Draw the already-collected scene with ALL selected blockers absent. A
	// white fragment means real, eye-visible geometry remains; black is empty.
	// This includes player sprites and transparent artwork, independently of
	// their actual color, and never lets the sky or a cutaway edge fill the mask.
	auto& coverage = screen->mIsometricCutawayCoverage;
	const auto& viewport = screen->mSceneViewport;
	if (!coverage || coverage->GetWidth() != viewport.width || coverage->GetHeight() != viewport.height)
		coverage = std::make_unique<FCanvasTexture>(viewport.width, viewport.height);

	float clearColor[4];
	for (int i = 0; i < 4; ++i)
	{
		clearColor[i] = screen->mSceneClearColor[i];
		screen->mSceneClearColor[i] = 0.f;
	}
	di.VPUniforms.mCutawayParams.W = -1.f;
	screen->RenderTextureView(coverage.get(), [&](IntRect& bounds)
	{
		state.SetScissor(0, 0, -1, -1);
		state.SetDepthMask(true);
		state.SetColorMask(true);
		state.Clear(CT_Color | CT_Depth | CT_Stencil);
		state.SetViewport(bounds.left, bounds.top, bounds.width, bounds.height);
		state.SetScissor(bounds.left, bounds.top, bounds.width, bounds.height);
		state.EnableDepthTest(true);
		state.EnableStencil(false);
		// Portals and shot effects are not background geometry. In particular,
		// drawing a sky here would authorize a hole into otherwise empty space.
		di.RenderScene(state);
		di.RenderTranslucent(state);
	});
	for (int i = 0; i < 4; ++i) screen->mSceneClearColor[i] = clearColor[i];

	// The ordinary scene draw retains its original color/depth wherever the
	// coverage texture is empty; elsewhere it uses the existing soft opening.
	di.VPUniforms.mCutawayParams.W = 1.f;
	state.mCutawayCoverageTexture = coverage->GetHardwareTexture(0, 0);
	// Vulkan defers clears until the next render pass. The offscreen pass
	// consumed the initial clear request, so explicitly clear the main target.
	di.Set3DViewport(state);
	screen->mViewpoints->Bind(state, di.vpIndex);
}

void CreateIsometricDebugScene(HWDrawInfo& di)
{
	const auto& vp = di.Viewpoint;
	BeginIsometricVisibility();
	lastScene = {};
	lastScene.eye = vp.VisibilityPos;
	lastScene.sector = vp.VisibilitySector;
	lastScene.candidateHash = 2166136261u;
	lastScene.cutawayTop = di.VPUniforms.mCutawayTop;
	lastScene.cutawayBottom = di.VPUniforms.mCutawayBottom;
	lastScene.cutawayStrength = di.VPUniforms.mCutawayParams.Y;

	// Canonical full-circle traversal: neither mouse facing nor camera settings
	// determine the candidate set. Collection submits nothing and marks no map data.
	di.mDrawer.CollectScene(di.mClipper, vp.VisibilityPos, vp.VisibilitySector, iso_debug_visibility_horizontal);
	const auto& candidates = di.mDrawer.GotSections();
	BitArray visitedSectors(sector.Size());
	BitArray visitedWalls(wall.Size());
	BitArray candidateSectors(sector.Size());
	visitedSectors.Zero();
	visitedWalls.Zero();
	candidateSectors.Zero();
	for (unsigned i = 0; i < candidates.Size(); ++i)
	{
		if (candidates[i]) candidateSectors.Set(sections[i].sector);
	}

	portalState.StartFrame();
	ProcessAll.Clock();
	screen->mVertexData->Map();
	screen->mLights->Map();
	di.tsprites.clear();
	di.ingeo = false;
	di.geoofs = { 0, 0 };
	di.deferIsometricGeometry = di.cutawayVP[0] >= 0;
	tspritetype* playerSprite = nullptr;
	auto forward = DAngle::fromBam(vp.RotAngle).ToVector();

	for (unsigned i = 0; i < candidates.Size(); ++i)
	{
		if (!candidates[i]) continue;
		++lastScene.sections;
		lastScene.candidateHash = (lastScene.candidateHash ^ i) * 16777619u;
		auto& section = sections[i];
		auto sec = &sector[section.sector];

		HWFlat flat;
		flat.ProcessSector(&di, sec, i, SSRF_RENDERFLOOR);

		for (auto lineIndex : section.lines)
		{
			int wallIndex = sectionLines[lineIndex].wall;
			if (wallIndex < 0 || visitedWalls[wallIndex]) continue;
			auto wal = &wall[wallIndex];
			auto front = sec;
			auto back = wal->nextSector();
			auto delta = wal->delta();
			// Orthographic rays are parallel. Use their direction, not the
			// finite camera position, when choosing the visible wall side.
			if (forward.X * delta.Y - forward.Y * delta.X < 0)
			{
				// A one-sided wall has no exterior face. Do not manufacture one:
				// it turns near-side room boundaries into opaque camera blockers.
				if (!iso_debug_boundary_walls || !wal->twoSided()) continue;
				// A candidate on the opposite side will submit its own face.
				if (candidateSectors[sectindex(back)]) continue;
				// Only an actual shared wall face is admitted, never the other room.
				wal = wal->nextWall();
				front = back;
				back = sec;
				++lastScene.boundarySides;
			}
			visitedWalls.Set(wallIndex);
			if (wal->twoSided()) visitedWalls.Set(wallindex(wal->nextWall()));
			HWWall hwwall;
			hwwall.Process(&di, wal, front, back);
			++lastScene.walls;
		}

		if (visitedSectors[section.sector]) continue;
		visitedSectors.Set(section.sector);
		CoreSectIterator actors(section.sector);
		while (auto actor = actors.Next())
		{
			if ((actor->spr.cstat & CSTAT_SPRITE_INVISIBLE) ||
				actor->spr.scale.X == 0 || actor->spr.scale.Y == 0) continue;
			if (!actor->spr.spritetexture().isValid()) continue;
			auto sprite = renderAddTsprite(di.tsprites, actor);
			if (!sprite) break;
			if (actor == vp.CameraActor) playerSprite = sprite;
		}
	}

	gi->processSprites(di.tsprites, DVector3(vp.Pos.X, -vp.Pos.Y, -vp.Pos.Z),
		DAngle::fromBam(vp.RotAngle), vp.TicFrac);
	lastScene.sprites = di.tsprites.Size();
	di.DispatchSprites();
	SelectIsometricCutaways(di, playerSprite);
	BuildIsometricVisibility(di);
	BuildIsometricShotTrails(di);
	lastScene.cutawayWalls = di.cutawayWalls;
	lastScene.cutawayFlats = di.cutawayFlats;
	screen->mLights->Unmap();
	screen->mVertexData->Unmap();
	ProcessAll.Unclock();
}

CCMD(iso_debug_stats)
{
	if (!render_isometric_debug_enabled() || gamestate != GS_LEVEL || lastScene.sector < 0)
	{
		Printf("No diagnostic isometric scene. Enable iso_debug in a Duke level.\n");
		return;
	}
	Printf("IsoRaze scene candidates: eye=(%.3f, %.3f, %.3f), sector=%d\n"
		"  candidates: sections=%u, hash=%08x; submitted walls=%u, boundary exteriors=%u, sprites=%u; cutaway wall parts=%u, floors=%u\n",
		lastScene.eye.X, -lastScene.eye.Y, -lastScene.eye.Z, lastScene.sector,
		lastScene.sections, lastScene.candidateHash, lastScene.walls, lastScene.boundarySides, lastScene.sprites, lastScene.cutawayWalls, lastScene.cutawayFlats);
	Printf("  cutaway view endpoints=(%.2f, %.2f, %.2f) / (%.2f, %.2f, %.2f), radius=%.2f, strength=%.3f\n",
		lastScene.cutawayTop.X, lastScene.cutawayTop.Y, lastScene.cutawayTop.Z,
		lastScene.cutawayBottom.X, lastScene.cutawayBottom.Y, lastScene.cutawayBottom.Z, lastScene.cutawayTop.W, lastScene.cutawayStrength);
}

CCMD(iso_debug_check)
{
	if (!render_isometric_debug_enabled() || gamestate != GS_LEVEL || lastScene.sector < 0 ||
		(unsigned)lastScene.sector >= sector.Size())
	{
		Printf("Enable iso_debug in a Duke level before checking candidate collection.\n");
		return;
	}
	auto before = MappingFlags();
	BunchDrawer drawer;
	Clipper clipper;
	drawer.CollectScene(&clipper, lastScene.eye, lastScene.sector, iso_debug_visibility_horizontal);
	auto first = drawer.GotSections();
	drawer.CollectScene(&clipper, lastScene.eye, lastScene.sector, iso_debug_visibility_horizontal);
	const auto& second = drawer.GotSections();
	bool stable = first.Size() == second.Size();
	for (unsigned i = 0; stable && i < first.Size(); ++i) stable = first[i] == second[i];
	Printf("IsoRaze candidate check: repeatable=%s, seen/mapped flags unchanged=%s.\n",
		stable ? "PASS" : "FAIL", before == MappingFlags() ? "PASS" : "FAIL");
}
