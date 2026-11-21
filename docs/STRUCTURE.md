# REIM 仓库目录分类

> 目的：给仓库里的**全部目录**一个明确分类、入库状态和归属规则，避免源码、文档、测试、工具、本机输入和生成产物混在一起。
> `docs/` 是正式文档区，必须保留；任何新增目录都要归入本文的一类，并同步更新本文与 `.gitignore`。
> 基线：2026-11-21，M3.5 计划写入后。

---

## 1. 分类总则

| 代码 | 类别 | 入库 | 说明 |
|---|---|---|---|
| **D** | 文档与决策 | 是 | 架构、路线、计划、验收、开发指南、转换决策 |
| **S** | 源码 | 是 | Runtime、Converter、Contract、UE Host |
| **T** | 测试 | 是 | 单元测试、场景、fixture |
| **G** | 工具与脚本 | 是 | 验收、差分、打包、审计脚本 |
| **X** | 第三方依赖 | 是 | 固定版本 + 许可证 |
| **B** | 构建入口 | 是 | 根 CMake、preset、启动脚本 |
| **I** | 本机输入 | 否 | 用户自有 Java Edition 文件、Mojang 库 |
| **O** | 生成/构建产物 | 否 | build/generated/work/logs、UE 中间目录、缓存 |
| **L** | 本地 IDE/Agent | 否 | `.idea/`、`.claude/` 等 |
| **P** | 规划中 | 尚未创建 | 后续阶段才会出现的目录 |

**硬规则**

- Mojang jar、libraries、assets、存档、`version_manifest_v2.json` **永不入库**。
- `generated/`、`work/`、`build/`、`logs/` 是本机产物，不入库；需要长期保存的报告写入 `docs/` 或 `build/` 的忽略区。
- 生成代码不手改；修改进入 `converter/patches/`。
- 新增顶层目录必须同时更新本文件、`.gitignore`、`README.md` 的文档索引。

---

## 2. 顶层目录分类

| 路径 | 类别 | 入库 | 用途 |
|---|---|---|---|
| `.git/` | VCS | 否 | Git 元数据；不是仓库内容 |
| `.claude/` | L | 否 | 本地 Agent/编辑器配置；当前为空 |
| `.idea/` | L | 否 | JetBrains 本地状态 |
| `docs/` | **D** | 是 | **正式文档区（必须保留）**：架构、路线、计划、验收、开发、兼容性、目录分类 |
| `conversion-decisions/` | D | 是 | 按 MC 版本的转换决策审计 |
| `converter/` | S | 是 | Converter / Java 分析 / 数据生成 / Patch |
| `runtime/` | S | 是 | C++ Runtime、Contract、Preview/headless、UE Host |
| `tests/` | T | 是 | 单元/集成测试、场景、fixture |
| `tools/` | G | 是 | PowerShell/Python 验收、差分、打包、审计脚本 |
| `third_party/` | X | 是 | Catch2、LZ4、miniz、nlohmann、stb、GLFW 源码包 |
| `build/` | O | 否 | CMake/UE 构建输出；当前含 `envcheck/`、`m1/`、`m2/`、`m3/`、`m3-plan-backup/`、`native-build/`、`native-build-dev/`、`native-version/` |
| `generated/` | O | 否 | Converter 生成的 schema v3 数据/资产（`generated/26.3`） |
| `work/` | O | 否 | 分析、报告、数据生成中间产物（`work/26.3`） |
| `logs/` | O | 否 | 运行/验收日志 |
| `versions/` | I | 否 | 用户自有 Java Edition 版本 jar（`versions/26.3`） |
| `libraries/` | I | 否 | Mojang/Maven 依赖 jar（`libraries/<group>/...`） |

根文件：`README.md`（D）、`CMakeLists.txt` / `CMakePresets.json`（B）、`play-m3.bat`（B）、`.gitignore` / `.gitattributes`（B）。

> `build/`、`generated/`、`work/`、`versions/`、`libraries/`、UE 中间目录的内部子目录会随构建器、版本和本机缓存变化；本文按类别分组，不把它们的内部结构当作仓库合同。入库的目录则逐个列出。

---

## 3. `docs/`（必须保留）

`docs/` 当前没有子目录，全部为正式文档：

