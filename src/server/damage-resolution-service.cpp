#include "damage-resolution-service.h"

#include "card-lifetime-manager.h"
#include "engine.h"
#include "json.h"
#include "player-lifecycle-service.h"
#include "player-state-service.h"
#include "protocol.h"
#include "resolution-history.h"
#include "room.h"
#include "room-runtime.h"
#include "room-state.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>

using namespace QSanProtocol;

DamageResolutionService::DamageResolutionService(Room &room)
    : m_room(room)
{
}

QVariantMap DamageResolutionService::queryActualDamage(const QVariantMap &filter) const
{
    QVariantMap query = filter;
    query.insert(QStringLiteral("kind"), QStringLiteral("actual_damage"));
    QVariantMap result = m_room.queryHistoryFacts(query);
    QVariantList facts = result.value(QStringLiteral("items")).toList();
    for (QVariant &value : facts) {
        QVariantMap fact = value.toMap();
        QVariantMap data = fact.value(QStringLiteral("data")).toMap();
        int absorbed = 0;
        int hpLoss = 0;
        bool hasArmorSnapshot = false;
        bool armorSnapshotComplete = true;
        QVariant armorBefore, armorAfter;
        QVariantMap components{{QStringLiteral("kind"), QStringLiteral("damage_component")},
            {QStringLiteral("event_id"), fact.value(QStringLiteral("event_id"))},
            {QStringLiteral("watermark"), result.value(QStringLiteral("watermark"))}};
        for (;;) {
            const QVariantMap page = m_room.m_resolutionHistory.queryFacts(components);
            // Derived damage amounts require component coverage as well as the
            // actual-damage kind; imported snapshots may guarantee only one.
            result.insert(QStringLiteral("complete"), result.value(QStringLiteral("complete")).toBool()
                && page.value(QStringLiteral("complete")).toBool());
            result.insert(QStringLiteral("coverage_complete"), result.value(QStringLiteral("coverage_complete")).toBool()
                && page.value(QStringLiteral("coverage_complete")).toBool());
            if (page.contains(QStringLiteral("error")))
                result.insert(QStringLiteral("error"), page.value(QStringLiteral("error")));
            for (const QVariant &entry : page.value(QStringLiteral("items")).toList()) {
                const QVariantMap part = entry.toMap().value(QStringLiteral("data")).toMap();
                if (part.value(QStringLiteral("component")) == QStringLiteral("armor")) {
                    absorbed += part.value(QStringLiteral("amount")).toInt();
                    if (part.contains(QStringLiteral("armor_before")) && part.contains(QStringLiteral("armor_after"))) {
                        if (!hasArmorSnapshot) armorBefore = part.value(QStringLiteral("armor_before"));
                        armorAfter = part.value(QStringLiteral("armor_after"));
                        hasArmorSnapshot = true;
                    } else armorSnapshotComplete = false;
                } else if (part.value(QStringLiteral("component")) == QStringLiteral("hp"))
                    hpLoss += part.value(QStringLiteral("amount")).toInt();
            }
            if (!page.value(QStringLiteral("has_more")).toBool()) break;
            components.insert(QStringLiteral("after"), page.value(QStringLiteral("next_after")));
        }
        // Components are immutable. This read-only projection never claims
        // pending HP loss has happened when an armor callback interrupts it.
        data.insert(QStringLiteral("absorbed"), absorbed);
        data.insert(QStringLiteral("hp_loss"), hpLoss);
        data.insert(QStringLiteral("amount"), absorbed + hpLoss);
        // Armor removal is observed at its own commit, never after LostHujia's nested effects.
        if (hasArmorSnapshot && armorSnapshotComplete) {
            data.insert(QStringLiteral("armor_before"), armorBefore);
            data.insert(QStringLiteral("armor_after"), armorAfter);
        }
        fact.insert(QStringLiteral("data"), data);
        value = fact;
    }
    result.insert(QStringLiteral("items"), facts);
    result.insert(QStringLiteral("facts"), facts);
    return result;
}

QVariantMap DamageResolutionService::queryCardUseDamage(qint64 useEventId) const
{
    const QVariantMap use = useEventId ? m_room.historyEvent(useEventId)
        : m_room.historyParent(m_room.currentHistoryEventId(), "use_card", true);
    if (use.value("kind").toString() != "use_card")
        return {{"complete", false}, {"attribution_complete", false}};
    useEventId = use.value("id").toLongLong();
    const QVariantMap usedCard = use.value("data").toMap().value("card").toMap();
    QVariantMap query{{"turn_id", use.value("turn_id")}};
    QVariantMap result;
    QVariantList items;
    bool complete = true;
    for (;;) {
        const QVariantMap page = queryActualDamage(query);
        if (result.isEmpty()) {
            result = page;
            query.insert("watermark", page.value("watermark"));
        }
        complete = complete && page.value("complete").toBool();
        for (const QVariant &entry : page.value("items").toList()) {
            const QVariantMap fact = entry.toMap();
            const QVariantMap damageCard = fact.value("data").toMap().value("card").toMap();
            if (damageCard.isEmpty()) continue;
            bool sameCard = true;
            for (const QString &key : {QStringLiteral("id"), QStringLiteral("name"),
                     QStringLiteral("class_name"), QStringLiteral("virtual"), QStringLiteral("subcards")})
                sameCard = sameCard && damageCard.value(key) == usedCard.value(key);
            if (!sameCard) continue;
            // Physical card IDs are reusable and virtual cards ephemeral. Anchor
            // damage to this use event, excluding independently nested card uses.
            if (m_room.historyParent(fact.value("event_id").toLongLong(), "use_card", true)
                    .value("id").toLongLong() == useEventId)
                items.append(entry);
        }
        if (!page.value("has_more").toBool()) break;
        query.insert("after", page.value("next_after"));
    }
    result.insert("items", items);
    result.insert("facts", items);
    result.insert("has_more", false);
    result.insert("complete", complete);
    // Attribution here is the enclosing use event, not the causing skill owner.
    result.insert("attribution_complete", complete);
    return result;
}

