# Blood–Duke Superset

## Goal and architecture agreed for the experimental branch

Build a first-class combined game module under Raze, with one process and one
shared world. Player, weapons, enemies and map content will be independent
choices. Both native implementations supply reusable behavior. Shared gameplay
services will provide common units, damage, status effects, targeting and
inventory interactions. Freezing/shrinking must work on Blood enemies and
burning/electrical attacks on Duke enemies, subject to explicit resistances.

Build shared capabilities as a superset: simpler behavior is a subset or
configuration where practical. Choose each subsystem's foundation from code
evidence, independently of which game supplies the map or player. Blood's
native dude movement currently supplies Caleb's controller, drag and moving
surface response. Its thing physics also has explicit elasticity/bounce
parameters worth generalizing for shared objects. Preserve dedicated collision,
timing and special-behavior handlers when parameters cannot reproduce an
algorithm. Enabling a capability must not force every object to use it.

Map logic remains separate: Duke maps use Duke sector effects, switches, doors
and events; Blood maps use Blood's equivalents. Shared players, enemies, weapons,
projectiles and pickups access each map through environment services. Health
damage, knockback, ignition, freezing and shrinking are distinct operations.
Character selection supplies an identity and movement profile, not a permanent
restriction on weapons/pickups. A unified movement profile can be added later.

Keep representation conversion separate from balance adjustment. Shared health
uses 1/16-point precision; Blood raw health already uses those units and Duke
points convert by multiplying by 16. Both native simulations currently use 30
updates per second and a 120 Hz clock. Movement conversion must preserve the
whole acceleration/friction/jump model, not just a speed ratio.

The initial playable combination remains:

- Preserve Duke map geometry, artwork, scenery, mechanisms and world simulation.
- Use Blood player movement, weapons, alternate fire, health, armor and pickups.
- Test Atomic Edition maps first, with Duke enemies. Selectable Blood enemy
  replacement is a later increment.
- Keep Caleb at his native size and maps at their original scale initially.
- Add jetpack and shrinking compatibility abilities for Duke progression.
- Develop this independently of the isometric presentation.

## Combined frontend bootstrap

`source/games/superset/` owns a separate `GameInterface`, selected by `-superset`.
Use `-iwad` for Duke data and `-blooddata` for the isolated Blood bank:

```powershell
./build-mashup/Release/raze.exe -superset -iwad D:/dos/duke3D/duke3d.grp -blooddata D:/prog/isoraze/build-isoraze/run/blood-data -map E1L1
```

The default player remains Duke. Add `-superset-player caleb` to select native
Blood movement, camera inertia, jumping/crouching and the native pitchfork.
`-superset-player duke` retains Duke movement; the optional `+mashup_combat 1`
pitchfork experiment is also available with Duke. The HUD and map mechanisms
still use the Duke adapter. Only Duke map data is accepted by this first map
adapter; Blood-map loading and independent loadout/enemy selections remain work
for later milestones. The current Caleb adapter supports single-player.

The frontend owns `Superset` configuration/autoexec/autoload sections, save and
demo directories, and the independent `Raze.Superset` save signature/schema. Its content manifest
records the rules revision, map adapter version, primary Duke archive digest
and resolved Blood resource bank digest. Loading checks this metadata before
selecting a map or restoring actors. Installation paths do not enter the digests.
Full primary-mod dependency tracking is still pending; the current manifest
fingerprints the primary Duke archive and secondary bank, not every loose Duke
override or CON mod. The optional pitchfork selection is saved with the session.

Legacy game flags currently describe the Duke map adapter for existing native
code. They do not identify the combined frontend, and are never formed by ORing
Blood and Duke together. `LumpFilter` still selects Duke's startup assets while
`currentGame` and `SavegameFolder` identify Superset. Native Blood and Duke retain
their own frontends and save signatures. The new module composes the native
adapter instead of subclassing or copying its implementation.

`superset_check` verifies frontend/config identity, manifest round-trip and
rejection of changed/missing metadata, plus fractional health and clock
conversions. Add `-Superset -MashupBloodDirectory <path>` to the PowerShell
regression harness to check this before/after save/load, inspect saved metadata
and verify the separate configuration section. `-MashupCombat` additionally
exercises the existing pitchfork prototype.

