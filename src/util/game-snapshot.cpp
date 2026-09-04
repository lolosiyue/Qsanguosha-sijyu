#include "game-snapshot.h"

#include "serverplayer.h"
#include "room.h"
#include "engine.h"
#include "wrapped-card.h"
#include "package.h"
#include "settings.h"
#include "game-rng.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMetaType>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace {

QString isoDateWithMilliseconds(const QDateTime &value)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
    return value.toString(Qt::ISODateWithMs);
#else
    const QString seconds = value.toString(Qt::ISODate);
    return seconds.left(19)
        + QStringLiteral(".%1").arg(value.time().msec(), 3, 10, QLatin1Char('0'))
        + seconds.mid(19);
#endif
}

QDateTime fromIsoDateWithMilliseconds(const QString &value)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
    return QDateTime::fromString(value, Qt::ISODateWithMs);
#else
    return QDateTime::fromString(value, Qt::ISODate);
#endif
}

QVariantList intsToVariant(const QList<int> &values)
{
    QVariantList result;
    for (int value : values)
        result << value;
    return result;
}

QList<int> intsFromVariant(const QVariant &value)
{
    QList<int> result;
    for (const QVariant &item : value.toList())
        result << item.toInt();
    return result;
}

QVariantMap intMapToVariant(const QMap<int, int> &values)
{
    QVariantMap result;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        result[QString::number(it.key())] = it.value();
    return result;
}

QMap<int, int> intMapFromVariant(const QVariant &value)
{
    QMap<int, int> result;
    const QVariantMap map = value.toMap();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        result[it.key().toInt()] = it.value().toInt();
    return result;
}

QVariantMap ownerMapToVariant(const QMap<int, QString> &values)
{
    QVariantMap result;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        result[QString::number(it.key())] = it.value();
    return result;
}

QMap<int, QString> ownerMapFromVariant(const QVariant &value)
{
    QMap<int, QString> result;
    const QVariantMap map = value.toMap();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        result[it.key().toInt()] = it.value().toString();
    return result;
}

// QJsonValue::fromVariant turns unknown metatypes into null in some Qt
// versions. Whitelist the JSON types first so a CardUseStruct cannot be
// silently erased from a supposedly lossless snapshot.
bool isJsonSafe(const QVariant &value, const QString &path, QString *error)
{
    if (!value.isValid() || value.isNull())
        return true;

    switch (value.userType()) {
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::QString:
        return true;
    case QMetaType::LongLong: {
        constexpr qlonglong JsonExactIntegerLimit = 9007199254740991LL;
        const qlonglong number = value.toLongLong();
        if (number >= -JsonExactIntegerLimit && number <= JsonExactIntegerLimit)
            return true;
        break;
    }
    case QMetaType::ULongLong:
        if (value.toULongLong() <= 9007199254740991ULL)
            return true;
        break;
    case QMetaType::Double:
        if (std::isfinite(value.toDouble()))
            return true;
        break;
    case QMetaType::QStringList: {
        const QStringList list = value.toStringList();
        for (int i = 0; i < list.size(); ++i) {
            if (!isJsonSafe(QVariant(list.at(i)), path + QStringLiteral("[%1]").arg(i), error))
                return false;
        }
        return true;
    }
    case QMetaType::QVariantList: {
        const QVariantList list = value.toList();
        for (int i = 0; i < list.size(); ++i) {
            if (!isJsonSafe(list.at(i), path + QStringLiteral("[%1]").arg(i), error))
                return false;
        }
        return true;
    }
    case QMetaType::QVariantMap: {
        const QVariantMap map = value.toMap();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            if (!isJsonSafe(it.value(), path + QStringLiteral(".") + it.key(), error))
                return false;
        }
        return true;
    }
    case QMetaType::QJsonValue:
    case QMetaType::QJsonObject:
    case QMetaType::QJsonArray:
        return true;
    default:
        break;
    }
    if (error)
        *error = QStringLiteral("unsupported or lossy JSON value at %1 (%2)")
            .arg(path, QString::fromLatin1(value.typeName()));
    return false;
}

bool checkMap(const QVariantMap &map, const QString &path, QString *error)
{
    return isJsonSafe(map, path, error);
}

// tag 裡面嘅 ServerPlayer* 換成 {"__player": "<objectName>"}。指標唔係 JSON 值,
// 但佢指嘅玩家喺 snapshot 內部已經有名, 所以呢個轉換無損; restore 由
// takeover-scenario.cpp 按名解返 runtime 指標。其他指標(例如 Card*)唔會被換走,
// 仍然會令 snapshot ineligible。
QVariant normalizePlayerRefs(const QVariant &value)
{
    switch (value.userType()) {
    case QMetaType::QVariantList: {
        QVariantList list = value.toList();
        for (QVariant &item : list)
            item = normalizePlayerRefs(item);
        return list;
    }
    case QMetaType::QVariantMap: {
        QVariantMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            it.value() = normalizePlayerRefs(it.value());
        return map;
    }
    default:
        break;
    }
    if (!value.isValid() || value.isNull())
        return value;
    if (!value.metaType().flags().testFlag(QMetaType::PointerToQObject))
        return value;
    const Player *player = qobject_cast<const Player *>(value.value<QObject *>());
    if (!player)
        return value;
    return QVariantMap{{QString::fromLatin1(GameSnapshotTags::PlayerRefKey),
                        player->objectName()}};
}

QVariantMap normalizeTagMap(const QVariantMap &tags)
{
    return normalizePlayerRefs(QVariant(tags)).toMap();
}

// 只可以放「server 側短暫持有一張自己 clone 出嚟嘅 Card」嘅 tag。呢類 tag 過唔到
// JSON 邊界, 而且就算勉強寫低一個替代值, restore 之後讀取端(value<const Card*>)
// 一樣攞到 nullptr, 所以捕捉時直接略過, 由遊戲流程喺下一次事件重建。
// 其他未知嘅非 JSON 值仍然要令 snapshot ineligible —— 唔好用呢個名單去掩蓋
// 新出現嘅狀態損失。
const QStringList &volatilePlayerTags()
{
    static const QStringList names{QStringLiteral("ComboMovesCard")};
    return names;
}

bool isJsonInteger(const QJsonValue &value)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    return std::isfinite(number) && std::floor(number) == number;
}

bool requireJsonType(const QJsonObject &object, const QString &key,
                     QJsonValue::Type type, const QString &path,
                     QString *error)
{
    const QJsonValue value = object.value(key);
    if (value.type() == type)
        return true;
    if (error)
        *error = QStringLiteral("snapshot JSON field %1.%2 has the wrong type")
            .arg(path, key);
    return false;
}

bool requireJsonInteger(const QJsonObject &object, const QString &key,
                        const QString &path, QString *error)
{
    if (isJsonInteger(object.value(key)))
        return true;
    if (error)
        *error = QStringLiteral("snapshot JSON field %1.%2 is not an integer")
            .arg(path, key);
    return false;
}

bool validateTypedArray(const QJsonArray &array, QJsonValue::Type type,
                        bool integerValues, const QString &path, QString *error)
{
    for (qsizetype i = 0; i < array.size(); ++i) {
        const QJsonValue value = array.at(i);
        if ((integerValues && !isJsonInteger(value))
            || (!integerValues && value.type() != type)) {
            if (error)
                *error = QStringLiteral("snapshot JSON array %1[%2] has the wrong type")
                    .arg(path).arg(i);
            return false;
        }
    }
    return true;
}

bool validateIntegerObject(const QJsonObject &object, const QString &path,
                           QString *error)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!isJsonInteger(it.value())) {
            if (error)
                *error = QStringLiteral("snapshot JSON map %1.%2 is not integer-valued")
                    .arg(path, it.key());
            return false;
        }
    }
    return true;
}

bool validateCardLedgerObject(const QJsonObject &object, QJsonValue::Type valueType,
                              bool integerValues, const QString &path,
                              QString *error)
{
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        bool idOk = false;
        const int id = it.key().toInt(&idOk);
        if (!idOk || id < 0
            || (integerValues && !isJsonInteger(it.value()))
            || (!integerValues && it.value().type() != valueType)) {
            if (error)
                *error = QStringLiteral("snapshot JSON ledger %1.%2 is invalid")
                    .arg(path, it.key());
            return false;
        }
    }
    return true;
}

