#include "SpudState.h"

#include "ISpudObject.h"
#include "SpudPropertyUtil.h"
#include "SpudSubsystem.h"
#include "Engine/LevelStreaming.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/MovementComponent.h"
#include "ImageUtils.h"
#include "../Public/SpudMemoryReaderWriter.h"
#include "GameFramework/PlayerState.h"

DEFINE_LOG_CATEGORY(LogSpudState)

USpudState::USpudState()
{
	// In case game crashed etc, remove all garbage active level files at construction too
	RemoveAllActiveGameLevelFiles();
}

void USpudState::ResetState()
{
	RemoveAllActiveGameLevelFiles();
	SaveData.Reset();
}

void USpudState::StoreWorldGlobals(UWorld* World)
{
	if (UPackage* Package = World->GetPackage())
	{
		SaveData.GlobalData.CurrentLevel = Package->GetLoadedPath().GetPackageFName().ToString();
	}
	else
	{
		SaveData.GlobalData.CurrentLevel = World->GetFName().ToString();
	}
}


void USpudState::StoreLevel(ULevel* Level, bool bReleaseAfter, bool bBlocking)
{
	const FString LevelName = GetLevelName(Level);
	auto LevelData = GetLevelData(LevelName, true);

	if (LevelData.IsValid())
	{
		// Mutex lock the level (load and unload events on streaming can be in loading threads)
		FScopeLock LevelLock(&LevelData->Mutex);

		// Clear any existing data for levels being updated from
		// Which is either the specific level, or all loaded levels
		if (LevelData)
			LevelData->PreStoreWorld();

		for (auto Actor : Level->Actors)
		{
			if (SpudPropertyUtil::IsPersistentObject(Actor))
			{
				StoreActor(Actor, LevelData);
			}
		}

		// ReSharper disable once CppExpressionWithoutSideEffects
		OnLevelStore.ExecuteIfBound(LevelName);
	}

	if (bReleaseAfter)
		ReleaseLevelData(LevelName, bBlocking);
}

USpudState::StorePropertyVisitor::StorePropertyVisitor(
	USpudState* Parent,
	TSharedPtr<FSpudClassDef> InClassDef, TArray<uint32>& InPropertyOffsets,
	FSpudClassMetadata& InMeta, FSpudMemoryWriter& InOut):
	ParentState(Parent),
	ClassDef(InClassDef),
	PropertyOffsets(InPropertyOffsets),
	Meta(InMeta),
	Out(InOut)
{
}

bool USpudState::StorePropertyVisitor::VisitProperty(UObject* RootObject, FProperty* Property,
                                                                    uint32 CurrentPrefixID, void* ContainerPtr,
                                                                    int Depth)
{
	SpudPropertyUtil::StoreProperty(RootObject, Property, CurrentPrefixID, ContainerPtr, Depth, ClassDef, PropertyOffsets, Meta, Out);

	StoreNestedUObjectIfNeeded(RootObject, Property, CurrentPrefixID, ContainerPtr, Depth);
	
	return true;
}


void USpudState::StorePropertyVisitor::StoreNestedUObjectIfNeeded(UObject* RootObject, FProperty* Property,
	uint32 CurrentPrefixID, void* ContainerPtr, int Depth)
{
	// Special case nested UObjects - we cascade if not null, but based on the runtime type (this is why visitor does not cascade,
	// since it only has the static type and in the case of nulls wouldn't know what to do)
	if (SpudPropertyUtil::IsNestedUObjectProperty(Property))
	{
		if (const auto OProp = CastField<FObjectProperty>(Property))
		{
			const void* DataPtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
			const auto Obj = OProp->GetObjectPropertyValue(DataPtr);

			if (Obj)
			{
				if (Obj->IsAsset())
				{
					constexpr auto Format = ESpudObjectStoreFormat::AssetPath;
					SpudPropertyUtil::WriteRaw(Format, Out);

					// Storing asset links is potentially dangerous - their path should not change across save/load
					// cycles; use at your own risk
					const auto Path = FTopLevelAssetPath(Obj);
					check(Path.IsValid());

					UE_LOG(LogSpudState, Verbose, TEXT("Storing asset link for %s: %s"), *Property->GetNameCPP(), *Obj->GetName());

					SpudPropertyUtil::WriteRaw(Path, Out);
				}
				else
				{
					constexpr auto Format = ESpudObjectStoreFormat::NestedProperties;
					SpudPropertyUtil::WriteRaw(Format, Out);

					const bool IsCallback = Obj->GetClass()->ImplementsInterface(USpudObjectCallback::StaticClass());

					if (IsCallback)
					{
						ISpudObjectCallback::Execute_SpudPreStore(Obj, ParentState);
					}

					TArray<uint32> ObjectPropertyOffsets;
					TArray<uint8> ObjectData;
					FSpudMemoryWriter ObjectOut(ObjectData);
					const uint32 NewPrefixID = GetNestedPrefix(Property, CurrentPrefixID);
					ParentState->StoreObjectProperties(Obj, NewPrefixID, ObjectPropertyOffsets, Meta, ObjectOut, Depth+1);
					Out << ObjectPropertyOffsets;
					Out << ObjectData;

					if (IsCallback)
					{
						// No custom data callbacks for nested UObjects, only root ones
						// This is because nested UObjects don't get their own data package, and could be null sometimes etc,
						// could interfere with data packing in nasty ways
						// I *could* store UObjects in their own data wrappers but that becomes cumbersome so don't for now
						ISpudObjectCallback::Execute_SpudPostStore(Obj, ParentState);
					}
				}
			}
		}	
	}
}

void USpudState::StorePropertyVisitor::UnsupportedProperty(UObject* RootObject,
                                                           FProperty* Property, uint32 CurrentPrefixID, int Depth)
{
	UE_LOG(LogSpudState, Error, TEXT("Property %s/%s is marked for save but is an unsupported type, ignoring. "),
        *RootObject->GetName(), *Property->GetName());
	
}

uint32 USpudState::StorePropertyVisitor::GetNestedPrefix(
	FProperty* Prop, uint32 CurrentPrefixID)
{
	// When updating we generate new prefix IDs as needed
	return SpudPropertyUtil::FindOrAddNestedPrefixID(CurrentPrefixID, Prop, Meta);
}


void USpudState::WriteCoreActorData(AActor* Actor, FArchive& Out) const
{
	// Save core information which isn't in properties
	// We write this as packed data

	// Version: this needs to be incremented if any changes
	constexpr uint16 CoreDataVersion = 1;

	// Current Format:
	// - Version (uint16)
	// - Hidden (bool)
	// - Transform (FTransform)
	// - Velocity (FVector)
	// - AngularVelocity (FVector)
	// - Control rotation (FRotator) (non-zero for Pawns only)

	// We could omit some of this data for non-movables but it's simpler to include for all

	SpudPropertyUtil::WriteRaw(CoreDataVersion, Out);

	SpudPropertyUtil::WriteRaw(Actor->IsHidden(), Out);
	SpudPropertyUtil::WriteRaw(Actor->GetTransform(), Out);
	
	FVector Velocity = FVector::ZeroVector;
	FVector AngularVelocity = FVector::ZeroVector;
	FRotator ControlRotation = FRotator::ZeroRotator;

	const auto RootComp = Actor->GetRootComponent();
	if (RootComp && RootComp->Mobility == EComponentMobility::Movable)
	{
		const auto PrimComp = Cast<UPrimitiveComponent>(RootComp);
		if (PrimComp && PrimComp->IsSimulatingPhysics())
		{
			Velocity = Actor->GetVelocity();
			AngularVelocity = PrimComp->GetPhysicsAngularVelocityInDegrees();
		}
		else if (const auto	MoveComponent = Cast<UMovementComponent>(Actor->FindComponentByClass(UMovementComponent::StaticClass())))
		{
			Velocity = MoveComponent->Velocity;
		}
	}

	
	if (const auto Pawn = Cast<APawn>(Actor))
	{
		ControlRotation = Pawn->GetControlRotation();
	}
	SpudPropertyUtil::WriteRaw(Velocity, Out);
	SpudPropertyUtil::WriteRaw(AngularVelocity, Out);
	SpudPropertyUtil::WriteRaw(ControlRotation, Out);

}

