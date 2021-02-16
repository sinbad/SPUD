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
	/// Thumbnail screenshot (may be blank if one wasn't included in the save game)
	UPROPERTY(BlueprintReadOnly)
	UTexture2D* Thumbnail;

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

public:
	/// Direct access to save data - not recommended but if you really need it...
	FSpudSaveData SaveData;

protected:

	FString Source;

	void WriteCoreActorData(AActor* Actor, FArchive& Out) const;

	class StorePropertyVisitor : public SpudPropertyUtil::PropertyVisitor
	{
	protected:
		// Bare UObject but safe because we only call it inside GameState itself
		FSpudClassDef& ClassDef;
		TArray<uint32>& PropertyOffsets;
		FSpudClassMetadata& Meta;
		FMemoryWriter& Out;
	public:
		StorePropertyVisitor(FSpudClassDef& InClassDef, TArray<uint32>& InPropertyOffsets, FSpudClassMetadata& InMeta, FMemoryWriter& InOut);
		virtual bool VisitProperty(UObject* RootObject, FProperty* Property, uint32 CurrentPrefixID,
		                           void* ContainerPtr, int Depth) override;

		virtual void UnsupportedProperty(UObject* RootObject, FProperty* Property, uint32 CurrentPrefixID,
			int Depth) override;
		virtual uint32 GetNestedPrefix(FStructProperty* SProp, uint32 CurrentPrefixID) override;
	};

	FSpudSaveData::TLevelDataPtr GetLevelData(const FString& LevelName, bool AutoCreate);
	FSpudNamedObjectData* GetLevelActorData(const AActor* Actor, FSpudSaveData::TLevelDataPtr LevelData, bool AutoCreate);
	static FString GetClassName(const UObject* Obj);
	FSpudSpawnedActorData* GetSpawnedActorData(AActor* Actor, FSpudSaveData::TLevelDataPtr LevelData, bool AutoCreate);
	FSpudNamedObjectData* GetGlobalObjectData(const UObject* Obj, bool AutoCreate);
	FSpudNamedObjectData* GetGlobalObjectData(const FString& ID, bool AutoCreate);

	bool ShouldActorBeRespawnedOnRestore(AActor* Actor) const;
	void StoreActor(AActor* Actor, FSpudSaveData::TLevelDataPtr LevelData);
	void StoreLevelActorDestroyed(AActor* Actor, FSpudSaveData::TLevelDataPtr LevelData);
	void StoreGlobalObject(UObject* Obj, FSpudNamedObjectData* Data);

	// Actually restores the world, on the assumption that it's already loaded into the correct map
	void RestoreLoadedWorld(UWorld* World, bool bSingleLevel, const FString& OnlyLevelName = "");
	// Returns whether this is an actor which is not technically in a level, but is auto-created so doesn't need to be
	// spawned by the restore process. E.g. GameMode, Pawns
	bool ShouldRespawnRuntimeActor(const AActor* Actor) const;
	void PreRestoreObject(UObject* Obj, uint32 StoredUserVersion);
	void PostRestoreObject(UObject* Obj, const FSpudCustomData& FromCustomData, uint32 StoredUserVersion);
	void RestoreActor(AActor* Actor, FSpudSaveData::TLevelDataPtr LevelData, const TMap<FGuid, UObject*>* RuntimeObjects);
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


	/// Get the folder which will contain the level-specific game data for the active game while it's running
	/// This is so that not all level data needs to be in memory at once.
	FString GetActiveGameLevelFolder();

	/// Purge the active game's level data on disk, ready for a new game or loaded game.	
	void RemoveAllActiveGameLevelFiles();

