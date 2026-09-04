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

namespace {

QString akarinStatusKey(const ServerPlayer *player)
{
    return "inovation_akarin_status_" + player->objectName();
}

void applyAkarinEffect(Room *room, ServerPlayer *player, ServerPlayer *viewer = nullptr)
{
    if (!player)
        return;

    QStringList viewers = room->getTag(akarinStatusKey(player)).toStringList();
    if (viewer && viewers.contains(viewer->objectName()))
        return;

    room->setEmotion(player, "akarin");

    LogMessage log;
    log.type = viewer ? "$AkarinPlayer" : "$AkarinPlayerToAll";
    log.from = player;
    if (viewer) {
        log.to << viewer;
        room->sendLog(log, viewer);
        viewers << viewer->objectName();
    } else {
        room->sendLog(log);
        for (ServerPlayer *other : room->getOtherPlayers(player)) {
            if (!viewers.contains(other->objectName()))
                viewers << other->objectName();
        }
    }
    room->setTag(akarinStatusKey(player), viewers);
}

void removeAkarinEffect(Room *room, ServerPlayer *player, ServerPlayer *viewer = nullptr)
{
    if (!player)
        return;

    const QString tagName = akarinStatusKey(player);
    QStringList viewers = room->getTag(tagName).toStringList();
    if (viewer && !viewers.contains(viewer->objectName()))
        return;

    LogMessage log;
    log.type = viewer ? "$RemoveAkarin" : "$RemoveAkarinToAll";
    log.from = player;
    if (viewer) {
        log.to << viewer;
        room->sendLog(log, viewer);
        viewers.removeAll(viewer->objectName());
    } else {
        room->sendLog(log);
        viewers.clear();
    }

    if (viewers.isEmpty())
        room->removeTag(tagName);
    else
        room->setTag(tagName, viewers);
}

}

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

//akarin
class SE_Touming : public TriggerSkill
{
public:
    SE_Touming() : TriggerSkill("inovation_SE_Touming")
    {
        events << EventPhaseStart << EventPhaseEnd << Death;
        frequency = NotFrequent;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *akarin, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
            if (!akarin->hasSkill(objectName()))
                return false;
            if (akarin->getPhase() == Player::Discard)
                akarin->setMark("inovation_SE_Touming_num", akarin->getHandcardNum());
            else if (akarin->getPhase() == Player::RoundStart && akarin->getMark("touming_used") > 0){
                removeAkarinEffect(room, akarin);
                akarin->setMark("touming_used", 0);
            }
        }
        else if (triggerEvent == EventPhaseEnd)
        {
            if (!akarin->hasSkill(objectName()))
                return false;
            if (akarin->getPhase() == Player::Discard && akarin->getHandcardNum() == akarin->getMark("inovation_SE_Touming_num"))
            {
                if (!akarin->askForSkillInvoke(objectName(), data))
                    return false;
                room->broadcastSkillInvoke(objectName());
                room->doLightbox("inovation_SE_Touming$", 1500);
                applyAkarinEffect(room, akarin);
                akarin->setMark("touming_used", 1);
                akarin->drawCards((room->getAlivePlayers().length() + 1)/2);
            }
        }
        else if (triggerEvent == Death){
            DeathStruct death = data.value<DeathStruct>();
            if (death.who != akarin)
                return false;
            removeAkarinEffect(room, akarin);
        }

        return false;
    }
};

class SE_ToumingClear : public DetachEffectSkill
{
public:
    SE_ToumingClear() : DetachEffectSkill("inovation_SE_Touming")
    {
    }

    void onSkillDetached(Room *room, ServerPlayer *player) const
    {
        removeAkarinEffect(room, player);
    }
};

class SE_Tuanzi : public TriggerSkill
{
public:
    SE_Tuanzi() : TriggerSkill("inovation_SE_Tuanzi")
    {
        events << CardUsed;
        frequency = NotFrequent;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *akarin, QVariant &data) const
    {
        if (triggerEvent == CardUsed) {
            if (akarin->getPhase() == Player::Play)
            {
                CardUseStruct use = data.value<CardUseStruct>();
                if ((use.card->isKindOf("TrickCard") && use.card->isBlack()) || use.card->isKindOf("BasicCard"))
                {
                    if (!akarin->askForSkillInvoke(objectName(), data))
                        return false;
                    room->broadcastSkillInvoke(objectName());
                    QList<int> ids;
                    ids.append(use.card->getEffectiveId());
                    CardsMoveStruct move(ids, NULL, Player::DrawPile,
                        CardMoveReason(CardMoveReason::S_REASON_PUT, akarin->objectName(), objectName(), QString()));
                    room->moveCardsAtomic(move, true);
                }
            }
        }

        return false;
    }
};

class Huanxing : public TriggerSkill
{
public:
    Huanxing() : TriggerSkill("inovation_huanxing")
    {
        events << CardUsed << EventPhaseEnd << TurnStart << TrickCardCanceling << SlashProceed;
        frequency = NotFrequent;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *nao, QVariant &data) const
    {
        if (triggerEvent == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.to.length() == 0 || !use.to.at(0) || use.to.at(0)->getMark("disappear") == 1 || use.from->objectName() == use.to.at(0)->objectName())
                return false;
            if (use.to.length() > 1)
                return false;
            if (!use.to.at(0)->hasSkill(objectName()))
                return false;
            if (!use.to.at(0)->askForSkillInvoke(objectName(), data))
                return false;
            foreach(ServerPlayer* p, room->getAlivePlayers()){
                if (p->getMark("@inovation_huanxing_target") > 0){
                    p->loseMark("@inovation_huanxing_target");
                    removeAkarinEffect(room, use.to.at(0), p);
                }
            }
            use.from->gainMark("@inovation_huanxing_target");
            room->broadcastSkillInvoke(objectName(), qsanRandomBounded(4) + 1);
            room->doLightbox("inovation_huanxing$", 300);
            applyAkarinEffect(room, use.to.at(0), use.from);
            use.to.at(0)->setMark("disappear", 1);
            return true;
        }
        else if (triggerEvent == EventPhaseEnd) {
            if (!nao || !nao->hasSkill(objectName()) || nao->getPhase() != Player::Finish)
                return false;
            foreach(ServerPlayer* p, room->getAlivePlayers()){
                if (p->getMark("@inovation_huanxing_target") > 0){
                    p->loseMark("@inovation_huanxing_target");
                    removeAkarinEffect(room, nao, p);
                }
            }

        }
        else if (triggerEvent == TurnStart) {
            if (!nao || !nao->hasSkill(objectName()))
                return false;
            nao->setMark("disappear", 0);

        }
        else if (triggerEvent == TrickCardCanceling) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
            if (effect.from && effect.from->hasSkill(objectName()) && effect.to && effect.to->getMark("@inovation_huanxing_target") == 1){
                LogMessage log;
                log.type = "#inovation_huanxing_effect";
                log.from = effect.to;
                log.arg = effect.card->objectName();
                room->sendLog(log);
                room->broadcastSkillInvoke(objectName(), 6);
                return true;
            }
        }
        else if (triggerEvent == SlashProceed) {
            SlashEffectStruct effect = data.value<SlashEffectStruct>();
            if (effect.from && effect.from->hasSkill(objectName()) && effect.to && effect.to->getMark("@inovation_huanxing_target") == 1){
                LogMessage log;
                log.type = "#inovation_huanxing_effect";
                log.from = effect.to;
                log.arg = effect.slash->objectName();
                room->sendLog(log);
                room->broadcastSkillInvoke(objectName(), 5);
                room->slashResult(effect, NULL);
                return true;
            }
        }

        return false;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }
};

class Fushang : public TriggerSkill
{
public:
    Fushang() : TriggerSkill("fushang")
    {
        events << Damaged << EventPhaseStart << EnterDying;
        frequency = NotFrequent;
    }

    void doFushang(Room *room, ServerPlayer *player) const
    {
        if (player->getMark("@fushang_time") > 0){
            player->loseMark("@fushang_time");
            if (player->getMark("@fushang_time") == 0){
                room->broadcastSkillInvoke(objectName(), 1);
                room->recover(player, RecoverStruct(player, NULL, player->getMark("@fushang")));
                player->loseAllMarks("@fushang");
            }
        }
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {

        if (triggerEvent == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.to || damage.to->isDead()){
                return false;
            }
            ServerPlayer *nao = room->findPlayerBySkillName(objectName());
            if (!nao || !nao->askForSkillInvoke(objectName(), data))
                return false;
            room->broadcastSkillInvoke(objectName());
            damage.to->gainMark("@fushang");
            damage.to->gainMark("@fushang_time", 2 - damage.to->getMark("@fushang_time"));
            return false;
        }
        else if (triggerEvent == EventPhaseStart) {
            if (player->getPhase() == Player::RoundStart){
                doFushang(room, player);
            }
        }
        else if (triggerEvent == EnterDying){
            if (player->containsTrick("key_trick")){
                doFushang(room, player);
            }
        }
        return false;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
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

class GuangyuViewAsSkill : public OneCardViewAsSkill
{
public:
    GuangyuViewAsSkill() : OneCardViewAsSkill("guangyu")
    {
        response_pattern = "@@guangyu";
    }

    bool viewFilter(const Card *to_select) const
    {
        QStringList guangyu = Self->property("guangyu").toString().split("+");
        foreach(QString id, guangyu) {
            bool ok;
            if (id.toInt(&ok) == to_select->getEffectiveId() && ok)
                return true;
        }
        return false;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        KeyTrick *gy = new KeyTrick(originalCard->getSuit(), originalCard->getNumber());
        gy->addSubcard(originalCard);
        gy->setSkillName("guangyu");
        return gy;
    }
};

class Guangyu : public TriggerSkill
{
public:
    Guangyu() : TriggerSkill("guangyu")
    {
        events << BeforeCardsMove;
        view_as_skill = new GuangyuViewAsSkill;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player)
            return false;
        if (move.to_place == Player::DiscardPile
            && ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)) {

            int i = 0;
            QList<int> guangyu_card;
            foreach(int card_id, move.card_ids) {
                const Card *c = Sanguosha->getCard(card_id);
                if (room->getCardOwner(card_id) == move.from && c->isRed()) {
                    guangyu_card << card_id;
                }
                i++;
            }
            if (guangyu_card.isEmpty())
                return false;

            room->setPlayerProperty(player, "guangyu", ListI2S(guangyu_card).join("+"));
            if (room->getTag("nagisa_voice").isNull())
                room->setTag("nagisa_voice", QVariant(1));
            int num = room->getTag("nagisa_voice").toInt();

            do {
                if (!room->askForUseCard(player, "@@guangyu", "@guangyu-use")) break;
                if (num > 40)
                    room->broadcastSkillInvoke(objectName(), 40);
                else
                    room->broadcastSkillInvoke(objectName(), num);
                num++;
                QList<int> ids = ListS2I(player->property("guangyu").toString().split("+"));
                QList<int> to_remove;
                foreach(int card_id, guangyu_card) {
                    if (!ids.contains(card_id))
                        to_remove << card_id;
                }
                move.removeCardIds(to_remove);
                data = QVariant::fromValue(move);
                guangyu_card = ids;
            } while (!guangyu_card.isEmpty());
            room->setTag("nagisa_voice", QVariant(num));
        }
        return false;
    }
};

class GuangyuTrigger : public TriggerSkill
{
public:
    GuangyuTrigger() : TriggerSkill("#guangyu-trigger")
    {
        events << EventPhaseStart << PreCardUsed;
        frequency = NotFrequent;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
            if (!player || player->getPhase() != Player::Judge || player->getJudgingArea().length() == 0)
                return false;
            foreach(const Card* card, player->getJudgingArea()){
                if (card->isKindOf("KeyTrick")){
                    ServerPlayer *nagisa = room->findPlayerBySkillName("guangyu");
                    if (!nagisa || !nagisa->askForSkillInvoke("guangyu", data))
                        return false;
                    int num = room->getTag("nagisa_voice").toInt();
                    if (num > 40)
                        room->broadcastSkillInvoke("guangyu", qsanRandomBounded(9) + 31);
                    else
                        room->broadcastSkillInvoke("guangyu", num);
                    num++;
                    room->setTag("nagisa_voice", QVariant(num));
                    room->doLightbox("guangyu$", 800);
                    foreach(const Card* card, player->getJudgingArea()){
                        player->obtainCard(card);
                    }
                    return false;
                }
            }
        }
        else if (triggerEvent == PreCardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("KeyTrick") && use.card->getSkillName() == "guangyu") {
                QList<int> ids = ListS2I(player->property("guangyu").toString().split("+"));
                ids.removeOne(use.card->getEffectiveId());
                room->setPlayerProperty(player, "guangyu", ListI2S(ids).join("+"));
            }
            return false;
        }
        return false;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }
};

class Xiyuan : public TriggerSkill
{
public:
    Xiyuan() : TriggerSkill("xiyuan")
    {
        events << Death;
        frequency = NotFrequent;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();

            if (death.who != player)
                return false;
            if (!death.who->hasSkill(objectName()) || room->getOtherPlayers(death.who).length() == 0)
                return false;
            if (room->getTag("xiyuan_used").toBool() || !death.who->askForSkillInvoke(objectName(), data))
                return false;
            ServerPlayer *tomoya = room->askForPlayerChosen(death.who, room->getOtherPlayers(death.who), objectName());
            room->broadcastSkillInvoke(objectName());
            room->doLightbox("xiyuan$", 3000);
            room->changeHero(tomoya, "inovation_Ushio", false, true, true, true);
            LogMessage log;
            log.type = "#XiyuanChangeHero";
            log.from = death.who;
            log.to << tomoya;
            log.arg = objectName();
            room->sendLog(log);
            room->setTag("xiyuan_used", QVariant(true));
        }
        return false;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->hasSkill(this);
    }
};

class Chengmeng : public TriggerSkill
{
public:
    Chengmeng() : TriggerSkill("chengmeng")
    {
        frequency = Club;
        club_name = "yanjubu",
        events << CardsMoveOneTime << TurnStart;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
         if (triggerEvent == CardsMoveOneTime) {
             CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
             ServerPlayer *to;
             if (!move.to)
                 return false;
             foreach(ServerPlayer *p, room->getAlivePlayers()){
                 if (p->objectName() == move.to->objectName())
                     to = p;
             }
             if (room->getCurrent()&&(room->getCurrent()->getPhase() == Player::Draw||room->getCurrent()->getPhase()==Player::NotActive)){
                 return false;
             }
             ServerPlayer *current = room->getCurrent();
             if (!current)
                 return false;
             if (!to)
                 return false;
             if (to == player)
                 return false;
             if (move.to_place!= Player::PlaceHand)
                 return false;
             if (to->hasClub() || player->getMark("chengmeng_used")>0 || !player->askForSkillInvoke(objectName(), data))
                 return false;
             room->setPlayerMark(player,"chengmeng_used",1);
             if (room->askForChoice(to, "chengmeng", "chengmeng_accept+cancel", QVariant::fromValue(player)) == "chengmeng_accept"){
                 to->addClub("yanjubu");
             }
             else{
                 LogMessage log;
                 log.type = "$refuse_club";
                 log.from = to;
                 log.arg = "yanjubu";
                 room->sendLog(log);
             }
         }
         else if (triggerEvent == TurnStart) {
             if (player->getMark("chengmeng_used")>0)
                 room->setPlayerMark(player,"chengmeng_used",0);
         }
         return false;
    }
};

