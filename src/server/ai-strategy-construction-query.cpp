#include "ai-strategy-round44-certificate.h"
#include "ai-strategy-construction-query.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>

namespace {
struct Constructor {
    const char *source;
    const char *product;
    const char *printedSkill;
    AIRequest::DecisionKind kind;
    const char *question;
};
// Fixed b6025acc ai/feiDIY-ai.lua197/311/350/728 and meizl-ai.lua5513.
// Feilangduo deliberately prints feihujue; Feiyanjun prints feiyanjunCard.
// None of these source clones carries material, declaration or recast mutation.
const Constructor constructors[] = {
    {"feilianying", "fire_attack", "feilianying", AIRequest::PlayerChosen, "feilianyingCard"},
    {"feihujue", "duel", "feihujue", AIRequest::Activate, ""},
    {"feilangduo", "duel", "feihujue", AIRequest::Activate, ""},
    {"feiyanjun", "duel", "feiyanjunCard", AIRequest::Activate, ""},
    {"meizl_o_yuxiang", "savage_assault", "meizl_o_yuxiang", AIRequest::SkillInvoke, "meizl_o_yuxiang"}
};
}

bool aiAuditedConstructionQuery(const AIRequest &request, const ServerPlayer *viewer,
                               const QJsonObject &spec, QString *error)
{
    const auto reject = [&](const char *reason) {
        if (error) *error = QString::fromLatin1(reason);
        return false;
    };
    if (!viewer || !viewer->getRoom() || request.viewerObjectName != viewer->objectName()
        || !aiRound44RangeRequestCurrent(*viewer->getRoom(),request,viewer))
        return reject("construction query request owner or revision mismatch");
    static const QSet<QString> fields{"source_key", "skill", "name", "suit", "number", "subcards"};
    if (spec.size() != fields.size()) return reject("construction query field set mismatch");
    for (auto it = spec.begin(); it != spec.end(); ++it)
        if (!fields.contains(it.key())) return reject("construction query has unaudited field");
    if (!spec.value("source_key").isString() || !spec.value("skill").isString()
        || !spec.value("name").isString() || !spec.value("suit").isDouble()
        || !spec.value("number").isDouble() || !spec.value("subcards").isArray())
        return reject("construction query field type mismatch");
    const QString key = spec.value("source_key").toString();
    // The named recipient need not own Mouxianchou (newgenerals.lua16176).
    // This is its exact askForCard policy probe, never a payable conversion.
    if (key == "mouxianchou") {
        const bool ok = request.kind == AIRequest::RespondCard && request.pattern == ".."
            && request.prompt == "@mouxianchou-slash"
            && spec.value("name").toString() == "slash"
            && spec.value("skill").toString() == "_mouxianchou"
            && spec.value("suit").toDouble() == int(Card::NoSuit)
            && spec.value("number").toDouble() == 0 && spec.value("subcards").toArray().isEmpty();
        if (!ok) return reject("Mouxianchou constructor differs from the named source question");
        if (error) error->clear();
        return true;
    }
    if (key == "keyaotaiping" || key == "kejieyaotaiping") {
        if (request.kind != AIRequest::Activate || request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            || (!viewer->hasSkill(key) && !viewer->hasLordSkill(key))
            || spec.value("skill").toString() != key
            || spec.value("suit").toDouble() != int(Card::NoSuit)
            || spec.value("number").toDouble() != 0 || spec.value("subcards").toArray().size() != 2)
            return reject("Taiping constructor scope or face differs from source");
        QList<int> ids;
        Card::Suit materialSuit = Card::NoSuit;
        for (const QJsonValue &cost : spec.value("subcards").toArray()) {
            const double n = cost.toDouble(-1);
            if (!cost.isDouble() || !std::isfinite(n) || n != std::floor(n) || n < 0
                || n > std::numeric_limits<int>::max() || ids.contains(int(n)))
                return reject("Taiping requires two ordered distinct physical materials");
            const int id = int(n);
            bool disclosed = false;
            if (viewer->handCards().contains(id))
                for (const AICardView &card : request.worldView.handCards)
                    if (card.effectiveId == id) disclosed = true;
            if (viewer->getPile("wooden_ox").contains(id))
                for (const AICardPileView &pile : request.worldView.self.piles)
                    if (pile.name == "wooden_ox")
                        for (const AICardView &card : pile.cards)
                            if (card.effectiveId == id) disclosed = true;
            const Card *card = disclosed ? Sanguosha->getCard(id) : nullptr;
            if (!card || viewer->getRoom()->getCardOwner(id) != viewer
                || (!ids.isEmpty() && card->getSuit() != materialSuit))
                return reject("Taiping materials are not disclosed same-suit owner hand or wooden_ox");
            materialSuit = card->getSuit();
            ids.append(id);
        }
        const QString product = materialSuit == Card::Heart ? "archery_attack"
            : materialSuit == Card::Spade ? "savage_assault"
            : materialSuit == Card::Diamond ? "god_salvation"
            : materialSuit == Card::Club ? "amazing_grace" : QString();
        if (product.isEmpty() || product != spec.value("name").toString())
            return reject("Taiping product does not match the source material suit");
        if (error) error->clear();
        return true;
    }
    for (const auto &row : constructors) {
        if (key != QLatin1String(row.source)) continue;
        if (request.kind != row.kind
            || (row.kind == AIRequest::Activate && request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
            || (row.kind != AIRequest::Activate && request.choiceOptions.reason != QLatin1String(row.question)))
            return reject("construction query is outside its fixed source question");
        if (!viewer->hasSkill(key) && !viewer->hasLordSkill(key))
            return reject("construction query source is not currently owned");
        if (spec.value("name").toString() != QLatin1String(row.product)
            || spec.value("skill").toString() != QLatin1String(row.printedSkill)
            || spec.value("suit").toDouble() != int(Card::NoSuit)
            || spec.value("number").toDouble() != 0
            || !spec.value("subcards").toArray().isEmpty())
            return reject("construction query differs from fixed source clone");
        if (error) error->clear();
        return true;
    }
    return reject("construction query source is outside audited finite catalog");
}

void projectAIConstructionFacts(AIWorldView &world)
{
    QJsonObject values = world.strategyFacts.value("MRmou").toObject();
    // MRmou AI tests Sanguosha:getSkill('olwansha'), not any player's ownership.
    values.insert("has_olwansha_definition", Sanguosha->getSkill("olwansha") != nullptr);
    world.strategyFacts.insert("MRmou", values);
}

// mountainrumour.lua1698-1851: the dynamic factory emits these prototypes
// only in Play with an empty pattern. Response products are different cards.
int aiAuditedConstructionSourcePrototype(const AIRequest &request, const ServerPlayer *viewer,
                                        const QString &skill, const QString &prototype)
{
    if (!viewer || !viewer->getRoom() || request.viewerObjectName != viewer->objectName()
        || !aiRound44RangeRequestCurrent(*viewer->getRoom(),request,viewer)
        || (!viewer->hasSkill(skill) && !viewer->hasLordSkill(skill))) return -1;
    struct Action { const char *skill; const char *prototype; int flags; };
    // Exact fixed-source/provider pairs. These only select the prototype:
    // active scope, ordered costs, canSelect and createCard remain authoritative.
    static const Action actions[] = {
        {"MR_huishi", "#MR_huishi", 0}, // extensions/MR.lua:149
        {"MR_jilve", "#MR_jilve", 0}, // extensions/MR.lua:223
        {"MR_fanjian", "#MR_fanjian", 1}, // extensions/MR.lua:764
        {"MR_quhu", "#MR_quhu", 0}, // extensions/MR.lua:896
        {"MR_gongxin", "#MR_gongxin", 0}, // extensions/MR.lua:1223
        {"keyaoquwu", "#keyaoquwuCard", 0}, // extensions/keyaobao.lua:368
        {"kejieyaoquwu", "#kejieyaoquwuCard", 0}, // extensions/keyaobao.lua:1285
        {"keyaoshidu", "#keyaoshiduCard", 1}, // extensions/keyaobao.lua:606
        {"kejieyaoshidu", "#kejieyaoshiduCard", 1}, // extensions/keyaobao.lua:1546
        {"feilianying", "#feilianyingCard", 0}, // extensions/feiDIY.lua:91
        {"feiqianxun", "#feiqianxuncard", 0}, // extensions/feiDIY.lua:157
        {"feihujue", "#feihujueCard", 0}, // extensions/feiDIY.lua:498
        {"feilangduo", "#feilangduoCard", 0}, // extensions/feiDIY.lua:584
        {"feizuijiao", "#feizuijiaoCard", 0}, // extensions/feiDIY.lua:1060
        {"feilianhuan", "#feilianhuanCard", 0}, // extensions/feiDIY.lua:1354
        {"feisheji", "#feishejiCard", 1}, // extensions/feiDIY.lua:758
        {"feiyanjun", "#feiyanjunCard", 1}, // extensions/feiDIY.lua:1236
    };
    const bool play = request.kind == AIRequest::Activate
        && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.pattern.isEmpty();
    const bool response = request.kind == AIRequest::UseCard || request.kind == AIRequest::RespondCard;
    for (const auto &row : actions) {
        if (skill != QLatin1String(row.skill) || prototype != QLatin1String(row.prototype)) continue;
        if (skill == "feilianying") {
            return response && request.pattern == "@@feilianying"
                && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                    || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) ? row.flags : -1;
        }
        if (skill == "feisheji" && response && request.pattern == "slash"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return row.flags;
        return play ? row.flags : -1;
    }
    if (!play) return -1;
    for (int i = 1; i <= 10; ++i) {
        if (skill != "shisheng" + QString::number(i)) continue;
        if (prototype != "#shisheng_sc" + QString::number(i)
            || viewer->getMark("@wins") < i) return -1;
        return i == 10 ? 0 : 1;
    }
    return -1;
}

// Fixed b6025acc MRmou-ai.lua738-764 / MR.lua1389-1413. This is an
// admission predicate, never permission to skip native canSelect/createCard.
bool aiAuditedNonconsumingSelection(const AIRequest &request, const ServerPlayer *viewer,
    const QString &skill, const QJsonObject &product, const QJsonArray &selectedMaterials,
    QString *error)
{
    const auto reject = [&](const char *reason) {
        if (error) *error = QString::fromLatin1(reason);
        return false;
    };
    if (!viewer || !viewer->getRoom() || request.viewerObjectName != viewer->objectName()
        || !aiRound44RangeRequestCurrent(*viewer->getRoom(),request,viewer)
        || request.kind != AIRequest::Activate
        || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || !request.pattern.isEmpty()
        || skill != "MR_zhizhe" || !viewer->hasSkill(skill))
        return reject("nonconsuming selection is outside the fixed owner Play source");
    static const QSet<QString> fields{"name", "skill", "suit", "number", "subcards"};
    if (product.size() != fields.size()) return reject("nonconsuming product field set mismatch");
    for (auto it = product.begin(); it != product.end(); ++it)
        if (!fields.contains(it.key())) return reject("nonconsuming product has unaudited field");
    if (!product.value("name").isString() || !product.value("skill").isString()
        || product.value("skill").toString() != skill
        || !product.value("suit").isDouble() || product.value("suit").toDouble() != int(Card::NoSuit)
        || !product.value("number").isDouble() || product.value("number").toDouble() != 0
        || !product.value("subcards").isArray() || !product.value("subcards").toArray().isEmpty()
        || selectedMaterials.size() != 1)
        return reject("nonconsuming selection requires one material and the exact zero-cost face");
    const QJsonValue value = selectedMaterials.first();
    const double n = value.toDouble(-1);
    if (!value.isDouble() || !std::isfinite(n) || n != std::floor(n) || n < 0
        || n > std::numeric_limits<int>::max())
        return reject("nonconsuming material must be a physical card ID");
    const int id = int(n);
    bool disclosed = false;
    for (const AICardView &card : request.worldView.handCards)
        if (card.effectiveId == id) disclosed = true;
    if (!disclosed || !viewer->handCards().contains(id)
        || viewer->getRoom()->getCardOwner(id) != viewer)
        return reject("nonconsuming material is not current disclosed owner hand");
    const Card *material = Sanguosha->getCard(id);
    if (!material || material->objectName() != product.value("name").toString()
        || (!material->isKindOf("BasicCard") && !material->isNDTrick())
        || material->isKindOf("Jink") || material->isKindOf("Nullification")
        || viewer->getMark(material->objectName() + "+MR_zhizhe-Clear") >= 1)
        return reject("nonconsuming material or product differs from the fixed provider");
    if (error) error->clear();
    return true;
}
