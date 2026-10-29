/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "Platforms/ClipboardGuard.h"

#include <QTest>

using namespace Qt::StringLiterals;

// Only the pure size-limiting logic is unit-tested here. Snapshotting and
// restoring talk to the live compositor clipboard, so they stay out of unit
// tests and are covered by the manual check in the task brief.
class ClipboardGuardTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testLimitEntriesKeepsOrderUntilCap();
    void testLimitEntriesKeepsEverythingUnderCap();
    void testLimitEntriesStopsAtFirstEntryThatDoesNotFit();
    void testLimitEntriesWithEmptyInput();
    void testLimitEntriesZeroBudget();
};

void ClipboardGuardTest::testLimitEntriesKeepsOrderUntilCap()
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

void ClipboardGuardTest::testLimitEntriesKeepsEverythingUnderCap()
{
    const QList<QPair<QString, QByteArray>> entries = {
        {u"text/plain"_s, QByteArray(10, 'a')},
        {u"image/png"_s, QByteArray(20, 'b')},
    };
    const auto kept = ClipboardGuard::limitEntries(entries, 30);
    QCOMPARE(kept, entries);
}

void ClipboardGuardTest::testLimitEntriesStopsAtFirstEntryThatDoesNotFit()
{
    const QList<QPair<QString, QByteArray>> entries = {
        {u"text/plain"_s, QByteArray(10, 'a')},
        {u"image/png"_s, QByteArray(20, 'b')},
        {u"text/html"_s, QByteArray(5, 'c')},
    };
    // The second entry does not fit, so the smaller third one is not moved up.
    const auto kept = ClipboardGuard::limitEntries(entries, 20);
    QCOMPARE(kept.size(), 1);
    QCOMPARE(kept.first().first, u"text/plain"_s);
}

void ClipboardGuardTest::testLimitEntriesWithEmptyInput()
{
    QVERIFY(ClipboardGuard::limitEntries({}, 25).isEmpty());
}

void ClipboardGuardTest::testLimitEntriesZeroBudget()
{
    const QList<QPair<QString, QByteArray>> entries = {
        {u"text/plain"_s, QByteArray()},
        {u"image/png"_s, QByteArray(1, 'b')},
    };
    const auto kept = ClipboardGuard::limitEntries(entries, 0);
    QCOMPARE(kept.size(), 1);
    QCOMPARE(kept.first().first, u"text/plain"_s);
}

QTEST_GUILESS_MAIN(ClipboardGuardTest)

#include "ClipboardGuardTest.moc"
