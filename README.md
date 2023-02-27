# SPUD: Steve's Persistent Unreal Data library

## What is it?

SPUD is a save game and streaming level persistence solution for Unreal Engine 5.

The 2 main core features:

1. Save / Load game state easily
1. Streamed levels retain their state as they unload / reload without a save needed


Some more details:

* Easily mark actors in your levels as persistent 
  * You implement ISpudObject, a marker interface with no required methods
* Pick properties to save
    * By enabling the "SaveGame" option
* All property types supported, including arrays, references between objects, and custom structs
* You can also manually mark non-level UObjects (e.g. GameInstance) for inclusion in the save
* Dynamically spawned objects that exist at save are re-spawned on load
* Level objects which have been destroyed are automatically re-destroyed on level load
* Core details like transform, controller rotation and physics state are automatically saved
* Usable in C++ or Blueprints

An introduction video:

[![Intro Video](http://img.youtube.com/vi/AzDoMGeJgi4/0.jpg)](http://www.youtube.com/watch?v=AzDoMGeJgi4 "Intro to SPUD")

## Examples

This project contains the master documentation for this library, but if you want
to see examples of its use, see the [SPUD Examples](https://github.com/sinbad/SPUDExamples) project.


## Installing

### Cloning

The best way is to clone this repository as a submodule; that way you can contribute
pull requests if you want. The project should be placed in your project's Plugins folder.

```
> cd YourProject
> git submodule add https://github.com/sinbad/SPUD Plugins/SPUD
> git add ../.gitmodules
> git commit
```

Alternatively you can download the ZIP of this repo and place it in 
`YourProject/Plugins/SPUD`.

### Referencing in C++

Edit YourProject.Build.cs and do something similar to this:

```csharp
using System.IO;
using UnrealBuildTool;

public class SPUDExamples : ModuleRules
{
	private string PluginsPath
	{
		get { return Path.GetFullPath( Path.Combine( ModuleDirectory, "../../Plugins/" ) ); }
	}
	
	protected void AddSPUD() {
		// Linker
		PrivateDependencyModuleNames.AddRange(new string[] { "SPUD" });
		// Headers
		PublicIncludePaths.Add(Path.Combine( PluginsPath, "SPUD", "Source", "SPUD", "Public"));
	}

	public SPUDExamples(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });
		
		AddSPUD();
	}
}
```

After adding this you should right-click your .uproject file and "Generate Visual
Studio Project Files". 

## Basic Usage


> ### VERY IMPORTANT
>
> You **MUST** save all your levels before playing in editor (PIE). Failure to
> do so results in mis-categorisation of some level objects. The Output Log will
> dump an error about this when unsaved levels are detected. 
>
> To fix this, either:
>
> 1. Use the menu option *File -> Save All Levels* before playing in the editor
> 2. In Project Settings > Plugins > SPUD, enable the option *Save All Levels On Play In Editor*.
>
> The latter option is your best bet for making sure you don't accidentally have
> strange bugs, at the expense of a slight delay to PIE if you have unsaved levels.
>
> Do not report any bugs unless you've checked your levels are saved!!

The core functionality of SPUD is writing the state of chosen UObjects, including 
a chosen subset of their properties, to a persistent data format so it can be 
saved / loaded and kept consistent across map changes and streaming. 

### Automatically Persisting Actors

Actors present in your world can be picked up automatically. To do this, 
you opt-in your classes by implementing the interface `ISpudObject`. You can
do this in C++:

```c++
class AMyActor : public AActor, public ISpudObject
{
...
```

or in Blueprints:

![Implementing ISpudObject in Blueprints](./doc/images/BPSpudObject.png)

You don't have to implement any methods on this interface, it is solely a marker
so that SPUD will know to look at your object. Any actor marked this way
will be automatically made persistent. This includes GameModes and GameStates.

### Explicitly Persisting Global Objects

Global objects like GameInstance won't be picked up for persistence even if
you implement `ISpudObject`, because they're not included in the world. However,
you can opt these objects in to persistence so they also get saved:

```c++
	GetSpudSubsystem(GetWorld())->AddPersistentGlobalObjectWithName(this, "MyGameInstance");	
```

Global objects must always exist, SPUD won't re-create them on load, but it will
re-populate their state.

### Standard Persistent State

Just by opting the class in to SPUD persistence, the following state is
automatically saved:

* Hidden flag
* Transform (Movable objects only)
* Controller Rotation (Pawns only)
* Physics velocities (Physics objects only)
* Any Movement Component's velocity (e.g. player movement, projectile movement, if present)

### Pick Properties to Save

In addition to the standard state, you can then tell SPUD to save additional properties of 
the object. You use the "SaveGame" `UPROPERTY` flag to do this.

In C++:

```c++
	UPROPERTY(SaveGame)
	int MySavedInt;
```

or Blueprints, in the advanced property details section:

![Opting into SaveGame in Blueprints](./doc/images/BPPropSaveGame.png)

For the most common case of an object in a level, that's it! 
Many types of property are supported. For more details, see [Properties](./doc/props.md);

### Destroyed Actors

If a level actor that implements `ISpudObject` is destroyed, that destruction will 
be made persistent by SPUD. Re-loading a map will automatically re-create that actor, 
but as part of the restore process SPUD will destroy it again, returning the
world to the correct state. You don't need to do anything extra to make this work.

### Runtime Spawned Actors

Actors which are not part of the level but are spawned at runtime, that also
implement `ISpudObject`, will be automatically re-spawned on load.

However, because these objects need to be uniquely identified, you must give
these classes a special FGuid property called `SpudGuid`.

For example:

```c++
	UPROPERTY()
	FGuid SpudGuid;
```

You don't have to assign a value to this property, SPUD will generate a GUID if
it's blank. Also you should **NOT mark it as SaveGame**. It's not your save state,
just some metadata SPUD needs to uniquely identify this object.

### Gameplay Framework Spawned Actors

Some actors are not stored in the level, and are spawned at runtime but *not*
explicitly during game code; they're spawned automatically by the gameplay 
framework during the initialisation of the level. Examples include:

* Pawns / Characters
* PlayerState
* GameState
* GameMode

If you have state in these objects, then you need to implement the `OverrideName`
method in `ISpudObject`, either in C++ or Blueprints, to give these instances
a unique, pre-defined name. 

For example, in C++:

```c++
FString AMyPlayerState::OverrideName_Implementation() const
{
	static const FString Name("PlayerState");
	return Name;
}

```

In this case we're assuming there's only one player, so only one player state.
If you had more than one player then they should each have a unique name.
This just makes sure that when restoring these objects, the automatically 
created instances are correctly associated with the previous state.

> The reason we don't use SpudGuid here is because you'd have to come up with a
> fixed unique GUID which is awkward. And really SpudGuid is for dealing with any
> number of runtime spawned objects, wheras in these cases there's always a known 
> number of them (often just one), they're more akin to level objects, just automatically
> constructed ones.



## Saving and Loading 

You can call any of the save / load methods on `USpudSubSystem`.

For example quick save/load In Blueprints:
![Save/Load in Blueprints](./doc/images/BPSpudSaveLoad.png)

Or in C++:

```c++
auto SpudSystem = GetSpudSubsystem(GetWorld());
SpudSystem->QuickSaveGame();
```

There are many other methods for saving to named slots, listing save games and so on.
When loading a game, the current map will *always* be unloaded, and the game will
travel to the map in the save game (even if it's the same one). This ensures things
are reset correctly before restoring state. For this reason, loading is
asynchronous (see events on USpudSubSystem if you want to listen in on when loading completes).

### A note on streaming 

When it comes to streaming, persistence of level data happens automatically so
long as streaming requests are routed through `USpudSubSystem`, which has
methods to request streamed levels, or to withdraw a request (levels are streamed
out when outstanding requests hit 0).

SPUD comes with a new streaming volume, `ASpudStreamingVolume` to make this
easier to use. But you can call the streaming methods manually as well.

When you travel between maps, SPUD gets notified and will save state to the
active game. 

More information is available in [Levels and Streaming](./doc/levelstreaming.md)


## More details

* [Frequently Asked Questions](./doc/faq.md)
* [Technical Details](./doc/tech.md)


## License

The MIT License (MIT)
Copyright © 2021 Steve Streeting

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the “Software”), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.