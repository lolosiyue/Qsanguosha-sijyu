#include "inovation.h"
#include "serverplayer.h"
#include "room.h"
#include "skill.h"
#include "maneuvering.h"
#include "clientplayer.h"
#include "engine.h"
#include "client.h"
#include "exppattern.h"
#include "roomthread.h"
#include "wrapped-card.h"
#include "json.h"
#include "settings.h"
#include "skill-instance-utils.h"
#include "card-lifetime-manager.h"
#include <QScopeGuard>

//mapo tofu
MapoTofu::MapoTofu(Card::Suit suit, int number)
    : BasicCard(suit, number)
{
    setObjectName("mapo_tofu");
}

QString MapoTofu::getSubtype() const
{
    return "food_card";
}

bool MapoTofu::IsAvailable(const Player *player, const Card *tofu)
{
    MapoTofu *newanaleptic = new MapoTofu(Card::NoSuit, 0);
    CardLifetimeLease lease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(newanaleptic));
    newanaleptic->deleteLater();
#define THIS_TOFU (tofu == NULL ? newanaleptic : tofu)
    if (player->isCardLimited(THIS_TOFU, Card::MethodUse) || player->isProhibited(player, THIS_TOFU))
        return false;

    return player->usedTimes("MapoTofu") <= Sanguosha->correctCardTarget(TargetModSkill::Residue, player, THIS_TOFU);
#undef THIS_TOFU
}

bool MapoTofu::isAvailable(const Player *player) const
{

    return IsAvailable(player, this) && BasicCard::isAvailable(player);
}

bool MapoTofu::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.length() == 0 && Self->distanceTo(to_select) <= 1 && to_select->getMark("mtUsed") == 0;
}

void MapoTofu::onUse(Room *room, CardUseStruct &card_use) const
{
    CardUseStruct use = card_use;
    if (use.to.isEmpty())
        use.to << use.from;
    BasicCard::onUse(room, use);
}

void MapoTofu::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    if (targets.isEmpty())
        targets << source;
    BasicCard::use(room, source, targets);
}

void MapoTofu::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    //room->setEmotion(effect.to, "mapo_tofu");//TODO

    DamageStruct damage;
    damage.to = effect.to;
    damage.damage = effect.to->getHp() > 0 ? effect.to->getHp() - 1: 0;
    int toDamge = damage.damage;
    // damage.chain = false;
    damage.chain = true;
    damage.nature = DamageStruct::Fire;
    effect.to->getRoom()->damage(damage);
    LogMessage log;
    log.type = "#MapoTofuUse";
    log.from = effect.from;
    log.to << effect.to;
    log.arg = objectName();
    room->sendLog(log);
    effect.to->setMark("mtUsed", toDamge + 1);
}

// Applied receipts share the native pair projection; preserve a pre-existing external pair.
static void rememberInovationAkarin(Room *room, ServerPlayer *hidden, ServerPlayer *observer)
{
    QVariantMap baseline = room->getTag("InovationAkarinBaseline").toMap();
    const QString key = hidden->objectName() + ":" + observer->objectName();
    if (!baseline.contains(key)) baseline.insert(key, room->isAkarin(hidden, observer));
    room->setTag("InovationAkarinBaseline", baseline);
}

static void releaseInovationAkarin(Room *room, ServerPlayer *hidden, ServerPlayer *observer)
{
    if (!hidden || !observer) return;
    if (!hidden->getTag("ToumingEffects").toList().isEmpty()) return;
    for (const QVariant &value : room->getTag("HuanxingEffects").toList()) {
        const QVariantMap receipt = value.toMap();
        if (receipt.value("owner").toString() == hidden->objectName() && receipt.value("target").toString() == observer->objectName()) return;
    }
    for (const QVariant &value : room->getTag("JianshiEffects").toList()) {
        const QVariantMap receipt = value.toMap();
        if (receipt.value("target").toString() == hidden->objectName() && receipt.value("observers").toStringList().contains(observer->objectName())) return;
    }
    QVariantMap baseline = room->getTag("InovationAkarinBaseline").toMap();
    const QString key = hidden->objectName() + ":" + observer->objectName();
    if (baseline.contains(key) && !baseline.value(key).toBool()) room->removeAkarinEffect(hidden, observer);
    baseline.remove(key); room->setTag("InovationAkarinBaseline", baseline);
}

//akarin
class SE_Touming : public TriggerSkillV2
{
public:
    SE_Touming() : TriggerSkillV2("inovation_SE_Touming")
    { events << EventPhaseStart << EventPhaseEnd << Death << EventLoseSkill; global = true; }
    static void clear(Room *room, ServerPlayer *owner, bool all)
    {
        if (!owner) return;
        QVariantList retained;
        for (const QVariant &value : owner->getTag("ToumingEffects").toList())
            if (!all && owner->hasSkillInstance("inovation_SE_Touming", value.toMap().value("instance").toInt())) retained << value;
        owner->setTag("ToumingEffects", retained);
        if (retained.isEmpty()) { for (ServerPlayer *observer : room->getAllPlayers()) releaseInovationAkarin(room, owner, observer); room->setPlayerMark(owner, "touming_used", 0); }
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        if (!actor) return false;
        if ((event == EventPhaseStart && actor->getPhase() == Player::RoundStart)
            || (event == Death && data.value<DeathStruct>().who == actor)) clear(room, actor, true);
        else if (event == EventLoseSkill) clear(room, actor, false);
        if (event == EventPhaseStart && actor->getPhase() == Player::Discard)
            for (int id : actor->getValidSkillInstanceIds(objectName()))
                actor->setSkillInstanceStateValue(objectName(), id, "discard_hand", actor->getHandcardNum());
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseEnd && actor && actor->isAlive() && actor->getPhase() == Player::Discard)
            for (int id : actor->getValidSkillInstanceIds(objectName())) {
                const QVariant before = actor->getSkillInstanceStateValue(objectName(), id, "discard_hand");
                if (before.isValid() && actor->getHandcardNum() == before.toInt())
                    result[actor] << SkillInstanceUtils::formatName(objectName(), id);
            }
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!ctx.owner || !ctx.owner->askForSkillInvoke(this)) return false; ctx.targets << ctx.invoker; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner) return false;
        QVariantList receipts = target->getTag("ToumingEffects").toList();
        receipts << QVariantMap{{"instance", ctx.activationRef.key.instanceID}, {"source_owner", ctx.sourceRef.ownerObjectName},
            {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
        target->setTag("ToumingEffects", receipts);
        room->broadcastSkillInvoke(objectName()); room->doLightbox("inovation_SE_Touming$", 1500);
        for (ServerPlayer *observer : room->getAllPlayers()) rememberInovationAkarin(room, target, observer);
        room->akarinPlayer(target); room->setPlayerMark(target, "touming_used", 1);
        target->drawCards((room->getAlivePlayers().length() + 1) / 2 * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class SE_ToumingClear : public DetachEffectSkill
{
public:
    SE_ToumingClear() : DetachEffectSkill("inovation_SE_Touming") {}
    void onSkillDetached(Room *room, ServerPlayer *player) const override { SE_Touming::clear(room, player, false); }
};
class SE_Tuanzi : public TriggerSkillV2
{
public:
    SE_Tuanzi() : TriggerSkillV2("inovation_SE_Tuanzi") { events << CardUsed; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return actor && actor->isAlive() && actor->hasSkill(objectName()) && actor->getPhase() == Player::Play
            && use.card && ((use.card->isKindOf("TrickCard") && use.card->isBlack()) || use.card->isKindOf("BasicCard"))
            ? TriggerList{{actor, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false; ctx.targets << ctx.invoker; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const Card *card = ctx.original_data->value<CardUseStruct>().card;
        if (!card) return false;
        QList<int> ids = card->isVirtualCard() ? card->getSubcards() : QList<int>() << card->getEffectiveId();
        for (int i = ids.size() - 1; i >= 0; --i)
            if (room->getCardPlace(ids.at(i)) != Player::PlaceTable) ids.removeAt(i);
        if (ids.isEmpty()) return false;
        room->broadcastSkillInvoke(objectName());
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::DrawPile,
            CardMoveReason(CardMoveReason::S_REASON_PUT, ctx.owner->objectName(), objectName(), QString())), true);
        return false;
    }
};
class Huanxing : public TriggerSkillV2
{
public:
    Huanxing() : TriggerSkillV2("inovation_huanxing")
    {
        events << EventSkillInvoking << CardUsed << EventPhaseEnd << TurnStart << TrickCardCanceling << SlashProceed << Death;
        global = true;
    }
    bool pay(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != CardUsed) return true;
        if (!ctx.owner || ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "used").toBool()) return false;
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "used", true); return true;
    }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        return ctx.current_event != CardUsed || (ctx.owner
            && !ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "used").toBool());
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext active = data.value<SkillContext>();
            if (active.bypass_cost && active.skill_name == objectName() && active.activationRef.key.skillName == objectName() && active.owner && active.choice == "activate")
                active.owner->setSkillInstanceStateValue(objectName(), active.activationRef.key.instanceID, "used", true);
            return false;
        }

        if (!actor) return false;
        if (event == TurnStart)
            for (int id : actor->getSkillInstanceIds(objectName()))
                actor->setSkillInstanceStateValue(objectName(), id, "used", false);
        const bool finish = event == EventPhaseEnd && actor->getPhase() == Player::Finish;
        const bool death = event == Death && data.value<DeathStruct>().who == actor;
        if (!finish && !death) return false;
        QVariantList kept, removed;
        for (const QVariant &value : room->getTag("HuanxingEffects").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("owner").toString() == actor->objectName()
                || (death && receipt.value("target").toString() == actor->objectName())) removed << value;
            else kept << value;
        }
        room->setTag("HuanxingEffects", kept);
        for (const QVariant &value : removed) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ServerPlayer *target = room->findPlayerByObjectName(receipt.value("target").toString(), true);
            if (owner && target) releaseInovationAkarin(room, owner, target);
        }
        for (ServerPlayer *target : room->getAllPlayers()) {
            int count = 0;
            for (const QVariant &value : kept) if (value.toMap().value("target").toString() == target->objectName()) ++count;
            room->setPlayerMark(target, "@inovation_huanxing_target", count);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from && use.to.size() == 1 && use.to.first() != use.from && use.to.first()->isAlive()
                && use.to.first()->hasSkill(objectName())) result[use.to.first()] << objectName();
            return result;
        }
        return result;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != TrickCardCanceling && event != SlashProceed) return false;
        ServerPlayer *owner = nullptr, *target = nullptr;
        if (event == TrickCardCanceling) { const CardEffectStruct effect = data.value<CardEffectStruct>(); owner = effect.from; target = effect.to; }
        else { const SlashEffectStruct effect = data.value<SlashEffectStruct>(); owner = effect.from; target = effect.to; }
        if (!owner || !owner->isAlive() || !target) return true;
        for (const QVariant &value : room->getTag("HuanxingEffects").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("owner").toString() != owner->objectName() || receipt.value("target").toString() != target->objectName()) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = ctx.initiator = ctx.invoker = owner;
            ctx.instanceID = receipt.value("instance").toInt(); ctx.current_event = event; ctx.original_data = &data;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.extra_data = receipt; ctx.targets << target; ctx.is_forced = true; ctx.instanceID = receipt.value("serial").toInt(); ctx.trigger_count = 0; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx)
            : ctx.owner && ctx.owner->isAlive() && room->getTag("HuanxingEffects").toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        if (event == CardUsed) {
            if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
            ctx.choice = "activate"; ctx.targets << ctx.original_data->value<CardUseStruct>().from;
        } else if (ctx.targets.isEmpty()) return false;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner || !target || !target->isAlive() || !ctx.original_data) return false;
        if (event == CardUsed) {
            QVariantList receipts = room->getTag("HuanxingEffects").toList();
            const qint64 serial = room->getTag("HuanxingEffectsSequence").toLongLong()+1; room->setTag("HuanxingEffectsSequence",serial);
            receipts << QVariantMap{{"serial",serial},{"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID},
                {"target", target->objectName()}, {"source_owner", ctx.sourceRef.ownerObjectName},
                {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
            room->setTag("HuanxingEffects", receipts);
            room->addPlayerMark(target, "@inovation_huanxing_target");
            room->broadcastSkillInvoke(objectName(), qsanRandomBounded(4) + 1);
            room->doLightbox("inovation_huanxing$", 300);
            rememberInovationAkarin(room, ctx.owner, target); room->akarinPlayer(ctx.owner, target);
        } else {
            LogMessage log; log.type = "#inovation_huanxing_effect"; log.from = target;
            if (event == TrickCardCanceling) {
                const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
                if (!effect.card) return false;
                log.arg = effect.card->objectName(); room->sendLog(log);
                room->broadcastSkillInvoke(objectName(), 6);
            } else {
                const SlashEffectStruct effect = ctx.original_data->value<SlashEffectStruct>();
                if (!effect.slash) return false;
                log.arg = effect.slash->objectName(); room->sendLog(log);
                room->broadcastSkillInvoke(objectName(), 5);
                room->slashResult(effect, nullptr);
            }
        }
        return event != CardUsed;
    }
};
class Fushang : public TriggerSkillV2
{
public:
    Fushang() : TriggerSkillV2("fushang")
    {
        events << EventSkillEffectFinished << Damaged << EventPhaseStart << EnterDying << Death;
        global = true;
    }

    static void project(Room *room, ServerPlayer *target)
    {
        int total = 0, remaining = 0;
        for (const QVariant &value : room->getTag("FushangEffects").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("target").toString() != target->objectName()) continue;
            total += receipt.value("amount").toInt();
            remaining = qMax(remaining, receipt.value("remaining").toInt());
        }
        room->setPlayerMark(target, "@fushang", total);
        room->setPlayerMark(target, "@fushang_time", remaining);
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext accepted = data.value<SkillContext>();
            if (accepted.skill_name == objectName() && !accepted.activationRef.isValid() && accepted.invoker) {
                QVariantList receipts = room->getTag("FushangEffects").toList();
                receipts.removeOne(accepted.extra_data); room->setTag("FushangEffects", receipts);
                project(room, accepted.invoker);
            }
            return false;
        }

        if (!actor || event == Damaged) return false;
        if (event == Death && data.value<DeathStruct>().who != actor) return false;
        const bool tick = (event == EventPhaseStart && actor->getPhase() == Player::RoundStart)
            || (event == EnterDying && actor->containsTrick("key_trick"));
        if (!tick && event != Death) return false;
        QVariantList receipts;
        for (const QVariant &value : room->getTag("FushangEffects").toList()) {
            QVariantMap receipt = value.toMap();
            if (receipt.value("target").toString() == actor->objectName()) {
                if (event == Death) continue;
                receipt["remaining"] = qMax(0, receipt.value("remaining").toInt() - 1);
            }
            receipts << receipt;
        }
        room->setTag("FushangEffects", receipts);
        project(room, actor);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (event != Damaged) return result;
        ServerPlayer *target = data.value<DamageStruct>().to;
        if (!target || !target->isAlive()) return result;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event == Damaged) return false;
        if (!actor || !actor->isAlive() || event == Death) return true;
        if (!(event == EventPhaseStart && actor->getPhase() == Player::RoundStart)
            && !(event == EnterDying && actor->containsTrick("key_trick"))) return true;
        for (const QVariant &value : room->getTag("FushangEffects").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("target").toString() != actor->objectName() || receipt.value("remaining").toInt() != 0) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = ctx.owner;
            ctx.invoker = actor;
            ctx.instanceID = receipt.value("instance").toInt();
            ctx.amount = receipt.value("amount").toInt();
            ctx.extra_data = receipt;
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.targets << actor;
            ctx.is_forced = true; contexts << ctx;
        }
        return true;
    }

    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        return ctx.invoker && ctx.invoker->isAlive() && ctx.sourceRef.isValid()
            && room->getTag("FushangEffects").toList().contains(ctx.extra_data);
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != Damaged) return true;
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << ctx.original_data->value<DamageStruct>().to;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        QVariantList receipts = room->getTag("FushangEffects").toList();
        if (event == Damaged) {
            int amount = getEffectiveAmount(ctx);
            QVariantList updated;
            for (const QVariant &value : receipts) {
                QVariantMap receipt = value.toMap();
                if (receipt.value("target").toString() == target->objectName()) {
                    // A new treatment refreshes the recipient's shared two-tick countdown.
                    receipt["remaining"] = 2;
                    if (receipt.value("owner").toString() == ctx.activationRef.ownerObjectName
                        && receipt.value("instance").toInt() == ctx.activationRef.key.instanceID) {
                        amount += receipt.value("amount").toInt();
                        continue;
                    }
                }
                updated << receipt;
            }
            updated << QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
                {"source_instance", ctx.sourceRef.key.instanceID}, {"target", target->objectName()},
                {"remaining", 2}, {"amount", amount}};
            room->setTag("FushangEffects", updated);
            room->broadcastSkillInvoke(objectName());
            project(room, target);
        } else {
            // Consume before recovery: nested events cannot replay an expired receipt.
            receipts.removeOne(ctx.extra_data);
            room->setTag("FushangEffects", receipts);
            project(room, target);
            room->broadcastSkillInvoke(objectName(), 1);
            room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
        }
        return false;
    }
};
KeyTrick::KeyTrick(Card::Suit suit, int number)
    : DelayedTrick(suit, number)
{
    setObjectName("key_trick");
    mute = true;
    handling_method = Card::MethodNone;
}

bool KeyTrick::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    int count=0;
    int key=0;
    QList<const Player *> sib = Self->getAliveSiblings();
    sib << Self;
    foreach (const Player *p, sib){
        if(p->hasClub()&&p->getClubName()=="yanjubu"){
             count=count+1;
        }
    }
    foreach (const Card *c, to_select->getJudgingArea()){
        if(c->objectName()==objectName()){
             key=key+1;
        }
    }
    if (targets.isEmpty() && (key==0 ||(key<count && to_select->hasClub()&&to_select->getClubName()=="yanjubu")))
        return true;
    /*if (!targets.isEmpty() || to_select->containsTrick(objectName()))
        return false;*/
    return false;
}

void KeyTrick::takeEffect(ServerPlayer *) const
{
}

void KeyTrick::onEffect(CardEffectStruct &) const
{
}

void KeyTrick::onNullified(ServerPlayer *player) const
{
    player->getRoom()->throwCard(this, NULL, player);
}

class GuangyuViewAsSkill : public ViewAsSkillV2
{
public:
    GuangyuViewAsSkill() : ViewAsSkillV2("guangyu", 1)
    {
        setResponsePattern("@@guangyu");
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@guangyu";
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || !request.selectedCardIds.isEmpty()) return false;
        return request.initiator->property("guangyu").toString().split("+").contains(QString::number(card->getEffectiveId()));
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest prefix = request; prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "KeyTrick"; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *original = Sanguosha->getCard(request.selectedCardIds.first());
        if (!original) return nullptr;
        KeyTrick *gy = new KeyTrick(original->getSuit(), original->getNumber());
        gy->addSubcard(original);
        gy->setSkillName(objectName());
        return gy;
    }
};

class Guangyu : public TriggerSkillV2
{
public:
    Guangyu() : TriggerSkillV2("guangyu") { events << BeforeCardsMove; view_as_skill = new GuangyuViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!actor || !actor->isAlive() || !actor->hasSkill(objectName()) || move.from != actor
            || move.to_place != Player::DiscardPile
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) return {};
        for (int id : move.card_ids) if (Sanguosha->getCard(id)->isRed()) return {{actor, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QList<int> ids;
        for (int id : move.card_ids)
            if (room->getCardOwner(id) == ctx.owner && Sanguosha->getCard(id)->isRed()) ids << id;
        const QVariant previous = ctx.owner->property("guangyu");
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int oldSelector = ctx.owner->getMark(selector);
        auto restore = qScopeGuard([&] {
            room->setPlayerProperty(ctx.owner, "guangyu", previous);
            room->setPlayerMark(ctx.owner, selector, oldSelector);
        });
        room->setPlayerMark(ctx.owner, selector, ctx.activationRef.key.instanceID);
        while (!ids.isEmpty() && ctx.owner->isAlive()) {
            room->setPlayerProperty(ctx.owner, "guangyu", ListI2S(ids).join("+"));
            const Card *used = room->askForUseCard(ctx.owner, "@@guangyu", "@guangyu-use");
            if (!used) break;
            const QList<int> materials = used->isVirtualCard() ? used->getSubcards() : QList<int>() << used->getEffectiveId();
            if (materials.isEmpty()) break;
            // The accepted conversion consumes only its own original discard material.
            move.removeCardIds(materials);
            *ctx.original_data = QVariant::fromValue(move);
            for (int id : materials) ids.removeAll(id);
            const int voice = qMax(1, room->getTag("nagisa_voice").toInt());
            room->broadcastSkillInvoke(objectName(), qMin(40, voice));
            room->setTag("nagisa_voice", voice + 1);
        }
        return false; // The nested ordinary-card use is the entire action.
    }
};

class GuangyuTrigger : public TriggerSkillV2
{
public:
    GuangyuTrigger() : TriggerSkillV2("#guangyu-trigger") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *actor, QVariant &) const override
    {
        TriggerList result;
        if (!actor || actor->getPhase() != Player::Judge || !actor->containsTrick("key_trick")) return result;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner->hasSkill("guangyu")) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *actor, SkillContext &ctx) const override
    {
        if (!ctx.owner || !actor || !ctx.owner->askForSkillInvoke("guangyu")) return false;
        ctx.targets << actor;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !target->containsTrick("key_trick")) return false;
        const int voice = qMax(1, room->getTag("nagisa_voice").toInt());
        room->broadcastSkillInvoke("guangyu", voice > 40 ? qsanRandomBounded(9) + 31 : voice);
        room->setTag("nagisa_voice", voice + 1);
        room->doLightbox("guangyu$", 800);
        DummyCard cards;
        for (const Card *card : target->getJudgingArea()) cards.addSubcard(card);
        if (cards.subcardsLength() > 0) room->obtainCard(target, &cards);
        return false;
    }
};
class Xiyuan : public TriggerSkillV2
{
public:
    Xiyuan() : TriggerSkillV2("xiyuan") { events << EventSkillInvoking << Death; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext active = data.value<SkillContext>();
            if (active.bypass_cost && active.skill_name == objectName() && active.activationRef.key.skillName == objectName()) addUsage(active);
        }
        return false;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {}; return actor && data.value<DeathStruct>().who == actor && actor->hasSkill(objectName())
        ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || room->getOtherPlayers(ctx.owner).isEmpty() || !ctx.owner->askForSkillInvoke(this)) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName());
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { Q_UNUSED(ctx); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        room->broadcastSkillInvoke(objectName());
        room->doLightbox("xiyuan$", 3000);
        room->changeHero(target, "inovation_Ushio", false, true, true, true);
        LogMessage log; log.type = "#XiyuanChangeHero"; log.from = ctx.owner;
        log.to << target; log.arg = objectName(); room->sendLog(log);
        return false;
    }
};

