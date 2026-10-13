#include "ai-strategy-public-action-flags.h"
#include "ai-strategy-room-ledger.h"
#include "ai-strategy-round53-xufa-response.h"
#include "ai-strategy-round54-jijiang.h"
#include "ai-strategy-events.h"
#include "room.h"
#include "serverplayer.h"
#include "engine.h"
#include "standard.h"
#include <QObject>
#include <QSet>
#include <QHash>

namespace {
// QObject ownership gives each Room its own ledger and teardown. No static
// cross-Room state, legacy AI globals or scripting userdata are involved.
class EventKnowledge final : public AIRoomLedger {
public:
    explicit EventKnowledge(Room &room) : AIRoomLedger(room)
    { setObjectName(QStringLiteral("isolated_ai_event_knowledge")); }
    QHash<QString, QHash<QString, QSet<int>>> hands;
    QHash<QString, QHash<QString, QSet<int>>> revoked;
};
EventKnowledge &knowledge(Room &room)
{
    QObject *child = room.findChild<QObject *>(QStringLiteral("isolated_ai_event_knowledge"), Qt::FindDirectChildrenOnly);
    if (auto *state = dynamic_cast<EventKnowledge *>(child)) return *state;
    return *new EventKnowledge(room);
}

bool publicPlace(Player::Place place)
{
    return place == Player::PlaceEquip || place == Player::PlaceDelayedTrick
        || place == Player::DiscardPile;
}

const AIPlayerView *playerView(const AIWorldView &world, const Player *player)
{
    if (!player) return nullptr;
    if (world.self.objectName == player->objectName()) return &world.self;
    for (const AIPlayerView &view : world.players)
        if (view.objectName == player->objectName()) return &view;
    return nullptr;
}

bool knownInWorld(const AIWorldView &world, const Player *owner, int id)
{
    if (!owner) return false;
    const AIPlayerView *view = playerView(world, owner);
    if (!view) return false;
    const QList<AICardView> &cards = owner->objectName() == world.self.objectName ? world.handCards : view->knownCards;
    for (const AICardView &card : cards) if (card.effectiveId == id) return true;
    return false;
}

AICardView physicalView(const Card *card, bool own)
{
    AICardView view;
    view.cardId = card->getId(); view.effectiveId = card->getEffectiveId();
    view.objectName = card->objectName(); view.className = card->getClassName();
    view.nameLength = card->nameLength();
    view.skillName = card->getSkillName(false);
    if (view.effectiveId >= 0) if (const Card *original = Sanguosha->getEngineCard(view.effectiveId)) view.engineClassName = original->getClassName();
    if (own || card->getSourceSkillName() == view.skillName) view.sourceSkillName = card->getSourceSkillName();
    view.skillNamesComplete = own;
    if (own) view.skillNames = card->getSkillNames();
    view.kindOfNames = card->getKindOfNames();
    view.suit = int(card->getSuit()); view.number = card->getNumber();
    view.red = card->isRed(); view.black = card->isBlack();
    view.typeId = int(card->getTypeId()); view.handlingMethod = int(card->getHandlingMethod());
    view.virtualCard = card->isVirtualCard(); view.targetFixed = card->targetFixed();
    view.damageCard = card->isDamageCard(); view.singleTarget = card->isSingleTargetCard(); view.canRecast = card->canRecast();
    if (const auto *equip = dynamic_cast<const EquipCard *>(card->getRealCard())) {
        view.equipSlot = int(equip->location()); view.equipOccupyLocations = equip->getOccupyLocations();
    }
    if (const auto *weapon = dynamic_cast<const Weapon *>(card->getRealCard())) view.weaponRange = weapon->getRange();
    return view;
}

QJsonObject cardJson(const Card *card, bool own, Room &room, ServerPlayer *observer)
{
    const AICardView view = physicalView(card, own);
    QJsonObject out{{"id",view.cardId},{"effective_id",view.effectiveId},{"name",view.objectName},
        {"class_name",view.className},{"name_length",view.nameLength},{"engine_class_name",view.engineClassName},{"type_id",view.typeId},{"suit",view.suit},{"number",view.number},
        {"red",view.red},{"black",view.black},{"skill_name",view.skillName},
        {"kind_of",QJsonArray::fromStringList(view.kindOfNames)},
        {"skill_names_complete",own},
        {"target_fixed",view.targetFixed},{"damage_card",view.damageCard},{"single_target",view.singleTarget},
        {"virtual_card",view.virtualCard},{"handling_method",view.handlingMethod},
        {"equip_slot",view.equipSlot},{"weapon_range",view.weaponRange},{"can_recast",view.canRecast},
        {"subtype",card->getSubtype()}};
    if (own) out.insert("skill_names",QJsonArray::fromStringList(view.skillNames));
    if (own || card->getSourceSkillName() == view.skillName) out.insert("source_skill",view.sourceSkillName);
    QJsonObject flags = readAIStrategyPublicCardFlags(room, observer, card);
    if (own) {
        for (const QString &flag : card->getFlags())
            if (!flag.startsWith("visible_")) flags.insert(flag, true);
        out.insert("flags_complete", true);
        out.insert("flags_complete_scope", "owner_non_visibility");
    }
    out.insert("known_flags", flags);
    out.insert("subcards",QJsonArray{});
    QJsonArray equipLocations; for (int slot : view.equipOccupyLocations) equipLocations.append(slot);
    out.insert("equip_occupy_locations",equipLocations);
    return out;
}

void grantHand(EventKnowledge &state, ServerPlayer *observer, Player *owner, int id)
{
    if (observer && owner && owner->handCards().contains(id)) {
        state.hands[observer->objectName()][owner->objectName()].insert(id);
        state.revoked[observer->objectName()][owner->objectName()].remove(id);
    }
}
}

