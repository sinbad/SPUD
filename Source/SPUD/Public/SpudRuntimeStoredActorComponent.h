// (c) 2024 Anxious Cat Studio

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpudRuntimeStoredActorComponent.generated.h"


/**
 * Tracks runtime-spawned actors across world partition cell loads/unloads. Assumes the owning actor contains the
 * SpudGuid property.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class SPUD_API USpudRuntimeStoredActorComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USpudRuntimeStoredActorComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
    virtual void BeginPlay() override;

private:
    /// Can the owning actor can cross cells? Ticking should be enabled if this flag is true.
    UPROPERTY(EditDefaultsOnly)
    bool bCanCrossCell = false;

    FString CurrentCellName;
    bool bCurrentCellLoaded = false;

    void UpdateCurrentCell();

    UFUNCTION()
    void OnPreUnloadCell(const FString& LevelName);

    void GetCurrentOverlappedCell(const UWorldPartitionRuntimeCell*& CurrentOverlappedCell) const;
};
