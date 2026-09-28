#include "mobile-strengthen.h"
#include "settings.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "yjcm.h"
#include "yjcm2012.h"
#include "yjcm2013.h"
#include "yjcm2014.h"
#include "mountain.h"
#include "skill-instance-utils.h"
#include <QScopeGuard>
#include <memory>
#include <climits>
//#include "json.h"

class MobileStrengthenActiveQuota : public TriggerSkillV2
{
public:
    explicit MobileStrengthenActiveQuota(const QString &skillName)
        : TriggerSkillV2("#" + skillName + "-quota"), activeName(skillName)
    {
        events << EventSkillInvoking;
        global = true;
    }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const SkillContext accepted = data.value<SkillContext>();
        if (!accepted.bypass_cost || accepted.skill_name != activeName || accepted.executionID == 0
            || !accepted.use_card || !accepted.sourceRef.isValid() || !accepted.activationRef.isValid()) return true;
        const ViewAsSkillV2 *skill = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(activeName));
        if (!skill) return true;
        const SkillInstanceRef ref = skill->getUsageRef(accepted);
        ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!ref.isValid() || !holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return true;
        // These custom quotas insert into an exact-instance set, so repeated acceptance cannot double-charge.
        // A borrowed or granted activation may have a different name from its frozen source.
        skill->addUsage(accepted);
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
private:
    QString activeName;
};

class MobileLiyong : public TriggerSkillV2
{
public:
    MobileLiyong() : TriggerSkillV2("mobileliyong") { global=true; frequency=Compulsory; events << CardOffset << TargetSpecified << ConfirmDamage << Damage << CardFinished; }
    static QVariantMap receipt(Room *room,const SkillContext &ctx)
    {
        const int serial=room->getTag("MobileLiyongSerial").toInt()+1; room->setTag("MobileLiyongSerial",serial);
        return {{"serial",serial},{"owner",ctx.activationRef.ownerObjectName},{"skill",ctx.activationRef.key.skillName},{"instance",ctx.activationRef.key.instanceID},
            {"source_owner",ctx.sourceRef.ownerObjectName},{"source_skill",ctx.sourceRef.key.skillName},{"source_instance",ctx.sourceRef.key.instanceID},
            {"actor",ctx.invoker->objectName()},{"turn",room->historyScopes().value("turn_id")},{"phase",room->historyScopes().value("phase_id")},{"amount",ctx.amount}};
    }
    bool recordEvent(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data) const override
    {
        if(event==CardFinished) { const Card *card=data.value<CardUseStruct>().card; if(card) { card->removeTag("MobileLiyongPending"); card->removeTag("MobileLiyongApplied"); } return false; }
        if(event!=TargetSpecified || !actor || actor->getPhase()!=Player::Play) return false;
        const CardUseStruct use=data.value<CardUseStruct>(); if(!use.card || !use.card->isKindOf("Slash")) return false;
        QVariantList kept,due=use.card->getTag("MobileLiyongPending").toList();
        for(const QVariant &value:actor->getTag("MobileLiyongPending").toList()) {
            if(value.toMap().value("phase")==room->historyScopes().value("phase_id")) { QVariantMap frozen=value.toMap(); frozen["use_id"]=room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id"); due << frozen; } else kept << value;
        }
        // Bind the privilege to this exact Slash before any optional interception can cancel it.
        actor->setTag("MobileLiyongPending",kept); use.card->setTag("MobileLiyongPending",due);
        room->setPlayerMark(actor,"&mobileliyong-PlayClear",kept.isEmpty()?0:1); return false;
    }
    TriggerList triggerable(TriggerEvent event,Room *,ServerPlayer *actor,QVariant &data) const override
    {
        if(event!=CardOffset || !actor || actor->isDead() || actor->getPhase()!=Player::Play || !actor->hasSkill(objectName())) return {};
        const CardEffectStruct effect=data.value<CardEffectStruct>(); return effect.from==actor && effect.card && effect.card->isKindOf("Slash")?TriggerList{{actor,{objectName()}}}:TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data,QList<SkillContext> &contexts) const override
    {
        if(event==CardOffset) return false;
        if(!actor || actor->isDead() || event==CardFinished) return true;
        const Card *card=event==TargetSpecified?data.value<CardUseStruct>().card:data.value<DamageStruct>().card; if(!card) return true;
        const QVariantList receipts=card->getTag(event==TargetSpecified?"MobileLiyongPending":"MobileLiyongApplied").toList();
        for(const QVariant &value:receipts) {
            const QVariantMap r=value.toMap();
            const qint64 useId=room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id").toLongLong();
            if(useId<=0 || r.value("use_id").toLongLong()!=useId || r.value("actor").toString()!=actor->objectName()) continue;
            ServerPlayer *target=actor;
            if(event!=TargetSpecified) {
                const DamageStruct d=data.value<DamageStruct>();
                if(!d.to || d.to->isDead() || r.value("target").toString()!=d.to->objectName()) continue;
                target=event==ConfirmDamage?d.to:actor;
            }
            SkillContext ctx; ctx.skill_name=objectName(); ctx.owner=room->findPlayerByObjectName(r.value("owner").toString(),true); ctx.initiator=actor; ctx.invoker=actor;
            ctx.instanceID=r.value("serial").toInt(); ctx.sourceRef=SkillInstanceRef(r.value("source_owner").toString(),SkillInstanceKey(r.value("source_skill").toString(),r.value("source_instance").toInt()));
            ctx.extra_data=r; ctx.amount=r.value("amount").toInt(); ctx.is_forced=true; ctx.original_data=&data; ctx.current_event=event; ctx.targets={target}; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room,const SkillContext &ctx) const override
    {
        if(ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room,ctx);
        const Card *card=ctx.current_event==TargetSpecified?ctx.original_data->value<CardUseStruct>().card:ctx.original_data->value<DamageStruct>().card;
        return card && card->getTag(ctx.current_event==TargetSpecified?"MobileLiyongPending":"MobileLiyongApplied").toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event,Room *,ServerPlayer *,SkillContext &ctx) const override { if(event==CardOffset) ctx.targets={ctx.owner}; return true; }
    bool effect(TriggerEvent event,Room *room,ServerPlayer *actor,SkillContext &ctx) const override
    {
        if(event!=TargetSpecified) return false; ctx.manual_effect=true;
        const QList<ServerPlayer *> targets=ctx.original_data->value<CardUseStruct>().to;
        for(ServerPlayer *target:targets) { SkillContext local=ctx; local.choice="invalidate"; skillEffect(event,room,actor,local,target); }
        // No-response applies to every potential responder, including third-party Nullification.
        for(ServerPlayer *target:room->getAlivePlayers()) { SkillContext local=ctx; local.choice="no_response"; skillEffect(event,room,actor,local,target); }
        return false;
    }
    bool effectTarget(TriggerEvent event,Room *room,ServerPlayer *,SkillContext &ctx,ServerPlayer *target) const override
    {
        const int amount=getEffectiveAmount(ctx); if(amount<=0) return false;
        if(event==CardOffset) {
            QVariantMap r=receipt(room,ctx); r["amount"]=amount; r["actor"]=target->objectName(); QVariantList pending=target->getTag("MobileLiyongPending").toList();
            // Multiple dodges arm one next-Slash effect per exact activation, not multiple copies.
            for(int i=pending.size()-1;i>=0;--i) if(pending[i].toMap().value("owner")==r.value("owner") && pending[i].toMap().value("instance")==r.value("instance") && pending[i].toMap().value("phase")==r.value("phase")) pending.removeAt(i);
            pending << r; target->setTag("MobileLiyongPending",pending); room->setPlayerMark(target,"&mobileliyong-PlayClear",1); return false;
        }
        if(event==ConfirmDamage) { DamageStruct d=ctx.original_data->value<DamageStruct>(); if(d.to==target) { d.damage+=amount; *ctx.original_data=QVariant::fromValue(d); } return false; }
        if(event==Damage) { room->loseHp(HpLostStruct(target,amount,objectName(),ctx.invoker)); return false; }
        CardUseStruct use=ctx.original_data->value<CardUseStruct>();
        if(ctx.choice=="no_response") { if(!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName(); *ctx.original_data=QVariant::fromValue(use); return false; }
        if(!use.to.contains(target)) return false;
        QVariantMap r=ctx.extra_data.toMap(); r["target"]=target->objectName(); r["amount"]=amount;
        QVariantList applied=use.card->getTag("MobileLiyongApplied").toList();
        for(const QVariant &value:applied) if(value.toMap().value("serial")==r.value("serial") && value.toMap().value("target")==r.value("target")) return false;
        applied << r; use.card->setTag("MobileLiyongApplied",applied);
        QVariantList restrictions=room->getTag("MobileLiyongRestrictions").toList(); restrictions << r; room->setTag("MobileLiyongRestrictions",restrictions);
        const QVariant serial=r.value("serial"); const QString recipient=target->objectName();
        room->setPlayerMarkWithReceipt(target,"@skill_invalidity",target->getMark("@skill_invalidity")+1,
            [room,serial,recipient](const QString &mark,int before,int after) {
                if(mark!="@skill_invalidity" || after<before) return false;
                for(const QVariant &value:room->getTag("MobileLiyongRestrictions").toList()) if(value.toMap().value("serial")==serial && value.toMap().value("target").toString()==recipient) return true;
                return false;
            },[room,serial,recipient](const QString &,int before,int after) {
                QVariantList current=room->getTag("MobileLiyongRestrictions").toList();
                for(int i=0;i<current.size();++i) { QVariantMap receipt=current[i].toMap(); if(receipt.value("serial")==serial && receipt.value("target").toString()==recipient) { receipt["delta"]=after-before; current[i]=receipt; break; } }
                room->setTag("MobileLiyongRestrictions",current);
            }); return false;
    }
};

class MobileLiyongClear : public TriggerSkillV2
{
public:
    MobileLiyongClear() : TriggerSkillV2("#mobileliyong-clear") { global=true; events << EventPhaseChanging << Death << TurnBroken; }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 5; }
    bool recordEvent(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data) const override
    {
        if(!actor) return false;
        const bool death=event==Death && data.value<DeathStruct>().who==actor && actor->getPhase()==Player::Play;
        const bool end=event==TurnBroken || (event==EventPhaseChanging && data.value<PhaseChangeStruct>().to==Player::NotActive);
        if(!death && !end) return false;
        const QVariant turn=room->historyScopes().value("turn_id"); QVariantList kept,expired;
        for(const QVariant &value:room->getTag("MobileLiyongRestrictions").toList()) {
            if(value.toMap().value("turn")==turn) expired << value; else kept << value;
        }
        room->setTag("MobileLiyongRestrictions",kept);
        for(ServerPlayer *player:room->getAllPlayers(true)) {
            QVariantList pending; for(const QVariant &value:player->getTag("MobileLiyongPending").toList()) if(value.toMap().value("turn")!=turn) pending << value;
            player->setTag("MobileLiyongPending",pending); room->setPlayerMark(player,"&mobileliyong-PlayClear",pending.isEmpty()?0:1);
        }
        // Publish remaining ownership first: removal callbacks may create another exact restriction.
        for(const QVariant &value:expired) if(ServerPlayer *target=room->findPlayerByObjectName(value.toMap().value("target").toString(),true)) room->removePlayerMark(target,"@skill_invalidity",value.toMap().value("delta").toInt());
        if(!expired.isEmpty()) {
            for(ServerPlayer *player:room->getAllPlayers(true)) room->filterCards(player,player->getCards("he"),false);
            JsonArray args; args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL; room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT,args);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent,Room *,ServerPlayer *,QVariant &) const override { return {}; }
};

MobileQingjianCard::MobileQingjianCard()
{
    setSkillName("mobileqingjian");
    will_throw = false;
    handling_method = Card::MethodNone;
}

void MobileQingjianCard::onEffect(CardEffectStruct &effect) const
{
    if (effect.from->isDead() || effect.to->isDead()) return;
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "mobileqingjian", "");
    effect.from->getRoom()->obtainCard(effect.to, this, reason, false);
	effect.from->addMark("mobileqingjian_num",subcardsLength());
}

class MobileQingjianVS : public ViewAsSkillV2
{
public:
    static int committedGifts(Room *room,const QString &from,const QString &to,const QList<int> &ids,const QVariant &after,qint64 cause)
    {
        if(cause<=0) return 0; QSet<int> moved;
        QVariantMap filter{{"from",from},{"to",to},{"after",after},{"limit",128}};
        for(;;) {
            const QVariantMap page=room->queryHistoryMoves(filter); if(page.contains("error") || !page.value("complete").toBool()) return 0;
            for(const QVariant &value:page.value("items").toList()) {
                const QVariantMap fact=value.toMap(),data=fact.value("data").toMap(); const int id=data.value("card_id",-1).toInt();
                if(ids.contains(id) && data.value("to_place").toInt()==int(Player::PlaceHand) && room->historyEvent(fact.value("event_id").toLongLong()).value("parent_id").toLongLong()==cause) moved.insert(id);
            }
            if(!page.value("has_more").toBool()) return moved.size(); filter["after"]=page.value("next_after"); filter["watermark"]=page.value("watermark");
        }
    }
    MobileQingjianVS() : ViewAsSkillV2("mobileqingjian",99999) { expand_pile="mobileqingjian"; response_pattern="@@mobileqingjian!"; }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileQingjianCard"; }
    bool canActivate(const ActiveSkillRequest &r) const override { return r.initiator && r.pattern=="@@mobileqingjian!" && !r.initiator->property("mobileqingjian_ids").toString().isEmpty(); }
    bool canSelectCard(const ActiveSkillRequest &r,const Card *card) const override
    { return card && r.initiator && !card->hasFlag("using") && r.initiator->getPile("mobileqingjian").contains(card->getEffectiveId()) && r.initiator->property("mobileqingjian_ids").toString().split('+').contains(QString::number(card->getEffectiveId())); }
    bool cardSelectionFeasible(const ActiveSkillRequest &r) const override
    { if(r.selectedCardIds.isEmpty()) return false; for(int id:r.selectedCardIds) if(!canSelectCard(r,Sanguosha->getCard(id))) return false; return true; }
    bool canSelectTarget(const ActiveSkillRequest &r,const QList<const Player *> &selected,const Player *target) const override
    { return target && target->isAlive() && target!=r.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &,const QList<const Player *> &targets) const override { return targets.size()==1; }
    EffectFlow effectOnTarget(SkillContext &ctx,ServerPlayer *target) const override
    {
        if(getEffectiveAmount(ctx)<=0 || !ctx.initiator || !ctx.use_card) return ContinueEffects;
        const QList<int> ids=ctx.use_card->getSubcards(); const QStringList allowed=ctx.initiator->property("mobileqingjian_ids").toString().split('+');
        QSet<int> unique; for(int id:ids) { if(unique.contains(id) || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects; unique.insert(id); }
        for(int id:ids) if(!ctx.initiator->getPile("mobileqingjian").contains(id) || !allowed.contains(QString::number(id))) return ContinueEffects;
        if(ids.isEmpty()) return ContinueEffects;
        Room *room=target->getRoom(); const QVariant before=room->queryHistoryMoves({{"limit",1}}).value("watermark"); const qint64 cause=room->currentHistoryEventId();
        DummyCard cards(ids); room->obtainCard(target,&cards,CardMoveReason(CardMoveReason::S_REASON_GIVE,ctx.initiator->objectName(),target->objectName(),objectName(),""),false);
        QVariantMap state=ctx.initiator->getTag("MobileQingjianDistribution").toMap(); state["given"]=state.value("given").toInt()+committedGifts(room,ctx.initiator->objectName(),target->objectName(),ids,before,cause); ctx.initiator->setTag("MobileQingjianDistribution",state); return ContinueEffects;
    }
};

class MobileQingjian : public TriggerSkillV2
{
public:
    MobileQingjian() : TriggerSkillV2("mobileqingjian") { global=true; events << CardsMoveOneTime << EventPhaseChanging << EventSkillInvoking << TurnBroken; view_as_skill=new MobileQingjianVS; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    bool prepareSource(Room *room,SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room,ctx) && (!ctx.activationRef.isValid() || isUsable(ctx)); }
    bool recordEvent(TriggerEvent event,Room *room,ServerPlayer *,QVariant &data) const override
    {
        if(event==EventSkillInvoking) {
            const SkillContext accepted=data.value<SkillContext>();
            if(accepted.bypass_cost && accepted.skill_name==objectName() && accepted.executionID==0 && accepted.activationRef.isValid() && accepted.activationRef.key.skillName==objectName()) addUsage(accepted);
        } else if(event==TurnBroken || (event==EventPhaseChanging && data.value<PhaseChangeStruct>().to==Player::NotActive)) {
            QVariantList kept,due=room->getTag("MobileQingjianDue").toList();
            for(const QVariant &value:room->getTag("MobileQingjianReceipts").toList()) {
                if(value.toMap().value("turn")==room->historyScopes().value("turn_id")) due << value; else kept << value;
            }
            room->setTag("MobileQingjianReceipts",kept); room->setTag("MobileQingjianDue",due);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data) const override
    {
        if(event!=CardsMoveOneTime || !actor || actor->isDead() || !actor->hasSkill(objectName()) || actor->getPhase()==Player::Draw || actor->isKongcheng() || room->getTag("FirstRound").toBool() || !room->hasCurrent()) return {};
        const CardsMoveOneTimeStruct move=data.value<CardsMoveOneTimeStruct>(); return move.to==actor && move.to_place==Player::PlaceHand?TriggerList{{actor,{objectName()}}}:TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event,Room *room,ServerPlayer *,QVariant &data,QList<SkillContext> &contexts) const override
    {
        if(event!=EventPhaseChanging && event!=TurnBroken) return false;
        if(event!=TurnBroken && data.value<PhaseChangeStruct>().to!=Player::NotActive) return true;
        for(const QVariant &value:room->getTag("MobileQingjianDue").toList()) {
            const QVariantMap r=value.toMap(); if(r.value("turn")!=room->historyScopes().value("turn_id")) continue;
            ServerPlayer *target=room->findPlayerByObjectName(r.value("target").toString()); if(!target || target->isDead()) continue;
            SkillContext ctx; ctx.skill_name=objectName(); ctx.owner=room->findPlayerByObjectName(r.value("owner").toString(),true); ctx.initiator=target; ctx.invoker=target; ctx.targets={target};
            ctx.sourceRef=SkillInstanceRef(r.value("source_owner").toString(),SkillInstanceKey(r.value("source_skill").toString(),r.value("source_instance").toInt()));
            ctx.instanceID=r.value("serial").toInt(); ctx.amount=r.value("amount").toInt(); ctx.extra_data=r; ctx.is_forced=true; ctx.current_event=event; ctx.original_data=&data; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room,const SkillContext &ctx) const override
    { return ctx.activationRef.isValid()?TriggerSkillV2::isSourceAvailable(room,ctx):room->getTag("MobileQingjianDue").toList().contains(ctx.extra_data); }
    bool cost(TriggerEvent event,Room *room,ServerPlayer *,SkillContext &ctx) const override
    {
        if(event==EventPhaseChanging || event==TurnBroken) return true;
        const Card *cards=room->askForExchange(ctx.owner,objectName(),99999,1,false,"@mobileqingjian-put",true); if(!cards) return false;
        QVariantList ids; for(int id:cards->getSubcards()) ids << id; ctx.extra_data=ids; ctx.initiator=ctx.owner; ctx.targets={ctx.owner}; return !ids.isEmpty();
    }
    bool pay(TriggerEvent event,Room *,ServerPlayer *,SkillContext &ctx) const override
    { if(event==EventPhaseChanging || event==TurnBroken) return true; if(!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effectTarget(TriggerEvent event,Room *room,ServerPlayer *actor,SkillContext &ctx,ServerPlayer *target) const override
    {
        if(getEffectiveAmount(ctx)<=0) return false;
        if(ctx.choice=="reward") { target->drawCards(getEffectiveAmount(ctx),objectName()); return false; }
        if(ctx.choice=="give") {
            QList<int> ids; for(const QVariant &value:ctx.extra_data.toMap().value("remaining").toList()) if(!ids.contains(value.toInt()) && !Sanguosha->getCard(value.toInt())->hasFlag("using") && ctx.initiator->getPile(objectName()).contains(value.toInt())) ids << value.toInt();
            if(ids.isEmpty()) return false;
            const QVariant before=room->queryHistoryMoves({{"limit",1}}).value("watermark"); const qint64 cause=room->currentHistoryEventId();
            DummyCard cards(ids); room->obtainCard(target,&cards,CardMoveReason(CardMoveReason::S_REASON_GIVE,ctx.initiator->objectName(),target->objectName(),objectName(),""),false);
            QVariantMap state=ctx.initiator->getTag("MobileQingjianDistribution").toMap(); state["given"]=state.value("given").toInt()+MobileQingjianVS::committedGifts(room,ctx.initiator->objectName(),target->objectName(),ids,before,cause); ctx.initiator->setTag("MobileQingjianDistribution",state); return false;
        }
        if(event!=EventPhaseChanging && event!=TurnBroken) {
            QList<int> ids; QVariantList material; for(const QVariant &value:ctx.extra_data.toList()) if(!ids.contains(value.toInt()) && room->getCardOwner(value.toInt())==ctx.initiator && !Sanguosha->getCard(value.toInt())->hasFlag("using") && ctx.initiator->handCards().contains(value.toInt())) { ids << value.toInt(); material << value; }
            if(ids.isEmpty()) return false;
            const int serial=room->getTag("MobileQingjianSerial").toInt()+1; room->setTag("MobileQingjianSerial",serial);
            QVariantMap r{{"serial",serial},{"owner",ctx.activationRef.ownerObjectName},{"skill",ctx.activationRef.key.skillName},{"instance",ctx.activationRef.key.instanceID},
                {"source_owner",ctx.sourceRef.ownerObjectName},{"source_skill",ctx.sourceRef.key.skillName},{"source_instance",ctx.sourceRef.key.instanceID},
                {"target",target->objectName()},{"turn",room->historyScopes().value("turn_id")},{"amount",getEffectiveAmount(ctx)},{"ids",material}};
            QVariantList pending=room->getTag("MobileQingjianReceipts").toList(); pending << r; room->setTag("MobileQingjianReceipts",pending); target->addToPile(objectName(),ids); return false;
        }
        QVariantList due=room->getTag("MobileQingjianDue").toList(); due.removeAll(ctx.extra_data); room->setTag("MobileQingjianDue",due);
        const QVariantMap r=ctx.extra_data.toMap(); const QVariant priorIds=target->property("mobileqingjian_ids"),priorState=target->getTag("MobileQingjianDistribution");
        const auto restore=qScopeGuard([&]{ target->setProperty("mobileqingjian_ids",priorIds); room->notifyProperty(target,target,"mobileqingjian_ids"); target->setTag("MobileQingjianDistribution",priorState); });
        target->setTag("MobileQingjianDistribution",QVariantMap{{"serial",r.value("serial")},{"given",0}});
        SkillContext accepted=ctx; accepted.activationRef=SkillInstanceRef(r.value("owner").toString(),SkillInstanceKey(r.value("skill").toString(),r.value("instance").toInt()));
        Room::AcceptedViewAsEffectScope prompt(room,target,objectName(),accepted);
        while(target->isAlive()) {
            QList<int> remaining; for(const QVariant &value:r.value("ids").toList()) if(target->getPile(objectName()).contains(value.toInt())) remaining << value.toInt();
            if(remaining.isEmpty() || room->getOtherPlayers(target).isEmpty()) break;
            target->setProperty("mobileqingjian_ids",ListI2S(remaining).join('+')); room->notifyProperty(target,target,"mobileqingjian_ids");
            if(!prompt.isValid() || !room->askForUseCard(target,"@@mobileqingjian!","@mobileqingjian",-1,Card::MethodNone)) {
                SkillContext gift=ctx; gift.initiator=target; gift.choice="give"; QVariantMap transfer=r; QVariantList ids; for(int id:remaining) ids << id; transfer["remaining"]=ids; gift.extra_data=transfer;
                const QList<ServerPlayer *> others=room->getOtherPlayers(target); skillEffect(event,room,actor,gift,others.at(qsanRandomBounded(others.size())));
            }
            bool moved=false; for(int id:remaining) if(!target->getPile(objectName()).contains(id)) moved=true;
            if(!moved) break; // A canceled transfer cannot create an unbounded mandatory prompt loop.
        }
        if(target->isAlive() && target->getTag("MobileQingjianDistribution").toMap().value("given").toInt()>1) { SkillContext reward=ctx; reward.choice="reward"; skillEffect(event,room,actor,reward,target); }
        return false;
    }
};

class MobileFenji : public TriggerSkillV2
{
public:
    MobileFenji() : TriggerSkillV2("mobilefenji")
    {
        events << EventPhaseChanging;
        m_baseAmount = 2;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->isKongcheng()
            || data.value<PhaseChangeStruct>().to != Player::NotActive) return result;
        for (ServerPlayer *owner : room->getAllPlayers())
            if (owner->isAlive() && owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // The turn-ending player is the event subject, not the skill owner.
        if (!ctx.owner || !ctx.invoker || !ctx.invoker->isAlive() || !ctx.invoker->isKongcheng()
            || !ctx.owner->askForSkillInvoke(this, ctx.invoker)) return false;
        ctx.targets = {ctx.invoker};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.owner->peiyin(this);
        // Preserve the mobile variant's order: draw first, then lose HP.
        target->drawCards(getEffectiveAmount(ctx), objectName());
        room->loseHp(HpLostStruct(ctx.owner, 1, objectName(), ctx.owner));
        return false;
    }
};

MobileQiangxiCard::MobileQiangxiCard()
{
    setSkillName("mobileqiangxi");
}

bool MobileQiangxiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return Self->inMyAttackRange(to_select, subcards) && targets.isEmpty()
	&& to_select != Self && to_select->getMark("mobileqiangxi_used-PlayClear") <= 0;
}

void MobileQiangxiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    room->addPlayerMark(effect.to, "mobileqiangxi_used-PlayClear");

    if (subcards.isEmpty())
        room->loseHp(HpLostStruct(effect.from, 1, "mobileqiangxi", effect.from));

    room->damage(DamageStruct("mobileqiangxi", effect.from, effect.to));
}

class MobileQiangxi : public ViewAsSkillV2
{
public:
    MobileQiangxi() : ViewAsSkillV2("mobileqiangxi", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileQiangxiCard"; }
    static QStringList usedTargets(const Player *holder,const Player *actor,const SkillInstanceRef &ref)
    {
        if(const ServerPlayer *server=qobject_cast<const ServerPlayer *>(actor)) {
            const QString phase=server->getRoom()->historyScopes().value("phase_id").toString();
            return holder->getSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,"mobileqiangxi_phases").toMap().value(phase).toStringList();
        }
        return holder->getSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,"mobileqiangxi_phase_targets").toStringList();
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty() && card && !card->isVirtualCard()
            && !card->hasFlag("using") && card->isKindOf("Weapon") && !request.initiator->isJilei(card)
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId()));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return request.selectedCardIds.size() <= 1; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        if (!request.initiator || !target || target == request.initiator || !selected.isEmpty()
            || !request.activationRef.isValid() || !request.initiator->inMyAttackRange(target, request.selectedCardIds)) return false;
        for (const Player *holder : request.initiator->getSiblings(true))
            if (holder->objectName() == request.activationRef.ownerObjectName)
                return !usedTargets(holder,request.initiator,request.activationRef).contains(target->objectName());
        return false;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.initiator || !ref.isValid()) return false;
        const ServerPlayer *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return false;
        const QStringList used = usedTargets(holder,ctx.initiator,ref);
        for (ServerPlayer *target : ctx.targets)
            if (target && used.contains(target->objectName())) return false;
        return true;
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.initiator || !ref.isValid()) return;
        ServerPlayer *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return;
        QStringList used = usedTargets(holder,ctx.initiator,ref);
        for (ServerPlayer *target : ctx.targets)
            if (target && !used.contains(target->objectName())) used << target->objectName();
        QVariantMap phases=holder->getSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,"mobileqiangxi_phases").toMap();
        phases[ctx.initiator->getRoom()->historyScopes().value("phase_id").toString()]=used;
        holder->setSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,"mobileqiangxi_phases",phases);
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "mobileqiangxi_phase_targets", used);
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.initiator || !ref.isValid()) return;
        ServerPlayer *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if(holder) { QVariantMap phases=holder->getSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,"mobileqiangxi_phases").toMap();
            phases.remove(ctx.initiator->getRoom()->historyScopes().value("phase_id").toString());
            holder->setSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,"mobileqiangxi_phases",phases); holder->removeSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,"mobileqiangxi_phase_targets"); }
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || request.selectedCardIds.size() > 1 || !checkCustomUsage(ctx)) return false;
        for (int id : request.selectedCardIds)
            if (room->getCardOwner(id) != ctx.initiator
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || !Sanguosha->getCard(id)->isKindOf("Weapon") || !ctx.initiator->canDiscard(ctx.initiator, id)) return false;
        // Reserve the selected recipient before either cost can dispatch nested skill activations.
        addUsage(ctx);
        if (request.selectedCardIds.isEmpty())
            room->loseHp(HpLostStruct(ctx.initiator, 1, objectName(), ctx.initiator));
        else {
            DummyCard weapon(request.selectedCardIds);
            room->throwCard(&weapon, ctx.initiator, nullptr);
        }
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.invoker->getRoom()->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }
};

class MobileQiangxiClear : public TriggerSkillV2
{
public:
    MobileQiangxiClear() : TriggerSkillV2("#mobileqiangxi-clear")
    {
        events << EventPhaseChanging;
        global = true;
    }
    bool recordEvent(TriggerEvent,Room *room,ServerPlayer *,QVariant &) const override
    {
        for(ServerPlayer *holder:room->getAllPlayers(true)) for(const SkillInstance &instance:holder->getSkillInstances()) {
            QVariantMap phases=holder->getSkillInstanceStateValue(instance.skillName,instance.instanceID,"mobileqiangxi_phases").toMap(); if(phases.isEmpty()) continue;
            for(auto it=phases.begin();it!=phases.end();) { if(room->historyEvent(it.key().toLongLong()).value("status").toString()!="active") it=phases.erase(it); else ++it; }
            holder->setSkillInstanceStateValue(instance.skillName,instance.instanceID,"mobileqiangxi_phases",phases);
            holder->setSkillInstanceStateValue(instance.skillName,instance.instanceID,"mobileqiangxi_phase_targets",phases.value(room->historyScopes().value("phase_id").toString()).toStringList());
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent,Room *,ServerPlayer *,QVariant &) const override { return {}; }
};
class MobileJieming : public TriggerSkillV2
{
public:
    MobileJieming() : TriggerSkillV2("mobilejieming")
    {
        events << Damaged;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const int damage = data.value<DamageStruct>().damage;
        if (!player || player->isDead() || !player->hasSkill(objectName()) || damage <= 0) return {};
        // Each point is a separate invocation, with its own target interception.
        return {{player, {objectName() + "*" + QString::number(damage)}}};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(),
            "@mobilejieming-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(this);
        target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        if (target->getHandcardNum() < target->getMaxHp() && ctx.owner->isAlive())
            ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

MobileNiepanCard::MobileNiepanCard()
{
    setSkillName("mobileniepan");
    target_fixed = true;
}

void MobileNiepanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->doSuperLightbox(source, "mobileniepan");

    room->removePlayerMark(source, "@mobileniepanMark");

    source->throwAllHandCardsAndEquips("mobileniepan");
    foreach (const Card *trick, source->getJudgingArea()) {
        CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, source->objectName(),"mobileniepan","");
        room->throwCard(trick, reason, nullptr);
    }

    source->drawCards(3, "mobileniepan");

    int n = qMin(3 - source->getHp(), source->getMaxHp() - source->getHp());
    if (n > 0)
        room->recover(source, RecoverStruct(source, nullptr, n, "mobileniepan"));

    if (source->isChained())
        room->setPlayerChained(source);

    if (!source->faceUp())
        source->turnOver();
}

static void resolveMobileNiepan(Room *room, ServerPlayer *owner, ServerPlayer *target, int amount)
{
    target->throwAllHandCardsAndEquips("mobileniepan");
    for (const Card *trick : target->getJudgingArea()) {
        CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, target->objectName(), "mobileniepan", "");
        room->throwCard(trick, reason, nullptr);
    }
    if (target->isDead()) return;
    target->drawCards(amount, "mobileniepan");
    if (target->isDead()) return;
    const int recovery = qMin(amount - target->getHp(), target->getLostHp());
    if (recovery > 0) room->recover(target, RecoverStruct(owner, nullptr, recovery, "mobileniepan"));
    if (target->isDead()) return;
    if (target->isChained()) room->setPlayerChained(target, false);
    if (!target->faceUp()) target->turnOver();
}

class MobileNiepanVS : public ViewAsSkillV2
{
public:
    MobileNiepanVS() : ViewAsSkillV2("mobileniepan")
    {
        frequency = Limited;
        limit_mark = "@mobileniepanMark";
        m_baseAmount = 3;
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileNiepanCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        room->removePlayerMark(ctx.initiator, "@mobileniepanMark");
        room->doSuperLightbox(ctx.initiator, objectName());
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        // A target-fixed self effect still passes through the common recipient interception.
        ctx.manual_effect = true;
        ctx.targets = {ctx.invoker};
        return skillEffect(ctx, ctx.invoker);
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        resolveMobileNiepan(ctx.invoker->getRoom(), ctx.invoker, target, getEffectiveAmount(ctx));
        return ContinueEffects;
    }
};

class MobileNiepan : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    MobileNiepan() : TriggerSkillV2("mobileniepan")
    {
        events << AskForPeaches << EventSkillInvoking;
        frequency = Limited;
        limit_mark = "@mobileniepanMark";
        m_baseAmount = 3;
        view_as_skill = new MobileNiepanVS;
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        // Active entries commit their own quota; only this exact trigger's bypass path remains.
        if (accepted.executionID == 0 && accepted.bypass_cost && accepted.activationRef == ctx.activationRef
            && accepted.sourceRef == ctx.sourceRef && parseSkillName(accepted.skill_name) == objectName())
            addUsage(accepted);
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {};
        const DyingStruct dying = data.value<DyingStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && dying.who == player
            && player->getHp() <= 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Active and dying entry points consume the same activation instance's game quota.
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        room->removePlayerMark(ctx.owner, "@mobileniepanMark");
        room->doSuperLightbox(ctx.owner, objectName());
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        resolveMobileNiepan(room, ctx.owner, target, getEffectiveAmount(ctx));
        return false;
    }
};

class MobileShuangxiongVS : public ViewAsSkillV2
{
public:
    MobileShuangxiongVS() : ViewAsSkillV2("mobileshuangxiong",1) { setResponseOrUse(true); }
    QString historyKey(const ActiveSkillRequest &) const override { return "Duel"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.activationRef.isValid() && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->getSkillInstanceStateValue(objectName(),request.activationRef.key.instanceID,"colors").toStringList().isEmpty();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && request.activationRef.isValid() && card && request.selectedCardIds.isEmpty()
            && request.initiator->handCards().contains(card->getEffectiveId()) && !card->hasFlag("using")
            && !request.initiator->getSkillInstanceStateValue(objectName(),request.activationRef.key.instanceID,"colors").toStringList().contains(card->getColorString());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { if (request.selectedCardIds.size()!=1) return false; ActiveSkillRequest prefix=request; prefix.selectedCardIds.clear(); return canSelectCard(prefix,Sanguosha->getCard(request.selectedCardIds.first())); }
    bool willThrowSelectedCards() const override { return false; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr; const Card *material=Sanguosha->getCard(request.selectedCardIds.first());
        auto *duel=new Duel(material->getSuit(),material->getNumber()); duel->addSubcard(material); duel->setSkillName(objectName()); return duel;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.use_card || !ctx.invoker) return FinishSkill;
        // Freeze the accepted conversion, so its follow-up does not require the original grant.
        ctx.use_card->setTag("MobileShuangxiongSource", QVariantMap{{"owner",ctx.activationRef.ownerObjectName},{"skill",ctx.activationRef.key.skillName},{"instance",ctx.activationRef.key.instanceID},
            {"actor",ctx.invoker->objectName()},{"source_owner",ctx.sourceRef.ownerObjectName},{"source_skill",ctx.sourceRef.key.skillName},{"source_instance",ctx.sourceRef.key.instanceID},{"amount",getEffectiveAmount(ctx)}});
        return ContinueEffects;
    }
};