bool validateSkillInstanceJson(const QJsonObject &object, const QString &path,
                               QString *error)
{
    static const QStringList strings = {
        QStringLiteral("skillName"), QStringLiteral("parentSkillName"),
        QStringLiteral("parentRefOwner"), QStringLiteral("parentRefSkillName")
    };
    static const QStringList integers = {
        QStringLiteral("instanceID"), QStringLiteral("source"),
        QStringLiteral("parentInstanceID"), QStringLiteral("parentRefInstanceID"),
        QStringLiteral("amountOverride"), QStringLiteral("bindHead")
    };
    for (const QString &key : strings)
        if (!requireJsonType(object, key, QJsonValue::String, path, error)) return false;
    for (const QString &key : integers)
        if (!requireJsonInteger(object, key, path, error)) return false;
    if (!requireJsonType(object, QStringLiteral("visible"), QJsonValue::Bool, path, error)
        || !requireJsonType(object, QStringLiteral("hasAmountOverride"), QJsonValue::Bool, path, error)
        || !requireJsonType(object, QStringLiteral("state"), QJsonValue::Object, path, error)
        || !requireJsonType(object, QStringLiteral("correctState"), QJsonValue::Object, path, error))
        return false;
    return true;
}

bool validateCardJson(const QJsonObject &object, const QString &path,
                      QString *error)
{
    static const QStringList strings = {
        QStringLiteral("objectName"), QStringLiteral("className"),
        QStringLiteral("suit"), QStringLiteral("skillName"),
        QStringLiteral("sourceSkillName"), QStringLiteral("activationSkillName")
    };
    static const QStringList integers = {
        QStringLiteral("id"), QStringLiteral("suitId"), QStringLiteral("number"),
        QStringLiteral("skillInstanceId"), QStringLiteral("sourceSkillInstanceId"),
        QStringLiteral("activationSkillInstanceId")
    };
    for (const QString &key : strings)
        if (!requireJsonType(object, key, QJsonValue::String, path, error)) return false;
    for (const QString &key : integers)
        if (!requireJsonInteger(object, key, path, error)) return false;
    if (!requireJsonType(object, QStringLiteral("modified"), QJsonValue::Bool, path, error)
        || !requireJsonType(object, QStringLiteral("flags"), QJsonValue::Array, path, error)
        || !requireJsonType(object, QStringLiteral("marks"), QJsonValue::Object, path, error)
        || !requireJsonType(object, QStringLiteral("tags"), QJsonValue::Object, path, error))
        return false;
    return validateTypedArray(object.value(QStringLiteral("flags")).toArray(),
                              QJsonValue::String, false, path + QStringLiteral(".flags"), error)
        && validateIntegerObject(object.value(QStringLiteral("marks")).toObject(),
                                 path + QStringLiteral(".marks"), error);
}

bool validatePlayerJson(const QJsonObject &object, const QString &path,
                        QString *error)
{
    static const QStringList strings = {
        QStringLiteral("objectName"), QStringLiteral("screenName"),
        QStringLiteral("general"), QStringLiteral("general2"),
        QStringLiteral("kingdom"), QStringLiteral("role"),
        QStringLiteral("gender"), QStringLiteral("state")
    };
    static const QStringList integers = {
        QStringLiteral("hp"), QStringLiteral("maxhp"),
        QStringLiteral("seat"), QStringLiteral("playerSeat")
    };
    static const QStringList booleans = {
        QStringLiteral("alive"), QStringLiteral("faceup"),
        QStringLiteral("chained"), QStringLiteral("owner"),
        QStringLiteral("roleShown"), QStringLiteral("generalShowed"),
        QStringLiteral("general2Showed")
    };
    static const QStringList integerArrays = {
        QStringLiteral("handcards"), QStringLiteral("equips"),
        QStringLiteral("judgingArea")
    };
    for (const QString &key : strings)
        if (!requireJsonType(object, key, QJsonValue::String, path, error)) return false;
    for (const QString &key : integers)
        if (!requireJsonInteger(object, key, path, error)) return false;
    for (const QString &key : booleans)
        if (!requireJsonType(object, key, QJsonValue::Bool, path, error)) return false;
    for (const QString &key : integerArrays) {
        if (!requireJsonType(object, key, QJsonValue::Array, path, error)
            || !validateTypedArray(object.value(key).toArray(), QJsonValue::Double, true,
                                   path + QLatin1Char('.') + key, error))
            return false;
    }
    static const QStringList stringArrays = {
        QStringLiteral("flags"), QStringLiteral("skills")
    };
    for (const QString &key : stringArrays) {
        if (!requireJsonType(object, key, QJsonValue::Array, path, error)
            || !validateTypedArray(object.value(key).toArray(), QJsonValue::String, false,
                                   path + QLatin1Char('.') + key, error))
            return false;
    }
    static const QStringList objects = {
        QStringLiteral("piles"), QStringLiteral("marks"),
        QStringLiteral("history"), QStringLiteral("equipAreas"),
        QStringLiteral("dynamicProperties"), QStringLiteral("tags")
    };
    for (const QString &key : objects)
        if (!requireJsonType(object, key, QJsonValue::Object, path, error)) return false;
    if (!validateIntegerObject(object.value(QStringLiteral("marks")).toObject(), path + QStringLiteral(".marks"), error)
        || !validateIntegerObject(object.value(QStringLiteral("history")).toObject(), path + QStringLiteral(".history"), error)
        || !validateIntegerObject(object.value(QStringLiteral("equipAreas")).toObject(), path + QStringLiteral(".equipAreas"), error))
        return false;
    const QJsonObject piles = object.value(QStringLiteral("piles")).toObject();
    for (auto it = piles.constBegin(); it != piles.constEnd(); ++it) {
        if (!it.value().isArray()
            || !validateTypedArray(it.value().toArray(), QJsonValue::Double, true,
                                   path + QStringLiteral(".piles.") + it.key(), error))
            return false;
    }
    if (!requireJsonType(object, QStringLiteral("skillInstances"), QJsonValue::Array, path, error))
        return false;
    const QJsonArray instances = object.value(QStringLiteral("skillInstances")).toArray();
    for (qsizetype i = 0; i < instances.size(); ++i) {
        if (!instances.at(i).isObject()
            || !validateSkillInstanceJson(instances.at(i).toObject(),
                path + QStringLiteral(".skillInstances[%1]").arg(i), error))
            return false;
    }
    return true;
}

bool validateSnapshotStateJson(const QJsonObject &state, QString *error)
{
    static const QStringList strings = {
        QStringLiteral("turnSerial"), QStringLiteral("currentPlayer"),
        QStringLiteral("currentPhase"), QStringLiteral("gameMode"),
        QStringLiteral("ineligibleReason")
    };
    static const QStringList integers = {
        QStringLiteral("stateVersion"), QStringLiteral("turnCount"),
        QStringLiteral("roundCount")
    };
    static const QStringList arrays = {
        QStringLiteral("packages"), QStringLiteral("players"),
        QStringLiteral("seatOrder"), QStringLiteral("cards"),
        QStringLiteral("drawPile"), QStringLiteral("discardPile"),
        QStringLiteral("chatHistory"), QStringLiteral("pendingExtraTurns"),
        QStringLiteral("unsupportedState")
    };
    static const QStringList objects = {
        QStringLiteral("cardPlaces"), QStringLiteral("cardOwners"),
        QStringLiteral("roomTags"), QStringLiteral("catalogFingerprint"),
        QStringLiteral("configFingerprint"), QStringLiteral("gameplayRng"),
        QStringLiteral("aiRng"), QStringLiteral("luaTakeoverState")
    };
    const QString path = QStringLiteral("state");
    for (const QString &key : strings)
        if (!requireJsonType(state, key, QJsonValue::String, path, error)) return false;
    for (const QString &key : integers)
        if (!requireJsonInteger(state, key, path, error)) return false;
    for (const QString &key : arrays)
        if (!requireJsonType(state, key, QJsonValue::Array, path, error)) return false;
    for (const QString &key : objects)
        if (!requireJsonType(state, key, QJsonValue::Object, path, error)) return false;
    if (!requireJsonType(state, QStringLiteral("eligible"), QJsonValue::Bool, path, error))
        return false;

    for (const QString &key : {QStringLiteral("packages"), QStringLiteral("seatOrder"),
                               QStringLiteral("chatHistory"), QStringLiteral("unsupportedState")}) {
        if (!validateTypedArray(state.value(key).toArray(), QJsonValue::String, false,
                                path + QLatin1Char('.') + key, error))
            return false;
    }
    for (const QString &key : {QStringLiteral("drawPile"), QStringLiteral("discardPile")}) {
        if (!validateTypedArray(state.value(key).toArray(), QJsonValue::Double, true,
                                path + QLatin1Char('.') + key, error))
            return false;
    }
    if (!validateCardLedgerObject(state.value(QStringLiteral("cardPlaces")).toObject(),
                                  QJsonValue::Double, true,
                                  QStringLiteral("state.cardPlaces"), error))
        return false;
    const QJsonObject owners = state.value(QStringLiteral("cardOwners")).toObject();
    if (!validateCardLedgerObject(owners, QJsonValue::String, false,
                                  QStringLiteral("state.cardOwners"), error))
        return false;
    for (const QString &rngKey : {QStringLiteral("gameplayRng"), QStringLiteral("aiRng")}) {
        const QJsonObject rng = state.value(rngKey).toObject();
        for (const QString &key : {QStringLiteral("algorithm"), QStringLiteral("seed"),
                                   QStringLiteral("drawCount")}) {
            if (!requireJsonType(rng, key, QJsonValue::String,
                                 path + QLatin1Char('.') + rngKey, error))
                return false;
        }
    }
    const QJsonArray players = state.value(QStringLiteral("players")).toArray();
    for (qsizetype i = 0; i < players.size(); ++i) {
        if (!players.at(i).isObject()
            || !validatePlayerJson(players.at(i).toObject(),
                QStringLiteral("state.players[%1]").arg(i), error))
            return false;
    }
    const QJsonArray cards = state.value(QStringLiteral("cards")).toArray();
    for (qsizetype i = 0; i < cards.size(); ++i) {
        if (!cards.at(i).isObject()
            || !validateCardJson(cards.at(i).toObject(),
                QStringLiteral("state.cards[%1]").arg(i), error))
            return false;
    }
    return true;
}

