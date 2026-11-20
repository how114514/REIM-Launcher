# REIM 技术选型与 Unreal 分发

> 本文是 `ARCHITECTURE.md` 的配套文档，聚焦“每个交付物用什么技术”和“Unreal Engine 如何从开发环境变成用户可运行的 Runtime”。  
> 基线：Minecraft Java Edition 26.3 / Unreal Engine 5.8.4 / Windows x64 首发。

---

## 1. 总原则

REIM 是 **Minecraft Native Launcher**，不是 Java Edition Launcher。Java Edition 文件只作输入/规格，不可启动。三个技术边界不能混为一谈：

| 边界 | 产物 | 是否面向用户 | 是否依赖 UE | 是否依赖 JVM |
|---|---|---|---|---|
| **REIM Launcher** | `REIM.Launcher.exe` | 是 | 否 | **否**（JDK 只属于 Converter/Developer Mode） |
| **REIM Converter / Native Toolchain** | `reim-convert.exe` + JVM 助手 | Developer/Research Mode | 否 | Native 化时需要 |
| **REIM Native Runtime** | UE **Shipping** 发布包 | 是（**唯一可启动目标**） | **内含，用户无需安装** | 运行时不需 |
| **C++ 游戏逻辑** | `mc_<version>.dll` | 随 Runtime 包 | 否 | 否 |

**核心规则**

1. Launcher 不嵌入 UE、不承载游戏逻辑、**不启动 Java Edition**。
2. Converter 不链接 UE；它只产出 C++ 游戏逻辑/数据/manifest。
3. UE 只在开发/CI 侧用于编译、Cook、Package；用户侧只有 Shipping 发布包。
4. C++ 游戏逻辑只通过版本化 C ABI 与 UE Runtime Host 通信；**C ABI 不是 Launcher/Converter 的通用协议**。
5. 用户不需要安装 UE、Editor、UBT、插件、源码、JDK、MSVC、CMake。

---

## 2. REIM Launcher

| 领域 | 选择 | 说明 |
|---|---|---|
| 语言 | C++20 | 与 Converter/DLL 统一；不引入第二套运行时 |
| UI | Qt 6.8 LTS（Widgets/QML） | 成熟、跨平台、原生控件；LGPLv3 动态链接/商业许可需合规 |
| 构建 | CMake + MSVC 14.50 | 与 Converter/DLL 一致 |
| HTTP | Qt Network 或 libcurl | 下载、重试、代理、断点续传 |
| JSON | nlohmann/json（`JSON_NOEXCEPTION`） | 版本 JSON/manifest/配置 |
| 哈希/加密 | OpenSSL 3 LTS | SHA-1/SHA-256、OAuth PKCE、令牌保护 |
| 日志 | spdlog | 结构化日志、轮转 |
| 存储 | JSON + 目录（可选 SQLite） | 首版不引入数据库 |
| 进程 | Win32 `CreateProcess` + Job Object | 生命周期、崩溃回收、日志重定向 |
| 安装器 | WiX/NSIS | 签名、卸载、自更新 |
| 平台 | Windows 10/11 x64 首发 | Linux/macOS 后置 |

**职责边界**

- 账号、Minecraft 版本信息、本地 Java Edition 文件检测/准备、Native 化任务、Native 版本/实例/Runtime 管理、启动 Native Runtime、进程监管、日志/崩溃/更新。
- **不启动 Java Edition、不构造 classpath、不管理 Java 游戏运行时。**
- 不解析 Minecraft 类，不做游戏逻辑，不链接 UE。
- Native 化只是“调用 Converter 子进程 + 展示进度 + 注册结果”，不在 Launcher 进程内编译。

---

## 3. REIM Converter / Native Toolchain

| 领域 | 选择 | 说明 |
|---|---|---|
| 编排 | C++20 CLI（`reim-convert`） | 任务状态、缓存、日志、manifest |
| 分析/导出 | Java 25（Temurin）+ ASM + Vineflower | 类清单、字节码 hash、反编译（人工阅读）、数据生成器、导出器 |
| 翻译/生成 | C++20（`reim-translate`/生成器） | 按子系统选择：源码/字节码/IR/代码生成 |
| Patch | 文本 Patch + 符号定位 | 按 MC 版本维护，可重放/三路合并 |
| 构建 | CMake + MSVC 14.50 | 产出 `mc_<version>.dll`，不需要 UE |
| 差分 | C++/Python + Java oracle | server/client harness、截图、存档往返、协议 trace |
| 依赖 | ASM、Vineflower、zlib-ng、OpenSSL、PCRE2 等 | 仅开发/原生化阶段 |

**关键点**

