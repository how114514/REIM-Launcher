#include "ReimHostLog.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"

#include "reim_abi.h"

namespace
{
	FCriticalSection GLogLock;
	TUniquePtr<FArchive> GLogWriter;
	FString GLogPath;

	const TCHAR* LevelName(int32 Level)
	{
		if (Level >= REIM_LOG_ERROR) return TEXT("ERROR");
		if (Level == REIM_LOG_WARN) return TEXT("WARN");
		if (Level <= REIM_LOG_DEBUG) return TEXT("DEBUG");
		return TEXT("INFO");
	}

	/* Caller holds GLogLock. */
	void WriteLocked(const FString& Category, const FString& Message)
	{
		if (!GLogWriter.IsValid()) return;
		const FString Line = FString::Printf(
			TEXT("[%s][%s] %s\r\n"), *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")),
			*Category, *Message);
		FTCHARToUTF8 Utf8(*Line);
		GLogWriter->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
		GLogWriter->Flush();
	}

	void CloseLocked()
	{
		if (GLogWriter.IsValid())
		{
			WriteLocked(TEXT("host"), TEXT("host log closed"));
			GLogWriter->Close();
			GLogWriter.Reset();
		}
	}
}

namespace reim
{
	void HostLogOpen(const FString& Path)
	{
		FScopeLock Lock(&GLogLock);
		CloseLocked();
		GLogPath = Path;
		const FString Directory = FPaths::GetPath(Path);
		if (!Directory.IsEmpty()) IFileManager::Get().MakeDirectory(*Directory, true);
		GLogWriter.Reset(IFileManager::Get().CreateFileWriter(
			*Path, static_cast<uint32>(EFileWrite::FILEWRITE_Append)));
		if (GLogWriter.IsValid())
		{
			WriteLocked(TEXT("host"), FString::Printf(TEXT("host log opened: %s"), *Path));
		}
	}

	void HostLogClose()
	{
		FScopeLock Lock(&GLogLock);
		CloseLocked();
	}

	void HostLogWrite(int32 Level, const FString& Category, const FString& Message)
	{
		FScopeLock Lock(&GLogLock);
		WriteLocked(Category,
		            FString::Printf(TEXT("%s %s"), LevelName(Level), *Message));
	}

	void HostLogEvent(const FString& Message)
	{
		HostLogWrite(REIM_LOG_INFO, TEXT("host"), Message);
	}

	FString HostLogPath()
	{
		FScopeLock Lock(&GLogLock);
		return GLogPath;
	}
}
