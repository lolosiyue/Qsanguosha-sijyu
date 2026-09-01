#include "websocketsocket.h"

#include "protocol/protocol-runtime.h"
#include "settings.h"

#include <QtNetwork>
#include <QtWebSockets/QWebSocket>
#include <QtWebSockets/QWebSocketCorsAuthenticator>

namespace {

QHostAddress configuredBindAddress(QString *error)
{
    const QString configured = Config.BindAddress.trimmed().toLower();
    QHostAddress address;
    if (configured.isEmpty() || configured == QLatin1String("any"))
        return QHostAddress::Any;
    if (configured == QLatin1String("any-ipv4"))
        return QHostAddress::AnyIPv4;
    if (configured == QLatin1String("any-ipv6"))
        return QHostAddress::AnyIPv6;
    if (!address.setAddress(configured)) {
        if (error != nullptr)
            *error = QStringLiteral("Invalid server bind address: %1").arg(Config.BindAddress);
        return QHostAddress();
    }
    return address;
}

}

WebSocketServerSocket::WebSocketServerSocket()
    : server(new QWebSocketServer(QStringLiteral("QSanguosha"),
                                  QWebSocketServer::NonSecureMode, this))
{
    connect(server, &QWebSocketServer::newConnection,
            this, &WebSocketServerSocket::processNewConnection);
    connect(server, &QWebSocketServer::originAuthenticationRequired,
            this, &WebSocketServerSocket::allowOrigin);
}

bool WebSocketServerSocket::listen()
{
    QString addressError;
    const QHostAddress address = configuredBindAddress(&addressError);
    if (!addressError.isEmpty()) {
        qWarning("%s", qPrintable(addressError));
        return false;
    }
    if (server->listen(address, Config.WebSocketPort))
        return true;

    qWarning("Unable to listen for WebSocket on %s:%u: %s",
        qPrintable(address.toString()), static_cast<unsigned int>(Config.WebSocketPort),
        qPrintable(server->errorString()));
    return false;
}

void WebSocketServerSocket::daemonize()
{
}

QString WebSocketServerSocket::listeningAddress() const
{
    return server->serverAddress().toString();
}

quint16 WebSocketServerSocket::listeningPort() const
{
    return server->serverPort();
}

void WebSocketServerSocket::processNewConnection()
{
    QWebSocket *socket = server->nextPendingConnection();
    if (socket == nullptr)
        return;
    emit new_connection(new WebSocketClientSocket(socket));
}

void WebSocketServerSocket::allowOrigin(QWebSocketCorsAuthenticator *authenticator)
{
    if (authenticator != nullptr)
        authenticator->setAllowed(true);
}

WebSocketClientSocket::WebSocketClientSocket(QWebSocket *socket)
    : socket(socket)
{
    socket->setParent(this);
    socket->setMaxAllowedIncomingFrameSize(
        static_cast<quint64>(QSanProtocol::ProtocolFrameBuffer::MaxFrameSize));
    socket->setMaxAllowedIncomingMessageSize(
        static_cast<quint64>(QSanProtocol::ProtocolFrameBuffer::MaxFrameSize));
    connect(socket, &QWebSocket::disconnected, this, &ClientSocket::disconnected);
    connect(socket, &QWebSocket::textMessageReceived,
            this, &WebSocketClientSocket::getMessage);
    connect(socket, &QWebSocket::binaryMessageReceived,
            this, &WebSocketClientSocket::rejectBinary);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(socket, &QWebSocket::errorOccurred,
            this, &WebSocketClientSocket::raiseError);
#else
    connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &WebSocketClientSocket::raiseError);
#endif
    timerSignup.setSingleShot(true);
    connect(&timerSignup, &QTimer::timeout, this, &WebSocketClientSocket::disconnectFromHost);
}

void WebSocketClientSocket::connectToHost()
{
}

void WebSocketClientSocket::disconnectFromHost()
{
    socket->close();
}

void WebSocketClientSocket::send(const QByteArray &message)
{
    if (!isValidProtocolFrame(message)) {
        emit error_message(QStringLiteral("Refusing invalid protocol frame"));
        disconnectFromHost();
        return;
    }
    socket->sendTextMessage(QString::fromUtf8(message));
    socket->flush();
}

bool WebSocketClientSocket::isConnected() const
{
    return socket->state() == QAbstractSocket::ConnectedState;
}

QString WebSocketClientSocket::peerName() const
{
    QString peer_name = socket->peerName();
    if (peer_name.isEmpty())
        peer_name = QString("%1:%2").arg(socket->peerAddress().toString()).arg(socket->peerPort());
    return peer_name;
}

QString WebSocketClientSocket::peerAddress() const
{
    return socket->peerAddress().toString();
}

void WebSocketClientSocket::getMessage(const QString &message)
{
    const QByteArray frame = message.toUtf8();
    if (!isValidProtocolFrame(frame)) {
        emit error_message(QStringLiteral("Refusing invalid protocol frame"));
        disconnectFromHost();
        return;
    }
#ifndef QT_NO_DEBUG
    if (qEnvironmentVariableIsSet("QSAN_PROTOCOL_DEBUG_SENSITIVE"))
        qDebug().noquote() << "WS RX (may contain private game state):"
                           << message;
#endif
    emit message_got(frame);
}

void WebSocketClientSocket::rejectBinary(const QByteArray &)
{
    emit error_message(QStringLiteral("WebSocket binary frames are not allowed"));
    disconnectFromHost();
}

void WebSocketClientSocket::raiseError(QAbstractSocket::SocketError socket_error)
{
    QString reason;
    switch (socket_error) {
    case QAbstractSocket::ConnectionRefusedError:
        reason = tr("Connection was refused or timeout"); break;
    case QAbstractSocket::RemoteHostClosedError:
        reason = tr("Remote host close this connection"); break;
    case QAbstractSocket::HostNotFoundError:
        reason = tr("Host not found"); break;
    case QAbstractSocket::SocketAccessError:
        reason = tr("Socket access error"); break;
    case QAbstractSocket::NetworkError:
        return;
    default:
        reason = tr("Unknow error"); break;
    }

    emit error_message(tr("Connection failed, error code = %1\n reason:\n %2")
                           .arg(socket_error).arg(reason));
}

bool WebSocketClientSocket::isValidProtocolFrame(const QByteArray &message)
{
    return !message.isEmpty()
        && message.size() <= QSanProtocol::ProtocolFrameBuffer::MaxFrameSize
        && !message.contains('\n')
        && !message.contains('\r');
}
