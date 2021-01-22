#pragma once

#include "CoreMinimal.h"
#include "SpudState.h"

#include "ISpudObject.generated.h"

UINTERFACE(MinimalAPI)
class USpudObject : public UInterface
{
	GENERATED_BODY()
};

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
	/// Return whether this object should be respawned on load if it was detected as a runtime-created object
	/// The default is to respawn all runtime objects except for Pawns, GameModes, GameStates and Characters which are assumed to be created automatically.
	/// You can override this if you want this for things like player pawns, game modes which are marked as runtime created, but
	/// are created automatically at level start so should not be created by the load process.
	/// Instead you should give these classes PersistenceGuid properties with predefined values so they can be populated.
	/// This can only be changed in C++ implementations and not Blueprints since they don't support this default impl
    virtual ESpudRespawnMode GetSpudRespawnMode() const { return ESpudRespawnMode::Default; }
	
};


UINTERFACE(MinimalAPI)
class USpudObjectCallback : public UInterface
{
	GENERATED_BODY()
};

/**
* Interface for fine control of persistence. Implement this in your objects to be notified when they are persisted or
* restored.
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
	
	/// Called just before this object and its SaveGame properties are persisted into a game state
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD")
    void SpudPreSaveState(const USpudState* State);
	
	/// Called just after all the automatic property data has been written to the state for this object, but before the
	/// record is sealed. This is the place you can write any custom data you need that you can't expose in a UPROPERTY
	/// for some reason. Use methods on USpudStateCustomData to write custom data.
	/// You are in charge of making sure you write/read the same data in the finalise methods
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD")
    void SpudFinaliseSaveState(const USpudState* State, USpudStateCustomData* CustomData);

	/// Called just after this object and its SaveGame properties have been persisted (no further state can be written for this object)
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD")
    void SpudPostSaveState(const USpudState* State);

	/// Called just before this object's state is populated from a persistent state
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD")
    void SpudPreLoadState(const USpudState* State);
	
	/// Called just after all the automatic property data has been loaded for this object, but before the
	/// record is finished. This is the place you can read any custom data you wrote during
	/// for some reason. Use methods on USpudStateCustomData to write custom data.
	/// You are in charge of making sure you write/read the same data in the finalise methods
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD")
    void SpudFinaliseLoadState(USpudState* State, USpudStateCustomData* CustomData);

	/// Called just after the state for this object has been fully restored.
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SPUD")
    void SpudPostLoadState(const USpudState* State);

};
