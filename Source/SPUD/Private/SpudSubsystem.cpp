#include "SpudSubsystem.h"
#include "SpudState.h"
#include "Engine/LevelStreaming.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameUserSettings.h"
#include "ImageUtils.h"
#include "TimerManager.h"
#include "HAL/FileManager.h"
#include "Async/Async.h"
#include "Kismet/KismetRenderingLibrary.h"

#include "SaveGameSystem.h"
#include "PlatformFeatures.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SceneViewport.h"

DEFINE_LOG_CATEGORY(LogSpudSubsystem)


#define SPUD_QUICKSAVE_SLOTNAME TEXT("__QuickSave__")
#define SPUD_AUTOSAVE_SLOTNAME TEXT("__AutoSave__")


void USpudSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	bIsTearingDown = false;
	// Note: this will register for clients too, but callbacks will be ignored
	// We can't call ServerCheck() here because GameMode won't be valid (which is what we use to determine server mode)
	// UserIndex 0: single-player assumption; multi-user map loads would need a stored LoadingUserIndex member
	OnPostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &USpudSubsystem::OnPostLoadMap, 0);
	OnPreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &USpudSubsystem::OnPreLoadMap);
	
	OnSeamlessTravelHandle = FWorldDelegates::OnSeamlessTravelTransition.AddUObject(this, &USpudSubsystem::OnSeamlessTravelTransition);
	
#if WITH_EDITORONLY_DATA
	// The one problem we have is that in PIE mode, PostLoadMap doesn't get fired for the current map you're on
	// So we'll need to trigger it manually
	// Also "AlwaysLoaded" maps do NOT trigger PostLoad, and at this point, they're NOT in the level list, meaning if
	// we try to sub to levels right now, we'll only see the PersistentLevel
	// So, we're going to have to delay this call by a frame
	auto World = GetWorld();
	if (World && World->WorldType == EWorldType::PIE)
	{
		FTimerHandle TempHandle;
		GetWorld()->GetTimerManager().SetTimer(TempHandle,[this]()
		{
			// TODO: make this more configurable, use a known save etc
			NewGame(false);

		}, 0.2f, false);
	}

	
#endif
}

void USpudSubsystem::Deinitialize()
{
	Super::Deinitialize();
	bIsTearingDown = true;
	
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(OnPostLoadMapHandle);
	FCoreUObjectDelegates::PreLoadMap.Remove(OnPreLoadMapHandle);
	FWorldDelegates::OnSeamlessTravelTransition.Remove(OnSeamlessTravelHandle);

	// Clean up streaming level event listeners, as they may fire after we've been destroyed
	for (auto It = MonitoredStreamingLevels.CreateIterator(); It; ++It)
	{
		ULevelStreaming* const Level = It.Key();
		if (ensure(Level))
		{
			USpudStreamingLevelWrapper* const Wrapper = It.Value();
			Level->OnLevelShown.RemoveAll(Wrapper);
			Level->OnLevelHidden.RemoveAll(Wrapper);
			It.RemoveCurrent();
		}
	}
}


void USpudSubsystem::NewGame(bool bCheckServerOnly, bool bAfterLevelLoad)
{
	if (bCheckServerOnly && !ServerCheck(true))
		return;
		
	if (IsSavingGame())
	{
		UE_LOG(LogSpudSubsystem, Log, TEXT("NewGame deferred until async save completes"));
		bPendingEndGame = false;
		PendingNewGameArgs.Emplace(bCheckServerOnly, bAfterLevelLoad);
		return;
	}
	PendingNewGameArgs.Reset();

	EndGame();
	
	// EndGame will have unsubscribed from all current levels
	// Re-sub if we want to keep state for currently loaded levels, or not if starting from next level load
	// This allows the caller to call NewGame mid-game and then load a map, and the current levels won't try to save
	if (bAfterLevelLoad)
	{
		CurrentState = ESpudSystemState::NewGameOnNextLevel;
	}
	else
	{
		CurrentState = ESpudSystemState::RunningIdle;
		SubscribeAllLevelObjectEvents();
	}
}

bool USpudSubsystem::ServerCheck(bool LogWarning) const
{
	// Note: must only call this when game mode is present! Don't call when unloading
	// On missing world etc we just assume true for safety
	auto GI = GetGameInstance();
	if (!GI)
		return true;

	auto World = GI->GetWorld();
	if (!World)
		return true;
	
	return World->GetAuthGameMode() != nullptr;
}

void USpudSubsystem::EndGame()
{
	if (CurrentState == ESpudSystemState::SavingGameAsync)
	{
		UE_LOG(LogSpudSubsystem, Log, TEXT("EndGame deferred until async save completes"));
		PendingNewGameArgs.Reset();
		bPendingEndGame = true;
		return;
	}
	bPendingEndGame = false;

	if (ActiveState)
		ActiveState->ResetState();

	// Allow GC to collect
	ActiveState = nullptr;

	UnsubscribeAllLevelObjectEvents();
	CurrentState = ESpudSystemState::Disabled;
	IsRestoringState = false;
	SaveBlockers.Empty();
}

void USpudSubsystem::AutoSaveGame(FText Title, bool bTakeScreenshot, const USpudCustomSaveInfo* ExtraInfo, const int32 UserIndex, bool bAsync)
{
	// Authoritative enforcement is the guard in SaveGame(); this early-out only prevents the
	// numbered-slot cleanup below from deleting an old save for a save that will then be refused.
	if (IsSaveBlocked())
	{
		UE_LOG(LogSpudSubsystem, Warning, TEXT("AutoSaveGame refused before slot cleanup: save is blocked"));
		SaveComplete(SPUD_AUTOSAVE_SLOTNAME, UserIndex, false);
		return;
	}

	FString SlotName;
	FText DefaultTitle;

	if (MaxAutoSaves > 0)
	{
		const auto NextNumber = FMath::Max(1, GetHighestSlotNumber(SPUD_AUTOSAVE_SLOTNAME, UserIndex) + 1);
		SlotName = MakeNumberedSlotName(SPUD_AUTOSAVE_SLOTNAME, NextNumber);
		DefaultTitle = FText::Format(NSLOCTEXT("Spud", "AutoSaveTitleNumbered", "Autosave {0}"), NextNumber);
		CleanupOldNumberedSaves(SPUD_AUTOSAVE_SLOTNAME, MaxAutoSaves, NextNumber, UserIndex);
	}
	else
	{
		SlotName = SPUD_AUTOSAVE_SLOTNAME;
		DefaultTitle = NSLOCTEXT("Spud", "AutoSaveTitle", "Autosave");
	}

	SaveGame(SlotName, Title.IsEmpty() ? DefaultTitle : Title, bTakeScreenshot, ExtraInfo, UserIndex, bAsync);
}

void USpudSubsystem::QuickSaveGame(FText Title, bool bTakeScreenshot, const USpudCustomSaveInfo* ExtraInfo, const int32 UserIndex, bool bAsync)
{
	// Authoritative enforcement is the guard in SaveGame(); this early-out only prevents the
	// numbered-slot cleanup below from deleting an old save for a save that will then be refused.
	if (IsSaveBlocked())
	{
		UE_LOG(LogSpudSubsystem, Warning, TEXT("QuickSaveGame refused before slot cleanup: save is blocked"));
		SaveComplete(SPUD_QUICKSAVE_SLOTNAME, UserIndex, false);
		return;
	}

	FString SlotName;
	FText DefaultTitle;

	if (MaxQuickSaves > 0)
	{
		const auto NextNumber = FMath::Max(1, GetHighestSlotNumber(SPUD_QUICKSAVE_SLOTNAME,UserIndex) + 1);
		SlotName = MakeNumberedSlotName(SPUD_QUICKSAVE_SLOTNAME,NextNumber);
		DefaultTitle = FText::Format(NSLOCTEXT("Spud", "QuickSaveTitleNumbered", "Quick Save {0}"), NextNumber);
		CleanupOldNumberedSaves(SPUD_QUICKSAVE_SLOTNAME, MaxQuickSaves,NextNumber, UserIndex);
	}
	else
	{
		SlotName = SPUD_QUICKSAVE_SLOTNAME;
		DefaultTitle = NSLOCTEXT("Spud", "QuickSaveTitle", "Quick Save");
	}

	SaveGame(SlotName, Title.IsEmpty() ? DefaultTitle : Title, bTakeScreenshot, ExtraInfo, UserIndex, bAsync);
}

