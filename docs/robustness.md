# NekoShot 稳健性说明

本文记录 0.10.3 的稳健性设计、故障矩阵、验收证据和仍存在的缺口。目标不是“永不失败”，而是：

1. 失败时尽量不破坏已经写出的数据；
2. 失败要能被用户或脚本发现，而不是静默成功；
3. 状态机在取消、重复请求和 finalize 期间保持一致。

## 故障矩阵

| 故障场景 | 0.10.2 及以前 | 0.10.3 行为 | 剩余风险 |
|---|---|---|---|
| 录制中崩溃 / SIGKILL | MP4 无 `moov`，整段不可播 | ScreenCast 路径使用 fragmented MP4，已写片段可播；最多丢最后一个片段（约 1 s） | wf-recorder 回退路径仍非 fragmented |
| 锁屏 / DPMS / 输出断开 | ScreenCast 会话关闭，文件损坏 | 录制期间持有 logind `idle` inhibitor（ScreenSaver 回退），阻止空闲/DPMS；会话仍关闭时由 fragmented MP4 保底 | 已锁定的会话不会预检；DMS 之外的锁屏实现未逐一验证 |
| 磁盘写满 | 录制中途失败，留下损坏文件 | 启动前检查剩余空间（<64 MB 拒绝）和只读目录；fragmented MP4 保留已写片段 | 录制中不做持续空间监控，也没有最大时长/大小限制 |
| 编码器“成功”但输出 0 字节 | 可能报告保存成功 | 按失败上报 | 非零字节但内容损坏仍未校验 |
| finalize 期间再次 Stop | 可能退出进程/杀掉编码器 | `Rendering` 计入录制中，第二次 Stop 被忽略；实测双 stop 得到合法 MP4 | 120 s 看门狗超时后仍会 SIGKILL |
| 窗口拾取器等待中 Stop | daemon 可能永久等待 picker | Stop 取消拾取，daemon 继续运行 | 拾取期间没有独立 UI 状态提示 |
| 后台/DBus 截图或录制失败 | 进程仍退出 0 | 直接 one-shot 进程退出 1 | 转发给 daemon 的 CLI 仍异步，退出码不代表最终结果 |
| 剪贴板延迟恢复覆盖新截图 | 300 ms 固定等待后恢复 | 恢复任务与快照在进程内串行化 | 两个独立 `nekoshot` 进程并发时仍是 best effort |
| 第三方通过 D-Bus 触发截图/录制 | 默认允许 | 可用 `dbusRemoteCapture=false` 关闭；StopRecording 也遵守该开关 | 默认仍允许，没有发送方鉴权 |
| 会话已锁定 | 可能得到全黑截图或启动失败的录制 | 无预检 | 无锁屏/黑屏检测与重试 |
| niri 重启 | daemon 后续请求报错 | 无重连 | daemon 不会重新探测 `NIRI_SOCKET`/输出/ScreenCast |
| wf-recorder 回退录制中断 | 文件损坏 | 无 fragmented 保护 | 需要换封装或自行 finalize |
| 录制启动 | 首段内容可能晚于用户预期 | 未改变 | ScreenCast 协商约 4–10 s 的启动延迟未消除 |

## 验收证据

| 项目 | 结果 |
|---|---|
| Screen H.264 正常停止 | fragmented MP4，2.93 s / 3.3 MB，ffprobe 可读 |
| 录制中 `kill -9` gst | 1.0 s 可播放 MP4，不再是无 `moov` 文件 |
| fragmented MP4 + AAC | 视频 3.23 s、AAC 44.1 kHz 2.59 s，双流正常 |
| idle inhibitor | 录制期间 `systemd-inhibit --list` 可见 NekoShot 条目，停止后消失 |
| 75 s 无输入录制 | 屏幕保持点亮（mean≈26180），文件可播放；文件时长 65 s（启动协商延迟） |
| 假 `grim`（exit 1） | `-b -f` 返回 1，无输出文件 |
| 正常截图 | 返回 0 |
| 3 s 录制连续两次 stop | 进程正常退出，MP4 合法 2.43 s |
| daemon 拾取器 + Stop | 拾取进程消失，daemon 存活 |
| 剪贴板 sentinel | 多次恢复成功；有一次被外部程序写入剪贴板干扰 |

详细命令与环境见 [validation.md](validation.md)。

## 运维建议

- **崩溃后**：先尝试直接播放原文件。ScreenCast 的 fragmented MP4 通常能播到最后一个片段；wf-recorder 回退文件若不可播，保留原文件并用 `ffmpeg -i broken.mp4 -c copy fixed.mp4` 尝试修复，不要覆盖原文件。
- **长录**：保持 `nekoshot --daemon` 常驻，录制期间会自动抑制空闲；仍建议关注剩余空间（1800p Medium 约 133 MB/分钟）。
- **脚本**：直接 `nekoshot -b ...` 的退出码可靠；经过 daemon 转发的请求是异步的，需轮询输出文件或 D-Bus 信号。`--stop-recording` 返回不代表文件已写完。
- **剪贴板**：依赖窗口截图的剪贴板快照。避免在截图后 150 ms 内立刻复制新内容；跨进程并发截图仍是 best effort。
- **安全**：如果不希望同会话程序通过 D-Bus 触发截图/录制，设置 `[General] dbusRemoteCapture=false`。

## 仍待解决

按优先级：

1. **ScreenCast 启动延迟可见化**：目前文件比用户等待时间短 4–10 s，需要提示“录制已开始”或提前建立会话。
2. **A/V 起始对齐测试**：总时长漂移只有几十毫秒，但首帧音画偏移未测。
3. **wf-recorder 回退的崩溃安全**：换 fragmented 容器或增加收尾/修复。
4. **daemon 的 niri 重连与看门狗**：niri 重启、ScreenCast 服务重启后自动恢复。
5. **锁屏/黑屏预检与重试**。
6. **日志文件/verbose 模式**：目前失败主要靠通知和 journal。
7. **故障注入测试**：磁盘满、PipeWire 重启、niri 重启、剪贴板持有者卡死、畸形 IPC JSON。

## 实现位置

- `src/Platforms/IdleInhibitor.{h,cpp}`：logind/ScreenSaver idle 抑制。
- `src/Platforms/VideoPlatformNiri.cpp`：`fragment-duration=1000`、空间/只读预检、零字节检查、状态机与 finalize。
- `src/Platforms/ClipboardGuard.{h,cpp}`：快照/恢复串行化与 `restoreAfterDelay()`。
- `src/Platforms/NiriCapture.cpp`：窗口截图、剪贴板异步恢复、文件轮询。
- `src/SpectacleCore.{h,cpp}`：`isRecordingOrStarting()`、`exitCode()`、Stop 状态机。
- `src/SpectacleDBusAdapter.cpp`：`dbusRemoteCapture` 开关。
