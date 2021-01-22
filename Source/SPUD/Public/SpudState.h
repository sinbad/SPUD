#pragma once

#include "CoreMinimal.h"
#include "SpudData.h"
#include "SpudPropertyUtil.h"

#include "SpudState.generated.h"


DECLARE_LOG_CATEGORY_EXTERN(LogSpudState, Verbose, Verbose);

/// Description of a save game for display in load game lists, finding latest
/// All properties are read-only because they can only be populated via calls to save game
UCLASS(BlueprintType)
class SPUD_API USpudSaveGameInfo : public UObject
{
	// this class duplicates elements of FSpudSaveInfo deliberately, since that's a storage structure and this
	// is for Blueprint convenience
	GENERATED_BODY()
	public:
	/// Top-line title string. Might include the name of the region, current quest etc
	UPROPERTY(BlueprintReadOnly)
	FText Title;
	/// Timestamp of when this save was created
	UPROPERTY(BlueprintReadOnly)
	FDateTime Timestamp;
	/// The name of the save game slot this refers to
	UPROPERTY(BlueprintReadOnly)
	FString SlotName;

};

/// Holds the persistent state of a game.
/// Persistent state is any state which should be restored on load; whether that's the load of a save
/// game, or whether that's the loading of a streaming level section within an active game.
/// The state is divided into global state, and state associated with levels. Global state is always resident in this
/// object, but level state is only resident when needed, allowing persistent state to scale better as levels increase
/// without it all needing to be in memory at once.
/// 
/// This state can be persisted to disk in 2 ways:
/// 1. As a save game (all data for all levels combined into a single file)
/// 2. As the active game (levels are split into separate files so they can be loaded / saved individually to maintain active state)
///
/// Loading a save game involves taking data as a single save game, and splitting it out into separate 'active' files
/// so that as levels are loaded / unloaded, those single files can  be updated without needing to have any other level
/// persistent state in memory. Then as maps load, they can request data from this object to populate themselves.
/// Whenever you leave a map, or a streaming level is unloaded, that single level file is
/// updated to preserve the active game state.
///
/// Saving a game involves updating this state object with anything currently in memory, then saving it as a single file.
/// This means combining all the separated level chunks back into a single file.
///
/// To make the splitting / combining more efficient, the data format for a single level will be the same whether it's
/// in the single save file, or the separated active file. That means on save we can recombine the files without
/// actually loading / parsing the data back in.
UCLASS(BlueprintType)
class SPUD_API USpudState : public UObject
{
	GENERATED_BODY()

	friend class USpudStateCustomData;

protected:

	FSpudSaveData SaveData;

	static FString GetLevelName(const ULevel* Level);
	static FString GetLevelNameForObject(const UObject* Obj);

	void WriteCoreActorData(AActor* Actor, FArchive& Out) const;

	class UpdateFromPropertyVisitor : public SpudPropertyUtil::PropertyVisitor
	{
	protected:
		// Bare UObject but safe because we only call it inside GameState itself
		FSpudClassDef& ClassDef;
		TArray<uint32>& PropertyOffsets;
		FSpudClassMetadata& Meta;
		FMemoryWriter& Out;
	public:
		UpdateFromPropertyVisitor(FSpudClassDef& InClassDef, TArray<uint32>& InPropertyOffsets, FSpudClassMetadata& InMeta, FMemoryWriter& InOut);
		virtual bool VisitProperty(UObject* RootObject, FProperty* Property, uint32 CurrentPrefixID,
		                           void* ContainerPtr, int Depth) override;

		virtual void UnsupportedProperty(UObject* RootObject, FProperty* Property, uint32 CurrentPrefixID,
			int Depth) override;
		virtual uint32 GetNestedPrefix(FStructProperty* SProp, uint32 CurrentPrefixID) override;
	};

	FSpudLevelData* GetLevelData(const FString& LevelName, bool AutoCreate);
	FSpudNamedObjectData* GetLevelActorData(const AActor* Actor, FSpudLevelData* LevelData, bool AutoCreate);
	static FString GetClassName(const UObject* Obj);
	FSpudSpawnedActorData* GetSpawnedActorData(AActor* Actor, FSpudLevelData* LevelData, bool AutoCreate);
	FSpudNamedObjectData* GetGlobalObjectData(const UObject* Obj, bool AutoCreate);
	FSpudNamedObjectData* GetGlobalObjectData(const FString& ID, bool AutoCreate);

	void UpdateFromWorldImpl(UWorld* World, bool bSingleLevel, const FString& OnlyLevelName = "");
	bool ShouldActorBeRespawnedOnRestore(AActor* Actor) const;
	void UpdateFromActor(AActor* Actor, FSpudLevelData* LevelData);
	void UpdateLevelActorDestroyed(AActor* Actor, FSpudLevelData* LevelData);
	void UpdateFromGlobalObject(UObject* Obj, FSpudNamedObjectData* Data);