void projectAIStrategyKnownCards(AIWorldView &world, Room &room, ServerPlayer *observer)
{
    if (!observer) return;
    EventKnowledge &state = knowledge(room);
    // History is projected by the trusted viewer-scoped persistence boundary.
    // An unpartitioned legacy file cannot establish ownership of its samples.
    // With no configured store, preserve the Lua viewer shadow between rounds.
    if (!world.strategyFacts.contains("ai_data_known")) {
        world.strategyFacts.insert("ai_data_known",true);
        world.strategyFacts.insert("ai_data_persistent",false);
    }
    auto &viewer = state.hands[observer->objectName()];
    for (AIPlayerView &view : world.players) {
        ServerPlayer *owner = room.findPlayerByObjectName(view.objectName, true);
        if (!owner) continue;
        const QSet<int> revoked = state.revoked.value(observer->objectName()).value(view.objectName);
        if (!revoked.isEmpty() && !observer->canSeeHandcard(owner)) {
            for (auto it=view.knownCards.begin(); it!=view.knownCards.end();) {
                const Card *card = Sanguosha->getCard(it->effectiveId);
                if (revoked.contains(it->effectiveId) && card && !card->hasFlag("visible")) it=view.knownCards.erase(it);
                else ++it;
            }
        }
        auto known = viewer.find(view.objectName);
        if (known == viewer.end()) continue;
        QSet<int> projected;
        for (const AICardView &card : view.knownCards) projected.insert(card.effectiveId);
        // Stale knowledge never exposes a card in a new hidden owner/zone.
        const QList<int> hand = owner->handCards();
        for (auto it = known->begin(); it != known->end();) {
            if (!hand.contains(*it)) { it = known->erase(it); continue; }
            const Card *card = Sanguosha->getCard(*it);
            if (card && !projected.contains(*it)) view.knownCards.append(physicalView(card, false));
            ++it;
        }
    }
}

