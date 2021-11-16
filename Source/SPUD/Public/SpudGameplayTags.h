#pragma once

#include "CoreMinimal.h"
#include "SpudGameplayTags.generated.h"

/// Container for gameplay tags
/// Note that to avoid forcing users to add the GameplayTags module if they don't
/// use it, these are exposed just a FNames. If we exposed them as FGameplayTag in our
/// public API, everyone using C++ would have to add GameplayTags module to their Build.cs.
/// So instead, if you use GameplayTags, use RequestGameplayTag with these names when you
/// need them. They will have been registered already.
UCLASS()
class SPUD_API USpudGameplayTags : public UObject
{
	GENERATED_BODY()

public:
	static const FName AlwaysRespawn;
	static const FName NeverRespawn;
	static const FName NoRestoreTransform;
	static const FName NoRestoreVelocity;

	UFUNCTION(BlueprintCallable, BlueprintPure)
	static const FName& GetSpudAlwaysRespawnTagName() { return AlwaysRespawn; }
	UFUNCTION(BlueprintCallable, BlueprintPure)
	static const FName& GetSpudNeverRespawnTagName() { return NeverRespawn; }
	UFUNCTION(BlueprintCallable, BlueprintPure)
	static const FName& GetSpudNoRestoreTransformTagName() { return NoRestoreTransform; }
	UFUNCTION(BlueprintCallable, BlueprintPure)
	static const FName& GetSpudNoRestoreVelocityTagName() { return NoRestoreVelocity; }

};