class MobileShuangxiong : public TriggerSkillV2
{
public:
    MobileShuangxiong() : TriggerSkillV2("mobileshuangxiong") { global=true; events << EventPhaseStart << Damaged << EventPhaseChanging; view_as_skill=new MobileShuangxiongVS; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        if (event==EventPhaseChanging && actor && data.value<PhaseChangeStruct>().to==Player::NotActive) {
            const qint64 turn=room->historyScopes().value("turn_id").toLongLong();
            bool available=false;
            for (const SkillInstance &instance : actor->getSkillInstances()) if (instance.skillName==objectName()) {
                QVariantList kept; for(const QVariant &value:actor->getSkillInstanceStateValue(objectName(),instance.instanceID,"color_frames").toList())
                    if(value.toMap().value("turn").toLongLong()!=turn) kept << value;
                actor->setSkillInstanceStateValue(objectName(),instance.instanceID,"color_frames",kept);
                const QStringList colors=kept.isEmpty()?QStringList():kept.last().toMap().value("colors").toStringList();
                actor->setSkillInstanceStateValue(objectName(),instance.instanceID,"colors",colors); available |= !colors.isEmpty();
            }
            room->setPlayerMark(actor,"ViewAsSkill_mobileshuangxiongEffect",available?1:0);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &) const override
    { return event==EventPhaseStart && actor && actor->isAlive() && actor->hasSkill(objectName()) && actor->getPhase()==Player::Draw ? TriggerList{{actor,{objectName()}}}:TriggerList(); }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if(event!=Damaged) return false; const DamageStruct damage=data.value<DamageStruct>();
        if(!actor || actor->isDead() || !damage.card || !damage.from || !damage.card->isKindOf("Duel")) return true;
        const QVariantMap receipt=damage.card->getTag("MobileShuangxiongSource").toMap();
        if(receipt.value("actor").toString()!=actor->objectName()) return true;
        const qint64 useId=room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id").toLongLong(); if(useId<=0) return true;
        SkillContext ctx; ctx.skill_name=objectName(); ctx.owner=room->findPlayerByObjectName(receipt.value("owner").toString(),true); ctx.initiator=actor; ctx.invoker=actor;
        ctx.instanceID=receipt.value("instance").toInt(); ctx.sourceRef=SkillInstanceRef(receipt.value("source_owner").toString(),SkillInstanceKey(receipt.value("source_skill").toString(),receipt.value("source_instance").toInt()));
        ctx.extra_data=QVariantMap{{"receipt",receipt},{"use",useId}}; ctx.amount=receipt.value("amount").toInt(); ctx.current_event=event; ctx.original_data=&data; ctx.targets << actor; contexts << ctx; return true;
    }
    bool isSourceAvailable(Room *room,const SkillContext &ctx) const override
    {
        if(ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room,ctx);
        if(!ctx.original_data || !ctx.invoker || ctx.invoker->isDead()) return false;
        const DamageStruct damage=ctx.original_data->value<DamageStruct>();
        return damage.card && damage.card->getTag("MobileShuangxiongSource")==ctx.extra_data.toMap().value("receipt");
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if(event==Damaged) return ctx.invoker->askForSkillInvoke(this,*ctx.original_data);
        if(!ctx.owner->askForSkillInvoke(this)) return false; ctx.targets << ctx.owner; return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    { ctx.manual_effect=true; return skillEffect(event,room,actor,ctx,ctx.targets.first()); }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if(getEffectiveAmount(ctx)<=0) return false;
        if(event==Damaged) {
            const DamageStruct damage=ctx.original_data->value<DamageStruct>(); if(!damage.from) return false;
            const qint64 useId=ctx.extra_data.toMap().value("use").toLongLong();
            QVariantMap filter{{"kind","respond_card"},{"player",damage.from->objectName()},{"limit",128}}; QList<int> ids;
            for(;;) {
                const QVariantMap page=room->queryHistoryFacts(filter); if(page.contains("error") || !page.value("complete").toBool()) return false;
                for(const QVariant &value:page.value("items").toList()) {
                    const QVariantMap fact=value.toMap(); if(room->historyParent(fact.value("event_id").toLongLong(),"use_card",true).value("id").toLongLong()!=useId) continue;
                    const QVariantMap card=fact.value("data").toMap().value("card").toMap(); if(!card.value("classes").toList().contains(QVariant("Slash"))) continue;
                    QList<int> material=ListV2I(card.value("subcards").toList()); if(material.isEmpty() && card.value("id",-1).toInt()>=0) material << card.value("id").toInt();
                    for(int id:material) if(!ids.contains(id) && room->getCardPlace(id)==Player::DiscardPile) ids << id;
                }
                if(!page.value("has_more").toBool()) break; filter["after"]=page.value("next_after"); filter["watermark"]=page.value("watermark");
            }
            if(!ids.isEmpty()) { DummyCard reward(ids); room->obtainCard(target,&reward); } return false;
        }
        const QList<int> revealed=room->getNCards(2*getEffectiveAmount(ctx));
        const auto restore=qScopeGuard([&] { QList<int> stranded; for(int id:revealed) if(room->getCardPlace(id)==Player::DrawPile && !room->getDrawPile().contains(id)) stranded << id; if(!stranded.isEmpty()) room->returnToTopDrawPile(stranded); room->clearAG(); });
        if(revealed.isEmpty()) return false;
        LogMessage log; log.type="$TurnOver"; log.from=target; log.card_str=ListI2S(revealed).join("+"); room->sendLog(log); room->fillAG(revealed,target);
        const int id=room->askForAG(target,revealed,false,objectName()); room->clearAG();
        QList<int> remaining; for(int cid:revealed) if(room->getCardPlace(cid)==Player::DrawPile && !room->getDrawPile().contains(cid)) remaining << cid;
        room->returnToTopDrawPile(remaining);
        if(!revealed.contains(id) || room->getCardPlace(id)!=Player::DrawPile || !room->getDrawPile().contains(id)) return true;
        const QString color=Sanguosha->getCard(id)->getColorString();
        QVariantList frames=ctx.owner->getSkillInstanceStateValue(objectName(),ctx.activationRef.key.instanceID,"color_frames").toList();
        const QVariant turn=room->historyScopes().value("turn_id"); QVariantMap frame;
        if(!frames.isEmpty() && frames.last().toMap().value("turn")==turn) frame=frames.takeLast().toMap();
        QStringList colors=frame.value("colors").toStringList(); if(!colors.contains(color)) colors << color;
        frame["turn"]=turn; frame["colors"]=colors; frames << frame;
        ctx.owner->setSkillInstanceStateValue(objectName(),ctx.activationRef.key.instanceID,"color_frames",frames);
        ctx.owner->setSkillInstanceStateValue(objectName(),ctx.activationRef.key.instanceID,"colors",colors);
        room->setPlayerMark(ctx.owner,"ViewAsSkill_mobileshuangxiongEffect",1);
        room->obtainCard(target,id,true); return true;
    }
};

class MobileLuanjiVS : public ViewAsSkillV2
{
public:
    MobileLuanjiVS() : ViewAsSkillV2("mobileluanji", 2) { response_or_use = true; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    QString historyKey(const ActiveSkillRequest &) const override { return "ArcheryAttack"; }
    QStringList usedSuits(const Player *actor, const SkillInstanceRef &ref) const
    {
        if (!actor || !ref.isValid()) return {};
        for (const Player *holder : actor->getSiblings(true))
            if (holder->objectName() == ref.ownerObjectName) {
                if(const ServerPlayer *server=qobject_cast<const ServerPlayer *>(actor)) return holder->getSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,"mobileluanji_turns").toMap().value(server->getRoom()->historyScopes().value("turn_id").toString()).toStringList();
                return holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "mobileluanji_turn_suits").toStringList();
            }
        return {};
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        ArcheryAttack preview(Card::NoSuit, 0);
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return preview.isAvailable(request.initiator);
        return (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
            || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && Sanguosha->matchExpPattern(request.pattern, request.initiator, &preview);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && request.selectedCardIds.size() < 2 && !request.selectedCardIds.contains(card->getEffectiveId())
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getHandPile().contains(card->getEffectiveId()))
            && !usedSuits(request.initiator, request.activationRef).contains(card->getSuitString());
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 2) return nullptr;
        ArcheryAttack *card = new ArcheryAttack(Card::SuitToBeDecided, 0);
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        QStringList suits; for(int id:request.selectedCardIds) suits << Sanguosha->getCard(id)->getSuitString(); card->setTag("MobileLuanjiMaterialSuits",suits);
        return card;
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.initiator || !ref.isValid()) return false;
        const ServerPlayer *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return false;
        if (!ctx.use_card) return true;
        const QStringList used = usedSuits(ctx.initiator, ref);
        for(const QString &suit:ctx.use_card->getTag("MobileLuanjiMaterialSuits").toStringList()) if(used.contains(suit)) return false;
        return true;
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.initiator || !ctx.use_card || !ref.isValid()) return;
        ServerPlayer *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return;
        QStringList suits = usedSuits(ctx.initiator, ref);
        for (const QString &suit : ctx.use_card->getTag("MobileLuanjiMaterialSuits").toStringList()) {
            if (!suits.contains(suit)) suits << suit;
        }
        QVariantMap turns=holder->getSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,"mobileluanji_turns").toMap(); turns[ctx.initiator->getRoom()->historyScopes().value("turn_id").toString()]=suits;
        holder->setSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,"mobileluanji_turns",turns);
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "mobileluanji_turn_suits", suits);
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.initiator || !ref.isValid()) return;
        ServerPlayer *holder = ctx.initiator->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if(holder) { QVariantMap turns=holder->getSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,"mobileluanji_turns").toMap(); turns.remove(ctx.initiator->getRoom()->historyScopes().value("turn_id").toString()); holder->setSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,"mobileluanji_turns",turns); holder->removeSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,"mobileluanji_turn_suits"); }
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 2 || request.selectedCardIds.first() == request.selectedCardIds.last()
            || !checkCustomUsage(ctx)) return false;
        for (int id : request.selectedCardIds)
            if (!ctx.initiator->handCards().contains(id) && !ctx.initiator->getHandPile().contains(id)) return false;
        // Commit the activation's suits before the ordinary card pipeline moves either material.
        addUsage(ctx);
        return true;
    }
};

class MobileLuanji : public TriggerSkillV2
{
public:
    MobileLuanji() : TriggerSkillV2("mobileluanji")
    {
        events << EventPhaseChanging << CardResponded << CardFinished;
        frequency = Compulsory;
        view_as_skill = new MobileLuanjiVS;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if(event==EventPhaseChanging && data.value<PhaseChangeStruct>().to==Player::NotActive)
            for(ServerPlayer *holder:room->getAllPlayers(true)) for(const SkillInstance &instance:holder->getSkillInstances()) {
                QVariantMap turns=holder->getSkillInstanceStateValue(instance.skillName,instance.instanceID,"mobileluanji_turns").toMap(); if(turns.isEmpty()) continue;
                turns.remove(room->historyScopes().value("turn_id").toString()); holder->setSkillInstanceStateValue(instance.skillName,instance.instanceID,"mobileluanji_turns",turns);
                QString latest; for(auto it=turns.cbegin();it!=turns.cend();++it) if(it.key().toLongLong()>latest.toLongLong()) latest=it.key();
                holder->setSkillInstanceStateValue(instance.skillName,instance.instanceID,"mobileluanji_turn_suits",turns.value(latest).toStringList());
            }
        return true;
    }
    bool noDamage(Room *room) const
    {
        const QVariantMap history = room->queryCardUseDamage();
        return !history.contains("error") && history.value("complete").toBool() && history.value("items").toList().isEmpty();
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->isDead()) return {};
        if (event == CardResponded) {
            const CardResponseStruct response = data.value<CardResponseStruct>();
            ServerPlayer *owner = response.m_who;
            return !response.m_isRetrial && response.m_card && response.m_card->isKindOf("Jink")
                && response.m_toCard && response.m_toCard->isKindOf("ArcheryAttack")
                && owner && owner->isAlive() && owner->hasSkill(objectName())
                ? TriggerList{{owner, {objectName()}}} : TriggerList();
        }
        if (event != CardFinished || !player->hasSkill(objectName())) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        return use.from == player && use.card && use.card->isKindOf("ArcheryAttack")
            && !use.to.isEmpty() && noDamage(room) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardFinished && !noDamage(room)) return false;
        ctx.extra_data = event == CardFinished ? ctx.original_data->value<CardUseStruct>().to.size() : 1;
        ctx.targets = {event == CardResponded ? ctx.invoker : ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

MobileZaiqiCard::MobileZaiqiCard()
{
}

bool MobileZaiqiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
    return targets.length() < Self->getMark("mobilezaiqi-Clear");
}

void MobileZaiqiCard::onEffect(CardEffectStruct &effect) const
{
    if (effect.to->isDead()) return;
    QStringList choices;
    choices << "draw";
    if (effect.from->isAlive() && effect.from->isWounded())
        choices << "recover=" + effect.from->objectName();
    Room *room = effect.from->getRoom();
    QString choice = room->askForChoice(effect.to, "mobilezaiqi", choices.join("+"), QVariant::fromValue(effect.from));
    if (choice == "draw")
        effect.to->drawCards(1, "mobilezaiqi");
    else {
        room->recover(effect.from, RecoverStruct("mobilezaiqi", effect.to));
    }
}

class MobileZaiqi : public TriggerSkillV2
{
public:
    MobileZaiqi() : TriggerSkillV2("mobilezaiqi")
    {
        events << CardsMoveOneTime << EventPhaseEnd;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardsMoveOneTime || !player) return true;
        // Unrelated packages still consume these movement receipts; Zaiqi itself uses Room history.
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from==player){
				int n = 0, m = 0;
				for (int i = 0; i < move.card_ids.length(); i++) {
					if (move.from_places[i] == Player::PlaceEquip)
						n++;
					if (Sanguosha->getCard(move.card_ids.at(i))->isKindOf("EquipCard")
                        && (move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)!=CardMoveReason::S_REASON_USE
						&& (move.from_places[i] == Player::PlaceEquip || move.from_places[i] == Player::PlaceHand))
						m++;
				}
				n = qMin(n, 3 - player->getMark("&shanjia") - player->getMark("shanjiaMark"));
				m = qMin(m, 3 - player->getMark("&olshanjia") - player->getMark("olshanjiaMark"));
				if (n > 0) {
					if (player->hasSkill("shanjia", true))
						room->addPlayerMark(player, "&shanjia", n);
					else
						player->addMark("shanjiaMark", n);
				}
				if (m > 0) {
					if (player->hasSkill("olshanjia", true))
						room->addPlayerMark(player, "&olshanjia", m);
					else
						player->addMark("olshanjiaMark", m);
				}
			}
            if (move.to_place != Player::DiscardPile) return true;
            foreach (int id, move.card_ids) {
                player->addMark(QString::number(id)+"AnzhiRecord-Clear");
            }
        return true;
    }
    int redDiscards(Room *room, ServerPlayer *owner) const
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return -1;
        QVariantMap filter{{"turn_id", turn}, {"from", owner->objectName()}, {"limit", 128}};
        int count = 0;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return -1;
            for (const QVariant &item : page.value("items").toList()) {
                const QVariantMap move = item.toMap().value("data").toMap();
                if (!move.contains("to_place")) return -1;
                if (move.value("to_place").toInt() != Player::DiscardPile) continue;
                const QVariantMap card = move.value("card_before").toMap();
                if (!card.contains("red")) return -1;
                if (card.value("red").toBool()) ++count;
            }
            if (!page.value("has_more").toBool()) return count;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseEnd && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Discard && redDiscards(room, player) > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int count = redDiscards(room, ctx.owner);
        if (count <= 0) return false;
        ctx.targets = room->askForPlayersChosen(ctx.owner, room->getAlivePlayers(), objectName(), 0, count,
            "@mobilezaiqi:" + QString::number(count));
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "recover") {
            ServerPlayer *healer = room->findPlayerByObjectName(ctx.extra_data.toString(), true);
            room->recover(target, RecoverStruct(objectName(), healer, getEffectiveAmount(ctx)));
            return false;
        }
        QStringList choices{"draw"};
        if (ctx.owner->isAlive() && ctx.owner->isWounded()) choices << "recover=" + ctx.owner->objectName();
        if (room->askForChoice(target, objectName(), choices.join("+"), QVariant::fromValue(ctx.owner)) == "draw")
            target->drawCards(getEffectiveAmount(ctx), objectName());
        else {
            // Recovery affects the source independently of the selected chooser's draw hook.
            SkillContext recovery = ctx;
            recovery.choice = "recover";
            recovery.extra_data = target->objectName();
            skillEffect(event, room, player, recovery, ctx.owner);
        }
        return false;
    }
};

class MobileLieren : public TriggerSkillV2
{
public:
    MobileLieren() : TriggerSkillV2("mobilelieren")
    {
        events << TargetSpecified;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || player->isDead() || !player->hasSkill(objectName()) || !use.card || !use.card->isKindOf("Slash")) return {};
        QStringList names;
        for (ServerPlayer *target : use.to)
            if (target->isAlive() && player->canPindian(target)) names << target->objectName();
        return names.isEmpty() ? TriggerList() : TriggerList{{player, {objectName() + "->" + names.join("+")}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.preferredTarget;
        if (!target && !ctx.targets.isEmpty()) target = ctx.targets.first();
        if (!target || target->isDead() || !ctx.owner->canPindian(target)
            || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target))) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner->canPindian(target)) return false;
        room->broadcastSkillInvoke(this);
        PindianStruct *pindian = owner->PinDian(target, objectName(), nullptr);
        if (!pindian || owner->isDead() || target->isDead()) return false;
        if (pindian->from_number > pindian->to_number) {
            if (target->isNude()) return false;
            int id = room->askForCardChosen(owner, target, "he", objectName());
            if (id >= 0) {
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, owner->objectName());
                room->obtainCard(owner, Sanguosha->getCard(id), reason, room->getCardPlace(id) != Player::PlaceHand);
            }
        } else {
            const int fromId = pindian->from_card->getEffectiveId();
            const int toId = pindian->to_card->getEffectiveId();
            if (room->getCardPlace(fromId) != Player::DiscardPile || room->getCardPlace(toId) != Player::DiscardPile) return false;
            QList<CardsMoveStruct> moves;
            moves << CardsMoveStruct(toId, owner, Player::PlaceHand,
                CardMoveReason(CardMoveReason::S_REASON_SWAP, owner->objectName(), target->objectName(), objectName(), ""));
            moves << CardsMoveStruct(fromId, target, Player::PlaceHand,
                CardMoveReason(CardMoveReason::S_REASON_SWAP, target->objectName(), owner->objectName(), objectName(), ""));
            room->moveCardsAtomic(moves, true);
        }
        return false;
    }
};

class MobileXingshang : public TriggerSkillV2
{
public:
    MobileXingshang() : TriggerSkillV2("mobilexingshang")
    {
        events << Death;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *dead = data.value<DeathStruct>().who;
        // Death is dispatched to each player in order, not only to the victim.
        return player && player->isAlive() && player->hasSkill(objectName()) && dead && dead != player
            && (!dead->isNude() || player->isWounded()) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *dead = ctx.original_data->value<DeathStruct>().who;
        QStringList choices;
        if (dead && !dead->isNude()) choices << "get";
        if (ctx.owner->isWounded()) choices << "recover";
        if (choices.isEmpty() || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        ctx.choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"), *ctx.original_data);
        // The corpse supplies cards; only the living recipient is an effect target.
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(this);
        if (ctx.choice == "get") {
            ServerPlayer *dead = ctx.original_data->value<DeathStruct>().who;
            if (!dead) return false;
            DummyCard cards(dead->handCards());
            cards.addSubcards(dead->getEquips());
            if (cards.subcardsLength() > 0) {
                CardMoveReason reason(CardMoveReason::S_REASON_RECYCLE, target->objectName());
                room->obtainCard(target, &cards, reason, false);
            }
        } else {
            room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
        }
        return false;
    }
};

class MobileFangzhu : public TriggerSkillV2
{
public:
    MobileFangzhu() : TriggerSkillV2("mobilefangzhu") { events << Damaged; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const SkillInstance *instance=ctx.owner->findSkillInstance(ctx.activationRef.key.skillName,ctx.activationRef.key.instanceID);
        ctx.choice=instance && instance->parentRef.key.skillName=="jilve"?"jilve":QString();
        ServerPlayer *to = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(),
            "@mobilefangzhu-invoke", ctx.choice!="jilve", true);
        if (!to) return false;
        ctx.targets = {to};
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice!="jilve")
            room->broadcastSkillInvoke(objectName());
        else
            room->broadcastSkillInvoke("jilve", 2);
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *to) const override
    {
        if (!to || !to->isAlive() || getEffectiveAmount(ctx)<=0) return false;
        ServerPlayer *caopi = ctx.owner;
        // Keep the variant's zero-lost-HP choice and exact discard alternative.
        int losthp = caopi->getLostHp();
        if (losthp <= 0) {
            QString choice = room->askForChoice(to, objectName(), "turnover+losehp");
            if (choice == "turnover")
                to->turnOver();
            else
                room->loseHp(HpLostStruct(to, getEffectiveAmount(ctx), objectName(), caopi));
            return false;
        }

        int candis = 0;
        foreach (const Card *c, to->getCards("he")) {
            if (to->canDiscard(to, c->getEffectiveId()))
                candis++;
        }
        if (candis < losthp) {
            to->drawCards(losthp * getEffectiveAmount(ctx), objectName());
            to->turnOver();
        } else {
            if (room->askForDiscard(to, objectName(), losthp, losthp, true, true, "mobilefangzhu-discard:" + QString::number(losthp))) {
                room->loseHp(HpLostStruct(to, getEffectiveAmount(ctx), objectName(), caopi));
                return false;
            }
            to->drawCards(losthp * getEffectiveAmount(ctx), objectName());
            to->turnOver();
        }
        return false;
    }
};

MobilePoluCard::MobilePoluCard()
{
}

bool MobilePoluCard::targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const
{
    if (Self->isDead())
        return to_select != Self;
    return true;
}

void MobilePoluCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->addPlayerMark(source, "mobilepolu_usedtimes");
    int mark = source->getMark("mobilepolu_usedtimes");
    room->drawCards(targets, mark, "mobilepolu");
}

class MobilePolu : public TriggerSkillV2
{
public:
    MobilePolu() : TriggerSkillV2("mobilepolu") { events << Death << EventSkillInvoking; global = true; }
    int invocations(Room *room, const SkillContext &ctx) const
    {
        if (!ctx.sourceRef.isValid() || !ctx.activationRef.isValid()) return -1;
        QVariantMap filter{{"kind", "skill_invoked"}, {"skill_name", ctx.sourceRef.key.skillName},
            {"skill_owner", ctx.sourceRef.ownerObjectName}, {"limit", 128}};
        int count = 0;
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (page.contains("error") || !page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return -1;
            for (const QVariant &item : page.value("items").toList()) {
                const QVariantMap fact = item.toMap().value("data").toMap();
                if (fact.value("activation_owner").toString() == ctx.activationRef.ownerObjectName
                    && fact.value("activation_skill").toString() == ctx.activationRef.key.skillName
                    && fact.value("activation_instance_id").toInt() == ctx.activationRef.key.instanceID) ++count;
            }
            if (!page.value("has_more").toBool()) return count;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return true;
        SkillContext accepted = data.value<SkillContext>();
        if (accepted.skill_name != objectName() || accepted.executionID != 0
            || !accepted.sourceRef.isValid() || !accepted.activationRef.isValid()) return true;
        // The journal records this accepted invocation before its listeners run. Freeze its ordinal
        // now, so nested death resolutions cannot change an earlier invocation's draw amount.
        accepted.extra_data = invocations(room, accepted);
        data = QVariant::fromValue(accepted);
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Death || !player || !player->hasSkill(objectName())) return {};
        const DeathStruct death = data.value<DeathStruct>();
        int count = death.who == player ? 1 : 0;
        if (death.damage && death.damage->from == player) ++count;
        return count > 0 ? TriggerList{{player, {objectName() + "*" + QString::number(count)}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int count = invocations(room, ctx);
        if (count < 0) return false;
        ctx.targets = room->askForPlayersChosen(ctx.owner, room->getAlivePlayers(), objectName(), 0,
            room->alivePlayerCount(), "@mobilepolu:" + QString::number(count + 1));
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int count = ctx.extra_data.toInt();
        if (count > 0) target->drawCards(count * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class MobileJiuchiVS : public ViewAsSkillV2
{
public:
    MobileJiuchiVS() : ViewAsSkillV2("mobilejiuchi", 1)
    {
        response_or_use = true;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Analeptic"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Analeptic::IsAvailable(request.initiator);
        if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
            && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
        Analeptic preview(Card::NoSuit, 0);
        return Sanguosha->matchExpPattern(request.pattern, request.initiator, &preview);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && request.selectedCardIds.isEmpty() && card->getSuit() == Card::Spade
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getHandPile().contains(card->getEffectiveId()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        Analeptic *analeptic = new Analeptic(originalCard->getSuit(), originalCard->getNumber());
        analeptic->setSkillName(objectName());
        analeptic->addSubcard(originalCard->getEffectiveId());
        return analeptic;
    }
};

class MobileJiuchi : public TriggerSkillV2
{
public:
    MobileJiuchi() : TriggerSkillV2("mobilejiuchi")
    {
        events << Damage;
        view_as_skill = new MobileJiuchiVS;
    }

    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.is_forced = true;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && damage.from == player
            && damage.card && damage.card->isKindOf("Slash") && damage.card->hasFlag("drank")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = target;
        if (player->hasSkill("benghuai")) {
            LogMessage log;
            log.type = "#BenghuaiNullification";
            log.from = player;
            log.arg = objectName();
            log.arg2 = "benghuai";
            room->sendLog(log);
            room->broadcastSkillInvoke("mobilejiuchi");
            room->notifySkillInvoked(player, "mobilejiuchi");
        }
        // This is a public, already-applied turn effect consumed by Benghuai.
        room->addPlayerMark(player, "benghuai_nullification-Clear");
        return false;
    }
};

class MobileTuntian : public TriggerSkillV2
{
public:
    MobileTuntian() : TriggerSkillV2("mobiletuntian") { events << CardsMoveOneTime; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName()) || player->hasFlag("CurrentPlayer")) return {};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return move.from == player && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
            && !(move.to == player && (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip))
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        const QVariant previous = target->getTag("MobileTuntianJudgeReceipt");
        auto restore = qScopeGuard([&] {
            if (previous.isValid()) target->setTag("MobileTuntianJudgeReceipt", previous);
            else target->removeTag("MobileTuntianJudgeReceipt");
        });
        const qint64 token = room->getTag("MobileTuntianReceiptSequence").toLongLong() + 1;
        room->setTag("MobileTuntianReceiptSequence", token);
        // Keep primitive accepted provenance across retrials and source removal; nested judgments restore their parent.
        target->setTag("MobileTuntianJudgeReceipt", QVariantMap{{"token", token}, {"owner", ctx.owner->objectName()},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}});
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            JudgeStruct judge;
            judge.pattern = ".|heart"; judge.good = false; judge.reason = objectName(); judge.who = target;
            QVariantMap frame=target->getTag("MobileTuntianJudgeReceipt").toMap();
            frame["parent_event"]=room->currentHistoryEventId(); frame["judge_event"]=0; frame["amount"]=getEffectiveAmount(ctx); target->setTag("MobileTuntianJudgeReceipt",frame);
            room->judge(judge);
        }
        return false;
    }
};
class MobileTuntianJudge : public TriggerSkillV2
{
public:
    MobileTuntianJudge() : TriggerSkillV2("#mobiletuntian-judge")
    { events << StartJudge << FinishJudge; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data) const override
    {
        if(event!=StartJudge || !actor) return false;
        const JudgeStruct *judge=data.value<JudgeStruct *>(); if(!judge || judge->who!=actor || judge->reason!="mobiletuntian") return false;
        QVariantMap frame=actor->getTag("MobileTuntianJudgeReceipt").toMap();
        const QVariantMap history=room->historyParent(room->currentHistoryEventId(),"judge",true);
        if(frame.value("parent_event").toLongLong()>0 && frame.value("judge_event").toLongLong()==0 && frame.value("parent_event").toLongLong()==history.value("parent_id").toLongLong()) {
            frame["judge_event"]=history.value("id"); actor->setTag("MobileTuntianJudgeReceipt",frame);
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if(event!=FinishJudge) return true;
        const JudgeStruct *judge = data.value<JudgeStruct *>();
        if (!judge || !player || judge->who != player || judge->reason != "mobiletuntian" || !judge->card
            || room->getCardPlace(judge->card->getEffectiveId()) != Player::PlaceJudge) return true;
        const QVariantMap receipt = player->getTag("MobileTuntianJudgeReceipt").toMap();
        const qint64 judgeId=room->historyParent(room->currentHistoryEventId(),"judge",true).value("id").toLongLong();
        if(judgeId<=0 || receipt.value("judge_event").toLongLong()!=judgeId) return true;
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        const SkillInstanceRef source(receipt.value("source_owner").toString(),
            SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
        if (!owner || !source.isValid() || receipt.value("token").toLongLong() <= 0) return true;
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = owner; ctx.initiator = owner; ctx.invoker = player;
        ctx.sourceRef = source; ctx.instanceID = receipt.value("token").toInt(); ctx.amount=1;
        ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event;
        ctx.is_forced = true; ctx.targets = {player};
        // Empty activation marks an accepted continuation; live grant validity is no longer its authority.
        contexts << ctx;
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        return ctx.invoker && ctx.invoker->getTag("MobileTuntianJudgeReceipt").toMap() == ctx.extra_data.toMap();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (getEffectiveAmount(ctx)<=0 || !judge || judge->who != target || !judge->card
            || room->getCardPlace(judge->card->getEffectiveId()) != Player::PlaceJudge) return false;
        if (judge->isGood()) target->addToPile("field", judge->card);
        else room->obtainCard(target, judge->card, true);
        return false;
    }
};

class MobileTuntianDistance : public DistanceSkillV2
{
public:
    MobileTuntianDistance() : DistanceSkillV2("#mobiletuntian-dist")
    {
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.holder || ctx.holder != ctx.primary)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(-ctx.holder->getPile("field").length() * ctx.currentAmount);
    }
};

MobileTiaoxinCard::MobileTiaoxinCard()
{
    setSkillName("mobiletiaoxin");
}

void MobileTiaoxinCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    bool use_slash = false;
    if (effect.to->canSlash(effect.from, nullptr, false))
        use_slash = room->askForUseSlashTo(effect.to, effect.from, "@mobiletiaoxin-slash:" + effect.from->objectName());
    if (!use_slash && effect.from->canDiscard(effect.to, "he"))
        room->throwCard(room->askForCardChosen(effect.from, effect.to, "he", "mobiletiaoxin", false, Card::MethodDiscard), effect.to, effect.from);
}

class MobileTiaoxin : public ViewAsSkillV2
{
public:
    MobileTiaoxin() : ViewAsSkillV2("mobiletiaoxin")
    {
        setPhaseName("Play");
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileTiaoxinCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return target && target != request.initiator && selected.isEmpty();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        for(int repeat=0;repeat<getEffectiveAmount(ctx) && target->isAlive() && ctx.invoker->isAlive();++repeat) {
            const bool used=target->canSlash(ctx.invoker,nullptr,false) && room->askForUseSlashTo(target,ctx.invoker,"@mobiletiaoxin-slash:"+ctx.invoker->objectName());
            if(!used && ctx.invoker->isAlive() && target->isAlive() && ctx.invoker->canDiscard(target,"he")) {
                const int id=room->askForCardChosen(ctx.invoker,target,"he",objectName(),false,Card::MethodDiscard);
                if(id>=0 && room->getCardOwner(id)==target && ctx.invoker->canDiscard(target,id)) room->throwCard(id,target,ctx.invoker);
            }
        }
        return ContinueEffects;
    }
};

class MobileZhiji : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    MobileZhiji() : TriggerSkillV2("mobilezhiji")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
        waked_skills = "tenyearguanxing";
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        // Active entries commit their own quota; only this exact trigger's bypass path remains.
        if (accepted.executionID == 0 && accepted.bypass_cost && accepted.activationRef == ctx.activationRef
            && accepted.sourceRef == ctx.sourceRef && parseSkillName(accepted.skill_name) == objectName())
            addUsage(accepted);
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventSkillInvoking) return {};
        return player && player->isAlive() && player->getPhase() == Player::Start
            && player->hasSkill(objectName()) && (player->isKongcheng() || player->canWake(objectName()))
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return ctx.owner->isKongcheng() || ctx.owner->canWake(objectName());
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!(ctx.owner->isKongcheng() || ctx.owner->canWake(objectName()))) return false;
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        room->setPlayerMark(ctx.owner, objectName(), 1);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *jiangwei = target;

        if (ctx.owner->isKongcheng()) {
            LogMessage log;
            log.type = "#ZhijiWake";
            log.from = ctx.owner;
            log.arg = objectName();
            room->sendLog(log);
        }
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(ctx.owner, objectName());

        room->doSuperLightbox(ctx.owner, "mobilezhiji");



        if (jiangwei->isWounded() && room->askForChoice(jiangwei, objectName(), "recover+draw") == "recover")
            room->recover(jiangwei, RecoverStruct("mobilezhiji", ctx.owner, getEffectiveAmount(ctx)));
        else
            room->drawCards(jiangwei, 2 * getEffectiveAmount(ctx), objectName());

        if (room->changeMaxHpForAwakenSkill(jiangwei, -getEffectiveAmount(ctx), objectName()))
            room->acquireSkillFromEffect(jiangwei, "tenyearguanxing", ctx);
        return false;
    }
};

class MobileHunzi : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    MobileHunzi() : TriggerSkillV2("mobilehunzi")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
		waked_skills = "yingzi,yinghun";
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        // Active entries commit their own quota; only this exact trigger's bypass path remains.
        if (accepted.executionID == 0 && accepted.bypass_cost && accepted.activationRef == ctx.activationRef
            && accepted.sourceRef == ctx.sourceRef && parseSkillName(accepted.skill_name) == objectName())
            addUsage(accepted);
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventSkillInvoking) return {};
        return player && player->isAlive() && player->getPhase() == Player::Start
            && player->hasSkill(objectName()) && (player->getHp() <= 2 || player->canWake(objectName()))
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return ctx.owner->getHp() <= 2 || ctx.owner->canWake(objectName());
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!(ctx.owner->getHp() <= 2 || ctx.owner->canWake(objectName()))) return false;
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        room->setPlayerMark(ctx.owner, objectName(), 1);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *sunce = target;

        if (ctx.owner->getHp() <= 2) {
            LogMessage log;
            log.type = "#HunziWake";
            log.from = ctx.owner;
            log.arg = QString::number(ctx.owner->getHp());
            log.arg2 = objectName();
            room->sendLog(log);
        }
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(ctx.owner, objectName());

        room->doSuperLightbox(ctx.owner, "mobilehunzi");


        if (room->changeMaxHpForAwakenSkill(sunce, -getEffectiveAmount(ctx), objectName())) {
            room->acquireSkillFromEffect(sunce, "yingzi", ctx);
            room->acquireSkillFromEffect(sunce, "yinghun", ctx);
        }
        return false;
    }
};

