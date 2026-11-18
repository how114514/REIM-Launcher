# HTCL — How To Craft Launcher

HTCL 是一个 **Minecraft Native Launcher**，不是 Minecraft Java Edition Launcher。

它管理用户自有的 Java Edition 文件，把这些文件作为 **Native 化的输入和规格来源**，然后启动由 HTCL 自己发布的 **Native Runtime**。HTCL **不启动原版 Java Minecraft**。

## 产品边界

| 对象 | HTCL 中的角色 | 是否可启动 |
|---|---|---|
| 用户自有 Minecraft Java Edition 文件 | 规格、研究对象、转换输入（version JSON / client.jar / libraries / assets） | **否** |
| HTCL Native Runtime | UE Shipping 发布包 + C++ Minecraft Runtime | **是，唯一可启动目标** |
| Unreal Engine / Editor | 开发、编译、Cook、Package 工具 | 用户侧不安装 |
| JDK / MSVC / CMake | Converter / Developer Mode 工具 | 普通用户不安装 |

**HTCL 不会：**

- 启动 `javaw.exe`；
- 构造 Java classpath；
- 启动 Java 游戏实例；
- 提供“启动原版”按钮；
- 管理 Java 游戏运行时。

## 关键结论

- **用户不需要安装 Unreal Engine 或 Unreal Editor。**
  UE 是 HTCL 的开发/编译/Cook/Package 基础；用户拿到的是 **UE Shipping 打包产物**。
- **运行时不依赖 JVM。**
  JDK 只在 Converter 的分析/数据生成/差分 oracle 阶段使用。
- **普通用户不需要安装 JDK、Visual Studio、MSVC、Windows SDK、CMake 或 C++ 编译器。**
  User Mode 使用官方预编译 Native Runtime；本地 Native 化/编译属于 Developer/Research Mode。
- **HTCL 不是通用 Java→C++ 编译器。**
  核心是 **Minecraft 专用 Native 化 / 转换工具链**：分析、分类、代码生成、选择性翻译、Native Adapter、Patch、差分验证。
- **JRT 不是 JVM。**
  JRT 只是翻译分支的兼容/迁移基础设施；只有无法直接 Native 化的 Java 语义才使用，目标逐步缩小。
- **20 TPS 模拟与渲染 FPS 分离。**
  C++ Runtime 固定 20 TPS 模拟；UE 按 60/120/144+ FPS 消费不可变快照/增量。
- **UE 不拥有游戏规则。**
  C++ Minecraft Runtime 负责世界状态、物理、AI、存档、协议；UE 只负责运行时宿主和最终渲染。

## 三个交付物

| 交付物 | 用户侧形态 | 职责 |
|---|---|---|
| **HTCL Launcher** | 桌面应用 | 账号、Minecraft 版本信息、Java Edition 文件检测/准备、Native 化任务、Native 版本/实例/Runtime 管理、启动 Native Runtime、进程监管、日志 |
| **HTCL Converter / Native Toolchain** | 命令行工具链（Developer/Research Mode） | 版本分析、差异、数据导出、代码生成/选择性翻译、Native Adapter、Patch、编译、差分 |
| **HTCL Native Runtime** | **UE Shipping 发布包** | UE 运行时、窗口/输入/音频、Rendering Bridge、Unreal Renderer、加载 `mc_<version>.dll` |

## 端到端流程

```text
用户：HTCL Launcher
  ├─ 检测/读取用户自有 Java Edition 文件（不启动）
  │     → 记录为转换输入源
  ├─ User Mode：查找/下载/校验官方 Native Runtime
  │     或 Developer Mode：本地 Native 化（JDK/MSVC/CMake）
  └─ 启动 Native Runtime
        → HTCLRuntime.exe（UE Shipping，自包含）
        → 加载 mc_<version>.dll（C++ 游戏逻辑）
        → 20 TPS 模拟 → 不可变快照
        → Rendering Contract → Rendering Bridge → Unreal Renderer
```

## 渲染路线

```text
R0 最小 Native Rendering 验证（区块/玩家/摄像机）
  ↓
R1 区块 + 实体 + 基础环境
  ↓
R2 完整 Minecraft Renderer（UI/粒子/天气/材质/动画/后处理/OIT）
  ↓
R3 UE 高级渲染增强（LOD/RT/PBR/更远视距）
```

R0 **不是**原版 Java 渲染逻辑/OpenGL 绘制命令回放。

> 开发期 M1 允许使用 GLFW/SDL3 + OpenGL 的临时 C++ Renderer 验证真实世界数据；它不属于渲染路线的 R0，不进入发布，且与 UE Renderer 共用同一 C++ Runtime 和 Rendering Contract。

## 文档索引

| 文档 | 内容 |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 产品定位、Native Launcher 边界、UE 分发、总体架构、Native 化路线、Rendering Bridge、版本策略、风险、实测数据 |
| [docs/TECH_STACK.md](docs/TECH_STACK.md) | Launcher/Converter/Native Runtime 技术选型、UE 打包/Shipping、依赖/许可证 |
| [docs/ROADMAP.md](docs/ROADMAP.md) | 四阶段路线（M1 C++ Gameplay Preview → UE Native Rendering → 完整游戏 + Converter → Launcher 产品化；Converter 自 M1 启动）、并行轨道、门禁、风险 |
| [docs/M1_PLAN.md](docs/M1_PLAN.md) | M1 执行计划：工作包、C ABI/快照、Converter 产物、验收与新对话交接规则 |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | 开发环境、仓库布局、构建边界、编码/ABI 规则、测试与发布 |
| [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) | MC 版本/协议/存档/资源包/Mod/UE 运行环境兼容矩阵 |
| [docs/ARCHITECTURE.md#附录-e26-3-静态实测2026-11-18](docs/ARCHITECTURE.md#附录-e26-3-静态实测2026-11-18) | 26.3 client.jar + 45 个库的静态普查数据 |

## 当前状态

- 本文档是**架构基线**，不是已实现产品。
- 主版本基线：Minecraft Java Edition **26.3**（协议 777 / DataVersion 5023 / 资源包 97.1 / 数据包 121.0 / Java 25）。
- UE 基线：**5.8.4**（开发/CI）；用户侧不安装。
- 已对 26.3 做静态字节码普查（11,383 个类 + 45 个依赖库）；当前首要目标是 M1（C++ Native Gameplay Preview），Converter 自 M1 启动并与 Runtime 共同演进；真实世界加载与运行原型仍待验证，详见 ROADMAP 与 ARCHITECTURE 附录 E。

## 法律与资产

- 不随产品分发 Mojang 代码、纹理、声音、模型或“实质性部分”。
- Java Edition 文件只作输入；本地 Native 化在用户本机执行。
- 官方预编译 Native Runtime、UE Shipping 分发、账号服务与商标使用，发布前必须分别进行法律/许可证评审。
