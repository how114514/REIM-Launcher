#include "ReimHostSettingsSubsystem.h"

#include "ReimLog.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

void UReimHostSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FString Path;
	if (!FParse::Value(FCommandLine::Get(), TEXT("reim-instance="), Path) ||
	    Path.IsEmpty())
	{
		return;
	}
	InstancePath = FPaths::ConvertRelativePathToFull(Path);

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *InstancePath))
	{
		UE_LOG(LogREIM, Warning, TEXT("settings: cannot read %s"), *InstancePath);
		return;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogREIM, Warning, TEXT("settings: invalid JSON in %s"), *InstancePath);
		return;
	}

	double Number = 0.0;
	if (Root->TryGetNumberField(TEXT("mouse_sensitivity"), Number) && Number > 0.0)
	{
		MouseSensitivity = static_cast<float>(Number);
	}
	if (Root->TryGetNumberField(TEXT("render_distance"), Number) && Number >= 1.0)
	{
		RenderDistance = static_cast<uint32>(Number);
		PendingRenderDistance = RenderDistance;
	}
	Root->TryGetBoolField(TEXT("invert_y"), bInvertY);
	Root->TryGetBoolField(TEXT("vsync"), bVSync);
	Root->TryGetBoolField(TEXT("show_hud"), bShowHud);
	if (Root->TryGetNumberField(TEXT("frame_rate_limit"), Number))
	{
		FrameRateLimit = static_cast<int32>(FMath::Max(0.0, Number));
	}
	/* Command-line screenshot/automation runs may hide the HUD. */
	bShowHud = bShowHud && !FParse::Param(FCommandLine::Get(), TEXT("reim-nohud"));

	UE_LOG(LogREIM, Display,
	       TEXT("settings: sensitivity=%.3f invert=%d distance=%u vsync=%d maxfps=%d hud=%d"),
	       MouseSensitivity, bInvertY ? 1 : 0, RenderDistance, bVSync ? 1 : 0,
	       FrameRateLimit, bShowHud ? 1 : 0);

	if (FParse::Param(FCommandLine::Get(), TEXT("reim-test-save-settings")))
	{
		/* Automation hook (M2-D4): exercise the persistence path on a temp
		 * instance copy. Never used for normal runs. */
		SetMouseSensitivity(0.2f);
		SetInvertY(!bInvertY);
		SetVSync(!bVSync);
		SetFrameRateLimit(FrameRateLimit == 0 ? 60 : 0);
		RequestRenderDistance(RenderDistance < 16 ? RenderDistance + 1 : RenderDistance - 1);
		SaveToInstance();
	}
}

bool UReimHostSettingsSubsystem::IsRenderDistanceRestartPending() const
{
	return PendingRenderDistance != RenderDistance;
}

void UReimHostSettingsSubsystem::SetMouseSensitivity(float Value)
{
	const float Clamped = FMath::Clamp(Value, 0.02f, 2.0f);
	if (!FMath::IsNearlyEqual(Clamped, MouseSensitivity))
	{
		MouseSensitivity = Clamped;
		bDirty = true;
	}
}

void UReimHostSettingsSubsystem::SetInvertY(bool bValue)
{
	if (bInvertY != bValue)
	{
		bInvertY = bValue;
		bDirty = true;
	}
}

void UReimHostSettingsSubsystem::SetVSync(bool bValue)
{
	if (bVSync != bValue)
	{
		bVSync = bValue;
		bDirty = true;
	}
}

void UReimHostSettingsSubsystem::SetFrameRateLimit(int32 Value)
{
	const int32 Clamped = FMath::Max(0, Value);
	if (FrameRateLimit != Clamped)
	{
		FrameRateLimit = Clamped;
		bDirty = true;
	}
}

void UReimHostSettingsSubsystem::SetShowHud(bool bValue)
{
	if (bShowHud != bValue)
	{
		bShowHud = bValue;
		bDirty = true;
	}
}

void UReimHostSettingsSubsystem::RequestRenderDistance(uint32 Value)
{
	PendingRenderDistance = FMath::Clamp<uint32>(Value, 1, 16);
}

bool UReimHostSettingsSubsystem::SaveToInstance()
{
	if (InstancePath.IsEmpty())
	{
		UE_LOG(LogREIM, Warning, TEXT("settings: no instance.json; nothing to save"));
		return false;
	}

	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (FFileHelper::LoadFileToString(Text, *InstancePath))
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Root);
	}
	if (!Root.IsValid())
	{
		Root = MakeShared<FJsonObject>();
	}

	Root->SetNumberField(TEXT("mouse_sensitivity"), MouseSensitivity);
	Root->SetNumberField(TEXT("render_distance"), PendingRenderDistance);
	Root->SetBoolField(TEXT("invert_y"), bInvertY);
	Root->SetBoolField(TEXT("vsync"), bVSync);
	Root->SetNumberField(TEXT("frame_rate_limit"), FrameRateLimit);
	Root->SetBoolField(TEXT("show_hud"), bShowHud);

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		UE_LOG(LogREIM, Error, TEXT("settings: JSON serialization failed"));
		return false;
	}
	if (!FFileHelper::SaveStringToFile(Output, *InstancePath))
	{
		UE_LOG(LogREIM, Error, TEXT("settings: cannot write %s"), *InstancePath);
		return false;
	}
	bDirty = false;
	UE_LOG(LogREIM, Display,
	       TEXT("settings saved to %s (render_distance=%u applies on restart)"),
	       *InstancePath, PendingRenderDistance);
	return true;
}
