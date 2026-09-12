// Explicit focused executable; never registered with local CTest.
#include "../../src/excel/excel-ipc-server.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QTimer>

#ifdef QSANGUOSHA_EXCEL_IPC_TEST
namespace {
QByteArray roundTrip(ExcelIpcServer &server, const QByteArray &request)
{
    QTcpSocket socket;
    QEventLoop loop;
    QTimer deadline;
    QByteArray response;
    deadline.setSingleShot(true);
    QObject::connect(&socket, &QTcpSocket::connected, &loop, [&]() { socket.write(request); });
    QObject::connect(&socket, &QTcpSocket::readyRead, &loop, [&]() { response += socket.readAll(); });
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    socket.connectToHost(QHostAddress::LocalHost, server.port());
    deadline.start(1500);
    // The shared loop must dispatch the in-process server while awaiting reply.
    loop.exec();
    response += socket.readAll();
    return response;
}
QByteArray post(const QByteArray &body, const QByteArray &extra = QByteArray(),
                const QByteArray &auth = QByteArray("Bearer token"))
{
    return "POST /v1/commands HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: " + auth
        + "\r\nX-QSan-Session: session\r\nContent-Length: " + QByteArray::number(body.size())
        + "\r\n" + extra + "\r\n" + body;
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ExcelIpcServer server;
    int handled = 0;
    server.setHandler([&](const QString &, const QString &, const QUrlQuery &, const QJsonObject &body) {
        ++handled;
        ExcelIpcServer::Reply reply;
        reply.body = body;
        return reply;
    });
    QString error;
    if (!server.listen(QStringLiteral("session"), QStringLiteral("token"), &error)) return 1;
    const QJsonObject input{{QStringLiteral("text"), QStringLiteral("繁體中文測試")},
        {QStringLiteral("sequence"), QStringLiteral("18446744073709551615")}};
    QByteArray response = roundTrip(server, post(QJsonDocument(input).toJson(QJsonDocument::Compact)));
    if (!response.startsWith("HTTP/1.1 200") || handled != 1) return 2;
    const int bodyStart = response.indexOf("\r\n\r\n") + 4;
    if (QJsonDocument::fromJson(response.mid(bodyStart)).object() != input) return 3;
    if (!roundTrip(server, post("{}", "Transfer-Encoding: chunked\r\n")).startsWith("HTTP/1.1 400")) return 4;
    if (!roundTrip(server, post("{}", "Content-Length: 2\r\n")).startsWith("HTTP/1.1 400")) return 5;
    if (!roundTrip(server, post("{}", "Authorization:\r\n")).startsWith("HTTP/1.1 401")) return 6;
    if (!roundTrip(server, post("{}", QByteArray(), "Bearer wrong")).startsWith("HTTP/1.1 401")) return 7;
    if (!roundTrip(server, post("[]")).startsWith("HTTP/1.1 400")) return 8;
    if (handled != 1) return 9;
    bool drained = false;
    QObject::connect(&server, &ExcelIpcServer::drained, &app, [&]() { drained = true; });
    server.setHandler([&](const QString &, const QString &, const QUrlQuery &, const QJsonObject &) {
        QTimer::singleShot(0, &server, [&]() { server.drain(); });
        ExcelIpcServer::Reply reply;
        reply.body.insert(QStringLiteral("ok"), true);
        return reply;
    });
    response = roundTrip(server, post("{}"));
    QCoreApplication::processEvents();
    if (!response.startsWith("HTTP/1.1 200") || !response.endsWith("{\"ok\":true}")) return 10;
    if (!drained || server.port() != 0) return 11;
    return 0;
}
#endif
