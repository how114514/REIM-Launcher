# REIM M3 Acceptance Record

> 只记录**实际执行过**的检查。未执行的项明确写 `not_run` 或「豁免」，不得视为通过。
> M3 为 **5 个执行阶段（S1–S5）**：原 10 个会话按相邻依赖两两合并；最终验收与版本升级演练在阶段 5，其中验收（5B）必须在阶段 1–4 完成后执行。
> 本文件记录 **M3 阶段 1–5** 的实测结果，不是完成声明。A 工作包见 §3.1；阶段 2 D/E/H 的非 UE 部分见 §3.2；阶段 3 F/G 的非存档、非 UE 部分见 §3.3/§3.4；阶段 4 的 4B-K/4B-L/4A-I/4A-J 见 §3.6–§3.9；阶段 5A/5B 见 §3.10/§3.11。**阶段 4 不能标记完成**：UE Zen 已在阶段 5 解阻并实测 R0，但 I2/J1/J3/J4 的 Host 侧（音频/通用模型/实体/方块实体）仍未实现；B/C 尚未开始。

## 1. 状态

| 项 | 当前 |
|---|---|
| M3 阶段 | 阶段 5 进行中：5A 完成（M 门禁 no-go；L7 `not_run`）；5B 已执行 Runtime/Preview/UE-R0 验收项（§3.11），未收口项见 G3 |
| 阶段 | S1–S5（5 个阶段；原 10 个会话两两合并） |
| G3 | **FAIL**：SAV/存档往返、oracle 差分、干净机、版本升级均为 `not_run`；场景 62 口径 56/62（90.3%），低于 95% |
| 场景套件 | **68 条已实现场景全部 pass**（5 A + 12 INT + 12 INV + 8 SUR + 10 MOB + 8 WLD + 7 UI + 4 RES + 2 R2）；但 §1.6 的 62 条类别口径缺少 SAV-01..06，故 56/62 = 90.3%，`not_run` 6 条 > 3，门禁不通过（`build/m3/reports/scenario-accounting.json`） |
| M2 遗留 | F5 干净机 `not_run`；H6 清单未提供；UE Zen 阻塞已解除（R0 可运行）；tonemap/不透明双面仍无 UE 视觉判定；加载页/键位自动化未做 |

**阶段映射**（与 `M3_PLAN.md` §0.4 一致）

| 阶段 | 原会话 | 工作包 |
|---|---|---|
| S1 地基与世界 | 原 S1+S2 | A + B + C + N 骨架 |
| S2 交互与容器 | 原 S3+S4 | D + E + H |
| S3 生存与世界 | 原 S5+S6 | F + G + J 部分 |
| S4 表现与工具链 | 原 S7+S8 | I + J 剩余 + K + L |
| S5 验收与版本 | 原 S9+S10 | M + L7 + N + O |

## 2. 开发环境实测（2026-11-21）

| 项 | 实测值 | 状态 |
|---|---|---|
| Unreal Engine | 5.8.4 Patch 4，`D:\Program Files\Epic Games\UE_5.8` | 编译可用；**R0 运行可用**（阶段 5 启动 Zen server 后实测，Dev/Shipping 均进入实档；R1/R2/UI/音频未实现，见 §3.10.1/§3.11） |
| MSVC | 14.44.35207（VS 2022 BuildTools） | 可用 |
| Windows SDK | 10.0.26100.0 | 可用 |
| CMake | 4.4.4 | 可用；M3 preset 待阶段 1 增加 |
| Python | 3.12.10 | 可用 |
| JDK | Temurin 25.0.5 | 可用（仅离线） |
| Git LFS | 3.5.1 | 可用 |
| GPU | RTX 4060，DX12/SM6 | 可用 |
| CPU / RAM | i5-12490F 6C/12T / 32 GB | 可用 |
| 26.3 client jar | `D:\Minecraft\.minecraft\versions\26.3\26.3.jar` | 可用 |
| 26.3 server jar | `D:\Minecraft\reim-work\26.3\server.jar`（version.json: 26.3 / 5023 / 777 / Java 25） | 可用 |
| 真实存档 | `D:\Minecraft\.minecraft\saves\REIM-M1-26.3` | 可用（server 存档，无玩家数据） |
| 干净机 | 未提供 | **阻塞阶段 5B/O2** |
| 第二版本输入 | 未提供（仅 26.3、1.21.1、1.20.1、ATM10） | **阻塞版本升级演练** |
| D 盘余量 | 206 GB | 偏紧；需清理/扩容 |

## 3. 26.3 实档审计（阶段 1 A1）

**执行**：2026-11-21，`REIM-M3-26.3`（只读）与备份副本。

- 备份：`D:\Minecraft\reim-work\m3\backup\REIM-M3-26.3-orig`（25 个文件，逐文件 SHA-256 见 `build/m3/reports/save-backup-sha256.txt`）。
- 工作副本：`D:\Minecraft\reim-work\m3\worlds\REIM-M3-26.3-work`（所有写入测试只用副本）。
- 可重复审计：`python tools/m3-save-audit.py --world <save> --out <json> [--chunks]`（离线，不启动 Java）。
- 报告：`build/m3/reports/save-audit.json`（含每区块语义 hash）、`save-audit-m3.json`（Native 解析器交叉验证）。

实测布局与结论：

```text
level.dat                              gzip NBT，根 {Data}；DataVersion 5023，Version 26.3，
                                       spawn {pos: IntArray[16,65,80], yaw/pitch, dimension}，
                                       Time/GameType/difficulty_settings/DataPacks/initialized 等
level.dat_old                          同上（上一版）
data/minecraft/*.dat (9 个)            每个根 {DataVersion, data}：weather/world_clocks/
                                       world_gen_settings/game_rules/random_sequences/
                                       scheduled_events/scoreboard/stopwatches/custom_boss_events
players/data/                          空（本档无玩家数据；B3 需另建 fixture）
dimensions/minecraft/overworld/region  4 个 .mca：r.-1.-1 / r.-1.0 / r.0.-1 / r.0.0
dimensions/minecraft/overworld/data    chunk_tickets / raids / world_border
dimensions/minecraft/the_nether        region 目录不存在（0 区块）；data 有 chunk_tickets/raids/world_border
dimensions/minecraft/the_end           region 目录不存在；data 含 ender_dragon_fight
```

区块统计（overworld，529 区块，0 解析错误）：

- 压缩：**529/529 为 zlib（type 2）**；无 LZ4、无 `.mcc` 外部区块。LZ4 读取已接入并用合成块测试通过；LZ4 写入与 `.mcc` 写策略仍需 B1 用合成 fixture 覆盖。
- `Status`：`structure_starts` 480、`biomes` 24、`terrain` 16、`initialize_light` 8、`full` **1**（区块 (1,5)）。
- 实体：存放在区块 NBT 的 `entities` 列表（528 个区块有该字段，**全部为空**）；无顶层实体 region。
- POI：本档无 `poi/` 目录、无 POI region；B4 的位置需另建 fixture，当前记 `not_run`。
- 方块实体：存放在区块 `block_entities` 列表（529 个区块有该字段，**全部为空**）。
- `structures`、`Heightmaps`、`PostProcessing`、`block_ticks`、`fluid_ticks` 存在于全部区块；`isLightOn`/`SkyLight`/`BlockLight` 只在已 light 的少数区块出现（本档 → C4 照明对照需合成 fixture）。
- 该档只有 1 个 full 区块，**不足以**做完整光照/worldgen 对照；A7/B 的写入 spike 在其副本上做，C 的对比用固定种子 fixture。

保留待办（不阻塞 A，进入 B/C 前必须关闭）：

- [ ] `players/data/<uuid>.dat` 的 26.3 字段与文件名规则（B3；需含玩家 fixture）
- [ ] POI 的实际存放位置（B4；本档无 POI）
- [ ] weather/world_clocks/world_gen_settings/game_rules 的写归属与原版读取顺序（B2）
- [ ] 区块 `isLightOn`/heightmaps/blending/structures/ticket 维护（B1/C4）
- [ ] `.mcc` 与 LZ4 写入策略（B1，合成 fixture）
- [ ] 多文件保存 journal/崩溃恢复语义（B5）
- [ ] 原版读取验证：本次未启动任何 Java 游戏进程；A7 只做 NBT/区域语义往返，原版读取标 `not_run`，待允许离线 oracle 时补。

## 3.1 阶段 1 A 工作包执行记录（2026-11-21）

