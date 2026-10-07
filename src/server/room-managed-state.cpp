#include "room-managed-state.h"

#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "card-movement-service.h"
#include "extra-turn-scheduler.h"
#include "game-session-controller.h"
#include "managed-state-lua-bridge.h"
#include "roomthread.h"
#include "wrapped-card.h"
#include "ai-decision-coordinator.h"
#include "basicai.h"
#include "card-lifetime-manager.h"
#include "engine.h"
#include "request-coordinator.h"
#include "skill-runtime-coordinator.h"
#include <QDataStream>
#include <QIODevice>
#include <QPointer>
#include <QThread>
#include <mutex>
#include <vector>

namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
QByteArray encoded(const QVariant &value)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << value;
    return bytes;
}
QVariantMap rngValue(const GameRng::State &rng)
{
    return {{"algorithm", rng.algorithm}, {"seed", QString::number(rng.seed)},
            {"draws", QString::number(rng.drawCount)}};
}
QVariantMap refValue(const SkillInstanceRef &ref)
{
    return {{"owner", ref.ownerObjectName}, {"skill", ref.key.skillName}, {"id", ref.key.instanceID}};
}
QVariantList strings(const QStringList &items) { QVariantList result; for (const auto &s : items) result << s; return result; }
QStringList readStrings(const QVariant &items) { QStringList result; for (const auto &s : items.toList()) result << s.toString(); return result; }
bool exactKeys(const QVariantMap &map, const QStringList &keys)
{
    if (map.size() != keys.size()) return false;
    for (const auto &key : keys) if (!map.contains(key)) return false;
    return true;
}
bool exactReference(const QVariant &value, const QString &kind)
{
    if (value.userType() != QMetaType::QVariantMap) return false;
    const auto outer = value.toMap();
    if (!exactKeys(outer, {"$ref"}) || outer.value("$ref").userType() != QMetaType::QVariantMap) return false;
    const auto ref = outer.value("$ref").toMap();
    return exactKeys(ref, {"kind", "id"}) && ref.value("kind").userType() == QMetaType::QString
        && ref.value("kind").toString() == kind && ref.value("id").userType() == QMetaType::QString
        && !ref.value("id").toString().isEmpty();
}
bool exactStringList(const QVariant &value)
{
    if (value.userType() != QMetaType::QVariantList) return false;
    for (const auto &item : value.toList()) if (item.userType() != QMetaType::QString) return false;
    return true;
}
QVariantList integers(const QList<int> &items) { QVariantList result; for (int i : items) result << i; return result; }
QList<int> readIntegers(const QVariant &items) { QList<int> result; for (const auto &i : items.toList()) result << i.toInt(); return result; }
struct ManagedOwnedCard {
    Card *card = nullptr;
    ~ManagedOwnedCard() {
        // A staged owner holds the new clone before publication and the replaced
        // old clone afterwards. Never touch the live player's lease here.
        if(card) card->deleteLater();
        globalCardLifetimeManager().releaseVariantTags(this);
    }
};
struct ManagedCardFace {
    Card::Suit suit; int number, instance, sourceInstance, activationInstance;
    QString skill, source, activation, show;
    QStringList flags;
    QVariantMap tags, applied;
    bool targetFixed, mute, willThrow, preAction, recast, gift, transferable, damage, single;
    Card::HandlingMethod method;
};
QVariantMap faceWire(const ManagedCardFace &f)
{
    return {{"suit", int(f.suit)}, {"number", f.number}, {"instance", f.instance},
        {"source_instance", f.sourceInstance}, {"activation_instance", f.activationInstance},
        {"skill", f.skill}, {"source", f.source}, {"activation", f.activation}, {"show", f.show},
        {"flags", strings(f.flags)}, {"tags", f.tags}, {"applied", f.applied},
        {"target_fixed", f.targetFixed}, {"mute", f.mute}, {"will_throw", f.willThrow},
        {"pre_action", f.preAction}, {"recast", f.recast}, {"gift", f.gift},
        {"transferable", f.transferable}, {"damage", f.damage}, {"single", f.single}, {"method", int(f.method)}};
}
ManagedCardFace readFace(const QVariantMap &v)
{
    return {Card::Suit(v["suit"].toInt()), v["number"].toInt(), v["instance"].toInt(), v["source_instance"].toInt(),
        v["activation_instance"].toInt(), v["skill"].toString(), v["source"].toString(), v["activation"].toString(),
        v["show"].toString(), readStrings(v["flags"]), v["tags"].toMap(), v["applied"].toMap(),
        v["target_fixed"].toBool(), v["mute"].toBool(), v["will_throw"].toBool(), v["pre_action"].toBool(),
        v["recast"].toBool(), v["gift"].toBool(), v["transferable"].toBool(), v["damage"].toBool(),
        v["single"].toBool(), Card::HandlingMethod(v["method"].toInt())};
}
QVariantMap eventWire(const AIEventView &e)
{
    return {{"sequence", QString::number(e.sequence)}, {"revision", QString::number(e.revision)},
        {"event", e.triggerEvent}, {"kind", e.kind}, {"from", e.from}, {"to", e.to},
        {"targets", strings(e.targets)}, {"name", e.cardName}, {"class", e.cardClass}, {"skill", e.cardSkill},
        {"chain", e.chain}, {"transfer", e.transfer}, {"by_user", e.byUser}, {"suppressed", e.intentionSuppressed},
        {"details", e.details.toVariantMap()}, {"reason", e.reason}, {"cards", integers(e.cardIds)},
        {"private_cards", integers(e.privateCardIds)}, {"private_viewer", e.privateViewer}, {"private", e.privateEvent},
        {"amount", e.amount}, {"nature", e.nature}, {"place", e.place}, {"good", e.good}};
}
AIEventView readEvent(const QVariantMap &v)
{
    AIEventView e; e.sequence=v["sequence"].toString().toULongLong(); e.revision=v["revision"].toString().toULongLong();
    e.triggerEvent=v["event"].toInt(); e.kind=v["kind"].toString(); e.from=v["from"].toString(); e.to=v["to"].toString();
    e.targets=readStrings(v["targets"]); e.cardName=v["name"].toString(); e.cardClass=v["class"].toString(); e.cardSkill=v["skill"].toString();
    e.chain=v["chain"].toBool(); e.transfer=v["transfer"].toBool(); e.byUser=v["by_user"].toBool(); e.intentionSuppressed=v["suppressed"].toBool();
    e.details=QJsonObject::fromVariantMap(v["details"].toMap()); e.reason=v["reason"].toString(); e.cardIds=readIntegers(v["cards"]);
    e.privateCardIds=readIntegers(v["private_cards"]); e.privateViewer=v["private_viewer"].toString(); e.privateEvent=v["private"].toBool();
    e.amount=v["amount"].toInt(); e.nature=v["nature"].toInt(); e.place=v["place"].toInt(); e.good=v["good"].toBool(); return e;
}
}

struct RoomManagedState::NativeSlice {
    struct Seat {
        QPointer<ServerPlayer> player;
        int hp = 0;
        int maxHp = 0;
        Player::Phase phase = Player::PhaseNone;
        QMap<QString, int> marks;
        QVariantMap tags;
        QVariantMap combo;
        QPointer<Card> liveCombo;
        QVariantMap rawTags;
        std::shared_ptr<ManagedOwnedCard> stagedCombo;
        QMap<QString, QMap<int, SkillInstance>> skills;
        bool faceUp = true, chained = false;
        QList<const Card *> hand, judge;
        QList<const WrappedCard *> equips;
        QMap<QString, QList<int>> piles;
        QMap<QString, QStringList> pileOpen;
        QHash<QString, int> history;
        QSet<QString> flags;
        QList<int> shown, broken;
        QList<Player::Phase> phases;
        QList<PhaseStruct> phaseState;
        int phaseIndex = 0;
        QVariantMap descriptionUsage, descriptionValidity;
        QVariantList descriptionEffects;
    };
    struct Physical {
        QPointer<WrappedCard> wrapper;
        QPointer<Card> inner;
        ManagedCardFace outer, face;
        bool modified = false;
    };
    QVector<Seat> seats;
    QVariantMap roomTags;
    QVariantMap rawRoomTags;
    QByteArray topology;
    QByteArray fingerprint;
    quint64 revision = 0;
    QMap<int, Physical> cards;
    QList<int> draw, discard, table;
    QMap<int, Player::Place> places;
    QMap<int, ServerPlayer *> owners;
    ServerPlayer *current = nullptr;
    Player *stateCurrent = nullptr;
    QString pattern;
    CardUseStruct::CardUseReason reason = CardUseStruct::CARD_USE_REASON_UNKNOWN;
    QHash<QString, QStringList> roomFlags;
    QVariantMap nativeHistory;
    bool numericHistory = false;
    QList<AIEventView> aiEvents;
    quint64 aiSequence = 0;
    QHash<QString, QSet<QString>> markViewers;
    GameRng::State gameRng, aiRng;
    int movementId = 0;
    std::unique_ptr<GameRng> preparedGameRng, preparedAiRng;
    std::unique_ptr<ResolutionHistoryService> preparedHistory;
};

struct RoomManagedState::Candidate::Data {
    RoomManagedState *owner = nullptr;
    std::unique_ptr<GameState::WorldStore::Candidate> world;
    std::unique_ptr<NativeSlice> native;
    QByteArray expectedFingerprint;
    quint64 expectedRevision = 0;
};

RoomManagedState::Candidate::Candidate(std::unique_ptr<Data> data) : d(std::move(data)) {}
RoomManagedState::Candidate::~Candidate() = default;
RoomManagedState::Candidate::Candidate(Candidate &&) noexcept = default;
RoomManagedState::Candidate &RoomManagedState::Candidate::operator=(Candidate &&) noexcept = default;
RoomManagedState::RoomManagedState(Room &room) : m_room(room) {}
RoomManagedState::~RoomManagedState() = default;

void RoomManagedState::releaseOwnedCardsForShutdown()
{
    if (!m_running) return;
    auto &manager = globalCardLifetimeManager();
    for (const auto &player : m_enrolledPlayers) {
        if (!player) continue;
        const auto value = player->tag.value("ComboMovesCard");
        if (value.userType() != qMetaTypeId<CardTagOwner>()) continue;
        Card *card = value.value<CardTagOwner>().card;
        bool owned = false;
        {
            std::lock_guard<CardLifetimeManager::ProfiledMutex> lock(manager.m_mutex);
            const auto entry = manager.m_entries.constFind(card);
            const auto tokens = manager.m_variantTags.value(player.data()).value("ComboMovesCard");
            owned = entry != manager.m_entries.cend() && entry->domain == m_room.roomRuntime()
                && tokens.size() == 1 && tokens.first()->address == card;
        }
        // The tag remains alive until the normal root-release stage. Requesting
        // deletion first lets domain draining destroy it after its last lease.
        if (owned) manager.requestNativeDelete(card);
    }
}


