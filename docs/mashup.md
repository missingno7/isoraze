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
- Test Atomic Edition maps first, with Duke enemies. Native Blood axe zombies
  can now be spawned alongside them; automatic enemy replacement comes later.
- Keep Caleb at his native size and maps at their original scale initially.
- Add jetpack and shrinking compatibility abilities for Duke progression.
- Develop this independently of the isometric presentation.

## Combined frontend bootstrap

`source/games/superset/` owns a separate `GameInterface`, selected by `-superset`.
Use `-iwad` for Duke data and `-blooddata` for the isolated Blood bank:

```powershell
./build-mashup/Release/raze.exe -superset -iwad D:/dos/duke3D/duke3d.grp -blooddata D:/prog/isoraze/build-isoraze/run/blood-data -map E1L1
```

For Duke maps the default player remains Duke. Add `-superset-player caleb` to select native
Blood movement, camera inertia, jumping/crouching and the native pitchfork.
Blood shotgun/Tommy gun, spray-can primary fire and selected native pickups are
available through the explicit spawn commands described below; Duke pickups
are not replaced yet.
`-superset-player duke` retains Duke movement. Add `-superset-weapons blood`
or `-superset-weapons duke` to choose a weapon family independently of the
character. `-superset-weapons auto` uses the character's default (Caleb/Blood,
Duke/Duke); legacy `+mashup_combat 1` still requests Blood weapons in auto mode.
The HUD and map mechanisms use the map's native adapter. The Duke-map Caleb
adapter supports single-player. A first Blood-map adapter is now available with
native Caleb/Blood gameplay and an initial native Duke actor component; full
reverse combat, a unified weapon list and independent enemy selection remain work.

The frontend owns `Superset` configuration/autoexec/autoload sections, save and
demo directories, and the independent `Raze.Superset` save signature/schema. Its content manifest
records the rules revision, map family/adapter version, primary archive digest
and secondary resource bank digest. Loading checks this metadata before
selecting a map or restoring actors. Installation paths do not enter the digests.
Full primary-mod dependency tracking is still pending; the current manifest
fingerprints the primary archive and secondary bank, not every loose primary
override, CON mod or additional Blood ART/SFX dependency. The selected character
and weapon family are saved with the session.

Legacy game flags describe the current map adapter for existing native
code. They do not identify the combined frontend, and are never formed by ORing
Blood and Duke together. `LumpFilter` selects that map family's startup assets while
`currentGame` and `SavegameFolder` identify Superset. Native Blood and Duke retain
their own frontends and save signatures. The new module composes the native
adapter instead of subclassing or copying its implementation.

`superset_check` verifies frontend/config identity, manifest round-trip and
rejection of changed/missing metadata, plus fractional health and clock
conversions. Add `-Superset -MashupBloodDirectory <path>` to the PowerShell
regression harness to check this before/after save/load, inspect saved metadata
and verify the separate configuration section. `-MashupCombat` additionally
exercises the existing pitchfork prototype.

The next milestones are broader navigation/physics and targeting support,
expanded shared effects, reverse combat and expanded mixed content.
Spray-can burning of Duke enemies and native Blood axe-zombie AI/combat in the
Duke world are implemented; full actor reactions and cross-effect rules remain work.
The common health/time definitions are present, and the pitchfork now submits
shared Q4 physical hits. Other attacks and receivers still need migration.
Original-game regression checks remain required.

## Blood maps and the reverse actor bootstrap

Select Blood as the map source and provide a private Duke archive:

```powershell
./build-mashup/Release/raze.exe -superset -iwad D:/prog/isoraze/build-isoraze/run/blood-data/BLOOD.RFF -dukedata D:/dos/duke3D/duke3d.grp -map E1M1
```

This composes Blood's original `GameInterface` beneath the same Superset
frontend. Blood owns its map loader, XSECTOR/XSPRITE triggers, events, sequences,
player, enemies, physics, weapons, pickups, sounds, HUD and world clock. The
default character is Caleb in this adapter. Duke character/weapon selection is
explicitly rejected until its reverse controller/combat adapter exists; a stale
archived Duke weapon preference falls back with a diagnostic. This is a
single-player bootstrap. Switching map families during a live session remains
pending; choose the primary map family at startup for now.

The first foreign component is the original `DukeBloodSplat1` class and its
native Initialize/Tick/Animate handlers. `superset_spawn_duke_effect splat`
places one on a nearby wall for inspection. It remains a real `DukeActor`
alongside Blood actors, using Duke art and source palette colors. The original
decal growth algorithm stops at counter 364. The native Duke sprite queue owns
insertion/eviction; the Blood adapter supplies that component's GC, save and map
lifetime. Its current uncompiled-CON default capacity is 64. Secondary CON
settings are not yet applied in this adapter.

Only explicitly supported, nonblocking foreign effects are ticked/rendered.
Blood's sprite renderer checks actor family before entering its typed actor
path. Blood owns each simulation step; the foreign tick runs once when the
native clock advances, without running Duke's full ticker. The fixture compares
native frame/clock deltas with the Duke actor counter. Unknown foreign actors
are not passed through Blood rendering as if they were Blood actors.

The private Duke bank reads ART, palette.dat, lookup.dat and the engine's Duke
texture-name definitions without installing Duke's filter or archive in Blood's
filesystem. Both private banks share the ART decoder, and native/private banks
share the texture-alias parser including rotations. Duke sprite sets resolve
through `mashup/duke/` names. Primary tile ranges, palette, shade count, game
flags and filesystem entries stay unchanged. Private archive readers preserve
future access to CON/samples without making them primary Blood resources.