| 任务 | 结果 | 证据 / 说明 |
|---|---|---|
| A1 实档审计 | **完成** | §3；`tools/m3-save-audit.py`；备份 + 25 文件哈希 |
| A2 Contract v3 | **完成** | 5 个头文件，编译期布局断言；SHA-256 见 `build/m3/reports/contract-v3-sha256.txt`；`ARCHITECTURE.md` §8.2.1 |
| A3 Host 同步 | **部分** | Preview/headless 启动并消费 v3（headless 报告 112 UI draws/1 entity/1 audio）；UE Bridge 与 Adapter pin 升 v3 并编译成功；**UE 运行 not_run**：启动即在引擎初始化前被 Zen storage server 阻塞，`build/m3/reports/visual/m3-a3-ue-editor3-stdout.txt` |
| A4 动作通道 | **完成（超距除外）** | 队列/序号/界面/满队列/START-STOP 成对/重复 START 合并；`tests/test_m3_a.cpp` case 1；超距校验依赖 D1，phase 1 未实现，未宣称 |
| A5 UI spike | **部分** | Runtime 显示列表 + 命中 + 裁剪 + 字形 + 物品图标占位；Preview 截图与点击状态切换通过；headless/场景断言通过；**UE 画布未接入**（not_run） |
| A6 实体/粒子/音频通道 | **部分** | 一个盒模型 + Runtime 部件旋转 + 声音事件；Preview 截图可见盒模型并调用 Beep/记录事件；headless/场景断言通过；**UE 渲染/音频未接入**（not_run） |
| A7 存档写 spike | **完成（原版读取 not_run）** | 独立副本上 grass→stone；写后重读为 stone；529 区块中仅 (1,5) 语义 hash 变化；level.dat/weather 语义不变；原子替换；未启动 Java 游戏进程 |
| A8 场景骨架 | **完成（首批 5 条）** | `reim-scenario.exe` + `tests/scenarios/m3a/`；5 pass / 0 fail / 0 not_run |
| A9 冻结评审 | **完成** | v3 头文件 hash、增量规则、线程/上限、ABI 2 数据兼容占位记录在 §8.2.1 |

**A 验收未满足项（不得写成通过）**

- 三 Host 同一实档启动：UE 运行 `not_run`（Zen storage server）；Preview/headless 已通过。
- 一个方块写入后“原版可读”：未运行原版（用户禁止启动 Java 游戏进程）；只证明 NBT/区域语义往返与未知字段保留。
- A5/A6 的 UE 画面/音频：`not_run`；Preview/headless 已完成。
- 因此阶段 1 不能标记完成；B（完整存档写入）与 C（世界状态/光照）未开始。

**场景运行命令**

```bat
set REIM_TEST_WORLD=<save-copy>
build\m3\bin\reim-scenario.exe --suite tests\scenarios\m3a ^
  --out build\m3\reports\scenarios --generated generated\26.3 ^
  --dll build\m3\bin\mc_26.3.dll
```

**A7 运行命令（只在副本上）**

```bat
set REIM_TEST_ALLOW_WRITE=1
set REIM_TEST_WRITE_WORLD=<disposable-copy>
build\m3\bin\m1-tests.exe "M3-A7*"
```

## 3.2 阶段 2 D/E/H 执行记录（2026-11-21，非 UE 部分）

**构建**：`cmake --build --preset m3-release --parallel 8` 通过；`m1-tests.exe` 41 个用例 / 2204 断言全部通过（含新增 `M3-D*`、`M3-E*` 单元测试）。场景使用真实 26.3 存档副本 `D:\Minecraft\reim-work\m3\worlds\REIM-M3-26.3-work`，全部改动只在 Runtime 内存中，未写存档。

| 工作包 | 结果 | 证据 / 说明 |
|---|---|---|
| D1 选取 | **完成（方块）** | `Gameplay::raycast` DDA、4.5 格、命中面/点；发布 `ReimInteractionSnapshot`。实体选取 not_run（M3-F 无实体） |
| D2 动作执行 | **完成** | 破坏/放置/使用/攻击；BREAK_START/STOP 按住挖掘与松手取消（INT-11/12）；朝向/轴/台阶状态；越界目标、非法槽位、非法界面拒绝并计数 |
| D3 物品表 v3 | **完成（原生子集）** | 1327 项：方块物品由 `blocks.json` 生成；工具/食物/锭/燃料为原生表（Java 代码来源，见 §9.2 来源矩阵）；`items.json` 摄取待 M3-K4 |
| D4 背包 | **完成（实例内存）** | 9 快捷栏 + 27 主背包、堆叠/拆分/交换、Shift/右键/拖拽、光标栈；保存恢复 not_run |
| D5 掉落物 | **完成** | 生成/重力/合并/延迟拾取/消退；`gameplay.drops` 与 counters 可断言 |
| D6 手持/动画数据 | **部分** | 玩家快照发布 `held_item`/`selected_slot`；Preview HUD 显示物品名；第一人称 UE 渲染 not_run |
| D7 方块掉落/工具 | **完成（M3 子集）** | 石头/矿石/泥土/树叶/砂砾等掉落；工具等级/速度/耐久；精准采集/时运 not_run |
| D8 INT 场景 | **完成（12 条）** | `tests/scenarios/m3d/INT-01..12`，12 pass（含按住挖掘与松手取消）；存档集成 not_run（B1） |
| D9 选取方块/快捷栏 | **完成** | PICK_BLOCK / SELECT_SLOT / WHEEL / 数字键；INT-07/08 |
| E1 槽位事务 | **完成** | 左/右/Shift/拖拽/光标、输出槽只出不进、非法操作拒绝；INV-02/03/04/10/11 |
| E2 背包界面模型 | **完成** | 2×2 合成格 + 快捷栏 + 主背包；screen_id 命名空间；INV-01/02 |
| E3 工作台 3×3 | **完成（原生子集）** | 有序/无序、木板/原木/煤炭标签、工具/台阶/箱子/熔炉配方；137 条；数据包覆盖 not_run（阶段 4B） |
| E4 熔炉 | **完成（实例内存）** | 输入/燃料/输出、燃烧/熔炼进度、界面；41 条熔炼；保存 not_run |
| E5 箱子 | **完成（实例内存）** | 27 槽、界面、关闭重开保持；双箱 not_run；保存 not_run |
| E6 战利品 | **完成（原生子集）** | 方块战利品表、确定性 RNG、工具等级门槛；实体/箱子战利品待 M3-F/K4 |
| E7 容器 UI | **完成** | 槽位/提示/文本/点击/关闭；INV-05/07/08/09 |
| E8 INV 场景 | **完成（12 条）** | `tests/scenarios/m3e/INV-01..12`，12 pass；资源包/数据包覆盖 not_run |
| E9 差分 | **not_run** | 未运行 vanilla oracle；配方/熔炼/战利品以单元测试与场景断言替代，oracle 差分留待阶段 5B |
| H1 UE UI 画布 | **Preview 完成；UE not_run** | `ui/game_ui.cpp` 显示列表；Preview 截图 `build/m3/reports/visual/m3-s2-hud.png`、`m3-s2-inventory.png`；UE 仍被 Zen storage server 阻塞（§3.1 A3） |
| H2 HUD | **完成（Preview/数据）** | 准星/快捷栏/生命/饥饿/经验/手持名；生存伤害数值仍归 M3-F |
| H3 界面 | **完成（Preview/数据）** | 背包、工作台、熔炉、箱子、暂停；死亡界面 not_run（M3-F）；加载页/UE 截图 not_run |
| H4 文本 | **完成（英文）** | 5×7 字体图集、物品名/提示、英文回退；CJK 未纳入（§6 决策），明确英文回退 |
| H5 输入语义 | **完成** | E/Q/Esc/1–9 由 Runtime 解释；Preview 只转发键/指针/滚轮；鼠标左/右 → START/STOP，模态界面 → POINTER，滚轮 → WHEEL |
| H6 命中/裁剪/缩放 | **完成** | 640×360 虚拟布局居中、ui_scale、slot_at 与绘制共用布局、tooltip/count 裁剪；UI-05 1920×1080 scale 3 |
| H7 UI 场景 | **完成（6 条）** | `tests/scenarios/m3h/UI-01..06`，6 pass |
| H8 Host 审计 | **完成** | `python tools/m3-ui-audit.py`：`runtime/host/Source` 0 命中；报告 `build/m3/reports/ui-audit.json` |

**运行命令（实测）**

```bat
cmake --build --preset m3-release --parallel 8
build\m3\bin\m1-tests.exe
set REIM_TEST_WORLD=D:\Minecraft\reim-work\m3\worlds\REIM-M3-26.3-work
build\m3\bin\reim-scenario.exe --suite tests\scenarios\m3d --out build\m3\reports\scenarios-m3d
build\m3\bin\reim-scenario.exe --suite tests\scenarios\m3e --out build\m3\reports\scenarios-m3e
build\m3\bin\reim-scenario.exe --suite tests\scenarios\m3h --out build\m3\reports\scenarios-m3h
python tools\m3-ui-audit.py --root runtime\host\Source --json build\m3\reports\ui-audit.json
```

**阶段 2 明确 not_run（不得视为通过）**

