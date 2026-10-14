// Original work, MIT licensed.

#include "Input/KCFKartInputComponent.h"

#include "Components/KCFBoostComponent.h"
#include "Components/KCFCombatComponent.h"
#include "Core/KCFDeveloperSettings.h"
#include "Core/KCFLog.h"
#include "Data/KCFMobileSettingsDataAsset.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Input/KCFInputConfigDataAsset.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Player/KCFPlayerController.h"
#include "Vehicles/KCFKartCameraComponent.h"
#include "Vehicles/KCFKartPawn.h"

UKCFKartInputComponent::UKCFKartInputComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// Input is purely local. Nothing here replicates; the pawn relays the finished state to the
	// server itself, on an unreliable RPC at a fixed rate.
	SetIsReplicatedByDefault(false);
}

void UKCFKartInputComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const AKCFPlayerController* PC = GetOwningController())
	{
		if (const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (LoadedConfig)
				{
					if (UInputMappingContext* Context = LoadedConfig->MappingContext.Get())
					{
						Subsystem->RemoveMappingContext(Context);
					}
				}
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

AKCFPlayerController* UKCFKartInputComponent::GetOwningController() const
{
	return Cast<AKCFPlayerController>(GetOwner());
}

void UKCFKartInputComponent::ResolveConfiguration()
{
	if (!LoadedConfig && !InputConfig.IsNull())
	{
		LoadedConfig = InputConfig.LoadSynchronous();
	}

	if (!MobileSettings)
	{
		if (const UKCFDeveloperSettings* Settings = UKCFDeveloperSettings::Get())
		{
			MobileSettings = Settings->DefaultMobileSettings.LoadSynchronous();
		}
	}
}

void UKCFKartInputComponent::InitialiseLocalInput()
{
	ResolveConfiguration();

	const AKCFPlayerController* PC = GetOwningController();
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	if (!LocalPlayer || !LoadedConfig)
	{
		// Not an error: a touch-only build legitimately ships no Enhanced Input configuration, and
		// the mobile API drives the kart perfectly well without one.
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	UInputMappingContext* Context = LoadedConfig->MappingContext.LoadSynchronous();
	if (Subsystem && Context)
	{
		Subsystem->AddMappingContext(Context, LoadedConfig->MappingPriority);
	}
}

void UKCFKartInputComponent::BindInputActions(UInputComponent* PlayerInputComponent)
{
	ResolveConfiguration();

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput || !LoadedConfig)
	{
		return;
	}

	// Every action is optional so that a project binds only what its control scheme actually uses.
	auto BindIf = [EnhancedInput, this](const TSoftObjectPtr<UInputAction>& SoftAction, ETriggerEvent Event, auto Handler)
	{
		if (UInputAction* Action = SoftAction.IsNull() ? nullptr : SoftAction.LoadSynchronous())
		{
			EnhancedInput->BindAction(Action, Event, this, Handler);
		}
	};

	BindIf(LoadedConfig->MoveAction, ETriggerEvent::Triggered, &UKCFKartInputComponent::HandleMove);
	BindIf(LoadedConfig->MoveAction, ETriggerEvent::Completed, &UKCFKartInputComponent::HandleMove);

	BindIf(LoadedConfig->SteerAction, ETriggerEvent::Triggered, &UKCFKartInputComponent::HandleSteer);
	BindIf(LoadedConfig->SteerAction, ETriggerEvent::Completed, &UKCFKartInputComponent::HandleSteer);

	BindIf(LoadedConfig->ThrottleAction, ETriggerEvent::Triggered, &UKCFKartInputComponent::HandleThrottle);
	BindIf(LoadedConfig->ThrottleAction, ETriggerEvent::Completed, &UKCFKartInputComponent::HandleThrottle);

	BindIf(LoadedConfig->BrakeAction, ETriggerEvent::Triggered, &UKCFKartInputComponent::HandleBrake);
	BindIf(LoadedConfig->BrakeAction, ETriggerEvent::Completed, &UKCFKartInputComponent::HandleBrake);

	BindIf(LoadedConfig->DriftAction, ETriggerEvent::Started, &UKCFKartInputComponent::HandleDriftStarted);
	BindIf(LoadedConfig->DriftAction, ETriggerEvent::Completed, &UKCFKartInputComponent::HandleDriftCompleted);
	BindIf(LoadedConfig->DriftAction, ETriggerEvent::Canceled, &UKCFKartInputComponent::HandleDriftCompleted);

	BindIf(LoadedConfig->BoostAction, ETriggerEvent::Started, &UKCFKartInputComponent::HandleBoostStarted);
	BindIf(LoadedConfig->BoostAction, ETriggerEvent::Completed, &UKCFKartInputComponent::HandleBoostCompleted);

	BindIf(LoadedConfig->FireAction, ETriggerEvent::Started, &UKCFKartInputComponent::HandleFireStarted);
	BindIf(LoadedConfig->FireAction, ETriggerEvent::Completed, &UKCFKartInputComponent::HandleFireCompleted);
	BindIf(LoadedConfig->FireAction, ETriggerEvent::Canceled, &UKCFKartInputComponent::HandleFireCompleted);

	BindIf(LoadedConfig->UsePowerupAction, ETriggerEvent::Started, &UKCFKartInputComponent::HandleUsePowerup);
	BindIf(LoadedConfig->CycleSlotAction, ETriggerEvent::Started, &UKCFKartInputComponent::HandleCycleSlot);

	BindIf(LoadedConfig->LookAction, ETriggerEvent::Triggered, &UKCFKartInputComponent::HandleLook);
	BindIf(LoadedConfig->ResetCameraAction, ETriggerEvent::Started, &UKCFKartInputComponent::HandleRecenterCamera);
	BindIf(LoadedConfig->ResetKartAction, ETriggerEvent::Started, &UKCFKartInputComponent::HandleResetKart);
	BindIf(LoadedConfig->PauseAction, ETriggerEvent::Started, &UKCFKartInputComponent::HandlePause);
}

void UKCFKartInputComponent::SetControlledKart(AKCFKartPawn* InKart)
{
	ControlledKart = InKart;

	// Stale input must never carry across a possession change: a player who died with the throttle
	// down should not start their next life accelerating.
	ClearInput();
	UpdateTickEnabled();
}

void UKCFKartInputComponent::UpdateTickEnabled()
{
	const AKCFPlayerController* PC = GetOwningController();
	const bool bWantsTick = PC && PC->IsLocalController() && ControlledKart.IsValid();
	SetComponentTickEnabled(bWantsTick);
}

void UKCFKartInputComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AKCFKartPawn* Kart = ControlledKart.Get();
	if (!Kart)
	{
		UpdateTickEnabled();
		return;
	}

	// Repeat fire is resolved here rather than on a timer so that it naturally follows the weapon's
	// own cooldown: the request is simply refused until the weapon is ready again.
	if (bFireHeld && MobileSettings && MobileSettings->bFireRepeatsWhileHeld)
	{
		if (UKCFCombatComponent* Combat = Kart->GetCombatComponent())
		{
			if (Combat->CanFireWeapon())
			{
				Combat->FireCurrentWeapon();
			}
		}
	}

	FinaliseInputForFrame();
	Kart->ApplyInputState(InputState);
}

void UKCFKartInputComponent::FinaliseInputForFrame()
{
	// Steering is processed here, once, no matter how many sources wrote to the raw axis.
	InputState.Steering = MobileSettings
		? MobileSettings->ProcessSteeringAxis(RawSteeringAxis)
		: FMath::Clamp(RawSteeringAxis, -1.f, 1.f);

	if (MobileSettings && MobileSettings->bAutoAccelerate)
	{
		const bool bYield = MobileSettings->bAutoAccelerateYieldsToBrake && InputState.Brake > 0.f;
		InputState.Throttle = bYield ? 0.f : MobileSettings->AutoAccelerateThrottle;
	}

	// Look is a delta, so it is consumed rather than held: leaving it set would keep rotating the
	// camera for as long as the player did not touch the screen again.
	InputState.Look = PendingLook;
	PendingLook = FVector2D::ZeroVector;

	InputState.Sanitize();
}

// --- Enhanced Input handlers ---------------------------------------------------------------------

void UKCFKartInputComponent::HandleMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	RawSteeringAxis = Axis.X;

	// One stick folds forward and back into a single axis: positive drives, negative brakes and
	// then reverses. The movement component decides where braking becomes reverse.
	InputState.Throttle = FMath::Clamp(Axis.Y, 0.f, 1.f);
	if (!bMobileBrakeHeld)
	{
		InputState.Brake = FMath::Clamp(-Axis.Y, 0.f, 1.f);
	}
}