QString RoomManagedState::skillId(const QString &playerId, const QString &skillName, int instanceId)
{
    // Length prefixes prevent collisions even when IDs contain ':' or '/'.
    return QString::number(playerId.size()) + ':' + playerId
        + QString::number(skillName.size()) + ':' + skillName + ':' + QString::number(instanceId);
}

bool RoomManagedState::quiescent(QString *error) const
{
    if (!m_running && QThread::currentThread() != m_room.QObject::thread())
        return fail(error, QStringLiteral("managed setup requires the Room QObject owner thread"));
    if ((!m_running && (m_room.isRunning() || m_room.thread
         || m_room.m_gameSession->state() != GameSessionController::State::Waiting))
        || (m_running && (!m_room.m_gameSession->isPlaying() || !m_room.thread || !m_room.thread->isManagedTurnBoundary()))
        || !m_room.thread_3v3.isNull() || !m_room.thread_xmode.isNull()
        || !m_room.thread_1v1.isNull() || !m_room.thread_hegemony.isNull()
        )
        return fail(error, QStringLiteral("managed setup requires an unstarted room with no game workers"));
    auto *runtime = m_room.roomRuntime();
    if (!runtime || runtime->isClosing() || runtime->definitionsLoaded() || runtime->isLoadingDefinitions())
        return fail(error, QStringLiteral("managed setup requires deferred, unloaded rules definitions"));
    auto &game = runtime->lua();
    auto &ai = runtime->ai().lua();
    if (!game.rawState() || !ai.rawState() || game.isClosing() || ai.isClosing()
        || game.invocationDepth() || ai.invocationDepth()
        || !game.isCurrentThreadOwner() || !ai.isCurrentThreadOwner())
        return fail(error, QStringLiteral("managed setup requires two idle initialized owner-thread Lua VMs"));
    if ((!m_running && (!m_room.m_cardMovement->drawPile().isEmpty() || !m_room.m_cardMovement->discardPile().isEmpty()
        || !m_room.m_cardMovement->tableCards().isEmpty() || m_room.m_numericStateHistoryStarted || m_room.current))
        || !m_room.m_damageStack.isEmpty()
        || !m_room.m_pendingDying.isEmpty() || !m_room.m_completedDying.isEmpty() || !m_room.m_pendingResolutionScopes.isEmpty()
        || !m_room.m_extraTurns->pendingRequestsSnapshot().isEmpty() || m_room.m_extraTurns->isCurrentExtraTurn()
        || !m_room.m_externalAgents.isEmpty() || runtime->skillExecutions().size()
        || m_room.m_cardMovement->m_judgementDepth || !m_room.m_cardMovement->m_cascadeReceipts.isEmpty()
        || m_room.resolutionHistory().hasActiveContext())
        return fail(error, QStringLiteral("managed setup excludes cards, active history, requests and continuations"));
    const auto players = m_room.getPlayers();
    if (players.isEmpty()) return fail(error, QStringLiteral("managed setup requires at least one real player"));
    if (!m_enrolledPlayers.isEmpty()) {
        if (players.size() != m_enrolledPlayers.size()) return fail(error, QStringLiteral("enrolled roster changed"));
        for (qsizetype i = 0; i < players.size(); ++i)
            if (!m_enrolledPlayers[i] || m_enrolledPlayers[i].data() != players[i])
                return fail(error, QStringLiteral("enrolled player object was replaced"));
    }
    const auto history = m_room.resolutionHistory().snapshot().serialize();
    const ResolutionHistoryService emptyHistory;
    if (!m_running && history != emptyHistory.snapshot().serialize())
        return fail(error, QStringLiteral("managed setup cannot restore an existing gameplay history"));
    for (const auto *player : players) {
        bool unsupportedDynamic=false;
        for(const auto &name:player->dynamicPropertyNames()) {
            const auto value=player->property(name.constData());
            // Lobby avatar is immutable connection presentation metadata in the
            // explicitly enrolled network lab. It is checked in fixedTopology;
            // it is neither discarded nor an untracked gameplay provider.
            if (m_running && m_room.restrictedRewind() && name == "avatar"
                && value.userType() == QMetaType::QString) continue;
            // GameRule clears this remembered card name at NotActive. The
            // admitted card/skill inventory has no Suijiyingbian consumer;
            // absent and empty are the same audited boundary invariant.
            if(m_running && name=="Suijiyingbian" && value.userType()==QMetaType::QString && value.toString().isEmpty()) continue;
            bool derivedDistance=false;
            if(m_running && value.userType()==QMetaType::Int)
                for(const auto *other:players)
                    if(name==QByteArray("distanceTo_")+other->objectName().toLatin1()) { derivedDistance=true;break; }
            // Server rules compute distance natively. This Qt property is only
            // the outbound/client cache; RoomThread invalidates it after restore.
            if(!derivedDistance) unsupportedDynamic=true;
        }
        if (m_running && (!player->getSmartAI() || typeid(*player->getSmartAI()) != typeid(TrustAI)
            || (player->ai && typeid(*player->ai) != typeid(TrustAI))
            || (player->trust_ai && typeid(*player->trust_ai) != typeid(TrustAI) && typeid(*player->trust_ai) != typeid(BasicAI))))
            return fail(error, QStringLiteral("native AI lacks a state contract (only stateless TrustAI is audited)"));
        if ((!m_running && player->thread() != QThread::currentThread()) || player->m_isWaitingReply
            || (!m_running && player->m_expectedReplyMessageId.load() != 0)
            || (!m_running && (player->getGeneral() || player->getGeneral2()
            || !player->handcards.isEmpty() || !player->equips.isEmpty() || !player->judging_area.isEmpty()
            || !player->piles.isEmpty() || !player->history.isEmpty() || !player->flags.isEmpty()))
            || !player->general_piles.isEmpty() || !player->general_pile_open.isEmpty()
            || !player->description_s2k2v.isEmpty() || !player->card_description_swaps.isEmpty()
            || !player->victims.isEmpty() || !player->selected.isEmpty() || !player->m_pendingAnytimeSkills.isEmpty()
            || unsupportedDynamic
            || !player->fixed_distance.isEmpty() || !player->attack_range_pair.isEmpty()
            || !player->card_limitation.isEmpty() || !player->card_limitation_reasons.isEmpty()
            || player->tag.contains(QStringLiteral("Controller_Name")))
            return fail(error, QStringLiteral("player %1 unsupported state: waiting=%2 generalPiles=%3 generalOpen=%4 desc=%5 cardDesc=%6 victims=%7 selected=%8 anytime=%9 dynamic=%10 distance=%11 attack=%12 limits=%13 limitReasons=%14 controller=%15")
                .arg(player->objectName()).arg(player->m_isWaitingReply).arg(player->general_piles.size()).arg(player->general_pile_open.size())
                .arg(player->description_s2k2v.size()).arg(player->card_description_swaps.size()).arg(player->victims.size()).arg(player->selected.size())
                .arg(player->m_pendingAnytimeSkills.size()).arg(QString::fromUtf8(player->dynamicPropertyNames().join(',')))
                .arg(player->fixed_distance.size()).arg(player->attack_range_pair.size()).arg(player->card_limitation.size())
                .arg(player->card_limitation_reasons.size()).arg(player->tag.contains("Controller_Name")));
    }
    if (m_running && (!m_room.current || m_room.isCurrentExtraTurn() || m_room.m_dyingCursorDepth
        || m_room.m_finishingCascadeDepth || !m_room.m_cancelledHpCauses.isEmpty()
        || !m_room.m_pendingPreshowRequests.isEmpty() || !m_room.m_fillAGarg.isEmpty() || !m_room.m_takeAGargs.isEmpty()))
        return fail(error, QStringLiteral("running checkpoint has an unsupported active continuation/request"));
    if(m_running && (!m_room.m_skillRuntime->m_acceptedViewAsEffects.isEmpty()
        || !m_room.m_skillRuntime->m_changingSkillAmounts.isEmpty()
        || !m_room.m_skillRuntime->m_activeSkillUsageReservations.isEmpty()))
        return fail(error,QStringLiteral("skill execution/reservation has not unwound"));
    if (m_running && m_room.m_requests->hasManagedPendingRequests())
        return fail(error, QStringLiteral("running checkpoint has pending request/reply state"));
    return true;
}

