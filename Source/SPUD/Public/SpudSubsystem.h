#pragma once

#include "CoreMinimal.h"

#include "SpudCustomSaveInfo.h"
#include "SpudState.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Engine/World.h"

#include "SpudSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSpudSubsystem, Verbose, Verbose);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpudPreLoadGame, const FString&, SlotName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSpudPostLoadGame, const FString&, SlotName, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpudPreSaveGame, const FString&, SlotName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSpudPostSaveGame, const FString&, SlotName, bool, bSuccess);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpudPreLevelStore, const FString&, LevelName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSpudPostLevelStore, const FString&, LevelName, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpudPreLevelRestore, const FString&, LevelName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSpudPostLevelRestore, const FString&, LevelName, bool, bSuccess);

/// Helper delegates to allow blueprints to listen in on map transitions & streaming if they want
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpudPreTravelToNewMap, const FString&, NextMapName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSpudPostTravelToNewMap);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpudPreLoadStreamingLevel, const FName&, LevelName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpudPostLoadStreamingLevel, const FName&, LevelName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpudPreUnloadStreamingLevel, const FName&, LevelName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpudPostUnloadStreamingLevel, const FName&, LevelName);

// Callbacks passed to functions
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(bool, FSpudUpgradeSaveDelegate, class USpudState*, SaveState);

UENUM(BlueprintType)
enum class ESpudSystemState : uint8
{
	/// Game is not currently running, no persistence
	Disabled,
	/// Game is running, persistence is enabled
	RunningIdle,
	/// Currently loading a save game, cannot be interrupted
    LoadingGame,
	/// Currently saving a game, cannot be interrupted
    SavingGame,
	/// Starting a new game, after the next level load
	NewGameOnNextLevel,
};

UENUM(BlueprintType)
enum class ESpudSaveSorting : uint8
{
	/// No sort, ordering is dependent on file system retrieval 
	None,
	/// Sort by latest save first
	MostRecent,
	/// Sort by slot name, case-insensitive
	SlotName,
	/// Sort by title, case-insensitive
	Title
};

UCLASS(Transient)
class SPUD_API USpudStreamingLevelWrapper : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	ULevelStreaming* LevelStreaming;

	UFUNCTION()
	void OnLevelShown();
	UFUNCTION()
	void OnLevelHidden();
};

/// Subsystem which controls our save games, and also the active game's persistent state (for streaming levels)
UCLASS(Config=Engine)
class SPUD_API USpudSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

	friend USpudStreamingLevelWrapper;

