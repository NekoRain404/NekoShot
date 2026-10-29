# NekoShot 验收记录

- 日期：2026-10-28
- 被测版本：NekoShot 0.10.0，`master`，最终 HEAD `d0e733b`
- 构建：`./build/bin/nekoshot`（Release，`WITH_X11=OFF`）
- 测试会话：Wayland / niri 26.04（`niri-spicy-git 26.04.r220.g1bfaa83-2`），单输出
  `eDP-1` 2880x1800 @ 120 Hz，缩放 175%（逻辑尺寸 1645x1028）
- 自动化测试：`ctest --test-dir build` 6/6 通过
- 输入模拟：`ydotool`（临时启动 `ydotoold`，测试后停止）

> 说明：按 2026-10-28 21:12 的用户决定，本次验收**跳过全部录屏验证**（包括
> `RecordWindow`），录屏代码保留但标记为实验性/暂缓，不作为已验证能力。

## 手动矩阵

单屏 × 175% 缩放。`identify` 尺寸为 PNG 像素尺寸；逻辑尺寸 = 物理尺寸 / 1.75。

| # | 模式 / 场景 | 命令或操作 | 结果 | 证据 |
|---|---|---|---|---|
| 1 | 全屏（冷启动） | `./build/bin/nekoshot -b -f -o /tmp/nk-full.png -n` | ✅ rc=0 | `PNG 2880x1800 ... 2.9784MiB` |
| 2 | 当前屏（冷启动） | `./build/bin/nekoshot -b -m -o /tmp/nk-current.png -n` | ✅ rc=0 | `PNG 2880x1800 ... 2.97563MiB` |
| 3 | 活动窗口 | `./build/bin/nekoshot -b -a -o /tmp/nk-active.png -n` | ✅ rc=0 | `PNG 2839x1759 ... 458687B`；对应焦点窗口 tile 1622x1005 × 1.75 |
| 4 | 鼠标下窗口（点击） | `-b -u -o /tmp/nk-under.png -n`，ydotool 将指针移到 (800,500) 并点击 | ✅ rc=0 | `PNG 2895x1832 ... 629684B`；图像内容为指针下的 Chrome 窗口（含其 CSD），与焦点窗口不同 |
| 5 | 鼠标下窗口（取消） | 先 `rm -f /tmp/nk-under.png`，再执行 `-b -u -o /tmp/nk-under.png -n`，启动后从 0.5 s 起每 100 ms 发送 Esc | ✅ rc=0，无输出文件 | 运行前已删除旧文件；`ls: cannot access '/tmp/nk-under.png'` |
| 6 | 区域（成功） | `-b -r -o /tmp/nk-region.png -n`，遮罩出现后按 Enter 接受记忆选区 | ✅ rc=0 | `PNG 2880x1800 ... 1.26706MiB`；遮罩显示 `2,881x1,801` 选区，导出比选区少 1 px（边界夹取/取整） |
| 7 | 区域（取消） | 常驻模式下 `gdbus ... RectangularRegion 0`，遮罩出现后按 Esc | ✅ 遮罩关闭，常驻进程存活 | `niri msg layers` 的 `Overlay layer: (empty)`；无崩溃 |
| 8 | 常驻 D-Bus 区域 | `./build/bin/nekoshot --daemon &` + `gdbus ... RectangularRegion 0` | ✅ | 调用立即返回 `()`；`niri msg layers` 出现 `Namespace: "window"`、`Keyboard interactivity: exclusive`；`grim` 截图可见遮罩 UI |
| 9 | 常驻重复打开 | 第 8 项 Esc 关闭后再次调用 `RectangularRegion 0` | ✅ 再次打开 | 第二次遮罩在约 5 次 10 ms 轮询内出现；daemon PID 不变 |
| 10 | 转发 CLI：后台导出 | 常驻运行时 `./build/bin/nekoshot -b -o /tmp/nk-bg.png` | ✅ rc=0 | CLI 0.873 s 返回；文件由常驻进程在 CLI 退出后约 0.9 s 写入，`PNG 2880x1800 ... 3.12633MiB` |
| 11 | 转发 CLI：仅界面 | 常驻运行时 `timeout 8 ./build/bin/nekoshot -l` | ✅ | CLI rc=0；niri 出现窗口 `Title: "NekoShot"`、`App ID: "org.nekoshot.NekoShot"`；测试后 `niri msg action close-window --id 923` 关闭 |
| 12 | 转发 CLI：区域 | 常驻运行时 `./build/bin/nekoshot -r` | ✅ 遮罩出现，Esc 取消 | `niri msg layers` 检测到 `Namespace: "window"`；取消后 daemon 存活 |
| 13 | 剪贴板恢复 | `printf 'sentinel' \| wl-copy` → `-b -a -o /tmp/nk-clip.png -n` → `wl-paste --no-newline` | ✅ 恢复成功 | 截图前 `sentinel`，截图后仍为 `sentinel`；`PNG 2839x1759` |
| 14 | 缺少 grim | `PATH=/nope ./build/bin/nekoshot -b -f -o /tmp/x.png -n` | ✅ 失败 | rc=1；`/tmp/x.png` 不存在 |
| 15 | 指针开 / 关 | `-b -f -p` 与 `-b -f` 各一次 | ⚠️ 两次都成功，但截图里看不到光标 | `-p` 会向 grim 传 `-c`；在指针坐标附近裁剪对比未观察到光标像素，原因未进一步判定 |
| 16 | 100% 缩放 | — | ⛔ 未测 | 本机唯一输出固定为 175%；未修改系统缩放 |
| 17 | 录屏（Screen / Region / Window） | — | ⏸ 暂缓 | 按用户决定跳过（包括 `RecordWindow`）；其代码层已知限制见 README |

