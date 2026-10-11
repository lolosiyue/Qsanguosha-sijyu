#include "ai-strategy-scarlet-context.h"
#include "ai.h"
#include "ai-strategy-projection.h"
#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include "skill.h"
#include <QJsonArray>
#include <QSet>
#include <memory>

namespace {
using PublicCard = std::function<QJsonObject(const Card *)>;
bool roster(Room &room, const ServerPlayer *p)
{
    return !p || room.findPlayerByObjectName(p->objectName(), true) == p;
}
QJsonValue cardValue(const Card *card, const PublicCard &publicCard)
{
    return card ? QJsonValue(publicCard(card)) : QJsonValue(QJsonValue::Null);
}
QJsonObject damageValue(const DamageStruct &d, const PublicCard &publicCard)
{
    return {{"from", d.from ? d.from->objectName() : QString()},
        {"to", d.to ? d.to->objectName() : QString()}, {"amount", d.damage},
        {"nature", int(d.nature)}, {"chain", d.chain}, {"transfer", d.transfer},
        {"by_user", d.by_user}, {"reason", d.reason}, {"card", cardValue(d.card, publicCard)}};
}
QJsonObject useValue(const CardUseStruct &u, const PublicCard &publicCard)
{
    QJsonArray to; for (const auto *p : u.to) if (p) to.append(p->objectName());
    return {{"from", u.from ? u.from->objectName() : QString()}, {"to", to},
        {"card", cardValue(u.card, publicCard)}, {"m_addHistory", u.m_addHistory}};
}
QJsonObject effectValue(const CardEffectStruct &e, const PublicCard &publicCard)
{
    return {{"from", e.from ? e.from->objectName() : QString()},
        {"to", e.to ? e.to->objectName() : QString()}, {"card", cardValue(e.card, publicCard)},
        {"multiple", e.multiple}, {"offset_num", e.offset_num}};
}
bool validUse(Room &room, const CardUseStruct &u)
{
    if (!roster(room, u.from)) return false;
    for (const auto *p : u.to) if (!roster(room, p)) return false;
    return true;
}
QMap<int, const AICardView *> disclosedCards(const AIRequest &r)
{
    QMap<int, const AICardView *> ids;
    const auto cards = [&](const QList<AICardView> &list) {
        for (const auto &c : list) if (c.effectiveId >= 0) ids.insert(c.effectiveId, &c);
    };
    cards(r.worldView.handCards); cards(r.worldView.discardPile); cards(r.choiceOptions.cards);
    const auto player = [&](const AIPlayerView &p) {
        cards(p.knownCards); cards(p.equips); cards(p.judgingArea);
        for (const auto &pile : p.piles) if (pile.open) cards(pile.cards);
    };
    player(r.worldView.self); for (const auto &p : r.worldView.players) player(p);
    return ids;
}
bool publicJudgePattern(const QString &pattern)
{
    if (pattern.contains('$')) return false;
    for (const auto &branch : pattern.split('#')) {
        const auto factors = branch.split('|');
        if (factors.size() >= 4 && factors.at(3) != ".") return false;
    }
    return true;
}
void merge(AIWorldView &world, const QJsonObject &values)
{
    if (values.isEmpty()) return;
    auto facts = world.strategyFacts.value("scarletayuhuo").toObject();
    for (auto it = values.begin(); it != values.end(); ++it) facts.insert(it.key(), it.value());
    world.strategyFacts.insert("scarletayuhuo", facts);
}
}

