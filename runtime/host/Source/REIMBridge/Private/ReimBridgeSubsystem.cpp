#include "ReimBridgeSubsystem.h"

#include "ReimAdapterPin.h"
#include "ReimFramePacket.h"
#include "ReimHostLog.h"
#include "ReimLog.h"
#include "ReimSha256.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <cstddef>
#include <cstring>

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
	constexpr const char* kExpectedProfile = "26.3";
	constexpr uint32_t kRequiredCapabilities =
		REIM_CAP_FRAME_BLOB | REIM_CAP_INTERPOLATION | REIM_CAP_ATLAS_ASSET | REIM_CAP_R0;

	struct FReimInstanceFileConfig
	{
		FString InstancePath;
		FString InstanceDir;
		FString WorldPath;
		FString GeneratedPath;
		FString NativeVersionPath;
		FString MinecraftDir;
		FString DllPath;
		FString LogDir;
		uint32 RenderDistance = 6;
		uint32 RenderLevel = 0;
		float MouseSensitivity = 0.15f;
		bool bInvertY = false;
		bool bAutoStart = true;
		bool bSpawnOverride = false;
		float SpawnX = 0.0f;
		float SpawnY = 0.0f;
		float SpawnZ = 0.0f;
		float SpawnYaw = 0.0f;
		float SpawnPitch = 0.0f;
	};

	void* REIM_CALL HostAlloc(size_t Size, size_t Alignment)
	{
		if (Alignment < alignof(std::max_align_t)) Alignment = alignof(std::max_align_t);
		return FMemory::Malloc(Size, static_cast<uint32>(Alignment));
	}

	void REIM_CALL HostFree(void* Ptr)
	{
		FMemory::Free(Ptr);
	}

	void REIM_CALL HostLog(int32 Level, const char* Category, const char* Message)
	{
		const FString Cat = UTF8_TO_TCHAR(Category ? Category : "runtime");
		const FString Text = UTF8_TO_TCHAR(Message ? Message : "");
		if (Level >= REIM_LOG_ERROR)
		{
			UE_LOG(LogREIM, Error, TEXT("[%s] %s"), *Cat, *Text);
		}
		else if (Level == REIM_LOG_WARN)
		{
			UE_LOG(LogREIM, Warning, TEXT("[%s] %s"), *Cat, *Text);
		}
		else if (Level <= REIM_LOG_DEBUG)
		{
			UE_LOG(LogREIM, Verbose, TEXT("[%s] %s"), *Cat, *Text);
		}
		else
		{
			UE_LOG(LogREIM, Display, TEXT("[%s] %s"), *Cat, *Text);
		}
		/* Shipping has no engine log; the Host file log is the durable one. */
		reim::HostLogWrite(Level, Cat, Text);
	}

	FString ResolvePath(const FString& InPath, const FString& BaseDir)
	{
		if (InPath.IsEmpty()) return FString();
		FString Path = InPath;
		if (FPaths::IsRelative(Path))
		{
			Path = FPaths::Combine(BaseDir, Path);
		}
		return FPaths::ConvertRelativePathToFull(Path);
	}

	bool LoadJsonFile(const FString& Path, TSharedPtr<FJsonObject>& OutObject, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("cannot read %s"), *Path);
			return false;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, OutObject) || !OutObject.IsValid())
		{
			OutError = FString::Printf(TEXT("invalid JSON in %s"), *Path);
			return false;
		}
		return true;
	}

	bool LoadInstanceConfig(const FString& InstancePath, FReimInstanceFileConfig& Out,
	                        FString& OutError)
	{
		TSharedPtr<FJsonObject> Root;
		if (!LoadJsonFile(InstancePath, Root, OutError)) return false;

		Out = FReimInstanceFileConfig{};
		Out.InstancePath = InstancePath;
		Out.InstanceDir = FPaths::GetPath(FPaths::ConvertRelativePathToFull(InstancePath));

		Root->TryGetStringField(TEXT("world_path"), Out.WorldPath);
		Root->TryGetStringField(TEXT("generated_path"), Out.GeneratedPath);
		Root->TryGetStringField(TEXT("native_version_path"), Out.NativeVersionPath);
		Root->TryGetStringField(TEXT("minecraft_dir"), Out.MinecraftDir);
		Root->TryGetStringField(TEXT("dll_path"), Out.DllPath);
		Root->TryGetStringField(TEXT("log_dir"), Out.LogDir);

		double Number = 0.0;
		if (Root->TryGetNumberField(TEXT("render_distance"), Number))
		{
			Out.RenderDistance = static_cast<uint32>(FMath::Max(1.0, Number));
		}
		if (Root->TryGetNumberField(TEXT("render_level"), Number))
		{
			Out.RenderLevel = static_cast<uint32>(Number);
		}
		if (Root->TryGetNumberField(TEXT("mouse_sensitivity"), Number))
		{
			Out.MouseSensitivity = static_cast<float>(Number);
		}
		Root->TryGetBoolField(TEXT("invert_y"), Out.bInvertY);
		Root->TryGetBoolField(TEXT("auto_start"), Out.bAutoStart);

		/* Diagnostic spawn override (screenshots/automation). */
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		if (Root->TryGetNumberField(TEXT("spawn_x"), X) &&
		    Root->TryGetNumberField(TEXT("spawn_y"), Y) &&
		    Root->TryGetNumberField(TEXT("spawn_z"), Z))
		{
			Out.bSpawnOverride = true;
			Out.SpawnX = static_cast<float>(X);
			Out.SpawnY = static_cast<float>(Y);
			Out.SpawnZ = static_cast<float>(Z);
			if (Root->TryGetNumberField(TEXT("spawn_yaw"), Number))
			{
				Out.SpawnYaw = static_cast<float>(Number);
			}
			if (Root->TryGetNumberField(TEXT("spawn_pitch"), Number))
			{
				Out.SpawnPitch = static_cast<float>(Number);
			}
		}

		Out.WorldPath = ResolvePath(Out.WorldPath, Out.InstanceDir);
		Out.GeneratedPath = ResolvePath(Out.GeneratedPath, Out.InstanceDir);
		Out.NativeVersionPath = ResolvePath(Out.NativeVersionPath, Out.InstanceDir);
		Out.DllPath = ResolvePath(Out.DllPath, Out.InstanceDir);
		Out.LogDir = ResolvePath(Out.LogDir, Out.InstanceDir);

		if (Out.WorldPath.IsEmpty() || Out.GeneratedPath.IsEmpty())
		{
			OutError = TEXT("instance.json requires world_path and generated_path");
			return false;
		}
		return true;
	}

	FString EngineVersionString()
	{
		const FEngineVersion& Version = FEngineVersion::Current();
		return FString::Printf(TEXT("%u.%u.%u"), Version.GetMajor(), Version.GetMinor(),
		                       Version.GetPatch());
	}

	bool RequireString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field,
	                   FString& OutValue, FString& OutError)
	{
		if (!Object->TryGetStringField(Field, OutValue) || OutValue.IsEmpty())
		{
			OutError = FString::Printf(TEXT("missing '%s'"), Field);
			return false;
		}
		return true;
	}

	bool RequireObject(const TSharedPtr<FJsonObject>& Root, const TCHAR* Field,
	                   TSharedPtr<FJsonObject>& Out, FString& OutError)
	{
		const TSharedPtr<FJsonObject>* Found = nullptr;
		if (!Root->TryGetObjectField(Field, Found) || Found == nullptr || !Found->IsValid())
		{
			OutError = FString::Printf(TEXT("missing '%s' object"), Field);
			return false;
		}
		Out = *Found;
		return true;
	}

	uint64 HexPrefixToU64(const FString& Hex)
	{
		uint64 Value = 0;
		for (int32 i = 0; i < 16 && i < Hex.Len(); ++i)
		{
			const TCHAR C = Hex[i];
			int32 Digit = 0;
			if (C >= '0' && C <= '9') Digit = C - '0';
			else if (C >= 'a' && C <= 'f') Digit = C - 'a' + 10;
			else if (C >= 'A' && C <= 'F') Digit = C - 'A' + 10;
			else return 0;
			Value = (Value << 4) | static_cast<uint64>(Digit);
		}
		return Value;
	}

	struct FReimNativeVersionDesc
	{
		FString DllPath;
		FString ManifestSha256;
		FString AdapterSha256;
		FString AdapterId;
		FString AdapterVersion;
	};

	/* M2-E6/E9: a UE instance only starts from a Converter-produced
	 * NativeVersion. Every descriptor and every bound hash is checked here;
	 * a manually copied DLL or edited descriptor fails before load. */
	bool ValidateNativeVersion(const FReimInstanceFileConfig& Config,
	                           FReimNativeVersionDesc& Out, FString& OutError)
	{
		if (Config.NativeVersionPath.IsEmpty())
		{
			OutError = TEXT("native_version_path is required; UE runs only from a "
			                "Converter-produced NativeVersion (M2-E9)");
			return false;
		}

		const FString NvPath =
			FPaths::Combine(Config.NativeVersionPath, FString(TEXT("native_version.json")));
		if (!IFileManager::Get().FileExists(*NvPath))
		{
			OutError = FString::Printf(TEXT("native_version.json not found in %s"),
			                           *Config.NativeVersionPath);
			return false;
		}
		TSharedPtr<FJsonObject> Nv;
		if (!LoadJsonFile(NvPath, Nv, OutError)) return false;

		FString Schema;
		FString Profile;
		int32 Format = 0;
		int32 Abi = 0;
		Nv->TryGetStringField(TEXT("schema"), Schema);
		Nv->TryGetNumberField(TEXT("format"), Format);
		Nv->TryGetNumberField(TEXT("abi_version"), Abi);
		Nv->TryGetStringField(TEXT("profile"), Profile);
		if (Schema != TEXT("reim.native_version") || Format != 2 ||
		    Abi != static_cast<int32>(REIM_ABI_VERSION) || Profile != kExpectedProfile)
		{
			OutError = FString::Printf(
				TEXT("native_version.json rejected (schema=%s format=%d abi=%d profile=%s)"),
				*Schema, Format, Abi, *Profile);
			return false;
		}

		TSharedPtr<FJsonObject> Dll;
		if (!RequireObject(Nv, TEXT("dll"), Dll, OutError)) return false;
		FString DllFile;
		FString DllSha;
		if (!RequireString(Dll, TEXT("file"), DllFile, OutError) ||
		    !RequireString(Dll, TEXT("sha256"), DllSha, OutError))
		{
			return false;
		}
		if (DllFile != TEXT("mc_26.3.dll"))
		{
			OutError = FString::Printf(TEXT("native_version.json dll.file '%s' != mc_26.3.dll"),
			                           *DllFile);
			return false;
		}
		Out.DllPath = FPaths::Combine(Config.NativeVersionPath, DllFile);
		if (!IFileManager::Get().FileExists(*Out.DllPath))
		{
			OutError = FString::Printf(TEXT("NativeVersion DLL missing: %s"), *Out.DllPath);
			return false;
		}
		int64 DllSize = 0;
		const FString ActualDllSha = ReimSha256::HashFile(Out.DllPath, DllSize);
		if (ActualDllSha.IsEmpty() || !ActualDllSha.Equals(DllSha, ESearchCase::IgnoreCase))
		{
			OutError = FString::Printf(
				TEXT("DLL hash mismatch: native_version.json=%s actual=%s (tampered DLL?)"),
				*DllSha, *ActualDllSha);
			return false;
		}

		TSharedPtr<FJsonObject> Generated;
		if (!RequireObject(Nv, TEXT("generated"), Generated, OutError) ||
		    !RequireString(Generated, TEXT("manifest_sha256"), Out.ManifestSha256, OutError))
		{
			return false;
		}
		const FString ManifestPath =
			FPaths::Combine(Config.GeneratedPath, FString(TEXT("manifest.json")));
		int64 ManifestSize = 0;
		const FString ActualManifestSha = ReimSha256::HashFile(ManifestPath, ManifestSize);
		if (ActualManifestSha.IsEmpty() ||
		    !ActualManifestSha.Equals(Out.ManifestSha256, ESearchCase::IgnoreCase))
		{
			OutError = FString::Printf(
				TEXT("generated manifest hash mismatch: native_version.json=%s actual=%s"),
				*Out.ManifestSha256, ActualManifestSha.IsEmpty() ? TEXT("<missing>")
				                                                : *ActualManifestSha);
			return false;
		}

		TSharedPtr<FJsonObject> AdapterRef;
		if (!RequireObject(Nv, TEXT("adapter"), AdapterRef, OutError) ||
		    !RequireString(AdapterRef, TEXT("file"), DllFile, OutError) ||
		    !RequireString(AdapterRef, TEXT("sha256"), Out.AdapterSha256, OutError) ||
		    !RequireString(AdapterRef, TEXT("id"), Out.AdapterId, OutError) ||
		    !RequireString(AdapterRef, TEXT("version"), Out.AdapterVersion, OutError))
		{
			return false;
		}
		const FString AdapterPath = FPaths::Combine(Config.NativeVersionPath, DllFile);
		int64 AdapterSize = 0;
		const FString ActualAdapterSha = ReimSha256::HashFile(AdapterPath, AdapterSize);
		if (ActualAdapterSha.IsEmpty() ||
		    !ActualAdapterSha.Equals(Out.AdapterSha256, ESearchCase::IgnoreCase))
		{
			OutError = FString::Printf(
				TEXT("ue_adapter.json hash mismatch: native_version.json=%s actual=%s"),
				*Out.AdapterSha256, ActualAdapterSha.IsEmpty() ? TEXT("<missing>")
				                                              : *ActualAdapterSha);
			return false;
		}

		TSharedPtr<FJsonObject> Adapter;
		if (!LoadJsonFile(AdapterPath, Adapter, OutError)) return false;
		FString AdapterSchema;
		FString AdapterId;
		FString AdapterVersion;
		FString UeVersion;
		FString AssetRoot;
		int32 AdapterFormat = 0;
		int32 AdapterAbi = 0;
		int32 RenderLevel = -1;
		Adapter->TryGetStringField(TEXT("schema"), AdapterSchema);
		Adapter->TryGetNumberField(TEXT("format"), AdapterFormat);
		Adapter->TryGetNumberField(TEXT("abi_version"), AdapterAbi);
		Adapter->TryGetNumberField(TEXT("render_level"), RenderLevel);
		Adapter->TryGetStringField(TEXT("adapter_id"), AdapterId);
		Adapter->TryGetStringField(TEXT("adapter_version"), AdapterVersion);
		Adapter->TryGetStringField(TEXT("ue_version"), UeVersion);
		Adapter->TryGetStringField(TEXT("asset_root_sha256"), AssetRoot);

		const FString EngineVersion = EngineVersionString();
		if (AdapterSchema != TEXT("reim.ue_adapter") || AdapterFormat != 2 ||
		    AdapterAbi != static_cast<int32>(REIM_ADAPTER_ABI_VERSION) ||
		    RenderLevel != static_cast<int32>(REIM_ADAPTER_RENDER_LEVEL))
		{
			OutError = FString::Printf(
				TEXT("ue_adapter.json rejected (schema=%s format=%d abi=%d render=%d)"),
				*AdapterSchema, AdapterFormat, AdapterAbi, RenderLevel);
			return false;
		}
		if (AdapterId != TEXT(REIM_ADAPTER_ID) ||
		    AdapterVersion != TEXT(REIM_ADAPTER_VERSION))
		{
			OutError = FString::Printf(
				TEXT("adapter identity mismatch: descriptor %s/%s, this build %s/%s"),
				*AdapterId, *AdapterVersion, TEXT(REIM_ADAPTER_ID),
				TEXT(REIM_ADAPTER_VERSION));
			return false;
		}
		if (UeVersion != TEXT(REIM_ADAPTER_UE_VERSION) || EngineVersion != UeVersion)
		{
			OutError = FString::Printf(
				TEXT("UE version mismatch: descriptor %s, engine %s, adapter pin %s"),
				*UeVersion, *EngineVersion, TEXT(REIM_ADAPTER_UE_VERSION));
			return false;
		}
		if (AssetRoot.IsEmpty() || Out.AdapterId != AdapterId ||
		    Out.AdapterVersion != AdapterVersion)
		{
			OutError = TEXT("adapter descriptor and native_version.json disagree");
			return false;
		}

		UE_LOG(LogREIM, Display,
		       TEXT("NativeVersion validated: dll %s (%lld bytes), manifest %s, adapter "
		            "%s %s (UE %s, assets %s)"),
		       *DllSha, DllSize, *Out.ManifestSha256.Left(16), *AdapterId,
		       *AdapterVersion, *EngineVersion, *AssetRoot.Left(16));
		return true;
	}
}

