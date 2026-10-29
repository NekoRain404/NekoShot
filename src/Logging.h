/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#pragma once

#include <QString>

namespace Logging
{
/// Installs a message handler that appends warnings/errors to a rotating file
/// under the user's state directory, while keeping stderr output.
void install();

/// The log file path (valid after install()).
QString filePath();
}
