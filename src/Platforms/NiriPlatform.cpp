/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "NiriPlatform.h"
#include "NiriIpc.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>

namespace NiriPlatform
{
bool isNiriSession()
{
    return NiriIpc::isAvailable();
}

bool useLayerShellOverlay()
{
    return isNiriSession();
}

bool hasGlobalShortcuts()
{
    return !isNiriSession();
}

bool sessionLocked()
{
    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        return false;
    }
    QDBusMessage getSession = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
                                                             QStringLiteral("/org/freedesktop/login1"),
                                                             QStringLiteral("org.freedesktop.login1.Manager"),
                                                             QStringLiteral("GetSession"));
    getSession.setArguments({QStringLiteral("auto")});
    const QDBusMessage sessionReply = bus.call(getSession, QDBus::Block, 1000);
    if (sessionReply.type() != QDBusMessage::ReplyMessage || sessionReply.arguments().isEmpty()) {
        return false;
    }
    const QString sessionPath = sessionReply.arguments().constFirst().toString();
    if (sessionPath.isEmpty()) {
        return false;
    }
    QDBusMessage getLocked = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
                                                            sessionPath,
                                                            QStringLiteral("org.freedesktop.DBus.Properties"),
                                                            QStringLiteral("Get"));
    getLocked.setArguments({QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("LockedHint")});
    const QDBusMessage lockedReply = bus.call(getLocked, QDBus::Block, 1000);
    if (lockedReply.type() != QDBusMessage::ReplyMessage || lockedReply.arguments().isEmpty()) {
        return false;
    }
    return lockedReply.arguments().constFirst().value<QDBusVariant>().variant().toBool();
}
}