FString USpudState::GetLevelName(const FString& PackageName)
{
	// Detect what level an object originated from
	// GetLevel()->GetName / GetFName() returns "PersistentLevel" all the time
	// GetLevel()->GetPathName returns e.g. /Game/Maps/[UEDPIE_0_]TestAdventureMap.TestAdventureMap:PersistentLevel
	// Outer is "PersistentLevel"
	// Outermost is "/Game/Maps/[UEDPIE_0_]TestAdventureStream0" so that's what we want
	// Note that using Actor->GetOutermost() with WorldPartition will return some wrapper object.
	FString LevelName;
	PackageName.Split("/", nullptr, &LevelName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	// Strip off PIE prefix, "UEDPIE_N_" where N is a number
	if (LevelName.StartsWith("UEDPIE_"))
		LevelName = LevelName.Right(LevelName.Len() - 9);
	return LevelName;
}

FString USpudState::GetLevelName(const ULevel* Level)
{
	if (const auto OuterMost = Level->GetOutermost())
	{
		return GetLevelName(OuterMost->GetName());
	}

	return FString();
}

FString USpudState::GetLevelNameForActor(const AActor* Actor)
{
	return GetLevelName(Actor->GetLevel());
}

FSpudSaveData::TLevelDataPtr USpudState::GetLevelData(const FString& LevelName, bool AutoCreate)
{
	auto Ret = SaveData.GetLevelData(LevelName, true, GetActiveGameLevelFolder());
	
	if (!Ret.IsValid() && AutoCreate)
	{
		Ret = SaveData.CreateLevelData(LevelName);
	}
	
	return Ret;
}


void USpudState::ReleaseLevelData(const FString& LevelName, bool bBlocking)
{
	SaveData.WriteAndReleaseLevelData(LevelName, GetActiveGameLevelFolder(), bBlocking);
}


void USpudState::ReleaseAllLevelData()
{
	SaveData.WriteAndReleaseAllLevelData(GetActiveGameLevelFolder());
}

FSpudNamedObjectData* USpudState::GetLevelActorData(const AActor* Actor, FSpudSaveData::TLevelDataPtr LevelData, bool AutoCreate)
{
	// FNames are constant within a level
	const auto Name = SpudPropertyUtil::GetLevelActorName(Actor);
	FSpudNamedObjectData* Ret = LevelData->LevelActors.Contents.Find(Name);

	if (!Ret && AutoCreate)
	{
		Ret = &LevelData->LevelActors.Contents.Add(Name);
		Ret->Name = Name;
	}
	
	return Ret;
}

FSpudSpawnedActorData* USpudState::GetSpawnedActorData(AActor* Actor, FSpudSaveData::TLevelDataPtr LevelData, bool AutoCreate)
{
	// For automatically spawned singleton objects such as GameModes, Pawns you should create a SpudGuid
	// property which you generate statically (not at construction), e.g. in the BP default value.
	// This way we can update its values and not have to re-spawn it.
	// Actually dynamically spawned items can be re-spawned if not there.
	
	// We need a GUID for runtime spawned actors
	FGuid Guid = SpudPropertyUtil::GetGuidProperty(Actor);
	bool GuidOk = Guid.IsValid();
	if (!GuidOk && AutoCreate)
	{
		// Create a new Guid to save data with
		// Provided there's a property to save it in
		Guid = FGuid::NewGuid();
		GuidOk = SpudPropertyUtil::SetGuidProperty(Actor, Guid);
	}
	
	if (!GuidOk)
	{
		UE_LOG(LogSpudState, Error, TEXT("Ignoring runtime actor %s, missing or blank SpudGuid property"), *Actor->GetName())
		UE_LOG(LogSpudState, Error, TEXT("  Runtime spawned actors should have a SpudGuid property to identify them, initialised to valid unique value."))
		UE_LOG(LogSpudState, Error, TEXT("  NOTE: If this actor is part of a level and not runtime spawned, the cause of this false detection might be that you haven't SAVED the level before playing in the editor."))
		return nullptr;			
	}
	
	const auto GuidStr = Guid.ToString(SPUDDATA_GUID_KEY_FORMAT);
	FSpudSpawnedActorData* Ret = LevelData->SpawnedActors.Contents.Find(GuidStr);
	if (!Ret && AutoCreate)
	{
		Ret = &LevelData->SpawnedActors.Contents.Emplace(GuidStr);
		Ret->Guid = Guid;
		const FString ClassName = SpudPropertyUtil::GetClassName(Actor); 
		Ret->ClassID = LevelData->Metadata.FindOrAddClassIDFromName(ClassName);
	}
	
	return Ret;
}

void USpudState::StoreActor(AActor* Actor)
{
	if (Actor->HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject|RF_BeginDestroyed))
		return;

	const FString LevelName = GetLevelNameForActor(Actor);

	auto LevelData = GetLevelData(LevelName, true);
	StoreActor(Actor, LevelData);
		
}

void USpudState::StoreActor(AActor* Actor, const FString& CellName)
{
	if (Actor->HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject|RF_BeginDestroyed))
		return;

	const auto LevelData = GetLevelData(CellName, true);
	StoreActor(Actor, LevelData);
}

void USpudState::StoreLevelActorDestroyed(AActor* Actor)
{
	const FString LevelName = GetLevelNameForActor(Actor);

	auto LevelData = GetLevelData(LevelName, true);
	StoreLevelActorDestroyed(Actor, LevelData);
}

FSpudNamedObjectData* USpudState::GetGlobalObjectData(const UObject* Obj, bool AutoCreate)
{
	// Get the identifier; prefer GUID if present, if not just use name
	const FString ID = SpudPropertyUtil::GetGlobalObjectID(Obj);

	return GetGlobalObjectData(ID, AutoCreate);
}

FSpudNamedObjectData* USpudState::GetGlobalObjectData(const FString& ID, bool AutoCreate)
{
	FSpudNamedObjectData* Ret = SaveData.GlobalData.Objects.Contents.Find(ID);
	if (!Ret && AutoCreate)
	{
		Ret = &SaveData.GlobalData.Objects.Contents.Add(ID);
		Ret->Name = ID;
	}

	return Ret;
}


void USpudState::StoreGlobalObject(UObject* Obj)
{
	StoreGlobalObject(Obj, GetGlobalObjectData(Obj, true));
}

void USpudState::StoreGlobalObject(UObject* Obj, const FString& ID)
{
	StoreGlobalObject(Obj, GetGlobalObjectData(ID, true));
}

