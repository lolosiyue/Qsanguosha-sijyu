#include "player-lifecycle-service.h"

#include "card-movement-service.h"
#include "card-lifetime-manager.h"
#include "event-dispatcher.h"
#include "gamerule.h"
#include "room.h"
#include "room-notifier.h"
#include "room-roster.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "skill-runtime-coordinator.h"

#include "ai.h"
#include "banpair.h"
#include "engine.h"
#include "protocol/arrange-seats-message.h"
#include "protocol/switch-context-message.h"
#include "protocol/session/session-payloads.h"
#include "settings.h"
#include "standard.h"

#include <QSet>

#include <limits>

using namespace QSanProtocol;

PlayerLifecycleService::PlayerLifecycleService(Room &room, RoomRoster &roster,
                                               SkillRuntimeCoordinator &skillRuntime,
                                               CardMovementService &cardMovement,
                                               RoomNotifier &notifier,
                                               EventDispatcher &eventDispatcher)
    : m_room(room), m_roster(roster), m_skillRuntime(skillRuntime),
      m_cardMovement(cardMovement), m_notifier(notifier),
      m_eventDispatcher(eventDispatcher)
{
}

ServerPlayer *PlayerLifecycleService::addSocket(ClientSocket *socket)
{
    ServerPlayer *player = new ServerPlayer(&m_room);
    player->setObjectName(Room::generatePlayerName());
    player->setSocket(socket);
    m_roster.add(player);

    QObject::connect(player, SIGNAL(disconnected()), &m_room, SLOT(reportDisconnection()));
    QObject::connect(player, &ServerPlayer::request_got,
                     &m_room, &Room::processClientPacket);
    return player;
}

ServerPlayer *PlayerLifecycleService::addAIPlayer()
{
    ServerPlayer *player = new ServerPlayer(&m_room);
    player->setState("robot");
    m_roster.add(player);

    QObject::connect(player, SIGNAL(disconnected()), &m_room, SLOT(reportDisconnection()));
    QObject::connect(player, &ServerPlayer::request_got,
                     &m_room, &Room::processClientPacket);
    return player;
}

void PlayerLifecycleService::signup(ServerPlayer *player, const QString &screenName,
                                    const QString &avatar, bool isRobot)
{
    if (player->objectName().isEmpty())
        player->setObjectName(Room::generatePlayerName());
    m_room.safeSetPlayerProperty(player, "avatar", avatar);
    player->setScreenName(screenName);

    if (!isRobot) {
        m_room.notifyProperty(player, player, "objectName");
        // Everyone else's connection state arrives through a broadcast, but
        // nothing broadcasts a player's own state until it changes, so tell
        // the joining client about itself here.
        m_room.notifyProperty(player, player, "state");
        if (!m_room.getOwner()) {
            player->setOwner(true);
            m_room.notifyProperty(player, player, "owner");
        }
    }

    player->introduceTo(nullptr);

    if (isRobot) {
        ReadyPayload readyPayload;
        m_room.setReadyCommand(player, readyPayload.toVariant());
    } else {
        const QString greetingStr = "<font color=#EEB422>已加入游戏</font>";
        ChatPayload greeting;
        greeting.text = greetingStr;
        m_room.speakCommand(player, greeting.toVariant());
        player->startNetworkDelayTest();
        foreach (ServerPlayer *existing, m_roster.players()) {
            if (existing != player)
                existing->introduceTo(player);
        }
    }
}

void PlayerLifecycleService::restPlayer(ServerPlayer *player, const QString &reason,
                                        bool discardCards)
{
    if (!player)
        return;

    m_room.setPlayerProperty(player, "RestPlayer", true);
    if (!reason.isEmpty())
        player->setTag("RestReason", reason);

    QStringList skillNames;
    foreach (const Skill *skill, player->getVisibleSkillList())
        skillNames << skill->objectName();
    player->setTag("RestPlayerSkills", skillNames);

    if (discardCards && !player->isKongcheng())
        player->throwAllCards(reason);

    m_room.broadcastProperty(player, "alive");
    m_notifier.doBroadcastNotify(S_COMMAND_KILL_PLAYER, QVariant(player->objectName()));
    player->detachAllSkills();

    foreach (ServerPlayer *existing, m_roster.orderedFrom(m_room.current, true)) {
        if (existing->getAI())
            m_room.resetAI(existing);
    }
}

void PlayerLifecycleService::directRestPlayer(ServerPlayer *player, const QString &reason,
                                              bool discardCards)
{
    if (!player)
        return;

    player->setAlive(false);
    const QList<ServerPlayer *> affected = m_roster.alivePlayersAfter(player);
    m_roster.removeAlive(player);
    foreach (ServerPlayer *affectedPlayer, affected)
        m_room.broadcastProperty(affectedPlayer, "seat");

    m_room.setPlayerProperty(player, "RestPlayer", true);
    if (!reason.isEmpty())
        player->setTag("RestReason", reason);

    QStringList skillNames;
    foreach (const Skill *skill, player->getVisibleSkillList())
        skillNames << skill->objectName();
    player->setTag("RestPlayerSkills", skillNames);

    if (discardCards && !player->isKongcheng())
        player->throwAllCards(reason);

    m_room.broadcastProperty(player, "alive");
    m_notifier.doBroadcastNotify(S_COMMAND_KILL_PLAYER, QVariant(player->objectName()));
    player->detachAllSkills();
    m_room.updateStateItem();

    foreach (ServerPlayer *existing, m_roster.orderedFrom(m_room.current, true)) {
        if (existing->getAI())
            m_room.resetAI(existing);
    }
}

