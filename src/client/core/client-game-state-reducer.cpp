#include "client-game-state-reducer.h"

#include "client-game-state.h"
#include "protocol.h"

#include <QSet>

#include <algorithm>

using namespace QSanProtocol;

namespace {

// Player::DiscardPile. The core deliberately does not link the engine, so the
// place ordinal is spelled out the same way the hand and equip ones already are.
constexpr int kDiscardPilePlace = 5;

QStringList strings(const QVariant &value)
{
    if (value.canConvert<QStringList>()) {
        const QStringList direct = value.toStringList();
        if (!direct.isEmpty())
            return direct;
    }
    QStringList result;
    for (const QVariant &entry : value.toList())
        result.append(entry.toString());
    return result;
}

QList<int> integers(const QVariant &value)
{
    QList<int> result;
    for (const QVariant &entry : value.toList())
        result.append(entry.toInt());
    return result;
}

QVariantList variants(const QList<int> &values)
{
    QVariantList result;
    for (int value : values)
        result.append(value);
    return result;
}

QString resolvePlayerName(const ClientGameState *state, const QVariant &value)
{
    const QString name = value.toString();
    return name == QLatin1String(S_PLAYER_SELF_REFERENCE_ID) ? state->selfName() : name;
}

bool booleanValue(const QVariant &value)
{
    if (value.userType() == QMetaType::Bool)
        return value.toBool();
    const QString normalized = value.toString().trimmed().toLower();
    if (normalized == QLatin1String("true") || normalized == QLatin1String("1"))
        return true;
    if (normalized == QLatin1String("false") || normalized == QLatin1String("0"))
        return false;
    return value.toBool();
}

void adjustHandCount(ClientGameState *state, const QString &player, int delta)
{
    if (player.isEmpty() || delta == 0)
        return;
    const int count = qMax(0,
        state->playerValue(player, QStringLiteral("hand_count")).toInt() + delta);
    state->setPlayerValue(player, QStringLiteral("hand_count"), count);
}

void applyCardMovement(ClientGameState *state, int command, const QVariantMap &object)
{
    // The server only marshals the whole discard pile on a state sync, so the
    // count has to follow the moves in and out of Player::DiscardPile.
    QVariantList discardPile = state->gameValue(QStringLiteral("discard_pile")).toList();
    bool discardPileChanged = false;
    for (const QVariant &entry : object.value(QStringLiteral("moves")).toList()) {
        const QVariantMap move = entry.toMap();
        const QString fromPlayer = resolvePlayerName(
            state, move.value(QStringLiteral("from_player")));
        const QString owner = resolvePlayerName(
            state, move.value(QStringLiteral("to_player")));
        const int fromPlace = move.value(QStringLiteral("from_place")).toInt();
        const int place = move.value(QStringLiteral("to_place")).toInt();
        const QString pile = move.value(QStringLiteral("to_pile")).toString();
        const QList<int> cardIds = integers(move.value(QStringLiteral("card_ids")));
        if (command == S_COMMAND_LOSE_CARD && fromPlace == 0)
            adjustHandCount(state, fromPlayer, -cardIds.size());
        if (command == S_COMMAND_GET_CARD && place == 0)
            adjustHandCount(state, owner, cardIds.size());
        for (int cardId : cardIds) {
            state->setCardValue(cardId, QStringLiteral("owner"), owner);
            state->setCardValue(cardId, QStringLiteral("place"), place);
            state->setCardValue(cardId, QStringLiteral("pile"), pile);
            state->setCardValue(cardId, QStringLiteral("open"),
                                move.value(QStringLiteral("open")));

            const auto known = std::find_if(discardPile.begin(), discardPile.end(),
                [cardId](const QVariant &entry) { return entry.toInt() == cardId; });
            const bool discarded = place == kDiscardPilePlace;
            if (discarded && known == discardPile.end()) {
                discardPile.append(cardId);
                discardPileChanged = true;
            } else if (!discarded && known != discardPile.end()) {
                discardPile.erase(known);
                discardPileChanged = true;
            }
        }
    }
    if (discardPileChanged)
        state->setGameValue(QStringLiteral("discard_pile"), discardPile);
}

void appendOrRemove(QStringList *values, const QString &value, bool add);

void applyPlayerProperty(ClientGameState *state, const QVariantMap &object)
{
    const QString action = object.value(QStringLiteral("action")).toString();
    QString player = resolvePlayerName(state, object.value(QStringLiteral("player_name")));
    if (action == QLatin1String("tag")) {
        const QString tag = object.value(QStringLiteral("tag_name")).toString();
        if (player.isEmpty() || tag.isEmpty())
            return;
        QVariantMap tags = state->playerValue(player, QStringLiteral("tags")).toMap();
        if (object.value(QStringLiteral("value_kind")).toString() == QLatin1String("removed"))
            tags.remove(tag);
        else
            tags.insert(tag, object.value(QStringLiteral("value")));
        state->setPlayerValue(player, QStringLiteral("tags"), tags);
        return;
    }
    if (action == QLatin1String("general_pile")) {
        const QString pile = object.value(QStringLiteral("pile_name")).toString();
        if (player.isEmpty() || pile.isEmpty())
            return;
        QVariantMap piles = state->playerValue(
            player, QStringLiteral("general_piles")).toMap();
        QStringList generals = piles.value(pile).toStringList();
        for (const QString &general : strings(object.value(QStringLiteral("general_names"))))
            appendOrRemove(&generals, general, object.value(QStringLiteral("add")).toBool());
        piles.insert(pile, generals);
        state->setPlayerValue(player, QStringLiteral("general_piles"), piles);
        return;
    }
    if (action != QLatin1String("property"))
        return;
    const QString property = object.value(QStringLiteral("property_name")).toString();
    const QVariant value = object.value(QStringLiteral("string_value"));
    if (property == QLatin1String("objectName")) {
        const QString objectName = value.toString();
        if (!objectName.isEmpty()) {
            if (object.value(QStringLiteral("player_name")).toString()
                == QLatin1String(S_PLAYER_SELF_REFERENCE_ID)) {
                state->setSelfName(objectName);
            }
            player = objectName;
        }
    }
    if (player.isEmpty() || property.isEmpty())
        return;

    QString stateKey = property;
    if (property == QLatin1String("objectName"))
        stateKey = QStringLiteral("object_name");
    else if (property == QLatin1String("maxhp"))
        stateKey = QStringLiteral("max_hp");
    else if (property == QLatin1String("general2"))
        stateKey = QStringLiteral("deputy_general");
    else if (property == QLatin1String("player_seat"))
        stateKey = QStringLiteral("seat");
    else if (property == QLatin1String("handcard_num"))
        stateKey = QStringLiteral("hand_count");

    // "phase" is not here: Player::getPhaseString() puts a name ("play") on the
    // wire, and toInt() would turn every phase into 0.
    static const QSet<QString> integerProperties{QStringLiteral("hp"),
        QStringLiteral("maxhp"), QStringLiteral("seat"),
        QStringLiteral("player_seat"), QStringLiteral("handcard_num")};
    static const QSet<QString> booleanProperties{QStringLiteral("alive"),
        QStringLiteral("chained"), QStringLiteral("faceup"),
        QStringLiteral("removed"), QStringLiteral("owner"),
        QStringLiteral("hasjudgearea"), QStringLiteral("RestPlayer")};
    QVariant projected = value;
    if (integerProperties.contains(property))
        projected = value.toInt();
    else if (booleanProperties.contains(property))
        projected = booleanValue(value);

    if (property == QLatin1String("flags")) {
        QStringList flags = state->playerValue(player, QStringLiteral("flags")).toStringList();
        const QString flag = value.toString();
        // "." is not a flag: Player::setFlags() reads it as "clear them all"
        // (player.cpp:219), and the server ends every turn with one
        // (gamerule.cpp:411). Appending it instead leaves actioned,
        // CurrentPlayer and every turn-scoped package flag set for the rest of
        // the game -- nothing else ever removes them.
        if (flag == QLatin1String(".")) {
            state->setPlayerValue(player, QStringLiteral("flags"), QStringList());
            return;
        }
        const bool remove = flag.startsWith(QLatin1Char('-'));
        appendOrRemove(&flags, remove ? flag.mid(1) : flag, !remove);
        state->setPlayerValue(player, QStringLiteral("flags"), flags);
        const QString currentFlag = QStringLiteral("CurrentPlayer");
        if (flag == currentFlag)
            state->setGameValue(QStringLiteral("current_player"), player);
        else if (flag == QStringLiteral("-") + currentFlag
                 && state->gameValue(QStringLiteral("current_player")).toString() == player)
            state->setGameValue(QStringLiteral("current_player"), QString());
        return;
    }

    state->setPlayerValue(player, stateKey, projected);
    if (property == QLatin1String("phase")) {
        state->setGameValue(QStringLiteral("current_phase"), projected);
        state->setGameValue(QStringLiteral("current_player"), player);
    }
}

void appendOrRemove(QStringList *values, const QString &value, bool add)
{
    if (add && !values->contains(value))
        values->append(value);
    else if (!add)
        values->removeAll(value);
}

void appendSkill(ClientGameState *state, const QString &player, const QString &skill)
{
    if (player.isEmpty() || skill.isEmpty())
        return;
    QStringList skills = state->playerValue(player, QStringLiteral("skills")).toStringList();
    if (!skills.contains(skill))
        skills.append(skill);
    state->setPlayerValue(player, QStringLiteral("skills"), skills);
}

void removeSkill(ClientGameState *state, const QString &player, const QString &skill)
{
    if (player.isEmpty() || skill.isEmpty())
        return;
    QStringList skills = state->playerValue(player, QStringLiteral("skills")).toStringList();
    skills.removeAll(skill);
    state->setPlayerValue(player, QStringLiteral("skills"), skills);
}

QString skillInstanceKey(const QString &skill, int instanceId)
{
    return QStringLiteral("%1#%2").arg(skill).arg(instanceId);
}

void storeSkillInstance(ClientGameState *state, const QVariantMap &entry)
{
    const QString player = resolvePlayerName(
        state, entry.value(QStringLiteral("owner_name")));
    const QString skill = entry.value(QStringLiteral("skill_name")).toString();
    const int instanceId = entry.value(QStringLiteral("instance_id")).toInt();
    if (player.isEmpty() || skill.isEmpty() || instanceId <= 0)
        return;
    QVariantMap instances = state->playerValue(
        player, QStringLiteral("skill_instances")).toMap();
    QVariantMap stored = entry;
    stored.insert(QStringLiteral("owner_name"), player);
    instances.insert(skillInstanceKey(skill, instanceId), stored);
    state->setPlayerValue(player, QStringLiteral("skill_instances"), instances);
    if (entry.value(QStringLiteral("visible"), true).toBool())
        appendSkill(state, player, skill);
}

void setVisibleCards(ClientGameState *state, const QString &player,
                     const QVariant &cardIds, const QString &visibility)
{
    const QList<int> cards = integers(cardIds);
    state->setPlayerValue(player, visibility, variants(cards));
    for (int cardId : cards) {
        state->setCardValue(cardId, QStringLiteral("owner"), player);
        state->setCardValue(cardId, visibility, true);
    }
}

} // namespace

