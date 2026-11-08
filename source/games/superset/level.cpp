#ifndef NOONE_EXTENSIONS
#define NOONE_EXTENSIONS
#endif
#include "../duke/src/duke3d.h"
#include "../duke/src/dukeactor.h"
#include "../duke/src/concmd.h"
#include "../blood/src/blood.h"
#include "level.h"
#include "superset.h"
#include "enemies.h"
#include "damage.h"
#include "duke_pickups.h"
#include "duke_weapons.h"
#include "mashup_player.h"
#include "mashup_resources.h"
#include "savegamehelp.h"
#include "c_dispatch.h"
#include "c_cvars.h"
#include "gamestate.h"

namespace Duke3d { void TickActor(DDukeActor*); }
CVAR(Int, superset_replacement, 0, CVAR_ARCHIVE) // 0 original, 1 cross-game, 2 seeded mixture.
CVAR(Int, superset_replacement_seed, 0, CVAR_ARCHIVE)

namespace Superset
{
namespace
{
enum BindingKind { BloodSprite, BloodSector, DukeSector };
struct Binding { int kind = 0, sector = -1; TObjPtr<DCoreActor*> node; };
struct SectorProperties { int medium = 0; bool bloodMediumBinding = false; double previousFloor = 0; };
TArray<TObjPtr<DGameplayEntity*>> entities;
TArray<Binding> bindings;
TArray<SectorProperties> properties;
int nextIdentity = 1, replacement = 0, seed = 0;
bool ready = false, nativeBloodProvider = false;
bool ForeignController(Duke3d::DDukeActor* actor)
{
	return actor && actor->gameplayEntity && actor->gameplayEntity->actor == actor && actor->gameplayEntity->componentTick;
}
class EnemyConServices final : public Duke3d::ConExecutionServices
{
public:
	bool AllowCommand(int opcode, const int*) const override
	{
		using namespace Duke3d;
		switch (opcode)
		{
		case concmd_fall: case concmd_ifsquished: case concmd_ifaction:
		case concmd_ai: case concmd_ifai: case concmd_ifdead: case concmd_ifhitweapon:
		case concmd_action: case concmd_move: case concmd_ifmove: case concmd_ifp:
		case concmd_nullop: case concmd_ifcount: case concmd_count: case concmd_resetcount:
		case concmd_ifactioncount: case concmd_resetactioncount: case concmd_ifpdistl: case concmd_ifpdistg:
		case concmd_ifcansee: case concmd_ifcanshoottarget: case concmd_ifcanseetarget:
		case concmd_ifgapzl: case concmd_iffloordistl: case concmd_ifceilingdistl:
		case concmd_ifbulletnear: case concmd_ifnotmoving: case concmd_ifactornotstayput:
		case concmd_ifrnd: case concmd_state: case concmd_ends: case concmd_enda:
		case concmd_leftbrace: case concmd_rightbrace: case concmd_else: case concmd_break:
		case concmd_ifactor: case concmd_ifspawnedby: case concmd_ifwasweapon:
		case concmd_sound: case concmd_soundonce: case concmd_stopsound: case concmd_globalsound:
		case concmd_shoot: case concmd_spawn: case concmd_guts:
		case concmd_strength: case concmd_addstrength: case concmd_cstat: case concmd_addkills:
		case concmd_sleeptime: case concmd_killit: case concmd_ifrespawn:
		case concmd_sizeat: case concmd_sizeto: case concmd_spritepal: case concmd_getlastpal:
			return true;
		default: return false;
		}
	}
	void UnsupportedCommand(Duke3d::DDukeActor* actor, int opcode) override
	{
		actor->supersetConBlocked = true;
		Printf("Superset: entity %d needs an environment adapter for CON command %d.\n", actor->gameplayEntity->identity, opcode);
	}
} enemyConServices;
void MarkLevel()
{
	for (auto& entity : entities) GC::Mark(entity);
	for (auto& binding : bindings) GC::Mark(binding.node);
}
int Bind(int kind, sectortype* sector, DCoreActor* node = nullptr)
{
	Binding binding; binding.kind = kind; binding.sector = sector ? ::sector.IndexOf(sector) : -1; binding.node = node;
	GC::WriteBarrier(node);
	bindings.Push(binding);
	return int(bindings.Size()) - 1;
}
bool Activate(int index, DCoreActor* source, int command)
{
	if (unsigned(index) >= bindings.Size()) return false;
	auto& binding = bindings[index];
	if (binding.kind == BloodSprite)
	{
		if (auto node = dyn_cast<Blood::DBloodActor>(binding.node.Get())) Blood::trTriggerSprite(node, command, BloodState(source));
	}
	else if (unsigned(binding.sector) < ::sector.Size())
	{
		auto sector = &::sector[binding.sector];
		if (binding.kind == BloodSector) Blood::trTriggerSector(sector, command, BloodState(source));
		else
		{
			auto activator = dyn_cast<Duke3d::DDukeActor>(source);
			if (!activator)
				if (auto player = dyn_cast<Blood::DBloodPlayer>(PlayerArray[myconnectindex])) activator = PrepareBloodMapDukePlayer(player)->GetActor();
			Duke3d::operatesectors(sector, activator);
		}
	}
	return true;
}
Archetype NativeArchetype(DCoreActor* actor)
{
	if (auto blood = dyn_cast<Blood::DBloodActor>(actor))
	{
		if (blood->spr.statnum == Blood::kStatDude && blood->spr.type == Blood::kDudeZombieAxeNormal && blood->hasX() && blood->xspr.health) return Archetype::BloodAxeZombie;
		if (blood->spr.statnum == Blood::kStatItem && blood->spr.type == Blood::kItemWeaponSawedoff) return Archetype::BloodShotgun;
	}
	if (auto duke = dyn_cast<Duke3d::DDukeActor>(actor))
	{
		if (duke->GetClass() == PClass::FindActor("DukePigCop") && duke->spr.extra > 0 && duke->spr.scale.X) return Archetype::DukePigcop;
		if (duke->GetClass() == PClass::FindActor("DukeShotgunSprite") && duke->spr.scale.X) return Archetype::DukeShotgun;
	}
	return Archetype::None;
}
Archetype Opposite(Archetype type)
{
	switch (type)
	{
	case Archetype::BloodAxeZombie: return Archetype::DukePigcop;
	case Archetype::DukePigcop: return Archetype::BloodAxeZombie;
	case Archetype::BloodShotgun: return Archetype::DukeShotgun;
	case Archetype::DukeShotgun: return Archetype::BloodShotgun;
	default: return type;
	}
}
bool Enemy(Archetype type) { return type == Archetype::BloodAxeZombie || type == Archetype::DukePigcop; }
bool BloodImplementation(Archetype type) { return type == Archetype::BloodAxeZombie || type == Archetype::BloodShotgun; }
uint32_t SpawnChoice(int sourceIndex)
{
	uint32_t value = uint32_t(seed) ^ (uint32_t(sourceIndex) * 0x9e3779b9u);
	value ^= value >> 16; value *= 0x85ebca6bu; value ^= value >> 13; value *= 0xc2b2ae35u;
	return value ^ (value >> 16);
}
void Attach(DGameplayEntity* entity, DCoreActor* actor)
{
	entity->actor = actor; actor->gameplayEntity = entity;
	GC::WriteBarrier(entity, actor); GC::WriteBarrier(actor, entity);
}
void Keep(DGameplayEntity* entity)
{
	TObjPtr<DGameplayEntity*> reference {}; reference = entity;
	entities.Push(reference); GC::WriteBarrier(entity);
}
DCoreActor* Construct(DGameplayEntity* entity, Archetype type, sectortype* sector, const DVector3& feet, DAngle angle)
{
	DCoreActor* result = nullptr;
	if (type == Archetype::BloodShotgun) result = Blood::actSpawnPickup(sector, feet, Blood::kItemWeaponSawedoff);
	else if (type == Archetype::BloodAxeZombie)
	{
		auto source = BloodState(PlayerArray[myconnectindex]->GetActor());
		if (!source) return nullptr;
		auto blood = Blood::actSpawnDude(source, Blood::kDudeZombieAxeNormal, -1);
		if (!blood) return nullptr;
		ChangeActorSect(blood, sector);
		blood->spr.pos = feet;
		double top, bottom; Blood::GetActorExtents(blood, &top, &bottom);
		blood->spr.pos.Z += feet.Z - bottom;
		result = blood;
	}
	else if (type == Archetype::DukeShotgun && Duke3d::HasSecondaryDukeContext())
		result = SpawnBloodMapDukePickup(PlayerArray[myconnectindex]->GetActor(), PClass::FindActor("DukeShotgunSprite"), feet);
	else
	{
		const char* name = type == Archetype::DukePigcop ? "DukePigCop" : "DukeShotgunSprite";
		auto duke = Duke3d::CreateActor(sector, feet, PClass::FindActor(name), 0, DVector2(0,0), angle, 0, 0, nullptr, STAT_MISC);
		if (!duke) return nullptr;
		Attach(entity, duke);
		duke->mapSpawned = entity->sourceIndex >= 0;
		if (Duke3d::HasSecondaryDukeContext())
			duke->spr.setspritetexture(Mashup::DukeTexture(type == Archetype::DukePigcop ? "PIGCOP" : "SHOTGUNSPRITE"));
		Duke3d::spawninit(nullptr, duke, nullptr);
		result = duke;
	}
	if (result)
	{
		Attach(entity, result);
		result->spr.Angles.Yaw = angle;
		result->backuploc();
	}
	return result;
}
struct SpawnDescription
{
	DCoreActor* source;
	Archetype type;
	DVector3 feet;
	DAngle angle;
	int index;
};
SpawnDescription Decode(DCoreActor* source, Archetype type)
{
	auto feet = source->spr.pos;
	if (auto blood = dyn_cast<Blood::DBloodActor>(source); blood && Enemy(type))
	{
		double top, bottom; Blood::GetActorExtents(blood, &top, &bottom); feet.Z = bottom;
	}
	return {source, type, feet, source->spr.Angles.Yaw, source->GetIndex()};
}
bool Eligible(const SpawnDescription& description)
{
	if (auto blood = dyn_cast<Blood::DBloodActor>(description.source))
		return !Blood::gModernMap && Blood::actGetRespawnTime(blood) < 0 &&
			(!blood->hasX() || (!blood->xspr.rxID && !blood->xspr.key && !blood->xspr.dropMsg &&
				!blood->xspr.dudeDeaf && !blood->xspr.dudeGuard && !blood->xspr.dudeAmbush && !blood->xspr.dudeFlag4));
	return !Duke3d::ud.respawn_monsters && !Duke3d::ud.respawn_items;
}
bool Import(const SpawnDescription& description)
{
	auto entity = Create<DGameplayEntity>();
	entity->identity = nextIdentity++;
	entity->sourceIndex = description.index;
	entity->sourceNode = description.source;
	GC::WriteBarrier(entity, description.source);
	entity->enemy = Enemy(description.type);
	Archetype chosen = description.type;
	if (replacement == 1 || (replacement == 2 && (SpawnChoice(description.index) & 1))) chosen = Opposite(chosen);
	entity->archetype = int(chosen);
	Keep(entity);
	entity->componentTick = BloodImplementation(chosen) != nativeBloodProvider;
	if (auto blood = dyn_cast<Blood::DBloodActor>(description.source); blood && blood->hasX())
	{
		if (entity->enemy) entity->deathBinding = Bind(BloodSprite, nullptr, blood);
		else if (blood->xspr.Pickup) entity->pickupBinding = Bind(BloodSprite, nullptr, blood);
	}
	auto actor = chosen == description.type ? description.source : Construct(entity, chosen, description.source->sector(), description.feet, description.angle);
	if (!actor) { entities.Pop(); entity->Destroy(); return false; }
	Attach(entity, actor);
	if (actor != description.source)
	{
		// Preserve the native event node and references, with no gameplay body,
		// controller, health authority or transform reconciliation attached to it.
		description.source->gameplayEntity = entity;
		GC::WriteBarrier(description.source, entity);
		ChangeActorStat(description.source, MAXSTATUS - 4);
		description.source->spr.cstat = CSTAT_SPRITE_INVISIBLE;
		if (auto blood = dyn_cast<Blood::DBloodActor>(description.source)) { Blood::seqKill(blood); blood->xspr.health = 0; }
	}
	return true;
}
}

void InitRuntimeLevel() { GC::AddMarkerFunc(MarkLevel); }
void ClearRuntimeLevel() { entities.Clear(); bindings.Clear(); properties.Clear(); nextIdentity = 1; ready = false; }

void ImportRuntimeLevel(bool bloodSource)
{
	ClearRuntimeLevel();
	nativeBloodProvider = bloodSource;
	replacement = clamp(int(superset_replacement), 0, 2); seed = superset_replacement_seed;
	for (auto& sector : ::sector)
	{
		SectorProperties property; property.previousFloor = sector.floorz; property.bloodMediumBinding = bloodSource;
		if (bloodSource)
		{
			if (sector.hasX()) Bind(BloodSector, &sector);
		}
		else
		{
			property.medium = sector.lotag == ST_2_UNDERWATER ? 2 : sector.lotag == ST_1_ABOVE_WATER ? 1 : 0;
			if (Duke3d::isanearoperator(sector.lotag) || Duke3d::isanunderoperator(sector.lotag)) Bind(DukeSector, &sector);
		}
		properties.Push(property);
	}
	ready = true;
	TArray<SpawnDescription> descriptions;
	TSpriteIterator<DCoreActor> it;
	while (auto actor = it.Next())
	{
		auto type = NativeArchetype(actor);
		if (type == Archetype::None) continue;
		auto description = Decode(actor, type);
		if (Eligible(description)) descriptions.Push(description);
	}
	int imported = 0;
	for (auto& description : descriptions) imported += Import(description);
	Printf("Superset level import: %d shared entities, %u mechanism bindings; replacement=%d seed=%d.\n", imported, bindings.Size(), replacement, seed);
}

DCoreActor* ImportRuntimeObject(DCoreActor* source)
{
	if (!ready || !source) return nullptr;
	if (source->gameplayEntity) return source->gameplayEntity->actor;
	auto type = NativeArchetype(source);
	if (type == Archetype::None) return nullptr;
	auto description = Decode(source, type);
	return Eligible(description) && Import(description) ? source->gameplayEntity->actor.Get() : nullptr;
}

Environment LevelEnvironment(sectortype* sector)
{
	Environment result;
	if (!sector) return result;
	result.floorZ = getflorzofslopeptr(sector, sector->walls[0].pos);
	result.ceilingZ = getceilzofslopeptr(sector, sector->walls[0].pos);
	int index = ::sector.IndexOf(sector);
	if (!ready || unsigned(index) >= properties.Size()) return result;
	result.imported = true;
	auto& property = properties[index];
	result.medium = Medium(property.medium);
	if (property.bloodMediumBinding)
	{
		auto upper = dyn_cast<Blood::DBloodActor>(sector->upperLink.Get());
		result.medium = sector->hasX() && sector->xs().Underwater ? Medium::Underwater :
			upper && upper->spr.type == Blood::kMarkerUpWater ? Medium::WaterSurface : Medium::Air;
	}
	result.floorVelocity = sector->floorz - property.previousFloor;
	return result;
}

bool ActivateLevelSector(sectortype* sector, DCoreActor* activator, bool use)
{
	if (!IsActive() || !ready || !sector) return false;
	int index = ::sector.IndexOf(sector);
	for (unsigned n = 0; n < bindings.Size(); ++n)
	{
		auto& binding = bindings[n];
		if (binding.sector != index || binding.kind == BloodSprite) continue;
		if (use)
		{
			if (binding.kind == DukeSector && (sector->lotag & 16384)) return true;
			if (binding.kind == BloodSector && sector->hasX())
			{
				auto player = BloodState(activator);
				int key = sector->xs().Key;
				if (sector->xs().locked || (key && (!player || !player->IsPlayerActor() || !Blood::getPlayer(player->spr.type - Blood::kDudePlayer1)->hasKey[key]))) return true;
			}
		}
		return Activate(int(n), activator, use ? Blood::kCmdSpritePush : Blood::kCmdToggle);
	}
	return false;
}

bool IsRuntimeActor(DCoreActor* actor)
{
	return actor && actor->gameplayEntity && actor->gameplayEntity->actor == actor;
}

bool EntityDied(DCoreActor* actor, DCoreActor* killer)
{
	if (!IsRuntimeActor(actor) || !actor->gameplayEntity->enemy) return false;
	auto entity = actor->gameplayEntity.Get();
	if (!killer) killer = entity->damageSource;
	killer = ResolveGameplayActor(killer);
	if (!entity->deathEmitted)
	{
		entity->deathEmitted = true;
		Activate(entity->deathBinding, killer, Blood::kCmdOff);
	}
	if (!entity->killCredited)
	{
		entity->killCredited = true;
		Level.addKill(killer == PlayerArray[myconnectindex]->GetActor() || killer == BloodState(PlayerArray[myconnectindex]->GetActor()) ? myconnectindex : -1);
	}
	return true;
}

bool RequestDukeDeath(Duke3d::DDukeActor* actor)
{
	if (!IsRuntimeActor(actor) || !actor->gameplayEntity->enemy) return false;
	actor->gameplayEntity->deathRequested = true;
	return true;
}
void FinishDukeLifecycle(Duke3d::DDukeActor* actor)
{
	if (!IsRuntimeActor(actor) || !actor->gameplayEntity->deathRequested) return;
	// Stock CON calls addkills before entering the frozen action. A frozen
	// zero-health actor can thaw; emit the bound death only on a lethal outcome.
	if ((actor->flags2 & SFLAG2_DIENOW) || (actor->spr.extra < 0 && actor->spr.pal != 1))
	{
		EntityDied(actor, actor->GetHitOwner());
		actor->spr.cstat2 &= ~CSTAT2_SPRITE_COUNTKILL;
		actor->gameplayEntity->deathRequested = false;
	}
	else if (actor->spr.pal != 1) actor->gameplayEntity->deathRequested = false;
}

DCoreActor* ResolveGameplayActor(DCoreActor* actor)
{
	if (!actor) return nullptr;
	if (actor->gameplayEntity) return actor->gameplayEntity->actor;
	if (auto blood = dyn_cast<Blood::DBloodActor>(actor); blood && blood->supersetWorldActor) return blood->supersetWorldActor;
	if (auto duke = dyn_cast<Duke3d::DDukeActor>(actor); duke && IsDukeWeaponActor(duke)) return duke->supersetAttackSource;
	return actor;
}

bool EntityPickedUp(DCoreActor* actor, DCoreActor* collector)
{
	if (!IsRuntimeActor(actor) || actor->gameplayEntity->enemy) return false;
	auto entity = actor->gameplayEntity.Get();
	if (!entity->pickupEmitted)
	{
		entity->pickupEmitted = true;
		Activate(entity->pickupBinding, collector, Blood::kCmdSpritePickup);
	}
	return true;
}

bool RuntimeTarget(DCoreActor* actor, TargetInfo& result)
{
	if (!IsRuntimeActor(actor)) return false;
	return GameplayTarget(actor, result);
}
bool GameplayTarget(DCoreActor* actor, TargetInfo& result)
{
	if (!IsActive() || !actor || (actor->ObjectFlags & OF_EuthanizeMe) || ResolveGameplayActor(actor) != actor) return false;
	result = {};
	result.actor = actor;
	if (IsRuntimeActor(actor)) { result.identity = actor->gameplayEntity->identity; result.enemy = actor->gameplayEntity->enemy; }
	if (auto blood = dyn_cast<Blood::DBloodActor>(actor))
	{
		if (!IsRuntimeActor(actor)) result.enemy = blood->IsDudeActor() && !blood->IsPlayerActor() && blood->spr.statnum == Blood::kStatDude;
		result.health = blood->hasX() ? blood->xspr.health : 0;
		double top, bottom; Blood::GetActorExtents(blood, &top, &bottom);
		result.height = bottom - top;
		result.aim = blood->spr.pos;
		if (blood->IsDudeActor()) result.aim.Z -= blood->spr.scale.Y * Blood::getDudeInfo(blood->spr.type)->aimHeight;
	}
	else if (auto duke = dyn_cast<Duke3d::DDukeActor>(actor))
	{
		if (!IsRuntimeActor(actor)) result.enemy = Duke3d::badguy(duke) && !duke->isPlayer();
		result.health = max<Units::Health>(0, Units::FromDukeHealth(duke->spr.extra) - duke->supersetDamageRemainder);
		result.height = TexMan.GetGameTexture(duke->spr.spritetexture())->GetDisplayHeight() * duke->spr.scale.Y;
		result.aim = duke->spr.pos.plusZ(-result.height * .5);
	}
	else return false;
	result.radius = actor->clipdist;
	result.alive = result.enemy && result.health > 0 && (!IsRuntimeActor(actor) || !actor->gameplayEntity->deathEmitted);
	return true;
}
void ForEachGameplayTarget(const std::function<void(const TargetInfo&)>& visitor)
{
	TSpriteIterator<DCoreActor> it;
	while (auto actor = it.Next()) { TargetInfo target; if (GameplayTarget(actor, target) && target.enemy) visitor(target); }
}
void ForEachRuntimeTarget(const std::function<void(const TargetInfo&)>& visitor)
{
	for (auto& entity : entities)
		if (auto actor = entity->actor.Get()) { TargetInfo target; if (RuntimeTarget(actor, target) && target.enemy) visitor(target); }
}

DCoreActor* SpawnRuntimeEntity(Archetype type, sectortype* sector, const DVector3& feet, DAngle angle)
{
	if (!ready || type == Archetype::None || !sector) return nullptr;
	auto entity = Create<DGameplayEntity>();
	entity->identity = nextIdentity++; entity->archetype = int(type); entity->enemy = Enemy(type);
	entity->componentTick = BloodImplementation(type) != nativeBloodProvider;
	Keep(entity);
	auto actor = Construct(entity, type, sector, feet, angle);
	if (!actor) { entities.Pop(); entity->Destroy(); return nullptr; }
	entity->sourceNode = actor; GC::WriteBarrier(entity, actor);
	if (entity->enemy) Level.addKillCount();
	return actor;
}

void TickRuntimeLevel()
{
	if (!ready) return;
	bool dukeControllers = false;
	for (auto& entity : entities)
		if (entity->componentTick && entity->enemy && dyn_cast<Duke3d::DDukeActor>(entity->actor.Get())) { dukeControllers = true; break; }
	if (dukeControllers)
	{
		PrepareBloodMapDukePlayer(Blood::getPlayer(myconnectindex));
		Duke3d::movefta(); // Native distance/visibility wakeup, once for this component phase.
	}
	for (auto& entity : entities)
	{
		auto actor = dyn_cast<Duke3d::DDukeActor>(entity->actor.Get());
		if (!actor || !entity->componentTick || !entity->enemy || actor->supersetConBlocked) continue;
		actor->backuploc();
		if (!entity->deathEmitted && actor->spr.statnum == STAT_ACTOR)
			if (auto node = dyn_cast<Blood::DBloodActor>(entity->sourceNode.Get()); node && node->hasX())
				// Blood aiActivateDude sets this event state without sending TX-on.
				// Retain that lifecycle transition on the source node, not another AI.
				node->xspr.state = 1;
		Duke3d::TickActor(actor);
		if (actor->flags2 & SFLAG2_DIENOW) actor->Destroy();
	}
	for (unsigned n = 0; n < properties.Size(); ++n) properties[n].previousFloor = ::sector[n].floorz;
}

Duke3d::ConExecutionServices* RuntimeConServices(Duke3d::DDukeActor* actor)
{
	return ForeignController(actor) ? &enemyConServices : nullptr;
}
bool RuntimeDukeEnvironment(Duke3d::DDukeActor* actor, int& medium)
{
	if (!ForeignController(actor) && !IsRuntimeDukeEffect(actor)) return false;
	medium = int(LevelEnvironment(actor->sector()).medium);
	CollisionBase ceiling, floor;
	auto flags = actor->spr.cstat2;
	actor->spr.cstat2 |= CSTAT2_SPRITE_NOFIND;
	getzrange(actor->spr.pos.plusZ(-1), actor->sector(), &actor->ceilingz, ceiling, &actor->floorz, floor, 7.9375, CLIPMASK0);
	actor->spr.cstat2 = flags;
	return true;
}
int ActorMedium(Duke3d::DDukeActor* actor, sectortype* sector)
{
	return ForeignController(actor) || IsRuntimeDukeEffect(actor) ? int(LevelEnvironment(sector).medium) : sector->lotag;
}
bool IsRuntimeDukeEffect(Duke3d::DDukeActor* actor)
{
	if (!actor || !actor->supersetForeignFx) return false;
	return actor->IsKindOf(PClass::FindActor("DukeJibs1")) || actor->IsKindOf(PClass::FindActor("DukeBloodPool")) ||
		actor->GetClass() == PClass::FindActor("DukeBlood");
}
bool SpawnRuntimeDukeObject(Duke3d::DDukeActor* source, PClassActor* type, Duke3d::DDukeActor*& result)
{
	if (!ForeignController(source) && !IsRuntimeDukeEffect(source)) return false;
	result = nullptr;
	if (type == PClass::FindActor("DukeShotgunSprite"))
	{
		result = static_cast<Duke3d::DDukeActor*>(SpawnRuntimeEntity(Archetype::DukeShotgun, source->sector(), source->spr.pos, source->spr.Angles.Yaw));
		if (result)
		{
			result->attackertype = static_cast<PClassActor*>(source->GetClass());
			result->spr.pos = source->spr.pos;
			Duke3d::CallInitialize(result, source);
			ChangeActorStat(result, MAXSTATUS - 3);
		}
		return true;
	}
	if (!type || (!type->IsDescendantOf(PClass::FindActor("DukeJibs1")) && !type->IsDescendantOf(PClass::FindActor("DukeBloodPool")) &&
		type != PClass::FindActor("DukeBlood") && !type->IsDescendantOf(PClass::FindActor("DukeBloodSplat1"))))
	{
		Printf("Superset: native Duke spawn %s needs a component adapter.\n", type ? type->TypeName.GetChars() : "unbound class");
		return true;
	}
	result = Duke3d::CreateActor(source->sector(), source->spr.pos, type, 0, DVector2(0,0), source->spr.Angles.Yaw, 0, 0, source, STAT_MISC);
	if (result)
	{
		result->supersetForeignFx = true;
		Duke3d::CallInitialize(result, source);
	}
	return true;
}

bool AnimateRuntimeEntity(tspritetype* sprite, const DVector2& view, double fraction)
{
	auto actor = sprite->ownerActor;
	if (!IsRuntimeActor(actor) || !actor->gameplayEntity->componentTick) return false;
	if (auto blood = dyn_cast<Blood::DBloodActor>(actor))
	{
		if (cl_interpolate) { sprite->pos = actor->interpolatedpos(fraction); sprite->Angles.Yaw = actor->interpolatedyaw(fraction); }
		Blood::viewApplySpriteRotation(sprite, blood, view);
		return true;
	}
	return false;
}

namespace
{
FSerializer& Serialize(FSerializer& arc, const char* key, Binding& binding, Binding*)
{
	if (arc.BeginObject(key)) arc("kind", binding.kind)("sector", binding.sector)("node", binding.node).EndObject();
	return arc;
}
FSerializer& Serialize(FSerializer& arc, const char* key, SectorProperties& property, SectorProperties*)
{
	if (arc.BeginObject(key)) arc("medium", property.medium)("blood_medium_binding", property.bloodMediumBinding)("previous_floor", property.previousFloor).EndObject();
	return arc;
}
}
void SerializeRuntimeLevel(FSerializer& arc)
{
	if (arc.BeginObject("superset.level"))
	{
		arc("entities", entities)("bindings", bindings)("properties", properties)("next_identity", nextIdentity)
			("replacement", replacement)("seed", seed)("ready", ready)("native_blood_provider", nativeBloodProvider).EndObject();
	}
}

CCMD(superset_spawn)
{
	if (!IsActive() || !ready || gamestate != GS_LEVEL || argv.argc() < 2) return;
	Archetype type = Archetype::None;
	if (!stricmp(argv[1], "blood.zombie")) type = Archetype::BloodAxeZombie;
	else if (!stricmp(argv[1], "duke.pigcop")) type = Archetype::DukePigcop;
	else if (!stricmp(argv[1], "blood.shotgun")) type = Archetype::BloodShotgun;
	else if (!stricmp(argv[1], "duke.shotgun")) type = Archetype::DukeShotgun;
	if (type == Archetype::None) { Printf("Superset archetypes: blood.zombie duke.pigcop blood.shotgun duke.shotgun\n"); return; }
	auto source = PlayerArray[myconnectindex]->GetActor();
	bool here = !Enemy(type) && argv.argc() == 3 && !stricmp(argv[2], "here");
	auto pos = source->spr.pos + source->spr.Angles.Yaw.ToVector() * (here ? 0 : Enemy(type) ? 96 : 64);
	auto sector = source->sector(); updatesector(pos, &sector);
	if (!sector) return;
	pos.Z = getflorzofslopeptr(sector, pos);
	auto actor = SpawnRuntimeEntity(type, sector, pos, source->spr.Angles.Yaw + DAngle180);
	Printf("Superset spawn %s: %s (identity=%d).\n", argv[1], actor ? "PASS" : "FAIL", actor ? actor->gameplayEntity->identity : 0);
}

CCMD(superset_level_check)
{
	if (!IsActive() || !ready || gamestate != GS_LEVEL) return;
	if (argv.argc() >= 2 && (!stricmp(argv[1], "door") || !stricmp(argv[1], "fixture")))
	{
		auto player = PlayerArray[myconnectindex]->GetActor();
		sectortype* door = nullptr;
		for (auto& candidate : ::sector)
		{
			if (nativeBloodProvider ? candidate.hasX() && candidate.type == Blood::kSectorZMotion &&
				!candidate.xs().locked && !candidate.xs().Key && candidate.xs().rxID &&
				(candidate.xs().onCeilZ != candidate.xs().offCeilZ || candidate.xs().onFloorZ != candidate.xs().offFloorZ) :
				(candidate.lotag & 0x7fff) == ST_20_CEILING_DOOR && !(candidate.lotag & 16384)) { door = &candidate; break; }
		}
		if (!door) { Printf("Superset mechanism fixture: FAIL (no supported native door).\n"); return; }
		if (!stricmp(argv[1], "door"))
		{
			bool accepted = ActivateLevelSector(door, player, false);
			Printf("Superset mechanism use: %s (sector=%d floor=%g ceiling=%g).\n", accepted ? "PASS" : "FAIL", ::sector.IndexOf(door), door->floorz, door->ceilingz);
			return;
		}
		bool pickup = argv.argc() > 2 && !stricmp(argv[2], "pickup");
		auto position = player->spr.pos + player->spr.Angles.Yaw.ToVector() * (pickup ? 64 : 96);
		auto destination = player->sector(); updatesector(position, &destination);
		if (!destination) return;
		position.Z = getflorzofslopeptr(destination, position);
		DCoreActor* source = nullptr;
		if (nativeBloodProvider)
		{
			if (pickup) source = Blood::actSpawnPickup(destination, position, Blood::kItemWeaponSawedoff);
			else
			{
				auto blood = Blood::actSpawnDude(BloodState(player), Blood::kDudeZombieAxeNormal, -1);
				ChangeActorSect(blood, destination); blood->spr.pos = position;
				double top, bottom; Blood::GetActorExtents(blood, &top, &bottom); blood->spr.pos.Z += position.Z - bottom;
				Level.addKillCount(); source = blood;
			}
			auto blood = static_cast<Blood::DBloodActor*>(source); blood->addX();
			blood->xspr.txID = door->xs().rxID; blood->xspr.command = Blood::kCmdToggle;
			blood->xspr.state = 0; blood->xspr.triggerOff = !pickup;
			blood->xspr.triggerOn = pickup; blood->xspr.Pickup = pickup; blood->xspr.triggerOnce = true;
		}
		else
		{
			auto duke = Duke3d::CreateActor(destination, position, PClass::FindActor(pickup ? "DukeShotgunSprite" : "DukePigCop"),
				0, DVector2(0,0), player->spr.Angles.Yaw + DAngle180, 0, 0, nullptr, STAT_MISC);
			int monsters = Duke3d::ud.monsters_off; Duke3d::ud.monsters_off = 0;
			Duke3d::spawninit(nullptr, duke, nullptr); Duke3d::ud.monsters_off = monsters; source = duke;
		}
		auto actor = ImportRuntimeObject(source);
		Printf("Superset source fixture: %s (identity=%d source=%s chosen=%d event-sector=%d).\n", actor ? "PASS" : "FAIL",
			actor ? actor->gameplayEntity->identity : 0, pickup ? "pickup" : "enemy", actor ? actor->gameplayEntity->archetype : 0, ::sector.IndexOf(door));
		return;
	}
	if (argv.argc() == 2 && !stricmp(argv[1], "collect"))
	{
		for (int n = int(entities.Size()) - 1; n >= 0; --n)
			if (auto actor = entities[n]->actor.Get(); actor && !entities[n]->enemy && !entities[n]->pickupEmitted)
			{
				auto player = PlayerArray[myconnectindex]->GetActor();
				ChangeActorSect(player, actor->sector()); player->spr.pos.SetXY(actor->spr.pos.XY()); player->backuploc();
				double feetOffset = 0;
				if (auto blood = dyn_cast<Blood::DBloodActor>(player))
				{
					double top, bottom; Blood::GetActorExtents(blood, &top, &bottom); feetOffset = bottom - blood->spr.pos.Z;
				}
				player->spr.pos.Z = getflorzofslopeptr(actor->sector(), actor->spr.pos) - feetOffset;
				player->vel.Zero(); player->backuploc();
				if (auto blood = dyn_cast<Blood::DBloodPlayer>(PlayerArray[myconnectindex])) Blood::playerResetInertia(blood);
				return; // Collection remains the normal native eligibility/CON path.
			}
	}
	if (argv.argc() == 2 && !stricmp(argv[1], "kill"))
	{
		for (int n = int(entities.Size()) - 1; n >= 0; --n)
		{
			auto entity = entities[n];
			auto actor = entity->actor.Get(); TargetInfo target;
			if (actor && RuntimeTarget(actor, target) && target.alive)
			{
				// Exercise the normal receiver, including Blood's native resistance.
				bool accepted = ApplyHealthDamage(actor, {min(Units::FromDukeHealth(INT16_MAX), target.health * 16 + 16), DamageKind::Physical}, PlayerArray[myconnectindex]->GetActor());
				Printf("Superset entity health request: %s (identity=%d).\n", accepted ? "PASS" : "FAIL", entity->identity);
				return;
			}
		}
	}
	unsigned alive = 0, dead = 0, taken = 0; bool valid = true;
	for (auto& entity : entities)
	{
		if (entity->enemy && entity->deathEmitted) ++dead;
		if (entity->pickupEmitted) ++taken;
		auto actor = entity->actor.Get();
		if (!actor) continue;
		valid &= actor->gameplayEntity == entity;
		if (auto blood = dyn_cast<Blood::DBloodActor>(actor)) valid &= !blood->supersetWorldActor && !blood->supersetEnemyController;
		if (auto duke = dyn_cast<Duke3d::DDukeActor>(actor)) valid &= !duke->supersetBloodState;
		if (auto node = entity->sourceNode.Get(); node && node != actor) valid &= !(node->spr.cstat & CSTAT_SPRITE_BLOCK_ALL);
		TargetInfo target; if (RuntimeTarget(actor, target) && target.alive) ++alive;
	}
	Printf("Superset level: %s (entities=%u alive=%u dead=%u picked=%u bindings=%u).\n", valid ? "PASS" : "FAIL", entities.Size(), alive, dead, taken, bindings.Size());
}
}
