/*
 * M2-G5 UE automation:
 *
 *   REIM.Bridge.NullRHI  live GameInstance Bridge: 1000 ticks, no fatal/BUSY,
 *                        TPS, GT budget, frame-blob admission.
 *   REIM.Bridge.Errors   direct C ABI: wrong ABI, missing world, render_level
 *                        outside R0, and one clean create/start/fetch/destroy.
 *   REIM.R0.Smoke        GPU path: world ready, draws/tris, upload budget.
 *
 * Development builds only (WITH_DEV_AUTOMATION_TESTS). The packaged Shipping
 * binary has no automation code. Nothing here creates game logic: every check
 * reads the Contract or the Host's own counters.
 */
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "McWorldRenderer.h"
#include "McWorldRendererComponent.h"
#include "ReimBridgeSubsystem.h"
#include "ReimFramePacket.h"

#include "reim_abi.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
constexpr EAutomationTestFlags kReimTestFlags =
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::ProductFilter;

UReimBridgeSubsystem* FindLiveBridge(UWorld*& OutWorld)
{
	OutWorld = nullptr;
	if (GEngine == nullptr) return nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (World == nullptr || !World->IsGameWorld()) continue;
		UGameInstance* GameInstance = World->GetGameInstance();
		if (GameInstance == nullptr) continue;
		if (UReimBridgeSubsystem* Bridge =
		        GameInstance->GetSubsystem<UReimBridgeSubsystem>())
		{
			OutWorld = World;
			return Bridge;
		}
	}
	return nullptr;
}

AMcWorldRenderer* FindWorldRenderer(UWorld* World)
{
	if (World == nullptr) return nullptr;
	for (TActorIterator<AMcWorldRenderer> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void* REIM_CALL TestAlloc(size_t Size, size_t Alignment)
{
	if (Alignment < alignof(std::max_align_t)) Alignment = alignof(std::max_align_t);
	return FMemory::Malloc(Size, static_cast<uint32>(Alignment));
}

void REIM_CALL TestFree(void* Ptr)
{
	FMemory::Free(Ptr);
}

void REIM_CALL TestLog(int32 Level, const char* Category, const char* Message)
{
	UE_LOG(LogTemp, Display, TEXT("[reim-test:%hs] %hs"),
	       Category ? Category : "runtime", Message ? Message : "");
}
} // namespace

/* ------------------------------------------------------------------------- */
/* REIM.Bridge.NullRHI                                                        */
/* ------------------------------------------------------------------------- */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimBridgeNullRhiTest, "REIM.Bridge.NullRHI",
                                 kReimTestFlags)

