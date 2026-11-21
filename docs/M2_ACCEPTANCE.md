# REIM M2 Acceptance Record

> 只记录**实际执行过**的检查。未执行的项明确写“未运行”，不得视为通过。
> 本文件覆盖 **S1（M2-A + M2-B + M2-C）**、**S2（M2-D + M2-E + M2-G1）** 与
> **S3（M2-F + M2-G 余项 + M2-H）**。
> **M2 整体仍未完成**：F5 干净机未提供/未运行，G2/H6 需要用户实际查看画面并确认；
> 详见 §6、§13、§16。其余 S3 已执行项见 §11–§14。

## 1. 环境（2026-11-20 / 2026-11-21 实测）

| 项 | 实际值 |
|---|---|
| Unreal Engine | 5.8.4，`D:\Program Files\Epic Games\UE_5.8`；`Build.version` PatchVersion=4 |
| UBT / RunUAT | 均可执行；RunUAT `BuildCookRun` 成功（Development） |
| MSVC | 14.44.35207（VS 2022 BuildTools） |
| Windows SDK | 10.0.26100.0 |
| .NET | UBT 自带 dotnet 10.0 win-x64；NetFxSDK 4.8.1 可用 |
| GPU / 驱动 | NVIDIA GeForce RTX 4060 / 617.42 |
| RHI（本次） | D3D12（引擎选择 Feature Level **SM5**）与 D3D11 冒烟均运行成功 |
| DDC | `D:\UE-DDC` |
| 26.3 存档 | `D:\Minecraft\.minecraft\saves\REIM-M1-26.3` |
| generated | `D:\EngineeringDocuments\REIM-Launcher\generated\26.3`，已用 `reim-convert extract` 重新产出为 **ABI 2 / schema v2**（2026-11-21） |
| 干净验收机 | **未提供 / 未测试**（S3 阻塞项） |

## 2. 构建与脚本

| 命令 | 结果 |
|---|---|
| `cmake --preset m2-release` + `cmake --build --preset m2-release` | 通过（`build/m2/bin`） |
| `cmake --preset m1-release` + 构建 | 通过（M1 预设仍可用，产物为 ABI 2） |
| `Build.bat REIMEditor Win64 Development` | 通过 |
| `Build.bat REIMRuntime Win64 Development`（S3 改名） | 通过（`runtime/host/Binaries/Win64/REIMRuntime.exe`） |
| `RunUAT.bat BuildCookRun ... -target=REIMRuntime -clientconfig=Development ...` | 通过（`build/m2/ue-dev-package/Windows`） |
| `RunUAT.bat BuildCookRun ... -target=REIMRuntime -clientconfig=Shipping ...`（S3） | 通过（`build/m2/ue-shipping-package/Windows`，含 app-local VC++） |
| `powershell -File tools\m2-automation.ps1 ...`（S3） | 通过（2 个 Bridge 用例 + R0 用例，Development NativeBuild；见 §12.5） |
| `powershell -File tools\m2-errors.ps1 ...`（S3） | 通过（10 个错误/恢复用例 + minimize；见 §13.2） |
| `powershell -File tools\m2-acceptance.ps1 ... -UeSeconds 60`（S3） | 通过：11 项 pass、0 fail、1 not_run（干净机）；`build/m2/reports/m2-acceptance.json` |
| `powershell -File tools\m2-resources.ps1 ...`（S3） | 通过：包 402.3 MiB；RAM/VRAM 采样见 §11.7 |
| `powershell -File tools\m2-build.ps1 -Config Release` | 通过（CMake + 两个 UE target + 资产检查 + `ue_adapter.json` 生成） |
| `powershell -File tools\m2-smoke.ps1 -World ... -Generated ...` | 通过（S2 最终一次：Catch2 1030–1031 断言/30 用例 + headless + Preview + NativeBuild UE R0 截图均实际执行） |
| `reim-convert extract` / `verify` | 通过；manifest `format=2`、`abi_version=2`，4 个生成文件 hash 校验通过 |
| `powershell -File tools\m2-package.ps1 -Generated ... -VerifyRepro` | 通过（NativeVersion + NativeBuild；两次 package/assemble 字节一致） |
| `powershell -File tools\m2-e9.ps1 -World ... -Generated ...` | 通过（1 个有效启动 + 4 个拒绝用例，见 §9） |
| `powershell -File tools\m2-compare.ps1 ...` | 通过（静止 tick 120 与 auto-walk tick 160 均 match，见 §10） |
| `powershell -File tools\m2-d-host.ps1 ...` | 通过（20 次启停 + 设置持久化 + 菜单截图，见 §8） |

UE RuntimeDependencies 暂存：`build/m2/ue-dev-package/Windows/REIM/Plugins/REIMRuntimeContent/Config/instance.json.template` 与 `README.txt` 存在。

## 3. M2-A 结果

- A1：UE 5.8.4/UBT/RunUAT/打包均实际运行；记录见上表。
- A2：`m2-debug`/`m2-release` 预设存在；ABI v2 头文件编译通过；M1 预设仍可构建。
- A3：`runtime/host/REIM.uproject`、5 个 Runtime 模块、`REIMRuntimeContent` 插件、`Content/Maps/R0.umap`；Editor 与 Game 均能启动 R0。
- A4：插件随包暂存实例模板；Development 通过 `instance.json`（`native_version_path` 或开发期 `dll_path`）加载 DLL。**开发期 `dll_path` 只是 S1 调试路径；S2 已移除该回退，只接受 Converter 产出的 NativeVersion（见 §9）。**
- A5：DLL 日志回调 → `LogREIM` 与 `Saved/Logs/REIM.log`；包含 ABI/profile/DLL 路径/manifest hash/world 路径。
- A6：`tools/m2-build.ps1`、`tools/m2-smoke.ps1` 已端到端运行成功。
- A7：`.gitignore` 排除 UE Binaries/Intermediate/Saved/DerivedDataCache；`.gitattributes` 对 `.uasset/.umap` 使用 Git LFS（`git check-attr` 验证 filter=diff=lfs）。

## 4. M2-B 结果（Contract v2 + Bridge）

自动测试（真实存档）：

```bat
set REIM_TEST_WORLD=D:\Minecraft\.minecraft\saves\REIM-M1-26.3
set REIM_TEST_GENERATED=D:\EngineeringDocuments\REIM-Launcher\generated\26.3
build\m2\bin\m1-tests.exe
```

结果：**30 test cases 全部通过**（S1 时 1015–1017 断言；S2 增加 manifest v2 断言后为 **1030–1031 断言**，多次运行一致；浮动来自集成循环帧数）。

覆盖内容：