- 存档集成（D8/E4/E5）：`request_save` 仍返回 `REIM_ERR_NO_WORLD`，M3-B1 `WorldWriter` 未实现；所有容器/背包状态仅存在于实例内存。
- UE UI/手持/截图（H1–H3、D6）：UE 运行时仍被 Zen storage server 阻塞；只在 Preview 验证。
- E8 资源包/数据包覆盖、E9 oracle 差分：归阶段 4B/5B。
- 实体选取与攻击（D1/D2）：无实体系统，归 M3-F。
- 数据驱动 `items.json/recipes.json/loot_tables.json`：Converter 尚未产出（M3-K4），阶段 2 使用原生子集并在状态 JSON 暴露配方数量。
- 创造模式/末地/下界等：不属阶段 2。

**范围变更记录**：见 §7（阶段 2 原生数据子集、Contract 增量键码、仅内存状态）。

## 3.3 阶段 3 F 工作包执行记录（2026-11-21，非存档/非 UE 部分）

**构建**：`cmake --build --preset m3-release --parallel 8` 通过；`m1-tests.exe` **48 个用例 / 590 断言**全部通过（新增 `M3-F*` 7 个用例）。场景使用真实 26.3 存档副本 `D:\Minecraft\reim-work\m3\worlds\REIM-M3-26.3-work`；F 场景只改 Runtime 内存，未写存档。

| 任务 | 结果 | 证据 / 说明 |
|---|---|---|
| F1 玩家属性 | **完成** | `game/survival.{h,cpp}`：生命/饥饿/饱和/消耗/氧气/经验等级与进度；回血/饿死；`SUR-05/06/08` |
| F2 伤害来源 | **完成（护甲 not_run）** | 摔落/溺水/岩浆/火/窒息/生物/虚空/爆炸/饥饿；10 tick 无敌帧；`SUR-01..04`；护甲槽未实现（§1.3 尽力项） |
| F3 死亡/重生 | **完成（存档 not_run）** | 死亡界面筛选、背包掉落、重生点重置；`RESPAWN` 动作；`SUR-07`、`UI-07`；死亡状态仅内存 |
| F4 实体框架 | **完成（存档 not_run）** | id/type/物理/追踪/死亡动画/despawn；实体在 Runtime 模拟线程；存档往返依赖 B1 |
| F5 首批 8 生物 | **完成（自然生成近似）** | 僵尸/骷髅/苦力怕/蜘蛛/牛/猪/羊/鸡；生成/消失/日光燃烧/苦力怕爆炸；`MOB-01/03/04/07/10`；无 C4 光照引擎，自然生成用时间+地表暴露近似 |
| F6 AI | **完成（A* 子集）** | 游荡/看向/攻击/逃跑+3D 网格 A*（1 格跳跃、800 节点上限）；`MOB-02/08` |
| F7 战斗 | **完成（弓/箭 not_run）** | 攻击冷却/伤害/暴击/击退/生物攻击；`MOB-05/06`；弓/箭未实现（尽力项） |
| F8 实体渲染数据 | **完成（UE not_run）** | 8 个原生模型定义（`MODEL_DEFS_JSON` ids 1..8）+ 每实体部件变换/prev；Preview 盒体可见；UE 渲染/音频受 Zen 阻塞未运行 |
| F9 场景/性能 | **完成（场景 19 条）** | `SUR-01..08`、`MOB-01..10`、`UI-07`；40 生物实测 20.09 TPS（`MOB-09`）；存档集成 not_run |
| F10 SUR 场景 | **完成（8 条）** | 摔落/溺水/岩浆/生物伤害/饥饿/回血/死亡重生/经验 |

**运行命令（实测）**

```bat
cmake --build --preset m3-release --parallel 8
build\m3\bin\m1-tests.exe
set REIM_TEST_WORLD=D:\Minecraft\reim-work\m3\worlds\REIM-M3-26.3-work
build\m3\bin\reim-scenario.exe --suite tests\scenarios\m3f --out build\m3\reports\scenarios-m3f ^
  --generated generated\26.3 --dll build\m3\bin\mc_26.3.dll
```

结果：`m3f` 19 pass / 0 fail / 0 not_run；阶段 2 的 m3a/m3d/m3e/m3h 复跑 35 pass / 0 fail，无回归。

**阶段 3 F 明确 not_run（不得视为通过）**

- 存档集成（F3/F4/F9）：`request_save` 仍返回 `REIM_ERR_NO_WORLD`；实体/玩家/死亡状态只在 Runtime 内存（M3-B1 未开始）。
- UE 实体渲染/音频/第一人称（F8、J3、D6）：UE 运行时仍被 Zen storage server 阻塞；只在 Preview/headless 验证。
- 弓/箭（F7 尽力）、盔甲减伤（F2 尽力）、经验球物理（以 XP 直接入账替代）、自然生成的光照精确规则（无 C4 光照引擎）。
- 原版 oracle 差分（F9）：未运行，留待阶段 5B。

**范围变更记录**：见 §7（原生实体模型/掉落表、光照近似、仅内存状态）。

## 3.4 阶段 3 G 工作包执行记录（2026-11-21，非存档/非 UE 部分）

**构建**：`cmake --build --preset m3-release --parallel 8` 通过；`m1-tests.exe` **52 个用例 / 699 断言**全部通过（新增 `M3-G*` 4 个世界生成用例）。WLD 场景使用 `--world build\m3\worlds\m3g --create-world` 的内存世界；**未写任何存档**。

| 任务 | 结果 | 证据 / 说明 |
|---|---|---|
| G1 世界生成数据摄取 | **部分** | Runtime 读取可选 `generated/26.3/worldgen/profile.json` 并校验（非法即失败，单测覆盖）；当前文件不存在，`worldgen_source=native`。server jar 内置 datapack 摄取属 M3-K5（阶段 4B），未运行 |
| G2 噪声/密度/群系 | **完成（原生 profile）** | 固定种子确定性：同种子两次 fingerprint 一致、不同种子不同（`M3-G*` 单测）；`WLD-02` |
| G3 表面/洞穴/矿物/树木/花草 | **完成（语料级近似）** | 主世界海洋/沙滩/平原/森林/沙漠/针叶林；洞穴 3D 噪声雕刻；煤/铁/铜/金/红石/青金石/钻石/绿宝石深度带；橡木/云杉/白桦树与花草；`WLD-03/04/05`；结构/洞穴完整化 not_run |
| G4 生成管线/新世界 | **完成（内存；持久化 not_run）** | `Simulation` 生成 worker（离线生成、主线程合并、排序）、`--create-world --seed`、出生点选择、区块流式生成；`WLD-01/08`；level.dat/world_gen_settings/players/heightmaps/写入未做（B1） |
| G5 生成区块的网格/实体 | **完成（网格/实体；光照/POI not_run）** | 生成区块直接合成、1 次加载 169 区块、1200 段网格；自然生成开启时可生成生物（`MOB-10`）；C4 光照与 POI 归阶段 1/后置 |
| G6 维度 | **完成（主世界+下界最小闭环；末地 not_run）** | 下界独立生成器（下界岩/岩浆海/黑曜石状地形/萤石）；传送门 USE 切换、返回缓存恢复；`WLD-07` 中途断言 `dimension=1`、返回后 `dimension=0` 且主世界传送门保留；末地只保留加载路径，未运行 |
| G7 时间/天气 | **完成（月相/降雪 not_run）** | 24000 tick 昼夜、天空暗化/太阳角、确定性雨/雷计时；`debug_set_time/weather` 仅场景模式；`WLD-06`；保存 not_run；群系降雪未实现 |
| G8 差分 | **not_run** | 未运行 vanilla oracle；阶段 1 未冻结阈值；留待阶段 5B |
| G9 性能 | **完成（本机口径）** | 169 区块初始生成 + 40 tick 实测 19.96 TPS（`build/m3/reports/worldgen-smoke.json`）；`WLD-08` RD4：81+ 区块、TPS ≥15 |
| G10 WLD 场景 | **完成（8 条）** | `WLD-01..08` 全 pass |

**运行命令（实测）**

```bat
cmake --build --preset m3-release --parallel 8
build\m3\bin\m1-tests.exe
build\m3\bin\reim-headless.exe --world build\m3\worlds\m3g --generated generated\26.3 ^
  --create-world --seed 12345 --ticks 40 --dump build\m3\reports\worldgen-smoke.json
build\m3\bin\reim-scenario.exe --suite tests\scenarios\m3g --world build\m3\worlds\m3g ^
  --out build\m3\reports\scenarios-m3g --generated generated\26.3 --dll build\m3\bin\mc_26.3.dll
```

结果：`m3g` 8 pass / 0 fail / 0 not_run；全套 m3a/m3d/m3e/m3h/m3f/m3g = **62 pass / 0 fail / 0 not_run**。

**阶段 3 G 明确 not_run（不得视为通过）**

