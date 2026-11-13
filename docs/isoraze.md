# IsoRaze implementation notes

## Agreed initial behavior

- Scenery uses a 360-degree horizontal visibility footprint from the player.
  Visible terrain and boundary walls keep their full height. Actors and items
  retain height-aware player-eye checks. This supersedes the earlier strict-eye
  scenery experiment after E1L1's rooftop lost its building silhouette.
- Camera cutaways must never widen that visibility.
- Boundary walls directly touching admitted space show an existing exterior.
  One-sided walls keep their original facing; no reverse face is invented. This
  admits the shared wall only, not the adjoining room's floors or actors.
- Remembering explored terrain is deferred. Hidden actors must not remain live in remembered areas.
- WASD movement is screen relative in the input prototype. Mouse position determines the direction from the player to the cursor; existing game autoaim remains responsible for assistance.
- Game simulation, collision, damage, weapon behavior and maps remain authoritative.
- Duke is the first target; Blood adapters and continuous sector connections come later. TROR is out of scope.

## First increment: diagnostic camera and scene builder

`iso_debug 1` selects an experimental orthographic scene for the ordinary Duke
player view. It is disabled by default and is not saved in configuration files.
Security cameras, camera textures and the other games retain their normal path.
`iso_debug 0` returns to the original first-person scene.
The original HUD crosshair is suppressed while the diagnostic is enabled, without
changing the saved crosshair setting for first-person mode.

The first increment below describes the original coarse diagnostic. The current
build also has the horizontal scenery / height-aware actor mask described in
"Visibility and shot-trail follow-up" below. It remains experimental.

The original full-circle Build
traversal supplies potentially visible sections. The new scene builder then
submits their floors, wall sides and sprites using Raze's existing
geometry cache, materials, palette/shade handling and GPU backend. Whole candidate
sections can expose hidden portions and actors. Ceilings are omitted from this
diagnostic presentation; they must remain occluders in the future visibility pass.

Boundary exteriors are enabled by default. A two-sided boundary uses the opposite
wall's material when its sector is outside the candidate region. A one-sided
boundary is culled from behind: presenting its original texture on an invented
reverse face made tall street-side walls obscure the camera. This fixes face
selection without introducing a height-based cutaway policy.
This is currently applied to coarse candidates; precise visibility must later
constrain the allowed boundary fragments.

Diagnostic controls:

| Console variable/command | Meaning |
| --- | --- |
| `iso_debug 0` / `1` | Original scene / diagnostic isometric scene |
| `iso_debug_yaw 45` | Camera yaw in degrees |
| `iso_debug_pitch 55` | Downward camera pitch, constrained to 20–75 degrees |
| `iso_debug_scale 640` | Orthographic view height in Raze world units; smaller values zoom in |
| `iso_debug_boundary_walls 1` | Include exterior wall faces at candidate boundaries; `0` allows comparison |
| `iso_debug_eye_lighting 1` | Anchor distance shade/fog at the player eye; `0` compares camera-based attenuation |
| `iso_debug_cutaway 1` | Enable the soft opening on surfaces covering the player sprite; `0` allows comparison |
| `iso_debug_cutaway_radius 64` | Outer capsule radius in world units; scales with orthographic zoom |
| `iso_debug_cutaway_inner 0.5` | Fully clear inner radius as a fraction of the outer radius |
| `iso_debug_controls 1` | Screen-relative movement and mouse cursor aiming; `0` retains the original input mapping |
| `iso_debug_cursor_speed 1` | Cursor speed multiplier on the existing mouse sensitivity |
| `iso_debug_orbit_speed 0.15` | Camera degrees per sensitized mouse count while middle-dragging |
| `iso_debug_stats` | Player-eye origin, candidate section count/hash and submission counts |
| `iso_debug_check` | Repeats candidate collection and checks stability and preservation of seen/mapped flags |
| `iso_debug_input_stats` | Cursor/body projection, desired/actual yaw and player command axes |
| `iso_debug_input_check` | Checks eight projected movement/aim directions and normalized diagonal speed |
| `iso_debug_cursor x y` / `player` | Diagnostic cursor placement, normalized to the game viewport, or at the player |
| `iso_debug_mouse dx dy` | Diagnostic event injection through the native relative-mouse event path |

