#include "SpudEditorModule.h"

IMPLEMENT_GAME_MODULE(FSpudEditorModule, SPUDEditor);

DEFINE_LOG_CATEGORY(LogSpudEditor);

#define LOCTEXT_NAMESPACE "SpudEditor"

void FSpudEditorModule::StartupModule()
{
    UE_LOG(LogSpudEditor, Warning, TEXT("SpudEditor: StartupModule"));
    
    PrePIEHandle = FEditorDelegates::PostPIEStarted.AddStatic(&FSpudEditorModule::PostPIEStarted);

}

void FSpudEditorModule::ShutdownModule()
{
    FEditorDelegates::PreBeginPIE.Remove(PrePIEHandle);
    UE_LOG(LogSpudEditor, Warning, TEXT("SpudEditor: ShutdownModule"));
}

void FSpudEditorModule::PostPIEStarted(bool)
{
	TArray<FString> UnsavedLevels;
	// If playing in editor with unsaved levels, level objects can APPEAR as if they're runtime objects because the
    // level association is not visible until a newly added object is saved.
    for (ULevel* Level : GEditor->EditorWorld->GetLevels())
    {
    	if (Level->GetOutermost()->IsDirty())
    	{
    		UnsavedLevels.Add(Level->GetOutermost()->GetName());
    	}	
    }

	if (UnsavedLevels.Num() > 0)
	{
		UE_LOG(LogSpudEditor, Error, TEXT("Unsaved Levels: %s\n"
			"  Unsaved levels cause problems with persistence, because detecting which objects are runtime spawned or level spawned is unreliable.\n"
			"  Save ALL your levels (including streaming levels) before playing in editor to fix this error."),
			*FString::Join(UnsavedLevels, TEXT(", ")));		
	}
    
}

#undef LOCTEXT_NAMESPACE
