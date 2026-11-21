# REIM 兼容性说明

> 主版本基线：Minecraft Java Edition **26.3**（协议 777 / DataVersion 5023 / 资源包 97.1 / 数据包 121.0 / Java 25）。  
> 运行时基线：UE **5.8.4 Shipping**（用户不安装 UE）。  
> 本文说明“兼容什么、不兼容什么、如何验证”，不是已实现能力清单。

---

## 1. 版本轴（不能混为一个“版本号”）

| 轴 | 26.3 值 | 由谁决定 | 处理 |
|---|---|---|---|
| MC profile | 26.3 | 原生化构建 | 注册表/玩法/资源/数据格式 |
| 协议 wire | 777 | `mc/net` | codec/状态机；可选翻译层 |
| DataVersion | 5023 | `mc/save` | 存档读写/升级 |
| 资源包格式 | 97.1 | 资源加载 | 加载/转换/拒绝 |
| 数据包格式 | 121.0 | 数据加载 | 加载/转换/拒绝 |
| Java | 25 | Converter/工具链 | Java Edition 输入分析/数据生成；**不启动 Java Edition** |
| Converter | 版本化 | 工具链 | 生成语义/Patch 格式 |
| Patch set | 26.3-rN | 版本迁移 | 重放/合并 |
| Runtime ABI | 1+ | `contract/` | DLL/宿主兼容 |
| UE | 5.8.4 | Runtime 构建 | 开发/CI；用户不安装 |

**规则**：一个原生化构建绑定一个 MC profile；Launcher 可并存多个构建；不在一个进程里并行运行多套玩法逻辑。

---

## 2. 兼容性分级

| 等级 | 名称 | 定义 | 验证 |
|---|---|---|---|
| P0 | 数据兼容 | NBT/Anvil/包格式/协议编号 | 原版工具/语义 diff |
| P1 | 结构兼容 | 世界/区块/实体/UI 能加载呈现 | 加载测试/截图 |
| P2 | 行为兼容 | 交互/物理/AI/红石/合成符合原版 | 脚本化差分 |
| P3 | 位级/序列兼容 | RNG/tick 顺序/世界生成一致 | tick hash/区块 golden |
| P4 | 表现兼容 | 视觉/UI/音频接近原版 | 图像/音频 golden |

默认目标：玩法 **P2**；关键确定性系统 **P3**；渲染/UI **P1–P4** 分级推进。

---

## 3. 兼容矩阵（首版目标）

| 对象 | 首版 | 后续 | 说明 |
|---|---|---|---|
| **启动 Java Edition 26.3** | **否** | **否** | REIM 是 Native Launcher，不提供 Java 启动 |
| Java Edition 26.3 文件读取/准备 | 是 | — | 仅转换输入，不启动 |
| 原生化 26.3 单机 | 分阶段 | — | M1→M2/M3 纵向切片 |
| 原版 26.3 存档读取 | 是 | — | Anvil/NBT/实体/POI/level.dat |
| 原版 26.3 存档写入 | 是 | — | 不降级写；保留未知字段 |
| 旧存档 | 官方离线升级 | 有限原生迁移 | 不做完整 DFU |
| 原版 26.3 资源包 | 是（模型/纹理/声音/语言/UI） | shader 覆盖后续 | shader 首版不支持 |
| 原版 26.3 数据包 | 分阶段 | 完整命令/世界生成 | 依赖 Codec/命令实现 |
| 原版客户端连 REIM 服务端 | 否 | 多人阶段 | 协议 777；离线优先 |
| REIM 客户端连原版服务端 | 否 | 多人阶段（拉伸） | MS 登录/聊天签名 |
| Java Mod/Mixin | 否 | 否 | JVM/ASM；明确非目标 |
| OptiFine/Iris shaderpack | 否 | 独立项目 | GLSL/管线 |
| 原生 C ABI 扩展 | 否 | 四阶段之后 | 内容/命令/事件 |
| 专用服务器 | 否 | 可选（多人阶段） | headless |
| 回放/录制 | 否 | 四阶段之后 | 自有格式 |

---

## 4. 存档兼容

**容器**

