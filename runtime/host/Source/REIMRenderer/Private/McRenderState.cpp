#include "McRenderState.h"

#include "RHICommandList.h"

#include "ReimLog.h"

namespace
{
	FVector3f McPositionToUe(const float* Position)
	{
		// reim::bridge::PositionMetresToUe, kept local because this conversion
		// runs on the render thread and must match ReimCoordinate.h exactly.
		return FVector3f(Position[2] * 100.0f, -Position[0] * 100.0f,
		                 Position[1] * 100.0f);
	}

	FVector3f McNormalToUe(const float* Normal)
	{
		return FVector3f(Normal[2], -Normal[0], Normal[1]);
	}

	FBoxSphereBounds MakeBounds(const ReimBounds& Bounds)
	{
		const FVector Min(Bounds.min.z * 100.0, -Bounds.max.x * 100.0,
		                  Bounds.min.y * 100.0);
		const FVector Max(Bounds.max.z * 100.0, -Bounds.min.x * 100.0,
		                  Bounds.max.y * 100.0);
		return FBoxSphereBounds(FBox(Min, Max));
	}

	/* Preview works in display space: texel(sRGB) * tint(sRGB) * brightness.
	 * UE samples the atlas as sRGB (linear) and multiplies by vertex colour as
	 * linear data, so the baked tint have to be decoded to linear here for the
	 * sRGB output conversion to reproduce the Preview result. */
	float SrgbToLinear(float Value)
	{
		return Value <= 0.04045f
		           ? Value / 12.92f
		           : FMath::Pow((Value + 0.055f) / 1.055f, 2.4f);
	}

	uint8 EncodeLinear(float Value)
	{
		return static_cast<uint8>(
			FMath::Clamp(Value, 0.0f, 1.0f) * 255.0f + 0.5f);
	}

	/* Preview-equivalent baked lighting: no light layer (all zero) falls back
	 * to a fully lit directional surface, matching M1's known limitation. */
	FColor BakeVertexColor(const ReimVertex& Vertex, const FVector3f& Normal)
	{
		const uint32 Sky = (Vertex.light >> 8) & 0xFFu;
		const uint32 Block = Vertex.light & 0xFFu;
		float Light = FMath::Max(Sky, Block) / 15.0f;
		if (Sky == 0u && Block == 0u) Light = 1.0f;

		/* The Preview/Runtime sun direction is MC (0.40, 1.0, 0.30); bake with
		 * its UE-space image (x, -x, y) so up faces get the same 0.885 dot the
		 * Preview shader computes instead of 0.265. */
		const FVector3f Sun = FVector3f(0.30f, -0.40f, 1.0f).GetSafeNormal();
		const float Diffuse = FMath::Max(FVector3f::DotProduct(Normal, Sun), 0.0f);
		const float Brightness = (0.45f + 0.55f * Light) * (0.62f + 0.38f * Diffuse);

		const float R = static_cast<float>(Vertex.tint & 0xFFu) / 255.0f;
		const float G = static_cast<float>((Vertex.tint >> 8) & 0xFFu) / 255.0f;
		const float B = static_cast<float>((Vertex.tint >> 16) & 0xFFu) / 255.0f;
		const float A = static_cast<float>((Vertex.tint >> 24) & 0xFFu) / 255.0f;
		return FColor(EncodeLinear(SrgbToLinear(R * Brightness)),
		              EncodeLinear(SrgbToLinear(G * Brightness)),
		              EncodeLinear(SrgbToLinear(B * Brightness)), EncodeLinear(A));
	}
}

FMcSectionMesh::~FMcSectionMesh()
{
	if (VertexFactory.IsValid())
	{
		VertexFactory->ReleaseResource();
	}
	IndexBuffer.ReleaseResource();
	VertexBuffers.PositionVertexBuffer.ReleaseResource();
	VertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
	VertexBuffers.ColorVertexBuffer.ReleaseResource();
}

void FMcRenderState::RemoveSection(const ReimSectionRemove& Remove)
{
	for (uint32 Material = 0; Material < 2; ++Material)
	{
		const FMcSectionKey Key{Remove.chunk_x, Remove.chunk_z, Remove.section_y, Material};
		Sections.Remove(Key);
	}
}

