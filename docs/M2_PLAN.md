# REIM M2 执行计划 — UE Native Rendering（R0 + Shipping）

> 基线：Minecraft Java Edition 26.3 / Unreal Engine 5.8.4 / Windows x64。  
> 本文是 `ROADMAP.md` 第二阶段（M2）的执行级拆分，供新对话直接实施；与 `ROADMAP.md`、`ARCHITECTURE.md` 冲突时以两者为准。  
> 前置依据：`docs/M1_ACCEPTANCE.md`（M1 已按用户决定验收通过，含 10 分钟 soak 豁免）。  
> 本文不提供日期或工期承诺；完成与否只看验收条件与门禁。

---

## 0. 一页结论

**M2 的产物**

- `runtime/host/REIM.uproject`：UE 5.8.4 工程骨架，`REIMGame` / `REIMBridge` / `REIMRenderer` / `REIMUI` / `REIMInput` 模块。
- `REIMRuntime.exe`：UE Development 与 Shipping 两种构建；加载同一份 `mc_26.3.dll`。
- **Rendering Contract v2**（`runtime/contract/reim_abi.h`、`reim_render.h`）：帧所有权、插值状态、资产交付、运行时信息；与 UE Bridge 的线程/坐标/生命周期契约写死在头文件。
- **REIMBridge**：C ABI v2 适配层；唯一负责 DLL 加载、快照消费、主机内存块、MC→UE 坐标/单位转换、GT/RT 交接。
- **R0 UE Renderer**：区块几何、图集、基础光照/雾、玩家调试代理、摄像机；只消费 Contract，不做世界逻辑。
- **`mc_26.3.dll` ABI v2**：游戏逻辑仍是 M1 的同一套 Runtime；不因 UE 而分叉。
- **`REIMPreview.exe` ABI v2**：继续作为调试/回归 Host，与 UE 共用同一 DLL 和同一 Contract。
- **Converter v2 + UE 渲染集成**：`reim-convert package/assemble` 产出 `NativeVersion`（DLL + `native_version.json` + `ue_adapter.json` + 数据引用/实例模板）并可组合成 `NativeBuild`（UE Runtime + NativeVersion）；UE 渲染作为 **Native Adapter** 进入工具链，不做 Java 渲染翻译。
- **Shipping 包与干净机记录**：未安装 UE/JDK/MSVC/CMake 的 Windows 机器可启动并进入真实世界。
- 可重复的构建、打包、运行、双后端对照、截图与性能基线。

**M2 不做**

- R1/R2/R3 渲染覆盖；实体、方块实体、粒子、天气、完整 UI、音频、后处理、OIT。
- 新玩法、交互、背包、战斗、世界生成、维度切换、存档写入。
- 正式 Launcher UI、安装器、账号、更新通道。
- 通用 Java→C++ 编译器、JRT 扩张、全量转换覆盖。
- 把 Java 渲染管线/Shader/RenderType 翻译成 UE；UE 渲染是原生 Adapter，不是命令回放或第二套游戏实现。
- 原版命令流/OpenGL 回放、Blaze3D/LWJGL 兼容层。
- 公开分发 Shipping 包（M2 只做内部验收；分发前需法律评审）。

**完成定义**

在用户自有 26.3 真实存档上，同一份 `mc_26.3.dll` 同时被 `REIMPreview.exe` 与 `REIMRuntime.exe` 驱动；UE Shipping 包在未安装 UE 的干净 Windows 机器上启动并显示真实区块；20 TPS 模拟与渲染 FPS 独立；**UE 运行的是 Converter 工具链产出的 NativeBuild/NativeVersion（含 UE Adapter 描述与校验），而不是手工拼装的 DLL/工程**；临时 Renderer 保留且不形成第二套游戏实现；G2 通过。

> **S4 补充（2026-11-21）**：H6 首轮用户目视验收发现 UE 画面贴图颜色与原生 26.3 不符（草顶灰、云杉叶白、无烘焙光照）。图集确为用户 `client.jar` 的原生素材；根因在 UE 顶点色绑定与状态变体/非整方块覆盖，见 §15。S4（M2-I）只修视觉正确性，不改变 ABI。

---

## 1. 目标、边界与验收

### 1.1 目标

1. 用 UE 5.8.4 作为 **Native Runtime 的宿主与渲染后端**，显示 M1 的真实世界。
2. 实现并冻结 **Rendering Contract v2**：帧所有权、插值、资产交付、版本/能力校验。
3. 实现 **R0 最小 UE 渲染**：区块网格可见正确、图集/顶点光照/雾/透明可用，玩家/摄像机由 Runtime 数据驱动。
4. 实现 **REIMBridge**：DLL 加载、快照消费、GT/RT 交接、输入转发、暂停/退出；UE 侧不出现世界逻辑。
5. 实现 **Shipping 打包与干净机验证**：app-local VC++ 运行库、运行时 DLL/资产布局、日志/退出/错误诊断。
6. 保留 **临时 OpenGL Renderer** 作为调试/回归后端，并与 UE 跑同一存档、同一输入脚本、同一状态对照。
7. Converter 在 M2 做**必要扩展**：manifest/data v2、UE Adapter 描述、NativeVersion/NativeBuild 打包、可重复性与决策记录；UE 运行的是工具链产物，不是手工拼装。
8. 建立 M3 可复用的渲染/打包基线：R0 截图、性能数字、双后端语义对照、NativeBuild 组合、错误路径清单。

### 1.2 非目标

- 不实现 R1 的完整区块流式优化、实体/方块实体渲染、天空/时间/天气。
- 不实现完整 Native UI、HUD 以外的界面、音频。
- 不在 UE 中实现 Minecraft 世界、物理、方块查询、网格生成、存档解析或任何玩法规则。
- 不改变 M1 的只读世界、单维度、非完整方块跳过等已知限制。
- 不启动 Java Edition/JVM；JDK 仅用于离线 Converter/oracle。
- 不把 Development 包当发布包；不把 UE Editor 发给用户。
- 不承诺 DX11 完全等价；DX11 作为冒烟项，失败必须记录并列入 M4 前修复；DX12 为主验收路径。
- 不把 Java 渲染管线/Shader/RenderType 翻译成 UE；UE 渲染器是原生 Adapter，由 Converter 组合、版本化、校验、打包；不做 GL/命令回放。

### 1.3 验收（对应 ROADMAP M2）

1. 同一个 C++ Runtime 通过 UE 正常显示真实 Minecraft 世界。
2. 不依赖 Java Minecraft/JVM。
3. 20 TPS 模拟与渲染 FPS 解耦，帧率变化不影响世界 tick。
4. UE Shipping 包在**未安装 UE** 的干净 Windows 机器上启动并进入世界。
5. 临时 Renderer 仍可用于调试/回归，且不形成第二套游戏实现。
6. 启动、退出、崩溃日志可定位。
7. Runtime 使用的关键数据/生成代码来自 Converter 可重复产物，生成物版本与 ABI 一致。**M2 执行补充**：Converter 能产出/组合带 UE 渲染 Adapter 的 NativeVersion/NativeBuild；UE 加载并校验该产物，手工复制 DLL/工程不能通过。

**G2 门禁**

| 条件 | 通过标准 | 不通过时的处理 |
|---|---|---|
| 同 Runtime 双后端 | 同一 DLL/存档在 Preview 与 UE 中给出同一份世界状态（玩家/区块/方块样本一致，渲染差异有记录） | 修 Contract/Bridge，不复制游戏逻辑 |
| Converter 覆盖 | R0 数据/表/图集 + UE Adapter 集成包由 Converter 可重复产出；NativeVersion/NativeBuild 由工具链组合；Runtime/UE 拒绝不匹配 manifest | 补数据生成/Adapter 打包/校验，不回退到手写表或手工拼装 |
| R0 渲染 | 区块/玩家/摄像机可辨认；固定场景截图基线建立 | 缩小 R0 范围，优先保证数据链路 |
| Shipping 分发 | 干净机（无 UE）可启动并进入世界 | 修运行库/打包/ABI/加载路径，不留给 M4 |
| 性能/线程 | 20 TPS 与 FPS 独立、稳定；GT Bridge 有预算 | 调整快照/队列/线程模型，不在 GT 上跑模拟 |

**门禁规则**：1–7 与 G2 全部满足才进入 M3。项目所有者可显式豁免某一条，但必须在 `docs\M2_ACCEPTANCE.md` 记录“未执行 + 豁免”，不得写成实测通过。M1 的 10 分钟 soak 豁免不自动继承到 M2；M2 的稳定性项需单独记录。

---

## 2. 技术基线与关键决策

| 编号 | 决策 | 内容 |
|---|---|---|
| M2-D01 | 游戏权威 | 世界状态/物理/输入语义/光照等级仍由 C++ Runtime 决定；UE 只是宿主与渲染后端 |
| M2-D02 | 单一实现 | `mc_26.3.dll` 只有一份；Preview 与 UE 是两个 Host，不复制 Runtime、不复制网格/存档/物理 |
| M2-D03 | ABI v2 | 整份 Contract 版本化到 2；新增帧所有权、插值、资产交付、运行时信息；Preview/UE 同步重建，不再提供 v1 服务 |
| M2-D04 | 帧所有权 | Runtime 通过 `ReimHostApi::alloc` 分配**单块 host-owned blob**，Host 用完调用 `release_frame` 归还；Runtime 不保留 Host 指针，避免 UE RT 迟滞造成悬垂 |
| M2-D05 | 插值 | Runtime 发布 `prev/current` 相机与玩家 + `tick_alpha`；UE Game Thread 插值；section 更新仍按 tick |
| M2-D06 | 坐标转换 | 只在 `REIMBridge` 内做 MC↔UE 转换；Runtime 保持 MC 右手系/米；公式见 §2.1 |
| M2-D07 | 网格归属 | M2 继续使用 C++ Native mesher；UE 只上传/绘制；不在 UE 侧重写方块模型/网格生成 |
| M2-D08 | 材质/光照 | 自定义 VertexFactory + 两个材质（不透明/半透明）；顶点光照/色调/雾来自 Runtime；R0 关闭 Lumen/Nanite/自动曝光/TAA |
| M2-D09 | 资源交付 | 图集 PNG + 元数据通过 ABI `get_asset` 交给 Host；UE 不解析 blocks/textures，Bridge 只读 manifest/`native_version.json`/`ue_adapter.json` 做版本/能力校验；纹理只在 RT 上传 |
| M2-D10 | 线程 | DLL 内固定 20 TPS；GT 只 `fetch_frame`；RT 上传并释放帧块；GT 不阻塞模拟；DLL 不碰 UObject |
| M2-D11 | 构建 | DLL 用 CMake/MSVC C++20、`/MD`、关闭异常；UE 用 UBT；跨边界只用 C ABI/POD |
| M2-D12 | 打包 | UE Runtime 包只含 Runtime/Adapter/实例模板/配置；版本相关 DLL 由 NativeVersion 提供，`reim-convert assemble` 组合成 NativeBuild；generated manifest/图集属于用户数据；DLL 用绝对路径 `LoadLibraryExW` |
| M2-D13 | Converter 范围 | M2 做 R0 所需的数据/manifest v2 + UE Adapter 描述/集成包 + 可重复性；不扩张 JRT/翻译/全量覆盖 |
| M2-D14 | R0 范围 | 区块几何、图集、顶点光照/色调、雾、天空色、玩家调试代理、摄像机、加载进度、调试 HUD |
| M2-D15 | 平台 | Windows 10/11 x64；DX12 主验收；DX11 冒烟；DX11 若失败必须记为 M2 已知限制并列入 M4 前修复，不得悄悄删除 DX11 最低要求；不做 Linux/macOS/主机 |
| M2-D16 | 分发 | M2 Shipping 只用于内部/干净机验收；不分发 Mojang 资产；公开发布留给 M4 前法律评审 |
| M2-D17 | UE Adapter 归属 | UE 渲染器是 **Native Adapter**（G 路线），源码在 `runtime/host`；Converter pin 版本/hash 并写入 NativeVersion 描述（Adapter 二进制随 Runtime 包）；不从 Java 渲染翻译，不做 GL/命令回放 |
| M2-D18 | NativeBuild 组合 | `NativeBuild = UE Runtime 包（含 Adapter） + NativeVersion（含 DLL/描述）`；manifest 绑定 UE 5.8.4/Adapter/ABI 2/profile/数据 hash；Launcher 后续只安装/启动该组合 |
| M2-D19 | 构建编排 | UE Runtime 由 UBT/BuildCookRun（`m2-build.ps1`）构建；Converter 的 `package`/`assemble` 负责 pin/组合/校验/打包，不在 C++ 工具内替换 UBT 或从 CMake 编译 UE |

