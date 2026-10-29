# NekoShot Implementation Plan

> **历史文档（2026-10-28）**：本文是 0.10.x 的初始实现计划。部分约束已被后续
> 实现取代（负原点不再拒绝、Screen/Window/Region 优先 ScreenCast、录屏编码器
> 与质量档、fragmented MP4、idle inhibitor 等）。当前架构与行为以
> [design.md](design.md) 的历史注记、[robustness.md](robustness.md) 和
> [validation.md](validation.md) 为准。

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把上游 Spectacle 6.8.80 改造成在 niri 下可用的 NekoShot：保留全部截图、标注、OCR、导出与录屏功能，用 grim + niri IPC + wf-recorder 代偿 KDE 平台能力。

**Architecture:** 以 Spectacle 源码为基底做减法 fork。新增 `ImagePlatformNiri`（grim + niri IPC）、`VideoPlatformNiri`（wf-recorder），遮罩改用 LayerShellQt，热键交给 niri `binds`，其余 QML/标注/导出/OCR 原样复用。平台差异收敛在 `src/Platforms/` 与少量门控点。

**Tech Stack:** C++20、Qt 6.12、KF6 6.31、CMake + Ninja、QML、QtTest、grim、wl-clipboard、wf-recorder、niri IPC。

**Spec:** `docs/design.md`

## Global Constraints

- Qt 最低 6.11、KF6 最低 6.30（上游 CMake 已声明，不得下调）。
- 构建命令固定为：`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_X11=OFF -DBUILD_TESTING=ON`，然后 `cmake --build build`。
- 产品标识固定：二进制 `nekoshot`、App ID / D-Bus `org.nekoshot.NekoShot`、配置 `nekoshotrc`、图标 `nekoshot`、QML URI `org.nekoshot.NekoShot.private`。
- 不编译 Purpose（不定义 `PURPOSE_FOUND`，不 `find_package(KF6Purpose)`）。
- 不编译 X11（`WITH_X11=OFF`）；XCB 源码保留在树中但不参与构建。
- 外部命令只允许用 `QProcess` + 参数列表调用，禁止 shell 拼接。
- 临时文件用 `QTemporaryFile`，使用后立即删除。
- 读取 niri 状态一律用 `niri msg --json ...`，用 `QJsonDocument` 解析。
- 不修改 niri 源码，不新增 niri IPC。
- 每个任务结束时提交一次，提交信息以 `task N: ` 开头。
- 新代码必须通过项目现有编译选项（C++20、`-Wall -Wextra`）。

## Review Focus

以下五类输入/失败模式最可能伤害真实使用者，每个都必须在拥有该代码的任务里有一条测试或明确的验证步骤：

1. **没有任何输出，或抓取过程中输出消失** → 必须报错并结束，不能挂起、不能发出空图。
2. **多屏混合缩放（1.0 + 1.75）与负坐标布局** → 合成图像与裁剪必须对齐；负原点布局在 v1 必须被显式拒绝，不能静默错位。
3. **`pick-window` 被取消（Esc / 返回 `null`）** → 必须发出 `newScreenshotCanceled`，剪贴板不能被污染后无法恢复。
4. **窗口抓取前剪贴板里已有大图或剪贴板管理器抢占所有权** → 快照/恢复必须有大小上限和超时，失败时不得丢数据或卡住。
5. **运行时缺少 `grim` / `niri` / `wf-recorder`，或 niri CLI 与 compositor 版本不一致** → 必须给出明确错误（对话框或非零退出），不能静默无操作。

---

## Task 1: 重命名构建目标与 QML 模块

**Files:**
- Modify: `src/CMakeLists.txt`
- Modify: `src/Gui/*.qml`（所有 `import org.kde.spectacle.private`）
- Modify: `src/Main.cpp`（仅当存在硬编码的桌面文件名时；本任务不改）

**Interfaces:**
- Consumes: 上游 Spectacle 6.8.80 源码
- Produces: 可执行目标 `nekoshot`（`build/bin/nekoshot`），QML 模块 URI `org.nekoshot.NekoShot.private`，资源路径 `qrc:/qt/qml/org/nekoshot/NekoShot/private`

- [ ] **Step 1: 替换 QML import**

```bash
cd /home/nekorain/Development/NekoShot
grep -rl 'org\.kde\.spectacle\.private' src --include='*.qml' | xargs sed -i 's/org\.kde\.spectacle\.private/org.nekoshot.NekoShot.private/g'
```

- [ ] **Step 2: 修改 `src/CMakeLists.txt`**

只改这些位置，**不要**改 `spectacle.kcfg`、`spectacle_debug.h`、`spectacle.categories` 这些文件名：

```cmake
set(SPECTACLE_QML_URI "org.nekoshot.NekoShot.private")
set(SPECTACLE_QML_PATH "qrc:/qt/qml/org/nekoshot/NekoShot/private")
add_executable(nekoshot)
qt_add_qml_module(nekoshot URI ${SPECTACLE_QML_URI} DEPENDENCIES QtCore QtQuick)
```

并把 `target_sources`、`target_link_libraries`、`target_include_directories`、`qt_target_qml_sources`、`install(TARGETS ...)` 中作为**构建目标**出现的 `spectacle` 全部替换为 `nekoshot`。

