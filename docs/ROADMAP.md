# HTCL 路线图

> 基线：Minecraft Java Edition 26.3 / UE 5.8.4 / Windows x64 首发。  
> 本文是 `ARCHITECTURE.md` §13 的展开；工程量级以多名有经验的引擎/编译器/游戏逻辑工程师为参考，不是日期承诺。

---

## 1. 原则

1. **Launcher 先行**：检测/准备用户自有 Java Edition 输入、安装/启动 Native Runtime，是可以独立交付的价值；**HTCL 不启动 Java Edition**。
2. **P0 先证明可行性**：按子系统做“翻译/生成/手写”选型实验；不把“文档写完”当成“可行性已证明”。
3. **产品边界不可漂移**：没有 javaw/classpath/JVM 游戏启动路径；唯一可启动目标是 Native Runtime。
4. **纵向切片优先**：先打通“加载世界 → 渲染 → 移动 → 保存”，再扩广度。
5. **差分驱动**：原版是规格；每个子系统必须有 oracle/harness/截图/存档往返。
6. **版本更新工具化**：新 MC 版本不应导致手工重写整个 Runtime。
7. **UE 分发可验收**：P0 就要在**未安装 UE** 的干净机上跑通 Shipping 包。
8. **用户不需要编译**：User Mode 用官方预编译 Native Runtime；本地构建只属 Developer Mode。

---

## 2. 版本更新列车（长期核心）

```text
新 Minecraft 版本
      ↓
D0 检测/摄取
  version manifest → version JSON → client/server JAR → libraries → assets
      ↓
D1 准备（JDK 25，仅此阶段）
  数据生成器 --reports；Java 导出器；资源/语言/模型/声音索引
      ↓
D2 差异分析
  类/方法/字段 + 规范化字节码 hash；协议/注册表/数据包/资源包/类 diff
      ↓
D3 分诊
  每个子系统选择：代码生成 / 选择性翻译 / 原生手写 / 库替换 / Patch
      ↓
D4 产出
  生成表/常量/codec；翻译纯逻辑；手写渲染/平台/热路径；Patch；库适配
      ↓
D5 构建
  Developer Mode：CMake/MSVC → mc_<version>.dll；CI：UBT → UE Runtime
  User Mode：直接分发/更新官方预编译 Native Runtime
      ↓
D6 差分测试
  server/client oracle、tick/world/packet、存档往返、资源包、截图
      ↓
D7 渲染验证
  R0/R1/R2/R3 截图、性能、OIT/透明、资源包
      ↓
D8 发布
  UE Shipping 包 + manifest + 兼容矩阵；Launcher 更新
```

**参考 Horizon 的部分**

- 用户自有文件 + `research-lock` 式 pinned 输入；
- 数据生成器/导出器 + 生成表；
- 类清单 + 规范化字节码 hash + diff；
- port ledger（vanilla 方法 ↔ 原生实现）；
- 大规模 differential harness；
- “检测 → 准备 → diff → regenerate → harnesses → ranked work items”的版本流水线。

**HTCL 额外需要的**

- C++ 代码生成/选择性翻译；
- C++ Runtime Support/JRT（仅翻译分支）；
- Patch 集重放/三路合并；
- UE R0→R1→R2→R3 渲染验证与截图；
- C ABI/UE Shipping 包版本化。

---

## 3. 阶段总览

| 阶段 | 目标 | 主要成果 | 依赖 |
|---|---|---|---|
| **P0 定位与可行性** | Launcher 检测/准备 Java Edition 输入 + Native Runtime 安装/启动；验证 Native 化/渲染/ABI/UE 分发四条线 | Launcher MVP；26.3 分析报告；样本翻译/生成；UE Shipping+R0 最小 Native 骨架；go/no-go | 无 |
| **P1 端到端切片** | Native 版加载世界、渲染、移动、保存 | DLL+Runtime 闭环；R0 最小 Native 渲染；单机纵向切片 | P0 |
| **P2 可玩核心** | 方块/背包/合成/基础生物/存档；R1 接管 | 单机可玩；差分 v1；性能基线 | P1 |
| **P3 兼容广度** | 世界生成/维度/命令/UI/音频/资源包 | P2/P3 目标；8h soak；资源矩阵 | P2 |
| **P4 多人** | 协议/服务端/客户端互操作 | 原版客户端连接；协议翻译 | P2/P3 |
| **P5 生态/增强** | 原生扩展、R2/R3 渲染、回放、性能 | 扩展 API；增强 profile | P3/P4 |
| **P6 版本列车** | 26.4/27.x 迁移 | diff/生成/翻译/Patch/回归 | 全部 |

---

## 4. P0 — 定位与可行性（最重要的门禁）

**Launcher**

- 账号（Microsoft/离线）、Minecraft 版本信息、检测/准备用户自有 Java Edition 26.3 文件、Native 版本/实例/Runtime 安装与启动、日志。
- **不启动 Java Edition**；Java 25 仅 Converter 参考。

**Converter / Native Toolchain**

- 摄取/分析 26.3：类清单、依赖、反射/Unsafe/JNI 报告、库使用面。
- 对 1–2 个代表性子系统做“翻译/生成/原生”选型实验：
  - Brigadier（54 类，0 反射/native）；
  - DFU Codec/DynamicOps（484 类，0 反射/Unsafe）；
  - NBT（109 类）。
