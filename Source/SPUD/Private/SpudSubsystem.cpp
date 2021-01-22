#include "SpudSubsystem.h"
#include "EngineUtils.h"
#include "SpudGameState.h"
#include "Engine/LevelStreaming.h"
#include "Kismet/GameplayStatics.h"

#if WITH_EDITOR
#include "Editor.h"
#endif


DEFINE_LOG_CATEGORY(LogSpudSubsystem)


#define SPUD_QUICKSAVE_SLOTNAME "__QuickSave__"
#define SPUD_AUTOSAVE_SLOTNAME "__AutoSave__"


void USpudSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	OnPostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &USpudSubsystem::OnPostLoadMap);
	OnPreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &USpudSubsystem::OnPreLoadMap);

#if WITH_EDITORONLY_DATA
	// The one problem we have is that in PIE mode, PostLoadMap doesn't get fired for the current map you're on
	// So we'll need to trigger it manually
	auto World = GetWorld();
	if (World && World->WorldType == EWorldType::PIE)
	{
		// TODO: make this more configurable, use a known save etc
		NewGame();
	}
	
#endif
}

void USpudSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(OnPostLoadMapHandle);
	FCoreUObjectDelegates::PreLoadMap.Remove(OnPreLoadMapHandle);
}


void USpudSubsystem::NewGame()
{
	EndGame();
	CurrentState = ESpudSystemState::RunningIdle;
	SubscribeAllLevelObjectEvents();
}


void USpudSubsystem::EndGame()
{
	// Allow GC to collect
	ActiveState = nullptr;

	UnsubscribeAllLevelObjectEvents();
	CurrentState = ESpudSystemState::Disabled;
}

void USpudSubsystem::AutoSaveGame()
{
	SaveGame(SPUD_AUTOSAVE_SLOTNAME, NSLOCTEXT("Spud", "AutoSaveTitle", "Autosave"));
}

void USpudSubsystem::QuickSaveGame()
{
	SaveGame(SPUD_QUICKSAVE_SLOTNAME, NSLOCTEXT("Spud", "QuickSaveTitle", "Quick Save"));
}

void USpudSubsystem::QuickLoadGame()
{
	LoadGame(SPUD_QUICKSAVE_SLOTNAME);
}

void USpudSubsystem::LoadLatestSaveGame()
{
	auto Latest = GetLatestSaveGame();
	if (Latest)
		LoadGame(Latest->SlotName);
}

void USpudSubsystem::OnPreLoadMap(const FString& MapName)
{
	LevelRequesters.Empty();
	FirstStreamRequestSinceMapLoad = true;

	// When we transition out of a map while enabled, save contents
	if (CurrentState == ESpudSystemState::RunningIdle)
	{
		UnsubscribeAllLevelObjectEvents();

		const auto World = GetWorld();
		if (IsValid(World))
		{
			UE_LOG(LogSpudSubsystem, Verbose, TEXT("OnPreLoadMap saving: %s"), *UGameplayStatics::GetCurrentLevelName(World));

			auto State = GetActiveState();
			State->UpdateFromWorld(World);
		}
	}
}
void USpudSubsystem::OnPostLoadMap(UWorld* World)
{
	if (CurrentState == ESpudSystemState::RunningIdle ||
		CurrentState == ESpudSystemState::LoadingGame)
	{
		// This is called when a new map is loaded
		// In all cases, we try to load the state
		if (IsValid(World)) // nullptr seems possible if load is aborted or something?
		{
			UE_LOG(LogSpudSubsystem, Verbose, TEXT("OnPostLoadMap restore: %s"),
			       *UGameplayStatics::GetCurrentLevelName(World));

			auto State = GetActiveState();
			State->RestoreLoadedWorld(World);

			SubscribeLevelObjectEvents(World->GetCurrentLevel());
		}

		// If we were loading, this is the completion
		if (CurrentState == ESpudSystemState::LoadingGame)
		{
			CurrentState = ESpudSystemState::RunningIdle;
			UE_LOG(LogSpudSubsystem, Log, TEXT("Load: Success"));

			// TODO: load completion callback
		}
	}
}

