// Copyright Epic Games, Inc. is not affiliated with this plugin. Original work, MIT licensed.

using UnrealBuildTool;

public class KartCombatFrameworkEditor : ModuleRules
{
	public KartCombatFrameworkEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.Full;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		CppStandard = CppStandardVersion.Default;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"KartCombatFramework",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetRegistry",
				"DataValidation",
				"UnrealEd",
				"GameplayTags",
			}
		);

		// Everything in this module is editor-time validation. Nothing here is referenced by the
		// runtime module, which is what allows a packaged game to drop it entirely.
	}
}
