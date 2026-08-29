#include "protocol-negotiation.h"

#include <QMetaType>
#include <QStringList>
#include <QVariantList>

using namespace QSanProtocol;

namespace
{
ProtocolCapabilities normalizedCapabilities(const ProtocolCapabilities &capabilities)
{
    ProtocolCapabilities result;
    if (capabilities.supports(ProtocolVersion::V1))
        result.supportedVersions << ProtocolVersion::V1;
    if (capabilities.supports(ProtocolVersion::V2))
        result.supportedVersions << ProtocolVersion::V2;
    return result;
}

QVariantList encodedVersions(const ProtocolCapabilities &capabilities)
{
    QVariantList versions;
    const ProtocolCapabilities normalized = normalizedCapabilities(capabilities);
    for (ProtocolVersion version : normalized.supportedVersions)
        versions << static_cast<int>(version);
    return versions;
}

ProtocolCapabilitiesParseResult malformedCapabilities(const QString &diagnostic)
{
    ProtocolCapabilitiesParseResult result;
    result.capabilities = ProtocolNegotiation::legacyCapabilities();
    result.diagnostic = diagnostic;
    return result;
}

ProtocolCapabilitiesParseResult parseVersionValues(const QVariantList &values)
{
    if (values.isEmpty())
        return malformedCapabilities(QStringLiteral("protocol_versions must not be empty"));

    ProtocolCapabilities capabilities;
    for (const QVariant &value : values) {
        bool ok = false;
        const int rawVersion = value.toInt(&ok);
        if (!ok)
            return malformedCapabilities(
                QStringLiteral("protocol_versions must contain only numbers"));

        if (rawVersion == static_cast<int>(ProtocolVersion::V1)
            && !capabilities.supports(ProtocolVersion::V1)) {
            capabilities.supportedVersions << ProtocolVersion::V1;
        } else if (rawVersion == static_cast<int>(ProtocolVersion::V2)
                   && !capabilities.supports(ProtocolVersion::V2)) {
            capabilities.supportedVersions << ProtocolVersion::V2;
        }
    }

    if (!capabilities.supports(ProtocolVersion::V1)) {
        return malformedCapabilities(
            QStringLiteral("Protocol V1 must be advertised for the V1 handshake"));
    }

    ProtocolCapabilitiesParseResult result;
    result.capabilities = normalizedCapabilities(capabilities);
    result.valid = true;
    return result;
}

ProtocolCapabilitiesParseResult parseVersionToken(const QString &token)
{
    const QString value = token.mid(QStringLiteral("protocol=").size());
    const QStringList parts = value.split(QLatin1Char(','), Qt::KeepEmptyParts);
    QVariantList versions;
    for (const QString &part : parts) {
        bool ok = false;
        const int version = part.toInt(&ok);
        if (!ok)
            return malformedCapabilities(
                QStringLiteral("protocol token must contain comma-separated numbers"));
        versions << version;
    }
    return parseVersionValues(versions);
}
}

bool ProtocolCapabilities::supports(ProtocolVersion version) const
{
    return supportedVersions.contains(version);
}

ProtocolCapabilities ProtocolNegotiation::localCapabilities()
{
    ProtocolCapabilities capabilities;
    capabilities.supportedVersions << ProtocolVersion::V1 << ProtocolVersion::V2;
    return capabilities;
}

ProtocolCapabilities ProtocolNegotiation::legacyCapabilities()
{
    ProtocolCapabilities capabilities;
    capabilities.supportedVersions << ProtocolVersion::V1;
    return capabilities;
}

ProtocolNegotiationResult ProtocolNegotiation::negotiate(
    const ProtocolCapabilities &local, const ProtocolCapabilities &peer)
{
    ProtocolNegotiationResult result;
    if (local.supports(ProtocolVersion::V2) && peer.supports(ProtocolVersion::V2)) {
        result.preferredVersion = ProtocolVersion::V2;
        result.reason = QStringLiteral(
            "Protocol V2 is mutually advertised; Protocol V1 remains active until a V2 codec is available");
    } else if (local.supports(ProtocolVersion::V1) && peer.supports(ProtocolVersion::V1)) {
        result.reason = QStringLiteral("Protocol V1 compatibility fallback");
    } else {
        result.reason = QStringLiteral(
            "No common advertised version; Protocol V1 compatibility fallback");
    }

    // This slice negotiates preference only. Production framing and codec remain V1.
    result.activeVersion = ProtocolVersion::V1;
    return result;
}