class MobileBeige : public TriggerSkillV2
{
public:
    MobileBeige() : TriggerSkillV2("mobilebeige")
    {
        events << Damaged;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *victim, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!victim || victim->isDead() || !damage.card || !damage.card->isKindOf("Slash")) return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers()) {
            if (owner->hasSkill(objectName()) && owner->canDiscard(owner, "he"))
                result.insert(owner, {objectName()});
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room->askForCard(ctx.owner, "..", "@mobilebeige:" + ctx.invoker->objectName(),
            *ctx.original_data, objectName())) return false;
        ctx.targets = {ctx.invoker};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (ctx.choice == "club") {
            const int count = 2 * getEffectiveAmount(ctx);
            if (count > 0) room->askForDiscard(target, objectName(), count, count, false, true);
            return false;
        }
        if (ctx.choice == "spade") {
            target->turnOver();
            return false;
        }
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        room->broadcastSkillInvoke(objectName());
        JudgeStruct judge;
        judge.good = true;
        judge.play_animation = false;
        judge.who = target;
        judge.reason = objectName();
        room->judge(judge);
        if (!judge.card) return false;
        switch (judge.card->getSuit()) {
        case Card::Heart: {
            int count = qMin(target->getLostHp(), damage.damage * getEffectiveAmount(ctx));
            if (target->isAlive() && count > 0)
                room->recover(target, RecoverStruct(ctx.owner, nullptr, count, objectName()));
            break;
        }
        case Card::Diamond:
            if (target->isAlive()) target->drawCards(3 * getEffectiveAmount(ctx), objectName());
            break;
        case Card::Club:
        case Card::Spade:
            if (damage.from && damage.from->isAlive()) {
                // The judgement chooses a second recipient; expose its effect to target interception too.
                SkillContext retaliation = ctx;
                retaliation.choice = judge.card->getSuit() == Card::Club ? "club" : "spade";
                retaliation.targets = {damage.from};
                skillEffect(event, room, player, retaliation, damage.from);
            }
            break;
        default:
            break;
        }
        return false;
    }
};

MobileZhijianCard::MobileZhijianCard()
{
    setSkillName("mobilezhijian");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool MobileZhijianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty() || to_select == Self)
        return false;

    const Card *card = Sanguosha->getCard(subcards.first());
    const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
    int equip_index = static_cast<int>(equip->location());
    return to_select->getEquip(equip_index) == nullptr && !Self->isProhibited(to_select, card);
}

void MobileZhijianCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *erzhang = effect.from;
    erzhang->getRoom()->moveCardTo(this, erzhang, effect.to, Player::PlaceEquip,
        CardMoveReason(CardMoveReason::S_REASON_PUT,
        erzhang->objectName(), "mobilezhijian", ""));

    LogMessage log;
    log.type = "$ZhijianEquip";
    log.from = effect.to;
    log.card_str = QString::number(getEffectiveId());
    erzhang->getRoom()->sendLog(log);

    erzhang->drawCards(1, "mobilezhijian");
}

class MobileZhijianVS : public ViewAsSkillV2
{
public:
    MobileZhijianVS() : ViewAsSkillV2("mobilezhijian", 1)
    {
        setPhaseName("Play");
    }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileZhijianCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && card->isKindOf("EquipCard") && request.selectedCardIds.isEmpty()
            && request.initiator->handCards().contains(card->getEffectiveId());
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        if (!request.initiator || !target || target == request.initiator || !selected.isEmpty()
            || request.selectedCardIds.size() != 1) return false;
        const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        return equip && target->hasEquipArea(equip->location()) && !target->getEquip(equip->location())
            && !request.initiator->isProhibited(target, card);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if(getEffectiveAmount(ctx)<=0 || !ctx.use_card || ctx.use_card->subcardsLength()!=1) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        if (ctx.choice == "zhijian_reward") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            return ContinueEffects;
        }
        int id = ctx.use_card->getSubcards().first();
        const Card *card = Sanguosha->getCard(id);
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        // A delayed/intercepted recipient must still have the equipment slot and source material.
        if (!equip || !ctx.initiator || !ctx.initiator->handCards().contains(id) || !target->hasEquipArea(equip->location())
            || target->getEquip(equip->location()) || ctx.invoker->isProhibited(target, card)) return ContinueEffects;
        room->moveCardTo(card, ctx.initiator, target, Player::PlaceEquip,
            CardMoveReason(CardMoveReason::S_REASON_PUT, ctx.invoker->objectName(), objectName(), ""));
        LogMessage log;
        log.type = "$ZhijianEquip";
        log.from = target;
        log.card_str = QString::number(id);
        room->sendLog(log);
        if (ctx.invoker->isAlive()) {
            SkillContext reward = ctx;
            reward.choice = "zhijian_reward";
            skillEffect(reward, ctx.invoker);
        }
        return ContinueEffects;
    }
};

class MobileZhijian : public TriggerSkillV2
{
public:
    MobileZhijian() : TriggerSkillV2("mobilezhijian")
    {
        events << CardUsed;
        view_as_skill = new MobileZhijianVS;
    }

    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.is_forced = true;
        return TriggerSkillV2::prepareSource(room, ctx);
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && use.from == player
            && use.card && use.card->isKindOf("EquipCard") && player->getPhase() == Player::Play
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

MobileFangquanCard::MobileFangquanCard()
{
	mute = true;
}

void MobileFangquanCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    ServerPlayer *liushan = effect.from, *player = effect.to;

    LogMessage log;
    log.type = "#Fangquan";
    log.from = liushan;
    log.to << player;
    room->sendLog(log);

    room->setTag("MobileFangquanTarget", QVariant::fromValue(player));
}

class MobileFangquan : public TriggerSkillV2
{
public:
    MobileFangquan() : TriggerSkillV2("mobilefangquan")
    { global = true; events << EventPhaseChanging << EventPhaseStart << Death; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (event == Death && data.value<DeathStruct>().who == player)
            player->removeTag("MobileFangquanReceipts");
        else if (event == EventPhaseStart && player->getPhase() == Player::NotActive) {
            const QVariant turn = room->historyScopes().value("turn_id");
            QVariantList kept;
            for (const QVariant &value : player->getTag("MobileFangquanReceipts").toList())
                if (value.toMap().value("turn_id").toLongLong() != turn.toLongLong()) kept << value;
            player->setTag("MobileFangquanReceipts", kept);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseChanging || !player || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return false;
        for (const QVariant &value : player->getTag("MobileFangquanReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("turn_id").toLongLong() <= 0
                || receipt.value("turn_id").toLongLong() != room->historyScopes().value("turn_id").toLongLong()) continue;
            SkillContext ctx;
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = player; ctx.invoker = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (!ctx.owner || !ctx.sourceRef.isValid()) continue;
            ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt(); ctx.choice = "give";
            ctx.amount = receipt.value("amount").toInt();
            ctx.extra_data = QVariantMap{{"receipt", receipt}}; ctx.original_data = &data; ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.choice != "give") return TriggerSkillV2::isSourceAvailable(room, ctx);
        return ctx.initiator && ctx.initiator->getTag("MobileFangquanReceipts").toList()
            .contains(ctx.extra_data.toMap().value("receipt"));
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return event == EventPhaseChanging && player && player->isAlive() && player->hasSkill(objectName())
            && data.value<PhaseChangeStruct>().to == Player::Play && !player->isSkipped(Player::Play)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice != "give") {
            ctx.targets = {ctx.owner};
            return ctx.owner->askForSkillInvoke(this);
        }
        if (!ctx.invoker->canDiscard(ctx.invoker, "h")) return false;
        const std::unique_ptr<const Card> card(room->askForExchange(ctx.invoker, objectName(), 1, 1, false,
            "@mobilefangquan-give", true));
        if (!card || card->subcardsLength() != 1) return false;
        const int id = card->getSubcards().first();
        if (!ctx.invoker->handCards().contains(id) || !ctx.invoker->canDiscard(ctx.invoker, id)) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, room->getOtherPlayers(ctx.invoker), objectName());
        if (!target) return false;
        QVariantMap values = ctx.extra_data.toMap(); values["card"] = id; ctx.extra_data = values;
        ctx.targets = {target}; return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice != "give") return true;
        const int id = ctx.extra_data.toMap().value("card").toInt();
        if (!ctx.initiator || !ctx.initiator->handCards().contains(id) || !ctx.initiator->canDiscard(ctx.initiator, id)) return false;
        room->throwCard(id, objectName(), ctx.initiator);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "give") {
            QVariantList receipts = ctx.initiator->getTag("MobileFangquanReceipts").toList();
            const QVariantMap receipt = ctx.extra_data.toMap().value("receipt").toMap();
            receipts.removeAll(receipt); ctx.initiator->setTag("MobileFangquanReceipts", receipts);
            LogMessage log; log.type = "#Fangquan"; log.from = ctx.invoker; log.to << target; room->sendLog(log);
            room->scheduleExtraTurn(target, ctx.sourceRef, QList<Player::Phase>(), getEffectiveAmount(ctx));
            return false;
        }
        if (target != player || target->isSkipped(Player::Play)) return false;
        const qint64 previous = room->getTag("MobileFangquanSequence").toLongLong();
        if (previous < 0 || previous >= INT_MAX) return false;
        const int serial = int(previous + 1); room->setTag("MobileFangquanSequence", serial);
        QVariantList receipts = target->getTag("MobileFangquanReceipts").toList();
        receipts << QVariantMap{{"serial", serial}, {"owner", ctx.owner->objectName()}, {"actor", target->objectName()},
            {"amount", getEffectiveAmount(ctx)},
            {"turn_id", room->historyScopes().value("turn_id")},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        target->setTag("MobileFangquanReceipts", receipts);
        // Public flag drives the max-hand effect only; the delayed action belongs to its exact receipt.
        room->setPlayerFlag(target, objectName());
        target->skip(Player::Play, true);
        return false;
    }
};

class MobileFangquanMax : public MaxCardsSkillV2
{
public:
    MobileFangquanMax() : MaxCardsSkillV2("#mobilefangquan-max")
    {
        // Fangquan and Anguo leave applied effects on players without this skill.
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &) const override
    {
        return CorrectSkillResult::noEffect();
    }

    CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override
    {
        const Player *target = ctx.primary;
        if (!target) return CorrectSkillResult::noEffect();
        if (target->hasFlag("mobilefangquan")||target->getMark("&mobileanguo")>0)
            return CorrectSkillResult::useAmount(target->getMaxHp());
        return CorrectSkillResult::noEffect();
    }
};

class MobilePojun : public TriggerSkillV2
{
public:
    MobilePojun() : TriggerSkillV2("mobilepojun")
    { global=true; events << TargetSpecified << EventPhaseChanging << DamageCaused << TurnBroken; }
    bool recordEvent(TriggerEvent event,Room *room,ServerPlayer *,QVariant &data) const override
    {
        if(event!=TurnBroken && (event!=EventPhaseChanging || data.value<PhaseChangeStruct>().to!=Player::NotActive)) return false;
        const QVariant turn=room->historyScopes().value("turn_id"); QVariantList kept,due;
        for(const QVariant &value:room->getTag("MobilePojunReceipts").toList()) {
            if(value.toMap().value("turn")==turn) due << value; else kept << value;
        }
        // Detach this turn's liabilities before callbacks; nested turns retain their own receipts.
        room->setTag("MobilePojunReceipts",kept);
        QVariantList outstanding=room->getTag("MobilePojunDue").toList(); outstanding.append(due); room->setTag("MobilePojunDue",outstanding); return false;
    }
    bool collectTriggerContexts(TriggerEvent event,Room *room,ServerPlayer *,QVariant &data,QList<SkillContext> &contexts) const override
    {
        if(event!=EventPhaseChanging && event!=TurnBroken) return false;
        if(event!=TurnBroken && data.value<PhaseChangeStruct>().to!=Player::NotActive) return true;
        for(const QVariant &value:room->getTag("MobilePojunDue").toList()) {
            const QVariantMap receipt=value.toMap(); if(receipt.value("turn")!=room->historyScopes().value("turn_id")) continue; ServerPlayer *target=room->findPlayerByObjectName(receipt.value("target").toString(),true);
            if(!target || target->isDead()) continue;
            SkillContext ctx; ctx.skill_name=objectName(); ctx.owner=room->findPlayerByObjectName(receipt.value("owner").toString(),true);
            ctx.initiator=ctx.owner; ctx.invoker=target; ctx.targets={target}; ctx.instanceID=receipt.value("serial").toInt();
            ctx.sourceRef=SkillInstanceRef(receipt.value("source_owner").toString(),SkillInstanceKey(receipt.value("source_skill").toString(),receipt.value("source_instance").toInt()));
            ctx.extra_data=receipt; ctx.amount=receipt.value("amount").toInt(); ctx.is_forced=true; ctx.original_data=&data; ctx.current_event=event; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room,const SkillContext &ctx) const override
    { return !ctx.activationRef.isValid()?room->getTag("MobilePojunDue").toList().contains(ctx.extra_data):TriggerSkillV2::isSourceAvailable(room,ctx); }
    bool prepareSource(Room *room,SkillContext &ctx) const override
    { if(ctx.current_event==DamageCaused) ctx.is_forced=true; return TriggerSkillV2::prepareSource(room,ctx); }
    TriggerList triggerable(TriggerEvent event,Room *,ServerPlayer *actor,QVariant &data) const override
    {
        if(!actor || actor->isDead() || !actor->hasSkill(objectName())) return {};
        if(event==TargetSpecified) {
            const CardUseStruct use=data.value<CardUseStruct>();
            return use.from==actor && use.card && use.card->isKindOf("Slash") && !use.to.isEmpty()?TriggerList{{actor,{objectName()}}}:TriggerList();
        }
        if(event!=DamageCaused) return {};
        const DamageStruct d=data.value<DamageStruct>();
        return d.from==actor && d.to && d.to->isAlive() && d.card && d.card->isKindOf("Slash") && d.by_user
            && actor->getHandcardNum()>=d.to->getHandcardNum() && actor->getEquips().size()>=d.to->getEquips().size()?TriggerList{{actor,{objectName()}}}:TriggerList();
    }
    bool cost(TriggerEvent event,Room *,ServerPlayer *,SkillContext &ctx) const override
    {
        if(event==EventPhaseChanging || event==TurnBroken) return true;
        if(event==DamageCaused) { ctx.targets={ctx.original_data->value<DamageStruct>().to}; return true; }
        ctx.targets.clear();
        for(ServerPlayer *target:ctx.original_data->value<CardUseStruct>().to)
            if(target->isAlive() && target->getHp()>0 && !target->isNude() && ctx.owner->askForSkillInvoke(this,QVariant::fromValue(target))) ctx.targets << target;
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent event,Room *room,ServerPlayer *,SkillContext &ctx,ServerPlayer *target) const override
    {
        if(event==EventPhaseChanging || event==TurnBroken) {
            QVariantList due=room->getTag("MobilePojunDue").toList(); due.removeAll(ctx.extra_data); room->setTag("MobilePojunDue",due);
            if(getEffectiveAmount(ctx)<=0) return false;
            QList<int> ids; for(const QVariant &id:ctx.extra_data.toMap().value("ids").toList()) if(target->getPile("mobilepojun").contains(id.toInt())) ids << id.toInt();
            if(!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target,&cards,false); } return false;
        }
        if(getEffectiveAmount(ctx)<=0) return false;
        if(event==DamageCaused) {
            DamageStruct d=ctx.original_data->value<DamageStruct>(); if(d.to!=target) return false;
            LogMessage log; log.type="#MobilepojunDamage"; log.from=d.from; log.to << target; log.arg=QString::number(d.damage);
            d.damage+=getEffectiveAmount(ctx); log.arg2=QString::number(d.damage); room->sendLog(log); *ctx.original_data=QVariant::fromValue(d); return false;
        }
        QList<int> ids; const int count=qMin(target->getCards("he").size(),qMax(0,target->getHp())*getEffectiveAmount(ctx));
        for(int i=0;i<count && target->isAlive() && ctx.invoker && ctx.invoker->isAlive();++i) {
            const int id=room->askForCardChosen(ctx.invoker,target,"he",objectName()+"_dis",false,Card::MethodNone,ids,i>0);
            if(id<0 || Sanguosha->getCard(id)->hasFlag("using") || room->getCardOwner(id)!=target || (room->getCardPlace(id)!=Player::PlaceHand && room->getCardPlace(id)!=Player::PlaceEquip)) break;
            if(!ids.contains(id)) ids << id;
        }
        for(int i=ids.size()-1;i>=0;--i) if(Sanguosha->getCard(ids[i])->hasFlag("using") || room->getCardOwner(ids[i])!=target || (room->getCardPlace(ids[i])!=Player::PlaceHand && room->getCardPlace(ids[i])!=Player::PlaceEquip)) ids.removeAt(i);
        if(ids.isEmpty() || target->isDead()) return false;
        const int serial=room->getTag("MobilePojunSerial").toInt()+1; room->setTag("MobilePojunSerial",serial); QVariantList material; for(int id:ids) material << id;
        QVariantMap receipt{{"serial",serial},{"owner",ctx.activationRef.ownerObjectName},{"skill",ctx.activationRef.key.skillName},{"instance",ctx.activationRef.key.instanceID},
            {"source_owner",ctx.sourceRef.ownerObjectName},{"source_skill",ctx.sourceRef.key.skillName},{"source_instance",ctx.sourceRef.key.instanceID},
            {"target",target->objectName()},{"turn",room->historyScopes().value("turn_id")},{"amount",getEffectiveAmount(ctx)},{"ids",material}};
        QVariantList pending=room->getTag("MobilePojunReceipts").toList(); pending << receipt; room->setTag("MobilePojunReceipts",pending);
        // Publish exact material provenance before the pile move can reenter or throw.
        DummyCard cards(ids); target->addToPile("mobilepojun",&cards,false); return false;
    }
};

MobileGanluCard::MobileGanluCard()
{
    setSkillName("mobileganlu");
}

void MobileGanluCard::swapEquip(ServerPlayer *first, ServerPlayer *second) const
{
    Room *room = first->getRoom();
    room->swapEquips(first, second, "mobileganlu");
}

bool MobileGanluCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

bool MobileGanluCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (targets.isEmpty())
        return true;
    else if (targets.length() == 1) {
        int n1 = targets.first()->getEquips().length();
        int n2 = to_select->getEquips().length();
        return qAbs(n1 - n2) <= Self->getLostHp() || targets.first() == Self || to_select == Self;
    }
    return false;
}

void MobileGanluCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    LogMessage log;
    log.type = "#GanluSwap";
    log.from = source;
    log.to = targets;
    room->sendLog(log);

    swapEquip(targets.first(), targets[1]);
}

class MobileGanlu : public ViewAsSkillV2
{
public:
    MobileGanlu() : ViewAsSkillV2("mobileganlu")
    {
        setPhaseName("Play");
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileGanluCard"; }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return effectOnTargetGroup(ctx, ctx.targets);
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "admit_pair") ctx.extra_data = target->objectName();
        else target->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        if (!request.initiator || !target || selected.contains(target)) return false;
        if (selected.isEmpty()) return true;
        return selected.size() == 1 && (selected.first() == request.initiator || target == request.initiator
            || qAbs(selected.first()->getEquips().size() - target->getEquips().size()) <= request.initiator->getLostHp());
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 2; }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        // A swap needs both admitted recipients after target interception.
        if (targets.size() != 2) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        QList<ServerPlayer *> admitted;
        for (ServerPlayer *target : targets) {
            SkillContext part = ctx; part.choice = "admit_pair"; part.extra_data = QVariant();
            skillEffect(part, target);
            ServerPlayer *recipient = room->findPlayerByObjectName(part.extra_data.toString());
            if (!recipient || recipient->isDead() || admitted.contains(recipient)) return ContinueEffects;
            admitted << recipient;
        }
        if (admitted.first()->isDead() || admitted.last()->isDead()) return ContinueEffects;
        if (admitted.first() != ctx.invoker && admitted.last() != ctx.invoker
            && qAbs(admitted.first()->getEquips().size() - admitted.last()->getEquips().size()) > ctx.invoker->getLostHp())
            return ContinueEffects;
        LogMessage log;
        log.type = "#GanluSwap";
        log.from = ctx.invoker;
        log.to = admitted;
        room->sendLog(log);
        room->swapEquips(admitted.first(), admitted.last(), objectName());
        return ContinueEffects;
    }
};

MobileJieyueCard::MobileJieyueCard()
{
    mute = true;
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
}

void MobileJieyueCard::onUse(Room *, CardUseStruct &) const
{
}

class MobileJieyue : public TriggerSkillV2
{
public:
    MobileJieyue() : TriggerSkillV2("mobilejieyue") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Finish && !player->isNude()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        std::unique_ptr<const Card> gift(room->askForExchange(ctx.owner, objectName(), 1, 1, true,
            "mobilejieyue-invoke", true));
        if (!gift || gift->subcardsLength() != 1) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner),
            objectName(), "mobilejieyue-invoke", true, true);
        if (!target) return false;
        ctx.extra_data = gift->getSubcards().first();
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") {
            target->drawCards(3 * getEffectiveAmount(ctx), objectName());
            return false;
        }
        const int id = ctx.extra_data.toInt();
        if (room->getCardOwner(id) != ctx.owner
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        room->giveCard(ctx.owner, target, Sanguosha->getCard(id), objectName());
        if (target->isDead()) return false;
        if (!target->isNude()
            && room->askForChoice(target, objectName(), "discard+draw", QVariant::fromValue(ctx.owner)) == "discard") {
            QList<int> keep;
            // This is a selection of survivors, not another skill activation by the recipient.
            if (!target->isKongcheng())
                keep << room->askForCardsChosen(target, target, "h", objectName(), 1, 1, true, Card::MethodNone, {}, false);
            if (!target->getEquips().isEmpty())
                keep << room->askForCardsChosen(target, target, "e", objectName(), 1, 1, true, Card::MethodNone, {}, false);
            DummyCard discard;
            for (int cardID : target->handCards() + target->getEquipsId())
                if (!keep.contains(cardID) && target->canDiscard(target, cardID)) discard.addSubcard(cardID);
            if (discard.subcardsLength() > 0) room->throwCard(&discard, target, nullptr);
        } else if (ctx.owner->isAlive()) {
            SkillContext reward = ctx;
            reward.choice = "draw";
            skillEffect(event, room, player, reward, ctx.owner);
        }
        return false;
    }
};

class MobileDangxian : public TriggerSkillV2
{
public:
    MobileDangxian() : TriggerSkillV2("mobiledangxian")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::RoundStart
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = target;
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        QList<int> slashes;
        foreach (int id, room->getDiscardPile()) {
            if (Sanguosha->getCard(id)->isKindOf("Slash"))
                slashes << id;
        }
        // The obtained materials are fixed before their movement can trigger other skills.
        DummyCard cards;
        const int count = qMin(getEffectiveAmount(ctx), slashes.size());
        for (int i = 0; i < count; ++i)
            cards.addSubcard(slashes.takeAt(qsanRandomBounded(slashes.size())));
        if (!cards.getSubcards().isEmpty())
            room->obtainCard(player, &cards, true);
        if (player->isAlive())
            player->insertPhase(Player::Play);
        return false;
    }
};

class MobileFuli : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    MobileFuli() : TriggerSkillV2("mobilefuli")
    {
        events << AskForPeaches << EventSkillInvoking;
        frequency = Limited;
        limit_mark = "@mobilefuliMark";
    }

    int getKingdoms(Room *room) const
    {
        QSet<QString> kingdom_set;
        foreach(ServerPlayer *p, room->getAlivePlayers())
            kingdom_set << p->getKingdom();
        return kingdom_set.size();
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        // Active entries commit their own quota; only this exact trigger's bypass path remains.
        if (accepted.executionID == 0 && accepted.bypass_cost && accepted.activationRef == ctx.activationRef
            && accepted.sourceRef == ctx.sourceRef && parseSkillName(accepted.skill_name) == objectName())
            addUsage(accepted);
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {};
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.value<DyingStruct>().who == player
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner->askForSkillInvoke(this, *ctx.original_data); }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // The limited mark is presentation; the activation instance owns the quota.
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        room->removePlayerMark(ctx.owner, "@mobilefuliMark");
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *liaohua = target;
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox(liaohua, objectName());
        int n = qMin(getKingdoms(room) * getEffectiveAmount(ctx) - liaohua->getHp(),
                     liaohua->getMaxHp() - liaohua->getHp());
        if (n > 0)
            room->recover(liaohua, RecoverStruct(liaohua, nullptr, n, objectName()));
        foreach (ServerPlayer *p, room->getOtherPlayers(liaohua)) {
            if (p->getHp() >= liaohua->getHp())
                return false;
        }
        if (liaohua->isAlive())
            liaohua->turnOver();
        return false;
    }
};

MobileAnxuCard::MobileAnxuCard()
{
    setSkillName("mobileanxu");
}

bool MobileAnxuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (targets.isEmpty())
        return to_select != Self;
    if (targets.length() == 1) {
        if (targets.first()->isNude())
            return to_select != Self && !to_select->isNude();
        else
            return to_select != Self;
    }
    return targets.length() < 2;
}

bool MobileAnxuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

void MobileAnxuCard::onUse(Room *room, CardUseStruct &card_use) const
{
    QVariant data = QVariant::fromValue(card_use);
    RoomThread *thread = room->getThread();

    thread->trigger(PreCardUsed, room, card_use.from, data);
    card_use = data.value<CardUseStruct>();

    LogMessage log;
    log.from = card_use.from;
    log.to << card_use.to;
    log.type = "#UseCard";
    log.card_str = toString();
    room->sendLog(log);

    thread->trigger(CardUsed, room, card_use.from, data);
    card_use = data.value<CardUseStruct>();
    thread->trigger(CardFinished, room, card_use.from, data);
}

void MobileAnxuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    if (targets.last()->isNude()) return;

    int id = room->askForCardChosen(targets.first(), targets.last(), "he", "mobileanxu");
    Player::Place place = room->getCardPlace(id);
    CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, targets.first()->objectName(), "mobileanxu", "");
    room->obtainCard(targets.first(), Sanguosha->getCard(id), reason, place != Player::PlaceHand);

    if (place != Player::PlaceHand) return;
    source->drawCards(1, "mobileanxu");
}

class MobileAnxu : public ViewAsSkillV2
{
public:
    MobileAnxu() : ViewAsSkillV2("mobileanxu")
    {
        setPhaseName("Play");
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileAnxuCard"; }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return effectOnTargetGroup(ctx, ctx.targets);
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "admit_pair") ctx.extra_data = target->objectName();
        else target->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return target && target != request.initiator && !selected.contains(target)
            && (selected.isEmpty() || (selected.size() == 1 && !target->isNude()));
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 2; }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        if (targets.size() != 2 || targets.last()->isNude()) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        QList<ServerPlayer *> admitted;
        for (ServerPlayer *target : targets) {
            SkillContext part = ctx; part.choice = "admit_pair"; part.extra_data = QVariant();
            skillEffect(part, target);
            ServerPlayer *recipient = room->findPlayerByObjectName(part.extra_data.toString());
            if (!recipient || recipient->isDead() || admitted.contains(recipient)) return ContinueEffects;
            admitted << recipient;
        }
        ServerPlayer *recipient = admitted.first(), *donor = admitted.last();
        if (recipient->isDead() || donor->isDead() || donor->isNude()) return ContinueEffects;
        int id = room->askForCardChosen(recipient, donor, "he", objectName());
        Player::Place place = room->getCardPlace(id);
        if (id < 0 || room->getCardOwner(id) != donor
            || (place != Player::PlaceHand && place != Player::PlaceEquip)) return ContinueEffects;
        room->obtainCard(recipient, Sanguosha->getCard(id),
            CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, recipient->objectName(), objectName(), ""),
            place != Player::PlaceHand);
        // This continuation belongs to the accepted activation, not every holder observing the move.
        if (ctx.invoker->isAlive() && recipient->isAlive() && donor->isAlive()
            && recipient->getHandcardNum() != donor->getHandcardNum()) {
            ServerPlayer *less = recipient->getHandcardNum() < donor->getHandcardNum() ? recipient : donor;
            if (ctx.invoker->askForSkillInvoke(this, QVariant::fromValue(less))) {
                SkillContext draw = ctx; draw.choice = "draw";
                skillEffect(draw, less);
            }
        }
        if (place == Player::PlaceHand && ctx.invoker->isAlive()) {
            SkillContext draw = ctx; draw.choice = "draw";
            skillEffect(draw, ctx.invoker);
        }
        return ContinueEffects;
    }
};

class MobileZongshi : public TriggerSkillV2
{
public:
    MobileZongshi() : TriggerSkillV2("mobilezongshi")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Start && player->getHandcardNum() > player->getHp()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        // The granted turn effect intentionally survives losing this skill instance.
        room->addSlashCishu(target, 1000 * getEffectiveAmount(ctx));
        return false;
    }
};

class MobileZongshiKeep : public MaxCardsSkillV2
{
public:
    MobileZongshiKeep() : MaxCardsSkillV2("#mobilezongshi-keep")
    {
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const Player *target = ctx.holder;
        if (!target || target != ctx.primary) return CorrectSkillResult::noEffect();
        QSet<QString> kingdoms;
        foreach (const Player *player, target->parent()->findChildren<const Player *>()) {
            if (player->isAlive()) kingdoms << player->getKingdom();
        }
        return CorrectSkillResult::useAmount(kingdoms.size() * ctx.currentAmount);
    }
};

class MobileYicong : public DistanceSkillV2
{
public:
    MobileYicong() : DistanceSkillV2("mobileyicong")
    {
        setHolderSelector(CorrectSkill_Participants);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.holder) return CorrectSkillResult::noEffect();
        int correct = 0;
        if (ctx.holder == ctx.primary) correct += qMin(0, 1 - ctx.holder->getHp());
        if (ctx.holder == ctx.secondary) correct += qMax(0, ctx.holder->getLostHp() - 1);
        return CorrectSkillResult::useAmount(correct * ctx.currentAmount);
    }
};

class MobileBenxiDistance : public DistanceSkillV2
{
public:
    MobileBenxiDistance() : DistanceSkillV2("#mobilebenxi-distance")
    {
        // This mark is an applied turn effect, independent of its original skill grant.
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.primary
            ? CorrectSkillResult::useAmount(-ctx.primary->getMark("&mobilebenxi"))
            : CorrectSkillResult::noEffect();
    }
};

class MobileXuanfeng : public TriggerSkillV2
{
public:
    MobileXuanfeng() : TriggerSkillV2("mobilexuanfeng") { events << CardsMoveOneTime << EventPhaseEnd; }
    QList<ServerPlayer *> discardTargets(Room *room, ServerPlayer *owner) const
    {
        QList<ServerPlayer *> result;
        for (ServerPlayer *p : room->getOtherPlayers(owner))
            if (owner->canDiscard(p, "he")) result << p;
        return result;
    }
    bool canTransfer(Room *room, ServerPlayer *owner, ServerPlayer *from, ServerPlayer *to, int id) const
    {
        if (!from || !to || from == to || from == owner || to == owner || !from->isAlive() || !to->isAlive()
            || !owner->isAlive() || room->getCardOwner(id) != from || room->getCardPlace(id) != Player::PlaceEquip
            || !from->canMove(from, id) || !owner->canMove(from, id)) return false;
        const Card *card = Sanguosha->getCard(id);
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        if (!equip || owner->isProhibited(to, card)) return false;
        for (int slot : equip->getOccupyLocations())
            if (!to->hasEquipArea(slot) || to->getEquip(slot)) return false;
        return true;
    }
    QList<ServerPlayer *> movableFrom(Room *room, ServerPlayer *owner) const
    {
        QList<ServerPlayer *> result;
        for (ServerPlayer *from : room->getOtherPlayers(owner)) {
            bool movable = false;
            for (int id : from->getEquipsId())
                for (ServerPlayer *to : room->getOtherPlayers(owner))
                    if (canTransfer(room, owner, from, to, id)) { movable = true; break; }
            if (movable) result << from;
        }
        return result;
    }
    int discarded(Room *room, ServerPlayer *owner) const
    {
        const QVariant phase = room->historyScopes().value("phase_id");
        if (phase.toLongLong() <= 0) return -1;
        QVariantMap filter{{"phase_id", phase}, {"from", owner->objectName()}, {"limit", 128}};
        int count = 0;
        do {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return -1;
            for (const QVariant &item : page.value("items").toList()) {
                const QVariantMap move = item.toMap().value("data").toMap();
                if (!move.contains("reason")) return -1;
                if ((move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) ++count;
            }
            if (!page.value("has_more").toBool()) return count;
            filter["watermark"] = page.value("watermark"); filter["after"] = page.value("next_after");
        } while (true);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName())) return {};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        const bool eligible = event == CardsMoveOneTime ? move.from == player && move.from_places.contains(Player::PlaceEquip)
            : player->getPhase() == Player::Discard && discarded(room, player) >= 2;
        return eligible && (!discardTargets(room, player).isEmpty() || !movableFrom(room, player).isEmpty())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        QStringList choices;
        if (!discardTargets(room, ctx.owner).isEmpty()) choices << "discard";
        if (!movableFrom(room, ctx.owner).isEmpty()) choices << "move";
        if (choices.isEmpty()) return false;
        ctx.choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"));
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        const int times = (ctx.choice == "discard" ? 2 : 1) * getEffectiveAmount(ctx);
        for (int i = 0; i < times && ctx.owner->isAlive(); ++i) {
            const QList<ServerPlayer *> targets = ctx.choice == "discard" ? discardTargets(room, ctx.owner) : movableFrom(room, ctx.owner);
            if (targets.isEmpty()) break;
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets,
                ctx.choice == "discard" ? objectName() : objectName() + "_from",
                ctx.choice == "discard" ? QString() : "@movefield-equip-from");
            if (!target) break;
            SkillContext part = ctx; part.choice = ctx.choice == "discard" ? "discard_one" : "move_from";
            skillEffect(event, room, player, part, target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "discard_one") {
            if (!ctx.owner->canDiscard(target, "he")) return false;
            const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0 && ctx.owner->canDiscard(target, id)) room->throwCard(id, target, ctx.owner);
        } else if (ctx.choice == "move_from") {
            QList<int> disabled;
            for (int id : target->getEquipsId()) {
                bool movable = false;
                for (ServerPlayer *to : room->getOtherPlayers(ctx.owner))
                    if (canTransfer(room, ctx.owner, target, to, id)) { movable = true; break; }
                if (!movable) disabled << id;
            }
            if (disabled.size() == target->getEquipsId().size()) return false;
            const int id = room->askForCardChosen(ctx.owner, target, "e", objectName(), false, Card::MethodNone, disabled);
            if (id < 0 || disabled.contains(id)) return false;
            QList<ServerPlayer *> recipients;
            for (ServerPlayer *to : room->getOtherPlayers(ctx.owner))
                if (canTransfer(room, ctx.owner, target, to, id)) recipients << to;
            if (recipients.isEmpty()) return false;
            ServerPlayer *to = room->askForPlayerChosen(ctx.owner, recipients, objectName() + "_to",
                "@movefield-to:" + Sanguosha->getCard(id)->objectName());
            if (!to) return false;
            SkillContext transfer = ctx; transfer.choice = "move_to";
            transfer.extra_data = QVariantMap{{"from", target->objectName()}, {"card", id}};
            // Both the equipment holder and receiver get their own interception point.
            skillEffect(event, room, player, transfer, to);
        } else {
            const QVariantMap values = ctx.extra_data.toMap();
            ServerPlayer *from = room->findPlayerByObjectName(values.value("from").toString(), true);
            const int id = values.value("card").toInt();
            if (canTransfer(room, ctx.owner, from, target, id))
                room->moveCardTo(Sanguosha->getCard(id), from, target, Player::PlaceEquip,
                    CardMoveReason(CardMoveReason::S_REASON_TRANSFER, ctx.owner->objectName(), objectName(), ""), true);
        }
        return false;
    }
};