/* Kept outside the anonymous namespace so the header can hold it by pointer. */
class FReimBridgeState
{
public:
	FReimInstanceFileConfig Config;
	FReimNativeVersionDesc Descriptor;
	void* Dll = nullptr;
	const ReimRuntimeApi* Api = nullptr;
	ReimInstance* Instance = nullptr;
	TSharedPtr<FReimRuntimeHandle, ESPMode::ThreadSafe> Handle;
	TSharedPtr<FReimFramePacket, ESPMode::ThreadSafe> Latest;
	ReimRuntimeInfo Info{};
	bool bPaused = false;
	FString StatusJson;

	bool bRunning = false;
	bool bFatal = false;
	FString LastError;

	TArray<uint8> AtlasPng;
	FString AtlasMeta;
	uint64 AtlasRevision = 0;

	double GtFetchMs = 0.0;
	uint64 BusyFrames = 0;
	uint64 FetchedFrames = 0;

	void Shutdown()
	{
		reim::HostLogEvent(TEXT("shutdown: begin (pause -> stop -> destroy)"));
		/* M2-D2 close order: stop input/pause, stop simulation, drain RT, then
		 * destroy. Packet destructors stop releasing once Alive goes false, so
		 * destroy() frees any remaining blobs after the render commands drain. */
		if (Api != nullptr && Instance != nullptr)
		{
			if (bRunning) Api->set_simulation_paused(Instance, 1u);
		}
		if (Handle.IsValid())
		{
			Handle->Alive.store(false);
		}
		Latest.Reset();
		/* Renderer commands may still hold packets; finish them before the
		 * Runtime frees outstanding blobs or the DLL unloads. */
		FlushRenderingCommands();

		if (Api != nullptr && Instance != nullptr)
		{
			if (bRunning) Api->stop(Instance);
			Api->destroy(Instance);
		}
		Instance = nullptr;
		bRunning = false;
		Api = nullptr;

		FlushRenderingCommands();
		Handle.Reset();

#if PLATFORM_WINDOWS
		if (Dll != nullptr)
		{
			::FreeLibrary(static_cast<HMODULE>(Dll));
			Dll = nullptr;
		}
#endif
		reim::HostLogEvent(TEXT("shutdown: complete"));
	}

