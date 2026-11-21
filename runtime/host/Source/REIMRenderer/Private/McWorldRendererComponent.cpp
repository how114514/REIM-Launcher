#include "McWorldRendererComponent.h"

#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PrimitiveSceneProxy.h"
#include "RHI.h"
#include "RenderingThread.h"

#include "McRenderState.h"
#include "McWorldSceneProxy.h"
#include "ReimBridgeSubsystem.h"
#include "ReimFramePacket.h"
#include "ReimLog.h"

UMcWorldRendererComponent::UMcWorldRendererComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetVisibility(true, true);
	CastShadow = false;
	bCastDynamicShadow = false;
	bCastStaticShadow = false;
	bAffectDynamicIndirectLighting = false;
	bAffectDistanceFieldLighting = false;
	bUseAsOccluder = false;
	AlwaysLoadOnClient = true;
	AlwaysLoadOnServer = true;
	bSelectable = false;
}

void UMcWorldRendererComponent::OnRegister()
{
	Super::OnRegister();
}

void UMcWorldRendererComponent::OnUnregister()
{
	ClearWorld();
	Super::OnUnregister();
}

bool UMcWorldRendererComponent::InitializeResources(UReimBridgeSubsystem* Bridge)
{
	if (Bridge == nullptr)
	{
		UE_LOG(LogREIM, Error, TEXT("renderer: no Bridge subsystem"));
		return false;
	}
	if (RenderState.IsValid() == false)
	{
		RenderState = MakeShared<FMcRenderState, ESPMode::ThreadSafe>(GMaxRHIFeatureLevel);
	}

	const TArray<uint8>& AtlasPng = Bridge->GetAtlasPng();
	if (AtlasPng.Num() == 0)
	{
		UE_LOG(LogREIM, Error, TEXT("renderer: Bridge has no atlas asset"));
		return false;
	}

	/* R0 atlas sampling (M2-I2): one mip, nearest, clamp, never stream.
	 * ImportBufferAsTexture2D builds a mip chain; with a tile atlas the far
	 * mips average neighbouring tiles into grey, so build the transient
	 * texture directly from mip 0 instead. */
	FImage AtlasImage;
	if (!FImageUtils::DecompressImage(AtlasPng.GetData(), AtlasPng.Num(), AtlasImage) ||
	    AtlasImage.RawData.Num() == 0)
	{
		UE_LOG(LogREIM, Error, TEXT("renderer: atlas PNG decode failed"));
		return false;
	}
	AtlasTexture = UTexture2D::CreateTransient(AtlasImage.SizeX, AtlasImage.SizeY,
	                                           PF_B8G8R8A8);
	if (AtlasTexture == nullptr || AtlasTexture->GetPlatformData() == nullptr ||
	    AtlasTexture->GetPlatformData()->Mips.Num() == 0)
	{
		UE_LOG(LogREIM, Error, TEXT("renderer: atlas texture creation failed"));
		return false;
	}
	AtlasTexture->Filter = TF_Nearest;
	AtlasTexture->SRGB = true;
	AtlasTexture->NeverStream = true;
	AtlasTexture->AddressX = TA_Clamp;
	AtlasTexture->AddressY = TA_Clamp;
	{
		FTexture2DMipMap& Mip = AtlasTexture->GetPlatformData()->Mips[0];
		void* Dest = Mip.BulkData.Lock(LOCK_READ_WRITE);
		const int64 CopyBytes = FMath::Min<int64>(AtlasImage.RawData.Num(),
		                                          Mip.BulkData.GetBulkDataSize());
		FMemory::Memcpy(Dest, AtlasImage.RawData.GetData(), CopyBytes);
		Mip.BulkData.Unlock();
	}
	AtlasTexture->UpdateResource();

	OpaqueBase = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/R0/M_McBlock.M_McBlock"));
	TranslucentBase = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/R0/M_McBlockTranslucent.M_McBlockTranslucent"));
	if (OpaqueBase == nullptr)
	{
		OpaqueBase = UMaterial::GetDefaultMaterial(MD_Surface);
		UE_LOG(LogREIM, Error,
		       TEXT("renderer: /Game/R0/M_McBlock missing; using default material"));
	}
	if (TranslucentBase == nullptr)
	{
		TranslucentBase = UMaterial::GetDefaultMaterial(MD_Surface);
		UE_LOG(LogREIM, Error,
		       TEXT("renderer: /Game/R0/M_McBlockTranslucent missing; using default"));
	}

	OpaqueMaterial = UMaterialInstanceDynamic::Create(OpaqueBase, this);
	TranslucentMaterial = UMaterialInstanceDynamic::Create(TranslucentBase, this);
	OpaqueMaterial->SetTextureParameterValue(TEXT("Atlas"), AtlasTexture);
	TranslucentMaterial->SetTextureParameterValue(TEXT("Atlas"), AtlasTexture);

	UE_LOG(LogREIM, Display,
	       TEXT("renderer resources ready: atlas %dx%d mips=%d filter=%d addr=(%d,%d), "
	            "revision %llu"),
	       AtlasTexture->GetSizeX(), AtlasTexture->GetSizeY(),
	       AtlasTexture->GetPlatformData() ? AtlasTexture->GetPlatformData()->Mips.Num() : -1,
	       static_cast<int32>(AtlasTexture->Filter), static_cast<int32>(AtlasTexture->AddressX),
	       static_cast<int32>(AtlasTexture->AddressY), Bridge->GetAtlasRevision());

	MarkRenderStateDirty();
	return true;
}

