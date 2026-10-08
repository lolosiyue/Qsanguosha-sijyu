#ifndef QSAN_MANAGED_REWIND_PAYLOADS_H
#define QSAN_MANAGED_REWIND_PAYLOADS_H

#include <QVariant>
#include <QStringList>
#include <QMetaType>

namespace QSanProtocol {
namespace RewindPayloadDetail {
inline bool reject(QString *error, const QString &text) { if (error) *error = text; return false; }
inline bool decimal(const QString &text, bool positive = false) {
    if (text.isEmpty() || (text.size() > 1 && text.front() == QLatin1Char('0'))) return false;
    for (QChar ch : text) if (ch < QLatin1Char('0') || ch > QLatin1Char('9')) return false;
    bool ok = false; const auto number = text.toULongLong(&ok); return ok && (!positive || number > 0);
}
inline bool object(const QVariant &value, const QStringList &keys, QVariantMap *out, QString *error,
                   const QStringList &optionalKeys = {}) {
    if (value.metaType().id() != QMetaType::QVariantMap) return reject(error, QStringLiteral("rewind payload must be an object"));
    const auto map = value.toMap();
    const auto schema = map.value(QStringLiteral("schema_version"));
    const bool numeric = schema.metaType().id() == QMetaType::Int || schema.metaType().id() == QMetaType::LongLong
        || schema.metaType().id() == QMetaType::Double;
    qsizetype expectedSize = keys.size() + 1;
    for (const auto &key : optionalKeys) if (map.contains(key)) ++expectedSize;
    if (map.size() != expectedSize || !numeric || schema.toDouble() != 1.0)
        return reject(error, QStringLiteral("invalid rewind schema or fields"));
    for (const auto &key : keys) if (!map.contains(key)) return reject(error, QStringLiteral("missing rewind field: ") + key);
    *out = map; return true;
}
inline bool string(const QVariantMap &map, const QString &key, QString *out) {
    const auto value = map.value(key);
    if (value.metaType().id() != QMetaType::QString || value.toString().size() > 256) return false;
    *out = value.toString(); return true;
}
}
struct RewindControlPayload {
    QString rootGameId, worldId, generation, token, sequence, operation;
    QVariantMap toVariant() const {
        return {{"schema_version", 1}, {"root_game_id", rootGameId}, {"world_id", worldId},
                {"generation", generation}, {"token", token}, {"sequence", sequence}, {"operation", operation}};
    }
    static bool parse(const QVariant &value, RewindControlPayload *out, QString *error = nullptr) {
        using namespace RewindPayloadDetail;
        if (!out) return reject(error, QStringLiteral("null rewind output"));
        QVariantMap map; RewindControlPayload p;
        if (!object(value, {"root_game_id", "world_id", "generation", "token", "sequence", "operation"}, &map, error)) return false;
        if (!string(map,"root_game_id",&p.rootGameId) || !string(map,"world_id",&p.worldId)
            || !string(map,"generation",&p.generation) || !string(map,"token",&p.token)
            || !string(map,"sequence",&p.sequence) || !string(map,"operation",&p.operation)
            || !decimal(p.sequence, true)
            || !QStringList{"status","resync","cancel","step","turn","round"}.contains(p.operation))
            return reject(error, QStringLiteral("invalid rewind control fields"));
        const bool bootstrap = p.operation == QLatin1String("status") && p.rootGameId.isEmpty()
            && p.worldId.isEmpty() && p.generation.isEmpty() && p.token.isEmpty();
        if (!bootstrap && (p.rootGameId.isEmpty() || p.worldId.isEmpty() || p.token.isEmpty() || !decimal(p.generation)))
            return reject(error, QStringLiteral("rewind identity and generation are required"));
        *out = p; return true;
    }
};
struct RewindStatusPayload {
    QString rootGameId, worldId, generation = QStringLiteral("0"), token;
    QString ackSequence = QStringLiteral("0"), message, profile;
    bool supported = false, authorized = false, busy = false, startAllowed = false;
    QVariantMap toVariant() const {
        QVariantMap object{{"schema_version",1}, {"root_game_id",rootGameId}, {"world_id",worldId},
                {"generation",generation}, {"token",token}, {"ack_sequence",ackSequence},
                {"message",message}, {"profile",profile}, {"supported",supported},
                {"authorized",authorized}, {"busy",busy}};
        if (startAllowed) object.insert("start_allowed", true);
        return object;
    }
    static bool parse(const QVariant &value, RewindStatusPayload *out, QString *error = nullptr) {
        using namespace RewindPayloadDetail;
        if (!out) return reject(error, QStringLiteral("null rewind status output"));
        QVariantMap map; RewindStatusPayload p;
        if (!object(value,{"root_game_id","world_id","generation","token","ack_sequence","message","profile","supported","authorized","busy"},&map,error,{"start_allowed"})) return false;
        if (!string(map,"root_game_id",&p.rootGameId) || !string(map,"world_id",&p.worldId)
            || !string(map,"generation",&p.generation) || !string(map,"token",&p.token)
            || !string(map,"ack_sequence",&p.ackSequence) || !string(map,"message",&p.message)
            || !string(map,"profile",&p.profile) || !decimal(p.generation) || !decimal(p.ackSequence))
            return reject(error,QStringLiteral("invalid rewind status fields"));
        for (const auto &key : {"supported","authorized","busy"})
            if (map.value(key).metaType().id() != QMetaType::Bool) return reject(error,QStringLiteral("invalid rewind status boolean"));
        if (map.contains("start_allowed")) {
            if (map.value("start_allowed").metaType().id() != QMetaType::Bool)
                return reject(error,QStringLiteral("invalid rewind start boolean"));
            p.startAllowed=map.value("start_allowed").toBool();
        }
        p.supported=map.value("supported").toBool(); p.authorized=map.value("authorized").toBool(); p.busy=map.value("busy").toBool();
        *out=p; return true;
    }
};
}
#endif
