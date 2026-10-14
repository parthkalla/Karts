// Original work, MIT licensed.

#include "Vehicles/KCFKartPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/KCFBoostComponent.h"
#include "Components/KCFCombatComponent.h"
#include "Components/KCFHealthComponent.h"
#include "Components/KCFInventoryComponent.h"
#include "Components/KCFPowerupComponent.h"
#include "Components/KCFWeaponComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/KCFDeveloperSettings.h"
#include "Core/KCFGameplayTags.h"
#include "Core/KCFLog.h"
#include "Data/KCFGameModeDataAsset.h"
#include "Data/KCFVehicleDataAsset.h"
#include "Data/KCFWeaponDataAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Match/KCFGameMode.h"
#include "Match/KCFGameState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Player/KCFPlayerState.h"
#include "Subsystems/KCFDamageSubsystem.h"
#include "TimerManager.h"
#include "Vehicles/KCFKartCameraComponent.h"
#include "Vehicles/KCFKartMovementComponent.h"

const FName AKCFKartPawn::KartMovementComponentName(TEXT("KartMovement"));

AKCFKartPawn::AKCFKartPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The pawn ticks because driving genuinely is per-frame work: input has to reach the movement
	// backend every frame, and the drift/airborne edge detection below has to sample a continuous
	// state. Everything else in the framework is an event or a timer.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	CollisionBox->SetBoxExtent(FVector(90.f, 55.f, 40.f));
	CollisionBox->SetCollisionProfileName(TEXT("Pawn"));
	CollisionBox->SetNotifyRigidBodyCollision(true);
	CollisionBox->SetGenerateOverlapEvents(true);
	CollisionBox->CanCharacterStepUpOn = ECB_No;
	SetRootComponent(CollisionBox);

	StaticBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticBody"));
	StaticBody->SetupAttachment(CollisionBox);
	StaticBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	SkeletalBody = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalBody"));
	SkeletalBody->SetupAttachment(CollisionBox);
	SkeletalBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkeletalBody->SetVisibility(false);

	CameraBoom = CreateDefaultSubobject<UKCFKartCameraComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(CollisionBox);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);

	KartMovement = CreateDefaultSubobject<UPawnMovementComponent, UKCFKartMovementComponent>(KartMovementComponentName);
	if (KartMovement)
	{
		KartMovement->SetUpdatedComponent(CollisionBox);
	}

	HealthComponent = CreateDefaultSubobject<UKCFHealthComponent>(TEXT("Health"));
	InventoryComponent = CreateDefaultSubobject<UKCFInventoryComponent>(TEXT("Inventory"));
	WeaponComponent = CreateDefaultSubobject<UKCFWeaponComponent>(TEXT("Weapons"));
	PowerupComponent = CreateDefaultSubobject<UKCFPowerupComponent>(TEXT("Powerups"));
	BoostComponent = CreateDefaultSubobject<UKCFBoostComponent>(TEXT("Boost"));
	CombatComponent = CreateDefaultSubobject<UKCFCombatComponent>(TEXT("Combat"));

	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = false;

	const UKCFDeveloperSettings* Settings = UKCFDeveloperSettings::Get();
	SetNetUpdateFrequency(Settings ? Settings->KartNetUpdateFrequency : 60.f);
	SetMinNetUpdateFrequency(Settings ? Settings->KartMinNetUpdateFrequency : 15.f);
	NetCullDistanceSquared = Settings && Settings->KartNetCullDistance > 0.f
		? FMath::Square(Settings->KartNetCullDistance)
		: NetCullDistanceSquared;

	AutoPossessPlayer = EAutoReceiveInput::Disabled;
	bUseControllerRotationYaw = false;
}

// -------------------------------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------------------------------

void AKCFKartPawn::BeginPlay()
{
	Super::BeginPlay();

	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &AKCFKartPawn::HandleDeath);
	}

	if (VehicleData)
	{
		ApplyVehicleVisuals();
	}

	// The camera is a client concern. On a dedicated server there is nobody to look through it, and
	// leaving it running would cost a spring-arm collision sweep per kart per frame for nothing.
	if (GetNetMode() == NM_DedicatedServer)
	{
		if (CameraBoom)
		{
			CameraBoom->SetComponentTickEnabled(false);
		}
	}
	else if (CameraBoom && Camera)
	{
		CameraBoom->SetManagedCamera(Camera);
	}
}

void AKCFKartPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HealthComponent)
	{
		HealthComponent->OnDeath.RemoveDynamic(this, &AKCFKartPawn::HandleDeath);
	}

	Super::EndPlay(EndPlayReason);
}

void AKCFKartPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(AKCFKartPawn, VehicleData, COND_InitialOnly);
	DOREPLIFETIME(AKCFKartPawn, bDrivingEnabled);

	// Simulated proxies use this only to lean the chassis and turn the wheels. The owner already
	// knows its own input, so sending it back would be pure waste.
	DOREPLIFETIME_CONDITION(AKCFKartPawn, ReplicatedInput, COND_SkipOwner);
}

UPawnMovementComponent* AKCFKartPawn::GetMovementComponent() const
{
	return KartMovement;
}

TScriptInterface<IKCFVehicleMovement> AKCFKartPawn::GetVehicleMovement() const
{
	// The movement subobject comes first, so a subclass that substituted a different backend gets
	// the one it asked for. The component scan after it covers a project that added its own
	// implementation as a separate component instead.
	if (KartMovement && KartMovement->Implements<UKCFVehicleMovement>())
	{
		TScriptInterface<IKCFVehicleMovement> Result;
		Result.SetObject(KartMovement);
		Result.SetInterface(Cast<IKCFVehicleMovement>(KartMovement.Get()));
		return Result;
	}

	for (UActorComponent* Component : GetComponents())
	{
		if (Component && Component->Implements<UKCFVehicleMovement>())
		{
			TScriptInterface<IKCFVehicleMovement> Result;
			Result.SetObject(Component);
			Result.SetInterface(Cast<IKCFVehicleMovement>(Component));
			return Result;
		}
	}

	return TScriptInterface<IKCFVehicleMovement>();
}

void AKCFKartPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Team colour and inventory size both depend on the player state, which only exists once the
	// pawn is possessed.
	if (HasAuthority() && VehicleData && InventoryComponent)
	{
		InventoryComponent->SetSlotCount(VehicleData->InventorySlots);
	}

	if (HasAuthority() && VehicleData && InventoryComponent)
	{
		if (UKCFWeaponDataAsset* StartingWeapon = VehicleData->DefaultWeapon.LoadSynchronous())
		{
			InventoryComponent->AddWeapon(StartingWeapon->Id, StartingWeapon->Ammo, StartingWeapon->MaxAmmo);
		}
	}
}

void AKCFKartPawn::UnPossessed()
{
	// A kart with no driver must not keep driving on the server with the last input it received.
	CurrentInput.Reset();
	ReplicatedInput.Reset();

	if (const TScriptInterface<IKCFVehicleMovement> Movement = GetVehicleMovement())
	{
		IKCFVehicleMovement* Impl = static_cast<IKCFVehicleMovement*>(Movement.GetInterface());
		Impl->SetThrottleInput(0.f);
		Impl->SetBrakeInput(0.f);
		Impl->SetSteeringInput(0.f);
		Impl->SetDriftInput(false);
	}

	Super::UnPossessed();
}

// -------------------------------------------------------------------------------------------------
// Vehicle definition
// -------------------------------------------------------------------------------------------------

void AKCFKartPawn::SetVehicleData(UKCFVehicleDataAsset* InVehicleData)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!InVehicleData)
	{
		KCF_REPORT_RECOVERABLE(LogKartCombatFramework, TEXT("SetVehicleData called with no definition; the kart keeps its previous configuration."));
		return;
	}

	VehicleData = InVehicleData;
	OnRep_VehicleData();
}

