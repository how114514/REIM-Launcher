# REIM 开发指南

> 配套文档：`ARCHITECTURE.md`（架构）、`TECH_STACK.md`（选型/UE 分发）、`ROADMAP.md`（阶段）、`COMPATIBILITY.md`（兼容矩阵）。  
> 本文说明开发环境、仓库边界、构建方式、编码/ABI 规则和验收流程。

---

## 1. 两种环境，不要混淆

| | 开发/CI 环境 | 用户环境 |
|---|---|---|
| Unreal Engine | **M2 起**需要 UE 5.8.4 + Editor/UBT | **不需要** |
| JDK | **M1 起**需要 JDK 25（Converter 分析/准备） | **不需要**；REIM 不启动 Java Edition |
| MSVC/CMake | **M1 起**需要 | 不需要 |
| 产物 | Development/Shipping | **Shipping 发布包** |
| 用途 | 编译/Cook/Package/调试/差分 | 只运行 |

**规则**

- 开发机负责编译、Cook、Package、差分、截图、性能。
- 用户只拿 `REIM Native Runtime` Shipping 发布包；不安装 UE、Editor、插件、源码、JDK、MSVC、CMake。
- 不要把 Development 包、Editor 工程、`.uproject`、PDB 或未 Cook 的内容发给用户。
- 干净机验收：未安装 UE/JDK/MSVC/CMake，只装 VC++ 运行库，启动 Shipping 包。
- **REIM 不启动 Java Edition**：没有 javaw/classpath/JVM 游戏路径；Java Edition 文件只作输入。

---

## 2. 仓库布局与目录分类

> 完整分类、逐目录清单、入库规则与规划目录见 `docs/STRUCTURE.md`；本节只给顶层视图。

```text
REIM-Launcher/
├─ README.md / CMakeLists.txt / CMakePresets.json / play-m3.bat
├─ docs/                     # 正式文档：架构/路线/计划/验收/开发/兼容/目录分类
├─ conversion-decisions/     # 转换决策审计
├─ converter/                # Converter/分析/数据/Patch
│  ├─ orchestrator/          # reim-convert.exe
│  ├─ java-analyzer/         # JDK 25 类分析
│  ├─ adapters/ ledger/ patches/ samples/
├─ runtime/
│  ├─ contract/              # C ABI / 快照 / UI / 音频 / 游戏头文件
│  ├─ mc-native/             # anvil/asset/core/data/game/mesh/nbt/samples/src/ui/util/world
│  ├─ headless/ preview/ scenario/
│  └─ host/                  # UE Host：Source/Config/Content/Plugins 入库；
│                            # Binaries/Intermediate/Saved/DerivedDataCache/Build 不入库
├─ tests/                    # 单元/场景/fixture
├─ tools/                    # 验收/差分/打包/审计脚本
├─ third_party/              # 固定版本 + 许可清单
├─ generated/ work/ build/ logs/   # 本机产物（不入库）
└─ versions/ libraries/      # 用户/Mojang 输入（不入库）
```

规划中目录（`launcher/`、`converter/translate/`、`runtime/njs/` 等）见 `docs/STRUCTURE.md` §11；不要提前创建空目录。

**边界**

- Launcher 不链接 UE/JRT；不解析 Minecraft 类；不启动 Java Edition。
- Converter 不链接 UE；只产出 DLL/数据/manifest。
- 游戏 DLL 不包含 UE 头文件/UObject；只依赖 C++20 + JRT + 第三方库。
- UE Runtime 不包含 Minecraft 游戏规则；只消费 C ABI/快照契约。
- C ABI 只用于 Runtime Host ↔ Native DLL；Launcher/Converter 用 CLI/IPC/manifest/files。
- 生成代码不手改；修改进入 `patches/`。

---

## 3. 开发环境

### 3.1 最小开发环境（按阶段）

| 工具 | 版本 | 从哪个阶段开始必需 | 用途 |
|---|---|---|---|
| Windows | 10/11 x64 | 全部阶段 | 首发平台 |
| Git | 最新 | M1 | 版本管理 |
| CMake | 3.28+ | M1 | Runtime / Converter / 临时 Renderer 构建 |
| MSVC + Windows SDK | MSVC 14.44.35207、Windows SDK 10.0.26100 | M1 | C++ 编译。**只装 VS 2022 Build Tools，不装 Visual Studio IDE**（M2/M3 实测版本） |
| JDK | 25（Temurin） | M1 | Converter 分析/数据生成/差分 oracle；仅离线使用 |
| Python | 3.12 | M1 | 脚本/差分/报告 |
| Unreal Engine | 5.8.4 | M2 | UE Host/Renderer、Cook/Package；M1 不需要 |
| Qt | 6.8 LTS | M4 | Launcher UI；M1–M3 不需要 |
| Git LFS | 最新 | M2（首次引入大文件时） | 二进制资产/大文件 |

