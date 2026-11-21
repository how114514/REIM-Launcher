/*
 * Host input (M2-D1) and the pause/exit menu state (M2-D3).
 *
 * Every gameplay event is forwarded through the Bridge to ReimInputEvent; the
 * Host never edits the player or the world. Host-local keys (Esc, F1, F3) are
 * consumed here. The menu is drawn by AReimHUD and cannot enter gameplay.
 */
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "ReimPlayerController.generated.h"

class UReimBridgeSubsystem;
class UReimHostSettingsSubsystem;

UCLASS()
class REIMINPUT_API AReimPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AReimPlayerController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupInputComponent() override;

	bool IsMenuOpen() const { return bMenuOpen; }
	bool IsMouseCaptured() const { return bCaptured; }
	bool IsSimulationPaused() const { return bPaused; }

	void OpenMenu();
	void CloseMenu();

	UReimHostSettingsSubsystem* GetHostSettings() const;

	/** True once the Runtime's first ready frame has been seen. */
	bool IsWorldReady() const { return bWorldReady; }

private:
	UReimBridgeSubsystem* GetBridge() const;
	void ApplyKey(const FKey& Key, bool bPressed);
	void OnKeyPressed(FKey Key);
	void OnKeyReleased(FKey Key);
	void OnMouseX(float Value);
	void OnMouseY(float Value);
	void HandleMenuKey(const FKey& Key);
	void ToggleMenu();
	void ToggleHud();
	void TogglePause();
	void CaptureMouse();
	void ReleaseMouse();
	void ReleaseGameplayKeys();
	void UpdateAutoWalk();
	int32 MapKey(const FKey& Key) const;

	bool bMenuOpen = false;
	bool bCaptured = false;
	bool bPaused = false;
	bool bWorldReady = false;
	bool bAutoWalkStarted = false;
	bool bAutoWalkFinished = false;

	float AutoWalkSeconds = 0.0f;
	uint64 AutoWalkTicks = 0;
	uint64 AutoWalkReadyTick = 0;
	double AutoWalkElapsed = 0.0;
};
