// One map-facing pawn; native Blood controller state owns AI and physiology.
class SupersetBloodEnemy : DukeActor
{
	default
	{
		+INTERNAL_BADGUY;
		+DONTANIMATE;
		+NOFLOORPAL;
	}
	override void Tick() {}
	override void OnHit(DukeActor hitter) { SupersetBloodHit(hitter); }
}