### 详细观察

- **D-Bus 语义**：`RectangularRegion`、`RecordRegion`、`RecordWindow`、`StopRecording`
  等均为 `Q_NOREPLY`。`gdbus call` 很快返回 `()`，实际工作异步进行；转发 CLI 同样会在
  常驻进程完成导出前退出，脚本中不能以 CLI 返回作为文件已存在的依据。
- **鼠标下窗口**：本机 niri-spicy 的 `niri msg pick-window` 会在约 0.7–0.8 s 后自动确认
  指针下的窗口（即使没有点击）；`-u` 的点击路径仍可正常捕获。Esc 需要在自动确认前
  发送才能取消：从 0.5 s 起连续发送 Esc 时进程 rc=0 且不产生文件。
- **区域遮罩**：遮罩是 layer-shell surface，`niri msg layers` 中命名空间为
  `"window"`；Enter 接受选区，Esc 取消。`-b -r -o` 成功导出 2880x1800 PNG。
- **标注 / 导出**：遮罩内标注工具栏可见，导出流程按 `-o` 写出 PNG；OCR 按钮存在，
  但本次未做语言级验收（本机仅安装 `afr`、`osd` 语言数据，且没有 CLI 入口）。
- **通知**：保存后未在 `dbus-monitor` 中观察到 NekoShot 的
  `org.freedesktop.Notifications.Notify` 调用，DMS 桌面也未显示通知；窗口截图时观察到的
  是 niri 自己发出的 "Screenshot captured" 通知。此项标记为未验证，不作为通过项。
- **托盘**：NekoShot 只在录制状态创建 `StatusNotifierItem`；录屏验证暂缓，因此未验证。

## 性能

计时方法：冷启动使用 `date +%s%N` 包住一次独立进程（无 daemon），包含进程启动、
抓屏与 PNG 写盘。常驻热键在 `gdbus ... RectangularRegion 0` 前取时间戳，随后每 10 ms
执行一次 `niri msg layers`，首次出现 `Namespace: "window"` 时停止计时。

| 场景 | 第 1 次 | 第 2 次 | 第 3 次 | 中位数 | 目标 | 结论 |
|---|---:|---:|---:|---:|---:|---|
| 冷启动 `-b -f` | 1954 ms | 1635 ms | 1715 ms | **1715 ms** | ≤600 ms | ❌ 未达标 |
| 冷启动 `-b -a` | 1854 ms | 2069 ms | 2064 ms | **2064 ms** | ≤600 ms | ❌ 未达标 |
| 常驻热键→遮罩 | 765 ms | 243 ms | 296 ms | **296 ms** | ≤250 ms | ⚠️ 原始值略超；第 1 次为 QML 冷加载 |

