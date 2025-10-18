using UnrealBuildTool;
using System.IO;
// ReSharper disable ConditionIsAlwaysTrueOrFalse

public class SPUD : ModuleRules
{
	public SPUD(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		const bool ForceUseSaveGameSystem = false;

		// We have to use the SaveGameSystem if the Target Platform
		// isn't a known platform with an unrestricted filesystem
		if (
			!(
			    (Target.Platform == UnrealTargetPlatform.Win64) ||
			    (Target.Platform == UnrealTargetPlatform.Mac) ||
			    (Target.Platform == UnrealTargetPlatform.Linux) ||
			    (Target.Platform == UnrealTargetPlatform.LinuxArm64) 
		    ) || ForceUseSaveGameSystem
		)
		{
			PrivateDefinitions.Add("USE_SAVEGAMESYSTEM=1");
		}

        PublicIncludePaths.AddRange(
            new string[] {
            }
            );


        PrivateIncludePaths.AddRange(
            new string[] {
            }
            );

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"StructUtils",
				"ImageCore",
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);
	}
}
