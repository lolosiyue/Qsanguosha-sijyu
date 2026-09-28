#include "boss.h"
//#include "settings.h"
//#include "skill.h"
#include "standard.h"
//#include "client.h"
//#include "clientplayer.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "skill-instance-utils.h"
#include <QScopeGuard>
//#include "wrapped-card.h"


// Boss mode skills use the V2 lifecycle directly while retaining their
// historical names, prompts, and event semantics.
static bool bossHasEquipKind(const ServerPlayer *player, const QString &kind)
{
    if (!player) return false;
    for (const Card *card : player->getEquips()) if (card->isKindOf(kind)) return true;
    return false;
}

class BossDidong : public TriggerSkillV2
{
public:
    BossDidong() : TriggerSkillV2("bossdidong") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Finish && player->hasSkill(objectName()))
            result.insert(player, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner),
                                                        objectName(), "bossdidong-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    { room->broadcastSkillInvoke(objectName()); target->turnOver(); return false; }
};

class BossShanbeng : public TriggerSkillV2
{
public:
    BossShanbeng() : TriggerSkillV2("bossshanbeng") { events << Death; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != Death || !player || data.value<DeathStruct>().who != player
            || !player->hasSkill(objectName()))
            return result;
        result.insert(player, {objectName()});
        return result;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner) return false;
        ctx.manual_effect = true;
        for (ServerPlayer *target : room->getOtherPlayers(owner)) {
            if (target->getEquips().isEmpty()) continue;
            ctx.targets << target;
        }
        if (ctx.targets.isEmpty()) return false;
        room->sendCompulsoryTriggerLog(owner, this);
        for (ServerPlayer *target : ctx.targets)
            skillEffect(event, room, owner, ctx, target);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    {
        if (target && !target->getEquips().isEmpty()) target->throwAllEquips(objectName());
        return false;
    }
};

class BossBeiming : public TriggerSkillV2
{
public:
    BossBeiming() : TriggerSkillV2("bossbeiming") { events << Death; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const DeathStruct death = data.value<DeathStruct>();
        if (event == Death && player && death.who == player && death.damage
            && death.damage->from && death.damage->from != player && player->hasSkill(objectName()))
            result.insert(player, {objectName()});
        return result;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const DeathStruct death = ctx.original_data->value<DeathStruct>();
        ServerPlayer *killer = death.damage ? death.damage->from : nullptr;
        if (!killer) return false;
        LogMessage log;
        log.type = "#BeimingThrow";
        log.from = owner;
        log.to << killer;
        log.arg = objectName();
        room->sendLog(log);
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(owner, objectName());
        ctx.targets << killer;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    {
        target->throwAllHandCards(objectName());
        return false;
    }
};

class BossLuolei : public TriggerSkillV2
{
public:
    BossLuolei() : TriggerSkillV2("bossluolei") { events << EventPhaseStart; setBaseAmount(1); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Start && player->hasSkill(objectName()))
            result.insert(player, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner),
                                                        objectName(), "bossluolei-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx), DamageStruct::Thunder));
        return false;
    }
};

class BossGuihuo : public TriggerSkillV2
{
public:
    BossGuihuo() : TriggerSkillV2("bossguihuo") { events << EventPhaseStart; setBaseAmount(1); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Start && player->hasSkill(objectName()))
            result.insert(player, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner),
                                                        objectName(), "bossguihuo-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx), DamageStruct::Fire));
        return false;
    }
};

class BossMingbao : public TriggerSkillV2
{
public:
    BossMingbao() : TriggerSkillV2("bossmingbao") { events << Death; frequency = Compulsory; setBaseAmount(1); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == Death && player && data.value<DeathStruct>().who == player
            && player->hasSkill(objectName()))
            result.insert(player, {objectName()});
        return result;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner) return false;
        room->sendCompulsoryTriggerLog(owner, this);
        ctx.manual_effect = true;
        ctx.targets = room->getOtherPlayers(owner);
        for (ServerPlayer *target : ctx.targets)
            skillEffect(event, room, owner, ctx, target);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (target)
            room->damage(DamageStruct(objectName(), nullptr, target, getEffectiveAmount(ctx), DamageStruct::Fire));
        Q_UNUSED(owner);
        return false;
    }
};