void PlayerLifecycleService::unrestPlayer(ServerPlayer *player, bool restoreFullHp,
                                          bool restoreOriginalSkills)
{
    if (!player)
        return;

    m_room.setPlayerProperty(player, "RestPlayer", false);
    player->removeTag("RestReason");
    revivePlayer(player, true, true, false);

    if (restoreOriginalSkills) {
        const QStringList skillNames = player->getTag("RestPlayerSkills").toStringList();
        foreach (const QString &skillName, skillNames)
            m_skillRuntime.acquireSkill(player, skillName, true, true, true);
    }
    player->removeTag("RestPlayerSkills");

    if (restoreFullHp)
        m_room.setPlayerProperty(player, "hp", player->getMaxHp());

    foreach (ServerPlayer *existing, m_roster.orderedFrom(m_room.current, true)) {
        if (existing->getAI())
            m_room.resetAI(existing);
    }
}

bool PlayerLifecycleService::isRest(ServerPlayer *player) const
{
    return player && player->property("RestPlayer").toBool();
}

QList<ServerPlayer *> PlayerLifecycleService::getRestPlayers() const
{
    QList<ServerPlayer *> restPlayers;
    foreach (ServerPlayer *player, m_roster.orderedFrom(m_room.current, true)) {
        if (isRest(player))
            restPlayers << player;
    }
    return restPlayers;
}

void PlayerLifecycleService::revivePlayer(ServerPlayer *player, bool sendLog,
                                          bool throwMark, bool visibleOnly)
{
    if (player->isAlive())
        return;

    QVariant revivedTimes = player->property("Revived_Times").toInt();
    if (m_eventDispatcher.dispatch(Revive, player, revivedTimes))
        return;

    m_room.setEmotion(player, "revive");
    int turn = player->getMark("Global_TurnCount");
    const int turn2 = player->getMark("Global_TurnCount2");
    if (throwMark) {
        player->throwAllMarks(visibleOnly);
        if (!visibleOnly) {
            m_room.setPlayerMark(player, "Global_TurnCount", turn);
            m_room.setPlayerMark(player, "Global_TurnCount2", turn2);
        }
    }

    player->setAlive(true);
    m_room.broadcastProperty(player, "alive");
    if (m_room.current == player)
        m_room.setPlayerFlag(player, "CurrentPlayer");

    m_roster.rebuildAlive();
    m_roster.reseatAlive();
    foreach (ServerPlayer *alivePlayer, m_roster.alivePlayers())
        m_room.broadcastProperty(alivePlayer, "seat");

    m_notifier.doBroadcastNotify(S_COMMAND_REVIVE_PLAYER, player->objectName());
    m_room.updateStateItem();
    if (sendLog) {
        LogMessage log;
        log.type = "#Revive";
        log.from = player;
        m_notifier.sendLog(log, QList<ServerPlayer *>());
    }

    foreach (const Skill *skill, player->getSkillList()) {
        if (skill->getFrequency() == Skill::Club && !skill->getClubName().isEmpty())
            player->addClub(skill->getClubName());
    }

    turn = revivedTimes.toInt();
    revivedTimes = turn + 1;
    m_eventDispatcher.dispatch(Revived, player, revivedTimes);
    m_room.safeSetPlayerProperty(player, "Revived_Times", revivedTimes);
}

struct PlayerLifecycleService::DeathCursor {
    enum Stage { Start, BeforeOver, Announce, GameOver, AfterGameOver, Death, Clubs, Bury, Detach, Reason, Spectate, Finished } stage = Start;
    QPointer<ServerPlayer> victim, killer;
    DamageStruct damage;
    HpLostStruct hpLost;
    bool hasDamage = false, hasHpLost = false;
    CardLifetimeLease causeLease;
    QVariant data;
    QVariantMap fact;
    QList<QPointer<ServerPlayer>> observers;
    int observerIndex = 0;
    bool observersReady = false;
    quint64 owner = 0, completedOwner = 0;
    bool retained = false;
};

void PlayerLifecycleService::killPlayer(ServerPlayer *victim, DamageStruct *reason, HpLostStruct *hpLost)
{
    auto cursor = std::make_shared<DeathCursor>();
    cursor->victim = victim;
    cursor->killer = reason ? reason->from : nullptr;
    cursor->owner = m_room.getThread()->cascadeId();
    if (reason) {
        cursor->hasDamage = true;
        cursor->damage = *reason;
        cursor->causeLease = CardLifetimeLease(globalCardLifetimeManager(),
                                               globalCardLifetimeManager().liveToken(reason->card));
    }
    if (hpLost) { cursor->hasHpLost = true; cursor->hpLost = *hpLost; }
    ServerPlayer *killer = cursor->killer;
    if (m_room.historyRecordingEnabled()) {
        cursor->fact = {{"victim", victim->objectName()}, {"to", victim->objectName()},
            {"from", killer ? killer->objectName() : QString()},
            {"killer", killer ? killer->objectName() : QString()},
            {"cause_kind", hpLost ? QStringLiteral("hp_lost") : reason ? QStringLiteral("damage") : QStringLiteral("other")},
            {"victim_role", victim->getRole()}, {"victim_kingdom", victim->getKingdom()},
            {"killer_role", killer ? killer->getRole() : QString()},
            {"killer_kingdom", killer ? killer->getKingdom() : QString()},
            {"killer_is_friend", killer && killer->isFriendWith(victim)}, {"attribution_complete", true}};
    }
    DeathStruct death;
    death.who = victim;
    death.damage = cursor->hasDamage ? &cursor->damage : nullptr;
    death.hplost = cursor->hasHpLost ? &cursor->hpLost : nullptr;
    cursor->data = QVariant::fromValue(death);
    continueDeath(cursor, m_room.getThread()->isMandatoryCleanup()
        && m_room.getThread()->isCascadeCancelled());
    if (cursor->completedOwner && !m_room.getThread()->hasActiveCascade() && !m_room.getThread()->isCascadeCancelled())
        m_room.finishTriggerCascade(cursor->completedOwner, true);
}