std::unique_ptr<RoomManagedState::NativeSlice> RoomManagedState::captureNative(QString *error) const
{
    if (!quiescent(error)) return {};
        const auto captureFace = [](const Card *c) {
            return ManagedCardFace{c->m_suit,c->m_number,c->m_skillInstanceId,c->m_sourceSkillInstanceId,c->m_activationSkillInstanceId,
                c->m_skillName,c->m_sourceSkillName,c->m_activationSkillName,c->show_skill,c->flags,c->tag,c->m_appliedPhysicalEffectSource,
                c->target_fixed,c->mute,c->will_throw,c->has_preact,c->can_recast,c->is_gift,c->is_transferable,c->damage_card,c->single_target,c->handling_method};
        };
    // Type/reference preflight precedes QDataStream: custom QVariant stream
    // operators must never run merely to discover an unsupported native value.
    GameState::WorldState index;
    for (const auto *player : m_room.getPlayers()) {
        const QString id = player->objectName();
        if (id.isEmpty() || index.players.contains(id)) {
            fail(error, QStringLiteral("native players need unique stable IDs")); return {};
        }
        GameState::PlayerState value; value.id = id; index.players.insert(id, value);
        for (const auto &instances : player->m_skillInstances)
            for (const auto &skill : instances) {
                GameState::SkillState value; value.id = skillId(id, skill.skillName, skill.instanceID);
                index.skills.insert(value.id, value);
            }
    }
    QSet<int> physicalIds;
    if (m_running) {
        const auto add = [&](const QList<int> &ids) { for (int id : ids) physicalIds.insert(id); };
        add(m_room.m_cardMovement->drawPile()); add(m_room.m_cardMovement->discardPile()); add(m_room.m_cardMovement->tableCards());
        for (const auto *p : m_room.getPlayers()) {
            for (const Card *c : p->handcards) physicalIds.insert(c->getEffectiveId());
            for (const Card *c : p->equips) physicalIds.insert(c->getEffectiveId());
            for (const Card *c : p->judging_area) physicalIds.insert(c->getEffectiveId());
            for (const auto &pile : p->piles) add(pile);
        }
        for (auto it = m_enrolledCards.cbegin(); it != m_enrolledCards.cend(); ++it) physicalIds.insert(it.key());
        for (int id : physicalIds) {
            auto *card = m_room.roomRuntime()->state().m_cards.value(id);
            const QStringList auditedClasses = {"Slash", "Jink", "Peach"};
            if (!card || !card->m_card || !auditedClasses.contains(QString::fromLatin1(card->m_card->metaObject()->className()))
                || (!m_enrolledCards.isEmpty() && (!m_enrolledCards.contains(id) || m_enrolledCards.value(id).data() != card))
                || card->subcards!=QList<int>{id} || !card->change_cards.isEmpty()
                || card->m_card->subcards!=QList<int>{id} || !card->m_card->change_cards.isEmpty()) {
                fail(error, QStringLiteral("physical card %1 class=%2 wrapperSubcards=%3 innerSubcards=%4 wrapperChanges=%5 innerChanges=%6 outside audited inventory")
                    .arg(id).arg(card?card->getClassName():QStringLiteral("null"))
                    .arg(card?card->subcards.size():-1).arg(card&&card->m_card?card->m_card->subcards.size():-1)
                    .arg(card?card->change_cards.size():-1).arg(card&&card->m_card?card->m_card->change_cards.size():-1)); return {};
            }
            GameState::CardState value; value.id = QString::number(id); index.cards.insert(value.id, value);
        }
    }
    QSet<const Card *> seenOwnedCards;
    QHash<const Player *, QVariantMap> normalizedTags, ownedCards;
    QHash<const Player *, QPointer<Card>> liveOwnedCards;
    for(const auto *player:m_room.getPlayers()) {
        auto tags=player->tag;
        if(m_running && tags.contains("ComboMovesCard")) {
            const auto value=tags.value("ComboMovesCard");
            if(value.userType()!=qMetaTypeId<CardTagOwner>()) {
                fail(error,QStringLiteral("ComboMovesCard must be an explicitly owned native clone"));return {};
            }
            Card *c=value.value<CardTagOwner>().card;
            const QStringList classes={"Slash","Jink","Peach"};
            if(!c || !globalCardLifetimeManager().isLive(c) || !classes.contains(QString::fromLatin1(c->metaObject()->className()))
                || !physicalIds.contains(c->getId()) || !c->change_cards.isEmpty() || c->subcards!=QList<int>{c->getId()}
                || c->parent() || c->thread()!=QThread::currentThread()) {
                fail(error,QStringLiteral("unsupported ComboMovesCard clone class/reference/ownership"));return {};
            }
            if(seenOwnedCards.contains(c)) { fail(error,QStringLiteral("owned card clone is aliased between players"));return {}; }
            seenOwnedCards.insert(c);
            {
                auto &manager=globalCardLifetimeManager();
                std::lock_guard<CardLifetimeManager::ProfiledMutex> lock(manager.m_mutex);
                const auto entry=manager.m_entries.constFind(c);
                const auto tokens=manager.m_variantTags.value(player).value("ComboMovesCard");
                if(entry==manager.m_entries.cend() || entry->domain!=m_room.roomRuntime() || entry->nativeLeases!=1 || entry->wrappers
                    || entry->pending || entry->adoptionReservations || tokens.size()!=1 || tokens.first()->address!=c) {
                    fail(error,QStringLiteral("owned card has escaped or lacks its exclusive tag lease"));return {};
                }
            }
            const auto *physical=m_room.roomRuntime()->state().m_cards.value(c->getId());
            if(c==physical || c==physical->m_card || c->getClassName()!=physical->getClassName()
                || c->objectName()!=physical->objectName()) {
                fail(error,QStringLiteral("ComboMovesCard is not a matching independent physical-card clone"));return {};
            }
            const QVariantMap wire{{"owned_native_card",QStringLiteral("ComboMovesCard")},{"id",c->getId()},
                {"name",c->objectName()},{"class",c->getClassName()},{"face",faceWire(captureFace(c))}};
            if(!GameState::validateValue(wire,index,error)) return {};
            ownedCards.insert(player,wire);liveOwnedCards.insert(player,c);tags.insert("ComboMovesCard",wire);
        }
        normalizedTags.insert(player,tags);
    }
    auto roomTags=m_room.tag;
    if(m_running && roomTags.contains("HpChangedData") && roomTags.value("HpChangedData").userType()==qMetaTypeId<RecoverStruct>()) {
        const auto recover=roomTags.value("HpChangedData").value<RecoverStruct>();
        if(recover.who && !m_room.getPlayers().contains(recover.who)) {
            fail(error,QStringLiteral("recovery source is not an enrolled player"));return {};
        }
        QString part="none";QVariant card;
        if(recover.card) {
            const int id=recover.card->getId();
            const auto *wrapper=m_room.roomRuntime()->state().m_cards.value(id);
            if(!physicalIds.contains(id) || !wrapper || (recover.card!=wrapper && recover.card!=wrapper->m_card)) {
                fail(error,QStringLiteral("recovery card is not the current enrolled physical wrapper/inner"));return {};
            }
            part=recover.card==wrapper?"wrapper":"inner";card=GameState::reference("card",QString::number(id));
        }
        roomTags.insert("HpChangedData",QVariantMap{{"native_struct",QStringLiteral("RecoverStruct")},
            {"recover",recover.recover},{"reason",recover.reason},
            {"who",recover.who?QVariant(GameState::reference("player",recover.who->objectName())):QVariant()},
            {"card",card},{"card_part",part}});
    }
    if(m_running) for(auto it=roomTags.begin();it!=roomTags.end();++it) {
        if(!it.key().startsWith("UseHistory") || it.value().userType()!=qMetaTypeId<CardUseStruct>()) continue;
        const auto use=it.value().value<CardUseStruct>();
        bool simpleOptions=true;for(const auto &option:use.targetModReveal.options) if(!option.isEmpty()) simpleOptions=false;
        if(!use.m_ownedCard.isNull() || use.m_acceptedSkillEffectCard || use.hasSkillActivationRequest
            || use.sourceRef!=SkillInstanceRef{} || use.activationRef!=SkillInstanceRef{} || use.physicalEquipSource!=PhysicalEquipSource{}
            || use.skillExecutionID || !use.targetModReveal.sources.isEmpty() || !use.targetModReveal.roots.isEmpty()
            || !use.targetModReveal.owner.isEmpty() || !simpleOptions) {
            fail(error,QStringLiteral("UseHistory contains unsupported owned/skill/target-mod continuation"));return {};
        }
        bool valid=true;
        const auto playerRef=[&](const ServerPlayer *p)->QVariant {
            if(!p) return {};if(!m_room.getPlayers().contains(const_cast<ServerPlayer *>(p))) {valid=false;return {};}
            return GameState::reference("player",p->objectName());
        };
        const auto cardRef=[&](const Card *c)->QVariant {
            if(!c) return {};
            const int id=c->getId();const auto *wrapper=m_room.roomRuntime()->state().m_cards.value(id);
            if(!physicalIds.contains(id) || !wrapper || (c!=wrapper && c!=wrapper->m_card)) {valid=false;return {};}
            return QVariantMap{{"ref",GameState::reference("card",QString::number(id))},
                {"part",c==wrapper?QStringLiteral("wrapper"):QStringLiteral("inner")}};
        };
        QVariantList targets;for(const auto *p:use.to) targets<<playerRef(p);
        const QVariantMap wire{{"native_struct",QStringLiteral("CardUseStruct")},{"card",cardRef(use.card)},
            {"from",playerRef(use.from)},{"to",targets},{"whocard",cardRef(use.whocard)},{"who",playerRef(use.who)},
            {"owner_use",use.m_isOwnerUse},{"add_history",use.m_addHistory},{"handcard",use.m_isHandcard},
            {"validate_targets",use.m_validateTargets},{"nullified",strings(use.nullified_list)},
            {"no_respond",strings(use.no_respond_list)},{"no_offset",strings(use.no_offset_list)},
            {"extra_use",use.extra_use},{"bypass_cost",use.bypass_cost},{"skip_effect",use.skipSkillEffect},
            {"finished",use.cardFinished},{"reveal_history",use.targetModReveal.historyKey},
            {"reveal_event",QString::number(use.targetModReveal.useHistoryEventId)},
            {"empty_reveal_options",int(use.targetModReveal.options.size())}};
        if(!valid) {fail(error,QStringLiteral("UseHistory references a foreign/retired native object"));return {};}
        it.value()=wire;
    }
    for(auto it=roomTags.cbegin();it!=roomTags.cend();++it) if(!GameState::validateValue(it.value(),index,error)) {
        if(error) *error=QStringLiteral("room tag %1 [%2]: %3").arg(it.key(),QString::fromLatin1(it.value().metaType().name()),*error);return {};
    }
    for (const auto *player : m_room.getPlayers()) {
        const auto tags=normalizedTags.value(player);
        for(auto it=tags.cbegin();it!=tags.cend();++it) if(!GameState::validateValue(it.value(),index,error)) {
            if(error) *error=QStringLiteral("player %1 tag %2 [%3]: %4").arg(player->objectName(),it.key(),QString::fromLatin1(it.value().metaType().name()),*error);return {};
        }
        for (const auto &instances : player->m_skillInstances)
            for (const auto &skill : instances)
                if (!GameState::validateValue(skill.state, index, error)
                    || !GameState::validateValue(skill.correctState, index, error)) return {};
    }
    for (int id : physicalIds) {
        const auto *wrapper = m_room.roomRuntime()->state().m_cards.value(id);
        for (const Card *card : {static_cast<const Card *>(wrapper), static_cast<const Card *>(wrapper->m_card)})
            if (!GameState::validateValue(card->tag, index, error)
                || !GameState::validateValue(card->m_appliedPhysicalEffectSource, index, error)) return {};
    }
    auto result = std::make_unique<NativeSlice>();
    result->roomTags = roomTags;
    result->revision = m_room.roomRuntime()->stateRevision();
    QVariantList topology, mutableSeats;
    for (auto *player : m_room.getPlayers()) {
        NativeSlice::Seat seat;
        seat.player = player; seat.hp = player->hp; seat.maxHp = player->max_hp;
        seat.phase = player->phase; seat.marks = player->marks; seat.tags = normalizedTags.value(player);
        seat.combo=ownedCards.value(player);seat.liveCombo=liveOwnedCards.value(player);
        seat.skills = player->m_skillInstances;
        if (m_running) {
            seat.faceUp=player->face_up; seat.chained=player->chained;
            seat.hand=player->handcards; seat.equips=player->equips; seat.judge=player->judging_area;
            seat.piles=player->piles; seat.pileOpen=player->pile_open; seat.history=player->history; seat.flags=player->flags;
            seat.shown=player->shown_handcards; seat.broken=player->broken_equips;
            seat.phases=player->phases; seat.phaseState=player->_m_phases_state; seat.phaseIndex=player->_m_phases_index;
            seat.descriptionUsage=player->m_skillDescriptionUsage; seat.descriptionValidity=player->m_skillDescriptionValidity;
            seat.descriptionEffects=player->m_skillDescriptionEffects;
        }
        QVariantList definitions, values;
        for (auto instances = seat.skills.cbegin(); instances != seat.skills.cend(); ++instances) {
            for (auto entry = instances->cbegin(); entry != instances->cend(); ++entry) {
                const auto &skill = entry.value();
                if (instances.key() != skill.skillName || entry.key() != skill.instanceID) {
                    fail(error, QStringLiteral("native skill instance key/value mismatch"));
                    return {};
                }
                definitions << QVariantMap{{"skill", skill.skillName}, {"id", skill.instanceID},
                    {"source", int(skill.source)}, {"parent_skill", skill.parent.skillName},
                    {"parent_id", skill.parent.instanceID}, {"parent_ref", refValue(skill.parentRef)},
                    {"frozen", refValue(skill.frozenSourceRef)}, {"grant", refValue(skill.grantActivationRef)},
                    {"visible", skill.visible}, {"amount_override", skill.hasAmountOverride},
                    {"amount", skill.amountOverride}, {"bind_head", skill.bindHead}};
                values << QVariantMap{{"skill", skill.skillName}, {"id", skill.instanceID},
                                     {"state", skill.state}, {"correct", skill.correctState}};
            }
        }
        QVariantMap marks, nextIds;
        for (auto it = seat.marks.cbegin(); it != seat.marks.cend(); ++it) marks.insert(it.key(), it.value());
        for (auto it = player->m_nextSkillInstanceIds.cbegin(); it != player->m_nextSkillInstanceIds.cend(); ++it)
            nextIds.insert(it.key(), it.value());
        QVariantMap headSkills,deputySkills;
        for(auto it=player->head_skills.cbegin();it!=player->head_skills.cend();++it) headSkills.insert(it.key(),it.value());
        for(auto it=player->deputy_skills.cbegin();it!=player->deputy_skills.cend();++it) deputySkills.insert(it.key(),it.value());
        auto headAcquired=player->head_acquired_skills.values();headAcquired.sort();
        auto deputyAcquired=player->deputy_acquired_skills.values();deputyAcquired.sort();
        auto preshowed=player->getPreshowedSkillInstances().values();preshowed.sort();
        topology << QVariantMap{{"id", player->objectName()}, {"seat", player->seat},
            {"avatar", player->property("avatar")},
            {"player_seat", player->player_seat}, {"alive", player->alive},
            {"face_up", m_running ? false : player->face_up}, {"chained", m_running ? false : player->chained},
            {"role", player->role}, {"kingdom", player->kingdom},
            {"general", player->getGeneralName()}, {"general2", player->getGeneral2Name()},
            {"removed", player->removed}, {"equipment_slots", integers(player->equip_area)},
            {"skill_definitions", definitions}, {"next_skill_ids", nextIds},
            {"skills", player->skills}, {"acquired", player->acquired_skills},
            {"head_shown",player->general_showed},{"deputy_shown",player->general2_showed},
            {"actual_head",player->actual_general1?player->actual_general1->objectName():QString()},
            {"actual_deputy",player->actual_general2?player->actual_general2->objectName():QString()},
            {"gender",int(player->m_gender)},{"role_shown",player->role_shown},
            {"weapon_area",player->weapon_area},{"armor_area",player->armor_area},
            {"defensive_area",player->defensive_horse_area},{"offensive_area",player->offensive_horse_area},
            {"treasure_area",player->treasure_area},{"judge_area",player->hasjudgearea},
            {"disabled_show",strings(player->disable_show)},
            {"next",player->next?player->next->objectName():QString()},
            {"head_skills",headSkills},
            {"deputy_skills",deputySkills},
            {"head_acquired",strings(headAcquired)},
            {"deputy_acquired",strings(deputyAcquired)},
            {"preshowed",strings(preshowed)}};
        mutableSeats << QVariantMap{{"id", player->objectName()}, {"hp", seat.hp}, {"maxhp", seat.maxHp},
            {"phase", int(seat.phase)}, {"marks", marks}, {"tags", seat.tags}, {"skill_values", values}};
        result->seats.append(std::move(seat));
    }
    QVariantMap topologyValue{{"players", topology}};
    if (m_running) {
        QVariantMap inactiveDefinitions;
        const auto &catalogue=m_room.roomRuntime()->state().m_cards;
        if(!m_inactiveCards.isEmpty() && catalogue.size()!=physicalIds.size()+m_inactiveCards.size()) {
            fail(error,QStringLiteral("physical catalogue inventory changed"));return {};
        }
        for(auto it=catalogue.cbegin();it!=catalogue.cend();++it) if(!physicalIds.contains(it.key())) {
            const auto *c=it.value();
            if(!c || !c->m_card || (!m_inactiveCards.isEmpty() && m_inactiveCards.value(it.key()).data()!=c)) {
                fail(error,QStringLiteral("inactive card identity changed"));return {};
            }
            const QVariantMap data{{"name",c->objectName()},{"class",c->getClassName()},{"inner_name",c->m_card->objectName()},
                {"inner_class",c->m_card->getClassName()},{"inner_id",c->m_card->getId()},
                {"outer",faceWire(captureFace(c))},{"inner",faceWire(captureFace(c->m_card))},
                {"modified",c->m_isModified},{"subcards",integers(c->subcards)},{"inner_subcards",integers(c->m_card->subcards)}};
            if(!GameState::validateValue(data,index,error)) return {};
            if(!c->change_cards.isEmpty() || !c->m_card->change_cards.isEmpty()) {
                fail(error,QStringLiteral("inactive card has unmanaged change-card references"));return {};
            }
            inactiveDefinitions.insert(QString::number(it.key()),data);
        }
        topologyValue.insert("inactive_cards",inactiveDefinitions);
        QVariantMap cardDefinitions;
        for (int id : physicalIds) {
            auto *c = m_room.roomRuntime()->state().m_cards.value(id);
            result->cards.insert(id, {c,c->m_card,captureFace(c),captureFace(c->m_card),c->m_isModified});
            cardDefinitions.insert(QString::number(id), QVariantMap{{"name",c->objectName()}, {"class",c->getClassName()},
                {"wrapper_id",c->getId()},{"inner_name",c->m_card->objectName()},{"inner_class",c->m_card->getClassName()},{"inner_id",c->m_card->getId()}});
        }
        topologyValue.insert("cards",cardDefinitions);
        auto &movement=*m_room.m_cardMovement;
        result->draw=movement.drawPile(); result->discard=movement.discardPile(); result->table=movement.tableCards();
        result->places=movement.m_locations.m_places; result->owners=movement.m_locations.m_owners;
        result->current=m_room.current; auto &state=m_room.roomRuntime()->state();
        result->stateCurrent=state.m_currentPlayer;
        QSet<const Player *> playerObjects;for(const auto &seat:result->seats) playerObjects.insert(seat.player.data());
        if((result->stateCurrent && !playerObjects.contains(result->stateCurrent)) || !playerObjects.contains(result->current)) {
            fail(error,QStringLiteral("cross-world native current-player reference"));return {};
        }
        for(auto *owner:result->owners) if(owner && !playerObjects.contains(owner)) {
            fail(error,QStringLiteral("cross-world native card owner reference"));return {};
        }
        result->pattern=state.m_currentCardUsePattern;
        result->reason=state.m_currentCardUseReason; result->roomFlags=state.m_flags;
        result->nativeHistory=m_room.resolutionHistory().snapshot().serialize();
        if(!result->nativeHistory.value("complete").toBool()) { fail(error,QStringLiteral("native resolution history is incomplete"));return {}; }
        result->numericHistory=m_room.m_numericStateHistoryStarted;
        result->gameRng=m_room.roomRuntime()->rng().state(); result->aiRng=m_room.roomRuntime()->ai().rngState();
        result->aiEvents=m_room.m_aiDecisions->m_events; result->aiSequence=m_room.m_aiDecisions->m_eventSequence;
        result->markViewers=m_room.m_aiDecisions->m_markViewers; result->movementId=m_room._m_lastMovementId;
        const auto wire=runningWire(*result);
        for(auto it=wire.cbegin();it!=wire.cend();++it) if(!GameState::validateValue(it.value(),index,error)) {
            if(error) *error=QStringLiteral("native %1 [%2]: %3").arg(it.key(),QString::fromLatin1(it.value().metaType().name()),*error);return {};
        }
    } else {
        topologyValue.insert("game_rng",rngValue(m_room.roomRuntime()->rng().state()));
        topologyValue.insert("ai_rng",rngValue(m_room.roomRuntime()->ai().rngState()));
        topologyValue.insert("history",m_room.resolutionHistory().snapshot().serialize());
    }
    result->topology=encoded(topologyValue);
    result->fingerprint=nativeFingerprint(*result);
    if (!m_fixedTopology.isEmpty() && result->topology != m_fixedTopology) {
        fail(error, QStringLiteral("native topology/RNG/history changed outside the managed setup slice"));
        return {};
    }
    return result;
}