- 新世界/区块/时间天气/玩家/实体的**持久化与存档往返**：B1 未开始；`request_save` 仍返回 `REIM_ERR_NO_WORLD`。
- G1 server jar datapack 摄取、G8 oracle 差分：分别归 M3-K5 与阶段 5B。
- 末地生成/加载、结构/村庄/要塞、下界完整生成、群系降雪：超出 M3 必做或未实现。
- UE 侧 R1 实体/环境渲染与音频：Zen storage server 阻塞；只在 headless/Preview 数据层验证。
- 通过率口径：当前 62 条为已实现场景（含阶段 1 脚手架与扩展 INT/INV/MOB/UI），**SAV-01..06 与 RES-01..04 尚未编入**；最终 62 条类别套件在阶段 5B 收口，不能据此宣称 G3 通过。

**范围变更记录**：见 §7（原生 worldgen profile、光照近似、仅内存世界）。

## 3.5 阶段 3 J 部分执行记录（2026-11-21）

| 任务 | 结果 | 证据 / 说明 |
|---|---|---|
| J3 实体渲染接入 | **Runtime + Preview 完成；UE not_run** | Runtime 发布 8 种模型定义（ids 1..8）、每实体部件变换/prev；`REIMPreview` 按 model_id 绘制全部部件（旋转/枢轴/颜色/受伤/着火 tint）；`MOB-01` 断言 8 实体/10 部件。截图：`build/m3/reports/visual/m3-s3-entities.png`（8 生物，含燃烧 tint）、`m3-s3-entities-cows.png`（多部件牛/猪/羊/鸡）。UE R1 实体渲染受 Zen storage server 阻塞；Preview 使用 block atlas 回退 UV，真实生物皮肤资产归阶段 4 |
| J6 天空/环境 | **Runtime 数据 + Preview 完成；UE not_run** | 时间/天气/维度驱动 `ReimEnvironmentSnapshot`（天空/雾色、暗化、太阳角、雨雷）；Preview 已消费；`WLD-06` 断言夜间/雷暴。UE 画面未运行 |
| 死亡界面 | **完成（Preview/数据）** | `REIM_SCREEN_DEATH` 显示列表（YOU DIED/死因/重生提示）；`UI-07` 断言；截图 `build/m3/reports/visual/m3-s3-death.png`；UE 未运行 |
| J4 方块实体渲染 | **not_run** | 依赖 C6 方块实体（阶段 1 未开始）；箱子/熔炉/告示牌渲染数据未接入 |
| J1/J2/J5/J7/J9/J10 | **阶段 4 范围** | 通用模型 Host 接入、平滑光照/AO/群系混合、粒子/天气、第一人称、视觉回归与 Renderer 审计不在阶段 3 收口；本会话未做 |
| 阶段 3 J 结论 | **部分完成** | Runtime/Preview 数据与绘制链路已验证；UE 运行缺失使阶段 3 不能标记完成 |

## 3.6 阶段 4 4B-K 执行记录（2026-11-21）

**性质**：阶段 4 工具链子轨（4B-K），不包含 4A-I/J 与 4B-L。所有命令均在本机实际运行；UE 运行仍未验证。

| 任务 | 结果 | 证据 / 说明 |
|---|---|---|
| K1 数据生成器编排 | **完成** | `reim-convert data --profile 26.3 --out work/26.3`：离线 JDK 25 运行官方 data generator；解包 bundler 内层 `server-26.3.jar` 的 `data/minecraft/**`；从 client asset index 复制 M3 声音/语言/字体；产出 `data_report.json`。实测 reports 1286 blocks、datapack 9884 files/8369 JSON、assets 209 local/5149 indexed、191 sound files；缓存戳按 server JAR SHA-256，`--skip-reports` 可复用 |
| K2 注册表/物品/实体 v3 | **完成（数据层）** | `blocks.json` 追加官方 `state_ids`（35723 状态）与 `protocol_id`；`items.json` 1658 项，35 项标 `runtime_native`，其余 `data_only`；`entities.json` 161 项，8 项映射原生模型 id 1..8 |
| K3 模型摄取 | **完成（摘要层）** | `models.json` 1173 个模型、块级 variants/states/geometry 计数；块级变体几何仍在 `blocks.json`（rotation/uv/cull 已烘焙）；limitations 明示 multipart/权重未单列 |
| K4 配方/战利品/标签 | **完成（数据层）** | `recipes.json` 2042 条（1681 条 supported 类型）；`loot_tables.json` 1447 张、函数白名单外显式列表；`tags.json` 883 个标签；均确定性排序 |
| K5 世界生成数据 | **完成（摄取 + Runtime 头校验）** | `worldgen.json` 1148 个定义（noise/biome/surface/feature/structure 等）；Runtime 启动读取并校验 `definition_count>0`，生成仍为原生 profile（未声称数据驱动生成） |
| K6 资产 | **完成（M3 范围）** | `sounds.json` 1991 个 sound_event + 17 个稳定 M3 事件 id（含 OGG 本地文件）；`lang/en_us.json` 8559 条；`font/` 16 文件；`item_atlas.png` 824 个 16×16 图标（7 个 32×32 跳过并记录）；`assets_index.json` 5149 项；197 个本地二进制随 generated 自包含 |
| K7 manifest v3 / Adapter pin | **完成（未运行 UE）** | `manifest.json` format/schema_version 3、ABI 3、capabilities 含 r1/r2/ui/audio/save/entities/data_v3；inputs 含 server JAR/reports/datapack/assets digest；无绝对路径；Adapter pin 与 `ue_adapter.json` 升 format 3，`package` 接受 render_level 0..2；UE 运行未验证 |
| K8 schema 校验 | **完成** | Runtime `validate_generated` 只接受 format 3，并强制 items/entities/models/recipes/loot/tables/tags/worldgen/sounds/lang/coverage 文件；单测覆盖 format 1/2 拒绝与缺失文件拒绝；legacy ABI 2 兼容桥已移除 |
| K9 覆盖报告 | **完成** | `coverage_report.json`：来源矩阵、支持/不支持计数、Java 代码来源单列（实体模型/AI、方块行为、物品属性、原生 worldgen）；`data_only` 与未支持项显式 |
| 可重复性 | **完成** | 连续两次 extract：`manifest.json` SHA-256 均为 `36656a324555bb520997b41de87a741cd9f8c111c87dc0ed2fe4d6a2dc83b1b5`；data 命令重复运行 datapack/assets digest 不变 |

**实测命令**

```bat
build\m3\bin\reim-convert.exe data --profile 26.3 --out work\26.3
build\m3\bin\reim-convert.exe extract --profile 26.3 --out generated\26.3
build\m3\bin\reim-convert.exe verify --generated generated\26.3 --profile 26.3
build\m3\bin\m1-tests.exe
```

结果：`verify` 报 ABI 3、38 个 generated 文件、1288 blocks；`m1-tests` 52 用例 / 821 断言通过；m3a/m3d/m3e/m3h/m3f/m3g 共 62 场景全部 pass（使用生成的 schema v3 数据）。

**明确 not_run / 缺口（不得视为通过）**

- Runtime 的配方/熔炼/战利品仍使用原生 M3 子集；K4 数据已产出并可差分，但未接管玩法（阶段 2 的 137 配方/41 熔炼数字不变）。
- `data_only` 物品 1623 个：数据存在不等于行为可用（§8.12）。
- 7 个 32×32 物品纹理（长矛 in-hand）未入图集，已在 `item_atlas.json.skipped` 记录。
- K5 的“数据驱动生成”未实现；Runtime 只校验并加载 worldgen 头，生成仍是原生 profile。
- UE Adapter/音频/渲染运行未验证；K7 只完成文件与 hash 层。

## 3.7 阶段 4 4B-L 执行记录（2026-11-21）

| 任务 | 结果 | 证据 / 说明 |
|---|---|---|
| L1 Java 分析器扩展 | **完成** | `analyze` 产出 `classes.json`（format 2：类/方法/字段列表、access、native、bytecode SHA-256）、`deps.json`、`class_deps.json`（156459 条边）、`surface.json`（reflection 7 类、classloader 1 类、0 native 方法）；11383 类 / 98739 方法 / 51646 字段；两次运行全部 hash 一致 |
| L2 数据/资产差分 | **完成** | `diff` 比较 blocks/items/entities/models/recipes/loot/tags/worldgen/sounds/lang/assets；同目录差分 0 变更；人为增删 item/recipe 后准确报 1 added / 1 removed 并生成 2 条 work item；两次 diff.json 同 hash |
| L3 port ledger | **完成** | `converter/ledger/26.3.json` 21 条（native/generated/analysis）；`ledger --check` 0 问题、0 缺失模块；报告 `work/26.3/analysis/port_ledger.json` |
| L4 Patch 格式/重放/三路合并 | **完成（Patch 集为空）** | `patch verify`：确定性 replay、merge_ok=true、conflict_ok=true；`patch selftest` 覆盖三路合并与冲突失败；`converter/patches/26.3/patch_set.json` 为空，等待真实第二版本 |
| L5 版本差异报告 → 工作项 | **完成（机制；未执行真实版本演练）** | `diff.json` 含 `kind=version_upgrade_candidate`、逐子系统 work_items（owner/severity/action）；真实/合成第二版本输入仍缺，L7 属 5A `not_run` |
| L6 可重复性 | **完成（本机）** | analyze 两次各报告 hash 一致；diff 两次一致；data/extract 重复一致（§3.6）；排序固定、无时间戳 |
| L8 决策记录 | **完成** | `conversion-decisions/26.3.md` §7 记录数据路线、schema v3、来源矩阵、分析/diff/patch/ledger 与未做项 |

