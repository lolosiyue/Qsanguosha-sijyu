#include "tw.h"
//#include "sp.h"
//#include "client.h"
//#include "general.h"
//#include "skill.h"
//#include "standard-generals.h"
#include "engine.h"
//#include "maneuvering.h"
//#include "json.h"
//#include "settings.h"
#include "clientplayer.h"
//#include "util.h"
//#include "wrapped-card.h"
#include "room.h"
//#include "roomthread.h"

class Yinqin : public TriggerSkillV2
{
public:
    Yinqin() : TriggerSkillV2("yinqin")
    {
        events << EventPhaseStart;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Start
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        const QString kingdom = ctx.owner->getKingdom();
        ctx.choice = room->askForChoice(ctx.owner, objectName(),
            kingdom == "wei" ? "shu" : kingdom == "shu" ? "wei" : "wei+shu");
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // The choice belongs to this activation and cannot leak into another instance.
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(ctx.owner, objectName());
        room->setPlayerProperty(target, "kingdom", ctx.choice);
        return false;
    }
};

class TWBaobian : public TriggerSkillV2
{
public:
    TWBaobian() : TriggerSkillV2("twbaobian")
    {
        events << DamageCaused;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.to
            || !damage.card || (!damage.card->isKindOf("Slash") && !damage.card->isKindOf("Duel"))
            || damage.chain || damage.transfer || !damage.by_user) return {};
        return damage.to->getKingdom() == player->getKingdom()
                || (damage.to->getHandcardNum() > qMax(damage.to->getHp(), 0) && player->canDiscard(damage.to, "h"))
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
        ctx.choice = target->getKingdom() == ctx.owner->getKingdom() ? "draw" : "discard";
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") {
            const int n = target->getMaxHp() - target->getHandcardNum();
            if (n > 0) {
                room->broadcastSkillInvoke(objectName(), 1);
                target->drawCards(n, objectName());
            }
            return true;
        }
        if (!ctx.invoker->canDiscard(target, "h")) return false;
        room->broadcastSkillInvoke(objectName(), 2);
        QList<int> cards = target->handCards();
        qsanShuffle(cards);
        const int n = target->getHandcardNum() - qMax(target->getHp(), 0);
        // Preserve the existing random-discard rule; card movement remains an effect.
        if (n > 1) {
            DummyCard discarded(cards.mid(0, n - 1));
            room->throwCard(&discarded, target, ctx.invoker);
        }
        return false;
    }
};