class Dingxin : public TriggerSkill
{
public:
    Dingxin() : TriggerSkill("dingxin")
    {
        events << EventPhaseStart << Dying;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
            if (!player || player->getPhase() != Player::RoundStart || !player->hasSkill(objectName()))
                return false;
            if (room->getTag("nagisa_voice").isNull())
                room->setTag("nagisa_voice", QVariant(1));
            if (player->getHp() > 1){
                int num = room->getTag("nagisa_voice").toInt();
                if (num > 16)
                    room->broadcastSkillInvoke(objectName(), qsanRandomBounded(5) + 12);
                else
                    room->broadcastSkillInvoke(objectName(), num);
                room->setTag("nagisa_voice", QVariant(num + 1));
            }

            player->setFlags("dingxin_used");
            room->loseHp(player);
            player->setFlags("-dingxin_used");
        }
        else if (triggerEvent == Dying) {
            DyingStruct dying = data.value<DyingStruct>();
            if (!dying.who->hasSkill(objectName()) || !dying.who->hasFlag("dingxin_used"))
                return false;
            ServerPlayer *nagisa;
            foreach(ServerPlayer *p, room->getPlayers()){
                if ((p->getGeneralName() == "inovation_Nagisa" || (p->getGeneral2() && p->getGeneral2Name() == "inovation_Nagisa")) && p->isDead()){
                    nagisa = p;
                    break;
                }
            }
            QString choice;
            if (nagisa && nagisa->isDead()){
                choice = room->askForChoice(dying.who, objectName(), "dingxin_recover+dingxin_revive", data);
            }
            else{
                choice = "dingxin_recover";
            }
            if (choice == "dingxin_recover"){
                room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) + 17);
                room->doLightbox("dingxin$", 2000);
                room->recover(dying.who, RecoverStruct(dying.who, NULL, 3));
                LogMessage log;
                log.type = "#DingxinRecover";
                log.from = dying.who;
                room->sendLog(log);
            }
            else{
                room->broadcastSkillInvoke(objectName(), 19);
                room->doLightbox("dingxin$", 2000);
                room->revivePlayer(nagisa, true);
                room->setPlayerProperty(nagisa, "hp", QVariant(2));
                nagisa->drawCards(2);
                LogMessage log;
                log.type = "#DingxinRevive";
                log.from = dying.who;
                log.to << nagisa;
                room->sendLog(log);
            }
            return false;
        }
        return false;
    }
};

//Dark Sakura
class Xushu : public TriggerSkill
{
public:
    Xushu() : TriggerSkill("xushu")
    {
        frequency = Compulsory;
        events << Predamage << EventPhaseStart;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Predamage){
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.from->hasSkill(objectName()) || damage.to->hasSkill(objectName())) {
                if (damage.from->hasSkill(objectName(), 1)){
                    if (damage.reason != "shengjian_black")
                        room->broadcastSkillInvoke(objectName());
                    room->sendCompulsoryTriggerLog(damage.from, objectName());
                }
                else{
                    if (damage.to->getHp() > 4)
                        room->broadcastSkillInvoke(objectName(), 2);
                    room->sendCompulsoryTriggerLog(damage.to, objectName());
                }
                room->loseHp(damage.to, damage.damage);

                return true;
            }
        }
        else if (triggerEvent == EventPhaseStart){
            if (!player->hasSkill(objectName()) || player->getPhase() != Player::RoundStart)
                return false;
            room->loseHp(room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName()));
            room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) + 3);
        }

        return false;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }
};

//xishou
class Xishou : public TriggerSkill
{
public:
    Xishou() : TriggerSkill("xishou")
    {
        frequency = Frequent;
        events << Dying;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Dying){
            DyingStruct dying = data.value<DyingStruct>();
            ServerPlayer *sakura = room->findPlayerBySkillName(objectName());
            if (!sakura || dying.who == sakura || !player->hasSkill(objectName()))
                return false;
            QList<const Skill *> list = dying.who->getVisibleSkillList();
            QStringList choices;
            foreach(const Skill *skill, list){
                if (!sakura->hasSkill(skill))
                    choices.append(skill->objectName());
            }
            if (choices.length() == 0 || !sakura->askForSkillInvoke(objectName(), data))
                return false;

            QString choice = room->askForChoice(sakura, objectName(), choices.join("+"), data);
            room->broadcastSkillInvoke(objectName());
            if (!sakura->hasSkill(choice))
                room->acquireSkill(sakura, choice);
            room->recover(sakura, RecoverStruct(sakura));
        }

        return false;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }
};

//shengbei
//Dark Sakura
class Shengbei : public TriggerSkill
{
public:
    Shengbei() : TriggerSkill("shengbei")
    {
        frequency = Compulsory;
        events << DrawNCards << TurnStart;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == DrawNCards){
            if (player->hasSkill(objectName())){
                data.setValue(data.toInt() + 3);
            }
        }
        else if (triggerEvent == TurnStart){
            if (player->hasSkill(objectName())){
                bool do_voice = true;
                if (!player->faceUp()){
                    room->broadcastSkillInvoke(objectName());
                    player->turnOver();
                    do_voice = false;
                }
                if (player->getJudgingArea().length() > 0){
                    foreach(const Card* card, player->getJudgingArea()){
                        room->throwCard(card, player);
                    }
                    if (do_voice)
                        room->broadcastSkillInvoke(objectName());
                }
            }
        }
        return false;
    }
};

class ShengbeiMaxCards : public MaxCardsSkill
{
public:
    ShengbeiMaxCards() : MaxCardsSkill("#shengbei")
    {
    }

    int getFixed(const Player *target) const
    {
        if (target->hasSkill("shengbei"))
            return target->getHp() + 3;
        else
            return -1;
    }
};

//caoying
class Caoying : public TriggerSkill
{
public:
    Caoying() : TriggerSkill("caoying")
    {
        frequency = Frequent;
        events << TargetConfirmed << HpLost;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TargetConfirmed){
            CardUseStruct use = data.value<CardUseStruct>();
            foreach(ServerPlayer *p, use.to){
                if (p->hasSkill(objectName()) && p == player && !use.from->hasSkill(objectName())){
                    use.from->gainMark("@kage");
                }
            }
        }
        else if (triggerEvent == HpLost){
            if (player->getMark("@kage") == 0)
                return false;
            ServerPlayer *sakura = room->findPlayerBySkillName(objectName());
            if (!sakura)
                return false;
            if (sakura->askForSkillInvoke(objectName(), data)){
                room->broadcastSkillInvoke(objectName());
                for (int i = 0; i < player->getMark("@kage"); i++){
                    if (!player->isNude()){
                        room->throwCard(room->askForCardChosen(sakura, player, "he", objectName()), player, sakura);
                    }
                    else{
                        break;
                    }
                }
                player->loseAllMarks("@kage");
            }
        }

        return false;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }
};

class ShengjianBlack : public TriggerSkill
{
public:
    ShengjianBlack() : TriggerSkill("shengjian_black")
    {
        frequency = Frequent;
        events << HpLost;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == HpLost){
            if (player->hasSkill(objectName()) && player->askForSkillInvoke(objectName(), data)){
                ServerPlayer *p = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName());
                if (!p)
                    return false;
                room->broadcastSkillInvoke(objectName());
                DamageStruct damage;
                damage.from = player;
                damage.to = p;
                damage.reason = "shengjian_black";
                damage.damage = abs(player->getEquips().length() - p->getEquips().length());
                room->damage(damage);
                foreach(const Card* card, p->getEquips()){
                    room->throwCard(card, p, player);
                }
            }
        }
        return false;
    }
};



//chuangzao





//shana rework
//zhena
class Zhena : public TriggerSkill
{
public:
    Zhena() : TriggerSkill("inovation_Zhena")
    {
        frequency = NotFrequent;
        events << DamageCaused;
    }
    int getPriority(TriggerEvent) const
    {
        return -2;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (triggerEvent == DamageCaused){
            if (damage.nature != DamageStruct::Fire || !damage.from->hasSkill(objectName()) || damage.from->getPhase() != Player::Play || damage.from->hasFlag("zhena_used") || !damage.from->askForSkillInvoke(objectName(), data))
                return false;
            room->broadcastSkillInvoke(objectName());
            room->doLightbox("inovation_Zhena$", 2500);

            damage.from->setFlags("zhena_used");

            damage.damage += damage.to->getHp();
            data.setValue(damage);

            if (damage.from->getHp() > 1)
                room->loseHp(damage.from, damage.from->getHp() - 1);


        }
        return false;
    }
};

class Tianhuo : public TriggerSkill
{
public:
    Tianhuo() : TriggerSkill("inovation_Tianhuo")
    {
        frequency = Compulsory;
        events << DamageCaused << DamageInflicted;
    }
    int getPriority(TriggerEvent) const
    {
        return 2;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (triggerEvent == DamageInflicted){
            if (damage.to->hasSkill(objectName()) && damage.nature == DamageStruct::Fire && damage.to->isAlive()){
                room->broadcastSkillInvoke(objectName(), 2);
                damage.to->drawCards(damage.to->getLostHp());
                return true;
            }
        }
        else{
            if (!damage.card->isKindOf("Slash") && !damage.card->isKindOf("Duel") || !damage.from->hasSkill(objectName())){
                return false;
            }
            damage.nature = DamageStruct::Fire;
            data.setValue(damage);
        }

        return false;
    }
};

//nanami
class Shengyou : public TriggerSkill
{
public:
    Shengyou() : TriggerSkill("inovation_shengyou")
    {
        frequency = NotFrequent;
        events << EventPhaseStart;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (triggerEvent == EventPhaseStart){
            if (!player || !player->isAlive())
                return false;
            if (player->getPhase() == Player::RoundStart && player->getHandcardNum() < 3 && player->hasSkill(objectName())){
                QStringList people_list = Sanguosha->getLimitedGeneralNames();
                foreach (ServerPlayer *p, room->getAlivePlayers()){
                    if (people_list.contains(p->getGeneralName()))
                        people_list.removeOne(p->getGeneralName());
                    if (people_list.contains(p->getGeneral2Name()))
                        people_list.removeOne(p->getGeneral2Name());
                }
                foreach(QString name, people_list){
                    if (Sanguosha->getGeneral(name)->isMale())
                        people_list.removeOne(name);
                }
                //special
                if (people_list.contains("Louise"))
                    people_list.removeOne("Louise");
                if (people_list.contains("Misaka_Imouto"))
                    people_list.removeOne("Misaka_Imouto");
                if (people_list.contains("inovation_Natsume_Rin"))
                    people_list.removeOne("inovation_Natsume_Rin");
                if (people_list.contains("Riko"))
                    people_list.removeOne("Riko");
                if (people_list.contains("Koishi"))
                    people_list.removeOne("Koishi");
                if (people_list.contains("mianma"))
                    people_list.removeOne("mianma");
                if (people_list.contains("tsukushi"))
                    people_list.removeOne("tsukushi");
                if (people_list.contains("Niko"))
                    people_list.removeOne("Niko");
                if (people_list.length() > 0){
                    if (!player->askForSkillInvoke(objectName()))
                        return false;
                    QString general = room->askForGeneral(player, people_list.join("+"));
                    if (general == "")
                        return false;
                    room->broadcastSkillInvoke(objectName());
                    room->doLightbox("inovation_shengyou$", 800);
                    if (player->getGeneralName() == "inovation_Nanami"){
                        room->changeHero(player, general, false, false, false, true);
                        room->setTag("inovation_shengyou_isSecond", QVariant(false));
                    }
                    else{
                        room->changeHero(player, general, false, false, true, true);
                        room->setTag("inovation_shengyou_isSecond", QVariant(true));
                    }
                    room->attachSkillToPlayer(player, objectName());
                }
            }
            else if (player->getPhase() == Player::Finish){
                if (room->getTag("inovation_shengyou_isSecond").isNull() || player->getGeneralName() == "inovation_Nanami" || player->getGeneral2Name() == "inovation_Nanami")
                    return false;
                room->detachSkillFromPlayer(player, objectName());
                room->changeHero(player, "inovation_Nanami", false, false, room->getTag("inovation_shengyou_isSecond").toBool(), true);
            }
        }
        return false;
    }
};

class InovationJinqu : public TriggerSkill
{
public:
    InovationJinqu() : TriggerSkill("inovation_jinqu")
    {
        frequency = NotFrequent;
        events << DamageInflicted;
    }
    int getPriority(TriggerEvent) const
    {
        return -3;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to == player && damage.to->hasSkill(objectName())){
            if (!player->askForSkillInvoke(objectName(), data)){
                return false;
            }
            room->broadcastSkillInvoke(objectName());
            player->turnOver();
            player->drawCards(player->getLostHp() * damage.damage * 2);
            QList<int> list;
            foreach(const Card *card, player->getHandcards()){
                list.append(card->getEffectiveId());
            }
            while (room->askForYiji(player, list, objectName(), false, false, true, -1, room->getOtherPlayers(player))) {
                list.clear();
                foreach(const Card *card, player->getHandcards()){
                    list.append(card->getEffectiveId());
                }
                if (!player->isAlive())
                    return false;
            }
            if (player->faceUp() && player->getJudgingArea().length() > 0){
                room->throwCard(room->askForCardChosen(player, player, "j", objectName()), player, player);
            }
        }
        return false;
    }
};

//tomoya
InovationZhurenCard::InovationZhurenCard()
{
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

class InovationZhuren : public ViewAsSkill
{
public:
    InovationZhuren() : ViewAsSkill("inovation_zhuren")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("InovationZhurenCard");
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *) const
    {
        int key_num = 0;
        foreach(const Card *card, Self->getJudgingArea())
            key_num += card->isKindOf("KeyTrick") ? 1 : 0;

        return selected.length() < Self->getLostHp() + key_num;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return NULL;
        InovationZhurenCard *zrc = new InovationZhurenCard();
        zrc->addSubcards(cards);
        return zrc;
    }
};

class InovationZhurenTrigger : public TriggerSkill
{
public:
    InovationZhurenTrigger() : TriggerSkill("#inovation_zhuren")
    {
        frequency = NotFrequent;
        events << EventPhaseEnd;
    }

