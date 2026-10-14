// Original work, MIT licensed.

#include "Player/KCFPlayerState.h"

#include "Core/KCFLog.h"
#include "Net/UnrealNetwork.h"

AKCFPlayerState::AKCFPlayerState()
{
	// Scoreboards tolerate a fraction of a second of latency; sending this actor at the default rate
	// would be pure waste in an eight-player lobby.
	SetNetUpdateFrequency(2.f);
	SetMinNetUpdateFrequency(1.f);

	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
}

void AKCFPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AKCFPlayerState, KCFScore);
	DOREPLIFETIME(AKCFPlayerState, TeamId);
	DOREPLIFETIME(AKCFPlayerState, Kills);
	DOREPLIFETIME(AKCFPlayerState, Deaths);
	DOREPLIFETIME(AKCFPlayerState, Assists);
	DOREPLIFETIME(AKCFPlayerState, TeamKills);
	DOREPLIFETIME(AKCFPlayerState, PowerupsCollected);
	DOREPLIFETIME(AKCFPlayerState, CurrentKillStreak);
	DOREPLIFETIME(AKCFPlayerState, BestKillStreak);
	DOREPLIFETIME(AKCFPlayerState, DamageDealt);
	DOREPLIFETIME(AKCFPlayerState, DamageReceived);
	DOREPLIFETIME(AKCFPlayerState, LivesRemaining);
	DOREPLIFETIME(AKCFPlayerState, bEliminated);
	DOREPLIFETIME(AKCFPlayerState, bReady);
	DOREPLIFETIME(AKCFPlayerState, Placement);
	DOREPLIFETIME(AKCFPlayerState, SelectedVehicleId);

	// Only the owning client drives a respawn countdown from this; nobody else needs it.
	DOREPLIFETIME_CONDITION(AKCFPlayerState, LastDeathServerTime, COND_OwnerOnly);
}

void AKCFPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	// Called during seamless travel and when a reconnecting player's state is rebuilt. Without this,
	// a map change mid-session would silently wipe every scoreboard value.
	if (AKCFPlayerState* Other = Cast<AKCFPlayerState>(PlayerState))
	{
		Other->KCFScore = KCFScore;
		Other->TeamId = TeamId;
		Other->Kills = Kills;
		Other->Deaths = Deaths;
		Other->Assists = Assists;
		Other->TeamKills = TeamKills;
		Other->PowerupsCollected = PowerupsCollected;
		Other->CurrentKillStreak = CurrentKillStreak;
		Other->BestKillStreak = BestKillStreak;
		Other->DamageDealt = DamageDealt;
		Other->DamageReceived = DamageReceived;
		Other->LivesRemaining = LivesRemaining;
		Other->bEliminated = bEliminated;
		Other->Placement = Placement;
		Other->SelectedVehicleId = SelectedVehicleId;

		// Ready state is intentionally not carried over: arriving in a new lobby should require an
		// explicit ready-up rather than starting a match a player has not looked at.
	}
}

void AKCFPlayerState::OverrideWith(APlayerState* PlayerState)
{
	Super::OverrideWith(PlayerState);

	if (const AKCFPlayerState* Other = Cast<AKCFPlayerState>(PlayerState))
	{
		KCFScore = Other->KCFScore;
		TeamId = Other->TeamId;
		Kills = Other->Kills;
		Deaths = Other->Deaths;
		Assists = Other->Assists;
		TeamKills = Other->TeamKills;
		PowerupsCollected = Other->PowerupsCollected;
		CurrentKillStreak = Other->CurrentKillStreak;
		BestKillStreak = Other->BestKillStreak;
		DamageDealt = Other->DamageDealt;
		DamageReceived = Other->DamageReceived;
		LivesRemaining = Other->LivesRemaining;
		bEliminated = Other->bEliminated;
		Placement = Other->Placement;
		SelectedVehicleId = Other->SelectedVehicleId;
	}
}

void AKCFPlayerState::SetKCFTeamId_Implementation(int32 NewTeamId)
{
	if (!HasAuthority())
	{
		KCF_REPORT_RECOVERABLE(LogKCFNet, TEXT("SetKCFTeamId ignored: team assignment is server-authoritative."));
		return;
	}

	if (TeamId == NewTeamId)
	{
		return;
	}

	TeamId = NewTeamId;
	OnRep_TeamId();
}

float AKCFPlayerState::GetKillDeathRatio() const
{
	return static_cast<float>(Kills) / static_cast<float>(FMath::Max(1, Deaths));
}

// -------------------------------------------------------------------------------------------------
// Server-only mutators
// -------------------------------------------------------------------------------------------------

void AKCFPlayerState::AddScore(int32 Delta)
{
	if (!HasAuthority() || Delta == 0)
	{
		return;
	}

	KCFScore += Delta;

	// Mirror into the engine's float Score so that engine-side scoreboard utilities and any
	// third-party code reading APlayerState::GetScore() see a consistent value.
	SetScore(static_cast<float>(KCFScore));

	OnRep_KCFScore();
}