The next milestones are hardening Caleb's navigation in the Duke world,
shared targeting/damage in both directions, burning a Duke enemy and
freezing a Blood enemy, then the reverse map adapter and expanded mixed content.
The common health/time definitions are present, and the pitchfork now submits
shared Q4 physical hits. Other attacks and receivers still need migration.
Original-game regression checks remain required.

## Native Caleb controller milestone

The Blood controller now calls its native `playerProcess` and extracted
`actProcessDudeMovement` entry points. Native Blood retains the same movement
entry point with its original map behavior. A small `PlayerWorldServices`
interface supplies use/pickup policy, noclip, water, room links and surface
interpretation. Surface numbers are translated explicitly: Duke's surface IDs
must never be interpreted as Blood's IDs. Native drag, friction, gravity,
collision handling and jump/crouch algorithms are retained.

There is one blocking world pawn. Its Duke identity currently serves Duke
mechanisms, targeting and existing pickup handlers. The native Blood controller
state actor is invisible/nonblocking and advances the authoritative movement;
Duke's input movement and player CON ticker are skipped for Caleb. The adapter
publishes the resulting feet pose, camera, velocity, artwork and radius to the
world pawn. World transports/platform displacements are imported back into the
center-based Blood pose and camera inertia. The simulation clock advances once.
This is an incremental native adapter, not the final shared actor/AI model.

No scale compensation is applied: initial idle eye height is 56.6875 world units
and horizontal radius is 12. Native Blood SEQ state and the selected controller
are serialized with the session. Save schema is 2; rules revision is now 4
after integrating incoming Caleb damage and Blood object events. Earlier
Superset bootstrap saves are intentionally incompatible with this experimental
schema. The legacy native game save signatures remain separate.

Known Duke attacks now enter Caleb's native Blood damage/armor receiver directly
in shared Q4 units. Unmapped attacks and direct map health changes still cross
the legacy integer facade. Duke pickup handlers, HUD, some hazards and special
abilities still need explicit shared-gameplay integration. Full
doors/lifts/water/transport/crusher navigation and body
rendering need broader verification; short movement checks do not prove those.

`superset_movement_check start`, followed by actual move/jump/crouch input and
`superset_movement_check`, verifies the native profile, observed movement,
single collider and clock progression. The harness runs this with
`-Superset -SupersetPlayer caleb -MashupBloodDirectory <path>` before and after
save/load and after an optional map load. Weapons and movement are enabled
independently of the old pitchfork toggle when Caleb is selected.

Controller verification, 2026-11-08: Release build passed. Caleb E1L1 passed
movement/jump/crouch, profile, one-collider and single-clock checks before and
after save/load and after a fresh E1L2 map load, with primary/alternate pitchfork
input and native audio checks. Caleb E2L1 also passed before/after save/load
after introducing explicit surface translation. Screenshots were inspected.
Ordinary Duke E1L1 and Blood E1M1 passed regression checks after extracting the
native movement entry point. These are component checks, not navigation
playthroughs. Artifacts under `build-mashup/checks/`:

- `runtime-20261108T001944822`: Caleb E1L1 and fresh E1L2, with audio.
- `runtime-20261108T002130469`: ordinary Duke E1L1 and Blood E1M1.
- `runtime-20261108T002423679`: Caleb E2L1 with explicit material translation.
- `runtime-20261108T002513146`: final Superset/Duke-profile and native Blood
  regression after introducing the environment service.

## Unresolved gameplay decisions

These remain explicit design questions, not implicit crossover rules:

- Complete native attack/receiver mappings beyond the initial Blood category
  mapping and pitchfork physical receiver. Physical damage may need several
  traits; health units alone do not determine balance or armor penetration.
- Decide armor/pickup mappings and mixed inventory limits. The current Duke
  pickup facade does not settle how Duke armor relates to Blood armor types.
- Define effect interactions: fire versus frozen targets, ignition thresholds,
  electrical hit reactions, thawing/shattering and shrink/stomp exceptions.
- Define boss/mod actor capability defaults and overrides. Broad support for
  both enemy families does not imply identical susceptibility for every actor.
- Choose whether balancing profiles alter native speeds, weapon cadence or
  durability. Initial conversions preserve native values and full Caleb size.

