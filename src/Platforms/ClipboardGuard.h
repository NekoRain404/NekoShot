/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

#include <memory>

/**
 * Snapshots the Wayland clipboard with wl-paste and restores it with wl-copy.
 *
 * niri's screenshot-window action always writes the capture to the clipboard.
 * NekoShot takes the image from the file it requests, so the user's previous
 * clipboard contents are saved before the action runs and put back afterwards.
 *
 * Every external command runs through QProcess with an explicit argument list
 * and a short timeout. Snapshotting and restoring are best effort: failures are
 * logged and never propagated, so a screenshot cannot fail because of the
 * clipboard.
 */
class ClipboardGuard
{
public:
    explicit ClipboardGuard(qint64 maxBytes = 32 * 1024 * 1024);
    ~ClipboardGuard();

    ClipboardGuard(const ClipboardGuard &) = delete;
    ClipboardGuard &operator=(const ClipboardGuard &) = delete;
    ClipboardGuard(ClipboardGuard &&) noexcept = default;
    ClipboardGuard &operator=(ClipboardGuard &&) noexcept = default;

    // Puts the snapshotted entries back on the clipboard. Idempotent: the
    // snapshot is cleared afterwards, so a second call does nothing.
    void restore();

    // Drops the snapshot without writing it back, for captures that never
    // touched the clipboard (command failed or produced no image). The
    // destructor then has nothing left to restore.
    void discard();

    bool hasSnapshot() const;

    static bool isAvailable();

    /// Restores a snapshot after delayMsec on the calling thread while holding
    /// the clipboard-wide lock. A new capture's snapshot waits for this to
    /// finish, so a delayed restore cannot overwrite a newer capture's
    /// clipboard state.
    static void restoreAfterDelay(std::shared_ptr<ClipboardGuard> guard, int delayMsec);

    // Keeps entries in their original order until adding the next one would
    // exceed maxBytes. Entries are never split.
    static QList<QPair<QString, QByteArray>> limitEntries(const QList<QPair<QString, QByteArray>> &entries, qint64 maxBytes);

private:
    void restoreWithoutLock();

    QList<QPair<QString, QByteArray>> m_entries;
};
