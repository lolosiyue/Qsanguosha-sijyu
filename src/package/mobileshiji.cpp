#include "mobileshiji.h"
//#include "settings.h"
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
#include "wind.h"

MobileZhiQiaiCard::MobileZhiQiaiCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

void MobileZhiQiaiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->giveCard(effect.from, effect.to, this, "mobilezhiqiai");
    if (effect.from->isDead() || effect.to->isDead()) return;
    QStringList choices;
    if (effect.from->getLostHp() > 0)
        choices << "recover";
    choices << "draw";
    if (room->askForChoice(effect.to, "mobilezhiqiai", choices.join("+"), QVariant::fromValue(effect.from)) == "recover")
        room->recover(effect.from, RecoverStruct("mobilezhiqiai", effect.to));
    else
        effect.from->drawCards(2, "mobilezhiqiai");
}

class MobileZhiQiai : public OneCardViewAsSkill
{
public:
    MobileZhiQiai() : OneCardViewAsSkill("mobilezhiqiai")
    {
        filter_pattern = "^BasicCard";
    }

    const Card *viewAs(const Card *card) const
    {
        MobileZhiQiaiCard *c = new MobileZhiQiaiCard;
        c->addSubcard(card);
        return c;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileZhiQiaiCard");
    }
};

class MobileZhiShanxi : public TriggerSkill
{
public:
    MobileZhiShanxi() : TriggerSkill("mobilezhishanxi")
    {
        events << EventPhaseStart << HpRecover;
    }

    bool transferMark(ServerPlayer *to, Room *room) const
    {
        int n = 0;
        foreach (ServerPlayer *p, room->getOtherPlayers(to)) {
            if (to->isDead()) break;
            if (p->isAlive() && p->getMark("&mobilezhixi") > 0) {
                n++;
                int mark = p->getMark("&mobilezhixi");
                p->loseAllMarks("&mobilezhixi");
                to->gainMark("&mobilezhixi", mark);
            }
        }
        return n > 0;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Play || !player->hasSkill(objectName())) return false;
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getMark("&mobilezhixi") > 0) continue;
                targets << p;
            }
            if (targets.isEmpty()) return false;
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@mobilezhishanxi-invoke", true, true);
            if (!target) return false;
            room->broadcastSkillInvoke(objectName());
            if (transferMark(target, room)) return false;
            target->gainMark("&mobilezhixi");
        } else {
            if (player->getMark("&mobilezhixi") <= 0 || player->hasFlag("Global_Dying")) return false;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (player->isDead()) return false;
                if (p->isDead() || !p->hasSkill(objectName())) continue;
                room->sendCompulsoryTriggerLog(p, objectName(), true, true);
                if (p == player || player->getCardCount() < 2)
                    room->loseHp(HpLostStruct(player, 1, "mobilezhishanxi", p));
                else {
                    const Card *card = room->askForExchange(player, objectName(), 2, 2, true, "mobilezhishanxi-give:" + p->objectName(), true);
                    if (card) room->giveCard(player, p, card, objectName());
                    else room->loseHp(HpLostStruct(player, 1, "mobilezhishanxi", p));
                }
            }

        }
        return false;
    }
};

MobileZhiShamengCard::MobileZhiShamengCard()
{
}

void MobileZhiShamengCard::onEffect(CardEffectStruct &effect) const
{
    effect.to->drawCards(2, "mobilezhishameng");
    effect.from->drawCards(3, "mobilezhishameng");
}

class MobileZhiShameng : public ViewAsSkill
{
public:
    MobileZhiShameng() : ViewAsSkill("mobilezhishameng")
    {
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (to_select->isEquipped() || Self->isJilei(to_select) || selected.length() > 1) return false;
        if (selected.isEmpty()) return true;
        if (selected.length() == 1)
            return to_select->sameColorWith(selected.first());
        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != 2) return nullptr;
        MobileZhiShamengCard *c = new MobileZhiShamengCard;
        c->addSubcards(cards);
        return c;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileZhiShamengCard");
    }
};

class MobileZhiFubi : public GameStartSkill
{
public:
    MobileZhiFubi(const QString &mobilezhifubi_skill) : GameStartSkill(mobilezhifubi_skill), mobilezhifubi_skill(mobilezhifubi_skill)
    {
    }

    void onGameStart(ServerPlayer *player) const
    {
        Room *room = player->getRoom();
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), mobilezhifubi_skill,
                       "@mobilezhifubi-invoke", true, true);
        if (!target) return;
        room->broadcastSkillInvoke(mobilezhifubi_skill);
        target->gainMark("&mobilezhifu");
    }

private:
    QString mobilezhifubi_skill;
};

class MobileZhiFubiKeep : public MaxCardsSkill
{
public:
    MobileZhiFubiKeep() : MaxCardsSkill("#mobilezhifubi")
    {
        frequency = NotFrequent;
    }

    int getExtra(const Player *target) const
    {
        if (target->getMark("&mobilezhifu") > 0) {
            int sunshao = 0;
            foreach (const Player *p, target->getAliveSiblings()) {
                if (p->hasSkill("mobilezhifubi"))
                    sunshao++;
            }
            return 3 * sunshao;
        }
        return 0;
    }
};

class MobileZhiZuici : public TriggerSkill
{
public:
    MobileZhiZuici() : TriggerSkill("mobilezhizuici")
    {
        events << Dying;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DyingStruct dying = data.value<DyingStruct>();
        if (dying.who != player) return false;
        QStringList areas;
        for (int i = 0; i < 5; i++) {
            if (player->getEquip(i) && player->hasEquipArea(i))
                areas << QString::number(i);
        }
        if (areas.isEmpty()) return false;
        if (!player->askForSkillInvoke(this)) return false;
        room->broadcastSkillInvoke(objectName());
        QString area = room->askForChoice(player, objectName(), areas.join("+"));
        player->throwEquipArea(area.toInt());
        room->recover(player, RecoverStruct(player, nullptr, 1 - player->getHp(), objectName()));
        return false;
    }
};

class MobileZhiFubiStart : public PhaseChangeSkill
{
public:
    MobileZhiFubiStart(const QString &mobilezhifubi_skill) : PhaseChangeSkill("#" + mobilezhifubi_skill), mobilezhifubi_skill(mobilezhifubi_skill)
    {
    }

    bool triggerable(const ServerPlayer *target) const
    {
        Player::Phase phase = Player::Play;
        if (mobilezhifubi_skill == "thirdmobilezhifubi")
            phase = Player::Start;
        return target != nullptr && target->isAlive() && target->getMark("&mobilezhifu") > 0 && target->getPhase() == phase;
    }


    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (player->isDead()) return false;
            if (p->isDead() || !p->hasSkill(mobilezhifubi_skill)) continue;
            if (!p->askForSkillInvoke(mobilezhifubi_skill, player)) continue;
            room->broadcastSkillInvoke(mobilezhifubi_skill);

            QString choice = room->askForChoice(p, mobilezhifubi_skill, "max+slash", QVariant::fromValue(player));
            LogMessage log;
            log.type = "#FumianFirstChoice";
            log.from = p;
            log.arg = mobilezhifubi_skill + ":" + choice;
            room->sendLog(log);
            if (choice == "max")
                room->addMaxCards(player, 3);
            else
                room->addSlashCishu(player, 1);
        }
        return false;
    }
private:
    QString mobilezhifubi_skill;
};

SecondMobileZhiZuiciCard::SecondMobileZhiZuiciCard()
{
    target_fixed = true;
}

void SecondMobileZhiZuiciCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QStringList areas;
    for (int i = 0; i < 5; i++) {
        if (source->getEquip(i) && source->hasEquipArea(i))
            areas << QString::number(i);
    }
    if (areas.isEmpty()) return;
    QString area = room->askForChoice(source, "secondmobilezhizuici", areas.join("+"));
    source->throwEquipArea(area.toInt());
    room->recover(source, RecoverStruct(source, nullptr, qMin(2, source->getMaxHp() - source->getHp()), "secondmobilezhizuici"));

    bool hasmark = false;
    foreach (ServerPlayer *p, room->getAlivePlayers()) {
        if (p->getMark("&mobilezhifu") > 0) {
            hasmark = true;
            break;
        }
    }

    if (hasmark && source->isAlive())
        room->askForUseCard(source, "@@secondmobilezhizuici", "@secondmobilezhizuici");
}

SecondMobileZhiZuiciMarkCard::SecondMobileZhiZuiciMarkCard()
{
}

bool SecondMobileZhiZuiciMarkCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    if (targets.isEmpty())
        return to_select->getMark("&mobilezhifu") > 0;
    else if (targets.length() == 1)
        return true;
    else
        return false;
}

bool SecondMobileZhiZuiciMarkCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

void SecondMobileZhiZuiciMarkCard::onUse(Room *, CardUseStruct &card_use) const
{
    if (card_use.to.length() != 2 || card_use.to.first()->getMark("&mobilezhifu") <= 0) return;
    card_use.to.first()->loseMark("&mobilezhifu");
    if (card_use.to.last()->isAlive())
        card_use.to.last()->gainMark("&mobilezhifu");
}

class SecondMobileZhiZuiciVS : public ZeroCardViewAsSkill
{
public:
    SecondMobileZhiZuiciVS() : ZeroCardViewAsSkill("secondmobilezhizuici")
    {
        response_pattern = "@@secondmobilezhizuici";
    }

    const Card *viewAs() const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
            return new SecondMobileZhiZuiciCard;
        } else {
            if (Sanguosha->currentRoomState()->getCurrentCardUsePattern() == "@@secondmobilezhizuici")
                return new SecondMobileZhiZuiciMarkCard;
        }
        return nullptr;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->hasEquipArea() && !player->getEquips().isEmpty();
    }
};

class SecondMobileZhiZuici : public TriggerSkill
{
public:
    SecondMobileZhiZuici() : TriggerSkill("secondmobilezhizuici")
    {
        events << Dying;
        view_as_skill = new SecondMobileZhiZuiciVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DyingStruct dying = data.value<DyingStruct>();
        if (dying.who != player) return false;
        QStringList areas;
        for (int i = 0; i < 5; i++) {
            if (player->getEquip(i) && player->hasEquipArea(i))
                areas << QString::number(i);
        }
        if (areas.isEmpty()) return false;
        if (!player->askForSkillInvoke(this)) return false;
        room->broadcastSkillInvoke(objectName());
        QString area = room->askForChoice(player, objectName(), areas.join("+"));
        player->throwEquipArea(area.toInt());
        room->recover(player, RecoverStruct(player, nullptr, qMin(2, player->getMaxHp() - player->getHp()), "secondmobilezhizuici"));

        bool hasmark = false;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->getMark("&mobilezhifu") > 0) {
                hasmark = true;
                break;
            }
        }

        if (hasmark && player->isAlive())
            room->askForUseCard(player, "@@secondmobilezhizuici", "@secondmobilezhizuici");
        return false;
    }
};

MobileZhiDuojiCard::MobileZhiDuojiCard()
{
}

bool MobileZhiDuojiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && !to_select->getEquips().isEmpty();
}

void MobileZhiDuojiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->removePlayerMark(effect.from, "@mobilezhiduojiMark");
    room->doSuperLightbox(effect.from, "mobilezhiduoji");
    QList<int> equiplist = effect.to->getEquipsId();
    if (equiplist.isEmpty()) return;
    DummyCard equips(equiplist);
    room->obtainCard(effect.from, &equips);
}

class MobileZhiDuoji : public ViewAsSkill
{
public:
    MobileZhiDuoji() : ViewAsSkill("mobilezhiduoji")
    {
        frequency = Limited;
        limit_mark = "@mobilezhiduojiMark";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        return selected.length() < 2 && !to_select->isEquipped() && !Self->isJilei(to_select);
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != 2) return nullptr;
        MobileZhiDuojiCard *c = new MobileZhiDuojiCard;
        c->addSubcards(cards);
        return c;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("@mobilezhiduojiMark") > 0;
    }
};

MobileZhiJianzhanCard::MobileZhiJianzhanCard()
{
}

void MobileZhiJianzhanCard::onEffect(CardEffectStruct &effect) const
{
    Slash *slash = new Slash(Card::NoSuit, 0);
    slash->deleteLater();
    slash->setSkillName("_mobilezhijianzhan");

    Room *room = effect.from->getRoom();
    QStringList choices;
    QList<ServerPlayer *> can_slash;
    foreach (ServerPlayer *p, room->getOtherPlayers(effect.to)) {
        if (!effect.to->canSlash(p, slash) || p->getHandcardNum() >= effect.to->getHandcardNum()) continue;
        can_slash << p;
    }
    if (!can_slash.isEmpty())
        choices << "slash";
    choices << "draw";

    QString choice = room->askForChoice(effect.to, "mobilezhijianzhan", choices.join("+"), QVariant::fromValue(effect.from));
    if (choice == "slash") {
        foreach (ServerPlayer *p, can_slash) {
            if (!effect.to->canSlash(p, slash) || p->getHandcardNum() >= effect.to->getHandcardNum())
                can_slash.removeOne(p);
        }
        if (can_slash.isEmpty()) return;
        ServerPlayer *to = room->askForPlayerChosen(effect.from, can_slash, "mobilezhijianzhan", "@mobilezhijianzhan-slash:" + effect.to->objectName());
        room->useCard(CardUseStruct(slash, effect.to, to));
    } else
        effect.from->drawCards(1, "mobilezhijianzhan");
}

class MobileZhiJianzhan : public ZeroCardViewAsSkill
{
public:
    MobileZhiJianzhan() : ZeroCardViewAsSkill("mobilezhijianzhan")
    {
    }

    const Card *viewAs() const
    {
        return new MobileZhiJianzhanCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileZhiJianzhanCard");
    }
};

SecondMobileZhiDuojiCard::SecondMobileZhiDuojiCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

void SecondMobileZhiDuojiCard::onEffect(CardEffectStruct &effect) const
{
    effect.to->addToPile("smzdjji", subcards);
}

SecondMobileZhiDuojiRemove::SecondMobileZhiDuojiRemove()
{
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void SecondMobileZhiDuojiRemove::onUse(Room *room, CardUseStruct &card_use) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", "secondmobilezhiduoji", "");
    room->throwCard(this, reason, nullptr);
    card_use.from->drawCards(1, "secondmobilezhiduoji");
}

class SecondMobileZhiDuojiVS : public OneCardViewAsSkill
{
public:
    SecondMobileZhiDuojiVS() : OneCardViewAsSkill("secondmobilezhiduoji")
    {
        response_pattern = "@@secondmobilezhiduoji!";
        expand_pile = "smzdjji";
    }

    bool viewFilter(const Card *to_select) const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY)
            return true;
        else if (Sanguosha->currentRoomState()->getCurrentCardUsePattern() == "@@secondmobilezhiduoji!")
                return Self->getPile("smzdjji").contains(to_select->getEffectiveId());
        return false;
    }

    const Card *viewAs(const Card *card) const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
            SecondMobileZhiDuojiCard *c = new SecondMobileZhiDuojiCard;
            c->addSubcard(card);
            return c;
        } else if (Sanguosha->currentRoomState()->getCurrentCardUsePattern() == "@@secondmobilezhiduoji!") {
            SecondMobileZhiDuojiRemove *c = new SecondMobileZhiDuojiRemove;
            c->addSubcard(card);
            return c;
        }
        return nullptr;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("SecondMobileZhiDuojiCard");
    }
};

class SecondMobileZhiDuoji : public TriggerSkill
{
public:
    SecondMobileZhiDuoji() : TriggerSkill("secondmobilezhiduoji")
    {
        events << CardFinished << EventPhaseChanging;
        view_as_skill = new SecondMobileZhiDuojiVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive() && !target->getPile("smzdjji").isEmpty();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardFinished) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("EquipCard")) return false;
            if (room->getCardPlace(use.card->getEffectiveId()) != Player::PlaceEquip ||
                    room->getCardOwner(use.card->getEffectiveId()) != player) return false;

            ServerPlayer *xunchen = room->findPlayerBySkillName(objectName());
            if (!xunchen) return false;

            LogMessage log;
            log.type = "#ZhenguEffect";
            log.from = player;
            log.arg = "secondmobilezhiduoji";
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(xunchen, objectName());

            CardMoveReason _reason(CardMoveReason::S_REASON_EXTRACTION, xunchen->objectName());
            room->obtainCard(xunchen, use.card, _reason);

            QList<int> piles = player->getPile("smzdjji");
            if (player->isDead() || piles.isEmpty()) return false;

            CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", "secondmobilezhiduoji", "");
            if (piles.length() == 1) {
                room->throwCard(Sanguosha->getCard(piles.first()), reason, nullptr);
                player->drawCards(1, objectName());
            } else {
                if (!room->askForUseCard(player, "@@secondmobilezhiduoji!", "@secondmobilezhiduoji", -1, Card::MethodNone)) {
                    int id = piles.at(qrand() % piles.length());
                    room->throwCard(Sanguosha->getCard(id), reason, nullptr);
                    player->drawCards(1, objectName());
                }
            }
        } else {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;

            LogMessage log;
            log.type = "#ZhenguEffect";
            log.from = player;
            log.arg = "secondmobilezhiduoji";
            room->sendLog(log);

            ServerPlayer *xunchen = room->findPlayerBySkillName(objectName());
            if (xunchen) {
                room->broadcastSkillInvoke(objectName());
                room->notifySkillInvoked(xunchen, objectName());
            }

            QList<int> piles = player->getPile("smzdjji");
            CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", "secondmobilezhiduoji", "");
            DummyCard remove(piles);
            room->throwCard(&remove, reason, nullptr);

            if (xunchen && xunchen->isAlive()) {
                QList<int> get;
                foreach (int id, piles) {
                    if (room->getCardPlace(id) != Player::DiscardPile) continue;
                    get << id;
                }
                if (get.isEmpty()) return false;
                DummyCard _get(get);
                room->obtainCard(xunchen, &_get);
            }
        }
        return false;
    }
};

MobileZhiWanweiCard::MobileZhiWanweiCard()
{
}

void MobileZhiWanweiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->addPlayerMark(effect.from, "mobilezhiwanwei_lun");
    int hp = effect.from->getHp();
    if (hp + 1 > 0)
        room->recover(effect.to, RecoverStruct(effect.from, nullptr, qMin(hp + 1, effect.to->getMaxHp() - effect.to->getHp()), "mobilezhiwanwei"));
    if (hp > 0)
        room->loseHp(HpLostStruct(effect.from, hp, "mobilezhiwanwei", effect.from));
}

class MobileZhiWanweiVS : public ZeroCardViewAsSkill
{
public:
    MobileZhiWanweiVS() : ZeroCardViewAsSkill("mobilezhiwanwei")
    {
    }

