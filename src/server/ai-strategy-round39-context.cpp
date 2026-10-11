#include "ai-strategy-round39-context.h"
#include "ai.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "skill.h"
#include <QSet>

namespace {
thread_local AIRound39WendaoScope *activeWendao = nullptr;
QString question(AIRequest::DecisionKind kind)
{
    if (kind == AIRequest::PlayersChosen) return "players_chosen";
    if (kind == AIRequest::Discard) return "discard";
    if (kind == AIRequest::AmazingGrace) return "amazing_grace";
    return {};
}
bool publicFacePattern(const QString &pattern)
{
    if (pattern.contains('$')) return false;
    for (const QString &branch : pattern.split('#')) {
        const QStringList factors = branch.split('|');
        if (factors.size() >= 4 && factors.at(3) != ".") return false;
    }
    return true;
}
}

AIRound39WendaoScope::AIRound39WendaoScope(Room &room,
    const TriggerSkillV2 *definition, SkillContext *context)
    : previous_(activeWendao)
{
    // Always push, even for an unrelated nested skill: it must not inherit the
    // outer skill's private decision capability.
    activeWendao = this;
    if (!definition || definition->objectName() != "keqiwendao" || !context
        || context->current_event != AskForRetrial || !context->owner
        || context->owner->getRoom() != &room || !context->original_data
        || context->activationRef.key.skillName != "keqiwendao"
        || context->activationRef.ownerObjectName != context->owner->objectName()
        || !context->original_data->canConvert<JudgeStruct *>()) return;
    JudgeStruct *judge = context->original_data->value<JudgeStruct *>();
    if (!judge || judge->who != context->owner) return;
    room_ = &room; context_ = context; judge_ = judge;
}

AIRound39WendaoScope::~AIRound39WendaoScope()
{
    activeWendao = previous_;
}

void projectAIRound39Wendao(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    const QString kind = question(request.kind);
    if (kind.isEmpty() || request.choiceOptions.reason != "keqiwendao") return;
    // Explicit unknown for the exact named question outside its native frame.
    QJsonObject result{{"known", false}};
    const auto publish = [&]() {
        request.choiceOptions.context.insert("keqiwendao_judge", result);
    };
    publish();
    const auto *scope = activeWendao;
    if (!viewer || viewer->getRoom() != &room || room.roomRuntime()->isClosing()
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()
        || !scope || scope->room_ != &room || !scope->context_ || !scope->judge_)
        return;
    const SkillContext &context = *scope->context_;
    if (!context.original_data || context.original_data->value<JudgeStruct *>() != scope->judge_
        || context.owner != scope->judge_->who) return;
    if (request.kind == AIRequest::Discard) {
        if (!context.targets.contains(viewer)) return;
    } else if (viewer != context.owner) return;
    result = QJsonObject{{"known", true}, {"viewer", viewer->objectName()},
        {"decision_id", request.getDecisionId()}, {"state_revision", request.getStateRevision()},
        {"kind", kind}, {"reason", "keqiwendao"}};
    const QVariant value = room.getTag("keqiwendaoJudge");
    JudgeStruct *tagJudge = value.canConvert<JudgeStruct *>() ? value.value<JudgeStruct *>() : nullptr;
    if (!tagJudge) {
        result.insert("present", false); publish(); return; // known source null; not unknown
    }
    if (tagJudge != scope->judge_) { result = QJsonObject{{"known", false}}; publish(); return; }
    result.insert("present", true);
    result.insert("who", tagJudge->who->objectName());
    result.insert("good", tagJudge->isGood()); // cached result, including native UNKNOWN => false
    QList<AICardView> disclosed;
    if (request.kind == AIRequest::AmazingGrace) {
        for (const AICardView &card : request.choiceOptions.cards)
            if (request.choiceOptions.cardIds.contains(card.effectiveId)) disclosed.append(card);
    } else if (request.kind == AIRequest::Discard) {
        disclosed = request.worldView.handCards;
        disclosed.append(request.worldView.self.equips);
    } else {
        if (request.choiceOptions.playerNames.contains(viewer->objectName())) {
            disclosed = request.worldView.handCards;
            disclosed.append(request.worldView.self.equips);
        }
        for (const AIPlayerView &player : request.worldView.players) {
            if (!request.choiceOptions.playerNames.contains(player.objectName)) continue;
            disclosed.append(player.knownCards);
            disclosed.append(player.equips);
        }
    }
    QJsonObject outcomes;
    const bool predicateVisible = tagJudge->who == viewer || publicFacePattern(tagJudge->pattern);
    QSet<int> seen;
    for (const AICardView &view : disclosed) {
        const int id = view.effectiveId;
        if (id < 0 || view.virtualCard || seen.contains(id)) continue;
        seen.insert(id);
        const Card *card = room.getCard(id);
        if (!card || card->isVirtualCard() || card->getEffectiveId() != id
            || card->objectName() != view.objectName || card->getClassName() != view.className
            || int(card->getSuit()) != view.suit || card->getNumber() != view.number) continue;
        QJsonObject row{{"card_id", id}, {"name", view.objectName}, {"class_name", view.className},
            {"suit", view.suit}, {"number", view.number}};
        // DIRECT JudgeStruct::isGood(card), never CardFilter or ordinary
        // hypothetical retrial outcomes. No new authority, flags or skill facts.
        if (predicateVisible) row.insert("good", tagJudge->isGood(card));
        outcomes.insert(QString::number(id), row);
    }
    result.insert("outcomes", outcomes);
    publish();
}
