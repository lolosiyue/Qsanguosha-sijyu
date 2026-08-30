#include "engine-bootstrap.h"
#include "engine.h"
#include "aux-skills.h"
#include "card.h"
#include "game-snapshot.h"
#include "json.h"
#include "player.h"
#include "player-ui-state-builder.h"
#include "protocol.h"
#include "record-buffer.h"
#include "room.h"
#include "server-info.h"
#include "serverplayer.h"
#include "skill.h"
#include "skill-instance-utils.h"
#include "skill-registry.h"

#include <QDebug>
#include <QFileInfo>
#include <QTemporaryDir>

class TestPlayer : public Player
{
public:
    TestPlayer()
        : Player(nullptr)
    {
    }

    int aliveCount(bool = false) const override { return 1; }
    QString getGameMode() const override { return QStringLiteral("test"); }
    Player *getNextAlive(int = 1) const override { return const_cast<TestPlayer *>(this); }
    Player *getLastAlive(int = 1) const override { return const_cast<TestPlayer *>(this); }
};

struct RoomTestAccess
{
    static ServerPlayer *addPlayer(Room &room, const QString &objectName)
    {
        ServerPlayer *player = new ServerPlayer(&room);
        player->setObjectName(objectName);
        room.addPlayerToRoster(player);
        return player;
    }

    static bool resolveCardSkillInstance(Room &room, CardUseStruct &use)
    {
        return room.resolveCardSkillInstance(use);
    }

    static void startVirtualGame(Room &room)
    {
        room._virtual = true;
        room.startGame();
    }
};

class TestPhysicalViewAsSkill : public OneCardViewAsSkill
{
public:
    TestPhysicalViewAsSkill()
        : OneCardViewAsSkill(QStringLiteral("test-physical-view-as"))
    {
    }

    bool viewFilter(const Card *) const override { return true; }
    const Card *viewAs(const Card *originalCard) const override { return originalCard; }
};

class TestPlayerUIStateFixedSkill : public MaxCardsSkill
{
public:
    TestPlayerUIStateFixedSkill()
        : MaxCardsSkill(QStringLiteral("test-player-ui-state-fixed"))
    {
    }

    int getFixed(const Player *) const override { return 9; }
};

class TestPlayerUIStateExtraSkill : public MaxCardsSkill
{
public:
    TestPlayerUIStateExtraSkill()
        : MaxCardsSkill(QStringLiteral("test-player-ui-state-extra"))
    {
    }

    int getExtra(const Player *) const override { return 2; }
};

class TestPlayerUIStateDistanceSkill : public DistanceSkill
{
public:
    TestPlayerUIStateDistanceSkill()
        : DistanceSkill(QStringLiteral("test-player-ui-state-distance"))
    {
    }

    int getCorrect(const Player *from, const Player *to) const override
    {
        if (from && from->objectName() == QStringLiteral("ui-state-owner"))
            return -1;
        if (to && to->objectName() == QStringLiteral("ui-state-owner"))
            return 2;
        return 0;
    }
};

class TestPlayerUIStateEquipSkill : public ViewAsEquipSkill
{
public:
    TestPlayerUIStateEquipSkill()
        : ViewAsEquipSkill(QStringLiteral("test-player-ui-state-equip"))
    {
    }

    QString viewAsEquip(const Player *) const override
    {
        return QStringLiteral("crossbow,silver_lion");
    }
};

class TestRegistryTriggerSkill : public TriggerSkill
{
public:
    TestRegistryTriggerSkill()
        : TriggerSkill(QStringLiteral("test-registry-trigger"))
    {
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return false; }
};

static bool skillRegistryPreservesLegacyBehavior()
{
    SkillRegistry registry;
    DistanceSkill distance(QStringLiteral("test-registry-shared"));
    MaxCardsSkill maxCards(QStringLiteral("test-registry-shared"));
    TestRegistryTriggerSkill trigger;

    if (registry.add(&distance)
        || registry.find(QStringLiteral("test-registry-shared#7")) != &distance
        || !registry.distanceSkills().contains(&distance))
        return false;

    if (registry.add(&trigger)
        || registry.triggerSkill(QStringLiteral("test-registry-trigger#3")) != &trigger)
        return false;

    if (!registry.add(&maxCards)
        || registry.find(QStringLiteral("test-registry-shared")) != &maxCards
        || registry.distanceSkills().contains(&distance)
        || !registry.maxCardsSkills().contains(&maxCards))
        return false;

    return registry.names().size() == 2 && registry.allSkills().size() == 2;
}

static bool engineLegacySkillApisDelegateToRegistry()
{
    DistanceSkill distance(QStringLiteral("test-engine-registry-distance"));
    MaxCardsSkill maxCards(QStringLiteral("test-engine-registry-max-cards"));
    TestRegistryTriggerSkill trigger;
    Sanguosha->addSkills(QList<const Skill *>() << &distance << &maxCards << &trigger);

    return Sanguosha->getSkill(QStringLiteral("test-engine-registry-distance#2")) == &distance
        && Sanguosha->getTriggerSkill(QStringLiteral("test-registry-trigger#3")) == &trigger
        && Sanguosha->getDistanceSkills().contains(&distance)
        && Sanguosha->getMaxCardsSkills().contains(&maxCards);
}

