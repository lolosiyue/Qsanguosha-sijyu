#include "protocol-payload-registry.h"

#include "card-provenance-message.h"
#include "protocol.h"
#include "protocol-message-utils.h"
#include "session/session-payloads.h"
#include "skill-instance-message.h"
#include "state/player-ui-state.h"
#include "switch-context-message.h"
#include "sync-pile-message.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>
#include <QMetaType>
#include <QSet>

#include <algorithm>

using namespace QSanProtocol;

namespace
{
bool fail(QString *error, const QString &detail)
{
    if (error != nullptr)
        *error = detail;
    return false;
}

template <typename Payload>
bool validateTypedPayload(const QVariant &value, QString *error)
{
    Payload payload;
    return Payload::parse(value, &payload, error);
}

enum class FieldShape
{
    String,
    Integer,
    Boolean,
    StringList,
    IntegerList,
    Object,
    ObjectList
};

bool validateList(const QVariant &value, FieldShape elementShape)
{
    if (elementShape == FieldShape::String && value.userType() == QMetaType::QStringList)
        return true;
    if (value.userType() != QMetaType::QVariantList)
        return false;
    for (const QVariant &entry : value.toList()) {
        if (elementShape == FieldShape::String) {
            QString parsed;
            if (!ProtocolMessageUtils::tryParseString(entry, parsed))
                return false;
        } else if (elementShape == FieldShape::Integer) {
            int parsed = 0;
            if (!ProtocolMessageUtils::tryParseInt(entry, parsed))
                return false;
        } else if (elementShape == FieldShape::Object
                   && entry.userType() != QMetaType::QVariantMap) {
            return false;
        }
    }
    return true;
}

bool validateField(const QVariantMap &object, const QString &field,
                   FieldShape shape, const QString &schema, QString *error)
{
    if (!object.contains(field))
        return true;
    const QVariant value = object.value(field);
    bool valid = false;
    switch (shape) {
    case FieldShape::String: {
        QString parsed;
        valid = ProtocolMessageUtils::tryParseString(value, parsed);
        break;
    }
    case FieldShape::Integer: {
        int parsed = 0;
        valid = ProtocolMessageUtils::tryParseInt(value, parsed);
        break;
    }
    case FieldShape::Boolean: {
        bool parsed = false;
        valid = ProtocolMessageUtils::tryParseBool(value, parsed);
        break;
    }
    case FieldShape::StringList:
        valid = validateList(value, FieldShape::String);
        break;
    case FieldShape::IntegerList:
        valid = validateList(value, FieldShape::Integer);
        break;
    case FieldShape::Object:
        valid = value.userType() == QMetaType::QVariantMap;
        break;
    case FieldShape::ObjectList:
        valid = validateList(value, FieldShape::Object);
        break;
    }
    return valid || fail(error, QStringLiteral("%1 field %2 has the wrong type")
        .arg(schema, field));
}

bool isKnownCardMoveReason(int reason)
{
    static const QSet<int> reasons {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
        0x11, 0x12, 0x13, 0x17, 0x18, 0x19, 0x1A,
        0x23, 0x27, 0x28, 0x29, 0x2A,
        0x33, 0x37, 0x38, 0x39, 0x3A,
        0x47, 0x48, 0x4A, 0x57, 0x5A, 0x67, 0x68, 0x6A
    };
    return reasons.contains(reason);
}

bool validateCardMovementPayload(const QVariantMap &object, QString *error)
{
    const QString schema = QStringLiteral("CardMovementPayload");
    const QVariantList moves = object.value(QStringLiteral("moves")).toList();
    for (int index = 0; index < moves.size(); ++index) {
        const QVariantMap move = moves.at(index).toMap();
        const auto requireMove = [&](const QString &field, FieldShape shape) {
            if (!move.contains(field)) {
                return fail(error, QStringLiteral("%1 move %2 requires field %3")
                    .arg(schema).arg(index).arg(field));
            }
            return validateField(move, field, shape,
                                 QStringLiteral("%1 move %2").arg(schema).arg(index),
                                 error);
        };
        if (!requireMove(QStringLiteral("card_ids"), FieldShape::IntegerList)
            || !requireMove(QStringLiteral("from_place"), FieldShape::Integer)
            || !requireMove(QStringLiteral("to_place"), FieldShape::Integer)
            || !requireMove(QStringLiteral("from_player"), FieldShape::String)
            || !requireMove(QStringLiteral("to_player"), FieldShape::String)
            || !requireMove(QStringLiteral("from_pile"), FieldShape::String)
            || !requireMove(QStringLiteral("to_pile"), FieldShape::String)
            || !requireMove(QStringLiteral("reason"), FieldShape::Object)
            || !requireMove(QStringLiteral("open"), FieldShape::Boolean)) {
            return false;
        }

        int fromPlace = -1;
        int toPlace = -1;
        ProtocolMessageUtils::tryParseInt(move.value(QStringLiteral("from_place")), fromPlace);
        ProtocolMessageUtils::tryParseInt(move.value(QStringLiteral("to_place")), toPlace);
        constexpr int firstPlace = 0;
        constexpr int lastPlace = 9;
        if (fromPlace < firstPlace || fromPlace > lastPlace
            || toPlace < firstPlace || toPlace > lastPlace) {
            return fail(error, QStringLiteral("%1 move %2 has an unknown card place")
                .arg(schema).arg(index));
        }

        const QVariantMap reason = move.value(QStringLiteral("reason")).toMap();
        const auto requireReason = [&](const QString &field, FieldShape shape) {
            if (!reason.contains(field)) {
                return fail(error, QStringLiteral("%1 move %2 reason requires field %3")
                    .arg(schema).arg(index).arg(field));
            }
            return validateField(reason, field, shape,
                                 QStringLiteral("%1 move %2 reason").arg(schema).arg(index),
                                 error);
        };
        if (!requireReason(QStringLiteral("reason"), FieldShape::Integer)
            || !requireReason(QStringLiteral("player_id"), FieldShape::String)
            || !requireReason(QStringLiteral("skill_name"), FieldShape::String)
            || !requireReason(QStringLiteral("event_name"), FieldShape::String)
            || !requireReason(QStringLiteral("target_id"), FieldShape::String)) {
            return false;
        }
        int reasonValue = -1;
        ProtocolMessageUtils::tryParseInt(reason.value(QStringLiteral("reason")), reasonValue);
        if (!isKnownCardMoveReason(reasonValue)) {
            return fail(error, QStringLiteral("%1 move %2 has an unknown reason")
                .arg(schema).arg(index));
        }
    }
    return true;
}

bool validateGameEventPayload(const QVariantMap &object, QString *error)
{
    const QString schema = QStringLiteral("GameEventPayload");
    int event = -1;
    if (!ProtocolMessageUtils::tryParseInt(
            object.value(QStringLiteral("event")), event)
        || event < S_GAME_EVENT_PLAYER_DYING
        || event > S_GAME_EVENT_SHOW_GENERAL) {
        return fail(error, QStringLiteral("GameEventPayload event is unknown"));
    }
    const auto require = [&](const QString &field, FieldShape shape) {
        if (!object.contains(field))
            return fail(error, QStringLiteral("%1 requires field %2").arg(schema, field));
        return validateField(object, field, shape, schema, error);
    };

    switch (event) {
    case S_GAME_EVENT_PLAYER_DYING:
    case S_GAME_EVENT_PLAYER_QUITDYING:
    case S_GAME_EVENT_PLAYER_REFORM:
        return require(QStringLiteral("player_name"), FieldShape::String);
    case S_GAME_EVENT_HUASHEN:
        return require(QStringLiteral("player_name"), FieldShape::String)
            && require(QStringLiteral("general_name"), FieldShape::String)
            && require(QStringLiteral("skill_name"), FieldShape::String);
    case S_GAME_EVENT_PLAY_EFFECT:
        return require(QStringLiteral("skill_name"), FieldShape::String)
            && require(QStringLiteral("category"), FieldShape::String)
            && require(QStringLiteral("audio_type"), FieldShape::Integer)
            && require(QStringLiteral("player_name"), FieldShape::String);
    case S_GAME_EVENT_JUDGE_RESULT:
        return require(QStringLiteral("card_id"), FieldShape::Integer)
            && require(QStringLiteral("take_effect"), FieldShape::Boolean);
    case S_GAME_EVENT_DETACH_SKILL:
        return require(QStringLiteral("player_name"), FieldShape::String)
            && require(QStringLiteral("skill_name"), FieldShape::String)
            && validateField(object, QStringLiteral("translation_suffix"),
                             FieldShape::String, schema, error);
    case S_GAME_EVENT_ACQUIRE_SKILL:
    case S_GAME_EVENT_ADD_SKILL:
    case S_GAME_EVENT_LOSE_SKILL:
    case S_GAME_EVENT_SKILL_INVOKED:
        return require(QStringLiteral("player_name"), FieldShape::String)
            && require(QStringLiteral("skill_name"), FieldShape::String);
    case S_GAME_EVENT_CHANGE_GENDER: {
        if (!require(QStringLiteral("player_name"), FieldShape::String)
            || !require(QStringLiteral("gender"), FieldShape::Integer)) {
            return false;
        }
        int gender = -1;
        ProtocolMessageUtils::tryParseInt(object.value(QStringLiteral("gender")), gender);
        return (gender >= 0 && gender <= 3)
            || fail(error, QStringLiteral("GameEventPayload gender is unknown"));
    }
    case S_GAME_EVENT_CHANGE_HERO:
        return require(QStringLiteral("player_name"), FieldShape::String)
            && require(QStringLiteral("general_name"), FieldShape::String)
            && require(QStringLiteral("secondary"), FieldShape::Boolean)
            && require(QStringLiteral("send_log"), FieldShape::Boolean);
    case S_GAME_EVENT_PAUSE:
        return require(QStringLiteral("paused"), FieldShape::Boolean);
    case S_GAME_EVENT_REVEAL_PINDIAN:
        return require(QStringLiteral("from_player"), FieldShape::String)
            && require(QStringLiteral("from_card_id"), FieldShape::Integer)
            && require(QStringLiteral("to_player"), FieldShape::String)
            && require(QStringLiteral("to_card_id"), FieldShape::Integer)
            && require(QStringLiteral("success"), FieldShape::Boolean)
            && require(QStringLiteral("reason"), FieldShape::String);
    case S_GAME_EVENT_CHANGE_BGM:
        return require(QStringLiteral("path"), FieldShape::String)
            && require(QStringLiteral("stop_current"), FieldShape::Boolean);
    case S_GAME_EVENT_AVATAR_ICON:
        return require(QStringLiteral("player_name"), FieldShape::String)
            && require(QStringLiteral("secondary"), FieldShape::Boolean);
    case S_GAME_EVENT_SORT_HAND:
        return require(QStringLiteral("player_name"), FieldShape::String)
            && require(QStringLiteral("card_ids"), FieldShape::IntegerList);
    case S_GAME_EVENT_PREPARE_SKILL:
    case S_GAME_EVENT_UPDATE_SKILL:
    case S_GAME_EVENT_SHOW_GENERAL:
        return true;
    }
    return false;
}

bool validateConditionalSchema(const QString &schema, const QVariantMap &object,
                               QString *error)
{
    auto require = [&](const QString &field, FieldShape shape) {
        if (!object.contains(field))
            return fail(error, QStringLiteral("%1 requires field %2").arg(schema, field));
        return validateField(object, field, shape, schema, error);
    };

    if (schema == QLatin1String("UpdateCardPayload")) {
        const QString action = object.value(QStringLiteral("action")).toString();
        if (action == QLatin1String("reset"))
            return require(QStringLiteral("card_id"), FieldShape::Integer);
        if (action != QLatin1String("update"))
            return fail(error, QStringLiteral("UpdateCardPayload action is unknown"));
        return require(QStringLiteral("card_id"), FieldShape::Integer)
            && require(QStringLiteral("suit"), FieldShape::Integer)
            && require(QStringLiteral("number"), FieldShape::Integer)
            && require(QStringLiteral("card_name"), FieldShape::String)
            && require(QStringLiteral("skill_name"), FieldShape::String)
            && require(QStringLiteral("object_name"), FieldShape::String)
            && require(QStringLiteral("flags"), FieldShape::StringList);
    }
    if (schema == QLatin1String("ShowVirtualCardPayload"))
        return require(QStringLiteral("suit"), FieldShape::String);
    if (schema == QLatin1String("PlayerPropertyPayload")) {
        const QString action = object.value(QStringLiteral("action")).toString();
        if (action == QLatin1String("property"))
            return require(QStringLiteral("property_name"), FieldShape::String)
                && require(QStringLiteral("string_value"), FieldShape::String);
        if (action == QLatin1String("tag"))
            return require(QStringLiteral("tag_name"), FieldShape::String)
                && require(QStringLiteral("value_kind"), FieldShape::String);
        if (action == QLatin1String("general_pile"))
            return require(QStringLiteral("pile_name"), FieldShape::String)
                && require(QStringLiteral("general_names"), FieldShape::StringList)
                && require(QStringLiteral("add"), FieldShape::Boolean);
        return fail(error, QStringLiteral("PlayerPropertyPayload action is unknown"));
    }
    if (schema == QLatin1String("MirrorGuanxingPayload")) {
        const QString action = object.value(QStringLiteral("action")).toString();
        if (action == QLatin1String("finish"))
            return true;
        if (action == QLatin1String("move"))
            return require(QStringLiteral("from_index"), FieldShape::Integer)
                && require(QStringLiteral("to_index"), FieldShape::Integer);
        if (action == QLatin1String("start"))
            return require(QStringLiteral("player_name"), FieldShape::String)
                && require(QStringLiteral("up_only"), FieldShape::Boolean)
                && require(QStringLiteral("card_ids"), FieldShape::IntegerList);
        return fail(error, QStringLiteral("MirrorGuanxingPayload action is unknown"));
    }
    if (schema == QLatin1String("MoveFocusPayload")) {
        if (!object.contains(QStringLiteral("countdown")))
            return !object.contains(QStringLiteral("command"))
                || fail(error, QStringLiteral("MoveFocusPayload command requires countdown"));
        if (!require(QStringLiteral("command"), FieldShape::Integer)
            || !require(QStringLiteral("countdown"), FieldShape::Object)) {
            return false;
        }
        Countdown countdown;
        return countdown.tryParse(object.value(QStringLiteral("countdown")))
            || fail(error, QStringLiteral("MoveFocusPayload countdown is invalid"));
    }
    if (schema == QLatin1String("CardLimitationPayload")) {
        const QString action = object.value(QStringLiteral("action")).toString();
        if (action == QLatin1String("clear"))
            return true;
        if (action == QLatin1String("remove_by_reason"))
            return require(QStringLiteral("reason"), FieldShape::String);
        if (action == QLatin1String("set") || action == QLatin1String("remove"))
            return require(QStringLiteral("methods"), FieldShape::StringList)
                && require(QStringLiteral("pattern"), FieldShape::String)
                && require(QStringLiteral("reason"), FieldShape::String);
        return fail(error, QStringLiteral("CardLimitationPayload action is unknown"));
    }
    if (schema == QLatin1String("PreshowPayload")) {
        if (!require(QStringLiteral("states"), FieldShape::Object))
            return false;
        const QVariantMap states = object.value(QStringLiteral("states")).toMap();
        for (auto it = states.constBegin(); it != states.constEnd(); ++it) {
            bool parsed = false;
            if (!ProtocolMessageUtils::tryParseBool(it.value(), parsed))
                return fail(error, QStringLiteral("PreshowPayload state must be boolean"));
        }
    }
    if (schema == QLatin1String("SetMarkPayload")
        || schema == QLatin1String("CardMarkPayload")) {
        return require(QStringLiteral("value"), FieldShape::Integer);
    }
    if (schema == QLatin1String("SkillDescriptionPayload")
        || schema == QLatin1String("CardDescriptionPayload")) {
        return require(QStringLiteral("value"), FieldShape::String);
    }
    if (schema == QLatin1String("StateItemPayload"))
        return require(QStringLiteral("state"), FieldShape::String);
    if (schema == QLatin1String("CardMovementPayload"))
        return validateCardMovementPayload(object, error);
    if (schema == QLatin1String("GameEventPayload"))
        return validateGameEventPayload(object, error);
    return true;
}

bool validateGenericSchema(const QString &schema, const QVariant &value,
                           QString *error)
{
    const QVariantMap object = value.toMap();
    static const QHash<QString, FieldShape> commonFields {
        {QStringLiteral("action"), FieldShape::String},
        {QStringLiteral("player_name"), FieldShape::String},
        {QStringLiteral("screen_name"), FieldShape::String},
        {QStringLiteral("avatar"), FieldShape::String},
        {QStringLiteral("property_name"), FieldShape::String},
        {QStringLiteral("string_value"), FieldShape::String},
        {QStringLiteral("tag_name"), FieldShape::String},
        {QStringLiteral("value_kind"), FieldShape::String},
        {QStringLiteral("pile_name"), FieldShape::String},
        {QStringLiteral("skill_name"), FieldShape::String},
        {QStringLiteral("card_name"), FieldShape::String},
        {QStringLiteral("object_name"), FieldShape::String},
        {QStringLiteral("log_type"), FieldShape::String},
        {QStringLiteral("from_player"), FieldShape::String},
        {QStringLiteral("to_player"), FieldShape::String},
        {QStringLiteral("target_player"), FieldShape::String},
        {QStringLiteral("card_string"), FieldShape::String},
        {QStringLiteral("emotion"), FieldShape::String},
        {QStringLiteral("path"), FieldShape::String},
        {QStringLiteral("reason"), FieldShape::String},
        {QStringLiteral("pattern"), FieldShape::String},
        {QStringLiteral("first_player"), FieldShape::String},
        {QStringLiteral("second_player"), FieldShape::String},
        {QStringLiteral("history_name"), FieldShape::String},
        {QStringLiteral("first_argument"), FieldShape::String},
        {QStringLiteral("second_argument"), FieldShape::String},
        {QStringLiteral("trick_name"), FieldShape::String},
        {QStringLiteral("weapon_name"), FieldShape::String},
        {QStringLiteral("general_name"), FieldShape::String},
        {QStringLiteral("player"), FieldShape::String},
        {QStringLiteral("speaker"), FieldShape::String},
        {QStringLiteral("mark_name"), FieldShape::String},
        {QStringLiteral("flag"), FieldShape::String},
        {QStringLiteral("category"), FieldShape::String},
        {QStringLiteral("translation_suffix"), FieldShape::String},
        {QStringLiteral("from_pile"), FieldShape::String},
        {QStringLiteral("to_pile"), FieldShape::String},
        {QStringLiteral("event_name"), FieldShape::String},
        {QStringLiteral("player_id"), FieldShape::String},
        {QStringLiteral("target_id"), FieldShape::String},
        {QStringLiteral("taker"), FieldShape::String},
        {QStringLiteral("rule"), FieldShape::String},
        {QStringLiteral("key"), FieldShape::String},
        {QStringLiteral("kind"), FieldShape::String},
        {QStringLiteral("initiator"), FieldShape::String},
        {QStringLiteral("card"), FieldShape::String},
        {QStringLiteral("source_owner"), FieldShape::String},
        {QStringLiteral("source_skill"), FieldShape::String},
        {QStringLiteral("activation_owner"), FieldShape::String},
        {QStringLiteral("activation_skill"), FieldShape::String},
        {QStringLiteral("operation"), FieldShape::String},
        {QStringLiteral("seconds"), FieldShape::Integer},
        {QStringLiteral("delta"), FieldShape::Integer},
        {QStringLiteral("nature"), FieldShape::Integer},
        {QStringLiteral("lost_hp"), FieldShape::Integer},
        {QStringLiteral("number"), FieldShape::Integer},
        {QStringLiteral("card_id"), FieldShape::Integer},
        {QStringLiteral("source_instance_id"), FieldShape::Integer},
        {QStringLiteral("activation_instance_id"), FieldShape::Integer},
        {QStringLiteral("instance_id"), FieldShape::Integer},
        {QStringLiteral("event"), FieldShape::Integer},
        {QStringLiteral("times"), FieldShape::Integer},
        {QStringLiteral("animation"), FieldShape::Integer},
        {QStringLiteral("distance"), FieldShape::Integer},
        {QStringLiteral("level"), FieldShape::Integer},
        {QStringLiteral("move_id"), FieldShape::Integer},
        {QStringLiteral("swap_count"), FieldShape::Integer},
        {QStringLiteral("count"), FieldShape::Integer},
        {QStringLiteral("timeout_ms"), FieldShape::Integer},
        {QStringLiteral("range"), FieldShape::Integer},
        {QStringLiteral("index"), FieldShape::Integer},
        {QStringLiteral("area"), FieldShape::Integer},
        {QStringLiteral("from_index"), FieldShape::Integer},
        {QStringLiteral("to_index"), FieldShape::Integer},
        {QStringLiteral("audio_type"), FieldShape::Integer},
        {QStringLiteral("gender"), FieldShape::Integer},
        {QStringLiteral("from_place"), FieldShape::Integer},
        {QStringLiteral("to_place"), FieldShape::Integer},
        {QStringLiteral("from_card_id"), FieldShape::Integer},
        {QStringLiteral("to_card_id"), FieldShape::Integer},
        {QStringLiteral("command"), FieldShape::Integer},
        {QStringLiteral("standoff"), FieldShape::Boolean},
        {QStringLiteral("set"), FieldShape::Boolean},
        {QStringLiteral("enabled"), FieldShape::Boolean},
        {QStringLiteral("loop"), FieldShape::Boolean},
        {QStringLiteral("move_cards"), FieldShape::Boolean},
        {QStringLiteral("add"), FieldShape::Boolean},
        {QStringLiteral("up_only"), FieldShape::Boolean},
        {QStringLiteral("secondary"), FieldShape::Boolean},
        {QStringLiteral("enable_heart"), FieldShape::Boolean},
        {QStringLiteral("take_effect"), FieldShape::Boolean},
        {QStringLiteral("success"), FieldShape::Boolean},
        {QStringLiteral("send_log"), FieldShape::Boolean},
        {QStringLiteral("paused"), FieldShape::Boolean},
        {QStringLiteral("stop_current"), FieldShape::Boolean},
        {QStringLiteral("open"), FieldShape::Boolean},
        {QStringLiteral("player_names"), FieldShape::StringList},
        {QStringLiteral("winner_tokens"), FieldShape::StringList},
        {QStringLiteral("roles"), FieldShape::StringList},
        {QStringLiteral("to_players"), FieldShape::StringList},
        {QStringLiteral("arguments"), FieldShape::StringList},
        {QStringLiteral("general_names"), FieldShape::StringList},
        {QStringLiteral("methods"), FieldShape::StringList},
        {QStringLiteral("flags"), FieldShape::StringList},
        {QStringLiteral("card_ids"), FieldShape::IntegerList},
        {QStringLiteral("subcard_ids"), FieldShape::IntegerList},
        {QStringLiteral("disabled_card_ids"), FieldShape::IntegerList},
        {QStringLiteral("enabled_card_ids"), FieldShape::IntegerList},
        {QStringLiteral("moves"), FieldShape::ObjectList},
        {QStringLiteral("countdown"), FieldShape::Object},
        {QStringLiteral("states"), FieldShape::Object}
    };
    for (auto it = commonFields.constBegin(); it != commonFields.constEnd(); ++it) {
        if (!validateField(object, it.key(), it.value(), schema, error))
            return false;
    }
    return validateConditionalSchema(schema, object, error);
}

int expectedSchemaVersion(const QString &schema)
{
    return schema == QLatin1String("CardProvenancePayload")
        ? CardProvenanceMessage::CurrentVersion : 1;
}

bool validateKnownSchema(const QString &schema, const QVariant &value,
                         QString *error)
{
    if (schema == QLatin1String("EmptyPayload"))
        return validateTypedPayload<EmptyPayload>(value, error);
    if (schema == QLatin1String("ServerHelloPayload"))
        return validateTypedPayload<ServerHelloPayload>(value, error);
    if (schema == QLatin1String("SignupRequestPayload"))
        return validateTypedPayload<SignupRequestPayload>(value, error);
    if (schema == QLatin1String("SignupReplyPayload"))
        return validateTypedPayload<SignupReplyPayload>(value, error);
    if (schema == QLatin1String("SetupPayload"))
        return validateTypedPayload<SetupPayload>(value, error);
    if (schema == QLatin1String("ReadyPayload"))
        return validateTypedPayload<ReadyPayload>(value, error);
    if (schema == QLatin1String("DiagnosticPayload"))
        return validateTypedPayload<DiagnosticPayload>(value, error);
    if (schema == QLatin1String("NetworkDelayPayload"))
        return validateTypedPayload<NetworkDelayPayload>(value, error);
    if (schema == QLatin1String("CommandResultPayload"))
        return validateTypedPayload<CommandResultPayload>(value, error);
    if (schema == QLatin1String("ChatPayload"))
        return validateTypedPayload<ChatPayload>(value, error);
    if (schema == QLatin1String("ChatMessagePayload"))
        return validateTypedPayload<ChatMessagePayload>(value, error);
    if (schema == QLatin1String("AddRobotPayload"))
        return validateTypedPayload<AddRobotPayload>(value, error);
    if (schema == QLatin1String("TrustPayload"))
        return validateTypedPayload<TrustPayload>(value, error);
    if (schema == QLatin1String("PausePayload"))
        return validateTypedPayload<PausePayload>(value, error);
    if (schema == QLatin1String("AnytimeSkillPayload"))
        return validateTypedPayload<AnytimeSkillPayload>(value, error);
    if (schema == QLatin1String("SurrenderRequestPayload"))
        return validateTypedPayload<SurrenderRequestPayload>(value, error);
    if (schema == QLatin1String("CheatRequestPayload"))
        return validateTypedPayload<CheatRequestPayload>(value, error);
    if (schema == QLatin1String("CardProvenancePayload")) {
        CardProvenanceMessage payload;
        return payload.tryParse(value)
            || fail(error, QStringLiteral("CardProvenancePayload is invalid"));
    }
    if (schema == QLatin1String("PlayerUiStatePayload")) {
        PlayerUIStateMessage payload;
        return payload.tryParse(value)
            || fail(error, QStringLiteral("PlayerUiStatePayload is invalid"));
    }
    if (schema == QLatin1String("SkillInstancePayload")) {
        SkillInstanceMessage payload;
        return payload.tryParse(value)
            || fail(error, QStringLiteral("SkillInstancePayload is invalid"));
    }
    if (schema == QLatin1String("SyncPilePayload")) {
        SyncPileMessage payload;
        return payload.tryParse(value)
            || fail(error, QStringLiteral("SyncPilePayload is invalid"));
    }
    if (schema == QLatin1String("SwitchContextPayload")) {
        SwitchContextMessage payload;
        return payload.tryParse(value)
            || fail(error, QStringLiteral("SwitchContextPayload is invalid"));
    }
    return true;
}

QString messageTypeName(ProtocolMessageType type)
{
    switch (type) {
    case ProtocolMessageType::Request:
        return QStringLiteral("request");
    case ProtocolMessageType::Reply:
        return QStringLiteral("reply");
    case ProtocolMessageType::Notification:
        return QStringLiteral("notification");
    case ProtocolMessageType::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

QString endpointName(ProtocolEndpoint endpoint)
{
    switch (endpoint) {
    case ProtocolEndpoint::Room:
        return QStringLiteral("room");
    case ProtocolEndpoint::Lobby:
        return QStringLiteral("lobby");
    case ProtocolEndpoint::Client:
        return QStringLiteral("client");
    case ProtocolEndpoint::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

QVariantMap schemaObject()
{
    return QVariantMap{{QStringLiteral("schema_version"), 1}};
}

bool positionalPayload(const QVariant &value,
                       std::initializer_list<const char *> fields,
                       QVariantMap *output, QString *error)
{
    if (value.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("protocol payload must be a positional domain list"));
    const QVariantList values = value.toList();
    if (values.size() < static_cast<int>(fields.size()))
        return fail(error, QStringLiteral("protocol domain list is missing fields"));
    QVariantMap object = schemaObject();
    int index = 0;
    for (const char *field : fields)
        object.insert(QString::fromLatin1(field), values.at(index++));
    *output = object;
    return true;
}

bool scalarPayload(const QVariant &value, const char *field,
                   QVariantMap *output)
{
    QVariantMap object = schemaObject();
    object.insert(QString::fromLatin1(field), value);
    *output = object;
    return true;
}

bool intListFromDelimited(const QString &value, QVariantList *result)
{
    QVariantList parsed;
    if (!value.isEmpty()) {
        const QStringList parts = value.split(QLatin1Char('+'), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            bool ok = false;
            const int id = part.toInt(&ok);
            if (!ok)
                return false;
            parsed << id;
        }
    }
    *result = parsed;
    return true;
}

bool encodeGameEventPayload(const QVariant &value, QVariantMap *output,
                            QString *error)
{
    if (value.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("GameEventPayload domain value must be a list"));
    const QVariantList args = value.toList();
    int event = -1;
    if (args.isEmpty() || !ProtocolMessageUtils::tryParseInt(args.first(), event)
        || event < S_GAME_EVENT_PLAYER_DYING || event > S_GAME_EVENT_SHOW_GENERAL) {
        return fail(error, QStringLiteral("GameEventPayload event is unknown"));
    }
    QVariantMap object = schemaObject();
    object.insert(QStringLiteral("event"), event);
    auto copy = [&](int index, const char *field) {
        if (index < args.size())
            object.insert(QString::fromLatin1(field), args.at(index));
    };
    switch (event) {
    case S_GAME_EVENT_PLAYER_DYING:
    case S_GAME_EVENT_PLAYER_QUITDYING:
    case S_GAME_EVENT_PLAYER_REFORM:
        copy(1, "player_name");
        break;
    case S_GAME_EVENT_HUASHEN:
        copy(1, "player_name"); copy(2, "general_name"); copy(3, "skill_name");
        break;
    case S_GAME_EVENT_PLAY_EFFECT:
        copy(1, "skill_name");
        if (args.size() >= 3) {
            bool isMale = false;
            QString category;
            if (ProtocolMessageUtils::tryParseBool(args.at(2), isMale))
                category = isMale ? QStringLiteral("male") : QStringLiteral("female");
            else if (!ProtocolMessageUtils::tryParseString(args.at(2), category))
                return fail(error, QStringLiteral("GameEventPayload category is invalid"));
            object.insert(QStringLiteral("category"), category);
        }
        copy(3, "audio_type"); copy(4, "player_name");
        break;
    case S_GAME_EVENT_JUDGE_RESULT:
        copy(1, "card_id"); copy(2, "take_effect");
        break;
    case S_GAME_EVENT_DETACH_SKILL:
        copy(1, "player_name"); copy(2, "skill_name"); copy(3, "translation_suffix");
        break;
    case S_GAME_EVENT_ACQUIRE_SKILL:
    case S_GAME_EVENT_ADD_SKILL:
    case S_GAME_EVENT_LOSE_SKILL:
    case S_GAME_EVENT_SKILL_INVOKED:
        copy(1, "player_name"); copy(2, "skill_name");
        break;
    case S_GAME_EVENT_CHANGE_GENDER:
        copy(1, "player_name"); copy(2, "gender");
        break;
    case S_GAME_EVENT_CHANGE_HERO:
        copy(1, "player_name"); copy(2, "general_name");
        copy(3, "secondary"); copy(4, "send_log");
        break;
    case S_GAME_EVENT_PAUSE:
        copy(1, "paused");
        break;
    case S_GAME_EVENT_REVEAL_PINDIAN:
        copy(1, "from_player"); copy(2, "from_card_id");
        copy(3, "to_player"); copy(4, "to_card_id");
        copy(5, "success"); copy(6, "reason");
        break;
    case S_GAME_EVENT_CHANGE_BGM:
        copy(1, "path"); copy(2, "stop_current");
        break;
    case S_GAME_EVENT_AVATAR_ICON:
        copy(1, "player_name"); copy(2, "secondary");
        break;
    case S_GAME_EVENT_SORT_HAND: {
        copy(1, "player_name");
        QVariantList cardIds;
        if (args.size() < 3 || !intListFromDelimited(args.at(2).toString(), &cardIds))
            return fail(error, QStringLiteral("GameEventPayload sort_hand card_ids are invalid"));
        object.insert(QStringLiteral("card_ids"), cardIds);
        break;
    }
    case S_GAME_EVENT_PREPARE_SKILL:
    case S_GAME_EVENT_UPDATE_SKILL:
    case S_GAME_EVENT_SHOW_GENERAL:
        break;
    }
    *output = object;
    return true;
}

bool encodeRoomNotificationPayload(int command, const QVariant &value,
                                   QVariantMap *output, QString *error)
{
    switch (command) {
    case S_COMMAND_CLEAR_AMAZING_GRACE:
    case S_COMMAND_ADD_ROUND:
        *output = schemaObject();
        return true;
    case S_COMMAND_REMOVE_PLAYER: return scalarPayload(value, "player_name", output);
    case S_COMMAND_START_IN_X_SECONDS: return scalarPayload(value, "seconds", output);
    case S_COMMAND_ARRANGE_SEATS: return scalarPayload(value, "player_names", output);
    case S_COMMAND_GAME_START: return scalarPayload(value, "card_ids", output);
    case S_COMMAND_KILL_PLAYER:
    case S_COMMAND_REVIVE_PLAYER: return scalarPayload(value, "player_name", output);
    case S_COMMAND_CHANGE_TABLE_BG: return scalarPayload(value, "path", output);
    case S_COMMAND_NULLIFICATION_ASKED: return scalarPayload(value, "trick_name", output);
    case S_COMMAND_ENABLE_SURRENDER: return scalarPayload(value, "enabled", output);
    case S_COMMAND_UPDATE_BOSS_LEVEL: return scalarPayload(value, "level", output);
    case S_COMMAND_UPDATE_STATE_ITEM: return scalarPayload(value, "state", output);
    case S_COMMAND_AVAILABLE_CARDS: return scalarPayload(value, "card_ids", output);
    case S_COMMAND_RESET_PILE: return scalarPayload(value, "swap_count", output);
    case S_COMMAND_UPDATE_PILE: return scalarPayload(value, "count", output);
    case S_COMMAND_SYNCHRONIZE_DISCARD_PILE: return scalarPayload(value, "card_ids", output);
    case S_COMMAND_OPERATION_TIMEOUT: return scalarPayload(value, "timeout_ms", output);
    case S_COMMAND_FILL_GENERAL: return scalarPayload(value, "general_names", output);
    case S_COMMAND_UPDATE_SKILL: return scalarPayload(value, "skill_name", output);
    case S_COMMAND_CHANGE_HP:
        return positionalPayload(value, {"player_name", "delta", "nature", "lost_hp"}, output, error);
    case S_COMMAND_CHANGE_MAXHP:
        return positionalPayload(value, {"player_name", "delta"}, output, error);
    case S_COMMAND_SET_MARK:
        return positionalPayload(value, {"player_name", "mark_name", "value"}, output, error);
    case S_COMMAND_SET_EMOTION:
        return positionalPayload(value, {"player_name", "emotion"}, output, error);
    case S_COMMAND_INVOKE_SKILL:
        return positionalPayload(value, {"skill_name", "player_name"}, output, error);
    case S_COMMAND_SHOW_ALL_CARDS:
        return positionalPayload(value, {"player_name", "card_ids"}, output, error);
    case S_COMMAND_ANIMATE:
        return positionalPayload(value, {"animation", "first_argument", "second_argument"}, output, error);
    case S_COMMAND_FIXED_DISTANCE:
        return positionalPayload(value, {"from_player", "to_player", "distance", "set"}, output, error);
    case S_COMMAND_ATTACK_RANGE:
        return positionalPayload(value, {"from_player", "to_player", "set"}, output, error);
    case S_COMMAND_EXCHANGE_KNOWN_CARDS:
        return positionalPayload(value, {"first_player", "second_player"}, output, error);
    case S_COMMAND_SET_KNOWN_CARDS:
        return positionalPayload(value, {"player_name", "card_ids"}, output, error);
    case S_COMMAND_VIEW_GENERALS:
        return positionalPayload(value, {"reason", "general_names"}, output, error);
    case S_COMMAND_PLAY_AUDIO:
        return positionalPayload(value, {"path", "loop"}, output, error);
    case S_COMMAND_CARD_MARK:
        return positionalPayload(value, {"card_id", "mark_name", "value"}, output, error);
    case S_COMMAND_CARD_FLAG:
        return positionalPayload(value, {"card_id", "flag"}, output, error);
    case S_COMMAND_WEAPON_RANGE:
        return positionalPayload(value, {"weapon_name", "range"}, output, error);
    case S_COMMAND_FILL_AMAZING_GRACE:
        return positionalPayload(value, {"card_ids", "disabled_card_ids"}, output, error);
    case S_COMMAND_TAKE_AMAZING_GRACE:
        return positionalPayload(value, {"taker", "card_id", "move_cards"}, output, error);
    case S_COMMAND_TAKE_GENERAL:
        return positionalPayload(value, {"player_name", "general_name", "rule"}, output, error);
    case S_COMMAND_RECOVER_GENERAL:
        return positionalPayload(value, {"index", "general_name"}, output, error);
    case S_COMMAND_REVEAL_GENERAL:
        return positionalPayload(value, {"player_name", "general_name"}, output, error);
    case S_COMMAND_SKILL_DESCRIPTION_SWAP: {
        if (!positionalPayload(value, {"player_name", "skill_name", "key", "value"},
                               output, error)) {
            return false;
        }
        const QVariantList values = value.toList();
        if (values.size() >= 5)
            output->insert(QStringLiteral("instance_id"), values.at(4));
        return true;
    }
    case S_COMMAND_ADD_EQUIP_AREA:
        return positionalPayload(value, {"player_name", "area"}, output, error);
    case S_COMMAND_SET_EQUIP_AREA_COUNT:
        return positionalPayload(value, {"player_name", "area", "count"}, output, error);
    case S_COMMAND_UPDATE_CARD_DESC:
        return positionalPayload(value, {"player_name", "card_name", "key", "value"}, output, error);
    case S_COMMAND_SET_SHOWN_HANDCARD:
    case S_COMMAND_SET_BROKEN_EQUIP:
        return positionalPayload(value, {"player_name", "card_ids"}, output, error);
    case S_COMMAND_SKILL_GONGXIN:
        return positionalPayload(value, {"player", "enable_heart", "card_ids", "enabled_card_ids"}, output, error);
    case S_COMMAND_LOG_EVENT:
        return encodeGameEventPayload(value, output, error);
    default:
        break;
    }

    if (command == S_COMMAND_UPDATE_CARD
        && value.userType() != QMetaType::QVariantList) {
        *output = schemaObject();
        output->insert(QStringLiteral("action"), QStringLiteral("reset"));
        output->insert(QStringLiteral("card_id"), value);
        return true;
    }
    if (command == S_COMMAND_ATTACH_SKILL
        && value.userType() == QMetaType::QString) {
        *output = schemaObject();
        output->insert(QStringLiteral("player_name"), S_PLAYER_SELF_REFERENCE_ID);
        output->insert(QStringLiteral("skill_name"), value);
        return true;
    }
    if (value.userType() != QMetaType::QVariantList)
        return fail(error, QStringLiteral("registered room payload has no typed encoder"));
    const QVariantList args = value.toList();
    if (command == S_COMMAND_SHOW_CARD && args.size() == 2) {
        QVariantList cardIds;
        if (!intListFromDelimited(args.at(1).toString(), &cardIds))
            return fail(error, QStringLiteral("ShowCardPayload card_ids are invalid"));
        *output = schemaObject();
        output->insert(QStringLiteral("player_name"), args.at(0));
        output->insert(QStringLiteral("card_ids"), cardIds);
        return true;
    }
    if (command == S_COMMAND_SHOW_VIRTUAL_CARD && args.size() >= 5) {
        *output = schemaObject();
        output->insert(QStringLiteral("player_name"), args.at(0));
        output->insert(QStringLiteral("card_name"), args.at(1));
        output->insert(QStringLiteral("suit"), args.at(2));
        output->insert(QStringLiteral("number"), args.at(3));
        output->insert(QStringLiteral("skill_name"), args.at(4));
        QVariantList subcards;
        if (args.size() >= 6 && !intListFromDelimited(args.at(5).toString(), &subcards))
            return fail(error, QStringLiteral("ShowVirtualCardPayload subcard_ids are invalid"));
        output->insert(QStringLiteral("subcard_ids"), subcards);
        output->insert(QStringLiteral("target_player"), args.size() >= 7 ? args.at(6) : QVariant(QString()));
        return true;
    }
    if ((command == S_COMMAND_GET_CARD || command == S_COMMAND_LOSE_CARD)
        && !args.isEmpty()) {
        *output = schemaObject();
        output->insert(QStringLiteral("move_id"), args.first());
        QVariantList moves;
        for (int i = 1; i < args.size(); ++i)
            moves << args.at(i);
        output->insert(QStringLiteral("moves"), moves);
        return true;
    }
    if (command == S_COMMAND_ADD_HISTORY && (args.size() == 2 || args.size() == 3)) {
        *output = schemaObject();
        const int offset = args.size() == 3 ? 1 : 0;
        if (offset == 1)
            output->insert(QStringLiteral("player_name"), args.at(0));
        output->insert(QStringLiteral("history_name"), args.at(offset));
        output->insert(QStringLiteral("times"), args.at(offset + 1));
        return true;
    }
    if (command == S_COMMAND_MOVE_FOCUS && !args.isEmpty()) {
        *output = schemaObject();
        output->insert(QStringLiteral("player_names"), args.at(0));
        if (args.size() >= 3) {
            output->insert(QStringLiteral("command"), args.at(1));
            output->insert(QStringLiteral("countdown"), args.at(2));
        }
        return true;
    }
    if (command == S_COMMAND_ATTACH_SKILL) {
        *output = schemaObject();
        if (args.size() == 2) {
            output->insert(QStringLiteral("player_name"), args.at(0));
            output->insert(QStringLiteral("skill_name"), args.at(1));
            return true;
        }
    }
    if (command == S_COMMAND_UPDATE_CARD && args.size() >= 7) {
        *output = schemaObject();
        output->insert(QStringLiteral("action"), QStringLiteral("update"));
        const char *fields[] = {"card_id", "suit", "number", "card_name",
                                "skill_name", "object_name", "flags"};
        for (int i = 0; i < 7; ++i)
            output->insert(QString::fromLatin1(fields[i]), args.at(i));
        return true;
    }
    return fail(error, QStringLiteral("registered room payload has no typed encoder"));
}

QString replayPolicyName(ProtocolReplayPolicy policy)
{
    switch (policy) {
    case ProtocolReplayPolicy::Excluded:
        return QStringLiteral("excluded");
    case ProtocolReplayPolicy::Record:
        return QStringLiteral("record");
    case ProtocolReplayPolicy::TakeoverOnly:
        return QStringLiteral("takeover_only");
    }
    return QStringLiteral("excluded");
}

QString correlationPolicyName(ProtocolCorrelationPolicy policy)
{
    switch (policy) {
    case ProtocolCorrelationPolicy::None:
        return QStringLiteral("none");
    case ProtocolCorrelationPolicy::StartsRequest:
        return QStringLiteral("starts_request");
    case ProtocolCorrelationPolicy::RequiresReplyTo:
        return QStringLiteral("requires_reply_to");
    }
    return QStringLiteral("none");
}

ProtocolFlowDescriptor flow(ProtocolMessageType type, ProtocolEndpoint source,
                            ProtocolEndpoint destination, int command,
                            const char *commandName, const char *consumer,
                            const char *schema, ProtocolReplayPolicy replay,
                            ProtocolCorrelationPolicy correlation,
                            const char *status, int replyCommand = 0)
{
    ProtocolFlowDescriptor descriptor;
    descriptor.key = {type, source, destination, command};
    descriptor.commandName = QString::fromLatin1(commandName);
    descriptor.diagnosticName = QStringLiteral("%1.%2.%3.%4")
        .arg(messageTypeName(type), endpointName(source), endpointName(destination),
             descriptor.commandName);
    descriptor.producer = source == ProtocolEndpoint::Client
        ? QStringLiteral("Client")
        : (source == ProtocolEndpoint::Lobby ? QStringLiteral("ServerConnectionContext")
                                             : QStringLiteral("Room/RoomNotifier"));
    descriptor.consumer = QString::fromLatin1(consumer);
    descriptor.currentPayloadShape = QStringLiteral("typed_object");
    descriptor.targetSchema = QString::fromLatin1(schema);
    descriptor.requiredFields = {QStringLiteral("schema_version")};
    descriptor.replyCommand = replyCommand;
    descriptor.correlation = correlation;
    descriptor.replayPolicy = replay;
    descriptor.parser = QStringLiteral("ProtocolPayloadRegistry::validateObjectPayload");
    descriptor.encoder = QStringLiteral("domain DTO::toVariant");
    descriptor.migrationStatus = QString::fromLatin1(status);
    descriptor.productionEvidence = {
        descriptor.producer,
        descriptor.consumer
    };
    return descriptor;
}

void addRoomNotification(QList<ProtocolFlowDescriptor> &result, int command,
                         const char *commandName, const char *consumer,
                         const char *schema,
                         ProtocolReplayPolicy replay = ProtocolReplayPolicy::Record)
{
    result.append(flow(ProtocolMessageType::Notification,
                       ProtocolEndpoint::Room, ProtocolEndpoint::Client,
                       command, commandName, consumer, schema, replay,
                       ProtocolCorrelationPolicy::None, "complete"));
}

void addInteractionRequest(QList<ProtocolFlowDescriptor> &result, int command,
                           const char *commandName, int replyCommand)
{
    result.append(flow(ProtocolMessageType::Request,
                       ProtocolEndpoint::Room, ProtocolEndpoint::Client,
                       command, commandName, "ClientCore",
                       "InteractionRequestPayload", ProtocolReplayPolicy::Record,
                       ProtocolCorrelationPolicy::StartsRequest,
                        "complete", replyCommand));
}

void addInteractionReply(QList<ProtocolFlowDescriptor> &result, int command,
                         const char *commandName)
{
    result.append(flow(ProtocolMessageType::Reply,
                       ProtocolEndpoint::Client, ProtocolEndpoint::Room,
                       command, commandName, "RequestCoordinator",
                       "InteractionReplyPayload", ProtocolReplayPolicy::TakeoverOnly,
                       ProtocolCorrelationPolicy::RequiresReplyTo,
                        "complete"));
}

QList<ProtocolFlowDescriptor> buildDescriptors()
{
    QList<ProtocolFlowDescriptor> result;

    // Pre-signup flows use the Lobby endpoint and are never part of Replay.
    result.append(flow(ProtocolMessageType::Notification,
                       ProtocolEndpoint::Lobby, ProtocolEndpoint::Client,
                       S_COMMAND_CHECK_VERSION, "S_COMMAND_CHECK_VERSION",
                       "ClientSessionController", "ServerHelloPayload",
                       ProtocolReplayPolicy::Excluded,
                       ProtocolCorrelationPolicy::None, "complete"));
    result.last().requiredFields << QStringLiteral("game_version")
                                 << QStringLiteral("mod_name")
                                 << QStringLiteral("card_count");
    result.append(flow(ProtocolMessageType::Request,
                       ProtocolEndpoint::Client, ProtocolEndpoint::Lobby,
                       S_COMMAND_SIGNUP, "S_COMMAND_SIGNUP",
                       "ServerConnectionContext", "SignupRequestPayload",
                       ProtocolReplayPolicy::Excluded,
                       ProtocolCorrelationPolicy::StartsRequest,
                       "complete", S_COMMAND_SIGNUP));
    result.last().requiredFields << QStringLiteral("reconnect_requested")
                                 << QStringLiteral("screen_name")
                                 << QStringLiteral("avatar");
    result.append(flow(ProtocolMessageType::Reply,
                       ProtocolEndpoint::Lobby, ProtocolEndpoint::Client,
                       S_COMMAND_SIGNUP, "S_COMMAND_SIGNUP",
                       "ClientSessionController", "SignupReplyPayload",
                       ProtocolReplayPolicy::Excluded,
                       ProtocolCorrelationPolicy::RequiresReplyTo,
                       "complete"));
    result.last().requiredFields << QStringLiteral("accepted");
    result.last().optionalFields << QStringLiteral("reconnected")
                                 << QStringLiteral("player_id")
                                 << QStringLiteral("error_code")
                                 << QStringLiteral("message");
    result.append(flow(ProtocolMessageType::Notification,
                       ProtocolEndpoint::Lobby, ProtocolEndpoint::Client,
                       S_COMMAND_SETUP, "S_COMMAND_SETUP",
                       "ClientSessionController", "SetupPayload",
                       ProtocolReplayPolicy::Record,
                       ProtocolCorrelationPolicy::None, "complete"));
    result.last().requiredFields << QStringLiteral("server_name")
                                 << QStringLiteral("game_mode")
                                 << QStringLiteral("player_count");
    result.append(flow(ProtocolMessageType::Notification,
                       ProtocolEndpoint::Lobby, ProtocolEndpoint::Client,
                       S_COMMAND_WARN, "S_COMMAND_WARN",
                       "ClientSessionController", "DiagnosticPayload",
                       ProtocolReplayPolicy::Excluded,
                       ProtocolCorrelationPolicy::None, "complete"));
    result.last().requiredFields << QStringLiteral("code")
                                 << QStringLiteral("message")
                                 << QStringLiteral("fatal");

#define ROOM_NOTIFICATION(command, consumer, schema) \
    addRoomNotification(result, command, #command, consumer, schema)
    ROOM_NOTIFICATION(S_COMMAND_NETWORK_DELAY_TEST, "Client::networkDelayTest", "NetworkDelayPayload");
    ROOM_NOTIFICATION(S_COMMAND_ADD_PLAYER, "Client::addPlayer", "AddPlayerPayload");
    ROOM_NOTIFICATION(S_COMMAND_ADD_PLAYER_DYNAMIC, "Client::onPlayerAddedMidGame", "AddPlayerPayload");
    ROOM_NOTIFICATION(S_COMMAND_REMOVE_PLAYER, "Client::removePlayer", "PlayerIdentityPayload");
    ROOM_NOTIFICATION(S_COMMAND_START_IN_X_SECONDS, "Client::startInXs", "CountdownStartPayload");
    ROOM_NOTIFICATION(S_COMMAND_ARRANGE_SEATS, "Client::arrangeSeats", "ArrangeSeatsPayload");
    ROOM_NOTIFICATION(S_COMMAND_WARN, "Client::warn", "DiagnosticPayload");
    ROOM_NOTIFICATION(S_COMMAND_SPEAK, "Client::speak", "ChatMessagePayload");
    ROOM_NOTIFICATION(S_COMMAND_GAME_START, "Client::startGame", "GameStartPayload");
    ROOM_NOTIFICATION(S_COMMAND_GAME_OVER, "Client::gameOver", "GameOverPayload");
    ROOM_NOTIFICATION(S_COMMAND_CHANGE_HP, "Client::hpChange", "HpChangePayload");
    ROOM_NOTIFICATION(S_COMMAND_CHANGE_MAXHP, "Client::maxhpChange", "MaxHpChangePayload");
    ROOM_NOTIFICATION(S_COMMAND_KILL_PLAYER, "Client::killPlayer", "PlayerIdentityPayload");
    ROOM_NOTIFICATION(S_COMMAND_REVIVE_PLAYER, "Client::revivePlayer", "PlayerIdentityPayload");
    ROOM_NOTIFICATION(S_COMMAND_SHOW_CARD, "Client::showCard", "ShowCardPayload");
    ROOM_NOTIFICATION(S_COMMAND_SHOW_VIRTUAL_CARD, "Client::showVirtualCard", "ShowVirtualCardPayload");
    ROOM_NOTIFICATION(S_COMMAND_CARD_PROVENANCE, "Client::cardProvenance", "CardProvenancePayload");
    ROOM_NOTIFICATION(S_COMMAND_UPDATE_PLAYER_UI_STATE, "Client::updatePlayerUIState", "PlayerUiStatePayload");
    ROOM_NOTIFICATION(S_COMMAND_UPDATE_CARD, "Client::updateCard", "UpdateCardPayload");
    ROOM_NOTIFICATION(S_COMMAND_SET_MARK, "Client::setMark", "SetMarkPayload");
    ROOM_NOTIFICATION(S_COMMAND_LOG_SKILL, "Client::log", "SkillLogPayload");
    ROOM_NOTIFICATION(S_COMMAND_ATTACH_SKILL, "Client::attachSkill", "AttachSkillPayload");
    ROOM_NOTIFICATION(S_COMMAND_SKILL_INSTANCE, "Client::syncSkillInstances", "SkillInstancePayload");
    ROOM_NOTIFICATION(S_COMMAND_MOVE_FOCUS, "Client::moveFocus", "MoveFocusPayload");
    ROOM_NOTIFICATION(S_COMMAND_SET_EMOTION, "Client::setEmotion", "EmotionPayload");
    ROOM_NOTIFICATION(S_COMMAND_CHANGE_TABLE_BG, "Client::changeTableBg", "TableBackgroundPayload");
    ROOM_NOTIFICATION(S_COMMAND_INVOKE_SKILL, "Client::skillInvoked", "SkillInvokedPayload");
    ROOM_NOTIFICATION(S_COMMAND_SHOW_ALL_CARDS, "Client::showAllCards", "ShowAllCardsPayload");
    ROOM_NOTIFICATION(S_COMMAND_SKILL_GONGXIN, "Client::askForGongxin", "GongxinNotificationPayload");
    ROOM_NOTIFICATION(S_COMMAND_LOG_EVENT, "Client::handleGameEvent", "GameEventPayload");
    ROOM_NOTIFICATION(S_COMMAND_ADD_HISTORY, "Client::addHistory", "HistoryPayload");
    ROOM_NOTIFICATION(S_COMMAND_ANIMATE, "Client::animate", "AnimationPayload");
    ROOM_NOTIFICATION(S_COMMAND_FIXED_DISTANCE, "Client::setFixedDistance", "FixedDistancePayload");
    ROOM_NOTIFICATION(S_COMMAND_ATTACK_RANGE, "Client::setAttackRangePair", "AttackRangePayload");
    ROOM_NOTIFICATION(S_COMMAND_CARD_LIMITATION, "Client::cardLimitation", "CardLimitationPayload");
    ROOM_NOTIFICATION(S_COMMAND_NULLIFICATION_ASKED, "Client::setNullification", "NullificationStatePayload");
    ROOM_NOTIFICATION(S_COMMAND_ENABLE_SURRENDER, "Client::enableSurrender", "SurrenderEnabledPayload");
    ROOM_NOTIFICATION(S_COMMAND_EXCHANGE_KNOWN_CARDS, "Client::exchangeKnownCards", "ExchangeKnownCardsPayload");
    ROOM_NOTIFICATION(S_COMMAND_SET_KNOWN_CARDS, "Client::setKnownCards", "KnownCardsPayload");
    ROOM_NOTIFICATION(S_COMMAND_SWITCH_CONTEXT, "Client::processContextSwitch", "SwitchContextPayload");
    ROOM_NOTIFICATION(S_COMMAND_VIEW_GENERALS, "Client::viewGenerals", "ViewGeneralsPayload");
    ROOM_NOTIFICATION(S_COMMAND_PLAY_AUDIO, "Client::playAudio", "PlayAudioPayload");
    ROOM_NOTIFICATION(S_COMMAND_UPDATE_BOSS_LEVEL, "Client::updateBossLevel", "BossLevelPayload");
    ROOM_NOTIFICATION(S_COMMAND_UPDATE_STATE_ITEM, "Client::updateStateItem", "StateItemPayload");
    ROOM_NOTIFICATION(S_COMMAND_AVAILABLE_CARDS, "Client::setAvailableCards", "AvailableCardsPayload");
    ROOM_NOTIFICATION(S_COMMAND_GET_CARD, "Client::getCards", "CardMovementPayload");
    ROOM_NOTIFICATION(S_COMMAND_LOSE_CARD, "Client::loseCards", "CardMovementPayload");
    ROOM_NOTIFICATION(S_COMMAND_SET_PROPERTY, "Client::updateProperty", "PlayerPropertyPayload");
    ROOM_NOTIFICATION(S_COMMAND_RESET_PILE, "Client::resetPiles", "ResetPilePayload");
    ROOM_NOTIFICATION(S_COMMAND_UPDATE_PILE, "Client::setPileNumber", "PileCountPayload");
    ROOM_NOTIFICATION(S_COMMAND_SYNCHRONIZE_DISCARD_PILE, "Client::synchronizeDiscardPile", "DiscardPilePayload");
    ROOM_NOTIFICATION(S_COMMAND_SYNC_PILE, "Client::syncPile", "SyncPilePayload");
    ROOM_NOTIFICATION(S_COMMAND_CARD_MARK, "Client::setCardMark", "CardMarkPayload");
    ROOM_NOTIFICATION(S_COMMAND_CARD_FLAG, "Client::setCardFlag", "CardFlagPayload");
    ROOM_NOTIFICATION(S_COMMAND_OPERATION_TIMEOUT, "Client::setTimeout", "OperationTimeoutPayload");
    ROOM_NOTIFICATION(S_COMMAND_WEAPON_RANGE, "Client::updateWeaponRange", "WeaponRangePayload");
    ROOM_NOTIFICATION(S_COMMAND_MIRROR_GUANXING_STEP, "Client::mirrorGuanxingStep", "MirrorGuanxingPayload");
    ROOM_NOTIFICATION(S_COMMAND_FILL_AMAZING_GRACE, "Client::fillAG", "FillAmazingGracePayload");
    ROOM_NOTIFICATION(S_COMMAND_TAKE_AMAZING_GRACE, "Client::takeAG", "TakeAmazingGracePayload");
    ROOM_NOTIFICATION(S_COMMAND_CLEAR_AMAZING_GRACE, "Client::clearAG", "EmptyPayload");
    ROOM_NOTIFICATION(S_COMMAND_FILL_GENERAL, "Client::fillGenerals", "FillGeneralsPayload");
    ROOM_NOTIFICATION(S_COMMAND_TAKE_GENERAL, "Client::takeGeneral", "TakeGeneralPayload");
    ROOM_NOTIFICATION(S_COMMAND_RECOVER_GENERAL, "Client::recoverGeneral", "RecoverGeneralPayload");
    ROOM_NOTIFICATION(S_COMMAND_REVEAL_GENERAL, "Client::revealGeneral", "RevealGeneralPayload");
    ROOM_NOTIFICATION(S_COMMAND_UPDATE_SKILL, "Client::updateSkill", "UpdateSkillPayload");
    ROOM_NOTIFICATION(S_COMMAND_ADD_ROUND, "Client::addRound", "RoundPayload");
    ROOM_NOTIFICATION(S_COMMAND_SKILL_DESCRIPTION_SWAP, "Client::setSkillDescriptionSwap", "SkillDescriptionPayload");
    ROOM_NOTIFICATION(S_COMMAND_ADD_EQUIP_AREA, "Client::addEquipArea", "EquipAreaPayload");
    ROOM_NOTIFICATION(S_COMMAND_SET_EQUIP_AREA_COUNT, "Client::setEquipAreaCount", "EquipAreaCountPayload");
    ROOM_NOTIFICATION(S_COMMAND_UPDATE_CARD_DESC, "Client::updateCardDescription", "CardDescriptionPayload");
    ROOM_NOTIFICATION(S_COMMAND_ANYTIME_SKILL_DONE, "Client::handleAnytimeSkillDone", "AnytimeSkillPayload");
    ROOM_NOTIFICATION(S_COMMAND_SET_SHOWN_HANDCARD, "Client::setShownHandCards", "ShownHandCardsPayload");
    ROOM_NOTIFICATION(S_COMMAND_SET_BROKEN_EQUIP, "Client::setBrokenEquips", "BrokenEquipPayload");
    ROOM_NOTIFICATION(S_COMMAND_PRESHOW, "Client::preshow", "PreshowPayload");
#undef ROOM_NOTIFICATION

#define INTERACTION_REQUEST(command, reply) \
    addInteractionRequest(result, command, #command, reply)
    INTERACTION_REQUEST(S_COMMAND_CHOOSE_CARD, S_COMMAND_CHOOSE_CARD);
    INTERACTION_REQUEST(S_COMMAND_PLAY_CARD, S_COMMAND_RESPONSE_CARD);
    INTERACTION_REQUEST(S_COMMAND_RESPONSE_CARD, S_COMMAND_RESPONSE_CARD);
    INTERACTION_REQUEST(S_COMMAND_SHOW_CARD, S_COMMAND_RESPONSE_CARD);
    INTERACTION_REQUEST(S_COMMAND_EXCHANGE_CARD, S_COMMAND_DISCARD_CARD);
    INTERACTION_REQUEST(S_COMMAND_DISCARD_CARD, S_COMMAND_DISCARD_CARD);
    INTERACTION_REQUEST(S_COMMAND_INVOKE_SKILL, S_COMMAND_INVOKE_SKILL);
    INTERACTION_REQUEST(S_COMMAND_CHOOSE_GENERAL, S_COMMAND_CHOOSE_GENERAL);
    INTERACTION_REQUEST(S_COMMAND_CHOOSE_KINGDOM, S_COMMAND_CHOOSE_KINGDOM);
    INTERACTION_REQUEST(S_COMMAND_CHOOSE_SUIT, S_COMMAND_CHOOSE_SUIT);
    INTERACTION_REQUEST(S_COMMAND_CHOOSE_ROLE, S_COMMAND_CHOOSE_ROLE);
    INTERACTION_REQUEST(S_COMMAND_CHOOSE_ROLE_3V3, S_COMMAND_CHOOSE_ROLE_3V3);
    INTERACTION_REQUEST(S_COMMAND_CHOOSE_DIRECTION, S_COMMAND_CHOOSE_DIRECTION);
    INTERACTION_REQUEST(S_COMMAND_CHOOSE_PLAYER, S_COMMAND_CHOOSE_PLAYER);
    INTERACTION_REQUEST(S_COMMAND_CHOOSE_ORDER, S_COMMAND_CHOOSE_ORDER);
    INTERACTION_REQUEST(S_COMMAND_ASK_PEACH, S_COMMAND_RESPONSE_CARD);
    INTERACTION_REQUEST(S_COMMAND_NULLIFICATION, S_COMMAND_RESPONSE_CARD);
    INTERACTION_REQUEST(S_COMMAND_MULTIPLE_CHOICE, S_COMMAND_MULTIPLE_CHOICE);
    INTERACTION_REQUEST(S_COMMAND_PINDIAN, S_COMMAND_RESPONSE_CARD);
    INTERACTION_REQUEST(S_COMMAND_AMAZING_GRACE, S_COMMAND_AMAZING_GRACE);
    INTERACTION_REQUEST(S_COMMAND_SKILL_YIJI, S_COMMAND_SKILL_YIJI);
    INTERACTION_REQUEST(S_COMMAND_SKILL_GUANXING, S_COMMAND_SKILL_GUANXING);
    INTERACTION_REQUEST(S_COMMAND_SKILL_GONGXIN, S_COMMAND_SKILL_GONGXIN);
    INTERACTION_REQUEST(S_COMMAND_SURRENDER, S_COMMAND_SURRENDER);
    INTERACTION_REQUEST(S_COMMAND_ASK_GENERAL, S_COMMAND_ASK_GENERAL);
    INTERACTION_REQUEST(S_COMMAND_ARRANGE_GENERAL, S_COMMAND_ARRANGE_GENERAL);
    INTERACTION_REQUEST(S_COMMAND_LUCK_CARD, S_COMMAND_LUCK_CARD);
    INTERACTION_REQUEST(S_COMMAND_TRIGGER_ORDER, S_COMMAND_TRIGGER_ORDER);
    INTERACTION_REQUEST(S_COMMAND_QML_INTERACT, S_COMMAND_QML_INTERACT);
#undef INTERACTION_REQUEST

#define INTERACTION_REPLY(command) \
    addInteractionReply(result, command, #command)
    INTERACTION_REPLY(S_COMMAND_CHOOSE_CARD);
    INTERACTION_REPLY(S_COMMAND_RESPONSE_CARD);
    INTERACTION_REPLY(S_COMMAND_DISCARD_CARD);
    INTERACTION_REPLY(S_COMMAND_INVOKE_SKILL);
    INTERACTION_REPLY(S_COMMAND_CHOOSE_GENERAL);
    INTERACTION_REPLY(S_COMMAND_CHOOSE_KINGDOM);
    INTERACTION_REPLY(S_COMMAND_CHOOSE_SUIT);
    INTERACTION_REPLY(S_COMMAND_CHOOSE_ROLE);
    INTERACTION_REPLY(S_COMMAND_CHOOSE_ROLE_3V3);
    INTERACTION_REPLY(S_COMMAND_CHOOSE_DIRECTION);
    INTERACTION_REPLY(S_COMMAND_CHOOSE_PLAYER);
    INTERACTION_REPLY(S_COMMAND_CHOOSE_ORDER);
    INTERACTION_REPLY(S_COMMAND_SURRENDER);
    INTERACTION_REPLY(S_COMMAND_MULTIPLE_CHOICE);
    INTERACTION_REPLY(S_COMMAND_AMAZING_GRACE);
    INTERACTION_REPLY(S_COMMAND_SKILL_YIJI);
    INTERACTION_REPLY(S_COMMAND_SKILL_GUANXING);
    INTERACTION_REPLY(S_COMMAND_SKILL_GONGXIN);
    INTERACTION_REPLY(S_COMMAND_ASK_GENERAL);
    INTERACTION_REPLY(S_COMMAND_ARRANGE_GENERAL);
    INTERACTION_REPLY(S_COMMAND_LUCK_CARD);
    INTERACTION_REPLY(S_COMMAND_TRIGGER_ORDER);
    INTERACTION_REPLY(S_COMMAND_QML_INTERACT);
#undef INTERACTION_REPLY

    // Client controls. Request-shaped commands receive explicit typed replies;
    // notifications are idempotent commands with no implicit acknowledgement.
    result.append(flow(ProtocolMessageType::Notification, ProtocolEndpoint::Client,
                       ProtocolEndpoint::Room, S_COMMAND_READY,
                       "S_COMMAND_READY", "Room::setReadyCommand",
                       "ReadyPayload", ProtocolReplayPolicy::Excluded,
                       ProtocolCorrelationPolicy::None, "complete"));
    result.last().requiredFields << QStringLiteral("ready");
#define CLIENT_CONTROL(command, consumer, schema) \
    result.append(flow(ProtocolMessageType::Notification, ProtocolEndpoint::Client, \
                       ProtocolEndpoint::Room, command, #command, consumer, schema, \
                       ProtocolReplayPolicy::Excluded, ProtocolCorrelationPolicy::None, \
                       "complete"))
    CLIENT_CONTROL(S_COMMAND_ADD_ROBOT, "Room::addRobotCommand", "AddRobotPayload");
    CLIENT_CONTROL(S_COMMAND_TRUST, "Room::trustCommand", "TrustPayload");
    CLIENT_CONTROL(S_COMMAND_PAUSE, "Room::pauseCommand", "PausePayload");
    CLIENT_CONTROL(S_COMMAND_SPEAK, "Room::speakCommand", "ChatPayload");
    CLIENT_CONTROL(S_COMMAND_ANYTIME_SKILL, "Room::handleAnytimeSkillRequest", "AnytimeSkillPayload");
    CLIENT_CONTROL(S_COMMAND_MIRROR_GUANXING_STEP, "Room::mirrorGuanxingStep", "MirrorGuanxingPayload");
#undef CLIENT_CONTROL

#define CLIENT_REQUEST(command, consumer, schema) \
    result.append(flow(ProtocolMessageType::Request, ProtocolEndpoint::Client, \
                       ProtocolEndpoint::Room, command, #command, consumer, schema, \
                       ProtocolReplayPolicy::Excluded, ProtocolCorrelationPolicy::StartsRequest, \
                        "complete", command)); \
    result.append(flow(ProtocolMessageType::Reply, ProtocolEndpoint::Room, \
                       ProtocolEndpoint::Client, command, #command, "ClientSessionController", \
                       "CommandResultPayload", ProtocolReplayPolicy::Excluded, \
                        ProtocolCorrelationPolicy::RequiresReplyTo, "complete"))
    CLIENT_REQUEST(S_COMMAND_NETWORK_DELAY_TEST, "Room::networkDelayTest", "NetworkDelayPayload");
    CLIENT_REQUEST(S_COMMAND_CHEAT, "Room::processRequestCheat", "CheatRequestPayload");
    CLIENT_REQUEST(S_COMMAND_SURRENDER, "Room::processRequestSurrender", "SurrenderRequestPayload");
#undef CLIENT_REQUEST

    std::sort(result.begin(), result.end(), [](const ProtocolFlowDescriptor &left,
                                               const ProtocolFlowDescriptor &right) {
        if (left.key.command != right.key.command)
            return left.key.command < right.key.command;
        if (left.key.messageType != right.key.messageType)
            return left.key.messageType < right.key.messageType;
        if (left.key.source != right.key.source)
            return left.key.source < right.key.source;
        return left.key.destination < right.key.destination;
    });

    const QHash<QString, QStringList> requiredBySchema {
        {QStringLiteral("ServerHelloPayload"), {QStringLiteral("game_version"), QStringLiteral("mod_name"), QStringLiteral("card_count")}},
        {QStringLiteral("SignupRequestPayload"), {QStringLiteral("reconnect_requested"), QStringLiteral("screen_name"), QStringLiteral("avatar")}},
        {QStringLiteral("SignupReplyPayload"), {QStringLiteral("accepted")}},
        {QStringLiteral("SetupPayload"), {QStringLiteral("server_name"), QStringLiteral("game_mode"), QStringLiteral("game_rule_mode"), QStringLiteral("operation_timeout"), QStringLiteral("nullification_countdown"), QStringLiteral("server_timeout_gracious_period"), QStringLiteral("ban_packages"), QStringLiteral("random_seat"), QStringLiteral("enable_cheat"), QStringLiteral("free_choose"), QStringLiteral("enable_second_general"), QStringLiteral("enable_same"), QStringLiteral("enable_basara"), QStringLiteral("enable_hegemony"), QStringLiteral("enable_melee_mode"), QStringLiteral("enable_ai"), QStringLiteral("disable_chat"), QStringLiteral("max_hp_scheme"), QStringLiteral("scheme0_subtraction"), QStringLiteral("player_count")}},
        {QStringLiteral("ReadyPayload"), {QStringLiteral("ready")}},
        {QStringLiteral("DiagnosticPayload"), {QStringLiteral("code"), QStringLiteral("message"), QStringLiteral("fatal")}},
        {QStringLiteral("NetworkDelayPayload"), {QStringLiteral("nonce")}},
        {QStringLiteral("CommandResultPayload"), {QStringLiteral("success")}},
        {QStringLiteral("ChatPayload"), {QStringLiteral("text")}},
        {QStringLiteral("ChatMessagePayload"), {QStringLiteral("speaker"), QStringLiteral("text")}},
        {QStringLiteral("AddRobotPayload"), {QStringLiteral("fill_remaining"), QStringLiteral("count")}},
        {QStringLiteral("TrustPayload"), {QStringLiteral("trusted")}},
        {QStringLiteral("PausePayload"), {QStringLiteral("paused")}},
        {QStringLiteral("AnytimeSkillPayload"), {QStringLiteral("skill_name")}},
        {QStringLiteral("SurrenderRequestPayload"), {QStringLiteral("requested")}},
        {QStringLiteral("CheatRequestPayload"), {QStringLiteral("action")}},
        {QStringLiteral("AddPlayerPayload"), {QStringLiteral("player_name"), QStringLiteral("screen_name"), QStringLiteral("avatar")}},
        {QStringLiteral("PlayerIdentityPayload"), {QStringLiteral("player_name")}},
        {QStringLiteral("CountdownStartPayload"), {QStringLiteral("seconds")}},
        {QStringLiteral("ArrangeSeatsPayload"), {QStringLiteral("player_names")}},
        {QStringLiteral("GameStartPayload"), {QStringLiteral("card_ids")}},
        {QStringLiteral("GameOverPayload"), {QStringLiteral("standoff"), QStringLiteral("winner_tokens"), QStringLiteral("roles")}},
        {QStringLiteral("HpChangePayload"), {QStringLiteral("player_name"), QStringLiteral("delta"), QStringLiteral("nature"), QStringLiteral("lost_hp")}},
        {QStringLiteral("MaxHpChangePayload"), {QStringLiteral("player_name"), QStringLiteral("delta")}},
        {QStringLiteral("ShowCardPayload"), {QStringLiteral("player_name"), QStringLiteral("card_ids")}},
        {QStringLiteral("ShowVirtualCardPayload"), {QStringLiteral("player_name"), QStringLiteral("card_name"), QStringLiteral("suit"), QStringLiteral("number"), QStringLiteral("skill_name"), QStringLiteral("subcard_ids"), QStringLiteral("target_player")}},
        {QStringLiteral("CardProvenancePayload"), {QStringLiteral("kind"), QStringLiteral("initiator"), QStringLiteral("card"), QStringLiteral("source_owner"), QStringLiteral("source_skill"), QStringLiteral("source_instance_id"), QStringLiteral("activation_owner"), QStringLiteral("activation_skill"), QStringLiteral("activation_instance_id")}},
        {QStringLiteral("PlayerUiStatePayload"), {QStringLiteral("player_name"), QStringLiteral("state")}},
        {QStringLiteral("UpdateCardPayload"), {QStringLiteral("action"), QStringLiteral("card_id")}},
        {QStringLiteral("SetMarkPayload"), {QStringLiteral("player_name"), QStringLiteral("mark_name"), QStringLiteral("value")}},
        {QStringLiteral("SkillLogPayload"), {QStringLiteral("log_type"), QStringLiteral("from_player"), QStringLiteral("to_players"), QStringLiteral("card_string"), QStringLiteral("arguments")}},
        {QStringLiteral("AttachSkillPayload"), {QStringLiteral("player_name"), QStringLiteral("skill_name")}},
        {QStringLiteral("SkillInstancePayload"), {QStringLiteral("action")}},
        {QStringLiteral("MoveFocusPayload"), {QStringLiteral("player_names")}},
        {QStringLiteral("EmotionPayload"), {QStringLiteral("player_name"), QStringLiteral("emotion")}},
        {QStringLiteral("TableBackgroundPayload"), {QStringLiteral("path")}},
        {QStringLiteral("SkillInvokedPayload"), {QStringLiteral("player_name"), QStringLiteral("skill_name")}},
        {QStringLiteral("ShowAllCardsPayload"), {QStringLiteral("player_name"), QStringLiteral("card_ids")}},
        {QStringLiteral("GongxinNotificationPayload"), {QStringLiteral("player"), QStringLiteral("enable_heart"), QStringLiteral("card_ids"), QStringLiteral("enabled_card_ids")}},
        {QStringLiteral("GameEventPayload"), {QStringLiteral("event")}},
        {QStringLiteral("HistoryPayload"), {QStringLiteral("history_name"), QStringLiteral("times")}},
        {QStringLiteral("AnimationPayload"), {QStringLiteral("animation"), QStringLiteral("first_argument"), QStringLiteral("second_argument")}},
        {QStringLiteral("FixedDistancePayload"), {QStringLiteral("from_player"), QStringLiteral("to_player"), QStringLiteral("distance"), QStringLiteral("set")}},
        {QStringLiteral("AttackRangePayload"), {QStringLiteral("from_player"), QStringLiteral("to_player"), QStringLiteral("set")}},
        {QStringLiteral("CardLimitationPayload"), {QStringLiteral("action")}},
        {QStringLiteral("NullificationStatePayload"), {QStringLiteral("trick_name")}},
        {QStringLiteral("SurrenderEnabledPayload"), {QStringLiteral("enabled")}},
        {QStringLiteral("ExchangeKnownCardsPayload"), {QStringLiteral("first_player"), QStringLiteral("second_player")}},
        {QStringLiteral("KnownCardsPayload"), {QStringLiteral("player_name"), QStringLiteral("card_ids")}},
        {QStringLiteral("SwitchContextPayload"), {QStringLiteral("player_name")}},
        {QStringLiteral("ViewGeneralsPayload"), {QStringLiteral("reason"), QStringLiteral("general_names")}},
        {QStringLiteral("PlayAudioPayload"), {QStringLiteral("path"), QStringLiteral("loop")}},
        {QStringLiteral("BossLevelPayload"), {QStringLiteral("level")}},
        {QStringLiteral("StateItemPayload"), {QStringLiteral("state")}},
        {QStringLiteral("AvailableCardsPayload"), {QStringLiteral("card_ids")}},
        {QStringLiteral("CardMovementPayload"), {QStringLiteral("move_id"), QStringLiteral("moves")}},
        {QStringLiteral("PlayerPropertyPayload"), {QStringLiteral("action"), QStringLiteral("player_name")}},
        {QStringLiteral("ResetPilePayload"), {QStringLiteral("swap_count")}},
        {QStringLiteral("PileCountPayload"), {QStringLiteral("count")}},
        {QStringLiteral("DiscardPilePayload"), {QStringLiteral("card_ids")}},
        {QStringLiteral("SyncPilePayload"), {QStringLiteral("player_name"), QStringLiteral("pile_name"), QStringLiteral("card_ids")}},
        {QStringLiteral("CardMarkPayload"), {QStringLiteral("card_id"), QStringLiteral("mark_name"), QStringLiteral("value")}},
        {QStringLiteral("CardFlagPayload"), {QStringLiteral("card_id"), QStringLiteral("flag")}},
        {QStringLiteral("OperationTimeoutPayload"), {QStringLiteral("timeout_ms")}},
        {QStringLiteral("WeaponRangePayload"), {QStringLiteral("weapon_name"), QStringLiteral("range")}},
        {QStringLiteral("MirrorGuanxingPayload"), {QStringLiteral("action")}},
        {QStringLiteral("FillAmazingGracePayload"), {QStringLiteral("card_ids"), QStringLiteral("disabled_card_ids")}},
        {QStringLiteral("TakeAmazingGracePayload"), {QStringLiteral("taker"), QStringLiteral("card_id"), QStringLiteral("move_cards")}},
        {QStringLiteral("FillGeneralsPayload"), {QStringLiteral("general_names")}},
        {QStringLiteral("TakeGeneralPayload"), {QStringLiteral("player_name"), QStringLiteral("general_name"), QStringLiteral("rule")}},
        {QStringLiteral("RecoverGeneralPayload"), {QStringLiteral("index"), QStringLiteral("general_name")}},
        {QStringLiteral("RevealGeneralPayload"), {QStringLiteral("player_name"), QStringLiteral("general_name")}},
        {QStringLiteral("UpdateSkillPayload"), {QStringLiteral("skill_name")}},
        {QStringLiteral("SkillDescriptionPayload"), {QStringLiteral("player_name"), QStringLiteral("skill_name"), QStringLiteral("key"), QStringLiteral("value")}},
        {QStringLiteral("EquipAreaPayload"), {QStringLiteral("player_name"), QStringLiteral("area")}},
        {QStringLiteral("EquipAreaCountPayload"), {QStringLiteral("player_name"), QStringLiteral("area"), QStringLiteral("count")}},
        {QStringLiteral("CardDescriptionPayload"), {QStringLiteral("player_name"), QStringLiteral("card_name"), QStringLiteral("key"), QStringLiteral("value")}},
        {QStringLiteral("ShownHandCardsPayload"), {QStringLiteral("player_name"), QStringLiteral("card_ids")}},
        {QStringLiteral("BrokenEquipPayload"), {QStringLiteral("player_name"), QStringLiteral("card_ids")}},
        {QStringLiteral("PreshowPayload"), {QStringLiteral("player_name"), QStringLiteral("states")}}
    };
    for (ProtocolFlowDescriptor &descriptor : result) {
        descriptor.requiredFields = {QStringLiteral("schema_version")};
        descriptor.requiredFields.append(requiredBySchema.value(descriptor.targetSchema));
        descriptor.currentPayloadShape = QStringLiteral("typed_object");
        descriptor.parser = descriptor.targetSchema == QLatin1String("InteractionRequestPayload")
            || descriptor.targetSchema == QLatin1String("InteractionReplyPayload")
            ? QStringLiteral("ProtocolGameplayPayloadRegistry::decodeFromWire")
            : QStringLiteral("ProtocolPayloadRegistry::validateObjectPayload");
        descriptor.encoder = descriptor.targetSchema == QLatin1String("InteractionRequestPayload")
            || descriptor.targetSchema == QLatin1String("InteractionReplyPayload")
            ? QStringLiteral("ProtocolGameplayPayloadRegistry::encodeForWire")
            : QStringLiteral("ProtocolPayloadRegistry::encodeObjectPayload");
        descriptor.migrationStatus = QStringLiteral("complete");
    }
    return result;
}
}

bool ProtocolFlowKey::operator==(const ProtocolFlowKey &other) const
{
    return messageType == other.messageType && source == other.source
        && destination == other.destination && command == other.command;
}

const QList<ProtocolFlowDescriptor> &ProtocolPayloadRegistry::descriptors()
{
    static const QList<ProtocolFlowDescriptor> values = buildDescriptors();
    return values;
}

const ProtocolFlowDescriptor *ProtocolPayloadRegistry::find(const ProtocolFlowKey &key)
{
    const QList<ProtocolFlowDescriptor> &values = descriptors();
    for (const ProtocolFlowDescriptor &descriptor : values) {
        if (descriptor.key == key)
            return &descriptor;
    }
    return nullptr;
}

const ProtocolFlowDescriptor *ProtocolPayloadRegistry::find(const ProtocolMessage &message)
{
    return find({message.type, message.source, message.destination, message.command});
}

bool ProtocolPayloadRegistry::validateObjectPayload(
    const ProtocolMessage &message, QString *error)
{
    if (error != nullptr)
        error->clear();
    const ProtocolFlowDescriptor *descriptor = find(message);
    if (descriptor == nullptr) {
        return fail(error, QStringLiteral("Unregistered Protocol V2 flow: %1/%2/%3/%4")
            .arg(messageTypeName(message.type), endpointName(message.source),
                 endpointName(message.destination), QString::number(message.command)));
    }
    if (!message.hasPayload || message.payload.userType() != QMetaType::QVariantMap) {
        return fail(error, QStringLiteral("%1 payload must be an object")
            .arg(descriptor->diagnosticName));
    }
    const QVariantMap object = message.payload.toMap();
    int schemaVersion = 0;
    const int requiredSchemaVersion = expectedSchemaVersion(descriptor->targetSchema);
    if (!ProtocolMessageUtils::tryParseInt(
            object.value(QStringLiteral("schema_version")), schemaVersion)
        || schemaVersion != requiredSchemaVersion) {
        return fail(error, QStringLiteral("%1 schema_version must be integral %2")
            .arg(descriptor->diagnosticName).arg(requiredSchemaVersion));
    }
    for (const QString &required : descriptor->requiredFields) {
        if (!object.contains(required)) {
            return fail(error, QStringLiteral("%1 requires field %2")
                .arg(descriptor->diagnosticName, required));
        }
    }
    if (!validateKnownSchema(descriptor->targetSchema, message.payload, error)) {
        return false;
    }
    return validateGenericSchema(descriptor->targetSchema, message.payload, error);
}

bool ProtocolPayloadRegistry::encodeObjectPayload(
    const ProtocolMessage &logicalMessage, ProtocolMessage *wireMessage,
    QString *error)
{
    if (error != nullptr)
        error->clear();
    if (wireMessage == nullptr)
        return fail(error, QStringLiteral("Protocol wire message output is null"));
    const ProtocolFlowDescriptor *descriptor = find(logicalMessage);
    if (descriptor == nullptr)
        return fail(error, QStringLiteral("cannot encode an unregistered Protocol V2 flow"));

    ProtocolMessage encoded = logicalMessage;
    encoded.version = ProtocolVersion::V2;
    if (logicalMessage.hasPayload
        && logicalMessage.payload.userType() == QMetaType::QVariantMap) {
        const QVariantMap object = logicalMessage.payload.toMap();
        int schemaVersion = 0;
        if (ProtocolMessageUtils::tryParseInt(
                object.value(QStringLiteral("schema_version")), schemaVersion)
            && schemaVersion == expectedSchemaVersion(descriptor->targetSchema)) {
            if (!validateObjectPayload(encoded, error))
                return false;
            *wireMessage = encoded;
            return true;
        }
    }

    QVariantMap object;
    if (logicalMessage.type != ProtocolMessageType::Notification
        || logicalMessage.source != ProtocolEndpoint::Room
        || logicalMessage.destination != ProtocolEndpoint::Client
        || !encodeRoomNotificationPayload(logicalMessage.command,
                                           logicalMessage.payload,
                                           &object, error)) {
        if (error != nullptr && error->isEmpty()) {
            *error = QStringLiteral("%1 requires its registered typed encoder")
                .arg(descriptor->diagnosticName);
        }
        return false;
    }
    encoded.hasPayload = true;
    encoded.payload = object;
    if (!validateObjectPayload(encoded, error))
        return false;
    *wireMessage = encoded;
    return true;
}

bool ProtocolPayloadRegistry::isReplayEligible(
    const ProtocolMessage &message, bool takeoverMode)
{
    const ProtocolFlowDescriptor *descriptor = find(message);
    if (descriptor == nullptr)
        return false;
    if (descriptor->replayPolicy == ProtocolReplayPolicy::Record)
        return true;
    return takeoverMode
        && descriptor->replayPolicy == ProtocolReplayPolicy::TakeoverOnly;
}

QJsonObject ProtocolPayloadRegistry::inventoryJson()
{
    QJsonArray flows;
    int typedComplete = 0;
    for (const ProtocolFlowDescriptor &descriptor : descriptors()) {
        QJsonObject value;
        value.insert(QStringLiteral("command"), descriptor.commandName);
        value.insert(QStringLiteral("command_id"), descriptor.key.command);
        value.insert(QStringLiteral("message_type"), messageTypeName(descriptor.key.messageType));
        value.insert(QStringLiteral("source"), endpointName(descriptor.key.source));
        value.insert(QStringLiteral("destination"), endpointName(descriptor.key.destination));
        value.insert(QStringLiteral("producer"), descriptor.producer);
        value.insert(QStringLiteral("consumer"), descriptor.consumer);
        value.insert(QStringLiteral("current_payload_shape"), descriptor.currentPayloadShape);
        value.insert(QStringLiteral("target_typed_schema"), descriptor.targetSchema);
        value.insert(QStringLiteral("required_fields"), QJsonArray::fromStringList(descriptor.requiredFields));
        value.insert(QStringLiteral("optional_fields"), QJsonArray::fromStringList(descriptor.optionalFields));
        value.insert(QStringLiteral("reply_command_id"), descriptor.replyCommand);
        value.insert(QStringLiteral("correlation"), correlationPolicyName(descriptor.correlation));
        value.insert(QStringLiteral("replay_eligibility"), replayPolicyName(descriptor.replayPolicy));
        value.insert(QStringLiteral("parser"), descriptor.parser);
        value.insert(QStringLiteral("encoder"), descriptor.encoder);
        value.insert(QStringLiteral("migration_status"), descriptor.migrationStatus);
        value.insert(QStringLiteral("production_call_site_evidence"),
                     QJsonArray::fromStringList(descriptor.productionEvidence));
        if (descriptor.migrationStatus == QLatin1String("complete"))
            ++typedComplete;
        flows.append(value);
    }

    QJsonArray unused;
    auto addUnused = [&unused](const char *name, int command, const char *reason) {
        QJsonObject value;
        value.insert(QStringLiteral("command"), QString::fromLatin1(name));
        value.insert(QStringLiteral("command_id"), command);
        value.insert(QStringLiteral("reason"), QString::fromLatin1(reason));
        unused.append(value);
    };
    addUnused("S_COMMAND_UNKNOWN", S_COMMAND_UNKNOWN, "sentinel; no production producer");
    addUnused("S_COMMAND_SET_FLAG", S_COMMAND_SET_FLAG, "no production producer or consumer found");
    addUnused("S_COMMAND_MOVE_CARD", S_COMMAND_MOVE_CARD, "superseded by GET_CARD/LOSE_CARD; no live producer");
    QJsonObject summary;
    summary.insert(QStringLiteral("production_flow_count"), descriptors().size());
    summary.insert(QStringLiteral("typed_registry_flow_count"), descriptors().size());
    summary.insert(QStringLiteral("typed_complete"), typedComplete);
    summary.insert(QStringLiteral("implicit_passthrough"), 0);
    summary.insert(QStringLiteral("unclassified_production_flow"), 0);

    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(QStringLiteral("flow_identity"),
                QJsonArray({QStringLiteral("message_type"), QStringLiteral("source"),
                            QStringLiteral("destination"), QStringLiteral("command_id")}));
    root.insert(QStringLiteral("summary"), summary);
    root.insert(QStringLiteral("flows"), flows);
    root.insert(QStringLiteral("unused_or_retired_commands"), unused);
    return root;
}

QByteArray ProtocolPayloadRegistry::inventoryBytes()
{
    return QJsonDocument(inventoryJson()).toJson(QJsonDocument::Indented);
}

bool ProtocolPayloadRegistry::validateInventory(QString *error)
{
    if (error != nullptr)
        error->clear();
    constexpr int ExpectedProductionFlowCount = 144;
    if (descriptors().size() != ExpectedProductionFlowCount) {
        return fail(error, QStringLiteral("Protocol V2 registry must contain exactly %1 production flows")
            .arg(ExpectedProductionFlowCount));
    }

    QSet<QString> identities;
    for (const ProtocolFlowDescriptor &descriptor : descriptors()) {
        const QString identity = QStringLiteral("%1/%2/%3/%4")
            .arg(messageTypeName(descriptor.key.messageType),
                 endpointName(descriptor.key.source),
                 endpointName(descriptor.key.destination),
                 QString::number(descriptor.key.command));
        if (identities.contains(identity))
            return fail(error, QStringLiteral("Duplicate protocol flow identity: %1").arg(identity));
        identities.insert(identity);
        if (descriptor.targetSchema.isEmpty() || descriptor.parser.isEmpty()
            || descriptor.encoder.isEmpty() || descriptor.requiredFields.isEmpty()
            || descriptor.currentPayloadShape != QLatin1String("typed_object")
            || descriptor.migrationStatus != QLatin1String("complete")
            || descriptor.productionEvidence.size() < 2) {
            return fail(error, QStringLiteral("Incomplete protocol flow descriptor: %1")
                .arg(identity));
        }
    }
    return true;
}
