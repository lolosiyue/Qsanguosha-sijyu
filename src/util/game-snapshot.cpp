#include "game-snapshot.h"
#include "serverplayer.h"
#include "room.h"
#include "engine.h"
#include "wrapped-card.h"
#include "json.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>

using namespace QSanProtocol;

QVariantMap PlayerSnapshot::serialize() const
{
    QVariantMap map;
    map["objectName"] = objectName;
    map["screenName"] = screenName;
    map["general"] = general;
    map["general2"] = general2;
    map["kingdom"] = kingdom;
    map["role"] = role;
    map["hp"] = hp;
    map["maxhp"] = maxhp;
    map["seat"] = seat;
    map["playerSeat"] = playerSeat;
    map["alive"] = alive;
    map["faceup"] = faceup;
    map["chained"] = chained;
    map["owner"] = owner;
    map["roleShown"] = roleShown;
    map["generalShowed"] = generalShowed;
    map["general2Showed"] = general2Showed;
    map["gender"] = gender;
    map["state"] = state;

    QVariantList handcardsList;
    foreach (int id, handcards)
        handcardsList << id;
    map["handcards"] = handcardsList;

    QVariantList equipsList;
    foreach (int id, equips)
        equipsList << id;
    map["equips"] = equipsList;

    QVariantList judgingList;
    foreach (int id, judgingArea)
        judgingList << id;
    map["judgingArea"] = judgingList;

    QVariantMap pilesMap;
    foreach (const QString &key, piles.keys()) {
        QVariantList pileList;
        foreach (int id, piles[key])
            pileList << id;
        pilesMap[key] = pileList;
    }
    map["piles"] = pilesMap;

    QVariantMap marksMap;
    foreach (const QString &key, marks.keys())
        marksMap[key] = marks[key];
    map["marks"] = marksMap;

    map["flags"] = flags;
    map["skills"] = skills;

    QVariantMap historyMap;
    foreach (const QString &key, history.keys())
        historyMap[key] = history[key];
    map["history"] = historyMap;

    QVariantMap equipAreasMap;
    foreach (int key, equipAreas.keys())
        equipAreasMap[QString::number(key)] = equipAreas[key];
    map["equipAreas"] = equipAreasMap;

    return map;
}

PlayerSnapshot PlayerSnapshot::deserialize(const QVariantMap &map)
{
    PlayerSnapshot snapshot;
    snapshot.objectName = map["objectName"].toString();
    snapshot.screenName = map["screenName"].toString();
    snapshot.general = map["general"].toString();
    snapshot.general2 = map["general2"].toString();
    snapshot.kingdom = map["kingdom"].toString();
    snapshot.role = map["role"].toString();
    snapshot.hp = map["hp"].toInt();
    snapshot.maxhp = map["maxhp"].toInt();
    snapshot.seat = map["seat"].toInt();
    snapshot.playerSeat = map["playerSeat"].toInt();
    snapshot.alive = map["alive"].toBool();
    snapshot.faceup = map["faceup"].toBool();
    snapshot.chained = map["chained"].toBool();
    snapshot.owner = map["owner"].toBool();
    snapshot.roleShown = map["roleShown"].toBool();
    snapshot.generalShowed = map["generalShowed"].toBool();
    snapshot.general2Showed = map["general2Showed"].toBool();
    snapshot.gender = map["gender"].toString();
    snapshot.state = map["state"].toString();

    foreach (const QVariant &v, map["handcards"].toList())
        snapshot.handcards << v.toInt();

    foreach (const QVariant &v, map["equips"].toList())
        snapshot.equips << v.toInt();

    foreach (const QVariant &v, map["judgingArea"].toList())
        snapshot.judgingArea << v.toInt();

    QVariantMap pilesMap = map["piles"].toMap();
    foreach (const QString &key, pilesMap.keys()) {
        QList<int> pile;
        foreach (const QVariant &v, pilesMap[key].toList())
            pile << v.toInt();
        snapshot.piles[key] = pile;
    }

    QVariantMap marksMap = map["marks"].toMap();
    foreach (const QString &key, marksMap.keys())
        snapshot.marks[key] = marksMap[key].toInt();

    foreach (const QVariant &v, map["flags"].toList())
        snapshot.flags << v.toString();

    foreach (const QVariant &v, map["skills"].toList())
        snapshot.skills << v.toString();

    QVariantMap historyMap = map["history"].toMap();
    foreach (const QString &key, historyMap.keys())
        snapshot.history[key] = historyMap[key].toInt();

    QVariantMap equipAreasMap = map["equipAreas"].toMap();
    foreach (const QString &key, equipAreasMap.keys())
        snapshot.equipAreas[key.toInt()] = equipAreasMap[key].toInt();

    return snapshot;
}

