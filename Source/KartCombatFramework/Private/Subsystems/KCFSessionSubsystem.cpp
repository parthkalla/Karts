// Original work, MIT licensed.

#include "Subsystems/KCFSessionSubsystem.h"

#include "Core/KCFLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

// -------------------------------------------------------------------------------------------------
// Subsystem
// -------------------------------------------------------------------------------------------------

void UKCFSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	InstallDefaultService();
}

void UKCFSessionSubsystem::Deinitialize()
{
	SessionService = nullptr;
	LastSearchResults.Reset();
	Super::Deinitialize();
}

UKCFSessionSubsystem* UKCFSessionSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine && WorldContextObject
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: nullptr;

	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UKCFSessionSubsystem>() : nullptr;
}

void UKCFSessionSubsystem::InstallDefaultService()
{
	UKCFNullSessionService* Default = NewObject<UKCFNullSessionService>(this);
	SessionService.SetObject(Default);
	SessionService.SetInterface(static_cast<IKCFSessionService*>(Default));
}

void UKCFSessionSubsystem::SetSessionService(TScriptInterface<IKCFSessionService> NewService)
{
	if (NewService && NewService.GetInterface())
	{
		SessionService = NewService;
		UE_LOG(LogKCFNet, Log, TEXT("Session backend set to '%s'."), *GetServiceName().ToString());
		return;
	}

	// Restoring the LAN service rather than leaving the subsystem without one: a null backend would
	// turn every subsequent call into a silent failure.
	InstallDefaultService();
	UE_LOG(LogKCFNet, Warning, TEXT("SetSessionService was given no usable service; reverted to the built-in LAN backend."));
}

FName UKCFSessionSubsystem::GetServiceName() const
{
	const IKCFSessionService* Service = SessionService.GetInterface();
	return Service ? Service->GetServiceName() : NAME_None;
}

bool UKCFSessionSubsystem::IsServiceAvailable() const
{
	const IKCFSessionService* Service = SessionService.GetInterface();
	return Service && Service->IsAvailable();
}

void UKCFSessionSubsystem::CreateSession(const FKCFSessionSettings& Settings)
{
	IKCFSessionService* Service = SessionService.GetInterface();
	if (!Service)
	{
		OnCreateSessionComplete.Broadcast(false, TEXT("No session backend is installed."));
		return;
	}

	Service->CreateSession(Settings, FKCFSessionOperationDelegate::CreateWeakLambda(this,
		[this](bool bSuccess, const FString& Error)
		{
			OnCreateSessionComplete.Broadcast(bSuccess, Error);
		}));
}

void UKCFSessionSubsystem::FindSessions(bool bLANOnly, int32 MaxResults)
{
	IKCFSessionService* Service = SessionService.GetInterface();
	if (!Service)
	{
		OnFindSessionsComplete.Broadcast(false, {});
		return;
	}

	Service->FindSessions(bLANOnly, MaxResults, FKCFSessionSearchDelegate::CreateWeakLambda(this,
		[this](bool bSuccess, const TArray<FKCFSessionSearchResult>& Results, const FString& /*Error*/)
		{
			LastSearchResults = Results;
			OnFindSessionsComplete.Broadcast(bSuccess, LastSearchResults);
		}));
}

void UKCFSessionSubsystem::JoinSession(const FKCFSessionSearchResult& Result)
{
	IKCFSessionService* Service = SessionService.GetInterface();
	if (!Service)
	{
		OnJoinSessionComplete.Broadcast(false, TEXT("No session backend is installed."));
		return;
	}

	Service->JoinSession(Result, FKCFSessionOperationDelegate::CreateWeakLambda(this,
		[this](bool bSuccess, const FString& Error)
		{
			OnJoinSessionComplete.Broadcast(bSuccess, Error);
		}));
}

void UKCFSessionSubsystem::LeaveSession()
{
	IKCFSessionService* Service = SessionService.GetInterface();
	if (!Service)
	{
		OnLeaveSessionComplete.Broadcast(false, TEXT("No session backend is installed."));
		return;
	}

	Service->LeaveSession(FKCFSessionOperationDelegate::CreateWeakLambda(this,
		[this](bool bSuccess, const FString& Error)
		{
			OnLeaveSessionComplete.Broadcast(bSuccess, Error);
		}));
}

void UKCFSessionSubsystem::DestroySession()
{
	IKCFSessionService* Service = SessionService.GetInterface();
	if (!Service)
	{
		OnDestroySessionComplete.Broadcast(false, TEXT("No session backend is installed."));
		return;
	}

	Service->DestroySession(FKCFSessionOperationDelegate::CreateWeakLambda(this,
		[this](bool bSuccess, const FString& Error)
		{
			OnDestroySessionComplete.Broadcast(bSuccess, Error);
		}));
}

void UKCFSessionSubsystem::NotifyMatchStarted()
{
	if (IKCFSessionService* Service = SessionService.GetInterface())
	{
		Service->StartMatch(FKCFSessionOperationDelegate());
	}
}