public:
	/// Event fired just before a game is loaded
	UPROPERTY(BlueprintAssignable)
	FSpudPreLoadGame PreLoadGame;
	/// Event fired just after a game has finished loading
	UPROPERTY(BlueprintAssignable)
	FSpudPostLoadGame PostLoadGame;
	/// Event fired just before a game is saved
	UPROPERTY(BlueprintAssignable)
	FSpudPreSaveGame PreSaveGame;
	/// Event fired just after a game finished saving
	UPROPERTY(BlueprintAssignable)
	FSpudPostSaveGame PostSaveGame;
	/// Event fired just before we write the contents of a level to the state database
	UPROPERTY(BlueprintAssignable)
	FSpudPreLevelStore PreLevelStore;
	/// Event fired just after we've written the contents of a level to the state database
	UPROPERTY(BlueprintAssignable)
	FSpudPostLevelStore PostLevelStore;
	/// Event fired just before we're about to populate a loaded level from the state database
	UPROPERTY(BlueprintAssignable)
	FSpudPreLevelRestore PreLevelRestore;
	/// Event fired just after we've finished populating a loaded level from the state database
	UPROPERTY(BlueprintAssignable)
	FSpudPostLevelRestore PostLevelRestore;

	/// Event fired just prior to travelling to a new map (convenience for blueprints mainly, who don't have access to FCoreDelegates)
	UPROPERTY(BlueprintAssignable)
	FSpudPreTravelToNewMap PreTravelToNewMap;
	/// Event fired just after travelling to a new map (convenience for blueprints mainly, who don't have access to FCoreDelegates)
	UPROPERTY(BlueprintAssignable)
	FSpudPostTravelToNewMap PostTravelToNewMap;
	/// Event fired just before this subsystem loads a streaming level
	UPROPERTY(BlueprintAssignable)
	FSpudPreLoadStreamingLevel PreLoadStreamingLevel;
	/// Event fired just after a streaming level has loaded, but BEFORE any state has been restored
	UPROPERTY(BlueprintAssignable)
	FSpudPostLoadStreamingLevel PostLoadStreamingLevel;
	/// Event fired just before this subsystem unloads a streaming level, BEFORE any state has been stored if needed
	/// This is ALMOST the same as PreLevelStore, except when loading a game that's not called, but this is
	UPROPERTY(BlueprintAssignable)
	FSpudPreUnloadStreamingLevel PreUnloadStreamingLevel;
	/// Event fired just after a streaming level has unloaded
	UPROPERTY(BlueprintAssignable)
	FSpudPostUnloadStreamingLevel PostUnloadStreamingLevel;

	/// The time delay after the last request for a streaming level is withdrawn, that the level will be unloaded
	/// This is used to reduce load/unload thrashing at boundaries
	UPROPERTY(BlueprintReadWrite, Config)
	float StreamLevelUnloadDelay = 3;

	/// The desired width of screenshots taken for save games
	UPROPERTY(BlueprintReadWrite, Config)
	int32 ScreenshotWidth = 240;
	/// The desired height of screenshots taken for save games
	UPROPERTY(BlueprintReadWrite, Config)
	int32 ScreenshotHeight = 135;
	FDelegateHandle OnScreenshotHandle;

	/// If true, use the show/hide events of streaming levels to save/load, which is compatible with World Partition
	/// You can set this to false to change to the legacy mode which requires ASpudStreamingVolume
	UPROPERTY(BlueprintReadWrite, Config)
	bool bSupportWorldPartition = true;