void PlayerLifecycleService::adoptCancelledDeaths(quint64 from, quint64 owner)
{
    for (const auto &cursor : m_pendingDeaths) if (cursor->owner == from) cursor->owner = owner;
}

void PlayerLifecycleService::finishCancelledDeaths(quint64 cascadeId)
{
    for (int i = 0; i < m_pendingDeaths.size();) {
        if (m_pendingDeaths[i]->owner != cascadeId) { ++i; continue; }
        const auto cursor = m_pendingDeaths.takeAt(i);
        try { continueDeath(cursor); }
        catch (const TriggerCascadeBreak &cancel) { if (cancel.cascadeId != cascadeId) throw; }
    }
}

void PlayerLifecycleService::continueDeath(const std::shared_ptr<DeathCursor> &cursor, bool canonicalOnly)
{
    ServerPlayer *victim = cursor->victim;
    if (!victim || cursor->stage == DeathCursor::Finished) return;
    auto *thread = m_room.getThread();
    ++m_deathCursorDepth;
    const auto depthGuard = qScopeGuard([&] { --m_deathCursorDepth; });
    RoomThread::DyingContinuationScope continuation(*thread, !canonicalOnly
        && !(thread->isMandatoryCleanup() && thread->isCascadeCancelled()), cursor->retained ? cursor->owner : 0);
    DamageStruct *reason = cursor->hasDamage ? &cursor->damage : nullptr;
    QVariant &data = cursor->data;
    DeathStruct death = data.value<DeathStruct>();
    const auto event = [&](TriggerEvent e, ServerPlayer *target) {
        if (canonicalOnly || thread->isSettlementBudgetExhausted()) {
            RoomThread::MandatoryCleanupScope canonical(*thread);
            if (e == GameOverJudge || e == BuryVictim)
                return thread->triggerMandatoryGameRule(e, target, data);
            return false;
        }
        return thread->invokeDyingEvent(continuation, e, target, data);
    };
    try {
        while (cursor->stage != DeathCursor::Finished) {
            switch (cursor->stage) {
            case DeathCursor::Start: {
                cursor->stage = DeathCursor::BeforeOver;
                m_room.clearControllerRelation(victim);
                victim->setAlive(false);
                const QList<ServerPlayer *> affected = m_roster.alivePlayersAfter(victim);
                m_roster.removeAlive(victim);
                for (ServerPlayer *player : affected) m_room.broadcastProperty(player, "seat");
                break;
            }
            case DeathCursor::BeforeOver:
                cursor->stage = DeathCursor::Announce;
                if (event(BeforeGameOverJudge, victim)) cursor->stage = DeathCursor::Finished;
                break;
            case DeathCursor::Announce: {
                cursor->stage = DeathCursor::GameOver;
    m_room.updateStateItem();
    LogMessage log;
    log.to << victim;
    log.type = "#Contingency";
    QString hideFrom = victim->property("HideDeathIconFrom").toString();
    if (!hideFrom.isEmpty() || victim->property("HideDeathIcon").toBool())
        log.arg = "unknown";
    else
        log.arg = Config.EnableHegemony ? victim->getKingdom() : victim->getRole();
    if (reason && reason->from) {
        log.from = reason->from;
        log.type = reason->from == victim ? "#Suicide" : "#Murder";
    }
    m_notifier.sendLog(log, QList<ServerPlayer *>());

    m_room.broadcastProperty(victim, "alive");
    // Happy Rebel deaths preserve the same private role grants as living players.
    const bool preserveHiddenRoles = m_room.getMode() == QStringLiteral("7_happyrebel")
        && m_room.getTag("HappyRebelMode").toBool();
    if (!preserveHiddenRoles)
        m_room.revealRole(victim);
    m_notifier.doBroadcastNotify(S_COMMAND_KILL_PLAYER, victim->objectName());

                ResolutionHistoryEventGuard deathHistory(m_room.resolutionHistory(), "death", cursor->fact,
                                                          m_room.historyRecordingEnabled());
                if (deathHistory.id()) m_room.resolutionHistory().appendFact(deathHistory.id(), "death", cursor->fact);
                deathHistory.finish("completed");
                break;
            }
            case DeathCursor::GameOver:
                cursor->stage = DeathCursor::AfterGameOver;
                event(GameOverJudge, victim);
                break;
            case DeathCursor::AfterGameOver:
                cursor->stage = DeathCursor::Death;
                if (victim->isAlive()) cursor->stage = DeathCursor::Finished;
                else m_room.setEmotion(victim, "death");
                break;
            case DeathCursor::Death:
                if (!cursor->observersReady) {
                    cursor->observersReady = true;
                    for (ServerPlayer *p : m_roster.orderedFrom(m_room.current, true)) cursor->observers << p;
                }
                if (cursor->observerIndex < cursor->observers.size()) {
                    ServerPlayer *observer = cursor->observers[cursor->observerIndex++];
                    if (observer && (observer->isAlive() || observer == victim)) event(::Death, observer);
                } else cursor->stage = victim->isAlive() ? DeathCursor::Finished : DeathCursor::Clubs;
                break;
            case DeathCursor::Clubs: {
                cursor->stage = DeathCursor::Bury;
    foreach (const Skill *skill, victim->getSkillList()) {
        Skill::Frequency frequency = skill->Skill::getFrequency();
        if (!canonicalOnly && !thread->isMandatoryCleanup() && !thread->isSettlementBudgetExhausted())
            thread->invokeDyingCallback(continuation, [&] { frequency = skill->getFrequency(); });
        if (frequency == Skill::Club && !skill->getClubName().isEmpty())
            m_room.clearClub(skill->getClubName());
    }

                break;
            }
            case DeathCursor::Bury:
                cursor->stage = DeathCursor::Detach;
                try { event(BuryVictim, victim); }
                catch (TriggerEvent e) {
                    if (e == TurnBroken || e == StageChange) victim->setMark("wujieNoRewardAndPunish-Keep", 0);
                }
                break;
            case DeathCursor::Detach: {
                if ((canonicalOnly || thread->isSettlementBudgetExhausted()) && victim->isAlive()) {
                    m_room.m_cancelledHpCauses.remove(victim);
                    cursor->stage = DeathCursor::Finished; break;
                }
                cursor->stage = DeathCursor::Reason;
    victim->setMark("wujieNoRewardAndPunish-Keep", 0);
    victim->detachAllSkills();
                break;
            }
            case DeathCursor::Reason: {
                cursor->stage = DeathCursor::Spectate;
    death = data.value<DeathStruct>();
    if (death.damage) {
        QString deathReason = death.damage->reason;
        if (death.damage->card) {
            if (death.damage->card->isKindOf("SkillCard"))
                deathReason = death.damage->card->getSkillName();
            else
                deathReason = death.damage->card->objectName();
        }
        m_room.setPlayerProperty(victim, "My_Death_Reason", deathReason);
    }

                break;
            }
            case DeathCursor::Spectate: {
                cursor->stage = DeathCursor::Finished;
                const bool preserveHiddenRoles = m_room.getMode() == QStringLiteral("7_happyrebel")
                    && m_room.getTag("HappyRebelMode").toBool();
    if (!victim->isAlive() && Config.EnableAI) {
        bool exposeRoles = true;
        foreach (ServerPlayer *player, m_roster.alivePlayers()) {
            if (!player->isOffline())
                exposeRoles = false;
            if (victim->getState() != "robot") {
                // syncRole checks canSeeRole without granting new spectator visibility.
                if (preserveHiddenRoles)
                    m_room.syncRole(victim, player);
                else
                    m_room.notifyProperty(victim, player, "role");
            }
        }

        if (exposeRoles) {
            // Spectating an AI-only Hegemony table does not grant knowledge of
            // concealed generals. Roles are published by showGeneral instead.

            static QStringList continueList;
            if (continueList.isEmpty())
                continueList << "02_1v1" << "04_1v3" << "06_XMode";
            if (continueList.contains(Config.GameMode.mode_id))
                return;

            if (Config.AlterAIDelayAD) {
                Config.AIDelay = Config.AIDelayAD;
                if (Config.AIDelay > 0) {
                    const int playerCount = m_roster.players().length();
                    if (playerCount > 10)
                        Config.AIDelay = qMax(Config.AIDelay * 8 / playerCount, 100);
                }
            }
            if (!canonicalOnly && !thread->isSettlementBudgetExhausted()
                && victim->isOnline() && Config.SurrenderAtDeath
                && m_room.mode != "02_1v1" && m_room.mode != "06_XMode"
                && m_room.askForSkillInvoke(victim, "surrender", "yes", false))
                m_room.makeSurrender(victim);
        }
    }                break;
            }
            case DeathCursor::Finished: break;
            }
        }
    } catch (const TriggerCascadeBreak &cancel) {
        cursor->owner = cancel.cascadeId;
        if (thread->isNativeCommitActive()) {
            cursor->retained = true;
            if (!m_pendingDeaths.contains(cursor)) m_pendingDeaths << cursor;
        } else {
            // Nested dying/death events unwind their author cascade to its
            // oldest owner. Only the remaining canonical corpse operations
            // may run here; no failed death listener or kill entry is replayed.
            RoomThread::MandatoryCleanupScope cleanup(*thread);
            continueDeath(cursor, true);
        }
        throw;
    }
    cursor->completedOwner = continuation.cancelledCascadeId();
    continuation.rethrowCancelled();
}

