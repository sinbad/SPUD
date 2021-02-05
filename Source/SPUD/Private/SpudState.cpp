#include "SpudState.h"

#include "EngineUtils.h"
#include "ISpudObject.h"
#include "SpudPropertyUtil.h"
#include "SpudSubsystem.h"
#include "Engine/LevelStreaming.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/MovementComponent.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY(LogSpudState)

PRAGMA_DISABLE_OPTIMIZATION

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
	SaveData.GlobalData.CurrentLevel = World->GetFName().ToString();
}


void USpudState::StoreLevel(ULevel* Level, bool bRelease)
{
	const FString LevelName = GetLevelName(Level);
	auto LevelData = GetLevelData(LevelName, true);

	if (LevelData)
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
	}

	if (bRelease)
		ReleaseLevelData(LevelName);
}

USpudState::StorePropertyVisitor::StorePropertyVisitor(
	FSpudClassDef& InClassDef, TArray<uint32>& InPropertyOffsets,
	FSpudClassMetadata& InMeta, FMemoryWriter& InOut):
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
	return true;
}

void USpudState::StorePropertyVisitor::UnsupportedProperty(UObject* RootObject,
    FProperty* Property, uint32 CurrentPrefixID, int Depth)
{
	UE_LOG(LogSpudState, Error, TEXT("Property %s/%s is marked for save but is an unsupported type, ignoring. E.g. Arrays of custom structs are not supported."),
        *RootObject->GetName(), *Property->GetName());
	
}

uint32 USpudState::StorePropertyVisitor::GetNestedPrefix(
	FStructProperty* SProp, uint32 CurrentPrefixID)
{
	// When updating we generate new prefix IDs as needed
	return SpudPropertyUtil::FindOrAddNestedPrefixID(CurrentPrefixID, SProp, Meta);
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

FString USpudState::GetLevelName(const ULevel* Level)
{
	// FName isn't good enough, it's "PersistentLevel" rather than the actual map name
	// Using the Outer to get the package name does it, same as for any other object
	return GetLevelNameForObject(Level);
}
FString USpudState::GetLevelNameForObject(const UObject* Obj)
{
	// Detect what level an object originated from
	// GetLevel()->GetName / GetFName() returns "PersistentLevel" all the time
	// GetLevel()->GetPathName returns e.g. /Game/Maps/[UEDPIE_0_]TestAdventureMap.TestAdventureMap:PersistentLevel
	// Outer is "PersistentLevel"
	// Outermost is "/Game/Maps/[UEDPIE_0_]TestAdventureStream0" so that's what we want
	const auto OuterMost = Obj->GetOutermost();
	if (OuterMost)
	{
		FString LevelName;
		OuterMost->GetName().Split("/", nullptr, &LevelName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		// Strip off PIE prefix, "UEDPIE_N_" where N is a number
		if (LevelName.StartsWith("UEDPIE_"))
			LevelName = LevelName.Right(LevelName.Len() - 9);
		return LevelName;
	}
	else
	{
		return FString();
	}	
}

FSpudLevelData* USpudState::GetLevelData(const FString& LevelName, bool AutoCreate)
{
	auto Ret = SaveData.GetLevelData(LevelName, true, GetActiveGameLevelFolder());
	
	if (!Ret && AutoCreate)
	{
		Ret = SaveData.CreateLevelData(LevelName);
	}
	
	return Ret;
}


void USpudState::ReleaseLevelData(const FString& LevelName)
{
	SaveData.WriteAndReleaseLevelData(LevelName, GetActiveGameLevelFolder());
}

FSpudNamedObjectData* USpudState::GetLevelActorData(const AActor* Actor, FSpudLevelData* LevelData, bool AutoCreate)
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

FString USpudState::GetClassName(const UObject* Obj)
{
	// Full class name allows for re-spawning
	// E.g. /Game/Blueprints/Class.Blah_C
	return Obj->GetClass()->GetPathName();
}

FSpudSpawnedActorData* USpudState::GetSpawnedActorData(AActor* Actor, FSpudLevelData* LevelData, bool AutoCreate)
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
		const FString ClassName = GetClassName(Actor); 
		Ret->ClassID = LevelData->Metadata.FindOrAddClassIDFromName(ClassName);
	}
	
	return Ret;
}