QByteArray RoomManagedState::nativeFingerprint(const NativeSlice &native) const
{
    QVariantList seats;
    for (const auto &seat : native.seats) {
        QVariantMap marks;
        for (auto it = seat.marks.cbegin(); it != seat.marks.cend(); ++it) marks.insert(it.key(), it.value());
        QVariantList values;
        for (const auto &instances : seat.skills)
            for (const auto &skill : instances)
                values << QVariantMap{{"skill", skill.skillName}, {"id", skill.instanceID},
                                     {"state", skill.state}, {"correct", skill.correctState}};
        seats << QVariantMap{{"id", seat.player->objectName()}, {"hp", seat.hp}, {"maxhp", seat.maxHp},
            {"phase", int(seat.phase)}, {"marks", marks}, {"tags", seat.tags}, {"skill_values", values}};
    }
    QVariantMap value{{"topology", native.topology}, {"players", seats}, {"room_tags", native.roomTags}};
    if (m_running) value.insert("native",runningWire(native));
    return encoded(value);
}

QVariantMap RoomManagedState::runningWire(const NativeSlice &native) const
{
    QVariantList seats, events;
    for (const auto &s : native.seats) {
        QVariantList hand,equips,judge,phases,phaseState;
        for (auto *c:s.hand) hand<<c->getEffectiveId(); for(auto *c:s.equips) equips<<c->getEffectiveId(); for(auto *c:s.judge) judge<<c->getEffectiveId();
        for(auto p:s.phases) phases<<int(p); for(const auto &p:s.phaseState) phaseState<<QVariantMap{{"phase",int(p.phase)},{"skipped",p.skipped}};
        QVariantMap piles,open,history;
        for(auto it=s.piles.cbegin();it!=s.piles.cend();++it) piles.insert(it.key(),integers(it.value()));
        for(auto it=s.pileOpen.cbegin();it!=s.pileOpen.cend();++it) open.insert(it.key(),strings(it.value()));
        for(auto it=s.history.cbegin();it!=s.history.cend();++it) history.insert(it.key(),it.value());
        QStringList flags=s.flags.values(); flags.sort();
        seats<<QVariantMap{{"id",s.player->objectName()},{"face_up",s.faceUp},{"chained",s.chained},
            {"hand",hand},{"equips",equips},{"judge",judge},{"piles",piles},{"pile_open",open},{"history",history},
            {"flags",strings(flags)},{"shown",integers(s.shown)},{"broken",integers(s.broken)},
            {"phases",phases},{"phase_state",phaseState},{"phase_index",s.phaseIndex},
            {"owned_combo",s.combo},{"description_usage",s.descriptionUsage},{"description_validity",s.descriptionValidity},{"description_effects",s.descriptionEffects}};
    }
    QVariantMap cards,places,owners,roomFlags,viewers;
    for(auto it=native.cards.cbegin();it!=native.cards.cend();++it)
        cards.insert(QString::number(it.key()),QVariantMap{{"outer",faceWire(it->outer)},{"inner",faceWire(it->face)},{"modified",it->modified}});
    for(auto it=native.places.cbegin();it!=native.places.cend();++it) places.insert(QString::number(it.key()),int(it.value()));
    for(auto it=native.owners.cbegin();it!=native.owners.cend();++it) owners.insert(QString::number(it.key()),it.value()?it.value()->objectName():QString());
    for(auto it=native.roomFlags.cbegin();it!=native.roomFlags.cend();++it) roomFlags.insert(it.key(),strings(it.value()));
    for(auto it=native.markViewers.cbegin();it!=native.markViewers.cend();++it) { QStringList ids=it.value().values();ids.sort();viewers.insert(it.key(),strings(ids)); }
    for(const auto &e:native.aiEvents) events<<eventWire(e);
    return {{"version",1},{"seats",seats},{"cards",cards},{"draw",integers(native.draw)},
        {"discard",integers(native.discard)},{"table",integers(native.table)},{"places",places},{"owners",owners},
        {"current",native.current?native.current->objectName():QString()},
        {"state_current",native.stateCurrent?native.stateCurrent->objectName():QString()},
        {"pattern",native.pattern},{"reason",int(native.reason)},{"room_flags",roomFlags},
        {"history",native.nativeHistory},{"numeric_history",native.numericHistory},
        {"game_rng",rngValue(native.gameRng)},{"ai_rng",rngValue(native.aiRng)},
        {"ai_events",events},{"ai_sequence",QString::number(native.aiSequence)},
        {"mark_viewers",viewers},{"movement_id",native.movementId}};
}

