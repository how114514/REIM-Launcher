#include "ReimGameMode.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

#include "McWorldRenderer.h"
#include "McWorldRendererComponent.h"
#include "ReimBridgeSubsystem.h"
#include "ReimFramePacket.h"
#include "ReimHostSettingsSubsystem.h"
#include "ReimHUD.h"
#include "ReimHostLog.h"
#include "ReimLog.h"
#include "ReimPlayerController.h"

#include "Engine/Engine.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"

namespace
{
	FString JsonEscape(const FString& Text)
	{
		FString Out;
		Out.Reserve(Text.Len() + 8);
		for (const TCHAR C : Text)
		{
			if (C == TEXT('\\') || C == TEXT('"')) Out += TEXT('\\');
			if (C == TEXT('\n')) { Out += TEXT("\\n"); continue; }
			if (C == TEXT('\r')) { Out += TEXT("\\r"); continue; }
			Out += C;
		}
		return Out;
	}
}

AReimGameMode::AReimGameMode()
{
	FParse::Value(FCommandLine::Get(), TEXT("reim-shot="), CommandLineShotPath);
	FParse::Value(FCommandLine::Get(), TEXT("reim-shot-delay="), CommandLineShotDelay);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AReimPlayerController::StaticClass();
	HUDClass = FParse::Param(FCommandLine::Get(), TEXT("reim-nohud"))
		? nullptr : AReimHUD::StaticClass();
	bStartPlayersAsSpectators = false;

	FParse::Value(FCommandLine::Get(), TEXT("reim-exit-after="), ExitAfterSeconds);
	FParse::Value(FCommandLine::Get(), TEXT("reim-state-report="), StateReportPath);
	FParse::Value(FCommandLine::Get(), TEXT("reim-perf-report="), PerfReportPath);
	int32 StateTick = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("reim-state-tick="), StateTick) &&
	    StateTick > 0)
	{
		StateReportTick = static_cast<uint64>(StateTick);
	}
	int32 ExitTick = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("reim-exit-tick="), ExitTick) && ExitTick > 0)
	{
		ExitAfterTick = static_cast<uint64>(ExitTick);
	}
}

void AReimGameMode::StartPlay()
{
	Super::StartPlay();

	UWorld* World = GetWorld();
	if (World == nullptr) return;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = TEXT("McWorldRenderer");
	AMcWorldRenderer* Renderer = World->SpawnActor<AMcWorldRenderer>(
		AMcWorldRenderer::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator,
		SpawnParams);
	WorldRenderer = Renderer;

	if (Renderer != nullptr)
	{
		UE_LOG(LogREIM, Display, TEXT("R0 world renderer spawned"));
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator();
		     It; ++It)
		{
			if (APlayerController* PC = It->Get())
			{
				PC->SetViewTarget(Renderer);
			}
		}
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UReimHostSettingsSubsystem* Settings =
		        GameInstance->GetSubsystem<UReimHostSettingsSubsystem>())
		{
			GEngine->Exec(World, *FString::Printf(TEXT("r.VSync %d"),
			                                      Settings->GetVSync() ? 1 : 0));
			GEngine->Exec(World, *FString::Printf(TEXT("t.MaxFPS %d"),
			                                      Settings->GetFrameRateLimit()));
			UE_LOG(LogREIM, Display,
			       TEXT("host settings applied: vsync=%d maxfps=%d hud=%d"),
			       Settings->GetVSync() ? 1 : 0, Settings->GetFrameRateLimit(),
			       Settings->GetShowHud() ? 1 : 0);
		}
		if (UReimBridgeSubsystem* Bridge =
		        GameInstance->GetSubsystem<UReimBridgeSubsystem>())
		{
			UE_LOG(LogREIM, Display, TEXT("%s"), *Bridge->GetStatusSummary());
			UE_LOG(LogREIM, Display, TEXT("%s"), *Bridge->GetAdapterSummary());
			if (Bridge->IsFatal())
			{
				UE_LOG(LogREIM, Error, TEXT("Bridge fatal: %s"), *Bridge->GetLastError());
			}
		}
	}
}

void AReimGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	/* Covers the pause-menu exit path, where no Tick exit branch runs. */
	WritePerfReport();
	Super::EndPlay(EndPlayReason);
}