class BossBaolian : public TriggerSkillV2
{
public:
    BossBaolian() : TriggerSkillV2("bossbaolian") { events << EventPhaseStart; frequency = Compulsory; setBaseAmount(2); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Finish && player->hasSkill(objectName()))
            result.insert(player, {objectName()});
        return result;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        ctx.targets << owner;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class BossXiaoshou : public TriggerSkillV2
{
public:
    BossXiaoshou() : TriggerSkillV2("bossxiaoshou") { events << EventPhaseStart; setBaseAmount(2); }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || !player->isAlive()
            || player->getPhase() != Player::Finish || !player->hasSkill(objectName()))
            return result;
        for (ServerPlayer *candidate : room->getAlivePlayers()) {
            if (candidate->getHp() > player->getHp()) {
                result.insert(player, {objectName()});
                break;
            }
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        QList<ServerPlayer *> choices;
        for (ServerPlayer *candidate : room->getAlivePlayers())
            if (candidate->getHp() > ctx.owner->getHp()) choices << candidate;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, choices, objectName(),
                                                        "bossxiaoshou-invoke", true, true);
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

class BossGuiji : public TriggerSkillV2
{
public:
    BossGuiji() : TriggerSkillV2("bossguiji") { events << EventPhaseEnd; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseEnd && player && player->isAlive()
            && player->getPhase() == Player::Start && !player->getJudgingArea().isEmpty()
            && player->hasSkill(objectName()))
            result.insert(player, {objectName()});
        return result;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        ctx.targets << owner;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            const QList<const Card *> cards = target->getJudgingArea();
            if (cards.isEmpty()) break;
            room->throwCard(cards.at(qsanRandomBounded(cards.length())), objectName(), nullptr, owner);
        }
        return false;
    }
};

class BossLianyu : public TriggerSkillV2
{
public:
    BossLianyu() : TriggerSkillV2("bosslianyu") { events << EventPhaseStart; setBaseAmount(1); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Finish && player->hasSkill(objectName()))
            result.insert(player, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !room->askForSkillInvoke(ctx.owner, objectName() + "$-1")) return false;
        ctx.targets = room->getOtherPlayers(ctx.owner);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner) return false;
        ctx.manual_effect = true;
        for (ServerPlayer *target : ctx.targets)
            skillEffect(event, room, owner, ctx, target);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (target)
            room->damage(DamageStruct(objectName(), owner, target, getEffectiveAmount(ctx), DamageStruct::Fire));
        return false;
    }
};

class BossTaiping : public TriggerSkillV2
{
public:
    BossTaiping() : TriggerSkillV2("bosstaiping") { events << DrawNCards; frequency = Compulsory; setBaseAmount(2); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == DrawNCards && player && player->isAlive()
            && player->hasSkill(objectName())
            && data.value<DrawStruct>().reason == "draw_phase")
            result.insert(player, {objectName()});
        return result;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        room->sendCompulsoryTriggerLog(owner, this);
        draw.num += getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class BossSuoming : public TriggerSkillV2
{
public:
    BossSuoming() : TriggerSkillV2("bosssuoming") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || !player->isAlive()
            || player->getPhase() != Player::Finish || !player->hasSkill(objectName()))
            return result;
        for (ServerPlayer *target : room->getOtherPlayers(player)) {
            if (!target->isChained()) {
                result.insert(player, {objectName()});
                break;
            }
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !room->askForSkillInvoke(ctx.owner, objectName() + "$-1")) return false;
        ctx.targets = room->getOtherPlayers(ctx.owner);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner) return false;
        ctx.manual_effect = true;
        for (ServerPlayer *target : ctx.targets)
            skillEffect(event, room, owner, ctx, target);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    {
        if (target && !target->isChained()) room->setPlayerChained(target);
        return false;
    }
};

class BossXixing : public TriggerSkillV2
{
public:
    BossXixing() : TriggerSkillV2("bossxixing") { events << EventPhaseStart; setBaseAmount(1); }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || !player->isAlive()
            || player->getPhase() != Player::Start || !player->hasSkill(objectName()))
            return result;
        for (ServerPlayer *target : room->getOtherPlayers(player)) {
            if (target->isChained()) {
                result.insert(player, {objectName()});
                break;
            }
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        QList<ServerPlayer *> choices;
        for (ServerPlayer *target : room->getOtherPlayers(ctx.owner))
            if (target->isChained()) choices << target;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, choices, objectName(),
                                                        "bossxixing-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner || ctx.targets.isEmpty()) return false;
        ctx.manual_effect = true;
        room->broadcastSkillInvoke(objectName());
        const bool previousFlag = owner->hasFlag("bossxixing");
        owner->setFlags("bossxixing");
        const auto restoreFlag = qScopeGuard([owner, previousFlag] {
            owner->setFlags(previousFlag ? "bossxixing" : "-bossxixing");
        });
        // Damage and recovery affect different recipients and each needs its own effect hook.
        ctx.choice = "damage";
        skillEffect(event, room, owner, ctx, ctx.targets.first());
        if (owner->isAlive() && owner->hasFlag("bossxixing") && owner->isWounded()) {
            ctx.choice = "recover";
            skillEffect(event, room, owner, ctx, owner);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (ctx.choice == "damage")
            room->damage(DamageStruct(objectName(), owner, target, getEffectiveAmount(ctx), DamageStruct::Thunder));
        else if (target->isWounded())
            room->recover(target, RecoverStruct(objectName(), owner, getEffectiveAmount(ctx)));
        return false;
    }
};

