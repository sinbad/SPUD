#pragma once

#include "CoreMinimal.h"

#include "SpudPluginSettings.generated.h"


/**
* Settings for the SPUD plug-in.
*/
UCLASS(config=Engine)
class SPUDEDITOR_API USpudPluginSettings
    : public UObject
{
	GENERATED_BODY()

public:
	/// Whether to auto-save all levels before play in editor (PIE). Saving all levels is CRITICAL to SPUD
	/// being able to reliably categorise actors into those stored in the level, and those that were runtime spawned.
	/// Enabling this option is HIGHLY recommended, but not defaulted to avoid surprise
	UPROPERTY(config, EditAnywhere, Category=General)
	bool SaveAllLevelsOnPlayInEditor;


	USpudPluginSettings() : SaveAllLevelsOnPlayInEditor(false) {} 
	
};
