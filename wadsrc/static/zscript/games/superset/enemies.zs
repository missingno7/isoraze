// One map-facing pawn; native Blood controller state owns AI and physiology.
class SupersetBloodEnemy : DukeActor
{
	meta int SupersetFreezable;
	meta double SupersetColdFactor;
	property prefix: none;
	property SupersetFreezable: SupersetFreezable;
	property SupersetColdFactor: SupersetColdFactor;
	default
	{
		SupersetFreezable true;
		SupersetColdFactor 1;
		+INTERNAL_BADGUY;
		+DONTANIMATE;
		+NOFLOORPAL;
	}
	override void Tick() {}
	override void OnHit(DukeActor hitter) { SupersetBloodHit(hitter); }
}