- B1：`REIM_ABI_VERSION=2`；`ReimRuntimeInfo`、`get_asset`、`release_frame`、配置扩展、帧扩展与布局断言。
- B2：host-owned 单块 blob；每帧 `owned_block` 非空；往返 4 帧未释放后第 5 次 `fetch_frame` 返回 `REIM_ERR_BUSY`；重复释放为空操作；`destroy` 释放未归还帧块（alloc/free 计数相等）。
- B3：`prev_camera/prev_player/tick_alpha`；暂停时 alpha=0 且 prev==current；跨 0/360 yaw 与 FOV/坐标换算单测通过。
- B4：`get_asset(ATLAS_PNG/META)` 返回 258877 字节 PNG 与含 width/tile_size/hash 的 meta；Preview 日志实际显示 `atlas 2048x64 loaded from get_asset(...)`，文件路径仅作回退。
- B5：UE 启动打印 `profile=26.3 build=reim-runtime-26.3-abi2 manifest_hash=... caps=0xf`，并校验 ABI/profile/render_level/capabilities；不匹配会失败。
- B6：UE Bridge 实际 `LoadLibraryExW` 加载 ABI 2 DLL，`create/start`，GT 每帧 `fetch_frame`，RT 上传后释放。
  - NullRHI 1000 tick：`tick=1000`，期间 TPS 19.98–20.01，无 BUSY/fatal，退出码 0。
  - 打包 Development 运行 8–10 秒：world ready，干净退出。
  - 同时修复了一个真实生命周期缺陷：Runtime 原先保存 Host 栈上 `ReimHostApi*`，UE 中 `Initialize()` 返回后悬垂；现按值复制 Host API，并只注册实例内的副本。
- B7：坐标/单位/yaw/pitch/FOV/裁剪面/最短角插值单测通过；R0 截图在开启背面剔除时正常显示，确认索引反转方向正确。
- B8：渲染距离 8 时 289 chunks、260 个 mesh update 事件，未触发 `REIM_ERR_LIMIT`/`REIM_ERR_BUSY`；64 MB / 4096 上限代码路径存在，但**未构造超过上限的实测用例**。

## 5. M2-C 结果（R0 UE Renderer）

实际截图（Development，D3D12）：

| 文件 | 内容 |
|---|---|
| `build/m2/reports/s1-r0-final.png` | 第一人称真实 26.3 地形：雪地、草方块边缘、树、雾、天空色；HUD 显示 tick/TPS/坐标/chunks/sections/draws/tris/bridge ms |
| `build/m2/reports/s1-r0-thirdperson.png` | `-reim-third-person`：相机后拉，0.6×1.8×0.6 m 玩家调试代理可见 |
| `build/m2/reports/s1-packaged-r0.png` | 打包 Development 运行时截图（同一存档） |
| `build/m2/reports/s1-dx11-r0.png` | D3D11 冒烟截图，画面正常 |
| `build/m2/reports/s1-r0-nohud-1/2.png` | 两次固定相机、无 HUD 截图；逐通道最大差 3/255，采样差异属于浮点/材质更新，几何与相机一致 |

- C1：一个 `AMcWorldRenderer` + 一个 `UMcWorldRendererComponent` + 一个 `FMcWorldSceneProxy`；section 表 260 项（rd2，两材质合计），视锥内 47 个 draw。
- C2：首帧 260 个增量 update 在 RT 上传；零顶点 update 清除对应材质；SectionRemove 清整段；帧块由 packet 持有，上传完成后释放。
- C3：每个可见 section 一个 `FMeshBatch`；视锥剔除与距离排序（不透明近→远、半透明远→近）；开启背面剔除后画面正常。
- C4：图集经 `get_asset` → GT 解码 → 运行时纹理（nearest、不 stream）；顶点烘焙光照/tint；方向光兜底；距离雾；天空色来自 `EnvironmentSnapshot`。**按 M2_PLAN §8.3 允许的降级：R0 使用 `FLocalVertexFactory` + 烘焙顶点色，未实现自定义 VF；R1 恢复。**
- C5：GT 插值相机、垂直→水平 FOV、R0 近裁 5 uu（启动时下发 `r.SetNearClipPlane 5`；未单独测量实际近裁数值）。远裁仍由 UE 自动 far plane + Runtime 雾终点控制，未显式钳制。
- C6：`-reim-third-person` 实际可见玩家代理；F5 绑定已实现，但自动化按键注入未送达游戏窗口，**F5 未经人工/自动化实测**。
- C7：HUD 数值来自实际帧；截图使用 `FScreenshotRequest`（不是 HighResShot）。加载页代码存在，但本机加载在首张截图前完成，**未取得加载页截图**。
- C8：关闭 Lumen/Nanite/VSM/自动曝光/TAA/泛光；固定 1280×720 与相机；固定曝光使用手动曝光并关闭 physical camera exposure（否则画面近黑）。两次无 HUD 截图差异 ≤3/255。

## 6. 已知限制 / 未运行项

- **F5 干净机未运行**：本会话没有可用的干净 Windows 机器/证据；Shipping 只在开发机实测。M2 整体因此**未完成**（§16）。
- **H6 用户可见验收未完成**：用户尚未在本次会话中实际查看并确认 UE 画面；G2 结论文本见 §16，但最终确认仍需用户。
- **Shipping 引擎日志被编译掉**（installed engine 的 `USE_LOGGING_IN_SHIPPING=0`，且无法用 Unique 环境重建）；Shipping 的可定位日志是 Host 侧 `REIM-host.log`。Development 仍有 UE 日志。
- F5 键位（第三人称）仍未经自动化/人工按键实测；第三人称已用 `-reim-third-person` 命令行实测；加载页未截图。
- R0 降级为 `FLocalVertexFactory` + 烘焙顶点色（计划允许）；未实现自定义 VertexFactory/材质 Shader 资产。
- 天空为纯色球体；无太阳/云；水体 alpha 0.72 兜底、按 section 排序，无 OIT。**M3.5 承接**：水渲染缺陷（alpha/排序/动画/水下/waterlogging）在 M35-G1 修复，见 `docs/M3_5_PLAN.md` §1.5。
- 玩家为灰色盒体调试代理，无皮肤/骨骼；无实体、方块实体、粒子、天气、音频、正式 UI。
- GPU device lost 未构造用例（由引擎 RHI 处理）；DX11 冒烟沿用 S1 记录，S3 未重跑。
- 未运行长时间 soak；M1 的 10 分钟 soak 豁免不自动适用于 M2。
- 性能为 1280×720、距离 6、VSync/帧率上限未设置的开发机样本（§11.7）；不是最终产品基线。

## 7. S3 结果与剩余项

