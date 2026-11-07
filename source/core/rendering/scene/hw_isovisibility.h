#pragma once

#include "vectors.h"
struct HWDrawInfo;
class HWWall;
class FRenderState;
struct sectortype;

void BeginIsometricVisibility();
void CollectIsometricOccluder(const HWWall& wall);
void BuildIsometricVisibility(HWDrawInfo& di);
void BindIsometricGeometry(HWDrawInfo& di, FRenderState& state, sectortype* sector,
    sectortype* alternate = nullptr, int kind = 0, int cutawayPass = 0);
void AddIsometricShotTrail(const DVector3& start, const DVector3& end);
void BuildIsometricShotTrails(HWDrawInfo& di);
void DrawIsometricShotTrails(HWDrawInfo& di, FRenderState& state);