The bank retains Duke lookup/base-palette data and decodes the native colors,
including opaque index 0 and transparent index 255. Normal/underwater view roles
are mapped explicitly; other Blood view palettes are not assumed to mean the
same Duke effect. Complete indexed shading, fullbright masks, voxels, sounds,
overrides and CON execution remain integration work for secondary Duke content.

Existing Duke-world adapters use `IsDukeMap()` at their environment boundaries.
Frontend identity alone must not intercept native Blood missiles, clear its
things' hit flags, retune its dudes, or run the Duke-world controller/diagnostic
paths. Blood's original sequence/event serialization remains authoritative;
the separate secondary-Blood serializer used in Duke maps is not run again.
Shared gameplay becomes map-independent as the reverse collision/damage and
controller services are added; these gates identify the still one-way adapters.

Rules revision 10 records `map_rules=blood`, Blood's native save adapter version,
`blood_archive_md5` and `duke_bank_md5`. The latter fingerprints the secondary
archive and engine alias definitions without installation paths. The manifest
rejects the opposite map family, altered secondary content and missing metadata
before actor restoration. Full primary Blood ART/SFX/mod dependency tracking
is still pending. Earlier experimental rule revisions are incompatible.

```powershell
./tools/check-superset-blood-map.ps1 -DukeGrp D:/dos/duke3D/duke3d.grp -BloodRff D:/prog/isoraze/build-isoraze/run/blood-data/BLOOD.RFF -EnableSound
```

The fixture checks E1M1, fresh E1M2 loads and restoration, native-size Caleb,
native TNT-barrel health/hitscan flags, actual flare input/ammo, ignition/source
attribution and subsequent native burn damage. Its high-health, paused zombie
is diagnostic setup, not a changed enemy rule. It checks native Blood scheduled
FX restoration/expiry beside native Duke ticking, decal growth limits, queue
capacity/eviction, save/load/GC and complete foreign actor removal. Ignition is
checked separately from health loss because the native flare configuration can
ignite first and deal health damage through later burn ticks.

This is the first reverse-world component, not full Duke combat in Blood maps.
Native collision/hit structures still contain game-specific typed casts. Duke
weapons, other projectile families and enemies need further identity/collision services
before they can safely interact with arbitrary native Blood actors. Their CON
and map queries also need appropriate environment handlers. Merely invoking a
second full simulation or changing global game flags would not provide that.

## Native Duke laser projectiles in Blood maps

The reverse adapter now runs the original `DukeFirelaser` projectile and its
native trail class alongside Blood actors. The existing `DukeProjectile.Tick`,
native movement and class callbacks remain in use. Component hooks supply the
map environment and intercept contact before the Duke-only sprite cast or Duke
wall/sector mechanism handlers. This does not run Duke's full simulation.

`CoreActor.SpawnSharedDukeProjectile(class<DukeActor>, position, velocity)`
uses this actor as the attack source. Position is a world coordinate and velocity
is world displacement per 30 Hz simulation step. Currently it accepts the stock
`DukeFirelaser` class from a native Blood actor in Blood maps; unsupported
sources, classes, invalid inputs and positions outside the map return null.
`superset_spawn_duke_projectile laser` fires this component from Caleb's actual
eye and view direction as a diagnostic, without adding a weapon to inventory.

Duke's mover consumes half its stored velocity each step. Explicit conversion
at the shared spawn boundary maps its native laser speed of 52.5 to 26.25 world
units per step. Both horizontal and vertical velocity use this conversion;
native Build coordinate rounding remains part of the mover. Blood missiles
already consume world displacement per step. This is a representation change,
not a balance multiplier or a map/character rescale. The native player/NPC
projectile sizes and 7-plus-0-through-7 damage variation are retained. The shared
core random seed and projectile payload are already saved by their owners.

Duke clips horizontal movement at the previous height before advancing Z. A
descending shot can therefore enter a short Blood object between those checks.
For foreign vertical travel, the adapter also traces the actual 3D movement
segment through the shared engine. If the native mover reports no collision
but this segment hits an actor, it supplies that missed sprite contact. Native
wall/plane contacts and unobstructed movement remain authoritative. This was
verified with Caleb firing down at a native TNT barrel on a lower step; changing
the target's dimensions or health handler would not fix that collision problem.

Blood's existing `GetZRangeAtXYZ` now accepts generic collision results, and its
actor `CheckLink` accepts `CoreActor`. Their native depth and paired room-link
algorithms are reused directly. Blood actors retain their original interpolation
behavior; a foreign actor backs up its pose after a link to avoid interpolating
across rooms. The projectile reads Blood's extended underwater flag and water
markers instead of interpreting Blood sector types as Duke water tags. Sky and
solid planes absorb the current laser; Blood vector-trigger walls receive their
native wall-impact event. Other mirror and surface responses remain pending.

Projectiles carry a saved, GC-traced generic attack source rather than a fake
Duke player pawn. Blood players, enemies and things receive Physical health
damage through the common receiver, including armor, resistance, native pain,
retaliation and death credit. Impact does not implicitly request burning or
shared knockback. Foreign projectiles/trails remain nonblocking to Blood's
typed weapon and movement queries. The native projectile mover still sees real
Blood colliders and excludes its source through the Blood missile environment.

Native trail animation normally marks a trail for removal when it is processed
by rendering. The foreign component also retires an unseen trail after one
render opportunity, so an unrendered room cannot retain it indefinitely. Its
existing actor counter saves this short lifetime. Native Duke mode keeps its
original behavior. Map teardown removes the foreign actors with the native
world; no extra event queue or second full serializer is introduced. Foreign
attacks run after the native Blood actor phase, so the frontend commits Blood's
deferred actor transfers/removals again at the end of that component phase.
Native death state must be complete before a frame can be saved or collected.

