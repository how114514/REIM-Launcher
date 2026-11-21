# REIM M3 执行计划 — 完整单机游戏 + Converter 规模化

> 基线：Minecraft Java Edition 26.3 / Unreal Engine 5.8.4 / Windows x64 / DX12 SM6。  
> 本文是 `ROADMAP.md` 第三阶段（M3）的执行级拆分，供新对话直接实施；与 `ROADMAP.md`、`ARCHITECTURE.md`、`COMPATIBILITY.md` 冲突时，产品边界与阶段门禁以 `ROADMAP.md` 为准，**Contract/线程/范围裁剪以本文为准**。  
> 前置依据：`docs/M1_ACCEPTANCE.md`（M1 已验收）、`docs/M2_ACCEPTANCE.md`（M2 S1–S4 已实施；**F5 干净机未运行、H6 视觉问题仍有待改进项**）。M3 启动不等待 M2 全部关闭，但 §3.4 的遗留项必须在 M3 内收口，不得静默继承。  
> 本文不提供日期或工期承诺；完成与否只看验收条件与门禁。  
> 执行位置：UE 构建/Cook/打包、vanilla oracle、干净机验收在 **M3 指定开发机**（M2 同机）执行；其他机器可编辑文档、运行无 UE 的 Runtime/Converter 部分，但不得据此宣称 M3 的 UE/渲染/干净机项通过。  
> 平台定位：**DX12/SM6 为唯一目标渲染路径**；DX11 不在 M3 范围，也不作为最低要求。

---

## 0. 一页结论

### 0.1 M3 的产物

- **可玩的 C++ Native Minecraft**：从「新世界/真实存档 → 采集 → 合成 → 建造 → 生存 → 探索 → 存档重载」形成主要单机玩法闭环；UE 是唯一玩家面向 Runtime。
- **Rendering Contract v3**：在 v2 的帧/网格/相机之上，增加动作意图、物品/背包、交互目标、实体/粒子、UI 显示列表、音频事件、存档状态；线程/所有权/单位/坐标继续写死在头文件。
- **R1/R2 UE 渲染**：实体、方块实体、通用方块模型（多层/旋转/多部件/连接态）、平滑光照/生物群系混合、粒子、天气、天空、水动画、第一人称手持物品、完整 HUD/界面。
- **完整 Host UI/音频**：UE 只绘制 Runtime 产出的 UI 显示列表并播放 Runtime 发出的声音事件；背包/合成/容器/生存逻辑全部在 Runtime，不在 UE 重建。
- **可写存档**：26.3 `dimensions/` Anvil、`level.dat`、`data/minecraft/*.dat`、`players/data` 与实体/POI 原子写、自动保存、崩溃恢复；不降级写、未知字段保留；原版可读取 Native 写出的存档。
- **世界生成与维度**：数据驱动的原生世界生成（地形/群系/表面/矿物/植被），区块生成后台化并持久化；主世界必做，下界最小闭环；末地与完整结构按缺口后置。
- **Converter v3**：数据生成器报告摄取、注册表/方块状态/物品/配方/战利品/模型/语言/声音/群系色的可重复产出；分析/差分/port ledger/Patch；版本升级演练；选择性翻译与 JRT 走门禁。
- **场景套件与验收**：公开、可复现的 `tests/scenarios/`；`tools/m3-acceptance.ps1` 输出通过率（门禁 ≥95%）、差分/存档往返/性能/错误矩阵报告；M3 RC NativeBuild。
- **M2 遗留收口**：F5 干净机、H6 视觉待改进清单、M2 已知限制（双面、tonemap、单群系色、多 element 模型、LZ4）在 M3 内逐项关闭或显式豁免。

### 0.2 M3 不做

- 多人/网络协议、原版服务器互通、账号/聊天签名。
- Java Mod/Mixin/Forge/Fabric；OptiFine/Iris shaderpack；资源包 shader 覆盖。
- 红石、活塞、命令方块、命令/聊天命令系统（调试控制台除外）。
- 26.3 全量行为/位级一致；完整 DFU 旧存档迁移；完整结构/要塞/村庄全功能；末影龙/凋灵等 Boss。
- 附魔、酿造、铁砧、村民交易、成就/统计等长尾系统（按缺口后置）。
- 正式 Launcher UI、安装器、账号、更新通道（M4）。
- 通用 Java→C++ 编译器、完整 JVM 模拟、JRT 扩张为产品目标。
- Lumen/Nanite/RT/PBR 等 UE 高级增强（R3）；DX11；Linux/macOS/主机。
- 公开分发 M3 RC；M3 仍为内部验收，分发前法律评审。

### 0.3 完成定义

在用户自有 26.3 输入上：同一份 `mc_26.3.dll` 驱动 `REIMPreview.exe`、`reim-headless.exe` 与 UE `REIMRuntime.exe`；场景套件（≥62 条）通过率 **≥95%**；真实存档经 Native 写入后原版可读取；新世界可从种子生成并持久化；主世界 + 下界最小闭环可玩；**UE 只消费 Contract v3，不包含第二套世界/存档/物理/背包/合成逻辑**；Converter 可重复产出并组合 NativeBuild；G3 通过。

### 0.4 怎么分（总览）

M3 的工程量约为 M2 的 3–5 倍。切分按三层同时约束：

1. **纵向可玩**：每个阶段结束都有一个可实际运行的演示，而不是「模块全绿但游戏不可玩」。
2. **横向并行**：Gameplay / World / Presentation / Converter / QA 五条轨道，契约与存档先冻结，后续只增量。
3. **阶段闭环**：每个阶段包含若干可自检的技术闭环（实现 + 构建 + 场景 + 记录），不是一个文件目录。

原计划的 10 个工作阶段按依赖相邻两两合并为 **5 个执行阶段**，阶段内仍按原工作包顺序推进：

```text
阶段 1 地基与世界    S1  A 基线/Contract + B 存档写入 + C 世界状态/光照（原 S1+S2）
阶段 2 交互与容器    S2  D 交互/物品 + E 容器/合成 + H UI（原 S3+S4）
阶段 3 生存与世界    S3  F 生存/实体/战斗 + G 世界生成/维度 + J 部分（原 S5+S6）
阶段 4 表现与工具链  S4  I 音频/资源 + J 剩余 + K Converter 数据 + L 分析/差分（原 S7+S8）
阶段 5 验收与版本    S5  M 翻译门禁 + L7 版本演练 + N 测试/差分 + O 打包/验收（原 S9+S10）
```

阶段内的 4A/4B、5A/5B 只是并行子轨，不是独立阶段；阶段收口以整个阶段的 DoD 为准。

**为什么这样切**：Contract/存档/世界状态是所有玩法的共同地基，放在最前；玩法按「交互 → 容器 → 生存 → 世界」的依赖顺序纵向切片；Converter 与渲染不阻塞主玩法，但有独立门禁。10 个原阶段按依赖相邻合并为 5 个，减少阶段间交接；每对内部保留原工作包的顺序与 DoD。最终验收与版本升级演练一起放在阶段 5，并且验收只能在阶段 1–4 全部完成后收口，避免边做边宣称通过。详细理由与反例见 §6。

---

## 1. 目标、边界与验收

### 1.1 目标

1. 用 C++ Runtime 实现主要单机玩法闭环，UE 只做宿主与渲染，不接管游戏规则。
2. 冻结并实现 **Contract v3**：动作意图、物品/背包、交互目标、实体/粒子、UI、音频、存档状态。
3. 实现 **可写世界**：区块、玩家、实体、POI、level.dat 的原子写与兼容性。
4. 实现 **方块/物品/容器/合成/战利品/生存/战斗** 的最小完整语义，范围由 §1.3 与场景套件封顶。
5. 实现 **实体/生物/AI/生成** 的有限集合，并有差分/场景证据。
6. 实现 **数据驱动世界生成** 与区块后台生成/持久化；主世界必做，下界最小闭环。
7. 实现 **R1/R2 渲染** 与 **UI 显示列表/音频事件** 的 Host 侧消费，保持单一实现。
8. Converter 从 M1/M2 的「数据提取 + UE Adapter 组合」升级为 **可重复、可差分、可打补丁、可版本演练** 的工具链。
9. 建立 M3 的场景/差分/性能/存档往返基线，供 M4 复用。

### 1.2 非目标

- 不做 Java Edition 启动路径；JDK 只用于离线 Converter/oracle。
- 不在 UE 中实现世界、存档、物理、方块表、背包、合成、AI、UI 逻辑。
- 不把 Java 渲染管线/Shader/RenderType 翻译成 UE；不做 GL/命令回放。
- 不承诺 26.3 全量玩法/全量方块/全量生物/全量结构；不承诺 P3 位级世界生成。
- 不把 Converter 扩张为通用 Java→C++ 编译器；JRT 只在翻译门禁通过时按最小面使用。
- 不启动多人、协议翻译、Java Mod、shaderpack。
- 不把 M3 包发给普通用户；M4 前不做产品分发。

### 1.3 范围封顶：「完整游戏」= 场景定义的闭环

M3 的「完整」不是 26.3 全量复刻，而是 **一组公开场景能稳定完成的单机闭环**。以下矩阵是范围合同；变更必须写入 `docs/M3_ACCEPTANCE.md` 的 Scope Changes，并同步更新场景套件。

| 系统 | M3 必做 | M3 尽力 | 后置 |
|---|---|---|---|
| 方块交互 | 破坏/放置/使用/选取、朝向与状态、硬度/工具速度/耐久、掉落、基础苦力怕爆炸 | 精准采集/时运、TNT/完整爆炸 | 红石/活塞/命令方块 |
| 物品/背包 | 物品栈、9 快捷栏 + 27 主背包、丢弃/拾取、堆叠/拆分 | 盔甲槽/副手、物品耐久可视化 | 潜影盒/末影箱 |
| 容器/合成 | 2×2/3×3 合成、工作台、熔炉、箱子、配方标签、基础战利品表 | 高炉/烟熏炉/石切机、铁砧 | 酿造/附魔/村民交易 |
| 生存/战斗 | 生命/饥饿/经验、摔落/溺水/岩浆/生物伤害、死亡/重生、近战冷却/击退 | 箭/弓、盔甲槽与基础减伤、状态效果 | Boss/多人 |
| 实体/生物 | 8 种首批生物（僵尸/骷髅/苦力怕/蜘蛛/牛/猪/羊/鸡）、物理/寻路/攻击/掉落/生成/消失 | 更多生物、繁殖/驯服、投射物扩展 | 末影龙/凋灵 |
| 世界生成 | 主世界地形/群系/表面/洞穴最小子集/矿物/树木/花草、种子确定性、区块持久化 | 下界生成完整化、结构（村庄/矿洞）子集 | 末地生成、完整要塞/结构 |
| 维度 | 主世界；下界加载 + 新世界下界最小生成 + 传送最小闭环 | 下界生成完整化 | 末地 Boss |
| 存档 | 区块/玩家/实体/POI/level.dat 写、原子写、自动保存、崩溃恢复 | 备份/恢复 UI | DFU 旧存档 |
| 光照 | 天空光/方块光 15 级引擎、增量更新、保存 | 平滑光照/AO 完整化 | 光照性能高级优化 |
| 时间/天气 | 昼夜循环、天空光衰减、雨/雷状态与保存 | 月相、群系温度/降雪 | 完整天气表现 |
| UI | HUD、背包、工作台、熔炉、箱子、暂停/设置、死亡、加载 | 资源包选择、更多设置页 | 成就/统计 |
| 音频 | 方块/脚步/受伤/生物/UI/环境事件、音量分类 | 音乐/唱片机 | 空间音频高级 |
| 资源包 | 纹理/模型/声音/语言覆盖、包优先级、缺失回退 | 动画纹理 | shader/光影 |
| 数据包 | 配方/战利品/世界生成/标签覆盖 | 函数/标签完整合并 | 命令 |
| 渲染 | R1 实体/方块实体/通用模型/基础环境；R2 粒子/天气/天空/水/手持 | 云/阴影/后处理 | Lumen/Nanite/RT |
| Converter | 数据生成器摄取、schema v3、模型/配方/战利品/资产、可重复 | 差分/Patch/升级演练 | 全量翻译/JRT |

**模式**：M3 只要求生存模式；创造/旁观/硬核不作为 M3 验收项。创造模式可作为开发调试配置，但不进入场景套件，也不作为产品承诺。

**首批内容清单（用于场景与数据覆盖，不等于全部可渲染）**

- 方块族：草/泥土/沙/砂砾/石头/矿石（煤/铁/铜/金/钻石/红石/青金石/绿宝石）、原木/木板/树叶（橡木/云杉/白桦）、工作台/熔炉/箱子、火把、玻璃、水/岩浆、雪层、草/花/农作物、树苗、门/活板门/栅栏/墙/台阶/楼梯（至少橡木族）、黑曜石/下界岩/传送门。
- 物品族：木/石/铁/钻石 镐/斧/锹/剑/锄、面包/苹果/熟肉、煤/木炭/铁锭/金锭/钻石/木棍/火把、箭/弓、水桶（若实现液体交互）。
- 生物族：僵尸、骷髅、苦力怕、蜘蛛、牛、猪、羊、鸡。
- 维度：主世界必做；下界最小闭环；末地只要求加载已有区域（不要求生成/Boss）。

### 1.4 验收（对应 ROADMAP M3）

1. 公开脚本化场景中，主要单机玩法可完成（移动/交互/背包/合成/存档等），通过率 **≥95%**。
2. 真实存档可读写；原版可读取 Native 写出的存档（在支持范围内）。
3. 对目标版本可重复执行分析、转换、生成、构建和验证，结果可复现。
4. 差分测试覆盖核心世界/tick/存档/渲染样本。
5. 版本升级可通过 diff + Patch/生成完成，不手工重写整个 Runtime。
6. 不启动 Java Edition，不引入通用 Java→C++ 编译器目标。
7. Converter 工具链建立在 M1/M2 的摄取/分析/生成/样本转换能力之上，不是重新实现一套。

**M3 执行补充**

8. Contract v3 在阶段 1 冻结；Preview/headless/UE 同步升级；同一 DLL 双 Host 状态一致。
9. 存档写入不降级、未知字段保留；崩溃/磁盘满/只读不破坏原存档。
10. UI/音频/实体由 Runtime 数据驱动；UE 不包含背包/合成/世界/存档逻辑。
11. 20 TPS 与渲染 FPS 继续解耦；M3 负载（实体/粒子/世界生成）下仍达标。

### 1.5 G3 门禁

| 条件 | 通过标准 | 不通过时的处理 |
|---|---|---|
| 玩法闭环 | 场景套件 ≥95%，且核心闭环（采集→合成→建造→生存→存档）无整类失败 | 收缩范围/补原生实现，不硬推全量；不得删除场景规避 |
| 存档兼容 | 往返矩阵通过：Native 读→写→原版读；原版写→Native 读→写→原版读；未知字段保留 | 修 NBT/Anvil/数据组件路线，不把问题留给 M4 |
| 工具链可重复 | Converter 输出与 NativeVersion/NativeBuild 组合两次逐字节一致；DLL/UE 二进制用 canonical hash（必要时 `/Brepro`），不承诺跨机字节一致 | 固定遍历/命名/排序，补缓存与报告 |
| 差分有效 | 世界/方块更新/光照/配方/战利品/存档/渲染均有 oracle 或基线；失败可定位 | 补 harness，不把「看起来对」当通过 |
| 契约/双后端 | v3 冻结并同步；Preview/headless/UE 同 DLL；无第二套游戏逻辑 | 修 Contract/Bridge，不复制实现 |
| 三 Host v3 一致 | 同一 DLL 在 Preview/headless/UE 上对固定 v3 场景（物品/实体/UI 状态）一致 | 修 Contract/Bridge，不复制逻辑 |
| 性能/稳定 | 20 TPS ±0.2；GT/RT 预算；30 分钟无泄漏/无崩溃（soak 时长由用户定） | 优化/收缩视距与实体数，记录证据 |
| 干净机/RC | M3 RC 在干净机（无 UE/JDK/MSVC/CMake/VS）进入世界/游玩/退出；或用户显式豁免并记录 | 修打包/运行库/依赖，不写成开发机通过 |
| 版本升级 | 真实第二版本或批准的合成 delta 完成 diff→生成/Patch→构建→回归；未执行写 `not_run` | 不把 `not_run` 当通过；需要用户输入 |
| M2 遗留收口 | F5/H6/已知限制逐项关闭或显式豁免，记录在 `M3_ACCEPTANCE.md` | 不静默继承 |

