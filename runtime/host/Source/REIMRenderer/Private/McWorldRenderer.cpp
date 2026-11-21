#include "McWorldRenderer.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include "McWorldRendererComponent.h"
#include "ReimBridgeSubsystem.h"
#include "ReimCoordinate.h"
#include "ReimFramePacket.h"
#include "ReimLog.h"

AMcWorldRenderer::AMcWorldRenderer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	bFindCameraComponentWhenViewTarget = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->bConstrainAspectRatio = false;
	Camera->SetFieldOfView(90.0f);
	Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
	Camera->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
	Camera->PostProcessSettings.AutoExposureBias = 0.0f;
	/* Manual exposure, not physical camera exposure: R0 must be deterministic
	 * and independent of the lens settings. */
	Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
	Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
	Camera->PostProcessSettings.MotionBlurAmount = 0.0f;

	WorldComponent = CreateDefaultSubobject<UMcWorldRendererComponent>(TEXT("World"));
	WorldComponent->SetupAttachment(Root);

	SkySphere = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkySphere"));
	SkySphere->SetupAttachment(Root);
	SkySphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkySphere->SetCastShadow(false);
	SkySphere->bUseAsOccluder = false;
	SkySphere->SetVisibility(true, true);

	PlayerProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlayerProxy"));
	PlayerProxy->SetupAttachment(Root);
	PlayerProxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlayerProxy->SetCastShadow(false);
	PlayerProxy->SetVisibility(false);

	AutoReceiveInput = EAutoReceiveInput::Player0;
	bThirdPerson = FParse::Param(FCommandLine::Get(), TEXT("reim-third-person"));
}

void AMcWorldRenderer::BeginPlay()
{
	Super::BeginPlay();
	if (GEngine != nullptr)
	{
		/* Contract near clip is 5 uu (0.05 m). */
		GEngine->Exec(GetWorld(), TEXT("r.SetNearClipPlane 5"));
	}
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		PC->SetViewTarget(this);
		EnableInput(PC);
		if (InputComponent != nullptr)
		{
			InputComponent->BindKey(EKeys::F5, IE_Pressed, this,
			                        &AMcWorldRenderer::ToggleThirdPerson);
		}
	}

	if (SkySphere != nullptr)
	{
		UStaticMesh* Sphere = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UMaterialInterface* SkyBase = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/R0/M_Sky.M_Sky"));
		if (Sphere != nullptr)
		{
			SkySphere->SetStaticMesh(Sphere);
			SkySphere->SetWorldScale3D(FVector(50000.0f));
			if (SkyBase != nullptr)
			{
				SkyMaterial = UMaterialInstanceDynamic::Create(SkyBase, this);
				SkySphere->SetMaterial(0, SkyMaterial);
			}
			else
			{
				UE_LOG(LogREIM, Error, TEXT("renderer: /Game/R0/M_Sky missing"));
				SkySphere->SetVisibility(false, true);
			}
		}
	}

	if (PlayerProxy != nullptr)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (Cube != nullptr)
		{
			PlayerProxy->SetStaticMesh(Cube);
			PlayerProxy->SetRelativeScale3D(FVector(0.6f, 0.6f, 1.8f));
			UMaterialInterface* PlayerBase = LoadObject<UMaterialInterface>(
				nullptr, TEXT("/Game/R0/M_DebugPlayer.M_DebugPlayer"));
			if (PlayerBase != nullptr)
			{
				PlayerProxy->SetMaterial(0, PlayerBase);
			}
		}
	}

	UGameInstance* GameInstance = GetGameInstance();
	UReimBridgeSubsystem* Bridge =
		GameInstance ? GameInstance->GetSubsystem<UReimBridgeSubsystem>() : nullptr;
	if (WorldComponent != nullptr)
	{
		/* Section vertices are published in world space. The scene proxy applies
		 * the primitive's LocalToWorld, so this component must stay at identity
		 * or every mesh is translated twice. */
		WorldComponent->SetAbsolute(false, false, false);
		WorldComponent->SetAbsolute(true, true, true);
		WorldComponent->SetWorldTransform(FTransform::Identity, false, nullptr,
		                                  ETeleportType::TeleportPhysics);
		if (!WorldComponent->InitializeResources(Bridge))
		{
			UE_LOG(LogREIM, Error, TEXT("R0 renderer resources failed to initialize"));
		}
	}
	else
	{
		UE_LOG(LogREIM, Error, TEXT("R0 world component missing"));
	}
}

void AMcWorldRenderer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (WorldComponent != nullptr)
	{
		WorldComponent->ClearWorld();
	}
	Super::EndPlay(EndPlayReason);
}

void AMcWorldRenderer::Tick(float /*DeltaSeconds*/)
{
	Super::Tick(0.0f);
	UpdateCameraFromFrame();
}