bool RoomManagedState::restoreRunningWire(NativeSlice &native,const QVariantMap &wire,QString *error) const
{
    if(wire.value("version").toInt()!=1 || wire.value("seats").toList().size()!=native.seats.size())
        return fail(error,QStringLiteral("unsupported native world schema/inventory"));
    QMap<QString,ServerPlayer*> players;
    for(const auto &s:native.seats) players.insert(s.player->objectName(),s.player.data());
    bool valid=true;
    const auto playerFor=[&](const QString &id)->ServerPlayer* { if(id.isEmpty()) return nullptr; if(!players.contains(id)) valid=false; return players.value(id); };
    const auto cardFor=[&](int id)->WrappedCard* { if(!native.cards.contains(id)) valid=false; return native.cards.value(id).wrapper.data(); };
    const auto savedSeats=wire.value("seats").toList();
    for(qsizetype i=0;i<native.seats.size();++i) {
        auto &s=native.seats[i];const auto v=savedSeats[i].toMap();
        if(v.value("id").toString()!=s.player->objectName()) return fail(error,QStringLiteral("native seat order changed"));
        s.faceUp=v["face_up"].toBool();s.chained=v["chained"].toBool();s.hand.clear();s.equips.clear();s.judge.clear();
        for(int id:readIntegers(v["hand"])) s.hand<<cardFor(id);
        for(int id:readIntegers(v["equips"])) s.equips<<cardFor(id);
        for(int id:readIntegers(v["judge"])) s.judge<<cardFor(id);
        s.piles.clear();const auto piles=v["piles"].toMap();for(auto it=piles.cbegin();it!=piles.cend();++it) s.piles.insert(it.key(),readIntegers(it.value()));
        s.pileOpen.clear();const auto open=v["pile_open"].toMap();for(auto it=open.cbegin();it!=open.cend();++it) s.pileOpen.insert(it.key(),readStrings(it.value()));
        s.history.clear();const auto history=v["history"].toMap();for(auto it=history.cbegin();it!=history.cend();++it) s.history.insert(it.key(),it.value().toInt());
        const auto flags=readStrings(v["flags"]);s.flags=QSet<QString>(flags.begin(),flags.end());
        s.shown=readIntegers(v["shown"]);s.broken=readIntegers(v["broken"]);s.phases.clear();s.phaseState.clear();
        for(int phase:readIntegers(v["phases"])) s.phases<<Player::Phase(phase);
        for(const auto &entry:v["phase_state"].toList()) { const auto p=entry.toMap();PhaseStruct phase;phase.phase=Player::Phase(p["phase"].toInt());phase.skipped=p["skipped"].toInt();s.phaseState<<phase; }
        s.combo=v["owned_combo"].toMap();
        s.phaseIndex=v["phase_index"].toInt();s.descriptionUsage=v["description_usage"].toMap();
        s.descriptionValidity=v["description_validity"].toMap();s.descriptionEffects=v["description_effects"].toList();
    }
    const auto cards=wire.value("cards").toMap();
    if(cards.size()!=native.cards.size()) return fail(error,QStringLiteral("native card inventory changed"));
    for(auto it=native.cards.begin();it!=native.cards.end();++it) {
        if(!cards.contains(QString::number(it.key()))) return fail(error,QStringLiteral("missing native physical card"));
        const auto c=cards.value(QString::number(it.key())).toMap();it->outer=readFace(c["outer"].toMap());it->face=readFace(c["inner"].toMap());it->modified=c["modified"].toBool();
    }
    native.draw=readIntegers(wire["draw"]);native.discard=readIntegers(wire["discard"]);native.table=readIntegers(wire["table"]);
    native.places.clear();const auto places=wire["places"].toMap();for(auto it=places.cbegin();it!=places.cend();++it) native.places.insert(it.key().toInt(),Player::Place(it.value().toInt()));
    native.owners.clear();const auto owners=wire["owners"].toMap();for(auto it=owners.cbegin();it!=owners.cend();++it) native.owners.insert(it.key().toInt(),playerFor(it.value().toString()));
    native.current=playerFor(wire["current"].toString());native.stateCurrent=playerFor(wire["state_current"].toString());
    native.pattern=wire["pattern"].toString();native.reason=CardUseStruct::CardUseReason(wire["reason"].toInt());native.roomFlags.clear();
    const auto flags=wire["room_flags"].toMap();for(auto it=flags.cbegin();it!=flags.cend();++it) native.roomFlags.insert(it.key(),readStrings(it.value()));
    native.nativeHistory=wire["history"].toMap();native.numericHistory=wire["numeric_history"].toBool();
    const auto readRng=[](const QVariant &v) {const auto map=v.toMap();return GameRng::State{map["seed"].toString().toULongLong(),map["draws"].toString().toULongLong(),map["algorithm"].toUInt()};};
    native.gameRng=readRng(wire["game_rng"]);native.aiRng=readRng(wire["ai_rng"]);
    native.aiEvents.clear();for(const auto &e:wire["ai_events"].toList()) native.aiEvents<<readEvent(e.toMap());
    native.aiSequence=wire["ai_sequence"].toString().toULongLong();native.markViewers.clear();
    const auto viewers=wire["mark_viewers"].toMap();for(auto it=viewers.cbegin();it!=viewers.cend();++it) {const auto ids=readStrings(it.value());native.markViewers.insert(it.key(),QSet<QString>(ids.begin(),ids.end()));}
    native.movementId=wire["movement_id"].toInt();
    if(!valid) return fail(error,QStringLiteral("unresolved native player/card reference"));
    native.preparedGameRng=std::make_unique<GameRng>();native.preparedAiRng=std::make_unique<GameRng>();
    if(!native.preparedGameRng->restore(native.gameRng,error)||!native.preparedAiRng->restore(native.aiRng,error)) return false;
    ResolutionHistorySnapshot history;
    if(!ResolutionHistorySnapshot::deserialize(native.nativeHistory,&history,error)) return false;
    native.preparedHistory=std::make_unique<ResolutionHistoryService>();
    return native.preparedHistory->restore(history,error);
}

bool RoomManagedState::project(const NativeSlice &native, GameState::WorldState &world, QString *error) const
{
    world.players.clear(); world.seatOrder.clear(); world.skills.clear(); world.cards.clear();
    world.zones = {{"draw", GameState::ZoneKind::Draw, {}, {}, {}},
        {"discard", GameState::ZoneKind::Discard, {}, {}, {}},
        {"table", GameState::ZoneKind::Table, {}, {}, {}}, {"void", GameState::ZoneKind::Void, {}, {}, {}}};
    for (const auto &seat : native.seats) {
        const auto *player = seat.player.data();
        if (!player || player->objectName().isEmpty() || world.players.contains(player->objectName()))
            return fail(error, QStringLiteral("native players need unique stable IDs"));
        GameState::PlayerState value;
        value.id = player->objectName(); value.hp = seat.hp; value.maxHp = seat.maxHp;
        value.armor = seat.marks.value(QStringLiteral("@HuJia")); value.marks = seat.marks;
        value.tags = seat.tags; value.alive = player->alive; value.faceUp = m_running ? seat.faceUp : player->face_up; value.chained = m_running ? seat.chained : player->chained;
        if (m_running) for (auto it=seat.history.cbegin();it!=seat.history.cend();++it) value.usageHistory.insert(it.key(),it.value());
        value.properties.insert(QStringLiteral("native_phase"), int(seat.phase));
        world.players.insert(value.id, value); world.seatOrder << value.id;
        for (auto kind : {GameState::ZoneKind::Hand, GameState::ZoneKind::Equip, GameState::ZoneKind::Judge})
            world.zones.append({QString::number(int(kind)) + ':' + value.id, kind, value.id, {}, {}});
        for (const auto &instances : seat.skills) {
            for (const auto &skill : instances) {
                GameState::SkillState managed;
                managed.id = skillId(value.id, skill.skillName, skill.instanceID);
                managed.definition = skill.skillName; managed.ownerId = value.id;
                if (skill.parentRef.isValid())
                    managed.parentId = skillId(skill.parentRef.ownerObjectName, skill.parentRef.key.skillName, skill.parentRef.key.instanceID);
                else if (skill.parent.instanceID > 0)
                    managed.parentId = skillId(value.id, skill.parent.skillName, skill.parent.instanceID);
                for (const auto &provider : m_registry.providers())
                    if (provider.skillDefinitions.contains(skill.skillName)) managed.providerId = provider.id;
                if (managed.providerId.isEmpty())
                    return fail(error, QStringLiteral("native skill has no audited managed provider: %1").arg(skill.skillName));
                managed.state = skill.state; managed.correctState = skill.correctState;
                world.skills.insert(managed.id, managed);
            }
        }
    }
    if (m_running) {
        for(auto it=native.cards.cbegin();it!=native.cards.cend();++it) {
            const QString id=QString::number(it.key());
            world.cards.insert(id,{id,it->wrapper->objectName(),int(it->outer.suit),it->outer.number,
                {{"outer",faceWire(it->outer)},{"inner",faceWire(it->face)},{"modified",it->modified}}});
        }
        const auto ids=[](const QList<int> &list){QStringList out;for(int id:list) out<<QString::number(id);return out;};
        world.zones[0].cards=ids(native.draw);world.zones[1].cards=ids(native.discard);world.zones[2].cards=ids(native.table);
        for(qsizetype i=0;i<native.seats.size();++i) {
            const auto &seat=native.seats[i];
            for(const Card *c:seat.hand) world.zones[4+i*3].cards<<QString::number(c->getEffectiveId());
            for(const Card *c:seat.equips) world.zones[5+i*3].cards<<QString::number(c->getEffectiveId());
            for(const Card *c:seat.judge) world.zones[6+i*3].cards<<QString::number(c->getEffectiveId());
            for(auto it=seat.piles.cbegin();it!=seat.piles.cend();++it)
                world.zones.append({QStringLiteral("private:%1:%2:%3").arg(seat.player->objectName().size()).arg(seat.player->objectName(),it.key()),
                    GameState::ZoneKind::Private,seat.player->objectName(),it.key(),ids(it.value())});
        }
        QSet<QString> placed;
        for(const auto &zone:world.zones) for(const auto &id:zone.cards) {
            placed.insert(id);
            Player::Place place=Player::PlaceUnknown;
            switch(zone.kind) {
            case GameState::ZoneKind::Draw:place=Player::DrawPile;break;
            case GameState::ZoneKind::Discard:place=Player::DiscardPile;break;
            case GameState::ZoneKind::Table:place=Player::PlaceTable;break;
            case GameState::ZoneKind::Hand:place=Player::PlaceHand;break;
            case GameState::ZoneKind::Equip:place=Player::PlaceEquip;break;
            case GameState::ZoneKind::Judge:place=Player::PlaceDelayedTrick;break;
            case GameState::ZoneKind::Private:place=Player::PlaceSpecial;break;
            case GameState::ZoneKind::Void:break;
            }
            const auto *owner=native.owners.value(id.toInt());
            if(native.places.value(id.toInt(),Player::PlaceUnknown)!=place
                || (owner?owner->objectName():QString())!=zone.ownerId)
                return fail(error,QStringLiteral("native card zone/location index disagree: %1").arg(id));
        }
        for(const auto &id:world.cards.keys()) if(!placed.contains(id)) {
            if(native.places.value(id.toInt(),Player::PlaceUnknown)!=Player::PlaceUnknown || native.owners.value(id.toInt()))
                return fail(error,QStringLiteral("unplaced native card has an owner/location"));
            world.zones[3].cards<<id;
        }
        world.roomTags=native.roomTags;world.turn.playerId=native.current->objectName();world.turn.phase="RoundStart";
        world.gameplayRng=native.gameRng;world.aiRng=native.aiRng;
        world.history={};
        world.history.nextEventId=native.nativeHistory.value("next_id").toString().toULongLong();
        for(const auto &v:native.nativeHistory.value("events").toList()) {
            const auto e=v.toMap();QString parent=e.value("parent_id").toString();if(parent=="0") parent.clear();
            world.history.events.append({e.value("id").toString(),parent,e.value("kind").toString(),e.value("data").toMap()});
        }
        for(const auto &v:native.nativeHistory.value("active").toList()) world.history.activeEventIds<<v.toString();
        world.providers.insert("engine.native",{1,runningWire(native)});
        return GameState::validateWorld(world,m_registry,error);
    }
    world.roomTags = native.roomTags;
    world.turn.playerId = world.seatOrder.first();
    world.turn.phase = QStringLiteral("PhaseNone");
    world.turn.roundScopeId = QStringLiteral("setup:none");
    world.gameplayRng = m_room.roomRuntime()->rng().state();
    world.aiRng = m_room.roomRuntime()->ai().rngState();
    return GameState::validateWorld(world, m_registry, error);
}

