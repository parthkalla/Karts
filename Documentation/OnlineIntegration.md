# Online Integration

The framework does not hard-code EOS, Steam, or any other online backend. Core gameplay talks to
`IKCFSessionService`, and a project supplies the implementation.

## Why it is an interface

An online backend is a platform and business decision that changes over a project's life — Steam for the
PC beta, EOS for crossplay, a custom backend when the publisher has one, a null service in automated
tests. Each of those swaps out a dependency, a config section, and a set of async callbacks.

If `AKCFGameMode` included `OnlineSubsystem.h`, every one of those swaps would be a plugin edit, and the
plugin would be unusable on any platform whose subsystem it did not already know about. Behind an
interface, the swap is one line in your game instance.

The core module therefore has **no** dependency on `OnlineSubsystem`, `OnlineSubsystemUtils`,
`OnlineSubsystemEOS` or `OnlineSubsystemSteam`.

## The interface

```cpp
class IKCFSessionService
{
    virtual FName GetServiceName() const = 0;
    virtual bool  IsAvailable() const = 0;

    virtual void CreateSession(const FKCFSessionSettings& Settings, FKCFSessionOperationDelegate OnComplete) = 0;
    virtual void FindSessions(bool bLANOnly, int32 MaxResults, FKCFSessionSearchDelegate OnComplete) = 0;
    virtual void JoinSession(const FKCFSessionSearchResult& Result, FKCFSessionOperationDelegate OnComplete) = 0;
    virtual void LeaveSession(FKCFSessionOperationDelegate OnComplete) = 0;
    virtual void DestroySession(FKCFSessionOperationDelegate OnComplete) = 0;

    virtual void StartMatch(FKCFSessionOperationDelegate OnComplete) = 0;
    virtual void EndMatch(FKCFSessionOperationDelegate OnComplete) = 0;
};
```

Every operation is asynchronous and takes a completion delegate, because every real backend is
asynchronous. A synchronous signature would be a lie that each implementation had to work around.

`StartMatch` and `EndMatch` exist separately from create/destroy because most backends distinguish "a
lobby exists" from "a match is in progress" — it is what stops new players joining a finished match and
what drives server browser state.

### The data structures

`FKCFSessionSettings` (hosting): `SessionDisplayName`, `MapName`, `GameModeId`, `MaxPlayers`, `bIsLAN`,
`bIsPrivate`, `ExtraAttributes`.

`FKCFSessionSearchResult` (a discovered session): `SessionId`, `DisplayName`, `GameModeId`,
`CurrentPlayers`, `MaxPlayers`, `PingMs`, `ConnectString`, `ExtraAttributes`.

`ExtraAttributes` is a free-form `TMap<FString, FString>` forwarded to the backend as session attributes.
It is the escape hatch for anything backend-specific — region, skill bracket, build version — without the
framework having to know about any of it.

`SessionId` is opaque. Pass it back to `JoinSession` verbatim; never parse it.

## The subsystem

`UKCFSessionSubsystem` is a `UGameInstanceSubsystem` that owns the active service and exposes it to
Blueprint.

```cpp
FName GetServiceName() const;
bool  IsServiceAvailable() const;

void CreateSession(const FKCFSessionSettings& Settings);
void FindSessions(bool bLANOnly = true, int32 MaxResults = 32);
void JoinSession(const FKCFSessionSearchResult& Result);
void LeaveSession();
void DestroySession();

const TArray<FKCFSessionSearchResult>& GetLastSearchResults() const;
```

Blueprint delegates: `OnCreateSessionComplete`, `OnFindSessionsComplete`, `OnJoinSessionComplete`,
`OnLeaveSessionComplete`, `OnDestroySessionComplete`.

On initialise it installs `UKCFNullSessionService` so the subsystem is never in a state where calls
silently do nothing. `SetSessionService` replaces it, and reverts to the built-in LAN service if handed
something unusable — a null backend would turn every subsequent call into a silent failure, which is the
hardest kind of bug to find.

## The built-in LAN service

`UKCFNullSessionService` is a real, working implementation for local development. It reports its name as
`LAN` and is always available.

