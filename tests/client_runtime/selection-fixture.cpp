#include "selection-fixture.h"

#include "client-selection-runtime.h"
#include "client-room-context.h"
#include "interaction-reply-encoder.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ClientRulesFixtures {
QByteArray canonicalJson(const QJsonValue &value)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        QStringList keys = object.keys();
        std::sort(keys.begin(), keys.end());
        QByteArray result("{");
        for (const QString &key : keys) {
            if (result.size() > 1)
                result += ',';
            result += canonicalJson(QJsonValue(key)) + ':' + canonicalJson(object.value(key));
        }
        return result + '}';
    }
    if (value.isArray()) {
        QByteArray result("[");
        for (const QJsonValue &entry : value.toArray()) {
            if (result.size() > 1)
                result += ',';
            result += canonicalJson(entry);
        }
        return result + ']';
    }
    QJsonArray scalar;
    scalar.append(value);
    const QByteArray encoded = QJsonDocument(scalar).toJson(QJsonDocument::Compact);
    return encoded.mid(1, encoded.size() - 2);
}

namespace {
void require(bool condition, const QString &message)
{
    if (!condition)
        throw std::runtime_error(message.toStdString());
}

QString text(const QJsonObject &object, const QString &key, const QString &fallback = QString())
{
    if (!object.contains(key))
        return fallback;
    require(object.value(key).isString(), key + QStringLiteral(" must be a string"));
    return object.value(key).toString();
}

QString requiredText(const QJsonObject &object, const QString &key)
{
    const QString value = text(object, key);
    require(!value.isEmpty(), key + QStringLiteral(" must be a nonempty string"));
    return value;
}

int integer(const QJsonObject &object, const QString &key, int minimum, int maximum)
{
    const QJsonValue value = object.value(key);
    const double number = value.toDouble();
    require(value.isDouble() && std::isfinite(number) && std::floor(number) == number
                && number >= minimum && number <= maximum,
            key + QStringLiteral(" is missing or outside its integer range"));
    return static_cast<int>(number);
}

QJsonArray array(const QJsonObject &object, const QString &key, bool required = false)
{
    if (!required && !object.contains(key))
        return {};
    require(object.value(key).isArray(), key + QStringLiteral(" must be an array"));
    return object.value(key).toArray();
}

QJsonObject asObject(const QJsonValue &value, const QString &where)
{
    require(value.isObject(), where + QStringLiteral(" must be an object"));
    return value.toObject();
}

QStringList strings(const QJsonObject &object, const QString &key)
{
    QStringList result;
    for (const QJsonValue &value : array(object, key)) {
        require(value.isString() && !value.toString().isEmpty(),
                key + QStringLiteral(" must contain nonempty strings"));
        result.append(value.toString());
    }
    return result;
}

Card::Suit suit(const QString &name)
{
    if (name == QLatin1String("spade")) return Card::Spade;
    if (name == QLatin1String("club")) return Card::Club;
    if (name == QLatin1String("heart")) return Card::Heart;
    if (name == QLatin1String("diamond")) return Card::Diamond;
    if (name == QLatin1String("no_suit")) return Card::NoSuit;
    throw std::runtime_error("unknown suit");
}

Player::Place place(const QString &name)
{
    if (name == QLatin1String("hand")) return Player::PlaceHand;
    if (name == QLatin1String("equip")) return Player::PlaceEquip;
    if (name == QLatin1String("table")) return Player::PlaceTable;
    if (name == QLatin1String("discard")) return Player::DiscardPile;
    throw std::runtime_error("unsupported fixture card place");
}

CardUseStruct::CardUseReason reason(const QString &name)
{
    if (name == QLatin1String("play")) return CardUseStruct::CARD_USE_REASON_PLAY;
    if (name == QLatin1String("response")) return CardUseStruct::CARD_USE_REASON_RESPONSE;
    if (name == QLatin1String("response_use")) return CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    throw std::runtime_error("unknown card-use reason");
}

quint64 requestId(const QJsonObject &query)
{
    const QString encoded = requiredText(query, QStringLiteral("request_id"));
    static const QRegularExpression decimal(QStringLiteral("^[1-9][0-9]*$"));
    bool ok = false;
    const quint64 result = encoded.toULongLong(&ok);
    require(decimal.match(encoded).hasMatch() && ok && result != 0,
            QStringLiteral("request_id must be a positive decimal uint64 string"));
    return result;
}

QString buildStatus(ClientRules::SkillCardBuildStatus status)
{
    using S = ClientRules::SkillCardBuildStatus;
    switch (status) {
    case S::Unknown: return QStringLiteral("unknown");
    case S::Built: return QStringLiteral("built");
    case S::EngineUnavailable: return QStringLiteral("engine_unavailable");
    case S::MissingSkill: return QStringLiteral("missing_skill");
    case S::WrongSelf: return QStringLiteral("wrong_self");
    case S::ActivationUnavailable: return QStringLiteral("activation_unavailable");
    case S::MissingSubcard: return QStringLiteral("missing_subcard");
    case S::CardRejected: return QStringLiteral("card_rejected");
    case S::IncompleteSelection: return QStringLiteral("incomplete_selection");
    case S::CreateRejected: return QStringLiteral("create_rejected");
    }
    throw std::runtime_error("unmapped build status");
}

QString targetReason(ClientRules::TargetValidationReason value)
{
    using R = ClientRules::TargetValidationReason;
    switch (value) {
    case R::None: return QStringLiteral("none");
    case R::InvalidTarget: return QStringLiteral("invalid_target");
    case R::VoteLimitExceeded: return QStringLiteral("vote_limit_exceeded");
    case R::MissingTarget: return QStringLiteral("missing_target");
    case R::TargetCount: return QStringLiteral("target_count");
    }
    throw std::runtime_error("unmapped target reason");
}

QJsonObject cardData(const Card *card)
{
    require(card != nullptr, QStringLiteral("card registry/live card is incomplete"));
    // Virtual IDs are allocator-sequence counters, not portable card identities.
    return {{QStringLiteral("id"), card->isVirtualCard()
                ? QJsonValue(QJsonValue::Null) : QJsonValue(card->getId())},
            {QStringLiteral("object_name"), card->objectName()},
            {QStringLiteral("suit"), static_cast<int>(card->getSuit())},
            {QStringLiteral("number"), card->getNumber()},
            {QStringLiteral("class_name"), card->getClassName()},
            {QStringLiteral("package"), card->isVirtualCard() ? QString() : card->getPackage()}};
}

int resolveCard(const QMap<QString, int> &cards, const QString &key)
{
    require(cards.contains(key), QStringLiteral("unknown fixture card key: ") + key);
    return cards.value(key);
}

// This runner has a single isolated context. Member destruction clears players
// (and engine Self) before clearing the room's WrappedCards.
struct Scene
{
    ClientGameState state;
    ClientRoomContext room{&state};
    ClientPlayerModel players{&state};
    ~Scene() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
};

QJsonObject evaluate(const QJsonObject &query, Scene &scene, const QMap<QString, int> &cards)
{
    using namespace ClientRules;
    const auto useReason = reason(requiredText(query, QStringLiteral("reason")));
    scene.room.setCardUseContext(useReason, text(query, QStringLiteral("pattern")));
    CardSelectionDraft draft;
    const bool skillSelected = query.contains(QStringLiteral("skill"));
    require(skillSelected != query.contains(QStringLiteral("card")),
            QStringLiteral("query requires exactly one of card or skill"));
    if (skillSelected) {
        const QJsonObject skill = asObject(query.value(QStringLiteral("skill")), QStringLiteral("skill"));
        draft.skill.selfName = scene.state.selfName();
        draft.skill.skillName = requiredText(skill, QStringLiteral("name"));
        draft.skill.instanceId = skill.contains(QStringLiteral("instance_id"))
            ? integer(skill, QStringLiteral("instance_id"), 0, 2147483647) : 0;
        draft.skill.userString = text(skill, QStringLiteral("user_string"));
        for (const QString &key : strings(skill, QStringLiteral("subcards")))
            draft.skill.subcardIds.append(resolveCard(cards, key));
    } else {
        draft.cardId = resolveCard(cards, requiredText(query, QStringLiteral("card")));
    }
    draft.selectedTargets = strings(query, QStringLiteral("targets"));
    draft.targetPool = query.contains(QStringLiteral("target_pool"))
        ? strings(query, QStringLiteral("target_pool")) : scene.state.playerNames();
    const PlayerLookup lookup = [&scene](const QString &name) { return scene.players.player(name); };
    const quint64 correlation = requestId(query);
    const CardSelectionEvaluation evaluated = evaluateCardSelection(draft, lookup, scene.players.self());
    const auto &next = evaluated.nextTargets;
    const auto &validation = evaluated.targetValidation;
    QJsonObject votes;
    for (auto it = next.maxVotes.constBegin(); it != next.maxVotes.constEnd(); ++it)
        votes.insert(it.key(), it.value());
    QJsonObject result{
        {QStringLiteral("name"), requiredText(query, QStringLiteral("name"))},
        {QStringLiteral("request_id"), QString::number(correlation)},
        {QStringLiteral("known"), evaluated.known},
        {QStringLiteral("card_ready"), evaluated.cardReady},
        {QStringLiteral("can_confirm"), evaluated.canConfirm},
        {QStringLiteral("build_status"), skillSelected ? buildStatus(evaluated.buildStatus) : QStringLiteral("physical")},
        {QStringLiteral("card_text"), evaluated.cardText},
        {QStringLiteral("next_targets"), QJsonObject{
            {QStringLiteral("known"), next.known}, {QStringLiteral("fixed"), next.fixed},
            {QStringLiteral("feasible"), next.feasible},
            {QStringLiteral("candidates"), QJsonArray::fromStringList(next.candidates)},
            {QStringLiteral("max_votes"), votes}}},
        {QStringLiteral("target_validation"), QJsonObject{
            {QStringLiteral("known"), validation.known}, {QStringLiteral("valid"), validation.valid},
            {QStringLiteral("incomplete"), validation.incomplete},
            {QStringLiteral("reason"), targetReason(validation.reason)},
            {QStringLiteral("target"), validation.targetName},
            {QStringLiteral("selected_votes"), validation.selectedVotes}}},
        {QStringLiteral("preview_card"), QJsonValue(QJsonValue::Null)},
        {QStringLiteral("response"), QJsonValue(QJsonValue::Null)},
        {QStringLiteral("wire"), QJsonValue(QJsonValue::Null)}};
    if (evaluated.nativeCard != nullptr)
        result.insert(QStringLiteral("preview_card"), cardData(evaluated.nativeCard));
    if (evaluated.canConfirm) {
        InteractionRequest request;
        request.requestId = correlation;
        request.type = useReason == CardUseStruct::CARD_USE_REASON_PLAY
            ? InteractionType::PlayCard : InteractionType::ResponseCard;
        request.command = useReason == CardUseStruct::CARD_USE_REASON_PLAY
            ? QSanProtocol::S_COMMAND_PLAY_CARD : QSanProtocol::S_COMMAND_RESPONSE_CARD;
        const auto response = makeCardSelectionResponse(request, draft, evaluated);
        const auto *answer = response.payloadAs<InteractionResponse::CardSelectionData>();
        require(answer != nullptr, QStringLiteral("runtime produced no card response"));
        const auto wire = InteractionReplyEncoder::cardResponse(request, response);
        require(wire.command != QSanProtocol::S_COMMAND_UNKNOWN,
                QStringLiteral("canonical reply encoding failed"));
        QJsonArray ids, subcards;
        for (int id : answer->cardIds) ids.append(id);
        for (int id : answer->subcardIds) subcards.append(id);
        result.insert(QStringLiteral("response"), QJsonObject{
            {QStringLiteral("request_id"), QString::number(response.requestId)},
            {QStringLiteral("card_ids"), ids}, {QStringLiteral("subcard_ids"), subcards},
            {QStringLiteral("card_text"), answer->cardText},
            {QStringLiteral("targets"), QJsonArray::fromStringList(answer->targets)},
            {QStringLiteral("activation_skill_name"), answer->activationSkillName},
            {QStringLiteral("activation_skill_instance_id"), answer->activationSkillInstanceId}});
        result.insert(QStringLiteral("wire"), QJsonObject{
            {QStringLiteral("command"), static_cast<int>(wire.command)},
            {QStringLiteral("reply_to"), QString::number(wire.replyTo)},
            {QStringLiteral("payload"), QJsonValue::fromVariant(wire.payload)}});
    }
    // Only JSON survives this boundary. The isolated runner has no concurrent
    // gameplay/event handlers; queued temporary cards can now be destroyed.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return result;
}
} // namespace

