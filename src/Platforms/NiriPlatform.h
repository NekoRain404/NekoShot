/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

namespace NiriPlatform
{
// True when running inside a niri session (see NiriIpc::isAvailable()).
bool isNiriSession();

// The niri backend draws its selection overlay with layer-shell surfaces.
// Tasks 7 and 8 use this to decide how the grab overlay is presented.
bool useLayerShellOverlay();

// niri has no global shortcut registration API, so tasks 9 and 10 use this to
// disable the KGlobalAccel integration.
bool hasGlobalShortcuts();
}
