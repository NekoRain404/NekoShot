/* This file is part of Spectacle, the KDE screenshot utility
 * SPDX-FileCopyrightText: 2015 Boudhayan Gupta <bgupta@kde.org>
 * SPDX-FileCopyrightText: 2019 David Redondo <kde@david-redondo.de>
 * SPDX-FileCopyrightText: 2020 Ahmad Samir <a.samirh78@gmail.com>
 * SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "SpectacleWindow.h"

#include "ExportManager.h"
#include "SpectacleCore.h"
#include "Geometry.h"
#include "Gui/ExportMenu.h"
#include "Gui/HelpMenu.h"
#include "Gui/OptionsMenu.h"
#include "Gui/WidgetWindowUtils.h"

#include <QApplication>
#include <utility>

using namespace Qt::StringLiterals;
using G = Geometry;

QList<SpectacleWindow *> SpectacleWindow::s_spectacleWindowInstances = {};
bool SpectacleWindow::s_synchronizingVisibility = false;
bool SpectacleWindow::s_synchronizingTitle = false;
SpectacleWindow::TitlePreset SpectacleWindow::s_lastTitlePreset = Default;
QString SpectacleWindow::s_previousTitle = QGuiApplication::applicationDisplayName();
bool SpectacleWindow::s_synchronizingAnnotating = false;
bool SpectacleWindow::s_isAnnotating = false;

SpectacleWindow::SpectacleWindow(QQmlEngine *engine, QWindow *parent)
    : QQuickView(engine, parent)
    , m_context(new QQmlContext(engine->rootContext(), this))
{
    s_spectacleWindowInstances.append(this);

    connect(engine, &QQmlEngine::quit, QCoreApplication::instance(), &QCoreApplication::quit, Qt::QueuedConnection);
    connect(this, &QQuickView::statusChanged, this, [](QQuickView::Status status){
        if (status == QQuickView::Error) {
            QCoreApplication::quit();
        }
    });
    connect(this, &SpectacleWindow::xChanged, this, &SpectacleWindow::logicalXChanged);
    connect(this, &SpectacleWindow::yChanged, this, &SpectacleWindow::logicalYChanged);

    setTextRenderType(QQuickWindow::NativeTextRendering);

    // set up QML
    setResizeMode(QQuickView::SizeRootObjectToView);
    m_context->setContextProperty(u"contextWindow"_s, this);
}

SpectacleWindow::~SpectacleWindow()
{
    s_spectacleWindowInstances.removeOne(this);
}

qreal SpectacleWindow::logicalX() const
{
    return G::mapFromPlatformValue(x(), devicePixelRatio());
}

qreal SpectacleWindow::logicalY() const
{
    return G::mapFromPlatformValue(y(), devicePixelRatio());
}

bool SpectacleWindow::isAnnotating() const
{
    return s_isAnnotating;
}

void SpectacleWindow::setAnnotating(bool annotating)
{
    if (s_synchronizingAnnotating || s_isAnnotating == annotating) {
        return;
    }
    s_synchronizingAnnotating = true;
    s_isAnnotating = annotating;
    for (auto window : std::as_const(s_spectacleWindowInstances)) {
        Q_EMIT window->annotatingChanged();
    }
    s_synchronizingAnnotating = false;
}

void SpectacleWindow::unminimize()
{
    setVisible(true);
    setWindowStates(windowStates().setFlag(Qt::WindowMinimized, false));
}

QList<SpectacleWindow *> SpectacleWindow::instances()
{
    return s_spectacleWindowInstances;
}

void SpectacleWindow::setVisibilityForAll(QWindow::Visibility visibility)
{
    if (s_synchronizingVisibility || s_spectacleWindowInstances.isEmpty()) {
        return;
    }
    s_synchronizingVisibility = true;
    for (auto window : std::as_const(s_spectacleWindowInstances)) {
        window->setVisibility(visibility);
    }
    s_synchronizingVisibility = false;
}

void SpectacleWindow::setTitleForAll(TitlePreset preset, const QString &fileName)
{
    if (s_synchronizingTitle || s_spectacleWindowInstances.isEmpty()) {
        return;
    }
    s_synchronizingTitle = true;

    QString newTitle = titlePresetString(preset, fileName);

    if (!newTitle.isEmpty()) {
        if (s_lastTitlePreset != TitlePreset::Timer) {
            s_previousTitle = s_spectacleWindowInstances.constFirst()->title();
        }
        s_lastTitlePreset = preset;

        for (auto window : std::as_const(s_spectacleWindowInstances)) {
            window->setTitle(newTitle);
        }
    }

    s_synchronizingTitle = false;
}

void SpectacleWindow::closeAll()
{
    // counting down should prevent invalid memory access
    for (int i = s_spectacleWindowInstances.count() - 1; i >= 0; --i) {
        s_spectacleWindowInstances[i]->close();
    }
}

qreal SpectacleWindow::dprRound(qreal value) const
{
    return G::dprRound(value, devicePixelRatio());
}

QPointF SpectacleWindow::dprRound(const QPointF &point) const
{
    return G::dprRound(point, devicePixelRatio());
}

qreal SpectacleWindow::dprCeil(qreal value) const
{
    return G::dprCeil(value, devicePixelRatio());
}

qreal SpectacleWindow::dprFloor(qreal value) const
{
    return G::dprFloor(value, devicePixelRatio());
}

QString SpectacleWindow::baseFileName(const QUrl &url) const
{
    return url.fileName();
}

QString SpectacleWindow::titlePresetString(TitlePreset preset, const QString &fileName)
{
    if (preset == TitlePreset::Timer) {
        return i18ncp("@title:window", "%1 second", "%1 seconds",
                      qCeil(SpectacleCore::instance()->captureTimeRemaining() / 1000.0));
    } else if (preset == TitlePreset::Unsaved) {
        return i18nc("@title:window Unsaved Screenshot", "Unsaved") + u"*"_s;
    } else if (preset == TitlePreset::Saved && !fileName.isEmpty()) {
        return fileName;
    } else if (preset == TitlePreset::Modified && !fileName.isEmpty()) {
        return fileName + u"*"_s;
    } else if (preset == TitlePreset::Previous && !s_previousTitle.isEmpty()) {
        return s_previousTitle;
    }
    return QGuiApplication::applicationDisplayName();
}

void SpectacleWindow::deleter(SpectacleWindow *window)
{
    s_spectacleWindowInstances.removeOne(window);
    window->deleteLater();
}

void SpectacleWindow::setSource(const QUrl &source, const QVariantMap &initialProperties)
{
    if (source.isEmpty()) {
        m_component.reset(nullptr);
        QQuickView::setSource(source);
        return;
    }

    m_component.reset(new QQmlComponent(engine(), source, this));
    auto *component = m_component.get();
    QObject *object = nullptr;

    if (component->isLoading()) {
        connect(component, &QQmlComponent::statusChanged,
                this, [this, component, &source, &initialProperties]() {
            disconnect(component, &QQmlComponent::statusChanged, this, nullptr);
            QObject *object = nullptr;
            if (component->isReady()) {
                if (!initialProperties.isEmpty()) {
                    object = component->createWithInitialProperties(initialProperties,
                                                                    m_context.get());
                } else {
                    object = component->create(m_context.get());
                }
            }
            setContent(source, component, object);
        });
    } else if (component->isReady()) {
        if (!initialProperties.isEmpty()) {
            object = component->createWithInitialProperties(initialProperties, m_context.get());
        } else {
            object = component->create(m_context.get());
        }
    }

    setContent(source, component, object);
}

void SpectacleWindow::save()
{
    SpectacleCore::instance()->syncExportImage();
    ExportManager::instance()->exportImage(ExportManager::Save | ExportManager::UserAction,
                                           SpectacleCore::instance()->outputUrl());
}

void SpectacleWindow::saveAs()
{
    if (SpectacleCore::instance()->videoMode()) {
        ExportManager::instance()->exportVideo(ExportManager::SaveAs | ExportManager::UserAction,
                                               SpectacleCore::instance()->currentVideo());
        return;
    }
    SpectacleCore::instance()->syncExportImage();
    ExportManager::instance()->exportImage(ExportManager::SaveAs | ExportManager::UserAction,
                                           SpectacleCore::instance()->outputUrl());
}

void SpectacleWindow::copyImage()
{
    SpectacleCore::instance()->syncExportImage();
    ExportManager::instance()->exportImage(ExportManager::CopyImage | ExportManager::UserAction);
}

void SpectacleWindow::copyLocation()
{
    if (SpectacleCore::instance()->videoMode()) {
        ExportManager::instance()->exportVideo(ExportManager::CopyPath | ExportManager::UserAction,
                                               SpectacleCore::instance()->currentVideo());
        return;
    }
    SpectacleCore::instance()->syncExportImage();
    ExportManager::instance()->exportImage(ExportManager::CopyPath | ExportManager::UserAction);
}

void SpectacleWindow::showPrintDialog()
{
    SpectacleCore::instance()->syncExportImage();
    ExportMenu::instance()->openPrintDialog();
}

void SpectacleWindow::showPreferencesDialog()
{
    OptionsMenu::instance()->showPreferencesDialog();
}

void SpectacleWindow::applyAnnotationColor(int option, const QColor &color)
{
    auto document = SpectacleCore::instance()->annotationDocument();
    auto tool = document->tool();
    auto wrapper = document->selectedItemWrapper();
    auto apply = [&](const std::function<void()> &toolSetter, const std::function<void()> &wrapperSetter) {
        if (tool->type() == AnnotationTool::SelectTool) {
            wrapperSetter();
            wrapper->commitChanges();
        } else if (tool->type() == AnnotationTool::TextTool && wrapper->options().testFlag(AnnotationTool::TextOption)) {
            toolSetter();
            wrapperSetter();
            wrapper->commitChanges();
        } else {
            toolSetter();
        }
    };
    switch (option) {
    case AnnotationTool::StrokeOption:
        apply(
            [&] {
                tool->setStrokeColor(color);
            },
            [&] {
                wrapper->setStrokeColor(color);
            });
        break;
    case AnnotationTool::FillOption:
        apply(
            [&] {
                tool->setFillColor(color);
            },
            [&] {
                wrapper->setFillColor(color);
            });
        break;
    case AnnotationTool::FontOption:
        apply(
            [&] {
                tool->setFontColor(color);
            },
            [&] {
                wrapper->setFontColor(color);
            });
        break;
    default:
        qmlWarning(this) << "invalid color option" << option;
        break;
    }
}

void SpectacleWindow::applyAnnotationFont(const QString &family, int pointSize, bool bold, bool italic)
{
    QFont font(family);
    font.setPointSize(pointSize);
    font.setBold(bold);
    font.setItalic(italic);
    auto document = SpectacleCore::instance()->annotationDocument();
    auto tool = document->tool();
    auto wrapper = document->selectedItemWrapper();
    auto apply = [&](const std::function<void()> &toolSetter, const std::function<void()> &wrapperSetter) {
        if (tool->type() == AnnotationTool::SelectTool) {
            wrapperSetter();
            wrapper->commitChanges();
        } else if (tool->type() == AnnotationTool::TextTool && wrapper->options().testFlag(AnnotationTool::TextOption)) {
            toolSetter();
            wrapperSetter();
            wrapper->commitChanges();
        } else {
            toolSetter();
        }
    };
    apply(
        [&] {
            tool->setFont(font);
        },
        [&] {
            wrapper->setFont(font);
        });
}

void SpectacleWindow::mousePressEvent(QMouseEvent *event)
{
    m_pressedButtons = event->buttons();
    // QMenus need to be closed by hand when used from QML, see plasma-workspace/shellcorona.cpp
    if (auto popup = QApplication::activePopupWidget()) {
        popup->close();
        event->accept();
    } else {
        QQuickView::mousePressEvent(event);
    }
}

void SpectacleWindow::mouseReleaseEvent(QMouseEvent *event)
{
    m_pressedButtons = {};
    QQuickView::mouseReleaseEvent(event);
}

void SpectacleWindow::keyPressEvent(QKeyEvent *event)
{
    // Events need to be processed normally first for events to reach items
    QQuickView::keyPressEvent(event);
    if (event->isAccepted()) {
        return;
    }
    m_pressedKeys = event->key() | event->modifiers();
}

void SpectacleWindow::keyReleaseEvent(QKeyEvent *event)
{
    // Events need to be processed normally first for events to reach items
    QQuickView::keyReleaseEvent(event);
    if (event->isAccepted()) {
        return;
    }
    // Cancel defaults to Escape in QPlatformTheme.
    // Handling this here fixes https://bugs.kde.org/show_bug.cgi?id=428478
    if ((event->matches(QKeySequence::Quit)
        || event->matches(QKeySequence::Close)
        || event->matches(QKeySequence::Cancel))
        // We need to check if these were pressed previously or else pressing escape
        // in a dialog will quit spectacle when you release the escape key.
        && m_pressedKeys == (event->key() | event->modifiers())
    ) {
        event->accept();
        auto spectacleCore = SpectacleCore::instance();
        spectacleCore->cancelScreenshot();
    } else if (event->matches(QKeySequence::Preferences)) {
        event->accept();
        showPreferencesDialog();
    } else if (event->matches(QKeySequence::New)) {
        event->accept();
        SpectacleCore::instance()->takeNewScreenshot();
    } else if (event->matches(QKeySequence::HelpContents)) {
        event->accept();
        HelpMenu::instance()->showAppHelp();
    }
    m_pressedKeys = {};
}

#include "moc_SpectacleWindow.cpp"