class Chengmeng : public TriggerSkillV2
{
public:
    Chengmeng() : TriggerSkillV2("chengmeng")
    { frequency = Club; club_name = "yanjubu"; events << EventSkillInvoking << CardsMoveOneTime << TurnStart; }
    bool pay(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        Q_UNUSED(event);
        if (!ctx.owner || ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "used").toBool()) return false;
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "used", true); return true;
    }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    { return ctx.owner && !ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "used").toBool(); }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext active = data.value<SkillContext>();
            if (active.bypass_cost && active.skill_name == objectName() && active.activationRef.key.skillName == objectName() && active.owner)
                active.owner->setSkillInstanceStateValue(objectName(), active.activationRef.key.instanceID, "used", true);
            return false;
        }

        if (event == TurnStart && actor)
            for (int id : actor->getSkillInstanceIds(objectName()))
                actor->setSkillInstanceStateValue(objectName(), id, "used", false);
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (event != CardsMoveOneTime || !room->getCurrent()
            || room->getCurrent()->getPhase() == Player::Draw || room->getCurrent()->getPhase() == Player::NotActive) return result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!move.to || !move.to->isAlive() || move.to_place != Player::PlaceHand || move.to->hasClub()) return result;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner != move.to && owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        const Player *recipient = ctx.original_data->value<CardsMoveOneTimeStruct>().to;
        ServerPlayer *target = recipient ? room->findPlayerByObjectName(recipient->objectName()) : nullptr;
        if (!target || target->hasClub()) return false;
        ctx.targets << target;
        return true;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { Q_UNUSED(ctx); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || target->hasClub()) return false;
        if (room->askForChoice(target, objectName(), "chengmeng_accept+cancel", QVariant::fromValue(ctx.owner)) == "chengmeng_accept")
            target->addClub("yanjubu");
        else {
            LogMessage log; log.type = "$refuse_club"; log.from = target; log.arg = "yanjubu"; room->sendLog(log);
        }
        return false;
    }
};
class Dingxin : public TriggerSkillV2
{
public:
    Dingxin() : TriggerSkillV2("dingxin") { events << EventPhaseStart << Dying; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        TriggerList result;
        if (!actor || !actor->isAlive() || !actor->hasSkill(objectName())) return result;
        if (event == EventPhaseStart && actor->getPhase() == Player::RoundStart) result[actor] << objectName();
        else if (event == Dying && data.value<DyingStruct>().who == actor)
            for (int id : actor->getValidSkillInstanceIds(objectName()))
                if (actor->getSkillInstanceStateValue(objectName(), id, "losing_hp").toBool())
                    result[actor] << SkillInstanceUtils::formatName(objectName(), id);
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != Dying) { ctx.targets << ctx.invoker; return true; }
        ServerPlayer *nagisa = nullptr;
        for (ServerPlayer *player : room->getAllPlayers())
            if (!player->isAlive() && (player->getGeneralName() == "inovation_Nagisa"
                || player->getGeneral2Name() == "inovation_Nagisa")) { nagisa = player; break; }
        ctx.choice = nagisa ? room->askForChoice(ctx.owner, objectName(), "dingxin_recover+dingxin_revive", *ctx.original_data)
                            : "dingxin_recover";
        ctx.targets << (ctx.choice == "dingxin_revive" ? nagisa : ctx.owner);
        return true;
    }
    bool allowsDeadTarget(const SkillContext &ctx, const ServerPlayer *target) const override
    {
        return target && !target->isAlive() && ctx.choice == "dingxin_revive"
            && ctx.targets.contains(const_cast<ServerPlayer *>(target))
            && (target->getGeneralName() == "inovation_Nagisa" || target->getGeneral2Name() == "inovation_Nagisa");
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target) return false;
        if (event == EventPhaseStart) {
            ServerPlayer *owner = target;
            const int num = qMax(1, room->getTag("nagisa_voice").toInt());
            if (owner->getHp() > 1) {
                room->broadcastSkillInvoke(objectName(), num > 16 ? qsanRandomBounded(5) + 12 : num);
                room->setTag("nagisa_voice", num + 1);
            }
            const QVariant previous = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "losing_hp");
            auto restore = qScopeGuard([&] {
                if (owner->hasSkillInstance(objectName(), ctx.instanceID))
                    owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "losing_hp", previous);
            });
            owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "losing_hp", true);
            room->loseHp(owner, getEffectiveAmount(ctx));
            return false;
        }
        room->doLightbox("dingxin$", 2000);
        LogMessage log; log.from = ctx.owner;
        if (ctx.choice == "dingxin_revive") {
            if (target->isAlive()) return false;
            room->broadcastSkillInvoke(objectName(), 19);
            room->revivePlayer(target, true);
            room->setPlayerProperty(target, "hp", 2);
            target->drawCards(2 * getEffectiveAmount(ctx), objectName());
            log.type = "#DingxinRevive"; log.to << target;
        } else {
            room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) + 17);
            room->recover(target, RecoverStruct(objectName(), ctx.owner, 3 * getEffectiveAmount(ctx)));
            log.type = "#DingxinRecover";
        }
        room->sendLog(log);
        return false;
    }
};
//chuangzao





//shana rework
//zhena
class Zhena : public TriggerSkillV2
{
public:
    Zhena() : TriggerSkillV2("inovation_Zhena") { events << EventSkillInvoking << DamageCaused; }
    int getPriority(TriggerEvent) const override { return -2; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext active = data.value<SkillContext>();
            if (active.bypass_cost && active.skill_name == objectName() && active.activationRef.key.skillName == objectName()) addUsage(active);
        }
        return false;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        return actor && actor->isAlive() && actor->hasSkill(objectName()) && damage.from == actor
            && damage.to && damage.nature == DamageStruct::Fire && actor->getPhase() == Player::Play
            ? TriggerList{{actor, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
        if (!target) return false; ctx.targets << target; return true;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { Q_UNUSED(ctx); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (ctx.choice == "lose_hp") { if (target->getHp() > 1) room->loseHp(target, target->getHp() - 1); return false; }
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        room->broadcastSkillInvoke(objectName()); room->doLightbox("inovation_Zhena$", 2500);
        damage.damage += target->getHp() * getEffectiveAmount(ctx); *ctx.original_data = QVariant::fromValue(damage);
        ctx.choice = "lose_hp"; if (ctx.invoker && ctx.invoker->isAlive()) skillEffect(DamageCaused, room, ctx.invoker, ctx, ctx.invoker);
        return false;
    }
};
class Tianhuo : public TriggerSkillV2
{
public:
    Tianhuo() : TriggerSkillV2("inovation_Tianhuo") { frequency = Compulsory; events << DamageCaused << DamageInflicted; }
    int getPriority(TriggerEvent) const override { return 2; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!actor || !actor->isAlive() || !actor->hasSkill(objectName())) return {};
        return (event == DamageInflicted ? damage.to == actor && damage.nature == DamageStruct::Fire
            : damage.from == actor && damage.card && (damage.card->isKindOf("Slash") || damage.card->isKindOf("Duel")))
            ? TriggerList{{actor, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!ctx.original_data) return false; ctx.targets << ctx.original_data->value<DamageStruct>().to; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.original_data) return false;
        if (event == DamageInflicted) {
            room->broadcastSkillInvoke(objectName(), 2); target->drawCards(target->getLostHp() * getEffectiveAmount(ctx), objectName());
            return true;
        }
        DamageStruct damage = ctx.original_data->value<DamageStruct>(); damage.nature = DamageStruct::Fire;
        *ctx.original_data = QVariant::fromValue(damage); return false;
    }
};

//nanami
class Shengyou : public TriggerSkillV2
{
public:
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker ? ctx.invoker : ctx.owner; }
    Shengyou() : TriggerSkillV2("inovation_shengyou") { events << EventPhaseStart; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &) const override
    {
        return actor && actor->isAlive() && actor->getPhase() == Player::RoundStart && actor->getHandcardNum() < 3
            && actor->hasSkill(objectName()) && actor->getTag("ShengyouEffect").isNull()
            ? TriggerList{{actor, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (!actor || actor->getPhase() != Player::Finish) return false;
        const QVariantMap receipt = actor->getTag("ShengyouEffect").toMap();
        if (receipt.isEmpty()) return true;
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = ctx.initiator = ctx.invoker = actor;
        ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
            SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
        ctx.instanceID = receipt.value("instance").toInt(); ctx.current_event = event;
        ctx.original_data = &data; ctx.extra_data = receipt; ctx.targets << actor;
        ctx.is_forced = true; contexts << ctx;
        Q_UNUSED(room);
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.extra_data.toMap().isEmpty()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        return ctx.owner && ctx.owner->isAlive() && ctx.owner->getTag("ShengyouEffect") == ctx.extra_data;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.extra_data.toMap().isEmpty()) return true;
        if (!ctx.owner) return false;
        QStringList generals = Sanguosha->getLimitedGeneralNames();
        for (ServerPlayer *player : room->getAlivePlayers()) { generals.removeAll(player->getGeneralName()); generals.removeAll(player->getGeneral2Name()); }
        const QStringList excluded{"Louise", "Misaka_Imouto", "inovation_Natsume_Rin", "Riko", "Koishi", "mianma", "tsukushi", "Niko"};
        for (int i = generals.size() - 1; i >= 0; --i) {
            const General *general = Sanguosha->getGeneral(generals.at(i));
            if (!general || general->isMale() || excluded.contains(generals.at(i))) generals.removeAt(i);
        }
        if (generals.isEmpty() || !ctx.owner->askForSkillInvoke(this)) return false;
        ctx.choice = room->askForGeneral(ctx.owner, generals.join("+"));
        if (!generals.contains(ctx.choice)) return false;
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        const QVariantMap retained = ctx.extra_data.toMap();
        if (!retained.isEmpty()) {
            target->removeTag("ShengyouEffect");
            room->changeHero(target, retained.value("general").toString(), false, false, retained.value("second").toBool(), true);
            return false;
        }
        const SkillInstance *instance = target->findSkillInstance(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
        const bool second = instance && instance->bindHead == 2;
        const QString original = second ? target->getGeneral2Name() : target->getGeneralName();
        target->setTag("ShengyouEffect", QVariantMap{{"general", original}, {"second", second},
            {"instance", ctx.activationRef.key.instanceID}, {"source_owner", ctx.sourceRef.ownerObjectName},
            {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}});
        // Hero replacement removes the granting instance; the receipt alone owns restoration.
        room->broadcastSkillInvoke(objectName()); room->doLightbox("inovation_shengyou$", 800);
        room->changeHero(target, ctx.choice, false, false, second, true);
        return false;
    }
};
class InovationJinqu : public TriggerSkillV2
{
public:
    InovationJinqu() : TriggerSkillV2("inovation_jinqu") { events << DamageInflicted; }
    int getPriority(TriggerEvent) const override { return -3; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &data) const override
    { return actor && actor->isAlive() && actor->hasSkill(objectName()) && data.value<DamageStruct>().to == actor
        ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << ctx.owner; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.owner) return false;
        if (ctx.choice == "give") {
            const CardsMoveStruct move = ctx.extra_data.value<CardsMoveStruct>();
            QList<int> ids;
            for (int id : move.card_ids) if (ctx.owner->handCards().contains(id)) ids << id;
            if (!ids.isEmpty()) {
                DummyCard cards(ids); room->obtainCard(target, &cards,
                    CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.owner->objectName(), target->objectName(), objectName(), ""), false);
            }
            return false;
        }
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        room->broadcastSkillInvoke(objectName()); target->turnOver();
        if (!target->isAlive()) return false;
        target->drawCards(target->getLostHp() * damage.damage * 2 * getEffectiveAmount(ctx), objectName());
        QList<int> remaining = target->handCards();
        while (target->isAlive() && !remaining.isEmpty()) {
            const CardsMoveStruct move = room->askForYijiStruct(target, remaining, objectName(), false, false, true, -1,
                room->getOtherPlayers(target), CardMoveReason(), QString(), false, false);
            if (!move.to || move.card_ids.isEmpty()) break;
            ctx.choice = "give"; ctx.extra_data = QVariant::fromValue(move);
            skillEffect(event, room, actor, ctx, room->findPlayerByObjectName(move.to->objectName()));
        }
        if (target->isAlive() && target->faceUp() && !target->getJudgingArea().isEmpty()) {
            const int id = room->askForCardChosen(target, target, "j", objectName());
            if (room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceDelayedTrick) room->throwCard(id, target, target);
        }
        return false;
    }
};

InovationZhurenCard::InovationZhurenCard()
{
    setSkillName("inovation_zhuren");
    will_throw = false;
}

bool InovationZhurenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return to_select != Self && targets.length() == 0;
}

void InovationZhurenCard::use(Room *room, ServerPlayer *player, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.at(0);
    if (!target)
        return;
    player->setTag("inovation_zhurenCardNum", QVariant::fromValue(this->subcardsLength()));
    room->obtainCard(target, this, false);
}

class InovationZhuren : public ViewAsSkillV2
{
public:
    InovationZhuren() : ViewAsSkillV2("inovation_zhuren") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && maxCards(request) > 0; }
    int maxCards(const ActiveSkillRequest &request) const
    {
        if (!request.initiator) return 0;
        int keys = 0;
        for (const Card *card : request.initiator->getJudgingArea()) if (card->isKindOf("KeyTrick")) ++keys;
        return request.initiator->getLostHp() + keys;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->hasFlag("using") && !request.selectedCardIds.contains(card->getEffectiveId())
            && request.selectedCardIds.size() < maxCards(request)
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquips().contains(card));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty()) return false;
        ActiveSkillRequest prefix = request; prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (!canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return request.initiator && target && selected.isEmpty() && target != request.initiator; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "InovationZhurenCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !target || !target->isAlive() || !ctx.use_card) return ContinueEffects;
        Room *room = target->getRoom();
        const int amount = ctx.use_card->subcardsLength() * getEffectiveAmount(ctx);
        room->obtainCard(target, ctx.use_card,
            CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.initiator->objectName(), target->objectName(), objectName(), ""), false);
        QVariantList receipts = room->getTag("ZhurenEffects").toList();
        const qint64 serial = room->getTag("ZhurenEffectsSequence").toLongLong()+1; room->setTag("ZhurenEffectsSequence",serial);
        receipts << QVariantMap{{"serial",serial},{"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID},
            {"recipient", ctx.invoker->objectName()}, {"amount", amount}, {"source_owner", ctx.sourceRef.ownerObjectName},
            {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
        room->setTag("ZhurenEffects", receipts);
        return ContinueEffects;
    }
};

class InovationZhurenTrigger : public TriggerSkillV2
{
public:
    InovationZhurenTrigger() : TriggerSkillV2("#inovation_zhuren")
    { events << EventPhaseEnd << EventPhaseChanging; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) room->removeTag("ZhurenEffects");
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseEnd || !actor || !actor->isAlive() || actor->getPhase() != Player::Discard) return true;
        for (const QVariant &value : room->getTag("ZhurenEffects").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("recipient").toString() != actor->objectName()) continue;
            SkillContext ctx; ctx.skill_name = objectName();
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = ctx.owner; ctx.invoker = actor; ctx.targets << actor;
            ctx.instanceID = receipt.value("serial").toInt(); ctx.trigger_count = 0;
            ctx.current_event = event; ctx.original_data = &data; ctx.extra_data = receipt;
            ctx.amount = receipt.value("amount").toInt(); contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->isAlive() && room->getTag("ZhurenEffects").toList().contains(ctx.extra_data); }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantList receipts = room->getTag("ZhurenEffects").toList(); receipts.removeOne(ctx.extra_data); room->setTag("ZhurenEffects", receipts);
        if (target && target->isAlive()) target->drawCards(getEffectiveAmount(ctx), "inovation_zhuren");
        return false;
    }
};
class Daolu : public TriggerSkillV2
{
public:
    Daolu() : TriggerSkillV2("inovation_Daolu")
    {
        frequency = Wake;
        events << EventSkillInvoking << AskForPeachesDone;
    }

    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext active = data.value<SkillContext>();
            if (active.bypass_cost && active.skill_name == objectName() && active.activationRef.key.skillName == objectName()) addUsage(active);
        }
        return false;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ctx.choice = room->askForChoice(ctx.owner, objectName(), "inovation_Nagisa_Protector+Kyou_Lover+Tomoyo_Couple+Fuko_summoner");
        ctx.targets << ctx.invoker;
        return !ctx.choice.isEmpty();
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {};
        return event == AskForPeachesDone && player && player->isAlive() && player->hasSkill(objectName())
            && data.value<DyingStruct>().who == player ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { Q_UNUSED(ctx); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        if (!player || !player->isAlive()) return false;
        const QString choice = ctx.choice;
        room->loseMaxHp(player);
        room->setPlayerProperty(player, "hp", QVariant(2));
        if (choice == "inovation_Nagisa_Protector"){
            room->broadcastSkillInvoke(objectName(), 1);
            room->doLightbox("inovation_DaoluA$", 3000);
            player->gainMark("@inovation_Nagisa");
            if (!player->hasSkill("diangong")){
                room->acquireSkill(player, "diangong");
                // The registered related helper follows the acquired exact parent.
            }
        }
        else if (choice == "Tomoyo_Couple"){
            room->broadcastSkillInvoke(objectName(), 3);
            room->doLightbox("inovation_DaoluB$", 3000);
            player->gainMark("@Tomoyo");
            if (!player->hasSkill("inovation_shouyang")){
                room->acquireSkill(player, "inovation_shouyang");
            }
        }
        else if (choice == "Kyou_Lover"){
            room->broadcastSkillInvoke(objectName(), 2);
            room->doLightbox("inovation_DaoluD$", 3000);
            player->gainMark("@Kyou");
            if (!player->hasSkill("tanyan")){
                room->acquireSkill(player, "tanyan");
            }
        }
        else{
            room->broadcastSkillInvoke(objectName(), 4);
            room->doLightbox("inovation_DaoluC$", 3000);
            player->gainMark("@Fuko");
            if (!player->hasSkill("inovation_haixing")){
                room->acquireSkill(player, "inovation_haixing");
            }
        }
        return false;
    }
};

DiangongCard::DiangongCard()
{
    setSkillName("diangong");
    will_throw = false;
}

bool DiangongCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    /*
    foreach(const Card *card, to_select->getJudgingArea()){
        if (card->isKindOf("Lightning"))
            return false;
    }
    */
    return targets.length() == 0 ;
}

void DiangongCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.at(0);
    if (!target)
        return;
    Lightning *l = new Lightning(this->getSuit(), this->getNumber());
    l->addSubcard(this);
    l->setSkillName("diangong");
    CardUseStruct use;
    use.from = source;
    use.to.append(target);
    use.card = l;
    bool toJudge = target->containsTrick("lightning");
    room->useCard(use, true);
    if (toJudge){
        JudgeStruct judge;
        judge.pattern = ".|spade|2~9";
        judge.good = false;
        judge.reason = objectName();
        judge.time_consuming = true;
        judge.who = target;
        judge.negative = true;
        room->judge(judge);
        if (judge.isEffected()){
            room->damage(DamageStruct(l, NULL, target, 3, DamageStruct::Thunder));
            CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, QString());
            room->throwCard(l, reason, NULL);
        }
    }
}