- [ ] **Step 3: 构建**

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_X11=OFF -DBUILD_TESTING=ON
cmake --build build
```

Expected: 构建成功，`build/bin/nekoshot` 存在。

- [ ] **Step 4: 验证**

```bash
test -x build/bin/nekoshot && echo OK
grep -rn 'org\.kde\.spectacle\.private' src --include='*.qml' || echo "no old URI"
./build/bin/nekoshot --version
```

Expected: `OK`、`no old URI`、打印版本号。

- [ ] **Step 5: 提交**

```bash
git add src/CMakeLists.txt src/Gui
git commit -m "task 1: rename build target and QML module to nekoshot"
```

---

## Task 2: 产品标识（desktop / D-Bus / 配置 / 图标 / About）

**Files:**
- Rename: `dbus/org.kde.Spectacle.xml` → `dbus/org.nekoshot.NekoShot.xml`
- Rename: `dbus/app-org.kde.spectacle.service.in` → `dbus/app-org.nekoshot.NekoShot.service.in`
- Modify: `dbus/CMakeLists.txt`
- Rename: `desktop/org.kde.spectacle.desktop.cmake` → `desktop/org.nekoshot.NekoShot.desktop.cmake`
- Rename: `desktop/org.kde.spectacle.appdata.xml` → `desktop/org.nekoshot.NekoShot.appdata.xml`
- Rename: `desktop/spectacle.notifyrc` → `desktop/nekoshot.notifyrc`
- Modify: `desktop/CMakeLists.txt`
- Rename: `icons/sc-apps-spectacle.svg` → `icons/sc-apps-nekoshot.svg`
- Modify: `icons/CMakeLists.txt`（若存在引用）
- Rename: `src/Gui/SettingsDialog/spectacle.kcfg` → `src/Gui/SettingsDialog/nekoshot.kcfg`
- Modify: `src/Gui/SettingsDialog/settings.kcfgc`（`File=nekoshot.kcfg`）
- Modify: `src/CMakeLists.txt`（kcfg 安装路径）
- Modify: `src/Main.cpp`（`KAboutData`、`setOrganizationDomain`、`setDesktopFileName`、`registerService`）
- Modify: `src/SpectacleDBusAdapter.*`（`Q_CLASSINFO` 中的 D-Bus 接口名）

**Interfaces:**
- Consumes: Task 1 的 `nekoshot` 目标
- Produces: App ID `org.nekoshot.NekoShot`、配置文件名 `nekoshotrc`、D-Bus 接口 `org.nekoshot.NekoShot`

- [ ] **Step 1: 重命名文件**

```bash
git mv dbus/org.kde.Spectacle.xml dbus/org.nekoshot.NekoShot.xml
git mv dbus/app-org.kde.spectacle.service.in dbus/app-org.nekoshot.NekoShot.service.in
git mv desktop/org.kde.spectacle.desktop.cmake desktop/org.nekoshot.NekoShot.desktop.cmake
git mv desktop/org.kde.spectacle.appdata.xml desktop/org.nekoshot.NekoShot.appdata.xml
git mv desktop/spectacle.notifyrc desktop/nekoshot.notifyrc
git mv icons/sc-apps-spectacle.svg icons/sc-apps-nekoshot.svg
git mv src/Gui/SettingsDialog/spectacle.kcfg src/Gui/SettingsDialog/nekoshot.kcfg
```

- [ ] **Step 2: 更新标识**

`src/Main.cpp`：

```cpp
QCoreApplication::setOrganizationDomain(u"org.nekoshot"_s);
QCoreApplication::setApplicationName(u"nekoshot"_s);
app.setDesktopFileName(u"org.nekoshot.NekoShot"_s);
KAboutData aboutData(u"nekoshot"_s, i18n("NekoShot"), QStringLiteral(SPECTACLE_VERSION),
                     i18n("Screenshot utility for niri, based on Spectacle"), KAboutLicense::GPL_V2,
                     i18n("© 2015 Boudhayan Gupta and Spectacle contributors"));
QGuiApplication::setWindowIcon(QIcon::fromTheme(u"nekoshot"_s));
QDBusConnection::sessionBus().registerService(u"org.nekoshot.NekoShot"_s);
```

`src/SpectacleDBusAdapter.h` 的 `Q_CLASSINFO("D-Bus Interface", "org.nekoshot.NekoShot")`；`src/Main.cpp` 的 `registerObject` 保持 `/` 路径不变。

`dbus/CMakeLists.txt` 中两处 `ecm_generate_dbus_service_file` 合并为一个 `NAME org.nekoshot.NekoShot`、`EXECUTABLE ".../nekoshot --dbus"`、`SYSTEMD_SERVICE app-org.nekoshot.NekoShot.service`。

`desktop/CMakeLists.txt` 中所有路径与 kglobalaccel 符号链接改为 `org.nekoshot.NekoShot.desktop`、`nekoshot.notifyrc`、`org.nekoshot.NekoShot.appdata.xml`。

`desktop/org.nekoshot.NekoShot.desktop.cmake`：`Name=NekoShot`、`Exec=@KDE_INSTALL_FULL_BINDIR@/nekoshot`、`Icon=nekoshot`、`X-DBUS-ServiceName=org.nekoshot.NekoShot`，所有 Desktop Action 的 Exec 换成 `nekoshot`。

`src/Gui/SettingsDialog/settings.kcfgc`：`File=nekoshot.kcfg`。

`src/Gui/SettingsDialog/nekoshot.kcfg` 内首行：`<kcfgfile name="nekoshotrc" />`。

- [ ] **Step 3: 构建并检查安装产物**

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_X11=OFF -DBUILD_TESTING=ON
cmake --build build
DESTDIR=/tmp/nekoshot-install cmake --install build
find /tmp/nekoshot-install -type f | sort
```

Expected: 出现 `usr/bin/nekoshot`、`usr/share/applications/org.nekoshot.NekoShot.desktop`、`usr/share/dbus-1/services/org.nekoshot.NekoShot.service`、`usr/share/knotifications6/nekoshot.notifyrc`、`usr/share/metainfo/org.nekoshot.NekoShot.appdata.xml`。

- [ ] **Step 4: 验证 desktop 文件**

```bash
desktop-file-validate /tmp/nekoshot-install/usr/share/applications/org.nekoshot.NekoShot.desktop
grep -n 'Name=\|Exec=\|X-DBUS' /tmp/nekoshot-install/usr/share/applications/org.nekoshot.NekoShot.desktop | head
```

Expected: 校验通过；`Name=NekoShot`、`Exec=/usr/bin/nekoshot`。

- [ ] **Step 5: 提交**

```bash
git add -A
git commit -m "task 2: rename product identity to NekoShot"
```

---

## Task 3: NiriIpc JSON 解析

**Files:**
- Create: `src/Platforms/NiriIpc.h`
- Create: `src/Platforms/NiriIpc.cpp`
- Create: `tests/NiriIpcTest.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`（加入源文件）

**Interfaces:**
- Consumes: 无
- Produces:
```cpp
namespace NiriIpc {
struct Output { QString name; QRectF logicalRect; qreal scale; QString transform; };
struct Window { quint64 id = 0; QString title; QString appId; };

bool isAvailable();
QList<Output> parseOutputs(const QByteArray &json, QString *error = nullptr);
std::optional<Output> parseFocusedOutput(const QByteArray &json, QString *error = nullptr);
std::optional<Window> parseWindow(const QByteArray &json, QString *error = nullptr); // "null" -> nullopt
QList<Output> outputs(QString *error = nullptr);
std::optional<Output> focusedOutput(QString *error = nullptr);
std::optional<Window> focusedWindow(QString *error = nullptr);
std::optional<Window> pickWindow(QString *error = nullptr);
}
```
`isAvailable()` 在 `NIRI_SOCKET` 非空或 `XDG_CURRENT_DESKTOP` 含 `niri` 时返回 true。

- [ ] **Step 1: 写失败测试**