Bootstrap verification, 2026-11-08: Release build and `git diff --check` passed.
Superset E1L1 passed native runtime/resource/input/combat/audio checks before and
after save/load, plus a fresh E1L2 map load. Superset E2L1 passed the same
frontend/runtime/resource/save checks with the pitchfork prototype disabled.
Both runs deliberately changed the prototype toggle between saving and loading;
saved selections were restored. Save metadata and separate configuration
sections were inspected, and screenshots were checked. Ordinary Duke E1L1 and
Blood E1M1 passed runtime/save/load regression checks after the loading changes.
These remain short component/state checks, not full map playthroughs or proof
of native Caleb movement.

Artifacts under `build-mashup/checks/`:

- `runtime-20261108T000351346`: Superset with audio/pitchfork, E1L1 and E1L2.
- `runtime-20261108T000451497`: ordinary Duke E1L1 and Blood E1M1.
- `runtime-20261108T000603848`: Superset without pitchfork, E2L1.

## First increment: shared runtime foundations

This increment does **not** enable a playable mashup mode yet. Ordinary Duke and
Blood launches still select their original game interfaces and controllers.

Game-specific status, sector and full-world actor iterators now filter by the
actor's runtime class. Core actor iterators still see every actor. `Peek` remains
non-consuming, derived classes are included, and `Next` caches the next matching
actor before returning, so deleting the returned actor or intervening actors
from the other family does not interrupt traversal. This addresses the shared
status lists: Blood pickups and Duke sector effectors both use status 3.

Duke sector fields (`lockinfo`, `shadedsector`, `hitagactor`) no longer overlap
Blood's `XSECTOR` pointer and environment fields. A Duke sector without Blood
state can safely report `hasX() == false`, including when it has lock data.
The existing save keys are retained. Duke saves can carry Blood sector
extensions alongside Duke state, and the map garbage-collector marker visits
both sets of actor references in a Duke world.

`mashup_runtime_check`, run in a loaded Duke or Blood level, checks mixed-family
enumeration, subclasses, reset/peek behavior, removal, independent sector state,
sector serialization and cleanup. Test actors are removed synchronously before
the next simulation tick. The live map's geometry and player are not changed.

This is a prerequisite for coexistence, not permission to run both complete game
tickers. Collision results, player lookups, ownership, sprite animation and
damage callbacks still have game-specific casts and need explicit adapters.

## Second increment: isolated Blood resources

`-bloodduke <Blood data directory>` enables a secondary bank when Duke Nukem 3D
is selected. A path to `BLOOD.RFF` in that directory is also accepted. The
directory must contain `BLOOD.RFF`, `SOUNDS.RFF` and the original `TILES*.ART`.
These files are read through Raze's native filesystem/RFF readers, including
RFF decryption, without mounting them into Duke's global filesystem.

Blood tiles have stable names (`mashup/blood/tileNNNNN`) and consecutive texture
IDs outside Duke's reserved tile bank. Their ART offsets, animation metadata,
surface types and shade adjustments are retained. The in-memory sprite tile
field is widened to 32 bits and its texture accessors preserve secondary bank
IDs. Disk map formats remain unchanged; the existing numeric sprite save key
also accepts the wider value. A native serialization check covers IDs above
32767. Legacy CON tile conversion still refers to the primary Duke bank.

Blood's five palettes and fifteen stock lookup tables produce independent
translations. The renderer uses Raze's existing `FBuildTexture` and RGBA path
for this bank, retaining opaque Blood index 0 and transparent index 255. Duke's
palette, shade count, tile bounds, game type and mounted files remain unchanged.
Only the first row of each Blood lookup is represented by these translations;
lighting uses the active renderer's truecolor shading, not Blood's exact
64-step indexed shade ramp. Blood fullbright masks and voxels are not integrated
yet. Duke rendering continues through its existing texture selection path.

Blood's existing SEQ and QAV decoders now resolve resource IDs through this bank
and convert Blood tile numbers through its own texture mapping. Ordinary Blood
launches use the original filesystem and tile bank. Sound support was added in
the third increment below.

For a visible diagnostic, add `+mashup_resource_preview 1`. This draws a Caleb
sprite and animates the stock pitchfork idle QAV over the Duke view. It does not
replace Duke's weapon, process weapon callbacks, or change player movement.
`mashup_resource_check` verifies primary-game isolation, every pixel of Caleb's
sprite across all 75 palette/lookup combinations, palette selection, native SEQ
and QAV loading, and secondary sprite texture serialization.