class BossQiangzheng : public TriggerSkillV2
{
public:
    BossQiangzheng() : TriggerSkillV2("bossqiangzheng") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || !player->isAlive()
            || player->getPhase() != Player::Finish || !player->hasSkill(objectName()))
            return result;
        for (ServerPlayer *target : room->getOtherPlayers(player)) {
            if (!target->isKongcheng()) {
                result.insert(player, {objectName()});
                break;
            }
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !room->askForSkillInvoke(ctx.owner, objectName() + "$-1")) return false;
        ctx.targets = room->getOtherPlayers(ctx.owner);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner) return false;
        for (ServerPlayer *target : ctx.targets)
            room->doAnimate(1, owner->objectName(), target->objectName());
        ctx.manual_effect = true;
        for (ServerPlayer *target : ctx.targets) {
            if (!owner->isAlive()) break;
            skillEffect(event, room, owner, ctx, target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (!owner || !target || !target->isAlive() || target->isKongcheng()) return false;
        for (int i = 0; i < getEffectiveAmount(ctx) && owner->isAlive() && target->isAlive()
             && owner->canGet(target, "h"); ++i) {
            const int id = room->askForCardChosen(owner, target, "h", objectName(), false, Card::MethodGet);
            if (!target->handCards().contains(id) || !owner->canGet(target, id)) break;
            room->obtainCard(owner, Sanguosha->getCard(id),
                             CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, owner->objectName()), false);
        }
        return false;
    }
};

