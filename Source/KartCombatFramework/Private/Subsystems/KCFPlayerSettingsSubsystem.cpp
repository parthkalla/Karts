// Original work, MIT licensed.

#include "Subsystems/KCFPlayerSettingsSubsystem.h"

#include "Core/KCFDeveloperSettings.h"
#include "Core/KCFLog.h"
#include "Data/KCFMobileSettingsDataAsset.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Input/KCFKartInputComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Player/KCFPlayerController.h"
#include "Utilities/KCFSaveGame.h"

const TCHAR* UKCFPlayerSettingsSubsystem::SaveSlotName = TEXT("KCFPlayerSettings");

void UKCFPlayerSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ReloadSettings();
}

void UKCFPlayerSettingsSubsystem::Deinitialize()
{
	Settings = nullptr;
	RuntimeMobileSettings = nullptr;
	Super::Deinitialize();
}

UKCFPlayerSettingsSubsystem* UKCFPlayerSettingsSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine && WorldContextObject
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UKCFPlayerSettingsSubsystem>() : nullptr;
}

void UKCFPlayerSettingsSubsystem::ReloadSettings()
{
	const int32 UserIndex = GetLocalPlayer() ? GetLocalPlayer()->GetControllerId() : 0;

	if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, UserIndex))
	{
		Settings = Cast<UKCFSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, UserIndex));
	}

	if (!Settings)
	{
		// A missing or unreadable save is not an error worth surfacing to the player: defaults are
		// perfectly playable and the file will be written the first time they change anything.
		Settings = Cast<UKCFSaveGame>(UGameplayStatics::CreateSaveGameObject(UKCFSaveGame::StaticClass()));
	}

	BuildRuntimeMobileSettings();
	ApplySettings();
}

bool UKCFPlayerSettingsSubsystem::SaveSettings()
{
	if (!Settings)
	{
		return false;
	}

	const int32 UserIndex = GetLocalPlayer() ? GetLocalPlayer()->GetControllerId() : 0;
	const bool bSaved = UGameplayStatics::SaveGameToSlot(Settings, SaveSlotName, UserIndex);

	if (!bSaved)
	{
		UE_LOG(LogKartCombatFramework, Warning, TEXT("Failed to write player settings to slot '%s'."), SaveSlotName);
	}

	ApplySettings();
	return bSaved;
}

void UKCFPlayerSettingsSubsystem::ResetToDefaults()
{
	Settings = Cast<UKCFSaveGame>(UGameplayStatics::CreateSaveGameObject(UKCFSaveGame::StaticClass()));
	SaveSettings();
}

void UKCFPlayerSettingsSubsystem::BuildRuntimeMobileSettings()
{
	if (!RuntimeMobileSettings)
	{
		RuntimeMobileSettings = NewObject<UKCFMobileSettingsDataAsset>(this);
	}

	// Seeded from the project's authored defaults so that any field the save does not cover (the
	// tilt curve, the auto-centre delay, the fire buffer) keeps the designer's intent.
	if (const UKCFDeveloperSettings* DeveloperSettings = UKCFDeveloperSettings::Get())
	{
		if (const UKCFMobileSettingsDataAsset* Template = DeveloperSettings->DefaultMobileSettings.LoadSynchronous())
		{
			UEngine::CopyPropertiesForUnrelatedObjects(const_cast<UKCFMobileSettingsDataAsset*>(Template), RuntimeMobileSettings);
		}
	}
}

void UKCFPlayerSettingsSubsystem::CopySettingsToRuntime()
{
	if (!Settings || !RuntimeMobileSettings)
	{
		return;
	}

	RuntimeMobileSettings->SteeringMode = Settings->SteeringMode;
	RuntimeMobileSettings->SteeringDeadzone = Settings->SteeringDeadzone;
	RuntimeMobileSettings->SteeringSensitivity = Settings->SteeringSensitivity;
	RuntimeMobileSettings->LookSensitivity = Settings->LookSensitivity;
	RuntimeMobileSettings->bInvertLookY = Settings->bInvertLookY;
	RuntimeMobileSettings->bAutoAccelerate = Settings->bAutoAccelerate;
	RuntimeMobileSettings->bDriftIsToggle = Settings->bDriftIsToggle;
	RuntimeMobileSettings->bBoostIsToggle = Settings->bBoostIsToggle;
	RuntimeMobileSettings->bHapticsEnabled = Settings->bHapticsEnabled;
	RuntimeMobileSettings->HapticIntensity = Settings->HapticIntensity;
}

void UKCFPlayerSettingsSubsystem::ApplySettings()
{
	CopySettingsToRuntime();
	PushSettingsToInput();
	OnSettingsChanged.Broadcast();
}

void UKCFPlayerSettingsSubsystem::PushSettingsToInput()
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	AKCFPlayerController* PC = LocalPlayer ? Cast<AKCFPlayerController>(LocalPlayer->GetPlayerController(LocalPlayer->GetWorld())) : nullptr;
	if (!PC)
	{
		// Perfectly normal during start-up: settings load before a controller exists. ApplySettings
		// runs again from the settings menu and whenever the player changes something.
		return;
	}

	if (UKCFKartInputComponent* Input = PC->GetKartInput())
	{
		Input->SetMobileSettings(RuntimeMobileSettings);
	}
}