```cpp
// tests/NiriIpcTest.cpp
#include <QTest>
#include "Platforms/NiriIpc.h"

class NiriIpcTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testOutputs();
    void testFocusedOutput();
    void testWindow();
    void testCancelledPick();
};

static const char *s_outputs = R"({
  "eDP-1": {"name":"eDP-1","logical":{"x":0,"y":0,"width":1645,"height":1028,"scale":1.75,"transform":"Normal"}},
  "DP-1":  {"name":"DP-1","logical":{"x":-1920,"y":0,"width":1920,"height":1080,"scale":1.0,"transform":"Normal"}}
})";

void NiriIpcTest::testOutputs()
{
    QString error;
    const auto outputs = NiriIpc::parseOutputs(s_outputs, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(outputs.size(), 2);
    QCOMPARE(outputs[0].name, u"eDP-1"_s);
    QCOMPARE(outputs[0].scale, 1.75);
    QCOMPARE(outputs[0].logicalRect, QRectF(0, 0, 1645, 1028));
    QCOMPARE(outputs[1].logicalRect, QRectF(-1920, 0, 1920, 1080));
}

void NiriIpcTest::testFocusedOutput()
{
    const auto output = NiriIpc::parseFocusedOutput(R"({"name":"eDP-1","logical":{"x":0,"y":0,"width":1645,"height":1028,"scale":1.75,"transform":"Normal"}})");
    QVERIFY(output.has_value());
    QCOMPARE(output->name, u"eDP-1"_s);
}

void NiriIpcTest::testWindow()
{
    const auto window = NiriIpc::parseWindow(R"({"id":283,"title":"Chrome","app_id":"google-chrome","is_focused":true})");
    QVERIFY(window.has_value());
    QCOMPARE(window->id, quint64(283));
    QCOMPARE(window->appId, u"google-chrome"_s);
}

void NiriIpcTest::testCancelledPick()
{
    QString error;
    const auto window = NiriIpc::parseWindow("null", &error);
    QVERIFY(!window.has_value());
    QVERIFY(error.isEmpty());
}

QTEST_GUILESS_MAIN(NiriIpcTest)
#include "NiriIpcTest.moc"
```

- [ ] **Step 2: 运行测试确认失败**

```bash
cmake --build build --target niri_ipc_test 2>&1 | tail -5
```

Expected: 编译失败（`NiriIpc.h` 不存在）。

- [ ] **Step 3: 实现 NiriIpc**

`parseOutputs`：`QJsonDocument::fromJson` 失败时填 `error` 并返回空列表；遍历对象，读取 `name` 与 `logical.{x,y,width,height,scale,transform}`。`parseFocusedOutput` 读单个对象。`parseWindow` 对 `null` 返回 `nullopt` 且不设错误；对象缺 `id` 时设错误。`outputs()` 等函数通过 `QProcess` 执行 `niri msg --json outputs` / `focused-output` / `focused-window` / `pick-window`，超时 5000ms，失败时把 stderr 写入 `error`。

- [ ] **Step 4: 注册测试并运行**

`tests/CMakeLists.txt` 追加：

```cmake
ecm_add_test(NiriIpcTest.cpp ../src/Platforms/NiriIpc.cpp
    TEST_NAME "niri_ipc_test"
    LINK_LIBRARIES Qt::Test Qt::Core Qt::Gui
)
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_X11=OFF -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build -R niri_ipc_test --output-on-failure
```

Expected: PASS。

- [ ] **Step 5: 提交**

```bash
git add src/Platforms/NiriIpc.* tests/NiriIpcTest.cpp tests/CMakeLists.txt src/CMakeLists.txt
git commit -m "task 3: add niri IPC JSON parsing"
```

---

## Task 4: 图像合成与 grim 抓取

**Files:**
- Create: `src/Platforms/ImageComposition.h`
- Create: `src/Platforms/ImageComposition.cpp`
- Create: `src/Platforms/NiriCapture.h`
- Create: `src/Platforms/NiriCapture.cpp`
- Modify: `src/Platforms/ImagePlatformKWin.cpp`（改用共享的 `ImageComposition::combinedImage`）
- Create: `tests/NiriCaptureTest.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `NiriIpc::Output`
- Produces:
```cpp
namespace ImageComposition {
// 以 union 左上角为原点平移绘制；结果设置 devicePixelRatio、
// ImageMetaData::logicalXY（union 左上角）与 subGeometryList
QImage combinedImage(const QList<QImage> &images);
}
namespace NiriCapture {
QStringList grimArguments(const QString &outputName, bool includePointer);
QImage captureOutput(const NiriIpc::Output &output, bool includePointer, QString *error);
QImage captureAll(const QList<NiriIpc::Output> &outputs, bool includePointer, QString *error);
QStringList windowScreenshotArguments(quint64 windowId, const QString &path, bool includePointer);
QImage captureWindow(quint64 windowId, bool includePointer, qreal scale, QString *error);
}
```

- [ ] **Step 1: 写失败测试**

```cpp
// tests/NiriCaptureTest.cpp
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QTest>
#include "ImageMetaData.h"
#include "Platforms/ImageComposition.h"
#include "Platforms/NiriCapture.h"

class NiriCaptureTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testGrimArguments();
    void testWindowArguments();
    void testCombineSameDpr();
    void testCombineMixedDpr();
    void testCombineNegativeOrigin();
    void testCombineEmpty();
};

static QImage solid(int w, int h, qreal dpr, const QPointF &logical, QColor color)
{
    QImage image(w, h, QImage::Format_RGBA8888_Premultiplied);
    image.fill(color);
    image.setDevicePixelRatio(dpr);
    ImageMetaData::setLogicalXY(image, logical.x(), logical.y());
    return image;
}

void NiriCaptureTest::testGrimArguments()
{
    QCOMPARE(NiriCapture::grimArguments(u"eDP-1"_s, false),
             QStringList({"-t", "ppm", "-o", "eDP-1", "-"}));
    QCOMPARE(NiriCapture::grimArguments(u"eDP-1"_s, true),
             QStringList({"-t", "ppm", "-o", "eDP-1", "-c", "-"}));
}

void NiriCaptureTest::testWindowArguments()
{
    QCOMPARE(NiriCapture::windowScreenshotArguments(42, u"/tmp/x.png"_s, true),
             QStringList({"action", "screenshot-window", "--id", "42", "--path", "/tmp/x.png", "-d", "true", "-p", "true"}));
}

void NiriCaptureTest::testCombineSameDpr()
{
    QImage a = solid(800, 600, 1.0, {0, 0}, Qt::red);
    QImage b = solid(800, 600, 1.0, {800, 0}, Qt::blue);
    const QImage out = ImageComposition::combinedImage({a, b});
    QCOMPARE(out.size(), QSize(1600, 600));
    QCOMPARE(out.devicePixelRatio(), 1.0);
    QCOMPARE(ImageMetaData::logicalXY(out), QPointF(0, 0));
    QCOMPARE(ImageMetaData::subGeometryList(out).size(), 2);
    QCOMPARE(out.pixelColor(10, 10), QColor(Qt::red));
    QCOMPARE(out.pixelColor(810, 10), QColor(Qt::blue));
}

void NiriCaptureTest::testCombineMixedDpr()
{
    QImage a = solid(800, 600, 1.0, {0, 0}, Qt::red);
    QImage b = solid(800, 600, 1.75, {800, 0}, Qt::blue);
    const QImage out = ImageComposition::combinedImage({a, b});
    QCOMPARE(out.devicePixelRatio(), 2.0); // ceil(1.75)
    QCOMPARE(ImageMetaData::subGeometryList(out).size(), 2);
    QVERIFY(qAbs(out.width() - 2514) <= 2);
}