- 每次 `niri msg layers` 本身耗时约 20–25 ms；热键测量中 5 次轮询约含 100 ms 开销，
  扣掉轮询开销后热态约 150–200 ms，处于目标附近。原始测量中位数仍按 296 ms 记录。
- 冷启动未达 600 ms 目标；主要成本来自 Qt 进程启动、grim 抓屏与 PNG 编码/写盘。

## 打包（makepkg，不安装）

安装 makedepends 后，普通 `makepkg -f` 已能完成依赖检查并构建：

```text
$ cd packaging && makepkg -f --noconfirm
==> Finished making: nekoshot 0.10.2-2 (...)
```

产物：

- `packaging/nekoshot-0.10.2-2-x86_64.pkg.tar.zst`，1,607,529 字节（1.5M），
  sha256 `530c4d14d61ff4c2eb8b0fd839bc57575d6e10f116ab130de8e21c3324ae312b`
- `pacman -Qlp` 共 205 项，其中 172 项是 `/usr/share/locale/` 下的翻译文件
- 不再安装上游 Spectacle 的 HTML 手册与 man page；`kdoctools` 已从构建依赖移除
- 调试包不作为发布产物

### 包内非 locale 文件

```text
/usr/bin/nekoshot
/usr/lib/systemd/user/app-org.nekoshot.NekoShot.service
/usr/share/applications/org.nekoshot.NekoShot.desktop
/usr/share/config.kcfg/nekoshot.kcfg
/usr/share/dbus-1/interfaces/org.nekoshot.NekoShot.xml
/usr/share/dbus-1/services/org.nekoshot.NekoShot.service
/usr/share/icons/hicolor/scalable/apps/nekoshot.svg
/usr/share/kglobalaccel/org.nekoshot.NekoShot.desktop
/usr/share/knotifications6/nekoshot.notifyrc
/usr/share/licenses/nekoshot/GPL-2.0-or-later.txt
/usr/share/metainfo/org.nekoshot.NekoShot.appdata.xml
/usr/share/qlogging-categories6/spectacle.categories
```

已删除上游遗留的 `kconf_update` 脚本（它们只会处理不再读取的 `spectaclerc`），因此包内
不再出现 `spectacle.upd` 与 `kconf_update_bin/*`。

## 未验证 / 暂缓

- 录屏（Screen / Region / Window）、录制托盘图标与录制通知：按用户决定暂缓。
- OCR 语言级结果：无 CLI 入口，本次只确认 OCR 按钮存在，未执行文本识别验收。
- 保存通知：未观察到 NekoShot 自身的 `Notify` 调用（见上）。
- 100% 缩放、多显示器、负原点布局：本机无条件，未测。
- 包安装与卸载：按要求未安装。
- 指针嵌入截图：`-p` 不报错，但本次截图中未观察到光标像素，原因未进一步判定。

## 清理

- 常驻进程已停止；`busctl --user list` 中无 `org.nekoshot.NekoShot`。
- 无 `nekoshot` / `wf-recorder` 残留进程；`niri msg layers` 的 Overlay 层为空。
- 测试产生的录制文件（`~/Videos/Screencasts` 下 21:04、21:12 两个文件）已删除；
  录制临时目录已删除。
- 测试用 `ydotoold` 已停止；niri 会话、系统 `spectacle` 包未改动。

## 最终修复后的复验（0.10.0）

在终审修复与 UI 重设计之后，对安装到 `/usr/local` 的最终二进制做了一轮运行时复验：

- 常驻转发 `nekoshot -r`：遮罩出现 → 键盘选区（方向键 + Enter）→ 预览窗
  `org.nekoshot.NekoShot` 出现，遮罩层消失，守护进程存活。
- 连续两次 `gdbus RectangularRegion`：只出现一个遮罩（单飞守卫生效）。
- `wtype -k Escape` 关闭遮罩，守护进程存活；`ydotool` 注入在部分时序下未命中，
  属于输入注入工具差异，不是 NekoShot 的取消路径缺陷。
- `PATH=/nope nekoshot -b -f -o /tmp/x.png -n`：退出码 1，不产生文件。
- `-b -f`、`-b -m`、`-b -f --scaled`：均生成 2880x1800 PNG（`--scaled` 按设计回退为
  未缩放全屏）。