Rules revision 11 includes this component and rejects earlier experimental
revisions. Duke weapons/inventory, enemy controllers, secondary CON execution,
Duke sounds, other projectile families, special body effects and broader
cross-game attacker identities in Blood maps remain unfinished.

```powershell
./tools/check-superset-duke-projectiles.ps1 -DukeGrp D:/dos/duke3D/duke3d.grp -BloodRff D:/prog/isoraze/build-isoraze/run/blood-data/BLOOD.RFF -EnableSound
```

The fixture selects an existing open, dry room in E1M1 while retaining Caleb's
native size. It tests both-way room links, water roles and optional native
underwater slowdown, and an actual Blood wall event. Slow flight makes native
motion measurable through save/load, a fresh E1M2 and full GC; the projectile
then resumes at normal speed. Further shots test enemies, a short native TNT
barrel, player armor, death credit, wall/floor contacts, looking away, and actor
cleanup. Aiming uses Caleb's actual eye; moving a muzzle down to the height of
a short target on a lower step could otherwise place it below the shooter's
floor. Geometry and character dimensions are not changed to make the shots pass.

Verified on 2026-11-08 with a Release build and audio enabled:

- `duke-projectiles-20261108T081813383`: native flight and XYZ timing, lower-step
  barrel contact, enemy retaliation, player armor, both-way room links, water
  roles/slowdown, Blood wall events, saved source/GC, native death committed in
  the same frame and per-player kill credit, and unseen-trail cleanup.
- `blood-map-20261108T082015137`: the complete existing reverse-map adapter,
  shared health/VM, native flare combat/events and foreign effect save/GC checks.
- `runtime-20261108T080914690`: ordinary Duke E1L1 and Blood E1M1, including
  native flare/spray input, ammunition, missile persistence and reload.
- `freeze-20261108T081017826`: Caleb and Duke native freezer contacts,
  shatter/thaw, independent Cold/fire responses and save/GC.
- `enemies-20261108T081148763`: both characters' pursuit/melee, pitchfork,
  pistol, spray and RPG damage, native deaths, remains and cleanup.

Verified on 2026-11-08: Release build and the complete reverse-map fixture passed
with audio (`blood-map-20261108T063216312`), including all private Duke palette
decoders, native queue insertion/eviction, one tick per Blood frame and counter
restoration. Screenshots were inspected. Original Duke episode starts passed
(`runtime-20261108T062546246`); original Duke E1L1 and Blood E1M1 then passed with
the common save barrier, including native flare/spray persistence
(`runtime-20261108T063143430`). Both characters passed independent weapon-family
handoffs (`weapons-20261108T063249069`) and shared freezer/zombie checks
(`freeze-20261108T063415239`). Caleb E1L1, a fresh E1L2 load, incoming damage,
both death/restart paths and saved FX passed (`runtime-20261108T063933815`).

Fixture saves use the core `mashup_save_wait` completion barrier; the earlier
`superset_save_wait` command remains an alias. This also applies to original-game
checks. Input pulses for native Use/death restart stay held across queued/catch-up
packets; health, armor, movement and restart assertions remain unchanged.

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
are serialized with the session. Save schema is 2; rules revision is now 10
after adding the reverse map adapter and foreign Duke actor state. Earlier
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
| No native Blood category | Cold, with an independent shared response factor |

These categories come from `actDamageDude`, the per-enemy `damageVal` table,
player immunity and armor dispatch. They are not weapon/enemy pair rules.
Heat damage does not itself request ignition. Knockback, burning, freezing and
shrinking are separate concerns. The shared Duke receiver currently accepts
Physical and Heat; other categories need native response adapters. Blood keeps
its seven native responses; the Duke-world axe-zombie adapter also adds an
independent Cold factor. Freezing's
explicit defeat handling is described below; shared shrinking remains pending.

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
Native Blood axe-zombie receivers now preserve Q4 damage directly, including
their response factors and special burning behavior. Duke actors also accept
Heat through the later burning increment. Incoming Caleb damage and impulses
are handled by the next increment below.

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

## Common health entry point in both map families

`Superset::ApplyHealthDamage(CoreActor*, Damage, CoreActor*)` selects the
target's current receiver. Native Blood players, dudes and damageable things
use `actDamageSprite` in Blood maps. The existing Caleb controller, Blood enemy
facades and Duke native hit handlers remain the receivers in Duke maps. Flame
impact/burning and mixed enemy/player hit paths now use this common boundary.
No second game loop or global game-flag changes are involved.

The script equivalents extend `CoreActor`:

```c
target.ApplySharedHealthDamage(SD_Physical, 136, source); // 8.5 health points
target.ApplySharedDukeHealthHit(hitter, source);           // explicit attribution
```

`ReadDukeHitDamage` converts an existing shared payload or a recognized Duke
attack's integer `extra` value to Q4 health. Shot sparks, melee and lasers map
to Physical; explosive actors map to Blast. Freeze/shrink/grow attacks remain
outside this ordinary hit conversion. Callers still own collision, self-hit,
friendly-fire and attack-specific modifiers before submitting an accepted hit;
this health API does not run an entire projectile contact handler.

Blood's native tables, armor, player immunities, AI pain/targeting, death
sequences, triggers and kill credit remain authoritative. No attack/enemy pair
table is introduced. The API does not request impulse, ignition, freezing or
shrinking. Native response extensions remain meaningful: Tesla damage can set
Blood's stun flag and Heat can transform vulnerable low-health enemies into
burning forms. Those are retained native handlers, not a claim that all original
behavior is reproduced by resistance parameters alone.

