#include "SpudRuntimeStoredActorComponent.h"

#include "WanderingActorTrackerSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(SpudRuntimeStoredActorComponent, All, All);

USpudRuntimeStoredActorComponent::USpudRuntimeStoredActorComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    bAutoActivate = true;
}

void USpudRuntimeStoredActorComponent::BeginPlay()
{
    Super::BeginPlay();

    if (UWanderingActorTrackerSubsystem* Tracker = GetWorld()->GetSubsystem<UWanderingActorTrackerSubsystem>())
        Tracker->RegisterActor(GetOwner());
}

void USpudRuntimeStoredActorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWanderingActorTrackerSubsystem* Tracker = GetWorld()->GetSubsystem<UWanderingActorTrackerSubsystem>())
        Tracker->UnregisterActor(GetOwner());

    Super::EndPlay(EndPlayReason);
}