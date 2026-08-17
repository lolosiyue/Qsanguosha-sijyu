#include "engine-bootstrap.h"
#include "ai.h"
#include "room-test-access.h"
#include "room.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "skill.h"

#include <QCoreApplication>
#include <QDebug>

namespace {

struct ExtraTurnObservation
{
    QString playerName;
    QString reason;
    SkillInstanceRef sourceRef;
    QVariantList phases;
    bool currentPlayerMatches;
    bool globalTagActive;
};

class ExtraTurnProbe : public TriggerSkill
{
public:
    ExtraTurnProbe()
        : TriggerSkill(QStringLiteral("extra-turn-probe")), schedulePlayer(nullptr),
          nestedStarted(false), outerRestoredAfterNested(false),
          turnBrokenThrown(false), stageChangeThrown(false), scheduledDuring(false)
    {
        events << TurnStart;
        global = true;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        ExtraTurnObservation observation;
        observation.playerName = player->objectName();
        observation.reason = room->getCurrentExtraTurnReason();
        observation.sourceRef = room->getCurrentExtraTurnSourceRef();
        observation.phases = player->getTag("extraTurnPhases").toList();
        observation.currentPlayerMatches = room->getCurrent() == player;
        observation.globalTagActive = room->getTag(
            "Global_ExtraTurn" + player->objectName()).toBool();
        observations << observation;

        if (!scheduleDuringReason.isEmpty() && observation.reason == scheduleDuringReason
            && !scheduledDuring) {
            scheduledDuring = true;
            room->scheduleExtraTurn(schedulePlayer, scheduledReason,
                                    QList<Player::Phase>() << Player::Finish);
        }

        if (!nestedReason.isEmpty() && observation.reason == nestedReason && !nestedStarted) {
            nestedStarted = true;
            player->gainAnExtraTurn(QList<Player::Phase>() << Player::Finish);
            outerRestoredAfterNested = room->isCurrentExtraTurn()
                && room->getCurrentExtraTurnReason() == nestedReason
                && room->getCurrentExtraTurnSourceRef() == observation.sourceRef
                && room->getCurrent() == player;
        }

        if (!turnBrokenReason.isEmpty() && observation.reason == turnBrokenReason
            && !turnBrokenThrown) {
            turnBrokenThrown = true;
            throw TurnBroken;
        }

        if (!stageChangeReason.isEmpty() && observation.reason == stageChangeReason
            && !stageChangeThrown) {
            stageChangeThrown = true;
            if (schedulePlayer)
                room->scheduleExtraTurn(schedulePlayer, scheduledReason,
                                        QList<Player::Phase>() << Player::Finish);
            throw StageChange;
        }

        return false;
    }