void AKCFKartPawn::OnRep_VehicleData()
{
	if (!VehicleData)
	{
		return;
	}

	CollisionBox->SetBoxExtent(VehicleData->CollisionExtent, true);

	if (const TScriptInterface<IKCFVehicleMovement> Movement = GetVehicleMovement())
	{
		static_cast<IKCFVehicleMovement*>(Movement.GetInterface())->SetVehicleMovementData(VehicleData->Movement);
	}

	// Durability and boost are authoritative state, so only the server applies them; clients
	// receive the resulting values through replication.
	if (HasAuthority())
	{
		if (HealthComponent)
		{
			HealthComponent->ApplyVehicleData(VehicleData);
		}
		if (BoostComponent)
		{
			BoostComponent->ApplyVehicleData(VehicleData);
		}
		if (InventoryComponent)
		{
			InventoryComponent->SetSlotCount(VehicleData->InventorySlots);
		}
	}

	if (CameraBoom)
	{
		CameraBoom->ApplyVehicleData(VehicleData);
	}

	ApplyVehicleVisuals();

	OnVehicleDataApplied.Broadcast(this);
}

void AKCFKartPawn::ApplyVehicleVisuals()
{
	// Meshes and materials exist to be looked at. A dedicated server loading them would spend
	// memory and loading time on data it will never render.
	if (GetNetMode() == NM_DedicatedServer || !VehicleData)
	{
		return;
	}

	if (USkeletalMesh* Skeletal = VehicleData->VehicleMesh.LoadSynchronous())
	{
		SkeletalBody->SetSkeletalMesh(Skeletal);
		SkeletalBody->SetVisibility(true);
		StaticBody->SetVisibility(false);
	}
	else if (UStaticMesh* Static = VehicleData->StaticVehicleMesh.LoadSynchronous())
	{
		StaticBody->SetStaticMesh(Static);
		StaticBody->SetVisibility(true);
		SkeletalBody->SetVisibility(false);
	}

	UMeshComponent* ActiveMesh = SkeletalBody->IsVisible()
		? static_cast<UMeshComponent*>(SkeletalBody)
		: static_cast<UMeshComponent*>(StaticBody);

	for (int32 Index = 0; Index < VehicleData->MaterialOverrides.Num(); ++Index)
	{
		if (UMaterialInterface* Material = VehicleData->MaterialOverrides[Index].LoadSynchronous())
		{
			ActiveMesh->SetMaterial(Index, Material);
		}
	}

	// Team tinting is driven through a dynamic material instance rather than a separate material
	// per team, so adding a fifth team is a data change and not four new assets.
	const int32 TintIndex = VehicleData->TeamColorMaterialIndex;
	if (TintIndex != INDEX_NONE)
	{
		const AKCFPlayerState* KCFPlayerState = GetPlayerState<AKCFPlayerState>();
		const AKCFGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AKCFGameState>() : nullptr;
		const UKCFGameModeDataAsset* Config = GameState ? GameState->GetGameModeConfig() : nullptr;
		if (KCFPlayerState && Config)
		{
			const FKCFTeamDefinition Team = Config->GetTeamDefinition(KCFPlayerState->GetTeamId());
			if (UMaterialInstanceDynamic* Dynamic = ActiveMesh->CreateDynamicMaterialInstance(TintIndex))
			{
				Dynamic->SetVectorParameterValue(VehicleData->TeamColorParameterName, Team.TeamColor);
			}
		}
	}

	BP_OnVehicleVisualsReady();
}

// -------------------------------------------------------------------------------------------------
// Driving
// -------------------------------------------------------------------------------------------------

void AKCFKartPawn::ApplyInputState(const FKCFKartInputState& NewInput)
{
	CurrentInput = NewInput;
	CurrentInput.Sanitize();

	if (!bDrivingEnabled || !CanAct())
	{
		CurrentInput.Reset();
	}
}

void AKCFKartPawn::SetDrivingEnabled(bool bEnabled)
{
	if (!HasAuthority() || bDrivingEnabled == bEnabled)
	{
		return;
	}

	bDrivingEnabled = bEnabled;
	OnRep_DrivingEnabled();
}

void AKCFKartPawn::OnRep_DrivingEnabled()
{
	if (const TScriptInterface<IKCFVehicleMovement> Movement = GetVehicleMovement())
	{
		static_cast<IKCFVehicleMovement*>(Movement.GetInterface())->SetMovementEnabled(bDrivingEnabled);
	}

	if (!bDrivingEnabled)
	{
		CurrentInput.Reset();
	}
}

void AKCFKartPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	PushInputToMovement(DeltaTime);

	// The owning client relays its input to the server at a fixed rate rather than every frame: at
	// 120 fps a per-frame send would quadruple the upstream cost for no additional fidelity.
	if (IsLocallyControlled() && !HasAuthority())
	{
		TimeSinceInputSend += DeltaTime;
		if (TimeSinceInputSend >= 1.f / InputSendRate)
		{
			TimeSinceInputSend = 0.f;
			ServerSendInput(CurrentInput);
		}
	}
}

void AKCFKartPawn::PushInputToMovement(float DeltaTime)
{
	const TScriptInterface<IKCFVehicleMovement> Movement = GetVehicleMovement();
	IKCFVehicleMovement* Impl = Movement ? static_cast<IKCFVehicleMovement*>(Movement.GetInterface()) : nullptr;
	if (!Impl)
	{
		return;
	}

	// A simulated proxy drives itself from the replicated input so that its wheels turn and its
	// chassis leans. Its transform still comes from movement replication; this is cosmetic only.
	const FKCFKartInputState& Effective = (IsLocallyControlled() || HasAuthority()) ? CurrentInput : ReplicatedInput;

	Impl->SetThrottleInput(Effective.Throttle);
	Impl->SetBrakeInput(Effective.Brake);
	Impl->SetSteeringInput(Effective.Steering);
	Impl->SetDriftInput(Effective.bDrift);

	if (HasAuthority())
	{
		ReplicatedInput = CurrentInput;
	}

	// Camera look is applied locally and never replicated: where another player is pointing their
	// camera has no gameplay meaning in a kart game.
	if (CameraBoom && IsLocallyControlled() && !Effective.Look.IsNearlyZero())
	{
		CameraBoom->AddLookInput(Effective.Look * DeltaTime);
	}

	// Edge detection so the cosmetic delegates fire once per transition rather than every frame.
	const bool bNowDrifting = Impl->IsDrifting();
	if (bNowDrifting != bWasDrifting)
	{
		bWasDrifting = bNowDrifting;
		(bNowDrifting ? OnDriftStarted : OnDriftEnded).Broadcast(this);
		if (GetNetMode() != NM_DedicatedServer)
		{
			BP_OnDriftChanged(bNowDrifting);
		}
	}

	const bool bNowAirborne = !Impl->IsGrounded();
	if (bNowAirborne != bWasAirborne)
	{
		bWasAirborne = bNowAirborne;
		(bNowAirborne ? OnBecameAirborne : OnLanded).Broadcast(this);
	}

	const bool bNowBoosting = BoostComponent && BoostComponent->IsBoosting();
	if (bNowBoosting != bWasBoosting)
	{
		bWasBoosting = bNowBoosting;
		if (GetNetMode() != NM_DedicatedServer)
		{
			BP_OnBoostChanged(bNowBoosting);
		}
	}
}

bool AKCFKartPawn::ServerSendInput_Validate(FKCFKartInputState NewInput)
{
	// Structural rejection only: NaN or out-of-range values indicate a malformed or hostile packet.
	// Note that this validates the *shape* of the input, never its gameplay consequence - the
	// server runs its own simulation from it regardless.
	return FMath::IsFinite(NewInput.Steering)
		&& FMath::IsFinite(NewInput.Throttle)
		&& FMath::IsFinite(NewInput.Brake)
		&& !NewInput.Look.ContainsNaN()
		&& FMath::Abs(NewInput.Steering) <= 1.001f
		&& NewInput.Throttle >= -0.001f && NewInput.Throttle <= 1.001f
		&& NewInput.Brake >= -0.001f && NewInput.Brake <= 1.001f;
}

void AKCFKartPawn::ServerSendInput_Implementation(FKCFKartInputState NewInput)
{
	NewInput.Sanitize();
	ApplyInputState(NewInput);
}

float AKCFKartPawn::GetCurrentSpeed() const
{
	const TScriptInterface<IKCFVehicleMovement> Movement = GetVehicleMovement();
	return Movement ? static_cast<const IKCFVehicleMovement*>(Movement.GetInterface())->GetForwardSpeed() : 0.f;
}

float AKCFKartPawn::GetSpeedPercent() const
{
	const TScriptInterface<IKCFVehicleMovement> Movement = GetVehicleMovement();
	return Movement ? static_cast<const IKCFVehicleMovement*>(Movement.GetInterface())->GetSpeedPercent() : 0.f;
}