public:

	static FString GetLevelName(const ULevel* Level);
	static FString GetLevelNameForObject(const UObject* Obj);

	USpudState();

	/// Clears all state
	void ResetState();

	/// Store the top-level information about the world, but none of the level contents
	void StoreWorldGlobals(UWorld* World);

	/**
	 * @brief Store the state of objects in the current world which are attached to a specific level.
	 * Only processes actors which implement ISpudObject.
	 * @param Level The level to store
	 * @param bReleaseAfter If true, after storing the level data, it is removed from memory and stored on disk
	 * @param bBlocking If true, do not perform the write in a background thread and write before returning
	 */
	void StoreLevel(ULevel* Level, bool bReleaseAfter, bool bBlocking);

	/// Store the state of an actor. Does not require the object to implement ISpudObject
	/// This object will be associated with its level, and so will only be restored when its level is loaded.
	/// Will page in the level data concerned from disk if necessary and will retain it in memory
	void StoreActor(AActor* Obj);

	/// Notify the state that an actor that is part of a level is being destroyed, and that should be remembered
	/// Will page in the level data concerned from disk if necessary and will retain it in memory
	void StoreLevelActorDestroyed(AActor* Actor);

	/// Stores any data for a level to disk and releases the memory its using to store persistent state
	void ReleaseLevelData(const FString& LevelName, bool bBlocking);

	/// Store the state of a global object, such as a GameInstance. Does not require the object to implement ISpudObject
	/// This object will have the same state across all levels.
	/// The identifier of this object is generated from its FName or SpudGUid property.
	void StoreGlobalObject(UObject* Obj);
	
	/// Store the state of a global object, such as a GameInstance. Does not require the object to implement ISpudObject
	/// This object will have the same state across all levels.
	/// This version uses a specific ID instead of one generated from the object's FName or SpudGuid property. 
	void StoreGlobalObject(UObject* Obj, const FString& ID);

	/// Restore just the contents of a level from this state. The level must already be loaded, and most likely you
	/// want it to only *just* have been loaded, so it doesn't contain any runtime objects yet.
	/// Restores actors which implement ISpudObject as the reverse of StoreLevel.
	/// Does NOT restore any global object state (see RestoreGlobalObject).
	void RestoreLevel(UWorld* World, const FString& LevelName);

	/// Specialised function for restoring a specific level by reference
	void RestoreLevel(ULevel* Level);

	/// Request that data for a level is loaded in the calling thread
	/// Useful for pre-caching before RestoreLevel
	bool PreLoadLevelData(const FString& LevelName);

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

	/// Save all contents to an archive
	/// This includes all paged out level data, which will be recombined
	virtual void SaveToArchive(FArchive& Ar);

	/**
	 * @brief 
	 * @param Ar The save file archive
	 * @param bFullyLoadAllLevelData If true, load all data into memory including all data for all levels. If false,
	 * only load global data and enumerate levels, piping level data to separate disk files instead for loading individually later
	 */
	virtual void LoadFromArchive(FArchive& Ar, bool bFullyLoadAllLevelData);

	/// Get the name of the persistent level which the player is on in this state
	FString GetPersistentLevel() const { return SaveData.GlobalData.CurrentLevel; }

	/// Get whether the persistent data for a given level is in memory right now or not
	bool IsLevelDataLoaded(const FString& LevelName);

	/// Clear the state for a given level (does not reset a loaded level, just deletes saved state)
	UFUNCTION(BlueprintCallable)
	void ClearLevel(const FString& LevelName);

	/// Get the source of this state (e.g. save file), if any;
	UFUNCTION(BlueprintCallable)
	const FString& GetSource() const { return Source; }

	/// Get the title associated with this save state 
	UFUNCTION(BlueprintCallable)
	const FText& GetTitle() const { return SaveData.Info.Title; }
	/// Set the title associated with this save state 
	UFUNCTION(BlueprintCallable)
	void SetTitle(const FText& Title) {SaveData.Info.Title = Title; }

	/// Get the timestamp for when this save state was created
	UFUNCTION(BlueprintCallable)
    const FDateTime& GetTimestamp() const { return SaveData.Info.Timestamp; }
	/// Set the timestamp for when this save state was created
	UFUNCTION(BlueprintCallable)
    void SetTimestamp(const FDateTime& Timestamp) {SaveData.Info.Timestamp = Timestamp; }

	/// Set the screenshot data for this save		
	UFUNCTION(BlueprintCallable)
	void SetScreenshot(TArray<uint8>& ImgData);


	/// Rename a class in this save data
	/// This is for performing upgrades on save games that would otherwise be broken
	/// Returns whether any changes were made
	UFUNCTION(BlueprintCallable)
	bool RenameClass(const FString& OldClassName, const FString& NewClassName);
	
	/// Rename a property on a class in this save data
	/// This is for performing upgrades on save games that would otherwise be broken
	/// OldPrefix & NewPrefix are for handling nested structs, format is "StructVarName1/StructVarName2" ofr
	/// a property which is inside variable named StructVarName1 on the class, and then inside StructVarName2 inside that
	/// Returns whether any changes were made
	UFUNCTION(BlueprintCallable)
    bool RenameProperty(const FString& ClassName, const FString& OldPropertyName, const FString& NewPropertyName, const FString& OldPrefix, const
                        FString& NewPrefix);

	/// Rename a global object so that it can be correctly found on load
	/// This is for performing upgrades on save games that would otherwise be broken
	/// Returns whether any changes were made
	UFUNCTION(BlueprintCallable)
    bool RenameGlobalObject(const FString& OldName, const FString& NewName);

	/// Rename a level object so that it can be correctly found on load
	/// This is for performing upgrades on save games that would otherwise be broken
	/// Returns whether any changes were made
	UFUNCTION(BlueprintCallable)
    bool RenameLevelObject(const FString& LevelName, const FString& OldName, const FString& NewName);

	/// Get a list of the levels we have state about
	UFUNCTION(BlueprintCallable)
    TArray<FString> GetLevelNames(bool bLoadedOnly);

	/// Utility method to read *just* the information part of a save game from the start of an archive
	/// This only reads the minimum needed to describe the save file and doesn't load any other data.
	static bool LoadSaveInfoFromArchive(FArchive& Ar, USpudSaveGameInfo& OutInfo);

};