**最小起步（只做 M1）**

- Windows 10/11 x64 + Git + CMake + **VS 2022 Build Tools**（MSVC + Windows SDK）+ JDK 25 + Python 3.12；
- 另需用户自有的 Minecraft Java Edition **26.3 文件**与至少一个真实世界存档（见下）；
- 不需要 Unreal Engine、Qt、Qt Creator 或完整 Visual Studio IDE。

**M1 需要的游戏输入（不是开发工具）**

- 用户自有 26.3 Java Edition 文件：version JSON、client.jar、libraries、assets，用于 Converter 摄取/分析、数据提取和资源读取；
- 至少一个 26.3 世界存档（`.minecraft/saves/...` 或自行创建的测试世界），用于加载真实区块与地形；
- 26.3 server jar：用于运行官方数据生成器 `--reports`（生成注册表/方块表），也用于原版差分 oracle；若你已有可用的 reports，则可不重复生成；
- 准备方式：用官方启动器安装/更新到 26.3，或使用你已有的 26.3 文件；这些文件只作为输入/规格，**REIM 不启动 Java Edition**，也不得提交到仓库或随产品分发。

**关于 Rider / Visual Studio**

- Rider 是 IDE，**不提供 MSVC 编译器、Windows SDK、CMake 或 UE**；
- 完整 Visual Studio IDE 可以不装，但 **VS 2022 Build Tools 必须装**；Rider 检测并使用这套 MSVC 工具链；
- 不要把“装 Rider”理解为“无需 MSVC”；完全不用微软工具链会与 UE/UBT 的 Windows 构建基线冲突；
- Qt 通过 CMake 使用，不需要安装 Qt Creator。

### 3.2 可选

- Rider（M1 起；替代 Visual Studio IDE，不替代 MSVC 工具链）；
- Unreal Insights（M2 起）；
- RenderDoc/PIX（M2 起，渲染调试）；
- Tracy（M3 可选，Core 性能）；
- Visual Studio IDE、Qt Creator：**不需要**。

### 3.3 用户环境（发布）

- 不安装 UE/Editor/JDK/MSVC；
- VC++ 2015–2022 x64 运行库（app-local 或预置）；
- DX12/SM6 为唯一目标，DX11 不支持/不验收；Windows 10/11 x64；
- 正常 GPU 驱动。

---

## 4. 构建边界

| 产物 | 工具 | 说明 |
|---|---|---|
| `REIM.Launcher.exe` | CMake + MSVC + Qt | 不链接 UE/JRT |
| `reim-convert.exe` | CMake + MSVC | 不链接 UE |
| `reim-analyze.jar` | JDK 25 + ASM/Vineflower | 仅分析/导出；不进入 Runtime |
| `mc_<version>.dll` | CMake + MSVC | C++20；`/MD`；关闭 C++ 异常；无 UE |
| `REIMRuntime.exe` + pak | UBT + UAT `BuildCookRun` | UE 5.8.4；Shipping |
| `contract/reim_abi.h` | 纯 C 头文件 | **Runtime Host ↔ Native DLL** 的版本化 ABI；CI 校验 |

### 4.0 直接游玩（UE Host）

M3 的玩家面向入口是 UE Host；开发机上用仓库自带的启动脚本即可（自动启动 Zen、写入 play instance、拉起 Shipping RC）：

```bat
cd /d D:\EngineeringDocuments\REIM-Launcher
play-m3.bat
:: 或
powershell -ExecutionPolicy Bypass -File tools\play-m3.ps1
:: Development 版
powershell -ExecutionPolicy Bypass -File tools\play-m3.ps1 -Config Development
```

当前只接入 R0 世界渲染：可以进入存档/移动/看世界；HUD、背包、实体、音频、第一人称的 UE Host 侧尚未实现。想跑自动化/验收用 `tools\m3-acceptance.ps1`，不要用游玩入口代替。

### 4.1 UE 工程

