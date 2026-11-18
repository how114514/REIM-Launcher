# HTCL M1 执行计划 — C++ Native Gameplay Preview

> 基线：Minecraft Java Edition 26.3 / Windows x64。  
> 本文是 `ROADMAP.md` 第一阶段（M1）的执行级拆分，供新对话直接实施；与 `ROADMAP.md`、`ARCHITECTURE.md` 冲突时以两者为准。  
> 本文不提供日期或工期承诺；完成与否只看验收条件与门禁。

---

## 0. 一页结论

**M1 的产物**

- `mc_26.3.dll`：C++ Native Runtime，读取真实 26.3 存档、维护世界/玩家状态、固定 20 TPS，并通过版本化 C ABI 输出渲染快照与区块网格。
- `HTCLPreview.exe`：临时 OpenGL Host，只负责窗口、输入、上传/绘制 Runtime 生成的网格。
- `htcl-convert.exe` + `htcl-analyze.jar`：M1 阶段的 Converter 最小工具链（摄取/分析/数据提取/资源准备）。
- 可重复的构建、运行、数据对照与验收记录。

**M1 不做**

- Unreal Engine、UE Shipping、Launcher 正式 UI。
- 完整游戏玩法、多人、Mod、存档写入（M1 只读）。
- 通用 Java→C++ 编译器、完整反编译/翻译覆盖。
- 假世界、手写演示地形、预生成截图冒充成果。

**完成定义**

能在未启动 JVM 的情况下，用 C++ Runtime 打开用户自有 26.3 真实存档，显示实际区块/地形，第一人称移动/视角/基础碰撞可用，连续运行 10 分钟稳定；关键数据来自 Converter 真实产物。

---

## 1. 目标、边界与验收

### 1.1 目标

1. 用 C++ Runtime 读取、解析和维护真实 Minecraft 数据。
2. 加载至少一个真实 26.3 世界存档的主世界区域。
3. 显示实际区块与方块地形，不伪造世界。
4. 第一人称摄像机、基础移动、视角控制、重力与碰撞。
5. 固定 20 TPS 模拟与渲染 FPS 解耦。
6. 启动 Converter 工作线：文件摄取、版本/类依赖分析、数据提取、代表性样本转换/代码生成实验、Converter ↔ Runtime 基本接口。
7. 建立可复用到 M2 的 Rendering Contract 与 C ABI。

### 1.2 非目标

- 不要求 UE 渲染；`HTCLPreview` 是开发期临时 Host。
- 不要求方块交互、背包、合成、生物、战斗、世界生成、维度切换。
- 不要求完整资源包/模型/动画/天气/UI。
- 不要求写入存档；写回属于后续阶段。
- 不要求 Converter 覆盖全部类/库；只要求真实数据链路与代表性样本。
- 不要求 Launcher 产品 UI；开发用 CLI 启动即可。

### 1.3 验收（对应 ROADMAP M1）

1. 使用真实 26.3 存档启动，加载并显示实际区块与方块地形。
2. 第一人称相机可用，玩家可移动、转向、跳跃/下落，基础碰撞生效。
3. 运行时没有 `javaw.exe`、JVM 或 classpath。
4. 临时 Renderer 只读取 Runtime 快照；替换/关闭 Renderer 不改变世界状态。
5. 模拟固定 20 TPS，渲染 FPS 可独立波动。
6. 连续运行 10 分钟无崩溃，日志可定位加载失败原因。
7. 抽查区块方块数据与原始 NBT/存档一致。
8. Runtime 的关键世界/注册表/方块数据来自 Converter 真实产物；临时手写/模拟数据仅限调试，且可追踪、可替换。

**G1 门禁**：上述 1–8 全部满足才进入 M2；真实区块数据无法稳定加载时，先修数据管线，不得用演示世界替代。

---

## 2. 技术基线与关键决策