    bool trigger(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const
    {
        if (triggerEvent == EventPhaseEnd){
            if (player->isAlive() && player->hasSkill("inovation_zhuren") && player->getPhase() == Player::Discard
                && player->getTag("inovation_zhurenCardNum").isValid()) {
                int card_num = player->getTag("inovation_zhurenCardNum").toInt();
                if (card_num > 0)
                    player->drawCards(card_num);
                player->removeTag("inovation_zhurenCardNum");
            }

        }
        return false;
    }
};


class Daolu : public TriggerSkill
{
public:
    Daolu() : TriggerSkill("inovation_Daolu")
    {
        frequency = Wake;
        events << AskForPeachesDone;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == AskForPeachesDone){
            DyingStruct dying = data.value<DyingStruct>();
            if (dying.who != player || !player->hasSkill(objectName()) || player->getMark("@inovation_Nagisa") > 0 || player->getMark("@Tomoyo") > 0 || player->getMark("@Fuko") > 0 || player->getMark("@Kyou") > 0)
                return false;
            QString choice = room->askForChoice(player, objectName(), "inovation_Nagisa_Protector+Kyou_Lover+Tomoyo_Couple+Fuko_summoner");
            room->loseMaxHp(player);
            room->setPlayerProperty(player, "hp", QVariant(2));
            if (choice == "inovation_Nagisa_Protector"){
                room->broadcastSkillInvoke(objectName(), 1);
                room->doLightbox("inovation_DaoluA$", 3000);
                player->gainMark("@inovation_Nagisa");
                if (!player->hasSkill("diangong")){
                    room->acquireSkill(player, "diangong");
                    room->acquireSkill(player, "#diangong");
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
        }
        return false;
    }
};

DiangongCard::DiangongCard()
{
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

class Diangong : public OneCardViewAsSkill
{
public:
    Diangong() : OneCardViewAsSkill("diangong"){

    }

    bool viewFilter(const Card *card) const
    {
        return card->isBlack() && !card->isEquipped();
    }

    const Card *viewAs(const Card *originalCard) const
    {
        DiangongCard *dgc = new DiangongCard();
        dgc->addSubcard(originalCard);
        dgc->setSkillName("diangong");
        return dgc;
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return true;
    }
};

class DiangongTrigger : public TriggerSkill
{
public:
    DiangongTrigger() : TriggerSkill("#diangong")
    {
        frequency = NotFrequent;
        events << DamageInflicted;
    }
    int getPriority(TriggerEvent) const
    {
        return -3;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.card && (damage.card->isKindOf("Lighting") || damage.card->getSkillName() == "diangong")){
            ServerPlayer *tomoya = room->findPlayerBySkillName("diangong");
            if (!tomoya)
                return false;
            if (tomoya->askForSkillInvoke("diangongDamage", data)){
                room->broadcastSkillInvoke("diangong");
                ServerPlayer *toRecover = room->askForPlayerChosen(tomoya, room->getAlivePlayers(), objectName(), "@diangong-from");
                room->recover(toRecover, RecoverStruct(toRecover));
                return true;
            }
        }
        return false;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }
};

class Shouyang : public TriggerSkill
{
public:
    Shouyang() : TriggerSkill("inovation_shouyang")
    {
        frequency = Compulsory;
        events << DamageInflicted << Death << EventAcquireSkill;
    }
    int getPriority(TriggerEvent) const
    {
        return -4;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventAcquireSkill && data.toString() == objectName()){
            room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@inovation_Daolu-Tomo")->gainMark("@Tomo");
        }
        else if (triggerEvent == DamageInflicted){
            DamageStruct damage = data.value<DamageStruct>();

            ServerPlayer *tomoya = room->findPlayerBySkillName(objectName());
            if (tomoya && damage.to == tomoya){
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    if (p->getMark("@Tomo") > 0){
                        p->drawCards(2);
                    }
                }
                return false;
            }


            if (damage.to->getMark("@Tomo") == 0){
                return false;
            }

            if (!tomoya)
                return false;
            damage.to = tomoya;
            room->broadcastSkillInvoke(objectName());
            LogMessage log;
            log.type = "#inovation_shouyangTrigger";
            log.from = tomoya;
            log.to << damage.to;
            room->sendLog(log);
            data.setValue(damage);
        }
        else if (triggerEvent == Death){

            if (player->hasSkill(objectName())){
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    if (p->getMark("@Tomo") > 0){
                        DummyCard *dummy = new DummyCard(player->handCards());
                        QList <const Card *> equips = player->getEquips();
                        foreach(const Card *card, equips)
                            dummy->addSubcard(card);

                        if (dummy->subcardsLength() > 0) {
                            CardMoveReason reason(CardMoveReason::S_REASON_RECYCLE, p->objectName());
                            room->obtainCard(p, dummy, reason, false);
                        }
                        delete dummy;
                        return false;
                    }
                }
            }
        }

        return false;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }
};

class ShouyangClear : public DetachEffectSkill
{
public:
    ShouyangClear() : DetachEffectSkill("inovation_shouyang")
    {
    }

    void onSkillDetached(Room *room, ServerPlayer *player) const
    {
        foreach(ServerPlayer *p, room->getAlivePlayers()){
            p->loseAllMarks("@Tomo");
        }
    }
};

class Haixing : public TriggerSkill
{
public:
    Haixing() : TriggerSkill("inovation_haixing")
    {
        frequency = NotFrequent;
        events << Dying;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Dying){
            DyingStruct dying = data.value<DyingStruct>();
            if (!player || !player->hasSkill(objectName()) || !player->askForSkillInvoke(objectName(), data) || !room->askForDiscard(player, objectName(), 1, 1))
                return false;
            room->broadcastSkillInvoke(objectName());
            JudgeStruct judge;
            judge.pattern = ".";
            judge.reason = objectName();
            judge.who = player;
            judge.time_consuming = true;
            room->judge(judge);
            if (judge.card->getNumber() > 8)
                room->recover(dying.who, RecoverStruct(dying.who));
            if (judge.card->isRed())
                room->recover(dying.who, RecoverStruct(dying.who));
        }

        return false;
    }
};

class Tanyan : public TriggerSkill
{
public:
    Tanyan() : TriggerSkill("tanyan")
    {
        frequency = NotFrequent;
        events << EventPhaseStart << EventPhaseEnd << CardFinished;
    }
    int getPriority(TriggerEvent) const
    {
        return -2;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Play){
            ServerPlayer *tomoya = room->findPlayerBySkillName(objectName());
            if (!tomoya || tomoya->isKongcheng() || !tomoya->askForSkillInvoke(objectName()))
                return false;
            room->broadcastSkillInvoke(objectName());
            room->showAllCards(tomoya, player);
            player->setFlags("tanyan_target");
            room->setFixedDistance(player, tomoya, 1);
        }
        else if (triggerEvent == EventPhaseEnd && player->getPhase() == Player::Play){
            if (player->hasFlag("tanyan_target")){
                player->setFlags("-tanyan_target");
                ServerPlayer *tomoya = room->findPlayerBySkillName(objectName());
                if (!tomoya)
                    return false;
                room->removeFixedDistance(player, tomoya, 1);
                player->setMark("tanyan_slash", 0);
            }
        }
        else if (triggerEvent == CardFinished && player->getPhase() == Player::Play){
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player && use.card->isKindOf("Slash") && player->hasFlag("tanyan_target") && player->getMark("tanyan_slash") < 2){
                ServerPlayer *tomoya = room->findPlayerBySkillName(objectName());
                if (!tomoya)
                    return false;
                tomoya->obtainCard(use.card);
                player->obtainCard(Sanguosha->getCard(room->askForCardChosen(tomoya, tomoya, "he", objectName())));
                player->setMark("tanyan_slash", player->getMark("tanyan_slash") + 1);
            }
        }

        return false;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && !target->isMale();
    }
};

class Pasheng : public DistanceSkill
{
public:
    Pasheng() : DistanceSkill("inovation_SE_Pasheng")
    {
    }

    int getCorrect(const Player *from, const Player *to) const
    {
        if (from->hasSkill(this))
            return 100;
        else if (to->hasSkill(this))
            return -100;
        else
            return 0;
    }
};

class Maoqun : public TriggerSkill
{
public:
    Maoqun() : TriggerSkill("inovation_SE_Maoqun")
    {
        frequency = Compulsory;
        events << Damage;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &) const
    {
        if (triggerEvent == Damage){
            ServerPlayer *rin = room->findPlayerBySkillName("inovation_SE_Maoqun");
            if (!rin)
                return false;
            room->broadcastSkillInvoke(objectName());
            room->loseHp(rin);
            if (room->getDrawPile().length() == 0)
                room->swapPile();
            rin->addToPile("Neko", room->getDrawPile().at(0));
        }
        return false;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }
};

class MaoqunHeg : public TriggerSkill
{
public:
    MaoqunHeg() : TriggerSkill("inovation_SE_MaoqunHeg")
    {
        frequency = Compulsory;
        events << GameStart << EventAcquireSkill;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == GameStart || (triggerEvent == EventAcquireSkill && data.toString() == objectName())){
            room->broadcastSkillInvoke(objectName());
            for (int i = 0; i < room->getAlivePlayers().count(); i++){
                if (room->getDrawPile().length() == 0)
                    room->swapPile();
                player->addToPile("Neko", room->getDrawPile().at(0));
            }

        }
        return false;
    }
};

class Chengzhang : public TriggerSkill
{
public:
    Chengzhang() : TriggerSkill("inovation_SE_Chengzhang")
    {
        frequency = Wake;
        events << EventPhaseStart;
    }
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::RoundStart && player->getMark("@waked") == 0 && player->getPile("Neko").length() >= room->getAlivePlayers().length() * 3 / 2){
            if (player->getMaxHp() >= 99)
                room->loseMaxHp(player, 96);
            else
                room->loseMaxHp(player, player->getMaxHp() - 3);
            room->broadcastSkillInvoke(objectName());
            player->gainMark("@waked");
            room->doLightbox("inovation_SE_Chengzhang$", 3000);
            room->detachSkillFromPlayer(player, "inovation_SE_Pasheng");
            room->detachSkillFromPlayer(player, "inovation_SE_Maoqun");
            room->acquireSkill(player, "zhiling");
            room->acquireSkill(player, "#zhiling");
            room->acquireSkill(player, "#zhiling-max");
            room->acquireSkill(player, "inovation_SE_Zhixing");
        }
        return false;
    }
};

ZhilingCard::ZhilingCard()
{
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

class Zhiling : public ZeroCardViewAsSkill
{
public:
    Zhiling() : ZeroCardViewAsSkill("zhiling")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getPile("Neko").length() > 0;
    }

    const Card *viewAs() const
    {
        return new ZhilingCard;
    }
};

class ZhilingTrigger : public TriggerSkill
{
public:
    ZhilingTrigger() : TriggerSkill("#zhiling")
    {
        frequency = Compulsory;
        events << DrawNCards << DamageInflicted << AskForPeaches;
    }
    bool trigger(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == DrawNCards && player->getMark("@Neko_S") > 0){
            if (qsanRandomBounded(3) == 0)
                data.setValue(data.toInt() - 2);
        }
        else if (triggerEvent == DamageInflicted){
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.nature != DamageStruct::Normal && damage.to->getMark("@Neko_D") > 0){
                damage.damage += 1;
                data.setValue(damage);
            }
        }
        else if (triggerEvent == AskForPeaches){
            DyingStruct dying = data.value<DyingStruct>();
            if (dying.who->getMark("@Neko_H") > 0 && qsanRandomBounded(2) == 1)
                return dying.who->getSeat() != player->getSeat();
        }

        return false;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }
};

class ZhilingMaxCards : public MaxCardsSkill
{
public:
    ZhilingMaxCards() : MaxCardsSkill("#zhiling-max")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->getMark("@Neko_C") > 0)
            return -1;
        else
            return 0;
    }
};

class Zhixing : public TriggerSkill
{
public:
    Zhixing() : TriggerSkill("inovation_SE_Zhixing")
    {
        frequency = NotFrequent;
        events << Dying << DamageInflicted;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Dying){
            DyingStruct dying = data.value<DyingStruct>();
            if (!player || !player->hasSkill(objectName()))
                return false;
            foreach(const Card* card, dying.who->getJudgingArea()){
                if (card->isKindOf("KeyTrick"))
                    return false;
            }
            QVariant newData;
            newData.setValue(dying.who);
            if (!player->askForSkillInvoke(objectName(), newData)){
                return false;
            }
            room->broadcastSkillInvoke(objectName());
            room->doLightbox("inovation_SE_Zhixing$", 800);
            QList<ServerPlayer*> players = room->getAlivePlayers();
            foreach(ServerPlayer* p, players){
                if (p->isNude() && p->getJudgingArea().length() == 0)
                    players.removeOne(p);
            }
            if (players.length() == 0)
                return false;
            ServerPlayer *from = room->askForPlayerChosen(player, players, objectName(), "@zhixing-from");
            if (!from)
                return false;
            int id = room->askForCardChosen(player, from, "hej", objectName());
            if (id == -1)
                return false;
            KeyTrick *key = new KeyTrick(Sanguosha->getCard(id)->getSuit(), Sanguosha->getCard(id)->getNumber());
            key->addSubcard(id);
            key->setSkillName(objectName());
            CardUseStruct use;
            use.from = player;
            use.to.append(dying.who);
            use.card = key;
            room->useCard(use, true);
        }
        else if (triggerEvent == DamageInflicted){
            DamageStruct damage = data.value<DamageStruct>();
            bool hasKey = false;
            int id = -1;
            foreach(const Card* card, damage.to->getJudgingArea()){
                if (card->isKindOf("KeyTrick")){
                    hasKey = true;
                    id = card->getEffectiveId();
                }
            }
            if (!hasKey)
                return false;
            QVariant newData;
            newData.setValue(damage.to);
            ServerPlayer *rin = room->findPlayerBySkillName(objectName());
            if (!rin || !rin->askForSkillInvoke(objectName(), newData))
                return false;
            room->broadcastSkillInvoke(objectName());
            room->doLightbox("inovation_SE_Zhixing$", 800);
            room->throwCard(id, damage.to, rin);
            return true;
        }

        return false;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }
};

//koromo
class Kongdi : public TriggerSkill
{
public:
    Kongdi() : TriggerSkill("kongdi")
    {
        frequency = NotFrequent;
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (triggerEvent == CardsMoveOneTime){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            ServerPlayer *koromo = room->findPlayerBySkillName(objectName());
            if (!koromo || !move.to || koromo->getHandcardNum() >= move.to->getHandcardNum() - move.card_ids.length() || koromo == move.to || move.to_place != Player::PlaceHand || !move.from_places.contains(Player::DrawPile) || !koromo->askForSkillInvoke(objectName(), data))
                return false;
            ServerPlayer *to;
            foreach(ServerPlayer *p, room->getAlivePlayers()){
                if (p->objectName() == move.to->objectName())
                    to = p;
            }
            if (!to)
                return false;
            int id = room->askForCardChosen(koromo, to, "h", objectName(), true);
            if (id == -1)
                return false;
            room->showCard(to, id);
            QString choice = room->askForChoice(koromo, objectName(), "kongdi_di+kongdi_discard");
            if (qsanRandomBounded(5) == 1){
                room->broadcastSkillInvoke(objectName());
            }
            if (choice == "kongdi_di"){
                room->moveCardsToEndOfDrawpile(to, QList<int>() << id, objectName(), false);
            }
            else{
                room->throwCard(id, to, koromo);
            }
        }
        return false;
    }
};

