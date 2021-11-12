#include "SpudModule.h"

#include "SpudGameplayTags.h"

#define LOCTEXT_NAMESPACE "FSpud"

DEFINE_LOG_CATEGORY(LogSpudModule)

void FSpudModule::StartupModule()
{
	USpudGameplayTags::RegisterTags();
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	UE_LOG(LogSpudModule, Log, TEXT("SPUD Module Started"))
}

void FSpudModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	UE_LOG(LogSpudModule, Log, TEXT("SPUD Module Stopped"))
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FSpudModule, SPUD)
