# NekoShot 设计文档

> **历史设计（2026-10-28）**：实现过程中已改为 Mutter ScreenCast + GStreamer、
> Region `videocrop`、负原点支持、fragmented MP4、idle inhibitor 等。本文保留
> 初始设计决策；当前行为以 [README](../README.md)、[robustness.md](robustness.md)
> 和 [validation.md](validation.md) 为准。

- 日期：2026-10-28
- 状态：已实现（历史设计）
- 基线：Spectacle 6.8.80（commit d3f3be5）
- 目标环境：niri 26.04（当前 compositor 1bfaa83-modified / CLI 26.04-220-g1bfaa833）

> 旧工程（已安装的 nekoshot 包及其 /tmp 构建产物）不作为参考，本设计从上游 Spectacle 源码重新开始。

## 1. 意图

把 KDE Spectacle 的完整截图体验搬到 niri：同一个快捷键语义、同一套遮罩/选区/放大镜/标注/预览/导出/OCR/录屏流程，用户不需要改变肌肉记忆。niri 生态已有的工具（grim、niri IPC、wf-recorder、tesseract、KQuickImageEditor）作为平台能力来源，不重写它们。

非 KDE 环境下 Spectacle 目前不可用，原因是抓屏层只有 KWin ScreenShot2 / XCB / Null 三个实现，且 `Main.cpp` 在 Wayland 下硬性要求 KWin。NekoShot 的工作就是补上第四个平台实现，并替换桌面集成。

## 2. 目标 / 非目标 / 反目标

### 目标

1. 保留 Spectacle 6.8.80 的全部截图功能：区域、全部屏幕、当前屏幕、活动窗口、鼠标下窗口、延时、指针、阴影/装饰选项、标注、预览窗、复制/保存/另存、OCR、二维码、D-Bus/CLI、设置窗口、托盘、通知。
2. niri 原生手感：layer-shell 遮罩（不进入平铺/不触发工作区切换）、无窗口动画等待、热键到遮罩可交互在 250ms 量级。
3. 保留录屏：区域/屏幕/窗口三种模式，区域与屏幕用 wf-recorder 代偿，窗口走 portal（niri 的 Mutter ScreenCast 仅支持 monitor/window）。
4. 可构建、可测试、可打包（Arch PKGBUILD），并尽量保持与上游 Spectacle 的可合并性。
5. 应用标识、配置、通知、图标全部改为 NekoShot。

### 非目标

- 不修改 niri 合成器源码，不向 niri 加 IPC 方法。
- 不实现 KDE Purpose 分享（明确砍掉，`PURPOSE_FOUND` 不定义）。
- 不支持 X11 会话；XCB 后端保留源码但不编译。
- 不复制 KWin 的专有交互（例如 KWin 自己的窗口高亮抓取），用 niri 的等价交互代偿。
- 不引入 satty 之类的外部标注器。

### 反目标（即使技术上能跑也算失败）

- 遮罩需要 niri 窗口规则才能正常显示（用户必须手写规则才可用）。
- 抓屏依赖某个未在目标机器验证过的 niri IPC 参数。
- 冷启动或遮罩出现前有明显闪烁/动画。
- 为了 niri 适配而把上游 QML 改成另一套交互。

## 3. 约束与已验证事实

### 3.1 niri 能力（本机实测/源码确认）

| 能力 | 结论 |
|---|---|
| wlr-screencopy | 可用。grim 全屏 2880×1800：PPM 72ms，PNG 256ms |
| 区域抓取 | `grim -g` 可用（slurp 也可用） |
| 内置截图 UI | 选区/窗口/输出/放大镜/触控，**无标注**，不可脚本化定制 |
| `niri msg action screenshot-screen` | 支持 `--path`（绝对路径）、`-d`、`-p` |
| `niri msg action screenshot-window` | 支持 `--id`、`--path`、`-d`、`-p`；实测输出含窗口阴影（1622×1005 逻辑 → 2895×1832 物理） |
| 剪贴板副作用 | `screenshot-window -d false` 仍会写剪贴板（无法关闭） |
| `niri msg pick-window` | 交互式拾取，`--json` 返回窗口对象或 `null` |
| `niri msg pick-color` | 可用，可作为取色器功能 |
| IPC 窗口信息 | 有 layout/size，但**没有屏幕全局坐标**；窗口像素只能由合成器给出 |
| Mutter ScreenCast v4 | 实现 `RecordMonitor`、`RecordWindow`，**无区域/虚拟源** |
| layer-shell | 支持；LayerShellQt 6.8 已安装 |
| ext-image-copy-capture | 已实现（未来可取代 screenshot-window 的剪贴板副作用） |