### 2.1 坐标系与单位转换（写死）

Runtime Contract（不可变）：

- 右手系，`+Y` 向上，`+X` 东，`+Z` 南；1 单位 = 1 方块 = 1 米。
- yaw 角度制，0 朝 `+Z`，从上往下看顺时针增加；pitch 正向下，范围 `[-90, 90]`。
- 相机 FOV 为**垂直** FOV。

UE（左手法，X 前、Y 右、Z 上，1 uu = 1 cm）：

```text
UE_X =  MC_Z * 100        // 南 = UE 前
UE_Y = -MC_X * 100        // 东 = UE 左，故西 = UE 右
UE_Z =  MC_Y * 100
yaw_ue   = yaw_mc         // 两者 0 均指向“前/南”，顺时针增加
pitch_ue = -pitch_mc      // UE 正 pitch 向上，MC 正 pitch 向下
roll_ue  = 0
```

- 该映射把 MC 右手系翻转为 UE 左手系；**顶点位置/法线在 RT 上传时转换**，三角形绕序按转换后的方向重新验证（截图检查背面剔除）。
- 相机 FOV：UE 的 `SetFieldOfView` 是水平 FOV；`REIMBridge` 用 `hFov = 2*atan(tan(vFov/2)*aspect)` 由 Runtime 的垂直 FOV 计算，不直接把 `fov_y_degrees` 塞给 UE。
- 近/远裁剪：Runtime 单位为米，UE 侧乘 100；R0 近裁 5 uu、远裁 `(render_distance+1)*1600` uu。
- 转换只允许出现在 `REIMBridge` 的集中函数中；`REIMRenderer` 不得再混用 MC 坐标。

### 2.2 线程与所有权一览

| 数据 | 创建者 | 使用者 | 释放者 |
|---|---|---|---|
| 世界/玩家/模拟状态 | DLL 模拟线程 | DLL 模拟线程 | DLL（destroy） |
| 帧快照 + 网格顶点/索引 | Runtime 在 `fetch_frame` 内调用 `host->alloc` | GT 读取；RT 上传 | Host 调用 `release_frame`，Runtime 调 `host->free` |
| 图集 PNG/元数据 | Runtime 资产缓存（只读） | GT 创建 transient 纹理；RT 上传 | Runtime（destroy）；UE 纹理由 UE 释放 |
| Section GPU 缓冲 | RT | Renderer | RT（remove/destroy） |
| UE SceneProxy | GT | RT | UE 生命周期 |
| 输入事件 | GT | DLL 模拟线程（下一 tick） | DLL 队列 |
| 日志 | DLL 任意线程 | Host 回调 | 调用期间有效 |

- `fetch_frame` 只允许 GT 单线程调用；`release_frame` 允许 GT/RT 任意线程；`submit_input` 任意线程。
- `alloc/free` 回调必须线程安全、不得回调 Runtime、不得抛异常。
- 帧块最多 4 个未释放；超出时 `fetch_frame` 返回 `REIM_ERR_BUSY`（不阻塞模拟），Host 记录并复用上一帧 GPU 状态。

### 2.3 运行时目标产物

| 目标 | 类型 | 说明 |
|---|---|---|
| `mc_26.3.dll` | SHARED | C++20、`/MD`、无 UE、无 JVM；导出 ABI v2 |
| `REIMPreview.exe` | EXE | GLFW + OpenGL 3.3 Core；M1 回归 Host；消费 ABI v2 |
| `REIMRuntime.exe` | EXE | UE 5.8.4 Development/Shipping；从 NativeBuild 启动并加载同一 DLL |
| `REIM.uproject` | 工程 | UE Adapter 工程；模块源码入库，`.uasset/.umap` 走 Git LFS |
| `REIMBridge` | UE 模块（打包由 `REIMRuntimeContent` 插件负责） | C ABI v2、坐标转换、帧块生命周期、运行时资产 |
| `REIMRenderer` | UE 模块 | R0 SceneProxy/VF/材质/可见性 |
| `reim-convert.exe` | EXE | manifest/data v2 + `package`/`assemble`（NativeVersion/NativeBuild）；M2 不新增运行时依赖 |
| `m1-tests.exe` | EXE | 保留既有 Catch2 测试目标（含 ABI v2 更新），不重命名 |

---

## 3. 输入与前置

### 3.1 开发机提供

- **Unreal Engine 5.8.4**（Epic 官方安装版即可，必须包含 UBT/RunUAT/Editor）；记录实际安装根目录与版本。安装最小集：Engine 核心 + Windows 平台；Editor symbols 可选；不需要 Starter Content/Templates/Engine Source/多平台。
- 环境基线：`UE_ROOT` 指向 UE 5.8.4；`UE-LocalDataCachePath=D:\UE-DDC`（避免 C: 被 DDC/Shader 缓存占满）；干净机另行确认。
- Visual Studio 2022（C++ 游戏开发工作负载）、MSVC 14.50、Windows SDK 10.0.26100。
- CMake 3.28+、Git、Git LFS、Python 3.12、JDK 25（仅 Converter/oracle）。
- 磁盘：UE 安装 + 工程 + 打包产物建议预留 150 GB；内存 32 GB 推荐。
- 至少一块支持 DX12/SM6 的 GPU 与驱动；DX11 用于冒烟。
- 用户自有 26.3 存档、`generated/26.3`、Minecraft 目录。
- UE Adapter 构建产物（`runtime/host` 的 UBT 构建/打包结果）与可 pin 的 adapter 版本信息；Converter 不生成该产物。
- **干净验收机**：Windows 10/11 x64，未安装 UE/Editor/JDK/MSVC/CMake/VS；有可用 DX12 GPU/驱动；或使用带 GPU 直通/独立显卡的物理机/VM。

### 3.2 当前环境检查（2026-11-20 实测）

- UE 5.8.4 已安装：`D:\Program Files\Epic Games\UE_5.8`；`Build.version` = 5.8.4；UBT/RunUAT/UnrealPak/UnrealInsights/UnrealEditor-Cmd 均存在；`RunUAT -help` 退出码 0。
- 用最小 C++ 工程实际构建 `EnvCheckEditor Win64 Development` **成功**；使用 MSVC 14.44.35207 + Windows SDK 10.0.26100.0。工程位于 `build/envcheck`（gitignore，不提交）。
- .NET Framework 4.8.1 Developer Pack 已安装（NetFxSDK 4.8.1）；这是 UE Editor 构建的前置，缺失时 UBT 会报 `Could not find NetFxSDK install dir`。
- 用户环境变量：`UE_ROOT=D:\Program Files\Epic Games\UE_5.8`，`UE-LocalDataCachePath=D:\UE-DDC`（原先被误设为字面量 `-LocalDataCachePath`，已修正）。
- 开发机已有 M1 的 CMake 4.4.4/MSVC/JDK 25/Python 3.12/Git LFS 与 `generated/26.3`。
- `generated/26.3`、26.3 存档、26.3 版本文件存在；GPU RTX 4060；D: 剩余约 225 GB，C: 约 50 GB。
- 干净验收机尚未确认；S3 之前必须由用户提供并记录。
- 仍建议在每个 UE 构建/运行脚本中显式设置 `UE-LocalDataCachePath=D:\UE-DDC`，避免新终端未继承用户变量。

### 3.3 缺失处理

- 缺少 UE 5.8.4：停止 UE 相关工作，向用户询问安装路径；不得用 UE 5.7/5.9 或自制渲染器替代 M2 验收。
- 缺少干净机：Shipping 验收标记为阻塞；不得用开发机“模拟干净机”写成通过。
- 缺少存档/生成物：停止并询问；不得用演示世界或手写表替代。
- UE 构建失败：保留日志、错误码、模块名；不得把“能打开 Editor”当作 R0 通过。

---

## 4. 仓库布局（M2 增量）

```text
REIM-Launcher/
├─ runtime/
│  ├─ contract/                  # ABI v2（现有头文件扩展）
│  │  ├─ reim_abi.h
│  │  └─ reim_render.h
│  ├─ mc-native/                 # 现有 C++ Runtime（不链接 UE）
│  ├─ preview/                   # 现有 OpenGL Host（升级到 ABI v2）
│  ├─ host/                      # 【新增】UE 5.8.4 工程
│  │  ├─ REIM.uproject
│  │  ├─ Config/
│  │  ├─ Content/Maps/R0.umap
│  │  ├─ Shaders/Private/McBlock.usf
│  │  ├─ Source/REIM.Target.cs
│  │  ├─ Source/REIMEditor.Target.cs
│  │  ├─ Source/REIMGame/
│  │  ├─ Source/REIMBridge/
│  │  ├─ Source/REIMRenderer/
│  │  ├─ Source/REIMUI/
│  │  ├─ Source/REIMInput/
│  │  └─ Plugins/REIMRuntimeContent/REIMRuntimeContent.uplugin
│  └─ headless/                  # 现有 headless Host（升级 ABI v2）
├─ converter/
│  ├─ adapters/ue/               # 【新增】UE Adapter 描述/模板/版本 pin（不复制 UE 工程）
│  └─ orchestrator/              # manifest/data v2；package/assemble
├─ generated/                    # 不入库；由 Converter 生成
├─ tests/                        # Catch2；新增 ABI v2/帧所有权测试
├─ tools/                        # 【新增】m2-*.ps1 / UE 自动化包装
├─ build/
│  ├─ m2/                        # CMake/UE 构建与报告
│  ├─ native-version/26.3/       # 【新增】Converter 产出的 NativeVersion
│  └─ native-build/26.3/         # 【新增】UE Runtime + NativeVersion 组合
└─ docs/
   ├─ M2_PLAN.md
   └─ M2_ACCEPTANCE.md           # 【新增】实测记录（未运行不写通过）
```

**源码管理**

- `.uasset`、`.umap`、二进制图集测试资产从 M2 起使用 **Git LFS**；`Shaders/*.usf`、`Config/*.ini`、`Source/*.cpp/.h` 保持文本。
- 不提交 `generated/`、用户存档、Mojang 文件、Shipping 包、Cooked 内容、PDB。
- UE 工程不把 Editor 自动生成的临时文件入库；`.gitignore` 增加 `runtime/host/Binaries/`、`Intermediate/`、`Saved/`、`DerivedDataCache/`。

### 4.1 构建产物

```text
build/m2/bin/mc_26.3.dll
build/m2/bin/REIMPreview.exe
build/m2/bin/reim-headless.exe
build/m2/bin/reim-convert.exe
runtime/host/Binaries/Win64/REIMRuntime.exe      # UBT 输出（不入库）
build/m2/ue/Development/REIMRuntime.exe          # m2-build.ps1 暂存副本（可选）
build/m2/package/Windows/REIMRuntime.exe         # BuildCookRun -archive（UE Runtime 包）
build/native-version/26.3/                       # reim-convert package 产物
build/native-build/26.3/                         # reim-convert assemble 产物（最终可启动组合）
```

### 4.2 实例配置（Host 读取，UE/Preview 共用语义）

M2 引入一个 Host 侧的 `instance.json`，避免 Shipping 依赖长命令行；CLI 可覆盖单个字段。

```json
{
  "world_path": "D:\\Minecraft\\.minecraft\\saves\\REIM-M1-26.3",
  "generated_path": "D:\\EngineeringDocuments\\REIM-Launcher\\generated\\26.3",
  "native_version_path": "D:\\EngineeringDocuments\\REIM-Launcher\\build\\native-version\\26.3",
  "minecraft_dir": "D:\\Minecraft\\.minecraft",
  "render_distance": 8,
  "mouse_sensitivity": 0.15,
  "invert_y": false,
  "render_level": 0,
  "log_dir": "D:\\EngineeringDocuments\\REIM-Launcher\\build\\m2\\logs"
}
```

- Runtime 只接受 `ReimInstanceConfig` 中的字段；JSON 解析在 Host 侧，不进入 DLL。
- `render_level` 在 M2 固定为 0（R0）；其他值拒绝。
- Shipping 包内自带默认 `instance.json` 模板；真实路径由用户/测试脚本提供，不写死开发机路径。

---

## 5. 工作包

> 每个工作包完成后按 DoD 自检；不要跨包堆积未验证代码。

### M2-A — UE 工程与前置

