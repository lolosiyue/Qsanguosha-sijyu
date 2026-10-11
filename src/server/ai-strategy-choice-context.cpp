#include "ai-strategy-choice-context.h"
#include "ai.h"
#include "ai-strategy-public-disclosure.h"
#include "ai-strategy-card-identity.h"
#include "wrapped-card.h"
#include <QObject>
#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include "skill.h"
#include <QJsonArray>
namespace {
bool roster(Room &room, const Player *p)
{ return !p || room.findPlayerByObjectName(p->objectName(), true) == p; }
QObject *agHistory(Room &room, bool create)
{
    auto *ledger=room.findChild<QObject *>(QStringLiteral("isolated-ai-choice-ag-history"),Qt::FindDirectChildrenOnly);
    if (!ledger && create) { ledger=new QObject(&room); ledger->setObjectName(QStringLiteral("isolated-ai-choice-ag-history")); }
    return ledger;
}
QJsonObject agFace(const Card *card, int id)
{
    // Exactly the face the AG notification disclosed. No foreign card flags,
    // private skill chain, source IDs, limitations or action authority.
    return {{"id",id},{"effective_id",id},{"name",card->objectName()},
        {"class_name",card->getClassName()},{"engine_class_name",QString::fromLatin1(card->metaObject()->className())},
        {"kind_of",QJsonArray::fromStringList(card->getKindOfNames())},{"subtype",card->getSubtype()},
        {"suit",int(card->getSuit())},{"number",card->getNumber()},{"type_id",int(card->getTypeId())},
        {"red",card->isRed()},{"black",card->isBlack()},{"virtual_card",false},{"subcards",QJsonArray()},
        {"legacy_string_key",QString::number(id)},{"legacy_sort_key",QString::number(id)}};
}
QJsonObject face(const AICardView &c)
{
    QJsonArray sub; for (int id : c.subcardIds) sub.append(id);
    return {{"id",c.cardId},{"effective_id",c.effectiveId},{"name",c.objectName},
        {"class_name",c.className},{"engine_class_name",c.engineClassName},
        {"kind_of",QJsonArray::fromStringList(c.kindOfNames)},{"suit",c.suit},{"number",c.number},
        {"type_id",c.typeId},{"equip_slot",c.equipSlot},{"virtual_card",c.virtualCard},
        {"subcards",sub},{"red",c.red},{"black",c.black},{"skill_name",c.skillName},
        {"legacy_string_key",c.legacyStringKey},{"legacy_sort_key",c.legacySortKey},
        {"known_flags",c.knownFlags},{"flags_complete",c.flagsComplete}};
}
QJsonObject disclosed(const AIRequest &request, Room &room, ServerPlayer *viewer, int id)
{
    if (id < 0) return {};
    const auto scan = [id](const QList<AICardView> &cards) {
        for (const auto &card : cards) if (card.effectiveId == id) return face(card);
        return QJsonObject();
    };
    for (const auto *cards : {&request.worldView.handCards, &request.worldView.discardPile, &request.choiceOptions.cards}) {
        auto row = scan(*cards); if (!row.isEmpty()) return row;
    }
    const auto player = [&](const AIPlayerView &p) {
        for (const auto *cards : {&p.knownCards,&p.equips,&p.judgingArea}) { auto row=scan(*cards); if (!row.isEmpty()) return row; }
        for (const auto &pile:p.piles) if (pile.open) { auto row=scan(pile.cards); if (!row.isEmpty()) return row; }
        return QJsonObject();
    };
    auto row=player(request.worldView.self); if (!row.isEmpty()) return row;
    for (const auto &p:request.worldView.players) { row=player(p); if (!row.isEmpty()) return row; }
    const auto witness=readAIStrategyPublicDisclosure(room,viewer,id);
    return witness.value("known").toBool() ? witness.value("card").toObject() : QJsonObject();
}
}
void recordAIStrategyChoiceAGDisclosure(Room &room, ServerPlayer *recipient, const QList<int> &ids)
{
    if (!recipient || recipient->getRoom()!=&room || !roster(room,recipient)) return;
    auto *ledger=agHistory(room,true);
    auto viewers=ledger->property("viewers").toJsonObject();
    auto rows=viewers.value(recipient->objectName()).toObject();
    for (int id:ids) {
        if (id<0) continue;
        const Card *card=room.getCard(id);
        if (!card || card->isVirtualCard() || card->getEffectiveId()!=id) continue;
        const auto *owner=room.getCardOwner(id);
        rows.insert(QString::number(id),QJsonObject{{"card",agFace(card,id)},
            {"place",int(room.getCardPlace(id))},{"owner",owner?owner->objectName():QString()},
            {"identity",aiLegacyCardIdentity(room,recipient,card)}});
        // Direct invalidation also prevents identity A->B->A from resurrecting
        // a stale witness. Simple suit/number changes are checked at read time.
        auto watched=ledger->property("watched").toJsonObject();
        const auto key=QString::number(id);
        if (!watched.contains(key)) {
            if (const auto *wrapped=qobject_cast<const WrappedCard *>(card)) {
                QObject::connect(wrapped,&WrappedCard::identityChanged,ledger,
                    [&room,id](){invalidateAIStrategyChoiceAGDisclosure(room,id);},Qt::DirectConnection);
                watched.insert(key,true);ledger->setProperty("watched",watched);
            }
        }
    }
    viewers.insert(recipient->objectName(),rows);ledger->setProperty("viewers",viewers);
}
void invalidateAIStrategyChoiceAGDisclosure(Room &room, int id)
{
    auto *ledger=agHistory(room,false);if (!ledger) return;
    auto viewers=ledger->property("viewers").toJsonObject();
    for (auto it=viewers.begin();it!=viewers.end();++it) {
        auto rows=it.value().toObject();rows.remove(QString::number(id));it.value()=rows;
    }
    ledger->setProperty("viewers",viewers);
}
QJsonObject readAIStrategyChoiceAGDisclosure(Room &room, ServerPlayer *viewer, int id)
{
    const QJsonObject unknown{{"known",false}};
    if (!viewer || viewer->getRoom()!=&room || !roster(room,viewer) || id<0) return unknown;
    auto *ledger=agHistory(room,false);if (!ledger) return unknown;
    const auto row=ledger->property("viewers").toJsonObject().value(viewer->objectName()).toObject().value(QString::number(id)).toObject();
    if (row.isEmpty()) return unknown; // Never inspect a hidden face before proof.
    const auto *card=room.getCard(id);const auto *owner=room.getCardOwner(id);
    if (!card || card->isVirtualCard() || card->getEffectiveId()!=id
        || row.value("place").toInt()!=int(room.getCardPlace(id))
        || row.value("owner").toString()!=(owner?owner->objectName():QString())
        || row.value("identity").toString()!=aiLegacyCardIdentity(room,viewer,card)
        || row.value("card").toObject()!=agFace(card,id)) {
        invalidateAIStrategyChoiceAGDisclosure(room,id);return unknown;
    }
    return {{"known",true},{"card",row.value("card")}};
}
void projectAIStrategyChoiceContext(AIRequest &request, Room &room, ServerPlayer *viewer,
    const QVariant &data, QJsonObject &context, const std::function<QJsonObject(const Card *)> &publicCard)
{
    Q_UNUSED(publicCard);
    if (!viewer || request.viewerObjectName != viewer->objectName() || !roster(room,viewer)) return;
    // Pindian helper may be called while planning any skill. This is the
    // viewer's own finite tag namespace, never another player's private tag.
    QJsonObject tags;
    const auto ownerTags=viewer->getAllTags();
    const QString prefix=QStringLiteral("targetsPindian_");
    for (auto it=ownerTags.cbegin();it!=ownerTags.cend();++it)
        if (it.key().startsWith(prefix)) tags.insert(it.key().mid(prefix.size()),it.value().toString());
    auto pindian=request.worldView.strategyFacts.value("pindian").toObject();
    pindian.insert("owner_target_tags",tags);
    pindian.insert("owner_target_tags_complete",true);
    request.worldView.strategyFacts.insert("pindian",pindian);
    auto competition=request.worldView.strategyFacts.value("mcompetition").toObject();
    competition.insert("qishibusiyutushou_equips",viewer->property("qishibusiyutushouEquips").toString());
    request.worldView.strategyFacts.insert("mcompetition",competition);
    auto body=context.value("choice_body").toObject();
    const auto reason=request.choiceOptions.reason;
    const bool invoke=request.kind==AIRequest::SkillInvoke;
    const bool choice=request.kind==AIRequest::Choice;
    const bool chosen=request.kind==AIRequest::PlayerChosen;
    if ((invoke && (reason=="y_huaiju" || reason=="y_youfang" || reason=="y_zhixi"))
        || (choice && reason=="y_youfang")) {
        // Only the public endpoints are needed, never unrelated move card IDs.
        const auto move=data.value<CardsMoveOneTimeStruct>();
        if (roster(room,move.from) && roster(room,move.to))
            body.insert("shadow_move",QJsonObject{{"from",move.from?move.from->objectName():QString()},
                {"to",move.to?move.to->objectName():QString()}});
    }
    const bool shouju=(invoke && reason=="y_shouju") || (chosen && reason=="y_shouju")
        || (request.kind==AIRequest::RespondCard && request.choiceOptions.question=="askForCard"
            && request.prompt.section(':',0,0)=="y_shouju");
    if (shouju) {
        // The rule asks while this equipment/judge has been publicly discarded.
        const int id=(chosen?room.getTag("y_shouju"):data).toInt();
        auto row=disclosed(request,room,viewer,id);
        if (!row.isEmpty()) body.insert("y_shouju_card",row);
    }
    if (chosen && reason=="y_huaiju") {
        const int id=viewer->getMark("y_huaiju");
        auto row=disclosed(request,room,viewer,id);
        if (!row.isEmpty()) body.insert("y_huaiju_card",row);
    }
    if (chosen && reason=="y_xiaoyi") {
        // Rule 2555 publicly fills then clears AG before this owner tag read.
        // Require surviving lawful disclosed face; a tag alone is not visibility.
        const auto *card=viewer->getTag("y_xiaoyi").value<const Card *>();
        if (!card) body.insert("y_xiaoyi_card",false);
        else {
            auto row=disclosed(request,room,viewer,card->getEffectiveId());
            if (row.isEmpty()) {
                const auto seen=readAIStrategyChoiceAGDisclosure(room,viewer,card->getEffectiveId());
                if (seen.value("known").toBool()) row=seen.value("card").toObject();
            }
            if (!row.isEmpty()) body.insert("y_xiaoyi_card",row);
        }
    }
    if ((chosen || request.kind==AIRequest::CardChosen) && reason=="y_fanjin")
        body.insert("y_fanjin_property",viewer->property("y_fanjin").toString());
    if (choice && reason=="shenyi") body.insert("shenyi_phases",data.toString());
    if (invoke && reason=="caoduo") {
        const auto fields=data.toString().split(':');
        const auto *target=fields.size()>3?room.findPlayerByObjectName(fields.at(3)):nullptr;
        body.insert("caoduo_target",target?QJsonValue(target->objectName()):QJsonValue(false));
    }
    if (invoke && reason=="huanxing") body.insert("huanxing_used",viewer->getTag("huanxing").toBool());
    if (invoke && reason=="xiaode" && data.metaType().id()==QMetaType::QStringList) {
        const auto names=data.toStringList(); body.insert("xiaode_skills",QJsonArray::fromStringList(names));
        QJsonObject skills;
        for (const auto &name:names) {
            const Skill *skill=Sanguosha->getSkill(name); const ViewAsSkill *viewAs=Sanguosha->getViewAsSkill(name);
            const auto *trigger=dynamic_cast<const TriggerSkill *>(skill);
            QJsonObject row{{"exists",skill!=nullptr},{"view_as",viewAs!=nullptr},{"trigger",trigger!=nullptr},
                {"has_cards_move",trigger && trigger->getTriggerEvents().contains(CardsMoveOneTime)}};
            if (skill) row.insert("description",skill->getDescription());
            if (viewAs) row.insert("enabled_at_play",viewAs->isEnabledAtPlay(viewer));
            skills.insert(name,row);
        }
        context.insert("huashen_skills",skills);
    }
    if (request.kind==AIRequest::RespondCard && request.choiceOptions.question=="askForNullification") {
        const auto card=context.value("card").toObject();
        const auto heg=request.worldView.strategyFacts.value("hegemony").toObject();
        if (heg.value("enabled").toBool() && heg.value("original_overrides_loaded").toBool()
            && QSet<QString>{"HAwaitExhausted","HRuleTheWorld","HConquering","HConsolidateCountry","HChaos"}.contains(card.value("class_name").toString())) {
            const auto key=card.value("legacy_string_key").toString();
            if (!key.isEmpty()) {
                QJsonArray targets; bool valid=true;
                for (const auto &item:room.getTag("targets"+key).toList()) {
                    const auto *p=item.value<ServerPlayer *>();
                    if (!p || !roster(room,p)) { valid=false; break; }
                    targets.append(p->objectName());
                }
                if (valid) {
                    if (card.value("class_name") == "HAwaitExhausted")
                        body.insert("await_exhausted",QJsonObject{{"targets",targets},
                            {"nullifying_times",room.getTag("NullifyingTimes").toInt()},
                            {"nullification_type",room.getTag("NullificatonType").toBool()}});
                    else context.insert("original26_nullification",QJsonObject{{"targets",targets},
                        {"times",room.getTag("NullifyingTimes").toInt()},
                        {"type",room.getTag("NullificatonType").toBool()}});
                }
            }
        }
    }
    if (!body.isEmpty()) context.insert("choice_body",body);
}
