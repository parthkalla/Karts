// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Core/KCFTypes.h"
#include "GameFramework/PlayerController.h"
#include "KCFPlayerController.generated.h"

class AKCFGameState;
class AKCFKartPawn;
class AKCFPlayerState;
class UKCFKartInputComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKCFKillConfirmedSignature, AKCFPlayerState*, Victim, FName, WeaponId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKCFHitConfirmedSignature, float, DamageDealt, bool, bWasLethal);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKCFPossessedKartSignature, AKCFKartPawn*, KartPawn);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKCFRespawnCountdownSignature, float, SecondsRemaining);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKCFPauseRequestedSignature, bool, bPaused);

/**
 * Player controller for the framework.
 *
 * Owns the input abstraction and is the single channel for client-to-server requests. Every server
 * RPC here validates both structurally (in _Validate) and semantically (in _Implementation) before
 * acting; none of them accepts a gameplay result, only a request.
 *
 * Camera, HUD events and local feedback live here rather than on the pawn because they must survive
 * the pawn being destroyed on death.
 */
UCLASS(BlueprintType, Blueprintable)
class KARTCOMBATFRAMEWORK_API AKCFPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AKCFPlayerController();

	//~ Begin AActor
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor

	//~ Begin APlayerController
	virtual void SetupInputComponent() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void AcknowledgePossession(APawn* InPawn) override;
	//~ End APlayerController

	// --- Delegates ------------------------------------------------------------------------------

	/** Owning client only. Raised when the server confirms this player got a kill. */
	UPROPERTY(BlueprintAssignable, Category = "KCF|Player")
	FKCFKillConfirmedSignature OnKillConfirmed;

	/** Owning client only. Drives a hit marker. */
	UPROPERTY(BlueprintAssignable, Category = "KCF|Player")
	FKCFHitConfirmedSignature OnHitConfirmed;

	/** Raised on every machine when this controller takes or loses a kart. */
	UPROPERTY(BlueprintAssignable, Category = "KCF|Player")
	FKCFPossessedKartSignature OnPossessedKartChanged;

	/** Owning client only. Raised once when a respawn becomes available to request. */
	UPROPERTY(BlueprintAssignable, Category = "KCF|Player")
	FKCFRespawnCountdownSignature OnRespawnAvailable;

	/** Raised locally when the player presses pause. The project decides what a pause means. */
	UPROPERTY(BlueprintAssignable, Category = "KCF|Player")
	FKCFPauseRequestedSignature OnPauseRequested;

	// --- Accessors --------------------------------------------------------------------------------

	/** The input abstraction. Mobile UI Blueprints call into this. Never null after construction. */
	UFUNCTION(BlueprintPure, Category = "KCF|Input")
	UKCFKartInputComponent* GetKartInput() const { return KartInput; }

	UFUNCTION(BlueprintPure, Category = "KCF|Player")
	AKCFPlayerState* GetKCFPlayerState() const;

	UFUNCTION(BlueprintPure, Category = "KCF|Player")
	AKCFKartPawn* GetKartPawn() const;

	UFUNCTION(BlueprintPure, Category = "KCF|Match")
	AKCFGameState* GetKCFGameState() const;

	/** Seconds until this player may respawn. 0 when they are alive or may respawn now. */
	UFUNCTION(BlueprintPure, Category = "KCF|Player")
	float GetRespawnCountdown() const;

	/** True when the player is dead and the respawn delay has elapsed. */
	UFUNCTION(BlueprintPure, Category = "KCF|Player")
	bool CanRequestRespawn() const;

	// --- Client-callable requests -------------------------------------------------------------------
	// These are the Blueprint-facing entry points. Each forwards to a validated server RPC; none of
	// them changes any gameplay value locally.

	/** Lobby ready-up. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Player")
	void RequestReady(bool bNewReady);

	/** Asks the server to move this player to a team. Subject to mode rules and team balance. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Player")
	void RequestTeamChange(int32 DesiredTeamId);

	/** Asks the server to respawn this player. Ignored unless CanRequestRespawn would be true. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Player")
	void RequestRespawn();

	/** Asks the server to use a vehicle for subsequent spawns. Validated against the catalogue. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Player")
	void RequestVehicleSelection(FName VehicleId);

	/** Local-only pause notification. Multiplayer matches are never actually paused by this. */
	UFUNCTION(BlueprintCallable, Category = "KCF|Player")
	void RequestPause(bool bPaused);

	/**
	 * Asks the server to flip the kart upright where it stands.
	 * The escape hatch for a kart wedged in geometry or landed on its roof. Rate-limited server-side
	 * because an un-limited reset is a free teleport out of a bad position mid-fight.
	 */
	UFUNCTION(BlueprintCallable, Category = "KCF|Player")
	void RequestKartReset();

	// --- Server entry points ---------------------------------------------------------------------------

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetReady(bool bNewReady);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestTeamChange(int32 DesiredTeamId);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestRespawn();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestVehicleSelection(FName VehicleId);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestKartReset();

	// --- Server-to-owner notifications --------------------------------------------------------------------
	// Transient feedback that is not worth a replicated property. Called on the server, executed on
	// the owning client only.

	UFUNCTION(Client, Reliable)
	void ClientNotifyKill(AKCFPlayerState* Victim, FName WeaponId);

	UFUNCTION(Client, Unreliable)
	void ClientNotifyHit(float DamageDealt, bool bWasLethal);

	UFUNCTION(Client, Reliable)
	void ClientNotifyRespawnAvailable(float SecondsUntilRespawn);

protected:
	/** Input abstraction, created in the constructor so it exists before any UI can call it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "KCF|Input")
	TObjectPtr<UKCFKartInputComponent> KartInput;

	/**
	 * Server timestamp at which this player may next respawn. Owner-only: nobody else's HUD needs
	 * another player's respawn timer.
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "KCF|Player")
	float RespawnAvailableServerTime = -1.f;

	/** Rate limiter for team change requests, so a client cannot spam team swaps. */
	float LastTeamChangeRequestTime = -100.f;

	/** Minimum seconds between accepted team change requests. */
	static constexpr float TeamChangeCooldownSeconds = 3.f;

	/** Rate limiter for stuck-kart resets. */
	float LastKartResetRequestTime = -100.f;

	/** Minimum seconds between accepted kart resets. */
	static constexpr float KartResetCooldownSeconds = 5.f;

	/** Set by the respawn system. Server only. */
	void SetRespawnAvailableTime(float ServerTime);

	friend class UKCFRespawnComponent;

private:
	/** Shared rejection logging so every RPC reports refusals the same way. */
	void LogRejectedRequest(const TCHAR* RequestName, const TCHAR* Reason) const;
};