| 文件 | 子类 | 说明 |
|---|---|---|
| `ARCHITECTURE.md` | 架构 | 产品边界、总体架构、Rendering Bridge、版本策略、实测附录 |
| `TECH_STACK.md` | 架构 | Launcher/Converter/Native Runtime/UE 技术选型 |
| `COMPATIBILITY.md` | 兼容 | MC 版本/协议/存档/资源包/Mod/UE 兼容矩阵 |
| `ROADMAP.md` | 路线 | M1→M2→M3→M3.5→M4 阶段顺序与门禁 |
| `M1_PLAN.md` | 计划 | M1 执行计划 |
| `M2_PLAN.md` | 计划 | M2 执行计划 |
| `M3_PLAN.md` | 计划 | M3 执行计划 |
| `M3_5_PLAN.md` | 计划 | M3.5：原生功能实现 + Java→C++ 转换、UI/菜单/水渲染 |
| `M1_ACCEPTANCE.md` | 验收 | M1 实测记录 |
| `M2_ACCEPTANCE.md` | 验收 | M2 实测记录与已知限制 |
| `M3_ACCEPTANCE.md` | 验收 | M3 实测记录，G3 FAIL 与缺口 |
| `DEVELOPMENT.md` | 开发 | 环境、构建、编码/ABI、测试、发布 |
| `STRUCTURE.md` | 开发 | 本文件：目录分类与入库规则 |

**新增文档规则**

- 架构/路线/计划/验收/开发/兼容 → `docs/`。
- 转换决策（为什么这样转换）→ `conversion-decisions/<profile>.md`，不要放 `docs/`。
- 临时报告/截图/soak 结果 → `build/` 或 `work/`，不要放 `docs/`；需要长期引用的结论写入验收文档。

---

## 4. `conversion-decisions/`

| 路径 | 类别 | 入库 | 用途 |
|---|---|---|---|
| `26.3.md` | D | 是 | 26.3 数据路线、schema v3、来源矩阵、翻译门禁、UE 适配决策 |

---

## 5. `converter/`（Converter / 工具链）

| 路径 | 类别 | 入库 | 用途 |
|---|---|---|---|
| `converter/orchestrator/` | S | 是 | `reim-convert.exe`：prepare/data/analyze/extract/diff/ledger/patch/package/assemble |
| `converter/java-analyzer/` | S | 是 | 离线 Java 类分析器（JDK 25）；`src/reim/analyze/Analyzer.java` |
| `converter/adapters/ue/` | S | 是 | UE Adapter pin 与描述符生成 |
| `converter/ledger/` | D | 是 | Port ledger（Java 符号 ↔ 原生/生成/分析） |
| `converter/patches/` | S | 是 | 按 MC 版本的 Patch 集；当前 `26.3/patch_set.json` 为空 |
| `converter/samples/` | D | 是 | 代表性翻译实验与差异说明 |

**M3.5 规划新增**（尚未创建）

| 路径 | 类别 | 用途 |
|---|---|---|
| `converter/translate/` | S/P | 字节码 → IR → C++ 生成、NJS 绑定、原生替换表、覆盖报告 |
| `converter/verify/` | S/P | 翻译/覆盖/差分校验；若继续复用 `tools/` 则不创建 |

---

## 6. `runtime/`（C++ Runtime 与 Host）

### 6.1 顶层

| 路径 | 类别 | 入库 | 用途 |
|---|---|---|---|
| `runtime/contract/` | S | 是 | Runtime ↔ Host 的 C ABI / 快照 / UI / 音频 / 游戏头文件 |
| `runtime/mc-native/` | S | 是 | 手写原生游戏实现（世界/游戏/网格/存档/NBT/UI） |
| `runtime/headless/` | S | 是 | headless Host：soak、场景、性能 |
| `runtime/preview/` | S | 是 | GLFW/OpenGL 预览 Host：网格/实体/UI/天空/粒子 |
| `runtime/scenario/` | S | 是 | `reim-scenario.exe` 场景 runner |
| `runtime/host/` | S | 是 | UE 5.8.4 Host（REIMRuntime） |

### 6.2 `runtime/mc-native/`

