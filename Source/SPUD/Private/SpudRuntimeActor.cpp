// Fill out your copyright notice in the Description page of Project Settings.


#include "SpudRuntimeActor.h"

#include "SpudSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

void ASpudRuntimeActor::BeginPlay()
{
	Super::BeginPlay();

	if (bCanCrossCell)
	{
		return;
	}
	
	UpdateCurrentCell();
	if (bCurrentCellLoaded)
	{
		GetSpudSubsystem(GetWorld())->OnLevelStore.AddDynamic(this, &ASpudRuntimeActor::OnPreUnloadCell);
	}
}

void ASpudRuntimeActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	PrimaryActorTick.bCanEverTick = bCanCrossCell;
	PrimaryActorTick.bStartWithTickEnabled = bCanCrossCell;
}

ASpudRuntimeActor::ASpudRuntimeActor()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASpudRuntimeActor::OnPreUnloadCell(const FString& LevelName)
{
	if (CurrentCellName.IsEmpty())
	{
		return;
	}
	
	if (CurrentCellName == LevelName)
	{
		GetSpudSubsystem(GetWorld())->StoreActorByCell(this, CurrentCellName);
		Destroy();
	}
}

void ASpudRuntimeActor::GetCurrentOverlappedCell(const UWorldPartitionRuntimeCell* &CurrentOverlappedCell) const
{
	const auto WorldPartitionSubsystem = GetWorld()->GetSubsystem<UWorldPartitionSubsystem>();
	if (!WorldPartitionSubsystem)
	{
		// Assuming that all collisions are loaded if not using WorldPartition.
		return;
	}
	
	auto ForEachCellFunction = [this, &CurrentOverlappedCell](const UWorldPartitionRuntimeCell* Cell) -> bool
	{
		if (Cell->GetCellBounds().IsInsideXY(GetActorLocation()))
		{
			CurrentOverlappedCell = Cell;
		}
		return true;
	};
	
	// ReSharper disable once CppParameterMayBeConstPtrOrRef
	auto ForEachWPFunction = [ForEachCellFunction](UWorldPartition* WorldPartition) -> bool
	{
		if (WorldPartition)
		{
			WorldPartition->RuntimeHash->ForEachStreamingCells(ForEachCellFunction);
		}
		return true;
	};

	WorldPartitionSubsystem->ForEachWorldPartition(ForEachWPFunction);
}

FString ASpudRuntimeActor::GetCurrentLevelName()
{
	return CurrentCellName;
}

void ASpudRuntimeActor::SaveCurrentActor()
{
	if (!CurrentCellName.IsEmpty())
	{
		GetSpudSubsystem(GetWorld())->StoreActorByCell(this, CurrentCellName);
	}
}

void ASpudRuntimeActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bCanCrossCell)
	{
		UpdateCurrentCell();
		if (!bCurrentCellLoaded)
		{
			GetSpudSubsystem(GetWorld())->StoreActorByCell(this, CurrentCellName);
			Destroy();
		}
	}
}

void ASpudRuntimeActor::UpdateCurrentCell()
{
	const UWorldPartitionRuntimeCell* OutOverlappedCell;
	GetCurrentOverlappedCell(OutOverlappedCell);

	if (OutOverlappedCell)
	{
		CurrentCellName = OutOverlappedCell->GetName();
		bCurrentCellLoaded = OutOverlappedCell->GetCurrentState() == EWorldPartitionRuntimeCellState::Activated;
	}
}
