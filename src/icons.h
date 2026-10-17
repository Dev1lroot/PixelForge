#pragma once

#include <QIcon>
#include <QCursor>
#include <QString>

namespace Icons {
// All artwork is drawn procedurally at runtime (16x16 logical units).
QIcon get(const QString &name);
QPixmap pixmap(const QString &name, int size = 16);
QCursor toolCursor(const QString &iconName, bool crosshair = true);
QPixmap checkerboard(int cell = 4);
}