| 路径 | 用途 |
|---|---|
| `anvil/` | Anvil `.mca` 读写；region 写入/原子替换 |
| `asset/` | 资源包/数据包读取与覆盖 |
| `core/` | 20 TPS 模拟、玩家、相机、tick |
| `data/` | 方块表、生成 manifest 校验、schema |
| `game/` | 玩法：实体、AI、背包、配方、生存、粒子、交互 |
| `mesh/` | 区块 section 网格、面剔除、tint |
| `nbt/` | NBT 解析/写入 |
| `samples/` | 翻译样本（PackedIntegerArray、modified UTF-8） |
| `src/` | Runtime 实例、C ABI 实现、library 入口 |
| `ui/` | Runtime 显示列表、HUD/界面模型、字体 |
| `util/` | 压缩、文件、哈希、日志、PNG、UTF、ZIP |
| `world/` | 区块/世界/维度/世界生成/方块编辑 |

### 6.3 `runtime/host/`（UE）

| 路径 | 类别 | 入库 | 用途 |
|---|---|---|---|
| `runtime/host/Source/REIMBridge/` | S | 是 | Contract 消费、DLL 加载、快照/帧上传 |
| `runtime/host/Source/REIMRenderer/` | S | 是 | R0 渲染（区块/材质/天空/调试代理） |
| `runtime/host/Source/REIMGame/` | S | 是 | GameMode/自动化测试 |
| `runtime/host/Source/REIMInput/` | S | 是 | 输入转发 |
| `runtime/host/Source/REIMUI/` | S | 是 | UE UI 宿主（显示列表消费） |
| `runtime/host/Config/` | S | 是 | UE 配置 |
| `runtime/host/Content/` | S | 部分 | UE 资产；`Maps/`、`R0/` 入库；`Collections/`、`Developers/` 为本地缓存 |
| `runtime/host/Plugins/REIMRuntimeContent/` | S | 是 | UE 内容插件 |
| `runtime/host/Tools/` | G | 是 | UE 资产生成脚本 |
| `runtime/host/Build/` | O | 否 | Adapter 描述符等构建产物 |
| `runtime/host/Binaries/` | O | 否 | UE 二进制 |
| `runtime/host/Intermediate/` | O | 否 | UE 中间文件 |
| `runtime/host/Saved/` | O | 否 | UE 日志/缓存/Cook 输出 |
| `runtime/host/DerivedDataCache/` | O | 否 | UE DDC |

### 6.4 M3.5 规划新增

| 路径 | 类别 | 用途 |
|---|---|---|
| `runtime/njs/` | S/P | Native Java Support：对象/异常/集合/Random/GC/Codec 兼容层；**不是 JVM** |
| `runtime/renderer/` | S/P | 仅在独立 exe 渲染器需要独立目录时创建；当前复用 `runtime/preview/` |

> 早期文档把 Java 支持层称为 **JRT**；M3.5 采用 **NJS** 名称以避免“JVM/解释器”误解。两者指同一规划方向。

---

## 7. `tests/`

| 路径 | 类别 | 入库 | 用途 |
|---|---|---|---|
| `tests/*.cpp` | T | 是 | 单元/集成测试（M1/M2/M3/M3.5） |
| `tests/CMakeLists.txt` | T | 是 | 测试目标 |
| `tests/scenarios/m3a/` | T | 是 | M3-A 场景（Contract/UI/动作/实体通道） |
| `tests/scenarios/m3d/` | T | 是 | 交互 INT |
| `tests/scenarios/m3e/` | T | 是 | 背包/容器 INV |
| `tests/scenarios/m3f/` | T | 是 | 生存/生物 SUR/MOB |
| `tests/scenarios/m3g/` | T | 是 | 世界/维度 WLD |
| `tests/scenarios/m3h/` | T | 是 | UI |
| `tests/scenarios/m3i/` | T | 是 | 资源/安全 RES |
| `tests/scenarios/m3j/` | T | 是 | R2 粒子/天气 |
| `tests/scenarios/README.md` | T/D | 是 | 场景 DSL 说明 |
| `tests/fixtures/` | T | 是 | 测试 fixture |
| `tests/fixtures/packs/` | T | 是 | 资源包/数据包 fixture（含恶意包安全用例） |

**规则**

- 场景 fixture 必须来自固定种子/合成数据，不依赖用户存档。
- 新增场景套件 → `tests/scenarios/<suite>/`；新增 fixture → `tests/fixtures/`。
- 未实现功能写 `not_run`，不得用空场景/跳过场景计为通过。