Example using the local data paths:

```powershell
./build-mashup/Release/raze.exe -iwad D:/dos/duke3D/duke3d.grp -bloodduke D:/prog/isoraze/build-isoraze/run/blood-data -map E1L1 +mashup_resource_preview 1
```

Keep `-bloodduke` enabled when reloading saves containing secondary resources.
Without the combat option below, this remains an asset/bootstrap prototype.
Saves from the prototype should be reopened with the same game data and launch
options; the secondary bank is not yet recorded in the save's resource manifest.

## Third increment: native pitchfork combat

Add `+mashup_combat 1` alongside `-bloodduke` for the first interactive combat
prototype. This currently supports single-player Duke only. The pitchfork
replaces Duke's weapon drawing and firing. Blood's existing weapon state machine
handles raising, lowering, primary/alternate fire, QAV timing and callbacks.
Duke quick-kick and normal weapon firing are suppressed while it is active.

Each Duke player owns a separate native `DBloodPlayer` and nonblocking, invisible
Blood actor for weapon state and origins. Blood player lookups explicitly resolve
this secondary state. The main player array continues to hold Duke players; no
global player-array swaps or second complete game ticker are used. The Duke pawn
remains the only physical player. The combat actor follows it after Duke's world
simulation, including changes made by mechanisms and transports.

Pitchfork vectors use Blood's spread/range and Duke's hit callbacks. A short-lived
Duke melee hit object supplies the physical player as damage owner, allowing
Duke enemies, scenery and switches to react through their existing implementation.
Access switches retain their key requirement. Damage is converted from Blood's
fixed-point health through the shared physical-hit receiver described below.
Material effects and damage balance still need broader compatibility testing.

Blood raw SFX definitions now register under `mashup/blood/` names with private
sample IDs. Their bytes are read from the secondary bank by the existing native
sound decoder. Blood resource numbers do not replace Duke sound numbers. Blood
actor sound playback uses the corresponding physical Duke pawn, and channel
replacement/stop operations are restricted to Blood sounds. Pitchfork flesh hits
use the native Blood impact sound. WAV/music support and complete material-based
impact effects remain outside this increment.

The secondary player is garbage-collected through the Duke player's owned
reference and is included in native savegames. It is cleared before a new map is
loaded. Use the same launch options when reopening prototype saves.

```powershell
./build-mashup/Release/raze.exe -iwad D:/dos/duke3D/duke3d.grp -bloodduke D:/prog/isoraze/build-isoraze/run/blood-data -map E1L1 +mashup_combat 1
```

Only the pitchfork is enabled. Movement, physical dimensions, health, armor,
pickups, inventory and the status bar still use Duke behavior. Full-size Caleb's
collision/camera profile is not active yet; the agreed full-size target remains
unchanged. The invisible combat actor does not provide collision dimensions.
Jetpack and shrinking currently remain Duke's existing abilities.

`mashup_combat_check` is a diagnostic: it resets the pitchfork state, creates a
temporary target, advances the native weapon state machine until a QAV callback
hits it, then checks damage ownership, player routing and self-hit protection.
It also verifies isolated sound bytes and, with audio enabled, native sample
decoding/playback. `mashup_combat_input_check start` followed by holding Fire and
`mashup_combat_input_check` verifies the real button path and no Duke ammunition
consumption. Pickups may still increase Duke ammo during the test.

## Shared damage ingress

The first shared damage packet carries a Q4 health amount and a damage category.
Blood's existing seven damage categories supply the initial response vocabulary:

| Blood implementation | Shared health damage category |
| --- | --- |
| Fall | Impact |
| Burn | Heat |
| Bullet (also used by the pitchfork) | Physical |
| Explode | Blast |
| Drown | Asphyxiation |
| Spirit | Spirit |
| Tesla | Electric |

These categories come from `actDamageDude`, the per-enemy `damageVal` table,
player immunity and armor dispatch. They are not weapon/enemy pair rules.
Heat damage does not itself request ignition; knockback, burning, freezing and
shrinking remain separate work. Duke freezer/shrinker and nonphysical receiver
mappings have not been settled by this increment. The first Duke receiver
explicitly accepts Physical and rejects other shared categories.