| 编号 | 决策 | 内容 |
|---|---|---|
| M1-D01 | 进程/边界 | `mc_26.3.dll` 导出 `htcl_get_api`；`HTCLPreview.exe` 作为 Host 加载 DLL；C ABI 只用于 Host ↔ Native DLL |
| M1-D02 | 渲染职责 | Runtime 生成区块网格/快照；Preview 只上传、绘制、做视锥剔除与 HUD，不重建世界、不计算游戏逻辑 |
| M1-D03 | 坐标系 | 右手系、Y 向上、1 单位 = 1 方块/米；+X 东、+Z 南；yaw 0 朝 +Z，pitch 正方向向下；单位/朝向写入 C ABI 头文件 |
| M1-D04 | 线程 | Runtime 内部模拟线程固定 20 TPS；Host 渲染线程按显示器刷新率 `fetch_frame`；输入经队列在下一 tick 应用 |
| M1-D05 | 世界范围 | 主世界、单一维度；至少加载出生点周围区域；支持按玩家移动流式加载/卸载区块 |
| M1-D06 | 数据来源 | 只读用户自有 26.3 文件与存档；不下载/不改写；不提交 Mojang 文件 |
| M1-D07 | 资源 | 方块纹理优先从用户自有 `client.jar` 提取并生成图集；缺失时用占位色，但地形数据必须真实 |
| M1-D08 | 渲染覆盖 | M1 只保证完整方块 + 水（半透明）+ 基础地形；楼梯/半砖/复杂模型可跳过或降级，不列为验收失败 |
| M1-D09 | Converter | M1 启动 Converter：摄取/分析/数据提取/样本转换/生成表；生成物带版本/hash；不手改生成代码 |
| M1-D10 | 存档 | M1 只读；不写回，避免半成品破坏用户世界 |
| M1-D11 | 第三方 | CMake FetchContent + 固定版本/hash；依赖进入 `third_party/` 或构建缓存，不散落系统路径 |
| M1-D12 | 异常/ABI | Runtime DLL 关闭 C++ 异常；错误码 + 日志回调；不抛异常跨 ABI |

### 2.1 运行时目标产物

| 目标 | 类型 | 说明 |
|---|---|---|
| `mc_26.3.dll` | SHARED | C++20、`/MD`、无 UE、无 JVM；导出 C ABI |
| `HTCLPreview.exe` | EXE | GLFW + OpenGL 3.3 Core；加载 DLL；开发期 Host |
| `htcl-convert.exe` | EXE | Converter 编排/摄取/提取/检查 |
| `htcl-analyze.jar` | JAR | JDK 25 + ASM；类/依赖分析；离线 |
| `m1-tests.exe` | EXE | Catch2 单元/集成测试（按需运行） |

---

## 3. 输入与前置

### 3.1 用户提供

- Minecraft Java Edition **26.3** 安装/版本目录（含 `26.3.json`、`client.jar`、`libraries`、`assets`）；
- 至少一个 **26.3 世界存档**路径（`.minecraft/saves/...`）；
- 可选：26.3 `server.jar`（运行数据生成器 `--reports`；若不可用，改用现有 reports 或后续补）。

### 3.2 工具链

- Windows 10/11 x64；
- Rider（IDE，不替代 MSVC）；
- VS 2022 Build Tools（MSVC 14.50 + Windows SDK 10.0.26100）；
- CMake 3.28+；
- JDK 25（Temurin）；
- Python 3.12；
- Git。

### 3.3 缺失处理

- 缺少 26.3 文件或存档时，**停止**并向用户询问路径；不得生成假世界、假存档或伪造区块数据。
- 缺少 `server.jar`/reports 时，优先让 Converter 从现有文件提取；仍不可行则记录为阻塞项，不静默降级为假数据。

---

## 4. 仓库布局（M1 子集）

```text
HTCL/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ runtime/
│  ├─ contract/                  # C ABI / 快照结构（纯 C 头文件）
│  │  ├─ htcl_abi.h
│  │  └─ htcl_render.h
│  ├─ mc-native/                 # C++ Runtime（生成 mc_26.3.dll）
│  │  ├─ core/                   # tick/world/player/chunk
│  │  ├─ data/                   # 生成表加载、注册表/方块定义
│  │  ├─ nbt/                    # NBT 读取
│  │  ├─ anvil/                  # region/level.dat
│  │  ├─ mesh/                   # section 网格生成
│  │  └─ input/                  # 输入队列
│  └─ preview/                   # 临时 OpenGL Host（HTCLPreview.exe）
│     ├─ main.cpp
│     ├─ gl_renderer.*
│     ├─ shader.*
│     └─ hud.*
├─ converter/
│  ├─ orchestrator/              # htcl-convert.exe
│  └─ java-analyzer/             # htcl-analyze.jar（JDK 25 + ASM）
├─ generated/                    # 生成物（默认 gitignore，不提交）
│  └─ 26.3/
├─ third_party/                  # 固定版本依赖/许可清单
├─ tests/                        # Catch2 + golden
├─ tools/                        # Python oracle/验收脚本
└─ docs/
```