void markInvalid(GlobalSnapshot &state, const QString &reason)
{
    state.eligible = false;
    if (state.ineligibleReason.isEmpty())
        state.ineligibleReason = reason;
    if (!state.unsupportedState.contains(reason))
        state.unsupportedState << reason;
}

QVariantMap buildCatalogFingerprint()
{
    QVariantMap result;
    QStringList packageNames;
    for (const Package *package : Sanguosha->getPackages()) {
        if (package)
            packageNames << package->objectName();
    }
    std::sort(packageNames.begin(), packageNames.end());
    QByteArray catalogData;
    catalogData.append(Sanguosha->getVersion().toUtf8()).append('|')
        .append(Sanguosha->getMODName().toUtf8()).append('|')
        .append(QByteArray::number(Sanguosha->getCardCount()));
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (!card) {
            catalogData.append("|missing:").append(QByteArray::number(id));
            continue;
        }
        catalogData.append('|').append(QByteArray::number(id)).append(':')
            .append(card->objectName().toUtf8()).append(':')
            .append(card->getClassName().toUtf8()).append(':')
            .append(card->getPackage().toUtf8());
    }
    result[QStringLiteral("cardCount")] = Sanguosha->getCardCount();
    result[QStringLiteral("sha256")] = QString::fromLatin1(
        QCryptographicHash::hash(catalogData, QCryptographicHash::Sha256).toHex());
    result[QStringLiteral("packages")] = packageNames;
    result[QStringLiteral("gameVersion")] = Sanguosha->getVersion();
    result[QStringLiteral("modName")] = Sanguosha->getMODName();
    return result;
}

QVariantMap buildConfigFingerprint(const QString &gameMode)
{
    QVariantMap result;
    result[QStringLiteral("gameMode")] = gameMode.isEmpty() ? Config.GameMode.mode_id : gameMode;
    result[QStringLiteral("enabledPackages")] = Config.EnabledPackages;
    result[QStringLiteral("bannedPackages")] = Config.BanPackages;
    result[QStringLiteral("randomSeat")] = Config.RandomSeat;
    result[QStringLiteral("freeChoose")] = Config.FreeChoose;
    result[QStringLiteral("freeAssignSelf")] = Config.FreeAssignSelf;
    result[QStringLiteral("enableSecondGeneral")] = Config.Enable2ndGeneral;
    result[QStringLiteral("enableSame")] = Config.EnableSame;
    result[QStringLiteral("enableBasara")] = Config.EnableBasara;
    result[QStringLiteral("enableHegemony")] = Config.EnableHegemony;
    result[QStringLiteral("enableMeleeMode")] = Config.EnableMeleeMode;
    result[QStringLiteral("maxHpScheme")] = Config.MaxHpScheme;
    result[QStringLiteral("scheme0Subtraction")] = Config.Scheme0Subtraction;
    result[QStringLiteral("enableAI")] = Config.EnableAI;
    result[QStringLiteral("enableLuckCard")] = Config.EnableLuckCard;
    result[QStringLiteral("forbidSIMC")] = Config.ForbidSIMC;
    result[QStringLiteral("disableLua")] = Config.DisableLua;
    return result;
}

QByteArray canonicalJson(const QVariantMap &map)
{
    return QJsonDocument::fromVariant(map).toJson(QJsonDocument::Compact);
}

bool validateState(const GlobalSnapshot &state, QString *error)
{
    if (!state.eligible || !state.unsupportedState.isEmpty())
        return error ? (*error = state.ineligibleReason.isEmpty()
            ? QStringLiteral("snapshot is marked ineligible") : state.ineligibleReason, false) : false;
    if (state.turnSerial == 0)
        return error ? (*error = QStringLiteral("turnSerial is zero"), false) : false;
    if (state.gameMode.isEmpty() || state.currentPhase.isEmpty())
        return error ? (*error = QStringLiteral("turn identity is incomplete"), false) : false;
    bool phaseOk = false;
    const int phase = state.currentPhase.toInt(&phaseOk);
    if (!phaseOk || phase != static_cast<int>(Player::NotActive))
        return error ? (*error = QStringLiteral("snapshot is not at a top-level turn boundary"), false) : false;
    auto validRng = [](const RngSnapshot &rng) {
        bool algorithmOk = false, seedOk = false, countOk = false;
        const quint32 algorithm = rng.algorithm.toUInt(&algorithmOk);
        rng.seed.toULongLong(&seedOk);
        rng.drawCount.toULongLong(&countOk);
        return algorithmOk && seedOk && countOk
            && algorithm == GameRng::AlgorithmQsanRejectionV1;
    };
    if (!validRng(state.gameplayRng) || !validRng(state.aiRng))
        return error ? (*error = QStringLiteral("RNG state is missing or unsupported"), false) : false;
    if (state.catalogFingerprint.isEmpty() || state.configFingerprint.isEmpty())
        return error ? (*error = QStringLiteral("runtime fingerprint is missing"), false) : false;
    if (!state.configFingerprint.value(QStringLiteral("enableAI")).toBool()
        || state.configFingerprint.value(QStringLiteral("disableLua")).toBool()) {
        return error ? (*error = QStringLiteral(
            "takeover requires an enabled SmartAI Lua runtime"), false) : false;
    }
    if (state.players.isEmpty() || state.seatOrder.size() != state.players.size())
        return error ? (*error = QStringLiteral("player roster/seatOrder mismatch"), false) : false;
    QSet<QString> playerNames;
    QSet<int> physicalSeats;
    bool currentPlayerAlive = false;
    for (const PlayerSnapshot &player : state.players) {
        if (player.objectName.isEmpty() || playerNames.contains(player.objectName))
            return error ? (*error = QStringLiteral("duplicate or empty player objectName"), false) : false;
        playerNames.insert(player.objectName);
        if (player.objectName == state.currentPlayer)
            currentPlayerAlive = player.alive;
        if (player.playerSeat <= 0 || physicalSeats.contains(player.playerSeat))
            return error ? (*error = QStringLiteral("duplicate or invalid physical seat"), false) : false;
        physicalSeats.insert(player.playerSeat);
    }
    QSet<QString> orderedNames;
    for (const QString &name : state.seatOrder) {
        if (!playerNames.contains(name) || orderedNames.contains(name))
            return error ? (*error = QStringLiteral("seatOrder references unknown player"), false) : false;
        orderedNames.insert(name);
    }
    if (!playerNames.contains(state.currentPlayer))
        return error ? (*error = QStringLiteral("currentPlayer is not in roster"), false) : false;
    if (!currentPlayerAlive)
        return error ? (*error = QStringLiteral("currentPlayer is not alive"), false) : false;

    bool cardCountOk = false;
    const int expectedCardCount = state.catalogFingerprint.value(
        QStringLiteral("cardCount")).toInt(&cardCountOk);
    if (state.cards.isEmpty() || !cardCountOk
        || expectedCardCount != state.cards.size()
        || state.cardPlaces.size() != state.cards.size()
        || state.cardOwners.size() != state.cards.size())
        return error ? (*error = QStringLiteral("card catalog/ledger is incomplete"), false) : false;
    QSet<int> cardIds;
    for (const CardSnapshot &card : state.cards) {
        if (card.id < 0 || card.objectName.isEmpty() || card.className.isEmpty()
            || cardIds.contains(card.id))
            return error ? (*error = QStringLiteral("duplicate or invalid physical card id"), false) : false;
        cardIds.insert(card.id);
        if (!state.cardPlaces.contains(card.id))
            return error ? (*error = QStringLiteral("physical card has no place"), false) : false;
    }
    for (int id = 0; id < expectedCardCount; ++id) {
        if (!cardIds.contains(id))
            return error ? (*error = QStringLiteral("physical card id sequence is incomplete"), false) : false;
    }
    QMap<int, int> occurrences;
    QMap<int, int> expectedPlaces;
    QMap<int, QString> expectedOwners;
    for (int id : state.drawPile) {
        ++occurrences[id];
        expectedPlaces[id] = static_cast<int>(Player::DrawPile);
    }
    for (int id : state.discardPile) {
        ++occurrences[id];
        expectedPlaces[id] = static_cast<int>(Player::DiscardPile);
    }
    for (const PlayerSnapshot &player : state.players) {
        for (int id : player.handcards) {
            ++occurrences[id]; expectedPlaces[id] = static_cast<int>(Player::PlaceHand);
            expectedOwners[id] = player.objectName;
        }
        for (int id : player.equips) {
            ++occurrences[id]; expectedPlaces[id] = static_cast<int>(Player::PlaceEquip);
            expectedOwners[id] = player.objectName;
        }
        for (int id : player.judgingArea) {
            ++occurrences[id]; expectedPlaces[id] = static_cast<int>(Player::PlaceDelayedTrick);
            expectedOwners[id] = player.objectName;
        }
        for (auto it = player.piles.constBegin(); it != player.piles.constEnd(); ++it)
            for (int id : it.value()) {
                ++occurrences[id]; expectedPlaces[id] = static_cast<int>(Player::PlaceSpecial);
                expectedOwners[id] = player.objectName;
            }
    }
    for (auto it = occurrences.constBegin(); it != occurrences.constEnd(); ++it) {
        if (!cardIds.contains(it.key()))
            return error ? (*error = QStringLiteral("zone references unknown card id %1").arg(it.key()), false) : false;
    }
    for (auto it = state.cardPlaces.constBegin(); it != state.cardPlaces.constEnd(); ++it) {
        if (!cardIds.contains(it.key()))
            return error ? (*error = QStringLiteral("place ledger references unknown card id %1").arg(it.key()), false) : false;
    }
    for (auto it = state.cardOwners.constBegin(); it != state.cardOwners.constEnd(); ++it) {
        if (!cardIds.contains(it.key()))
            return error ? (*error = QStringLiteral("owner ledger references unknown card id %1").arg(it.key()), false) : false;
        if (!it.value().isEmpty() && !playerNames.contains(it.value()))
            return error ? (*error = QStringLiteral("owner ledger references unknown player"), false) : false;
    }
    for (int id : cardIds) {
        const int place = state.cardPlaces.value(id, static_cast<int>(Player::PlaceUnknown));
        const int count = occurrences.value(id);
        const QString owner = state.cardOwners.value(id);
        // Derivative/package placeholder cards remain in room-level zones
        // (normally PlaceTable) without belonging to a linear pile.
        if (place == static_cast<int>(Player::PlaceTable)
            || place == static_cast<int>(Player::PlaceWuGu)) {
            if (count == 0 && owner.isEmpty())
                continue;
            return error ? (*error = QStringLiteral("physical card %1 has an inconsistent room-level state").arg(id), false) : false;
        }
        if (place == static_cast<int>(Player::PlaceUnknown))
            return error ? (*error = QStringLiteral("physical card %1 is outside a restorable zone").arg(id), false) : false;
        if (count != 1)
            return error ? (*error = QStringLiteral("card ledger occurrence mismatch for id %1").arg(id), false) : false;
        if (state.cardPlaces.value(id) != expectedPlaces.value(id))
            return error ? (*error = QStringLiteral("card ledger place mismatch for id %1").arg(id), false) : false;
        if (state.cardOwners.value(id) != expectedOwners.value(id))
            return error ? (*error = QStringLiteral("card ledger owner mismatch for id %1").arg(id), false) : false;
    }
    return true;
}