**实测命令**

```bat
build\m3\bin\reim-convert.exe analyze --profile 26.3 --out work\26.3
build\m3\bin\reim-convert.exe ledger --profile 26.3 --out work\26.3 --check
build\m3\bin\reim-convert.exe diff --old <generated-old> --new <generated-new> --out build\m3\reports\diff
build\m3\bin\reim-convert.exe patch verify --set converter\patches\26.3 --out build\m3\reports\patch-verify.json
build\m3\bin\reim-convert.exe patch selftest --out build\m3\reports\patch-selftest.json
```

**明确 not_run**

- L7 真实版本升级演练（5A）：无第二版本输入，未执行。
- Patch 集为空：只验证机制，没有真实条目被应用。
- `diff` 的 Java 分析差分仅在显式传 `--old-analysis/--new-analysis` 时可用；本次只有 26.3 单版本，未运行跨版本分析。

## 3.8 阶段 4 4A-I 执行记录（2026-11-21）

**构建**：`cmake --build --preset m3-release --parallel 8` 通过；`m1-tests.exe` 821 断言通过；既有 62 场景回归通过。

| 任务 | 结果 | 证据 / 说明 |
|---|---|---|
| I1 声音注册/事件 | **完成（Runtime）** | `sounds.json` 1991 个 sound_event + 17 个稳定 M3 id；Runtime 加载并缓存 OGG 到 `sound_ogg_cache_`；`get_asset(SOUND_OGG, id)` 返回真实字节；类别/循环/衰减字段保留 |
| I2 UE 音频 | **not_run** | UE 运行仍被 Zen storage server 阻塞；本会话未生成 UE 音频截图/听感证据，未写成通过 |
| I3 事件映射 | **完成（Preview/headless）** | break=20、place=21、step=22、pickup=23、hit=24、weather=25/26、ambient=27、xp=28；动作队列发出的 break/place 在下一 tick 发布（修复了 tick 清空导致的丢事件）；`RES-03` 断言 20/21/23 均出现 |
| I4 资源包 | **部分完成（Runtime：语言/声音/纹理）** | 启动时读取 `resource_packs`；支持 lang 合并、sounds definitions/m3 合并、block/item 图集 PNG 覆盖与 revision 更新；模型/blockstates 覆盖明确警告为未重网格（需 Converter）；`RES-01`/`RES-02` pass |
| I5 数据包 | **完成（校验 + Converter 覆盖）** | Runtime 校验 `data_packs`（pack.mcmeta/format、路径/大小、类别计数）；`reim-convert data --datapack <pack>` 将 recipe/tags 覆盖进 `work/26.3/datapack`；实测覆盖后 `minecraft:oak_planks` 结果变为 dirt、item/planks 标签变为 dirt。Runtime 玩法仍用原生配方/战利品（未声称数据驱动） |
| I6 资产清单/校验 | **完成** | manifest v3 哈希校验、atlas revision、asset index 5149 项；启动校验失败即拒绝；无热重载（重启生效） |
| I7 RES 场景/安全 | **完成（4 条）** | `tests/scenarios/m3i/RES-01..04` 全 pass；`res_malicious.zip` 的 `../evil.txt` 在 create 阶段被拒绝（RES-04 pass）；单文件 64 MB/总 512 MB/条目 32768 限制生效 |
| I8 资产/许可证审计 | **完成** | `python tools/m3-asset-audit.py`：无受跟踪 Mojang 游戏数据；generated manifest 无绝对路径；third_party 许可证条目齐全；报告 `build/m3/reports/asset-audit.json` |

**实测命令**

```bat
cmake --build --preset m3-release --parallel 8
set REIM_TEST_WORLD=D:\Minecraft\reim-work\m3\worlds\REIM-M3-26.3-work
build\m3\bin\reim-scenario.exe --suite tests\scenarios\m3i --out build\m3\reports\scenarios-m3i ^
  --generated generated\26.3 --dll build\m3\bin\mc_26.3.dll
python tools\m3-asset-audit.py
```

结果：RES 4 pass / 0 fail；资产审计 OK；既有 m3a/m3d/m3e/m3h/m3f 场景无回归。

**明确 not_run / 缺口**

- UE 音频（I2）未运行；无 UE OGG 播放/混音证据。
- 资源包模型/blockstates 覆盖不在 Runtime 重网格；只记录警告，需 Converter 重新 extract 才能改变几何。
- 数据包覆盖只进入 generated 数据；Runtime 的 137 原生配方/41 熔炼/原生战利品未切换到数据包内容。
- CJK 字体未纳入；语言回退为 en_us（RES 只覆盖英文键）。

## 3.9 阶段 4 4A-J 执行记录（2026-11-21，Runtime/Preview 范围）

**构建**：`cmake --build --preset m3-release --parallel 8` 通过；`m1-tests.exe` 52 用例 / 821 断言通过；全套场景 m3a/m3d/m3e/m3h/m3f/m3g/m3i/m3j = **68 pass / 0 fail / 0 not_run**（62 条原有 + 4 RES + 2 R2）。

| 任务 | 结果 | 证据 / 说明 |
|---|---|---|
| J1 Host 通用模型 | **Preview 完成；UE not_run** | Runtime 产出 blocks.json 几何，Preview 上传绘制；UE 通用模型接入未运行。截图基线 `build/m3/reports/visual-current/day.png` |
| J2 平滑光照/AO/群系混合/水/树叶 | **部分** | 3×3 biome-cell tint 混合（草/叶/水）已接入 `section_mesh`；AO、平滑光照、水动画、树叶摆动未实现；未写成完成 |
| J3 Host 实体渲染 | **Preview 完成；UE not_run** | 阶段 3 已接实体部件/插值；本阶段视觉回归复跑无回归；UE R1 未运行 |
| J4 方块实体渲染 | **not_run** | 依赖 C6 方块实体（箱子/熔炉/告示牌），C6 未实现；未伪造 |
| J5 粒子 | **完成（Runtime/Preview）** | Runtime `ParticleSystem`：破坏 12 粒、爆炸 40 粒、雨/雷天气粒子；Preview 白纹理公告板 + 插值绘制；`R2-01` 12 粒、`R2-02` 天气粒子 pass；截图 `m3-s4-r2-rain-up.png` |
| J6 天空 | **完成（Preview，尽力）** | 太阳/月亮/星星/云 + 昼夜/天气颜色；夜/雨截图 `m3-s4-r2-night.png`、`m3-s4-r2-rain.png`；UE 天空未运行 |
| J7 第一人称/视野 | **部分完成（Preview）** | 第一人称手持物品（item atlas 四边形、bob/使用摆幅）、冲刺/水下 FOV、水下雾色；受伤屏幕效果/挥手全动画未做；UE 第一人称 not_run。截图 `m3-s4-r2-held.png` |
| J8 render level 协商 | **完成** | Runtime 接受 0/1/2，>2 返回 `REIM_ERR_CAPABILITY`；capabilities 含 R1/R2；status 发布 `render_level`；`R2-01` 断言 2；M2-B 单测改为拒绝 3 |
| J9 视觉回归 | **完成（Preview 共享子集）** | `tools/m3-visual-regress.py`：day/night/rain 三场景固定 tick 31、固定相机；基线 `build/m3/visual-baseline/`；复跑 mean diff ≤0.033、changed ratio ≤0.0004，3/3 pass；报告 `build/m3/reports/visual-current/visual-regress.json`；UE 专属实体/粒子/天气基线 not_run |
| J10 Renderer 审计 | **完成** | `python tools/m3-render-audit.py`：40 个 UE 源文件，0 命中；报告 `build/m3/reports/render-audit.json` |

**实测命令**

```bat
cmake --build --preset m3-release --parallel 8
build\m3\bin\m1-tests.exe
set REIM_TEST_WORLD=D:\Minecraft\reim-work\m3\worlds\REIM-M3-26.3-work
build\m3\bin\reim-scenario.exe --suite tests\scenarios\m3j --out build\m3\reports\scenarios-m3j ^
  --generated generated\26.3 --dll build\m3\bin\mc_26.3.dll
python tools\m3-visual-regress.py --world %REIM_TEST_WORLD% [--update]
python tools\m3-render-audit.py
```

**UE 尝试（2026-11-21）**

