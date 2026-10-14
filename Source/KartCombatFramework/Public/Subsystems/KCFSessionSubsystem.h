// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/KCFSessionServiceInterface.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "KCFSessionSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKCFSessionOperationResultSignature, bool, bSuccess, const FString&, ErrorMessage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FKCFSessionSearchResultSignature, bool, bSuccess, const TArray<FKCFSessionSearchResult>&, Results);

/**
 * The only session-aware class in the framework.
 *
 * Gameplay never calls an online backend. It calls this, and this forwards to whichever
 * IKCFSessionService implementation is installed. Swapping LAN for EOS is a matter of registering a
 * different service object; no gameplay file changes, which is the whole reason the seam exists.
 *
 * The default service is UKCFNullSessionService, a working LAN/listen-server implementation built on
 * engine travel alone. It is not a stub: a developer can host and join a real multiplayer match with
 * it before integrating any online subsystem.
 */
UCLASS()
class KARTCOMBATFRAMEWORK_API UKCFSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	UFUNCTION(BlueprintPure, Category = "KCF|Session", meta = (WorldContext = "WorldContextObject"))
	static UKCFSessionSubsystem* Get(const UObject* WorldContextObject);

	// --- Delegates -------------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "KCF|Session") FKCFSessionOperationResultSignature OnCreateSessionComplete;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Session") FKCFSessionSearchResultSignature OnFindSessionsComplete;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Session") FKCFSessionOperationResultSignature OnJoinSessionComplete;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Session") FKCFSessionOperationResultSignature OnLeaveSessionComplete;
	UPROPERTY(BlueprintAssignable, Category = "KCF|Session") FKCFSessionOperationResultSignature OnDestroySessionComplete;

	// --- Service management ------------------------------------------------------------------------

	/**
	 * Installs a backend. Call this once at startup, before any session operation.
	 * Passing null restores the built-in LAN service rather than leaving the subsystem unusable.
	 */
	UFUNCTION(BlueprintCallable, Category = "KCF|Session")
	void SetSessionService(TScriptInterface<IKCFSessionService> NewService);

	/** Name of the installed backend, for display and logging. */
	UFUNCTION(BlueprintPure, Category = "KCF|Session")
	FName GetServiceName() const;

	UFUNCTION(BlueprintPure, Category = "KCF|Session")
	bool IsServiceAvailable() const;

	// --- Operations --------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "KCF|Session")
	void CreateSession(const FKCFSessionSettings& Settings);

	UFUNCTION(BlueprintCallable, Category = "KCF|Session")
	void FindSessions(bool bLANOnly = true, int32 MaxResults = 32);

	UFUNCTION(BlueprintCallable, Category = "KCF|Session")
	void JoinSession(const FKCFSessionSearchResult& Result);

	UFUNCTION(BlueprintCallable, Category = "KCF|Session")
	void LeaveSession();

	UFUNCTION(BlueprintCallable, Category = "KCF|Session")
	void DestroySession();

	/** Called by the game mode when a match goes live, so the session stops advertising. */
	void NotifyMatchStarted();

	/** Called by the game mode when a match ends. */
	void NotifyMatchEnded();

	/** Results of the last search, so UI can rebuild a list without searching again. */
	UFUNCTION(BlueprintPure, Category = "KCF|Session")
	const TArray<FKCFSessionSearchResult>& GetLastSearchResults() const { return LastSearchResults; }

protected:
	/** The installed backend. Never null after Initialize. */
	UPROPERTY(Transient)
	TScriptInterface<IKCFSessionService> SessionService;

	UPROPERTY(Transient)
	TArray<FKCFSessionSearchResult> LastSearchResults;

	/** Creates and installs the built-in LAN service. */
	void InstallDefaultService();
};

/**
 * A working LAN / listen-server session backend built only on engine travel.
 *
 * Hosting opens the requested map as a listen server; joining issues a client travel to the stored
 * connect string. There is no discovery service behind it, so FindSessions returns whatever
 * addresses the project has registered through AddKnownServer - which is enough for a LAN playtest,
 * a dedicated server list baked into a build, or an automated test.
 *
 * It exists so that a developer who has not yet chosen an online subsystem can still play the game
 * across machines. It deliberately does not pretend to do matchmaking.
 */
UCLASS()
class KARTCOMBATFRAMEWORK_API UKCFNullSessionService : public UObject, public IKCFSessionService
{
	GENERATED_BODY()

public:
	//~ Begin IKCFSessionService
	virtual FName GetServiceName() const override { return TEXT("LAN"); }
	virtual bool IsAvailable() const override { return true; }
	virtual void CreateSession(const FKCFSessionSettings& Settings, FKCFSessionOperationDelegate OnComplete) override;
	virtual void FindSessions(bool bLANOnly, int32 MaxResults, FKCFSessionSearchDelegate OnComplete) override;
	virtual void JoinSession(const FKCFSessionSearchResult& Result, FKCFSessionOperationDelegate OnComplete) override;
	virtual void LeaveSession(FKCFSessionOperationDelegate OnComplete) override;
	virtual void DestroySession(FKCFSessionOperationDelegate OnComplete) override;
	virtual void StartMatch(FKCFSessionOperationDelegate OnComplete) override;
	virtual void EndMatch(FKCFSessionOperationDelegate OnComplete) override;
	//~ End IKCFSessionService

	/**
	 * Registers an address the LAN browser should offer, e.g. "192.168.1.20:7777".
	 * The replacement for real discovery, which an engine-only backend cannot provide.
	 */
	UFUNCTION(BlueprintCallable, Category = "KCF|Session")
	void AddKnownServer(const FString& Address, const FString& DisplayName);

	UFUNCTION(BlueprintCallable, Category = "KCF|Session")
	void ClearKnownServers();

	/** Map the game returns to when a session is left. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Session")
	FString MainMenuMap;

protected:
	UPROPERTY(Transient)
	TArray<FKCFSessionSearchResult> KnownServers;

	/** True once CreateSession has travelled to a listen server map. */
	bool bHostingSession = false;
};
