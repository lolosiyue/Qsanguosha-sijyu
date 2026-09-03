#include "nativesocket.h"
#include "settings.h"
#include <QtNetwork>

NativeServerSocket::NativeServerSocket()
{
    server = new QTcpServer(this);
    daemon = nullptr;
    connect(server, SIGNAL(newConnection()), this, SLOT(processNewConnection()));
}

bool NativeServerSocket::listen()
{
    const QString configured = Config.BindAddress.trimmed().toLower();
    QHostAddress address;
    if (configured.isEmpty() || configured == QLatin1String("any"))
        address = QHostAddress::Any;
    else if (configured == QLatin1String("any-ipv4"))
        address = QHostAddress::AnyIPv4;
    else if (configured == QLatin1String("any-ipv6"))
        address = QHostAddress::AnyIPv6;
    else if (!address.setAddress(configured)) {
        qWarning("Invalid server bind address: %s", qPrintable(Config.BindAddress));
        return false;
    }
    if (server->listen(address, Config.ServerPort))
        return true;

    qWarning("Unable to listen on %s:%u: %s",
        qPrintable(address.toString()), static_cast<unsigned int>(Config.ServerPort),
        qPrintable(server->errorString()));
    return false;
}

void NativeServerSocket::daemonize()
{
    daemon = new QUdpSocket(this);
    daemon->bind(Config.ServerPort, QUdpSocket::ShareAddress);
    connect(daemon, SIGNAL(readyRead()), this, SLOT(processNewDatagram()));
}

QString NativeServerSocket::listeningAddress() const
{
    return server->serverAddress().toString();
}

quint16 NativeServerSocket::listeningPort() const
{
    return server->serverPort();
}

void NativeServerSocket::processNewDatagram()
{
    while (daemon->hasPendingDatagrams()) {
        QHostAddress from;
        char ask_str[256];

        daemon->readDatagram(ask_str, sizeof(ask_str), &from);

        QByteArray data = Config.ServerName.toUtf8();
        daemon->writeDatagram(data, from, Config.DetectorPort);
        daemon->flush();
    }
}

void NativeServerSocket::processNewConnection()
{
    QTcpSocket *socket = server->nextPendingConnection();
    NativeClientSocket *connection = new NativeClientSocket(socket);
    emit new_connection(connection);
}

// ---------------------------------

NativeClientSocket::NativeClientSocket()
    : socket(new QTcpSocket(this))
{
    init();
}

NativeClientSocket::NativeClientSocket(QTcpSocket *socket)
    : socket(socket)
{
    socket->setParent(this);
    init();
    timerSignup.setSingleShot(true);
    connect(&timerSignup,SIGNAL(timeout()),this,SLOT(disconnectFromHost()));
}

void NativeClientSocket::init()
{
    connect(socket, SIGNAL(disconnected()), this, SIGNAL(disconnected()));
    connect(socket, SIGNAL(readyRead()), this, SLOT(getMessage()));
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(socket, SIGNAL(errorOccurred(QAbstractSocket::SocketError)),
        this, SLOT(raiseError(QAbstractSocket::SocketError)));
#else
    connect(socket, SIGNAL(error(QAbstractSocket::SocketError)),
        this, SLOT(raiseError(QAbstractSocket::SocketError)));
#endif
    connect(socket, SIGNAL(connected()), this, SIGNAL(connected()));
}

void NativeClientSocket::connectToHost()
{
    QString address = "127.0.0.1";
    ushort port = 9527u;

    if (Config.HostAddress.contains(QChar(':'))) {
        QStringList texts = Config.HostAddress.split(QChar(':'));
        address = texts.value(0);
        port = texts.value(1).toUShort();
    } else {
        address = Config.HostAddress;
        if (address == "127.0.0.1")
            port = Config.value("ServerPort", "9527").toString().toUShort();
    }

    socket->connectToHost(address, port);
}

void NativeClientSocket::connectToHost(const QString &host, quint16 port)
{
    socket->connectToHost(host, port);
}

void NativeClientSocket::getMessage()
{
    const QSanProtocol::ProtocolFrameAppendResult result =
        m_frameBuffer.append(socket->readAll());
    if (!result.success) {
        emit error_message(result.detail);
        disconnectFromHost();
        return;
    }

    for (const QByteArray &message : result.frames) {
#ifndef QT_NO_DEBUG
        if (qEnvironmentVariableIsSet("QSAN_PROTOCOL_DEBUG_SENSITIVE"))
            qDebug().noquote() << "RX (may contain private game state):"
                               << QString::fromUtf8(message);
#endif
        emit message_got(message);
    }
}

void NativeClientSocket::disconnectFromHost()
{
    socket->disconnectFromHost();
}

void NativeClientSocket::abort()
{
    m_frameBuffer.clear();
    socket->abort();
}

void NativeClientSocket::send(const QByteArray &message)
{
    if (message.isEmpty() || message.size() > QSanProtocol::ProtocolFrameBuffer::MaxFrameSize
        || message.contains('\n') || message.contains('\r')) {
        emit error_message(QStringLiteral("Refusing invalid protocol frame"));
        disconnectFromHost();
        return;
    }
    socket->write(message);
    socket->write("\n", 1);
#ifndef QT_NO_DEBUG
    if (qEnvironmentVariableIsSet("QSAN_PROTOCOL_DEBUG_SENSITIVE"))
        qDebug().noquote() << "TX (may contain private game state):"
                           << QString::fromUtf8(message);
#endif
    socket->flush();
}

bool NativeClientSocket::isConnected() const
{
    return socket->state() == QTcpSocket::ConnectedState;
}

QString NativeClientSocket::peerName() const
{
    QString peer_name = socket->peerName();
    if (peer_name.isEmpty())
        peer_name = QString("%1:%2").arg(socket->peerAddress().toString()).arg(socket->peerPort());

    return peer_name;
}

QString NativeClientSocket::peerAddress() const
{
    return socket->peerAddress().toString();
}

QAbstractSocket::SocketError NativeClientSocket::lastError() const
{
    return socket->error();
}

QString NativeClientSocket::errorString() const
{
    return socket->errorString();
}

void NativeClientSocket::raiseError(QAbstractSocket::SocketError socket_error)
{
    // translate error message
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
        return; // this error is ignored ...
    default: reason = tr("Unknow error"); break;
    }

    emit error_message(tr("Connection failed, error code = %1\n reason:\n %2").arg(socket_error).arg(reason));
}