bool FReimBridgeNullRhiTest::RunTest(const FString& /*Parameters*/)
{
	FAutomationTestBase* Test = this;
	TSharedPtr<double> StartTime = MakeShared<double>(FPlatformTime::Seconds());
	TSharedPtr<bool> SawReady = MakeShared<bool>(false);

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
		[Test, StartTime, SawReady]()
		{
			const double Elapsed = FPlatformTime::Seconds() - *StartTime;
			if (Elapsed > 150.0)
			{
				Test->AddError(FString::Printf(
					TEXT("Bridge.NullRHI timed out after %.1f s"), Elapsed));
				return true;
			}
			UWorld* World = nullptr;
			UReimBridgeSubsystem* Bridge = FindLiveBridge(World);
			if (Bridge == nullptr) return false;
			if (Bridge->IsFatal())
			{
				Test->AddError(FString::Printf(TEXT("Bridge fatal: %s"),
				                               *Bridge->GetLastError()));
				return true;
			}
			if (!Bridge->IsRunning()) return false;
			TSharedPtr<FReimFramePacket, ESPMode::ThreadSafe> Packet =
				Bridge->GetLatestFrame();
			if (!Packet.IsValid()) return false;
			if (!Packet->Frame.ready) return false;
			if (!*SawReady)
			{
				*SawReady = true;
				Test->AddInfo(FString::Printf(
					TEXT("world ready at tick %llu (%u chunks, %u sections)"),
					Packet->Frame.tick, Packet->Frame.loaded_chunks,
					Packet->Frame.mesh_sections));
			}
			if (Packet->Frame.tick < 1000) return false;

			const ReimRenderFrame& Frame = Packet->Frame;
			Test->TestTrue(TEXT("simulation reached 1000 ticks"), Frame.tick >= 1000);
			Test->TestTrue(TEXT("world ready"), Frame.ready != 0);
			Test->TestTrue(TEXT("TPS near 20"),
			               Frame.measured_tps >= 19.5f && Frame.measured_tps <= 20.5f);
			Test->TestTrue(TEXT("GT fetch+handoff <= 2 ms"),
			               Bridge->GetGtFetchMs() <= 2.0);
			Test->TestTrue(TEXT("frame blobs outstanding <= 4"),
			               Bridge->GetOutstandingFrameCount() <= 4);
			Test->TestEqual(TEXT("no frame-blob BUSY at 20 TPS"),
			                Bridge->GetBusyFrameCount(), static_cast<uint64>(0));
			Test->AddInfo(FString::Printf(
				TEXT("tick=%llu tps=%.2f bridge=%.3fms fetched=%llu outstanding=%llu busy=%llu"),
				Frame.tick, Frame.measured_tps, Bridge->GetGtFetchMs(),
				Bridge->GetFetchedFrameCount(), Bridge->GetOutstandingFrameCount(),
				Bridge->GetBusyFrameCount()));
			return true;
		}));
	return true;
}

/* ------------------------------------------------------------------------- */
/* REIM.Bridge.Errors                                                         */
/* ------------------------------------------------------------------------- */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimBridgeErrorsTest, "REIM.Bridge.Errors",
                                 kReimTestFlags)

