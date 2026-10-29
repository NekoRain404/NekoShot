/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "IdleInhibitor.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDebug>

namespace
{
const QString s_logindService = QStringLiteral("org.freedesktop.login1");
const QString s_logindPath = QStringLiteral("/org/freedesktop/login1");
const QString s_logindManager = QStringLiteral("org.freedesktop.login1.Manager");
const QString s_screenSaverService = QStringLiteral("org.freedesktop.ScreenSaver");
const QString s_screenSaverPath = QStringLiteral("/org/freedesktop/ScreenSaver");
const QString s_screenSaverInterface = QStringLiteral("org.freedesktop.ScreenSaver");
const QString s_reason = QStringLiteral("NekoShot is recording");
}

IdleInhibitor::~IdleInhibitor()
{
    stop();
}

void IdleInhibitor::start()
{
    if (m_active) {
        return;
    }

    if (QDBusConnection::systemBus().isConnected()) {
        QDBusMessage message = QDBusMessage::createMethodCall(s_logindService, s_logindPath, s_logindManager, QStringLiteral("Inhibit"));
        message.setArguments({QStringLiteral("idle"), QStringLiteral("NekoShot"), s_reason, QStringLiteral("block")});
        const QDBusMessage reply = QDBusConnection::systemBus().call(message, QDBus::Block, 1000);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            m_logindFd = reply.arguments().constFirst().value<QDBusUnixFileDescriptor>();
            if (m_logindFd.isValid()) {
                m_active = true;
                return;
            }
        }
        qWarning() << "IdleInhibitor: logind Inhibit failed:" << reply.errorMessage();
    }

    if (QDBusConnection::sessionBus().isConnected()) {
        QDBusMessage message = QDBusMessage::createMethodCall(s_screenSaverService, s_screenSaverPath, s_screenSaverInterface, QStringLiteral("Inhibit"));
        message.setArguments({QStringLiteral("NekoShot"), s_reason});
        const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 1000);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            m_screenSaverCookie = reply.arguments().constFirst().toUInt();
            m_active = m_screenSaverCookie != 0;
            if (m_active) {
                return;
            }
        }
        qWarning() << "IdleInhibitor: ScreenSaver Inhibit failed:" << reply.errorMessage();
    }
}

void IdleInhibitor::stop()
{
    if (m_screenSaverCookie != 0 && QDBusConnection::sessionBus().isConnected()) {
        QDBusMessage message = QDBusMessage::createMethodCall(s_screenSaverService, s_screenSaverPath, s_screenSaverInterface, QStringLiteral("UnInhibit"));
        message.setArguments({m_screenSaverCookie});
        QDBusConnection::sessionBus().call(message, QDBus::NoBlock);
        m_screenSaverCookie = 0;
    }
    // Closing the fd releases the logind inhibitor; the kernel also does this
    // if the process dies unexpectedly.
    m_logindFd = QDBusUnixFileDescriptor();
    m_active = false;
}

bool IdleInhibitor::isActive() const
{
    return m_active;
}
