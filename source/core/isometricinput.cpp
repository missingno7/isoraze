// Input adapter for the diagnostic Duke isometric view. Game commands continue
// through the original simulation, collision, weapon and autoaim paths.
#include "isometricinput.h"
#include "render.h"
#include "hw_drawinfo.h"
#include "coreactor.h"
#include "gamecontrol.h"
#include "automap.h"
#include "v_video.h"
#include "v_draw.h"
#include "texturemanager.h"
#include "buildtiles.h"
#include "gamestate.h"
#include "coreplayer.h"
#include "d_eventbase.h"
#include "keydef.h"

#include <cmath>

CVARD(Bool, iso_debug_controls, true, 0, "screen-relative movement and cursor aiming in diagnostic isometric mode")
CVARD(Float, iso_debug_cursor_speed, 1.f, 0, "isometric cursor speed relative to mouse sensitivity")
CVARD(Float, iso_debug_orbit_speed, 0.15f, 0, "camera orbit degrees per sensitized mouse count")
EXTERN_CVAR(Float, crosshairscale)
EXTERN_CVAR(Bool, invertmouse)
EXTERN_CVAR(Bool, invertmousex)
EXTERN_CVAR(Float, iso_debug_scale)
EXTERN_CVAR(Float, iso_debug_yaw)
EXTERN_CVAR(Float, iso_debug_pitch)

namespace
{
	struct InputView
	{
		bool valid = false, cursorReady = false;
		bool orbiting = false, middleCaptured = false, reprojectAim = false;
		DAngle orbitAim = nullAngle;
		double orbitCursorRadius = 0.;
		DVector2 afterOrbitDelta{};
		DCoreActor* actor = nullptr; // compared only; never dereferenced after a level change
		DVector2 origin{}, axisX{}, axisY{}, cursorUV{};
		IntRect bounds{};
		double determinant = 0;
	} inputView;

	DVector2 CursorPosition()
	{
		return { inputView.bounds.left + inputView.cursorUV.X * inputView.bounds.width,
			inputView.bounds.top + inputView.cursorUV.Y * inputView.bounds.height };
	}

	void SetCursorPosition(const DVector2& position)
	{
		inputView.cursorUV = {
			clamp((position.X - inputView.bounds.left) / inputView.bounds.width, 0., 1.),
			clamp((position.Y - inputView.bounds.top) / inputView.bounds.height, 0., 1.) };
	}

	DVector2 ScreenToWorld(const DVector2& delta)
	{
		const auto& x = inputView.axisX;
		const auto& y = inputView.axisY;
		return { (delta.X * y.Y - delta.Y * y.X) / inputView.determinant,
			(x.X * delta.Y - x.Y * delta.X) / inputView.determinant };
	}

	bool HasControls()
	{
		auto player = PlayerArray[myconnectindex];
		return iso_debug_controls && render_isometric_debug_enabled() && inputView.valid &&
			gamestate == GS_LEVEL && automapMode == am_off && player && inputView.actor == player->GetActor();
	}

	void CancelOrbit()
	{
		if (inputView.orbiting)
		{
			inputView.orbiting = false;
			inputView.reprojectAim = true;
		}
	}

	void ReprojectOrbitCursor()
	{
		auto heading = inputView.orbitAim.ToVector();
		auto direction = inputView.axisX * heading.X + inputView.axisY * heading.Y;
		direction /= direction.Length();
		double radius = inputView.orbitCursorRadius;
		// Shorten the ray at the viewport edge instead of clamping X/Y
		// independently, which would bend the held world-space aim direction.
		for (int i = 0; i < 2; ++i)
		{
			double start = i ? inputView.bounds.top : inputView.bounds.left;
			double size = i ? inputView.bounds.height : inputView.bounds.width;
			if (direction[i] > 1.e-8) radius = min(radius, (start + size - inputView.origin[i]) / direction[i]);
			else if (direction[i] < -1.e-8) radius = min(radius, (start - inputView.origin[i]) / direction[i]);
		}
		SetCursorPosition(inputView.origin + direction * max(radius, 0.) + inputView.afterOrbitDelta);
		inputView.afterOrbitDelta = {};
		inputView.reprojectAim = false;
	}
}

void ResetIsometricInput()
{
	inputView = {};
}

void UpdateIsometricInputState()
{
	if (!HasControls() || paused || System_WantGuiCapture() || !AppActive) CancelOrbit();
}