	// Actually restores the world, on the assumption that it's already loaded into the correct map
	void RestoreLoadedWorld(UWorld* World, bool bSingleLevel, const FString& OnlyLevelName = "");
	// Returns whether this is an actor which is not technically in a level, but is auto-created so doesn't need to be
	// spawned by the restore process. E.g. GameMode, Pawns
	bool ShouldRespawnRuntimeActor(const AActor* Actor) const;
	void PreRestoreObject(UObject* Obj);
	void PostRestoreObject(UObject* Obj, const FSpudCustomData& FromCustomData);
	void RestoreActor(AActor* Actor, FSpudLevelData* LevelData, const TMap<FGuid, UObject*>* RuntimeObjects);
	void RestoreGlobalObject(UObject* Obj, const FSpudNamedObjectData* Data);
	AActor* RespawnActor(const FSpudSpawnedActorData& SpawnedActor, const FSpudClassMetadata& Meta, ULevel* Level);
	void DestroyActor(const FSpudDestroyedLevelActor& DestroyedActor, ULevel* Level);
	void RestoreCoreActorData(AActor* Actor, const FSpudCoreActorData& FromData);
	void RestoreObjectProperties(UObject* Obj, const FSpudPropertyData& FromData, const FSpudClassMetadata& Meta,
	                             const TMap<FGuid, UObject*>* RuntimeObjects);
	void RestoreObjectPropertiesFast(UObject* Obj, const FSpudPropertyData& FromData,
	                                 const FSpudClassMetadata& Meta, const FSpudClassDef*
	                                 ClassDef, const TMap<FGuid, UObject*>* RuntimeObjects);
	void RestoreObjectPropertiesSlow(UObject* Obj, const FSpudPropertyData& FromData,
	                                 const FSpudClassMetadata& Meta,
	                                 const FSpudClassDef* ClassDef, const TMap<FGuid, UObject*>* RuntimeObjects);

	class RestorePropertyVisitor : public SpudPropertyUtil::PropertyVisitor
	{
	protected:
		const FSpudClassDef& ClassDef;
		const FSpudClassMetadata& Meta;
		const TMap<FGuid, UObject*>* RuntimeObjects;
		FMemoryReader& DataIn;
	public:
		RestorePropertyVisitor(FMemoryReader& InDataIn, const FSpudClassDef& InClassDef, const FSpudClassMetadata& InMeta, const TMap<FGuid, UObject*>* InRuntimeObjects):
			ClassDef(InClassDef), Meta(InMeta), RuntimeObjects(InRuntimeObjects), DataIn(InDataIn) {}

		virtual uint32 GetNestedPrefix(FStructProperty* SProp, uint32 CurrentPrefixID) override;		
	};


	// Fast path restoration when runtime class is the same as stored class
	class RestoreFastPropertyVisitor : public RestorePropertyVisitor
	{
	protected:
		TArray<FSpudPropertyDef>::TConstIterator StoredPropertyIterator;
	public:
		RestoreFastPropertyVisitor(const TArray<FSpudPropertyDef>::TConstIterator& InStoredPropertyIterator,
		                           FMemoryReader& InDataIn, const FSpudClassDef& InClassDef,
		                           const FSpudClassMetadata& InMeta, const TMap<FGuid, UObject*>* InRuntimeObjects)
			: RestorePropertyVisitor(InDataIn, InClassDef, InMeta, InRuntimeObjects),
			  StoredPropertyIterator(InStoredPropertyIterator)
		{
		}

		virtual bool VisitProperty(UObject* RootObject, FProperty* Property, uint32 CurrentPrefixID,
		                           void* ContainerPtr, int Depth) override;
	};
	
	// Slow path restoration when runtime class is the same as stored class
	class RestoreSlowPropertyVisitor : public RestorePropertyVisitor
	{
	public:
		RestoreSlowPropertyVisitor(FMemoryReader& InDataIn, const FSpudClassDef& InClassDef, const FSpudClassMetadata& InMeta, const TMap<FGuid, UObject*>* InRuntimeObjects)
			: RestorePropertyVisitor(InDataIn, InClassDef, InMeta, InRuntimeObjects) {}

		virtual bool VisitProperty(UObject* RootObject, FProperty* Property, uint32 CurrentPrefixID,
		                           void* ContainerPtr, int Depth) override;
	};
public:

	USpudState();

	/// Clears all state
	void ResetState();

	/// Update the game state from every object in the current world. Only processes actors which implement ISpudObject,
	/// and associates them with the level they're attached to.
	void UpdateFromWorld(UWorld* World);

	/// Update the game state from objects in the current world which are attached to a specific level.
	/// Only processes actors which implement ISpudObject.
	void UpdateFromLevel(UWorld* World, const FString& LevelName);