void AMcWorldRenderer::UpdateCameraFromFrame()
{
	UGameInstance* GameInstance = GetGameInstance();
	UReimBridgeSubsystem* Bridge =
		GameInstance ? GameInstance->GetSubsystem<UReimBridgeSubsystem>() : nullptr;
	if (Bridge == nullptr || Camera == nullptr) return;

	TSharedPtr<FReimFramePacket, ESPMode::ThreadSafe> Packet = Bridge->GetLatestFrame();
	if (!Packet.IsValid())
	{
		return;
	}

	if (WorldComponent != nullptr && Packet->FrameId != LastAppliedFrameId)
	{
		LastAppliedFrameId = Packet->FrameId;
		WorldComponent->ApplyFrame(Packet);
	}

	const ReimRenderFrame& Frame = Packet->Frame;
	const float Alpha = FMath::Clamp(Frame.tick_alpha, 0.0f, 1.0f);
	const reim::bridge::Vec3 Eye = reim::bridge::InterpolatePosition(
		reim::bridge::PositionMetresToUe(Frame.prev_camera.position.x,
		                                 Frame.prev_camera.position.y,
		                                 Frame.prev_camera.position.z),
		reim::bridge::PositionMetresToUe(Frame.camera.position.x,
		                                 Frame.camera.position.y,
		                                 Frame.camera.position.z),
		Alpha);

	float YawUe = 0.0f;
	float PitchUe = 0.0f;
	reim::bridge::LookToUe(Frame.camera.yaw, Frame.camera.pitch, YawUe, PitchUe);
	float PrevYawUe = 0.0f;
	float PrevPitchUe = 0.0f;
	reim::bridge::LookToUe(Frame.prev_camera.yaw, Frame.prev_camera.pitch, PrevYawUe,
	                       PrevPitchUe);
	const float Yaw = reim::bridge::InterpolateAngleDegrees(PrevYawUe, YawUe, Alpha);
	const float Pitch = reim::bridge::InterpolateLinear(PrevPitchUe, PitchUe, Alpha);

	const FRotator Rotation(Pitch, Yaw, 0.0f);
	FVector CameraLocation(Eye.x, Eye.y, Eye.z);
	if (bThirdPerson)
	{
		const FVector Forward = FRotationMatrix(Rotation).GetUnitAxis(EAxis::X);
		const float PullBack = FMath::Clamp(Frame.third_person_distance, 0.0f, 4.0f);
		CameraLocation -= Forward * (PullBack * 100.0f);
	}

	SetActorLocation(CameraLocation, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorRotation(Rotation, ETeleportType::TeleportPhysics);

	if (PlayerProxy != nullptr)
	{
		const bool bShowProxy = bThirdPerson && Frame.ready;
		PlayerProxy->SetVisibility(bShowProxy, true);
		if (bShowProxy)
		{
			const reim::bridge::Vec3 Feet = reim::bridge::PositionMetresToUe(
				Frame.player.position.x, Frame.player.position.y,
				Frame.player.position.z);
			PlayerProxy->SetWorldLocation(FVector(Feet.x, Feet.y, Feet.z + 90.0));
			PlayerProxy->SetWorldRotation(FRotator(0.0f, Yaw, 0.0f));
		}
	}

	static bool bLoggedCamera = false;
	if (!bLoggedCamera)
	{
		bLoggedCamera = true;
		UE_LOG(LogREIM, Display,
		       TEXT("camera actor %.1f %.1f %.1f rot %.1f %.1f; camera component %.1f %.1f %.1f"),
		       GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z, Yaw,
		       Pitch, Camera->GetComponentLocation().X, Camera->GetComponentLocation().Y,
		       Camera->GetComponentLocation().Z);
	}

	if (SkyMaterial != nullptr)
	{
		const FColor Sky(
			static_cast<uint8>((Frame.environment.sky_color >> 16) & 0xFF),
			static_cast<uint8>((Frame.environment.sky_color >> 8) & 0xFF),
			static_cast<uint8>(Frame.environment.sky_color & 0xFF), 255);
		SkyMaterial->SetVectorParameterValue(TEXT("SkyColor"), FLinearColor(Sky));
	}

	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		FVector2D ViewportSize;
		GEngine->GameViewport->GetViewportSize(ViewportSize);
		if (ViewportSize.Y > 1.0)
		{
			CachedAspect = static_cast<float>(ViewportSize.X / ViewportSize.Y);
		}
	}
	const double HorizontalFov = reim::bridge::VerticalToHorizontalFovDegrees(
		Frame.camera.fov_y_degrees, CachedAspect);
	Camera->SetFieldOfView(static_cast<float>(HorizontalFov));


}

void AMcWorldRenderer::ToggleThirdPerson()
{
	bThirdPerson = !bThirdPerson;
	UE_LOG(LogREIM, Display, TEXT("third person: %s"),
	       bThirdPerson ? TEXT("on") : TEXT("off"));
}