void USpudState::StoreGlobalObject(UObject* Obj, FSpudNamedObjectData* Data)
{
	
	if (Data)
	{
		FSpudClassMetadata& Meta = SaveData.GlobalData.Metadata;
		Data->ClassID = Meta.FindOrAddClassIDFromName(SpudPropertyUtil::GetClassName(Obj));
		const bool bIsCallback = Obj->GetClass()->ImplementsInterface(USpudObjectCallback::StaticClass());

		if (Obj->Implements<USpudObject>() && ISpudObject::Execute_ShouldSkip(Obj))
		{
			UE_LOG(LogSpudState, Verbose, TEXT("* SKIP Global object: %s"), *Obj->GetName());
			return;
		}

		UE_LOG(LogSpudState, Verbose, TEXT("* STORE Global object: %s"), *Obj->GetName());

		if (bIsCallback)
			ISpudObjectCallback::Execute_SpudPreStore(Obj, this);

		StoreObjectProperties(Obj, Data->Properties, Meta);
		
		if (bIsCallback)
		{
			Data->CustomData.Data.Empty();
			FSpudMemoryWriter CustomDataWriter(Data->CustomData.Data);
			auto CustomDataStruct = NewObject<USpudStateCustomData>();
			CustomDataStruct->Init(&CustomDataWriter);
			ISpudObjectCallback::Execute_SpudStoreCustomData(Obj, this, CustomDataStruct);
			
			ISpudObjectCallback::Execute_SpudPostStore(Obj, this);
		}
		
	}
}


void USpudState::StoreObjectProperties(UObject* Obj, FSpudPropertyData& Properties, FSpudClassMetadata& Meta, int StartDepth)
{
	auto& PropOffsets = Properties.PropertyOffsets;
		
	auto& PropData = Properties.Data;
	PropData.Empty();
	FSpudMemoryWriter PropertyWriter(PropData);

	StoreObjectProperties(Obj, SPUDDATA_PREFIXID_NONE, PropOffsets, Meta, PropertyWriter, StartDepth);	
}


void USpudState::StoreObjectProperties(UObject* Obj, uint32 PrefixID, TArray<uint32>& PropOffsets,
	FSpudClassMetadata& Meta, FSpudMemoryWriter& Out, int StartDepth)
{
	const FString& ClassName = SpudPropertyUtil::GetClassName(Obj);
	auto ClassDef = Meta.FindOrAddClassDef(ClassName);

	// visit all properties and write out
	StorePropertyVisitor Visitor(this, ClassDef, PropOffsets, Meta, Out);
	SpudPropertyUtil::VisitPersistentProperties(Obj, Visitor, StartDepth);
}

void USpudState::RestoreLevel(UWorld* World, const FString& LevelName)
{
	RestoreLoadedWorld(World, true, LevelName);
}

void USpudState::RestoreLevel(ULevel* Level)
{
	if (!IsValid(Level))
		return;
	
	FString LevelName = GetLevelName(Level);
	auto LevelData = GetLevelData(LevelName, false);

	if (!LevelData.IsValid())
	{
		UE_LOG(LogSpudState, Log, TEXT("Skipping restore level %s, no data (this may be fine)"), *LevelName);
		return;
	}

	// Mutex lock the level (load and unload events on streaming can be in loading threads)
	FScopeLock LevelLock(&LevelData->Mutex);
	
	UE_LOG(LogSpudState, Verbose, TEXT("RESTORE level %s - Start"), *LevelName);
	TMap<FGuid, UObject*> RuntimeObjectsByGuid;
	// Respawn dynamic actors first; they need to exist in order for cross-references in level actors to work
	for (auto&& SpawnedActor : LevelData->SpawnedActors.Contents)
	{
		auto Actor = RespawnActor(SpawnedActor.Value, LevelData->Metadata, Level);
		if (Actor)
			RuntimeObjectsByGuid.Add(SpawnedActor.Value.Guid, Actor);
		// Spawned actors will have been added to Level->Actors, their state will be restored there
	}

	TMap<FGuid, AActor*> RestoredRuntimeActors;

	// Restore existing actor state
	for (auto Actor : Level->Actors)
	{
		if (SpudPropertyUtil::IsPersistentObject(Actor))
		{
			RestoreActor(Actor, LevelData, &RuntimeObjectsByGuid);
			auto Guid = SpudPropertyUtil::GetGuidProperty(Actor);
			if (Guid.IsValid())
			{
				if (RuntimeObjectsByGuid.Contains(Guid))
				{
					if (const auto DuplicatedActor = RestoredRuntimeActors.Find(Guid))
					{
						UE_LOG(LogSpudState, Verbose, TEXT("RESTORE level %s - destroying duplicate runtime actor %s"),
						       *LevelName, *Guid.ToString(EGuidFormats::DigitsWithHyphens));

						// sometimes runtime actors are duplicated in the level actors array - for example, when hiding a
						// world partition cell and immediately showing it; need to remove duplicates in this case
						(*DuplicatedActor)->Destroy();
					}
					else
					{
						RestoredRuntimeActors.Emplace(Guid, Actor);
					}
				}
				else
				{
					RuntimeObjectsByGuid.Add(Guid, Actor);
				}
			}
		}
	}
	// Destroy actors in level but missing from save state
	for (auto&& DestroyedActor : LevelData->DestroyedActors.Values)
	{
		DestroyActor(*DestroyedActor, Level);			
	}
	UE_LOG(LogSpudState, Verbose, TEXT("RESTORE level %s - Complete"), *LevelName);

}

bool USpudState::PreLoadLevelData(const FString& LevelName)
{
	// Don't auto-create, but do load if needed
	auto Data = GetLevelData(LevelName, false);
	return Data != nullptr;
}

void USpudState::RestoreActor(AActor* Actor)
{
	if (Actor->HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject|RF_BeginDestroyed))
		return;

	const FString LevelName = GetLevelNameForActor(Actor);

	auto LevelData = GetLevelData(LevelName, false);
	if (!LevelData.IsValid())
	{
		UE_LOG(LogSpudState, Error, TEXT("Unable to restore Actor %s, missing level data"), *Actor->GetName());
		return;
	}

	RestoreActor(Actor, LevelData, nullptr);
}


