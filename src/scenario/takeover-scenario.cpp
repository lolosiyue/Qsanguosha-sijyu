#include "takeover-scenario.h"

#include "engine.h"
#include "extra-turn-scheduler.h"
#include "room.h"
#include "room-runtime.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "wrapped-card.h"
#include "crashhandler.h"

#include <QDebug>
#include <QScopeGuard>
#include <QSet>

namespace {

QStringList snapshotSeatOrder(const GlobalSnapshot &state)
{
    if (!state.seatOrder.isEmpty())
        return state.seatOrder;

    QStringList result;
    foreach (const PlayerSnapshot &snapshot, state.players)
        result << snapshot.objectName;
    return result;
}

const PlayerSnapshot *findSnapshotPlayer(const GlobalSnapshot &state,
                                         const QString &objectName)
{
    foreach (const PlayerSnapshot &snapshot, state.players) {
        if (snapshot.objectName == objectName)
            return &snapshot;
    }
    return nullptr;
}

void restoreHistory(Room *room, ServerPlayer *player,
                    const QMap<QString, int> &history)
{
    player->clearHistory();
    foreach (const QString &key, history.keys()) {
        const int count = history.value(key);
        if (count != 0)
            room->addPlayerHistory(player, key, count);
    }
}

void restoreSkills(Room *room, ServerPlayer *player,
                   const QStringList &wantedSkills)
{
    QSet<QString> wanted;
    foreach (const QString &skill, wantedSkills)
        wanted.insert(skill);
    const QList<const Skill *> visible = player->getVisibleSkillList(true);
    foreach (const Skill *skill, visible) {
        if (skill && !wanted.contains(skill->objectName()))
            player->loseSkill(skill->objectName());
    }

    foreach (const QString &skill, wantedSkills) {
        if (skill.isEmpty() || player->hasSkill(skill, true))
            continue;
        // handleAcquireDetachSkills also updates the trigger/skill caches.
        room->handleAcquireDetachSkills(player, QStringList() << ("+" + skill),
                                        true, false, false);
    }
}

QVariant resolvePlayerRefs(const QVariant &value, const TakeoverScenario *scenario);

void restorePlayer(Room *room, TakeoverScenario *scenario, ServerPlayer *player,
                   const PlayerSnapshot &snapshot)
{
    if (!player)
        return;

    // General assignment has already happened in prepareForStart.  Re-apply
    // names here because this is a restore, not a fresh general selection.
    if (!snapshot.general.isEmpty())
        player->setGeneralName(snapshot.general);
    player->setGeneral2Name(snapshot.general2);

    player->setHp(snapshot.hp);
    player->setMaxHp(snapshot.maxhp);
    player->setAlive(snapshot.alive);
    player->setFaceUp(snapshot.faceup);
    player->setChained(snapshot.chained);
    player->setShownRole(snapshot.roleShown);
    player->setGeneralShowed(snapshot.generalShowed);
    player->setGeneral2Showed(snapshot.general2Showed);
    player->setSeat(snapshot.seat);
    player->setPlayerSeat(snapshot.playerSeat);
    bool genderOk = false;
    const int gender = snapshot.gender.toInt(&genderOk);
    if (genderOk && gender >= static_cast<int>(General::Sexless)
        && gender <= static_cast<int>(General::Neuter)) {
        player->setGender(static_cast<General::Gender>(gender));
    }

    if (!snapshot.kingdom.isEmpty())
        room->setPlayerProperty(player, "kingdom", snapshot.kingdom);
    room->setPlayerProperty(player, "role", snapshot.role);

    player->setFlags(QStringLiteral("."));
    for (const QString &flag : snapshot.flags)
        player->setFlags(flag);
    // Skill instances are the authoritative skill state in the new snapshot
    // contract.  Rebuilding them first also preserves amount/state maps that
    // cannot be represented by the legacy visible-skill list.
    player->clearSkillInstances();
    foreach (const SkillInstanceSnapshot &saved, snapshot.skillInstances) {
        if (saved.skillName.isEmpty() || saved.instanceID <= 0)
            continue;
        SkillInstance instance;
        instance.skillName = saved.skillName;
        instance.instanceID = saved.instanceID;
        instance.source = static_cast<SkillInstanceSource>(saved.source);
        instance.parent = SkillInstanceKey(saved.parentSkillName, saved.parentInstanceID);
        ServerPlayer *parentOwner = scenario->runtimePlayer(saved.parentRefOwner);
        instance.parentRef = parentOwner
            ? SkillInstanceRef(parentOwner->objectName(),
                SkillInstanceKey(saved.parentRefSkillName, saved.parentRefInstanceID))
            : SkillInstanceRef();
        instance.visible = saved.visible;
        instance.hasAmountOverride = saved.hasAmountOverride;
        instance.amountOverride = saved.amountOverride;
        instance.bindHead = saved.bindHead;
        instance.correctState = saved.correctState;
        player->upsertSkillInstance(instance);
        player->setSkillInstanceState(instance.skillName, instance.instanceID, saved.state);
    }
    // The visible list is retained as a compatibility guard for innate/helper
    // skills whose runtime instance is intentionally not exposed to clients.
    restoreSkills(room, player, snapshot.skills);

    for (const QByteArray &name : player->dynamicPropertyNames())
        player->setProperty(name.constData(), QVariant());
    foreach (const QString &key, snapshot.dynamicProperties.keys())
        player->setProperty(key.toUtf8().constData(), snapshot.dynamicProperties.value(key));
    player->clearTags();
    foreach (const QString &key, snapshot.tags.keys())
        player->setTag(key, resolvePlayerRefs(snapshot.tags.value(key), scenario));
    restoreHistory(room, player, snapshot.history);

    // Equip-area capacity is part of the gameplay state.  The normal general
    // setup has established the default capacities before GameReady.
    for (int slot = 0; slot < 5; ++slot) {
        const int wanted = snapshot.equipAreas.value(slot, player->getEquipArea(slot));
        player->setEquipAreaCount(slot, wanted);
        player->syncEquipAreaCount(slot);
    }

    foreach (const QString &mark, player->getMarkNames())
        room->setPlayerMark(player, mark, snapshot.marks.value(mark, 0));
    foreach (const QString &mark, snapshot.marks.keys())
        room->setPlayerMark(player, mark, snapshot.marks.value(mark));
}

bool restoreCards(Room *room, TakeoverScenario *scenario)
{
    const GlobalSnapshot state = scenario->snapshot()->getState();

    // Restore the per-room wrappers before placing them.  A WrappedCard is
    // intentionally room-local; copying an engine card directly would lose
    // modified suit/number, skill provenance and card tags.
    foreach (const CardSnapshot &saved, state.cards) {
        if (saved.id < 0)
            return false;
        WrappedCard *wrapped = qobject_cast<WrappedCard *>(room->getCard(saved.id));
        if (!wrapped)
            return false;
        // RoomState already starts from the matching catalog fingerprint.
        // Only a card marked modified can have a different runtime identity.
        if (saved.modified) {
            Card *replacement = nullptr;
            if (!saved.className.isEmpty()) {
                replacement = Sanguosha->cloneCard(
                    saved.className, static_cast<Card::Suit>(saved.suitId), saved.number);
            }
            if (!replacement && !saved.objectName.isEmpty()) {
                replacement = Sanguosha->cloneCard(
                    saved.objectName, static_cast<Card::Suit>(saved.suitId), saved.number);
            }
            if (!replacement)
                return false;
            replacement->setId(saved.id);
            wrapped->copyEverythingFrom(replacement);
        }
        wrapped->setSkillName(saved.skillName);
        wrapped->setSkillInstanceId(saved.skillInstanceId);
        wrapped->setSourceSkill(saved.sourceSkillName, saved.sourceSkillInstanceId);
        wrapped->setActivationSkill(saved.activationSkillName, saved.activationSkillInstanceId);
        wrapped->clearFlags();
        for (const QString &flag : saved.flags)
            wrapped->setFlags(flag);
        for (const QString &mark : wrapped->getMarkNames())
            wrapped->setMark(mark, 0);
        foreach (const QString &key, saved.marks.keys())
            wrapped->setMark(key, saved.marks.value(key));
        const QStringList existingTags = wrapped->tag.keys();
        for (const QString &key : existingTags)
            wrapped->removeTag(key);
        foreach (const QString &key, saved.tags.keys())
            wrapped->setTag(key, saved.tags.value(key));
        wrapped->setModified(saved.modified);
    }

    // GameReady runs before GameRule's initial draw.  The room's only cards
    // are the freshly generated draw pile, so replacing the lists and index
    // is deterministic and does not synthesize movement history.
    room->getDrawPile().clear();
    foreach (int id, state.drawPile)
        room->getDrawPile() << id;
    foreach (int id, state.drawPile)
        room->setCardMapping(id, nullptr, Player::DrawPile);

    // The public API exposes discard as a value, not a mutable reference.  A
    // reverse sequence of moves produces the original top-to-bottom order.
    for (int i = state.discardPile.length() - 1; i >= 0; --i) {
        const int id = state.discardPile.at(i);
        room->setCardMapping(id, nullptr, Player::PlaceTable);
        room->moveCardTo(Sanguosha->getCard(id), nullptr, Player::DiscardPile,
                         CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, QString()));
    }

