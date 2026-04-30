#ifndef _GIFCHATBOX_H
#define _GIFCHATBOX_H

#include <QTextEdit>
#include <QMovie>
#include <QMap>
#include <QTimer>
#include <QUrl>
#include <QPixmap>

class GifChatBox : public QTextEdit
{
    Q_OBJECT

public:
    explicit GifChatBox(QWidget *parent = nullptr);
    ~GifChatBox();

protected:
    QVariant loadResource(int type, const QUrl &name) override;

private slots:
    void updateGifFrames();

private:
    QMap<QString, QMovie*> m_gifMovies;
    QTimer *m_gifTimer;
};

#endif