#pragma once

#include "CoreMinimal.h"
#include "SpudState.h"

#include "ISpudObject.generated.h"

UINTERFACE(MinimalAPI)
class USpudObject : public UInterface
{
	GENERATED_BODY()
};

UENUM(BlueprintType)
enum class ESpudRespawnMode : uint8
{
	Default UMETA(DisplayName="Default behaviour (based on class)"),
    AlwaysRespawn,
    NeverRespawn
};

/**
* Opts a class implementing this interface into persistence. If an object attached to the world implements this
* interface then it will be included in persistent game state. Any properties marked as SaveGame will be persisted.
*/
class SPUD_API ISpudObject
{
	GENERATED_BODY()

public:

	// Note: in order to support pure Blueprint overrides on behaviour customisation methods, all overrideable
	// methods MUST have default return state of the zero-filled data. No default implementations that return "true" for example
	// This is because a pure Blueprint override will not pick up the C++ implementation, because it's an interface, not a base class.
	
	/// Return whether this object should be respawned on load if it was detected as a runtime-created object
	/// The default is to respawn all runtime objects except for Pawns, GameModes, GameStates, PlayerStates and Characters which are assumed to be created automatically.
	/// You can override this if you want this for things like player pawns, game modes which are marked as runtime created, but
	/// are created automatically at level start so should not be created by the load process.
	/// Instead these objects will be identified by their names, much like level objects, and you should ensure that
	/// they always have the same names between save & load. 
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD Interface")
	ESpudRespawnMode GetSpudRespawnMode() const; virtual ESpudRespawnMode GetSpudRespawnMode_Implementation() const { return ESpudRespawnMode::Default; }

	/// Return whether this object should skip the restoration of its transform from the save data, if it's Movable.
	/// You can override this to true if you want this object to always retain its level location on restore.
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD Interface")
	bool ShouldSkipRestoreTransform() const; virtual bool ShouldSkipRestoreTransform_Implementation() const { return false; }

	/// Return whether this object should skip restoring its velocity from the save data. Only applies if it's Movable, has opted
	/// in to restoring transform, and has either physics sim enabled, or a movement component.
	/// You can override this to true if you want this object to manage its own velocity on load.
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD Interface")
	bool ShouldSkipRestoreVelocity() const; virtual bool ShouldSkipRestoreVelocity_Implementation() const { return false; }

	// Allows the object to override its name, as used for identifying itself in saved games.
	// The default is to use the object's native name. That is fine for level actors, but in built games (not the editor) actors that are
	// automatically spawned, such as the player's pawn, controller, and game state, get different names each time they are created.
	// Returning an empty string means to use the object's native name.
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD Interface")
	FString OverrideName() const; virtual FString OverrideName_Implementation() const { return FString(); }
};

UINTERFACE(MinimalAPI)
class USpudObjectCallback : public UInterface
{
	GENERATED_BODY()
};

/**
* Interface for fine control of persistence. Implement this in your objects to be notified when they are persisted or
* restored individually, and to include custom data in your stored records if you want.
*/
class SPUD_API ISpudObjectCallback
{
	GENERATED_BODY()

public:

	// --- IMPORTANT ---
	// WEIRD ASS PROBLEM: Passing USpudState to any of these interface methods, when it was a USaveGame with
	// a Serialize() method, caused the editor to start crashing at startup, trying to call Serialize() on the USpudState
	// through some editor loading code for a Wind component??
	// I fixed this mainly by making USpudState not a USaveGame and doing everything manually instead, no
	// Serialize() methods.
	
	/// Called just before this object and its SaveGame properties are persisted into a game state.
	/// This is called for root saved objects and nested UObjects
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD")
    void SpudPreStore(const USpudState* State);
	
	/// Called just after all the automatic property data has been written to the state for this object, but before the
	/// record is sealed. This is the place you can write any custom data you need that you can't expose in a UPROPERTY
	/// for some reason. Use methods on USpudStateCustomData to write custom data.
	/// You are in charge of making sure you write/read the same data in the finalise methods.
	/// This is only called for root objects (Actors and global objects), not nested UObjects, which cannot store custom data
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD")
    void SpudStoreCustomData(const USpudState* State, USpudStateCustomData* CustomData);

	/// Called just after this object and its SaveGame properties have been persisted (no further state can be written for this object)
	/// This is called for root objects and nested UObjects
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD")
    void SpudPostStore(const USpudState* State);

	/// Called just before PreRestore if the UserDataModelVersion of the stored data for this object is out of date
	/// This is an alternative to calling UpgradeAllSaveGames on USpudSubsystem, you can upgrade on demand with the
	/// benefit of having the actual object available. At this point you should probably only be modifying USpudState,
	/// to make it compatible, because the normal restore will still happen after this. See SpudPostRestoreDataModelUpgrade
	/// if you want to alter things afterwards.
	/// This is only called for root objects (actors and global objects), not nested UObjects
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD")
    void SpudPreRestoreDataModelUpgrade(USpudState* State, int32 StoredVersion, int32 CurrentVersion);
	
	/// Called just before this object's state is populated from a persistent state
	/// This is called for root objects and nested UObjects
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD")
    void SpudPreRestore(const USpudState* State);
	
	/// Called just after all the automatic property data has been loaded for this object, but before the
	/// record is finished. This is the place you can read any custom data you wrote during
	/// for some reason. Use methods on USpudStateCustomData to write custom data.
	/// You are in charge of making sure you write/read the same data in the finalise methods
	/// This is only called for root objects (actors and global objects), not nested UObjects
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD")
    void SpudRestoreCustomData(USpudState* State, USpudStateCustomData* CustomData);

	/// Called just before PostRestore if the UserDataModelVersion of the stored data for this object is out of date
	/// This is an alternative to calling UpgradeAllSaveGames on USpudSubsystem, you can upgrade on demand with the
	/// benefit of having the actual object available. At this point the normal restore process has occurred but you
	/// can do some post-restore things specific to the data model being changed.
	/// This is only called for root objects (actors and global objects), not nested UObjects
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD")
    void SpudPostRestoreDataModelUpgrade(const USpudState* State, int32 StoredVersion, int32 CurrentVersion);

	/// Called just after the state for this object has been fully restored.
	/// This is called for root objects and nested UObjects
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD")
    void SpudPostRestore(const USpudState* State);

};