ClientFlowDisposition ClientGameStateReducer::classifyNotification(int command)
{
    switch (command) {
    case S_COMMAND_NETWORK_DELAY_TEST:
    case S_COMMAND_OPERATION_TIMEOUT:
    case S_COMMAND_STATE_SYNC:
        return ClientFlowDisposition::SessionControl;
    case S_COMMAND_WARN:
    case S_COMMAND_SPEAK:
    case S_COMMAND_LOG_SKILL:
    case S_COMMAND_LOG_EVENT:
    case S_COMMAND_SET_EMOTION:
    case S_COMMAND_CHANGE_TABLE_BG:
    case S_COMMAND_INVOKE_SKILL:
        return ClientFlowDisposition::PresentationEvent;
    case S_COMMAND_ANIMATE:
    case S_COMMAND_PLAY_AUDIO:
        // Both drive desktop presentation only; a text transcript has nothing
        // to say about an animation or a sound.
        return ClientFlowDisposition::ExplicitTextIrrelevant;
    case S_COMMAND_ADD_PLAYER:
    case S_COMMAND_ADD_PLAYER_DYNAMIC:
    case S_COMMAND_REMOVE_PLAYER:
    case S_COMMAND_START_IN_X_SECONDS:
    case S_COMMAND_ARRANGE_SEATS:
    case S_COMMAND_GAME_START:
    case S_COMMAND_GAME_OVER:
    case S_COMMAND_CHANGE_HP:
    case S_COMMAND_CHANGE_MAXHP:
    case S_COMMAND_KILL_PLAYER:
    case S_COMMAND_REVIVE_PLAYER:
    case S_COMMAND_SHOW_CARD:
    case S_COMMAND_SHOW_VIRTUAL_CARD:
    case S_COMMAND_CARD_PROVENANCE:
    case S_COMMAND_UPDATE_PLAYER_UI_STATE:
    case S_COMMAND_UPDATE_CARD:
    case S_COMMAND_SET_MARK:
    case S_COMMAND_ATTACH_SKILL:
    case S_COMMAND_SKILL_INSTANCE:
    case S_COMMAND_MOVE_FOCUS:
    case S_COMMAND_SHOW_ALL_CARDS:
    case S_COMMAND_SKILL_GONGXIN:
    case S_COMMAND_ADD_HISTORY:
    case S_COMMAND_FIXED_DISTANCE:
    case S_COMMAND_ATTACK_RANGE:
    case S_COMMAND_CARD_LIMITATION:
    case S_COMMAND_NULLIFICATION_ASKED:
    case S_COMMAND_ENABLE_SURRENDER:
    case S_COMMAND_EXCHANGE_KNOWN_CARDS:
    case S_COMMAND_SET_KNOWN_CARDS:
    case S_COMMAND_SWITCH_CONTEXT:
    case S_COMMAND_VIEW_GENERALS:
    case S_COMMAND_UPDATE_BOSS_LEVEL:
    case S_COMMAND_UPDATE_STATE_ITEM:
    case S_COMMAND_AVAILABLE_CARDS:
    case S_COMMAND_GET_CARD:
    case S_COMMAND_LOSE_CARD:
    case S_COMMAND_SET_PROPERTY:
    case S_COMMAND_RESET_PILE:
    case S_COMMAND_UPDATE_PILE:
    case S_COMMAND_SYNCHRONIZE_DISCARD_PILE:
    case S_COMMAND_SYNC_PILE:
    case S_COMMAND_CARD_MARK:
    case S_COMMAND_CARD_FLAG:
    case S_COMMAND_WEAPON_RANGE:
    case S_COMMAND_FILL_AMAZING_GRACE:
    case S_COMMAND_TAKE_AMAZING_GRACE:
    case S_COMMAND_CLEAR_AMAZING_GRACE:
    case S_COMMAND_FILL_GENERAL:
    case S_COMMAND_TAKE_GENERAL:
    case S_COMMAND_RECOVER_GENERAL:
    case S_COMMAND_REVEAL_GENERAL:
    case S_COMMAND_UPDATE_SKILL:
    case S_COMMAND_ADD_ROUND:
    case S_COMMAND_SKILL_DESCRIPTION_SWAP:
    case S_COMMAND_ADD_EQUIP_AREA:
    case S_COMMAND_SET_EQUIP_AREA_COUNT:
    case S_COMMAND_UPDATE_CARD_DESC:
    case S_COMMAND_ANYTIME_SKILL_DONE:
    case S_COMMAND_SET_SHOWN_HANDCARD:
    case S_COMMAND_SET_BROKEN_EQUIP:
    case S_COMMAND_PRESHOW:
    case S_COMMAND_MIRROR_GUANXING_STEP:
        return ClientFlowDisposition::StateMutation;
    default:
        return ClientFlowDisposition::Unclassified;
    }
}