**构建产物输出**

```text
build/m1/bin/mc_26.3.dll
build/m1/bin/HTCLPreview.exe
build/m1/bin/htcl-convert.exe
build/m1/bin/htcl-analyze.jar
```

---

## 5. 工作包

> 每个工作包完成后按 DoD 自检；不要跨包堆积未验证代码。

### M1-A — 工程启动

| 任务 | 内容 | DoD |
|---|---|---|
| A1 | 建立目录与顶层 CMake，C++20、MSVC、`/MD`；Runtime DLL 关闭异常 | `cmake --preset m1-debug` 配置成功 |
| A2 | `CMakePresets.json`：`m1-debug`、`m1-release`，Visual Studio 17 2022 / x64 | Rider 与 CLI 都能配置 |
| A3 | 固定第三方依赖：GLFW、glad、stb、nlohmann/json、spdlog、Catch2、zlib-ng | 空目标可编译链接 |
| A4 | 建立空目标：`mc_26_3`（输出 `mc_26.3.dll`）、`HTCLPreview`、`htcl-convert`、`m1-tests` | 全部产出 |
| A5 | 日志与错误码骨架；Windows UTF-8/UTF-16 转换 | 失败有可读日志 |
| A6 | `generated/`、`build/`、用户文件加入 `.gitignore` | 不误提交 |

**验收**：`HTCLPreview.exe` 能开一个空窗口并正常退出；`mc_26.3.dll` 能被 `LoadLibrary` + `GetProcAddress("htcl_get_api")`。

### M1-B — Converter 摄取与分析

| 任务 | 内容 | DoD |
|---|---|---|
| B1 | `htcl-convert prepare --minecraft-dir <dir> --version 26.3 --out work/26.3` | 解析 version JSON；校验 client.jar/libraries/assets 是否存在；输出输入清单 |
| B2 | `htcl-analyze.jar`：类清单、方法数、规范化字节码 hash、外部依赖 | 输出 `analysis/classes.json`、`analysis/deps.json` |
| B3 | 代表性样本转换实验：选 1–2 个小组件（如 NBT/集合/字符处理）做 C++ 生成或手写对照 | 产出样本 + 差异记录 |
| B4 | 数据提取：运行 26.3 数据生成器 `--reports`（若可用），或读取现有 reports | `generated/26.3/reports/` 含 blocks/registries 等 |
| B5 | 生成 `blocks.json`：方块名/属性/是否完整方块/生命周期 | Runtime 可据此建立方块表 |
| B6 | 生成 `manifest.json`：profile、输入 hash、工具版本、生成物列表 | Runtime 加载时校验版本 |
| B7 | Converter ↔ Runtime 基本接口：生成物路径/版本约定 | Runtime 拒绝不匹配 manifest |

**验收**：同一输入重复执行，生成物 hash 稳定；分析报告可读；不依赖 JVM 运行游戏。

### M1-C — NBT / Anvil / 世界数据

| 任务 | 内容 | DoD |
|---|---|---|
| C1 | 只读 NBT 解析（TAG_End…LongArray；大端；modified UTF-8） | 单元测试覆盖常见标签 |
| C2 | `level.dat`：spawn、player pos/rotation、world height、version | 能取得出生点/玩家初始位置 |
| C3 | Region `.mca`：8KB 头、chunk 偏移、zlib/gzip/无压缩、外部 chunk 处理策略 | 能枚举并解压区块 |
| C4 | Chunk/section：`block_states` 调色板 + packed long、biomes（可最小） | 能按坐标读取方块状态 |
| C5 | 世界数据模型：区块/区段/方块状态/光照（skylight/blocklight） | 内存中可查询/遍历 |
| C6 | `htcl-convert inspect-world --world <path> --out report.json` | 输出区块数/方块样本/chunk 统计 |
| C7 | Python oracle：独立 NBT/Anvil dump（zlib + struct） | 与 C++ 的方块样本逐点一致 |

**验收**：对真实存档抽查坐标的方块名/属性与 oracle 完全一致；损坏区块跳过并记录，不崩溃。

### M1-D — Runtime 核心（Headless）