void UKCFKartInputComponent::HandleSteer(const FInputActionValue& Value)
{
	RawSteeringAxis = Value.Get<float>();
}

void UKCFKartInputComponent::HandleThrottle(const FInputActionValue& Value)
{
	InputState.Throttle = FMath::Clamp(Value.Get<float>(), 0.f, 1.f);
}

void UKCFKartInputComponent::HandleBrake(const FInputActionValue& Value)
{
	InputState.Brake = FMath::Clamp(Value.Get<float>(), 0.f, 1.f);
}

void UKCFKartInputComponent::HandleDriftStarted(const FInputActionValue&) { StartDrift(); }
void UKCFKartInputComponent::HandleDriftCompleted(const FInputActionValue&) { StopDrift(); }
void UKCFKartInputComponent::HandleBoostStarted(const FInputActionValue&) { StartBoost(); }
void UKCFKartInputComponent::HandleBoostCompleted(const FInputActionValue&) { StopBoost(); }
void UKCFKartInputComponent::HandleFireStarted(const FInputActionValue&) { FireWeapon(); }
void UKCFKartInputComponent::HandleFireCompleted(const FInputActionValue&) { bFireHeld = false; }
void UKCFKartInputComponent::HandleUsePowerup(const FInputActionValue&) { UsePowerup(); }
void UKCFKartInputComponent::HandleCycleSlot(const FInputActionValue&) { CycleSlot(); }
void UKCFKartInputComponent::HandleRecenterCamera(const FInputActionValue&) { RecenterCamera(); }
void UKCFKartInputComponent::HandleResetKart(const FInputActionValue&) { RequestKartReset(); }