	void Fail(const FString& Message)
	{
		bFatal = true;
		LastError = Message;
		UE_LOG(LogREIM, Error, TEXT("%s"), *Message);
		reim::HostLogWrite(REIM_LOG_ERROR, TEXT("host"), Message);
	}
};

UReimBridgeSubsystem::~UReimBridgeSubsystem()
{
	if (State != nullptr)
	{
		State->Shutdown();
		delete State;
		State = nullptr;
	}
}

void UReimBridgeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	State = new FReimBridgeState();

	FString InstancePath;
	if (!FParse::Value(FCommandLine::Get(), TEXT("reim-instance="), InstancePath) ||
	    InstancePath.IsEmpty())
	{
		reim::HostLogOpen(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"),
		                                  TEXT("REIM-host.log")));
		reim::HostLogEvent(TEXT("session start without -reim-instance"));
#if UE_BUILD_SHIPPING
		State->Fail(TEXT("no -reim-instance=<instance.json>; Shipping requires an "
		                 "instance config (M2-D5)"));
#else
		UE_LOG(LogREIM, Warning,
		       TEXT("no -reim-instance=<instance.json>; Runtime stays unloaded "
		            "(R0 automation/tests pass one explicitly)."));
#endif
		return;
	}
	InstancePath = FPaths::ConvertRelativePathToFull(InstancePath);

	FString Error;
	if (!LoadInstanceConfig(InstancePath, State->Config, Error))
	{
		/* Even an unreadable instance must leave a locatable reason. */
		reim::HostLogOpen(FPaths::Combine(FPaths::GetPath(InstancePath), TEXT("logs"),
		                                  TEXT("REIM-host.log")));
		reim::HostLogEvent(FString::Printf(TEXT("session start (invalid instance): %s"),
		                                   *InstancePath));
		State->Fail(FString::Printf(TEXT("instance config: %s"), *Error));
		return;
	}
	const FString LogDir = State->Config.LogDir.IsEmpty()
		? FPaths::Combine(State->Config.InstanceDir, TEXT("logs"))
		: State->Config.LogDir;
	reim::HostLogOpen(FPaths::Combine(LogDir, TEXT("REIM-host.log")));
	reim::HostLogEvent(FString::Printf(TEXT("session start: %s"), *InstancePath));
	reim::HostLogEvent(FString::Printf(
		TEXT("world=%s generated=%s native_version=%s"),
		*State->Config.WorldPath, *State->Config.GeneratedPath,
		*State->Config.NativeVersionPath));
	UE_LOG(LogREIM, Display,
	       TEXT("instance: %s world=%s generated=%s native_version=%s"),
	       *InstancePath, *State->Config.WorldPath, *State->Config.GeneratedPath,
	       *State->Config.NativeVersionPath);
	if (!State->Config.DllPath.IsEmpty())
	{
		UE_LOG(LogREIM, Warning,
		       TEXT("ignoring instance dll_path (%s): the DLL is resolved from the "
		            "validated NativeVersion only"),
		       *State->Config.DllPath);
	}

	if (!ValidateNativeVersion(State->Config, State->Descriptor, Error))
	{
		State->Fail(FString::Printf(TEXT("NativeVersion/Adapter rejected: %s"), *Error));
		return;
	}
	const FString DllPath = State->Descriptor.DllPath;
	reim::HostLogEvent(FString::Printf(
		TEXT("NativeVersion validated: dll=%s adapter=%s %s"),
		*DllPath, *State->Descriptor.AdapterId, *State->Descriptor.AdapterVersion));