**门禁规则**：1–7、G3 及上表新增门禁全部满足才进入 M4。项目所有者可显式豁免某一条，但必须在 `docs/M3_ACCEPTANCE.md` 记录「未执行 + 豁免」，不得写成实测通过。M2 的 F5 豁免不自动继承到 M3；M3 的干净机项需单独记录。

### 1.6 场景套件口径（通过率的定义）

场景是 M3 的唯一「完整度」合同。首发套件不少于 **62 条**，按能力类别分布：

| 类别 | 首发条数 | 覆盖示例 |
|---|---|---|
| INT 交互 | 10 | 破坏/放置/使用/选取、朝向、工具速度、耐久、水/雪/植物 |
| INV 背包/容器 | 12 | 堆叠/拆分、2×2/3×3、熔炉、箱子、快捷栏、掉落/拾取 |
| SUR 生存 | 8 | 摔落/溺水/岩浆/生物伤害、饥饿、回血、死亡/重生、经验 |
| MOB 生物/战斗 | 8 | 生成/消失、寻路、攻击、掉落、弓箭、难度 |
| WLD 世界/维度 | 8 | 种子确定性、群系/表面/矿物/树、下界传送、昼夜/天气 |
| SAV 存档 | 6 | 往返、未知字段、自动保存、崩溃恢复、只读/磁盘满 |
| UI/输入 | 6 | HUD、背包/容器界面、暂停/死亡、指针/键盘、缩放 |
| RES 资源/音频 | 4 | 资源包覆盖、缺失回退、声音事件、语言 |
| **合计** | **≥62** | 门禁 ≥95% → 至少 59 条通过 |

**规则**

- 场景以 JSON 描述，固定世界/种子/时间/输入 tick；核心玩法场景必须在 **headless/Preview** 可判定，不依赖 UE。
- UE 场景只覆盖 UI/视觉/音频/打包；不得用「UE 截图好看」替代玩法断言。
- 断言类型：方块状态、玩家属性、背包槽位、实体集合、UI 画面 id/元素、音频事件序列、存档语义 hash。
- 新增功能必须新增场景；删除/降级场景需在验收记录写明原因，通过率按当时套件计算。
- 未实现功能不得用空场景/跳过场景计为通过；脚本输出 `not_run` 而不是 `pass`。
- 通过率分母是套件总数（首发 62 条）：`pass / 62`；`not_run` 不计为通过。
- 核心闭环类别（INT/INV/SUR/SAV/UI）不得整类 `not_run`；`not_run` 超过 3 条即视为门禁失败，除非用户显式豁免并记录。
- 场景 fixture 放在 `tests/fixtures/`，由固定种子生成脚本产出，脚本与 hash 入库；不得依赖用户存档。
- SUR 场景归属 M3-F（F10），WLD 场景归属 M3-G（G10），INT/INV/MOB/SAV/UI/RES 分别由 D8/E8/F9/B7/H7/I7 负责。
- 对应阶段：SAV 归阶段 1；INT/INV/UI 归阶段 2；MOB/SUR/WLD 归阶段 3；RES 归阶段 4；全部场景在阶段 5B 汇总收口。

---

## 2. 技术基线与关键决策

### 2.1 决策表

| 编号 | 决策 | 内容 |
|---|---|---|
| M3-D01 | 游戏权威 | 世界/物理/方块/物品/背包/合成/生存/AI/存档/光照仍由 C++ Runtime 决定；UE 只是宿主与渲染后端 |
| M3-D02 | 单一实现 | `mc_26.3.dll` 只有一份；Preview/headless/UE 是不同 Host；不复制 Runtime、不复制网格/存档/物理/背包逻辑 |
| M3-D03 | ABI v3 | 整份 Contract 升到 3；新增动作、物品、交互、实体、粒子、UI、音频、存档状态；Preview/headless/UE 同步重建，不保留 v2 服务 |
| M3-D04 | 动作 = 意图 | Host 只提交 `ReimActionEvent`（破坏/放置/使用/攻击/槽位/指针）；Runtime 在下一 tick 校验并执行；Host 不直接改世界 |
| M3-D05 | UI = Runtime 显示列表 | Runtime 产出声明式 2D 显示列表（矩形/纹理四边形/字形），负责布局、命中测试、背包/合成语义；Host 只绘制并转发指针/键；不在 UE 实现物品/背包逻辑 |
| M3-D06 | 实体 = Runtime 模型实例 | Runtime 产出模型定义（部件/盒/纹理/变换）与每实体部件变换；Host 只缓存模型、插值、绘制；不在 UE 解析 MC 模型/动画 |
| M3-D07 | 音频 = 事件 + 资产 | Runtime 发出声音事件（id/位置/音量/音高/循环）；Host 解码 OGG 并播放/混音；音频事件不进玩法逻辑 |
| M3-D08 | 存档写 | Runtime 独占写入；原子临时文件 + fsync + rename；`session.lock`；自动保存；不降级写；未知 NBT 字段保留 |
| M3-D09 | 光照 | Runtime 原生天空光/方块光 15 级引擎，增量更新并写入存档；Host 只消费顶点 light |
| M3-D10 | 世界生成 | Runtime 原生、数据驱动（worldgen JSON/报告）；后台 worker 生成；固定种子确定性；按语料分阶段，不承诺 P3 全量 |
| M3-D11 | 范围封顶 | 以 §1.3 矩阵与场景套件定义「完整」；不做全量 parity 宣称 |
| M3-D12 | Converter 优先数据 | 注册表/方块状态/物品/配方/战利品/模型/资产优先由数据生成器与资源包产出；代码翻译按子系统门禁 |
| M3-D13 | Patch 体系 | 生成物/代码修改进入版本化 Patch 集；可重放、三路合并；生成代码不手改；不散落 `#if MC_VERSION` |
| M3-D14 | 差分 | 原版是规格；vanilla server/client oracle 离线运行（JDK 25），固定种子/时钟/输入；oracle 产物不随包分发 |
| M3-D15 | 双 Host 定位 | UE 是玩家面向完整 Host；Preview/headless 是回归/差分 Host，保留 HUD/调试与最小 UI，不要求全量音频/资源包 |
| M3-D16 | 线程 | 模拟线程固定 20 TPS；区块生成/网格/保存后台化；GT 只消费快照；RT 上传/绘制；DLL 不碰 UObject |
| M3-D17 | 性能预算 | 模拟 tick p99、GT、RT、生成吞吐、保存暂停均有预算；先测再优化 |
| M3-D18 | 分发 | M3 RC 仅内部/干净机验收；不打包 Mojang 资产；分发前法律评审（M4） |
| M3-D19 | 版本升级 | diff → 分诊 → 生成/翻译/Patch → 构建 → 差分回归；真实第二版本优先，无则用受控合成 delta 演练并标注 |
| M3-D20 | 资源/数据包 | 资源包支持纹理/模型/声音/语言覆盖；数据包支持配方/战利品/世界生成/标签；shader 覆盖忽略并警告；命令不做 |

### 2.2 M2 结束时的真实基线（M3 的起点）

| 层 | 已有 | 缺口（M3 必须补） |
|---|---|---|
| Runtime | C++20 DLL；真实 26.3 存档只读；单维度；完整方块 + 有限非整方块；玩家移动/碰撞/跳跃；20 TPS；方向光兜底；无光照更新 | 写存档；方块编辑/更新；光照引擎；物品/背包/容器/合成；生存/实体/AI；世界生成；时间/天气 |
| Contract | v2：帧块、网格增量、相机/玩家/环境、插值、资产、输入、暂停 | 动作意图、物品/背包、交互目标、实体/粒子、UI、音频、存档状态 |
| UE Host | 5.8.4 R0：单 SceneProxy、图集、烘焙顶点色、雾/天空、第三人称代理、HUD、菜单、NativeBuild 校验、Shipping | R1/R2；UI 画布；实体/粒子/方块实体；音频；通用模型；平滑光照；颜色校准 |
| Converter | client.jar 提取 blocks/textures/atlas/biome tint；schema v2；`package`/`assemble`；UE Adapter pin | 数据生成器 reports；注册表/物品/配方/战利品/世界生成/语言/声音；模型系统 v3；分析/差分/Patch；翻译门禁 |
| 测试 | Catch2 32 用例；双后端对照；错误矩阵；视觉断言；验收脚本 | 场景 DSL/套件；存档往返矩阵；worldgen/光照/配方/战利品差分；性能/soak 扩展 |
| 遗留 | M2 F5 未运行；H6 视觉有未列明问题；UE 不透明双面；tonemap 偏暗；单群系色；多 element/旋转模型 skip；LZ4 读不支持 | 在 M3 内逐项关闭或记录豁免（§3.4） |

### 2.3 端到端数据流（v3）

```text
Host 帧循环（GT）
  ├─ 键盘/鼠标/指针 → submit_input / submit_action（意图队列）
  ├─ fetch_frame ← Runtime（20 TPS 发布：网格/实体/粒子/UI/音频/玩家/环境）
  ├─ 插值与 UI 画布提交
  ├─ RT：网格/实体/粒子/UI 上传与绘制；Audio 线程：声音事件播放
  └─ release_frame（RT/音频消费完成后）

Runtime 模拟线程（20 TPS）
  ├─ 输入/动作 → 玩家/方块/物品/背包/容器/实体/AI/战斗
  ├─ 方块更新 → 光照更新 → 网格脏标记 → 网格 worker
  ├─ 世界生成 worker → 区块结果合并 → 持久化
  ├─ 时间/天气/生存 tick
  ├─ UI 显示列表构建（背包/容器/HUD）
  └─ 保存快照 → 保存 worker（原子写）
```

- 世界状态只在 Runtime；Host 不持有可写世界模型。
- 帧块仍是 Host 所有：v3 把网格/实体/粒子/UI/音频都放进同一个 blob，Host 消费后 `release_frame`。
- UI 命中测试在 Runtime：Host 发送指针坐标/点击，Runtime 决定点到了哪个槽/按钮。
- 保存不阻塞 tick：tick 线程只做快照，写盘在保存 worker。

### 2.4 线程与所有权一览（v3）

| 数据 | 创建者 | 使用者 | 释放者 |
|---|---|---|---|
| 世界/方块/物品/背包/实体/AI/UI 状态 | Runtime 模拟线程 | Runtime 模拟线程 | Runtime（destroy） |
| 帧快照（网格/实体/粒子/UI/音频/玩家/环境） | Runtime 在 `fetch_frame` 内 `host->alloc` | GT 读取；RT/音频消费 | Host `release_frame`；Runtime `host->free` |
| 区块生成结果 | 生成 worker | 模拟线程合并 | Runtime |
| 网格顶点/索引 | 网格 worker | RT 上传 | 帧块生命周期 |
| 保存快照 | 模拟线程 | 保存 worker | 保存 worker |
| 资产（图集/物品图集/字体/声音/语言/模型定义） | Runtime 资产缓存 | GT 创建纹理/解码；RT 上传 | Runtime（destroy） |
| UE SceneProxy/纹理/音频组件 | GT | RT/音频线程 | UE 生命周期 |
| 输入/动作事件 | GT | 模拟线程（下一 tick） | Runtime 队列 |
| 日志 | 任意线程 | Host 回调 | 调用期间有效 |

- `fetch_frame` 仍只允许 GT 单线程；锁内只交换快照，blob 分配/序列化在锁外。
- `release_frame` 任意线程、可乱序；最多 4 个未释放帧块；超限 `REIM_ERR_BUSY`。
- 生成/网格/保存 worker 不触碰 UObject、不调用 Host 回调（除日志快照）。
- `submit_action` 任意线程；队列有界；满了记录并拒绝，不阻塞模拟。

### 2.5 运行时目标产物

| 目标 | 类型 | 说明 |
|---|---|---|
| `mc_26.3.dll` | SHARED | C++20、`/MD`、无 UE、无 JVM；导出 ABI v3 |
| `REIMPreview.exe` | EXE | 回归/差分 Host；HUD + 最小 UI；消费 v3 |
| `reim-headless.exe` | EXE | 场景/差分/存档往返；无渲染 |
| `REIMRuntime.exe` | EXE | UE 5.8.4 Development/Shipping；R1/R2 + 完整 UI/音频 |
| `reim-convert.exe` | EXE | 数据生成/摄取/分析/差分/patch/package/assemble |
| `m3-tests.exe` | EXE | Catch2 单元/集成；含存档、光照、配方、战利品、UI 快照 |
| `reim-scenario.exe` | EXE | 场景 DSL runner；headless/Preview 执行与报告 |
| `REIM.uproject` | 工程 | UE Adapter；模块源码入库，`.uasset/.umap` 走 Git LFS |

---

## 3. 输入与前置

### 3.1 开发机提供

- M2 指定开发机环境：UE 5.8.4、MSVC 14.44.35207、Windows SDK 10.0.26100、CMake 3.28+（实测 4.4.4）、Python 3.12、Git LFS。
- **JDK 25**（离线 Converter/oracle；Runtime 不加载 JVM）。
- **26.3 server jar**：用于官方数据生成器 `--reports` 与离线 oracle；用户自有。
- 用户自有 26.3 存档副本（**验收前备份**；M3 会写入）。
- 干净验收机（Windows 10/11 x64，未安装 UE/JDK/MSVC/CMake/VS；DX12/SM6 GPU）。
- 磁盘：UE/工程/打包/生成物建议预留 200 GB；内存 32 GB 推荐。
- 第二个版本输入（26.3 相邻版本）或受控合成 delta 的批准（版本升级演练，§9.7）。

### 3.2 需要用户决定/提供

| 项 | 说明 | 默认建议 |
|---|---|---|
| 范围档位 | §1.3 必做/尽力/后置是否接受 | 按必做档执行；尽力项在波次余量内做 |
| 世界生成目标 | 地形/群系/表面/矿物/植被的语料级一致，还是要求结构/洞穴完整 | 前者；洞穴最小子集纳入必做，结构子集尽力 |
| 版本升级演练输入 | 真实第二版本，或批准合成 delta | 优先真实版本；否则合成 delta 并标注 |
| soak 时长 | 30 分钟/2 小时/更长 | 30 分钟门禁；长时由用户另跑 |
| H6 视觉问题清单 | 用户逐项说明待改进项 | 未提供前按 M2 §17.4 + 本计划 §3.4 清单处理 |
| 干净机 | 物理机/GPU 直通 VM | 必须提供或显式豁免 |

### 3.3 缺失处理

- 缺 server jar/报告：数据生成项（M3-K）标阻塞；不得用猜的表替代。
- 缺干净机：M3-O 的干净机项标阻塞；不得用开发机模拟。
- 缺第二版本且不批准合成 delta：版本升级演练标「未执行」；不得写成通过。
- 缺存档备份：禁止开始写存档相关工作；先备份并记录。
- 缺 UE：只允许推进无 UE 的 Runtime/Converter/场景部分；不得生成 UE 截图。

