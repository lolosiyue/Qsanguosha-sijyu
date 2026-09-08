#include "xp-control-protocol.h"
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QtEndian>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif
#ifndef QSAN_XP_BUILD_ID
#define QSAN_XP_BUILD_ID "unpaired-test-build"
#endif

namespace XpControl {
bool integer(const QJsonValue &value, int minimum, int maximum, int *result)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < minimum || number > maximum)
        return false;
    if (result)
        *result = static_cast<int>(number);
    return true;
}

bool decimal(const QString &text, quint64 *result)
{
    if (text.isEmpty() || text.size() > 20 || (text.size() > 1 && text.at(0) == QLatin1Char('0')))
        return false;
    for (const QChar ch : text)
        if (ch < QLatin1Char('0') || ch > QLatin1Char('9'))
            return false;
    bool ok = false;
    const quint64 value = text.toULongLong(&ok);
    if (ok && result)
        *result = value;
    return ok;
}

QString randomIdentity(QString *error)
{
#ifdef Q_OS_WIN
    HCRYPTPROV provider = 0;
    BYTE bytes[32];
    if (CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL,
                            CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        const bool ok = CryptGenRandom(provider, sizeof(bytes), bytes) != FALSE;
        CryptReleaseContext(provider, 0);
        if (ok)
            return QString::fromLatin1(QByteArray(reinterpret_cast<char *>(bytes), sizeof(bytes)).toHex());
    }
#endif
    if (error)
        *error = QStringLiteral("secure_random_unavailable");
    return QString();
}

QString fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();
    return QString::fromLatin1(hash.result().toHex());
}

QString runtimeHash(const QString &root, QString *error)
{
    QStringList files;
    for (const QString &subdir : {QStringLiteral("lua"), QStringLiteral("extensions"), QStringLiteral("lang")}) {
        QDirIterator it(QDir(root).filePath(subdir), QStringList() << "*.lua",
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = QDir(root).relativeFilePath(it.next());
            if (!path.contains("/.git/") && !path.contains("/logs/")
                && !path.contains("/temp/") && !path.contains("/data/"))
                files << path;
        }
    }
    files.sort();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString &path : files) {
        const QString digest = fileHash(QDir(root).filePath(path));
        if (digest.isEmpty()) {
            if (error) *error = QStringLiteral("runtime_unreadable: ") + path;
            return QString();
        }
        hash.addData(path.toUtf8());
        hash.addData("\0", 1);
        hash.addData(digest.toLatin1());
    }
    if (files.isEmpty()) {
        if (error) *error = QStringLiteral("runtime_empty");
        return QString();
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString buildIdentity() { return QStringLiteral(QSAN_XP_BUILD_ID); }

QJsonObject envelope(const QString &session, const QString &generation,
                     const QString &id, const QString &type, const QJsonObject &body)
{
    return {{"version", Version}, {"session", session}, {"generation", generation},
            {"id", id}, {"type", type}, {"body", body}};
}

bool validEnvelope(const QJsonObject &message, const QString &session, const QString &generation)
{
    return integer(message.value("version"), Version, Version)
        && message.value("session").toString() == session && !session.isEmpty()
        && message.value("generation").toString() == generation && decimal(generation)
        && decimal(message.value("id").toString())
        && message.value("type").isString() && message.value("body").isObject();
}

Channel::Channel(QLocalSocket *socket, QObject *parent)
    : QObject(parent), m_socket(socket)
{
    socket->setReadBufferSize(MaximumQueue);
    connect(socket, &QLocalSocket::readyRead, this, &Channel::read);
}

void Channel::fail(const QString &code)
{
    if (m_failed) return;
    m_failed = true;
    emit failed(code);
    m_socket->abort();
}

bool Channel::send(const QJsonObject &message)
{
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (m_failed || payload.size() > MaximumFrame
        || m_socket->bytesToWrite() + payload.size() + 4 > MaximumQueue) {
        fail(QStringLiteral("output_limit"));
        return false;
    }
    QByteArray frame(4, '\0');
    qToBigEndian<quint32>(quint32(payload.size()), reinterpret_cast<uchar *>(frame.data()));
    frame.append(payload);
    if (m_socket->write(frame) != frame.size()) {
        fail(QStringLiteral("write_failed"));
        return false;
    }
    return true;
}

void Channel::read()
{
    while (!m_failed && m_socket->bytesAvailable() > 0) {
        m_input.append(m_socket->read(qMin<qint64>(4096, MaximumQueue - m_input.size())));
        while (m_input.size() >= 4 && !m_failed) {
            const quint32 size = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(m_input.constData()));
            if (!size || size > MaximumFrame) { fail(QStringLiteral("frame_size")); return; }
            if (m_input.size() < int(size) + 4) break;
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(m_input.mid(4, int(size)), &error);
            m_input.remove(0, int(size) + 4);
            if (error.error != QJsonParseError::NoError || !document.isObject()) {
                fail(QStringLiteral("invalid_json")); return;
            }
            emit message(document.object());
        }
        if (m_input.size() >= MaximumQueue) { fail(QStringLiteral("input_limit")); return; }
    }
}
}
