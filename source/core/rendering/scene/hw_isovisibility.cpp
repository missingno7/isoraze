// Player-eye visibility is independent of presentation cutaways. Geometry is
// encoded losslessly for sector-connected fragment ray tests on every backend.
#include "hw_isovisibility.h"
#include "hw_drawinfo.h"
#include "hw_drawstructs.h"
#include "hw_viewpointbuffer.h"
#include "hw_renderstate.h"
#include "flatvertices.h"
#include "texturemanager.h"
#include "bitmap.h"
#include "coreactor.h"
#include "gamestate.h"
#include "gamecontrol.h"
#include "c_dispatch.h"
#include "render.h"
#include <vector>
#include <map>
#include <cstring>
#include <cmath>

CVARD(Bool, iso_debug_visibility, true, 0, "per-fragment player-eye visibility through sector geometry")
CVARD(Bool, iso_debug_visibility_horizontal, true, 0, "height-independent scenery; actors retain eye-height visibility")
CVARD(Float, iso_debug_visibility_feather, 12.f, 0, "inward visibility feather in world units; zero gives a hard boundary")
CVARD(Int, iso_debug_visibility_view, 0, 0, "visibility diagnostic: 0=normal, 1=permission mask")
CVARD(Bool, iso_debug_tracers, true, 0, "short visual trails for native Duke hitscan shots")

namespace
{
    constexpr int DataWidth = 1024;
    struct Occluder { HWWall wall; };
    std::vector<Occluder> occluders;
    std::vector<float> data, uploaded;
    std::map<uint64_t, int> viewpoints;
    unsigned dataUploads = 0;
    struct Trail { DVector3 start, end; int tick; };
    std::vector<Trail> trails;
    struct TrailDraw { int index; float alpha; };
    std::vector<TrailDraw> trailDraws;
    DVector3 lastEye;
    int lastEyeSector = -1;
    int lastTrailTick = -1;

    int Append(std::initializer_list<float> values)
    {
        int index = int(data.size());
        data.insert(data.end(), values.begin(), values.end());
        return index;
    }

    FVector3 VisibilityPlane(sectortype& sec, bool ceiling)
    {
        double c0, f0, cx, fx, cy, fy;
        calcSlope(&sec, DVector3(0., 0., 0.), &c0, &f0);
        calcSlope(&sec, DVector3(1., 0., 0.), &cx, &fx);
        calcSlope(&sec, DVector3(0., 1., 0.), &cy, &fy);
        return ceiling ? FVector3(float(c0 - cx), float(cy - c0), float(-c0)) :
            FVector3(float(f0 - fx), float(fy - f0), float(-f0));
    }

    bool Solid(const walltype& w)
    {
        return !w.twoSided() || (w.cstat & CSTAT_WALL_1WAY);
    }
}

void BeginIsometricVisibility()
{
    occluders.clear();
    viewpoints.clear();
}

void CollectIsometricOccluder(const HWWall& w)
{
    // Masked walls and wall sprites use the already-resolved renderer UVs.
    // Translucent glass transmits sight; actors do not act as opaque rooms.
    if (w.type != RENDERWALL_M2S || !w.texture ||
        (w.flags & HWWall::HWF_TRANSLUCENT) || w.alpha < 0.999f) return;
    occluders.push_back({ w });
}