void UKCFKartInputComponent::HandleLook(const FInputActionValue& Value)
{
	AddLookInput(Value.Get<FVector2D>());
}

void UKCFKartInputComponent::HandlePause(const FInputActionValue&)
{
	if (AKCFPlayerController* PC = GetOwningController())
	{
		PC->RequestPause(true);
	}
}

// --- Mobile API ---------------------------------------------------------------------------------------

void UKCFKartInputComponent::SetMobileSettings(UKCFMobileSettingsDataAsset* InSettings)
{
	MobileSettings = InSettings;
}

bool UKCFKartInputComponent::IsAutoAccelerationEnabled() const
{
	return MobileSettings && MobileSettings->bAutoAccelerate;
}

void UKCFKartInputComponent::SetMobileSteering(float AxisValue)
{
	RawSteeringAxis = FMath::IsFinite(AxisValue) ? FMath::Clamp(AxisValue, -1.f, 1.f) : 0.f;
}

void UKCFKartInputComponent::SetMobileThrottle(float AxisValue)
{
	InputState.Throttle = FMath::IsFinite(AxisValue) ? FMath::Clamp(AxisValue, 0.f, 1.f) : 0.f;
}

void UKCFKartInputComponent::SetMobileMovement(FVector2D AxisValues)
{
	if (AxisValues.ContainsNaN())
	{
		return;
	}

	SetMobileSteering(AxisValues.X);
	SetMobileThrottle(FMath::Max(AxisValues.Y, 0.f));

	if (!bMobileBrakeHeld)
	{
		InputState.Brake = FMath::Clamp(-AxisValues.Y, 0.f, 1.f);
	}
}

