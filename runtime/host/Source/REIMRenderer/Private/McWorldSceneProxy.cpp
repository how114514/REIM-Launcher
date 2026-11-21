#include "McWorldSceneProxy.h"

#include "Materials/MaterialRenderProxy.h"
#include "MeshBatch.h"
#include "SceneView.h"

FMcWorldSceneProxy::FMcWorldSceneProxy(
	UPrimitiveComponent* Component,
	TSharedPtr<FMcRenderState, ESPMode::ThreadSafe> InState,
	FMaterialRenderProxy* InOpaqueMaterial,
	FMaterialRenderProxy* InTranslucentMaterial)
	: FPrimitiveSceneProxy(Component)
	, State(MoveTemp(InState))
	, OpaqueMaterial(InOpaqueMaterial)
	, TranslucentMaterial(InTranslucentMaterial)
{
}

FMcWorldSceneProxy::~FMcWorldSceneProxy() = default;

SIZE_T FMcWorldSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer = 0;
	return reinterpret_cast<size_t>(&UniquePointer);
}

uint32 FMcWorldSceneProxy::GetMemoryFootprint() const
{
	return sizeof(*this) + GetAllocatedSize();
}

FPrimitiveViewRelevance FMcWorldSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Relevance;
	Relevance.bDrawRelevance = true;
	Relevance.bDynamicRelevance = true;
	Relevance.bStaticRelevance = false;
	Relevance.bRenderInMainPass = true;
	Relevance.bShadowRelevance = false;
	Relevance.bVelocityRelevance = false;
	return Relevance;
}

void FMcWorldSceneProxy::GetDynamicMeshElements(
	const TArray<const FSceneView*>& Views, const FSceneViewFamily& /*ViewFamily*/,
	uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	if (!State.IsValid()) return;
	State->StatDrawCalls.store(0);
	State->StatTriangles.store(0);
	State->StatVisibleMeshes.store(0);

	TArray<FMcSectionMesh*> Visible;
	Visible.Reserve(State->GetSections().Num());

	for (const auto& Pair : State->GetSections())
	{
		FMcSectionMesh* Mesh = Pair.Value.Get();
		if (Mesh == nullptr || Mesh->VertexFactory == nullptr ||
		    Mesh->IndexBuffer.Indices.Num() == 0)
		{
			continue;
		}
		// Section bounds are a cheap pre-pass; per-view frustum test follows.
		Visible.Add(Mesh);
	}

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		if ((VisibilityMap & (1u << ViewIndex)) == 0) continue;
		const FSceneView* View = Views[ViewIndex];
		if (View == nullptr) continue;

		TArray<FMcSectionMesh*> DrawList;
		DrawList.Reserve(Visible.Num());
		for (FMcSectionMesh* Mesh : Visible)
		{
			if (!View->ViewFrustum.IntersectBox(Mesh->Bounds.Origin,
			                                    Mesh->Bounds.BoxExtent))
			{
				continue;
			}
			DrawList.Add(Mesh);
		}

		const FVector ViewOrigin = View->ViewMatrices.GetViewOrigin();
		DrawList.Sort([&](const FMcSectionMesh& A, const FMcSectionMesh& B)
		{
			return FVector::DistSquared(A.Bounds.Origin, ViewOrigin) <
			       FVector::DistSquared(B.Bounds.Origin, ViewOrigin);
		});

		for (uint32 Pass = 0; Pass < 2; ++Pass)
		{
			const bool bTranslucentPass = Pass == 1;
			FMaterialRenderProxy* Material =
				bTranslucentPass ? TranslucentMaterial : OpaqueMaterial;
			if (Material == nullptr) continue;

			// Translucent back-to-front.
			if (bTranslucentPass)
			{
				DrawList.Sort([&](const FMcSectionMesh& A, const FMcSectionMesh& B)
				{
					return FVector::DistSquared(A.Bounds.Origin, ViewOrigin) >
					       FVector::DistSquared(B.Bounds.Origin, ViewOrigin);
				});
			}

			for (FMcSectionMesh* Mesh : DrawList)
			{
				if ((Mesh->Material == REIM_MATERIAL_TRANSPARENT) != bTranslucentPass)
				{
					continue;
				}

				FMeshBatch& Batch = Collector.AllocateMesh();
				Batch.VertexFactory = Mesh->VertexFactory.Get();
				Batch.MaterialRenderProxy = Material;
				Batch.Type = PT_TriangleList;
				Batch.DepthPriorityGroup = SDPG_World;
				Batch.MeshIdInPrimitive = 0;
				Batch.CastShadow = false;
				Batch.bUseForMaterial = true;
				Batch.bUseForDepthPass = !bTranslucentPass;
				Batch.bUseAsOccluder = false;
				Batch.bWireframe = false;
				/* R0 draws the opaque pass two-sided: converter-authored
				 * geometry (snow boxes, crosses) and the cube path disagree on
				 * UE's front-face convention after the MC→UE mirror, and a
				 * one-sided batch drops one of them. Depth testing still hides
				 * interior faces; S4 keeps this and records it as an R0 limit. */
				Batch.bDisableBackfaceCulling = true;
				Batch.ReverseCulling = false;

				Batch.Elements.Reset();
				FMeshBatchElement& Element = Batch.Elements.AddDefaulted_GetRef();
				Element.IndexBuffer = &Mesh->IndexBuffer;
				Element.FirstIndex = 0;
				Element.NumPrimitives = Mesh->IndexBuffer.Indices.Num() / 3;
				Element.MinVertexIndex = 0;
				Element.MaxVertexIndex = Mesh->NumVertices > 0
					? Mesh->NumVertices - 1
					: 0;
				Element.InstancedLODIndex = 0;

				Collector.AddMesh(ViewIndex, Batch);
				State->StatDrawCalls.fetch_add(1);
				State->StatTriangles.fetch_add(Element.NumPrimitives);
				State->StatVisibleMeshes.fetch_add(1);
			}
		}
	}

	State->StatDrawCalls.store(
		State->StatDrawCalls.load() / FMath::Max(Views.Num(), 1));
	State->StatTriangles.store(
		State->StatTriangles.load() / FMath::Max(Views.Num(), 1));
}