void UpdateIsometricInputView(const FRenderViewpoint& vp, const VSMatrix& view,
	const VSMatrix& projection, const IntRect& scene, const IntRect& output)
{
	if (!vp.Isometric || !iso_debug_controls || !vp.CameraActor ||
		scene.width <= 0 || scene.height <= 0 || output.width <= 0 || output.height <= 0 ||
		viewport3d.width <= 0 || viewport3d.height <= 0)
	{
		ResetIsometricInput();
		return;
	}

	if (inputView.actor != vp.CameraActor) ResetIsometricInput();
	bool initialize = !inputView.cursorReady;
	inputView.actor = vp.CameraActor;
	inputView.bounds = viewport3d;
	auto actor = vp.CameraActor;
	auto pos = actor->interpolatedpos(vp.TicFrac);
	// Aim from the middle of the body, at a fixed horizontal height. Existing
	// autoaim supplies vertical assistance; this is not an external-camera raycast.
	double height = 40., offset = 0.;
	if (TexMan.GetGameTexture(actor->spr.spritetexture()))
		offset = vp.CameraActor->GetOffsetAndHeight(height);
	double aimZ = pos.Z + offset - height * 0.5 - actor->spr.yoffset * actor->spr.scale.Y;
	VSMatrix worldToClip = projection;
	worldToClip.multMatrix(view);
	auto project = [&](FVector4 point, bool direction)
	{
		FVector4 clip;
		worldToClip.multMatrixPoint(&point.X, &clip.X);
		double sx = double(screen->GetWidth()) / output.width;
		double sy = double(screen->GetHeight()) / output.height;
		if (direction) return DVector2(clip.X * scene.width * 0.5 * sx, -clip.Y * scene.height * 0.5 * sy);
		return DVector2((scene.left + (clip.X / clip.W + 1.) * scene.width * 0.5 - output.left) * sx,
			(output.top + output.height - scene.top - (clip.Y / clip.W + 1.) * scene.height * 0.5) * sy);
	};
	inputView.origin = project({ float(pos.X), float(-aimZ), float(-pos.Y), 1.f }, false);
	inputView.axisX = project({ 1.f, 0.f, 0.f, 0.f }, true);
	inputView.axisY = project({ 0.f, 0.f, -1.f, 0.f }, true);
	inputView.determinant = inputView.axisX.X * inputView.axisY.Y - inputView.axisX.Y * inputView.axisY.X;
	inputView.valid = std::isfinite(inputView.determinant) && fabs(inputView.determinant) > 1e-8;
	if (!inputView.valid) return;
	if (initialize)
	{
		auto forward = actor->spr.Angles.Yaw.ToVector();
		auto screenForward = inputView.axisX * forward.X + inputView.axisY * forward.Y;
		screenForward *= max(48., screen->GetHeight() * 0.16) / screenForward.Length();
		SetCursorPosition(inputView.origin + screenForward);
		inputView.cursorReady = true;
	}
	UpdateIsometricInputState();
	if (inputView.orbiting || inputView.reprojectAim) ReprojectOrbitCursor();
}

bool IsometricControlsActive(DCoreActor* actor)
{
	return HasControls() && (!actor || inputView.actor == actor);
}

void MoveIsometricCursor(float dx, float dy)
{
	if (!HasControls() || paused || System_WantGuiCapture() || !AppActive)
	{
		CancelOrbit();
		return;
	}
	if (dx == 0.f && dy == 0.f) return;
	// EV_Mouse has FPS inversion and upward-positive Y already applied. A cursor
	// follows physical mouse direction independently of the FPS invert setting.
	double speed = std::isfinite(float(iso_debug_cursor_speed)) ? clamp(double(iso_debug_cursor_speed), 0.05, 10.) : 1.;
	DVector2 delta{ dx * (invertmousex ? -1. : 1.), -dy * (invertmouse ? -1. : 1.) };
	if (inputView.orbiting)
	{
		double orbitSpeed = std::isfinite(float(iso_debug_orbit_speed)) ? clamp(double(iso_debug_orbit_speed), 0.01, 5.) : 0.15;
		double yaw = std::isfinite(float(iso_debug_yaw)) ? double(iso_debug_yaw) : 45.;
		double pitch = std::isfinite(float(iso_debug_pitch)) ? double(iso_debug_pitch) : 55.;
		yaw = fmod(yaw + delta.X * orbitSpeed, 360.);
		iso_debug_yaw = float(yaw < 0. ? yaw + 360. : yaw);
		iso_debug_pitch = float(clamp(pitch - delta.Y * orbitSpeed, 20., 75.));
		inputView.reprojectAim = true;
		return;
	}
	// Cursor coordinates use the emulated screen; raw mouse counts do not.
	// Keep physical cursor speed stable when render scale or letterboxing changes.
	delta.X *= double(screen->GetWidth()) / max(1, screen->mOutputLetterbox.width);
	delta.Y *= double(screen->GetHeight()) / max(1, screen->mOutputLetterbox.height);
	if (inputView.reprojectAim) inputView.afterOrbitDelta += delta * speed;
	else SetCursorPosition(CursorPosition() + delta * speed);
}