void PlayerLifecycleService::changeHero(ServerPlayer *player, const QString &newGeneral,
                                        bool fullState, bool invokeStart,
                                        bool isSecondaryHero, bool sendLog, int startHp)
{
    QVariant changingData = newGeneral;
    if (m_eventDispatcher.dispatch(GeneralChange, player, changingData))
        return;

    const bool dragonPhoenixRevival = Config.EnableHegemony
        && player->isDead() && !isSecondaryHero
        && player->hasFlag("OriginalHegemonyDragonPhoenixReviving");
    if (dragonPhoenixRevival && !replaceDragonPhoenixGeneral(player, newGeneral))
        return;

    JsonArray arg;
    arg << static_cast<int>(S_GAME_EVENT_CHANGE_HERO) << player->objectName();
    arg << newGeneral << isSecondaryHero << sendLog;
    m_notifier.doBroadcastNotify(S_COMMAND_LOG_EVENT, arg);

    const QString oldKingdom = player->getKingdom();
    const bool hadSecondaryHero = player->getGeneral2() != nullptr;
    if (isSecondaryHero) {
        changePlayerGeneral2(player, newGeneral);
        if (!hadSecondaryHero)
            m_room.broadcastProperty(player, "general2");
    } else if (!dragonPhoenixRevival) {
        changePlayerGeneral(player, newGeneral);
    }

    int maxHp = player->getGeneralMaxHp();
    const int changedMaxHp = player->property("ChangeHeroMaxHp").toInt();
    if (changedMaxHp > 0 && !dragonPhoenixRevival) {
        m_room.setPlayerProperty(player, "ChangeHeroMaxHp", 0);
        maxHp = changedMaxHp - 1;
    }
    if (fullState)
        startHp = player->getGeneralStartHp();

    player->setMaxHp(maxHp);
    if (startHp > 0)
        player->setHp(qMin(startHp, maxHp));
    m_room.broadcastProperty(player, "maxhp");
    m_room.broadcastProperty(player, "hp");

    const General *general = isSecondaryHero ? player->getGeneral2() : player->getGeneral();
    QString kingdom = player->property("yinni_general_kingdom").toString();
    if (kingdom.isEmpty()) {
        kingdom = oldKingdom;
        if (general && !isSecondaryHero) {
            kingdom = general->getKingdom();
            if (general->getKingdoms().contains("+"))
                kingdom = m_room.askForKingdom(player, newGeneral + "_ChooseKingdom");
            else if (kingdom == "demon")
                kingdom = m_room.askForKingdom(player, "gamerule_demon");
            else if (kingdom == "god" && !m_room.scenario && newGeneral != "anjiang"
                     && !newGeneral.startsWith("boss_"))
                kingdom = m_room.askForKingdom(player, "gamerule_god");
        }
    } else {
        m_room.setPlayerProperty(player, "yinni_general_kingdom", "");
    }
    m_room.setPlayerProperty(player, "kingdom", kingdom);

    if (general) {
        foreach (const Skill *skill, general->getSkillList()) {
            if (dragonPhoenixRevival && player->getSkillInstanceIds(skill->objectName()).isEmpty())
                continue;
            kingdom = skill->getLimitMark();
            if (!kingdom.isEmpty()
                && !player->getTag("DontGiveLimitMark_" + skill->objectName()).toBool()) {
                player->setTag("DontGiveLimitMark_" + skill->objectName(), true);
                m_room.setPlayerMark(player, kingdom, 1);
            }
            if (skill->inherits("ViewAsEquipSkill")) {
                const ViewAsEquipSkill *viewAsEquip = Sanguosha->getViewAsEquipSkill(skill->objectName());
                QString view;
                if (!m_room.getThread()->invokeStructuralCallback([&] { view = viewAsEquip->viewAsEquip(player); })) continue;
                if (!view.isEmpty()) {
                    foreach (const QString &equipName, view.split(",")) {
                        if (Sanguosha->getViewAsSkill(equipName))
                            m_skillRuntime.attachSkillToPlayer(player, equipName);
                    }
                }
            } else if (skill->inherits("TriggerSkill")) {
                const TriggerSkill *triggerSkill = qobject_cast<const TriggerSkill *>(skill);
                m_eventDispatcher.registerTriggerSkill(triggerSkill);
                bool eligible = false;
                if (invokeStart && triggerSkill->hasEvent(GameStart))
                    m_room.getThread()->invokeStructuralCallback([&] { eligible = triggerSkill->triggerable(player, &m_room, GameStart); });
                if (eligible) {
                    QVariant data;
                    m_room.getThread()->invokeStructuralCallback([&] { triggerSkill->trigger(GameStart, &m_room, player, data); });
                }
            }
        }
    }

    m_room.resetAI(player);
    QVariant changedData = newGeneral;
    m_eventDispatcher.dispatch(GeneralChanged, player, changedData);
    if (dragonPhoenixRevival)
        player->setTag("OriginalHegemonyDragonPhoenixResult", true);
}