void USpudState::StoreActor(AActor* Obj)
{
	if (Obj->HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject|RF_BeginDestroyed))
		return;

	const FString LevelName = GetLevelNameForObject(Obj);

	FSpudLevelData* LevelData = GetLevelData(LevelName, true);
	StoreActor(Obj, LevelData);
		
}

void USpudState::StoreLevelActorDestroyed(AActor* Actor)
{
	const FString LevelName = GetLevelNameForObject(Actor);

	FSpudLevelData* LevelData = GetLevelData(LevelName, true);
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
		const FString& ClassName = GetClassName(Obj);
		auto& ClassDef = Meta.FindOrAddClassDef(ClassName);
		auto& PropOffsets = Data->Properties.PropertyOffsets;
		
		auto& PropData = Data->Properties.Data;
		bool bIsCallback = Obj->GetClass()->ImplementsInterface(USpudObjectCallback::StaticClass());

		UE_LOG(LogSpudState, Verbose, TEXT("* STORE Global object: %s"), *Obj->GetName());

		if (bIsCallback)
			ISpudObjectCallback::Execute_SpudPreStore(Obj, this);

		PropData.Empty();
		FMemoryWriter PropertyWriter(PropData);

		// visit all properties and write out
		StorePropertyVisitor Visitor(ClassDef, PropOffsets, Meta, PropertyWriter);
		SpudPropertyUtil::VisitPersistentProperties(Obj, Visitor);
		
		if (bIsCallback)
		{
			Data->CustomData.Data.Empty();
			FMemoryWriter CustomDataWriter(Data->CustomData.Data);
			auto CustomDataStruct = NewObject<USpudStateCustomData>();
			CustomDataStruct->Init(&CustomDataWriter);
			ISpudObjectCallback::Execute_SpudStoreCustomData(Obj, this, CustomDataStruct);
			
			ISpudObjectCallback::Execute_SpudPostStore(Obj, this);
		}
		
	}

	
	
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
	FSpudLevelData* LevelData = GetLevelData(LevelName, false);

	if (!LevelData)
	{
		UE_LOG(LogSpudState, Warning, TEXT("Skipping restore level %s, no data (this may be fine)"), *LevelName);
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
	// Restore existing actor state
	for (auto Actor : Level->Actors)
	{
		if (SpudPropertyUtil::IsPersistentObject(Actor))
		{
			RestoreActor(Actor, LevelData, &RuntimeObjectsByGuid);
			auto Guid = SpudPropertyUtil::GetGuidProperty(Actor);
			if (Guid.IsValid())
			{
				RuntimeObjectsByGuid.Add(Guid, Actor);
			}
		}
	}
	// Destroy actors in level but missing from save state
	for (auto&& DestroyedActor : LevelData->DestroyedActors.Values)
	{
		DestroyActor(DestroyedActor, Level);			
	}
	UE_LOG(LogSpudState, Verbose, TEXT("RESTORE level %s - Complete"), *LevelName);

}

void USpudState::RestoreActor(AActor* Actor)
{
	if (Actor->HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject|RF_BeginDestroyed))
		return;

	const FString LevelName = GetLevelNameForObject(Actor);

	FSpudLevelData* LevelData = GetLevelData(LevelName, false);
	if (!LevelData)
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
	UE_LOG(LogSpudState, Verbose, TEXT(" * Respawning actor %s of type %s"), *SpawnedActor.Guid.ToString(), *ClassName);

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
	// I know this cast style only supports C++ not Blueprints, but this method can only be defined in C++ anyway
	if (auto IObj = Cast<ISpudObject>(Actor))
	{
		RespawnMode = IObj->GetSpudRespawnMode();
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
            !Actor->IsA(ACharacter::StaticClass());
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


void USpudState::RestoreActor(AActor* Actor, FSpudLevelData* LevelData, const TMap<FGuid, UObject*>* RuntimeObjects)
{
	if (Actor->HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject|RF_BeginDestroyed))
		return;

	const bool bRespawned = ShouldActorBeRespawnedOnRestore(Actor);
	const FSpudObjectData* ActorData;

	if (bRespawned)
	{
		ActorData = GetSpawnedActorData(Actor, LevelData, false);
		UE_LOG(LogSpudState, Verbose, TEXT(" * RESTORE Level Actor: %s"), *Actor->GetName())
	}
	else
	{
		ActorData = GetLevelActorData(Actor, LevelData, false);
		UE_LOG(LogSpudState, Verbose, TEXT(" * RESTORE Level Actor: %s"), *Actor->GetName())
	}

	if (ActorData)
	{
		PreRestoreObject(Actor);
		
		RestoreCoreActorData(Actor, ActorData->CoreData);
		RestoreObjectProperties(Actor, ActorData->Properties, LevelData->Metadata, RuntimeObjects);

		PostRestoreObject(Actor, ActorData->CustomData);		
	}
}


