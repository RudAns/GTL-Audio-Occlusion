<div align="center">

# 游戏中的声音遮挡、阻挡与衍射

**房间—门户传播 · 多射线阻挡 · 超额程差衍射 · 分频透射 · Steam Audio HRTF · GPU 波动场烘焙**<br>
从射线检测到房间—门户传播，让墙后、门外、楼上的声音按真实路径衰减和定位：技术调研 + UE 5.8 可交互复现

<a href="LICENSE"><img alt="license MIT" src="https://img.shields.io/badge/license-MIT-0a7bbb"></a>
<img alt="引擎 UE 5.8" src="https://img.shields.io/badge/%E5%BC%95%E6%93%8E-UE_5.8-313131?logo=unrealengine">
<img alt="代码 C++ · HLSL" src="https://img.shields.io/badge/%E4%BB%A3%E7%A0%81-C%2B%2B_%C2%B7_HLSL-00599c">
<a href="Docs/Research.html"><img alt="调研 65 篇文献" src="https://img.shields.io/badge/%E8%B0%83%E7%A0%94-65_%E7%AF%87%E6%96%87%E7%8C%AE-e05d44"></a>
<img alt="手游 可行" src="https://img.shields.io/badge/%E6%89%8B%E6%B8%B8-%E5%8F%AF%E8%A1%8C-2ea44f">
<img alt="耳机 推荐" src="https://img.shields.io/badge/%E8%80%B3%E6%9C%BA-%E6%8E%A8%E8%8D%90-8a2be2">