/// Custom data that can be stored alongside properties for a UObject to handle anything else
/// Note: this is *deliberately* a very simple wrapper around sequential data. You have to write/read
/// the same way and it's not upgrade-proof (without you doing the work). The reason it's not more friendly,
/// with sa name lookups and metadata etc, is that this can get really inefficient because it's executing per object.
/// I want to push people toward using properties first and foremost because those have been optimised, with fast
/// paths for unchanged class structures and so on. Therefore if you want to do something purely custom here you
/// can, but it's pretty raw (and therefore still fast).
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
	bool CanWrite() const { return Ar && Ar->IsSaving(); }
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

	// Now a bunch of explicit functions so that Blueprints can do something useful with this

	/// Write a vector
	UFUNCTION(BlueprintCallable)
	void WriteVector(const FVector& V) { Write(V); }
	/**
	 * @brief Read a vector
	 * @param OutVector The vector we read if successful
	 * @return True if the value was read successfully
	 */
	UFUNCTION(BlueprintCallable)
    bool ReadVector(FVector& OutVector) { return Read(OutVector); }

	/// Write a rotator
	UFUNCTION(BlueprintCallable)
    void WriteRotator(const FRotator& Rot) { Write(Rot); }
	/**
	* @brief Read a rotator
	* @param OutRotator The rotator we read if successful
	* @return True if the value was read successfully
	*/
	UFUNCTION(BlueprintCallable)
    bool ReadRotator(FRotator& OutRotator) { return Read(OutRotator); }

	/// Write a transform
	UFUNCTION(BlueprintCallable)
    void WriteTransform(const FTransform& T) { Write(T); }
	/**
	* @brief Read a transform
	* @param OutTransform The transform we read if successful
	* @return True if the value was read successfully
	*/
	UFUNCTION(BlueprintCallable)
    bool ReadTransform(FTransform& OutTransform) { return Read(OutTransform); }

	/// Write a quaternion
	UFUNCTION(BlueprintCallable)
    void WriteQuaternion(const FQuat& Q) { Write(Q); }
	/**
	* @brief Read a quaternion
	* @param OutQuaternion The quaternion we read if successful
	* @return True if the value was read successfully
	*/
	UFUNCTION(BlueprintCallable)
    bool ReadQuaternion(FQuat& OutQuaternion) { return Read(OutQuaternion); }

	/// Write a string
	UFUNCTION(BlueprintCallable)
    void WriteString(const FString& S) { Write(S); }
	/**
	* @brief Read a string
	* @param OutString The string we read if successful
	* @return True if the value was read successfully
	*/
	UFUNCTION(BlueprintCallable)
    bool ReadString(FString& OutString) { return Read(OutString); }

	/// Write text
	UFUNCTION(BlueprintCallable)
    void WriteText(const FText& S) { Write(S); }
	/**
	* @brief Read text
	* @param OutText The text we read if successful
	* @return True if the value was read successfully
	*/
	UFUNCTION(BlueprintCallable)
    bool ReadText(FText& OutText) { return Read(OutText); }

	/// Write an int
	UFUNCTION(BlueprintCallable)
    void WriteInt(int V) { Write(V); }
	/**
	* @brief Read an int
	* @param OutInt The int we read if successful
	* @return True if the value was read successfully
	*/
	UFUNCTION(BlueprintCallable)
    bool ReadInt(int& OutInt) { return Read(OutInt); }

	/// Write an int64
	UFUNCTION(BlueprintCallable)
    void WriteInt64(int64 V) { Write(V); }
	/**
	* @brief Read an int64
	* @param OutInt64 The int64 we read if successful
	* @return True if the value was read successfully
	*/
	UFUNCTION(BlueprintCallable)
    bool ReadInt64(int64& OutInt64) { return Read(OutInt64); }

	/// Write a float
	UFUNCTION(BlueprintCallable)
    void WriteFloat(float V) { Write(V); }
	/**
	* @brief Read a float
	* @param OutFloat The float we read if successful
	* @return True if the value was read successfully
	*/
	UFUNCTION(BlueprintCallable)
    bool ReadFloat(float& OutFloat) { return Read(OutFloat); }

	/// Write a byte
	UFUNCTION(BlueprintCallable)
    void WriteByte(uint8 V) { Write(V); }
	/**
	* @brief Read a byte
	* @param OutByte The byte we read if successful
	* @return True if the value was read successfully
	*/
	UFUNCTION(BlueprintCallable)
    bool ReadByte(uint8& OutByte) { return Read(OutByte); }

};