`true` means a supported receiver handled the request, including native
invulnerability or immunity; it does not promise health loss. The current input
range matches the existing Duke payload adapter, 0 through 32767 health points
(524272 Q4 units), before receiver-specific scaling. Invalid amounts,
categories, deleted objects and unsupported receivers return `false`. Zero
native Blood damage does not enter AI or cause a Tesla reaction. Native Blood
currently accepts its seven damage categories and native Blood/environmental
sources. Cold responses on arbitrary native Blood actors, and attribution or
retaliation against a foreign actor without a Blood identity, require further
adapters and return `false`. Existing Cold/freezing support for the Duke-world
axe-zombie facade remains available. These limits keep pending crossover rules
explicit; full Duke weapon controllers and other projectile families in Blood
maps remain pending. The reverse laser component is described above.

`superset_health_check` invokes both public VM entry points against live native
Blood actors. It checks Duke physical hit conversion, fractional health, seven
category responses, native Tesla stun, separate ignition/impulse, blocked and
invalid requests, Blast mapping, Caleb armor, damageable things and native death
with player kill credit. Its seed/saved phases verify native Q4 health and AI
target restoration through a fresh map, save/load and full GC. Test deaths use
Blood's deferred removal queue, including any native drops or gibs.

The reverse-map harness runs these checks alongside native flare combat,
native events and isolated Duke effect ticking. Original-game and Duke-world
combat regressions are required when this common boundary changes.

Verified on 2026-11-08 with the Release build and audio enabled:

- `blood-map-20261108T071252885`: both public VM entry points, native damage
  responses, Q4 save/GC, death credit and the full reverse-map fixture.
- `enemies-20261108T070938675`: Caleb and Duke pursuit/melee, native weapon
  damage, burning transformation, deaths and cleanup.
- `freeze-20261108T071055647`: both characters, native freezer projectiles,
  independent Cold/fire responses, shatter/thaw and save/GC.
- `runtime-20261108T071337954`: ordinary Duke E1L1 and Blood E1M1, including
  native flare/spray input, ammunition, missile persistence and reload.
- `runtime-20261108T071445345`: Caleb E1L1, fresh E1L2, incoming damage,
  ordinary/blast death and restart, movement and shared hit/save checks.

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
their engine sprite/SEQ representation. Native Blood five/eight-view rotation
is now shared by the axe-zombie adapter. Shadows, other object-specific
rendering and foreign FX sound/material routing remain separate
integration work.

Shared object ticks advance Blood sequences once per simulation clock, process
native object callbacks, run native FX gravity/drag/movement and drain deferred
removals. This runs independently of selecting Caleb or Duke. The secondary
event queue is serialized and cleared with its world; Blood map mechanisms are
not started by this hook. The tick now advances native axe-zombie AI/movement;
Blood thing/gib-body physics and other enemy families remain unfinished.

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

## Native Blood pickups and hitscan guns

Superset now uses native Blood weapon and pickup state for the pitchfork,
sawed-off shotgun and Tommy gun. Native QAV callbacks retain weapon selection,
raising/lowering, primary/alternate firing, spread, cadence, ammunition use,
reloads and switching after ammunition runs out. Bullet vectors submit shared
Q4 Physical damage through Duke hit/CON callbacks, preserving native Blood
damage amounts. They do not consume Duke ammunition. Flesh impacts use native
Blood sound/FX; full material effects and autoaim against Duke actors remain
integration work. Manual aiming works through the native hitscan path.

Caleb enables this weapon family automatically. Either character can use it
with `-superset-weapons blood` or the runtime `superset_weapons blood` command.
`superset_weapons duke` selects the native Duke inventory, including for Caleb;
`superset_weapons auto` restores the character default. The archived
`superset_weapon_profile` setting uses 0/1/2 for auto/Blood/Duke. Blood pickups
remain collectible with either family active. Legacy `+mashup_combat 1` still enables
Blood weapons when the family setting is auto. This toggle still enables
only the pitchfork in the older `-bloodduke` experiment. Blood armor is consumed
by Caleb's native damage receiver; the optional Duke movement/combat profile
stores it but still uses Duke's incoming damage/shield receiver. That unfinished
armor adapter is not a decided rule for character-specific armor restrictions.

For now, place actual Blood items through the console, for example:

```text
superset_spawn_pickup shotgun
superset_spawn_pickup tommy
superset_spawn_pickup shells
superset_spawn_pickup drum
superset_spawn_pickup bodyarmor
```

The command without arguments lists supported names: `shotgun`, `tommy`, `spraycan`, `sprayfuel`,
`shells`, `shellbox`, `bullets`, `drum`, `medpouch`, `essence`, `lifeseed`,
`redpotion`, `basicarmor`, `bodyarmor`, `firearmor`, `spiritarmor`, `superarmor`.
Items spawn on the floor ahead and retain native artwork, collection distance,
line-of-sight requirements, amounts, health caps and full-inventory refusal.
Native item callbacks apply inventory changes; environment services handle
collection policy and removal without starting Blood map triggers in a Duke
map. Spawned items have no respawn configuration. Other weapons, powerups and
automatic Duke pickup conversion are pending.

These are native Blood amounts: a shotgun grants 8 shells, a Tommy gun grants
50 bullets, Life Essence heals 20 and Medicine Pouch heals 50 up to 100 health.
Body Armor grants its native physical pool; Super Armor supplies all three
pools. Their raw values remain Q4 units. Whether converted Duke pickups should
grant Blood amounts or preserve Duke amounts remains an open gameplay decision.
The extra readout displays the active Blood weapon/ammunition and Caleb's three
armor pools. The remaining status bar still belongs to the Duke map adapter.

