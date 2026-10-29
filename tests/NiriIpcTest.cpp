#include "Platforms/NiriIpc.h"
#include <QTest>

using namespace Qt::StringLiterals;

class NiriIpcTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testOutputs();
    void testFocusedOutput();
    void testWindow();
    void testCancelledPick();
    void testMalformedJson();
    void testNonObjectDocument();
    void testMissingWindowId();
    void testDisabledOutput();
    void testNoFocusedOutput();
    void testWindowWorkspaceId();
    void testWorkspaces();
    void testWorkspacesMalformed();
    void testPhysicalSize();
};

static const char *s_outputs = R"({
  "eDP-1": {"name":"eDP-1","logical":{"x":0,"y":0,"width":1645,"height":1028,"scale":1.75,"transform":"normal"}},
  "DP-1":  {"name":"DP-1","logical":{"x":-1920,"y":0,"width":1920,"height":1080,"scale":1.0,"transform":"normal"}}
})";

static const NiriIpc::Output *findOutput(const QList<NiriIpc::Output> &outputs, const QString &name)
{
    for (const auto &output : outputs) {
        if (output.name == name) {
            return &output;
        }
    }
    return nullptr;
}

void NiriIpcTest::testOutputs()
{
    QString error;
    const auto outputs = NiriIpc::parseOutputs(s_outputs, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(outputs.size(), 2);

    const auto *eDP1 = findOutput(outputs, u"eDP-1"_s);
    const auto *dp1 = findOutput(outputs, u"DP-1"_s);
    QVERIFY(eDP1);
    QVERIFY(dp1);
    QCOMPARE(eDP1->scale, 1.75);
    QCOMPARE(eDP1->transform, u"normal"_s);
    QCOMPARE(eDP1->logicalRect, QRectF(0, 0, 1645, 1028));
    QCOMPARE(dp1->logicalRect, QRectF(-1920, 0, 1920, 1080));
}

void NiriIpcTest::testFocusedOutput()
{
    const auto output = NiriIpc::parseFocusedOutput(R"({"name":"eDP-1","logical":{"x":0,"y":0,"width":1645,"height":1028,"scale":1.75,"transform":"normal"}})");
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

void NiriIpcTest::testMalformedJson()
{
    QString error;
    const auto outputs = NiriIpc::parseOutputs(R"({"eDP-1": )", &error);
    QVERIFY(outputs.isEmpty());
    QVERIFY(!error.isEmpty());
}

void NiriIpcTest::testNonObjectDocument()
{
    QString error;
    const auto outputs = NiriIpc::parseOutputs("[]", &error);
    QVERIFY(outputs.isEmpty());
    QVERIFY(!error.isEmpty());
}

void NiriIpcTest::testMissingWindowId()
{
    QString error;
    const auto window = NiriIpc::parseWindow(R"({"title":"Chrome","app_id":"google-chrome"})", &error);
    QVERIFY(!window.has_value());
    QVERIFY(!error.isEmpty());
}

void NiriIpcTest::testDisabledOutput()
{
    QString error;
    const auto outputs = NiriIpc::parseOutputs(R"({"HDMI-A-1":{"name":"HDMI-A-1","logical":null}})", &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(outputs.isEmpty());
}

void NiriIpcTest::testNoFocusedOutput()
{
    QString error;
    const auto output = NiriIpc::parseFocusedOutput("null", &error);
    QVERIFY(!output.has_value());
    QVERIFY(error.isEmpty());
}

void NiriIpcTest::testWindowWorkspaceId()
{
    const auto window = NiriIpc::parseWindow(R"({"id":7,"workspace_id":42,"title":"Term","app_id":"kitty"})");
    QVERIFY(window.has_value());
    QCOMPARE(window->workspaceId, quint64(42));
}

void NiriIpcTest::testWorkspaces()
{
    static const char *s_workspaces = R"([
        {"id":1,"output":"eDP-1","is_active":false,"is_focused":false},
        {"id":42,"output":"DP-1","is_active":true,"is_focused":true},
        {"id":7,"output":"","is_active":false,"is_focused":false}
    ])";
    QString error;
    const auto workspaces = NiriIpc::parseWorkspaces(s_workspaces, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(workspaces.size(), 3);
    QCOMPARE(workspaces[1].id, quint64(42));
    QCOMPARE(workspaces[1].output, u"DP-1"_s);
    QVERIFY(workspaces[1].isActive);
    QVERIFY(workspaces[1].isFocused);
    QCOMPARE(NiriIpc::outputNameForWorkspace(workspaces, 42), u"DP-1"_s);
    QCOMPARE(NiriIpc::outputNameForWorkspace(workspaces, 1), u"eDP-1"_s);
    QVERIFY(NiriIpc::outputNameForWorkspace(workspaces, 999).isEmpty());
    QVERIFY(NiriIpc::outputNameForWorkspace(workspaces, 7).isEmpty());
}

void NiriIpcTest::testWorkspacesMalformed()
{
    QString error;
    const auto workspaces = NiriIpc::parseWorkspaces(R"({"id":1})", &error);
    QVERIFY(workspaces.isEmpty());
    QVERIFY(!error.isEmpty());
}

void NiriIpcTest::testPhysicalSize()
{
    const char *json = R"({
      "eDP-1": {"name":"eDP-1","physical_size":[300,190],
        "modes":[{"width":2880,"height":1800,"refresh_rate":120000,"is_preferred":true}],
        "current_mode":0,
        "logical":{"x":0,"y":0,"width":1645,"height":1028,"scale":1.75,"transform":"Normal"}},
      "DP-1": {"name":"DP-1",
        "modes":[{"width":1920,"height":1080},{"width":1080,"height":1920}],
        "current_mode":1,
        "logical":{"x":-1080,"y":0,"width":1080,"height":1920,"scale":1.0,"transform":"90"}}
    })";
    QString error;
    const auto outputs = NiriIpc::parseOutputs(json, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(outputs.size(), 2);
    const auto *edp = findOutput(outputs, u"eDP-1"_s);
    const auto *dp = findOutput(outputs, u"DP-1"_s);
    QVERIFY(edp);
    QVERIFY(dp);
    QCOMPARE(edp->physicalSize, QSize(2880, 1800));
    // A rotated output falls back to logical*scale instead of guessing the
    // screencopy buffer orientation.
    QVERIFY(dp->physicalSize.isEmpty());
}

QTEST_GUILESS_MAIN(NiriIpcTest)
#include "NiriIpcTest.moc"
