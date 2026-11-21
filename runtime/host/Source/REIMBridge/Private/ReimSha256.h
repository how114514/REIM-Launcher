/* Small self-contained SHA-256 for NativeVersion file/hash checks (M2-E6).
 * UE Core has SHA-1 only; keeping this local avoids new module dependencies. */
#pragma once

#include "CoreMinimal.h"

namespace ReimSha256
{
	/** Lowercase hex digest of a byte range. */
	FString HashBytes(const uint8* Data, SIZE_T Size);

	/** Lowercase hex digest of a file, or an empty string when unreadable. */
	FString HashFile(const FString& Path, int64& OutSize);
}
