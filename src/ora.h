#pragma once

#include "document.h"
#include <QString>

// OpenRaster (.ora) — an open, layered interchange format supported by GIMP, Krita, MyPaint.
namespace Ora {
bool save(const QString &path, const QVector<Layer> &layers, const QSize &size, double dpi, const QImage &merged,
          QString *error = nullptr);
bool load(const QString &path, QVector<Layer> &layers, QSize &size, double &dpi, QString *error = nullptr);
}