- 抓取黑图的一次现象由 DMS 的 `fade-to-dpms` 覆盖层导致，直接 `grim` 同时为黑；
  唤醒显示器后恢复正常，与 NekoShot 无关。
- 快速连续发起多次转发抓取时，单飞守卫会丢弃重叠请求（CLI 仍返回 0）；这是防并发
  设计，脚本调用应串行或等待完成。
- 用鼠标绝对坐标注入拖拽未能在本环境稳定选中，选区验收以键盘路径为准。


另外，0.10.0-2 修复了标注颜色/字体的卡死：原实现使用 Qt Widgets `QColorDialog`/`QFontDialog`，
窗口被 layer-shell Overlay 遮罩压在下面且处于模态，表现为卡死；现改为遮罩内嵌的
`ColorPopup` / `FontPopup`（预设色板、Alpha、十六进制、字体族/字号/Bold/Italic），
不再创建独立窗口。

## 录屏验证（0.10.1，旧记录）

> 以下记录针对 0.10.1；0.10.2 增加了 Screen ScreenCast、音频、H.265/AV1、
> VA profile 探测和码率限制，最新结果见文末「录屏验证（0.10.2）」。

- **Screen**：`nekoshot -R s` → 3 秒 → `nekoshot --stop-recording`，得到 VP9
  2878×1798、2.65 s 的 WebM，`ffprobe` 可读。
- **Region**：`nekoshot -R r` → 遮罩 → 键盘选区（提交为 `-g 559,502 546x386`）→
  wf-recorder → 停止，得到 VP9 956×676、4.92 s 的 WebM。
- **Window**：niri Mutter ScreenCast `RecordWindow(window-id)` → PipeWire node →
  GStreamer `vp9enc/webmmux`；实现者实测 1409×1759、8.57 s、499 帧的 VP9 WebM。
  无焦点窗口时 153 ms 内失败并提示 `No focused window to record`；重复启动被拒；
  未安装 `x264enc` 时 H.264 明确报错。
- **限制**：Window 暂不支持音频；静态窗口因 PipeWire 仅在画面变化时推帧而帧率较低；
  H.264 Window 需要 `gst-plugins-ugly`；指针通过 `cursor-mode` 生效。

## 录屏验证（0.10.2）

- 被测版本：0.10.2，`master`，HEAD 包含 `feat: add H.265 and AV1 recording
  formats` 及其后的码率/VA 加固；最终包为 `nekoshot-0.10.2-2-x86_64.pkg.tar.zst`
  （`pkgver=0.10.2`、`pkgrel=2`，1,607,529 B）。
- 环境同前：niri 26.04、单输出 `eDP-1` 2880×1800、175% 缩放、120 Hz。
- VA 支持：`vainfo` 报告 `VAProfileH264Main/High`、`VAProfileHEVCMain`、
  `VAProfileAV1Profile0`，因此 Screen/Window 可广告 H.264/H.265/AV1 硬编。
- 码率：`vah264enc`/`vah265enc`/`vaav1enc` 显式 `bitrate=16000`（kbps），
  VP9 显式 `target-bitrate=12000000`（bit/s），`videorate` 固定 30 fps。

### Screen / Window（系统声开启，3 秒）

| 场景 | 文件 | ffprobe | 大小（字节） |
|---|---|---|---|
| Screen H.265 | `Screencast_20261029_131600.mp4` | hevc 2880×1800 + aac 44.1 kHz stereo，2.93 s | 5,195,242 |
| Window H.265 | `Screencast_20261029_131632.mp4` | hevc 1409×1759 + aac，3.09 s | 5,443,704 |
| Screen AV1 | `Screencast_20261029_131645.mp4` | av1 2880×1800 + aac，3.83 s | 5,670,292 |
| Window AV1 | `Screencast_20261029_131658.mp4` | av1 1409×1759 + aac，3.53 s | 5,071,092 |

加固前（同一硬件、未设 bitrate）：Screen H.265 88,916,032 B；Window H.265
32,559,816 B；Screen AV1 31,112,720 B；Window AV1 15,576,064 B。设置显式码率后
文件缩小到约 5 MB。

### Region