### 3.4 M2 遗留收口（M3 内必须处理）

| 遗留 | 处理 | 归属 |
|---|---|---|
| F5 干净机未运行 | M3-O 用新 RC 重跑；若仍无机器，用户显式豁免并记录 | 阶段 5B（M3-O） |
| H6 视觉问题未列明 | 先列出问题清单（用户/截图对照），逐项修；未确认不得写通过 | 阶段 1 收集，阶段 4 收口 |
| UE 不透明双面 | 通用模型/绕序修正后恢复单面 + 背面剔除；若不能，记录原因与 GPU 代价 | 阶段 1 / 阶段 4 |
| UE tonemap 偏暗 | 色彩校准/曝光设置；与 Preview 差异阈值化 | 阶段 4 |
| biome 单色 | 3×3 生物群系混合/平滑；至少草/叶/水 | 阶段 3 / 阶段 4 |
| 多 element/旋转模型 skip | 模型系统 v3 通用化；楼梯/台阶/栅栏/门/墙至少首批 | 阶段 1 / 阶段 4 |
| LZ4 不支持 | 已 vendored `third_party/lz4`（v1.9.4，BSD-2-Clause，SHA-256 见 `third_party/LICENSES.md`）；阶段 1 接入读，写继续优先 zlib | 阶段 1（B1） |
| 加载页未截图/键位未自动化 | 场景/截图补齐 | 阶段 5B |

---

## 4. 仓库布局（M3 增量）

```text
REIM-Launcher/
├─ runtime/
│  ├─ contract/
│  │  ├─ reim_abi.h          # v3：实例/控制/动作/资产/保存
│  │  ├─ reim_render.h       # v3：帧、网格、相机/玩家/环境、实体/粒子
│  │  ├─ reim_ui.h           # 新增：UI 显示列表/命中测试契约
│  │  ├─ reim_audio.h        # 新增：音频事件
│  │  └─ reim_game.h         # 新增：物品/背包/容器/交互快照
│  ├─ mc-native/
│  │  ├─ core/               # 模拟、玩家、输入
│  │  ├─ game/               # 交互、物品、背包、容器、合成、战利品、生存、战斗
│  │  ├─ entity/             # 实体、生物、AI、生成、投射物
│  │  ├─ world/              # 方块状态、更新、光照、生成、维度、时间天气
│  │  ├─ save/               # 26.3 dimensions Anvil、level.dat、data/minecraft、players/data 写
│  │  ├─ ui/                 # 布局、显示列表、字体、命中测试
│  │  ├─ audio/              # 声音注册/事件
│  │  ├─ model/              # 通用方块/实体模型网格化
│  │  ├─ data/               # 方块/物品/配方/战利品/世界生成/资产表
│  │  └─ asset/              # 资源包/数据包加载、图集、语言、声音
│  ├─ preview/               # v3 Host；HUD + 最小 UI + 调试
│  ├─ headless/              # 场景/差分/存档往返
│  └─ host/                  # UE 5.8.4
│     ├─ Source/REIMBridge/  # v3、动作、帧块、资产校验
│     ├─ Source/REIMRenderer/# R1/R2、模型/实体/粒子、UI 画布
│     ├─ Source/REIMUI/      # Host 原生覆盖/致命错误；不实现背包逻辑
│     ├─ Source/REIMAudio/   # 声音事件消费/播放
│     └─ Source/REIMInput/   # 键鼠/指针 → 输入/动作
├─ converter/
│  ├─ data/                  # reports 摄取、schema v3、模型/配方/战利品/资产
│  ├─ analyze/               # 类/方法/字段、hash、依赖、port ledger
│  ├─ produce/               # 数据/代码生成、确定性排序
│  ├─ patches/26.3/          # 版本化 Patch 集
│  ├─ verify/                # 差分/oracle/报告
│  └─ adapters/ue/           # Adapter pin（沿用）
├─ tests/
│  ├─ scenarios/             # 公开场景 JSON
│  └─ fixtures/              # 固定种子 fixture 生成脚本/hash
├─ tools/m3-*.ps1            # build/smoke/scenario/diff/save/version/acceptance
├─ build/m3/                 # 构建、报告、截图
└─ docs/M3_PLAN.md / M3_ACCEPTANCE.md
```

**源码管理**

- 继续使用 Git LFS 管理 `.uasset/.umap`；不提交 `generated/`、存档、Mojang 文件、Cooked 内容、Shipping 包、PDB。
- 新增资源：LZ4 v1.9.4（BSD-2-Clause）已 vendored 到 `third_party/lz4`；字体/音频解码若新增需固定版本与许可证清单。
- oracle/差分产物（区块 golden、存档副本、声音样本）不进仓库；放 `build/m3/oracle/` 并 gitignore。

### 4.1 构建产物

```text
build/m3/bin/mc_26.3.dll
build/m3/bin/REIMPreview.exe
build/m3/bin/reim-headless.exe
build/m3/bin/reim-scenario.exe
build/m3/bin/reim-convert.exe
build/m3/bin/m3-tests.exe
runtime/host/Binaries/Win64/REIMRuntime.exe
build/m3/ue-dev-package/Windows/REIMRuntime.exe
build/m3/ue-shipping-package/Windows/REIMRuntime.exe
build/m3/adapter/26.3/                 # ue_adapter.json + hash
build/native-version/26.3/             # v3 NativeVersion
build/native-build/26.3/               # v3 NativeBuild（最终可启动）
build/m3/reports/                      # 场景/差分/性能/截图
build/m3/oracle/                       # 本机 oracle 产物（不入库）
```

### 4.2 实例配置 v3

`instance.json` 在 v2 字段上追加（Host 读取；Runtime 只接受 `ReimInstanceConfig`）：

```json
{
  "world_path": "D:\\Minecraft\\.minecraft\\saves\\REIM-M3-26.3",
  "generated_path": "D:\\EngineeringDocuments\\REIM-Launcher\\generated\\26.3",
  "native_version_path": "D:\\EngineeringDocuments\\REIM-Launcher\\build\\native-version\\26.3",
  "minecraft_dir": "D:\\Minecraft\\.minecraft",
  "render_distance": 8,
  "render_level": 2,
  "mouse_sensitivity": 0.15,
  "invert_y": false,
  "gamemode": "survival",
  "difficulty": "normal",
  "create_world": false,
  "seed": 0,
  "autosave_seconds": 60,
  "ui_scale": 2,
  "audio": { "master": 1.0, "music": 0.7, "blocks": 1.0 },
  "resource_packs": [],
  "data_packs": [],
  "scenario": null,
  "log_dir": "D:\\EngineeringDocuments\\REIM-Launcher\\build\\m3\\logs"
}
```

- `render_level`：1=R1，2=R2；0 保留为 R0 回退（调试）。
- 相对路径以 `instance.json` 所在目录解析；包内模板只用相对路径/占位符。
- 资源包/数据包路径由 Host 解析并通过 `ReimInstanceConfig` 传给 Runtime；Runtime 不读 Host 配置文件。
- 场景模式（`scenario`）只用于自动化；Shipping 默认拒绝。

---

## 5. 工作包

> 每个工作包完成后按 DoD 自检；不要跨包堆积未验证代码。包内任务编号用于阶段交接。

### M3-A — 基线与 Contract v3

| 任务 | 内容 | DoD |
|---|---|---|
| A1 | M2 遗留收口清单 + **26.3 实档存档审计**：dimensions/data/weather/players/实体/POI 的实际路径与字段 | `M3_ACCEPTANCE.md` 有清单、实档审计与归属；不静默继承 |
| A2 | Contract v3 头文件：动作（含持续挖掘/使用、拖拽/滚轮）、物品/背包、交互、实体/粒子、UI（裁剪/字形/物品图标）、音频、资产、保存状态；写线程/所有权/单位/坐标 | 编译期布局断言；Preview/headless/UE 可编译；`ARCHITECTURE.md` §8 同步 |
| A3 | Host 同步：Preview/headless/UE Bridge v3；能力协商；`render_level` 0/1/2；新增 `m3-debug`/`m3-release` preset | 三 Host 启动并消费空帧；v2 服务不保留 |
| A4 | 动作通道：`submit_action`、意图校验、队列、下一 tick 应用、拒绝日志；指针/拖拽/滚轮与 start/stop 语义 | 单测覆盖非法槽位/越权/超距/满队列/按住/松手；Headless 可脚本化 |
| A5 | UI 契约 spike：矩形/纹理四边形/字形/裁剪矩形/缩放；字体图集；指针 hover/拖拽/滚轮；命中测试；方块物品等距图标 | Preview/UE 显示同一测试屏；文本可读；点击/拖拽/裁剪正确；3D 物品图标可见 |
| A6 | 实体/粒子/音频通道 spike：空列表、模型定义资产、一个盒模型、一个声音事件 | UE 显示一个模型实例并播放一次声音；无第二套逻辑 |
| A7 | 存档写 spike：按 26.3 `dimensions/` 读一块区域→改一个方块→写回→原版可读；`level.dat` 与拆分数据文件最小写 | 存档副本往返成功；原版启动无损坏；记录实档路径 |
| A8 | 场景/差分骨架：JSON DSL、headless runner、断言、报告；首批 5 条 | `reim-scenario` 可跑；报告含 pass/fail/not_run；失败可定位 |
| A9 | 冻结评审：v3 头文件 hash、schema v3 草案、线程/预算；记录冻结决议 | `M3_ACCEPTANCE.md` 有冻结记录；后续只增不改语义 |

**验收**：三 Host 在同一真实存档上启动；空 UI/实体/音频通道工作；一个方块写入后原版可读；场景骨架可跑。

### M3-B — 存档写入与持久化

| 任务 | 内容 | DoD |
|---|---|---|
| B1 | `WorldWriter`：26.3 `dimensions/<ns>/<dim>/region` 路径、区域读-改-写、sector 分配、压缩（zlib 写；gzip/zlib/none/LZ4 读；LZ4 写可选；`.mcc` 读+写策略）、临时文件+FlushFileBuffers+ReplaceFile/MoveFileEx；维护 `Status`/`isLightOn`/heightmaps | 写入后原版可读；断电/中断不损坏原文件；LZ4/.mcc 用例通过 |
| B2 | `level.dat` 写：保留未知字段，更新 spawn/time/version；同步写 `data/minecraft/weather.dat`、`world_clocks.dat`、`world_gen_settings.dat`、`game_rules.dat`；原子替换 | 语义 diff 仅预期字段变化；原版可读；拆分数据文件不丢未知字段 |
| B3 | 玩家数据：26.3 `players/data/<uuid>.dat`（及兼容字段）；位置/朝向/生命/饥饿/经验/背包；需要含玩家的 fixture | 重启后玩家状态恢复；原版可读；无玩家存档明确跳过 |
| B4 | 实体/POI 写：先按 26.3 实档确认存放位置（区块 NBT 或 `dimensions/.../data`）；UUID/类型；未加载区块保持原样 | 实体往返；原版可读；不丢未知实体；实档路径记录在验收 |
| B5 | 自动保存、退出保存、崩溃恢复、备份轮转；多文件保存 journal/marker；磁盘满/只读/锁冲突错误路径 | 保存不阻塞 tick；多文件中断可恢复；错误明确；原存档不被破坏 |
| B6 | 往返矩阵：Native 读→写→原版读；原版写→Native 读→写→原版读；未知字段/压缩变体/.mcc/拆分数据文件/玩家/实体/POI | 报告逐项 pass；差异可解释 |
| B7 | 保存场景（SAV-01..06）、实档审计 fixture 与语义 NBT diff 工具 | 场景通过；diff 输出可读 |
| B8 | 性能：快照/写盘预算；大区域保存；后台保存 | 主线程暂停 < 50 ms；保存期间 TPS 稳定 |

**验收**：真实存档可写；原版读取；未知字段保留；崩溃/磁盘异常不损坏。

### M3-C — 世界状态、方块行为与光照

| 任务 | 内容 | DoD |
|---|---|---|
| C1 | 方块状态注册表：name+properties→state id；section 调色板；`set_block`；邻居更新 | 编辑后状态/渲染/保存一致 |
| C2 | 方块行为表：solid/collision AABB/liquid/gravity/replaceable/plant support/face culling/light emission+opacity/tool tier；每项标明来源（report/Java 代码/原生表）；首批行为实现 | 首批方块行为场景通过；数据缺失有明确回退 |
| C3 | 计划 tick/随机 tick：作物/树苗/雪/冰/液体流动子集 | 作物生长/雪融化/水流动场景通过 |
| C4 | 光照引擎：天空光/方块光 BFS、高度图、增量更新、跨 section、保存/加载 | 编辑/放置火把/挖洞的光照与 vanilla 语料一致 |
| C5 | 区块 section 生命周期：实体/方块实体列表、脏标记、网格重算预算 | 编辑不卡顿；卸载/重载无残留 |
| C6 | 方块实体：箱子/熔炉/告示牌（首批）数据、tick、保存、渲染数据 | 方块实体交互/保存/渲染通过 |
| C7 | 通用模型系统（Runtime 侧）：Converter 模型定义 → 状态/邻居上下文 → 网格；多层/旋转/多部件/连接态（栅栏/墙/玻璃板） | 楼梯/台阶/栅栏/门/墙首批不再 skip；Host 不解析模型 |
| C8 | 差分：方块更新 trace、光照数组、状态变化 vs vanilla | 报告逐项 pass 或解释 |
| C9 | 性能：tick 预算、网格 worker、重算批量 | tick p99 达标；无单 tick 尖峰 |

**验收**：可编辑世界；光照正确；通用模型首批可见；20 TPS。

### M3-D — 交互与物品

| 任务 | 内容 | DoD |
|---|---|---|
| D1 | 射线选取：方块/实体、触及距离、命中面/点；目标快照 | 第一/第三人称选取正确；越界不命中 |
| D2 | 动作执行：破坏/放置/使用/攻击；按住/松手的持续挖掘与使用；朝向/状态属性；替换规则；冷却 | 放置/破坏规则场景通过；非法动作被拒绝；松手停止 |
| D3 | 物品表 v3：堆叠、工具/等级/耐久、食物、方块物品、使用动作、燃料、属性 | Runtime 加载并校验；不匹配拒绝 |
| D4 | 物品栈与背包模型：9 快捷栏 + 27 主背包；堆叠/拆分/交换；丢弃 | 背包操作场景通过；保存/恢复 |
| D5 | 掉落物实体：物理、合并、延迟拾取、消失；经验球（若生存波次就绪） | 掉落/拾取场景通过 |
| D6 | 手持物品/挥手/使用动画数据；第一人称手持渲染数据 | UE 第一人称可见手持；切换物品更新 |
| D7 | 方块掉落/基础战利品 v1；工具速度/耐久；破坏时间 | 采集闭环场景通过 |
| D8 | 交互场景（INT-01..10）与存档集成 | 场景通过；重载后一致 |
| D9 | 选取方块/快捷栏选择/数字键 | 场景通过；Host 不写世界 |

**验收**：可采集/放置/持有/掉落/拾取；工具耐久；存档恢复。

### M3-E — 容器、合成与战利品

