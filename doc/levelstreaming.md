# Levels and Streaming

In previous versions of SPDU, you needed to request streaming levels via `USpudSubSystem`,
for example using `ASpudStreamingVolume`. 

As of the latest version of SPUD, this is no longer required, but you can still
use it for the other benefits.

SPUD now also supports [World Partition](https://docs.unrealengine.com/5.1/en-US/world-partition-in-unreal-engine/). 


## Manually requesting streaming levels

```c++
	/// Make a request that a streaming level is loaded. Won't load if already loaded, but will
	/// record the request count so that unloading is done when all requests are withdrawn.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	void AddRequestForStreamingLevel(UObject* Requester, FName LevelName, bool BlockingLoad);
	/// Withdraw a request for a streaming level. Once all requesters have 
    // rescinded their requests, the streaming level will be considered ready to be unloaded.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	void WithdrawRequestForStreamingLevel(UObject* Requester, FName LevelName);
```

You can call these methods directly if you want to request levels explicitly.
Or, you can use our convenience class `ASpudStreamingVolume`.

## SPUD Streaming Volume

The `ASpudStreamingVolume` class is very similar to the standard streaming volume
class, with 3 important differences:

### 1. Requests are routed via `USpudSubSystem`

This allows state to be stored/loaded at the right time

### 2. SPUD volumes trigger for player pawns

The default UE streaming volumes only respond to cameras. The problem with this
is if you've got a 3rd person camera, you have to account for the way this camera
might be separate from the character you're controlling, and be careful to ensure
the camera can't accidentally "pop out" of the volume while the character is in it.
This results in some rather unintuitive padding that you have to do.

The SPUD streaming volumes do trigger on cameras, but also on *player-controlled pawns*.
This makes them much safer to use without having to pad things excessively.

### 3. You can associate levels directly with the volume

To associate a level with a UE streaming volume, you have to go to the level
window, click the *tiny* button to open level details, then associate the volume
with the level. This is unintuitive and fiddly.

The SPUD volumes make this much simpler. Just place the volume in the level, open
up its own details, and pick one or more levels to associate with it. That's it.
Associate the volume with levels in the same place you tweak its size and location.
Much easier!

## Placing streaming volumes

Make sure you're currently editing the **persistent level**, then under Place Actor,
search for "Spud Streaming Volume". Drag one into the scene.

Position the volume, change its size in Brush Settings, and associate one or
more streaming levels in the Level Streaming Volume section.

That's it! Now whenever a camera or a player controlled pawn enters that volume,
the level(s) will be requested to be loaded.

Download [the SPUD Examples project](https://github.com/sinbad/SPUDExamples) to see this in action.

> WIP