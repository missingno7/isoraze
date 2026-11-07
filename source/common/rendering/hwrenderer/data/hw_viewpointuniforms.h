#pragma once

#include "matrix.h"

struct HWDrawInfo;

enum class ELightBlendMode : uint8_t
{
	CLAMP = 0,
	CLAMP_COLOR = 1,
	NOCLAMP = 2,

	DEFAULT = CLAMP,
};

struct HWViewpointUniforms
{
	VSMatrix mProjectionMatrix;
	VSMatrix mViewMatrix;
	VSMatrix mNormalViewMatrix;
	FVector4 mCameraPos;
	FVector4 mLightingPos{}; // w=1: observer-centred radial attenuation; w=0: original FPS depth
	FVector4 mCutawayTop{}; // view-space body endpoint, w=outer radius in world units
	FVector4 mCutawayBottom{}; // view-space body endpoint, w=inner/outer radius
	FVector4 mCutawayParams{}; // x: 0=off, 1=solid, 2=edge, 3=translucent; y=strength; z=debug; w:-1=coverage, 1=coverage-gated
	FVector4 mVisibilityOrigin{}; // eye xyz, w=sector+1 (0 disables the mask)
	FVector4 mVisibilityParams{}; // feather width, texture width, debug mode, reserved
	FVector4 mVisibilityTarget{}; // owning sector, alternate wall sector, kind, reserved
	FVector4 mClipLine;

	float mGlobVis = 1.f;
	int mPalLightLevels = 0;
	int mViewHeight = 0;
	float mClipHeight = 0.f;
	float mClipHeightDirection = 0.f;
	int mShadowmapFilter = 1;

	int mLightBlendMode = 0;

	void CalcDependencies()
	{
		mNormalViewMatrix.computeNormalMatrix(mViewMatrix);
	}
};