AActor* USpudState::RespawnActor(const FSpudSpawnedActorData& SpawnedActor,
                                 const FSpudClassMetadata& Meta,
                                 ULevel* Level)
{
	const FString ClassName = Meta.GetClassNameFromID(SpawnedActor.ClassID);
	const FSoftClassPath CP(ClassName);
	const auto Class = CP.TryLoadClass<AActor>();

	if (!Class)
	{
		UE_LOG(LogSpudState, Error, TEXT("Cannot respawn instance of %s, class not found"), *ClassName);
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.OverrideLevel = Level;
	// Need to always spawn since we're not setting position until later
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UE_LOG(LogSpudState, Verbose, TEXT(" * Respawning actor %s of type %s"), *SpawnedActor.Guid.ToString(EGuidFormats::DigitsWithHyphens), *ClassName);

	// Important to spawn using level's world, our GetWorld may not be valid it turns out
	auto World = Level->GetWorld();
	AActor* Actor = World->SpawnActor<AActor>(Class, Params);
	if (Actor)
	{
		if (!SpudPropertyUtil::SetGuidProperty(Actor, SpawnedActor.Guid))
		{
			UE_LOG(LogSpudState, Error, TEXT("Re-spawned a runtime actor of class %s but it is missing a SpudGuid property!"), *ClassName);
		}		
	}
	else
	{
		UE_LOG(LogSpudState, Error, TEXT("Error spawning actor of type %s"), *ClassName);
	}
	return Actor;
}

void USpudState::DestroyActor(const FSpudDestroyedLevelActor& DestroyedActor, ULevel* Level)
{
	// We only ever have to destroy level actors, not runtime objects (those are just missing on restore)
	auto Obj = StaticFindObject(AActor::StaticClass(), Level, *DestroyedActor.Name);
	if (auto Actor = Cast<AActor>(Obj))
	{
		UE_LOG(LogSpudState, Verbose, TEXT(" * Destroying actor %s"), *DestroyedActor.Name);
		Level->GetWorld()->DestroyActor(Actor);
	}
}

bool USpudState::ShouldRespawnRuntimeActor(const AActor* Actor) const
{
	ESpudRespawnMode RespawnMode = ESpudRespawnMode::Default;
	if (Actor->Implements<USpudObject>())
	{
		RespawnMode = ISpudObject::Execute_GetSpudRespawnMode(Actor);
	}

	switch (RespawnMode)
	{
	default:
	case ESpudRespawnMode::Default:
		// Default behaviour is to respawn everything except pawns, characters, game modes, game states
		// Those we assume are created by other init processes
		return !Actor->IsA(AGameModeBase::StaticClass()) &&
            !Actor->IsA(AGameStateBase::StaticClass()) &&
            !Actor->IsA(APawn::StaticClass()) &&
            !Actor->IsA(ACharacter::StaticClass()) &&
            !Actor->IsA(APlayerState::StaticClass());
	case ESpudRespawnMode::AlwaysRespawn:
		return true;
	case ESpudRespawnMode::NeverRespawn:
		return false;
	}
}


bool USpudState::ShouldActorBeRespawnedOnRestore(AActor* Actor) const
{
	return SpudPropertyUtil::IsRuntimeActor(Actor) &&
		ShouldRespawnRuntimeActor(Actor);
}

bool USpudState::ShouldActorTransformBeRestored(AActor* Actor) const
{
	if (Actor->Implements<USpudObject>())
	{
		return !ISpudObject::Execute_ShouldSkipRestoreTransform(Actor);
	}
	// Assume true
	return true;
}

bool USpudState::ShouldActorVelocityBeRestored(AActor* Actor) const
{
	if (Actor->Implements<USpudObject>())
	{
		return !ISpudObject::Execute_ShouldSkipRestoreVelocity(Actor);
	}
	// Assume true
	return true;
}

void USpudState::RestoreActor(AActor* Actor, FSpudSaveData::TLevelDataPtr LevelData, const TMap<FGuid, UObject*>* RuntimeObjects)
{
	if (Actor->HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject|RF_BeginDestroyed))
		return;

	const bool bRespawned = ShouldActorBeRespawnedOnRestore(Actor);
	const FSpudObjectData* ActorData;

	if (bRespawned)
	{
		ActorData = GetSpawnedActorData(Actor, LevelData, false);
		UE_LOG(LogSpudState, Verbose, TEXT(" * RESTORE Runtime Actor: %s"), *Actor->GetName())
	}
	else
	{
		ActorData = GetLevelActorData(Actor, LevelData, false);
		UE_LOG(LogSpudState, Verbose, TEXT(" * RESTORE Level Actor: %s"), *Actor->GetName())
	}

	if (ActorData)
	{
		PreRestoreObject(Actor, LevelData->GetUserDataModelVersion());
		
		RestoreCoreActorData(Actor, ActorData->CoreData);
		const auto ClassDef = LevelData->Metadata.GetClassDef(ActorData->ClassID);
		RestoreObjectProperties(Actor, ActorData->Properties, LevelData->Metadata, ClassDef, RuntimeObjects);

		PostRestoreObject(Actor, ActorData->CustomData, LevelData->GetUserDataModelVersion());		
	}
}


void USpudState::PreRestoreObject(UObject* Obj, uint32 StoredUserVersion)
{
	if(Obj->GetClass()->ImplementsInterface(USpudObjectCallback::StaticClass()))
	{
		if (GCurrentUserDataModelVersion != StoredUserVersion)
			ISpudObjectCallback::Execute_SpudPreRestoreDataModelUpgrade(Obj, this, StoredUserVersion, GCurrentUserDataModelVersion);
			
		ISpudObjectCallback::Execute_SpudPreRestore(Obj, this);
		
	}
}

void USpudState::PostRestoreObject(UObject* Obj, const FSpudCustomData& FromCustomData, uint32 StoredUserVersion)
{
	if (Obj->GetClass()->ImplementsInterface(USpudObjectCallback::StaticClass()))
	{
		if (GCurrentUserDataModelVersion != StoredUserVersion)
			ISpudObjectCallback::Execute_SpudPostRestoreDataModelUpgrade(Obj, this, StoredUserVersion, GCurrentUserDataModelVersion);

		FSpudMemoryReader Reader(FromCustomData.Data);
		auto CustomData = NewObject<USpudStateCustomData>();
		CustomData->Init(&Reader);
		ISpudObjectCallback::Execute_SpudRestoreCustomData(Obj, this, CustomData);
		ISpudObjectCallback::Execute_SpudPostRestore(Obj, this);
	}
}

void USpudState::RestoreCoreActorData(AActor* Actor, const FSpudCoreActorData& FromData)
{
	// Restore core data based on version
	// Unlike properties this is packed data, versioned

	FSpudMemoryReader In(FromData.Data);
	
	// All formats have version number first (this is separate from the file version)
	uint16 InVersion = 0;
	SpudPropertyUtil::ReadRaw(InVersion, In);

	if (InVersion == 1)
	{
		// First, and only version right now
		// V1 Format:
		// - Version (uint16)
		// - Hidden (bool)
		// - Transform (FTransform)
		// - Velocity (FVector)
		// - AngularVelocity (FVector)
		// - Control rotation (FRotator) (non-zero for Pawns only)

		bool Hidden;
		SpudPropertyUtil::ReadRaw(Hidden, In);
		Actor->SetActorHiddenInGame(Hidden);

		FTransform XForm;
		SpudPropertyUtil::ReadRaw(XForm, In);

		FVector Velocity, AngularVelocity;
		SpudPropertyUtil::ReadRaw(Velocity, In);
		SpudPropertyUtil::ReadRaw(AngularVelocity, In);

		FRotator ControlRotation;
		SpudPropertyUtil::ReadRaw(ControlRotation, In);


		auto Pawn = Cast<APawn>(Actor);
		if (Pawn && Pawn->IsPlayerControlled() &&
			!GetSpudSubsystem(Pawn->GetWorld())->IsLoadingGame())
		{
			// This is a player-controlled pawn, and we're not loading the game
			// That means this was a map transition. In this case we do NOT want to reset the pawn's position
			// because we don't know that the player wants to appear at the last place they were
			// Let user code decide which player start is used
			// SKIP the rest - but we must have still read data above
			return;
			
		}

		const auto RootComp = Actor->GetRootComponent();
		if (RootComp && RootComp->Mobility == EComponentMobility::Movable &&
			ShouldActorTransformBeRestored(Actor))
		{
			// Only set the actor transform if movable, to avoid editor warnings about static/stationary objects
			Actor->SetActorTransform(XForm, false, nullptr, ETeleportType::ResetPhysics);

			if (ShouldActorVelocityBeRestored(Actor))
			{
				if (Velocity.SizeSquared() > FLT_EPSILON || AngularVelocity.SizeSquared() > FLT_EPSILON)
				{
					const auto PrimComp = Cast<UPrimitiveComponent>(RootComp);

					// note: DO NOT use IsSimulatingPhysics() since that's dependent on BodyInstance.BodySetup being valid, which
					// it might not be at setup. We only want the *intention* to simulate physics, not whether it's currently happening
					if (PrimComp && PrimComp->BodyInstance.bSimulatePhysics)
					{
						PrimComp->SetAllPhysicsLinearVelocity(Velocity);
						PrimComp->SetAllPhysicsAngularVelocityInDegrees(AngularVelocity);
					}
					else if (const auto	MoveComponent = Cast<UMovementComponent>(Actor->FindComponentByClass(UMovementComponent::StaticClass())))
					{
						MoveComponent->Velocity = Velocity;
					}
				}
			}
		}

		if (Pawn)
		{
			if (auto Controller = Pawn->GetController())
			{
				Controller->SetControlRotation(ControlRotation);
			}
		}

	}
	else
	{
		UE_LOG(LogSpudState, Error, TEXT("Core Actor Data for %s is corrupt, not restoring"), *Actor->GetName())
		return;
	}
}