FPrimitiveSceneProxy* UMcWorldRendererComponent::CreateSceneProxy()
{
	if (!RenderState.IsValid() || OpaqueMaterial == nullptr ||
	    TranslucentMaterial == nullptr)
	{
		UE_LOG(LogREIM, Warning,
		       TEXT("renderer: CreateSceneProxy before resources/materials were ready"));
		return nullptr;
	}
	UE_LOG(LogREIM, Display, TEXT("renderer: scene proxy created"));
	return new FMcWorldSceneProxy(this, RenderState,
	                              OpaqueMaterial->GetRenderProxy(),
	                              TranslucentMaterial->GetRenderProxy());
}

void UMcWorldRendererComponent::GetUsedMaterials(
	TArray<UMaterialInterface*>& OutMaterials, bool /*bGetDebugMaterials*/) const
{
	/* The scene proxy owns both MIDs directly; UE verifies every material
	 * proxy used by a primitive against this list. */
	if (OpaqueMaterial != nullptr) OutMaterials.Add(OpaqueMaterial);
	if (TranslucentMaterial != nullptr) OutMaterials.Add(TranslucentMaterial);
}

FBoxSphereBounds UMcWorldRendererComponent::CalcBounds(
	const FTransform& /*LocalToWorld*/) const
{
	/* R0 batches are already world-space and span whatever chunks are loaded;
	 * per-section culling happens in the scene proxy. A world-scale bound keeps
	 * the single proxy visible without a GT read of the RT section table. */
	const FVector Extent(1.0e8);
	return FBoxSphereBounds(FVector::ZeroVector, Extent, 1.0e8);
}