bool PlayerLifecycleService::replaceDragonPhoenixGeneral(ServerPlayer *player,
                                                         const QString &newGeneral)
{
    const General *replacement = Sanguosha->getGeneral(newGeneral);
    QStringList names = m_room.getTag(player->objectName()).toStringList();
    if (!replacement || names.size() != 2)
        return false;

    player->removeGeneral(false);
    if (!player->getActualGeneral2Name().startsWith(QStringLiteral("sujiang")))
        return false;

    // Retire exact roots through the existing coordinator. Its normal teardown
    // removes helpers and cross-player attachments without reusing instance IDs.
    // Clearing the container or losing skills by name would bypass that lifecycle.
    const QList<SkillInstance> oldInstances = player->getSkillInstances();
    foreach (const SkillInstance &instance, oldInstances) {
        if (instance.source != SourceHelper)
            m_skillRuntime.detachSkillFromPlayer(player,
                SkillInstanceUtils::formatName(instance.skillName, instance.instanceID),
                false, false, true);
    }

    names[0] = newGeneral;
    names[1] = player->getActualGeneral2Name();
    m_room.setTag(player->objectName(), names);
    // Actual identities stay owner-only. Publish only the explicitly revealed
    // replacement, and update actual slots before changeHero recalculates HP.
    m_room.safeSetPlayerProperty(player, "actual_general1", newGeneral);
    m_room.notifyProperty(player, player, "actual_general1");
    m_room.setPlayerProperty(player, "general_showed", true);
    m_room.setPlayerProperty(player, "general", newGeneral);
    m_room.setPlayerProperty(player, "gender", replacement->getGender());
    m_room.setPlayerProperty(player, "ChangeHeroMaxHp", 0);

    QSet<QString> relatedNames;
    foreach (const Skill *skill, replacement->getSkillList()) {
        foreach (const Skill *related, Sanguosha->getRelatedSkills(skill->objectName()))
            relatedNames.insert(related->objectName());
    }
    foreach (const Skill *skill, replacement->getSkillList()) {
        if (!skill->relateToPlace(false) && !relatedNames.contains(skill->objectName()))
            player->addSkill(skill->objectName(), true);
    }
    player->setSkillsPreshowed("h");
    m_room.filterCards(player, player->getCards("he"), true);
    player->syncHegemonyRevealState();
    player->refreshUIState();
    return true;
}

