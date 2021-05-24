#include "SpudEditorModule.h"


#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "SPUDEditor/Public/SpudPluginSettings.h"

IMPLEMENT_GAME_MODULE(FSpudEditorModule, SPUDEditor);

DEFINE_LOG_CATEGORY(LogSpudEditor);

#define LOCTEXT_NAMESPACE "SpudEditor"

void FSpudEditorModule::StartupModule()
{
    UE_LOG(LogSpudEditor, Log, TEXT("SpudEditor: StartupModule"));
    
    PrePIEHandle = FEditorDelegates::PreBeginPIE.AddStatic(&FSpudEditorModule::PreBeginPIE);

	// register settings
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

	if (SettingsModule)
	{
		ISettingsSectionPtr SettingsSection = SettingsModule->RegisterSettings("Project", "Plugins", "SPUD",
            LOCTEXT("SpudSettingsName", "SPUD"),
            LOCTEXT("SpudSettingsDescription", "Configure the SPUD plug-in."),
            GetMutableDefault<USpudPluginSettings>()
        );
	}
}

void FSpudEditorModule::ShutdownModule()
{
    FEditorDelegates::PreBeginPIE.Remove(PrePIEHandle);
    UE_LOG(LogSpudEditor, Log, TEXT("SpudEditor: ShutdownModule"));
}

void FSpudEditorModule::PreBeginPIE(bool)
{
	TArray<FString> UnsavedLevels;
	const bool AutoSave = GetDefault<USpudPluginSettings>()->SaveAllLevelsOnPlayInEditor;
	// If playing in editor with unsaved levels, level objects can APPEAR as if they're runtime objects because the
    // level association is not visible until a newly added object is saved.
    for (ULevel* Level : GWorld->GetLevels())
    {
    	if (Level->GetOutermost()->IsDirty())
    	{
    		UnsavedLevels.Add(Level->GetOutermost()->GetName());
    	}	
    }

	if (UnsavedLevels.Num() > 0)
	{
		if (AutoSave)
		{
			UE_LOG(LogSpudEditor, Log, TEXT("Auto-saving Levels: %s"), *FString::Join(UnsavedLevels, TEXT(", ")));
			// Use the same options as the File > Save All Levels command
			const bool bPromptUserToSave = false;
			const bool bSaveMapPackages = true;
			const bool bSaveContentPackages = false;
			const bool bFastSave = false;
			FEditorFileUtils::SaveDirtyPackages( bPromptUserToSave, bSaveMapPackages, bSaveContentPackages, bFastSave );		
		}
		else
		{
			UE_LOG(LogSpudEditor, Error, TEXT("Unsaved Levels: %s\n"
                "  Unsaved levels cause problems with persistence, because detecting which objects are runtime spawned or level spawned is unreliable.\n"
                "  Use File > Save All Levels before playing in editor to fix this error, or enable auto-save in the SPUD plugin settings."),
                *FString::Join(UnsavedLevels, TEXT(", ")));
		}
	}
    
}

#undef LOCTEXT_NAMESPACE
