/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "NiriIpc.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QProcess>
#include <QSize>
#include <QStringList>

namespace
{
constexpr int s_timeoutMsec = 5000;

// Picking a window blocks until the user clicks a window or presses Esc, so it
// must not share the short timeout used by the non-interactive commands.
constexpr int s_pickTimeoutMsec = 10 * 60 * 1000;

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

bool isNullDocument(const QByteArray &json)
{
    return json.trimmed() == "null";
}

NiriIpc::Output outputFromObject(const QJsonObject &object)
{
    NiriIpc::Output output;
    output.name = object.value(QStringLiteral("name")).toString();

    const QJsonObject logical = object.value(QStringLiteral("logical")).toObject();
    output.logicalRect = QRectF(logical.value(QStringLiteral("x")).toDouble(),
                                logical.value(QStringLiteral("y")).toDouble(),
                                logical.value(QStringLiteral("width")).toDouble(),
                                logical.value(QStringLiteral("height")).toDouble());
    output.scale = logical.value(QStringLiteral("scale")).toDouble(1.0);
    output.transform = logical.value(QStringLiteral("transform")).toString();

    const QJsonArray modes = object.value(QStringLiteral("modes")).toArray();
    const int currentMode = object.value(QStringLiteral("current_mode")).toInt(-1);
    if (currentMode >= 0 && currentMode < modes.size() && modes.at(currentMode).isObject()) {
        const QJsonObject mode = modes.at(currentMode).toObject();
        const int width = mode.value(QStringLiteral("width")).toInt();
        const int height = mode.value(QStringLiteral("height")).toInt();
        const QString transform = output.transform.toLower();
        // Only unrotated modes are guaranteed to match the screencopy buffer.
        // A 90/270 output is captured in its logical orientation, so callers
        // fall back to logical*scale for it.
        if (width > 0 && height > 0 && transform != QStringLiteral("90") && transform != QStringLiteral("270")) {
            output.physicalSize = QSize(width, height);
        }
    }
    return output;
}

std::optional<QByteArray> runNiri(const QStringList &arguments, QString *error, int finishTimeoutMsec = s_timeoutMsec)
{
    QProcess process;
    process.setProgram(QStringLiteral("niri"));
    process.setArguments(QStringList{QStringLiteral("msg"), QStringLiteral("--json")} + arguments);
    process.start();

    if (!process.waitForStarted(s_timeoutMsec)) {
        setError(error, process.errorString());
        return std::nullopt;
    }
    if (!process.waitForFinished(finishTimeoutMsec)) {
        process.kill();
        process.waitForFinished();
        setError(error, QStringLiteral("niri msg timed out after %1 ms").arg(finishTimeoutMsec));
        return std::nullopt;
    }
    if (process.exitStatus() != QProcess::NormalExit) {
        setError(error, QStringLiteral("niri msg crashed"));
        return std::nullopt;
    }
    if (process.exitCode() != 0) {
        QString message = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (message.isEmpty()) {
            message = QStringLiteral("niri msg exited with code %1").arg(process.exitCode());
        }
        setError(error, message);
        return std::nullopt;
    }

    return process.readAllStandardOutput();
}
}

namespace NiriIpc
{
bool isAvailable()
{
    if (!qEnvironmentVariableIsEmpty("NIRI_SOCKET")) {
        return true;
    }
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QStringLiteral("niri"), Qt::CaseInsensitive);
}

QList<Output> parseOutputs(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, parseError.errorString());
        return {};
    }
    if (!document.isObject()) {
        setError(error, QStringLiteral("expected a JSON object"));
        return {};
    }

    QList<Output> outputs;
    const QJsonObject root = document.object();
    outputs.reserve(root.size());
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (!it.value().isObject()) {
            continue;
        }
        const QJsonObject object = it.value().toObject();
        // Disabled outputs carry "logical": null; skip them rather than inventing a
        // 0x0 output at the origin that later geometry matching could pick.
        if (!object.value(QStringLiteral("logical")).isObject()) {
            continue;
        }
        outputs.append(outputFromObject(object));
    }
    return outputs;
}

