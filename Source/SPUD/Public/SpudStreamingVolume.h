#pragma once

#include "CoreMinimal.h"
#include "SpudStreamingVolume.generated.h"

/// Drop-in replacement for ALevelStreamingVolume which has a number of advantages:
/// 1. Communicates with SpudSubsystem to organise the streaming in/out of levels such that they get persisted correctly
/// 2. Responds to both cameras and player-controlled pawns, making setup easier for 3rd person cameras (less prone to camera popping out of intuitive volumes)
/// 3. Linked streaming levels are editable directly in the volume, instead of the weird backwards system of pointing the level at the volume
UCLASS(Blueprintable, ClassGroup="SPUD", HideCategories=(Advanced, Attachment, Collision, Volume, Navigation))
class SPUD_API ASpudStreamingVolume : public AVolume
{
	GENERATED_BODY()

protected:

	UPROPERTY(Category=LevelStreamingVolume, EditAnywhere, BlueprintReadOnly, meta=(DisplayName = "Streaming Levels", AllowedClasses="World"))
	TArray<FSoftObjectPath> StreamingLevels;

	UPROPERTY()
	TArray<AActor*> RelevantActorsInVolume;

	UPROPERTY()
	TArray<APawn*> PawnsInVolume;
	
	ASpudStreamingVolume(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	bool IsRelevantActor(AActor* Actor) const;
	void AddRelevantActor(AActor* Actor);
	void RemoveRelevantActor(AActor* Actor);

	UFUNCTION()
	void OnPawnControllerChanged(APawn* Pawn, AController* NewCtrl);

public:

	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;
	virtual void NotifyActorEndOverlap(AActor* OtherActor) override;
};
