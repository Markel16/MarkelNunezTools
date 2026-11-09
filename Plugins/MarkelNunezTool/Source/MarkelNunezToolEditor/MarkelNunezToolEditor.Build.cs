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
				"AssetRegistry",
				"Core",
				"CoreUObject",
				"Engine",
				"MarkelNunezTool",
				"PCG",
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"UnrealEd",
			});
	}
}