| 任务 | 内容 | DoD |
|---|---|---|
| D1 | C ABI 头文件（M1 最小面）：create/destroy/start/stop/fetch_frame/submit_input/set_paused；与 E1 共同冻结 | `HTCLPreview` 能加载/校验版本 |
| D2 | 世界加载：出生点周围区块（半径可配置，默认 6） | 真实区块进入内存 |
| D3 | 方块访问/碰撞 AABB（0.6×1.8，眼高 1.62）、重力、跳跃 | Headless 玩家可站立/下落 |
| D4 | 固定 20 TPS 模拟线程、累加器、上限追帧 | tick 稳定；FPS 不影响 tick |
| D5 | 玩家/相机状态、yaw/pitch、input 队列 | 输入在下一 tick 应用 |
| D6 | 区块流式加载/卸载（按玩家位置） | 移动跨区块不崩 |
| D7 | Headless 模式：`--headless --ticks N --dump` | 无窗口可跑；输出状态/方块样本 |

**验收**：真实世界加载；1000 tick 无异常；玩家移动/碰撞正确；无 JVM。

### M1-E — 渲染快照与网格

| 任务 | 内容 | DoD |
|---|---|---|
| E1 | 定义 `HtclRenderFrame`：frame/tick/partial_tick、Camera、Player、SectionMeshUpdate、SectionRemove、Environment | 与 `ARCHITECTURE.md` §8.2/§8.3 一致 |
| E2 | Section 网格生成：16³、面剔除、邻区块边界、不透明/半透明分组 | 每个 section 输出顶点/索引/bounds/revision |
| E3 | 顶点格式：position/normal/uv/light/sprite/tint | Preview 可直接上传 |
| E4 | 纹理图集：从 client.jar 提取方块纹理，生成 `atlas.png` + `textures.json` | Runtime 能映射方块→sprite |
| E5 | 网格缓存/脏标记/重建 | 加载后一次生成；无每帧重建 |
| E6 | 快照生命周期/所有权：Runtime 持有到下次 fetch，Host 不释放 Runtime 内存 | 无悬垂指针/重复释放 |
| E7 | 头文件注释线程/单位/坐标/所有权契约 | 可据此实现 UE Bridge |

**验收**：Preview 能显示真实区块；关闭/替换 Preview 不影响 Runtime 世界；20 TPS 与渲染帧率独立。

### M1-F — 临时 OpenGL Host

| 任务 | 内容 | DoD |
|---|---|---|
| F1 | GLFW 窗口、OpenGL 3.3 Core、glad、VSync、关闭/ESC | 窗口稳定 |
| F2 | 加载 `mc_26.3.dll`、校验 ABI、create/start | 失败有诊断 |
| F3 | 每帧 `fetch_frame`；上传/更新/移除 section VBO/VAO | 区块可见 |
| F4 | Shader：方块纹理、方向光、雾、alpha（水） | 地形可辨认 |
| F5 | 视锥剔除、距离排序（半透明） | 基本性能 |
| F6 | HUD：FPS、TPS、坐标、区块数、绘制数 | 调试可读 |
| F7 | 鼠标捕获、键盘输入 → `submit_input` | 输入进入 Runtime |
| F8 | 第三人称调试/玩家 AABB（可切换） | 可确认玩家实体存在 |

**验收**：真实地形显示；相机跟随玩家；FPS 与 TPS 独立；无渲染器侧世界逻辑。

### M1-G — 玩家操作与相机

| 任务 | 内容 | DoD |
|---|---|---|
| G1 | WASD、空格、Shift、鼠标视角、灵敏度/反转配置 | 操作顺畅 |
| G2 | 第一人称相机、pitch 限制、eye height | 视角正确 |
| G3 | 碰撞：水平轴分离、地面检测、跳跃/下落 | 不穿墙/不掉出世界 |
| G4 | 区块流式加载与玩家移动配合 | 不因跨区块卡死 |
| G5 | 基础 HUD/日志：加载进度、错误原因 | 可定位问题 |

**验收**：可连续移动/观察 10 分钟；无穿模/崩溃。

### M1-H — 集成与验收

| 任务 | 内容 | DoD |
|---|---|---|
| H1 | 统一启动参数：`--minecraft-dir`、`--world`、`--generated`、`--render-distance` | 文档化命令可复现 |
| H2 | 错误路径：缺文件/版本不符/损坏区块/ABI 不匹配 | 明确错误，不崩溃 |
| H3 | 10 分钟 soak；日志轮转/关键统计 | 稳定 |
| H4 | 数据对照：Python oracle vs Runtime 方块样本 | 一致 |
| H5 | 验收脚本/清单（按需运行） | 产出验收报告 |
| H6 | 记录已知限制（非完整方块/模型、只读、单维度） | 不夸大成果 |