bool IsometricInputResponder(const event_t* ev)
{
	// A captured release must not leak to a game binding after interruption.
	if (ev->type == EV_KeyUp && ev->data1 == KEY_MOUSE3 && inputView.middleCaptured)
	{
		CancelOrbit();
		inputView.middleCaptured = false;
		return true;
	}
	if (!HasControls() || System_WantGuiCapture() || !AppActive)
	{
		CancelOrbit();
		return false;
	}
	if (ev->type == EV_Mouse)
	{
		MoveIsometricCursor(ev->x, ev->y);
		return true;
	}
	if (ev->type != EV_KeyDown && ev->type != EV_KeyUp) return false;
	if (ev->data1 == KEY_MOUSE3)
	{
		if (ev->type != EV_KeyDown) return false;
		inputView.middleCaptured = true;
		if (!inputView.orbiting && !paused)
		{
			if (inputView.reprojectAim) ReprojectOrbitCursor();
			DAngle aim = PlayerArray[myconnectindex]->GetActor()->spr.Angles.Yaw;
			GetIsometricAim(aim);
			inputView.orbitAim = aim;
			inputView.orbitCursorRadius = (CursorPosition() - inputView.origin).Length();
			inputView.orbiting = true;
			inputView.reprojectAim = true;
		}
		return true;
	}
	if (ev->data1 != KEY_MWHEELUP && ev->data1 != KEY_MWHEELDOWN) return false;
	// Consume both halves before weapon bindings. Menus, automap and FPS keep
	// their original wheel handling. Zoom changes presentation only.
	if (ev->type == EV_KeyDown)
	{
		float scale = std::isfinite(float(iso_debug_scale)) ? float(iso_debug_scale) : 640.f;
		iso_debug_scale = clamp(scale * (ev->data1 == KEY_MWHEELUP ? 1.f / 1.1f : 1.1f), 192.f, 2048.f);
	}
	return true;
}

bool GetIsometricAim(DAngle& yaw)
{
	if (!HasControls()) return false;
	if (inputView.orbiting || inputView.reprojectAim)
	{
		yaw = inputView.orbitAim;
		return true;
	}
	auto delta = CursorPosition() - inputView.origin;
	if (delta.LengthSquared() < pow(max(4., screen->GetHeight() / 180.), 2.)) return false;
	yaw = ScreenToWorld(delta).Angle();
	return true;
}

DVector2 IsometricMovement(const DVector2& screenDirection)
{
	if (!HasControls() || screenDirection.isZero()) return {};
	auto world = ScreenToWorld(screenDirection);
	return world * (min(screenDirection.Length(), 1.) / world.Length());
}

void DrawIsometricCursor()
{
	if (!HasControls() || System_WantGuiCapture()) return;
	auto cursor = CursorPosition();
	auto tex = TexMan.FindGameTexture("CROSSHAIR", ETextureType::Any);
	if (!tex || !tex->isValid()) return;
	double scale = min(inputView.bounds.width / 320., inputView.bounds.height / 200.) *
		(crosshairscale > 0.f ? float(crosshairscale) : 1.f);
	DrawTexture(twod, tex, cursor.X, cursor.Y, DTA_ScaleX, scale, DTA_ScaleY, scale,
		DTA_CenterOffsetRel, true, TAG_DONE);
}

CCMD(iso_debug_cursor)
{
	if (HasControls() && argv.argc() == 2 && !stricmp(argv[1], "player"))
	{
		SetCursorPosition(inputView.origin);
		return;
	}
	if (!HasControls() || argv.argc() != 3)
	{
		Printf("iso_debug_cursor <x 0..1> <y 0..1> | player: place the cursor in the game view.\n");
		return;
	}
	double x = atof(argv[1]), y = atof(argv[2]);
	if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1) return;
	inputView.cursorUV = { x, y };
}