void USpudState::RestoreObjectProperties(UObject* Obj, const FSpudPropertyData& FromData, const FSpudClassMetadata& Meta,
	TSharedPtr<const FSpudClassDef> StoredClassDef, const TMap<FGuid, UObject*>* RuntimeObjects, int StartDepth)
{
	FSpudMemoryReader In(FromData.Data);
	RestoreObjectProperties(Obj, In, Meta, StoredClassDef, FromData.PropertyOffsets, RuntimeObjects, StartDepth);

}


void USpudState::RestoreObjectProperties(UObject* Obj, FSpudMemoryReader& In, const FSpudClassMetadata& Meta,
										 TSharedPtr<const FSpudClassDef> StoredClassDef, const TArray<uint32>& PropertyOffsets,
										 const TMap<FGuid, UObject*>* RuntimeObjects, int StartDepth)
{
	if (!StoredClassDef)
	{
		UE_LOG(LogSpudState, Error, TEXT("Unable to find StoredClassDef for: %s"), *SpudPropertyUtil::GetClassName(Obj));
		return;
	}
	
	const auto ClassName = SpudPropertyUtil::GetClassName(Obj);
	const auto ClassDef = Meta.GetClassDef(ClassName);

	// We can use the "fast" path if the stored definition of the class properties exactly matches the runtime order
	// ClassDef caches the result of this across the context of one loaded file
	bool bUseFastPath = StoredClassDef->MatchesRuntimeClass(Obj->GetClass(), Meta);

	UE_LOG(LogSpudState, Verbose, TEXT("%s Class: %s"), *SpudPropertyUtil::GetLogPrefix(StartDepth), *StoredClassDef->ClassName);

	if (!bUseFastPath && bTestRequireFastPath)
	{
		UE_LOG(LogSpudState, Error, TEXT("Test required the use of the fast path but slow path was used for %s"), *ClassName);
	}
	// force use of slow path for testing if needed
	if (bTestRequireSlowPath)
		bUseFastPath = false;
	
	
	if (bUseFastPath)
		RestoreObjectPropertiesFast(Obj, In, Meta, StoredClassDef, PropertyOffsets, RuntimeObjects, StartDepth);
	else
		RestoreObjectPropertiesSlow(Obj, In, Meta, StoredClassDef, PropertyOffsets, RuntimeObjects, StartDepth);
}

void USpudState::RestoreObjectPropertiesFast(UObject* Obj, FSpudMemoryReader& In,
                                             const FSpudClassMetadata& Meta,
                                             TSharedPtr<const FSpudClassDef> ClassDef,
                                             const TArray<uint32>& PropertyOffsets,
                                             const TMap<FGuid, UObject*>* RuntimeObjects,
                                             int StartDepth)
{
	UE_LOG(LogSpudState, Verbose, TEXT("%s FAST path, %d properties"), *SpudPropertyUtil::GetLogPrefix(StartDepth), ClassDef->Properties.Num());
	const auto StoredPropertyIterator = ClassDef->Properties.CreateConstIterator();

	RestoreFastPropertyVisitor Visitor(this, StoredPropertyIterator, In, ClassDef, PropertyOffsets, Meta, RuntimeObjects);
	SpudPropertyUtil::VisitPersistentProperties(Obj, Visitor, StartDepth);
	
}

void USpudState::RestoreObjectPropertiesSlow(UObject* Obj, FSpudMemoryReader& In,
                                                       const FSpudClassMetadata& Meta,
                                                       TSharedPtr<const FSpudClassDef> ClassDef,
                                                       const TArray<uint32>& PropertyOffsets,
                                                       const TMap<FGuid, UObject*>* RuntimeObjects,
                                                       int StartDepth)
{
	UE_LOG(LogSpudState, Verbose, TEXT("%s SLOW path, %d properties"), *SpudPropertyUtil::GetLogPrefix(StartDepth), ClassDef->Properties.Num());

	RestoreSlowPropertyVisitor Visitor(this, In, ClassDef, PropertyOffsets, Meta, RuntimeObjects);
	SpudPropertyUtil::VisitPersistentProperties(Obj, Visitor, StartDepth);
}


uint32 USpudState::RestorePropertyVisitor::GetNestedPrefix(FProperty* Prop, uint32 CurrentPrefixID)
{
	// This doesn't create a new ID, expects it to be there already (should be since restoring)
	return SpudPropertyUtil::GetNestedPrefixID(CurrentPrefixID, Prop, Meta);
}

void USpudState::RestorePropertyVisitor::RestoreNestedUObjectIfNeeded(UObject* RootObject, FProperty* Property,
														uint32 CurrentPrefixID, void* ContainerPtr, int Depth)
{
	if (SpudPropertyUtil::IsNestedUObjectProperty(Property))
	{
		if (const auto OProp = CastField<FObjectProperty>(Property))
		{
			void* DataPtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
			const auto Obj = OProp->GetObjectPropertyValue(DataPtr);

			// By this point, the restore will have created the instance if the data was non-null, since the
			// property before this contains the class (or null)
			if (Obj)
			{
				auto Format = ESpudObjectStoreFormat::NestedProperties;
				if (ParentState->SaveData.Info.SystemVersion >= 3)
				{
					SpudPropertyUtil::ReadRaw(Format, DataIn);
				}

				if (Format == ESpudObjectStoreFormat::AssetPath)
				{
					// Special case for assets - we just store the path, which needs to be loaded
					FTopLevelAssetPath Path;

					SpudPropertyUtil::ReadRaw(Path, DataIn);

					UE_LOG(LogSpudState, Verbose, TEXT("Restoring asset link for %s: %s"), *Property->GetNameCPP(), *Path.ToString());

					const FSoftObjectPtr TmpPtr{FSoftObjectPath(Path)};
					OProp->SetObjectPropertyValue(DataPtr, TmpPtr.LoadSynchronous());
				}
				else
				{
					const bool IsCallback = Obj->GetClass()->ImplementsInterface(USpudObjectCallback::StaticClass());

					if (IsCallback)
					{
						ISpudObjectCallback::Execute_SpudPreRestore(Obj, ParentState);
					}
					
					TArray<uint32> ObjectPropertyOffsets;
					TArray<uint8> ObjectData;
					DataIn << ObjectPropertyOffsets;
					DataIn << ObjectData;
					
					FSpudMemoryReader ObjectDataIn(ObjectData);
					const uint32 NewPrefixID = GetNestedPrefix(Property, CurrentPrefixID);
					const auto StoredClassDef = Meta.GetClassDef(SpudPropertyUtil::GetClassName(Obj));
					ParentState->RestoreObjectProperties(Obj, ObjectDataIn, Meta, StoredClassDef, ObjectPropertyOffsets, RuntimeObjects, Depth+1);

					if (IsCallback)
					{
						// No custom data callbacks for nested UObjects, only root ones
						// This is because nested UObjects don't get their own data package, and could be null sometimes etc,
						// could interfere with data packing in nasty ways
						// I *could* store UObjects in their own data wrappers but that becomes cumbersome so don't for now
						ISpudObjectCallback::Execute_SpudPostRestore(Obj, ParentState);
					}
				}
			}
		}
	}	
}