void DamageResolutionService::recordAppliedDamage(const DamageStruct &damage, int absorbed, int hpLoss)
{
    if (!m_room.historyRecordingEnabled()) return;
    const QVariantMap event = m_room.m_resolutionHistory.findParent(
        m_room.currentHistoryEventId(), QStringLiteral("damage"), true);
    const qint64 id = event.value(QStringLiteral("id")).toLongLong();
    if (!id || event.value(QStringLiteral("data")).toMap().value(QStringLiteral("applied")).toBool()) return;
    QVariantMap fact = m_room.historyCause(CardMoveReason(CardMoveReason::S_REASON_UNKNOWN,
        damage.from ? damage.from->objectName() : QString(), damage.reason, QString()));
    fact.insert(QStringLiteral("from"), damage.from ? damage.from->objectName() : QString());
    fact.insert(QStringLiteral("to"), damage.to ? damage.to->objectName() : QString());
    fact.insert(QStringLiteral("requested_amount"), damage.damage);
    fact.insert(QStringLiteral("amount"), absorbed + hpLoss);
    fact.insert(QStringLiteral("absorbed"), absorbed);
    fact.insert(QStringLiteral("hp_loss"), hpLoss);
    fact.insert(QStringLiteral("nature"), int(damage.nature));
    fact.insert(QStringLiteral("chain"), damage.chain);
    fact.insert(QStringLiteral("transfer"), damage.transfer);
    fact.insert(QStringLiteral("card"), m_room.historyCardSnapshot(damage.card));
    // Commit once before any post-mutation rule callback can query or abort.
    m_room.m_resolutionHistory.appendFact(id, QStringLiteral("actual_damage"), fact);
    m_room.m_resolutionHistory.updateEvent(id, {{QStringLiteral("applied"), true}});
}

void DamageResolutionService::applyDamageHp(ServerPlayer *player, const DamageStruct &damage, int absorbed, int hpLoss)
{
    m_room.m_playerState->setPlayerProperty(player, "hp", player->getHp() - hpLoss,
        [this, damage, absorbed, hpLoss]() {
            recordDamageComponent(damage, QStringLiteral("hp"), hpLoss);
            recordAppliedDamage(damage, absorbed, hpLoss);
        });
}

void DamageResolutionService::recordDamageComponent(const DamageStruct &damage, const QString &component, int amount)
{
    if (!m_room.historyRecordingEnabled() || amount <= 0) return;
    const QVariantMap event = m_room.m_resolutionHistory.findParent(
        m_room.currentHistoryEventId(), QStringLiteral("damage"), true);
    const qint64 id = event.value(QStringLiteral("id")).toLongLong();
    if (!id) return;
    QVariantMap part{{QStringLiteral("component"), component}, {QStringLiteral("amount"), amount}};
    if (component == QLatin1String("armor") && damage.to) {
        // loseHujia supplies the committed removal before LostHujia observers.
        // @HuJia never dispatches MarkChanged, so this is still the mutation boundary.
        const int after = damage.to->getHujia();
        part.insert(QStringLiteral("armor_before"), after + amount);
        part.insert(QStringLiteral("armor_after"), after);
    }
    m_room.m_resolutionHistory.appendFact(id, QStringLiteral("damage_component"), part);
    // The first irreversible component makes this actual damage visible;
    // later components extend the projection without rewriting old facts.
    recordAppliedDamage(damage, component == QLatin1String("armor") ? amount : 0,
                        component == QLatin1String("hp") ? amount : 0);
}