class Diangong : public ViewAsSkillV2
{
public:
    Diangong() : ViewAsSkillV2("diangong", 1) {}
    QString historyKey(const ActiveSkillRequest &) const override { return "DiangongCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.initiator && request.selectedCardIds.isEmpty() && card && card->isBlack()
        && !card->hasFlag("using") && request.initiator->handCards().contains(card->getEffectiveId()); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest prefix = request; prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *target) const override
    { return selected.isEmpty() && target && target->hasJudgeArea(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    bool willThrowSelectedCards() const override { return false; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !target || !target->isAlive() || !ctx.use_card || !target->hasJudgeArea()) return ContinueEffects;
        if (getEffectiveAmount(ctx) <= 0 || !ctx.initiator) return ContinueEffects;
        for (int id : ctx.use_card->getSubcards())
            if (!ctx.initiator->handCards().contains(id) || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        Room *room = target->getRoom();
        if (!target->containsTrick("lightning")) {
            auto *lightning = new Lightning(Card::SuitToBeDecided, -1); CardLifetimeLease lightningLease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(lightning)); lightning->deleteLater();
            lightning->addSubcards(ctx.use_card->getSubcards()); lightning->setSkillName(objectName());
            room->useCardFromSkillEffect(CardUseStruct(lightning, ctx.invoker, target), ctx);
        } else {
            // A second Lightning is an immediate judgement; it does not create a duplicate delayed trick.
            room->throwCard(ctx.use_card, CardMoveReason(CardMoveReason::S_REASON_USE, ctx.initiator->objectName(), objectName(), ""), ctx.initiator);
            JudgeStruct judge; judge.pattern = ".|spade|2~9"; judge.good = false;
            judge.reason = objectName(); judge.time_consuming = true; judge.who = target; judge.negative = true;
            room->judge(judge);
            if (judge.isEffected()) {
                Lightning lightning(Card::NoSuit, 0); lightning.setSkillName(objectName());
                room->damage(DamageStruct(&lightning, nullptr, target, 3 * getEffectiveAmount(ctx), DamageStruct::Thunder));
            }
        }
        return ContinueEffects;
    }
};

class DiangongTrigger : public TriggerSkillV2
{
public:
    DiangongTrigger() : TriggerSkillV2("#diangong") { events << DamageInflicted; }
    int getPriority(TriggerEvent) const override { return -3; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || (!damage.card->isKindOf("Lightning") && damage.card->getSkillName() != "diangong")) return result;
        for (ServerPlayer *owner : room->getAlivePlayers()) if (owner->hasSkill("diangong")) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke("diangongDamage", *ctx.original_data)) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "@diangong-from");
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        room->broadcastSkillInvoke("diangong");
        room->recover(target, RecoverStruct("diangong", ctx.owner, getEffectiveAmount(ctx)));
        return true;
    }
};
class Shouyang : public TriggerSkillV2
{
public:
    Shouyang() : TriggerSkillV2("inovation_shouyang")
    { frequency = Compulsory; events << DamageInflicted << Death << EventAcquireSkill << EventLoseSkill << GameStart; global = true; }
    int getPriority(TriggerEvent) const override { return -4; }
    static void project(Room *room)
    {
        for (ServerPlayer *owner : room->getAlivePlayers())
            for (int id : owner->getSkillInstanceIds("inovation_shouyang"))
                ++counts[owner->getSkillInstanceStateValue("inovation_shouyang", id, "target").toString()];
        for (ServerPlayer *target : room->getAllPlayers()) room->setPlayerMark(target, "@Tomo", counts.value(target->objectName()));
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
    { if (event == EventLoseSkill) project(room); return false; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventAcquireSkill) {
            SkillChangeStruct change;
            if (actor && change.tryParse(data) && change.skillName == objectName())
                result[actor] << SkillInstanceUtils::formatName(objectName(), change.instanceID);
        } else if (event == GameStart && actor && actor->hasSkill(objectName())) result[actor] << objectName();
        else if (event == DamageInflicted) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (!damage.to) return result;
            for (ServerPlayer *owner : room->getAlivePlayers())
                for (int id : owner->getValidSkillInstanceIds(objectName())) {
                    const QString target = owner->getSkillInstanceStateValue(objectName(), id, "target").toString();
                    if (!target.isEmpty() && (damage.to == owner || damage.to->objectName() == target))
                        result[owner] << SkillInstanceUtils::formatName(objectName(), id);
                }
        } else if (event == Death && actor && data.value<DeathStruct>().who == actor && actor->hasSkill(objectName()))
            result[actor] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        if (event == EventAcquireSkill || event == GameStart) {
            if (!ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "target").toString().isEmpty()) return false;
            const QList<ServerPlayer *> candidates = room->getOtherPlayers(ctx.owner);
            if (candidates.isEmpty()) return false;
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@inovation_Daolu-Tomo");
            if (!target) return false;
            ctx.targets << target;
        } else {
            const QString name = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "target").toString();
            ServerPlayer *protectedPlayer = room->findPlayerByObjectName(name);
            if (!protectedPlayer || !protectedPlayer->isAlive()) return false;
            if (event == DamageInflicted && ctx.original_data->value<DamageStruct>().to != ctx.owner) {
                ctx.choice = "redirect"; ctx.targets << ctx.owner;
            } else ctx.targets << protectedPlayer;
        }
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (event == EventAcquireSkill || event == GameStart) {
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "target", target->objectName());
            project(room);
        } else if (event == Death) {
            DummyCard cards(ctx.owner->handCards());
            for (const Card *card : ctx.owner->getEquips()) cards.addSubcard(card);
            if (cards.subcardsLength() > 0) room->obtainCard(target, &cards,
                CardMoveReason(CardMoveReason::S_REASON_RECYCLE, target->objectName()), false);
            project(room);
        } else if (ctx.choice == "redirect") {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            LogMessage log; log.type = "#inovation_shouyangTrigger"; log.from = ctx.owner; log.to << damage.to;
            room->sendLog(log); room->broadcastSkillInvoke(objectName());
            damage.to = target; *ctx.original_data = QVariant::fromValue(damage);
        } else target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class ShouyangClear : public DetachEffectSkill
{
public:
    ShouyangClear() : DetachEffectSkill("inovation_shouyang") {}
    void onSkillDetached(Room *room, ServerPlayer *) const override { Shouyang::project(room); }
};
class Haixing : public TriggerSkillV2
{
public:
    Haixing() : TriggerSkillV2("inovation_haixing") { events << Dying; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        ServerPlayer *target = data.value<DyingStruct>().who;
        if (!target || !target->isAlive()) return result;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && owner->canDiscard(owner, "h")) result[owner] << objectName();
        return result;
    }    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        const Card *card = room->askForCard(ctx.owner, ".|.|.|hand", objectName(), *ctx.original_data, Card::MethodNone);
        if (!card || card->getEffectiveId() < 0 || ctx.owner->isJilei(card)) return false;
        ctx.extra_data = card->getEffectiveId();
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (!ctx.owner->handCards().contains(id) || !ctx.owner->canDiscard(ctx.owner, id)) return false;
        room->throwCard(id, objectName(), ctx.owner);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        room->broadcastSkillInvoke(objectName());
        JudgeStruct judge; judge.pattern = "."; judge.reason = objectName(); judge.who = ctx.owner; judge.time_consuming = true;
        room->judge(judge);
        ctx.extra_data = int(judge.card && judge.card->getNumber() > 8) + int(judge.card && judge.card->isRed());
        if (ctx.extra_data.toInt() > 0) ctx.targets << ctx.original_data->value<DyingStruct>().who;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive())
            for (int i = 0; i < ctx.extra_data.toInt(); ++i) room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
        return false;
    }
};
class Tanyan : public TriggerSkillV2
{
public:
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker ? ctx.invoker : ctx.owner; }
    Tanyan() : TriggerSkillV2("tanyan") { events << EventPhaseStart << EventPhaseChanging << CardFinished; global = true; }
    int getPriority(TriggerEvent) const override { return -2; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !actor || data.value<PhaseChangeStruct>().from != Player::Play) return false;
        QVariantList kept;
        for (const QVariant &value : room->getTag("TanyanEffects").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("actor").toString() != actor->objectName()) { kept << value; continue; }
            if (ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true))
                room->removeFixedDistance(actor, owner, 1);
        }
        room->setTag("TanyanEffects", kept);
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && actor && actor->isAlive() && actor->getPhase() == Player::Play)
            for (ServerPlayer *owner : room->getAlivePlayers())
                if (owner->hasSkill(objectName()) && !owner->isKongcheng()) result[owner] << objectName();
        return result;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != CardFinished) return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!actor || actor->getPhase() != Player::Play || use.from != actor || !use.card || !use.card->isKindOf("Slash")) return true;
        const int count = room->countHistoryCards(actor, "phase", "Slash");
        if (count < 1 || count > 2) return true;
        for (const QVariant &value : room->getTag("TanyanEffects").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("actor").toString() != actor->objectName()) continue;
            SkillContext ctx; ctx.skill_name = objectName();
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString());
            if (!ctx.owner || !ctx.owner->isAlive()) continue;
            ctx.initiator = ctx.owner; ctx.invoker = actor; ctx.instanceID = receipt.value("instance").toInt();
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.original_data = &data; ctx.current_event = event; ctx.extra_data = receipt; ctx.targets << ctx.owner;
            ctx.is_forced = true; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return !ctx.activationRef.isValid() ? ctx.owner && ctx.owner->isAlive()
            && room->getTag("TanyanEffects").toList().contains(ctx.extra_data) : TriggerSkillV2::isSourceAvailable(room, ctx);
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *actor, SkillContext &ctx) const override
    {
        if (event == CardFinished) return true;
        if (!ctx.owner || !actor || ctx.owner->isKongcheng() || !ctx.owner->askForSkillInvoke(this)) return false;
        ctx.targets << actor;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.owner) return false;
        if (event == EventPhaseStart) {
            room->broadcastSkillInvoke(objectName()); room->showAllCards(ctx.owner, target);
            room->setFixedDistance(target, ctx.owner, 1);
            QVariantList receipts = room->getTag("TanyanEffects").toList();
            receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID},
                {"actor", target->objectName()}, {"source_owner", ctx.sourceRef.ownerObjectName},
                {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
            room->setTag("TanyanEffects", receipts);
        } else if (ctx.choice.startsWith("return:")) {
            const int id = ctx.choice.section(':', 1).toInt();
            if (room->getCardOwner(id) == ctx.owner) room->obtainCard(target, id, false);
        } else {
            const Card *card = ctx.original_data->value<CardUseStruct>().card;
            QList<int> ids = card->isVirtualCard() ? card->getSubcards() : QList<int>() << card->getEffectiveId();
            for (int i = ids.size() - 1; i >= 0; --i) if (room->getCardPlace(ids.at(i)) != Player::PlaceTable) ids.removeAt(i);
            if (ids.isEmpty()) return false;
            DummyCard material(ids); room->obtainCard(target, &material);
            if (target->isAlive() && !target->isNude() && ctx.invoker->isAlive()) {
                const int id = room->askForCardChosen(target, target, "he", objectName());
                if (room->getCardOwner(id) != target) return false;
                ctx.choice = "return:" + QString::number(id);
                skillEffect(event, room, ctx.invoker, ctx, ctx.invoker);
            }
        }
        return false;
    }
};
class Pasheng : public DistanceSkillV2
{
public:
    Pasheng() : DistanceSkillV2("inovation_SE_Pasheng")
    {
        setHolderSelector(CorrectSkill_Participants);
        setBaseAmount(100);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.holder == ctx.primary) return CorrectSkillResult::useAmount(ctx.currentAmount);
        if (ctx.holder == ctx.secondary) return CorrectSkillResult::useAmount(-ctx.currentAmount);
        return CorrectSkillResult::noEffect();
    }
};

class Maoqun : public TriggerSkillV2
{
public:
    Maoqun() : TriggerSkillV2("inovation_SE_Maoqun") { frequency = Compulsory; events << Damage; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        TriggerList result;
        for (ServerPlayer *owner : room->getAlivePlayers()) if (owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.owner; return ctx.owner != nullptr; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        room->broadcastSkillInvoke(objectName());
        room->loseHp(target, getEffectiveAmount(ctx));
        if (target->isAlive()) target->addToPile("Neko", room->getNCards(getEffectiveAmount(ctx)));
        return false;
    }
};

class MaoqunHeg : public TriggerSkillV2
{
public:
    MaoqunHeg() : TriggerSkillV2("inovation_SE_MaoqunHeg") { frequency = Compulsory; events << GameStart << EventAcquireSkill; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        TriggerList result;
        if (!actor || !actor->isAlive()) return result;
        if (event == EventAcquireSkill) {
            SkillChangeStruct change;
            if (change.tryParse(data) && change.skillName == objectName()) result[actor] << SkillInstanceUtils::formatName(objectName(), change.instanceID);
        } else if (actor->hasSkill(objectName())) result[actor] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "initialized").toBool()) return false;
        ctx.targets << ctx.owner;
        return true;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "initialized", true); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive()) {
            room->broadcastSkillInvoke(objectName());
            target->addToPile("Neko", room->getNCards(room->alivePlayerCount() * getEffectiveAmount(ctx)));
        }
        return false;
    }
};

class Chengzhang : public TriggerSkillV2
{
public:
    Chengzhang() : TriggerSkillV2("inovation_SE_Chengzhang") { frequency = Wake; events << EventSkillInvoking << EventPhaseStart; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext active = data.value<SkillContext>();
            if (active.bypass_cost && active.skill_name == objectName() && active.activationRef.key.skillName == objectName()) addUsage(active);
        }
        return false;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &) const override
    {
        if (event == EventSkillInvoking) return {};
        return actor && actor->isAlive() && actor->hasSkill(objectName()) && actor->getPhase() == Player::RoundStart
            && actor->getPile("Neko").size() >= room->alivePlayerCount() * 3 / 2 ? TriggerList{{actor, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.owner) return false;
        QVariantList removed;
        const SkillInstance *activation = ctx.owner->findSkillInstance(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
        if (activation && activation->bindHead != 0) {
            const int slot = activation->bindHead;
            for (const QString &name : {QString("inovation_SE_Pasheng"), QString("inovation_SE_Maoqun")})
                for (int id : ctx.owner->getSkillInstanceIds(name)) {
                    const SkillInstance *sibling = ctx.owner->findSkillInstance(name, id);
                    if (sibling && sibling->bindHead == slot && !sibling->parentRef.isValid())
                        removed << SkillInstanceUtils::formatName(name, id);
                }
        }
        ctx.extra_data = removed; ctx.targets << ctx.invoker; return true;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { Q_UNUSED(ctx); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *owner) const override
    {
        if (!owner || !owner->isAlive()) return false;
        if (owner->getMaxHp() >= 100) room->loseMaxHp(owner, 96);
        else if (owner->getMaxHp() > 3) room->loseMaxHp(owner, owner->getMaxHp() - 3);
        room->broadcastSkillInvoke(objectName()); owner->gainMark("@waked");
        room->doLightbox("inovation_SE_Chengzhang$", 3000);
        // Only siblings of this awakening source are retired, never unrelated acquired copies.
        if (owner == ctx.owner) for (const QVariant &name : ctx.extra_data.toList()) room->detachSkillFromPlayer(owner, name.toString());
        room->acquireSkill(owner, "zhiling");
        room->acquireSkill(owner, "inovation_SE_Zhixing");
        return false;
    }
};
ZhilingCard::ZhilingCard()
{
    setSkillName("zhiling");
}

bool ZhilingCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.length() == 0 && (to_select->getMark("@Neko_S") == 0 || to_select->getMark("@Neko_C") == 0 || to_select->getMark("@Neko_D") == 0 || to_select->getMark("@Neko_H") == 0) && !to_select->hasFlag("Can_not");
}

void ZhilingCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.at(0);
    if (!target)
        return;
    QList<int> list = source->getPile("Neko");
    QList<int> left = source->getPile("Neko");
    if (target->getMark("@Neko_S") > 0){
        foreach(int id, list){
            if (Sanguosha->getCard(id)->getSuit() == Card::Spade)
                left.removeOne(id);
        }
    }
    if (target->getMark("@Neko_C") > 0){
        foreach(int id, list){
            if (Sanguosha->getCard(id)->getSuit() == Card::Club)
                left.removeOne(id);
        }
    }
    if (target->getMark("@Neko_D") > 0){
        foreach(int id, list){
            if (Sanguosha->getCard(id)->getSuit() == Card::Diamond)
                left.removeOne(id);
        }
    }
    if (target->getMark("@Neko_H") > 0){
        foreach(int id, list){
            if (Sanguosha->getCard(id)->getSuit() == Card::Heart)
                left.removeOne(id);
        }
    }
    if (left.length() == 0){
        room->setPlayerFlag(target, "Can_not");
        return;
    }
    room->fillAG(left, source);
    int id = room->askForAG(source, left, false, objectName());
    room->clearAG(source);
    if (id == -1)
        return;
    switch (Sanguosha->getCard(id)->getSuit()){
    case Card::Spade:
        target->gainMark("@Neko_S");
        break;
    case Card::Club:
        target->gainMark("@Neko_C");
        break;
    case Card::Diamond:
        target->gainMark("@Neko_D");
        break;
    case Card::Heart:
        target->gainMark("@Neko_H");
        break;
    }
    room->throwCard(id, NULL, NULL);
}

class Zhiling : public ViewAsSkillV2
{
public:
    Zhiling() : ViewAsSkillV2("zhiling", 0) {}
    static QString suitKey(Card::Suit suit)
    {
        return suit == Card::Spade ? "S" : suit == Card::Club ? "C" : suit == Card::Diamond ? "D" : suit == Card::Heart ? "H" : QString();
    }
    static bool affected(const Player *player, Card::Suit suit)
    {
        for (const QVariant &value : player->getTag("ZhilingEffects").toList())
            if (value.toMap().value("suit").toInt() == int(suit)) return true;
        return false;
    }
    static QList<int> available(const Player *owner, const Player *target)
    {
        QList<int> ids;
        if (!owner || !target) return ids;
        for (int id : owner->getPile("Neko")) {
            const Card *card = Sanguosha->getCard(id);
            if (card && !suitKey(card->getSuit()).isEmpty() && !affected(target, card->getSuit())) ids << id;
        }
        return ids;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "ZhilingCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->getPile("Neko").isEmpty(); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return target && selected.isEmpty() && !available(request.initiator, target).isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator || ctx.targets.size() != 1) return false;
        const QList<int> ids = available(ctx.initiator, ctx.targets.first());
        if (ids.isEmpty()) return false;
        room->fillAG(ids, ctx.initiator);
        auto clear = qScopeGuard([&] { room->clearAG(ctx.initiator); });
        const int id = room->askForAG(ctx.initiator, ids, false, objectName());
        if (!ids.contains(id)) return false;
        ctx.extra_data = QVariantMap{{"id", id}, {"suit", int(Sanguosha->getCard(id)->getSuit())}};
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        const int id = ctx.extra_data.toMap().value("id", -1).toInt();
        if (!ctx.initiator || ctx.targets.size() != 1 || !available(ctx.initiator, ctx.targets.first()).contains(id)) return false;
        room->throwCard(id, nullptr, ctx.initiator);
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    { if (ctx.extra_data.toMap().isEmpty() && (!ctx.invoker || !cost(ctx.invoker->getRoom(), ctx, ActiveSkillRequest()))) return FinishSkill; return ContinueEffects; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || ctx.extra_data.toMap().isEmpty()) return ContinueEffects;
        const Card::Suit suit = Card::Suit(ctx.extra_data.toMap().value("suit").toInt());
        if (affected(target, suit)) return ContinueEffects;
        // The permanent effect owns its frozen source and amount after the grant disappears.
        QVariantList receipts = target->getTag("ZhilingEffects").toList();
        receipts << QVariantMap{{"suit", int(suit)}, {"amount", getEffectiveAmount(ctx)},
            {"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_instance", ctx.sourceRef.key.instanceID}};
        target->setTag("ZhilingEffects", receipts);
        target->getRoom()->setPlayerMark(target, "@Neko_" + suitKey(suit), getEffectiveAmount(ctx));
        return ContinueEffects;
    }
};

class ZhilingTrigger : public TriggerSkillV2
{
public:
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker ? ctx.invoker : ctx.owner; }
    ZhilingTrigger() : TriggerSkillV2("#zhiling")
    { frequency = Compulsory; global = true; events << DrawNCards << DamageInflicted << AskForPeaches; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        ServerPlayer *target = event == AskForPeaches ? data.value<DyingStruct>().who
            : event == DamageInflicted ? data.value<DamageStruct>().to : data.value<DrawStruct>().who;
        if (!target || !target->isAlive()) return true;
        const Card::Suit suit = event == DrawNCards ? Card::Spade : event == DamageInflicted ? Card::Diamond : Card::Heart;
        if ((event == DrawNCards && target->getPhase() != Player::Draw)
            || (event == DamageInflicted && data.value<DamageStruct>().nature == DamageStruct::Normal)
            || (event == AskForPeaches && actor == target)) return true;
        for (const QVariant &value : target->getTag("ZhilingEffects").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("suit").toInt() != int(suit)) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            ctx.initiator = ctx.owner; ctx.invoker = actor ? actor : target; ctx.instanceID = receipt.value("instance").toInt();
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.amount = receipt.value("amount", 1).toInt(); ctx.current_event = event; ctx.original_data = &data;
            ctx.extra_data = receipt; ctx.targets << target; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return !ctx.targets.isEmpty() && ctx.targets.first()->getTag("ZhilingEffects").toList().contains(ctx.extra_data); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }
    bool effectTarget(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        if (!ctx.original_data) return false;
        if (event == DrawNCards && qsanRandomBounded(3) == 0) {
            DrawStruct draw = ctx.original_data->value<DrawStruct>(); draw.num = qMax(0, draw.num - 2 * getEffectiveAmount(ctx));
            *ctx.original_data = QVariant::fromValue(draw);
        } else if (event == DamageInflicted) {
            DamageStruct damage = ctx.original_data->value<DamageStruct>(); damage.damage += getEffectiveAmount(ctx);
            *ctx.original_data = QVariant::fromValue(damage);
        } else if (event == AskForPeaches) return qsanRandomBounded(2) == 1;
        return false;
    }
};

class ZhilingMaxCards : public MaxCardsSkillV2
{
public:
    ZhilingMaxCards() : MaxCardsSkillV2("#zhiling-max") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        // Public marks project the permanent applied amount for client corrections.
        if (ctx.primary && ctx.primary->getMark("@Neko_C") > 0)
            return CorrectSkillResult::useAmount(-ctx.primary->getMark("@Neko_C") * ctx.currentAmount);
        return CorrectSkillResult::noEffect();
    }
};

class Zhixing : public TriggerSkillV2
{
public:
    Zhixing() : TriggerSkillV2("inovation_SE_Zhixing") { events << Dying << DamageInflicted; }
    static int key(const Player *target)
    {
        if (target) for (const Card *card : target->getJudgingArea()) if (card->isKindOf("KeyTrick")) return card->getEffectiveId();
        return -1;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        ServerPlayer *target = event == Dying ? data.value<DyingStruct>().who : data.value<DamageStruct>().to;
        if (!target || !target->isAlive() || (event == Dying ? key(target) >= 0 || !target->hasJudgeArea() : key(target) < 0)) return result;
        for (ServerPlayer *owner : room->getAlivePlayers()) if (owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        ServerPlayer *target = event == Dying ? ctx.original_data->value<DyingStruct>().who : ctx.original_data->value<DamageStruct>().to;
        if (!target || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target))) return false;
        ctx.targets << target;
        if (event == DamageInflicted) { ctx.extra_data = key(target); return ctx.extra_data.toInt() >= 0; }
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *p : room->getAlivePlayers()) if (!p->isNude() || !p->getJudgingArea().isEmpty()) candidates << p;
        if (candidates.isEmpty()) return false;
        ServerPlayer *from = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@zhixing-from");
        if (!from) return false;
        const int id = room->askForCardChosen(ctx.owner, from, "hej", objectName());
        ctx.extra_data = QVariantMap{{"id", id}, {"from", from->objectName()}};
        return id >= 0;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != DamageInflicted) return true;
        if (ctx.targets.isEmpty() || key(ctx.targets.first()) != ctx.extra_data.toInt()) return false;
        room->throwCard(ctx.extra_data.toInt(), ctx.targets.first(), ctx.owner);
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        room->broadcastSkillInvoke(objectName()); room->doLightbox("inovation_SE_Zhixing$", 800);
        if (event == DamageInflicted) return true;
        const QVariantMap selected = ctx.extra_data.toMap(); const int id = selected.value("id", -1).toInt();
        ServerPlayer *from = room->findPlayerByObjectName(selected.value("from").toString());
        if (!from || room->getCardOwner(id) != from || (room->getCardPlace(id) != Player::PlaceHand
            && room->getCardPlace(id) != Player::PlaceEquip && room->getCardPlace(id) != Player::PlaceDelayedTrick)
            || key(target) >= 0 || !target->hasJudgeArea()) return false;
        const Card *material = Sanguosha->getCard(id);
        auto *card = new KeyTrick(material->getSuit(), material->getNumber()); CardLifetimeLease cardLease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(card)); card->deleteLater();
        card->addSubcard(id); card->setSkillName(objectName());
        room->useCardFromSkillEffect(CardUseStruct(card, ctx.invoker, target), ctx, true);
        return false;
    }
};

//koromo
class Kongdi : public TriggerSkillV2
{
public:
    Kongdi() : TriggerSkillV2("kongdi") { events << CardsMoveOneTime; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!move.to || !move.to->isAlive() || move.to_place != Player::PlaceHand
            || !move.from_places.contains(Player::DrawPile)) return result;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner != move.to && owner->hasSkill(objectName())
                && owner->getHandcardNum() < move.to->getHandcardNum() - move.card_ids.length())
                result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        const Player *recipient = ctx.original_data->value<CardsMoveOneTimeStruct>().to;
        ServerPlayer *target = recipient ? room->findPlayerByObjectName(recipient->objectName()) : nullptr;
        if (!target || target->isKongcheng()) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !ctx.owner) return false;
        for (int n = 0; n < getEffectiveAmount(ctx) && target->isAlive() && ctx.owner->isAlive() && !target->isKongcheng(); ++n) {
            const int id = room->askForCardChosen(ctx.owner, target, "h", objectName(), true);
            if (id < 0) break;
            room->showCard(target, id);
            const QString choice = room->askForChoice(ctx.owner, objectName(), "kongdi_di+kongdi_discard");
            if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand) continue;
            if (qsanRandomBounded(5) == 1) room->broadcastSkillInvoke(objectName());
            if (choice == "kongdi_di") room->moveCardsToEndOfDrawpile(target, QList<int>() << id, objectName(), false);
            else room->throwCard(id, target, ctx.owner);
        }
        return false;
    }
};
class Yixiangting : public TriggerSkillV2
{
public:
    Yixiangting() : TriggerSkillV2("inovation_yixiang")
    {
        frequency = Compulsory;
        events << BeforeCardsMove;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != BeforeCardsMove || !room) return {};
        TriggerList result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!move.to || move.to_place != Player::PlaceHand || !move.from_places.contains(Player::DrawPile)) return result;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner != move.to && owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *recipient = ctx.original_data ? qobject_cast<ServerPlayer *>(ctx.original_data->value<CardsMoveOneTimeStruct>().to) : nullptr;
        if (!recipient) return false;
        ctx.targets << recipient; return true;
    }
    bool effectTarget(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *recipient) const override
    {
        if (!ctx.original_data) return false;
        QVariant &data = *ctx.original_data;
        if (triggerEvent == BeforeCardsMove){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            ServerPlayer *koromo = ctx.owner;
            if (!koromo || move.to != recipient || koromo == move.to || move.to_place != Player::PlaceHand || !move.from_places.contains(Player::DrawPile))
                return false;
            QList<int> new_ids;
            QList<int> to_remove;
            int rd;
            foreach(int id, move.card_ids){
                if (room->getDrawPile().contains(id)){
                    rd = qsanRandomBounded(room->getDrawPile().length());
                    while (new_ids.contains(room->getDrawPile().at(rd)))
                        rd = qsanRandomBounded(room->getDrawPile().length());
                    new_ids.append(room->getDrawPile().at(rd));
                    to_remove.append(id);
                }
            }
            move.removeCardIds(to_remove);
            foreach(int new_id, new_ids){
                move.card_ids.append(new_id);
                move.from_places.append(Player::DrawPile);
                move.from_pile_names.append(QString());
                move.open.append(false);
            }
            if (move.to->getPhase() != Player::Draw && qsanRandomBounded(3) == 1){
                room->broadcastSkillInvoke(objectName());
            }
            data.setValue(move);
        }
        return false;
    }
};