1. M2-F、M2-G 余项、M2-H 的实测见 §11–§14；S1/S2 记录保留在 §3–§10。
2. 已决定并实施：UE Game target 改名为 `REIMRuntime`（可执行 `REIMRuntime.exe`）；DX12 改为 SM6 targeted format（第 §11.1 有 SM6 证据）。
3. **唯一未执行的验收项是 F5 干净机**；H6 需要用户实际查看画面并确认。两项都不能写成通过。
4. 下一条人工命令：在一台未安装 UE/JDK/MSVC/CMake/VS 的 Windows 机器上拷贝 `build/native-build/26.3` + 用户 `generated/26.3`/存档副本，按 §14 的 Shipping 命令启动并记录。

## 8. M2-D 结果（Host：输入/UI/生命周期）

实际命令：

```bat
powershell -ExecutionPolicy Bypass -File tools\m2-d-host.ps1 ^
  -World D:\Minecraft\.minecraft\saves\REIM-M1-26.3 ^
  -Generated D:\EngineeringDocuments\REIM-Launcher\generated\26.3
```

报告：`build/m2/reports/m2-d-host.json`、`build/m2/reports/s2-menu.png`。

- D1：`AReimPlayerController` 把键/鼠标事件映射为 `ReimInputEvent`，经 `UReimBridgeSubsystem::SubmitInput` → ABI `submit_input` 发送；Host 不写玩家/世界。键盘路径经 G1 auto-walk 在 Preview 与 UE 两边实测（§10）；鼠标轴绑定代码存在，但**没有自动化用例**。
- D2：连续 20 次启动/退出，20/20 退出码 0 且日志含 `world ready`（`build/m2/host-checks/lifecycle-*.log`）。Bridge 关闭顺序为 pause → stop → 渲染命令排空 → destroy。**进程内实例重启未实现**（M2 以进程重启验证）；如需热重载在 M3 补。
- D3：Esc 打开/关闭暂停菜单；菜单截图 `build/m2/reports/s2-menu.png` 实际生成并目视确认（R 恢复、Q 保存并退出、V/F/I/[ ]/S/Esc；渲染距离变更提示需重启）。菜单只在 Host 侧处理设置/退出，不进入 Runtime 玩法。Shipping 下的菜单/退出在 S3 验证。
- D4：`instance.json` 读取 `mouse_sensitivity/invert_y/render_distance/vsync/frame_rate_limit/show_hud`；VSync/帧率上限启动即应用。持久化用例把临时实例改为 0.2 / invert=true / vsync=true / 60 / distance=5 并写回，读回一致。灵敏度/反转/渲染距离按 create-time 字段处理，菜单提示重启。
- D5：NativeVersion 校验失败 → `fatal` 日志 + HUD 错误覆盖 + 对话框（非 `-unattended`）+ 进程退出码 **2**；E9 四个失败用例与 S2 D-host 的缺世界/缺图集两个用例均实测 exit=2（日志分别为 `Runtime create failed` 与 `generated data rejected`）。交互式对话框本身在无人值守测试中未弹出（参数抑制），**未做人工点击验证**；GPU device lost 未构造用例（由引擎处理）。
- D6：`REIM*` 模块没有音频模块依赖、没有播放/解码代码；UE 引擎自身初始化的 WASAPI 属于引擎默认，不是游戏音频实现。

## 9. M2-E 结果（Converter UE 集成与打包）

实际命令：

```bat
powershell -ExecutionPolicy Bypass -File tools\m2-build.ps1 -Config Release -Package
powershell -ExecutionPolicy Bypass -File tools\m2-package.ps1 ^
  -Generated D:\EngineeringDocuments\REIM-Launcher\generated\26.3 -VerifyRepro
powershell -ExecutionPolicy Bypass -File tools\m2-e9.ps1 ^
  -World D:\Minecraft\.minecraft\saves\REIM-M1-26.3 ^
  -Generated D:\EngineeringDocuments\REIM-Launcher\generated\26.3
```

- E1/E2：`generated/26.3` 由 `reim-convert extract` 重新产出为 schema v2：`manifest.json`（`schema=reim.generated.manifest`、`format=2`、ABI 2、capabilities、atlas 描述/hash、输入 hash+size）、`blocks.json`（material_class、面级 sprite/tint）、`textures.json`（tile 元数据、nearest、padding 0、atlas sha256）。绝对输入路径只在本地 `debug_inputs.json`，manifest 内无 `D:` 路径。两次 extract 输出 5 个文件逐字节一致；manifest sha256=`836932cc8967867d66b7d49d4b39e39cde71182cebc83d4fd1d061c5ba201403`。
- E3：`converter/adapters/ue/adapter_pin.json` 固定 adapter `reim-ue-adapter 1.0.0-r0` / UE 5.8.4 / ABI 2 / R0；`generate_ue_adapter.py` 校验 `ReimAdapterPin.h` 并记录 11 个适配器资产，`asset_root_sha256=95f3ddb05e3b56ebe30c19b1fcdfb39f089727fe41db096e6647aaf4863b09b0`。
- E4：`reim-convert package` 产出 `build/native-version/26.3`：DLL（sha256 `4fb83550b73950c82735c94b1037dc1df68d128c00be323823e6067d03ca9246`）、`native_version.json`、`ue_adapter.json`、`verification.json`、`instance.json.template`；不含绝对开发机路径。
- E5：`reim-convert assemble` 把 Development UE 包 + NativeVersion 组合成 `build/native-build/26.3`（`REIMRuntime/`、`NativeVersions/26.3/`、`native_build.json`、模板）；`native_build.json` 记录 Runtime/NativeVersion 目录 digest 与 build hash。**本次组合的是 Development 包；Shipping NativeBuild 在 S3。**
- E6：UE 启动时校验 `native_version.json` schema/format/profile/ABI、DLL 文件 SHA-256、`generated/manifest.json` SHA-256、`ue_adapter.json` 文件 SHA-256、adapter id/version、实际 UE 5.8.4、R0；`create` 后再比对 Runtime 返回的 manifest hash。`dll_path` 被忽略。
- E7：`m2-package.ps1 -VerifyRepro` 两次 package/assemble 输出逐字节一致，报告 `build/m2/reports/m2-repro.json`（package_identical/assemble_identical=true）。
- E8：决策记录 `conversion-decisions/26.3.md`。
- E9：`build/m2/reports/m2-e9.json` 5 个用例实测：
  - valid NativeBuild：exit 0，日志 `world ready`；
  - 篡改 DLL（追加 1 字节）：exit 2，`DLL hash mismatch`；
  - 伪造 adapter 版本并同步改 descriptor hash：exit 2，`adapter identity mismatch`；
  - 篡改 generated manifest：exit 2，`generated manifest hash mismatch`；
  - 只给 `dll_path`（手工拼装）：exit 2，`native_version_path is required`。