- JDK 25 只在 Native 化阶段使用；Native Runtime 不加载 JVM。
- **User Mode（普通用户）**：直接使用官方预编译 Native Runtime；不安装 JDK/MSVC/CMake，不现场编译 Minecraft。
- **Developer/Research Mode**：本地分析/转换/编译/差分；需要 JDK 25 + MSVC/CMake；不需要 UE。
- Native 化结果按 `(输入 hash, 工具版本, Patch 集, profile)` 缓存；安装/更新时执行，不在每次启动时执行。
- 官方预编译 Native Runtime 是 User Mode 默认路径；本地 Native 化是 Developer Mode。两者都要法律评审。

---

## 4. C++ 游戏逻辑 / Runtime Support

| 领域 | 选择 | 说明 |
|---|---|---|
| 语言 | C++20 | UBT 的 UE 侧也用 C++20 |
| 构建 | CMake + MSVC 14.50 | `/MD`；关闭 C++ 异常；无 UE 头文件 |
| 导出 | Runtime Host ↔ Native DLL 的 C ABI | `reim_get_api`/POD/句柄/回调；不是 Launcher/Converter 通用协议 |
| 并发 | std::thread/atomic + Asio | 服务端 20 TPS、客户端 20 TPS、worker pool、网络；模拟与渲染 FPS 独立 |
| 压缩 | zlib-ng | Anvil/协议 |
| 加密 | OpenSSL 3 | SHA/AES/RSA/SecureRandom |
| JSON | nlohmann/json | 数据包/资源包 |
| 正则 | PCRE2 | 比 std::regex 更接近 Java 语法 |
| 日志 | spdlog | Core 日志 + 宿主桥接 |
| GC | Boehm（v0）/自研（目标） | 仅在翻译分支需要；详见 ARCHITECTURE §5.4.8 |
| 测试 | Catch2 v3 | 无 UE 单测/golden |

**规则**

- 不包含 `CoreMinimal.h`、UObject、`UWorld`、`AActor` 或任何 `Engine/` 头文件。
- 跨 ABI 不传 C++ 对象/STL/UE 容器；不抛 C++ 异常。
- 可在没有 UE 的 headless/差分环境中运行。
- 生成代码不手改；修改进入 Patch 集。

---

## 5. REIM Runtime（UE 侧）

| 领域 | 选择 | 说明 |
|---|---|---|
| 引擎 | **UE 5.8.4** | 固定版本；开发机/CI 安装 |
| 构建 | UBT + UAT `BuildCookRun` | Shipping 打包 |
| UI | Slate/UMG | 游戏内 UI/调试；Launcher 不用 UE |
| 渲染 | RHI/RDG/SceneProxy/自定义 VertexFactory | R0 最小 Native → R1 → R2 → R3 |
| 音频 | UE Audio / `USoundWaveProcedural` | 运行时解码/播放 |
| 输入 | Enhanced Input + 原始输入 | GT 采集，转发客户端模拟线程 |
| 纹理 | `UTexture2D::CreateTransient`/纹理数组 | 资源包运行时上传 |
| 资产 | Cooked pak/IoStore + 用户资源缓存 | 不依赖 Editor 导入 |
| 平台 | Windows 10/11 x64 | DX12/SM6 唯一目标；DX11 不支持/不验收 |
| 测试 | UE Automation + Catch2 | 集成/渲染/性能 |

**UE 侧模块**

- `REIMGame`：生命周期、GameInstance/Subsystem、空舞台。
- `REIMBridge`：C ABI 适配、快照获取、模拟线程/GT 同步。
- `REIMRenderer`：SceneProxy、材质/Shader、RDG、后处理。
- `REIMUI`：Native UI 模型/绘制数据呈现（R0 不做，R1 最小 HUD，R2 完整，R3 可选 Slate）。
- `REIMAudio`：声音事件 → UE 音频。
- `REIMInput`：输入采集 → 客户端模拟线程。
- `REIMAssets`：资源包/纹理/音频/字体运行时加载。

---

## 6. Unreal Engine：开发环境 vs 用户 Runtime

### 6.1 结论

**用户不需要安装 Unreal Engine 或 Unreal Editor。**  
UE 是 REIM 的开发、编译、Cook、Package 和 Runtime 基础；用户只运行 REIM 发布的 **Shipping 打包产物**。

```text
开发/CI：
  UE 5.8.4 (Editor + UBT)
      ↓ Build / Cook / Stage / Package (Shipping)
  REIM Runtime 发布包
      ↓
用户：
  REIM Launcher → REIM Runtime 发布包 → REIMRuntime.exe
```

### 6.2 Development vs Shipping

| | Development / Editor | Shipping |
|---|---|---|
| UE 安装 | 需要 | **不需要** |
| 用途 | 开发/调试/Cook | 玩家运行 |
| 控制台/统计/自动化 | 保留 | 剥离 |
| 可独立分发到无 UE 机器 | 否 | **是** |
| 调试符号 | 可附带 | 单独归档 |
| 体积 | 大 | 较小（Cooked） |
| 输入 | Editor/Game | 发布包 |

