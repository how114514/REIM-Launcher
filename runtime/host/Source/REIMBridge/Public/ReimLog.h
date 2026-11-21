#pragma once

#include "CoreMinimal.h"
#include "Logging/LogCategory.h"
#include "Logging/LogMacros.h"

/* Inline category shared by every REIM Host module. Each DLL gets its own
 * category object with the same name, which avoids exporting a log-category
 * symbol across module boundaries. */
struct FLogCategoryLogREIM
	: public FLogCategory<ELogVerbosity::Log, ELogVerbosity::All>
{
	FORCEINLINE FLogCategoryLogREIM() : FLogCategory(TEXT("LogREIM")) {}
};

inline FLogCategoryLogREIM LogREIM;