#if PLATFORM_WINDOWS
	State->Dll = ::LoadLibraryExW(*DllPath, nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (State->Dll == nullptr)
	{
		State->Fail(FString::Printf(TEXT("LoadLibraryExW failed for %s (error %lu)"),
		                            *DllPath, ::GetLastError()));
		return;
	}
	using FGetApiFn = const ReimRuntimeApi*(REIM_CALL*)(uint32_t);
	const FGetApiFn GetApi =
		reinterpret_cast<FGetApiFn>(::GetProcAddress(static_cast<HMODULE>(State->Dll),
		                                              "reim_get_api"));
	if (GetApi == nullptr)
	{
		State->Fail(TEXT("GetProcAddress(reim_get_api) failed"));
		return;
	}
	State->Api = GetApi(REIM_ABI_VERSION);
	if (State->Api == nullptr || State->Api->abi_version != REIM_ABI_VERSION ||
	    State->Api->struct_size < sizeof(ReimRuntimeApi) ||
	    State->Api->release_frame == nullptr || State->Api->get_runtime_info == nullptr ||
	    State->Api->get_asset == nullptr)
	{
		State->Fail(FString::Printf(TEXT("DLL rejected ABI %u or has a v1 API table"),
		                            REIM_ABI_VERSION));
		return;
	}
	UE_LOG(LogREIM, Display, TEXT("loaded %s (ABI %u, api %u bytes)"), *DllPath,
	       State->Api->abi_version, State->Api->struct_size);
	reim::HostLogEvent(FString::Printf(TEXT("DLL loaded: %s (ABI %u)"), *DllPath,
	                                   State->Api->abi_version));
#else
	State->Fail(TEXT("REIM Runtime is Windows-only"));
	return;
#endif

	ReimHostApi Host{};
	Host.struct_size = sizeof(Host);
	Host.abi_version = REIM_ABI_VERSION;
	Host.alloc = &HostAlloc;
	Host.free = &HostFree;
	Host.log = &HostLog;

	const FString InstanceDir = State->Config.InstanceDir;
	ReimInstanceConfig Config{};
	Config.struct_size = sizeof(Config);
	Config.abi_version = REIM_ABI_VERSION;
	Config.world_path = *State->Config.WorldPath;
	Config.generated_path = *State->Config.GeneratedPath;
	Config.minecraft_dir = State->Config.MinecraftDir.IsEmpty()
		? nullptr : *State->Config.MinecraftDir;
	Config.render_distance = State->Config.RenderDistance;
	Config.mouse_sensitivity = State->Config.MouseSensitivity;
	Config.invert_y = State->Config.bInvertY ? 1u : 0u;
	Config.render_level = State->Config.RenderLevel;
	Config.instance_dir = *InstanceDir;
	Config.native_version_path = State->Config.NativeVersionPath.IsEmpty()
		? nullptr : *State->Config.NativeVersionPath;
	if (State->Config.bSpawnOverride)
	{
		Config.flags |= REIM_CONFIG_FLAG_SPAWN_OVERRIDE;
		Config.spawn_x = State->Config.SpawnX;
		Config.spawn_y = State->Config.SpawnY;
		Config.spawn_z = State->Config.SpawnZ;
		Config.spawn_yaw = State->Config.SpawnYaw;
		Config.spawn_pitch = State->Config.SpawnPitch;
	}

	int32 CreateError = REIM_OK;
	State->Instance = State->Api->create(&Config, &Host, &CreateError);
	if (State->Instance == nullptr)
	{
		State->Fail(FString::Printf(TEXT("Runtime create failed (%d)"), CreateError));
		return;
	}
	State->Handle = MakeShared<FReimRuntimeHandle, ESPMode::ThreadSafe>();
	State->Handle->Api = State->Api;
	State->Handle->Instance = State->Instance;
	State->Handle->Alive.store(true);

	State->Info = ReimRuntimeInfo{};
	State->Info.struct_size = sizeof(State->Info);
	if (State->Api->get_runtime_info(State->Instance, &State->Info) != REIM_OK)
	{
		State->Fail(TEXT("get_runtime_info failed"));
		return;
	}
	const FString Profile(ANSI_TO_TCHAR(State->Info.profile));
	if (State->Info.abi_version != REIM_ABI_VERSION ||
	    State->Info.dll_abi_version != REIM_ABI_VERSION ||
	    Profile != kExpectedProfile || State->Info.render_level != 0 ||
	    (State->Info.capabilities & kRequiredCapabilities) != kRequiredCapabilities)
	{
		State->Fail(FString::Printf(
			TEXT("runtime info mismatch (abi %u/%u profile %s render %u caps 0x%x)"),
			State->Info.abi_version, State->Info.dll_abi_version, *Profile,
			State->Info.render_level, State->Info.capabilities));
		return;
	}
	const uint64 ExpectedManifestHash = HexPrefixToU64(State->Descriptor.ManifestSha256);
	if (ExpectedManifestHash == 0 || ExpectedManifestHash != State->Info.manifest_hash)
	{
		State->Fail(FString::Printf(
			TEXT("DLL accepted a different generated manifest (runtime %llu, "
			     "NativeVersion %llu)"),
			State->Info.manifest_hash, ExpectedManifestHash));
		return;
	}
	UE_LOG(LogREIM, Display,
	       TEXT("runtime info: profile=%s build=%s manifest_hash=%llu caps=0x%x"),
	       *Profile, ANSI_TO_TCHAR(State->Info.build_id), State->Info.manifest_hash,
	       State->Info.capabilities);
	reim::HostLogEvent(FString::Printf(
		TEXT("runtime info: profile=%s build=%s manifest_hash=%llu caps=0x%x"),
		*Profile, ANSI_TO_TCHAR(State->Info.build_id), State->Info.manifest_hash,
		State->Info.capabilities));

	const void* AssetData = nullptr;
	uint64 AssetSize = 0;
	if (State->Api->get_asset(State->Instance, REIM_ASSET_ATLAS_PNG, &AssetData,
	                          &AssetSize, &State->AtlasRevision) == REIM_OK &&
	    AssetData != nullptr && AssetSize != 0)
	{
		State->AtlasPng.SetNumUninitialized(static_cast<int32>(AssetSize));
		FMemory::Memcpy(State->AtlasPng.GetData(), AssetData,
		                static_cast<SIZE_T>(AssetSize));
	}
	const void* MetaData = nullptr;
	uint64 MetaSize = 0;
	uint64 MetaRevision = 0;
	if (State->Api->get_asset(State->Instance, REIM_ASSET_ATLAS_META, &MetaData,
	                          &MetaSize, &MetaRevision) == REIM_OK &&
	    MetaData != nullptr)
	{
		State->AtlasMeta = FString(
			static_cast<int32>(MetaSize),
			static_cast<const ANSICHAR*>(MetaData));
	}
	if (State->AtlasPng.Num() == 0)
	{
		State->Fail(TEXT("get_asset(ATLAS_PNG) failed; R0 cannot render"));
		return;
	}
	UE_LOG(LogREIM, Display, TEXT("atlas asset: %d bytes revision %llu"),
	       State->AtlasPng.Num(), State->AtlasRevision);

	if (!State->Config.bAutoStart)
	{
		UE_LOG(LogREIM, Display, TEXT("auto_start=false; instance created but not started"));
		return;
	}

	const int32 StartResult = State->Api->start(State->Instance);
	if (StartResult != REIM_OK)
	{
		State->Fail(FString::Printf(TEXT("Runtime start failed (%d)"), StartResult));
		return;
	}
	State->bRunning = true;
	UE_LOG(LogREIM, Display, TEXT("Runtime started (20 TPS on the DLL thread)"));
	reim::HostLogEvent(TEXT("Runtime started (20 TPS on the DLL thread)"));
}

