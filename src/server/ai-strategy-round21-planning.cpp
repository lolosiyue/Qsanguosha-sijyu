#include "ai-strategy-round21-planning.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <limits>

namespace {
bool ownerRequest(const AIRequest &request, const ServerPlayer *viewer)
{
    return viewer && viewer->getRoom() && request.viewerObjectName == viewer->objectName()
        && request.stateRevision == viewer->getRoom()->roomRuntime()->stateRevision();
}
bool play(const AIRequest &request)
{
    return request.kind == AIRequest::Activate
        && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.pattern.isEmpty();
}
bool disclosed(const AIRequest &request, int id)
{
    for (const AICardView &card : request.worldView.handCards)
        if (card.effectiveId == id) return true;
    QList<AIPlayerView> disclosedPlayers = request.worldView.players;
    disclosedPlayers.prepend(request.worldView.self);
    for (const AIPlayerView &player : disclosedPlayers) {
        for (const AICardView &card : player.equips) if (card.effectiveId == id) return true;
        for (const AICardView &card : player.judgingArea) if (card.effectiveId == id) return true;
        for (const AICardView &card : player.knownCards) if (card.effectiveId == id) return true;
        for (const AICardPileView &pile : player.piles)
            for (const AICardView &card : pile.cards) if (card.effectiveId == id) return true;
    }
    return false;
}
}

bool aiRound21PlanningConstruction(const AIRequest &request, const ServerPlayer *viewer,
    const QJsonObject &spec, QString *error)
{
    const auto reject = [&](const char *why) { if (error) *error = QString::fromLatin1(why); return false; };
    if (!ownerRequest(request, viewer) || !play(request)) return reject("Round21 readonly constructor requires current owner Play");
    const QSet<QString> fields{"source_key", "name", "skill", "suit", "number", "subcards"};
    if (spec.size() != fields.size()) return reject("Round21 constructor field count differs");
    for (auto it = spec.begin(); it != spec.end(); ++it)
        if (!fields.contains(it.key())) return reject("Round21 constructor has extra field");
    if (!spec.value("source_key").isString() || !spec.value("name").isString()
        || !spec.value("skill").isString() || !spec.value("suit").isDouble()
        || !spec.value("number").isDouble() || !spec.value("subcards").isArray()
        || spec.value("suit").toDouble() != int(Card::NoSuit) || spec.value("number").toDouble() != 0)
        return reject("Round21 source requires exact NoSuit/0 constructor");
    const QString skill = spec.value("source_key").toString(), name = spec.value("name").toString();
    if (spec.value("skill").toString() != skill || !viewer->hasSkill(skill))
        return reject("Round21 raw printed skill or owner skill differs");
    const QJsonArray costs = spec.value("subcards").toArray();
    if (skill == "jinbingxin" || skill == "kezushengmo") {
        // Source jie_package185 / olClan1380: clone each PUBLIC pattern Basic
        // as a zero-material hypothesis, before choosing its declared seed.
        // Do not run canActivate here: hypotheses are not payment proposals.
        if (!costs.isEmpty() || !request.worldView.strategyFacts.value("pattern_names").toArray().contains(name))
            return reject("Round21 Basic hypothesis is not a zero-cost public pattern");
        bool basic = false;
        for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
            const Card *card = Sanguosha->getEngineCard(id);
            if (card && card->objectName() == name && card->getTypeId() == Card::TypeBasic) { basic = true; break; }
        }
        if (!basic) return reject("Round21 declared hypothesis is not a public Basic definition");
    } else if (skill == "kezujiexuan") {
        // Source olClan722: every hypothesis keeps the NoSuit/0 constructor,
        // including the yin first-he RED branch which the native VS rejects.
        const int state = viewer->getChangeSkillState(skill);
        if (name != (state == 1 ? "snatch" : "dismantlement") || costs.size() != 1)
            return reject("Jiexuan source product or material count differs");
        const QJsonValue value = costs.first(); const double n = value.toDouble(-1);
        if (!value.isDouble() || !std::isfinite(n) || n != std::floor(n)
            || n < 0 || n > std::numeric_limits<int>::max()) return reject("Jiexuan material ID is malformed");
        const int id = int(n);
        bool sourceZone = viewer->getHandPile().contains(id);
        for (const Card *card : viewer->getCards("he")) if (card && card->getEffectiveId() == id) sourceZone = true;
        if (!sourceZone || !disclosed(request, id)) return reject("Jiexuan material is not disclosed in the source owner zones");
    } else return reject("Round21 constructor source is not audited");
    if (error) error->clear();
    return true;
}