void UMcWorldRendererComponent::ApplyFrame(
	const TSharedPtr<FReimFramePacket, ESPMode::ThreadSafe>& Packet)
{
	if (!Packet.IsValid() || !RenderState.IsValid()) return;
	if (Packet->FrameId == LastUploadedFrameId) return;
	if (LastUploadedFrameId == 0)
	{
		UE_LOG(LogREIM, Display, TEXT("renderer: first frame upload (%u updates, %u removes)"),
		       Packet->Frame.update_count, Packet->Frame.remove_count);
	}
	LastUploadedFrameId = Packet->FrameId;
	EnqueuedFrameId.store(Packet->FrameId);

	const ReimRenderFrame& Frame = Packet->Frame;
	if (OpaqueMaterial != nullptr && TranslucentMaterial != nullptr)
	{
		const FLinearColor FogColor(
			((Frame.environment.fog_color >> 16) & 0xFF) / 255.0f,
			((Frame.environment.fog_color >> 8) & 0xFF) / 255.0f,
			(Frame.environment.fog_color & 0xFF) / 255.0f, 1.0f);
		const float FogStart = Frame.environment.fog_start * 100.0f;
		const float FogEnd = Frame.environment.fog_end * 100.0f;
		OpaqueMaterial->SetVectorParameterValue(TEXT("FogColor"), FogColor);
		OpaqueMaterial->SetScalarParameterValue(TEXT("FogStart"), FogStart);
		OpaqueMaterial->SetScalarParameterValue(TEXT("FogEnd"), FogEnd);
		TranslucentMaterial->SetVectorParameterValue(TEXT("FogColor"), FogColor);
		TranslucentMaterial->SetScalarParameterValue(TEXT("FogStart"), FogStart);
		TranslucentMaterial->SetScalarParameterValue(TEXT("FogEnd"), FogEnd);
	}

	TSharedPtr<FMcRenderState, ESPMode::ThreadSafe> State = RenderState;
	ENQUEUE_RENDER_COMMAND(ReimApplyFrame)(
		[State, Packet](FRHICommandListImmediate& RHICmdList)
		{
			State->ApplyFrame(RHICmdList, Packet->Frame);
		});
}

void UMcWorldRendererComponent::ClearWorld()
{
	if (RenderState.IsValid())
	{
		TSharedPtr<FMcRenderState, ESPMode::ThreadSafe> State = RenderState;
		RenderState.Reset();
		ENQUEUE_RENDER_COMMAND(ReimClearWorld)(
			[State](FRHICommandListImmediate& RHICmdList)
			{
				State->Clear(RHICmdList);
			});
	}
}

uint32 UMcWorldRendererComponent::GetDrawCalls() const
{
	return RenderState.IsValid() ? RenderState->StatDrawCalls.load() : 0;
}

uint32 UMcWorldRendererComponent::GetTriangles() const
{
	return RenderState.IsValid() ? RenderState->StatTriangles.load() : 0;
}

uint32 UMcWorldRendererComponent::GetVisibleMeshes() const
{
	return RenderState.IsValid() ? RenderState->StatVisibleMeshes.load() : 0;
}

uint32 UMcWorldRendererComponent::GetTotalMeshes() const
{
	return RenderState.IsValid() ? RenderState->StatTotalMeshes.load() : 0;
}

uint64 UMcWorldRendererComponent::GetUploadedFrames() const
{
	return RenderState.IsValid() ? RenderState->StatUploadedFrames.load() : 0;
}

uint64 UMcWorldRendererComponent::GetUploadedBytes() const
{
	return RenderState.IsValid() ? RenderState->StatUploadedBytes.load() : 0;
}

double UMcWorldRendererComponent::GetUploadMsLast() const
{
	return RenderState.IsValid() ? RenderState->StatUploadMsLast.load() : 0.0;
}

double UMcWorldRendererComponent::GetUploadMsAvg() const
{
	if (!RenderState.IsValid()) return 0.0;
	const uint64 Frames = RenderState->StatUploadedFrames.load();
	return Frames > 0 ? RenderState->StatUploadMsTotal.load() / Frames : 0.0;
}

double UMcWorldRendererComponent::GetUploadMsPeak() const
{
	return RenderState.IsValid() ? RenderState->StatUploadMsPeak.load() : 0.0;
}

uint64 UMcWorldRendererComponent::GetPendingUploads() const
{
	if (!RenderState.IsValid()) return 0;
	const uint64 Enqueued = EnqueuedFrameId.load();
	const uint64 Applied = RenderState->StatAppliedFrameId.load();
	return Enqueued > Applied ? Enqueued - Applied : 0;
}