    const Card *viewAs() const
    {
        return new MobileZhiWanweiCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileZhiWanweiCard") && player->getMark("mobilezhiwanwei_lun") <= 0;
    }
};

class MobileZhiWanwei : public TriggerSkill
{
public:
    MobileZhiWanwei() : TriggerSkill("mobilezhiwanwei")
    {
        events << Dying;
        view_as_skill = new MobileZhiWanweiVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DyingStruct dying = data.value<DyingStruct>();
        if (player == dying.who || player->getMark("mobilezhiwanwei_lun") >= 0) return false;
        int hp = player->getHp();
        if (hp + 1 <= 0) return false;
        if (!player->askForSkillInvoke(this, dying.who)) return false;
        room->broadcastSkillInvoke(objectName());
        room->addPlayerMark(player, "mobilezhiwanwei_lun");
        room->recover(dying.who, RecoverStruct(player, nullptr, qMin(hp + 1, dying.who->getMaxHp() - dying.who->getHp()), "mobilezhiwanwei"));
        if (hp > 0)
            room->loseHp(HpLostStruct(player, hp, "mobilezhiwanwei", player));
        return false;
    }
};

class MobileZhiYuejianVS : public ViewAsSkill
{
public:
    MobileZhiYuejianVS() : ViewAsSkill("mobilezhiyuejian")
    {
        response_pattern = "@@mobilezhiyuejian";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        return selected.length() < 2 && !Self->isJilei(to_select);
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != 2)
            return nullptr;

        DummyCard *card = new DummyCard;
        card->setSkillName(objectName());
        card->addSubcards(cards);
        return card;
    }
};

class MobileZhiYuejian : public TriggerSkill
{
public:
    MobileZhiYuejian() : TriggerSkill("mobilezhiyuejian")
    {
        events << Dying;
        view_as_skill = new MobileZhiYuejianVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DyingStruct dying = data.value<DyingStruct>();
        if (player != dying.who || player->getCardCount() < 2 || !player->canDiscard(player, "he")) return false;
        if (!room->askForCard(player, "@@mobilezhiyuejian", "@mobilezhiyuejian", data, objectName())) return false;
        room->broadcastSkillInvoke(objectName());
        room->recover(player, RecoverStruct("mobilezhiyuejian", player));
        return false;
    }
};

class MobileZhiYuejianMax : public MaxCardsSkill
{
public:
    MobileZhiYuejianMax() : MaxCardsSkill("#mobilezhiyuejian-max")
    {
    }

    int getFixed(const Player *target) const
    {
        if (target->hasSkill("mobilezhiyuejian"))
            return target->getMaxHp();
        return -1;
    }
};

MobileZhiJianyuCard::MobileZhiJianyuCard()
{
}

bool MobileZhiJianyuCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.length() < 2;
}

bool MobileZhiJianyuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

void MobileZhiJianyuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->addPlayerMark(source, "mobilezhijianyu_lun");
    room->addPlayerMark(targets.first(), "&mobilezhijianyu+#" + source->objectName() + "#" + targets.last()->objectName());
    room->addPlayerMark(targets.last(), "&mobilezhijianyu+#" + source->objectName() + "#" + targets.first()->objectName());
}

class MobileZhiJianyuVS : public ZeroCardViewAsSkill
{
public:
    MobileZhiJianyuVS() : ZeroCardViewAsSkill("mobilezhijianyu")
    {
    }

    const Card *viewAs() const
    {
        return new MobileZhiJianyuCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("mobilezhijianyu_lun") <= 0;
    }
};

class MobileZhiJianyu : public TriggerSkill
{
public:
    MobileZhiJianyu() : TriggerSkill("mobilezhijianyu")
    {
        events << EventPhaseStart << TargetSpecifying;
        view_as_skill = new MobileZhiJianyuVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    int getPriority(TriggerEvent triggerEvent) const
    {
        if (triggerEvent == EventPhaseStart)
            return 5;
        return TriggerSkill::getPriority(triggerEvent);
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::RoundStart) return false;
            foreach (ServerPlayer *p, room->getAllPlayers(true)) {
                foreach (QString mark, p->getMarkNames()) {
                    if (mark.startsWith("&mobilezhijianyu+#" + player->objectName()) && p->getMark(mark) > 0)
                        room->setPlayerMark(p, mark, 0);
                }
            }
        } else {
            if (player->getPhase() != Player::Play) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SkillCard")) return false;
            foreach (ServerPlayer *p, use.to) {
                if (p->isDead()) continue;
                int n = 0;
                foreach (ServerPlayer *feiyi, room->getAllPlayers()) {
                    if (feiyi->isDead() || !feiyi->hasSkill(objectName())) continue;
                    if (player->getMark("&mobilezhijianyu+#" + feiyi->objectName() + "#" + p->objectName()) > 0) {
                        room->sendCompulsoryTriggerLog(feiyi, objectName(), true, true);
                        n++;
                    }
                }
                if (n > 0)
                    p->drawCards(n, objectName());
            }
        }
        return false;
    }
};

class MobileZhiShengxi : public PhaseChangeSkill
{
public:
    MobileZhiShengxi() : PhaseChangeSkill("mobilezhishengxi")
    {
        frequency = Frequent;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::Finish) return false;
        if (player->getMark("damage_point_round") > 0) return false;
        if (!player->askForSkillInvoke(this)) return false;
        room->broadcastSkillInvoke(objectName());
        player->drawCards(2, objectName());
        return false;
    }
};

class MobileZhiQinzheng : public TriggerSkill
{
public:
    MobileZhiQinzheng() : TriggerSkill("mobilezhiqinzheng")
    {
        events << CardUsed << CardResponded;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        const Card *card = nullptr;
        if (event == CardUsed) {
            card = data.value<CardUseStruct>().card;
        } else
            card = data.value<CardResponseStruct>().m_card;

        if (!card || card->isKindOf("SkillCard")) return false;
        int mark = player->getMark("&mobilezhiqinzheng") + 1;
        room->setPlayerMark(player, "&mobilezhiqinzheng", mark);

        if (mark % 3 == 0 || mark % 5 == 0 || mark % 8 == 0) {
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            if (mark % 3 == 0)
                getQinzhengCard(player, 3);
            if (mark % 5 == 0 && player->isAlive())
                getQinzhengCard(player, 5);
            if (mark % 8 == 0 && player->isAlive())
                getQinzhengCard(player, 8);
        }
        return false;
    }

    void getQinzhengCard(ServerPlayer *player, int num) const
    {
        Room *room = player->getRoom();
        QList<int> card_ids;
        foreach (int id, room->getDrawPile()) {
            const Card *card = Sanguosha->getCard(id);
            if (num == 3 && (card->isKindOf("Slash") || card->isKindOf("Jink")))
                card_ids << id;
            else if (num == 5 && (card->isKindOf("Peach") || card->isKindOf("Analeptic")))
                card_ids << id;
            else if (num == 8 && (card->isKindOf("ExNihilo") || card->isKindOf("Duel")))
                card_ids << id;
        }
        if (card_ids.isEmpty()) return;
        int id = card_ids.at(qrand() % card_ids.length());
        room->obtainCard(player, id, true);
    }
};

class MobileZhiQinzhengClear : public TriggerSkill
{
public:
    MobileZhiQinzhengClear() : TriggerSkill("#mobilezhiqinzheng-clear")
    {
        events << EventLoseSkill;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (data.toString() != "mobilezhiqinzheng") return false;
        room->setPlayerMark(player, "&mobilezhiqinzheng", 0);
        return false;
    }
};

class MobileZhiWuku : public TriggerSkill
{
public:
    MobileZhiWuku() : TriggerSkill("mobilezhiwuku")
    {
        events << CardUsed;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("EquipCard")) return false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isDead() || !p->hasSkill(objectName()) || p->getMark("&mobilezhiwuku") >= 3) continue;
            room->sendCompulsoryTriggerLog(p, objectName(), true, true);
            p->gainMark("&mobilezhiwuku");
        }
        return false;
    }
};

class MobileZhiSanchen : public PhaseChangeSkill
{
public:
    MobileZhiSanchen() : PhaseChangeSkill("mobilezhisanchen")
    {
        frequency = Wake;
        waked_skills = "mobilezhimiewu";
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive()&&player->getPhase() == Player::Finish
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getMark("&mobilezhiwuku")>2){
			LogMessage log;
			log.type = "#MobileZhiSanchenWake";
			log.from = player;
			log.arg = QString::number(player->getMark("&mobilezhiwuku"));
			log.arg2 = objectName();
			room->sendLog(log);
		}else if(!player->canWake(objectName()))
			return false;
        player->peiyin(this);
        room->notifySkillInvoked(player, objectName());
        room->doSuperLightbox(player, objectName());
        room->setPlayerMark(player, "mobilezhisanchen", 1);
        if (room->changeMaxHpForAwakenSkill(player, 1, objectName())) {
            room->recover(player, RecoverStruct("mobilezhisanchen", player));
            room->handleAcquireDetachSkills(player, "mobilezhimiewu");
        }
        return false;
    }
};

MobileZhiMiewuCard::MobileZhiMiewuCard()
{
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool MobileZhiMiewuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card) {
		card->addSubcards(subcards);
		card->setSkillName("mobilezhimiewu");
		card->setCanRecast(false);
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}

    const Card *_card = Self->tag.value("mobilezhimiewu").value<const Card *>();
    if (_card == nullptr)
        return false;

    card = Sanguosha->cloneCard(_card);
    card->setCanRecast(false);
    card->addSubcards(subcards);
    card->setSkillName("mobilezhimiewu");
    card->deleteLater();
    return card->targetFilter(targets, to_select, Self);
}

bool MobileZhiMiewuCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card) {
		card->deleteLater();
		return card->targetFixed();
	}

	const Card *_card = Self->tag.value("mobilezhimiewu").value<const Card *>();
	if (_card == nullptr)
		return false;

	card = Sanguosha->cloneCard(_card);
	card->deleteLater();
	return card->targetFixed();
}

bool MobileZhiMiewuCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card) {
		card->addSubcards(subcards);
		card->setSkillName("mobilezhimiewu");
		card->setCanRecast(false);
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}

    const Card *_card = Self->tag.value("mobilezhimiewu").value<const Card *>();
    if (_card == nullptr)
        return false;

    card = Sanguosha->cloneCard(_card);
    card->setCanRecast(false);
    card->addSubcards(subcards);
    card->setSkillName("mobilezhimiewu");
    card->deleteLater();
    return card->targetsFeasible(targets, Self);
}

const Card *MobileZhiMiewuCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer *player = card_use.from;
    player->loseMark("&mobilezhiwuku");
    Room *room = player->getRoom();
    room->addPlayerMark(player, "mobilezhimiewu-Clear");

    QString to_yizan = user_string;

    if ((user_string.contains("slash") || user_string.contains("Slash")) &&
            Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        QStringList guhuo_list = Sanguosha->getSlashNames();
        if (guhuo_list.isEmpty())
            guhuo_list << "slash";
        to_yizan = room->askForChoice(player, "mobilezhimiewu_slash", guhuo_list.join("+"));
    }

    const Card *card = Sanguosha->getCard(subcards.first());
    if (to_yizan == "normal_slash")
        to_yizan = "slash";

    Card *use_card = Sanguosha->cloneCard(to_yizan, card->getSuit(), card->getNumber());
    use_card->setSkillName("mobilezhimiewu");
    use_card->addSubcards(getSubcards());
    room->setCardFlag(use_card, "mobilezhimiewu");
	use_card->deleteLater();
    return use_card;
}

const Card *MobileZhiMiewuCard::validateInResponse(ServerPlayer *player) const
{
    player->loseMark("&mobilezhiwuku");
    Room *room = player->getRoom();
    room->addPlayerMark(player, "mobilezhimiewu-Clear");

    QString to_yizan;
    if (user_string == "peach+analeptic") {
        QStringList guhuo_list;
        guhuo_list << "peach";
        if (Sanguosha->hasCard("analeptic"))
            guhuo_list << "analeptic";
        to_yizan = room->askForChoice(player, "mobilezhimiewu_saveself", guhuo_list.join("+"));
    } else if (user_string == "slash") {
        QStringList guhuo_list = Sanguosha->getSlashNames();
        if (guhuo_list.isEmpty())
            guhuo_list << "slash";
        to_yizan = room->askForChoice(player, "mobilezhimiewu_slash", guhuo_list.join("+"));
    } else
        to_yizan = user_string;

    const Card *card = Sanguosha->getCard(subcards.first());
    if (to_yizan == "normal_slash")
        to_yizan = "slash";

    Card *use_card = Sanguosha->cloneCard(to_yizan, card->getSuit(), card->getNumber());
    use_card->setSkillName("mobilezhimiewu");
    use_card->addSubcards(getSubcards());
    room->setCardFlag(use_card, "mobilezhimiewu");
	use_card->deleteLater();
    return use_card;
}

class MobileZhiMiewuVS : public ViewAsSkill
{
public:
    MobileZhiMiewuVS() : ViewAsSkill("mobilezhimiewu")
    {
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        if (!player->hasTurn()) return false;
        return player->getMark("&mobilezhiwuku") > 0 && player->getMark("mobilezhimiewu-Clear") <= 0;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        if (!player->hasTurn()) return false;
        if (player->getMark("&mobilezhiwuku") <= 0 || player->getMark("mobilezhimiewu-Clear") > 0) return false;
        if (pattern.startsWith(".") || pattern.startsWith("@")) return false;
        if (pattern == "peach" && player->getMark("Global_PreventPeach") > 0) return false;
        for (int i = 0; i < pattern.length(); i++) {
            QChar ch = pattern[i];
            if (ch.isUpper() || ch.isDigit()) return false; // This is an extremely dirty hack!! For we need to prevent patterns like 'BasicCard'
        }
        return true;
    }

    bool isEnabledAtNullification(const ServerPlayer *player) const
    {
        ServerPlayer *current = player->getRoom()->getCurrent();
        if (!current || current->isDead() || current->getPhase() == Player::NotActive) return false;
        return player->getMark("&mobilezhiwuku") > 0 && player->getMark("mobilezhimiewu-Clear") <= 0;
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
            if (Self->isCardLimited(to_select, Card::MethodResponse))
                return false;
        } else {
            if (Self->isLocked(to_select))
                return false;
        }
        return selected.isEmpty();
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != 1) return nullptr;
        if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE
            || Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
            MobileZhiMiewuCard *card = new MobileZhiMiewuCard;
            card->setUserString(Sanguosha->getCurrentCardUsePattern());
            card->addSubcards(cards);
            return card;
        }

        const Card *c = Self->tag.value("mobilezhimiewu").value<const Card *>();
        if (c && c->isAvailable(Self)) {
            MobileZhiMiewuCard *card = new MobileZhiMiewuCard;
            card->setUserString(c->objectName());
            card->addSubcards(cards);
            return card;
        }
        return nullptr;
    }
};

class MobileZhiMiewu : public TriggerSkill
{
public:
    MobileZhiMiewu() : TriggerSkill("mobilezhimiewu")
    {
        events << CardFinished << CardResponded;
        view_as_skill = new MobileZhiMiewuVS;
    }

    QDialog *getDialog() const
    {
        return GuhuoDialog::getInstance("mobilezhimiewu", true, true, true, false, true);
    }

    int getPriority(TriggerEvent) const
    {
        return 0;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const
    {
        const Card *card = nullptr;
        if (event == CardFinished)
            card = data.value<CardUseStruct>().card;
        else {
            CardResponseStruct res = data.value<CardResponseStruct>();
            if (res.m_isUse) return false;
            card = res.m_card;
        }
        if (!card || card->isKindOf("SkillCard") || (!card->hasFlag("mobilezhimiewu") && !card->getSkillNames().contains(objectName()))) return false;
        player->drawCards(1, objectName());
        return false;
    }
};

MobileZhiPackage::MobileZhiPackage()
    : Package("mobilezhi")
{
    General *mobilezhi_wangcan = new General(this, "mobilezhi_wangcan", "wei", 3);
    mobilezhi_wangcan->addSkill(new MobileZhiQiai);
    mobilezhi_wangcan->addSkill(new MobileZhiShanxi);

    General *mobilezhi_chenzhen = new General(this, "mobilezhi_chenzhen", "shu", 3);
    mobilezhi_chenzhen->addSkill(new MobileZhiShameng);

    General *mobilezhi_sunshao = new General(this, "mobilezhi_sunshao", "wu", 3);
    mobilezhi_sunshao->addSkill(new MobileZhiFubi("mobilezhifubi"));
    mobilezhi_sunshao->addSkill(new MobileZhiFubiKeep);
    mobilezhi_sunshao->addSkill(new MobileZhiZuici);
    related_skills.insertMulti("mobilezhifubi", "#mobilezhifubi");

    General *second_mobilezhi_sunshao = new General(this, "second_mobilezhi_sunshao", "wu", 3);
    second_mobilezhi_sunshao->addSkill(new MobileZhiFubi("secondmobilezhifubi"));
    second_mobilezhi_sunshao->addSkill(new MobileZhiFubiStart("secondmobilezhifubi"));
    second_mobilezhi_sunshao->addSkill(new SecondMobileZhiZuici);

    General *third_mobilezhi_sunshao = new General(this, "third_mobilezhi_sunshao", "wu", 3);
    third_mobilezhi_sunshao->addSkill(new MobileZhiFubi("thirdmobilezhifubi"));
    third_mobilezhi_sunshao->addSkill(new MobileZhiFubiStart("thirdmobilezhifubi"));
    third_mobilezhi_sunshao->addSkill("secondmobilezhizuici");
    related_skills.insertMulti("thirdmobilezhifubi", "#thirdmobilezhifubi");

    General *mobilezhi_xunchen = new General(this, "mobilezhi_xunchen", "qun", 3);
    mobilezhi_xunchen->addSkill(new MobileZhiDuoji);
    mobilezhi_xunchen->addSkill(new MobileZhiJianzhan);

    General *second_mobilezhi_xunchen = new General(this, "second_mobilezhi_xunchen", "qun", 3);
    second_mobilezhi_xunchen->addSkill(new SecondMobileZhiDuoji);
    second_mobilezhi_xunchen->addSkill("mobilezhijianzhan");

    General *mobilezhi_bianfuren = new General(this, "mobilezhi_bianfuren", "wei", 3, false);
    mobilezhi_bianfuren->addSkill(new MobileZhiWanwei);
    mobilezhi_bianfuren->addSkill(new MobileZhiYuejian);
    mobilezhi_bianfuren->addSkill(new MobileZhiYuejianMax);
    related_skills.insertMulti("mobilezhiyuejian", "#mobilezhiyuejian-max");

    General *mobilezhi_feiyi = new General(this, "mobilezhi_feiyi", "shu", 3);
    mobilezhi_feiyi->addSkill(new MobileZhiJianyu);
    mobilezhi_feiyi->addSkill(new MobileZhiShengxi);

    General *mobilezhi_luotong = new General(this, "mobilezhi_luotong", "wu", 4);
    mobilezhi_luotong->addSkill(new MobileZhiQinzheng);
    mobilezhi_luotong->addSkill(new MobileZhiQinzhengClear);
    related_skills.insertMulti("mobilezhiqinzheng", "#mobilezhiqinzheng-clear");

    General *mobilezhi_duyu = new General(this, "mobilezhi_duyu", "qun", 4);
    mobilezhi_duyu->addSkill(new MobileZhiWuku);
    mobilezhi_duyu->addSkill(new MobileZhiSanchen);
    mobilezhi_duyu->addRelateSkill("mobilezhimiewu");

    skills << new MobileZhiMiewu;

    addMetaObject<MobileZhiQiaiCard>();
    addMetaObject<MobileZhiShamengCard>();
    addMetaObject<SecondMobileZhiZuiciCard>();
    addMetaObject<SecondMobileZhiZuiciMarkCard>();
    addMetaObject<MobileZhiDuojiCard>();
    addMetaObject<MobileZhiJianzhanCard>();
    addMetaObject<SecondMobileZhiDuojiCard>();
    addMetaObject<SecondMobileZhiDuojiRemove>();
    addMetaObject<MobileZhiWanweiCard>();
    addMetaObject<MobileZhiJianyuCard>();
    addMetaObject<MobileZhiMiewuCard>();
}

ADD_PACKAGE(MobileZhi)



MobileXinYinjuCard::MobileXinYinjuCard()
{
}

void MobileXinYinjuCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if (!effect.to->canSlash(effect.from, false) ||
            !room->askForUseSlashTo(effect.to, effect.from, "@mobilexinyinju-slash:" + effect.from->objectName(), false))
        room->addPlayerMark(effect.to, "&mobilexinyinju");
}