### 3.2 桌面环境

- 无 kglobalacceld（KGlobalAccel 不可用），热键由 niri 配置提供。
- 无 KWin ScreenShot2；KDE 服务不可假设存在。
- DMS（quickshell）提供 `org.kde.StatusNotifierWatcher` 与 `org.freedesktop.Notifications`，托盘与通知可用。
- 剪贴板由 wl-clipboard 体系管理，cliphist 在跑。

### 3.3 构建环境

- Qt 6.12 / KF6 6.31 / ECM 6.31 / kquickimageeditor 0.7 / tesseract 5.5。
- Spectacle 6.8.80 要求 Qt ≥ 6.11、KF6 ≥ 6.30，满足。
- 依赖体积（相对 niri+grim+tesseract+wl-clipboard 基线）：全量 ~1.27 GiB / 280 包；砍 Purpose 后 ~968 MiB。本机已装齐，增量 0。

### 3.4 当前显示

- 单输出 eDP-1，2880×1800 @ scale 1.75，逻辑 1645×1028。
- 设计必须支持多输出与混合缩放，不能只按单屏写死。

## 4. 方案与选择

| 方案 | 做法 | 收益 | 代价 |
|---|---|---|---|
| A（选中） | 完整 fork Spectacle，改名 NekoShot，新增 niri 平台层，替换桌面集成 | 功能/观感最快对齐，标注/OCR/预览/导出原样复用 | 背 KF6/KIO 依赖；需逐处拆 KDE 假设 |
| B | 只 fork QML UI 层，重写外壳 | 依赖轻、启动快 | 重写 30–40% 应用逻辑，与上游脱钩 |
| C | grim + 自研遮罩 + satty 标注 | 代码最少 | 拿不到 Spectacle 的遮罩/标注/预览体验 |

选择 A。理由：平台层已经是干净接缝（`ImagePlatform` / `VideoPlatform` / `ExportManager`），QML 层与 KWin 无关；用户要的是 Spectacle 的功能与手感，不是另一套截图工具。

明确砍掉：Purpose 分享（节省 303 MiB，只影响分享菜单），QML 中 `PURPOSE_FOUND` 分支不编译。

## 5. 总体架构

```text
QML UI (CaptureOverlay / SelectionEditor / ViewerWindow / Settings)
        │
SpectacleCore（原样保留：抓取流程、延时、导出编排、OCR、D-Bus）
        │
   ┌────┴─────────────┬──────────────────┐
ImagePlatform        VideoPlatform     ExportManager
   │                    │                  │
ImagePlatformNiri   VideoPlatformNiri   KIO / 剪贴板 / Prison
(grim + niri IPC)   (wf-recorder / portal)
```

桌面集成从 KDE 专属换成 niri：

| KDE 依赖 | niri 代偿 |
|---|---|
| KGlobalAccel 全局热键 | niri `binds` + D-Bus 单实例（可常驻） |
| KWin 抓屏 | `ImagePlatformNiri`（grim） |
| KWin 交互式窗口抓取 | `niri msg pick-window` + `screenshot-window --id` |
| KWin 录屏 | `VideoPlatformNiri`（wf-recorder；窗口走 portal） |
| KDE 托盘 | KStatusNotifierItem（DMS 提供 watcher，可用） |
| KDE 通知 | KNotification → `org.freedesktop.Notifications`（DMS，可用） |

## 6. ImagePlatformNiri

### 6.1 探测与加载

新增 `isNiriSession()`：`NIRI_SOCKET` 非空，或 `XDG_CURRENT_DESKTOP` 含 `niri`。

`PlatformLoader::loadImagePlatform()` 顺序：