`check-superset-inventory.ps1` uses the existing asset-free navigation fixture
with monsters disabled. It resets the player to a flat room between placements;
normal movement input collects actual items. Native fire input and temporary
Duke targets verify both fire modes, vector counts/types, ammo use and damage
ownership. The test checks health-cap refusal and restores weapons, ammunition,
armor, health and an uncollected item across save/load. Its setup commands are
diagnostics, not alternate inventory implementations. `mashup_combat_check`
resets the native Blood inventory and should only be used in disposable checks.

```powershell
./tools/check-superset-inventory.ps1 -DukeGrp 'D:/games/duke/duke3d.grp' -BloodDirectory 'D:/games/blood' -PythonPath python -EnableSound
```

Verified on 2026-11-08: Release build and both character profiles passed the
inventory/fire/save checks with audio (`inventory-20261108T020934052`).
Shotgun/readout screenshots were inspected. Caleb E1L1 and a fresh E1L2 load
passed the existing movement, fractional damage, armor, death/restart, shared
FX and save checks (`runtime-20261108T021142379`). Original Duke E1L1 and Blood
E1M1 passed runtime/save regressions (`runtime-20261108T021249822`). These are
component checks on local Plutonium Pak data, not stock-map playthroughs or
proof of automatic pickup conversion and arbitrary CON compatibility.

## Native flame projectiles and shared burning

The spray can's primary attack now uses Blood's native weapon/QAV callback,
fuel consumption, missile artwork, velocity, drag, collision algorithm and
sequence/event lifetime. Select it with slot 7 after collecting `spraycan`;
`sprayfuel` supplies its native ammunition. Caleb and Duke's optional Blood
weapon profile can both use it. Alternate throw/drop requires thing physics
and explosion/environment adapters; alternate fire is disabled for this weapon
until those work. Other missile weapons remain outside the selectable set.

`actSpawnMissile` extracts native missile initialization. `actMoveMissile`
retains native movement with a generic collision result and an injected
environment interface for collision ownership and room links. The original
Blood caller keeps its native impact handlers, wall triggers and room links.
The Duke environment excludes the physical shooter from collision and routes
flame contacts to shared gameplay without treating Duke actors as Blood actors
or starting Blood map mechanisms. Duke projectile transport/water-portal
variants are not implemented by this adapter yet.

Duke's player velocity is copied into the secondary weapon state in the same
world-units-per-step representation. Native flame initialization can therefore
retain its half-source-velocity inheritance with either movement profile.
For emitted flame-lick FX, Duke enemy forward speed is converted to a vector
using its facing angle; it is not interpreted as Blood's world-space X speed.

Flame contact submits two separate operations: eight Q4 units of Heat damage
(0.5 health point), and ignition. Ignition adds Blood's native
`(4 + difficulty) * 4` clock duration, capped at 2400 clocks for enemies.
The ordinary burn tick then removes four clocks and submits eight Q4 Heat units,
matching Blood's ordinary burning tick. Repeated native flame contacts can
extend the duration. `superset_blood_difficulty` supplies the source difficulty
for this formula (0–4, initially 2), independently of Duke's native difficulty;
it is saved with the session. This is not a resolved mapping between the games'
difficulty curves. The axe-zombie adapter also uses this source difficulty for
its native AI and damage-response factors; other Blood enemy families are not
enabled yet.

Duke enemies default to normal Heat damage and allow ignition. Actor defaults
can override `SupersetHeatFactor` (0 means health immunity, 0.5 halves damage)
and `SupersetBurnable` independently. Thus Heat damage does not automatically
ignite an actor, an unignitable actor can still take direct Heat damage, and a
Heat-immune actor can still receive the effect unless ignition is also disabled.
These are configurable starting responses, not settled boss or mod-specific
exceptions. Factors use shared Q4 precision and must be finite within 0–256.

Health-only hits retain native Duke `onHit`/CON consumption and death handling,
with fractional damage carried as before. They suppress the default hit push,
damage turn, shotgun blood and hit gibs. They do not add impulse as a side effect
of burn damage. Burning does not yet switch Duke enemies to a specialized
burning AI/death animation. Fire versus frozen actors, extinguishing policy,
and special actor exceptions still require explicit rules. Existing native
CON responses to special states remain authoritative in this adapter. The shared
Blood freezing/thawing receiver is described in the later freezing section.

Burn duration, visual cadence and source identity serialize on the native world
actor, with GC tracking. Attribution uses the physical player rather than the
secondary controller, and emitted projectiles retain that resolution after the
Blood weapon mode is disabled. Blood's flame-lick FX spawning is reused, with
extents adjusted for centered Blood sprites and feet-based Duke sprites.
Shared object ticks advance projectiles and statuses once after the Duke world
phase. Native axe-zombie AI now shares this phase; the initial Blood-map
bootstrap is described above, and reverse combat remains pending.

```powershell
./tools/check-superset-flames.ps1 -DukeGrp 'D:/games/duke/duke3d.grp' -BloodDirectory 'D:/games/blood' -PythonPath python -EnableSound
```

The fixture test uses actual pickup/input/QAV/projectile processing and a
controlled native Duke target. It checks fuel use, contacts, damage without
impulse, burn ownership, restored moving missiles and burn state, source
difficulty restoration, ownership after disabling the weapon mode, expiry,
separate heat/ignition responses and actual native CON death. A final movement
check verifies that nonzero player motion reaches the weapon state. Target AI is
paused during timing checks; this is not a combat playthrough. Add
`-BloodProjectiles` with `-BloodRff` to the ordinary runtime harness to exercise
native Blood flare/spray input, ammunition, saved flame missiles and reload.