bool AKCFKartPawn::IsDrifting() const
{
	const TScriptInterface<IKCFVehicleMovement> Movement = GetVehicleMovement();
	return Movement && static_cast<const IKCFVehicleMovement*>(Movement.GetInterface())->IsDrifting();
}

bool AKCFKartPawn::IsBoosting() const
{
	return BoostComponent && BoostComponent->IsBoosting();
}

bool AKCFKartPawn::IsAirborne() const
{
	const TScriptInterface<IKCFVehicleMovement> Movement = GetVehicleMovement();
	return Movement && !static_cast<const IKCFVehicleMovement*>(Movement.GetInterface())->IsGrounded();
}

bool AKCFKartPawn::CanAct() const
{
	if (HealthComponent && HealthComponent->IsDead())
	{
		return false;
	}

	const AKCFGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AKCFGameState>() : nullptr;
	return GameState ? GameState->IsMatchActive() : true;
}

// -------------------------------------------------------------------------------------------------
// Reset and spawn protection
// -------------------------------------------------------------------------------------------------

void AKCFKartPawn::ResetKart(const FTransform& NewTransform)
{
	if (!HasAuthority())
	{
		return;
	}

	CurrentInput.Reset();
	ReplicatedInput.Reset();

	if (const TScriptInterface<IKCFVehicleMovement> Movement = GetVehicleMovement())
	{
		static_cast<IKCFVehicleMovement*>(Movement.GetInterface())->ResetVehicle(NewTransform);
	}
	else
	{
		SetActorTransform(NewTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (BoostComponent)
	{
		BoostComponent->StopBoost();
	}
}

void AKCFKartPawn::ResetKartInPlace()
{
	if (!HasAuthority())
	{
		return;
	}

	// Yaw is preserved so the player faces the way they were going; roll and pitch are discarded,
	// which is the entire point of the reset.
	FTransform Upright = GetActorTransform();
	const FRotator Current = Upright.Rotator();
	Upright.SetRotation(FRotator(0.f, Current.Yaw, 0.f).Quaternion());

	// Lifted slightly so a kart that reset inside a slope does not immediately re-penetrate it.
	Upright.SetLocation(Upright.GetLocation() + FVector(0.f, 0.f, 60.f));

	ResetKart(Upright);
}

void AKCFKartPawn::ApplySpawnProtection(float DurationSeconds, bool bBreaksOnFire)
{
	if (!HasAuthority() || !HealthComponent || DurationSeconds <= 0.f)
	{
		return;
	}

	bSpawnProtectionBreaksOnFire = bBreaksOnFire;
	HealthComponent->GrantTimedInvulnerability(DurationSeconds);
	HealthComponent->AddActiveTag(KCFGameplayTags::Kart_State_SpawnProtected);
}

void AKCFKartPawn::BreakSpawnProtection()
{
	if (!HasAuthority() || !bSpawnProtectionBreaksOnFire || !HealthComponent)
	{
		return;
	}

	if (!HealthComponent->IsInvulnerable())
	{
		return;
	}

	bSpawnProtectionBreaksOnFire = false;
	HealthComponent->ClearAllInvulnerability();
	HealthComponent->RemoveActiveTag(KCFGameplayTags::Kart_State_SpawnProtected);
}

bool AKCFKartPawn::IsSpawnProtected() const
{
	return HealthComponent
		&& HealthComponent->GetActiveTags().HasTag(KCFGameplayTags::Kart_State_SpawnProtected);
}

// -------------------------------------------------------------------------------------------------
// Firing geometry
// -------------------------------------------------------------------------------------------------

FTransform AKCFKartPawn::GetMuzzleTransform() const
{
	const FName Socket = VehicleData ? VehicleData->MuzzleSocket : NAME_None;

	if (!Socket.IsNone())
	{
		if (SkeletalBody && SkeletalBody->DoesSocketExist(Socket))
		{
			return SkeletalBody->GetSocketTransform(Socket);
		}
		if (StaticBody && StaticBody->DoesSocketExist(Socket))
		{
			return StaticBody->GetSocketTransform(Socket);
		}
	}

	// Falling back to a point in front of the chassis rather than refusing to fire: a kart whose
	// art has no muzzle socket should still be playable.
	FTransform Fallback = GetActorTransform();
	Fallback.SetLocation(Fallback.GetLocation() + GetActorForwardVector() * (CollisionBox->GetScaledBoxExtent().X + 60.f) + FVector(0.f, 0.f, 30.f));
	return Fallback;
}

FTransform AKCFKartPawn::GetDropTransform() const
{
	const FName Socket = VehicleData ? VehicleData->DropSocket : NAME_None;

	if (!Socket.IsNone())
	{
		if (SkeletalBody && SkeletalBody->DoesSocketExist(Socket))
		{
			return SkeletalBody->GetSocketTransform(Socket);
		}
		if (StaticBody && StaticBody->DoesSocketExist(Socket))
		{
			return StaticBody->GetSocketTransform(Socket);
		}
	}

	FTransform Fallback = GetActorTransform();
	Fallback.SetLocation(Fallback.GetLocation() - GetActorForwardVector() * (CollisionBox->GetScaledBoxExtent().X + 40.f));
	return Fallback;
}

// -------------------------------------------------------------------------------------------------
// Damage interface
// -------------------------------------------------------------------------------------------------

FKCFDamageResult AKCFKartPawn::ApplyKCFDamage_Implementation(const FKCFDamageInfo& DamageInfo)
{
	if (!HasAuthority())
	{
		return FKCFDamageResult::Rejected(EKCFDamageRejectReason::NoAuthority);
	}

	if (!HealthComponent)
	{
		return FKCFDamageResult::Rejected(EKCFDamageRejectReason::TargetNotDamageable);
	}

	return HealthComponent->ApplyDamage(DamageInfo);
}

bool AKCFKartPawn::IsDeadOrDestroyed_Implementation() const
{
	return !IsValid(this) || (HealthComponent && HealthComponent->IsDead());
}

bool AKCFKartPawn::IsImmuneToDamage_Implementation(const FKCFDamageInfo& DamageInfo) const
{
	if (!HealthComponent)
	{
		return false;
	}

	if (DamageInfo.bIgnoreInvulnerability)
	{
		return false;
	}

	if (HealthComponent->IsInvulnerable())
	{
		return true;
	}

	// A project can author immunity to a whole damage family by granting a tag through a powerup,
	// without the framework needing to know what that family means.
	return HealthComponent->GetActiveTags().HasTag(KCFGameplayTags::Immunity_All)
		|| (DamageInfo.DamageTags.Num() > 0 && HealthComponent->GetActiveTags().HasAny(DamageInfo.DamageTags));
}

AKCFPlayerState* AKCFKartPawn::GetOwningKCFPlayerState_Implementation() const
{
	return GetPlayerState<AKCFPlayerState>();
}

int32 AKCFKartPawn::GetKCFTeamId_Implementation() const
{
	const AKCFPlayerState* KCFPlayerState = GetPlayerState<AKCFPlayerState>();
	return KCFPlayerState ? KCFPlayerState->GetTeamId() : KCF_NO_TEAM;
}

void AKCFKartPawn::SetKCFTeamId_Implementation(int32 NewTeamId)
{
	// The team lives on the player state, which is the thing that survives the pawn dying. Writing
	// it here would create a second copy that could disagree after a respawn.
	if (AKCFPlayerState* KCFPlayerState = GetPlayerState<AKCFPlayerState>())
	{
		IKCFTeamAgent::Execute_SetKCFTeamId(KCFPlayerState, NewTeamId);
	}
}

// -------------------------------------------------------------------------------------------------
// Collisions
// -------------------------------------------------------------------------------------------------

void AKCFKartPawn::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved,
	FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);

	const float ImpactSpeed = FMath::Abs(FVector::DotProduct(GetVelocity(), -HitNormal));

	const UKCFDeveloperSettings* Settings = UKCFDeveloperSettings::Get();
	const float Threshold = Settings ? Settings->CollisionDamageSpeedThreshold : 900.f;

	if (ImpactSpeed >= Threshold)
	{
		OnSignificantImpact.Broadcast(HitLocation, ImpactSpeed);
		if (GetNetMode() != NM_DedicatedServer)
		{
			BP_OnImpact(HitLocation, ImpactSpeed);
		}
	}

	if (HasAuthority())
	{
		HandleCollisionDamage(Other, HitLocation, HitNormal, ImpactSpeed);
	}
}

