#include "ai-strategy-yczh2017.h"
#include "ai.h"
#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include "skill.h"
#include <QJsonArray>

void projectAIStrategyYCZH2017(AIRequest &request, Room &room, ServerPlayer *viewer,
    const std::function<QJsonObject(const Card *)> &publicDecisionCard)
{
    if (!viewer || request.viewerObjectName != viewer->objectName()) return;
    QJsonObject facts{{"funan_level_up", viewer->property("funan_level_up").toBool()},
                      {"funan_card", false}};
    // Funan temporarily records the publicly declared response while it asks
    // its owner. Do not read this tag for another observer or another question.
    if (request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "funan") {
        const Card *card = viewer->getTag("FunanCard").value<const Card *>();
        if (card) facts.insert("funan_card", publicDecisionCard(card));
    }
    request.worldView.strategyFacts.insert("yczh2017", facts);
    // These recipients were captured before Huimin drew cards. Recomputing
    // them from present hand sizes changes the native selection domain.
    if (request.kind == AIRequest::UseCard && request.pattern == "@@huimin!") {
        QJsonObject value; bool found = false;
        for (const auto &scope : request.skillActions) {
            if (!scope.isValid() || scope.getActivationOwner() != viewer->objectName()
                || scope.getActivationSkillName() != "huimin"
                || !room.isAcceptedViewAsEffect(scope.activationRef)) continue;
            const int id = scope.getActivationInstanceId();
            const QVariant count = viewer->getSkillInstanceStateValue("huimin", id, "count");
            const QVariant recipients = viewer->getSkillInstanceStateValue("huimin", id, "recipients");
            if (!count.isValid() || !recipients.isValid()) continue;
            const QJsonObject candidate{{"count", count.toInt()},
                {"recipients", QJsonArray::fromStringList(recipients.toStringList())}};
            // Two differing active scopes are ambiguous; no arbitrary winner.
            if (found && value != candidate) { value = {}; break; }
            value = candidate; found = true;
        }
        if (!value.isEmpty()) {
            request.choiceOptions.context.insert("huimin", value);
            // UseCard does not serialize AIChoiceOptions. The shared facade
            // merges this world context for card and choice inquiries alike.
            request.worldView.decisionContext.insert("huimin", value);
        }
    }
    // Current Fumian uses a split player inquiry. Its public declared use is
    // read only from the live enclosing Fumian execution, never a stale tag.
    if ((request.kind == AIRequest::PlayersChosen || request.kind == AIRequest::PlayerChosen)
        && request.choiceOptions.reason == "fumian") {
        const QVariantMap history = room.historyParent(room.currentHistoryEventId(), "skill", true).value("data").toMap();
        const qint64 execution = history.value("execution_id").toLongLong();
        const auto *entry = room.findSkillExecution(execution);
        if (execution > 0 && entry && !entry->finished) {
            const SkillContext ctx = room.getSkillExecutionContext(execution);
            if ((ctx.skill_name == "fumian" || ctx.activationRef.key.skillName == "fumian")
                && ctx.original_data && ctx.original_data->canConvert<CardUseStruct>()) {
                const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
                // The ongoing use belongs to this viewer, even when Fumian
                // originated from another public receipt owner.
                if (use.from == viewer && use.card) {
                    QJsonArray targets;
                    for (const ServerPlayer *p : use.to) if (p) targets.append(p->objectName());
                    request.choiceOptions.context.insert("fumian_use", QJsonObject{
                        {"from", viewer->objectName()}, {"to", targets}, {"card", publicDecisionCard(use.card)}});
                }
            }
        }
    }
    // Shouxi offers printed engine definitions in a private-to-viewer AG.
    // Serialize only those already offered IDs, never an arbitrary ID oracle.
    if (request.kind == AIRequest::AmazingGrace && request.choiceOptions.reason == "shouxi") {
        QJsonObject printed;
        for (int id : request.choiceOptions.cardIds) {
            const Card *card = Sanguosha->getEngineCard(id);
            if (!card) continue;
            QJsonObject row = publicDecisionCard(card);
            row.insert("legacy_string_key", card->toString());
            row.insert("legacy_sort_key", card->toString());
            row.insert("virtual_card", false);
            row.insert("subcards", QJsonArray());
            printed.insert(QString::number(id), row);
        }
        request.choiceOptions.context.insert("shouxi_printed_cards", printed);
    }
}
