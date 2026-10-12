#include "ai-strategy-round56-maxcards.h"
#include "ai-strategy-round56-projection.h"
#include "ai-strategy-round53-pingjian.h"
#include "ai-strategy-round21-choice.h"
#include "ai-strategy-response-observer.h"
#include "ai-strategy-public-action-flags.h"
#include "ai-strategy-flag-lifecycle.h"
#include "ai-strategy-public-disclosure.h"
#include "ai-strategy-projection.h"
#include "ai-strategy-owner.h"
#include "ai-strategy-animecard.h"
#include "ai-strategy-aliases.h"
#include "ai-strategy-use-dispatch.h"
#include "ai-strategy-card-identity.h"
#include "card-lifetime-manager.h"
#include "ai-strategy-damage.h"
#include "ai-strategy-events.h"
#include "ai-strategy-random.h"
#include "ai-strategy-yczh2016.h"
#include "engine.h"
#include "ai-card-query-scope.h"
#include "wrapped-card.h"
#include "room-state.h"
#include <memory>
namespace QSanEngine { extern thread_local Player *Self; }
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "settings.h"
#include "standard-cards.h"
#include "maneuvering.h"
#include "skill.h"
#include <QJsonArray>
#include <QScopeGuard>

namespace {
QString strategyCardSortKey(const Card *card)
{
    QString key = card->toString();
    if (card->isKindOf("ActiveSkillCard")) {
        key += QLatin1Char('|') + card->getSkillName();
        if (card->getSkillInstanceId() > 0)
            key += QLatin1Char('#') + QString::number(card->getSkillInstanceId());
    }
    return key;
}
QObject *notificationLedger(Room &room, bool create)
{
    const auto name = QStringLiteral("isolated-ai-notification-ledger");
    auto *ledger = room.findChild<QObject *>(name, Qt::FindDirectChildrenOnly);
    if (!ledger && create) { ledger = new QObject(&room); ledger->setObjectName(name); }
    return ledger;
}
}
void recordAIStrategyNotifiedPile(Room &room, ServerPlayer *viewer, const QString &pile,
                                const QJsonObject &locations, bool active,
                                const QList<int> &orderedIds, bool orderKnown)
{
    if (!viewer) return;
    auto *ledger = notificationLedger(room, true);
    auto all = ledger->property("disclosures").toJsonObject();
    auto byViewer = all.value(viewer->objectName()).toObject();
    if (active) {
        QJsonObject cards;
        QJsonArray currentCards;
        for (auto it = locations.begin(); it != locations.end(); ++it) {
            const int id = it.key().toInt();
            // Fake-pile moves disclose IDs and locations, not a private wrapped
            // card update. Only this viewer's own card may use its live wrapper.
            // For every other source retain the immutable printed identity,
            // regardless of whether its hidden wrapper has changed.
            const bool owned = room.getCardOwner(id) == viewer;
            const Card *card = owned ? Sanguosha->getCard(id) : Sanguosha->getEngineCard(id);
            if (!card) continue;
            if (owned) currentCards.append(id);
            QJsonObject printed{{"id", id}, {"effective_id", id}, {"name", card->objectName()},
                {"legacy_string_key", QString::number(id)}, {"legacy_sort_key", QString::number(id)},
                {"class_name", card->getClassName()}, {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
                {"type_id", int(card->getTypeId())}, {"suit", int(card->getSuit())}, {"number", card->getNumber()},
                {"red", card->isRed()}, {"black", card->isBlack()}, {"subtype", card->getSubtype()},
                {"skill_name", card->getSkillName(false)}, {"single_target", card->isSingleTargetCard()}, {"damage_card", card->isDamageCard()},
                {"virtual_card", false}, {"subcards", QJsonArray()}, {"will_throw", card->willThrow()}, {"can_recast", card->canRecast()},
                {"target_fixed", card->targetFixed()}, {"handling_method", int(card->getHandlingMethod())}};
            if (const auto *equip = dynamic_cast<const EquipCard *>(card->getRealCard())) {
                printed.insert("equip_slot", int(equip->location()));
                QJsonArray occupiedSlots;
                for (int slot : equip->getOccupyLocations()) occupiedSlots.append(slot);
                printed.insert("equip_occupy_locations", occupiedSlots);
            }
            if (const auto *weapon = dynamic_cast<const Weapon *>(card->getRealCard())) printed.insert("weapon_range", weapon->getRange());
            cards.insert(it.key(), printed);
        }
        QJsonObject notification{{"locations", locations}, {"cards", cards},
            {"current_card_ids", currentCards}, {"revision", QString::number(room.roomRuntime()->stateRevision())}};
        // Actual recipient notification order, including duplicates and a
        // genuinely empty list. Absence means this producer did not supply order.
        if (orderKnown) {
            QJsonArray order; for (int id : orderedIds) order.append(id);
            notification.insert("ordered_ids", order);
        }
        byViewer.insert(pile, notification);
    }
    else byViewer.remove(pile);
    all.insert(viewer->objectName(), byViewer);
    ledger->setProperty("disclosures", all);
}
QJsonObject readAIStrategyNotifiedPile(Room &room, ServerPlayer *viewer, const QString &pile)
{
    auto *ledger = notificationLedger(room, false);
    if (!ledger || !viewer) return {};
    auto row = ledger->property("disclosures").toJsonObject()
        .value(viewer->objectName()).toObject().value(pile).toObject();
    // A subsequent hidden move can invalidate a remembered location. Never
    // consult its actual hidden position to decide whether it is still known.
    if (row.value("revision").toString() != QString::number(room.roomRuntime()->stateRevision())) {
        row.insert("locations", QJsonObject());
        row.insert("current_card_ids", QJsonArray());
        row.remove("ordered_ids"); // No resurrection from later card membership.
    }
    // A fake-pile ID is not a current face. An independently witnessed real
    // public move may refresh only keys still in this active notification.
    auto cards = row.value("cards").toObject();
    auto locations = row.value("locations").toObject();
    auto current = row.value("current_card_ids").toArray();
    for (auto it = cards.begin(); it != cards.end(); ++it) {
        bool ok = false; const int id = it.key().toInt(&ok);
        if (!ok || id < 0) continue;
        const auto disclosed = readAIStrategyPublicDisclosure(room, viewer, id);
        if (!disclosed.value("known").toBool()) continue;
        it.value() = disclosed.value("card");
        locations.insert(it.key(), QJsonObject{{"owner", disclosed.value("owner")}, {"place", disclosed.value("place")}});
        if (!current.contains(id)) current.append(id);
    }
    if (!row.isEmpty()) {
        row.insert("cards", cards); row.insert("locations", locations); row.insert("current_card_ids", current);
    }
    return row;
}

QMap<QString, bool> projectAICanUseTargets(Room &room, ServerPlayer *owner, const Card *card)
{
    QMap<QString, bool> result;
    if (!owner || !card) return result;
    const bool limited = owner->isCardLimited(card, Card::MethodUse);
    AIMarkQueryScope temporary(owner, QStringLiteral("&xiyan1-Clear"), owner->getMark("&xiyan1-Clear") + 1);
    for (const auto *target : room.getAlivePlayers()) {
        int votes = 0;
        result.insert(target->objectName(), !limited && (card->targetFilter({}, target, owner, votes) || votes > 0));
    }
    return result;
}

namespace {
QJsonObject candidateJson(const AICardCandidateView &c)
{
    QJsonArray combinations;
    for (const auto &targets : c.targetCombinations) combinations.append(QJsonArray::fromStringList(targets));
    QJsonObject filters, votes, canUse, extra;
    for (auto it = c.extraTargetByPlayer.cbegin(); it != c.extraTargetByPlayer.cend(); ++it) extra.insert(it.key(), it.value());
    for (auto it = c.canUseTargets.cbegin(); it != c.canUseTargets.cend(); ++it) canUse.insert(it.key(), it.value());
    for (auto it = c.targetFilterEmpty.cbegin(); it != c.targetFilterEmpty.cend(); ++it) filters.insert(it.key(), it.value());
    for (auto it = c.maxVotes.cbegin(); it != c.maxVotes.cend(); ++it) votes.insert(it.key(), it.value());
    QJsonObject result{{"candidate_id", -1}, {"card_id", c.cardId}, {"available", c.available},
        {"limited", c.limited}, {"jilei", c.jilei}, {"target_fixed", c.targetFixed},
        {"complete_coverage", c.completeCoverage}, {"feasible_with_no_target", c.feasibleWithNoTarget},
        {"legal_targets", QJsonArray::fromStringList(c.legalTargets)}, {"target_combinations", combinations},
        {"target_filter_empty", filters}, {"extra_target_by_player", extra}, {"can_use_targets", canUse}, {"max_votes", votes}, {"history_available", c.historyAvailable}};
    if (c.extraTargetKnown) result.insert("extra_target_count", c.extraTargetCount);
    if (c.affectedTargetsKnown) result.insert("affected_targets", QJsonArray::fromStringList(c.affectedTargets));
    return result;
}
QJsonObject evaluationCard(const Card *card, ServerPlayer *owner, bool localClone = false)
{
    QJsonArray subcards, flags, locations;
    for (int id : card->getSubcards()) subcards.append(id);
    const bool ownsCard = card->getEffectiveId() >= 0
        && owner->getRoom()->getCardOwner(card->getEffectiveId()) == owner;
    if (ownsCard || localClone)
        for (const auto &flag : card->getFlags()) if (!flag.startsWith("visible_")) flags.append(flag);
    QJsonObject result{{"id", card->getId()}, {"effective_id", card->getEffectiveId()},
        {"name", card->objectName()}, {"class_name", card->getClassName()}, {"subtype", card->getSubtype()},
        {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
        {"suit", int(card->getSuit())}, {"number", card->getNumber()}, {"type_id", int(card->getTypeId())},
        {"skill_name", card->getSkillName(false)},
        {"skill_names", QJsonArray::fromStringList(card->getSkillNames())},
        {"red", card->isRed()}, {"black", card->isBlack()}, {"subcards", subcards}, {"flags", flags},
        {"virtual_card", card->isVirtualCard()}, {"target_fixed", card->targetFixed()},
        {"single_target", card->isSingleTargetCard()}, {"damage_card", card->isDamageCard()}, {"will_throw", card->willThrow()}, {"can_recast", card->canRecast()},
        {"handling_method", int(card->getHandlingMethod())}, {"evaluation_only", true}, {"evaluation_owner", owner->objectName()},
        {"owner_state_known", true}, {"locked", owner->isLocked(card)}, {"jilei", owner->isJilei(card)},
        {"using", card->hasFlag("using")}, {"kill_off", card->hasFlag("AIGlobal_KillOff")}};
    result.insert("skill_names_complete", ownsCard || localClone);
    if (ownsCard || localClone) {
        result.insert("flags_complete_scope", "owner_non_visibility");
        result.insert("flags_complete", true);
        QJsonObject knownFlags;
        for (const auto &flag : flags) knownFlags.insert(flag.toString(), true);
        result.insert("known_flags", knownFlags);
    }
    if (!ownsCard && !localClone) {
        result.remove("skill_names");
        result.remove("flags");
        result.insert("known_flags", readAIStrategyPublicCardFlags(*owner->getRoom(), owner, card));
    }
    const QString identity = aiLegacyCardIdentity(*owner->getRoom(), owner, card);
    if (!identity.isEmpty()) result.insert("legacy_identity_token", identity);
    if (ownsCard || localClone || !card->isVirtualCard()) {
        result.insert("legacy_string_key", card->toString());
        result.insert("legacy_sort_key", strategyCardSortKey(card));
    }
    if (ownsCard || card->getSourceSkillName() == card->getSkillName(false))
        result.insert("source_skill", card->getSourceSkillName());
    if (const auto *equip = dynamic_cast<const EquipCard *>(card->getRealCard())) {
        result.insert("equip_slot", int(equip->location()));
        for (int slot : equip->getOccupyLocations()) locations.append(slot);
        result.insert("equip_occupy_locations", locations);
    }
    if (const auto *weapon = dynamic_cast<const Weapon *>(card->getRealCard())) result.insert("weapon_range", weapon->getRange());
    QJsonObject residue;
    for (const auto *target : owner->getRoom()->getAlivePlayers())
        residue.insert(target->objectName(), Sanguosha->correctCardTarget(TargetModSkill::Residue, owner, card, target));
    result.insert("target_residue", residue);
    return result;
}
QJsonObject cardRules(Room &room, ServerPlayer *owner, const Card *card)
{
    QJsonObject limits, prohibited, canUse;
    const auto values = projectAICanUseTargets(room, owner, card);
    for (auto it = values.cbegin(); it != values.cend(); ++it) canUse.insert(it.key(), it.value());
    for (int method = int(Card::MethodNone); method <= int(Card::MethodGet); ++method)
        limits.insert(QString::number(method), owner->isCardLimited(card, Card::HandlingMethod(method)));
    for (const auto *target : room.getAlivePlayers()) prohibited.insert(target->objectName(), room.isProhibited(owner, target, card) != nullptr);
    QJsonObject result{{"limits", limits}, {"prohibited", prohibited}, {"can_use", canUse}, {"available", card->isAvailable(owner)},
        {"target_mod_distance", Sanguosha->correctCardTarget(TargetModSkill::DistanceLimit, owner, card)},
        {"target_mod_extra", Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, owner, card)},
        {"target_mod_residue", Sanguosha->correctCardTarget(TargetModSkill::Residue, owner, card)}};
    if (card->isKindOf("Analeptic")) result.insert("analeptic_static_available", Analeptic::IsAvailable(owner, card));
    return result;
}
void historyAvailability(AICardCandidateView &candidate, const Card *card, ServerPlayer *owner)
{
    if (!card || card->getTypeId() != Card::TypeBasic) return;
    const QString key = card->isKindOf("Slash") ? QStringLiteral("Slash") : card->getClassName();
    // The common planner tries at most four pending Slashes, while Analeptic's
    // count follows the actual owner card pool. No live history is modified.
    const int maximum = qMin(256, owner->getCards("he").size() + owner->getHandPile().size() + 8);
    for (int offset = 0; offset <= maximum; ++offset) {
        AIHistoryQueryScope scope(owner, key, offset);
        candidate.historyAvailable.insert(QString::number(offset), card->isAvailable(owner));
    }
}
void projectSlash(AIWorldView &world, Room &room, ServerPlayer *viewer, const Card *card, const QString &key)
{
    if (!card || !card->isKindOf("Slash")) return;
    QJsonObject byActor = world.strategyFacts.value("slash_checks").toObject();
    QJsonObject targets = byActor.value(viewer->objectName()).toObject();
    const auto *weapon = dynamic_cast<const Weapon *>(viewer->getWeapon());
    const int printedRange = weapon ? weapon->getRange() : 1;
    const QSet<int> offsets{0, 1, printedRange - 2, printedRange - 3,
        viewer->getAttackRange() - 2, viewer->getAttackRange() - 3};
    for (const auto *target : room.getAlivePlayers()) {
        QJsonObject cards = targets.value(target->objectName()).toObject(), modes;
        for (bool distance : {true, false}) {
            QJsonObject values;
            for (int offset : offsets) values.insert(QString::number(offset), viewer->canSlash(target, card, distance, offset));
            modes.insert(distance ? "true" : "false", values);
        }
        cards.insert(key, modes); targets.insert(target->objectName(), cards);
    }
    byActor.insert(viewer->objectName(), targets); world.strategyFacts.insert("slash_checks", byActor);
}
}

namespace {
void projectCardFlags(AICardView &row, const QString &owner, ServerPlayer *viewer)
{
    row.legacyIdentityToken = aiLegacyCardIdentity(*viewer->getRoom(), viewer, row.nativeIdentity);
    row.skillNamesComplete = false;
    row.skillNames.clear();
    if (row.effectiveId < 0) return;
    const Card *card = Sanguosha->getCard(row.effectiveId);
    if (!card) return;
    row.willThrow = card->willThrow();
    const auto observation = readAIResponseCardObservation(*viewer->getRoom(), viewer, card);
    if (observation.contains("dwqingshidamage")) row.knownFlags.insert("dwqingshidamage", observation.value("dwqingshidamage"));
    row.knownFlags.insert("visible", card->hasFlag("visible"));
    if (!owner.isEmpty()) {
        const QString flag = "visible_" + viewer->objectName() + "_" + owner;
        row.knownFlags.insert(flag, card->hasFlag(flag));
    }
    // Card identity disclosure does not disclose arbitrary private card flags.
    if (owner == viewer->objectName()) {
        row.skillNames = card->getSkillNames();
        row.skillNamesComplete = true;
        row.flagsComplete = true;
        for (const QString &flag : card->getFlags())
            if (!flag.startsWith("visible_")) row.knownFlags.insert(flag, true);
    }
}
void projectPlayerCardFlags(AIPlayerView &player, ServerPlayer *viewer)
{
    for (auto *cards : {&player.equips, &player.judgingArea, &player.knownCards})
        for (auto &row : *cards) projectCardFlags(row, player.objectName, viewer);
    for (auto &pile : player.piles)
        for (auto &row : pile.cards) projectCardFlags(row, player.objectName, viewer);
}
}

void projectAIStrategyWorld(AIWorldView &world, Room &room, ServerPlayer *viewer, bool eventOnly)
{
    if (!viewer || world.self.objectName != viewer->objectName()) return;
    // Player::getSiblings follows QObject child creation order, independently
    // of the turn-rotated Room roster. Only public player names are projected.
    QJsonObject siblingOrders;
    QSet<QString> knownPlayers{world.self.objectName};
    for (const auto &row : world.players) knownPlayers.insert(row.objectName);
    for (const auto *player : room.getAllPlayers(true)) {
        QJsonArray names; bool complete = true;
        for (const auto *sibling : player->getSiblings(true)) {
            if (!sibling || !knownPlayers.contains(sibling->objectName())) { complete = false; break; }
            names.append(sibling->objectName());
        }
        if (complete) siblingOrders.insert(player->objectName(), names);
    }
    world.strategyFacts.insert("player_sibling_order", siblingOrders);
    projectAIStrategyAliases(world, room);
    world.strategyFacts.insert("role_predictable_config", Config.value("RolePredictable", false).toBool());
    projectAIStrategyKnownCards(world, room, viewer);
    projectPlayerCardFlags(world.self, viewer);
    for (auto &player : world.players) projectPlayerCardFlags(player, viewer);
    for (auto &row : world.handCards) projectCardFlags(row, viewer->objectName(), viewer);
    for (auto &row : world.discardPile) projectCardFlags(row, {}, viewer);
    QJsonObject equipSkills;
    for (const auto *equip : Sanguosha->findChildren<const EquipCard *>())
        if (Sanguosha->getSkill(equip)) equipSkills.insert(equip->objectName(), true);
    world.strategyFacts.insert("public_equip_skill_names", equipSkills);
    QJsonArray dying;
    for (const auto *player : room.getAlivePlayers())
        if (player->hasFlag("Global_Dying")) dying.append(player->objectName());
    world.strategyFacts.insert("dying_players", dying);
    projectAIStrategyFlagLifecycle(world, room);
    projectAIStrategyOwner(world, room, viewer);
    projectAIStrategyAnimecardCatalog(world, room, viewer);
    projectAIStrategySkills(world, room, viewer);
    projectAIRound53Pingjian(world, room, viewer);
    projectAIStrategyDamage(world, room, viewer);
    QJsonObject filtered, filteredHand;
    QSet<int> filterIds;
    for (const Card *card : viewer->getCards("he")) filterIds.insert(card->getEffectiveId());
    for (int id : viewer->getHandPile()) filterIds.insert(id);
    for (const AICardView &card : world.self.judgingArea) filterIds.insert(card.effectiveId);
    for (const AICardPileView &pile : world.self.piles)
        if (pile.open)
            for (const AICardView &card : pile.cards) filterIds.insert(card.effectiveId);
    for (int id : filterIds) {
        filtered.insert(QString::number(id), projectAIOwnerFilteredCard(room, viewer, id));
        filteredHand.insert(QString::number(id), projectAIOwnerFilteredCard(room, viewer, id, nullptr, int(Player::PlaceHand)));
    }
    world.strategyFacts.insert("card_filter_hand", QJsonObject{{viewer->objectName(), filteredHand}});
    world.strategyFacts.insert("card_filter", QJsonObject{{viewer->objectName(), filtered}});
    // Qiaobian evaluates public delayed tricks with the viewer as actor. Only
    // public board descriptors enter this owner rule map; hidden hands do not.
    const auto projectJudgingRules = [&](const AIPlayerView &player) {
        for (const auto &row : player.judgingArea) {
            const Card *card = Sanguosha->getCard(row.effectiveId);
            if (!card) continue;
            const QString key = "id:" + QString::number(row.effectiveId);
            QJsonObject rule = world.self.cardRules.value(key).toObject(), prohibited;
            for (const auto *target : room.getAlivePlayers())
                prohibited.insert(target->objectName(), viewer->isProhibited(target, card));
            rule.insert("prohibited", prohibited);
            world.self.cardRules.insert(key, rule);
        }
    };
    projectJudgingRules(world.self);
    for (const auto &player : world.players) projectJudgingRules(player);
    if (eventOnly) return;
    projectAIStrategySlashProbes(world, room, viewer);
    // Separate finite owner evaluation/material consumer, after the fixed
    // Slash-probe lifetime ended; never extends over the earlier world work.
    std::unique_ptr<AIRound56OwnerEvaluationsScope> ownerEvaluationsScope;
    auto *evaluationLua=room.roomRuntime()->lua().rawState();
    if(!inAIRound56ProjectionScope(room,evaluationLua))
        ownerEvaluationsScope.reset(new AIRound56OwnerEvaluationsScope(room,viewer,evaluationLua));
    int budget = 8192;
    QJsonObject evaluations;
    for (auto it = world.evaluationCards.begin(); it != world.evaluationCards.end(); ++it) {
        if(ownerEvaluationsScope)ownerEvaluationsScope->prepareFactory(it.key());
        Card *card = Sanguosha->cloneCard(it.key());
        if (!card) continue;
        // This fresh world's keys come only from the coordinator's fixed
        // six-name initializer, before later choice-specific additions.
        recordAIRound56ProjectionOwnerSourceCard(room,viewer,card,it.key());
        auto retire = qScopeGuard([card] { card->deleteLater(); });
        auto candidate = projectAIStrategyCandidate(room, viewer, card, Card::MethodUse, AIRequest::Activate, {}, budget);
        historyAvailability(candidate, card, viewer);
        evaluations.insert(it.key(), candidateJson(candidate));
        // These are fresh owner-side prototypes, so their empty skill chain,
        // flags and exact native scalar metadata are known rather than omitted.
        auto descriptor = evaluationCard(card, viewer, true);
        descriptor.insert("evaluation_rules", cardRules(room, viewer, card));
        if (it.key() == "analeptic")
            descriptor.insert("source_analeptic_available", aiRound21ChoiceAnalepticAvailable(viewer));
        it.value() = descriptor;
        projectSlash(world, room, viewer, card, "name:" + it.key());
    }
    world.strategyFacts.insert("evaluation_candidates", evaluations);
    QJsonObject dismantlementProbes;
    for (const Card *cost : viewer->getCards("he")) {
        Card *probe = Sanguosha->cloneCard("dismantlement");
        if (!probe) break;
        // The complete projection preflight proved this literal factory before
        // owner materials were selected. Record its actual native birth before
        // addSubcard or any policy query; cost comes from the same disclosed
        // owner getCards("he") source retained by that live readonly scope.
        recordAIRound56ProjectionOwnerSourceCard(room,viewer,probe,QStringLiteral("dismantlement"));
        const auto retire = qScopeGuard([probe] { probe->deleteLater(); });
        probe->addSubcard(cost);
        QJsonObject row = evaluationCard(probe, viewer, true);
        row.insert("id", -2000000 - cost->getEffectiveId());
        row.insert("effective_id", -2000000 - cost->getEffectiveId());
        row.insert("evaluation_rules", cardRules(room, viewer, probe));
        dismantlementProbes.insert(QString::number(cost->getEffectiveId()), row);
    }
    world.strategyFacts.insert("dismantlement_probes_by_cost", dismantlementProbes);
    for (const auto &row : world.handCards) {
        const Card *card = Sanguosha->getCard(row.effectiveId);
        projectSlash(world, room, viewer, card, "id:" + QString::number(row.effectiveId));
    }
}

void projectAIStrategyFanProbe(AIWorldView &world, Room &room, ServerPlayer *viewer, const Card *source)
{
    if (!viewer || world.self.objectName != viewer->objectName() || !source) return;
    // Called only for the owning Fan invoke's declared CardUse. Match donor
    // dummyCard("fire_slash") + addSubcard(use.card), including virtual costs.
    // This hypothetical result is not an activation/conversion capability.
    Card *probe = Sanguosha->cloneCard("fire_slash", Card::NoSuit, 0);
    if (!probe) return;
    const auto retire = qScopeGuard([probe] { probe->deleteLater(); });
    probe->setSkillName("fan");
    probe->addSubcard(source);
    QJsonObject row = evaluationCard(probe, viewer, true);
    row.insert("id", -4100000); row.insert("effective_id", -4100000);
    row.insert("evaluation_rules", cardRules(room, viewer, probe));
    row.insert("target_mod_distance", Sanguosha->correctCardTarget(TargetModSkill::DistanceLimit, viewer, probe));
    row.insert("target_mod_extra", Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, viewer, probe));
    QJsonObject sourceIdentity{{"name", source->objectName()}, {"class_name", source->getClassName()},
        {"suit", int(source->getSuit())}, {"number", source->getNumber()}};
    world.strategyFacts.insert("fan_conversion_probe", QJsonObject{{"source", sourceIdentity}, {"card", row}});
}