void FMcRenderState::ApplyUpdate(FRHICommandListBase& RHICmdList,
                                 const ReimSectionMeshUpdate& Update)
{
	const FMcSectionKey Key{Update.chunk_x, Update.chunk_z, Update.section_y,
	                        Update.material};
	if (Update.vertex_count == 0 || Update.index_count == 0 ||
	    Update.vertices == nullptr || Update.indices == nullptr)
	{
		// Zero-count update replaces the material with nothing (cull/remesh).
		Sections.Remove(Key);
		return;
	}

	if (const TUniquePtr<FMcSectionMesh>* Existing = Sections.Find(Key))
	{
		if ((*Existing)->Revision > Update.revision)
		{
			return; // stale incremental update
		}
		Sections.Remove(Key);
	}

	TUniquePtr<FMcSectionMesh> Mesh = MakeUnique<FMcSectionMesh>();
	Mesh->Revision = Update.revision;
	Mesh->Material = Update.material;
	Mesh->NumVertices = Update.vertex_count;
	Mesh->Bounds = MakeBounds(Update.bounds);
	Mesh->VertexFactory = MakeUnique<FLocalVertexFactory>(
		FeatureLevel, "McSectionVertexFactory");

	TArray<FDynamicMeshVertex> Vertices;
	Vertices.Reserve(static_cast<int32>(Update.vertex_count));
	for (uint32 Index = 0; Index < Update.vertex_count; ++Index)
	{
		const ReimVertex& Source = Update.vertices[Index];
		const FVector3f Normal = McNormalToUe(Source.normal).GetSafeNormal();
		const FVector3f Reference =
			FMath::Abs(Normal.Z) < 0.9f ? FVector3f(0.0f, 0.0f, 1.0f)
			                            : FVector3f(1.0f, 0.0f, 0.0f);
		FVector3f TangentX = FVector3f::CrossProduct(Reference, Normal).GetSafeNormal();
		const FVector3f TangentY = FVector3f::CrossProduct(Normal, TangentX);

		const FColor Baked = BakeVertexColor(Source, Normal);
		FDynamicMeshVertex Vertex(McPositionToUe(Source.position), TangentX, Normal,
		                          FVector2f(Source.uv[0], Source.uv[1]), Baked);
		Vertex.TangentZ.Vector.W =
			GetBasisDeterminantSignByte(TangentX, TangentY, Normal);
		Vertices.Add(Vertex);
	}

	/* MC indices are authored for a right-handed frame; the axis swap mirrors
	 * one axis, so reverse each triangle to keep UE's front faces outward. */
	TArray<uint32> Indices;
	Indices.Reserve(static_cast<int32>(Update.index_count));
	for (uint32 Index = 0; Index + 2 < Update.index_count; Index += 3)
	{
		Indices.Add(Update.indices[Index]);
		Indices.Add(Update.indices[Index + 2]);
		Indices.Add(Update.indices[Index + 1]);
	}

	Mesh->VertexBuffers.InitFromDynamicVertex(
		RHICmdList, Mesh->VertexFactory.Get(), Vertices, 1, 0);

	/* Bind the per-vertex colour stream (R0's baked light/tint). The third
	 * argument is bInOverrideColorVertexBuffer: `true` prepares the factory for
	 * per-instance override colours (white here), which silently drops
	 * ColorVertexBuffer — grass tops and leaves lost their tint/light. */
	FLocalVertexFactory::FDataType Data;
	Mesh->VertexBuffers.InitComponentVF(Mesh->VertexFactory.Get(), 0, false, Data);
	Mesh->VertexFactory->SetData(RHICmdList, Data);

	if (Data.ColorComponent.VertexBuffer == nullptr && !bReportedMissingColorStream)
	{
		bReportedMissingColorStream = true;
		UE_LOG(LogREIM, Error,
		       TEXT("renderer: vertex colour stream not bound; tint/light will be lost"));
	}

	Mesh->IndexBuffer.Indices = MoveTemp(Indices);
	Mesh->IndexBuffer.InitResource(RHICmdList);

	Sections.Add(Key, MoveTemp(Mesh));
}

void FMcRenderState::ApplyFrame(FRHICommandListBase& RHICmdList,
                                const ReimRenderFrame& Frame)
{
	const double Start = FPlatformTime::Seconds();
	uint64 Bytes = 0;
	for (uint32 Index = 0; Index < Frame.remove_count; ++Index)
	{
		RemoveSection(Frame.removes[Index]);
	}
	for (uint32 Index = 0; Index < Frame.update_count; ++Index)
	{
		Bytes += static_cast<uint64>(Frame.updates[Index].vertex_count) * sizeof(ReimVertex);
		Bytes += static_cast<uint64>(Frame.updates[Index].index_count) * sizeof(uint32);
		ApplyUpdate(RHICmdList, Frame.updates[Index]);
	}
	StatTotalMeshes.store(static_cast<uint32>(Sections.Num()));

	const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;
	const double Previous = StatUploadMsTotal.load();
	StatUploadMsLast.store(Ms);
	StatUploadMsTotal.store(Previous + Ms);
	StatUploadedFrames.fetch_add(1);
	StatUploadedBytes.fetch_add(Bytes);
	double Peak = StatUploadMsPeak.load();
	while (Ms > Peak && !StatUploadMsPeak.compare_exchange_weak(Peak, Ms))
	{
	}
	StatAppliedFrameId.store(Frame.frame_id);
}

void FMcRenderState::Clear(FRHICommandListBase& RHICmdList)
{
	Sections.Empty();
	StatDrawCalls.store(0);
	StatTriangles.store(0);
	StatVisibleMeshes.store(0);
	StatTotalMeshes.store(0);
	StatAppliedFrameId.store(0);
}
