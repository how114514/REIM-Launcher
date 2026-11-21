/*
 * Host-owned frame packet (M2-B2/B6).
 *
 * fetch_frame returns one blob owned by the Host. The packet keeps the blob,
 * the Runtime handle and the frame id together; its destructor calls
 * release_frame on whichever thread drops the last reference (RT upload, or GT
 * when a newer frame replaces it). Blob memory must not be read after the
 * packet is destroyed.
 */
#pragma once

#include "CoreMinimal.h"

#include <atomic>

#include "reim_abi.h"

/** Shared Runtime lifetime guard. The flag goes false before destroy() so late
 *  packet destructors cannot call into a destroyed instance. */
struct FReimRuntimeHandle
{
	const ReimRuntimeApi* Api = nullptr;
	ReimInstance* Instance = nullptr;
	std::atomic<bool> Alive{false};
	/* M2-G3: frame blobs fetched vs returned, observable from GT. */
	std::atomic<uint64> Fetched{0};
	std::atomic<uint64> Released{0};
};

struct FReimFramePacket
{
	ReimRenderFrame Frame{};
	uint64 FrameId = 0;
	void* Blob = nullptr;
	TSharedPtr<FReimRuntimeHandle, ESPMode::ThreadSafe> Handle;

	~FReimFramePacket()
	{
		if (Blob != nullptr && Handle.IsValid())
		{
			if (Handle->Alive.load())
			{
				Handle->Api->release_frame(Handle->Instance, FrameId);
			}
			Handle->Released.fetch_add(1);
		}
	}
};