- HEVC：wf-recorder `-c libx265` 短录可产出 hevc MP4（2.85 s，125,885 B）。
- AV1：`libsvtav1` 在 wf-recorder 下失败（`bad parameter 0x80001005` /
  `avcodec_open2 failed`）；改用 `-c libaom-av1 -p cpu-used=8` 后成功，
  `/tmp/region-aom.mp4`：av1 700×700、2.74 s、49,273 B。
- Region AV1 为软件编码，CPU 占用和编码速度明显低于 VA-API；短片段已验证，
  未做长录/质量对比。

## 多屏几何、窗口拾取与静态帧（0.10.3）

- 被测版本：0.10.3，构建 `./build/bin/nekoshot`（Release，`WITH_X11=OFF`）。
- 环境不变：单输出 `eDP-1` 2880×1800、175% 缩放、120 Hz；没有第二块物理
  显示器，因此多屏/混合 DPI/负原点只能做单元测试和代码路径验收。
- `ctest --test-dir build --output-on-failure`：6/6 通过。

### 坐标与抓屏

| 场景 | 命令 / 操作 | 结果 |
|---|---|---|
| 区域截图（记忆选区 400×300 @ 100,100） | `XDG_CONFIG_HOME=/tmp/nk-cfg` + `-b -r`，Enter 接受 | ✅ PNG 700×525（400×300 × 1.75），说明全局选区到图像本地坐标的裁剪正确 |
| 区域截图（空选区，全屏） | `rememberSelectionRect=Never` + `-b -r`，Enter 接受 | ✅ PNG 2880×1800 |
| 全屏 / 当前屏 | `-b -f`、`-b -m` | ✅ 2880×1800 |
| 活动窗口 | 聚焦 kitty 窗口后 `-b -a` | ✅ 1409×1759（805×1005 × 1.75） |
| 鼠标下窗口 | `-b -u`，ydotool 移动并点击 | ✅ 1409×1759 |
| 缩放全屏 | `-b -f --scaled` | ✅ PNG 1646×1029（逻辑分辨率，`grim -s 1`） |
| 窗口 DPR 来源 | `niri msg workspaces` 将窗口 workspace 映射到输出，再取该输出 scale | ✅ 单屏与焦点输出一致；混合 DPI 路径由单元测试 `testWorkspaces` 覆盖 |

- 负原点裁剪换算由 `NiriCaptureTest::testImageLocalRect` 和
  `testCombineMixedDprNegativeOrigin` 覆盖：`(-800,100)` 的图像原点、全局
  选区 `(-800,100,100,50)` 会映射为图像本地 `(0,0,100,50)`。
- `AllScreensScaled` 只在 `qGuiApp->screens().size() > 1` 时出现在
  `supportedGrabModes()` 中（与 KWin 后端一致）；CLI 在单屏下仍可显式请求
  `--scaled`，实际走 `grim -s 1`。

### Window 拾取与静态帧

| 场景 | 操作 | 结果 |
|---|---|---|
| Window 拾取 | `-R w -o /tmp/nk-win6.mp4 -n`，`niri msg pick-window` 出现后移动并点击 tiled kitty 窗口 | ✅ 1409×1759 H.264 + AAC，2.8 s / 84 帧 |
| 全屏窗口 | 同上，点击 fullscreen 窗口 | ✅ 2881×1801 H.264，3.4 s / 102 帧 |
| 静态窗口帧率 | 对静态 tiled 窗口录制约 4 s | ✅ 不再出现 0.03 s 单帧文件；`compositor` 重复最后画面，`nb_frames` 与时长匹配 |
| 拾取取消（Esc） | 启动拾取后 `ydotool key 1` | ✅ 进程退出，无输出文件，无残留 `niri msg pick-window` |
| 拾取取消（StopRecording） | 启动拾取后 `nekoshot --stop-recording` | ✅ 立即取消，无输出文件，无残留拾取进程 |
| 拾取超时 | 10 分钟定时器 | 代码路径；未等待实际超时 |

### 录屏回归

- Screen H.264 + AAC（`-R s -o /tmp/nk-screen.mp4`）：2880×1800，
  2.33 s / 70 帧；AAC 44.1 kHz 立体声 2.08 s。
