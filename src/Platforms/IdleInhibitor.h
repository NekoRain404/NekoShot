/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QDBusUnixFileDescriptor>

/**
 * Keeps the session from going idle while an encoder is running.
 *
 * Prefers logind's "idle" inhibitor (the returned fd is held for the lifetime
 * of the recording); falls back to org.freedesktop.ScreenSaver for systems
 * without logind. Releasing is automatic if the process dies, which is why the
 * fd-based inhibitor is preferred over a toggle-style API.
 */
class IdleInhibitor
{
public:
    IdleInhibitor() = default;
    ~IdleInhibitor();
    IdleInhibitor(const IdleInhibitor &) = delete;
    IdleInhibitor &operator=(const IdleInhibitor &) = delete;

    void start();
    void stop();
    bool isActive() const;

private:
    QDBusUnixFileDescriptor m_logindFd;
    uint m_screenSaverCookie = 0;
    bool m_active = false;
};