---

## 8. `tools/`

| 路径 | 类别 | 入库 | 用途 |
|---|---|---|---|
| `tools/*.ps1` | G | 是 | 构建、打包、验收、冒烟、错误矩阵、视觉回归 |
| `tools/*.py` | G | 是 | 审计、场景统计、图像比较、存档/资源/UI/JRT 审计 |
| `tools/__pycache__/` | O | 否 | Python 缓存；已在 `.gitignore` |

新增脚本放在 `tools/`；一次性临时脚本不要入库，或放 `build/`/`work/`。

---

## 9. `third_party/`

| 路径 | 类别 | 入库 | 用途 |
|---|---|---|---|
| `third_party/catch2/` | X | 是 | 单元测试框架 |
| `third_party/lz4/` | X | 是 | LZ4 区块压缩 |
| `third_party/miniz/` | X | 是 | zlib/gzip |
| `third_party/nlohmann/` | X | 是 | JSON |
| `third_party/stb/` | X | 是 | 图像 |
| `third_party/glfw-3.4.tar.gz` | X | 是 | GLFW 源码包 |
| `third_party/LICENSES.md` | X/D | 是 | 许可证清单 |

**规则**：新增第三方库必须固定版本、记录许可证、更新 `LICENSES.md`；不要在 `third_party/` 里放构建产物。

---

## 10. 本机输入与生成产物（不入库）

| 路径 | 类别 | 说明 |
|---|---|---|
| `versions/26.3/` | I | 用户自有 server/client jar 与 version JSON；永不提交 |
| `libraries/<group>/...` | I | Mojang/Maven 依赖 jar；永不提交 |
| `version_manifest_v2.json` | I | Mojang 元数据；`.gitignore` |
| `generated/26.3/` | O | Converter schema v3 数据/资产/图集/语言/声音 |
| `work/26.3/` | O | 类分析、reports、datapack、缓存 |
| `build/` | O | `m1/m2/m3`、`native-build*`、`native-version`、UE 包 |
| `logs/` | O | 运行/验收日志 |
| `tools/__pycache__/` | O | Python 缓存 |
| `.idea/`、`.claude/` | L | 本地 IDE/Agent 状态 |

以上路径均不应出现在 `git status` 的待提交列表；若出现，先补 `.gitignore`。

---

## 11. 规划中目录（尚未创建）

| 路径 | 阶段 | 用途 |
|---|---|---|
| `launcher/` | M4 | Qt/C++ Launcher：UI/实例/版本/Runtime/启动/更新 |
| `converter/translate/` | M3.5 | 字节码 → IR → C++ 生成与 NJS 绑定 |
| `runtime/njs/` | M3.5 | Native Java Support（对象/异常/集合/GC/Codec） |
| `converter/verify/` | M3.5（可选） | 翻译/覆盖校验；若 `tools/` 足够则不建 |
| `docs/adr/` | 可选 | 若 ADR 数量增长再拆分；当前决策在 `docs/` 与 `conversion-decisions/` |

创建这些目录时，必须在本文件登记并更新 `README.md` 文档索引与 `.gitignore`（如适用）。

---

## 12. 一张图

```text
REIM-Launcher/
├─ docs/                     # D：正式文档（必须）
├─ conversion-decisions/     # D：转换决策
├─ converter/                # S：Converter/分析/数据/Patch
├─ runtime/                  # S：C++ Runtime + Contract + Host
│  ├─ contract/              #     C ABI
│  ├─ mc-native/             #     原生游戏实现
│  ├─ preview/ headless/     #     开发 Host
│  ├─ scenario/              #     场景 runner
│  └─ host/                  #     UE Host（Source/Config/Content/Plugins 入库）
├─ tests/                    # T：单元/场景/fixture
├─ tools/                    # G：验收/差分/打包脚本
├─ third_party/              # X：第三方源码 + 许可证
├─ CMakeLists.txt            # B：构建入口
├─ CMakePresets.json         # B：preset
├─ play-m3.bat               # B：启动入口
│
├─ build/ generated/ work/   # O：本机产物（不入库）
├─ logs/                     # O
├─ versions/ libraries/      # I：用户/Mojang 输入（不入库）
└─ .idea/ .claude/           # L：本地状态（不入库）
```