void UReimBridgeSubsystem::Deinitialize()
{
	if (State != nullptr)
	{
		State->Shutdown();
		delete State;
		State = nullptr;
	}
	reim::HostLogClose();
	Super::Deinitialize();
}

void UReimBridgeSubsystem::Tick(float /*DeltaTime*/)
{
	if (State == nullptr || !State->bRunning || State->bFatal) return;

	if (State->FetchedFrames == 0)
	{
		UE_LOG(LogREIM, Display, TEXT("Bridge first fetch"));
	}
	ReimRenderFrame Frame{};
	Frame.struct_size = sizeof(Frame);
	Frame.abi_version = REIM_ABI_VERSION;

	const double Start = FPlatformTime::Seconds();
	const int32 Result = State->Api->fetch_frame(State->Instance, &Frame);
	State->GtFetchMs = (FPlatformTime::Seconds() - Start) * 1000.0;

	if (Result == REIM_ERR_BUSY)
	{
		++State->BusyFrames;
		/* Host reuses the latest GPU state; the Runtime keeps the pending
		 * events and is not blocked. Log the first occurrence only. */
		if (State->BusyFrames == 1)
		{
			UE_LOG(LogREIM, Warning,
			       TEXT("fetch_frame BUSY: 4 frame blobs outstanding; reusing last frame"));
		}
		return;
	}
	if (Result != REIM_OK)
	{
		State->Fail(FString::Printf(TEXT("fetch_frame failed (%d)"), Result));
		return;
	}

	TSharedPtr<FReimFramePacket, ESPMode::ThreadSafe> Packet =
		MakeShared<FReimFramePacket, ESPMode::ThreadSafe>();
	Packet->Frame = Frame;
	Packet->FrameId = Frame.frame_id;
	Packet->Blob = Frame.owned_block;
	Packet->Handle = State->Handle;
	State->Latest = Packet;
	++State->FetchedFrames;
	State->Handle->Fetched.fetch_add(1);
	/* M2-G3 measures fetch + the GT-side packet handoff, not only the call. */
	State->GtFetchMs = (FPlatformTime::Seconds() - Start) * 1000.0;
}