**验收**：ROADMAP M1 八条全部通过，形成 G1 结论。

---

## 6. 顺序与并行

```text
M1-A 工程启动
  ├─→ M1-B Converter 摄取/分析 ─┐
  ├─→ M1-C NBT/Anvil/世界数据 ──┤
  │                              ├─→ M1-D Runtime（Headless）
  └─→ E1 契约定义 ──────────────┘        │
                                         ├─→ M1-E 网格/快照
                                         │        │
                                         │        └─→ M1-F OpenGL Host
                                         │                 │
                                         └─────────────────┴─→ M1-G 操作/相机
                                                                  │
                                                                  └─→ M1-H 验收
```

**可并行**

- B（Converter）与 C（NBT/Anvil）可同时推进；B 的数据提取依赖 reports，C 只依赖存档。
- E1（契约）在 A 后即可开始，不必等 Runtime 全部完成。
- F（窗口/渲染骨架）可在 E 之前先做空窗口与绘制管线，但不得先造世界。
- Python oracle（C7）可与 C 并行，作为独立实现。

**关键路径**：A → C → D → E（网格）→ F → G → H。

---

## 7. C ABI 与快照（M1 最小面）

> 详细规则见 `ARCHITECTURE.md` §8；以下只列出 M1 必须实现的字段，避免过度设计。

### 7.1 导出

```c
#define HTCL_ABI_VERSION 1
HTCL_EXPORT const HtclRuntimeApi* htcl_get_api(uint32_t abi_version);
```

- `HtclRuntimeApi`：`create` / `destroy` / `start` / `stop` / `fetch_frame` / `submit_input` / `set_simulation_paused`。
- `HtclHostApi`：至少 `alloc` / `free` / `log`；其他回调后续阶段再加。
- 所有结构带 `struct_size`/`abi_version`；版本不匹配拒绝加载。
- 不传 C++ 对象/STL；Runtime 内部字符串不跨界；日志用 UTF-8。

### 7.2 帧与快照

```text
HtclRenderFrame
  ├─ frame_id, tick, partial_tick
  ├─ CameraSnapshot        // pos/rot/fov/dimension
  ├─ PlayerSnapshot        // pos/vel/rot/flags
  ├─ SectionMeshUpdate[]   // chunkX/Z, sectionY, revision, bounds, vertices, indices, material
  ├─ SectionRemove[]       // 卸载区块
  └─ EnvironmentSnapshot   // M1 可只含天空/雾颜色（可选）
```

- **SectionMeshUpdate**：M1 只要求不透明/半透明两种 material；顶点含 position/normal/uv/light/sprite/tint。
- **生命周期**：快照内指针归 Runtime 所有，Host 在下次 `fetch_frame` 前只读；Host 不释放。
- **线程**：`fetch_frame` 只在 Host 渲染线程；`submit_input` 任意线程；`start/stop` 在控制线程。
- **增量**：M1 可在首次加载时发送全部 section，之后只发送新增/变更/移除；无逐帧世界复制。

### 7.3 单位与坐标

- 1 单位 = 1 方块 = 1 米；Y 向上；右手系；+X 东、+Z 南。
- yaw 0 朝 +Z，顺时针增加；pitch 正向下；相机使用角度制，头文件写清楚。
- UE 在 M2 Bridge 中转换为 UE 坐标系/厘米，不在 Runtime 中混用。

---

## 8. Converter 与生成物（M1）

### 8.1 CLI（M1 目标接口）

```text
htcl-convert prepare  --minecraft-dir <dir> --version 26.3 --out work/26.3
htcl-convert analyze  --profile 26.3 --out work/26.3
htcl-convert extract  --profile 26.3 --out generated/26.3
htcl-convert inspect-world --world <path> --out report.json
```

### 8.2 生成物

