#ifndef LOCAL_RESPONSE_UI_CASE_H
#define LOCAL_RESPONSE_UI_CASE_H

#include "json.h"
#include "protocol.h"

#include <QJsonObject>
#include <ctime>

class LocalResponseUiCase
{
public:
    static bool load(const QString &path, LocalResponseUiCase *result, QString *error);

    QString path() const;
    QString name() const;
    QJsonObject root() const;
    QJsonObject bootstrap() const;
    QJsonObject request() const;
    QJsonArray actions() const;
    QJsonObject presentedExpectation() const;
    QJsonObject replyExpectation() const;
    QJsonObject finalExpectation() const;

    bool makeRequestPacket(QSanProtocol::Packet *packet, QString *commandName,
        QVariant *body, QString *error) const;

    static bool commandFromName(const QString &name,
        QSanProtocol::CommandType *command);
    static QString commandName(QSanProtocol::CommandType command);

private:
    QString m_path;
    QJsonObject m_root;
};

#endif
