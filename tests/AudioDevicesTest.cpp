/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "Platforms/AudioDevices.h"

#include <QTest>

using namespace Qt::StringLiterals;

class AudioDevicesTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testSources();
    void testApplicationStreams();
    void testMalformed();
};

void AudioDevicesTest::testSources()
{
    const QByteArray json = R"([
        {"index":47,"name":"speaker.monitor","description":"Monitor of Speaker","monitor_source":"speaker"},
        {"index":48,"name":"mic","description":"Digital Microphone","monitor_source":""},
        {"description":"no name"}
    ])";
    QString error;
    const auto sources = AudioDevices::parseSources(json, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(sources.size(), 2);
    QCOMPARE(sources.at(0).name, u"speaker.monitor"_s);
    QCOMPARE(sources.at(0).description, u"Monitor of Speaker"_s);
    QVERIFY(sources.at(0).isMonitor);
    QCOMPARE(sources.at(1).name, u"mic"_s);
    QVERIFY(!sources.at(1).isMonitor);
}

void AudioDevicesTest::testApplicationStreams()
{
    const QByteArray json = R"([
        {"index":1,"properties":{"application.name":"firefox","media.name":"Video","object.id":"101"}},
        {"index":2,"properties":{"application.name":"firefox","media.name":"Music","object.id":"102"}},
        {"index":3,"properties":{"application.name":"no-node","media.name":"x"}},
        {"index":4,"properties":{"media.name":"anonymous","object.id":"103"}}
    ])";
    QString error;
    const auto streams = AudioDevices::parseApplicationStreams(json, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    // Entries without a PipeWire node id and without an application name are
    // skipped: they cannot be targeted.
    QCOMPARE(streams.size(), 2);
    QCOMPARE(streams.at(0).application, u"firefox"_s);
    QCOMPARE(streams.at(0).title, u"Video"_s);
    QCOMPARE(streams.at(0).nodeId, quint32(101));
    QCOMPARE(streams.at(1).nodeId, quint32(102));
}

void AudioDevicesTest::testMalformed()
{
    QString error;
    QVERIFY(AudioDevices::parseSources("not json", &error).isEmpty());
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(AudioDevices::parseSources(R"({"name":"object"})", &error).isEmpty());
    QVERIFY(!error.isEmpty());
    error.clear();
    QVERIFY(AudioDevices::parseApplicationStreams("[]", &error).isEmpty());
    QVERIFY(error.isEmpty());
}

QTEST_GUILESS_MAIN(AudioDevicesTest)
#include "AudioDevicesTest.moc"