bool USpudState::RestoreFastPropertyVisitor::VisitProperty(UObject* RootObject, FProperty* Property,
                                                           uint32 CurrentPrefixID, void* ContainerPtr, int Depth)
{
	// Fast path can just iterate both sides of properties because stored properties are in the same order
	if (StoredPropertyIterator)
	{
		auto& StoredProperty = *StoredPropertyIterator;
		SpudPropertyUtil::RestoreProperty(RootObject, Property, ContainerPtr, StoredProperty, RuntimeObjects, Meta, Depth, DataIn);

		// We DON'T increment the property iterator for custom structs, since they don't have any values of their own
		// It's their nested properties that have the values, they're only context
		if (!SpudPropertyUtil::IsCustomStructProperty(Property))
			++StoredPropertyIterator;

		RestoreNestedUObjectIfNeeded(RootObject, Property, CurrentPrefixID, ContainerPtr, Depth);

		return true;
	}
	return false;
}

bool USpudState::RestoreSlowPropertyVisitor::VisitProperty(UObject* RootObject, FProperty* Property,
                                                                     uint32 CurrentPrefixID, void* ContainerPtr, int Depth)
{
	// This is the slow alternate property restoration path
	// Used when the runtime class definition no longer matches the stored class definition
	// This should go away as soon as the data is re-saved and go back to the fast path


	// Custom structs don't need to do anything at the root, visitor calls will cascade inside for each property inside the struct
	// Builtin structs continue though since those are restored with custom, more efficient member population
	if (SpudPropertyUtil::IsCustomStructProperty(Property))
		return true;
	
	// PropertyLookup is PrefixID -> Map of PropertyNameID to PropertyIndex
	auto InnerMapPtr = ClassDef->PropertyLookup.Find(CurrentPrefixID);
	if (!InnerMapPtr)
	{
		UE_LOG(LogSpudState, Error, TEXT("Error in RestoreSlowPropertyVisitor, PrefixID invalid for %s, class %s"), *Property->GetName(), *ClassDef->ClassName);
		return true;
	}
	
	uint32 PropID = Meta.GetPropertyIDFromName(Property->GetName());
	if (PropID == SPUDDATA_INDEX_NONE)
	{
		UE_LOG(LogSpudState, Log, TEXT("Skipping property %s on class %s, not found in class definition"), *Property->GetName(), *ClassDef->ClassName);
		return true;
	}
	const int* PropertyIndexPtr = InnerMapPtr->Find(PropID);
	if (!PropertyIndexPtr)
	{
		UE_LOG(LogSpudState, Log, TEXT("Skipping property %s on class %s, data not found"), *Property->GetName(), *ClassDef->ClassName);
		return true;		
	}
	if (*PropertyIndexPtr < 0 || *PropertyIndexPtr >= ClassDef->Properties.Num())
	{
		UE_LOG(LogSpudState, Error, TEXT("Error in RestoreSlowPropertyVisitor, invalid property index for %s on class %s"), *Property->GetName(), *ClassDef->ClassName);
		return true;		
	}
	auto& StoredProperty = ClassDef->Properties[*PropertyIndexPtr];
	DataIn.Seek(PropertyOffsets[*PropertyIndexPtr]);
	SpudPropertyUtil::RestoreProperty(RootObject, Property, ContainerPtr, StoredProperty, RuntimeObjects, Meta, Depth, DataIn);

	RestoreNestedUObjectIfNeeded(RootObject, Property, CurrentPrefixID, ContainerPtr, Depth);
	
	return true;
}

void USpudState::RestoreLoadedWorld(UWorld* World)
{
	RestoreLoadedWorld(World, false);
}

void USpudState::RestoreLoadedWorld(UWorld* World, bool bSingleLevel, const FString& OnlyLevel)
{
	// So that we don't need to check every instance of a class for matching stored / runtime class properties
	// we will keep a cache of whether to use the fast or slow path. It's only valid for this specific load
	// because we may load level data or different ages
	for (auto& Level : World->GetLevels())
	{
		// Null levels possible
		if (!IsValid(Level))
			continue;

		if (bSingleLevel && GetLevelName(Level) != OnlyLevel)
			continue;

		if (!ShouldStoreLevel(Level))
			continue;

		RestoreLevel(Level);
		
	}

}

void USpudState::RestoreGlobalObject(UObject* Obj)
{
	RestoreGlobalObject(Obj, GetGlobalObjectData(Obj, false));
}

void USpudState::RestoreGlobalObject(UObject* Obj, const FString& ID)
{
	RestoreGlobalObject(Obj, GetGlobalObjectData(ID, false));
}

