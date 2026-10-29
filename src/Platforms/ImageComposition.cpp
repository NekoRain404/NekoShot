/* SPDX-FileCopyrightText: 2024 Noah Davis <noahadvs@gmail.com>
 * SPDX-FileCopyrightText: 2026 NekoShot contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "ImageComposition.h"
#include "ImageMetaData.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace ImageComposition
{
QImage combinedImage(const QList<QImage> &images)
{
    if (images.empty()) {
        return {};
    }
    if (images.size() == 1) {
        return images.constFirst();
    }
    QRectF imageRect;
    qreal maxDpr = 0;
    ImageMetaData::SubGeometryList geometryList;
    for (const auto &i : images) {
        const auto dpr = i.devicePixelRatio();
        const auto rect = QRectF{ImageMetaData::logicalXY(i), i.deviceIndependentSize()};
        maxDpr = std::max(maxDpr, dpr);
        imageRect |= rect;
        geometryList << ImageMetaData::subGeometryPropertyMap(rect, dpr);
    }
    static const auto finalFormat = QImage::Format_RGBA8888_Premultiplied;
    const bool allSameDpr = std::all_of(images.cbegin(), images.cend(), [maxDpr](const QImage &i) {
        return i.devicePixelRatio() == maxDpr;
    });
    // Images are positioned relative to the top-left corner of their union.
    const QPointF origin = imageRect.topLeft();
    if (allSameDpr) {
        QImage finalImage{imageRect.size().toSize() * maxDpr, finalFormat};
        finalImage.fill(Qt::transparent);
        QPainter painter(&finalImage);
        for (const auto &image : images) {
            // Explicitly setting the position and size so that you don't need to read
            // QPainter source code to understand how this works.
            painter.drawImage({(ImageMetaData::logicalXY(image) - origin) * maxDpr, image.size()}, image);
        }
        painter.end();
        // Setting DPR after painting prevents it from affecting the coordinates of the QPainter.
        // During testing, setting final image DPR first and relying on QPainter::drawImage
        // automatic scaling would occasionally use the wrong target position. I have no idea why.
        // It might not even be directly related to QPainter, so keep an eye out for bugs like that.
        finalImage.setDevicePixelRatio(maxDpr);
        ImageMetaData::setSubGeometryList(finalImage, geometryList);
        ImageMetaData::setLogicalXY(finalImage, origin.x(), origin.y());
        return finalImage;
    }
    // We ceil to the next integer size up so that integer DPR images are always crisp.
    const auto finalDpr = std::ceil(maxDpr);
    QImage finalImage{imageRect.size().toSize() * finalDpr, finalFormat};
    finalImage.fill(Qt::transparent);
    QPainter painter(&finalImage);
    for (const auto &image : images) {
        const auto pos = (ImageMetaData::logicalXY(image) - origin) * finalDpr;
        const auto size = (image.deviceIndependentSize() * finalDpr).toSize();
        const auto imageDpr = image.devicePixelRatio();
        const bool hasIntDpr = static_cast<int>(imageDpr) == imageDpr;
        const auto interpolation = hasIntDpr ? Qt::FastTransformation : Qt::SmoothTransformation;
        painter.drawImage(QRectF{pos, size},
                          size == image.size() //
                              ? image
                              : image.scaled(size, Qt::KeepAspectRatio, interpolation));
    }
    painter.end();
    finalImage.setDevicePixelRatio(finalDpr);
    ImageMetaData::setSubGeometryList(finalImage, geometryList);
    ImageMetaData::setLogicalXY(finalImage, origin.x(), origin.y());
    return finalImage;
}
}