- Screen VP9 + Opus（`-R s -o /tmp/nk-screen.webm`）：2880×1800，
  3.1 s / 93 帧（`nb_read_frames=93`）。
- 新的 GStreamer 参数单元测试覆盖 VP9/H.264/H.265/AV1 × 无音频/系统声/
  麦克风/双路，以及 `videocrop` 引入前的 capability gate。

### 未验证 / 注意事项

- 没有第二块物理显示器，真正的多屏、混合 DPI、负原点和跨输出选区没有
  端到端验收；当前结论来自单元测试与代码路径审查。
- 本机 DMS 的 `fade-to-dpms` 会在空闲时把画面变黑；直接 `grim` 同样为黑，
  测试前已用输入唤醒，黑图与 NekoShot 无关。
- Region 仍走 `wf-recorder`，没有光标开关；Region AV1 仍是 `libaom-av1`
  软件编码。
- `compositor` 会带来少量 CPU 开销；长时间录制与 A/V 漂移仍未测量。

## 录制质量档与长录（0.10.3 续）

- 新增设置项 `[VideoSave] videoQuality`：`Low` / `Medium` / `High`
  （默认 `Medium`），映射 4000 / 10000 / 18000 kbit/s。
- 实测：

| 场景 | 结果 |
|---|---|
| Screen H.264，Medium，约 4.5 s | `bit_rate=9,505,223`，5,311,767 B |
| Screen H.264，High，约 4.6 s | `bit_rate=16,965,298`，9,685,897 B |
| Screen H.264 + AAC，Medium，67 s | video 67.67 s / 2030 帧；audio 67.61 s；漂移约 53 ms；gst-launch 约 38–42% 单核，RSS 约 103 MB；133,375,290 B |
| Region VP9，Medium，400×300 选区 | 700×524 WebM，154,505 B，可解码 |

- 停止路径：`nekoshot --stop-recording` 现在等待编码器写盘完成再退出；
  6 s 录制得到 5.49 s 的合法 MP4。此前会把 `allDone` 立即发出，导致
  gst-launch 在 moov 写入前被终止。
- 修复了录制退出时 `KStatusNotifierItem` 静态析构的 SIGSEGV：托盘图标改为在
  `~SpectacleCore()` 中、`QCoreApplication` 仍存活时销毁。
- 停止编码器改为异步 `Rendering` 状态 + 120 s 看门狗，GUI 不再阻塞在
  `waitForFinished()`。
- 仍未验证：更长时间（>10 分钟）的漂移、不同内容复杂度下的画质主观评价、
  低/高档在 4K 输出上的效果，以及带有真实系统声音内容的听感验收。

### Region 录屏改走 ScreenCast（0.10.3）

| 场景 | 结果 |
|---|---|
| Region H.264（400×300 @ 100,100） | 701×526 MP4，3.9 s / 117 帧 |
| Region 含指针 | 抽帧可见光标位于裁剪区域内（ScreenCast `cursor-mode`） |
| Region H.264 + AAC | 视频 701×526 / 2.83 s；AAC 44.1 kHz 立体声 / 2.72 s |
| Region AV1（`preferredVideoFormat=32`，即 MP4_AV1） | AV1 701×526 / 2.87 s / 6.5 Mbps，由 `vaav1enc` 硬件编码，不再走 libaom 软编 |
| 裁剪帧检查 | 裁剪区域内部没有遮罩/工具栏；右侧与底部的绿色像素是终端窗口自身内容 |

- 早期实现按 `qRound(logical × scale)` 估算流尺寸，可能比选区多 1 px；现已改为
  读取 `modes[current_mode]` 的物理尺寸，400×300 选区实测精确输出 700×525。
- 跨输出选区、无 GStreamer 环境仍走 `wf-recorder`，此时没有指针与 ScreenCast
  音频；该路径只由单元测试（`regionCaptureFor` 返回 `nullopt`）覆盖。

## 打包（0.10.3）

```text
$ cd packaging && makepkg -f --noconfirm
==> Finished making: nekoshot 0.10.3-1 (Thu 29 Oct 2026 03:38:32 PM CST)
```

- `packaging/nekoshot-0.10.3-1-x86_64.pkg.tar.zst`，1,593,234 字节（1.52M），
  sha256 `85e638aa76c1e31303a3cd05b6ba69a175a79a10c0ac453f30e79e8596a33f17`
