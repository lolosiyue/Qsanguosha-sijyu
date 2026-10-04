#include "external-agent-transport.h"
#include <QHostAddress>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTimer>
#include <QSet>

namespace {
constexpr qint64 MaxFrame = 256 * 1024;
constexpr qint64 MaxOutput = 8 * 1024 * 1024;
bool only(const QJsonObject &o, const QSet<QString> &keys) {
    for (auto it = o.begin(); it != o.end(); ++it) if (!keys.contains(it.key())) return false;
    return true;
}
}

ExternalAgentLocalTransport::ExternalAgentLocalTransport(
    std::shared_ptr<ExternalAgentEndpoint> endpoint, QObject *parent)
    : QObject(parent), m_endpoint(std::move(endpoint)), m_server(this)
{
    Q_ASSERT(m_endpoint);
    // 256 random bits, memory-only. Never persisted or placed in command arguments.
    for (int i = 0; i < 4; ++i)
        m_token += QString::number(QRandomGenerator::system()->generate64(), 16).rightJustified(16, '0');
    m_endpoint->disconnect();
    m_server.setMaxPendingConnections(1);
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
        while (auto *socket = m_server.nextPendingConnection()) {
            if (m_client || socket->peerAddress() != QHostAddress::LocalHost) {
                qWarning() << "EXTERNAL_TRANSPORT connection-refused";
                socket->abort(); socket->deleteLater(); continue;
            }
            m_client = socket;
            m_authenticated = false;
            socket->setReadBufferSize(MaxFrame + 1);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { incoming(socket); });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                if (m_client == socket) {
                    if (m_authenticated) m_endpoint->disconnect();
                    m_client.clear(); m_authenticated = false;
                }
                socket->deleteLater();
            });
            QTimer::singleShot(10000, socket, [this, socket] {
                // A handshake timeout is a transport budget, never a game deadline.
                if (m_client == socket && !m_authenticated) {
                    qWarning() << "EXTERNAL_TRANSPORT handshake-timeout"; socket->abort();
                }
            });
        }
    });
}
ExternalAgentLocalTransport::~ExternalAgentLocalTransport() { close(); }

bool ExternalAgentLocalTransport::listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
QJsonObject ExternalAgentLocalTransport::bootstrap() const {
    return {{"version",1},{"host","127.0.0.1"},{"port",int(m_server.serverPort())},{"token",m_token}};
}
void ExternalAgentLocalTransport::complete(const QString &winner) {
    m_complete = true; m_winner = winner;
}
void ExternalAgentLocalTransport::close() {
    m_server.close();
    if (m_client) {
        QObject::disconnect(m_client, nullptr, this, nullptr);
        m_client->abort(); m_client->deleteLater(); m_client.clear();
    }
    m_authenticated = false;
    m_endpoint->disconnect();
}
void ExternalAgentLocalTransport::send(QTcpSocket *socket, const QJsonObject &object) {
    const QByteArray frame = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    if (frame.size() > MaxOutput || socket->bytesToWrite() + frame.size() > MaxOutput) {
        qWarning() << "EXTERNAL_TRANSPORT output-limit" << frame.size() << socket->bytesToWrite();
        socket->abort(); return;
    }
    socket->write(frame);
}
void ExternalAgentLocalTransport::incoming(QTcpSocket *socket) {
    // Bounded framing and work per event-loop turn; no blocking socket calls.
    for (int i = 0; i < 16 && m_client == socket; ++i) {
        if (socket->property("rejected").toBool()) return;
        if (!socket->canReadLine()) {
            if (socket->bytesAvailable() > MaxFrame) {
                qWarning() << "EXTERNAL_TRANSPORT input-limit"; socket->abort();
            }
            return;
        }
        const QByteArray line = socket->readLine(MaxFrame + 1);
        if (line.size() > MaxFrame || !line.endsWith('\n')) {
            qWarning() << "EXTERNAL_TRANSPORT frame-limit" << line.size(); socket->abort(); return;
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            send(socket, {{"ok",false},{"error","invalid-json"}}); continue;
        }
        message(socket, doc.object());
    }
    if (m_client == socket && socket->canReadLine())
        QTimer::singleShot(0, socket, [this, socket] { incoming(socket); });
}
void ExternalAgentLocalTransport::message(QTcpSocket *socket, const QJsonObject &o) {
    if (!m_authenticated) {
        if (!only(o,{"op","version","token"}) || o["op"] != "hello"
            || o["version"] != 1 || o["token"] != m_token) {
            send(socket, {{"ok",false},{"error","unauthorized"}});
            socket->setProperty("rejected",true);
            socket->disconnectFromHost(); return;
        }
        m_authenticated = true; m_endpoint->reconnect();
        send(socket, {{"ok",true},{"version",1}}); return;
    }
    const auto op = o["op"].toString();
    if (op == "poll" && only(o,{"op"})) {
        if (m_complete) { send(socket,{{"status","finished"},{"winner",m_winner}}); return; }
        AIRequest request;
        QJsonObject reply{{"status",m_endpoint->status()},{"lastError",m_endpoint->lastError()}};
        if (m_endpoint->pending(request)) reply.insert("request",externalAgentRequestJson(request));
        send(socket,reply);
    } else if (op == "submit" && only(o,{"op","result"})) {
        AIResult result;
        QString error;
        bool accepted = false;
        if (!o["result"].isObject() || !externalAgentResultJson(o["result"].toObject(),result))
            error = "invalid-result";
        else accepted = m_endpoint->submit(result,&error);
        send(socket,{{"ok",accepted},{"queued",accepted},{"error",error}});
    } else if (op == "cancel" && only(o,{"op"})) {
        m_endpoint->cancel(); send(socket,{{"ok",true},{"status","cancelled"}});
    } else send(socket,{{"ok",false},{"error","unknown-operation"}});
}
