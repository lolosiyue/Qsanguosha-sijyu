#ifndef PROTOCOL_CODEC_H
#define PROTOCOL_CODEC_H

#include "protocol-message.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

namespace QSanProtocol {

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
    virtual QByteArray encode(const ProtocolMessage &message,
                              QString *error = nullptr) const = 0;
    virtual ProtocolDecodeResult decode(QByteArrayView raw,
                                        ProtocolMessage *message) const = 0;
};

}

#endif
