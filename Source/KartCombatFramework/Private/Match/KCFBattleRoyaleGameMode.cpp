// Original work, MIT licensed.

#include "Match/KCFBattleRoyaleGameMode.h"

#include "Core/KCFLog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Match/KCFPlayerSpawnPoint.h"
#include "Match/KCFPlayZone.h"

AKCFBattleRoyaleGameMode::AKCFBattleRoyaleGameMode()
{
	ZoneClass = AKCFPlayZone::StaticClass();
}

void AKCFBattleRoyaleGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Resolved early rather than at match start so a missing zone is reported while the lobby is
	// still filling, not in the first second of play.
	ResolvePlayZone();
}

AKCFPlayZone* AKCFBattleRoyaleGameMode::ResolvePlayZone()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return nullptr;
	}

	if (PlayZone)
	{
		return PlayZone;
	}

	// A hand-placed zone wins: its centre, opening radius and phase schedule were authored against
	// this particular arena, which nothing computed here can match.
	for (TActorIterator<AKCFPlayZone> It(World); It; ++It)
	{
		PlayZone = *It;
		break;
	}

	if (PlayZone)
	{
		return PlayZone;
	}

	if (!ZoneClass)
	{
		UE_LOG(LogKCFMatch, Error,
			TEXT("Battle royale has no play zone: the level contains none and ZoneClass is unset. ")
			TEXT("The match will run, but nothing will force it to end."));
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	PlayZone = World->SpawnActor<AKCFPlayZone>(ZoneClass, FTransform::Identity, Params);

	if (PlayZone)
	{
		UE_LOG(LogKCFMatch, Log, TEXT("No play zone in the level; spawned one from '%s'."), *GetNameSafe(ZoneClass));
	}

	return PlayZone;
}

FVector AKCFBattleRoyaleGameMode::ComputeArenaCenter() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return FVector::ZeroVector;
	}

	// The spawn points are the only description of the playable area the framework has that does
	// not require the project to author anything extra, and they bound it well in practice.
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;

	for (TActorIterator<AKCFPlayerSpawnPoint> It(World); It; ++It)
	{
		if (const AKCFPlayerSpawnPoint* Point = *It)
		{
			Sum += Point->GetActorLocation();
			++Count;
		}
	}

	return Count > 0 ? Sum / static_cast<float>(Count) : FVector::ZeroVector;
}

void AKCFBattleRoyaleGameMode::OnMatchStateChanged(EKCFMatchState OldState, EKCFMatchState NewState)
{
	Super::OnMatchStateChanged(OldState, NewState);

	AKCFPlayZone* Zone = ResolvePlayZone();
	if (!Zone)
	{
		return;
	}

	switch (NewState)
	{
	case EKCFMatchState::InProgress:
	{
		// A zone left at the origin was spawned by this mode rather than placed, so its position
		// carries no intent and the spawn points are a better guess at where the arena is.
		const FVector PlacedCenter = Zone->GetActorLocation();
		const FVector Center = PlacedCenter.IsNearlyZero() ? ComputeArenaCenter() : PlacedCenter;

		// Started here rather than at Preparing so the circle does not begin closing while players
		// are still streaming in and frozen on the grid.
		Zone->StartZone(Center, InitialZoneRadius);
		break;
	}

	case EKCFMatchState::Finished:
	case EKCFMatchState::ReturningToLobby:
		Zone->StopZone();
		break;

	default:
		break;
	}
}

bool AKCFBattleRoyaleGameMode::GetRespawnTransformOverride(AController* Controller, FTransform& OutTransform) const
{
	if (!bRespawnInsideZone || !PlayZone || !Controller)
	{
		return false;
	}

	// Only intervenes when the normal spawn would be lethal. Leaving a usable spawn point alone
	// keeps the designer's placement in charge everywhere it still works.
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const AKCFPlayerSpawnPoint* BestInside = nullptr;
	float BestDistanceSquared = MAX_flt;
	const FVector Center = PlayZone->GetZoneCenter();

	for (TActorIterator<AKCFPlayerSpawnPoint> It(World); It; ++It)
	{
		const AKCFPlayerSpawnPoint* Point = *It;
		if (!Point || !Point->bEnabled || !PlayZone->IsInsideZone(Point->GetActorLocation()))
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(Point->GetActorLocation(), Center);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			BestInside = Point;
		}
	}

	if (BestInside)
	{
		OutTransform = BestInside->GetActorTransform();
		return true;
	}

	// Every spawn point is outside the circle, which happens once it has closed past them. Dropping
	// the player at the centre is survivable; respawning them into the damage would not be.
	OutTransform = FTransform(FRotator::ZeroRotator, Center + FVector(0.f, 0.f, 200.f));
	return true;
}