void DamageResolutionService::enterDying(ServerPlayer *player, DamageStruct *reason, HpLostStruct *hpLost)
{
    auto cursor = std::make_shared<Room::DyingCursor>();
    cursor->player = player;
    cursor->owner = m_room.thread->cascadeId();
    if (reason) {
        cursor->hasDamage = true;
        cursor->damage = *reason;
        cursor->causeLease = CardLifetimeLease(globalCardLifetimeManager(),
                                               globalCardLifetimeManager().liveToken(reason->card));
    }
    if (hpLost) { cursor->hasHpLost = true; cursor->hpLost = *hpLost; }
    DyingStruct dying;
    dying.who = player;
    dying.damage = cursor->hasDamage ? &cursor->damage : nullptr;
    dying.hplost = cursor->hasHpLost ? &cursor->hpLost : nullptr;
    cursor->data = QVariant::fromValue(dying);
    continueDying(cursor);
    if (cursor->completedOwner && !m_room.thread->hasActiveCascade() && !m_room.thread->isCascadeCancelled())
        m_room.finishTriggerCascade(cursor->completedOwner, true);
}

void DamageResolutionService::continueDying(const std::shared_ptr<Room::DyingCursor> &cursor)
{
    ServerPlayer *player = cursor->player;
    if (!player) return;
    if (cursor->stage == Room::DyingCursor::Finished) {
        m_room.m_cancelledHpCauses.remove(player);
        if (cursor->retained && cursor->owner) m_room.m_completedDying[cursor->owner].insert(player);
        return;
    }
    ++m_room.m_dyingCursorDepth;
    const auto cursorGuard = qScopeGuard([&] { --m_room.m_dyingCursorDepth; });
    RoomThread::DyingContinuationScope continuation(*m_room.thread, true, cursor->retained ? cursor->owner : 0);
    const QVariant previousDying = m_room.getTag("CurrentDying");
    const bool previousFlag = player->hasFlag("Global_Dying");
    const QVariant previousSaver = player->getTag("MyDyingSaver");
    const auto restoreFrame = qScopeGuard([&] {
        if (previousDying.isValid()) m_room.setTag("CurrentDying", previousDying);
        else m_room.removeTag("CurrentDying");
        m_room.setPlayerFlag(player, previousFlag ? "Global_Dying" : "-Global_Dying");
        if (previousSaver.isValid()) player->setTag("MyDyingSaver", previousSaver);
        else player->removeTag("MyDyingSaver");
    });
    if (cursor->retained) {
        if (cursor->saverAtDeferral.isValid()) player->setTag("MyDyingSaver", cursor->saverAtDeferral);
        else player->removeTag("MyDyingSaver");
    }
    DamageStruct *reason = cursor->hasDamage ? &cursor->damage : nullptr;
    HpLostStruct *hpLost = cursor->hasHpLost ? &cursor->hpLost : nullptr;
    Room::ResolutionScope resolution(m_room, "dying", player, reason ? reason->from : nullptr, player);
    QVariantMap dyingFact{{"player", player->objectName()}, {"to", player->objectName()},
        {"from", reason && reason->from ? reason->from->objectName() : QString()},
        {"reason", reason ? reason->reason : hpLost ? hpLost->reason : QString()},
        {"hp", player->getHp()}, {"alive", player->isAlive()}, {"attribution_complete", true}};
    if (!cursor->history && m_room.historyRecordingEnabled()) {
        cursor->history = m_room.m_resolutionHistory.beginEvent("dying", dyingFact, false);
        m_room.m_resolutionHistory.appendFact(cursor->history, "dying_start", dyingFact);
    }
    // The scope guard must be declared before the context guard so it runs last:
    // finishing the event removes its id from the active stack, and the context
    // guard has to close while that id is still there. Reversing the order left
    // the journal permanently incomplete and silently disabled every
    // negative-count rule (keji and friends) for the rest of the game.
    const auto finishHistory = qScopeGuard([&] {
        if (!cursor->history) return;
        if (cursor->stage == Room::DyingCursor::Finished) m_room.m_resolutionHistory.finishEvent(cursor->history, "completed");
        else if (!cursor->retained) m_room.m_resolutionHistory.finishEvent(cursor->history, "interrupted");
    });
    ResolutionHistoryContextGuard historyContext(m_room.m_resolutionHistory, cursor->history, cursor->history != 0);
    if (cursor->stage != Room::DyingCursor::Start && (cursor->resolved || !player->isAlive() || player->getHp() > 0))
        cursor->stage = Room::DyingCursor::Quit;
    m_room.setPlayerFlag(player, "Global_Dying");
    QStringList current = previousDying.toStringList();
    current << player->objectName();
    m_room.setTag("CurrentDying", current);
    const auto event = [&](TriggerEvent e, ServerPlayer *target) {
        if (m_room.thread->isSettlementBudgetExhausted()) return false;
        return m_room.thread->invokeDyingEvent(continuation, e, target, cursor->data,
            e == AskForPeachesDone ? &cursor->nativeDecisionStarted : nullptr);
    };
    try {
        while (cursor->stage != Room::DyingCursor::Finished) {
            if (!m_room.thread->settlementCursorCheckpoint(player)) {
                // The offending callback has unwound. A retained frame must
                // wait until its canonical card/container commit is complete.
                if (m_room.thread->isNativeCommitActive() || m_room.thread->isDyingEventActive() || m_room.m_dyingCursorDepth > 1)
                    throw TriggerCascadeBreak{m_room.thread->cascadeId()};
                RoomThread::MandatoryCleanupScope canonical(*m_room.thread);
                const quint64 failedContext = m_room.thread->cascadeId();
                m_room.finishTriggerCascade(failedContext, true);
                adoptCancelledDying(failedContext, continuation.cancelledCascadeId()
                    ? continuation.cancelledCascadeId() : failedContext);
                const bool unfinished = cursor->stage != Room::DyingCursor::Quit || cursor->decisionIncomplete;
                const bool forceDeath = unfinished && !cursor->resolved && !cursor->nativeDecisionStarted
                    && player->isAlive() && player->getHp() <= 0
                    && player->hasFlag("Global_Dying");
                cursor->decisionIncomplete = false;
                cursor->stage = Room::DyingCursor::Quit;
                if (forceDeath) {
                    qWarning().noquote() << "ROOMTHREAD_SETTLEMENT_FORCED_DEATH"
                        << QJsonDocument(QJsonObject{{"room", m_room.getId()}, {"player", player->objectName().left(64)},
                            {"phase", int(player->getPhase())}}).toJson(QJsonDocument::Compact);
                    // Keep the original cause and existing native death/mode
                    // settlement. Optional author death/rescue work is cut.
                    m_room.killPlayer(player, reason, hpLost);
                }
            }
            switch (cursor->stage) {
            case Room::DyingCursor::Start: {
                cursor->stage = Room::DyingCursor::Enter;
                JsonArray arg;
                arg << S_GAME_EVENT_PLAYER_DYING << player->objectName();
                m_room.doBroadcastNotify(S_COMMAND_LOG_EVENT, arg);
                break;
            }
            case Room::DyingCursor::Enter:
                cursor->stage = Room::DyingCursor::Observers;
                if (event(EnterDying, player) || !player->hasFlag("Global_Dying"))
                    cursor->stage = Room::DyingCursor::Quit;
                break;
            case Room::DyingCursor::Observers:
                if (!cursor->observersReady) {
                    cursor->observersReady = true;
                    for (ServerPlayer *p : m_room.getAllPlayers()) cursor->observers << p;
                }
                if (!player->hasFlag("Global_Dying")) { cursor->stage = Room::DyingCursor::Quit; break; }
                if (cursor->observerIndex < cursor->observers.size()) {
                    ServerPlayer *observer = cursor->observers[cursor->observerIndex++];
                    if (observer && (event(Dying, observer) || !player->hasFlag("Global_Dying")))
                        cursor->observerIndex = cursor->observers.size();
                } else {
                    cursor->stage = Room::DyingCursor::Savers;
                    LogMessage log;
                    log.type = "#AskForPeaches";
                    log.from = player;
                    log.to = m_room.getAllPlayers();
                    log.arg = QString::number(1-player->getHp());
                    for (ServerPlayer *saver : log.to) cursor->savers << saver;
                    m_room.sendLog(log);
                }
                break;
            case Room::DyingCursor::Savers:
                if (!player->hasFlag("Global_Dying")) { cursor->stage = Room::DyingCursor::Quit; break; }
                if (cursor->saverIndex < cursor->savers.size()) {
                    ServerPlayer *saver = cursor->savers[cursor->saverIndex++];
                    if (!saver) break;
                    RoomState &state = m_room.m_runtime->state();
                    const QString previousSaver = state.getDyingSaver();
                    const QString previousTarget = state.getDyingSaveTarget();
                    state.setDyingSave(saver->objectName(), player->objectName());
                    const auto restoreSaver = qScopeGuard([&] { state.setDyingSave(previousSaver, previousTarget); });
                    event(AskForPeaches, saver);
                } else {
                    cursor->stage = Room::DyingCursor::PeachesDone;
                    m_room.notifyMoveFocus(player, S_COMMAND_ASK_PEACH);
                }
                break;
            case Room::DyingCursor::PeachesDone:
                cursor->stage = Room::DyingCursor::Quit;
                cursor->decisionIncomplete = true;
                event(AskForPeachesDone, player);
                if (!m_room.thread->isSettlementBudgetExhausted() || cursor->nativeDecisionStarted || !player->isAlive()
                    || player->getHp() > 0 || !player->hasFlag("Global_Dying"))
                    cursor->decisionIncomplete = false;
                break;
            case Room::DyingCursor::Quit: {
                cursor->stage = Room::DyingCursor::Finished;
                m_room.setPlayerFlag(player, "-Global_Dying");
                current = m_room.getTag("CurrentDying").toStringList();
                current.removeOne(player->objectName());
                m_room.setTag("CurrentDying", current);
                if (player->isAlive()) {
                    JsonArray arg;
                    arg << S_GAME_EVENT_PLAYER_QUITDYING << player->objectName();
                    m_room.doBroadcastNotify(S_COMMAND_LOG_EVENT, arg);
                }
                dyingFact.insert("hp", player->getHp());
                dyingFact.insert("alive", player->isAlive());
                if (cursor->history) m_room.m_resolutionHistory.appendFact(cursor->history, "dying_result", dyingFact);
                event(QuitDying, player);
                player->removeTag("MyDyingSaver");
                break;
            }
            case Room::DyingCursor::Finished: break;
            }
        }
    } catch (const TriggerCascadeBreak &cancel) {
        // Physical commits and nested total exhaustion must unwind the
        // offending callback before this original frame can continue. Retain
        // the already-advanced cursor, never replay EnterDying or a saver.
        cursor->owner = cancel.cascadeId;
        cursor->resolved = !player->hasFlag("Global_Dying") || !player->isAlive() || player->getHp() > 0;
        cursor->saverAtDeferral = player->getTag("MyDyingSaver");
        if (m_room.thread->isNativeCommitActive() || m_room.thread->isSettlementBudgetExhausted()) {
            cursor->retained = true;
            bool queued = false;
            for (const auto &pending : m_room.m_pendingDying)
                if (pending == cursor) queued = true;
            if (!queued) m_room.m_pendingDying << cursor;
        }
        throw;
    }
    if (continuation.cancelled()) {
        cursor->completedOwner = continuation.cancelledCascadeId();
        m_room.m_completedDying[cursor->completedOwner].insert(player);
    }
    if (cursor->retained && cursor->owner) m_room.m_completedDying[cursor->owner].insert(player);
    m_room.m_cancelledHpCauses.remove(player);
    continuation.rethrowCancelled();
}

