#include "skin-image-provider.h"
#include "skin-bank.h"
#include "runtime-paths.h"

#include <QFile>

const QString SkinImageProvider::Id = QStringLiteral("qsanskin");

// Pixmap providers run on the GUI thread, which the skin requires.
SkinImageProvider::SkinImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
{
}

QPixmap SkinImageProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    // Callers may tag the URL with a cache-busting query.
    const QString path = id.section('?', 0, 0);
    QPixmap pixmap;
    if (!G_ROOM_SKIN.loadPixmap(pixmap, path))
        pixmap = QPixmap(1, 1);
    if (size)
        *size = pixmap.size();
    if (requestedSize.width() > 0 && requestedSize.height() > 0)
        return pixmap.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return pixmap;
}

QUrl SkinImageProvider::generatedUrl(const QString &path)
{
    if (QFile::exists(QSanRuntimePaths::assetPath(path)) || !G_ROOM_SKIN.generatesFile(path))
        return QUrl();
    return QUrl(QStringLiteral("image://%1/%2").arg(Id, path));
}