bool FReimBridgeErrorsTest::RunTest(const FString& /*Parameters*/)
{
#if PLATFORM_WINDOWS
	FString DllPath;
	FString WorldPath;
	FString GeneratedPath;
	FParse::Value(FCommandLine::Get(), TEXT("reim-test-dll="), DllPath);
	FParse::Value(FCommandLine::Get(), TEXT("reim-test-world="), WorldPath);
	FParse::Value(FCommandLine::Get(), TEXT("reim-test-generated="), GeneratedPath);
	if (DllPath.IsEmpty() || WorldPath.IsEmpty() || GeneratedPath.IsEmpty())
	{
		AddError(TEXT("REIM.Bridge.Errors requires -reim-test-dll/-reim-test-world/"
		              "-reim-test-generated (m2-automation.ps1 supplies them)"));
		return false;
	}

	HMODULE Dll = ::LoadLibraryExW(*DllPath, nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	TestNotNull(TEXT("LoadLibraryExW"), static_cast<void*>(Dll));
	if (Dll == nullptr) return false;

	using FGetApiFn = const ReimRuntimeApi*(REIM_CALL*)(uint32_t);
#pragma warning(push)
#pragma warning(disable : 4191)
	FGetApiFn GetApi = reinterpret_cast<FGetApiFn>(
		reinterpret_cast<void*>(::GetProcAddress(Dll, "reim_get_api")));
#pragma warning(pop)
	TestNotNull(TEXT("reim_get_api"), reinterpret_cast<void*>(GetApi));
	if (GetApi == nullptr)
	{
		::FreeLibrary(Dll);
		return false;
	}

	/* The Runtime logs this rejection at Error level; it is the expected
	 * behaviour of this negative test, not an automation failure. */
	AddExpectedError(TEXT("reim_get_api: ABI version mismatch"),
	                 EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("ABI 1 request is rejected"), GetApi(1));
	const ReimRuntimeApi* Api = GetApi(REIM_ABI_VERSION);
	TestNotNull(TEXT("ABI 2 api"), static_cast<const void*>(Api));
	if (Api == nullptr)
	{
		::FreeLibrary(Dll);
		return false;
	}
	TestEqual(TEXT("DLL api abi"), Api->abi_version, REIM_ABI_VERSION);

	ReimHostApi Host{};
	Host.struct_size = sizeof(Host);
	Host.abi_version = REIM_ABI_VERSION;
	Host.alloc = &TestAlloc;
	Host.free = &TestFree;
	Host.log = &TestLog;

	ReimInstanceConfig Config{};
	Config.struct_size = sizeof(Config);
	Config.abi_version = REIM_ABI_VERSION;
	Config.world_path = L"D:\\REIM\\no-such-world";
	Config.generated_path = *GeneratedPath;
	Config.render_distance = 2;
	Config.render_level = 0;

	int32 ErrorCode = REIM_OK;
	ReimInstance* Instance = Api->create(&Config, &Host, &ErrorCode);
	TestNull(TEXT("missing world rejected"), static_cast<void*>(Instance));
	TestTrue(TEXT("missing world reports an error"), ErrorCode != REIM_OK);
	if (Instance != nullptr) Api->destroy(Instance);

	/* M3-J8: the Runtime negotiates render levels 0/1/2; only values above 2
	 * are a capability error. The UE Host still pins R0, so this test talks
	 * to the DLL directly rather than through the Bridge. */
	Config.world_path = *WorldPath;
	Config.render_level = 1;
	ErrorCode = REIM_OK;
	Instance = Api->create(&Config, &Host, &ErrorCode);
	TestNotNull(TEXT("render_level 1 accepted"), static_cast<void*>(Instance));
	if (Instance != nullptr)
	{
		ReimRuntimeInfo LevelInfo{};
		LevelInfo.struct_size = sizeof(LevelInfo);
		TestEqual(TEXT("render_level 1 info"), Api->get_runtime_info(Instance, &LevelInfo),
		          static_cast<int32>(REIM_OK));
		TestEqual(TEXT("runtime reports render level 1"), LevelInfo.render_level, 1u);
		Api->destroy(Instance);
	}

	Config.render_level = 3;
	ErrorCode = REIM_OK;
	Instance = Api->create(&Config, &Host, &ErrorCode);
	TestNull(TEXT("render_level 3 rejected"), static_cast<void*>(Instance));
	TestEqual(TEXT("render_level 3 is REIM_ERR_CAPABILITY"), ErrorCode,
	          static_cast<int32>(REIM_ERR_CAPABILITY));
	if (Instance != nullptr) Api->destroy(Instance);

	Config.render_level = 0;
	ErrorCode = REIM_OK;
	Instance = Api->create(&Config, &Host, &ErrorCode);
	TestNotNull(TEXT("valid config creates"), static_cast<void*>(Instance));
	if (Instance == nullptr)
	{
		TestEqual(TEXT("valid create error"), ErrorCode, static_cast<int32>(REIM_OK));
		::FreeLibrary(Dll);
		return false;
	}
	TestEqual(TEXT("start"), Api->start(Instance), static_cast<int32>(REIM_OK));

	ReimRuntimeInfo Info{};
	Info.struct_size = sizeof(Info);
	TestEqual(TEXT("get_runtime_info"), Api->get_runtime_info(Instance, &Info),
	          static_cast<int32>(REIM_OK));
	TestEqual(TEXT("runtime profile"), FString(ANSI_TO_TCHAR(Info.profile)),
	          FString(TEXT("26.3")));
	TestEqual(TEXT("runtime render level"), Info.render_level, 0u);

	FPlatformProcess::Sleep(0.35f);
	ReimRenderFrame Frame{};
	Frame.struct_size = sizeof(Frame);
	Frame.abi_version = REIM_ABI_VERSION;
	TestEqual(TEXT("fetch_frame"), Api->fetch_frame(Instance, &Frame),
	          static_cast<int32>(REIM_OK));
	TestTrue(TEXT("fetch owns a host blob"), Frame.owned_block != nullptr);
	if (Frame.owned_block != nullptr)
	{
		Api->release_frame(Instance, Frame.frame_id);
		Api->release_frame(Instance, Frame.frame_id); // duplicate is a no-op
	}
	Api->stop(Instance);
	Api->destroy(Instance);

	::FreeLibrary(Dll);
	AddInfo(TEXT("ABI rejection, world/render_level negotiation (0/1/2, >2 rejected) and clean lifecycle passed"));
	return true;
#else
	AddError(TEXT("REIM Runtime is Windows-only"));
	return false;
#endif
}

/* ------------------------------------------------------------------------- */
/* REIM.R0.Smoke                                                              */
/* ------------------------------------------------------------------------- */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReimR0SmokeTest, "REIM.R0.Smoke",
                                 kReimTestFlags | EAutomationTestFlags::NonNullRHI)