class BossZuijiu : public TriggerSkillV2
{
public:
    BossZuijiu() : TriggerSkillV2("bosszuijiu") { events << ConfirmDamage; frequency = Compulsory; setBaseAmount(1); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (event == ConfirmDamage && player && player->isAlive()
            && player->hasSkill(objectName()) && damage.card
            && damage.card->isKindOf("Slash"))
            result.insert(player, {objectName()});
        return result;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const int amount = getEffectiveAmount(ctx);
        LogMessage log;
        log.type = "#ZuijiuBuff";
        log.from = owner;
        log.to << damage.to;
        log.arg = QString::number(damage.damage);
        damage.damage += amount;
        log.arg2 = QString::number(damage.damage);
        room->sendLog(log);
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(owner, objectName());
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

class BossModao : public TriggerSkillV2
{
public:
    BossModao() : TriggerSkillV2("bossmodao") { events << EventPhaseStart; frequency = Compulsory; setBaseAmount(2); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Start && player->hasSkill(objectName()))
            result.insert(player, {objectName()});
        return result;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(owner, this);
        ctx.targets << owner;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class BossQushou : public TriggerSkillV2
{
public:
    BossQushou() : TriggerSkillV2("bossqushou") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Play && player->hasSkill(objectName()))
            result.insert(player, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        SavageAssault card(Card::NoSuit, 0);
        return card.isAvailable(ctx.owner)
            && room->askForSkillInvoke(ctx.owner, objectName(), false);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &) const override
    {
        SavageAssault *card = new SavageAssault(Card::NoSuit, 0);
        card->setSkillName(objectName());
        CardUseStruct use(card, owner);
        use.setOwnedCard(card);
        if (card->isAvailable(owner)) room->useCard(use);
        return false;
    }
};

class BossMojian : public TriggerSkillV2
{
public:
    BossMojian() : TriggerSkillV2("bossmojian") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Play && player->hasSkill(objectName()))
            result.insert(player, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ArcheryAttack card(Card::NoSuit, 0);
        return card.isAvailable(ctx.owner)
            && room->askForSkillInvoke(ctx.owner, objectName(), false);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &) const override
    {
        ArcheryAttack *card = new ArcheryAttack(Card::NoSuit, 0);
        card->setSkillName(objectName());
        CardUseStruct use(card, owner);
        use.setOwnedCard(card);
        if (card->isAvailable(owner)) room->useCard(use);
        return false;
    }
};

class BossDanshu : public TriggerSkillV2
{
public:
    BossDanshu() : TriggerSkillV2("bossdanshu") { events << CardsMoveOneTime; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        const bool fromHand = move.from_places.contains(Player::PlaceHand);
        const bool fromEquip = move.from_places.contains(Player::PlaceEquip);
        if (event == CardsMoveOneTime && player && move.from == player
            && !player->hasFlag("CurrentPlayer") && player->hasSkill(objectName())
            && (fromHand || fromEquip))
            result.insert(player, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        JudgeStruct judge;
        judge.who = target;
        judge.reason = objectName();
        judge.good = true;
        judge.pattern = ".|red";
        room->judge(judge);
        if (judge.isGood() && target->isAlive() && target->isWounded())
            room->recover(target, RecoverStruct(objectName(), owner, getEffectiveAmount(ctx)));
        return false;
    }
};

class Jingjia : public TriggerSkillV2
{
public:
    Jingjia() : TriggerSkillV2("jingjia") { events << GameStart; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == GameStart && player && player->hasSkill(objectName()))
            result.insert(player, {objectName()});
        return result;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        ctx.targets << owner;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    {
        for (int id : Sanguosha->getRandomCards()) {
            if (!target->isAlive()) break;
            if (room->getCardOwner(id)) continue;
            const Card *card = Sanguosha->getCard(id);
            if (!card) continue;
            const QString name = card->objectName();
            if (name != "wushuangji" && name != "baihuapao"
                && name != "shimandai" && name != "zijinguan")
                continue;
            const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
            if (equip && target->hasEquipArea(equip->location()) && !target->getEquip(equip->location())) {
                room->moveCardTo(card, nullptr, Player::PlaceTable);
                if (target->isAlive() && room->getCardPlace(id) == Player::PlaceTable)
                    card->use(room, target, QList<ServerPlayer *>() << target);
            }
        }
        return false;
    }
};

class BossAozhan : public TriggerSkillV2
{
public:
    BossAozhan() : TriggerSkillV2("bossaozhan") { events << DamageForseen << EventPhaseChanging << DrawNCards; frequency = Compulsory; setBaseAmount(1); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return result;
        bool valid = false;
        if (event == DamageForseen) {
            valid = data.value<DamageStruct>().damage > 1
                && bossHasEquipKind(player, "Armor");
        } else if (event == EventPhaseChanging) {
            valid = data.value<PhaseChangeStruct>().to == Player::Judge
                && bossHasEquipKind(player, "Treasure");
        } else if (event == DrawNCards) {
            valid = data.value<DrawStruct>().reason == "draw_phase"
                && bossHasEquipKind(player, "Horse");
        }
        if (valid) result.insert(player, {objectName()});
        return result;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        if (event == DamageForseen) return true;
        if (event == EventPhaseChanging) {
            owner->skip(Player::Judge);
        } else {
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            draw.num += getEffectiveAmount(ctx);
            *ctx.original_data = QVariant::fromValue(draw);
        }
        return false;
    }
};
// These two skills have no V2 counterpart; retain their established contracts.
class BossGuimei : public ProhibitSkill
{
public:
    BossGuimei() : ProhibitSkill("bossguimei") {}
    bool isProhibited(const Player *, const Player *to, const Card *card,
                      const QList<const Player *> &) const override
    {
        return card->isKindOf("DelayedTrick") && to->hasSkill(objectName());
    }
};

class BossManjia : public ViewAsEquipSkill
{
public:
    BossManjia() : ViewAsEquipSkill("bossmanjia") {}
    QString viewAsEquip(const Player *target) const override
    {
        return target->hasEquipArea(1) && !target->getArmor() ? "vine" : QString();
    }
};

BossModePackage::BossModePackage()
    : Package("~BossMode")
{
    General *chi = new General(this, "boss_chi", "qun", 5, true, true);
    chi->addSkill(new BossGuimei);
    chi->addSkill(new BossDidong);
    chi->addSkill(new BossShanbeng);

    General *mei = new General(this, "boss_mei", "qun", 5, false, true);
    mei->addSkill("bossguimei");
    mei->addSkill("nosenyuan");
    mei->addSkill(new BossBeiming);

    General *wang = new General(this, "boss_wang", "qun", 5, true, true);
    wang->addSkill("bossguimei");
    wang->addSkill(new BossLuolei);
    wang->addSkill("huilei");

    General *liang = new General(this, "boss_liang", "qun", 5, false, true);
    liang->addSkill("bossguimei");
    liang->addSkill(new BossGuihuo);
    liang->addSkill(new BossMingbao);

    General *niutou = new General(this, "boss_niutou", "qun", 10, true, true);
    niutou->addSkill(new BossBaolian);
    niutou->addSkill("mengjin");
    niutou->addSkill(new BossManjia);
    niutou->addSkill(new BossXiaoshou);

    General *mamian = new General(this, "boss_mamian", "qun", 9, true, true);
    mamian->addSkill(new BossGuiji);
    mamian->addSkill("nosfankui");
    mamian->addSkill(new BossLianyu);
    mamian->addSkill("nosjuece");

    General *heiwuchang = new General(this, "boss_heiwuchang", "qun", 15, true, true);
    heiwuchang->addSkill("bossguiji");
    heiwuchang->addSkill(new BossTaiping);
    heiwuchang->addSkill(new BossSuoming);
    heiwuchang->addSkill(new BossXixing);

    General *baiwuchang = new General(this, "boss_baiwuchang", "qun", 18, true, true);
    baiwuchang->addSkill("bossbaolian");
    baiwuchang->addSkill(new BossQiangzheng);
    baiwuchang->addSkill(new BossZuijiu);
    baiwuchang->addSkill("nosjuece");

    General *luocha = new General(this, "boss_luocha", "qun", 20, false, true);
    luocha->addSkill(new BossModao);
    luocha->addSkill(new BossQushou);
    luocha->addSkill("yizhong");
    luocha->addSkill("kuanggu");

    General *yecha = new General(this, "boss_yecha", "qun", 18, true, true);
    yecha->addSkill("bossmodao");
    yecha->addSkill(new BossMojian);
    yecha->addSkill("bazhen");
    yecha->addSkill(new BossDanshu);
}
ADD_PACKAGE(BossMode)


JiwuCard::JiwuCard()
{
    setSkillName("jiwu");
    target_fixed = true;
}

void JiwuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QStringList choices;
    for (const QString &skill : {QString("wansha"), QString("lieren"), QString("qiangxi"), QString("xuanfeng"), QString("tieji")})
        if (!source->hasSkill(skill, true)) choices << skill;
    if (!choices.isEmpty()) room->acquireOneTurnSkills(source, "jiwu", room->askForChoice(source, "jiwu", choices.join("+")));
}

class JiwuClear : public TriggerSkillV2
{
public:
    JiwuClear() : TriggerSkillV2("#jiwu-clear") { events << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        // Applied grants outlive their granting instance; detach only the IDs this effect created.
        for (ServerPlayer *holder : room->getAllPlayers(true)) {
            const QVariantList grants = holder->getTag("JiwuGrants").toList();
            holder->removeTag("JiwuGrants");
            for (const QVariant &value : grants) {
                const QVariantMap grant = value.toMap();
                room->detachSkillFromPlayer(holder, SkillInstanceUtils::formatName(
                    grant.value("skill").toString(), grant.value("instance").toInt()), false, true);
            }
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};

class Jiwu : public ViewAsSkillV2
{
public:
    Jiwu() : ViewAsSkillV2("jiwu", 1) {}
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.initiator && card && request.selectedCardIds.isEmpty()
        && request.initiator->handCards().contains(card->getEffectiveId())
        && request.initiator->canDiscard(card->getEffectiveId()); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "JiwuCard"; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr; }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    { return ctx.invoker && ViewAsSkillV2::pay(room, ctx, request); }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.invoker) skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            QStringList choices;
            for (const QString &skill : {QString("wansha"), QString("lieren"), QString("qiangxi"), QString("xuanfeng"), QString("tieji")})
                if (!target->hasSkill(skill, true)) choices << skill;
            if (choices.isEmpty()) break;
            const QString skill = room->askForChoice(target, objectName(), choices.join("+"));
            if (!choices.contains(skill) || target->hasSkill(skill, true)) break;
            const int instance = room->acquireSkill(target, skill);
            if (instance <= 0) continue;
            QVariantList grants = target->getTag("JiwuGrants").toList();
            grants << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName},
                                  {"source_instance", ctx.sourceRef.key.instanceID},
                                  {"skill", skill}, {"instance", instance}};
            target->setTag("JiwuGrants", grants);
        }
        return ContinueEffects;
    }
};

