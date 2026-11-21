#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "ReimGameMode.generated.h"

class AMcWorldRenderer;
class UReimBridgeSubsystem;
struct ReimRenderFrame;

/** Composes the R0 host: one world renderer actor, the Bridge subsystem and a
 *  minimal HUD. No gameplay rules live here (M2-D01/D02). */
UCLASS()
class REIMGAME_API AReimGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AReimGameMode();

	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	AMcWorldRenderer* GetWorldRenderer() const { return WorldRenderer.Get(); }

private:
	void ShowFatalDialog(const FString& Message);
	void WriteStateReport(const ReimRenderFrame& Frame, UReimBridgeSubsystem* Bridge);
	void SamplePerf(const ReimRenderFrame& Frame, UReimBridgeSubsystem* Bridge);
	void WritePerfReport();

	TWeakObjectPtr<AMcWorldRenderer> WorldRenderer;
	double ElapsedSeconds = 0.0;
	double NextStatusLog = 5.0;
	bool bLoggedReady = false;
	float ExitAfterSeconds = 0.0f;
	uint64 ExitAfterTick = 0;
	bool bExitRequested = false;
	bool bFatalDialogShown = false;
	FString CommandLineShotPath;
	double CommandLineShotDelay = 2.0;
	bool bCommandLineShotDone = false;
	FString StateReportPath;
	uint64 StateReportTick = 0;
	bool bStateReportWritten = false;
	bool bStateReportPauseRequested = false;

	/* M2-G2/G3 performance baseline, written when the process exits. */
	FString PerfReportPath;
	bool bPerfWritten = false;
	double PerfStartSeconds = 0.0;
	double FirstReadySeconds = -1.0;
	uint64 PerfSampleCount = 0;
	double PerfFpsTotal = 0.0;
	double PerfFpsMin = 1.0e9;
	double PerfFpsMax = 0.0;
	double PerfTpsTotal = 0.0;
	double PerfTpsMin = 1.0e9;
	double PerfTpsMax = 0.0;
	double PerfBridgeTotal = 0.0;
	double PerfBridgePeak = 0.0;
	/** One GT fetch sample per rendered frame; percentiles are computed in the
	 *  perf report so a single scheduler outlier is visible but not confused
	 *  with the steady budget (M2-G3). */
	TArray<float> PerfFetchSamples;
	uint64 PerfOutstandingPeak = 0;
	uint64 PerfPendingUploadPeak = 0;
	uint64 PerfBusyAtStart = 0;
	uint32 PerfDrawsMax = 0;
	uint32 PerfTrisMax = 0;
	uint64 PerfLastTick = 0;
	/* Upload totals at first ready; steady state excludes the initial load. */
	bool bReadyUploadCaptured = false;
	uint64 ReadyUploadFrames = 0;
	uint64 ReadyUploadBytes = 0;
	double ReadyUploadMs = 0.0;
};