class MobileJiushiVS : public ViewAsSkillV2
{
public:
    MobileJiushiVS() : ViewAsSkillV2("mobilejiushi") { response_or_use = true; }
    QString historyKey(const ActiveSkillRequest &) const override { return "Analeptic"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !request.initiator->faceUp()) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Analeptic::IsAvailable(request.initiator);
        if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
            && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
        Analeptic preview(Card::NoSuit, 0);
        return Sanguosha->matchExpPattern(request.pattern, request.initiator, &preview);
    }
    const Card *createCard(const ActiveSkillRequest &) const override
    {
        Analeptic *card = new Analeptic(Card::NoSuit, 0);
        card->setSkillName(objectName());
        return card;
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator || !ctx.initiator->faceUp()) return false;
        // Turning over is the immutable payer's cost, before the ordinary Analeptic pipeline.
        ctx.initiator->turnOver();
        return true;
    }
};

class MobileJiushi : public TriggerSkillV2
{
public:
    MobileJiushi() : TriggerSkillV2("mobilejiushi")
    {
        events << DamageDone << Damaged << DamageComplete << TurnedOver << EventPhaseChanging;
        view_as_skill = new MobileJiushiVS;
    }
    QString damageKey(Room *room) const
    {
        const qint64 id = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong();
        return id > 0 ? QString::number(id) : QString();
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging && ctx.original_data
            && ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive) {
            // A turn interruption may bypass DamageComplete; no frame survives its enclosing turn.
            const SkillInstanceRef ref = ctx.activationRef;
            ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
            if (holder) {
                QVariantMap frames=holder->getSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,"mobilejiushi_damage_frames").toMap();
                for(auto it=frames.begin();it!=frames.end();) { if(it.value().toMap().value("turn")==room->historyScopes().value("turn_id")) it=frames.erase(it); else ++it; }
                holder->setSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,"mobilejiushi_damage_frames",frames);
            }
            return;
        }
        if ((event != DamageDone && event != DamageComplete) || !ctx.original_data || !ctx.owner) return;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.to != ctx.owner) return;
        const QString key = damageKey(room);
        if (key.isEmpty()) return;
        const SkillInstanceRef ref = ctx.activationRef;
        ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return;
        QVariantMap frames = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
            "mobilejiushi_damage_frames").toMap();
        // Nested damage has its own immutable event key and cannot overwrite its parent's face state.
        if (event == DamageDone) frames[key] = QVariantMap{{"face_down",!ctx.owner->faceUp()},{"turn",room->historyScopes().value("turn_id")}};
        else frames.remove(key);
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "mobilejiushi_damage_frames", frames);
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.is_forced = ctx.current_event == TurnedOver;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName())) return {};
        const bool eligible = event == Damaged ? !player->faceUp()
            : event == TurnedOver && player->property("mobilejiushi_levelup").toBool();
        return eligible ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == Damaged) {
            const SkillInstanceRef ref = ctx.activationRef;
            const ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
            if (!holder || !holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID,
                "mobilejiushi_damage_frames").toMap().value(damageKey(room)).toMap().value("face_down").toBool()
                || ctx.owner->faceUp() || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        }
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx)<=0) return false;
        if (event == Damaged) {
            if (target->faceUp()) return false;
            room->broadcastSkillInvoke(objectName());
            target->turnOver();
            if (target==ctx.owner && ctx.owner->property("mobilejiushi_levelup").toBool()) return false;
        } else {
            room->sendCompulsoryTriggerLog(ctx.owner, this, qsanRandomBounded(2) + 1);
        }
        if (target->isDead()) return false;
        QList<int> tricks;
        for (int id : room->getDrawPile())
            if (Sanguosha->getCard(id)->isKindOf("TrickCard")) tricks << id;
        qsanShuffle(tricks);
        while (tricks.size() > qMax(0, getEffectiveAmount(ctx))) tricks.removeLast();
        if (!tricks.isEmpty()) {
            DummyCard reward(tricks);
            room->obtainCard(target, &reward, true);
        }
        return false;
    }
};

class MobileChengzhang : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    MobileChengzhang() : TriggerSkillV2("mobilechengzhang")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        // Active entries commit their own quota; only this exact trigger's bypass path remains.
        if (accepted.executionID == 0 && accepted.bypass_cost && accepted.activationRef == ctx.activationRef
            && accepted.sourceRef == ctx.sourceRef && parseSkillName(accepted.skill_name) == objectName())
            addUsage(accepted);
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    int damageTotal(Room *room, ServerPlayer *owner) const
    {
        int total = 0;
        QVariant watermark;
        // Dealt and suffered are separate facts for this rule; self-damage contributes to both.
        for (const QString &side : QStringList{"from", "to"}) {
            QVariantMap filter{{side, owner->objectName()}, {"limit", 128}};
            if (watermark.isValid()) filter.insert("watermark", watermark);
            for (;;) {
                const QVariantMap page = room->queryActualDamage(filter);
                if (page.contains("error") || !page.value("complete").toBool()) return -1;
                if (!watermark.isValid()) watermark = page.value("watermark");
                for (const QVariant &item : page.value("items").toList()) {
                    const QVariantMap damage = item.toMap().value("data").toMap();
                    if (!damage.contains("amount")) return -1;
                    total += damage.value("amount").toInt();
                }
                if (!page.value("has_more").toBool()) break;
                filter.insert("watermark", watermark);
                filter.insert("after", page.value("next_after"));
            }
        }
        return total;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventSkillInvoking) return {};
        return player && player->isAlive() && player->getPhase() == Player::Start && player->hasSkill(objectName())
            && (player->canWake(objectName()) || damageTotal(room, player) >= 7)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int count = damageTotal(room, ctx.owner);
        if (count < 7 && !ctx.owner->canWake(objectName())) return false;
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        ctx.extra_data = count;
        room->setPlayerMark(ctx.owner, objectName(), 1);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.extra_data.toInt() >= 7) {
            LogMessage log;
            log.type = "#MobilechengzhangWake";
            log.from = ctx.owner;
            log.arg = QString::number(ctx.extra_data.toInt());
            log.arg2 = objectName();
            room->sendLog(log);
        }
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(ctx.owner, objectName());
        room->doSuperLightbox(ctx.owner, objectName());
        if (room->changeMaxHpForAwakenSkill(target, 0, objectName())) {
            room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
            if (target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
            // This permanent upgrade is the awakening's applied outcome, not its usage authority.
            room->setPlayerProperty(target, "mobilejiushi_levelup", true);
            room->changeTranslation(target, "mobilejiushi", 2);
        }
        return false;
    }
};

MobileGongqiCard::MobileGongqiCard()
{
    setSkillName("mobilegongqi");
}

bool MobileGongqiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && Self->canDiscard(to_select, "he");
}

void MobileGongqiCard::onEffect(CardEffectStruct &effect) const
{
    if (effect.from->isDead() || effect.to->isDead()) return;
    if (!effect.from->canDiscard(effect.to, "he")) return;

    Room *room = effect.from->getRoom();
    int id = room->askForCardChosen(effect.from, effect.to, "he", "mobilegongqi", false, Card::MethodDiscard);
    room->throwCard(id, effect.to, effect.from);
}

class MobileGongqi : public ViewAsSkillV2
{
public:
    MobileGongqi() : ViewAsSkillV2("mobilegongqi", 1)
    {
        setPhaseName("Play");
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileGongqiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && !card->isVirtualCard()
            && !card->hasFlag("using") && !card->isKindOf("BasicCard")
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId()))
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return request.initiator && target && target != request.initiator && selected.isEmpty()
            && request.initiator->canDiscard(target, "he");
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        for (int i = 0; i < getEffectiveAmount(ctx) && ctx.invoker->isAlive()
            && target->isAlive() && ctx.invoker->canDiscard(target, "he"); ++i) {
            int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard);
            if (id < 0) break;
            room->throwCard(id, target, ctx.invoker);
        }
        return ContinueEffects;
    }
};

class MobileGongqiAttack : public AttackRangeSkillV2
{
public:
    MobileGongqiAttack() : AttackRangeSkillV2("#mobilegongqi-attack")
    {
        frequency = NotFrequent;
        setBaseAmount(1000);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const Player *target = ctx.holder;
        if (!target || target != ctx.primary) return CorrectSkillResult::noEffect();
        if (target->getOffensiveHorse() || target->getDefensiveHorse())
            return CorrectSkillResult::useAmount(ctx.currentAmount);
        return CorrectSkillResult::noEffect();
    }
};

class MobileQuanji : public TriggerSkillV2
{
public:
    MobileQuanji() : TriggerSkillV2("mobilequanji")
    {
        events << EventPhaseEnd << Damaged;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName())) return {};
        if (event == EventPhaseEnd)
            return player->getPhase() == Player::Play && player->getHandcardNum() > player->getHp()
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        const int count = data.value<DamageStruct>().damage;
        return count > 0 ? TriggerList{{player, {objectName() + "*" + QString::number(count)}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner->askForSkillInvoke(this); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = target;
        room->broadcastSkillInvoke(this);
        player->drawCards(getEffectiveAmount(ctx), objectName());
        if (player->isAlive() && !player->isKongcheng()) {
            int card_id;
            if (player->getHandcardNum() == 1) {
                room->getThread()->delay();
                card_id = player->handCards().first();
            } else {
                const Card *card = room->askForExchange(player, "quanji", 1, 1, false, "QuanjiPush");
                if (!card) return false;
                card_id = card->getEffectiveId();
            }
            player->addToPile("power", card_id);
        }
        return false;
    }
};

class MobileQuanjiKeep : public MaxCardsSkillV2
{
public:
    MobileQuanjiKeep() : MaxCardsSkillV2("#mobilequanji")
    {
        frequency = Frequent;
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const Player *target = ctx.holder;
        if (!target || target != ctx.primary) return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(target->getPile("power").length() * ctx.currentAmount);
    }
};

class MobileLihuo : public TriggerSkillV2
{
public:
    MobileLihuo() : TriggerSkillV2("mobilelihuo")
    { global = true; events << ChangeSlash << DamageCaused << CardFinished; }
    bool convertible(const CardUseStruct &use) const
    {
        if (!use.from || !use.card || use.card->objectName() != "slash") return false;
        FireSlash preview(use.card->getSuit(), use.card->getNumber());
        preview.addSubcard(use.card); preview.setSkillName(objectName());
        for (ServerPlayer *target : use.to) if (!use.from->canSlash(target, &preview, false)) return false;
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return event == ChangeSlash && player && player->isAlive() && player->hasSkill(objectName())
            && convertible(data.value<CardUseStruct>()) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event == ChangeSlash) return false;
        if (!player) return true;
        const Card *card = event == DamageCaused ? data.value<DamageStruct>().card : data.value<CardUseStruct>().card;
        if (!card) return true;
        const QVariantMap receipt = card->getTag("MobileLihuoReceipt").toMap();
        const qint64 useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (receipt.isEmpty() || useId <= 0 || receipt.value("use_id").toLongLong() != useId
            || receipt.value("actor").toString() != player->objectName()) return true;
        int amount = receipt.value("amount").toInt();
        ServerPlayer *target = player;
        if (event == DamageCaused) {
            if (!receipt.value("chained").toBool()) return true;
            target = data.value<DamageStruct>().to;
        } else {
            if (player->hasFlag("Global_ProcessBroken")) return true;
            const QVariantMap history = room->queryCardUseDamage(useId);
            if (history.contains("error") || !history.value("complete").toBool()) return true;
            int total = 0;
            for (const QVariant &value : history.value("items").toList()) {
                const QVariantMap damage = value.toMap().value("data").toMap();
                if (!damage.contains("amount")) return true;
                total += damage.value("amount").toInt();
            }
            amount *= total / 2;
        }
        if (!target || target->isDead() || amount <= 0) return true;
        SkillContext ctx;
        ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        ctx.invoker = player; ctx.initiator = player;
        ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
            SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
        if (!ctx.owner || !ctx.sourceRef.isValid()) return true;
        ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt();
        ctx.amount = amount; ctx.is_forced = true; ctx.extra_data = receipt;
        ctx.original_data = &data; ctx.current_event = event; ctx.targets = {target}; contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.original_data) return false;
        const Card *card = ctx.current_event == DamageCaused ? ctx.original_data->value<DamageStruct>().card
            : ctx.original_data->value<CardUseStruct>().card;
        return card && card->getTag("MobileLihuoReceipt").toMap() == ctx.extra_data.toMap();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != ChangeSlash) return true;
        if (!convertible(ctx.original_data->value<CardUseStruct>())
            || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data, false)) return false;
        ctx.targets = {ctx.owner}; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == DamageCaused) {
            if (ctx.original_data->value<DamageStruct>().to == target)
                target->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
            return false;
        }
        if (event == CardFinished) {
            const Card *card = ctx.original_data->value<CardUseStruct>().card;
            if (card) card->removeTag("MobileLihuoReceipt");
            room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.owner));
            return false;
        }
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (target != use.from || !convertible(use)) return false;
        const qint64 previous = room->getTag("MobileLihuoSequence").toLongLong();
        if (previous < 0 || previous >= INT_MAX) return false;
        const int serial = int(previous + 1); room->setTag("MobileLihuoSequence", serial);
        bool chained = false;
        for (ServerPlayer *p : use.to) chained = chained || p->isChained();
        FireSlash *slash = new FireSlash(use.card->getSuit(), use.card->getNumber());
        slash->addSubcard(use.card); slash->setSkillName(objectName()); slash->deleteLater();
        use.sourceRef = ctx.sourceRef; use.activationRef = ctx.activationRef; use.changeCard(slash);
        // The replacement virtual card owns its primitive receipt for this exact use only.
        slash->setTag("MobileLihuoReceipt", QVariantMap{{"serial", serial}, {"owner", ctx.owner->objectName()},
            {"actor", target->objectName()}, {"amount", getEffectiveAmount(ctx)}, {"chained", chained},
            {"use_id", room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id")},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}});
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

MobileZongxuanCard::MobileZongxuanCard()
{
    setSkillName("mobilezongxuan");
    target_fixed = true;
}

void MobileZongxuanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    source->drawCards(1, "mobilezongxuan");
    if (source->isDead() || source->isNude()) return;
    const Card *c = room->askForExchange(source, "mobilezongxuan", 1, 1, true, "mobilezongxuan-put");
    CardMoveReason reason(CardMoveReason::S_REASON_PUT, source->objectName(), "mobilezongxuan", "");
    room->moveCardTo(c, nullptr, Player::DrawPile, reason, false);
}

MobileZongxuanPutCard::MobileZongxuanPutCard()
{
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
    m_skillName = "mobilezongxuan";
}

void MobileZongxuanPutCard::use(Room *, ServerPlayer *, QList<ServerPlayer *> &) const
{
}

class MobileZongxuanVS : public ViewAsSkillV2
{
public:
    MobileZongxuanVS() : ViewAsSkillV2("mobilezongxuan") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileZongxuanCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    TargetMode targetMode() const override { return NoTarget; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(ctx, ctx.invoker);
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if(getEffectiveAmount(ctx)<=0) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        target->drawCards(getEffectiveAmount(ctx), objectName());
        if (target->isDead() || target->isNude()) return ContinueEffects;
        const Card *selected = room->askForExchange(target, objectName(), 1, 1, true, "mobilezongxuan-put");
        if (!selected || selected->subcardsLength() != 1) return ContinueEffects;
        const int id = selected->getSubcards().first();
        if (room->getCardOwner(id) == target && (room->getCardPlace(id) == Player::PlaceHand
            || room->getCardPlace(id) == Player::PlaceEquip))
            room->moveCardTo(Sanguosha->getCard(id), target, nullptr, Player::DrawPile,
                CardMoveReason(CardMoveReason::S_REASON_PUT, target->objectName(), objectName(), ""), false);
        return ContinueEffects;
    }
};

class MobileZongxuan : public TriggerSkillV2
{
public:
    MobileZongxuan() : TriggerSkillV2("mobilezongxuan")
    {
        events << CardsMoveOneTime;
        view_as_skill = new MobileZongxuanVS;
    }
    QList<int> candidates(Room *room, const CardsMoveOneTimeStruct &move) const
    {
        QList<int> ids;
        if (move.to_place != Player::DiscardPile
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) return ids;
        for (int index = 0; index < move.card_ids.size() && index < move.from_places.size(); ++index)
            if ((move.from_places.at(index) == Player::PlaceHand || move.from_places.at(index) == Player::PlaceEquip)
                && room->getCardPlace(move.card_ids.at(index)) == Player::DiscardPile) ids << move.card_ids.at(index);
        return ids;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && move.from == player
            && !candidates(room, move).isEmpty() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<int> available = candidates(room, ctx.original_data->value<CardsMoveOneTimeStruct>());
        QList<int> selected;
        while (!available.isEmpty()) {
            room->fillAG(available, ctx.owner);
            const auto clear = qScopeGuard([&] { room->clearAG(ctx.owner); });
            const int id = room->askForAG(ctx.owner, available, true, objectName());
            if (!available.contains(id)) break;
            selected << id;
            available.removeOne(id);
        }
        if (selected.isEmpty()) return false;
        ctx.extra_data = ListI2V(selected);
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QList<int> ids;
        for (int id : ListV2I(ctx.extra_data.toList()))
            if (room->getCardPlace(id) == Player::DiscardPile) ids << id;
        if (ids.isEmpty()) return false;
        LogMessage log;
        log.type = "$YinshicaiPut";
        log.from = target;
        log.card_str = ListI2S(ids).join("+");
        room->sendLog(log);
        DummyCard cards(ids);
        // Only the accepted discard materials can move; no temporary pile or second skill activation is used.
        room->moveCardTo(&cards, nullptr, Player::DrawPile,
            CardMoveReason(CardMoveReason::S_REASON_PUT, ctx.owner->objectName(), objectName(), ""), true, true);
        return false;
    }
};

MobileJunxingCard::MobileJunxingCard()
{
    setSkillName("mobilejunxing");
}

void MobileJunxingCard::onEffect(CardEffectStruct &effect) const
{
    if (effect.to->isDead()) return;
    int length = subcardsLength();
    int can_dis = 0;
    QList<int> list = effect.to->handCards() + effect.to->getEquipsId();
    foreach (int id, list) {
        if (effect.to->canDiscard(effect.to, id))
            can_dis++;
    }

    if (can_dis < length) {
        effect.to->turnOver();
        effect.to->drawCards(length, "mobilejunxing");
        return;
    }

    Room *room = effect.from->getRoom();
    effect.to->setTag("mobilejunxing_effect", QVariant::fromValue(effect.from));
    const Card *card = room->askForDiscard(effect.to, "mobilejunxing", length, length, true, true);
    effect.to->removeTag("mobilejunxing_effect");
    if (!card) {
        effect.to->turnOver();
        effect.to->drawCards(length, "mobilejunxing");
    } else
        room->loseHp(HpLostStruct(effect.to, 1, "mobilejunxing", effect.from));
}

class MobileJunxing : public ViewAsSkillV2
{
public:
    MobileJunxing() : ViewAsSkillV2("mobilejunxing", 999)
    {
        setPhaseName("Play");
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileJunxingCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && !request.selectedCardIds.contains(card->getEffectiveId())
            && request.initiator->handCards().contains(card->getEffectiveId())
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return !request.selectedCardIds.isEmpty(); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return target && target != request.initiator && selected.isEmpty();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        const int count = ctx.use_card->subcardsLength()*getEffectiveAmount(ctx);
        if(count<=0) return ContinueEffects;
        int available = 0;
        foreach (int id, target->handCards() + target->getEquipsId()) {
            if (target->canDiscard(target, id)) ++available;
        }
        bool discarded = false;
        if (available >= count) {
            const QVariant previous=target->getTag("mobilejunxing_effect"); auto restore=qScopeGuard([&]{target->setTag("mobilejunxing_effect",previous);});
            target->setTag("mobilejunxing_effect", QVariant::fromValue(ctx.invoker));
            discarded = room->askForDiscard(target, objectName(), count, count, true, true) != nullptr;

        }
        if (discarded)
            room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        else {
            target->turnOver();
            if (target->isAlive()) target->drawCards(count, objectName());
        }
        return ContinueEffects;
    }
};

class MobileJuece : public TriggerSkillV2
{
public:
    MobileJuece() : TriggerSkillV2("mobilejuece")
    {
        events << EventPhaseStart;
    }
    QList<ServerPlayer *> candidates(Room *room, ServerPlayer *owner) const
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toString().isEmpty() || turn.toString() == "0") return {};
        QVariantMap filter{{"turn_id", turn}, {"limit", 128}};
        QSet<QString> lost;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return {};
            foreach (const QVariant &item, page.value("items").toList()) {
                const QVariantMap move = item.toMap().value("data").toMap();
                if (!move.contains("from_place")) continue;
                const int place = move.value("from_place").toInt();
                if (place == Player::PlaceHand || place == Player::PlaceEquip)
                    lost.insert(move.value("from").toString());
            }
            if (!page.value("has_more").toBool()) break;
            // Keep one immutable history frontier across all pages of this query.
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *player, room->getOtherPlayers(owner)) {
            if (lost.contains(player->objectName())) targets << player;
        }
        return targets;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Finish && !candidates(room, player).isEmpty()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates(room, ctx.owner),
            objectName(), "@mobilejuece", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        return false;
    }
};

MobileMiejiCard::MobileMiejiCard()
{
    setSkillName("mobilemieji");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool MobileMiejiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && !to_select->isNude();
}

void MobileMiejiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->showCard(effect.from, getSubcards().first());

    CardMoveReason reason(CardMoveReason::S_REASON_PUT, effect.from->objectName(), "", "mobilemieji", "");
    room->moveCardTo(this, effect.from, nullptr, Player::DrawPile, reason, true);

    QList<const Card *> trick, nottrick;
    foreach (const Card *c, effect.to->getCards("he")) {
        if (c->isKindOf("TrickCard"))
            trick << c;
        else if (!c->isKindOf("TrickCard") && effect.to->canDiscard(effect.to, c->getEffectiveId()))
            nottrick << c;
    }

    if (trick.isEmpty() && nottrick.isEmpty()) return;

    if (trick.isEmpty() && !nottrick.isEmpty())
        room->askForDiscard(effect.to, "mobilemieji", 2, 2, false, true, "@mobilemieji_nottrick", "^TrickCard");
    else if (nottrick.isEmpty() && !trick.isEmpty()) {
        const Card *c = room->askForExchange(effect.to, "mobilemieji", 1, 1, false, "@mobilemieji_trick:" + effect.from->objectName(), false, "TrickCard");
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.to->objectName(), effect.from->objectName(), "mobilemieji", "");
        room->obtainCard(effect.from, c, reason, true);
    } else {
        const Card *cc = room->askForUseCard(effect.to, "@@mobilemieji!", "@mobilemieji:" + effect.from->objectName());
        if (!cc) {
            if (!trick.isEmpty()) {
                const Card *give = trick.at(qsanRandomBounded(trick.length()));
                CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.to->objectName(), effect.from->objectName(), "mobilemieji", "");
                room->obtainCard(effect.from, give, reason, true);
                return;
            }
            if (!nottrick.isEmpty()) {
                DummyCard *dis = new DummyCard;
                const Card *d = nottrick.at(qsanRandomBounded(nottrick.length()));
                nottrick.removeOne(d);
                dis->addSubcard(d);
                if (!nottrick.isEmpty()) {
                    const Card *dd = nottrick.at(qsanRandomBounded(nottrick.length()));
                    dis->addSubcard(dd);
                }
                room->throwCard(dis, effect.to, nullptr);
                dis->deleteLater();
            }
        } else {
            const Card *c = Sanguosha->getCard(cc->getSubcards().first());
            if (c->isKindOf("TrickCard")) {
                CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.to->objectName(), effect.from->objectName(), "mobilemieji", "");
                room->obtainCard(effect.from, c, reason, true);
            } else
                room->throwCard(cc, effect.to, nullptr);
        }
    }

}

MobileMiejiDiscardCard::MobileMiejiDiscardCard()
{
    will_throw = false;
    target_fixed = true;
    mute = true;
    handling_method = Card::MethodNone;
    m_skillName = "mobilemieji";
}

void MobileMiejiDiscardCard::onUse(Room *, CardUseStruct &) const
{
}

class MobileMieji : public ViewAsSkillV2
{
public:
    MobileMieji() : ViewAsSkillV2("mobilemieji", 1)
    {
        setPhaseName("Play");
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileMiejiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && !card->isVirtualCard()
            && !card->hasFlag("using") && card->isBlack() && card->isKindOf("TrickCard")
            && request.initiator->handCards().contains(card->getEffectiveId());
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return target && target != request.initiator && !target->isNude() && selected.isEmpty();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        const int id = request.selectedCardIds.first();
        const Card *card = Sanguosha->getCard(id);
        if (!ctx.initiator || !ctx.initiator->handCards().contains(id) || !card->isBlack() || !card->isKindOf("TrickCard")) return false;
        room->showCard(ctx.initiator, id);
        if (!ctx.initiator->handCards().contains(id)) return false;
        room->moveCardTo(card, ctx.initiator, nullptr, Player::DrawPile,
            CardMoveReason(CardMoveReason::S_REASON_PUT, ctx.invoker->objectName(), "", objectName(), ""), true);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        int tricks = 0, discards = 0;
        foreach (const Card *card, target->getCards("he")) {
            if (card->isKindOf("TrickCard")) ++tricks;
            else if (target->canDiscard(target, card->getEffectiveId())) ++discards;
        }
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return ContinueEffects;
        // The recipient makes a material decision, not an activation of a skill it does not own.
        if (tricks > 0 && ctx.invoker->isAlive()) {
            const int count = qMin(amount, tricks);
            const Card *gift = room->askForExchange(target, objectName(), count, count, true,
                "@mobilemieji_trick:" + ctx.invoker->objectName(), discards > 0, "TrickCard");
            if (gift) {
                room->giveCard(target, ctx.invoker, gift, objectName());
                return ContinueEffects;
            }
        }
        if (discards > 0) {
            const int count = qMin(2 * amount, discards);
            room->askForDiscard(target, objectName(), count, count, false, true, "@mobilemieji_nottrick", "^TrickCard");
        }
        return ContinueEffects;
    }
};

MobileXianzhenCard::MobileXianzhenCard()
{
    setSkillName("mobilexianzhen");
    will_throw = false;
    handling_method = Card::MethodPindian;
}

bool MobileXianzhenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void MobileXianzhenCard::onEffect(CardEffectStruct &effect) const
{
    if (!effect.from->canPindian(effect.to, false)) return;

    Room *room = effect.from->getRoom();

    if (effect.from->pindian(effect.to, "mobilexianzhen")) {
        room->addPlayerMark(effect.to, "Armor_Nullified");
        room->addPlayerMark(effect.from, "mobilexianzhen_from-PlayClear");
        room->addPlayerMark(effect.to, "mobilexianzhen_to-PlayClear");
    } else {
        room->addPlayerMark(effect.from, "mobilexianzhen_lose-PlayClear");
        room->setPlayerCardLimitation(effect.from, "use", "Slash", true);
    }
}

class MobileXianzhenVS : public ViewAsSkillV2
{
public:
    MobileXianzhenVS() : ViewAsSkillV2("mobilexianzhen") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileXianzhenCard"; }
    bool canActivate(const ActiveSkillRequest &r) const override { return r.initiator && r.reason==CardUseStruct::CARD_USE_REASON_PLAY && r.initiator->canPindian(); }
    bool canSelectTarget(const ActiveSkillRequest &r,const QList<const Player *> &targets,const Player *target) const override
    { return r.initiator && target && target->isAlive() && targets.isEmpty() && r.initiator->canPindian(target); }
    bool targetsFeasible(const ActiveSkillRequest &,const QList<const Player *> &targets) const override { return targets.size()==1; }
    EffectFlow effectOnTarget(SkillContext &ctx,ServerPlayer *target) const override
    {
        if(getEffectiveAmount(ctx)<=0 || !ctx.invoker) return ContinueEffects; Room *room=target->getRoom();
        if(ctx.choice.isEmpty()) {
            if(!ctx.invoker->canPindian(target,false)) return ContinueEffects;
            std::unique_ptr<PindianStruct> result(ctx.invoker->PinDian(target,objectName())); if(!result) return ContinueEffects;
            const bool slash=result->from_card && result->from_card->isKindOf("Slash"),success=result->success;
            SkillContext benefit=ctx; benefit.choice=success?"win":"lose"; benefit.extra_data=QVariantMap{{"opponent",target->objectName()},{"slash",slash}}; skillEffect(benefit,ctx.invoker); return ContinueEffects;
        }
        const int serial=room->getTag("MobileXianzhenSerial").toInt()+1; room->setTag("MobileXianzhenSerial",serial);
        QVariantMap r{{"serial",serial},{"owner",ctx.activationRef.ownerObjectName},{"skill",ctx.activationRef.key.skillName},{"instance",ctx.activationRef.key.instanceID},
            {"source_owner",ctx.sourceRef.ownerObjectName},{"source_skill",ctx.sourceRef.key.skillName},{"source_instance",ctx.sourceRef.key.instanceID},
            {"actor",target->objectName()},{"target",ctx.extra_data.toMap().value("opponent")},{"phase",room->historyScopes().value("phase_id")},{"turn",room->historyScopes().value("turn_id")},
            {"amount",getEffectiveAmount(ctx)},{"win",ctx.choice=="win"},{"slash",ctx.extra_data.toMap().value("slash")},{"phase_expired",false}};
        ServerPlayer *opponent=room->findPlayerByObjectName(r.value("target").toString());
        if(ctx.choice=="win" && (!opponent || opponent->isDead())) return ContinueEffects;
        QVariantList receipts=room->getTag("MobileXianzhenReceipts").toList(); receipts << r; room->setTag("MobileXianzhenReceipts",receipts);
        if(ctx.choice=="win") { room->setPlayerEquipsNullified(opponent,"Armor|.|.|.|target:"+target->objectName(),objectName()+":"+QString::number(serial),false); project(room,target); }
        else room->setPlayerCardLimitation(target,"use","Slash",false,objectName()+":"+QString::number(serial));
        return ContinueEffects;
    }
    static void project(Room *room,ServerPlayer *actor)
    {
        QStringList names; for(const QVariant &value:room->getTag("MobileXianzhenReceipts").toList()) { const QVariantMap r=value.toMap(); if(r.value("actor").toString()==actor->objectName() && r.value("win").toBool() && !r.value("phase_expired").toBool()) names << r.value("target").toString(); }
        room->setPlayerProperty(actor,"mobilexianzhen_targets",names.join('+'));
    }
};

class MobileXianzhen : public TriggerSkillV2
{
public:
    MobileXianzhen() : TriggerSkillV2("mobilexianzhen") { global=true; frequency=Compulsory; events << EventPhaseProceeding; view_as_skill=new MobileXianzhenVS; }
    bool collectTriggerContexts(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data,QList<SkillContext> &contexts) const override
    {
        if(!actor || actor->isDead() || actor->getPhase()!=Player::Discard) return true;
        for(const QVariant &value:room->getTag("MobileXianzhenReceipts").toList()) {
            const QVariantMap r=value.toMap(); if(!r.value("slash").toBool() || r.value("actor").toString()!=actor->objectName() || r.value("turn")!=room->historyScopes().value("turn_id")) continue;
            SkillContext ctx; ctx.skill_name=objectName(); ctx.owner=room->findPlayerByObjectName(r.value("owner").toString(),true); ctx.invoker=actor; ctx.initiator=actor; ctx.targets={actor};
            ctx.instanceID=r.value("serial").toInt(); ctx.sourceRef=SkillInstanceRef(r.value("source_owner").toString(),SkillInstanceKey(r.value("source_skill").toString(),r.value("source_instance").toInt()));
            ctx.amount=r.value("amount").toInt(); ctx.extra_data=r; ctx.is_forced=true; ctx.current_event=event; ctx.original_data=&data; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room,const SkillContext &ctx) const override { return room->getTag("MobileXianzhenReceipts").toList().contains(ctx.extra_data); }
    bool effectTarget(TriggerEvent,Room *room,ServerPlayer *,SkillContext &ctx,ServerPlayer *target) const override
    { if(getEffectiveAmount(ctx)>0) { QList<int> ids; for(const Card *card:target->getCards("h")) if(card->isKindOf("Slash")) ids << card->getEffectiveId(); if(!ids.isEmpty()) room->ignoreCards(target,ids); } return false; }
};

class MobileXianzhenClear : public TriggerSkillV2
{
public:
    MobileXianzhenClear() : TriggerSkillV2("#mobilexianzhen-clear") { global=true; events << EventPhaseChanging << Death << TurnBroken; }
    bool recordEvent(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data) const override
    {
        if(!actor || (event==Death && data.value<DeathStruct>().who!=actor)) return false;
        const bool phaseEnd=event==EventPhaseChanging;
        const bool turnEnd=event==TurnBroken || (event==EventPhaseChanging && data.value<PhaseChangeStruct>().to==Player::NotActive);
        if(!phaseEnd && !turnEnd && event!=Death) return false;
        QVariantList kept,expired; const QVariant phase=room->historyScopes().value("phase_id"),turn=room->historyScopes().value("turn_id");
        for(const QVariant &value:room->getTag("MobileXianzhenReceipts").toList()) {
            QVariantMap r=value.toMap(); const bool retire=(event==Death && r.value("actor").toString()==actor->objectName()) || (turnEnd && r.value("turn")==turn);
            const bool expire=retire || (phaseEnd && room->historyEvent(r.value("phase").toLongLong()).value("status").toString()=="finished");
            if(expire && !r.value("phase_expired").toBool()) expired << r;
            if(!retire) { if(expire) r["phase_expired"]=true; kept << r; }
        }
        // Publish the precise survivors before mark and limitation callbacks.
        room->setTag("MobileXianzhenReceipts",kept);
        for(const QVariant &value:expired) { const QVariantMap r=value.toMap();
            if(r.value("win").toBool()) { if(ServerPlayer *target=room->findPlayerByObjectName(r.value("target").toString(),true)) room->removePlayerEquipsNullified(target,"Armor|.|.|.|target:"+r.value("actor").toString()+"$0","mobilexianzhen:"+r.value("serial").toString()); }
            else if(ServerPlayer *payer=room->findPlayerByObjectName(r.value("actor").toString(),true)) room->removePlayerCardLimitation(payer,"use","Slash$0","mobilexianzhen:"+r.value("serial").toString());
        }
        for(ServerPlayer *p:room->getAllPlayers(true)) MobileXianzhenVS::project(room,p); return false;
    }
    TriggerList triggerable(TriggerEvent,Room *,ServerPlayer *,QVariant &) const override { return {}; }
};

class MobileXianzhenTargetMod : public TargetModSkillV2
{
public:
    MobileXianzhenTargetMod() : TargetModSkillV2("#mobilexianzhen-target","^SkillCard") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if(!ctx.primary || !ctx.secondary || !ctx.primary->property("mobilexianzhen_targets").toString().split('+').contains(ctx.secondary->objectName())) return CorrectSkillResult::noEffect();
        if(ctx.modType==Residue) return CorrectSkillResult::unlimitedResidue();
        return ctx.modType==DistanceLimit?CorrectSkillResult::useAmount(999):CorrectSkillResult::noEffect();
    }
};

class MobileJinjiu : public FilterSkill
{
public:
    MobileJinjiu() : FilterSkill("mobilejinjiu")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->objectName() == "analeptic";
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->setSkillName(objectName());/*
        WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
        card->takeOver(slash);*/
        return slash;
    }
};

class MobileJinjiuLimit : public CardLimitSkill
{
public:
    MobileJinjiuLimit() : CardLimitSkill("#mobilejinjiu-limit")
    {
    }

    QString limitList(const Player *) const
    {
        return "use";
    }

    QString limitPattern(const Player *target) const
    {
        foreach (const Player *p, target->getAliveSiblings()) {
            if (p->hasFlag("CurrentPlayer") && p->hasSkill("mobilejinjiu"))
                return "Analeptic";
        }
        return "";
    }
};