PlayerSnapshot PlayerSnapshot::fromPlayer(ServerPlayer *player)
{
    PlayerSnapshot snapshot;
    if (!player)
        return snapshot;

    snapshot.objectName = player->objectName();
    snapshot.screenName = player->screenName();
    snapshot.general = player->getGeneralName();
    snapshot.general2 = player->getGeneral2Name();
    snapshot.kingdom = player->getKingdom();
    snapshot.role = player->getRole();
    snapshot.hp = player->getHp();
    snapshot.maxhp = player->getMaxHp();
    snapshot.seat = player->getSeat();
    snapshot.playerSeat = player->getPlayerSeat();
    snapshot.alive = player->isAlive();
    snapshot.faceup = player->faceUp();
    snapshot.chained = player->isChained();
    snapshot.owner = player->isOwner();
    snapshot.roleShown = player->hasShownRole();
    snapshot.generalShowed = player->hasShownGeneral();
    snapshot.general2Showed = player->hasShownGeneral2();
    snapshot.gender = QString::number(static_cast<int>(player->getGender()));
    snapshot.state = player->getState();

    snapshot.handcards = player->handCards();
    snapshot.equips = player->getEquipsId();
    snapshot.judgingArea = player->getJudgingAreaID();

    foreach (const QString &pileName, player->getPileNames()) {
        snapshot.piles[pileName] = player->getPile(pileName);
    }

    foreach (const QString &markName, player->getMarkNames()) {
        int value = player->getMark(markName);
        if (value != 0)
            snapshot.marks[markName] = value;
    }

    snapshot.flags = player->getFlags().split("|");
    snapshot.flags.removeAll("");

    foreach (const Skill *skill, player->getVisibleSkillList(true)) {
        snapshot.skills << skill->objectName();
    }

    foreach (const QString &key, player->getHistory().keys()) {
        snapshot.history[key] = player->getHistory().value(key);
    }

    for (int i = 0; i < 5; i++) {
        snapshot.equipAreas[i] = player->getEquipArea(i);
    }

    return snapshot;
}

QVariantMap GlobalSnapshot::serialize() const
{
    QVariantMap map;
    map["turnCount"] = turnCount;
    map["roundCount"] = roundCount;
    map["currentPlayer"] = currentPlayer;
    map["currentPhase"] = currentPhase;
    map["gameMode"] = gameMode;
    map["packages"] = packages;

    QVariantList drawPileList;
    foreach (int id, drawPile)
        drawPileList << id;
    map["drawPile"] = drawPileList;

    QVariantList discardPileList;
    foreach (int id, discardPile)
        discardPileList << id;
    map["discardPile"] = discardPileList;

    QVariantList playersList;
    foreach (const PlayerSnapshot &p, players)
        playersList << p.serialize();
    map["players"] = playersList;

    map["seatOrder"] = seatOrder;
    map["roomTags"] = roomTags;
    map["chatHistory"] = chatHistory;

    return map;
}

GlobalSnapshot GlobalSnapshot::deserialize(const QVariantMap &map)
{
    GlobalSnapshot snapshot;
    snapshot.turnCount = map["turnCount"].toInt();
    snapshot.roundCount = map["roundCount"].toInt();
    snapshot.currentPlayer = map["currentPlayer"].toString();
    snapshot.currentPhase = map["currentPhase"].toString();
    snapshot.gameMode = map["gameMode"].toString();
    snapshot.packages = map["packages"].toStringList();

    foreach (const QVariant &v, map["drawPile"].toList())
        snapshot.drawPile << v.toInt();

    foreach (const QVariant &v, map["discardPile"].toList())
        snapshot.discardPile << v.toInt();

    foreach (const QVariant &v, map["players"].toList())
        snapshot.players << PlayerSnapshot::deserialize(v.toMap());

    snapshot.seatOrder = map["seatOrder"].toStringList();
    snapshot.roomTags = map["roomTags"].toMap();
    snapshot.chatHistory = map["chatHistory"].toStringList();

    return snapshot;
}

