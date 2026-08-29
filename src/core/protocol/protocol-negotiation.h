#ifndef PROTOCOL_NEGOTIATION_H
#define PROTOCOL_NEGOTIATION_H

#include "protocol-version.h"

#include <QList>
#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace QSanProtocol {

struct ProtocolCapabilities
{
    QList<ProtocolVersion> supportedVersions;

    bool supports(ProtocolVersion version) const;
};

struct ProtocolCapabilitiesParseResult
{
    ProtocolCapabilities capabilities;
    bool valid = false;
    QString diagnostic;
};

struct ProtocolNegotiationResult
{
    ProtocolVersion preferredVersion = ProtocolVersion::V1;
    ProtocolVersion activeVersion = ProtocolVersion::V1;
    QString reason;
};

struct ProtocolServerAdvertisement
{
    QString gameVersion;
    QString modName = QStringLiteral("official");
    int cardCount = 0;
    bool hasCardCount = false;
    bool capabilityAdvertised = false;
    ProtocolCapabilitiesParseResult capability;
};

class ProtocolNegotiation
{
public:
    static ProtocolCapabilities localCapabilities();
    static ProtocolCapabilities legacyCapabilities();
    static ProtocolNegotiationResult negotiate(const ProtocolCapabilities &local,
                                                const ProtocolCapabilities &peer);

    static QString encodeServerAdvertisement(const QString &gameVersion,
                                             const QString &modName,
                                             const ProtocolCapabilities &capabilities,
                                             int cardCount);
    static ProtocolServerAdvertisement parseServerAdvertisement(
        const QString &advertisement);

    static QVariantMap encodeClientCapabilities(
        const ProtocolCapabilities &capabilities);
    static ProtocolCapabilitiesParseResult parseClientCapabilities(
        const QVariant &value);
};

class ProtocolSessionState
{
public:
    ProtocolSessionState();
    explicit ProtocolSessionState(const ProtocolCapabilities &localCapabilities);

    const ProtocolCapabilities &localCapabilities() const;
    const ProtocolCapabilities &peerCapabilities() const;
    QList<ProtocolVersion> peerSupportedVersions() const;
    bool peerSupports(ProtocolVersion version) const;

    ProtocolVersion preferredVersion() const;
    ProtocolVersion activeVersion() const;
    QString reason() const;
    QString diagnostic() const;

    void setPeerCapabilities(const ProtocolCapabilities &capabilities,
                             const QString &diagnostic = QString());

private:
    ProtocolCapabilities m_localCapabilities;
    ProtocolCapabilities m_peerCapabilities;
    ProtocolNegotiationResult m_negotiation;
    QString m_diagnostic;
};

}

#endif