| 任务 | 内容 | DoD |
|---|---|---|
| A1 | 安装/定位 UE 5.8.4；验证 `Engine/Build/BatchFiles/RunUAT.bat`、Editor、UBT | 版本/路径写入 `M2_ACCEPTANCE.md` 环境表；空 C++ 工程能构建 |
| A2 | CMake 增加 `m2-debug`/`m2-release` 预设，输出 `build/m2`；ABI v2 头文件可编译 | `cmake --preset m2-release` + 构建通过；M1 构建预设仍可用（代码升级到 ABI v2） |
| A3 | 创建 `REIM.uproject` 与模块骨架；空地图 `R0.umap`；GameInstance/Subsystem 生命周期 | Development Editor 与 Game 均可启动空场景 |
| A4 | `REIMRuntimeContent` 插件用 `RuntimeDependencies` 暂存实例模板/配置；DLL 由 NativeVersion 提供，Development 用 NativeBuild 布局加载；manifest 与图集来自用户 `generated/` | Development 下按 NativeBuild 布局加载 DLL；manifest/Adapter 由实例配置校验 |
| A5 | Host 日志 ↔ DLL 日志回调；`Saved/Logs` 分类日志；崩溃上下文 | 日志含 ABI/profile/世界路径/DLL 版本；失败可定位 |
| A6 | `tools/m2-build.ps1`、`m2-smoke.ps1` 骨架 | 一条命令构建 CMake + UE 并产出可运行包 |
| A7 | `.gitignore`/Git LFS 规则；UE 临时目录排除 | 仓库不误提交 Binaries/Intermediate/Saved/生成物 |

**验收**：UE Development 启动空场景；Bridge 能 `LoadLibraryExW` 加载 ABI v2 DLL、`create`/`start`/`stop`；日志与退出码可读；尚未渲染世界。

### M2-B — Contract v2 与 Bridge 数据面

| 任务 | 内容 | DoD |
|---|---|---|
| B1 | ABI v2：版本号、`get_runtime_info`、`get_asset`、`release_frame`、配置扩展（`instance_dir`/`native_version_path`/`render_level`）、帧扩展（prev/current、tick_alpha、owned blob） | 头文件带 `struct_size`/版本注释/线程/单位；编译期布局断言 |
| B2 | Host-owned 帧块：Runtime 用 `alloc` 组装单块 blob（updates/removes/顶点/索引）；最多 4 块未释放；`release_frame` 释放 | Preview/headless 升级后 1000 tick 无泄漏；超限返回 `REIM_ERR_BUSY` 且模拟不停 |
| B3 | 插值数据：`prev_camera`/`prev_player`、`tick_alpha`；角度最短路径规则写入头文件 | 单元测试覆盖跨 0/360 yaw、pitch 钳制、tick 未推进时 alpha |
| B4 | 资产交付：`get_asset(REIM_ASSET_ATLAS_PNG / ATLAS_META)`；生命周期为实例存活期；revision | UE 能拿到 PNG/元数据；Preview 优先改用 `get_asset`，文件路径仅作诊断回退；不匹配 manifest 拒绝 |
| B5 | 运行时信息/能力：profile、DLL build id、ABI、feature flags、manifest hash；供 UE 侧校验 NativeVersion/Adapter | UE 启动时打印并校验；ABI/profile/Adapter 不匹配明确失败 |
| B6 | `REIMBridge`：加载/校验/create/start；GT `fetch_frame`；RT 上传后 `release_frame`（NullRHI 下在 GT 消费后释放）；输入队列；暂停；关闭顺序 | NullRHI 下能消费真实帧；无 UAF/泄漏；关闭顺序无挂起 |
| B7 | 坐标/单位转换工具与测试：位置/法线/yaw/pitch/FOV/裁剪面 | 已知坐标/朝向单测通过；截图验证三角形绕序/背面剔除 |
| B8 | 背压/预算：单帧 blob 上限 64 MB、update/remove 各 4096、指标（GT fetch ms、blob bytes、pending uploads） | 距离 6/8 加载不超过上限；超限返回明确错误并提示降低距离，不静默丢 section |

**验收**：UE（`-nullrhi`）跑 1000 tick；Preview 与 UE 共用 ABI v2 DLL；帧块所有权测试通过；无模拟阻塞。

### M2-C — R0 UE Renderer

| 任务 | 内容 | DoD |
|---|---|---|
| C1 | `AMcWorldRenderer` + `UMcWorldRendererComponent` + `FMcWorldSceneProxy`；一个世界级 Proxy，不按 section 建 Actor | 场景中只有 1–2 个渲染 Actor；section 表可增/改/删/清 |
| C2 | RT 上传：顶点 MC→UE 转换、VB/IB 创建/更新/销毁；零顶点 update 清除材质；SectionRemove 清整段 | 加载后网格可见；卸载/清空不残留；无 RT 读已释放帧块 |
| C3 | `FMeshBatch` 绘制：自定义 VertexFactory（position/normal/uv/light/tint）；不透明/半透明分组；section bounds 视锥剔除 | 每个可见 section 有 draw；背面剔除正确；剔除计数可见 |
| C4 | 材质/Shader：GT 解码图集 PNG（ImageWrapper/ImportBufferAsTexture2D）→ transient 纹理（nearest、无 mip）；顶点光照、方向光兜底、tint/alpha、距离雾；R0 固定太阳方向 | 真实草地/雪/树/水可辨认；无光照层时方向光兜底与 M1 表现一致 |
| C5 | 摄像机：GT 插值相机、垂直/水平 FOV 转换、近远裁；第三人称调试距离 | 1080p 下视角正确；转头/移动平滑；不穿地 |
| C6 | 玩家调试代理：由 PlayerSnapshot 驱动简单几何（F5 切换）；不加载皮肤/骨骼 | 第三人称可见玩家位置/尺寸；无玩法逻辑 |
| C7 | 加载/统计：`ready`/`loading_percent` 加载画面；UE `HighResShot`；HUD 显示 draw/tris/上传毫秒 | 首帧前显示加载；截图可复现；HUD 数字来自实际帧 |
| C8 | R0 渲染配置：关闭 Lumen/Nanite/VSM/自动曝光/TAA；固定分辨率/截图相机 | 截图稳定；同一存档多次截图差异可解释 |

**验收**：UE 中显示真实区块；与 Preview 同存档/同出生点画面语义一致；关闭/替换 Preview 不影响 UE 或 Runtime 状态；无 UE 侧世界逻辑。

### M2-D — UE Host：输入、UI、生命周期

| 任务 | 内容 | DoD |
|---|---|---|
| D1 | 输入采集：WASD/空格/Shift/Ctrl、鼠标增量、视角；映射到 `ReimInputEvent`；点击捕获、Esc 释放；自动输入/auto-walk 测试钩子 | 输入只经 ABI 进入 Runtime；GT 不直接改玩家/世界；双后端可用同一输入脚本 |
| D2 | 生命周期：GameInstance/Subsystem 启停顺序；关闭 UE 时 stop/flush/free；重启实例 | 连续启动/退出 20 次无崩溃/无泄漏 |
| D3 | 最小 UI：调试 HUD（FPS/TPS/坐标/区块/加载/Bridge 毫秒）；暂停/退出菜单（Shipping 可退出） | Shipping 下无控制台也能退出/改设置；菜单不进入 Runtime 玩法 |
| D4 | 设置：渲染距离、灵敏度、反转、VSync/帧率上限；写 `instance.json`/配置；渲染距离变更要求重启并提示 | 设置可持久化；不支持的变更明确提示 |
| D5 | 错误 UX：缺 DLL/manifest/ABI/profile/世界/图集、GPU device lost、DLL fatal | 对话框 + 日志 + 非零退出；不崩溃、不静默回退假世界 |
| D6 | 音频：M2 只预留事件边界，不实现播放；不引入资源解码 | 无音频依赖；不阻塞 R0 |

**验收**：UE 中第一人称可移动/转向/跳跃；Esc 菜单可退出；错误路径可定位；输入/UI 不包含游戏规则。

### M2-E — Converter 工具链：UE 渲染集成与打包

> 目标：让“Java Edition → C++ Runtime + UE 渲染”成为 Converter 的可重复产物，而不是 M2 手工拼装的一次性 UE 工程。UE 渲染器是 **Native Adapter**（G 路线），不从 Java 渲染翻译；Converter 负责 pin、组合、校验、打包。

| 任务 | 内容 | DoD |
|---|---|---|
| E1 | generated manifest v2（`generated/26.3/manifest.json`）：schema/ABI 2、profile、input hashes、tool version、生成物 hash、atlas/meta 版本、能力；输入绝对路径移入仅本地 `debug_inputs` | Runtime/UE 校验 v2；不匹配明确拒绝；manifest 不泄露开发机绝对路径 |
| E2 | `blocks.json`/`textures.json` schema v2：稳定排序、材质类、面级 sprite/tint、图集 tile 元数据；不手改 | 相同输入两次生成 hash 稳定；Runtime/Preview 仍能加载 |
| E3 | UE Adapter 描述 `ue_adapter.json`：adapter id/version、UE 5.8.4、ABI 2、render level R0、材质/Shader/地图资产 hash、feature flags、平台；由 pinned adapter build 生成 | 描述与实际 UE Adapter 构建一致；篡改/缺失被 UE 拒绝 |
| E4 | NativeVersion 打包 `reim-convert package`：组合 DLL + generated 数据引用 + `ue_adapter.json` + `native_version.json` + `instance.json` 模板 + `verification.json` → `build/native-version/26.3` | 一条命令产出完整 NativeVersion；UE/Preview 可直接消费；不含绝对开发机路径 |
| E5 | NativeBuild 组合 `reim-convert assemble`：UE Runtime 包 + NativeVersion → `build/native-build/26.3`；`native_build.json` 记录 runtime/adapter/native version hash | 最终可启动组合由工具链生成；手工拷贝不能通过版本校验 |
| E6 | 版本绑定/校验：UE Runtime 启动时校验 `native_version.json`、`ue_adapter.json`、generated manifest、DLL/ABI/profile/UE 版本 | 任一不匹配明确报错；不允许“能加载就先跑” |
| E7 | 可重复性：两次 package/assemble 的生成物 hash 对比、输入 hash、工具版本、adapter pin | 报告写入 `build/m2/reports`；差异为 0 或有解释 |
| E8 | 决策/覆盖：`conversion-decisions/26.3.md` 记录 UE 渲染=Native Adapter、未做 JRT/Java 渲染翻译、Adapter/Contract 绑定、未支持方块模型 | 不把 M2 写成已完成渲染翻译/全量覆盖；决策可审计 |
| E9 | 端到端证明：UE 只从 NativeBuild 加载；手工替换 DLL/改 adapter hash/绕过 manifest 被拒绝 | 工具链产物可启动；手工拼装不可通过；证据写入验收记录 |

**验收**：R0 所需数据 + UE Adapter 集成包由 Converter 可重复产出；`package`/`assemble` 生成 NativeVersion/NativeBuild；UE 运行工具链产物；M1 的 oracle/Preview 回归不破。

### M2-F — 打包与 Shipping

| 任务 | 内容 | DoD |
|---|---|---|
| F1 | `BuildCookRun` 脚本：Development 与 Shipping；`-build -cook -stage -pak -archive`；固定 UE 5.8.4 | 两种配置都能产出 `REIMRuntime.exe` |
| F2 | UE Runtime 包内容：`REIMRuntime.exe`/Cooked 内容/Adapter、`instance.json` 模板、Config、Licenses、app-local VC++；**不打包** 版本相关 DLL、`generated/`（含 manifest/atlas）、存档、Mojang 资产；NativeBuild 由 `reim-convert assemble` 组合 Runtime 包 + NativeVersion（DLL 在 NativeVersion） | 包内路径无开发机绝对路径；启动时校验 NativeVersion/Adapter 与 Runtime/DLL/UE 版本一致 |
| F3 | Shipping 行为：无控制台；暂停/退出菜单；日志文件；DLL fatal → UE 日志/受控退出；崩溃 dump 可选 | 干净机可退出；崩溃时日志含 tick/世界/ABI/DLL 版本 |
| F4 | DLL 加载安全：绝对路径 `LoadLibraryExW`；限制搜索路径；DLL/ABI/hash 校验 | 缺失/篡改/错误 ABI 明确报错；不加载错误目录同名 DLL |
| F5 | 干净机验收：未安装 UE/JDK/MSVC/CMake/VS；测试目录 = NativeBuild（Runtime + NativeVersion）+ 用户提供的 `generated/` 与存档副本（不进入包）；进入世界、移动、退出 | 记录机器信息、GPU/驱动、截图、日志；失败不写成通过 |
| F6 | 依赖审计：`dumpbin /dependents`、VC++ 运行库、缺失 DLL、Editor 路径、JVM/classpath 检查 | 无缺失依赖；无 UE Editor 路径；无 Java 进程 |
| F7 | 基线：包大小、冷启动到世界时间、RAM/VRAM、60 秒冒烟 | 数字写入验收记录；异常有解释 |
| F8 | NativeBuild 组合验收：`assemble` 产出的目录包含 Runtime/NativeVersion/`native_build.json`/实例模板；干净机只拷贝该组合 + 用户数据 | 手工拼装的 DLL/Adapter 不能通过；组合 hash 可追溯 |