void DamageResolutionService::adoptCancelledDying(quint64 from, quint64 owner)
{
    if (m_room.m_finishingCascadeDepth) owner = m_room.m_finishingCascadeId;
    if (from == owner) return;
    for (const auto &cursor : m_room.m_pendingDying) if (cursor->owner == from) cursor->owner = owner;
    m_room.m_playerLifecycle->adoptCancelledDeaths(from, owner);
    m_room.m_completedDying[owner].unite(m_room.m_completedDying.take(from));
    if (m_room.m_completedDying[owner].isEmpty()) m_room.m_completedDying.remove(owner);
}

ServerPlayer*DamageResolutionService::getCurrentDyingPlayer() const
{
	QStringList currentdying = m_room.getTag("CurrentDying").toStringList();
	if (currentdying.isEmpty()) return nullptr;
	foreach(ServerPlayer*p, m_room.getAlivePlayers()){
		if (p->objectName() == currentdying.last())
			return p;
	}
	return nullptr;
}

void DamageResolutionService::loseHp(ServerPlayer*victim, int lose, bool ignore_hujia, ServerPlayer*from, const QString&reason)
{
	HpLostStruct lost;
	lost.from = from;
	lost.to = victim;
	lost.lose = lose;
	lost.reason = reason;
	lost.ignore_hujia = ignore_hujia;
	loseHp(lost);
}