void projectAIStrategyDisclosedFilters(AIWorldView &world, Room &room, ServerPlayer *viewer,
                                       const QList<AICardView> &disclosed)
{
    if (!viewer || world.self.objectName != viewer->objectName()) return;
    // Both AG's hypothetical acquisition (Hand) and Guanxing/retrial's
    // hypothetical judgment (Judge) use explicitly disclosed request cards.
    // Never turn choiceOptions.cardIds alone into identities: those may name
    // another player's concealed candidates.
    QJsonObject filterActors = world.strategyFacts.value("card_filter_hand").toObject();
    QJsonObject acquiredFilters = filterActors.value(viewer->objectName()).toObject();
    QJsonObject judgeActors = world.strategyFacts.value("card_filter").toObject();
    QJsonObject judgeFilters = judgeActors.value(viewer->objectName()).toObject();
    for (const auto &offered : disclosed) {
        const QString key = QString::number(offered.effectiveId);
        acquiredFilters.insert(key,
            projectAIOwnerFilteredCard(room, viewer, offered.effectiveId, nullptr, int(Player::PlaceHand), &offered));
        judgeFilters.insert(key,
            projectAIOwnerFilteredCard(room, viewer, offered.effectiveId, nullptr, int(Player::PlaceJudge), &offered));
    }
    filterActors.insert(viewer->objectName(), acquiredFilters);
    judgeActors.insert(viewer->objectName(), judgeFilters);
    world.strategyFacts.insert("card_filter_hand", filterActors);
    world.strategyFacts.insert("card_filter", judgeActors);
}