| 文件 | 内容 |
|---|---|
| `manifest.json` | profile、ABI、输入 hash、工具版本、生成物列表 |
| `blocks.json` | 方块状态 → 属性/完整方块/碰撞/纹理 sprite/渲染分类 |
| `textures.json` | sprite 名称 → 图集 tile/uv |
| `atlas.png` | 方块纹理图集（M1 可只含测试世界出现方块） |
| `analysis/classes.json` | 类清单/方法数/字节码 hash |
| `analysis/deps.json` | 外部依赖/引用面 |
| `reports/*` | 数据生成器原始 reports（若有） |

### 8.3 规则

- 生成代码/表不手改；修改进入 Converter/Patch。
- 生成物默认不入库；通过 manifest 固定输入与版本。
- Runtime 启动时校验 manifest/profile/ABI；不匹配则报错。
- 不使用手写假表替代 Converter 产物；临时占位仅限调试并标记。

---

## 9. 测试与验收

### 9.1 自动/半自动

| 层级 | 内容 | 工具 |
|---|---|---|
| 单元 | NBT 标签、region 头、调色板、AABB、tick | Catch2 |
| 数据对照 | 真实区块方块样本 vs Python oracle | Python 3.12 |
| 集成 | Headless 加载世界、1000 tick、区块流式 | `m1-tests` / CLI |
| 渲染 | 截图、区块可见、20 TPS/FPS 独立 | Preview + 人工 |
| 稳定 | 10 分钟 soak | Preview |

### 9.2 验收命令（实现后提供）

```bat
cmake --preset m1-release
cmake --build --preset m1-release
build\m1\bin\HTCLPreview.exe --minecraft-dir <dir> --world <save> --generated generated\26.3
```

> 按用户规则，默认不主动运行完整验收/长时间 soak；由用户运行并反馈。

### 9.3 通过标准

- 数据对照：抽查坐标 100% 一致；
- 10 分钟无崩溃；
- 20 TPS 稳定，FPS 可波动；
- 无 `javaw.exe` / JVM / classpath；
- Renderer 关闭/替换不影响世界状态；
- Runtime 关键数据来自 `generated/26.3`。

---

## 10. 风险与缓解

| 风险 | 影响 | 缓解 |
|---|---|---|
| 26.3 数据生成器入口/格式与预期不同 | 无法生成注册表/方块表 | 先确认 `server.jar` 的 bundler 入口；不可用则解析现有 reports/客户端数据，并记录差距 |
| NBT/Anvil 边界情况（外部 chunk、压缩类型、调色板跨度） | 加载失败/方块错误 | 先支持单 region/常见压缩；坏块跳过并报错；oracle 对照 |
| 方块模型/纹理映射复杂 | 视觉差/方块缺失 | M1 只保证完整方块 + 水；非完整方块跳过/降级；图集按测试世界裁剪 |
| Runtime 网格生成性能 | 加载卡顿 | 限制半径；加载时批量生成；后续再做异步/worker |
| 契约过早/过晚定稿 | M2 返工 | M1-A 后先定 E1；按 ARCHITECTURE §8 校验；M2 前冻结 |
| 临时 Renderer 渗入游戏逻辑 | 两套实现/数据不一致 | Preview 只读快照；代码评审禁止世界修改 API |
| 使用假数据“先跑起来” | 验收失真 | G1 门禁：关键数据必须来自 Converter/真实存档 |
| MSVC/CMake/Rider 配置问题 | 无法构建 | 使用 CMake Presets；Rider 指向 MSVC 工具链；最小构建先跑通 |

---

## 11. 新对话启动说明

**先读**

1. `README.md`（产品边界）；
2. `docs/ROADMAP.md` §1–§3（M1 目标/门禁）；
3. `docs/ARCHITECTURE.md` §7.0–§7.2、§8（渲染方向/C ABI）；
4. `docs/TECH_STACK.md` §1–§5（选型边界）；
5. `docs/DEVELOPMENT.md` §3–§4（环境/构建）；
6. 本文。

**硬规则**

- 不启动 Java Edition/JVM；JDK 25 只用于离线 Converter/数据生成/oracle。
- 不使用假世界、假区块、预生成地形；必须读取真实 26.3 存档。
- 不先做 UE、Launcher UI、玩法系统；严格按 M1-A→H。
- 不手改 `generated/`；不提交 Mojang 文件/世界/生成物。
- C ABI 只用于 Host ↔ Native DLL；Preview 不反向控制世界。
- 所有文件使用 Windows 绝对路径（如 `C:\Users\...\HTCL\...`）。
- 每完成一个工作包按 DoD 自检；未过 G1 不进入 M2。