void DamageResolutionService::loseHp(const HpLostStruct&lost_data)
{
	if (!lost_data.to || lost_data.lose < 1 || lost_data.to->isDead()) return;
	QVariantMap history = m_room.historyCause(CardMoveReason(CardMoveReason::S_REASON_UNKNOWN,
		lost_data.from ? lost_data.from->objectName() : QString(), lost_data.reason, QString()));
	history.insert(QStringLiteral("from"), lost_data.from ? lost_data.from->objectName() : QString());
	history.insert(QStringLiteral("to"), lost_data.to->objectName());
	history.insert(QStringLiteral("player"), lost_data.to->objectName());
	history.insert(QStringLiteral("requested_amount"), lost_data.lose);
	history.insert(QStringLiteral("ignore_hujia"), lost_data.ignore_hujia);
	ResolutionHistoryEventGuard hpLostHistory(m_room.m_resolutionHistory, QStringLiteral("lose_hp"),
		history, m_room.historyRecordingEnabled());
	QVariant data = QVariant::fromValue(lost_data);
	if (m_room.thread->trigger(PreHpLost, &m_room, lost_data.to, data)) {
		hpLostHistory.finish(QStringLiteral("prevented"));
		return;
	}

	HpLostStruct lost = data.value<HpLostStruct>();
	if (!lost.to || lost.lose < 1 || lost.to->isDead()) {
		hpLostHistory.finish(QStringLiteral("prevented"));
		return;
	}

	int hujia = lost.ignore_hujia?0:lost.to->getHujia();
	if (hujia > 0){
		hujia = qMin(hujia,lost.lose);
		lost.to->loseHujia(hujia);
	}
	JsonArray arg;
	arg << lost.to->objectName() << -lost.lose << -1 << hujia;
	m_room.doBroadcastNotify(S_COMMAND_CHANGE_HP, arg);

	if (lost.lose > hujia){
		m_room.setTag("HpChangedData", data);
		m_room.setPlayerProperty(lost.to, "hp", lost.to->getHp()+hujia-lost.lose);
	}

	if (hpLostHistory.id()) {
		QVariantMap fact = m_room.historyCause(CardMoveReason(CardMoveReason::S_REASON_UNKNOWN,
			lost.from ? lost.from->objectName() : QString(), lost.reason, QString()));
		fact.insert(QStringLiteral("from"), lost.from ? lost.from->objectName() : QString());
		fact.insert(QStringLiteral("to"), lost.to->objectName());
		fact.insert(QStringLiteral("player"), lost.to->objectName());
		fact.insert(QStringLiteral("requested_amount"), lost.lose);
		fact.insert(QStringLiteral("ignore_hujia"), lost.ignore_hujia);
		// This is the local contribution used by loseHp, not a claim about
		// armor/HP committed after replacement or nested property callbacks.
		fact.insert(QStringLiteral("armor_requested_contribution"), hujia);
        // The phase event already exists during PhaseChanging. Freeze whether
        // its Start dispatch has happened now, not its later final state.
        const QVariantMap phase = m_room.historyEvent(m_room.historyScopes().value(QStringLiteral("phase_id")).toLongLong());
        const QVariantMap phaseData = phase.value(QStringLiteral("data")).toMap();
        if (phase.value(QStringLiteral("kind")).toString() == QLatin1String("phase")
            && phaseData.contains(QStringLiteral("entered")))
            fact.insert(QStringLiteral("phase_entered"), phaseData.value(QStringLiteral("entered")));
        m_room.m_resolutionHistory.appendFact(hpLostHistory.id(), QStringLiteral("hp_lost"), fact);
	}
	m_room.thread->trigger(HpLost, &m_room, lost.to, data);
	hpLostHistory.finish(QStringLiteral("completed"));
}