void NiriCaptureTest::testCombineNegativeOrigin()
{
    QImage a = solid(800, 600, 1.0, {-800, 0}, Qt::red);
    QImage b = solid(800, 600, 1.0, {0, 0}, Qt::blue);
    const QImage out = ImageComposition::combinedImage({a, b});
    QCOMPARE(out.size(), QSize(1600, 600));
    QCOMPARE(ImageMetaData::logicalXY(out), QPointF(-800, 0));
    QCOMPARE(out.pixelColor(10, 10), QColor(Qt::red));
    QCOMPARE(out.pixelColor(810, 10), QColor(Qt::blue));
}

void NiriCaptureTest::testCombineEmpty()
{
    QVERIFY(ImageComposition::combinedImage({}).isNull());
}

QTEST_GUILESS_MAIN(NiriCaptureTest)
#include "NiriCaptureTest.moc"
```

- [ ] **Step 2: 运行确认失败**

```bash
cmake --build build --target niri_capture_test 2>&1 | tail -5
```

Expected: 编译失败（文件不存在）。

- [ ] **Step 3: 实现**

`ImageComposition::combinedImage`：复制上游 `ImagePlatformKWin.cpp` 中的 `combinedImage`，唯一改动是把绘制坐标减去 `imageRect.topLeft()`，并在返回前 `ImageMetaData::setLogicalXY(finalImage, imageRect.topLeft().x(), imageRect.topLeft().y())`。

`NiriCapture::grimArguments`：返回 `{"-t","ppm","-o",<name>,"-c"(仅指针),"-"}`。

`NiriCapture::captureOutput`：`QProcess` 运行 `grim` + `grimArguments`，超时 5000ms；`QImage::fromData(stdout, "PPM")`；给图像设置 `devicePixelRatio(output.scale)`、`ImageMetaData::setLogicalXY(logicalRect.topLeft())`、`ImageMetaData::setScreen(name)`；失败写 `error`。

`NiriCapture::captureAll`：逐个输出调用 `captureOutput`，任一失败返回空图并写错误；最后 `ImageComposition::combinedImage(images)`。输出列表为空时写 `error = "No outputs"`。

`NiriCapture::windowScreenshotArguments`：返回 `{"action","screenshot-window","--id",<id>,"--path",<path>,"-d","true","-p",<"true"/"false">}`。

`NiriCapture::captureWindow`：`QTemporaryFile` 生成 `/tmp/nekoshot-XXXXXX.png`；运行 `niri` + 上述参数；读取文件；设置 `devicePixelRatio(scale)`；删除临时文件。

同时在 `ImagePlatformKWin.cpp` 中删除本地 `combinedImage`，改调用 `ImageComposition::combinedImage`。

- [ ] **Step 4: 注册测试并运行**

```cmake
ecm_add_test(NiriCaptureTest.cpp ../src/Platforms/NiriCapture.cpp ../src/Platforms/ImageComposition.cpp
    TEST_NAME "niri_capture_test"
    LINK_LIBRARIES Qt::Test Qt::Core Qt::Gui
)
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_X11=OFF -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build -R niri_capture_test --output-on-failure
```

Expected: PASS。

- [ ] **Step 5: 提交**

```bash
git add src/Platforms/ImageComposition.* src/Platforms/NiriCapture.* src/Platforms/ImagePlatformKWin.cpp tests/NiriCaptureTest.cpp tests/CMakeLists.txt src/CMakeLists.txt
git commit -m "task 4: add image composition and grim capture helpers"
```

---

## Task 5: ImagePlatformNiri 核心与平台加载

**Files:**
- Create: `src/Platforms/NiriPlatform.h`
- Create: `src/Platforms/NiriPlatform.cpp`
- Create: `src/Platforms/ImagePlatformNiri.h`
- Create: `src/Platforms/ImagePlatformNiri.cpp`
- Modify: `src/Platforms/PlatformLoader.cpp`
- Modify: `src/Main.cpp`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `NiriIpc::*`、`NiriCapture::*`
- Produces:
```cpp
namespace NiriPlatform {
bool isNiriSession();
bool useLayerShellOverlay();          // Task 6 使用，本任务先返回 isNiriSession()
bool hasGlobalShortcuts();            // Task 10 使用，本任务先返回 !isNiriSession()
}
class ImagePlatformNiri : public ImagePlatform {
    Q_OBJECT
public:
    explicit ImagePlatformNiri(QObject *parent = nullptr);
    GrabModes supportedGrabModes() const override;
    ShutterModes supportedShutterModes() const override;
public Q_SLOTS:
    void doGrab(ShutterMode, GrabMode, bool includePointer, bool includeDecorations, bool includeShadow) override;
};
```

- [ ] **Step 1: 写失败测试**

在 `tests/NiriCaptureTest.cpp` 增加：

```cpp
#include "Platforms/ImagePlatformNiri.h"