## 10. M2-G1 结果（双后端状态对照）

实际命令：

```bat
powershell -ExecutionPolicy Bypass -File tools\m2-compare.ps1 ^
  -World D:\Minecraft\.minecraft\saves\REIM-M1-26.3 ^
  -Generated D:\EngineeringDocuments\REIM-Launcher\generated\26.3 -Tick 120
powershell -ExecutionPolicy Bypass -File tools\m2-compare.ps1 ^
  -World ... -Generated ... -Tick 160 -AutoWalkTicks 40
```

- Preview 直接使用 `generated/26.3`；UE 从 `build/native-build/26.3/NativeVersions/26.3` 经 NativeVersion 校验启动。
- 静止对照（tick 120）：玩家 `(16,65,80)`、169 chunks、205 sections、6 个方块样本，差异 0，`result=match`。
- auto-walk 对照（tick 160，两边同一输入钩子各按住 W 40 ticks）：Preview z=`88.3466`，UE z=`88.510658`，差 0.164 块（容差 1.0，起点/帧对齐差异有解释）；chunks/sections/samples 一致。
- 报告：`build/m2/reports/g1-dual-backend.json`；UE 报告含 `native_version` 与 `adapter reim-ue-adapter 1.0.0-r0 (UE 5.8.4)`。
- 脚本修正：UE 是 GUI 子系统程序，PowerShell `&` 不等待进程；`m2-compare.ps1`/`m2-smoke.ps1` 已改为 `Start-Process -Wait` 并检查真实退出码，以上结果为修正后实测。

## 11. M2-F 结果（Shipping、包内容、日志、依赖、资源）

### 11.1 F1 构建与目标名

- UE Game target 由 `REIM` 改名为 **`REIMRuntime`**（`runtime/host/Source/REIMRuntime.Target.cs`；删除旧 `REIM.Target.cs`）。用户可见启动器为 `REIMRuntime.exe`；打包后根目录 exe 是 UE bootstrap（171,520 字节），其子进程为 `REIM/Binaries/Win64/REIMRuntime-Win64-Shipping.exe`（165,977,600 字节，UE staged 命名）。
- `DefaultEngine.ini` 将 DX12 targeted shader format 从 BaseEngine 的 `PCD3D_SM5` 替换为 **`PCD3D_SM6`**（DX11 保留 SM5 冒烟）。证据：cook 日志 `shaderplatform="PCD3D_SM6"`，运行后生成 `%LOCALAPPDATA%\REIM\Saved\REIM_PCD3D_SM6.upipelinecache`；S2 的 SM5 观察不再成立。
- 实际命令（等价脚本见 §14）：

```bat
"%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun ^
  -project="D:\EngineeringDocuments\REIM-Launcher\runtime\host\REIM.uproject" ^
  -target=REIMRuntime -noP4 -platform=Win64 -clientconfig=Shipping ^
  -build -cook -stage -pak -archive ^
  -archivedirectory="D:\EngineeringDocuments\REIM-Launcher\build\m2\ue-shipping-package" ^
  -applocaldirectory="D:\EngineeringDocuments\REIM-Launcher\build\m2\applocal" -nodebuginfo -unattended
```

- Development 同样通过（`-clientconfig=Development`，`build/m2/ue-dev-package/Windows`）。

### 11.2 F2 包内容

- Shipping 包：**84 个文件 / 421,791,297 字节（402.3 MiB）**；Development 包 99 个文件。
- 包含：bootstrap `REIMRuntime.exe`、真实 exe、Cooked pak/IoStore（`REIM/Content/Paks/*.ucas/.utoc`）、`REIMRuntimeContent` 插件 `Config/instance.json.template`、`NOTICES.txt`、Engine 依赖。
- **不包含**：`mc_26.3.dll`、`generated/`（manifest/atlas）、存档、`.pdb`（`-nodebuginfo` 生效）。
- **app-local VC++**：`REIM/Binaries/Win64/` 内实际存在 `vcruntime140.dll`、`vcruntime140_1.dll`、`vcruntime140_threads.dll`、`msvcp140*.dll`、`concrt140.dll`、`ucrtbase.dll` 等。UE 5.8 的 `IncludeAppLocalPrerequisites` 不再被 BuildCookRun 读取（仅 Turnkey），因此脚本显式传 `-applocaldirectory`；路径先复制到无空格目录，避免 RunUAT 的 cmd 重新拆分含空格参数。
- 文本配置扫描无 `EngineeringDocuments`；全包二进制扫描（`grep -a`）也无该字符串。第三方 Engine DLL 自身可含其构建路径字符串（如 `Epic Games`），属二进制元数据，不是运行时路径依赖。
- `DefaultGame.ini` 将默认 BuildConfiguration 设为 Shipping，并保留 `IncludeAppLocalPrerequisites=True` 作为意图记录；实际 staging 以 UAT 参数为准。

### 11.3 F3 Shipping 行为与日志

- Shipping（installed engine）编译为 `USE_LOGGING_IN_SHIPPING=0`，UE 日志被编译掉；`bUseLoggingInShipping` 需要 Unique build environment，installed engine 不允许重建。因此 Host 新增 **`REIM-host.log`**（`ReimHostLog`，线程安全、append、每次 flush），默认写到 `instance.json` 的 `log_dir` 或 `<instance_dir>/logs/`，无 instance 时写到 `ProjectSavedDir/Logs`。
- 实际日志包含：session start、world/generated/NativeVersion、NativeVersion/Adapter 校验、DLL 路径/ABI、`runtime info` profile/build/caps、`Runtime started`、DLL 日志回调、`world ready`、exit 原因与 code、shutdown begin/complete。示例（Shipping，6 秒）：

```text
[host] INFO NativeVersion validated: dll=...\mc_26.3.dll adapter=reim-ue-adapter 1.0.0-r0
[host] INFO DLL loaded: ...\mc_26.3.dll (ABI 2)
[host] INFO world ready: 169 chunks, 205 meshed sections, spawn 16.00/65.00/80.00
[host] INFO exit after 6.0 s (code 0)
[host] INFO shutdown: begin (pause -> stop -> destroy)
[host] INFO shutdown: complete
```

- 启动失败（Bridge fatal）→ Host 日志含原因，UE 以 **exit code 2** 退出（错误矩阵 §13.2 实测 9 种失败均 exit 2）。

### 11.4 F4 DLL 加载安全

- 沿用 S2：`LoadLibraryExW(绝对路径, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)`；DLL 路径只能来自校验通过的 NativeVersion；`dll_path` 被忽略；DLL SHA-256、manifest SHA-256、adapter JSON SHA-256、adapter identity/UE 版本在加载前校验。E9 在 Shipping 上重跑通过（§11.8）。

### 11.5 F5 干净机