void AKCFPlayerState::RecordKill(bool bWasTeamKill)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bWasTeamKill)
	{
		++TeamKills;
	}
	else
	{
		++Kills;
		++CurrentKillStreak;
		BestKillStreak = FMath::Max(BestKillStreak, CurrentKillStreak);
	}

	BroadcastStatsChanged();
}

void AKCFPlayerState::RecordDeath()
{
	if (!HasAuthority())
	{
		return;
	}

	++Deaths;
	CurrentKillStreak = 0;

	if (LivesRemaining > 0)
	{
		SetLivesRemaining(LivesRemaining - 1);
	}

	BroadcastStatsChanged();
}

void AKCFPlayerState::RecordAssist()
{
	if (!HasAuthority())
	{
		return;
	}

	++Assists;
	BroadcastStatsChanged();
}

void AKCFPlayerState::AddDamageDealt(float Amount)
{
	if (!HasAuthority() || Amount <= 0.f || !FMath::IsFinite(Amount))
	{
		return;
	}

	const float Previous = DamageDealt;
	DamageDealt = FMath::RoundToFloat(DamageDealt + Amount);

	// Quantising to whole points means a long string of small ticks only dirties the property when
	// the displayed number would actually change.
	if (!FMath::IsNearlyEqual(Previous, DamageDealt))
	{
		BroadcastStatsChanged();
	}
}

void AKCFPlayerState::AddDamageReceived(float Amount)
{
	if (!HasAuthority() || Amount <= 0.f || !FMath::IsFinite(Amount))
	{
		return;
	}

	const float Previous = DamageReceived;
	DamageReceived = FMath::RoundToFloat(DamageReceived + Amount);

	if (!FMath::IsNearlyEqual(Previous, DamageReceived))
	{
		BroadcastStatsChanged();
	}
}

void AKCFPlayerState::RecordPowerupCollected()
{
	if (!HasAuthority())
	{
		return;
	}

	++PowerupsCollected;
	BroadcastStatsChanged();
}

void AKCFPlayerState::SetLivesRemaining(int32 NewLives)
{
	if (!HasAuthority())
	{
		return;
	}

	LivesRemaining = FMath::Max(0, NewLives);
}

void AKCFPlayerState::SetEliminated(bool bNewEliminated)
{
	if (!HasAuthority() || bEliminated == bNewEliminated)
	{
		return;
	}

	bEliminated = bNewEliminated;
	OnRep_Eliminated();
}

void AKCFPlayerState::SetPlacement(int32 NewPlacement)
{
	if (!HasAuthority())
	{
		return;
	}

	Placement = FMath::Max(0, NewPlacement);
}

void AKCFPlayerState::SetReady(bool bNewReady)
{
	if (!HasAuthority() || bReady == bNewReady)
	{
		return;
	}

	bReady = bNewReady;
	OnRep_Ready();
}

void AKCFPlayerState::SetSelectedVehicleId(FName NewVehicleId)
{
	if (!HasAuthority())
	{
		return;
	}

	SelectedVehicleId = NewVehicleId;
}

void AKCFPlayerState::ResetMatchStats(int32 StartingLives)
{
	if (!HasAuthority())
	{
		return;
	}

	KCFScore = 0;
	SetScore(0.f);
	Kills = 0;
	Deaths = 0;
	Assists = 0;
	TeamKills = 0;
	PowerupsCollected = 0;
	CurrentKillStreak = 0;
	BestKillStreak = 0;
	DamageDealt = 0.f;
	DamageReceived = 0.f;
	Placement = 0;
	LastDeathServerTime = -1.f;
	LivesRemaining = FMath::Max(0, StartingLives);
	bEliminated = false;

	OnRep_KCFScore();
	BroadcastStatsChanged();
}

void AKCFPlayerState::MarkDeathTime(float ServerTime)
{
	if (!HasAuthority())
	{
		return;
	}

	LastDeathServerTime = ServerTime;
}

// -------------------------------------------------------------------------------------------------
// OnRep handlers
// -------------------------------------------------------------------------------------------------

void AKCFPlayerState::OnRep_KCFScore()
{
	OnScoreChanged.Broadcast(this, KCFScore);
}

void AKCFPlayerState::OnRep_TeamId()
{
	OnTeamChanged.Broadcast(this, TeamId);
}

void AKCFPlayerState::OnRep_Stats()
{
	BroadcastStatsChanged();
}

void AKCFPlayerState::OnRep_Eliminated()
{
	if (bEliminated)
	{
		OnEliminated.Broadcast(this);
	}
	BroadcastStatsChanged();
}

void AKCFPlayerState::OnRep_Ready()
{
	OnReadyStateChanged.Broadcast(this, bReady);
}

void AKCFPlayerState::BroadcastStatsChanged()
{
	OnStatsChanged.Broadcast(this);
}