void testSupportedModes()
{
    ImagePlatformNiri platform;
    const auto modes = platform.supportedGrabModes();
    QVERIFY(modes.testFlag(ImagePlatform::AllScreens));
    QVERIFY(modes.testFlag(ImagePlatform::CurrentScreen));
    QVERIFY(modes.testFlag(ImagePlatform::PerScreenImageNative));
    QVERIFY(!modes.testFlag(ImagePlatform::ActiveWindow)); // Task 7 再打开
    QCOMPARE(platform.supportedShutterModes(), ImagePlatform::ShutterModes(ImagePlatform::Immediate));
}
```

测试目标需链接 `../src/Platforms/ImagePlatformNiri.cpp ../src/Platforms/ImagePlatform.cpp ../src/Platforms/NiriPlatform.cpp ../src/Platforms/NiriIpc.cpp ../src/Platforms/NiriCapture.cpp ../src/Platforms/ImageComposition.cpp` 与 `Qt::Gui Qt::Concurrent KF6::I18n`。

- [ ] **Step 2: 运行确认失败**

```bash
cmake --build build --target niri_capture_test 2>&1 | tail -5
```

Expected: 编译失败。

- [ ] **Step 3: 实现**

`NiriPlatform::isNiriSession()` 调用 `NiriIpc::isAvailable()`。

`ImagePlatformNiri::supportedGrabModes()` 返回 `AllScreens | CurrentScreen | PerScreenImageNative | AllScreensScaled`；`supportedShutterModes()` 返回 `Immediate`。

`doGrab` 根据 `grabMode` 选择：
- `AllScreens` / `AllScreensScaled` / `PerScreenImageNative` → `NiriIpc::outputs()` + `NiriCapture::captureAll`（后两者走 `newCroppableScreenshotTaken`，`AllScreens` 走 `newScreenshotTaken`）
- `CurrentScreen` → `NiriIpc::focusedOutput()` + `NiriCapture::captureOutput`
- 其他 → `newScreenshotFailed(i18n("Unsupported capture mode"))`

抓取在 `QtConcurrent::run` 中执行，结果用 `QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)` 发信号；空图或错误时发 `newScreenshotFailed(error)`。

若输出列表中任一输出的 `logicalRect.x() < 0` 或 `y() < 0`，直接发 `newScreenshotFailed(i18n("Negative-origin monitor layouts are not supported"))`：v1 的遮罩视口按全局逻辑坐标绘制，负原点会导致选区错位。

若 `QStandardPaths::findExecutable("grim").isEmpty()`，发 `newScreenshotFailed(i18n("grim is not installed"))`，不启动进程。

`PlatformLoader::loadImagePlatform()` 在 `SPECTACLE_IMAGE_PLATFORM` 覆盖之后、KWin 检测之前加入：

```cpp
if (NiriPlatform::isNiriSession()) {
    return std::make_unique<ImagePlatformNiri>();
}
```

`Main.cpp` 的 Wayland 检查改为：

```cpp
if (KWindowSystem::isPlatformWayland() && !ScreenShotEffect::isLoaded() && !NiriPlatform::isNiriSession()) {
    // 原来的错误提示
}
```

- [ ] **Step 4: 构建与手动验证**

```bash
cmake --build build
ctest --test-dir build -R niri_capture_test --output-on-failure
./build/bin/nekoshot -b -f -o /tmp/nekoshot-all.png -n
identify /tmp/nekoshot-all.png
./build/bin/nekoshot -b -m -o /tmp/nekoshot-cur.png -n
identify /tmp/nekoshot-cur.png
PATH=/nope ./build/bin/nekoshot -b -f -o /tmp/should-not-exist.png -n; echo "exit=$?"
```

Expected: 两张图均为 2880×1800（单输出 eDP-1）；测试 PASS；缺 grim 时非零退出且不产生文件。

- [ ] **Step 5: 提交**

```bash
git add src/Platforms/NiriPlatform.* src/Platforms/ImagePlatformNiri.* src/Platforms/PlatformLoader.cpp src/Main.cpp src/CMakeLists.txt tests/NiriCaptureTest.cpp tests/CMakeLists.txt
git commit -m "task 5: add niri image platform backend"
```

---

## Task 6: Layer-shell 遮罩

**Files:**
- Modify: `src/Gui/CaptureWindow.cpp`
- Modify: `src/Gui/CaptureWindow.h`（若需要保存是否使用 layer-shell）
- Modify: `src/Platforms/NiriPlatform.cpp`

**Interfaces:**
- Consumes: `NiriPlatform::useLayerShellOverlay()`
- Produces: niri 会话下 CaptureWindow 以 layer surface 呈现

- [ ] **Step 1: 实现配置**

`NiriPlatform::useLayerShellOverlay()` 返回 `isNiriSession()`。

`CaptureWindow` 构造函数中，在设置 flags 之后、`syncGeometryWithScreen()` 之前：

```cpp
if (NiriPlatform::useLayerShellOverlay()) {
    if (auto *layer = LayerShellQt::Window::get(this)) {
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        layer->setAnchors(LayerShellQt::Window::Anchors::fromInt(
            LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorBottom
            | LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight));
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
        layer->setScreen(m_screenToFollow);
        layer->setCloseOnDismissed(true);
    }
}
```

niri 会话下不要设置 `Qt::WindowFullScreen`（用 `setFlags(Qt::FramelessWindowHint)` 即可）；`syncGeometryWithScreen()` 中不要设置 `setMinimumSize/setMaximumSize`，只 `setGeometry(screenRect)` 与 `setScreen()`。

- [ ] **Step 2: 构建**

```bash
cmake --build build
```

- [ ] **Step 3: 手动验证**

```bash
./build/bin/nekoshot -r
```

Expected：遮罩立即出现（无窗口动画、不切换工作区），覆盖顶栏；按 `Esc` 取消。另开终端执行 `niri msg layers` 应看到 namespace 为 `nekoshot` 或 Qt 默认的 layer 名称。

- [ ] **Step 4: 多屏/缩放验证（有第二块屏时）**

在 100% 与 175% 混合布局下框选一个小矩形，确认选区尺寸标签与实际裁剪图像尺寸一致。

- [ ] **Step 5: 提交**

```bash
git add src/Gui/CaptureWindow.* src/Platforms/NiriPlatform.*
git commit -m "task 6: use layer-shell overlay for capture windows on niri"
```

---

## Task 7: 活动窗口抓取

**Files:**
- Modify: `src/Platforms/ImagePlatformNiri.cpp`
- Modify: `tests/NiriCaptureTest.cpp`

**Interfaces:**
- Consumes: `NiriIpc::focusedWindow()`、`NiriCapture::captureWindow()`
- Produces: `GrabMode::ActiveWindow` 可用

- [ ] **Step 1: 写测试**

在 `testSupportedModes` 中断言 `modes.testFlag(ImagePlatform::ActiveWindow)` 为 true（本任务实现后）。

- [ ] **Step 2: 实现**

`doGrab` 增加 `ActiveWindow` 分支：`focusedWindow()`；为空 → `newScreenshotFailed(i18n("No focused window"))`；否则读取焦点输出 scale（缺失时用 `qGuiApp->primaryScreen()->devicePixelRatio()`），调用 `captureWindow(id, includePointer, scale, &error)`。临时文件与剪贴板副作用在本任务接受，Task 9 处理恢复。

`supportedGrabModes()` 加入 `ActiveWindow`。

- [ ] **Step 3: 构建与手动验证**

```bash
cmake --build build
./build/bin/nekoshot -b -a -o /tmp/nekoshot-window.png -n
identify /tmp/nekoshot-window.png
```

Expected：得到当前焦点窗口的 PNG（尺寸约为窗口逻辑尺寸 × 1.75，含阴影）。

- [ ] **Step 4: 提交**

```bash
git add src/Platforms/ImagePlatformNiri.cpp tests/NiriCaptureTest.cpp
git commit -m "task 7: support active window capture via niri"
```

---

## Task 8: 鼠标下窗口抓取与取消

**Files:**
- Modify: `src/Platforms/ImagePlatformNiri.cpp`
- Modify: `tests/NiriCaptureTest.cpp`

**Interfaces:**
- Consumes: `NiriIpc::pickWindow()`
- Produces: `GrabMode::WindowUnderCursor` 可用；取消时发 `newScreenshotCanceled`

- [ ] **Step 1: 写测试**

`parseWindow("null")` 已在 Task 3 覆盖；本任务在 `testSupportedModes` 中断言 `WindowUnderCursor` 为 true。

- [ ] **Step 2: 实现**

`doGrab` 增加 `WindowUnderCursor`：`pickWindow()`；`nullopt` 且无错误 → `newScreenshotCanceled()`；有错误 → `newScreenshotFailed(error)`；否则同 ActiveWindow 抓取。

`supportedGrabModes()` 加入 `WindowUnderCursor`。

- [ ] **Step 3: 手动验证**

```bash
cmake --build build
./build/bin/nekoshot -b -u -o /tmp/nekoshot-under.png -n
```

点击一个窗口，确认生成图片；再运行一次并按 `Esc`，确认进程正常退出且没有新文件。

- [ ] **Step 4: 提交**

```bash
git add src/Platforms/ImagePlatformNiri.cpp tests/NiriCaptureTest.cpp
git commit -m "task 8: support window-under-cursor capture via niri picker"
```

---

## Task 9: 剪贴板快照/恢复

**Files:**
- Create: `src/Platforms/ClipboardGuard.h`
- Create: `src/Platforms/ClipboardGuard.cpp`
- Modify: `src/Platforms/NiriCapture.cpp`（窗口抓取前使用）
- Modify: `src/Gui/SettingsDialog/nekoshot.kcfg`
- Create: `tests/ClipboardGuardTest.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `wl-paste` / `wl-copy`
- Produces:
```cpp
class ClipboardGuard {
public:
    explicit ClipboardGuard(qint64 maxBytes = 32 * 1024 * 1024);
    ~ClipboardGuard();              // 调用 restore()
    void restore();                 // 幂等；每个命令超时 1000ms
    bool hasSnapshot() const;
    static bool isAvailable();
    static QList<QPair<QString, QByteArray>>
        limitEntries(const QList<QPair<QString, QByteArray>> &entries, qint64 maxBytes);
};
```