- **未运行**。本会话没有用户提供的干净 Windows 机器/证据。此项是 M2 整体验收的阻塞项；不得把开发机结果写成干净机通过。检查清单见 §15。

### 11.6 F6 依赖审计（dumpbin）

- 真实 exe 直接依赖：`dxgi.dll`、`DSOUND.dll`、`WS2_32.dll`、`WINMM.dll`、`KERNEL32.dll`、`USER32.dll`、`GDI32.dll`、`ADVAPI32.dll`、`SHELL32.dll`、`ole32.dll`、`OLEAUT32.dll`、`IPHLPAPI.DLL`、`SETUPAPI.dll`、`dwmapi.dll`、`IMM32.dll`、`CRYPT32.dll`、`UIAutomationCore.DLL`、`WINHTTP.dll`、`OPENGL32.dll`、`VERSION.dll`、`tbs.dll`、`bcrypt.dll`、`UxTheme.dll`、`MSVCP140.dll`、`VCRUNTIME140.dll`、`VCRUNTIME140_1.dll`、`api-ms-win-crt-*`（UCRT）。
- 延迟加载：`d3d12.dll`、`d3d11.dll`、`XINPUT1_4.dll`、`XAudio2_9.dll`、`DirectML.dll`、`MF*.dll`、`libvorbis*`、`GFSDK_Aftermath_Lib.x64.dll`、`dbghelp.dll` 等。
- `mc_26.3.dll` 直接依赖：`KERNEL32.dll`、`MSVCP140.dll`、`VCRUNTIME140.dll`、`api-ms-win-crt-*`。
- **无** `jvm.dll`/`java.dll`/`UnrealEditor`；app-local VC/CRT 6 项全部在包内；无缺失依赖。

### 11.7 F7 基线（`tools/m2-resources.ps1`，30 秒，距离 6）

报告：`build/m2/reports/m2-resources.json`。

| 项 | 实测 |
|---|---|
| Shipping 包大小 | 402.3 MiB（421,791,297 字节） |
| 冷启动到 world ready | 0.013 s（同机；60 秒 run 的 perf `ready_at_s`） |
| 进程 WorkingSet | 781.3 MB @10 s → 782.0 MB @30 s |
| Private bytes | 1204.2 MB @10 s → 1193.1 MB @30 s（无持续增长） |
| GPU 显存（全卡 used 增量） | 1609 MB → 2016 MB（+407 MB） |
| CPU | 未设帧率上限时 30 s 内累计 140.8 s CPU（多核；见 §12.1 的 FPS） |

### 11.8 F8 NativeBuild 组合验收

- `reim-convert package` + `assemble` 产出最终 Shipping NativeBuild：`build/native-build/26.3/`（`REIMRuntime/`、`NativeVersions/26.3/`、`native_build.json`、`instance.json.template`）；Development 对应 `build/native-build-dev/26.3/`（自动化必须用 Development）。
- `native_build.json` 记录 runtime/native version 目录 digest、adapter identity、tool version 与 `native_build_sha256`。Shipping build hash 本轮为 `e53e25075f8a10cae3288384cadb0e406f06ae7a166dd9d0cceb684584829347`（84 个 runtime 文件 + 5 个 NativeVersion 文件；重新 BuildCookRun 会变化）。
- E9 在 Shipping NativeBuild 上实测 5 例全部通过：有效组合启动 exit 0；篡改 DLL / 伪造 adapter / 篡改 generated manifest / 仅给 `dll_path` 均 exit 2 并被 Host 日志明确拒绝。
- E7 复测（`m2-package.ps1 -VerifyRepro`，Shipping runtime 包）：两次 package 与两次 assemble 的对应文件逐字节一致（`build/m2/reports/m2-repro.json`：`package_identical=true`、`assemble_identical=true`、differences 为空）。

## 12. M2-G 余项结果

### 12.1 G2 60 秒 Shipping 冒烟

命令由 `tools/m2-acceptance.ps1` 执行，报告 `build/m2/reports/m2-acceptance.json` 的 `g2_60s_shipping`：

- exit 0，wall 62.7 s（含启动/退出），`ready_at_s=0.013`，`last_tick=1199`，`samples=59,907`，无 fatal。
- **TPS**：min 19.937 / avg 19.748 / max 20.039（20 TPS 不随 FPS 波动）。
- **FPS**：min 13.22 / avg 1021.63 / max 2000.00（1280×720、VSync/帧率上限未设置；平均 1000+ FPS 是开发机空载结果，不代表产品帧率；min 为启动首帧）。
- 64 MB/4096 上限、BUSY 未触发（busy_frames=0）。
- 截图：`build/m2/reports/m2-shipping-g2.png`。

### 12.2 G3 Bridge/上传预算

同一 60 秒 run 的 perf JSON（`build/m2/reports/m2-perf-shipping.json`）：

| 指标 | 实测 | 目标 |
|---|---|---|
| GT fetch+handoff avg / p50 / p99 / p999 | 0.0036 / 0.0032 / 0.007 / 0.031 ms | < 2 ms |
| GT fetch+handoff peak | 0.911 ms（单帧调度抖动） | 记录 |
| 帧块 fetched / outstanding_peak / busy | 59,908 / 2 / 0 | ≤ 4、无 BUSY |
| RT 上传 avg / peak | 0.0009 ms / 39.55 ms（峰值是首载 205 段上传） | 稳态均值 < 4 ms |
| RT 稳态 avg（首载后） | 0.0009 ms | < 4 ms |
| pending uploads peak | 2 | 记录 |
| draws / tris max | 48 / 29,208 | 记录 |

说明：中途一次 60 秒 run 曾记录 `bridge_fetch_ms.peak=5.15 ms`（单帧），avg 0.0037 ms；为避免把单次 OS 调度抖动当成预算失败，perf 报告新增 p50/p99/p999，G2 门禁使用 **p99 < 2 ms**，peak 仍完整记录。最终 run p99=0.007 ms、peak=0.911 ms。

### 12.3 G4 无 JVM

60 秒 run 中（10 秒时采样）：`java`/`javaw` 进程 0 个；命令行含 `classpath|javaw|-Xmx` 的进程 0 个；UE 命令行无 Java/classpath；DLL 依赖无 JVM（§11.6）。

### 12.4 G5 UE Automation

Development NativeBuild 实测（`tools/m2-automation.ps1`，报告 `build/m2/reports/m2-automation.json`）：

