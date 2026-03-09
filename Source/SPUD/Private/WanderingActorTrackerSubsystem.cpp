#include "WanderingActorTrackerSubsystem.h"

#include "SpudSubsystem.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

// Console variable to toggle debug drawing of WP cell cache bounds at runtime
// Usage: WanderingActorTracker.DebugDrawCells 1
static TAutoConsoleVariable<bool> CVarDebugDrawCells(
    TEXT("WanderingActorTracker.DebugDrawCells"),
    false,
    TEXT("Draw debug boxes for WP cell cache")
);

void UWanderingActorTrackerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    const UGameInstance* GI = GetWorld()->GetGameInstance();
    if (!GI) return;

    CachedSpudSubsystem = GI->GetSubsystem<USpudSubsystem>();

    if (USpudSubsystem* Spud = CachedSpudSubsystem.Get())
    {
        // OnLevelStore fires when SPUD saves a specific streaming level
        Spud->OnLevelStore.AddDynamic(this, &ThisClass::OnLevelStore);
        // Track streaming level load/unload to keep cell state cache up to date
        Spud->PostLoadStreamingLevel.AddDynamic(this, &ThisClass::OnPostLoadStreamingLevel);
        Spud->PostUnloadStreamingLevel.AddDynamic(this, &ThisClass::OnPostUnloadStreamingLevel);
    }
}

void UWanderingActorTrackerSubsystem::Deinitialize()
{
    if (USpudSubsystem* Spud = CachedSpudSubsystem.Get())
    {
        Spud->OnLevelStore.RemoveDynamic(this, &ThisClass::OnLevelStore);
        Spud->PostLoadStreamingLevel.RemoveDynamic(this, &ThisClass::OnPostLoadStreamingLevel);
        Spud->PostUnloadStreamingLevel.RemoveDynamic(this, &ThisClass::OnPostUnloadStreamingLevel);
    }

    CachedSpudSubsystem = nullptr;
    TrackedActors.Empty();
    CellCache.Empty();

    Super::Deinitialize();
}

// Only create this subsystem in game worlds
bool UWanderingActorTrackerSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
    const UWorld* World = Cast<UWorld>(Outer);
    return World && World->IsGameWorld();
}

void UWanderingActorTrackerSubsystem::RegisterActor(AActor* Actor)
{
    if (!Actor) return;

    // Prevent duplicate registration
    if (TrackedActors.ContainsByPredicate([Actor](const FTrackedActor& T) { return T.Actor == Actor; }))
        return;

    FTrackedActor& NewTracked = TrackedActors.AddDefaulted_GetRef();
    NewTracked.Actor = Actor;
    // LastValidCellName will be populated on the first Tick
}

void UWanderingActorTrackerSubsystem::UnregisterActor(AActor* Actor)
{
    if (!Actor) return;

    TrackedActors.RemoveAll([Actor](const FTrackedActor& T) { return T.Actor == Actor; });
}

// Called by SPUD when it is about to save a specific streaming level.
// We store each tracked actor into whichever cell it physically occupies,
// falling back to LastValidCellName if no cell is found at the current location.
void UWanderingActorTrackerSubsystem::OnLevelStore(const FString& LevelName)
{
    USpudSubsystem* Spud = CachedSpudSubsystem.Get();
    if (!Spud) return;

    for (auto& [Actor, LastValidCellName] : TrackedActors)
    {
        if (!Actor.IsValid()) continue;

        // Find the physical cell the actor is currently inside
        FString CurrentCell;
        bool bIsActivated = false;
        FindCellForLocation(Actor->GetActorLocation(), CurrentCell, bIsActivated);

        // Prefer the physical cell; fall back to last known valid cell
        const FString& TargetCell = !CurrentCell.IsEmpty()
            ? CurrentCell
            : LastValidCellName;

        if (TargetCell.IsEmpty()) continue;

        // Only store if this is the level SPUD is currently saving
        if (TargetCell != LevelName) continue;

        Spud->StoreActorByCell(Actor.Get(), TargetCell);
    }
}

void UWanderingActorTrackerSubsystem::OnPostUnloadStreamingLevel(const FName& LevelName)
{
    OnStreamingStateUpdated();
}