- Anvil `.mca`：32×32 区块、8 KiB 头、4 KiB sector；
- 26.3 维度布局：`dimensions/<namespace>/<dimension>/region/r.x.z.mca`（不再假设顶层 `region/`）；
- 压缩 ID：1 gzip / 2 zlib / 3 none / 4 LZ4；超大区块 `.mcc`；
- `level.dat`（spawn/time/version）+ `data/minecraft/*.dat`（weather、world_clocks、world_gen_settings、game_rules 等）+ `players/data/<uuid>.dat` + `dimensions/<dim>/data/minecraft/*.dat`（chunk_tickets/raids/world_border）；实体/POI 的实际存放位置以 26.3 实档审计为准。

**规则**

- 26.3/5023 原生读写；旧版本默认用官方 26.3 服务端 `--forceUpgrade` 离线升级，再导入。
- 不降级写入：读取到高于当前 DataVersion 的存档时拒绝写入并提示。
- 原子/sector 安全写、临时文件/备份、`session.lock`、后台保存、崩溃恢复；多文件保存用 journal/marker 明确提交顺序。
- 未知 NBT compound/list 保留；未知 item components 必须保留原始 NBT，不能只存 hash；读写限制（深度/长度/数组）防恶意存档。
- 新/改区块维护 `Status=full`、`isLightOn`、heightmaps 等 vanilla 可读字段。
- 26.x 世界目录/版本历史有迁移；Launcher/存档层按实际版本解析，不硬编码。
- 验收：C++ 写 → 原版读 → 原版写 → C++ 读；语义 diff（忽略压缩/顺序/sector）。

**不做**

- 首版不做完整 C++ DFU；DFU 规则是编译后的 Java 类，不是 JSON 规则树。
- 不承诺所有历史 DataVersion 的原生迁移。

---

## 5. 网络协议兼容

- 协议 **777**；状态：handshake → status → login → configuration → play。
- configuration 包含 registry/tags/known packs；1.20.5+ 数据组件；1.19+ 聊天签名。
- 压缩/VarInt/包上限；AES/CFB8 + RSA；Microsoft OAuth → Xbox Live/XSTS → Minecraft services。
- 本地单机默认走同一 codec + 内存 transport；可保留 fast path 开关用于差分。
- 多人顺序：
  1. 原版 26.3 客户端连接 REIM 服务端（离线优先）；
  2. REIM 客户端连接原版 26.3 服务端（MS 登录/聊天签名）；
  3. 协议翻译（26.1/26.2 → 26.3 语义），不复制玩法逻辑。
- 聊天签名/在线认证是独立里程碑；首版可离线优先/放宽服务端策略。
- 验收：golden packet trace、原版客户端会话、fuzz、限速/恶意包。

---

## 6. 资源包与数据包

**资源包**

- 支持 26.3（97.1）：blockstates、block/item models、textures（动画 `.mcmeta`）、sounds.json/OGG、lang、font、atlases、equipment、item model、GUI 纹理。
- 运行时不依赖 Editor：PNG/OGG/JSON 由 Runtime/宿主运行时加载；纹理用 `CreateTransient`/纹理数组。
- 缺失资源：原版回退（占位/黑白格）+ 警告；不静默成功。
- 资源包 shader 覆盖：首版警告/忽略；内置 shader 由 UE 材质/映射替代。
- OptiFine/Iris shaderpack：首版不支持；独立适配项目。
- 验收：16x/32x/HD/动画/字体/声音包；切换/热重载/缺失资源。

**数据包**

- 支持 26.3（121.0）常见目录：loot_table、recipe、advancement、function、predicate、item_modifier、tags、worldgen、dimension/dimension_type、structure、damage_type。
- 包优先级/`replace`/标签合并/`/reload` 按原版语义。
- 命令/函数/谓词依赖 Core 命令解析器；未实现项给出明确错误，不执行部分语义。
- 验收：内置/常见包/旧格式/恶意包；ZIP slip/解压炸弹/路径穿越。

---

## 7. UE Runtime 的用户环境兼容