    const QStringList order = snapshotSeatOrder(state);
    foreach (const QString &seatName, order) {
        const PlayerSnapshot *snapshot = findSnapshotPlayer(state, seatName);
        ServerPlayer *player = scenario->runtimePlayer(seatName);
        if (!snapshot || !player)
            continue;

        // Hand cards are deliberately obtained in serialized order.
        foreach (int id, snapshot->handcards)
            room->obtainCard(player, id, true);
        foreach (int id, snapshot->equips)
            room->moveCardTo(Sanguosha->getCard(id), player, Player::PlaceEquip,
                             CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, QString()));
        foreach (int id, snapshot->judgingArea)
            room->moveCardTo(Sanguosha->getCard(id), player, Player::PlaceDelayedTrick,
                             CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, QString()));
        foreach (const QString &pileName, snapshot->piles.keys())
            player->addToPile(pileName, snapshot->piles.value(pileName), false);
    }

    // The ledger is authoritative for cards in non-linear zones (table and
    // custom places) and for owner identity.  Movement above keeps the public
    // player/pile containers coherent; this final pass restores the exact
    // location index without changing list order.
    foreach (int id, state.cardPlaces.keys()) {
        const Player::Place place = static_cast<Player::Place>(state.cardPlaces.value(id));
        ServerPlayer *owner = scenario->runtimePlayer(state.cardOwners.value(id));
        room->setCardMapping(id, owner, place);
    }
    for (const CardSnapshot &saved : state.cards) {
        Card *card = room->getCard(saved.id);
        ServerPlayer *owner = scenario->runtimePlayer(state.cardOwners.value(saved.id));
        const Player::Place place = static_cast<Player::Place>(state.cardPlaces.value(saved.id));
        if (owner && (place == Player::PlaceHand || place == Player::PlaceSpecial))
            room->notifyUpdateCard(owner, saved.id, card);
        else
            room->broadcastUpdateCard(room->getPlayers(), saved.id, card);
        for (auto mark = saved.marks.constBegin(); mark != saved.marks.constEnd(); ++mark)
            room->setCardMark(saved.id, mark.key(), mark.value(),
                              place == Player::PlaceHand ? owner : nullptr);
    }
    room->doBroadcastNotify(QSanProtocol::S_COMMAND_UPDATE_PILE,
                            room->getDrawPile().length());
    return true;
}

