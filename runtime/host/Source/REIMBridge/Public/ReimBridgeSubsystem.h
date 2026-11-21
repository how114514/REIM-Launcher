/*
 * UReimBridgeSubsystem (M2-A3/A5, M2-B5/B6).
 *
 * Owns the only DLL instance in the process. The Game Thread calls Tick(),
 * which fetches one incremental frame per frame and publishes it as a
 * FReimFramePacket. Rendering consumes packets; the Runtime never sees UE
 * objects, and this module never parses saves or builds meshes.
 */
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"

#include "reim_abi.h"

#include "ReimBridgeSubsystem.generated.h"

class FReimBridgeState;
struct FReimFramePacket;

UCLASS()
class REIMBRIDGE_API UReimBridgeSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual ~UReimBridgeSubsystem() override;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickableInEditor() const override { return false; }

	/** Latest fetched packet (Game Thread only). May be null before the first
	 *  successful fetch. */
	TSharedPtr<FReimFramePacket, ESPMode::ThreadSafe> GetLatestFrame() const;

	bool IsRunning() const;
	bool IsFatal() const;
	FString GetLastError() const;
	FString GetStatusSummary() const;

	/** Atlas bytes returned by get_asset; owned by the Runtime. */
	const TArray<uint8>& GetAtlasPng() const;
	FString GetAtlasMeta() const;
	uint64 GetAtlasRevision() const;

	double GetGtFetchMs() const;
	uint64 GetBusyFrameCount() const;
	uint64 GetFetchedFrameCount() const;
	uint64 GetOutstandingFrameCount() const;

	/** Input goes only through the ABI (M2-D1); the Host never edits the
	 *  player or world. */
	bool SubmitInput(const ReimInputEvent& Event);
	void SubmitKey(int32 Key, bool bPressed, bool bRepeat = false);
	void SubmitMouseMotion(float Dx, float Dy);
	void SubmitMouseButton(int32 Button, bool bPressed);
	void SetSimulationPaused(bool bPaused);
	bool IsSimulationPaused() const;

	/** Runtime diagnostics JSON (tick, player, chunk counts, block samples). */
	FString GetStatusJson() const;

	FString GetInstancePath() const;
	FString GetNativeVersionPath() const;
	FString GetAdapterSummary() const;

	/** Host events that must survive Shipping (no engine log): world ready,
	 *  exit reason, perf report. */
	void LogEvent(const FString& Message) const;
	FString GetHostLogPath() const;

private:
	/* Raw pointer so UHT-generated constructors do not need the complete type
	 * (destructor is out of line and deletes it). */
	FReimBridgeState* State = nullptr;
};