class Yixiangting : public TriggerSkill
{
public:
    Yixiangting() : TriggerSkill("inovation_yixiang")
    {
        frequency = Compulsory;
        events << BeforeCardsMove;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (triggerEvent == BeforeCardsMove){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            ServerPlayer *koromo = room->findPlayerBySkillName(objectName());
            if (!koromo || !move.to || koromo == move.to || move.to_place != Player::PlaceHand || !move.from_places.contains(Player::DrawPile))
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
                move.from_pile_names.append(NULL);
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

class TouzhiVS : public OneCardViewAsSkill
{
public:
    TouzhiVS() : OneCardViewAsSkill("touzhi")
    {
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return true;
    }

    bool viewFilter(const Card *card) const
    {
        if (!card->isKindOf("TrickCard") || card->isKindOf("AOE") || card->isKindOf("GodSalvation") || card->isKindOf("AmazingGrace") || card->isKindOf("Collateral"))
            return false;
        return true;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Card *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());

        slash->addSubcard(originalCard->getId());
        slash->setSkillName("touzhi");
        return slash;
    }
};

class Touzhi : public TriggerSkill
{
public:
    Touzhi() : TriggerSkill("touzhi")
    {
        events << SlashHit << SlashMissed << CardUsed;
        view_as_skill = new TouzhiVS;
    }

    int getEffectIndex(const ServerPlayer*, const Card*){
        return 0;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {if (triggerEvent == SlashHit) {
            SlashEffectStruct effect = data.value<SlashEffectStruct>();
            if (effect.slash->getSkillName() == objectName()){
                int id = effect.slash->getSubcards().at(0);
                const Card *card = Sanguosha->getCard(id);
                CardUseStruct use;
                use.from = effect.from;
                use.to.append(effect.to);
                use.card = card;
                room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) + 4);
                room->useCard(use, false);
            }
        }
        else if (triggerEvent == SlashMissed) {
            SlashEffectStruct effect = data.value<SlashEffectStruct>();
            if (effect.from->hasSkill(objectName()) && effect.slash->getSkillName() == objectName()){
                room->broadcastSkillInvoke(objectName(), qsanRandomBounded(3) + 1);
                Analeptic *a = new Analeptic(Card::NoSuit, 0);
                a->setSkillName(objectName());
                CardUseStruct use;
                use.from = effect.from;
                use.to.append(effect.from);
                use.card = a;
                room->useCard(use, false);
                effect.from->drawCards(1);
                effect.from->gainMark("@kyou_fire");
            }
        }
        else if (triggerEvent == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Slash") && use.card->getSkillName() == objectName()) {
                if (use.m_addHistory) {
                    room->addPlayerHistory(use.from, use.card->getClassName(), -1);
                    use.m_addHistory = false;
                    data = QVariant::fromValue(use);
                }
            }
        }

        return false;
    }
};

class TouzhiDis : public DistanceSkill
{
public:
    TouzhiDis() : DistanceSkill("#touzhi")
    {
    }

    int getCorrect(const Player *from, const Player *) const
    {
        if (from->hasSkill(this))
            return - 1 - from->getMark("@kyou_fire");
        else
            return 0;
    }
};

class YoujiaoViewAsSkill : public OneCardViewAsSkill
{
public:
    YoujiaoViewAsSkill() : OneCardViewAsSkill("youjiao")
    {
        response_pattern = "@@youjiao";
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->isKindOf("BasicCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        KeyTrick *yj = new KeyTrick(originalCard->getSuit(), originalCard->getNumber());
        yj->addSubcard(originalCard);
        yj->setSkillName("youjiao");
        return yj;
    }
};

class Youjiao : public TriggerSkill
{
public:
    Youjiao() : TriggerSkill("youjiao")
    {
        frequency = NotFrequent;
        events << HpLost;
        view_as_skill = new YoujiaoViewAsSkill;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (triggerEvent == HpLost){
            QVariant new_data;
            new_data.setValue(player);
            ServerPlayer *kyou = room->findPlayerBySkillName(objectName());
            if (!kyou || player->getHp() >= kyou->getHp())
                return false;
            if (room->askForUseCard(kyou, "@@youjiao", "@youjiao-use")){
                kyou->drawCards(1);
                player->drawCards(1);
            }
        }

        return false;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }
};

class Takamakuri : public TriggerSkill
{
public:
    Takamakuri() : TriggerSkill("Takamakuri")
    {
        frequency = NotFrequent;
        events << Damage;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *akari, QVariant &data) const
    {
        if (triggerEvent == Damage){
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.from != akari)
                return false;
            if (akari && akari->isAlive() && akari->askForSkillInvoke(objectName(), data)){
                akari->setFlags("TakamakuriUsed");
                int id = room->getDrawPile().at(0);
                QList<int> ids;
                ids.append(id);
                room->fillAG(ids);
                room->getThread()->delay(800);

                room->clearAG();
                if (Sanguosha->getCard(id)->isKindOf("BasicCard")){
                    room->broadcastSkillInvoke(objectName());
                    room->obtainCard(akari, id);
                    if (damage.to->getEquips().length() > 0)
                        room->throwCard(room->askForCardChosen(akari, damage.to, "e", objectName()), damage.to, akari);
                }
            }
        }
        return false;
    }
};

class Tobiugachi : public TriggerSkill
{
public:
    Tobiugachi() : TriggerSkill("Tobiugachi")
    {
        frequency = NotFrequent;
        events << CardAsked;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *akari, QVariant &data) const
    {
        if (triggerEvent == CardAsked){
            QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
            if (pattern == "jink" && akari->hasSkill(objectName()) && akari->getHandcardNum() > akari->getHp() && akari->askForSkillInvoke(objectName(), data)){
                if (room->askForDiscard(akari, objectName(), akari->getHandcardNum() - akari->getHp() + 1, akari->getHandcardNum() - akari->getHp() + 1)){
                    akari->setFlags("TobiugachiUsed");
                    Card* jink = Sanguosha->cloneCard("jink", Card::NoSuit, 0);
                    jink->setSkillName(objectName());
                    room->provide(jink);
                    ServerPlayer *target = room->askForPlayerChosen(akari, room->getAlivePlayers(), objectName());
                    QStringList list = target->getPileNames();
                    bool hasPile = false;
                    foreach (QString pile, list){
                        if (target->getPile(pile).length() > 0){
                            hasPile = true;
                        }
                    }
                    QString choice = "ToBiGetRegion";
                    if (hasPile)
                        choice = room->askForChoice(akari, objectName(), "ToBiGetRegion+TobiGetPile");
                    if (choice == "TobiGetPile"){
                        QString choice2 = room->askForChoice(akari, objectName() + "1", list.join("+"));
                        QList<int> pile = target->getPile(choice2);
                        room->fillAG(pile, akari);
                        int id = room->askForAG(akari, pile, false, objectName());
                        if (id == -1)
                            return false;
                        room->obtainCard(akari, id);
                        room->clearAG(akari);
                    }
                    else{
                        int id =  room->askForCardChosen(akari, target, "hej", objectName());
                        if (id == -1)
                            return false;
                        room->obtainCard(akari, id);
                    }
                }
            }
        }
        return false;
    }
};


class Fukurouza : public TriggerSkill
{
public:
    Fukurouza() : TriggerSkill("Fukurouza")
    {
        frequency = NotFrequent;
        events << EventPhaseEnd;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseEnd && player->getPhase() == Player::Finish){
            ServerPlayer *akari = room->findPlayerBySkillName(objectName());
            bool broad = true;
            if (akari && akari->isAlive() && akari->hasFlag("TobiugachiUsed") && room->askForSkillInvoke(akari, objectName() + "Tobi", data)){
                room->broadcastSkillInvoke(objectName());
                broad = false;
                DamageStruct damage;
                damage.from = akari;
                damage.to = player;
                damage.reason = objectName();
                room->damage(damage);
            }

            if (akari && akari->isAlive() && akari->hasFlag("TakamakuriUsed") && room->askForSkillInvoke(akari, objectName() + "Taka", data)){
                if (broad)
                    room->broadcastSkillInvoke(objectName());
                akari->drawCards(1);
                akari->setFlags("-TakamakuriUsed");
            }

            if (akari && akari->isAlive() && akari->hasFlag("TobiugachiUsed"))
                akari->setFlags("-TobiugachiUsed");
            if (akari && akari->isAlive() && akari->hasFlag("TakamakuriUsed"))
                akari->setFlags("-TakamakuriUsed");
        }
        return false;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }
};





class Kuisi : public TriggerSkill
{
public:
    Kuisi() : TriggerSkill("kuisi")
    {
        frequency = NotFrequent;
        events << Death;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (triggerEvent == Death){
            DeathStruct death = data.value<DeathStruct>();
            if (!death.damage || !death.damage->from || death.damage->from->isDead())
                return false;
            ServerPlayer *saki = room->findPlayerBySkillName(objectName());
            if (saki){
                if (!saki->askForSkillInvoke(objectName(), data))
                    return false;
                room->broadcastSkillInvoke(objectName());
                room->doLightbox("kuisi$", 2000);
                room->loseHp(death.damage->from, death.damage->from->getHp());
            }
        }
        return false;
    }
};

YouerCard::YouerCard()
{
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

class Youervs : public ZeroCardViewAsSkill
{
public:
    Youervs() : ZeroCardViewAsSkill("youer")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("YouerCard");
    }

    const Card *viewAs() const
    {
        return new YouerCard();
    }
};

class Youer : public TriggerSkill
{
public:
    Youer() : TriggerSkill("youer")
    {
        frequency = NotFrequent;
        events << DamageCaused << EventPhaseStart;
        view_as_skill = new Youervs;
        global = true;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == DamageCaused){
            DamageStruct da = data.value<DamageStruct>();
            if (da.damage<da.to->getHp()){
                return false;
            }
            foreach(ServerPlayer *p, room->getAlivePlayers()){
                if (p&&p->getMark("youer_target")>0&&p!=da.to&&room->askForSkillInvoke(p, objectName(), data)){
                    room->broadcastSkillInvoke(objectName(),1);
                    da.to=p;
                    data.setValue(da);
                    break;
                }
            }
        }
        else if (triggerEvent == EventPhaseStart){
            if (player->getPhase()!=Player::Play||player->isKongcheng()){
                return false;
            }
            ServerPlayer *sp = NULL;
            foreach(ServerPlayer *p, room->getAlivePlayers()){
                if (p&&p->getMark("youer_target")>0&&player->inMyAttackRange(p)){
                    sp=p;
                    break;
                }
            }
            if (!sp||!sp->askForSkillInvoke("youertiaoxin",data)){
                return false;
            }
            int id = room->askForCardChosen(sp,player,"h",objectName());
            room->throwCard(id,player,sp);
            Card *c=Sanguosha->getCard(id);
            if (c->isKindOf("Slash")||c->isKindOf("Duel")){
                room->useCard(CardUseStruct(c, player, sp));
                if (c->isKindOf("Slash")) {
                    room->addPlayerHistory(player,"Slash",1);
                }
            }
        }
        return false;
    }
};





//zuikaku









//shizuo

class Baonu : public TriggerSkill
{
public:
    Baonu() : TriggerSkill("baonu")
    {
        events << DrawNCards << EventPhaseEnd;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *shizuo, QVariant &data) const
    {
        if (triggerEvent == DrawNCards) {
            if (room->askForSkillInvoke(shizuo, objectName())){
                room->loseHp(shizuo);
                room->broadcastSkillInvoke(objectName());
                shizuo->gainMark("@Baonu");
                data.setValue(shizuo->getLostHp());
            }
        }
        else if (triggerEvent == EventPhaseEnd){
            if (shizuo->getPhase() == Player::Finish){
                shizuo->loseAllMarks("@Baonu");
            }
        }

        return false;
    }
};

JizhanCard::JizhanCard()
{
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

class Jizhanshiz : public ZeroCardViewAsSkill
{
public:
    Jizhanshiz() : ZeroCardViewAsSkill("jizhanshiz")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("JizhanCard") && player->getMark("@Baonu") > 0;
    }

    const Card *viewAs() const
    {
        return new JizhanCard();
    }
};

//3000
class Tianzi : public TriggerSkill
{
public:
    Tianzi() : TriggerSkill("tianzi")
    {
        events << EventPhaseEnd;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *nagi, QVariant &data) const
    {
        if (triggerEvent == EventPhaseEnd){
            if (nagi->getPhase() == Player::Judge || nagi->getPhase() == Player::Draw || nagi->getPhase() == Player::Play || nagi->getPhase() == Player::Discard){
                if (nagi->isNude()){
                    return false;
                }
                const Card *card = room->askForCard(nagi, "..", "@tianzi-discard", data, objectName());
                if (card){
                    //room->throwCard(card, nagi, nagi);
                    room->broadcastSkillInvoke(objectName());
                    if (card->isKindOf("TrickCard")){
                        nagi->drawCards(2);
                    }
                    else if (card->isKindOf("EquipCard")){
                        nagi->drawCards(2);
                    }
                    else{
                        nagi->drawCards(1);
                    }
                }
            }
        }
        return false;
    }
};

class Yuzhai : public TriggerSkill
{
public:
    Yuzhai() : TriggerSkill("yuzhai")
    {
        events << EventPhaseStart << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *nagi, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart){
            if (nagi->getPhase() == Player::Finish){
                if (nagi->getMark("@Yuzhai") > nagi->getHp() && room->askForSkillInvoke(nagi, objectName(), data)){
                    room->broadcastSkillInvoke(objectName());
                    for (int i = nagi->getHp(); i < nagi->getMark("@Yuzhai"); i++){
                        if (i > nagi->getHp() + 2){
                            break;
                        }
                        ServerPlayer *p = room->askForPlayerChosen(nagi, room->getOtherPlayers(nagi), objectName());
                        if (p->isNude()){
                            continue;
                        }
                        int id = room->askForCardChosen(nagi, p, "he", objectName());
                        if (id != -1){
                            room->throwCard(id, p, nagi);
                        }
                    }


                    nagi->loseAllMarks("@Yuzhai");
                }
            }
        }
        else if (triggerEvent == CardsMoveOneTime){
            if (nagi->getPhase() == Player::NotActive){
                return false;
            }
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from && move.from->hasSkill(objectName()) && nagi->objectName() == move.from->objectName() && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD){
                nagi->gainMark("@Yuzhai", move.card_ids.length());
            }
        }
        return false;
    }
};