void USpudSubsystem::QuickLoadGame(const FString& TravelOptions, const int32 UserIndex)
{
	FString SlotName;
	if (MaxQuickSaves > 0)
	{
		const auto LatestNumber = GetHighestSlotNumber(SPUD_QUICKSAVE_SLOTNAME,UserIndex);
		if (LatestNumber > 0)
		{
			SlotName = MakeNumberedSlotName(SPUD_QUICKSAVE_SLOTNAME,LatestNumber);
		}
		else if (LatestNumber == 0)
		{
			SlotName = SPUD_QUICKSAVE_SLOTNAME;
		}
		else
		{
			return;
		}
	}
	else
	{
		SlotName = SPUD_QUICKSAVE_SLOTNAME;
	}

	LoadGame(SlotName, TravelOptions, UserIndex);
}


bool USpudSubsystem::IsQuickSave(const FString& SlotName)
{
	int32 Unused;
	return ParseNumberedSlotName(SlotName, SPUD_QUICKSAVE_SLOTNAME, Unused);
}

bool USpudSubsystem::IsAutoSave(const FString& SlotName)
{
	int32 Unused;
	return ParseNumberedSlotName(SlotName, SPUD_AUTOSAVE_SLOTNAME, Unused);
}

bool USpudSubsystem::ParseNumberedSlotName(const FString& SlotName, const TCHAR* BaseSlotName, int32& OutNumber)
{
	if (SlotName == BaseSlotName)
	{
		OutNumber = 0;
		return true;
	}

	const FString Base(BaseSlotName);
	const auto Prefix = Base.LeftChop(2) + TEXT("_");
	if (SlotName.StartsWith(Prefix) && SlotName.EndsWith(TEXT("__")) && SlotName.Len() > Prefix.Len() + 2)
	{
		const auto NumberStr = SlotName.Mid(Prefix.Len(), SlotName.Len() - Prefix.Len() - 2);
		if (NumberStr.IsNumeric())
		{
			OutNumber = FCString::Atoi(*NumberStr);
			return OutNumber > 0;
		}
	}

	return false;
}

FString USpudSubsystem::MakeNumberedSlotName(const TCHAR* BaseSlotName, int32 Number)
{
	const FString Base(BaseSlotName);
	return Base.LeftChop(2) + FString::Printf(TEXT("_%d__"), Number);
}

int32 USpudSubsystem::GetHighestSlotNumber(const TCHAR* BaseSlotName, const int32 UserIndex) const
{
	TArray<FString> SaveFiles;
	ListSaveGameFiles(SaveFiles, UserIndex);

	auto HighestNumber = -1;
	for (const auto& File : SaveFiles)
	{
		const auto SlotName = FPaths::GetBaseFilename(File);
		int32 Number;
		if (ParseNumberedSlotName(SlotName, BaseSlotName, Number) && Number > HighestNumber)
		{
			HighestNumber = Number;
		}
	}
	return HighestNumber;
}

void USpudSubsystem::CleanupOldNumberedSaves(const TCHAR* BaseSlotName, int32 MaxToKeep, int32 LatestNumber, const int32 UserIndex)
{
	TArray<FString> SaveFiles;
	ListSaveGameFiles(SaveFiles, UserIndex);

	const auto OldestToKeep = LatestNumber - MaxToKeep + 1;
	for (const auto& File : SaveFiles)
	{
		const auto SlotName = FPaths::GetBaseFilename(File);
		int32 Number;
		if (ParseNumberedSlotName(SlotName, BaseSlotName, Number) && Number < OldestToKeep)
		{
			DeleteSave(SlotName, UserIndex);
		}
	}
}

void USpudSubsystem::NotifyLevelLoadedExternally(FName LevelName)
{
	HandleLevelLoaded(LevelName);
}

void USpudSubsystem::NotifyLevelUnloadedExternally(ULevel* Level)
{
	HandleLevelUnloaded(Level);
}

void USpudSubsystem::LoadLatestSaveGame(const FString& TravelOptions, const int32 UserIndex)
{
	auto Latest = GetLatestSaveGame();
	if (Latest)
		LoadGame(Latest->SlotName, TravelOptions, UserIndex);
}

void USpudSubsystem::OnPreLoadMap(const FString& MapName)
{
	if (!ServerCheck(false))
		return;

	PreTravelToNewMap.Broadcast(MapName);
	// All streaming maps will be unloaded by travelling, so remove all
	LevelRequests.Empty();
	StopUnloadTimer();
	MonitoredStreamingLevels.Empty();

	// Actors/abilities that pushed a save blocker are destroyed by travel without removing it.
	// Clear so a leaked blocker can't permanently disable saving in the next map. Logged at Log
	// (not Warning) because a surviving blocker at travel time is the expected case.
	if (!SaveBlockers.IsEmpty())
	{
		UE_LOG(LogSpudSubsystem, Log, TEXT("Clearing %d save blocker(s) on map transition"), SaveBlockers.Num());
		SaveBlockers.Empty();
	}

	FirstStreamRequestSinceMapLoad = true;

	// When we transition out of a map while enabled, save contents
	if (CurrentState == ESpudSystemState::RunningIdle)
	{
		UnsubscribeAllLevelObjectEvents();

		const auto World = GetWorld();
		if (IsValid(World))
		{
			UE_LOG(LogSpudSubsystem, Verbose, TEXT("OnPreLoadMap saving: %s"), *UGameplayStatics::GetCurrentLevelName(World));
#ifdef USE_SAVEGAMESYSTEM
			// Keep it in memory
			StoreWorld(World, false, true);
#else
			// Map and all streaming level data will be released.
			// Block while doing it so they all get written predictably
			StoreWorld(World, true, true);
#endif
		}
	}
}

void USpudSubsystem::OnSeamlessTravelTransition(UWorld* World)
{
	if (IsValid(World))
	{
		FString MapName = UGameplayStatics::GetCurrentLevelName(World);
		UE_LOG(LogSpudSubsystem, Verbose, TEXT("OnSeamlessTravelTransition: %s"), *MapName);
		// Just before seamless travel, do the same thing as pre load map on OpenLevel
		OnPreLoadMap(MapName);
	}
}

void USpudSubsystem::OnPostLoadMap(UWorld* World, const int32 UserIndex)
{
	// Issue #130: double-check that world has authority as well, GameInstance can be null?
	if (!ServerCheck(false) || (World && World->GetNetMode() >= NM_Client))
		return;


	switch(CurrentState)
	{
	case ESpudSystemState::NewGameOnNextLevel:
		if (IsValid(World)) // nullptr seems possible if load is aborted or something?
		{
			const FString LevelName = UGameplayStatics::GetCurrentLevelName(World);
			UE_LOG(LogSpudSubsystem,
				   Verbose,
				   TEXT("OnPostLoadMap NewGame starting: %s"),
				   *LevelName);
			// We need to subscribe to ALL currently loaded levels, because of "AlwaysLoaded" sublevels
			SubscribeAllLevelObjectEvents();
			CurrentState = ESpudSystemState::RunningIdle;
		}
		break;
	case ESpudSystemState::RunningIdle:
	case ESpudSystemState::LoadingGame:
		// This is called when a new map is loaded
		// In all cases, we try to load the state
		if (IsValid(World)) // nullptr seems possible if load is aborted or something?
		{
			const FString LevelName = UGameplayStatics::GetCurrentLevelName(World);
			UE_LOG(LogSpudSubsystem,
			       Verbose,
			       TEXT("OnPostLoadMap restore: %s"),
			       *LevelName);

			IsRestoringState = true;

			const auto State = GetActiveState();
			PreLevelRestore.Broadcast(LevelName);
			State->RestoreLoadedWorld(World);
			PostLevelRestore.Broadcast(LevelName, true);
			
			IsRestoringState = false;
			// We need to subscribe to ALL currently loaded levels, because of "AlwaysLoaded" sublevels
			SubscribeAllLevelObjectEvents();
		}

		// If we were loading, this is the completion
		if (CurrentState == ESpudSystemState::LoadingGame)
		{
			LoadComplete(SlotNameInProgress, UserIndex, true);
			UE_LOG(LogSpudSubsystem, Log, TEXT("Load: Success"));
		}

		break;
	default:
		break;
			
	}

	PostTravelToNewMap.Broadcast();
}