Native Blood tines deal `17 << 3` Q4 units to a non-player (8.5 points), and
`17 << 4` to a player. The bridge previously truncated every non-player hit to
8 points. It now retains fractional damage per target after native hit
acceptance, yielding 17 points for two hits. The native `hitextra = -1` sentinel
is treated as an empty queue for shared hits, rather than subtracting a point
from the first hit. Native hits without a shared payload keep their original
arithmetic. Native self-hit, size and friendly-fire gates, ownership, double-hit
modifiers and `onHit`/CON reactions remain in the native adapter.

The remainder, pending-hit marker and projectile payload serialize with their
actors. Common zero health is translated to negative enemy health when the hit
is consumed because Duke enemy CON tests `< 0`; Duke player CON accepts zero.
Fractions are discarded on lethal consumption and actor initialization.

This remains an adapter over Duke's integer CON health, not the finished shared
health model: damage below an integer point is deferred until another accepted
shared hit. Direct CON health replacement/capped healing and custom `onHit`
handlers that bypass native damage accumulation need explicit synchronization.
Blood enemy receivers and shared status effects remain to be integrated.
Incoming Caleb damage and impulses are handled by the next increment below.

`superset_damage_check` checks category round trips, two fractional hits against
a native trooper, rejected hits, ownership, native modifiers, lethal health,
ordinary-hit arithmetic and the trooper's actual CON death animation. The
runtime harness seeds a half-point remainder before saving, destroys the test
actor, then verifies its restored remainder and second hit after loading.

Verified on 2026-11-08: Release build and damage/category/CON-death checks passed
with Caleb on E1L1 (audio enabled, save/load and fresh E1L2 load) and E2L1
(including the serialized remainder). The legacy Duke-movement pitchfork mode
also passed the new checks. Ordinary Duke E1L1 and Blood E1M1 passed their
runtime/save/load regressions. Artifacts under `build-mashup/checks/`:

- `runtime-20261108T005121986`: Caleb E1L1 and E1L2, with audio.
- `runtime-20261108T005246925`: ordinary Duke and Blood.
- `runtime-20261108T005332809`: legacy pitchfork mode, including dead-hit cleanup.
- `runtime-20261108T005520410`: Caleb E2L1, final damage/default-state checks.

## Incoming Caleb damage and shared Blood effects

The Caleb receiver reuses Blood's native enemy-response factor, player
immunities, armor consumption, fractional health, pain sounds/state and death
sequences. Incoming Duke points convert to shared Q4 units, without a balance
multiplier. Accepted direct hits enter this receiver before the legacy Duke
queue; radius damage enters when its native queued hit is consumed. Importing
the world pose afterward does not apply the health loss twice.

| Duke attack implementation | Caleb health response |
| --- | --- |
| ShotSpark bullet hits and melee hits | Physical / Blood bullet armor |
| Trooper Firelaser | Physical, provisionally; no ignition |
| Attacks explicitly flagged EXPLOSIVE | Blast / Blood fire armor |
| Freezer, shrinker, grower | Await shared effect rules; legacy path remains |
| Other unclassified/mod-specific attacks | Legacy health-delta path remains |

The laser rule remains a gameplay choice, not a physical-units conversion.
Physical is the provisional default because the native laser applies ordinary
hit damage without ignition. Heat/fire armor is an available alternative; the
classification is isolated in `DukeDamageKind`.

Knockback is a separate impulse using Duke's original normal/double-thrust
coefficients. Both horizontal movers consume world displacement per simulation
step, so no extra velocity ratio is applied. Blood's drag then acts on Caleb's
velocity. Native damage immunity can block health damage without erasing the
impulse. The global god cheat blocks these mapped attacks.

Blood's death/restart routines now accept the existing environment service.
Native Blood keeps its original triggers, frag handling and respawn path.
In a Duke world, Caleb retains native death sequences and the world pawn loses
collision; Use after the sequence finishes requests Raze's normal checkpoint
reload/restart. Duke attacker attribution stays on the world pawn, without
casting the attacker to a Blood actor or reporting a fabricated suicide.

Explosive death exposed unsafe Duke rendering casts on visible Blood gibs.
Duke's animation passes now operate only on Duke actors. Foreign effects keep
their engine sprite/SEQ representation. Full native Blood rotation, shadows,
object-specific rendering and foreign FX sound/material routing remain separate
integration work.