1. `NEKOSHOT_IMAGE_PLATFORM` 环境变量覆盖（调试用，旧名 `SPECTACLE_IMAGE_PLATFORM` 不再使用）。
2. niri 会话 → `ImagePlatformNiri`。
3. KWin ScreenShot2 可用 → `ImagePlatformKWin`（保留，KDE 会话仍可用）。
4. 真 X11 → `ImagePlatformXcb`（默认不编译）。
5. `ImagePlatformNull`。

`Main.cpp` 中的 "Wayland 必须有 KWin" 检查改为：Wayland 下且既非 niri 也无 ScreenShot2 时才报错。

### 6.2 输出模型

启动时与 `screenAdded/Removed`、每次抓取前读取 `niri msg --json outputs`，得到每个输出的：

- `name`（如 eDP-1）
- `logical.x/y/width/height`
- `logical.scale`
- `transform`

与 `QScreen` 按 `name` 匹配，失败则按逻辑矩形匹配。所有坐标换算以 niri 数据为准，避免 Qt/Wayland 的取整差异。

### 6.3 抓取矩阵

| CaptureMode | GrabMode | 实现 |
|---|---|---|
| RectangularRegion | PerScreenImageNative | 各输出 `grim -t ppm -o <name> -` 原生抓取 → 按逻辑坐标组合 → 交给 Spectacle 遮罩裁剪 |
| AllScreens | AllScreens | 同上，组合后直接导出（不再进遮罩） |
| AllScreensScaled | AllScreensScaled | 同上，按最大 scale 统一缩放 |
| CurrentScreen | CurrentScreen | `grim -t ppm -o <focused-output> -` |
| ActiveWindow | ActiveWindow | `niri msg action screenshot-window --id <focused> --path <tmp> -d true -p <bool>` → 读文件 |
| WindowUnderCursor | WindowUnderCursor | `niri msg --json pick-window` 取 id → 同上 |
| FullScreen | 回退到 AllScreens | — |

关键实现约定：

- 优先 PPM 走 stdout，避免 PNG 编码与磁盘 IO（72ms vs 256ms）。QImage 自带 PPM 解码。
- 多输出并行抓取（QProcess 并发），组合时使用 `ImageMetaData::setLogicalXY` / `setSubGeometryList` / `setDevicePixelRatio`，逻辑对齐 KWin 后端。
- `includePointer` → `grim -c`；niri 窗口抓取 → `-p`。
- 窗口抓取噪声：niri 的窗口截图带阴影，无法关闭；`includeShadow=false` 在 niri 下不生效，设置界面需注明。
- `includeDecorations` 同理：niri 无服务端装饰（`prefer-no-csd`），该设置不再生效，界面注明。

### 6.4 多输出与 DPR

每个输出图像按自身 scale 设置 DPR；组合后 DPR 取最大值，`subGeometryList` 记录各输出的逻辑位置/尺寸/scale。QML 遮罩使用逻辑坐标，DPR 只影响绘制清晰度与最终裁剪像素。

### 6.5 窗口抓取与剪贴板

niri 的窗口截图必然写剪贴板，这是平台限制。处理策略：

1. 抓取前快照剪贴板（`wl-paste --list-types` + 各 MIME 数据，上限 32 MiB）。
2. 调用 `niri msg action screenshot-window ... --path <tmp> -d true`。
3. 读回图片后恢复剪贴板（`wl-copy`）。
4. 恢复失败只记日志，不阻断流程；设置项 `restoreClipboardAfterWindowCapture` 默认开启。

临时文件用 `QTemporaryFile`（`/tmp/nekoshot-XXXXXX.png`），读取后立即删除。

窗口截图尺寸包含阴影，DPR 取该窗口所在输出的 scale。

### 6.6 取消与错误

- `pick-window` 返回 `null` / 用户按 Esc → `newScreenshotCanceled()`。
- grim / niri 进程非零退出、超时（5s）→ `newScreenshotFailed(<stderr>)`。
- 输出不存在或焦点输出缺失 → 回退到全部输出 / 报错。

### 6.7 能力位

```text
supportedGrabModes = AllScreens | CurrentScreen | ActiveWindow
                   | WindowUnderCursor | PerScreenImageNative
                   | AllScreensScaled
supportedShutterModes = Immediate
```

不声明 `TransientWithParent`（niri picker 只返回单个窗口），因此 WindowUnderCursor 走 `GrabMode::WindowUnderCursor`。