void projectAIStrategyRequest(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    if (!viewer || viewer->objectName() != request.viewerObjectName) return;
    projectAIUseCardDispatch(request, room, viewer);
    if (request.kind == AIRequest::UseCard
        && request.pattern == "@@_shuibojian"
        && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        const qint64 executionId = viewer->getTag("ShuibojianExecution").toLongLong();
        const SkillContext parent = room.getSkillExecutionContext(executionId);
        if (parent.owner == viewer && parent.skill_name == "_shuibojian" && parent.original_data) {
            const CardUseStruct use = parent.original_data->value<CardUseStruct>();
            if (use.card && use.from == viewer) {
                QJsonArray targets;
                for (const auto *target : use.to) if (target) targets.append(target->objectName());
                request.choiceOptions.context.insert("shuibojian_use", QJsonObject{
                    {"from", viewer->objectName()}, {"to", targets}, {"card", evaluationCard(use.card, viewer)}});
                QJsonObject candidates;
                const QVariantMap values = viewer->property("shuibojian_candidates").toMap();
                for (auto it = values.cbegin(); it != values.cend(); ++it) {
                    if (!room.findPlayerByObjectName(it.key(), true)) continue;
                    QJsonArray victims;
                    for (const QString &name : it.value().toStringList())
                        if (room.findPlayerByObjectName(name, true)) victims.append(name);
                    candidates.insert(it.key(), victims);
                }
                request.choiceOptions.context.insert("shuibojian_candidates", candidates);
                request.choiceOptions.context.insert("shuibojian_maximum", viewer->property("shuibojian_maximum").toInt());
            }
        }
    }
    for (auto &row : request.choiceOptions.cards) {
        const Player *owner = room.getCardOwner(row.effectiveId);
        projectCardFlags(row, owner ? owner->objectName() : QString(), viewer);
    }
    if (request.kind == AIRequest::PlayerChosen && request.choiceOptions.reason == "qiaobian") {
        const auto *target = room.getTag("QiaobianTarget").value<ServerPlayer *>();
        request.choiceOptions.context.insert("qiaobian_target", target && room.getAllPlayers(true).contains(const_cast<ServerPlayer *>(target))
            ? QJsonValue(target->objectName()) : QJsonValue(false));
    }
    // These transient ForAI flags identify publicly declared skill targets.
    // Mingce logs #CollateralSlash before its choice; Dimeng recipients are
    // public declared targets before the hand exchange triggers Luoying.
    const auto auditedLuoyingAlias = [&] {
        const QString reason = request.choiceOptions.reason;
        if (reason != "shenxian" && reason != "yinli") return false;
        const auto aliases = request.worldView.strategyFacts.value("audited_aliases").toObject();
        const auto row = aliases.value("entries").toObject()
            .value("ai_skill_invoke." + reason).toObject();
        return aliases.value("captured").toBool() && row.value("known").toBool()
            && row.value("present").toBool() && row.value("enabled").toBool()
            && row.value("matched").toBool() && row.value("source_matches").toBool()
            && row.value("canonical_registry").toString() == "ai_skill_invoke"
            && row.value("canonical_path").toArray() == QJsonArray{QStringLiteral("luoying")};
    };
    if ((request.kind == AIRequest::Choice && request.choiceOptions.reason == "mingce")
        || (request.kind == AIRequest::SkillInvoke
            && (request.choiceOptions.reason == "luoying" || auditedLuoyingAlias()))) {
        const bool mingce = request.choiceOptions.reason == "mingce";
        QJsonArray targets;
        for (const ServerPlayer *player : room.getAllPlayers(true))
            if (player && player->hasFlag(mingce ? "MingceTarget" : "DimengTarget")) targets.append(player->objectName());
        request.choiceOptions.context.insert(mingce ? "mingce_targets" : "dimeng_targets", targets);
    }
    if (request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "guzheng")
        request.choiceOptions.context.insert("guzheng_to_get_count", viewer->getTag("GuzhengToGet").toList().size());
    if (request.kind == AIRequest::Choice && request.choiceOptions.reason == "nosguhuo") {
        // The fixed skill broadcasts #Guhuo with this declaration before
        // questioning any player. The concealed material is never projected.
        QJsonObject standard = request.worldView.strategyFacts.value("standard").toObject();
        standard.insert("nosguhuo_type", room.getTag("NosGuhuoType").toString());
        request.worldView.strategyFacts.insert("standard", standard);
    }
    if (request.kind == AIRequest::Choice && request.choiceOptions.reason == "huashen") {
        QJsonObject skills;
        for (const QString &name : request.choiceOptions.choices) {
            const Skill *skill = Sanguosha->getSkill(name);
            const ViewAsSkill *viewAs = Sanguosha->getViewAsSkill(name);
            const auto *trigger = dynamic_cast<const TriggerSkill *>(skill);
            QJsonObject row{{"exists", skill != nullptr}, {"view_as", viewAs != nullptr},
                {"trigger", trigger != nullptr}, {"has_cards_move", trigger && trigger->getTriggerEvents().contains(CardsMoveOneTime)}};
            if (skill) row.insert("description", skill->getDescription());
            if (viewAs) row.insert("enabled_at_play", viewAs->isEnabledAtPlay(viewer));
            skills.insert(name, row);
        }
        request.choiceOptions.context.insert("huashen_skills", skills);
    }
    const QJsonValue extraSlash = request.worldView.strategyFacts.value("extra_target_slash");
    if (!extraSlash.isUndefined()) request.choiceOptions.context.insert("extra_target_slash", extraSlash);
    if (request.choiceOptions.reason == "gongxin") {
        const auto *target = viewer->getTag("gongxin").value<ServerPlayer *>();
        if (target && room.getAllPlayers(true).contains(const_cast<ServerPlayer *>(target)))
            request.choiceOptions.context.insert("gongxin_target", target->objectName());
        else request.choiceOptions.context.insert("gongxin_target", false);
        QJsonObject printed;
        for (const auto &offered : request.choiceOptions.cards) {
            const Card *card = Sanguosha->getEngineCard(offered.effectiveId);
            if (!card) continue;
            // Only immutable faces of already disclosed offered IDs. No live
            // wrapper flags, skill identities, private tags, or hidden cards.
            printed.insert(QString::number(offered.effectiveId), QJsonObject{
                {"id", offered.effectiveId}, {"effective_id", offered.effectiveId},
                {"name", card->objectName()}, {"class_name", card->getClassName()}, {"subtype", card->getSubtype()},
                {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
                {"suit", int(card->getSuit())}, {"number", card->getNumber()},
                {"type_id", int(card->getTypeId())}, {"red", card->isRed()}, {"black", card->isBlack()},
                {"legacy_string_key", QString::number(offered.effectiveId)}, {"legacy_sort_key", QString::number(offered.effectiveId)},
                {"virtual_card", false}, {"subcards", QJsonArray()}});
        }
        request.choiceOptions.context.insert("gongxin_engine_cards", printed);
    }
    projectAIStrategyYCZH2016(request, room, viewer);
    projectAIStrategyRandomRequest(request, room, viewer);
    projectAIStrategyRandomPlanningRequest(request, room, viewer);
    for (auto &candidate : request.cardCandidates)
        historyAvailability(candidate, Sanguosha->getCard(candidate.cardId), viewer);
    projectAIStrategyDisclosedFilters(request.worldView, room, viewer, request.choiceOptions.cards);
    QList<AICardView> acquiredCards;
    if (request.kind == AIRequest::AmazingGrace || request.kind == AIRequest::Guanxing)
        acquiredCards = request.choiceOptions.cards;
    else if (request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "taoxi") {
        // The owner is deciding whether to borrow from this declared-use target.
        // Evaluate only faces already present in this viewer's lawful snapshot;
        // a tag or a guessed physical ID does not reveal another hand.
        const auto use = request.worldView.strategyFacts.value("yjcm2015").toObject().value("taoxi_use").toObject();
        const auto targets = use.value("to").toArray();
        if (use.value("from").toString() == viewer->objectName() && !targets.isEmpty()) {
            for (const auto &other : request.worldView.players)
                if (other.objectName == targets.first().toString()) acquiredCards = other.knownCards;
            projectAIStrategyDisclosedFilters(request.worldView, room, viewer, acquiredCards);
        }
    }
    else if (request.kind == AIRequest::Choice && request.choiceOptions.reason == "eastfast") {
        const auto context = request.choiceOptions.context.value("eastfast").toObject();
        const auto row = context.value("card").toObject();
        if (context.value("known").toBool() && row.value("effective_id").isDouble()
            && row.value("effective_id").toInt(-1) >= 0 && row.value("name").isString()
            && row.value("class_name").isString() && row.value("suit").isDouble()
            && row.value("number").isDouble() && row.value("virtual_card").isBool()
            && !row.value("virtual_card").toBool()) {
            AICardView card;
            card.cardId = card.effectiveId = row.value("effective_id").toInt();
            card.objectName = row.value("name").toString(); card.className = row.value("class_name").toString();
            card.suit = row.value("suit").toInt(); card.number = row.value("number").toInt();
            card.skillName = row.value("skill_name").toString(); card.knownFlags = row.value("known_flags").toObject();
            acquiredCards.append(card);
            projectAIStrategyDisclosedFilters(request.worldView, room, viewer, acquiredCards);
        }
    }
    if (acquiredCards.isEmpty()) return;
    int budget = 8192; QJsonObject rows;
    for (const auto &offered : acquiredCards) {
        // Hypothetical acquisition evaluates the offered face, not another
        // location's private wrapped flags or skill identity.
        std::unique_ptr<Card> disclosedCard;
        const Card *card = nullptr;
        if (request.kind == AIRequest::AmazingGrace && request.choiceOptions.reason == "shouxi"
            && request.choiceOptions.cardIds.contains(offered.effectiveId)) {
            // Fixed Shouxi asks getEngineCard for each already offered ID.
            // Its printed definition is independent of any live owner filter.
            card = Sanguosha->getEngineCard(offered.effectiveId);
        } else if (room.getCardOwner(offered.effectiveId) == viewer) card = Sanguosha->getCard(offered.effectiveId);
        else if (offered.effectiveId >= 0 && !offered.virtualCard) {
            QStringList flags;
            for (auto it = offered.knownFlags.constBegin(); it != offered.knownFlags.constEnd(); ++it)
                if (it.value().toBool()) flags.append(it.key());
            disclosedCard.reset(Sanguosha->cloneCard(offered.objectName, Card::Suit(offered.suit), offered.number, flags));
            if (!disclosedCard || disclosedCard->getClassName() != offered.className) continue;
            disclosedCard->setId(offered.effectiveId);
            disclosedCard->setSkillName(offered.skillName);
            card = disclosedCard.get();
        }
        if (!card) continue;
        auto candidate = projectAIStrategyCandidate(room, viewer, card, Card::MethodUse, AIRequest::Activate, {}, budget);
        historyAvailability(candidate, card, viewer);
        QJsonObject acquired{{"evaluation_only", true},
            {"card", evaluationCard(card, viewer)}, {"candidate", candidateJson(candidate)},
            {"card_rules", cardRules(room, viewer, card)}};
        // These two source AG bodies plan getCard(id), not our hypothetical
        // acquisition clone. Keep that clone/profile identity unchanged. Only
        // a separate current offered-object witness can save the source Card.
        if (request.kind == AIRequest::AmazingGrace
            && (request.choiceOptions.reason == "kezumingjie" || request.choiceOptions.reason == "kezulilun")
            && request.choiceOptions.cardIds.contains(offered.effectiveId)) {
            const Card *actual = Sanguosha->getCard(offered.effectiveId);
            const auto token = offered.nativeIdentity;
            if (actual && dynamic_cast<const WrappedCard *>(actual) && token
                && globalCardLifetimeManager().isLive(token) && token->address == actual
                && offered.cardId == actual->getId() && offered.effectiveId == actual->getEffectiveId()
                && !offered.virtualCard && !actual->isVirtualCard()
                && offered.objectName == actual->objectName() && offered.className == actual->getClassName()
                && offered.suit == int(actual->getSuit()) && offered.number == actual->getNumber()
                && offered.skillName == actual->getSkillName(false) && offered.subcardIds == actual->getSubcards()
                && offered.canRecast == actual->canRecast()) {
                const QString identity = aiLegacyCardIdentity(room, viewer, token);
                if (!identity.isEmpty() && identity == offered.legacyIdentityToken)
                    acquired.insert("observed_source", QJsonObject{{"physical_id", offered.effectiveId},
                        {"legacy_identity_token", identity}});
            }
        }
        rows.insert(QString::number(offered.effectiveId), acquired);
        projectSlash(request.worldView, room, viewer, card, "id:" + QString::number(offered.effectiveId));
    }
    request.choiceOptions.context.insert("acquired_card_evaluations", rows);
}

QJsonObject projectAIOwnerFilteredCard(Room &room, ServerPlayer *viewer, int id, const JudgeStruct *judge, int hypotheticalPlace, const AICardView *disclosed)
{
    if (!viewer) return {};
    const bool owns = room.getCardOwner(id) == viewer;
    if (!owns && (!disclosed || disclosed->effectiveId != id || id < 0)) return {};
    std::unique_ptr<Card> publicInput;
    const Card *original = nullptr;
    if (owns) original = Sanguosha->getCard(id);
    else {
        // Hypothetical acquisition starts from the exact disclosed descriptor,
        // not arbitrary private flags/source tags of the current native wrapper.
        QStringList flags;
        for (auto it = disclosed->knownFlags.constBegin(); it != disclosed->knownFlags.constEnd(); ++it)
            if (it.value().toBool()) flags.append(it.key());
        publicInput.reset(Sanguosha->cloneCard(disclosed->objectName, Card::Suit(disclosed->suit), disclosed->number, flags));
        if (!publicInput || publicInput->getClassName() != disclosed->className) return {};
        publicInput->setId(id); publicInput->setSkillName(disclosed->skillName);
        original = publicInput.get();
    }
    if (!original) return {};
    QList<const FilterSkill *> filters;
    for (const Skill *skill : viewer->getSkillList(true)) {
        if (skill->isLordSkill() ? !([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,viewer,gameLua));
        return viewer->hasLordSkill(skill); }()) : !([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,viewer,gameLua));
        return viewer->hasSkill(skill); }())) continue;
        if (const auto *filter = dynamic_cast<const FilterSkill *>(Sanguosha->getViewAsSkill(skill->objectName()))) filters.append(filter);
    }
    // sgs.getPlayerSkillList appends the lord's visible lord skills for Weidi,
    // without deduplication or rechecking them as the viewer's owned instances.
    const ServerPlayer *lord = room.getLord();
    if (([&] { std::unique_ptr<AIRound56WorldSkillQueriesScope> source;
        auto *gameLua=room.roomRuntime()->lua().rawState();
        if(!inAIRound56ProjectionScope(room,gameLua))
            source.reset(new AIRound56WorldSkillQueriesScope(room,viewer,gameLua));
        return viewer->hasSkill("weidi"); }()) && lord && lord != viewer) {
        for (const Skill *skill : lord->getSkillList(true))
            if (skill->isLordSkill())
                if (const auto *filter = dynamic_cast<const FilterSkill *>(Sanguosha->getViewAsSkill(skill->objectName()))) filters.append(filter);
    }
    // These callback bodies were audited at the pinned engine/extensions SHAs:
    // they only construct cards or mutate their selected WrappedCard. Unknown
    // providers must not be granted a live Room mutation channel by a name API.
    static const QSet<QString> audited{
        QStringLiteral("#MeowHongyan"),
        QStringLiteral("#ZhuohunBf"),
        QStringLiteral("#htms_godotfilter"),
        QStringLiteral("#huanmeng-filter"),
        QStringLiteral("#japenknifeslashe2"),
        QStringLiteral("#kejiexianjuaoex"),
        QStringLiteral("#kejiguaner"),
        QStringLiteral("#kejinxguner"),
        QStringLiteral("#linguangfilter"),
        QStringLiteral("#mankongzhibf"),
        QStringLiteral("#meibu-filter"),
        QStringLiteral("#olmeibu-filter"), QStringLiteral("#olzhixi-filter"),
        QStringLiteral("#mouhongyan_vsc"),
        QStringLiteral("#myjl-filter"),
        QStringLiteral("#ny_tenth_jvewu_change"),
        QStringLiteral("#ofhongyuanbf"),
        QStringLiteral("#qhwindwushenFilter"),
        QStringLiteral("#se_mingqie_filter"),
        QStringLiteral("#sfofl_shuangrenFilter"),
        QStringLiteral("#tiequanf"),
        QStringLiteral("#xuanguangfilter"),
        QStringLiteral("#yuanjue1"),
        QStringLiteral("#zhizhe"),
        QStringLiteral("LuaKuangshi"),
        QStringLiteral("SevenBeiJie"),
        QStringLiteral("SixGuicaiFilter"),
        QStringLiteral("TH_PerfectMath"),
        QStringLiteral("TH_ScarletDestiny"),
        QStringLiteral("bloodfight"),
        QStringLiteral("chihun"),
        QStringLiteral("duqu"),
        QStringLiteral("eatforlike"),
        QStringLiteral("fcj_longnu_yang"),
        QStringLiteral("fcj_longnu_yin"),
        QStringLiteral("fczhizhe_vs"),
        QStringLiteral("fuhei"),
        QStringLiteral("fuzou_filter"),
        QStringLiteral("guandu_quanjiu"),
        // zombie-scenario.cpp:296; equip-outside-slot -> local IronChain.
        QStringLiteral("ganran"),
        QStringLiteral("heixing"),
        QStringLiteral("hongyan"),
        QStringLiteral("japenknifeslashe"),
        QStringLiteral("jghupo"),
        QStringLiteral("jinjiu"),
        QStringLiteral("jjjuanjia"),
        QStringLiteral("kechengduxingex"),
        QStringLiteral("keolweilin_black"),
        QStringLiteral("keolweilin_red"),
        QStringLiteral("kezhuanninghanbuff"),
        QStringLiteral("krskitgzhanyue"),
        QStringLiteral("leij"),
        QStringLiteral("luajuesi"),
        QStringLiteral("luaqilin"),
        QStringLiteral("luayudun"),
        QStringLiteral("lxtx_jinjiu"),
        QStringLiteral("meizlkuijiu"),
        QStringLiteral("melee_peach_filter"),
        QStringLiteral("mingchuan"),
        QStringLiteral("mobilejinjiu"),
        QStringLiteral("mobilexinxianghai"),
        QStringLiteral("ny_10th_jiaoxia_filter"),
        QStringLiteral("olhongyan"),
        QStringLiteral("olwushen"),
        QStringLiteral("qishi"),
        QStringLiteral("ruyi"),
        QStringLiteral("s2_benxi"),
        QStringLiteral("s2_feiyan"),
        QStringLiteral("s2_haozhan"),
        QStringLiteral("s2_hengchong"),
        QStringLiteral("s2_qiangji"),
        QStringLiteral("s2_shanxianOther"),
        QStringLiteral("s4_pinglu"),
        QStringLiteral("s4_s_liaoshang"),
        QStringLiteral("s_w_tuyong"),
        QStringLiteral("secondtenyearjinjiu"),
        QStringLiteral("secondtenyearshizhi"),
        QStringLiteral("sfofl_xuezhan"),
        QStringLiteral("sgkgodwushen"),
        QStringLiteral("sgskanxueSlash"),
        QStringLiteral("shimaZHCMT"),
        QStringLiteral("shizhi"),
        QStringLiteral("sxwuyan"),
        QStringLiteral("tc_wumou"),
        QStringLiteral("wushen"),
        QStringLiteral("xdbaohe"),
        QStringLiteral("xunshi"),
        QStringLiteral("ys_xionchijink"),
        QStringLiteral("zb_ganran")
    };
    std::unique_ptr<Card> result(Sanguosha->cloneCard(original));
    if (!result) return {};
    bool transformed = false;
    QString repair;
    for (const auto *filter : filters) {
        if (room.getCardPlace(id) == Player::PlaceEquip) break;
        if (!owns) {
            const QString name = filter->objectName();
            const QMap<QString, QString> flagReads{{"#ny_tenth_jvewu_change", "ny_tenth_jvewu"},
                {"keolweilin_red", "keolweilin"}, {"keolweilin_black", "keolweilin"},
                };
            const bool readsFlag = flagReads.contains(name)
                && (name != "keolweilin_red" || original->isRed())
                && (name != "keolweilin_black" || !original->isRed());
            if (name == "#zhizhe" || (name == "fczhizhe_vs"
                && (original->objectName() == "_fczhizhe_basic" || original->objectName() == "_fczhizhe_trick"))
                || (name == "SixGuicaiFilter" && hypotheticalPlace == int(Player::PlaceJudge))
                || (readsFlag && !disclosed->flagsComplete
                    && !disclosed->knownFlags.contains(flagReads.value(name))))
                return {{"complete", false}, {"unsupported_filter", "undisclosed_card_state:" + name}};
        }
        if (filter->objectName() == QStringLiteral("luaqilin") && !room.getCurrent())
            return {{"complete", false}, {"unsupported_filter", "luaqilin_no_current"}};
        const bool repairDouba = filter->objectName() == QStringLiteral("s2_douba");
        if (repairDouba && !room.getCurrent()) return {{"complete", false}, {"unsupported_filter", "s2_douba_no_current"}};
        if (!repairDouba && !audited.contains(filter->objectName()))
            return {{"complete", false}, {"unsupported_filter", filter->objectName()}};
        Card *input = Sanguosha->cloneCard(original);
        if (!input) return {};
        input->setId(id);
        WrappedCard shadow(input);
        AICardQueryScope mapping(&room, room.roomState(), id, &shadow, viewer, hypotheticalPlace);
        Player *oldSelf = QSanEngine::Self;
        Player *oldPlayer = room.roomState()->getCurrentPlayer();
        setEngineSelf(viewer); room.roomState()->setCurrentPlayer(viewer);
        auto restore = qScopeGuard([&] { setEngineSelf(oldSelf); room.roomState()->setCurrentPlayer(oldPlayer); });
        if (!filter->viewFilter(&shadow)) continue;
        if (filter->objectName() == QStringLiteral("#zhizhe")
            && room.getTag("ZhizheFilter_" + shadow.toString()).toString().split('+').size() < 3)
            return {{"complete", false}, {"unsupported_filter", "zhizhe_malformed_native_descriptor"}};
        const Card *converted = nullptr;
        if (repairDouba) {
            // Donor assigns a card to its global skill variable. Preserve the
            // red/black result with a local clone; never corrupt rules globals.
            if (!shadow.isRed() && !shadow.isBlack())
                return {{"complete", false}, {"unsupported_filter", "s2_douba_colorless_uninitialized_card"}};
            Card *local = Sanguosha->cloneCard(shadow.isRed() ? "slash" : "duel", shadow.getSuit(), shadow.getNumber());
            if (!local) return {};
            local->setSkillName(filter->objectName()); converted = local;
            repair = "s2_douba_global_card_assignment_replaced_with_local";
        } else converted = filter->viewAs(&shadow);
        if (!converted) continue;
        Card *copy = Sanguosha->cloneCard(converted);
        if (converted != &shadow && converted->isVirtualCard()) const_cast<Card *>(converted)->deleteLater();
        if (!copy) return {};
        copy->setId(-1); result.reset(copy); transformed = true;
    }
    const Card *card = result.get();
    QJsonArray disclosedFlags;
    for (const QString &flag : card->getFlags()) if (!flag.startsWith("visible_")) disclosedFlags.append(flag);
    QJsonObject dto{{"id", transformed ? -1 : id}, {"effective_id", transformed ? -1 : id},
        {"legacy_string_key", card->toString()}, {"legacy_sort_key", strategyCardSortKey(card)},
        {"name", card->objectName()}, {"class_name", card->getClassName()}, {"subtype", card->getSubtype()},
        {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
        {"suit", int(card->getSuit())}, {"number", card->getNumber()}, {"type_id", int(card->getTypeId())},
        {"skill_name", card->getSkillName(false)}, {"red", card->isRed()}, {"black", card->isBlack()},
        {"virtual_card", transformed}, {"subcards", QJsonArray()}, {"flags", disclosedFlags}, {"flags_complete_scope", "owner_non_visibility"},
        {"skill_names_complete", false},
        {"single_target", card->isSingleTargetCard()}, {"damage_card", card->isDamageCard()}, {"target_fixed", card->targetFixed()},
        {"handling_method", int(card->getHandlingMethod())}, {"will_throw", card->willThrow()}, {"can_recast", card->canRecast()}};
    QJsonObject projected{{"complete", true}, {"card", dto}};
    if (judge) projected.insert("judge_outcome", judge->isGood(card));
    if (!repair.isEmpty()) projected.insert("donor_error_repair", repair);
    return projected;
}