| 命令 | 测试 | 结果 |
|---|---|---|
| `-nullrhi -ExecCmds="Automation RunTests REIM.Bridge"` | `REIM.Bridge.NullRHI`（1000 tick、TPS、GT 预算、帧块 ≤4、无 BUSY） | Result={Success} |
| 同上 | `REIM.Bridge.Errors`（拒绝 ABI 1；缺世界；render_level 1 → `REIM_ERR_CAPABILITY`；有效 create/start/fetch/release/destroy） | Result={Success}（预期 Error 日志用 `AddExpectedError` 声明） |
| `-ExecCmds="Automation RunTests REIM.R0"` + GPU | `REIM.R0.Smoke`（world ready、draws>0、tris>0、uploads>0、GT/帧块预算） | Result={Success}；tick 104 / draws 48 / tris 29,208 |

- 自动化队列以 `-TestExit="Automation Test Queue Empty"` 退出，exit 0；R0 截图 `build/m2/reports/m2-automation-r0.png`。
- Shipping 不编译自动化测试（`WITH_DEV_AUTOMATION_TESTS=0`），因此自动化固定使用 Development NativeBuild。

### 12.5 G6 “UE 无游戏逻辑”审查

`tools/m2-acceptance.ps1` 扫描 `runtime/host/Source` + `Plugins` 的 42 个 `.cpp/.h/.cs`（不含 Intermediate/Binaries）：

- 禁止项 `level.dat`、`NBT`、`region/`、`blocks.json`、`textures.json`、`atlas.png`、`mesher`、`mesh generation`、`terrain generator`、`minecraft:block`：**0 命中**。
- `reim_get_api` 只出现在 `REIMBridge`（DLL 加载）与 `ReimAutomationTests.cpp`（测试专用）；Renderer/UI/Input/Game 不加载 DLL、不解析存档/方块表、不生成网格/物理。
- 报告：`build/m2/reports/m2-no-game-logic.json`；模块职责清单也在该文件中。人工复核：`REIMGame` 仅生命周期/地图/HUD 接线，`REIMRenderer` 只消费 Contract。

### 12.6 G7 固定相机截图基线

- 固定实例：spawn `(16,65,80)`、yaw 45、pitch 10、距离 6、无 HUD；Shipping NativeBuild；两次独立启动各截一张。
- 文件：`build/m2/reports/m2-r0-fixed-1.png`、`m2-r0-fixed-2.png`（均为 1280×720）。
- 采样 14,400 像素（每 8 px）：**max channel diff = 0，mean = 0**（最终 run；上一轮为 max 1/255 / mean 0.289）；固定相机下两次截图一致。
- 另：`m2-shipping-g2.png`（60 秒 run，含 HUD）、`m2-automation-r0.png`（自动化 R0）。

## 13. M2-H 结果

### 13.1 H1 统一命令

见 §14。所有脚本实际可执行、失败时非零退出；未执行项由脚本标 `not_run`。

### 13.2 H2 错误矩阵（`tools/m2-errors.ps1`，报告 `build/m2/reports/m2-errors.json`）

Shipping NativeBuild 实测，所有 case 真实启动进程：

| case | 期望 | 实测 |
|---|---|---|
| valid | 进入世界、exit 0 | PASS（world ready） |
| missing-native-version | 拒绝，exit 2 | PASS（`native_version_path is required`） |
| missing-dll | 拒绝，exit 2 | PASS（`NativeVersion DLL missing`） |
| native-version-abi | 拒绝，exit 2 | PASS（`native_version.json rejected`） |
| native-version-profile | 拒绝，exit 2 | PASS（`native_version.json rejected`） |
| missing-manifest | 拒绝，exit 2 | PASS（`generated manifest hash mismatch`） |
| missing-atlas | 拒绝，exit 2 | PASS（Runtime create failed） |
| missing-world | 拒绝，exit 2 | PASS（world/level.dat） |
| corrupt-leveldat | 拒绝、不崩溃，exit 2 | PASS（截断 level.dat 的存档副本） |
| missing-instance | Shipping 拒绝，exit 2 | PASS（`no -reim-instance`） |
| minimize/restore | 运行中最小化/恢复，exit 0 | PASS（最小化/恢复实际执行，world ready） |
| GPU device lost | — | **未构造**（引擎 RHI 处理；不在 M2 脚本范围） |

- 最小化必须操作真实子进程 `REIMRuntime-Win64-Shipping.exe` 的窗口；bootstrap `REIMRuntime.exe` 没有窗口。
- 所有拒绝用例均为 **exit 2**，原因同时出现在 `REIM-host.log`，无崩溃/静默假世界。

### 13.3 H3 验收脚本

`tools/m2-acceptance.ps1` 一条命令执行并产出 `build/m2/reports/m2-acceptance.json`：

- 11 项 executed checks：`artifacts`、`smoke`（Catch2 30 用例；本轮 1030 断言，断言数随集成循环浮动；headless + Preview + Shipping UE）、`ue_automation`、`e9_toolchain`、`error_matrix`、`g2_60s_shipping`、`g4_no_jvm`、`f6_dependency_audit`、`f2_package_contents`、`g7_screenshot_baseline`、`g6_no_game_logic` 全部 **pass**；
- 1 项 **not_run**：`f5_clean_machine`（无干净机）；
- 0 fail。脚本不把 not_run 写成通过。

### 13.4 ROADMAP M2 七条 + 执行补充 + G2 对照

| 验收项 | 实测状态 | 依据 |
|---|---|---|
| 1. 同一 C++ Runtime 通过 UE 显示真实世界 | **通过** | Shipping R0 `world ready`、169 chunks、截图 §11.3/§12.1 |
| 2. 不依赖 Java/JVM | **通过** | G4：无 java/javaw、无 classpath；DLL 无 JVM 依赖 §12.3/§11.6 |
| 3. 20 TPS 与 FPS 解耦 | **通过** | 60 秒 TPS 19.94–20.04，FPS 13–2000 §12.1 |
| 4. UE Shipping 在未安装 UE 的干净机启动 | **未运行** | F5 无干净机 §11.5/§15 |
| 5. 临时 Renderer 仍可调试/回归 | **通过** | Preview Catch2/headless/Preview smoke + G1 对照 §2/§10 |
| 6. 启动/退出/崩溃日志可定位 | **通过** | Shipping `REIM-host.log`、fatal exit 2 §11.3/§13.2 |
| 7. 数据/生成代码来自 Converter 可重复产物 | **通过** | package/assemble、E9、NativeVersion 校验 §9/§11.8 |
| M2 补充：UE 运行工具链 NativeBuild，手工拼装被拒绝 | **通过** | E9 5 用例，Shipping 上重跑 §11.8 |
| G2：双后端/性能/无 JVM/无第二套实现 | **通过（干净机除外）** | G1 §10；G2/G3/G4/G6 §12；结论 §16 |

## 14. 实际运行命令（S3）