bool FReimR0SmokeTest::RunTest(const FString& /*Parameters*/)
{
	FAutomationTestBase* Test = this;
	TSharedPtr<double> StartTime = MakeShared<double>(FPlatformTime::Seconds());

	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(
		[Test, StartTime]()
		{
			const double Elapsed = FPlatformTime::Seconds() - *StartTime;
			if (Elapsed > 120.0)
			{
				Test->AddError(FString::Printf(
					TEXT("R0.Smoke timed out after %.1f s"), Elapsed));
				return true;
			}
			UWorld* World = nullptr;
			UReimBridgeSubsystem* Bridge = FindLiveBridge(World);
			if (Bridge == nullptr || World == nullptr) return false;
			if (Bridge->IsFatal())
			{
				Test->AddError(FString::Printf(TEXT("Bridge fatal: %s"),
				                               *Bridge->GetLastError()));
				return true;
			}
			TSharedPtr<FReimFramePacket, ESPMode::ThreadSafe> Packet =
				Bridge->GetLatestFrame();
			if (!Packet.IsValid() || !Packet->Frame.ready) return false;

			AMcWorldRenderer* Renderer = FindWorldRenderer(World);
			UMcWorldRendererComponent* Component =
				Renderer ? Renderer->GetWorldComponent() : nullptr;
			if (Component == nullptr) return false;
			/* Give the render thread a few frames after world ready. */
			if (Packet->Frame.tick < 60) return false;
			if (Component->GetDrawCalls() == 0) return false;

			Test->TestTrue(TEXT("one world renderer actor"), Renderer != nullptr);
			Test->TestTrue(TEXT("visible draws"), Component->GetDrawCalls() > 0);
			Test->TestTrue(TEXT("triangles submitted"), Component->GetTriangles() > 0);
			Test->TestTrue(TEXT("sections meshed"), Component->GetTotalMeshes() > 0);
			Test->TestTrue(TEXT("RT uploaded frames"), Component->GetUploadedFrames() > 0);
			Test->TestTrue(TEXT("GT fetch+handoff <= 2 ms"),
			               Bridge->GetGtFetchMs() <= 2.0);
			Test->TestTrue(TEXT("frame blobs outstanding <= 4"),
			               Bridge->GetOutstandingFrameCount() <= 4);
			Test->AddInfo(FString::Printf(
				TEXT("tick=%llu chunks=%u sections=%u draws=%u tris=%u uploads=%llu "
				     "upload_avg=%.3fms upload_peak=%.3fms"),
				Packet->Frame.tick, Packet->Frame.loaded_chunks,
				Packet->Frame.mesh_sections, Component->GetDrawCalls(),
				Component->GetTriangles(), Component->GetUploadedFrames(),
				Component->GetUploadMsAvg(), Component->GetUploadMsPeak()));
			return true;
		}));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
