#pragma once

#include "CoreMinimal.h"

#include "Modules/ModuleInterface.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSpudTestModule, All, All)

class FSpudTestModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};