bool USpudSubsystem::SaveGame(const FString& SlotName, const FText& Title /* = "" */)
{
	if (CurrentState != ESpudSystemState::RunningIdle)
	{
		// TODO: ignore or queue?
		UE_LOG(LogSpudSubsystem, Fatal, TEXT("TODO: Overlapping calls to save/load, resolve this"));
		return false;
	}

	CurrentState = ESpudSystemState::SavingGame;

	auto State = GetActiveState();

	// We do NOT reset
	// a) deleted objects must remain, they're built up over time
	// b) we may not be updating all levels and must retain for the others

	for (auto Ptr : GlobalObjects)
	{
		if (Ptr.IsValid())
			State->UpdateFromGlobalObject(Ptr.Get());
	}
	for (auto Pair : NamedGlobalObjects)
	{
		if (Pair.Value.IsValid())
			State->UpdateFromGlobalObject(Pair.Value.Get(), Pair.Key);
	}
	
	// TODO: what this should REALLY do is only write data for globals and the currently loaded levels
	// Then it should combine this data with other level data which isn't loaded right now, into a single
	// save file. Ideally without loading all the other stuff into memory.
	State->UpdateFromWorld(GetWorld());

	// UGameplayStatics::SaveGameToSlot prefixes our save with a lot of crap that we don't need
	// And also wraps it with FObjectAndNameAsStringProxyArchive, which again we don't need
	// Plus it writes it all to memory first, which we don't need another copy of. Write direct to file
	// I'm not sure if the save game system doesn't do this because of some console hardware issues, but
	// I'll worry about that at some later point
	IFileManager& FileMgr = IFileManager::Get();
	auto Archive = TUniquePtr<FArchive>(FileMgr.CreateFileWriter(*GetSaveGameFilePath(SlotName)));

	bool SaveOK = true;
	if(Archive)
	{
		State->SaveToArchive(*Archive, Title);
		// Always explicitly close to catch errors from flush/close
		Archive->Close();

		if (Archive->IsError() || Archive->IsCriticalError())
		{
			UE_LOG(LogSpudSubsystem, Error, TEXT("Error while saving game to %s"), *SlotName);
			SaveOK = false;
		}
		else
		{
			UE_LOG(LogSpudSubsystem, Log, TEXT("Save to slot %s: Success"), *SlotName);
			SaveOK = false;
		}
	}
	else
	{
		UE_LOG(LogSpudSubsystem, Error, TEXT("Error while creating save game for slot %s"), *SlotName);
		SaveOK = false;
	}
	
	UE_LOG(LogSpudSubsystem, Log, TEXT("Save %s: Success"), *SlotName);

	CurrentState = ESpudSystemState::RunningIdle;

	return SaveOK;
}

bool USpudSubsystem::LoadGame(const FString& SlotName)
{
	if (CurrentState != ESpudSystemState::RunningIdle)
	{
		// TODO: ignore or queue?
		UE_LOG(LogSpudSubsystem, Fatal, TEXT("TODO: Overlapping calls to save/load, resolve this"));
		return false;
	}

	CurrentState = ESpudSystemState::LoadingGame;

	auto State = GetActiveState();

	State->ResetState();

	// TODO: async load
	// TODO: split file and load only the data we need

	IFileManager& FileMgr = IFileManager::Get();
	auto Archive = TUniquePtr<FArchive>(FileMgr.CreateFileReader(*GetSaveGameFilePath(SlotName)));

	if(Archive)
	{
		State->LoadFromArchive(*Archive);
		Archive->Close();

		if (Archive->IsError() || Archive->IsCriticalError())
		{
			UE_LOG(LogSpudSubsystem, Error, TEXT("Error while loading game from %s"), *SlotName);
			CurrentState = ESpudSystemState::RunningIdle;
			return false;
		}
	}
	else
	{
		UE_LOG(LogSpudSubsystem, Error, TEXT("Error while opening save game for slot %s"), *SlotName);		
		CurrentState = ESpudSystemState::RunningIdle;
		return false;
	}

	// Just do the reverse of what we did
	// Global objects first before map, these should be only objects which survive map load
	for (auto Ptr : GlobalObjects)
	{
		if (Ptr.IsValid())
			State->RestoreGlobalObject(Ptr.Get());
	}
	for (auto Pair : NamedGlobalObjects)
	{
		if (Pair.Value.IsValid())
			State->RestoreGlobalObject(Pair.Value.Get(), Pair.Key);
	}

	// This is deferred, final load process will happen in PostLoadMap
	UGameplayStatics::OpenLevel(GetWorld(), FName(State->GetPersistentLevel()));

	return true;
}

bool USpudSubsystem::DeleteSave(const FString& SlotName)
{
	IFileManager& FileMgr = IFileManager::Get();
	return FileMgr.Delete(*GetSaveGameFilePath(SlotName), false, true);
}

void USpudSubsystem::AddPersistentGlobalObject(UObject* Obj)
{
	GlobalObjects.AddUnique(TWeakObjectPtr<UObject>(Obj));	
}

void USpudSubsystem::AddPersistentGlobalObjectWithName(UObject* Obj, const FString& Name)
{
	NamedGlobalObjects.Add(Name, Obj);
}

void USpudSubsystem::RemovePersistentGlobalObject(UObject* Obj)
{
	GlobalObjects.Remove(TWeakObjectPtr<UObject>(Obj));
	
	for (auto It = NamedGlobalObjects.CreateIterator(); It; ++It)
	{
		if (It.Value().Get() == Obj)
			It.RemoveCurrent();
	}
}

void USpudSubsystem::AddRequestForStreamingLevel(UObject* Requester, FName LevelName, bool BlockingLoad)
{
	auto && Requesters = LevelRequesters.FindOrAdd(LevelName);
	Requesters.AddUnique(Requester);
	// Load on the first request only
	if (Requesters.Num() == 1)
		LoadStreamLevel(LevelName, BlockingLoad);
}

void USpudSubsystem::WithdrawRequestForStreamingLevel(UObject* Requester, FName LevelName)
{
	if (auto Requesters = LevelRequesters.Find(LevelName))
	{
		Requesters->Remove(Requester);
		if (Requesters->Num() == 0)
		{
			// This level can be unloaded
			// TODO: do it deferred in case only temporary
			UnloadStreamLevel(LevelName);
		}
	}
}



