#include "ReimHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"

#include "McWorldRenderer.h"
#include "McWorldRendererComponent.h"
#include "ReimBridgeSubsystem.h"
#include "ReimFramePacket.h"
#include "ReimHostSettingsSubsystem.h"
#include "ReimLog.h"
#include "ReimPlayerController.h"

void AReimHUD::DrawFatal()
{
	UGameInstance* GameInstance = GetGameInstance();
	UReimBridgeSubsystem* Bridge =
		GameInstance ? GameInstance->GetSubsystem<UReimBridgeSubsystem>() : nullptr;
	const FString Message = Bridge ? Bridge->GetLastError() : TEXT("unknown error");

	DrawRect(FLinearColor(0.25f, 0.02f, 0.02f, 0.9f), 0.0f, 0.0f,
	         Canvas->SizeX, Canvas->SizeY);
	DrawText(TEXT("REIM Runtime cannot start"), FLinearColor::White, 40.0f, 40.0f,
	         nullptr, 2.0f);
	DrawText(Message, FLinearColor(1.0f, 0.85f, 0.8f), 40.0f, 100.0f, nullptr, 1.2f);
	DrawText(TEXT("See Saved/Logs/REIM.log and the exit code; this is not a fake world."),
	         FLinearColor::White, 40.0f, 140.0f, nullptr, 1.1f);
}

void AReimHUD::DrawLoading(const ReimRenderFrame& Frame)
{
	DrawRect(FLinearColor(0.02f, 0.04f, 0.08f, 0.85f), 0.0f, 0.0f,
	         Canvas->SizeX, Canvas->SizeY);
	DrawText(TEXT("Loading Minecraft 26.3 world..."), FLinearColor::White, 40.0f, 40.0f,
	         nullptr, 1.8f);
	DrawText(FString::Printf(TEXT("%u%%  (%u/%u chunks)"), Frame.loading_percent,
	                         Frame.loaded_chunks, Frame.total_chunks),
	         FLinearColor(1.0f, 0.85f, 0.2f), 40.0f, 90.0f, nullptr, 1.4f);
	DrawText(TEXT("REIM R0  |  Minecraft 26.3 native runtime"), FLinearColor::White,
	         40.0f, 130.0f, nullptr, 1.1f);
}

void AReimHUD::DrawMenu(AReimPlayerController* PlayerController)
{
	UReimHostSettingsSubsystem* Settings =
		PlayerController ? PlayerController->GetHostSettings() : nullptr;
	if (Settings == nullptr) return;

	const float Width = FMath::Min(Canvas->SizeX - 80.0f, 720.0f);
	const float Height = 330.0f;
	const float X = (Canvas->SizeX - Width) * 0.5f;
	const float Y = (Canvas->SizeY - Height) * 0.5f;
	DrawRect(FLinearColor(0.02f, 0.03f, 0.05f, 0.92f), X, Y, Width, Height);
	DrawRect(FLinearColor(0.5f, 0.55f, 0.6f, 0.9f), X, Y, Width, 2.0f);

	float LineY = Y + 22.0f;
	const auto Line = [&](const FString& Text, const FLinearColor& Color, float Scale)
	{
		DrawText(Text, Color, X + 24.0f, LineY, nullptr, Scale);
		LineY += 30.0f * Scale;
	};
	Line(TEXT("REIM PAUSED"), FLinearColor::White, 1.6f);
	Line(TEXT("R  resume"), FLinearColor(0.85f, 0.9f, 1.0f), 1.15f);
	Line(TEXT("Q  save settings and exit"), FLinearColor(0.85f, 0.9f, 1.0f), 1.15f);
	Line(FString::Printf(TEXT("V  VSync: %s"), Settings->GetVSync() ? TEXT("ON") : TEXT("OFF")),
	     FLinearColor(0.75f, 0.85f, 0.75f), 1.15f);
	Line(FString::Printf(TEXT("F  frame limit: %d (0 = uncapped)"),
	                     Settings->GetFrameRateLimit()),
	     FLinearColor(0.75f, 0.85f, 0.75f), 1.15f);
	Line(FString::Printf(TEXT("I  invert Y: %s"),
	                     Settings->GetInvertY() ? TEXT("ON") : TEXT("OFF")),
	     FLinearColor(0.75f, 0.85f, 0.75f), 1.15f);
	Line(FString::Printf(TEXT("[ ]  render distance: %u -> %u"),
	                     Settings->GetRenderDistance(),
	                     Settings->GetPendingRenderDistance()),
	     Settings->IsRenderDistanceRestartPending()
	         ? FLinearColor(1.0f, 0.85f, 0.2f)
	         : FLinearColor(0.75f, 0.85f, 0.75f),
	     1.15f);
	if (Settings->IsRenderDistanceRestartPending())
	{
		Line(TEXT("     restart required for render distance to take effect"),
		     FLinearColor(1.0f, 0.85f, 0.2f), 1.0f);
	}
	Line(FString::Printf(TEXT("S  save settings (sensitivity %.2f applies on restart)"),
	                     Settings->GetMouseSensitivity()),
	     FLinearColor(0.7f, 0.7f, 0.7f), 1.0f);
	Line(TEXT("Esc  close menu"), FLinearColor(0.7f, 0.7f, 0.7f), 1.0f);
}