**验收**：Shipping 在干净 Windows 机器启动并进入真实世界；NativeBuild 由工具链组合；DX12 主路径通过；DX11 结果单独记录。

### M2-G — 双后端回归与性能

| 任务 | 内容 | DoD |
|---|---|---|
| G1 | 双后端脚本：同一存档/出生点/auto-walk；Preview 直接用 generated/，UE 从 NativeBuild 启动；各自导出状态 JSON/方块样本 | 玩家位置、区块数、section 数、方块样本一致；渲染差异有记录；UE 侧确认走 NativeBuild |
| G2 | UE 60 秒冒烟：TPS≈20、FPS 目标、加载/流式、退出；可选长时 soak | 无崩溃；TPS 不随 FPS 波动；日志统计完整 |
| G3 | Bridge 预算：GT fetch+复制 < 2 ms；RT 上传均值/峰值；帧块未释放计数 | 1080p、距离 6/8 下达标；超预算有 profile 证据 |
| G4 | 无 JVM 检查：UE 进程期间无 `javaw/java`；命令行无 classpath；DLL 无 JVM 依赖 | 检查命令与结果写入报告 |
| G5 | UE Automation：Bridge NullRHI、R0 场景冒烟、错误路径；脚本包装 | Development 下自动化通过；失败留日志/截图 |
| G6 | “UE 无游戏逻辑”审查：扫描 Source 中存档/NBT/区块网格/物理/方块表代码；代码评审清单 | 只有 Bridge/Renderer 消费 Contract；无第二套实现 |
| G7 | 回归基线：R0 截图集合、固定相机/种子/资源、性能 JSON | M3 可直接复用；变化有 diff 说明 |

**验收**：双后端语义一致；性能/线程达标；无第二套游戏实现；自动化可重复。

### M2-H — 验收与记录

| 任务 | 内容 | DoD |
|---|---|---|
| H1 | 统一命令与文档：构建、打包、运行、自动化、日志位置 | 文档命令可复现；路径使用实际安装目录 |
| H2 | 错误矩阵：缺文件/ABI/profile/GPU device lost/最小化/损坏存档/DLL fatal | 每项有明确错误与退出码，不崩溃 |
| H3 | `tools/m2-acceptance.ps1`：CMake 测试、ABI/双后端、Preview 回归、UE R0 截图、性能、JVM 检查、干净机清单 | 输出 `build/m2/reports/m2-acceptance.json` + 截图；未执行项明确标注 |
| H4 | `docs/M2_ACCEPTANCE.md`：按 ROADMAP M2 七条 + M2 执行补充（NativeBuild 由工具链产出）与 G2 逐条记录实际运行结果 | 只写实测；豁免/未运行单独标注 |
| H5 | 已知限制：R0 范围、非完整方块、单维度、只读、光照兜底、DX11、截图差异、无音频/实体/UI | 不夸大成果；与 M1 限制合并表述 |
| H6 | G2 结论与用户可见验收 | 用户实际运行 UE 画面并确认；形成进入 M3 的依据 |

**验收**：ROADMAP M2 七条全部满足，或未执行条目经用户显式豁免并记录，形成 G2 结论。

### M2-I — 贴图/模型正确性（S4，H6 发现）

> 触发：2026-11-21 H6 首轮用户目视验收发现 UE 画面贴图/颜色与原生 26.3 不符。图集本身来自用户 `client.jar`（`manifest.json` 记录 `client_jar_sha256`），不是素材缺失；问题在 UE 呈现层的顶点色绑定，以及 Converter/运行时对状态变体与非整方块未覆盖。证据与根因见 §15。

| 编号 | 内容 | DoD |
|---|---|---|
| I1 | 修 UE 顶点色绑定：`McRenderState.cpp` 的 `InitComponentVF(..., true)` 在 UE 5.8 语义是 per-instance override 颜色，不是逐顶点色；确认 `M_McBlock` 为 `Atlas × VertexColor` 输出到 Emissive（Unlit） | 草顶恢复 tint 与烘焙明暗；云杉叶恢复深绿；不再灰草/白叶/平板光 |
| I2 | 图集采样与色彩空间：clamp、单 mip、nearest、NeverStream；tint 的 sRGB/linear 与 Preview 统一 | 无跨 tile 渗色；同场景 Preview/UE 色差在阈值内 |
| I3 | 状态变体端到端：世界 `properties` 参与视觉解析；至少 `grass_block[snowy=true]` → `grass_block_snow` | 雪地草方块侧面使用 `grass_block_snow` |
| I4 | 多层/非整方块几何：多层 element、薄方块（`snow_height*`）、十字植物（`short_grass`/花/蕨）、双面 cutout | 出生点可见雪层与草；无洋红缺失块 |
| I5 | biome tint：使用已解析的 biome palette，按位置给草/叶/水着色；Converter 产出群系颜色表 | 雪地群系颜色与原生一致；Preview/UE 同色 |
| I6 | Alpha/cutout 与水面：树叶/植物 cutout；水 tint/alpha（动画纹理记为限制） | 树叶不是整块不透明；水色正确 |
| I7 | 视觉回归工具：固定相机像素断言（草/叶/雪/水颜色阈值、无洋红块、Preview/UE 对照），接入一键验收 | 报告 JSON 含逐项 pass/fail 与截图 |
| I8 | 重建与 H6 重跑：extract → CMake → UE Dev/Shipping → package/assemble → H6；更新验收记录 | 用户确认 H6；未确认不得写通过 |

**验收**：H6 出生点同视角下草/叶/雪/水颜色正确且无缺失块；Preview/UE 差异可解释；ABI v2 不变；NativeBuild 仍由工具链产出并通过 hash 校验。

---

## 6. 顺序与并行

```text
M2-A UE 工程/前置
  ├─→ M2-B Contract v2 + Bridge ──→ M2-C R0 Renderer ──→ M2-D Host 输入/UI
  │            │                            │                     │
  ├─→ M2-E Converter / UE Adapter 集成 ←────┘                     │
  │            │                                                  │
  │            └──────────────→ M2-F NativeBuild/Shipping ←──────┘
  │                                      │
  └──────────────────────────────────────┴─→ M2-G 双后端/性能 ──→ M2-H 验收
```

**可并行**

- B（Contract/Bridge）与 E1/E2（数据 v2）可同时推进；B1 的字段冻结后 E1/E2 才能定稿。
- A 的打包骨架与 B 的 NullRHI 数据面可并行；C 依赖 B 的帧块/资产接口。
- E3（Adapter 描述）可在 C 的适配器构建产物稳定后定稿；E4/E5（NativeVersion/NativeBuild）依赖 C 与 UE Runtime 包。
- F 的 `BuildCookRun` 脚本可在 C 之前先跑空场景，但不得提前宣称 Shipping 通过；最终 F 依赖 E 的 NativeBuild。
- G1 的双后端状态导出可在 D 完成后立即做，不必等完整 F。
- 干净机 F5 依赖用户提供机器，在 S3 内执行；若机器延迟，S3 先做其余项并记录阻塞。

**关键路径**：A → B → C → E → F → G → H。D 与 E 并行；F 同时依赖 D（Host）与 E（NativeBuild）。

**无 UE 时的处理**：只允许推进 A2（CMake/ABI v2 编译）、B1/B2/B3 的头文件与 Preview/headless 侧、E1/E2/E3/E8 的 schema/描述/决策工作；E4/E5 的打包、C/D/F 必须等 UE。不得生成“UE 截图”。UE 安装是 M2 的硬前置。

---

## 7. Rendering Contract v2

> 详细规则见 `ARCHITECTURE.md` §7、§8；本节只列 M2 必须实现/变更的部分。最终字段名与布局在 B1 评审后冻结，冻结后改动必须升 ABI 版本。

### 7.1 变更原则

1. **整份 Contract 版本化**：`REIM_ABI_VERSION` 升到 2；所有结构只保证同一版本内稳定，单字段变化也升版本。
2. **只增不改语义**：M1 的 `fetch_frame`/`submit_input`/`set_simulation_paused`/`get_status_json` 语义保留；新增帧块/资产/信息接口。
3. **Host 同步重建**：Preview/headless/UE 都升级到 v2；不保留 v1 兼容服务，避免两套生命周期规则。
4. **跨边界零 C++ 对象**：只传 POD/句柄/不可变缓冲；不传 STL/UE 容器；不抛异常。
5. **头文件即契约**：线程、单位、坐标、所有权、错误码写在 `reim_abi.h`/`reim_render.h` 注释中；实现与测试以此为准。

### 7.2 导出与 API 表（目标形状）

```c
#define REIM_ABI_VERSION 2u

typedef enum ReimResult {
  REIM_OK = 0,
  /* M1 错误码保留 */
  REIM_ERR_BUSY = 11,          /* v2: 帧块未归还/资源暂不可用 */
  REIM_ERR_CAPABILITY = 12     /* v2: profile/能力不匹配 */
} ReimResult;

typedef struct ReimRuntimeInfo {
  uint32_t struct_size;
  uint32_t abi_version;
  char     profile[16];        /* "26.3" */
  char     build_id[64];
  uint64_t manifest_hash;
  uint32_t capabilities;       /* REIM_CAP_* 位 */
  uint32_t render_level;       /* 0 = R0 */
  uint32_t reserved[8];
} ReimRuntimeInfo;

typedef enum ReimAssetKind {
  REIM_ASSET_ATLAS_PNG = 1,
  REIM_ASSET_ATLAS_META = 2
} ReimAssetKind;

typedef struct ReimRuntimeApi {
  /* 与 v1 相同前缀，保证字段顺序稳定 */
  uint32_t struct_size;
  uint32_t abi_version;

  ReimInstance* (*create)(const ReimInstanceConfig*, const ReimHostApi*, int32_t*);
  void (*destroy)(ReimInstance*);
  int32_t (*start)(ReimInstance*);
  void (*stop)(ReimInstance*);
  int32_t (*fetch_frame)(ReimInstance*, ReimRenderFrame*);
  void (*submit_input)(ReimInstance*, const ReimInputEvent*);
  void (*set_simulation_paused)(ReimInstance*, uint8_t);
  int32_t (*get_status_json)(ReimInstance*, char*, uint32_t);

  /* v2 新增 */
  void (*release_frame)(ReimInstance*, uint64_t frame_id);
  int32_t (*get_runtime_info)(ReimInstance*, ReimRuntimeInfo* out);
  int32_t (*get_asset)(ReimInstance*, uint32_t kind,
                       const void** out_data, uint64_t* out_size,
                       uint64_t* out_revision);
};
```

- `ReimHostApi` 在 v2 中 **要求** `alloc`/`free` 非空；`log` 仍可选但推荐。
- `ReimInstanceConfig` v2 追加 `instance_dir`、`native_version_path`、`render_level`、可选 `spawn_yaw`/`spawn_pitch`（诊断/截图）；保留 `struct_size` 校验。
- `frame_id` 单调递增；`release_frame` 必须与最近的 `fetch_frame` 的 `frame_id` 对应；重复释放是空操作并记警告。

### 7.3 帧块与所有权（M2-D04）

```text
fetch_frame(out):
  Runtime 统计本次 updates/removes/顶点/索引字节数
  Runtime 调用 host->alloc(total_bytes, 16) 得到 blob
  Runtime 把 update 结构、remove 结构、顶点、索引按固定顺序写入 blob
  out->updates/removes/vertices/indices 指向 blob 内部
  out->owned_block = blob; out->owned_bytes = total_bytes
  out->frame_id = ++counter

Host:
  GT 读取/转换需要的字段（不阻塞模拟）
  GT 把 blob 指针/所需元数据交给 RT（TSharedPtr 自定义 deleter 或 UE 任务）
  RT 上传 GPU 后调用 release_frame(instance, frame_id)
  Runtime 调用 host->free(blob)
```