// GameSnapshot 捕捉時把 tag 內的 ServerPlayer* 換成 {"__player": "<objectName>"}
// （見 game-snapshot.cpp 的 normalizePlayerRefs）。呢度按名解返 runtime 指標,
// 解唔到就放一個 null ServerPlayer*, 唔好留低一個技能睇唔明的 map。
QVariant resolvePlayerRefs(const QVariant &value, const TakeoverScenario *scenario)
{
    if (value.userType() == QMetaType::QVariantList) {
        QVariantList list = value.toList();
        for (QVariant &item : list)
            item = resolvePlayerRefs(item, scenario);
        return list;
    }
    if (value.userType() != QMetaType::QVariantMap)
        return value;
    QVariantMap map = value.toMap();
    const QString refKey = QString::fromLatin1(GameSnapshotTags::PlayerRefKey);
    if (map.size() == 1 && map.contains(refKey)) {
        ServerPlayer *player = scenario
            ? scenario->runtimePlayer(map.value(refKey).toString())
            : nullptr;
        return QVariant::fromValue(player);
    }
    for (auto it = map.begin(); it != map.end(); ++it)
        it.value() = resolvePlayerRefs(it.value(), scenario);
    return map;
}

void restoreRoomTags(Room *room, const QVariantMap &tags,
                     const TakeoverScenario *scenario)
{
    const QStringList existingKeys = room->getAllTags().keys();
    for (const QString &key : existingKeys)
        room->removeTag(key);
    foreach (const QString &key, tags.keys())
        room->setTag(key, resolvePlayerRefs(tags.value(key), scenario));
}

