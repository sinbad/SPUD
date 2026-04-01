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
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
