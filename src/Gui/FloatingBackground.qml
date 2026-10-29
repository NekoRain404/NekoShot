/* SPDX-FileCopyrightText: 2022 Noah Davis <noahadvs@gmail.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

import QtQuick
import org.kde.kirigami as Kirigami
import org.nekoshot.NekoShot.private

/**
 * Frosted panel material: translucent dark tint, a hairline border and a soft
 * shadow. There is no compositor blur behind it, so the tint is opaque enough
 * to keep text readable over bright content.
 */
Kirigami.ShadowedRectangle {
    id: root

    radius: Theme.radiusPanel
    color: Theme.panel
    border.color: Theme.panelBorder
    border.width: contextWindow.dprRound(1)
    shadow.color: Theme.shadowColor
    shadow.size: Theme.shadowBlur
    shadow.xOffset: 0
    shadow.yOffset: Theme.shadowY
}