//kyou

class TouzhiVS : public ViewAsSkillV2
{
public:
    TouzhiVS() : ViewAsSkillV2("touzhi", 1)
    {
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !request.selectedCardIds.isEmpty() || !card || card->hasFlag("using")
            || !request.initiator->handCards().contains(card->getEffectiveId()) || !card->isKindOf("TrickCard") || card->isKindOf("AOE") || card->isKindOf("GodSalvation") || card->isKindOf("AmazingGrace") || card->isKindOf("Collateral"))
            return false;
        return true;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        if (!originalCard || !cardSelectionFeasible(request)) return nullptr;
        Card *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->addSubcard(originalCard);
        slash->setSkillName("touzhi");
        slash->setTag("TouzhiMaterial", QVariantMap{{"name", originalCard->objectName()},
            {"suit", int(originalCard->getSuit())}, {"number", originalCard->getNumber()}});
        return slash;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }
};

class Touzhi : public TriggerSkillV2
{
public:
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker ? ctx.invoker : ctx.owner; }
    Touzhi() : TriggerSkillV2("touzhi")
    { events << CardUsed << CardFinished << EventSkillInvoking; view_as_skill = new TouzhiVS; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext active = data.value<SkillContext>();
            if (active.activationRef.key.skillName == objectName() && active.use_card && active.use_card->isKindOf("Slash")) {
                active.use_card->setTag("TouzhiAmount", getEffectiveAmount(active));
                ServerPlayer *holder = room->findPlayerByObjectName(active.activationRef.ownerObjectName);
                if (holder && holder->findSkillInstance(objectName(), active.activationRef.key.instanceID)) {
                    const int value = holder->getSkillInstanceCorrectStateValue(objectName(), active.activationRef.key.instanceID, "distance").toInt();
                    holder->setSkillInstanceCorrectStateValue(objectName(), active.activationRef.key.instanceID, "distance", value + 1);
                }
            }
        } else if (event == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card && use.card->isKindOf("Slash") && use.card->getSkillName() == objectName() && use.m_addHistory) {
                room->addPlayerHistory(use.from, use.card->getClassName(), -1); use.m_addHistory = false; data = QVariant::fromValue(use);
            }
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != CardFinished) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!actor || !actor->isAlive() || !use.card || !use.card->isKindOf("Slash") || use.card->getSkillName() != objectName()) return true;
        const QVariantMap used = room->historyParent(room->currentHistoryEventId(), "use_card", true);
        if (!use.activationRef.isValid() || !use.sourceRef.isValid()) return true;
        const QVariantMap damage = room->queryCardUseDamage(used.value("id").toLongLong());
        if (!damage.value("complete").toBool() || damage.contains("error")) return true;
        SkillContext ctx; ctx.skill_name = objectName(); ctx.invoker = actor;
        ctx.owner = room->findPlayerByObjectName(use.activationRef.ownerObjectName, true); ctx.initiator = ctx.owner;
        ctx.instanceID = use.activationRef.key.instanceID; ctx.sourceRef = use.sourceRef;
        ctx.original_data = &data; ctx.current_event = event;
        QVariantMap receipt = use.card->getTag("TouzhiMaterial").toMap();
        receipt.insert("activation_owner", use.activationRef.ownerObjectName);
        receipt.insert("activation_skill", use.activationRef.key.skillName);
        receipt.insert("activation_instance", use.activationRef.key.instanceID);
        ctx.extra_data = receipt; ctx.amount = use.card->getTag("TouzhiAmount").toInt();
        // The resolved ordinary card owns this continuation even if its granting skill was removed.
        for (const QVariant &value : damage.value("items").toList()) {
            ServerPlayer *target = room->findPlayerByObjectName(value.toMap().value("data").toMap().value("to").toString());
            if (target && target->isAlive() && use.to.contains(target) && !ctx.targets.contains(target)) ctx.targets << target;
        }
        if (damage.value("items").toList().isEmpty()) { ctx.choice = "miss"; ctx.targets << actor; }
        if (!ctx.targets.isEmpty()) contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return !ctx.activationRef.isValid() && ctx.sourceRef.isValid() && ctx.invoker && ctx.invoker->isAlive() && ctx.original_data && ctx.original_data->value<CardUseStruct>().card; }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.invoker->isAlive()) return false;
        SkillContext continuation = ctx;
        const QVariantMap receipt = ctx.extra_data.toMap();
        continuation.activationRef = SkillInstanceRef(receipt.value("activation_owner").toString(),
            SkillInstanceKey(receipt.value("activation_skill").toString(), receipt.value("activation_instance").toInt()));
        if (ctx.choice == "miss") {
            auto *card = new Analeptic(Card::NoSuit, 0); CardLifetimeLease cardLease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(card)); card->deleteLater(); card->setSkillName(objectName());
            room->useCardFromSkillEffect(CardUseStruct(card, ctx.invoker, target), continuation, false);
            if (target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
        } else {
            const QVariantMap material = ctx.extra_data.toMap();
            Card *card = Sanguosha->cloneCard(material.value("name").toString(), Card::Suit(material.value("suit").toInt()), material.value("number").toInt());
            if (card) { CardLifetimeLease lease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(card)); card->deleteLater(); card->setSkillName(objectName()); room->useCardFromSkillEffect(CardUseStruct(card, ctx.invoker, target), continuation, false); }
        }
        return false;
    }
};

class TouzhiDis : public DistanceSkillV2
{
public:
    TouzhiDis() : DistanceSkillV2("#touzhi") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.holder || ctx.holder != ctx.primary) return CorrectSkillResult::noEffect();
        const SkillInstance *helper = ctx.holder->findSkillInstance(ctx.instanceRef.key.skillName, ctx.instanceRef.key.instanceID);
        if (!helper || helper->parentRef.key.skillName != "touzhi") return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(-ctx.holder->getSkillInstanceCorrectStateValue("touzhi", helper->parentRef.key.instanceID, "distance").toInt() * ctx.currentAmount);
    }
};

class TouzhiTargetMod : public TargetModSkillV2
{
public:
    TouzhiTargetMod() : TargetModSkillV2("#touzhi-target") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == TargetModSkill::Residue && ctx.card && ctx.card->isKindOf("Slash") && ctx.card->getSkillName() == "touzhi"
            ? CorrectSkillResult::unlimitedResidue() : CorrectSkillResult::noEffect();
    }
};

class YoujiaoViewAsSkill : public ViewAsSkillV2
{
public:
    YoujiaoViewAsSkill() : ViewAsSkillV2("youjiao", 1)
    {
        setResponsePattern("@@youjiao");
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "KeyTrick"; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@youjiao";
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty() && to_select && !to_select->hasFlag("using")
            && request.initiator->handCards().contains(to_select->getEffectiveId()) && to_select->isKindOf("BasicCard");
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        if (!originalCard || !cardSelectionFeasible(request)) return nullptr;
        KeyTrick *yj = new KeyTrick(originalCard->getSuit(), originalCard->getNumber());
        yj->addSubcard(originalCard);
        yj->setSkillName("youjiao");
        return yj;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }
};

class Youjiao : public TriggerSkillV2
{
public:
    Youjiao() : TriggerSkillV2("youjiao") { events << HpLost; view_as_skill = new YoujiaoViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *actor, QVariant &) const override
    {
        TriggerList result;
        if (!actor || !actor->isAlive()) return result;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && actor->getHp() < owner->getHp()) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        if (!ctx.owner || !actor) return false;
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int previous = ctx.owner->getMark(selector);
        auto restore = qScopeGuard([&] { room->setPlayerMark(ctx.owner, selector, previous); });
        room->setPlayerMark(ctx.owner, selector, ctx.activationRef.key.instanceID);
        if (!room->askForUseCard(ctx.owner, "@@youjiao", "@youjiao-use")) return false;
        ctx.targets << ctx.owner << actor;
        room->sortByActionOrder(ctx.targets);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};
class Takamakuri : public TriggerSkillV2
{
public:
    Takamakuri() : TriggerSkillV2("Takamakuri") { events << Damage; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &data) const override
    { return actor && actor->isAlive() && actor->hasSkill(objectName()) && data.value<DamageStruct>().from == actor
        ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << ctx.owner; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (ctx.choice == "discard") {
            if (!target->getEquips().isEmpty() && ctx.owner->canDiscard(target, "e")) {
                const int id = room->askForCardChosen(ctx.owner, target, "e", objectName(), false, Card::MethodDiscard);
                if (room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceEquip && ctx.owner->canDiscard(target, id))
                    room->throwCard(id, target, ctx.owner);
            }
            return false;
        }
        for (int n = 0; n < getEffectiveAmount(ctx) && target->isAlive(); ++n) {
            if (room->getDrawPile().isEmpty()) room->swapPile();
            if (room->getDrawPile().isEmpty()) break;
            const int id = room->getDrawPile().first();
            room->fillAG(QList<int>() << id);
            { auto clear = qScopeGuard([&] { room->clearAG(); }); room->getThread()->delay(800); }
            if (Sanguosha->getCard(id)->isKindOf("BasicCard") && room->getCardPlace(id) == Player::DrawPile) {
                room->broadcastSkillInvoke(objectName()); room->obtainCard(target, id);
                ServerPlayer *victim = ctx.original_data->value<DamageStruct>().to;
                if (victim && victim->isAlive()) { ctx.choice = "discard"; skillEffect(event, room, actor, ctx, victim); ctx.choice.clear(); }
            }
        }
        return false;
    }
};

class Tobiugachi : public TriggerSkillV2
{
public:
    Tobiugachi() : TriggerSkillV2("Tobiugachi") { events << CardAsked; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &) const override
    {
        return actor && actor->isAlive() && actor->hasSkill(objectName()) && actor->getHandcardNum() > actor->getHp()
            && Sanguosha->currentRoomState()->getCurrentCardUsePattern() == "jink"
            ? TriggerList{{actor, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        const int count = ctx.owner->getHandcardNum() - qMax(0, ctx.owner->getHp() - 1);
        if (count <= 0) return false;
        const Card *selected = room->askForExchange(ctx.owner, objectName(), count, count);
        if (!selected || selected->subcardsLength() != count) return false;
        QVariantList ids;
        for (int id : selected->getSubcards()) ids << id;
        ctx.extra_data = ids; ctx.choice = "provide"; ctx.targets << ctx.invoker;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        QList<int> ids;
        for (const QVariant &value : ctx.extra_data.toList()) {
            const int id = value.toInt();
            if (!ctx.owner->handCards().contains(id) || !ctx.owner->canDiscard(ctx.owner, id)) return false;
            ids << id;
        }
        if (ids.isEmpty()) return false;
        DummyCard cards(ids); room->throwCard(&cards, ctx.owner);
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.owner) return false;
        if (ctx.choice == "provide") {
            auto *jink = new Jink(Card::NoSuit, 0); jink->setSkillName(objectName());
            jink->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
            jink->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
            room->provide(jink);
            QList<ServerPlayer *> candidates;
            for (ServerPlayer *p : room->getAlivePlayers()) {
                bool hasCard = !p->isNude() || !p->getJudgingArea().isEmpty();
                for (const QString &pile : p->getPileNames()) hasCard |= !p->getPile(pile).isEmpty();
                if (hasCard) candidates << p;
            }
            if (!candidates.isEmpty()) {
                ServerPlayer *victim = room->askForPlayerChosen(ctx.invoker, candidates, objectName());
                ctx.choice = "extract"; if (victim) skillEffect(event, room, actor, ctx, victim);
            }
            return false;
        }
        if (ctx.choice == "obtain") {
            const QVariantMap receipt = ctx.extra_data.toMap(); const int id = receipt.value("id", -1).toInt();
            ServerPlayer *from = room->findPlayerByObjectName(receipt.value("from").toString());
            if (from && room->getCardOwner(id) == from) room->obtainCard(target, id);
            return false;
        }
        QStringList piles;
        for (const QString &pile : target->getPileNames()) if (!target->getPile(pile).isEmpty()) piles << pile;
        const bool region = !target->isNude() || !target->getJudgingArea().isEmpty();
        QString choice = piles.isEmpty() ? "ToBiGetRegion" : !region ? "TobiGetPile"
            : room->askForChoice(ctx.owner, objectName(), "ToBiGetRegion+TobiGetPile");
        int id = -1;
        if (choice == "TobiGetPile") {
            const QString pile = room->askForChoice(ctx.owner, objectName() + "1", piles.join("+"));
            const QList<int> ids = target->getPile(pile);
            if (ids.isEmpty()) return false;
            room->fillAG(ids, ctx.owner); auto clear = qScopeGuard([&] { room->clearAG(ctx.owner); });
            id = room->askForAG(ctx.owner, ids, false, objectName());
            if (!target->getPile(pile).contains(id)) return false;
        } else if (region) id = room->askForCardChosen(ctx.owner, target, "hej", objectName());
        if (id < 0 || room->getCardOwner(id) != target) return false;
        ctx.extra_data = QVariantMap{{"id", id}, {"from", target->objectName()}}; ctx.choice = "obtain";
        skillEffect(event, room, actor, ctx, ctx.owner);
        return false;
    }
};

class Fukurouza : public TriggerSkillV2
{
public:
    Fukurouza() : TriggerSkillV2("Fukurouza") { events << EventPhaseEnd; }
    static QStringList invoked(Room *room, const Player *owner)
    {
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        if (turn <= 0) return {};
        QVariantMap filter{{"kind", "skill_invoked"}, {"turn_id", turn}, {"player", owner->objectName()}, {"limit", 100}};
        QStringList result;
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            // Accepted invocation facts identify the player without inferring root ownership.
            if (!page.value("complete").toBool() || page.contains("error")) return {};
            for (const QVariant &value : page.value("items").toList()) {
                const QString name = value.toMap().value("data").toMap().value("invoked_skill").toString();
                if ((name == "Tobiugachi" || name == "Takamakuri") && !result.contains(name)) result << name;
            }
            if (!page.value("has_more").toBool()) break;
            filter["watermark"] = page.value("watermark"); filter["after"] = page.value("next_after");
        }
        return result;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *actor, QVariant &) const override
    {
        TriggerList result;
        if (actor && actor->getPhase() == Player::Finish) for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && !invoked(room, owner).isEmpty()) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        if (!ctx.owner || !actor) return false;
        const QStringList used = invoked(room, ctx.owner); QStringList chosen;
        if (used.contains("Tobiugachi") && room->askForSkillInvoke(ctx.owner, objectName() + "Tobi")) chosen << "damage";
        if (used.contains("Takamakuri") && room->askForSkillInvoke(ctx.owner, objectName() + "Taka")) chosen << "draw";
        ctx.choice = chosen.join("+");
        if (chosen.contains("damage")) ctx.targets << actor;
        if (chosen.contains("draw") && !ctx.targets.contains(ctx.owner)) ctx.targets << ctx.owner;
        return !chosen.isEmpty();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        room->broadcastSkillInvoke(objectName());
        if (target == actor && ctx.choice.contains("damage")) room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        if (target == ctx.owner && target->isAlive() && ctx.choice.contains("draw")) target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class Kuisi : public TriggerSkillV2
{
public:
    Kuisi() : TriggerSkillV2("kuisi") { events << Death; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        const DeathStruct death = data.value<DeathStruct>();
        if (!death.damage || !death.damage->from || !death.damage->from->isAlive()) return result;
        for (ServerPlayer *owner : room->getAlivePlayers())
            if (owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        const DeathStruct death = ctx.original_data->value<DeathStruct>();
        if (!death.damage || !death.damage->from) return false;
        ctx.targets << death.damage->from;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    {
        if (target && target->isAlive() && target->getHp() > 0) {
            room->broadcastSkillInvoke(objectName());
            room->doLightbox("kuisi$", 2000);
            room->loseHp(target, target->getHp());
        }
        return false;
    }
};
YouerCard::YouerCard()
{
    setSkillName("youer");
    mute = true;
}

bool YouerCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty()) return false;
    return true;
}

void YouerCard::use(Room *room, ServerPlayer *saki, QList<ServerPlayer *> &targets) const
{
   ServerPlayer *target = targets.at(0);
   room->broadcastSkillInvoke("youer",qsanRandomBounded(2)+2);
   room->setPlayerMark(target, "youer_target", 1);
   foreach(ServerPlayer *p, room->getOtherPlayers(target)){
       room->setPlayerMark(p, "youer_target", 0);
   }
}

class Youervs : public ViewAsSkillV2
{
public:
    Youervs() : ViewAsSkillV2("youer", 0) {}
    QString historyKey(const ActiveSkillRequest &) const override { return "YouerCard"; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *target) const override
    { return target && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return ContinueEffects;
        Room *room = target->getRoom();
        // The printed rule has exactly one bait in the whole room, regardless of skill count.
        room->setTag("YouerEffect", QVariantMap{{"target", target->objectName()}, {"owner", ctx.activationRef.ownerObjectName},
            {"instance", ctx.activationRef.key.instanceID}, {"source_owner", ctx.sourceRef.ownerObjectName},
            {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}});
        for (ServerPlayer *player : room->getAllPlayers()) room->setPlayerMark(player, "youer_target", player == target ? 1 : 0);
        room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) + 2);
        return ContinueEffects;
    }
};

class Youer : public TriggerSkillV2
{
public:
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker ? ctx.invoker : ctx.owner; }
    Youer() : TriggerSkillV2("youer")
    { events << DamageCaused << EventPhaseStart; view_as_skill = new Youervs; global = true; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const QVariantMap receipt = room->getTag("YouerEffect").toMap();
        ServerPlayer *bait = room->findPlayerByObjectName(receipt.value("target").toString());
        if (!bait || !bait->isAlive()) return true;
        if (event == DamageCaused) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (!damage.to || damage.to == bait || damage.damage < damage.to->getHp()) return true;
        } else if (!actor || actor->getPhase() != Player::Play || actor->isKongcheng() || !actor->inMyAttackRange(bait)) return true;
        SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        ctx.initiator = ctx.owner; ctx.invoker = bait; ctx.instanceID = receipt.value("instance").toInt();
        ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
            SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
        ctx.current_event = event; ctx.original_data = &data; ctx.extra_data = receipt;
        ctx.targets << (event == DamageCaused ? bait : actor); contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->isAlive() && room->getTag("YouerEffect") == ctx.extra_data; }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.invoker && room->askForSkillInvoke(ctx.invoker, event == DamageCaused ? objectName() : "youertiaoxin", *ctx.original_data); }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.invoker || !ctx.invoker->isAlive()) return false;
        if (event == DamageCaused) {
            DamageStruct damage = ctx.original_data->value<DamageStruct>(); damage.to = target;
            *ctx.original_data = QVariant::fromValue(damage); room->broadcastSkillInvoke(objectName(), 1);
            return false;
        }
        if (target->isKongcheng() || !ctx.invoker->canDiscard(target, "h")) return false;
        const int id = room->askForCardChosen(ctx.invoker, target, "h", objectName(), false, Card::MethodDiscard);
        if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand || !ctx.invoker->canDiscard(target, id)) return false;
        const Card *material = Sanguosha->getCard(id);
        const bool use = material->isKindOf("Slash") || material->isKindOf("Duel");
        const QString name = material->objectName(); const Card::Suit suit = material->getSuit(); const int number = material->getNumber();
        room->throwCard(id, target, ctx.invoker);
        if (use && target->isAlive() && ctx.invoker->isAlive() && room->getCardPlace(id) == Player::DiscardPile) {
            Card *card = Sanguosha->cloneCard(name, suit, number);
            if (card) {
                CardLifetimeLease lease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(card)); card->deleteLater();
                SkillContext continuation = ctx;
                const QVariantMap receipt = ctx.extra_data.toMap();
                continuation.activationRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(objectName(), receipt.value("instance").toInt()));
                card->addSubcard(id); card->setSkillName(objectName());
                room->useCardFromSkillEffect(CardUseStruct(card, target, ctx.invoker), continuation, true);
            }
        }
        return false;
    }
};

class Baonu : public TriggerSkillV2
{
public:
    Baonu() : TriggerSkillV2("baonu") { events << DrawNCards << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        if (event == EventPhaseChanging && actor && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            actor->removeTag("BaonuEffects"); room->setPlayerMark(actor, "@Baonu", 0);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &) const override
    { return event == DrawNCards && actor && actor->isAlive() && actor->hasSkill(objectName()) && actor->getPhase() == Player::Draw
        ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->askForSkillInvoke(this)) return false;
        ctx.targets << ctx.owner; return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { if (!ctx.owner) return false; room->loseHp(ctx.owner); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.original_data) return false;
        room->broadcastSkillInvoke(objectName());
        QVariantList receipts = target->getTag("BaonuEffects").toList();
        receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
        target->setTag("BaonuEffects", receipts); room->setPlayerMark(target, "@Baonu", receipts.size());
        DrawStruct draw = ctx.original_data->value<DrawStruct>(); draw.num = target->getLostHp() * getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

JizhanCard::JizhanCard()
{
    setSkillName("jizhanshiz");
}

bool JizhanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty()) return false;
    return to_select != Self && Self->inMyAttackRange(to_select) && !to_select->isNude();
}

void JizhanCard::use(Room *room, ServerPlayer *shizuo, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.at(0);
    int id = room->askForCardChosen(shizuo, target, "he", objectName());
    QList<ServerPlayer *> good_targets = room->getOtherPlayers(target);
    good_targets.removeOne(shizuo);
    ServerPlayer *target2 = room->askForPlayerChosen(shizuo, good_targets, "jizhanshiz");
    target2->obtainCard(Sanguosha->getCard(id));
    room->damage(DamageStruct(Sanguosha->getCard(id), shizuo, target2, 1));
    /*
    if (Sanguosha->getCard(id)->isKindOf("EquipCard")){
        if (target2->getEquips().length() > 0){
            room->throwCard(room->askForCardChosen(shizuo, target2, "e", objectName()), target2, shizuo);
        }
    }*/
}