- **最多 4 个未释放帧块**；达到上限时 `fetch_frame` 返回 `REIM_ERR_BUSY`，不分配、不阻塞、不改变模拟；Host 记录并复用上一帧。
- `release_frame` 任意线程；Runtime 必须允许 GT/RT 调用；Host `free` 实现需线程安全。
- 零顶点 update 仍保留在 updates 中，Host 用其清除对应材质；`SectionRemove` 清除整个 section。
- `fetch_frame` 每次返回**增量**；首次加载包含全部 section；之后只含变更/移除。
- 帧块是 Host 内存在途数据的唯一所有权载体；Runtime 不持有 `ReimSectionMeshUpdate*` 之外的任何 Host 指针。

### 7.4 插值

```text
ReimRenderFrame v2 追加：
  ReimCameraSnapshot prev_camera;
  ReimPlayerSnapshot prev_player;
  float tick_alpha;        // 0..1，同 partial_tick
```

- `camera`/`player` 为当前 tick 状态；`prev_*` 为上一 tick；`tick_alpha` 为两 tick 之间比例。
- GT 插值规则：位置线性；yaw/pitch 走最短角路径；`tick_alpha` 在暂停/未推进时保持 0。
- section/网格不插值；流式/方块变化按 tick 发布。
- 若 Host 不需要插值（Preview），可直接使用 `camera`/`player`。

### 7.5 资产

- `get_asset` 返回 Runtime 实例生命周期内有效的只读内存；Host 不得释放；实例销毁前必须完成 RT 上传。
- `REIM_ASSET_ATLAS_PNG`：PNG 字节；`REIM_ASSET_ATLAS_META`：UTF-8 JSON（宽/高/tile/padding/采样/格式/hash）。
- 资产 revision 与 manifest 绑定；manifest 不匹配时 `create` 失败，不进入渲染。
- M2 只有图集；字体/声音/模型/动画留给后续阶段，不预留大而空的接口。

### 7.6 线程/单位/坐标契约（写入头文件）

- `create/destroy/start/stop`：控制线程，互不并发；`destroy` 必须能处理未 `start` 的实例。
- `fetch_frame`：Host 渲染线程（UE GT）单线程；不得并发调用。
- `release_frame`：任意线程；不得阻塞。
- `submit_input`：任意线程；事件在下一 tick 应用。
- `get_asset`：实例存活期任意线程；返回内存只读。
- 1 单位 = 1 方块 = 1 米；右手系/`+Y` 上；UE 转换只在 Bridge。
- 所有字符串：路径 UTF-16，日志 UTF-8；不跨界传 `std::string`。

### 7.7 版本校验

1. UE 加载 DLL 后校验 `REIM_ABI_VERSION`、`ReimRuntimeInfo.abi_version`、`profile == "26.3"`、generated manifest v2。
2. 校验 `native_version.json`（DLL/data hash）与 `ue_adapter.json`（UE 5.8.4/ABI 2/R0/资产 hash）；任一不一致 → 明确错误，不进入世界。
3. `render_level != 0` → `REIM_ERR_CAPABILITY`。
4. `struct_size` 小于头文件最小值 → 拒绝；未知字段忽略但记录。
5. Preview/headless 使用同一套 Contract/数据校验；NativeVersion/Adapter 校验只在 UE Host 与打包脚本中执行，避免调试 Host 依赖 UE。

---

## 8. UE Host / Bridge / R0 Renderer

### 8.1 UE 工程与模块

| 模块 | 职责 | 禁止 |
|---|---|---|
| `REIMGame` | GameInstance/Subsystem、地图/生命周期、启动参数、模式切换、退出 | 不做玩法/世界/存档/物理 |
| `REIMBridge` | DLL 加载、ABI v2、帧块、坐标转换、输入转发、暂停、日志/错误 | 不解析存档、不生成网格、不查询方块 |
| `REIMRenderer` | SceneProxy、VertexFactory、材质、图集、视锥/距离排序、雾 | 不重建世界、不修改 Contract 数据 |
| `REIMUI` | 调试 HUD、加载页、暂停/退出菜单 | 不做完整 Minecraft UI |
| `REIMInput` | 键鼠采集 → ReimInputEvent；捕获/释放 | 不直接改玩家/世界 |
| `REIMRuntimeContent` 插件 | `RuntimeDependencies` 打包 DLL/实例模板/配置 | 不包含 Mojang 资产/Minecraft 文件 |

**UE Adapter 与 Converter 的绑定**

- `runtime/host` 构建出的 Adapter（模块/Shader/材质/地图）是 **pinned artifact**；`converter/adapters/ue` 保存其描述生成器、版本 pin 与 hash 清单。
- Converter **不生成** UE C++/材质/Shader 代码；它负责把 Adapter 描述与版本化 Runtime/数据组合成 NativeVersion/NativeBuild，并在启动前后校验。
- 这样“Java Edition → C++ Runtime + UE 渲染”是可重复的工具链产物，而不是手工拷贝 UE 工程。

**工程设置（R0 baseline）**

- 固定 UE 5.8.4；Windows x64；DX12 为默认 RHI；DX11 可启动冒烟。
- 关闭 Lumen、Nanite、VSM、自动曝光、TAA、运动模糊；固定分辨率与截图相机。
- Shipping 不含 Editor-only 模块；不启用控制台依赖。
- `RuntimeDependencies` 把实例模板、`Config/`、`Licenses/` 与 Adapter 资产放进包；版本相关 `mc_26.3.dll` 由 NativeVersion 提供，不放进通用 Runtime 包；manifest/图集来自用户 `generated/`。

### 8.2 Bridge 数据面

```text
启动：
  LoadLibraryExW(绝对路径, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)
  GetProcAddress("reim_get_api") → 校验 ABI 2
  get_runtime_info → profile/build/capabilities
  读取 native_version.json + ue_adapter.json + generated manifest
    → 校验 profile/ABI/hash/UE 5.8.4/Adapter 版本
  create(config, host) → start()

每帧（GT）：
  fetch_frame(&frame)
  if REIM_ERR_BUSY: 复用上一帧 GPU 状态，记录
  维护 pending update map（section/material → 最新 revision）
  创建/更新渲染代理的 section 表
  把 host-owned blob 交给 RT（任务/命令）
  相机/玩家插值 → 设置 UE Camera

RT 上传完成后：
  release_frame(frame_id)
```

- **更新合并**：GT 在处理 blob 前按 `(section, material)` 合并增量；旧 revision 丢弃；remove 与 update 的顺序按 blob 内顺序应用。
- **首次加载**：`ready == 0` 时显示加载页；`ready == 1` 且 pending uploads 为 0 才进入可操作。
- **纹理**：GT 用 `get_asset` 的 PNG 创建 transient `UTexture2D`；RT 上传 mip 0；material instance 绑定；revision 变化时重建。
- **关闭**：停止输入 → `set_simulation_paused(true)` → `stop()` → 等待 RT 排空并归还所有未释放帧块 → `destroy()` → 释放纹理/代理。
- **日志**：DLL 日志回调 → UE `UE_LOG(LogREIM...)` 并写 `Saved/Logs/REIM.log`；错误包含 ABI/profile/世界/manifest 路径。

### 8.3 R0 Renderer

- **一个世界级 Proxy**：`AMcWorldRenderer`（或 AInfo）+ `UMcWorldRendererComponent`；`CreateSceneProxy` 返回 `FMcWorldSceneProxy`；不为每个 section 建 Actor/Component。
- **section 表**：Proxy 持 `TMap<(chunkX,sectionY,chunkZ), FMcSectionMeshes>`；每个材质持 VB/IB、bounds、revision；GPU 资源只在 RT 创建/释放。
- **绘制**：`GetDynamicMeshElements` 每帧按视锥/距离挑选可见 section，生成 `FMeshBatch`；不透明近到远，半透明远到近；每 batch 绑定材质与图集。
- **顶点工厂**：自定义 `FMcVertexFactory`，输入 position/normal/uv/light/sprite/tint；shader 解码光照与 tint；若 VF 集成受阻，允许 R0 临时退化为 `FLocalVertexFactory` + 烘焙顶点色（Bridge 在 GT 侧把 light+tint 合成为 color），必须在验收记录标注，R1 恢复自定义 VF。
- **材质**：`M_McBlock`（opaque）与 `M_McBlockTranslucent`（translucent；alpha 取 tint.a，tint.a 为 255 时用 0.72 兜底，与 M1 的水面表现一致）；图集纹理参数；距离雾参数；方向光兜底（skylight/blocklight 全 0 时）。
- **图集**：nearest、clamp、无 mip/padding（R0 与 M1 一致）；通道顺序按 UE 格式转换；不做动画纹理。
- **环境**：天空色/雾色/雾起止来自 `EnvironmentSnapshot`；R0 固定太阳方向、无时间变化。
- **玩家**：第一人称只画相机；F5 第三人称画简单盒/胶囊（0.6×1.8 m）与 AABB 线框；不加载皮肤/模型。
- **明确不做**：不做 UE 侧 mesher、方块模型解析、光照传播、粒子/天气/UI/音频/后处理/OIT、Lumen/Nanite。

### 8.4 性能预算（M2 校准）

| 指标 | 目标 | 说明 |
|---|---|---|
| 模拟 | 20 TPS ± 0.2 | 与 M1 一致；UE 帧率不驱动 tick |
| 渲染 | 1080p、距离 6–8、60 FPS 目标 | 记录实际 GPU/驱动/画质设置 |
| GT Bridge | fetch + 合并 + 提交 < 2 ms/帧 | 首次加载/大块上传单独统计 |
| RT 上传 | 稳态均值 < 4 ms；首载可更高 | 流式更新按 tick 分摊 |
| 内存/显存 | 记录基线，无持续增长 | 60 秒冒烟中采样 |
| 加载 | 首帧到可操作时间记录 | 不承诺固定秒数，只要求可复现 |

---

## 9. Converter 与生成物（M2）

### 9.1 CLI：从数据提取到 NativeBuild

```text
reim-convert prepare  --minecraft-dir <dir> --version 26.3 --out work/26.3
reim-convert analyze  --profile 26.3 --out work/26.3
reim-convert extract  --profile 26.3 --out generated/26.3
reim-convert inspect-world --world <path> --out report.json

:: 新增：组合 NativeVersion / NativeBuild
reim-convert package --profile 26.3 --generated generated/26.3 ^
  --dll build/m2/bin/mc_26.3.dll ^
  --ue-adapter <pinned-adapter-dir> ^
  --out build/native-version/26.3

reim-convert assemble --native-version build/native-version/26.3 ^
  --runtime-package build/m2/package/Windows ^
  --out build/native-build/26.3
```

- `package`：把版本化 DLL、生成数据引用、`ue_adapter.json`、`native_version.json`、实例模板、验证摘要组合成 NativeVersion。
- `assemble`：把 UE Runtime 包与 NativeVersion 组合成最终 NativeBuild；记录三者的 hash/版本。Runtime 包由 UBT/BuildCookRun（`tools/m2-build.ps1`）产出后交给 `assemble`，M2 不在 C++ 工具内替换 UBT。
- `package/assemble` **不生成 UE 渲染代码**；它们 pin 并使用 Native Adapter 构建产物，校验 Contract/ABI/profile/UE 版本。
- M2 不扩张为通用差分框架；必要的 `verify` 只做本地校验。

**端到端产物链**

```text
用户自有 Java Edition 26.3 文件
  → prepare / analyze / extract
  → generated/26.3（数据/表/图集/manifest）
  → package（+ mc_26.3.dll + UE Adapter 描述）
  → NativeVersion（版本相关）
  → assemble（+ UE Runtime/Adapter 包）
  → NativeBuild（可启动：C++ Runtime + UE 渲染）
```

- 版本相关的是 DLL/data/Contract；UE Adapter 是 MC 版本无关、UE 版本绑定的 Native Adapter。
- “把 Java 版转成有 UE 渲染的 C++ 版”体现为 NativeBuild，而不是把 Java 渲染代码翻译成 UE。

### 9.2 生成物

