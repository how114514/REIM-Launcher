# M1 S4 验收记录（M1-F + M1-G + M1-H）

> 日期：2026-11-19  
> 存档：`D:\Minecraft\.minecraft\saves\REIM-M1-26.3`  
> 生成物：`D:\EngineeringDocuments\REIM-Launcher\generated\26.3`  
> 构建：`cmake --preset m1-release` / `cmake --build --preset m1-release`  
> 规则：本文只记录**实际运行过**的结果。未实际执行的项目明确标为“未运行/待用户执行”。

---

## 1. S4 交付内容

| 工作包 | 实现 |
|---|---|
| M1-F1 | GLFW 窗口、OpenGL 3.3 Core、VSync/`--no-vsync`、ESC 释放鼠标或关闭、`--hidden` 自动化 |
| M1-F2 | `LoadLibrary` + `reim_get_api` + ABI/结构尺寸校验；create/start 失败有名称化错误码 |
| M1-F3 | 每帧 `fetch_frame`；增量 update/remove 上传到 VAO/VBO/EBO；零顶点 update 清除该材质 |
| M1-F4 | 图集着色器：方向光、雾、透明水混合；无光照层时方向光兜底（S3 已知问题） |
| M1-F5 | 视锥剔除；不透明近到远、半透明远到近排序 |
| M1-F6 | HUD：FPS、TPS、帧耗时、坐标、区块/区段、方块数、draw/tris/culled、图集尺寸 |
| M1-F7 | 鼠标捕获（点击捕获、ESC 释放）、WASD/空格/Shift/Ctrl/方向键 → `submit_input` |
| M1-F8 | F5 第一/第三人称切换；F8 玩家 AABB 线框；第三人称相机按 Runtime 回拉距离停在碰撞方块前 |
| M1-G | 灵敏度/反转参数、第一人称相机、碰撞/跳跃/下落、流式加载、加载进度与错误日志 |
| M1-H1 | 统一参数 `--minecraft-dir`、`--world`、`--generated`、`--render-distance` |
| M1-H2 | 缺世界/缺生成物/无效 DLL/ABI 不匹配 → 明确错误与非零退出 |
| M1-H3 | 每 5 秒统计日志、`preview.log` 超 8 MiB 轮转为 `.1`、可选 soak |
| M1-H4 | `tools\mc-oracle.py runtime` 对照 Runtime 方块样本 |
| M1-H5 | `tools\s4-acceptance.ps1`（含可选 `-SoakSeconds 600`）、`tools\s4-smoke.ps1` |
| M1-H6 | 本文“已知限制” |

预览端只消费 Runtime 快照：不读取存档、不重建世界、不写回；图集来自 `generated/26.3` 静态资源。

### 1.1 本轮修复（2026-11-19 晚）

- **方向键视角**：`InputState` 增加 `look_*`，↑↓←→ 在 Simulation 内以固定 5°/tick 旋转视角（100°/s @20 TPS），不再是被提交后丢弃的死路径。
- **第三人称相机防穿模**：Runtime 每帧沿 `-forward` 以 5 cm 步进查询碰撞方块，得到 0–4 格的回拉距离，经 `ReimRenderFrame::third_person_distance` 交给 Host；Preview 不再固定后退 4 格。
- **空 section 清除**：remesh 时若 section 变空，会为曾存在的材质发出零顶点 update，Host 删除旧网格；为后续方块编辑功能补齐清除路径。
- **多实例日志**：DLL 内改为广播 Host 日志列表，先销毁的实例不再清掉其他实例的 sink。
- **报告字段**：`s4-preview.json` 增加 `third_person_distance`。
- **文档**：测试断言数改为“随帧轮询变化”，避免再出现固定数字与实际运行不符。

---

## 2. 自动化验收（实际运行）

运行命令：

```bat
powershell -ExecutionPolicy Bypass -File tools\s4-acceptance.ps1 -PreviewSeconds 12
```

结果（`build\m1\reports\s4-acceptance.json`）：

| 检查 | 结果 | 实测 |
|---|---|---|
| Release 构建 | PASS | 全部目标产出 |
| `m1-tests`（含真实世界集成） | PASS | 27 test cases；断言数随集成测试的帧轮询次数变化（本轮 1106），不作为固定指标 |
| `reim-headless` 100 tick | PASS | tick=100，169 chunks，205 meshed sections，TPS≈20.0 |
| Python oracle vs Runtime 样本 | PASS | 12 checks，0 mismatch，6 samples |
| Preview 真实地形 | PASS | draw=36，tris=22526，avg FPS≈164，TPS≈20.0 |
| 截图非空 | PASS | ≈216 KB，采样 155 种颜色 |
| Preview 输入通路 | PASS | `--auto-walk 3` 前进 12.57 方块 |
| 缺失世界错误路径 | PASS | 明确错误，退出码 2 |
| 无效 DLL 错误路径 | PASS | LoadLibrary/ProcAddress 失败被报告 |
| 生成物 manifest | PASS | DLL create 接受 manifest |
| 10 分钟 soak | **未运行** | 默认跳过；用 `-SoakSeconds 600` 执行 |

