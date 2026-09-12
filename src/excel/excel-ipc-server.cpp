#include "excel-ipc-server.h"

#include <QJsonDocument>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace {
const int MaxHeaders = 16 * 1024;
const int MaxBody = 1024 * 1024;
const int MaxReply = 4 * 1024 * 1024;
const int MaxClients = 16;
const int ReadDeadline = 5000;

bool equalSecret(const QByteArray &a, const QByteArray &b)
{
    const int n = qMax(a.size(), b.size());
    unsigned int difference = static_cast<unsigned int>(a.size() ^ b.size());
    for (int i = 0; i < n; ++i)
        difference |= static_cast<unsigned char>((i < a.size() ? a.at(i) : 0)
                                                 ^ (i < b.size() ? b.at(i) : 0));
    return difference == 0;
}

QByteArray headerValue(const QByteArray &headers, const QByteArray &name, bool *duplicate)
{
    QByteArray result;
    bool found = false;
    *duplicate = false;
    const QList<QByteArray> lines = headers.split('\n');
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        if (line.left(colon).trimmed().toLower() != name.toLower())
            continue;
        if (found)
            *duplicate = true;
        found = true;
        result = line.mid(colon + 1).trimmed();
    }
    return result;
}
}

struct ExcelIpcServer::Client {
    QTcpSocket *socket;
    QTimer *timer;
    QByteArray data;
    bool responded = false;
};

ExcelIpcServer::ExcelIpcServer(QObject *parent) : QObject(parent), m_server(new QTcpServer(this))
{
    connect(m_server, SIGNAL(newConnection()), this, SLOT(acceptConnections()));
}

ExcelIpcServer::~ExcelIpcServer() { close(); }

bool ExcelIpcServer::listen(const QString &session, const QString &token, QString *error)
{
    close();
    m_session = session;
    m_token = token.toUtf8();
    if (m_session.isEmpty() || m_token.isEmpty()) {
        if (error) *error = QStringLiteral("missing_credentials");
        return false;
    }
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        if (error) *error = m_server->errorString();
        return false;
    }
    return true;
}

quint16 ExcelIpcServer::port() const { return m_server->serverPort(); }

void ExcelIpcServer::close()
{
    m_draining = false;
    m_server->close();
    while (!m_clients.isEmpty())
        removeClient(m_clients.last());
}

void ExcelIpcServer::drain()
{
    m_server->close();
    m_draining = true;
    // Finish already queued responses, including the shutdown acknowledgement.
    // Incomplete requests cannot delay owned-process cleanup indefinitely.
    const QList<Client *> clients = m_clients;
    for (Client *client : clients)
        if (!client->responded) fail(client, 503, "session_stopping");
    if (m_clients.isEmpty()) emit drained();
}

void ExcelIpcServer::setHandler(const Handler &handler) { m_handler = handler; }

void ExcelIpcServer::acceptConnections()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (m_clients.size() >= MaxClients) {
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }
        Client *client = new Client;
        client->socket = socket;
        socket->setReadBufferSize(MaxHeaders + MaxBody + 1);
        client->timer = new QTimer(socket);
        client->timer->setSingleShot(true);
        client->timer->start(ReadDeadline);
        m_clients.append(client);
        connect(socket, SIGNAL(readyRead()), this, SLOT(readClient()));
        connect(socket, SIGNAL(disconnected()), this, SLOT(clientDisconnected()));
        connect(client->timer, SIGNAL(timeout()), this, SLOT(clientTimedOut()));
    }
}

void ExcelIpcServer::readClient()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    Client *client = nullptr;
    for (int i = 0; i < m_clients.size(); ++i) if (m_clients.at(i)->socket == socket) client = m_clients.at(i);
    if (!client || client->responded) return;
    client->data += socket->read(MaxHeaders + MaxBody + 1 - client->data.size());
    QString method, path; QUrlQuery query; QJsonObject body; int status = 0; const char *code = nullptr;
    const bool complete = parseRequest(client, &method, &path, &query, &body, &status, &code);
    if (!complete) return;
    if (status) { fail(client, status, code); return; }
    if (!authenticate(client->data.left(client->data.indexOf("\r\n\r\n") + 4))) { fail(client, 401, "unauthorized"); return; }
    Reply reply = m_handler ? m_handler(method, path, query, body) : Reply{503, QJsonObject()};
    finish(client, reply);
}