    mutable QList<ExtraTurnObservation> observations;
    ServerPlayer *schedulePlayer;
    QString scheduleDuringReason;
    QString scheduledReason;
    QString nestedReason;
    QString turnBrokenReason;
    QString stageChangeReason;
    mutable bool nestedStarted;
    mutable bool outerRestoredAfterNested;
    mutable bool turnBrokenThrown;
    mutable bool stageChangeThrown;
    mutable bool scheduledDuring;
};

static bool observationsMatch(const QList<ExtraTurnObservation> &observations,
                              const QStringList &players, const QStringList &reasons)
{
    if (observations.length() != players.length() || players.length() != reasons.length())
        return false;

    const QVariantList expectedPhases = QVariantList() << static_cast<int>(Player::Finish);
    for (int i = 0; i < observations.length(); ++i) {
        const ExtraTurnObservation &observation = observations.at(i);
        if (observation.playerName != players.at(i) || observation.reason != reasons.at(i)
            || observation.phases != expectedPhases || !observation.currentPlayerMatches
            || !observation.globalTagActive)
            return false;
    }
    return true;
}

static bool scheduledTurnsPreserveBatchOrderingAndFacade()
{
    ExtraTurnProbe probe;
    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *first = RoomTestAccess::addOrdinaryPlayer(room, QStringLiteral("first"), true);
    ServerPlayer *second = RoomTestAccess::addOrdinaryPlayer(room, QStringLiteral("second"), true);
    ServerPlayer *third = RoomTestAccess::addOrdinaryPlayer(room, QStringLiteral("third"), true);
    room.setCurrent(second);
    RoomTestAccess::attachThread(room, &probe);

    ServerPlayer *invalid = new ServerPlayer(&room);
    invalid->setObjectName(QStringLiteral("invalid"));
    invalid->setAlive(false);
    if (room.scheduleExtraTurn(nullptr, QStringLiteral("null")) != 0
        || room.scheduleExtraTurn(invalid, QStringLiteral("dead")) != 0
        || room.scheduleExtraTurn(first, QStringLiteral("zero"), {}, 0) != 0)
        return false;

    room.setTag(QStringLiteral("Global_ExtraTurnfirst"), QStringLiteral("original-room-tag"));
    first->setTag(QStringLiteral("extraTurnPhases"), QStringLiteral("original-player-tag"));
    room.setPlayerMark(first, QStringLiteral("@extra_turn"), 7);

    const QList<Player::Phase> phases = QList<Player::Phase>() << Player::Finish;
    const SkillInstanceRef sourceRef(second->objectName(),
                                     SkillInstanceKey(QStringLiteral("source-skill"), 3));
    if (room.scheduleExtraTurn(first, QStringLiteral("first-a"), phases, 1) != 1
        || room.scheduleExtraTurn(third, QStringLiteral("third"), phases, 1) != 1
        || room.scheduleExtraTurn(second, sourceRef, phases, 1) != 1
        || room.scheduleExtraTurn(first, QStringLiteral("first-b"), phases, 1) != 1)
        return false;

    probe.schedulePlayer = third;
    probe.scheduleDuringReason = QStringLiteral("source-skill");
    probe.scheduledReason = QStringLiteral("third-late");
    RoomTestAccess::process(room);

    const QStringList expectedPlayers = {
        QStringLiteral("second"), QStringLiteral("third"), QStringLiteral("first"),
        QStringLiteral("first"), QStringLiteral("third")
    };
    const QStringList expectedReasons = {
        QStringLiteral("source-skill"), QStringLiteral("third"), QStringLiteral("first-a"),
        QStringLiteral("first-b"), QStringLiteral("third-late")
    };
    if (!observationsMatch(probe.observations, expectedPlayers, expectedReasons)
        || probe.observations.first().sourceRef != sourceRef)
        return false;

    return room.getCurrent() == second && !room.isCurrentExtraTurn()
        && room.getCurrentExtraTurnReason().isEmpty()
        && !room.getCurrentExtraTurnSourceRef().isValid()
        && room.getTag(QStringLiteral("Global_ExtraTurnfirst")).toString()
            == QStringLiteral("original-room-tag")
        && first->getTag(QStringLiteral("extraTurnPhases")).toString()
            == QStringLiteral("original-player-tag")
        && first->getMark(QStringLiteral("@extra_turn")) == 7;
}

static bool nestedExecutionRestoresOuterContext()
{
    ExtraTurnProbe probe;
    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *outer = RoomTestAccess::addOrdinaryPlayer(room, QStringLiteral("outer"), true);
    ServerPlayer *previous = RoomTestAccess::addOrdinaryPlayer(room, QStringLiteral("previous"), true);
    room.setCurrent(previous);
    RoomTestAccess::attachThread(room, &probe);

    probe.nestedReason = QStringLiteral("outer-reason");
    const SkillInstanceRef sourceRef(outer->objectName(),
                                     SkillInstanceKey(QStringLiteral("outer-skill"), 9));
    RoomTestAccess::execute(room, outer, QList<Player::Phase>() << Player::Finish,
                            probe.nestedReason, sourceRef);

    const QStringList expectedPlayers = { QStringLiteral("outer"), QStringLiteral("outer") };
    const QStringList expectedReasons = { QStringLiteral("outer-reason"), QString() };
    return observationsMatch(probe.observations, expectedPlayers, expectedReasons)
        && probe.observations.first().sourceRef == sourceRef
        && !probe.observations.last().sourceRef.isValid()
        && probe.outerRestoredAfterNested && room.getCurrent() == previous
        && !room.isCurrentExtraTurn();
}

static bool controlEventsRestoreProcessingAndPendingRequests()
{
    ExtraTurnProbe probe;
    Room room(nullptr, QStringLiteral("02_1v1"));
    ServerPlayer *first = RoomTestAccess::addOrdinaryPlayer(room, QStringLiteral("first"), true);
    ServerPlayer *second = RoomTestAccess::addOrdinaryPlayer(room, QStringLiteral("second"), true);
    ServerPlayer *third = RoomTestAccess::addOrdinaryPlayer(room, QStringLiteral("third"), true);
    room.setCurrent(first);
    RoomTestAccess::attachThread(room, &probe);
    const QList<Player::Phase> phases = QList<Player::Phase>() << Player::Finish;

    probe.turnBrokenReason = QStringLiteral("break");
    room.scheduleExtraTurn(first, probe.turnBrokenReason, phases);
    room.scheduleExtraTurn(second, QStringLiteral("after-break"), phases);
    RoomTestAccess::process(room);
    if (!observationsMatch(probe.observations,
            QStringList() << QStringLiteral("first") << QStringLiteral("second"),
            QStringList() << QStringLiteral("break") << QStringLiteral("after-break")))
        return false;

    probe.observations.clear();
    probe.schedulePlayer = third;
    probe.scheduledReason = QStringLiteral("scheduled-during-stage");
    probe.stageChangeReason = QStringLiteral("stage");
    room.scheduleExtraTurn(first, probe.stageChangeReason, phases);
    room.scheduleExtraTurn(second, QStringLiteral("pending-tail"), phases);

    bool propagated = false;
    try {
        RoomTestAccess::process(room);
    } catch (TriggerEvent controlEvent) {
        propagated = controlEvent == StageChange;
    }
    if (!propagated || room.isCurrentExtraTurn() || room.getCurrent() != first)
        return false;

    RoomTestAccess::process(room);
    return observationsMatch(probe.observations,
        QStringList() << QStringLiteral("first") << QStringLiteral("second")
                      << QStringLiteral("third"),
        QStringList() << QStringLiteral("stage") << QStringLiteral("pending-tail")
                      << QStringLiteral("scheduled-during-stage"));
}

}

int runExtraTurnSchedulerTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    if (!scheduledTurnsPreserveBatchOrderingAndFacade())
        return 2;
    if (!nestedExecutionRestoresOuterContext())
        return 3;
    if (!controlEventsRestoreProcessingAndPendingRequests())
        return 4;

    qInfo() << "extra turn scheduler behavior passed";
    return 0;
}
