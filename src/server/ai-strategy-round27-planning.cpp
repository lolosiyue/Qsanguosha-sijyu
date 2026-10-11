#include "ai-strategy-round27-planning.h"
#include "ai-strategy-card-identity.h"
#include "ai-strategy-projection.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include "structs.h"
#include <QJsonArray>

namespace {
bool absent(const QVariant &v) { return !v.isValid() || v.isNull(); }
bool sameFace(const AICardView &v, const Card *c) {
    return c && !c->isVirtualCard() && v.effectiveId == c->getEffectiveId()
        && v.objectName == c->objectName() && v.className == c->getClassName()
        && v.suit == int(c->getSuit()) && v.number == c->getNumber();
}
bool noKnownFilters(const AIRequest &r, const ServerPlayer *p) {
    const AIPlayerView *v = p->objectName() == r.worldView.self.objectName ? &r.worldView.self : nullptr;
    if (!v) for (const auto &row : r.worldView.players)
        if (row.objectName == p->objectName()) { v = &row; break; }
    if (!v || !v->skillsComplete) return false;
    for (const auto &s : v->skills)
        if (!s.invalid && dynamic_cast<const FilterSkill *>(Sanguosha->getViewAsSkill(s.skillName))) return false;
    return true;
}
bool publicPattern(const QString &pattern) {
    if (pattern.contains('$')) return false;
    for (const auto &branch : pattern.split('#')) {
        const auto factors = branch.split('|');
        if (factors.size() >= 4 && factors.at(3) != ".") return false;
    }
    return true;
}
}