void AReimGameMode::ShowFatalDialog(const FString& Message)
{
	if (bFatalDialogShown) return;
	bFatalDialogShown = true;
	UE_LOG(LogREIM, Error, TEXT("fatal: %s"), *Message);
	reim::HostLogWrite(REIM_LOG_ERROR, TEXT("host"),
	                   FString::Printf(TEXT("fatal: %s (exit code 2)"), *Message));
	if (FParse::Param(FCommandLine::Get(), TEXT("unattended")) ||
	    FParse::Param(FCommandLine::Get(), TEXT("nullrhi")) ||
	    FParse::Param(FCommandLine::Get(), TEXT("reim-nodialog")))
	{
		return;
	}
	FPlatformMisc::MessageBoxExt(EAppMsgType::Ok, *Message,
	                             TEXT("REIM Runtime could not start"));
}

void AReimGameMode::WriteStateReport(const ReimRenderFrame& Frame,
                                     UReimBridgeSubsystem* Bridge)
{
	if (StateReportPath.IsEmpty() || bStateReportWritten) return;
	bStateReportWritten = true;

	const FString Status = Bridge ? Bridge->GetStatusJson() : TEXT("{}");
	FString Out;
	Out += TEXT("{\n");
	Out += TEXT("  \"stage\": \"S2-ue\",\n");
	Out += FString::Printf(TEXT("  \"abi\": %u,\n"), REIM_ABI_VERSION);
	Out += FString::Printf(TEXT("  \"tick\": %llu,\n"), Frame.tick);
	Out += FString::Printf(TEXT("  \"ready\": %s,\n"), Frame.ready ? TEXT("true") : TEXT("false"));
	Out += FString::Printf(TEXT("  \"loaded_chunks\": %u,\n"), Frame.loaded_chunks);
	Out += FString::Printf(TEXT("  \"mesh_sections\": %u,\n"), Frame.mesh_sections);
	Out += FString::Printf(TEXT("  \"block_count\": %llu,\n"), Frame.block_count);
	Out += FString::Printf(TEXT("  \"measured_tps\": %.3f,\n"), Frame.measured_tps);
	Out += FString::Printf(TEXT("  \"player\": {\"x\": %.6f, \"y\": %.6f, \"z\": %.6f, "
	                            "\"yaw\": %.6f, \"pitch\": %.6f, \"flags\": %u},\n"),
	                       Frame.player.position.x, Frame.player.position.y,
	                       Frame.player.position.z, Frame.player.yaw, Frame.player.pitch,
	                       Frame.player.flags);
	Out += FString::Printf(TEXT("  \"native_version\": \"%s\",\n"),
	                       *JsonEscape(Bridge ? Bridge->GetNativeVersionPath() : FString()));
	Out += FString::Printf(TEXT("  \"adapter\": \"%s\",\n"),
	                       *JsonEscape(Bridge ? Bridge->GetAdapterSummary() : FString()));
	Out += TEXT("  \"status\": ") + Status + TEXT("\n");
	Out += TEXT("}\n");

	const FString Directory = FPaths::GetPath(StateReportPath);
	if (!Directory.IsEmpty()) IFileManager::Get().MakeDirectory(*Directory, true);
	if (FFileHelper::SaveStringToFile(Out, *StateReportPath))
	{
		UE_LOG(LogREIM, Display, TEXT("state report written: %s (tick %llu)"),
		       *StateReportPath, Frame.tick);
	}
	else
	{
		UE_LOG(LogREIM, Error, TEXT("state report write failed: %s"), *StateReportPath);
	}
}