| 文件 | 层级 | M2 内容 |
|---|---|---|
| `manifest.json` | generated | schema/ABI 2、profile、input hashes、tool version、生成物/PNG hash、atlas meta 版本、能力；绝对路径仅本地 `debug_inputs` |
| `blocks.json` | generated | 方块表 v2：稳定排序、材质类、面级 sprite/tint、完整方块/碰撞/透明/液体；不新增未使用的模型语义 |
| `textures.json` | generated | 图集 tile/UV/索引 v2；稳定顺序；与 `atlas.png` 尺寸一致 |
| `atlas.png` | generated | 用户自有输入生成的图集；M2 只保证 R0 所需方块；不随产品分发 Mojang 资产 |
| `atlas_meta.json`（或 manifest 内） | generated | 尺寸/tile/padding/采样/格式/hash；经 `get_asset` 交付 |
| `ue_adapter.json` | NativeVersion | adapter id/version、UE 5.8.4、ABI 2、R0、材质/Shader/地图资产 hash、feature flags、平台 |
| `native_version.json` | NativeVersion | profile、DLL hash、generated manifest hash、adapter hash、ABI、能力、验证摘要 |
| `instance.json` 模板 | NativeVersion | world/generated/native-version 路径与 Host 设置 |
| `verification.json` | NativeVersion | 构建/校验结果、输入 hash、工具版本 |
| `native_build.json` | NativeBuild | runtime package hash、adapter hash、native version hash、UE/ABI/profile |
| `analysis/classes.json`、`deps.json` | work | 沿用；M2 不扩张为全量翻译报告 |
| `reports/*` | work | 有则保留；没有不阻塞 M2 |

### 9.3 规则

- 生成物不手改；修改进入 Converter/Patch。
- 同一输入/工具版本/Adapter pin 重复执行，生成物 hash 稳定；输入路径不参与内容 hash。
- Runtime/UE 只接受 manifest v2 + `native_version.json` + `ue_adapter.json`；profile/ABI/hash/UE 版本/adapter 不匹配拒绝启动。
- Shipping 包不包含 `debug_inputs`/`generated/` 用户路径/存档/Mojang 资产；NativeBuild 只组合 Runtime + NativeVersion，用户数据由实例配置提供。
- 不把反编译源码/Mojang 文件提交或打包。
- **UE 渲染是 Native Adapter，不是 Java 渲染翻译**；Converter 的职责是 pin/组合/版本化/校验，不生成 UE C++/材质/Shader。

### 9.4 覆盖与决策

- M2 的 Converter 目标是“R0 所需数据 + UE Adapter 集成可靠、可重复、可校验”，不是扩大翻译覆盖。
- `conversion-decisions/26.3.md` 记录：数据生成路线、UE 渲染=Native Adapter、未做 JRT/Java 渲染翻译、Adapter/Contract/UE 版本绑定、未支持方块模型。
- 若发现 blocks/textures 字段不足以支撑 R0，优先补数据生成，不在 Runtime/UE 硬编码方块特例。
- JRT/字节码翻译/DFU 全量适配不进入 M2。
- NativeVersion/NativeBuild 的目录布局作为稳定接口冻结；Launcher（M4）按该布局安装/启动。
- **MC 版本升级**：UE Runtime/Adapter 可复用，Converter 重新产出 `mc_<version>.dll`/数据/manifest 并 `assemble` 成新的 NativeBuild；**UE 版本升级**只影响 Adapter/Runtime 包，两条升级线保持独立。

---

## 10. 构建、打包与运行

### 10.1 开发构建

```bat
cd /d D:\EngineeringDocuments\REIM-Launcher
set UE_ROOT=D:\Program Files\Epic Games\UE_5.8
set UE-LocalDataCachePath=D:\UE-DDC
cmake --preset m2-release
cmake --build --preset m2-release --parallel 8

:: UE 编辑器工程生成/构建（路径按实际 UE 5.8.4 安装）
"%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" ^
  REIMEditor Win64 Development ^
  -project="D:\EngineeringDocuments\REIM-Launcher\runtime\host\REIM.uproject" ^
  -waitmutex
```

### 10.2 Shipping 打包

```bat
set UE_ROOT=D:\Program Files\Epic Games\UE_5.8
set UE-LocalDataCachePath=D:\UE-DDC
"%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun ^
  -project="D:\EngineeringDocuments\REIM-Launcher\runtime\host\REIM.uproject" ^
  -noP4 -platform=Win64 -clientconfig=Shipping ^
  -build -cook -stage -pak -archive ^
  -archivedirectory="D:\EngineeringDocuments\REIM-Launcher\build\m2\package"
```

- IoStore/`-iostore` 二选一，在 F1 记录；启用时产物为 `.utoc/.ucas`。
- Project Settings → Packaging → Prerequisites 选择 app-local VC++；F6 用 `dumpbin /dependents` 验证。
- Shipping 不含 Editor-only 模块；确保暂停/退出菜单可用。
- UBT 的可执行文件实际输出在工程 `Binaries/Win64/`；验收脚本可复制/暂存到 `build/m2/ue/`，但不得把 `Binaries/` 提交仓库。

**组合 NativeVersion / NativeBuild（Converter 工具链）**

```bat
build\m2\bin\reim-convert.exe package --profile 26.3 ^
  --generated generated\26.3 ^
  --dll build\m2\bin\mc_26.3.dll ^
  --ue-adapter runtime\host\Build\Adapter ^
  --out build\native-version\26.3

build\m2\bin\reim-convert.exe assemble ^
  --native-version build\native-version\26.3 ^
  --runtime-package build\m2\package\Windows ^
  --out build\native-build\26.3
```

NativeBuild 布局：

```text
build/native-build/26.3/
├─ REIMRuntime/                  # UE Shipping/Development 包
├─ NativeVersions/26.3/          # mc_26.3.dll + native_version.json + ue_adapter.json
├─ native_build.json             # Runtime/Adapter/NativeVersion 版本与 hash
└─ instance.json.template
```

- 最终可启动的是 NativeBuild，不是单独拷贝的 DLL 或 UE 工程。
- `ue_adapter` 目录由 UE Adapter 构建产出；Converter 只 pin/校验/组合，不生成 UE 渲染代码。

### 10.3 运行

```bat
:: Preview（M1 回归 Host，ABI v2；直接用 generated/，不走 NativeBuild）
build\m2\bin\REIMPreview.exe --world <save> --generated generated\26.3 --render-distance 6

:: UE（从 NativeBuild 启动，Development 或 Shipping）
build\m2\native-build\26.3\REIMRuntime\REIMRuntime.exe ^
  -reim-instance="D:\EngineeringDocuments\REIM-Launcher\build\m2\instance.json" -log

:: UE Shipping（干净机；NativeBuild + 用户数据副本）
REIMRuntime.exe -reim-instance="C:\REIMTest\instance.json" -log
```

- 实例路径/生成物/NativeVersion/日志目录由 `instance.json` 提供；CLI 覆盖字段用于自动化。
- 不把开发机绝对路径写进 Shipping 包；验收脚本生成临时实例配置。
- UE Runtime 启动时校验 NativeBuild 的 Runtime/Adapter/NativeVersion 版本与 hash；手工替换 DLL 或绕过 manifest 必须失败。

### 10.4 自动化

```bat
powershell -ExecutionPolicy Bypass -File tools\m2-build.ps1 -Config Release
powershell -ExecutionPolicy Bypass -File tools\m2-smoke.ps1 -World <save>
powershell -ExecutionPolicy Bypass -File tools\m2-acceptance.ps1 -World <save> -UeSeconds 60
```

UE Automation（Development）：

```bat
:: 以下路径用 Development NativeBuild；Shipping 不含 Automation
:: 数据面：无渲染设备
build\m2\native-build\26.3\REIMRuntime\REIMRuntime.exe ^
  -nullrhi -unattended -nopencils ^
  -ExecCmds="Automation RunTests REIM.Bridge; Quit" -log

:: R0 渲染：需要 GPU
build\m2\native-build\26.3\REIMRuntime\REIMRuntime.exe ^
  -unattended -nopencils ^
  -ExecCmds="Automation RunTests REIM.R0; Quit" -log
```

- `REIM.Bridge.NullRHI`：加载/ABI/1000 tick/帧块释放/错误路径。
- `REIM.R0.Smoke`：有 GPU 的 R0 场景冒烟；不做像素级断言，截图/统计由验收脚本采集。
- 自动化失败必须保留日志与截图；不得用“Editor 能打开”替代。

---

## 11. 测试与验收

### 11.1 自动/半自动

| 层级 | 内容 | 工具 |
|---|---|---|
| 单元 | ABI v2 布局/版本、帧块所有权、插值、坐标转换、manifest 校验 | Catch2 |
| 数据 | 真实区块方块样本 vs Python oracle（沿用 M1） | Python 3.12 |
| 集成 | headless/Preview ABI v2、1000 tick、流式 | `m1-tests` / CLI |
| Converter | `package`/`assemble`、NativeVersion/NativeBuild hash、Adapter/UE 版本校验 | `reim-convert` + PowerShell |
| Bridge | NullRHI 加载/帧消费/释放/错误 | UE Automation |
| 渲染 | R0 截图、视锥/透明、图集、雾 | UE + 人工 |
| 双后端 | 同存档/同输入状态对照 | PowerShell + JSON |
| Shipping | 干净机启动/进入世界/退出 | 人工/脚本记录 |
| 性能 | TPS/FPS/GT/RT/内存 | UE Insights + 报告 |

### 11.2 通过标准

- 同步：同一存档/出生点下 Preview 与 UE 的玩家位置、区块数、section 数、方块样本一致；渲染差异有解释。
- 线程：20 TPS 稳定；FPS 在 30–144 波动时 tick 不受影响；GT Bridge 在预算内。
- 无 JVM：UE/Preview 进程与命令行无 Java/classpath；DLL 无 JVM 依赖。
- Shipping：干净机（无 UE/JDK/MSVC/CMake）可启动、进入世界、移动、退出；日志可定位。
- Converter：manifest/data v2 可重复；`package`/`assemble` 产出 NativeVersion/NativeBuild；篡改/不匹配被拒绝。
- NativeBuild：UE 只从工具链产出的 NativeBuild 启动；手工替换 DLL/Adapter、绕过 manifest 必须失败。
- 单实现：UE 源码中没有存档/NBT/网格/物理/方块表实现；Renderer 只消费 Contract；UE 渲染是 Native Adapter，没有 Java 渲染翻译/命令回放。

### 11.3 验收命令与记录

- 自动化脚本产出 `build/m2/reports/m2-acceptance.json`、R0 截图、双后端 JSON、性能数字。
- 干净机结果由用户提供/现场执行，写入 `docs/M2_ACCEPTANCE.md`；脚本不自动把未执行项标为通过。
- 按用户规则，默认不主动运行长时间 soak；由用户运行并反馈。

---

## 12. 风险与缓解

| 风险 | 影响 | 缓解 |
|---|---|---|
| UE 5.8.4 未安装/版本不符 | M2 无法开始 | A1 硬前置；记录路径/版本；不用其他引擎版本替代 |
| UE 构建/打包耗时长 | 会话上下文与迭代速度 | 会话拆分；增量构建；BuildCookRun 脚本化；不在 Editor 里手工重复操作 |
| 自定义 VertexFactory/材质集成复杂 | R0 卡住 | 允许 R0 退化为 `FLocalVertexFactory` + 烘焙顶点色，Contract 不变并记录；R1 恢复 |
| GT→RT 帧所有权/释放错误 | 悬垂/泄漏/崩溃 | host-owned blob + `release_frame` + 4 块上限 + NullRHI/泄漏测试 |
| Shipping 中 DLL 加载失败/路径劫持 | 干净机失败/安全问题 | NativeVersion 布局 + 绝对路径 `LoadLibraryExW` + 受限搜索 + hash/ABI 校验 |
| VC++ 运行库缺失 | 干净机启动失败 | app-local 运行库；`dumpbin /dependents`；F5 实测 |
| 干净机/VM 无法用 DX12 | Shipping 验收阻塞 | 物理机或 GPU 直通；DX12 主验收，DX11 冒烟记录；不把 VM 软件 RHI 当通过 |
| UE 侧混入游戏逻辑 | 两套实现/返工 | Bridge/Renderer 边界；G6 扫描与评审；Contract 只读 |
| 坐标/绕序/FOV 转换错误 | 画面翻转/视角不对 | 集中转换函数 + 单测 + 已知地标截图 + 背面剔除检查 |
| 图集/纹理格式/采样错误 | 方块花屏/偏色 | `get_asset` 元数据校验；RGBA8 通道交换测试；nearest/无 mip 与 M1 对齐 |
| 光照层缺失（M1 已知） | UE 画面偏平 | 方向光兜底；记录为 R0 限制，R1/M3 补光照 |
| R0 视觉与 Preview 差异大 | 验收争议 | 固定相机/配置/截图；差异分类为渲染后端差异，不伪造一致 |
| Converter v2 与 M1 数据不兼容 | Preview 回归失败 | E2 保持 schema 可并存/升级测试；双后端回归 |
| `.uasset/.umap` 入库/合并问题 | 工程损坏/仓库膨胀 | Git LFS；仅必要资产；避免多人同时改二进制 |
| Shipping 包误分发 Mojang 资产 | 法律风险 | 包内不含 `generated/`/存档/Mojang 文件；M2 仅内部验收；发布前评审 |
| Converter 与 UE 手工拼装，NativeBuild 不可复现 | “原生 MC”不在工具链里，版本漂移 | `package/assemble` 组合 + `ue_adapter.json`/`native_version.json`/`native_build.json` hash + 手工拼装拒绝 |
| UE Adapter/Contract/Runtime 版本漂移 | 加载错配/画面异常 | Adapter/UE/ABI/profile 绑定；启动校验；不匹配明确失败 |
| 把 Java 渲染逻辑翻译成 UE / 第二套渲染实现 | 范围失控/行为分叉 | 明确 Native Adapter（G 路线）；不做命令回放；G6 扫描与评审 |
| 性能不达标 | 体验差/阻塞 M3 | 距离 6 起步；GT/RT 预算；先测再优化；必要时先做可见性/批处理 |