void projectAIRound27PlanningContext(AIRequest &r, Room &room, const ServerPlayer *observer,
    const QVariant &data, QJsonObject &context, const std::function<QJsonObject(const Card *)> &face,
    const std::function<QJsonObject(const Card *)> &judgeProfile) {
    if (!observer || observer->getRoom() != &room || r.viewerObjectName != observer->objectName()
        || r.stateRevision != room.roomRuntime()->stateRevision() || r.choiceOptions.reason != "fcj_huanshi") return;
    const bool invoke = r.kind == AIRequest::SkillInvoke;
    const bool ag = r.kind == AIRequest::AmazingGrace;
    if (!invoke && !ag) return;
    auto *viewer = const_cast<ServerPlayer *>(observer);
    JudgeStruct *judge = nullptr;
    ServerPlayer *source = nullptr;
    auto fc = context.value("fcDIY").toObject();
    if (invoke) {
        // First makeChoiceRequest has no originalData yet. Do not manufacture
        // absent judge or erase the later projectDecisionContext result.
        if (!viewer->hasSkill("fcj_huanshi") || !data.canConvert<JudgeStruct *>()) return;
        judge = data.value<JudgeStruct *>();
        source = viewer;
    } else {
        const auto value = viewer->getTag("fcj_huanshiJudge");
        const auto from = viewer->getTag("fcj_huanshiFrom");
        if (absent(value)) fc.insert("fcj_huanshiJudge", false);
        if (absent(from)) fc.insert("fcj_huanshiFrom", false);
        context.insert("fcDIY", fc);
        if (!value.canConvert<JudgeStruct *>() || !from.canConvert<ServerPlayer *>()) return;
        judge = value.value<JudgeStruct *>(); source = from.value<ServerPlayer *>();
        // Native asks judge.who, not necessarily the Huanshi skill owner.
        if (!judge || judge->who != viewer || !source || !room.getAllPlayers(true).contains(source)
            || !source->hasSkill("fcj_huanshi")) return;
    }
    if (!judge || !judge->who || !judge->card || !face || !room.getAllPlayers(true).contains(judge->who)) return;
    const auto material = source->getCards("he");
    QList<AICardView> disclosed;
    if (invoke) {
        // Snapshot ownership and exact current face are both required. Preserve
        // all HE cards, INCLUDING Response-limited cards examined by source4147.
        for (const Card *c : material) {
            const AICardView *v = nullptr;
            for (const auto &row : r.worldView.handCards) if (sameFace(row, c)) { v = &row; break; }
            if (!v) for (const auto &row : r.worldView.self.equips) if (sameFace(row, c)) { v = &row; break; }
            if (!v || room.getCardOwner(c->getEffectiveId()) != viewer) return;
            disclosed << *v;
        }
    } else {
        if (!r.choiceOptions.candidatesComplete) return;
        for (int id : r.choiceOptions.cardIds) {
            const Card *c = Sanguosha->getCard(id);
            bool inHE = false; for (const Card *m : material) if (m && m->getEffectiveId() == id) { inHE = true; break; }
            if (!c || room.getCardOwner(id) != source || !inHE) return;
            const AICardView *v = nullptr;
            for (const auto &row : r.choiceOptions.cards) if (row.effectiveId == id && sameFace(row, c)) { v = &row; break; }
            if (!v) return;
            disclosed << *v;
        }
    }
    if (ag) {
        // Fixed smart-ai5463 calls doDisCard(self.player, offeredId), whose
        // first query is player:canDiscard(player,id). Player.cpp1726 does
        // NOT require that id to belong to player: it tests the viewer's own
        // MethodDiscard restriction against that exact disclosed card.
        // Preserve that query, without moving the card or authorizing payment.
        auto permissions = r.worldView.self.canDiscard.value(viewer->objectName()).toObject();
        for (const auto &v : disclosed)
            permissions.insert(QString::number(v.effectiveId), viewer->canDiscard(viewer, v.effectiveId));
        r.worldView.self.canDiscard.insert(viewer->objectName(), permissions);
    }
    AIIdentityProjectionScope identity(room, viewer);
    const bool rawComplete = publicPattern(judge->pattern);
    bool filteredComplete = rawComplete && (judge->who == viewer || noKnownFilters(r, judge->who));
    QJsonObject raw, filtered, suits, numbers, repairs, candidates, filterRows, handRows;
    auto filters = r.worldView.strategyFacts.value("card_filter").toObject();
    auto handFilters = r.worldView.strategyFacts.value("card_filter_hand").toObject();
    filterRows = filters.value(viewer->objectName()).toObject();
    handRows = handFilters.value(viewer->objectName()).toObject();
    for (const auto &v : disclosed) {
        const int id = v.effectiveId;
        const QString key = QString::number(id);
        const Card *c = Sanguosha->getCard(id);
        auto candidateFace = invoke && judgeProfile ? judgeProfile(c) : face(c);
        if (invoke && judgeProfile) {
            // getCards("he") projects realEquip(inner), while fixed source4143
            // subsequently fetches Sanguosha:getCard(id), the WrappedCard outer.
            // Authenticate the observed material separately; do not merge their
            // native identities or substitute the inner object for source use.
            const QString materialIdentity = v.legacyIdentityToken.isEmpty()
                ? aiLegacyCardIdentity(room, viewer, v.nativeIdentity) : v.legacyIdentityToken;
            if (!materialIdentity.isEmpty()) candidateFace.insert("material_identity_token", materialIdentity);
        }
        candidates.insert(key, candidateFace);
        if (rawComplete) raw.insert(key, judge->isGood(c));
        if (judge->who == viewer) {
            const auto row = projectAIOwnerFilteredCard(room, viewer, id, rawComplete ? judge : nullptr,
                int(Player::PlaceJudge), &v);
            filterRows.insert(key, row);
            const auto printed = row.value("card").toObject();
            if (!row.value("complete").toBool() || !row.value("judge_outcome").isBool()) filteredComplete = false;
            else {
                filtered.insert(key, row.value("judge_outcome"));
                suits.insert(key, printed.value("suit")); numbers.insert(key, printed.value("number"));
                if (row.contains("donor_error_repair")) repairs.insert(key, row.value("donor_error_repair"));
            }
        } else if (filteredComplete) {
            filtered.insert(key, raw.value(key)); suits.insert(key, int(c->getSuit())); numbers.insert(key, c->getNumber());
        }
        // Beige's isCard(Peach,c,self.player) is a hypothetical classification
        // for AG. Reuse the existing audited disclosed-face, hand-place filter.
        handRows.insert(key, projectAIOwnerFilteredCard(room, viewer, id, nullptr, int(Player::PlaceHand), &v));
    }
    filters.insert(viewer->objectName(), filterRows); handFilters.insert(viewer->objectName(), handRows);
    r.worldView.strategyFacts.insert("card_filter", filters);
    r.worldView.strategyFacts.insert("card_filter_hand", handFilters);
    int holders = 0; bool reserveComplete = true; QJsonObject reserve;
    if (judge->reason != "lightning") for (auto *p : room.getAllPlayers()) if (p->containsTrick("lightning")) {
        ++holders;
        if (!noKnownFilters(r, p)) reserveComplete = false;
    }
    if (reserveComplete && holders > 0) for (const auto &v : disclosed)
        if (v.suit == int(Card::Spade) && v.number >= 2 && v.number <= 9) reserve.insert(QString::number(v.effectiveId), true);
    if (!filteredComplete) filtered = {};
    QJsonObject row{{"scope_key", "fcj_huanshi"}, {"who", judge->who->objectName()}, {"reason", judge->reason},
        {"pattern", judge->pattern}, {"good", judge->isGood()}, {"predicate_good", judge->good}, {"negative", judge->negative},
        {"card", judgeProfile ? judgeProfile(judge->card) : face(judge->card)}, {"raw_outcome_by_id", raw}, {"raw_outcomes_complete", rawComplete},
        {"candidate_faces", candidates}, {"outcome_by_id", filtered}, {"outcomes_complete", filteredComplete},
        {"unfiltered_outcome_by_id", raw}, {"unfiltered_outcomes_complete", rawComplete},
        {"filtered_suit_by_id", suits}, {"filtered_number_by_id", numbers}, {"filtered_complete", filteredComplete},
        {"repair_by_id", repairs}, {"lightning_holder_count", holders}, {"lightning_reserve_complete", reserveComplete},
        {"lightning_candidate_ids", reserve}};
    if (judge->reason.contains("beige")) {
        // smart-ai5409 reads only CurrentDamageStruct.from. A missing tag
        // converts to the source's empty DamageStruct; do not substitute a
        // previous damage event or disclose its card/private fields.
        const auto current = room.getTag("CurrentDamageStruct");
        if (absent(current)) {
            row.insert("damage_context_complete", true); row.insert("damage_from", "");
        } else if (current.canConvert<DamageStruct>()) {
            const auto damage = current.value<DamageStruct>();
            if (!damage.from || room.getAllPlayers(true).contains(damage.from)) {
                row.insert("damage_context_complete", true);
                row.insert("damage_from", damage.from ? damage.from->objectName() : QString());
            }
        }
    }
    fc.insert("fcj_huanshiJudge", row); fc.insert("fcj_huanshiFrom", source->objectName());
    context.insert("fcDIY", fc);
    // Existing retrial helpers consume this same typed judge/filter projection.
    context.insert("judge", row);
}
