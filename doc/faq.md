# FAQ

## What UE versions are supported?

Recommended: UE 4.26.1+

UE 4.25.4 also works, with some caveats (see below).

## It's not working in Play In Editor (PIE) mode!

First and foremost, **make sure you've saved all your levels**. If you're playing
in the editor (PIE) and your levels aren't saved, the data UE reports to SPUD isn't
reliable and can cause problems.

Either:

1. Always use "File > Save All Levels" before playing in the editor, or
2. Enable auto-saving of all levels before PIE in "Project Settings > Plugins > SPUD"

The Output Log will report an error about this if it detects you're running
with unsaved levels.

## It's not working in Standalone mode!

I have no idea why this happens, but it seems to be pre-4.26 only. Playing in Standalone mode from the editor randomly
resets some properties sometimes and I haven't been able to figure out why. It's
only playing Standalone from the editor:

* PIE mode works fine (if you save all levels, see above). 
* Packaged games work fine too

Having discussed this with others who have experienced other weird problems
specifically with Standalone, we've concluded that Standalone mode is just "bad". 
However in 4.26 everything seemed to start working in Standalone so YMMV.

To be safe, use PIE, or package the game.

## Screenshots aren't saved when I save a game!

This seems to be caused by a weird UE bug: if you have Widgets open in the Editor, screenshot
requests will always time out, resulting in a blank screenshot. Close all your Widget blueprints
in the editor to resolve this.

## Seamless travel doesn't save data in UE 4.25

Unfortunately the events needed to detect when seamless travel is starting were only
added in UE 4.26. If you want to use seamless travel in UE 4.25, you'll have to call `USpudSubsystem::PreLoadMap`
yourself just before travelling.

## Physics doesn't play out the same way every time I load!

Correct, they won't. Unreal Engine's physics are *non-deterministic*, which means you
won't necessarily get the exact same results every time, even if objects
positions and velocities are restored correctly.

This is not a SPUD bug, it's just the way UE's physics are. You'd get the same
result if you loaded your level and applied the same forces, each time things
would play out slightly differently.

## I have a runtime actor which is spawned automatically so SPUD shouldn't respawn it

If it's a subclass of Pawns, GameModes, GameStates, PlayerStates and Characters then
SPUD will automatically figure out it doesn't have to respawn them. For anything
else, implement the `GetSpudRespawnMode` method on `ISpudObject` to change
respawn behaviour.

## Player pawn falls through the level before it's streamed in!

On spawning or loading a game, assuming your player pawn implements ISpudObject
it will be moved to the correct location. However, level streaming can take a
little time so the character could fall through the level until that's done.

One way to fix this is to put some sort of collider in the Persistent Level (which
is always loaded) to catch things until the level is loaded, but that's only really
practical for simple worlds. 

A better way is to disable your character's movement and gravity until it can 
tell that the ground has appeared underneath it. This is what the example
character does.

Firstly, on BeginPlay, and on TeleportTo and post-restore (by implementing ISpudObjectCallback)
we call `BeginWaitingForStreaming`. This means whenever there's a chance the character
has moved quickly into a new area, we wait for the ground to appear:

```c++

void ASPUDExamplesCharacter::BeginWaitingForStreaming()
{
	// Just after we've been spawned, or just after teleport, the level around us might not be streamed in yet
	bIsWaitingForStreaming = true;
	if (UCharacterMovementComponent* MoveComp = Cast<UCharacterMovementComponent>(GetMovementComponent()))
	{
		MoveComp->GravityScale = 0;			
	}
}
```

We also override `IsMoveInputIgnored` to make sure the character isn't allowed to 
move.

```c++
bool ASPUDExamplesCharacter::IsMoveInputIgnored() const
{
	// Don't allow movement until streaming is ready
	return Super::IsMoveInputIgnored() || bIsWaitingForStreaming;
}
```

Then, on Tick we check whether things are OK yet, by doing a sphere trace downwards.
As soon as a static world is seen underneath us, we know it's ok to enable 
gravity and movement again.

```c++ 
void ASPUDExamplesCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bIsWaitingForStreaming)
		CheckStreamingOK();
}

void ASPUDExamplesCharacter::CheckStreamingOK()
{
	FHitResult OutHit;
	TArray<AActor*> ToIgnore;
	if (UKismetSystemLibrary::SphereTraceSingle(GetWorld(),
        GetActorLocation(), GetActorLocation() + FVector::DownVector * 200,
        30, UEngineTypes::ConvertToTraceType(ECC_WorldStatic),
        false, ToIgnore, EDrawDebugTrace::None, OutHit, true))
	{
		bIsWaitingForStreaming = false;
		if (UCharacterMovementComponent* MoveComp = Cast<UCharacterMovementComponent>(GetMovementComponent()))
		{
			MoveComp->GravityScale = 1;			
		}
		
	}
}
```

So that's one way to make your characters streaming / teleporting / loading friendly!
If your characters move differently or it's possible you could save them above
a large or even infinite drop, then you might need to tweak this a bit.

## Cross-references across streaming level boundaries

Persistent object cross-references are supported as saved state. However, 
they can only reference objects in the same level, or in the Persistent level, 
because you cannot guarantee which levels are loaded at once.

If objects needs to cross streaming level boundaries then you might consider
putting them in the Persistent Level instead of the streamed level. However, 
be careful about making them respond to gravity if there's a chance they can be
in an area that unloads.