- [ ] **Step 1: 写测试**

```cpp
void testLimitEntriesKeepsOrderUntilCap()
{
    const QList<QPair<QString, QByteArray>> entries = {
        {u"text/plain"_s, QByteArray(10, 'a')},
        {u"image/png"_s, QByteArray(20, 'b')},
        {u"text/html"_s, QByteArray(30, 'c')},
    };
    const auto kept = ClipboardGuard::limitEntries(entries, 25);
    QCOMPARE(kept.size(), 1);
    QCOMPARE(kept.first().first, u"text/plain"_s);
}
```

- [ ] **Step 2: 运行确认失败**

```bash
cmake --build build --target clipboard_guard_test 2>&1 | tail -5
```

Expected: 编译失败。

- [ ] **Step 3: 实现**

构造函数：`wl-paste --list-types` 取 MIME 列表；逐个 `wl-paste --type <mime>` 读取（各 1000ms 超时）；`limitEntries` 截断；保存结果。`restore()`：对每个条目 `wl-copy --type <mime>` 写入数据，最后 `wl-copy --clear` 不需要；置空快照。析构调用 `restore()`。

`NiriCapture::captureWindow` 在调用 `niri` 之前构造 `ClipboardGuard`（仅当 `Settings::restoreClipboardAfterWindowCapture()` 为 true），命令结束后 guard 析构恢复。

`nekoshot.kcfg` 的 `GuiConfig` 组新增：

```xml
<entry name="restoreClipboardAfterWindowCapture" type="Bool">
    <default>true</default>
</entry>
```

- [ ] **Step 4: 构建与测试**

```bash
cmake --build build
ctest --test-dir build -R clipboard_guard_test --output-on-failure
```

Expected: PASS。

- [ ] **Step 5: 手动验证**

先复制一段文本，再执行 `./build/bin/nekoshot -b -a -n`，随后 `wl-paste` 应仍然是原文本。

- [ ] **Step 6: 提交**

```bash
git add src/Platforms/ClipboardGuard.* src/Platforms/NiriCapture.cpp src/Gui/SettingsDialog/nekoshot.kcfg tests/ClipboardGuardTest.cpp tests/CMakeLists.txt src/CMakeLists.txt
git commit -m "task 9: preserve clipboard around niri window captures"
```

---

## Task 10: KGlobalAccel 门控与 niri binds

**Files:**
- Modify: `src/Platforms/NiriPlatform.cpp`（`hasGlobalShortcuts()`）
- Modify: `src/CaptureModeModel.cpp`
- Modify: `src/RecordingModeModel.cpp`
- Modify: `src/Gui/RecordingModeMenu.cpp`
- Modify: `src/SpectacleCore.cpp`
- Create: `contrib/niri-binds.kdl`
- Modify: `README.md`

**Interfaces:**
- Consumes: `NiriPlatform::hasGlobalShortcuts()`
- Produces: niri 下不再调用 KGlobalAccel；提供官方 binds 片段

- [ ] **Step 1: 实现门控**

`NiriPlatform::hasGlobalShortcuts()` 返回 `!isNiriSession()`。

`CaptureModeModel.cpp` 的 `actionShortcutsToString` 开头加：

```cpp
if (!NiriPlatform::hasGlobalShortcuts()) {
    return {};
}
```

`RecordingModeModel.cpp`、`Gui/RecordingModeMenu.cpp`、`SpectacleCore.cpp` 中所有 `KGlobalAccel::self()` 调用外层加同样的判断（无快捷键时使用空的 `QKeySequence`/跳过记录停止快捷键提示）。

- [ ] **Step 2: 写 binds 文件**

`contrib/niri-binds.kdl`：

```kdl
binds {
    Print       { spawn "nekoshot" "-r"; }
    Ctrl+Print  { spawn "nekoshot" "-m"; }
    Alt+Print   { spawn "nekoshot" "-a"; }
    Mod+Shift+S { spawn "nekoshot" "-r"; }
    Mod+Shift+U { spawn "nekoshot" "-u"; }
    Mod+Shift+R { spawn "nekoshot" "-R" "r"; }
}
```

README 增加 "niri 快捷键" 一节，说明需要删除 niri 默认的 `Print { screenshot; }` 并 include 此文件。

- [ ] **Step 3: 构建与手动验证**

```bash
cmake --build build
./build/bin/nekoshot -l &
sleep 2
journalctl --user -n 50 --no-pager 2>/dev/null | grep -i kglobalaccel || echo "no kglobalaccel noise"
```

Expected: 构建成功；设置窗口的快捷键列显示为空，且日志无 KGlobalAccel 相关报错。

- [ ] **Step 4: 提交**

```bash
git add src/Platforms/NiriPlatform.cpp src/CaptureModeModel.cpp src/RecordingModeModel.cpp src/Gui/RecordingModeMenu.cpp src/SpectacleCore.cpp contrib/niri-binds.kdl README.md
git commit -m "task 10: gate KGlobalAccel on niri and ship niri binds"
```

---

## Task 11: 常驻模式

**Files:**
- Modify: `src/CommandLineOptions.h`
- Modify: `src/CommandLineOptions.cpp`（`allOptions` 加入新选项）
- Modify: `src/SpectacleCore.h`
- Modify: `src/SpectacleCore.cpp`
- Modify: `src/Main.cpp`

**Interfaces:**
- Consumes: `KDBusService` 单实例
- Produces: `--daemon` 选项；`bool SpectacleCore::isDaemonMode() const`