class Qinshi: public TriggerSkill
{
public:
    Qinshi() : TriggerSkill("qinshi")
    {
        events << GameStart << Death << EventPhaseEnd;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *mumei, QVariant &data) const
    {
        if (triggerEvent == GameStart){
            room->setPlayerProperty(mumei, "maxhp",mumei->getMaxHp() +  room->getAllPlayers().length());
            room->recover(mumei, RecoverStruct(mumei, NULL, room->getAllPlayers().length()));
        }
        else if (triggerEvent == EventPhaseEnd){
            if (mumei->getPhase() == Player::Finish){
                room->broadcastSkillInvoke(objectName());
                room->loseHp(mumei, 1);
            }
        }
        else if (triggerEvent == Death){
            DeathStruct death = data.value<DeathStruct>();
            if (!death.damage || !death.damage->from || !death.damage->from->hasSkill(objectName()))
                return false;
            room->broadcastSkillInvoke(objectName());
            room->recover(death.damage->from, RecoverStruct(death.damage->from));
        }
        return false;
    }
};

class Kangfen : public TriggerSkill
{
public:
    Kangfen() : TriggerSkill("kangfen")
    {
        events << EventPhaseEnd << Damaged;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseEnd){
            if (player->getPhase() == Player::Finish && !player->hasSkill(objectName())){
                ServerPlayer *mumei = room->findPlayerBySkillName(objectName());

                if (mumei && mumei->isAlive() && !mumei->hasFlag("kangfen_damaged")){
                    if (room->askForSkillInvoke(mumei, objectName(), data)){
                        room->broadcastSkillInvoke(objectName());
                        mumei->gainAnExtraTurn();
                    }
                }
                else{
                    if (mumei && mumei->isAlive()){
                        room->setPlayerFlag(mumei, "-kangfen_damaged");
                    }
                }
            }
        }
        else if (triggerEvent == Damaged){
            DamageStruct da = data.value<DamageStruct>();
            if (da.to && da.to->hasSkill(objectName())){
                room->setPlayerFlag(da.to, "kangfen_damaged");

            }
        }
        return false;
    }
};

class Xiedou : public ViewAsSkill
{
public:
    Xiedou() : ViewAsSkill("xiedou")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasFlag("XiedouUsed") && player->getHandcardNum() > player->getEquips().length() && Self->getEquips().length() > 0;
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        return selected.length() < Self->getHandcardNum() - Self->getEquips().length() && !to_select->isEquipped();
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() < Self->getHandcardNum() - Self->getEquips().length())
            return NULL;
        Duel *duel = new Duel(cards.at(0)->getSuit(), cards.at(0)->getNumber());
        duel->addSubcards(cards);
        Self->setFlags("XiedouUsed");
        duel->setSkillName(objectName());
        return duel;
    }
};



TaxianCard::TaxianCard()
{
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

class TaxianVs : public ZeroCardViewAsSkill
{
public:
    TaxianVs() : ZeroCardViewAsSkill("taxian")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("TaxianCard");
    }

    const Card *viewAs() const
    {
        return new TaxianCard();
    }
};

class Taxian : public TriggerSkill
{
public:
    Taxian() : TriggerSkill("taxian")
    {
        events << SlashProceed;
        view_as_skill = new TaxianVs;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (triggerEvent == SlashProceed){
            SlashEffectStruct ses = data.value<SlashEffectStruct>();
            if (ses.from && ses.from->hasSkill(objectName()) && ses.slash && ses.slash->getSkillName() == objectName()){
                room->slashResult(ses, NULL);
                return true;
            }
        }

        return false;
    }
};

class Guishen : public TriggerSkill
{
public:
    Guishen() : TriggerSkill("guishen")
    {
        events << EventPhaseEnd << Damage;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseEnd){
            if (player->getPhase() == Player::Finish){
                if (player->getMark("@Guishen") >= player->getHp()){
                    room->recover(player, RecoverStruct(player, NULL, player->getMark("@Guishen") - player->getHp()));
                    player->drawCards(player->getHp());
                }
                player->loseAllMarks("@Guishen");
            }
        }
        else if (triggerEvent == Damage){
            DamageStruct da = data.value<DamageStruct>();
            if (da.from && da.from->hasSkill(objectName()) && da.from->getPhase() != Player::NotActive){
                da.from->gainMark("@Guishen", 1);

            }
        }
        return false;
    }
};





class Jianjin : public TriggerSkill
{
public:
    Jianjin() : TriggerSkill("jianjin")
    {
        events << EventPhaseStart << EventPhaseEnd << Damaged << HpRecover;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::RoundStart){
            if (player->hasSkill(objectName())){
                if (player->getMark("@Jianjin") == 0){
                    player->gainMark("@Jianjin");
                }
            }
        }
        else if (triggerEvent == EventPhaseEnd && player->getPhase() == Player::Finish){
            if (player->hasSkill(objectName())){
                if (player->getMark("@Jianjin") == 0){
                    player->gainMark("@Jianjin");
                }
            }
            ServerPlayer * iroha = room->findPlayerBySkillName(objectName());
            if (iroha && iroha->isAlive()){
                iroha->loseAllMarks("@Jianjin_damage_recovery");
            }
        }
        else if (triggerEvent == Damaged){
            DamageStruct damage = data.value<DamageStruct>();
            ServerPlayer * iroha = room->findPlayerBySkillName(objectName());
            if (iroha && iroha->getMark("@Jianjin") == 0){
                return false;
            }
            if (iroha && iroha->isAlive() && iroha->getMark("@Jianjin_damage_recovery") < 3){
                iroha->gainMark("@Jianjin_damage_recovery", damage.damage);
            }
            if ((damage.to || damage.from) && iroha && iroha->isAlive() && iroha->askForSkillInvoke(objectName(), data)){
                iroha->loseAllMarks("@Jianjin");
                QList<ServerPlayer *> sl;
                if (damage.from)
                    sl.append(damage.from);
                if (damage.to)
                    sl.append(damage.to);
                room->broadcastSkillInvoke(objectName());
                room->askForPlayerChosen(iroha, sl, objectName())->drawCards(iroha->getMark("@Jianjin_damage_recovery"));
            }
        }
        else if (triggerEvent == HpRecover){
            RecoverStruct r = data.value<RecoverStruct>();
            ServerPlayer * iroha = room->findPlayerBySkillName(objectName());
            if (!iroha || !iroha->isAlive() || iroha->getMark("@Jianjin") == 0){
                return false;
            }
            if (iroha && iroha->isAlive() && iroha->getMark("@Jianjin_damage_recovery") < 3){
                iroha->gainMark("@Jianjin_damage_recovery", r.recover);
            }
        }
        return false;
    }
};

class Faka : public TriggerSkill
{
public:
    Faka() : TriggerSkill("faka")
    {
        frequency = NotFrequent;
        events << CardsMoveOneTime << HpRecover;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == CardsMoveOneTime && player->hasSkill(objectName())){
            if (room->getCurrent()->getPhase() == Player::NotActive){
                return false;
            }
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            ServerPlayer *iroha = room->findPlayerBySkillName(objectName());
            if (!iroha || move.to != iroha || move.from == move.to || (move.to_place != Player::PlaceEquip && move.to_place != Player::PlaceHand)){
                return false;
            }
            ServerPlayer *current = room->getCurrent();
            if (!current || current->isDead() || current == iroha){
                return false;
            }
            if (!iroha->askForSkillInvoke(objectName(), data)){
                return false;
            }
            QList<ServerPlayer *> qs;
            qs.append(current);
            if (move.from && move.from->isAlive()){
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    if (move.from->objectName() == p->objectName()){
                        qs.append(p);
                    }
                }
            }
            ServerPlayer *goodman = room->askForPlayerChosen(iroha, qs, objectName());
            CardsMoveStruct new_move;
            new_move.card_ids = move.card_ids;
            new_move.from = iroha;
            new_move.to = goodman;
            new_move.to_place = Player::PlaceHand;
            new_move.reason.m_reason = CardMoveReason::S_REASON_GIVE;
            room->moveCardsAtomic(new_move, true);
            room->broadcastSkillInvoke(objectName());
            room->damage(DamageStruct(NULL, iroha, goodman, new_move.card_ids.length()));
        }
        else if (triggerEvent == HpRecover){
            RecoverStruct r = data.value<RecoverStruct>();
            ServerPlayer * iroha = room->findPlayerBySkillName(objectName());
            if (iroha && iroha->isAlive() && r.who == iroha){
                ServerPlayer *current = room->getCurrent();
                if (current == iroha){
                    return false;
                }
                room->broadcastSkillInvoke(objectName());
                current->turnOver();
                current->drawCards(1);

            }
        }
        return false;
    }
};



NingjuCard::NingjuCard()
{
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


class Ningju : public ZeroCardViewAsSkill
{
public:
    Ningju() : ZeroCardViewAsSkill("ningju")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->usedTimes("NingjuCard") < 3;
    }

    const Card *viewAs() const
    {
        return new NingjuCard();
    }
};


class Zhinian : public TriggerSkill
{
public:
    Zhinian() : TriggerSkill("zhinian")
    {
        frequency = Wake;
        events << AskForPeachesDone;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->hasSkill(this);
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == AskForPeachesDone){
            DyingStruct dying = data.value<DyingStruct>();
            if (dying.who->hasSkill(objectName()) && dying.who->getMaxHp() > 0 && dying.who->getHp() < 1 && dying.who->getMark("@waked") == 0 && dying.who == player){
                room->broadcastSkillInvoke(objectName());
                room->doLightbox("zhinian$", 2500);
                room->setPlayerProperty(player, "hp", 3);
                player->gainMark("@waked");
                room->acquireSkill(player, "chengxu");
            }
        }
        return false;
    }
};

class Chengxu : public TriggerSkill
{
public:
    Chengxu() : TriggerSkill("chengxu")
    {
        frequency = Compulsory;
        events << DamageInflicted << EventPhaseEnd;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == DamageInflicted){
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.to->hasSkill(objectName())){
                room->broadcastSkillInvoke(objectName());
                return true;
            }
        }
        else if (triggerEvent == EventPhaseEnd){
            if (player->hasSkill(objectName()) && player->getPhase()==Player::Finish){
                room->broadcastSkillInvoke(objectName());
                room->loseMaxHp(player);
            }
        }
        return false;
    }
};






class Xingjian : public TriggerSkill
{
public:
    Xingjian() : TriggerSkill("xingjian")
    {
        events << EventPhaseStart << Death;
        frequency = Wake;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart){
            if (player->getPhase() == Player::Play && player->hasSkill(objectName())){
                if (room->hasAura() && (room->getAura() == objectName() || room->getAura() == "MacrossF" || room->getAuraPlayer()->getHp() < player->getHp())){
                    return false;
                }
                if (!player->askForSkillInvoke(objectName(), data)){
                    return false;
                }
                room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) * 2 + 1);
                if (room->getAura() == "yaojing"){
                    room->doAura(player, "MacrossF");
                }
                else{
                    room->doAura(player, objectName());
                }
            }
            else if (player->getPhase() == Player::RoundStart && room->hasAura() && (room->getAura() == objectName() || room->getAura() == "MacrossF") && player->getEquips().length() > 0){
                QString choice = room->askForChoice(player, objectName(), "xingjian_skip+xingjian_throw", data);
                ServerPlayer * ranka = room->findPlayerBySkillName(objectName());
                if (!ranka || player == ranka){
                    return false;
                }
                room->broadcastSkillInvoke(objectName(), 2);
                if (choice == "xingjian_throw"){

                    room->obtainCard(ranka, room->askForCardChosen(ranka, player, "e", objectName()));
                }
                else{
                    if (ranka && ranka->isAlive() && !ranka->isNude()){
                        room->obtainCard(player, room->askForCardChosen(player, ranka, "he", objectName()));
                        player->skip(Player::Draw);
                    }
                }
            }
        }
        else if (event == Death){
            DeathStruct death = data.value<DeathStruct>();
            if (death.who->hasSkill(objectName()) && room->hasAura() && (room->getAura() == objectName() || room->getAura() == "MacrossF")){
                if (room->getAura() == "MacrossF"){
                    ServerPlayer *sher = room->findPlayerBySkillName("yaojing");
                    if (sher &&sher->isAlive()){
                        room->doAura(sher, "yaojing");
                        return false;
                    }

                }
                room->clearAura();
            }
        }
        return false;
    }
};

class XingjianClear : public DetachEffectSkill
{
public:
    XingjianClear() : DetachEffectSkill("xingjian")
    {
    }

    void onSkillDetached(Room *room, ServerPlayer *player) const
    {
        if ( room->hasAura() && (room->getAura() == objectName() || room->getAura() == "MacrossF")){
            if (room->getAura() == "MacrossF"){
                ServerPlayer *sher = room->findPlayerBySkillName("yaojing");
                if (sher &&sher->isAlive()){
                    room->doAura(sher, "yaojing");
                    return;
                }

            }
            room->clearAura();
        }
    }
};

class Goutong : public TriggerSkill
{
public:
    Goutong() : TriggerSkill("goutong")
    {
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (event == CardsMoveOneTime){

            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            ServerPlayer *ranka = room->findPlayerBySkillName(objectName());
            if (!ranka || (move.to != ranka && move.from != ranka) || move.from == move.to || (!move.from_places.contains(Player::PlaceHand) && !move.from_places.contains(Player::PlaceEquip)) || (move.to_place != Player::PlaceEquip && move.to_place != Player::PlaceHand)){
                return false;
            }
            ServerPlayer* from;
            ServerPlayer* to;
            foreach(ServerPlayer *p, room->getAlivePlayers()){
                if (p->objectName() == move.from->objectName()){
                    from = p;
                }
                if (p->objectName() == move.to->objectName()){
                    to = p;
                }
            }
            if (!ranka->askForSkillInvoke(objectName(), data)){
                return false;
            }
            room->broadcastSkillInvoke(objectName());
            room->recover(from, RecoverStruct(from));
            room->recover(to, RecoverStruct(to));
            from->drawCards(1);
            to->drawCards(1);

        }
        return false;
    }
};