bool capturePlayer(ServerPlayer *player, PlayerSnapshot *snapshot, QString *error)
{
    if (!player || !snapshot) {
        if (error) *error = QStringLiteral("null player");
        return false;
    }
    snapshot->objectName = player->objectName();
    snapshot->screenName = player->screenName();
    snapshot->general = player->getGeneralName();
    snapshot->general2 = player->getGeneral2Name();
    snapshot->kingdom = player->getKingdom();
    snapshot->role = player->getRole();
    snapshot->hp = player->getHp();
    snapshot->maxhp = player->getMaxHp();
    snapshot->seat = player->getSeat();
    snapshot->playerSeat = player->getPlayerSeat();
    snapshot->alive = player->isAlive();
    snapshot->faceup = player->faceUp();
    snapshot->chained = player->isChained();
    snapshot->owner = player->isOwner();
    snapshot->roleShown = player->hasShownRole();
    snapshot->generalShowed = player->hasShownGeneral();
    snapshot->general2Showed = player->hasShownGeneral2();
    snapshot->gender = QString::number(static_cast<int>(player->getGender()));
    snapshot->state = player->getState();
    snapshot->handcards = player->handCards();
    snapshot->equips = player->getEquipsId();
    snapshot->judgingArea = player->getJudgingAreaID();

    for (const QString &pileName : player->getPileNames())
        snapshot->piles[pileName] = player->getPile(pileName);
    for (const QString &markName : player->getMarkNames()) {
        const int value = player->getMark(markName);
        if (value != 0)
            snapshot->marks[markName] = value;
    }
    snapshot->flags = player->getFlagList();
    for (const Skill *skill : player->getVisibleSkillList(true))
        snapshot->skills << skill->objectName();
    for (auto it = player->getHistory().constBegin(); it != player->getHistory().constEnd(); ++it)
        snapshot->history[it.key()] = it.value();
    for (int i = 0; i < 5; ++i)
        snapshot->equipAreas[i] = player->getEquipArea(i);

    // Preserve both QObject properties and the explicit Player tag map. Any
    // non-JSON value makes this boundary ineligible instead of being dropped.
    for (const QByteArray &name : player->dynamicPropertyNames()) {
        const QString key = QString::fromUtf8(name);
        const QVariant value = player->property(name.constData());
        if (!isJsonSafe(value, QStringLiteral("player.%1").arg(key), error))
            return false;
        snapshot->dynamicProperties[key] = value;
    }
    snapshot->tags = player->getAllTags();
    // GameRule 喺每次 CardUsed 都會寫 ComboMovesCard (一張 CardTagOwner 持有嘅
    // clone), 即第一回合之後基本必然存在。唔剔走佢, 每一個 turn snapshot 都會
    // 因為呢一個 tag 變 ineligible, takeover/replay 就淨返第一個節點。
    for (const QString &volatileTag : volatilePlayerTags())
        snapshot->tags.remove(volatileTag);
    snapshot->tags = normalizeTagMap(snapshot->tags);
    if (!checkMap(snapshot->tags, QStringLiteral("player.%1.tags").arg(snapshot->objectName), error))
        return false;

    for (const SkillInstance &instance : player->getSkillInstances()) {
        SkillInstanceSnapshot item;
        item.skillName = instance.skillName;
        item.instanceID = instance.instanceID;
        item.source = static_cast<int>(instance.source);
        item.parentSkillName = instance.parent.skillName;
        item.parentInstanceID = instance.parent.instanceID;
        item.parentRefOwner = instance.parentRef.ownerObjectName;
        item.parentRefSkillName = instance.parentRef.key.skillName;
        item.parentRefInstanceID = instance.parentRef.key.instanceID;
        item.visible = instance.visible;
        item.hasAmountOverride = instance.hasAmountOverride;
        item.amountOverride = instance.amountOverride;
        item.bindHead = instance.bindHead;
        item.state = player->getSkillInstanceState(instance.skillName, instance.instanceID);
        item.correctState = player->getSkillInstanceCorrectState(instance.skillName, instance.instanceID);
        if (!checkMap(item.state, QStringLiteral("skillInstance.state"), error)
            || !checkMap(item.correctState, QStringLiteral("skillInstance.correctState"), error))
            return false;
        snapshot->skillInstances << item;
    }
    return true;
}