The observer is captured before the presentation camera is displaced. A canonical
360-degree candidate traversal makes its hash independent of camera yaw, pitch and
zoom. It does not update automap flags or submit render items. The diagnostic
presentation also suppresses the usual seen/mapped writes: those flags can affect
weather and game triggers, so their eventual compatibility policy needs a separate
implementation. Sprite animation callbacks remain the existing game callbacks.

Distance shade and fog use radial distance from the player eye in the diagnostic
view, avoiding attenuation caused solely by moving the camera outside the world.
The physical camera position remains separate for view-dependent effects. Normal
perspective views retain their existing lighting equations. This does not claim
pixel equivalence with the original facing-dependent lighting at every angle.

### Soft foreground-surface cutaway

Cutaway selection first checks whether a submitted surface actually overlaps
opaque pixels of the animated local-player sprite and is closer to the camera
at that same screen position. The actual billboard vertices, animation frame,
UV flips and offsets define the silhouette; shadows and attached effects do
not. Surface triangles are clipped against opaque artwork runs, with depth
interpolated through the clipped polygon. Masked surface artwork must also
overlap an opaque player pixel. Transparent corners and holes do not qualify.
This CPU overlap test needs no GPU readback and is shared by the hardware
backends. Surface submission is deferred until sprite
processing has resolved the local player's presentation.

Only selected surfaces receive the larger screen-plane capsule around the
interpolated player body. Its center is clear and its edge uses a smooth opacity
gradient. The existing per-fragment depth limit keeps the opening on the camera
side of the body. Wall direction does not determine selection. Every qualifying
layer receives the same larger gradient, including layers hidden behind another
blocker; a nearby surface which never overlaps the sprite remains solid. This
includes sky-transition wall pieces, wall-aligned sprites, sector floors and
floor sprites, including sloped surfaces. Ordinary actor billboards are
unaffected. A small depth feather softens
crossings of the player plane. Toggling the opening eases over 150 ms.

Before drawing that opening, a coverage pass renders the already-collected
scene to a viewport-sized canvas with every selected blocker absent. Permitted
geometry and actor pixels write white after artwork alpha tests; the empty
background stays black. Sky portals, cutaway edges and shot-trail effects do
not contribute coverage. Fully transparent pixels do not count. This tests
geometry coverage independently of its color, so genuinely dark scenery still
qualifies. It applies the same uncut-world player-eye mask as the ordinary scene.

The normal scene draw samples this canvas and retains the original surface's
color and depth wherever coverage is empty. Where real geometry remains, it
uses the existing larger gradient. This implements the original-scene fallback
without storing and compositing two complete color scenes. It needs one extra
geometry draw only when surfaces qualify, with no additional scene collection,
game updates or CPU readback. Camera/portal views and first-person rendering
keep their original paths. All blocking layers are absent together during the
coverage pass, so one cut wall cannot authorize a hole through another cut wall
into the void. The canvas resizes with the scene viewport and uses raw matching
projection coordinates on all backends.

The depth comparison is bounded by the nearest point on the actual body
capsule as well as the upright plane through Duke. Extending only that plane
below the feet could cut foundations farther away than the character, even
though they did not obstruct him. Taking the nearer of those two depth
references closes that leak without changing the capsule's soft edge or adding
a height-based wall policy. "Foreground" means the camera side of Duke,
independent of his aiming direction.

Walls and floors use the same finite-triangle/sprite overlap test. There is no
separate floor-plane or height-based selection rule. Flat ground and rear
surfaces do not qualify. A slope can qualify if it really intersects part of the
sprite (for example, a boot at the uphill side), but cannot open onto empty
background. This also handles a partially submerged sprite without identifying
water or adding a support-floor exception. Sloped floor edges reuse
the existing sloped-surface sorter; opaque surfaces retain their solid depth
pass outside the opening, while blended edges do not write depth. Sector portal
surfaces keep their existing rendering path.

Opaque wall fragments outside the opening keep their usual color/depth pass.
Fragments inside it do not write solid depth. A copy of the soft edge joins the
existing sorted translucent pass, with depth writes disabled. Existing translucent
walls multiply their opacity by the cutaway mask. Per-view uniform variants keep
the mask out of ordinary actor billboards, the HUD and first-person views. Security-camera
rendering does not reset the main view's transition.