void projectAIStrategyEvent(AIEventView &event, const AIWorldView &world, Room &room,
                           ServerPlayer *observer, ServerPlayer *target, const QVariant &data)
{
    if (!observer || !target) return;
    EventKnowledge &state = knowledge(room);
    // Full borrowed/source skill chains belong only to the actual card actor.
    // Foreign observers retain the public activation-only record, without even
    // revealing whether an additional private root exists.
    const Card *actorCard = nullptr;
    const Player *cardActor = nullptr;
    if (data.canConvert<DamageStruct>()) {
        const auto value=data.value<DamageStruct>(); actorCard=value.card; cardActor=value.from;
    } else if (data.canConvert<CardUseStruct>()) {
        const auto value=data.value<CardUseStruct>(); actorCard=value.card; cardActor=value.from;
    } else if (data.canConvert<CardEffectStruct>()) {
        const auto value=data.value<CardEffectStruct>(); actorCard=value.card; cardActor=value.from;
    } else if (data.canConvert<RecoverStruct>()) {
        const auto value=data.value<RecoverStruct>(); actorCard=value.card; cardActor=value.who;
    } else if (data.canConvert<DeathStruct>()) {
        const auto value=data.value<DeathStruct>();
        if (value.damage) { actorCard=value.damage->card; cardActor=value.damage->from; }
    }
    if (actorCard) {
        QJsonObject card = event.details.value("card").toObject();
        if (!card.isEmpty()) {
            // Two native constructor origins only, at the actual declared-use
            // viewer boundary. Never export a different/private current chain.
            if (cardActor != observer && event.triggerEvent == PreCardUsed
                && event.kind == QStringLiteral("card_use") && data.canConvert<CardUseStruct>()) {
                auto names = readAIRound53XufaConstructedSkills(room, observer, actorCard);
                if (names.isEmpty()) names = readAIRound54OLJijiangConstructedSkills(room, observer, actorCard);
                if (!names.isEmpty()) {
                    card.insert("skill_names", names.value("skill_names"));
                    card.insert("skill_names_complete", true);
                    event.details.insert("card_skills", names.value("skill_names"));
                    event.details.insert("card_skills_complete", true);
                }
            }
            QJsonObject flags = readAIStrategyPublicCardFlags(room, observer, actorCard);
            if (cardActor == observer) {
                for (const QString &flag : actorCard->getFlags())
                    if (!flag.startsWith("visible_")) flags.insert(flag, true);
                card.insert("flags_complete", true);
                card.insert("flags_complete_scope", "owner_non_visibility");
                event.intentionSuppressed = actorCard->hasFlag("AIGlobal_ComboFallback")
                    || actorCard->hasFlag("meihuomoyan") || actorCard->hasFlag("sgkgodshunshi")
                    || actorCard->hasFlag("kenewmieyao");
                event.details.insert("intention_suppression_known", true);
            }
            card.insert("known_flags", flags);
            event.details.insert("card", card);
        }
    }
    if (actorCard && cardActor==observer) {
        event.details.insert("card_skills",QJsonArray::fromStringList(actorCard->getSkillNames()));
        event.details.insert("card_skills_complete",true);
        QJsonObject card=event.details.value("card").toObject();
        if (!card.isEmpty()) {
            card.insert("skill_names",QJsonArray::fromStringList(actorCard->getSkillNames()));
            card.insert("skill_names_complete",true);
            card.insert("source_skill",actorCard->getSourceSkillName());
            // Owner PreCardUsed valuation compares this declared recipe with
            // the owner's current hand. Publish only currently owned/public
            // material IDs; no foreign private cost is inferred from actorhood.
            if (event.triggerEvent == PreCardUsed) {
                bool known = true;
                QJsonArray materials;
                for (int id : actorCard->getSubcards()) {
                    const auto place = room.getCardPlace(id);
                    if (id < 0 || (room.getCardOwner(id) != observer
                        && place != Player::PlaceTable && !publicPlace(place))) {
                        known = false;
                        break;
                    }
                    materials.append(id);
                }
                if (known) {
                    card.insert("virtual_card", actorCard->isVirtualCard());
                    card.insert("subcards", materials);
                    card.insert("effective_id", actorCard->getEffectiveId());
                }
            }
            event.details.insert("card",card);
        }
    }
    if (event.kind == QStringLiteral("damage") || event.kind == QStringLiteral("judge")) {
        const QString skill = !event.reason.isEmpty() ? event.reason : event.cardSkill;
        bool complete = true;
        QString owner;
        if (!skill.isEmpty()) for (ServerPlayer *candidate : room.getAllPlayers()) {
            const AIPlayerView *view = playerView(world,candidate);
            if (candidate != observer && (!view || !view->skillsComplete)) { complete=false; break; }
            // findPlayerBySkillName's second argument is includeLose, not dead.
            if (candidate->hasSkill(skill,event.kind == QStringLiteral("damage"))) { owner=candidate->objectName(); break; }
        }
        event.details.insert("intention_from_complete",complete);
        event.details.remove("intention_from");
        if (complete) event.details.insert("intention_from",owner);
        else event.details.insert("intention_from_private",true);
    }
    if (event.triggerEvent == CardsMoveOneTime && data.canConvert<CardsMoveOneTimeStruct>()) {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        QJsonObject value{{"from",move.from ? move.from->objectName() : QString()},
            {"to",move.to ? move.to->objectName() : QString()}, {"to_place",int(move.to_place)},
            {"reason",move.reason.m_reason},
            {"basic_reason",move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON},
            {"reason_player",move.reason.m_playerId},{"skill",move.reason.m_skillName},
            {"card_count",move.card_ids.size()}};
        QJsonArray rows;
        for (int index=0; index<move.card_ids.size(); ++index) {
            const int id = move.card_ids.at(index);
            const Player::Place fromPlace = move.from_places.value(index, Player::PlaceUnknown);
            const Card *card = Sanguosha->getCard(id);
            QJsonObject row{{"from_place",int(fromPlace)}, {"public",card && card->hasFlag("visible")},
                {"open",index < move.open.size() && move.open.at(index)}};
            if (fromPlace == Player::PlaceHand && target == room.getCurrent()) {
                // Source filterEvent removes visible_* flags, across owners.
                auto &owned = state.hands[observer->objectName()];
                for (auto it=owned.begin(); it!=owned.end(); ++it) it->remove(id);
                for (ServerPlayer *owner : room.getAllPlayers(true))
                    state.revoked[observer->objectName()][owner->objectName()].insert(id);
            }
            if (move.to_place == Player::PlaceHand && move.to == target && move.from == observer)
                grantHand(state,observer,move.to,id);
            const bool authorized = card && (publicPlace(fromPlace) || publicPlace(move.to_place)
                || move.from == observer || move.to == observer
                || (fromPlace == Player::PlaceHand && move.from && observer->canSeeHandcard(move.from))
                || (move.to_place == Player::PlaceHand && move.to && observer->canSeeHandcard(move.to))
                || card->hasFlag("visible") || knownInWorld(world,move.from,id) || knownInWorld(world,move.to,id));
            // PresentCard is a package tag. Only its owner gets the comparison;
            // other observers receive explicit private provenance, not its value.
            if (!move.to) row.insert("present_match",false);
            else if (move.to == observer && card) row.insert("present_match",move.to->getTag("PresentCard").toString() == card->toString());
            else row.insert("present_match_private",true);
            if (authorized) {
                row.insert("card",cardJson(card,move.from == observer || move.to == observer,room,observer));
                auto *source = qobject_cast<ServerPlayer *>(move.from);
                QJsonObject legality;
                bool privateLegality = false;
                if (source == observer) {
                    // ServerPlayer::canUse changes a live mark and emits signals.
                    // Reproduce its query under the read-only mark overlay instead.
                    const bool limited = source->isCardLimited(card,Card::MethodUse);
                    for (ServerPlayer *other : room.getAlivePlayers()) {
                        bool allowed = false;
                        if (!limited) {
                            AIMarkQueryScope scope(source,QStringLiteral("&xiyan1-Clear"),source->getMark("&xiyan1-Clear")+1);
                            int maxVotes = 0;
                            allowed = card->targetFilter(QList<const Player *>(),other,source,maxVotes) || maxVotes > 0;
                        }
                        legality.insert(other->objectName(),allowed);
                    }
                } else if (source) {
                    // Even a public skill callback can consult its owner's hand.
                    // Do not turn another player's hypothetical canUse into an oracle.
                    privateLegality = true;
                }
                row.insert("can_use",legality);
                if (privateLegality) row.insert("can_use_private",true);
            }
            rows.append(row);
        }
        value.insert("cards",rows); event.details.insert("move",value);
    } else if (event.triggerEvent == ChoiceMade) {
        const QString raw = data.toString();
        const QStringList prompt = raw.split(':');
        if (raw.contains(QStringLiteral("fenxin:yes"))) {
            event.details.insert("fenxin_invoked",true);
            // Fenxin's invoker received the target in its invoke request. Another
            // viewer cannot inspect a third player's private FenxinTarget flag.
            const bool complete = observer == target;
            event.details.insert("fenxin_target_complete",complete);
            if (complete) {
                QString chosen;
                for (ServerPlayer *player : room.getAlivePlayers()) if (player->hasFlag("FenxinTarget")) { chosen=player->objectName(); break; }
                event.details.insert("fenxin_target",chosen);
            }
        }
        if (observer == target && prompt.value(0) == QStringLiteral("cardChosen")) {
            bool ok=false; const int id=prompt.value(2).toInt(&ok);
            if (ok) {
                const Player::Place place = room.getCardPlace(id);
                event.details.insert("chosen_card_place",int(place));
                ServerPlayer *owner = room.findPlayerByObjectName(prompt.value(3),true);
                const Card *card = Sanguosha->getCard(id);
                if (card && (publicPlace(place) || owner == observer
                    || (owner && observer->canSeeHandcard(owner)) || knownInWorld(world,owner,id)))
                    event.details.insert("chosen_card",cardJson(card,owner == observer,room,observer));
            }
        }
        if (observer == target) {
            if (prompt.value(0) == QStringLiteral("viewCards")) {
                // Room::showCard can disclose only a subset, while doGongxin
                // supplies the full list. The event name is not a full-hand grant.
                ServerPlayer *owner = room.findPlayerByObjectName(prompt.value(1),true);
                for (const QString &item : prompt.value(2).split('+',Qt::SkipEmptyParts)) {
                    bool ok=false; const int id=item.toInt(&ok);
                    if (ok) grantHand(state,observer,owner,id);
                }
            } else if (prompt.value(0) == QStringLiteral("cardChosen") && prompt.value(4) == QStringLiteral("visible")) {
                ServerPlayer *owner = room.findPlayerByObjectName(prompt.value(3),true);
                if (owner) for (int id : owner->handCards()) grantHand(state,observer,owner,id);
            } else if (prompt.value(0) == QStringLiteral("Yiji")) {
                ServerPlayer *owner = room.findPlayerByObjectName(prompt.value(2),true);
                for (const QString &item : prompt.value(3).split('+',Qt::SkipEmptyParts)) {
                    bool ok=false; const int id=item.toInt(&ok); if (ok) grantHand(state,observer,owner,id);
                }
            }
        }
    } else if (event.triggerEvent == ShowCards) {
        const QStringList fields = data.toString().split(':');
        if (fields.size()>1 && fields.at(1)==observer->objectName())
            for (const QString &item : fields.at(0).split('+',Qt::SkipEmptyParts)) {
                bool ok=false; const int id=item.toInt(&ok); if (ok) grantHand(state,observer,target,id);
            }
    } else if (event.triggerEvent == EventPhaseChanging && data.canConvert<PhaseChangeStruct>()) {
        const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to == Player::NotActive) {
            // GameRule clears -Clear marks for every player, not just the
            // player whose turn ended. This receipt exposes only this viewer.
            event.details.insert("strategy_marks_cleared",QJsonArray{observer->objectName()});
        }
        // GameRule clears every living player's -PlayClear marks when Play
        // ends, and also clears dead players' marks at the turn boundary.
        // Only this observer's legacy policy cache is disclosed here.
        if (change.to == Player::NotActive || (change.from == Player::Play && observer->isAlive()))
            event.details.insert("strategy_play_marks_cleared",QJsonArray{observer->objectName()});
    }
}