void DamageResolutionService::changePlayerMaxHp(ServerPlayer*player, int change, const QString&reason)
{
	if (change == 0 || player->isDead() || player->inYinniState())
		return;

	MaxHpStruct maxhp(player, change, reason);
	QVariant data = QVariant::fromValue(maxhp);
	if (m_room.thread->trigger(MaxHpChange, &m_room, player, data)) return;

	maxhp = data.value<MaxHpStruct>();
	if (maxhp.change==0||player->isDead()) return;

	player->setMaxHp(qMax(player->getMaxHp() + maxhp.change, 0));
	m_room.broadcastProperty(player, "maxhp");
	m_room.broadcastProperty(player, "hp");

	LogMessage log;
	log.type = "#GainMaxHp";
	log.arg = QString::number(maxhp.change);
	if(maxhp.change<0){
		log.type = "#LoseMaxHp";
		log.arg = QString::number(-maxhp.change);
	}
	log.from = player;
	m_room.sendLog(log);

	JsonArray arg;
	arg << player->objectName() << maxhp.change;
	m_room.doBroadcastNotify(S_COMMAND_CHANGE_MAXHP, arg);

	if (player->getMaxHp() <= 0) m_room.killPlayer(player);
	else m_room.thread->trigger(MaxHpChanged, &m_room, player, data);
}

void DamageResolutionService::loseMaxHp(ServerPlayer*victim, int lose, const QString&reason)
{
	changePlayerMaxHp(victim, -lose, reason);
}

void DamageResolutionService::gainMaxHp(ServerPlayer*player, int gain, const QString&reason)
{
	changePlayerMaxHp(player, gain, reason);
}

bool DamageResolutionService::changeMaxHpForAwakenSkill(ServerPlayer*player, int magnitude, const QString&reason)
{
	int n = player->getMark("@waked");
	m_room.addPlayerMark(player, "@waked");
	if (magnitude < 0){
		if (Config.Enable2ndGeneral&&player->getGeneral2()
			&&Config.MaxHpScheme > 0&&Config.PreventAwakenBelow3&&player->getMaxHp() <= 3){
			m_room.setPlayerMark(player, "AwakenLostMaxHp", 1);
		} else
			loseMaxHp(player, -magnitude, reason);
	} else if (magnitude > 0)
		gainMaxHp(player, magnitude, reason);
	foreach(QString skill_name, player->getTag(reason + "_SKILLCANWAKE").toStringList())
		m_room.setPlayerMark(player, "&" + skill_name + "+:+" + reason, 0);
	player->removeTag(reason + "_SKILLCANWAKE");
	return player->getMark("@waked") > n;
}

void DamageResolutionService::recover(ServerPlayer*player, const RecoverStruct&recover, bool set_emotion)
{
    this->recover(player, recover, set_emotion, nullptr);
}

