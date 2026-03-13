#include "SpudRuntimeStoredActorComponent.h"

#include "SpudRoamingActorSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(SpudRuntimeStoredActorComponent, All, All);

USpudRuntimeStoredActorComponent::USpudRuntimeStoredActorComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    bAutoActivate = true;
}

void USpudRuntimeStoredActorComponent::BeginPlay()
{
    Super::BeginPlay();

    if (USpudRoamingActorSubsystem* Tracker = GetWorld()->GetSubsystem<USpudRoamingActorSubsystem>())
        Tracker->RegisterActor(GetOwner());
}

void USpudRuntimeStoredActorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (USpudRoamingActorSubsystem* Tracker = GetWorld()->GetSubsystem<USpudRoamingActorSubsystem>())
        Tracker->UnregisterActor(GetOwner());

    Super::EndPlay(EndPlayReason);
}