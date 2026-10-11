#include "ai-strategy-olclan.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>

namespace {
QString notifiedPile(const QString &pattern)
{
    // These exact donor callbacks evaluate already disclosed physical cards.
    // The two *Ids tags are written from the same ordered list immediately
    // before notifyMoveToPile, which also writes the corresponding ForAI tag.
    if (pattern == "@@zuguangu") return "zuguangu";
    if (pattern == "@@zukaiji") return "zukaiji";
    if (pattern == "@@zugaobian") return "zugaobian";
    if (pattern == "@@zugaoshi") return "zugaoshi";
    if (pattern == "@@kezuliuju") return "kezuliuju";
    if (pattern == "@@kezuhuanjia") return "kezuhuanjia";
    if (pattern == "@@zugezhi") return "zugezhi";
    return {};
}
bool physicalId(const QJsonValue &value, int *out)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || number > std::numeric_limits<int>::max()
        || std::floor(number) != number) return false;
    *out = int(number); return true;
}
}

bool authorizeAIOlClanNotifiedEvaluation(const AIRequest &request, int cardId,
    QJsonObject *disclosedCard, QString *error)
{
    if (disclosedCard) *disclosedCard = {};
    const auto reject = [error](const char *message) {
        if (error) *error = QString::fromLatin1(message);
        return false;
    };
    QString pile = notifiedPile(request.pattern);
    const bool jieli = request.pattern == "@@zujieli";
    if (jieli) {
        // Both source lists are notified to this exact actor. Select a list
        // by its disclosed ID only; the complete/current checks below remain.
        const auto piles = request.worldView.strategyFacts.value("notified_piles").toObject();
        for (const QString &name : {QStringLiteral("zujieli"), QStringLiteral("zujieliN")})
            if (piles.value('#' + name).toObject().value("card_ids").toArray().contains(cardId)) {
                if (!pile.isEmpty()) return reject("notified card occurs in both source lists");
                pile = name;
            }
    }
    if (request.kind != AIRequest::UseCard
        || request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        || request.handlingMethod != (jieli ? Card::MethodNone : Card::MethodUse) || pile.isEmpty() || cardId < 0
        || request.decisionId == 0 || request.viewerObjectName.isEmpty()
        || request.viewerObjectName != request.worldView.self.objectName
        || request.stateRevision != request.worldView.revision)
        return reject("notified evaluation requires its exact active viewer request");
    const QJsonValue piles = request.worldView.strategyFacts.value("notified_piles");
    if (!piles.isObject()) return reject("notified pile disclosure is absent");
    const QJsonValue raw = piles.toObject().value('#' + pile);
    if (!raw.isObject()) return reject("notified pile disclosure is absent");
    const QJsonObject row = raw.toObject();
    if (row.value("complete") != QJsonValue(true)
        || row.value("tag_key") != QJsonValue(pile + "ForAI")
        || !row.value("card_ids").isArray() || !row.value("current_card_ids").isArray()
        || !row.value("cards").isArray())
        return reject("notified pile disclosure is incomplete");
    const QJsonArray ids = row.value("card_ids").toArray();
    const QJsonArray cards = row.value("cards").toArray();
    if (ids.size() != cards.size()) return reject("notified faces do not match their ordered ids");
    QSet<int> disclosedIds;
    QJsonObject selected;
    for (qsizetype i = 0; i < ids.size(); ++i) {
        int id = -1, faceId = -1, effectiveId = -1;
        if (!physicalId(ids.at(i), &id) || disclosedIds.contains(id) || !cards.at(i).isObject())
            return reject("notified ids are malformed or duplicated");
        const QJsonObject card = cards.at(i).toObject();
        if (!physicalId(card.value("id"), &faceId) || !physicalId(card.value("effective_id"), &effectiveId)
            || faceId != id || effectiveId != id || card.value("name").toString().isEmpty()
            || card.value("class_name").toString().isEmpty())
            return reject("notified card face is not the disclosed physical card");
        disclosedIds.insert(id);
        if (id == cardId) selected = card;
    }
    QSet<int> currentIds;
    int previous = -1;
    for (const QJsonValue &value : row.value("current_card_ids").toArray()) {
        int id = -1;
        if (!physicalId(value, &id) || !disclosedIds.contains(id) || currentIds.contains(id))
            return reject("current notified ids are not a disclosed subset");
        // Projection emits this subset in source tag order; do not authorize a
        // forged reorder as the source's first-card or stable-sort input.
        qsizetype index = 0;
        while (index < ids.size() && ids.at(index) != QJsonValue(id)) ++index;
        if (index <= previous) return reject("current notified ids changed source order");
        previous = int(index); currentIds.insert(id);
    }
    if (selected.isEmpty() || !currentIds.contains(cardId))
        return reject("notified card is remembered but not currently disclosed");
    if (disclosedCard) *disclosedCard = selected;
    return true;
}