class Tijin : public TriggerSkillV2
{
public:
    Tijin(const QString &name = "tijin") : TriggerSkillV2(name)
    {
        global = true;
        if (name == "tijin") events << TargetSpecifying;
        else { events << CardFinished; frequency = Compulsory; }
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (event != TargetSpecifying) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.from || !use.card || !use.card->isKindOf("Slash") || use.to.size() != 1) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != use.from && use.from->inMyAttackRange(owner)) result[owner] << objectName();
        return result;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != CardFinished) return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.from || player != use.from || !use.card) return true;
        const QVariantList receipts = use.card->getTag("TijinReceipts").toList();
        const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (useEvent <= 0) return true;
        for (const QVariant &item : receipts) {
            const QVariantMap receipt = item.toMap();
            if (receipt.value("use_event").toLongLong() != useEvent) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner || !owner->isAlive() || !owner->canDiscard(use.from, "he")) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.initiator = ctx.invoker = owner;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            ctx.instanceID = receipt.value("dispatch").toInt();
            ctx.extra_data = receipt;
            ctx.targets = {use.from};
            ctx.original_data = &data;
            ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }

    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const Card *card = ctx.original_data ? ctx.original_data->value<CardUseStruct>().card : nullptr;
        return ctx.extra_data.toMap().value("applied_tijin").toBool() && ctx.invoker && ctx.invoker->isAlive()
            && card && card->getTag("TijinReceipts").toList().contains(ctx.extra_data);
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardFinished) return true;
        if (room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong() <= 0) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.from || use.to.size() != 1 || use.from == ctx.owner || !use.from->inMyAttackRange(ctx.owner)) return false;
        if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != CardFinished) return false;
        const Card *card = ctx.original_data->value<CardUseStruct>().card;
        QVariantList receipts = card->getTag("TijinReceipts").toList();
        // Settle the opportunity before recipient interception or nested card movements.
        if (!receipts.removeOne(ctx.extra_data)) ctx.targets.clear();
        card->setTag("TijinReceipts", receipts);
        return false;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (event == CardFinished) {
            if (!ctx.invoker->canDiscard(target, "he")) return false;
            const int id = room->askForCardChosen(ctx.invoker, target, "he", "tijin", false, Card::MethodDiscard);
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (card && !card->hasFlag("using") && room->getCardOwner(id) == target
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                && ctx.invoker->canDiscard(target, id)) room->throwCard(id, target, ctx.invoker);
            return false;
        }
        if (!use.card || use.to.size() != 1) return false;
        const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (useEvent <= 0) return false;
        room->broadcastSkillInvoke(objectName());
        use.to.first()->removeQinggangTag(use.card);
        use.to = {target};
        QVariantList receipts = use.card->getTag("TijinReceipts").toList();
        // A physical card may be used again; only receipts from this use survive the append.
        for (int i = receipts.size() - 1; i >= 0; --i)
            if (receipts.at(i).toMap().value("use_event").toLongLong() != useEvent) receipts.removeAt(i);
        const int dispatch = use.card->getTag("TijinNextReceipt").toInt() + 1;
        use.card->setTag("TijinNextReceipt", dispatch);
        receipts << QVariantMap{{"applied_tijin", true}, {"owner", ctx.owner->objectName()},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_id", ctx.activationRef.key.instanceID},
            {"dispatch", dispatch}, {"use_event", QString::number(useEvent)}};
        use.card->setTag("TijinReceipts", receipts);
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class Xiaolian : public TriggerSkillV2
{
public:
    Xiaolian(const QString &name = "xiaolian") : TriggerSkillV2(name)
    {
        global = true;
        if (name == "xiaolian") events << TargetConfirming;
        else { events << Damaged << CardFinished; frequency = Compulsory; }
    }

    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardFinished) return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.card && player == use.from) use.card->removeTag("XiaolianReceipts");
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != TargetConfirming) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash") || use.to.size() != 1 || use.to.first() != player) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != player) result[owner] << objectName();
        return result;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != Damaged) return false;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !player || player != damage.to) return true;
        const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (useEvent <= 0) return true;
        for (const QVariant &item : damage.card->getTag("XiaolianReceipts").toList()) {
            const QVariantMap receipt = item.toMap();
            if (receipt.value("owner").toString() != player->objectName()
                || receipt.value("use_event").toLongLong() != useEvent) continue;
            ServerPlayer *target = room->findPlayerByObjectName(receipt.value("beneficiary").toString(), true);
            if (!target || !target->isAlive()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = ctx.initiator = ctx.invoker = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            ctx.instanceID = receipt.value("dispatch").toInt();
            ctx.extra_data = receipt; ctx.targets = {target};
            ctx.original_data = &data; ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }

    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const Card *card = ctx.original_data ? ctx.original_data->value<DamageStruct>().card : nullptr;
        return ctx.invoker && ctx.invoker->isAlive() && card
            && ctx.extra_data.toMap().value("applied_xiaolian").toBool()
            && card->getTag("XiaolianReceipts").toList().contains(ctx.extra_data);
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == Damaged) return true;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || use.to.size() != 1 || use.to.first() == ctx.owner) return false;
        const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (useEvent <= 0) return false;
        for (const QVariant &item : use.card->getTag("XiaolianReceipts").toList()) {
            const QVariantMap receipt = item.toMap();
            if (receipt.value("activation_owner").toString() == ctx.owner->objectName()
                && receipt.value("activation_id").toInt() == ctx.activationRef.key.instanceID
                && receipt.value("use_event").toLongLong() == useEvent) return false;
        }
        if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.extra_data = use.to.first()->objectName();
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != Damaged) return false;
        const Card *card = ctx.original_data->value<DamageStruct>().card;
        QVariantList receipts = card->getTag("XiaolianReceipts").toList();
        // A cancelled recipient or declined gift still consumes this damage opportunity.
        if (!receipts.removeOne(ctx.extra_data)) ctx.targets.clear();
        card->setTag("XiaolianReceipts", receipts);
        return false;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == Damaged) {
            if (ctx.invoker->isNude()) return false;
            // This is the already-applied protection's optional gift, not a fresh skill grant.
            const Card *gift = room->askForExchange(ctx.invoker, "xiaolian", 1, 1, true, "@xiaolian-put", true);
            if (gift && gift->subcardsLength() == 1) {
                const int id = gift->getSubcards().first();
                if (!Sanguosha->getCard(id)->hasFlag("using") && room->getCardOwner(id) == ctx.invoker
                    && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip))
                    target->addToPile("xlhorse", id);
            }
            return false;
        }
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || use.to.size() != 1) return false;
        const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (useEvent <= 0) return false;
        room->broadcastSkillInvoke(objectName());
        use.to.first()->removeQinggangTag(use.card);
        use.to = {target};
        QVariantList receipts = use.card->getTag("XiaolianReceipts").toList();
        for (int i = receipts.size() - 1; i >= 0; --i)
            if (receipts.at(i).toMap().value("use_event").toLongLong() != useEvent) receipts.removeAt(i);
        const int dispatch = use.card->getTag("XiaolianNextReceipt").toInt() + 1;
        use.card->setTag("XiaolianNextReceipt", dispatch);
        receipts << QVariantMap{{"applied_xiaolian", true}, {"owner", target->objectName()},
            {"activation_owner", ctx.activationRef.ownerObjectName},
            {"beneficiary", ctx.extra_data.toString()}, {"source_owner", ctx.sourceRef.ownerObjectName},
            {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_id", ctx.activationRef.key.instanceID}, {"dispatch", dispatch}, {"use_event", QString::number(useEvent)}};
        use.card->setTag("XiaolianReceipts", receipts);
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class XiaolianDist : public DistanceSkillV2
{
public:
    XiaolianDist() : DistanceSkillV2("#xiaolian-dist")
    {
        // Placed horse cards are an applied room rule, surviving every granting skill's removal.
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.secondary && ctx.primary != ctx.secondary
            ? CorrectSkillResult::useAmount(ctx.secondary->getPile("xlhorse").size() * ctx.currentAmount)
            : CorrectSkillResult::noEffect();
    }
};


TaiwanSPPackage::TaiwanSPPackage()
    : Package("Taiwan_sp")
{
    General *tw_caocao = new General(this, "tw_caocao$", "wei", 4, true); // TW SP 019
    tw_caocao->addSkill("nosjianxiong");
    tw_caocao->addSkill("hujia");

    General *tw_simayi = new General(this, "tw_simayi", "wei", 3, true);
    tw_simayi->addSkill("nosfankui");
    tw_simayi->addSkill("nosguicai");

    General *tw_xiahoudun = new General(this, "tw_xiahoudun", "wei", 4, true); // TW SP 025
    tw_xiahoudun->addSkill("nosganglie");

    General *tw_zhangliao = new General(this, "tw_zhangliao", "wei", 4, true); // TW SP 013
    tw_zhangliao->addSkill("nostuxi");

    General *tw_xuchu = new General(this, "tw_xuchu", "wei", 4, true);
    tw_xuchu->addSkill("nosluoyi");

    General *tw_guojia = new General(this, "tw_guojia", "wei", 3, true); // TW SP 015
    tw_guojia->addSkill("tiandu");
    tw_guojia->addSkill("nosyiji");

    General *tw_zhenji = new General(this, "tw_zhenji", "wei", 3, false); // TW SP 007
    tw_zhenji->addSkill("qingguo");
    tw_zhenji->addSkill("luoshen");

    General *tw_liubei = new General(this, "tw_liubei$", "shu", 4, true); // TW SP 017
    tw_liubei->addSkill("rende");
    tw_liubei->addSkill("jijiang");

    General *tw_guanyu = new General(this, "tw_guanyu", "shu", 4, true); // TW SP 018
    tw_guanyu->addSkill("wusheng");

    General *tw_zhangfei = new General(this, "tw_zhangfei", "shu", 4, true);
    tw_zhangfei->addSkill("paoxiao");

    General *tw_zhugeliang = new General(this, "tw_zhugeliang", "shu", 3, true); // TW SP 012
    tw_zhugeliang->addSkill("guanxing");
    tw_zhugeliang->addSkill("kongcheng");

    General *tw_zhaoyun = new General(this, "tw_zhaoyun", "shu", 4, true); // TW SP 006
    tw_zhaoyun->addSkill("longdan");

    General *tw_machao = new General(this, "tw_machao", "shu", 4, true); // TW SP 010
    tw_machao->addSkill("mashu");
    tw_machao->addSkill("nostieji");

    General *tw_huangyueying = new General(this, "tw_huangyueying", "shu", 3, false); // TW SP 011
    tw_huangyueying->addSkill("nosjizhi");
    tw_huangyueying->addSkill("nosqicai");

    General *tw_sunquan = new General(this, "tw_sunquan$", "wu", 4, true); // TW SP 021
    tw_sunquan->addSkill("zhiheng");
    tw_sunquan->addSkill("jiuyuan");

    General *tw_ganning = new General(this, "tw_ganning", "wu", 4, true); // TW SP 009
    tw_ganning->addSkill("qixi");

    General *tw_lvmeng = new General(this, "tw_lvmeng", "wu", 4, true);
    tw_lvmeng->addSkill("keji");

    General *tw_huanggai = new General(this, "tw_huanggai", "wu", 4, true); // TW SP 014
    tw_huanggai->addSkill("noskurou");

    General *tw_zhouyu = new General(this, "tw_zhouyu", "wu", 3, true);
    tw_zhouyu->addSkill("nosyingzi");
    tw_zhouyu->addSkill("nosfanjian");

    General *tw_daqiao = new General(this, "tw_daqiao", "wu", 3, false); // TW SP 005
    tw_daqiao->addSkill("nosguose");
    tw_daqiao->addSkill("liuli");

    General *tw_luxun = new General(this, "tw_luxun", "wu", 3, true); // TW SP 016
    tw_luxun->addSkill("nosqianxun");
    tw_luxun->addSkill("noslianying");

    General *tw_sunshangxiang = new General(this, "tw_sunshangxiang", "wu", 3, false); // TW SP 028
    tw_sunshangxiang->addSkill("jieyin");
    tw_sunshangxiang->addSkill("xiaoji");

    General *tw_huatuo = new General(this, "tw_huatuo", "qun",3, true);
    tw_huatuo->addSkill("qingnang");
    tw_huatuo->addSkill("jijiu");

    General *tw_lvbu = new General(this, "tw_lvbu", "qun", 4, true); // TW SP 008
    tw_lvbu->addSkill("wushuang");

    General *tw_diaochan = new General(this, "tw_diaochan", "qun", 3, false); // TW SP 002
    tw_diaochan->addSkill("noslijian");
    tw_diaochan->addSkill("biyue");

    General *tw_xiaoqiao = new General(this, "tw_xiaoqiao", "wu", 3, false);
    tw_xiaoqiao->addSkill("tianxiang");
    tw_xiaoqiao->addSkill("hongyan");

    General *tw_yuanshu = new General(this, "tw_yuanshu", "qun", 4, true); // TW SP 004
    tw_yuanshu->addSkill("yongsi");
    tw_yuanshu->addSkill("weidi");
}
ADD_PACKAGE(TaiwanSP)

TaiwanYJCMPackage::TaiwanYJCMPackage()
: Package("Taiwan_yjcm")
{
    General *xiahb = new General(this, "twyj_xiahouba", "shu"); // TAI 001
    xiahb->addSkill(new Yinqin);
    xiahb->addSkill(new TWBaobian);

    General *zumao = new General(this, "twyj_zumao", "wu"); // TAI 002
    zumao->addSkill(new Tijin);
    // Applied protection settles compulsorily even after the granting instance disappears.
    zumao->addSkill(new Tijin("#tijin-effect"));
    related_skills.insert("tijin", "#tijin-effect");

    General *caoang = new General(this, "twyj_caoang", "wei"); // TAI 003
    caoang->addSkill(new XiaolianDist);
    caoang->addSkill(new Xiaolian);
    caoang->addSkill(new Xiaolian("#xiaolian-effect"));
    related_skills.insert("xiaolian", "#xiaolian-effect");
    related_skills.insert("xiaolian", "#xiaolian-dist");
}
ADD_PACKAGE(TaiwanYJCM)
