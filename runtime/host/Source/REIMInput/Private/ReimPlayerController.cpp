#include "ReimPlayerController.h"

#include "ReimBridgeSubsystem.h"
#include "ReimFramePacket.h"
#include "ReimHostSettingsSubsystem.h"
#include "ReimLog.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerInput.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AReimPlayerController::AReimPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
	bShowMouseCursor = false;
	FString Value;
	if (FParse::Value(FCommandLine::Get(), TEXT("reim-auto-walk-ticks="), Value))
	{
		AutoWalkTicks = static_cast<uint64>(FCString::Strtoui64(*Value, nullptr, 10));
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("reim-auto-walk="), Value))
	{
		AutoWalkSeconds = FCString::Atof(*Value);
	}
}

UReimHostSettingsSubsystem* AReimPlayerController::GetHostSettings() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UReimHostSettingsSubsystem>() : nullptr;
}

void AReimPlayerController::BeginPlay()
{
	Super::BeginPlay();
	bShowMouseCursor = false;
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameAndUI());
	}
	UE_LOG(LogREIM, Display,
	       TEXT("input: click to capture, Esc menu, F1 HUD, F3 pause%s%llu"),
	       AutoWalkTicks > 0 ? TEXT(", auto-walk-ticks=") : TEXT(""),
	       AutoWalkTicks);
	if (FParse::Param(FCommandLine::Get(), TEXT("reim-menu")))
	{
		/* Diagnostic hook: open the pause menu at start for a screenshot. */
		OpenMenu();
	}
}

void AReimPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (InputComponent == nullptr) return;

	InputComponent->BindKey(EKeys::Escape, IE_Pressed, this,
	                        &AReimPlayerController::OnKeyPressed);
	InputComponent->BindKey(EKeys::F1, IE_Pressed, this,
	                        &AReimPlayerController::OnKeyPressed);
	InputComponent->BindKey(EKeys::F3, IE_Pressed, this,
	                        &AReimPlayerController::OnKeyPressed);
	InputComponent->BindKey(EKeys::R, IE_Pressed, this,
	                        &AReimPlayerController::OnKeyPressed);
	InputComponent->BindKey(EKeys::Q, IE_Pressed, this,
	                        &AReimPlayerController::OnKeyPressed);
	InputComponent->BindKey(EKeys::V, IE_Pressed, this,
	                        &AReimPlayerController::OnKeyPressed);
	InputComponent->BindKey(EKeys::I, IE_Pressed, this,
	                        &AReimPlayerController::OnKeyPressed);
	InputComponent->BindKey(EKeys::F, IE_Pressed, this,
	                        &AReimPlayerController::OnKeyPressed);
	InputComponent->BindKey(EKeys::LeftBracket, IE_Pressed, this,
	                        &AReimPlayerController::OnKeyPressed);
	InputComponent->BindKey(EKeys::RightBracket, IE_Pressed, this,
	                        &AReimPlayerController::OnKeyPressed);

	for (const FKey& Chord : {EKeys::W, EKeys::A, EKeys::S, EKeys::D, EKeys::SpaceBar,
	                          EKeys::LeftShift, EKeys::LeftControl, EKeys::Up,
	                          EKeys::Down, EKeys::Left, EKeys::Right,
	                          EKeys::LeftMouseButton, EKeys::RightMouseButton})
	{
		InputComponent->BindKey(Chord, IE_Pressed, this,
		                        &AReimPlayerController::OnKeyPressed);
		InputComponent->BindKey(Chord, IE_Released, this,
		                        &AReimPlayerController::OnKeyReleased);
	}

	InputComponent->BindAxisKey(EKeys::MouseX, this,
	                            &AReimPlayerController::OnMouseX);
	InputComponent->BindAxisKey(EKeys::MouseY, this,
	                            &AReimPlayerController::OnMouseY);
}