protected:
	FDelegateHandle OnPreLoadMapHandle;
	FDelegateHandle OnPostLoadMapHandle;
	FDelegateHandle OnSeamlessTravelHandle;
	int32 LoadUnloadRequests = 0;
	bool FirstStreamRequestSinceMapLoad = true;
	TMap<int32, FName> LevelsPendingLoad;
	TMap<int32, FName> LevelsPendingUnload;
	FCriticalSection LevelsPendingLoadMutex;
	FCriticalSection LevelsPendingUnloadMutex;
	FTimerHandle StreamLevelUnloadTimerHandle;
	float ScreenshotTimeout = 0;	
	FString SlotNameInProgress;
	FText TitleInProgress;
	UPROPERTY()
	TObjectPtr<const USpudCustomSaveInfo> ExtraInfoInProgress;

	UPROPERTY()
	TArray<TWeakObjectPtr<UObject>> GlobalObjects;
	UPROPERTY()
	TMap<FString, TWeakObjectPtr<UObject>> NamedGlobalObjects;
	
	UPROPERTY(BlueprintReadOnly)
	ESpudSystemState CurrentState = ESpudSystemState::RunningIdle;

	// True while restoring game state, either by loading a game or restoring the state of a streamed-in level.
	UPROPERTY(BlueprintReadOnly)
	bool IsRestoringState = false;

	/// True when system shutdown has been started
	UPROPERTY(BlueprintReadOnly)
	bool bIsTearingDown = false;

	// The currently active game state
	UPROPERTY()
	TObjectPtr<USpudState> ActiveState;

	USpudState* GetActiveState()
	{
		if (!IsValid(ActiveState))
			ActiveState = NewObject<USpudState>();

		return ActiveState;
	}

	struct FStreamLevelRequests
	{
		TArray<TWeakObjectPtr<>> Requesters;
		bool bPendingUnload;
		float LastRequestExpiredTime;

		FStreamLevelRequests(): bPendingUnload(false), LastRequestExpiredTime(0)
		{
		}
	};
	
	// Map of streaming level names to the requests to load them 
	TMap<FName, FStreamLevelRequests> LevelRequests;

	UPROPERTY()
	TMap<ULevelStreaming*, USpudStreamingLevelWrapper*> MonitoredStreamingLevels;

	bool ServerCheck(bool LogWarning) const;

	UFUNCTION()
	void OnPreLoadMap(const FString& MapName);
	UFUNCTION()
	void OnSeamlessTravelTransition(UWorld* World);
	UFUNCTION()
	void OnPostLoadMap(UWorld* World);
	UFUNCTION()
	void OnActorDestroyed(AActor* Actor);
	void SubscribeAllLevelObjectEvents();
	void SubscribeLevelObjectEvents(ULevel* Level);
	void UnsubscribeLevelObjectEvents(ULevel* Level);
	void UnsubscribeAllLevelObjectEvents();
	
	// This is a latent callback and has to be BlueprintCallable
	UFUNCTION(BlueprintCallable)
	void PostLoadStreamLevel(int32 LinkID);
	UFUNCTION(BlueprintCallable)
    void PostUnloadStreamLevel(int32 LinkID);
	UFUNCTION(BlueprintCallable)
    void PostLoadStreamLevelGameThread(FName LevelName);
	UFUNCTION(BlueprintCallable)
    void PostUnloadStreamLevelGameThread(FName LevelName);

	void StoreWorld(UWorld* World, bool bReleaseLevels, bool bBlocking);
	void StoreLevel(ULevel* Level, bool bRelease, bool bBlocking);

	UFUNCTION()
	void ScreenshotTimedOut();
	UFUNCTION()
    void OnScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Colours);

	void FinishSaveGame(const FString& SlotName, const FText& Title, const USpudCustomSaveInfo* ExtraInfo, TArray<uint8>* ScreenshotData);
	void LoadComplete(const FString& SlotName, bool bSuccess);
	void SaveComplete(const FString& SlotName, bool bSuccess);

	void HandleLevelLoaded(FName LevelName);
	void HandleLevelLoaded(ULevel* Level) { HandleLevelLoaded(FName(USpudState::GetLevelName(Level))); }
	void HandleLevelUnloaded(ULevel* Level);

	void LoadStreamLevel(FName LevelName, bool Blocking);
	void StartUnloadTimer();
	void StopUnloadTimer();
	void CheckStreamUnload();
	void UnloadStreamLevel(FName LevelName);

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure)
	bool IsLoadingGame() const { return CurrentState == ESpudSystemState::LoadingGame; }

	UFUNCTION(BlueprintPure)
    bool IsSavingGame() const { return CurrentState == ESpudSystemState::SavingGame; }

	UFUNCTION(BlueprintPure)
    bool IsIdle() const { return CurrentState == ESpudSystemState::RunningIdle; }

	
	/**
	 * Start a new game with a blank persistent state
	 * @param bCheckServerOnly Whether to only allow this call on the server 
	 * @param bAfterLevelLoad Restart tracking of state only after the next level load. Set this to true if you're currently
	 *   in a level which has persistent state that you don't want to keep. You MUST load a level after setting this option,
	 *   even if it's the same level you're currently on.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
    void NewGame(bool bCheckServerOnly = true, bool bAfterLevelLoad = false);

	/// Terminate a running game. Does not save state. Call this when returning to a main menu for example.
	/// All map changes after this are ignored by the persistence system
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	void EndGame();

	/** Trigger an autosave of the game; this one will register as the latest save, but will NOT count as a Quick Save
    * for Quick Load purposes
	* @param Title Optional title of the save, if blank will be titled "Autosave"
	* @param bTakeScreenshot If true, the save will include a screenshot, the dimensions of which are
	* set by the ScreenshotWidth/ScreenshotHeight properties.
	* @param ExtraInfo Optional object containing custom fields you want to be available when listing saves
	**/
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
    void AutoSaveGame(FText Title = FText(), bool bTakeScreenshot = true, const USpudCustomSaveInfo* ExtraInfo = nullptr);
	/** Perform a Quick Save of the game in a single re-used slot, in response to a player request
	 * @param Title Optional title of the save, if blank will be titled "Quick Save"
	 * @param bTakeScreenshot If true, the save will include a screenshot, the dimensions of which are
	 * set by the ScreenshotWidth/ScreenshotHeight properties.
	 * @param ExtraInfo Optional object containing custom fields you want to be available when listing saves
	 **/
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
    void QuickSaveGame(FText Title = FText(), bool bTakeScreenshot = true, const USpudCustomSaveInfo* ExtraInfo = nullptr);
	
	/**
	 * Quick load the game from the last player-requested Quick Save slot (NOT the last autosave or manual save)
	 * @param TravelOptions Options string to include in the travel URL e.g. "Listen"
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
    void QuickLoadGame(const FString& TravelOptions = FString(TEXT("")));
	
	/**
	 * Continue a game from the latest save of any kind - autosave, quick save, manual save. The same as calling LoadGame on the most recent. 
	 * @param TravelOptions Options string to include in the travel URL e.g. "Listen"
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
    void LoadLatestSaveGame(const FString& TravelOptions = FString(TEXT("")));

	/// Create a save game descriptor which you can use to store additional descriptive information about a save game.
	/// Fill the returned object in then pass it to the SaveGame call to have additional info to display on save/load screens
	/// Could be things like completion percentage, hours played, current quests, character class, character level etc
	UFUNCTION(BlueprintCallable)
	USpudCustomSaveInfo* CreateCustomSaveInfo();

	/**
	 * Save a game. Asynchronous, use the PostSaveGame event to determine when the save is finished.
	 * @param SlotName The name of the slot for this save
	 * @param Title A descriptive title to go with the save
	 * @param bTakeScreenshot If true, the save will include a screenshot, the dimensions of which are
	 * set by the ScreenshotWidth/ScreenshotHeight properties.
	 * @param ExtraInfo Optional object containing custom fields you want to be available when listing saves
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
    void SaveGame(const FString& SlotName, const FText& Title = FText(), bool bTakeScreenshot = true, const USpudCustomSaveInfo* ExtraInfo = nullptr);
	/**
	 * Load the game in a given slot name. Asynchronous, use the PostLoadGame event to determine when load is complete (and success)
	 * @param SlotName The slot name of the save to load
	 * @param TravelOptions Options string to include in the travel URL e.g. "Listen"
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
    void LoadGame(const FString& SlotName, const FString& TravelOptions = FString(TEXT("")));

	/// Delete the save game in a given slot
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
    bool DeleteSave(const FString& SlotName);

	/**
	* Add a global object to the list of objects which will have their state saved / loaded
	* Level actors which implement ISpudObject will automatically be saved/loaded but global objects like GameInstance
	* will not be, by default. Call this to add additional objects to the save/load behaviour.
	*
	* As a global object, Obj is expected to always exist and will not be re-created by this system. In addition,
	* it must be uniquely identifiable, either by its FName, or by a SpudGuid property you add to it. If you cannot
	* guarantee the FName is always the same (may not always be the case with auto-created objects like GameInstance)
	* then either add a predefined SpudGuid (FGuid) property and always set it to a known value, or use the alternative
	* AddPersistentGlobalObjectWithName method, which identifies this object explicitly.
	* 
	* @param Obj The global object to track. This object will have its state saved/loaded.
	*/
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	void AddPersistentGlobalObject(UObject* Obj);
	
	/**
	* Add a global object to the list of objects which will have their state saved / loaded, identified by a name.
	* Level actors which implement ISpudObject will automatically be saved/loaded but global objects like GameInstance
	* will not be, by default. Call this to add additional objects to the save/load behaviour. The name is in case you can't guarantee the
	* FName of the object is always the same (e.g. GameInstance_0, GameInstance_1 auto-generated names), so you always
	* want this object to be identified by this name. You could also add a constant FGuid property called SpudGuid to
	* identify the object, but this method means you don't have to do that.
	* 
	* @param Obj The global object to track. This object will have its state saved/loaded.
	* @param Name The name by which to identify this object in the save file. 
	*/
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
    void AddPersistentGlobalObjectWithName(UObject* Obj, const FString& Name);

	/**
	 * Remove an object from tracking so it will no longer be saved or loaded.
	 * @param Obj The object to remove from tracking
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
    void RemovePersistentGlobalObject(UObject* Obj);

	/**
	 * Clears / forgets all state associated with a named level in the active game. Use this to reset a level back
	 * to its original state. The level should not be loaded when you call this, because it does NOT reset any actors,
	 * it just clears the state so that next time the level is loaded, there is no saved state to restore. So call this
	 * before loading the level / travelling to the map.
	 * @param LevelName The name of the level to remove state for. This should be the name of the map file with no
	 * prefix or extension.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	void ClearLevelState(const FString& LevelName);

	/// Make a request that a streaming level is loaded. Won't load if already loaded, but will
	/// record the request count so that unloading is done when all requests are withdrawn.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	void AddRequestForStreamingLevel(UObject* Requester, FName LevelName, bool BlockingLoad);
	/// Withdraw a request for a streaming level. Once all requesters have rescinded their requests, the
	/// streaming level will be considered ready to be unloaded.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	void WithdrawRequestForStreamingLevel(UObject* Requester, FName LevelName);

	/// Get the list of the save games with metadata
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	TArray<USpudSaveGameInfo*> GetSaveGameList(bool bIncludeQuickSave = true, bool bIncludeAutoSave = true, ESpudSaveSorting Sorting = ESpudSaveSorting::None);

	/// Get info about the latest save game
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	USpudSaveGameInfo* GetLatestSaveGame();

	/// Get info about the quick save game, may return null if none
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
    USpudSaveGameInfo* GetQuickSaveGame();

	/// Get info about the auto save game, may return null if none
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
    USpudSaveGameInfo* GetAutoSaveGame();

	/// Get information about a specific save game slot
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	USpudSaveGameInfo* GetSaveGameInfo(const FString& SlotName);


	/// By default you're not allowed to interrupt save / load operations and any requests received while another is
	/// believed to be ongoing is ignored. This method basically overrides this and tells the system to accept
	/// new requests regardless. This is a last resort that you should never need, but it's here as a safety valve in case of errors.
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
    void ForceReset();

	/// Set the version number recorded with world's data model in all save data written after this.
	/// This is for use when you need to explicitly upgrade save games because of a change in the
	/// classes / properties being saved, and the default property name matching rules are not enough.
	/// Many changes to what you save are non-breaking. For example the following are not breaking:
	/// 1. Adding new classes that are saved (they just won't be populated from old save games)
	/// 2. Adding new SaveGame properties (they just won't be changed by loading old saves)
	/// 3. Removing classes or properties (that data in old saves will be ignored)
	///
	/// Mostly it will just mean that you either lose some old data (fine if you don't care) or get the defaults
	/// when you pull in data from old saves.
	///
	/// However, renaming things or making more significant changes where you need to upgrade the data through a
	/// more explicit transformation process can be a problem.
	/// That is what this UserDataModelVersion is for. If you never set it, it will be 0. This number will be written
	/// into the metadata of all save data.
	///
	/// If you later make a change to your save data which you need to explicitly upgrade, then you should increment
	/// this number (call this function before you need to save/load any data, as part of your game startup routine).
	/// 
	/// If, when loading some object data, the number in the data is different to this version, then all data for objects
	/// described by the old version will be fed through a user-defined upgrade hook, to allow you to make manual fixes.
	UFUNCTION(BlueprintCallable)
    void SetUserDataModelVersion(int32 Version);

	/// Gets the current version number of your game's data model (@see SetUserDataModelVersion for more details)
	UFUNCTION(BlueprintCallable)
    int32 GetUserDataModelVersion() const;

	/**
	 * Triggers the upgrade process for all save games (asynchronously)
	 * 
	 * Each save game present will be fully loaded, and for each where the user data model version of any part differs from latest,
     * the SaveNeedsUpgrading callback will be triggered (in a background thread). That callback should perform any
     * changes it needs to the USpudState. When the callback completes the save will be written back to
     * disk if the callback returned true.
     * Note that at no point will any actors or levels be loaded. All changes made to the save are done manually so
     * that next time they need to be applied to real game objects, the state is as you need it to be.
     * If for some reason you need to have the actors loaded to perform the upgrade, then you should instead
     * implement ISpudObjectCallback on those objects and perform the upgrades there instead (this cannot be done in
     * bulk though, only on demand as each object is loaded).
	 * @param WorldContextObject World context object
	 * @param bUpgradeEvenIfNoUserDataModelVersionDifferences The default behaviour is to only upgrade save games where
	 * the UserDataModelVersion in the save differs from the current user model version. You can therefore control which
	 * saves need manual upgrading by incrementing SetUserDataModelVersion when you have a breaking change. Set this
	 * argument to true to process all saves instead.
	 * @param SaveNeedsUpgradingCallback The delegate which will be called for each save which needs upgrading. Important:
	 * the save game will only be re-written to disk if you return true from this callback. A backup will be made of the previous save.
	 * @param LatentInfo Completion callback
	 */
	UFUNCTION(BlueprintCallable, meta=(Latent, LatentInfo = "LatentInfo"), Category="SPUD")
	void UpgradeAllSaveGames(bool bUpgradeEvenIfNoUserDataModelVersionDifferences, FSpudUpgradeSaveDelegate SaveNeedsUpgradingCallback, FLatentActionInfo LatentInfo);
	
	/// Return whether a named slot is a quick save
	/// Useful for when parsing through saves to check if something is a manual save or not
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
    bool IsQuickSave(const FString& SlotName);
	/// Return whether a named slot is a quick save
	/// Useful for when parsing through saves to check if something is a manual save or not
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
    bool IsAutoSave(const FString& SlotName);

	/**
	 * Notify @see USpudSubsystem that a level was loaded externally. By default, SPUD uses its custom
	 * @see ASpudStreamingVolume instances for notification of level streaming events. This method provides
	 * an interface to use other methods of loading streaming levels.
	 * @param LevelName The level that was loaded.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	void NotifyLevelLoadedExternally(FName LevelName);

	/**
	 * Notify @see USpudSubsystem that a level was unloaded externally. By default, SPUD uses its custom
	 * @see ASpudStreamingVolume instances for notification of level streaming events. This method provides
	 * an interface to use other methods of unloading streaming levels.
	 * @param Level The level that was unloaded.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly)
	void NotifyLevelUnloadedExternally(ULevel* Level);

	static FString GetSaveGameDirectory();
	static FString GetSaveGameFilePath(const FString& SlotName);
	// Lists saves: note that this is only the filenames, not the directory
	static void ListSaveGameFiles(TArray<FString>& OutSaveFileList);
	static FString GetActiveGameFolder();
	static FString GetActiveGameFilePath(const FString& Name);


	// FTickableGameObject begin
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickableWhenPaused() const override;
	virtual TStatId GetStatId() const override;
	// FTickableGameObject end
	
};

inline USpudSubsystem* GetSpudSubsystem(UWorld* WorldContext)
{
	if (IsValid(WorldContext) && WorldContext->IsGameWorld())
	{
		auto GI = WorldContext->GetGameInstance();
		if (IsValid(GI))
			return GI->GetSubsystem<USpudSubsystem>();		
	}
		
	return nullptr;
}