The cutaway does not collect additional sections, floors or sprites and does not
change collision or weapon traces. The independent visibility mask continues
to authorize exposed scenery and actors against the uncut world. The capsule
still uses actor bounds to shape its gradient, while selection uses the animated
sprite silhouette. Model/voxel player replacements and portal surfaces are not
selected by this sprite test. Complex overlaps and the existing transparency
sorter's broader orthographic limitations still need coverage.

### Movement and mouse aiming prototype

Controls are enabled automatically with `iso_debug 1`. Existing movement bindings
are used: by default W/S move up/down the screen and A/D move left/right. Movement
is independent of character facing and follows changes to the camera orientation.
Diagonal movement has the same world-space speed as cardinal movement. Run,
autorun, jump, crouch, use and weapon buttons continue through their native paths;
no saved key bindings are overwritten. Left click fires with the default bindings.

Mouse motion moves the original game crosshair artwork, with its configured
scale, confined to the game view. The ordinary
center crosshair remains hidden. The cursor starts along the character's current
facing, retains its normalized position on viewport changes, and holds the last
facing inside a small dead zone around the body. It follows physical mouse
direction independently of FPS inversion. Mouse sensitivity remains configurable,
and render scaling does not change the cursor's physical speed. Mouse movement
is ignored during pause, menus and loss of focus. Native menu input remains active.

Mouse wheel up zooms in and wheel down zooms out in 10% steps, clamped to an
orthographic height of 192–2048 world units. The active isometric controls consume
both wheel press and release before weapon bindings, without changing saved
bindings. Menus, console, automap and first-person mode keep their native wheel
behavior. The cursor retains its normalized screen position through zoom;
movement and aiming use the updated projection. `iso_debug_wheel <notches>`
injects native wheel events for diagnostics (positive zooms in).
Hold middle mouse and drag horizontally to orbit around Duke, or drag up/down
to raise/lower the camera's viewing angle. Pitch stays within 20–75 degrees and
yaw wraps through a full turn. The view stays orthographic, keeps Duke as its
target, and keeps the same orbit distance at a given zoom. Mouse wheel remains
the independent zoom control. Camera drag uses physical mouse directions
regardless of FPS mouse inversion and has its own speed multiplier.

While dragging, Duke retains the world-space aim direction from the start of
the drag. The crosshair is reprojected through each new camera view along that
same direction, shortening its distance at viewport edges without changing
heading. Releasing middle mouse resumes cursor aiming without turning Duke to
a different direction. Motion arriving immediately after release is retained
until the new view has been projected. Screen-relative WASD uses the updated
main view as before.

Isometric mouse motion is queued in order with mouse-button events, so a short
press/move/release burst cannot send its movement to cursor aiming or invoke
the middle button's inventory binding. Ordinary FPS input retains its original
immediate mouse accumulation and bindings. Dragging cancels on pause, GUI
capture, automap, focus loss and input/view resets, and requires a fresh press
to begin again. `iso_debug_middle 1/0` injects native middle-button events for
diagnostics; `iso_debug_input_stats` also reports camera angles and drag state.

The inverse of the actual main-view projection maps screen directions onto a
horizontal plane through the player's body center. Mouse aiming supplies yaw and
neutral pitch through ordinary input commands. It does not select a target from
an external-camera ray. Existing autoaim settings, hit resolution and weapon
behavior remain authoritative. Aim and movement work with both synchronous and
uncapped input. Ordinary character billboards use their existing directional art.

The Duke input adapter tags world-space XY with `SB_ISOMETRIC_MOVE` (unused action
bit 30), so the simulation does not rotate movement a second time when the
character turns. Packet fields and serialization layout are unchanged. Switching
input modes clears pending axes in the previous coordinate system while retaining
action bits. These experimental command semantics require the modified engine;
vanilla-engine playback and multiplayer compatibility have not been validated.
Security-camera textures cannot replace the stored main-view projection. Normal
views and level/input resets clear it; other games retain their original input.
The adapter is inactive in the automap or when viewing another player/camera.

This input prototype was added at the user's request before the precise visibility
gate is complete. It is suitable for testing controls, not a claim that all visible
geometry, actors or game-trigger semantics are correct.

Current limitations: precise surface/object visibility, transformed portals and
mirrors, complete transparency sorting and smooth camera tracking are not
implemented by this increment. Complex surface overlaps, lighting and depth still
need broader coverage. Use this mode to inspect geometry and the new boundary,
not to judge gameplay correctness.