bool captureCard(Card *card, CardSnapshot *snapshot, QString *error)
{
    if (!card || !snapshot) {
        if (error) *error = QStringLiteral("null card");
        return false;
    }
    snapshot->id = card->getId();
    snapshot->objectName = card->objectName();
    snapshot->className = card->getRealCard()->getClassName();
    snapshot->suit = card->getSuitString();
    snapshot->suitId = static_cast<int>(card->getSuit());
    snapshot->number = card->getNumber();
    snapshot->skillName = card->getSkillName(false);
    snapshot->skillInstanceId = card->getSkillInstanceId();
    snapshot->sourceSkillName = card->getSourceSkillName();
    snapshot->sourceSkillInstanceId = card->getSourceSkillInstanceId();
    snapshot->activationSkillName = card->getActivationSkillName();
    snapshot->activationSkillInstanceId = card->getActivationSkillInstanceId();
    snapshot->modified = card->isModified();
    snapshot->flags = card->getFlags();
    for (const QString &mark : card->getMarkNames())
        snapshot->marks[mark] = card->getMark(mark);
    snapshot->tags = card->tag;
    const bool safe = checkMap(snapshot->tags, QStringLiteral("card.%1.tags").arg(snapshot->id), error);
    if (!safe)
        snapshot->tags = QJsonValue::fromVariant(snapshot->tags).toObject().toVariantMap();
    return safe;
}

}

QVariantMap RngSnapshot::serialize() const
{
    return {{QStringLiteral("algorithm"), algorithm}, {QStringLiteral("seed"), seed},
            {QStringLiteral("drawCount"), drawCount}};
}

RngSnapshot RngSnapshot::deserialize(const QVariantMap &map)
{
    RngSnapshot result;
    result.algorithm = map.value(QStringLiteral("algorithm")).toString();
    result.seed = map.value(QStringLiteral("seed")).toString();
    result.drawCount = map.value(QStringLiteral("drawCount")).toString();
    return result;
}

QVariantMap SkillInstanceSnapshot::serialize() const
{
    return {{QStringLiteral("skillName"), skillName}, {QStringLiteral("instanceID"), instanceID},
            {QStringLiteral("source"), source}, {QStringLiteral("parentSkillName"), parentSkillName},
            {QStringLiteral("parentInstanceID"), parentInstanceID}, {QStringLiteral("parentRefOwner"), parentRefOwner},
            {QStringLiteral("parentRefSkillName"), parentRefSkillName},
            {QStringLiteral("parentRefInstanceID"), parentRefInstanceID}, {QStringLiteral("visible"), visible},
            {QStringLiteral("hasAmountOverride"), hasAmountOverride}, {QStringLiteral("amountOverride"), amountOverride},
            {QStringLiteral("bindHead"), bindHead}, {QStringLiteral("state"), state},
            {QStringLiteral("correctState"), correctState}};
}

SkillInstanceSnapshot SkillInstanceSnapshot::deserialize(const QVariantMap &map)
{
    SkillInstanceSnapshot result;
    result.skillName = map.value(QStringLiteral("skillName")).toString();
    result.instanceID = map.value(QStringLiteral("instanceID")).toInt();
    result.source = map.value(QStringLiteral("source")).toInt();
    result.parentSkillName = map.value(QStringLiteral("parentSkillName")).toString();
    result.parentInstanceID = map.value(QStringLiteral("parentInstanceID")).toInt();
    result.parentRefOwner = map.value(QStringLiteral("parentRefOwner")).toString();
    result.parentRefSkillName = map.value(QStringLiteral("parentRefSkillName")).toString();
    result.parentRefInstanceID = map.value(QStringLiteral("parentRefInstanceID")).toInt();
    result.visible = map.value(QStringLiteral("visible")).toBool();
    result.hasAmountOverride = map.value(QStringLiteral("hasAmountOverride")).toBool();
    result.amountOverride = map.value(QStringLiteral("amountOverride")).toInt();
    result.bindHead = map.value(QStringLiteral("bindHead")).toInt();
    result.state = map.value(QStringLiteral("state")).toMap();
    result.correctState = map.value(QStringLiteral("correctState")).toMap();
    return result;
}

QVariantMap CardSnapshot::serialize() const
{
    QVariantMap result;
    result[QStringLiteral("id")] = id;
    result[QStringLiteral("objectName")] = objectName;
    result[QStringLiteral("className")] = className;
    result[QStringLiteral("suit")] = suit;
    result[QStringLiteral("suitId")] = suitId;
    result[QStringLiteral("number")] = number;
    result[QStringLiteral("skillName")] = skillName;
    result[QStringLiteral("skillInstanceId")] = skillInstanceId;
    result[QStringLiteral("sourceSkillName")] = sourceSkillName;
    result[QStringLiteral("sourceSkillInstanceId")] = sourceSkillInstanceId;
    result[QStringLiteral("activationSkillName")] = activationSkillName;
    result[QStringLiteral("activationSkillInstanceId")] = activationSkillInstanceId;
    result[QStringLiteral("modified")] = modified;
    result[QStringLiteral("flags")] = flags;
    result[QStringLiteral("marks")] = QVariantMap();
    QVariantMap marksMap;
    for (auto it = marks.constBegin(); it != marks.constEnd(); ++it)
        marksMap[it.key()] = it.value();
    result[QStringLiteral("marks")] = marksMap;
    result[QStringLiteral("tags")] = tags;
    return result;
}

CardSnapshot CardSnapshot::deserialize(const QVariantMap &map)
{
    CardSnapshot result;
    result.id = map.value(QStringLiteral("id")).toInt();
    result.objectName = map.value(QStringLiteral("objectName")).toString();
    result.className = map.value(QStringLiteral("className")).toString();
    result.suit = map.value(QStringLiteral("suit")).toString();
    result.suitId = map.value(QStringLiteral("suitId")).toInt();
    result.number = map.value(QStringLiteral("number")).toInt();
    result.skillName = map.value(QStringLiteral("skillName")).toString();
    result.skillInstanceId = map.value(QStringLiteral("skillInstanceId")).toInt();
    result.sourceSkillName = map.value(QStringLiteral("sourceSkillName")).toString();
    result.sourceSkillInstanceId = map.value(QStringLiteral("sourceSkillInstanceId")).toInt();
    result.activationSkillName = map.value(QStringLiteral("activationSkillName")).toString();
    result.activationSkillInstanceId = map.value(QStringLiteral("activationSkillInstanceId")).toInt();
    result.modified = map.value(QStringLiteral("modified")).toBool();
    result.flags = map.value(QStringLiteral("flags")).toStringList();
    const QVariantMap marksMap = map.value(QStringLiteral("marks")).toMap();
    for (auto it = marksMap.constBegin(); it != marksMap.constEnd(); ++it)
        result.marks[it.key()] = it.value().toInt();
    result.tags = map.value(QStringLiteral("tags")).toMap();
    return result;
}

QVariantMap PlayerSnapshot::serialize() const
{
    QVariantMap result;
    result[QStringLiteral("objectName")] = objectName;
    result[QStringLiteral("screenName")] = screenName;
    result[QStringLiteral("general")] = general;
    result[QStringLiteral("general2")] = general2;
    result[QStringLiteral("kingdom")] = kingdom;
    result[QStringLiteral("role")] = role;
    result[QStringLiteral("hp")] = hp;
    result[QStringLiteral("maxhp")] = maxhp;
    result[QStringLiteral("seat")] = seat;
    result[QStringLiteral("playerSeat")] = playerSeat;
    result[QStringLiteral("alive")] = alive;
    result[QStringLiteral("faceup")] = faceup;
    result[QStringLiteral("chained")] = chained;
    result[QStringLiteral("owner")] = owner;
    result[QStringLiteral("roleShown")] = roleShown;
    result[QStringLiteral("generalShowed")] = generalShowed;
    result[QStringLiteral("general2Showed")] = general2Showed;
    result[QStringLiteral("gender")] = gender;
    result[QStringLiteral("state")] = state;
    result[QStringLiteral("handcards")] = intsToVariant(handcards);
    result[QStringLiteral("equips")] = intsToVariant(equips);
    result[QStringLiteral("judgingArea")] = intsToVariant(judgingArea);
    QVariantMap pileMap;
    for (auto it = piles.constBegin(); it != piles.constEnd(); ++it)
        pileMap[it.key()] = intsToVariant(it.value());
    result[QStringLiteral("piles")] = pileMap;
    QVariantMap markMap;
    for (auto it = marks.constBegin(); it != marks.constEnd(); ++it)
        markMap[it.key()] = it.value();
    result[QStringLiteral("marks")] = markMap;
    result[QStringLiteral("flags")] = flags;
    result[QStringLiteral("skills")] = skills;
    QVariantMap historyMap;
    for (auto it = history.constBegin(); it != history.constEnd(); ++it)
        historyMap[it.key()] = it.value();
    result[QStringLiteral("history")] = historyMap;
    result[QStringLiteral("equipAreas")] = intMapToVariant(equipAreas);
    result[QStringLiteral("dynamicProperties")] = dynamicProperties;
    result[QStringLiteral("tags")] = tags;
    QVariantList instanceList;
    for (const SkillInstanceSnapshot &instance : skillInstances)
        instanceList << instance.serialize();
    result[QStringLiteral("skillInstances")] = instanceList;
    return result;
}