void UWanderingActorTrackerSubsystem::OnPostLoadStreamingLevel(const FName& LevelName)
{
    OnStreamingStateUpdated();
}

// Keeps the cell state cache in sync with the current WP streaming state.
// If the number of valid cells has changed, performs a full rebuild.
// Otherwise just refreshes the State field of each cached entry.
void UWanderingActorTrackerSubsystem::OnStreamingStateUpdated()
{
    UWorldPartitionSubsystem* WorldPartitionSubsystem = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
    if (!WorldPartitionSubsystem) return;

    // Count only cells with valid content
    int32 ActualCellCount = 0;
    WorldPartitionSubsystem->ForEachWorldPartition([&ActualCellCount](const UWorldPartition* WorldPartition) -> bool
    {
        if (WorldPartition)
            WorldPartition->RuntimeHash->ForEachStreamingCells([&ActualCellCount](const UWorldPartitionRuntimeCell* Cell) -> bool
            {
                if (Cell && Cell->GetContentBounds().IsValid)
                    ++ActualCellCount;
                return true;
            });
        return true;
    });

    // Cell count mismatch means cells were added or removed
    if (ActualCellCount != CellCache.Num())
    {
        RebuildCellCache();
        return;
    }

    // Update only the streaming state of each cached cell
    for (auto& Data : CellCache)
        Data.State = Data.Cell->GetCurrentState();
}

// Rebuilds the cell cache from scratch by iterating all WP streaming cells.
// Cells without valid content bounds are skipped
void UWanderingActorTrackerSubsystem::RebuildCellCache()
{
    CellCache.Empty();

    UWorldPartitionSubsystem* WorldPartitionSubsystem = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
    if (!WorldPartitionSubsystem) return;

    auto ForEachCellFunction = [this](const UWorldPartitionRuntimeCell* Cell) -> bool
    {
        if (Cell)
        {
            // Skip cells with no content
            if (!Cell->GetContentBounds().IsValid) return true;

            FCachedCellData& Data = CellCache.AddDefaulted_GetRef();
            Data.Cell = Cell;
            Data.Bounds = Cell->GetCellBounds();
            Data.LevelName = USpudState::GetLevelName(Cell);
            Data.State = Cell->GetCurrentState();
        }
        return true;
    };

    auto ForEachWPFunction = [&ForEachCellFunction](const UWorldPartition* WorldPartition) -> bool
    {
        if (WorldPartition)
            WorldPartition->RuntimeHash->ForEachStreamingCells(ForEachCellFunction);
        return true;
    };

    WorldPartitionSubsystem->ForEachWorldPartition(ForEachWPFunction);
}

// Finds the most specific WP cell that contains the given location.
// "Most specific" = smallest XY area (e.g. a house cell inside a landscape cell).
bool UWanderingActorTrackerSubsystem::FindCellForLocation(
    const FVector& Location,
    FString& OutCellName,
    bool& OutIsActivated) const
{
    float SmallestArea = 0.f;
    int32 BestIndex = INDEX_NONE;

    for (int32 i = 0; i < CellCache.Num(); ++i)
    {
        const FCachedCellData& Data = CellCache[i];

        if (!Data.Bounds.IsInside(Location)) continue;

        const FVector Size = Data.Bounds.GetSize();
        const float Area = Size.X * Size.Y;

        if (BestIndex == INDEX_NONE || Area < SmallestArea)
        {
            SmallestArea = Area;
            BestIndex = i;
        }
    }

    if (BestIndex == INDEX_NONE)
        return false;

    OutCellName    = CellCache[BestIndex].LevelName;
    OutIsActivated = CellCache[BestIndex].State == EWorldPartitionRuntimeCellState::Activated;
    return true;
}