| Operation | What it does |
| --- | --- |
| `CreateSession` | `ServerTravel` to `Settings.MapName` with `?listen` |
| `FindSessions` | Returns servers registered with `AddKnownServer`. **Does not discover** |
| `JoinSession` | `ClientTravel` to `Result.ConnectString` |
| `LeaveSession` | `ClientTravel` back to `MainMenuMap` |
| `DestroySession` | `ServerTravel` back to `MainMenuMap` |
| `StartMatch` / `EndMatch` | Logs and completes successfully |

```cpp
void AddKnownServer(const FString& Address, const FString& DisplayName);
void ClearKnownServers();
```

**The honest limitation** (also in the [README](README.md#known-limitations)): there is no UDP broadcast,
no scan, no discovery of any kind. `FindSessions` returns exactly the servers you registered. That is
enough for a direct-connect menu and for testing, and it is not matchmaking.

The framework ships it rather than a stub because a stub that returns `true` and does nothing is worse than
useless — it looks like it works until you test with two machines.

## Writing a backend

Do this in your **game module**, not in the plugin.

### 1. Add the dependency to your module

```csharp
PrivateDependencyModuleNames.AddRange(new string[]
{
    "OnlineSubsystem",
    "OnlineSubsystemUtils",
});
```

### 2. Implement the interface

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Interfaces/KCFSessionServiceInterface.h"
#include "MyEOSSessionService.generated.h"

UCLASS()
class UMyEOSSessionService : public UObject, public IKCFSessionService
{
    GENERATED_BODY()

public:
    virtual FName GetServiceName() const override { return TEXT("EOS"); }
    virtual bool  IsAvailable() const override;

    virtual void CreateSession(const FKCFSessionSettings& Settings,
                               FKCFSessionOperationDelegate OnComplete) override;
    virtual void FindSessions(bool bLANOnly, int32 MaxResults,
                              FKCFSessionSearchDelegate OnComplete) override;
    virtual void JoinSession(const FKCFSessionSearchResult& Result,
                             FKCFSessionOperationDelegate OnComplete) override;
    virtual void LeaveSession(FKCFSessionOperationDelegate OnComplete) override;
    virtual void DestroySession(FKCFSessionOperationDelegate OnComplete) override;
    virtual void StartMatch(FKCFSessionOperationDelegate OnComplete) override;
    virtual void EndMatch(FKCFSessionOperationDelegate OnComplete) override;

private:
    FKCFSessionOperationDelegate PendingCreate;
    FDelegateHandle CreateHandle;
};
```

Mapping to the online session interface:

```cpp
void UMyEOSSessionService::CreateSession(const FKCFSessionSettings& Settings,
                                         FKCFSessionOperationDelegate OnComplete)
{
    IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
    const IOnlineSessionPtr Sessions = OSS ? OSS->GetSessionInterface() : nullptr;
    if (!Sessions.IsValid())
    {
        OnComplete.ExecuteIfBound(false, TEXT("No online subsystem available."));
        return;
    }

    FOnlineSessionSettings OnlineSettings;
    OnlineSettings.NumPublicConnections   = Settings.MaxPlayers;
    OnlineSettings.bIsLANMatch            = Settings.bIsLAN;
    OnlineSettings.bShouldAdvertise       = !Settings.bIsPrivate;
    OnlineSettings.bUsesPresence          = true;
    OnlineSettings.bAllowJoinInProgress   = true;

    OnlineSettings.Set(SETTING_MAPNAME, Settings.MapName, EOnlineDataAdvertisementType::ViaOnlineService);
    OnlineSettings.Set(FName("GAMEMODE"), Settings.GameModeId.ToString(),
                       EOnlineDataAdvertisementType::ViaOnlineService);

    for (const TPair<FString, FString>& Attribute : Settings.ExtraAttributes)
    {
        OnlineSettings.Set(FName(*Attribute.Key), Attribute.Value,
                           EOnlineDataAdvertisementType::ViaOnlineService);
    }

    PendingCreate = OnComplete;
    CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
        FOnCreateSessionCompleteDelegate::CreateUObject(this, &UMyEOSSessionService::HandleCreated));

    Sessions->CreateSession(0, NAME_GameSession, OnlineSettings);
}
```

Three things to get right in every implementation:

- **Always fire the delegate**, including on the early-out paths. A caller waiting on a delegate that never
  arrives is a hung menu.
- **Clear your delegate handles** in the completion callback. Accumulated handles cause duplicate
  callbacks, which look like double-joins.
- **Populate `ConnectString`** in `FindSessions` results, because `JoinSession` travels to it.

### 3. Install it

In your game instance's `Init`:

```cpp
void UMyGameInstance::Init()
{
    Super::Init();

    if (UKCFSessionSubsystem* SessionSubsystem = GetSubsystem<UKCFSessionSubsystem>())
    {
        UMyEOSSessionService* Service = NewObject<UMyEOSSessionService>(this);
        SessionSubsystem->SetSessionService(Service);
    }
}
```

One line changes the whole game's backend. Nothing in gameplay code knows or cares.

### Choosing at runtime

```cpp
if (IOnlineSubsystem::Get(STEAM_SUBSYSTEM) != nullptr)
{
    SessionSubsystem->SetSessionService(NewObject<UMySteamSessionService>(this));
}
else if (IOnlineSubsystem::Get(FName("EOS")) != nullptr)
{
    SessionSubsystem->SetSessionService(NewObject<UMyEOSSessionService>(this));
}
// else: the built-in LAN service stays installed.
```

## Match lifecycle

`AKCFGameMode` notifies the session service at two points:

```cpp
NotifySessionMatchStarted()   // on entering InProgress
NotifySessionMatchEnded()     // on entering Finished
```

These call `StartMatch` and `EndMatch` on the active service. Implement them to update your backend's
session state — closing the session to joiners when the match starts, reporting results when it ends.

`EKCFMatchEndReason` tells you which of `TimeLimit`, `ScoreLimit`, `LastStanding`, `NotEnoughPlayers`,
`HostEnded` or `ObjectiveComplete` happened, which is usually what a results report wants.

Both are `virtual`, so a game mode subclass can extend them for its own telemetry without replacing the
session call.

## A session flow in Blueprint

**Host:**

```
Build FKCFSessionSettings (DisplayName, MapName, GameModeId, MaxPlayers, bIsLAN)
  → Session Subsystem → Create Session
  → bind OnCreateSessionComplete
      success → travel happens automatically
      failure → show the error string
```

**Browse and join:**

```
Session Subsystem → Find Sessions (bLANOnly, MaxResults)
  → bind OnFindSessionsComplete(bSuccess, Results)
      → populate a list from DisplayName / CurrentPlayers / MaxPlayers / PingMs
  → on click → Join Session (the FKCFSessionSearchResult for that row)
  → bind OnJoinSessionComplete
```

Pass the whole `FKCFSessionSearchResult` back, not just its id. It carries the `ConnectString` the service
needs.

**Leave:**

```
Session Subsystem → Leave Session        (client)
Session Subsystem → Destroy Session      (host)
```

## Testing without a backend

The LAN service is enough for most development:

1. Package or run two instances.
2. On the host, `CreateSession` with the map you want.
3. On the client, either `AddKnownServer("127.0.0.1:7777", "Local")` then `FindSessions`, or type
   `open 127.0.0.1:7777` in the console.

For a dedicated server, start it with the map on the command line and have clients `open` the address
directly. No session service is involved.

## Platform notes

**Steam** needs `OnlineSubsystemSteam` configured in `DefaultEngine.ini` with your app id, and the Steam
client running. Presence and lobbies come through the standard session interface.

**EOS** needs `OnlineSubsystemEOS` and the EOS product/sandbox/deployment ids in `DefaultEngine.ini`.
`bUsesPresence` matters for invites.

**Console platforms** have their own subsystems with their own certification requirements — session
privacy, invite handling, and player blocking all have rules. The interface boundary is the same; the
implementation is platform-specific, which is exactly why it is not in the plugin.
