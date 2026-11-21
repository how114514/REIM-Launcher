/*
 * Render-thread section table for the R0 world renderer (M2-C2/C3).
 *
 * Everything in FMcRenderState is created, mutated and released on the render
 * thread. The Game Thread only enqueues commands with a shared reference; the
 * frame packet travels with the command so the Runtime blob is released only
 * after GPU upload (M2-D04).
 */
#pragma once

#include "CoreMinimal.h"

#include "DynamicMeshBuilder.h"
#include "LocalVertexFactory.h"
#include "StaticMeshResources.h"

#include <atomic>

#include "reim_render.h"

struct FMcSectionKey
{
	int32 ChunkX = 0;
	int32 ChunkZ = 0;
	int32 SectionY = 0;
	uint32 Material = 0;

	friend bool operator==(const FMcSectionKey& A, const FMcSectionKey& B)
	{
		return A.ChunkX == B.ChunkX && A.ChunkZ == B.ChunkZ &&
		       A.SectionY == B.SectionY && A.Material == B.Material;
	}
	friend uint32 GetTypeHash(const FMcSectionKey& Key)
	{
		uint32 Hash = ::GetTypeHash(Key.ChunkX);
		Hash = HashCombine(Hash, ::GetTypeHash(Key.ChunkZ));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.SectionY));
		Hash = HashCombine(Hash, ::GetTypeHash(Key.Material));
		return Hash;
	}
};

struct FMcSectionMesh
{
	FStaticMeshVertexBuffers VertexBuffers;
	FDynamicMeshIndexBuffer32 IndexBuffer;
	TUniquePtr<FLocalVertexFactory> VertexFactory;
	FBoxSphereBounds Bounds;
	uint32 Revision = 0;
	uint32 NumVertices = 0;
	uint32 Material = 0;

	~FMcSectionMesh();
};

/** Section table shared between UMcWorldRendererComponent (GT enqueue side) and
 *  FMcWorldSceneProxy (RT draw side). */
class FMcRenderState : public TSharedFromThis<FMcRenderState, ESPMode::ThreadSafe>
{
public:
	explicit FMcRenderState(ERHIFeatureLevel::Type InFeatureLevel)
		: FeatureLevel(InFeatureLevel)
	{
	}

	/** Applies one incremental frame: removes then updates in blob order. */
	void ApplyFrame(FRHICommandListBase& RHICmdList, const ReimRenderFrame& Frame);

	/** Releases every GPU resource. Render thread only. */
	void Clear(FRHICommandListBase& RHICmdList);

	const TMap<FMcSectionKey, TUniquePtr<FMcSectionMesh>>& GetSections() const
	{
		return Sections;
	}

	ERHIFeatureLevel::Type GetFeatureLevel() const { return FeatureLevel; }

	/* RT -> GT observable numbers for the debug HUD (M2-C7). */
	std::atomic<uint32> StatDrawCalls{0};
	std::atomic<uint32> StatTriangles{0};
	std::atomic<uint32> StatVisibleMeshes{0};
	std::atomic<uint32> StatTotalMeshes{0};

	/* M2-G3 upload budget: measured around ApplyFrame on the render thread. */
	std::atomic<uint64> StatAppliedFrameId{0};
	std::atomic<uint64> StatUploadedFrames{0};
	std::atomic<uint64> StatUploadedBytes{0};
	std::atomic<double> StatUploadMsLast{0.0};
	std::atomic<double> StatUploadMsTotal{0.0};
	std::atomic<double> StatUploadMsPeak{0.0};

private:
	void ApplyUpdate(FRHICommandListBase& RHICmdList, const ReimSectionMeshUpdate& Update);
	void RemoveSection(const ReimSectionRemove& Remove);

	ERHIFeatureLevel::Type FeatureLevel;
	TMap<FMcSectionKey, TUniquePtr<FMcSectionMesh>> Sections;
	bool bReportedMissingColorStream = false;
};
