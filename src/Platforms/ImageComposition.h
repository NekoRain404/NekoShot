/* SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QImage>
#include <QList>

namespace ImageComposition
{
// 以 union 左上角为原点平移绘制；结果设置 devicePixelRatio、
// ImageMetaData::logicalXY（union 左上角）与 subGeometryList。
// 单张图像原样返回（其 logicalXY 已经是自身位置），不做平移或元数据重写。
QImage combinedImage(const QList<QImage> &images);
}
