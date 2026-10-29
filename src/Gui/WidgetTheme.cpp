/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "WidgetTheme.h"

#include <QApplication>
#include <QPalette>

using namespace Qt::StringLiterals;

namespace
{
constexpr auto s_window = "#1C1C1E";
constexpr auto s_base = "#2C2C2E";
constexpr auto s_text = "#F5F5F7";
constexpr auto s_textSecondary = "#C7C7CC";
constexpr auto s_accent = "#0A84FF";
constexpr auto s_border = "rgba(255, 255, 255, 0.14)";
constexpr auto s_hover = "rgba(255, 255, 255, 0.16)";
constexpr auto s_pressed = "rgba(255, 255, 255, 0.22)";
constexpr auto s_input = "rgba(255, 255, 255, 0.06)";
}

namespace WidgetTheme
{
void apply(QApplication &app)
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(s_window));
    palette.setColor(QPalette::WindowText, QColor(s_text));
    palette.setColor(QPalette::Base, QColor(s_base));
    palette.setColor(QPalette::AlternateBase, QColor("#242426"));
    palette.setColor(QPalette::Text, QColor(s_text));
    palette.setColor(QPalette::Button, QColor(s_base));
    palette.setColor(QPalette::ButtonText, QColor(s_text));
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, QColor(s_accent));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::ToolTipBase, QColor(s_base));
    palette.setColor(QPalette::ToolTipText, QColor(s_text));
    palette.setColor(QPalette::Link, QColor(s_accent));
    palette.setColor(QPalette::PlaceholderText, QColor(s_textSecondary));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#8E8E93"));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#8E8E93"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#8E8E93"));
    app.setPalette(palette);

    // Qt Widgets do not expose radii or spacing through QPalette, so the
    // remaining tokens live in one stylesheet applied at application scope.
    app.setStyleSheet(
        uR"QSS(
        QWidget { color: #F5F5F7; font-size: 13px; }
        QDialog, QMainWindow, QScrollArea, QStackedWidget { background: #1C1C1E; }
        QLabel { background: transparent; }
        QGroupBox {
            border: 1px solid rgba(255, 255, 255, 0.12);
            border-radius: 10px;
            margin-top: 14px;
            padding-top: 10px;
        }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; color: #C7C7CC; }

        QPushButton {
            background: rgba(255, 255, 255, 0.10);
            border: 1px solid rgba(255, 255, 255, 0.14);
            border-radius: 8px;
            padding: 6px 14px;
            color: #F5F5F7;
        }
        QPushButton:hover { background: rgba(255, 255, 255, 0.16); }
        QPushButton:pressed { background: rgba(255, 255, 255, 0.22); }
        QPushButton:default { background: #0A84FF; border-color: #0A84FF; color: white; }
        QPushButton:disabled { color: #8E8E93; background: rgba(255, 255, 255, 0.05); }

        QLineEdit, QAbstractSpinBox, QComboBox {
            background: rgba(255, 255, 255, 0.06);
            border: 1px solid rgba(255, 255, 255, 0.14);
            border-radius: 8px;
            padding: 4px 8px;
            min-height: 22px;
            selection-background-color: #0A84FF;
        }
        QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus { border-color: #0A84FF; }
        QComboBox::drop-down { border: none; width: 22px; }
        QComboBox QAbstractItemView {
            background: #2C2C2E;
            border: 1px solid rgba(255, 255, 255, 0.16);
            border-radius: 8px;
            selection-background-color: #0A84FF;
            outline: none;
        }

        QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; }
        QCheckBox::indicator:unchecked, QRadioButton::indicator:unchecked {
            border: 1px solid rgba(255, 255, 255, 0.35);
            background: transparent;
            border-radius: 4px;
        }
        QCheckBox::indicator:checked {
            background: #0A84FF;
            border: 1px solid #0A84FF;
            border-radius: 4px;
        }

        QMenu {
            background: #1C1C1E;
            border: 1px solid rgba(255, 255, 255, 0.12);
            border-radius: 10px;
            padding: 6px;
        }
        QMenu::item { padding: 6px 24px 6px 12px; border-radius: 6px; color: #F5F5F7; }
        QMenu::item:selected { background: #0A84FF; color: white; }
        QMenu::item:disabled { color: #8E8E93; }
        QMenu::separator { height: 1px; background: rgba(255, 255, 255, 0.10); margin: 4px 8px; }

        QTabWidget::pane { border: 1px solid rgba(255, 255, 255, 0.12); border-radius: 10px; top: -1px; }
        QTabBar::tab {
            background: transparent;
            color: #C7C7CC;
            padding: 7px 14px;
            margin-right: 2px;
            border-radius: 7px;
        }
        QTabBar::tab:selected { background: rgba(255, 255, 255, 0.12); color: white; }
        QTabBar::tab:hover:!selected { background: rgba(255, 255, 255, 0.06); }

        QToolTip {
            background: #2C2C2E;
            color: #F5F5F7;
            border: 1px solid rgba(255, 255, 255, 0.16);
            border-radius: 6px;
            padding: 4px 8px;
        }

        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical {
            background: rgba(255, 255, 255, 0.22);
            border-radius: 4px;
            min-height: 28px;
        }
        QScrollBar::handle:vertical:hover { background: rgba(255, 255, 255, 0.32); }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
        QScrollBar::handle:horizontal {
            background: rgba(255, 255, 255, 0.22);
            border-radius: 4px;
            min-width: 28px;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
    )QSS"_s);
}
}