Shared object ticks advance Blood sequences once per simulation clock, process
native object callbacks, run native FX gravity/drag/movement and drain deferred
removals. This runs independently of selecting Caleb or Duke. The secondary
event queue is serialized and cleared with its world; Blood map mechanisms are
not started by this hook. Blood thing/gib-body physics and general enemy AI are
not yet part of this object tick.

`superset_incoming_check` verifies actual native bullet/laser reception, physical
and blast armor, immunity, god mode, the native electrical response factor,
fractional health and separate impulses. The harness also checks ordinary and
explosive death sequences, waits for completion, presses Use and verifies the
checkpoint's restored health/armor. `superset_fx_check` seeds a moving foreign
effect with a timed removal, deletes it before load, then verifies restored
movement and expiration. The FX check also runs with Duke selected.

Verified on 2026-11-08: Release build passed. Caleb E1L1 with audio and a fresh
E1L2 load passed the incoming/armor, ordinary and explosive death/restart, FX
movement/save/expiry and existing resource/combat/movement checks
(`runtime-20261108T012411000`). The final E2L1 run passed the same damage,
death/restart and independent FX checks (`runtime-20261108T013039070`). Superset
with Duke selected passed FX movement/save/expiry, and ordinary Blood E1M1
passed its runtime/save/load regression (`runtime-20261108T012733583`). These
checks were followed by an ordinary Duke E1L1 run without the secondary bank
(`runtime-20261108T013215115`), which also passed. These
remain component checks on the local Plutonium Pak data, not full playthroughs
or proof of arbitrary CON/mod compatibility.

## Build and regression checks

Configure a separate `build-mashup` directory with Visual Studio 2022, then build
Release. In the current Windows environment, use one MSBuild node with
`--parallel 1 -- /nr:false`; project parallelism can stall. Place the usual
ZMusic/OpenAL runtime DLLs beside the resulting executable and its matching
`raze.pk3`.

Run the checks with PowerShell 7 and paths to existing game data:

```powershell
./tools/check-mashup-runtime.ps1 -DukeGrp 'D:/games/duke/duke3d.grp' -BloodRff 'D:/games/blood/BLOOD.RFF'
```

The script defaults to E1L1, E2L1, E3L1 and E4L1 for Duke and E1M1 for Blood.
Use `-DukeMaps E1L1` for a shorter Duke run, or pass only one game's data path.
Add `-MashupBloodDirectory <directory>` to test the secondary bank in each Duke
case, including the resource checks and diagnostic screenshot. A simultaneous
`-BloodRff` case remains an ordinary Blood launch to check the fallback path.
Add `-MashupCombat` for actual Fire-button and native pitchfork damage checks.
`-EnableSound` exercises native audio playback. `-DukeTransitionMap E1L2` adds a
new-map load and a third set of checks; it verifies the destination in the log.
Full garbage collection runs after load to exercise ownership of secondary state.
Each case runs the native checks before and after saving/loading, captures a
screenshot, checks the results and exits. Configuration, saves, screenshots and
logs go into a new directory under `build-mashup/checks`. Original game data is
read-only. The script disables the isometric view and uses Vulkan in a window
with background simulation enabled. Level intro screens are skipped so E4L1's
intro does not consume the test's initial wait.

Verified on 2026-11-07: the Release build and native checks passed on Duke E1L1,
E2L1, E3L1 and E4L1, and Blood E1M1, including a save/load round trip in each
case. Duke and Blood screenshots were inspected. The local Duke archive is the
44,348,015-byte Plutonium Pak version; Atomic Edition 1.5 data has not separately
been tested. These are startup/state regression checks, not playthroughs or
evidence that the future mashup's movement and combat are compatible yet.

Second-increment verification, 2026-11-08 (local time): the Release build and
resource checks passed on Duke E1L1, E2L1, E3L1 and E4L1 with the secondary Blood
bank, before and after a save/load round trip. Ordinary Blood E1M1 also passed
the runtime/save/load checks. The Duke E1L1 diagnostic screenshot was inspected:
Caleb and the native pitchfork QAV render alongside Duke art. A final E1L1 check
covers luminosity translation preservation, and an ordinary Duke E1L1 launch
checks the bank-disabled path. These checks still use the local Plutonium Pak
data described above.

### Native map navigation checks