void UKCFKartInputComponent::StartMobileBrake()
{
	bMobileBrakeHeld = true;
	InputState.Brake = 1.f;
}

void UKCFKartInputComponent::StopMobileBrake()
{
	bMobileBrakeHeld = false;
	InputState.Brake = 0.f;
}

void UKCFKartInputComponent::StartDrift()
{
	// A toggle layout flips the held state so one tap engages and the next releases, which is far
	// easier on a touchscreen than keeping a thumb planted on a drift button mid-corner.
	InputState.bDrift = (MobileSettings && MobileSettings->bDriftIsToggle) ? !InputState.bDrift : true;
}

void UKCFKartInputComponent::StopDrift()
{
	if (MobileSettings && MobileSettings->bDriftIsToggle)
	{
		return;
	}
	InputState.bDrift = false;
}

void UKCFKartInputComponent::StartBoost()
{
	InputState.bBoost = true;

	// Boost is a request rather than a held state: the component decides whether a charge exists.
	// Sending it here instead of waiting for the next tick keeps the button feeling immediate.
	if (AKCFKartPawn* Kart = ControlledKart.Get())
	{
		if (UKCFBoostComponent* Boost = Kart->GetBoostComponent())
		{
			Boost->RequestBoost();
		}
	}
}

void UKCFKartInputComponent::StopBoost()
{
	if (MobileSettings && MobileSettings->bBoostIsToggle)
	{
		// A latched boost still ends on its own server-side timer; releasing the button must not
		// cut short a boost the player has already spent a charge on.
		return;
	}
	InputState.bBoost = false;
}

void UKCFKartInputComponent::FireWeapon()
{
	bFireHeld = true;

	if (AKCFKartPawn* Kart = ControlledKart.Get())
	{
		if (UKCFCombatComponent* Combat = Kart->GetCombatComponent())
		{
			Combat->FireCurrentWeapon();
		}
	}
}

void UKCFKartInputComponent::UsePowerup()
{
	if (AKCFKartPawn* Kart = ControlledKart.Get())
	{
		if (UKCFCombatComponent* Combat = Kart->GetCombatComponent())
		{
			Combat->UseCurrentPowerup();
		}
	}
}

void UKCFKartInputComponent::UseSelectedSlot()
{
	if (AKCFKartPawn* Kart = ControlledKart.Get())
	{
		if (UKCFCombatComponent* Combat = Kart->GetCombatComponent())
		{
			Combat->UseSelectedSlot();
		}
	}
}

void UKCFKartInputComponent::CycleSlot()
{
	if (AKCFKartPawn* Kart = ControlledKart.Get())
	{
		if (UKCFCombatComponent* Combat = Kart->GetCombatComponent())
		{
			Combat->CycleToNextSlot();
		}
	}
}

void UKCFKartInputComponent::AddLookInput(FVector2D Delta)
{
	if (Delta.ContainsNaN())
	{
		return;
	}

	const float Sensitivity = MobileSettings ? MobileSettings->LookSensitivity : 1.f;
	const float YSign = (MobileSettings && MobileSettings->bInvertLookY) ? -1.f : 1.f;

	PendingLook += FVector2D(Delta.X * Sensitivity, Delta.Y * Sensitivity * YSign);
}

void UKCFKartInputComponent::RecenterCamera()
{
	if (AKCFKartPawn* Kart = ControlledKart.Get())
	{
		if (UKCFKartCameraComponent* Camera = Kart->GetKartCamera())
		{
			Camera->SnapBehindKart();
		}
	}
}

void UKCFKartInputComponent::RequestKartReset()
{
	if (AKCFPlayerController* PC = GetOwningController())
	{
		PC->RequestKartReset();
	}
}

void UKCFKartInputComponent::ClearInput()
{
	InputState.Reset();
	RawSteeringAxis = 0.f;
	PendingLook = FVector2D::ZeroVector;
	bMobileBrakeHeld = false;
	bFireHeld = false;
}
