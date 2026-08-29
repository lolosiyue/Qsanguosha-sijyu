#ifndef PROTOCOL_CODEC_H
#define PROTOCOL_CODEC_H

#include "protocol-version.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

namespace QSanProtocol {

class Packet;

enum class ProtocolDecodeError
{
    None,
    NullOutput,
    EmptyInput,
    PacketTooLarge,
    InvalidJson,
    InvalidEnvelope,
    InvalidHeader,
    UnsupportedVersion
};

struct ProtocolDecodeResult
{
    bool success = false;
    ProtocolDecodeError error = ProtocolDecodeError::None;
    QString detail;
};

class IProtocolCodec
{
public:
    virtual ~IProtocolCodec() = default;

    virtual ProtocolVersion version() const = 0;
    virtual QByteArray encode(const Packet &packet, QString *error = nullptr) const = 0;
    virtual ProtocolDecodeResult decode(QByteArrayView raw, Packet *packet) const = 0;
};

}

#endif