void UKCFSessionSubsystem::NotifyMatchEnded()
{
	if (IKCFSessionService* Service = SessionService.GetInterface())
	{
		Service->EndMatch(FKCFSessionOperationDelegate());
	}
}

// -------------------------------------------------------------------------------------------------
// Built-in LAN backend
// -------------------------------------------------------------------------------------------------

namespace KCFNullSession
{
	/** Invokes a completion delegate only when the caller bound one. */
	static void Complete(const FKCFSessionOperationDelegate& Delegate, bool bSuccess, const FString& Error)
	{
		Delegate.ExecuteIfBound(bSuccess, Error);
	}
}

void UKCFNullSessionService::CreateSession(const FKCFSessionSettings& Settings, FKCFSessionOperationDelegate OnComplete)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		KCFNullSession::Complete(OnComplete, false, TEXT("No world is available to host from."));
		return;
	}

	if (Settings.MapName.IsEmpty())
	{
		KCFNullSession::Complete(OnComplete, false, TEXT("Hosting requires a map name in the session settings."));
		return;
	}

	// ?listen turns the travel into a listen server. Everything else - max players, the game mode
	// rule set - is the game mode's business and is read on the far side of the travel.
	const FString TravelURL = FString::Printf(TEXT("%s?listen?MaxPlayers=%d?GameModeId=%s"),
		*Settings.MapName,
		Settings.MaxPlayers,
		*Settings.GameModeId.ToString());

	bHostingSession = true;
	UE_LOG(LogKCFNet, Log, TEXT("LAN backend hosting: %s"), *TravelURL);

	World->ServerTravel(TravelURL, true);
	KCFNullSession::Complete(OnComplete, true, FString());
}

void UKCFNullSessionService::FindSessions(bool /*bLANOnly*/, int32 MaxResults, FKCFSessionSearchDelegate OnComplete)
{
	// No discovery protocol exists behind an engine-only backend, so the result set is exactly what
	// the project registered. Returning success with an empty list is the honest answer; pretending
	// to search would make a missing online subsystem look like an empty LAN.
	TArray<FKCFSessionSearchResult> Results = KnownServers;
	if (MaxResults > 0 && Results.Num() > MaxResults)
	{
		Results.SetNum(MaxResults);
	}

	OnComplete.ExecuteIfBound(true, Results, FString());
}

void UKCFNullSessionService::JoinSession(const FKCFSessionSearchResult& Result, FKCFSessionOperationDelegate OnComplete)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;

	if (!PC)
	{
		KCFNullSession::Complete(OnComplete, false, TEXT("No local player controller to travel with."));
		return;
	}

	if (Result.ConnectString.IsEmpty())
	{
		KCFNullSession::Complete(OnComplete, false, TEXT("The session has no connect string."));
		return;
	}

	UE_LOG(LogKCFNet, Log, TEXT("LAN backend joining: %s"), *Result.ConnectString);
	PC->ClientTravel(Result.ConnectString, ETravelType::TRAVEL_Absolute);

	KCFNullSession::Complete(OnComplete, true, FString());
}

void UKCFNullSessionService::LeaveSession(FKCFSessionOperationDelegate OnComplete)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;

	if (!PC || MainMenuMap.IsEmpty())
	{
		KCFNullSession::Complete(OnComplete, false, TEXT("Set MainMenuMap on the session service before leaving a session."));
		return;
	}

	bHostingSession = false;
	PC->ClientTravel(MainMenuMap, ETravelType::TRAVEL_Absolute);
	KCFNullSession::Complete(OnComplete, true, FString());
}

void UKCFNullSessionService::DestroySession(FKCFSessionOperationDelegate OnComplete)
{
	// For a listen server, destroying the session and leaving it are the same operation: there is
	// no backend registration to tear down.
	LeaveSession(OnComplete);
}

void UKCFNullSessionService::StartMatch(FKCFSessionOperationDelegate OnComplete)
{
	// Nothing to advertise and nothing to stop advertising. Reported as a success rather than a
	// failure so that callers do not have to special-case the LAN backend.
	KCFNullSession::Complete(OnComplete, true, FString());
}

void UKCFNullSessionService::EndMatch(FKCFSessionOperationDelegate OnComplete)
{
	KCFNullSession::Complete(OnComplete, true, FString());
}

void UKCFNullSessionService::AddKnownServer(const FString& Address, const FString& DisplayName)
{
	if (Address.IsEmpty())
	{
		return;
	}

	FKCFSessionSearchResult Entry;
	Entry.SessionId = Address;
	Entry.DisplayName = DisplayName.IsEmpty() ? Address : DisplayName;
	Entry.ConnectString = Address;
	KnownServers.Add(Entry);
}

void UKCFNullSessionService::ClearKnownServers()
{
	KnownServers.Reset();
}