void BuildIsometricVisibility(HWDrawInfo& di)
{
    auto& u = di.VPUniforms;
    auto& state = *screen->RenderState();
    u.mVisibilityOrigin = {};
    u.mVisibilityTarget = { -1, -1, 4, 0 };
    state.mVisibilityTexture = nullptr;
    if (!iso_debug_visibility || di.Viewpoint.VisibilitySector < 0) return;

    const int wallBase = int(sector.Size()) * 12;
    data.assign(wallBase + wall.Size() * 8, 0.f);
    std::vector<std::vector<int>> spriteMasks(sector.Size());
    for (unsigned i = 0; i < sector.Size(); ++i)
    {
        auto& sec = sector[i];
        auto floor = VisibilityPlane(sec, false), ceiling = VisibilityPlane(sec, true);
        int p = i * 12;
        data[p] = float(wallBase + (sec.walls.Size() ? wallindex(&sec.walls[0]) : 0) * 8);
        data[p + 1] = float(sec.walls.Size());
        data[p + 2] = floor.X; data[p + 3] = floor.Y; data[p + 4] = floor.Z;
        data[p + 5] = ceiling.X; data[p + 6] = ceiling.Y; data[p + 7] = ceiling.Z;
        for (unsigned j = 0; j < sec.walls.Size(); ++j)
        {
            auto& w = sec.walls[j];
            int q = wallBase + wallindex(&w) * 8;
            auto end = w.point2Wall()->pos;
            data[q] = float(w.pos.X); data[q + 1] = float(-w.pos.Y);
            data[q + 2] = float(end.X); data[q + 3] = float(-end.Y);
            data[q + 4] = Solid(w) ? -1.f : float(sectindex(w.nextSector()));
            data[q + 5] = -1.f;
            // Only actual solid jambs constrain the feather, not subdivisions
            // of a continuous opening. point2, rather than array adjacency,
            // handles multiple loops in the same Build sector.
            bool startCap = false;
            walltype* previous = nullptr;
            for (auto& candidate : sec.walls)
                if (candidate.point2Wall() == &w) { previous = &candidate; startCap = Solid(candidate); break; }
            bool endCap = Solid(*w.point2Wall());
            if (Solid(w))
            {
                auto d = w.delta(), next = w.point2Wall()->delta();
                auto before = previous ? previous->delta() : DVector2{};
                startCap = fabs(d.X * before.Y - d.Y * before.X) > 0.001;
                endCap = fabs(d.X * next.Y - d.Y * next.X) > 0.001;
            }
            data[q + 6] = startCap ? 1.f : 0.f;
            data[q + 7] = endCap ? 1.f : 0.f;
        }
    }

    std::map<FGameTexture*, int> alphaOffsets;
    for (auto& item : occluders)
    {
        auto& w = item.wall;
        int alphaIndex, width, height;
        auto tex = w.texture;
        auto found = alphaOffsets.find(tex);
        if (found == alphaOffsets.end())
        {
            auto bitmap = tex->GetTexture()->GetBgraBitmap(nullptr);
            width = bitmap.GetWidth(); height = bitmap.GetHeight();
            if (width <= 0 || height <= 0 || !bitmap.GetPixels()) continue;
            alphaIndex = Append({ float(width), float(height) });
            alphaOffsets[tex] = alphaIndex;
            auto pixels = bitmap.GetPixels();
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    data.push_back(pixels[y * bitmap.GetPitch() + x * 4 + 3] >= 128 ? 1.f : 0.f);
            // Horizontal scenery transmits through a masked column if any
            // height has an opening. Actors still sample the exact texel.
            for (int x = 0; x < width; ++x)
            {
                bool opaque = true;
                for (int y = 0; y < height && opaque; ++y)
                    opaque = pixels[y * bitmap.GetPitch() + x * 4 + 3] >= 128;
                data.push_back(opaque ? 1.f : 0.f);
            }
            for (int y = 0; y < height; ++y)
            {
                bool opaque = true;
                for (int x = 0; x < width && opaque; ++x)
                    opaque = pixels[y * bitmap.GetPitch() + x * 4 + 3] >= 128;
                data.push_back(opaque ? 1.f : 0.f);
            }
        }
        else alphaIndex = found->second;
        int p = Append({ w.glseg.x1, w.glseg.y1, w.glseg.x2, w.glseg.y2,
            w.ztop[0], w.ztop[1], w.zbottom[0], w.zbottom[1],
            w.tcs[HWWall::UPLFT].u, w.tcs[HWWall::UPRGT].u,
            w.tcs[HWWall::UPLFT].v, w.tcs[HWWall::UPRGT].v,
            w.tcs[HWWall::LOLFT].v, w.tcs[HWWall::LORGT].v,
            float(alphaIndex), w.Sprite ? 1.f : 0.f,
            w.seg && (w.seg->cstat & CSTAT_WALL_ROTATE_90) ? 1.f : 0.f,
            w.tcs[HWWall::LOLFT].u, w.tcs[HWWall::LORGT].u, 0.f });
        if (w.Sprite) spriteMasks[sectindex(w.frontsector)].push_back(p);
        else if (w.seg) data[wallBase + wallindex(w.seg) * 8 + 5] = float(p);
    }
    for (unsigned i = 0; i < sector.Size(); ++i)
    {
        data[i * 12 + 8] = float(data.size());
        data[i * 12 + 9] = float(spriteMasks[i].size());
        for (int index : spriteMasks[i]) data.push_back(float(index));
    }
    data.resize(((data.size() + DataWidth - 1) / DataWidth) * DataWidth, 0.f);
    if (!screen->mIsometricVisibilityTexture || data != uploaded)
    {
        // Raw BGRA8 transport preserves all IEEE float bits, with no palette,
        // gamma, filtering, mipmapping or texture replacement conversion.
        screen->mIsometricVisibilityTexture.reset(screen->CreateHardwareTexture(4));
        screen->mIsometricVisibilityTexture->CreateTexture(reinterpret_cast<uint8_t*>(data.data()),
            DataWidth, int(data.size() / DataWidth), 13, false, "IsoRaze visibility geometry");
        uploaded = data;
        ++dataUploads;
    }
    state.mVisibilityTexture = screen->mIsometricVisibilityTexture.get();
    auto& eye = di.Viewpoint.VisibilityPos;
    lastEye = { eye.X, -eye.Y, -eye.Z };
    lastEyeSector = di.Viewpoint.VisibilitySector;
    u.mVisibilityOrigin = { float(eye.X), float(eye.Z), float(eye.Y), float(di.Viewpoint.VisibilitySector + 1) };
    float feather = std::isfinite(float(iso_debug_visibility_feather)) ? clamp(float(iso_debug_visibility_feather), 0.f, 64.f) : 12.f;
    u.mVisibilityParams = { feather, float(DataWidth), float(clamp(int(iso_debug_visibility_view), 0, 1)), iso_debug_visibility_horizontal ? 1.f : 0.f };
    di.vpIndex = screen->mViewpoints->SetViewpoint(state, &u);
}