std::optional<Output> parseFocusedOutput(const QByteArray &json, QString *error)
{
    if (isNullDocument(json)) {
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, parseError.errorString());
        return std::nullopt;
    }
    if (!document.isObject()) {
        setError(error, QStringLiteral("expected a JSON object"));
        return std::nullopt;
    }
    return outputFromObject(document.object());
}

std::optional<Window> parseWindow(const QByteArray &json, QString *error)
{
    if (isNullDocument(json)) {
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, parseError.errorString());
        return std::nullopt;
    }
    if (!document.isObject()) {
        setError(error, QStringLiteral("expected a JSON object"));
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    const QJsonValue idValue = object.value(QStringLiteral("id"));
    if (!idValue.isDouble()) {
        setError(error, QStringLiteral("window object has no numeric id"));
        return std::nullopt;
    }

    Window window;
    window.id = static_cast<quint64>(idValue.toInteger());
    window.title = object.value(QStringLiteral("title")).toString();
    window.appId = object.value(QStringLiteral("app_id")).toString();
    window.workspaceId = static_cast<quint64>(object.value(QStringLiteral("workspace_id")).toInteger());
    return window;
}

QList<Workspace> parseWorkspaces(const QByteArray &json, QString *error)
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

    QList<Workspace> workspaces;
    const QJsonArray array = document.array();
    workspaces.reserve(array.size());
    for (const auto &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        Workspace workspace;
        workspace.id = static_cast<quint64>(object.value(QStringLiteral("id")).toInteger());
        workspace.output = object.value(QStringLiteral("output")).toString();
        workspace.isActive = object.value(QStringLiteral("is_active")).toBool();
        workspace.isFocused = object.value(QStringLiteral("is_focused")).toBool();
        workspaces.append(workspace);
    }
    return workspaces;
}

QString outputNameForWorkspace(const QList<Workspace> &workspaces, quint64 workspaceId)
{
    for (const auto &workspace : workspaces) {
        if (workspace.id == workspaceId) {
            return workspace.output;
        }
    }
    return {};
}

QList<Output> outputs(QString *error)
{
    const auto json = runNiri({QStringLiteral("outputs")}, error);
    if (!json) {
        return {};
    }
    return parseOutputs(*json, error);
}

std::optional<Output> focusedOutput(QString *error)
{
    const auto json = runNiri({QStringLiteral("focused-output")}, error);
    if (!json) {
        return std::nullopt;
    }
    return parseFocusedOutput(*json, error);
}

std::optional<Output> outputForWindow(const Window &window, QString *error)
{
    if (window.workspaceId == 0) {
        return std::nullopt;
    }
    const auto workspaceJson = runNiri({QStringLiteral("workspaces")}, error);
    if (!workspaceJson) {
        return std::nullopt;
    }
    QString parseErrorText;
    const auto workspaces = parseWorkspaces(*workspaceJson, &parseErrorText);
    if (workspaces.isEmpty()) {
        setError(error, parseErrorText);
        return std::nullopt;
    }
    const QString outputName = outputNameForWorkspace(workspaces, window.workspaceId);
    if (outputName.isEmpty()) {
        return std::nullopt;
    }

    const auto outputJson = runNiri({QStringLiteral("outputs")}, error);
    if (!outputJson) {
        return std::nullopt;
    }
    const auto outputs = parseOutputs(*outputJson, &parseErrorText);
    if (outputs.isEmpty()) {
        setError(error, parseErrorText);
        return std::nullopt;
    }
    for (const auto &output : outputs) {
        if (output.name == outputName) {
            return output;
        }
    }
    return std::nullopt;
}

std::optional<Window> focusedWindow(QString *error)
{
    const auto json = runNiri({QStringLiteral("focused-window")}, error);
    if (!json) {
        return std::nullopt;
    }
    return parseWindow(*json, error);
}

std::optional<Window> pickWindow(QString *error)
{
    const auto json = runNiri({QStringLiteral("pick-window")}, error, s_pickTimeoutMsec);
    if (!json) {
        return std::nullopt;
    }
    return parseWindow(*json, error);
}
}