void PlayerLifecycleService::changePlayerGeneral(ServerPlayer *player,
                                                 const QString &newGeneral)
{
    const General *general = player->getGeneral();
    QStringList skillNames;
    if (general) {
        QSet<QString> relatedNames;
        foreach (const Skill *parent, general->getSkillList()) {
            foreach (const Skill *related, Sanguosha->getRelatedSkills(parent->objectName()))
                relatedNames.insert(related->objectName());
        }
        foreach (const Skill *skill, general->getSkillList()) {
            if (relatedNames.contains(skill->objectName()))
                continue;
            skillNames << skill->objectName();
            player->loseSkill(skillNames.last(), true);
            if (skill->isChangeSkill()) {
                foreach (const QString &mark, player->getMarkNames()) {
                    if (mark.startsWith("&" + skillNames.last()) && mark.endsWith("_num"))
                        m_room.setPlayerMark(player, mark, 0);
                }
            }
            const QString limitMark = skill->getLimitMark();
            if (!limitMark.isEmpty())
                m_room.setPlayerMark(player, limitMark, 0);
            if (skill->inherits("ViewAsEquipSkill")) {
                const ViewAsEquipSkill *viewAsEquip = Sanguosha->getViewAsEquipSkill(skillNames.last());
                const QString view = viewAsEquip->viewAsEquip(player);
                if (view.isEmpty())
                    continue;
                foreach (const QString &equipName, view.split(",")) {
                    if (Sanguosha->getViewAsSkill(equipName))
                        m_skillRuntime.detachSkillFromPlayer(player, equipName, true, false, true);
                }
            }
        }
    }
    foreach (const Card *card, player->getCards("he")) {
        if (skillNames.contains(card->getSkillName()))
            m_room.filterCards(player, QList<const Card *>() << card, true);
    }

    m_room.setPlayerProperty(player, "general", newGeneral);
    general = player->getGeneral();
    player->setGender(general->getGender());
    m_room.setPlayerProperty(player, "kingdom", general->getKingdom());

    QSet<QString> relatedNames;
    foreach (const Skill *parent, general->getSkillList()) {
        foreach (const Skill *related, Sanguosha->getRelatedSkills(parent->objectName()))
            relatedNames.insert(related->objectName());
    }
    foreach (const Skill *skill, general->getSkillList()) {
        if (!relatedNames.contains(skill->objectName()))
            player->addSkill(skill->objectName(), true);
    }
    m_room.filterCards(player, player->getCards("he"), false);
    foreach (ServerPlayer *receiver, m_roster.players())
        m_skillRuntime.notifySkillInstanceSnapshot(receiver);
}

void PlayerLifecycleService::changePlayerGeneral2(ServerPlayer *player,
                                                  const QString &newGeneral)
{
    const General *general = player->getGeneral2();
    QStringList skillNames;
    if (general) {
        QSet<QString> relatedNames;
        foreach (const Skill *parent, general->getSkillList()) {
            foreach (const Skill *related, Sanguosha->getRelatedSkills(parent->objectName()))
                relatedNames.insert(related->objectName());
        }
        foreach (const Skill *skill, general->getSkillList()) {
            if (relatedNames.contains(skill->objectName()))
                continue;
            skillNames << skill->objectName();
            player->loseSkill(skillNames.last(), false);
            if (skill->isChangeSkill()) {
                foreach (const QString &mark, player->getMarkNames()) {
                    if (mark.startsWith("&" + skillNames.last()) && mark.endsWith("_num"))
                        m_room.setPlayerMark(player, mark, 0);
                }
            }
            const QString limitMark = skill->getLimitMark();
            if (!limitMark.isEmpty())
                m_room.setPlayerMark(player, limitMark, 0);
            if (skill->inherits("ViewAsEquipSkill")) {
                const ViewAsEquipSkill *viewAsEquip = Sanguosha->getViewAsEquipSkill(skillNames.last());
                const QString view = viewAsEquip->viewAsEquip(player);
                if (view.isEmpty())
                    continue;
                foreach (const QString &equipName, view.split(",")) {
                    if (Sanguosha->getViewAsSkill(equipName))
                        m_skillRuntime.detachSkillFromPlayer(player, equipName, true, false, true);
                }
            }
        }
        foreach (const Card *card, player->getCards("he")) {
            if (skillNames.contains(card->getSkillName()))
                m_room.filterCards(player, QList<const Card *>() << card, true);
        }
    }

    m_room.setPlayerProperty(player, "general2", newGeneral);
    general = player->getGeneral2();
    if (general) {
        QSet<QString> relatedNames;
        foreach (const Skill *parent, general->getSkillList()) {
            foreach (const Skill *related, Sanguosha->getRelatedSkills(parent->objectName()))
                relatedNames.insert(related->objectName());
        }
        foreach (const Skill *skill, general->getSkillList()) {
            if (!relatedNames.contains(skill->objectName()))
                player->addSkill(skill->objectName(), false);
        }
    }
    m_room.filterCards(player, player->getCards("he"), false);
    foreach (ServerPlayer *receiver, m_roster.players())
        m_skillRuntime.notifySkillInstanceSnapshot(receiver);
}