// Clamps the actor's location to within the bounds of the given cell (with a small inset).
// This ensures the actor restores strictly inside its cell and doesn't immediately
// trigger the unload logic due to a boundary position.
void UWanderingActorTrackerSubsystem::ClampActorToCell(AActor* Actor, const FString& CellName) const
{
    const FCachedCellData* TargetCell = CellCache.FindByPredicate([&CellName](const FCachedCellData& Data)
    {
        return Data.LevelName == CellName;
    });

    if (!TargetCell) return;

    // Small inset to avoid placing the actor exactly on the cell boundary
    static constexpr float Inset = 10.f;

    const FVector Location = Actor->GetActorLocation();
    const FVector Clamped  = FVector(
        FMath::Clamp(Location.X, TargetCell->Bounds.Min.X + Inset, TargetCell->Bounds.Max.X - Inset),
        FMath::Clamp(Location.Y, TargetCell->Bounds.Min.Y + Inset, TargetCell->Bounds.Max.Y - Inset),
        FMath::Clamp(Location.Z, TargetCell->Bounds.Min.Z + Inset, TargetCell->Bounds.Max.Z - Inset)
    );

    if (!Clamped.Equals(Location))
        Actor->SetActorLocation(Clamped);
}

// Clamps the actor into its target cell, stores it in SPUD, then queues it for destruction.
// Actual Destroy() is deferred to after the Tick loop to avoid invalidating iterators.
void UWanderingActorTrackerSubsystem::SaveAndDestroyActor(
    FTrackedActor& Tracked,
    const FString& CellName,
    USpudSubsystem* Spud,
    TArray<AActor*>& OutActorsToDestroy)
{
    ClampActorToCell(Tracked.Actor.Get(), CellName);

    if (Spud)
        Spud->StoreActorByCell(Tracked.Actor.Get(), CellName);

    OutActorsToDestroy.Add(Tracked.Actor.Get());
}

void UWanderingActorTrackerSubsystem::Tick(float DeltaTime)
{
    // Only tick on the authority
    if (!GetWorld()->GetAuthGameMode()) return;

    USpudSubsystem* Spud = CachedSpudSubsystem.Get();
    if (!Spud) return;

    // Collect actors to destroy after the loop to avoid iterator invalidation
    TArray<AActor*> ActorsToDestroy;

    for (auto It = TrackedActors.CreateIterator(); It; ++It)
    {
        FTrackedActor& Tracked = *It;
        
        if (!Tracked.Actor.IsValid())
        {
            It.RemoveCurrent();
            continue;
        }

        FString CurrentCell;
        bool bIsActivated = false;
        const bool bFound = FindCellForLocation(Tracked.Actor->GetActorLocation(), CurrentCell, bIsActivated);

        if (bFound && bIsActivated)
        {
            // If Actor is in an active cell, update last known valid cell
            Tracked.LastValidCellName = CurrentCell;
        }
        else if (bFound && !bIsActivated)
        {
            //If actor's cell is inactive, save into that cell and destroy
            SaveAndDestroyActor(Tracked, CurrentCell, Spud, ActorsToDestroy);
        }
        else if (!bFound && !Tracked.LastValidCellName.IsEmpty())
        {
            // IF actor is outside all cell bounds, fall back to last known cell
            SaveAndDestroyActor(Tracked, Tracked.LastValidCellName, Spud, ActorsToDestroy);
        }
        else
        {
            //If no cell found and no fallback, cannot save, log a warning
            UE_LOG(LogTemp, Warning, TEXT("WanderingActorTracker: %s has no valid cell, cannot save"),
                *Tracked.Actor->GetName());
        }
    }

    // Destroy queued actors —
    for (AActor* Actor : ActorsToDestroy)
    {
        // Suppress network replication before destroying
        Actor->SetNetDormancy(DORM_DormantAll);
        Actor->Destroy();
    }

#if ENABLE_DRAW_DEBUG
    // Visualize cell cache bounds
    // Enable via console: WanderingActorTracker.DebugDrawCells 1
    if (CVarDebugDrawCells.GetValueOnGameThread())
    {
        for (const FCachedCellData& Data : CellCache)
        {
            const FColor Color = Data.State == EWorldPartitionRuntimeCellState::Activated
                ? FColor::Green
                : FColor::Red;

            DrawDebugBox(GetWorld(),
                Data.Bounds.GetCenter(),
                Data.Bounds.GetExtent(),
                Color,
                false,
                0.f,
                0,
                50.f
            );
        }
    }
#endif
}