## Local build and isolated launch

The repository includes MSVC fallback libraries; no dependency bootstrap is needed
for the initial Windows configuration. Use an installed CMake or Visual Studio's
bundled copy if it is not on `PATH`:

```powershell
cmake -S D:\prog\isoraze -B D:\prog\isoraze\build-isoraze -G "Visual Studio 17 2022" -A x64 -DPK3_QUIET_ZIPDIR=ON
cmake --build D:\prog\isoraze\build-isoraze --config Release --parallel 1 -- /nr:false
Copy-Item D:\prog\isoraze\build-isoraze\raze.pk3 D:\prog\isoraze\build-isoraze\Release\raze.pk3
```

Stage required runtime DLLs beside the newly built executable. Keep `raze.pk3`
from that same build. Use explicit configuration, save, log and screenshot paths
under the build directory and load `D:\dos\duke3D\duke3d.grp` read-only:

```powershell
& D:\prog\isoraze\build-isoraze\Release\raze.exe -iwad D:\dos\duke3D\duke3d.grp -noautoload -noautoexec `
  -config D:\prog\isoraze\build-isoraze\run\raze.ini `
  -savedir D:\prog\isoraze\build-isoraze\run\saves `
  -shotdir D:\prog\isoraze\build-isoraze\run\shots -map E2L1 +iso_debug 1 +use_mouse 1