bool restoreRng(Room *room, const GlobalSnapshot &state)
{
    auto stateFor = [](const RngSnapshot &saved, GameRng::State *result) {
        if (!result)
            return false;
        bool seedOk = false, drawsOk = false;
        const quint64 seed = saved.seed.toULongLong(&seedOk);
        const quint64 draws = saved.drawCount.toULongLong(&drawsOk);
        if (!seedOk || !drawsOk)
            return false;
        result->seed = seed;
        result->drawCount = draws;
        bool algorithmOk = false;
        const quint32 numeric = saved.algorithm.toUInt(&algorithmOk);
        if (!algorithmOk)
            return false;
        result->algorithm = numeric;
        return result->isValid();
    };

    GameRng::State gameplay;
    if (!stateFor(state.gameplayRng, &gameplay)
        || !room->roomRuntime()->rng().restoreState(gameplay))
        return false;
    GameRng::State ai;
    if (!stateFor(state.aiRng, &ai)
        || !room->roomRuntime()->ai().restoreRngState(ai))
        return false;
    return true;
}

bool restoreExtraTurns(Room *room, TakeoverScenario *scenario,
                       const QVariantList &savedRequests)
{
    QMap<QString, ServerPlayer *> runtimeBySnapshotSeat;
    for (const PlayerSnapshot &player : scenario->snapshot()->getState().players)
        runtimeBySnapshotSeat.insert(player.objectName,
                                     scenario->runtimePlayer(player.objectName));
    QString error;
    if (!room->restorePendingExtraTurns(savedRequests,
                                        runtimeBySnapshotSeat, &error)) {
        qWarning().noquote() << "Takeover pending extra turns rejected:" << error;
        return false;
    }
    return true;
}

} // namespace