class Shenqu : public TriggerSkillV2
{
public:
    Shenqu() : TriggerSkillV2("shenqu") { events << EventPhaseStart << Damaged; frequency = Frequent; setBaseAmount(2); }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        if (event == EventPhaseStart && player->getPhase() == Player::RoundStart) {
            for (ServerPlayer *owner : room->getAllPlayers())
                if (owner->isAlive() && owner->hasSkill(objectName()) && owner->getHandcardNum() <= owner->getMaxHp())
                    result.insert(owner, QStringList(objectName()));
        } else if (event == Damaged && player->hasSkill(objectName())) {
            Peach peach(Card::NoSuit, 0); if (peach.isAvailable(player)) result.insert(player, QStringList(objectName()));
        }
        Q_UNUSED(data); return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        if (event == EventPhaseStart) {
            if (!ctx.owner->askForSkillInvoke(objectName() + "$-1")) return false;
            ctx.targets << ctx.owner;
            return true;
        }
        const Card *card = room->askForCard(ctx.owner, "Peach", "@shenqu-peach", *ctx.original_data, Card::MethodUse, nullptr, true);
        if (!card) return false;
        // The selected Peach may be a virtual conversion; its physical ID is not its identity.
        ctx.extra_data = QVariant::fromValue(card);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        if (event == Damaged && ctx.extra_data.isValid()) {
            const Card *card = ctx.extra_data.value<const Card *>();
            if (!card) return false;
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(ctx.owner, objectName());
            room->useCard(CardUseStruct(card, ctx.owner, ctx.owner));
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class Xiuluo : public TriggerSkillV2
{
public:
    Xiuluo() : TriggerSkillV2("xiuluo") { events << EventPhaseStart; }
    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != EventPhaseStart || player != ctx.owner || player->getPhase() != Player::Start) return;
        // One resolution can repeat, but a declined copy must not prompt again in this event.
        player->setSkillInstanceStateValue(objectName(), ctx.instanceID, "declined", false);
        player->setSkillInstanceStateValue(objectName(), ctx.instanceID, "attempt_limit", player->getJudgingArea().size());
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive()
            && player->getPhase() == Player::Start && player->hasSkill(objectName())
            && player->canDiscard(player, "h") && hasDelayed(player)) {
            for (int id : player->getValidSkillInstanceIds(objectName())) {
                if (player->getSkillInstanceStateValue(objectName(), id, "declined").toBool()) continue;
                const int limit = player->getSkillInstanceStateValue(objectName(), id, "attempt_limit").toInt();
                if (limit > 0) result[player] << SkillInstanceUtils::formatName(objectName(), id) + '*' + QString::number(limit);
            }
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        QStringList suits;
        for (const Card *card : ctx.owner->getJudgingArea())
            if (card->isKindOf("DelayedTrick") && !suits.contains(card->getSuitString())) suits << card->getSuitString();
        const Card *discard = suits.isEmpty() ? nullptr : room->askForCard(ctx.owner,
            QString(".|%1|.|hand").arg(suits.join(",")), "@xiuluo", QVariant(), Card::MethodNone, nullptr, false, objectName());
        if (!discard || !ctx.owner->handCards().contains(discard->getEffectiveId())
            || !ctx.owner->canDiscard(discard->getEffectiveId())) {
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "declined", true);
            return false;
        }
        ctx.extra_data = QVariantMap{{"card", discard->getEffectiveId()}, {"suit", int(discard->getSuit())}};
        ctx.targets << ctx.owner;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toMap().value("card").toInt();
        if (!owner->handCards().contains(id) || !owner->canDiscard(id)) return false;
        room->throwCard(id, objectName(), owner, owner);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        const auto clearSelection = qScopeGuard([room, owner] { room->clearAG(owner); });
        for (int i = 0; i < getEffectiveAmount(ctx) && owner->isAlive() && target->isAlive(); ++i) {
            QList<int> matching, other;
            for (const Card *card : target->getJudgingArea()) {
                if (!card->isKindOf("DelayedTrick")) continue;
                (int(card->getSuit()) == ctx.extra_data.toMap().value("suit").toInt() ? matching : other) << card->getEffectiveId();
            }
            if (matching.isEmpty()) break;
            room->fillAG(matching + other, owner, other);
            const int id = room->askForAG(owner, matching, false, objectName());
            room->clearAG(owner);
            if (!matching.contains(id) || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceDelayedTrick) break;
            room->throwCard(id, objectName(), nullptr);
        }
        return false;
    }