class MobileXinYinjuVS : public ZeroCardViewAsSkill
{
public:
    MobileXinYinjuVS() : ZeroCardViewAsSkill("mobilexinyinju")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileXinYinjuCard");
    }

    const Card *viewAs() const
    {
        return new MobileXinYinjuCard;
    }
};

class MobileXinYinju : public PhaseChangeSkill
{
public:
    MobileXinYinju() : PhaseChangeSkill("mobilexinyinju")
    {
        view_as_skill = new MobileXinYinjuVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive() && target->getMark("&mobilexinyinju") > 0 && target->getPhase() == Player::Start;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        LogMessage log;
        log.type = "#ZhenguEffect";
        log.from = player;
        log.arg = objectName();
        room->sendLog(log);
        room->setPlayerMark(player, "&mobilexinyinju", 0);
        if (!player->isSkipped(Player::Play))
            player->skip(Player::Play);
        if (!player->isSkipped(Player::Discard))
            player->skip(Player::Discard);
        return false;
    }
};

class MobileXinChijie : public TriggerSkill
{
public:
    MobileXinChijie() : TriggerSkill("mobilexinchijie")
    {
        events << TargetConfirming;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (!player->hasTurn()) return false;
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("SkillCard")) return false;
        if (use.to.length() != 1 || use.from == player || player->getMark("mobilexinchijie-Clear") > 0) return false;
        if (!player->askForSkillInvoke(this, data)) return false;
        room->broadcastSkillInvoke(objectName());
        room->addPlayerMark(player, "mobilexinchijie-Clear");

        JudgeStruct judge;
        judge.who = player;
        judge.reason = objectName();
        judge.pattern = ".|.|7~99";
        judge.good = true;
        room->judge(judge);

        if (judge.isGood()){
			use.to.removeOne(player);
			data = QVariant::fromValue(use);
		}
        return false;
    }
};

MobileXinCunsiCard::MobileXinCunsiCard()
{
}

bool MobileXinCunsiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}

void MobileXinCunsiCard::onEffect(CardEffectStruct &effect) const
{
    effect.from->turnOver();
    if (effect.to->isDead()) return;
    Room *room = effect.from->getRoom();
    QList<int> slashs, ids = room->getDrawPile() + room->getDiscardPile();
    foreach (int id, ids) {
        if (Sanguosha->getCard(id)->isKindOf("Slash"))
            slashs << id;
    }
    if (!slashs.isEmpty())
        room->obtainCard(effect.to, slashs.at(qrand() % slashs.length()));
    room->addPlayerMark(effect.to, "&mobilexincunsi");
}

class MobileXinCunsiVS : public ZeroCardViewAsSkill
{
public:
    MobileXinCunsiVS() : ZeroCardViewAsSkill("mobilexincunsi")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileXinCunsiCard") && player->faceUp();
    }

    const Card *viewAs() const
    {
        return new MobileXinCunsiCard;
    }
};

class MobileXinCunsi : public TriggerSkill
{
public:
    MobileXinCunsi() : TriggerSkill("mobilexincunsi")
    {
        events << DamageCaused << PreCardUsed;
        view_as_skill = new MobileXinCunsiVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == PreCardUsed) {
            int mark = player->getMark("&mobilexincunsi");
            if (mark <= 0) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash")) return false;
            room->setPlayerMark(player, "&mobilexincunsi", 0);
            int n = room->getTag("mobilexincunsi_damage_" + use.card->toString()).toInt();
            room->setTag("mobilexincunsi_damage_" + use.card->toString(), n + mark);
        } else {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("Slash")) return false;
            int n = room->getTag("mobilexincunsi_damage_" + damage.card->toString()).toInt();
            room->removeTag("mobilexincunsi_damage_" + damage.card->toString());

            if (n <= 0) return false;

            LogMessage log;
            log.type = "#MobileXinCunsiDamage";
            log.from = damage.from;
            log.arg = QString::number(damage.damage);
            int d = damage.damage + n;
            log.arg2 = QString::number(d);
            room->sendLog(log);

            damage.damage = d;
            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

class MobileXinGuixiu : public TriggerSkill
{
public:
    MobileXinGuixiu() : TriggerSkill("mobilexinguixiu")
    {
        events << Damaged << TurnedOver;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (event == Damaged) {
            if (player->faceUp()) return false;
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            player->turnOver();
        } else {
            if (!player->faceUp()) return false;
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            player->drawCards(1, objectName());
        }
        return false;
    }
};

class SecondMobileXinGuixiu : public PhaseChangeSkill
{
public:
    SecondMobileXinGuixiu() : PhaseChangeSkill("secondmobilexinguixiu")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::Finish) return false;
        int hp = player->getHp();
        if (hp % 2 == 0) {
            if (player->isWounded())
                room->sendCompulsoryTriggerLog(player, this);
            room->recover(player, RecoverStruct("secondmobilexinguixiu", player));
        } else {
            room->sendCompulsoryTriggerLog(player, this);
            player->drawCards(1, objectName());
        }
        return false;
    }
};

class SecondMobileXinQingyu : public TriggerSkill
{
public:
    SecondMobileXinQingyu() : TriggerSkill("secondmobilexinqingyu")
    {
        events << EventPhaseStart << DamageInflicted << Dying;
        shiming_skill = true;
        waked_skills = "secondmobilexinxuancun";
        frequency = NotCompulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->getMark(objectName()) > 0) return false;
        if  (event == DamageInflicted) {
            int num = 0;
            foreach (int id, player->handCards() + player->getEquipsId()) {
                if (!player->canDiscard(player, id)) continue;
                num++;
                if (num > 1) break;
            }
            if (num < 2) return false;
            room->sendCompulsoryTriggerLog(player, this, 1);
            room->askForDiscard(player, objectName(), 2, 2, false, true);
            return true;
        } else if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Start) return false;
            if (player->getLostHp() == 0 && player->isKongcheng()) {
                room->sendShimingLog(player, this);
                room->handleAcquireDetachSkills(player, "secondmobilexinxuancun");
            }
        } else {
            DyingStruct dying = data.value<DyingStruct>();
            if (dying.who != player) return false;
            room->sendShimingLog(player, this, false);
            room->loseMaxHp(player, 1, "secondmobilexinqingyu");
        }
        return false;
    }
};

class SecondMobileXinXuancun : public PhaseChangeSkill
{
public:
    SecondMobileXinXuancun() : PhaseChangeSkill("secondmobilexinxuancun")
    {
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive() && target->getPhase() == Player::NotActive;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (player->isDead()) return false;
            if (p->isDead() || !p->hasSkill(objectName())) continue;
            int draw = p->getHp() - p->getHandcardNum();
            if (draw <= 0 || !p->askForSkillInvoke(this, player)) continue;
            room->broadcastSkillInvoke(this);
            draw = qMin(draw, 2);
            player->drawCards(draw, objectName());
        }
        return false;
    }
};

class MobileXinHeji : public TriggerSkill
{
public:
    MobileXinHeji() : TriggerSkill("mobilexinheji")
    {
        events << CardFinished << CardUsed;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
    {
		CardUseStruct use = data.value<CardUseStruct>();
        if(event==CardUsed){
			if (use.card->isKindOf("Duel")||(use.card->isKindOf("Slash") && use.card->isRed())){
				room->setCardFlag(use.card,"mobilexinhejiBf");
			}
			return false;
		}
        if (use.to.length() != 1) return false;
        ServerPlayer *to = use.to.first();
        if (to->isDead()||!use.card->hasFlag("mobilexinhejiBf")) return false;

        foreach (ServerPlayer *p, room->getOtherPlayers(to)) {
            if (to->isDead()) return false;
            if (p->isDead() || !p->hasSkill(objectName())) continue;

            const Card *card = room->askForCard(p, "Slash,Duel|.|.|hand", "@mobilexinheji-use:" + to->objectName(), data,
                               Card::MethodUse, to, true);
            if (!card) continue;
            LogMessage log;
            log.type = "#InvokeSkill";
            log.arg = "mobilexinheji";
            log.from = p;
            room->sendLog(log);
            room->broadcastSkillInvoke("mobilexinheji");
            room->notifySkillInvoked(p, "mobilexinheji");

            room->useCard(CardUseStruct(card, p, to), false);

            if (!card->isVirtualCard() && p->isAlive()) {
                QList<int> ids = room->getDrawPile() + room->getDiscardPile();
				qShuffle(ids);
                foreach (int id, ids) {
                    if (Sanguosha->getCard(id)->isRed()){
                        room->obtainCard(p,id);
						break;
					}
                }
            }
        }
        return false;
    }
};

MobileXinMouliCard::MobileXinMouliCard()
{
    handling_method = Card::MethodNone;
    will_throw = false;
}

void MobileXinMouliCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->giveCard(effect.from, effect.to, this, "mobilexinmouli");
    if (effect.to->isDead()) return;
    //effect.to->gainMark("&mobilexinli+#" + effect.from->objectName());
    LogMessage log;
    log.type = "#GetMark";
    log.from = effect.to;
    log.arg = "mobilexinli";
    log.arg2 = QString::number(1);
    room->sendLog(log);
    room->addPlayerMark(effect.to, "&mobilexinli+#" + effect.from->objectName());
}

class MobileXinMouliVS : public OneCardViewAsSkill
{
public:
    MobileXinMouliVS() : OneCardViewAsSkill("mobilexinmouli")
    {
        filter_pattern = ".|.|.|hand";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileXinMouliCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        MobileXinMouliCard *card = new MobileXinMouliCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class MobileXinMouli : public TriggerSkill
{
public:
    MobileXinMouli() : TriggerSkill("mobilexinmouli")
    {
        events << MarkChanged << EventPhaseStart << CardFinished;
        view_as_skill = new MobileXinMouliVS;
    }

    bool hasMouLiMark(ServerPlayer *player) const
    {
        foreach (QString mark, player->getMarkNames()) {
            if (!mark.startsWith("&mobilexinli") || player->getMark(mark) <= 0) continue;
            return true;
        }
        return false;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == MarkChanged) {
            MarkStruct mark = data.value<MarkStruct>();
            if (!mark.name.startsWith("&mobilexinli")) return false;
            if (hasMouLiMark(player)) {
                if (player->hasSkill("mobilexinmouli_effect", true) || player->isDead()) return false;
                room->attachSkillToPlayer(player, "mobilexinmouli_effect");
            } else {
                if (!player->hasSkill("mobilexinmouli_effect", true)) return false;
                room->detachSkillFromPlayer(player, "mobilexinmouli_effect", true);
            }
        } else if (event == EventPhaseStart) {
            if (player->getPhase() != Player::RoundStart) return false;

            foreach (ServerPlayer *p, room->getAllPlayers(true))
                room->setPlayerMark(p, "mobilexinmouli_first_finish-Keep", 0);

            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->isDead()) continue;
                int num = p->getMark("&mobilexinli+#" + player->objectName());
                if (num <= 0) continue;

                LogMessage log;
                log.type = "#LoseMark";
                log.from = p;
                log.arg = "mobilexinli";
                log.arg2 = QString::number(num);
                room->sendLog(log);

                room->setPlayerMark(p, "&mobilexinli+#" + player->objectName(), 0);
            }
        } else if (event == CardFinished) {  //偷懒，改成第一次结算完，而不是使用的第一张结算完
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.from || use.from->getMark("mobilexinmouli_first_finish-Keep") > 0) return false;
            if (use.card->isKindOf("Slash") || use.card->isKindOf("Jink")) {
                room->addPlayerMark(use.from, "mobilexinmouli_first_finish-Keep");
                foreach (ServerPlayer *p, room->getAllPlayers()) {
                    if (p->isDead() || !p->hasSkill(objectName())) continue;
                    if (use.from->getMark("&mobilexinli+#" + p->objectName()) > 0) {
                        room->sendCompulsoryTriggerLog(p, this);
                        p->drawCards(3, objectName());
                    }
                }
            }
        }
        return false;
    }
};

class MobileXinMouliEffect : public OneCardViewAsSkill
{
public:
    MobileXinMouliEffect() : OneCardViewAsSkill("mobilexinmouli_effect")
    {
        attached_lord_skill = true;
        response_or_use = true;
    }

    bool viewFilter(const Card *to_select) const
    {
        switch (Sanguosha->currentRoomState()->getCurrentCardUseReason()) {
        case CardUseStruct::CARD_USE_REASON_PLAY: {
            return to_select->isBlack();
        }
        case CardUseStruct::CARD_USE_REASON_RESPONSE:
        case CardUseStruct::CARD_USE_REASON_RESPONSE_USE: {
            QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
            if (pattern.contains("slash") || pattern.contains("Slash"))
                return to_select->isBlack();
            else if (pattern == "jink")
                return to_select->isRed();
            return false;
        }
        default:
            return false;
        }
    }

    bool hasWangling(const Player *player) const
    {
        if (player->hasSkill("mobilexinmouli", true)) return true;
        foreach (const Player *p, player->getAliveSiblings()) {
            if (p->hasSkill("mobilexinmouli", true))
                return true;
        }
        return false;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return hasWangling(player) && Slash::IsAvailable(player);
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
            return false;
        if (!hasWangling(player)) return false;
        return pattern.contains("jink") || pattern.contains("Jink") || pattern.contains("slash") || pattern.contains("Slash");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        if (originalCard->isRed()) {
            Jink *jink = new Jink(originalCard->getSuit(), originalCard->getNumber());
            jink->addSubcard(originalCard);
            jink->setSkillName(objectName());
            return jink;
        } else if (originalCard->isBlack()) {
            Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
            slash->addSubcard(originalCard);
            slash->setSkillName(objectName());
            return slash;
        }
        return nullptr;
    }
};

class MobileXinZifu : public TriggerSkill
{
public:
    MobileXinZifu() : TriggerSkill("mobilexinzifu")
    {
        events << Death;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DeathStruct death = data.value<DeathStruct>();
        if (death.who == player) return false;
        if (death.who->getMark("&mobilexinli+#" + player->objectName()) <= 0) return false;
        room->sendCompulsoryTriggerLog(player, this);
        room->loseMaxHp(player, 2, "mobilexinzifu");
        return false;
    }
};

class MobileXinXunyi : public TriggerSkill
{
public:
    MobileXinXunyi() : TriggerSkill("mobilexinxunyi")
    {
        events << GameStart << Death;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == GameStart) {
            ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@mobilexinxunyi-invoke", true, true);
            if (!target) return false;
            room->broadcastSkillInvoke(this);

            LogMessage log;
            log.type = "#GetMark";
            log.from = target;
            log.arg = "mobilexinyi";
            log.arg2 = QString::number(1);
            room->sendLog(log);
            room->addPlayerMark(target, "&mobilexinyi+#" + player->objectName());
        } else {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who == player) return false;
            int mark = death.who->getMark("&mobilexinyi+#" + player->objectName());
            if (mark <= 0) return false;

            QList<ServerPlayer *> players = room->getOtherPlayers(death.who);
            players.removeOne(player);
            if (players.isEmpty()) return false;
            ServerPlayer *target = room->askForPlayerChosen(player, players, objectName(), "@mobilexinxunyi-transfer", true, true);
            if (!target) return false;
            room->broadcastSkillInvoke(this);

            LogMessage log;
            log.type = "#MobileXinXunyiTransferMark";
            log.from = player;
            log.to << target;
            log.arg = "mobilexinyi";
            log.arg2 = QString::number(mark);
            room->sendLog(log);
            room->setPlayerMark(death.who, "&mobilexinyi+#" + player->objectName(), 0);
            room->addPlayerMark(target, "&mobilexinyi+#" + player->objectName(), mark);
        }
        return false;
    }
};

class MobileXinXunyiEffect : public TriggerSkill
{
public:
    MobileXinXunyiEffect() : TriggerSkill("#mobilexinxunyi")
    {
        events << Damaged << Damage;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }

    QList<ServerPlayer *> getYiTargets(ServerPlayer *player, int type, bool discard) const
    {
        QList<ServerPlayer *> targets;
        Room *room = player->getRoom();

        if (type == 0) {
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->getMark("&mobilexinyi+#" + player->objectName()) > 0 && player->hasSkill("mobilexinxunyi")) {
                    if (!discard || p->canDiscard(p, "he"))
                        targets << p;
                }
            }
        } else {
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (player->getMark("&mobilexinyi+#" + p->objectName()) > 0 && p->hasSkill("mobilexinxunyi")) {
                    if (!discard || p->canDiscard(p, "he"))
                        targets << p;
                }
            }
        }

        return targets;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (event == Damaged) {
            /*for (int num = 0; num < 2; num++) {
                QList<ServerPlayer *> targets = getYiTargets(player, num);
                if (damage.from)
                    targets.removeOne(damage.from);
                foreach (ServerPlayer *target, targets) {
                    for (int i = 0; i < damage.damage; i++) {
                        if (target->isDead() || !target->canDiscard(target, "he")) break;
                        room->sendCompulsoryTriggerLog(player, "mobilexinxunyi", true, true);
                        room->askForDiscard(target, "mobilexinxunyi", 1, 1, false, true);
                    }
                }
            }*/
            if (player->hasSkill("mobilexinxunyi")) {
                QList<ServerPlayer *> targets = getYiTargets(player, 0, true);
                if (damage.from)
                    targets.removeOne(damage.from);
                for (int i = 0; i < damage.damage; i++) {
                    room->sendCompulsoryTriggerLog(player, "mobilexinxunyi", true, true);
                    foreach (ServerPlayer *target, targets) {
                        if (target->isDead() || !target->canDiscard(target, "he") || !player->hasSkill("mobilexinxunyi")) break;
                        room->askForDiscard(target, "mobilexinxunyi", 1, 1, false, true);
                    }
                }
            }

            QList<ServerPlayer *> targets = getYiTargets(player, 1, true);
            if (damage.from)
                targets.removeOne(damage.from);
            for (int i = 0; i < damage.damage; i++) {
                foreach (ServerPlayer *target, targets) {
                    if (target->isDead() || !target->hasSkill("mobilexinxunyi") || !target->canDiscard(target, "he")) continue;
                    room->sendCompulsoryTriggerLog(target, "mobilexinxunyi", true, true);
                    room->askForDiscard(target, "mobilexinxunyi", 1, 1, false, true);
                }
            }
        } else {
            if (player->hasSkill("mobilexinxunyi")) {
                QList<ServerPlayer *> targets = getYiTargets(player, 0, false);
                if (damage.to->getMark("&mobilexinyi+#" + player->objectName()) > 0)
                    targets.removeOne(damage.to);
                for (int i = 0; i < damage.damage; i++) {
                    room->sendCompulsoryTriggerLog(player, "mobilexinxunyi", true, true);
                    //room->drawCards(targets, 1, objectName());
                    foreach (ServerPlayer *target, targets) {
                        if (target->isDead()) continue;
                        target->drawCards(1, "mobilexinxunyi");
                    }
                }
            }

            QList<ServerPlayer *> targets = getYiTargets(player, 1, false);
            if (player->getMark("&mobilexinyi+#" + damage.to->objectName()) > 0)
                targets.removeOne(damage.to);
            foreach (ServerPlayer *target, targets) {
                if (target->isDead() || !target->hasSkill("mobilexinxunyi")) continue;
                room->sendCompulsoryTriggerLog(target, "mobilexinxunyi", true, true);
                for (int i = 0; i < damage.damage; i++) {
                    if (target->isDead() || !target->hasSkill("mobilexinxunyi")) break;
                    target->drawCards(1, "mobilexinxunyi");
                }
            }
        }
        return false;
    }
};

class MobileXinXianghai : public FilterSkill
{
public:
    MobileXinXianghai() : FilterSkill("mobilexinxianghai")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->isKindOf("EquipCard")
		&& Sanguosha->getCardPlace(to_select->getEffectiveId()) == Player::PlaceHand;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Card *ana = new Analeptic(originalCard->getSuit(), originalCard->getNumber());
        ana->setSkillName(objectName());/*
        WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
        card->takeOver(ana);*/
        return ana;
    }
};

class MobileXinXianghaiMax : public MaxCardsSkill
{
public:
    MobileXinXianghaiMax() : MaxCardsSkill("#mobilexinxianghai")
    {
    }

    int getExtra(const Player *target) const
    {
        int reduce = 0;
        foreach (const Player *p, target->getAliveSiblings()) {
            if (p->hasSkill("mobilexinxianghai"))
				reduce--;
        }
        return reduce;
    }
};

MobileXinChuhaiCard::MobileXinChuhaiCard()
{
    target_fixed = true;
}

void MobileXinChuhaiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    source->drawCards(1, "mobilexinchuhai");
    if (source->isDead()) return;

    QList<ServerPlayer *> pindian_targets;
    foreach (ServerPlayer *p, room->getAlivePlayers()) {
        if (!source->canPindian(p)) continue;
        pindian_targets << p;
    }
    if (pindian_targets.isEmpty()) return;

    ServerPlayer *pindian_target = room->askForPlayerChosen(source, pindian_targets, "mobilexinchuhai", "@mobilexinchuhai-invoke", false);
    room->doAnimate(1, source->objectName(), pindian_target->objectName());
    if (!source->canPindian(pindian_target, false)) return;

    if (source->pindian(pindian_target, "mobilexinchuhai")) {
        if (source->isDead() || pindian_target->isDead()) return;

        room->addPlayerMark(source, "mobilexinchuhai_from-PlayClear");
        room->addPlayerMark(pindian_target, "mobilexinchuhai_to-PlayClear");

        if (!pindian_target->isKongcheng()) {
            room->doGongxin(source, pindian_target, QList<int>(), "mobilexinchuhai");

            QList<int> type_ids, get_ids;
            foreach (const Card *c, pindian_target->getHandcards()) {
                int type_id = c->getTypeId();
                if (!type_ids.contains(type_id))
                    type_ids << type_id;
            }

            foreach (int type_id, type_ids) {
                QList<int> cards = room->getDiscardPile() + room->getDrawPile(), list;
                foreach (int id, cards) {
                    if (Sanguosha->getCard(id)->getTypeId() == type_id)
                        list << id;
                }
                if (!list.isEmpty()) {
                    int id = list.at(qrand() % list.length());
                    get_ids << id;
                }
            }

            if (!get_ids.isEmpty()) {
                DummyCard get(get_ids);
                room->obtainCard(source, &get, true);
            }
        }
    }
}

class MobileXinChuhaiVS : public ZeroCardViewAsSkill
{
public:
    MobileXinChuhaiVS() : ZeroCardViewAsSkill("mobilexinchuhai")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileXinChuhaiCard");
    }

    const Card *viewAs() const
    {
        return new MobileXinChuhaiCard;
    }
};

class MobileXinChuhai : public TriggerSkill
{
public:
    MobileXinChuhai() : TriggerSkill("mobilexinchuhai")
    {
        events << Damage;
        view_as_skill = new MobileXinChuhaiVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive() && target->getMark("mobilexinchuhai_from-PlayClear") > 0
			&& target->hasEquipArea() && target->getEquips().length() < 5;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to->getMark("mobilexinchuhai_to-PlayClear") <= 0) return false;

        QList<const Card *> equips;
        foreach (int id, room->getDiscardPile() + room->getDrawPile()) {
            const Card *card = Sanguosha->getCard(id);
            if (!card->isKindOf("EquipCard")) continue;
            const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
            if (!equip) continue;
            if (player->getEquip(equip->location()) || !player->hasEquipArea(equip->location())) continue;
            equips << card;
        }

        if (equips.isEmpty()) return false;

        room->sendCompulsoryTriggerLog(player, this);
        const Card *equip = equips.at(qrand() % equips.length());

        LogMessage log;
        log.type = room->getCardPlace(equip->getEffectiveId()) == Player::DrawPile ? "$MobileXinChuhaiPutEquipFromDrawPile" : "$MobileXinChuhaiPutEquipFromDiscardPile";
        log.from = player;
        log.card_str = equip->toString();
        room->sendLog(log);

        room->moveCardTo(equip, nullptr, player, Player::PlaceEquip,
            CardMoveReason(CardMoveReason::S_REASON_PUT,
            player->objectName(), "mobilexinchuhai", ""));

        return false;
    }
};

class MobileXinMingshi : public MasochismSkill
{
public:
    MobileXinMingshi() : MasochismSkill("mobilexinmingshi")
    {
        frequency = Compulsory;
    }

    void onDamaged(ServerPlayer *player, const DamageStruct &damage) const
    {
        if (!damage.from) return;
        Room *room = player->getRoom();
        for (int i = 0; i < damage.damage; i++) {
            if (damage.from->isDead() || !damage.from->canDiscard(damage.from, "he")) return;
            if (!player->hasSkill(objectName())) return;
            room->sendCompulsoryTriggerLog(player, this);
            room->askForDiscard(damage.from, objectName(), 1, 1, false, true);
        }
    }
};

MobileXinLirangCard::MobileXinLirangCard()
{
    target_fixed = true;
}

void MobileXinLirangCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QList<int> hands = source->handCards(), handcards;
    source->throwAllHandCards(getSkillName());
    if (source->isDead()) return;

    foreach (int id, hands) {
        if (room->getCardPlace(id) == Player::DiscardPile)
            handcards << id;
    }
    if (handcards.isEmpty()) return;

    int hp = source->getHp();
    if (hp < 1) return;

    room->setPlayerFlag(source, "mobilexinlirang_InTempMoving");

    CardMoveReason r(CardMoveReason::S_REASON_UNKNOWN, source->objectName());
    CardsMoveStruct fake_move(handcards, nullptr, source, Player::DiscardPile, Player::PlaceHand, r);
    QList<CardsMoveStruct> moves;
    moves << fake_move;
    QList<ServerPlayer *> _source;
    _source << source;
    room->notifyMoveCards(true, moves, true, _source);
    room->notifyMoveCards(false, moves, true, _source);

    int num = qMin(hp, handcards.length());
    QList<int> ids = room->askForyiji(source, handcards, "mobilexinlirang", false, true, true, num,
                                      room->getOtherPlayers(source), CardMoveReason(), "@mobilexinlirang-give:" + QString::number(num));

    foreach (int id, ids)
        handcards.removeOne(id);
    if (!ids.isEmpty()) {
        CardsMoveStruct move(ids, source, nullptr, Player::PlaceHand, Player::DiscardPile,
            CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, source->objectName(), "mobilexinlirang", ""));
        QList<CardsMoveStruct> moves;
        moves.append(move);
        room->notifyMoveCards(true, moves, false, _source);
        room->notifyMoveCards(false, moves, false, _source);
    }

    if (!handcards.isEmpty()) {
        CardsMoveStruct fake_move2(handcards, source, nullptr, Player::PlaceHand, Player::DiscardPile, r);
        QList<CardsMoveStruct> moves2;
        moves2 << fake_move2;
        room->notifyMoveCards(true, moves2, true, _source);
        room->notifyMoveCards(false, moves2, true, _source);
    }

    room->setPlayerFlag(source, "-mobilexinlirang_InTempMoving");

    if (ids.isEmpty()) return;
    source->drawCards(1, "mobilexinlirang");
}

class MobileXinLirang : public ZeroCardViewAsSkill
{
public:
    MobileXinLirang() : ZeroCardViewAsSkill("mobilexinlirang")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileXinLirangCard") && player->canDiscard(player, "h");
    }

    const Card *viewAs() const
    {
        return new MobileXinLirangCard;
    }
};

class MobileXinMingfa : public PhaseChangeSkill
{
public:
    MobileXinMingfa() : PhaseChangeSkill("mobilexinmingfa")
    {
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::Finish || player->isNude()) return false;
        const Card *card = room->askForCard(player, "..", "@mobilexinmingfa-show", QVariant(), Card::MethodNone);
        if (!card) return false;
        player->tag["MobileXinMingfaCard"] = QVariant::fromValue(card);
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = player;
        log.arg = objectName();
        room->sendLog(log);
        room->broadcastSkillInvoke(this);
        room->notifySkillInvoked(player, objectName());
        room->showCard(player, card->getEffectiveId());
        return false;
    }
};

class MobileXinMingfaPindian : public TriggerSkill
{
public:
    MobileXinMingfaPindian() : TriggerSkill("#mobilexinmingfa-pindian")
    {
        events << EventPhaseStart << PindianVerifying;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
            if (player->isDead() || player->getPhase() != Player::Play) return false;
            const Card *card = player->tag["MobileXinMingfaCard"].value<const Card *>();
            player->tag.remove("MobileXinMingfaCard");
            if (!card || !player->hasCard(card->getEffectiveId())) return false;
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (!player->canPindian(p)) continue;
                targets << p;
            }
            if (targets.isEmpty()) return false;
            room->fillAG(QList<int>() << card->getEffectiveId(), player);
            ServerPlayer *t = room->askForPlayerChosen(player, targets, "mobilexinmingfa", "@mobilexinmingfa-pindian", false, true);
            room->clearAG(player);
            room->broadcastSkillInvoke("mobilexinmingfa");

            PindianStruct *pindian = player->PinDian(t, "mobilexinmingfa", card);
            if (player->isDead()) return false;
            if (pindian->success) {
                if (t->isAlive() && !t->isNude()) {
                    int id = room->askForCardChosen(player, t, "he", "mobilexinmingfa");
                    room->obtainCard(player, id, false);
                }
                int number = Sanguosha->getEngineCard(pindian->from_card->getEffectiveId())->getNumber() - 1;
                QList<int> ids;
                foreach (int id, room->getDrawPile()) {
                    if (Sanguosha->getCard(id)->getNumber() == number)
                        ids << id;
                }
                if (ids.isEmpty()) return false;
                int id = ids.at(qrand() % ids.length());
                room->obtainCard(player, id);
            } else
                room->addPlayerMark(player, "mobilexinmingfa-Clear");
        } else {
            PindianStruct *pindian = data.value<PindianStruct *>();
            QList<ServerPlayer *> pindian_players;
            pindian_players << pindian->from << pindian->to;
            room->sortByActionOrder(pindian_players);

            foreach (ServerPlayer *p, pindian_players) {
                LogMessage log;
                log.type = "#MobileXinMingfaPindian";
                log.from = p;
                log.arg = "mobilexinmingfa";
                if (p->hasSkill("mobilexinmingfa") && p == pindian->from) {
                    pindian->from_number += 2;
                    pindian->from_number = qMin(pindian->from_number, 13);
                    log.arg2 = QString::number(pindian->from_number);
                    room->sendLog(log);
                    room->broadcastSkillInvoke("mobilexinmingfa");
                    room->notifySkillInvoked(p, "mobilexinmingfa");
                    data = QVariant::fromValue(pindian);
                } else if (p->hasSkill("mobilexinmingfa") && p == pindian->to) {
                    pindian->to_number += 2;
                    pindian->to_number = qMin(pindian->to_number, 13);
                    log.arg2 = QString::number(pindian->to_number);
                    room->sendLog(log);
                    room->broadcastSkillInvoke("mobilexinmingfa");
                    room->notifySkillInvoked(p, "mobilexinmingfa");
                    data = QVariant::fromValue(pindian);
                }
            }
        }
        return false;
    }
};

class MobileXinMingfaPro : public ProhibitSkill
{
public:
    MobileXinMingfaPro() : ProhibitSkill("#mobilexinmingfa-pro")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return from->getMark("mobilexinmingfa-Clear") > 0 && from != to && !card->isKindOf("SkillCard");
    }
};

MobileXinRongbeiCard::MobileXinRongbeiCard()
{
}

bool MobileXinRongbeiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.isEmpty() && to_select->getEquips().length() < S_EQUIP_AREA_LENGTH;
}

void MobileXinRongbeiCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *to = effect.to;
    Room *room = to->getRoom();

    room->removePlayerMark(effect.from, "@mobilexinrongbeiMark");
    room->doSuperLightbox(effect.from, "mobilexinrongbei");

    QList<int> areas;

    for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
        if (to->getEquip(i)) continue;
        areas << i;
    }
    if (areas.isEmpty()) return;

    QList<const EquipCard *> equips;
    foreach (int id, room->getDrawPile()) {
        const Card *card = Sanguosha->getCard(id);
        if (!card->isKindOf("EquipCard")) continue;
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        int equip_index = static_cast<int>(equip->location());
        if (to->getEquip(equip_index)) continue;
        equips << equip;
    }
    if (equips.isEmpty()) return;

    for (int i = 0; i < areas.length(); i++) {
        if (to->isDead()) return;
        int area = areas.at(i);

        QList<const Card *> equip_cards;
        foreach (const EquipCard *ec, equips) {
            int equip_index = static_cast<int>(ec->location());
            if (equip_index == area)
                equip_cards << ec;
        }
        if (equip_cards.isEmpty()) continue;
        const Card *equip = equip_cards.at(qrand() % equip_cards.length());
        room->obtainCard(to, equip);
        if (to->isAlive() && to->hasEquipArea(area) && !to->isLocked(equip, true) && !to->isProhibited(to, equip))
            room->useCard(CardUseStruct(equip, to, to));
    }
}

class MobileXinRongbei : public ZeroCardViewAsSkill
{
public:
    MobileXinRongbei() : ZeroCardViewAsSkill("mobilexinrongbei")
    {
        frequency = Limited;
        limit_mark = "@mobilexinrongbeiMark";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("@mobilexinrongbeiMark") > 0;
    }

    const Card *viewAs() const
    {
        return new MobileXinRongbeiCard;
    }
};

class SecondMobileXinXingqi : public TriggerSkill
{
public:
    SecondMobileXinXingqi() : TriggerSkill("secondmobilexinxingqi")
    {
        events << CardUsed << EventPhaseStart;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        QStringList beis;
        QString bei = player->property("second_mobilexin_wangling_bei").toString();
        if (!bei.isEmpty()) beis = bei.split("+");

        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Finish) return false;
            if (beis.isEmpty()) return false;
            if (!player->askForSkillInvoke(this)) return false;
            room->broadcastSkillInvoke(this);
            QString choice = room->askForChoice(player, objectName(), bei);
            beis.removeOne(choice);
            room->setPlayerProperty(player, "second_mobilexin_wangling_bei", beis.join("+"));

            LogMessage log;
            log.type = "#SecondMobileXinXingqiRemove";
            log.from = player;
            log.arg = choice;
            room->sendLog(log);

            QList<int> ids;
            foreach (int id, room->getDrawPile()) {
                if (Sanguosha->getCard(id)->sameNameWith(choice))
					ids << id;
            }
            if (ids.isEmpty()) return false;

            int id = ids.at(qrand() % ids.length());
            room->obtainCard(player, id);
        } else {
            const Card *card = nullptr;
            if (event == CardUsed)
                card = data.value<CardUseStruct>().card;
            if (!card || card->isKindOf("DelayedTrick") || card->isKindOf("SkillCard")) return false;

            QString name = card->objectName();
            if (card->isKindOf("Slash"))
                name = "slash";
            if (beis.contains(name)) return false;

            LogMessage log;
            log.type = "#SecondMobileXinXingqiLog";
            log.from = player;
            log.arg = name;
            room->sendLog(log);
            //room->broadcastSkillInvoke(this);
            room->notifySkillInvoked(player, objectName());

            beis << name;
            room->setPlayerProperty(player, "second_mobilexin_wangling_bei", beis.join("+"));
        }
        return false;
    }
};

