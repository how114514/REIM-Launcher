# Third-party inputs (S1)

Everything is vendored or unpacked from `third_party/`; no dependency is
installed system-wide and the build does not use the network.

| Dependency | Version/source | License | SHA-256 |
|---|---|---|---|
| GLFW | 3.4 source tarball (codeload tag `3.4`) | zlib/libpng | `c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01` |
| nlohmann/json | v3.11.3 single header | MIT | `9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6` |
| stb_image | `nothings/stb` master snapshot (2026-11-19) | MIT / public domain | `deb4f18c9ca21b751a1323019031c94c0bf5ca1d2929ef5913cc56f00aab6e91` |
| stb_image_write | `nothings/stb` master snapshot (2026-11-19) | MIT / public domain | `a8e7f77f0ccfdc9bac09a71268b131931ff81aafca82c393aeb6aaba10d9e7ca` |
| Catch2 | v2.13.10 single header | BSL-1.0 | `3725c0f0a75f376a5005dde31ead0feb8f7da7507644c201b814443de8355170` |
| miniz | `richgel999/miniz` master snapshot (2026-11-19), split C sources/headers | MIT | files below |

miniz files:

- `miniz.c` `29a0c08c4610d20ab18381cbf18190f91b4b41d05c242376364756061b631f1d`
- `miniz_tdef.c` `7b3baaa59f50116eb3e72a23302c505b76cb01cc456672f7957a74d9e7b44047`
- `miniz_tinfl.c` `7113a08a7722d671ce89b8de61fe6d02d0cadca0ff274ba357bf2a9dc85745ca`
- `miniz_zip.c` `bea8946aee21dadedc82267dbd359b1a662969f622267a12f6cfb52f9ce7bc33`
- `miniz.h` `2b9c48355793988f0d12b3cef357e52b741d744385157b59cb422237ea226501`
- `miniz_common.h` `d0770cc478923e33bf22820fc51bbe22b6b99c3230ce4f5fc10eea3debc75623`
- `miniz_export.h` `95390c253bb345ba04946b7d5b3c4cf37dae893df971805a3ac0c184e9f4f681`
- `miniz_tdef.h` `123050e3c876519828d3934899e0b003ad13a98114919d94f1e06b4b9d02a925`
- `miniz_tinfl.h` `2c89615a4cbc5de4eac5b861051849392ec698264ee7dd399ea587c592cf68d1`
- `miniz_zip.h` `43d09867f79993a9ba1627cb1f3bbd78d3bf5ee0c91cd6e7527aa9ca1e3aed44`

## S1 deviations from docs/M1_PLAN.md A3

- **zlib-ng** is replaced by **miniz** (zlib-compatible deflate/inflate + ZIP
  reader). Both are fixed local inputs; miniz avoids a network fetch and a large
  Windows build in S1. The archive reader is only used by the offline Converter.
- **glad** is not needed for the S1 empty window. GLFW 3.4 provides the GL 3.3
  context and the Windows OpenGL entry points used by `REIMPreview` (clear and
  readback). A loader will be introduced with the M1-F renderer (S4).
- **spdlog** is replaced by the small `runtime/mc-native/util/log.*` sink: a
  thread-safe callback with a console/file sink. It is sufficient for the S1
  logging skeleton and avoids pulling a formatting dependency into the DLL.
- No Mojang binary is vendored. `client.jar`, libraries, assets and worlds stay
  in the user's Minecraft directory and are read-only inputs.
