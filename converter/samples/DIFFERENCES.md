# S1 representative translation experiments (M1-B / B3)

This note records two small components selected for the first Converter-to-
native sample pair. They are intentionally low-level and are reused by later
M1 work (palette decoding in S2, NBT strings in S2). No Mojang source code is
copied; only observable format semantics are implemented.

## 1. Palette bit storage

- Java (26.3): `net.minecraft.util.BitStorage` / `PackedIntegerArray`
  interface. The world format uses 4-plus bits per entry, stores values
  least-significant-bit first, and never lets one value cross a 64-bit word.
  `valuesPerLong = 64 / bits`, the last word's high bits are unused.
- Native: `runtime/mc-native/samples/packed_bits.{h,cpp}`.
- Differences:
  - Java uses signed `long` shifts; C++ casts to `uint64_t` before shifting so
    the result is defined for all values (Java's `>>>` semantics).
  - The C++ entry point validates word count and bit width and returns a
    boolean instead of throwing; Java would surface an
    `IndexOutOfBoundsException`/array allocation failure.
  - `bits_for_palette` returns at least 4 because the 26.3 world format floors
    at 4 bits per entry.
- Validation: `m1-tests` packs/unpacks 4096 entries for bit widths 4, 5, 6, 8
  and 12, including a 4096-entry palette that produces 5-bit storage.

## 2. Modified UTF-8 strings (NBT / DataInputStream)

- Java (26.3): NBT names and strings use `DataInput.readUTF` /
  `DataOutput.writeUTF`, i.e. Java modified UTF-8: NUL is encoded as `C0 80`
  and supplementary characters are two 3-byte UTF-16 surrogate sequences.
- Native: `runtime/mc-native/util/utf.cpp` (`modified_utf8_to_utf8`).
- Differences:
  - C++ output is standard UTF-8 (supplementary characters become 4 bytes),
    which is what the Runtime logs and JSON tables use.
  - Malformed bytes are replaced with `?` instead of aborting the document; the
    NBT reader still reports structural errors separately.
- Validation vectors (also in `m1-tests`):
  - `41 C0 80 42` -> `A\0B`
  - `ED A0 BD ED B8 80` (U+1F600 surrogate pair) -> `F0 9F 98 80`
  - `E4 BD A0 E5 A5 BD` -> standard UTF-8 for two CJK characters.

Both samples are handwritten native code with deterministic tests; the
Converter does not attempt a general Java-to-C++ translation in S1.