**第一步**

1. 确认用户提供的 26.3 目录、世界存档路径、`server.jar`/reports 是否可用；
2. 按 §12 选择本次会话（S1 = M1-A + M1-B；S2 = M1-C；S3 = M1-D + M1-E；S4 = M1-F + M1-G + M1-H）；
3. 从对应工作包开始，不跨区间抢做后续内容。

**结束条件**

- 交付 M1-A→H 的源码/构建脚本/生成工具；
- 提供构建与运行命令；
- 按 §1.3 输出验收结果与已知限制；
- 不把“未实际运行的测试”写成通过。

---

## 12. 会话拆分与交接（建议 4 个会话）

> 目标：在不让上下文爆掉的前提下，尽量减少对话次数。每个会话只做一个明确区间，结束必须留下可恢复的状态。

| 会话 | 工作包 | 估算 AI 时长 | 开始条件 | 结束交付 |
|---|---|---|---|---|
| **S1 基础与 Converter** | M1-A + M1-B | 7–12 小时 | 环境、26.3 文件 | 可构建空目标；Converter 摄取/分析/数据提取可用；空窗口 |
| **S2 真实世界数据** | M1-C | 8–12 小时 | S1 | NBT/Anvil/level.dat 可解析；方块样本与 Python oracle 一致；`inspect-world` 可用 |
| **S3 Runtime 与网格** | M1-D + M1-E | 12–20 小时 | S2 | Headless 世界/玩家/20 TPS；C ABI；区块网格与快照 |
| **S4 显示、操作与验收** | M1-F + M1-G + M1-H | 8–15 小时 | S3 | OpenGL 真实地形；第一人称移动/碰撞；G1 验收记录 |

**为什么是 4 个**

- 1–2 个会话会把数据、Runtime、渲染、验收挤在一起，上下文和调试都容易失控；
- 8 个工作包逐个开会话又太碎；
- 4 个会话按“数据 → Runtime → 显示 → 验收”切分，边界清楚，交接成本最低；
- 如果 S3 中途出现上下文告警，在 M1-E（网格/快照）完成后切出第 5 个会话，最多 5 个。

### 12.1 每个会话的开始提示

```text
读 docs/M1_PLAN.md，本次只做 Sx：<工作包列表>。
先读 §12 的交接状态和上一次交付；只读本区间相关代码。
允许构建/运行/快速冒烟并自行修复；不要扩展到其他 Sx 的工作包。
未实际运行的结果不要写成通过。
```

### 12.2 每个会话的结束条件

- 本区间工作包的 DoD 全部满足；
- 构建通过，快速冒烟通过；
- 更新 §12.3 的交接状态（只改状态，不改需求）；
- 留下：已完成内容、未完成项、已知问题、下个会话的第一条命令/文件；
- 不做本区间之外的“顺手重构”。

### 12.3 交接状态（新对话从这里继续）

| 会话 | 状态 | 已完成 | 已知问题 / 下一步 |
|---|---|---|---|
| S1 基础与 Converter | 未开始 | — | 先确认环境与 26.3 文件路径 |
| S2 真实世界数据 | 未开始 | — | 依赖 S1 的 Converter 产物 |
| S3 Runtime 与网格 | 未开始 | — | 依赖 S2 的世界数据 |
| S4 显示、操作与验收 | 未开始 | — | 依赖 S3 的 ABI/网格/G1 |

**状态写法**：`未开始` → `进行中` → `完成`；若阻塞写 `阻塞：<原因>`。只更新本表，不在这里写长篇总结。

### 12.4 会话边界（防止越界）

| 会话 | 只允许 | 明确不做 |
|---|---|---|
| S1 | 工程骨架、依赖、Converter 摄取/分析/数据提取 | UE、Runtime 渲染、玩法、Launcher UI |
| S2 | NBT/Anvil/level.dat/区块数据/oracle | OpenGL、玩家操作、网格 |
| S3 | Runtime、20 TPS、C ABI、网格/快照 | 正式 UI、完整玩法、Converter 扩展 |
| S4 | Preview、输入/相机/碰撞、验收/修复 | 新功能、Converter 扩展、UE |

**交接原则**：接口（`htcl_abi.h`、坐标/单位、线程模型、生成物格式）在 S1/S3 冻结；后续会话只实现/修复，不重新设计。若必须改，先在本表记录原因，再改文档与代码。