- `.uproject` + `Source/REIMGame`、`REIMBridge`、`REIMRenderer`、`REIMUI`、`REIMAudio`、`REIMInput`、`REIMAssets`。
- `REIMGame` 使用空舞台地图；不把 Minecraft 世界对象变成 Actor/Component。
- 插件按需启用；Shipping 不包含 Editor-only 模块。
- 资产引用通过编辑器/配置绑定；不在代码里硬编码资源路径。
- 功能尽量组件化；组件挂到对应 Actor/Subsystem，便于统一管理。

### 4.2 UE 打包（Shipping）

日常使用脚本（S3 实测；M3 阶段 5 增加 `m3-package.ps1`，输出 `build\m3\ue-shipping-package`）：

```bat
:: UE 运行/打包前置：启动 Zen storage server（打包结束可能停止共享 Zen）
"%UE_ROOT%\Engine\Binaries\Win64\ZenLaunch.exe" -Sponsor=None
powershell -ExecutionPolicy Bypass -File tools\m3-package.ps1 -Config Shipping
powershell -ExecutionPolicy Bypass -File tools\m3-acceptance.ps1 -World <save-copy>
```

`m3-package.ps1` 只做 BuildCookRun/Stage/Pak；`m3-acceptance.ps1` 运行构建、测试、场景、审计、UE R0 与三 Host 一致性，并把未执行项写成 `not_run`。`m2-build.ps1` 仍可用于 M2 历史路径。

脚本内部调用（路径按实际安装调整；`REIMRuntime` 是 UE Game target）：

```bat
"%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" ^
  BuildCookRun ^
  -project="D:\EngineeringDocuments\REIM-Launcher\runtime\host\REIM.uproject" ^
  -target=REIMRuntime ^
  -noP4 -platform=Win64 -clientconfig=Shipping ^
  -build -cook -stage -pak -archive ^
  -archivedirectory="D:\EngineeringDocuments\REIM-Launcher\build\m2\ue-shipping-package" ^
  -applocaldirectory="D:\EngineeringDocuments\REIM-Launcher\build\m2\applocal" ^
  -nodebuginfo -unattended
```

- UE 5.8 的 `IncludeAppLocalPrerequisites` 不再被 BuildCookRun 读取；脚本把 Engine 的 `AppLocalDependencies` 复制到无空格目录并显式传 `-applocaldirectory`。`-nodebuginfo` 排除 PDB。
- DX12 targeted shader format 为 `PCD3D_SM6`（`DefaultEngine.ini` 替换 BaseEngine 的 SM5）；DX11 保留 SM5 冒烟。
- Shipping 的 UE 引擎日志被编译掉（installed engine），诊断日志看实例目录的 `REIM-host.log`。
- 最终组合由 `reim-convert assemble` 生成到 `build/native-build/26.3`；Development 版本在 `build/native-build-dev/26.3` 供自动化。
- 每次 Release 在**未安装 UE** 的干净机跑冒烟（F5，尚未执行）。

### 4.3 游戏 DLL

```bat
cmake -S runtime -B build/runtime -G "Visual Studio 17 2022" -A x64
cmake --build build/runtime --config Release
```

- 输出 `mc_<version>.dll` + PDB + 符号映射。
- 不链接 UE；不包含 UE 头文件；跨 Runtime Host/DLL 边界用 C ABI（不是 Launcher/Converter 通用协议）。
- 构建配置：C++20、`/MD`、关闭异常/RTTI（转换代码用自建异常模型）；第三方库同配置。

---

## 5. Native 化工作流（Developer Mode）

> 命令是**目标接口/示意**，随 `reim-convert` 实现调整；当前仓库尚未实现。
> **User Mode 不走这里**：普通用户直接使用官方预编译 Native Runtime，不安装 JDK/MSVC/CMake、不编译。

```text
# 1. 准备（JDK 25；用户自有 26.3 文件）
reim-convert prepare --profile 26.3 --minecraft-dir <用户安装> --out work/26.3

# 2. 分析/差异
reim-convert analyze --profile 26.3 --out work/26.3
reim-convert diff --old 26.2 --new 26.3 --out work/diff-26.2-26.3

# 3. 分诊/产出（按子系统）
reim-convert produce --profile 26.3 --decisions config/decisions-26.3.json

# 4. 构建 DLL
reim-convert build --profile 26.3 --out build/26.3

# 5. 差分/截图
reim-convert verify --profile 26.3 --cases tests/cases

# 6. 注册到 Launcher / 打包 Runtime
```

