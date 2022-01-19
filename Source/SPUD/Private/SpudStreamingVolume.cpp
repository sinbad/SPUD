#include "SpudStreamingVolume.h"

#include "Engine/CollisionProfile.h"
#include "SpudSubsystem.h"
#include "Components/BrushComponent.h"
#include "Kismet/GameplayStatics.h"

ASpudStreamingVolume::ASpudStreamingVolume(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
	// unlike the standard streaming volume, we're going to use pawns *and* cameras to load
	auto BC = GetBrushComponent();
	BC->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
	BC->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);
	BC->SetCollisionResponseToChannel(ECollisionChannel::ECC_Pawn, ECollisionResponse::ECR_Overlap);
	BC->SetCollisionResponseToChannel(ECollisionChannel::ECC_Camera, ECollisionResponse::ECR_Overlap);
	BC->bAlwaysCreatePhysicsState = true;

	bColored = true;
	BrushColor.R = 255;
	BrushColor.G = 165;
	BrushColor.B = 0;
	BrushColor.A = 255;
}

void ASpudStreamingVolume::BeginPlay()
{
	Super::BeginPlay();

	// So, there's a problem with pawns. It's possible that a pawn has its collision enabled when it's not currently
	// possessed, which triggers the overlap event, but we don't care about it yet because it's not player controlled.
	// Similarly if a possessed pawn which is overlapping this volume is then unpossessed, we need to unsub the level.
	// So we need to be told when a pawn is is possessed or unpossessed to be able to close this loophole.
	auto GI = GetWorld()->GetGameInstance();
	if (GI)
	{
		GI->GetOnPawnControllerChanged().AddDynamic(this, &ASpudStreamingVolume::OnPawnControllerChanged);
	}
	
}

void ASpudStreamingVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	auto GI = GetWorld()->GetGameInstance();
	if (GI)
	{
		GI->GetOnPawnControllerChanged().RemoveDynamic(this, &ASpudStreamingVolume::OnPawnControllerChanged);
	}
	
}

bool ASpudStreamingVolume::IsRelevantActor(AActor* Actor) const
{
	// This gets called for Cameras and Pawns (I just prefer this to cameras-only for 3rd person setups, having to
	// worry about a distant camera poking out of the volume when the character is still in it)
	// However, only consider player-controlled pawns to avoid AI keeping levels alive
	if (auto Pawn = Cast<APawn>(Actor))
	{
		return Pawn->IsPlayerControlled();
	}

	// Must be a camera
	return true;
	
}

void ASpudStreamingVolume::OnPawnControllerChanged(APawn* Pawn, AController* NewCtrl)
{
	// If player controlled and already overlapping...
	// This means becoming the possessed pawn when pawn already overlapped so was potentially previously ignored
	// If already overlapping, this might change the decision of whether relevant
	if (PawnsInVolume.Contains(Pawn))
	{
		if (IsRelevantActor(Pawn))
			AddRelevantActor(Pawn);
		else
			RemoveRelevantActor(Pawn);
	}
	
}

void ASpudStreamingVolume::NotifyActorBeginOverlap(AActor* OtherActor)
{
	if (auto Pawn = Cast<APawn>(OtherActor))
	{
		// We need to track ALL pawns in the area in case they get possessed / unpossessed
		PawnsInVolume.Add(Pawn);
	}
	
	if (!IsRelevantActor(OtherActor))
		return;

	AddRelevantActor(OtherActor);
}

void ASpudStreamingVolume::NotifyActorEndOverlap(AActor* OtherActor)
{
	if (auto Pawn = Cast<APawn>(OtherActor))
	{
		// We need to track ALL pawns in the area in case they get possessed / unpossessed
		PawnsInVolume.Remove(Pawn);
	}

	if (!IsRelevantActor(OtherActor))
		return;

	RemoveRelevantActor(OtherActor);

}


void ASpudStreamingVolume::AddRelevantActor(AActor* Actor)
{
	const int OldNum = RelevantActorsInVolume.Num();
	
	RelevantActorsInVolume.AddUnique(Actor);

	// Shouldn't need to listen in to actor destruction, that will trigger end overlap

	if (OldNum == 0)
	{
		auto PS = GetSpudSubsystem(GetWorld());
		if (PS)
		{
			for (auto Level : StreamingLevels)
			{
				if (!Level.IsNull())
				{
					// Can't use GetAssetPathName in PIE because it gets prefixed with UEDPIE_0_ for uniqueness with editor version
					const FName LevelName = FName(Level.GetAssetName());
					//UE_LOG(LogTemp, Verbose, TEXT("Requesting Stream Load: %s"), *Level.GetAssetName());
					PS->AddRequestForStreamingLevel(this, LevelName, false);				
				}
			}
		}
	}
}

void ASpudStreamingVolume::RemoveRelevantActor(AActor* Actor)
{
	const int Removed = RelevantActorsInVolume.Remove(Actor);

	if (Removed > 0 && RelevantActorsInVolume.Num() == 0)
	{
		auto PS = GetSpudSubsystem(GetWorld());
		if (PS)
		{
			for (auto Level : StreamingLevels)
			{
				if (!Level.IsNull())
				{
					// Can't use GetAssetPathName in PIE because it gets prefixed with UEDPIE_0_ for uniqueness with editor version
					const FName LevelName = FName(Level.GetAssetName());
					//UE_LOG(LogTemp, Verbose, TEXT("Withdrawing Stream Level Request: %s"), *LevelName.ToString());
					PS->WithdrawRequestForStreamingLevel(this, LevelName);				
				}
			}
		}		
	
	}
}