void BindIsometricGeometry(HWDrawInfo& di, FRenderState& state, sectortype* sec,
    sectortype* alternate, int kind, int cutawayPass)
{
    if (!di.Viewpoint.Isometric) return;
    // Portal/camera rendering can change the global render state's bindings.
    // Rebind the mask belonging to this ordinary view, never its own prepass.
    state.mCutawayCoverageTexture = di.VPUniforms.mCutawayParams.W > 0.f && screen->mIsometricCutawayCoverage ?
        screen->mIsometricCutawayCoverage->GetHardwareTexture(0, 0) : nullptr;
    int a = sec ? sectindex(sec) : -1, b = alternate ? sectindex(alternate) : -1;
    // This cache is per scene, shared only by this ordinary isometric view.
    uint64_t key = uint64_t(a + 2) | (uint64_t(b + 2) << 16) |
        (uint64_t(kind) << 32) | (uint64_t(cutawayPass) << 40) |
        (uint64_t(di.VPUniforms.mCutawayParams.W < 0.f ? 1 : di.VPUniforms.mCutawayParams.W > 0.f ? 2 : 0) << 48);
    auto found = viewpoints.find(key);
    int index;
    if (found != viewpoints.end()) index = found->second;
    else
    {
        auto u = di.VPUniforms;
        u.mVisibilityTarget = { float(a), float(b), float(kind), 0 };
        u.mCutawayParams.X = float(cutawayPass);
        index = screen->mViewpoints->SetViewpoint(state, &u);
        viewpoints[key] = index;
    }
    screen->mViewpoints->Bind(state, index);
}

CCMD(iso_debug_visibility_stats)
{
    Printf("IsoRaze eye mask: enabled=%d, geometry floats=%zu, uploads=%u, masked occluders=%zu, inward feather=%.2f, horizontal scenery=%d.\n",
        !!iso_debug_visibility, data.size(), dataUploads, occluders.size(), float(iso_debug_visibility_feather), !!iso_debug_visibility_horizontal);
}