private:
    static bool hasDelayed(const ServerPlayer *player)
    {
        for (const Card *card : player->getJudgingArea())
            if (card->isKindOf("DelayedTrick")) return true;
        return false;
    }
};

class Shenwei : public TriggerSkillV2
{
public:
    Shenwei() : TriggerSkillV2("shenwei") { events << DrawNCards; frequency = Compulsory; setBaseAmount(1); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == DrawNCards && player && player->isAlive()
            && player->hasSkill(objectName())
            && data.value<DrawStruct>().reason == "draw_phase")
            result.insert(player, {objectName()});
        return result;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(owner, this);
        ctx.manual_effect = true;
        return skillEffect(event, room, owner, ctx, owner);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *) const override
    {
        int enemies = 0;
        for (ServerPlayer *player : room->getOtherPlayers(owner))
            if (!owner->isYourFriend(player)) ++enemies;
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num += enemies * getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class ShenweiKeep : public MaxCardsSkillV2
{
public:
    ShenweiKeep() : MaxCardsSkillV2("#shenwei") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.primary->hasSkill("shenwei"))
            return CorrectSkillResult::noEffect();
        int amount = 0;
        for (const Player *player : ctx.primary->getAliveSiblings())
            if (!ctx.primary->isYourFriend(player)) ++amount;
        return CorrectSkillResult::useAmount(amount * ctx.currentAmount);
    }
};

