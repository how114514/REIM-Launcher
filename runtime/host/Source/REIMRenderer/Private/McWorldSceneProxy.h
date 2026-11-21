#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"

#include "McRenderState.h"

class FMaterialRenderProxy;

/** One scene proxy for the whole world (M2-C1): a section table plus two
 *  materials, no per-section actors or components. */
class FMcWorldSceneProxy final : public FPrimitiveSceneProxy
{
public:
	FMcWorldSceneProxy(UPrimitiveComponent* Component,
	                   TSharedPtr<FMcRenderState, ESPMode::ThreadSafe> InState,
	                   FMaterialRenderProxy* InOpaqueMaterial,
	                   FMaterialRenderProxy* InTranslucentMaterial);

	virtual ~FMcWorldSceneProxy() override;

	virtual SIZE_T GetTypeHash() const override;
	virtual uint32 GetMemoryFootprint() const override;
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
	                                    const FSceneViewFamily& ViewFamily,
	                                    uint32 VisibilityMap,
	                                    FMeshElementCollector& Collector) const override;

private:
	TSharedPtr<FMcRenderState, ESPMode::ThreadSafe> State;
	FMaterialRenderProxy* OpaqueMaterial = nullptr;
	FMaterialRenderProxy* TranslucentMaterial = nullptr;
};