class MobileJinjiuEffect : public TriggerSkillV2
{
public:
    MobileJinjiuEffect() : TriggerSkillV2("#mobilejinjiu")
    {
        events << DamageInflicted;
    }

    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.is_forced = true;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill("mobilejinjiu") && damage.card
            && damage.card->isKindOf("Slash") && damage.card->getTag("drank").toInt() > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariant &data = *ctx.original_data;
        DamageStruct damage = data.value<DamageStruct>();
        if (target != damage.to || !damage.card) return false;
        int drank = damage.card->getTag("drank").toInt();
        if (drank <= 0) return false;
        room->broadcastSkillInvoke("mobilejinjiu");
        room->notifySkillInvoked(ctx.owner, "mobilejinjiu");
        int n = damage.damage;
        LogMessage log;
        log.type = "#MobilejinjiuReduce";
        log.from = ctx.owner;
        log.arg = "mobilejinjiu";
        damage.damage -= drank * getEffectiveAmount(ctx);
        log.arg2 = QString::number(damage.damage);
        if (damage.damage <= 0) {
            log.type = "#MobilejinjiuPrevent";
            log.arg2 = QString::number(n);
            room->sendLog(log);
            return true;
        }
        room->sendLog(log);
        data = QVariant::fromValue(damage);
        return false;
    }
};

MobileQiaoshuiCard::MobileQiaoshuiCard()
{
    setSkillName("mobileqiaoshui");
}

bool MobileQiaoshuiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void MobileQiaoshuiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    if (!source->canPindian(targets.first(), false)) return;
    bool success = source->pindian(targets.first(), "mobileqiaoshui", nullptr);
    if (success)
        source->setFlags("MobileQiaoshuiSuccess");
    else {
        source->setFlags("MobileQiaoshuiNotSuccess");
        room->setPlayerCardLimitation(source, "use", "TrickCard", true);
    }
}

class MobileQiaoshuiViewAsSkill : public ViewAsSkillV2
{
public:
    MobileQiaoshuiViewAsSkill() : ViewAsSkillV2("mobileqiaoshui") { setPhaseName("Play"); response_pattern="@@mobileqiaoshui!"; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &r) const override { return r.pattern=="@@mobileqiaoshui!"?"ExtraCollateralCard":"MobileQiaoshuiCard"; }
    bool canActivate(const ActiveSkillRequest &r) const override
    { return r.initiator && (r.pattern=="@@mobileqiaoshui!" || (r.reason==CardUseStruct::CARD_USE_REASON_PLAY && r.initiator->canPindian())); }
    bool canSelectTarget(const ActiveSkillRequest &r,const QList<const Player *> &selected,const Player *target) const override
    {
        if(!r.initiator || !target || target->isDead() || selected.contains(target)) return false;
        if(r.pattern=="@@mobileqiaoshui!") {
            if(selected.isEmpty()) return r.initiator->property("mobileqiaoshui_available").toString().split('+').contains(target->objectName());
            return selected.size()==1 && selected.first()->canSlash(target);
        }
        return selected.isEmpty() && r.initiator->canPindian(target);
    }
    bool targetsFeasible(const ActiveSkillRequest &r,const QList<const Player *> &targets) const override { return targets.size()==(r.pattern=="@@mobileqiaoshui!"?2:1); }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.manual_effect=true;
        if(ctx.targets.size()==2) {
            // The response only selects a Collateral pair; the accepted parent applies its recipient hook.
            ctx.initiator->setTag("MobileQiaoshuiCollateralReply",QStringList{ctx.targets.first()->objectName(),ctx.targets.last()->objectName()}); return ContinueEffects;
        }
        if(ctx.targets.size()==1) skillEffect(ctx,ctx.targets.first()); return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx,ServerPlayer *target) const override
    {
        if(getEffectiveAmount(ctx)<=0 || !ctx.invoker) return ContinueEffects; Room *room=target->getRoom();
        if(ctx.choice.isEmpty()) {
            if(!ctx.invoker->canPindian(target,false)) return ContinueEffects;
            const bool success=ctx.invoker->pindian(target,objectName()); SkillContext result=ctx; result.choice=success?"win":"lose"; skillEffect(result,ctx.invoker); return ContinueEffects;
        }
        const int serial=room->getTag("MobileQiaoshuiSerial").toInt()+1; room->setTag("MobileQiaoshuiSerial",serial);
        QVariantMap r{{"serial",serial},{"owner",ctx.activationRef.ownerObjectName},{"skill",ctx.activationRef.key.skillName},{"instance",ctx.activationRef.key.instanceID},
            {"source_owner",ctx.sourceRef.ownerObjectName},{"source_skill",ctx.sourceRef.key.skillName},{"source_instance",ctx.sourceRef.key.instanceID},
            {"actor",target->objectName()},{"phase",room->historyScopes().value("phase_id")},{"turn",room->historyScopes().value("turn_id")},{"amount",getEffectiveAmount(ctx)},{"win",ctx.choice=="win"}};
        QVariantList receipts=target->getTag("MobileQiaoshuiReceipts").toList(); receipts << r; target->setTag("MobileQiaoshuiReceipts",receipts);
        if(ctx.choice=="lose") room->setPlayerCardLimitation(target,"use","TrickCard",false,objectName()+":"+QString::number(serial)); return ContinueEffects;
    }
};

class MobileQiaoshui : public TriggerSkillV2
{
public:
    MobileQiaoshui() : TriggerSkillV2("mobileqiaoshui") { global=true; events << PreCardUsed << EventPhaseChanging << Death << TurnBroken; view_as_skill=new MobileQiaoshuiViewAsSkill; }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent event) const override { return event==EventPhaseEnd?0:TriggerSkillV2::getPriority(event); }
    bool recordEvent(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data) const override
    {
        if(!actor) return false;
        if(event==PreCardUsed) {
            const CardUseStruct use=data.value<CardUseStruct>(); if(actor->getPhase()!=Player::Play || !use.card || (!use.card->isNDTrick() && !use.card->isKindOf("BasicCard"))) return false;
            QVariantList kept,due;
            for(const QVariant &value:actor->getTag("MobileQiaoshuiReceipts").toList()) { QVariantMap r=value.toMap();
                if(r.value("win").toBool() && r.value("phase")==room->historyScopes().value("phase_id")) { r["use_id"]=room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id"); due << r; } else kept << value; }
            actor->setTag("MobileQiaoshuiReceipts",kept); use.card->setTag("MobileQiaoshuiDue",due); return false;
        }
        const bool phaseEnd=event==EventPhaseChanging,turnEnd=event==TurnBroken || (event==EventPhaseChanging && data.value<PhaseChangeStruct>().to==Player::NotActive);
        const bool death=event==Death && data.value<DeathStruct>().who==actor; if(!phaseEnd && !turnEnd && !death) return false;
        for(ServerPlayer *p:room->getAllPlayers(true)) {
            QVariantList kept,expired; for(const QVariant &value:p->getTag("MobileQiaoshuiReceipts").toList()) { const QVariantMap r=value.toMap();
                if((death && p==actor) || (phaseEnd && room->historyEvent(r.value("phase").toLongLong()).value("status").toString()=="finished") || (turnEnd && r.value("turn")==room->historyScopes().value("turn_id"))) expired << value; else kept << value; }
            p->setTag("MobileQiaoshuiReceipts",kept);
            for(const QVariant &value:expired) if(!value.toMap().value("win").toBool()) room->removePlayerCardLimitation(p,"use","TrickCard$0",objectName()+":"+value.toMap().value("serial").toString());
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data,QList<SkillContext> &contexts) const override
    {
        if(event!=PreCardUsed || !actor || actor->isDead() || Sanguosha->currentRoomState()->getCurrentCardUseReason()!=CardUseStruct::CARD_USE_REASON_PLAY) return true;
        const CardUseStruct use=data.value<CardUseStruct>(); if(!use.card) return true;
        const qint64 useId=room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id").toLongLong();
        for(const QVariant &value:use.card->getTag("MobileQiaoshuiDue").toList()) {
            const QVariantMap r=value.toMap(); if(useId<=0 || r.value("use_id").toLongLong()!=useId) continue;
            SkillContext ctx; ctx.skill_name=objectName(); ctx.owner=room->findPlayerByObjectName(r.value("owner").toString(),true); ctx.invoker=actor; ctx.initiator=actor; ctx.instanceID=r.value("serial").toInt();
            ctx.sourceRef=SkillInstanceRef(r.value("source_owner").toString(),SkillInstanceKey(r.value("source_skill").toString(),r.value("source_instance").toInt()));
            ctx.extra_data=r; ctx.amount=r.value("amount").toInt(); ctx.current_event=event; ctx.original_data=&data; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *,const SkillContext &ctx) const override
    { const Card *card=ctx.original_data->value<CardUseStruct>().card; return card && card->getTag("MobileQiaoshuiDue").toList().contains(ctx.extra_data); }
    bool effect(TriggerEvent event,Room *room,ServerPlayer *actor,SkillContext &ctx) const override
    {
        ctx.manual_effect=true; if(getEffectiveAmount(ctx)<=0) return false;
        for(int repetition=0;repetition<getEffectiveAmount(ctx) && ctx.invoker->isAlive();++repetition) {
            const CardUseStruct use=ctx.original_data->value<CardUseStruct>(); QList<ServerPlayer *> available;
            if(!use.card->isKindOf("AOE") && !use.card->isKindOf("GlobalEffect")) {
                const bool prior=ctx.invoker->hasFlag("MobileQiaoshuiExtraTarget"); room->setPlayerFlag(ctx.invoker,"MobileQiaoshuiExtraTarget");
                auto restore=qScopeGuard([&]{ if(!prior) room->setPlayerFlag(ctx.invoker,"-MobileQiaoshuiExtraTarget"); });
                for(ServerPlayer *p:room->getAlivePlayers()) {
                    if(use.to.contains(p) || ctx.invoker->isProhibited(p,use.card)) continue;
                    if(use.card->targetFixed()) { if(!use.card->isKindOf("Peach") || p->isWounded()) available << p; }
                    else { int extra=0; if(use.card->targetFilter({},p,ctx.invoker,extra) || extra>0) available << p; }
                }
            }
            QStringList choices{"cancel"}; if(use.to.size()>1) choices.prepend("remove"); if(!available.isEmpty()) choices.prepend("add"); if(choices.size()==1) break;
            const QString choice=room->askForChoice(ctx.invoker,objectName(),choices.join('+'),*ctx.original_data); if(choice=="cancel") break;
            ServerPlayer *target=nullptr; QString victim;
            if(choice=="add" && use.card->isKindOf("Collateral")) {
                const QVariant prior=ctx.invoker->property("mobileqiaoshui_available"),reply=ctx.invoker->getTag("MobileQiaoshuiCollateralReply");
                auto restore=qScopeGuard([&]{ ctx.invoker->setProperty("mobileqiaoshui_available",prior); room->notifyProperty(ctx.invoker,ctx.invoker,"mobileqiaoshui_available"); ctx.invoker->setTag("MobileQiaoshuiCollateralReply",reply); });
                QStringList names; for(ServerPlayer *p:available) names << p->objectName(); ctx.invoker->setProperty("mobileqiaoshui_available",names.join('+')); room->notifyProperty(ctx.invoker,ctx.invoker,"mobileqiaoshui_available"); ctx.invoker->removeTag("MobileQiaoshuiCollateralReply");
                const QVariantMap r=ctx.extra_data.toMap(); SkillContext accepted=ctx; accepted.activationRef=SkillInstanceRef(r.value("owner").toString(),SkillInstanceKey(r.value("skill").toString(),r.value("instance").toInt()));
                Room::AcceptedViewAsEffectScope prompt(room,ctx.invoker,objectName(),accepted); if(prompt.isValid()) room->askForUseCard(ctx.invoker,"@@mobileqiaoshui!","@qiaoshui-add:::collateral");
                const QStringList pair=ctx.invoker->getTag("MobileQiaoshuiCollateralReply").toStringList(); if(pair.size()==2) { target=room->findPlayerByObjectName(pair.first()); victim=pair.last(); }
                if(!target || !available.contains(target)) {
                    target=available.at(qsanRandomBounded(available.size())); QList<ServerPlayer *> victims;
                    for(ServerPlayer *p:room->getOtherPlayers(target)) if(target->canSlash(p)) victims << p;
                    if(victims.isEmpty()) continue; victim=victims.at(qsanRandomBounded(victims.size()))->objectName();
                }
            } else target=room->askForPlayerChosen(ctx.invoker,choice=="add"?available:use.to,"qiaoshui",choice=="add"?"@qiaoshui-add:::"+use.card->objectName():"@qiaoshui-remove:::"+use.card->objectName());
            if(!target) continue; SkillContext local=ctx; local.choice=choice; local.extra_data=QVariantMap{{"victim",victim}}; skillEffect(event,room,actor,local,target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent,Room *room,ServerPlayer *,SkillContext &ctx,ServerPlayer *target) const override
    {
        if(getEffectiveAmount(ctx)<=0) return false; CardUseStruct use=ctx.original_data->value<CardUseStruct>();
        if(ctx.choice=="remove") { if(use.to.size()<=1 || !use.to.contains(target)) return false; use.to.removeOne(target); }
        else {
            if(use.to.contains(target) || ctx.invoker->isProhibited(target,use.card)) return false;
            if(use.card->isKindOf("Collateral")) { ServerPlayer *victim=room->findPlayerByObjectName(ctx.extra_data.toMap().value("victim").toString()); if(!victim || !target->canSlash(victim)) return false; target->setTag("attachTarget",QVariant::fromValue(victim)); }
            use.to << target; room->sortByActionOrder(use.to);
        }
        *ctx.original_data=QVariant::fromValue(use); return false;
    }
};

class MobileQiaoshuiTargetMod : public TargetModSkillV2
{
public:
    MobileQiaoshuiTargetMod() : TargetModSkillV2("#mobileqiaoshui-target", "Slash,TrickCard+^DelayedTrick")
    {
        frequency = NotFrequent;
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType == DistanceLimit && ctx.primary && ctx.primary->hasFlag("MobileQiaoshuiExtraTarget"))
            return CorrectSkillResult::useAmount(1000);
        return CorrectSkillResult::noEffect();
    }
};

MobileZongshihCard::MobileZongshihCard()
{
    mute = true;
    handling_method = Card::MethodNone;
    will_throw = false;
    target_fixed = true;
}

void MobileZongshihCard::onUse(Room *, CardUseStruct &) const
{
}

class MobileZongshih : public TriggerSkillV2
{
public:
    MobileZongshih() : TriggerSkillV2("mobilezongshih")
    {
        events << Pindian;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        const PindianStruct *pindian = data.value<PindianStruct *>();
        TriggerList result;
        if (!pindian) return result;
        for (ServerPlayer *owner : QList<ServerPlayer *>{pindian->from, pindian->to})
            if (owner && owner->isAlive() && owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const PindianStruct *pindian = ctx.original_data->value<PindianStruct *>();
        if (!pindian || !pindian->from_card || !pindian->to_card) return false;
        room->broadcastSkillInvoke(objectName());
        QList<int> pindianCards;
        const int fromId = pindian->from_card->getEffectiveId(), toId = pindian->to_card->getEffectiveId();
        if (pindian->from_number <= pindian->to_number && room->getCardPlace(fromId) == Player::PlaceTable)
            pindianCards << fromId;
        if (pindian->to_number <= pindian->from_number && room->getCardPlace(toId) == Player::PlaceTable
            && !pindianCards.contains(toId)) pindianCards << toId;
        const int count = getEffectiveAmount(ctx);
        if (count <= 0) return false;
        const QList<int> top = room->getNCards(count);
        LogMessage log;
        log.type = "$ViewDrawPile";
        log.from = ctx.owner;
        log.card_str = ListI2S(top).join("+");
        room->sendLog(log, ctx.owner);
        QList<int> candidates = top + pindianCards, selected;
        // Selection is a card choice, not a second skill activation with fake material ownership.
        for (int i = 0; i < count && !candidates.isEmpty() && ctx.owner->isAlive(); ++i) {
            room->fillAG(candidates, ctx.owner);
            const int id = room->askForAG(ctx.owner, candidates, true, objectName(), "@mobilezongshih");
            room->clearAG(ctx.owner);
            if (!candidates.contains(id)) break;
            candidates.removeOne(id);
            selected << id;
        }
        room->returnToTopDrawPile(top);
        if (target->isDead()) return false;
        for (int id : QList<int>(selected)) {
            if ((top.contains(id) && room->getCardPlace(id) != Player::DrawPile)
                || (pindianCards.contains(id) && room->getCardPlace(id) != Player::PlaceTable)) selected.removeOne(id);
        }
        if (!selected.isEmpty()) {
            DummyCard cards(selected);
            room->obtainCard(target, &cards, false);
        }
        return false;
    }
};
class MobileDanshou : public TriggerSkillV2
{
public:
    MobileDanshou() : TriggerSkillV2("mobiledanshou")
    {
        events << EventPhaseStart;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (!TriggerSkillV2::prepareSource(room, ctx)) return false;
        ctx.is_forced = ctx.invoker && targetedCount(room, ctx.invoker, ctx.owner) == 0;
        return true;
    }
    int targetedCount(Room *room, ServerPlayer *actor, ServerPlayer *owner) const
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toString().isEmpty() || turn.toString() == "0") return -1;
        QVariantMap filter{{"kind", "use_card_targets"}, {"from", actor->objectName()},
            {"turn_id", turn}, {"limit", 128}};
        int count = 0;
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return -1;
            foreach (const QVariant &item, page.value("items").toList()) {
                const QVariantMap use = item.toMap().value("data").toMap();
                const QVariantMap card = use.value("card").toMap();
                if (!card.contains("type")) return -1;
                if (card.value("type").toInt() != Card::TypeSkill
                    && use.value("targets").toStringList().contains(owner->objectName())) ++count;
            }
            if (!page.value("has_more").toBool()) return count;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *actor, QVariant &) const override
    {
        TriggerList result;
        if (!actor || actor->isDead() || actor->getPhase() != Player::Finish) return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(actor)) {
            if (!owner->hasSkill(objectName())) continue;
            const int count = targetedCount(room, actor, owner);
            if (count == 0 || (count > 0 && owner->canDiscard(owner, "he")))
                result.insert(owner, {objectName()});
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int count = targetedCount(room, ctx.invoker, ctx.owner);
        if (count < 0) return false;
        ctx.extra_data = count;
        ctx.targets = {count == 0 ? ctx.owner : ctx.invoker};
        if (count == 0) return true;
        ctx.owner->setTag("mobiledanshou_target", QVariant::fromValue(ctx.invoker));
        const Card *cards = room->askForDiscard(ctx.owner, objectName(), count, count, true, true,
            QString("@mobiledanshou-dis:%1::%2").arg(ctx.invoker->objectName()).arg(count), ".", objectName());
        ctx.owner->removeTag("mobiledanshou_target");
        return cards != nullptr;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.extra_data.toInt() == 0) {
            room->sendCompulsoryTriggerLog(ctx.owner, this, 2);
            target->drawCards(getEffectiveAmount(ctx), objectName());
        } else {
            room->broadcastSkillInvoke(objectName(), 1, ctx.owner);
            room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        }
        return false;
    }
};

class MobileDuodao : public TriggerSkillV2
{
public:
    MobileDuodao() : TriggerSkillV2("mobileduodao")
    {
        events << Damaged;
        frequency = Frequent;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && damage.from
            && damage.from->isAlive() && damage.from->getWeapon()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *attacker = ctx.original_data->value<DamageStruct>().from;
        if (!attacker || attacker->isDead() || !attacker->getWeapon() || !ctx.owner->askForSkillInvoke(this, attacker)) return false;
        ctx.targets = {attacker};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target->getWeapon()) {
            room->broadcastSkillInvoke(this);
            ctx.owner->obtainCard(target->getWeapon());
        }
        return false;
    }
};

class MobileAnjian : public TriggerSkillV2
{
public:
    MobileAnjian() : TriggerSkillV2("mobileanjian")
    {
        events << DamageCaused << TargetSpecified;
        frequency = Compulsory;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != TargetSpecified) return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || player->isDead() || use.from != player || !use.card || !use.card->isKindOf("Slash")) return true;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            for (ServerPlayer *target : use.to) {
                if (!target || !target->isAlive() || target->inMyAttackRange(player)) continue;
                SkillContext ctx;
                ctx.owner = player;
                ctx.invoker = player;
                ctx.instanceID = id;
                ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
                ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
                if (!ctx.sourceRef.isValid()) continue;
                ctx.skill_name = objectName() + "#" + QString::number(id);
                bool amountOk = false;
                ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &amountOk);
                if (!amountOk) ctx.amount = getBaseAmount();
                // One choice per out-of-range target. The "->" string is only parsed for equip skills.
                ctx.targets = {target};
                ctx.preferredTarget = target;
                ctx.preferredTargetSeat = target->getSeat();
                ctx.current_event = event;
                ctx.original_data = &data;
                contexts << ctx;
            }
        }
        return true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == DamageCaused) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && damage.to) {
                const QString receipt = "mobileanjian_damage_" + damage.to->objectName();
                const int amount = damage.card->getMark(receipt);
                if (amount > 0) {
                    // This card already accepted the bonus; later loss of its granting skill cannot revoke it.
                    room->setCardMark(damage.card, receipt, 0);
                    damage.damage += amount;
                    data = QVariant::fromValue(damage);
                }
            }
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.preferredTarget;
        if (!target || target->isDead() || target->inMyAttackRange(ctx.owner)) return false;
        ctx.owner->setTag("mobileanjian_usedata", *ctx.original_data);
        ctx.choice = room->askForChoice(ctx.owner, objectName(), "noresponse+damage", QVariant::fromValue(target));
        ctx.owner->removeTag("mobileanjian_usedata");
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.to.contains(target)) return false;
        LogMessage log;
        log.type = "#FumianFirstChoice";
        log.from = ctx.owner;
        log.arg = "mobileanjian:" + ctx.choice;
        room->sendLog(log);
        if (ctx.choice == "noresponse") {
            if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName();
            *ctx.original_data = QVariant::fromValue(use);
        } else {
            room->addCardMark(use.card, "mobileanjian_damage_" + target->objectName(), getEffectiveAmount(ctx));
        }
        return false;
    }
};

class MobileZhuikong : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    MobileZhuikong() : TriggerSkillV2("mobilezhuikong")
    {
        events << EventPhaseStart << EventPhaseChanging << Death << EventSkillInvoking;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const bool turnEnd = event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive;
        ServerPlayer *beneficiary = event == Death ? data.value<DeathStruct>().who : player;
        if ((turnEnd || event == Death) && beneficiary) {
            // The imposed turn limit is the beneficiary's, and it ends with that turn even if the grant is gone.
            room->setPlayerProperty(beneficiary, "mobilezhuikong_limits", QVariantList());
            room->setPlayerMark(beneficiary, "&mobilezhuikong-SelfClear", 0);
        }
        return false;
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        // Active entries commit their own quota; only this exact trigger's bypass path remains.
        if (accepted.executionID == 0 && accepted.bypass_cost && accepted.activationRef == ctx.activationRef
            && accepted.sourceRef == ctx.sourceRef && parseSkillName(accepted.skill_name) == objectName())
            addUsage(accepted);
    }
    LimitScope getLimitScope() const override { return Limit_Round; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventSkillInvoking) return {};
        TriggerList result;
        if (!player || player->isDead() || player->getPhase() != Player::RoundStart) return result;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && player->getHp() >= owner->getHp() && owner->canPindian(player))
                result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker || ctx.invoker->isDead() || !ctx.owner->canPindian(ctx.invoker)
            || !ctx.owner->askForSkillInvoke(objectName(), ctx.invoker)) return false;
        ctx.targets = {ctx.invoker};
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner->canPindian(target)) return false;
        room->broadcastSkillInvoke(this);
        PindianStruct *pindian = ctx.owner->PinDian(target, objectName());
        if (!pindian) return false;
        if (pindian->success) {
            // The restricted player is the turn player. -SelfClear dies with their turn, not with a nested turn or skill loss.
            QVariantList limits = target->property("mobilezhuikong_limits").toList();
            limits << QVariantMap{{"beneficiary", target->objectName()}, {"turn", room->historyScopes().value("turn_id")},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
                {"source_instance", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
                {"activation_instance", ctx.activationRef.key.instanceID}};
            room->setPlayerProperty(target, "mobilezhuikong_limits", limits);
            room->addPlayerMark(target, "&mobilezhuikong-SelfClear");
        } else {
            const int id = pindian->to_card->getEffectiveId();
            if (room->getCardPlace(id) != Player::DiscardPile || ctx.owner->isDead()) return false;
            room->obtainCard(ctx.owner, id);
            std::unique_ptr<Slash> slash(new Slash(Card::NoSuit, 0));
            slash->setSkillName("_mobilezhuikong");
            if (target->isAlive() && ctx.owner->isAlive() && target->canSlash(ctx.owner, slash.get(), false)) {
                CardUseStruct use(slash.get(), target, ctx.owner);
                use.sourceRef = ctx.sourceRef;
                use.activationRef = ctx.activationRef;
                room->useCardFromSkillEffect(use, ctx, true);
            }
        }
        return false;
    }
};

class MobileZhuikongProhibit : public ProhibitSkill
{
public:
    MobileZhuikongProhibit() : ProhibitSkill("#mobilezhuikong")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        if (!from || !to || !card || card->getTypeId() == Card::TypeSkill || to == from) return false;
        // The flag remains for OLZhuikong, which shares this prohibit rule. The mark is this skill's own deadline.
        return from->hasFlag("mobilezhuikong") || from->getMark("&mobilezhuikong-SelfClear") > 0;
    }
};

class MobileQiuyuan : public TriggerSkillV2
{
public:
    MobileQiuyuan() : TriggerSkillV2("mobileqiuyuan")
    {
        events << TargetConfirming;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && use.from
            && use.card && use.card->isKindOf("Slash") && use.to.contains(player)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QList<ServerPlayer *> targets = room->getOtherPlayers(ctx.owner);
        targets.removeOne(use.from);
        if (targets.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "@mobileqiuyuan-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        QStringList payable;
        foreach (int id, target->handCards()) {
            const Card *card = Sanguosha->getCard(id);
            if (card->isKindOf("BasicCard") && !card->isKindOf("Slash")) payable << QString::number(id);
        }
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        const int count = qMin(amount, payable.size());
        const Card *gift = count > 0 ? room->askForExchange(target, objectName(), count, count, false,
            "@mobileqiuyuan-give:" + ctx.owner->objectName(), true, payable.join(",")) : nullptr;
        if (gift && ctx.owner->isAlive()) {
            room->giveCard(target, ctx.owner, gift, objectName());
            return false;
        }
        // Re-read after the material decision so nested effects cannot be overwritten.
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.from || use.from->isDead() || !use.card || target->isDead() || use.to.contains(target)
            || !use.from->canSlash(target, use.card, false)) return false;
        LogMessage log;
        log.type = "#BecomeTarget";
        log.from = target;
        log.card_str = use.card->toString();
        room->sendLog(log);
        use.to.append(target);
        room->sortByActionOrder(use.to);
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class MobileJingce : public TriggerSkillV2
{
public:
    MobileJingce() : TriggerSkillV2("mobilejingce")
    {
        events << EventPhaseStart;
        frequency = Frequent;
        m_baseAmount = 2;
    }
    int usedMaterials(Room *room) const
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toString().isEmpty() || turn.toString() == "0") return -1;
        QVariantMap filter{{"turn_id", turn}, {"limit", 100}};
        int count = 0;
        while (true) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return -1;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap();
                const QVariantMap move = fact.value("data").toMap();
                if (!move.contains("reason") || !move.contains("to_place")) return -1;
                if (move.value("to_place").toInt() != Player::DiscardPile) continue;
                const int reason = move.value("reason").toInt();
                const bool use = (reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_USE;
                if (!use && reason != CardMoveReason::S_REASON_RESPONSE) continue;
                const qint64 eventId = fact.value("event_id").toLongLong();
                QVariantMap parent = room->historyParent(eventId, "respond_card", true);
                if (use) {
                    const QVariantMap used = room->historyParent(eventId, "use_card", true);
                    // A MethodUse response has respond_card provenance even inside another use.
                    if (used.value("id").toLongLong() > parent.value("id").toLongLong()) parent = used;
                }
                const QVariantMap card = parent.value("data").toMap().value("card").toMap();
                if (!card.contains("type")) return -1;
                // One move fact per physical material; a reused card counts on each use.
                if (card.value("type").toInt() != Card::TypeSkill) ++count;
            }
            if (!page.value("has_more").toBool()) break;
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
        return count;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish) return {};
        const int count = usedMaterials(room);
        return count >= 0 && count >= player->getHp() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner->askForSkillInvoke(this); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(this);
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

MobileDingpinCard::MobileDingpinCard()
{
    setSkillName("mobiledingpin");
}

bool MobileDingpinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.isEmpty() && to_select->getMark("mobiledingpin_target-PlayClear") == 0;
}

void MobileDingpinCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();

    JudgeStruct judge;
    judge.who = effect.to;
    judge.good = true;
    judge.pattern = ".|black";
    judge.reason = "mobiledingpin";

    room->judge(judge);

    if (judge.isGood()) {
        effect.to->drawCards(qMin(3, effect.to->getHp()), "mobiledingpin");
        room->addPlayerMark(effect.to, "mobiledingpin_target-PlayClear");
    } else {
        Card::Suit suit = judge.card->getSuit();
        if (suit == Card::Diamond)
            effect.from->turnOver();
        else if (suit == Card::Heart)
            room->removePlayerMark(effect.from, "dingpin_" + Sanguosha->getCard(subcards.first())->getType() + "-Clear");
    }
}

class MobileDingpinVS : public ViewAsSkillV2
{
public:
    MobileDingpinVS() : ViewAsSkillV2("mobiledingpin",1) { setPhaseName("Play"); }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileDingpinCard"; }
    static QVariantMap historyCounts(Room *room,const Player *player)
    {
        const QVariant turn=room->historyScopes().value("turn_id"); if(turn.toLongLong()<=0) return {{"known",false}};
        QVariantMap counts;
        // 使用 includes response-use (respond_card is_use). 打出 is not a use and does not debit a type.
        for(const QString &kind:{QString("use_card"),QString("respond_card"),QString("move")}) {
            const QString actor=kind=="respond_card"?QString("player"):QString("from");
            QVariantMap filter{{"kind",kind},{"turn_id",turn},{actor,player->objectName()},{"limit",128}};
            for(;;) {
                const QVariantMap page=kind=="move"?room->queryHistoryMoves(filter):room->queryHistoryFacts(filter);
                if(page.contains("error") || !page.value("complete").toBool()) return {{"known",false}};
                for(const QVariant &value:page.value("items").toList()) {
                    const QVariantMap data=value.toMap().value("data").toMap();
                    if(kind=="respond_card") {
                        if(!data.contains("is_use")) return {{"known",false}};
                        if(!data.value("is_use").toBool()) continue;
                    }
                    if(kind=="move" && (data.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON)!=CardMoveReason::S_REASON_DISCARD) continue;
                    const QVariantMap card=data.value(kind=="move"?"card_before":"card").toMap(); if(!card.contains("type")) return {{"known",false}};
                    const int type=card.value("type").toInt(); if(type==Card::TypeSkill) continue;
                    const QString key=QString::number(type); counts[key]=counts.value(key).toInt()+1;
                }
                if(!page.value("has_more").toBool()) break; filter["after"]=page.value("next_after"); filter["watermark"]=page.value("watermark");
            }
        }
        return {{"known",true},{"turn",turn},{"counts",counts}};
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card=ViewAsSkillV2::createCard(request);
        if(card && request.selectedCardIds.size()==1)
            card->setTag("MobileDingpinMaterialType",int(Sanguosha->getCard(request.selectedCardIds.first())->getTypeId()));
        return card;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason==CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->canDiscard(request.initiator,"he"); }
    bool canSelectCard(const ActiveSkillRequest &request,const Card *card) const override
    {
        if(!request.initiator || !request.activationRef.isValid() || !card || !request.selectedCardIds.isEmpty() || card->hasFlag("using") || request.initiator->isJilei(card)
            || (!request.initiator->handCards().contains(card->getEffectiveId()) && !request.initiator->getEquipsId().contains(card->getEffectiveId()))) return false;
        QVariantMap history=request.initiator->getSkillInstanceStateValue(objectName(),request.activationRef.key.instanceID,"history_types").toMap();
        if(const ServerPlayer *server=qobject_cast<const ServerPlayer *>(request.initiator)) history=historyCounts(server->getRoom(),server);
        if(!history.value("known").toBool()) return false;
        const QVariantMap refund=request.initiator->getSkillInstanceStateValue(objectName(),request.activationRef.key.instanceID,"type_refund").toMap();
        const QString type=QString::number(int(card->getTypeId()));
        const int credited=refund.value("turn")==history.value("turn")?refund.value("counts").toMap().value(type).toInt():0;
        return history.value("counts").toMap().value(type).toInt()<=credited;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { if(request.selectedCardIds.size()!=1) return false; ActiveSkillRequest prefix=request; prefix.selectedCardIds.clear(); return canSelectCard(prefix,Sanguosha->getCard(request.selectedCardIds.first())); }
    bool canSelectTarget(const ActiveSkillRequest &request,const QList<const Player *> &selected,const Player *target) const override
    {
        if(!request.initiator || !target || target->isDead() || !selected.isEmpty()) return false;
        const QVariantMap success=request.initiator->getSkillInstanceStateValue(objectName(),request.activationRef.key.instanceID,"successful_targets").toMap();
        if(const ServerPlayer *server=qobject_cast<const ServerPlayer *>(request.initiator))
            if(success.value("phase")!=server->getRoom()->historyScopes().value("phase_id")) return true;
        return !success.value("targets").toStringList().contains(target->objectName());
    }
    bool targetsFeasible(const ActiveSkillRequest &,const QList<const Player *> &targets) const override { return targets.size()==1; }
    bool pay(Room *room,SkillContext &ctx,const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) && ViewAsSkillV2::pay(room,ctx,request); }
    EffectFlow effectOnTarget(SkillContext &ctx,ServerPlayer *target) const override
    {
        if(getEffectiveAmount(ctx)<=0) return ContinueEffects; Room *room=target->getRoom();
        if(ctx.choice=="flip") { target->turnOver(); return ContinueEffects; }
        JudgeStruct judge; judge.who=target; judge.good=true; judge.pattern=".|black"; judge.reason=objectName(); room->judge(judge);
        if(!judge.card) return ContinueEffects;
        if(judge.isGood()) {
            const QVariant phase=room->historyScopes().value("phase_id"); QVariantMap success=ctx.owner->getSkillInstanceStateValue(objectName(),ctx.activationRef.key.instanceID,"successful_targets").toMap();
            QStringList names=success.value("phase")==phase?success.value("targets").toStringList():QStringList(); if(!names.contains(target->objectName())) names << target->objectName();
            ctx.owner->setSkillInstanceStateValue(objectName(),ctx.activationRef.key.instanceID,"successful_targets",QVariantMap{{"phase",phase},{"targets",names}});
            if(target->isAlive()) target->drawCards(qMax(0,qMin(3,target->getHp()))*getEffectiveAmount(ctx),objectName());
        } else if(judge.card->getSuit()==Card::Diamond) { SkillContext flip=ctx; flip.choice="flip"; skillEffect(flip,ctx.invoker); }
        else if(judge.card->getSuit()==Card::Heart && ctx.use_card && ctx.use_card->subcardsLength()==1) {
            const QVariant turn=room->historyScopes().value("turn_id"); QVariantMap refund=ctx.owner->getSkillInstanceStateValue(objectName(),ctx.activationRef.key.instanceID,"type_refund").toMap();
            QVariantMap counts=refund.value("turn")==turn?refund.value("counts").toMap():QVariantMap();
            const QString type=QString::number(ctx.use_card->getTag("MobileDingpinMaterialType").toInt()); counts[type]=counts.value(type).toInt()+1;
            // Hearts release one historical type debit for this instance, not every matching use/discard.
            ctx.owner->setSkillInstanceStateValue(objectName(),ctx.activationRef.key.instanceID,"type_refund",QVariantMap{{"turn",turn},{"counts",counts}});
        }
        return ContinueEffects;
    }
};

