/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "NiriPlatform.h"
#include "NiriIpc.h"

namespace NiriPlatform
{
bool isNiriSession()
{
    return NiriIpc::isAvailable();
}

bool useLayerShellOverlay()
{
    return isNiriSession();
}

bool hasGlobalShortcuts()
{
    return !isNiriSession();
}
}
