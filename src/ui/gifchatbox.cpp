#include "gifchatbox.h"
#include "settings.h"
#include <QDebug>
#include <QTextDocument>

GifChatBox::GifChatBox(QWidget *parent)
    : QTextEdit(parent)
{
    setReadOnly(true);

    m_gifTimer = new QTimer(this);
    connect(m_gifTimer, SIGNAL(timeout()), this, SLOT(updateGifFrames()));
    m_gifTimer->start(100);
}

GifChatBox::~GifChatBox()
{
    for (auto it = m_gifMovies.begin(); it != m_gifMovies.end(); ++it) {
        delete it.value();
    }
    m_gifMovies.clear();
}

QVariant GifChatBox::loadResource(int type, const QUrl &name)
{
    if (type == QTextDocument::ImageResource) {
        QString path = name.toString();

        if (path.toLower().endsWith(".gif")) {
            if (!m_gifMovies.contains(path)) {
                QMovie *movie = new QMovie(path);
                if (movie->isValid()) {
                    movie->setScaledSize(QSize(100, 100));
                    m_gifMovies[path] = movie;
                    movie->start();
                    qDebug() << "Created GIF animation:" << path;
                } else {
                    delete movie;
                    qDebug() << "GIF animation creation failed:" << path;
                    return QPixmap(path);
                }
            }

            QMovie *movie = m_gifMovies[path];
            if (movie && movie->isValid()) {
                return movie->currentPixmap();
            }
        }

        QPixmap originalPixmap(path);

        if (!originalPixmap.isNull() && originalPixmap.width() > 100) {
            QPixmap scaledPixmap = originalPixmap.scaled(100, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            return scaledPixmap;
        }

        return originalPixmap;
    }

    return QTextEdit::loadResource(type, name);
}

void GifChatBox::updateGifFrames()
{
    bool needUpdate = false;

    for (auto it = m_gifMovies.begin(); it != m_gifMovies.end(); ++it) {
        QMovie *movie = it.value();
        if (movie && movie->state() == QMovie::Running) {
            document()->addResource(QTextDocument::ImageResource,
                                  QUrl(it.key()), movie->currentPixmap());
            needUpdate = true;
        }
    }

    if (needUpdate) {
        viewport()->update();
    }
}