void USpudState::PreRestoreObject(UObject* Obj)
{
	if(Obj->GetClass()->ImplementsInterface(USpudObjectCallback::StaticClass()))
	{
		ISpudObjectCallback::Execute_SpudPreRestore(Obj, this);
		
	}
}

void USpudState::PostRestoreObject(UObject* Obj, const FSpudCustomData& FromCustomData)
{
	if (Obj->GetClass()->ImplementsInterface(USpudObjectCallback::StaticClass()))
	{
		FMemoryReader Reader(FromCustomData.Data);
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

	FMemoryReader In(FromData.Data);
	
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
		if (RootComp && RootComp->Mobility == EComponentMobility::Movable)
		{
			// Only set the actor transform if movable, to avoid editor warnings about static/stationary objects
			Actor->SetActorTransform(XForm, false, nullptr, ETeleportType::ResetPhysics);
			
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

void USpudState::RestoreObjectProperties(UObject* Obj, const FSpudPropertyData& FromData, const FSpudClassMetadata& Meta, const TMap<FGuid, UObject*>* RuntimeObjects)
{
	const auto ClassName = GetClassName(Obj);
	const auto ClassDef = Meta.GetClassDef(ClassName);
	if (!ClassDef)
	{
		UE_LOG(LogSpudState, Error, TEXT("Unable to find ClassDef for: %s %s"), *GetClassName(Obj));
		return;
	}

	// We can use the "fast" path if the stored definition of the class properties exactly matches the runtime order
	// ClassDef caches the result of this across the context of one loaded file
	const bool bUseFastPath = ClassDef->MatchesRuntimeClass(Meta);	

	UE_LOG(LogSpudState, Verbose, TEXT(" |- Class: %s"), *ClassDef->ClassName);

	if (bUseFastPath)
		RestoreObjectPropertiesFast(Obj, FromData, Meta, ClassDef, RuntimeObjects);
	else
		RestoreObjectPropertiesSlow(Obj, FromData, Meta, ClassDef, RuntimeObjects);
}


void USpudState::RestoreObjectPropertiesFast(UObject* Obj, const FSpudPropertyData& FromData,
                                                       const FSpudClassMetadata& Meta,
                                                       const FSpudClassDef* ClassDef,
                                                       const TMap<FGuid, UObject*>* RuntimeObjects)
{
	UE_LOG(LogSpudState, Verbose, TEXT(" |- FAST path, %d properties"), ClassDef->Properties.Num());
	const auto StoredPropertyIterator = ClassDef->Properties.CreateConstIterator();

	FMemoryReader In(FromData.Data);
	RestoreFastPropertyVisitor Visitor(StoredPropertyIterator, In, *ClassDef, Meta, RuntimeObjects);
	SpudPropertyUtil::VisitPersistentProperties(Obj, Visitor);
	
}

void USpudState::RestoreObjectPropertiesSlow(UObject* Obj, const FSpudPropertyData& FromData,
                                                       const FSpudClassMetadata& Meta,
                                                       const FSpudClassDef* ClassDef,
                                                       const TMap<FGuid, UObject*>* RuntimeObjects)
{
	UE_LOG(LogSpudState, Verbose, TEXT(" |- SLOW path, %d properties"), ClassDef->Properties.Num());

	FMemoryReader In(FromData.Data);
	RestoreSlowPropertyVisitor Visitor(In, *ClassDef, Meta, RuntimeObjects);
	SpudPropertyUtil::VisitPersistentProperties(Obj, Visitor);
}


uint32 USpudState::RestorePropertyVisitor::GetNestedPrefix(FStructProperty* SProp, uint32 CurrentPrefixID)
{
	// This doesn't create a new ID, expects it to be there already (should be since restoring)
	return SpudPropertyUtil::GetNestedPrefixID(CurrentPrefixID, SProp, Meta);
}


bool USpudState::RestoreFastPropertyVisitor::VisitProperty(UObject* RootObject, FProperty* Property,
	uint32 CurrentPrefixID, void* ContainerPtr, int Depth)
{
	// Fast path can just iterate both sides of properties because stored properties are in the same order
	auto& StoredProperty = *StoredPropertyIterator;
	SpudPropertyUtil::RestoreProperty(RootObject, Property, ContainerPtr, StoredProperty, RuntimeObjects, DataIn);

	// We DON'T increment the property iterator for custom structs, since they don't have any values of their own
	// It's their nested properties that have the values, they're only context
	if (!SpudPropertyUtil::IsCustomStructProperty(Property))
		++StoredPropertyIterator;

	return true;
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
	auto InnerMapPtr = ClassDef.PropertyLookup.Find(CurrentPrefixID);
	if (!InnerMapPtr)
	{
		UE_LOG(LogSpudState, Error, TEXT("Error in RestoreSlowPropertyVisitor, PrefixID invalid for %, class %s"), *Property->GetName(), *ClassDef.ClassName);
		return true;
	}
	
	uint32 PropID = Meta.GetPropertyIDFromName(Property->GetName());
	if (PropID == SPUDDATA_INDEX_NONE)
	{
		UE_LOG(LogSpudState, Warning, TEXT("Skipping property %s on class %s, not found in class definition"), *Property->GetName(), *ClassDef.ClassName);
		return true;
	}
	const int* PropertyIndexPtr = InnerMapPtr->Find(PropID);
	if (!PropertyIndexPtr)
	{
		UE_LOG(LogSpudState, Warning, TEXT("Skipping property %s on class %s, data not found"), *Property->GetName(), *ClassDef.ClassName);
		return true;		
	}
	if (*PropertyIndexPtr < 0 || *PropertyIndexPtr >= ClassDef.Properties.Num())
	{
		UE_LOG(LogSpudState, Error, TEXT("Error in RestoreSlowPropertyVisitor, invalid property index for %s on class %s"), *Property->GetName(), *ClassDef.ClassName);
		return true;		
	}
	auto& StoredProperty = ClassDef.Properties[*PropertyIndexPtr];
	
	SpudPropertyUtil::RestoreProperty(RootObject, Property, ContainerPtr, StoredProperty, RuntimeObjects, DataIn);
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
		PreRestoreObject(Obj);
		
		RestoreObjectProperties(Obj, Data->Properties, SaveData.GlobalData.Metadata, nullptr);

		PostRestoreObject(Obj, Data->CustomData);
	}
	
}
void USpudState::StoreActor(AActor* Actor, FSpudLevelData* LevelData)
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
	TArray<uint8>* pDestPropertyData = nullptr;
	TArray<uint8>* pDestCustomData = nullptr;
	FSpudClassMetadata& Meta = LevelData->Metadata;
	FSpudClassDef& ClassDef = Meta.FindOrAddClassDef(GetClassName(Actor));
	TArray<uint32>* pOffsets = nullptr;
	if (bRespawn)
	{
		auto ActorData = GetSpawnedActorData(Actor, LevelData, true);
		if (ActorData)
		{
			pDestCoreData = &ActorData->CoreData.Data;
			pDestPropertyData = &ActorData->Properties.Data;
			pDestCustomData = &ActorData->CustomData.Data;
			pOffsets = &ActorData->Properties.PropertyOffsets;
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
			pDestPropertyData = &ActorData->Properties.Data;
			pDestCustomData = &ActorData->CustomData.Data;
			pOffsets = &ActorData->Properties.PropertyOffsets;
			Name = ActorData->Name;
		}
	}

	if (!pDestPropertyData || !pOffsets)
	{
		// Something went wrong, we'll assume the detail has been logged elsewhere
		return;	
	}
	

	if (bRespawn)
		UE_LOG(LogSpudState, Verbose, TEXT("* STORE Runtime Actor: %s (%s)"), *Guid.ToString(EGuidFormats::DigitsWithHyphens), *Name)
	else
		UE_LOG(LogSpudState, Verbose, TEXT("* STORE Level Actor: %s/%s"), *LevelData->Name, *Name);

	pDestPropertyData->Empty();
	FMemoryWriter PropertyWriter(*pDestPropertyData);

	bool bIsCallback = Actor->GetClass()->ImplementsInterface(USpudObjectCallback::StaticClass());

	if (bIsCallback)
		ISpudObjectCallback::Execute_SpudPreStore(Actor, this);

	// Core data first
	pDestCoreData->Empty();
	FMemoryWriter CoreDataWriter(*pDestCoreData);
	WriteCoreActorData(Actor, CoreDataWriter);

	// Now properties, visit all and write out
	StorePropertyVisitor Visitor(ClassDef, *pOffsets, Meta, PropertyWriter);
	SpudPropertyUtil::VisitPersistentProperties(Actor, Visitor);

	if (bIsCallback)
	{
		if (pDestCustomData)
		{
			pDestCustomData->Empty();
			FMemoryWriter CustomDataWriter(*pDestCustomData);
			auto CustomDataStruct = NewObject<USpudStateCustomData>();
			CustomDataStruct->Init(&CustomDataWriter);
			ISpudObjectCallback::Execute_SpudStoreCustomData(Actor, this, CustomDataStruct);
		}			
	
		ISpudObjectCallback::Execute_SpudPostStore(Actor, this);
	}
}


void USpudState::StoreLevelActorDestroyed(AActor* Actor, FSpudLevelData* LevelData)
{
	// We don't check for duplicates, because it should only be possible to destroy a uniquely named level actor once
	LevelData->DestroyedActors.Add(SpudPropertyUtil::GetLevelActorName(Actor));
}

void USpudState::SaveToArchive(FArchive& Ar, const FText& Title)
{
	// We use separate read / write in order to more clearly support chunked file format
	// with the backwards compatibility that comes with 
	FSpudChunkedDataArchive ChunkedAr(Ar);
	SaveData.PrepareForWrite(Title);
	// Use WritePaged in all cases; if all data is loaded it amounts to the same thing
	SaveData.WriteToArchive(ChunkedAr, GetActiveGameLevelFolder());

}

void USpudState::LoadFromArchive(FArchive& Ar, bool bFullyLoadAllLevelData)
{
	// Firstly, destroy any active game level files
	RemoveAllActiveGameLevelFiles();
	
	FSpudChunkedDataArchive ChunkedAr(Ar);
	if (bFullyLoadAllLevelData)
		SaveData.ReadFromArchive(ChunkedAr);
	else
		SaveData.ReadFromArchive(ChunkedAr, false, GetActiveGameLevelFolder());
}

bool USpudState::IsLevelDataLoaded(const FString& LevelName)
{
	auto Lvldata = SaveData.GetLevelData(LevelName, false, GetActiveGameLevelFolder());

	if (!Lvldata)
		return false;

	return Lvldata->IsLoaded();
}

bool USpudState::LoadSaveInfoFromArchive(FArchive& Ar, USpudSaveGameInfo& OutInfo)
{
	FSpudChunkedDataArchive ChunkedAr(Ar);
	FSpudSaveInfo StorageInfo;
	const bool Ok = FSpudSaveData::ReadSaveInfoFromArchive(ChunkedAr, StorageInfo);
	if (Ok)
	{
		OutInfo.Title = StorageInfo.Title;
		OutInfo.Timestamp = StorageInfo.Timestamp;
	}
	return Ok;
	
}


FString USpudState::GetActiveGameLevelFolder()
{
	return FString::Printf(TEXT("%sActiveGameCache/"), *FPaths::ProjectSavedDir());	
}

void USpudState::RemoveAllActiveGameLevelFiles()
{
	FSpudSaveData::DeleteAllLevelDataFiles(GetActiveGameLevelFolder());
}


PRAGMA_ENABLE_OPTIMIZATION
