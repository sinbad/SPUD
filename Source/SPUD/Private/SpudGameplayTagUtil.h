#pragma once
#include "GameplayTagContainer.h"


/// Container for private gameplay tag data / functions
/// Separate from USpudGameplayTags because that's exposed and we don't
/// want to force a GameplayTags module dependency on all C++ users if they don't use it
struct FSpudGameplayTagUtil
{
protected:
	static FGameplayTag AlwaysRespawnTag;
	static FGameplayTag NeverRespawnTag;
	static FGameplayTag NoRestoreTransformTag;
	static FGameplayTag NoRestoreVelocityTag;

public:

	// Called by module startup
	static void RegisterTags();

	static bool ActorHasAlwaysRespawnTag(const AActor* Actor);
	static bool ActorHasNeverRespawnTag(const AActor* Actor);
	static bool ActorHasNoRestoreTransformTag(const AActor* Actor);
	static bool ActorHasNoRestoreVelocityTag(const AActor* Actor);

};