PlayerSnapshot PlayerSnapshot::deserialize(const QVariantMap &map)
{
    PlayerSnapshot result;
    result.objectName = map.value(QStringLiteral("objectName")).toString();
    result.screenName = map.value(QStringLiteral("screenName")).toString();
    result.general = map.value(QStringLiteral("general")).toString();
    result.general2 = map.value(QStringLiteral("general2")).toString();
    result.kingdom = map.value(QStringLiteral("kingdom")).toString();
    result.role = map.value(QStringLiteral("role")).toString();
    result.hp = map.value(QStringLiteral("hp")).toInt();
    result.maxhp = map.value(QStringLiteral("maxhp")).toInt();
    result.seat = map.value(QStringLiteral("seat")).toInt();
    result.playerSeat = map.value(QStringLiteral("playerSeat")).toInt();
    result.alive = map.value(QStringLiteral("alive")).toBool();
    result.faceup = map.value(QStringLiteral("faceup")).toBool();
    result.chained = map.value(QStringLiteral("chained")).toBool();
    result.owner = map.value(QStringLiteral("owner")).toBool();
    result.roleShown = map.value(QStringLiteral("roleShown")).toBool();
    result.generalShowed = map.value(QStringLiteral("generalShowed")).toBool();
    result.general2Showed = map.value(QStringLiteral("general2Showed")).toBool();
    result.gender = map.value(QStringLiteral("gender")).toString();
    result.state = map.value(QStringLiteral("state")).toString();
    result.handcards = intsFromVariant(map.value(QStringLiteral("handcards")));
    result.equips = intsFromVariant(map.value(QStringLiteral("equips")));
    result.judgingArea = intsFromVariant(map.value(QStringLiteral("judgingArea")));
    const QVariantMap pilesMap = map.value(QStringLiteral("piles")).toMap();
    for (auto it = pilesMap.constBegin(); it != pilesMap.constEnd(); ++it)
        result.piles[it.key()] = intsFromVariant(it.value());
    const QVariantMap marksMap = map.value(QStringLiteral("marks")).toMap();
    for (auto it = marksMap.constBegin(); it != marksMap.constEnd(); ++it)
        result.marks[it.key()] = it.value().toInt();
    result.flags = map.value(QStringLiteral("flags")).toStringList();
    result.skills = map.value(QStringLiteral("skills")).toStringList();
    const QVariantMap historyMap = map.value(QStringLiteral("history")).toMap();
    for (auto it = historyMap.constBegin(); it != historyMap.constEnd(); ++it)
        result.history[it.key()] = it.value().toInt();
    result.equipAreas = intMapFromVariant(map.value(QStringLiteral("equipAreas")));
    result.dynamicProperties = map.value(QStringLiteral("dynamicProperties")).toMap();
    result.tags = map.value(QStringLiteral("tags")).toMap();
    for (const QVariant &value : map.value(QStringLiteral("skillInstances")).toList())
        result.skillInstances << SkillInstanceSnapshot::deserialize(value.toMap());
    return result;
}

PlayerSnapshot PlayerSnapshot::fromPlayer(ServerPlayer *player)
{
    PlayerSnapshot result;
    QString ignored;
    capturePlayer(player, &result, &ignored);
    return result;
}

QVariantMap GlobalSnapshot::serialize() const
{
    QVariantMap result;
    result[QStringLiteral("stateVersion")] = GameSnapshot::TakeoverSchemaVersion;
    result[QStringLiteral("turnCount")] = turnCount;
    result[QStringLiteral("roundCount")] = roundCount;
    result[QStringLiteral("turnSerial")] = QString::number(turnSerial);
    result[QStringLiteral("currentPlayer")] = currentPlayer;
    result[QStringLiteral("currentPhase")] = currentPhase;
    result[QStringLiteral("gameMode")] = gameMode;
    result[QStringLiteral("packages")] = packages;
    result[QStringLiteral("drawPile")] = intsToVariant(drawPile);
    result[QStringLiteral("discardPile")] = intsToVariant(discardPile);
    QVariantList playerList;
    for (const PlayerSnapshot &player : players)
        playerList << player.serialize();
    result[QStringLiteral("players")] = playerList;
    result[QStringLiteral("seatOrder")] = seatOrder;
    QVariantList cardList;
    for (const CardSnapshot &card : cards)
        cardList << card.serialize();
    result[QStringLiteral("cards")] = cardList;
    result[QStringLiteral("cardPlaces")] = intMapToVariant(cardPlaces);
    result[QStringLiteral("cardOwners")] = ownerMapToVariant(cardOwners);
    result[QStringLiteral("roomTags")] = roomTags;
    result[QStringLiteral("chatHistory")] = chatHistory;
    result[QStringLiteral("catalogFingerprint")] = catalogFingerprint;
    result[QStringLiteral("configFingerprint")] = configFingerprint;
    result[QStringLiteral("gameplayRng")] = gameplayRng.serialize();
    result[QStringLiteral("aiRng")] = aiRng.serialize();
    result[QStringLiteral("pendingExtraTurns")] = pendingExtraTurns;
    result[QStringLiteral("luaTakeoverState")] = luaTakeoverState;
    result[QStringLiteral("unsupportedState")] = unsupportedState;
    result[QStringLiteral("eligible")] = eligible;
    result[QStringLiteral("ineligibleReason")] = ineligibleReason;
    return result;
}

GlobalSnapshot GlobalSnapshot::deserialize(const QVariantMap &map)
{
    GlobalSnapshot result;
    result.turnCount = map.value(QStringLiteral("turnCount")).toInt();
    result.roundCount = map.value(QStringLiteral("roundCount")).toInt();
    result.turnSerial = map.value(QStringLiteral("turnSerial")).toString().toULongLong();
    result.currentPlayer = map.value(QStringLiteral("currentPlayer")).toString();
    result.currentPhase = map.value(QStringLiteral("currentPhase")).toString();
    result.gameMode = map.value(QStringLiteral("gameMode")).toString();
    result.packages = map.value(QStringLiteral("packages")).toStringList();
    result.drawPile = intsFromVariant(map.value(QStringLiteral("drawPile")));
    result.discardPile = intsFromVariant(map.value(QStringLiteral("discardPile")));
    for (const QVariant &value : map.value(QStringLiteral("players")).toList())
        result.players << PlayerSnapshot::deserialize(value.toMap());
    result.seatOrder = map.value(QStringLiteral("seatOrder")).toStringList();
    for (const QVariant &value : map.value(QStringLiteral("cards")).toList())
        result.cards << CardSnapshot::deserialize(value.toMap());
    result.cardPlaces = intMapFromVariant(map.value(QStringLiteral("cardPlaces")));
    result.cardOwners = ownerMapFromVariant(map.value(QStringLiteral("cardOwners")));
    result.roomTags = map.value(QStringLiteral("roomTags")).toMap();
    result.chatHistory = map.value(QStringLiteral("chatHistory")).toStringList();
    result.catalogFingerprint = map.value(QStringLiteral("catalogFingerprint")).toMap();
    result.configFingerprint = map.value(QStringLiteral("configFingerprint")).toMap();
    result.gameplayRng = RngSnapshot::deserialize(map.value(QStringLiteral("gameplayRng")).toMap());
    result.aiRng = RngSnapshot::deserialize(map.value(QStringLiteral("aiRng")).toMap());
    result.pendingExtraTurns = map.value(QStringLiteral("pendingExtraTurns")).toList();
    result.luaTakeoverState = map.value(QStringLiteral("luaTakeoverState")).toMap();
    result.unsupportedState = map.value(QStringLiteral("unsupportedState")).toStringList();
    result.eligible = map.value(QStringLiteral("eligible")).toBool();
    result.ineligibleReason = map.value(QStringLiteral("ineligibleReason")).toString();
    return result;
}

QString GameSnapshot::takeoverFormat()
{
    return QStringLiteral("qsanguosha.takeover.snapshot");
}

GameSnapshot::GameSnapshot(QObject *parent)
    : QObject(parent), m_timestamp(QDateTime::currentDateTime())
{
}