- [ ] **Step 1: 实现**

`CommandLineOptions.h`：

```cpp
const QCommandLineOption daemon = {
    {u"daemon"_s},
    i18n("Stay running and wait for D-Bus requests")
};
```

`SpectacleCore::activate` 解析后，若设置了 `daemon`：`m_daemonMode = true; m_startMode = StartMode::DBus;`。

`SpectacleCore::isDaemonMode()` 返回 `m_daemonMode`。

`Main.cpp` 中把退出连接改为：

```cpp
if (!spectacleCore->isDaemonMode()) {
    QObject::connect(spectacleCore, &SpectacleCore::allDone, &app, &QCoreApplication::quit, Qt::QueuedConnection);
}
```

注意：`activate` 在每次 D-Bus 调用时都会重新解析参数，`--daemon` 只影响启动参数；`m_daemonMode` 一旦为 true，后续 `activate()` 不得把它重置为 false。

- [ ] **Step 2: 构建与手动验证**

```bash
cmake --build build
./build/bin/nekoshot --daemon &
sleep 2
gdbus call --session --dest org.nekoshot.NekoShot --object-path / --method org.nekoshot.NekoShot.RectangularRegion 0
```

Expected: 遮罩出现；完成后进程仍在；重复调用第二次仍然工作。

- [ ] **Step 3: 提交**

```bash
git add src/CommandLineOptions.* src/SpectacleCore.* src/Main.cpp
git commit -m "task 11: add daemon mode for zero-latency hotkeys"
```

---

## Task 12: 停止录屏的 D-Bus 与 CLI

**Files:**
- Modify: `dbus/org.nekoshot.NekoShot.xml`
- Modify: `src/SpectacleDBusAdapter.h`
- Modify: `src/SpectacleDBusAdapter.cpp`
- Modify: `src/CommandLineOptions.h/.cpp`
- Modify: `src/SpectacleCore.cpp`

**Interfaces:**
- Consumes: `VideoPlatform::finishRecording()`
- Produces: D-Bus `StopRecording()`；CLI `--stop-recording`

- [ ] **Step 1: 实现**

XML 增加：

```xml
<method name="StopRecording">
    <doc:doc><doc:description><doc:para>Stops the current recording.</doc:para></doc:description></doc:doc>
</method>
```

`SpectacleCore` 若没有公开访问器，增加 `VideoPlatform *videoPlatform() const { return m_videoPlatform.get(); }`。

`SpectacleDBusAdapter` 增加 `void StopRecording();`，实现为：

```cpp
if (parent()->videoPlatform()->isRecording()) {
    parent()->finishRecording();
}
```

（`SpectacleCore` 需要提供 `videoPlatform()` 访问器；若已有则复用。）

CLI 增加 `--stop-recording`；`SpectacleCore::activate` 开头检查该选项，若正在录屏则 `finishRecording()`；非常驻模式下随后 `Q_EMIT allDone()` 退出，常驻模式保持运行。

同时在 `contrib/niri-binds.kdl` 追加：

```kdl
    Mod+Shift+X { spawn "nekoshot" "--stop-recording"; }
```

- [ ] **Step 2: 构建与手动验证**

```bash
cmake --build build
./build/bin/nekoshot --daemon &
gdbus call --session --dest org.nekoshot.NekoShot --object-path / --method org.nekoshot.NekoShot.RecordScreen 0
sleep 3
gdbus call --session --dest org.nekoshot.NekoShot --object-path / --method org.nekoshot.NekoShot.StopRecording
ls -lt ~/Videos | head -3
```

Expected: 录屏开始（托盘出现）、停止后生成视频文件。

- [ ] **Step 3: 提交**

```bash
git add dbus/org.nekoshot.NekoShot.xml src/SpectacleDBusAdapter.* src/CommandLineOptions.* src/SpectacleCore.*
git commit -m "task 12: add stop recording via D-Bus and CLI"
```

---

## Task 13: wf-recorder 录屏后端（区域/屏幕）

**Files:**
- Create: `src/Platforms/VideoPlatformNiri.h`
- Create: `src/Platforms/VideoPlatformNiri.cpp`
- Modify: `src/Platforms/PlatformLoader.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/WfRecorderArgsTest.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `NiriIpc::focusedOutput()`、`VideoPlatform` 基类
- Produces:
```cpp
class VideoPlatformNiri : public VideoPlatform {
    Q_OBJECT
public:
    explicit VideoPlatformNiri(QObject *parent = nullptr);
    RecordingModes supportedRecordingModes() const override; // Screen | Region
    Formats supportedFormats() const override;               // WebM_VP9 | MP4_H264（按 ffmpeg 编码器探测）
    bool isRecordingAudio() const override;
    void startRecording(const QUrl &fileUrl, RecordingMode mode,
                        const QVariantMap &options, bool includePointer) override;
    void finishRecording() override;

    static QStringList argumentsFor(RecordingMode mode, const QString &outputPath,
                                    const QRect &region, Format format,
                                    bool systemAudio, const QString &microphone);
};
```

- [ ] **Step 1: 写失败测试**

```cpp
void testRegionArguments()
{
    const auto args = VideoPlatformNiri::argumentsFor(
        VideoPlatform::Region, u"/tmp/out.webm"_s, QRect(10, 20, 300, 200),
        QString(), VideoPlatform::WebM_VP9, false, QString());
    QCOMPARE(args, QStringList({"-f", "/tmp/out.webm", "-g", "10,20 300x200", "-c", "libvpx-vp9"}));
}