- `pacman -Qlp` 共 208 项：新增 `usr/share/man/man1/nekoshot.1.gz` 与 57 个
  `nekoshot.mo`，不再包含旧的 `spectacle.mo`；无其他非预期文件。
- `ctest` 包含 `appstreamtest`、`filename_test`、`niri_ipc_test`、
  `niri_capture_test`、`clipboard_guard_test`、`wf_recorder_args_test`，6/6 通过。

## 0.10.3 收尾（i18n、Widgets 主题、打包）

- 翻译域从 `spectacle` 改为 `nekoshot`：`po/*/spectacle.po` 全部重命名为
  `nekoshot.po`，重新生成 `po/nekoshot.pot`，57 个语言文件合并新字符串；
  `zh_CN` 补齐画质、窗口拾取、光标提示等关键译文。
- Qt Widgets 全局主题：`WidgetTheme::apply()` 在 `QApplication` 创建后应用
  深色 QPalette + QSS（菜单、对话框、输入框、按钮、标签页、滚动条、Tooltip）。
- 取色弹层新增 HSV 饱和度/明度方块与色相条，保留预设色板、Alpha 与 Hex。
- 截图指针：实测 `grim -c` 与不带 `-c` 的图像在指针区域无差异，确认 niri
  的 screencopy 不合成光标；截图指针选项在 niri 下禁用并显示说明。
  录制仍通过 ScreenCast 的 `cursor-mode` 包含指针。
- Region 裁剪改用 `modes[current_mode]` 的物理尺寸，400×300 选区现在精确
  输出 700×525（此前 701×526）。
- Window 格式按模式校验：优先 H.264，其次 H.265、AV1；`.webm` 优先 VP9；
  替换格式时发送 `recordingFormatChanged` 通知。
- 新增 `nekoshot.1` man page、`.github/workflows/build.yml`、
  `tests/integration/run-niri.sh`（无 `NIRI_SOCKET` 时 CTest SKIP）、
  `packaging/.SRCINFO`、`packaging/make-release.sh`、`packaging/README.md`。
- `ctest` 现在有 7 项：6 个单元测试 + 1 个 live smoke（本地通过，7.1 s）。

## D-Bus 捕获开关（0.10.3）

- `[General] dbusRemoteCapture=false` 时启动 `--daemon`，`gdbus ... RectangularRegion 0`
  返回 `()` 但不出现遮罩（`niri msg layers` 中 nekoshot 层为 0）。
- 改回默认 `true` 后同一调用出现遮罩；Esc 取消。
- 该开关只作用于 `SpectacleDBusAdapter`，niri 快捷键触发的 CLI/KDBusService
  转发路径不受影响。

## 启动与热键性能（0.10.3）

环境：单输出 2880×1800 @175%，niri 26.04，Release 构建，屏幕已唤醒。

| 场景 | 5 次测量（ms） | 中位数 | 目标 | 结论 |
|---|---|---:|---:|---|
| 冷启动 `-b -f` | 448 / 436 / 442 / 445 / 447 | **445** | ≤600 | ✅ |
| 冷启动 `-b -a` | 614 / 598 / 601 / 584 / 588 | **598** | ≤600 | ✅（余量很小） |
| 常驻热键→遮罩（首次） | 245–266 | ~250 | ≤250 | ✅ 附近 |
| 常驻热键→遮罩（后续） | 106 / 109 / 116 / 127 | **~115** | ≤250 | ✅ |

阶段耗时（`NEKOSHOT_PROFILE=1`，`-b -f` 一次）：

```text
qapplication 50 ms
kdbusservice 51 ms
core-ctor-end 51 ms
exec-tick 165 ms      # Qt/Wayland 事件循环首次处理
doGrab-enter 165 ms
capture-output-enter 178 ms
capture-output-bytes 238 ms   # grim PPM 约 60 ms
capture-output-decoded 243 ms # PPM 解码约 5 ms
write-image-enter 247 ms
export-image-return 321 ms    # PNG 编码/写盘约 74 ms
```

优化项：