GameSnapshot::GameSnapshot(Room *room, QObject *parent)
    : QObject(parent), m_timestamp(QDateTime::currentDateTime())
{
    if (!room)
        return;

    m_state.turnCount = room->getTag(QStringLiteral("TurnLengthCount")).toInt();
    m_state.roundCount = room->getTag(QStringLiteral("Round")).toInt();
    const QVariant serialTag = room->getTag(QStringLiteral("ReplaySnapshotTurnSerial"));
    m_state.turnSerial = serialTag.isValid()
        ? serialTag.toString().toULongLong()
        : static_cast<quint64>(qMax(0, m_state.turnCount));
    if (ServerPlayer *current = room->getCurrent()) {
        m_state.currentPlayer = current->objectName();
        m_state.currentPhase = QString::number(static_cast<int>(current->getPhase()));
    }
    m_state.gameMode = room->getMode();

    QList<ServerPlayer *> players = room->getAllPlayers(true);
    std::sort(players.begin(), players.end(), [](ServerPlayer *a, ServerPlayer *b) {
        return a->getPlayerSeat() < b->getPlayerSeat();
    });
    for (ServerPlayer *player : players) {
        PlayerSnapshot item;
        QString error;
        if (!capturePlayer(player, &item, &error))
            markInvalid(m_state, error);
        m_state.players << item;
        m_state.seatOrder << player->objectName();
    }

    m_state.drawPile = room->getDrawPile();
    m_state.discardPile = room->getDiscardPile();

    const QList<const Package *> packages = Sanguosha->getPackages();
    for (const Package *package : packages) {
        if (package)
            m_state.packages << package->objectName();
    }
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        Card *card = room->getCard(id);
        CardSnapshot item;
        QString error;
        if (!captureCard(card, &item, &error))
            markInvalid(m_state, QStringLiteral("card %1: %2").arg(id).arg(error));
        m_state.cards << item;
        m_state.cardPlaces[id] = static_cast<int>(room->getCardPlace(id));
        m_state.cardOwners[id] = QString();
        if (ServerPlayer *owner = room->getCardOwner(id))
            m_state.cardOwners[id] = owner->objectName();
    }
    m_state.catalogFingerprint = buildCatalogFingerprint();

    // Settings that affect card/general selection or rule execution are part
    // of the preflight fingerprint. Keep this explicit and deterministic;
    // cosmetic client settings do not belong in a gameplay snapshot.
    m_state.configFingerprint = buildConfigFingerprint(room->getMode());

    if (GameRng *rng = GameRng::current()) {
        const GameRng::State rngState = rng->exportState();
        m_state.gameplayRng.algorithm = QString::number(rngState.algorithm);
        m_state.gameplayRng.seed = QString::number(rngState.seed);
        m_state.gameplayRng.drawCount = QString::number(rngState.drawCount);
    }

    const GameRng::State aiRngState = room->roomRuntime()->ai().exportRngState();
    m_state.aiRng.algorithm = QString::number(aiRngState.algorithm);
    m_state.aiRng.seed = QString::number(aiRngState.seed);
    m_state.aiRng.drawCount = QString::number(aiRngState.drawCount);

    QString stateError;
    m_state.pendingExtraTurns = room->snapshotPendingExtraTurns();
    if (!isJsonSafe(m_state.pendingExtraTurns, QStringLiteral("pendingExtraTurns"), &stateError))
        markInvalid(m_state, stateError);

    QString luaError;
    if (!room->luaRuntime()->exportTakeoverState(m_state.luaTakeoverState, &luaError))
        markInvalid(m_state, luaError);

    stateError.clear();
    m_state.roomTags = normalizeTagMap(room->getAllTags());
    m_state.roomTags.remove(QStringLiteral("ReplaySnapshotTurnSerial"));
    const bool safeRoomTags = checkMap(m_state.roomTags, QStringLiteral("roomTags"), &stateError);
    if (!safeRoomTags) {
        markInvalid(m_state, stateError);
        // Keep the in-memory snapshot free of Card-owning metatypes. The
        // node is ineligible, but inspecting it must not extend a Card lease.
        m_state.roomTags = QJsonValue::fromVariant(m_state.roomTags).toObject().toVariantMap();
    }

    // A physical card must occur in exactly one authoritative zone. The
    // place map catches cards in table/special zones that are not represented
    // by a player list; duplicate or missing IDs make takeover ineligible.
    QMap<int, int> occurrences;
    for (int id : m_state.drawPile) ++occurrences[id];
    for (int id : m_state.discardPile) ++occurrences[id];
    for (const PlayerSnapshot &player : m_state.players) {
        for (int id : player.handcards) ++occurrences[id];
        for (int id : player.equips) ++occurrences[id];
        for (int id : player.judgingArea) ++occurrences[id];
        for (auto it = player.piles.constBegin(); it != player.piles.constEnd(); ++it)
            for (int id : it.value()) ++occurrences[id];
    }
    for (int id = 0; id < m_state.cards.size(); ++id) {
        const int place = m_state.cardPlaces.value(
            id, static_cast<int>(Player::PlaceUnknown));
        if ((place == static_cast<int>(Player::PlaceTable)
             || place == static_cast<int>(Player::PlaceWuGu))
            && occurrences.value(id) == 0
            && m_state.cardOwners.value(id).isEmpty())
            continue;
        if (occurrences.value(id) != 1)
            markInvalid(m_state, QStringLiteral(
                "card ledger mismatch for id %1: place=%2 occurrences=%3 owner=%4")
                .arg(id).arg(place).arg(occurrences.value(id))
                .arg(m_state.cardOwners.value(id)));
        if (place == static_cast<int>(Player::PlaceUnknown))
            markInvalid(m_state, QStringLiteral("card %1 has unknown place").arg(id));
    }
    m_state.eligible = m_state.eligible && m_state.unsupportedState.isEmpty();
    m_turnCount = m_state.turnCount;
}

GameSnapshot::GameSnapshot(const QString &filepath, QObject *parent)
    : QObject(parent), m_timestamp(QDateTime::currentDateTime())
{
    load(filepath);
}

bool GameSnapshot::save(const QString &filepath)
{
    m_error.clear();
    if (m_snapshotType != QStringLiteral("turn")) {
        m_error = QStringLiteral("snapshot is not a resumable turn boundary");
        return false;
    }
    if (!m_state.eligible) {
        m_error = m_state.ineligibleReason.isEmpty()
            ? QStringLiteral("snapshot contains unsupported or incomplete state")
            : m_state.ineligibleReason;
        return false;
    }
    QString validationError;
    if (!validateState(m_state, &validationError)) {
        m_error = validationError;
        return false;
    }
    QVariantMap root;
    root[QStringLiteral("format")] = takeoverFormat();
    root[QStringLiteral("schemaVersion")] = TakeoverSchemaVersion;
    root[QStringLiteral("timestamp")] = isoDateWithMilliseconds(m_timestamp);
    root[QStringLiteral("replayPath")] = m_replayPath;
    root[QStringLiteral("snapshotType")] = m_snapshotType;
    root[QStringLiteral("description")] = m_description;
    root[QStringLiteral("state")] = m_state.serialize();

    QString jsonError;
    if (!checkMap(root, QStringLiteral("root"), &jsonError)) {
        m_error = jsonError;
        return false;
    }
    QJsonDocument document = QJsonDocument::fromVariant(root);
    if (document.isNull()) {
        m_error = QStringLiteral("snapshot cannot be represented as JSON");
        return false;
    }
    const QFileInfo info(filepath);
    if (!QDir().mkpath(info.absolutePath())) {
        m_error = QStringLiteral("cannot create snapshot directory");
        return false;
    }
    QSaveFile file(filepath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_error = file.errorString();
        return false;
    }
    const QByteArray data = document.toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit()) {
        m_error = file.errorString();
        return false;
    }
    return true;
}