void USpudSubsystem::SaveGame(const FString& SlotName, const FText& Title, bool bTakeScreenshot, const USpudCustomSaveInfo* ExtraInfo, const int32 UserIndex, bool bAsync)
{
	if (!ServerCheck(true))
	{
		SaveComplete(SlotName, UserIndex, false);
        return;
	}

	if (SlotName.IsEmpty())
	{
		UE_LOG(LogSpudSubsystem, Error, TEXT("Cannot save a game with a blank slot name"));		
		SaveComplete(SlotName, UserIndex, false);
		return;
	}

	if (CurrentState != ESpudSystemState::RunningIdle)
	{
		// TODO: ignore or queue?
		UE_LOG(LogSpudSubsystem, Error, TEXT("TODO: Overlapping calls to save/load, resolve this"));
		SaveComplete(SlotName, UserIndex, false);
		return;
	}

	if (IsSaveBlocked())
	{
		const auto Reasons = GetSaveBlockers();
		UE_LOG(LogSpudSubsystem, Warning, TEXT("Save to slot %s refused: blocked by %d reason(s) [%s]"),
			*SlotName, Reasons.Num(),
			*FString::JoinBy(Reasons, TEXT(", "), [](const FName& R){ return R.ToString(); }));
		SaveComplete(SlotName, UserIndex, false);
		return;
	}

	CurrentState = bAsync ? ESpudSystemState::SavingGameAsync : ESpudSystemState::SavingGame;
	PreSaveGame.Broadcast(SlotName);

	if (bTakeScreenshot)
	{
		SlotNameInProgress = SlotName;
		TitleInProgress = Title;
		ExtraInfoInProgress = ExtraInfo;

		if (const auto Viewport = GEngine->GameViewport ? GEngine->GameViewport->GetGameViewport() : nullptr)
		{
			if (const auto Size = Viewport->GetSizeXY(); Size.X > 0 && Size.Y > 0)
			{
				if (GEngine->GetGameUserSettings()->IsHDREnabled())
				{
					if (TArray<FLinearColor> HDRPixels; Viewport->ReadLinearColorPixels(HDRPixels))
					{
						TWeakObjectPtr WeakThis(this);
						AsyncTask(ENamedThreads::AnyBackgroundHiPriTask,
							[WeakThis, HDRPixels = MoveTemp(HDRPixels), W = Size.X, H = Size.Y, UserIndex]() mutable
							{
								TArray<FColor> Pixels;
								Pixels.SetNumUninitialized(HDRPixels.Num());
								for (auto i = 0; i < HDRPixels.Num(); ++i)
								{
									Pixels[i] = HDRPixels[i].ToFColor(true);
								}
								HDRPixels.Empty();

								AsyncTask(ENamedThreads::GameThread, [WeakThis, Pixels = MoveTemp(Pixels), W, H, UserIndex]() mutable
								{
									if (const auto S = WeakThis.Get())
									{
										S->OnScreenshotCaptured(W, H, MoveTemp(Pixels), UserIndex);
									}
								});
							}
						);
							
						return;
					}
				}
				else if (TArray<FColor> Pixels; Viewport->ReadPixels(Pixels))
				{
					OnScreenshotCaptured(Size.X, Size.Y, MoveTemp(Pixels), UserIndex);
					return;
				}
			}
		}

		UE_LOG(LogSpudSubsystem, Warning, TEXT("Failed to read viewport pixels for save screenshot, saving without thumbnail"));
		FinishSaveGame(SlotName, UserIndex, Title, ExtraInfo, nullptr);
	}
	else
	{
		FinishSaveGame(SlotName, UserIndex, Title, ExtraInfo, nullptr);
	}
}

void USpudSubsystem::OnScreenshotCaptured(int32 Width, int32 Height, TArray<FColor>&& Colours, const int32 UserIndex)
{
	const int32 TargetWidth = ScreenshotWidth;
	const int32 TargetHeight = ScreenshotHeight;
	TWeakObjectPtr WeakThis(this);

	AsyncTask(ENamedThreads::AnyBackgroundHiPriTask,
		[WeakThis, RawPixels = MoveTemp(Colours), Width, Height, TargetWidth, TargetHeight, UserIndex]() mutable
		{
			TArray<FColor> CroppedResized;
			FImageUtils::CropAndScaleImage(Width, Height, TargetWidth, TargetHeight, RawPixels, CroppedResized);
			RawPixels.Empty();

			auto PngData = MakeShared<TArray<uint8>>();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1
			FImageUtils::ThumbnailCompressImageArray(TargetWidth, TargetHeight, CroppedResized, *PngData);
#else
			FImageUtils::CompressImageArray(TargetWidth, TargetHeight, CroppedResized, *PngData);
#endif

			AsyncTask(ENamedThreads::GameThread, [WeakThis, PngData, UserIndex]()
			{
				if (const auto S = WeakThis.Get())
				{
					S->FinishSaveGame(S->SlotNameInProgress, UserIndex, S->TitleInProgress, S->ExtraInfoInProgress, &*PngData);
				}
			});
		});
}

void USpudSubsystem::FinishSaveGame(const FString& SlotName, const int32 UserIndex, const FText& Title, const USpudCustomSaveInfo* ExtraInfo, TArray<uint8>* ScreenshotData)
{
	auto State = GetActiveState();
	auto World = GetWorld();

	// We do NOT reset
	// a) deleted objects must remain, they're built up over time
	// b) we may not be updating all levels and must retain for the others

	State->StoreWorldGlobals(World);
	
	for (const auto& Ptr : GlobalObjects)
	{
		if (Ptr.IsValid())
			State->StoreGlobalObject(Ptr.Get());
	}
	for (const auto& Pair : NamedGlobalObjects)
	{
		if (Pair.Value.IsValid())
			State->StoreGlobalObject(Pair.Value.Get(), Pair.Key);
	}

	// Store any data that is currently active in the game world in the state object
	StoreWorld(World, false, true);

	State->SetTitle(Title);
	State->SetTimestamp(FDateTime::Now());
	if (PopulateCustomSaveInfo.IsBound())
	{
		const auto CustomInfo = NewObject<USpudCustomSaveInfo>();
		if (ExtraInfo)
		{
			CustomInfo->SetData(ExtraInfo->GetData());
		}
		PopulateCustomSaveInfo.Broadcast(*CustomInfo);
		State->SetCustomSaveInfo(CustomInfo);
	}
	else
	{
		State->SetCustomSaveInfo(ExtraInfo);
	}
	if (ScreenshotData)
		State->SetScreenshot(*ScreenshotData);

	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	if (!SaveSystem)
	{
		SaveComplete(SlotName, UserIndex, false);
		return;
	}

	if (CurrentState == ESpudSystemState::SavingGameAsync)
	{
		// StoreWorld has finished capturing all actor data into plain struct buffers.
		// SaveToArchive only reads those buffers (no UObject derefs) so it can safely
		// run on a background thread. EndGame() is guarded against SavingGameAsync,
		// so ActiveState (UPROPERTY) remains alive for the duration of the task.
		TWeakObjectPtr WeakState(State);
		TWeakObjectPtr WeakThis(this);
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
			[WeakThis, WeakState, SaveSystem, SlotName, UserIndex]{
				auto CompleteOnGameThread = [WeakThis, SlotName, UserIndex](bool bSuccess)
				{
					AsyncTask(ENamedThreads::GameThread, [WeakThis, SlotName, UserIndex, bSuccess]{
						if (USpudSubsystem* S = WeakThis.Get())
							S->SaveComplete(SlotName, UserIndex, bSuccess);
					});
				};

				const auto RawState = WeakState.Get();
				if (!RawState)
				{
					UE_LOG(LogSpudSubsystem, Error, TEXT("Save state was destroyed before async serialization could start for slot %s"), *SlotName);
					CompleteOnGameThread(false);
					return;
				}

				TSharedRef<TArray<uint8>> OutSaveData = MakeShared<TArray<uint8>>();
				{
					FMemoryWriter Archive(*OutSaveData, true);
					RawState->SaveToArchive(Archive);
					Archive.Close();
					if (Archive.IsError() || Archive.IsCriticalError())
					{
						UE_LOG(LogSpudSubsystem, Error, TEXT("Error while creating save game for slot %s"), *SlotName);
							CompleteOnGameThread(false);
							return;
						}
				}

				if (OutSaveData->Num() == 0 || SlotName.IsEmpty())
				{
					UE_LOG(LogSpudSubsystem, Error, TEXT("Error while creating save game for slot %s"), *SlotName);
					CompleteOnGameThread(false);
					return;
				}

				// Dispatch disk write from the game thread for platform safety - console
				// save systems may not support calls from arbitrary background threads.
				AsyncTask(ENamedThreads::GameThread, [WeakThis, SaveSystem, SlotName, UserIndex, OutSaveData]()
				{
					SaveSystem->SaveGameAsync(false, *SlotName, FPlatformMisc::GetPlatformUserForUserIndex(UserIndex), OutSaveData,
						[WeakThis, UserIndex, SlotName](const FString&, FPlatformUserId, bool bSuccess)
						{
							check(IsInGameThread());

							if (bSuccess)
							{
								UE_LOG(LogSpudSubsystem, Log, TEXT("Save to slot %s: Success"), *SlotName);
							}
							else
							{
								UE_LOG(LogSpudSubsystem, Error, TEXT("Error while saving game to %s"), *SlotName);
							}

							if (USpudSubsystem* S = WeakThis.Get())
							{
								S->SaveComplete(SlotName, UserIndex, bSuccess);
							}
						});
				});
			});
		}
		else
		{
			TSharedRef<TArray<uint8>> OutSaveData(new TArray<uint8>());
			auto Archive = FMemoryWriter(*OutSaveData, true);
			State->SaveToArchive(Archive);
			Archive.Close();

			if (Archive.IsError() || Archive.IsCriticalError())
			{
				UE_LOG(LogSpudSubsystem, Error, TEXT("Error while creating save game for slot %s"), *SlotName);
				SaveComplete(SlotName, UserIndex, false);
			}
			else if (OutSaveData->Num() > 0 && SlotName.Len() > 0)
			{
				bool SaveOK;
				if (!SaveSystem->SaveGame(false, *SlotName, UserIndex, *OutSaveData))
				{
					UE_LOG(LogSpudSubsystem, Error, TEXT("Error while saving game to %s"), *SlotName);
					SaveOK = false;
				}
				else
				{
					UE_LOG(LogSpudSubsystem, Log, TEXT("Save to slot %s: Success"), *SlotName);
					SaveOK = true;
				}
				SaveComplete(SlotName, UserIndex, SaveOK);
			}
			else
			{
				UE_LOG(LogSpudSubsystem, Error, TEXT("Error while creating save game for slot %s"), *SlotName);
				SaveComplete(SlotName, UserIndex, false);
			}
	}
}