[📥 下载调研报告](https://github.com/RudAns/GTL-Audio-Occlusion/releases/latest/download/Research.html) · [📥 下载落地文档](https://github.com/RudAns/GTL-Audio-Occlusion/releases/latest/download/Implementation.html)

<br>

[![调研报告封面](Docs/shots/00_research_cover.png)](Docs/Research.html)

</div>

## 调研报告

**[Docs/Research.html](Docs/Research.html)**：单文件网页，约 18,000 字，65 条中日韩英文献，带 4 个交互示意（含 Web Audio 试听），下载后用浏览器打开。

墙后、门外、楼上的声音应当如何衰减、滤波并改变方向，是游戏空间音频中最常被简化、也最影响定位判断的环节。报告比较了 Wwise Spatial Audio、Steam Audio、微软 Project Acoustics（Triton）、FMOD、CRI ADX 与 UE5 原生方案，整理了卡普空 RE ENGINE、《彩虹六号：围攻》、《守望先锋》、《杀手》、《战争机器》等已上线作品的公开做法，并给出手游分档方案与 UE5 复现蓝图。主要结论：

- 遮挡问题的核心不是“直线上有没有墙”，而是“声音经哪条路到达”。已上线作品的共识结构是：空间拓扑 → 路径 → 少量参数；射线只用于同一空间内的局部阻挡。
- UE 原生遮挡是单射线二值判定，默认参数不产生效果，只适合作为基线或小型项目的快速方案。
- 衍射与透射都与频率相关。用路径的超额程差按闭式公式求每个频段的衰减，比按角度映射更接近物理，且几乎没有额外成本。距离衰减只对总路径长度计算一次，多条路径按能量合成。
- 手游可以完整使用房间—门户方案；实时几何声学与卷积混响留给 PC 与主机。
- 运行时不需要训练 AI 模型；AI 的价值在于离线自动划分房间、门户与材质。

## 技术落地文档

**[Docs/Implementation.html](Docs/Implementation.html)**：把每一层的做法、参数、UE 接入要点和坑点清单整理成可以直接搬进项目的形式（单文件网页，示意图内嵌）。

> 提示：如需在自己的项目中使用，可以把本工程的源码和这份 HTML 一起交给 AI 编程助手，让它参照文档把需要的部分接入你的工程。

## UE 5.8 可交互 Demo

一座六个房间的“声屋”里放了收音机、机床、座钟、篝火、测试音箱和巡逻者六个声源，可以逐层打开处理步骤，也可以在六组对照之间切换，第一人称和俯视视角都能看到射线、路径、探针和烘焙切片。

![俯视总览](Docs/shots/01_overview_topdown.png)

### 运行

- 引擎：UE 5.8。`Run.bat` 会自动查找引擎：环境变量 `UE_ROOT`、启动器注册表、各磁盘根目录下的 `UE_5.8` 与 `Program Files\Epic Games\UE_5.8`；都找不到时先设置 `UE_ROOT`。
- 双击 `Run.bat`：编译编辑器模块后以游戏窗口启动。也可以直接用编辑器打开 `AudioOcclusion.uproject`，地图是 `/Game/Maps/SoundHouse`。
- 需要耳机：所有声源都走 Steam Audio 的 HRTF 双耳渲染。

### 操作

| 按键 | 作用 |
|---|---|
| WASD / 方向键 | 移动 |
| 鼠标 | 点选左侧处理层与对照组；第一人称下转视角，按住 Alt 出鼠标点选 |
| V | 切换第一人称 / 俯视 |
| Q / E | 俯视镜头按 45° 一档旋转 |
| 滚轮 | 俯视镜头缩放 |
| F | 开关最近的门 |
| 1 ~ 0 | 预设组合（见下表） |
| G / B | 切换对照组 / 在组内切换 A、B（C），也可以用鼠标点 |
| Tab | 切换关注的声源 |
| R | 只听关注的声源（独奏） |
| H | 开关调试绘制（射线、路径、虚拟声源、透射命中点） |
| K | 重新烘焙 GPU 波动场（高耗时，约 1~3 分钟；启动时自动读取上次结果），也可以在右侧面板点 |
| P / L / O | 探针显示 / 切片视图（体素、首达时间、直达声级、早期能量、比值）/ 波场回放 |
| F1 | 帮助 |
| F3 | 隐藏界面 |
| Esc | 退出 |

### 十个步骤

左侧面板按真实依赖关系排列：先单选处理方案（不处理、UE 原生、自研管线、波动烘焙、Steam Audio），再勾选该方案下的子层；衍射映射和穿墙透射挂在门户路径下面，房间混响和时间平滑是对所有方案生效的通用层。数字键是下表的预设组合，3~8 在自研管线里逐层叠加。

| 键 | 步骤 | 做了什么 |
|---|---|---|
| 1 | 仅距离衰减 | 只有距离衰减，是所有方案的基础 |
| 2 | UE 原生遮挡 | 引擎自带的单射线二值遮挡（-12 dB、600 Hz 低通） |
| 3 | 多射线比例 | 12 条射线打向声源周围 40 cm 的点，按被挡比例连续衰减 |
| 4 | 房间—门户路径 | 房间盒与门洞组成图，Dijkstra 找最短路径并拉紧，声像放到最后一段方向上 |
| 5 | 程差频率映射 | 按绕行多出的距离（excess path）分 250 Hz / 1 kHz / 4 kHz 计算衍射损失（Kurze–Anderson），门的开度另算 |
| 6 | 穿墙透射 | 沿直线累加每层材质的分频隔声量，作为第二个声部在真实位置播放，和路径声部按能量相加 |
| 7 | 房间混响耦合 | 每个房间一条混响子混音，由房间内的声源激励，再按门户路径耦合到听者所在房间 |
| 8 | 时间平滑 | 参数一阶平滑（0.2 s），虚拟声源沿方位角滑动，避免路径切换时跳变 |
| 9 | 波动烘焙 | 读取 GPU FDTD 烘焙的探针数据：首达时间、直达声级、早期能量、感知方向 |
| 0 | Steam Audio | Valve Steam Audio 4.8.1 的体积遮挡与分频透射 |

### 六组对照

| 组 | 关注点 | A | B | C |
|---|---|---|---|---|
| 1 基线 | 二值切换与边缘抖动 | 仅距离衰减 | UE 原生遮挡 | |
| 2 遮挡量 | 参数是否连续 | UE 原生单射线 | 多射线比例 + 平滑 | |
| 3 路径 | 声音是否从正确的门口传来，关门后是否改道 | 多射线比例 | 房间—门户路径 | |
| 4 衍射映射 | 同样的拐角，远近不同时衰减是否不同 | 按角度（Wwise 式） | 按程差分频段 | |
| 5 方案级 | 同一位置的听感、开销与制作成本 | 自研管线全开 | 波动烘焙 | Steam Audio |
| 6 组合 | 叠加后是否一致、有无重复计算 | 波动烘焙 + 动态门户 | Steam Audio + 自研门户路径 | |

### 推荐的体验路线

1. 站在客厅柱子前（出生点附近），按 Tab 选“测试音箱”，R 独奏，依次按 1、2、3，左右平移，听二值跳变和连续衰减的区别。
2. 走到客厅东侧，选“收音机”，按 4、5，看路径线绕过两道门洞；按 F 关琴房门，再按 6，路径断开后只剩穿墙透射。
3. 工程附带一份烘焙结果（`BakeData/`），打开就能用；改了场景再按 K 重新烘焙（约 1~3 分钟，结果存到 `Saved/WaveBake/`）。然后按 P 看探针、L 切换切片、O 看波场回放，再按 9 用烘焙数据驱动声音。
4. 按 0 切到 Steam Audio，对比同一位置的听感；G 切到第 6 组，B 切换组合方案。

### 截图

| | |
|---|---|
| ![第一人称客厅](Docs/shots/02_firstperson_hall.png) | ![多射线](Docs/shots/05_step3_multiray_pillar.png) |
| ![门户路径](Docs/shots/06_step4_portal_radio.png) | ![关门透射](Docs/shots/08_step6_door_closed_transmission.png) |
| ![直达声级切片](Docs/shots/15_slice_lds.png) | ![波场回放](Docs/shots/18_wave_replay.png) |
| ![Steam Audio](Docs/shots/19_steamaudio_radio.png) | ![帮助](Docs/shots/21_help.png) |

### 工程结构

```
Source/AudioOcclusion
  AcousticSubsystem      求解与应用：十个步骤、六组对照、房间—门户图、射线查询、平滑、混响耦合
  AcousticEmitterComponent  每个声源三个同步声部：路径、透射、Steam Audio
  AcousticRoom / AcousticPortal / AcousticDoor  房间盒、门洞、可开合的门
  WaveBakeActor          体素化、探针布置、烘焙状态机、查询、切片与回放可视化
  OcclusionCharacter / OcclusionPlayerController / OcclusionHUDWidget  角色、输入、界面
  OcclusionBuildLibrary  用代码搭建声屋场景并导出 Steam Audio 几何
  CaptureDirector        自测脚本：截图、读数、录音
Source/OcclusionWave     GPU FDTD 波动求解（RDG 计算着色器），着色器在 Shaders/WaveFDTD.usf
Plugins/SteamAudio       Steam Audio 4.8.1 UE 插件，按 UE 5.8 从源码编译
Tools                    生成声音、导入资产、重建关卡、自测截图的脚本
```

### 重建与自测

- 重建关卡：`powershell -File Tools/build_level.ps1`（删除旧地图，代码搭建场景，再导出 Steam Audio 静态几何与动态门）。
- 自测：`powershell -File Tools/capture.ps1 -Build`，在 `Saved/Capture` 下输出 21 张截图、各步骤录音（WAV）和求解读数 `readout.txt`。

### 已知限制

- Steam Audio 只启用了直达路径的遮挡与透射，没有烘焙它的 pathing 探针，所以 Steam Audio 模式下关门后声音不会绕门传来。第 6 组 B 方案用自研门户路径补上这一部分。
- UE 5.8 对 HRTF 声源的“距离衰减前”子混音发送会读错缓冲区（单声道缓冲按双声道读取），产生 NaN 并让整个混音静音。本工程改为距离衰减后发送，再除回距离增益，效果等价。
- 美术资源来自 UE 官方示例内容（ArchVis、关卡原型、角色模板）。

## 许可证

本仓库自己编写的代码、脚本、着色器、文档和截图以 [MIT 许可证](LICENSE) 发布。下列第三方内容不在 MIT 范围内，仍按各自的许可使用：

- Steam Audio 插件（`Plugins/SteamAudio`）：Valve Corporation，Apache License 2.0，许可证原文在 `Plugins/SteamAudio/LICENSE`。
- 角色、关卡原型与建筑场景（`Content/Characters`、`Content/LevelPrototyping`、`Content/ArchVis`、`Content/Building`、`Content/Variant_Horror`、`Content/XRFramework`、`Content/Input`）：Epic Games 随引擎模板提供的示例内容（EULA 中的 Examples），按 Unreal Engine EULA 可随项目再分发，版权归 Epic Games 所有。
- 单次脚步声（`SourceAssets/Audio/Raw/Footstep_Rock_Land01.wav` 及由它处理得到的 `SW_Step`）：取自 UE 引擎插件 NetworkPredictionExtras 自带的录音，属于引擎内容，按 Unreal Engine EULA 使用。其余循环声音由 `Tools/gen_sounds.py` 生成，属于本仓库。
- Unreal Engine 5.8：打开、编译和运行本工程需遵守 [Unreal Engine EULA](https://www.unrealengine.com/eula/unreal)。
