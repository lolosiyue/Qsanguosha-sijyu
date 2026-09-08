#include "rules-bundle-identity.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

namespace QSanRules {
QByteArray canonical(const QJsonValue &value)
{
    // Qt orders object keys; arrays deliberately retain registration order.
    QByteArray bytes = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return bytes.mid(1, bytes.size() - 2);
}

QString digest(const QString &domain, const QJsonValue &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(
        domain.toUtf8() + '\0' + canonical(value), QCryptographicHash::Sha256).toHex());
}

QJsonObject seal(QJsonObject identity)
{
    identity.remove(QStringLiteral("bundle_id"));
    identity.insert(QStringLiteral("bundle_id"), digest(QStringLiteral("qsan-rules-bundle-v1"), identity));
    return identity;
}

bool validate(const QJsonObject &identity)
{
    static const QRegularExpression hash(QStringLiteral("^[0-9a-f]{64}$"));
    if (canonical(identity).size() > 32768
        || identity.value(QStringLiteral("schema_version")) != IdentitySchema
        || identity.value(QStringLiteral("protocol_version")) != 2
        || !identity.value(QStringLiteral("bridge_schema")).isDouble()
        || identity.value(QStringLiteral("bridge_schema")).toInt(-1) <= 0
        || identity.value(QStringLiteral("bridge_schema")).toDouble()
            != identity.value(QStringLiteral("bridge_schema")).toInt()
        || identity.value(QStringLiteral("ruleset")).toString().isEmpty()
        || identity.value(QStringLiteral("content_profile")).toString().isEmpty())
        return false;
    for (const char *key : {"bundle_id", "cpp_hash", "card_registry_hash", "lua_hash", "bindings_abi"})
        if (!hash.match(identity.value(QLatin1String(key)).toString()).hasMatch())
            return false;
    const auto packages = identity.value(QStringLiteral("packages"));
    if (!packages.isArray() || packages.toArray().isEmpty() || packages.toArray().size() > 512)
        return false;
    QSet<QString> seen;
    for (const auto &entry : packages.toArray()) {
        if (!entry.isString() || entry.toString().isEmpty() || seen.contains(entry.toString()))
            return false;
        seen.insert(entry.toString());
    }
    const auto schemas = identity.value(QStringLiteral("interaction_schemas"));
    if (!schemas.isObject() || schemas.toObject().isEmpty() || schemas.toObject().size() > 256)
        return false;
    const auto object = schemas.toObject();
    for (auto it = object.begin(); it != object.end(); ++it)
        if (it.key().isEmpty() || !hash.match(it.value().toString()).hasMatch())
            return false;
    return seal(identity).value(QStringLiteral("bundle_id")) == identity.value(QStringLiteral("bundle_id"));
}

QString compatibilityError(const QJsonObject &server, const QJsonObject &client, bool required)
{
    if (client.isEmpty())
        return required ? QStringLiteral("rules_identity_required") : QString();
    if (!validate(client))
        return QStringLiteral("rules_identity_invalid");
    if (!validate(server) || server.value(QStringLiteral("content_profile")) != QLatin1String("builtin-v1")
        || client.value(QStringLiteral("content_profile")) != server.value(QStringLiteral("content_profile")))
        return QStringLiteral("rules_content_unsupported");
    if (client.value(QStringLiteral("bridge_schema")) != server.value(QStringLiteral("bridge_schema"))
        || client.value(QStringLiteral("bindings_abi")) != server.value(QStringLiteral("bindings_abi")))
        return QStringLiteral("rules_reload_required");
    const auto requiredSchemas = server.value(QStringLiteral("interaction_schemas")).toObject();
    const auto supportedSchemas = client.value(QStringLiteral("interaction_schemas")).toObject();
    for (auto it = requiredSchemas.begin(); it != requiredSchemas.end(); ++it)
        if (supportedSchemas.value(it.key()) != it.value())
            return QStringLiteral("rules_interaction_unsupported");
    if (server.value(QStringLiteral("bundle_id")) != client.value(QStringLiteral("bundle_id")))
        return QStringLiteral("rules_version_mismatch");
    return {};
}
}