class MobileDingpin : public TriggerSkillV2
{
public:
    MobileDingpin() : TriggerSkillV2("mobiledingpin") { global=true; events << CardUsed << CardsMoveOneTime << EventAcquireSkill << EventPhaseStart; view_as_skill=new MobileDingpinVS; }
    bool recordEvent(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data) const override
    {
        if(!actor || (event==CardsMoveOneTime && data.value<CardsMoveOneTimeStruct>().from!=actor)) return false;
        if(event==EventPhaseStart && actor->getPhase()!=Player::Play) return false;
        const QVariantMap history=MobileDingpinVS::historyCounts(room,actor);
        for(const SkillInstance &instance:actor->getSkillInstances()) if(instance.skillName==objectName())
            actor->setSkillInstanceStateValue(objectName(),instance.instanceID,"history_types",history);
        return false;
    }
    TriggerList triggerable(TriggerEvent,Room *,ServerPlayer *,QVariant &) const override { return {}; }
};

class MobileZhongyong : public TriggerSkillV2
{
public:
    MobileZhongyong() : TriggerSkillV2("mobilezhongyong") { global=true; events << CardOffset << CardFinished << CardsMoveOneTime; }
    static QVariantMap origin(Room *room,const SkillContext &ctx)
    {
        const int serial=room->getTag("MobileZhongyongSerial").toInt()+1; room->setTag("MobileZhongyongSerial",serial);
        return {{"serial",serial},{"owner",ctx.activationRef.ownerObjectName},{"skill",ctx.activationRef.key.skillName},{"instance",ctx.activationRef.key.instanceID},
            {"source_owner",ctx.sourceRef.ownerObjectName},{"source_skill",ctx.sourceRef.key.skillName},{"source_instance",ctx.sourceRef.key.instanceID},{"amount",ctx.amount},{"turn",room->historyScopes().value("turn_id")}};
    }
    static QList<int> materials(const Card *card) { if(!card) return {}; return card->isVirtualCard()?card->getSubcards():QList<int>{card->getEffectiveId()}; }
    bool recordEvent(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data) const override
    {
        if(event==CardOffset) {
            const CardEffectStruct effect=data.value<CardEffectStruct>(); if(effect.from!=actor || !effect.card || !effect.card->isKindOf("Slash") || !effect.offset_card) return false;
            const qint64 useId=room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id").toLongLong(); if(useId<=0) return false;
            QVariantMap offsets=effect.card->getTag("MobileZhongyongOffsets").toMap(); QVariantList ids=offsets.value("use_id").toLongLong()==useId?offsets.value("ids").toList():QVariantList();
            for(int id:materials(effect.offset_card)) if(!ids.contains(id)) ids << id;
            effect.card->setTag("MobileZhongyongOffsets",QVariantMap{{"use_id",useId},{"ids",ids},{"offset",true}}); return false;
        }
        if(event!=CardsMoveOneTime) return false;
        const CardsMoveOneTimeStruct move=data.value<CardsMoveOneTimeStruct>(); if(move.to!=actor || move.to_place!=Player::PlaceHand || move.reason.m_skillName!=objectName()) return false;
        QVariantList kept,applied;
        for(const QVariant &value:room->getTag("MobileZhongyongTransfers").toList()) {
            const QVariantMap r=value.toMap(); if(r.value("target").toString()==actor->objectName() && move.card_ids.contains(r.value("id").toInt()) && r.value("cause_event").toLongLong()>0 && room->historyParent(room->currentHistoryEventId(),"move_cards",true).value("parent_id").toLongLong()==r.value("cause_event").toLongLong()) applied << r; else kept << r;
        }
        room->setTag("MobileZhongyongTransfers",kept);
        QVariantList restrictions=room->getTag("MobileZhongyongRestrictions").toList(); restrictions.append(applied); room->setTag("MobileZhongyongRestrictions",restrictions);
        for(const QVariant &value:applied) room->setPlayerCardLimitation(actor,"use",QString::number(value.toMap().value("id").toInt()),false,objectName()+":"+value.toMap().value("serial").toString());
        return false;
    }
    TriggerList triggerable(TriggerEvent event,Room *,ServerPlayer *actor,QVariant &data) const override
    { const Card *card=event==CardFinished?data.value<CardUseStruct>().card:nullptr; return actor && actor->isAlive() && actor->hasSkill(objectName()) && actor->getPhase()==Player::Play && card && card->isKindOf("Slash")?TriggerList{{actor,{objectName()}}}:TriggerList(); }
    bool cost(TriggerEvent,Room *room,ServerPlayer *,SkillContext &ctx) const override
    {
        const CardUseStruct use=ctx.original_data->value<CardUseStruct>(); const qint64 useId=room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id").toLongLong();
        const QVariantMap offsets=use.card->getTag("MobileZhongyongOffsets").toMap(); QList<int> ids; const bool offset=useId>0 && offsets.value("use_id").toLongLong()==useId && offsets.value("offset").toBool();
        if(offset) { for(const QVariant &value:offsets.value("ids").toList()) if(room->getCardPlace(value.toInt())==Player::DiscardPile) ids << value.toInt(); }
        else { if(!room->CardInPlace(use.card,Player::DiscardPile) || !ctx.owner->askForSkillInvoke(this,*ctx.original_data)) return false; ids=materials(use.card); }
        if(ids.isEmpty()) return false;
        ServerPlayer *target=ctx.owner;
        if(offset) {
            QList<ServerPlayer *> targets; for(ServerPlayer *p:room->getAlivePlayers()) if(!use.to.contains(p) || p==ctx.owner) targets << p;
            room->fillAG(ids,ctx.owner); auto clear=qScopeGuard([&]{room->clearAG(ctx.owner);}); target=room->askForPlayerChosen(ctx.owner,targets,objectName(),"@mobilezhongyong-invoke",true,true); if(!target) return false;
        }
        QVariantList material; for(int id:ids) material << id; ctx.extra_data=QVariantMap{{"ids",material},{"jink",offset}}; ctx.targets={target}; return true;
    }
    bool effect(TriggerEvent event,Room *room,ServerPlayer *actor,SkillContext &ctx) const override
    {
        ctx.manual_effect=true; if(ctx.targets.isEmpty()) return false;
        SkillContext transfer=ctx; transfer.choice="give"; skillEffect(event,room,actor,transfer,ctx.targets.first());
        const QString recipient=transfer.extra_data.toMap().value("recipient").toString(); if(recipient.isEmpty() || !ctx.extra_data.toMap().value("jink").toBool() || !ctx.invoker || ctx.invoker->isDead()) return false;
        if(recipient!=ctx.invoker->objectName()) { SkillContext bonus=ctx; bonus.choice="bonus"; skillEffect(event,room,actor,bonus,ctx.invoker); return false; }
        const CardUseStruct use=ctx.original_data->value<CardUseStruct>(); if(!room->CardInPlace(use.card,Player::DiscardPile)) return false;
        QList<ServerPlayer *> targets; for(ServerPlayer *p:room->getAlivePlayers()) if(p!=ctx.invoker && !use.to.contains(p)) targets << p;
        if(targets.isEmpty()) return false; const QList<int> ids=materials(use.card); if(ids.isEmpty()) return false;
        room->fillAG(ids,ctx.invoker); auto clear=qScopeGuard([&]{room->clearAG(ctx.invoker);});
        ServerPlayer *target=room->askForPlayerChosen(ctx.invoker,targets,"mobilezhongyong_slash","@mobilezhongyong-give",true,false); if(!target) return false;
        SkillContext gift=ctx; gift.choice="give"; QVariantList material; for(int id:ids) material << id; gift.extra_data=QVariantMap{{"ids",material}}; skillEffect(event,room,actor,gift,target); return false;
    }
    bool effectTarget(TriggerEvent,Room *room,ServerPlayer *,SkillContext &ctx,ServerPlayer *target) const override
    {
        if(getEffectiveAmount(ctx)<=0) return false;
        if(ctx.choice=="bonus") {
            QVariantMap r=origin(room,ctx); r["amount"]=getEffectiveAmount(ctx); r["actor"]=target->objectName();
            QVariantList pending=target->getTag("MobileZhongyongPending").toList(); pending << r; target->setTag("MobileZhongyongPending",pending);
            int amount=0; for(const QVariant &value:pending) amount+=value.toMap().value("amount").toInt(); room->setPlayerMark(target,"&mobilezhongyong",amount); room->addSlashCishu(target,getEffectiveAmount(ctx)); return false;
        }
        QList<int> ids; for(const QVariant &value:ctx.extra_data.toMap().value("ids").toList()) { const int id=value.toInt(); if(room->getCardPlace(id)!=Player::DiscardPile) return false; if(!ids.contains(id)) ids << id; }
        if(ids.isEmpty()) return false;
        const QVariantMap base=origin(room,ctx); const QVariant serial=base.value("serial"); QVariantList pending=room->getTag("MobileZhongyongTransfers").toList();
        for(int id:ids) { QVariantMap r=base; r["target"]=target->objectName(); r["id"]=id; r["cause_event"]=room->currentHistoryEventId(); pending << r; }
        room->setTag("MobileZhongyongTransfers",pending);
        auto clear=qScopeGuard([&]{ QVariantList kept; for(const QVariant &value:room->getTag("MobileZhongyongTransfers").toList()) if(value.toMap().value("serial")!=serial) kept << value; room->setTag("MobileZhongyongTransfers",kept); });
        DummyCard cards(ids); room->obtainCard(target,&cards,CardMoveReason(target==ctx.invoker?CardMoveReason::S_REASON_RECYCLE:CardMoveReason::S_REASON_GIVE,ctx.invoker->objectName(),target->objectName(),objectName(),""),true);
        // The actual move recorder proves a transfer, even if nested effects immediately move its cards again.
        bool moved=false; for(const QVariant &value:room->getTag("MobileZhongyongRestrictions").toList()) if(value.toMap().value("serial")==serial) moved=true;
        if(moved) { QVariantMap result=ctx.extra_data.toMap(); result["recipient"]=target->objectName(); ctx.extra_data=result; } return false;
    }
};

class MobileZhongyongEffect : public TriggerSkillV2
{
public:
    MobileZhongyongEffect() : TriggerSkillV2("#mobilezhongyong-effect") { global=true; frequency=Compulsory; events << PreCardUsed << ConfirmDamage; }
    bool recordEvent(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data) const override
    {
        if(event!=PreCardUsed || !actor) return false; const Card *card=data.value<CardUseStruct>().card; if(!card || !card->isKindOf("Slash")) return false;
        QVariantList receipts; const QVariant useId=room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id");
        for(const QVariant &value:actor->getTag("MobileZhongyongPending").toList()) { QVariantMap r=value.toMap(); if(r.value("turn")!=room->historyScopes().value("turn_id")) continue; r["use_id"]=useId; receipts << r; }
        actor->removeTag("MobileZhongyongPending"); card->setTag("MobileZhongyongDamage",receipts); room->setPlayerMark(actor,"&mobilezhongyong",0); return false;
    }
    bool collectTriggerContexts(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data,QList<SkillContext> &contexts) const override
    {
        if(event!=ConfirmDamage || !actor) return true; const DamageStruct damage=data.value<DamageStruct>(); if(!damage.card || !damage.to) return true;
        const qint64 useId=room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id").toLongLong();
        for(const QVariant &value:damage.card->getTag("MobileZhongyongDamage").toList()) {
            const QVariantMap r=value.toMap(); if(useId<=0 || r.value("use_id").toLongLong()!=useId) continue;
            SkillContext ctx; ctx.skill_name=objectName(); ctx.owner=room->findPlayerByObjectName(r.value("owner").toString(),true); ctx.invoker=actor; ctx.initiator=actor; ctx.targets={damage.to}; ctx.instanceID=r.value("serial").toInt();
            ctx.sourceRef=SkillInstanceRef(r.value("source_owner").toString(),SkillInstanceKey(r.value("source_skill").toString(),r.value("source_instance").toInt()));
            ctx.amount=r.value("amount").toInt(); ctx.extra_data=r; ctx.current_event=event; ctx.original_data=&data; ctx.is_forced=true; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *,const SkillContext &ctx) const override
    { const Card *card=ctx.original_data->value<DamageStruct>().card; return card && card->getTag("MobileZhongyongDamage").toList().contains(ctx.extra_data); }
    bool effectTarget(TriggerEvent,Room *,ServerPlayer *,SkillContext &ctx,ServerPlayer *target) const override
    { DamageStruct d=ctx.original_data->value<DamageStruct>(); if(d.to==target) { d.damage+=getEffectiveAmount(ctx); *ctx.original_data=QVariant::fromValue(d); } return false; }
};

class MobileZhongyongRemove : public TriggerSkillV2
{
public:
    MobileZhongyongRemove() : TriggerSkillV2("#mobilezhongyong-remove") { global=true; events << EventPhaseChanging << Death << TurnBroken; }
    bool recordEvent(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data) const override
    {
        const bool death=event==Death && actor && data.value<DeathStruct>().who==actor;
        if(!death && event!=TurnBroken && (event!=EventPhaseChanging || data.value<PhaseChangeStruct>().to!=Player::NotActive)) return false;
        for(ServerPlayer *player:room->getAllPlayers(true)) {
            QVariantList pending; int amount=0;
            for(const QVariant &value:player->getTag("MobileZhongyongPending").toList()) if(!(death?player==actor:value.toMap().value("turn")==room->historyScopes().value("turn_id"))) { pending << value; amount+=value.toMap().value("amount").toInt(); }
            player->setTag("MobileZhongyongPending",pending); room->setPlayerMark(player,"&mobilezhongyong",amount);
        }
        if(death) return false;
        QVariantList kept,expired; for(const QVariant &value:room->getTag("MobileZhongyongRestrictions").toList()) {
            if(value.toMap().value("turn")==room->historyScopes().value("turn_id")) expired << value; else kept << value;
        }
        room->setTag("MobileZhongyongRestrictions",kept);
        for(const QVariant &value:expired) { const QVariantMap r=value.toMap(); if(ServerPlayer *target=room->findPlayerByObjectName(r.value("target").toString(),true)) room->removePlayerCardLimitation(target,"use",QString::number(r.value("id").toInt())+"$0","mobilezhongyong:"+r.value("serial").toString()); }
        return false;
    }
    TriggerList triggerable(TriggerEvent,Room *,ServerPlayer *,QVariant &) const override { return {}; }
};

MobileShenxingCard::MobileShenxingCard()
{
    setSkillName("mobileshenxing");
    target_fixed = true;
}

void MobileShenxingCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    if (source->isAlive()) {
        int n = 1;
        if (!Sanguosha->getCard(subcards.first())->sameColorWith(Sanguosha->getCard(subcards.last())))
            n++;
        room->drawCards(source, n, "mobileshenxing");
    }
}

class MobileShenxing : public ViewAsSkillV2
{
public:
    MobileShenxing() : ViewAsSkillV2("mobileshenxing", 2)
    {
        setPhaseName("Play");
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &ctx) const override { return qMax(0, ctx.initiator ? ctx.initiator->getHp() : 0); }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileShenxingCard"; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && request.selectedCardIds.size() < 2 && !request.selectedCardIds.contains(card->getEffectiveId())
            && !request.initiator->isJilei(card) && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId()));
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getCardCount(true) >= 2 && request.initiator->canDiscard(request.initiator, "he");
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card=ViewAsSkillV2::createCard(request);
        if(card && request.selectedCardIds.size()==2) card->setTag("MobileShenxingDraw",Sanguosha->getCard(request.selectedCardIds.first())->sameColorWith(Sanguosha->getCard(request.selectedCardIds.last()))?1:2);
        return card;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ctx.targets = {ctx.invoker};
        return skillEffect(ctx, ctx.invoker);
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards((ctx.use_card ? ctx.use_card->getTag("MobileShenxingDraw").toInt() : 0) * getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
};

MobileBingyiCard::MobileBingyiCard()
{
}

bool MobileBingyiCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    QList<const Card *>cards = Self->getHandcards();
    if (cards.isEmpty()) return false;

    bool same_color = true, same_type = true;
    Card::Color color = cards.first()->getColor();
    int type_id = cards.first()->getTypeId();

    foreach (const Card *c, cards) {
        if (c->getColor() != color) {
            same_color = false;
            break;
        }
    }
    if (!same_color) {
        foreach (const Card *c, cards) {
            if (c->getTypeId() != type_id) {
                same_type = false;
                break;
            }
        }
    }
    if (!same_color && !same_type)
        return targets.isEmpty();
    return targets.length() <= Self->getHandcardNum();
}

bool MobileBingyiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
    QList<const Card *>cards = Self->getHandcards();
    if (cards.isEmpty()) return false;

    bool same_color = true, same_type = true;
    Card::Color color = cards.first()->getColor();
    int type_id = cards.first()->getTypeId();

    foreach (const Card *c, cards) {
        if (c->getColor() != color) {
            same_color = false;
            break;
        }
    }
    if (!same_color) {
        foreach (const Card *c, cards) {
            if (c->getTypeId() != type_id) {
                same_type = false;
                break;
            }
        }
    }
    if (!same_color && !same_type)
        return false;
    return targets.length() < Self->getHandcardNum();
}

void MobileBingyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->showAllCards(source, static_cast<ServerPlayer *>(nullptr));
    foreach(ServerPlayer *p, targets)
        room->drawCards(p, 1, "mobilebingyi");
}

class MobileBingyi : public TriggerSkillV2
{
public:
    MobileBingyi() : TriggerSkillV2("mobilebingyi")
    {
        events << EventPhaseStart;
    }
    bool matchingHand(ServerPlayer *owner) const
    {
        const QList<const Card *> cards = owner->getHandcards();
        if (cards.isEmpty()) return false;
        bool sameColor = true, sameType = true;
        for (const Card *card : cards) {
            sameColor = sameColor && card->getColor() == cards.first()->getColor();
            sameType = sameType && card->getTypeId() == cards.first()->getTypeId();
        }
        return sameColor || sameType;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
            && matchingHand(player) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!matchingHand(ctx.owner)) return false;
        ctx.targets = room->askForPlayersChosen(ctx.owner, room->getAlivePlayers(), objectName(), 0,
            ctx.owner->getHandcardNum(), "@mobilebingyi", true);
        return !ctx.targets.isEmpty();
    }
    bool effect(TriggerEvent event,Room *room,ServerPlayer *actor,SkillContext &ctx) const override
    {
        ctx.manual_effect=true; SkillContext reveal=ctx; reveal.choice="reveal"; reveal.extra_data=QVariant(); skillEffect(event,room,actor,reveal,ctx.owner);
        const int count=reveal.extra_data.toInt(); if(count<=0) return false;
        const QList<ServerPlayer *> selected=ctx.targets.mid(0,count);
        for(ServerPlayer *target:selected) { SkillContext draw=ctx; draw.choice="draw"; skillEffect(event,room,actor,draw,target); } return false;
    }
    bool effectTarget(TriggerEvent,Room *room,ServerPlayer *,SkillContext &ctx,ServerPlayer *target) const override
    {
        if(getEffectiveAmount(ctx)<=0) return false;
        if(ctx.choice=="reveal") {
            if(!matchingHand(target)) return false;
            // Freeze eligibility/count before revealing the actual recipient's hand invokes callbacks.
            ctx.extra_data=target->getHandcardNum(); room->showAllCards(target,static_cast<ServerPlayer *>(nullptr));
        } else target->drawCards(getEffectiveAmount(ctx),objectName());
        return false;
    }
};

class MobileQieting : public TriggerSkillV2
{
public:
    MobileQieting() : TriggerSkillV2("mobileqieting")
    {
        events << EventPhaseChanging;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        TriggerList result;
        if (!actor || data.value<PhaseChangeStruct>().to != Player::NotActive) return result;
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toString().isEmpty() || turn.toString() == "0") return result;
        QVariantMap filter{{"turn_id", turn}, {"from", actor->objectName()}, {"limit", 128}};
        for (;;) {
            const QVariantMap page = room->queryActualDamage(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return result;
            foreach (const QVariant &item, page.value("items").toList()) {
                const QVariantMap damage = item.toMap().value("data").toMap();
                if (!damage.contains("to") || damage.value("to").toString() != actor->objectName()) return result;
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
        foreach (ServerPlayer *owner, room->getOtherPlayers(actor)) {
            if (owner->hasSkill(objectName())) result.insert(owner, {objectName()});
        }
        return result;
    }
    QList<int> movable(ServerPlayer *actor, ServerPlayer *owner) const
    {
        QList<int> result;
        if (!actor || actor->isDead() || !owner || owner->isDead()) return result;
        for (int slot = 0; slot < S_EQUIP_AREA_LENGTH; ++slot) {
            if (actor->getEquip(slot) && !actor->getEquip(slot)->hasFlag("using") && !owner->getEquip(slot) && owner->hasEquipArea(slot))
                result << actor->getEquip(slot)->getEffectiveId();
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this, ctx.invoker)) return false;
        QStringList choices;
        if (ctx.invoker->isAlive() && !ctx.invoker->isKongcheng())
            choices << "view=" + ctx.invoker->objectName();
        if (!movable(ctx.invoker, ctx.owner).isEmpty()) choices << "move=" + ctx.invoker->objectName();
        choices << "draw";
        ctx.choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"), QVariant::fromValue(ctx.invoker));
        ctx.targets = {ctx.choice == "draw" ? ctx.owner : ctx.invoker};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if(getEffectiveAmount(ctx)<=0) return false;
        if(ctx.choice=="receive_hand" || ctx.choice=="receive_equip") {
            const QVariantMap r=ctx.extra_data.toMap(); ServerPlayer *from=room->findPlayerByObjectName(r.value("from").toString()); const int id=r.value("id",-1).toInt();
            if(!from || id<0 || room->getCardOwner(id)!=from || Sanguosha->getCard(id)->hasFlag("using")) return false;
            if(ctx.choice=="receive_equip") { if(movable(from,target).contains(id)) room->moveCardTo(Sanguosha->getCard(id),from,target,Player::PlaceEquip,CardMoveReason(CardMoveReason::S_REASON_TRANSFER,ctx.owner->objectName(),objectName(),"")); }
            else if(from->handCards().contains(id)) room->obtainCard(target,id,false); return false;
        }
        room->broadcastSkillInvoke(this);
        if (ctx.choice == "draw") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
        } else if (ctx.choice.startsWith("move")) {
            const QList<int> legal = movable(target, ctx.owner);
            if (legal.isEmpty()) return false;
            QList<int> disabled = target->getEquipsId();
            foreach (int id, legal) disabled.removeOne(id);
            const int id = room->askForCardChosen(ctx.owner, target, "e", objectName(), false, Card::MethodNone, disabled);
            if(movable(target,ctx.owner).contains(id)) { SkillContext receive=ctx; receive.choice="receive_equip"; receive.extra_data=QVariantMap{{"from",target->objectName()},{"id",id}}; skillEffect(event,room,actor,receive,ctx.owner); }
        } else {
            const int count = qMin(target->getHandcardNum(), 2 * getEffectiveAmount(ctx));
            QList<int> cards;
            for (int i = 0; i < count && target->isAlive() && ctx.owner->isAlive(); ++i) {
                const int id = room->askForCardChosen(ctx.owner, target, "h", "mobileqieting_view", false, Card::MethodNone, cards);
                if(id<0 || cards.contains(id) || room->getCardOwner(id)!=target || !target->handCards().contains(id) || Sanguosha->getCard(id)->hasFlag("using")) break;
                cards << id;
            }
            if (cards.isEmpty() || ctx.owner->isDead()) return false;
            room->fillAG(cards, ctx.owner);
            auto clear=qScopeGuard([&]{room->clearAG(ctx.owner);});
            const int id = room->askForAG(ctx.owner, cards, false, objectName());
            // The revealed selection does not authorize taking a card moved by a nested effect.
            if(cards.contains(id) && target->handCards().contains(id)) { SkillContext receive=ctx; receive.choice="receive_hand"; receive.extra_data=QVariantMap{{"from",target->objectName()},{"id",id}}; skillEffect(event,room,actor,receive,ctx.owner); }
        }
        return false;
    }
};

MobileJianyingCard::MobileJianyingCard()
{
    mute = true;
    handling_method = Card::MethodUse;
    will_throw = false;
}

bool MobileJianyingCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Card *card = Sanguosha->cloneCard(user_string);
    card->setSkillName("mobilejianying");
    card->addSubcard(subcards.first());
    card->setCanRecast(false);
    QString suitstring = Self->property("MobileJianyingLastSuitString").toString();
    if (!suitstring.isEmpty()){
        //card->setFlags("CardInformationHelper|" + suitstring + "|" + QString::number(card->getNumber()));
		if(suitstring=="spade")
			card->setSuit(Card::Spade);
		else if(suitstring=="club")
			card->setSuit(Card::Club);
		else if(suitstring=="heart")
			card->setSuit(Card::Heart);
		else
			card->setSuit(Card::Diamond);
	}
    card->deleteLater();
    return card->targetFilter(targets, to_select, Self);
}

bool MobileJianyingCard::targetFixed() const
{
    Card *card = Sanguosha->cloneCard(user_string);
    card->setSkillName("mobilejianying");
    card->addSubcard(subcards.first());
    card->setCanRecast(false);
    card->deleteLater();
    return card->targetFixed();
}

bool MobileJianyingCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    Card *card = Sanguosha->cloneCard(user_string);
    card->setSkillName("mobilejianying");
    card->setCanRecast(false);
    QString suitstring = Self->property("MobileJianyingLastSuitString").toString();
    if (!suitstring.isEmpty()){
        //card->setFlags("CardInformationHelper|" + suitstring + "|" + QString::number(card->getNumber()));
		if(suitstring=="spade")
			card->setSuit(Card::Spade);
		else if(suitstring=="club")
			card->setSuit(Card::Club);
		else if(suitstring=="heart")
			card->setSuit(Card::Heart);
		else
			card->setSuit(Card::Diamond);
	}
    card->deleteLater();
    return card->targetsFeasible(targets, Self);
}

const Card *MobileJianyingCard::validate(CardUseStruct &card_use) const
{
    Room *room = card_use.from->getRoom();

    Card *use_card = Sanguosha->cloneCard(user_string);
    use_card->setSkillName("mobilejianying");
    use_card->addSubcard(subcards.first());
    use_card->deleteLater();

    QString suitstring = card_use.from->property("MobileJianyingLastSuitString").toString();
    if (!suitstring.isEmpty()){
        //room->setCardFlag(use_card, "CardInformationHelper|" + suitstring + "|" + QString::number(getNumber()));
		if(suitstring=="spade")
			use_card->setSuit(Card::Spade);
		else if(suitstring=="club")
			use_card->setSuit(Card::Club);
		else if(suitstring=="heart")
			use_card->setSuit(Card::Heart);
		else
			use_card->setSuit(Card::Diamond);
	}
	room->addPlayerMark(card_use.from,"mobilejianyingUse-PlayClear");
	card_use.m_addHistory = false;
    return use_card;
}

class MobileJianyingVS : public ViewAsSkillV2
{
public:
    MobileJianyingVS() : ViewAsSkillV2("mobilejianying", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, false); }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && request.selectedCardIds.isEmpty() && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId()));
    }
protected:
    Card *buildCard(const ActiveSkillRequest &request, const QString &name) const override
    {
        std::unique_ptr<Card> card(ViewAsSkillV2::buildCard(request, name));
        if (!card || !card->isKindOf("BasicCard") || !request.initiator) return nullptr;
        int suit = request.initiator->property("mobilejianying_last_suit").toInt();
        if (const ServerPlayer *server = qobject_cast<const ServerPlayer *>(request.initiator)) {
            const QVariantMap history = server->getRoom()->queryCardHistory(server, "turn");
            if (history.contains("error") || !history.value("complete").toBool()) return nullptr;
            const QVariantList cards = history.value("items").toList();
            suit = cards.isEmpty() ? int(Card::NoSuit) : cards.last().toMap().value("suit", int(Card::NoSuit)).toInt();
        } else if (!request.initiator->property("mobilejianying_last_suit").isValid()) {
            suit = Card::NoSuit;
        }
        // Client projection only previews the suit; server reconstruction always reads the journal.
        if (suit >= Card::Spade && suit <= Card::Diamond) card->setSuit(static_cast<Card::Suit>(suit));
        return card.release();
    }
};

class MobileJianying : public TriggerSkillV2
{
public:
    MobileJianying() : TriggerSkillV2("mobilejianying")
    {
        events << CardUsed << CardResponded << PreCardUsed << EventPhaseChanging;
        global = true;
        view_as_skill = new MobileJianyingVS;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (event == PreCardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card && use.card->getSkillName() == objectName()) {
                use.m_addHistory = false;
                data = QVariant::fromValue(use);
            }
        } else if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to == Player::NotActive)
                for (ServerPlayer *actor : room->getAllPlayers(true))
                    room->setPlayerProperty(actor, "mobilejianying_last_suit", int(Card::NoSuit));
        } else {
            const Card *card = event == CardUsed ? data.value<CardUseStruct>().card
                : data.value<CardResponseStruct>().m_isUse ? data.value<CardResponseStruct>().m_card : nullptr;
            if (card && !card->isKindOf("SkillCard"))
                room->setPlayerProperty(player, "mobilejianying_last_suit", int(card->getSuit()));
        }
        return true;
    }
    bool consecutiveMatch(TriggerEvent event, Room *room, ServerPlayer *owner) const
    {
        const QString kind = event == CardUsed ? "use_card" : "respond_card";
        const qint64 eventID = room->historyParent(room->currentHistoryEventId(), kind, true).value("id").toLongLong();
        if (eventID <= 0) return false;
        const QVariantMap current = room->queryHistoryFacts({{"kind", kind}, {"event_id", eventID}, {"limit", 1}});
        const QVariantList currentItems = current.value("items").toList();
        if (current.contains("error") || !current.value("complete").toBool() || currentItems.size() != 1) return false;
        const QVariantMap currentFact = currentItems.first().toMap();
        const QVariant phase = currentFact.value("phase_id");
        const qint64 frontier = currentFact.value("sequence").toLongLong();
        if (phase.toLongLong() <= 0 || frontier <= 0) return false;
        QMap<qint64, QVariantMap> cards;
        for (const QString &factKind : {QString("use_card"), QString("respond_card")}) {
            QVariantMap filter{{"kind", factKind}, {"phase_id", phase}, {"watermark", frontier}, {"limit", 128},
                {factKind == "use_card" ? "from" : "player", owner->objectName()}};
            for (;;) {
                const QVariantMap page = room->queryHistoryFacts(filter);
                if (page.contains("error") || !page.value("complete").toBool()) return false;
                for (const QVariant &item : page.value("items").toList()) {
                    const QVariantMap fact = item.toMap();
                    const QVariantMap data = fact.value("data").toMap();
                    if (factKind == "respond_card" && !data.value("is_use").toBool()) continue;
                    const QVariantMap card = data.value("card").toMap();
                    if (!card.contains("type") || !card.contains("suit") || !card.contains("number")) return false;
                    if (card.value("type").toInt() != Card::TypeSkill)
                        cards.insert(fact.value("sequence").toLongLong(), card);
                }
                if (!page.value("has_more").toBool()) break;
                filter.insert("after", page.value("next_after"));
            }
        }
        if (cards.size() < 2 || cards.lastKey() != frontier) return false;
        auto it = cards.constEnd();
        const QVariantMap latest = (--it).value();
        const QVariantMap previous = (--it).value();
        const int suit = latest.value("suit").toInt(), number = latest.value("number").toInt();
        return (suit >= Card::Spade && suit <= Card::Diamond && suit == previous.value("suit").toInt())
            || (number > 0 && number == previous.value("number").toInt());
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if ((event != CardUsed && event != CardResponded) || !player || player->isDead()
            || player->getPhase() != Player::Play || !player->hasSkill(objectName())) return {};
        if (event == CardResponded && !data.value<CardResponseStruct>().m_isUse) return {};
        return consecutiveMatch(event, room, player) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!consecutiveMatch(event, room, ctx.owner) || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class MobileJianyingTargetMod : public TargetModSkillV2
{
public:
    MobileJianyingTargetMod() : TargetModSkillV2("#mobilejianying-target", "BasicCard")
    {
        frequency = NotFrequent;
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const Card *card = ctx.card;
        if (ctx.modType != Residue || !card) return CorrectSkillResult::noEffect();
        if (card->getSkillName() == "mobilejianying" || card->hasFlag("mobilejianying"))
            return CorrectSkillResult::useAmount(999);
        return CorrectSkillResult::noEffect();
    }
};

MobileYanzhuCard::MobileYanzhuCard()
{
    setSkillName("mobileyanzhu");
}

void MobileYanzhuCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *from = effect.from, *to = effect.to;
    if (to->isAllNude()) return;
    Room *room = from->getRoom();
    QStringList choices;
    if (!to->getEquips().isEmpty())
        choices << "equip=" + from->objectName();
    choices << "obtain=" + from->objectName();
    QString choice = room->askForChoice(to, "mobileyanzhu", choices.join("+"), QVariant::fromValue(from));
    if (choice.startsWith("equip")) {
        DummyCard dummy;
        dummy.addSubcards(to->getEquips());
        room->obtainCard(from, &dummy);
        room->handleAcquireDetachSkills(from, "-mobileyanzhu");
        room->setPlayerProperty(from, "MobileXingxueLevelUp", true);
        room->changeTranslation(from, "mobilexingxue", 1);
    } else {
        int id = room->askForCardChosen(from, to, "hej", "mobileyanzhu");
        room->obtainCard(from, id, false);
    }
}

class MobileYanzhu : public ViewAsSkillV2
{
public:
    MobileYanzhu() : ViewAsSkillV2("mobileyanzhu")
    {
        setPhaseName("Play");
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileYanzhuCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return target && target != request.initiator && !target->isAllNude() && selected.isEmpty();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if(getEffectiveAmount(ctx)<=0) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        if(ctx.choice=="upgrade") {
            const SkillInstanceRef ref=ctx.activationRef; ServerPlayer *holder=room->findPlayerByObjectName(ref.ownerObjectName,true);
            if(holder && holder->hasSkillInstance(ref.key.skillName,ref.key.instanceID)) room->detachSkillFromPlayer(holder,SkillInstanceUtils::formatName(ref.key.skillName,ref.key.instanceID));
            room->setPlayerProperty(target,"MobileXingxueLevelUp",true); room->changeTranslation(target,"mobilexingxue",1); return ContinueEffects;
        }
        if (ctx.choice == "receive") {
            const QVariantMap values = ctx.extra_data.toMap();
            ServerPlayer *donor = room->findPlayerByObjectName(values.value("donor").toString(), true);
            if (!donor) return ContinueEffects;
            QList<int> selected = ListV2I(values.value("ids").toList());
            if (!values.value("equip").toBool() && !donor->isAllNude()) {
                const int count = qMin(qMax(0, getEffectiveAmount(ctx)), donor->getCards("hej").size());
                if (count > 0) selected = room->askForCardsChosen(ctx.invoker, donor, "hej", objectName(),
                    count, count, false, Card::MethodNone, {}, false);
            }
            QList<int> ids;
            for (int id : selected)
                if (room->getCardOwner(id) == donor && (room->getCardPlace(id) == Player::PlaceHand
                    || room->getCardPlace(id) == Player::PlaceEquip || room->getCardPlace(id) == Player::PlaceDelayedTrick)) ids << id;
            if (!ids.isEmpty()) {
                DummyCard cards(ids);
                room->obtainCard(target, &cards, values.value("equip").toBool());
                bool received=true; for(int id:ids) if(room->getCardOwner(id)!=target) received=false;
                QVariantMap result=values; result["received"]=received; ctx.extra_data=result;
            }
            return ContinueEffects;
        }
        if (target->isAllNude()) return ContinueEffects;
        QStringList choices;
        if (!target->getEquips().isEmpty()) choices << "equip=" + ctx.invoker->objectName();
        choices << "obtain=" + ctx.invoker->objectName();
        const bool equip = room->askForChoice(target, objectName(), choices.join("+"),
            QVariant::fromValue(ctx.invoker)).startsWith("equip");
        QList<int> ids;
        if (equip) ids = target->getEquipsId();
        SkillContext receive = ctx;
        receive.choice = "receive";
        receive.extra_data = QVariantMap{{"donor", target->objectName()}, {"ids", ListI2V(ids)}, {"equip", equip}};
        skillEffect(receive, ctx.invoker);
        if(equip && receive.extra_data.toMap().value("received").toBool()) {
            SkillContext upgrade=ctx; upgrade.choice="upgrade"; skillEffect(upgrade,ctx.invoker);
        }
        return ContinueEffects;
    }
};