**规则**

- 每次 Native 化记录输入 hash、工具版本、Patch 集、决策文件、验证结果。
- 同输入/同工具/Patch 应可复现；生成代码固定遍历顺序/命名。
- 失败保留中间产物与日志；错误定位到类/方法/Patch/构建阶段。
- 不把反编译源码提交/发布；只保留必要的符号/摘要/补丁。
- 不把 Native 化结果默认提交仓库；按版本缓存。

---

## 6. 编码与 ABI 规则

1. **C++20**；UE 侧遵循 Epic Coding Standard；Core/DLL 用清晰、可维护的 C++。
2. **Core/DLL 零 UE 依赖**：不 include `CoreMinimal.h`、UObject、`UWorld`、`AActor`、`Engine/`。
3. **C ABI 边界**：只用于 Runtime Host ↔ Native DLL；只传 POD/句柄/不可变快照/回调；不传 C++ 对象、STL、UE 容器。
4. **异常**：不抛 C++ 异常穿越 ABI/UE；转换代码用挂起异常/展开模型；原生适配用错误码。
5. **线程契约**：DLL 内部 20 TPS 模拟线程；`fetch_frame` 只在 GT；`submit_*` 任意线程（内部队列）；模拟不被渲染 FPS 驱动；写进头文件。
6. **所有权**：跨 DLL 分配同侧释放；使用 allocator/release 回调；世代号检测迟到更新。
7. **资源引用**：UE 资产通过编辑器/配置绑定；代码只持有句柄/路径参数，不硬编码。
8. **生成代码**：不手改；修改进 Patch；生成器确定性。
9. **版本隔离**：不散落 `#if MC_VERSION`；差异进 profile/适配器/Patch。
10. **性能**：先测量；热路径用池化/批处理/SoA；不做无证据的抽象。

---

## 7. 测试与验收

| 层级 | 内容 | 工具 |
|---|---|---|
| 单元 | JRT/集合/字符串/数学/异常/NBT/调色板/光照/RNG/协议 | Catch2 |
| 原生化 | 翻译/生成快照、编译、符号映射、Patch 重放 | C++/Python |
| Golden | 存档/区块/协议 trace/UI/网格 hash | C++/Python |
| 集成 | Launcher 启动/下载；Runtime+DLL；UE 渲染 | 脚本/UE Automation |
| 差分 | 原版 server/client oracle vs C++ | Java/Python harness |
| 视觉 | R0/R1/R2/UI 截图；资源包 | 图像 diff + 人工 |
| 性能 | tick/帧/内存/加载/网络/GC | Insights/benchmark |
| 稳定性 | 8h/24h、存档循环、断线重连、崩溃恢复 | 自动脚本 |
| 安全 | 恶意包/NBT/ZIP/命令/fuzz | libFuzzer/ASan |
| 分发 | 干净机（无 UE）Shipping 冒烟 | 手动/CI |

**门禁**

- PR：编译、单测、ABI、快速原生化快照。
- Nightly：原生化样本、差分核心、UE 集成、截图、性能。
- Release：全量差分、存档/协议/资源矩阵、**Shipping 干净机**、8h soak、许可证审计。
- 版本升级：diff、Patch 重放、回归、旧版本兼容矩阵。

---

## 8. 调试与可观测性

- 生成代码保留 Java 类/方法/行号映射；PDB + 符号映射。
- 日志分类（world/net/save/render/ui/gameplay）；不记录令牌/隐私。
- 崩溃上下文：最近 tick、世界/存档路径、内容/Patch hash、线程、Java/C++ 栈。
- Unreal Insights：tick/网格/上传/网络/存档；Core 可选 Tracy。
- 调试可视化：区块状态、光照、网格统计、Bridge 队列、GC 停顿。
- 回放/录制：输入/包/种子/内容 hash。

---

## 9. 资产与法律

- 不提交/分发 Mojang JAR、纹理、声音、模型、反编译源码。
- 原生化在用户本机对自有文件执行；CI 使用受控输入，产物不公开分发。
- 预编译原生化产物、UE Shipping 分发、账号/商标使用分别法律评审。
- 第三方依赖固定版本 + LICENSE 清单。
- 用户资源包/数据包视为不可信输入：路径/ZIP/大小/深度/符号链接限制。