class Jianshi : public TriggerSkill
{
public:
    Jianshi() : TriggerSkill("jianshi")
    {
        events << CardsMoveOneTime << Death;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->hasSkill(this);
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (event == CardsMoveOneTime){

            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            ServerPlayer *kotori = room->findPlayerBySkillName(objectName());
            if (!move.from_places.contains(Player::DrawPile)){
                return false;
            }
            if (move.to_place == Player::DrawPile){
                return false;
            }

            if (!kotori || kotori->isDead()){
                return false;
            }

            bool has = false;
            Card *key;
            foreach(int id, move.card_ids){
                if (Sanguosha->getCard(id)->isKindOf("KeyTrick")){
                    has = true;
                    key = Sanguosha->getCard(id);
                    break;
                }
            }

            if (!has){
                return false;
            }

            CardsMoveStruct new_move;
            new_move.from = move.to;
            new_move.card_ids = move.card_ids;
            new_move.from_pile_name = move.to_pile_name;
            new_move.from_place = move.to_place;
            new_move.reason.m_reason = CardMoveReason::S_REASON_TRANSFER;
            new_move.to = kotori;
            new_move.to_place = Player::PlaceHand;
            room->broadcastSkillInvoke(objectName());
            room->moveCardsAtomic(new_move, true);
            ServerPlayer *target = room->askForPlayerChosen(kotori, room->getOtherPlayers(kotori), objectName());
            room->useCard(CardUseStruct(key, kotori, target));

            //clear old
            foreach(ServerPlayer *p, room->getOtherPlayers(kotori)){
                if (p->getMark("@Jianshi_akarin")){
                    foreach(ServerPlayer *q, room->getOtherPlayers(p)){
                        if (!q->hasSkill(objectName())){
                            removeAkarinEffect(room, p, q);
                        }
                    }
                    p->loseAllMarks("@Jianshi_akarin");
                }
            }

            //add new
            target->gainMark("@Jianshi_akarin");
            foreach(ServerPlayer *p, room->getOtherPlayers(target)){
                if (!p->hasSkill(objectName())){
                    applyAkarinEffect(room, target, p);
                }
            }

        }
        else if (event == Death){
            DeathStruct death = data.value<DeathStruct>();
            if (death.who->hasSkill(objectName())){
                foreach(ServerPlayer *p, room->getOtherPlayers(death.who)){
                    if (p->getMark("@Jianshi_akarin")){
                        foreach(ServerPlayer *q, room->getOtherPlayers(p)){
                            if (!q->hasSkill(objectName())){
                                removeAkarinEffect(room, p, q);
                            }
                        }
                        p->loseAllMarks("@Jianshi_akarin");
                    }
                }
            }
        }
        return false;
    }

};

class JianshiClear : public DetachEffectSkill
{
public:
    JianshiClear() : DetachEffectSkill("jianshi")
    {
    }

    void onSkillDetached(Room *room, ServerPlayer *player) const
    {
        foreach(ServerPlayer *p, room->getAlivePlayers()){
            if (p->getMark("@Jianshi_akarin")){
                foreach(ServerPlayer *q, room->getOtherPlayers(p)){
                    if (!q->hasSkill(objectName())){
                        removeAkarinEffect(room, p, q);
                    }
                }
                p->loseAllMarks("@Jianshi_akarin");
            }
        }
    }
};

class Qiyue : public TriggerSkill
{
public:
    Qiyue() : TriggerSkill("qiyue")
    {
        events << AskForPeachesDone << BeforeCardsMove;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (triggerEvent == AskForPeachesDone){
            DyingStruct dying = data.value<DyingStruct>();
            if (dying.who && dying.who->getHp() <= 0){
                ServerPlayer *kotori = room->findPlayerBySkillName(objectName());
                if (!kotori || !kotori->isAlive() || !kotori->askForSkillInvoke(objectName(), data)){
                    return false;
                }



                room->broadcastSkillInvoke(objectName());
                room->doLightbox("qiyue$", 2000);
                room->setPlayerFlag(kotori, "qiyue_calculate");

                int num = 5 - kotori->getMaxHp();
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    p->drawCards(num);
                }
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    for (int i = 0; i < num; i++){
                        if (!p->isAllNude()){
                            room->throwCard(room->askForCardChosen(p, p, "hej", objectName()), p, p);
                        }
                    }
                }
                room->setPlayerFlag(kotori, "-qiyue_calculate");
                if (!kotori->hasFlag("qiyue_return_max")){
                    room->loseMaxHp(kotori);
                }
                else{
                    kotori->setFlags("-qiyue_return_max");
                }
            }
        }
        else if (triggerEvent == BeforeCardsMove){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (!move.from_places.contains(Player::DrawPile)){
                return false;
            }
            ServerPlayer *kotori = room->findPlayerBySkillName(objectName());
            if (kotori && kotori->isAlive() && kotori->hasFlag("qiyue_calculate") && !kotori->hasFlag("qiyue_return_max")){
                if (room->getDrawPile().length() - move.card_ids.length() <= 0){
                    if (kotori->isLord() && room->getAllPlayers(true).length() > 4){
                        room->setPlayerProperty(kotori, "maxhp", QVariant::fromValue(4));
                    }
                    else{
                        room->setPlayerProperty(kotori, "maxhp", QVariant::fromValue(3));
                    }
                    kotori->setFlags("qiyue_return_max");
                }
            }
        }
        return false;
    }

};


class Nangua : public TriggerSkill
{
public:
    Nangua() : TriggerSkill("nangua")
    {
        events << EnterDying << HpRecover;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (event == EnterDying){
            DyingStruct dying = data.value<DyingStruct>();
            if (dying.who->hasSkill(objectName()) && room->askForSkillInvoke(dying.who, objectName(), data)){
                room->broadcastSkillInvoke(objectName());
                dying.who->drawCards(dying.who->getMaxHp());
            }
        }
        else if (event == HpRecover){
            RecoverStruct re = data.value<RecoverStruct>();
            if (re.who && re.who->getHp() < 2 && re.who->hasSkill(objectName()) && room->askForSkillInvoke(re.who, objectName(), data)){
                if (room->askForChoice(re.who, objectName(), "nangua_recover+nangua_turnover", data) == "nangua_recover"){
                    if (re.who->getHp() < 1){
                        room->recover(re.who, RecoverStruct(re.who, NULL, 1 - re.who->getHp()));
                        room->setPlayerProperty(re.who, "hp", 1);
                    }
                }
                else{
                    re.who->turnOver();
                }
            }
        }
        return false;
    }
};

class InovationJixian : public TriggerSkill
{
public:
    InovationJixian() : TriggerSkill("inovation_jixian")
    {
        events << EventPhaseEnd << AskForPeachesDone;
    }

    void doInovationJixian(Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->isNude() || !room->askForSkillInvoke(player, objectName(), data) || !room->askForDiscard(player, objectName(), 1, 1, false, true)){
            return;
        }
        ServerPlayer *p = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName());
        if (p){
            int num = player->getLostHp() + 1;
            if (num > 2){
                room->broadcastSkillInvoke(objectName(), 1);
            }
            else{
                room->broadcastSkillInvoke(objectName(), 2);
            }
            room->damage(DamageStruct(objectName(), player, p, num));
            if (num > 2){
                room->detachSkillFromPlayer(player, objectName());
                room->loseHp(player);
            }
        }
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseEnd && player->hasSkill(objectName()) && player->getPhase() == Player::Finish){
            doInovationJixian(room, player, data);
        }
        else if (event == AskForPeachesDone){
            doInovationJixian(room, player, data);
        }
        return false;
    }
};

class Yandan : public TriggerSkill
{
public:
    Yandan() : TriggerSkill("yandan")
    {
        events << CardsMoveOneTime << Death;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (triggerEvent == CardsMoveOneTime){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (!move.from_places.contains(Player::PlaceHand) && !move.from_places.contains(Player::PlaceEquip)){
                return false;
            }
            if (move.from->getPhase() != Player::NotActive){
                return false;
            }
            if (move.reason.m_reason != CardMoveReason::S_REASON_DISCARD && move.reason.m_reason != CardMoveReason::S_REASON_DISMANTLE && move.reason.m_reason != CardMoveReason::S_REASON_THROW && move.reason.m_reason != CardMoveReason::S_REASON_RULEDISCARD){
                return false;
            }

            if (move.card_ids.length() == 0){
                return false;
            }

            ServerPlayer *makoto = room->findPlayerBySkillName(objectName());

            if (!makoto || !makoto->isAlive()){
                return false;
            }

            if (makoto->getPile("Yandan").length() >= makoto->getMaxHp()){
                return false;
            }

            if (!makoto->askForSkillInvoke(objectName(), data)){
                return false;
            }
            room->fillAG(move.card_ids, makoto);
            int id = room->askForAG(makoto, move.card_ids, true, objectName());
            room->clearAG(makoto);
            if (id != -1){
                makoto->addToPile("Yandan", id);
            }

        }
        else if (triggerEvent == Death){
            DeathStruct death = data.value<DeathStruct>();
            ServerPlayer *dead = death.who;
            ServerPlayer *makoto = room->findPlayerBySkillName(objectName());
            if (!makoto){
                return false;
            }
            makoto->addMark("yandan_death");
            if (dead->isNude() || makoto == dead){
                return false;
            }
            if (!makoto->askForSkillInvoke(objectName(), data)){
                return false;
            }
            QList<const Card*> cards = dead->getHandcards();
            cards.append(dead->getEquips());
            QList<int> list;
            foreach(const Card* card, cards){
                list.append(card->getId());
            }
            room->fillAG(list, makoto);
            int id = room->askForAG(makoto, list, true, objectName());
            room->clearAG(makoto);
            if (id != -1){
                room->broadcastSkillInvoke(objectName());
                makoto->addToPile("Yandan", id);
            }
        }

        return false;
    }
};

class YandanMaxCards : public MaxCardsSkill
{
public:
    YandanMaxCards() : MaxCardsSkill("#yandan")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->hasSkill("yandan")){
            int i = target->getPile("Yandan").length() > 0 ? 1 : 0;
            return  i + target->getMark("yandan_death");
        }
        else
            return 0;
    }
};


class YandanClear : public DetachEffectSkill
{
public:
    YandanClear() : DetachEffectSkill("yandan", "Yandan")
    {
    }
};

class Xiwang : public TriggerSkill
{
public:
    Xiwang() : TriggerSkill("xiwang")
    {
        events << EventPhaseStart;
        frequency = Wake;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (event == EventPhaseStart && player->hasSkill(objectName()) && player->getPhase() == Player::RoundStart){
            if (player->getPile("Yandan").length() > player->getHp() && player->getMark("@waked") == 0){
                room->broadcastSkillInvoke(objectName());
                room->doLightbox("lunpo$", 2000);
                room->loseMaxHp(player);
                player->drawCards(1);
                player->gainMark("@waked");
                room->acquireSkill(player, "lunpo");
            }
        }
        return false;
    }
};


class Lunpo : public TriggerSkill
{
public:
    Lunpo() : TriggerSkill("lunpo")
    {
        events << EventPhaseStart << EventPhaseChanging << Death << CardUsed;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart){
            if (player->getPhase() == Player::Play && player->hasSkill(objectName())){
                int minHp = 100;
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    if (p->getHp() < minHp){
                        minHp = p->getHp();
                    }
                }
                if (player->getPile("Yandan").length() < minHp){
                    return false;
                }
                if (!player->askForSkillInvoke("lunpo_inturn", data)){
                    return false;
                }

                QList<int> list = player->getPile("Yandan");

                for (int i = 0; i < minHp; i++){
                    room->fillAG(list, player);
                    int id = room->askForAG(player, list, false, objectName());
                    room->clearAG(player);
                    if (id != -1){
                        list.removeOne(id);
                        room->throwCard(id, player, player);
                    }
                }
                room->broadcastSkillInvoke(objectName(), 1);
                room->doLightbox("lunpo$", 500);
                foreach(ServerPlayer *p, room->getOtherPlayers(player)){
                    p->addMark("lunpo");
                    room->addPlayerMark(p, "@skill_invalidity");

                }
                JsonArray args;
                args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
                room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
            }
        }
        else if (event == CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("EquipCard")){
                return false;
            }

            ServerPlayer *makoto = room->findPlayerBySkillName(objectName());
            if (!makoto || !makoto->isAlive()){
                return false;
            }
            if (makoto->getPile("Yandan").length() == 0){
                return false;
            }
            QList<int> list;
            foreach(int id, makoto->getPile("Yandan")){
                if (Sanguosha->getCard(id)->getSuit() == use.card->getSuit()){
                    list.append(id);
                }
            }

            if (list.length() == 0 || !makoto->askForSkillInvoke(objectName(), data)){
                return false;
            }

            room->fillAG(list, makoto);
            int id = room->askForAG(makoto, list, true, objectName());
            room->clearAG(makoto);
            if (id != -1){
                room->throwCard(id, makoto, makoto);
                room->broadcastSkillInvoke(objectName(), 2);
                room->doLightbox("lunpo$", 300);
                if (use.card->isKindOf("DelayedTrick")){
                    room->throwCard(use.card->getId(), makoto, makoto);
                }
                return true;
            }

        }
        else if (event == EventPhaseChanging){
            QList<ServerPlayer *> players = room->getAllPlayers();
            foreach(ServerPlayer *player, players) {
                if (player->getMark("lunpo") == 0) continue;
                player->removeMark("lunpo");
                room->removePlayerMark(player, "@skill_invalidity");
            }
            JsonArray args;
            args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
            room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
        }
        else if (event == Death){
            DeathStruct death = data.value<DeathStruct>();
            if (death.who->hasSkill(objectName())){
                QList<ServerPlayer *> players = room->getAllPlayers();
                foreach(ServerPlayer *player, players) {
                    if (player->getMark("lunpo") == 0) continue;
                    player->removeMark("lunpo");
                    room->removePlayerMark(player, "@skill_invalidity");
                }
                JsonArray args;
                args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
                room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
            }
        }
        return false;
    }
};

class LunpoInvalidity : public InvaliditySkill
{
public:
    LunpoInvalidity() : InvaliditySkill("#lunpo-inv")
    {
    }

    bool isSkillValid(const Player *player, const Skill *skill) const
    {
        return player->getMark("lunpo") == 0 || skill->isAttachedLordSkill();
    }
};

class Xinyang : public TriggerSkill
{
public:
    Xinyang() : TriggerSkill("xinyang")
    {
        events << ShowCards << StartJudge;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == ShowCards){
            ServerPlayer * sanae = room->findPlayerBySkillName(objectName());
            if (!sanae || sanae->isDead()){
                return false;
            }
            if (!room->askForSkillInvoke(sanae, objectName(), data)){
                return false;
            }
            sanae->addToPile("xinyang", room->getDrawPile().first());
        }
        else if (event == StartJudge){
            if (player && player->isAlive()){
                ServerPlayer * sanae = room->findPlayerBySkillName(objectName());
                if (sanae && sanae->isAlive() && sanae->hasSkill(objectName()) && sanae->getPile("xinyang").length() > 0 && room->askForSkillInvoke(sanae, "xinyang_judge", data)){
                    room->fillAG(sanae->getPile("xinyang"), sanae);
                    int id = room->askForAG(sanae, sanae->getPile("xinyang"), true, objectName());
                    room->clearAG(sanae);
                    if (id != -1){
                        room->moveCardTo(Sanguosha->getCard(id), sanae, NULL, Player::DrawPile, CardMoveReason(CardMoveReason::S_REASON_PUT, sanae->objectName()), true);
                    }
                }
            }
        }
        return false;
    }
};

class XinyangClear : public DetachEffectSkill
{
public:
    XinyangClear() : DetachEffectSkill("xinyang", "xinyang")
    {
    }
};


InovationFengzhuCard::InovationFengzhuCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool InovationFengzhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        const Card *card = NULL;
        if (!user_string.isEmpty())
            card = Sanguosha->cloneCard(user_string.split("+").first());
        return card && card->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, card, targets);
    }

    const Card *_card = Self->getTag("inovation_fengzhu").value<const Card *>();
    if (_card == NULL)
        return false;

    Card *card = Sanguosha->cloneCard(_card->objectName(), Card::NoSuit, 0);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, card, targets);
}