void USpudSubsystem::SaveComplete(const FString& SlotName, const int32 UserIndex, bool bSuccess)
{
	CurrentState = ESpudSystemState::RunningIdle;
	PostSaveGame.Broadcast(SlotName, bSuccess);
	// It's possible that the reference to SlotName *is* SlotNameInProgress, so we can't reset it until after
	SlotNameInProgress = "";
	TitleInProgress = FText();
	ExtraInfoInProgress = nullptr;

	if (PendingNewGameArgs.IsSet())
	{
		const auto [bCheckServerOnly, bAfterLevelLoad] = PendingNewGameArgs.GetValue();
		PendingNewGameArgs.Reset();
		NewGame(bCheckServerOnly, bAfterLevelLoad);
	}
	else if (bPendingEndGame)
	{
		bPendingEndGame = false;
		EndGame();
	}
}

void USpudSubsystem::AddSaveBlocker(FName Reason)
{
	check(IsInGameThread());

	if (Reason.IsNone())
	{
		UE_LOG(LogSpudSubsystem, Warning, TEXT("AddSaveBlocker called with NAME_None; ignoring"));
		return;
	}

	const auto NewCount = ++SaveBlockers.FindOrAdd(Reason);
	UE_LOG(LogSpudSubsystem, Verbose, TEXT("Save blocker added: %s (count %d, total reasons %d)"),
		*Reason.ToString(), NewCount, SaveBlockers.Num());
}

void USpudSubsystem::RemoveSaveBlocker(FName Reason)
{
	check(IsInGameThread());

	const auto Count = SaveBlockers.Find(Reason);
	if (!Count)
	{
		UE_LOG(LogSpudSubsystem, Warning, TEXT("RemoveSaveBlocker(%s): reason was not registered"),
			*Reason.ToString());
		return;
	}

	if (--(*Count) <= 0)
	{
		SaveBlockers.Remove(Reason);
		UE_LOG(LogSpudSubsystem, Verbose, TEXT("Save blocker cleared: %s (total reasons %d)"),
			*Reason.ToString(), SaveBlockers.Num());
	}
}

TArray<FName> USpudSubsystem::GetSaveBlockers() const
{
	TArray<FName> Reasons;
	SaveBlockers.GetKeys(Reasons);
	return Reasons;
}

void USpudSubsystem::HandleLevelLoaded(FName LevelName)
{
	if (!ServerCheck(false))
		return;
	
	// Defer the restore to the game thread, streaming calls happen in loading thread?
	// However, quickly ping the state to force it to pre-load the leveldata
	// that way the loading occurs in this thread, less latency
	GetActiveState()->PreLoadLevelData(LevelName.ToString());

	TWeakObjectPtr WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, LevelName]()
	{
		USpudSubsystem* Self = WeakThis.Get();
		if (!Self)
		{
			return;
		}
		FTimerHandle H;
		if (UWorld* World = Self->GetWorld())
		{
			World->GetTimerManager().SetTimer(H,
				FTimerDelegate::CreateWeakLambda(Self, [Self, LevelName]()
				{
					Self->PostLoadStreamLevelGameThread(LevelName);
				}), 0.01f, false);
		}
	});
}

void USpudSubsystem::HandleLevelUnloaded(ULevel* Level)
{
	if (!ServerCheck(false))
		return;
	
	if (ShouldStoreLevel(Level))
	{
		UnsubscribeLevelObjectEvents(Level);

		if (CurrentState != ESpudSystemState::LoadingGame && !bIsTearingDown)
		{
			// NOTE: even though we're attempting to NOT do this while tearing down, in PIE it will still happen on end play
			// This is because for some reason, in PIE the GameInstance shutdown function is called AFTER the levels are flushed,
			// compared to before in normal game shutdown. See the difference between UEditorEngine::EndPlayMap() and UGameEngine::PreExit()
			// We can't really fix this; we could listen on FEditorDelegates::PrePIEEnded but that would require linking the editor module (bleh) 
			// save the state
			// when loading game we will unload the current level and streaming and don't want to restore the active state from that
			// After storing, the level data is released so doesn't take up memory any more
			StoreLevel(Level, true, false);
		}
	}
}



void USpudSubsystem::StoreWorld(UWorld* World, bool bReleaseLevels, bool bBlocking)
{
	for (auto && Level : World->GetLevels())
	{
		if (ShouldStoreLevel(Level))
		{
			StoreLevel(Level, bReleaseLevels, bBlocking);
		}
	}	
}

void USpudSubsystem::StoreLevel(ULevel* Level, bool bRelease, bool bBlocking)
{
	const FString LevelName = USpudState::GetLevelName(Level);
	PreLevelStore.Broadcast(LevelName);
	GetActiveState()->StoreLevel(Level, bRelease, bBlocking);
	PostLevelStore.Broadcast(LevelName, true);
}

