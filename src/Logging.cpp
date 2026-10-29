/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "Logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

namespace
{
constexpr qint64 s_maxLogBytes = 1024 * 1024;
QMutex s_mutex;
QString s_path;
QtMessageHandler s_previousHandler = nullptr;

void rotateIfNeeded()
{
    if (QFileInfo(s_path).size() < s_maxLogBytes) {
        return;
    }
    const QString oldPath = s_path + QStringLiteral(".1");
    QFile::remove(oldPath);
    QFile::rename(s_path, oldPath);
}

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const bool important = type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg;
    if (important && !s_path.isEmpty()) {
        QMutexLocker locker(&s_mutex);
        rotateIfNeeded();
        QFile file(s_path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            const char *level = type == QtWarningMsg ? "WARN" : type == QtCriticalMsg ? "CRIT" : "FATAL";
            QTextStream stream(&file);
            stream << QDateTime::currentDateTime().toString(Qt::ISODate) << ' ' << level << ' ' << qFormatLogMessage(type, context, message) << '\n';
        }
    }
    if (s_previousHandler) {
        s_previousHandler(type, context, message);
        return;
    }
    const QByteArray line = qFormatLogMessage(type, context, message).toLocal8Bit();
    std::fprintf(stderr, "%s\n", line.constData());
    std::fflush(stderr);
}
}

namespace Logging
{
void install()
{
    if (!s_path.isEmpty()) {
        return;
    }
    QString dir = QStandardPaths::writableLocation(QStandardPaths::StateLocation);
    if (dir.isEmpty()) {
        dir = QDir::homePath() + QStringLiteral("/.local/state/nekoshot");
    }
    QDir().mkpath(dir);
    s_path = dir + QStringLiteral("/nekoshot.log");
    s_previousHandler = qInstallMessageHandler(handler);
}

QString filePath()
{
    return s_path;
}
}