bool RoomManagedState::initialize(const GameState::ProviderRegistry &registry, QString *error)
{
    if (m_store) return fail(error, QStringLiteral("managed setup is already initialized"));
    if (!registry.packages().isEmpty())
        return fail(error, QStringLiteral("managed setup cannot enroll native packages"));
    return initializeImpl(registry,error);
}

bool RoomManagedState::initializeRunning(const GameState::ProviderRegistry &registry,QString *error)
{
    if(m_store || registry.packages().isEmpty()) return fail(error,QStringLiteral("running enrollment requires explicit native package audit"));
    m_running=true;
    if(initializeImpl(registry,error)) return true;
    m_running=false;return false;
}

bool RoomManagedState::initializeImpl(const GameState::ProviderRegistry &registry,QString *error)
{
    if (!quiescent(error)) return false;
    LuaRuntime::Binding gameLock(m_room.roomRuntime()->lua(), false);
    LuaRuntime::Binding aiLock(m_room.roomRuntime()->ai().lua(), false);
    m_registry = registry;
    if(m_running) {
        GameState::ProviderContract contract;contract.id="engine.native";contract.version=1;
        contract.audit="RoomManagedState fixed native inventory at outer normal-turn boundary";
        if(!m_registry.registerProvider(contract,error)) return false;
    }
    auto native = captureNative(error);
    if (!native) return false;
    const auto timeline = m_room.m_gameTimeline;
    if (!timeline) return fail(error, QStringLiteral("Room has no authoritative game timeline"));
    GameState::WorldState world;
    world.rootGameId = timeline->rootGameId(); world.worldId = timeline->worldId();
    world.completeDomains = GameState::requiredDomains(); world.turn.turnScopeId = m_running ? "turn:initial" : "setup:initial";
    world.turn.roundScopeId=m_running ? "round:initial" : "setup:none";world.nativePackages=m_registry.packages();
    for (const auto &provider : m_registry.providers())
        world.providers.insert(provider.id, {provider.version, {{"owners", QVariantMap()}}});
    if (!project(*native, world, error)) return false;
    auto store = GameState::WorldStore::create(world, m_registry, error, timeline);
    if (!store) return false;
    // Provider construction is fallible; it may not silently alter native fields.
    if (!buildNativeCandidate(*native, store->state(), error)) return false;
    if (nativeFingerprint(*native) != native->fingerprint)
        return fail(error, QStringLiteral("initial provider construction changed native state"));
    auto after = captureNative(error);
    if (!after || after->fingerprint != native->fingerprint || after->revision != native->revision)
        return fail(error, QStringLiteral("native room changed while initializing managed providers"));
    m_fixedTopology = native->topology;
    for (const auto &seat : native->seats) m_enrolledPlayers.append(seat.player);
    for(auto it=native->cards.cbegin();it!=native->cards.cend();++it) m_enrolledCards.insert(it.key(),it->wrapper.data());
    if(m_running) {
        const auto &cards=m_room.roomRuntime()->state().m_cards;
        for(auto it=cards.cbegin();it!=cards.cend();++it)
            if(!m_enrolledCards.contains(it.key())) m_inactiveCards.insert(it.key(),it.value());
    }
    m_store = std::move(store);
    return true;
}

bool RoomManagedState::installLuaProvider(LuaRuntime &runtime, const QString &providerId, QString *error)
{
    if (!m_store || !quiescent(error)) return false;
    if(providerId=="engine.native") return fail(error,QStringLiteral("reserved native provider cannot be installed in Lua"));
    if (&runtime != &m_room.roomRuntime()->lua() && &runtime != &m_room.roomRuntime()->ai().lua())
        return fail(error, QStringLiteral("provider bridge VM does not belong to this Room"));
    return ManagedStateLuaBridge::install(runtime, *m_store, providerId, error);
}

QString RoomManagedState::checkpoint(const QString &name, QString *error)
{
    if(m_running) { fail(error,QStringLiteral("running rooms require a real turn boundary checkpoint"));return {}; }
    if (!m_store || !quiescent(error)) return {};
    LuaRuntime::Binding gameLock(m_room.roomRuntime()->lua(), false);
    LuaRuntime::Binding aiLock(m_room.roomRuntime()->ai().lua(), false);
    auto native = captureNative(error);
    if (!native) return {};
    const QString scope = QStringLiteral("setup:") + name;
    if (name.isEmpty()) { fail(error, QStringLiteral("setup checkpoint needs a name")); return {}; }
    for (const auto &anchor : m_store->timeline().anchors())
        if (anchor.kind == GameTimeline::AnchorKind::ManagedSetup && anchor.scopeId == scope) {
            fail(error, QStringLiteral("duplicate active managed setup checkpoint"));
            return {};
        }
    return m_store->checkpointUpdate([&](GameState::WorldState &world, QString *why) {
        world.turn.turnScopeId = scope;
        return project(*native, world, why);
    }, GameTimeline::AnchorKind::ManagedSetup, scope, {}, error).id;
}

bool RoomManagedState::checkpointTurn(const QString &turnScope,const QString &roundScope,bool beginsRound,QString *error)
{
    if(!m_running || !m_store || !quiescent(error)) return false;
    LuaRuntime::Binding gameLock(m_room.roomRuntime()->lua(),false);
    LuaRuntime::Binding aiLock(m_room.roomRuntime()->ai().lua(),false);
    auto native=captureNative(error);if(!native) return false;
    const auto import=[&](GameState::WorldState &world,QString *why) {
        world.turn.turnScopeId=turnScope;world.turn.roundScopeId=roundScope;
        return project(*native,world,why);
    };
    return !m_store->checkpointUpdate(import,GameTimeline::AnchorKind::PlayerTurn,turnScope,
        native->current->objectName(),error,beginsRound).id.isEmpty();
}