void AKCFKartPawn::HandleCollisionDamage(AActor* Other, const FVector& HitLocation, const FVector& HitNormal, float ImpactSpeed)
{
	const UKCFDeveloperSettings* Settings = UKCFDeveloperSettings::Get();
	if (!Settings || !Other || Other == this)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Collision damage is rate-limited because a kart scraping along a wall generates a hit event
	// every frame; without this, grinding a wall would be lethal in under a second.
	const float Now = World->GetTimeSeconds();
	if (Now - LastCollisionDamageTime < CollisionDamageCooldown)
	{
		return;
	}

	if (ImpactSpeed < Settings->CollisionDamageSpeedThreshold)
	{
		return;
	}

	const AKCFGameState* GameState = World->GetGameState<AKCFGameState>();
	const UKCFGameModeDataAsset* Config = GameState ? GameState->GetGameModeConfig() : nullptr;
	if (Config && !Config->bCollisionDamageEnabled)
	{
		return;
	}

	// Only kart-to-kart contact deals damage. A wall is already punishing because it stops you.
	AKCFKartPawn* OtherKart = Cast<AKCFKartPawn>(Other);
	if (!OtherKart)
	{
		return;
	}

	UKCFDamageSubsystem* DamageSubsystem = UKCFDamageSubsystem::Get(this);
	if (!DamageSubsystem)
	{
		return;
	}

	LastCollisionDamageTime = Now;

	const float ExcessSpeed = ImpactSpeed - Settings->CollisionDamageSpeedThreshold;
	const float Damage = (ExcessSpeed / 1000.f) * Settings->CollisionDamagePerThousandSpeed;
	if (Damage <= 0.f)
	{
		return;
	}

	FKCFDamageInfo Info(Damage, EKCFDamageCategory::Collision);
	Info.InstigatorPlayerState = GetPlayerState<AKCFPlayerState>();
	Info.DamageCauser = this;
	Info.HitLocation = HitLocation;
	Info.HitNormal = HitNormal;
	Info.KnockbackForce = ImpactSpeed * 0.35f;
	Info.KnockbackDirection = -HitNormal;

	DamageSubsystem->ApplyDamage(OtherKart, Info);
}