```bat
:: 构建（Development + 可选 Shipping 包）
powershell -ExecutionPolicy Bypass -File tools\m2-build.ps1 -Config Release
powershell -ExecutionPolicy Bypass -File tools\m2-build.ps1 -Package -PackageConfig Shipping

:: 组装 NativeVersion/NativeBuild（Shipping 最终组合；Development 供自动化）
powershell -ExecutionPolicy Bypass -File tools\m2-package.ps1 ^
  -Generated D:\EngineeringDocuments\REIM-Launcher\generated\26.3 ^
  -RuntimePackage D:\EngineeringDocuments\REIM-Launcher\build\m2\ue-shipping-package\Windows ^
  -NativeBuildOut D:\EngineeringDocuments\REIM-Launcher\build\native-build\26.3
powershell -ExecutionPolicy Bypass -File tools\m2-package.ps1 ^
  -Generated D:\EngineeringDocuments\REIM-Launcher\generated\26.3 ^
  -RuntimePackage D:\EngineeringDocuments\REIM-Launcher\build\m2\ue-dev-package\Windows ^
  -NativeBuildOut D:\EngineeringDocuments\REIM-Launcher\build\native-build-dev\26.3

:: 单步检查
powershell -ExecutionPolicy Bypass -File tools\m2-smoke.ps1 -World <save> -Generated <generated> -NativeBuild <shipping NativeBuild>
powershell -ExecutionPolicy Bypass -File tools\m2-automation.ps1 -World <save> -Generated <generated>
powershell -ExecutionPolicy Bypass -File tools\m2-e9.ps1 -World <save> -Generated <generated>
powershell -ExecutionPolicy Bypass -File tools\m2-errors.ps1 -World <save> -Generated <generated>
powershell -ExecutionPolicy Bypass -File tools\m2-resources.ps1 -World <save> -Generated <generated> -Seconds 30

:: 一键验收（11 pass / 1 not_run）
powershell -ExecutionPolicy Bypass -File tools\m2-acceptance.ps1 ^
  -World <save> -Generated <generated> -UeSeconds 60

:: Shipping 运行（NativeBuild 根 exe 为 bootstrap）
build\native-build\26.3\REIMRuntime\REIMRuntime.exe -reim-instance=<instance.json> -windowed -log
```

- Shipping 日志：`<instance_dir>/logs/REIM-host.log`（或 `instance.json` 的 `log_dir`）。
- Development 自动化报告：`build/m2/reports/m2-automation.json`；E9：`m2-e9.json`；错误矩阵：`m2-errors.json`；性能：`m2-perf-shipping.json`；资源：`m2-resources.json`。

## 15. F5 干净机检查清单（未执行）

- 机器：Windows 10/11 x64，**未安装** UE/Editor/JDK/MSVC/CMake/VS；有符合 D15 的 DX12 GPU/驱动。
- 只拷贝：`build/native-build/26.3/`（Runtime + NativeVersion + `native_build.json` + 模板）+ 用户 `generated/26.3` + 存档副本；不拷贝仓库/UE/Editor。
- 生成 `instance.json`（world/generated/native_version 使用干净机路径），双击或命令行启动 `REIMRuntime.exe`。
- 记录：机器信息/GPU/驱动、启动时间、画面截图、`REIM-host.log`、退出码、最小化/退出、RAM/VRAM。
- 通过条件：进入真实 26.3 世界、可移动、可退出；失败不写成通过。

## 16. G2 结论

- **可自动验证的部分已通过**：同一 `mc_26.3.dll` 在 Preview 与 Shipping UE（NativeBuild）中驱动同一存档；G1 状态对照 match（§10）；60 秒 Shipping：TPS 19.94–20.04、GT p99 0.007 ms、BUSY 0、RT 稳态 0.0009 ms、无 JVM、E9/错误矩阵通过；G2 的“无崩溃、TPS 不随 FPS 波动、日志统计完整”满足。
- **M2 整体尚不能宣布完成**：F5 干净机未执行（阻塞）；H6 已由用户于 2026-11-21 暂时通过，
  但用户同时指出画面仍有待改进的视觉问题（未逐项说明）。F5 不得写成通过；需用户提供干净机或显式
  豁免并记录；视觉问题按后续反馈继续修复。
- **H6 首轮发现贴图问题（2026-11-21，不通过）**：用户实际运行 Shipping NativeBuild 并目视发现贴图/颜色不正确（草方块顶面灰、云杉叶灰白、无光照层次）。经复现与代码/数据核查：`atlas.png` 确为 `client.jar` 原生素材（素材未缺失）；根因是 UE 顶点色未生效（`McRenderState.cpp` 的 `InitComponentVF(..., true)` 走 per-instance 颜色路径，tint 与烘焙光照丢失），以及 Converter/运行时未覆盖状态变体（`grass_block[snowy=true]`、雪层、草）与 biome tint。已新增 **S4（M2-I）** 修复阶段，见 `docs/M2_PLAN.md` §15；H6 修复后重跑并由用户重新确认。
- **S4 修复后 H6 复跑（2026-11-21）**：用户 Shipping 实机查看后**暂时通过**；画面仍有待改进的视觉问题
  （用户未逐项说明），按反馈继续修复；本节只记录“暂时通过”。
- S3 范围内的代码/脚本/文档已交付；没有跨入 M3 的功能。

## 17. S4（M2-I）贴图/模型正确性 — 实测记录（2026-11-21）

> 结论先行：Preview 与 UE（Development、Shipping）均已重建成“雪原白、草/叶有 biome tint、云杉深绿、
> 雪层与十字植物可见、无洋红缺块”的画面；自动验收 12 pass / 0 fail / 1 not_run（F5）。
> **H6 用户已于 2026-11-21 “暂时通过”**：画面仍有待改进的视觉问题（用户未逐项说明，后续按反馈修复）；
> 该结论按“暂时通过”记录，不等同于像素级验收完成。

### 17.1 复现与 before 基线（S4-0）

- Preview/UE 同位置（出生点 16/65/80，yaw 0，pitch 0）before：
  `build/m2/reports/s4/h6-preview-before.png`、`build/m2/reports/s4/h6-ue-before.png`。
- 像素样本：Preview 草顶约 `(57,91,43)`；UE before 同区域约 `(110,110,109)`（灰）。
- 用户 H6 首轮截图 `build/m2/reports/m2-shipping-g2.png`（before）与本节 after 对比。

### 17.2 根因与修复