	/// Update the game state from objects in the current world which are attached to a specific level.
	/// Only processes actors which implement ISpudObject.
	void UpdateFromLevel(ULevel* Level);

	/// Update the game state from an object in the world. Does not require the object to implement ISpudObject
	/// This object will be associated with its level, and so will only be restored when its level is loaded.
	void UpdateFromActor(AActor* Obj);

	/// Notify the state that an actor that is part of a level is being destroyed, and that should be remembered
	void UpdateLevelActorDestroyed(AActor* Actor);

	/// Update the game state from a global object, such as a GameInstance. Does not require the object to implement ISpudObject
	/// This object will have the same state across all levels.
	/// The identifier of this object is generated from its FName or SpudGUid property.
	void UpdateFromGlobalObject(UObject* Obj);
	
	/// Update the game state from a global object, such as a GameInstance. Does not require the object to implement ISpudObject
	/// This object will have the same state across all levels.
	/// This version uses a specific ID instead of one generated from the object's FName or SpudGuid property. 
	void UpdateFromGlobalObject(UObject* Obj, const FString& ID);

	/// Restore just the contents of a level from this state. The level must already be loaded, and most likely you
	/// want it to only *just* have been loaded, so it doesn't contain any runtime objects yet.
	/// Restores actors which implement ISpudObject as the reverse of UpdateFromLevel.
	/// Does NOT restore any global object state (see RestoreGlobalObject).
	void RestoreLevel(UWorld* World, const FString& LevelName);

	/// Specialised function for restoring a specific level by reference
	void RestoreLevel(ULevel* Level);

	// Restores the world and all levels currently in it, on the assumption that it's already loaded into the correct map
	void RestoreLoadedWorld(UWorld* World);

	/// Restores a single actor from  this state. Does not require the actor to implement ISpudObject.
	/// NOTE: this is a limited function, it's less efficient than using RestoreLevel for multiple actors, and it
	/// also cannot restore object cross-references if those references refer to runtime-spawned objects
	void RestoreActor(AActor* Actor);
	
	/// Restore the contents of a single global object
	/// This object will have the same state across all levels.
	/// The identifier of this object is generated from its FName or SpudGUid property.
	void RestoreGlobalObject(UObject* Obj);
	
	/// Restore the contents of a single global object
	/// This object will have the same state across all levels.
	/// This version uses a specific ID instead of one generated from the object's FName or SpudGuid property. 
	void RestoreGlobalObject(UObject* Obj, const FString& ID);

	// Separate Read / Write because it's just better for us and Serialize() often does batshit things
	// E.g. When this class was subclassed from USaveGame and had a Serialize(), and was used as an argument to an
	// interface, the Editor would crash on startup, calling my Serialize in the middle of loading some wind component???
	// Must be because it was matching some pattern and the interface was drawing attention or something. Let's keep
	// it completely custom so we don't have to deal with crap like that.
	virtual void SaveToArchive(FArchive& Ar, const FText& Title);
	virtual void LoadFromArchive(FArchive& Ar);

	/// Get the name of the persistent level which the player is on in this state
	FString GetPersistentLevel() const { return SaveData.GlobalData.CurrentLevel; }

	/// Utility method to read *just* the information part of a save game from the start of an archive
	/// This only reads the minimum needed to describe the save file and doesn't load any other data.
	static bool LoadSaveInfoFromArchive(FArchive& Ar, USpudSaveGameInfo& OutInfo);
};

UCLASS()
class SPUD_API USpudStateCustomData : public UObject
{
	GENERATED_BODY()
protected:
	FArchive *Ar;

public:
	USpudStateCustomData() : Ar(nullptr) {}

	void Init(FArchive* InOut)
	{
		Ar = InOut;
	}

	bool CanRead() const { return Ar && Ar->IsLoading(); }
	bool CanWrite() const { return Ar && Ar->IsLoading(); }
	bool AtEnd() const { return Ar && Ar->AtEnd(); }

	/// Write a value to the custom data
	/// NOTE: May reformat some data types for efficiency, e.g. bool becomes uint8
	template <typename T>
    void Write(const T& Value)
	{
		if (!CanWrite())
		{
			UE_LOG(LogSpudState, Error, TEXT("CustomData invalid for writing"));
			return;
		}
		
		SpudPropertyUtil::WriteRaw(Value, *Ar);
	}

	/// Try to read a value from the custom data
	template <typename T>
    bool Read(T& OutValue)
	{
		if (!CanRead())
		{
			UE_LOG(LogSpudState, Error, TEXT("CustomData invalid for reading"));
			return false;
		}
		if (AtEnd())
		{
			UE_LOG(LogSpudState, Error, TEXT("CustomData has reached the end, cannot read"));
			return false;
		}
		
		SpudPropertyUtil::ReadRaw(OutValue, *Ar);
		return true;
	}


};

