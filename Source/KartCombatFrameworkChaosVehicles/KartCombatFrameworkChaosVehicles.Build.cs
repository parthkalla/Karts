// Copyright Epic Games, Inc. is not affiliated with this plugin. Original work, MIT licensed.

using UnrealBuildTool;

public class KartCombatFrameworkChaosVehicles : ModuleRules
{
	public KartCombatFrameworkChaosVehicles(ReadOnlyTargetRules Target) : base(Target)
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
				"ChaosVehicles",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"PhysicsCore",
			}
		);

		// This module exists so that "ChaosVehicles" appears in exactly one Build.cs in the plugin.
		// A project that ships the arcade movement component - which is the default, and the right
		// choice on mobile - never links the vehicle solver at all.
	}
}