class SecondMobileXinZifu : public TriggerSkill
{
public:
    SecondMobileXinZifu() : TriggerSkill("secondmobilexinzifu")
    {
        events << EventPhaseEnd;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() != Player::Play) return false;
        if (player->getMark("secondmobilexinzifu-PlayClear") > 0) return false;
        room->sendCompulsoryTriggerLog(player, this);
        room->addMaxCards(player, -1);
        room->setPlayerProperty(player, "second_mobilexin_wangling_bei", "");
        return false;
    }
};

class SecondMobileXinMibei : public TriggerSkill
{
public:
    SecondMobileXinMibei() : TriggerSkill("secondmobilexinmibei")
    {
        events << EventPhaseEnd << CardFinished;
        shiming_skill = true;
        waked_skills = "secondmobilexinmouli";
        frequency = NotCompulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->getMark("secondmobilexinmibei") > 0) return false;
        if (event == EventPhaseEnd) {
            if (player->getPhase() != Player::Discard) return false;
            if (player->getMark("secondmobilexinmibei-Clear") <= 0) return false;
            QString bei = player->property("second_mobilexin_wangling_bei").toString();
            if (!bei.isEmpty()) return false;
            room->sendShimingLog(player, this, false, 2);
            room->loseMaxHp(player, 1, "secondmobilexinmibei");
        } else {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SkillCard")) return false;
            QString bei = player->property("second_mobilexin_wangling_bei").toString();
            if (bei.isEmpty()) return false;
            QStringList beis = bei.split("+");
            if (beis.length() < 2 * (S_CARD_TYPE_LENGTH - 1)) return false;

            QHash<QString, int> hash;
            foreach (QString name, beis) {
                const Card *card = Sanguosha->findChild<const Card *>(name);
                if (card) hash[card->getType()]++;
            }

            int basic = hash["basic"], equip = hash["equip"], trick = hash["trick"];
            if (basic < 2 || equip < 2 || trick < 2) return false;

            room->sendShimingLog(player, this, true, 1);
            DummyCard *dummy = new DummyCard();
            foreach (int id, room->getDrawPile()) {
                const Card *card = Sanguosha->getCard(id);
                if(hash[card->getType()]>1){
					dummy->addSubcard(id);
					hash[card->getType()] = 0;
				}
            }
            room->obtainCard(player, dummy);
            dummy->deleteLater();

            room->acquireSkill(player, "secondmobilexinmouli");
        }
        return false;
    }
};

SecondMobileXinMouliCard::SecondMobileXinMouliCard()
{
}

void SecondMobileXinMouliCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *from = effect.from, *to = effect.to;
    if (from->isDead()) return;
    QString bei = from->property("second_mobilexin_wangling_bei").toString();
    if (bei.isEmpty()) return;

    QStringList beis = bei.split("+");
    Room *room = from->getRoom();

    QString choice = room->askForChoice(to, "secondmobilexinmouli", bei);
    beis.removeOne(choice);
    room->setPlayerProperty(from, "second_mobilexin_wangling_bei", beis.join("+"));

    LogMessage log;
    log.type = "#SecondMobileXinXingqiRemove";
    log.from = from;
    log.arg = choice;
    room->sendLog(log);

    foreach (int id, room->getDrawPile()) {
        if (Sanguosha->getCard(id)->sameNameWith(choice)){
			room->obtainCard(to, id);
			break;
		}
    }
}

class SecondMobileXinMouli : public ZeroCardViewAsSkill
{
public:
    SecondMobileXinMouli() : ZeroCardViewAsSkill("secondmobilexinmouli")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("SecondMobileXinMouliCard") && !player->property("second_mobilexin_wangling_bei").toString().isEmpty();
    }

    const Card *viewAs() const
    {
        return new SecondMobileXinMouliCard;
    }
};

class XinDulie : public TriggerSkill
{
public:
    XinDulie() : TriggerSkill("xindulie")
    {
        events << TargetConfirming;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == TargetConfirming) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Slash")&&use.from->getHp()>player->getHp()){
				room->sendCompulsoryTriggerLog(player,objectName());
				player->peiyin("dulie");
				JudgeStruct judge;
				judge.who = player;
				judge.reason = objectName();
				judge.pattern = ".|heart";
				judge.good = true;
				room->judge(judge);
				if (judge.isGood()){
					use.to.removeOne(player);
					data.setValue(use);
				}
			}
        }
        return false;
    }
};

class XinPowei : public TriggerSkill
{
public:
    XinPowei() : TriggerSkill("xinpowei")
    {
		shiming_skill = true;
        events << GameStart << Damaged << EventPhaseStart << Dying;
		waked_skills = "xinshenzhuo";
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == Damaged) {
            player->loseAllMarks("&stscdlwei");
        } else if(event == Dying){
            DyingStruct dying = data.value<DyingStruct>();
			if(dying.who==player&&player->hasSkill(objectName())&&player->getMark(objectName())<1){
				player->peiyin("powei",3);
				room->sendShimingLog(player,this,false);
				room->recover(player,RecoverStruct(objectName(),player,1-player->getHp()));
				foreach (ServerPlayer *p, room->getAllPlayers())
					p->loseAllMarks("&stscdlwei");
				player->throwAllEquips(objectName());
			}
        } else if(event == EventPhaseStart){
			if(player->getPhase()==Player::RoundStart){
				if(player->hasSkill(objectName())&&player->getMark(objectName())<1){
					bool has = true;
					foreach (ServerPlayer *p, room->getAlivePlayers()) {
						has = p->getMark("&stscdlwei")>0;
						if(has) break;
					}
					if(has){
						room->sendCompulsoryTriggerLog(player,objectName());
						player->peiyin("powei",1);
						QList<ServerPlayer *>tps;
						foreach (ServerPlayer *p, room->getAllPlayers()) {
							if(p->getMark("&stscdlwei")>0){
								ServerPlayer *tp = p->getNextAlive();
								if(tp==player) tp = tp->getNextAlive();
								if(tp!=p){
									p->loseAllMarks("&stscdlwei");
									tps << tp;
								}
							}
						}
						foreach (ServerPlayer *p, tps) {
							p->gainMark("&stscdlwei");
						}
					}else{
						player->peiyin("powei",2);
						room->sendShimingLog(player,this,true);
						room->acquireSkill(player,"xinshenzhuo");
					}
				}
				if(player->getMark("&stscdlwei")>0){
					foreach (ServerPlayer *p, room->getAllPlayers()) {
						if(p->hasSkill(objectName())&&p->askForSkillInvoke(this,player)){
							p->peiyin("powei",1);
							room->insertAttackRangePair(player,p);
							player->addMark("stscdlwei"+p->objectName());
							if(room->askForCard(p,".","xinpowei0:"+player->objectName(),QVariant::fromValue(player))){
								room->damage(DamageStruct(objectName(),p,player));
							}else if(p->getHp()>=player->getHp()&&player->getHandcardNum()>0){
								int id = room->askForCardChosen(p,player,"h",objectName());
								if(id>=0) room->obtainCard(p,id,false);
							}
						}
					}
				}
			}else if(player->getPhase()==Player::NotActive){
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->getMark("stscdlwei"+p->objectName())>0){
						player->removeMark("stscdlwei"+p->objectName());
						room->removeAttackRangePair(player,p);
					}
				}
			}
        } else {
            if (player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,objectName());
				player->peiyin("powei",1);
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(p->getMark("&stscdlwei")<1) p->gainMark("&stscdlwei");
				}
			}
        }
        return false;
    }
};

class XinShenzhuo : public TriggerSkill
{
public:
    XinShenzhuo() : TriggerSkill("xinshenzhuo")
    {
        events << CardFinished << EventPhaseChanging << CardUsed;
        frequency = Compulsory;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardFinished) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->hasFlag("xinshenzhuoBf")&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				if(room->askForChoice(player,objectName(),"xinshenzhuo1+xinshenzhuo2",data)=="xinshenzhuo1"){
					player->drawCards(1,objectName());
					room->addSlashCishu(player,1);
				}else{
					player->drawCards(3,objectName());
					room->setPlayerCardLimitation(player,"use","Slash",false);
					player->addMark("xinshenzhuoBan-Clear");
				}
			}
        }else if (event == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Slash")&&!use.card->isVirtualCard()){
				room->setCardFlag(use.card,"xinshenzhuoBf");
			}
        }else{
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if(p->getMark("xinshenzhuoBan-Clear")>0)
					room->removePlayerCardLimitation(p,"use","Slash");
			}
		}
        return false;
    }
};







MobileXinPackage::MobileXinPackage()
    : Package("mobilexin")
{
    General *mobilexin_xinpi = new General(this, "mobilexin_xinpi", "wei", 3);
    mobilexin_xinpi->addSkill(new MobileXinYinju);
    mobilexin_xinpi->addSkill(new MobileXinChijie);

    General *mobilexin_mifuren = new General(this, "mobilexin_mifuren", "shu", 3, false);
    mobilexin_mifuren->addSkill(new MobileXinCunsi);
    mobilexin_mifuren->addSkill(new MobileXinGuixiu);

    General *second_mobilexin_mifuren = new General(this, "second_mobilexin_mifuren", "shu", 3, false);
    second_mobilexin_mifuren->addSkill(new SecondMobileXinGuixiu);
    second_mobilexin_mifuren->addSkill(new SecondMobileXinQingyu);
    second_mobilexin_mifuren->addRelateSkill("secondmobilexinxuancun");

    General *mobilexin_wujing = new General(this, "mobilexin_wujing", "wu", 4);
    mobilexin_wujing->addSkill(new MobileXinHeji);

    General *mobilexin_wangling = new General(this, "mobilexin_wangling", "wei", 4);
    mobilexin_wangling->addSkill(new MobileXinMouli);
    mobilexin_wangling->addSkill(new MobileXinZifu);

    General *second_mobilexin_wangling = new General(this, "second_mobilexin_wangling", "wei", 4);
    second_mobilexin_wangling->addSkill(new SecondMobileXinXingqi);
    second_mobilexin_wangling->addSkill(new SecondMobileXinZifu);
    second_mobilexin_wangling->addSkill(new SecondMobileXinMibei);
    second_mobilexin_wangling->addRelateSkill("secondmobilexinmouli");

    General *mobilexin_wangfuzhaolei = new General(this, "mobilexin_wangfuzhaolei", "shu", 4);
    mobilexin_wangfuzhaolei->addSkill(new MobileXinXunyi);
    mobilexin_wangfuzhaolei->addSkill(new MobileXinXunyiEffect);
    related_skills.insertMulti("mobilexinxunyi", "#mobilexinxunyi");

    General *mobilexin_zhouchu = new General(this, "mobilexin_zhouchu", "wu", 4);
    mobilexin_zhouchu->addSkill(new MobileXinXianghai);
    mobilexin_zhouchu->addSkill(new MobileXinXianghaiMax);
    mobilexin_zhouchu->addSkill(new MobileXinChuhai);
    related_skills.insertMulti("mobilexinxianghai", "#mobilexinxianghai");

    General *mobilexin_kongrong = new General(this, "mobilexin_kongrong", "qun", 3);
    mobilexin_kongrong->addSkill(new MobileXinMingshi);
    mobilexin_kongrong->addSkill(new MobileXinLirang);

    General *mobilexin_yanghu = new General(this, "mobilexin_yanghu", "qun", 3);
    mobilexin_yanghu->addSkill(new MobileXinMingfa);
    mobilexin_yanghu->addSkill(new MobileXinMingfaPindian);
    mobilexin_yanghu->addSkill(new MobileXinMingfaPro);
    mobilexin_yanghu->addSkill(new MobileXinRongbei);
    related_skills.insertMulti("mobilexinmingfa", "#mobilexinmingfa-pindian");
    related_skills.insertMulti("mobilexinmingfa", "#mobilexinmingfa-pro");

    addMetaObject<MobileXinYinjuCard>();
    addMetaObject<MobileXinCunsiCard>();
    addMetaObject<MobileXinMouliCard>();
    addMetaObject<MobileXinChuhaiCard>();
    addMetaObject<MobileXinLirangCard>();
    addMetaObject<MobileXinRongbeiCard>();
    addMetaObject<SecondMobileXinMouliCard>();

    General *xin_shentaishici = new General(this, "xin_shentaishici", "god", 4);
    xin_shentaishici->addSkill(new XinDulie);
    xin_shentaishici->addSkill(new XinPowei);
	skills << new XinShenzhuo;

    skills << new SecondMobileXinXuancun << new MobileXinMouliEffect << new SecondMobileXinMouli;
}
ADD_PACKAGE(MobileXin)


MobileRenRenshiCard::MobileRenRenshiCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool MobileRenRenshiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && to_select->getMark("mobilerenrenshi-PlayClear") <= 0;
}

void MobileRenRenshiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->addPlayerMark(effect.to, "mobilerenrenshi-PlayClear");
    room->giveCard(effect.from, effect.to, this, "mobilerenrenshi");
}

class MobileRenRenshi : public OneCardViewAsSkill
{
public:
    MobileRenRenshi() : OneCardViewAsSkill("mobilerenrenshi")
    {
        filter_pattern = ".|.|.|hand";
    }

    const Card *viewAs(const Card *card) const
    {
        MobileRenRenshiCard *c = new MobileRenRenshiCard;
        c->addSubcard(card);
        return c;
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return true;
    }
};

MobileRenBuqiCard::MobileRenBuqiCard()
{
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void MobileRenBuqiCard::onUse(Room *room, CardUseStruct &card_use) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, card_use.from->objectName(), "mobilerenbuqi", "");
    room->throwCard(this, reason, nullptr);
}

class MobileRenBuqiVS : public ViewAsSkill
{
public:
    MobileRenBuqiVS() : ViewAsSkill("mobilerenbuqi")
    {
        expand_pile = "mrhxren";
        response_pattern = "@@mobilerenbuqi";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        return selected.length() < 2 && Self->getPile("mrhxren").contains(to_select->getEffectiveId());
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != 2)
            return nullptr;

        MobileRenBuqiCard *card = new MobileRenBuqiCard;
        card->addSubcards(cards);
        return card;
    }
};

class MobileRenBuqi : public TriggerSkill
{
public:
    MobileRenBuqi() : TriggerSkill("mobilerenbuqi")
    {
        events << Dying << Death;
        frequency = Compulsory;
        view_as_skill = new MobileRenBuqiVS;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        QList<int> rens = player->getPile("mrhxren");
        if (event == Dying) {
            if (rens.length() < 2) return false;
            DyingStruct dying = data.value<DyingStruct>();
            //if (dying.who == player) return false;
            room->sendCompulsoryTriggerLog(player, this);

            DummyCard *dummy = new DummyCard;
            dummy->deleteLater();

            if (rens.length() == 2) {
                dummy->addSubcards(rens);
                CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, player->objectName(), "mobilerenbuqi", "");
                room->throwCard(dummy, reason, nullptr);
                room->recover(dying.who, RecoverStruct("mobilerenbuqi", player));
            } else {
                if (room->askForUseCard(player, "@@mobilerenbuqi", "@mobilerenbuqi", -1, Card::MethodNone))
                    room->recover(dying.who, RecoverStruct("mobilerenbuqi", player));
                else {
                    dummy->addSubcard(rens.first());
                    dummy->addSubcard(rens.last());
                    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, player->objectName(), "mobilerenbuqi", "");
                    room->throwCard(dummy, reason, nullptr);
                    room->recover(dying.who, RecoverStruct("mobilerenbuqi", player));
                }
            }
        } else {
            if (rens.isEmpty()) return false;
            room->sendCompulsoryTriggerLog(player, this);
            player->clearOnePrivatePile("mrhxren");
        }
        return false;
    }
};

class MobileRenDebao : public TriggerSkill
{
public:
    MobileRenDebao() : TriggerSkill("mobilerendebao")
    {
        events << CardsMoveOneTime << EventPhaseStart;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from == player && move.to && move.to != player && player->getPile("mrhxren").length() < player->getMaxHp()
				&& (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))) {
                room->sendCompulsoryTriggerLog(player, this);
                player->addToPile("mrhxren", room->drawCard());
            }
        } else {
            if (player->getPhase() != Player::Start || player->getPile("mrhxren").isEmpty()) return false;
            room->sendCompulsoryTriggerLog(player, this);
            LogMessage log;
            log.type = "$KuangbiGet";
            log.from = player;
            log.arg = "mrhxren";
            log.card_str = ListI2S(player->getPile("mrhxren")).join("+");
            room->sendLog(log);
            DummyCard dummy(player->getPile("mrhxren"));
            room->obtainCard(player, &dummy, true);
        }
        return false;
    }
};

class MobileRenSheyi : public TriggerSkill
{
public:
    MobileRenSheyi() : TriggerSkill("mobilerensheyi")
    {
        events << DamageInflicted;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->isDead() || !p->hasSkill(objectName()) || p->getHp() <= player->getHp()) continue;
            int give_num = qMax(1, p->getHp());
            if (p->getCardCount() < give_num) continue;

            p->tag["mobilerensheyi_data"] = data;
            QString prompt = QString("@mobilerensheyi-give:%1:%2:%3").arg(player->objectName()).arg(give_num).arg(damage.damage);
            const Card *card = room->askForExchange(p, objectName(), 999, give_num, true, prompt, true);
            p->tag.remove("mobilerensheyi_data");
            if (!card) continue;

            LogMessage log;
            log.type = "#InvokeSkill";
            log.from = p;
            log.arg = objectName();
            room->sendLog(log);
            room->broadcastSkillInvoke(this);
            room->notifySkillInvoked(p, objectName());

            room->giveCard(p, player, card, objectName());
            return true;
        }
        return false;
    }
};

class MobileRenTianyin : public PhaseChangeSkill
{
public:
    MobileRenTianyin() : PhaseChangeSkill("mobilerentianyin")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::Finish) return false;
        QList<int> basics, tricks, equips;

        foreach (int id, room->getDrawPile()) {
            const Card *card = Sanguosha->getCard(id);
            if (player->getMark("mobilerentianyin_" + card->getType() + "-Clear") > 0) continue;
            if (card->isKindOf("BasicCard"))
                basics << id;
            else if (card->isKindOf("TrickCard"))
                tricks << id;
            else if (card->isKindOf("EquipCard"))
                equips << id;
        }

        DummyCard *dummy = new DummyCard;
        dummy->deleteLater();
        if (!basics.isEmpty()) {
            int id = basics.at(qrand() % basics.length());
            dummy->addSubcard(id);
        }
        if (!tricks.isEmpty()) {
            int id = tricks.at(qrand() % tricks.length());
            dummy->addSubcard(id);
        }
        if (!equips.isEmpty()) {
            int id = equips.at(qrand() % equips.length());
            dummy->addSubcard(id);
        }
        if (dummy->subcardsLength() == 0) return false;
        room->sendCompulsoryTriggerLog(player, this);
        room->obtainCard(player, dummy, true);
        return false;
    }
};

MobileRenBomingCard::MobileRenBomingCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

void MobileRenBomingCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->giveCard(effect.from, effect.to, this, "mobilerenboming");
}

class MobileRenBomingVS : public OneCardViewAsSkill
{
public:
    MobileRenBomingVS() : OneCardViewAsSkill("mobilerenboming")
    {
        filter_pattern = ".";
    }

    const Card *viewAs(const Card *card) const
    {
        MobileRenBomingCard *c = new MobileRenBomingCard;
        c->addSubcard(card);
        return c;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->usedTimes("MobileRenBomingCard") < 2;
    }
};

class MobileRenBoming : public TriggerSkill
{
public:
    MobileRenBoming() : TriggerSkill("mobilerenboming")
    {
        events << PreCardUsed << EventPhaseEnd << EventPhaseStart;
        view_as_skill = new MobileRenBomingVS;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == PreCardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("MobileRenBomingCard") || player->getPhase() != Player::Play) return false;
            room->addPlayerMark(player, "mobilerenboming-PlayClear");
        } else if (event == EventPhaseEnd) {
            if (player->getPhase() != Player::Play) return false;
            int mark = player->getMark("mobilerenboming-PlayClear");
            room->setPlayerMark(player, "mobilerenboming-PlayClear", 0);
            if (mark >= 2)
                room->addPlayerMark(player, "mobilerenboming-Clear");
        } else if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Finish) return false;
            int mark = player->getMark("mobilerenboming-Clear");
            for (int i = 0; i < mark; i++) {
                room->sendCompulsoryTriggerLog(player, this);
                player->drawCards(1, objectName());
            }
        }
        return false;
    }
};

class MobileRenEjian : public TriggerSkill
{
public:
    MobileRenEjian() : TriggerSkill("mobilerenejian")
    {
        events << CardsMoveOneTime;
        frequency = Compulsory;
    }

    bool hasSameType(ServerPlayer *player, const Card *card) const
    {
        foreach (const Card *c, player->getCards("he")) {
            if (c == card) continue;
            if (c->getType() == card->getType())
                return true;
        }
        return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!move.to || move.to->isDead() || move.to == player || move.reason.m_skillName != "mobilerenboming") return false;
        if (move.to_place != Player::PlaceHand) return false;

        ServerPlayer *to = (ServerPlayer *)move.to;
        foreach (int id, move.card_ids) {
            QStringList ejian_names = player->tag["mobilerenejian_names"].toStringList();
            if (ejian_names.contains(move.to->objectName())) return false;

            const Card *card = Sanguosha->getCard(id);
            if (!hasSameType(to, card)) continue;

            room->sendCompulsoryTriggerLog(player, this);
            ejian_names << move.to->objectName();
            player->tag["mobilerenejian_names"] = ejian_names;
            room->setPlayerMark(to, "&mobilerenejian", 1);

            QStringList choices;
            choices << "damage" << "discard=" + card->getType();
            if (room->askForChoice(to, objectName(), choices.join("+"), QVariant::fromValue(player)) == "damage")
                room->damage(DamageStruct("mobilerenejian", nullptr, to));
            else {
                room->showAllCards(to);
                DummyCard *dummy = new DummyCard();
                dummy->deleteLater();
                foreach (const Card *c, to->getCards("he")) {
                    if (c->getType() == card->getType() && to->canDiscard(to, c->getEffectiveId()))
                        dummy->addSubcard(c);
                }
                if (dummy->subcardsLength() > 0)
                    room->throwCard(dummy, to);
            }
        }
        return false;
    }
};

class MobileRenGuying : public TriggerSkill
{
public:
    MobileRenGuying() : TriggerSkill("mobilerenguying")
    {
        events << CardsMoveOneTime << EventPhaseStart;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardsMoveOneTime) {
			if (!player->hasTurn()) return false;
            if (player->getMark("mobilerenguying-Clear") > 0 || player->getPhase() != Player::NotActive) return false;
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != player || move.card_ids.length() != 1) return false;
            if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
                || (move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE
				|| move.reason.m_reason == CardMoveReason::S_REASON_RESPONSE) {
                room->sendCompulsoryTriggerLog(player, this);
                room->addPlayerMark(player, "mobilerenguying-Clear");
                room->addPlayerMark(player, "&mobilerenguying");

                ServerPlayer *current = room->getCurrent();
                const Card *card = Sanguosha->getEngineCard(move.card_ids.first());

                QStringList choices;
                if (!current->isNude())
                    choices << "give=" + player->objectName();
                choices << "obtain=" + player->objectName() + "=" + card->objectName();
                if (room->askForChoice(current, objectName(), choices.join("+"), QVariant::fromValue(player)).startsWith("give")) {
                    if (player->isDead()) return false;
                    const Card *give = current->getCards("he").at(qrand() % current->getCardCount());
                    room->giveCard(current, player, give, objectName());
                } else {
                    if (player->isDead()) return false;
                    room->obtainCard(player, card, true);
                    const Card *obtain = Sanguosha->getCard(card->getEffectiveId());
                    if (obtain->isKindOf("EquipCard") && player->canUse(obtain))
                        room->useCard(CardUseStruct(obtain, player, player));
                }
            }
        } else {
            if (player->getPhase() != Player::Start || player->getMark("&mobilerenguying") <= 0) return false;
            int mark = player->getMark("&mobilerenguying");
            room->askForDiscard(player, objectName(), mark, mark, false, true);
            room->setPlayerMark(player, "&mobilerenguying", 0);
        }
        return false;
    }
};

MobileRenMuzhenCard::MobileRenMuzhenCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool MobileRenMuzhenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    int num = subcardsLength();
    if (num == 1) {
        const Card *card = Sanguosha->getCard(getEffectiveId());
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        if (!equip) return false;
        int equip_index = static_cast<int>(equip->location());
        return to_select->getEquip(equip_index) == nullptr && !Self->isProhibited(to_select, card) && targets.isEmpty() && to_select != Self;
    } else if (num == 2)
        return !to_select->getEquips().isEmpty() && targets.isEmpty() && to_select != Self;

    return false;
}

void MobileRenMuzhenCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    int num = subcardsLength();
    if (num == 1) {
        room->addPlayerMark(effect.from, "mobilerenmuzhen_put-PlayClear");

        LogMessage log;
        log.type = "$ZhijianEquip";
        log.from = effect.to;
        log.card_str = QString::number(getEffectiveId());
        room->sendLog(log);

        room->moveCardTo(this, effect.from, effect.to, Player::PlaceEquip,
            CardMoveReason(CardMoveReason::S_REASON_PUT, effect.from->objectName(), "mobilerenmuzhen", ""));

        if (effect.from->isDead() || effect.to->isDead() || effect.to->isKongcheng()) return;
        int id = room->askForCardChosen(effect.from, effect.to, "h", "mobilerenmuzhen");
        CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName());
        room->obtainCard(effect.from, Sanguosha->getCard(id), reason, false);
    } else if (num == 2) {
        room->addPlayerMark(effect.from, "mobilerenmuzhen_give-PlayClear");

        room->giveCard(effect.from, effect.to, this, "mobilerenmuzhen");
        if (effect.from->isDead() || effect.to->isDead() || effect.to->getEquips().isEmpty()) return;
        int id = room->askForCardChosen(effect.from, effect.to, "e", "mobilerenmuzhen");
        CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName());
        room->obtainCard(effect.from, Sanguosha->getCard(id), reason, false);
    }
}

class MobileRenMuzhen : public ViewAsSkill
{
public:
    MobileRenMuzhen() : ViewAsSkill("mobilerenmuzhen")
    {
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (selected.isEmpty()) return true;
        if (Self->getMark("mobilerenmuzhen_give-PlayClear") > 0)
            return selected.isEmpty() && to_select->isKindOf("EquipCard");
        else if (Self->getMark("mobilerenmuzhen_put-PlayClear") > 0)
            return selected.length() < 2;
        return selected.length() < 2;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty()) return nullptr;
        if (Self->getMark("mobilerenmuzhen_give-PlayClear") > 0 && cards.length() != 1) return nullptr;
        if (Self->getMark("mobilerenmuzhen_put-PlayClear") > 0 && cards.length() != 2) return nullptr;

        MobileRenMuzhenCard *c = new MobileRenMuzhenCard;
        c->addSubcards(cards);
        return c;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("mobilerenmuzhen_give-PlayClear") <= 0 || player->getMark("mobilerenmuzhen_put-PlayClear") <= 0;
    }
};

class MobileRenYaohu : public TriggerSkill
{
public:
    MobileRenYaohu() : TriggerSkill("mobilerenyaohu")
    {
        events << TargetSpecifying << EventPhaseStart;
    }

    static QString getYaohuKingdom(const Player *player)
    {
        foreach (QString mark, player->getMarkNames()) {
            if (mark.startsWith("&mobilerenyaohu+:+") && player->getMark(mark)>0){
				QString kingdom = mark.split("+").last();
				if(Sanguosha->getKingdoms().contains(kingdom))
					return kingdom;
			}
        }
        return "";
    }

    static int getYaohuTargetsNum(const Player *player)
    {
        if (!player->hasSkill("mobilerenyaohu", true)) return -1;

        QString kingdom = getYaohuKingdom(player);
        if (kingdom.isEmpty()) return -1;

        int num = 0;
        foreach (const Player *p, player->getAliveSiblings(true)) {
            if (p->getKingdom() == kingdom)
				num++;
        }
        return num;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if(event==TargetSpecifying){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isDamageCard()){
				foreach (ServerPlayer *p, use.to) {
					if (p->isDead()||player->getMark(p->objectName()+"mobilerenyaohu-PlayClear")<1) continue;
					LogMessage log;
					log.type = "#ZhenguEffect";
					log.from = player;
					log.arg = "mobilerenyaohu";
					room->sendLog(log);
					if (player->getCardCount()>1){
						const Card *ex = room->askForExchange(player, "mobilerenyaohu", 2, 2, true, "@mobilerenyaohu-give:" + p->objectName(), true);
						if (ex){
							room->giveCard(player, p, ex, "mobilerenyaohu");
							continue;
						}
					}
					use.to.removeOne(p);
					data = QVariant::fromValue(use);
				}
			}
		}else{
			if(player->getPhase() == Player::RoundStart){
				if(player->getMark("mobilerenyaohu_lun")<1&&player->hasSkill(objectName())){
					QStringList kingdoms;
					foreach (ServerPlayer *p, room->getAlivePlayers()) {
						QString kingdom = p->getKingdom();
						if (kingdoms.contains(kingdom)) continue;
						kingdoms << kingdom;
					}
					if (kingdoms.isEmpty()) return false;
			
					room->sendCompulsoryTriggerLog(player, this);
					room->addPlayerMark(player, "mobilerenyaohu_lun");
			
					QString kingdom = room->askForKingdom(player, "mobilerenyaohu", kingdoms);
			
					foreach (QString mark, player->getMarkNames()) {
						if (mark.startsWith("&mobilerenyaohu+:+"))
							room->setPlayerMark(player, mark, 0);
					}
					room->setPlayerMark(player, "&mobilerenyaohu+:+" + kingdom, 1);
				}
			}else if(player->getPhase() == Player::Play){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if (player->isDead()) break;
					if (p->isDead() || !p->hasSkill(objectName())) continue;
		
					QList<int> sheng = p->getPile("mrlzsheng");
					if (sheng.isEmpty()) continue;
		
					QString kingdom = player->getKingdom(), yaohu_kingdom = getYaohuKingdom(p);
					if (kingdom != yaohu_kingdom) continue;
					room->sendCompulsoryTriggerLog(p, this);
		
					room->fillAG(sheng, player);
					int id = room->askForAG(player, sheng, false, objectName());
					room->clearAG(player);
					room->obtainCard(player, id);
					if (player->isDead()) break;
		
					QStringList choices;
					QList<ServerPlayer *> targets;
					foreach (ServerPlayer *q, room->getOtherPlayers(p)) {
						if (!player->canSlash(q) || !player->inMyAttackRange(q)) continue;
						if(targets.isEmpty())
							choices << "slash=" + p->objectName();
						targets << q;
					}
					choices << "damagecard=" + p->objectName();
					QString choice = room->askForChoice(player, objectName(), choices.join("+"));
		
					if (choice.contains("slash=")) {
						ServerPlayer *t = room->askForPlayerChosen(p, targets, objectName(), "@mobilerenyaohu-slash:" + player->objectName());
						if(t){
							room->doAnimate(1, p->objectName(), t->objectName());
							if (room->askForUseSlashTo(player, t, "@mobilerenyaohu-use:" + t->objectName())) continue;
						}
					}
					room->addPlayerMark(player, p->objectName()+"mobilerenyaohu-PlayClear");
				}
			}
		}
        return false;
    }
};

class MobileRenJutu : public PhaseChangeSkill
{
public:
    MobileRenJutu() : PhaseChangeSkill("mobilerenjutu")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::Start) return false;

        QList<int> sheng = player->getPile("mrlzsheng");
        bool send_log = true;
        if (!sheng.isEmpty()) {
            room->sendCompulsoryTriggerLog(player, this);
            send_log = false;

            LogMessage log;
            log.type = "$KuangbiGet";
            log.from = player;
            log.arg = "mrlzsheng";
            log.card_str = ListI2S(sheng).join("+");
            room->sendLog(log);
            DummyCard get(sheng);
            room->obtainCard(player, &get);
        }

        int num = MobileRenYaohu::getYaohuTargetsNum(player);
        if (num < 0) return false;

        if (send_log)
            room->sendCompulsoryTriggerLog(player, this);

        player->drawCards(num + 1, objectName());
        if (player->isAlive() && !player->isNude() && num > 0) {
            const Card *ex = room->askForExchange(player, objectName(), num, num, true, "@mobilerenjutu-put:" + QString::number(num));
            player->addToPile("mrlzsheng", ex);
        }
        return false;
    }
};

class MobileRenHuaibi : public TriggerSkill
{
public:
    MobileRenHuaibi() : TriggerSkill("mobilerenhuaibi$")
    {
        events << EventPhaseChanging;
		waked_skills = "#mobilerenhuaibi";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive() && target->hasLordSkill(objectName());
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::Discard)
				room->broadcastSkillInvoke(objectName());
        }
        return false;
    }
};

class MobileRenHuaibiMCS : public MaxCardsSkill
{
public:
    MobileRenHuaibiMCS() : MaxCardsSkill("#mobilerenhuaibi")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->hasLordSkill("mobilerenhuaibi"))
			return qMax(MobileRenYaohu::getYaohuTargetsNum(target), 0);
		return 0;
    }
};

MobileRenPackage::MobileRenPackage()
    : Package("mobileren")
{
    General *mobileren_huaxin = new General(this, "mobileren_huaxin", "wei", 3);
    mobileren_huaxin->addSkill(new MobileRenRenshi);
    mobileren_huaxin->addSkill(new MobileRenBuqi);
    mobileren_huaxin->addSkill(new MobileRenDebao);

    General *mobileren_caizhenji = new General(this, "mobileren_caizhenji", "wei", 3, false);
    mobileren_caizhenji->addSkill(new MobileRenSheyi);
    mobileren_caizhenji->addSkill(new MobileRenTianyin);
    mobileren_caizhenji->addSkill("#tenyearjingce-record");
    related_skills.insertMulti("mobilerentianyin", "#tenyearjingce-record");

    General *mobileren_xujing = new General(this, "mobileren_xujing", "shu", 3);
    mobileren_xujing->addSkill(new MobileRenBoming);
    mobileren_xujing->addSkill(new MobileRenEjian);

    General *mobileren_xiangchong = new General(this, "mobileren_xiangchong", "shu", 4);
    mobileren_xiangchong->addSkill(new MobileRenGuying);
    mobileren_xiangchong->addSkill(new MobileRenMuzhen);

    General *mobileren_liuzhang = new General(this, "mobileren_liuzhang$", "qun", 3);
    mobileren_liuzhang->addSkill(new MobileRenJutu);
    mobileren_liuzhang->addSkill(new MobileRenYaohu);
    mobileren_liuzhang->addSkill(new MobileRenHuaibi);
    mobileren_liuzhang->addSkill(new MobileRenHuaibiMCS);

    addMetaObject<MobileRenRenshiCard>();
    addMetaObject<MobileRenBuqiCard>();
    addMetaObject<MobileRenBomingCard>();
    addMetaObject<MobileRenMuzhenCard>();
}

ADD_PACKAGE(MobileRen)


class MobileYongXiangzhen : public TriggerSkill
{
public:
    MobileYongXiangzhen() : TriggerSkill("mobileyongxiangzhen")
    {
        events << CardFinished;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *) const
    {
        return true;
    }

    QList<ServerPlayer *> getDamageFroms(Room *room, const Card *card) const
    {
        QList<ServerPlayer *> froms;
        foreach (QString flag, card->getFlags()) {
            if (!flag.startsWith("MobileYongXiangzhen_SavageAssault_DamageFrom_")) continue;
            QString name = flag.split("_").last();
            ServerPlayer *from = room->findChild<ServerPlayer *>(name);
            if (!from || from->isDead() || froms.contains(from)) continue;
            froms << from;
        }
        return froms;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("SavageAssault")) return false;
        QList<ServerPlayer *> froms = getDamageFroms(room, use.card);
        if (froms.isEmpty()) return false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isDead() || !p->hasSkill(objectName())) continue;
            room->sendCompulsoryTriggerLog(p, this);
            QList<ServerPlayer *> drawers = froms;
            drawers << p;
            room->sortByActionOrder(drawers);
            room->drawCards(drawers, 1, objectName());
        }
        return false;
    }
};

class MobileYongXiangzhenNullify : public TriggerSkill
{
public:
    MobileYongXiangzhenNullify() : TriggerSkill("#mobileyongxiangzhen")
    {
        events << CardEffected;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (!player->hasSkill("mobileyongxiangzhen")) return false;
        CardEffectStruct effect = data.value<CardEffectStruct>();
        if (effect.card->isKindOf("SavageAssault")) {
            room->broadcastSkillInvoke("mobileyongxiangzhen");
            LogMessage log;
            log.type = "#SkillNullify";
            log.from = player;
            log.arg = "mobileyongxiangzhen";
            log.arg2 = "savage_assault";
            room->sendLog(log);
            room->notifySkillInvoked(player, "mobileyongxiangzhen");
            return true;
        }
        return false;
    }
};

class MobileYongFangzong : public ProhibitSkill
{
public:
    MobileYongFangzong() : ProhibitSkill("mobileyongfangzong")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        if (from->getMark("mobileyongxizhan-Clear") > 0 || to->getMark("mobileyongxizhan-Clear") > 0) return false;
        if (!card->isDamageCard() || card->isKindOf("DelayedTrick")) return false;
        return (from->hasSkill(objectName()) && from->inMyAttackRange(to) && from->getPhase() == Player::Play)
			|| (to->hasSkill(objectName()) && from->inMyAttackRange(to));
    }
};

