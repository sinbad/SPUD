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

    /// Force updating current cell.
    UFUNCTION(BlueprintCallable)
    void UpdateCurrentCell();

    /**
     * Can the owning actor can cross cells? Ticking will be enabled if this flag is true. If the position changes
     * infrequently, it's more efficient to manually call UpdateCurrentCell().
     */
    UPROPERTY(EditDefaultsOnly)
    bool bCanCrossCell = false;

protected:
    virtual void BeginPlay() override;

private:
    FString CurrentCellName;

    UFUNCTION()
    void OnLevelStore(const FString& LevelName);

    UFUNCTION()
    void OnPostUnloadCell(const FName& LevelName);

    void GetCurrentOverlappedCell(const UWorldPartitionRuntimeCell*& CurrentOverlappedCell) const;
};