TakeoverScenario::TakeoverScenario(const QString &snapshotPath,
                                   const QString &selectedSeat,
                                   QObject *parent)
    : Scenario(QStringLiteral("takeover_scenario")),
      m_snapshotPath(snapshotPath), m_selectedSeat(selectedSeat)
{
    if (parent)
        setParent(parent);

    m_snapshot = new GameSnapshot(snapshotPath, this);
    if (!m_snapshot || !m_snapshot->getError().isEmpty())
        m_loadError = m_snapshot ? m_snapshot->getError()
                                 : QStringLiteral("takeover snapshot could not be loaded");
    else if (m_snapshot->getState().players.isEmpty())
        m_loadError = QStringLiteral("takeover snapshot is empty");
    else if (!m_snapshot->isEligible())
        m_loadError = m_snapshot->getError().isEmpty()
            ? QStringLiteral("takeover snapshot is marked ineligible")
            : m_snapshot->getError();
    else if (!GameSnapshot::validateRuntimeCompatibility(
                 m_snapshot->getState(), &m_loadError)) {
        // m_loadError is populated by the strict runtime fingerprint check.
    } else {
        const PlayerSnapshot *selected = findSnapshotPlayer(
            m_snapshot->getState(), m_selectedSeat);
        if (!selected || !selected->alive)
            m_loadError = QStringLiteral("selected takeover seat is missing or dead");
    }

    rule = new TakeoverRule(this);
}

void TakeoverScenario::assign(QStringList &generals, QStringList &roles) const
{
    if (!isLoaded())
        return;

    const GlobalSnapshot state = m_snapshot->getState();
    const QStringList order = snapshotSeatOrder(state);
    foreach (const QString &seatName, order) {
        const PlayerSnapshot *player = findSnapshotPlayer(state, seatName);
        if (!player)
            continue;
        generals << player->general;
        roles << player->role;
    }
}

int TakeoverScenario::getPlayerCount() const
{
    return isLoaded() ? m_snapshot->getState().players.length() : 0;
}

bool TakeoverScenario::isLoaded() const
{
    return m_snapshot && m_loadError.isEmpty()
        && m_snapshot->isEligible()
        && m_snapshot->getState().unsupportedState.isEmpty()
        && !m_snapshot->getState().players.isEmpty();
}

QString TakeoverScenario::loadError() const
{
    return m_loadError;
}

QString TakeoverScenario::selectedSeat() const
{
    return m_selectedSeat;
}

const GameSnapshot *TakeoverScenario::snapshot() const
{
    return m_snapshot;
}

void TakeoverScenario::bindRuntimePlayers(Room *room)
{
    m_runtimeBySnapshotSeat.clear();
    m_snapshotByRuntime.clear();
    if (!room || !isLoaded())
        return;

    const QStringList order = snapshotSeatOrder(m_snapshot->getState());
    QList<ServerPlayer *> runtime = room->getPlayers();
    if (runtime.length() != order.length())
        return;

    // Signup puts the owner/human first and ADD_ROBOT appends robots.  Move
    // the selected snapshot seat into that connection without renaming it.
    const int selectedIndex = order.indexOf(m_selectedSeat);
    if (selectedIndex < 0)
        return;

    int humanIndex = -1;
    for (int i = 0; i < runtime.length(); ++i) {
        if (runtime.at(i)->isOwner()) {
            if (humanIndex >= 0)
                return;
            humanIndex = i;
        }
    }
    if (humanIndex < 0) {
        for (int i = 0; i < runtime.length(); ++i) {
            if (runtime.at(i)->getState() != QStringLiteral("robot")
                && !runtime.at(i)->isOffline()) {
                if (humanIndex >= 0)
                    return;
                humanIndex = i;
            }
        }
    }
    if (humanIndex < 0)
        return;
    runtime.swapItemsAt(humanIndex, selectedIndex);

    for (int i = 0; i < order.length(); ++i) {
        m_runtimeBySnapshotSeat.insert(order.at(i), runtime.at(i));
        m_snapshotByRuntime.insert(runtime.at(i), order.at(i));
    }
}

ServerPlayer *TakeoverScenario::runtimePlayer(const QString &snapshotObjectName) const
{
    return m_runtimeBySnapshotSeat.value(snapshotObjectName, nullptr);
}

QString TakeoverScenario::snapshotObjectName(const ServerPlayer *runtimePlayer) const
{
    return m_snapshotByRuntime.value(runtimePlayer);
}

TakeoverRule::TakeoverRule(TakeoverScenario *scenario)
    : ScenarioRule(scenario), m_restored(false)
{
    events << GameReady;
}