void AReimGameMode::SamplePerf(const ReimRenderFrame& Frame,
                               UReimBridgeSubsystem* Bridge)
{
	if (Bridge == nullptr || GetWorld() == nullptr) return;

	const double Fps = 1.0 / FMath::Max(GetWorld()->GetDeltaSeconds(), 0.0001);
	const double Tps = Frame.measured_tps;
	const double FetchMs = Bridge->GetGtFetchMs();

	++PerfSampleCount;
	PerfFpsTotal += Fps;
	PerfFpsMin = FMath::Min(PerfFpsMin, Fps);
	PerfFpsMax = FMath::Max(PerfFpsMax, Fps);
	if (Tps > 0.0)
	{
		PerfTpsTotal += Tps;
		PerfTpsMin = FMath::Min(PerfTpsMin, Tps);
		PerfTpsMax = FMath::Max(PerfTpsMax, Tps);
	}
	PerfBridgeTotal += FetchMs;
	PerfBridgePeak = FMath::Max(PerfBridgePeak, FetchMs);
	if (PerfFetchSamples.Num() < 200000)
	{
		PerfFetchSamples.Add(static_cast<float>(FetchMs));
	}
	PerfOutstandingPeak = FMath::Max(PerfOutstandingPeak,
	                                 Bridge->GetOutstandingFrameCount());
	PerfLastTick = Frame.tick;

	/* Count busy frames from admission on, not whatever the first sample saw. */
	if (PerfSampleCount == 1) PerfBusyAtStart = Bridge->GetBusyFrameCount();

	UMcWorldRendererComponent* World =
		WorldRenderer.IsValid() ? WorldRenderer->GetWorldComponent() : nullptr;
	if (World != nullptr)
	{
		PerfPendingUploadPeak = FMath::Max(PerfPendingUploadPeak, World->GetPendingUploads());
		PerfDrawsMax = FMath::Max(PerfDrawsMax, World->GetDrawCalls());
		PerfTrisMax = FMath::Max(PerfTrisMax, World->GetTriangles());
		if (!bReadyUploadCaptured)
		{
			bReadyUploadCaptured = true;
			ReadyUploadFrames = World->GetUploadedFrames();
			ReadyUploadBytes = World->GetUploadedBytes();
			ReadyUploadMs = World->GetUploadMsAvg() *
			                static_cast<double>(ReadyUploadFrames);
		}
	}
}

void AReimGameMode::WritePerfReport()
{
	if (PerfReportPath.IsEmpty() || bPerfWritten) return;
	bPerfWritten = true;

	UGameInstance* GameInstance = GetGameInstance();
	UReimBridgeSubsystem* Bridge =
		GameInstance ? GameInstance->GetSubsystem<UReimBridgeSubsystem>() : nullptr;
	UMcWorldRendererComponent* World =
		WorldRenderer.IsValid() ? WorldRenderer->GetWorldComponent() : nullptr;

	const uint64 UploadFrames = World ? World->GetUploadedFrames() : 0;
	const uint64 UploadBytes = World ? World->GetUploadedBytes() : 0;
	const double UploadTotalMs = World
		? World->GetUploadMsAvg() * static_cast<double>(UploadFrames) : 0.0;
	const uint64 SteadyFrames = UploadFrames > ReadyUploadFrames
		? UploadFrames - ReadyUploadFrames : 0;
	const double SteadyMs = UploadTotalMs > ReadyUploadMs
		? UploadTotalMs - ReadyUploadMs : 0.0;
	const double FpsAvg = PerfSampleCount > 0 ? PerfFpsTotal / PerfSampleCount : 0.0;
	const double TpsAvg = PerfSampleCount > 0 ? PerfTpsTotal / PerfSampleCount : 0.0;
	const double FetchAvg = PerfSampleCount > 0 ? PerfBridgeTotal / PerfSampleCount : 0.0;

	TArray<float> SortedFetch = PerfFetchSamples;
	SortedFetch.Sort();
	const auto Percentile = [&SortedFetch](double Q) -> double
	{
		if (SortedFetch.Num() == 0) return 0.0;
		const int32 Index = FMath::Clamp(
			FMath::FloorToInt(Q * (SortedFetch.Num() - 1)), 0, SortedFetch.Num() - 1);
		return SortedFetch[Index];
	};

	FString Out;
	Out += TEXT("{\n");
	Out += TEXT("  \"stage\": \"S3-ue-perf\",\n");
	Out += FString::Printf(TEXT("  \"duration_s\": %.3f,\n"), ElapsedSeconds);
	Out += FString::Printf(TEXT("  \"ready_at_s\": %.3f,\n"), FirstReadySeconds);
	Out += FString::Printf(TEXT("  \"samples\": %llu,\n"), PerfSampleCount);
	Out += FString::Printf(TEXT("  \"last_tick\": %llu,\n"), PerfLastTick);
	Out += FString::Printf(
		TEXT("  \"tps\": {\"min\": %.3f, \"avg\": %.3f, \"max\": %.3f},\n"),
		PerfTpsMin, TpsAvg, PerfTpsMax);
	Out += FString::Printf(
		TEXT("  \"fps\": {\"min\": %.2f, \"avg\": %.2f, \"max\": %.2f},\n"),
		PerfFpsMin, FpsAvg, PerfFpsMax);
	Out += FString::Printf(
		TEXT("  \"bridge_fetch_ms\": {\"avg\": %.4f, \"p50\": %.4f, \"p99\": %.4f, "
		     "\"p999\": %.4f, \"peak\": %.4f, \"samples\": %d},\n"),
		FetchAvg, Percentile(0.50), Percentile(0.99), Percentile(0.999),
		PerfBridgePeak, PerfFetchSamples.Num());
	Out += FString::Printf(
		TEXT("  \"frame_blobs\": {\"fetched\": %llu, \"outstanding_peak\": %llu, "
		     "\"busy_frames\": %llu},\n"),
		Bridge ? Bridge->GetFetchedFrameCount() : 0, PerfOutstandingPeak,
		Bridge ? Bridge->GetBusyFrameCount() - PerfBusyAtStart : 0);
	Out += FString::Printf(
		TEXT("  \"rt_upload\": {\"frames\": %llu, \"bytes\": %llu, \"avg_ms\": %.4f, "
		     "\"peak_ms\": %.4f, \"steady_frames\": %llu, \"steady_avg_ms\": %.4f, "
		     "\"pending_peak\": %llu},\n"),
		UploadFrames, UploadBytes,
		UploadFrames > 0 ? UploadTotalMs / UploadFrames : 0.0,
		World ? World->GetUploadMsPeak() : 0.0, SteadyFrames,
		SteadyFrames > 0 ? SteadyMs / SteadyFrames : 0.0, PerfPendingUploadPeak);
	Out += FString::Printf(TEXT("  \"draw_calls_max\": %u,\n"), PerfDrawsMax);
	Out += FString::Printf(TEXT("  \"triangles_max\": %u,\n"), PerfTrisMax);
	Out += FString::Printf(TEXT("  \"fatal\": %s,\n"),
	                       (Bridge && Bridge->IsFatal()) ? TEXT("true") : TEXT("false"));
	Out += FString::Printf(TEXT("  \"last_error\": \"%s\",\n"),
	                       *JsonEscape(Bridge ? Bridge->GetLastError() : FString()));
	Out += FString::Printf(TEXT("  \"instance_path\": \"%s\",\n"),
	                       *JsonEscape(Bridge ? Bridge->GetInstancePath() : FString()));
	Out += FString::Printf(TEXT("  \"native_version\": \"%s\"\n"),
	                       *JsonEscape(Bridge ? Bridge->GetNativeVersionPath() : FString()));
	Out += TEXT("}\n");

	const FString Directory = FPaths::GetPath(PerfReportPath);
	if (!Directory.IsEmpty()) IFileManager::Get().MakeDirectory(*Directory, true);
	if (FFileHelper::SaveStringToFile(Out, *PerfReportPath))
	{
		UE_LOG(LogREIM, Display, TEXT("perf report written: %s"), *PerfReportPath);
		reim::HostLogEvent(FString::Printf(TEXT("perf report written: %s"),
		                                   *PerfReportPath));
	}
	else
	{
		UE_LOG(LogREIM, Error, TEXT("perf report write failed: %s"), *PerfReportPath);
		reim::HostLogWrite(REIM_LOG_ERROR, TEXT("host"),
		                   FString::Printf(TEXT("perf report write failed: %s"),
		                                   *PerfReportPath));
	}
}

void AReimGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ElapsedSeconds += DeltaSeconds;
	if (!bCommandLineShotDone && !CommandLineShotPath.IsEmpty() && ElapsedSeconds >= CommandLineShotDelay)
	{
		bCommandLineShotDone = true;
		const FString Directory = FPaths::GetPath(CommandLineShotPath);
		if (!Directory.IsEmpty()) IFileManager::Get().MakeDirectory(*Directory, true);
		FScreenshotRequest::RequestScreenshot(CommandLineShotPath, false, false);
		UE_LOG(LogREIM, Display, TEXT("command-line screenshot requested: %s"),
		       *CommandLineShotPath);
	}

	UGameInstance* GameInstance = GetGameInstance();
	UReimBridgeSubsystem* Bridge =
		GameInstance ? GameInstance->GetSubsystem<UReimBridgeSubsystem>() : nullptr;
	if (Bridge == nullptr)
	{
		if (!bExitRequested && ExitAfterSeconds > 0.0f &&
		    ElapsedSeconds >= ExitAfterSeconds)
		{
			bExitRequested = true;
			UE_LOG(LogREIM, Display, TEXT("exit after %.1f s"), ElapsedSeconds);
			WritePerfReport();
			FPlatformMisc::RequestExitWithStatus(false, 0);
		}
		return;
	}

	if (Bridge->IsFatal() && !bExitRequested)
	{
		bExitRequested = true;
		WritePerfReport();
		ShowFatalDialog(Bridge->GetLastError());
		/* A graceful UE exit discards the requested return code; a startup
		 * failure must be observable as a nonzero exit (M2-D5). Logs were
		 * already written by ShowFatalDialog. */
		GLog->Flush();
		FPlatformMisc::RequestExitWithStatus(true, 2);
		return;
	}

	TSharedPtr<FReimFramePacket, ESPMode::ThreadSafe> Packet = Bridge->GetLatestFrame();
	if (Packet.IsValid())
	{
		const ReimRenderFrame& Frame = Packet->Frame;
		const bool bReportDue =
			StateReportTick > 0 ? Frame.tick >= StateReportTick
			                    : (ElapsedSeconds >= 3.0 && Frame.ready != 0);
		if (!StateReportPath.IsEmpty() && !bStateReportWritten && Frame.ready && bReportDue)
		{
			/* Pause first, then report on the next frame: get_status_json then
			 * describes the same tick as this frame (G1 comparison). */
			if (!bStateReportPauseRequested)
			{
				bStateReportPauseRequested = true;
				Bridge->SetSimulationPaused(true);
			}
			else
			{
				WriteStateReport(Frame, Bridge);
			}
		}
		if (Frame.ready && !bLoggedReady)
		{
			bLoggedReady = true;
			FirstReadySeconds = ElapsedSeconds;
			UE_LOG(LogREIM, Display,
			       TEXT("world ready: %u chunks, %u meshed sections, spawn %.2f/%.2f/%.2f"),
			       Frame.loaded_chunks, Frame.mesh_sections, Frame.player.position.x,
			       Frame.player.position.y, Frame.player.position.z);
			Bridge->LogEvent(FString::Printf(
				TEXT("world ready: %u chunks, %u meshed sections, spawn %.2f/%.2f/%.2f"),
				Frame.loaded_chunks, Frame.mesh_sections, Frame.player.position.x,
				Frame.player.position.y, Frame.player.position.z));
		}
		if (Frame.ready) SamplePerf(Frame, Bridge);
		if (ElapsedSeconds >= NextStatusLog)
		{
			NextStatusLog += 5.0;
			UMcWorldRendererComponent* World =
				WorldRenderer.IsValid() ? WorldRenderer->GetWorldComponent() : nullptr;
			UE_LOG(LogREIM, Display,
			       TEXT("tick=%llu tps=%.2f fps=%.1f chunks=%u sections=%u updates=%u "
			            "removes=%u bridge=%.2fms busy=%llu out=%llu upload=%.2f/%.2fms "
			            "pending=%llu"),
			       Frame.tick, Frame.measured_tps, 1.0f / FMath::Max(DeltaSeconds, 1e-4f),
			       Frame.loaded_chunks, Frame.mesh_sections, Frame.update_count,
			       Frame.remove_count, Bridge->GetGtFetchMs(), Bridge->GetBusyFrameCount(),
			       Bridge->GetOutstandingFrameCount(),
			       World ? World->GetUploadMsAvg() : 0.0,
			       World ? World->GetUploadMsPeak() : 0.0,
			       World ? World->GetPendingUploads() : 0);
		}
		const bool bReportPending = !StateReportPath.IsEmpty() && !bStateReportWritten;
		if (!bExitRequested && !bReportPending && ExitAfterTick > 0 &&
		    Frame.tick >= ExitAfterTick)
		{
			bExitRequested = true;
			UE_LOG(LogREIM, Display, TEXT("exit after tick %llu"), Frame.tick);
			Bridge->LogEvent(FString::Printf(TEXT("exit after tick %llu (code 0)"),
			                                 Frame.tick));
			WritePerfReport();
			FPlatformMisc::RequestExitWithStatus(false, 0);
		}
	}

	if (!bExitRequested && ExitAfterSeconds > 0.0f && ElapsedSeconds >= ExitAfterSeconds &&
	    (StateReportPath.IsEmpty() || bStateReportWritten))
	{
		bExitRequested = true;
		UE_LOG(LogREIM, Display, TEXT("exit after %.1f s"), ElapsedSeconds);
		if (Bridge != nullptr)
		{
			Bridge->LogEvent(FString::Printf(TEXT("exit after %.1f s (code 0)"),
			                                 ElapsedSeconds));
		}
		else
		{
			reim::HostLogEvent(FString::Printf(TEXT("exit after %.1f s (code 0)"),
			                                   ElapsedSeconds));
		}
		WritePerfReport();
		FPlatformMisc::RequestExitWithStatus(false, 0);
	}
}