class Shenji : public TargetModSkillV2
{
public:
    Shenji() : TargetModSkillV2("shenji") { setBaseAmount(1); frequency = NotCompulsory; }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.primary->hasSkill(objectName()))
            return CorrectSkillResult::noEffect();
        if (ctx.modType == TargetModSkill::ExtraTarget)
            return CorrectSkillResult::useAmount(2 * ctx.currentAmount);
        if (ctx.modType == TargetModSkill::Residue)
            return CorrectSkillResult::useAmount(ctx.currentAmount);
        return CorrectSkillResult::noEffect();
    }
};

class BossAozhanMod : public TargetModSkillV2
{
public:
    BossAozhanMod() : TargetModSkillV2("#bossaozhan-mod") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::Residue || !ctx.primary
            || !ctx.primary->hasSkill("bossaozhan"))
            return CorrectSkillResult::noEffect();
        for (const Card *card : ctx.primary->getEquips())
            if (card->isKindOf("Weapon")) return CorrectSkillResult::useAmount(ctx.currentAmount);
        return CorrectSkillResult::noEffect();
    }
};

class WushuangjiSkill : public WeaponSkillV2
{
public:
    WushuangjiSkill() : WeaponSkillV2("wushuangji", "wushuangji") { events << Damage; setBaseAmount(1); }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result; if (event != Damage || !player || !WeaponSkillV2::triggerable(player)) return result;
        const DamageStruct damage = data.value<DamageStruct>(); if (!damage.card || !damage.card->isKindOf("Slash")) return result;
        const CardUseStruct use = room->getUseStruct(damage.card); if (!use.to.contains(damage.to)) return result;
        result.insert(player, {objectName()}); return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!owner->askForSkillInvoke(this, QVariant::fromValue(damage.to)))
            return false;
        if (damage.to && owner->canDiscard(damage.to, "he")) {
            const int id = room->askForCardChosen(owner, damage.to, "he", objectName(), false,
                                                  Card::MethodDiscard, QList<int>(), true);
            if (id > -1) {
                ctx.extra_data = id;
                ctx.choice = "discard";
                ctx.targets = {damage.to};
                return true;
            }
        }
        ctx.choice = "draw";
        ctx.targets = {owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        room->setEmotion(owner, "weapon/" + objectName());
        if (ctx.choice == "draw") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            return false;
        }
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive() && owner->canDiscard(target, "he"); ++i) {
            const int id = i == 0 ? ctx.extra_data.toInt()
                : room->askForCardChosen(owner, target, "he", objectName(), false, Card::MethodDiscard);
            if (room->getCardOwner(id) != target || !owner->canDiscard(target, id)) break;
            const Player::Place place = room->getCardPlace(id);
            if (place != Player::PlaceHand && place != Player::PlaceEquip) break;
            room->throwCard(id, objectName(), target, owner);
        }
        return false;
    }
};

class BaihuapaoSkill : public ArmorSkillV2
{
public:
    BaihuapaoSkill() : ArmorSkillV2("baihuapao", "baihuapao") { events << DamageInflicted; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (event == DamageInflicted && player && ArmorSkillV2::triggerable(player)
            && damage.nature != DamageStruct::Normal)
            result.insert(player, {objectName()});
        return result;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        room->setEmotion(owner, "armor/" + objectName());
        room->sendCompulsoryTriggerLog(owner, this);
        ctx.targets << owner;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        return damage.to == target && target->damageRevises(*ctx.original_data, -damage.damage);
    }
};

class ShimandaiSkill : public ArmorSkillV2
{
public:
    ShimandaiSkill() : ArmorSkillV2("shimandai", "shimandai") { events << TargetSpecified; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result; if (event != TargetSpecified || !player) return result; const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->getTypeId() <= 0 || use.to.size() != 1 || use.to.first() == use.from) return result;
        ServerPlayer *owner = use.to.first(); if (owner->hasArmorEffect(objectName()) && ArmorSkillV2::triggerable(owner)) result.insert(owner, {objectName()}); return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->setEmotion(ctx.owner, "armor/" + objectName());
        JudgeStruct judge;
        judge.who = target;
        judge.reason = objectName();
        judge.good = true;
        judge.pattern = ".|heart";
        room->judge(judge);
        if (judge.isGood()) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (use.to.contains(target)) use.nullified_list << target->objectName();
            *ctx.original_data = QVariant::fromValue(use);
        }
        return false;
    }
};