MobileXingxueCard::MobileXingxueCard()
{
}

bool MobileXingxueCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
    bool xingxue = Self->property("MobileXingxueLevelUp").toBool();
    int max = xingxue ? Self->getMaxHp() : Self->getHp();
    return targets.length() < max;
}

bool MobileXingxueCard::hasAliveTargets(ServerPlayer *player, QList<ServerPlayer *> &targets) const
{
    foreach (ServerPlayer *p, targets) {
        if (p == player) continue;
        if (p->isAlive())
            return true;
    }
    return false;
}

void MobileXingxueCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    bool xingxue = source->property("MobileXingxueLevelUp").toBool();
    QStringList target_names;
    foreach (ServerPlayer *t, targets)
        target_names << t->objectName();

    foreach (ServerPlayer *t, targets) {
        t->drawCards(1, "mobilexingxue");
        if (t->isAlive() && !t->isNude()) {
            QStringList choices;
            if (xingxue && hasAliveTargets(t, targets))
                choices << "give";
            choices << "put";

            QString choice = room->askForChoice(t, "mobilexingxue", choices.join("+"), target_names);

            if (choice == "put") {
                const Card *c = room->askForExchange(t, "mobilexingxue", 1, 1, true, "@xingxue-put");

                CardsMoveStruct m(c->getSubcards(), nullptr, Player::DrawPile, CardMoveReason(CardMoveReason::S_REASON_PUT, t->objectName()));
                room->setPlayerFlag(t, "Global_GongxinOperator");
                room->moveCardsAtomic(m, false);
                room->setPlayerFlag(t, "-Global_GongxinOperator");
            } else {
                QList<ServerPlayer *> new_targets = targets;
                new_targets.removeOne(t);
                QList<int> cards = t->handCards() + t->getEquipsId();
                room->askForYiji(t, cards, "mobilexingxue", false, false, false, 1, new_targets);
            }
        }
    }
}

class MobileXingxue : public TriggerSkillV2
{
public:
    MobileXingxue() : TriggerSkillV2("mobilexingxue") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const bool upgraded = ctx.owner->property("MobileXingxueLevelUp").toBool();
        const int maximum = upgraded ? ctx.owner->getMaxHp() : ctx.owner->getHp();
        if (maximum <= 0) return false;
        ctx.targets = room->askForPlayersChosen(ctx.owner, room->getAlivePlayers(), objectName(), 0, maximum,
            "@mobilexingxue:" + QString::number(maximum), true);
        ctx.choice = upgraded ? "upgraded" : "normal";
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "gift") {
            const QVariantMap values = ctx.extra_data.toMap();
            ServerPlayer *giver = room->findPlayerByObjectName(values.value("giver").toString(), true);
            const int id = values.value("card").toInt();
            if (giver && giver->isAlive() && room->getCardOwner(id) == giver
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip))
                room->giveCard(giver, target, Sanguosha->getCard(id), objectName());
            return false;
        }
        target->drawCards(getEffectiveAmount(ctx), objectName());
        if (target->isDead() || target->isNude()) return false;
        QList<ServerPlayer *> others;
        QStringList names;
        for (ServerPlayer *recipient : ctx.targets) {
            names << recipient->objectName();
            if (recipient != target && recipient->isAlive()) others << recipient;
        }
        const QString choices = ctx.choice == "upgraded" && !others.isEmpty() ? "give+put" : "put";
        const QString choice = room->askForChoice(target, objectName(), choices, names);
        const Card *selected = room->askForExchange(target, objectName(), 1, 1, true, "@xingxue-put");
        if (!selected || selected->subcardsLength() != 1) return false;
        const int id = selected->getSubcards().first();
        if (room->getCardOwner(id) != target || (room->getCardPlace(id) != Player::PlaceHand
            && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        for (int index = others.size() - 1; index >= 0; --index)
            if (others.at(index)->isDead()) others.removeAt(index);
        if (choice == "give" && !others.isEmpty()) {
            ServerPlayer *recipient = room->askForPlayerChosen(target, others, objectName());
            if (recipient) {
                SkillContext gift = ctx;
                gift.choice = "gift";
                gift.extra_data = QVariantMap{{"giver", target->objectName()}, {"card", id}};
                // The second recipient gets a separate hook; no borrowed context or fake skill card is created.
                skillEffect(event, room, ctx.owner, gift, recipient);
            }
        } else {
            const bool priorFlag = target->hasFlag("Global_GongxinOperator");
            if (!priorFlag) room->setPlayerFlag(target, "Global_GongxinOperator");
            const auto clear = qScopeGuard([&] {
                if (!priorFlag) room->setPlayerFlag(target, "-Global_GongxinOperator");
            });
            room->moveCardTo(Sanguosha->getCard(id), target, nullptr, Player::DrawPile,
                CardMoveReason(CardMoveReason::S_REASON_PUT, target->objectName(), objectName(), ""), false);
        }
        return false;
    }
};

MobileSidiCard::MobileSidiCard()
{
    setSkillName("mobilesidi");
}

bool MobileSidiCard::targetFilter(const QList<const Player *> &targets, const Player *target, const Player *Self) const
{
    if(targets.isEmpty()){
		return target!=Self&&target->getMark("mobilesidiFrom"+Self->objectName())<1;
	}
	return targets.length() < 2;
}

bool MobileSidiCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length()==2;
}

void MobileSidiCard::onUse(Room *room, CardUseStruct &use) const
{
    ServerPlayer *tp = use.to.takeLast();
	use.to.first()->setTag("mobilesidiTo", QVariant::fromValue(tp));
	room->setPlayerMark(use.to.first(),"mobilesidiFrom"+use.from->objectName(),1);
	room->setPlayerMark(use.to.first(),"&mobilesidi+:+"+tp->getGeneralName(),1,QList<ServerPlayer*>()<<use.from);
	SkillCard::onUse(room,use);
}

class MobileSidiVS : public ViewAsSkillV2
{
public:
    MobileSidiVS() : ViewAsSkillV2("mobilesidi") { response_pattern="@@mobilesidi"; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileSidiCard"; }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    bool canActivate(const ActiveSkillRequest &r) const override { return r.initiator && r.pattern=="@@mobilesidi"; }
    bool canSelectTarget(const ActiveSkillRequest &r,const QList<const Player *> &selected,const Player *target) const override
    {
        if(!r.initiator || !target || target->isDead() || selected.contains(target)) return false;
        if(selected.isEmpty()) return target!=r.initiator && !r.initiator->property("mobilesidi_blocked").toString().split('+').contains(target->objectName());
        return selected.size()==1;
    }
    bool targetsFeasible(const ActiveSkillRequest &,const QList<const Player *> &targets) const override { return targets.size()==2; }
    EffectFlow effect(SkillContext &ctx) const override { ctx.manual_effect=true; return effectOnTargetGroup(ctx,ctx.targets); }
    EffectFlow effectOnTarget(SkillContext &ctx,ServerPlayer *target) const override
    { ctx.extra_data=QVariantMap{{"target",target->objectName()},{"amount",getEffectiveAmount(ctx)}}; return ContinueEffects; }
    EffectFlow effectOnTargetGroup(SkillContext &ctx,const QList<ServerPlayer *> &targets) const override
    {
        if(targets.size()!=2 || !ctx.invoker || !ctx.initiator) return ContinueEffects;
        Room *room=ctx.invoker->getRoom(); SkillContext first=ctx,second=ctx; first.extra_data=QVariant(); second.extra_data=QVariant();
        skillEffect(first,targets.first()); skillEffect(second,targets.last());
        const QVariantMap a=first.extra_data.toMap(),b=second.extra_data.toMap();
        ServerPlayer *actor=room->findPlayerByObjectName(a.value("target").toString()),*predicted=room->findPlayerByObjectName(b.value("target").toString());
        if(!actor || !predicted || actor==predicted || actor->isDead() || predicted->isDead() || a.value("amount").toInt()<=0 || b.value("amount").toInt()<=0) return ContinueEffects;
        const QVariantMap origin=ctx.initiator->getTag("MobileSidiPromptOrigin").toMap(); if(origin.isEmpty()) return ContinueEffects;
        QVariantList pending=actor->getTag("MobileSidiPredictions").toList();
        for(const QVariant &value:pending) if(value.toMap().value("owner")==origin.value("owner") && value.toMap().value("instance")==origin.value("instance")) return ContinueEffects;
        const int serial=room->getTag("MobileSidiSerial").toInt()+1; room->setTag("MobileSidiSerial",serial);
        QVariantMap receipt=origin; receipt["serial"]=serial; receipt["actor"]=actor->objectName(); receipt["target"]=predicted->objectName(); receipt["amount"]=getEffectiveAmount(ctx); receipt["actor_amount"]=a.value("amount"); receipt["target_amount"]=b.value("amount");
        pending << receipt; actor->setTag("MobileSidiPredictions",pending);
        room->setPlayerMark(actor,"&mobilesidi+:+"+predicted->getGeneralName(),1,{ctx.initiator}); return ContinueEffects;
    }
};

class MobileSidi : public TriggerSkillV2
{
public:
    MobileSidi() : TriggerSkillV2("mobilesidi") { global=true; events << CardFinished << TargetSpecifying; view_as_skill=new MobileSidiVS; }
    bool recordEvent(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data) const override
    {
        if(event==CardFinished) { const Card *card=data.value<CardUseStruct>().card; if(card) card->removeTag("MobileSidiDue"); return false; }
        if(event!=TargetSpecifying || !actor) return false;
        const CardUseStruct use=data.value<CardUseStruct>(); if(!use.card || use.card->getTypeId()<=0 || use.card->isKindOf("DelayedTrick")) return false;
        QVariantList due=actor->getTag("MobileSidiPredictions").toList(); actor->removeTag("MobileSidiPredictions");
        QVariantList matching; const qint64 useId=room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id").toLongLong();
        for(const QVariant &value:due) { QVariantMap r=value.toMap(); if(use.to.size()==1 && r.value("target").toString()==use.to.first()->objectName()) { r["use_id"]=useId; matching << r; } }
        // Every first qualifying use consumes its predictions, including an incorrect prediction.
        use.card->setTag("MobileSidiDue",matching);
        for(const QString &mark:actor->getMarkNames()) if(mark.startsWith("&mobilesidi+:+")) room->setPlayerMark(actor,mark,0);
        return false;
    }
    TriggerList triggerable(TriggerEvent event,Room *,ServerPlayer *actor,QVariant &data) const override
    {
        if(event!=CardFinished || !actor || actor->isDead() || !actor->hasSkill(objectName())) return {};
        const Card *card=data.value<CardUseStruct>().card; return card && card->getTypeId()>0 && !card->isKindOf("DelayedTrick")?TriggerList{{actor,{objectName()}}}:TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data,QList<SkillContext> &contexts) const override
    {
        if(event!=TargetSpecifying) return false;
        const Card *card=data.value<CardUseStruct>().card; if(!actor || !card) return true;
        for(const QVariant &value:card->getTag("MobileSidiDue").toList()) {
            const QVariantMap r=value.toMap(); ServerPlayer *owner=room->findPlayerByObjectName(r.value("owner").toString()); if(!owner || owner->isDead() || r.value("use_id").toLongLong()<=0 || r.value("use_id").toLongLong()!=room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id").toLongLong()) continue;
            SkillContext ctx; ctx.skill_name=objectName(); ctx.owner=owner; ctx.initiator=owner; ctx.invoker=owner; ctx.instanceID=r.value("serial").toInt();
            ctx.sourceRef=SkillInstanceRef(r.value("source_owner").toString(),SkillInstanceKey(r.value("source_skill").toString(),r.value("source_instance").toInt()));
            ctx.extra_data=r; ctx.amount=r.value("amount").toInt(); ctx.is_forced=true; ctx.original_data=&data; ctx.current_event=event; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room,const SkillContext &ctx) const override
    {
        if(ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room,ctx);
        const Card *card=ctx.original_data->value<CardUseStruct>().card;
        return card && ctx.extra_data.toMap().value("use_id").toLongLong()==room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id").toLongLong() && card->getTag("MobileSidiDue").toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event,Room *room,ServerPlayer *,SkillContext &ctx) const override
    {
        if(event==CardFinished) { ctx.targets={ctx.owner}; return true; }
        const QVariantMap r=ctx.extra_data.toMap(); ctx.choice=r.value("target").toString()==ctx.owner->objectName()?"self":room->askForChoice(ctx.owner,objectName(),"mobilesidi1+mobilesidi2",*ctx.original_data); return true;
    }
    bool effect(TriggerEvent event,Room *room,ServerPlayer *actor,SkillContext &ctx) const override
    {
        if(event==CardFinished) return false; ctx.manual_effect=true; const QVariantMap r=ctx.extra_data.toMap();
        if(ctx.choice=="self" || ctx.choice=="mobilesidi2") { skillEffect(event,room,actor,ctx,ctx.invoker); return false; }
        SkillContext removal=ctx; removal.choice="remove"; removal.amount=r.value("target_amount").toInt(); skillEffect(event,room,actor,removal,room->findPlayerByObjectName(r.value("target").toString()));
        if(!room->getCurrentDyingPlayer()) { SkillContext damage=ctx; damage.choice="damage"; damage.amount=r.value("actor_amount").toInt(); skillEffect(event,room,actor,damage,room->findPlayerByObjectName(r.value("actor").toString())); }
        return false;
    }
    bool effectTarget(TriggerEvent event,Room *room,ServerPlayer *,SkillContext &ctx,ServerPlayer *target) const override
    {
        if(getEffectiveAmount(ctx)<=0) return false;
        if(event!=CardFinished) {
            if(ctx.choice=="remove") { CardUseStruct use=ctx.original_data->value<CardUseStruct>(); use.to.removeOne(target); *ctx.original_data=QVariant::fromValue(use); }
            else if(ctx.choice=="damage") room->damage(DamageStruct(objectName(),ctx.invoker,target,getEffectiveAmount(ctx)));
            else target->drawCards((ctx.choice=="self"?1:2)*getEffectiveAmount(ctx),objectName());
            return false;
        }
        QStringList blocked; for(ServerPlayer *p:room->getOtherPlayers(target)) for(const QVariant &value:p->getTag("MobileSidiPredictions").toList())
            if(value.toMap().value("owner").toString()==ctx.activationRef.ownerObjectName && value.toMap().value("instance").toInt()==ctx.activationRef.key.instanceID) blocked << p->objectName();
        if(blocked.size()>=room->getOtherPlayers(target).size()) return false;
        const QVariant prior=target->property("mobilesidi_blocked"),origin=target->getTag("MobileSidiPromptOrigin");
        auto restore=qScopeGuard([&]{ target->setProperty("mobilesidi_blocked",prior); room->notifyProperty(target,target,"mobilesidi_blocked"); target->setTag("MobileSidiPromptOrigin",origin); });
        target->setProperty("mobilesidi_blocked",blocked.join('+')); room->notifyProperty(target,target,"mobilesidi_blocked");
        target->setTag("MobileSidiPromptOrigin",QVariantMap{{"owner",ctx.activationRef.ownerObjectName},{"skill",ctx.activationRef.key.skillName},{"instance",ctx.activationRef.key.instanceID},
            {"source_owner",ctx.sourceRef.ownerObjectName},{"source_skill",ctx.sourceRef.key.skillName},{"source_instance",ctx.sourceRef.key.instanceID}});
        Room::AcceptedViewAsEffectScope prompt(room,target,objectName(),ctx); if(prompt.isValid()) room->askForUseCard(target,"@@mobilesidi","mobilesidi0"); return false;
    }
};

MobileYaomingCard::MobileYaomingCard()
{
    setSkillName("mobileyaoming");
}

bool MobileYaomingCard::targetFilter(const QList<const Player *> &targets, const Player *target, const Player *Self) const
{
    if(targets.isEmpty()){
		if(target->getHandcardNum()>Self->getHandcardNum())
			return Self->canDiscard(target,"he");
		return target->getHandcardNum()<=Self->getHandcardNum();
	}
	return false;
}

void MobileYaomingCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	source->loseMark("&charge_num");
    foreach (ServerPlayer *t, targets) {
		QStringList choices;
		if(t->getHandcardNum()>source->getHandcardNum())
			choices << "discard";
		else{
			if(t->getHandcardNum()==source->getHandcardNum()){
				if(t!=source&&source->canDiscard(t,"he"))
					choices << "discard";
			}
			choices << "draw";
		}
		QString choice = room->askForChoice(source,"mobileyaoming",choices.join("+"),QVariant::fromValue(t));
		if(choice=="discard"){
			int id = room->askForCardChosen(source,t,"he","mobileyaoming",false,Card::MethodDiscard);
			if(id>=0) room->throwCard(id,"mobileyaoming",t,source);
		}else
			t->drawCards(1,objectName());
		if(!source->getTag("mobileyaomingChoice").isNull()){
			if(source->getTag("mobileyaomingChoice").toString()!=choice){
				source->gainMark("&charge_num");
				source->removeTag("mobileyaomingChoice");
				continue;
			}
		}
		source->setTag("mobileyaomingChoice", choice);
	}
}

class MobileYaomingVS : public ViewAsSkillV2
{
public:
    MobileYaomingVS() : ViewAsSkillV2("mobileyaoming") { response_pattern = "@@mobileyaoming"; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileYaomingCard"; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card=ViewAsSkillV2::createCard(request);
        if(card && request.initiator) card->setTag("MobileYaomingPrevious",request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,request.activationRef.key.instanceID,"yaoming_choice"));
        return card;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.initiator->getMark("&charge_num") > 0
            && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY || request.pattern == "@@mobileyaoming");
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return target && request.initiator && selected.isEmpty()
            && (target->getHandcardNum() <= request.initiator->getHandcardNum()
                || request.initiator->canDiscard(target, "he"));
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator || ctx.initiator->getMark("&charge_num") < 1) return false;
        ctx.initiator->loseMark("&charge_num"); return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "charge_refund") {
            target->gainMark("&charge_num", getEffectiveAmount(ctx)); return ContinueEffects;
        }
        if(getEffectiveAmount(ctx)<=0) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        QStringList choices;
        if (target->getHandcardNum() >= ctx.invoker->getHandcardNum() && target != ctx.invoker
            && ctx.invoker->canDiscard(target, "he")) choices << "discard";
        if (target->getHandcardNum() <= ctx.invoker->getHandcardNum()) choices << "draw";
        if (choices.isEmpty()) return ContinueEffects;
        const QString choice = room->askForChoice(ctx.invoker, objectName(), choices.join("+"), QVariant::fromValue(target));
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
        QString previous=ctx.use_card?ctx.use_card->getTag("MobileYaomingPrevious").toString():QString();
        if (holder && holder->getSkillInstanceIds(ref.key.skillName).contains(ref.key.instanceID)) {
            // Commit before the resulting card movement can re-enter this exact instance.
            holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "yaoming_choice",
                !previous.isEmpty() && previous != choice ? QString() : choice);
        }
        if (choice == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
        else for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive()
                  && ctx.invoker->isAlive() && ctx.invoker->canDiscard(target, "he"); ++i) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0 && ctx.invoker->canDiscard(target, id)) room->throwCard(id, objectName(), target, ctx.invoker);
        }
        if (!previous.isEmpty() && previous != choice && ctx.invoker->isAlive()) {
            SkillContext refund = ctx; refund.choice = "charge_refund";
            skillEffect(refund, ctx.invoker);
        }
        return ContinueEffects;
    }
};

class MobileYaoming : public TriggerSkillV2
{
public:
    MobileYaoming() : TriggerSkillV2("mobileyaoming")
    {
        events << Damaged; frequency = Compulsory;
        view_as_skill = new MobileYaomingVS;
        setProperty("ChargeNum", "2/4");
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return true; }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        skillEffect(event, room, player, ctx, ctx.owner);
        if (ctx.owner->isDead() || ctx.owner->getMark("&charge_num") < 1
            || ctx.activationRef.ownerObjectName != ctx.owner->objectName()
            || !ctx.owner->getSkillInstanceIds(ctx.activationRef.key.skillName).contains(ctx.activationRef.key.instanceID)) return false;
        // Select the existing exact activation for the optional new action; no disposable state-bearing child.
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int previous = ctx.owner->getMark(selector);
        room->setPlayerMark(ctx.owner, selector, ctx.activationRef.key.instanceID);
        const auto restore = qScopeGuard([&] { room->setPlayerMark(ctx.owner, selector, previous); });
        room->askForUseCard(ctx.owner, "@@mobileyaoming", "mobileyaoming0");
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        int capacity = 0;
        for (const Skill *skill : target->getVisibleSkillList()) {
            const QString value = skill->property("ChargeNum").toString();
            if (value.contains("/")) capacity += value.split("/").last().toInt();
        }
        const int gain = qMin(qMax(0, capacity - target->getMark("&charge_num")),
            ctx.original_data->value<DamageStruct>().damage * getEffectiveAmount(ctx));
        if (gain > 0) target->gainMark("&charge_num", gain);
        return false;
    }
};

class MobileQingxi : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    MobileQingxi() : TriggerSkillV2("mobileqingxi")
    {
        events << DamageCaused << EventSkillInvoking;
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        // Active entries commit their own quota; only this exact trigger's bypass path remains.
        if (accepted.executionID == 0 && accepted.bypass_cost && accepted.activationRef == ctx.activationRef
            && accepted.sourceRef == ctx.sourceRef && parseSkillName(accepted.skill_name) == objectName())
            addUsage(accepted);
    }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && player->hasTurn()
            && damage.to && damage.to != player ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!ctx.owner->askForSkillInvoke(objectName() + "$-1", damage.to)) return false;
        ctx.targets = {damage.to};
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Trigger quotas commit after acceptance, even if the defender discards.
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target != ctx.original_data->value<DamageStruct>().to) return false;
        const int count = qMax(1, 4 - ctx.owner->distanceTo(target));
        if (target->canDiscard(target, "h") && room->askForDiscard(target, objectName(), count, count,
                true, false, "mobileqingxi0:" + QString::number(count))) return false;
        ctx.owner->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
        return false;
    }
};

class MobileAnguo : public TriggerSkillV2
{
public:
    MobileAnguo() : TriggerSkillV2("mobileanguo") { global=true; events << GameStart << EventPhaseStart << DamageInflicted << Dying; }
    static void project(Room *room)
    {
        QMap<QString,int> counts; for(const QVariant &value:room->getTag("MobileAnguoRelations").toList()) ++counts[value.toMap().value("target").toString()];
        for(ServerPlayer *p:room->getAllPlayers(true)) room->setPlayerMark(p,"&mobileanguo",counts.value(p->objectName()));
    }
    QVariantMap relation(Room *room,const SkillContext &ctx) const
    {
        for(const QVariant &value:room->getTag("MobileAnguoRelations").toList()) { const QVariantMap r=value.toMap();
            if(r.value("owner").toString()==ctx.activationRef.ownerObjectName && r.value("instance").toInt()==ctx.activationRef.key.instanceID) return r; }
        return {};
    }
    bool prepareSource(Room *room,SkillContext &ctx) const override
    {
        if(!TriggerSkillV2::prepareSource(room,ctx)) return false;
        if(!ctx.activationRef.isValid()) return true;
        if(ctx.current_event==GameStart) { ctx.is_forced=true; return true; }
        const QVariantMap r=relation(room,ctx); ServerPlayer *guard=room->findPlayerByObjectName(r.value("target").toString());
        if(!guard || guard->isDead()) return false;
        if(ctx.current_event==DamageInflicted) { ctx.is_forced=true; const DamageStruct d=ctx.original_data->value<DamageStruct>(); return d.from && d.from!=guard && d.damage>=ctx.owner->getHp(); }
        return true;
    }
    TriggerList triggerable(TriggerEvent event,Room *,ServerPlayer *actor,QVariant &) const override
    {
        if(!actor || actor->isDead() || !actor->hasSkill(objectName()) || event==Dying) return {};
        return event==GameStart || event==DamageInflicted || (event==EventPhaseStart && actor->getPhase()==Player::Play)?TriggerList{{actor,{objectName()}}}:TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data,QList<SkillContext> &contexts) const override
    {
        if(event!=Dying) return false; const DyingStruct dying=data.value<DyingStruct>(); if(!actor || dying.who!=actor || actor->getHp()>=1) return true;
        for(const QVariant &value:room->getTag("MobileAnguoRelations").toList()) {
            const QVariantMap r=value.toMap(); if(r.value("target").toString()!=actor->objectName()) continue;
            ServerPlayer *owner=room->findPlayerByObjectName(r.value("actor").toString()); if(!owner || owner->isDead()) continue;
            SkillContext ctx; ctx.skill_name=objectName(); ctx.owner=room->findPlayerByObjectName(r.value("owner").toString(),true); ctx.initiator=owner; ctx.invoker=owner; ctx.targets={actor};
            ctx.instanceID=r.value("serial").toInt(); ctx.sourceRef=SkillInstanceRef(r.value("source_owner").toString(),SkillInstanceKey(r.value("source_skill").toString(),r.value("source_instance").toInt()));
            ctx.amount=r.value("amount").toInt(); ctx.extra_data=r; ctx.is_forced=true; ctx.current_event=event; ctx.original_data=&data; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room,const SkillContext &ctx) const override
    { return ctx.activationRef.isValid()?TriggerSkillV2::isSourceAvailable(room,ctx):room->getTag("MobileAnguoRelations").toList().contains(ctx.extra_data); }
    bool cost(TriggerEvent event,Room *room,ServerPlayer *,SkillContext &ctx) const override
    {
        if(event==Dying) return true;
        if(event==DamageInflicted) { ctx.targets={ctx.owner}; return true; }
        const QStringList visited=ctx.owner->getSkillInstanceStateValue(objectName(),ctx.activationRef.key.instanceID,"visited").toStringList(); QList<ServerPlayer *> available;
        for(ServerPlayer *p:room->getOtherPlayers(ctx.owner)) if(!visited.contains(p->objectName())) available << p;
        if(available.isEmpty()) return false;
        ServerPlayer *target=room->askForPlayerChosen(ctx.owner,available,objectName(),event==GameStart?"mobileanguo0":"mobileanguo1",event!=GameStart,true);
        if(!target) return false; ctx.targets={target}; return true;
    }
    bool effect(TriggerEvent event,Room *room,ServerPlayer *actor,SkillContext &ctx) const override
    {
        if(event!=Dying) return false; ctx.manual_effect=true;
        QVariantList kept=room->getTag("MobileAnguoRelations").toList(); kept.removeAll(ctx.extra_data); room->setTag("MobileAnguoRelations",kept); project(room);
        ServerPlayer *guard=ctx.original_data->value<DyingStruct>().who;
        SkillContext recovery=ctx; recovery.choice="recover"; skillEffect(event,room,actor,recovery,guard);
        if(!ctx.invoker || ctx.invoker->isDead()) return false;
        SkillContext liability=ctx; liability.choice="liability"; liability.extra_data=QVariant(); skillEffect(event,room,actor,liability,ctx.invoker);
        if(liability.extra_data.toBool()) { SkillContext armor=ctx; armor.choice="armor"; skillEffect(event,room,actor,armor,guard); } return false;
    }
    bool effectTarget(TriggerEvent event,Room *room,ServerPlayer *,SkillContext &ctx,ServerPlayer *target) const override
    {
        const int amount=getEffectiveAmount(ctx); if(amount<=0) return false;
        if(event==DamageInflicted) { const DamageStruct d=ctx.original_data->value<DamageStruct>(); return d.to==target && target->damageRevises(*ctx.original_data,-d.damage); }
        if(event==Dying) {
            if(ctx.choice=="recover") { if(target->getHp()<1) room->recover(target,RecoverStruct(objectName(),ctx.invoker,1-target->getHp())); }
            else if(ctx.choice=="armor") target->gainHujia(amount);
            else {
                QStringList choices; if(target->getHp()>1) choices << "hp"; if(target->getMaxHp()>1) choices << "max_hp"; if(choices.isEmpty()) return false;
                const QString choice=room->askForChoice(target,objectName(),choices.join('+'));
                ctx.extra_data=true;
                if(choice=="hp" && target->getHp()>1) room->loseHp(target,target->getHp()-1,true,ctx.invoker,objectName());
                else if(choice=="max_hp" && target->getMaxHp()>1) room->loseMaxHp(target,target->getMaxHp()-1,objectName());
            }
            return false;
        }
        QStringList visited=ctx.owner->getSkillInstanceStateValue(objectName(),ctx.activationRef.key.instanceID,"visited").toStringList();
        if(visited.contains(target->objectName())) return false; visited << target->objectName(); ctx.owner->setSkillInstanceStateValue(objectName(),ctx.activationRef.key.instanceID,"visited",visited);
        QVariantList receipts; for(const QVariant &value:room->getTag("MobileAnguoRelations").toList()) if(value.toMap().value("owner").toString()!=ctx.activationRef.ownerObjectName || value.toMap().value("instance").toInt()!=ctx.activationRef.key.instanceID) receipts << value;
        const int serial=room->getTag("MobileAnguoSerial").toInt()+1; room->setTag("MobileAnguoSerial",serial);
        receipts << QVariantMap{{"serial",serial},{"owner",ctx.activationRef.ownerObjectName},{"skill",ctx.activationRef.key.skillName},{"instance",ctx.activationRef.key.instanceID},
            {"source_owner",ctx.sourceRef.ownerObjectName},{"source_skill",ctx.sourceRef.key.skillName},{"source_instance",ctx.sourceRef.key.instanceID},{"actor",ctx.invoker->objectName()},{"target",target->objectName()},{"amount",amount}};
        room->setTag("MobileAnguoRelations",receipts); project(room); return false;
    }
};

class MobileBenxi : public TriggerSkillV2
{
public:
    MobileBenxi() : TriggerSkillV2("mobilebenxi") { global=true; events << EventPhaseStart << EventPhaseChanging << PreCardUsed << CardFinished << Death << TurnBroken; }
    static void project(Room *room,ServerPlayer *actor)
    {
        int total=0; for(const QVariant &value:actor->getTag("MobileBenxiReceipts").toList()) total+=value.toMap().value("count").toInt();
        room->setPlayerMark(actor,"&mobilebenxi",total);
    }
    bool recordEvent(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data) const override
    {
        if(event==TurnBroken && actor) {
            // An interrupted play phase can stay active in history. The distance bonus ends with the broken turn.
            actor->setTag("MobileBenxiReceipts",QVariantList()); project(room,actor); return false;
        }
        if(!actor || (event!=Death && event!=EventPhaseChanging)) return false;
        if(event==Death && data.value<DeathStruct>().who!=actor) return false;
        QVariantList kept;
        for(const QVariant &value:actor->getTag("MobileBenxiReceipts").toList()) {
            const QVariantMap phase=room->historyEvent(value.toMap().value("phase").toLongLong());
            if(event!=Death && phase.value("status").toString()=="active") kept << value;
        }
        actor->setTag("MobileBenxiReceipts",kept); project(room,actor); return false;
    }
    TriggerList triggerable(TriggerEvent event,Room *,ServerPlayer *actor,QVariant &) const override
    { return event==EventPhaseStart && actor && actor->isAlive() && actor->getPhase()==Player::Play && actor->hasSkill(objectName()) && actor->canDiscard(actor,"he")?TriggerList{{actor,{objectName()}}}:TriggerList(); }
    bool collectTriggerContexts(TriggerEvent event,Room *room,ServerPlayer *actor,QVariant &data,QList<SkillContext> &contexts) const override
    {
        if(event==EventPhaseStart) return false;
        if(!actor || actor->isDead() || (event!=PreCardUsed && event!=CardFinished)) return true;
        const CardUseStruct use=data.value<CardUseStruct>(); if(!use.card || use.from!=actor) return true;
        QVariantList receipts;
        if(event==PreCardUsed) {
            if(!use.card->isKindOf("BasicCard") && !use.card->isNDTrick()) return true;
            for(const QVariant &value:actor->getTag("MobileBenxiReceipts").toList()) if(!value.toMap().value("spent").toBool()
                && value.toMap().value("phase")==room->historyScopes().value("phase_id")) receipts << value;
        } else {
            const qint64 useId=room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id").toLongLong(); if(useId<=0) return true;
            const QVariantMap history=room->queryCardUseDamage(useId); if(history.contains("error") || !history.value("complete").toBool() || history.value("items").toList().isEmpty()) return true;
            for(const QVariant &value:use.card->getTag("MobileBenxiUses").toList()) if(value.toMap().value("use_id").toLongLong()==useId) receipts << value;
        }
        for(const QVariant &value:receipts) {
            const QVariantMap receipt=value.toMap(); SkillContext ctx; ctx.skill_name=objectName(); ctx.owner=room->findPlayerByObjectName(receipt.value("owner").toString(),true);
            ctx.initiator=actor; ctx.invoker=actor; ctx.instanceID=receipt.value("serial").toInt(); ctx.sourceRef=SkillInstanceRef(receipt.value("source_owner").toString(),SkillInstanceKey(receipt.value("source_skill").toString(),receipt.value("source_instance").toInt()));
            ctx.amount=receipt.value("amount").toInt(); ctx.extra_data=receipt; ctx.is_forced=true; ctx.current_event=event; ctx.original_data=&data; ctx.targets={actor}; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room,const SkillContext &ctx) const override
    {
        if(ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room,ctx);
        if(!ctx.initiator || !ctx.original_data) return false;
        if(ctx.current_event==PreCardUsed) {
            for(const QVariant &value:ctx.initiator->getTag("MobileBenxiReceipts").toList())
                if(value.toMap().value("serial")==ctx.extra_data.toMap().value("serial")) return true;
            return false;
        }
        return ctx.original_data->value<CardUseStruct>().card && ctx.original_data->value<CardUseStruct>().card->getTag("MobileBenxiUses").toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event,Room *room,ServerPlayer *,SkillContext &ctx) const override
    {
        if(event==PreCardUsed) {
            // The next qualifying card consumes the pending privilege even when its skill effect is canceled.
            QVariantList pending=ctx.initiator->getTag("MobileBenxiReceipts").toList();
            for(int i=0;i<pending.size();++i) if(pending[i].toMap().value("serial")==ctx.extra_data.toMap().value("serial")) { QVariantMap spent=pending[i].toMap(); spent["spent"]=true; pending[i]=spent; break; }
            ctx.initiator->setTag("MobileBenxiReceipts",pending); return true;
        }
        if(event!=EventPhaseStart) return true;
        const Card *cards=room->askForExchange(ctx.owner,objectName(),999,1,true,"mobilebenxi0",true); if(!cards) return false;
        QVariantList ids; for(int id:cards->getSubcards()) ids << id; ctx.extra_data=ids; ctx.initiator=ctx.owner; ctx.targets={ctx.owner}; return !ids.isEmpty();
    }
    bool pay(TriggerEvent event,Room *room,ServerPlayer *,SkillContext &ctx) const override
    {
        if(event!=EventPhaseStart) return true;
        QList<int> ids; for(const QVariant &value:ctx.extra_data.toList()) { const int id=value.toInt(); if(ids.contains(id) || Sanguosha->getCard(id)->hasFlag("using") || room->getCardOwner(id)!=ctx.initiator || (room->getCardPlace(id)!=Player::PlaceHand && room->getCardPlace(id)!=Player::PlaceEquip) || !ctx.initiator->canDiscard(ctx.initiator,id)) return false; ids << id; }
        if(ids.isEmpty()) return false; DummyCard cards(ids); room->throwCard(&cards,objectName(),ctx.initiator); return true;
    }
    bool effect(TriggerEvent event,Room *room,ServerPlayer *actor,SkillContext &ctx) const override
    {
        if(event!=PreCardUsed) return false;
        ctx.manual_effect=true; QVariantMap receipt=ctx.extra_data.toMap();
        CardUseStruct use=ctx.original_data->value<CardUseStruct>(); const qint64 useId=room->historyParent(room->currentHistoryEventId(),"use_card",true).value("id").toLongLong();
        receipt["use_id"]=useId; QVariantList applied=use.card->getTag("MobileBenxiUses").toList(); applied << receipt; use.card->setTag("MobileBenxiUses",applied);
        QList<ServerPlayer *> available=room->getCardTargets(ctx.invoker,use.card,use.to);
        for(int i=available.size()-1;i>=0;--i) if(ctx.invoker->distanceTo(available[i])!=1) available.removeAt(i);
        const QVariant previous=ctx.invoker->getTag("mobilebenxiUse"); ctx.invoker->setTag("mobilebenxiUse",*ctx.original_data);
        auto restore=qScopeGuard([&]{ if(previous.isValid()) ctx.invoker->setTag("mobilebenxiUse",previous); else ctx.invoker->removeTag("mobilebenxiUse"); });
        const QList<ServerPlayer *> selected=room->askForPlayersChosen(ctx.invoker,available,objectName(),0,receipt.value("count").toInt(),"mobilebenxi1:"+use.card->objectName(),false,false);
        for(ServerPlayer *target:selected) { SkillContext local=ctx; local.choice="add"; skillEffect(event,room,actor,local,target); } return false;
    }
    bool effectTarget(TriggerEvent event,Room *room,ServerPlayer *,SkillContext &ctx,ServerPlayer *target) const override
    {
        if(getEffectiveAmount(ctx)<=0) return false;
        if(event==CardFinished) {
            const Card *card=ctx.original_data->value<CardUseStruct>().card; QVariantList receipts=card->getTag("MobileBenxiUses").toList(); receipts.removeAll(ctx.extra_data); card->setTag("MobileBenxiUses",receipts);
            target->drawCards(5*getEffectiveAmount(ctx),objectName()); return false;
        }
        if(event==PreCardUsed) { CardUseStruct use=ctx.original_data->value<CardUseStruct>(); if(!use.to.contains(target)) { use.to << target; room->sortByActionOrder(use.to); *ctx.original_data=QVariant::fromValue(use); } return false; }
        const int serial=room->getTag("MobileBenxiSerial").toInt()+1; room->setTag("MobileBenxiSerial",serial);
        QVariantMap receipt{{"serial",serial},{"owner",ctx.activationRef.ownerObjectName},{"skill",ctx.activationRef.key.skillName},{"instance",ctx.activationRef.key.instanceID},
            {"source_owner",ctx.sourceRef.ownerObjectName},{"source_skill",ctx.sourceRef.key.skillName},{"source_instance",ctx.sourceRef.key.instanceID},
            {"phase",room->historyScopes().value("phase_id")},{"count",ctx.extra_data.toList().size()*getEffectiveAmount(ctx)},{"amount",getEffectiveAmount(ctx)},{"spent",false}};
        QVariantList pending=target->getTag("MobileBenxiReceipts").toList(); pending << receipt; target->setTag("MobileBenxiReceipts",pending); project(room,target); return false;
    }
};

class MobilePingkou : public TriggerSkillV2
{
public:
    MobilePingkou() : TriggerSkillV2("mobilepingkou") { events << EventPhaseChanging; }
    int skipped(Room *room, const ServerPlayer *player) const
    {
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        if (turn <= 0) return -1;
        QVariantMap filter{{"kind", "phase"}, {"turn_id", turn}, {"player", player->objectName()}, {"limit", 64}};
        int count = 0;
        do {
            const QVariantMap page = room->queryHistoryEvents(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return -1;
            for (const QVariant &item : page.value("items").toList())
                if (item.toMap().value("outcome").toString() == "skipped") ++count;
            if (!page.value("has_more").toBool()) return count;
            filter["watermark"] = page.value("watermark");
            filter["after"] = page.value("next_after");
        } while (true);
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && data.value<PhaseChangeStruct>().to == Player::NotActive && skipped(room, player) > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int maximum = skipped(room, ctx.owner);
        if (maximum <= 0) return false;
        ctx.targets = room->askForPlayersChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(),
            0, maximum, "@pingkou:" + QString::number(maximum), true);
        return !ctx.targets.isEmpty();
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ctx.owner->peiyin(this);
        // Keep the selected damage recipients ordered, then offer the reward its own target hook.
        const QList<ServerPlayer *> recipients = ctx.targets;
        for (ServerPlayer *target : recipients) {
            SkillContext damage = ctx;
            damage.choice = "damage";
            skillEffect(event, room, ctx.owner, damage, target);
        }
        SkillContext reward = ctx;
        reward.choice = "reward";
        skillEffect(event, room, ctx.owner, reward, ctx.owner);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "damage") {
            room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        } else {
            QList<int> candidates;
            for (int id : room->getDrawPile())
                if (Sanguosha->getCard(id)->isKindOf("EquipCard")) candidates << id;
            qsanShuffle(candidates);
            while (candidates.size() > qMax(0, getEffectiveAmount(ctx))) candidates.removeLast();
            if (!candidates.isEmpty()) {
                DummyCard reward(candidates);
                room->obtainCard(target, &reward);
            }
        }
        return false;
    }
};

MobileFurongCard::MobileFurongCard()
{
    setSkillName("mobilefurong");
}

bool MobileFurongCard::targetFilter(const QList<const Player *> &targets, const Player *target, const Player *Self) const
{
	return targets.isEmpty()&&target!=Self;
}

void MobileFurongCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    foreach (ServerPlayer *t, targets) {
		QString schoice = room->askForChoice(source,"mobilefurong","zhenya+anfu",QVariant::fromValue(t));
		QString tchoice = room->askForChoice(t,"mobilefurong","fankang+guishun",QVariant::fromValue(source));
		if(schoice=="zhenya"){
			if(tchoice=="fankang"){
				room->damage(DamageStruct("mobilefurong",source,t));
				source->drawCards(1,"mobilefurong");
			}else if(t->getCardCount()>0){
				int id = room->askForCardChosen(source,t,"he","mobilefurong");
				if(id>-1){
					room->obtainCard(source,id,false);
					if(t->isDead()) continue;
					const Card*dc = room->askForExchange(source,"mobilefurong",2,2,true,"mobilefurong0:"+t->objectName());
					if(dc) room->giveCard(source,t,dc,"mobilefurong");
				}
			}
		}else{
			if(tchoice=="fankang"){
				room->damage(DamageStruct("mobilefurong",nullptr,source));
				source->drawCards(1,"mobilefurong");
			}else if(t->getCardCount()>1){
				const Card*dc = room->askForExchange(t,"mobilefurong",2,2,true,"mobilefurong1:"+source->objectName());
				if(dc) room->giveCard(t,source,dc,"mobilefurong");
			}else
				room->setPlayerMark(t,"&mobilefurong",1);
		}
	}
}