TStatId UReimBridgeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UReimBridgeSubsystem, STATGROUP_Tickables);
}

bool UReimBridgeSubsystem::IsTickable() const
{
	return State != nullptr && State->bRunning && !State->bFatal;
}

ETickableTickType UReimBridgeSubsystem::GetTickableTickType() const
{
	return ETickableTickType::Conditional;
}

TSharedPtr<FReimFramePacket, ESPMode::ThreadSafe> UReimBridgeSubsystem::GetLatestFrame() const
{
	return State != nullptr ? State->Latest : nullptr;
}

bool UReimBridgeSubsystem::IsRunning() const
{
	return State != nullptr && State->bRunning;
}

bool UReimBridgeSubsystem::IsFatal() const
{
	return State != nullptr && State->bFatal;
}

FString UReimBridgeSubsystem::GetLastError() const
{
	return State != nullptr ? State->LastError : FString();
}

FString UReimBridgeSubsystem::GetStatusSummary() const
{
	if (State == nullptr) return TEXT("bridge: uninitialized");
	if (State->bFatal) return FString::Printf(TEXT("bridge: FATAL %s"), *State->LastError);
	if (!State->bRunning) return TEXT("bridge: loaded, not started");
	return FString::Printf(
		TEXT("bridge: profile=%s frames=%llu busy=%llu fetch=%.2fms"),
		ANSI_TO_TCHAR(State->Info.profile), State->FetchedFrames, State->BusyFrames,
		State->GtFetchMs);
}

