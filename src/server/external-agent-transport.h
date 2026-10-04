#ifndef QSAN_EXTERNAL_AGENT_TRANSPORT_H
#define QSAN_EXTERNAL_AGENT_TRANSPORT_H

#include "external-agent.h"
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QPointer>

QJsonObject externalAgentRequestJson(const AIRequest &request);
bool externalAgentResultJson(const QJsonObject &object, AIResult &result);

// Own on the host event-loop thread. One listener is a capability for one seat.
// The bootstrap capability is ephemeral and must be passed through a private pipe.
class ExternalAgentLocalTransport : public QObject
{
public:
    explicit ExternalAgentLocalTransport(std::shared_ptr<ExternalAgentEndpoint> endpoint,
                                         QObject *parent = nullptr);
    ~ExternalAgentLocalTransport() override;
    bool listen(); // IPv4 loopback only, OS-assigned port; no public bind option
    QJsonObject bootstrap() const;
    void complete(const QString &winner);
    void close();
private:
    void incoming(QTcpSocket *socket);
    void message(QTcpSocket *socket, const QJsonObject &object);
    void send(QTcpSocket *socket, const QJsonObject &object);
    std::shared_ptr<ExternalAgentEndpoint> m_endpoint;
    QTcpServer m_server;
    QPointer<QTcpSocket> m_client;
    QString m_token;
    QString m_winner;
    bool m_authenticated = false;
    bool m_complete = false;
};
#endif
