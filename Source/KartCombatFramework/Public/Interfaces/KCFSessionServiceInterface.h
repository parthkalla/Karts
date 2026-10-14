// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "KCFSessionServiceInterface.generated.h"

/** Settings used when hosting. Deliberately backend-agnostic. */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFSessionSettings
{
	GENERATED_BODY()

	/** Shown in server browsers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Session")
	FString SessionDisplayName;

	/** Map to travel to when the session is created. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Session")
	FString MapName;

	/** Id of the UKCFGameModeDataAsset the host selected. Advertised so browsers can filter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Session")
	FName GameModeId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Session", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxPlayers = 8;

	/** LAN-only session. Backends that do not distinguish may ignore this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Session")
	bool bIsLAN = true;

	/** Invite-only / not advertised publicly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Session")
	bool bIsPrivate = false;

	/** Free-form key/value pairs forwarded to the backend as session attributes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "KCF|Session")
	TMap<FString, FString> ExtraAttributes;
};

/** One discovered session. */
USTRUCT(BlueprintType)
struct KARTCOMBATFRAMEWORK_API FKCFSessionSearchResult
{
	GENERATED_BODY()

	/** Backend-specific opaque identifier. Pass it back verbatim to JoinSession. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Session")
	FString SessionId;

	UPROPERTY(BlueprintReadOnly, Category = "KCF|Session")
	FString DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "KCF|Session")
	FName GameModeId;

	UPROPERTY(BlueprintReadOnly, Category = "KCF|Session")
	int32 CurrentPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KCF|Session")
	int32 MaxPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "KCF|Session")
	int32 PingMs = 0;

	/** Travel URL. For the null service this is an address; for EOS/Steam it is backend-resolved. */
	UPROPERTY(BlueprintReadOnly, Category = "KCF|Session")
	FString ConnectString;

	UPROPERTY(BlueprintReadOnly, Category = "KCF|Session")
	TMap<FString, FString> ExtraAttributes;
};

/** Result of an asynchronous session operation. */
DECLARE_DELEGATE_TwoParams(FKCFSessionOperationDelegate, bool /*bSuccess*/, const FString& /*ErrorMessage*/);

/** Result of a session search. */
DECLARE_DELEGATE_ThreeParams(FKCFSessionSearchDelegate, bool /*bSuccess*/, const TArray<FKCFSessionSearchResult>& /*Results*/, const FString& /*ErrorMessage*/);

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UKCFSessionService : public UInterface
{
	GENERATED_BODY()
};

/**
 * Backend-agnostic session operations.
 *
 * No gameplay class in the framework references EOS, Steam, PlayFab or OnlineSubsystem. They all go
 * through UKCFSessionSubsystem, which owns one implementation of this interface. Adding a backend is
 * a new UObject class implementing these seven methods plus one line of configuration; no existing
 * file changes.
 *
 * Every method is asynchronous even when a particular backend could answer synchronously, because an
 * API that is synchronous for LAN and asynchronous for EOS forces every caller to handle both shapes.
 */
class KARTCOMBATFRAMEWORK_API IKCFSessionService
{
	GENERATED_BODY()

public:
	/** Human-readable backend name, for logs and the UI ("LAN", "EOS", "Steam"). */
	virtual FName GetServiceName() const = 0;

	/** True when the backend is initialised and able to accept calls. */
	virtual bool IsAvailable() const = 0;

	virtual void CreateSession(const FKCFSessionSettings& Settings, FKCFSessionOperationDelegate OnComplete) = 0;
	virtual void FindSessions(bool bLANOnly, int32 MaxResults, FKCFSessionSearchDelegate OnComplete) = 0;
	virtual void JoinSession(const FKCFSessionSearchResult& Result, FKCFSessionOperationDelegate OnComplete) = 0;
	virtual void LeaveSession(FKCFSessionOperationDelegate OnComplete) = 0;
	virtual void DestroySession(FKCFSessionOperationDelegate OnComplete) = 0;

	/** Marks the session in-progress so matchmaking stops advertising it. */
	virtual void StartMatch(FKCFSessionOperationDelegate OnComplete) = 0;

	/** Marks the session joinable again after a match concludes. */
	virtual void EndMatch(FKCFSessionOperationDelegate OnComplete) = 0;
};