## 7. 遮罩与选区

### 7.1 窗口形态

CaptureWindow 在 niri 下改为 **layer-shell overlay**：

- `LayerShellQt::Window::get(window)` 后设置：`LayerOverlay`、四边锚定、`exclusiveZone = -1`、`keyboardInteractivity = Exclusive`、`setScreen(QScreen*)`。
- 每个 QScreen 一个遮罩窗口；Qt 6.5+ 不需要调用已废弃的 `Shell::useLayerShell()`，M2 需实测确认 shell integration 被正确选中，否则在 main 里显式设置 `QT_WAYLAND_SHELL_INTEGRATION=layer-shell`。
- 不用 `Qt::WindowFullScreen`；窗口尺寸由锚定决定，同时把 geometry 设为该屏逻辑矩形。
- layer-shell 不可用时回退到上游的全屏 QWindow 行为。

### 7.2 时序（无感的关键）

1. 快捷键触发 → 进程激活（常驻时直接 D-Bus 调用）。
2. 隐藏所有自身窗口。
3. 抓图（grim，并行）。
4. 构造遮罩窗口并显示（layer 面无窗口动画）。
5. 选区确认 → 裁剪 → 标注/预览。

所有抓图都发生在遮罩显示之前，因此遮罩不会进入截图。

### 7.3 交互

沿用 Spectacle 的 SelectionEditor：十字准星、放大镜、尺寸气泡、方向键微调、Space 拖动、Enter 确认、Esc 取消、右键取消。多屏时工具栏跟随主选区。

### 7.4 延时与指针

- 延时：隐藏窗口 → `QTimer` → 抓图，沿用 Spectacle 的倒计时进度。
- 指针：`grim -c`；窗口模式用 niri 的 `-p`。

## 8. 桌面集成

### 8.1 热键

niri 没有全局快捷键服务，热键由 `binds` 提供。仓库提供 `contrib/niri-binds.kdl`：

```kdl
binds {
    Print       { spawn "nekoshot" "-r"; }
    Ctrl+Print  { spawn "nekoshot" "-m"; }
    Alt+Print   { spawn "nekoshot" "-a"; }
    Mod+Shift+S { spawn "nekoshot" "-r"; }
    // 录屏开关
    Mod+Shift+R { spawn "nekoshot" "--toggle-recording" "region"; }
}
```

用户需替换 niri 默认的 `Print { screenshot; }`。文档中给出说明。

`CaptureModeModel` / `RecordingModeModel` / `RecordingModeMenu` 中的 KGlobalAccel 调用在 niri 会话下跳过（返回空），设置界面显示"由 niri 管理"。

### 8.2 单实例与常驻

- 保留 `KDBusService::Unique` 与 `org.nekoshot.NekoShot` D-Bus 接口。
- 新增 `--daemon`（`StartMode::Daemon`）：启动后常驻，每完成一次操作不退出，等待下一次 D-Bus 调用。
- 推荐 `spawn-at-startup "nekoshot" "--daemon"`；热键直接 `gdbus call` 或 `nekoshot -r`（后者会转发到常驻实例并立即退出）。
- 若 M4 发现状态复用（遮罩/导出/QML 文档）在重复调用下有残留，则回退为"每次冷启动 + KDBusService 激活"，并单独优化启动时间；功能正确性优先于常驻。

### 8.3 托盘、通知、录屏指示

- 保留 KStatusNotifierItem（DMS 的 watcher 可用），录屏时显示托盘图标与菜单。
- 通知走 KNotification → DMS。
- 录屏停止：提供 D-Bus 方法 / CLI `--stop-recording`，niri bind 指向它（替代 KGlobalAccel 的停止快捷键）。

### 8.4 启动自检

- 无 `NIRI_SOCKET` 且无 KWin → 报错并退出（GUI 模式弹窗）。
- `grim` 不存在 → 启动时提示安装。

## 9. 录屏 VideoPlatformNiri

| 模式 | 实现 | 说明 |
|---|---|---|
| Region | `wf-recorder -g "x,y wxh"` | wlr-screencopy 区域录制；支持 h264/vp9，GIF/WebP 视 ffmpeg 支持 |
| Screen | `wf-recorder -o <output>` | 或全屏 |
| Window | portal（`VideoPlatformWayland` + KPipeWire） | niri Mutter ScreenCast 支持 RecordWindow；M5 做 spike，失败则该模式标记不可用并提示 |