class MobileYongFangzongDraw : public PhaseChangeSkill
{
public:
    MobileYongFangzongDraw() : PhaseChangeSkill("#mobileyongfangzong")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        if (player->getPhase() != Player::Finish || !player->hasSkill("mobileyongfangzong") || player->getMark("mobileyongxizhan-Clear") > 0) return false;
        int alive = room->alivePlayerCount(), hand = player->getHandcardNum();
        if (alive <= hand) return false;
        room->sendCompulsoryTriggerLog(player, "mobileyongfangzong", true, true);
        player->drawCards(alive - hand, "mobileyongfangzong");
        return false;
    }
};

class MobileYongXizhan : public PhaseChangeSkill
{
public:
    MobileYongXizhan() : PhaseChangeSkill("mobileyongxizhan")
    {
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->getPhase() == Player::RoundStart;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->isDead() || !p->hasSkill(objectName())) continue;
            room->sendCompulsoryTriggerLog(p, this);

            if (!p->canDiscard(p, "he")) {
                room->loseHp(HpLostStruct(p, 1, "mobileyongxizhan", p));
                continue;
            }

            const Card *card = room->askForDiscard(p, objectName(), 1, 1, true, true, "@mobileyongxizhan-discard");
            if (!card) {
                room->loseHp(HpLostStruct(p, 1, "mobileyongxizhan", p));
                continue;
            }

            room->addPlayerMark(p, "mobileyongxizhan-Clear");

            if (player->isDead()) continue;

            if (card->getSuit() == Card::Spade) {
                Analeptic *ana = new Analeptic(Card::NoSuit, 0);
                ana->setSkillName("_mobileyongxizhan");
                room->setCardFlag(ana,"YUANBEN");
                if (player->canUse(ana, player, true))
                    room->useCard(CardUseStruct(ana, player, player), true);
                ana->deleteLater();
            } else if (card->getSuit() == Card::Club) {
                IronChain *ic = new IronChain(Card::NoSuit, 0);
                ic->setSkillName("_mobileyongxizhan");
                room->setCardFlag(ic,"YUANBEN");
                if (p->canUse(ic, player, true))
                    room->useCard(CardUseStruct(ic, p, player), true);
                ic->deleteLater();
            } else if (card->getSuit() == Card::Heart) {
                ExNihilo *ex = new ExNihilo(Card::NoSuit, 0);
                ex->setSkillName("_mobileyongxizhan");
                room->setCardFlag(ex,"YUANBEN");
                if (p->canUse(ex, p, true))
                    room->useCard(CardUseStruct(ex, p, p), true);
                ex->deleteLater();
            } else if (card->getSuit() == Card::Diamond) {
                FireSlash *fire_slash = new FireSlash(Card::NoSuit, 0);
                fire_slash->setSkillName("_mobileyongxizhan");
                room->setCardFlag(fire_slash,"YUANBEN");
                if (p->canSlash(player, fire_slash, false))
                    room->useCard(CardUseStruct(fire_slash, p, player), true);
                fire_slash->deleteLater();
            }
        }
        return false;
    }
};

class MobileYongZaoli : public TriggerSkill
{
public:
    MobileYongZaoli() : TriggerSkill("mobileyongzaoli")
    {
        events << CardUsed << CardResponded << EventPhaseStart << CardsMoveOneTime << EventPhaseChanging;
        frequency = Compulsory;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive||change.from == Player::NotActive){
				room->setPlayerProperty(player, "zaoli_list", "");
			}
        } else if(event == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to == player && move.to_place == Player::PlaceHand) {
				QStringList zaoli_list = player->property("zaoli_list").toString().split("+");
                foreach (int id, move.card_ids) {
                    QString str = QString::number(id);
                    if (zaoli_list.contains(str)) continue;
                    zaoli_list << str;
                }
                room->setPlayerProperty(player, "zaoli_list", zaoli_list.join("+"));
            }
        }else if (event == EventPhaseStart) {
            if (player->getPhase() != Player::RoundStart || player->getMark("&myzlli") <= 0) return false;
            if (!player->hasSkill(objectName())) return false;
            room->sendCompulsoryTriggerLog(player, this);
            int mark = player->getMark("&myzlli");
            player->loseAllMarks("&myzlli");
            if (player->isAlive() && player->canDiscard(player, "he"))
                mark += room->askForDiscard(player, objectName(), 999, 1, false, true, "@mobileyongzaoli-discard")->subcardsLength();
            player->drawCards(mark, objectName());
            room->loseHp(HpLostStruct(player, 1, objectName(), player));
        } else {
            if (player->getMark("&myzlli") >= 4) return false;
            if (!player->hasSkill(objectName())) return false;
            if (event == CardUsed) {
                CardUseStruct use = data.value<CardUseStruct>();
                if (use.card->isKindOf("SkillCard") || !use.m_isHandcard) return false;
            } else if (event == CardResponded) {
                CardResponseStruct res = data.value<CardResponseStruct>();
                if (res.m_card->isKindOf("SkillCard") || !res.m_isHandcard) return false;
            }
            room->sendCompulsoryTriggerLog(player, objectName());
            player->gainMark("&myzlli");
        }
        return false;
    }
};

class MobileYongZaoliBf : public CardLimitSkill
{
public:
    MobileYongZaoliBf() : CardLimitSkill("#mobileyongzaolibf")
    {
    }

    QString limitList(const Player *) const
    {
        return "use,response";
    }

    QString limitPattern(const Player *target) const
    {
        if (target->getPhase() == Player::Play && target->hasSkill("mobileyongzaoli")) {
            QStringList patterns, zaoli_list = target->property("zaoli_list").toString().split("+");
            foreach (const Card *card, target->getHandcards()) {
                if (zaoli_list.contains(card->toString())) continue;
				patterns << card->toString();
            }
            return patterns.join(",");
        }
		return "";
    }
};

MobileYongJungongCard::MobileYongJungongCard()
{
    target_fixed = true;
}

void MobileYongJungongCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->addPlayerMark(source, "&mobileyongjungong-Clear");
    int mark = source->getMark("&mobileyongjungong-Clear");
    if (subcardsLength() == 0)
        room->loseHp(HpLostStruct(source, mark, "mobileyongjungong", source));
    if (source->isDead()) return;

    Slash *slash = new Slash(Card::NoSuit, 0);
    slash->setSkillName("_mobileyongjungong");
    slash->deleteLater();
    if (source->isLocked(slash)) return;

    QList<ServerPlayer *> targets;
    foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
        if (!source->canSlash(p, slash, false)) continue;
        targets << p;
    }
    if (targets.isEmpty()) return;

    if (targets.length() == 1) {
        ServerPlayer *t = targets.first();
        room->useCard(CardUseStruct(slash, source, t));
        return;
    }

    if (room->askForUseCard(source, "@@mobileyongjungong!", "@mobileyongjungong", -1, Card::MethodUse, false)) return;
    ServerPlayer *t = targets.at(qrand() % targets.length());
    room->useCard(CardUseStruct(slash, source, t));
}

class MobileYongJungongVS : public ViewAsSkill
{
public:
    MobileYongJungongVS() : ViewAsSkill("mobileyongjungong")
    {
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        QString pattern = Sanguosha->getCurrentCardUsePattern();
        if (pattern == "@@mobileyongjungong!")
            return false;
        return !Self->isJilei(to_select) && selected.length() < Self->getMark("&mobileyongjungong-Clear") + 1;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        QString pattern = Sanguosha->getCurrentCardUsePattern();
        if (pattern == "@@mobileyongjungong!") {
            Slash *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName("_mobileyongjungong");
            return slash;
        }

        if (cards.isEmpty())
            return new MobileYongJungongCard;

        if (cards.length() != Self->getMark("&mobileyongjungong-Clear") + 1) return nullptr;
        MobileYongJungongCard *c = new MobileYongJungongCard;
        c->addSubcards(cards);
        return c;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("mobileyongjungong-Clear") <= 0;
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern == "@@mobileyongjungong!";
    }
};

class MobileYongJungong : public TriggerSkill
{
public:
    MobileYongJungong() : TriggerSkill("mobileyongjungong")
    {
        events << PreChangeSlash << DamageDone;
        view_as_skill = new MobileYongJungongVS;
    }

    int getPriority(TriggerEvent triggerEvent) const
    {
        if (triggerEvent == PreChangeSlash)
            return 5;
        return TriggerSkill::getPriority(triggerEvent);
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target!=nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (event == PreChangeSlash) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash") || !use.card->getSkillNames().contains("mobileyongjungong")) return false;
            room->setCardFlag(use.card, "mobileyongjungong_slash_" + use.from->objectName());
        } else {
            if (!room->hasCurrent()) return false;
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("Slash")) return false;
            ServerPlayer *from = nullptr;
            foreach (QString flag, damage.card->getFlags()) {
                if (!flag.startsWith("mobileyongjungong_slash_")) continue;
                QStringList flags = flag.split("_");
                if (flags.length() != 3) continue;
                QString name = flags.last();
                from = room->findPlayerByObjectName(name, true);
                break;
            }
            if (!from || from->isDead()) return false;
            room->addPlayerMark(from, "mobileyongjungong-Clear");
        }
        return false;
    }
};

class MobileYongJungongtMod : public TargetModSkill
{
public:
    MobileYongJungongtMod() : TargetModSkill("#mobileyongjungong-target")
    {
    }

    int getResidueNum(const Player *from, const Card *, const Player *) const
    {
        if(from->getMark("huitianUse_lun")==2)
            return 999;
        return 0;
    }

    int getDistanceLimit(const Player *, const Card *card, const Player *) const
    {
        if (card->getSkillName() == "mobileyongjungong")
            return 1000;
        return 0;
    }
};

class MobileYongDengli : public TriggerSkill
{
public:
    MobileYongDengli() : TriggerSkill("mobileyongdengli")
    {
        events << TargetSpecifying << TargetConfirming;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("Slash")) return false;
        if (triggerEvent == TargetSpecifying) {
            foreach (ServerPlayer *p ,use.to) {
                if (p->isDead() || p->getHp() != player->getHp()) continue;
                if (!player->askForSkillInvoke(this, data)) break;
                room->broadcastSkillInvoke(objectName());
                player->drawCards(1, objectName());
            }
        } else {
            if (!use.to.contains(player)) return false;
            if (!use.from || use.from->isDead() || use.from->getHp() != player->getHp()) return false;
            if (!player->askForSkillInvoke(this, data)) return false;
            room->broadcastSkillInvoke(objectName());
            player->drawCards(1, objectName());
        }
        return false;
    }
};

MobileYongPackage::MobileYongPackage()
    : Package("mobileyong")
{
    General *mobileyong_huaman = new General(this, "mobileyong_huaman", "shu", 4, false);
    mobileyong_huaman->addSkill(new MobileYongXiangzhen);
    mobileyong_huaman->addSkill(new MobileYongXiangzhenNullify);
    mobileyong_huaman->addSkill(new MobileYongFangzong);
    mobileyong_huaman->addSkill(new MobileYongFangzongDraw);
    mobileyong_huaman->addSkill(new MobileYongXizhan);
    related_skills.insertMulti("mobileyongxiangzhen", "#mobileyongxiangzhen");
    related_skills.insertMulti("mobileyongfangzong", "#mobileyongfangzong");

    General *mobileyong_sunyi = new General(this, "mobileyong_sunyi", "wu", 4);
    mobileyong_sunyi->addSkill(new MobileYongZaoli);
    mobileyong_sunyi->addSkill(new MobileYongZaoliBf);
    related_skills.insertMulti("mobileyongzaoli", "#mobileyongzaolibf");

    General *mobileyong_gaolan = new General(this, "mobileyong_gaolan", "qun", 4);
    mobileyong_gaolan->addSkill(new MobileYongJungong);
    mobileyong_gaolan->addSkill(new MobileYongJungongtMod);
    mobileyong_gaolan->addSkill(new MobileYongDengli);
    related_skills.insertMulti("mobileyongjungong", "#mobileyongjungong-target");

    addMetaObject<MobileYongJungongCard>();
}

ADD_PACKAGE(MobileYong)


MobileYanYajunCard::MobileYanYajunCard()
{
    will_throw = false;
    handling_method = Card::MethodPindian;
}

bool MobileYanYajunCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void MobileYanYajunCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    ServerPlayer *from = effect.from, *to = effect.to;

    PindianStruct *pindian = from->PinDian(to, "mobileyanyajun", this);
    if (pindian->success) {
        QList<int> pindian_ids;
        if (room->CardInPlace(pindian->from_card, Player::DiscardPile))
            pindian_ids << pindian->from_card->getEffectiveId();
        if (room->CardInPlace(pindian->to_card, Player::DiscardPile) && !pindian_ids.contains(pindian->to_card->getEffectiveId()))
            pindian_ids << pindian->to_card->getEffectiveId();
        if (pindian_ids.isEmpty()) return;

        room->notifyMoveToPile(from, pindian_ids, "mobileyanyajun", Player::DiscardPile, true);

        try {
            room->askForUseCard(from, "@@mobileyanyajun2", "@mobileyanyajun2", 2, Card::MethodNone);
        }
        catch (TriggerEvent triggerEvent) {
            if (triggerEvent == TurnBroken || triggerEvent == StageChange)
                room->notifyMoveToPile(from, pindian_ids, "mobileyanyajun", Player::DiscardPile, false);
            throw triggerEvent;
        }

        room->notifyMoveToPile(from, pindian_ids, "mobileyanyajun", Player::DiscardPile, false);

    } else
        room->addMaxCards(from, -1);
}

MobileYanYajunPutCard::MobileYanYajunPutCard()
{
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
    m_skillName = "mobileyanyajun";
}

void MobileYanYajunPutCard::onUse(Room *room, CardUseStruct &card_use) const
{
    LogMessage log;
    log.type = "$YinshicaiPut";
    log.from = card_use.from;
    log.card_str = ListI2S(subcards).join("+");
    room->sendLog(log);
    CardMoveReason reason(CardMoveReason::S_REASON_PUT, card_use.from->objectName(), "mobileyanyajun", "");
    room->moveCardTo(this, nullptr, Player::DrawPile, reason, true);
}

class MobileYanYajunVS : public OneCardViewAsSkill
{
public:
    MobileYanYajunVS() : OneCardViewAsSkill("mobileyanyajun")
    {
        expand_pile = "#mobileyanyajun";
    }

    bool viewFilter(const Card *to_select) const
    {
        QString pattern = Sanguosha->getCurrentCardUsePattern();
        if (pattern.endsWith("2"))
            return Self->getPile("#mobileyanyajun").contains(to_select->getEffectiveId());
        else if (pattern.endsWith("1")) {
            QStringList strs = Self->property("MobileYanYajunIds").toString().split("+");
            QList<int> ids = ListS2I(strs);
            return ids.contains(to_select->getEffectiveId());
        }
        return false;
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
       return pattern.startsWith("@@mobileyanyajun") ;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        QString pattern = Sanguosha->getCurrentCardUsePattern();
        if (pattern.endsWith("2")) {
            MobileYanYajunPutCard *c = new MobileYanYajunPutCard();
            c->addSubcard(originalCard);
            return c;
        } else if (pattern.endsWith("1")) {
            MobileYanYajunCard *c = new MobileYanYajunCard();
            c->addSubcard(originalCard);
            return c;
        }
        return nullptr;
    }
};

class MobileYanYajun : public TriggerSkill
{
public:
    MobileYanYajun() : TriggerSkill("mobileyanyajun")
    {
        events << DrawNCards << EventPhaseStart;
        view_as_skill = new MobileYanYajunVS;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == DrawNCards) {
            DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="draw_phase") return false;
			room->sendCompulsoryTriggerLog(player, this);
			draw.num++;
            data = QVariant::fromValue(draw);
        } else {
            if (player->getPhase() != Player::Play || !player->canPindian()) return false;
            QString fulin = player->property("fulin_list").toString();

            QStringList this_turn_cards;
            foreach (QString str, fulin.split("+")) {
                int id = str.toInt();
                if (!player->hasCard(id)) continue;
                this_turn_cards << str;
            }
            if (this_turn_cards.isEmpty()) return false;

            room->setPlayerProperty(player, "MobileYanYajunIds", this_turn_cards.join("+"));
            room->askForUseCard(player, "@@mobileyanyajun1", "@mobileyanyajun1", 1, Card::MethodPindian);
        }
        return false;
    }
};

MobileYanZundiCard::MobileYanZundiCard()
{
}

bool MobileYanZundiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}

void MobileYanZundiCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *from = effect.from, *to = effect.to;
    if (from->isDead()) return;
    Room *room = from->getRoom();

    JudgeStruct judge;
    judge.who = from;
    judge.pattern = ".";
    judge.reason = "mobileyanzundi";
    judge.play_animation = false;
    room->judge(judge);

    if (to->isDead()) return;
    QString color = judge.pattern;
    if (color == "red")
        room->moveField(to, "mobileyanzundi", true, "ej");
    else if (color == "black")
        to->drawCards(3, "mobileyanzundi");
}

class MobileYanZundiVS : public OneCardViewAsSkill
{
public:
    MobileYanZundiVS() : OneCardViewAsSkill("mobileyanzundi")
    {
        filter_pattern = ".|.|.|hand!";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileYanZundiCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        MobileYanZundiCard *c = new MobileYanZundiCard();
        c->addSubcard(originalCard);
        return c;
    }
};

class MobileYanZundi : public TriggerSkill
{
public:
    MobileYanZundi() : TriggerSkill("mobileyanzundi")
    {
        events << FinishJudge;
        view_as_skill = new MobileYanZundiVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const
    {
        JudgeStruct *judge = data.value<JudgeStruct *>();
        if (judge->reason != objectName()) return false;
        judge->pattern = judge->card->getColorString();
        return false;
    }
};

class MobileYanDifei : public MasochismSkill
{
public:
    MobileYanDifei() : MasochismSkill("mobileyandifei")
    {
        frequency = Compulsory;
    }

    void onDamaged(ServerPlayer *player, const DamageStruct &damage) const
    {
        if (player->getMark("mobileyandifei_used-Clear") > 0) return;
        Room *room = player->getRoom();
        if (!room->hasCurrent()) return;
        room->sendCompulsoryTriggerLog(player, this);
        room->addPlayerMark(player, "mobileyandifei_used-Clear");
        if (!player->canDiscard(player, "he") || !room->askForDiscard(player, objectName(), 1, 1, true, true, "@mobileyandifei-discard"))
            player->drawCards(1, objectName());
        if (player->isDead() || player->isKongcheng()) return;
        room->showAllCards(player);
        if (!damage.card || !damage.card->hasSuit() || damage.card->isKindOf("SkillCard")) return;
        Card::Suit suit = damage.card->getSuit();
        foreach (const Card *card, player->getHandcards()) {
            if (card->getSuit() == suit) return;
        }
        room->recover(player, RecoverStruct("mobileyandifei", player));
    }
};

MobileYanYanjiaoCard::MobileYanYanjiaoCard()
{
}

void MobileYanYanjiaoCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *from = effect.from, *to = effect.to;
    Room *room = from->getRoom();
    QStringList suits;
    foreach (const Card *card, from->getHandcards()) {
        QString suit = card->getSuitString();
        if (suits.contains(suit)) continue;
        suits << suit;
    }
    if (suits.isEmpty()) return;