---

## 13. 新对话启动说明

**先读**

1. `README.md`（产品边界）；
2. `docs/ROADMAP.md` §1–§2、§4（M2 目标/验收/G2）；
3. `docs/ARCHITECTURE.md` §6、§7、§8（Runtime、渲染接管、ABI/Bridge）；
4. `docs/TECH_STACK.md` §4–§6、§8（DLL/UE/打包/CI）；
5. `docs/DEVELOPMENT.md` §1–§4、§6（环境/构建/ABI 规则）；
6. `docs/M1_ACCEPTANCE.md`（已接受事实、豁免项、已知限制）；
7. 本文。

**硬规则**

- 不启动 Java Edition/JVM；JDK 25 只用于离线 Converter/oracle。
- 不使用假世界/假区块/预生成截图；UE 必须显示真实 26.3 存档。
- 不在 UE 中实现第二套世界/物理/方块/网格/存档逻辑。
- 不手改 `generated/`；不提交 Mojang 文件/世界/生成物/Shipping 包。
- UE 渲染是 Native Adapter；最终验收只认 Converter 产出的 NativeBuild，不得手工拼装 DLL/UE 工程；不把 Java 渲染翻译成 UE。
- C ABI 只用于 Runtime Host ↔ Native DLL；UE 不反向控制世界。
- 所有文件使用 Windows 绝对路径（如 `D:\EngineeringDocuments\REIM-Launcher\...`）。
- 未实际运行的结果不写成通过；干净机/长时 soak 未执行就标“未运行/豁免”。
- 未安装 UE 时停止 UE 工作并询问，不得用替代引擎或假截图。

**第一步**

1. 确认 UE 5.8.4 环境（已实测可构建）；确认存档、`generated/26.3`、干净验收机是否可用；
2. 检查 M1 ABI v1 现状：`runtime/contract/reim_abi.h`、`reim_render.h`、`runtime_instance.cpp`；确认 Converter 是否已有 `package`/`assemble`/Adapter pin 设计（M2-E）；
3. 按 §14 选择会话：S1 = M2-A + M2-B + M2-C；S2 = M2-D + M2-G1 + M2-E；S3 = M2-F + M2-G 余项 + M2-H；
4. 从对应工作包开始，不跨区间抢做后续内容。

**结束条件**

- 交付本会话工作包的源码/脚本/文档；
- 提供构建、打包、运行命令；
- 按 §1.3 输出实际验收结果与已知限制；
- 不把“未实际运行的测试”写成通过。

---

## 14. 会话拆分与交接（建议 4 个会话）

> 目标：减少会话切换；每个会话覆盖一个完整技术闭环。合并后单会话更长，按工作包结束点自检并更新交接状态，避免上下文堆积。

| 会话 | 工作包 | 估算 AI 时长 | 开始条件 | 结束交付 |
|---|---|---|---|---|
| **S1 基础 + R0 渲染** | M2-A + M2-B + M2-C | 22–38 小时 | UE 5.8.4、M1 构建可用 | UE 工程/ABI v2/Bridge；R0 真实地形/图集/光照/雾；Preview v2；R0 截图 |
| **S2 Host + Converter UE 集成** | M2-D + M2-G1 + M2-E | 16–28 小时 | S1 | 第一人称输入/菜单/加载；`package`/`assemble`；NativeVersion/NativeBuild；双后端状态对照 |
| **S3 Shipping + 回归 + 验收** | M2-F + M2-G（余项）+ M2-H | 12–22 小时 | S2；干净机可用 | NativeBuild Shipping；干净机记录；性能/JVM/错误路径报告；`M2_ACCEPTANCE.md`；G2 结论 |
| **S4 贴图/模型正确性（新增）** | M2-I | 10–20 小时 | S3 完成；H6 首轮（2026-11-21）发现贴图颜色错误 | UE 顶点色/tint 修复；状态变体与雪层/植物；biome tint；视觉回归；H6 重跑证据 |

**为什么是 4 个**

- S1 把“UE 工程 → ABI/Bridge → R0 Renderer”放在同一会话，第一次把 Contract/帧块/渲染闭环，减少跨会话契约反复。
- S2 把 Host 与 Converter UE 集成放在一起：输入/生命周期与 NativeBuild 启动/校验是同一个运行闭环；G1 双后端也在此完成。
- S3 把 Shipping、回归和验收放在最后，避免验收发现问题再跨会话往返。
- 合并后单会话更长：每个工作包（A/B/C 等）结束时都按 §14.2 留交接状态；上下文告急时在本会话内切分，不改变计划编号。
- 若 S1 的 VF 集成严重阻塞，优先按 C3/§8.3 的降级方案继续，不新开会话重做。
- S4 是 H6 首轮目视验收后新增的修复阶段：贴图颜色不正确，按门禁不能带病宣布 M2 完成；S4 只修视觉正确性，不扩功能、不改 ABI。

### 14.1 每个会话的开始提示

```text
读 docs/M2_PLAN.md，本次只做 Sx：<工作包列表>。
先读 §14.3 的交接状态和上一次交付；只读本区间相关代码。
允许构建/运行/快速冒烟并自行修复；不要扩展到其他 Sx 的工作包。
合并会话较长：每完成一个工作包就更新 §14.3，不要攒到最后。
未安装 UE 时停止 UE 工作并报告；不要用替代引擎或假截图。
未实际运行的结果不要写成通过。
S4 只允许修贴图/模型/颜色正确性：先保存 before 截图，不改 ABI 语义。
```

### 14.2 每个会话的结束条件

- 本会话计划完成的工作包 DoD 满足；未完成项写入 §14.3；
- 构建通过，快速冒烟通过；
- 更新 §14.3 的交接状态（只改状态，不改需求）；
- 留下：已完成内容、未完成项、已知问题、下个会话的第一条命令/文件；
- 不做本区间之外的“顺手重构”。

### 14.3 交接状态（新对话从这里继续）

| 会话 | 状态 | 已完成 | 已知问题 / 下一步 |
|---|---|---|---|
| S1 基础 + R0 渲染 | 完成（S1 范围） | 实测记录见 `docs/M2_ACCEPTANCE.md`：A2 + A3–A7（Editor/Game/打包 Development、插件暂存、日志、`m2-build.ps1`/`m2-smoke.ps1`、gitignore/LFS）；B1–B8（ABI v2、host-owned 帧块/4 块背压、插值、get_asset、get_runtime_info、坐标/FOV 单测、rd8 限值）；C1–C8（单 SceneProxy、RT 上传、图集/烘焙光照/雾/天空、相机、第三人称代理、HUD/截图、固定曝光）。m1-tests 30 用例通过（最终 1017 断言）；UE NullRHI 1000 tick（TPS 19.98–20.01，无 BUSY/fatal）；真实 26.3 地形与 DX11 截图已保存 | 遗留且不阻塞 S2：F5 未经实测、加载页未截图；D3D12 本次为 SM5（需补 SM6 targeted format）；R0 使用计划允许的 `FLocalVertexFactory`+烘焙顶点色降级；UE 仍用开发期 `dll_path`，NativeVersion/Adapter 校验与 Shipping/干净机在 S2/S3 完成 |
| S2 Host + Converter UE 集成 | 完成（S2 范围） | 实测记录见 `docs/M2_ACCEPTANCE.md` §8–§10：E1–E9（manifest/blocks/textures schema v2 且无绝对路径、adapter pin/`ue_adapter.json`、`package`/`assemble` 字节可复现、Bridge 校验 DLL/manifest/adapter hash 与 adapter pin、E9 5 用例）；D1–D6（输入仅经 ABI、20 次启停、菜单截图 `s2-menu.png`、settings 持久化、fatal exit 2、无音频依赖）；G1 静止与 auto-walk 两种对照均 match（同 tick 玩家/区块/section/方块样本一致） | 遗留且不阻塞 S3：鼠标轴与其余键位映射未自动化（W 键经 ABI 已实测）；D5 交互式对话框未在无人值守下验证（fatal 非零退出已测）；UE 可执行名仍为 `REIM.exe`，F2 的命名在 S3 决定；Shipping/干净机、性能/JVM/错误矩阵、G5 自动化在 S3 |
| S3 Shipping + 回归 + 验收 | 完成（S3 范围）；**M2 阻塞：F5 干净机 + H6 用户确认** | 实测记录见 `docs/M2_ACCEPTANCE.md` §11–§16：F1/F2/F3（`REIMRuntime` 目标、Development+Shipping BuildCookRun、app-local VC++、无 PDB/generated、DX12 SM6、Host `REIM-host.log`）；F4/E9 Shipping 5 用例；F6 `dumpbin` 依赖审计；F8 Shipping NativeBuild；G2 60 秒（TPS 19.94–20.04、exit 0、无 fatal）；G3（GT p99 0.007 ms、busy 0、RT 稳态 0.0009 ms）；G4 无 JVM；G5 自动化 3 用例通过；G6 零命中；G7 固定相机两次差异 0/255；H2 错误矩阵 11/11（minimize 实测）；H3 `m2-acceptance.ps1` 11 pass/0 fail/1 not_run | F5 未运行：本会话无干净 Windows 机器/证据；H6 用户尚未实际查看并确认画面。两者按门禁不得写成通过；需用户提供干净机或显式豁免并记录，然后重跑 `tools\m2-acceptance.ps1` 并补 §15/§16。下一条命令见 §14 Shipping 运行 |
| S4 贴图/模型正确性 | 完成（S4 范围）；**H6 用户暂时通过（2026-11-21，仍有待改进视觉问题）** | 实测记录见 `docs/M2_ACCEPTANCE.md` §17。S4-0 before/after 截图与像素样本（`build/m2/reports/s4/`）。S4-1/S4-2：`InitComponentVF(..., false)` + **修正材质 VertexColor 连接（RGB 输出为匿名输出，旧脚本 `"RGB"` 静默失败）** + UE 空间太阳方向 + sRGB→linear 顶点色 + 单 mip 图集（CreateTransient，nearest/clamp/NeverStream）。S4-3/4：Converter `variants`（`grass_block[snowy=true]`→`grass_block_snow`、雪层 layers）与 `geometry`（薄盒/十字/≤2 element 盒）；Runtime `find_state` 与显式 quad 发射。S4-5：`biome_tints.json`（client.jar 群系 + colormap），雪原草色 0x80B497 核对。S4-6：masked cutout + Preview discard；水用群系 water tint（抽查蓝色半透明）。S4-7：`tools/m2-visual.ps1` 接入 `m2-acceptance.ps1`，Preview/UE Dev/UE Shipping 均 pass。S4-8：extract→CMake→UE Dev/Shipping→package/assemble（字节可复现）→验收 12 pass/0 fail/1 not_run（F5） | H6 需用户重新目视确认（未确认前 M2 不得宣布完成）；F5 干净机仍阻塞。已知限制：UE 不透明 pass 双面（几何/整方块正面约定差异，R0 记录）、UE tonemap 较 Preview 约 13% 偏暗、biome 单色无插值、多 element/旋转模型仍 skip |