int32 AReimPlayerController::MapKey(const FKey& Key) const
{
	if (Key == EKeys::W) return REIM_KEY_W;
	if (Key == EKeys::A) return REIM_KEY_A;
	if (Key == EKeys::S) return REIM_KEY_S;
	if (Key == EKeys::D) return REIM_KEY_D;
	if (Key == EKeys::SpaceBar) return REIM_KEY_SPACE;
	if (Key == EKeys::LeftShift) return REIM_KEY_LEFT_SHIFT;
	if (Key == EKeys::LeftControl) return REIM_KEY_LEFT_CONTROL;
	if (Key == EKeys::Up) return REIM_KEY_UP;
	if (Key == EKeys::Down) return REIM_KEY_DOWN;
	if (Key == EKeys::Left) return REIM_KEY_LEFT;
	if (Key == EKeys::Right) return REIM_KEY_RIGHT;
	return REIM_KEY_UNKNOWN;
}

UReimBridgeSubsystem* AReimPlayerController::GetBridge() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UReimBridgeSubsystem>() : nullptr;
}

void AReimPlayerController::ApplyKey(const FKey& Key, bool bPressed)
{
	UReimBridgeSubsystem* Bridge = GetBridge();
	if (Bridge == nullptr) return;
	if (Key == EKeys::LeftMouseButton)
	{
		if (bPressed) CaptureMouse();
		Bridge->SubmitMouseButton(0, bPressed);
		return;
	}
	if (Key == EKeys::RightMouseButton)
	{
		Bridge->SubmitMouseButton(1, bPressed);
		return;
	}
	const int32 Mapped = MapKey(Key);
	if (Mapped != REIM_KEY_UNKNOWN)
	{
		Bridge->SubmitKey(Mapped, bPressed);
	}
}

void AReimPlayerController::OnKeyPressed(FKey Key)
{
	if (Key == EKeys::Escape)
	{
		ToggleMenu();
		return;
	}
	if (Key == EKeys::F1)
	{
		ToggleHud();
		return;
	}
	if (Key == EKeys::F3)
	{
		TogglePause();
		return;
	}
	if (bMenuOpen)
	{
		HandleMenuKey(Key);
		return;
	}
	ApplyKey(Key, true);
}

void AReimPlayerController::OnKeyReleased(FKey Key)
{
	if (bMenuOpen) return;
	ApplyKey(Key, false);
}

void AReimPlayerController::OnMouseX(float Value)
{
	if (!bCaptured || bMenuOpen) return;
	if (UReimBridgeSubsystem* Bridge = GetBridge()) Bridge->SubmitMouseMotion(Value, 0.0f);
}

void AReimPlayerController::OnMouseY(float Value)
{
	if (!bCaptured || bMenuOpen) return;
	if (UReimBridgeSubsystem* Bridge = GetBridge()) Bridge->SubmitMouseMotion(0.0f, Value);
}

void AReimPlayerController::HandleMenuKey(const FKey& Key)
{
	UReimHostSettingsSubsystem* Settings = GetHostSettings();
	if (Settings == nullptr) return;

	if (Key == EKeys::R)
	{
		CloseMenu();
	}
	else if (Key == EKeys::Q)
	{
		Settings->SaveToInstance();
		UE_LOG(LogREIM, Display, TEXT("exit requested from menu"));
		FPlatformMisc::RequestExitWithStatus(false, 0);
	}
	else if (Key == EKeys::V)
	{
		Settings->SetVSync(!Settings->GetVSync());
		GEngine->Exec(GetWorld(), *FString::Printf(TEXT("r.VSync %d"),
		                                           Settings->GetVSync() ? 1 : 0));
	}
	else if (Key == EKeys::I)
	{
		Settings->SetInvertY(!Settings->GetInvertY());
	}
	else if (Key == EKeys::F)
	{
		const int32 Next = Settings->GetFrameRateLimit() == 0 ? 60
		                  : Settings->GetFrameRateLimit() == 60 ? 120 : 0;
		Settings->SetFrameRateLimit(Next);
		GEngine->Exec(GetWorld(), *FString::Printf(TEXT("t.MaxFPS %d"), Next));
	}
	else if (Key == EKeys::LeftBracket)
	{
		Settings->RequestRenderDistance(Settings->GetPendingRenderDistance() > 1
		                                    ? Settings->GetPendingRenderDistance() - 1
		                                    : 1);
	}
	else if (Key == EKeys::RightBracket)
	{
		Settings->RequestRenderDistance(Settings->GetPendingRenderDistance() + 1);
	}
	else if (Key == EKeys::S)
	{
		Settings->SaveToInstance();
	}
}