void DamageResolutionService::recover(ServerPlayer*player, const RecoverStruct&recover, bool set_emotion, qint64 *historyEventId)
{
	if (historyEventId) *historyEventId = 0;
	if (player->isDead() || recover.recover <= 0 || player->getMark("command5_effect") > 0) return;
	Room::ResolutionScope resolution(m_room, QStringLiteral("recover"), player, recover.who, player);
	ResolutionHistoryEventGuard recoverHistory(m_room.m_resolutionHistory, QStringLiteral("recover"),
		{{QStringLiteral("from"), recover.who ? recover.who->objectName() : QString()},
		 {QStringLiteral("to"), player->objectName()}, {QStringLiteral("requested_amount"), recover.recover},
		 {QStringLiteral("card"), m_room.historyCardSnapshot(recover.card)},
		 {QStringLiteral("reason_skill"), recover.reason}}, m_room.historyRecordingEnabled());
	if (historyEventId) *historyEventId = recoverHistory.id();

	QVariant data = QVariant::fromValue(recover);
	if (m_room.thread->trigger(StartHpRecover,&m_room,player,data)||player->getLostHp()<=0||m_room.thread->trigger(PreHpRecover,&m_room,player,data)) {
		recoverHistory.finish(QStringLiteral("prevented"));
		return;
	}

	if (player->hasFlag("Global_Dying")&&recover.who){
		QStringList list = player->getTag("MyDyingSaver").toStringList();
		list << recover.who->objectName();
		player->setTag("MyDyingSaver", list);
	}

	RecoverStruct recover_struct = data.value<RecoverStruct>();
	recover_struct.recover = qMin(player->getMaxHp() - player->getHp(), recover_struct.recover);

	m_room.setEmotion(player, "recover_hp");

	JsonArray arg;
	arg << player->objectName() << recover_struct.recover << (recover.card&&recover.card->isKindOf("Peach")?0:1) << 0;
	m_room.doBroadcastNotify(S_COMMAND_CHANGE_HP, arg);

	m_room.setTag("HpChangedData", data);
	const int hpBefore = player->getHp();
	QVariantMap recoverFact = m_room.historyCause(CardMoveReason(CardMoveReason::S_REASON_UNKNOWN,
		recover_struct.who ? recover_struct.who->objectName() : QString(), recover_struct.reason, QString()));
	recoverFact.insert(QStringLiteral("from"), recover_struct.who ? recover_struct.who->objectName() : QString());
	recoverFact.insert(QStringLiteral("to"), player->objectName());
	recoverFact.insert(QStringLiteral("player"), player->objectName());
	recoverFact.insert(QStringLiteral("requested_amount"), data.value<RecoverStruct>().recover);
	recoverFact.insert(QStringLiteral("card"), m_room.historyCardSnapshot(recover_struct.card));
	const qint64 recoverEventId = recoverHistory.id();
	m_room.m_playerState->setPlayerProperty(player, "hp", qMin(hpBefore + recover_struct.recover, player->getMaxHp()),
		[this, player, hpBefore, recoverEventId, recoverFact]() mutable {
			const int hpAfter = player->getHp();
			if (!recoverEventId || hpAfter <= hpBefore) return;
			// Freeze only this committed HP increase, before HpChanged may recover or damage again.
			recoverFact.insert(QStringLiteral("hp_before"), hpBefore);
			recoverFact.insert(QStringLiteral("hp_after"), hpAfter);
			recoverFact.insert(QStringLiteral("amount"), hpAfter - hpBefore);
			m_room.m_resolutionHistory.appendFact(recoverEventId, QStringLiteral("actual_recover"), recoverFact);
		});

	if (set_emotion)
		m_room.setEmotion(player, "recover");/*

	if (player->getHp() > 0&&player->hasFlag("Global_Dying")){
		m_room.setPlayerFlag(player, "-Global_Dying");
		QStringList currentdying = m_room.getTag("CurrentDying").toStringList();
		currentdying.removeOne(player->objectName());
		m_room.setTag("CurrentDying", currentdying);
	}*/

	m_room.thread->trigger(HpRecover, &m_room, player, data);
	recoverHistory.finish(QStringLiteral("completed"));
}

ServerPlayer*DamageResolutionService::getSaver(ServerPlayer*player) const
{
	QStringList list = player->getTag("MyDyingSaver").toStringList();
	if (list.isEmpty()) return nullptr;
	foreach(ServerPlayer*p, m_room.getAlivePlayers()){
		if (p->objectName() == list.last())
			return p;
	}
	return nullptr;
}

