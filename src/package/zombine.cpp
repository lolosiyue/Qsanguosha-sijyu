#include "settings.h"
#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "json.h"
#include "exppattern.h"
#include "zombine.h"

class ZbGanran : public FilterSkill
{
public:
    ZbGanran() : FilterSkill("zb_ganran")
    {
    }

    bool viewFilter(const Card* to_select) const
    {
        if (to_select->getTypeId() == Card::TypeEquip) {
            Room *room = Sanguosha->currentRoom();
            return room->getCardPlace(to_select->getId()) != Player::PlaceEquip;
        }
        return false;
    }

    const Card *viewAs(const Card *c) const
    {
        IronChain *ironchain = new IronChain(c->getSuit(), c->getNumber());
        ironchain->setSkillName(objectName());
        WrappedCard *card = Sanguosha->getWrappedCard(c->getId());
        card->takeOver(ironchain);
        return card;
    }
};

class ZbXunmeng : public TriggerSkillV2
{
public:
    ZbXunmeng() : TriggerSkillV2("zb_xunmeng")
    {
        events << ConfirmDamage;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(this)
            && damage.from == player && damage.card && damage.card->isKindOf("Slash")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        SkillContext damage = ctx;
        damage.choice = "damage";
        damage.extra_data = false;
        skillEffect(event, room, player, damage, ctx.original_data->value<DamageStruct>().to);
        if (damage.extra_data.toBool() && ctx.owner->isAlive() && ctx.owner->getHp() > 1) {
            SkillContext loss = ctx;
            loss.choice = "losehp";
            skillEffect(event, room, player, loss, ctx.owner);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (ctx.choice == "losehp") room->loseHp(HpLostStruct(target, amount, objectName(), ctx.owner));
        else {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            LogMessage log;
            log.type = "#Xunmeng";
            log.from = ctx.owner;
            log.to << target;
            log.arg = QString::number(damage.damage);
            damage.damage += amount;
            log.arg2 = QString::number(damage.damage);
            room->sendLog(log);
            // Commit before HP loss can enter a nested dying window.
            ctx.original_data->setValue(damage);
            ctx.extra_data = true;
        }
        return false;
    }
};

class ZbZaibian : public TriggerSkillV2
{
public:
    ZbZaibian() : TriggerSkillV2("zb_zaibian")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    int getNumDiff(ServerPlayer *zombie) const
    {
        int human = 0, zombies = 0;
        foreach (ServerPlayer *player, zombie->getRoom()->getAlivePlayers()) {
            QString role = player->getRole();
            if (role == "rebel" || role == "renegade") {
                zombies++;
            } else {
                human++;
            }
        }
        int x = human - zombies + 1;
        if (x < 0) return 0;
        return x;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(this)
            && player->getPhase() == Player::Play && getNumDiff(player) > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner != nullptr; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.owner) return false;
        const int count = getNumDiff(ctx.owner) * getEffectiveAmount(ctx);
        if (count > 0) {
            LogMessage log;
            log.type = "#ZaibianGood";
            log.from = target;
            log.arg = QString::number(count);
            log.arg2 = objectName();
            room->sendLog(log);
            target->drawCards(count, objectName());
        }
        return false;
    }
};

class ZbDanyu : public TriggerSkillV2
{
public:
    ZbDanyu() : TriggerSkillV2("zb_danyu")
    {
        events << Damage << EventPhaseStart << EventLoseSkill;
        frequency = Compulsory;
    }