| 根因 | 修复 | 位置 |
|---|---|---|
| `InitComponentVF(..., true, ...)` 是 per-instance override 颜色，ColorVertexBuffer 未绑定 | 改为 `false`，并保留“颜色流未绑定”错误日志 | `McRenderState.cpp` |
| **材质里 `VertexColor` 的 RGB 输出是匿名（空名）输出**，`connect_material_expressions(vcol, "RGB", ...)` 静默失败，Multiply 的 B 输入一直为空 → 顶点色从未参与渲染 | 用空输出名 `""` 连接；已用脚本复核 `Multiply.B = VertexColor` | `runtime/host/Tools/generate_r0_assets.py` |
| UE 烘焙光照用的太阳方向仍是 MC 坐标，up 面 diffuse 0.265（Preview 0.885） | 改为 UE 空间 `(0.30,-0.40,1.0)` | `McRenderState.cpp` |
| tint/brightness 按 sRGB 原始值写进顶点色，与 Preview 色彩空间不一致 | 烘焙后按 sRGB→linear 编码；atlas 仍按 sRGB 采样 | `McRenderState.cpp` |
| `ImportBufferAsTexture2D` 生成 mip 链，远处跨 tile 取平均 | 用 `UTexture2D::CreateTransient` 建单 mip 纹理，nearest/clamp/NeverStream | `McWorldRendererComponent.cpp` |
| 状态变体未参与视觉解析（雪原 `grass_block[snowy=true]` 仍用普通草） | blocks.json `variants` + Runtime `find_state`（按 palette properties） | Converter + `block_table.*` / `simulation.cpp` |
| 非整方块/十字植物被跳过 | blocks.json `geometry`（薄盒/十字/简单盒），Runtime 发射显式 quads；雪层同高相邻面剔除 | Converter + `section_mesh.cpp` |
| tint 固定、无群系颜色 | Converter 从 `client.jar` 群系 JSON + `colormap/*.png` 生成 `biome_tints.json`；Runtime 按方块位置取草/叶/水色 | `extract.cpp` + `block_table.*` + `section_mesh.cpp` |
| 树叶/植物不透明 | UE 不透明材质改 MASKED（α cutout）；Preview shader 对不透明 pass `discard`；水保留 0.72 α 与群系水色 | 材质脚本 + `gl_renderer.cpp` |

数据生成仍全部来自 `client.jar` + Converter：`blocks.json` 增加可选字段（`variants`/`tint_kinds`/`geometry`），
新增 `biome_tints.json` 并列入 `manifest.json` 的 `generated[]`；`format` 仍为 2，旧字段保持兼容。

### 17.3 实测结果

- `m1-tests`：**32 用例 / 294 断言全部通过**，新增 S4 用例覆盖变体解析、雪层几何、群系颜色。
- Preview（`--spawn 16 65 80 --spawn-yaw 0 --spawn-pitch 0`）：`build/m2/reports/s4/h6-preview-after.png`；
  雪原白、草/叶绿色、雪层与 `short_grass` 可见、无洋红。
- UE Development H6 视角：`build/m2/reports/s4/h6-ue-after-1.png`；UE Shipping：`build/m2/reports/s4/h6-ue-shipping-after.png`。
  两后端同视角画面语义一致（雪、草、云杉、南瓜、十字植物）。
- 视觉断言（`tools/m2-visual.ps1`）对 Preview、UE Dev、UE Shipping 均 pass：
  洋红像素 0；雪地亮低饱和像素比例 0.45–0.50；绿色植被像素 1.4–1.6 万；深绿叶像素 1.4–1.6 万。
- 固定相机两次 Shipping 截图：max channel diff 1，mean 0.29（G7）。
- 重新执行 `tools/m2-acceptance.ps1 -UeSeconds 60`：**12 pass / 0 fail / 1 not_run（F5 干净机）**；
  新增 `s4_visual_colors` 通过。G2 60 秒：TPS 均值 19.73（逐窗 min 19.965 / max 20.043）、exit 0、
  无 fatal、BUSY 0。
- 工具链：`m2-package.ps1 -VerifyRepro` 两次 package/assemble 逐字节一致；NativeVersion/Adapter hash 校验通过。
- 水面抽查：Preview `build/m2/reports/s4/water-preview.png` 与 UE `build/m2/reports/s4/h6-ue-top2.png`
  均显示蓝色半透明水体。

### 17.4 已知限制（S4 内不解决）

- UE 不透明 pass 为**双面**：Converter 几何与整方块在 UE 镜像后的正面约定不一致，单面批处理会丢一侧；
  S4 选择双面 + 深度测试，记录为 R0 限制（GPU 三角形栅格增加，契约/数据不变）。
- UE 仍经过引擎 tonemap：同视角雪地均值 UE≈196 vs Preview≈225（约 13% 偏暗），色相/语义一致；
  未做逐像素色彩校准，差异可解释。
- biome tint 为**单群系固定色**：未实现 3×3 群系插值、swamp 特殊 grass 色变体、干树叶色。
- 非整方块仅覆盖 ≤2 个 element 的模型（雪层、十字植物、薄盒、部分台阶/活板门）；楼梯/栅栏/门等
  多 element 或带旋转的模型仍 `skip`。动画纹理/水面动画不承诺。**M3.5 承接**：水渲染（含水面动画/流动/排序/waterlogging）在 M35-G1 修复，见 `docs/M3_5_PLAN.md` §1.5。
- F5 干净机仍未执行（S3 阻塞项不变）。
- **H6（2026-11-21）**：用户实际运行 Shipping NativeBuild 后“暂时通过”；同时指出画面仍有问题，
  尚未给出逐项清单。后续按用户反馈继续修复，不把“暂时通过”升级为“视觉完全通过”。

### 17.5 S4 命令

```bat
build\m2\bin\reim-convert.exe extract --profile 26.3 --out generated\26.3
cmake --build --preset m2-release
powershell -ExecutionPolicy Bypass -File tools\m2-build.ps1 -Package -PackageConfig Development
powershell -ExecutionPolicy Bypass -File tools\m2-build.ps1 -Package -PackageConfig Shipping
powershell -ExecutionPolicy Bypass -File tools\m2-package.ps1 -Generated D:\EngineeringDocuments\REIM-Launcher\generated\26.3 ^
  -RuntimePackage D:\EngineeringDocuments\REIM-Launcher\build\m2\ue-shipping-package\Windows ^
  -NativeBuildOut D:\EngineeringDocuments\REIM-Launcher\build\native-build\26.3 -VerifyRepro
powershell -ExecutionPolicy Bypass -File tools\m2-visual.ps1 ^
  -PreviewImage build\m2\reports\s4\h6-preview-after.png ^
  -UeImage build\m2\reports\s4\h6-ue-shipping-after.png
powershell -ExecutionPolicy Bypass -File tools\m2-acceptance.ps1 ^
  -World <save> -Generated D:\EngineeringDocuments\REIM-Launcher\generated\26.3 -UeSeconds 60
```

> F5 与 H6 状态不变：F5 未运行（无干净机证据）；H6 需用户重新目视确认，未确认前 M2 不得宣布完成。
