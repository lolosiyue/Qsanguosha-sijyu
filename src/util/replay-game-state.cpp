#include "replay-game-state.h"
#include "game-snapshot.h"
#include "protocol.h"
#include "json.h"

#include <QFile>

using namespace QSanProtocol;

ReplayGameState::ReplayGameState(QObject *parent)
    : QObject(parent)
{
}

bool ReplayGameState::rebuildFromCommands(const QList<QPair<int, QString>> &pairs, int upToIndex)
{
    clear();

    int limit = (upToIndex < 0 || upToIndex >= pairs.size()) ? pairs.size() : upToIndex + 1;

    for (int i = 0; i < limit; i++) {
        const QString &cmd = pairs[i].second;
        if (!applyCommand(cmd)) {
            continue;
        }
    }

    return validateState();
}

bool ReplayGameState::applyCommand(const QString &cmd)
{
    Packet packet;
    if (!packet.parse(cmd.toLatin1().constData()))
        return false;

    CommandType commandType = packet.getCommandType();
    const QVariant &body = packet.getMessageBody();

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
    case S_COMMAND_MOVE_CARD:
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
    if (!JsonUtils::isString(body))
        return false;

    QString l = body.toString();
    static QRegExp rx("(.*):(@?\\w+):(\\d+):(\\d+):([+\\w-]*):([RCFSTBHAMN123a-r]*)(\\s+)?");
    if (!rx.exactMatch(l))
        return false;

    QStringList texts = rx.capturedTexts();
    m_state.gameMode = texts.at(2);
    return true;
}

bool ReplayGameState::processAddPlayer(const QVariant &body)
{
    JsonArray info = body.value<JsonArray>();
    if (info.size() < 2)
        return false;

    QString name = info[0].toString();
    QString screenName = QString::fromUtf8(QByteArray::fromBase64(info[1].toString().toLatin1()));

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
    QString name = body.toString();
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
    QStringList self_info;
    if (!JsonUtils::tryParse(body, self_info) || self_info.size() < 3)
        return false;

    const QString &who = self_info.at(0);
    const QString &property = self_info.at(1);
    const QString &value = self_info.at(2);

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
    JsonArray args = body.value<JsonArray>();
    if (args.size() < 3)
        return false;

    QString who = args.at(0).toString();
    QString mark = args.at(1).toString();
    int num = args.at(2).toInt();

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
    JsonArray args = body.value<JsonArray>();
    if (args.size() < 2)
        return false;

    JsonArray moves = args.at(0).value<JsonArray>();
    foreach (const QVariant &moveVar, moves) {
        JsonArray move = moveVar.value<JsonArray>();
        if (move.size() < 6)
            continue;

        QList<int> cardIds;
        JsonArray ids = move.at(0).value<JsonArray>();
        foreach (const QVariant &idVar, ids) {
            cardIds << idVar.toInt();
        }

        QString fromPlayer = move.at(2).toString();
        QString fromPile = move.at(3).toString();
        QString toPlayer = move.at(4).toString();
        QString toPile = move.at(5).toString();

        foreach (int cardId, cardIds) {
            if (!fromPlayer.isEmpty()) {
                PlayerSnapshot *from = getPlayerState(fromPlayer);
                if (from) {
                    if (fromPile == "hand" || fromPile.isEmpty()) {
                        from->handcards.removeAll(cardId);
                    } else if (fromPile == "equip") {
                        from->equips.removeAll(cardId);
                    } else if (fromPile == "judge") {
                        from->judgingArea.removeAll(cardId);
                    } else {
                        if (from->piles.contains(fromPile)) {
                            from->piles[fromPile].removeAll(cardId);
                        }
                    }
                }
            }

            if (!toPlayer.isEmpty()) {
                PlayerSnapshot *to = getPlayerState(toPlayer);
                if (to) {
                    if (toPile == "hand" || toPile.isEmpty()) {
                        to->handcards.append(cardId);
                    } else if (toPile == "equip") {
                        to->equips.append(cardId);
                    } else if (toPile == "judge") {
                        to->judgingArea.append(cardId);
                    } else {
                        to->piles[toPile].append(cardId);
                    }
                }
                updateCardMapping(cardId, toPlayer, toPile);
            } else {
                if (toPile == "draw") {
                    m_state.drawPile.append(cardId);
                } else if (toPile == "discard") {
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
    JsonArray change = body.value<JsonArray>();
    if (change.size() < 3)
        return false;

    QString name = change[0].toString();
    int hpChange = change[1].toInt();

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
    JsonArray args = body.value<JsonArray>();
    if (args.size() >= 2) {
        QString winners = args.at(0).toString();
        m_state.currentPlayer = QString();
    }
    return true;
}

bool ReplayGameState::processLogSkill(const QVariant &body)
{
    QStringList log;
    if (!JsonUtils::tryParse(body, log) || log.size() < 3)
        return false;

    const QString &type = log.at(0);
    const QString &from = log.at(1);
    QStringList tos = log.at(2).split('+');

    if (type.startsWith("#Damage")) {
        int damage = log.value(4).toInt();
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
    JsonArray args = body.value<JsonArray>();
    if (!JsonUtils::isNumber(args.value(0)))
        return false;
    const int version = args[0].toInt();
    const bool v1 = version == 1 && args.size() == 8;
    const bool v2 = version == 2 && args.size() == 10;
    if ((!v1 && !v2) || !JsonUtils::isString(args[1]) || !JsonUtils::isString(args[2])
        || !JsonUtils::isString(args[3]))
        return false;
    const int sourceSkillIndex = v2 ? 5 : 4;
    const int sourceIdIndex = v2 ? 6 : 5;
    const int activationSkillIndex = v2 ? 8 : 6;
    const int activationIdIndex = v2 ? 9 : 7;
    if ((v2 && (!JsonUtils::isString(args[4]) || !JsonUtils::isString(args[7])))
        || !JsonUtils::isString(args[sourceSkillIndex]) || !JsonUtils::isNumber(args[sourceIdIndex])
        || !JsonUtils::isString(args[activationSkillIndex]) || !JsonUtils::isNumber(args[activationIdIndex]))
        return false;

    QVariantMap record;
    record["version"] = version;
    record["kind"] = args[1];
    record["initiator"] = args[2];
    record["card"] = args[3];
    record["sourceOwner"] = v2 ? args[4].toString() : args[2].toString();
    record["sourceSkill"] = args[sourceSkillIndex];
    record["sourceID"] = args[sourceIdIndex];
    record["activationOwner"] = v2 ? args[7].toString() : args[2].toString();
    record["activationSkill"] = args[activationSkillIndex];
    record["activationID"] = args[activationIdIndex];
    m_cardProvenance << record;
    return true;
}
