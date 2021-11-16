#include "SpudGameplayTagUtil.h"

#include "GameplayTagsManager.h"
#include "SpudGameplayTags.h"

FGameplayTag FSpudGameplayTagUtil::AlwaysRespawnTag;
FGameplayTag FSpudGameplayTagUtil::NeverRespawnTag;
FGameplayTag FSpudGameplayTagUtil::NoRestoreTransformTag;
FGameplayTag FSpudGameplayTagUtil::NoRestoreVelocityTag;

void FSpudGameplayTagUtil::RegisterTags()
{
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	AlwaysRespawnTag = Manager.AddNativeGameplayTag(USpudGameplayTags::AlwaysRespawn, TEXT("When restoring this runtime spawned object, always respawn even if it's a class we wouldn't normally"));
	NeverRespawnTag = Manager.AddNativeGameplayTag(USpudGameplayTags::NeverRespawn, TEXT("When restoring a runtime spawned object, never respawn it"));
	NoRestoreTransformTag = Manager.AddNativeGameplayTag(USpudGameplayTags::NoRestoreTransform, TEXT("When restoring this object, never restore its transform"));
	NoRestoreVelocityTag = Manager.AddNativeGameplayTag(USpudGameplayTags::NoRestoreVelocity, TEXT("When restoring this object, never restore its velocity"));
}

bool FSpudGameplayTagUtil::ActorHasAlwaysRespawnTag(const AActor* Actor)
{
	// The problem with this is that in order to get the gameplay tag container we use, we need the user
	// to expose it to us. IGameplayTagAssetInterface would do it, but it's C++ only (marked CannotImplementInterfaceInBlueprint)
	// So we're right back to needing C++ implementations again.
	// That's why I'm abandoning this work, it's not any easier to implement in pure Blueprints
	return false;
}

bool FSpudGameplayTagUtil::ActorHasNeverRespawnTag(const AActor* Actor)
{
	return false;
}

bool FSpudGameplayTagUtil::ActorHasNoRestoreTransformTag(const AActor* Actor)
{
	return false;
}

bool FSpudGameplayTagUtil::ActorHasNoRestoreVelocityTag(const AActor* Actor)
{
	return false;
}
