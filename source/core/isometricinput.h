#pragma once

#include "vectors.h"
#include "intrect.h"

class DCoreActor;
class VSMatrix;
struct FRenderViewpoint;
struct event_t;

// Main-view projection only. Security-camera textures must not replace this.
void UpdateIsometricInputView(const FRenderViewpoint& vp, const VSMatrix& view,
	const VSMatrix& projection, const IntRect& scene, const IntRect& output);
void ResetIsometricInput();
void UpdateIsometricInputState();
bool IsometricControlsActive(DCoreActor* actor = nullptr);
void MoveIsometricCursor(float dx, float dy);
bool IsometricInputResponder(const event_t* ev);
bool GetIsometricAim(DAngle& yaw);
DVector2 IsometricMovement(const DVector2& screenDirection);
void DrawIsometricCursor();