    QString suit = room->askForChoice(from, "mobileyanyanjiao", suits.join("+"), QVariant::fromValue(to));
    DummyCard *dummy = new DummyCard();
    dummy->deleteLater();
    foreach (const Card *card, from->getHandcards()) {
        if (card->getSuitString() == suit)
        dummy->addSubcard(card);
    }
    if (dummy->subcardsLength() <= 0) return;

    room->addPlayerMark(from, "&mobileyanyanjiao_draw", dummy->subcardsLength());
    room->giveCard(from, to, dummy, "mobileyanyanjiao");
    room->damage(DamageStruct("mobileyanyanjiao", from, to));
}

class MobileYanYanjiaoVS : public ZeroCardViewAsSkill
{
public:
    MobileYanYanjiaoVS() : ZeroCardViewAsSkill("mobileyanyanjiao")
    {
    }

    const Card *viewAs() const
    {
        return new MobileYanYanjiaoCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileYanYanjiaoCard") && !player->isKongcheng();
    }
};

class MobileYanYanjiao : public PhaseChangeSkill
{
public:
    MobileYanYanjiao() : PhaseChangeSkill("mobileyanyanjiao")
    {
        view_as_skill = new MobileYanYanjiaoVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive() && target->getMark("&mobileyanyanjiao_draw") > 0 && target->getPhase() == Player::RoundStart;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        int mark = player->getMark("&mobileyanyanjiao_draw");
        LogMessage log;
        log.type = "#ZhenguEffect";
        log.from = player;
        log.arg = objectName();
        room->sendLog(log);
        room->broadcastSkillInvoke(this);
        room->notifySkillInvoked(player, objectName());
        room->setPlayerMark(player, "&mobileyanyanjiao_draw", 0);
        player->drawCards(mark, objectName());
        return false;
    }
};

class MobileYanZhenting : public TriggerSkill
{
public:
    MobileYanZhenting() : TriggerSkill("mobileyanzhenting")
    {
        events << TargetConfirming;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (!room->hasCurrent()) return false;
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !(use.card->isKindOf("Slash") || use.card->isKindOf("DelayedTrick"))) return false;

        foreach (ServerPlayer *jw, room->getOtherPlayers(player)) {
            if (!use.to.contains(player)) return false;
            if (jw->isDead() || !jw->hasSkill(objectName()) || use.to.contains(jw)) continue;
            if (!use.from || jw == use.from || !jw->inMyAttackRange(player) || jw->getMark("mobileyanzhenting_used-Clear") > 0) continue;
            if (use.card->isKindOf("DelayedTrick") && jw->containsTrick(use.card->objectName())) continue;
            if (!jw->askForSkillInvoke(this, "mobileyanzhenting_replace:" + player->objectName() + "::" + use.card->objectName())) continue;
            room->addPlayerMark(jw, "mobileyanzhenting_used-Clear");
            room->broadcastSkillInvoke(this);
            use.to.removeOne(player);
            use.to << jw;
            room->sortByActionOrder(use.to);
            data = QVariant::fromValue(use);

            QStringList choices;
            if (use.from->isAlive() && jw->canDiscard(use.from, "h"))
                choices << "discard=" + use.from->objectName();
            choices << "draw" << "cancel";

            QString choice = room->askForChoice(jw, objectName(), choices.join("+"), QVariant::fromValue(use.from));
            if (choice == "cancel") {
                room->getThread()->trigger(TargetConfirming, room, jw, data);
                continue;
            }
            if (choice == "draw")
                jw->drawCards(1, objectName());
            else {
                if (!jw->canDiscard(use.from, "h")) continue;
                int id = room->askForCardChosen(jw, use.from, "h", objectName(), false, Card::MethodDiscard);
                room->throwCard(id, use.from, jw);
            }
            room->getThread()->trigger(TargetConfirming, room, jw, data);
        }
        return false;
    }
};

MobileYanJincuiCard::MobileYanJincuiCard()
{
}

void MobileYanJincuiCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *from = effect.from, *to = effect.to;
    Room *room = from->getRoom();

    room->removePlayerMark(from, "@mobileyanjincuiMark");
    room->doSuperLightbox(from, "mobileyanjincui");

    room->swapSeat(from, to);

    if (from->isDead()) return;
    int hp = from->getHp();
    if (hp > 0)
        room->loseHp(HpLostStruct(from, hp, "mobileyanjincui", from));
}

class MobileYanJincui : public ZeroCardViewAsSkill
{
public:
    MobileYanJincui() : ZeroCardViewAsSkill("mobileyanjincui")
    {
        frequency = Limited;
        limit_mark = "@mobileyanjincuiMark";
    }

    const Card *viewAs() const
    {
        return new MobileYanJincuiCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("@mobileyanjincuiMark") > 0;
    }
};

class MobileYanJianyi : public PhaseChangeSkill
{
public:
    MobileYanJianyi() : PhaseChangeSkill("mobileyanjianyi")
    {
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->getPhase() == Player::NotActive;
    }

    bool onPhaseChange(ServerPlayer *player, Room *room) const
    {
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->isDead() || !p->hasSkill(objectName())) continue;
            QList<int> ids;
            foreach (QVariant id, room->getTag("MobileYanJianyiRecord").toList()) {
                if (!ids.contains(id.toInt())&&room->getCardPlace(id.toInt()) == Player::DiscardPile)
                    ids << id.toInt();
            }
            if (ids.isEmpty()) return false;
            room->sendCompulsoryTriggerLog(p, this);
            room->fillAG(ids, p);
            int id = room->askForAG(p, ids, false, objectName());
            room->clearAG(p);
            room->obtainCard(p, id);
        }
		room->removeTag("MobileYanJianyiRecord");
        return false;
    }
};

MobileYanShangyiCard::MobileYanShangyiCard()
{
}

bool MobileYanShangyiCard::targetFilter(const QList<const Player *> &targets, const Player *to_selet, const Player *Self) const
{
    return targets.isEmpty() && to_selet != Self && !to_selet->isKongcheng();
}

void MobileYanShangyiCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *from = effect.from, *to = effect.to;
    Room *room = from->getRoom();

    if (!from->isKongcheng())
        room->doGongxin(to, from, QList<int>(), "mobileyanjincui");
    if (to->isAlive() && !to->isKongcheng()) {
        int id = room->doGongxin(from, to, to->handCards(), "mobileyanjincui");
        if (id < 0) id = to->getRandomHandCardId();
        room->obtainCard(from, id, false);
    }
}

class MobileYanShangyi : public OneCardViewAsSkill
{
public:
    MobileYanShangyi() : OneCardViewAsSkill("mobileyanshangyi")
    {
        filter_pattern = ".!";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileYanShangyiCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        MobileYanShangyiCard *c = new MobileYanShangyiCard();
        c->addSubcard(originalCard);
        return c;
    }
};

class Guxuan : public TriggerSkill
{
public:
    Guxuan() : TriggerSkill("guxuan")
    {
        events << TargetConfirmed;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
		if(use.card->isKindOf("TrickCard")&&use.from&&use.from!=player&&use.to.contains(player)
		&&player->canDiscard("hej")&&player->askForSkillInvoke(objectName()+"$-1",data)){
			QStringList choices;
			if(player->canDiscard("h"))
				choices << "guxuan0=hand_area";
			if(player->canDiscard("e"))
				choices << "guxuan0=equip_area";
			if(player->canDiscard("j"))
				choices << "guxuan0=judge_area";
			QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
			int n = 0;
			if(choice.contains("hand_area")){
				n = player->getHandcardNum();
				player->throwAllHandCards(objectName());
			}else if(choice.contains("equip_area")){
				n = player->getEquips().length();
				player->throwAllEquips(objectName());
			}else{
				n = player->getJudgingArea().length();
				room->throwCard(player->getJudgingAreaID(),objectName(),player);
			}
			if(player->isDead()) return false;
			QList<ServerPlayer *>tps;
			choice = choice.at(8);
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(player->canDiscard(choice,p)) tps << p;
			}
			tps = room->askForPlayersChosen(player,tps,objectName(),0,n,"guxuan1:"+QString::number(n));
			foreach (ServerPlayer *p, tps)
				room->doAnimate(1,player->objectName(),p->objectName());
			foreach (ServerPlayer *p, tps){
				n = room->askForCardChosen(player,p,choice,objectName(),false,Card::MethodDiscard);
				if(n>=0) room->throwCard(n,objectName(),p,player);
				if(player->isDead()) return false;
			}
		}
        return false;
    }
};

JuejinCard::JuejinCard()
{
    target_fixed = true;
}

void JuejinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->removePlayerMark(source, "@juejin");
    room->doSuperLightbox(source, "juejin");
	int n = source->getMark("juejinDying");
	if(n<1) return;
	n = qMin(n,4);
	QStringList choices;
	if(source->isWounded())
		choices << "juejin1="+QString::number(n);
	choices << "juejin2="+QString::number(n) << "juejin3="+QString::number(n) << "juejin4="+QString::number(n);
	for (int i = 0; i < n; i++) {
		QString choice = room->askForChoice(source,"juejin",choices.join("+"));
		choices.removeOne(choice);
		if(choice.contains("juejin1"))
			room->recover(source,RecoverStruct("juejin",source,n));
		else if(choice.contains("juejin2")){
			source->drawCards(n,"juejin");
		}else if(choice.contains("juejin3")){
			room->addMaxCards(source,n,false);
		}else{
			ServerPlayer *tp = room->askForPlayerChosen(source,room->getOtherPlayers(source),"juejin","juejin0");
			if(tp) room->damage(DamageStruct("juejin",source,tp,n));
		}
		if(choices.isEmpty()) break;
		if(source->isDead()) return;
	}
	if(source->hasSkill("huitian")&&source->askForSkillInvoke("huitian$-1")){
		room->setPlayerMark(source,"huitianUse_lun",1);
		source->addMark("huitianBf");
		room->addPlayerMark(source,"&huitian+num");
	}
}

class Juejinvs : public ZeroCardViewAsSkill
{
public:
    Juejinvs() : ZeroCardViewAsSkill("juejin")
    {
		response_pattern = "@@juejin";
    }

    const Card *viewAs() const
    {
        return new JuejinCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("@juejin")>0;
    }
};

class Juejin : public TriggerSkill
{
public:
    Juejin() : TriggerSkill("juejin")
    {
        events << Dying;
        view_as_skill = new Juejinvs;
		frequency = Limited;
        limit_mark = "@juejin";
		setProperty("IgnoreInvalidity",true);
	}

    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DyingStruct dying = data.value<DyingStruct>();
		if(dying.who==player){
			player->addMark("juejinDying");
			if(player->getMark("@juejin")>0&&player->askForSkillInvoke(objectName(),data,false)){
				Card*dc = new JuejinCard;
				room->useCard(CardUseStruct(dc,player));
				dc->deleteLater();
				//room->askForUseCard(player,"@@juejin","juejin0");
			}
		}
        return false;
    }
};

class Huitian : public TriggerSkill
{
public:
    Huitian() : TriggerSkill("huitian")
    {
        events << DrawNCards << EventPhaseStart << Damage << ConfirmDamage << RoundStart;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == DrawNCards) {
            DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="draw_phase"||player->getMark("huitianUse_lun")!=2) return false;
			draw.num += 2;
            data.setValue(draw);
        } else if(event == Damage){
            DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash"))
				player->addMark("huitianSlash-Clear");
        } else if(event == ConfirmDamage){
            DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")&&player->getMark("huitianUse_lun")==2)
				player->damageRevises(data,1);
        } else if(event == RoundStart){
            if(player->getMark("&huitian+num")>2&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				room->killPlayer(player);
			}
        } else if(event == EventPhaseStart){
            if(player->getPhase()==Player::NotActive){
				foreach(ServerPlayer *p, room->getAllPlayers()){
					if(p->getMark("huitianUse_lun")==1){
						room->addPlayerMark(p,"huitianUse_lun");
						p->gainAnExtraTurn();
						room->setPlayerMark(p,"huitianUse_lun",3);
					}
				}
			}else if(player->getPhase()==Player::Finish&&player->getMark("huitianSlash-Clear")>0&&player->getHp()<3
			&&player->getMark("huitianUse_lun")<1&&player->hasSkill(objectName())&&player->askForSkillInvoke(objectName()+"$-1")){
				room->addPlayerMark(player,"huitianUse_lun");
				room->addPlayerMark(player,"&huitian+num");
			}
        }
        return false;
    }
};

XingzhenCard::XingzhenCard()
{
}

bool XingzhenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card) {
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
    return false;
}

bool XingzhenCard::targetFixed() const
{
	if (user_string.isEmpty()||Sanguosha->getCurrentCardUseReason()!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card) {
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool XingzhenCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card) {
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
    return false;
}

const Card *XingzhenCard::validate(CardUseStruct &use) const
{
    Room *room = use.from->getRoom();
	if(user_string.isEmpty()){
		QList<int>ids,bans,cans;
		foreach(int id, room->getDrawPile()){
			ids << id;
			Card*c = Sanguosha->getCard(id);
			if(c->isKindOf("TrickCard")&&c->isAvailable(use.from))
				cans << id;
			else
				bans << id;
			if(ids.length()>=7) break;
		}
		room->fillAG(ids,use.from,bans);
		int id = room->askForAG(use.from,cans,true,"xingzhen");
		room->clearAG(use.from);
		if(id>=0){
			use.from->skillInvoked("xingzhen");
			room->setPlayerMark(use.from,"xingzhenId",id);
			room->askForUseCard(use.from,"@@xingzhen!","xingzhen0:"+Sanguosha->getCard(id)->objectName());
		}
	}else{
		QList<int>ids,bans,cans;
		foreach(int id, room->getDrawPile()){
			ids << id;
			Card*c = Sanguosha->getCard(id);
			foreach(QString cn, user_string.split("+")){
				if(c->objectName().contains(cn)){
					if(use.from->isLocked(c)) break;
					cans << id;
					break;
				}
			}
			if(!cans.contains(id))
				bans << id;
			if(ids.length()>=7) break;
		}
		room->fillAG(ids,use.from,bans);
		int id = room->askForAG(use.from,cans,true,"xingzhen");
		room->clearAG(use.from);
		if(id>=0){
			use.from->skillInvoked("xingzhen");
			return Sanguosha->getCard(id);
		}
	}
    return nullptr;
}

const Card *XingzhenCard::validateInResponse(ServerPlayer *player) const
{
    Room *room = player->getRoom();
	QList<int>ids,bans,cans;
	foreach(int id, room->getDrawPile()){
		ids << id;
		Card*c = Sanguosha->getCard(id);
		foreach(QString cn, user_string.split("+")){
			if(c->objectName().contains(cn)){
				if(player->isLocked(c)) break;
				cans << id;
				break;
			}
		}
		if(!cans.contains(id))
			bans << id;
		if(ids.length()>=7) break;
	}
	room->fillAG(ids,player,bans);
	int id = room->askForAG(player,cans,true,"xingzhen");
	room->clearAG(player);
	if(id>=0){
		player->skillInvoked("xingzhen");
		return Sanguosha->getCard(id);
	}
    return nullptr;
}

class Xingzhen : public ZeroCardViewAsSkill
{
public:
	Xingzhen() : ZeroCardViewAsSkill("xingzhen")
	{
	}

	const Card *viewAs() const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
        if(pattern=="@@xingzhen!") return Sanguosha->getCard(Self->getMark("xingzhenId"));
		SkillCard*sc = new XingzhenCard;
		sc->setUserString(pattern);
		return sc;
	}

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        if(pattern=="@@xingzhen!") return true;
		if (pattern.startsWith(".") || pattern.startsWith("@")) return false;
        foreach(QString cn, pattern.split("+")){
			Card*dc = Sanguosha->cloneCard(cn);
			if(dc){
				dc->deleteLater();
				if(player->hasFlag("CurrentPlayer"))
					return dc->isKindOf("TrickCard");
				return dc->isKindOf("BasicCard");
			}
        }
        return false;
    }

	bool isEnabledAtPlay(const Player *) const
	{
		return true;
	}
};




MobileYanPackage::MobileYanPackage()
    : Package("mobileyan")
{
    General *mobileyan_cuiyan = new General(this, "mobileyan_cuiyan", "wei", 3);
    mobileyan_cuiyan->addSkill(new MobileYanYajun);
    mobileyan_cuiyan->addSkill(new MobileYanZundi);
    mobileyan_cuiyan->addSkill("#fulinbf");
    related_skills.insertMulti("mobileyanyajun", "#fulinbf");

    General *mobileyan_zhangchangpu = new General(this, "mobileyan_zhangchangpu", "wei", 3, false);
    mobileyan_zhangchangpu->addSkill(new MobileYanDifei);
    mobileyan_zhangchangpu->addSkill(new MobileYanYanjiao);

    General *mobileyan_jiangwan = new General(this, "mobileyan_jiangwan", "shu", 3);
    mobileyan_jiangwan->addSkill(new MobileYanZhenting);
    mobileyan_jiangwan->addSkill(new MobileYanJincui);

    General *mobileyan_jiangqin = new General(this, "mobileyan_jiangqin", "wu", 4);
    mobileyan_jiangqin->addSkill(new MobileYanJianyi);
    mobileyan_jiangqin->addSkill(new MobileYanShangyi);

    General *yan_shenjiangwei = new General(this, "yan_shenjiangwei", "god", 4,true,false,false,1);
    yan_shenjiangwei->addSkill(new Guxuan);
    yan_shenjiangwei->addSkill(new Juejin);
    yan_shenjiangwei->addSkill(new Huitian);
    yan_shenjiangwei->addSkill(new Xingzhen);
    addMetaObject<JuejinCard>();
    addMetaObject<XingzhenCard>();



    addMetaObject<MobileYanYajunCard>();
    addMetaObject<MobileYanYajunPutCard>();
    addMetaObject<MobileYanZundiCard>();
    addMetaObject<MobileYanYanjiaoCard>();
    addMetaObject<MobileYanJincuiCard>();
    addMetaObject<MobileYanShangyiCard>();
}
ADD_PACKAGE(MobileYan)