| 任务 | 内容 | DoD |
|---|---|---|
| E1 | 容器抽象与槽位事务：点击/Shift 点击/拖拽（基础）、游标物品、拒绝非法操作 | 事务场景通过；无物品复制/丢失 |
| E2 | 背包界面模型：2×2 合成格、快捷栏、主背包；界面切换 | UI 快照与交互场景通过 |
| E3 | 工作台 3×3；有序/无序配方；标签解析；配方匹配 | 配方矩阵场景通过；数据驱动 |
| E4 | 熔炉：输入/燃料/输出、熔炼配方、点燃状态、方块实体 tick/保存 | 熔炼时序场景通过；保存恢复 |
| E5 | 箱子/木桶：存储、界面、保存；双箱（尽力） | 存取/保存场景通过 |
| E6 | 战利品表：方块/实体/箱子；条件/函数子集；不支持的函数明确跳过并报告；确定性 RNG | 抽取结果与 oracle 一致或解释；不支持项可见 |
| E7 | 容器 UI：槽位/提示/文本；点击语义；界面关闭 | UI 场景通过；Host 无合成逻辑 |
| E8 | 场景 INV-01..12；资源包/数据包覆盖配方/战利品 | 场景通过；覆盖生效 |
| E9 | 差分：配方输出、熔炼时序、战利品抽取 vs oracle | 报告通过 |

**验收**：采集→合成→工作台/熔炉/箱子闭环；保存重载；无复制漏洞。

### M3-F — 生存、实体、生物与战斗

| 任务 | 内容 | DoD |
|---|---|---|
| F1 | 玩家属性：生命/最大生命/饥饿/饱和/消耗/氧气/经验等级与进度；回血/饿死 | 属性场景通过；HUD 数据正确 |
| F2 | 伤害来源：摔落/溺水/岩浆/火/窒息/生物/虚空；无敌帧；基础护甲减伤（盔甲槽实现时） | 伤害场景通过；数值可解释 |
| F3 | 死亡/重生：死亡界面、掉落策略、重生点、存档 | 死亡/重载场景通过 |
| F4 | 实体框架：id/UUID、类型数据、物理、追踪、保存/加载 | 实体往返；未知实体保留 |
| F5 | 首批 8 种生物：物理、碰撞、生成规则（光照/群系/高度）、消失；苦力怕基础爆炸（伤害+有限方块破坏） | 生成/消失/爆炸场景通过 |
| F6 | AI：游荡/看向/攻击/逃跑/跟随；A* 寻路子集；避免卡墙 | 微场景 trace/断言通过 |
| F7 | 战斗：攻击冷却/伤害/击退/暴击（基础）、生物攻击；弓/箭（尽力） | 战斗场景通过 |
| F8 | 实体渲染数据：模型实例、部件变换、prev/current 插值；实体音效 | UE 可见生物/动作；无 UE 侧 AI |
| F9 | 场景 MOB-01..08；性能（N 生物）；存档集成 | 场景通过；tick/GT 预算达标 |
| F10 | 场景 SUR-01..08（摔落/溺水/岩浆/生物伤害、饥饿、回血、死亡/重生、经验） | 场景通过；与 F1–F7 断言一致 |

**验收**：生存闭环；生物可见可战；死亡重生；存档一致。

### M3-G — 世界生成与维度

| 任务 | 内容 | DoD |
|---|---|---|
| G1 | 世界生成数据摄取：noise settings、biome source、surface rules、features、structures（server jar 内置 datapack/report） | Runtime 加载并校验；缺失明确失败 |
| G2 | 噪声/密度引擎 + 群系采样；固定种子确定性 | 同种子两次生成一致；语料统计通过 |
| G3 | 地表/洞穴最小子集/矿物/树木/花草特征放置；结构子集（尽力） | 场景与语料通过；不承诺全量 |
| G4 | 区块生成管线：后台 worker、结果合并、持久化、出生点选择、高度图；新世界创建入口（seed、level.dat、world_gen_settings、players/data、初始区块、Status/光照） | 新世界可用；生成不阻塞 TPS；原版可读 |
| G5 | 生成区块的光照/网格/实体/POI | 新区域可见/可交互/可保存 |
| G6 | 维度：主世界必做；下界加载 + 新世界下界最小生成 + 传送门最小闭环；末地加载已有区域 | 维度切换/保存场景通过；末地 Boss 后置 |
| G7 | 时间/天气：昼夜、天空光衰减、雨/雷计时、群系降雪（尽力）、保存 | 场景通过；环境快照驱动渲染 |
| G8 | 差分：固定种子区块样本、群系/表面/矿物/植被统计 vs vanilla；阈值在阶段 1 用 oracle 校准后冻结 | 报告通过；阈值未冻结不得宣称通过 |
| G9 | 性能：生成吞吐/chunks per sec、内存、worker 数 | 生成期间 TPS/GT 达标 |
| G10 | 场景 WLD-01..08（种子确定性、群系/表面/矿物/树、下界传送、昼夜/天气、新世界创建） | 场景通过；worldgen 差分可定位 |

**验收**：新种子新世界可生成/探索/保存；下界最小闭环；时间天气正确。

### M3-H — UI 与输入

| 任务 | 内容 | DoD |
|---|---|---|
| H1 | UE UI 画布：显示列表绘制（矩形/纹理/字形/裁剪矩形/缩放）；方块物品等距图标；Preview 提供最小同构画布用于回归断言 | UE 画面完整；Preview 可断言 UI 数据（不要求全量音频/资源包）；3D 物品图标可见 |
| H2 | HUD：生命/饥饿/经验/快捷栏/准星/手持/状态图标 | 场景截图与数据一致 |
| H3 | 界面：背包、工作台、熔炉、箱子、暂停/设置、死亡、加载；UE 完整可用，Preview 最小 | UE 全界面可用；键盘/指针语义正确 |
| H4 | 文本：语言文件、回退、物品名/提示、字体图集；CJK/中文字形作为显式范围决策 | 已支持语言可读；缺语言回退；CJK 若纳入则字形/许可证齐全，否则明确英文回退 |
| H5 | 输入语义：指针 hover/点击/拖拽/滚轮、Esc/焦点、鼠标捕获、键位；Host 只转发 | 场景通过；无 UI 逻辑在 Host |
| H6 | 命中测试/裁剪/缩放/DPI：Runtime 负责布局与命中；Host 只给坐标 | 多分辨率/缩放/滚动裁剪场景通过 |
| H7 | UI 场景（UI-01..06）与快照断言 | 场景通过；失败可定位 |
| H8 | 「Host 无 UI 逻辑」审计：UE 源码扫描 | 0 命中或只有绘制/转发 |

**验收**：完整 UI 可用；Preview/UE 同画面语义；无第二套背包/合成逻辑。

### M3-I — 音频与资源

| 任务 | 内容 | DoD |
|---|---|---|
| I1 | 声音注册/事件：id、类别、衰减、循环；资产索引 | Runtime 发出事件；Host 可映射 |
| I2 | UE 音频：OGG 解码/播放/混音/位置衰减；Preview 可选静音 | 方块/生物/UI 声音可听；无爆音 |
| I3 | 事件映射：破坏/放置/脚步/拾取/受伤/生物/环境/UI；音乐（尽力） | 场景音频事件序列通过 |
| I4 | 资源包：pack.mcmeta/格式 97.1、纹理/模型/声音/语言覆盖、字体覆盖、优先级、缺失回退 | 资源包场景通过；不崩溃；字体/CJK 覆盖按范围决策 |
| I5 | 数据包：配方/战利品/世界生成/标签覆盖；校验；错误明确 | 数据包场景通过 |
| I6 | 资产清单 v3/revision/hash；启动校验；热重载不做（重启） | 不匹配拒绝；覆盖生效 |
| I7 | 资源/音频场景（RES-01..04）；恶意包/ZIP slip/大小限制 | 场景通过；安全用例通过 |
| I8 | 资产/许可证审计：包内无 Mojang 资产；第三方清单 | 报告通过 |

**验收**：声音可用；资源包/数据包覆盖生效；缺失不崩溃；包内容合法。

### M3-J — 渲染 R1/R2

| 任务 | 内容 | DoD |
|---|---|---|
| J1 | Host 通用模型渲染接入：模型定义/网格上传、图集/材质、连接态与 cull 验证；不解析模型语义 | 首批方块全部正确；无洋红缺块；Host 只上传/绘制 |
| J2 | 平滑光照/AO、生物群系混合、水动画、树叶摆动（尽力） | 视觉场景通过；共享子集与 Preview 对照，R1 专属项只做 UE 基线 |
| J3 | Host 实体渲染接入：模型定义缓存、部件变换、prev/current 插值、手持物品/掉落物；实体模型由 Runtime/原生定义提供，不解析 Java 模型 | 生物/物品可见；动画连续；Host 无实体逻辑 |
| J4 | 方块实体渲染：箱子/熔炉/告示牌（首批） | 可见且随状态变化 |
| J5 | 粒子：类型/位置/颜色/纹理/寿命；天气粒子；实例化/公告板；粒子定义由 Runtime/原生表提供 | 破坏/爆炸/雨雪可见；预算达标 |
| J6 | 天空：昼夜/太阳/月亮/星星/云（尽力）、雾/天气 | 环境快照驱动；截图基线 |
| J7 | 第一人称手持/挥手/受伤/水下效果；视野/缩放 | 场景通过；无 UE 侧动画逻辑 |
| J8 | Render level 1/2/0 协商；R0 回退保留 | 三档可启动；能力不匹配明确失败 |
| J9 | 视觉回归：固定相机/场景/阈值；Preview/UE 只对照共享子集（方块/UI/相机/基础环境），实体/粒子/天气只做 UE 基线 | 报告 pass/fail；差异可解释；不要求 Preview 实现 R1/R2 |
| J10 | 「Host 无第二套渲染世界」审计 | Renderer 只消费 Contract |

**验收**：R1/R2 画面可辨认；视觉基线建立；性能达标；无双实现。

### M3-K — Converter 数据规模化

| 任务 | 内容 | DoD |
|---|---|---|
| K1 | 数据生成器编排：离线 JDK + server jar `--reports`；同时解包 server jar 内置 datapack 与 client.jar assets；缓存/校验/失败诊断 | 一条命令产出 reports/datapack/assets；不启动游戏进程 |
| K2 | 注册表/方块状态/物品/实体定义 v3：完整状态、属性、物品属性/标签、实体尺寸/属性/掉落引用；每项标注来源（report/内置 datapack/client assets/Java 代码） | 覆盖报告；稳定排序；来源矩阵完整 |
| K3 | 模型/blockstate 摄取：variants/multipart/权重、elements/rotation/uv/tint；client.jar assets 优先；输出模型定义 v3 | 首批方块模型完整；两次输出一致 |
| K4 | 配方/战利品/标签摄取与校验（内置 datapack）；确定性排序；不支持的战利品函数显式报告 | 场景所需配方/战利品齐全；hash 稳定 |
| K5 | 世界生成数据摄取：noise/biome/surface/feature/structure 定义（内置 datapack） | Runtime 可加载；缺失明确失败 |
| K6 | 资产：语言/声音/字体/群系色/图集（方块/物品/UI）；字体/CJK 资产与许可证作为显式决策 | 资产索引完整；hash 绑定 manifest；字体覆盖范围记录 |
| K7 | manifest v3：schema/ABI 3/profile/tool/input/generated hash/capabilities；UE Adapter v3 pin/hash 重绑；debug_inputs 分离 | 无绝对开发机路径；两次 extract 字节一致；Adapter 不匹配拒绝 |
| K8 | schema 校验/兼容：Runtime 拒绝不匹配；旧字段兼容策略 | 错误路径场景通过 |
| K9 | 数据覆盖报告：支持/不支持/近似清单；Java 代码来源的原生表/移植项单独列出；决策记录 | 报告可审计；不夸大；代码来源项可见 |

**验收**：client.jar + server jar 可重复产出全套数据；Runtime 加载；覆盖报告。

### M3-L — 分析和差分

| 任务 | 内容 | DoD |
|---|---|---|
| L1 | Java 分析器扩展：类/方法/字段清单、规范化字节码 hash、依赖图、反射/Unsafe/JNI 扫描 | 26.3 报告可复现；两次一致 |
| L2 | 数据/资源差分：方块/物品/配方/战利品/世界生成/资产 | 可生成 diff 报告与工作项 |
| L3 | port ledger：Java 符号 ↔ 原生模块/Patch/数据生成；覆盖状态 | 核心子系统有 ledger；可查询 |
| L4 | Patch 格式：版本化、可重放、三路合并、冲突策略、审计 | 同 Patch 重放一致；冲突明确失败 |
| L5 | 版本差异报告 → 工作项；错误定位到阶段/类/方法/Patch | 报告可执行；中间产物保留 |
| L6 | 可重复性：排序/命名/缓存；两次分析/差分一致 | 报告 hash 稳定 |
| L7 | 版本升级演练：真实第二版本或受控合成 delta；diff→分诊→生成/翻译/Patch→回归 | 演练记录；未执行写 not_run |
| L8 | 决策记录 `conversion-decisions/26.3.md` 更新 | 可审计；不写成已完成翻译 |

**验收**：分析/差分/Patch 闭环；演练证据；错误可定位。

### M3-M — 选择性翻译与 JRT 门禁

| 任务 | 内容 | DoD |
|---|---|---|
| M1 | 候选评审：从 DFU Codec/Brigadier/其他纯逻辑中选 0–2 个；标准=差分收益/维护成本/JRT 债务 | 门禁决议记录；默认允许「不翻译」 |
| M2 | 若通过：JRT 最小面（对象/字符串/集合/异常/线程子集）+ 测试 | JRT 单测通过；不扩张为 JVM |
| M3 | 若通过：一个子系统端到端翻译 + 差分；构建接入 | 差分通过；生成代码可复现 |
| M4 | 若否：记录证据与原生/生成替代方案 | 不阻塞其他工作包 |
| M5 | JRT 面统计/退出计划；无 Java 进程/无动态类加载 | 审计通过；错误模型清晰 |

**验收**：门禁决议明确；若做则一个子系统闭环；若不做则理由充分；不引入 JVM。

### M3-N — 测试、差分与性能

| 任务 | 内容 | DoD |
|---|---|---|
| N1 | 场景 DSL/runner：类别/数量/断言/报告；headless + UE | ≥62 条可跑；not_run 显式 |
| N2 | vanilla oracle：固定种子/时钟/输入；区块/存档/配方/战利品/光影 dump；明确 server oracle 为主，client 只在有离线 harness 时使用 | oracle 可复现；产物不入库；oracle 来源记录 |
| N3 | 存档往返矩阵自动化（含拆分数据文件/玩家/实体/POI） | 报告逐项 pass |
| N4 | 差分：世界样本、方块更新、光照、配方/战利品、实体 trace | 覆盖核心系统；失败可定位 |
| N5 | 视觉/音频回归：固定相机/场景/阈值/事件序列；Preview/UE 只对照共享子集 | 报告 pass/fail |
| N6 | 性能/soak：TPS/GT/RT/生成/保存/内存；30 分钟门禁 | 预算达标；无泄漏 |
| N7 | 错误矩阵/fuzz：损坏存档/NBT/区域/拆分数据/资源包/数据包/磁盘满/只读 | 明确错误；不崩溃 |
| N8 | 覆盖报告/通过率计算/CI 脚本；`not_run` 与豁免显式 | 可重复；未执行不写成通过 |

**验收**：套件 ≥95%；差分/存档/性能/错误报告齐全。

### M3-O — 打包与验收

| 任务 | 内容 | DoD |
|---|---|---|
| O1 | NativeBuild RC：v3 schema、版本/hash 绑定、可重复组合 | RC 可启动；不一致组合拒绝 |
| O2 | 干净机：UE 5.8.4 未安装；DX12/SM6；进入世界/游玩/退出 | 记录机器/截图/日志；失败不写成通过 |
| O3 | 依赖/许可证审计：dumpbin、包内容、无 Mojang 资产/无 JVM；明确 generated 资产在包外、干净机使用用户自有输入 | 报告通过；审计范围（Runtime 包 vs 用户输入）写清 |
| O4 | 文档：`M3_ACCEPTANCE.md`、COMPATIBILITY/ARCHITECTURE/DEVELOPMENT 更新 | 命令可复现；范围/限制如实 |
| O5 | G3 记录 + 用户试玩确认 | 逐条状态；豁免单独标注 |
| O6 | RC 检查清单/已知限制/后续缺口 | 可进入 M4 或明确阻塞 |

