#include "nativesocket.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QPointer>
#include <QTimer>
#include <cstdio>

namespace {
enum class Action { Keep, Delete, NestedDelete, Abort, ErrorDelete };

bool runCase(Action action)
{
    QTcpServer listener;
    if (!listener.listen(QHostAddress::LocalHost, 0)) return false;
    QTcpSocket peer;
    peer.connectToHost(QHostAddress::LocalHost, listener.serverPort());
    if (!peer.waitForConnected(1000) || !listener.waitForNewConnection(1000)) return false;
    QTcpSocket *accepted = listener.nextPendingConnection();
    QPointer<NativeClientSocket> transport = new NativeClientSocket(accepted);
    // Accumulate one complete batch before entering the production receiver.
    QObject::disconnect(accepted, SIGNAL(readyRead()), transport, SLOT(getMessage()));
    int messages = 0;
    int errors = 0;
    QObject context;
    QObject::connect(transport, &ClientSocket::message_got, &context,
        [&](const QByteArray &) {
            ++messages;
            if (action == Action::Delete) delete transport.data();
            if (action == Action::Abort) transport->abort();
            if (action == Action::NestedDelete) {
                // Model the game-over modal loop tearing down its owning client.
                QEventLoop loop;
                QTimer::singleShot(0, &loop, [&]() {
                    delete transport.data();
                    loop.quit();
                });
                loop.exec();
            }
        });
    QObject::connect(transport, &ClientSocket::error_message, &context,
        [&](const QString &) { ++errors; delete transport.data(); });
    const QByteArray bytes = action == Action::ErrorDelete
        ? QByteArray(QSanProtocol::ProtocolFrameBuffer::MaxFrameSize + 1, 'x')
        : QByteArray("first\nsecond\n");
    peer.write(bytes);
    if (!peer.waitForBytesWritten(1000)) { delete transport.data(); return false; }
    QElapsedTimer timer;
    timer.start();
    while (accepted->bytesAvailable() < bytes.size() && timer.elapsed() < 2000)
        accepted->waitForReadyRead(100);
    if (accepted->bytesAvailable() != bytes.size()) { delete transport.data(); return false; }
    const bool invoked = QMetaObject::invokeMethod(transport, "getMessage", Qt::DirectConnection);
    const bool passed = invoked && (action == Action::ErrorDelete
        ? errors == 1 && messages == 0 && !transport
        : errors == 0 && messages == (action == Action::Keep ? 2 : 1));
    delete transport.data();
    return passed;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    for (Action action : {Action::Keep, Action::Delete, Action::NestedDelete,
                         Action::Abort, Action::ErrorDelete}) {
        if (!runCase(action)) {
            std::fprintf(stderr, "Native socket lifetime case %d failed\n", int(action));
            return 1;
        }
    }
    std::puts("NATIVE_SOCKET_LIFETIME: 5 cases passed");
    return 0;
}