const TArray<uint8>& UReimBridgeSubsystem::GetAtlasPng() const
{
	static const TArray<uint8> Empty;
	return State != nullptr ? State->AtlasPng : Empty;
}

FString UReimBridgeSubsystem::GetAtlasMeta() const
{
	return State != nullptr ? State->AtlasMeta : FString();
}

uint64 UReimBridgeSubsystem::GetAtlasRevision() const
{
	return State != nullptr ? State->AtlasRevision : 0;
}

double UReimBridgeSubsystem::GetGtFetchMs() const
{
	return State != nullptr ? State->GtFetchMs : 0.0;
}

uint64 UReimBridgeSubsystem::GetBusyFrameCount() const
{
	return State != nullptr ? State->BusyFrames : 0;
}

uint64 UReimBridgeSubsystem::GetFetchedFrameCount() const
{
	return State != nullptr ? State->FetchedFrames : 0;
}

void UReimBridgeSubsystem::LogEvent(const FString& Message) const
{
	reim::HostLogEvent(Message);
}

FString UReimBridgeSubsystem::GetHostLogPath() const
{
	return reim::HostLogPath();
}

uint64 UReimBridgeSubsystem::GetOutstandingFrameCount() const
{
	if (State == nullptr || !State->Handle.IsValid()) return 0;
	const uint64 Fetched = State->Handle->Fetched.load();
	const uint64 Released = State->Handle->Released.load();
	return Fetched > Released ? Fetched - Released : 0;
}