> N/O 的最终执行属于 **阶段 5B（最终验收）**，必须在阶段 1–4 全部完成后运行。

**验收**：G3 通过（或豁免记录）；RC + 干净机；用户确认。

---

## 6. 怎么分（核心）

### 6.1 切分原则

1. **先地基后玩法**：Contract v3、存档写入、方块状态/光照是所有玩法的共同依赖；先做玩法会导致反复重写。
2. **纵向可玩**：每一波结束必须有可运行演示；拒绝「数据全绿但游戏不可玩」。
3. **横向并行但不交叉写**：Gameplay/World/Presentation/Converter/QA 五轨并行；同一文件同一时间只有一个 owner。
4. **契约冻结窗口**：阶段 1 冻结 v3；后续只做 `struct_size` 增量 + capability，语义变化升 v4。
5. **数据驱动优先，但识别代码来源**：每个机制先问「能否由 client.jar/server 内置 datapack/reports 生成」，不能才原生；实体模型、粒子、方块行为/碰撞/光照等 Java 代码来源必须进入原生表/port ledger，不假装是数据产出。
6. **场景定义范围**：「完整」由 §1.6 场景套件定义，不由 26.3 parity 定义；新增功能必须新增场景。
7. **阶段按闭环切**：阶段内每个工作包都要完成实现 + 构建 + 场景 + 记录；不按目录/文件切。
8. **发现类修复独立**：验收发现的缺陷用 `Sx-a` 子会话修，不扩功能、不改契约（沿用 M2-S4 经验）。

### 6.2 为什么是 5 个执行阶段（原 10 个会话合并）

| 方案 | 问题 |
|---|---|
| 按模块切（先所有存档，再所有 UI…） | 每个模块都要等世界状态/契约，跨模块调试多，长期无可玩演示 |
| 按文件/目录切 | 交接成本高，同文件多人改冲突；无法验证闭环 |
| 一口气做完再验收 | 风险不可见，问题在最后才暴露；不符合门禁 |
| 先 Converter 全量再做游戏 | 违背纵向切片；Converter 没有 Runtime 需求会过度设计 |
| 先世界生成 | 世界生成不直接产生可玩价值，且风险最高；应放在交互/存档稳定后 |
| 只做玩法不碰 Converter | 违反 M3 验收第 3/7 条；数据会退化为手写表 |

**5 个执行阶段**（原 S1–S10 相邻两两合并）是当前建议的粒度：每个阶段包含两个原会话的工作包，阶段结束仍是一个可自检闭环；再拆会增加阶段间交接成本，再合并会失去验证点。阶段 3 是最大风险包，必要时可在阶段内拆成 3A（生存/实体）与 3B（世界生成/维度），不改变阶段编号与门禁。阶段 5B 是最终验收，必须在阶段 1–4 完成后运行。

### 6.3 依赖图

```text
阶段 1 地基与世界（原 S1+S2：A + B + C + N 骨架）
 ├─→ 阶段 2 交互与容器（原 S3+S4：D + E + H）
 │    └─→ 阶段 3 生存与世界（原 S5+S6：F + G + J 部分）
 │         └─→ 阶段 4A 表现层（原 S7：I + J 剩余）──────────┐
 ├─→ 阶段 4B 工具链（原 S8：K + L）                          │
 │    └─→ 阶段 5A 工具链深度/版本演练（原 S9：M + L7）───────┤
 └─→ 场景/oracle（N）随各阶段持续增长 ──────────────────────┴─→ 阶段 5B 最终验收（N + O）
```

**关键路径**：阶段 1 → 阶段 2 → 阶段 3 → 阶段 4A → 阶段 5B。阶段 4B 可在阶段 1 schema 草案后启动，与阶段 2–4A 并行；阶段 5A 依赖阶段 4B；阶段 5B 必须等阶段 1–4 与 5A 全部完成。阶段 3 内 F4（实体框架）先于 G5（实体/POI 初始化），其余 G 任务可与 F 并行。

### 6.4 并行轨道

| 轨道 | 内容 | 阶段 | 备注 |
|---|---|---|---|
| G Gameplay | 交互/物品/容器/生存/战斗 | 阶段 2–3 | 关键路径 |
| W World | 方块状态/光照/世界生成/维度/存档 | 阶段 1、3 | 与 G 共享世界状态，文件 owner 分离 |
| V Presentation | UI/渲染/音频/资源包 | 阶段 2–4 | 依赖 Contract v3 与 Runtime 数据 |
| C Converter | 数据/分析/差分/Patch/翻译门禁 | 阶段 4–5 | 不阻塞玩法；数据 schema 与 Runtime 同步 |
| Q Quality | 场景/oracle/差分/性能 | 阶段 1 起持续、阶段 5B 收口 | 每个阶段必须新增场景 |

**单人串行**：按阶段 1→2→3→4A→5A→5B 顺序执行；阶段 4B 可在阶段 1 后插入并行。

**两人/多执行者并行**：G 与 C 可分叉；阶段 3 内 F/G 可分叉；阶段 4A 可在阶段 3 的模型/实体契约稳定后分叉；阶段 5B 必须等阶段 1–4 与 5A 完成。约束：Contract 头文件、`generated` schema、`simulation` 核心文件同一时间只有一个 owner；跨轨改动先在阶段 1 契约层合入，再各自实现。

### 6.5 阶段表

> 估算为「AI 执行时长/个人开发量级」，不是承诺；不含用户等待/评审/干净机协调。

| 阶段 | 工作包 | 估算 | 开始条件 | 结束交付 |
|---|---|---|---|---|
| **阶段 1 地基与世界（原 S1+S2）** | M3-A + M3-B + M3-C + M3-N 骨架 | 42–73 h | M2 S4 完成；存档备份；server jar 可用 | v3 冻结；三 Host 同步；动作/UI/实体/音频空通道；可写存档；方块编辑/更新；光照引擎；方块实体首批；通用模型接入；存档往返矩阵；场景 5 条 |
| **阶段 2 交互与容器（原 S3+S4）** | M3-D + M3-E + M3-H | 40–70 h | 阶段 1 | 破坏/放置/使用/选取；物品表/背包/快捷栏；掉落/拾取；手持；HUD；2×2/3×3、工作台、熔炉、箱子；战利品表；完整背包/容器界面；物品循环闭环 |
| **阶段 3 生存与世界（原 S5+S6）** | M3-F + M3-G + M3-J 部分 | 52–90 h | 阶段 2 | 生命/饥饿/经验/伤害/死亡重生；8 种生物/AI/生成；战斗；实体渲染 R1；种子世界生成/持久化；新世界创建；主世界+下界最小闭环；时间/天气；环境渲染；worldgen 差分 |
| **阶段 4 表现与工具链（原 S7+S8）** | M3-I + M3-J 剩余 + M3-K + M3-L | 44–76 h | 4A：阶段 3 的模型/实体契约稳定；4B：阶段 1 schema 草案 | R2 粒子/天气/天空/水/手持；音频；资源包/数据包；视觉回归；M2 视觉遗留收口；数据生成器/schema v3/模型/配方/战利品/资产；分析/差分/port ledger/Patch；可重复 |
| **阶段 5 验收与版本（原 S9+S10）** | M3-M + M3-L7 + M3-N + M3-O | 28–52 h | 5A：阶段 4B；5B：阶段 1–4 与 5A 完成 | 翻译/JRT 门禁决议（可 no-go）；版本升级演练；场景 ≥95%；存档/差分/性能/错误报告；RC NativeBuild；干净机；G3；用户确认 |

**总计约 206–361 h**（个人开发量级；10→5 合并只减少阶段数和交接次数，不减少工作量；实际取决于数据面、世界生成和视觉迭代）。

### 6.6 每个阶段的开始提示

```text
读 docs/M3_PLAN.md，本次只做阶段 Sx：<工作包列表>；阶段 4/5 请注明子轨（4A 表现层 / 4B 工具链 / 5A 版本演练 / 5B 验收）。
先读 §6.7 交接状态、§7 Contract v3、§1.3 范围封顶。
允许构建/运行/场景/冒烟并自行修复；不要扩展到其他 Sx 的工作包。
每完成一个工作包就更新 §6.7，不要攒到最后。
M3 只在指定开发机执行 UE/Cook/打包/干净机；缺 UE 时停止 UE 工作并报告。
不启动 Java 游戏/JVM；JDK 只用于离线 Converter/oracle。
未实际运行的结果不要写成通过；新增功能必须新增场景。
UE 只消费 Contract；发现需要把背包/合成/世界/存档逻辑写进 UE 时，先停下来改契约。
```

### 6.7 交接状态（新对话从这里继续）

| 阶段 | 状态 | 已完成 | 已知问题 / 下一步 |
|---|---|---|---|
| 阶段 1 地基与世界 | 进行中（UE 已解阻；B/C 未开始） | A1–A4、A7–A9 已执行；A5/A6 在 Runtime/Preview/headless 完成；Contract v3 冻结（hash/决议见 `M3_ACCEPTANCE.md` §3.1/ARCHITECTURE §8.2.1）；A8 首批 5 场景全 pass；A7 只在副本上写、原版读取 `not_run`；阶段 5 已实测 UE R0 三 Host 同实档启动 | A5/A6 的 UE 画布/实体/音频仍未接入（R0 不含）；B/C 未开始；下一步 B1（完整 WorldWriter）与 C，再补 A5/A6 UE 侧；原版读取仍 `not_run` |
| 阶段 2 交互与容器 | 进行中（D/E/H 非 UE 部分完成） | D1–D5、D7–D9；E1–E7；H1–H8（Preview/headless）；INT-01..12、INV-01..12、UI-01..06 共 30 条 pass；单元测试 41 用例通过；详见 `M3_ACCEPTANCE.md` §3.2 | D6 手持仅 Contract 数据；D8/E4/E5 存档 not_run（B1）；E8 包覆盖 not_run（阶段 4B）；E9 oracle not_run；D1 实体选取/攻击已由 M3-F 补齐（阶段 2 当时 not_run）；UE UI 未实现（Host 仅 R0 世界网格）；配方/物品/战利品为原生子集待 M3-K4 |
| 阶段 3 生存与世界 | 进行中（F/G 的 Runtime+场景完成；存档/UE 项 `not_run`） | F1–F10 的 Runtime/场景部分；G1–G7、G9、G10 的 Runtime/场景部分（G8 `not_run`）：确定性主世界生成（噪声/群系/表面/洞穴/矿物/树/花草）、后台生成管线、新世界内存创建、主世界+下界最小闭环与传送门、昼夜/天气、环境快照；SUR-01..08、MOB-01..10、UI-07、WLD-01..08 共 27 条 pass；全套 62 条已实现场景 62 pass；`m1-tests` 52 用例 / 699 断言通过；J3 实体部件渲染与 J6 环境快照已接入 Runtime/Preview（截图 `build/m3/reports/visual/m3-s3-*.png`）；详见 `M3_ACCEPTANCE.md` §3.3/§3.4/§3.5 | 阶段 3 不能标记完成：B1 存档写未开始（新世界/实体/时间天气持久化 `not_run`）；UE R0 已运行；R1 实体/环境/音频 Host 未实现（`not_run`）；G1 数据摄取只有可选 profile 校验，server datapack 摄取归 M3-K5；G8 oracle 差分未运行；弓/箭、盔甲、结构/末地 not_run |
| 阶段 4 表现与工具链 | 进行中（UE 已解阻；Runtime/Preview 范围完成） | 4B-K K1–K9；4B-L L1–L6/L8；4A-I I1/I3–I8（RES-01..04 pass）；4A-J J5/J6/J7/J8/J9/J10：Runtime 粒子/天气、Preview 天空/太阳/月亮/星星/云/雨、第一人称手持 + FOV/水下效果、render_level 0/1/2 协商、视觉回归 3 场景 pass、Renderer 审计 0 命中；J2 3×3 biome tint 混合；详见 `M3_ACCEPTANCE.md` §3.6–§3.9 | UE R0 已运行（阶段 5 实测），但 J1/J3/J4、I2 的 Host 侧仍未实现，不能把阶段 4 标完成；J4 另依赖 C6 方块实体；J2 的 AO/水动画/树叶摆动未做；资源包模型重网格未做；L7 属 5A `not_run` |
| 阶段 5 验收与版本 | 进行中（5A 完成；5B 可执行项完成，门禁未通过） | 5A-M：翻译/JRT 门禁 **no-go**（候选评审、JRT face=0、`tools/m3-jrt-audit.py` 0 命中）；5A-L7：用户未批准合成 delta，**not_run**（无相邻第二版本）。5B：构建 + 52 用例/821 断言 pass；68 场景全 pass，但 62 口径 **56/62=90.3%，SAV 6 条 not_run**；UE Zen 解阻后 R0 Dev/Shipping 进入实档/截图/退出、`REIM.Bridge.*` 2/2 与 `REIM.R0.Smoke` pass、三 Host 同 DLL 状态一致（tick 61，20 字段）；Shipping RC + `m3-package.ps1` + assemble；30 分钟 Runtime soak pass（20.17 TPS、内存 48.97→48.95 MB）；错误矩阵 10/10 + adapter format/render_level 2 例；package audit/repro pass；审计 0 命中；`tools/m3-acceptance.ps1` 一键报告 **executed 21 / fail 1（scenario-gate 56/62）/ not_run 4**；详见 `M3_ACCEPTANCE.md` §3.10–§3.11 | 5B 未收口：SAV-01..06/存档往返（B1 未实现）；干净机（O2/F5 `not_run`）；UE 仅 R0（R1/R2/UI/实体/音频 Host 未实现）；vanilla oracle 差分/版本升级 `not_run`；**G3 FAIL**；下一步属其他 Sx（B1/B2…、C、UE UI/实体/音频），不在本会话扩张 |

**状态写法**：`未开始` → `进行中` → `完成`；阻塞写 `阻塞：<原因>`。只更新本表，不在此写长篇总结。

### 6.8 阶段边界（防止越界）

| 阶段 | 只允许 | 明确不做 |
|---|---|---|
| 阶段 1 | Contract v3、三 Host 同步、动作/UI/实体/音频空通道、存档写 spike 与完整存档、方块状态/行为/光照/方块实体、通用模型接入、场景骨架 | 完整玩法、worldgen、R1/R2、Converter 扩张 |
| 阶段 2 | 交互/物品/掉落/工具/手持、背包/容器/合成/熔炉/箱子/战利品、HUD/完整 UI | 生存/生物/战斗、worldgen、R2 |
| 阶段 3 | 生存/伤害/经验、实体/AI/生成/战斗、R1 实体、世界生成/维度/时间天气/环境渲染、新世界创建 | R2 粒子/天气/天空/水/手持、资源包、Converter 全量 |
| 阶段 4 | R2 粒子/天气/天空/水/手持、音频、资源包/数据包、视觉收口；数据规模化、分析/差分/Patch | 新玩法、worldgen 扩张、ABI 语义改动、翻译门禁/版本演练 |
| 阶段 5 | 翻译门禁/JRT（若通过）、版本升级演练、场景/差分/性能/RC/验收、G3 记录 | 新功能、重构、范围扩张 |

**交接原则**：Contract v3 头文件、schema v3、存档格式、线程/所有权在阶段 1 冻结；阶段 2 起只实现/修复。必须改时先记录原因，再同时改文档、头文件、Converter、Preview、headless、UE。

### 6.9 发现类修复（Sx-a）

验收或用户反馈发现的缺陷（如 M2 的 S4 贴图问题）按以下规则新增子会话：