    static void refreshMark(Room *room, ServerPlayer *owner)
    {
        int total = 0;
        for (const SkillInstance &instance : owner->getSkillInstances())
            if (instance.skillName == "zb_danyu")
                total += owner->getSkillInstanceStateValue(instance.skillName, instance.instanceID, "pending", 0).toInt();
        room->setPlayerMark(owner, "&danyu", total);
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventLoseSkill) return false;
        // Loss has already removed the instance; the public mark is only a projection.
        if (player) refreshMark(room, player);
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(this)) return {};
        if ((event == Damage && data.value<DamageStruct>().from == player)
            || (event == EventPhaseStart && player->getPhase() == Player::RoundStart))
            return {{player, {objectName()}}};
        return {};
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !ctx.activationRef.isValid()) return false;
        const QString name = ctx.activationRef.key.skillName;
        const int id = ctx.activationRef.key.instanceID;
        int pending = owner->getSkillInstanceStateValue(name, id, "pending", 0).toInt();
        if (event == Damage && ctx.original_data) {
            const int gained = ctx.original_data->value<DamageStruct>().damage;
            owner->setSkillInstanceStateValue(name, id, "pending", pending + gained);
            refreshMark(room, owner);
        } else if (event == EventPhaseStart && pending > 0) {
            // Consume this source's accumulation before drawing; other copies remain intact.
            owner->setSkillInstanceStateValue(name, id, "pending", 0);
            refreshMark(room, owner);
            ctx.extra_data = pending;
            ctx.targets = {owner};
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive()) target->drawCards(ctx.extra_data.toInt() * 3 * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class ZbDanyuCard : public SkillCard
{
public:
    ZbDanyuCard() : SkillCard()
    {
        target_fixed = false;
        will_throw = true;
    }

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
    {
        if (!targets.isEmpty()) return false;
        return to_select->isDead() && to_select->getGeneral2Name() == "zombie";
    }

    void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
    {
        if (targets.isEmpty()) return;

        ServerPlayer *target = targets.first();

        room->revivePlayer(target);
        room->setPlayerProperty(target, "role", source->getRole());

        int draw_num = target->getMaxHp() - target->getHandcardNum();
        if (draw_num > 0) {
            target->drawCards(draw_num, "zb_danyu");
        }
    }
};

class ZbDanyuRevive : public TriggerSkillV2
{
public:
    ZbDanyuRevive() : TriggerSkillV2("#zb_danyu_revive")
    {
        events << Death;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override
    {
        // The original revival entry is intentionally dormant.
        return {};
    }
};

CeeCard::CeeCard()
{
    setSkillName("Cee");
    will_throw = false;
    handling_method = Card::MethodNone;
}

void CeeCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *zhouyu = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = zhouyu->getRoom();
    room->loseHp(HpLostStruct(target, 1, "Cee", zhouyu));
}

class ZbWansha : public TriggerSkillV2
{
public:
    ZbWansha() : TriggerSkillV2("zb_wansha")
    {
        events << Dying;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *current = room->getCurrent();
        // Dying is dispatched once per observer; admit the current owner only on its dispatch.
        return current && player == current && current->isAlive() && current->hasSkill(this)
            && data.value<DyingStruct>().who
            ? TriggerList{{current, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner && ctx.original_data) {
            // The current turn's owner supplies Wansha, not the dying event actor.
            ServerPlayer *current = ctx.owner;

            room->broadcastSkillInvoke(objectName());

            DyingStruct dying = ctx.original_data->value<DyingStruct>();

            LogMessage log;
            log.from = current;
            log.arg = objectName();
            log.type = "#WanshaOne";
            if (current != dying.who) {
                log.type = "#WanshaTwo";
                log.to << dying.who;
            }
            room->sendLog(log);
            room->notifySkillInvoked(current, objectName());
        }
        return false;
    }
};

class Cee : public ViewAsSkillV2
{
public:
    Cee() : ViewAsSkillV2("Cee", 1)
    {
        setPhaseName("Play");
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && !request.initiator->isKongcheng()
            && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->hasFlag("using") && request.selectedCardIds.isEmpty()
            && request.initiator->handCards().contains(card->getEffectiveId());
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        const int id = request.selectedCardIds.first();
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return ctx.initiator && room->getCardOwner(id) == ctx.initiator && canSelectCard(selection, Sanguosha->getCard(id));
    }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "CeeCard"; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets,
                         const Player *candidate) const override
    {
        return candidate && candidate->isAlive() && candidate != request.initiator && targets.isEmpty();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 1;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.invoker && target)
            target->getRoom()->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        return ContinueEffects;
    }
};

ZombinePackage::ZombinePackage()
    : Package("Zombine")
{
    General *zombie = new General(this, "zb_zombie", "god", 5, true);
    zombie->addSkill(new ZbXunmeng);
    zombie->addSkill(new ZbGanran);
    zombie->addSkill(new ZbZaibian);
    zombie->addSkill(new ZbWansha);
    zombie->addSkill("paoxiao");

    General *female_zombie = new General(this, "zb_female_zombie", "god", 5, false);
    female_zombie->addSkill(new ZbDanyu);
    female_zombie->addSkill(new ZbDanyuRevive);
    female_zombie->addSkill("zb_ganran");
    female_zombie->addSkill("zb_zaibian");
    female_zombie->addSkill("zb_wansha");
    female_zombie->addSkill("paoxiao");

    addMetaObject<CeeCard>();
    addMetaObject<ZbDanyuCard>();
}

ADD_PACKAGE(Zombine)
