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

    /**
     * Can the owning actor can cross cells? Ticking will be enabled if this flag is true. If the position changes
     * infrequently, it's more efficient to manually call UpdateCurrentCell().
     */
    UPROPERTY(EditDefaultsOnly)
    bool bCanCrossCell = false;

    FString CurrentCellName;
    
    void UpdateCurrentCell(bool& CellActivated);
    void DestroyActor();
    
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void OnLevelStore(const FString& LevelName);

    UFUNCTION()
    void OnPostUnloadCell(const FName& LevelName);

    UFUNCTION()
    void OnPreUnloadCell(const FName& LevelName);
    
    void GetCurrentOverlappedCell(const UWorldPartitionRuntimeCell*& CurrentOverlappedCell) const;
};