- 命名 `Sx-a`（x 为阶段号 1–5），只修缺陷，不扩功能、不改 ABI 语义。
- 先保存 before 截图/场景，再做最小修复，after 证据入 `M3_ACCEPTANCE.md`。
- 修复后重跑受影响场景与 `m3-acceptance.ps1`；未重跑不得标记完成。
- 若根因在契约/数据 schema，按 §6.8 交接原则升级，不偷偷破坏冻结。

---

## 7. Rendering Contract v3

> 详细规则最终写入 `runtime/contract/*.h`；本节是目标形状。阶段 1 评审后冻结；字段名/布局以头文件为准。

### 7.1 版本与头文件

- `REIM_ABI_VERSION` 升到 **3**；Preview/headless/UE 同步重建，不提供 v2 服务。
- 头文件分工：
  - `reim_abi.h`：实例/控制/输入/动作/资产/保存/运行时信息。
  - `reim_render.h`：帧、网格、相机/玩家/环境、实体、粒子。
  - `reim_game.h`：物品栈、背包/容器、交互目标、屏幕状态。
  - `reim_ui.h`：UI 显示列表/绘制项/命中语义。
  - `reim_audio.h`：音频事件。
- **冻结规则**：v3 内只允许 `struct_size` 前缀扩展 + capability 位；字段语义/所有权/线程变化 → v4。
- 所有结构带 `struct_size`/最小尺寸宏/布局断言；跨边界只传 POD/句柄/不可变缓冲。

### 7.2 动作与输入（目标形状）

```c
typedef enum ReimActionType {
  REIM_ACTION_BREAK_BLOCK = 1,
  REIM_ACTION_PLACE_BLOCK = 2,
  REIM_ACTION_USE_BLOCK   = 3,
  REIM_ACTION_ATTACK      = 4,
  REIM_ACTION_USE_ITEM    = 5,
  REIM_ACTION_PICK_BLOCK  = 6,
  REIM_ACTION_SELECT_SLOT = 7,
  REIM_ACTION_SLOT_CLICK  = 8,   /* a=slot, b=button, c=mods */
  REIM_ACTION_DROP_ITEM   = 9,
  REIM_ACTION_CLOSE_SCREEN= 10,
  REIM_ACTION_RESPAWN     = 11,
  REIM_ACTION_POINTER     = 12,  /* UI 指针位置/按键/滚轮，命中由 Runtime 判定 */
  REIM_ACTION_BREAK_START = 13,  /* 按住左键；Runtime 维护挖掘进度 */
  REIM_ACTION_BREAK_STOP  = 14,  /* 松手；停止挖掘 */
  REIM_ACTION_USE_START   = 15,  /* 按住右键；吃东西/拉弓/持续使用 */
  REIM_ACTION_USE_STOP    = 16,
  REIM_ACTION_DRAG_START  = 17,  /* 从槽位开始拖拽 */
  REIM_ACTION_DRAG_TO     = 18,  /* 拖拽中指针/目标槽位 */
  REIM_ACTION_DRAG_END    = 19,
  REIM_ACTION_WHEEL       = 20   /* a=滚动增量（快捷栏/背包） */
} ReimActionType;

typedef struct ReimActionEvent {
  uint32_t struct_size;
  uint32_t type;
  uint64_t sequence;      /* Host 单调递增；Runtime 按序处理 */
  uint32_t screen_id;     /* 当前界面；0 = 无界面 */
  int32_t  a, b;          /* slot/button/mods，按 type 解释 */
  uint32_t c;             /* 按键位/修饰键/标志 */
  float    x, y, z;       /* 命中点/目标位置（世界坐标） */
  float    screen_x, screen_y; /* UI 指针，屏幕像素 */
  float    wheel;         /* 滚轮增量 */
} ReimActionEvent;
```

- `submit_action` 任意线程；事件在下一 tick 应用；队列有界（建议 1024），满则拒绝并记录，不阻塞模拟。
- `BREAK_START/STOP`、`USE_START/STOP` 必须成对；Runtime 维护挖掘/使用进度，Host 不模拟进度。重复 START 视为同一段按住。
- `POINTER` 每帧发送最新屏幕坐标、按键位与滚轮；队列只保留最新指针状态（hover 不因队列满而卡住）。
- `SLOT_CLICK`/`DRAG_*` 的 `a` 是**当前 `screen_id` 下的槽位编号**；不同界面不共享槽位命名空间。
- Runtime 校验：触及距离、目标存在、槽位合法、界面状态、冷却；非法动作记录并忽略。
- Host 不得绕过动作直接改世界；调试作弊（飞行/传送）只通过显式调试动作/配置，且在 Shipping 中禁用。

### 7.3 玩家/交互/环境快照

`ReimPlayerSnapshot` v3 追加：

- 属性：`health/max_health`、`food/saturation/exhaustion`、`air/max_air`、`xp_level/xp_progress/xp_total`、`gamemode`、`difficulty`、`dimension`。
- 状态：`dead`、`on_fire`、`in_water`、`sneaking`、`sprinting`、`flying`、`on_ground`、`using_item`、`use_ticks`。
- 手持/快捷栏：`selected_slot`、`held_item`（`ReimItemStack`）、`offhand_item`（可选）。
- 位置/朝向/eye height/AABB 保持 v2 语义；`prev_*` + `tick_alpha` 继续插值。

新增 `ReimInteractionSnapshot`：

- 目标类型（无/方块/实体）、方块坐标/状态 id、命中面/命中点、实体 id、触及距离、是否可挖掘、挖掘进度、攻击冷却。

`ReimEnvironmentSnapshot` v3 追加：

- `time_of_day`、`day`、`moon_phase`、`sky_darken`、`weather`（clear/rain/thunder）、`rain_level`、`thunder_level`、`dimension`、`sun_angle`、`ambient_light`。
- 天空/雾色/起止保留 v2 字段；渲染由 Host 消费。

### 7.4 实体/粒子/音频

```c
typedef struct ReimEntityRender {
  uint64_t entity_id;
  uint32_t type_id;
  uint32_t model_id;       /* 指向资产中的模型定义 */
  ReimVec3 position;       /* 当前 tick */
  ReimVec3 prev_position;  /* 上一 tick，Host 插值 */
  float yaw, pitch, prev_yaw, prev_pitch;
  float scale;
  float limb_swing, limb_swing_amount;
  float head_yaw, head_pitch;
  float hurt_time, death_time;
  uint32_t flags;          /* invisible/glowing/on_fire/... */
  ReimItemStack held_item;
  uint32_t part_count;
  const ReimPartTransform* parts; /* 部件变换，索引与模型定义一致 */
} ReimEntityRender;

typedef struct ReimParticle {
  uint32_t type;
  ReimVec3 position, prev_position, velocity;
  float size, age, lifetime;
  uint32_t color;
  uint16_t texture;
  uint16_t flags;
} ReimParticle;

typedef struct ReimAudioEvent {
  uint32_t sound_id;
  ReimVec3 position;
  float volume, pitch;
  uint32_t category;
  uint8_t loop;
  uint8_t reserved[3];
  uint64_t sequence;
} ReimAudioEvent;
```

- 模型定义（部件盒/枢轴/UV/纹理）通过资产索引交付；Host 缓存，不在每帧重复上传。
- 实体动画/部件变换由 Runtime 计算（含 prev/current）；Host 只插值/绘制。
- 粒子位置由 Runtime 模拟；Host 可插值绘制，但不改变玩法状态。
- 音频事件按帧聚合；Host 负责混音/距离衰减；事件不改变世界。

### 7.5 UI 显示列表

- Runtime 产出 `ReimUiFrame` + 绘制项列表（全部在帧 blob 内）：

```c
typedef enum ReimUiDrawKind {
  REIM_UI_RECT = 1,
  REIM_UI_TEXTURED_QUAD = 2,
  REIM_UI_GLYPH_QUAD = 3
} ReimUiDrawKind;

typedef struct ReimUiDraw {
  uint32_t kind;
  uint32_t flags;       /* blend/mask/... */
  float x, y, w, h;
  float u0, v0, u1, v1;
  float clip_x, clip_y, clip_w, clip_h; /* 裁剪矩形；w/h<=0 表示不裁剪 */
  uint32_t color;       /* RGBA8 tint */
  uint32_t texture;     /* 资产 id / 图集 id */
  uint32_t text_offset; /* 文本/字形数据在 blob 内的偏移 */
  uint32_t text_length;
} ReimUiDraw;
```

- `ReimUiFrame`：`screen_id`、`screen_revision`、`ui_scale`、`cursor_mode`、`cursor_x/cursor_y`、`draw_count`、`draws`、`text_bytes`、`inventory_snapshot`（槽位/物品，供 headless 断言）。
- **命中测试在 Runtime**：Host 发送指针坐标/按键/滚轮（`REIM_ACTION_POINTER` / 槽位/拖拽动作），Runtime 按当前界面判定；Host 不维护槽位/按钮映射。
- 文本由 Runtime 用字体图集生成字形四边形；Host 不做字体/layout。语言/回退在 Runtime；FONT_ATLAS 元数据必须含 baseline/ascent/descent 与字符映射。
- 物品图标由 Runtime 生成纹理四边形（物品图集）；方块物品的等距图标由 Converter 预渲染，Host 不解析物品模型。
- 裁剪矩形支持背包滚动/提示框；Host 只按 draw 列表与裁剪矩形绘制，不自行组合槽位。
- UI 帧按 tick 发布；Host 可在两 tick 间绘制光标（光标位置由 Runtime 的指针状态提供或在 Host 插值），不改变逻辑。

### 7.6 资产

- 新增 `REIM_ASSET_INDEX`（UTF-8 JSON：kind/id/hash/size/依赖），Host 启动时拉取并校验。
- `get_asset(instance, kind, asset_id, &data, &size, &revision)`：kind 扩展为：
  - `ATLAS_PNG/META`、`ITEM_ATLAS_PNG/META`、`UI_ATLAS_PNG/META`、`FONT_ATLAS/META`、`SOUND_OGG`、`LANG_JSON`、`MODEL_DEFS_JSON`。
- 资产在实例生命周期内只读；Host 不释放；revision 与 manifest hash 绑定。
- 资产不匹配/缺失 → 启动失败或明确回退（语言/声音可有回退；纹理/模型缺失不得静默假世界）。

### 7.7 保存/控制

```c
/* 请求保存；异步执行；返回是否已排队。 */
int32_t (REIM_CALL* request_save)(ReimInstance*, uint32_t flags);
/* 查询保存状态：idle/saving/last_result/last_saved_tick */
int32_t (REIM_CALL* get_save_status)(ReimInstance*, ReimSaveStatus*);
```

- `stop()` 必须先完成一次保存（或明确返回失败），再停止模拟；`destroy()` 释放未归还帧块。
- 保存失败（磁盘满/只读/锁冲突）必须可查询、可日志定位；不得静默继续。
- 暂停（`set_simulation_paused`）继续保留；暂停时可打开背包/容器（单机语义），保存仍可进行。
- 调试控制（时间/天气/传送/游戏模式）只通过显式调试动作或 Host 开发参数；Shipping 禁用。

### 7.8 帧块布局与上限

- 仍为单个 Host-owned blob；内部按 16 字节对齐分区：网格 updates/removes、顶点/索引、实体/部件、粒子、UI draws/text、音频事件、玩家/环境。
- v3 上限（可在阶段 1 校准，写死到头文件）：

| 列表 | 上限 | 超限行为 |
|---|---|---|
| 网格 update/remove | 4096 / 4096 | `REIM_ERR_LIMIT`，保留 pending |
| blob 字节 | 64 MB（现有） | `REIM_ERR_LIMIT` |
| 实体渲染 | 8192 | `REIM_ERR_LIMIT`（玩法关键，不静默丢） |
| 实体部件变换总数 | 1,048,576 | `REIM_ERR_LIMIT`；防单实体部件爆炸 |
| 粒子 | 65536 | 丢弃最旧并计数（表现项，可丢） |
| UI draw | 8192 | `REIM_ERR_LIMIT`（界面关键） |
| UI 文本 | 1 MB | `REIM_ERR_LIMIT` |
| 音频事件 | 4096 | 保留关键类别，丢弃最旧环境音并计数 |

- 每帧 `frame_id` 单调；`snapshot_generation`/`full_snapshot` 语义保留；新增列表随全量帧一起重建。
- `REIM_ERR_BUSY` 语义不变；Host 复用上一帧，首帧保持加载页。

### 7.9 线程/所有权（写入头文件）

- 模拟线程：世界/物品/背包/实体/AI/UI 状态/命中测试。
- 生成/网格/保存 worker：不触碰 UObject；结果按确定顺序合并。
- `fetch_frame` GT 单线程；`release_frame` 任意线程；`submit_action/input` 任意线程。
- 音频事件从模拟线程发布到 Host；Host 音频线程播放，不回调 Runtime。
- Host `alloc/free` 线程安全；不得回调 Runtime/抛异常。

### 7.10 能力与校验

- `ReimRuntimeInfo` v3：profile/build/ABI/manifest hash/capabilities/render_level/schema version。
- 新增 capability：`REIM_CAP_R1 0x10`、`REIM_CAP_R2 0x20`、`REIM_CAP_UI 0x40`、`REIM_CAP_AUDIO 0x80`、`REIM_CAP_SAVE 0x100`、`REIM_CAP_ENTITIES 0x200`。
- `render_level` 0/1/2；不支持 → `REIM_ERR_CAPABILITY`。
- UE 启动校验：NativeVersion/Adapter manifest v3、DLL hash、UE 5.8.4、ABI 3、profile、schema；不匹配明确失败。
- Preview/headless 只校验 Contract/manifest/ABI/profile，不依赖 UE Adapter。

---

## 8. Runtime 游戏系统设计

> 本节只写 M3 必须落地的语义与边界；具体数据结构在实现时定稿。所有系统都遵循：Runtime 权威、数据驱动优先、Host 只消费。

### 8.1 世界与方块状态

- **状态注册表**：`name + properties → state_id`；section 调色板；`set_block` 维护状态、邻居更新、脏标记、光照、保存脏页。
- **Chunk/Section**：方块状态数组、光照数组、高度图、方块实体列表、实体列表、biome 调色板；加载/卸载/持久化。
- **确定性 tick 顺序**：区块坐标排序、方块实体/实体按 id/UUID 排序；禁止依赖哈希容器迭代顺序。
- **方块实体**：箱子/熔炉/告示牌首批；拥有独立 tick、NBT、渲染数据。

### 8.2 方块行为与更新

- **数据标志**：solid/collision AABB/liquid/gravity/replaceable/plant support/flame/face culling/light emission+opacity/tool tier/needs support；由 Converter/report、Java 代码分析或原生表提供，每项标注来源，Runtime 消费。
- **原生行为首批**：重力（沙/砂砾）、液体（水/岩浆简化流动）、植物支撑/破坏、作物/树苗生长、雪/冰融化、火把附着、门/活板门开合、箱子/熔炉交互。
- **更新规则**：放置/破坏触发邻居更新；状态属性（朝向、半砖、连接）由放置上下文计算；无红石更新。
- **计划 tick/随机 tick**：每 tick 限额；作物/树苗/雪/冰；液体流动按 tick 预算。
- **不做**：红石信号/活塞/命令方块/方块实体复杂逻辑（告示牌编辑可选）。

### 8.3 光照

- **天空光**：高度图 + 15 级列衰减 + 水平扩散；**方块光**：光源 BFS；跨 section 边界；增量更新。
- **遮蔽规则**：不透明方块 15，树叶/水按原版透明度子集；首批近似必须记录。
- **时间影响**：天空光乘天空暗化系数（夜晚/天气）；方块光独立。
- **保存/加载**：读写 section 的 `SkyLight`/`BlockLight` 长数组；生成区块初始化。
- **差分**：固定语料的 light 数组/亮度采样 vs vanilla；允许近似项必须在验收记录列出。
- **渲染**：顶点 light 仍是 (sky<<8|block)；Host 不计算光照。