**状态写法**：`未开始` → `进行中` → `完成`；若阻塞写 `阻塞：<原因>`。只更新本表，不在这里写长篇总结。

### 14.4 会话边界（防止越界）

| 会话 | 只允许 | 明确不做 |
|---|---|---|
| S1 | M2-A/B/C：UE 工程、打包骨架、ABI v2、Bridge、R0 Renderer、Preview v2 | 输入/菜单、Converter UE 集成、Shipping 验收 |
| S2 | M2-D/E/G1：输入/UI/生命周期、UE Adapter pin、`package`/`assemble`、双后端对照 | 新渲染功能、Shipping 打包/干净机、验收记录 |
| S3 | M2-F/G余/H：Shipping、干净机、性能/错误路径、验收 | 新功能、Converter 范围扩张、重构 |
| S4 | M2-I：UE 顶点色/图集色彩、状态变体、雪层/植物/多层模型、biome tint、alpha/cutout、视觉回归 | 新 ABI 语义、新玩法、实体/方块实体/粒子/动画纹理、M3 功能 |

**交接原则**：ABI v2（`reim_abi.h`、`reim_render.h`）、坐标/单位、线程/所有权、manifest v2、`ue_adapter.json`/`native_version.json`/`native_build.json` schema 在 S1/S2 冻结（S1 冻结 Contract；S2 冻结 Adapter/NativeVersion）；后续会话只实现/修复，不重新设计。若必须改，先在本表记录原因，再同时改文档、头文件、Converter、Preview 与 UE。

---

## 15. S4 详细计划 — 贴图/模型正确性（H6 发现）

### 15.1 触发与证据（2026-11-21）

- 用户按 §14 的 Shipping 命令启动 `build\native-build\26.3\REIMRuntime\REIMRuntime.exe`，使用 `build\m2\acceptance\instance-g2.json` 进入出生点（XYZ 16/65/80，yaw 0，pitch 0），目视发现贴图/颜色不对。
- 实际画面（`build\m2\reports\m2-shipping-g2.png`、`s3-shipping-r0.png` 与本轮新截图一致）：
  - 草方块**顶面为灰色**、云杉树**叶为灰白**、整体几乎没有光照层次；
  - 泥土、南瓜、石头、草方块**侧面**颜色与图集一致（这些不依赖 tint；草侧的绿色镶边已烘焙在 `grass_block_side` 贴图里）；但雪地群系应使用的 `grass_block_snow` 变体未生效，见根因 2/3。
- 同一份 `mc_26.3.dll`（sha256 `4fb83550…`）与同一 `generated/26.3`，由 `REIMPreview.exe` 在同一位置渲染为**绿色草顶、深绿云杉叶**，说明数据链路与图集正确，差异在 UE 呈现层。
- 素材：`generated/26.3/atlas.png` 为 2048×64，内容为 `client.jar` 原生贴图（`manifest.json` 记录 `client_jar_sha256`）。**结论：不是“没有用原生素材”，而是 UE 没有应用 tint/光照，且 Converter/运行时未覆盖的状态变体与非整方块没有渲染。**
- 世界数据实测（解析 `dimensions/minecraft/overworld/region/r.0.0.mca`）：
  - 出生点地面：`grass_block[snowy=true]`（y=64）+ `snow[layers=1]`（y=65）；
  - 同区块：`short_grass`、`pumpkin`；附近 chunk(2,6) 有 `spruce_leaves`；
  - 即原生画面应为：雪地草方块 + 薄雪层 + 草、云杉。

### 15.2 根因

1. **P0 — UE 顶点色（tint + 烘焙光照）未生效**
   - `runtime/host/Source/REIMRenderer/Private/McRenderState.cpp` 在 `InitFromDynamicVertex` 之后再次调用
     `InitComponentVF(Mesh->VertexFactory.Get(), 0, true, Data)`。
   - UE 5.8 `Engine/Source/Runtime/Engine/Public/StaticMeshResources.h` 中 `bInOverrideColorVertexBuffer` 的语义是
     “make a vertex factory ready for **per-instance colors**”；`true` 走的是 per-instance/override 颜色路径，不是绑定 `ColorVertexBuffer` 的逐顶点颜色。
   - 结果：`VertexColor` 为白色 → 草顶（`block/grass_block_top` 是灰度贴图）变灰、云杉叶（灰度）变白、`BakeVertexColor` 的 tint 与方向光全部丢失。
   - 还需确认 `M_McBlock` 材质图确实把 `TextureSample(Atlas) × VertexColor` 接到 Emissive（材质为 Unlit）；若未接，同时修材质资产。
2. **P0 — 状态变体没有参与视觉解析**
   - `runtime/mc-native/world/chunk.cpp` 已解析方块 `properties` 与 biome palette，但世界映射只按方块名（`BlockTable::find(name)`），属性被丢弃。
   - `blocks.json` 每个方块只保留一个 `representative_variant`（`grass_block` 固定为 `snowy=false`），因此整个雪原都按普通草方块渲染。
3. **P0/P1 — 非整方块与多层模型被跳过**
   - `minecraft:snow`（`layers=1..7`，`thin_block`）、`short_grass`（`tinted_cross`）等 `full_block=false` → `render_class=skip`，完全不生成几何。
   - `converter/orchestrator/model_resolver.cpp` 只取第一个“整方块” element；草方块模型的 `grass_block_side_overlay` 层、雪层厚度、十字植物都被忽略。
4. **P1 — tint 为固定默认色，未使用 biome 数据**
   - `converter/orchestrator/model_resolver.cpp` 的 `default_tint()` 硬编码原版默认草/叶色（注释写明 “Per-biome tint arrives with biome data”）。
   - 运行时已解析 `biome_palette`/`biome_indices`，但无人消费；修好 P0-1 后，雪地群系会显示错误的绿色。
5. **P1 — 图集采样与色彩空间**
   - UE 侧 `ImportBufferAsTexture2D` 可能生成 mip/使用 wrap；需强制 `TA_Clamp`、单 mip、`TF_Nearest`、`NeverStream`，否则远处会跨 tile 渗色，与 Preview 不一致。
   - tint 字节为 sRGB；UE `VertexColor` 按线性处理，直接相乘会偏亮/偏灰；需要统一（转换到线性或材质内处理），Preview/UE 使用同一套规则。
6. **P1 — alpha/水面**
   - 树叶当前 `render_class=opaque`；十字植物需要双面 cutout；水固定 alpha 0.72 且无 biome 水色/动画（动画纹理 M2 不承诺）。

### 15.3 任务（S4-0 … S4-8）

| 编号 | 内容 | 主要文件 | DoD |
|---|---|---|---|
| S4-0 | 复现与基线：保存 Preview/UE 同位置 before 截图；记录像素采样点（草顶、云杉叶、雪、泥土、南瓜） | `build/m2/acceptance/g2/`、`build/m2/reports/` | before 图与像素值入报告；后续修复有对照 |
| S4-1 | 修 UE 顶点色绑定（如 `InitComponentVF(..., false, ...)`，或直接使用 `InitFromDynamicVertex` 的默认绑定）；确认材质 `Atlas × VertexColor` | `McRenderState.cpp`、`M_McBlock.uasset` | 草顶恢复绿色、云杉叶恢复深绿、面与面之间有烘焙明暗 |
| S4-2 | 图集采样/色彩：clamp、单 mip、nearest、NeverStream；sRGB→linear 统一 | `McWorldRendererComponent.cpp`、材质、Preview 着色器 | 无跨 tile 渗色；同场景 Preview/UE 颜色差异在阈值内 |
| S4-3 | 状态变体端到端：解析并传递 `properties`，至少 `grass_block[snowy=true]` → `grass_block_snow`；数据结构支持按状态取视觉 | `chunk.cpp`、`simulation.cpp`、`block_table.*`、`blocks.json` schema | 雪地草方块侧面为 `grass_block_snow`（带雪缘） |
| S4-4 | 多层/非整方块几何：多层 element、薄方块（`snow_height*`）、十字植物（`short_grass`/花/蕨）；双面 cutout；剔除规则 | `model_resolver.cpp`、`extract.cpp`、`blocks.json`、`section_mesh.cpp` | 出生点可见雪层与草；无洋红缺失块 |
| S4-5 | biome tint：按方块位置取 biome；Converter 产出草/叶/水颜色表；雪地群系使用对应色 | `chunk.cpp`、`simulation.cpp`、`model_resolver.cpp`、生成物 | 雪地草/叶颜色与原生一致；Preview/UE 同色 |
| S4-6 | Alpha/水面：树叶/植物 cutout；水 tint/alpha | 材质、`section_mesh.cpp`、生成物 | 树叶不是整块不透明；水色正确（动画除外） |
| S4-7 | 视觉回归工具：固定相机像素断言（草/叶/雪/水颜色阈值、无洋红块、可选 Preview/UE 对照），接入一键验收 | `tools/m2-visual.ps1`（新）、`tools/m2-acceptance.ps1`、报告 JSON | 报告含逐项 pass/fail 与截图；失败可定位 |
| S4-8 | 重建与 H6 重跑：extract → CMake → UE Dev/Shipping → package/assemble → H6；更新 `M2_ACCEPTANCE.md` | 工具链与文档 | 用户确认 H6；未确认不得写通过 |

### 15.4 实施顺序与命令

1. S4-0（复现/before 截图）→ S4-1（顶点色）→ S4-2（采样/色彩）：先拿到肉眼可见的最大修复。
2. S4-3 + S4-4（状态与几何）→ S4-5（biome tint）→ S4-6（alpha/水）。
3. S4-7（回归脚本）→ 重建 → S4-8（H6）。

```powershell
# 数据（若 schema/变体/几何有变更）
build\m2\bin\reim-convert.exe extract --profile 26.3 --out generated\26.3

# C++ Runtime + Converter
cmake --preset m2-release
cmake --build --preset m2-release

# UE（按需 Development/Shipping）
powershell -ExecutionPolicy Bypass -File tools\m2-build.ps1 -Config Release
powershell -ExecutionPolicy Bypass -File tools\m2-build.ps1 -Package -PackageConfig Shipping

# NativeVersion/NativeBuild（工具链，不手工拷贝）
powershell -ExecutionPolicy Bypass -File tools\m2-package.ps1 -Generated D:\EngineeringDocuments\REIM-Launcher\generated\26.3 -VerifyRepro

# Preview before/after（同位置）
build\m2\bin\REIMPreview.exe --world "D:\Minecraft\.minecraft\saves\REIM-M1-26.3" --generated "D:\EngineeringDocuments\REIM-Launcher\generated\26.3" --render-distance 6 --hidden --frames 150 --screenshot build\m2\reports\h6-preview.png

# UE H6（PowerShell 用一行或反引号续行，不能使用 cmd 的 ^）
.\build\native-build\26.3\REIMRuntime\REIMRuntime.exe "-reim-instance=D:\EngineeringDocuments\REIM-Launcher\build\m2\acceptance\instance-g2.json" -windowed -log -ResX=1280 -ResY=720
```

### 15.5 验收与证据

- 目视：出生点同视角下，雪地草方块侧面带雪缘、雪层与草可见、云杉叶为深绿、画面有烘焙明暗层次；南瓜/泥土/石头保持正确。
- 像素断言（建议阈值，脚本实现时可调）：
  - 非雪地草顶绿色：`G > R + 15` 且 `G > B + 15`（出生点若全为雪，用 `--spawn` 或移动到附近非雪草方块取样）；
  - 云杉叶：绿色且亮度低于草顶（tint 生效）；
  - 雪（出生点地面）：RGB 均 > 180 且低饱和；
  - 任意帧无洋红 checker（`R>200, G<40, B>200`）。
- 回归：同一 H6 场景 Preview/UE 固定相机截图差异可解释；`m2-acceptance.ps1` 不含 not_run 的视觉项。
- 记录：S4 完成证据写入 `docs/M2_ACCEPTANCE.md`（新增 S4 小节），H6 仍以用户确认为准；F5 干净机不受影响。
- ABI v2 与坐标/单位契约不变；NativeVersion/NativeBuild 由工具链产出且 hash 校验通过。

### 15.6 非目标（S4 不做）

- 新 ABI 字段或语义；把游戏逻辑/世界解析搬进 UE。
- 完整非整方块覆盖（楼梯/台阶/栅栏/门/墙/红石等全部朝向与连接）、方块实体、实体、粒子、天气、动画纹理、OIT、PBR/阴影/Lumen。
- 生物群系混合、平滑光照（AO）、正式 UI/安装器等 M3 范围。