void testScreenArgumentsWithAudio()
{
    const auto args = VideoPlatformNiri::argumentsFor(
        VideoPlatform::Screen, u"/tmp/out.mp4"_s, QRect(0, 0, 1645, 1028),
        VideoPlatform::MP4_H264, true, QString());
    QCOMPARE(args, QStringList({"-f", "/tmp/out.mp4", "-g", "0,0 1645x1028", "-c", "libx264", "--audio"}));
}
```

（`argumentsFor` 不接收输出名；Screen 模式由调用方从 `NiriIpc::focusedOutput().logicalRect` 构造 `QRect`，再传给 `argumentsFor`。）

- [ ] **Step 2: 实现**

`argumentsFor` 规则：
- 基础：`{"-f", outputPath}`；Region/Screen 都用 `-g "<x>,<y> <w>x<h>"`；Scale/Region 的矩形由调用方给出。
- 格式：`WebM_VP9` → `{"-c","libvpx-vp9"}`；`MP4_H264` → `{"-c","libx264"}`。
- 音频：`systemAudio` → 追加 `--audio`；`microphone` 非空 → 追加 `--audio=<microphone>`。

`startRecording`：用 `NiriIpc::focusedOutput()` 取得屏幕几何（Screen 模式）；Region 从 `options.value("rect").toRect()` 取；按 `Settings::preferredVideoFormat()` 选格式；`QProcess::start("wf-recorder", args)`；设置状态 `Recording` 并启动计时。`finishRecording`：`::kill(processId(), SIGINT)`，等待进程结束（超时 10s 后 `kill()`）；退出码 0 → `setRecordingState(Finished)` 并 `recordingSaved(fileUrl)`，否则 `recordingFailed(stderr)`。

`PlatformLoader::loadVideoPlatform()`：niri 会话且 `QStandardPaths::findExecutable("wf-recorder")` 非空 → `VideoPlatformNiri`；找不到 → `VideoPlatformNull(i18n("wf-recorder is not installed"))`。

- [ ] **Step 3: 构建与测试**

```bash
cmake --build build
ctest --test-dir build -R wf_recorder_args_test --output-on-failure
```

Expected: PASS。

- [ ] **Step 4: 手动验证**

```bash
./build/bin/nekoshot --daemon &
gdbus call --session --dest org.nekoshot.NekoShot --object-path / --method org.nekoshot.NekoShot.RecordRegion 0
```

框选后录制 5 秒，再调用 `StopRecording`；`ffprobe` 检查文件。

- [ ] **Step 5: 提交**

```bash
git add src/Platforms/VideoPlatformNiri.* src/Platforms/PlatformLoader.cpp src/CMakeLists.txt tests/WfRecorderArgsTest.cpp tests/CMakeLists.txt
git commit -m "task 13: add wf-recorder based video platform"
```

---

## Task 14: 窗口录屏探测与降级

**Files:**
- Modify: `src/Platforms/VideoPlatformNiri.cpp`
- Modify: `README.md`

**Interfaces:**
- Consumes: niri 的 Mutter ScreenCast（`org.gnome.Mutter.ScreenCast` 支持 RecordWindow）
- Produces: 明确的窗口录屏可用性结论

- [ ] **Step 1: 探测 portal 路径**

```bash
SPECTACLE_VIDEO_PLATFORM=VideoPlatformWayland ./build/bin/nekoshot --daemon &
gdbus call --session --dest org.nekoshot.NekoShot --object-path / --method org.nekoshot.NekoShot.RecordWindow 0
```

观察是否出现 xdg-desktop-portal 的窗口选择提示并生成文件。

- [ ] **Step 2: 按结果落地**

- 若可用：在 `VideoPlatformNiri::supportedRecordingModes()` 之外，`PlatformLoader` 对 Window 模式保留 `VideoPlatformWayland`（或在 `VideoPlatformNiri` 内代理调用 portal 实现），并在 README 记录。
- 若不可用：`VideoPlatformNiri::supportedRecordingModes()` 保持 `Screen | Region`，README 的 "已知限制" 写明窗口录屏在 v1 不可用及原因。

- [ ] **Step 3: 构建与验证**

```bash
cmake --build build
./build/bin/nekoshot -l &
```

Expected: 录屏菜单只显示实际可用的模式，不出现点了没反应的选项。

- [ ] **Step 4: 提交**

```bash
git add src/Platforms/VideoPlatformNiri.cpp README.md
git commit -m "task 14: document window recording availability on niri"
```

---

## Task 15: PKGBUILD 与安装验证

**Files:**
- Create: `packaging/PKGBUILD`

**Interfaces:**
- Consumes: Task 2 的安装产物
- Produces: 可用 `makepkg -f` 构建的本地包

- [ ] **Step 1: 写 PKGBUILD**

```bash
pkgname=nekoshot
pkgver=6.8.80
pkgrel=1
pkgdesc="Spectacle-based screenshot utility for niri"
arch=(x86_64)
url="https://github.com/NekoRain404/NekoShot"
license=(GPL-2.0-or-later)
depends=(grim kquickimageeditor niri qt6-base qt6-declarative qt6-wayland qt6-multimedia kcoreaddons kconfig ki18n kguiaddons kwindowsystem knotifications kdbusaddons kglobalaccel kxmlgui kio kirigami kstatusnotifieritem prison kcrash kpipewire layer-shell-qt tesseract)
makedepends=(cmake ninja extra-cmake-modules kdoctools qt6-tools wayland plasma-wayland-protocols)
conflicts=('spectacle')
optdepends=('wf-recorder: screen recording' 'tesseract-data-eng: English OCR')
source=()
sha256sums=()

build() {
  cmake -S "$startdir/.." -B "$startdir/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DWITH_X11=OFF -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build "$startdir/build"
}

package() {
  DESTDIR="$pkgdir" cmake --install "$startdir/build"
}
```

- [ ] **Step 2: 构建包**

```bash
cd packaging && makepkg -f --nodeps
ls -l nekoshot-*.pkg.tar.zst
pacman -Qlp nekoshot-*.pkg.tar.zst | head
```

Expected: 生成包；文件列表含 `/usr/bin/nekoshot` 与 `org.nekoshot.NekoShot.desktop`。

- [ ] **Step 3: 提交**

```bash
git add packaging/PKGBUILD
git commit -m "task 15: add Arch PKGBUILD"
```

---

## Task 16: 文档、手动矩阵与性能记录

**Files:**
- Modify: `README.md`
- Create: `docs/validation.md`

**Interfaces:**
- Consumes: 全部前序任务
- Produces: 可复现的验收记录

- [ ] **Step 1: 运行手动矩阵并记录**

在 `docs/validation.md` 中逐项记录结果：模式（区域/全屏/当前屏/活动窗口/鼠标下窗口）× 单屏 × 100%/175% × 指针开/关 × 成功/取消/错误；记录每个场景的截图或 `identify` 输出。

- [ ] **Step 2: 记录性能**

```bash
/usr/bin/time -f 'capture %e s' ./build/bin/nekoshot -b -f -o /tmp/perf.png -n
```

记录冷启动与常驻（`--daemon` + `gdbus`）各三次的耗时，写进 `docs/validation.md`。目标：常驻热键→遮罩 ≤250ms，冷启动 ≤600ms。

- [ ] **Step 3: 更新 README**

写明：NekoShot 是什么、依赖、构建、安装、niri binds、已知限制（窗口录屏、阴影不可关、负原点布局、Purpose 不支持）。

- [ ] **Step 4: 提交**

```bash
git add README.md docs/validation.md
git commit -m "task 16: document build, binds, validation and limits"
```

---

## Task 17: 上游同步说明

**Files:**
- Create: `docs/upstream.md`

**Interfaces:**
- Consumes: 现有 `upstream` remote
- Produces: 后续跟进 Spectacle 的操作说明

- [ ] **Step 1: 写文档**

记录：`upstream` remote 指向 `https://invent.kde.org/graphics/spectacle.git`；NekoShot 改动集中在 `src/Platforms/Niri*`、`ImageComposition`、`CaptureWindow`、`PlatformLoader`、`Main`、`CommandLineOptions`、`build/dbus/desktop` 标识；合并上游时的冲突预期与处理顺序。

- [ ] **Step 2: 提交**

```bash
git add docs/upstream.md
git commit -m "task 17: document upstream sync strategy"
```