static bool discardSkillSelectsCardsForExplicitClientPlayerContext()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return false;
    }

    TestPlayer player;
    DummyCard card;
    DiscardSkill skill;
    skill.setNum(1);
    skill.setMinNum(1);
    skill.setIncludeEquip(false);
    skill.setIsDiscard(true);
    skill.setPattern(QStringLiteral("."));
    skill.setPlayer(&player);

    const bool selectable = skill.viewFilter(QList<const Card *>(), &card);
    if (!selectable)
        qCritical() << "DiscardSkill rejected a card for its explicit client player context";
    return selectable;
}

static int physicalResponseIgnoresStaleHelperActivation()
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *player = RoomTestAccess::addPlayer(room, QStringLiteral("human"));

    DummyCard physicalCard;
    physicalCard.setId(42);
    if (physicalCard.isVirtualCard()) {
        qCritical() << "physical response fixture is not a real engine card";
        return 1;
    }

    physicalCard.setActivationSkill(QStringLiteral("response-skill"), 7);
    CardUseStruct use(&physicalCard, player);
    use.hasSkillActivationRequest = true;

    const bool accepted = RoomTestAccess::resolveCardSkillInstance(room, use);
    physicalCard.setActivationSkill(QString(), 0);
    if (!accepted)
        qCritical() << "physical response was rejected because of stale helper activation metadata";
    if (!accepted)
        return 2;
    if (use.activationRef.isValid() || use.sourceRef.isValid())
        return 3;

    TestPhysicalViewAsSkill registeredSkill;
    Sanguosha->addSkills(QList<const Skill *>() << &registeredSkill);
    const int instanceId = player->createSkillInstance(registeredSkill.objectName(), SourceAcquired, true);
    physicalCard.setActivationSkill(registeredSkill.objectName(), instanceId);
    CardUseStruct registeredUse(&physicalCard, player);
    registeredUse.hasSkillActivationRequest = true;
    const bool registeredAccepted = RoomTestAccess::resolveCardSkillInstance(room, registeredUse);
    physicalCard.setSkillInstanceId(0);
    physicalCard.setSourceSkill(QString(), 0);
    physicalCard.setActivationSkill(QString(), 0);
    if (!registeredAccepted || !registeredUse.activationRef.isValid()
        || registeredUse.activationRef.key.skillName != registeredSkill.objectName()
        || registeredUse.activationRef.key.instanceID != instanceId)
        return 4;
    return 0;
}

static bool playerUIStateBuilderAggregatesPresentationState()
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *owner = RoomTestAccess::addPlayer(room, QStringLiteral("ui-state-owner"));
    ServerPlayer *sibling = RoomTestAccess::addPlayer(room, QStringLiteral("ui-state-sibling"));
    owner->setAlive(true);
    sibling->setAlive(true);

    TestPlayerUIStateFixedSkill fixedSkill;
    TestPlayerUIStateExtraSkill extraSkill;
    TestPlayerUIStateDistanceSkill distanceSkill;
    TestPlayerUIStateEquipSkill equipSkill;
    Sanguosha->addSkills(QList<const Skill *>()
                         << &fixedSkill << &extraSkill << &distanceSkill << &equipSkill);
    owner->Player::addSkill(fixedSkill.objectName());
    owner->Player::addSkill(extraSkill.objectName());
    owner->Player::addSkill(distanceSkill.objectName());
    owner->Player::addSkill(equipSkill.objectName());

    const PlayerUIState state = PlayerUIStateBuilder::build(*owner, room);
    const QString fixedEntry = QStringLiteral("test-player-ui-state-fixed^F9^ui-state-owner");
    const QString extraEntry = QStringLiteral("test-player-ui-state-extra^2^ui-state-owner");
    const QStringList equipEntries = QStringList()
        << QStringLiteral("crossbow^test-player-ui-state-equip")
        << QStringLiteral("silver_lion^test-player-ui-state-equip");

    const bool valid = state.handMax == owner->getMaxCards()
        && state.maxCardsSkills.contains(fixedEntry)
        && state.maxCardsSkills.contains(extraEntry)
        && state.offensiveDistance == -1
        && state.defensiveDistance == 2
        && state.offensiveSkills == QStringList(distanceSkill.objectName())
        && state.defensiveSkills == QStringList(distanceSkill.objectName())
        && state.viewAsEquipSkills == equipEntries;
    if (!valid)
        qCritical() << "PlayerUIStateBuilder returned unexpected presentation state"
                    << state.toVariant();
    return valid;
}