ClientStateReduction ClientGameStateReducer::applyNotification(
    ClientGameState *state, int command, const QVariant &payload)
{
    ClientStateReduction result;
    result.disposition = classifyNotification(command);
    if (state == nullptr) {
        result.detail = QStringLiteral("ClientGameState output is null");
        return result;
    }
    if (result.disposition == ClientFlowDisposition::Unclassified) {
        result.detail = QStringLiteral("unclassified room notification %1").arg(command);
        return result;
    }

    if (payload.userType() != QMetaType::QVariantMap) {
        result.detail = QStringLiteral("room notification %1 payload is not a typed object")
            .arg(command);
        return result;
    }
    const QVariantMap object = payload.toMap();
    bool schemaOk = false;
    const int schemaVersion = object.value(QStringLiteral("schema_version")).toInt(&schemaOk);
    if (!schemaOk || schemaVersion <= 0) {
        result.detail = QStringLiteral("room notification %1 has no valid schema_version")
            .arg(command);
        return result;
    }

    state->recordFlow(command, payload);
    if (result.disposition == ClientFlowDisposition::PresentationEvent
        || result.disposition == ClientFlowDisposition::ExplicitTextIrrelevant) {
        if (command == S_COMMAND_SPEAK) {
            result.eventText = QStringLiteral("%1: %2")
                .arg(object.value(QStringLiteral("speaker")).toString(),
                     object.value(QStringLiteral("text")).toString());
        } else if (command == S_COMMAND_WARN) {
            result.eventText = object.value(QStringLiteral("message")).toString();
        } else if (command == S_COMMAND_LOG_SKILL) {
            result.eventText = QStringLiteral("%1 %2")
                .arg(object.value(QStringLiteral("log_type")).toString(),
                     object.value(QStringLiteral("from_player")).toString());
        } else if (command == S_COMMAND_SET_EMOTION) {
            result.eventText = QStringLiteral("%1 emotion %2")
                .arg(object.value(QStringLiteral("player_name")).toString(),
                     object.value(QStringLiteral("emotion")).toString());
        } else if (command == S_COMMAND_INVOKE_SKILL) {
            result.eventText = QStringLiteral("%1 invoked %2")
                .arg(object.value(QStringLiteral("player_name")).toString(),
                     object.value(QStringLiteral("skill_name")).toString());
        } else {
            result.eventText = QStringLiteral("presentation event %1").arg(command);
        }
        state->appendPresentationEvent(command, result.eventText, payload);
        result.success = true;
        return result;
    }

    switch (command) {
    case S_COMMAND_ADD_PLAYER:
    case S_COMMAND_ADD_PLAYER_DYNAMIC: {
        const QString name = resolvePlayerName(
            state, object.value(QStringLiteral("player_name")));
        state->addPlayer(name);
        state->setPlayerValue(name, QStringLiteral("screen_name"),
                              object.value(QStringLiteral("screen_name")));
        state->setPlayerValue(name, QStringLiteral("avatar"),
                              object.value(QStringLiteral("avatar")));
        break;
    }
    case S_COMMAND_REMOVE_PLAYER:
        state->removePlayer(resolvePlayerName(
            state, object.value(QStringLiteral("player_name"))));
        break;
    case S_COMMAND_ARRANGE_SEATS: {
        const QStringList names = strings(object.value(QStringLiteral("player_names")));
        state->setPlayerNames(names);
        for (int i = 0; i < names.size(); ++i)
            state->setPlayerValue(names.at(i), QStringLiteral("seat"), i + 1);
        break;
    }
    case S_COMMAND_START_IN_X_SECONDS:
        state->setGameValue(QStringLiteral("starts_in_seconds"),
                            object.value(QStringLiteral("seconds")));
        break;
    case S_COMMAND_GAME_START:
        state->setGameValue(QStringLiteral("started"), true);
        state->setGameValue(QStringLiteral("game_over"), false);
        state->setGameValue(QStringLiteral("status"), QStringLiteral("active"));
        state->setGameValue(QStringLiteral("draw_pile"), object.value(QStringLiteral("card_ids")));
        state->setGameValue(QStringLiteral("draw_pile_count"),
                            object.value(QStringLiteral("card_ids")).toList().size());
        break;
    case S_COMMAND_GAME_OVER:
        state->setGameValue(QStringLiteral("game_over"), true);
        state->setGameValue(QStringLiteral("status"), QStringLiteral("game_over"));
        state->setGameValue(QStringLiteral("result"), object);
        break;
    case S_COMMAND_CHANGE_HP: {
        const QString name = resolvePlayerName(
            state, object.value(QStringLiteral("player_name")));
        const int hp = state->playerValue(name, QStringLiteral("hp")).toInt()
            + object.value(QStringLiteral("delta")).toInt();
        state->setPlayerValue(name, QStringLiteral("hp"), hp);
        break;
    }
    case S_COMMAND_CHANGE_MAXHP: {
        const QString name = resolvePlayerName(
            state, object.value(QStringLiteral("player_name")));
        const int hp = state->playerValue(name, QStringLiteral("max_hp")).toInt()
            + object.value(QStringLiteral("delta")).toInt();
        state->setPlayerValue(name, QStringLiteral("max_hp"), hp);
        break;
    }
    case S_COMMAND_KILL_PLAYER:
        state->setPlayerAlive(resolvePlayerName(
            state, object.value(QStringLiteral("player_name"))), false);
        break;
    case S_COMMAND_REVIVE_PLAYER:
        state->setPlayerAlive(resolvePlayerName(
            state, object.value(QStringLiteral("player_name"))), true);
        break;
    case S_COMMAND_SET_MARK:
        state->setPlayerMark(resolvePlayerName(
            state, object.value(QStringLiteral("player_name"))),
            object.value(QStringLiteral("mark_name")).toString(),
            object.value(QStringLiteral("value")).toInt());
        break;
    case S_COMMAND_SHOW_CARD:
    case S_COMMAND_SHOW_ALL_CARDS:
        setVisibleCards(state, resolvePlayerName(
                            state, object.value(QStringLiteral("player_name"))),
                        object.value(QStringLiteral("card_ids")),
                        QStringLiteral("shown_cards"));
        break;
    case S_COMMAND_SHOW_VIRTUAL_CARD:
        state->setGameValue(QStringLiteral("last_virtual_card"), object);
        break;
    case S_COMMAND_CARD_PROVENANCE:
        state->setGameValue(QStringLiteral("last_card_provenance"), object);
        break;
    case S_COMMAND_SKILL_GONGXIN:
        state->setGameValue(QStringLiteral("gongxin"), object);
        break;
    case S_COMMAND_UPDATE_PLAYER_UI_STATE: {
        const QString player = resolvePlayerName(
            state, object.value(QStringLiteral("player_name")));
        const QVariantMap uiState = object.value(QStringLiteral("state")).toMap();
        state->setPlayerValue(player, QStringLiteral("ui_state"), uiState);
        state->setPlayerValue(player, QStringLiteral("hand_max"),
                              uiState.value(QStringLiteral("handMax")));
        state->setPlayerValue(player, QStringLiteral("offensive_distance"),
                              uiState.value(QStringLiteral("offensiveDistance")));
        state->setPlayerValue(player, QStringLiteral("defensive_distance"),
                              uiState.value(QStringLiteral("defensiveDistance")));
        static const QStringList skillFields{QStringLiteral("maxCardsSkills"),
            QStringLiteral("offensiveSkills"), QStringLiteral("defensiveSkills"),
            QStringLiteral("viewAsEquipSkills")};
        for (const QString &field : skillFields) {
            for (const QString &skill : strings(uiState.value(field)))
                appendSkill(state, player, skill);
        }
        break;
    }
    case S_COMMAND_ATTACH_SKILL:
        appendSkill(state, resolvePlayerName(
                        state, object.value(QStringLiteral("player_name"))),
                    object.value(QStringLiteral("skill_name")).toString());
        break;
    case S_COMMAND_SKILL_INSTANCE: {
        const QString action = object.value(QStringLiteral("action")).toString();
        if (action == QLatin1String("snapshot")) {
            for (const QString &player : state->playerNames())
                state->setPlayerValue(player, QStringLiteral("skill_instances"), QVariantMap());
            for (const QVariant &entryValue : object.value(QStringLiteral("entries")).toList()) {
                storeSkillInstance(state, entryValue.toMap());
            }
        } else if (action == QLatin1String("upsert")) {
            storeSkillInstance(state, object.value(QStringLiteral("entry")).toMap());
        } else {
            const QString player = resolvePlayerName(
                state, object.value(QStringLiteral("owner_name")));
            const QString skill = object.value(QStringLiteral("skill_name")).toString();
            const int instanceId = object.value(QStringLiteral("instance_id")).toInt();
            QVariantMap instances = state->playerValue(
                player, QStringLiteral("skill_instances")).toMap();
            const QString key = skillInstanceKey(skill, instanceId);
            if (action == QLatin1String("remove")) {
                instances.remove(key);
                bool stillPresent = false;
                for (const QVariant &value : instances) {
                    if (value.toMap().value(QStringLiteral("skill_name")).toString() == skill) {
                        stillPresent = true;
                        break;
                    }
                }
                if (!stillPresent)
                    removeSkill(state, player, skill);
            } else {
                QVariantMap entry = instances.value(key).toMap();
                if (action == QLatin1String("amount")) {
                    entry.insert(QStringLiteral("has_amount_override"),
                                 object.value(QStringLiteral("has_amount_override")));
                    entry.insert(QStringLiteral("amount"), object.value(QStringLiteral("amount")));
                } else if (action == QLatin1String("correct_state")
                           || action == QLatin1String("state")) {
                    const QString field = action == QLatin1String("state")
                        ? QStringLiteral("state") : QStringLiteral("correct_state");
                    QVariantMap values = entry.value(field).toMap();
                    const QString operation = object.value(QStringLiteral("operation")).toString();
                    const QString stateKey = object.value(QStringLiteral("key")).toString();
                    if (operation == QLatin1String("clear"))
                        values.clear();
                    else if (operation == QLatin1String("replace"))
                        values = object.value(QStringLiteral("value")).toMap();
                    else if (operation == QLatin1String("remove"))
                        values.remove(stateKey);
                    else
                        values.insert(stateKey, object.value(QStringLiteral("value")));
                    entry.insert(field, values);
                }
                if (!entry.isEmpty())
                    instances.insert(key, entry);
            }
            state->setPlayerValue(player, QStringLiteral("skill_instances"), instances);
        }
        break;
    }
    case S_COMMAND_SET_PROPERTY:
        applyPlayerProperty(state, object);
        break;
    case S_COMMAND_GET_CARD:
    case S_COMMAND_LOSE_CARD:
        applyCardMovement(state, command, object);
        break;
    case S_COMMAND_UPDATE_CARD: {
        const int cardId = object.value(QStringLiteral("card_id")).toInt();
        if (object.value(QStringLiteral("action")).toString() == QLatin1String("reset")) {
            state->setCardValue(cardId, QStringLiteral("modified"), false);
        } else {
            for (auto it = object.constBegin(); it != object.constEnd(); ++it)
                state->setCardValue(cardId, it.key(), it.value());
            state->setCardValue(cardId, QStringLiteral("modified"), true);
        }
        break;
    }
    case S_COMMAND_CARD_MARK: {
        const int cardId = object.value(QStringLiteral("card_id")).toInt();
        QVariantMap marks = state->card(cardId).value(QStringLiteral("marks")).toMap();
        const QString mark = object.value(QStringLiteral("mark_name")).toString();
        const QVariant value = object.value(QStringLiteral("value"));
        // removeCardMark() reaches zero and stops (card-state-service.cpp:39),
        // and Card::setMark() drops the entry rather than storing a zero
        // (card.cpp:948) -- the same rule ClientGameState::setPlayerMark
        // already follows for a player.
        if (value.toInt() == 0)
            marks.remove(mark);
        else
            marks.insert(mark, value);
        state->setCardValue(cardId, QStringLiteral("marks"), marks);
        break;
    }
    case S_COMMAND_CARD_FLAG: {
        const int cardId = object.value(QStringLiteral("card_id")).toInt();
        QStringList flags = state->card(cardId).value(QStringLiteral("flags")).toStringList();
        const QString flag = object.value(QStringLiteral("flag")).toString();
        // Card::setFlags() clears on "." (card.cpp:998), and the server sends
        // one every time a card moves (card-movement-service.cpp:169) or a
        // response is resolved. Appending it would leave visible and cardTip:
        // flags on the card for the rest of the game.
        if (flag == QLatin1String(".")) {
            state->setCardValue(cardId, QStringLiteral("flags"), QStringList());
            // A card's marks are flags in the engine ("cardMark:<name>:<value>",
            // card.cpp:942), so clearing the flags clears them too. They are a
            // separate map here and would otherwise survive.
            state->setCardValue(cardId, QStringLiteral("marks"), QVariantMap());
            break;
        }
        appendOrRemove(&flags, flag.startsWith(QLatin1Char('-')) ? flag.mid(1) : flag,
                       !flag.startsWith(QLatin1Char('-')));
        state->setCardValue(cardId, QStringLiteral("flags"), flags);
        break;
    }
    case S_COMMAND_UPDATE_PILE:
        state->setGameValue(QStringLiteral("draw_pile_count"), object.value(QStringLiteral("count")));
        break;
    case S_COMMAND_RESET_PILE:
        state->setGameValue(QStringLiteral("swap_count"), object.value(QStringLiteral("swap_count")));
        break;
    case S_COMMAND_SYNCHRONIZE_DISCARD_PILE:
        state->setGameValue(QStringLiteral("discard_pile"), object.value(QStringLiteral("card_ids")));
        break;
    case S_COMMAND_SYNC_PILE: {
        const QString player = resolvePlayerName(
            state, object.value(QStringLiteral("player_name")));
        QVariantMap piles = state->playerValue(player, QStringLiteral("piles")).toMap();
        piles.insert(object.value(QStringLiteral("pile_name")).toString(),
                     object.value(QStringLiteral("card_ids")));
        state->setPlayerValue(player, QStringLiteral("piles"), piles);
        break;
    }
    case S_COMMAND_SET_KNOWN_CARDS:
        setVisibleCards(state, resolvePlayerName(
                            state, object.value(QStringLiteral("player_name"))),
                        object.value(QStringLiteral("card_ids")),
                        QStringLiteral("known_cards"));
        break;
    case S_COMMAND_EXCHANGE_KNOWN_CARDS: {
        const QString first = resolvePlayerName(
            state, object.value(QStringLiteral("first_player")));
        const QString second = resolvePlayerName(
            state, object.value(QStringLiteral("second_player")));
        const QVariant firstCards = state->playerValue(first, QStringLiteral("known_cards"));
        state->setPlayerValue(first, QStringLiteral("known_cards"),
                              state->playerValue(second, QStringLiteral("known_cards")));
        state->setPlayerValue(second, QStringLiteral("known_cards"), firstCards);
        break;
    }
    case S_COMMAND_SET_SHOWN_HANDCARD:
        setVisibleCards(state, resolvePlayerName(
                            state, object.value(QStringLiteral("player_name"))),
                        object.value(QStringLiteral("card_ids")),
                        QStringLiteral("shown_hand_cards"));
        break;
    case S_COMMAND_SET_BROKEN_EQUIP:
        state->setPlayerValue(resolvePlayerName(
                                  state, object.value(QStringLiteral("player_name"))),
            QStringLiteral("broken_equipment"), object.value(QStringLiteral("card_ids")));
        break;
    case S_COMMAND_MOVE_FOCUS: {
        QStringList players;
        for (const QString &player : strings(object.value(QStringLiteral("player_names"))))
            players.append(resolvePlayerName(state, player));
        state->setGameValue(QStringLiteral("focus"), players);
        state->setGameValue(QStringLiteral("focus_countdown"),
                            object.value(QStringLiteral("countdown")));
        break;
    }
    case S_COMMAND_ADD_HISTORY: {
        QString player = resolvePlayerName(
            state, object.value(QStringLiteral("player_name")));
        if (player.isEmpty())
            player = state->selfName();
        QVariantMap history = state->playerValue(
            player, QStringLiteral("history")).toMap();
        const QString name = object.value(QStringLiteral("history_name")).toString();
        const int times = object.value(QStringLiteral("times")).toInt();
        if (name == QLatin1String("."))
            history.clear();
        else if (times == 0)
            history.remove(name);
        else
            history.insert(name, history.value(name).toInt() + times);
        state->setPlayerValue(player, QStringLiteral("history"), history);
        break;
    }
    case S_COMMAND_FIXED_DISTANCE: {
        const QString from = resolvePlayerName(
            state, object.value(QStringLiteral("from_player")));
        const QString to = resolvePlayerName(
            state, object.value(QStringLiteral("to_player")));
        QVariantMap distances = state->playerValue(
            from, QStringLiteral("fixed_distances")).toMap();
        if (object.value(QStringLiteral("set")).toBool())
            distances.insert(to, object.value(QStringLiteral("distance")));
        else
            distances.remove(to);
        state->setPlayerValue(from, QStringLiteral("fixed_distances"), distances);
        break;
    }
    case S_COMMAND_ATTACK_RANGE: {
        const QString from = resolvePlayerName(
            state, object.value(QStringLiteral("from_player")));
        const QString to = resolvePlayerName(
            state, object.value(QStringLiteral("to_player")));
        QStringList pairs = state->playerValue(
            from, QStringLiteral("attack_range_pairs")).toStringList();
        appendOrRemove(&pairs, to, object.value(QStringLiteral("set")).toBool());
        state->setPlayerValue(from, QStringLiteral("attack_range_pairs"), pairs);
        break;
    }
    case S_COMMAND_CARD_LIMITATION: {
        const QString player = state->selfName();
        QVariantList limitations = state->playerValue(
            player, QStringLiteral("card_limitations")).toList();
        const QString action = object.value(QStringLiteral("action")).toString();
        if (action == QLatin1String("clear")) {
            if (!object.value(QStringLiteral("single_turn")).toBool()) {
                limitations.clear();
            } else {
                for (qsizetype i = limitations.size() - 1; i >= 0; --i) {
                    if (limitations.at(i).toMap().value(
                            QStringLiteral("single_turn")).toBool())
                        limitations.removeAt(i);
                }
            }
        } else if (action == QLatin1String("remove_by_reason")) {
            const QString reason = object.value(QStringLiteral("reason")).toString();
            for (qsizetype i = limitations.size() - 1; i >= 0; --i) {
                if (limitations.at(i).toMap().value(
                        QStringLiteral("reason")).toString() == reason)
                    limitations.removeAt(i);
            }
        } else {
            QVariantMap entry = object;
            entry.remove(QStringLiteral("schema_version"));
            entry.remove(QStringLiteral("action"));
            const auto matches = [&entry](const QVariant &value) {
                QVariantMap candidate = value.toMap();
                candidate.remove(QStringLiteral("schema_version"));
                candidate.remove(QStringLiteral("action"));
                return candidate == entry;
            };
            if (action == QLatin1String("set")) {
                bool exists = false;
                for (const QVariant &value : limitations)
                    exists = exists || matches(value);
                if (!exists)
                    limitations.append(entry);
            } else if (action == QLatin1String("remove")) {
                for (qsizetype i = limitations.size() - 1; i >= 0; --i) {
                    if (matches(limitations.at(i)))
                        limitations.removeAt(i);
                }
            }
        }
        state->setPlayerValue(player, QStringLiteral("card_limitations"), limitations);
        break;
    }
    case S_COMMAND_NULLIFICATION_ASKED:
        state->setGameValue(QStringLiteral("nullification_trick"),
                            object.value(QStringLiteral("trick_name")));
        break;
    case S_COMMAND_ENABLE_SURRENDER:
        state->setGameValue(QStringLiteral("surrender_enabled"),
                            object.value(QStringLiteral("enabled")));
        break;
    case S_COMMAND_OPERATION_TIMEOUT:
        state->setGameValue(QStringLiteral("operation_timeout_ms"),
                            object.value(QStringLiteral("timeout_ms")));
        break;
    case S_COMMAND_SWITCH_CONTEXT:
        state->setGameValue(QStringLiteral("current_player"), resolvePlayerName(
            state, object.value(QStringLiteral("player_name"))));
        break;
    case S_COMMAND_VIEW_GENERALS:
        state->setGameValue(QStringLiteral("view_generals"), object);
        break;
    case S_COMMAND_FILL_AMAZING_GRACE:
        state->setGameValue(QStringLiteral("amazing_grace"), object);
        break;
    case S_COMMAND_TAKE_AMAZING_GRACE:
    {
        state->setGameValue(QStringLiteral("last_amazing_grace_take"), object);
        QVariantMap grace = state->gameValue(QStringLiteral("amazing_grace")).toMap();
        QVariantList cards = grace.value(QStringLiteral("card_ids")).toList();
        cards.removeAll(object.value(QStringLiteral("card_id")));
        grace.insert(QStringLiteral("card_ids"), cards);
        state->setGameValue(QStringLiteral("amazing_grace"), grace);
        break;
    }
    case S_COMMAND_CLEAR_AMAZING_GRACE:
        state->setGameValue(QStringLiteral("amazing_grace"), QVariantMap());
        break;
    case S_COMMAND_FILL_GENERAL:
        state->setGameValue(QStringLiteral("general_pool"), object.value(QStringLiteral("general_names")));
        break;
    case S_COMMAND_TAKE_GENERAL: {
        QStringList pool = strings(state->gameValue(QStringLiteral("general_pool")));
        pool.removeAll(object.value(QStringLiteral("general_name")).toString());
        state->setGameValue(QStringLiteral("general_pool"), pool);
        state->setGameValue(QStringLiteral("last_general_take"), object);
        break;
    }
    case S_COMMAND_RECOVER_GENERAL: {
        QStringList pool = strings(state->gameValue(QStringLiteral("general_pool")));
        const int index = object.value(QStringLiteral("index")).toInt();
        if (index >= 0 && index < pool.size())
            pool[index] = object.value(QStringLiteral("general_name")).toString();
        else
            pool.append(object.value(QStringLiteral("general_name")).toString());
        state->setGameValue(QStringLiteral("general_pool"), pool);
        break;
    }
    case S_COMMAND_REVEAL_GENERAL: {
        const QString player = resolvePlayerName(
            state, object.value(QStringLiteral("player_name")));
        state->setPlayerValue(player, QStringLiteral("revealed_general"),
                              object.value(QStringLiteral("general_name")));
        break;
    }
    case S_COMMAND_UPDATE_SKILL:
        state->setGameValue(QStringLiteral("last_updated_skill"),
                            object.value(QStringLiteral("skill_name")));
        break;
    case S_COMMAND_ADD_ROUND:
        state->setGameValue(QStringLiteral("round"), state->gameValue(QStringLiteral("round")).toInt() + 1);
        break;
    case S_COMMAND_AVAILABLE_CARDS:
        state->setGameValue(QStringLiteral("available_cards"), object.value(QStringLiteral("card_ids")));
        break;
    case S_COMMAND_UPDATE_STATE_ITEM:
        state->setGameValue(QStringLiteral("state_item"), object.value(QStringLiteral("state")));
        break;
    case S_COMMAND_UPDATE_BOSS_LEVEL:
        state->setGameValue(QStringLiteral("boss_level"), object.value(QStringLiteral("level")));
        break;
    case S_COMMAND_SKILL_DESCRIPTION_SWAP: {
        const QString player = resolvePlayerName(
            state, object.value(QStringLiteral("player_name")));
        QVariantMap descriptions = state->playerValue(
            player, QStringLiteral("skill_descriptions")).toMap();
        const QString skillKey = skillInstanceKey(
            object.value(QStringLiteral("skill_name")).toString(),
            object.value(QStringLiteral("instance_id")).toInt());
        QVariantMap values = descriptions.value(skillKey).toMap();
        values.insert(object.value(QStringLiteral("key")).toString(),
                      object.value(QStringLiteral("value")));
        descriptions.insert(skillKey, values);
        state->setPlayerValue(player, QStringLiteral("skill_descriptions"), descriptions);
        break;
    }
    case S_COMMAND_ADD_EQUIP_AREA: {
        const QString player = resolvePlayerName(
            state, object.value(QStringLiteral("player_name")));
        QVariantMap areas = state->playerValue(player, QStringLiteral("equip_areas")).toMap();
        areas.insert(object.value(QStringLiteral("area")).toString(), 1);
        state->setPlayerValue(player, QStringLiteral("equip_areas"), areas);
        break;
    }
    case S_COMMAND_SET_EQUIP_AREA_COUNT: {
        const QString player = resolvePlayerName(
            state, object.value(QStringLiteral("player_name")));
        QVariantMap areas = state->playerValue(player, QStringLiteral("equip_areas")).toMap();
        areas.insert(object.value(QStringLiteral("area")).toString(),
                     object.value(QStringLiteral("count")));
        state->setPlayerValue(player, QStringLiteral("equip_areas"), areas);
        break;
    }
    case S_COMMAND_UPDATE_CARD_DESC: {
        const QString player = resolvePlayerName(
            state, object.value(QStringLiteral("player_name")));
        QVariantMap descriptions = state->playerValue(
            player, QStringLiteral("card_descriptions")).toMap();
        QVariantMap values = descriptions.value(
            object.value(QStringLiteral("card_name")).toString()).toMap();
        values.insert(object.value(QStringLiteral("key")).toString(),
                      object.value(QStringLiteral("value")));
        descriptions.insert(object.value(QStringLiteral("card_name")).toString(), values);
        state->setPlayerValue(player, QStringLiteral("card_descriptions"), descriptions);
        break;
    }
    case S_COMMAND_ANYTIME_SKILL_DONE: {
        QStringList pending = state->playerValue(
            state->selfName(), QStringLiteral("pending_anytime_skills")).toStringList();
        pending.removeAll(object.value(QStringLiteral("skill_name")).toString());
        state->setPlayerValue(state->selfName(),
                              QStringLiteral("pending_anytime_skills"), pending);
        break;
    }
    case S_COMMAND_WEAPON_RANGE: {
        QVariantMap ranges = state->gameValue(QStringLiteral("weapon_ranges")).toMap();
        ranges.insert(object.value(QStringLiteral("weapon_name")).toString(),
                      object.value(QStringLiteral("range")));
        state->setGameValue(QStringLiteral("weapon_ranges"), ranges);
        break;
    }
    case S_COMMAND_MIRROR_GUANXING_STEP: {
        const QString action = object.value(QStringLiteral("action")).toString();
        if (action == QLatin1String("finish")) {
            state->setGameValue(QStringLiteral("guanxing"), QVariantMap());
        } else {
            QVariantMap guanxing = state->gameValue(QStringLiteral("guanxing")).toMap();
            if (action == QLatin1String("start")) {
                guanxing = object;
                guanxing.insert(QStringLiteral("moves"), QVariantList());
            } else if (action == QLatin1String("move")) {
                QVariantList moves = guanxing.value(QStringLiteral("moves")).toList();
                moves.append(object);
                guanxing.insert(QStringLiteral("moves"), moves);
            }
            state->setGameValue(QStringLiteral("guanxing"), guanxing);
        }
        break;
    }
    case S_COMMAND_PRESHOW:
        state->setPlayerValue(resolvePlayerName(
                                  state, object.value(QStringLiteral("player_name"))),
                              QStringLiteral("preshow"),
                              object.value(QStringLiteral("states")));
        break;
    case S_COMMAND_STATE_SYNC:
        state->setConnectionValue(QStringLiteral("sync_id"), object.value(QStringLiteral("sync_id")));
        state->setConnectionValue(QStringLiteral("sync_phase"), object.value(QStringLiteral("phase")));
        break;
    case S_COMMAND_NETWORK_DELAY_TEST:
        state->setConnectionValue(QStringLiteral("delay_nonce"),
                                  object.value(QStringLiteral("nonce")));
        break;
    default:
        // Every registered state-bearing command remains observable through
        // latestPayload/flowCount even when no compact semantic projection is needed.
        break;
    }

    result.success = true;
    return result;
}
