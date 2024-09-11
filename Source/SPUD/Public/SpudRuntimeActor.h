// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ISpudObject.h"
#include "GameFramework/Actor.h"
#include "SpudRuntimeActor.generated.h"

UCLASS(Blueprintable)
class SPUD_API ASpudRuntimeActor : public AActor, public ISpudObject
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	
public:
	ASpudRuntimeActor();

	UFUNCTION()
	void OnPreUnloadCell(const FString& LevelName);
	
	void GetCurrentOverlappedCell(const UWorldPartitionRuntimeCell*& CurrentOverlappedCell) const;
	
	virtual void Tick(float DeltaTime) override;
	
	// Update current cell by actor's location
	void UpdateCurrentCell();
	
	// Used to identify unique actor
	UPROPERTY()
	FGuid SpudGuid;

	// Used to debug
	UFUNCTION(BlueprintCallable)
	FString GetCurrentLevelName();

	UFUNCTION(BlueprintCallable)
	void SaveCurrentActor();
	
	UPROPERTY(BlueprintReadOnly, EditDefaultsOnly)
	bool bCanCrossCell = false;
	
	UPROPERTY(BlueprintReadOnly, Transient)
	FString CurrentCellName;

	UPROPERTY(BlueprintReadOnly, Transient)
	bool bCurrentCellLoaded = false;
};
