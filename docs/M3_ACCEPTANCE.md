# REIM M3 Acceptance Record

> 只记录**实际执行过**的检查。未执行的项明确写 `not_run` 或「豁免」，不得视为通过。
> S10 已并入 S5：M3 为 **S1–S9**，其中 **S5a** 是玩法实现，**S5b** 是最终验收（必须在 S6–S9 完成后执行）。
> 本文件当前是 **M3 启动骨架**，不是完成声明。

## 1. 状态

| 项 | 当前 |
|---|---|
| M3 阶段 | 未开始（计划已修订） |
| 会话 | S1–S9；S5 分 S5a/S5b |
| G3 | `not_run` |
| 场景套件 | 0 / 62 |
| M2 遗留 | F5 干净机未运行；H6 仅“暂时通过”，逐项清单待用户提供 |

## 2. 开发环境实测（2026-11-21）

| 项 | 实测值 | 状态 |
|---|---|---|
| Unreal Engine | 5.8.4 Patch 4，`D:\Program Files\Epic Games\UE_5.8` | 可用 |
| MSVC | 14.44.35207（VS 2022 BuildTools） | 可用 |
| Windows SDK | 10.0.26100.0 | 可用 |
| CMake | 4.4.4 | 可用；M3 preset 待 S1 增加 |
| Python | 3.12.10 | 可用 |
| JDK | Temurin 25.0.5 | 可用（仅离线） |
| Git LFS | 3.5.1 | 可用 |
| GPU | RTX 4060，DX12/SM6 | 可用 |
| CPU / RAM | i5-12490F 6C/12T / 32 GB | 可用 |
| 26.3 client jar | `D:\Minecraft\.minecraft\versions\26.3\26.3.jar` | 可用 |
| 26.3 server jar | `D:\Minecraft\reim-work\26.3\server.jar`（version.json: 26.3 / 5023 / 777 / Java 25） | 可用 |
| 真实存档 | `D:\Minecraft\.minecraft\saves\REIM-M1-26.3` | 可用（server 存档，无玩家数据） |
| 干净机 | 未提供 | **阻塞 S5b/O2** |
| 第二版本输入 | 未提供（仅 26.3、1.21.1、1.20.1、ATM10） | **阻塞版本升级演练** |
| D 盘余量 | 206 GB | 偏紧；需清理/扩容 |

## 3. 26.3 实档审计（S1 A1 必做）

当前实测布局：

```text
level.dat                         (402 B, DataVersion 5023)
data/minecraft/weather.dat
data/minecraft/world_clocks.dat
data/minecraft/world_gen_settings.dat
data/minecraft/game_rules.dat
players/data/                     (空)
dimensions/minecraft/overworld/region/r.*.mca
dimensions/minecraft/the_nether/data/minecraft/*.dat
dimensions/minecraft/the_end/data/minecraft/*.dat
```

待审计/确认：

- [ ] 实体与 POI 的实际存放位置（本存档无顶层 `entities/`、`poi/` region）
- [ ] `players/data/<uuid>.dat` 的 26.3 字段与文件名规则
- [ ] weather/world_clocks/world_gen_settings/game_rules 的写入归属与原版读取顺序
- [ ] 区块 `Status`/`isLightOn`/heightmaps/blending/structures/ticket 字段
- [ ] `.mcc` 与 LZ4 的写入策略
- [ ] 多文件保存的 journal/崩溃恢复语义

## 4. M2 遗留收口

| 遗留 | 归属 | 状态 |
|---|---|---|
| F5 干净机 | S5b / M3-O | `not_run` |
| H6 视觉逐项清单 | S1 收集 / S7 收口 | 待用户清单 |
| UE 不透明双面 | S2/S7 | 未开始 |
| UE tonemap 偏暗 | S7 | 未开始 |
| biome 单色 | S6/S7 | 未开始 |
| 多 element/旋转模型 skip | S2/S7 | 未开始 |
| LZ4 不支持 | S2/B1 | 未开始 |
| 加载页截图/键位自动化 | S5b | 未开始 |

## 5. G3 门禁（全部 `not_run`）

| 条件 | 状态 | 证据 |
|---|---|---|
| 玩法闭环（≥95%） | `not_run` | — |
| 存档兼容 | `not_run` | — |
| 工具链可重复 | `not_run` | — |
| 差分有效 | `not_run` | — |
| 契约/双后端 | `not_run` | — |
| 三 Host v3 一致 | `not_run` | — |
| 性能/稳定 | `not_run` | — |
| 干净机/RC | `not_run` | — |
| 版本升级 | `not_run` | — |
| M2 遗留收口 | `not_run` | — |

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

（暂无）