void AReimHUD::DrawHUD()
{
	Super::DrawHUD();
	if (Canvas == nullptr || GEngine == nullptr) return;

	UGameInstance* GameInstance = GetGameInstance();
	UReimBridgeSubsystem* Bridge =
		GameInstance ? GameInstance->GetSubsystem<UReimBridgeSubsystem>() : nullptr;
	if (Bridge == nullptr) return;

	AReimPlayerController* PlayerController =
		Cast<AReimPlayerController>(GetOwningPlayerController());
	UReimHostSettingsSubsystem* Settings =
		GameInstance ? GameInstance->GetSubsystem<UReimHostSettingsSubsystem>() : nullptr;

	if (Bridge->IsFatal())
	{
		DrawFatal();
		return;
	}
	if (PlayerController != nullptr && PlayerController->IsMenuOpen())
	{
		DrawMenu(PlayerController);
		return;
	}
	if (Settings != nullptr && !Settings->GetShowHud()) return;

	TSharedPtr<FReimFramePacket, ESPMode::ThreadSafe> Packet = Bridge->GetLatestFrame();
	if (!Packet.IsValid())
	{
		DrawText(TEXT("REIM: waiting for Runtime frame"), FLinearColor::White, 24.0f,
		         24.0f, nullptr, 1.15f);
		return;
	}

	const ReimRenderFrame& Frame = Packet->Frame;
	if (!Frame.ready)
	{
		DrawLoading(Frame);
		return;
	}

	float Y = 24.0f;
	const auto Line = [&](const FString& Text, const FLinearColor& Color)
	{
		DrawText(Text, Color, 24.0f, Y, nullptr, 1.15f);
		Y += 20.0f;
	};

	Line(FString::Printf(
		     TEXT("REIM R0  %.0f FPS  %.1f TPS  tick %llu%s"),
		     1.0f / FMath::Max(GetWorld()->GetDeltaSeconds(), 0.0001f),
		     Frame.measured_tps, Frame.tick,
		     (PlayerController != nullptr && PlayerController->IsSimulationPaused())
		         ? TEXT("  PAUSED")
		         : TEXT("")),
	     FLinearColor::White);
	Line(FString::Printf(TEXT("XYZ %.1f / %.1f / %.1f  yaw %.0f pitch %.0f"),
	                     Frame.player.position.x, Frame.player.position.y,
	                     Frame.player.position.z, Frame.player.yaw, Frame.player.pitch),
	     FLinearColor(0.85f, 0.9f, 1.0f));
	UMcWorldRendererComponent* WorldComponent = nullptr;
	for (TActorIterator<AMcWorldRenderer> It(GetWorld()); It; ++It)
	{
		WorldComponent = It->GetWorldComponent();
		break;
	}
	const uint32 DrawCalls = WorldComponent ? WorldComponent->GetDrawCalls() : 0;
	const uint32 Triangles = WorldComponent ? WorldComponent->GetTriangles() : 0;
	Line(FString::Printf(TEXT("chunks %u  sections %u  draws %u  tris %u  bridge %.2f ms"),
	                     Frame.loaded_chunks, Frame.mesh_sections, DrawCalls, Triangles,
	                     Bridge->GetGtFetchMs()),
	     FLinearColor(0.75f, 0.85f, 0.75f));
	Line(TEXT("click capture  |  Esc menu  |  F1 HUD  |  F3 pause  |  F5 third person"),
	     FLinearColor(0.6f, 0.65f, 0.7f));
}