CCMD(iso_debug_input_stats)
{
	auto player = PlayerArray[myconnectindex];
	if (gamestate != GS_LEVEL || !player || !player->GetActor())
	{
		Printf("No active player for input statistics.\n");
		return;
	}
	auto cursor = CursorPosition();
	DAngle aim = nullAngle;
	bool aiming = GetIsometricAim(aim);
	auto actor = player->GetActor();
	Printf("IsoRaze input: active=%d, cursor=(%.2f, %.2f), body=(%.2f, %.2f), aim=%.3f, deadzone=%d, paused=%d, gui=%d, focus=%d\n"
		"  player=(%.3f, %.3f, %.3f), yaw=%.3f, pitch=%.3f, command XY=(%.5f, %.5f), world-input=%d, fire=%d\n",
		HasControls(), cursor.X, cursor.Y, inputView.origin.X, inputView.origin.Y, aim.Degrees(), !aiming,
		paused, System_WantGuiCapture(), AppActive,
		actor->spr.pos.X, actor->spr.pos.Y, actor->spr.pos.Z, actor->spr.Angles.Yaw.Degrees(),
		actor->spr.Angles.Pitch.Degrees(), player->cmd.ucmd.vel.X, player->cmd.ucmd.vel.Y,
		!!(player->cmd.ucmd.actions & SB_ISOMETRIC_MOVE), !!(player->cmd.ucmd.actions & SB_FIRE));
	Printf("  camera yaw=%.3f, pitch=%.3f, zoom=%.3f; orbit=%d, captured=%d, reproject=%d\n",
		float(iso_debug_yaw), float(iso_debug_pitch), float(iso_debug_scale),
		inputView.orbiting, inputView.middleCaptured, inputView.reprojectAim);
}

CCMD(iso_debug_middle)
{
	if (argv.argc() != 2) return;
	event_t ev{};
	ev.type = atoi(argv[1]) ? EV_KeyDown : EV_KeyUp;
	ev.data1 = KEY_MOUSE3;
	D_PostEvent(&ev);
}

CCMD(iso_debug_mouse)
{
	if (!HasControls() || argv.argc() != 3)
	{
		Printf("iso_debug_mouse <dx> <dy>: inject a relative mouse event for diagnostics.\n");
		return;
	}
	PostMouseMove(clamp(atoi(argv[1]), -10000, 10000), clamp(atoi(argv[2]), -10000, 10000));
}

CCMD(iso_debug_wheel)
{
	if (!HasControls() || argv.argc() != 2) return;
	int notches = clamp(atoi(argv[1]), -32, 32);
	for (int i = 0; i < abs(notches); ++i)
	{
		event_t ev{};
		ev.type = EV_KeyDown;
		ev.data1 = notches > 0 ? KEY_MWHEELUP : KEY_MWHEELDOWN;
		D_PostEvent(&ev);
		ev.type = EV_KeyUp;
		D_PostEvent(&ev);
	}
}

CCMD(iso_debug_input_check)
{
	if (!HasControls())
	{
		Printf("Enable iso_debug in a Duke level before checking input projection.\n");
		return;
	}
	double speedError = 0., movementError = 0., aimError = 0.;
	DVector2 directions[] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 },
		{ 1, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 } };
	for (const auto& direction : directions)
	{
		auto world = IsometricMovement(direction);
		auto projected = inputView.axisX * world.X + inputView.axisY * world.Y;
		auto expected = direction / direction.Length();
		speedError = max(speedError, fabs(world.Length() - 1.));
		movementError = max(movementError, (projected / projected.Length() - expected).Length());
		auto heading = world.Angle().ToVector();
		auto aimProjection = inputView.axisX * heading.X + inputView.axisY * heading.Y;
		aimError = max(aimError, (aimProjection / aimProjection.Length() - expected).Length());
	}
	// DAngle::ToVector uses the engine's interpolated float sine table. Allow
	// its round-trip precision here; the projection inverse stays double-precise.
	bool pass = speedError < 1e-8 && movementError < 1e-8 && aimError < 1e-6;
	Printf("IsoRaze input check: eight screen directions, diagonal speed and aim projection=%s.\n", pass ? "PASS" : "FAIL");
	Printf("  maximum errors: speed=%.3g, movement=%.3g, aim=%.3g\n", speedError, movementError, aimError);
}