bool InovationFengzhuCard::targetFixed() const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        const Card *card = NULL;
        if (!user_string.isEmpty())
            card = Sanguosha->cloneCard(user_string.split("+").first());
        return card && card->targetFixed();
    }

    const Card *_card = Self->getTag("inovation_fengzhu").value<const Card *>();
    if (_card == NULL)
        return false;

    Card *card = Sanguosha->cloneCard(_card->objectName(), Card::NoSuit, 0);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetFixed();
}

bool InovationFengzhuCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        const Card *card = NULL;
        if (!user_string.isEmpty())
            card = Sanguosha->cloneCard(user_string.split("+").first());
        return card && card->targetsFeasible(targets, Self);
    }

    const Card *_card = Self->getTag("inovation_fengzhu").value<const Card *>();
    if (_card == NULL)
        return false;

    Card *card = Sanguosha->cloneCard(_card->objectName(), Card::NoSuit, 0);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetsFeasible(targets, Self);
}

const Card *InovationFengzhuCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer *sanae = card_use.from;
    Room *room = sanae->getRoom();

    QString to_guhuo = user_string;
    if (user_string == "slash" && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        QStringList guhuo_list;
        guhuo_list << "slash";
        if (!Config.BanPackages.contains("maneuvering"))
            guhuo_list = QStringList() << "normal_slash" << "thunder_slash" << "fire_slash";
        to_guhuo = room->askForChoice(sanae, "inovation_fengzhu_slash", guhuo_list.join("+"));
    }

    //room->moveCardTo(this, NULL, Player::DrawPile, true);

    QString user_str;
    if (to_guhuo == "normal_slash")
        user_str = "slash";
    else
        user_str = to_guhuo;

    Card *c = Sanguosha->cloneCard(user_str, Card::NoSuit, 0);

    QString classname;
    if (c->isKindOf("Slash"))
        classname = "Slash";
    else
        classname = c->getClassName();

    room->setPlayerFlag(sanae, "inovation_fengzhu_used");

    if (sanae && sanae->isAlive() && sanae->hasSkill("inovation_fengzhu")){
        const Card* card = room->askForCardShow(sanae, sanae, "inovation_fengzhu");
        if (card){
            room->showCard(sanae, card->getEffectiveId());
            JudgeStruct judge;
            judge.reason = "inovation_fengzhu";
            judge.who = sanae;
            judge.pattern = ".|" + card->getSuitString();
            room->judge(judge);
            if (judge.isGood()){
                c->setSkillName("inovation_fengzhu");
                c->deleteLater();
                return c;
            }
            else{
                room->obtainCard(sanae, judge.card->getEffectiveId());
            }
        }
    }

    return NULL;
}

const Card *InovationFengzhuCard::validateInResponse(ServerPlayer *sanae) const
{
    Room *room = sanae->getRoom();

    QString to_guhuo = user_string;
    if (user_string == "peach+analeptic") {
        bool can_use_peach = !sanae->hasFlag("inovation_fengzhu_used");
        bool can_use_analeptic = !sanae->hasFlag("inovation_fengzhu_used");
        QStringList guhuo_list;
        if (can_use_peach)
            guhuo_list << "peach";
        if (can_use_analeptic && !Config.BanPackages.contains("maneuvering"))
            guhuo_list << "analeptic";
        to_guhuo = room->askForChoice(sanae, "inovation_fengzhu_saveself", guhuo_list.join("+"));
    }
    else if (user_string == "slash") {
        QStringList guhuo_list;
        guhuo_list << "slash";
        if (!Config.BanPackages.contains("maneuvering"))
            guhuo_list = QStringList() << "normal_slash" << "thunder_slash" << "fire_slash";
        to_guhuo = room->askForChoice(sanae, "inovation_fengzhu_slash", guhuo_list.join("+"));
    }
    else
        to_guhuo = user_string;

    //room->moveCardTo(this, NULL, Player::DrawPile, true);

    QString user_str;
    if (to_guhuo == "normal_slash")
        user_str = "slash";
    else
        user_str = to_guhuo;

    Card *c = Sanguosha->cloneCard(user_str, Card::NoSuit, 0);

    QString classname;
    if (c->isKindOf("Slash"))
        classname = "Slash";
    else
        classname = c->getClassName();

    room->setPlayerFlag(sanae, "inovation_fengzhu_used");

    if (sanae && sanae->isAlive() && sanae->hasSkill("inovation_fengzhu") && !sanae->isKongcheng()){
        const Card* card = room->askForCardShow(sanae, sanae, "inovation_fengzhu");
        if (card){
            JudgeStruct judge;
            judge.reason = "inovation_fengzhu";
            judge.who = sanae;
            judge.pattern = ".|" + card->getSuitString();
            room->judge(judge);
            if (judge.isGood()){
                c->setSkillName("inovation_fengzhu");
                c->deleteLater();
                return c;
            }
            else{
                room->obtainCard(sanae, judge.card->getEffectiveId());
            }
        }
    }

    return NULL;

}

class InovationFengzhuVS : public ZeroCardViewAsSkill
{
public:
    InovationFengzhuVS() : ZeroCardViewAsSkill("inovation_fengzhu")
    {
    }

    const Card *viewAs() const
    {
        QString pattern;

        if (Self->getHandcardNum() == 0){
            return NULL;
        }

        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
            const Card *c = Self->getTag("inovation_fengzhu").value<const Card *>();
            if (c == NULL || Self->hasFlag("inovation_fengzhu_used"))
                return NULL;

            pattern = c->objectName();
        }
        else {
            pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
            if (pattern == "peach+analeptic" && Self->getMark("Global_PreventPeach") > 0)
                pattern = "analeptic";

            // check if it can use
            bool can_use = false;
            QStringList p = pattern.split("+");
            foreach(const QString &x, p) {
                const Card *c = Sanguosha->cloneCard(x);
                QString us = c->getClassName();
                if (c->isKindOf("Slash"))
                    us = "Slash";

                if (!Self->hasFlag("inovation_fengzhu_used"))
                    can_use = true;

                delete c;
                if (can_use)
                    break;
            }

            if (!can_use)
                return NULL;
        }

        InovationFengzhuCard *fz = new InovationFengzhuCard;
        fz->setUserString(pattern);

        return fz;

    }

    bool isEnabledAtPlay(const Player *player) const
    {
        if (player->isKongcheng()){
            return false;
        }

        if (player->hasFlag("inovation_fengzhu_used")){
            return false;
        }

        QList<const Player *> sib = player->getAliveSiblings();
        if (player->isAlive())
            sib << player;

        bool noround = true;

        foreach(const Player *p, sib) {
            if (p->getPhase() != Player::NotActive) {
                noround = false;
                break;
            }
        }

        return true; // for DIY!!!!!!!
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        QList<const Player *> sib = player->getAliveSiblings();
        if (player->isAlive())
            sib << player;

        bool noround = true;

        foreach(const Player *p, sib) {
            if (p->getPhase() != Player::NotActive) {
                noround = false;
                break;
            }
        }

        if (noround)
            return false;

        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            return false;

#define FENGZHU_CAN_USE(x) (!player->hasFlag("inovation_fengzhu_used"))

        if (pattern == "slash")
            return FENGZHU_CAN_USE(Slash);
        else if (pattern == "peach")
            return FENGZHU_CAN_USE(Peach) && player->getMark("Global_PreventPeach") == 0;
        else if (pattern.contains("analeptic"))
            return FENGZHU_CAN_USE(Peach) || FENGZHU_CAN_USE(Analeptic);
        else if (pattern == "jink")
            return FENGZHU_CAN_USE(Jink);

#undef FENGZHU_CAN_USE

        return false;
    }
};

class InovationFengzhu : public TriggerSkill
{
public:
    InovationFengzhu() : TriggerSkill("inovation_fengzhu")
    {
        view_as_skill = new InovationFengzhuVS;
        events << EventPhaseChanging;
    }

    QDialog *getDialog() const
    {
        return GuhuoDialog::getInstance(objectName(), true, false);
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to != Player::NotActive)
            return false;

        foreach(ServerPlayer *p, room->getAlivePlayers()) {
            if (p->hasFlag("inovation_fengzhu_used"))
                room->setPlayerFlag(p, "-inovation_fengzhu_used");
        }

        return false;
    }
};


class Zuzhou : public TriggerSkill
{
public:
    Zuzhou() : TriggerSkill("inovation_zuzhou")
    {
        frequency = Compulsory;
        events << TargetConfirmed << EventPhaseEnd << Death;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TargetConfirmed){
            CardUseStruct use = data.value<CardUseStruct>();
            foreach(ServerPlayer *p, use.to){
                if (p->hasSkill(objectName()) && p == player && use.from && p != use.from){
                    if (use.from->getMark("@inovation_zuzhou") == 0){
                        room->broadcastSkillInvoke(objectName());
                    }
                    if (p->getLostHp() == 0){
                        use.from->gainMark("@inovation_zuzhou", 1);
                    }
                    else{
                        use.from->gainMark("@inovation_zuzhou", p->getLostHp());
                    }
                }
            }
        }
        else if (triggerEvent == EventPhaseEnd){
            if (player->getPhase() != Player::Discard){
                return false;
            }
            bool will_turen = false;
            if (player->getMaxCards() > 0)
                will_turen = true;

            player->loseAllMarks("@inovation_zuzhou");

            if (will_turen){
                return false;
            }
            ServerPlayer *f = room->findPlayerBySkillName(objectName());
            if (f && f->isAlive()){
                f->drawCards(1);
            }
        }
        else if (triggerEvent == Death){
            DeathStruct death = data.value<DeathStruct>();
            if (death.who->hasSkill(objectName())){
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    p->loseAllMarks("@inovation_zuzhou");
                }
            }
        }

        return false;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }
};

class ZuzhouClear : public DetachEffectSkill
{
public:
    ZuzhouClear() : DetachEffectSkill("inovation_zuzhou")
    {
    }

    void onSkillDetached(Room *room, ServerPlayer *player) const
    {
        foreach(ServerPlayer *p, room->getAlivePlayers()){
            p->loseAllMarks("@inovation_zuzhou");
        }
    }
};

class ZuzhouMaxCards : public MaxCardsSkill
{
public:
    ZuzhouMaxCards() : MaxCardsSkill("#inovation_zuzhou")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->getMark("@inovation_zuzhou") > 0){
            return  -target->getMark("@inovation_zuzhou");
        }
        else
            return 0;
    }
};


JiguanCard::JiguanCard()
{
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

class JiguanVS : public ZeroCardViewAsSkill
{
public:
    JiguanVS() : ZeroCardViewAsSkill("jiguan")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("JiguanCard");
    }

    const Card *viewAs() const
    {
        return new JiguanCard();
    }
};

class Jiguan : public TriggerSkill
{
public:
    Jiguan() : TriggerSkill("jiguan")
    {
        view_as_skill = new JiguanVS;
        events << CardUsed;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        ServerPlayer *fear = room->findPlayerBySkillName(objectName());
        if (!fear || fear->isDead() || fear->getPile("jiguan").length() == 0){
            return false;
        }
        if (use.from->isDead()){
            return false;
        }
        QList<int> ava;
        foreach(int id, fear->getPile("jiguan")){
            if (Sanguosha->getCard(id)->getNumber() == use.card->getNumber()){
                ava.append(id);
            }
        }

        if (ava.length() == 0){
            return false;
        }

        if (!room->askForSkillInvoke(fear, objectName(), data)){
            return false;
        }

        room->fillAG(ava, fear);
        int tid = room->askForAG(fear, ava, true, objectName());
        room->clearAG(fear);
        if (tid == -1){
            return false;
        }
        room->showCard(fear, tid);
        room->broadcastSkillInvoke(objectName());
        room->doAnimate(QSanProtocol::S_ANIMATE_LIGHTBOX, "lani=skills/jiguan", QString("%1:%2").arg(1000).arg(0));
        if (use.from == fear){
            room->loseHp(room->askForPlayerChosen(fear, room->getAlivePlayers(), objectName()));
        }
        else{
            room->throwCard(tid, fear, fear);
            room->loseHp(use.from);
        }


        return false;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

};

class JiguanClear : public DetachEffectSkill
{
public:
    JiguanClear() : DetachEffectSkill("jiguan", "jiguan")
    {
    }
};

//misaka mikoto
PaojiCard::PaojiCard()
{
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

class Paojivs : public ViewAsSkill
{
public:
    Paojivs() :ViewAsSkill("paoji")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("PaojiCard")&&player->hasSkill("paoji");
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        return selected.length() == 0 && !to_select->isEquipped();
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return NULL;
        PaojiCard *pj = new PaojiCard();
        pj->addSubcards(cards);
        return pj;
    }
};

class Paoji : public TriggerSkill
{
public:
    Paoji() : TriggerSkill("paoji")
    {
        events << GameStart << CardUsed << DamageCaused;
        global=true;
        view_as_skill=new Paojivs;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        /*PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to == Player::Draw && room->askForSkillInvoke(mikoto, objectName(), data)){
            mikoto->skip(Player::Draw);
            QStringList stringlist;
            for (int i = 1; i <= mikoto->getMaxHp(); i++){
                stringlist.append(QString::number(i));
            }
            ServerPlayer *target = room->askForPlayerChosen(mikoto, room->getAlivePlayers(), objectName());
            if (!target){
                return false;
            }
            int num = room->askForChoice(mikoto, objectName(), stringlist.join("+"), data).toInt();

            QList<int> card_ids;
            QList<Card::Color> colors;
            for (int i = 0; i < num; i++){
                JudgeStruct judge;
                judge.reason = objectName();
                judge.play_animation = true;
                judge.who = target;
                room->judge(judge);
                card_ids.append(judge.card->getEffectiveId());
                if (!colors.contains(judge.card->getColor())){
                    colors.append(judge.card->getColor());
                }
                if (i == 0){
                    room->setTag("paoji_first_color", QVariant(judge.card->isRed()));
                }
            }
            if (colors.length() == 1){
                room->damage(DamageStruct(objectName(), mikoto, target, 1, DamageStruct::Thunder));
                room->broadcastSkillInvoke(objectName(), 2 + qsanRandomBounded(2));
            }
            else{
                room->broadcastSkillInvoke(objectName(), 1);
            }

            DummyCard *dummy = new DummyCard(card_ids);
            room->obtainCard(mikoto, dummy);
        }*/

        if (triggerEvent==GameStart){
            if (player->hasSkill(objectName())) {
                player->gainMark("@ying",4);
            }
        }
        else if(triggerEvent==CardUsed){
            if (!player->hasSkill(objectName())){
                return false;
            }
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->objectName()!="thunder_slash"||!player->askForSkillInvoke(objectName(),data)){
                return false;
            }
            room->broadcastSkillInvoke(objectName());
            if (player->getMark("@ying")>0){
                player->loseMark("@ying");
                JudgeStruct judge;
                judge.pattern = "club|spade";
                judge.good = true;
                judge.reason = objectName();
                judge.who = player;
                room->judge(judge);
                if (judge.card->isBlack()){
                    use.card->setFlags(objectName());
                }
            }
            else if(player->getMark("@ying")==0){
                QStringList stringlist;
                for (int i = 1; i <= room->getAlivePlayers().length(); i++){
                    stringlist.append(QString::number(i));
                }
                int num = room->askForChoice(player, "paoji_addtargets", stringlist.join("+"), data).toInt();
                for (int i = 1; i <= num; i++){
                    stringlist.append(QString::number(i));
                    ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName());
                    if (!use.to.contains(target)){
                        use.to.append(target);
                    }
                }
                data.setValue(use);
                use.card->setFlags(objectName());
                room->detachSkillFromPlayer(player,objectName());
            }
        }
        else if (triggerEvent==DamageCaused){
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card&&damage.card->hasFlag(objectName())){
                    damage.damage=damage.damage+1;
                    data.setValue(damage);
                    damage.card->clearFlags();
            }
        }
        return false;
    }
};