void USpudSubsystem::LoadGame(const FString& SlotName, const FString& TravelOptions, const int32 UserIndex)
{
	if (!ServerCheck(true))
	{
		LoadComplete(SlotName, UserIndex, false);
		return;
	}

	if (CurrentState != ESpudSystemState::RunningIdle)
	{
		// TODO: ignore or queue?
		UE_LOG(LogSpudSubsystem, Error, TEXT("TODO: Overlapping calls to save/load, resolve this"));
		LoadComplete(SlotName, UserIndex, false);
		return;
	}

	CurrentState = ESpudSystemState::LoadingGame;
	IsRestoringState = true;
	PreLoadGame.Broadcast(SlotName);

	UE_LOG(LogSpudSubsystem, Verbose, TEXT("Loading Game from slot %s"), *SlotName);		

	auto State = GetActiveState();

	State->ResetState();

#ifdef USE_SAVEGAMESYSTEM
	
	// VIVI: Consoles require using the SaveGameSystem
	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();

	if (SaveSystem)
	{
		TArray<uint8> InSaveData;
		if (SaveSystem->LoadGame(false, *SlotName, UserIndex, InSaveData))
		{
			auto Archive = FMemoryReader(InSaveData, true);
			// Whole thing is in memory, might as well load it all
			State->LoadFromArchive(Archive, true);
			Archive.Close();

			if (Archive.IsError() || Archive.IsCriticalError())
			{
				UE_LOG(LogSpudSubsystem, Error, TEXT("Error while loading game from %s"), *SlotName);
				LoadComplete(SlotName, UserIndex, false);
				return;
			}
		}
		else
		{
			UE_LOG(LogSpudSubsystem, Error, TEXT("Error while loading game from %s"), *SlotName);
			LoadComplete(SlotName, UserIndex, false);
			return;
		}
	}
	
#else

	// TODO: async load

	IFileManager& FileMgr = IFileManager::Get();
	auto Archive = TUniquePtr<FArchive>(FileMgr.CreateFileReader(*GetSaveGameFilePath(SlotName)));

	if(Archive)
	{
		// Load only global data and page in level data as needed
		State->LoadFromArchive(*Archive, false);
		Archive->Close();

		if (Archive->IsError() || Archive->IsCriticalError())
		{
			UE_LOG(LogSpudSubsystem, Error, TEXT("Error while loading game from %s"), *SlotName);
			LoadComplete(SlotName, UserIndex, false);
			return;
		}
	}
	else
	{
		UE_LOG(LogSpudSubsystem, Error, TEXT("Error while opening save game for slot %s"), *SlotName);		
		LoadComplete(SlotName, UserIndex, false);
		return;
	}
	
#endif

    // The world package gets loaded way before we end up loading the world
    // this cause an issue with the world being garbage collected from the package before we load, thus the load failing
    // Manually loading the package here and keeping a ref to the world to prevent GC
    // It's ugly, but only solution I could find.
    // Related UDN posts:
    //      https://udn.unrealengine.com/s/question/0D54z00007NVjVPCA1/async-loading-of-a-persistent-level-before-openlevel
    //      https://udn.unrealengine.com/s/question/0D5QP000002ZPnb0AG/streamablemanager%E3%82%92%E5%88%A9%E7%94%A8%E3%81%97%E3%81%9F%E9%9D%9E%E5%90%8C%E6%9C%9F%E8%AA%AD%E3%81%BF%E8%BE%BC%E3%81%BF%E4%B8%AD%E3%81%AB%E5%AE%9F%E8%A1%8C%E3%81%97%E3%81%9Fopenlevel%E3%81%A7%E3%83%87%E3%83%95%E3%82%A9%E3%83%AB%E3%83%88%E3%83%AC%E3%83%99%E3%83%AB%E3%81%B8%E9%81%B7%E7%A7%BB%E3%81%97%E3%81%A6%E3%81%97%E3%81%BE%E3%81%86
	{
        const FString LevelName = State->GetPersistentLevel();
	    // See if the level is already in memory
	    UPackage* WorldPackage = FindPackage(nullptr, *LevelName);

	    // If the level isn't already in memory, load level from disk
	    if (WorldPackage == nullptr)
	    {
	        WorldPackage = LoadPackage(nullptr, *LevelName, LOAD_None);
	    }

	    if (WorldPackage != nullptr)
	    {
	        // Find the newly loaded world, and keep a reference to it to prevent GC
	        WorldToLoad = UWorld::FindWorldInPackage(WorldPackage);
	    }
	}
	
	// Just do the reverse of what we did
	// Global objects first before map, these should be only objects which survive map load
	for (const auto& Ptr : GlobalObjects)
	{
		if (Ptr.IsValid())
			State->RestoreGlobalObject(Ptr.Get());
	}
	for (const auto& Pair : NamedGlobalObjects)
	{
		if (Pair.Value.IsValid())
			State->RestoreGlobalObject(Pair.Value.Get(), Pair.Key);
	}

	// This is deferred, final load process will happen in PostLoadMap
	SlotNameInProgress = SlotName;
	UE_LOG(LogSpudSubsystem, Verbose, TEXT("(Re)loading map: %s"), *State->GetPersistentLevel());
	UGameplayStatics::OpenLevel(GetWorld(), FName(State->GetPersistentLevel()), true, TravelOptions);
}


void USpudSubsystem::LoadComplete(const FString& SlotName, const int32 UserIndex, bool bSuccess)
{
	CurrentState = ESpudSystemState::RunningIdle;
	IsRestoringState = false;
	PostLoadGame.Broadcast(SlotName, bSuccess);
	// It's possible that the reference to SlotName *is* SlotNameInProgress, so we can't reset it until after
	SlotNameInProgress = "";
	
	WorldToLoad = nullptr;
}

bool USpudSubsystem::DeleteSave(const FString& SlotName, const int32 UserIndex)
{
	if (!ServerCheck(true))
		return false;

#ifdef USE_SAVEGAMESYSTEM
	// VIVI: Consoles require using the SaveGameSystem
	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();

	if (SaveSystem)
	{
		return SaveSystem->DeleteGame(false, *SlotName, UserIndex);
	}
	return false;
#else
	IFileManager& FileMgr = IFileManager::Get();
	return FileMgr.Delete(*GetSaveGameFilePath(SlotName), false, true);
#endif
}

void USpudSubsystem::AddPersistentGlobalObject(UObject* Obj)
{
	GlobalObjects.AddUnique(TWeakObjectPtr(Obj));
}

void USpudSubsystem::AddPersistentGlobalObjectWithName(UObject* Obj, const FString& Name)
{
	NamedGlobalObjects.Add(Name, Obj);
}

void USpudSubsystem::RemovePersistentGlobalObject(UObject* Obj)
{
	GlobalObjects.Remove(TWeakObjectPtr(Obj));
	
	for (auto It = NamedGlobalObjects.CreateIterator(); It; ++It)
	{
		if (It.Value().Get() == Obj)
			It.RemoveCurrent();
	}
}

void USpudSubsystem::ClearLevelState(const FString& LevelName)
{
	GetActiveState()->ClearLevel(LevelName);
	
}

void USpudSubsystem::AddRequestForStreamingLevel(UObject* Requester, FName LevelName, bool BlockingLoad)
{
	if (!ServerCheck(false))
		return;

	auto && Request = LevelRequests.FindOrAdd(LevelName);
	const int PrevRequesters = Request.Requesters.Num();
	Request.Requesters.AddUnique(Requester);
	if (Request.bPendingUnload)
	{
		Request.bPendingUnload = false; // no load required, just flip the unload flag
		Request.LastRequestExpiredTime = 0;
	}
	else if (PrevRequesters == 0)
	{
		// Load on the first request only		
		LoadStreamLevel(LevelName, BlockingLoad);		
	}
}

void USpudSubsystem::WithdrawRequestForStreamingLevel(UObject* Requester, FName LevelName)
{
	if (!ServerCheck(false))
		return;

	if (auto Request = LevelRequests.Find(LevelName))
	{
		const int Removed = Request->Requesters.Remove(Requester);
		if (Removed > 0 && Request->Requesters.Num() == 0)
		{
			// This level can be unloaded after time delay
			Request->bPendingUnload = true;
			Request->LastRequestExpiredTime = UGameplayStatics::GetTimeSeconds(GetWorld());
			StartUnloadTimer();
		}
	}
}

void USpudSubsystem::StartUnloadTimer()
{
	if (!StreamLevelUnloadTimerHandle.IsValid())
	{
		// Set up a timer which repeatedly checks for actual unload
		// This doesn't need to be every tick, just every 0.5s
		GetWorld()->GetTimerManager().SetTimer(StreamLevelUnloadTimerHandle, this, &USpudSubsystem::CheckStreamUnload, 0.5, true);
	}	
}


void USpudSubsystem::StopUnloadTimer()
{
	if (StreamLevelUnloadTimerHandle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(StreamLevelUnloadTimerHandle);
	}	
}

void USpudSubsystem::CheckStreamUnload()
{
	const float UnloadBeforeTime = UGameplayStatics::GetTimeSeconds(GetWorld()) - StreamLevelUnloadDelay;
	bool bAnyStillWaiting = false;
	for (auto && Pair : LevelRequests)
	{
		const FName& LevelName = Pair.Key;
		FStreamLevelRequests& Request = Pair.Value;
		if (Request.bPendingUnload)
		{
			if (Request.Requesters.Num() == 0 &&
				Request.LastRequestExpiredTime <= UnloadBeforeTime)
			{
				Request.bPendingUnload = false;
				UnloadStreamLevel(LevelName);
			}
			else
				bAnyStillWaiting = true;
		}
	}

	// Only run the timer while we have something to do
	if (!bAnyStillWaiting)
		StopUnloadTimer();
}




void USpudSubsystem::LoadStreamLevel(FName LevelName, bool Blocking)
{
	FScopeLock PendingLoadLock(&LevelsPendingLoadMutex);
	PreLoadStreamingLevel.Broadcast(LevelName);
	
	FLatentActionInfo Latent;
	Latent.ExecutionFunction = "PostLoadStreamLevel";
	Latent.CallbackTarget = this;
	int32 RequestID = LoadUnloadRequests++; // overflow is OK
	Latent.UUID = RequestID; // this eliminates duplicate calls so should be unique
	Latent.Linkage = RequestID;
	LevelsPendingLoad.Add(RequestID, LevelName);

	// Upgrade to a blocking call if this is the first streaming level since map change (ensure appears in time)
	if (FirstStreamRequestSinceMapLoad)
	{
		Blocking = true;
		FirstStreamRequestSinceMapLoad = false;
	}

	// We don't make the level visible until the post-load callback
	UGameplayStatics::LoadStreamLevel(GetWorld(), LevelName, false, Blocking, Latent);
}