- JRT 最小骨架（仅翻译分支）：对象/字符串/集合/异常/线程；GC/异常模型 spike。
- 记录每个子系统的决策与通过率。

**Runtime**

- UE 5.8.4 工程骨架；C ABI；20 TPS 模拟；R0 最小 Native 渲染（区块/玩家/摄像机，不做命令回放）。
- **Shipping 打包**：BuildCookRun；在**未安装 UE** 的干净机上启动。
- VC++ 运行库 app-local/预置验证。

**差分/QA**

- 原版 server oracle；最小 harness；截图基线。

**P0 go/no-go**

| 实验 | 通过标准 | 失败含义 |
|---|---|---|
| P0-A Brigadier | 翻译/编译；选定用例 100% 与 JVM 一致 | 翻译器/异常/字符串/集合模型有硬伤 |
| P0-B DFU Codec | JSON/NBT round-trip ≥95%，零崩溃 | 数据加载路线需重写/缩小 |
| P0-C NBT | 语料 round-trip 100%，未知字段保留 | 存档路线不可行 |
| P0-D 模拟切片 | 1,000 tick，RNG/tick ≥99% 与 oracle 一致 | JRT/线程/顺序语义有硬伤 |
| P0-E R0 最小 Native 渲染 | 区块/玩家/摄像机截图差异在阈值内；20 TPS/FPS 独立；无崩溃 | Contract/Bridge/网格方案需调整 |
| P0-F 宿主/ABI/UE 分发 | 干净机（无 UE）启动 Shipping 包；DLL/20 TPS 模拟/快照稳定 | 打包/运行库/ABI/线程/快照模型需调整 |

**Go 条件**：A/B/C 全部通过，D/E/F 至少各有一个可运行 demo。  
**No-Go/改线**：A/B/C 任一无法在时间盒内通过；收缩范围、增加原生重写比例，或调整路线。

---

## 5. P1 — 端到端切片

- Native 版加载 26.3 测试世界；R0 区块/玩家/摄像机；移动/相机/碰撞；保存只读/写入。
- 资源导入/图集/基础纹理/字体；R0 最小 Native 渲染链路完整。
- 差分：世界加载/渲染截图/基本输入。
- 验收：进入世界并游玩 10 分钟；存档可被原版读取；R0 截图接近规格。

---

## 6. P2 — 可玩核心

- 方块交互/背包/合成/熔炉/箱子/基础生物/战斗/光照/天气/保存。
- R1 接管区块/基础实体/基础环境；UI 最小 HUD（完整 UI 到 R2）。
- 差分 v1；性能基线；8h 稳定性起步。
- 验收：30 分钟脚本化场景；P2 通过率 ≥95%（场景公开）；R1 vs R0 截图评审。

---

## 7. P3 — 兼容广度

- 世界生成/维度/结构/地物；命令/函数/战利品/进度；完整 UI/音频/资源包；OIT/透明。
- 验收：固定种子区块/结构 golden；UI/音频评审；资源/数据包矩阵；8h soak。

---

## 8. P4 — 多人

- 26.3 服务端/客户端协议；登录/配置/play；原版客户端连接；聊天签名；专用服务器。
- 验收：原版客户端 30 分钟多人会话；恶意包/限速测试；协议 golden。

---

## 9. P5/P6 — 生态与版本列车

**P5**

- 原生 C ABI 扩展；R2 渲染/增强 profile；回放/录制；性能优化；工具链自动化。

**P6**

- 每个稳定版：检测/准备/diff/生成/翻译/Patch/差分/渲染验证/发布。
- UE 升级独立分支：编译/自动化/截图/性能/打包回归。
- 兼容矩阵/发布说明更新。

---

## 10. 并行轨道

| 轨道 | 内容 | 可在何时独立推进 |
|---|---|---|
| **L（Launcher）** | Java Edition 输入/Native 版本/实例/账号/Runtime/启动/更新 | P0 起 |
| **C（Converter/JRT）** | 分析/翻译/生成/运行时/库替换/Patch | P0 起 |
| **R（Runtime/Renderer）** | UE Shipping/ABI/R0→R1→R2→R3/资源/UI/音频 | P0 起 |
| **Q（QA）** | oracle/差分/截图/性能/干净机/CI | 第一天 |

P0 后 L 可独立交付；C/R 以 C ABI/契约并行；Q 从第一天介入。

---

## 11. 交付里程碑

| 里程碑 | 交付 |
|---|---|
| M0 | Launcher 检测/准备 Java Edition 输入 + 安装/启动 Native Runtime；26.3 静态/动态分析报告 |
| M1 | 样本翻译/生成 + JRT 最小闭环；P0 go/no-go 报告 |
| M2 | UE Shipping 包在干净机启动；R0 最小 Native 测试场景 |
| M3 | Native 版加载世界/移动/保存（纵向切片） |
| M4 | 单机可玩核心 + R1 渲染 + 差分 v1 |
| M5 | 兼容广度 + 资源包/UI/音频 + 8h soak |
| M6 | 多人/协议 + 版本列车 |

---

## 12. 明确不在首版

- Java Mod/Mixin/Forge/Fabric 兼容；
- OptiFine/Iris shaderpack；
- 完整 DFU 旧存档原生迁移；
- 完整 P3 世界生成/红石/AI/多人；
- 移动端/主机端；
- 通用 Java→C++ 编译器产品。
