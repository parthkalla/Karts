// Original work, MIT licensed.

#include "Match/KCFRaceProgressComponent.h"

#include "Core/KCFLog.h"
#include "GameFramework/Actor.h"
#include "Match/KCFRaceCheckpoint.h"
#include "Net/UnrealNetwork.h"

UKCFRaceProgressComponent::UKCFRaceProgressComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UKCFRaceProgressComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Everything here is public information in a race: the position board shows every competitor's
	// lap and split, so there is nothing to restrict to the owner.
	DOREPLIFETIME(UKCFRaceProgressComponent, CompletedLaps);
	DOREPLIFETIME(UKCFRaceProgressComponent, NextCheckpointOrdinal);
	DOREPLIFETIME(UKCFRaceProgressComponent, TotalCheckpointsPassed);
	DOREPLIFETIME(UKCFRaceProgressComponent, BestLapSeconds);
	DOREPLIFETIME(UKCFRaceProgressComponent, LastLapSeconds);
	DOREPLIFETIME(UKCFRaceProgressComponent, TotalRaceSeconds);
	DOREPLIFETIME(UKCFRaceProgressComponent, bFinished);
}

void UKCFRaceProgressComponent::BeginRace(float ServerTime)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	CompletedLaps = 0;
	NextCheckpointOrdinal = 0;
	TotalCheckpointsPassed = 0;
	BestLapSeconds = -1.f;
	LastLapSeconds = -1.f;
	TotalRaceSeconds = -1.f;
	bFinished = false;
	LastCheckpoint.Reset();

	RaceStartTime = ServerTime;
	CurrentLapStartTime = ServerTime;

	OnRep_Progress();
}

bool UKCFRaceProgressComponent::RecordCheckpoint(AKCFRaceCheckpoint* Checkpoint, int32 Ordinal, int32 CheckpointCount, float ServerTime)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || bFinished || CheckpointCount <= 0)
	{
		return false;
	}

	// The whole anti-shortcut rule is this one comparison: only the gate you are actually due to
	// cross counts. Driving backwards through a gate you have already passed, or cutting the track
	// to reach a later one, changes nothing.
	if (Ordinal != NextCheckpointOrdinal)
	{
		return false;
	}

	LastCheckpoint = Checkpoint;
	++TotalCheckpointsPassed;

	const bool bCompletedLap = (NextCheckpointOrdinal == CheckpointCount - 1);
	NextCheckpointOrdinal = bCompletedLap ? 0 : NextCheckpointOrdinal + 1;

	if (bCompletedLap)
	{
		++CompletedLaps;

		if (CurrentLapStartTime >= 0.f)
		{
			LastLapSeconds = FMath::Max(0.f, ServerTime - CurrentLapStartTime);
			if (BestLapSeconds < 0.f || LastLapSeconds < BestLapSeconds)
			{
				BestLapSeconds = LastLapSeconds;
			}
		}

		CurrentLapStartTime = ServerTime;
	}

	OnRep_Progress();
	return bCompletedLap;
}

void UKCFRaceProgressComponent::MarkFinished(float ServerTime)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || bFinished)
	{
		return;
	}

	bFinished = true;
	TotalRaceSeconds = RaceStartTime >= 0.f ? FMath::Max(0.f, ServerTime - RaceStartTime) : -1.f;

	OnRep_Progress();
}

void UKCFRaceProgressComponent::OnRep_Progress()
{
	OnProgressChanged.Broadcast();

	// Compared against a mirror rather than just checking CompletedLaps, because this handler also
	// runs for a checkpoint crossing mid-lap and a lap-completed banner must not fire then.
	if (CompletedLaps > LastBroadcastLap)
	{
		LastBroadcastLap = CompletedLaps;
		OnLapCompleted.Broadcast(CompletedLaps);
	}
	else if (CompletedLaps < LastBroadcastLap)
	{
		LastBroadcastLap = CompletedLaps;
	}
}
