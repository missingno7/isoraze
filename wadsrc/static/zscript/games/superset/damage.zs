// Shared health entry points. Q4 amounts use 16 units per health point.
// They do not perform attack collision/acceptance checks or apply status effects.
extend class CoreActor
{
	enum ESharedDamageKind
	{
		SD_Impact, SD_Heat, SD_Physical, SD_Blast,
		SD_Asphyxiation, SD_Spirit, SD_Electric, SD_Cold
	};
	// False means unsupported input/receiver/source, not resistance or invulnerability.
	native bool ApplySharedHealthDamage(int kind, int q4Amount, CoreActor source = null);
	// Source is explicit: the caller owns attack attribution and contact policy.
	native bool ApplySharedDukeHealthHit(DukeActor hitter, CoreActor source = null);
	// Source is this actor. Velocity is world displacement per 30 Hz step.
	// Currently supports DukeFirelaser in Blood maps, otherwise returns null.
	native DukeActor SpawnSharedDukeProjectile(class<DukeActor> type, Vector3 position, Vector3 velocity);
	// Place a supported native Duke pickup in the current Blood environment.
	native DukeActor SpawnSharedDukePickup(class<DukeActor> type, Vector3 position);
}

extend class DukeActor
{
	native CoreActor, bool AimHitscan(readonly<DukeActor> weapon, double angle);
	native double HitscanAimOffset(CoreActor target);
	native bool TraceSharedHitscan(Vector3 position, Vector3 direction, HitInfo hit);
	native bool SharedHitscanContact(HitInfo hit, DukeActor spark);
	native int SharedProjectileMedium();
	native bool SharedProjectileContact(CollisionData coll);
	native void InheritProjectileEnvironment(DukeActor parent);
}
