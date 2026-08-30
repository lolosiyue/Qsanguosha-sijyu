#include "replay-game-state.h"

#include "protocol/session/session-payloads.h"
#include "game-snapshot.h"
#include "protocol/card-provenance-message.h"
#include "protocol/protocol-message-utils.h"
#include "protocol.h"
#include "json.h"

#include <QFile>

using namespace QSanProtocol;

namespace {

bool typedObject(const QVariant &value, QVariantMap *object)
{
    if (object == nullptr || value.userType() != QMetaType::QVariantMap)
        return false;
    const QVariantMap parsed = value.toMap();
    int schemaVersion = 0;
    if (!ProtocolMessageUtils::tryParseInt(
            parsed.value(QStringLiteral("schema_version")), schemaVersion)
        || schemaVersion != 1) {
        return false;
    }
    *object = parsed;
    return true;
}

}

ReplayGameState::ReplayGameState(QObject *parent)
    : QObject(parent)
{
}

bool ReplayGameState::rebuildFromEvents(
    const QList<QSanReplay::ReplayEvent> &events, int upToIndex)
{
    clear();

    const int limit = (upToIndex < 0 || upToIndex >= events.size())
        ? events.size() : upToIndex + 1;

    for (int i = 0; i < limit; i++) {
        if (!applyMessage(events.at(i).message)) {
            continue;
        }
    }

    return validateState();
}

bool ReplayGameState::applyMessage(const ProtocolMessage &message)
{
    const CommandType commandType = static_cast<CommandType>(message.command);
    const QVariant &body = message.payload;

    switch (commandType) {
    case S_COMMAND_SETUP:
        return processSetup(body);
    case S_COMMAND_ADD_PLAYER:
        return processAddPlayer(body);
    case S_COMMAND_REMOVE_PLAYER:
        return processRemovePlayer(body);
    case S_COMMAND_SET_PROPERTY:
        return processSetProperty(body);
    case S_COMMAND_SET_MARK:
        return processSetMark(body);
    case S_COMMAND_GET_CARD:
    case S_COMMAND_LOSE_CARD:
        return processMoveCards(body);
    case S_COMMAND_CHANGE_HP:
        return processChangeHp(body);
    case S_COMMAND_GAME_OVER:
        return processGameOver(body);
    case S_COMMAND_LOG_SKILL:
        return processLogSkill(body);
    case S_COMMAND_CARD_PROVENANCE:
        return processCardProvenance(body);
    default:
        break;
    }

    return true;
}

bool ReplayGameState::applySnapshot(GameSnapshot *snapshot)
{
    if (!snapshot)
        return false;

    clear();
    m_state = snapshot->getState();

    foreach (const PlayerSnapshot &p, m_state.players) {
        m_playerMap[p.objectName] = const_cast<PlayerSnapshot*>(&m_state.players[m_state.players.indexOf(p)]);

        foreach (int cardId, p.handcards) {
            updateCardMapping(cardId, p.objectName, "hand");
        }
        foreach (int cardId, p.equips) {
            updateCardMapping(cardId, p.objectName, "equip");
        }
        foreach (int cardId, p.judgingArea) {
            updateCardMapping(cardId, p.objectName, "judge");
        }
        foreach (const QString &pileName, p.piles.keys()) {
            foreach (int cardId, p.piles[pileName]) {
                updateCardMapping(cardId, p.objectName, pileName);
            }
        }
    }

    foreach (int cardId, m_state.drawPile) {
        updateCardMapping(cardId, QString(), "draw");
    }
    foreach (int cardId, m_state.discardPile) {
        updateCardMapping(cardId, QString(), "discard");
    }

    return true;
}

PlayerSnapshot* ReplayGameState::getPlayerState(const QString &playerName)
{
    if (m_playerMap.contains(playerName))
        return m_playerMap[playerName];
    return nullptr;
}

GlobalSnapshot ReplayGameState::getGlobalState() const
{
    return m_state;
}

int ReplayGameState::getCardPosition(int cardId) const
{
    if (m_cardPileMap.contains(cardId)) {
        QString pile = m_cardPileMap[cardId];
        if (pile == "hand") return 0;
        if (pile == "equip") return 1;
        if (pile == "judge") return 2;
        if (pile == "draw") return 3;
        if (pile == "discard") return 4;
    }
    return -1;
}

QString ReplayGameState::getCardOwner(int cardId) const
{
    return m_cardOwnerMap.value(cardId, QString());
}

QString ReplayGameState::getCardPile(int cardId) const
{
    return m_cardPileMap.value(cardId, QString());
}

bool ReplayGameState::validateState() const
{
    return !m_state.players.isEmpty();
}

void ReplayGameState::clear()
{
    m_state = GlobalSnapshot();
    m_cardOwnerMap.clear();
    m_cardPileMap.clear();
    m_playerMap.clear();
    m_cardProvenance.clear();
}