void PlayerLifecycleService::requestSummonBetween(ServerPlayer *before, ServerPlayer *after,
                                                  const QString &generalName)
{
    SummonRequest request;
    request.before = before;
    request.after = after;
    request.generalName = generalName;
    if (m_room.getThread()) request.cascadeId = m_room.getThread()->deferredCreatorCascadeId();
    m_pendingSummons.append(request);
}

bool PlayerLifecycleService::hasPendingSummons() const
{
    return !m_pendingSummons.isEmpty();
}

void PlayerLifecycleService::finishDeferredCascade(quint64 token, bool cancelled, quint64 parentToken)
{
    for (int i = m_pendingSummons.size() - 1; i >= 0; --i) {
        if (m_pendingSummons.at(i).cascadeId != token) continue;
        if (cancelled) m_pendingSummons.removeAt(i);
        else m_pendingSummons[i].cascadeId = parentToken;
    }
}

void PlayerLifecycleService::processPendingSummons()
{
    foreach (const SummonRequest &request, m_pendingSummons) {
        ServerPlayer *player = insertPlayerMidGame(request.before, request.after,
                                                   request.generalName);
        if (player)
            m_dynamicPlayers.append(player);
    }
    m_pendingSummons.clear();
}

ServerPlayer *PlayerLifecycleService::insertPlayerMidGame(ServerPlayer *before,
                                                          ServerPlayer *after,
                                                          const QString &generalName)
{
    Q_ASSERT(before != nullptr && after != nullptr && !generalName.isEmpty());
    if (!before || !after || generalName.isEmpty())
        return nullptr;
    if (before->getNextAlive() != after) {
        Q_ASSERT(false);
        return nullptr;
    }

    ServerPlayer *player = new ServerPlayer(&m_room);
    player->setObjectName(QString("sgs%1").arg(m_roster.players().length() + 1));
    player->setGeneralName(generalName);
    player->setProperty("avatar_general", generalName);

    const General *general = Sanguosha->getGeneral(generalName);
    const int hp = general ? general->getMaxHp() : 3;
    player->setMaxHp(hp);
    player->setHp(hp);
    player->setState("robot");

    before->setNext(player);
    player->setNext(after);
    const int aliveIndex = m_roster.alivePlayers().indexOf(before);
    m_roster.insertAfter(before, player, aliveIndex >= 0 || before->isAlive());

    QVariantMap info{{QStringLiteral("schema_version"), 1},
                     {QStringLiteral("player_name"), player->objectName()},
                     {QStringLiteral("screen_name"), player->screenName()},
                     {QStringLiteral("avatar"), generalName}};
    m_notifier.doBroadcastNotify(S_COMMAND_ADD_PLAYER_DYNAMIC, info);

    const QList<ServerPlayer *> players = m_roster.players();
    for (int i = 0; i < players.length(); ++i) {
        players[i]->setSeat(i + 1);
        m_room.broadcastProperty(players[i], "seat");
    }

    ArrangeSeatsMessage seats;
    foreach (ServerPlayer *existing, players)
        seats.playerNames << existing->objectName();
    seats.playOrderReversed = m_roster.isPlayOrderReversed();
    m_notifier.doBroadcastNotify(S_COMMAND_ARRANGE_SEATS, seats.toVariant());

    foreach (const Skill *skill, player->getVisibleSkillList()) {
        const TriggerSkill *triggerSkill = qobject_cast<const TriggerSkill *>(skill);
        if (triggerSkill)
            m_eventDispatcher.registerTriggerSkill(triggerSkill);
    }
    // The summoned player has no AI yet, and drawCards dispatches DrawNCards,
    // whose handler calls target->getSmartAI()->filterEvent() with no null
    // check (roomthread.cpp).  Attach one the same way every other path here
    // does, before anything can trigger.
    m_room.resetAI(player);
    m_cardMovement.drawCards(player, 4, "InitialHandCards", true, false);
    return player;
}

void PlayerLifecycleService::reconnect(ServerPlayer *player, ClientSocket *socket)
{
    if (socket != nullptr)
        player->setSocket(socket);
    player->setState("online");
    marshal(player);
    m_room.broadcastProperty(player, "state");
}