快速稳定性冒烟（**60 秒**，非 10 分钟门禁）：

```bat
REIMPreview.exe --world <save> --generated generated\26.3 --hidden --seconds 60 --auto-walk 10
```

实测：60.01 s、1198 tick、TPS≈20.04、avg FPS≈164.9、182 chunks（含流式 halo）、无崩溃退出。  
报告：`build\m1\reports\s4-soak-60s.json`（这是 60 秒冒烟，**不是** 10 分钟门禁）。

截图：

- `build\m1\reports\s4-preview.png`（真实草地/积雪/树木/山体 + HUD）
- `build\m1\reports\s4-auto-walk.png`（前进后地形）
- `build\m1\reports\s4-third-person.png`（第三人称 + 绿色玩家 AABB）

---

## 3. ROADMAP M1 八条门禁状态

| # | 条目 | 状态 | 依据 |
|---|---|---|---|
| 1 | 真实 26.3 存档显示实际区块/地形 | PASS（自动） | Preview 报告 + 截图；169 初始区块、205 网格 section |
| 2 | 第一人称移动/转向/跳跃/基础碰撞 | PARTIAL | Runtime 输入与碰撞由 S3 集成测试覆盖；本会话 `--auto-walk` 经 Host→ABI 前进 12.57 方块；**鼠标手感/10 分钟人工操作待用户** |
| 3 | 无 `javaw.exe`/JVM/classpath | PASS | Preview 运行期间 `Get-Process javaw,java` 为 none；两个 Host 均无 classpath/JVM 启动路径 |
| 4 | Renderer 只读快照 | PASS（设计） | Preview 独立进程，仅调用 C ABI；世界/模拟在 DLL 线程 |
| 5 | 固定 20 TPS，FPS 独立波动 | PASS（自动） | TPS≈20.0，FPS≈164 |
| 6 | 连续 10 分钟无崩溃 | **未运行** | 由用户执行 `-SoakSeconds 600` |
| 7 | 区块方块数据与原始 NBT 一致 | PASS（自动） | Python oracle 独立实现，0 mismatch |
| 8 | 关键数据来自 Converter | PASS | DLL create 校验 `manifest.json`/profile/ABI；图集与方块表来自 `generated/26.3` |

**G1 结论：未形成。** 第 2 条人工部分与第 6 条 10 分钟 soak 尚未执行，不得宣称 G1 通过。

---

## 4. 手动验收清单（用户执行）

1. `REIMPreview.exe --world <save> --generated generated\26.3`
2. 点击窗口捕获鼠标；移动鼠标观察视角连续、无跳变；ESC 释放，再按 ESC 退出。
3. WASD 移动、Shift 潜行、Ctrl 冲刺、空格跳跃；确认不穿墙、不掉出世界。
4. F5 第三人称、F8 玩家 AABB、F1 HUD、F3 暂停/恢复；背靠墙/山坡时确认第三人称相机被拉近而不是穿进地形。
5. 不使用鼠标时，用 ↑↓←→ 确认视角以固定速率旋转。
6. 连续移动/观察 10 分钟；检查 `build\m1\bin\logs\preview.log` 的 stats 行与退出。
7. 跑完整门禁 soak：
6. 跑完整门禁 soak：

```bat
powershell -ExecutionPolicy Bypass -File tools\s4-acceptance.ps1 -SoakSeconds 600
```

---

## 5. 已知限制（M1-D08 等）

- 只渲染完整方块 + 水/透明方块；楼梯、半砖、栅栏等非完整模型跳过或缺失。
- 仅主世界、单一维度；只读存档，不写回。
- 图集使用最近邻采样；无 mipmap/AO/阴影/生物群系动态染色。
- 测试存档多数 section 无光照层（light=0），渲染使用方向光兜底，不代表最终光照。
- 第三人称相机只对碰撞方块回拉；水、草等非碰撞方块不会阻挡相机。
- 透明网格按 section 中心远到近排序并固定 0.72 alpha，没有面级排序，贴水面观察可能有混合伪影。
- 天空/雾色是固定常量，不随时间、天气或生物群系变化。
- Preview 是开发期临时 Host，不是 Launcher 正式 UI，也不是 UE 渲染。
- 截图为隐藏窗口离屏结果；交互手感必须在可见窗口人工确认。

---

## 6. 命令索引

```bat
cmake --preset m1-release
cmake --build --preset m1-release --parallel 8
build\m1\bin\m1-tests.exe

build\m1\bin\reim-headless.exe --headless --world <save> --generated generated\26.3 ^
  --ticks 100 --dump build\m1\reports\s4-headless.json

python tools\mc-oracle.py runtime --world <save> ^
  --report build\m1\reports\s4-headless.json

build\m1\bin\REIMPreview.exe --world <save> --generated generated\26.3 ^
  --render-distance 6 --minecraft-dir <minecraft_dir>

powershell -ExecutionPolicy Bypass -File tools\s4-smoke.ps1 -World <save>
powershell -ExecutionPolicy Bypass -File tools\s4-acceptance.ps1 -SoakSeconds 600
```
