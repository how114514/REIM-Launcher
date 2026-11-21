#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "McWorldRenderer.generated.h"

class UCameraComponent;
class UMcWorldRendererComponent;
class UStaticMeshComponent;

/** One world-level renderer actor (M2-C1). It owns a single scene proxy and a
 *  camera driven by Runtime snapshots; it never queries or mutates the world. */
UCLASS()
class REIMRENDERER_API AMcWorldRenderer : public AActor
{
	GENERATED_BODY()

public:
	AMcWorldRenderer();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UCameraComponent* GetCamera() const { return Camera; }
	UMcWorldRendererComponent* GetWorldComponent() const { return WorldComponent; }

private:
	void UpdateCameraFromFrame();
	void ToggleThirdPerson();

	UPROPERTY(VisibleAnywhere, Category = "REIM")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "REIM")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "REIM")
	TObjectPtr<UMcWorldRendererComponent> WorldComponent;

	/** Sky-coloured inverted sphere; colour comes from EnvironmentSnapshot. */
	UPROPERTY(VisibleAnywhere, Category = "REIM")
	TObjectPtr<UStaticMeshComponent> SkySphere;

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> SkyMaterial;

	/** Debug-only player proxy (M2-C6): a 0.6 x 1.8 x 0.6 m cube, no skin. */
	UPROPERTY(VisibleAnywhere, Category = "REIM")
	TObjectPtr<UStaticMeshComponent> PlayerProxy;

	uint64 LastAppliedFrameId = 0;
	float CachedAspect = 16.0f / 9.0f;
	bool bThirdPerson = false;
};