class Jizhanshiz : public ViewAsSkillV2
{
public:
    Jizhanshiz() : ViewAsSkillV2("jizhanshiz", 0) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "JizhanCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->getTag("BaonuEffects").toList().isEmpty(); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return request.initiator && target && selected.isEmpty() && target != request.initiator && request.initiator->inMyAttackRange(target) && !target->isNude(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *actor = ctx.invoker;
        if (!actor || !target || !target->isAlive()) return ContinueEffects;
        Room *room = actor->getRoom();
        if (ctx.choice == "recipient") {
            const QVariantMap receipt = ctx.extra_data.toMap();
            const int id = receipt.value("id", -1).toInt();
            ServerPlayer *from = room->findPlayerByObjectName(receipt.value("from").toString());
            if (!from || room->getCardOwner(id) != from || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return ContinueEffects;
            room->obtainCard(target, id);
            if (target->isAlive()) room->damage(DamageStruct(objectName(), actor, target, getEffectiveAmount(ctx)));
            return ContinueEffects;
        }
        if (target->isNude()) return ContinueEffects;
        QList<ServerPlayer *> recipients = room->getOtherPlayers(target); recipients.removeOne(actor);
        if (recipients.isEmpty()) return ContinueEffects;
        const int id = room->askForCardChosen(actor, target, "he", objectName());
        ServerPlayer *recipient = room->askForPlayerChosen(actor, recipients, objectName());
        if (!recipient || room->getCardOwner(id) != target) return ContinueEffects;
        ctx.choice = "recipient"; ctx.extra_data = QVariantMap{{"id", id}, {"from", target->objectName()}};
        skillEffect(ctx, recipient);
        return ContinueEffects;
    }
};
//3000
class Tianzi : public TriggerSkillV2
{
public:
    Tianzi() : TriggerSkillV2("tianzi") { events << EventPhaseEnd; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &) const override
    {
        return actor && actor->isAlive() && actor->hasSkill(objectName()) && !actor->isNude()
            && (actor->getPhase() == Player::Judge || actor->getPhase() == Player::Draw || actor->getPhase() == Player::Play || actor->getPhase() == Player::Discard)
            ? TriggerList{{actor, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const Card *card = room->askForCard(ctx.owner, "..", "@tianzi-discard", *ctx.original_data, Card::MethodNone);
        if (!card || card->getEffectiveId() < 0 || !ctx.owner->canDiscard(ctx.owner, card->getEffectiveId())) return false;
        ctx.extra_data = QVariantMap{{"id", card->getEffectiveId()}, {"draw", card->isKindOf("BasicCard") ? 1 : 2}};
        ctx.targets << ctx.owner; return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toMap().value("id", -1).toInt();
        if (!ctx.owner || room->getCardOwner(id) != ctx.owner || !ctx.owner->canDiscard(ctx.owner, id)) return false;
        room->throwCard(id, objectName(), ctx.owner); return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive()) { room->broadcastSkillInvoke(objectName()); target->drawCards(ctx.extra_data.toMap().value("draw", 1).toInt() * getEffectiveAmount(ctx), objectName()); }
        return false;
    }
};

class Yuzhai : public TriggerSkillV2
{
public:
    Yuzhai() : TriggerSkillV2("yuzhai") { events << EventPhaseStart; }
    static int discarded(Room *room, const Player *owner)
    {
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong(); if (turn <= 0) return -1;
        QVariantMap filter{{"turn_id", turn}, {"from", owner->objectName()}, {"limit", 100}}; int count = 0;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (!page.value("complete").toBool() || page.contains("error")) return -1;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap move = value.toMap().value("data").toMap();
                if ((move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) ++count;
            }
            if (!page.value("has_more").toBool()) break;
            filter["after"] = page.value("next_after"); filter["watermark"] = page.value("watermark");
        }
        return count;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *actor, QVariant &) const override
    { return actor && actor->isAlive() && actor->hasSkill(objectName()) && actor->getPhase() == Player::Finish && discarded(room, actor) > actor->getHp()
        ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->askForSkillInvoke(this)) return false;
        ctx.extra_data = qMax(0, discarded(room, ctx.owner) - ctx.owner->getHp()); return ctx.extra_data.toInt() > 0;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        const int count = ctx.extra_data.toInt() * getEffectiveAmount(ctx);
        for (int i = 0; i < count && ctx.owner->isAlive(); ++i) {
            QList<ServerPlayer *> candidates;
            for (ServerPlayer *p : room->getOtherPlayers(ctx.owner)) if (ctx.owner->canDiscard(p, "he")) candidates << p;
            if (candidates.isEmpty()) break;
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName());
            if (!target) break;
            skillEffect(event, room, actor, ctx, target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive() && ctx.owner->canDiscard(target, "he")) {
            const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodDiscard);
            if (room->getCardOwner(id) == target && ctx.owner->canDiscard(target, id)) room->throwCard(id, target, ctx.owner);
        }
        return false;
    }
};

class Qinshi : public TriggerSkillV2
{
public:
    Qinshi() : TriggerSkillV2("qinshi") { events << GameStart << Death << EventPhaseEnd; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        if (event == Death) {
            const DeathStruct death = data.value<DeathStruct>();
            actor = death.damage ? death.damage->from : nullptr;
        }
        return actor && actor->isAlive() && actor->hasSkill(objectName())
            && (event != EventPhaseEnd || actor->getPhase() == Player::Finish) ? TriggerList{{actor, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.owner; return ctx.owner != nullptr; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        const int amount = getEffectiveAmount(ctx);
        if (event == GameStart) {
            const int count = room->getAllPlayers().size() * amount;
            room->setPlayerProperty(target, "maxhp", target->getMaxHp() + count); room->recover(target, RecoverStruct(objectName(), ctx.owner, count));
        } else if (event == EventPhaseEnd) { room->broadcastSkillInvoke(objectName()); room->loseHp(target, amount); }
        else { room->broadcastSkillInvoke(objectName()); room->recover(target, RecoverStruct(objectName(), ctx.owner, amount)); }
        return false;
    }
};

class Kangfen : public TriggerSkillV2
{
public:
    Kangfen() : TriggerSkillV2("kangfen") { events << EventPhaseEnd; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *actor, QVariant &) const override
    {
        TriggerList result;
        if (!actor || actor->getPhase() != Player::Finish) return result;
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toULongLong() == 0) return result;
        for (ServerPlayer *owner : room->getAlivePlayers()) {
            if (owner == actor || !owner->hasSkill(objectName())) continue;
            const QVariantMap damage = room->queryActualDamage({{"turn_id", turn}, {"to", owner->objectName()}, {"limit", 1}});
            if (damage.contains("error") || !damage.value("complete").toBool()) continue;
            if (damage.value("items").toList().isEmpty()) result[owner] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!ctx.owner || !ctx.owner->askForSkillInvoke(this)) return false; ctx.targets << ctx.invoker; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *recipient) const override
    {
        if (recipient && recipient->isAlive()) {
            room->broadcastSkillInvoke(objectName());
            // Preserve the immediate nested turn and its accepted skill provenance.
            for (int i = 0; i < getEffectiveAmount(ctx) && recipient->isAlive(); ++i)
                room->executeExtraTurn(recipient, QList<Player::Phase>(), objectName(), ctx.sourceRef);
        }
        return false;
    }
};
class Xiedou : public ViewAsSkillV2
{
public:
    Xiedou() : ViewAsSkillV2("xiedou")
    {
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getHandcardNum() > request.initiator->getEquips().length()
            && request.initiator->getEquips().length() > 0;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        return request.initiator && request.selectedCardIds.length() < request.initiator->getHandcardNum() - request.initiator->getEquips().length()
            && to_select && !request.initiator->isJilei(to_select) && !to_select->hasFlag("using") && !request.selectedCardIds.contains(to_select->getEffectiveId())
            && request.initiator->handCards().contains(to_select->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.isEmpty() || request.selectedCardIds.size() != request.initiator->getHandcardNum() - request.initiator->getEquips().size()) return false;
        ActiveSkillRequest prefix = request; prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) { if (!canSelectCard(prefix, Sanguosha->getCard(id))) return false; prefix.selectedCardIds << id; }
        return true;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Duel"; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        if (!request.initiator || !target) return false;
        auto *duel = new Duel(Card::NoSuit, 0);
        CardLifetimeLease lease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(duel));
        duel->deleteLater();
        return duel->targetFilter(selected, target, request.initiator) && !request.initiator->isProhibited(target, duel, selected);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int i = 0; i < getEffectiveAmount(ctx) && ctx.invoker && ctx.invoker->isAlive() && target && target->isAlive(); ++i) {
            // Selected hand cards are discarded by the active payment pipeline.
            auto *duel = new Duel(Card::NoSuit, 0); CardLifetimeLease duelLease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(duel)); duel->deleteLater(); duel->setSkillName(objectName());
            ctx.invoker->getRoom()->useCardFromSkillEffect(CardUseStruct(duel, ctx.invoker, target), ctx, false);
        }
        return ContinueEffects;
    }
};

TaxianCard::TaxianCard()
{
    setSkillName("taxian");
}
bool TaxianCard::targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const
{
    return to_select != Self && Self->inMyAttackRange(to_select) && Self->canSlash(to_select);
}

bool TaxianCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() > 0;
}

void TaxianCard::use(Room *room, ServerPlayer *ayanami, QList<ServerPlayer *> &targets) const
{
    ThunderSlash *slash = new ThunderSlash(Card::NoSuit, 0);
    if (targets.length() >= 3){
        slash->setSkillName("taxian");
    }

    room->useCard(CardUseStruct(slash, ayanami, targets));
    foreach(ServerPlayer *p , targets){
        if (p->inMyAttackRange(ayanami)){
            Slash *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName("taxian");
            room->useCard(CardUseStruct(slash, p, ayanami));
        }
    }
}

class TaxianVs : public ViewAsSkillV2
{
public:
    TaxianVs() : ViewAsSkillV2("taxian", 0) {}
    QString historyKey(const ActiveSkillRequest &) const override { return "TaxianCard"; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        return request.initiator && target && !selected.contains(target) && target != request.initiator
            && request.initiator->inMyAttackRange(target) && request.initiator->canSlash(target, false);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return !targets.isEmpty(); }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.invoker || ctx.targets.isEmpty()) return FinishSkill;
        Room *room = ctx.invoker->getRoom();
        const QList<ServerPlayer *> declared = ctx.targets;
        ctx.extra_data = QStringList(); ctx.choice = "strike";
        for (ServerPlayer *target : declared) if (target && target->isAlive()) skillEffect(ctx, target);
        QList<ServerPlayer *> accepted;
        for (const QString &name : ctx.extra_data.toStringList()) if (ServerPlayer *target = room->findPlayerByObjectName(name)) accepted << target;
        if (accepted.isEmpty()) return FinishSkill;
        ctx.is_canceled = false;
        auto *slash = new ThunderSlash(Card::NoSuit, 0); CardLifetimeLease slashLease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(slash)); slash->deleteLater();
        slash->setSkillName(objectName());
        CardUseStruct use(slash, ctx.invoker, accepted);
        if (accepted.size() >= 3) {
            ctx.choice = "norespond"; ctx.extra_data = QStringList();
            for (ServerPlayer *responder : room->getAlivePlayers()) skillEffect(ctx,responder);
            use.no_respond_list = ctx.extra_data.toStringList();
        }
        ctx.is_canceled = false;
        room->useCardFromSkillEffect(use, ctx, false);
        ctx.choice = "retaliate";
        for (ServerPlayer *target : accepted) if (target->isAlive()) skillEffect(ctx, target);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        if (ctx.choice == "strike" || ctx.choice == "norespond") {
            if (target && target->isAlive()) { QStringList names = ctx.extra_data.toStringList(); names << target->objectName(); ctx.extra_data = names; }
            return ContinueEffects;
        }
        if (ctx.choice == "counterattack") {
            ServerPlayer *attacker = target->getRoom()->findPlayerByObjectName(ctx.extra_data.toString());
            if (!attacker || attacker->isDead() || !attacker->inMyAttackRange(target) || !attacker->canSlash(target,false)) return ContinueEffects;
            auto *slash = new Slash(Card::NoSuit, 0); CardLifetimeLease slashLease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(slash)); slash->deleteLater(); slash->setSkillName(objectName());
            target->getRoom()->useCardFromSkillEffect(CardUseStruct(slash,attacker,target),ctx,false); return ContinueEffects;
        }
        if (ctx.invoker && ctx.invoker->isAlive() && target && target->isAlive()
            && target->inMyAttackRange(ctx.invoker) && target->canSlash(ctx.invoker, false)) {
            SkillContext strike = ctx; strike.choice = "counterattack"; strike.extra_data = target->objectName(); skillEffect(strike,ctx.invoker);
        }
        return ContinueEffects;
    }
};
class Taxian : public TaxianVs
{
public:
    Taxian() : TaxianVs() {}
};
class Guishen : public TriggerSkillV2
{
public:
    Guishen() : TriggerSkillV2("guishen") { events << EventPhaseEnd; frequency = Compulsory; }
    static int damage(Room *room, const Player *owner)
    {
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong(); if (turn <= 0) return -1;
        QVariantMap filter{{"turn_id", turn}, {"from", owner->objectName()}, {"limit", 100}}; int amount = 0;
        for (;;) {
            const QVariantMap page = room->queryActualDamage(filter);
            if (!page.value("complete").toBool() || page.contains("error")) return -1;
            for (const QVariant &value : page.value("items").toList()) amount += value.toMap().value("data").toMap().value("amount").toInt();
            if (!page.value("has_more").toBool()) break;
            filter["after"] = page.value("next_after"); filter["watermark"] = page.value("watermark");
        }
        return amount;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *actor, QVariant &) const override
    { return actor && actor->isAlive() && actor->hasSkill(objectName()) && actor->getPhase() == Player::Finish && damage(room, actor) >= actor->getHp()
        ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { if (!ctx.owner) return false; ctx.extra_data = damage(room, ctx.owner); ctx.targets << ctx.owner; return ctx.extra_data.toInt() >= 0; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        room->recover(target, RecoverStruct(objectName(), ctx.owner, qMax(0, ctx.extra_data.toInt() - target->getHp()) * getEffectiveAmount(ctx)));
        if (target->isAlive()) target->drawCards(qMax(0, target->getHp()) * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class Jianjin : public TriggerSkillV2
{
public:
    Jianjin() : TriggerSkillV2("jianjin") { events << EventSkillInvoking << EventPhaseStart << EventPhaseEnd << Damaged; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext active = data.value<SkillContext>();
            if (active.bypass_cost && active.skill_name == objectName() && active.activationRef.key.skillName == objectName()) addUsage(active);
        }
        return false;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    { return ctx.owner && !ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, (ctx.choice.isEmpty() ? (ctx.owner->getPhase() == Player::NotActive ? QString("outside") : QString("inside")) : ctx.choice)).toBool(); }
    void addUsage(const SkillContext &ctx) const override
    { if (ctx.owner) ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, (ctx.choice.isEmpty() ? (ctx.owner->getPhase() == Player::NotActive ? QString("outside") : QString("inside")) : ctx.choice), true); }
    void record(TriggerEvent event, Room *, ServerPlayer *actor, SkillContext &ctx) const override
    {
        if (!ctx.owner || actor != ctx.owner) return;
        if (event == EventPhaseStart && actor->getPhase() == Player::RoundStart) actor->setSkillInstanceStateValue(objectName(), ctx.instanceID, "inside", false);
        if (event == EventPhaseEnd && actor->getPhase() == Player::Finish) actor->setSkillInstanceStateValue(objectName(), ctx.instanceID, "outside", false);
    }
    static int total(Room *room)
    {
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong(); if (turn <= 0) return -1;
        int result = 0; QVariant watermark;
        for (const QString &kind : {QString("actual_damage"), QString("actual_recover")}) {
            QVariantMap filter{{"turn_id", turn}, {"kind", kind}, {"limit", 100}};
            if (watermark.isValid()) filter["watermark"] = watermark;
            for (;;) {
                const QVariantMap page = kind == "actual_damage" ? room->queryActualDamage(filter) : room->queryHistoryFacts(filter);
                if (!page.value("complete").toBool() || page.contains("error")) return -1;
                if (!watermark.isValid()) watermark = page.value("watermark");
                for (const QVariant &value : page.value("items").toList()) result += value.toMap().value("data").toMap().value("amount").toInt();
                if (!page.value("has_more").toBool()) break;
                filter["after"] = page.value("next_after"); filter["watermark"] = watermark;
            }
        }
        return qMin(3, result);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
    {
        if (event == EventSkillInvoking) return {};
        TriggerList result; if (event != Damaged || total(room) <= 0) return result;
        for (ServerPlayer *owner : room->getAlivePlayers()) if (owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.choice = ctx.owner->getPhase() == Player::NotActive ? "outside" : "inside";
        const DamageStruct damage = ctx.original_data->value<DamageStruct>(); QList<ServerPlayer *> candidates;
        if (damage.from && damage.from->isAlive()) candidates << damage.from;
        if (damage.to && damage.to->isAlive() && !candidates.contains(damage.to)) candidates << damage.to;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName());
        if (!target) return false; ctx.targets << target; ctx.extra_data = total(room); return ctx.extra_data.toInt() > 0;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { Q_UNUSED(ctx); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { if (target && target->isAlive()) { room->broadcastSkillInvoke(objectName()); target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName()); } return false; }
};

class Faka : public TriggerSkillV2
{
public:
    Faka() : TriggerSkillV2("faka") { events << CardsMoveOneTime << HpRecover; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    { if (ctx.current_event == HpRecover) ctx.is_forced = true; return TriggerSkillV2::prepareSource(room, ctx); }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        ServerPlayer *current = room->getCurrent();
        if (!current || !current->isAlive() || current->getPhase() == Player::NotActive) return {};
        ServerPlayer *owner = actor;
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (!move.to || move.from == move.to || (move.to_place != Player::PlaceHand && move.to_place != Player::PlaceEquip)) return {};
            owner = room->findPlayerByObjectName(move.to->objectName());
        }
        return owner && owner->isAlive() && owner != current && owner->hasSkill(objectName()) ? TriggerList{{owner, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *current = room->getCurrent();
        if (!ctx.owner || !current || !current->isAlive() || current == ctx.owner) return false;
        if (event == HpRecover) { ctx.targets << current; return true; }
        if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QList<ServerPlayer *> candidates; candidates << current;
        if (move.from && move.from->isAlive()) {
            ServerPlayer *from = room->findPlayerByObjectName(move.from->objectName());
            if (from && !candidates.contains(from)) candidates << from;
        }
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName());
        if (!target) return false;
        ctx.targets << target; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        room->broadcastSkillInvoke(objectName());
        if (event == HpRecover) { target->turnOver(); if (target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName()); }
        else {
            const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>(); QList<int> ids;
            for (int id : move.card_ids) if (room->getCardOwner(id) == ctx.owner
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) ids << id;
            if (ids.isEmpty()) return false;
            DummyCard cards(ids); room->obtainCard(target, &cards,
                CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.owner->objectName(), target->objectName(), objectName(), ""), true);
            if (target->isAlive()) room->damage(DamageStruct(objectName(), ctx.invoker, target, ids.size() * getEffectiveAmount(ctx)));
        }
        return false;
    }
};

NingjuCard::NingjuCard()
{
    setSkillName("ningju");
    mute = true;
}
bool NingjuCard::targetFilter(const QList<const Player *> &, const Player *, const Player *) const
{
    return true;
}

void NingjuCard::use(Room *room, ServerPlayer *chiaki, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.at(0);
    QList<int> card_ids;
    foreach(ServerPlayer *player, room->getAlivePlayers()){
        if (player->inMyAttackRange(target)){
            player->drawCards(1);

        }
    }
    int num = 0;

    QString status = "None";
    room->setTag("ningju_color", QVariant(status));


    foreach(ServerPlayer *player, room->getAlivePlayers()){
        if (player->inMyAttackRange(target)){
            int id = room->askForCardChosen(player, player, "he", "ningju");
            if (chiaki->getMark("@waked") > 0){
                room->obtainCard(chiaki, id);
                num += 1;
            }
            else{
                if (status == "None"){
                    status = Sanguosha->getCard(id)->isRed() ? "Red" : "Black";
                }
                else if (status == "Red"){
                    status = Sanguosha->getCard(id)->isRed() ? "Red" : "Mix";
                }
                else if (status == "Black"){
                    status = Sanguosha->getCard(id)->isRed() ? "Mix" : "Black";
                }
                room->setTag("ningju_color", QVariant(status));
                room->throwCard(id, player, player);
                card_ids.append(id);
            }

        }
    }

    if (chiaki->getMark("@waked") > 0){
        status = "None";
        for (int i = 0; i < num; i++){
            int id2 = room->askForCardChosen(chiaki, chiaki, "he", "ningju");
            if (status == "None"){
                status = Sanguosha->getCard(id2)->isRed() ? "Red" : "Black";
            }
            else if (status == "Red"){
                status = Sanguosha->getCard(id2)->isRed() ? "Red" : "Mix";
            }
            else if (status == "Black"){
                status = Sanguosha->getCard(id2)->isRed() ? "Mix" : "Black";
            }
            room->setTag("ningju_color", QVariant(status));
            room->throwCard(id2, chiaki, chiaki);
            card_ids.append(id2);
        }
    }

    room->setTag("ningju_color", QVariant("None"));
    if (card_ids.length() == 0){
        return;
    }
    QList<Card::Color> colors;
    foreach(int card_id, card_ids){
        Card::Color color = Sanguosha->getCard(card_id)->getColor();
        if (!colors.contains(color)){
            colors.append(color);
        }
    }
    if (colors.length() == 1){
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("ningju_slash");
        room->broadcastSkillInvoke("ningju", 1);
        if (chiaki->canSlash(target, false)){
            room->doAnimate(QSanProtocol::S_ANIMATE_LIGHTBOX, "lani=skills/zhinian", QString("%1:%2").arg(1000).arg(0));
            room->useCard(CardUseStruct(slash, chiaki, target));
        }
    }
    else{
        room->broadcastSkillInvoke("ningju", 2);
    }
}


class Ningju : public ViewAsSkillV2
{
public:
    Ningju() : ViewAsSkillV2("ningju", 0) {}
    QString historyKey(const ActiveSkillRequest &) const override { return "NingjuCard"; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 3; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *target) const override
    { return target && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.invoker || ctx.targets.size() != 1) return FinishSkill;
        Room *room = ctx.invoker->getRoom(); ServerPlayer *victim = ctx.targets.first(); QList<ServerPlayer *> participants;
        for (ServerPlayer *p : room->getOtherPlayers(ctx.invoker)) if (p->inMyAttackRange(victim)) participants << p;
        ctx.extra_data = QVariantMap{{"awakened", !ctx.invoker->getTag("NingjuAwakening").toMap().isEmpty()}, {"received", 0}, {"colors", QVariantList()}};
        ctx.choice = "draw";
        for (ServerPlayer *p : participants) if (p->isAlive()) skillEffect(ctx, p);
        ctx.choice = "contribute";
        for (ServerPlayer *p : participants) if (p->isAlive()) { skillEffect(ctx, p); ctx.choice = "contribute"; }
        if (ctx.extra_data.toMap().value("awakened").toBool() && ctx.invoker->isAlive()) { ctx.choice = "discard"; skillEffect(ctx, ctx.invoker); }
        const QVariantList colors = ctx.extra_data.toMap().value("colors").toList();
        bool same = !colors.isEmpty(); for (const QVariant &color : colors) same &= color == colors.first();
        if (same && ctx.invoker->isAlive() && victim->isAlive() && ctx.invoker->canSlash(victim, false)) {
            ctx.choice = "slash"; skillEffect(ctx, victim);
        } else room->broadcastSkillInvoke(objectName(), 2);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.invoker || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom(); QVariantMap state = ctx.extra_data.toMap();
        if (ctx.choice == "slash") {
            auto *slash = new Slash(Card::NoSuit, 0); CardLifetimeLease slashLease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(slash)); slash->deleteLater(); slash->setSkillName("ningju_slash");
            room->broadcastSkillInvoke(objectName(), 1); room->useCardFromSkillEffect(CardUseStruct(slash, ctx.invoker, target), ctx, false);
            return ContinueEffects;
        }
        if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return ContinueEffects; }
        if (ctx.choice == "obtain") {
            const int id = state.value("id", -1).toInt();
            ServerPlayer *from = room->findPlayerByObjectName(state.value("from").toString());
            if (from && room->getCardOwner(id) == from && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) {
                room->obtainCard(target, id, false); state["received"] = state.value("received").toInt() + 1; ctx.extra_data = state;
            }
            return ContinueEffects;
        }
        const bool give = state.value("awakened").toBool() && ctx.choice == "contribute";
        const int count = ctx.choice == "discard" ? state.value("received").toInt() : getEffectiveAmount(ctx);
        for (int i = 0; i < count && target->isAlive() && !target->isNude(); ++i) {
            if (!give && !target->canDiscard(target, "he")) break;
            const int id = room->askForCardChosen(target, target, "he", objectName(), false, give ? Card::MethodNone : Card::MethodDiscard);
            if (room->getCardOwner(id) != target || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) continue;
            if (give) {
                state = ctx.extra_data.toMap(); state["id"] = id; state["from"] = target->objectName(); ctx.extra_data = state;
                ctx.choice = "obtain"; skillEffect(ctx, ctx.invoker); ctx.choice = "contribute";
            } else if (target->canDiscard(target, id)) {
                const int color = int(Sanguosha->getCard(id)->getColor()); room->throwCard(id, target, target);
                state = ctx.extra_data.toMap(); QVariantList colors = state.value("colors").toList(); colors << color; state["colors"] = colors; ctx.extra_data = state;
            }
        }
        return ContinueEffects;
    }
};