void DamageResolutionService::resolveDamage(DamageStruct damage)
{
	if (damage.damage<1 || !damage.to->isAlive()) return;
	Room::ResolutionScope resolution(m_room, QStringLiteral("damage"), damage.to, damage.from, damage.to);
	ResolutionHistoryEventGuard damageHistory(m_room.m_resolutionHistory, QStringLiteral("damage"),
		{{QStringLiteral("from"), damage.from ? damage.from->objectName() : QString()},
		 {QStringLiteral("to"), damage.to->objectName()},
		 {QStringLiteral("amount"), damage.damage},
		 {QStringLiteral("card"), m_room.historyCardSnapshot(damage.card)},
		 {QStringLiteral("reason_skill"), damage.reason}}, m_room.historyRecordingEnabled());

	const int previousDepth = m_room.m_damageStack.size();
	const QVariant previousDamage = m_room.getTag("CurrentDamageStruct");
	QHash<ServerPlayer *, QVariant> previousTransfers;
	for (ServerPlayer *player : m_room.getPlayers()) previousTransfers.insert(player, player->getTag("TransferDamage"));
	try {
		bool prevented = true;
		QVariant data = QVariant::fromValue(damage);
		do {
			if (!damage.tips.contains("ConfirmDamage")){//(!damage.chain&&!damage.transfer){
				if(damage.reason.isEmpty()&&damage.card) damage.reason = damage.card->getSkillName();
				damage.tips << "ConfirmDamage" << QString("STARTDAMAGE:%1").arg(damage.damage);
				data.setValue(damage);
				if (m_room.thread->trigger(ConfirmDamage, &m_room, damage.from, data))
					break;
				damage = data.value<DamageStruct>();
			}

			if (m_room.thread->trigger(Predamage, &m_room, damage.from, data))
				break;
			damage = data.value<DamageStruct>();

			if (m_room.thread->trigger(DamageForseen, &m_room, damage.to, data))
				break;
			damage = data.value<DamageStruct>();

			if (m_room.thread->trigger(DamageCaused, &m_room, damage.from, data))
				break;
			damage = data.value<DamageStruct>();

			if (m_room.thread->trigger(DamageInflicted, &m_room, damage.to, data))
				break;
			damage = data.value<DamageStruct>();

			prevented = false;
			resolution.update(damage.from, damage.to);
			m_room.m_damageStack.push_back(damage);
			m_room.setTag("CurrentDamageStruct", data);

			//m_room.thread->trigger(PreDamageDone, &m_room, damage.to, data);

			if (damage.from){
				m_room.doAnimate(1,damage.from->objectName(),damage.to->objectName());
				m_room.addPlayerMark(damage.from, "damage_point_round", damage.damage);
				if (damage.from != damage.to) m_room.addPlayerMark(damage.from, "damage_point_turn-Clear", damage.damage);
				if (damage.from->getPhase() == Player::Play) m_room.addPlayerMark(damage.from, "damage_point_play_phase", damage.damage);
			}else
				m_room.doAnimate(1,"tablePile",damage.to->objectName());

			m_room.thread->trigger(DamageDone, &m_room, damage.to, data);
			damage = data.value<DamageStruct>();

			if (damage.from && !damage.from->hasFlag("Global_DebutFlag")
                && !(Config.EnableHegemony && damage.from->hasFlag("Global_DFDebut")))
				m_room.thread->trigger(Damage, &m_room, damage.from, data);

			if (!damage.to->hasFlag("Global_DebutFlag")
                && !(Config.EnableHegemony && damage.to->hasFlag("Global_DFDebut")))
				m_room.thread->trigger(Damaged, &m_room, damage.to, data);
		} while (false);

		if(prevented&&damage.to->getTag("TransferDamage").isValid()){
			// Make sure that the trigger in which 'TransferDamage' tag is set returns TRUE
			DamageStruct transfer = damage.to->getTag("TransferDamage").value<DamageStruct>();
			damage.to->removeTag("TransferDamage");
			transfer.transfer = true;
			m_room.damage(transfer);
		}

		damage.prevented = prevented;
		data.setValue(damage);

		m_room.thread->trigger(DamageComplete, &m_room, damage.to, data);
		damageHistory.update({{QStringLiteral("prevented"), prevented}});

		if (!prevented){
			m_room.m_damageStack.pop();
			if (m_room.m_damageStack.isEmpty()) m_room.removeTag("CurrentDamageStruct");
			else m_room.setTag("CurrentDamageStruct", QVariant::fromValue(m_room.m_damageStack.first()));
		}
	} catch (const TriggerCascadeBreak &) {
		const auto restoreDamage = qScopeGuard([&] {
			while (m_room.m_damageStack.size() > previousDepth) m_room.m_damageStack.pop();
			if (previousDamage.isValid()) m_room.setTag("CurrentDamageStruct", previousDamage);
			else m_room.removeTag("CurrentDamageStruct");
			for (auto previous = previousTransfers.cbegin(); previous != previousTransfers.cend(); ++previous) {
				// A consumed transfer is a completed effect; never resurrect it.
				if (!previous.key()->getTag("TransferDamage").isValid()) continue;
				if (previous.value().isValid()) previous.key()->setTag("TransferDamage", previous.value());
				else previous.key()->removeTag("TransferDamage");
			}
		});
		throw;
	} catch (TriggerEvent triggerEvent){
		if (triggerEvent == StageChange || triggerEvent == TurnBroken
			|| triggerEvent == GameFinished){
			m_room.removeTag("CurrentDamageStruct");
			m_room.m_damageStack.clear();
		}
		throw triggerEvent;
	}
}

bool DamageResolutionService::hasWelfare(const ServerPlayer*player) const
{
	if (m_room.mode == "06_3v3")
		return player->isLord() || player->getRole() == "renegade";
	if (m_room.mode == "03_1v2")
		return player->isLord();
	if (Config.EnableHegemony || m_room.mode == "06_XMode" || m_room.mode == "06_ol" || m_room.mode == "05_ol")
		return false;
	if (Sanguosha->isCustomGameMode(m_room.mode)) {
		GameModeStruct gameMode = Sanguosha->getGameMode(m_room.mode);
		return player->isLord() && gameMode.isValid() && gameMode.lord_welfare;
	}
	return player->isLord()&&m_room.player_count > 4;
}