Verified on 2026-11-08: Release build passed. Both profiles passed the flame,
response, save/ownership/expiry and motion checks with audio
(`flames-20261108T024153548`), and the burning/spray screenshot was inspected.
Caleb's inventory/hitscan regression passed (`inventory-20261108T023935061`).
Caleb E1L1, save/load, damage, death/restart and fresh E1L2 checks passed
(`runtime-20261108T023737629`). One preceding E1L2 movement probe reported no
jump/crouch; a rerun passed without a controller change. Its cause is not
established; the movement diagnostic now records received input and health
details on failure. Final original Duke E1L1 and Blood E1M1 checks, including
native Blood flare/spray firing, saved flame missiles and reload, passed
(`runtime-20261108T024442754`). The final Duke-profile flame/motion rerun also
passed (`flames-20261108T024541017`). These remain component checks, not full map or
enemy-family compatibility coverage.

## Native Blood axe zombies in the Duke world

`superset_spawn_enemy zombie` spawns a normal Blood axe zombie in clear space
ahead of the player. It uses native `actSpawnDude`, AI states, movement, attack
callbacks, SEQ animation, pain/resistance responses and death callbacks. Its
native transformation into a burning axe zombie is preserved. No Duke enemy
CON program substitutes for its Blood AI. Only this enemy family is enabled
while additional native weapon and world interactions are integrated.

The map-facing `SupersetBloodEnemy` pawn supplies the one physical collider and
Duke targeting identity. A linked, invisible/nonblocking `DBloodActor` owns the
native brain, velocity and fractional health. These are two representations of
one enemy, not separate simulations/processes or two colliding monsters. Native
Blood five/eight-view sprite rotation is reused. Both characters have a native
Blood target alias even when Duke uses his own weapons with `mashup_combat 0`.
Character selection therefore does not determine which enemy can attack him.

The existing movement environment service now separates object movement from
player-only use/pickup/restart operations. Native Blood dude drag, gravity and
collision remain in that subsystem. Collision queries map adapted world actors
to their native state; unadapted Duke actors still obstruct geometry without
entering Blood's typed trigger paths. Duke map mechanisms remain on the Duke
side. NPC water/transport/door behavior and broader hazards need further work;
the player navigation fixture does not establish NPC navigation compatibility.

Shared damage dispatch reaches `actDamageSprite` before Duke's integer-health
facade. All seven Blood category mappings are available at this receiver,
retaining its per-category native response factors and Q4 health. Native Duke
physical and flagged explosive attacks use the existing category mapping.
Blood vector impulses are applied separately using native vector impulse and
target mass; native Duke hits use a separate impulse conversion. Flame contacts
route Heat and ignition independently. Blood burn timers, native AI transitions
and special death sequences remain native, rather than using Duke's generic
burning receiver. The point-blank missile-spawn scan also now matches Blood's
native four-times-clearance query and backs off at contact as native code does.

Native source difficulty comes from `superset_blood_difficulty`, including the
original `DudeDifficulty` response curve for the normal/burning axe zombie.
This is source behavior preservation, not a new shared balance multiplier.
Player/adapted-enemy attackers resolve to native aliases. An unadapted Duke NPC
attacker is retained as a generic source, without a fabricated self-hit; native
Blood retaliation against that foreign NPC remains unfinished. Native Duke
shrinker/grower hits are not interpreted as ordinary zombie damage:
they await shared effect handling. Freezer hits now have the separate Cold and
freeze-on-defeat adapter described below.

World/controller/source references, health, AI targets and native sequences are
GC-tracked and saved. Native deferred status transfer is preserved: the pending
bit can mean conversion to a corpse, not deletion. A native corpse may retain
positive thing health without being a living enemy. Its world facade becomes
nonblocking/dead, while its death SEQ can finish. Unbound foreign Blood things
and gibs are currently visual/nonblocking; their native flags/data remain, but
full thing/corpse movement, bouncing, damage and kicking are not integrated.
Foreign flame missiles retain native collision flags during their own mover
and are nonblocking during Duke's typed object-touch phase.

```powershell
./tools/check-superset-enemies.ps1 -DukeGrp 'D:/dos/duke3D/duke3d.grp' -BloodDirectory 'D:/prog/isoraze/build-isoraze/run/blood-data' -PythonPath python -EnableSound
```

This generated Duke-map fixture observes native pursuit and melee health loss
against both characters, one collider, bidirectional ownership, saved/restored
AI targets after a fresh map load and full GC, actual player-input kills and
continuing native death animation. Caleb uses his native pitchfork and spray
can; Duke uses his own pistol and RPG with native ammo consumption. The spray
case observes native burn health/duration, transformation and eventual death.
The test also moves past nonblocking remains, removes the world pawn and expires
the native controller independently, checking both cleanup paths after full GC.
It does not replace a stock-map
combat/navigation playthrough or prove other Blood enemy families.

Verification on 2026-11-08: the Release build and enemy checks passed with audio
for both profiles, including native pawn/controller removal and full GC
(`enemies-20261108T034209161`). Duke's pistol/zombie screenshot was inspected.
The related regressions also passed:

- `runtime-20261108T033524545`: ordinary Duke E1L1 and Blood E1M1 runtime/save/load,
  including original Blood flare/spray input, ammo and saved missiles.
- `runtime-20261108T033705381`: Caleb E1L1, save/load, damage/death/restart and a
  fresh E1L2 load, with audio. The preceding run reached E1L2 but exceeded the
  default 45-second harness timeout; this run used its supported 60-second limit.
- `navigation-20261108T033849256`: all eleven player navigation checks for both
  profiles after splitting the movement environment service.
