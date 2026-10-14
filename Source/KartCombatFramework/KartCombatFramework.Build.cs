// Copyright Epic Games, Inc. is not affiliated with this plugin. Original work, MIT licensed.

using UnrealBuildTool;

public class KartCombatFramework : ModuleRules
{
	public KartCombatFramework(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		IWYUSupport = IWYUSupport.Full;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		CppStandard = CppStandardVersion.Default;

		// Public dependencies appear in this module's public headers.
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"NetCore",
				"GameplayTags",
				"EnhancedInput",
				"Niagara",
			}
		);

		// Private dependencies are implementation details and must not leak into Public/.
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"PhysicsCore",
				"DeveloperSettings",
				"InputCore",
			}
		);

		// Deliberately NOT dependencies:
		//   UMG/UMGEditor - the framework ships no widgets. HUDs bind to delegates from project Blueprints,
		//                   which keeps dedicated-server builds free of any Slate widget construction.
		//   ChaosVehicles - isolated in the optional KartCombatFrameworkChaosVehicles module so that
		//                   projects using a different vehicle backend never link it.
		//   OnlineSubsystem - reached through IKCFSessionService; a concrete backend is a project choice.
		//   Slate/SlateCore - nothing in the framework draws. Debug drawing uses DrawDebugHelpers (Engine).
	}
}
