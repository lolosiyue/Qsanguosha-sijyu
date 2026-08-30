#include "replay-container.h"

#include <QCryptographicHash>
#include <QtMath>
#include <QtEndian>

#include <cstring>

using namespace QSanReplay;

namespace
{
constexpr char ContainerMagic[] = "QSANRPNG";
constexpr quint32 ContainerVersion = 1;
constexpr qsizetype DigestSize = 32;
constexpr qsizetype HeaderSize = sizeof(ContainerMagic) - 1
    + sizeof(quint32) + sizeof(quint64) + DigestSize;
}

QImage ReplayContainer::encodePng(const QByteArray &replayData)
{
    const QByteArray compressed = qCompress(replayData, 9);
    QByteArray container;
    container.reserve(HeaderSize + compressed.size());
    container.append(ContainerMagic, sizeof(ContainerMagic) - 1);
    const quint32 version = qToBigEndian(ContainerVersion);
    const quint64 size = qToBigEndian(static_cast<quint64>(compressed.size()));
    container.append(reinterpret_cast<const char *>(&version), sizeof(version));
    container.append(reinterpret_cast<const char *>(&size), sizeof(size));
    container.append(QCryptographicHash::hash(compressed, QCryptographicHash::Sha256));
    container.append(compressed);

    const int pixels = qMax(1, qCeil(container.size() / 4.0));
    const int width = qCeil(qSqrt(static_cast<double>(pixels)));
    QImage image(width, width, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    std::memcpy(image.bits(), container.constData(),
                static_cast<size_t>(container.size()));
    return image;
}

QByteArray ReplayContainer::decodePng(const QString &filename)
{
    QImage image(filename);
    if (image.isNull())
        return QByteArray();
    image = image.convertToFormat(QImage::Format_RGBA8888);
    if (image.bits() == nullptr
        || image.sizeInBytes() < HeaderSize) {
        return QByteArray();
    }

    const char *bytes = reinterpret_cast<const char *>(image.constBits());
    if (std::memcmp(bytes, ContainerMagic, sizeof(ContainerMagic) - 1) != 0)
        return QByteArray();

    qsizetype offset = sizeof(ContainerMagic) - 1;
    quint32 encodedVersion = 0;
    std::memcpy(&encodedVersion, bytes + offset, sizeof(encodedVersion));
    offset += sizeof(encodedVersion);
    if (qFromBigEndian(encodedVersion) != ContainerVersion)
        return QByteArray();

    quint64 encodedSize = 0;
    std::memcpy(&encodedSize, bytes + offset, sizeof(encodedSize));
    offset += sizeof(encodedSize);
    const quint64 size = qFromBigEndian(encodedSize);
    if (size > static_cast<quint64>(image.sizeInBytes() - HeaderSize))
        return QByteArray();

    const QByteArray expectedDigest(bytes + offset, DigestSize);
    offset += DigestSize;
    const QByteArray compressed(bytes + offset, static_cast<qsizetype>(size));
    if (QCryptographicHash::hash(compressed, QCryptographicHash::Sha256)
        != expectedDigest) {
        return QByteArray();
    }
    return qUncompress(compressed);
}
