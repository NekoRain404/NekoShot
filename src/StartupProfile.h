/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QDebug>
#include <QElapsedTimer>

namespace StartupProfile
{
inline QElapsedTimer &timer()
{
    static QElapsedTimer t;
    if (!t.isValid()) {
        t.start();
    }
    return t;
}

inline bool enabled()
{
    static const bool value = qEnvironmentVariableIsSet("NEKOSHOT_PROFILE");
    return value;
}

inline void mark(const char *name)
{
    if (enabled()) {
        qWarning("startup: %s %lld ms", name, timer().elapsed());
    }
}
}
