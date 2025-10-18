# Properties

## Marking Properties As Persistent

In C++:

```c++
	UPROPERTY(SaveGame)
	int MySavedInt;
```

Blueprints, in the advanced property details section:

![Opting into SaveGame in Blueprints](./images/BPPropSaveGame.png)

## Supported Property Types

The following property types are fully supported, either as single entries, and as
**arrays**.

* Integer (int)
* Integer64 (int64)
* Boolean (bool)
* Float (float)
* Byte (uint8)
* String (FString)
* Name (FName)
* Vector (FVector)
* Rotator (FRotator)
* Transform (FTransform)
* Guid (FGuid)
* Actor references, either as:
  * Strong references: `AActor*` and subclasses
  * Weak references: `TWeakObjectPtr<AActor>` and subclasses

The following property types are fully supported as individual items, but
have **caveats for arrays and maps**:

* Custom UStructs
* Nested UObject instances (null preserving, will re-instantiate based on property type)
* Nested components marked as SaveGame

~~All other property types - Maps, Sets, arrays of Custom UStructs or UObjects *are* supported 
**but** are not quite as functional. Due to their added complexity, they are serialized
as "opaque" records using UE's own internal serialisation methods. This means SPUD 
doesn't "look inside" them, it just saves/restores them as a blob, and cannot 
support SPUD's additional special processing such as retaining links between actors
in these properties. But for simple stand-alone state retention, they will work.~~

 Maps, Sets, arrays of UObjects are *not* supported anymore due to changes in recent UE 5.x
 archive class hierarchy. We used to fall back on UE's own serialisation but that is no longer possible. If you 
 need to use these structures then you will need to use custom data (see below).
 Arrays of custom UStructs should be ok though.

## Upgrading Properties

You might add or remove properties over time, and save games might have been taken on
previous versions. That's OK!

SPUD uses a fast restoration path when the property set for a class is the same
as the stored data, but when it's not, a slower path where properties are matched
up by name / type is used instead, so you can still restore data from old saves
even if a new save would have an altered set of properties.

## Components

Only properties marked at the top level of the UObject are persisted. Certain special
cases are handled, such as restoring physics velocities and controller rotation,
but for simplicity and efficiency we do not dig into all actor components.

For any components you wish to be persisted, you must:

1. Hold a reference to them in a root object `UPROPERTY`, marked as `SaveGame`
1. Instantiate those components at construction time

This will cause SPUD to cascade into that component just like any other nested
`UObject`, and any properties in the component which are marked to be saved will
processed in the same way.


### Using custom data

If for any reason you have some additional data to store for an object that isn't
supported by the above features, on top, you can implement the optional `ISpudObjectCallback`.
This interface gets the opportunity to save / load any custom data it wants, which
is stored with the rest of the object state. 


![Custom Data in Blueprints](./images/BPCustomData.png)

Custom Data isn't made upgrade-proof like properties, so be careful with this.
You have to read / write custom data the same way. But it allows you to essentially
store anything from anywhere if you can't make it work using a `UPROPERTY` on 
the root object.