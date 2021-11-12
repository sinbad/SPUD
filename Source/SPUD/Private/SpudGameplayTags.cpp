#include "SpudGameplayTags.h"
#include "GameplayTagsManager.h"

const FName USpudGameplayTags::AlwaysRespawn = FName("Spud.AlwaysRespawn");
const FName USpudGameplayTags::NeverRespawn = FName("Spud.NeverRespawn");
const FName USpudGameplayTags::NoRestoreTransform = FName("Spud.NoRestoreTransform");
const FName USpudGameplayTags::NoRestoreVelocity = FName("Spud.NoRestoreVelocity");

void USpudGameplayTags::RegisterTags()
{
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	Manager.AddNativeGameplayTag(AlwaysRespawn, TEXT("When restoring this runtime spawned object, always respawn even if it's a class we wouldn't normally"));
	Manager.AddNativeGameplayTag(NeverRespawn, TEXT("When restoring a runtime spawned object, never respawn it"));
	Manager.AddNativeGameplayTag(NoRestoreTransform, TEXT("When restoring this object, never restore its transform"));
	Manager.AddNativeGameplayTag(NoRestoreVelocity, TEXT("When restoring this object, never restore its velocity"));
}
