#include "SpudModule.h"
#include "SpudPropertyUtil.h"


#define LOCTEXT_NAMESPACE "FSpud"

DEFINE_LOG_CATEGORY(LogSpudModule)

void FSpudModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	UE_LOG(LogSpudModule, Log, TEXT("SPUD Module Started"))

	// Clear the cache after every Garbage Collection cycle
	PostGarbageCollectHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddLambda([]()
		{
			SpudPropertyUtil::ClearModule();
		});

	// Clear the cache whenever a game world is destroyed / level transition
	PreWorldFinishDestroyHandle = FWorldDelegates::OnPreWorldFinishDestroy.AddLambda([](UWorld* World)
		{
			SpudPropertyUtil::ClearModule();
		});

#if WITH_EDITOR
	// Recompiling a Blueprint rebuilds the properties on the same class object and
	// frees the old ones, and a GC doesn't always follow the compile
	ObjectsReinstancedHandle = FCoreUObjectDelegates::OnObjectsReinstanced.AddLambda(
		[](const FCoreUObjectDelegates::FReplacementObjectMap&)
		{
			SpudPropertyUtil::ClearModule();
		});
#endif
}

void FSpudModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	UE_LOG(LogSpudModule, Log, TEXT("SPUD Module Stopped"))

	FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostGarbageCollectHandle);
	FWorldDelegates::OnPreWorldFinishDestroy.Remove(PreWorldFinishDestroyHandle);
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectsReinstanced.Remove(ObjectsReinstancedHandle);
#endif

	SpudPropertyUtil::ClearModule();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSpudModule, SPUD)
