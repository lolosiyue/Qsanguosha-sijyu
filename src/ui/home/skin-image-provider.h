#ifndef _SKIN_IMAGE_PROVIDER_H
#define _SKIN_IMAGE_PROVIDER_H

#include <QQuickImageProvider>
#include <QUrl>

// Serves image://qsanskin/<asset path> from the room skin, so QML pages can show
// art the skin generates when the image files are not installed.
class SkinImageProvider : public QQuickImageProvider
{
public:
    static const QString Id;

    SkinImageProvider();
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;

    // The provider URL for path when the file is missing and the skin can draw it; otherwise empty.
    static QUrl generatedUrl(const QString &path);
};

#endif