- `flames-20261108T033943852`: both profiles' shared burning, response, ownership,
  persistence, expiry, CON-death and movement checks after the muzzle/collision fix.
- `inventory-20261108T034505256`: both profiles' native pickups, shotgun/Tommy
  primary/alternate fire, independent ammo and saved inventory/items, with audio.
  The inventory harness now has a configurable 90-second default timeout: its
  scripted waits alone consume about 53 seconds, leaving inadequate startup/save
  margin with the previous 60-second limit. The prior run passed its assertions
  but reached that limit just before exit.

## Independent character and weapon families

The character continues to own movement, size, health and incoming damage while
the selected weapon family owns selection and attack timing. Caleb reuses Duke's
native weapon input, selection, quick kick and firing routines without running
Duke movement or inventory controls. Caleb's normal Blood armor and native
damage receiver remain active. Map access/tip interactions keep their Duke
timers and can temporarily prevent firing as in the original map simulation.

Changing family retains both inventories, ammunition and the last Blood weapon.
It releases attack buttons and lets the current native shot/reload/throw finish
before handing off. Supported Blood weapons must reach a native idle QAV;
timer zero alone can mean an intermediate reload stage. Duke weapon timing
similarly remains native. Returning to Blood raises the stowed weapon through
native selection/ammo checks. Existing projectiles and effects keep their owner
when the emitting family is no longer selected.

Rules revision 9 saves the requested family and each player's applied family,
stowed weapon, both inventories and pending native action. A save taken during
handoff resumes that action without refunding/repeating it. Older Superset rule
revisions are intentionally rejected. Ordinary Blood and Duke save formats keep
their previous signatures and behavior.

This is two selectable native weapon inventories, not yet a single list of all
weapons. Blood's supported set remains pitchfork, shotgun, Tommy gun and spray
primary. Native Duke weapons are selectable, but their special interactions need
individual validation; pistol and freezer have actual-input coverage for Caleb.
For example, tripbomb planting directly changes Duke's body/vertical velocity;
that dedicated interaction still needs a Blood-controller hook.
Selecting Duke weapons does not activate Duke inventory controls/jetpack for
Caleb. Blood sprite repeats are not interpreted as Duke's shrunken HUD state;
the normal Duke weapon is drawn without changing Caleb's physical dimensions.
Shrinking, player freezing, other Blood weapons in Duke maps and reverse combat
remain pending. Duke pickups retain Duke inventory ownership; automatic
cross-game pickup conversion and the common armor policy remain open decisions.

```powershell
./tools/check-superset-weapons.ps1 -DukeGrp 'D:/dos/duke3D/duke3d.grp' -BloodDirectory 'D:/prog/isoraze/build-isoraze/run/blood-data' -PythonPath python -EnableSound
```

The fixture checks both characters, Blood pickups while Duke weapons are active,
independent pistol/shell ammo, a handoff requested during shotgun alternate fire,
stowed selection, fresh-map/load/GC restoration and replay of the pending action.
Caleb additionally exercises movement/size and Blood health/armor with Duke guns.
Save phases await completion through the ordinary input stream. Input pulses
around saves also allow the native input pipeline to settle before firing.

Verified on 2026-11-08: Release build and both character profiles passed
(`weapons-20261108T051330934`), including Caleb's Blood incoming-damage receiver
with Duke weapons selected. After correcting the foreign sprite-repeat HUD gate,
Caleb passed again (`weapons-20261108T051700118`); its screenshot shows the native
Duke pistol and the separate Blood armor readout. Both profiles passed actual
freezer input, native ammo, frozen AI/SEQ, shatter, thaw and save/GC
(`freeze-20261108T051514271`). Original Duke episode starts and Blood E1M1,
including actual flare/spray ammo and missile persistence, passed
(`runtime-20261108T051746355`). These are focused fixtures, not a full campaign
or arbitrary-mod compatibility claim.

Navigation passed for both characters using Duke weapons
(`navigation-20261108T052329860`) and Caleb using Blood weapons
(`navigation-20261108T052418233`). All eleven map assertions remain in place:
doors, lift, access lock/key consumption, repeated transport/cooldown, exit
clock, water transitions and restored map state. The fixture holds Use across
the queued input window instead of relying on two-tick console pulses, and
waits for the checkpoint save to complete before altering/loading the scene.
Both native Blood inventory profiles also passed pickup amounts/caps,
shotgun/Tommy primary and alternate fire, independent ammunition and saved items
(`inventory-20261108T052441953`).
The stock-map controller regression passed E1L1, save/load, incoming damage,
both death/restart paths, FX expiry and a fresh E1L2 load
(`runtime-20261108T052906437`). Its movement pulses now hold across queued input,
with crouched horizontal travel to keep the roof probe local; movement, native
eye/radius, single-collider and clock assertions are unchanged. Caleb's complete
weapon-family fixture passed with the same input pacing
(`weapons-20261108T053010365`).

## Shared freezing and Cold damage

The first foreign freezing receiver supports the native Blood axe zombie and
its burning form. Duke's original freezer projectiles keep their native firing,
autoaim, velocity, bounce, damage randomization and already-frozen contact gate.
For this Blood receiver, health depletion submits Cold damage; a hit that would
defeat a freezable target instead starts a timed freeze. Duke CON enemies retain
their native frozen-state algorithms. This is an effect boundary with a Blood
adapter, not a migration of every Duke CON state into the shared ticker.
The native freezer hit impulse is applied separately from its Cold response;
entering the frozen state stops horizontal velocity.