class MobileFurongVS : public ViewAsSkillV2
{
public:
    MobileFurongVS() : ViewAsSkillV2("mobilefurong", 0) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileFurongCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return target && target->isAlive() && target != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom(); const int amount = getEffectiveAmount(ctx);
        if (ctx.choice == "draw") { target->drawCards(amount,objectName()); return ContinueEffects; }
        if (ctx.choice == "self_damage") {
            room->damage(DamageStruct(objectName(),nullptr,target,amount));
            if (target->isAlive()) { SkillContext draw = ctx; draw.choice = "draw"; skillEffect(draw,target); }
            return ContinueEffects;
        }
        if (ctx.choice == "obtain" || ctx.choice == "give") {
            const QVariantMap values = ctx.extra_data.toMap(); ServerPlayer *from = room->findPlayerByObjectName(values.value("from").toString());
            QList<int> ids; if (from) for (const QVariant &value : values.value("ids").toList()) {
                const int id = value.toInt();
                if (ids.contains(id) || Sanguosha->getCard(id)->hasFlag("using") || room->getCardOwner(id) != from || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return ContinueEffects;
                ids << id;
            }
            if (from && !ids.isEmpty()) { DummyCard gift(ids); room->giveCard(from,target,&gift,objectName()); }
            return ContinueEffects;
        }
        const QString own = room->askForChoice(ctx.invoker,objectName(),"zhenya+anfu",QVariant::fromValue(target));
        const QString other = room->askForChoice(target,objectName(),"fankang+guishun",QVariant::fromValue(ctx.invoker));
        if (target->isDead() || ctx.invoker->isDead()) return ContinueEffects;
        if (other == "fankang") {
            if (own == "zhenya") {
                room->damage(DamageStruct(objectName(),ctx.invoker,target,amount));
                if (ctx.invoker->isAlive()) { SkillContext draw = ctx; draw.choice = "draw"; skillEffect(draw,ctx.invoker); }
            } else { SkillContext self = ctx; self.choice = "self_damage"; skillEffect(self,ctx.invoker); }
        } else if (own == "zhenya") {
            if (target->isNude()) return ContinueEffects;
            const int id = room->askForCardChosen(ctx.invoker,target,"he",objectName());
            if (id < 0) return ContinueEffects;
            SkillContext obtain = ctx; obtain.choice = "obtain";
            obtain.extra_data = QVariantMap{{"from",target->objectName()},{"ids",QVariantList{id}}};
            skillEffect(obtain,ctx.invoker);
            if (target->isDead() || ctx.invoker->isDead()) return ContinueEffects;
            const std::unique_ptr<const Card> gift(room->askForExchange(ctx.invoker,objectName(),2*amount,2*amount,true,"mobilefurong0:"+target->objectName()));
            if (gift) { SkillContext give = ctx; give.choice = "give"; give.extra_data = QVariantMap{{"from",ctx.invoker->objectName()},{"ids",ListI2V(gift->getSubcards())}}; skillEffect(give,target); }
        } else if (target->getCardCount() > 1) {
            const std::unique_ptr<const Card> gift(room->askForExchange(target,objectName(),2*amount,2*amount,true,"mobilefurong1:"+ctx.invoker->objectName()));
            if (gift) { SkillContext give = ctx; give.choice = "give"; give.extra_data = QVariantMap{{"from",target->objectName()},{"ids",ListI2V(gift->getSubcards())}}; skillEffect(give,ctx.invoker); }
        } else {
            const qint64 serial = room->getTag("MobileFurongSequence").toLongLong()+1; room->setTag("MobileFurongSequence",serial);
            QVariantList receipts = target->getTag("MobileFurongReceipts").toList();
            receipts << QVariantMap{{"serial",serial},{"owner",ctx.activationRef.ownerObjectName},{"skill",ctx.activationRef.key.skillName},{"instance",ctx.activationRef.key.instanceID},
                {"source_owner",ctx.sourceRef.ownerObjectName},{"source_skill",ctx.sourceRef.key.skillName},{"source_instance",ctx.sourceRef.key.instanceID},{"amount",amount}};
            target->setTag("MobileFurongReceipts",receipts); room->setPlayerMark(target,"&mobilefurong",1);
        }
        return ContinueEffects;
    }
};

class MobileFurong : public TriggerSkillV2
{
public:
    MobileFurong() : TriggerSkillV2("mobilefurong") { global = true; frequency = Compulsory; events << EventPhaseChanging << Death; view_as_skill = new MobileFurongVS; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        if (!actor) return false;
        if (event == Death && data.value<DeathStruct>().who == actor) { actor->removeTag("MobileFurongReceipts"); actor->removeTag("MobileFurongDue"); room->setPlayerMark(actor,"&mobilefurong",0); }
        else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::Draw) {
            // Spend pending phase liabilities at their due boundary, including canceled effects.
            actor->setTag("MobileFurongDue",actor->getTag("MobileFurongReceipts")); actor->removeTag("MobileFurongReceipts"); room->setPlayerMark(actor,"&mobilefurong",0);
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseChanging || !actor || actor->isDead() || data.value<PhaseChangeStruct>().to != Player::Draw) return true;
        for (const QVariant &value : actor->getTag("MobileFurongDue").toList()) {
            const QVariantMap receipt = value.toMap(); SkillContext ctx; ctx.skill_name = objectName();
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(),true); ctx.initiator = actor; ctx.invoker = actor;
            ctx.instanceID = receipt.value("serial").toInt(); ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),SkillInstanceKey(receipt.value("source_skill").toString(),receipt.value("source_instance").toInt()));
            ctx.extra_data = receipt; ctx.amount = receipt.value("amount").toInt(); ctx.original_data = &data; ctx.current_event = event; ctx.is_forced = true; ctx.targets << actor; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.initiator && ctx.initiator->getTag("MobileFurongDue").toList().contains(ctx.extra_data); }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantList due = ctx.initiator->getTag("MobileFurongDue").toList(); due.removeAll(ctx.extra_data); ctx.initiator->setTag("MobileFurongDue",due);
        if (getEffectiveAmount(ctx)>0) target->skip(Player::Draw); return false;
    }
};

class OlYongsi : public TriggerSkillV2
{
public:
    OlYongsi() : TriggerSkillV2("olyongsi") {
        frequency = Compulsory;
        events << DrawNCards << EventPhaseStart;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName())) return {};
        const bool eligible = event == DrawNCards ? data.value<DrawStruct>().reason == "draw_phase"
            : player->getPhase() == Player::Discard;
        return eligible ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = target;
        QVariant &data = *ctx.original_data;
        if (triggerEvent == DrawNCards) {
            if (target != ctx.invoker) return false;
            DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="draw_phase") return false;
			room->sendCompulsoryTriggerLog(player, "olyongsi", true);
            room->broadcastSkillInvoke("olyongsi");

            QSet<QString> kingdoms;
            QList<ServerPlayer *> alives = room->getAlivePlayers();
            foreach(ServerPlayer *p, alives) {
                QString kingdom = p->getKingdom();
                if (!kingdoms.contains(kingdom))
                    kingdoms.insert(kingdom);
            }
			draw.num = kingdoms.count() * getEffectiveAmount(ctx);
            data = QVariant::fromValue(draw);
        }
        else {
            if (ctx.owner->getPhase() == Player::Discard) {
                room->sendCompulsoryTriggerLog(player, "olyongsi", true);
                if (!room->askForDiscard(player, "olyongsi", 1, 1, true, true, "@olyongsi"))
                    room->loseHp(HpLostStruct(player, getEffectiveAmount(ctx), "olyongsi", player));
            }
        }
        return false;
    }
};

class OlJixi : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx); }
    OlJixi() : TriggerSkillV2("oljixi")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake; waked_skills = "wangzun";
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    int consecutiveFinishes(Room *room, const ServerPlayer *owner) const
    {
        QVariantMap lastLoss;
        QVariantMap filter{{"kind", "hp_lost"}, {"player", owner->objectName()}, {"limit", 128}};
        do {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return -1;
            for (const QVariant &item : page.value("items").toList()) {
                const QVariantMap fact = item.toMap();
                if (fact.value("id").toLongLong() > lastLoss.value("id").toLongLong()) lastLoss = fact;
            }
            if (!page.value("has_more").toBool()) break;
            filter["watermark"] = page.value("watermark"); filter["after"] = page.value("next_after");
        } while (true);
        const qint64 lastId = lastLoss.value("id").toLongLong();
        const qint64 lossPhase = lastLoss.value("phase_id").toLongLong();
        const QVariantMap lossData = lastLoss.value("data").toMap();
        filter = {{"kind", "phase"}, {"player", owner->objectName()}, {"limit", 128}};
        int count = 0;
        do {
            const QVariantMap page = room->queryHistoryEvents(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return -1;
            for (const QVariant &item : page.value("items").toList()) {
                const QVariantMap phase = item.toMap(), details = phase.value("data").toMap();
                if (details.value("phase").toInt() != Player::Finish) continue;
                const qint64 id = phase.value("id").toLongLong();
                if (id <= lastId && id != lossPhase) continue;
                if (!details.contains("entered")) return -1;
                if (!details.value("entered").toBool()) continue;
                // The phase event begins before Changing. A loss there precedes its Finish entry.
                if (id == lossPhase) {
                    if (!lossData.contains("phase_entered")) return -1;
                    if (lossData.value("phase_entered").toBool()) continue;
                }
                ++count;
            }
            if (!page.value("has_more").toBool()) return count;
            filter["watermark"] = page.value("watermark"); filter["after"] = page.value("next_after");
        } while (true);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Finish
            && (player->canWake(objectName()) || consecutiveFinishes(room, player) >= 3)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        const bool natural = consecutiveFinishes(room, ctx.owner) >= 3;
        ctx.extra_data = natural;
        return natural || ctx.owner->canWake(objectName());
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx); return true;
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.executionID != 0 || parseSkillName(accepted.skill_name) != objectName()
            || accepted.sourceRef != ctx.sourceRef || accepted.activationRef != ctx.activationRef) return;
        if (accepted.bypass_cost) addUsage(accepted);
        room->setPlayerMark(ctx.owner, objectName(), 1); // Presentation; the exact instance quota is authoritative.
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.extra_data.toBool()) {
            LogMessage log; log.type = "#oljixi-wake"; log.from = ctx.owner; log.arg = objectName(); room->sendLog(log);
        }
        room->doSuperLightbox(ctx.owner, objectName());
        const int amount = getEffectiveAmount(ctx);
        if (!room->changeMaxHpForAwakenSkill(target, amount, objectName())) return false;
        room->recover(target, RecoverStruct(objectName(), ctx.owner, amount));
        if (target->isDead()) return false;
        QStringList choices, lordskills;
        if (Sanguosha->getSkill("wangzun")) choices << "wangzun";
        if (ServerPlayer *lord = room->getLord())
            for (const Skill *skill : lord->getVisibleSkillList())
                if (skill->isLordSkill() && lord->hasLordSkill(skill, true)) lordskills << skill->objectName();
        if (!lordskills.isEmpty()) choices << "lordskill";
        if (choices.isEmpty()) return false;
        const QString choice = room->askForChoice(target, objectName(), choices.join("+"));
        if (choice == "wangzun") room->acquireSkillFromEffect(target, "wangzun", ctx);
        else {
            target->drawCards(2 * amount, objectName());
            for (const QString &skill : lordskills) room->acquireSkillFromEffect(target, skill, ctx);
        }
        return false;
    }
};








MobileStStandardPackage::MobileStStandardPackage()
    : Package("mobile_st_standard")
{



    General *mobile_zhangfei = new General(this, "mobile_zhangfei", "shu", 4);
    mobile_zhangfei->addSkill("tenyearpaoxiao");
    mobile_zhangfei->addSkill(new MobileLiyong);
    mobile_zhangfei->addSkill(new MobileLiyongClear);
    related_skills.insert("mobileliyong", "#mobileliyong-clear");

    General *mobile_xiahoudun = new General(this, "mobile_xiahoudun", "wei", 4);
    mobile_xiahoudun->addSkill("ganglie");
    mobile_xiahoudun->addSkill(new MobileQingjian);

    General *mobile2_yuanshu = new General(this, "mobile2_yuanshu", "qun");
    mobile2_yuanshu->addSkill(new OlYongsi);
    mobile2_yuanshu->addSkill(new OlJixi);

    addMetaObject<MobileQingjianCard>();
    addMetaObject<MobileQiangxiCard>();
    addMetaObject<MobileNiepanCard>();
    addMetaObject<MobileZaiqiCard>();
    addMetaObject<MobilePoluCard>();
    addMetaObject<MobileTiaoxinCard>();
    addMetaObject<MobileZhijianCard>();
    addMetaObject<MobileFangquanCard>();
    addMetaObject<MobileGanluCard>();
    addMetaObject<MobileJieyueCard>();
    addMetaObject<MobileAnxuCard>();
    addMetaObject<MobileGongqiCard>();
    addMetaObject<MobileZongxuanCard>();
    addMetaObject<MobileZongxuanPutCard>();
    addMetaObject<MobileJunxingCard>();
    addMetaObject<MobileMiejiCard>();
    addMetaObject<MobileMiejiDiscardCard>();
    addMetaObject<MobileXianzhenCard>();
    addMetaObject<MobileQiaoshuiCard>();
    addMetaObject<MobileZongshihCard>();
    addMetaObject<MobileDingpinCard>();
    addMetaObject<MobileShenxingCard>();
    addMetaObject<MobileBingyiCard>();
    addMetaObject<MobileJianyingCard>();
    addMetaObject<MobileYanzhuCard>();
    addMetaObject<MobileXingxueCard>();
}
ADD_PACKAGE(MobileStStandard)

MobileStWindPackage::MobileStWindPackage()
    : Package("mobile_st_wind")
{
    General *mobile_zhoutai = new General(this, "mobile_zhoutai", "wu", 4);
    mobile_zhoutai->addSkill("buqu");
    mobile_zhoutai->addSkill(new MobileFenji);



}
ADD_PACKAGE(MobileStWind)

MobileStThicketPackage::MobileStThicketPackage()
    : Package("mobile_st_thicket")
{
    General *mobile_zhurong = new General(this, "mobile_zhurong", "shu", 4, false);
    mobile_zhurong->addSkill("juxiang");
    mobile_zhurong->addSkill(new MobileLieren);

    General *mobile_menghuo = new General(this, "mobile_menghuo", "shu", 4);
    mobile_menghuo->addSkill("huoshou");
    mobile_menghuo->addSkill(new MobileZaiqi);

    General *mobile_caopi = new General(this, "mobile_caopi$", "wei", 3);
    mobile_caopi->addSkill(new MobileXingshang);
    mobile_caopi->addSkill(new MobileFangzhu);
    mobile_caopi->addSkill("songwei");

    General *mobile_sunjian = new General(this, "mobile_sunjian", "wu", 4);
    mobile_sunjian->addSkill("yinghun");
    mobile_sunjian->addSkill(new MobilePolu);

    General *mobile_dongzhuo = new General(this, "mobile_dongzhuo$", "qun", 8);
    mobile_dongzhuo->addSkill(new MobileJiuchi);
    mobile_dongzhuo->addSkill("roulin");
    mobile_dongzhuo->addSkill("benghuai");
    mobile_dongzhuo->addSkill("baonue");


}
ADD_PACKAGE(MobileStThicket)

MobileStFirePackage::MobileStFirePackage()
    : Package("mobile_st_fire")
{
    General *mobile_wolong = new General(this, "mobile_wolong", "shu", 3);
    mobile_wolong->addSkill("bazhen");
    mobile_wolong->addSkill("olhuoji");
    mobile_wolong->addSkill("olkanpo");

    General *mobile_pangtong = new General(this, "mobile_pangtong", "shu", 3);
    mobile_pangtong->addSkill("ollianhuan");
    mobile_pangtong->addSkill(new MobileNiepan);

    General *mobile_dianwei = new General(this, "mobile_dianwei", "wei", 4);
    mobile_dianwei->addSkill(new MobileQiangxi);
    mobile_dianwei->addSkill(new MobileQiangxiClear);
    mobile_dianwei->addSkill(new MobileStrengthenActiveQuota("mobileqiangxi"));
    related_skills.insert("mobileqiangxi", "#mobileqiangxi-quota");
    related_skills.insert("mobileqiangxi", "#mobileqiangxi-clear");

    General *mobile_xunyu = new General(this, "mobile_xunyu", "wei", 3);
    mobile_xunyu->addSkill("quhu");
    mobile_xunyu->addSkill(new MobileJieming);

    General *mobile_yuanshao = new General(this, "mobile_yuanshao$", "qun", 4);
    mobile_yuanshao->addSkill(new MobileLuanji);
    mobile_yuanshao->addSkill(new MobileStrengthenActiveQuota("mobileluanji"));
    related_skills.insert("mobileluanji", "#mobileluanji-quota");
    mobile_yuanshao->addSkill("xueyi");

    General *mobile_yanliangwenchou = new General(this, "mobile_yanliangwenchou", "qun", 4);
    mobile_yanliangwenchou->addSkill(new MobileShuangxiong);

}
ADD_PACKAGE(MobileStFire)

MobileStMountainPackage::MobileStMountainPackage()
    : Package("mobile_st_mountain")
{
    General *mobile_liushan = new General(this, "mobile_liushan$", "shu", 3);
    mobile_liushan->addSkill("xiangle");
    mobile_liushan->addSkill(new MobileFangquan);
    mobile_liushan->addSkill(new MobileFangquanMax);
    mobile_liushan->addSkill("ruoyu");
    related_skills.insert("mobilefangquan", "#mobilefangquan-max");

    General *mobile_jiangwei = new General(this, "mobile_jiangwei", "shu", 4);
    mobile_jiangwei->addSkill(new MobileTiaoxin);
    mobile_jiangwei->addSkill(new MobileZhiji);
    mobile_jiangwei->addRelateSkill("tenyearguanxing");

    General *mobile_dengai = new General(this, "mobile_dengai", "wei", 4);
    mobile_dengai->addSkill(new MobileTuntian);
    mobile_dengai->addSkill(new MobileTuntianJudge);
    related_skills.insert("mobiletuntian", "#mobiletuntian-judge");
    mobile_dengai->addSkill(new MobileTuntianDistance);
    mobile_dengai->addSkill("zaoxian");
    related_skills.insert("mobiletuntian", "#mobiletuntian-dist");

    General *mobile_sunce = new General(this, "mobile_sunce$", "wu", 4);
    mobile_sunce->addSkill("jiang");
    mobile_sunce->addSkill(new MobileHunzi);
    mobile_sunce->addSkill("zhiba");

    General *mobile_erzhang = new General(this, "mobile_erzhang", "wu", 3);
    mobile_erzhang->addSkill(new MobileZhijian);
    mobile_erzhang->addSkill("guzheng");

    General *mobile_caiwenji = new General(this, "mobile_caiwenji", "qun", 3, false);
    mobile_caiwenji->addSkill(new MobileBeige);
    mobile_caiwenji->addSkill("duanchang");

    MigrateToMobileStMountain(this);
}
ADD_PACKAGE(MobileStMountain)

MobileStYJ2011Package::MobileStYJ2011Package()
    : Package("mobile_st_yj2011")
{
    General *mobile_caozhi = new General(this, "mobile_caozhi", "wei", 3);
    mobile_caozhi->addSkill("luoying");
    mobile_caozhi->addSkill(new MobileJiushi);
    mobile_caozhi->addSkill(new MobileChengzhang);

    General *mobile_yujin = new General(this, "mobile_yujin", "wei", 4);
    mobile_yujin->addSkill(new MobileJieyue);

    General *mobile_xusheng = new General(this, "mobile_xusheng", "wu", 4);
    mobile_xusheng->addSkill(new MobilePojun);

    General *mobile_wuguotai = new General(this, "mobile_wuguotai", "wu", 3, false);
    mobile_wuguotai->addSkill(new MobileGanlu);
    mobile_wuguotai->addSkill("buyi");

    General *mobile_lingtong = new General(this, "mobile_lingtong", "wu", 4);
    mobile_lingtong->addSkill(new MobileXuanfeng);

    General *mobile_gaoshun = new General(this, "mobile_gaoshun", "qun", 4);
    mobile_gaoshun->addSkill(new MobileXianzhen);
    mobile_gaoshun->addSkill(new MobileXianzhenClear);
    mobile_gaoshun->addSkill(new MobileXianzhenTargetMod);
    mobile_gaoshun->addSkill(new MobileJinjiu);
    mobile_gaoshun->addSkill(new MobileJinjiuLimit);
    mobile_gaoshun->addSkill(new MobileJinjiuEffect);
    related_skills.insert("mobilexianzhen", "#mobilexianzhen-clear");
    related_skills.insert("mobilexianzhen", "#mobilexianzhen-target");
    related_skills.insert("mobilejinjiu", "#mobilejinjiu-limit");
    related_skills.insert("mobilejinjiu", "#mobilejinjiu");

    MigrateToMobileStYJ2011(this);
}
ADD_PACKAGE(MobileStYJ2011)

MobileStYJ2012Package::MobileStYJ2012Package()
    : Package("mobile_st_yj2012")
{
    General *mobile_liaohua = new General(this, "mobile_liaohua", "shu", 4);
    mobile_liaohua->addSkill(new MobileDangxian);
    mobile_liaohua->addSkill(new MobileFuli);

    General *mobile_zhonghui = new General(this, "mobile_zhonghui", "wei", 4);
    mobile_zhonghui->addSkill(new MobileQuanji);
    mobile_zhonghui->addSkill(new MobileQuanjiKeep);
    mobile_zhonghui->addSkill("zili");
    mobile_zhonghui->addRelateSkill("paiyi");
    related_skills.insert("mobilequanji", "#mobilequanji");

    General *mobile_bulianshi = new General(this, "mobile_bulianshi", "wu", 3, false);
    mobile_bulianshi->addSkill(new MobileAnxu);
    mobile_bulianshi->addSkill("zhuiyi");

    General *mobile_chengpu = new General(this, "mobile_chengpu", "wu", 4);
    mobile_chengpu->addSkill(new MobileLihuo);
    mobile_chengpu->addSkill("chunlao");

    General *mobile_handang = new General(this, "mobile_handang", "wu", 4);
    mobile_handang->addSkill(new MobileGongqi);
    mobile_handang->addSkill(new MobileGongqiAttack);
    mobile_handang->addSkill("jiefan");
    related_skills.insert("mobilegongqi", "#mobilegongqi-attack");

    General *mobile_gongsunzan = new General(this, "mobile_gongsunzan", "qun", 4);
    mobile_gongsunzan->addSkill(new MobileYicong);
    mobile_gongsunzan->addSkill("qiaomeng");
    skills << new MobileBenxiDistance;

    General *mobile_liubiao = new General(this, "mobile_liubiao", "qun", 3);
    mobile_liubiao->addSkill("zishou");
    mobile_liubiao->addSkill("#zishou");
    mobile_liubiao->addSkill(new MobileZongshi);
    mobile_liubiao->addSkill(new MobileZongshiKeep);
    related_skills.insert("mobilezongshi", "#mobilezongshi-keep");

    MigrateToMobileStYJ2012(this);
}
ADD_PACKAGE(MobileStYJ2012)

MobileStYJ2013Package::MobileStYJ2013Package()
    : Package("mobile_st_yj2013")
{
    General *mobile_jianyong = new General(this, "mobile_jianyong", "shu", 3);
    mobile_jianyong->addSkill(new MobileQiaoshui);
    mobile_jianyong->addSkill(new MobileQiaoshuiTargetMod);
    mobile_jianyong->addSkill(new MobileZongshih);
    related_skills.insert("mobileqiaoshui", "#mobileqiaoshui-target");

    General *mobile_manchong = new General(this, "mobile_manchong", "wei", 3);
    mobile_manchong->addSkill(new MobileJunxing);
    mobile_manchong->addSkill("yuce");

    General *mobile_guohuai = new General(this, "mobile_guohuai", "wei", 4);
    mobile_guohuai->addSkill(new MobileJingce);

    General *mobile_zhuran = new General(this, "mobile_zhuran", "wu", 4);
    mobile_zhuran->addSkill(new MobileDanshou);

    General *mobile_panzhangmazhong = new General(this, "mobile_panzhangmazhong", "wu", 4);
    mobile_panzhangmazhong->addSkill(new MobileDuodao);
    mobile_panzhangmazhong->addSkill(new MobileAnjian);

    General *mobile_yufan = new General(this, "mobile_yufan", "wu", 3);
    mobile_yufan->addSkill(new MobileZongxuan);
    mobile_yufan->addSkill("zhiyan");

    General *mobile_liru = new General(this, "mobile_liru", "qun", 3);
    mobile_liru->addSkill(new MobileJuece);
    mobile_liru->addSkill(new MobileMieji);
    mobile_liru->addSkill("fencheng");

    General *mobile_fuhuanghou = new General(this, "mobile_fuhuanghou", "qun", 3, false);
    mobile_fuhuanghou->addSkill(new MobileZhuikong);
    mobile_fuhuanghou->addSkill(new MobileZhuikongProhibit);
    mobile_fuhuanghou->addSkill(new MobileQiuyuan);
    related_skills.insert("mobilezhuikong", "#mobilezhuikong");






}
ADD_PACKAGE(MobileStYJ2013)

MobileStYJ2014Package::MobileStYJ2014Package()
    : Package("mobile_st_yj2014")
{
    General *mobile_wuyi = new General(this, "mobile_wuyi", "shu", 4);
    mobile_wuyi->addSkill(new MobileBenxi);

    General *mobile_zhoucang = new General(this, "mobile_zhoucang", "shu", 4);
    mobile_zhoucang->addSkill(new MobileZhongyong);
    mobile_zhoucang->addSkill(new MobileZhongyongEffect);
    mobile_zhoucang->addSkill(new MobileZhongyongRemove);
    related_skills.insert("mobilezhongyong", "#mobilezhongyong-effect");
    related_skills.insert("mobilezhongyong", "#mobilezhongyong-remove");

    General *mobile_caozhen = new General(this, "mobile_caozhen", "wei", 4);
    mobile_caozhen->addSkill(new MobileSidi);
    addMetaObject<MobileSidiCard>();

    General *mobile_chenqun = new General(this, "mobile_chenqun", "wei", 3);
    mobile_chenqun->addSkill(new MobileDingpin);
    mobile_chenqun->addSkill("faen");

    General *mobile_guyong = new General(this, "mobile_guyong", "wu", 3);
    mobile_guyong->addSkill(new MobileShenxing);
    mobile_guyong->addSkill(new MobileBingyi);

    General *mobile_zhuhuan = new General(this, "mobile_zhuhuan", "wu", 4);
    mobile_zhuhuan->addSkill("fenli");
    mobile_zhuhuan->addSkill(new MobilePingkou);

    General *mobile_caifuren = new General(this, "mobile_caifuren", "qun", 3, false);
    mobile_caifuren->addSkill(new MobileQieting);
    mobile_caifuren->addSkill("xianzhou");

    General *mobile_jushou = new General(this, "mobile_jushou", "qun", 3);
    mobile_jushou->addSkill(new MobileJianying);
    mobile_jushou->addSkill(new MobileJianyingTargetMod);
    mobile_jushou->addSkill("shibei");
    related_skills.insert("mobilejianying", "#mobilejianying-target");


}
ADD_PACKAGE(MobileStYJ2014)

MobileStYJ2015Package::MobileStYJ2015Package()
    : Package("mobile_st_yj2015")
{
    General *mobile_zhangyi = new General(this, "mobile_zhangyi", "shu", 4);
    mobile_zhangyi->addSkill(new MobileFurong);
    mobile_zhangyi->addSkill("shizhi");
    addMetaObject<MobileFurongCard>();

    General *mobile_caoxiu = new General(this, "mobile_caoxiu", "wei", 4);
    mobile_caoxiu->addSkill("qianju");
    mobile_caoxiu->addSkill(new MobileQingxi);

    General *mobile_sunxiu = new General(this, "mobile_sunxiu$", "wu", 3);
    mobile_sunxiu->addSkill(new MobileYanzhu);
    mobile_sunxiu->addSkill(new MobileXingxue);
    mobile_sunxiu->addSkill("zhaofu");

    General *mobile_quancong = new General(this, "mobile_quancong", "wu", 4);
    mobile_quancong->addSkill(new MobileYaoming);
    addMetaObject<MobileYaomingCard>();

    General *mobile_zhuzhi = new General(this, "mobile_zhuzhi", "wu", 4);
    mobile_zhuzhi->addSkill(new MobileAnguo);


}
ADD_PACKAGE(MobileStYJ2015)