class ZijinguanSkill : public TreasureSkillV2
{
public:
    ZijinguanSkill() : TreasureSkillV2("zijinguan", "zijinguan") { events << EventPhaseStart; setBaseAmount(1); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->getPhase() == Player::Start
            && TreasureSkillV2::triggerable(player))
            result.insert(player, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ServerPlayer *target = room->askForPlayerChosen(
            ctx.owner, room->getOtherPlayers(ctx.owner), objectName(),
            "zijinguan0:", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->setEmotion(ctx.owner, "treasure/" + objectName());
        room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        return false;
    }
};

Wushuangji::Wushuangji(Suit suit, int number) : Weapon(suit, number, 4) { setObjectName("wushuangji"); }
Baihuapao::Baihuapao(Suit suit, int number) : Armor(suit, number) { setObjectName("baihuapao"); }
Shimandai::Shimandai(Suit suit, int number) : Armor(suit, number) { setObjectName("shimandai"); }
Zijinguan::Zijinguan(Suit suit, int number) : Treasure(suit, number) { setObjectName("zijinguan"); }
Lianjunshengyan::Lianjunshengyan(Suit suit, int number) : GlobalEffect(suit, number) { setObjectName("lianjunshengyan"); }

// Ordinary card resolution remains on its native CardEffect path.
void Lianjunshengyan::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if(effect.from==effect.to){
		CardUseStruct use = room->getUseStruct(this);
		effect.from->drawCards(use.to.length(),objectName());
	}else{
		if(effect.to->getLostHp()>0&&room->askForChoice(effect.to,objectName(),"r+d")=="r"){
			room->recover(effect.to,RecoverStruct(effect.from,this));
		}else
			effect.to->drawCards(1,objectName());
	}
}

HulaoPassPackage::HulaoPassPackage()
    : Package("HulaoPass")
{
    General *shenlvbu1 = new General(this, "shenlvbu1", "god", 8, true, true); // SP 008 (2-1)
    shenlvbu1->addSkill("mashu");
    shenlvbu1->addSkill("wushuang");
    shenlvbu1->addSkill(new Jingjia);
    shenlvbu1->addSkill(new BossAozhan);
    shenlvbu1->addSkill(new BossAozhanMod);
    related_skills.insert("bossaozhan", "#bossaozhan-mod");

    General *shenlvbu2 = new General(this, "shenlvbu2", "god", 4, true, true); // SP 008 (2-2)
    shenlvbu2->addSkill("mashu");
    shenlvbu2->addSkill("wushuang");
    shenlvbu2->addSkill(new Xiuluo);
    shenlvbu2->addSkill(new ShenweiKeep);
    shenlvbu2->addSkill(new Shenwei);
    shenlvbu2->addSkill(new Shenji);
    related_skills.insert("shenwei", "#shenwei");

    General *shenlvbu3 = new General(this, "shenlvbu3", "god", 4, true, true);
    shenlvbu3->addSkill("wushuang");
    shenlvbu3->addSkill(new Shenqu);
    shenlvbu3->addSkill(new Jiwu);
    shenlvbu3->addSkill(new JiwuClear);
    related_skills.insert("jiwu", "#jiwu-clear");
    shenlvbu3->addRelateSkill("wansha");
    shenlvbu3->addRelateSkill("lieren");
    shenlvbu3->addRelateSkill("qiangxi");
    shenlvbu3->addRelateSkill("xuanfeng");
    shenlvbu3->addRelateSkill("tieji");
	addMetaObject<JiwuCard>();

    QList<Card *> cards;
    cards << new Wushuangji(Card::Diamond, 12)
		<< new Baihuapao(Card::Diamond, 1)
		<< new Shimandai(Card::Spade, 2)
		<< new Shimandai(Card::Club, 2)
		<< new Zijinguan(Card::Club, 1)
		<< new Lianjunshengyan(Card::Heart, 1)
		<< new Lianjunshengyan(Card::Heart, 3)
		<< new Lianjunshengyan(Card::Heart, 4);

    foreach (Card *card, cards)
        card->setParent(this);

    skills << new WushuangjiSkill << new BaihuapaoSkill << new ShimandaiSkill << new ZijinguanSkill;

}
ADD_PACKAGE(HulaoPass)