TakeoverScenario *TakeoverRule::takeoverScenario() const
{
    return qobject_cast<TakeoverScenario *>(parent());
}

bool TakeoverRule::trigger(TriggerEvent triggerEvent, Room *room,
                          ServerPlayer *player, QVariant &) const
{
    if (triggerEvent != GameReady || player)
        return false;
    if (m_restored)
        return true;

    if (!room || !takeoverScenario() || !takeoverScenario()->isLoaded())
        return false;

    m_restored = restore(room);
    if (!m_restored) {
        const QString error = QStringLiteral("takeover snapshot restore failed before TurnStart");
        room->reportTakeoverFailure(error);
        throw GameFinished;
    }
    return true;
}

bool TakeoverRule::restore(Room *room) const
{
    TakeoverScenario *scenario = takeoverScenario();
    if (!scenario)
        return false;

    scenario->bindRuntimePlayers(room);
    const GameSnapshot *snapshot = scenario->snapshot();
    if (!snapshot)
        return false;
    const GlobalSnapshot state = snapshot->getState();

    const QStringList order = snapshotSeatOrder(state);
    if (order.length() != room->getPlayers().length())
        return false;

    room->setRestoringTakeoverSnapshot(true);
    const auto restoreGuard = qScopeGuard([room]() {
        room->setRestoringTakeoverSnapshot(false);
    });
    foreach (const QString &seatName, order) {
        const PlayerSnapshot *playerSnapshot = findSnapshotPlayer(state, seatName);
        if (!playerSnapshot || !scenario->runtimePlayer(seatName))
            return false;
        restorePlayer(room, scenario, scenario->runtimePlayer(seatName), *playerSnapshot);
        // The selected seat is the newly connected human, even when the
        // historical seat was controlled by a robot.  Keep the runtime
        // connection state authoritative for request routing.
        if (seatName == scenario->selectedSeat())
            scenario->runtimePlayer(seatName)->setState(QStringLiteral("online"));
        else
            scenario->runtimePlayer(seatName)->setState(QStringLiteral("robot"));
    }

    room->rebuildAlivePlayers();

    if (!restoreCards(room, scenario)) {
        qWarning() << "Takeover snapshot card state could not be restored";
        return false;
    }
    restoreRoomTags(room, state.roomTags, scenario);

    if (!restoreRng(room, state)) {
        qWarning() << "Takeover snapshot has an invalid RNG state";
        return false;
    }
    if (!room->luaRuntime()->restoreTakeoverState(state.luaTakeoverState)) {
        qWarning() << "Takeover snapshot has an invalid gameplay Lua state";
        return false;
    }
    if (!restoreExtraTurns(room, scenario, state.pendingExtraTurns)) {
        qWarning() << "Takeover snapshot has invalid pending extra turns";
        return false;
    }

    room->setTag(QStringLiteral("TurnLengthCount"), state.turnCount);
    room->setTag(QStringLiteral("Round"), state.roundCount);

    ServerPlayer *current = scenario->runtimePlayer(state.currentPlayer);
    if (!current)
        current = room->getAlivePlayers().isEmpty() ? nullptr : room->getAlivePlayers().first();
    if (current) {
        room->setCurrent(current);
        bool phaseOk = false;
        const int phase = state.currentPhase.toInt(&phaseOk);
        if (phaseOk && phase >= static_cast<int>(Player::RoundStart)
            && phase <= static_cast<int>(Player::PhaseNone))
            current->setPhase(static_cast<Player::Phase>(phase));
    }

    room->updateStateItem();
    if (room->getThread())
        room->getThread()->markDistanceCacheDirty();
    room->syncTakeoverPlayerState();
    CrashHandler::setGameStats(room->getPlayers().length(), state.turnCount);
    room->initializeReplayRecordPath();
    qInfo() << "Takeover snapshot restored:" << scenario->selectedSeat();
    return true;
}
