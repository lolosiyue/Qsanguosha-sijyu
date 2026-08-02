#include "engine-bootstrap.h"
#include "engine.h"
#include "aux-skills.h"
#include "card.h"
#include "json.h"
#include "player.h"
#include "protocol.h"
#include "record-buffer.h"
#include "room.h"
#include "server-info.h"
#include "serverplayer.h"
#include "skill-instance-utils.h"

#include <QCoreApplication>
#include <QDebug>

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
        room.m_players << player;
        return player;
    }

    static bool resolveCardSkillInstance(Room &room, CardUseStruct &use)
    {
        return room.resolveCardSkillInstance(use);
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
    physicalCard.setSkillInstanceID(0);
    physicalCard.setSourceSkill(QString(), 0);
    physicalCard.setActivationSkill(QString(), 0);
    if (!registeredAccepted || !registeredUse.activationRef.isValid()
        || registeredUse.activationRef.key.skillName != registeredSkill.objectName()
        || registeredUse.activationRef.key.instanceID != instanceId)
        return 4;
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

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
    recordBuffer.recordLine(packet.toJson());
    const QList<QByteArray> records = recordBuffer.getRecords();
    if (records.size() < 2 || !records.first().contains("[1,2,1,1]"))
        return 5;

    if (!discardSkillSelectsCardsForExplicitClientPlayerContext())
        return 6;

    const int physicalResponseResult = physicalResponseIgnoresStaleHelperActivation();
    if (physicalResponseResult != 0)
        return 70 + physicalResponseResult;

    qInfo() << "qsanguosha_engine smoke passed";
    return 0;
}