```bat
cd runtime\host
Binaries\Win64\REIMRuntime.exe -game -log -windowed -ResX=640 -ResY=360 -nosplash -unattended
```

25 秒超时内日志持续出现 `LogStorageServerConnection: Send to storage server failed ... GET /prj/REIM.845db468/oplog/Windows`（10 次），引擎未完成初始化；证据 `build/m3/reports/visual/m3-s4-ue-zen-blocked.log`。按规则停止 UE 工作并报告，未生成 UE 截图/音频证据。

**明确 not_run / 缺口**

- UE 侧 J1/J3/J4/J6/J7 画面与 I2 音频：Zen storage server 阻塞，全部 `not_run`；不允许用 Preview 结果替代 UE 验收。
- J4 还缺少 C6 方块实体数据/网格；不是渲染单独可补的项。
- J2 仅做 3×3 biome tint；AO/平滑光照/水动画/树叶摆动未做。
- J7 受伤屏幕效果、完整挥手动画未做。
- J9 只覆盖 Preview 共享子集；UE 专属实体/粒子/天气视觉基线未运行。

## 3.10 阶段 5A 执行记录（2026-11-21，版本演练/翻译门禁）

**结论**：5A 工作包（M3-M、M3-L7）已按用户决定收口；5A 不改变任何 Runtime 玩法/Contract 语义。

| 任务 | 结果 | 证据 / 说明 |
|---|---|---|
| M1 候选评审 | **完成（no-go）** | 评审 DFU Codec/DynamicOps、Brigadier、其他纯逻辑；0 个候选通过门禁。理由与门禁标准写入 `conversion-decisions/26.3.md` §8 |
| M2 JRT 最小面 | **not_run（no-go 后不需要）** | 未创建 JRT 源码/库；M3-M5 审计见下 |
| M3 子系统翻译 | **not_run（no-go）** | 未翻译任何 Java 代码；未生成代码补丁 |
| M4 替代方案/证据 | **完成** | 原生表 + generated schema v3 + port ledger；未把 Java 代码来源伪装成数据产出（§7/§3.6） |
| M5 JRT 面统计/退出计划 | **完成（审计）** | `python tools/m3-jrt-audit.py`：86 个 Runtime/Preview/headless/Contract 源文件，0 个非注释 JVM/JNI/JRT/bytecode 命中，0 个 JVM 链接项；报告 `build/m3/reports/jrt-audit.json`；Runtime 无 Java 进程/类加载/字节码解释器 |
| L7 版本升级演练 | **not_run** | 无 26.3 相邻第二版本；用户在本次会话选择「记 not_run，等真实版本」，未批准受控合成 delta。Patch 集仍为空；不得写成版本升级通过 |

**判定**：翻译/JRT 门禁为 **no-go**，不阻塞其他工作包；M3-M 通过的是「决议与证据」，不是翻译实现。G3 的「版本升级」条件仍为 `not_run`（L7）。

### 3.10.1 UE 解阻与 5B 预检（发现类修复 S5-a）

阶段 1–4 记录的 UE 阻塞根因是 **Zen storage server 无进程监听 8558**，不是引擎/项目缺陷。本次在指定开发机启动 UE 自带 Zen server 后 UE R0 可运行：

```bat
"C:\Users\<user>\AppData\Local\UnrealEngine\Common\Zen\Install\zenserver.exe" --port 8558 --data-dir D:\UE-DDC\Zen --http asio ...
:: 或官方启动器
"D:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\ZenLaunch.exe" -Sponsor=None
```

由此发现并修复两个阻塞验收的缺陷（只修缺陷，不扩功能）：

| 缺陷 | 修复 | 影响 |
|---|---|---|
| Bridge 仍要求 `ue_adapter.json` format=2，而 K7 已升 format 3 | `ReimBridgeSubsystem.cpp` 适配器校验改为 format 3 | UE 首次能通过 NativeVersion/Adapter 校验并加载 DLL |
| UE 自动化 `REIM.Bridge.Errors` 仍预期 render_level 1 被拒绝，J8 已改为 0/1/2 协商 | 测试改为接受 1、拒绝 3，并断言 info.render_level=1 | 测试与 Contract/Runtime 一致 |

**实测结果（全部为本次实际运行）**

| 项 | 结果 | 证据 |
|---|---|---|
| UE R0 实档进入/截图/退出 | **pass** | 81 chunks / 207 sections / 800588 blocks / 20.00 TPS；截图 `build/m3/reports/visual/m3-s5-ue-world.png`；20 s 后 exit 0；`m3-s5-ue-world.log`、`m3-s5-ue-state.json`、`m3-s5-ue-perf.json` |
| `REIM.Bridge.NullRHI` | **pass** | 1000 tick、frame blob 准入；`build/m3/reports/m3-s5-ue-nullrhi.log` |
| `REIM.Bridge.Errors` | **pass** | 缺失 world、ABI、render_level 0/1/2、>2 拒绝、干净生命周期；同上 |
| `REIM.R0.Smoke` | **pass** | GPU 路径 world ready、draw/tris、上传预算；`build/m3/reports/m3-s5-ue-r0.log` |
| UE R1/R2 渲染 | **not_run / 未实现** | Host 仍只接 R0 世界网格；Bridge 固定接受 adapter render_level 0；不把 R0 截图当作 R1/R2 |
| UE UI/实体/音频 | **not_run / 未实现** | Host 源码无 UI 显示列表、实体渲染或音频事件消费；Runtime 侧数据已验证（headless/Preview） |

**范围声明**：本节的 UE 结果只证明 **R0 世界渲染与 Bridge/R0 自动化**；不证明 R1/R2、UE UI、实体、音频、第一人称或干净机。

## 3.11 阶段 5B 执行记录（2026-11-21，测试/打包/验收）

**性质**：5B 在阶段 1–4 未全部完成的前提下执行**可执行部分**；不把缺失项写成通过。UE 已解阻，因此本次实际运行了 R0 世界渲染、Bridge/R0 自动化、三 Host 状态一致性、RC Shipping 与错误矩阵。

### 3.11.1 构建、单元测试、场景

| 项 | 结果 | 证据 |
|---|---|---|
| Native 构建 | **pass** | `cmake --build --preset m3-release --parallel 8` exit 0 |
| 单元/集成测试 | **pass（52 用例 / 821 断言）** | `build/m1-tests.exe`；`build/m3/reports/m3-s5-tests.log` |
| 场景套件（已实现） | **68 pass / 0 fail / 0 not_run** | m3a 5、m3d 12、m3e 12、m3h 6、m3f 19、m3g 8、m3i 4、m3j 2；报告 `build/m3/reports/scenarios-s5/` |
| 62 条类别口径 | **56/62 = 90.3%；not_run=6；门禁 FAIL** | `python tools/m3-scenario-accounting.py` → `build/m3/reports/scenario-accounting.json`；SAV-01..06 未编写（B1 WorldWriter 未实现） |
| Preview 视觉回归 | **pass（day/night/rain 3/3）** | `python tools/m3-visual-regress.py`；`build/m3/reports/visual-current/visual-regress.json` |
| UE R0 视觉基线 | **pass（Dev vs Shipping）** | 同世界/时间截图 mean abs diff 0.1844、changed ratio 0.0019（阈值 3.0/0.02）；`build/m3/reports/ue-r0-visual.json` |

### 3.11.2 N1–N8

| 任务 | 结果 | 说明 |
|---|---|---|
| N1 场景 DSL/runner | **完成（已实现套件）** | 68 条可跑；SAV 缺 6 条，`not_run` 显式计入 |
| N2 vanilla oracle | **not_run** | 用户规则禁止启动 Java 游戏进程；未用 Python 读路径替代 server oracle。JDK 只用于离线 Converter |
| N3 存档往返矩阵 | **not_run** | `request_save` 仍返回 `REIM_ERR_NO_WORLD`；B1 未开始；SAV-01..06 未编写 |
| N4 差分 | **机制 pass；vanilla 差分 not_run** | `diff` 同输入 0 work items；`patch verify/selftest` 通过；无 oracle 世界/配方/战利品基线 |
| N5 视觉/音频回归 | **部分（Preview 视觉 + 事件断言）** | Preview 3/3；UE R0 Dev/RC 图像对比通过；RES-03 音频事件 pass；UE 音频 `not_run`（Host 未接） |
| N6 性能/soak | **pass（Runtime soak + UE 20 s）** | 30 分钟 headless soak：tick 35839、20.17 TPS、内存 48.97→48.95 MB、无 timeout/fail；UE 20 s：20.00 TPS、Bridge p99 0.04 ms、RT 稳态 0.006 ms；见 §3.11.7 |
| N7 错误矩阵 | **pass（10/10 + 2 个 M3 用例）** | `m2-errors` 对 Shipping NativeBuild 全 pass；另测 adapter format 2 拒绝、render_level 3 拒绝；minimize 因无顶层窗口未实际执行（报告中标注） |
| N8 通过率/CI 脚本 | **完成** | 新增 `tools/m3-acceptance.ps1`、`m3-scenario-accounting.py`、`m3-host-consistency.py`、`m3-jrt-audit.py`、`m3-package-audit.py`、`m3-image-compare.py`、`m3-package.ps1`；`m3-acceptance.json` 逐项写 pass/fail/not_run |