void PlayerLifecycleService::marshal(ServerPlayer *player)
{
    const QString syncId = QString::number(m_nextStateSyncId);
    m_nextStateSyncId = m_nextStateSyncId == std::numeric_limits<quint64>::max()
        ? 1 : m_nextStateSyncId + 1;
    StateSyncPayload sync;
    sync.syncId = syncId;
    sync.phase = QStringLiteral("begin");
    sync.reconnect = true;
    m_notifier.doNotify(player, S_COMMAND_STATE_SYNC, sync.toVariant());

    m_room.notifyProperty(player, player, "objectName");
    m_room.notifyProperty(player, player, "role");
    m_room.notifyProperty(player, player, "flags", "marshalling");

    ArrangeSeatsMessage seats;
    foreach (ServerPlayer *existing, m_roster.players()) {
        if (existing != player)
            existing->introduceTo(player);
        seats.playerNames << existing->objectName();
    }
    seats.playOrderReversed = m_roster.isPlayOrderReversed();
    m_notifier.doNotify(player, S_COMMAND_ARRANGE_SEATS, seats.toVariant());

    foreach (ServerPlayer *dynamicPlayer, m_dynamicPlayers) {
        QVariantMap info{{QStringLiteral("schema_version"), 1},
                         {QStringLiteral("player_name"), dynamicPlayer->objectName()},
                         {QStringLiteral("screen_name"), dynamicPlayer->screenName()},
                         {QStringLiteral("avatar"), dynamicPlayer->getGeneralName()}};
        m_notifier.doNotify(player, S_COMMAND_ADD_PLAYER_DYNAMIC, info);
    }

    m_notifier.doNotify(player, S_COMMAND_START_IN_X_SECONDS,
        QVariantMap{{QStringLiteral("schema_version"), 1},
                    {QStringLiteral("seconds"), 0}});
    foreach (ServerPlayer *existing, m_roster.players()) {
        m_room.notifyProperty(player, existing, "general");
        if (existing->getGeneral2())
            m_room.notifyProperty(player, existing, "general2");
        m_room.notifyProperty(player, existing, "state");
        m_room.notifyProperty(player, existing, "RestPlayer");
    }

    if (m_room.hasGameStarted())
        m_notifier.doNotify(player, S_COMMAND_GAME_START,
                            JsonUtils::toJsonArray(Sanguosha->getRandomCards()));

    foreach (ServerPlayer *existing, m_roster.players())
        existing->marshal(player);
    m_skillRuntime.notifySkillInstanceSnapshot(player);

    foreach (ServerPlayer *existing, m_roster.players()) {
        const QMap<QString, QHash<QString, QString> > swaps = existing->getAllSkillDescriptionSwaps();
        foreach (const QString &skillName, swaps.keys()) {
            if (Config.EnableHegemony && player != existing) {
                QString baseName;
                const int id = SkillInstanceUtils::parseName(skillName, baseName);
                bool visible = false;
                for (const SkillInstance &instance : existing->getSkillInstances()) {
                    if (instance.skillName == baseName && (id <= 0 || instance.instanceID == id)
                        && SkillRuntimeCoordinator::canReceiveSkillInstance(m_room, player, existing, instance)) {
                        visible = true;
                        break;
                    }
                }
                if (!visible) continue;
            }
            const QHash<QString, QString> swap = swaps[skillName];
            foreach (const QString &key, swap.keys()) {
                JsonArray arg;
                arg << existing->objectName() << skillName << key << swap[key];
                m_notifier.doNotify(player, S_COMMAND_SKILL_DESCRIPTION_SWAP, arg);
            }
        }
    }

    foreach (ServerPlayer *existing, m_roster.players()) {
        const QMap<QString, QHash<QString, QString> > swaps = existing->getAllCardDescriptionSwaps();
        foreach (const QString &cardName, swaps.keys()) {
            const QHash<QString, QString> swap = swaps[cardName];
            foreach (const QString &key, swap.keys()) {
                JsonArray arg;
                arg << existing->objectName() << cardName << key << swap[key];
                m_notifier.doNotify(player, S_COMMAND_UPDATE_CARD_DESC, arg);
            }
        }
    }

    foreach (ServerPlayer *controlled, m_roster.orderedFrom(m_room.current, true)) {
        if (controlled == player || m_room.getActualController(controlled) != player)
            continue;
        JsonArray knownCardsArg;
        knownCardsArg << controlled->objectName()
                      << JsonUtils::toJsonArray(controlled->handCards());
        m_notifier.doNotify(player, S_COMMAND_SET_KNOWN_CARDS, knownCardsArg);
    }

    foreach (const QVariant &chatMessage, m_room.m_chatHistory)
        m_notifier.doNotify(player, S_COMMAND_SPEAK, chatMessage);

    m_room.notifyProperty(player, player, "flags", "-marshalling");
    SwitchContextMessage contextMessage;
    contextMessage.playerName = player->objectName();
    m_notifier.doNotify(player, S_COMMAND_SWITCH_CONTEXT, contextMessage.toVariant());

    if (m_room.hasGameStarted()) {
        m_notifier.doNotify(player, S_COMMAND_UPDATE_PILE,
                            QVariant(m_cardMovement.drawPile().length()));
        if (!m_room.m_fillAGarg.isEmpty()) {
            m_notifier.doNotify(player, S_COMMAND_FILL_AMAZING_GRACE, m_room.m_fillAGarg);
            foreach (const JsonArray &takeArg, m_room.m_takeAGargs)
                m_notifier.doNotify(player, S_COMMAND_TAKE_AMAZING_GRACE, takeArg);
        }
        m_notifier.doNotify(player, S_COMMAND_SYNCHRONIZE_DISCARD_PILE,
                            JsonUtils::toJsonArray(m_cardMovement.discardPile()));
    }

    sync.phase = QStringLiteral("end");
    // This snapshot remains staged until STATE_SYNC end commits it atomically.
    m_room.notifyResolutionState(QStringLiteral("reset"), player);
    m_notifier.doNotify(player, S_COMMAND_STATE_SYNC, sync.toVariant());
}