static bool gameStartupConstructsRoomThreadBeforePreparingViewAsEquipSkills()
{
    const General *wolong = Sanguosha->getGeneral(QStringLiteral("wolong"));
    const General *caocao = Sanguosha->getGeneral(QStringLiteral("caocao"));
    if (!wolong || !caocao) {
        qCritical() << "startup regression fixture generals are unavailable";
        return false;
    }

    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *first = RoomTestAccess::addPlayer(room, QStringLiteral("first"));
    ServerPlayer *second = RoomTestAccess::addPlayer(room, QStringLiteral("second"));
    first->setState(QStringLiteral("robot"));
    second->setState(QStringLiteral("robot"));
    first->setGeneral(wolong);
    second->setGeneral(caocao);

    RoomTestAccess::startVirtualGame(room);
    if (!room.getThread()) {
        qCritical() << "RoomThread was not constructed before preparing ViewAsEquipSkill mappings";
        return false;
    }
    return true;
}

static bool roomSnapshotFacadePersistsAndRetrievesSnapshot()
{
    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid()) {
        qCritical() << "Unable to create snapshot regression directory";
        return false;
    }

    Room room(nullptr, QStringLiteral("02_1v1"));
    const QString replayPath = temporaryDir.filePath(QStringLiteral("snapshot-facade.replay.txt"));
    room.setReplayPath(replayPath);
    room.setTag(QStringLiteral("TurnLengthCount"), 7);

    room.saveSnapshot(QStringLiteral("turn"));

    GameSnapshot *snapshot = room.getSnapshot(7);
    const QString snapshotDir = GameSnapshot::getSnapshotDir(replayPath);
    const QString snapshotPath = snapshotDir + QStringLiteral("/")
        + GameSnapshot::generateSnapshotFilename(7, QStringLiteral("turn"));
    const bool valid = room.getReplayPath() == replayPath
        && room.getSnapshotDir() == snapshotDir
        && QFileInfo::exists(snapshotPath)
        && snapshot
        && snapshot->getReplayPath() == replayPath
        && snapshot->getSnapshotType() == QStringLiteral("turn")
        && snapshot->getDescription() == QStringLiteral("Turn 7");
    if (!valid)
        qCritical() << "Room snapshot facade did not preserve persisted snapshot state";
    return valid;
}

int runEngineSmokeTests()
{
    ServerInfoStruct info;
    const QString setup = QString::fromLatin1("U2VydmVy:02_1v1_standard:15:3:standard:RC");
    if (!info.parse(setup) || info.GameMode != QStringLiteral("02_1v1")
        || info.GameRuleMode != QStringLiteral("_standard"))
        return 1;

    if (info.getCommandTimeout(QSanProtocol::S_COMMAND_CHOOSE_GENERAL,
                               QSanProtocol::S_CLIENT_INSTANCE) != 22500)
        return 2;

    SkillInstanceUtils::SkillActivationRequest request;
    if (!SkillInstanceUtils::decodeActivationRequest(
            JsonArray() << QStringLiteral("slash") << 1 << 3,
            QStringLiteral("slash"), request)
        || !request.supplied || request.instanceID != 3)
        return 3;

    QSanProtocol::Packet packet(QSanProtocol::S_DESC_UNKNOWN,
                                QSanProtocol::S_COMMAND_UNKNOWN);
    if (!packet.parse(QByteArrayLiteral("[1,2,1,1]")))
        return 4;

    RecordBuffer recordBuffer;
    QSanProtocol::ProtocolMessage replayMessage;
    replayMessage.type = QSanProtocol::ProtocolMessageType::Notification;
    replayMessage.source = QSanProtocol::ProtocolEndpoint::Room;
    replayMessage.destination = QSanProtocol::ProtocolEndpoint::Client;
    replayMessage.command = QSanProtocol::S_COMMAND_UNKNOWN;
    QString replayError;
    if (!recordBuffer.recordMessage(replayMessage, &replayError))
        return 5;
    const QList<QByteArray> records = recordBuffer.getRecords();
    if (records.size() != 1
        || !recordBuffer.rawReplayData().startsWith(
            "QSAN_REPLAY {\"format_version\":2,\"protocol_version\":2}\n"))
        return 5;

    if (!discardSkillSelectsCardsForExplicitClientPlayerContext())
        return 6;

    const int physicalResponseResult = physicalResponseIgnoresStaleHelperActivation();
    if (physicalResponseResult != 0)
        return 70 + physicalResponseResult;

    if (!playerUIStateBuilderAggregatesPresentationState())
        return 80;

    if (!skillRegistryPreservesLegacyBehavior())
        return 90;

    if (!engineLegacySkillApisDelegateToRegistry())
        return 100;

    if (!gameStartupConstructsRoomThreadBeforePreparingViewAsEquipSkills())
        return 110;

    if (!roomSnapshotFacadePersistsAndRetrievesSnapshot())
        return 120;

    qInfo() << "qsanguosha_engine smoke passed";
    return 0;
}
