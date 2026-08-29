#ifndef PROTOCOL_V2_CODEC_H
#define PROTOCOL_V2_CODEC_H

#include "protocol-codec.h"

namespace QSanProtocol {

class ProtocolV2Codec final : public IProtocolCodec
{
public:
    static constexpr qsizetype MaxPacketSize = 65535;

    ProtocolVersion version() const override;
    QByteArray encode(const ProtocolMessage &message,
                      QString *error = nullptr) const override;
    ProtocolDecodeResult decode(QByteArrayView raw,
                                ProtocolMessage *message) const override;
};

}

#endif