CCMD(iso_debug_visibility_probe)
{
    if (lastEyeSector < 0 || gamestate != GS_LEVEL || argv.argc() != 5)
    {
        Printf("iso_debug_visibility_probe <world x> <world y> <world z> <sector>: native geometric LOS reference.\n");
        return;
    }
    int index = atoi(argv[4]);
    DVector3 target(atof(argv[1]), atof(argv[2]), atof(argv[3]));
    if (index < 0 || unsigned(index) >= sector.Size() || unsigned(lastEyeSector) >= sector.Size()) return;
    Printf("Eye-mask reference: eye=(%.3f,%.3f,%.3f), sector=%d; target=(%.3f,%.3f,%.3f), sector=%d; native geometric LOS=%s.\n",
        lastEye.X, lastEye.Y, lastEye.Z, lastEyeSector, target.X, target.Y, target.Z, index,
        cansee(lastEye, &sector[lastEyeSector], target, &sector[index]) ? "VISIBLE" : "HIDDEN");
}

void AddIsometricShotTrail(const DVector3& start, const DVector3& end)
{
    if (!render_isometric_debug_enabled() || !iso_debug_tracers || gamestate != GS_LEVEL) return;
    if (!std::isfinite(end.X) || !std::isfinite(end.Y) || !std::isfinite(end.Z) ||
        (end - start).LengthSquared() > 1e12) return;
    if (PlayClock < lastTrailTick) trails.clear();
    lastTrailTick = PlayClock;
    if (trails.size() >= 128) trails.erase(trails.begin());
    trails.push_back({ start, end, PlayClock });
}

void BuildIsometricShotTrails(HWDrawInfo& di)
{
    trailDraws.clear();
    if (PlayClock < lastTrailTick) trails.clear();
    lastTrailTick = PlayClock;
    trails.erase(std::remove_if(trails.begin(), trails.end(), [](const Trail& t)
        { return PlayClock - t.tick >= 14 || PlayClock < t.tick; }), trails.end());
    if (!iso_debug_tracers) return;
    double yaw = di.Viewpoint.HWAngles.Yaw.Radians();
    double pitch = di.Viewpoint.HWAngles.Pitch.Radians();
    DVector3 view(-sin(yaw) * cos(pitch), -sin(pitch), -cos(yaw) * cos(pitch));
    for (auto& trail : trails)
    {
        DVector3 a(trail.start.X, -trail.start.Z, -trail.start.Y);
        DVector3 b(trail.end.X, -trail.end.Z, -trail.end.Y);
        auto d = b - a;
        if (d.LengthSquared() < 0.25) continue;
        DVector3 side(d.Y * view.Z - d.Z * view.Y, d.Z * view.X - d.X * view.Z, d.X * view.Y - d.Y * view.X);
        if (side.LengthSquared() < 1e-8) side = { cos(yaw), 0., -sin(yaw) };
        side *= (di.Viewpoint.OrthographicHeight / max(1, screen->mSceneViewport.height) * 0.8) / side.Length();
        auto ret = screen->mVertexData->AllocVertices(4);
        DVector3 points[] = { a - side, a + side, b - side, b + side };
        for (int i = 0; i < 4; ++i)
            ret.first[i].Set(float(points[i].X), float(points[i].Y), float(points[i].Z), 0.f, 0.f);
        trailDraws.push_back({ int(ret.second), 1.f - float(PlayClock - trail.tick) / 14.f });
    }
}

void DrawIsometricShotTrails(HWDrawInfo& di, FRenderState& state)
{
    if (trailDraws.empty()) return;
    BindIsometricGeometry(di, state, nullptr, nullptr, 3);
    state.SetDepthMask(false);
    state.SetDepthFunc(DF_LEqual);
    state.SetRenderStyle(STYLE_Add);
    state.EnableTexture(false);
    state.EnableFog(false);
    state.SetObjectColor(0xffffffff);
    state.SetAddColor(0);
    state.SetDynLight(0, 0, 0);
    state.SetLightParms(1, 0);
    state.SetNoSoftLightLevel();
    state.SetLightIndex(-1);
    state.AlphaFunc(Alpha_GEqual, 0.f);
    for (auto& draw : trailDraws)
    {
        state.SetColor(1.f, 0.85f, 0.45f, draw.alpha);
        state.Draw(DT_TriangleStrip, draw.index, 4);
    }
    state.ResetColor();
    state.EnableTexture(true);
    state.EnableFog(true);
    state.SetRenderStyle(STYLE_Translucent);
    state.SetDepthMask(true);
    screen->mViewpoints->Bind(state, di.vpIndex);
}
