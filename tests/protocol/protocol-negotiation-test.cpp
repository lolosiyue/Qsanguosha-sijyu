#include "json.h"
#include "protocol.h"
#include "protocol/protocol-negotiation.h"

#include <QTextStream>

using namespace QSanProtocol;

namespace
{
bool expect(bool condition, const QString &label)
{
    if (condition)
        return true;
    QTextStream(stderr) << label << " failed\n";
    return false;
}

ProtocolCapabilities capabilities(bool v1, bool v2)
{
    ProtocolCapabilities result;
    if (v1)
        result.supportedVersions << ProtocolVersion::V1;
    if (v2)
        result.supportedVersions << ProtocolVersion::V2;
    return result;
}

bool negotiationRules()
{
    struct Case
    {
        ProtocolCapabilities local;
        ProtocolCapabilities peer;
        ProtocolVersion preferred;
        QString label;
    };

    const QList<Case> cases = {
        {capabilities(true, false), capabilities(true, false), ProtocolVersion::V1,
         QStringLiteral("V1 and V1")},
        {capabilities(true, true), capabilities(true, false), ProtocolVersion::V1,
         QStringLiteral("V2 local and V1 peer")},
        {capabilities(true, false), capabilities(true, true), ProtocolVersion::V1,
         QStringLiteral("V1 local and V2 peer")},
        {capabilities(true, true), capabilities(true, true), ProtocolVersion::V2,
         QStringLiteral("V2 mutual preference")}
    };

    for (const Case &testCase : cases) {
        const ProtocolNegotiationResult result = ProtocolNegotiation::negotiate(
            testCase.local, testCase.peer);
        if (!expect(result.preferredVersion == testCase.preferred,
                    testCase.label + QStringLiteral(" preferred version"))
            || !expect(result.activeVersion == ProtocolVersion::V1,
                       testCase.label + QStringLiteral(" active V1"))
            || !expect(!result.reason.isEmpty(),
                       testCase.label + QStringLiteral(" reason"))) {
            return false;
        }
    }
    return true;
}

bool serverAdvertisementCompatibility()
{
    const QString encoded = ProtocolNegotiation::encodeServerAdvertisement(
        QStringLiteral("20251231"), QStringLiteral("official"),
        ProtocolNegotiation::localCapabilities(), 3919);
    if (!expect(encoded == QStringLiteral("20251231:official:protocol=1,2:3919"),
                QStringLiteral("server advertisement wire"))) {
        return false;
    }

    // Preserve the legacy parser contract: first=version, second=mod, last=card count.
    const QStringList legacyFields = encoded.split(QLatin1Char(':'));
    if (!expect(legacyFields.constFirst() == QStringLiteral("20251231"),
                QStringLiteral("legacy server version field"))
        || !expect(legacyFields.at(1) == QStringLiteral("official"),
                   QStringLiteral("legacy server mod field"))
        || !expect(legacyFields.constLast().toInt() == 3919,
                   QStringLiteral("legacy server card count field"))) {
        return false;
    }

    const ProtocolServerAdvertisement parsed =
        ProtocolNegotiation::parseServerAdvertisement(encoded);
    if (!expect(parsed.gameVersion == QStringLiteral("20251231")
                    && parsed.modName == QStringLiteral("official")
                    && parsed.hasCardCount && parsed.cardCount == 3919,
                QStringLiteral("new client server metadata"))
        || !expect(parsed.capabilityAdvertised && parsed.capability.valid,
                   QStringLiteral("new client capability token"))
        || !expect(parsed.capability.capabilities.supports(ProtocolVersion::V1)
                       && parsed.capability.capabilities.supports(ProtocolVersion::V2),
                   QStringLiteral("new client server capabilities"))) {
        return false;
    }

    const ProtocolServerAdvertisement legacyOfficial =
        ProtocolNegotiation::parseServerAdvertisement(QStringLiteral("20251231:3919"));
    const ProtocolServerAdvertisement legacyMod =
        ProtocolNegotiation::parseServerAdvertisement(
            QStringLiteral("20251231:custom:3919"));
    if (!expect(!legacyOfficial.capabilityAdvertised
                    && legacyOfficial.capability.capabilities.supports(ProtocolVersion::V1)
                    && !legacyOfficial.capability.capabilities.supports(ProtocolVersion::V2),
                QStringLiteral("legacy official server fallback"))
        || !expect(legacyOfficial.modName == QStringLiteral("official")
                       && legacyOfficial.cardCount == 3919,
                   QStringLiteral("legacy official server metadata"))
        || !expect(legacyMod.modName == QStringLiteral("custom")
                       && legacyMod.cardCount == 3919,
                   QStringLiteral("legacy mod server metadata"))) {
        return false;
    }

    const ProtocolServerAdvertisement extended =
        ProtocolNegotiation::parseServerAdvertisement(
            QStringLiteral("20251231:official:foo=bar:protocol=1,2,99:3919"));
    if (!expect(extended.capability.valid
                    && extended.capability.capabilities.supports(ProtocolVersion::V2),
                QStringLiteral("unknown server extension and future version"))) {
        return false;
    }

    const ProtocolServerAdvertisement malformed =
        ProtocolNegotiation::parseServerAdvertisement(
            QStringLiteral("20251231:official:protocol=1,x:3919"));
    const ProtocolServerAdvertisement duplicate =
        ProtocolNegotiation::parseServerAdvertisement(
            QStringLiteral("20251231:official:protocol=1:protocol=1,2:3919"));
    return expect(malformed.capabilityAdvertised && !malformed.capability.valid
                      && malformed.capability.capabilities.supports(ProtocolVersion::V1)
                      && !malformed.capability.diagnostic.isEmpty(),
                  QStringLiteral("malformed server capability fallback"))
        && expect(!duplicate.capability.valid
                      && duplicate.capability.capabilities.supports(ProtocolVersion::V1),
                  QStringLiteral("duplicate server capability fallback"));
}

bool clientAdvertisementCompatibility()
{
    JsonArray legacySignup;
    legacySignup << false << QStringLiteral("dXNlcg==") << QStringLiteral("avatar");
    const QVariantMap capabilityObject = ProtocolNegotiation::encodeClientCapabilities(
        ProtocolNegotiation::localCapabilities());

    JsonArray newSignup = legacySignup;
    newSignup << QVariant::fromValue(capabilityObject);
    if (!expect(newSignup.size() == 4,
                QStringLiteral("new signup field count"))
        || !expect(newSignup.mid(0, 3) == legacySignup,
                   QStringLiteral("legacy signup fields unchanged"))) {
        return false;
    }

    Packet signupPacket(S_SRC_CLIENT | S_TYPE_NOTIFICATION | S_DEST_ROOM,
                        S_COMMAND_SIGNUP);
    signupPacket.setMessageBody(QVariant::fromValue(newSignup));
    Packet decoded;
    if (!expect(decoded.parse(signupPacket.toJson()),
                QStringLiteral("new signup remains Protocol V1"))) {
        return false;
    }
    const JsonArray decodedBody = decoded.getMessageBody().value<JsonArray>();
    if (!expect(decodedBody.size() == 4 && decodedBody.mid(0, 3) == legacySignup,
                QStringLiteral("old server can ignore fourth signup field"))) {
        return false;
    }

    QVariantMap withUnknownKey = decodedBody.at(3).toMap();
    withUnknownKey.insert(QStringLiteral("future_key"), QStringLiteral("ignored"));
    const ProtocolCapabilitiesParseResult parsed =
        ProtocolNegotiation::parseClientCapabilities(withUnknownKey);
    if (!expect(parsed.valid
                    && parsed.capabilities.supports(ProtocolVersion::V1)
                    && parsed.capabilities.supports(ProtocolVersion::V2),
                QStringLiteral("new server parses new client capability"))) {
        return false;
    }

    QVariantMap malformedSchema = capabilityObject;
    malformedSchema.insert(QStringLiteral("schema_version"), 2);
    QVariantMap malformedVersions = capabilityObject;
    malformedVersions.insert(QStringLiteral("protocol_versions"), QStringLiteral("1,2"));
    QVariantMap missingV1 = capabilityObject;
    missingV1.insert(QStringLiteral("protocol_versions"),
                     QVariant::fromValue(QVariantList() << 2 << 99));
    const QList<QVariant> malformedCases = {
        QStringLiteral("not-an-object"),
        malformedSchema,
        malformedVersions,
        missingV1
    };
    for (const QVariant &testCase : malformedCases) {
        const ProtocolCapabilitiesParseResult fallback =
            ProtocolNegotiation::parseClientCapabilities(testCase);
        if (!expect(!fallback.valid
                        && fallback.capabilities.supports(ProtocolVersion::V1)
                        && !fallback.capabilities.supports(ProtocolVersion::V2)
                        && !fallback.diagnostic.isEmpty(),
                    QStringLiteral("malformed client capability fallback"))) {
            return false;
        }
    }
    return true;
}

bool compatibilityMatrixAndSessionIsolation()
{
    const ProtocolCapabilities legacy = ProtocolNegotiation::legacyCapabilities();
    const ProtocolCapabilities current = ProtocolNegotiation::localCapabilities();

    const ProtocolNegotiationResult oldOld = ProtocolNegotiation::negotiate(legacy, legacy);
    const ProtocolNegotiationResult oldNew = ProtocolNegotiation::negotiate(current, legacy);
    const ProtocolNegotiationResult newOld = ProtocolNegotiation::negotiate(legacy, current);
    const ProtocolNegotiationResult newNew = ProtocolNegotiation::negotiate(current, current);
    if (!expect(oldOld.preferredVersion == ProtocolVersion::V1,
                QStringLiteral("old client old server"))
        || !expect(oldNew.preferredVersion == ProtocolVersion::V1,
                   QStringLiteral("old client new server"))
        || !expect(newOld.preferredVersion == ProtocolVersion::V1,
                   QStringLiteral("new client old server"))
        || !expect(newNew.preferredVersion == ProtocolVersion::V2
                       && newNew.activeVersion == ProtocolVersion::V1,
                   QStringLiteral("new client new server"))) {
        return false;
    }

    ProtocolSessionState first;
    ProtocolSessionState second;
    first.setPeerCapabilities(current);
    if (!expect(first.peerSupports(ProtocolVersion::V2)
                    && first.preferredVersion() == ProtocolVersion::V2
                    && first.activeVersion() == ProtocolVersion::V1,
                QStringLiteral("first connection negotiated state"))
        || !expect(!second.peerSupports(ProtocolVersion::V2)
                       && second.preferredVersion() == ProtocolVersion::V1
                       && second.activeVersion() == ProtocolVersion::V1,
                   QStringLiteral("second connection remains independent"))) {
        return false;
    }

    const ProtocolCapabilitiesParseResult malformed =
        ProtocolNegotiation::parseClientCapabilities(QStringLiteral("bad"));
    second.setPeerCapabilities(malformed.capabilities, malformed.diagnostic);
    return expect(second.preferredVersion() == ProtocolVersion::V1
                      && second.activeVersion() == ProtocolVersion::V1
                      && !second.diagnostic().isEmpty(),
                  QStringLiteral("malformed session diagnostic"));
}
}

int main()
{
    return negotiationRules()
            && serverAdvertisementCompatibility()
            && clientAdvertisementCompatibility()
            && compatibilityMatrixAndSessionIsolation()
        ? 0
        : 1;
}