void USpudSubsystem::PostLoadStreamLevel(int32 LinkID)
{
	FScopeLock PendingLoadLock(&LevelsPendingLoadMutex);
	
	// We should be able to obtain the level name
	if (LevelsPendingLoad.Contains(LinkID))
	{
		FName LevelName = LevelsPendingLoad.FindAndRemoveChecked(LinkID);

		// This might look odd but for physics restoration to work properly we need a very specific
		// set of circumstances:
		// 1. Level must be made visible first
		// 2. We need to wait for all the objects to be ticked at least once
		// 3. Then we restore
		//
		// Failure to do this means SetPhysicsLinearVelocity etc just does *nothing* silently

		// Make visible
		auto StreamLevel = UGameplayStatics::GetStreamingLevel(GetWorld(), LevelName);
		if (StreamLevel)
		{
			StreamLevel->SetShouldBeVisible(true);
		}		

		if (!bSupportWorldPartition)
		{
			// When supporting WP, shown event will trigger this
			HandleLevelLoaded(LevelName);
		}
	}
	else
	{
		UE_LOG(LogSpudSubsystem, Error, TEXT("PostLoadStreamLevel called but not for a level we loaded??"));
	}
}


void USpudSubsystem::PostLoadStreamLevelGameThread(FName LevelName)
{
	PostLoadStreamingLevel.Broadcast(LevelName);
	auto StreamLevel = UGameplayStatics::GetStreamingLevel(GetWorld(), LevelName);

	if (StreamLevel)
	{
		ULevel* Level = StreamLevel->GetLoadedLevel();
		if (!Level)
		{
			UE_LOG(LogSpudSubsystem, Log, TEXT("PostLoadStreamLevel called for %s but level is null; probably unloaded again?"), *LevelName.ToString());
			return;
		}

		if (!ShouldStoreLevel(Level))
		{
			return;
		}

		IsRestoringState = true;

		PreLevelRestore.Broadcast(LevelName.ToString());
		// It's important to note that this streaming level won't be added to UWorld::Levels yet
		// This is usually where things like the TActorIterator get actors from, ULevel::Actors
		// we have the ULevel here right now, so restore it directly
		GetActiveState()->RestoreLevel(Level);

		// NB: after restoring the level, we could release MOST of the memory for this level
		// However, we don't for 2 reasons:
		// 1. Destroyed actors for this level are logged continuously while running, so that still needs to be active
		// 2. We can assume that we'll need to write data back to save when this level is unloaded. It's actually less
		//    memory thrashing to re-use the same memory we have until unload, since it'll likely be almost identical in structure
		StreamLevel->SetShouldBeVisible(true);
		SubscribeLevelObjectEvents(Level);
		PostLevelRestore.Broadcast(LevelName.ToString(), true);

		IsRestoringState = false;
	}
}

void USpudSubsystem::UnloadStreamLevel(FName LevelName)
{
	auto StreamLevel = UGameplayStatics::GetStreamingLevel(GetWorld(), LevelName);

	if (StreamLevel)
	{
		ULevel* Level = StreamLevel->GetLoadedLevel();
		if (!Level)
		{
			// Already unloaded
			return;
		}
		PreUnloadStreamingLevel.Broadcast(LevelName);

		if (!bSupportWorldPartition)
		{
			// If using WP, the hidden event will trigger this instead
			HandleLevelUnloaded(Level);
		}
		
		// Now unload
		FScopeLock PendingUnloadLock(&LevelsPendingUnloadMutex);

		FLatentActionInfo Latent;
		Latent.ExecutionFunction = "PostUnloadStreamLevel";
		Latent.CallbackTarget = this;
		int32 RequestID = LoadUnloadRequests++; // overflow is OK
		Latent.UUID = RequestID; // this eliminates duplicate calls so should be unique
		Latent.Linkage = RequestID;
		LevelsPendingUnload.Add(RequestID, LevelName);
		UGameplayStatics::UnloadStreamLevel(GetWorld(), LevelName, Latent, false);
	}	
}

void USpudSubsystem::ForceReset()
{
	CurrentState = ESpudSystemState::RunningIdle;
	IsRestoringState = false;
	bPendingEndGame = false;
	PendingNewGameArgs.Reset();
}

void USpudSubsystem::SetUserDataModelVersion(int32 Version)
{
	GCurrentUserDataModelVersion = Version;
}


int32 USpudSubsystem::GetUserDataModelVersion() const
{
	return GCurrentUserDataModelVersion;
}

void USpudSubsystem::PostUnloadStreamLevel(int32 LinkID)
{
	FScopeLock PendingUnloadLock(&LevelsPendingUnloadMutex);
	
	const FName LevelName = LevelsPendingUnload.FindAndRemoveChecked(LinkID);

	// Pass back to the game thread, streaming calls happen in loading thread?
	AsyncTask(ENamedThreads::GameThread, [this, LevelName]()
    {
        PostUnloadStreamLevelGameThread(LevelName);				
    });
}


void USpudSubsystem::PostUnloadStreamLevelGameThread(FName LevelName)
{
	PostUnloadStreamingLevel.Broadcast(LevelName);
}

bool USpudSubsystem::ShouldStoreLevel(const ULevel* Level)
{
	if (!Level)
		return false;
	
	const auto LevelName = USpudState::GetLevelName(Level);
	for (const auto& Pattern : ExcludeLevelNamePatterns)
	{
		if (LevelName.MatchesWildcard(Pattern))
		{
			return false;
		}
	}
	return true;
}

void USpudSubsystem::SetRenderTargetData(FString Name, UTextureRenderTarget2D* RenderTarget)
{
	UKismetRenderingLibrary::ExportRenderTarget(GetWorld(), RenderTarget, GetSaveGameDirectory(), Name + ".png");
}

UTexture2D* USpudSubsystem::GetRenderTargetData(FString Name)
{
	UTexture2D* texture = nullptr;
	FString TotalFileName = FPaths::Combine(GetSaveGameDirectory(), Name + ".png");
	texture = FImageUtils::ImportFileAsTexture2D(TotalFileName);
	return texture;
}

void USpudSubsystem::StoreActorByCell(AActor* Actor, const FString& CellName)
{
	if (!SpudPropertyUtil::IsPersistentObject(Actor))
		return;
	
	GetActiveState()->StoreActor(Actor, CellName);
}

void USpudSubsystem::SubscribeAllLevelObjectEvents()
{
	const auto World = GetWorld();
	if (IsValid(World))
	{
		for (ULevel* Level : World->GetLevels())
		{
			if (ShouldStoreLevel(Level))
			{
				SubscribeLevelObjectEvents(Level);
			}
		}
	}
}

void USpudSubsystem::UnsubscribeAllLevelObjectEvents()
{
	const auto World = GetWorld();
	if (IsValid(World))
	{
		for (ULevel* Level : World->GetLevels())
		{
			if (ShouldStoreLevel(Level))
			{
				UnsubscribeLevelObjectEvents(Level);
			}
		}
	}
}


void USpudSubsystem::SubscribeLevelObjectEvents(ULevel* Level)
{
	if (Level)
	{
		for (auto Actor : Level->Actors)
		{
			if (!SpudPropertyUtil::IsPersistentObject(Actor))
				continue;			
			// We don't care about runtime spawned actors, only level actors
			// Runtime actors will just be omitted, level actors need to be logged as destroyed
			if (!SpudPropertyUtil::IsRuntimeActor(Actor))
				Actor->OnDestroyed.AddUniqueDynamic(this, &USpudSubsystem::OnActorDestroyed);			
		}		
	}	
}

void USpudSubsystem::UnsubscribeLevelObjectEvents(ULevel* Level)
{
	if (Level)
	{
		for (auto Actor : Level->Actors)
		{
			if (!SpudPropertyUtil::IsPersistentObject(Actor))
				continue;

			if (!SpudPropertyUtil::IsRuntimeActor(Actor))
				Actor->OnDestroyed.RemoveDynamic(this, &USpudSubsystem::OnActorDestroyed);			
		}		
	}	
}

void USpudSubsystem::OnActorDestroyed(AActor* Actor)
{
	if (CurrentState == ESpudSystemState::RunningIdle)
	{
		auto Level = Actor->GetLevel();
		// Ignore actor destruction caused by levels being unloaded
		if (Level && !Level->bIsBeingRemoved)
		{
			auto State = GetActiveState();
			State->StoreLevelActorDestroyed(Actor);
		}
	}
}

struct FSaveSorter
{
	ESpudSaveSorting Sorting;
	
	FSaveSorter(ESpudSaveSorting S) : Sorting(S) {}
	