void USpudState::RestoreGlobalObject(UObject* Obj, const FSpudNamedObjectData* Data)
{
	if (Data)
	{
		UE_LOG(LogSpudState, Verbose, TEXT("* RESTORE Global Object %s"), *Data->Name)
		PreRestoreObject(Obj, SaveData.GlobalData.GetUserDataModelVersion());

		const auto StoredClassDef = SaveData.GlobalData.Metadata.GetClassDef(Data->ClassID);
		RestoreObjectProperties(Obj, Data->Properties, SaveData.GlobalData.Metadata, StoredClassDef, nullptr);

		PostRestoreObject(Obj, Data->CustomData, SaveData.GlobalData.GetUserDataModelVersion());
	}
	
}
void USpudState::StoreActor(AActor* Actor, FSpudSaveData::TLevelDataPtr LevelData)
{
	if (Actor->HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject|RF_BeginDestroyed))
		return;

	// GetUniqueID() is unique in the current play session but not across games
	// GetFName() is unique within a level, and stable for objects loaded from a level
	// For runtime created objects we need another stable GUID
	// For that we'll rely on a SpudGuid property
	// For convenience you can use one of the persistent base classes to get that, otherwise you need
	// to add a SpudGuid propery
	
	// This is how we identify run-time created objects
	bool bRespawn = ShouldActorBeRespawnedOnRestore(Actor);
	FString Name;
	FGuid Guid;

	TArray<uint8>* pDestCoreData = nullptr;
	FSpudPropertyData* pDestProperties = nullptr;
	TArray<uint8>* pDestCustomData = nullptr;
	FSpudClassMetadata& Meta = LevelData->Metadata;
	if (bRespawn)
	{
		auto ActorData = GetSpawnedActorData(Actor, LevelData, true);
		if (ActorData)
		{
			pDestCoreData = &ActorData->CoreData.Data;
			pDestProperties = &ActorData->Properties;
			pDestCustomData = &ActorData->CustomData.Data;
			Guid = ActorData->Guid;
			Name = SpudPropertyUtil::GetLevelActorName(Actor);
		}
	}
	else
	{
		auto ActorData = GetLevelActorData(Actor, LevelData, true);
		if (ActorData)
		{
			pDestCoreData = &ActorData->CoreData.Data;
			pDestProperties = &ActorData->Properties;
			pDestCustomData = &ActorData->CustomData.Data;
			Name = ActorData->Name;

#if WITH_EDITOR
			// Verify that cases where the actor wasn't loaded from the level, but also
			// wasn't respawned, such as Characters, GameState, that we got an overridden name because
			// the FName isn't reliable in non-Editor builds
			// See https://github.com/sinbad/SPUD/issues/41
			if (SpudPropertyUtil::IsRuntimeActor(Actor))
			{
				FString OverrideName = ISpudObject::Execute_OverrideName(Actor);
				if (OverrideName.IsEmpty())
				{
					UE_LOG(LogSpudProps, Warning, TEXT("Actor %s should implement 'OverrideName' with a predefined name. "
						"This is because it's not saved in the level, but is also a special type not automatically respawned by Spud. "
						"Examples include ACharacter, APlayerState etc. These instances should have a predefined name to reliably restore them. "
						"They may work fine in editor builds, but will start to fail in non-Editor builds."), *Actor->GetName())
				}
			}
#endif

		}
	}

	if (!pDestProperties)
	{
		// Something went wrong, we'll assume the detail has been logged elsewhere
		return;	
	}
	

	if (bRespawn)
		UE_LOG(LogSpudState, Verbose, TEXT(" * STORE Runtime Actor: %s (%s)"), *Guid.ToString(EGuidFormats::DigitsWithHyphens), *Name)
	else
		UE_LOG(LogSpudState, Verbose, TEXT(" * STORE Level Actor: %s/%s"), *LevelData->Name, *Name);

	bool bIsCallback = Actor->GetClass()->ImplementsInterface(USpudObjectCallback::StaticClass());

	if (bIsCallback)
		ISpudObjectCallback::Execute_SpudPreStore(Actor, this);

	// Core data first
	pDestCoreData->Empty();
	FSpudMemoryWriter CoreDataWriter(*pDestCoreData);
	WriteCoreActorData(Actor, CoreDataWriter);

	// Now properties, visit all and write out
	StoreObjectProperties(Actor, *pDestProperties, Meta);

	if (bIsCallback)
	{
		if (pDestCustomData)
		{
			pDestCustomData->Empty();
			FSpudMemoryWriter CustomDataWriter(*pDestCustomData);
			auto CustomDataStruct = NewObject<USpudStateCustomData>();
			CustomDataStruct->Init(&CustomDataWriter);
			ISpudObjectCallback::Execute_SpudStoreCustomData(Actor, this, CustomDataStruct);
		}			
	
		ISpudObjectCallback::Execute_SpudPostStore(Actor, this);
	}
}


void USpudState::StoreLevelActorDestroyed(AActor* Actor, FSpudSaveData::TLevelDataPtr LevelData)
{
	// We don't check for duplicates, because it should only be possible to destroy a uniquely named level actor once
	LevelData->DestroyedActors.Add(SpudPropertyUtil::GetLevelActorName(Actor));
}

void USpudState::SaveToArchive(FArchive& SPUDAr)
{
	// We use separate read / write in order to more clearly support chunked file format
	// with the backwards compatibility that comes with 
	FSpudChunkedDataArchive ChunkedAr(SPUDAr);
	SaveData.PrepareForWrite();
	// Use WritePaged in all cases; if all data is loaded it amounts to the same thing
	SaveData.WriteToArchive(ChunkedAr, GetActiveGameLevelFolder());

}

void USpudState::LoadFromArchive(FArchive& SPUDAr, bool bFullyLoadAllLevelData)
{
	// Firstly, destroy any active game level files
	RemoveAllActiveGameLevelFiles();

	Source = SPUDAr.GetArchiveName();
	
	FSpudChunkedDataArchive ChunkedAr(SPUDAr);
	SaveData.ReadFromArchive(ChunkedAr, bFullyLoadAllLevelData, GetActiveGameLevelFolder());
}

bool USpudState::IsLevelDataLoaded(const FString& LevelName)
{
	auto Lvldata = SaveData.GetLevelData(LevelName, false, GetActiveGameLevelFolder());

	if (!Lvldata.IsValid())
		return false;

	return Lvldata->IsLoaded();
}

void USpudState::ClearLevel(const FString& LevelName)
{
	SaveData.DeleteLevelData(LevelName, GetActiveGameLevelFolder());
}

bool USpudState::LoadSaveInfoFromArchive(FArchive& SPUDAr, USpudSaveGameInfo& OutInfo)
{
	FSpudChunkedDataArchive ChunkedAr(SPUDAr);
	FSpudSaveInfo StorageInfo;
	const bool Ok = FSpudSaveData::ReadSaveInfoFromArchive(ChunkedAr, StorageInfo);
	if (Ok)
	{
		OutInfo.Title = StorageInfo.Title;
		OutInfo.Timestamp = StorageInfo.Timestamp;
		if (StorageInfo.Screenshot.ImageData.Num() > 0)
			OutInfo.Thumbnail = FImageUtils::ImportBufferAsTexture2D(StorageInfo.Screenshot.ImageData);
		else
			OutInfo.Thumbnail = nullptr;
		OutInfo.CustomInfo = NewObject<USpudCustomSaveInfo>();
		OutInfo.CustomInfo->SetData(StorageInfo.CustomInfo);
	}
	return Ok;
	
}

void USpudStateCustomData::BeginWriteChunk(FString MagicID)
{
	auto MagicChar = StringCast<ANSICHAR>(*MagicID);

	if (MagicChar.Length() > 4)
	{
		UE_LOG(LogSpudData, Error, TEXT("Chunk ID %s is more than 4 characters long, will be truncated"), *MagicID);
	}

	const TSharedPtr<FSpudAdhocWrapperChunk> Chunk = MakeShareable<FSpudAdhocWrapperChunk>(new FSpudAdhocWrapperChunk(MagicChar.Get()));
	ChunkStack.Push(Chunk);

	Chunk->ChunkStart(*GetUnderlyingArchive());
}

void USpudStateCustomData::EndWriteChunk(FString MagicID)
{
	const auto CharStr = StringCast<ANSICHAR>(*MagicID);
	
	if (CharStr.Length() > 4)
	{
		UE_LOG(LogSpudData, Error, TEXT("Chunk ID %s is more than 4 characters long, truncating"), *MagicID);
	}

	if (ChunkStack.Num() == 0)
	{
		UE_LOG(LogSpudData, Error, TEXT("Cannot end chunk with ID %s, no chunks left to end"), *MagicID);
		return;	
	}

	if (strncmp(ChunkStack.Top()->Magic, CharStr.Get(), 4) != 0)
	{
		UE_LOG(LogSpudData, Fatal,
		       TEXT("Cannot call EndWriteChunk with ID %s because the last BeginWriteChunk was called with ID %hs"),
		       *MagicID,
		       ChunkStack.Top()->Magic);
		return;
	}

	const TSharedPtr<FSpudAdhocWrapperChunk> Chunk = ChunkStack.Pop();
	Chunk->ChunkEnd(*GetUnderlyingArchive());
	
}