> 不要把 Development 包当作发布包；验收必须在**未安装 UE** 的干净机上跑 Shipping。

### 6.3 打包流水线

UE 的 UAT `BuildCookRun` 阶段：

1. **Build**：编译目标平台可执行文件。
2. **Cook**：把资产转换为目标平台格式。
3. **Stage**：把可执行文件 + Cooked 内容复制到 staging 目录。
4. **Package**：打包为平台分发格式（`.pak`；启用 IoStore 时 `.utoc`/`.ucas`）。
5. **Archive**：输出到发布目录。

参考命令（路径/参数按项目调整）：

```bat
RunUAT.bat BuildCookRun ^
  -project="C:\REIM\REIM.uproject" ^
  -noP4 -platform=Win64 -clientconfig=Shipping ^
  -cook -allmaps -build -stage -pak -archive ^
  -archivedirectory="C:\Builds\REIM"
```

- 启用 IoStore 时按项目设置加 `-iostore`；产物为 `.utoc`/`.ucas`。
- Shipping 会剥离控制台/统计/自动化；游戏内必须提供退出/设置 UI。
- 打包后的 Runtime 与 Editor/项目源码无关；用户只需要发布目录。

### 6.4 运行库与用户环境

| 依赖 | 是否需要 | 处理 |
|---|---|---|
| Unreal Engine / Editor | **否** | 不安装、不随包提供 |
| Microsoft Visual C++ 2015–2022 x64 Redistributable | **是** | Project Settings → Packaging → Prerequisites：**app-local** 副本或预置安装器 |
| DirectX End-User Runtime（旧版 D3DX/XAudio/XInput） | 按需 | 现代 DX12 项目通常不需要；若使用旧组件再附带 |
| GPU 驱动 | 是 | 用户自备；DX12/SM6 唯一目标；DX11 不验收 |
| .NET | 运行时不要求 | UE 工具链在开发/CI 侧需要；发布包不应依赖 .NET 工具 |
| Windows | Windows 10/11 x64 | 首发矩阵 |

**推荐**：使用 “Include app-local prerequisites”（把 VC++ 运行库放在游戏 EXE 旁），避免用户机器缺少运行库；或提供预置安装器。两者选其一，并在干净机验证。

### 6.5 插件与模块

- 项目 `Plugins/` 在开发工程中参与编译/Cook；最终**打进 Runtime 发布包**。
- 用户不安装插件、不打开 Editor、不访问插件市场。
- Shipping 不包含 Editor-only 插件/模块；插件代码在打包时编译。
- 插件/模块与 UE 5.8.4/包版本绑定；不做用户侧热插拔。
- `RunUAT BuildPlugin` 是“把插件分发给其他 UE 开发者”的工具，不是 REIM 用户侧安装方式；REIM 的 Runtime 是完整游戏包。

### 6.6 Runtime 发布包布局（建议）

```text
REIMRuntime/
├─ REIMRuntime.exe
├─ REIMRuntime/Content/Paks/...        # .pak / .utoc / .ucas
├─ mc_<version>.dll
├─ manifest.json                       # 版本/ABI/内容 hash/能力
├─ Prerequisites/                      # app-local VC++ 运行库（可选）
├─ Config/                             # 默认设置
├─ Licenses/                           # UE/第三方/声明
└─ logs/                               # 运行时生成
```

- Launcher 负责下载、校验、解压、更新、启动；用户不手动安装 UE。
- 更新按发布包/差分进行；UE 升级只影响开发/CI 与新发布包。

### 6.7 干净机验收

发布前必须测试：

1. 一台**未安装 UE/Editor** 的 Windows 10/11 x64 机器；
2. 只安装 VC++ 运行库（或使用 app-local）；
3. 从 Launcher 安装/启动 Runtime；
4. 进入世界、渲染、输入、音频、退出；
5. 无 Editor/引擎路径依赖、无缺失 DLL、无开发者控制台依赖。

---

## 7. 依赖与许可证

| 依赖 | 用途 | 许可/注意 |
|---|---|---|
| Qt 6.8 LTS | Launcher UI | LGPLv3（动态链接）或商业许可 |
| UE 5.8.4 | Runtime | Epic EULA/分成；发布前确认 |
| Temurin JDK 25 | Converter 分析/数据生成/差分 oracle | GPLv2+CE；可随工具链分发 |
| MSVC 14.50 / Windows SDK | 构建 | Microsoft 许可 |
| ASM | 字节码分析 | BSD |
| Vineflower | 反编译（人工阅读） | Apache 2.0 |
| nlohmann/json | JSON | MIT |
| spdlog/fmt | 日志 | MIT |
| OpenSSL 3 LTS | 加密 | Apache 2.0 |
| Asio | 网络 | Boost Software License（standalone） |
| zlib-ng | 压缩 | zlib |
| libpng/libvorbis | 图像/音频 | libpng/zlib-like；BSD |
| PCRE2 | 正则 | BSD |
| Boehm GC（候选） | GC | MIT-like |
| Catch2 v3 | 单测 | BSL-1.0 |