| 项 | 要求 | 说明 |
|---|---|---|
| Unreal Engine/Editor | **不需要** | Runtime 是 Shipping 发布包 |
| Windows | 10/11 x64 | 首发 |
| VC++ 运行库 | 2015–2022 x64 | app-local 或预置安装器 |
| DirectX | DX12/SM6 为要求；DX11 不支持 | Nanite/Lumen 等同样需 DX12+SM6 |
| GPU 驱动 | 用户自备 | 不支持的系统/驱动给出明确提示 |
| 磁盘/内存 | 按基准场景定 | 资源包/渲染距离影响 |
| 用户插件安装 | **不需要** | 插件已打进 Runtime |
| .NET/UBT/JDK | 不需要 | 仅 Converter/开发/CI |

**验收**：未安装 UE 的干净机，只装 VC++ 运行库，从 Launcher 安装/启动/进入世界/退出。

---

## 8. 原生化支持矩阵（按子系统）

| 子系统 | 首版策略 | 兼容目标 | 备注 |
|---|---|---|---|
| 注册表/方块/物品/实体定义 | 代码生成 | P0–P2 | 数据生成器 + diff |
| DFU Codec/DynamicOps | 翻译（候选） | P0–P2 | 实测 484 类、0 反射/Unsafe；P0-B 验证 |
| Brigadier/NBT | 翻译/手写 | P0–P2 | 类少、无反射/native |
| 世界/区块/实体/物理/AI | 翻译 + Patch + 热路径原生 | P2–P3 | 差分驱动；P3 分阶段 |
| 客户端 UI/渲染编排 | 原生适配 + R0 最小 Native；UI 到 R2 | P1–P4 | 不做原版命令流/GL 回放 |
| 区块/实体/环境渲染 | R1 UE 接管 | P1–P4 | R0 最小 Native 回退 |
| 网络 codec | 翻译 + Asio | P2–P4 | Netty 替换 |
| 平台（窗口/输入/音频/文件） | 原生 | P0–P1 | 宿主/OS |
| Guava/FastUtil/Netty/Gson/Log4j/JNA/OSHI/ICU | 替换（按使用面） | P0–P1 | 不整体翻译 |
| DFU 旧存档 fixer | 官方离线升级 | P0 | 不做完整原生 |
| Java Mod/Mixin | 不支持 | — | 明确非目标 |

**实测参考（26.3 client.jar）**

- 11,383 类 / 98,739 方法 / 4.13 MB；0 native、0 Unsafe、0 `Class.forName`、5 个反射类；
- 32,235 个 invokedynamic，5 种标准 bootstrap；
- 游戏本体使用 DFU 78 类、Guava 92、FastUtil 182、Brigadier 37、LWJGL 234、JOML 26、Netty 60、Gson 18、Authlib 43、ICU 10、JNA 11；
- DFU 484 类、0 反射/Unsafe、1,504 indy；
- FastUtil 12,965 类 / 5.4 MB；ICU4J 1,670 类 / 15 MB——替换优先。

详见 `ARCHITECTURE.md` 附录 E。

---

## 9. 平台与版本支持策略

- 首发 Windows 10/11 x64；Linux/macOS 后置（影响 UE/Launcher/构建矩阵）。
- 主版本：26.3；后续跟随稳定版，不在 snapshot 上追更。
- 每次发布一个主 profile；协议翻译只做相邻版本；旧存档显式升级。
- 兼容矩阵公开（文档/Launcher），标注“支持/部分支持/不支持”。
- 不承诺任意历史资源包/数据包/Mod。

---

## 10. 已知限制（首版）

- 不支持 Java Mod/Forge/Fabric/Mixin；
- 不支持 OptiFine/Iris shaderpack；资源包核心 shader 覆盖不支持；
- 不做完整 C++ DFU；
- 世界生成/红石/AI/命令/多人分阶段，不承诺首发全覆盖；
- 不承诺跨 GPU 像素级一致；
- Developer Mode 本地 Native 化需要 MSVC/CMake/JDK；User Mode 使用官方预编译 Native Runtime，不安装开发工具；
- UE Shipping 包需要 VC++ 运行库；不支持的系统/驱动明确提示。
