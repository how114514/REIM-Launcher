#include "ReimSha256.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"

namespace
{
	class FSha256
	{
	public:
		FSha256()
		{
			State[0] = 0x6a09e667u; State[1] = 0xbb67ae85u;
			State[2] = 0x3c6ef372u; State[3] = 0xa54ff53au;
			State[4] = 0x510e527fu; State[5] = 0x9b05688cu;
			State[6] = 0x1f83d9abu; State[7] = 0x5be0cd19u;
		}

		void Update(const uint8* Data, SIZE_T Size)
		{
			Total += static_cast<uint64>(Size);
			while (Size > 0)
			{
				const SIZE_T Take = FMath::Min<SIZE_T>(Size, 64 - BufferUsed);
				FMemory::Memcpy(Buffer + BufferUsed, Data, Take);
				BufferUsed += Take;
				Data += Take;
				Size -= Take;
				if (BufferUsed == 64)
				{
					Compress(Buffer);
					BufferUsed = 0;
				}
			}
		}

		void Finish(uint8 Out[32])
		{
			const uint64 Bits = Total * 8ull;
			const uint8 Pad = 0x80;
			Update(&Pad, 1);
			const uint8 Zero = 0;
			while (BufferUsed != 56) Update(&Zero, 1);
			uint8 Length[8];
			for (int i = 0; i < 8; ++i) Length[7 - i] = static_cast<uint8>(Bits >> (i * 8));
			Update(Length, 8);
			for (int i = 0; i < 8; ++i)
			{
				Out[i * 4 + 0] = static_cast<uint8>(State[i] >> 24);
				Out[i * 4 + 1] = static_cast<uint8>(State[i] >> 16);
				Out[i * 4 + 2] = static_cast<uint8>(State[i] >> 8);
				Out[i * 4 + 3] = static_cast<uint8>(State[i]);
			}
		}

	private:
		static uint32 Rotr(uint32 Value, int Count) { return (Value >> Count) | (Value << (32 - Count)); }

		void Compress(const uint8* Block)
		{
			static const uint32 K[64] = {
				0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
				0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
				0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
				0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
				0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
				0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
				0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
				0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
				0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
				0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
				0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};

			uint32 W[64];
			for (int i = 0; i < 16; ++i)
			{
				W[i] = (static_cast<uint32>(Block[i * 4]) << 24) |
				       (static_cast<uint32>(Block[i * 4 + 1]) << 16) |
				       (static_cast<uint32>(Block[i * 4 + 2]) << 8) |
				       static_cast<uint32>(Block[i * 4 + 3]);
			}
			for (int i = 16; i < 64; ++i)
			{
				const uint32 S0 = Rotr(W[i - 15], 7) ^ Rotr(W[i - 15], 18) ^ (W[i - 15] >> 3);
				const uint32 S1 = Rotr(W[i - 2], 17) ^ Rotr(W[i - 2], 19) ^ (W[i - 2] >> 10);
				W[i] = W[i - 16] + S0 + W[i - 7] + S1;
			}

			uint32 A = State[0], B = State[1], C = State[2], D = State[3];
			uint32 E = State[4], F = State[5], G = State[6], H = State[7];
			for (int i = 0; i < 64; ++i)
			{
				const uint32 S1 = Rotr(E, 6) ^ Rotr(E, 11) ^ Rotr(E, 25);
				const uint32 Ch = (E & F) ^ (~E & G);
				const uint32 T1 = H + S1 + Ch + K[i] + W[i];
				const uint32 S0 = Rotr(A, 2) ^ Rotr(A, 13) ^ Rotr(A, 22);
				const uint32 Maj = (A & B) ^ (A & C) ^ (B & C);
				const uint32 T2 = S0 + Maj;
				H = G; G = F; F = E; E = D + T1;
				D = C; C = B; B = A; A = T1 + T2;
			}
			State[0] += A; State[1] += B; State[2] += C; State[3] += D;
			State[4] += E; State[5] += F; State[6] += G; State[7] += H;
		}

		uint32 State[8];
		uint8 Buffer[64]{};
		SIZE_T BufferUsed = 0;
		uint64 Total = 0;
	};
}

namespace ReimSha256
{
	FString HashBytes(const uint8* Data, SIZE_T Size)
	{
		FSha256 Sha;
		if (Data != nullptr && Size > 0) Sha.Update(Data, Size);
		uint8 Digest[32];
		Sha.Finish(Digest);
		FString Out;
		Out.Reserve(64);
		for (int i = 0; i < 32; ++i) Out += FString::Printf(TEXT("%02x"), Digest[i]);
		return Out;
	}

	FString HashFile(const FString& Path, int64& OutSize)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path)) return FString();
		OutSize = Bytes.Num();
		return HashBytes(Bytes.GetData(), static_cast<SIZE_T>(Bytes.Num()));
	}
}
