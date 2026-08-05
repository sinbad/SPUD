#include "SpudModule.h"
#include "SpudPropertyUtil.h"


#define LOCTEXT_NAMESPACE "FSpud"

DEFINE_LOG_CATEGORY(LogSpudModule)

void FSpudModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	UE_LOG(LogSpudModule, Log, TEXT("SPUD Module Started"))

	UE_LOG(LogSpudModule, Log, TEXT("SPUD Clear Module"))
	SpudPropertyUtil::ClearModule();

	// Clear the cache after every Garbage Collection cycle
	FCoreUObjectDelegates::GetPostGarbageCollect().AddLambda([]()
		{
			SpudPropertyUtil::ClearModule();
		});

	// Clear the cache whenever a game world is destroyed / level transition
	FWorldDelegates::OnPreWorldFinishDestroy.AddLambda([](UWorld* World)
		{
			SpudPropertyUtil::ClearModule();
		});
}

void FSpudModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	UE_LOG(LogSpudModule, Log, TEXT("SPUD Module Stopped"))

	SpudPropertyUtil::ClearModule();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSpudModule, SPUD)