### 8.4 存档写入

- **布局**：26.3 使用 `dimensions/<namespace>/<dimension>/region/r.x.z.mca`；不得假设顶层 `region/`。维度元数据在 `dimensions/<dim>/data/minecraft/*.dat`。
- **容器**：Anvil `.mca`（32×32 区块、8 KiB 头、4 KiB sector）；压缩读 gzip/zlib/none/LZ4，写优先 zlib（LZ4 写可选）；`.mcc` 超大区块读/写（至少读，写出策略单列）。
- **写路径**：读-modify-写单个区块；sector 分配/回收；临时文件 + FlushFileBuffers + ReplaceFile/MoveFileEx；备份轮转；`session.lock`；多文件保存 journal/marker 规定 `level.dat`、`data/minecraft/*.dat`、`players/data` 与 region 的提交/恢复顺序。
- **数据范围**：区块方块/光照/群系/高度图/方块实体/实体/POI（实体与 POI 的实际存放位置以 26.3 实档审计为准）；`level.dat`（spawn/time/version）；`data/minecraft/weather.dat`、`world_clocks.dat`、`world_gen_settings.dat`、`game_rules.dat`；`players/data/<uuid>.dat`；`dimensions/<dim>/data/minecraft/*.dat`（chunk_tickets/raids/world_border）。
- **区块字段**：`Status=full`、`isLightOn`、heightmaps、blending/structures/ticket 字段必须维护；新生成区块写完后原版可直接读取。
- **规则**：DataVersion 5023 可写；旧存档先用 26.3 server `--forceUpgrade` 离线升级再导入；高于当前拒绝读写；未知 NBT 原样保留；未知 item components 也必须原样保留（不能只存 hash）；自动保存间隔可配；退出保存必须完成或明确失败。
- **验收**：实档审计 + 往返矩阵 + 原版读取 + 损坏/磁盘满/只读/多文件中断错误路径。

### 8.5 物品、背包、容器、合成、战利品

- **物品表**：id/name/max_stack/max_damage/tool class+tier/food/use action/block placement/fuel/attributes；数据生成，Runtime 只读。
- **物品栈**：item_id、count、damage、components（M3 只做最小字段；未知组件必须保留**原始 NBT 字节/引用**，不能只存 hash，否则无法回写）。
- **背包模型**：9 快捷栏 + 27 主背包；可选 4 盔甲 + 副手（尽力）；槽位点击/Shift 点击/拖拽；游标栈；拒绝非法/复制。
- **容器**：箱子/木桶/熔炉/工作台；方块实体持有槽位；打开状态、保存、界面数据。
- **合成**：有序/无序配方、标签、2×2/3×3；匹配确定性；配方覆盖来自数据包；不做配方书。
- **熔炉**：输入/燃料/输出/燃烧时间/熔炼进度；保存；界面进度。
- **战利品**：方块/实体/箱子；条件/函数子集（数量、概率、附魔映射到物品属性子集）；确定性 RNG；与 oracle 对照。
- **不做**：附魔/酿造/铁砧/村民交易；潜影盒/末影箱。

### 8.6 生存与战斗

- **属性**：生命/最大生命、饥饿/饱和/消耗、氧气、经验等级/进度/总量；回血/饿死/溺水。
- **伤害**：摔落/岩浆/火/窒息/生物/虚空；无敌帧；基础护甲减伤（盔甲槽实现时）；伤害方向反馈。
- **死亡/重生**：死亡界面、掉落规则（可配置 keepInventory 调试）、重生点、保存。
- **战斗**：攻击冷却、伤害、击退、暴击（基础）；生物攻击；弓/箭（尽力）；盾牌/状态效果后置。
- **玩家存档**：位置/朝向/生命/饥饿/经验/背包/游戏模式；与原版字段兼容。

### 8.7 实体、AI、生成

- **实体框架**：实体 id/UUID、类型数据（尺寸/属性/类别）、物理（重力/碰撞/液体）、追踪、保存/加载；未知实体保留 NBT。
- **模型/粒子来源**：实体模型/动画和粒子定义是 Java 代码，不是 JSON；8 种生物与首批粒子必须原生移植/建表，并在 port ledger 记录，Converter 只负责能提取的部分。
- **首批生物**：僵尸、骷髅、苦力怕、蜘蛛、牛、猪、羊、鸡；每种有生成规则、掉落、声音、动画参数。
- **AI**：目标选择（游荡/看向/攻击/逃跑/跟随）、A* 寻路子集、卡墙恢复、白天燃烧（僵尸/骷髅）、苦力怕引爆（基础）。
- **生成**：光照/群系/高度/生物群系规则；生成上限/消失；世界生成与已加载区块。
- **投射物**：箭（尽力）；物品/经验球；TNT 后置。
- **保存**：实体/POI 的实际存放位置以 26.3 实档审计为准（区块 NBT 或 `dimensions/.../data`）；UUID 稳定；不丢未知字段。
- **不做**：繁殖/驯服/村民/Boss/末影龙/凋灵；复杂群体 AI。

### 8.8 世界生成与维度

- **数据驱动**：noise settings、biome source、surface rules、features、structure sets 从 server jar 内置 datapack/report 摄取；Runtime 解析/校验。
- **原生引擎**：噪声/密度、群系采样、表面构建、洞穴最小子集、矿物/树木/花草特征；固定种子确定。
- **管线**：生成 worker、结果按坐标顺序合并、光照/网格/实体/POI 初始化、写盘；出生点选择；新世界创建入口（seed、level.dat、world_gen_settings、players/data、初始区块、Status/光照）。
- **维度**：主世界必做；下界加载 + 新世界下界最小生成 + 传送门最小闭环；末地只加载已有区域（生成/Boss 后置）。
- **差分**：固定种子区块样本、群系/表面/矿物/植被统计；不承诺逐方块一致，但阈值必须在阶段 1 用 oracle 校准并冻结，报告差异。
- **性能**：生成吞吐/内存/worker 数；生成期间 TPS 稳定。

### 8.9 时间与天气

- 昼夜 24000 tick；天空暗化/太阳角度；月相（尽力）；雨/雷计时与群系降雪（尽力）；保存。
- 环境快照驱动渲染；天气不改变方块逻辑（除雪层/火等行为子集）。
- 调试时间/天气只通过开发参数/动作；Shipping 禁用。

### 8.10 输入与 UI 语义

- 键鼠映射：移动/跳跃/潜行/冲刺/攻击/使用/放置/丢弃/快捷栏/背包/暂停/截图（Host 调试）。
- 指针：UI 打开时捕获/释放；hover/点击/拖拽/滚轮；Runtime 命中测试；`screen_id` 决定槽位命名空间。
- 按住语义：挖掘/使用由 START/STOP 成对事件驱动；Host 不模拟进度。
- 界面状态机：无界面/背包/工作台/熔炉/箱子/暂停/死亡/加载；Esc 逐级关闭；不允许非法动作。
- UI 缩放/DPI：Host 提供画布尺寸/缩放；Runtime 布局与裁剪；Preview/UE 同结果。

### 8.11 资源包与数据包

- **资源包**：格式 97.1；pack.mcmeta、assets 覆盖（纹理/模型/声音/语言/字体）；包优先级/合并；缺失回退；shader 覆盖忽略并警告；CJK 字体与许可证作为显式范围决策。
- **数据包**：配方/战利品/世界生成/标签；覆盖/合并；校验；错误明确；函数/命令不做。
- **安全**：ZIP slip/路径穿越/解压炸弹/大小/深度/符号链接；不可信输入。
- **缓存**：启动加载/索引；不做热重载（重启生效）。

### 8.12 M3 内容范围（再次强调）

- §1.3 的首批清单是「场景必须覆盖」的最低集合；数据层可加载更多，但未实现的方块/物品/生物必须有明确回退（不渲染/不使用/报错），不得静默伪造成可用。
- 覆盖报告由 Converter 产出（支持/不支持/近似），并随验收记录；不得把「数据里有」当成「玩法已支持」。

---

## 9. Converter M3

### 9.1 数据生成器（reports）

- 离线运行（JDK 25 + 用户自有 26.3 server jar）：
  ```bat
  java -DbundlerMainClass=net.minecraft.data.Main -jar server.jar --reports --output work\26.3\reports
  ```
- 产物：registries、packets、blocks/items/commands 报告；由 `reim-convert` 摄取并校验 hash/版本。
- 同时解包 server jar 内置 datapack（`data/minecraft/**`：recipes/loot/tags/worldgen/structures）与 client.jar assets（`assets/minecraft/**`：models/blockstates/lang/sounds/font/colormap）。`--reports` 只补充注册表/方块/物品等，**不是全部数据来源**。
- 不启动游戏客户端/服务端；数据生成器是离线 JVM 工具，不是游戏进程；不进入 Runtime；产物不入库。

### 9.2 Schema v3

- `manifest.json`：schema=reim.generated.manifest、format=3、ABI 3、profile、tool version、input hashes、generated hashes、capabilities、atlas/asset 索引 hash；无绝对开发机路径。
- 新增/扩展生成物：
  - `blocks.json`：全状态、属性、行为标志、模型引用、tint、方块实体类型。
  - `items.json`：物品属性、堆叠、耐久、工具、食物、燃料、方块放置、模型引用、标签。
  - `models.json`：blockstate variants/multipart、elements、rotation、uv、cullface、textures、权重。
  - `recipes.json`、`loot_tables.json`、`tags.json`、`worldgen.json`、`biomes.json`、`sounds.json`、`lang/`、`font/`、`item_atlas`/`ui_atlas` 元数据。

**数据来源矩阵**

| 数据项 | 首选来源 | 备注 |
|---|---|---|
| 方块/物品注册表、状态、物品属性/标签 | `--reports` + 内置 datapack | reports 覆盖不全时以 jar 数据补 |
| recipes/loot tables/tags/worldgen/structures | server jar 内置 datapack | 不是 `--reports` 的产物 |
| models/blockstates/lang/sounds/font/colormap | client.jar assets | 资源包可覆盖 |
| 实体模型/动画、粒子定义 | Java 代码 | 必须原生移植/建表；Converter 只能记录/生成部分 |
| 方块行为、碰撞 AABB、光照/透明度、工具等级/燃料、AI/生成规则 | Java 代码 + 少量 report/tag | 进入原生表/port ledger，不得声称是 reports 产出 |
| 方块物品等距图标 | Converter/原生预渲染 | UI 不能直接用平面贴图 |
| CJK/字体 | client.jar font/unifont 或额外字体 | 许可证/字形覆盖需显式决策 |

- 规则：稳定排序、无时间戳、canonical JSON；两次 extract 字节一致；`debug_inputs.json` 本地诊断、不参与 hash、不打包。
- Runtime/UE 只接受 schema v3 + ABI 3 + profile；不匹配明确失败。

### 9.3 模型系统 v3

- 从 `blockstates` + `models` 完整摄取：variants、multipart、加权随机、`x/y` 旋转、`uvlock`、`cullface`、`tintindex`、多层 element。
- Runtime 通用网格器按「状态 + 邻居上下文」生成面（栅栏/墙/玻璃板/门连接）；不再逐方块特例。
- 物品模型：generated/handheld/block parent；物品图集/方块模型引用；UI 图标由 Runtime 生成四边形。
- 实体模型：数据定义部件（盒/枢轴/纹理/父子），Runtime 计算变换；**26.3 实体模型/动画是 Java 代码，不是 JSON**——8 种生物与首批粒子必须原生移植/建表（Converter 能做代码分析但不能假装数据提取），不生成 UE 资产。
- 覆盖报告：已支持模型类型/未支持（如特殊渲染器方块）；未支持必须明确回退，不静默。

### 9.4 分析与差分

- Java 分析器：类/方法/字段清单、规范化字节码 hash、依赖图、反射/Unsafe/JNI/ClassLoader 扫描；两次运行一致。
- 数据 diff：方块/物品/配方/战利品/世界生成/资产/语言；按子系统输出工作项。
- port ledger：Java 符号 ↔ 原生模块/Patch/数据生成的映射与覆盖状态；核心系统必须可查询。
- 差分入口：`reim-convert analyze/diff/ledger`；报告写入 `build/m3/reports`。

### 9.5 Patch 体系

- 格式：版本化 Patch 集（`converter/patches/26.3/`）；条目含目标文件/符号/差异/原因/验证用例。
- 能力：重放、三路合并、冲突明确失败、审计；生成代码不手改。
- 版本升级：diff → 分诊 → 生成/翻译/Patch → 构建 → 差分回归；Patch 冲突进入人工决策，不静默覆盖。
- 不散落 `#if MC_VERSION`；版本差异集中在 profile/数据/Patch。

### 9.6 选择性翻译与 JRT 门禁

- **默认策略**：数据生成 + 原生实现优先；翻译不是 M3 的默认路径。
- **候选**：DFU Codec/DynamicOps（若数据驱动加载成为瓶颈）、Brigadier（仅当命令纳入范围；M3 不做命令则排除）。
- **门禁标准**：差分收益、维护成本、JRT 债务、性能、调试性；任一明显不划算则 no-go。
- **若通过**：JRT 最小面（对象/字符串/集合/异常/线程子集），一个子系统端到端 + 差分 + 构建；JRT 面统计/退出计划。
- **若 no-go**：记录证据与替代方案；不阻塞其他工作包。
- **红线**：不做运行期 class loading/字节码解释器/完整 JVM；不引入 Java 进程；不让 JRT 进入数据/原生/替换分支。

### 9.7 版本升级演练

- 首选：用户提供第二个 26.3 相邻版本输入，跑真实 diff→Patch/生成→回归。
- 若无：经用户批准，使用 **受控合成 delta**（对 26.3 数据/符号做已知变更）演练流水线机制；报告标注「合成演练，不等同真实版本升级」。
- 证据：diff 报告、工作项、Patch 重放、构建 hash、差分回归结果、未解决项。
- 未执行则写 `not_run`，不得写成通过。

### 9.8 可重复性与诊断

- 固定遍历/排序/命名；无时间戳/绝对路径进入 hash；Converter 输出与 NativeVersion/NativeBuild 组合两次 byte-identical；DLL/UE 二进制用 canonical hash（必要时 `/Brepro`），不承诺跨机字节一致。
- 失败保留中间产物与日志；错误定位到阶段/类/方法/Patch/构建步骤。
- 决策记录：`conversion-decisions/26.3.md` 记录数据路线、模型覆盖、翻译门禁、未支持项；不夸大「已翻译」。

---

## 10. Host 侧渲染、UI、音频

### 10.1 R1/R2 范围

| 层 | 内容 |
|---|---|
| R1 | 通用方块模型（多层/旋转/多部件/连接）、实体（部件模型/插值）、方块实体（箱子/熔炉/告示牌）、基础天空/时间、生物群系混合、平滑光照/AO |
| R2 | 粒子、天气（雨/雪/雷）、云/太阳/月亮/星星（尽力）、水动画、第一人称手持/挥手、受伤/水下效果、完整 HUD/界面、物品图标 |
| 不做 | Lumen/Nanite/VSM/RT/PBR、后处理高级、OIT、阴影级联、DLSS/FSR |

- R0 保留为调试回退（`render_level=0`）；R1/R2 由 capability/render_level 协商。
- 所有渲染数据来自 Contract；UE 不解析方块模型/实体模型/动画/背包。

### 10.2 Host 渲染器结构