bool RoomManagedState::buildNativeCandidate(NativeSlice &native, const GameState::WorldState &world, QString *error) const
{
    if(m_running && !restoreRunningWire(native,world.providers.value("engine.native").state,error)) return false;
    GameState::WorldState expected = world;
    if (!project(native, expected, error)) return false;
    if (world.players.keys() != expected.players.keys() || world.skills.keys() != expected.skills.keys()
        || world.seatOrder != expected.seatOrder || world.cards.keys()!=expected.cards.keys()
        || rngValue(world.gameplayRng) != rngValue(expected.gameplayRng)
        || rngValue(world.aiRng) != rngValue(expected.aiRng))
        return fail(error, QStringLiteral("candidate changed unsupported native inventory/RNG"));
    if (!m_running && (world.zones.size() != expected.zones.size() || !world.history.events.isEmpty()
        || !world.history.activeEventIds.isEmpty() || world.history.nextEventId != 1
        || world.turn.phase != QStringLiteral("PhaseNone") || world.turn.extraTurn
        || world.turn.playerId != expected.turn.playerId || world.turn.roundScopeId != expected.turn.roundScopeId
        || !world.turn.turnScopeId.startsWith(QStringLiteral("setup:"))))
        return fail(error, QStringLiteral("candidate changed setup-only zones/history/scope"));
    if(world.zones.size()!=expected.zones.size() || world.turn.playerId!=expected.turn.playerId
        || (m_running && (world.turn.phase!="RoundStart" || world.turn.extraTurn)))
        return fail(error,QStringLiteral("candidate changed native turn/zone inventory"));
    for(auto it=world.cards.cbegin();it!=world.cards.cend();++it) {
        const auto &b=expected.cards[it.key()];
        if(it->definition!=b.definition || it->suit!=b.suit || it->number!=b.number || it->state!=b.state)
            return fail(error,QStringLiteral("candidate changed reserved native card state"));
    }
    if(m_running) {
        if(world.history.nextEventId!=expected.history.nextEventId || world.history.activeEventIds!=expected.history.activeEventIds
            || world.history.events.size()!=expected.history.events.size()) return fail(error,QStringLiteral("candidate changed reserved native history"));
        for(qsizetype i=0;i<world.history.events.size();++i) {
            const auto &a=world.history.events[i];const auto &b=expected.history.events[i];
            if(a.id!=b.id||a.parentId!=b.parentId||a.kind!=b.kind||a.data!=b.data) return fail(error,QStringLiteral("candidate changed reserved native history"));
        }
    }
    for (qsizetype i = 0; i < world.zones.size(); ++i) {
        const auto &a = world.zones[i]; const auto &b = expected.zones[i];
        if (a.id != b.id || a.kind != b.kind || a.ownerId != b.ownerId || a.name != b.name || a.cards != b.cards)
            return fail(error, QStringLiteral("candidate changed native empty zones"));
    }
    for (auto &seat : native.seats) {
        const QString id = seat.player->objectName();
        const auto &value = world.players.value(id);
        const auto &old = expected.players.value(id);
        if (value.alive != old.alive || value.faceUp != old.faceUp || value.chained != old.chained
            || value.properties.keys() != QStringList{QStringLiteral("native_phase")}
            || value.properties.value("native_phase").userType() != QMetaType::Int
            || value.usageHistory!=old.usageHistory || value.armor != value.marks.value(QStringLiteral("@HuJia")))
            return fail(error, QStringLiteral("candidate changed unsupported native player state"));
        const int phase = value.properties.value("native_phase").toInt();
        if (phase < Player::RoundStart || phase > Player::PhaseNone)
            return fail(error, QStringLiteral("invalid native phase"));
        seat.hp = value.hp; seat.maxHp = value.maxHp; seat.phase = static_cast<Player::Phase>(phase);
        seat.marks = value.marks; seat.tags = value.tags;
        for (auto &instances : seat.skills) {
            for (auto &skill : instances) {
                const QString stable = skillId(id, skill.skillName, skill.instanceID);
                const auto &value = world.skills.value(stable);
                const auto &old = expected.skills.value(stable);
                if (value.definition != old.definition || value.ownerId != old.ownerId
                    || value.parentId != old.parentId || value.providerId != old.providerId)
                    return fail(error, QStringLiteral("candidate changed native skill topology"));
                skill.state = value.state; skill.correctState = value.correctState;
            }
        }
    }
    if(m_running) for(auto &seat:native.seats) {
        if((seat.combo.isEmpty() && seat.tags.contains("ComboMovesCard"))
            || (!seat.combo.isEmpty() && seat.tags.value("ComboMovesCard").toMap()!=seat.combo))
            return fail(error,QStringLiteral("provider changed reserved owned-card tag"));
        seat.rawTags=seat.tags;
        seat.stagedCombo=std::make_shared<ManagedOwnedCard>();
        if(!seat.combo.isEmpty()) {
            const int id=seat.combo.value("id").toInt();
            const Card *prototype=Sanguosha->getEngineCard(id);
            if(!prototype || QString::fromLatin1(prototype->metaObject()->className())!=seat.combo.value("class").toString()
                || prototype->objectName()!=seat.combo.value("name").toString())
                return fail(error,QStringLiteral("owned-card factory no longer matches audited native definition"));
            Card *c=Card::Clone(prototype);
            seat.stagedCombo->card=c;
            if(!c || c->getClassName()!=seat.combo.value("class").toString()
                || c->objectName()!=seat.combo.value("name").toString())
                return fail(error,QStringLiteral("cannot rebuild owned native card definition"));
            auto f=readFace(seat.combo.value("face").toMap());
            c->m_id=id;c->m_suit=f.suit;c->m_number=f.number;
            c->m_skillInstanceId=f.instance;c->m_sourceSkillInstanceId=f.sourceInstance;c->m_activationSkillInstanceId=f.activationInstance;
            c->m_skillName=f.skill;c->m_sourceSkillName=f.source;c->m_activationSkillName=f.activation;c->show_skill=f.show;
            c->flags=f.flags;c->tag=f.tags;c->m_appliedPhysicalEffectSource=f.applied;
            c->target_fixed=f.targetFixed;c->mute=f.mute;c->will_throw=f.willThrow;c->has_preact=f.preAction;
            c->can_recast=f.recast;c->is_gift=f.gift;c->is_transferable=f.transferable;
            c->damage_card=f.damage;c->single_target=f.single;c->handling_method=f.method;
            const auto tag=QVariant::fromValue(CardTagOwner{c});
            QByteArray why;
            if(!globalCardLifetimeManager().retainVariantTag(seat.stagedCombo.get(),"ComboMovesCard",tag,&why))
                return fail(error,QStringLiteral("cannot stage owned-card lease: %1").arg(QString::fromUtf8(why)));
            seat.rawTags.insert("ComboMovesCard",tag);
        }
    }
    native.roomTags = world.roomTags;
    if(m_running) {
        native.rawRoomTags=native.roomTags;
        if(native.roomTags.contains("HpChangedData")) {
            const auto value=native.roomTags.value("HpChangedData");const auto map=value.toMap();
            if(value.userType()!=QMetaType::QVariantMap || !exactKeys(map,{"native_struct","recover","reason","who","card","card_part"})
                || map.value("native_struct").userType()!=QMetaType::QString || map.value("native_struct").toString()!="RecoverStruct"
                || map.value("card_part").userType()!=QMetaType::QString
                || map.value("recover").userType()!=QMetaType::Int || map.value("reason").userType()!=QMetaType::QString)
                return fail(error,QStringLiteral("unsupported typed HpChangedData payload"));
            ServerPlayer *who=nullptr;const Card *card=nullptr;
            if(map.value("who").isValid()) {
                if(!exactReference(map.value("who"),"player")) return fail(error,QStringLiteral("malformed recovery player reference"));
                const auto ref=map.value("who").toMap().value("$ref").toMap();
                if(ref.value("kind").toString()!="player") return fail(error,QStringLiteral("invalid recovery player reference"));
                for(const auto &seat:native.seats) if(seat.player->objectName()==ref.value("id").toString()) who=seat.player;
                if(!who) return fail(error,QStringLiteral("unresolved recovery player reference"));
            }
            const auto part=map.value("card_part").toString();
            if(map.value("card").isValid()) {
                if(!exactReference(map.value("card"),"card")) return fail(error,QStringLiteral("malformed recovery card reference"));
                const auto ref=map.value("card").toMap().value("$ref").toMap();const int id=ref.value("id").toString().toInt();
                if(ref.value("kind").toString()!="card" || QString::number(id)!=ref.value("id").toString()
                    || !native.cards.contains(id) || (part!="wrapper" && part!="inner"))
                    return fail(error,QStringLiteral("invalid recovery card reference"));
                card=part=="wrapper"?static_cast<Card *>(native.cards[id].wrapper.data()):native.cards[id].inner.data();
            } else if(part!="none") return fail(error,QStringLiteral("invalid null recovery card reference"));
            const RecoverStruct recover(who,card,map.value("recover").toInt(),map.value("reason").toString());
            native.rawRoomTags.insert("HpChangedData",QVariant::fromValue(recover));
        }
        for(auto it=native.roomTags.cbegin();it!=native.roomTags.cend();++it) {
            if(!it.key().startsWith("UseHistory")) continue;
            const auto map=it.value().toMap();
            if(it.value().userType()!=QMetaType::QVariantMap
                || !exactKeys(map,{"native_struct","card","from","to","whocard","who","owner_use","add_history",
                    "handcard","validate_targets","nullified","no_respond","no_offset","extra_use","bypass_cost",
                    "skip_effect","finished","reveal_history","reveal_event","empty_reveal_options"})
                || map.value("native_struct").userType()!=QMetaType::QString || map.value("native_struct").toString()!="CardUseStruct")
                return fail(error,QStringLiteral("unsupported typed UseHistory envelope"));
            bool valid=true;
            for(const auto &key:{"owner_use","add_history","handcard","validate_targets","bypass_cost","skip_effect","finished"})
                valid=valid && map.value(key).userType()==QMetaType::Bool;
            for(const auto &key:{"extra_use","empty_reveal_options"}) valid=valid && map.value(key).userType()==QMetaType::Int;
            for(const auto &key:{"reveal_history","reveal_event"}) valid=valid && map.value(key).userType()==QMetaType::QString;
            for(const auto &key:{"nullified","no_respond","no_offset"}) valid=valid && exactStringList(map.value(key));
            valid=valid && map.value("to").userType()==QMetaType::QVariantList;
            bool eventOk=false;
            const auto eventId=map.value("reveal_event").toString().toLongLong(&eventOk);
            valid=valid && eventOk && QString::number(eventId)==map.value("reveal_event").toString();
            if(!valid) return fail(error,QStringLiteral("invalid exact UseHistory field types"));
            const auto playerRef=[&](const QVariant &v)->ServerPlayer * {
                if(!v.isValid()) return nullptr;
                if(!exactReference(v,"player")) {valid=false;return nullptr;}
                const auto ref=v.toMap().value("$ref").toMap();
                if(ref.value("kind").toString()=="player") for(const auto &seat:native.seats)
                    if(seat.player->objectName()==ref.value("id").toString()) return seat.player;
                valid=false;return nullptr;
            };
            const auto cardRef=[&](const QVariant &v)->const Card * {
                if(!v.isValid()) return nullptr;
                const auto data=v.toMap();
                if(v.userType()!=QMetaType::QVariantMap || !exactKeys(data,{"ref","part"})
                    || !exactReference(data.value("ref"),"card") || data.value("part").userType()!=QMetaType::QString) {
                    valid=false;return nullptr;
                }
                const auto ref=data.value("ref").toMap().value("$ref").toMap();
                const int id=ref.value("id").toString().toInt();const auto part=data.value("part").toString();
                if(ref.value("kind").toString()!="card" || QString::number(id)!=ref.value("id").toString()
                    || !native.cards.contains(id) || (part!="wrapper" && part!="inner")) {
                    valid=false;return nullptr;
                }
                return part=="wrapper"?static_cast<const Card *>(native.cards[id].wrapper.data()):native.cards[id].inner.data();
            };
            CardUseStruct use;use.card=cardRef(map.value("card"));use.from=playerRef(map.value("from"));
            use.whocard=cardRef(map.value("whocard"));use.who=playerRef(map.value("who"));
            for(const auto &v:map.value("to").toList()) { auto *p=playerRef(v);if(!p) valid=false;use.to<<p; }
            use.m_isOwnerUse=map.value("owner_use").toBool();use.m_addHistory=map.value("add_history").toBool();
            use.m_isHandcard=map.value("handcard").toBool();use.m_validateTargets=map.value("validate_targets").toBool();
            use.nullified_list=readStrings(map.value("nullified"));use.no_respond_list=readStrings(map.value("no_respond"));
            use.no_offset_list=readStrings(map.value("no_offset"));use.extra_use=map.value("extra_use").toInt();
            use.bypass_cost=map.value("bypass_cost").toBool();use.skipSkillEffect=map.value("skip_effect").toBool();
            use.cardFinished=map.value("finished").toBool();use.targetModReveal.historyKey=map.value("reveal_history").toString();
            use.targetModReveal.useHistoryEventId=map.value("reveal_event").toString().toLongLong();
            const int options=map.value("empty_reveal_options").toInt();
            if(options<0 || options>16) valid=false;
            else for(int i=0;i<options;++i) use.targetModReveal.options.append(QList<SkillInstanceRef>());
            if(!valid) return fail(error,QStringLiteral("invalid native UseHistory references/options"));
            native.rawRoomTags.insert(it.key(),QVariant::fromValue(use));
        }
    }
    return true;
}