**不引入**：Boost、EnTT、GLM（自研小数学）、Protobuf/FlatBuffers（进程内不需要）、SQLite（首版非必需）、Lua、嵌入式 JVM 到游戏进程。

---

## 8. 构建系统与 CI

| 产物 | 构建 | 说明 |
|---|---|---|
| Launcher | CMake + MSVC | 不链接 UE/JRT |
| Converter | CMake + MSVC + JDK 助手 | 不链接 UE |
| 游戏 DLL/JRT | CMake + MSVC | 不包含 UE 头文件 |
| UE Runtime | UBT + `BuildCookRun` | 固定 UE 5.8.4 镜像 |
| 契约 | C 头文件 | 双方版本化；CI 校验 ABI |

**CI 阶段**

- PR：Launcher/Converter/DLL 编译；Catch2；ABI 检查；快速原生化快照。
- Nightly：26.3 样本原生化；差分；UE 集成；R0/R1 截图；性能。
- Release：全量回归；**Shipping 打包**；干净机冒烟；存档/协议/资源矩阵；签名。
- 版本升级：diff/重新生成/Patch 重放/差分/渲染验证。

---

## 9. 备选与不选原因

| 方案 | 结论 | 原因 |
|---|---|---|
| 把 Launcher 做成 UE 应用 | 不选 | 体积/启动/崩溃面/许可证/Java Edition 输入管理都不合适 |
| 用户安装 UE 后运行 `.uproject` | 不选 | 用户侧不可接受；Shipping 才是分发形态 |
| 全 CMake 构建 UE | 不选 | UBT 是 UE 唯一受支持构建系统 |
| 预编译 UE 核心库给 CMake | 不选 | CRT/ABI/异常/RTTI 风险 |
| 每个 section 一个 UObject | 不选 | GC/性能不可接受 |
| UE 默认 PBR/Lumen 直接渲染 MC | 默认不选 | 视觉偏离原版；parity 模式需自定义 |
| 通用 Java→C++ 编译器 | 不选 | 范围失控；历史项目均为子集/已归档 |
| 用户每次启动都原生化 | 不选 | 耗时；应按输入/工具/Patch 缓存 |
| 把 Mojang JAR/资产打进发布包 | 不选 | 法律风险；用户自有文件 |


---

## 10. 参考

**Unreal Engine 打包/分发**

- Build Operations: Cooking, Packaging, Deploying, and Running Projects：<https://dev.epicgames.com/documentation/en-us/unreal-engine/build-operations-cooking-packaging-deploying-and-running-projects-in-unreal-engine>
- Packaging Unreal Engine Projects：<https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-unreal-engine-projects>
- Packaging Settings（Prerequisites 等）：<https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-packaging-settings-in-unreal-engine>
- Hardware and Software Specifications（UE 5.8）：<https://dev.epicgames.com/documentation/en-us/unreal-engine/hardware-and-software-specifications-for-unreal-engine>
- Plugins：<https://dev.epicgames.com/documentation/en-us/unreal-engine/plugins-in-unreal-engine>
- BuildPlugin（面向 UE 开发者分发插件，不是 REIM 用户安装方式）：<https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-automation-tool>

**Horizon（方法参考，不复制实现）**

- 仓库：<https://github.com/horizon-rust/horizon>
- 项目站：<https://playhorizon.dev/>
- Native client：<https://github.com/horizon-rust/horizon/blob/main/docs/native-client.md>
- Launcher preparation：<https://github.com/horizon-rust/horizon/blob/main/docs/launcher-preparation.md>
- Parity workflow：<https://github.com/horizon-rust/horizon/blob/main/docs/parity-workflow.md>
- Version ports：<https://github.com/horizon-rust/horizon/blob/main/docs/version-ports.md>

**Java/字节码/转换（可行性参考）**

- ParparVM：<https://www.codenameone.com/blog/parparvm-compiles-itself/>
- JTransc（已归档）：<https://github.com/soywiz-archive/jtransc>
- GraalVM native-image：<https://www.graalvm.org/latest/reference-manual/native-image/>
- mc-image：<https://github.com/kb-1000/mc-image>
- MCNativeBuilder：<https://github.com/thejudge156/MCNativeBuilder>
- ASM：<https://asm.ow2.io/>；Vineflower：<https://vineflower.org/>
