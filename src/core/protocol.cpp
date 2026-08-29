#include "protocol.h"
#include "json.h"
#include "protocol/protocol-v1-codec.h"

using namespace std;
using namespace QSanProtocol;

unsigned int QSanProtocol::Packet::globalSerialSequence = 0;
const char *QSanProtocol::S_PLAYER_SELF_REFERENCE_ID = "MG_SELF";

const int QSanProtocol::S_ALL_ALIVE_PLAYERS = 0;

bool QSanProtocol::Countdown::tryParse(const QVariant &var)
{
    if (!var.canConvert<JsonArray>())
        return false;
    JsonArray val = var.value<JsonArray>();
    //compatible with old JSON representation of Countdown
    if (JsonUtils::isString(val[0])) {
        if (val[0].toString() == "MG_COUNTDOWN")
            val.removeFirst();
        else
            return false;
    }
    if (val.size() == 2) {
        if (!JsonUtils::isNumberArray(val, 0, 1)) return false;
        current = (time_t)val[0].toInt();
        max = (time_t)val[1].toInt();
        type = S_COUNTDOWN_USE_SPECIFIED;
        return true;
    } else if (val.size() == 1 && val[0].canConvert<int>()) {
        CountdownType type = (CountdownType)val[0].toInt();
        if (type != S_COUNTDOWN_NO_LIMIT && type != S_COUNTDOWN_USE_DEFAULT)
            return false;
        else
            this->type = type;
        return true;
    } else
        return false;
}

QVariant QSanProtocol::Countdown::toVariant() const
{
    JsonArray val;
    if (type == S_COUNTDOWN_NO_LIMIT || type == S_COUNTDOWN_USE_DEFAULT) {
        val << (int)type;
    } else {
        val << (int)current;
        val << (int)max;
    }
    return val;
}

QSanProtocol::Packet::Packet(int packetDescription, CommandType command)
    : globalSerial(0), localSerial(0),
    command(command),
    packetDescription(static_cast<PacketDescription>(packetDescription))
{
}

unsigned int QSanProtocol::Packet::createGlobalSerial()
{
    globalSerial = ++globalSerialSequence;
    return globalSerial;
}

bool QSanProtocol::Packet::parse(const QByteArray &raw)
{
    const ProtocolV1Codec codec;
    return codec.decode(QByteArrayView(raw), this).success;
}

QByteArray QSanProtocol::Packet::toJson() const
{
    const ProtocolV1Codec codec;
    return codec.encode(*this);
}

QString QSanProtocol::Packet::toString() const
{
    return QString::fromUtf8(toJson());
}

