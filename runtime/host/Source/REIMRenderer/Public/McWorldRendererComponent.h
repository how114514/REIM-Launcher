#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"

#include <atomic>

#include "McWorldRendererComponent.generated.h"

struct FReimFramePacket;
class FMcRenderState;
class FMcWorldSceneProxy;
class FPrimitiveSceneProxy;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UReimBridgeSubsystem;
class UTexture2D;

/** Consumes Bridge packets on the Game Thread and hands section deltas to the
 *  render thread (M2-C1/C2). The component holds no world logic. */
UCLASS(ClassGroup = (REIM), meta = (BlueprintSpawnableComponent))
class REIMRENDERER_API UMcWorldRendererComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	UMcWorldRendererComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	/** GT: creates the atlas texture and material instances. Must run before
	 *  frames are applied; the render proxy is rebuilt afterwards. */
	bool InitializeResources(UReimBridgeSubsystem* Bridge);

	void ApplyFrame(const TSharedPtr<FReimFramePacket, ESPMode::ThreadSafe>& Packet);
	void ClearWorld();

	uint32 GetDrawCalls() const;
	uint32 GetTriangles() const;
	uint32 GetVisibleMeshes() const;
	uint32 GetTotalMeshes() const;

	/* M2-G3 RT upload budget (RT -> GT atomics). */
	uint64 GetUploadedFrames() const;
	uint64 GetUploadedBytes() const;
	double GetUploadMsLast() const;
	double GetUploadMsAvg() const;
	double GetUploadMsPeak() const;
	uint64 GetPendingUploads() const;

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials,
	                              bool bGetDebugMaterials = false) const override;

private:
	TSharedPtr<FMcRenderState, ESPMode::ThreadSafe> RenderState;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> AtlasTexture;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> OpaqueBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> TranslucentBase;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> OpaqueMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TranslucentMaterial;

	uint64 LastUploadedFrameId = 0;
	std::atomic<uint64> EnqueuedFrameId{0};
};
