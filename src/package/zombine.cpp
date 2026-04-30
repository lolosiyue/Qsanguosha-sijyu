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

class ZbXunmeng : public TriggerSkill
{
public:
    ZbXunmeng() : TriggerSkill("zb_xunmeng")
    {
        events << ConfirmDamage;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room* room, ServerPlayer *zombie, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();

        const Card *reason = damage.card;
        if (reason == nullptr)
            return false;

        if (reason->isKindOf("Slash")) {
            LogMessage log;
            log.type = "#Xunmeng";
            log.from = zombie;
            log.to << damage.to;
            log.arg = QString::number(damage.damage);
            log.arg2 = QString::number(++damage.damage);
            room->sendLog(log);

            data = QVariant::fromValue(damage);
            if (zombie->getHp() > 1)
                room->loseHp(HpLostStruct(zombie, 1, "zb_xunmeng", zombie));
        }

        return false;
    }
};

class ZbZaibian : public TriggerSkill
{
public:
    ZbZaibian() : TriggerSkill("zb_zaibian")
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

    bool trigger(TriggerEvent, Room *, ServerPlayer *zombie, QVariant &) const
    {
        if (zombie->getPhase() == Player::Play) {
            int x = getNumDiff(zombie);
            if (x > 0) {
                Room *room = zombie->getRoom();
                LogMessage log;
                log.type = "#ZaibianGood";
                log.from = zombie;
                log.arg = QString::number(x);
                log.arg2 = objectName();
                room->sendLog(log);
                zombie->drawCards(x, objectName());
            }
        }
        return false;
    }
};

class ZbDanyu : public TriggerSkill
{
public:
    ZbDanyu() : TriggerSkill("zb_danyu")
    {
        events << Damage << EventPhaseStart;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Damage) {
            DamageStruct damage = data.value<DamageStruct>();
            player->gainMark("&danyu", damage.damage);
        } else if (triggerEvent == EventPhaseStart) {
            if (player->getPhase() == Player::RoundStart) {
                int marks = player->getMark("&danyu");
                if (marks > 0) {
                    player->drawCards(marks * 3, objectName());
                    room->setPlayerMark(player, "&danyu", 0);
                }
            }
        }
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

class ZbDanyuRevive : public TriggerSkill
{
public:
    ZbDanyuRevive() : TriggerSkill("#zb_danyu_revive")
    {
        events << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        Q_UNUSED(room);
        Q_UNUSED(player);
        Q_UNUSED(data);
        return false;
    }
};

CeeCard::CeeCard()
{
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

class ZbWansha : public TriggerSkill
{
public:
    ZbWansha() : TriggerSkill("zb_wansha")
    {
        events << Dying;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == Dying) {
            ServerPlayer *current = room->getCurrent();
            if (!current || !current->isAlive() || !current->hasSkill(this)) return false;

            room->broadcastSkillInvoke(objectName());

            DyingStruct dying = data.value<DyingStruct>();

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

class Cee : public OneCardViewAsSkill
{
public:
    Cee() : OneCardViewAsSkill("Cee")
    {
        filter_pattern = ".|.|.|hand";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->isKongcheng() && !player->hasUsed("CeeCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        CeeCard *card = new CeeCard;
        card->addSubcard(originalCard);
        card->setSkillName(objectName());
        return card;
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