Duke transporter cooldowns and access-card activation now have small native
handlers shared by the Duke player and the Caleb adapter. They still belong to
the Duke environment; Caleb does not run Duke's movement or inventory timer
loop. The adapter also advances native timed exits and exit-button sequences.
This fixes actions that otherwise stop when Duke's player input is replaced.

`make-superset-navigation.py` generates a Build v7 map containing a ceiling door,
floor lift, paired teleporters, access-card-controlled door and paired water
sectors. It contains geometry and native map tags only, with no game assets.
The diagnostic `superset_navigation` command is restricted to this fixture.
It positions the player for each case; actual use, movement, animations and
portal traversal execute through normal input and game ticks.

```powershell
./tools/check-superset-navigation.ps1 -DukeGrp 'D:/games/duke/duke3d.grp' -BloodDirectory 'D:/games/blood' -PythonPath python
```

The PowerShell 7 harness runs both selectable characters, checks opening and
walking through the door, riding the lift, teleporting twice with cooldown
recovery, refusal without a card, activation and card consumption, timed-exit
countdown advancement, save/load restoration and diving/surfacing. A different
player position is set between save and load to detect failed restoration.
It does not test a full end-of-level transition, breath depletion, crushers,
all transport variants or arbitrary geometry. It is not a stock-map playthrough.
Water entry/exit passed at Caleb's original size in this fixture; no scale or
water-portal adjustment was introduced based on the earlier suspicion alone.

Verified on 2026-11-08: the Release build and all eleven navigation checks
passed for Duke and Caleb (`navigation-20261108T003909335`). Ordinary Duke E1L1
and Blood E1M1 runtime/save/load checks passed
(`runtime-20261108T003734072`). Caleb's E1L1 checks, save/load and fresh E1L2
load also passed with audio enabled (`runtime-20261108T003823712`). These runs
use the local Plutonium Pak archive described above.

## Scale findings

Both games use the same Build-to-Raze coordinate conversion. Caleb's larger
size comes from his actor artwork/sequence scale and the player posture offsets.
With the stock Blood data inspected during investigation:

| Measurement in Raze world units | Caleb | Duke |
| --- | ---: | ---: |
| Standing eye above floor | about 56.1–56.7 | 40 |
| Horizontal movement collision radius | 12 | 10.25 |

Blood's standing `eyeAboveZ = 22` is measured above the sprite origin, not the
floor. The idle frame is tile 3870, 117 pixels high at Y repeat 37, with top
offset -1. Its bottom lies 60 * 37 / 64 = 34.6875 units below the sprite origin.
With feet on the floor, the eye height is therefore 56.6875. Walking frames
produce a slightly different extent. Collision code also applies its own
clearance adjustments; artwork height is not a fixed capsule height.

Do not compensate automatically. First test doors, vents, crouch routes, jumps,
moving floors, water, switch reach and crushers with native Caleb dimensions.
If a scale profile is introduced later, approximately 0.71 matches Duke's eye
height, but does not simultaneously match his collision radius. Camera-only
compensation does not address collision, projectile origins or interaction reach.

## Next integration boundaries

1. Expand the native Blood movement adapter checks beyond the generated door,
   lift, water, access-switch and transport fixture to stock-map routes,
   crushers and other mechanism variants. Keep one collider and authoritative pose/health;
   the current native controller-state actor and Duke world identity are an
   incremental adapter, not the final shared actor model.
2. Generalize actor identity, targeting, ownership and damage in both directions.
   Use shared fractional health units and retain native AI/weapon behavior.
   Keep native same-game resistances and death/trigger callbacks meaningful.
3. Introduce shared status effects with per-actor reactions: first burning on a
   Duke enemy and freezing on a Blood enemy, then shrinking, electrical and
   other damage interactions. Coordinate animation/AI callbacks and save state.
4. Route native movement/environment queries to each map adapter's water,
   platforms, transports, use actions and hazards. Add the Blood map adapter
   and prove foreign actors in both map families.
5. Expand selectable mixed inventories, weapons, enemies and pickup policies;
   provide jetpack/shrinking progression abilities. Keep conversions separate
   from tunable balance and verify safe regrowth and passage clearance.
6. Complete resource features and mod dependencies as needed: fullbright masks,
   voxels, music/encoded samples, indexed shading and primary CON overrides.