bool ExcelIpcServer::parseRequest(Client *client, QString *method, QString *path,
                                  QUrlQuery *query, QJsonObject *body, int *status,
                                  const char **code) const
{
    *status = 0; *code = nullptr;
    const int end = client->data.indexOf("\r\n\r\n");
    if (end < 0) { if (client->data.size() > MaxHeaders) { *status=431; *code="headers_too_large"; return true; } return false; }
    if (end + 4 > MaxHeaders) { *status=431; *code="headers_too_large"; return true; }
    const QByteArray headers = client->data.left(end);
    const QList<QByteArray> first = headers.split('\n').first().trimmed().split(' ');
    if (first.size() != 3 || first.at(2) != "HTTP/1.1") { *status=400; *code="bad_request"; return true; }
    *method = QString::fromLatin1(first.at(0));
    const QUrl target(QString::fromUtf8(first.at(1)));
    *path = target.path(); *query = QUrlQuery(target);
    bool duplicate = false; const QByteArray length = headerValue(headers, "content-length", &duplicate);
    if (duplicate || length.isEmpty() && (*method == "POST")) { *status=400; *code="invalid_content_length"; return true; }
    bool ok = true; const qint64 size = length.isEmpty() ? 0 : length.toLongLong(&ok);
    if (!ok || size < 0 || size > MaxBody) { *status=413; *code="body_too_large"; return true; }
    bool transferDuplicate = false; const QByteArray transfer = headerValue(headers, "transfer-encoding", &transferDuplicate);
    if (transferDuplicate || !transfer.isEmpty()) { *status=400; *code="chunked_not_supported"; return true; }
    if (client->data.size() < end + 4 + size) return false;
    const QByteArray payload = client->data.mid(end + 4, size);
    if (!payload.isEmpty()) {
        QJsonParseError parseError; const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) { *status=400; *code="invalid_json"; return true; }
        *body = document.object();
    }
    return true;
}

bool ExcelIpcServer::authenticate(const QByteArray &headers) const
{
    bool authDuplicate = false, sessionDuplicate = false;
    const QByteArray auth = headerValue(headers, "authorization", &authDuplicate);
    const QByteArray session = headerValue(headers, "x-qsan-session", &sessionDuplicate);
    return !authDuplicate && !sessionDuplicate && equalSecret(auth, QByteArray("Bearer ") + m_token)
        && equalSecret(session, m_session.toUtf8());
}

void ExcelIpcServer::finish(Client *client, const Reply &reply)
{
    const QByteArray payload = QJsonDocument(reply.body).toJson(QJsonDocument::Compact);
    if (payload.size() > MaxReply) { fail(client, 500, "response_too_large"); return; }
    const QByteArray reason = reply.status == 200 ? "OK" : "Error";
    QByteArray response = "HTTP/1.1 " + QByteArray::number(reply.status) + " " + reason + "\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: " + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload;
    client->responded = true;
    client->timer->start(ReadDeadline);
    client->socket->write(response);
    client->socket->disconnectFromHost();
}

void ExcelIpcServer::fail(Client *client, int status, const char *code)
{ QJsonObject body; body.insert(QStringLiteral("ok"), false); body.insert(QStringLiteral("error"), QString::fromLatin1(code)); Reply reply; reply.status=status; reply.body=body; finish(client, reply); }
void ExcelIpcServer::clientDisconnected() { QTcpSocket *s=qobject_cast<QTcpSocket *>(sender()); for (Client *c : m_clients) if(c->socket==s){removeClient(c);return;} }
void ExcelIpcServer::clientTimedOut()
{
    QTimer *timer = qobject_cast<QTimer *>(sender());
    for (Client *client : m_clients) {
        if (client->timer != timer) continue;
        if (!client->responded) fail(client, 408, "request_timeout");
        else removeClient(client);
        return;
    }
}
void ExcelIpcServer::removeClient(Client *client)
{
    m_clients.removeOne(client);
    client->socket->disconnect(this);
    client->timer->stop();
    client->socket->abort();
    client->socket->deleteLater();
    delete client;
    if (m_draining && m_clients.isEmpty()) emit drained();
}