class Zhinian : public TriggerSkillV2
{
public:
    Zhinian() : TriggerSkillV2("zhinian") { frequency = Wake; events << EventSkillInvoking << AskForPeachesDone; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext active = data.value<SkillContext>();
            if (active.bypass_cost && active.skill_name == objectName() && active.activationRef.key.skillName == objectName()) addUsage(active);
        }
        return false;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        if (event == EventSkillInvoking) return {}; return actor && actor->isAlive() && actor->hasSkill(objectName()) && actor->getHp() < 1 && actor->getMaxHp() > 0 && data.value<DyingStruct>().who == actor
        ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.owner; return ctx.owner != nullptr; }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { Q_UNUSED(ctx); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        room->broadcastSkillInvoke(objectName()); room->doLightbox("zhinian$", 2500);
        room->recover(target, RecoverStruct(objectName(), ctx.owner, qMax(0, 3 - target->getHp()) * getEffectiveAmount(ctx)));
        target->gainMark("@waked");
        target->setTag("NingjuAwakening", QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}});
        room->acquireSkill(target, "chengxu");
        return false;
    }
};

class Chengxu : public TriggerSkillV2
{
public:
    Chengxu() : TriggerSkillV2("chengxu") { frequency = Compulsory; events << DamageInflicted << EventPhaseEnd; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &) const override
    { return actor && actor->isAlive() && actor->hasSkill(objectName()) && (event == DamageInflicted || actor->getPhase() == Player::Finish)
        ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.owner; return ctx.owner != nullptr; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        room->broadcastSkillInvoke(objectName());
        if (event == DamageInflicted) return true;
        room->loseMaxHp(target, getEffectiveAmount(ctx)); return false;
    }
};

namespace {
ServerPlayer *liveAuraReceipt(Room *room, const QString &tag, const QString &skill)
{
    const QVariantMap receipt = room->getTag(tag).toMap();
    ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
    return owner && owner->isAlive() && receipt.value("skill", skill).toString() == skill
        && owner->hasSkillInstance(skill, receipt.value("instance").toInt()) ? owner : nullptr;
}

void clearXingjianAura(Room *room, ServerPlayer *owner, bool dead)
{
    if (!room || !owner) return;
    const QVariantMap receipt = room->getTag("XingjianAuraSource").toMap();
    if (receipt.value("owner").toString() != owner->objectName()
        || (!dead && owner->hasSkillInstance("xingjian", receipt.value("instance").toInt()))) return;
    room->removeTag("XingjianAuraSource");
    // Removing an unrelated copy must not remove the established aura component.
    if (room->getAura() == "MacrossF") {
        if (ServerPlayer *sher = liveAuraReceipt(room, "YaojingAuraSource", "yaojing")) {
            room->doAura(sher, "yaojing");
            return;
        }
    }
    if (room->getAura() == "xingjian" || room->getAura() == "MacrossF") room->clearAura();
}
}

class Xingjian : public TriggerSkillV2
{
public:
    Xingjian() : TriggerSkillV2("xingjian")
    {
        events << EventPhaseStart << Death << EventLoseSkill;
        frequency = Wake;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == Death) clearXingjianAura(room, data.value<DeathStruct>().who, true);
        else if (event == EventLoseSkill) clearXingjianAura(room, player, false);
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive()) return {};
        if (player->getPhase() == Player::Play && player->hasSkill(objectName()))
            return {{player, {objectName()}}};
        if (player->getPhase() != Player::RoundStart || player->getEquips().isEmpty()
            || (room->getAura() != objectName() && room->getAura() != "MacrossF")) return {};
        ServerPlayer *owner = liveAuraReceipt(room, "XingjianAuraSource", objectName());
        if (!owner || owner == player) return {};
        const int id = room->getTag("XingjianAuraSource").toMap().value("instance").toInt();
        return {{owner, {SkillInstanceUtils::formatName(objectName(), id)}}};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !actor) return false;
        if (actor->getPhase() == Player::Play && actor == ctx.owner) {
            if (room->hasAura() && (room->getAura() == objectName() || room->getAura() == "MacrossF"
                || (room->getAuraPlayer() && room->getAuraPlayer()->getHp() < actor->getHp()))) return false;
            if (!ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data)) return false;
        } else {
            if (actor->getPhase() != Player::RoundStart || actor == ctx.owner || actor->getEquips().isEmpty()) return false;
            ctx.choice = room->askForChoice(actor, objectName(), "xingjian_skip+xingjian_throw", *ctx.original_data);
        }
        ctx.targets << actor;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *actor) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !owner->isAlive() || !actor) return false;
        if (ctx.choice == "obtain") {
            const QVariantMap receipt = ctx.extra_data.toMap();
            ServerPlayer *from = room->findPlayerByObjectName(receipt.value("from").toString());
            const int id = receipt.value("id").toInt();
            if (from && room->getCardOwner(id) == from && (room->getCardPlace(id) == Player::PlaceEquip || room->getCardPlace(id) == Player::PlaceHand)) {
                room->obtainCard(actor, id);
                if (receipt.value("skip").toBool()) actor->skip(Player::Draw);
            }
            return false;
        }
        if (ctx.choice == "extract") {
            ServerPlayer *recipient = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString());
            ctx.choice = "obtain"; if (recipient) skillEffect(EventPhaseStart, room, ctx.invoker, ctx, recipient); return false;
        }
        if (actor == owner) {
            room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) * 2 + 1);
            const bool combine = room->getAura() == "yaojing"
                && liveAuraReceipt(room, "YaojingAuraSource", "yaojing");
            if (room->doAura(owner, combine ? "MacrossF" : objectName()))
                room->setTag("XingjianAuraSource", QVariantMap{{"owner", ctx.activationRef.ownerObjectName},
                    {"skill", ctx.activationRef.key.skillName}, {"instance", ctx.activationRef.key.instanceID},
                    {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}});
        } else {
            room->broadcastSkillInvoke(objectName(), 2);
            if (ctx.choice == "xingjian_throw" && !actor->getEquips().isEmpty()) {
                const int id = room->askForCardChosen(owner, actor, "e", objectName());
                if (room->getCardOwner(id) == actor && room->getCardPlace(id) == Player::PlaceEquip) {
                    ctx.extra_data = QVariantMap{{"from", actor->objectName()}, {"id", id}};
                    ctx.choice = "obtain"; skillEffect(EventPhaseStart, room, ctx.invoker, ctx, owner);
                }
            } else if (ctx.choice == "xingjian_skip" && !owner->isNude()) {
                const int id = room->askForCardChosen(actor, owner, "he", objectName());
                if (room->getCardOwner(id) == owner) {
                    ctx.extra_data = QVariantMap{{"from", owner->objectName()}, {"id", id}, {"recipient", actor->objectName()}, {"skip", true}};
                    ctx.choice = "extract"; skillEffect(EventPhaseStart, room, ctx.invoker, ctx, owner);
                }
            }
        }
        return false;
    }
};

class XingjianClear : public DetachEffectSkill
{
public:
    XingjianClear() : DetachEffectSkill("xingjian") {}
    void onSkillDetached(Room *room, ServerPlayer *player) const override
    { clearXingjianAura(room, player, false); }
};
class Goutong : public TriggerSkillV2
{
public:
    Goutong() : TriggerSkillV2("goutong") { events << CardsMoveOneTime; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (event != CardsMoveOneTime || !move.from || !move.to || move.from == move.to
            || (!move.from_places.contains(Player::PlaceHand) && !move.from_places.contains(Player::PlaceEquip))
            || (move.to_place != Player::PlaceHand && move.to_place != Player::PlaceEquip)) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner == move.from || owner == move.to) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && ctx.original_data && ctx.owner->askForSkillInvoke(this, *ctx.original_data);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        room->broadcastSkillInvoke(objectName());
        for (const Player *recipient : {move.from, move.to}) {
            if (!recipient) continue;
            ServerPlayer *target = room->findPlayerByObjectName(recipient->objectName());
            if (target) ctx.targets << target;
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->recover(target, RecoverStruct(objectName(), ctx.owner, getEffectiveAmount(ctx)));
        if (target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};
class Jianshi : public TriggerSkillV2
{
public:
    Jianshi() : TriggerSkillV2("jianshi") { events << CardsMoveOneTime << Death; frequency = Compulsory; global = true; }
    static void clear(Room *room, const QString &ownerName, int instance = 0)
    {
        QVariantList kept, removed;
        for (const QVariant &value : room->getTag("JianshiEffects").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("owner").toString() == ownerName && (instance == 0 || receipt.value("instance").toInt() == instance)) removed << value;
            else kept << value;
        }
        room->setTag("JianshiEffects", kept);
        for (const QVariant &value : removed) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *target = room->findPlayerByObjectName(receipt.value("target").toString(), true);
            if (!target) continue;
            for (const QString &name : receipt.value("observers").toStringList()) {
                bool retained = false;
                for (const QVariant &remaining : kept) retained |= remaining.toMap().value("target") == receipt.value("target")
                    && remaining.toMap().value("observers").toStringList().contains(name);
                if (!retained) if (ServerPlayer *observer = room->findPlayerByObjectName(name, true)) releaseInovationAkarin(room, target, observer);
            }
            int count = 0; for (const QVariant &remaining : kept) count += remaining.toMap().value("target").toString() == target->objectName();
            room->setPlayerMark(target, "@Jianshi_akarin", count);
        }
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    { if (event == Death && data.value<DeathStruct>().who) clear(room, data.value<DeathStruct>().who->objectName()); return false; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (event != CardsMoveOneTime) return result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place == Player::DrawPile) return result;
        bool found = false;
        for (int i = 0; i < move.card_ids.size(); ++i)
            found |= move.from_places.value(i) == Player::DrawPile && Sanguosha->getCard(move.card_ids.at(i))->isKindOf("KeyTrick");
        if (found) for (ServerPlayer *owner : room->getAlivePlayers()) if (owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.owner; return ctx.owner != nullptr; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.owner || !ctx.original_data) return false;
        if (ctx.choice == "vanish") {
            clear(room, ctx.activationRef.ownerObjectName, ctx.activationRef.key.instanceID);
            QStringList observers;
            for (ServerPlayer *observer : room->getOtherPlayers(target)) if (observer != ctx.owner) {
                observers << observer->objectName(); rememberInovationAkarin(room, target, observer); room->akarinPlayer(target, observer);
            }
            QVariantList receipts = room->getTag("JianshiEffects").toList();
            receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID},
                {"target", target->objectName()}, {"observers", observers}, {"source_owner", ctx.sourceRef.ownerObjectName},
                {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
            room->setTag("JianshiEffects", receipts); room->addPlayerMark(target, "@Jianshi_akarin"); return false;
        }
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QList<int> ids; int key = -1;
        for (int i = 0; i < move.card_ids.size(); ++i) {
            const int id = move.card_ids.at(i);
            if (room->getCardPlace(id) != move.to_place || room->getCardOwner(id) != move.to) continue;
            ids << id;
            if (move.from_places.value(i) == Player::DrawPile && Sanguosha->getCard(id)->isKindOf("KeyTrick")) key = id;
        }
        if (key < 0 || ids.isEmpty()) return false;
        const Card::Suit suit = Sanguosha->getCard(key)->getSuit(); const int number = Sanguosha->getCard(key)->getNumber();
        DummyCard cards(ids); room->obtainCard(target, &cards); room->broadcastSkillInvoke(objectName());
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *p : room->getOtherPlayers(target)) if (p->hasJudgeArea() && !p->containsTrick("key_trick")) candidates << p;
        if (candidates.isEmpty() || room->getCardOwner(key) != target || room->getCardPlace(key) != Player::PlaceHand) return false;
        ServerPlayer *recipient = room->askForPlayerChosen(target, candidates, objectName());
        if (!recipient || !recipient->isAlive() || !target->handCards().contains(key)) return false;
        auto *card = new KeyTrick(suit, number); CardLifetimeLease cardLease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(card)); card->deleteLater(); card->addSubcard(key); card->setSkillName(objectName());
        room->useCardFromSkillEffect(CardUseStruct(card, target, recipient), ctx, false);
        ctx.choice = "vanish"; skillEffect(event, room, actor, ctx, recipient);
        return false;
    }
};

class JianshiClear : public DetachEffectSkill
{
public:
    JianshiClear() : DetachEffectSkill("jianshi") {}
    // The printed duration ends on death or the next activation, not loss of the skill.
    void onSkillDetached(Room *, ServerPlayer *) const override {}
};

class Qiyue : public TriggerSkillV2
{
public:
    Qiyue() : TriggerSkillV2("qiyue") { events << AskForPeachesDone << BeforeCardsMove << SwappedPile << GameStart; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        if (event == GameStart && actor) actor->setTag("QiyueInitialMaxHp", actor->getMaxHp());
        bool empty = event == SwappedPile;
        if (event == BeforeCardsMove) empty = data.value<CardsMoveOneTimeStruct>().from_places.contains(Player::DrawPile) && room->getDrawPile().isEmpty();
        if (empty) {
            QVariantList executions = room->getTag("QiyueExecutions").toList();
            for (QVariant &value : executions) { QVariantMap receipt = value.toMap(); receipt["empty"] = true; value = receipt; }
            room->setTag("QiyueExecutions", executions);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (event != AskForPeachesDone || !data.value<DyingStruct>().who || data.value<DyingStruct>().who->getHp() > 0) return result;
        for (ServerPlayer *owner : room->getAlivePlayers()) if (owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->askForSkillInvoke(this, *ctx.original_data); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        const qint64 token = room->getTag("QiyueSequence").toLongLong() + 1; room->setTag("QiyueSequence", token);
        QVariantList executions = room->getTag("QiyueExecutions").toList();
        executions << QVariantMap{{"execution", token}, {"empty", false}}; room->setTag("QiyueExecutions", executions);
        auto clear = qScopeGuard([&] {
            QVariantList kept; for (const QVariant &value : room->getTag("QiyueExecutions").toList())
                if (value.toMap().value("execution").toULongLong() != token) kept << value;
            room->setTag("QiyueExecutions", kept);
        });
        ctx.extra_data = qMax(0, 5 - ctx.owner->getMaxHp()) * getEffectiveAmount(ctx);
        room->broadcastSkillInvoke(objectName()); room->doLightbox("qiyue$", 2000);
        const QList<ServerPlayer *> players = room->getAlivePlayers();
        ctx.choice = "draw"; for (ServerPlayer *p : players) if (p->isAlive()) skillEffect(event, room, actor, ctx, p);
        ctx.choice = "discard"; for (ServerPlayer *p : players) if (p->isAlive()) skillEffect(event, room, actor, ctx, p);
        bool empty = false;
        for (const QVariant &value : room->getTag("QiyueExecutions").toList()) if (value.toMap().value("execution").toULongLong() == token) empty = value.toMap().value("empty").toBool();
        ctx.choice = empty ? "restore" : "lose"; skillEffect(event, room, actor, ctx, ctx.owner);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (ctx.choice == "draw") target->drawCards(ctx.extra_data.toInt(), objectName());
        else if (ctx.choice == "discard") {
            for (int i = 0; i < ctx.extra_data.toInt() && target->isAlive() && target->canDiscard(target, "hej"); ++i) {
                const int id = room->askForCardChosen(target, target, "hej", objectName(), false, Card::MethodDiscard);
                if (room->getCardOwner(id) == target && target->canDiscard(target, id)) room->throwCard(id, target, target);
            }
        } else if (ctx.choice == "restore") room->setPlayerProperty(target, "maxhp", target->getTag("QiyueInitialMaxHp", target->isLord() && room->getAllPlayers(true).size() > 4 ? 4 : 3).toInt());
        else room->loseMaxHp(target, getEffectiveAmount(ctx));
        return false;
    }
};

class Nangua : public TriggerSkillV2
{
public:
    Nangua() : TriggerSkillV2("nangua") { events << EnterDying << HpRecover; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &) const override
    { return actor && actor->isAlive() && actor->hasSkill(objectName()) && (event == EnterDying || actor->getHp() <= 1)
        ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        if (event == HpRecover) ctx.choice = room->askForChoice(ctx.owner, objectName(), "nangua_recover+nangua_turnover", *ctx.original_data);
        ctx.targets << ctx.owner; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        room->broadcastSkillInvoke(objectName());
        if (event == EnterDying) target->drawCards(target->getMaxHp() * getEffectiveAmount(ctx), objectName());
        else if (ctx.choice == "nangua_turnover") target->turnOver();
        else if (target->getHp() < 1) room->setPlayerProperty(target, "hp", 1);
        return false;
    }
};

class InovationJixian : public TriggerSkillV2
{
public:
    InovationJixian() : TriggerSkillV2("inovation_jixian") { events << EventPhaseEnd << AskForPeachesDone; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    { return actor && actor->isAlive() && actor->hasSkill(objectName()) && actor->canDiscard(actor, "he")
        && (event == EventPhaseEnd ? actor->getPhase() == Player::Finish : data.value<DyingStruct>().who == actor)
        ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        const Card *card = room->askForCard(ctx.owner, "..", objectName(), *ctx.original_data, Card::MethodNone);
        if (!card || card->getEffectiveId() < 0 || !ctx.owner->canDiscard(ctx.owner, card->getEffectiveId())) return false;
        ctx.extra_data = card->getEffectiveId();
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName());
        if (!target) return false; ctx.targets << target; return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (!ctx.owner || room->getCardOwner(id) != ctx.owner || !ctx.owner->canDiscard(ctx.owner, id)) return false;
        room->throwCard(id, objectName(), ctx.owner); return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.owner) return false;
        if (ctx.choice == "lose") {
            room->detachSkillFromPlayer(ctx.owner, SkillInstanceUtils::formatName(objectName(), ctx.activationRef.key.instanceID));
            room->loseHp(target, getEffectiveAmount(ctx)); return false;
        }
        const int amount = (ctx.owner->getLostHp() + 1) * getEffectiveAmount(ctx);
        room->broadcastSkillInvoke(objectName(), amount > 2 ? 1 : 2);
        const QVariantMap before = room->queryHistoryFacts({{"limit", 1}});
        const qint64 skillEvent = room->historyParent(room->currentHistoryEventId(), "skill", true).value("id").toLongLong();
        room->damage(DamageStruct(objectName(), ctx.invoker, target, amount));
        if (skillEvent <= 0 || before.contains("error") || !before.value("complete").toBool()) return false;
        QVariantMap filter{{"from", ctx.invoker->objectName()}, {"after", before.value("watermark")}, {"limit", 100}}; int actual = 0;
        for (;;) {
            const QVariantMap page = room->queryActualDamage(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return false;
            for (const QVariant &value : page.value("items").toList())
                if (room->historyParent(value.toMap().value("event_id").toLongLong(), "skill", true).value("id").toLongLong() == skillEvent)
                    actual += value.toMap().value("data").toMap().value("amount").toInt();
            if (!page.value("has_more").toBool()) break;
            filter["after"] = page.value("next_after"); filter["watermark"] = page.value("watermark");
        }
        if (actual > 2 && ctx.owner->isAlive()) { ctx.choice = "lose"; skillEffect(event, room, actor, ctx, ctx.owner); }
        return false;
    }
};

class Yandan : public TriggerSkillV2
{
public:
    Yandan() : TriggerSkillV2("yandan") { events << CardsMoveOneTime << Death; frequency = Frequent; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
    {
        if (event != Death) return false;
        QVariantMap filter{{"kind", "death"}, {"limit", 100}}; int count = 0;
        for (;;) {
            const QVariantMap page = room->queryHistoryEvents(filter);
            if (!page.value("complete").toBool() || page.contains("error")) return false;
            count += page.value("items").toList().size();
            if (!page.value("has_more").toBool()) break;
            filter["after"] = page.value("next_after"); filter["watermark"] = page.value("watermark");
        }
        for (ServerPlayer *p : room->getAllPlayers()) room->setPlayerMark(p, "yandan_death", count);
        return false;
    }
    static QList<int> cards(TriggerEvent event, const QVariant &data, Room *room)
    {
        QList<int> result;
        if (event == Death) {
            ServerPlayer *dead = data.value<DeathStruct>().who;
            if (dead) { result = dead->handCards(); for (const Card *card : dead->getEquips()) result << card->getEffectiveId(); }
        } else {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (!move.from || move.from->getPhase() != Player::NotActive || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) return result;
            for (int i = 0; i < move.card_ids.size(); ++i) if ((move.from_places.value(i) == Player::PlaceHand || move.from_places.value(i) == Player::PlaceEquip)
                && room->getCardPlace(move.card_ids.at(i)) == Player::DiscardPile) result << move.card_ids.at(i);
        }
        return result;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result; if (cards(event, data, room).isEmpty()) return result;
        for (ServerPlayer *owner : room->getAlivePlayers()) if (owner->hasSkill(objectName())
            && (event == Death ? data.value<DeathStruct>().who != owner : owner->getPile("Yandan").size() <= owner->getMaxHp())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        const QList<int> ids = cards(event, *ctx.original_data, room); if (ids.isEmpty()) return false;
        room->fillAG(ids, ctx.owner); auto clear = qScopeGuard([&] { room->clearAG(ctx.owner); });
        const int id = room->askForAG(ctx.owner, ids, true, objectName()); if (!ids.contains(id)) return false;
        ctx.extra_data = id; ctx.targets << ctx.owner; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive() && cards(event, *ctx.original_data, room).contains(ctx.extra_data.toInt())) {
            room->broadcastSkillInvoke(objectName()); target->addToPile("Yandan", ctx.extra_data.toInt());
        }
        return false;
    }
};

class YandanMaxCards : public MaxCardsSkillV2
{
public:
    YandanMaxCards() : MaxCardsSkillV2("#yandan") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    { return ctx.holder ? CorrectSkillResult::useAmount((int(!ctx.holder->getPile("Yandan").isEmpty()) + ctx.holder->getMark("yandan_death")) * ctx.currentAmount) : CorrectSkillResult::noEffect(); }
};

class YandanClear : public DetachEffectSkill
{
public:
    YandanClear() : DetachEffectSkill("yandan") {}
    void onSkillDetached(Room *, ServerPlayer *owner) const override { if (owner->getSkillInstanceIds("yandan").isEmpty()) owner->clearOnePrivatePile("Yandan"); }
};

class Xiwang : public TriggerSkillV2
{
public:
    Xiwang() : TriggerSkillV2("xiwang") { events << EventSkillInvoking << EventPhaseStart; frequency = Wake; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext active = data.value<SkillContext>();
            if (active.bypass_cost && active.skill_name == objectName() && active.activationRef.key.skillName == objectName()) addUsage(active);
        }
        return false;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &) const override
    {
        if (event == EventSkillInvoking) return {}; return actor && actor->isAlive() && actor->hasSkill(objectName()) && actor->getPhase() == Player::RoundStart && actor->getPile("Yandan").size() > actor->getHp()
        ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.owner; return ctx.owner != nullptr; }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { Q_UNUSED(ctx); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        room->broadcastSkillInvoke(objectName()); room->doLightbox("lunpo$", 2000); room->loseMaxHp(target, getEffectiveAmount(ctx));
        if (target->isAlive()) { target->drawCards(getEffectiveAmount(ctx), objectName()); target->gainMark("@waked"); room->acquireSkill(target, "lunpo"); }
        return false;
    }
};

class Lunpo : public TriggerSkillV2
{
public:
    Lunpo() : TriggerSkillV2("lunpo") { events << EventPhaseStart << EventPhaseChanging << Death << CardUsed; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const bool end = event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive;
        ServerPlayer *dead = event == Death ? data.value<DeathStruct>().who : nullptr;
        if (!end && !dead) return false;
        for (ServerPlayer *target : room->getAllPlayers()) {
            QVariantList kept;
            for (const QVariant &value : target->getTag("LunpoEffects").toList())
                if (!end && value.toMap().value("owner").toString() != dead->objectName()) kept << value;
            const int removed = target->getTag("LunpoEffects").toList().size() - kept.size();
            target->setTag("LunpoEffects", kept); room->setPlayerMark(target, "lunpo", kept.size());
            if (removed > 0) room->removePlayerMark(target, "@skill_invalidity", removed);
        }
        return false;
    }
    static QList<int> matching(const Player *owner, const Card *card)
    {
        QList<int> ids; if (!owner || !card) return ids;
        for (int id : owner->getPile("Yandan")) if (Sanguosha->getCard(id)->getSuit() == card->getSuit()) ids << id;
        return ids;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && actor && actor->isAlive() && actor->getPhase() == Player::Play && actor->hasSkill(objectName())) result[actor] << objectName();
        else if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || use.card->isKindOf("EquipCard")) return result;
            for (ServerPlayer *owner : room->getAlivePlayers()) if (owner->getPhase() == Player::NotActive && owner->hasSkill(objectName()) && !matching(owner, use.card).isEmpty()) result[owner] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        QList<int> available; int count = 1;
        if (event == EventPhaseStart) {
            count = ctx.owner->getHp(); for (ServerPlayer *p : room->getAlivePlayers()) count = qMin(count, p->getHp()); count = qMax(0, count);
            available = ctx.owner->getPile("Yandan");
        } else available = matching(ctx.owner, ctx.original_data->value<CardUseStruct>().card);
        if (available.size() < count || !room->askForSkillInvoke(ctx.owner, event == EventPhaseStart ? "lunpo_inturn" : objectName(), *ctx.original_data)) return false;
        QVariantList chosen;
        for (int n = 0; n < count; ++n) {
            room->fillAG(available, ctx.owner); auto clear = qScopeGuard([&] { room->clearAG(ctx.owner); });
            const int id = room->askForAG(ctx.owner, available, false, objectName()); if (!available.removeOne(id)) return false; chosen << id;
        }
        ctx.extra_data = chosen;
        if (event == EventPhaseStart) ctx.targets = room->getAlivePlayers();
        else { const CardUseStruct use = ctx.original_data->value<CardUseStruct>(); ctx.targets = use.to; if (ctx.targets.isEmpty() && use.from) ctx.targets << use.from; }
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<int> ids;
        for (const QVariant &value : ctx.extra_data.toList()) { const int id = value.toInt(); if (!ctx.owner->getPile("Yandan").contains(id)) return false; ids << id; }
        if (!ids.isEmpty()) { DummyCard cards(ids); room->throwCard(&cards, ctx.owner); }
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (event == EventPhaseStart) {
            QVariantList receipts = target->getTag("LunpoEffects").toList();
            receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
            target->setTag("LunpoEffects", receipts); room->setPlayerMark(target, "lunpo", receipts.size()); room->addPlayerMark(target, "@skill_invalidity");
        } else {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.nullified_list.contains(target->objectName())) use.nullified_list << target->objectName();
            *ctx.original_data = QVariant::fromValue(use);
        }
        return false;
    }
};