	FORCEINLINE bool operator()(const USpudSaveGameInfo& A, const USpudSaveGameInfo& B ) const
	{
		switch (Sorting)
		{
		default:
		case ESpudSaveSorting::None:
			return false;
		case ESpudSaveSorting::MostRecent:
			// Reverse ordering
			return A.Timestamp > B.Timestamp;
		case ESpudSaveSorting::SlotName:
			return A.SlotName.Compare(B.SlotName, ESearchCase::IgnoreCase) < 0;
		case ESpudSaveSorting::Title:
			return A.Title.CompareToCaseIgnored(B.Title) < 0;
		}
	}
};

TArray<USpudSaveGameInfo*> USpudSubsystem::GetSaveGameList(bool bIncludeQuickSave, bool bIncludeAutoSave, ESpudSaveSorting Sorting, const int32 UserIndex)
{

	TArray<FString> SaveFiles;
	ListSaveGameFiles(SaveFiles, UserIndex);

	TArray<USpudSaveGameInfo*> Ret;
	for (auto && File : SaveFiles)
	{
		FString SlotName = FPaths::GetBaseFilename(File);

		if ((!bIncludeQuickSave && SlotName == SPUD_QUICKSAVE_SLOTNAME) ||
			(!bIncludeAutoSave && SlotName == SPUD_AUTOSAVE_SLOTNAME))
		{
			continue;			
		}

		auto Info = GetSaveGameInfo(SlotName, UserIndex);
		if (Info)
			Ret.Add(Info);
	}

	if (Sorting != ESpudSaveSorting::None)
	{
		Ret.Sort(FSaveSorter(Sorting));
	}

	return Ret;
}

USpudSaveGameInfo* USpudSubsystem::GetSaveGameInfo(const FString& SlotName, const int32 UserIndex)
{

#ifdef USE_SAVEGAMESYSTEM
	
	// VIVI: Consoles require using the SaveGameSystem
	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();

	if (SaveSystem)
	{
		TArray<uint8> InSaveData;
		// Usually we'd want to parse just the very first part of the file, not all of it.
		// But the Save Game System has to give us the entire thing.
		if (SaveSystem->LoadGame(false, *SlotName, UserIndex, InSaveData))
		{
			auto Archive = FMemoryReader(InSaveData, true);

			if (Archive.IsError() || Archive.IsCriticalError())
			{
				UE_LOG(LogSpudSubsystem, Error, TEXT("Unable to open slot %s for reading info"), *SlotName);
				return nullptr;
			}
			

			auto Info = NewObject<USpudSaveGameInfo>();
			Info->SlotName = SlotName;

			const bool bResult = USpudState::LoadSaveInfoFromArchive(Archive, *Info);
			Archive.Close();

			return bResult ? Info : nullptr;
		}
		else
		{
			UE_LOG(LogSpudSubsystem, Error, TEXT("Unable to open slot %s for reading info"), *SlotName);
			return nullptr;
		}
	}
	else
	{
		UE_LOG(LogSpudSubsystem, Error, TEXT("Unable to open slot %s for reading info"), *SlotName);
		return nullptr;
	}

	
#else

	IFileManager& FM = IFileManager::Get();
	// We want to parse just the very first part of the file, not all of it
	FString AbsoluteFilename = FPaths::Combine(GetSaveGameDirectory(), SlotName + ".sav");
	auto Archive = TUniquePtr<FArchive>(FM.CreateFileReader(*AbsoluteFilename));

	if(!Archive)
	{
		UE_LOG(LogSpudSubsystem, Error, TEXT("Unable to open %s for reading info"), *AbsoluteFilename);
		return nullptr;
	}

	auto Info = NewObject<USpudSaveGameInfo>();
	Info->SlotName = SlotName;

	const bool bResult = USpudState::LoadSaveInfoFromArchive(*Archive, *Info);
	Archive->Close();

	return bResult ? Info : nullptr;
	
#endif
}

USpudSaveGameInfo* USpudSubsystem::GetLatestSaveGame(const int32 UserIndex)
{
	auto SaveGameList = GetSaveGameList(true, true, ESpudSaveSorting::None, UserIndex);
	USpudSaveGameInfo* Best = nullptr;
	for (auto Curr : SaveGameList)
	{
		if (!Best || Curr->Timestamp > Best->Timestamp)
			Best = Curr;		
	}
	return Best;
}


USpudSaveGameInfo* USpudSubsystem::GetQuickSaveGame(const int32 UserIndex)
{
	if (MaxQuickSaves > 0)
	{
		const auto LatestNumber = GetHighestSlotNumber(SPUD_QUICKSAVE_SLOTNAME,UserIndex);
		if (LatestNumber > 0)
		{
			return GetSaveGameInfo(MakeNumberedSlotName(SPUD_QUICKSAVE_SLOTNAME,LatestNumber), UserIndex);
		}
		
		if (LatestNumber == 0)
		{
			return GetSaveGameInfo(SPUD_QUICKSAVE_SLOTNAME, UserIndex);
		}
		
		return nullptr;
	}
	
	return GetSaveGameInfo(SPUD_QUICKSAVE_SLOTNAME, UserIndex);
}

USpudSaveGameInfo* USpudSubsystem::GetAutoSaveGame(const int32 UserIndex)
{
	if (MaxAutoSaves > 0)
	{
		const auto LatestNumber = GetHighestSlotNumber(SPUD_AUTOSAVE_SLOTNAME, UserIndex);
		if (LatestNumber > 0)
		{
			return GetSaveGameInfo(MakeNumberedSlotName(SPUD_AUTOSAVE_SLOTNAME, LatestNumber), UserIndex);
		}
		if (LatestNumber == 0)
		{
			return GetSaveGameInfo(SPUD_AUTOSAVE_SLOTNAME, UserIndex);
		}
		
		return nullptr;
	}
	
	return GetSaveGameInfo(SPUD_AUTOSAVE_SLOTNAME, UserIndex);
}

FString USpudSubsystem::GetSaveGameDirectory()
{
	return FString::Printf(TEXT("%sSaveGames/"), *FPaths::ProjectSavedDir());
}

FString USpudSubsystem::GetSaveGameFilePath(const FString& SlotName)
{
	return FString::Printf(TEXT("%s%s.sav"), *GetSaveGameDirectory(), *SlotName);
}

void USpudSubsystem::ListSaveGameFiles(TArray<FString>& OutSaveFileList, const int32 UserIndex)
{
#ifdef USE_SAVEGAMESYSTEM
	// VIVI: Consoles require using the SaveGameSystem
	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();

	if (SaveSystem)
	{
		SaveSystem->GetSaveGameNames(OutSaveFileList, UserIndex);
	}
#else
	IFileManager& FM = IFileManager::Get();

	FM.FindFiles(OutSaveFileList, *GetSaveGameDirectory(), TEXT(".sav"));
#endif
}

FString USpudSubsystem::GetActiveGameFolder()
{
	return FString::Printf(TEXT("%sCurrentGame/"), *FPaths::ProjectSavedDir());
}

FString USpudSubsystem::GetActiveGameFilePath(const FString& Name)
{
	return FString::Printf(TEXT("%sSaveGames/%s.sav"), *GetActiveGameFolder(), *Name);
}


class FUpgradeAllSavesAction : public FPendingLatentAction
{
public:
	FName ExecutionFunction;
	int32 OutputLink;
	FWeakObjectPtr CallbackTarget;

	struct FUpgradeTask : public FNonAbandonableTask
	{
		bool bUpgradeAlways;
		FSpudUpgradeSaveDelegate UpgradeCallback;
		int32 UserIndex;

		FUpgradeTask(bool InUpgradeAlways, FSpudUpgradeSaveDelegate InCallback, int32 InUserIndex) : bUpgradeAlways(InUpgradeAlways), UpgradeCallback(InCallback), UserIndex(InUserIndex) {}

		bool SaveNeedsUpgrading(const USpudState* State)
		{
			if (State->SaveData.GlobalData.IsUserDataModelOutdated())
				return true;

			for (auto& Pair : State->SaveData.LevelDataMap)
			{
				if (Pair.Value->IsUserDataModelOutdated())
					return true;				
			}

			return false;
		}