bool GameSnapshot::load(const QString &filepath)
{
    m_error.clear();
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_error = QStringLiteral("invalid snapshot JSON: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("format")).type() != QJsonValue::String
        || object.value(QStringLiteral("format")).toString() != takeoverFormat()) {
        m_error = QStringLiteral("unsupported snapshot format");
        return false;
    }
    if (!isJsonInteger(object.value(QStringLiteral("schemaVersion")))
        || object.value(QStringLiteral("schemaVersion")).toDouble()
            != static_cast<double>(TakeoverSchemaVersion)) {
        m_error = QStringLiteral("unsupported snapshot schema version");
        return false;
    }
    if (object.value(QStringLiteral("state")).type() != QJsonValue::Object) {
        m_error = QStringLiteral("snapshot state is missing or not an object");
        return false;
    }
    const QVariantMap root = object.toVariantMap();
    const QVariantMap stateMap = root.value(QStringLiteral("state")).toMap();
    const QStringList required = {
        QStringLiteral("stateVersion"), QStringLiteral("turnCount"),
        QStringLiteral("roundCount"), QStringLiteral("turnSerial"),
        QStringLiteral("currentPlayer"), QStringLiteral("currentPhase"),
        QStringLiteral("gameMode"), QStringLiteral("packages"),
        QStringLiteral("players"), QStringLiteral("seatOrder"),
        QStringLiteral("cards"), QStringLiteral("cardPlaces"),
        QStringLiteral("cardOwners"), QStringLiteral("drawPile"),
        QStringLiteral("discardPile"), QStringLiteral("roomTags"),
        QStringLiteral("chatHistory"),
        QStringLiteral("catalogFingerprint"), QStringLiteral("configFingerprint"),
        QStringLiteral("gameplayRng"), QStringLiteral("aiRng"),
        QStringLiteral("pendingExtraTurns"), QStringLiteral("luaTakeoverState"),
        QStringLiteral("unsupportedState"), QStringLiteral("eligible"),
        QStringLiteral("ineligibleReason")
    };
    for (const QString &key : required) {
        if (!stateMap.contains(key)) {
            m_error = QStringLiteral("snapshot state field missing: %1").arg(key);
            return false;
        }
    }
    if (!validateSnapshotStateJson(object.value(QStringLiteral("state")).toObject(),
                                   &m_error))
        return false;
    bool stateVersionOk = false;
    const int stateVersion = stateMap.value(QStringLiteral("stateVersion")).toInt(&stateVersionOk);
    if (!stateVersionOk || stateVersion != TakeoverSchemaVersion) {
        m_error = QStringLiteral("snapshot state schema mismatch");
        return false;
    }
    QString jsonError;
    if (!checkMap(root, QStringLiteral("root"), &jsonError)) {
        m_error = jsonError;
        return false;
    }
    m_timestamp = fromIsoDateWithMilliseconds(
        root.value(QStringLiteral("timestamp")).toString());
    if (!m_timestamp.isValid()) {
        m_error = QStringLiteral("snapshot timestamp is invalid");
        return false;
    }
    m_replayPath = root.value(QStringLiteral("replayPath")).toString();
    m_snapshotType = root.value(QStringLiteral("snapshotType")).toString();
    if (m_snapshotType != QStringLiteral("turn")) {
        m_error = QStringLiteral("snapshot is not a resumable turn boundary");
        return false;
    }
    m_description = root.value(QStringLiteral("description")).toString();
    m_state = GlobalSnapshot::deserialize(stateMap);
    if (!validateState(m_state, &jsonError)) {
        m_error = jsonError;
        return false;
    }
    m_turnCount = m_state.turnCount;
    return true;
}

GlobalSnapshot GameSnapshot::getState() const { return m_state; }

void GameSnapshot::setState(const GlobalSnapshot &state)
{
    m_state = state;
    m_turnCount = state.turnCount;
}

int GameSnapshot::getTurnCount() const { return m_turnCount; }
void GameSnapshot::setTurnCount(int turn) { m_turnCount = turn; m_state.turnCount = turn; }
quint64 GameSnapshot::getTurnSerial() const { return m_state.turnSerial; }
void GameSnapshot::setTurnSerial(quint64 serial) { m_state.turnSerial = serial; }
QDateTime GameSnapshot::getTimestamp() const { return m_timestamp; }
QString GameSnapshot::getReplayPath() const { return m_replayPath; }
void GameSnapshot::setReplayPath(const QString &path) { m_replayPath = path; }
QString GameSnapshot::getSnapshotType() const { return m_snapshotType; }
void GameSnapshot::setSnapshotType(const QString &type) { m_snapshotType = type; }
QString GameSnapshot::getDescription() const { return m_description; }
void GameSnapshot::setDescription(const QString &desc) { m_description = desc; }
bool GameSnapshot::isEligible() const { return m_state.eligible && m_error.isEmpty(); }
QString GameSnapshot::getError() const { return m_error.isEmpty() ? m_state.ineligibleReason : m_error; }

QString GameSnapshot::getSnapshotDir(const QString &replayPath)
{
    const QFileInfo info(replayPath);
    return info.absolutePath() + QStringLiteral("/") + info.completeBaseName() + QStringLiteral(".snapshots");
}

QString GameSnapshot::generateSnapshotFilename(int turnCount, const QString &type, const QString &playerName)
{
    QString filename = QStringLiteral("turn_%1").arg(turnCount, 3, 10, QChar('0'));
    if (!type.isEmpty()) {
        filename += QStringLiteral("_") + type;
        if (!playerName.isEmpty())
            filename += QStringLiteral("_") + playerName;
    }
    return filename + QStringLiteral(".json");
}

QVariantMap GameSnapshot::currentCatalogFingerprint()
{
    return buildCatalogFingerprint();
}

QVariantMap GameSnapshot::currentConfigFingerprint(const QString &gameMode)
{
    return buildConfigFingerprint(gameMode);
}

bool GameSnapshot::validateRuntimeCompatibility(const GlobalSnapshot &state,
                                                QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    const GameModeStruct mode = Sanguosha->getGameMode(state.gameMode);
    static const QStringList specialControllers = {
        QStringLiteral("02_1v1"), QStringLiteral("06_3v3"),
        QStringLiteral("06_XMode"), QStringLiteral("04_1v3"),
        QStringLiteral("04_boss"), QStringLiteral("08_defense")
    };
    if (!mode.isValid() || mode.is_scenario || mode.is_mini_scene
        || specialControllers.contains(state.gameMode)
        || state.gameMode.startsWith(QStringLiteral("hegemony"), Qt::CaseInsensitive)
        || Sanguosha->getScenario(state.gameMode) != nullptr) {
        return fail(QStringLiteral("game mode is not supported by ordinary takeover"));
    }

    const QVariantMap expectedCatalog = buildCatalogFingerprint();
    if (canonicalJson(state.catalogFingerprint) != canonicalJson(expectedCatalog)) {
        return fail(QStringLiteral(
            "engine/package/card catalog fingerprint mismatch: snapshot=%1 runtime=%2")
            .arg(state.catalogFingerprint.value(QStringLiteral("sha256")).toString(),
                 expectedCatalog.value(QStringLiteral("sha256")).toString()));
    }
    const QVariantMap expectedConfig = buildConfigFingerprint(state.gameMode);
    if (canonicalJson(state.configFingerprint) != canonicalJson(expectedConfig))
        return fail(QStringLiteral("gameplay configuration fingerprint mismatch"));

    if (state.configFingerprint.value(QStringLiteral("gameMode")).toString() != state.gameMode)
        return fail(QStringLiteral("snapshot game mode fingerprint is inconsistent"));
    if (state.configFingerprint.value(QStringLiteral("enableHegemony")).toBool())
        return fail(QStringLiteral("hegemony takeover is not supported"));
    if (state.configFingerprint.value(QStringLiteral("enableBasara")).toBool())
        return fail(QStringLiteral("basara takeover is not supported"));

    auto instanceToken = [](const QString &skillName, int instanceID) {
        return skillName + QLatin1Char('#') + QString::number(instanceID);
    };
    QMap<QString, QSet<QString>> instances;
    for (const PlayerSnapshot &player : state.players) {
        if (!player.general.isEmpty() && !Sanguosha->getGeneral(player.general))
            return fail(QStringLiteral("missing general: %1").arg(player.general));
        if (!player.general2.isEmpty() && !Sanguosha->getGeneral(player.general2))
            return fail(QStringLiteral("missing secondary general: %1").arg(player.general2));
        for (const QString &skill : player.skills) {
            if (!Sanguosha->getSkill(skill))
                return fail(QStringLiteral("missing skill: %1").arg(skill));
        }
        for (const SkillInstanceSnapshot &instance : player.skillInstances) {
            if (!Sanguosha->getSkill(instance.skillName))
                return fail(QStringLiteral("missing skill instance definition: %1").arg(instance.skillName));
            const QString token = instanceToken(instance.skillName, instance.instanceID);
            if (instance.instanceID <= 0 || instances[player.objectName].contains(token))
                return fail(QStringLiteral("duplicate or invalid skill instance"));
            instances[player.objectName].insert(token);
        }
    }
    for (const PlayerSnapshot &player : state.players) {
        for (const SkillInstanceSnapshot &instance : player.skillInstances) {
            if (!instance.parentSkillName.isEmpty()
                && !instances.value(player.objectName).contains(
                    instanceToken(instance.parentSkillName, instance.parentInstanceID))) {
                return fail(QStringLiteral("broken local skill instance parent"));
            }
            if (!instance.parentRefOwner.isEmpty()
                && !instances.value(instance.parentRefOwner).contains(
                    instanceToken(instance.parentRefSkillName,
                                  instance.parentRefInstanceID))) {
                return fail(QStringLiteral("broken cross-player skill instance parent"));
            }
        }
    }
    return true;
}
