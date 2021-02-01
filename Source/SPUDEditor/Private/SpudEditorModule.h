#pragma once

#include "CoreMinimal.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "UnrealEd.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSpudEditor, All, All)

class FSpudEditorModule : public IModuleInterface
{
private:
    FDelegateHandle PrePIEHandle;
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    static void PreBeginPIE(bool);
};