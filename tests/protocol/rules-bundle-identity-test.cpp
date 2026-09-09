#include "protocol/rules-bundle-identity.h"
#include "protocol/session/session-payloads.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

using namespace QSanProtocol;

namespace
{
int caseCount = 0;

bool expect(bool condition, const QString &label)
{
    ++caseCount;
    if (condition)
        return true;
    QTextStream(stderr) << label << " failed\n";
    return false;
}

QJsonArray cards()
{
    return {
        QJsonObject{{QStringLiteral("id"), 0}, {QStringLiteral("name"), QStringLiteral("slash")},
                    {QStringLiteral("suit"), QStringLiteral("spade")}, {QStringLiteral("number"), 7}},
        QJsonObject{{QStringLiteral("id"), 1}, {QStringLiteral("name"), QStringLiteral("jink")},
                    {QStringLiteral("suit"), QStringLiteral("heart")}, {QStringLiteral("number"), 2}}};
}

QString luaHash(const QString &source)
{
    return QSanRules::digest(QStringLiteral("lua"), QJsonObject{
        {QStringLiteral("skill"), QStringLiteral("fixture_skill")},
        {QStringLiteral("source"), source}});
}

QJsonObject identity()
{
    return QSanRules::seal(QJsonObject{
        {QStringLiteral("schema_version"), QSanRules::IdentitySchema},
        {QStringLiteral("protocol_version"), 2},
        {QStringLiteral("bridge_schema"), QSanRules::BridgeSchema},
        {QStringLiteral("ruleset"), QStringLiteral("sijyu")},
        {QStringLiteral("content_profile"), QStringLiteral("declared-v1")},
        {QStringLiteral("cpp_hash"), QSanRules::digest(QStringLiteral("cpp"), QStringLiteral("rules-v1"))},
        {QStringLiteral("card_registry_hash"), QSanRules::digest(QStringLiteral("cards"), cards())},
        {QStringLiteral("lua_hash"), luaHash(QStringLiteral("return 1"))},
        {QStringLiteral("bindings_abi"), QSanRules::digest(QStringLiteral("abi"), QStringLiteral("bindings-v1"))},
        {QStringLiteral("packages"), QJsonArray{QStringLiteral("standard"), QStringLiteral("wind")}},
        {QStringLiteral("interaction_schemas"), QJsonObject{
            {QStringLiteral("37"), QSanRules::digest(QStringLiteral("interaction"), QStringLiteral("use-card-v1"))},
            {QStringLiteral("38"), QSanRules::digest(QStringLiteral("interaction"), QStringLiteral("choose-player-v1"))}}}});
}

QJsonObject changed(const QJsonObject &source, const QString &key, const QJsonValue &value)
{
    QJsonObject result = source;
    result.insert(key, value);
    // Reseal legitimate alternative bundles so failures exercise compatibility, not tampering.
    return QSanRules::seal(result);
}

bool expectError(const QJsonObject &server, const QJsonObject &client, bool required,
                 const QString &expected, const QString &label)
{
    const QString actual = QSanRules::compatibilityError(server, client, required);
    return expect(actual == expected, label + QStringLiteral(" (actual: %1)").arg(actual));
}

QVariantMap wireRoundTrip(const QVariantMap &source)
{
    const QByteArray wire = QJsonDocument(QJsonObject::fromVariantMap(source))
        .toJson(QJsonDocument::Compact);
    return QJsonDocument::fromJson(wire).object().toVariantMap();
}

bool canonicalContract()
{
    QJsonObject left;
    left.insert(QStringLiteral("b"), 2);
    left.insert(QStringLiteral("a"), 1);
    QJsonObject right;
    right.insert(QStringLiteral("a"), 1);
    right.insert(QStringLiteral("b"), 2);
    return expect(QSanRules::canonical(left) == QByteArrayLiteral("{\"a\":1,\"b\":2}"),
                  QStringLiteral("canonical object key ordering"))
        && expect(QSanRules::digest(QStringLiteral("fixture"), left)
                      == QSanRules::digest(QStringLiteral("fixture"), right),
                  QStringLiteral("insertion order does not alter object digest"))
        && expect(QSanRules::digest(QStringLiteral("cards"), left)
                      != QSanRules::digest(QStringLiteral("lua"), left),
                  QStringLiteral("digest domains are separated"));
}

bool matchingAndMismatchingBundles()
{
    const QJsonObject server = identity();
    if (!expect(QSanRules::validate(server), QStringLiteral("sealed fixture is valid"))
        || !expect(QSanRules::seal(server) == server, QStringLiteral("sealing is idempotent"))
        || !expectError(server, server, true, {}, QStringLiteral("same bundle on required transport"))
        || !expectError(server, server, false, {}, QStringLiteral("same bundle on TCP"))
        || !expectError(server, {}, true, QStringLiteral("rules_identity_required"),
                        QStringLiteral("missing identity on required transport"))
        || !expectError(server, {}, false, {}, QStringLiteral("legacy TCP may omit identity"))) {
        return false;
    }

    const QJsonArray originalCards = cards();
    const QJsonArray reversedCards{originalCards.at(1), originalCards.at(0)};
    const QJsonObject reordered = changed(server, QStringLiteral("card_registry_hash"),
        QSanRules::digest(QStringLiteral("cards"), reversedCards));
    if (!expect(originalCards.size() == reversedCards.size(), QStringLiteral("reordered card count stays equal"))
        || !expect(QSanRules::validate(reordered), QStringLiteral("reordered registry has valid seal"))
        || !expectError(server, reordered, true, QStringLiteral("rules_version_mismatch"),
                        QStringLiteral("equal card count cannot hide registration order"))
        || !expectError(server, reordered, false, QStringLiteral("rules_version_mismatch"),
                        QStringLiteral("TCP submitted identity must also match"))
        || !expectError(server, changed(server, QStringLiteral("packages"),
                            QJsonArray{QStringLiteral("wind"), QStringLiteral("standard")}),
                        true, QStringLiteral("rules_version_mismatch"),
                        QStringLiteral("same packages in different order"))
        || !expectError(server, changed(server, QStringLiteral("lua_hash"), luaHash(QStringLiteral("return 2"))),
                        true, QStringLiteral("rules_version_mismatch"),
                        QStringLiteral("same skill name with different script"))
        || !expectError(server, changed(server, QStringLiteral("cpp_hash"),
                            QSanRules::digest(QStringLiteral("cpp"), QStringLiteral("rules-v2"))),
                        true, QStringLiteral("rules_version_mismatch"),
                        QStringLiteral("changed native rules implementation"))
        || !expectError(server, changed(server, QStringLiteral("ruleset"), QStringLiteral("other-ruleset")),
                        true, QStringLiteral("rules_version_mismatch"), QStringLiteral("different ruleset"))
        || !expectError(server, changed(server, QStringLiteral("bridge_schema"), 1),
                        true, QStringLiteral("rules_version_mismatch"), QStringLiteral("old bridge changes code identity"))
        || !expectError(server, changed(server, QStringLiteral("bindings_abi"),
                            QSanRules::digest(QStringLiteral("abi"), QStringLiteral("bindings-v0"))),
                        true, QStringLiteral("rules_version_mismatch"), QStringLiteral("old bindings change code identity"))) {
        return false;
    }

    QJsonObject supported = server.value(QStringLiteral("interaction_schemas")).toObject();
    supported.remove(QStringLiteral("38"));
    const QJsonObject missingSchema = changed(server, QStringLiteral("interaction_schemas"), supported);
    if (!expect(QSanRules::validate(missingSchema), QStringLiteral("missing-schema fixture is sealed and valid"))
        || !expectError(server, missingSchema, true, QStringLiteral("rules_interaction_unsupported"),
                        QStringLiteral("missing required interaction"))) {
        return false;
    }
    supported = server.value(QStringLiteral("interaction_schemas")).toObject();
    supported.insert(QStringLiteral("38"), QSanRules::digest(QStringLiteral("interaction"), QStringLiteral("old-shape")));
    const QJsonObject unsupportedProfile = changed(server, QStringLiteral("content_profile"), QStringLiteral("extension-v1"));
    return expectError(server, changed(server, QStringLiteral("interaction_schemas"), supported),
                       true, QStringLiteral("rules_interaction_unsupported"),
                       QStringLiteral("same interaction command with incompatible shape"))
        && expectError(server, unsupportedProfile, true, QStringLiteral("rules_content_unsupported"),
                       QStringLiteral("unknown client content profile"))
        && expectError(unsupportedProfile, unsupportedProfile, true, QStringLiteral("rules_content_unsupported"),
                       QStringLiteral("equal unsupported profiles cannot opt into extension support"));
}

bool codeIdentityIsSeparable()
{
    QJsonObject server = identity();
    QJsonObject client = identity();
    if (!expect(server.value(QStringLiteral("code_id")).toString().size() == 64,
                QStringLiteral("sealed identity carries a code_id")))
        return false;

    client.insert(QStringLiteral("lua_hash"), luaHash(QStringLiteral("other")));
    client = QSanRules::seal(client);
    if (!expect(client.value(QStringLiteral("code_id")) == server.value(QStringLiteral("code_id")),
                QStringLiteral("content change leaves code_id untouched"))
        || !expect(QSanRules::compatibilityError(server, client, true)
                       == QStringLiteral("rules_version_mismatch"),
                   QStringLiteral("content divergence reports a version mismatch")))
        return false;

    QJsonObject other = identity();
    other.insert(QStringLiteral("bindings_abi"), QString(64, QLatin1Char('a')));
    other = QSanRules::seal(other);
    return expect(other.value(QStringLiteral("code_id")) != server.value(QStringLiteral("code_id")),
                  QStringLiteral("bindings change moves code_id"))
        && expect(QSanRules::compatibilityError(server, other, true)
                      == QStringLiteral("rules_version_mismatch"),
                  QStringLiteral("code divergence is reported without content comparison"));
}

bool invalidIdentitiesRejected()
{
    const QJsonObject server = identity();
    QJsonObject tampered = server;
    tampered.insert(QStringLiteral("lua_hash"), luaHash(QStringLiteral("return 3")));
    QJsonObject tamperedCodeId = server;
    tamperedCodeId.insert(QStringLiteral("code_id"), QString(64, QLatin1Char('f')));
    QJsonObject falseBundle = server;
    falseBundle.insert(QStringLiteral("bundle_id"), QString(64, QLatin1Char('0')));
    const QJsonObject fractional = changed(server, QStringLiteral("bridge_schema"), 2.5);
    const QJsonObject malformedHash = changed(server, QStringLiteral("cpp_hash"), QStringLiteral("not-a-hash"));
    const QJsonObject duplicatePackage = changed(server, QStringLiteral("packages"),
        QJsonArray{QStringLiteral("standard"), QStringLiteral("standard")});
    const QJsonObject unsupportedProtocol = changed(server, QStringLiteral("protocol_version"), 3);
    const QJsonObject unsupportedIdentity = changed(server, QStringLiteral("schema_version"), 2);
    for (const QJsonObject &client : {tampered, tamperedCodeId, falseBundle, fractional, malformedHash,
                                     duplicatePackage, unsupportedProtocol, unsupportedIdentity}) {
        if (!expect(!QSanRules::validate(client), QStringLiteral("invalid identity fails validation"))
            || !expectError(server, client, true, QStringLiteral("rules_identity_invalid"),
                            QStringLiteral("invalid identity rejected on required transport"))
            || !expectError(server, client, false, QStringLiteral("rules_identity_invalid"),
                            QStringLiteral("invalid submitted identity rejected on TCP"))) {
            return false;
        }
    }
    return true;
}

bool sessionRoundTrips()
{
    const QJsonObject bundle = identity();
    ServerHelloPayload hello;
    hello.gameVersion = QStringLiteral("test-version");
    hello.modName = QStringLiteral("sijyu");
    hello.cardCount = cards().size();
    hello.rulesBundle = bundle;
    const QVariantMap helloWire = wireRoundTrip(hello.toVariant());
    ServerHelloPayload parsedHello;
    QString error;
    if (!expect(helloWire.value(QStringLiteral("schema_version")).toInt() == 1,
                QStringLiteral("Hello retains outer schema 1"))
        || !expect(ServerHelloPayload::parse(helloWire, &parsedHello, &error)
                       && parsedHello.rulesBundle == bundle && parsedHello.cardCount == hello.cardCount,
                   QStringLiteral("Hello metadata survives JSON round trip"))) {
        return false;
    }
    hello.rulesBundle = {};
    const QVariantMap legacyHello = wireRoundTrip(hello.toVariant());
    if (!expect(!legacyHello.contains(QStringLiteral("rules_bundle"))
                    && ServerHelloPayload::parse(legacyHello, &parsedHello, &error)
                    && parsedHello.rulesBundle.isEmpty(),
                QStringLiteral("legacy Hello absent metadata clears prior bundle"))) {
        return false;
    }

    SignupRequestPayload signup;
    signup.reconnectRequested = true;
    signup.screenName = QStringLiteral("fixture-player");
    signup.avatar = QStringLiteral("caocao");
    signup.hasRoomId = true;
    signup.roomId = 7;
    signup.hasRulesBundle = true;
    signup.rulesBundle = bundle;
    const QVariantMap signupWire = wireRoundTrip(signup.toVariant());
    SignupRequestPayload parsedSignup;
    if (!expect(signupWire.value(QStringLiteral("schema_version")).toInt() == 2,
                QStringLiteral("Signup retains outer schema 2"))
        || !expect(SignupRequestPayload::parse(signupWire, &parsedSignup, &error)
                       && parsedSignup.hasRulesBundle && parsedSignup.rulesBundle == bundle
                       && parsedSignup.reconnectRequested && parsedSignup.hasRoomId && parsedSignup.roomId == 7,
                   QStringLiteral("reconnect Signup metadata survives JSON round trip"))) {
        return false;
    }

    signup.hasRulesBundle = false;
    signup.hasRoomId = false;
    for (int schemaVersion : {1, 2}) {
        QVariantMap legacy = signup.toVariant();
        legacy.insert(QStringLiteral("schema_version"), schemaVersion);
        if (!expect(!legacy.contains(QStringLiteral("rules_bundle"))
                        && SignupRequestPayload::parse(wireRoundTrip(legacy), &parsedSignup, &error)
                        && !parsedSignup.hasRulesBundle && parsedSignup.rulesBundle.isEmpty(),
                    QStringLiteral("legacy Signup schema %1 may omit metadata").arg(schemaVersion))) {
            return false;
        }
    }

    signup.hasRulesBundle = true;
    signup.rulesBundle = {};
    const QVariantMap emptyBundle = wireRoundTrip(signup.toVariant());
    if (!expect(emptyBundle.contains(QStringLiteral("rules_bundle"))
                    && SignupRequestPayload::parse(emptyBundle, &parsedSignup, &error)
                    && parsedSignup.hasRulesBundle && parsedSignup.rulesBundle.isEmpty(),
                QStringLiteral("explicit empty metadata remains present after parse"))
        || !expectError(bundle, parsedSignup.rulesBundle, parsedSignup.hasRulesBundle,
                        QStringLiteral("rules_identity_required"),
                        QStringLiteral("explicit empty TCP identity cannot use absent-metadata exemption"))) {
        return false;
    }

    for (const QVariant &malformed : {QVariant(QStringLiteral("invalid")), QVariant(QVariantList{}), QVariant()}) {
        QVariantMap badSignup = signupWire;
        badSignup.insert(QStringLiteral("rules_bundle"), malformed);
        QVariantMap badHello = helloWire;
        badHello.insert(QStringLiteral("rules_bundle"), malformed);
        if (!expect(!SignupRequestPayload::parse(wireRoundTrip(badSignup), &parsedSignup, &error),
                    QStringLiteral("Signup rejects non-object metadata"))
            || !expect(!ServerHelloPayload::parse(wireRoundTrip(badHello), &parsedHello, &error),
                       QStringLiteral("Hello rejects non-object metadata"))) {
            return false;
        }
    }
    return true;
}
}

int main()
{
    if (!canonicalContract() || !matchingAndMismatchingBundles()
        || !codeIdentityIsSeparable() || !invalidIdentitiesRejected() || !sessionRoundTrips()) {
        return 1;
    }
    QTextStream(stdout) << "rules bundle identity: " << caseCount << " checks passed\n";
    return 0;
}