void AReimPlayerController::ToggleMenu()
{
	if (bMenuOpen)
	{
		CloseMenu();
	}
	else
	{
		OpenMenu();
	}
}

void AReimPlayerController::OpenMenu()
{
	if (bMenuOpen) return;
	bMenuOpen = true;
	ReleaseMouse();
	ReleaseGameplayKeys();
	if (!bPaused) TogglePause();
	SetInputMode(FInputModeGameAndUI());
	bShowMouseCursor = true;
	UE_LOG(LogREIM, Display, TEXT("menu opened (simulation paused)"));
}

void AReimPlayerController::CloseMenu()
{
	if (!bMenuOpen) return;
	bMenuOpen = false;
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
	bCaptured = true;
	if (bPaused) TogglePause();
	UE_LOG(LogREIM, Display, TEXT("menu closed (simulation resumed)"));
}

void AReimPlayerController::ToggleHud()
{
	if (UReimHostSettingsSubsystem* Settings = GetHostSettings())
	{
		Settings->SetShowHud(!Settings->GetShowHud());
	}
}

void AReimPlayerController::TogglePause()
{
	UReimBridgeSubsystem* Bridge = GetBridge();
	if (Bridge == nullptr) return;
	bPaused = !bPaused;
	Bridge->SetSimulationPaused(bPaused);
}

void AReimPlayerController::CaptureMouse()
{
	bCaptured = true;
	bShowMouseCursor = false;
	SetInputMode(FInputModeGameOnly());
}

void AReimPlayerController::ReleaseMouse()
{
	bCaptured = false;
	bShowMouseCursor = true;
	SetInputMode(FInputModeGameAndUI());
}

void AReimPlayerController::ReleaseGameplayKeys()
{
	UReimBridgeSubsystem* Bridge = GetBridge();
	if (Bridge == nullptr) return;
	for (int32 Key : {REIM_KEY_W, REIM_KEY_A, REIM_KEY_S, REIM_KEY_D, REIM_KEY_SPACE,
	                  REIM_KEY_LEFT_SHIFT, REIM_KEY_LEFT_CONTROL})
	{
		Bridge->SubmitKey(Key, false);
	}
}

void AReimPlayerController::UpdateAutoWalk()
{
	UReimBridgeSubsystem* Bridge = GetBridge();
	if (Bridge == nullptr || bAutoWalkFinished) return;
	if (AutoWalkTicks == 0 && AutoWalkSeconds <= 0.0f) return;

	TSharedPtr<FReimFramePacket, ESPMode::ThreadSafe> Packet = Bridge->GetLatestFrame();
	if (!Packet.IsValid()) return;
	const ReimRenderFrame& Frame = Packet->Frame;
	bWorldReady = Frame.ready != 0;
	if (!bWorldReady) return;

	if (!bAutoWalkStarted)
	{
		bAutoWalkStarted = true;
		AutoWalkReadyTick = Frame.tick;
		AutoWalkElapsed = 0.0;
		Bridge->SubmitKey(REIM_KEY_W, true);
		UE_LOG(LogREIM, Display, TEXT("auto-walk started at tick %llu"), Frame.tick);
		return;
	}

	AutoWalkElapsed += GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0;
	const bool bByTicks = AutoWalkTicks > 0 && Frame.tick >= AutoWalkReadyTick + AutoWalkTicks;
	const bool bBySeconds = AutoWalkTicks == 0 && AutoWalkElapsed >= AutoWalkSeconds;
	if (bByTicks || bBySeconds)
	{
		bAutoWalkFinished = true;
		Bridge->SubmitKey(REIM_KEY_W, false);
		UE_LOG(LogREIM, Display, TEXT("auto-walk stopped at tick %llu"), Frame.tick);
	}
}

void AReimPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateAutoWalk();
}
