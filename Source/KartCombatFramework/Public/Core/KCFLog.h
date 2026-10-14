// Original work, MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

/** General framework log. Verbose/VeryVerbose are compiled out of Shipping by the macros below. */
KARTCOMBATFRAMEWORK_API DECLARE_LOG_CATEGORY_EXTERN(LogKartCombatFramework, Log, All);

/** Network/replication specific log, separated so it can be enabled without the general firehose. */
KARTCOMBATFRAMEWORK_API DECLARE_LOG_CATEGORY_EXTERN(LogKCFNet, Log, All);

/** Combat (damage, weapons, projectiles) log. */
KARTCOMBATFRAMEWORK_API DECLARE_LOG_CATEGORY_EXTERN(LogKCFCombat, Log, All);

/** Match flow / game mode log. */
KARTCOMBATFRAMEWORK_API DECLARE_LOG_CATEGORY_EXTERN(LogKCFMatch, Log, All);

/**
 * Prefixes a log line with the network role of the object so that listen-server logs are readable.
 * Safe to call with a null pointer.
 */
KARTCOMBATFRAMEWORK_API FString KCF_NetContext(const UObject* WorldContext);

/**
 * Verbose logging that is compiled out entirely in Shipping and Test builds.
 * Use this for anything that could be hit more than a few times per second.
 */
#if NO_LOGGING
	#define KCF_VLOG(Category, Format, ...) do { } while (0)
#else
	#define KCF_VLOG(Category, Format, ...) \
		UE_LOG(Category, Verbose, TEXT("%s ") Format, *KCF_NetContext(this), ##__VA_ARGS__)
#endif

/** Standard framework log with network-role context. */
#if NO_LOGGING
	#define KCF_LOG(Category, Verbosity, Format, ...) do { } while (0)
#else
	#define KCF_LOG(Category, Verbosity, Format, ...) \
		UE_LOG(Category, Verbosity, TEXT("%s ") Format, *KCF_NetContext(this), ##__VA_ARGS__)
#endif

/**
 * Reports a configuration or authority mistake that the framework can recover from.
 * Deliberately not a check(): a missing optional Data Asset must never crash a shipping client.
 */
#define KCF_REPORT_RECOVERABLE(Category, Format, ...) \
	do \
	{ \
		UE_LOG(Category, Warning, TEXT("%s ") Format, *KCF_NetContext(this), ##__VA_ARGS__); \
	} while (0)
