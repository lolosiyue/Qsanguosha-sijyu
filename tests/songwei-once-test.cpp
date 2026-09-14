#include "engine-bootstrap.h"
#include "engine.h"
#include "room-test-access.h"
#include "room.h"
#include "server-info.h"
#include "serverplayer.h"
#include "skill.h"

#include <QCoreApplication>
#include <QDebug>

// Regression: mobilemou_caopi's lord skill 颂威 (mobilemousongwei) can make one
// other wei player lose all skills once per game. The trigger skill never
// registered its MobileMouSongweivs view-as skill, so the engine had no view-as
// skill named "mobilemousongwei". SmartAI::fillSkillCards only consults
// isEnabledAtPlay when that view-as skill exists, and the server does not check
// skill-card ownership in PlayerDecisionService::activate, so the AI lord played
// @MobileMouSongweiCard on the same target every play-phase loop for ever
// (20p GUI soak livelock, 2026-09-14).
namespace {

bool expect(bool condition, const char *context)
{
    if (!condition)
        qCritical() << "songwei-once:" << context;
    return condition;
}

ServerPlayer *addSeat(Room &room, const QString &objectName, const QString &general,
                      const QString &role, int seat)
{
    ServerPlayer *player = RoomTestAccess::addOrdinaryPlayer(room, objectName, true);
    player->setState(QStringLiteral("robot"));
    player->drainAllLocks();
    player->releaseLock(ServerPlayer::SEMA_MUTEX);
    player->setGeneral(Sanguosha->getGeneral(general));
    player->setRole(role);
    player->setSeat(seat);
    player->setPlayerSeat(seat);
    return player;
}

bool songweiIsLimitedToOncePerGame()
{
    const ViewAsSkill *songwei = Sanguosha->getViewAsSkill(QStringLiteral("mobilemousongwei"));
    if (!expect(songwei != nullptr, "mobilemousongwei registers its view-as skill"))
        return false;

    Room room(nullptr, QStringLiteral("05p"));
    EngineRuntimeContextScope scope(*Sanguosha, &room);
    room.roomRuntime()->state().reset();

    ServerPlayer *lord = addSeat(room, QStringLiteral("sgs1"), QStringLiteral("mobilemou_caopi"),
                                 QStringLiteral("lord"), 1);
    ServerPlayer *liege = addSeat(room, QStringLiteral("sgs2"), QStringLiteral("caocao"),
                                  QStringLiteral("rebel"), 2);
    RoomTestAccess::resetAlive(room);
    RoomTestAccess::attachThread(room);
    room.acquireSkill(lord, QStringLiteral("mobilemousongwei"), false, false, false);
    room.setCurrent(lord);
    lord->setPhase(Player::Play);

    bool ok = expect(liege->getKingdom() == QStringLiteral("wei"), "fixture: the target is wei");
    ok &= expect(songwei->isEnabledAtPlay(lord), "the lord can use 颂威 before it is spent");

    SkillCard *card = Sanguosha->cloneSkillCard(QStringLiteral("MobileMouSongweiCard"));
    if (!expect(card != nullptr, "MobileMouSongweiCard is registered"))
        return false;
    CardEffectStruct effect;
    effect.card = card;
    effect.from = lord;
    effect.to = liege;
    card->onEffect(effect);
    delete card;

    ok &= expect(!songwei->isEnabledAtPlay(lord), "颂威 is disabled once it has been used");
    return ok;
}

} // namespace

int runSongweiOnceTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }
    // Player::hasLordSkill refuses lord skills in 1v1/3v3 modes, and an empty
    // mode string matches that ban list.
    const QString oldMode = ServerInfo.GameMode;
    ServerInfo.GameMode = QStringLiteral("05p");
    const bool ok = songweiIsLimitedToOncePerGame();
    ServerInfo.GameMode = oldMode;
    if (!ok)
        return 2;
    qInfo() << "songwei once-per-game passed";
    return 0;
}