class LunpoInvalidity : public InvaliditySkill
{
public:
    LunpoInvalidity() : InvaliditySkill("#lunpo-inv") {}
    bool isSkillValid(const Player *player, const Skill *skill) const override
    { return player->getMark("lunpo") == 0 || skill->getFrequency() == Skill::Compulsory || skill->getFrequency() == Skill::Wake || skill->isAttachedLordSkill(); }
};

class Xinyang : public TriggerSkillV2
{
public:
    Xinyang() : TriggerSkillV2("xinyang") { events << ShowCards << StartJudge; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
    {
        TriggerList result;
        for (ServerPlayer *owner : room->getAlivePlayers()) if (owner->hasSkill(objectName()) && (event == ShowCards || !owner->getPile("xinyang").isEmpty())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        if (!ctx.owner || !room->askForSkillInvoke(ctx.owner, event == ShowCards ? objectName() : "xinyang_judge", *ctx.original_data)) return false;
        ctx.targets << (event == ShowCards ? ctx.owner : actor);
        if (event == StartJudge) {
            const QList<int> ids = ctx.owner->getPile("xinyang"); room->fillAG(ids, ctx.owner); auto clear = qScopeGuard([&] { room->clearAG(ctx.owner); });
            const int id = room->askForAG(ctx.owner, ids, true, objectName()); if (!ids.contains(id)) return false; ctx.extra_data = id;
        }
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (event == ShowCards) target->addToPile("xinyang", room->getNCards(getEffectiveAmount(ctx)));
        else if (ctx.owner->getPile("xinyang").contains(ctx.extra_data.toInt())) room->moveCardTo(Sanguosha->getCard(ctx.extra_data.toInt()), ctx.owner, nullptr,
            Player::DrawPile, CardMoveReason(CardMoveReason::S_REASON_PUT, ctx.owner->objectName(), objectName(), ""), true);
        return false;
    }
};

class XinyangClear : public DetachEffectSkill
{
public:
    XinyangClear() : DetachEffectSkill("xinyang") {}
    void onSkillDetached(Room *, ServerPlayer *owner) const override { if (owner->getSkillInstanceIds("xinyang").isEmpty()) owner->clearOnePrivatePile("xinyang"); }
};

InovationFengzhuCard::InovationFengzhuCard()
{
    setSkillName("inovation_fengzhu");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool InovationFengzhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = nullptr;
        if (!user_string.isEmpty())
            card = Sanguosha->cloneCard(user_string.split("+").first());
        if (!card) return false;
        CardLifetimeLease lease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(card));
        card->deleteLater();
        return card && card->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, card, targets);
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *_card = Self ? Self->getTag("inovation_fengzhu").value<const Card *>() : nullptr;
    Card *card = _card ? Sanguosha->cloneCard(_card->objectName(), Card::NoSuit, 0)
        : Sanguosha->cloneCard(user_string.split("+").first());
    if (card == NULL)
        return false;
    CardLifetimeLease lease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(card));
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, card, targets);
}

bool InovationFengzhuCard::targetFixed() const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = nullptr;
        if (!user_string.isEmpty())
            card = Sanguosha->cloneCard(user_string.split("+").first());
        if (!card) return false;
        CardLifetimeLease lease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(card));
        card->deleteLater();
        return card && card->targetFixed();
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *_card = Self ? Self->getTag("inovation_fengzhu").value<const Card *>() : nullptr;
    Card *card = _card ? Sanguosha->cloneCard(_card->objectName(), Card::NoSuit, 0)
        : Sanguosha->cloneCard(user_string.split("+").first());
    if (card == NULL)
        return false;
    CardLifetimeLease lease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(card));
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetFixed();
}

bool InovationFengzhuCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = nullptr;
        if (!user_string.isEmpty())
            card = Sanguosha->cloneCard(user_string.split("+").first());
        if (!card) return false;
        CardLifetimeLease lease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(card));
        card->deleteLater();
        return card && card->targetsFeasible(targets, Self);
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *_card = Self ? Self->getTag("inovation_fengzhu").value<const Card *>() : nullptr;
    Card *card = _card ? Sanguosha->cloneCard(_card->objectName(), Card::NoSuit, 0)
        : Sanguosha->cloneCard(user_string.split("+").first());
    if (card == NULL)
        return false;
    CardLifetimeLease lease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(card));
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetsFeasible(targets, Self);
}

// Legacy serialized card IDs are rebuilt by the V2 declaration entry before validation.
const Card *InovationFengzhuCard::validate(CardUseStruct &) const { return nullptr; }
const Card *InovationFengzhuCard::validateInResponse(ServerPlayer *) const { return nullptr; }

class InovationFengzhuVS : public ViewAsSkillV2
{
public:
    InovationFengzhuVS() : ViewAsSkillV2("inovation_fengzhu", 0) { setResponseOrUse(true); }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, false); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && !request.initiator->isKongcheng()
            && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) && !usableNames(request).isEmpty();
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.choice = "pending";
        if (ctx.invoker && ctx.invoker->isAlive()) skillEffect(ctx, ctx.invoker);
        // Failed judgement and a cancelled self target consume the accepted attempt but produce no basic card.
        if (ctx.choice != "success") { ctx.is_canceled = true; return FinishSkill; }
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || target->isKongcheng() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        const Card *shown = room->askForCardShow(target, target, objectName());
        if (!shown || !target->handCards().contains(shown->getEffectiveId())) return ContinueEffects;
        const int id = shown->getEffectiveId(); const QString suit = shown->getSuitString();
        room->showCard(target, id);
        if (!target->isAlive()) return ContinueEffects;
        JudgeStruct judge; judge.reason = objectName(); judge.who = target; judge.pattern = ".|" + suit; judge.good = true; room->judge(judge);
        if (judge.isGood()) ctx.choice = "success";
        else if (judge.card && target->isAlive() && (room->getCardPlace(judge.card->getEffectiveId()) == Player::DiscardPile
            || room->getCardPlace(judge.card->getEffectiveId()) == Player::PlaceJudge)) room->obtainCard(target, judge.card->getEffectiveId());
        return ContinueEffects;
    }
};

class InovationFengzhu : public TriggerSkillV2
{
public:
    InovationFengzhu() : TriggerSkillV2("inovation_fengzhu") { view_as_skill = new InovationFengzhuVS; }
    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, false); }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};

class Zuzhou : public TriggerSkillV2
{
public:
    Zuzhou() : TriggerSkillV2("inovation_zuzhou") { frequency = Compulsory; events << TargetConfirmed << EventPhaseEnd << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            for (ServerPlayer *target : room->getAllPlayers()) { target->removeTag("ZuzhouEffects"); room->setPlayerMark(target, "@inovation_zuzhou", 0); }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        TriggerList result;
        if (event == TargetConfirmed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (actor && actor->isAlive() && actor->hasSkill(objectName()) && use.to.contains(actor) && use.from && use.from != actor) result[actor] << objectName();
        } else if (event == EventPhaseEnd && actor && actor->getPhase() == Player::Discard && actor->getMaxCards() <= 1)
            for (ServerPlayer *owner : room->getAlivePlayers()) if (owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!ctx.owner) return false; ctx.targets << (event == TargetConfirmed ? ctx.original_data->value<CardUseStruct>().from : ctx.owner); return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        room->broadcastSkillInvoke(objectName());
        if (event == EventPhaseEnd) target->drawCards(getEffectiveAmount(ctx), objectName());
        else {
            const int amount = qMax(1, ctx.owner->getLostHp()) * getEffectiveAmount(ctx);
            QVariantList receipts = target->getTag("ZuzhouEffects").toList();
            receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID}, {"amount", amount},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
            target->setTag("ZuzhouEffects", receipts); room->addPlayerMark(target, "@inovation_zuzhou", amount);
        }
        return false;
    }
};

class ZuzhouClear : public DetachEffectSkill
{
public:
    ZuzhouClear() : DetachEffectSkill("inovation_zuzhou") {}
    void onSkillDetached(Room *, ServerPlayer *) const override {}
};

class ZuzhouMaxCards : public MaxCardsSkillV2
{
public:
    ZuzhouMaxCards() : MaxCardsSkillV2("#inovation_zuzhou") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    { return ctx.primary ? CorrectSkillResult::useAmount(-ctx.primary->getMark("@inovation_zuzhou") * ctx.currentAmount) : CorrectSkillResult::noEffect(); }
};

JiguanCard::JiguanCard()
{
    setSkillName("jiguan");
    target_fixed = true;
}

void JiguanCard::use(Room *room, ServerPlayer *fear, QList<ServerPlayer *> &) const
{
    fear->drawCards(1);
    QList<int> ids;
    foreach(const Card* card, fear->getHandcards()){
        if (card->isBlack()){
            ids.append(card->getId());
        }
    }
    foreach(const Card* card, fear->getEquips()){
        if (card->isBlack()){
            ids.append(card->getId());
        }
    }

    for (int i = 0; i < 2; i++){
        if (ids.length() > 0){
            if (room->askForChoice(fear, "jiguan", "jiguan_put+jiguan_pass") == "jiguan_put"){
                room->fillAG(ids, fear);
                int id = room->askForAG(fear, ids, true, objectName());
                room->clearAG(fear);
                if (id != -1){
                    ids.removeOne(id);
                    fear->addToPile("jiguan", id, false);
                }
                else{
                    break;
                }
            }
            else{
                break;
            }

        }
    }

}

class JiguanVS : public ViewAsSkillV2
{
public:
    JiguanVS() : ViewAsSkillV2("jiguan") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "JiguanCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    TargetMode targetMode() const override { return NoTarget; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.invoker) skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return ContinueEffects;
        Room *room = target->getRoom();
        target->drawCards(getEffectiveAmount(ctx), objectName());
        for (int n = 0; n < 2 * getEffectiveAmount(ctx) && target->isAlive(); ++n) {
            QList<int> ids;
            for (const Card *card : target->getCards("he")) if (card->isBlack() && !card->hasFlag("using")) ids << card->getEffectiveId();
            if (ids.isEmpty() || room->askForChoice(target, objectName(), "jiguan_put+jiguan_pass") != "jiguan_put") break;
            room->fillAG(ids, target);
            auto clear = qScopeGuard([&] { room->clearAG(target); });
            const int id = room->askForAG(target, ids, true, objectName());
            if (!ids.contains(id)) break;
            if (room->getCardOwner(id) == target) target->addToPile("jiguan", id, false);
        }
        return ContinueEffects;
    }
};
class Jiguan : public TriggerSkillV2
{
public:
    Jiguan() : TriggerSkillV2("jiguan") { view_as_skill = new JiguanVS; events << CardUsed; }
    static QList<int> matching(const Player *owner, const Card *card)
    { QList<int> ids; if (card) for (int id : owner->getPile("jiguan")) if (Sanguosha->getCard(id)->getNumber() == card->getNumber()) ids << id; return ids; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result; const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.from || !use.from->isAlive()) return result;
        for (ServerPlayer *owner : room->getAlivePlayers()) if (owner->hasSkill(objectName()) && !matching(owner, use.card).isEmpty()) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>(); const QList<int> ids = matching(ctx.owner, use.card);
        if (ids.isEmpty()) return false;
        room->fillAG(ids, ctx.owner); auto clear = qScopeGuard([&] { room->clearAG(ctx.owner); });
        const int id = room->askForAG(ctx.owner, ids, true, objectName()); if (!ids.contains(id)) return false;
        ctx.extra_data = id;
        ServerPlayer *target = use.from == ctx.owner ? room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName()) : use.from;
        if (!target) return false; ctx.targets << target; return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt(); if (!ctx.owner->getPile("jiguan").contains(id)) return false;
        room->showCard(ctx.owner, id); if (!ctx.owner->getPile("jiguan").contains(id)) return false;
        room->throwCard(id, ctx.owner, ctx.owner); return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { if (target && target->isAlive()) { room->broadcastSkillInvoke(objectName()); room->loseHp(target, getEffectiveAmount(ctx)); } return false; }
};

class JiguanClear : public DetachEffectSkill
{
public:
    JiguanClear() : DetachEffectSkill("jiguan") {}
    void onSkillDetached(Room *, ServerPlayer *owner) const override { if (owner->getSkillInstanceIds("jiguan").isEmpty()) owner->clearOnePrivatePile("jiguan"); }
};

//misaka mikoto
PaojiCard::PaojiCard()
{
    setSkillName("paoji");
    mute = true;
}

bool PaojiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty()) return false;
    return true;
}

void PaojiCard::use(Room *room, ServerPlayer *mikoto, QList<ServerPlayer *> &targets) const
{
   ServerPlayer *target = targets.at(0);
   room->broadcastSkillInvoke("paoji");
   Card *sub = Sanguosha->getCard(this->getSubcards().at(0));
   Card *card = Sanguosha->cloneCard("thunder_slash",sub->getSuit(), sub->getNumber());
   card->addSubcard(sub);
   room->useCard(CardUseStruct(card, mikoto, target));
}

class Paojivs : public ViewAsSkillV2
{
public:
    Paojivs() : ViewAsSkillV2("paoji", 1) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "ThunderSlash"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isKongcheng(); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && !card->hasFlag("using")
            && request.initiator->handCards().contains(card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest prefix = request; prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool willThrowSelectedCards() const override { return false; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        // The ordinary conversion owns its material movement and retains printed suit/number.
        auto *slash = new ThunderSlash(material->getSuit(), material->getNumber());
        slash->addSubcard(material); slash->setSkillName(objectName());
        return slash;
    }
};
class Paoji : public TriggerSkillV2
{
public:
    Paoji() : TriggerSkillV2("paoji") { events << GameStart << CardUsed << DamageCaused; global = true; view_as_skill = new Paojivs; }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker ? ctx.invoker : ctx.owner; }
    void record(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        if (event == GameStart && actor == ctx.owner) {
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "coins", 4);
            int coins = 0; for (int id : ctx.owner->getSkillInstanceIds(objectName())) coins += ctx.owner->getSkillInstanceStateValue(objectName(), id, "coins").toInt();
            room->setPlayerMark(ctx.owner, "@ying", coins);
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    { return event == CardUsed && actor && actor->isAlive() && actor->hasSkill(objectName()) && data.value<CardUseStruct>().card
        && data.value<CardUseStruct>().card->isKindOf("ThunderSlash") ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != DamageCaused) return false;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.to || damage.chain || damage.transfer) return true;
        for (const QVariant &value : damage.card->getTag("PaojiEffects").toList()) {
            const QVariantMap receipt = value.toMap(); SkillContext ctx; ctx.skill_name = objectName();
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true); ctx.initiator = ctx.owner; ctx.invoker = actor;
            ctx.instanceID = receipt.value("instance").toInt(); ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.amount = receipt.value("amount", 1).toInt(); ctx.extra_data = receipt; ctx.current_event = event; ctx.original_data = &data; ctx.targets << damage.to; ctx.is_forced = true; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        return damage.card && damage.card->getTag("PaojiEffects").toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == DamageCaused) return true;
        if (!ctx.owner || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.choice = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "coins").toInt() > 0 ? "judge" : "last";
        ctx.targets << ctx.owner; return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == DamageCaused || ctx.choice != "judge") return true;
        const int coins = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "coins").toInt(); if (coins <= 0) return false;
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "coins", coins - 1); room->removePlayerMark(ctx.owner, "@ying"); return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (event == DamageCaused) { DamageStruct damage = ctx.original_data->value<DamageStruct>(); damage.damage += getEffectiveAmount(ctx); *ctx.original_data = QVariant::fromValue(damage); return false; }
        CardUseStruct use = ctx.original_data->value<CardUseStruct>(); if (!use.card) return false;
        if (ctx.choice == "add_target") {
            if (!use.to.contains(target) && ctx.invoker && ctx.invoker->canSlash(target, use.card, false)) {
                use.to << target; room->sortByActionOrder(use.to); *ctx.original_data = QVariant::fromValue(use);
            }
            return false;
        }
        room->broadcastSkillInvoke(objectName()); bool success = ctx.choice == "last";
        if (ctx.choice == "judge") { JudgeStruct judge; judge.pattern = ".|black"; judge.good = true; judge.reason = objectName(); judge.who = target; room->judge(judge); success = judge.card && judge.card->isBlack(); }
        if (!success) return false;
        QVariantList receipts = use.card->getTag("PaojiEffects").toList();
        receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
        use.card->setTag("PaojiEffects", receipts);
        if (ctx.choice == "last") {
            QList<ServerPlayer *> candidates;
            for (ServerPlayer *p : room->getAlivePlayers()) if (!use.to.contains(p) && target->canSlash(p, use.card, false)) candidates << p;
            while (!candidates.isEmpty()) {
                ServerPlayer *extra = room->askForPlayerChosen(target, candidates, "paoji_addtargets", QString(), true);
                if (!extra) break;
                candidates.removeOne(extra); ctx.choice = "add_target"; skillEffect(event, room, ctx.invoker, ctx, extra); ctx.choice = "last";
                use = ctx.original_data->value<CardUseStruct>();
            }
            room->sortByActionOrder(use.to); *ctx.original_data = QVariant::fromValue(use);
            room->detachSkillFromPlayer(ctx.owner, SkillInstanceUtils::formatName(objectName(), ctx.activationRef.key.instanceID));
        }
        return false;
    }
};

