// (c) 2024 Anxious Cat Studio


#include "SpudRuntimeStoredActorComponent.h"

#include "SpudSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionSubsystem.h"


USpudRuntimeStoredActorComponent::USpudRuntimeStoredActorComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;

    bAutoActivate = true;
}

void USpudRuntimeStoredActorComponent::BeginPlay()
{
    Super::BeginPlay();

    if (bCanCrossCell)
    {
        return;
    }

    UpdateCurrentCell();

    if (bCurrentCellLoaded)
    {
        GetSpudSubsystem(GetWorld())->OnLevelStore.AddDynamic(this, &ThisClass::OnPreUnloadCell);
    }
}

void USpudRuntimeStoredActorComponent::UpdateCurrentCell()
{
    const UWorldPartitionRuntimeCell* OutOverlappedCell = nullptr;
    GetCurrentOverlappedCell(OutOverlappedCell);

    if (OutOverlappedCell)
    {
        CurrentCellName = OutOverlappedCell->GetName();
        bCurrentCellLoaded = OutOverlappedCell->GetCurrentState() == EWorldPartitionRuntimeCellState::Activated;
    }
}

// ReSharper disable once CppMemberFunctionMayBeConst
void USpudRuntimeStoredActorComponent::OnPreUnloadCell(const FString& LevelName)
{
    if (CurrentCellName.IsEmpty() || !IsActive())
    {
        return;
    }

    if (CurrentCellName == LevelName)
    {
        GetSpudSubsystem(GetWorld())->StoreActorByCell(GetOwner(), CurrentCellName);
    }
}

void USpudRuntimeStoredActorComponent::GetCurrentOverlappedCell(
    const UWorldPartitionRuntimeCell*& CurrentOverlappedCell) const
{
    const auto WorldPartitionSubsystem = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
    if (!WorldPartitionSubsystem)
    {
        return;
    }

    const auto ForEachCellFunction = [this, &CurrentOverlappedCell](const UWorldPartitionRuntimeCell* Cell) -> bool
    {
        if (Cell->GetCellBounds().IsInsideXY(GetOwner()->GetActorLocation()))
        {
            CurrentOverlappedCell = Cell;
        }
        return true;
    };

    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    auto ForEachWpFunction = [ForEachCellFunction](UWorldPartition* WorldPartition) -> bool
    {
        if (WorldPartition)
        {
            WorldPartition->RuntimeHash->ForEachStreamingCells(ForEachCellFunction);
        }

        return true;
    };

    WorldPartitionSubsystem->ForEachWorldPartition(ForEachWpFunction);
}

void USpudRuntimeStoredActorComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                                     FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (bCanCrossCell)
    {
        UpdateCurrentCell();
        if (!bCurrentCellLoaded)
        {
            GetSpudSubsystem(GetWorld())->StoreActorByCell(GetOwner(), CurrentCellName);
        }
    }
}

void USpudRuntimeStoredActorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetSpudSubsystem(GetWorld())->OnLevelStore.RemoveDynamic(this, &ThisClass::OnPreUnloadCell);

    Super::EndPlay(EndPlayReason);
}