- `--daemon` 在启动时预加载 QML 引擎及 `CaptureOverlay`、`AnnotationEditor`、
  `ScreenshotView`、`FloatingToolBar`、`CaptureSettingsColumn`、`ViewerWindow`、
  `DialogPage`，首次热键从 768 ms 降到 249 ms。
- 单输出窗口截图直接用 `QScreen::devicePixelRatio()`，省掉
  `niri msg workspaces` + `outputs` 两次 IPC。
- 等待 niri 截图文件的轮询从 20 ms 改为 5 ms。
- 剪贴板恢复改为后台任务：图像立即返回，150 ms 后恢复快照；10 次
  `sentinel` 测试全部恢复成功（niri 的剪贴板写入实测在文件落盘后约
  6–115 ms），`Main` 退出前等待线程池 1 秒以保证一次性 `-b` 也恢复。
- 后台/DBus 截图不再启动无人消费的 QR 扫描线程。
- 新增 `NEKOSHOT_PROFILE=1` 阶段计时和 `NEKOSHOT_PNG_COMPRESSION` 调试开关。

`-b -a` 的余量很小（约 2 ms）：niri `screenshot-window` 本身约 110 ms、
PNG 编码约 70–100 ms，加上约 120 ms 的 Qt/Wayland 事件循环首次处理。
若系统负载或 PNG 内容更复杂，可能回到 600 ms 以上；进一步压缩需要直接复用
niri 写出的 PNG 或减少 Qt 启动轮次。

## 状态机稳健性（0.10.3 收尾）

- `VideoPlatform::isRecording()` 现在把 `Rendering`（异步写索引）也算作录制中；
  在 finalize 期间再次 `--stop-recording` 不会退出进程或杀掉编码器。
- 新增 `isRecordingOrStarting()`；Niri 后端在窗口拾取器或 PipeWire 流等待期间
  返回 true。
- 验证：
  - daemon 中 `RecordWindow` 启动拾取器后调用 `--stop-recording`，拾取进程被
    取消，daemon 继续运行。
  - 3 秒 Screen 录制连续两次 stop，进程正常退出，MP4 为合法 2.43 s。
- 仍未处理：锁屏/DPMS/输出断开、崩溃/强杀、磁盘写满时 MP4 缺少 moov。

## 稳健性加固（0.10.3）

### 崩溃/强杀后的可播放性

- Screen H.264 正常停止：fragmented MP4，`duration=2.93 s`，3.3 MB。
- 录制中 `kill -9` gst-launch：文件仍可被 ffprobe 读取，得到 1.0 s 的合法 MP4
  （最后一个 1 秒片段），不再是无 `moov` 的损坏文件。代价是崩溃时最多丢失
  约 1 秒。
- fragmented MP4 + AAC：视频 3.23 s、AAC 44.1 kHz 2.59 s，两条流均正常。

### 空闲抑制

- Screen 录制期间 `systemd-inhibit --list` 可见
  `NekoShot ... idle ... NekoShot is recording ... block`；停止后条目消失。
- 75 秒无输入录制：屏幕仍亮（截图 mean≈26180，非黑），文件可播放；
  文件时长 65 s，说明 ScreenCast 启动/协商有约 10 s 的起始延迟（不是数据丢失，
  后续会单独测量）。此前同一环境无抑制时会在约 60 s 被 DPMS 打断。

### 退出码与失败检测

- `PATH` 指向退出码为 1 的假 `grim`：`nekoshot -b -f` 返回 **1**，不产生文件。
- 正常截图返回 **0**。
- 编码器正常退出但输出 0 字节时，上报失败而不是“保存成功”。

### 状态机

- daemon 中 `RecordWindow` 启动拾取器后 `--stop-recording`：拾取进程消失，
  daemon 继续运行。
- 3 秒录制连续两次 stop：进程正常退出，MP4 合法 2.43 s。
- `Rendering` 计入录制中，finalize 期间的第二次 stop 不会杀编码器。

### 其他

- 剪贴板快照/恢复在单进程内串行化；连续 sentinel 测试大部分通过，有一次被
  外部程序写入剪贴板干扰。跨进程并发截图仍是 best effort。
- 剩余空间不足/只读目录的拒绝逻辑已实现，但未用真实小容量文件系统做故障注入。
- wf-recorder 回退路径没有 fragmented MP4 和 idle 抑制之外的额外保护。