class Dianci : public TriggerSkillV2
{
public:
    Dianci() : TriggerSkillV2("inovation_dianci") { events << EventPhaseStart << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !actor || data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        for (ServerPlayer *target : room->getAllPlayers()) {
            target->removeTag("DianciProhibitions"); room->setPlayerProperty(target, "dianci_prohibited_targets", QStringList()); QVariantList kept;
            for (const QVariant &value : target->getTag("DianciSuitEffects").toList()) {
                const QVariantMap receipt = value.toMap();
                if (receipt.value("owner").toString() == actor->objectName() && receipt.value("turn").toLongLong() != turn)
                    room->removePlayerCardLimitationByReason(target, receipt.value("reason").toString());
                else kept << value;
            }
            target->setTag("DianciSuitEffects", kept);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && actor && actor->isAlive() && actor->getPhase() == Player::RoundStart && !actor->isKongcheng())
            for (ServerPlayer *owner : room->getAlivePlayers()) if (owner->hasSkill(objectName()) && owner->distanceTo(actor) <= 1) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *actor, SkillContext &ctx) const override
    { if (!ctx.owner || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false; ctx.targets << actor; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.owner || !ctx.owner->isAlive()) return false;
        if (ctx.choice == "chain") { room->setPlayerProperty(target, "chained", true); return false; }
        if (ctx.choice == "obtain" || ctx.choice == "give" || ctx.choice == "kill") {
            const QVariantMap receipt = ctx.extra_data.toMap(); const int id = receipt.value("id", -1).toInt();
            ServerPlayer *from = room->findPlayerByObjectName(receipt.value("from").toString());
            if (!from || !from->handCards().contains(id)) return false;
            if (ctx.choice == "kill") {
                auto *slash = new Slash(Card::Suit(receipt.value("suit").toInt()), receipt.value("number").toInt()); CardLifetimeLease slashLease(globalCardLifetimeManager(), globalCardLifetimeManager().observeCard(slash)); slash->deleteLater(); slash->addSubcard(id); slash->setSkillName(objectName());
                room->useCardFromSkillEffect(CardUseStruct(slash, ctx.invoker, target), ctx, false); return false;
            }
            room->obtainCard(target, id, false);
            if (ctx.choice == "obtain") {
                QVariantList prohibitions = from->getTag("DianciProhibitions").toList();
                prohibitions << QVariantMap{{"target", target->objectName()}, {"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID}};
                from->setTag("DianciProhibitions", prohibitions);
                QStringList names; for (const QVariant &value : prohibitions) names << value.toMap().value("target").toString();
                room->setPlayerProperty(from, "dianci_prohibited_targets", names);
            } else {
                const qint64 serial = room->getTag("DianciSequence").toLongLong() + 1; room->setTag("DianciSequence", serial);
                const QString reason = objectName() + ":" + QString::number(serial);
                const QString pattern = ".|" + receipt.value("suit_name").toString() + "|.|hand";
                QVariantList effects = target->getTag("DianciSuitEffects").toList();
                effects << QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID}, {"reason", reason},
                    {"turn", room->historyScopes().value("turn_id")}, {"source_owner", ctx.sourceRef.ownerObjectName},
                    {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
                target->setTag("DianciSuitEffects", effects); room->setPlayerCardLimitation(target, "discard,use,response", pattern, false, reason);
            }
            return false;
        }
        if (target->isKongcheng()) return false;
        const int id = room->askForCardChosen(ctx.owner, target, "h", objectName());
        if (!target->handCards().contains(id)) return false;
        const Card *material = Sanguosha->getCard(id);
        const QVariantMap receipt{{"id", id}, {"from", target->objectName()}, {"suit", int(material->getSuit())}, {"number", material->getNumber()}, {"suit_name", material->getSuitString()}};
        const bool black = material->isBlack(); room->showCard(target, id, ctx.owner);
        if (!black || !target->handCards().contains(id)) return false;
        const QString choice = room->askForChoice(ctx.owner, objectName(), "inovation_dianci_obtain+inovation_dianci_kill+inovation_dianci_chain+inovation_dianci_give");
        ctx.extra_data = receipt; room->broadcastSkillInvoke(objectName());
        if (choice == "inovation_dianci_obtain") { ctx.choice = "obtain"; skillEffect(event, room, actor, ctx, ctx.owner); }
        else if (choice == "inovation_dianci_chain") {
            if (!target->handCards().contains(id)) return false;
            room->moveCardTo(Sanguosha->getCard(id), target, nullptr, Player::DrawPile, CardMoveReason(CardMoveReason::S_REASON_PUT, ctx.owner->objectName(), objectName(), ""), false);
            QList<ServerPlayer *> candidates = room->getAlivePlayers(); ctx.choice = "chain";
            for (int i = 0; i < 2 * getEffectiveAmount(ctx) && !candidates.isEmpty(); ++i) {
                ServerPlayer *chosen = room->askForPlayerChosen(ctx.owner, candidates, objectName(), QString(), true); if (!chosen) break;
                candidates.removeOne(chosen); skillEffect(event, room, actor, ctx, chosen);
            }
        } else {
            QList<ServerPlayer *> candidates;
            for (ServerPlayer *p : room->getAlivePlayers())
                if (choice == "inovation_dianci_kill" ? ctx.owner->canSlash(p, false) : p != target) candidates << p;
            if (candidates.isEmpty()) return false;
            ServerPlayer *chosen = room->askForPlayerChosen(ctx.owner, candidates, objectName());
            ctx.choice = choice == "inovation_dianci_kill" ? "kill" : "give";
            if (chosen) skillEffect(event, room, actor, ctx, chosen);
        }
        return false;
    }
};

class DianciProhibit : public ProhibitSkill
{
public:
    DianciProhibit() : ProhibitSkill("#inovation_dianci") {}
    bool isProhibited(const Player *from, const Player *to, const Card *, const QList<const Player *> &) const override
    {
        if (from && to && from->property("dianci_prohibited_targets").toStringList().contains(to->objectName())) return true;
        return false;
    }
};

class Shuji : public TriggerSkillV2
{
public:
    Shuji() : TriggerSkillV2("shuji") { events << CardsMoveOneTime << EventPhaseStart << EventPhaseChanging << EventLoseSkill; global = true; }
    static bool available(const Player *owner, int id, Room *room)
    {
        const Card *card = Sanguosha->getCard(id);
        if (!card || !card->isKindOf("TrickCard") || room->getCardPlace(id) != Player::DiscardPile || owner->getPile("huanshu").size() >= 9) return false;
        for (int book : owner->getPile("huanshu")) if (Sanguosha->getCard(book)->getClassName() == card->getClassName()) return false;
        return true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        if (!actor || (event != EventLoseSkill && !(event == EventPhaseChanging && data.value<PhaseChangeStruct>().from == Player::Discard))) return false;
        QVariantList kept;
        for (const QVariant &value : actor->getTag("ShujiLimitations").toList()) {
            const QVariantMap receipt = value.toMap();
            if (event == EventPhaseChanging || !actor->hasSkillInstance(objectName(), receipt.value("instance").toInt())) room->removePlayerCardLimitationByReason(actor, receipt.value("reason").toString());
            else kept << value;
        }
        actor->setTag("ShujiLimitations", kept); return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &) const override
    { return event == EventPhaseStart && actor && actor->isAlive() && actor->hasSkill(objectName()) && actor->getPhase() == Player::Discard
        ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != CardsMoveOneTime) return false;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_USE || move.to_place != Player::DiscardPile) return true;
        for (ServerPlayer *owner : room->getAlivePlayers()) if (owner == actor && owner->canDiscard(owner, "he"))
            for (int instance : owner->getValidSkillInstanceIds(objectName())) {
                for (int id : move.card_ids) if (available(owner, id, room)) {
                    SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = ctx.initiator = ctx.invoker = owner; ctx.instanceID = instance;
                    ctx.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(objectName(), instance));
                    ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
                    bool amountOk = false; ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &amountOk); if (!amountOk) ctx.amount = getBaseAmount();
                    ctx.current_event = event; ctx.original_data = &data; ctx.extra_data = QVariantMap{{"book", id}};
                    // Keep the original move ordinal even after earlier books leave the discard pile.
                    ctx.trigger_count = move.card_ids.indexOf(id); ctx.preferredTarget = owner;
                    if (prepareSource(room, ctx)) contexts << ctx;
                }
            }
        return true;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false; ctx.targets << ctx.owner;
        if (event == EventPhaseStart) return true;
        QVariantMap selected = ctx.extra_data.toMap(); const int id = selected.value("book", -1).toInt();
        if (!available(ctx.owner, id, room)) return false;
        const QVariant previous = room->getTag("shuji-card"); room->setTag("shuji-card", id);
        auto restore = qScopeGuard([&] { if (previous.isValid()) room->setTag("shuji-card", previous); else room->removeTag("shuji-card"); });
        const Card *cost = room->askForCard(ctx.owner, "..", "@shuji-discard", *ctx.original_data, Card::MethodNone);
        if (!cost || cost->getEffectiveId() < 0 || !ctx.owner->canDiscard(ctx.owner, cost->getEffectiveId())) return false;
        selected["cost"] = cost->getEffectiveId(); ctx.extra_data = selected; return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) return true;
        const int id = ctx.extra_data.toMap().value("cost", -1).toInt();
        if (!ctx.owner || room->getCardOwner(id) != ctx.owner || !ctx.owner->canDiscard(ctx.owner, id) || !available(ctx.owner, ctx.extra_data.toMap().value("book", -1).toInt(), room)) return false;
        room->throwCard(id, objectName(), ctx.owner); return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (event == EventPhaseStart) {
            const QString reason = objectName() + ":" + ctx.activationRef.ownerObjectName + ":" + QString::number(ctx.activationRef.key.instanceID);
            QVariantList receipts = target->getTag("ShujiLimitations").toList(); receipts << QVariantMap{{"instance", ctx.activationRef.key.instanceID}, {"reason", reason}}; target->setTag("ShujiLimitations", receipts);
            room->setPlayerCardLimitation(target, "discard", "TrickCard|.|.|hand", false, reason);
        } else {
            const int id = ctx.extra_data.toMap().value("book", -1).toInt();
            if (available(target, id, room)) { room->broadcastSkillInvoke(objectName(), target->getGeneral2Name() == "inovation_Hugh" ? 3 : qsanRandomBounded(2) + 1); target->addToPile("huanshu", id); }
        }
        return false;
    }
};

class ShujiMaxCards : public MaxCardsSkillV2
{
public:
    ShujiMaxCards() : MaxCardsSkillV2("#shuji") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.holder || ctx.holder->getPhase() != Player::Discard) return CorrectSkillResult::noEffect();
        int count = 0; for (const Card *card : ctx.holder->getHandcards()) count += card->isKindOf("TrickCard");
        return CorrectSkillResult::useAmount(count * ctx.currentAmount);
    }
};

class Jicheng : public TriggerSkillV2
{
public:
    Jicheng() : TriggerSkillV2("jicheng") { events << EventSkillInvoking << EventPhaseStart; frequency = Wake; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext active = data.value<SkillContext>();
            if (active.bypass_cost && active.skill_name == objectName() && active.activationRef.key.skillName == objectName()) addUsage(active);
        }
        return false;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &) const override
    {
        if (event == EventSkillInvoking) return {};
        if (!actor || !actor->isAlive() || !actor->hasSkill(objectName()) || actor->getPhase() != Player::RoundStart) return {};
        bool leastHp = true, leastHand = true;
        for (ServerPlayer *p : room->getOtherPlayers(actor)) { leastHp &= actor->getHp() <= p->getHp(); leastHand &= actor->getHandcardNum() <= p->getHandcardNum(); }
        return leastHp || leastHand ? TriggerList{{actor, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.owner; return ctx.owner != nullptr; }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { Q_UNUSED(ctx); return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        room->broadcastSkillInvoke(objectName()); room->doLightbox("jicheng$", 3000);
        room->recover(target, RecoverStruct(objectName(), ctx.owner, target->getLostHp()));
        if (target->isAlive()) { target->drawCards(2 * getEffectiveAmount(ctx), objectName()); target->gainMark("@waked"); room->changeHero(target, "inovation_Hugh", false, false, true); }
        return false;
    }
};

class Shoushi : public TriggerSkillV2
{
public:
    Shoushi() : TriggerSkillV2("shoushi") { events << PreCardUsed << TrickCardCanceling << TargetConfirmed; global = true; frequency = Compulsory; }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker ? ctx.invoker : ctx.owner; }
    static int matching(const Player *owner, const Card *card)
    { int count = 0; if (owner && card) for (int id : owner->getPile("huanshu")) count += Sanguosha->getCard(id)->getSuit() == card->getSuit(); return count; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *actor, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!actor || !actor->isAlive() || !actor->hasSkill(objectName()) || !use.card) return {};
        if (event == PreCardUsed && use.from == actor && use.card->isNDTrick() && matching(actor, use.card) > 0) return {{actor, {objectName()}}};
        if (event == TargetConfirmed && use.card->isKindOf("TrickCard") && use.to.contains(actor))
            for (int id : actor->getPile("huanshu")) if (Sanguosha->getCard(id)->getClassName() == use.card->getClassName()) return {{actor, {objectName()}}};
        return {};
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != TrickCardCanceling) return false;
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!effect.card || !effect.to) return true;
        for (const QVariant &value : effect.card->getTag("ShoushiEffects").toList()) {
            const QVariantMap receipt = value.toMap(); SkillContext ctx; ctx.skill_name = objectName();
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true); ctx.initiator = ctx.owner; ctx.invoker = effect.to;
            ctx.instanceID = receipt.value("instance").toInt(); ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.current_event = event; ctx.original_data = &data; ctx.extra_data = receipt; ctx.targets << effect.to; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        return effect.card && effect.card->getTag("ShoushiEffects").toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == TrickCardCanceling) return true;
        if (!ctx.owner || !ctx.original_data || (event == TargetConfirmed && !ctx.owner->askForSkillInvoke(this, *ctx.original_data))) return false;
        ctx.targets << ctx.owner; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (event == TrickCardCanceling) return true;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>(); if (!use.card) return false;
        if (event == TargetConfirmed) {
            if (!use.nullified_list.contains(target->objectName())) use.nullified_list << target->objectName();
            *ctx.original_data = QVariant::fromValue(use); return false;
        }
        if (ctx.choice == "add" || ctx.choice == "remove") {
            if (ctx.choice == "add") { if (room->getUseExtraTargets(use).contains(target)) use.to << target; }
            else use.to.removeOne(target);
            room->sortByActionOrder(use.to); *ctx.original_data = QVariant::fromValue(use); return false;
        }
        const int count = matching(ctx.owner, use.card); if (count <= 0) return false;
        QVariantList receipts = use.card->getTag("ShoushiEffects").toList();
        receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
        use.card->setTag("ShoushiEffects", receipts); room->broadcastSkillInvoke(objectName());
        if (count >= 2) target->drawCards(getEffectiveAmount(ctx), objectName());
        if (count < 3 || !target->isAlive() || use.card->isKindOf("Collateral")) return false;
        use = ctx.original_data->value<CardUseStruct>(); const QList<ServerPlayer *> extra = room->getUseExtraTargets(use);
        QStringList choices; choices << "cancel"; if (!extra.isEmpty()) choices << "add"; if (use.to.size() > 1) choices << "remove";
        if (choices.size() == 1) return false;
        ctx.choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"), *ctx.original_data);
        if (ctx.choice == "cancel") return false;
        ServerPlayer *chosen = room->askForPlayerChosen(ctx.owner, ctx.choice == "add" ? extra : use.to, objectName(),
            (ctx.choice == "add" ? "@shoushi-add:::" : "@shoushi-remove:::") + use.card->objectName());
        if (chosen) skillEffect(event, room, actor, ctx, chosen);
        return false;
    }
};

class Kaiqi : public TriggerSkillV2
{
public:
    Kaiqi() : TriggerSkillV2("kaiqi") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *actor, QVariant &) const override
    { return actor && actor->isAlive() && actor->hasSkill(objectName()) && actor->getPhase() == Player::Play && !actor->getPile("huanshu").isEmpty()
        ? TriggerList{{actor, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "@shuji-prompt", true);
        if (!target) return false; ctx.extra_data = target->objectName(); return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        QList<ServerPlayer *> candidates = room->getAlivePlayers();
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        room->broadcastSkillInvoke(objectName()); room->doLightbox("kaiqi$", 800);
        while (ctx.owner->isAlive() && !ctx.owner->getPile("huanshu").isEmpty() && target && candidates.contains(target)) {
            candidates.removeOne(target); const QList<int> ids = ctx.owner->getPile("huanshu");
            int id = -1;
            { room->fillAG(ids, ctx.owner); auto clear = qScopeGuard([&] { room->clearAG(ctx.owner); }); id = room->askForAG(ctx.owner, ids, false, objectName()); }
            if (!ids.contains(id)) break; ctx.extra_data = id; skillEffect(event, room, actor, ctx, target);
            if (candidates.isEmpty() || ctx.owner->getPile("huanshu").isEmpty()) break;
            target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@shuji-prompt", true);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { if (target && target->isAlive() && ctx.owner->getPile("huanshu").contains(ctx.extra_data.toInt())) room->obtainCard(target, ctx.extra_data.toInt()); return false; }
};

// inovation_fengbi: 標記技（封弊）——效果實作在
// PlayerDecisionService::askForCardChosen（他人不可指名其手牌）
// 與 PlayerCardContainer::updateHandcardNum（對其他玩家隱藏手牌數）
class InovationFengbi : public TriggerSkillV2
{
public:
    InovationFengbi() : TriggerSkillV2("inovation_fengbi")
    {
        frequency = Compulsory;
        events << NonTrigger;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override
    {
        return {};
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override
    {
        return false;
    }
};

InovationPackage::InovationPackage()
    : Package("inovation")
{
    General *nagisa = new General(this, "inovation_Nagisa", "real", 3, false);
    nagisa->addSkill(new Guangyu);
    nagisa->addSkill(new GuangyuTrigger);
    nagisa->addSkill(new Xiyuan);
    nagisa->addSkill(new Chengmeng);
    related_skills.insert("guangyu", "#guangyu-trigger");

    General *ushio = new General(this, "inovation_Ushio", "real", 3, false, true);
    ushio->addSkill(new Dingxin);

    General *tomoya = new General(this, "inovation_Tomoya", "real", 4);
    tomoya->addSkill(new InovationZhuren);
    tomoya->addSkill(new InovationZhurenTrigger);
    related_skills.insert("inovation_zhuren", "#inovation_zhuren");
    tomoya->addSkill(new Daolu);
    skills << new Diangong << new DiangongTrigger
           << new Shouyang << new Haixing << new Tanyan << new ShouyangClear;
    related_skills.insert("diangong", "#diangong");
    related_skills.insert("inovation_shouyang", "#inovation_shouyang-clear");

    General *kyou = new General(this, "inovation_fKyou", "real", 4, false);
    kyou->addSkill(new Touzhi);
    kyou->addSkill(new TouzhiDis);
    kyou->addSkill(new TouzhiTargetMod);
    related_skills.insert("touzhi", "#touzhi");
    related_skills.insert("touzhi", "#touzhi-target");
    kyou->addSkill(new Youjiao);

    General *natsumeRin = new General(this, "inovation_Natsume_Rin", "real", 99, false, false, false, 3);
    natsumeRin->addSkill(new Pasheng);
    natsumeRin->addSkill(new Maoqun);
    natsumeRin->addSkill(new Chengzhang);
    skills << new Zhiling << new ZhilingTrigger << new ZhilingMaxCards << new Zhixing;
    related_skills.insert("zhiling", "#zhiling");
    related_skills.insert("zhiling", "#zhiling-max");

    General *kKotori = new General(this, "inovation_KKotori", "magic", 3, false);
    kKotori->addSkill(new Jianshi);
    kKotori->addSkill(new JianshiClear);
    related_skills.insert("jianshi", "#jianshi-clear");
    kKotori->addSkill(new Qiyue);

    General *nao = new General(this, "inovation_Nao", "science", 3, false);
    nao->addSkill(new Huanxing);
    nao->addSkill(new Fushang);

    General *wSaki = new General(this, "inovation_WSaki", "science", 3, false);
    wSaki->addSkill(new Kuisi);
    wSaki->addSkill(new Youer);

    General *nanami = new General(this, "inovation_Nanami", "real", 3, false);
    nanami->addSkill(new Shengyou);
    nanami->addSkill(new InovationJinqu);

    General *mikoto = new General(this, "inovation_Mikoto", "science", 3, false);
    mikoto->addSkill(new Paoji);
    mikoto->addSkill(new Dianci);
    mikoto->addSkill(new DianciProhibit);
    related_skills.insert("inovation_dianci", "#inovation_dianci");

    General *shana = new General(this, "inovation_Shana", "magic", 3, false);
    shana->addSkill(new Zhena);
    shana->addSkill(new Tianhuo);

    General *akarin = new General(this, "inovation_Akarin", "real", 3, false);
    akarin->addSkill(new SE_Touming);
    akarin->addSkill(new SE_ToumingClear);
    related_skills.insert("inovation_SE_Touming", "#inovation_SE_Touming-clear");
    akarin->addSkill(new SE_Tuanzi);

    General *akari = new General(this, "inovation_Akari", "science", 3, false);
    akari->addSkill(new Takamakuri);
    akari->addSkill(new Tobiugachi);
    akari->addSkill(new Fukurouza);

    General *koromo = new General(this, "inovation_Koromo", "real", 3, false);
    koromo->addSkill(new Kongdi);
    koromo->addSkill(new Yixiangting);

    General *ayanamiR = new General(this, "inovation_AyanamiR", "kancolle", 3, false);
    ayanamiR->addSkill(new Taxian);
    ayanamiR->addSkill(new Guishen);

    General *ranka = new General(this, "inovation_Ranka", "diva", 3, false);
    ranka->addSkill(new Xingjian);
    ranka->addSkill(new XingjianClear);
    related_skills.insert("xingjian", "#xingjian-clear");
    ranka->addSkill(new Goutong);

    General *sanae = new General(this, "inovation_Sanae", "touhou", 3, false);
    sanae->addSkill(new Xinyang);
    sanae->addSkill(new XinyangClear);
    related_skills.insert("xinyang", "#xinyang-clear");
    sanae->addSkill(new InovationFengzhu);

    General *mumei = new General(this, "inovation_Mumei", "science", 2, false);
    mumei->addSkill(new Qinshi);
    mumei->addSkill(new Kangfen);
    mumei->addSkill(new Xiedou);

    General *mine = new General(this, "inovation_Mine", "science", 3, false);
    mine->addSkill(new Nangua);
    mine->addSkill(new InovationJixian);

    General *nMakoto = new General(this, "inovation_NMakoto", "real", 4);
    nMakoto->addSkill(new Yandan);
    nMakoto->addSkill(new YandanClear);
    nMakoto->addSkill(new YandanMaxCards);
    related_skills.insert("yandan", "#yandan");
    related_skills.insert("yandan", "#yandan-clear");
    nMakoto->addSkill(new Xiwang);
    skills << new Lunpo << new LunpoInvalidity;
    related_skills.insert("lunpo", "#lunpo-inv");

    General *chiaki = new General(this, "inovation_Chiaki", "real", 3, false);
    chiaki->addSkill(new Ningju);
    chiaki->addSkill(new Zhinian);
    skills << new Chengxu;
    skills << new InovationFengbi;

    General *shizuo = new General(this, "inovation_Shizuo", "real", 7);
    shizuo->addSkill(new Baonu);
    shizuo->addSkill(new Jizhanshiz);

    General *nagi = new General(this, "inovation_Nagi", "real", 3, false);
    nagi->addSkill(new Tianzi);
    nagi->addSkill(new Yuzhai);

    General *iroha = new General(this, "inovation_Iroha", "real", 3, false);
    iroha->addSkill(new Jianjin);
    iroha->addSkill(new Faka);

    General *fear = new General(this, "inovation_Fear", "real", 3, false);
    fear->addSkill(new Zuzhou);
    fear->addSkill(new ZuzhouMaxCards);
    fear->addSkill(new ZuzhouClear);
    fear->addSkill(new Jiguan);
    fear->addSkill(new JiguanClear);
    related_skills.insert("inovation_zuzhou", "#inovation_zuzhou");
    related_skills.insert("inovation_zuzhou", "#inovation_zuzhou-clear");
    related_skills.insert("jiguan", "#jiguan-clear");

    General *dalian = new General(this, "inovation_Dalian", "magic", 3, false);
    dalian->addSkill(new Shuji);
    dalian->addSkill(new ShujiMaxCards);
    related_skills.insert("shuji", "#shuji");
    dalian->addSkill(new Jicheng);

    General *hugh = new General(this, "inovation_Hugh", "magic", 3, true, true);
    hugh->addSkill(new Shoushi);
    hugh->addSkill(new Kaiqi);

    QList<Card *> cards;
    cards << new KeyTrick(Card::Heart, 10)
          << new KeyTrick(Card::Heart, 4)
          << new KeyTrick(Card::Diamond, 8)
          << new KeyTrick(Card::Spade, 11)
          << new KeyTrick(Card::Club, 1)
          << new MapoTofu(Card::Spade, 1);

    for (Card *card : cards)
        card->setParent(this);

    addMetaObject<InovationZhurenCard>();
    addMetaObject<DiangongCard>();
    addMetaObject<ZhilingCard>();
    addMetaObject<YouerCard>();
    addMetaObject<JizhanCard>();
    addMetaObject<TaxianCard>();
    addMetaObject<NingjuCard>();
    addMetaObject<JiguanCard>();
    addMetaObject<PaojiCard>();
    addMetaObject<InovationFengzhuCard>();
}

ADD_PACKAGE(Inovation)