- 音频：`videoRecordSystemAudio` → `wf-recorder --audio`；麦克风 → `--audio-device`。设置项保留，映射到 wf-recorder 参数。
- 停止、计时、托盘状态沿用 Spectacle 的 VideoPlatform 信号。
- 录屏期间的遮罩/预览行为保持上游逻辑。

## 10. 标注 / OCR / 导出

- 标注：KQuickImageEditor 0.7 原样保留（箭头、矩形、椭圆、文字、模糊、高亮、画笔、撤销/重做）。
- OCR：tesseract C API 保留；启动时枚举可用语言，缺语言包时在设置页提示安装 `tesseract-data-*`（本机当前只有 afr/osd，需要补 eng）。
- 二维码：Prison 保留。
- 导出：KIO 保留（本地/远程 URL）；Purpose 不编译，分享菜单项消失。
- 剪贴板：QClipboard / wl-clipboard 由 Qt Wayland 处理，图像复制沿用上游。

## 11. 命名与配置

| 项目 | 值 |
|---|---|
| 产品名 | NekoShot |
| 二进制 | `nekoshot` |
| App ID / D-Bus | `org.nekoshot.NekoShot` |
| desktop 文件 | `org.nekoshot.NekoShot.desktop` |
| 图标 | `nekoshot` |
| 配置文件 | `nekoshotrc` |
| QML URI | `org.nekoshot.NekoShot.private` |
| appdata | `org.nekoshot.NekoShot.appdata.xml` |
| notifyrc | `nekoshot.notifyrc` |

- `KAboutData` 名称/描述改为 NekoShot，保留上游作者署名，并注明 "Based on Spectacle"。
- `src/Gui/SettingsDialog/spectacle.kcfg` 更名为 `nekoshot.kcfg`，`kcfgfile name="nekoshotrc"`。
- 许可证保持 GPL-2.0-or-later，沿用上游 `LICENSES/`。

## 12. 构建、依赖与打包

### 12.1 CMake

- `-DWITH_X11=OFF`（默认值在 NekoShot 改为 OFF）。
- 删除 `find_package(KF6Purpose)` 与 `KF6::PurposeWidgets` 链接。
- QML 模块 URI 改为 `org.nekoshot.NekoShot.private`（一次性机械替换所有 QML import）。
- 新增 `src/Platforms/ImagePlatformNiri.cpp/.h`、`src/Platforms/VideoPlatformNiri.cpp/.h`。

### 12.2 运行时依赖（Arch）

`qt6-base qt6-declarative qt6-wayland qt6-multimedia kcoreaddons kconfig ki18n kguiaddons kwindowsystem knotifications kdbusaddons kglobalaccel kxmlgui kio kirigami kstatusnotifieritem prison kcrash kquickimageeditor layer-shell-qt tesseract grim kpipewire`

可选：`wf-recorder`（录屏）、`tesseract-data-eng`、`kimageformats`、`breeze-icons`。

预期增量体积（干净 niri 系统）：约 968 MiB（无 Purpose）。本机增量 0。

### 12.3 安装内容

`/usr/bin/nekoshot`、desktop 文件、D-Bus service、systemd user service、图标、appdata、翻译、`contrib/niri-binds.kdl`、文档。

## 13. 里程碑与验收

### M1 平台层骨架

- 新增 `ImagePlatformNiri`：AllScreens / CurrentScreen / PerScreenImageNative。
- 加载器、`Main.cpp` 门控、`--background --output` 路径打通。
- 验收：`nekoshot -b -f -o /tmp/a.png` 产出正确尺寸与内容的 PNG；`-m` 同上。

### M2 遮罩

- layer-shell 遮罩、多屏、DPR、选区/放大镜/尺寸、延时、指针。
- 验收：`nekoshot -r` 在 eDP-1 上立刻出现遮罩（无动画、不切换工作区），框选后可复制/保存；175% 缩放下裁剪尺寸与选区一致。

### M3 窗口模式

- ActiveWindow、WindowUnderCursor（niri picker）、剪贴板快照/恢复。
- 验收：两种模式均能产出窗口截图；抓取后原剪贴板内容被恢复。