GameSnapshot::GameSnapshot(QObject *parent)
    : QObject(parent)
{
    m_timestamp = QDateTime::currentDateTime();
}

GameSnapshot::GameSnapshot(Room *room, QObject *parent)
    : QObject(parent)
{
    m_timestamp = QDateTime::currentDateTime();

    if (!room)
        return;

    m_state.turnCount = room->getTag("TurnLengthCount").toInt();

    ServerPlayer *current = room->getCurrent();
    if (current) {
        m_state.currentPlayer = current->objectName();
        m_state.currentPhase = QString::number(static_cast<int>(current->getPhase()));
    }

    m_state.gameMode = room->getMode();

    foreach (ServerPlayer *p, room->getAllPlayers(true)) {
        m_state.players << PlayerSnapshot::fromPlayer(p);
        m_state.seatOrder << p->objectName();
    }

    m_state.drawPile = room->getDrawPile();
    m_state.discardPile = room->getDiscardPile();

    m_state.roomTags = room->getAllTags();

    m_turnCount = m_state.turnCount;
}

GameSnapshot::GameSnapshot(const QString &filepath, QObject *parent)
    : QObject(parent)
{
    load(filepath);
}

bool GameSnapshot::save(const QString &filepath)
{
    QVariantMap root;
    root["version"] = 1;
    root["timestamp"] = m_timestamp.toString(Qt::ISODate);
    root["replayPath"] = m_replayPath;
    root["snapshotType"] = m_snapshotType;
    root["description"] = m_description;
    root["state"] = m_state.serialize();

    QJsonDocument doc = QJsonDocument::fromVariant(root);
    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool GameSnapshot::load(const QString &filepath)
{
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    QVariantMap root = doc.toVariant().toMap();

    m_timestamp = QDateTime::fromString(root["timestamp"].toString(), Qt::ISODate);
    m_replayPath = root["replayPath"].toString();
    m_snapshotType = root["snapshotType"].toString();
    m_description = root["description"].toString();
    m_state = GlobalSnapshot::deserialize(root["state"].toMap());
    m_turnCount = m_state.turnCount;

    return true;
}

GlobalSnapshot GameSnapshot::getState() const
{
    return m_state;
}

void GameSnapshot::setState(const GlobalSnapshot &state)
{
    m_state = state;
}

int GameSnapshot::getTurnCount() const
{
    return m_turnCount;
}

void GameSnapshot::setTurnCount(int turn)
{
    m_turnCount = turn;
}

QDateTime GameSnapshot::getTimestamp() const
{
    return m_timestamp;
}

QString GameSnapshot::getReplayPath() const
{
    return m_replayPath;
}

void GameSnapshot::setReplayPath(const QString &path)
{
    m_replayPath = path;
}

QString GameSnapshot::getSnapshotType() const
{
    return m_snapshotType;
}

void GameSnapshot::setSnapshotType(const QString &type)
{
    m_snapshotType = type;
}

QString GameSnapshot::getDescription() const
{
    return m_description;
}

void GameSnapshot::setDescription(const QString &desc)
{
    m_description = desc;
}

QString GameSnapshot::getSnapshotDir(const QString &replayPath)
{
    QFileInfo info(replayPath);
    QString baseName = info.completeBaseName();
    QString dirPath = info.absolutePath() + "/" + baseName + ".snapshots";
    return dirPath;
}

QString GameSnapshot::generateSnapshotFilename(int turnCount, const QString &type, const QString &playerName)
{
    QString filename = QString("turn_%1").arg(turnCount, 3, 10, QChar('0'));
    if (!type.isEmpty()) {
        filename += "_" + type;
        if (!playerName.isEmpty())
            filename += "_" + playerName;
    }
    filename += ".json";
    return filename;
}