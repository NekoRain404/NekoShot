/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

/**
 * PulseAudio/PipeWire device enumeration for the recording settings.
 *
 * Everything goes through `pactl -f json`, which works the same on
 * PulseAudio and on PipeWire's pulse compatibility layer. The parsers are
 * separated from the process calls so they can be unit-tested.
 */
namespace AudioDevices
{
struct Source {
    QString name;
    QString description;
    bool isMonitor = false;
};

struct ApplicationStream {
    QString application;
    QString title;
    quint32 nodeId = 0;
};

QList<Source> parseSources(const QByteArray &json, QString *error = nullptr);
QList<ApplicationStream> parseApplicationStreams(const QByteArray &json, QString *error = nullptr);

QList<Source> sources(QString *error = nullptr);
QList<ApplicationStream> applicationStreams(QString *error = nullptr);

/// PipeWire node id of the first playing stream owned by \a application.
/// Returns nullopt when the application is not playing audio.
std::optional<quint32> nodeIdForApplication(const QString &application, QString *error = nullptr);
}
