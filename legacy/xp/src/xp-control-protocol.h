#ifndef QSAN_XP_CONTROL_PROTOCOL_H
#define QSAN_XP_CONTROL_PROTOCOL_H

#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>

namespace XpControl {
enum { Version = 1, MaximumFrame = 256 * 1024, MaximumQueue = 512 * 1024 };
bool integer(const QJsonValue &value, int minimum, int maximum, int *result = nullptr);
bool decimal(const QString &text, quint64 *result = nullptr);
QString randomIdentity(QString *error);
QString fileHash(const QString &path);
QString runtimeHash(const QString &root, QString *error);
QString buildIdentity();
QJsonObject envelope(const QString &session, const QString &generation,
                     const QString &id, const QString &type, const QJsonObject &body = {});
bool validEnvelope(const QJsonObject &message, const QString &session,
                   const QString &generation);

// One bounded decoder per connection; partial and coalesced frames share this path.
class Channel : public QObject
{
    Q_OBJECT
public:
    explicit Channel(QLocalSocket *socket, QObject *parent = nullptr);
    bool send(const QJsonObject &message);
    QLocalSocket *socket() const { return m_socket; }
signals:
    void message(const QJsonObject &message);
    void failed(const QString &code);
private:
    void read();
    void fail(const QString &code);
    QLocalSocket *m_socket;
    QByteArray m_input;
    bool m_failed = false;
};
}
#endif
