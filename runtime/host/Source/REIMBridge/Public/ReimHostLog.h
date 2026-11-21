/*
 * Host file log (M2-F3/H2).
 *
 * UE Shipping built against an installed engine compiles UE_LOG out
 * (USE_LOGGING_IN_SHIPPING=0), so the Host keeps its own log next to the
 * instance file. It records startup (instance/world/generated/NativeVersion/
 * DLL/ABI/profile/adapter), the DLL log callback, fatal causes, the exit
 * reason, and shutdown. Thread-safe: the Runtime's log callback arrives on
 * the simulation thread while the Game Thread writes too.
 */
#pragma once

#include "CoreMinimal.h"

namespace reim
{
	/** Opens (append) or reopens the Host log. Creates the directory. */
	REIMBRIDGE_API void HostLogOpen(const FString& Path);
	REIMBRIDGE_API void HostLogClose();

	/** Level is a REIM_LOG_* value (see reim_abi.h). */
	REIMBRIDGE_API void HostLogWrite(int32 Level, const FString& Category,
	                                 const FString& Message);
	REIMBRIDGE_API void HostLogEvent(const FString& Message);

	REIMBRIDGE_API FString HostLogPath();
}