		void DoWork()
		{
			if (!UpgradeCallback.IsBound())
				return;

			TArray<FString> SaveFiles;
			USpudSubsystem::ListSaveGameFiles(SaveFiles, UserIndex);

#ifdef USE_SAVEGAMESYSTEM
			// VIVI: Consoles require using the SaveGameSystem
			ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();

			if (SaveSystem)
			{
				for (auto && SaveFile : SaveFiles)
				{
					TArray<uint8> InSaveData;
					if (SaveSystem->LoadGame(false, *SaveFile, UserIndex, InSaveData))
					{
						auto Archive = FMemoryReader(InSaveData, true);	

						auto State = NewObject<USpudState>();
						// Load all data because we want to upgrade
						State->LoadFromArchive(Archive, true);
						Archive.Close();

						if (Archive.IsError() || Archive.IsCriticalError())
						{
							UE_LOG(LogSpudSubsystem, Error, TEXT("Error while loading game to check for upgrades: %s"), *SaveFile);
							continue;
						}

						if (bUpgradeAlways || SaveNeedsUpgrading(State))
						{
							if (UpgradeCallback.Execute(State))
							{
								// VIVI: Do we really want to make a new "old" save?
								SaveSystem->SaveGame(false, *FString::Printf(TEXT("%s_Backup"), *SaveFile), UserIndex, InSaveData);
								
								// Now save
								TArray<uint8> OutSaveData;
								auto OutArchive = FMemoryWriter(OutSaveData, true);
								State->SaveToArchive(Archive);
								OutArchive.Close();
								
								if (OutSaveData.Num() > 0 && SaveFile.Len() > 0)
								{
									// VIVI: 0 = first player controller. Figure out if there's a better way to do this.
									if (!SaveSystem->SaveGame(false, *SaveFile, UserIndex, OutSaveData))
									{
										UE_LOG(LogSpudSubsystem, Error, TEXT("Error while upgrading save %s"), *SaveFile);
									}
								}
							}
						}
					}
				}
			}
#else
			IFileManager& FileMgr = IFileManager::Get();
			for (auto && SaveFile : SaveFiles)
			{
				FString AbsoluteFilename = FPaths::Combine(USpudSubsystem::GetSaveGameDirectory(), SaveFile);
				auto Archive = TUniquePtr<FArchive>(FileMgr.CreateFileReader(*AbsoluteFilename));

				if(Archive)
				{
					auto State = NewObject<USpudState>();
					// Load all data because we want to upgrade
					State->LoadFromArchive(*Archive, true);
					Archive->Close();

					if (Archive->IsError() || Archive->IsCriticalError())
					{
						UE_LOG(LogSpudSubsystem, Error, TEXT("Error while loading game to check for upgrades: %s"), *SaveFile);
						continue;
					}

					if (bUpgradeAlways || SaveNeedsUpgrading(State))
					{
						if (UpgradeCallback.Execute(State))
						{
							// Move aside old save
							FString BackupFilename = AbsoluteFilename + ".bak"; 
							FileMgr.Move(*BackupFilename, *AbsoluteFilename, true, true);
							// Now save
							auto OutArchive = TUniquePtr<FArchive>(FileMgr.CreateFileWriter(*AbsoluteFilename));
							if (OutArchive)
							{
								State->SaveToArchive(*OutArchive);
							}
						}
					}
				}
			}
#endif

		}

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FUpgradeTask, STATGROUP_ThreadPoolAsyncTasks);
		}
	
	};

	FAsyncTask<FUpgradeTask> UpgradeTask;

	FUpgradeAllSavesAction(bool UpgradeAlways, FSpudUpgradeSaveDelegate InUpgradeCallback, const FLatentActionInfo& LatentInfo, const int32 UserIndex)
        : ExecutionFunction(LatentInfo.ExecutionFunction)
        , OutputLink(LatentInfo.Linkage)
        , CallbackTarget(LatentInfo.CallbackTarget)
        , UpgradeTask(UpgradeAlways, InUpgradeCallback, UserIndex)
	{
		// We do the actual upgrade work in a background task, this action is just to monitor when it's done
		UpgradeTask.StartBackgroundTask();
	}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		// This is essentially a game thread tick. Finish the latent action when the background task is done
		Response.FinishAndTriggerIf(UpgradeTask.IsDone(), ExecutionFunction, OutputLink, CallbackTarget);
	}

#if WITH_EDITOR
	virtual FString GetDescription() const override
	{
		return "Upgrade All Saves";
	}
#endif
};


void USpudSubsystem::UpgradeAllSaveGames(bool bUpgradeEvenIfNoUserDataModelVersionDifferences,
                                         FSpudUpgradeSaveDelegate SaveNeedsUpgradingCallback,
                                         FLatentActionInfo LatentInfo,
                                         const int32 UserIndex)
{
	
	FLatentActionManager& LatentActionManager = GetGameInstance()->GetLatentActionManager();
	if (LatentActionManager.FindExistingAction<FUpgradeAllSavesAction>(LatentInfo.CallbackTarget, LatentInfo.UUID) == nullptr)
	{
		LatentActionManager.AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID,
		                                 new FUpgradeAllSavesAction(bUpgradeEvenIfNoUserDataModelVersionDifferences,
		                                                            SaveNeedsUpgradingCallback, LatentInfo, UserIndex));
	}
}

USpudCustomSaveInfo* USpudSubsystem::CreateCustomSaveInfo()
{
	return NewObject<USpudCustomSaveInfo>();
}



// FTickableGameObject begin


void USpudSubsystem::Tick(float DeltaTime)
{
	if (bSupportWorldPartition)
	{
		auto world = GetWorld();
		// only for authority.
		if (world && world->GetAuthGameMode())
		{
			const auto& streamingLevels = world->GetStreamingLevels();

			// Find newly added levels.
			for (const auto level : streamingLevels)
			{
				if (!MonitoredStreamingLevels.Contains(level))
				{
					UE_LOG(LogSpudSubsystem, Verbose, TEXT("Loaded streaming level: %s"), *GetNameSafe(level));
					auto wrapper = NewObject<USpudStreamingLevelWrapper>(world);
					wrapper->LevelStreaming = level;
					MonitoredStreamingLevels.Add(level, wrapper);
					level->OnLevelShown.AddUniqueDynamic(wrapper, &USpudStreamingLevelWrapper::OnLevelShown);
					level->OnLevelHidden.AddUniqueDynamic(wrapper, &USpudStreamingLevelWrapper::OnLevelHidden);
					if (level->IsLevelVisible())
						wrapper->OnLevelShown();
				}
			}

			// Discard unloaded levels.
			for (auto it = MonitoredStreamingLevels.CreateIterator(); it; ++it)
			{
				if (const auto Level = it.Key(); Level && !streamingLevels.Contains(Level))
				{
					UE_LOG(LogSpudSubsystem, Verbose, TEXT("Unloaded streaming level: %s"), *GetNameSafe(Level));
					check(!Level->IsLevelVisible());
					Level->OnLevelShown.RemoveAll(it.Value());
					Level->OnLevelHidden.RemoveAll(it.Value());
					it.RemoveCurrent();

					PostUnloadStreamingLevel.Broadcast(FName(USpudState::GetLevelName(Level->GetWorldAssetPackageName())));
				}
			}
		}
	}
}

ETickableTickType USpudSubsystem::GetTickableTickType() const
{
	// This is for timeout purposes
	return ETickableTickType::Always;
}

bool USpudSubsystem::IsTickableWhenPaused() const
{
	// We need the screenshot failsafe timeout even when paused
	return true;
}

TStatId USpudSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USpudSubsystem, STATGROUP_Tickables);
}


// FTickableGameObject end

void USpudStreamingLevelWrapper::OnLevelShown()
{
	const auto level = LevelStreaming->GetLoadedLevel();
	if (level)
	{
		UE_LOG(LogSpudSubsystem, Verbose, TEXT("Level shown: %s"), *USpudState::GetLevelName(level));
		
		auto spud = UGameInstance::GetSubsystem<USpudSubsystem>(GetWorld()->GetGameInstance());
		if (ensureMsgf(spud, TEXT("Unable to find SpudSubsystem, so cannot load the state of level: %s"), *USpudState::GetLevelName(level)))
		{
			spud->HandleLevelLoaded(level);
		}
	}
	else
		UE_LOG(LogSpudSubsystem, Verbose, TEXT("No loaded level"));
}

void USpudStreamingLevelWrapper::OnLevelHidden()
{
	const auto level = LevelStreaming->GetLoadedLevel();
	if (level)
	{
		const auto levelName = USpudState::GetLevelName(level);
		UE_LOG(LogSpudSubsystem, Verbose, TEXT("Level hidden: %s"), *levelName);
		
		// We no longer crash, but we still need to know when this happens, as we really should be
		// storing the state of the unloaded level
		auto spud = UGameInstance::GetSubsystem<USpudSubsystem>(GetWorld()->GetGameInstance());
		if (ensureMsgf(spud, TEXT("Unable to find SpudSubsystem, so cannot save the state of level: %s"), *levelName))
		{
			spud->PreUnloadStreamingLevel.Broadcast(FName(levelName));
			spud->HandleLevelUnloaded(level);
		}
	}
	else
		UE_LOG(LogSpudSubsystem, Verbose, TEXT("No loaded level"));
}