bool run(const QJsonObject &fixture, QJsonObject *output, QString *error)
{
    if (error != nullptr) error->clear();
    try {
        require(output != nullptr, QStringLiteral("output is null"));
        require(integer(fixture, QStringLiteral("schema_version"), 1, 1) == 1,
                QStringLiteral("unsupported fixture schema"));
        require(Sanguosha != nullptr && Sanguosha->currentRoomContext() == nullptr,
                QStringLiteral("fixture requires an initialized engine without a live room"));
        const QString name = requiredText(fixture, QStringLiteral("name"));
        Scene scene;
        scene.state.setSetup(asObject(fixture.value(QStringLiteral("setup")), QStringLiteral("setup")).toVariantMap());
        scene.state.setSelfName(requiredText(fixture, QStringLiteral("self")));
        const QJsonArray people = array(fixture, QStringLiteral("players"), true);
        require(!people.isEmpty() && people.size() <= 64, QStringLiteral("invalid fixture player count"));
        QStringList names;
        QSet<int> seats;
        for (const QJsonValue &value : people) {
            const auto person = asObject(value, QStringLiteral("player"));
            const QString player = requiredText(person, QStringLiteral("name"));
            const auto properties = asObject(person.value(QStringLiteral("properties")), QStringLiteral("properties"));
            const int seat = integer(properties, QStringLiteral("seat"), 1, 64);
            require(!names.contains(player) && !seats.contains(seat), QStringLiteral("duplicate player or seat"));
            names.append(player);
            seats.insert(seat);
            scene.state.addPlayer(player);
            for (auto it = properties.constBegin(); it != properties.constEnd(); ++it) {
                require(it.key() != QLatin1String("object_name") && it.key() != QLatin1String("objectName"),
                        QStringLiteral("player identity cannot be overridden by properties"));
                const QVariant projected = it.key() == QLatin1String("skills") || it.key() == QLatin1String("flags")
                    ? QVariant(strings(properties, it.key())) : it.value().toVariant();
                scene.state.setPlayerValue(player, it.key(), projected);
            }
            for (const QString &skill : strings(properties, QStringLiteral("skills")))
                require(Sanguosha->getSkill(skill) != nullptr, QStringLiteral("fixture requires skill: ") + skill);
            const QString general = text(properties, QStringLiteral("general"));
            require(general.isEmpty() || Sanguosha->getGeneral(general) != nullptr,
                    QStringLiteral("fixture requires general: ") + general);
            if (properties.contains(QStringLiteral("alive")))
                require(properties.value(QStringLiteral("alive")).isBool(), QStringLiteral("alive must be boolean"));
            scene.state.setPlayerAlive(player, properties.value(QStringLiteral("alive")).toBool(true));
        }
        require(names.contains(scene.state.selfName()), QStringLiteral("self is not in fixture players"));
        scene.state.setPlayerNames(names);
        scene.state.setCardIdSpace(Sanguosha->getCardCount());
        QJsonArray registry;
        for (int id = 0; id < Sanguosha->getCardCount(); ++id)
            registry.append(cardData(Sanguosha->getEngineCard(id)));
        const QString registryHash = QString::fromLatin1(QCryptographicHash::hash(
            canonicalJson(registry), QCryptographicHash::Sha256).toHex());
        QMap<QString, int> cards;
        QSet<int> used;
        QJsonObject resolved;
        for (const QJsonValue &value : array(fixture, QStringLiteral("cards"), true)) {
            const auto entry = asObject(value, QStringLiteral("card"));
            const QString key = requiredText(entry, QStringLiteral("key"));
            const auto selector = asObject(entry.value(QStringLiteral("selector")), QStringLiteral("selector"));
            const QString cardName = requiredText(selector, QStringLiteral("name"));
            const bool hasSuit = selector.contains(QStringLiteral("suit"));
            const auto wantedSuit = hasSuit ? suit(requiredText(selector, QStringLiteral("suit"))) : Card::NoSuit;
            const int number = selector.contains(QStringLiteral("number"))
                ? integer(selector, QStringLiteral("number"), 0, 13) : -1;
            require(!cards.contains(key), QStringLiteral("duplicate card key: ") + key);
            int found = -1;
            for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
                const Card *card = Sanguosha->getEngineCard(id);
                if (card && !used.contains(id) && card->objectName() == cardName
                    && (!hasSuit || card->getSuit() == wantedSuit)
                    && (number < 0 || card->getNumber() == number)) { found = id; break; }
            }
            require(found >= 0, QStringLiteral("no registered card matches fixture key: ") + key);
            const QString owner = text(entry, QStringLiteral("owner"));
            require(owner.isEmpty() || names.contains(owner), QStringLiteral("card owner is not in fixture players"));
            const auto location = place(requiredText(entry, QStringLiteral("place")));
            require(!owner.isEmpty() || (location != Player::PlaceHand && location != Player::PlaceEquip),
                    QStringLiteral("hand/equip card requires an owner"));
            cards.insert(key, found);
            used.insert(found);
            resolved.insert(key, cardData(Sanguosha->getEngineCard(found)));
            scene.state.setCardValue(found, QStringLiteral("owner"), owner);
            scene.state.setCardValue(found, QStringLiteral("place"), static_cast<int>(location));
        }
        scene.room.setOwnerResolver([&scene](int id) { return scene.players.cardOwner(id); });
        scene.room.enterGame();
        scene.players.sync();
        const QJsonArray queries = array(fixture, QStringLiteral("queries"), true);
        require(!queries.isEmpty() && queries.size() <= 1024, QStringLiteral("invalid query count"));
        QJsonArray results;
        QSet<QString> queryNames;
        for (const QJsonValue &value : queries) {
            const auto query = asObject(value, QStringLiteral("query"));
            const QString queryName = requiredText(query, QStringLiteral("name"));
            require(!queryNames.contains(queryName), QStringLiteral("duplicate query name"));
            queryNames.insert(queryName);
            for (const QJsonValue &updateValue : array(query, QStringLiteral("updates"))) {
                const auto update = asObject(updateValue, QStringLiteral("update"));
                const int id = resolveCard(cards, requiredText(update, QStringLiteral("card")));
                if (update.contains(QStringLiteral("reset")))
                    require(update.value(QStringLiteral("reset")).isBool(), QStringLiteral("reset must be boolean"));
                QSanProtocol::ProtocolMessage message;
                message.command = QSanProtocol::S_COMMAND_UPDATE_CARD;
                QVariantMap payload{{QStringLiteral("card_id"), id}};
                if (update.value(QStringLiteral("reset")).toBool()) {
                    payload.insert(QStringLiteral("action"), QStringLiteral("reset"));
                } else {
                    const QString cardName = requiredText(update, QStringLiteral("name"));
                    require(Sanguosha->hasCard(cardName), QStringLiteral("unknown update card: ") + cardName);
                    payload.insert(QStringLiteral("card_name"), cardName);
                    payload.insert(QStringLiteral("object_name"), cardName);
                    payload.insert(QStringLiteral("suit"), static_cast<int>(suit(requiredText(update, QStringLiteral("suit")))));
                    payload.insert(QStringLiteral("number"), integer(update, QStringLiteral("number"), 0, 13));
                    payload.insert(QStringLiteral("skill_name"), text(update, QStringLiteral("skill_name")));
                    payload.insert(QStringLiteral("flags"), strings(update, QStringLiteral("flags")));
                }
                message.payload = payload;
                scene.room.applyMessage(message);
            }
            scene.players.sync();
            results.append(evaluate(query, scene, cards));
        }
        *output = {{QStringLiteral("schema_version"), 1}, {QStringLiteral("name"), name},
                   {QStringLiteral("card_registry_sha256"), registryHash},
                   {QStringLiteral("resolved_cards"), resolved}, {QStringLiteral("queries"), results}};
        return true;
    } catch (const std::exception &exception) {
        if (error != nullptr) *error = QString::fromUtf8(exception.what());
        return false;
    }
}
} // namespace ClientRulesFixtures
