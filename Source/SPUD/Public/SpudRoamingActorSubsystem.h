#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldPartition/WorldPartitionLevelStreamingPolicy.h"
#include "SpudRoamingActorSubsystem.generated.h"

class USpudSubsystem;

UCLASS()
class SPUD_API USpudRoamingActorSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "RoamingActorSubsystem")
    void RegisterActor(AActor* Actor);

    UFUNCTION(BlueprintCallable, Category = "RoamingActorSubsystem")
    void UnregisterActor(AActor* Actor);

protected:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(USpudRoamingActorSubsystem , STATGROUP_Tickables);
    }

    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickableWhenPaused() const override { return false; }

    UFUNCTION()
    void OnLevelStore(const FString& LevelName);
    
    UFUNCTION()
    void OnPostLoadStreamingLevel(const FName& LevelName);
    
    UFUNCTION()
    void OnPostUnloadStreamingLevel(const FName& LevelName);
    
    UFUNCTION()
    void OnPreUnloadStreamingLevel(const FName& LevelName);

private:
    struct FTrackedActor
    {
        TWeakObjectPtr<AActor> Actor;
        FString LastValidCellName;
    };

    struct FCachedCellData
    {
        const UWorldPartitionRuntimeCell* Cell;
        FBox Bounds;
        FString LevelName;
        EWorldPartitionRuntimeCellState State;
        bool bPendingUnload = false;
    };

    TArray<FTrackedActor> TrackedActors;
    TArray<FCachedCellData> CellCache;
    TWeakObjectPtr<USpudSubsystem> CachedSpudSubsystem;
    
    void OnStreamingStateUpdated();
    void RebuildCellCache();
    bool FindCellForLocation(const FVector& Location, FString& OutCellName, bool& OutIsActivated) const;
    void ClampActorToCell(AActor* Actor, const FString& CellName) const;
    void SaveAndDestroyActor(FTrackedActor& Tracked, const FString& CellName, USpudSubsystem* Spud, TArray<AActor*>& OutActorsToDestroy);
};