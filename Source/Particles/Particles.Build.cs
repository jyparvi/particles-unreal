// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Particles : ModuleRules
{
    public Particles(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateIncludePaths.AddRange(new string[]
        {
            System.IO.Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Private"),
            System.IO.Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Internal")
        });

        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "EnhancedInput"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "RenderCore",
                "RHI",
                "Renderer",
                "SlateCore"
            }
        );
    }
}
