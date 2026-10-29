/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

class QApplication;

namespace WidgetTheme
{
/// Applies the same dark, rounded design tokens used by the QML surfaces to
/// Qt Widgets (settings dialog, menus, tooltips). Safe to call once after the
/// QApplication exists and before any window is shown.
void apply(QApplication &app);
}