### M4 桌面集成与常驻

- 全量改名、配置迁移、niri binds 文档、`--daemon`、托盘/通知、停止录屏 D-Bus。
- 验收：niri 启动后常驻；按热键 → 遮罩 ≤250ms；托盘可见；通知可点"打开/标注"。

### M5 录屏

- `VideoPlatformNiri`（wf-recorder）实现 Region/Screen；Window portal spike。
- 验收：三种模式按可用性工作；文件可播放；停止/计时/托盘正确。

### M6 打磨与打包

- 单元测试、错误路径、PKGBUILD、文档、性能预算复核。
- 验收：`makepkg -si` 成功；全部手动矩阵通过。

## 14. 风险与对策

| 风险 | 影响 | 对策 |
|---|---|---|
| layer-shell 在 Qt 6.12 下未被选中 | 遮罩变成普通窗口/有动画 | M2 首日验证；失败则显式设置 shell integration 或回退全屏窗口 |
| 混合 DPR 多屏组合错位 | 裁剪区域偏移 | 用 niri 输出数据而非 QScreen；单测覆盖 1.0/1.75 混合 |
| 窗口抓取污染剪贴板 | 用户剪贴板被替换 | 快照/恢复 + 设置项；后续可用 ext-image-copy-capture 根治 |
| niri CLI 与 compositor 版本偏差 | 参数不支持/行为不同 | 运行时探测（`--help`、能力探测），不硬编码未验证参数 |
| portal 窗口录制不可用 | 窗口录屏缺失 | wf-recorder 兜底屏幕/区域；窗口模式明确提示 |
| KGlobalAccel 无 daemon 时的告警/阻塞 | 启动噪音 | niri 会话下跳过所有 KGlobalAccel 调用 |
| 常驻实例状态残留 | 第二次截图异常 | M4 专项测试；不稳定则回退冷启动 |
| 上游合并冲突 | 维护成本 | 新代码集中在 Platforms/ 与少量门控点；保留 KWin/X11 源码结构 |

## 15. 验证计划

### 单元测试

- niri JSON 解析：outputs（含 transform/scale）、windows、focused-window、pick-window 的 null 分支。
- 几何组合：单屏/多屏、1.0/1.75、负坐标、transform。
- 命令构造：grim 参数、screenshot-window 参数、临时文件生命周期。
- 剪贴板快照/恢复的边界（超过上限、非文本 MIME）。

### 手动矩阵

- 模式 × 屏幕数（1/2）× 缩放（100%/175%）× 指针（开/关）× 结果（成功/取消/错误）。
- 特殊场景：全屏窗口、浮动窗口、XWayland 窗口、锁屏前、录屏中再次截图。
- 启动方式：冷启动、常驻、D-Bus 激活、desktop action。

### 性能预算

- 热键 → 遮罩可交互：常驻 ≤250ms；冷启动 ≤600ms。
- 选区确认 → 复制到剪贴板 ≤150ms。
- 全屏保存 PNG ≤500ms（含编码）。
- 常驻内存：与普通 Qt Quick 应用同量级，不做硬指标，但需记录基线。

## 16. 留给实现者的决定

1. `--daemon` 是否保留为默认安装方式（取决于 M4 的冷启动测量与状态复用测试）。
2. 窗口抓取是否值得改用 ext-image-copy-capture（彻底避免剪贴板副作用）——v1 不做。
3. layer-shell 遮罩与全屏窗口的最终取舍以 M2 实测为准。
4. KWin/X11 源码是否长期保留（当前决定：保留但不编译 X11，KWin 保留以便 KDE 会话可用）。
5. GIF/WebP 录屏是否在 v1 提供（取决于 wf-recorder 与 ffmpeg 的实际编码支持）。

## 附：本设计中我做的判断（请一并检查）

- 抓屏用 grim 的 PPM stdout，而不是 PNG 或 niri 的 screenshot-screen（避免编码与磁盘 IO）。
- 区域模式复用 Spectacle 自己的遮罩，而不是 slurp。
- 窗口模式接受 niri 的剪贴板副作用，用快照/恢复缓解。
- 热键完全交给 niri binds，不做自建全局快捷键守护。
- 明确砍掉 Purpose，不做分享。
