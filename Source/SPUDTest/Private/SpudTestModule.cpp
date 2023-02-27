#include "SpudTestModule.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogSpudTestModule)

void FSpudTestModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	UE_LOG(LogSpudTestModule, Log, TEXT("SpudTest Module Started"))
}

void FSpudTestModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	UE_LOG(LogSpudTestModule, Log, TEXT("SpudTest Module Stopped"))
}


IMPLEMENT_MODULE(FSpudTestModule, SPUDTest)