`status.h` provides separate freeze, thaw, shatter and frozen-state queries.
Cold health damage alone does not request freezing or shattering. The Blood
receiver uses `SupersetColdFactor` (default 1, zero means damage immunity) and
`SupersetFreezable` (default true) independently. No Cold slot exists in Blood's
seven-entry native response arrays. The adapter supplies the shared factor and
reuses the ordinary native hit/AI/death handler, without borrowing Bullet
resistance, Heat ignition or Tesla recoil. A generic Cold hit can kill normally;
the freezer's explicit defeat handler caps that hit before native death.

Stock Duke enemies thaw alive with zero integer health. Blood's native AI stops
at zero, so this adapter retains one Q4 health quantum (1/16 point), including
after thaw. It does not refill the enemy's health. This is an explicit temporary
representation compromise while native integer and Q4 receivers coexist.
The first freezer profile uses stock `THAWTIME`: 138 simulation steps, or 552
shared clocks (4.6 seconds). Mod-specific CON thaw constants are not imported
into this foreign profile yet. Repeated freezer contacts do not refresh it,
matching the native already-frozen projectile gate.

Freezing suspends the target's native Blood AI timers, thinking/movement inputs
and SEQ frame/callback progression. Its gravity, collision, drag and moving-world
physics remain in `actProcessDudes`. Native sequences created while paused keep
a pending first-frame update, applied once on resume before normal timing
continues. This avoids losing a native transformation callback or allowing a
queued melee callback during freezing. Pending updates serialize with the SEQ;
freeze duration and source identity serialize/GC-track on the world pawn.
Native knockdown or recoil resumes with its remaining duration after thaw,
rather than being replaced by a newly invented idle/chase state.

The physical world pawn uses Duke's frozen palette marker for native projectile
and autoaim gates. Rendering selects Blood's isolated `cold.plu` lookup and
retains the paused native frame/rotation. An accepted physical or explosive
weapon hit explicitly requests shattering. That path awards one native kill,
reuses Duke ice/glass fragments and sound, and removes both actor
representations and their native SEQ/events. Health-only effect damage does not
implicitly shatter a statue. Native Duke automatic frozen-target quick-kicking,
water-drip visuals, other Blood enemy families, player freezing and all
boss/mod-specific exceptions still need integration.

**Open rule, provisional default:** burning and freezing currently coexist.
Heat damage and burning do not automatically thaw or shatter. Native Blood
burning transformation/damage can continue while its AI/SEQ is paused; a lethal
health hit ends freezing so native death can proceed. The user-facing fire/ice
question remains open; this default is not a settled crossover balance rule.

```powershell
./tools/check-superset-freeze.ps1 -DukeGrp 'D:/dos/duke3D/duke3d.grp' -BloodDirectory 'D:/prog/isoraze/build-isoraze/run/blood-data' -PythonPath python -EnableSound
```

The fixture exercises Duke's freezer through actual input and native ammo for
both characters. Caleb switches to his Blood weapon family and uses actual
pitchfork input for shattering; Duke uses his native pistol. Snapshots and a
native physics observation check one collider, source/GC ownership, unchanged
AI/SEQ during a vulnerable hold, frozen-body gravity, timed thaw with resumed native brain, native ice
fragments and exactly one kill. Separate diagnostic calls check Cold resistance,
Q4 precision, independent freeze capability and native burning transformation
with a deferred SEQ first frame. These component checks do not prove arbitrary
maps, mods or other actor families.

Enemy, freezing, weapon, inventory and navigation fixtures split their scripts at ordinary saves.
`superset_save_wait` continues only after the normal input-stream save completes,
preventing a later load/map command from replacing a request or changing the
world before its snapshot. A fixed tick delay had produced missing saves,
`TXT_SAVEPENDING`, and a purported corpse snapshot containing the freshly loaded
living zombie. The vulnerable freezing hold also clears leftover freezer bolts
and preparation hit queues first: stock `FREEZERHURTOWNER YES` permits native
ricochets to hurt the shooter, independently of the paused enemy's AI.

Verification on 2026-11-08: Release build and both freezing profiles passed
(`freeze-20261108T042650879`), with audio. Duke/freezer and Caleb/pitchfork frozen
zombie screenshots were inspected. Original Duke E1L1 and Blood E1M1
runtime/save/load, including original Blood flare/spray input, ammo and saved
missiles, passed (`runtime-20261108T042833760`). Both profiles' existing native
enemy pursuit/melee, weapon kills, burning, nonblocking remains and cleanup
checks passed with save-completion synchronization
(`enemies-20261108T043335853`).
Caleb's E1L1 runtime, shared damage, death/restart, FX and save/load checks plus
a fresh E1L2 load passed with audio (`runtime-20261108T043524940`).

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
2. Extend the common health receiver with foreign attacker identity, targeting,
   ownership and remaining response categories in both directions. Retain
   shared fractional units, native AI/weapon behavior, same-game resistances
   and death/trigger callbacks.
3. Expand shared status effects with per-actor reactions: burning on Duke and
   axe-zombie targets and freezing on axe zombies are present; add shrinking, electrical and
   other damage interactions. Coordinate animation/AI callbacks and save state.
4. Route native movement/environment queries to each map adapter's water,
   platforms, transports, use actions and hazards. Extend the Blood map adapter
   and expand the reverse laser/environment component into native Duke weapon
   controllers, other projectile families and enemy AI.
5. Expand selectable mixed inventories, weapons, enemies and pickup policies;
   provide jetpack/shrinking progression abilities. Keep conversions separate
   from tunable balance and verify safe regrowth and passage clearance.
6. Complete resource features and mod dependencies as needed: fullbright masks,
   voxels, music/encoded samples, indexed shading and primary CON overrides.