bool USpudStateCustomData::BeginReadChunk(FString MagicID)
{
	const auto MagicChar = StringCast<ANSICHAR>(*MagicID);

	if (MagicChar.Length() > 4)
	{
		UE_LOG(LogSpudData, Error, TEXT("Chunk ID %s is more than 4 characters long, will be truncated"), *MagicID);
	}

	const TSharedPtr<FSpudAdhocWrapperChunk> Chunk = MakeShareable<FSpudAdhocWrapperChunk>(new FSpudAdhocWrapperChunk(MagicChar.Get()));
	const bool bOK = Chunk->ChunkStart(*GetUnderlyingArchive());
	if (bOK)
	{
		ChunkStack.Push(Chunk);
	}
	return bOK;
}

void USpudStateCustomData::EndReadChunk(FString MagicID)
{
	const auto CharStr = StringCast<ANSICHAR>(*MagicID);
	
	if (CharStr.Length() > 4)
	{
		UE_LOG(LogSpudData, Error, TEXT("Chunk ID %s is more than 4 characters long, truncating"), *MagicID);
	}

	if (ChunkStack.Num() == 0)
	{
		UE_LOG(LogSpudData, Error, TEXT("Cannot end chunk with ID %s, no chunks left to end"), *MagicID);
		return;	
	}

	if (strncmp(ChunkStack.Top()->Magic, CharStr.Get(), 4) != 0)
	{
		UE_LOG(LogSpudData, Fatal,
			   TEXT("Cannot call EndReadChunk with ID %s because the last BeginWriteChunk was called with ID %hs"),
			   *MagicID,
			   ChunkStack.Top()->Magic);
		return;
	}

	const TSharedPtr<FSpudAdhocWrapperChunk> Chunk = ChunkStack.Pop();
	Chunk->ChunkEnd(*GetUnderlyingArchive());
	
}

bool USpudStateCustomData::PeekChunk(FString& OutMagicID)
{
	FSpudChunkedDataArchive Ar(*GetUnderlyingArchive());
	FSpudChunkHeader Header;
	if (Ar.PreviewNextChunk(Header))
	{
		OutMagicID = FSpudChunkHeader::MagicToString(Header.MagicFriendly);
		return true;
	}

	return false;
}

bool USpudStateCustomData::SkipChunk(FString MagicID)
{
	const auto CharStr = StringCast<ANSICHAR>(*MagicID);
	
	if (CharStr.Length() > 4)
	{
		UE_LOG(LogSpudData, Error, TEXT("Chunk ID %s is more than 4 characters long, truncating"), *MagicID);
	}
	
	FSpudChunkedDataArchive Ar(*GetUnderlyingArchive());
	FSpudChunkHeader Header;
	if (Ar.PreviewNextChunk(Header, true))
	{
		if (strncmp(Header.MagicFriendly, CharStr.Get(), 4) == 0)
		{
			Ar.SkipNextChunk();
			return true;
		}
	}

	return false;

}

bool USpudStateCustomData::IsStillInChunk(FString MagicID) const
{
	if (ChunkStack.Num() == 0)
	{
		return false;	
	}
	
	const auto CharStr = StringCast<ANSICHAR>(*MagicID);
	
	if (CharStr.Length() > 4)
	{
		UE_LOG(LogSpudData, Error, TEXT("Chunk ID %s is more than 4 characters long, truncating"), *MagicID);
	}
	if (strncmp(ChunkStack.Top()->Magic, CharStr.Get(), 4) != 0)
	{
		return false;
	}

	return ChunkStack.Top()->IsStillInChunk(*GetUnderlyingArchive());
	
}

FString USpudState::GetActiveGameLevelFolder()
{
	return FString::Printf(TEXT("%sSpudCache/"), *FPaths::ProjectSavedDir());	
}

void USpudState::RemoveAllActiveGameLevelFiles()
{
	FSpudSaveData::DeleteAllLevelDataFiles(GetActiveGameLevelFolder());
}

bool USpudState::ShouldStoreLevel(ULevel* Level) const
{
	if (!Level)
		return false;
	
	if (auto Sys = GetSpudSubsystem(Level->GetWorld()))
	{
		return Sys->ShouldStoreLevel(Level);
	}
	return true;
}


void USpudState::SetCustomSaveInfo(const USpudCustomSaveInfo* ExtraInfo)
{
	if (ExtraInfo)
	{
		// Copy data
		SaveData.Info.CustomInfo = ExtraInfo->GetData();
	}
	else
	{
		SaveData.Info.CustomInfo.Reset();
	}
}

void USpudState::SetScreenshot(TArray<uint8>& ImgData)
{
	auto& Scr = SaveData.Info.Screenshot;
	Scr.ImageData = ImgData;
}

bool USpudState::RenameClass(const FString& OldClassName, const FString& NewClassName)
{
	// We only have to fix the metadata. All instances refer to the class by ID, so we just rename the
	// class in-place. In practice this doesn't *really* matter except for spawned objects, which need
	// to have the correct class name. Everything else doesn't really, the class ID is just used to find
	// the property def in the save file which will still work even if the runtime class isn't called that any more
	bool Changed = SaveData.GlobalData.Metadata.RenameClass(OldClassName, NewClassName);
	for (auto && Pair : SaveData.LevelDataMap)
	{
		Changed = Pair.Value->Metadata.RenameClass(OldClassName, NewClassName) || Changed;
	}
	return Changed;
}

bool USpudState::RenameProperty(const FString& ClassName, const FString& OldPropertyName,
                                const FString& NewPropertyName, const FString& OldPrefix, const FString& NewPrefix)
{
	// It's a little more complex than renaming a class because property names can be shared
	// between classes (so "Status" property on ClassA has the same ID as "Status" property on ClassB), so you can't
	// just replace in situ. For safety we'll always leave the existing property entry where it is, create or re-use
	// another property name entry.
	// But still only affects metadata; instances just have a list of data offsets corresponding with the class def,
	// which is what looks after the naming
	bool Changed = SaveData.GlobalData.Metadata.RenameProperty(ClassName, OldPropertyName, NewPropertyName, OldPrefix, NewPrefix);
	for (auto && Pair : SaveData.LevelDataMap)
	{
		Changed = Pair.Value->Metadata.RenameProperty(ClassName, OldPropertyName, NewPropertyName, OldPrefix, NewPrefix) || Changed;
	}
	return Changed;
}

bool USpudState::RenameGlobalObject(const FString& OldName, const FString& NewName)
{
	return SaveData.GlobalData.Objects.RenameObject(OldName, NewName);
}

bool USpudState::RenameLevelObject(const FString& LevelName, const FString& OldName, const FString& NewName)
{
	auto LevelData = GetLevelData(LevelName, false);
	if (LevelData.IsValid())
	{
		FScopeLock LevelLock(&LevelData->Mutex);

		return LevelData->LevelActors.RenameObject(OldName, NewName);
	}
	return false;
}

TArray<FString> USpudState::GetLevelNames(bool bLoadedOnly)
{
	TArray<FString> Ret;
	FScopeLock MapLock(&SaveData.LevelDataMapMutex);
	for (auto && Pair : SaveData.LevelDataMap)
	{
		auto Lvl = Pair.Value;
		FScopeLock LvlLock(&Lvl->Mutex);
		if (!bLoadedOnly || Lvl->Status != LDS_Unloaded)
		{
			Ret.Add(Lvl->Name);
		}
	}
	return Ret;
}