void projectAIStrategyScarletContext(AIRequest &request, Room &room, ServerPlayer *viewer,
    const QVariant &data, QJsonObject &context, const PublicCard &publicCard)
{
    Q_UNUSED(data);
    if (!viewer || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName() || !roster(room, viewer)) return;
    const auto kind = request.kind;
    const auto reason = request.choiceOptions.reason;
    const auto pattern = request.pattern;
    QJsonObject facts;
    // Fixed extension 5443: the private AG has already revealed its offered cards
    // to this very recipient. No arbitrary card-id probe is introduced for the
    // donor's erroneous `for card_id in ipairs(card_ids)` index loop.
    if (kind == AIRequest::AmazingGrace && reason == "s2_guanhui") {
        const auto value = room.getTag("s2_guanhui");
        const auto *judge = value.canConvert<JudgeStruct *>() ? value.value<JudgeStruct *>() : nullptr;
        if (!judge) facts.insert("s2_guanhui", false);
        else if (judge->who && roster(room, judge->who) && judge->card && publicJudgePattern(judge->pattern)) {
            QJsonObject outcomes;
            const auto cards = disclosedCards(request);
            for (auto it = cards.begin(); it != cards.end(); ++it) {
                // Match only the already disclosed face, never a foreign live
                // wrapper (which might have privately changed since revelation).
                const auto &face = *it.value();
                std::unique_ptr<Card> card(Sanguosha->cloneCard(face.objectName, Card::Suit(face.suit), face.number));
                if (card) outcomes.insert(QString::number(it.key()), judge->isGood(card.get()));
            }
            facts.insert("s2_guanhui", QJsonObject{{"who", judge->who->objectName()},
                {"card", publicCard(judge->card)}, {"reason", judge->reason}, {"pattern", judge->pattern},
                {"good", judge->isGood()}, {"predicate_good", judge->good}, {"negative", judge->negative},
                {"play_animation", judge->play_animation}, {"outcome_by_id", outcomes}});
        }
    }
    // Fixed extension 6020/6021/6044: source reads exactly the old AmazingGrace
    // tag and calls fillAG before this Choice. Nonempty identities still require
    // independent disclosure in this viewer's snapshot/active fillAG ledger.
    // Missing visibility is not empty.
    if (kind == AIRequest::Choice && reason == "s2_lixian") {
        const auto value = room.getTag("AmazingGrace");
        QJsonArray ids; QJsonObject faces; bool known = true;
        const auto disclosed = disclosedCards(request);
        const auto notified = readAIStrategyNotifiedPile(room, viewer, "#ai_current_ag").value("cards").toObject();
        if (value.canConvert<QVariantList>()) for (const auto &v : value.toList()) {
            const int id = v.toInt(); const auto key = QString::number(id);
            // The existing fillAG recipient ledger is an independent visibility
            // witness. Its frozen face is used; never read a hidden live wrapper.
            if (notified.value(key).isObject()) faces.insert(key, notified.value(key));
            else if (!disclosed.contains(id)) { known = false; break; }
            ids.append(id);
        }
        if (known) { facts.insert("AmazingGrace", ids); facts.insert("disclosed_cards", faces); }
    }
    const bool useScope = (kind == AIRequest::UseCard && pattern == "@@s2_mieshi")
        || (kind == AIRequest::Choice && reason == "s3_zhuhai")
        || (kind == AIRequest::PlayerChosen && reason == "s3_yt_junlie")
        || ((kind == AIRequest::SkillInvoke || kind == AIRequest::Discard) && reason == "s3_fans_yinbing");
    if (useScope) {
        // QVariant::value is deliberately retained: absent/wrong-type tags give
        // the same default CardUseStruct as the donor SWIG toCardUse.
        const auto use = room.getTag("CurrentUseStruct").value<CardUseStruct>();
        if (validUse(room, use)) facts.insert("CurrentUseStruct", useValue(use, publicCard));
    }
    if (kind == AIRequest::UseCard && pattern == "@@s2_qiangrang") {
        // Source extension 13004 stores CardUseStruct, AI 3793 asks for
        // CardEffectStruct. Preserve Qt's empty default, not a fabricated effect.
        const auto effect = room.getTag("CurrentUseStruct").value<CardEffectStruct>();
        if (roster(room, effect.from) && roster(room, effect.to))
            facts.insert("CurrentUseStruct_CardEffect", effectValue(effect, publicCard));
    }
    if (kind == AIRequest::UseCard && pattern == "@@s3_yt_ruiqiDiscard") {
        const auto value = room.getTag("s3_yt_ruiqi");
        if (value.canConvert<CardEffectStruct>()) {
            const auto effect = value.value<CardEffectStruct>();
            // The asked player is the effect recipient, not necessarily its owner.
            if (effect.to == viewer && roster(room, effect.from))
                facts.insert("s3_yt_ruiqi", effectValue(effect, publicCard));
        } else if (!value.isValid()) facts.insert("s3_yt_ruiqi", effectValue(CardEffectStruct(), publicCard));
    }
    if (kind == AIRequest::UseCard && pattern == "@@s3_fans_jibian1") {
        const auto use = room.getTag("s3_fans_jibian").value<CardUseStruct>();
        if (validUse(room, use)) facts.insert("s3_fans_jibian", useValue(use, publicCard));
    }
    const bool kangdao = kind == AIRequest::PlayerChosen && reason == "s3_kangdao_target";
    const bool jibian = kind == AIRequest::UseCard && pattern == "@@s3_fans_jibian2";
    if (kangdao || jibian) {
        const auto *p = room.getTag(kangdao ? "s3_kangdao" : "s3_fans_jibian").value<ServerPlayer *>();
        if (roster(room, p)) facts.insert(kangdao ? "s3_kangdao" : "s3_fans_jibian_Player",
            p ? QJsonValue(p->objectName()) : QJsonValue(false));
    }
    const bool damageScope = (kind == AIRequest::SkillInvoke && reason == "s3_lianzhan")
        || (kind == AIRequest::UseCard && pattern == "@@s3_huanxiangbenghuai")
        || (kind == AIRequest::Choice && reason == "s3_jxwj_duce");
    if (damageScope) {
        const auto damage = room.getTag("CurrentDamageStruct").value<DamageStruct>();
        if (roster(room, damage.from) && roster(room, damage.to))
            facts.insert("CurrentDamageStruct", damageValue(damage, publicCard));
    }
    merge(request.worldView, facts);
    if (!facts.isEmpty()) context.insert("scarletayuhuo", facts);
}

void projectAIStrategyScarletEventContext(AIWorldView &world, Room &room, ServerPlayer *viewer,
    const AIEventView &event, const PublicCard &publicCard)
{
    if (!viewer || world.self.objectName != viewer->objectName() || !roster(room, viewer)
        || event.triggerEvent != ChoiceMade || event.from != viewer->objectName()
        || event.kind != "choice") return;
    const auto choice = event.details.value("choice_kind").toString();
    const auto fields = event.details.value("prompt_list").toArray();
    const bool invoke = choice == "skillInvoke" && (event.reason == "s_w_qimou" || event.reason == "s2_zhengyi");
    const bool selected = choice == "skillChoice" && event.reason == "s3_jxwj_duce";
    if ((!invoke && !selected) || fields.size() < 3 || fields.at(0).toString() != choice
        || fields.at(1).toString() != event.reason) return;
    // These ChoiceMade notifications are actor-private in the actual producer.
    // This closes only that lawful observer domain, not legacy cross-viewer AI
    // global sharing. Room::damage keeps CurrentDamageStruct until this event.
    const auto damage = room.getTag("CurrentDamageStruct").value<DamageStruct>();
    if (roster(room, damage.from) && roster(room, damage.to))
        merge(world, QJsonObject{{"CurrentDamageStruct", damageValue(damage, publicCard)}});
}
