/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "ClipboardGuard.h"

#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>

#include <mutex>

#include <utility>

namespace
{
constexpr int s_timeoutMsec = 1000;

QString wlPaste()
{
    return QStringLiteral("wl-paste");
}

QString wlCopy()
{
    return QStringLiteral("wl-copy");
}

// Runs a program with an argument list (never a shell string). On success the
// standard output is returned in output. Every failure path logs and returns
// false; restoring the clipboard must never take the capture down with it.
bool runProcess(const QString &program, const QStringList &arguments, const QByteArray &input, QByteArray *output, bool logFailures = true)
{
    const auto warn = [logFailures](const QString &message) {
        if (logFailures) {
            qWarning("ClipboardGuard: %s", qPrintable(message));
        }
    };

    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();

    if (!process.waitForStarted(s_timeoutMsec)) {
        warn(QStringLiteral("could not start %1: %2").arg(program, process.errorString()));
        return false;
    }

    if (!input.isEmpty()) {
        process.write(input);
    }
    process.closeWriteChannel();

    if (!process.waitForFinished(s_timeoutMsec)) {
        process.kill();
        process.waitForFinished();
        warn(QStringLiteral("%1 timed out after %2 ms").arg(program).arg(s_timeoutMsec));
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit) {
        warn(QStringLiteral("%1 crashed").arg(program));
        return false;
    }
    if (process.exitCode() != 0) {
        const QString detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
        warn(QStringLiteral("%1 exited with code %2: %3").arg(program).arg(process.exitCode()).arg(detail));
        return false;
    }

    if (output) {
        *output = process.readAllStandardOutput();
    }
    return true;
}

std::mutex &clipboardRestoreMutex()
{
    static std::mutex mutex;
    return mutex;
}

QStringList readClipboardTypes(bool logFailures)
{
    QByteArray output;
    if (!runProcess(QStringLiteral("wl-paste"), {QStringLiteral("--list-types")}, {}, &output, logFailures)) {
        return {};
    }

    QStringList types;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &line : lines) {
        const QString type = QString::fromUtf8(line).trimmed();
        if (!type.isEmpty()) {
            types.append(type);
        }
    }
    return types;
}

}

ClipboardGuard::ClipboardGuard(qint64 maxBytes)
{
    std::lock_guard<std::mutex> lock(clipboardRestoreMutex());
    if (!isAvailable()) {
        qWarning("ClipboardGuard: wl-paste and wl-copy are required to preserve the clipboard");
        return;
    }

    const QStringList types = readClipboardTypes(true);
    if (types.isEmpty()) {
        return;
    }

    QList<QPair<QString, QByteArray>> entries;
    entries.reserve(types.size());
    for (const QString &type : std::as_const(types)) {
        QByteArray data;
        if (!runProcess(wlPaste(), {QStringLiteral("--type"), type, QStringLiteral("--no-newline")}, {}, &data)) {
            // A single unreadable type should not discard the types that did
            // read successfully.
            continue;
        }
        entries.append({type, data});
    }

    m_entries = limitEntries(entries, maxBytes);
}

ClipboardGuard::~ClipboardGuard()
{
    restore();
}

void ClipboardGuard::restore()
{
    std::lock_guard<std::mutex> lock(clipboardRestoreMutex());
    restoreWithoutLock();
}

void ClipboardGuard::restoreWithoutLock()
{
    if (m_entries.isEmpty()) {
        return;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("wl-copy")).isEmpty()) {
        qWarning("ClipboardGuard: wl-copy is not installed, cannot restore the clipboard");
        m_entries.clear();
        return;
    }

    // wl-copy serves one type per invocation, so the clipboard ends up with the
    // last type written. That is still the correct data for text and the usual
    // case; the full type list is best effort.
    for (const auto &entry : std::as_const(m_entries)) {
        runProcess(wlCopy(), {QStringLiteral("--type"), entry.first}, entry.second, nullptr);
    }
    m_entries.clear();
}

void ClipboardGuard::discard()
{
    std::lock_guard<std::mutex> lock(clipboardRestoreMutex());
    m_entries.clear();
}

bool ClipboardGuard::hasSnapshot() const
{
    return !m_entries.isEmpty();
}

void ClipboardGuard::restoreAfterDelay(std::shared_ptr<ClipboardGuard> guard, int delayMsec)
{
    std::lock_guard<std::mutex> lock(clipboardRestoreMutex());
    if (delayMsec > 0) {
        QThread::msleep(delayMsec);
    }
    guard->restoreWithoutLock();
}

bool ClipboardGuard::isAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("wl-paste")).isEmpty() && !QStandardPaths::findExecutable(QStringLiteral("wl-copy")).isEmpty();
}

QList<QPair<QString, QByteArray>> ClipboardGuard::limitEntries(const QList<QPair<QString, QByteArray>> &entries, qint64 maxBytes)
{
    QList<QPair<QString, QByteArray>> limited;
    qint64 total = 0;
    for (const auto &entry : entries) {
        const qint64 size = entry.second.size();
        if (total + size > maxBytes) {
            break;
        }
        limited.append(entry);
        total += size;
    }
    return limited;
}