void USpudSubsystem::LoadStreamLevel(FName LevelName, bool Blocking)
{
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
	// We should be able to obtain the level name
	if (LevelsPendingLoad.Contains(LinkID))
	{
		FName LevelName = LevelsPendingLoad.FindAndRemoveChecked(LinkID);
		auto StreamLevel = UGameplayStatics::GetStreamingLevel(GetWorld(), LevelName);

		if (StreamLevel)
		{
			// It's important to note that this streaming level won't be added to UWorld::Levels yet
			// This is usually where things like the TActorIterator get actors from, ULevel::Actors
			// we have the ULevel here right now, so restore it directly
			GetActiveState()->RestoreLevel(StreamLevel->GetLoadedLevel());			
			StreamLevel->SetShouldBeVisible(true);
			SubscribeLevelObjectEvents(StreamLevel->GetLoadedLevel());
		}
	}
}


void USpudSubsystem::UnloadStreamLevel(FName LevelName)
{
	auto StreamLevel = UGameplayStatics::GetStreamingLevel(GetWorld(), LevelName);

	if (StreamLevel)
		UnsubscribeLevelObjectEvents(StreamLevel->GetLoadedLevel());
	
	if (CurrentState != ESpudSystemState::LoadingGame)
	{
		// save the state, if not loading game
		// when loading game we will unload the current level and streaming and don't want to restore the active state from that
		GetActiveState()->UpdateFromLevel(GetWorld(), LevelName.ToString());
	}
	
	// Now unload
	FLatentActionInfo Latent;
	Latent.ExecutionFunction = "PostUnloadStreamLevel";
	Latent.CallbackTarget = this;
	int32 RequestID = LoadUnloadRequests++; // overflow is OK
	Latent.UUID = RequestID; // this eliminates duplicate calls so should be unique
	UGameplayStatics::UnloadStreamLevel(GetWorld(), LevelName, Latent, false);
}

void USpudSubsystem::ForceReset()
{
	CurrentState = ESpudSystemState::RunningIdle;
}

void USpudSubsystem::PostUnloadStreamLevel(int32 LinkID)
{
	// Nothing to do here right now
}

void USpudSubsystem::SubscribeAllLevelObjectEvents()
{
	const auto World = GetWorld();
	if (IsValid(World))
	{
		for (ULevel* Level : World->GetLevels())
		{
			SubscribeLevelObjectEvents(Level);			
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
			UnsubscribeLevelObjectEvents(Level);			
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
				Actor->OnDestroyed.AddDynamic(this, &USpudSubsystem::OnActorDestroyed);			
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
			State->UpdateLevelActorDestroyed(Actor);
		}
	}
}


void USpudSubsystem::RefreshSaveGameList()
{
	IFileManager& FM = IFileManager::Get();

	TArray<FString> SaveFiles;
	FM.FindFiles(SaveFiles, *GetSaveGameDirectory(), TEXT(".sav"));

	SaveGameList.Empty();
	for (auto && File : SaveFiles)
	{
		// We want to parse just the very first part of the file, not all of it
		FString AbsoluteFilename = FPaths::Combine(GetSaveGameDirectory(), File);
		auto Archive = TUniquePtr<FArchive>(FM.CreateFileReader(*AbsoluteFilename));

		if(!Archive)
		{
			UE_LOG(LogSpudSubsystem, Error, TEXT("Unable to open %s for reading info"), *File);
			continue;
		}
		
		auto Info = NewObject<USpudSaveGameInfo>();
		Info->SlotName = FPaths::GetBaseFilename(File);

		USpudGameState::LoadSaveInfoFromArchive(*Archive, *Info);
		Archive->Close();
		
		SaveGameList.Add(Info);		
		
	}
}

const TArray<USpudSaveGameInfo*>& USpudSubsystem::GetSaveGameList()
{
	RefreshSaveGameList();
	return SaveGameList;
}

USpudSaveGameInfo* USpudSubsystem::GetLatestSaveGame()
{
	RefreshSaveGameList();
	USpudSaveGameInfo* Best = nullptr;
	for (auto Curr : SaveGameList)
	{
		if (!Best || Curr->Timestamp > Best->Timestamp)
			Best = Curr;		
	}
	return Best;
}


FString USpudSubsystem::GetSaveGameDirectory()
{
	return FString::Printf(TEXT("%sSaveGames/"), *FPaths::ProjectSavedDir());
}

FString USpudSubsystem::GetSaveGameFilePath(const FString& SlotName)
{
	return FString::Printf(TEXT("%s%s.sav"), *GetSaveGameDirectory(), *SlotName);
}

FString USpudSubsystem::GetActiveGameFolder()
{
	return FString::Printf(TEXT("%sCurrentGame/"), *FPaths::ProjectSavedDir());
}

FString USpudSubsystem::GetActiveGameFilePath(const FString& Name)
{
	return FString::Printf(TEXT("%sSaveGames/%s.sav"), *GetActiveGameFolder(), *Name);
}