QString ProtocolNegotiation::encodeServerAdvertisement(
    const QString &gameVersion, const QString &modName,
    const ProtocolCapabilities &capabilities, int cardCount)
{
    QStringList versions;
    for (const QVariant &version : encodedVersions(capabilities))
        versions << QString::number(version.toInt());

    return QStringLiteral("%1:%2:protocol=%3:%4")
        .arg(gameVersion,
             modName.isEmpty() ? QStringLiteral("official") : modName,
             versions.join(QLatin1Char(',')),
             QString::number(cardCount));
}

ProtocolServerAdvertisement ProtocolNegotiation::parseServerAdvertisement(
    const QString &advertisement)
{
    ProtocolServerAdvertisement result;
    result.capability.capabilities = legacyCapabilities();
    result.capability.valid = true;

    const QStringList fields = advertisement.split(QLatin1Char(':'), Qt::KeepEmptyParts);
    if (fields.isEmpty())
        return result;

    result.gameVersion = fields.constFirst();
    if (fields.size() > 2)
        result.modName = fields.at(1);
    if (fields.size() > 1) {
        result.cardCount = fields.constLast().toInt();
        result.hasCardCount = true;
    }

    for (int index = 1; index + 1 < fields.size(); ++index) {
        const QString &field = fields.at(index);
        if (!field.startsWith(QStringLiteral("protocol=")))
            continue;

        if (result.capabilityAdvertised) {
            result.capability = malformedCapabilities(
                QStringLiteral("duplicate protocol capability token"));
            return result;
        }
        result.capabilityAdvertised = true;
        result.capability = parseVersionToken(field);
    }
    return result;
}

QVariantMap ProtocolNegotiation::encodeClientCapabilities(
    const ProtocolCapabilities &capabilities)
{
    QVariantMap result;
    result.insert(QStringLiteral("schema_version"), 1);
    result.insert(QStringLiteral("protocol_versions"),
                  QVariant::fromValue(encodedVersions(capabilities)));
    return result;
}

ProtocolCapabilitiesParseResult ProtocolNegotiation::parseClientCapabilities(
    const QVariant &value)
{
    if (value.userType() != QMetaType::QVariantMap) {
        return malformedCapabilities(
            QStringLiteral("protocol capability must be an object"));
    }

    const QVariantMap object = value.toMap();
    bool validSchema = false;
    const int schemaVersion = object.value(QStringLiteral("schema_version"))
                                  .toInt(&validSchema);
    if (!validSchema || schemaVersion != 1) {
        return malformedCapabilities(
            QStringLiteral("unsupported protocol capability schema_version"));
    }

    const QVariant versions = object.value(QStringLiteral("protocol_versions"));
    if (versions.userType() != QMetaType::QVariantList) {
        return malformedCapabilities(
            QStringLiteral("protocol_versions must be an array"));
    }
    return parseVersionValues(versions.toList());
}

ProtocolSessionState::ProtocolSessionState()
    : ProtocolSessionState(ProtocolNegotiation::localCapabilities())
{
}

ProtocolSessionState::ProtocolSessionState(
    const ProtocolCapabilities &localCapabilities)
    : m_localCapabilities(normalizedCapabilities(localCapabilities)),
      m_peerCapabilities(ProtocolNegotiation::legacyCapabilities()),
      m_negotiation(ProtocolNegotiation::negotiate(m_localCapabilities,
                                                   m_peerCapabilities))
{
}

const ProtocolCapabilities &ProtocolSessionState::localCapabilities() const
{
    return m_localCapabilities;
}

const ProtocolCapabilities &ProtocolSessionState::peerCapabilities() const
{
    return m_peerCapabilities;
}

QList<ProtocolVersion> ProtocolSessionState::peerSupportedVersions() const
{
    return m_peerCapabilities.supportedVersions;
}

bool ProtocolSessionState::peerSupports(ProtocolVersion version) const
{
    return m_peerCapabilities.supports(version);
}

ProtocolVersion ProtocolSessionState::preferredVersion() const
{
    return m_negotiation.preferredVersion;
}

ProtocolVersion ProtocolSessionState::activeVersion() const
{
    return m_negotiation.activeVersion;
}

QString ProtocolSessionState::reason() const
{
    return m_negotiation.reason;
}

QString ProtocolSessionState::diagnostic() const
{
    return m_diagnostic;
}

void ProtocolSessionState::setPeerCapabilities(
    const ProtocolCapabilities &capabilities, const QString &diagnostic)
{
    m_peerCapabilities = normalizedCapabilities(capabilities);
    if (m_peerCapabilities.supportedVersions.isEmpty())
        m_peerCapabilities = ProtocolNegotiation::legacyCapabilities();
    m_diagnostic = diagnostic;
    m_negotiation = ProtocolNegotiation::negotiate(m_localCapabilities,
                                                   m_peerCapabilities);
}
