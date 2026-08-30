#ifndef QSAN_REPLAY_CONTAINER_H
#define QSAN_REPLAY_CONTAINER_H

#include <QByteArray>
#include <QImage>
#include <QString>

namespace QSanReplay {

class ReplayContainer
{
public:
    static QImage encodePng(const QByteArray &replayData);
    static QByteArray decodePng(const QString &filename);
};

}

#endif
