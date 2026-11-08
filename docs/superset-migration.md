# Superset runtime migration

This audit starts at `692888be4`, including the native Duke pickup work added
after the review of `9fe0a8a`.

## Keep

- Raze's `DCoreActor`, sector/wall geometry, collision, resource banks, GC and
  serialization provide the common runtime substrate. A new geometry engine or
  second simulation process is unnecessary.
- Native Blood QAV/SEQ and Duke weapon/CON routines provide useful behavior.
  Existing unit conversions, sound isolation and action-completion checks stay.
- Native mechanism algorithms stay behind imported interaction bindings; map
  provenance does not define a gameplay entity's implementation.

## Migrate

- `mashup_player.cpp`, `superset/player.cpp` and `superset/enemies.cpp` currently
  pair physical Duke actors with invisible Blood actors and reconcile pose and
  health through `Publish`/`ImportWorldChanges`. Those are compatibility adapters,
  not the entity model. Migrated enemies must have one real actor and receiver;
  native event anchors may retain source bindings but never simulate that actor.
- `DDukePlayer` and `DBloodPlayer` currently own independent arsenal arrays and
  a global family request controls selection. Superset will own one inventory;
  native routines will access its storage through views. Ordinary game callers
  retain their original storage and save representation.
- `IsBloodMap`/`IsDukeMap` currently choose ordinary movement, damage and pickup
  behavior. For migrated objects, imported properties, receiver capabilities and
  bound mechanisms will determine behavior. Format checks stay at import and
  native compatibility boundaries.

## Vertical slice boundary

The common inventory and weapon bank own selection, pending selection and both
shotguns' distinct ammo/ownership. Native animation state remains specialized.
The common level owns imported entity identity, selected archetype and lifecycle
bindings. Its factory separates source settings/events from replacement choice.
Native mechanism implementations and source-event anchors remain explicit
compatibility boundaries. Replacements are deterministic and serialized, and
unsupported machinery/progression objects are excluded.

No ammo equivalence, armor equivalence, movement rescaling or damage rebalance is
implied by the representation change. Cross-family pickup auto-selection is an
explicit gameplay decision. Player pose compatibility views and unmigrated
content remain temporary adapters until their operations use authoritative state
directly; they must not be described as completed migration.

## Implemented vertical slice (rules revision 16)

| State / operation | Authority and implementation |
| --- | --- |
| Geometry | Existing Raze sector/wall arrays. No copied geometry. |
| Environment | `superset/level.cpp` imports medium and mechanism bindings. Moving geometry remains live; Blood's dynamic water flags are read through their imported binding. |
| Weapon ownership / ammunition | One `DGameplayInventory`, referenced by both native player views. Native C++ accessors and internal VM getters address that storage. Native view serializers omit their old arrays. |
| Weapon selection | Stable shared definition IDs, configurable ordered banks, saved selected/pending IDs. Input selects banks/actions, not a native weapon family. |
| Entity identity / lifecycle | One `DGameplayEntity` with a stable identity, source index, chosen archetype, physical actor, common target/owner/source references, event bindings and once-only flags. |
| Transform / collision / health | The one real native actor. Blood uses native Q4 health; Duke uses its native points plus the existing fractional remainder, exposed through the shared health/target API. No second controller body or reconciliation for migrated enemies. |
| Enemy implementation | Real axe zombie or Pig Cop. Native AI/SEQ or CON/animation runs as a component; a foreign Pig Cop uses native wakeup and an explicit safe CON service. |
| Map events | Source Blood sprite anchors retain TX/command/trigger state. Common death/pickup emits through these original event nodes. Native sector interaction bindings invoke Blood or Duke door algorithms. |

Import decodes supported source actors before applying `superset_replacement`
(0 original, 1 opposite game, 2 deterministic seeded mixture). The policy and
seed are captured at level import. Chosen archetypes, source anchors, native
controller state and lifecycle flags serialize; loading does not call import or
reroll. `ImportRuntimeObject` and the same factory also accept later objects.
Common runtime spawning creates actual actors, rather than the old paired
`SupersetBloodEnemy` prototype. Imported enemies retain the source kill total;
runtime enemies add one to it. Repeated native death notifications cannot add
another kill or emit the original event again. CON's provisional freeze kill
notification is resolved after the script chooses its state, before emitting a
real death event.

The supported import archetypes are ordinary Blood axe zombies, Duke Pig Cops,
and each game's shotgun pickup. Replacements preserve feet position and yaw,
then use the selected archetype's native size/controller. Native health,
resistance, ammo capacity and animation algorithms remain intact. Active
respawning, modern Blood maps, incoming Blood RX/key/drop bindings and unsupported
AI guard/deaf/ambush settings or archetypes stay outside replacement. Machinery
is never matched as an enemy. When a replacement wakes, its Blood event node
receives the original AI's activation-state transition, so an initially idle
source can still send its death TX event. No shadow AI or pose/health copy runs.

Pickup switching is one archived setting for the whole arsenal:
`superset_pickup_switch 0` is manual, `1` selects newly owned weapons (default),
and `2` selects every accepted weapon pickup. Ammo-only pickups do not select a
weapon. A pickup that grants a weapon is treated as a weapon grant. Character
and source game do not select another policy. Both shotguns remain owned in
bank 3 and retain separate ammo; repeated 3 cycles them. Native QAV/weapon
controllers finish pending actions before a cross-controller handoff.