int aiRound21PlanningPrototype(const AIRequest &request, const ServerPlayer *viewer,
    const QString &skill, const QString &prototype)
{
    if (!ownerRequest(request, viewer)) return -1;
    // Round21 A: fixed owner-material source constructors; live payment remains native.
    if (viewer->hasSkill(skill)) {
        if (play(request) && request.pattern == ""
            && skill == "kezulilun" && prototype == "#kezulilunCard") return 1;
        if (request.kind == AIRequest::UseCard && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@kezuchenyacz"
            && skill == "kezuchenyacz" && prototype == "#kezuchenyaCard") return 1;
        if (request.kind == AIRequest::UseCard && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@kezujianyuancz"
            && skill == "kezujianyuancz" && prototype == "#kezujianyuanCard") return 1;
        if (request.kind == AIRequest::UseCard && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@zujuetu!"
            && skill == "zujuetu" && prototype == "#zujuetuCard") return 1;
        if (request.kind == AIRequest::UseCard && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@zufuning"
            && skill == "zufuning" && prototype == "#zufuningCard") return 1;
        if (play(request) && request.pattern == ""
            && skill == "zumingdian" && prototype == "#zumingdianCard") return 1;
        if (request.kind == AIRequest::UseCard && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@zumingdian"
            && skill == "zumingdian" && prototype == "#zumingdianCard") return 1;
    }
    if (play(request)) {
        if (skill == "jinbolan_skill" && prototype == "JinBolanSkillCard") return 0;
        if (skill == "jinbingxin" && prototype == "JinBingxinCard") return 2;
        if (skill == "kezushengmo" && prototype == "#kezushengmoCard") return 2;
        if (skill == "zukudu" && prototype == "#zukuduCard") return 1;
    }
    // Jinbingxin/ Shengmo also have actual V2 Basic-card response declarations.
    // No @@jinbingxin or @@zukudu writer exists in the fixed native producer.
    const bool response = (request.kind == AIRequest::UseCard || request.kind == AIRequest::RespondCard)
        && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    if (response && !request.pattern.startsWith("@@")) {
        if (skill == "jinbingxin" && prototype == "JinBingxinCard") return 2;
        if (skill == "kezushengmo" && prototype == "#kezushengmoCard") return 2;
    }
    return -1;
}

void projectAIRound21PlanningContext(AIRequest &request, Room &room, const ServerPlayer *viewer,
    const std::function<QJsonObject(const Card *)> &publicCard)
{
    if (!viewer || viewer->getRoom() != &room || request.viewerObjectName != viewer->objectName()) return;
    const QString reason = request.choiceOptions.reason;
    const bool canmou = request.kind == AIRequest::PlayerChosen && reason == "jincanmou";
    const bool congjian = request.kind == AIRequest::SkillInvoke && reason == "jincongjian";
    const bool guess = request.kind == AIRequest::SkillInvoke && reason == "jinxiongshu_guess";
    if (!canmou && !congjian && !guess) return;
    QJsonObject context = request.choiceOptions.context.value("jie_package").toObject();
    const auto roster = room.getAllPlayers(true);
    if (canmou || congjian) {
        const QString key = canmou ? "JincanmouData" : "JincongjianData";
        const QVariant tag = viewer->getTag(key);
        if (!tag.isValid() || tag.isNull()) context.insert(key, false);
        else if (tag.canConvert<CardUseStruct>()) {
            const CardUseStruct use = tag.value<CardUseStruct>();
            if (!use.card) context.insert(key, false);
            else if (publicCard && use.from && roster.contains(use.from)
                && !use.card->isVirtualCard() && disclosed(request, use.card->getEffectiveId())
                && use.card == Sanguosha->getCard(use.card->getEffectiveId())) {
                bool valid = true; QJsonArray targets;
                for (const ServerPlayer *p : use.to) {
                    if (!p || !roster.contains(const_cast<ServerPlayer *>(p))) { valid = false; break; }
                    targets.append(p->objectName());
                }
                const QJsonObject card = publicCard(use.card);
                if (valid && !card.isEmpty()) context.insert(key, QJsonObject{
                    {"from", use.from->objectName()}, {"to", targets}, {"card", card}});
            }
        }
    }
    if (guess) {
        // No writer remains in fixed olwenwu.cpp. A missing tag yields -1 and
        // donor nil-card failure. Never borrow the newer declared-name prompt.
        const ServerPlayer *target = room.getCurrent();
        if (target) {
            const QVariant tag = target->getTag("JinXiongshuShowCard_" + viewer->objectName());
            const int id = tag.toInt() - 1;
            if (!tag.isValid() || id < 0) context.insert("JinXiongshuShowCard", false);
            else if (disclosed(request, id) && publicCard) {
                const Card *card = Sanguosha->getCard(id);
                if (card) {
                    QJsonObject face = publicCard(card);
                    // This question historically reveals the card to this AI.
                    // Only an already disclosed card reaches the query, so no
                    // hidden ID, face or unobserved tag value is projected.
                    if (!face.isEmpty()) context.insert("JinXiongshuShowCard", face);
                }
            }
        }
    }
    request.choiceOptions.context.insert("jie_package", context);
}
