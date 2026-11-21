/*
 * Host-side settings (M2-D4). Values live beside the Runtime config in
 * instance.json; the Runtime only ever sees the ReimInstanceConfig fields
 * forwarded by the Bridge. Render distance changes require a restart and are
 * reported as pending.
 */
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ReimHostSettingsSubsystem.generated.h"

UCLASS()
class REIMINPUT_API UReimHostSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	FString GetInstancePath() const { return InstancePath; }

	float GetMouseSensitivity() const { return MouseSensitivity; }
	bool GetInvertY() const { return bInvertY; }
	uint32 GetRenderDistance() const { return RenderDistance; }
	uint32 GetPendingRenderDistance() const { return PendingRenderDistance; }
	bool IsRenderDistanceRestartPending() const;
	bool GetVSync() const { return bVSync; }
	int32 GetFrameRateLimit() const { return FrameRateLimit; }
	bool GetShowHud() const { return bShowHud; }

	void SetMouseSensitivity(float Value);
	void SetInvertY(bool bValue);
	void SetVSync(bool bValue);
	void SetFrameRateLimit(int32 Value);
	void SetShowHud(bool bValue);
	void RequestRenderDistance(uint32 Value);

	/** Writes the host settings back to instance.json (Runtime fields are
	 *  preserved). Returns false and logs when the file cannot be written. */
	bool SaveToInstance();

private:
	FString InstancePath;
	float MouseSensitivity = 0.15f;
	bool bInvertY = false;
	uint32 RenderDistance = 6;
	uint32 PendingRenderDistance = 6;
	bool bVSync = false;
	int32 FrameRateLimit = 0;
	bool bShowHud = true;
	bool bDirty = false;
};