**一键验收实测**：`powershell -File tools/m3-acceptance.ps1 -World <copy> -Generated generated/26.3 -SoakReport build/m3/reports/perf/m3-s5-soak-30m.json` → **executed 21 / fail 1 / not_run 4**；唯一 fail 是 `scenario-gate`（56/62，SAV 6 条未编写），其余已执行项全 pass；not_run = save-roundtrip、vanilla-oracle-diff、version-upgrade、clean-machine。报告 `build/m3/reports/m3-acceptance.json`，G3 `false` 及缺失列表在同一文件。

### 3.11.3 UE R0 与三 Host 一致性

| 项 | 结果 | 证据 |
|---|---|---|
| UE Development R0 | **pass** | 81 chunks / 207 sections / 800588 blocks / 20.00 TPS；`build/m3/reports/visual/m3-s5-ue-world.png`、`m3-s5-ue-state.json`、`m3-s5-ue-perf.json`；exit 0 |
| UE Shipping NativeBuild RC | **pass** | `build/native-build/26.3/REIMRuntime/REIMRuntime.exe` exit 0；截图 `m3-s5-rc-world.png`；state/perf 同 R0 |
| `REIM.Bridge.Errors` + `REIM.Bridge.NullRHI` | **pass 2/2** | `build/m3/reports/m3-s5-ue-nullrhi.log`；render_level 0/1/2 接受、3 拒绝 |
| `REIM.R0.Smoke` | **pass 1/1** | `build/m3/reports/m3-s5-ue-r0.log`；GPU world ready/draw/tris/上传预算 |
| 三 Host 同 DLL 状态一致（tick 61） | **pass（20 字段）** | Preview/headless/UE 均 81 chunks/207 sections/800588 blocks、相同玩家位置/生命/饥饿/维度/时间/UI screen/配方数/save 状态；`build/m3/reports/host-consistency.json`；DLL SHA-256 均为 `bd22cf86…` |
| UE R1/R2/UI/实体/音频 | **not_run / Host 未实现** | Host 只消费 R0 世界网格；不把 R0 截图当作 R1/R2 或 UI/音频证据 |

**Zen 运行前置**：UE/Cook/打包会启动/停止自己的 Zen server。本轮在 `m2-package` 打包结束后 Zen 8558 被停止，UE 会再次阻塞；验收前需重新运行 `ZenLaunch.exe -Sponsor=None`。这是环境操作，不是引擎缺陷。

### 3.11.4 打包、可重复性、审计（O1/O3）

| 项 | 结果 | 证据 |
|---|---|---|
| O1 Shipping RC | **完成** | `tools/m3-package.ps1 -Config Shipping` → `build/m3/ue-shipping-package/Windows`；`reim-convert assemble` → `build/native-build/26.3`；RC 在开发机启动/进入世界/退出 |
| O1 可重复性 | **pass（本机）** | `package` 两次：native_version/verification/ue_adapter/DLL 字节一致；`assemble` 两次：`native_build.json` 均为 `9386f073…`；目录 digest `3ea0872a…`/`ec1dbc32…` |
| O3 依赖/许可证/内容 | **pass** | `python tools/m3-package-audit.py`：包内/RC 无 `*.jar/*.mca/level.dat/jvm.dll`、无 generated/world；DLL 只依赖 VC 运行库；Shipping exe 无 JVM 导入；NOTICES/third_party 许可证存在；`build/m3/reports/package-audit.json` |
| 资产/渲染/UI/JRT 审计 | **pass** | asset 198 files；render 40 files 0 hit；UI 0 hit；JRT 86 files 0 hit、0 JVM 链接 |
| O2 干净机 | **not_run** | 无干净机；未在开发机模拟干净机 |
| O5 G3/用户试玩 | **FAIL / 未确认** | 见 §5；等待用户试玩与缺失项关闭 |

### 3.11.5 本次实际运行命令（节选）

```bat
cmake --build --preset m3-release --parallel 8
build\m3\bin\m1-tests.exe
set REIM_TEST_WORLD=D:\Minecraft\reim-work\m3\worlds\REIM-M3-26.3-work
for %s in (m3a m3d m3e m3h m3f m3g m3i m3j) do ^
  build\m3\bin\reim-scenario.exe --suite tests\scenarios\%s --out build\m3\reports\scenarios-s5\%s ^
    --generated generated\26.3 --dll build\m3\bin\mc_26.3.dll
python tools\m3-scenario-accounting.py --reports build\m3\reports\scenarios-s5
python tools\m3-visual-regress.py --world %REIM_TEST_WORLD%
python tools\m3-asset-audit.py
python tools\m3-render-audit.py
python tools\m3-ui-audit.py --root runtime\host\Source
python tools\m3-jrt-audit.py
"D:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\ZenLaunch.exe" -Sponsor=None
build\m3\bin\reim-convert.exe package --profile 26.3 --generated generated\26.3 ^
  --dll build\m3\bin\mc_26.3.dll --ue-adapter runtime\host\Build\Adapter --out build\native-version\26.3
powershell -ExecutionPolicy Bypass -File tools\m3-package.ps1 -Config Shipping
build\m3\bin\reim-convert.exe assemble --native-version build\native-version\26.3 ^
  --runtime-package build\m3\ue-shipping-package\Windows --out build\native-build\26.3
powershell -ExecutionPolicy Bypass -File tools\m2-errors.ps1 -World %REIM_TEST_WORLD% ^
  -Generated generated\26.3 -NativeBuild build\native-build\26.3
python tools\m3-package-audit.py
powershell -ExecutionPolicy Bypass -File tools\m3-acceptance.ps1 -World %REIM_TEST_WORLD%
```

### 3.11.6 5B 明确 not_run / 缺口（不得视为通过）

- SAV-01..06 与存档往返矩阵：B1 WorldWriter 未实现；`request_save` 返回 `REIM_ERR_NO_WORLD`。
- vanilla oracle 差分（worldgen/配方/战利品/光照/实体）：未启动 Java 游戏进程，未运行 server oracle。
- 干净机 O2/F5：无机器。
- 版本升级 L7：无相邻第二版本，未批准合成 delta。
- UE R1/R2、UI 显示列表、实体、音频、第一人称：Host 未实现；R0 结果不能替代。
- H6 视觉逐项清单：用户未提供；tonemap/不透明双面未在 UE 做判定。
- 多 element/旋转模型 skip、AO/水动画/树叶摆动、加载页截图/键位自动化：仍未完成。

### 3.11.7 30 分钟 soak（N6）

**实测**：`reim-headless.exe --world <real-save-copy> --generated generated/26.3 --render-distance 6 --seconds 1800 --timeout 1900 --dump build/m3/reports/perf/m3-s5-soak-30m.json`

| 指标 | 结果 |
|---|---|
| 时长 | 1800.01 s（30 min），exit 0 |
| tick / TPS | tick 35839、20.17 TPS（20±0.2 内） |
| 世界 | 169 chunks / 207 sections / 800588 blocks，ready=true |
| 结果 | `timed_out=false`、`failed=false`；无崩溃 |
| 内存 | 48.97 MB → 48.95 MB（30 s 采样，`m3-s5-soak-memory.csv`）；无持续增长 |
| 通道 | 天气/时间推进（day 1、weather 1、audio_events 77）；玩家生命/饥饿保持 |

**口径**：这是 Runtime/headless 的 30 分钟稳定性；UE R0 的性能样本是 20 s，R1/R2 与完整 Host 负载未测，不据此宣称 UE 30 分钟通过。

## 4. M2 遗留收口

| 遗留 | 归属 | 状态 |
|---|---|---|
| F5 干净机 | 阶段 5B / M3-O | `not_run` |
| H6 视觉逐项清单 | 阶段 1 收集 / 阶段 4 收口 | 待用户清单；本阶段只建立 Preview 视觉回归基线 |
| UE 不透明双面 | 阶段 1 / 阶段 4 | `not_run`：UE R0 已可运行，但未做双面/绕序视觉判定 |
| UE tonemap 偏暗 | 阶段 4 | `not_run`：UE R0 截图已产生，但未与 Preview 做受控色彩校准 |
| biome 单色 | 阶段 3 / 阶段 4 | **部分完成**：3×3 biome-cell tint 混合已接入 mesh（草/叶/水）；跨 section 混合、AO/平滑光照未做 |
| 多 element/旋转模型 skip | 阶段 1 / 阶段 4 | **未开始**：extract 仍只对 ≤2 element/无旋转模型产出 geometry；楼梯/门/栅栏/墙仍需 C7/J1 收口 |
| LZ4 不支持 | 阶段 1 / B1 | **读取已接入**（`compression::lz4_decompress_region`，合成 LZ4 块测试通过）；写入仍为 zlib；`.mcc` 读取沿用旧路径，写策略待 B1 |
| 加载页截图/键位自动化 | 阶段 5B | 未开始 |

