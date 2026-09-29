#ifndef PROTOCOL_V2_CODEC_H
#define PROTOCOL_V2_CODEC_H

#include "protocol-codec.h"

namespace QSanProtocol {

class ProtocolV2Codec final : public IProtocolCodec
{
public:
    // The content manifest carried in ServerHello grows with every declared
    // Lua asset, so the old 65535-byte cap broke on large content sets
    // (Protocol V2 packet exceeds 65535 bytes). 4 MiB matches the local
    // framing limit already used by solo-server-host.cpp for the same data.
    static constexpr qsizetype MaxPacketSize = 4 * 1024 * 1024;

    ProtocolVersion version() const override;
    QByteArray encode(const ProtocolMessage &message,
                      QString *error = nullptr) const override;
    ProtocolDecodeResult decode(QByteArrayView raw,
                                ProtocolMessage *message) const override;
};

}

#endif
