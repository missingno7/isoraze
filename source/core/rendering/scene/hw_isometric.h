#pragma once

struct FRenderViewpoint;
struct HWDrawInfo;
class HWWall;
class HWFlat;
class FRenderState;

// Diagnostic scene builder. Candidate sectors are not precise player visibility.
void SetupIsometricDebugView(FRenderViewpoint& viewpoint);
void CreateIsometricDebugScene(HWDrawInfo& di);
void SetupIsometricCutaway(HWDrawInfo& di);
bool IsometricCutawayWall(HWDrawInfo& di, const HWWall& wall);
bool IsometricCutawayFlat(HWDrawInfo& di, HWFlat& flat);
void RenderIsometricCutawayCoverage(HWDrawInfo& di, FRenderState& state);