std::unique_ptr<RoomManagedState::Candidate> RoomManagedState::prepareRestore(const QString &anchorId, QString *error)
{
    if (!m_store || !quiescent(error)) return {};
    LuaRuntime::Binding gameLock(m_room.roomRuntime()->lua(), false);
    LuaRuntime::Binding aiLock(m_room.roomRuntime()->ai().lua(), false);
    auto native = captureNative(error);
    if (!native) return {};
    auto data = std::make_unique<Candidate::Data>();
    data->owner = this; data->expectedFingerprint = native->fingerprint; data->expectedRevision = native->revision;
    data->world = m_store->prepareRestore(anchorId, error);
    const auto anchor=m_store->timeline().anchor(anchorId);
    if (data->world && (anchor.kind==GameTimeline::AnchorKind::FullRound ? data->world->state().turn.roundScopeId : data->world->state().turn.turnScopeId) != anchor.scopeId) {
        fail(error, QStringLiteral("provider changed the bound managed setup anchor scope"));
        return {};
    }
    if(m_running && data->world) {
        const auto saved=m_store->checkpointState(anchorId);
        if(!saved || encoded(saved->providers.value("engine.native").state)!=encoded(data->world->state().providers.value("engine.native").state)) {
            fail(error,QStringLiteral("provider changed reserved native checkpoint payload"));return {};
        }
    }
    if(m_running && data->world) {
        const auto saved=m_store->checkpointState(anchorId);
        auto keys=saved->roomTags.keys();for(const auto &key:data->world->state().roomTags.keys()) if(!keys.contains(key)) keys<<key;
        for(const auto &key:keys) if(key=="HpChangedData" || key.startsWith("UseHistory"))
            if(encoded(saved->roomTags.value(key))!=encoded(data->world->state().roomTags.value(key))) {
                fail(error,QStringLiteral("provider changed reserved native typed room tag"));return {};
            }
    }
    if (!data->world || !buildNativeCandidate(*native, data->world->state(), error)) return {};
    data->native = std::move(native);
    return std::unique_ptr<Candidate>(new Candidate(std::move(data)));
}

bool RoomManagedState::publish(Candidate &&candidate, QString *error)
{
    if (!m_store || !candidate.d || candidate.d->owner != this || !quiescent(error))
        return fail(error, QStringLiteral("invalid managed Room candidate or non-quiescent Room"));
    LuaRuntime::Binding gameLock(m_room.roomRuntime()->lua(), false);
    LuaRuntime::Binding aiLock(m_room.roomRuntime()->ai().lua(), false);
    auto live = captureNative(error);
    if (!live || live->revision != candidate.d->expectedRevision
        || live->fingerprint != candidate.d->expectedFingerprint)
        return fail(error, QStringLiteral("native Room changed after candidate preparation"));
    auto &data = *candidate.d;
    if (live->seats.size() != data.native->seats.size()) return fail(error, QStringLiteral("native roster changed"));
    for (qsizetype i = 0; i < live->seats.size(); ++i)
        if (!data.native->seats[i].player || live->seats[i].player != data.native->seats[i].player)
            return fail(error, QStringLiteral("native player identity changed"));
    for(auto it=live->cards.cbegin();it!=live->cards.cend();++it) {
        const auto target=data.native->cards.value(it.key());
        if(it->wrapper!=target.wrapper || it->inner!=target.inner) return fail(error,QStringLiteral("native card object changed after prepare"));
    }
    std::vector<std::unique_lock<QMutex>> cacheLocks;
    cacheLocks.reserve(size_t(data.native->seats.size()));
    for (const auto &seat : data.native->seats) cacheLocks.emplace_back(seat.player->m_skillCacheMutex);
    auto &lifetime=globalCardLifetimeManager();
    std::unique_lock<CardLifetimeManager::ProfiledMutex> lifetimeLock(lifetime.m_mutex,std::defer_lock);
    decltype(lifetime.m_variantTags) preparedTags;
    if(m_running) {
        lifetimeLock.lock();
        preparedTags=lifetime.m_variantTags;
        for(qsizetype i=0;i<data.native->seats.size();++i) {
            auto &seat=data.native->seats[i];
            if(!seat.stagedCombo || live->seats[i].liveCombo!=seat.liveCombo)
                return fail(error,QStringLiteral("owned card identity changed after candidate prepare"));
            const void *owner=seat.player.data();const void *staged=seat.stagedCombo.get();
            const QByteArray key("ComboMovesCard");
            const auto old=preparedTags.value(owner).value(key);
            const auto next=preparedTags.value(staged).value(key);
            const auto exclusive=[&](Card *card,const auto &tokens) {
                if(!card) return tokens.isEmpty();
                const auto entry=lifetime.m_entries.constFind(card);
                return entry!=lifetime.m_entries.cend() && entry->domain==m_room.roomRuntime() && entry->nativeLeases==1 && !entry->wrappers
                    && !entry->pending && !entry->adoptionReservations && tokens.size()==1 && tokens.first()->address==card;
            };
            if(!exclusive(seat.liveCombo.data(),old) || !exclusive(seat.stagedCombo->card,next))
                return fail(error,QStringLiteral("owned card lease changed before publication"));
            // All detach/allocation happens here, before either root publishes.
            if(next.isEmpty()) { preparedTags[owner].remove(key);if(preparedTags[owner].isEmpty()) preparedTags.remove(owner); }
            else preparedTags[owner].insert(key,next);
            if(old.isEmpty()) { preparedTags[staged].remove(key);if(preparedTags[staged].isEmpty()) preparedTags.remove(staged); }
            else preparedTags[staged].insert(key,old);
        }
    }
    if (!m_store->canPublish(*data.world, error)) return false;
    // Single owner thread + both VM locks: no observer can run between this root
    // publication and raw native swaps. There are no Lua API calls or callbacks.
    if (!m_store->publish(std::move(*data.world), error)) return false;
    if(m_running) lifetime.m_variantTags.swap(preparedTags);
    for (auto &seat : data.native->seats) {
        auto *player = seat.player.data();
        std::swap(player->hp, seat.hp); std::swap(player->max_hp, seat.maxHp);
        std::swap(player->phase, seat.phase); player->marks.swap(seat.marks);
        if(m_running) {
            player->tag.swap(seat.rawTags);
            seat.stagedCombo->card=seat.liveCombo.data();
        } else player->tag.swap(seat.tags);
        player->m_skillInstances.swap(seat.skills);
        // No rules definitions/UI general exist on this admitted path. Drop the
        // pure derived cache without emitting skill/mark/HP signals.
        player->m_skillValidityCache.clear();
    }
    if(m_running) m_room.tag.swap(data.native->rawRoomTags);
    else m_room.tag.swap(data.native->roomTags);
    if(m_running) publishRunning(*data.native);
    m_room.roomRuntime()->advanceStateRevision(RoomRuntime::PlayerPropertyChanged);
    data.owner = nullptr;
    return true;
}

void RoomManagedState::publishRunning(NativeSlice &native) noexcept
{
    for(auto &s:native.seats) {
        auto *p=s.player.data();
        std::swap(p->face_up,s.faceUp);std::swap(p->chained,s.chained);
        p->handcards.swap(s.hand);p->equips.swap(s.equips);p->judging_area.swap(s.judge);
        p->piles.swap(s.piles);p->pile_open.swap(s.pileOpen);p->history.swap(s.history);p->flags.swap(s.flags);
        p->shown_handcards.swap(s.shown);p->broken_equips.swap(s.broken);p->phases.swap(s.phases);
        p->_m_phases_state.swap(s.phaseState);std::swap(p->_m_phases_index,s.phaseIndex);
        p->m_skillDescriptionUsage.swap(s.descriptionUsage);p->m_skillDescriptionValidity.swap(s.descriptionValidity);
        p->m_skillDescriptionEffects.swap(s.descriptionEffects);
    }
    const auto swapFace=[](Card *c,ManagedCardFace &f) noexcept {
        std::swap(c->m_suit,f.suit);std::swap(c->m_number,f.number);
        std::swap(c->m_skillInstanceId,f.instance);std::swap(c->m_sourceSkillInstanceId,f.sourceInstance);
        std::swap(c->m_activationSkillInstanceId,f.activationInstance);
        c->m_skillName.swap(f.skill);c->m_sourceSkillName.swap(f.source);c->m_activationSkillName.swap(f.activation);
        c->show_skill.swap(f.show);c->flags.swap(f.flags);c->tag.swap(f.tags);c->m_appliedPhysicalEffectSource.swap(f.applied);
        std::swap(c->target_fixed,f.targetFixed);std::swap(c->mute,f.mute);std::swap(c->will_throw,f.willThrow);
        std::swap(c->has_preact,f.preAction);std::swap(c->can_recast,f.recast);std::swap(c->is_gift,f.gift);
        std::swap(c->is_transferable,f.transferable);std::swap(c->damage_card,f.damage);std::swap(c->single_target,f.single);
        std::swap(c->handling_method,f.method);
    };
    for(auto &card:native.cards) {
        swapFace(card.wrapper.data(),card.outer);swapFace(card.inner.data(),card.face);
        std::swap(card.wrapper->m_isModified,card.modified);
    }
    auto &movement=*m_room.m_cardMovement;
    movement.drawPile().swap(native.draw);movement.discardPile().swap(native.discard);movement.m_tableCards.swap(native.table);
    movement.m_locations.m_places.swap(native.places);movement.m_locations.m_owners.swap(native.owners);
    std::swap(m_room.current,native.current);
    auto &runtime=*m_room.roomRuntime();auto &state=runtime.state();
    std::swap(state.m_currentPlayer,native.stateCurrent);state.m_currentCardUsePattern.swap(native.pattern);
    std::swap(state.m_currentCardUseReason,native.reason);state.m_flags.swap(native.roomFlags);
    runtime.rng().swapState(*native.preparedGameRng);runtime.ai().commitPreparedRng(*native.preparedAiRng);
    m_room.resolutionHistory().swap(*native.preparedHistory);
    std::swap(m_room.m_numericStateHistoryStarted,native.numericHistory);std::swap(m_room._m_lastMovementId,native.movementId);
    auto &ai=*m_room.m_aiDecisions;ai.m_events.swap(native.aiEvents);std::swap(ai.m_eventSequence,native.aiSequence);
    ai.m_markViewers.swap(native.markViewers);ai.m_publicBoardValid=false;ai.m_distanceCacheValid=false;
}