bool UReimBridgeSubsystem::SubmitInput(const ReimInputEvent& Event)
{
	if (State == nullptr || State->Api == nullptr || State->Instance == nullptr ||
	    !State->bRunning || State->bFatal)
	{
		return false;
	}
	State->Api->submit_input(State->Instance, &Event);
	return true;
}

void UReimBridgeSubsystem::SubmitKey(int32 Key, bool bPressed, bool bRepeat)
{
	ReimInputEvent Event{};
	Event.struct_size = sizeof(Event);
	Event.type = REIM_INPUT_KEY;
	Event.key = Key;
	Event.action = bPressed ? (bRepeat ? REIM_ACTION_REPEAT : REIM_ACTION_PRESS)
	                        : REIM_ACTION_RELEASE;
	SubmitInput(Event);
}

void UReimBridgeSubsystem::SubmitMouseMotion(float Dx, float Dy)
{
	ReimInputEvent Event{};
	Event.struct_size = sizeof(Event);
	Event.type = REIM_INPUT_MOUSE_MOTION;
	Event.dx = Dx;
	Event.dy = Dy;
	SubmitInput(Event);
}

void UReimBridgeSubsystem::SubmitMouseButton(int32 Button, bool bPressed)
{
	ReimInputEvent Event{};
	Event.struct_size = sizeof(Event);
	Event.type = REIM_INPUT_MOUSE_BUTTON;
	Event.key = Button;
	Event.action = bPressed ? REIM_ACTION_PRESS : REIM_ACTION_RELEASE;
	SubmitInput(Event);
}

void UReimBridgeSubsystem::SetSimulationPaused(bool bInPaused)
{
	if (State == nullptr || State->Api == nullptr || State->Instance == nullptr) return;
	State->bPaused = bInPaused;
	State->Api->set_simulation_paused(State->Instance, bInPaused ? 1u : 0u);
	UE_LOG(LogREIM, Display, TEXT("simulation %s"), bInPaused ? TEXT("paused")
	                                                          : TEXT("resumed"));
}

bool UReimBridgeSubsystem::IsSimulationPaused() const
{
	return State != nullptr && State->bPaused;
}

FString UReimBridgeSubsystem::GetStatusJson() const
{
	if (State == nullptr || State->Api == nullptr || State->Instance == nullptr ||
	    State->Api->get_status_json == nullptr)
	{
		return TEXT("{}");
	}
	const int32 Needed = State->Api->get_status_json(State->Instance, nullptr, 0);
	if (Needed <= 0) return TEXT("{}");
	TArray<char> Buffer;
	Buffer.SetNumZeroed(Needed + 1);
	const int32 Written =
		State->Api->get_status_json(State->Instance, Buffer.GetData(),
		                            static_cast<uint32>(Buffer.Num()));
	if (Written <= 0) return TEXT("{}");
	return FString(Written, ANSI_TO_TCHAR(Buffer.GetData()));
}

FString UReimBridgeSubsystem::GetInstancePath() const
{
	return State != nullptr ? State->Config.InstancePath : FString();
}

FString UReimBridgeSubsystem::GetNativeVersionPath() const
{
	return State != nullptr ? State->Config.NativeVersionPath : FString();
}

FString UReimBridgeSubsystem::GetAdapterSummary() const
{
	if (State == nullptr) return TEXT("adapter: -");
	return FString::Printf(TEXT("adapter %s %s (UE %s)"),
	                       *State->Descriptor.AdapterId,
	                       *State->Descriptor.AdapterVersion,
	                       TEXT(REIM_ADAPTER_UE_VERSION));
}

IMPLEMENT_MODULE(FDefaultModuleImpl, REIMBridge);
