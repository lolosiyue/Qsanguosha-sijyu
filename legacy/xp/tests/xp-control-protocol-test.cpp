#include "xp-control-protocol.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QScopedPointer>
#include <QTextStream>
#include <QUuid>
#include <QtEndian>

#include <functional>
#include <limits>

namespace {

QElapsedTimer deadline;

bool expect(bool condition, const QString &description)
{
    if (!condition)
        QTextStream(stderr) << "FAIL: " << description << '\n';
    return condition;
}

bool waitUntil(const std::function<bool()> &condition)
{
    QElapsedTimer attempt;
    attempt.start();
    // One overall deadline also bounds the failure path when several peers stall.
    while (!condition() && attempt.elapsed() < 500 && deadline.elapsed() < 8000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    return condition();
}

QByteArray header(quint32 size)
{
    QByteArray bytes(4, '\0');
    qToBigEndian<quint32>(size, reinterpret_cast<uchar *>(bytes.data()));
    return bytes;
}

QByteArray frame(const QByteArray &payload)
{
    return header(quint32(payload.size())) + payload;
}

QByteArray frame(const QJsonObject &object)
{
    return frame(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QJsonObject sizedObject(int payloadSize)
{
    QJsonObject object{{"data", QString()}};
    const int overhead = QJsonDocument(object).toJson(QJsonDocument::Compact).size();
    object["data"] = QString(payloadSize - overhead, QLatin1Char('x'));
    return object;
}

class LocalPair
{
public:
    bool open()
    {
        const QString name = QStringLiteral("qsan-xp-control-test-")
            + QUuid::createUuid().toString().remove('{').remove('}');
        if (!expect(server.listen(name), QStringLiteral("local server listen: ") + server.errorString()))
            return false;
        client.connectToServer(name);
        if (!expect(waitUntil([this] {
                return server.hasPendingConnections() && client.state() == QLocalSocket::ConnectedState;
            }), QStringLiteral("local socket connection")))
            return false;
        peer.reset(server.nextPendingConnection());
        if (!expect(!peer.isNull(), QStringLiteral("accepted socket")))
            return false;
        channel.reset(new XpControl::Channel(peer.data()));
        QObject::connect(channel.data(), &XpControl::Channel::message,
                         [this](const QJsonObject &message) { messages.append(message); });
        QObject::connect(channel.data(), &XpControl::Channel::failed,
                         [this](const QString &code) { failures.append(code); });
        QObject::connect(peer.data(), &QLocalSocket::readyRead, [this] { ++readEvents; });
        return true;
    }

    bool write(const QByteArray &bytes)
    {
        const int before = readEvents;
        if (!expect(client.write(bytes) == bytes.size(), QStringLiteral("raw socket write")))
            return false;
        client.flush();
        // Wait for actual delivery, so partial-frame assertions cannot pass before I/O.
        return expect(waitUntil([this, before] { return readEvents > before; }),
                      QStringLiteral("raw bytes delivered to decoder"));
    }

    QLocalServer server;
    QLocalSocket client;
    QList<QJsonObject> messages;
    QStringList failures;
    int readEvents = 0;
    QScopedPointer<QLocalSocket> peer;
    QScopedPointer<XpControl::Channel> channel;
};

bool integerBoundaries()
{
    const int minimum = (std::numeric_limits<int>::min)();
    const int maximum = (std::numeric_limits<int>::max)();
    for (int value : {minimum, -1, 0, 1, maximum}) {
        int parsed = 19;
        if (!expect(XpControl::integer(QJsonValue(double(value)), minimum, maximum, &parsed)
                        && parsed == value,
                    QStringLiteral("accept exact int boundary %1").arg(value)))
            return false;
    }
    const QList<QJsonValue> rejected = {
        QJsonValue(double(minimum) - 1), QJsonValue(double(maximum) + 1),
        QJsonValue(0.5), QJsonValue(-0.5), QJsonValue(1e100),
        QJsonValue(std::numeric_limits<double>::infinity()),
        QJsonValue(-std::numeric_limits<double>::infinity()),
        QJsonValue(std::numeric_limits<double>::quiet_NaN()),
        QJsonValue(QStringLiteral("1")), QJsonValue(true), QJsonValue(QJsonValue::Null),
        QJsonValue(QJsonValue::Undefined), QJsonValue(QJsonArray()), QJsonValue(QJsonObject())
    };
    for (int index = 0; index < rejected.size(); ++index) {
        int parsed = 19;
        if (!expect(!XpControl::integer(rejected.at(index), minimum, maximum, &parsed) && parsed == 19,
                    QStringLiteral("reject invalid integer without changing output %1").arg(index)))
            return false;
    }
    return expect(XpControl::integer(QJsonValue(2), 2, 10)
                      && XpControl::integer(QJsonValue(10), 2, 10)
                      && !XpControl::integer(QJsonValue(1), 2, 10)
                      && !XpControl::integer(QJsonValue(11), 2, 10),
                  QStringLiteral("enforce supplied inclusive bounds"));
}

bool decimalBoundaries()
{
    const quint64 values[] = {0, 1, Q_UINT64_C(9007199254740993), (std::numeric_limits<quint64>::max)()};
    for (quint64 value : values) {
        quint64 parsed = 19;
        if (!expect(XpControl::decimal(QString::number(value), &parsed) && parsed == value,
                    QStringLiteral("accept canonical uint64 %1").arg(value)))
            return false;
    }
    const QStringList rejected = {
        QString(), QStringLiteral("00"), QStringLiteral("01"), QStringLiteral("+1"),
        QStringLiteral("-1"), QStringLiteral(" 1"), QStringLiteral("1 "), QStringLiteral("1\n"),
        QStringLiteral("1.0"), QStringLiteral("1e2"), QStringLiteral("0x10"),
        QStringLiteral("18446744073709551616"), QStringLiteral("99999999999999999999"),
        QStringLiteral("100000000000000000000"), QString(QChar(0x0661)),
        QStringLiteral("1") + QChar(0) + QStringLiteral("2")
    };
    for (int index = 0; index < rejected.size(); ++index) {
        quint64 parsed = 19;
        if (!expect(!XpControl::decimal(rejected.at(index), &parsed) && parsed == 19,
                    QStringLiteral("reject noncanonical uint64 without changing output %1").arg(index)))
            return false;
    }
    return expect(XpControl::decimal(QStringLiteral("18446744073709551615")),
                  QStringLiteral("uint64 validation without output pointer"));
}

bool envelopeValidation()
{
    const QString session = QStringLiteral("test-session");
    const QString generation = QStringLiteral("9007199254740993");
    const QJsonObject valid = XpControl::envelope(session, generation,
        QStringLiteral("18446744073709551615"), QStringLiteral("status"), {{"probe", true}});
    if (!expect(XpControl::validEnvelope(valid, session, generation)
                    && valid.value("body").toObject().value("probe").toBool(),
                QStringLiteral("valid envelope preserves full-width string identifiers")))
        return false;
    const QList<QPair<QString, QJsonValue>> invalidFields = {
        {"version", XpControl::Version + 1}, {"version", XpControl::Version - 1},
        {"version", XpControl::Version + 0.5}, {"version", QStringLiteral("1")},
        {"session", QStringLiteral("stale-session")}, {"session", 1},
        {"generation", QStringLiteral("9007199254740994")}, {"generation", 1},
        {"generation", QStringLiteral("01")}, {"id", QStringLiteral("18446744073709551616")},
        {"id", QStringLiteral("01")}, {"id", QString()}, {"id", 1},
        {"type", 1}, {"body", QJsonArray()}
    };
    for (int index = 0; index < invalidFields.size(); ++index) {
        QJsonObject message = valid;
        message[invalidFields.at(index).first] = invalidFields.at(index).second;
        if (!expect(!XpControl::validEnvelope(message, session, generation),
                    QStringLiteral("reject invalid envelope field %1").arg(index)))
            return false;
    }
    for (const QString &field : valid.keys()) {
        QJsonObject message = valid;
        message.remove(field);
        if (!expect(!XpControl::validEnvelope(message, session, generation),
                    QStringLiteral("reject missing envelope field ") + field))
            return false;
    }
    return expect(!XpControl::validEnvelope(valid, QStringLiteral("other"), generation)
                      && !XpControl::validEnvelope(valid, session, QStringLiteral("1"))
                      && !XpControl::validEnvelope(XpControl::envelope({}, "1", "0", "status"), {}, "1")
                      && !XpControl::validEnvelope(XpControl::envelope(session, "01", "0", "status"), session, "01")
                      && XpControl::validEnvelope(XpControl::envelope(session, "0", "0", "status"), session, "0"),
                  QStringLiteral("validate expected session and canonical generation"));
}

bool partialAndCoalescedFrames()
{
    LocalPair pair;
    if (!pair.open()) return false;
    const QJsonObject first{{"sequence", 1}, {"text", QStringLiteral("partial frame")}};
    const QJsonObject second{{"sequence", 2}};
    const QJsonObject third{{"sequence", 3}};
    const QByteArray bytes = frame(first);
    if (!pair.write(bytes.left(1)) || !expect(pair.messages.isEmpty(), "one header byte is incomplete")
        || !pair.write(bytes.mid(1, 2)) || !expect(pair.messages.isEmpty(), "three header bytes are incomplete")
        || !pair.write(bytes.mid(3, 6)) || !expect(pair.messages.isEmpty(), "partial payload is incomplete")
        || !pair.write(bytes.mid(9)))
        return false;
    if (!expect(pair.messages == QList<QJsonObject>({first}), "partial frame emitted exactly once"))
        return false;
    const QByteArray tail = frame(third);
    if (!pair.write(frame(second) + frame(first) + tail.left(6))
        || !expect(pair.messages == QList<QJsonObject>({first, second, first}), "coalesced frames retain order")
        || !pair.write(tail.mid(6)))
        return false;
    return expect(pair.messages == QList<QJsonObject>({first, second, first, third}) && pair.failures.isEmpty(),
                  "coalesced trailing partial frame is retained without duplicates");
}

bool inputRejection(const QByteArray &bytes, const QString &code)
{
    LocalPair pair;
    if (!pair.open() || !pair.write(bytes)) return false;
    if (!expect(pair.messages.isEmpty() && pair.failures == QStringList({code})
                    && pair.peer->state() == QLocalSocket::UnconnectedState,
                QStringLiteral("reject input and abort: ") + code))
        return false;
    return expect(!pair.channel->send(QJsonObject()) && pair.failures == QStringList({code}),
                  "terminal failure emits once and rejects later sends");
}

bool frameLimitsAndOutput()
{
    {
        LocalPair pair;
        if (!pair.open()) return false;
        const QJsonObject object = sizedObject(XpControl::MaximumFrame);
        if (!pair.write(frame(object))
            || !expect(waitUntil([&pair] { return pair.messages.size() == 1; })
                           && pair.messages.first() == object && pair.failures.isEmpty(),
                       "maximum input frame accepted"))
            return false;
        if (!expect(pair.channel->send(object), "maximum output frame accepted")) return false;
        QByteArray received;
        if (!expect(waitUntil([&pair, &received] {
                received.append(pair.client.readAll());
                return received.size() >= XpControl::MaximumFrame + 4;
            }) && received == frame(object), "output uses exact big-endian length and JSON payload"))
            return false;
    }
    {
        LocalPair pair;
        if (!pair.open()) return false;
        if (!expect(!pair.channel->send(sizedObject(XpControl::MaximumFrame + 1))
                        && pair.failures == QStringList({QStringLiteral("output_limit")})
                        && pair.peer->state() == QLocalSocket::UnconnectedState,
                    "oversized output is rejected and connection aborted"))
            return false;
    }
    {
        LocalPair pair;
        if (!pair.open()) return false;
        const QJsonObject object = sizedObject(XpControl::MaximumFrame);
        bool rejected = false;
        // Do not pump the peer's event loop: a stalled reader must have bounded output.
        for (int index = 0; index < 64 && !rejected; ++index)
            rejected = !pair.channel->send(object);
        if (!expect(rejected && pair.failures == QStringList({QStringLiteral("output_limit")})
                        && pair.peer->state() == QLocalSocket::UnconnectedState,
                    "stalled receiver reaches output queue limit"))
            return false;
    }
    return true;
}

bool randomIdentityValidation()
{
    QString error;
    const QString first = XpControl::randomIdentity(&error);
#ifdef Q_OS_WIN
    const QString second = XpControl::randomIdentity(&error);
    if (!expect(first.size() == 64 && second.size() == 64 && first != second && error.isEmpty(),
                "Windows secure identities are distinct 256-bit strings"))
        return false;
    for (const QChar ch : first + second)
        if (!expect((ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
                        || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')),
                    "identity has canonical lower-case hex encoding"))
            return false;
#else
    if (!expect(first.isEmpty() && error == QStringLiteral("secure_random_unavailable"),
                "unsupported platform fails closed for secure identity"))
        return false;
#endif
    return true;
}

}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    deadline.start();
    if (!integerBoundaries() || !decimalBoundaries() || !envelopeValidation()
        || !partialAndCoalescedFrames()
        || !inputRejection(header(0), QStringLiteral("frame_size"))
        || !inputRejection(header(XpControl::MaximumFrame + 1), QStringLiteral("frame_size"))
        || !inputRejection(header((std::numeric_limits<quint32>::max)()), QStringLiteral("frame_size"))
        || !inputRejection(frame(QByteArray("{broken}")) + frame(QJsonObject{{"later", true}}),
                           QStringLiteral("invalid_json"))
        || !inputRejection(frame(QByteArray("[]")), QStringLiteral("invalid_json"))
        || !frameLimitsAndOutput() || !randomIdentityValidation()
        || !expect(deadline.elapsed() < 8000, "protocol tests finish before overall deadline"))
        return 1;
    QTextStream(stdout) << "PASS: XP control protocol numeric, envelope, framing and identity contracts ("
                        << deadline.elapsed() << " ms)\n";
    return 0;
}