- **网格**：沿用单世界 SceneProxy；section VB/IB；通用模型网格由 Runtime 产出（或按模型定义在 Runtime 网格化后上传；Host 不解析模型语义）。M3 推荐 Runtime 网格化，Host 只上传，保持单一实现。
- **实体/粒子**：模型定义资产 + 实例数据；Host 缓存模型、插值、实例化/批处理；透明/排序与区块一致。
- **UI**：全屏 Slate/Canvas 画布绘制 Runtime 显示列表；字形/物品图标都是纹理四边形（方块物品为预渲染等距图标）；Host 只负责 DPI/缩放/裁剪与光标绘制，不自行组合槽位。
- **音频**：Host 音频组件消费事件；OGG 解码/播放/混音/衰减；无音频资产时静音并记录。
- **颜色/曝光**：统一 tonemap/曝光设置；与 Preview 对照校准（M2 tonemap 偏暗项）；固定截图配置。

### 10.3 性能预算（M3 校准）

| 指标 | 目标 | 说明 |
|---|---|---|
| 模拟 | 20 TPS ±0.2；tick p99 ≤ 25 ms | RD 8、40 生物、100 掉落物、生成活动 |
| 世界生成 | 生成不使 tick p99 超预算；吞吐记录 | 后台 worker；固定种子 |
| GT Bridge+UI | fetch/合并/提交 p99 < 3 ms | 含实体/粒子/UI |
| RT 上传 | 稳态 < 4 ms；首载单独记录 | 网格/实体/粒子 |
| 渲染 | 1080p、RD 8–12、60 FPS 目标 | 记录 GPU/驱动/画质 |
| 保存 | 主线程暂停 < 50 ms；保存期间 TPS 稳定 | 快照后后台写 |
| 内存/显存 | 30 分钟无持续增长 | 实体/粒子/生成缓存有界 |
| 音频 | 无 underrun/爆音 | 事件队列有界 |

### 10.4 「Host 无游戏逻辑」审计

- UE 源码不得出现：NBT/Anvil/区块网格/方块表/物理/背包/合成/配方/战利品/光照/AI/worldgen 实现。
- 允许：Contract 消费、坐标转换、插值、GPU 上传、UI 绘制、音频播放、错误对话框。
- 审计脚本扫描关键字 + 人工评审；报告入验收。

---

## 11. 测试、差分与验收

### 11.1 场景 DSL

```json
{
  "id": "INV-03",
  "world": "fixture/m3-basic",
  "seed": 12345,
  "gamemode": "survival",
  "setup": [{ "action": "give", "item": "minecraft:oak_log", "count": 8 }],
  "steps": [
    { "tick": 1, "action": "slot_click", "slot": 36, "button": 0 },
    { "tick": 2, "action": "assert_screen", "screen": "inventory" }
  ],
  "assert": [
    { "type": "inventory_count", "item": "minecraft:oak_planks", "min": 4 },
    { "type": "block_at", "x": 16, "y": 65, "z": 80, "state": "minecraft:air" }
  ]
}
```

- Runner：headless 优先；UE 只跑 UI/视觉/音频/打包场景。
- 世界 fixture：固定种子/时间/天气/背包；不依赖用户存档（回归用 fixture；真实存档另跑）；生成脚本与 hash 放 `tests/fixtures/` 并入库。
- 输入动作覆盖 `BREAK_START/STOP`、`USE_START/STOP`、`POINTER`、`DRAG_*`、`WHEEL`；槽位断言必须带 `screen_id`。
- 输出：`build/m3/reports/scenarios/<id>.json` + 截图/日志；失败含期望/实际。

### 11.2 vanilla oracle

- 离线 JDK + 用户自有 server jar；固定种子/时钟/输入；**以 server oracle 为主**；client oracle 只在有离线 harness 时使用，不假设存在 headless client；导出：
  - 区块样本（方块/光照/群系/高度图）；
  - 存档往返（原版写/Native 写）；
  - 配方/熔炼/战利品抽取；
  - 方块更新/作物生长 trace；
  - 实体微场景状态（不是逐 tick AI trace）。
- oracle 产物只在本机/CI 受控目录；不提交、不打包、不分发。
- 对照规则：数值/状态优先；无法逐位一致的项（AI/结构）按场景断言 + 差异说明。

### 11.3 存档往返矩阵

| 用例 | 步骤 | 通过标准 |
|---|---|---|
| SAV-01 | Native 读真实存档（26.3 `dimensions/`）→ 改方块/背包 → 写 → 原版读 | 原版无损坏；改动生效；拆分数据文件一致 |
| SAV-02 | 原版写 → Native 读 → Native 写 → 原版读 | 语义一致；未知字段保留；level.dat/data/players 同步 |
| SAV-03 | 未知 NBT/实体/方块实体/物品组件 | 写回后仍在；原始 NBT 保留，不丢 |
| SAV-04 | 压缩变体 gzip/zlib/none/LZ4/.mcc | 可读；写回可被原版读；`.mcc` 写出策略明确 |
| SAV-05 | 自动保存/退出保存/崩溃恢复/多文件中断 | 不损坏；可恢复；journal 生效 |
| SAV-06 | 只读/磁盘满/锁冲突/无玩家/无下界区域 | 明确错误或明确跳过；原文件不变 |

### 11.4 差分与视觉/音频回归

- 世界：固定种子区块样本、群系/表面/矿物/植被统计；阈值在阶段 1 用 oracle 校准并冻结，报告差异。
- 方块/光照：编辑 trace、light 数组采样。
- 配方/战利品：输出/抽取与 oracle；不支持函数显式报告。
- 实体：微场景状态/掉落/生成数量。
- 视觉：固定相机/场景/时间/天气；Preview/UE 只对照共享子集；实体/粒子/天气只做 UE 基线；无洋红缺块；UI 截图。
- 音频：事件序列/类别/触发条件；不做波形逐位对比。

### 11.5 性能与稳定性

- 30 分钟门禁（用户可另定更长）：TPS/GT/RT/生成/保存/内存/显存采样；无崩溃/无持续增长。
- 场景批量执行时间、生成吞吐、保存耗时；报告 JSON + 表格。
- 先测后优化；超预算需 profile 证据，不靠降视距掩盖。

### 11.6 错误矩阵与 fuzz

- 缺 DLL/NativeVersion/manifest/adapter/schema/资产/世界/存档；ABI/profile/render_level 不匹配。
- 损坏 NBT/区域/level.dat/data/minecraft/players/data/实体；LZ4 损坏；ZIP slip/解压炸弹；资源包/数据包非法。
- 磁盘满/只读/锁冲突；保存中断；GPU device lost；音频设备缺失。
- 期望：明确错误码/日志/受控退出；不崩溃、不静默假世界。

### 11.7 验收命令与报告

```bat
:: 构建（m3-debug/m3-release preset 由阶段 1 A3 增加）
cmake --preset m3-release
cmake --build --preset m3-release --parallel 8
powershell -ExecutionPolicy Bypass -File tools\m3-build.ps1 -Config Release

:: 新世界创建（无 Launcher；headless/开发入口）
build\m3\bin\reim-headless.exe --create-world --world <new-save> --seed <seed>

:: 数据/工具链
build\m3\bin\reim-convert.exe data --profile 26.3 --server-jar <server.jar> --out generated\26.3
build\m3\bin\reim-convert.exe analyze --profile 26.3 --out work\26.3
build\m3\bin\reim-convert.exe diff --old <p1> --new <p2> --out work\diff
build\m3\bin\reim-convert.exe package --profile 26.3 --generated generated\26.3 ^
  --dll build\m3\bin\mc_26.3.dll --ue-adapter build\m3\adapter\26.3 --out build\native-version\26.3
build\m3\bin\reim-convert.exe assemble --native-version build\native-version\26.3 ^
  --runtime-package build\m3\ue-shipping-package\Windows --out build\native-build\26.3

:: 场景/存档/差分/性能
build\m3\bin\reim-scenario.exe --suite tests\scenarios --out build\m3\reports\scenarios
powershell -ExecutionPolicy Bypass -File tools\m3-save-roundtrip.ps1 -World <save-copy>
powershell -ExecutionPolicy Bypass -File tools\m3-diff.ps1 -Oracle build\m3\oracle -Out build\m3\reports
powershell -ExecutionPolicy Bypass -File tools\m3-perf.ps1 -Seconds 1800

:: 一键验收
powershell -ExecutionPolicy Bypass -File tools\m3-acceptance.ps1 ^
  -World <save-copy> -Generated generated\26.3 -UeSeconds 60
```

- 报告：`m3-acceptance.json`、`scenarios/*`、`save-roundtrip.json`、`diff/*`、`perf/*`、`visual/*`、`errors/*`。
- 未执行项写 `not_run`；不得把开发机结果写成干净机通过。

---

## 12. 风险与缓解

| 风险 | 级别 | 影响 | 缓解 |
|---|---|---|---|
| **范围爆炸**（玩法+Converter+渲染同时展开） | 极高 | 长期不可交付 | §1.3 封顶、场景定义、波次/门禁；尽力项不阻塞 |
| 存档写损坏真实世界 | 极高 | 用户数据丢失/信任崩塌 | 验收前强制备份；实档审计；原子写/备份/锁/journal；往返矩阵；fuzz；先 spike 再全量 |
| 26.3 实档布局/拆分数据被误判 | 极高 | 写错路径/丢实体/原版不可读 | 阶段 1 先做 `dimensions/`、`data/minecraft`、`players/data`、实体/POI 审计并固化 fixture |
| 世界生成 parity 不可达 | 高 | 大量返工/不可宣称 | 数据驱动 + 语料统计；不承诺 P3 全量；洞穴最小子集、结构尽力 |
| 数据来源误判（把 Java 代码当 reports） | 高 | 阶段 4B 低估/生成器缺失 | §9.2 来源矩阵 + port ledger；代码来源项单列 |
| 生物 AI/战斗与原版差异 | 高 | 体验不符 | 有限生物集；微场景/状态断言；不承诺逐 tick 一致 |
| Contract v3 过早冻结/反复改 | 高 | 三 Host + Converter 返工 | 阶段 1 spike 后冻结；`struct_size` 增量；语义变更升 v4；改契约走交接规则 |
| UI 显示列表方案不可行/性能差 | 中高 | UI 返工 | 阶段 1 UI spike（文本/图标/点击）；备选：语义控件树（需记录理由）；先测绘制量 |
| 网格/光照/世界生成阻塞 tick | 高 | TPS 崩塌 | 后台 worker + 确定性合并；预算；先测再优化；必要时降生成吞吐 |
| 数据生成器/服务端 jar 缺失 | 中高 | Converter 数据不完整 | §3.3 阻塞处理；不手写假表；报告缺口 |
| 翻译/JRT 变成无底洞 | 高 | 吞噬 M3 工期 | 默认不翻译；门禁 + no-go；JRT 最小面/退出计划 |
| 版本升级无第二版本 | 中 | 验收第 5 条无法证明 | 真实版本优先；受控合成 delta；如实标注 |
| 资源包/数据包安全/兼容 | 中 | 崩溃/被恶意输入利用 | 子集 + 校验 + 大小/深度/路径限制；fuzz |
| UE UI/音频/渲染实现扩散为第二套逻辑 | 高 | 双实现/返工 | Contract 边界；显示列表/事件；审计脚本；评审清单 |
| M2 遗留（F5/H6/视觉）被遗忘 | 中 | 带病验收 | §3.4 清单 + 阶段 1 建项 + 阶段 5B 收口 |
| 法律/资产分发 | 极高 | 无法发布 | 包内无 Mojang 资产；oracle 仅本地；M4 前评审 |
| 并行轨道文件冲突 | 中 | 合并成本/回归 | 单 owner 规则；契约层先合；场景回归 |

---

## 13. 新对话启动说明

**先读**

1. `README.md`（产品边界）；
2. `docs/ROADMAP.md` §1–§2、§5（M3 目标/验收/G3）；
3. `docs/ARCHITECTURE.md` §5（Native 化）、§6–§8（Runtime/渲染/ABI）；
4. `docs/M1_ACCEPTANCE.md`、`docs/M2_ACCEPTANCE.md`（已接受事实、遗留、已知限制）；
5. `docs/COMPATIBILITY.md`（存档/资源包/兼容矩阵）；
6. 本文（尤其 §1.3、§6、§7）。

**硬规则**

- 不启动 Java Edition/JVM；JDK 只用于离线 Converter/oracle。
- 不使用假世界/假区块/预生成截图；验收必须用真实 26.3 输入或公开 fixture。
- 不在 UE 中实现第二套世界/物理/方块/网格/存档/背包/合成/AI/UI 逻辑。
- 不手改 `generated/`；不提交 Mojang 文件/世界/生成物/Shipping 包/oracle 产物。
- 写存档前必须备份；不降级写；未知 NBT 保留。
- Contract v3 冻结后只增量；语义变化升 v4；改契约必须同步文档/头文件/Converter/Preview/headless/UE。
- 未实际运行的结果不写成通过；`not_run` 必须显式。
- UE/Cook/打包/干净机只在指定开发机执行；缺 UE 时停止 UE 工作。
- DX12/SM6 唯一目标；不引入 DX11 回退。
- 新增功能必须新增场景；通过率按当时套件计算。

**第一步**

1. 确认 M2 遗留清单（F5/H6/视觉 backlog）、**26.3 实档审计**、存档备份、server jar、干净机、第二版本/合成 delta、磁盘与 CJK 决策；
2. 读 §6.7 交接状态；确认当前阶段编号与工作包（阶段 4/5 注明 4A/4B、5A/5B 子轨）；
3. 从阶段 1 开始：Contract v3 + 存档写 spike + 场景骨架；
4. 每完成一个工作包更新 §6.7，不跨区间抢做。

**结束条件**

- 本会话工作包 DoD 满足；构建 + 场景 + 冒烟通过；
- 更新 §6.7 交接状态；
- 留下：已完成、未完成、已知问题、下个会话第一条命令/文件；
- 不把未执行测试写成通过。

---

## 14. 待用户决定/输入汇总

| # | 项 | 影响 | 默认 |
|---|---|---|---|
| 1 | §1.3 范围档位是否接受 | 决定 M3 广度 | 按必做档；尽力项按余量 |
| 2 | 世界生成目标（语料级 vs 结构/洞穴完整） | 阶段 3 工作量/验收口径 | 语料级；洞穴最小子集必做，结构后置 |
| 3 | 版本升级演练输入（真实第二版本/合成 delta） | M3 验收第 5 条 | 优先真实；否则合成并标注 |
| 4 | soak 时长 | 稳定性门禁 | 30 分钟；长时另跑 |
| 5 | H6 视觉问题逐项清单 | 阶段 4 视觉收口 | 未提供前按 §3.4 清单 |
| 6 | 干净机 | M3-O 阻塞项 | 必须提供或显式豁免 |
| 7 | 首批内容清单是否调整 | 场景/数据覆盖 | 按 §1.3 |
| 8 | 创造/旁观模式是否纳入 | 场景/UI/飞行 | M3 只做生存；创造仅调试配置 |
| 9 | 下界是否要求新世界最小生成 | 传送闭环可行性 | 要求最小生成；完整化后置 |
| 10 | CJK/中文 UI 是否要求 | 字体资产/许可证/UX | 未定；未纳入则明确英文回退 |
| 11 | 磁盘/测试数据准备 | UE Cook/DDC/备份/oracle | D 盘 206 GB 偏紧；需清理或扩容 |
| 12 | M3 测试存档（玩家/下界/压缩变体） | B3/G6/SAV | 需另建 fixture；现有 server 存档无玩家数据 |

---

> 本文是执行计划，不是完成声明。M3 是否完成，只看 `docs/M3_ACCEPTANCE.md` 的实测记录与 G3 门禁；任何未运行的项必须标 `not_run` 或「豁免」，不得写成通过。
