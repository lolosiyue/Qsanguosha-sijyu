#include "qt-collection-utils.h"
#include "ai-strategy-judge-context.h"
#include "ai-strategy-round30-original.h"
#include "ai-strategy-projection.h"
#include "ai.h"
#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include "skill.h"
#include "util.h"
QJsonObject projectAIStrategyJudgeContext(const AIRequest &request, Room &room,
    ServerPlayer *viewer, const JudgeStruct *judge,
    const std::function<QJsonObject(const Card *)> &publicCard)
{
    if (!viewer || !judge || !judge->who) return {};
        const auto knownNoFilter = [&](const ServerPlayer *player) {
            const AIPlayerView *visible = nullptr;
            if (player->objectName() == request.worldView.self.objectName) visible = &request.worldView.self;
            else for (const AIPlayerView &candidate : request.worldView.players)
                if (candidate.objectName == player->objectName()) { visible = &candidate; break; }
            if (!visible || !visible->skillsComplete) return false;
            for (const AISkillView &skill : visible->skills) {
                // A visible skill's public class may be inspected; never enumerate
                // the foreign player's hidden live SkillInstances to infer absence.
                if (!skill.invalid && dynamic_cast<const FilterSkill *>(Sanguosha->getViewAsSkill(skill.skillName)))
                    return false;
            }
            return true;
        };
        // Owner filter queries use an audited detached wrapper/mapping scope.
        // Another player's private filters remain outside this viewer's authority.
        bool complete = judge->who == viewer || knownNoFilter(judge->who);
        int lightningHolders = 0;
        bool lightningReserveComplete = true;
        if (judge->reason != QStringLiteral("lightning")) {
            for (ServerPlayer *holder : room.getAllPlayers()) {
                if (!holder->containsTrick(QStringLiteral("lightning"))) continue;
                ++lightningHolders;
                if (!knownNoFilter(holder)) lightningReserveComplete = false;
            }
        }
        QList<int> owned = viewer->handCards();
        for (const Card *equip : viewer->getEquips())
            if (equip) owned << equip->getEffectiveId();
        // Response candidates already carry the authority's pattern/method gates.
        // Invocation/choice outcomes describe owned cards, not skill-cost legality.
        const QSet<int> ownedSet = qsanToSet(owned);
        const QList<int> candidates = request.kind == AIRequest::RespondCard
            ? request.choiceOptions.cardIds : owned;
        QJsonObject outcomes, filteredSuits, filteredNumbers, unfilteredOutcomes, filterRepairs;
        bool publicPattern = !judge->pattern.contains('$');
        for (const QString &branch : judge->pattern.split('#')) {
            const QStringList factors = branch.split('|');
            if (factors.size() >= 4 && factors.at(3) != ".") publicPattern = false;
        }
        QJsonObject lightningCandidates;
        for (int id : candidates) {
            if (!ownedSet.contains(id)) continue;
            const Card *card = Sanguosha->getCard(id);
            if (!card) {
                complete = false;
                lightningReserveComplete = false;
                break;
            }
            if (viewer->isCardLimited(card, request.handlingMethod)) continue;
            // Name/suit/number-only predicates depend solely on this disclosed
            // owned card. Do not run foreign availability or zone predicates.
            if (publicPattern) unfilteredOutcomes.insert(QString::number(id), judge->isGood(card));
            if (judge->who == viewer) {
                const QJsonObject filtered = projectAIOwnerFilteredCard(room, viewer, id, judge);
                const QJsonObject printed = filtered.value("card").toObject();
                if (filtered.contains("donor_error_repair"))
                    filterRepairs.insert(QString::number(id), filtered.value("donor_error_repair"));
                if (!filtered.value("complete").toBool() || printed.isEmpty()
                    || !filtered.value("judge_outcome").isBool()) complete = false;
                else {
                    outcomes.insert(QString::number(id), filtered.value("judge_outcome"));
                    filteredSuits.insert(QString::number(id), printed.value("suit"));
                    filteredNumbers.insert(QString::number(id), printed.value("number"));
                }
            } else if (complete) {
                outcomes.insert(QString::number(id), judge->isGood(card));
                filteredSuits.insert(QString::number(id), int(card->getSuit()));
                filteredNumbers.insert(QString::number(id), card->getNumber());
            }
            if (lightningHolders > 0 && lightningReserveComplete
                && card->getSuit() == Card::Spade && card->getNumber() >= 2
                && card->getNumber() <= 9)
                lightningCandidates.insert(QString::number(id), true);
        }
        if (!complete) outcomes = QJsonObject();
        if (!lightningReserveComplete) lightningCandidates = QJsonObject();
        // Lua first applies its retrial policy, then reserves one eligible card
        // from the end per holder. Reserving here would use the wrong candidate order.
        QJsonObject result{{"who", judge->who->objectName()},
            {"reason", judge->reason}, {"pattern", judge->pattern}, {"good", judge->isGood()},
            {"predicate_good", judge->good}, {"negative", judge->negative}, {"card", publicCard(judge->card)},
            {"outcome_by_id", outcomes}, {"outcomes_complete", complete},
            {"unfiltered_outcome_by_id", unfilteredOutcomes}, {"unfiltered_outcomes_complete", publicPattern},
            {"repair_by_id", filterRepairs},
            {"filtered_suit_by_id", filteredSuits}, {"filtered_number_by_id", filteredNumbers},
            {"filtered_complete", complete},
            {"lightning_candidate_ids", lightningCandidates},
            {"lightning_holder_count", lightningHolders},
            {"lightning_reserve_complete", lightningReserveComplete}};
        projectAIRound30OriginalJudge(request, room, viewer, judge, result, publicCard);
        return result;
}