class Dianci : public TriggerSkill
{
public:
    Dianci() : TriggerSkill("inovation_dianci")
    {
        events << EventPhaseStart << EventPhaseEnd;
        global=true;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        /*CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from && move.from->hasSkill(objectName()) && (!move.to || move.to != move.from) && move.from->getPhase() == Player::NotActive){
            ServerPlayer *mikoto = room->findPlayerBySkillName(objectName());
            if (!mikoto || !mikoto->askForSkillInvoke(objectName(), data)){
                return false;
            }
            foreach(int id, move.card_ids){
                ServerPlayer *target = room->askForPlayerChosen(mikoto, room->getAlivePlayers(), objectName());
                if (!target){
                    return false;
                }
                room->setPlayerProperty(target, "chained", QVariant(true));

            }
        }*/
        ServerPlayer *sp=room->findPlayerBySkillName(objectName());
        if (!sp){
            return false;
        }
        if (triggerEvent==EventPhaseStart){
            if (sp->distanceTo(player)>1||player->isKongcheng()||player->getPhase()!=Player::RoundStart||!sp->askForSkillInvoke(objectName(),data)){
                return false;
            }
            int id=room->askForCardChosen(sp,player,"h",objectName());
            room->showCard(player, id, sp);
            Card *c=Sanguosha->getCard(id);
            if (c->isBlack()){
                 QString choice=room->askForChoice(sp,objectName(),"inovation_dianci_obtain+inovation_dianci_kill+inovation_dianci_chain+inovation_dianci_give");
                 room->broadcastSkillInvoke(objectName());
                 if (choice=="inovation_dianci_obtain"){
                     room->obtainCard(sp,c);
                     room->setPlayerFlag(player, sp->objectName()+"inovation_dianci_pro");
                 }
                 else if (choice=="inovation_dianci_kill"){
                     Slash *slash = new Slash(c->getSuit(), c->getNumber());
                     slash->addSubcard(c);
                     ServerPlayer *target = room->askForPlayerChosen(sp, room->getAlivePlayers(), objectName());
                     if (!target){
                         return false;
                     }
                     room->useCard(CardUseStruct(slash, sp, target));
                 }
                 else if (choice=="inovation_dianci_chain"){
                     QList<int> ids;
                     ids.append(id);
                     CardsMoveStruct move(ids, NULL, Player::DrawPile,
                         CardMoveReason(CardMoveReason::S_REASON_PUT, sp->objectName(), objectName(), QString()));
                     room->moveCardsAtomic(move,false);
                     for (int i=0;i<2;i++){
                         ServerPlayer *target = room->askForPlayerChosen(sp, room->getAlivePlayers(), objectName());
                         if (!target){
                             return false;
                         }
                         room->setPlayerProperty(target, "chained", QVariant(true));
                     }
                 }
                 else {
                     ServerPlayer *target=room->askForPlayerChosen(sp,room->getOtherPlayers(player),objectName());
                     room->obtainCard(target,c);
                     QString type="";
                     if (c->getSuit()==Card::Spade){
                         type=".|spade|.|hand";
                     }
                     else if (c->getSuit()==Card::Heart){
                         type=".|heart|.|hand";
                     }
                     else if (c->getSuit()==Card::Club){
                         type=".|club|.|hand";
                     }
                     else{
                         type=".|diamond|.|hand";
                     }
                     room->setPlayerCardLimitation(target,"discard,use,response",type,false);
                     room->setTag(target->objectName()+"inovation_dianci",QVariant(type));
                     if (sp->objectName()==player->objectName()){
                         room->setPlayerMark(sp,"thisturn",1);
                     }
                 }
            }
        }
        else if (triggerEvent==EventPhaseEnd){
            if (player->getPhase()==Player::Finish&&player->hasSkill(objectName())){
                if (player->getMark("thisturn")>0){
                    room->setPlayerMark(player,"thisturn",0);
                    return false;
                }
                foreach (ServerPlayer *p,room->getAlivePlayers()){
                    QString s=room->getTag(p->objectName()+"inovation_dianci").toString();
                    if (s!=""){
                        room->removePlayerCardLimitation(p,"discard,use,response",s);
                    }
                }
            }
        }
        return false;
    }
};

class DianciProhibit : public ProhibitSkill
{
public:
    DianciProhibit() : ProhibitSkill("#inovation_dianci")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        if (from->hasFlag(to->objectName()+"inovation_dianci_pro")){
            return true;
        }
        return false;
    }
};


class Shuji : public TriggerSkill
{
public:
    Shuji() : TriggerSkill("shuji")
    {
        events << CardsMoveOneTime << EventPhaseStart << EventPhaseEnd;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == CardsMoveOneTime){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.reason.m_reason != CardMoveReason::S_REASON_USE && move.reason.m_reason != CardMoveReason::S_REASON_LETUSE){
                return false;
            }

            if (move.card_ids.length() == 0 || move.to_place != Player::DiscardPile){
                return false;
            }

            ServerPlayer *dalian = room->findPlayerBySkillName(objectName());

            if (!dalian || !dalian->isAlive() || dalian->isKongcheng()){
                return false;
            }
            foreach(int card_id, move.card_ids){
                Card *card = Sanguosha->getCard(card_id);
                if (card->isKindOf("TrickCard")){

                    QList<int> list = dalian->getPile("huanshu");
                    if (list.length() > 8){
                        return false;
                    }
                    bool has_same = false;
                    foreach(int id, list){
                        if (Sanguosha->getCard(id)->getClassName() == card->getClassName()){
                            has_same = true;
                            break;
                        }
                    }

                    if (has_same){
                        continue;
                    }

                    room->setTag("shuji-card", QVariant(card_id));
                    if (room->askForDiscard(dalian, objectName(), 1, 1, true, true, "@shuji-discard")){
                        if (dalian->getGeneral2Name() == "inovation_Hugh"){
                            room->broadcastSkillInvoke(objectName(), 3);
                        }
                        else{
                            room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) + 1);
                        }

                        dalian->addToPile("huanshu", card_id);
                    }
                    room->removeTag("shuji-card");
                }
            }
        }
        else if (triggerEvent == EventPhaseStart){
            if (player->hasSkill(objectName()) && player->getPhase() == Player::Discard){
                QString _type = "TrickCard|.|.|hand"; // Handcards only
                room->setPlayerCardLimitation(player, "discard", _type, true);
            }
        }
        else if (triggerEvent == EventPhaseEnd){
            if (player->hasSkill(objectName()) && player->getPhase() == Player::Discard){
                QString _type = "TrickCard|.|.|hand"; // Handcards only
                room->removePlayerCardLimitation(player, "discard", _type);
            }
        }
        return false;
    }
};

class ShujiMaxCards : public MaxCardsSkill
{
public:
    ShujiMaxCards() : MaxCardsSkill("#shuji")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->hasSkill("shuji")){
            int num = 0;
            foreach (const Card* card, target->getHandcards()){
                num += card->isKindOf("TrickCard") ? 1 : 0;
            }
            return  num;
        }
        else
            return 0;
    }
};

class Jicheng : public TriggerSkill
{
public:
    Jicheng() : TriggerSkill("jicheng")
    {
        events << EventPhaseStart;
        frequency = Wake;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (event == EventPhaseStart && player->hasSkill(objectName()) && player->getPhase() == Player::RoundStart){
            int minHp = 100;
            int minHand = 100;
            foreach(ServerPlayer *p, room->getOtherPlayers(player)){
                if (p->getHp() < minHp){
                    minHp = p->getHp();
                }
                if (p->getHandcardNum() < minHand){
                    minHand = p->getHandcardNum();
                }
            }
            if ((player->getHandcardNum() < minHand || player->getHp() < minHp) && player->getMark("@waked") == 0){
                room->broadcastSkillInvoke(objectName());
                room->doLightbox("jicheng$", 3000);
                room->recover(player, RecoverStruct(player, 0, player->getLostHp()));
                player->drawCards(2);
                player->gainMark("@waked");
                room->changeHero(player, "inovation_Hugh", false, false, true);
            }
        }
        return false;
    }
};

/*
class ShoushiProhibit : public ProhibitSkill
{
public:
    ShoushiProhibit() : ProhibitSkill("#shoushi")
    {
    }

    bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        if (!to->hasSkill(this)){
            return false;
        }
        foreach(int card_id, to->getPile("huanshu")){
            if (Sanguosha->getCard(card_id)->getClassName() == card->getClassName()){
                return true;
            }
        }
        return false;
    }
};*/

class Shoushi : public TriggerSkill
{
public:
    Shoushi() : TriggerSkill("shoushi")
    {
        events << PreCardUsed << TrickCardCanceling << TargetConfirmed;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TrickCardCanceling){
            CardEffectStruct effect = data.value<CardEffectStruct>();
            if (effect.from && effect.from->hasSkill(objectName())){
                if (!effect.card || !effect.card->isNDTrick()){
                    return false;
                }
                int num = 0;
                ServerPlayer *jianyong = effect.from;
                foreach(int card_id, jianyong->getPile("huanshu")){
                    num += Sanguosha->getCard(card_id)->getSuit() == effect.card->getSuit() ? 1 : 0;
                }
                if (num == 0){
                    return false;
                }

                if (!jianyong->hasFlag("Shoushi_sound_used")){
                    room->broadcastSkillInvoke(objectName());
                    jianyong->setFlags("Shoushi_sound_used");
                }

                return true;
            }

        }
        if (triggerEvent == TargetConfirmed && TriggerSkill::triggerable(player)) {
            CardUseStruct use = data.value<CardUseStruct>();

            if (use.to.contains(player) && use.from != player) {
                if (use.card && use.card->isNDTrick()) {
                    bool can_trigger = false;
                    foreach(int card_id, player->getPile("huanshu")){
                        if (Sanguosha->getCard(card_id)->getClassName() == use.card->getClassName()){
                            can_trigger = true;
                            break;
                        }
                    }

                    if (can_trigger && room->askForSkillInvoke(player, objectName(), data)) {
                        room->broadcastSkillInvoke(objectName());
                        use.nullified_list << player->objectName();
                        data = QVariant::fromValue(use);
                    }
                }
            }
        }
        else if (triggerEvent == PreCardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isNDTrick() && use.from->hasSkill(objectName())) {
                ServerPlayer *jianyong = use.from;
                int num = 0;
                foreach(int card_id, jianyong->getPile("huanshu")){
                    num += Sanguosha->getCard(card_id)->getSuit() == use.card->getSuit() ? 1 : 0;
                }


                //1
                // cannot wu xie ke ji


                if (num < 2){
                    return false;
                }

                jianyong->drawCards(1);

                // 2

                if (num < 3){
                    return false;
                }
                if (use.card->isKindOf("Collateral")){
                    return false;
                }
                QList<ServerPlayer *> available_targets;
                if (!use.card->isKindOf("AOE") && !use.card->isKindOf("GlobalEffect")) {
                    room->setPlayerFlag(jianyong, "ShoushiExtraTarget");
                    foreach(ServerPlayer *p, room->getAlivePlayers()) {
                        if (use.to.contains(p) || room->isProhibited(jianyong, p, use.card)) continue;
                        if (use.card->targetFixed()) {
                            if (!use.card->isKindOf("Peach") || p->isWounded())
                                available_targets << p;
                        }
                        else {
                            if (use.card->targetFilter(QList<const Player *>(), p, jianyong))
                                available_targets << p;
                        }
                    }
                    room->setPlayerFlag(jianyong, "-ShoushiExtraTarget");
                }
                QStringList choices;
                choices << "cancel";
                if (use.to.length() > 1) choices.prepend("remove");
                if (!available_targets.isEmpty()) choices.prepend("add");
                if (choices.length() == 1) return false;

                QString choice = room->askForChoice(jianyong, "shoushi", choices.join("+"), data);
                if (choice == "cancel")
                    return false;
                else if (choice == "add") {
                    ServerPlayer *extra = NULL;
                    extra = room->askForPlayerChosen(jianyong, available_targets, "shoushi", "@shoushi-add:::" + use.card->objectName());
                    use.to.append(extra);
                    room->sortByActionOrder(use.to);
                }
                else {
                    ServerPlayer *removed = room->askForPlayerChosen(jianyong, use.to, "shoushi", "@shoushi-remove:::" + use.card->objectName());
                    use.to.removeOne(removed);
                }
            }
            data = QVariant::fromValue(use);

        }

        return false;
    }
};


class Kaiqi : public TriggerSkill
{
public:
    Kaiqi() : TriggerSkill("kaiqi")
    {
        events << EventPhaseStart;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (triggerEvent == EventPhaseStart){
            QList<ServerPlayer *> left = room->getAlivePlayers();
            if (player->hasSkill(objectName()) && player->getPhase() == Player::Play){
                int i = 0;
                while (player->getPile("huanshu").length() > 0 && left.length() > 0){
                    ServerPlayer *target = room->askForPlayerChosen(player, left, objectName(), "@shuji-prompt", true);
                    if (!target){
                        return false;
                    }
                    if (i == 0){
                        room->broadcastSkillInvoke(objectName());
                        room->doLightbox("kaiqi$", 800);
                    }
                    i++;

                    left.removeOne(target);
                    QList<int> card_ids = player->getPile("huanshu");
                    room->fillAG(card_ids, player);
                    int id = room->askForAG(player, card_ids, false, objectName());
                    room->clearAG(player);
                    if (id == -1){
                        return false;
                    }
                    room->obtainCard(target, id);
                }
            }
        }
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

    General *sakura = new General(this, "inovation_DarkSakura1", "magic", 8, false, true);
    sakura->addSkill(new Xushu);
    sakura->addSkill(new Xishou);

    General *sakura2 = new General(this, "inovation_DarkSakura2", "magic", 4, false, true);
    sakura2->addSkill("xushu");
    sakura2->addSkill("xishou");
    sakura2->addSkill(new Shengbei);
    sakura2->addSkill(new ShengbeiMaxCards);
    related_skills.insert("shengbei", "#shengbei");
    sakura2->addSkill(new Caoying);
    sakura2->addSkill(new ShengjianBlack);

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