```

Create the runtime directories first. Do not copy outputs into the original game
directory. The existing executable in that directory is not a verified build of
this checkout.

## Verification completed on 2026-11-07

- Configured with Visual Studio 2022 and CMake 3.27.2. The Release build succeeded.
  MSBuild project parallelism stalled in this execution environment, so the build
  uses one MSBuild node (individual compiler jobs still use `/MP`). RelWithDebInfo
  hit MSVC C1902 program-database version errors; no source workaround was added.
- Built and launched the unchanged source baseline and captured E1L1 through Vulkan.
- Built the diagnostic increment and rendered the same original E1L1 through both
  OpenGL and Vulkan. The game reported the existing CON compiler warnings in both
  baseline and modified runs.
- With the player paused, changes to camera yaw, pitch and zoom retained 18
  candidate sections and hash `e70923a9` in both backends. The corrected diagnostic
  submitted 59 wall sides and prepared 50 temporary sprites at the tested origin.
  These are candidate/submission counts, not a claim of precise visible content.
- `iso_debug_check` passed repeatability and preservation of automap, sector-seen
  and sprite-mapped flags around candidate collection.
- The OpenGL screenshots immediately before enabling and after disabling the
  diagnostic were byte-identical in the paused scene (SHA-256
  `822E403BAC327C5D27A7316DAD068A233892F7341996AE8960EF71381D8F97F8`).
- Follow-up captures in Spaceport (E2L1), Bank Roll (E3L2) and Death Row (E1L3)
  exposed camera-distance darkening. Player-eye attenuation fixes that severe
  darkening in the tested scenes. Original shade, palette and materials remain
  active; the diagnostic does not force full brightness.
- Tested the boundary toggle while paused on all three maps through OpenGL:

  | Map | Candidate sections | Wall submissions, off → on | Exterior additions | Sprites, off/on |
  | --- | ---: | ---: | ---: | ---: |
  | E2L1 | 24 | 145 → 197 | 52 | 20 |
  | E3L2 | 7 | 30 → 42 | 12 | 14 |
  | E1L3 | 21 | 70 → 96 | 26 | 15 |

  Candidate hashes and sprite counts did not change when exterior faces were
  enabled. Candidate checks passed on each map. These counts describe the tested
  world snapshots, not a visibility guarantee or cross-run determinism test.
- The latest boundary/lighting path also launched successfully through Vulkan
  and the GLES renderer (desktop OpenGL 3 mode). Both rendered normal and indexed
  materials, and passed the candidate checks. Vulkan's separate Spaceport run
  admitted 23 sections and added 37 boundary faces; GLES admitted 24 and added 52.
  The runs were not synchronized to an identical simulation snapshot.
- After the lighting changes, paused Spaceport first-person captures before and
  after toggling the diagnostic and indexed materials were byte-identical within
  each backend. OpenGL SHA-256:
  `FE7C66B86A5DCD11C8E5FEE094E8FCC01B1B0920510870DA2B4BAEDAEFFBA85B`;
  Vulkan: `21D15E667D37366D295615597D988D23E91792A49B85AB3B550DD2D31B8A91D5`;
  GLES: `AE8C12B718151C6EAC90D3F68110D8B4CE893252950785CF2C64F8C467B184DF`.
  This checks restoration of the tested scene, not all first-person rendering.
- Lighting equivalence, live moving geometry and gameplay completion have not
  been validated. Death Row's resumed capture includes the original chair damage
  flash; foreground wall occlusion is also visible in the captures.
- Logs, isolated configuration and screenshots are under `build-isoraze/run`;
  build output is ignored by Git. No original game files were modified.

### Cutaway follow-up verification

- The Release build succeeded with the cutaway and crosshair changes. Native
  captures rendered through OpenGL, Vulkan and GLES (desktop OpenGL 3 mode),
  including indexed materials. No new shader or runtime errors were reported.
- Bank Roll exposed two overlapping normal walls at the player projection.
  One carries the sky-transition flag; excluding that flag left it opaque.
  Both layers now participate. Temporary coverage/depth probes were removed;
  there are no map-specific wall IDs in the implementation.
- In paused Bank Roll, all three backends retained 7 candidate sections, hash
  `f2a48205`, and 14 prepared sprites with the opening off/on. At yaw 45 the
  opening affected 15 wall parts. Yaws 135, 225 and 315 affected 9, 4 and 9 parts
  respectively, with pitches 45, 20 and 75 and view height 640. Candidate checks
  passed and the counts/hash remained stable across camera changes.
- Final paused OpenGL checks also passed in Spaceport (23 sections, hash
  `e151eda2`, 20 sprites, 25 cutaway wall parts) and Death Row (21 sections, hash
  `08470f36`, 16 sprites, 26 parts). Toggling the opening retained the candidate
  hash and sprite count in each scene. Live captures additionally show the
  player through the soft opening in Spaceport and Bank Roll.
- Bank Roll first-person screenshots before and after toggling the diagnostic,
  changing camera settings and indexed materials were byte-identical within
  each backend. SHA-256: OpenGL
  `00741A78EED85A66BD3EC07E57112BD970E2BEC5599A650298D6A96A51ED11FB`;
  Vulkan `9B2A99194AB245B6403145BFA0895E4D5D781C24DE583A5051BBD968AA87399E`;
  GLES `B1A39A46A1E055260210D6195094E2F6E7303183FDBDEC153C19BA20623F457F`.
- Harness scripts, captures and logs use the `cut-`/`cutaway-` prefixes under
  `build-isoraze/run`. Test input bindings and settings were isolated from the
  interactive configuration. The matching generated `raze.pk3` is staged beside
  the Release executable. The original game directory remains untouched.

### Controls follow-up verification

- The Release build succeeded. Native runs exited successfully on OpenGL
  (Hollywood Holocaust), Vulkan (Spaceport) and GLES (Bank Roll). Scripts, isolated
  test configurations, logs and screenshots use the `controls-` prefix under
  `build-isoraze/run`. The original interactive configuration and game data were
  not used as output destinations.
- In E1L1 at camera yaw 45, holding forward while switching aim from approximately
  132 degrees to -43 degrees retained command XY `(0.70711, 0.70711)`. The player
  physically moved in that direction. Right strafe with synchronous input used
  `(-0.70711, 0.70711)`. At camera yaw 135, forward used that same second vector.
  Pistol fire passed through `SB_FIRE` and reduced ammunition from 48 to 46.
- Eight-direction projection, diagonal speed and aim checks passed at camera yaws
  45/135/225/315, pitches including 20/75 degrees and multiple zoom levels.
  Both `cl_syncinput` modes and `cl_capfps 1` were exercised.
- Mouse-event injection used `PostMouseMove`, the same entry used by native mouse
  providers. At sensitivity 2, raw delta `(24,12)` moved the cursor by `(48,24)`
  screen pixels, including with both FPS inversion settings enabled. At 0.75
  render scale it moved by `(36,18)` virtual pixels, equivalent to the same
  physical displacement. The player's yaw followed the cursor.
- Cursor-at-player held the previous yaw; large deltas clamped at the game-view
  boundary. Injected motion during pause and an open menu left cursor and player
  yaw unchanged. HUD size 0, render scale 0.75 and a 1024x768 window retained valid
  projection and movement checks. Captures were visually inspected.
- Held forward switched between world-space and original local-space commands
  when controls or the diagnostic were toggled. Re-enabling restored screen
  movement. A paused GLES first-person before/after pair was byte-identical:
  SHA-256 `B1A39A46A1E055260210D6195094E2F6E7303183FDBDEC153C19BA20623F457F`.
  Candidate repeatability/mapping checks also passed in the final GLES run.
- These tests cover the input adapter and representative scenes. Native physical
  mouse hardware, all weapons, all movement states and every map have not received
  a full interactive playthrough.

## Visibility and shot-trail follow-up

The default is now `iso_debug_visibility 1` and
`iso_debug_visibility_horizontal 1`. Scenery uses a horizontal footprint rather
than eye-height surface visibility. This keeps building facades below rooftops
and the full height of visible platforms. Visibility still follows actual sector
connections and solid walls/corners. A sector-height door blocks transmission
when its shared opening closes. Opening heights are evaluated at the crossing
point, including slopes, without identifying game-specific door mechanisms.

Enemies, pickups and other sprite types retain the three-dimensional eye ray.
The local player is exempt so the character remains a useful presentation aid.
A broadened candidate traversal is used for scenery, but it never grants actor
visibility by itself. Prepared sprite counts in `iso_debug_stats` are candidate
counts, not counts of fragments that survive the mask.

Rather than adding six eye-camera render passes, a GPU fragment ray follows the
sector graph from the observer to each surface point. Current wall endpoints,
sector planes and resolved opaque masked-wall / wall-sprite alpha data are
transported losslessly through an unfiltered data texture. Sector ownership is
checked at the destination, so coincident coordinates in an unrelated room do
not automatically authorize its geometry. Physical floors and ceilings still
block actor rays. The cutaway never changes these occluders.

The shared GLSL helper is loaded by OpenGL, Vulkan and the desktop OpenGL 3 mode
of the GLES backend. The eye mask requires GLSL 3.30 or newer. ES 2 / GLSL 1.00
keeps its first-person path; the experimental mask fails closed there. The
visibility texture is owned by the framebuffer, reset before backend teardown,
and cached while its encoded geometry/alpha data is unchanged.

The visibility feather darkens a narrow strip on the authorized side of portal
jambs and solid-wall silhouette corners. Hidden fragments are discarded before
writing depth. It is independent of the player cutaway's alpha blending. Wall
subdivisions on the same straight line do not produce corner feathers.

The cutaway keeps the upright player-plane test to avoid fading rear walls near
the rounded ends, and bounds its extrapolation with the actual capsule depth to
avoid cutting behind the feet. A magenta debug overlay shows affected fragments.
This corrects the geometric selection error; a complete interactive audit of
every reported darkening case remains necessary.

Mouse aiming uses the original `CROSSHAIR` artwork instead of the temporary gold
diamond. Hitscan shots add short gold ribbons from the original shot start to
the native resolved hit point. They fade over 14 Build-clock units, depth-test
against the presentation scene and retain the height-aware visibility mask.
Pistol, shotgun, chaingun and grower paths emit trails; an existing teleporting
hitscan emits separate segments on either side. No additional aiming, damage,
collision, gameplay RNG or simulation actors are introduced by the effect.

| Setting / command | Purpose |
| --- | --- |
| `iso_debug_visibility 0/1` | Compare coarse candidates against the surface mask |
| `iso_debug_visibility_horizontal 0/1` | Strict eye-height scenery / horizontal scenery; actors stay height-aware |
| `iso_debug_visibility_feather 12` | Inward feather width in world units; `0` gives a hard boundary |
| `iso_debug_visibility_view 1` | White/gray permission-mask view |
| `iso_debug_visibility_stats` | Encoded data size, uploads, occluders and selected policy |
| `iso_debug_visibility_probe x y z sector` | Native `cansee` geometric reference for a world point; excludes texture alpha |
| `iso_debug_cutaway_view 1` | Highlight cutaway fragments in magenta |
| `iso_debug_tracers 0/1` | Disable / enable the visual shot trails |

Current limits: the candidate pass can still omit geometry; this is not a
complete replacement for Build's rendering policy. Fragment rays stop after 96
sector crossings and fail closed on excessive depth. Actor permission uses the
rendered sprite geometry, not an exact solid-body model. Floor/slope sprite
occluders, transformed portals, unusual overlapping geometry, translucent
materials and very large replacement textures require further work. Scenery's
masked-wall policy transmits through a texture column/row with a vertical hole;
this is intentionally more permissive than the actor's exact texel sample.
Previously explored terrain remains deferred. Broad performance and gameplay
compatibility are not claimed.

Verification artifacts are isolated under `build-isoraze/run`, using the
`visibility-`, `horizontal-`, `fixture-` and `trail-` prefixes. A generated native
Build test map has a source room, a narrow height-limited opening, and a wider
room with two enemies: one in the opening's horizontal interval and one outside
it. The closed variant collapses the opening. Native geometric reference probes
confirm the expected visible center / hidden side / hidden low actor samples.
The final fixture captures show the center enemy through the opening, hide the
off-axis enemy, and restore the visible floor beneath the center enemy only for
horizontal scenery. The actor mask stays unchanged between the two scenery
policies. Closing the opening reduces the candidate set to the source room and
the native reference reports the center target hidden.

The Release build and whitespace checks passed. Native runtime checks covered
OpenGL on E1L1, Vulkan on Bank Roll (E3L2), and desktop GLES on Spaceport (E2L1),
including indexed materials and alternate camera yaw. Candidate repeatability
and map-flag preservation passed on all three backends. E1L1's horizontal view
restores the rooftop building facade. Paused first-person screenshots before
and after the isometric comparison are byte-identical on each backend. A live
pistol check consumed native ammunition, displayed the shot ribbon, and showed
it expire between shots. These are representative checks, not a full map or
weapon compatibility audit.

The later street-wall correction removes only fabricated reverse faces on
one-sided map walls. In E1L1 sector 305, walls 1788 and 1792 have no neighboring
sector and are back-facing at camera yaw 45; drawing their reverse sides caused
the tall facade obstruction. Existing two-sided exterior faces remain supported.
No height-based cutaway rule was retained. The `wallface-` runtime captures show
the street obstruction removed; the rooftop capture matches the earlier image
byte-for-byte. The paused first-person before/after images also match. Native
wheel-event checks verified 640 → 581.818 → 640, the 320/1280 limits, interception
of the wheel binding in isometric mode, and native binding fallthrough after
switching to first-person mode. The Release build and whitespace checks passed.

The bounded-depth follow-up uses the `finite-` and `bounded-` captures. Generated
native maps place Duke at the edge of a raised platform, with either no added
occluder, a wall between him and the camera, or a wall behind him. Old/new uncut
baselines match byte-for-byte. The old shader marked 54,560 foundation pixels
for cutting; the bounded reference reduces this to a thin 3,612-pixel foreground
rim while preserving the actual blocking-wall opening. The rear wall receives
no cutaway. Vulkan and desktop GLES pass these mask comparisons. OpenGL E1L1
passes the first-person byte comparison and candidate/map-flag checks. Native
wheel events reach the new 192/2048 limits, movement/aim projection passes at
both limits, and the wheel binding is consumed only in isometric mode. These
checks reproduce the below-feet leak; other darkening reports still need their
own reproductions.

The floor-cutaway follow-up uses the generated `floor-*.map` fixtures and
`floor-final-*` captures. It covers a raised sector floor, a sloped sector floor,
floor sprites, two overlapping floor sprites, a mixed wall/sloped-floor case,
and a translucent floor sprite. Duke is hidden without the cutaway and visible
with it. Flat ground, a ramp underfoot and a rear surface receive no cutaway;
their solid/cutaway/debug captures are byte-identical. Vulkan covers all nine
cases; OpenGL and desktop GLES cover the raised, sloped, stacked, mixed and
ground cases. A static native first-person old/new comparison is also
byte-identical. An earlier E1L1 round-trip comparison ran unpaused in the
background and differed in its animated explosion, so it is not used as the
pixel-equality check. The Release build and whitespace checks passed.

The sprite-selected cutaway follow-up uses the `sprite-*.map` fixtures and
`sprite-checked-*`, `sprite-backend-*`, `sprite-alpha-*` and `sprite-camera-*`
captures/logs. A native wall running towards the camera beside Duke reproduces
the unnecessary opening: the previous build changes 3,851 pixels; the new
solid/cut/debug captures are identical. A blocking wall selects one surface,
two overlapping walls select both, and the adjacent wall stays solid in both
cases. The raised/sloped/stacked/mixed floor cases still expose Duke. Rear and
flat-ground captures remain identical. Unlike the earlier body-segment test,
the sprite rule also selects a ramp which really intersects an uphill boot;
the resulting small near-foot opening follows the same rule as other surfaces.

Isolated imported-art fixtures verify both directions of the alpha test: an
opaque wall passing only through transparent player artwork does not qualify,
and a wall whose hole surrounds Duke does not qualify. Their solid/cut/debug
captures are identical. The player-art test also includes a positive blocker
over the opaque artwork. Original assets are not modified. Vulkan covers all
nine floor fixtures and the four wall fixtures. OpenGL and desktop GLES cover
the side/front/stacked walls plus sloped/stacked/ground floors. Selection stays
correct at zoom 192/640/2048 and side-wall camera pitches 20/55/75. E1L1, Bank
Roll (E3L2), and Death Row pass the repeatability and unchanged mapping checks.
Static first-person old/new captures are byte-identical. The Release build,
runtime log checks and whitespace checks pass.

The empty-background fallback follow-up uses `coverage-*` logs/captures. The
native ramp fixture, which previously opened a black strip at Duke's boots,
now has identical solid/cut/debug captures on Vulkan, OpenGL and desktop GLES.
A shallow-water sector (native lotag 1) reproduces the submerged-sprite overlap.
With an isolated constant-color test floor, the previous build exposes 20,466
black pixels outside the player region and marks 91,378 pixels there for
cutaway. The coverage-gated build has zero of either on all three backends.
The constant artwork removes water texture motion as a comparison variable;
the original animated-art test is also visually checked. These imported test
resources live in the ignored run directory and never modify original assets.
Raised and stacked floors still reveal Duke, and a truly dark floor behind an
obstruction still authorizes the opening. This checks occupancy rather than
testing rendered scene colors. Native ramp, water, raised, stacked and dark
fixtures run on all three backends; additional floors, mixed wall/floor and
translucent fixtures run on Vulkan.
Side/rear walls remain unchanged, single/two-layer blocking walls still fade,
and the mask remains aligned at zoom 192/640/2048, half render resolution,
4x MSAA and indexed-color rendering. E1L1, Bank Roll and Death Row pass native
mapping/repeatability checks. Returning from cutaway rendering to a static
first-person scene is byte-identical to the prior build. Runtime logs contain
only the original CON warnings; build and whitespace checks pass. The coverage
pass adds a geometry draw and a color/depth canvas only while surfaces qualify.

The two-axis camera follow-up uses `orbit-controls` logs/captures. Native mouse
events verify horizontal orbit, vertical elevation, 20/75-degree limits, yaw
wrapping, FPS inversion independence, synchronous input and wheel zoom during
a drag. Held world aim stays unchanged through dragging and release, including
when the crosshair reaches a viewport edge. Short press/move/release bursts
remain ordered; normal cursor aim resumes afterward. Only the FPS test invokes
the original middle-button binding. Projection checks allow the engine sine
table's float round-trip precision (observed normalized aim error below 3e-8);
movement and speed still use the original 1e-8 tolerance.

`orbit-render-vk-final` and `orbit-render-gles` check the raised-floor cutaway
at pitches 20 and 75, plus E1L1 and Bank Roll at arbitrary yaw/pitch. Projection,
candidate repeatability and unchanged mapping checks pass. Captures confirm
the orthographic view and floor opening remain aligned. The unfocused
`orbit-cancel` run confirms controls stay inactive without focus; it does not
exercise interruption of an active drag. Interactive menu/focus interruption
and live gameplay across camera angles still need broader verification.

## Remaining verification gates

1. Broaden the horizontal-scenery / height-aware actor mask on openings,
   corners, slopes, masked walls and partially occluded actors. Camera cutaways
   must not change the permission result.
2. Broaden cutaway coverage for elevated floors and animated player bounds without
   widening the visibility result. Audit orthographic lighting/depth and
   transparency sorting; test live moving
   geometry and preserve original game-trigger semantics.
3. Broaden interactive controls/aiming coverage across weapons, movement states,
   moving geometry and height changes while retaining native autoaim and weapon
   resolution. Complete the precise visibility and game-trigger policies before
   treating the prototype as a gameplay-correct isometric engine.