bool authorizeAIOlClanMarkedPhysicalProduct(const AIRequest &request, int productId,
    QJsonObject *disclosedCard, bool *printedProduct, QString *error)
{
    if (disclosedCard) *disclosedCard = {};
    if (printedProduct) *printedProduct = false;
    const auto reject = [error](const char *message) {
        if (error) *error = QString::fromLatin1(message);
        return false;
    };
    QString skill, mark;
    bool printed = false;
    if (request.pattern == QStringLiteral("@@kezumingjiemark")) {
        skill = QStringLiteral("kezumingjiemark"); mark = QStringLiteral("kezumingjie-PlayClear"); printed = true;
    } else if (request.pattern == QStringLiteral("@@kezulilunuse")) {
        skill = QStringLiteral("kezulilunuse"); mark = QStringLiteral("kezulilunuse");
    } else return reject("marked physical product pattern is not audited");
    if (request.kind != AIRequest::UseCard || request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        || request.handlingMethod != Card::MethodUse || request.decisionId == 0 || productId < 0
        || request.viewerObjectName.isEmpty() || request.viewerObjectName != request.worldView.self.objectName
        || request.stateRevision != request.worldView.revision)
        return reject("marked physical product requires its active owner request");
    const auto &marks = request.worldView.self.publicMarks;
    const auto selected = marks.constFind(mark);
    if (selected == marks.constEnd() || selected.value() != productId)
        return reject("marked physical product is not the disclosed owner selection");
    const auto matching = [&](const AiSkillActionContext &action) {
        return action.isValid() && action.getActivationOwner() == request.viewerObjectName
            && action.getActivationSkillName() == skill
            && action.activationQuotaAvailable && action.sourceQuotaAvailable;
    };
    bool scope = request.hasSkillActionContext && matching(request.skillActionContext);
    for (const auto &action : request.skillActions) if (matching(action)) { scope = true; break; }
    if (!scope) return reject("marked physical product has no authorized native skill scope");
    const AICardView *face = nullptr;
    const auto find = [&](const QList<AICardView> &cards) {
        for (const auto &card : cards) {
            if (card.cardId == productId && card.effectiveId == productId && !card.virtualCard
                && !card.objectName.isEmpty() && !card.className.isEmpty()) { face = &card; return; }
        }
    };
    // Lists below are already viewer-sanitized snapshot descriptors. A card ID
    // in a count-only/closed pile, a mark alone or old AG memory cannot pass.
    find(request.worldView.handCards);
    if (!face) find(request.worldView.discardPile);
    const auto findPlayer = [&](const AIPlayerView &player) {
        if (!face) find(player.equips);
        if (!face) find(player.judgingArea);
        if (!face) find(player.knownCards);
        for (const auto &pile : player.piles) if (!face) find(pile.cards);
    };
    findPlayer(request.worldView.self);
    for (const auto &player : request.worldView.players) if (!face) findPlayer(player);
    if (!face) return reject("marked physical product has no currently disclosed face");
    if (disclosedCard) *disclosedCard = QJsonObject{{"id", productId}, {"effective_id", productId},
        {"name", face->objectName}, {"class_name", face->className},
        {"suit", face->suit}, {"number", face->number}, {"virtual_card", false}};
    if (printedProduct) *printedProduct = printed;
    if (error) error->clear();
    return true;
}
