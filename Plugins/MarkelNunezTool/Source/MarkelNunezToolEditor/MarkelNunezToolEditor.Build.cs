// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MarkelNunezToolEditor : ModuleRules
{
	public MarkelNunezToolEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"MarkelNunezTool",
				"Slate",
				"SlateCore",
				"UnrealEd",
			});
	}
}
