#include "SpudStreamingVolume.h"

#include "Engine/CollisionProfile.h"
#include "SpudSubsystem.h"
#include "Components/BrushComponent.h"

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

	ActorsOverlapping = 0;
}

void ASpudStreamingVolume::NotifyActorBeginOverlap(AActor* OtherActor)
{
	// This gets called for Cameras and Pawns (I just prefer this to cameras-only for 3rd person setups, having to
	// worry about a distant camera poking out of the volume when the character is still in it)
	// However, only consider player-controlled pawns to avoid AI keeping levels alive
	if (auto Pawn = Cast<APawn>(OtherActor))
	{
		if (!Pawn->IsPlayerControlled())
			return;
	}	
	
	++ActorsOverlapping;
	
	//UE_LOG(LogTemp, Warning, TEXT("Actor: %s begin overlap"), *OtherActor->GetName());
	auto PS = GetSpudSubsystem(GetWorld());
	if (PS)
	{
		for (auto Level : StreamingLevels)
		{
			if (!Level.IsNull())
			{
				// Can't use GetAssetPathName in PIE because it gets prefixed with UEDPIE_0_ for uniqueness with editor version
				FName LevelName = FName(Level.GetAssetName());
				//UE_LOG(LogTemp, Warning, TEXT("Requesting Stream Load: %s"), *Level.GetAssetName());
				PS->AddRequestForStreamingLevel(this, LevelName, false);				
			}
		}
	}
}

void ASpudStreamingVolume::NotifyActorEndOverlap(AActor* OtherActor)
{
	--ActorsOverlapping;
	//UE_LOG(LogTemp, Warning, TEXT("Actor: %s end overlap"), *OtherActor->GetName());

	if (ActorsOverlapping <= 0)
	{
		auto PS = GetSpudSubsystem(GetWorld());
		if (PS)
		{
			for (auto Level : StreamingLevels)
			{
				if (!Level.IsNull())
				{
					// Can't use GetAssetPathName in PIE because it gets prefixed with UEDPIE_0_ for uniqueness with editor version
					FName LevelName = FName(Level.GetAssetName());
					//UE_LOG(LogTemp, Warning, TEXT("Withdrawing Stream Level Request: %s"), *LevelName.ToString());
					PS->WithdrawRequestForStreamingLevel(this, LevelName);				
				}
			}
		}		
		
	}
}
