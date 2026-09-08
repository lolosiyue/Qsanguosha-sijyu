#ifndef QSAN_PROTOCOL_RULES_IDENTITY_H
#define QSAN_PROTOCOL_RULES_IDENTITY_H

// Qt Core only: this contract must not link the gameplay engine into protocol tests.
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <cmath>

namespace QSanProtocol::RulesIdentity {
struct Comparison
{
    bool compatible = false;
    QString reason;
};

inline QStringList hashFields()
{
    return {QStringLiteral("rules_code_sha256"), QStringLiteral("lua_content_sha256"),
            QStringLiteral("lua_bindings_sha256"), QStringLiteral("card_registry_sha256")};
}

inline bool version(const QJsonValue &value)
{
    const double number = value.toDouble();
    return value.isDouble() && std::isfinite(number) && number >= 1
        && number <= 2147483647.0 && number == std::floor(number);
}

inline bool valid(const QJsonValue &value)
{
    if (!value.isObject()) return false;
    const QJsonObject object = value.toObject();
    QStringList fields{QStringLiteral("schema_version"), QStringLiteral("profile"),
                       QStringLiteral("protocol_major"), QStringLiteral("bridge_api"),
                       QStringLiteral("cpp_packages"), QStringLiteral("interaction_schemas")};
    fields.append(hashFields());
    if (object.size() != fields.size()) return false;
    for (const QString &key : fields) {
        if (!object.contains(key)) return false;
    }
    static const QRegularExpression name(QStringLiteral("\\A[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}\\z"));
    static const QRegularExpression hash(QStringLiteral("\\A[0-9a-f]{64}\\z"));
    if (object.value(QStringLiteral("schema_version")) != QJsonValue(1)
        || !object.value(QStringLiteral("profile")).isString()
        || !name.match(object.value(QStringLiteral("profile")).toString()).hasMatch()
        || !version(object.value(QStringLiteral("protocol_major")))
        || !version(object.value(QStringLiteral("bridge_api")))) return false;
    for (const QString &key : hashFields()) {
        if (!object.value(key).isString() || !hash.match(object.value(key).toString()).hasMatch())
            return false;
    }
    const QJsonValue packages = object.value(QStringLiteral("cpp_packages"));
    if (!packages.isArray() || packages.toArray().isEmpty() || packages.toArray().size() > 512)
        return false;
    QSet<QString> seen;
    for (const QJsonValue &item : packages.toArray()) {
        if (!item.isString() || !name.match(item.toString()).hasMatch() || seen.contains(item.toString()))
            return false;
        seen.insert(item.toString());
    }
    const QJsonValue schemas = object.value(QStringLiteral("interaction_schemas"));
    if (!schemas.isObject() || schemas.toObject().isEmpty() || schemas.toObject().size() > 128)
        return false;
    const QJsonObject entries = schemas.toObject();
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        if (!name.match(it.key()).hasMatch() || !version(it.value())) return false;
    }
    return true;
}

inline Comparison compare(const QJsonValue &local, const QJsonValue &server)
{
    const auto fail = [](const QString &reason) { return Comparison{false, reason}; };
    if (!valid(local)) return fail(QStringLiteral("rules_identity_invalid_local"));
    const QJsonObject left = local.toObject();
    if (left.value(QStringLiteral("protocol_major")) != QJsonValue(2)
        || left.value(QStringLiteral("bridge_api")) != QJsonValue(1))
        return fail(QStringLiteral("rules_identity_unsupported_local_contract"));
    if (server.isUndefined() || server.isNull())
        return fail(QStringLiteral("rules_identity_missing_server"));
    if (!valid(server)) return fail(QStringLiteral("rules_identity_invalid_server"));
    const QJsonObject right = server.toObject();
    QStringList fields{QStringLiteral("profile"), QStringLiteral("protocol_major"),
                       QStringLiteral("bridge_api")};
    fields.append(hashFields());
    fields.append(QStringLiteral("cpp_packages"));
    fields.append(QStringLiteral("interaction_schemas"));
    for (const QString &field : fields) {
        if (left.value(field) != right.value(field))
            return fail(QStringLiteral("rules_identity_mismatch:") + field);
    }
    return {true, QString()};
}
} // namespace QSanProtocol::RulesIdentity
#endif