// -------------------------------------------------------------------------------------------------
// Death
// -------------------------------------------------------------------------------------------------

void AKCFKartPawn::HandleDeath(AActor* Victim, AActor* Killer, const FKCFDamageInfo& FatalDamage)
{
	// Clients learn about the death through the replicated dead flag and react cosmetically. Every
	// consequence below is scoring and match state, which only the server may decide.
	if (!HasAuthority())
	{
		return;
	}

	CurrentInput.Reset();
	ReplicatedInput.Reset();
	SetDrivingEnabled(false);

	if (WeaponComponent)
	{
		WeaponComponent->ResetWeaponState();
	}
	if (PowerupComponent)
	{
		PowerupComponent->ClearAllEffects();
	}

	const AKCFGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AKCFGameState>() : nullptr;
	const UKCFGameModeDataAsset* Config = GameState ? GameState->GetGameModeConfig() : nullptr;
	if (InventoryComponent && (!Config || Config->bClearInventoryOnDeath))
	{
		InventoryComponent->ClearAll();
	}

	AKCFPlayerState* VictimState = GetPlayerState<AKCFPlayerState>();
	AKCFPlayerState* KillerState = FatalDamage.InstigatorPlayerState;

	TArray<AKCFPlayerState*> Assisters;
	if (UKCFDamageSubsystem* DamageSubsystem = UKCFDamageSubsystem::Get(this))
	{
		Assisters = DamageSubsystem->CollectAssisters(this, KillerState);

		// The ledger is cleared immediately so that damage dealt before this death cannot also earn
		// an assist on the next one.
		DamageSubsystem->ClearLedgerForVictim(this);
	}

	if (AKCFGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AKCFGameMode>() : nullptr)
	{
		GameMode->HandleKartDeath(VictimState, KillerState, FatalDamage.WeaponId, Assisters);
	}

	// The pawn is destroyed rather than hidden: a dead kart has no gameplay role, and leaving it
	// around would keep paying replication and collision costs for a wreck.
	SetLifeSpan(0.35f);
}
