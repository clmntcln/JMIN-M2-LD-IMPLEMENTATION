// Copyright Doupi Design 2025. All Rights Reserved.

using UnrealBuildTool;

public class TeleportToMouse : ModuleRules
{
    public TeleportToMouse(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "ComponentVisualizers"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "InputCore",
                "UnrealEd",
                "LevelEditor",
                "EditorStyle",
                "EditorFramework"
            }
        );
    }
}