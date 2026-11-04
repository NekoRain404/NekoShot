/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "AudioDevices.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>

namespace
{
constexpr int s_timeoutMsec = 3000;

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

std::optional<QByteArray> runPactl(const QStringList &arguments, QString *error)
{
    QProcess process;
    process.setProgram(QStringLiteral("pactl"));
    process.setArguments(QStringList{QStringLiteral("-f"), QStringLiteral("json")} + arguments);
    process.start();
    if (!process.waitForStarted(s_timeoutMsec)) {
        setError(error, process.errorString());
        return std::nullopt;
    }
    if (!process.waitForFinished(s_timeoutMsec)) {
        process.kill();
        process.waitForFinished();
        setError(error, QStringLiteral("pactl timed out"));
        return std::nullopt;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QString message = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (message.isEmpty()) {
            message = QStringLiteral("pactl exited with code %1").arg(process.exitCode());
        }
        setError(error, message);
        return std::nullopt;
    }
    return process.readAllStandardOutput();
}

QJsonArray documentArray(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, parseError.errorString());
        return {};
    }
    if (!document.isArray()) {
        setError(error, QStringLiteral("expected a JSON array"));
        return {};
    }
    return document.array();
}
}

namespace AudioDevices
{
QList<Source> parseSources(const QByteArray &json, QString *error)
{
    QList<Source> sources;
    const QJsonArray array = documentArray(json, error);
    sources.reserve(array.size());
    for (const auto &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        Source source;
        source.name = object.value(QStringLiteral("name")).toString();
        if (source.name.isEmpty()) {
            continue;
        }
        source.description = object.value(QStringLiteral("description")).toString();
        // PulseAudio reports the owning sink in "monitor_source" for monitor
        // sources; real inputs leave it empty.
        source.isMonitor = !object.value(QStringLiteral("monitor_source")).toString().isEmpty();
        sources.append(source);
    }
    return sources;
}

QList<ApplicationStream> parseApplicationStreams(const QByteArray &json, QString *error)
{
    QList<ApplicationStream> streams;
    const QJsonArray array = documentArray(json, error);
    streams.reserve(array.size());
    for (const auto &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject properties = value.toObject().value(QStringLiteral("properties")).toObject();
        const QString application = properties.value(QStringLiteral("application.name")).toString();
        // object.id is PipeWire's node id. Pure PulseAudio servers do not have
        // it, and per-application capture is only possible through PipeWire.
        bool ok = false;
        const quint32 nodeId = properties.value(QStringLiteral("object.id")).toString().toUInt(&ok);
        if (application.isEmpty() || !ok || nodeId == 0) {
            continue;
        }
        ApplicationStream stream;
        stream.application = application;
        stream.title = properties.value(QStringLiteral("media.name")).toString();
        stream.nodeId = nodeId;
        streams.append(stream);
    }
    return streams;
}

QList<Source> sources(QString *error)
{
    const auto json = runPactl({QStringLiteral("list"), QStringLiteral("sources")}, error);
    if (!json) {
        return {};
    }
    return parseSources(*json, error);
}

QList<ApplicationStream> applicationStreams(QString *error)
{
    const auto json = runPactl({QStringLiteral("list"), QStringLiteral("sink-inputs")}, error);
    if (!json) {
        return {};
    }
    return parseApplicationStreams(*json, error);
}

std::optional<quint32> nodeIdForApplication(const QString &application, QString *error)
{
    if (application.isEmpty()) {
        return std::nullopt;
    }
    const auto streams = applicationStreams(error);
    for (const auto &stream : streams) {
        if (stream.application == application) {
            return stream.nodeId;
        }
    }
    // Application names are user-visible labels; allow a case-insensitive
    // match when the exact name has changed slightly.
    for (const auto &stream : streams) {
        if (stream.application.compare(application, Qt::CaseInsensitive) == 0) {
            return stream.nodeId;
        }
    }
    return std::nullopt;
}
}