int ReplayGameState::getTurnCount() const
{
    return m_state.turnCount;
}

QString ReplayGameState::getCurrentPlayer() const
{
    return m_state.currentPlayer;
}

bool ReplayGameState::processSetup(const QVariant &body)
{
    QSanProtocol::SetupPayload setup;
    if (!QSanProtocol::SetupPayload::parse(body, &setup))
        return false;
    m_state.gameMode = setup.gameMode;
    return true;
}

bool ReplayGameState::processAddPlayer(const QVariant &body)
{
    QVariantMap object;
    QString name;
    QString screenName;
    if (!typedObject(body, &object)
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("player_name")), name)
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("screen_name")), screenName)) {
        return false;
    }

    PlayerSnapshot p;
    p.objectName = name;
    p.screenName = screenName;
    p.alive = true;
    p.hp = 4;
    p.maxhp = 4;

    m_state.players.append(p);
    m_playerMap[name] = &m_state.players.last();
    m_state.seatOrder.append(name);

    return true;
}

bool ReplayGameState::processRemovePlayer(const QVariant &body)
{
    QVariantMap object;
    QString name;
    if (!typedObject(body, &object)
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("player_name")), name)) {
        return false;
    }
    m_playerMap.remove(name);
    m_state.seatOrder.removeAll(name);

    for (int i = 0; i < m_state.players.size(); i++) {
        if (m_state.players[i].objectName == name) {
            m_state.players.removeAt(i);
            break;
        }
    }

    return true;
}

bool ReplayGameState::processSetProperty(const QVariant &body)
{
    QVariantMap object;
    QString action;
    QString who;
    if (!typedObject(body, &object)
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("action")), action)
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("player_name")), who)) {
        return false;
    }
    if (action == QLatin1String("tag") || action == QLatin1String("general_pile"))
        return true;
    QString property;
    QString value;
    if (action != QLatin1String("property")
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("property_name")), property)
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("string_value")), value)) {
        return false;
    }

    QString targetName = (who == S_PLAYER_SELF_REFERENCE_ID) ? m_state.seatOrder.value(0, QString()) : who;
    PlayerSnapshot *p = getPlayerState(targetName);
    if (!p)
        return true;

    if (property == "general") {
        p->general = value;
    } else if (property == "general2") {
        p->general2 = value;
    } else if (property == "hp") {
        p->hp = value.toInt();
    } else if (property == "maxhp") {
        p->maxhp = value.toInt();
    } else if (property == "kingdom") {
        p->kingdom = value;
    } else if (property == "role") {
        p->role = value;
    } else if (property == "state") {
        p->state = value;
    }

    return true;
}

bool ReplayGameState::processSetMark(const QVariant &body)
{
    QVariantMap object;
    QString who;
    QString mark;
    int num = 0;
    if (!typedObject(body, &object)
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("player_name")), who)
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("mark_name")), mark)
        || !ProtocolMessageUtils::tryParseInt(
            object.value(QStringLiteral("value")), num)) {
        return false;
    }

    if (mark == "Global_TurnCount") {
        m_state.turnCount = num;
    }

    PlayerSnapshot *p = getPlayerState(who);
    if (p) {
        p->marks[mark] = num;
    }

    return true;
}

bool ReplayGameState::processMoveCards(const QVariant &body)
{
    QVariantMap object;
    if (!typedObject(body, &object)
        || object.value(QStringLiteral("moves")).userType() != QMetaType::QVariantList) {
        return false;
    }

    const QVariantList moves = object.value(QStringLiteral("moves")).toList();
    foreach (const QVariant &moveVar, moves) {
        if (moveVar.userType() != QMetaType::QVariantMap)
            return false;
        const QVariantMap move = moveVar.toMap();

        QList<int> cardIds;
        const QVariantList ids = move.value(QStringLiteral("card_ids")).toList();
        for (const QVariant &idVar : ids) {
            int cardId = 0;
            if (!ProtocolMessageUtils::tryParseInt(idVar, cardId))
                return false;
            cardIds << cardId;
        }
        QString fromPlayer;
        QString fromPile;
        QString toPlayer;
        QString toPile;
        int fromPlace = 0;
        int toPlace = 0;
        if (!ProtocolMessageUtils::tryParseString(
                move.value(QStringLiteral("from_player")), fromPlayer)
            || !ProtocolMessageUtils::tryParseString(
                move.value(QStringLiteral("from_pile")), fromPile)
            || !ProtocolMessageUtils::tryParseString(
                move.value(QStringLiteral("to_player")), toPlayer)
            || !ProtocolMessageUtils::tryParseString(
                move.value(QStringLiteral("to_pile")), toPile)
            || !ProtocolMessageUtils::tryParseInt(
                move.value(QStringLiteral("from_place")), fromPlace)
            || !ProtocolMessageUtils::tryParseInt(
                move.value(QStringLiteral("to_place")), toPlace)) {
            return false;
        }

        foreach (int cardId, cardIds) {
            if (!fromPlayer.isEmpty()) {
                PlayerSnapshot *from = getPlayerState(fromPlayer);
                if (from) {
                    if (fromPlace == Player::PlaceHand) {
                        from->handcards.removeAll(cardId);
                    } else if (fromPlace == Player::PlaceEquip) {
                        from->equips.removeAll(cardId);
                    } else if (fromPlace == Player::PlaceDelayedTrick) {
                        from->judgingArea.removeAll(cardId);
                    } else if (!fromPile.isEmpty()) {
                        if (from->piles.contains(fromPile)) {
                            from->piles[fromPile].removeAll(cardId);
                        }
                    }
                }
            }

            if (!toPlayer.isEmpty()) {
                PlayerSnapshot *to = getPlayerState(toPlayer);
                if (to) {
                    if (toPlace == Player::PlaceHand) {
                        to->handcards.append(cardId);
                    } else if (toPlace == Player::PlaceEquip) {
                        to->equips.append(cardId);
                    } else if (toPlace == Player::PlaceDelayedTrick) {
                        to->judgingArea.append(cardId);
                    } else if (!toPile.isEmpty()) {
                        to->piles[toPile].append(cardId);
                    }
                }
                updateCardMapping(cardId, toPlayer, toPile);
            } else {
                if (toPlace == Player::DrawPile) {
                    m_state.drawPile.append(cardId);
                } else if (toPlace == Player::DiscardPile) {
                    m_state.discardPile.append(cardId);
                }
                updateCardMapping(cardId, QString(), toPile);
            }
        }
    }

    return true;
}