## 5. G3 门禁（实测结果；`not_run` 不计为通过）

| 条件 | 状态 | 证据 |
|---|---|---|
| 玩法闭环（≥95%） | **FAIL** | 62 口径 56/62 = 90.3%；SAV-01..06 六条 `not_run`；已实现 68 条全 pass，但不足以补足 SAV 类别 |
| 存档兼容 | `not_run` | B1 WorldWriter 未实现；`request_save` = `REIM_ERR_NO_WORLD`；未启动原版验证 |
| 工具链可重复 | **PASS（本机）** | package 两次逐字节一致；assemble 两次 `native_build.json` 同 hash；不承诺跨机字节一致 |
| 差分有效 | **FAIL（机制通过，oracle 未运行）** | diff/patch/ledger 机制 pass；无 vanilla 世界/配方/战利品/光照基线 |
| 契约/双后端 | **PASS** | 同一 `mc_26.3.dll`（SHA-256 `bd22cf86…`）驱动 Preview/headless/UE；Render/UI 审计 0 命中；Host 无第二套逻辑 |
| 三 Host v3 一致 | **部分通过** | tick 61 的 20 个 Runtime 字段（世界/玩家/UI screen/配方/save）三 Host 一致；实体/音频通道未在该固定场景生产事件，未据此宣称完整 |
| 性能/稳定 | **部分通过** | 30 分钟 Runtime soak pass（20.17 TPS、内存稳定）；UE R0 20 s：20.00 TPS、Bridge p99 0.04 ms、RT 稳态 0.006 ms；R1/R2 与完整 Host 负载未测 |
| 干净机/RC | **FAIL** | Shipping RC 在开发机启动/进入世界/退出 pass；干净机 `not_run` |
| 版本升级 | `not_run` | 无相邻第二版本；合成 delta 未批准 |
| M2 遗留收口 | **部分** | Zen 阻塞已解除；F5/H6/双面/tonemap/多 element/LZ4 写/加载页仍未关闭或未豁免 |

## 6. 待用户决定（会改变范围/验收）

| # | 项 | 默认 |
|---|---|---|
| 1 | 干净机（物理机/GPU 直通 VM） | 必须提供或显式豁免 |
| 2 | 26.3 相邻第二版本，或批准受控合成 delta | 优先真实版本 |
| 3 | 生存-only 是否接受（创造/旁观后置） | 接受 |
| 4 | 下界要求新世界最小生成 | 要求 |
| 5 | 洞穴最小子集纳入必做 | 接受 |
| 6 | CJK/中文 UI 是否要求 | 未定；未纳入则英文回退 |
| 7 | H6 视觉问题逐项清单 | 待提供 |
| 8 | 磁盘清理/扩容 | 建议处理后再开始 UE Cook |

## 7. Scope Changes

> 任何范围变更必须写在这里，并同步场景套件。

| 日期 | 变更 | 原因 / 场景同步 |
|---|---|---|
| 2026-11-21 | 阶段 2 的物品/配方/熔炼/战利品使用**原生 M3 子集**（1327 物品、137 配方、41 熔炼），而不是从生成数据读取 | Converter 尚未产出 `items.json/recipes.json/loot_tables.json`（M3-K4，阶段 4B）；原生表按 §9.2 来源矩阵标注为 Java 代码来源。INT/INV 场景覆盖该子集；oracle 差分留待阶段 5B |
| 2026-11-21 | Contract v3 增加**增量键码** `REIM_KEY_E/Q/1-9/ENTER/TAB`（无结构体布局变化） | 背包/丢弃/快捷栏/暂停由 Runtime 解释键意图；Rendering Contract 的 v3 冻结规则允许不改变字段语义/布局的 ABI 增量。Preview/headless 已同步；UE 重编译在指定开发机执行时进行 |
| 2026-11-21 | 阶段 2 容器/背包状态**仅保存在 Runtime 内存**，不做存档集成 | M3-B1 WorldWriter 未实现（阶段 1 B 未开始）；D8/E4/E5 的存档项标 `not_run`，不写成通过 |
| 2026-11-21 | 阶段 3 的 8 种生物模型/动画/掉落/生成规则为**原生表**（`entity.cpp`），不是 Converter 产出 | 26.3 实体模型是 Java 代码（M3_PLAN 9.2 来源矩阵）；Converter 只记录/生成部分；`MODEL_DEFS_JSON` 标注 native source |
| 2026-11-21 | 阶段 3 的自然生成使用**时间+地表暴露近似**，未使用 15 级光照引擎 | C4 光照引擎属阶段 1 且未开始；自然生成默认在场景中关闭，正常运行开启；该近似写入已知限制 |
| 2026-11-21 | 阶段 3 玩家/实体/死亡状态**仅保存在 Runtime 内存**；XP 以点数直接入账，不生成经验球实体；爆炸不产生方块掉落 | B1 未实现；范围封顶未要求经验球物理；爆炸掉落属尽力项 |
| 2026-11-21 | 场景 DSL 新增 `platform` setup 与实体/生存断言（`entity_type_count`、`entity_type_health_at_most` 等） | 仅 headless/Preview 诊断用，不进入 Shipping；不改变 Contract |
| 2026-11-21 | 阶段 3 世界生成为**原生 profile**（`world/worldgen.cpp`），只支持可选的 `generated/worldgen/profile.json` 覆盖；未摄取 server datapack | M3-K5 属阶段 4B；按 §9.2 来源矩阵标注 native；`worldgen_source` 状态字段防止把 native 当成数据驱动 |
| 2026-11-21 | 阶段 3 新世界/区块/天气**仅在内存**；headless `--create-world` 不写 level.dat/region | B1 WorldWriter 未开始；持久化/往返矩阵 `not_run`，不写成通过 |
| 2026-11-21 | 阶段 3 自然生成用「时间 + 地表暴露 + 土壤」近似，未使用 15 级光照；下界按维度规则生成敌对生物 | C4 光照引擎未开始；场景默认关闭自然生成，`MOB-10` 显式开启 |
| 2026-11-21 | 场景 DSL 新增 `create_world`/`seed`、`debug_set_time/weather` 动作、阶段内断言 `assert` 步骤与 `biome_at` | 仅自动化/诊断；Contract v3 动作 64/65 已定义，Shipping 禁用 |
| 2026-11-21 | 阶段 4：`render_level` 从「只接受 0」改为协商 0/1/2（>2 拒绝）；Runtime capabilities 增加 R1/R2，status 发布 render_level | M3-J8；ABI 布局不变，仅语义/注释更新；M2-B 单测改为拒绝 3；R2-01 断言 2 |
| 2026-11-21 | 阶段 4：资源包在 Runtime 支持语言/声音/纹理覆盖，模型/blockstates 的运行时重网格**未做**；数据包在 Runtime 只校验/计数，Converter `data --datapack` 才覆盖 generated recipe/tags | I4/I5；不把「数据包覆盖」写成 Runtime 玩法生效；模型重网格留待 C7/J1 后续 |
| 2026-11-21 | 阶段 4：新增 Runtime 粒子/天气系统与 Preview 天空/手持物品；J2 仅 3×3 biome tint，AO/水动画/树叶摆动未做 | J5/J6/J7 表现项；新场景 R2-01/02 与视觉回归；不宣称完整 R2 |
| 2026-11-21 | 阶段 5 S5-a：Bridge 的 `ue_adapter.json` format 校验从 2 改为 3；UE 自动化 render_level 测试改为接受 1、拒绝 3 | 阶段 4 K7 已把 descriptor 升 format 3，但 UE Bridge 未同步，导致 UE 永远拒绝启动；J8 已改 Runtime 语义。只修复缺陷，未改 ABI/Contract 布局 |
| 2026-11-21 | 阶段 5：Zen storage server 作为 UE 运行前置显式记录；打包会停止共享 Zen，验收前需重新启动 | 阶段 1–4 的 UE `not_run` 根因是环境进程缺失；R0 现已实测。R1/R2/UI/实体/音频仍未实现，不据此宣称阶段 4 完成 |
| 2026-11-21 | 阶段 5：L7 版本升级演练记 `not_run`；干净机记 `not_run`；不构造合成 delta、不模拟干净机 | 用户本次明确选择；G3 对应项保持不通过 |
| 2026-11-21 | 阶段 5：新增验收工具 `m3-acceptance.ps1`、`m3-scenario-accounting.py`、`m3-host-consistency.py`、`m3-jrt-audit.py`、`m3-package-audit.py`、`m3-image-compare.py`、`m3-package.ps1` | 仅测试/验收工具，不改变 Runtime/Contract/玩法；对应 N8/O1/O3 |