## Explicit remaining migration boundaries

- The frontend still delegates the original whole-map mechanism/world phase to
  one native provider. The registry/interaction API is now common, but this is
  not yet a scheduler for arbitrarily mixed Blood and Duke mechanisms. Import
  currently runs after native map initialization, rather than replacing both
  native loaders in one change.
- Caleb's player pose/health compatibility views still use
  `Publish`/`ImportWorldChanges`. They now share arsenal storage, but player
  actor consolidation is not finished. The old console-spawned paired enemy
  remains available for its existing freeze/flame regressions; it is not used
  by the common entity factory.
- The verified slice is single-player. Duke's character controller is still
  unavailable on Blood imports; Caleb can use both imports. This is a temporary
  controller/environment boundary, not an inventory ownership rule.
- Armor, keys, packs and powerup timers remain native inventory components.
  Existing native VM array fields remain for original games/older external
  scripts; Superset internal code uses accessors. External scripts writing
  those legacy fields directly need migration.
- Shared health dispatch and hitscan targeting cross actual actor classes.
  Typed seekers, area attacks and all cross-enemy status responses are not
  comprehensively migrated. Existing paired-enemy freeze/thaw/fire behavior is
  preserved; it is not proof that the new direct axe zombie has every status
  capability yet.
- Foreign Pig Cop CON intentionally stops on unsupported map-specific commands.
  Its optional shield drop still needs a native armor pickup adapter and reports
  that limitation explicitly. No rule converting Duke shield into Blood armor
  is implied. Arbitrary CON mods are not an acceptance claim for this slice.
- The direct Blood controller retains Blood's physics, including drag, impulses
  and moving-surface behavior. A common configurable thing-physics component
  exposing all bounce/collision policies remains to be extracted.
- Caleb remains full size (standing eye 56.6875 world units; Duke's native eye
  is 40). Duke's native autoaim also excludes targets within 32 world units.
  Tests found that a short Pig Cop at point-blank distance can sit below Caleb's
  unpitched shot. This is a compatibility decision for a future aim profile;
  neither map scale nor the native cutoff is silently changed here. Complete
  cross-controller room-link/transport support also remains beyond the door
  interaction slice.

The next concrete migration is to move Caleb's controller onto common actor
state access, replacing its player pose reconciliation. In parallel scope after
that, migrate direct-actor status capability/state from the paired enemy, then
expand inventory components without equating armor or ammo implicitly. Death
inventory retention across character profiles, shield/body-armor interaction,
and shrink responses of individual enemy types still need explicit gameplay
rules.

## Focused verification

`tools/check-superset-level.ps1` loads original E1M1/E1L1, imports native source
fixtures through the same object boundary, replaces enemies and pickups both
ways, checks one physical body, kills through the common receiver and validates
once-only kill credit after replay/load. A Blood fixture's original TX channel
opens an actual original-map door; its pickup retains the native trigger.
Native doors from both maps also move through the common interaction API.
Changing the configured policy/seed and loading restores saved choices.
The checks explicitly disable isometric presentation. A real Duke shotgun
attack must damage the selected common enemy in each format. The Blood event
save captures a door still moving; restoring it finishes the original native
mechanism without another death notification.

`tools/check-superset-arsenal.ps1` checks all three pickup-switch policies for
both source games in both map formats. It verifies one serialized arsenal,
both shotguns in bank 3, native Duke fire, Blood two-shell alternate fire,
safe saved pending handoff, next/previous and configured saved bank order.
Older regression readers expose aliases to that one inventory in test memory;
the new arsenal check inspects the raw save and rejects duplicate native arrays.

Verified local Release runs on 2026-11-08 (under `build-mashup/checks/`):

- `level-20261108T133017023`: both original map formats, first-person native
  shotgun damage to the common target, initially idle replacement source,
  death TX/native moving door, kill credit, pickup events, save/GC/seed restore.
- `arsenal-20261108T132356980`: both formats, all pickup-switch policies,
  native primary/alternate consumption, pending action restore, cycling and
  bank order restored after an actual fresh map load.
- `weapons-20261108T125941713`: existing Caleb and Duke weapon/controller,
  inactive pickup, native ammo and saved pending-action regression.
- `freeze-20261108T130357449`: both existing paired-enemy freeze profiles,
  paused AI/SEQ, saved shatter/thaw, freezing extinguishes and fire thaws.
- `blood-map-20261108T131732270`: native Blood combat/events, shared receiver
  VM/category/source checks, foreign effects, native queues and save/GC cleanup.
- `duke-program-20261108T133214479`: normalized native CON compiler parity,
  isolated private program/player state, VM events, fresh-process save restore
  and content fingerprint rejection.
- `runtime-20261108T131850387`: ordinary Duke E1L1/E2L1/E3L1/E4L1 and Blood
  E1M1 startup/save/load, including native Blood flare/spray persistence.

These checks establish the slice above, not complete playthrough compatibility
or support for arbitrary CON mods, every item, enemy or mechanism combination.