bool ReplayGameState::processChangeHp(const QVariant &body)
{
    QVariantMap object;
    QString name;
    int hpChange = 0;
    if (!typedObject(body, &object)
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("player_name")), name)
        || !ProtocolMessageUtils::tryParseInt(
            object.value(QStringLiteral("delta")), hpChange)) {
        return false;
    }

    PlayerSnapshot *p = getPlayerState(name);
    if (p) {
        p->hp += hpChange;
        if (p->hp <= 0) {
            p->alive = false;
        }
    }

    return true;
}

bool ReplayGameState::processGameOver(const QVariant &body)
{
    QVariantMap object;
    if (!typedObject(body, &object)
        || object.value(QStringLiteral("winner_tokens")).userType() != QMetaType::QVariantList
        || object.value(QStringLiteral("roles")).userType() != QMetaType::QVariantList) {
        return false;
    }
    m_state.currentPlayer.clear();
    return true;
}

bool ReplayGameState::processLogSkill(const QVariant &body)
{
    QVariantMap object;
    QString type;
    QString from;
    QStringList tos;
    QStringList arguments;
    if (!typedObject(body, &object)
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("log_type")), type)
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("from_player")), from)
        || !JsonUtils::tryParse(object.value(QStringLiteral("to_players")), tos)
        || !JsonUtils::tryParse(object.value(QStringLiteral("arguments")), arguments)
        || arguments.size() != 5) {
        return false;
    }

    if (type.startsWith("#Damage")) {
        int damage = arguments.value(0).toInt();
        if (!from.isEmpty()) {
            PlayerSnapshot *p = getPlayerState(from);
            if (p) {
                p->marks["damage_total"] = p->marks.value("damage_total", 0) + damage;
            }
        }
        if (!tos.isEmpty()) {
            PlayerSnapshot *p = getPlayerState(tos.first());
            if (p) {
                p->marks["damaged_total"] = p->marks.value("damaged_total", 0) + damage;
            }
        }
    } else if (type == "#Murder" || type == "#Suicide") {
        if (!tos.isEmpty()) {
            PlayerSnapshot *victim = getPlayerState(tos.first());
            if (victim) {
                victim->alive = false;
            }
        }
        if (!from.isEmpty() && type == "#Murder") {
            PlayerSnapshot *killer = getPlayerState(from);
            if (killer) {
                killer->marks["kill_total"] = killer->marks.value("kill_total", 0) + 1;
            }
        }
    }

    return true;
}

void ReplayGameState::updateCardMapping(int cardId, const QString &owner, const QString &pile)
{
    m_cardOwnerMap[cardId] = owner;
    m_cardPileMap[cardId] = pile;
}

bool ReplayGameState::processCardProvenance(const QVariant &body)
{
    CardProvenanceMessage message;
    if (!message.tryParse(body))
        return false;

    QVariantMap record;
    record["version"] = message.version;
    record["kind"] = message.kind;
    record["initiator"] = message.initiator;
    record["card"] = message.card;
    record["sourceOwner"] = message.sourceOwner;
    record["sourceSkill"] = message.sourceSkill;
    record["sourceID"] = message.sourceInstanceId;
    record["activationOwner"] = message.activationOwner;
    record["activationSkill"] = message.activationSkill;
    record["activationID"] = message.activationInstanceId;
    m_cardProvenance << record;
    return true;
}
