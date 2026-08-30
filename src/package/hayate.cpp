#include "hayate.h"
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
#include "game-rng.h"

class Yingdi : public TriggerSkill
{
public:
    Yingdi() : TriggerSkill("yingdi")
    {
        frequency = Compulsory;
        events << TargetConfirmed << Damage << EventPhaseStart;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TargetConfirmed){

            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || (!use.card->isKindOf("Slash") && !use.card->isKindOf("Duel"))){
                return false;
            }
            if (!use.from || use.from->getMark("@real_hei") > 0){
                return false;
            }
            if (use.to.length() == 0){
                return false;
            }
            //if it is ai, then return false
            if (use.from->getAI() != NULL){
                return false;
            }

            foreach(ServerPlayer *to, use.to){
                if (!to->hasSkill(objectName())){
                    continue;
                }
                use.nullified_list << to->objectName();
            }
                room->broadcastSkillInvoke(objectName(), qsanRandomBounded(3) + 1);
            data = QVariant::fromValue(use);
        }
        else if (triggerEvent == Damage){
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.from || !damage.from->hasSkill(objectName())){
                return false;
            }
            if (damage.to && damage.to->getMark("@real_hei") == 0){
                room->broadcastSkillInvoke(objectName(), 4);
                damage.to->gainMark("@real_hei");
            }
        }
        else if (triggerEvent == EventPhaseStart){
            if (player && player->hasSkill(objectName()) && player->isAlive() && player->getPhase() == Player::RoundStart){
                QStringList lst;
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    if (!lst.contains(p->getRole())){
                        lst.append(p->getRole());
                    }
                }
                if (lst.length() <= 2){
                    room->broadcastSkillInvoke(objectName(), 4);
                    room->detachSkillFromPlayer(player, objectName());
                    room->acquireSkill(player, "jiesha");
                }
                else{
                    room->broadcastSkillInvoke(objectName(), qsanRandomBounded(3) + 1);
                }
            }
        }
        return false;
    }
};

class YingdiClear : public DetachEffectSkill
{
public:
    YingdiClear() : DetachEffectSkill("yingdi")
    {
    }

    void onSkillDetached(Room *room, ServerPlayer *) const
    {
        foreach(ServerPlayer *player, room->getAlivePlayers()){
            if (player->getMark("@real_hei") > 0){
                player->loseAllMarks("@real_hei");
            }
        }
    }
};


class Diansuo : public TriggerSkill
{
public:
    Diansuo() : TriggerSkill("diansuo")
    {
        frequency = NotFrequent;
        events << DamageCaused;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        
        if (triggerEvent == DamageCaused){
            DamageStruct damage = data.value<DamageStruct>();
            ServerPlayer * hei = room->findPlayerBySkillName(objectName());
            if (!hei)
                return false;
            QList<ServerPlayer *> other_chained;
            QList<ServerPlayer *> other_not_chained;
            foreach(ServerPlayer *p, room->getAlivePlayers()){
                if (p != hei && p->isChained()){
                    other_chained.append(p);
                }
                if (p != hei && !p->isChained()){
                    other_not_chained.append(p);
                }
            }
            if (damage.from == hei){
                if (damage.from->isChained() || other_not_chained.count() == 0 || !room->askForSkillInvoke(hei, objectName(), data)){
                    return false;
                }
                ServerPlayer *victim = room->askForPlayerChosen(hei, other_not_chained, objectName(), "@diansuo-prompt");
                room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) + 1);
                room->setPlayerProperty(hei, "chained", QVariant(true));
                room->setPlayerProperty(victim, "chained", QVariant(true));
                damage.from = victim;
                data.setValue(damage);
            }
            else{
                if (!hei->isChained() || other_chained.count() == 0 || !room->askForSkillInvoke(hei, objectName(), data)){
                    return false;
                }
                ServerPlayer *victim = room->askForPlayerChosen(hei, other_chained, objectName(), "@diansuo-prompt-remove");
                room->broadcastSkillInvoke(objectName());
                room->setPlayerProperty(hei, "chained", QVariant(false));
                room->setPlayerProperty(victim, "chained", QVariant(false));
                damage.to = victim;
                room->broadcastSkillInvoke(objectName(), 3);
                data.setValue(damage);
            }
        }
        return false;
    }
};

class Jiesha : public TriggerSkill
{
public:
    Jiesha() : TriggerSkill("jiesha")
    {
        frequency = Compulsory;
        events << SlashProceed;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (triggerEvent == SlashProceed) {
            SlashEffectStruct effect = data.value<SlashEffectStruct>();
            if (effect.from && effect.from->hasSkill(objectName()) && effect.from->getWeapon() && effect.from->getWeapon()->isKindOf("DoubleSword") && effect.to){
                LogMessage log;
                log.type = "$jiesha_effect";
                log.from = effect.to;
                log.arg = effect.slash->objectName();
                room->sendLog(log);
                room->slashResult(effect, NULL);
                room->broadcastSkillInvoke(objectName());
                return true;
            }
        }
        return false;
    }
};

// for oumashu
class LonelinessInvalidity : public InvaliditySkill
{
public:
    LonelinessInvalidity() : InvaliditySkill("#loneliness-inv")
    {
    }

    bool isSkillValid(const Player *player, const Skill *skill) const
    {
        return player->getMark("Loneliness" + skill->objectName()) == 0;
    }
};



// akame


// diarmuid
class Pomo : public TriggerSkill
{
public:
    Pomo() : TriggerSkill("pomo")
    {
        frequency = Compulsory;
        events << TargetConfirmed << CardFinished << TargetSpecified;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TargetConfirmed)
        {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.from && use.from->hasSkill(objectName()) && use.card->isKindOf("Slash") && use.from == player){
                room->setPlayerFlag(use.from, "PomoArmor");
                room->broadcastSkillInvoke(objectName());
                foreach(ServerPlayer *p, use.to){
                    room->setPlayerMark(p, "Armor_Nullified", 1);
                }
            }
        }
        else if (triggerEvent == TargetSpecified && TriggerSkill::triggerable(player)){
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash"))
                return false;
            foreach(ServerPlayer *p, use.to) {
                if (!player->isAlive()) break;
                p->addMark("pomo");
                room->addPlayerMark(p, "@all_skill_invalidity");

                foreach(ServerPlayer *pl, room->getAllPlayers())
                    room->filterCards(pl, pl->getCards("he"), true);
                JsonArray args;
                args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
                room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
            }
        }
        else if (triggerEvent == CardFinished)
        {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Slash") && use.from->hasFlag("PomoArmor")){
                foreach(ServerPlayer *p, use.to){
                    room->setPlayerMark(p, "Armor_Nullified", 0);
                    if (p->getMark("pomo") == 0) continue;
                    room->removePlayerMark(p, "@all_skill_invalidity", p->getMark("pomo"));
                    p->setMark("pomo", 0);
                }
            }
        }
        return false;
    }
};


class Bimie : public TriggerSkill
{
public:
    Bimie() : TriggerSkill("bimie")
    {
        frequency = NotFrequent;
        events << Damage << PreHpRecover << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Damage)
        {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && damage.card->isKindOf("Slash") && damage.from == player && damage.from->hasSkill(objectName()) && damage.to->getMark("@zhou") == 0 && damage.to->isAlive()){
                if (room->askForSkillInvoke(player, objectName(), data)){
                    room->broadcastSkillInvoke(objectName());
                    room->doLightbox("bimie$", 1500);
                    damage.to->gainMark("@zhou", 1);
                }
            }
        }
        else if (triggerEvent == PreHpRecover){
            return player->getMark("@zhou") > 0;
        }
        else if (triggerEvent == Death)
        {
            DeathStruct death = data.value<DeathStruct>();
            if (player == death.who && death.who->hasSkill(objectName())){
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    room->setPlayerMark(p, "@zhou", 0);
                }
            }
        }
        return false;
    }
};


class Gangqu : public TriggerSkill
{
public:
    Gangqu() : TriggerSkill("gangqu")
    {
        frequency = NotFrequent;
        events << EventPhaseStart << CardEffected;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart)
        {
            if (player->getPhase() == Player::Finish){
                if (!player->isKongcheng() && player->getPile("gang").length() == 0 && room->askForSkillInvoke(player, objectName(), data)){
                    player->addToPile("gang", room->askForCardChosen(player, player, "h", objectName()));
                }
            }
            else if (player->getPhase() == Player::Start && player->getPile("gang").length() > 0){
                if (player->getPile("gang").length() == 1){
                    room->obtainCard(player, player->getPile("gang").first());
                }
                else{
                    room->fillAG(player->getPile("gang"), player);
                    room->obtainCard(player, room->askForAG(player, player->getPile("gang"), false, objectName()));
                    room->clearAG(player);
                }
            }
        }
        else if (triggerEvent == CardEffected){
            CardEffectStruct effect = data.value<CardEffectStruct>();
            if (effect.to == player && player->getPile("gang").length() > 0 && effect.card->getType() == (Sanguosha->getCard(player->getPile("gang").first()))->getType() && room->askForSkillInvoke(player, objectName() + "Prevent", data)){
                room->broadcastSkillInvoke(objectName());
                room->doLightbox(objectName() + "$", 800);
                return true;
            }
        }
        return false;
    }
};

class GangquClear : public DetachEffectSkill
{
public:
    GangquClear() : DetachEffectSkill("gangqu", "gang")
    {
    }
};

TiaojiaoCard::TiaojiaoCard()
{
}

bool TiaojiaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty()) return false;
    return to_select != Self;
}

void TiaojiaoCard::use(Room *room, ServerPlayer *tsukushi, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.first();
    ServerPlayer *slashTarget = room->askForPlayerChosen(tsukushi, room->getAlivePlayers(), "tiaojiao");
    if (!room->askForUseSlashTo(target, slashTarget, "@TiaojiaoSlash:" + tsukushi->getGeneralName() + ":" + target->getGeneralName() + ":" + slashTarget->getGeneralName(), false)){
        if (!target->isNude()){
            room->obtainCard(tsukushi, room->askForCardChosen(tsukushi, target, "hej", objectName()));
        }
    }
}

class Tiaojiao : public ZeroCardViewAsSkill
{
public:
    Tiaojiao() : ZeroCardViewAsSkill("tiaojiao")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("TiaojiaoCard");
    }

    const Card *viewAs() const
    {
        return new TiaojiaoCard();
    }
};


class Gongming : public TriggerSkill
{
public:
    Gongming() : TriggerSkill("gongming")
    {
        frequency = NotFrequent;
        events << HpRecover;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        ServerPlayer *sher = room->findPlayerBySkillName(objectName());
        if (!sher){
            return false;
        }
        if (sher->getPhase() == Player::NotActive){
            return false;
        }
        if (room->askForChoice(sher, objectName(), "youdraw+hedraws") == "youdraw"){
            sher->drawCards(sher->getLostHp() + 1);
        }
        else{
            player->drawCards(sher->getLostHp() + 1);
        }
        room->broadcastSkillInvoke(objectName());
        return false;
    }
};

class YaojingVS : public OneCardViewAsSkill
{
public:
    YaojingVS() : OneCardViewAsSkill("yaojing")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->hasFlag("Yaojing_Active");
    }

    bool viewFilter(const Card *) const
    {
        return true;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Card *god_salvation = new GodSalvation(originalCard->getSuit(), originalCard->getNumber());
        god_salvation->addSubcard(originalCard->getId());
        god_salvation->setSkillName(objectName());
        return god_salvation;
    }
};

class Yaojing : public TriggerSkill
{
public:
    Yaojing() : TriggerSkill("yaojing")
    {
        events << EventPhaseChanging << CardUsed << Death;
        view_as_skill = new YaojingVS;
    }

    int getEffectIndex(const ServerPlayer*, const Card*){
        return 0;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging && player->hasSkill(objectName())) {
            // change phase to not active -> make change to maximum
            PhaseChangeStruct changing = data.value<PhaseChangeStruct>();
            if (changing.from == Player::Discard){
                player->setTag("yaojing_times", 0);
            }
            else if (changing.to == Player::Play){
                if (room->hasAura()){
                    if (room->getAura() == objectName() || room->getAura() == "MacrossF"){
                        // is current

                        room->setPlayerFlag(player, "Yaojing_Active");
                        return false;
                    }
                    if (room->getAuraPlayer()->getHandcardNum() > player->getHandcardNum()){
                        // cannot replace
                        return false;
                    }

                }
                if (!player->askForSkillInvoke(objectName(), data)){
                    return false;
                }
                room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) * 2 + 1);
                room->setPlayerFlag(player, "Yaojing_Active");
                if (room->getAura() == "xingjian"){
                    room->doAura(player, "MacrossF");
                }
                else{
                    room->doAura(player, objectName());
                }

            }
        }
        else if (triggerEvent == CardUsed) {
            // gain card used mark
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("GodSalvation") && use.from == player && player->hasSkill(objectName())){
                if (use.card->getSkillName() == objectName()){
                    room->doLightbox(objectName() + "$", 1500);
                    player->setTag("yaojing_times", player->getTag("yaojing_times").toInt() + 1);
                }
            }
        }
        else if (triggerEvent == Death){
            DeathStruct death = data.value<DeathStruct>();
            if (death.who->hasSkill(objectName()) && room->hasAura() && (room->getAura() == objectName() || room->getAura() == "MacrossF")){
                player->setTag("yaojing_times", 0);
                if (room->getAura() == "MacrossF"){
                    ServerPlayer *ranka = room->findPlayerBySkillName("xingjian");
                    if (ranka && ranka->isAlive()){
                        room->doAura(ranka, "xingjian");
                        return false;
                    }

                }
                room->clearAura();
            }
        }

        return false;
    }
};

class YaojingClear : public DetachEffectSkill
{
public:
    YaojingClear() : DetachEffectSkill("yaojing")
    {
    }

    void onSkillDetached(Room *room, ServerPlayer *player) const
    {
        if ( room->hasAura() && (room->getAura() == objectName() || room->getAura() == "MacrossF")){
            player->setTag("yaojing_times", 0);
            if (room->getAura() == "MacrossF"){
                ServerPlayer *ranka = room->findPlayerBySkillName("xingjian");
                if (ranka &&ranka->isAlive()){
                    room->doAura(ranka, "xingjian");
                    return;
                }

            }
            room->clearAura();
        }
    }
};

class YaojingMaxCards : public MaxCardsSkill
{
public:
    YaojingMaxCards() : MaxCardsSkill("#yaojing")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->getTag("yaojing_times").toInt() > 0){
            return  -target->getTag("yaojing_times").toInt();
        }
        else
            return 0;
    }
};

class Kurimu : public TriggerSkill
{
public:
    Kurimu() : TriggerSkill("kurimu")
    {
        frequency = NotFrequent;
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from && move.from == player){
            foreach(int card_id, move.card_ids){
                Card *card = Sanguosha->getCard(card_id);
                if (player->getPhase() == Player::NotActive && card->getSuit() == Card::Diamond && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip)) && room->askForSkillInvoke(player, objectName(), data)){
                    room->broadcastSkillInvoke(objectName());
                    room->doLightbox(objectName() + "$", 800);
                    ServerPlayer *to = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName());
                    player->drawCards(1);
                    to->drawCards(1);
                }
            }
        }
        return false;
    }
};

class Minatsu : public TriggerSkill
{
public:
    Minatsu() : TriggerSkill("minatsu")
    {
        frequency = NotFrequent;
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from && move.from == player){
            foreach(int card_id, move.card_ids){
                Card *card = Sanguosha->getCard(card_id);
                if (player->getPhase() == Player::NotActive && card->getSuit() == Card::Club && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip)) && room->askForSkillInvoke(player, objectName(), data)){
                    room->broadcastSkillInvoke(objectName());
                    room->doLightbox(objectName() + "$", 800);
                    ServerPlayer *to = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName());
                    Card *card1 = Sanguosha->cloneCard("fire_slash", Card::NoSuit, 0);
                    card1->setSkillName(objectName());
                    room->useCard(CardUseStruct(card1, player, to, false));
                }
            }
        }
        return false;
    }
};


class Chizuru : public TriggerSkill
{
public:
    Chizuru() : TriggerSkill("chizuru")
    {
        frequency = NotFrequent;
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from && move.from == player){
            foreach(int card_id, move.card_ids){
                Card *card = Sanguosha->getCard(card_id);
                if (player->getPhase() == Player::NotActive && card->getSuit() == Card::Heart && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip)) && room->askForSkillInvoke(player, objectName(), data)){
                    room->broadcastSkillInvoke(objectName());
                    room->doLightbox(objectName() + "$", 800);
                    ServerPlayer *to = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName());
                    room->recover(to, RecoverStruct(player), true);
                }
            }
        }
        return false;
    }
};

class Mafuyu : public TriggerSkill
{
public:
    Mafuyu() : TriggerSkill("mafuyu")
    {
        frequency = NotFrequent;
        events << CardsMoveOneTime << EventPhaseStart << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == CardsMoveOneTime){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from && move.from->hasSkill(objectName()) && move.from->objectName() == player->objectName()){

                foreach(int card_id, move.card_ids){
                    Card *card = Sanguosha->getCard(card_id);
                    if (player->getPhase() == Player::NotActive && card->getSuit() == Card::Spade && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip)) && room->askForSkillInvoke(player, objectName(), data)){
                        room->broadcastSkillInvoke(objectName());
                        room->doLightbox(objectName() + "$", 800);
                        ServerPlayer *to = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName());
                        to->gainMark("@mafuyu");
                    }
                }
            }
        }
        else if (triggerEvent == EventPhaseStart){
            if (player->getPhase() == Player::RoundStart){
                ServerPlayer *key = room->findPlayerBySkillName(objectName());
                if (!key && player->getMark("@mafuyu") > 0){
                    player->loseAllMarks("@mafuyu");
                    return false;
                }
                if (player->getMark("@mafuyu") > 0 && !player->isSkipped(Player::Play)){
                    room->broadcastSkillInvoke(objectName());
                    player->loseMark("@mafuyu");
                    player->skip(Player::Play);
                }
            }
        }
        else if (triggerEvent == Death){
            DeathStruct death = data.value<DeathStruct>();
            if (death.who->hasSkill(objectName())){
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    p->loseAllMarks("@mafuyu");
                }
            }
        }
        return false;
    }
};

class MafuyuClear : public DetachEffectSkill
{
public:
    MafuyuClear() : DetachEffectSkill("mafuyu")
    {
    }

    void onSkillDetached(Room *room, ServerPlayer *) const
    {
        foreach(ServerPlayer *p, room->getAlivePlayers()){
            p->loseAllMarks("@mafuyu");
        }
    }
};


HaremuCard::HaremuCard()
{
    will_throw = false;
}

bool HaremuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return to_select->hasFlag("haremu_target") && targets.length() == 0;
}

void HaremuCard::use(Room *room, ServerPlayer *player, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.at(0);
    if (!target)
        return;
    QStringList used = player->getTag("heremu_targets").toStringList();
    used.append(target->objectName());
    player->setTag("heremu_targets", used);
    room->obtainCard(target, this, true);

    if (!target->hasClub() && room->askForChoice(target, "haremu", "haremu_accept+cancel", QVariant::fromValue(player)) == "haremu_accept"){
        target->addClub("bekiyou");
    }
    else{
        LogMessage log;
        log.type = "$refuse_club";
        log.from = target;
        log.arg = "haremu";
        room->sendLog(log);
    }
}

class HaremuVS : public OneCardViewAsSkill
{
public:
    HaremuVS() : OneCardViewAsSkill("haremu"){
        response_pattern = "@@haremu";
    }

    bool viewFilter(const Card *) const
    {
        return true;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        HaremuCard *hc = new HaremuCard();
        hc->addSubcard(originalCard);
        hc->setSkillName("haremu");
        return hc;
    }
};

class Haremu : public TriggerSkill
{
public:
    Haremu() : TriggerSkill("haremu")
    {
        frequency = Club;
        club_name = "bekiyou",
        events << EventPhaseStart;
        view_as_skill = new HaremuVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart){
            if (player->isFemale() && !player->hasSkill(objectName()) && player->getPhase() == Player::Play){
                ServerPlayer *key = room->findPlayerBySkillName(objectName());
                if (!key || !key->isAlive()){
                    return false;
                }
                QStringList used = key->getTag("heremu_targets").toStringList();
                if (used.contains(player->objectName())){
                    return false;
                }
                room->setPlayerFlag(player, "haremu_target");
                room->askForUseCard(key, "@@haremu", "@haremu-use");
            }
        }
        return false;
    }
};

class HaremuMaxCards : public MaxCardsSkill
{
public:
    HaremuMaxCards() : MaxCardsSkill("#haremu")
    {
    }

    int getExtra(const Player *target) const
    {
        if (!target->hasClub("bekiyou")){
            return 0;
        }
        int i = 1;
        foreach(const Player *p, target->getSiblings()){
            if (p->isAlive() && p->hasClub("bekiyou")){
                i++;
            }
        }
        return i;
    }
};

// redo  gaokang
class Gaokang : public TriggerSkill
{
public:
    Gaokang() : TriggerSkill("gaokang")
    {
        frequency = NotFrequent;
        events << DamageInflicted;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (triggerEvent == DamageInflicted){
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.damage < 1 || damage.nature != DamageStruct::Normal || !damage.to){
                return false;
            }
            ServerPlayer *redo = room->findPlayerBySkillName(objectName());
            if (!redo || redo->isDead() ||redo->isKongcheng() || redo->distanceTo(damage.to) > 1 || !room->askForSkillInvoke(redo, objectName(), data)){
                return false;
            }
            room->throwCard(room->askForCardChosen(redo, redo, "he", objectName()), redo, redo);
            damage.damage -= 1;
            room->broadcastSkillInvoke(objectName());
            data.setValue(damage);
            if (redo->isKongcheng()){
                damage.to->drawCards(2);
            }
        }
        return false;
    }
};

// eugeo
class Rennai : public TriggerSkill
{
public:
    Rennai() : TriggerSkill("rennai")
    {
        frequency = Compulsory;
        events << DamageInflicted << PreHpLost << EventPhaseStart << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    void calcFreeze(Room *room, ServerPlayer *player) const{
        if (!player || player->isDead()){
            return;
        }
        if (room->askForChoice(player, objectName(), "rennai_hp+rennai_handcardnum") == "rennai_hp"){
            QStringList hps;
            foreach(ServerPlayer *p, room->getAlivePlayers()){

                if (!hps.contains(QString::number(p->getHp()))){
                    hps.append(QString::number(p->getHp()));
                }
            }
            int targetHp = room->askForChoice(player, objectName(), hps.join("+"), QVariant("hp")).toInt();
            if (room->askForChoice(player, objectName(), "rennai_gain+rennai_lose", QVariant("hp+" + QString::number(targetHp))) == "rennai_gain"){
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    if (p->getHp() == targetHp){
                        p->gainMark("@Frozen_Eu");
                    }
                }
            }
            else{
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    if (p->getHp() == targetHp){
                        p->loseMark("@Frozen_Eu");
                    }
                }
            }
        }
        else{
            QStringList handcardnums;
            foreach(ServerPlayer *p, room->getAlivePlayers()){
                if (!handcardnums.contains(QString::number(p->getHandcardNum()))){
                    handcardnums.append(QString::number(p->getHandcardNum()));
                }
            }
            int targetHandcardnum = room->askForChoice(player, objectName(), handcardnums.join("+"), QVariant("handcardnum")).toInt();
            if (room->askForChoice(player, objectName(), "rennai_gain+rennai_lose", QVariant("handcardnum+" + QString::number(targetHandcardnum))) == "rennai_gain"){
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    if (p->getHandcardNum() == targetHandcardnum){
                        p->gainMark("@Frozen_Eu");
                    }
                }
            }
            else{
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    if (p->getHandcardNum() == targetHandcardnum){
                        p->loseMark("@Frozen_Eu");
                    }
                }
            }
        }
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == DamageInflicted){
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.to->hasSkill(objectName()) && damage.to->getMark("@Patience") == 0){
                room->loseHp(damage.to);
                room->doLightbox(objectName() + "$", 800);
                damage.to->gainMark("@Patience");
                calcFreeze(room, damage.to);
                return true;
            }
            else if (damage.to->hasSkill(objectName()) && damage.to->getMark("@Patience") > 0){
                if (damage.from){
                    LogMessage log;
                    log.type = "$rennai_effect";
                    log.arg = damage.from->getGeneralName();
                    room->sendLog(log);
                    calcFreeze(room, damage.to);
                }
                else if (damage.card){
                    LogMessage log;
                    log.type = "$rennai_effect";
                    log.arg = damage.card->getClassName();
                    room->sendLog(log);
                    calcFreeze(room, damage.to);
                }

                return true;
            }
        }
        else if (triggerEvent == PreHpLost && TriggerSkill::triggerable(player)){
            if (player->hasSkill(objectName()) && player->getMark("@Patience") > 0){
                LogMessage log;
                log.type = "$rennai_effect2";
                room->sendLog(log);
                calcFreeze(room, player);
                return true;
            }
        }
        else if (triggerEvent == EventPhaseStart && player->getPhase() == Player::RoundStart){
            player->loseAllMarks("@Patience");
        }
        else if (triggerEvent == Death){
            DeathStruct death = data.value<DeathStruct>();
            if (death.who->hasSkill(objectName())){
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    p->loseAllMarks("@Frozen_Eu");
                }
            }
        }
        return false;
    }
};

class RennaiClear : public DetachEffectSkill
{
public:
    RennaiClear() : DetachEffectSkill("rennai")
    {
    }

    void onSkillDetached(Room *room, ServerPlayer *) const
    {
        foreach(ServerPlayer *p, room->getAlivePlayers()){
            p->loseAllMarks("@Frozen_Eu");
        }
    }
};

class Zhanfang : public TriggerSkill
{
public:
    Zhanfang() : TriggerSkill("zhanfang")
    {
        frequency = NotFrequent;
        events << PreCardUsed << CardFinished << TrickCardCanceling << SlashProceed;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (triggerEvent == PreCardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.from->hasSkill(objectName())) {

                if (use.card->isKindOf("Collateral") || use.card->isKindOf("EquipCard") || use.card->isKindOf("DelayedTrick")){
                    return false;
                }
                if (use.to.count() != 1){
                    return false;
                }
                ServerPlayer *target = use.to.first();

                if (target->getMark("@Frozen_Eu") > 0 && room->askForSkillInvoke(use.from, objectName(), data)){
                    use.to.clear();
                    foreach(ServerPlayer *p, room->getAlivePlayers()){
                        if (p->getMark("@Frozen_Eu") > 0){
                            use.to.append(p);
                        }
                    }
                    use.card->setFlags("zhanfang_card");
                    room->doLightbox(objectName() + "$", 800);
                    data = QVariant::fromValue(use);
                }
            }


        }
        else if (triggerEvent == CardFinished){
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.from->isAlive() && use.card->hasFlag("zhanfang_card")){
                foreach(ServerPlayer *p, use.to){
                    if (p->isAlive() && p->getEquips().count() > 0 && room->askForChoice(p, objectName(), "zhanfang_discard+cancel", data) == "zhanfang_discard"){
                        room->throwCard(room->askForCardChosen(p, p, "e", objectName()), p);
                        p->loseMark("@Frozen_Eu");
                    }
                }
            }
        }
        else if (triggerEvent == TrickCardCanceling){
            CardEffectStruct effect = data.value<CardEffectStruct>();
            if (effect.from && effect.from->isAlive() && effect.from->hasSkill(objectName()) && effect.to && effect.to->getMark("@Frozen_Eu") > 0){
                LogMessage log;
                log.type = "$zhanfang_effect";
                log.from = effect.to;
                log.arg = effect.card->objectName();
                room->sendLog(log);
                room->broadcastSkillInvoke(objectName());
                return true;
            }
        }
        else if (triggerEvent == SlashProceed){
            SlashEffectStruct effect = data.value<SlashEffectStruct>();
            if (effect.from && effect.from->isAlive() && effect.from->hasSkill(objectName()) && effect.to && effect.to->getMark("@Frozen_Eu") > 0){
                LogMessage log;
                log.type = "$zhanfang_effect";
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
};

class Huajian : public TriggerSkill
{
public:
    Huajian() : TriggerSkill("huajian")
    {
        frequency = Limited;
        events << Death << CardsMoveOneTime << CardUsed;
        global = true;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Death){
            DeathStruct death = data.value<DeathStruct>();
            if (player && death.who == player && player->hasSkill(objectName())){
                int num = -1;

                for (int i = 0; i < Sanguosha->getCardCount(); i++){
                    if (Sanguosha->getCard(i)->objectName() == "GreenRose"){
                        num = i;
                    }
                }
                if (num == -1){
                    return false;
                }
                if (room->getTag("huajian_used").toBool() || !room->askForSkillInvoke(player, objectName(), data)){
                    return false;
                }
                ServerPlayer *dest = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName());
                room->doLightbox(objectName() + "$", 3000);
                if (dest->getWeapon()){
                    room->obtainCard(dest, dest->getWeapon());
                }

                CardsMoveStruct move;
                move.card_ids.append(num);
                move.to = dest;
                move.to_place = Player::PlaceEquip;
                move.reason.m_reason = CardMoveReason::S_REASON_RECYCLE;
                room->moveCardsAtomic(move, true);
                room->setTag("huajian_target", dest->objectName());
                room->setTag("huajian_used", QVariant(true));
            }
        }
        else if (triggerEvent == CardsMoveOneTime){

            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (!move.from || move.from->objectName() != room->getTag("huajian_target").toString()){
                return false;
            }
            if (move.from_places.contains(Player::PlaceEquip) || move.to_place == Player::DiscardPile){
                foreach(int id, move.card_ids){
                    if (Sanguosha->getCard(id)->objectName() == "GreenRose"){
                        foreach(ServerPlayer *p, room->getAlivePlayers()){
                            if (p->objectName() == room->getTag("huajian_target").toString()){
                                room->obtainCard(p, id);
                                return false;
                            }
                        }

                    }
                }
            }
        }
        else if (triggerEvent == CardUsed){
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.from->objectName() != room->getTag("huajian_target").toString() || !use.card){
                return false;
            }

            foreach(int id, use.card->getSubcards()){
                if (Sanguosha->getCard(id)->objectName() == "GreenRose"){
                    foreach(ServerPlayer *p, room->getAlivePlayers()){
                        if (p->objectName() == room->getTag("huajian_target").toString()){
                            room->obtainCard(p, id);
                            return false;
                        }
                    }
                }
            }
        }

        return false;
    }
};

//k1
class Guiyin : public TriggerSkill
{
public:
    Guiyin() : TriggerSkill("guiyin")
    {
        frequency = Compulsory;
        events << Damaged << EventPhaseStart << EventPhaseEnd << TargetConfirmed << Death;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Damaged){
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.from && damage.from != player && damage.to->hasSkill(objectName())){
                if (!player->hasFlag("guiyin2_sound_used")){
                    room->broadcastSkillInvoke(objectName(), 2);
                    player->setFlags("guiyin2_sound_used");
                }
                damage.from->gainMark("@Oni");
                room->setPlayerMark(damage.to, "OniLv", damage.to->getMark("OniLv") + 1);
            }
            
        }

        else if (triggerEvent == EventPhaseStart){
            if (player->hasSkill(objectName()) && player->getPhase() == Player::Finish && player->getMark("OniLv") > 2){
                room->broadcastSkillInvoke(objectName(), 3);
                foreach(ServerPlayer *p, room->getOtherPlayers(player)){
                    if (p->inMyAttackRange(player)){
                        p->gainMark("@Oni");
                        room->setPlayerMark(player, "OniLv", player->getMark("OniLv") + 1);
                    }
                }
            }
        }
        else if (triggerEvent == EventPhaseEnd){
            if (player->hasSkill(objectName()) && player->getPhase() == Player::Play && player->getMark("OniLv") > 4){
                room->broadcastSkillInvoke(objectName(), 4);
                room->doLightbox(objectName() + "$", 2000);
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    if (p->getMark("@Oni") > 0){
                        room->damage(DamageStruct(objectName(), player, p, p->getMark("@Oni") > 1 ? (p == player ? 1 : 2) : p->getMark("@Oni")));
                        p->loseAllMarks("@Oni");
                    }
                }
                foreach(ServerPlayer *p, room->getOtherPlayers(player)){
                    bool hasHeart = false;
                    QList<int> hearts;
                    foreach(const Card *card, p->getHandcards()){
                        if (card->getSuit() == Card::Heart){
                            hasHeart = true;
                            hearts.append(card->getEffectiveId());
                        }
                    }
                    foreach(const Card *card, p->getEquips()){
                        if (card->getSuit() == Card::Heart){
                            hasHeart = true;
                            hearts.append(card->getEffectiveId());
                        }
                    }
                    if (!hasHeart){
                        continue;
                    }
                    // has heart
                    if (room->askForChoice(p, objectName(), "guiyin_give+cancel", data) == "guiyin_give"){
                        room->fillAG(hearts, p);
                        int card_id = room->askForAG(p, hearts, true, objectName());
                        room->clearAG(p);
                        if (card_id != -1){
                            room->obtainCard(player, card_id);
                            room->broadcastSkillInvoke(objectName(), 5);
                            room->doLightbox(objectName() + "_detach$", 3000);
                            room->detachSkillFromPlayer(player, objectName());
                            room->acquireSkill(player, "qiubang");
                            room->acquireSkill(player, "youshui");
                            break;
                        }
                    }
                }
            }
        }
        else if (triggerEvent == TargetConfirmed){
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.to.contains(player) && use.from && use.card && !use.card->isKindOf("SkillCard")){
                use.from->gainMark("@Oni");
                room->setPlayerMark(player, "OniLv", player->getMark("OniLv") + 1);
                if (use.from != player && !player->hasFlag("guiyin1_sound_used")){
                    room->broadcastSkillInvoke(objectName(), 1);
                    player->setFlags("guiyin1_sound_used");
                }
            }
        }
        else if (triggerEvent == Death){
            DeathStruct death = data.value<DeathStruct>();
            if (player == death.who && death.who->hasSkill(objectName())){
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                p->loseAllMarks("@Oni");
            }
            }

        }
        return false;
    }
};

class GuiyinClear : public DetachEffectSkill
{
public:
    GuiyinClear() : DetachEffectSkill("guiyin")
    {
    }

    void onSkillDetached(Room *room, ServerPlayer *) const
    {
        foreach(ServerPlayer *p, room->getAlivePlayers()){
            p->loseAllMarks("@Oni");
        }
    }
};

class GuiyinDis : public DistanceSkill
{
public:
    GuiyinDis() : DistanceSkill("#guiyin")
    {
    }

    int getCorrect(const Player *from, const Player *) const
    {
        if (from->hasSkill("guiyin") && from->getMark("OniLv") > 0)
            return -2;
        return 0;
    }
};



class Qiubang : public TriggerSkill
{
public:
    Qiubang() : TriggerSkill("qiubang")
    {
        frequency = NotFrequent;
        events << TargetConfirming;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TargetConfirming){
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.to.count() == 1 && use.to.first()->objectName() == player->objectName() && player->isAlive() && use.from && use.from != player && use.card && use.card->isBlack() && !use.card->isKindOf("SkillCard") && room->askForSkillInvoke(player, objectName(), data)){
                room->broadcastSkillInvoke(objectName());
                int used = player->getTag("QiubangUsed").toInt();
                player->drawCards(used - 3 > 3 ? 3 : (used - 3 < 1 ? 1 : used - 3));
                player->setTag("QiubangUsed", player->getTag("QiubangUsed").toInt() + 1);
                const Card *card = use.card;
                if (card->isKindOf("DelayedTrick")){
                    room->moveCardTo(card, use.to.first(), NULL, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, use.to.first()->objectName()), false);
                }
                Card *duel = Sanguosha->cloneCard("duel", card->getSuit(), card->getNumber(), card->getFlags());
                duel->addSubcard(card);
                use.card = duel;
                data.setValue(use);
            }
        }
        return false;
    }
};

class QiubangDis : public DistanceSkill
{
public:
    QiubangDis() : DistanceSkill("#qiubang")
    {
    }

    int getCorrect(const Player *from, const Player *) const
    {
        if (from->hasSkill("qiubang"))
            return -2;
        return 0;
    }
};



YoushuiCard::YoushuiCard()
{
    will_throw = false;
}

bool YoushuiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return to_select->hasFlag("youshui_target") && targets.length() == 0;
}

void YoushuiCard::use(Room *room, ServerPlayer *, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.at(0);
    if (!target)
        return;
    target->setTag("youshuiNum", this->subcardsLength());
    room->obtainCard(target, this, false);
}

class YoushuiVS : public ViewAsSkill
{
public:
    YoushuiVS() : ViewAsSkill("youshui")
    {
        response_pattern = "@@youshui";
    }

    bool viewFilter(const QList<const Card *> &, const Card *) const
    {
        return true;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return NULL;
        YoushuiCard *ysc = new YoushuiCard();
        ysc->addSubcards(cards);
        return ysc;
    }
};

class Youshui : public TriggerSkill
{
public:
    Youshui() : TriggerSkill("youshui")
    {
        frequency = NotFrequent;
        events << EnterDying << QuitDying;
        view_as_skill = new YoushuiVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (triggerEvent == EnterDying){
            DyingStruct dying = data.value<DyingStruct>();
            if (dying.damage && dying.who && dying.damage->card && (dying.damage->card->isKindOf("Slash") || dying.damage->card->isKindOf("Duel"))){
                ServerPlayer *k1 = room->findPlayerBySkillName(objectName());
                if (!k1 || k1->isDead() || k1->objectName() == dying.who->objectName()){
                    return false;
                }
                // give card
                room->setPlayerFlag(dying.who, "youshui_target");
                room->askForUseCard(k1, "@@youshui", "@youshui-use");
                room->setPlayerFlag(dying.who, "-youshui_target");
            }
        }
        else if (triggerEvent == QuitDying){
            DyingStruct dying = data.value<DyingStruct>();
            if (dying.damage && dying.damage->card && (dying.damage->card->isKindOf("Slash") || dying.damage->card->isKindOf("Duel"))){
                if (dying.who->isAlive() && dying.who->getTag("youshuiNum").toInt() > 0){
                    ServerPlayer *k1 = room->findPlayerBySkillName(objectName());
                    if (!k1 || k1->isDead()){
                        return false;
                    }
                    int num = dying.who->getTag("youshuiNum").toInt() * 2;
                    for (int i = 0; i < num; i++){
                        if (!dying.who->isNude()){
                            room->obtainCard(k1, room->askForCardChosen(dying.who, dying.who, "he", objectName()));
                        }
                    }
                    dying.who->setTag("youshuiNum", 0);
                }

            }
        }
        return false;
    }
};

// yuri
class Zuozhan : public TriggerSkill
{
public:
    Zuozhan() : TriggerSkill("zuozhan")
    {
        frequency = NotFrequent;
        events << EventPhaseStart << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart){
            if (player->getPhase() == Player::Start){
                ServerPlayer * yuri = room->findPlayerBySkillName(objectName());
                if (!yuri || (yuri->getHp() > player->getHp() && !player->hasClub("sss")) || !room->askForSkillInvoke(yuri, objectName(), data))
                    return false;
                room->broadcastSkillInvoke(objectName());
                if (player->hasClub("sss"))
                    room->doLightbox(objectName() + "$", 800);
                QStringList choices;
                choices << "1_Zuozhan" << "2_Zuozhan" << "3_Zuozhan" << "4_Zuozhan";
                QString choice1 = room->askForChoice(yuri, "zuozhan1%from:" + player->objectName(), choices.join("+"));
                choices.removeAll(choice1);
                QString choice2 = room->askForChoice(yuri, "zuozhan2%from:" + player->objectName(), choices.join("+"));
                choices.removeAll(choice2);
                QString choice3 = room->askForChoice(yuri, "zuozhan3%from:" + player->objectName(), choices.join("+"));
                choices.removeAll(choice3);
                QString choice4 = choices.first();
                QStringList result;
                result << choice1 << choice2 << choice3 << choice4;

                player->setTag("zuozhan_tag", result);
            }
            else if (player->getPhase() == Player::Finish){
                player->removeTag("zuozhan_tag");
            }
        }
        else if (triggerEvent == EventPhaseChanging){
            QStringList result = player->getTag("zuozhan_tag").toStringList();
            if (result.count() == 0){
                return false;
            }
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive){
                player->removeTag("zuozhan_tag");
                return false;
            }
            QString next = result.first();
            if (next == "1_Zuozhan"){
                change.to = Player::Judge;
            }
            else if (next == "2_Zuozhan"){
                change.to = Player::Draw;
            }
            else if (next == "3_Zuozhan"){
                change.to = Player::Play;
            }
            else if (next == "4_Zuozhan"){
                change.to = Player::Discard;
            }
            else if (next == "0_Zuozhan"){
                change.to = Player::Finish;
            }
            data.setValue(change);
            result.removeAt(0);
            if (result.count() == 0 && next != "0_Zuozhan"){
                result.append("0_Zuozhan");
            }
            player->setTag("zuozhan_tag", result);
        }
        return false;
    }
};



class Nishen : public TriggerSkill
{
public:
    Nishen() : TriggerSkill("nishen")
    {
        frequency = Club;
        club_name = "sss",
        events << EnterDying << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Death){
            DeathStruct death = data.value<DeathStruct>();
            if (death.who->hasClub("sss") && death.who == player){
                room->setTag("no_reward_or_punish", QVariant(death.who->objectName()));
                foreach(ServerPlayer *p, room->getPlayersByClub("sss")){
                    if (p->getLostHp() > 0){
                        if (room->askForChoice(p, objectName(), "draw+recover", data) == "draw"){
                            p->drawCards(2);
                        }
                        else{
                            room->recover(p, RecoverStruct(death.who));
                        }
                    }
                    else{
                        p->drawCards(2);
                    }
                }
            }
        }
        else if (triggerEvent == EnterDying){
            DyingStruct dying = data.value<DyingStruct>();
            if (!dying.who->hasSkill(objectName())){
                ServerPlayer *yuri = room->findPlayerBySkillName(objectName());
                if (!yuri || !yuri->isAlive() || dying.who->hasClub()){
                    return false;
                }
                QStringList used = yuri->getTag("sss_targets").toStringList();
                if (used.contains(dying.who->objectName())){
                    return false;
                }
                if (room->askForSkillInvoke(yuri, objectName(), data)){
                    room->broadcastSkillInvoke(objectName());
                    if (room->askForChoice(dying.who, "nishen", "nishen_accept+cancel", QVariant::fromValue(yuri)) == "nishen_accept"){
                        dying.who->addClub("sss");
                    }
                    else{
                        LogMessage log;
                        log.type = "$refuse_club";
                        log.from = dying.who;
                        log.arg = "sss";
                        room->sendLog(log);
                    }
                    QStringList used = yuri->getTag("sss_targets").toStringList();
                    used.append(dying.who->objectName());
                    yuri->setTag("sss_targets", used);
                }

            }
        }
        return false;
    }
};

class Mengxian : public TriggerSkill
{
public:
    Mengxian() : TriggerSkill("mengxian")
    {
        frequency = Frequent;
        events << EventPhaseStart << FinishJudge;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart){
            if (player->getPhase() == Player::Draw){
                while (player->askForSkillInvoke(objectName(), data)){
                    QString equip = (player->isKongcheng() ? "" : "+equip");
                    QString choice = room->askForChoice(player, objectName(), "basic+trick" + equip);
                    if (choice == "equip"){
                        room->throwCard(room->askForCardChosen(player, player, "h", objectName()), player, player);
                    }
                    JudgeStruct judge;
                    judge.who = player;
                    judge.negative = false;
                    judge.play_animation = false;
                    judge.time_consuming = true;
                    judge.reason = objectName();
                    judge.pattern = choice == "equip" ? "EquipCard" : (choice == "trick" ? "TrickCard" : "BasicCard");
                    room->judge(judge);
                    if ((judge.card->isKindOf("BasicCard") && choice == "basic") || (judge.card->isKindOf("TrickCard") && choice == "trick") || (judge.card->isKindOf("EquipCard") && choice == "equip")){
                        if (judge.card->isKindOf("BasicCard"))
                            room->broadcastSkillInvoke(objectName(), 1);
                        else if (judge.card->isKindOf("TrickCard"))
                            room->broadcastSkillInvoke(objectName(), 2);
                        else
                            room->broadcastSkillInvoke(objectName(), 3);
                        room->doLightbox(objectName() + "$", 500);
                        break;
                    }


                }
            }

        }
        else if (triggerEvent == FinishJudge){
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge->reason == objectName())
                player->obtainCard(judge->card);
                return true;
        }
        return false;
    }
};

class Yuanwang : public TriggerSkill
{
public:
    Yuanwang() : TriggerSkill("yuanwang")
    {
        frequency = Club;
        club_name = "sos",
        events << EventPhaseStart << TurnStart << EventPhaseChanging << TargetConfirmed << CardUsed << TrickCardCanceling << SlashProceed;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        bool is_sos_turn = room->getTag("sos_status").toBool();
        if (triggerEvent == EventPhaseStart){
            ServerPlayer *haruhi = room->findPlayerBySkillName(objectName());
            if (!haruhi || !haruhi->isAlive()){
                return false;
            }
            if (player->getPhase() == Player::Play && player->hasSkill(objectName())){
                // get money!
                        if (qsanRandomBounded(10) < 3){
                    QList<ServerPlayer *> victims;
                    foreach(ServerPlayer *p, room->getAlivePlayers()){
                        if (p->hasClub() && p->getClubName() != "sos" && !p->isKongcheng()){
                            victims << p;
                        }
                    }
                    if (victims.count() > 0){
                            ServerPlayer *target = victims.at(qsanRandomBounded(victims.count()));
                        int id = target->getRandomHandCardId();
                        if (id != -1){
                            //room->doLightbox(objectName() + "_obtain$", 500);
                            LogMessage log;
                            log.type = "$yuanwang_obtain";
                            log.arg = target->getClubName();
                            log.from = target;
                            log.card_str = QString::number(id);
                            room->sendLog(log);
                            room->obtainCard(haruhi, id);
                        }
                    }
                }
                // invite others
                QList<ServerPlayer *> targets = room->getPlayersWithNoClub();
                QList<ServerPlayer *> targets_copy = targets;
                foreach(ServerPlayer *s, targets_copy){
                    if (s->getKingdom() == "real" || s->getKingdom() == "wu"){
                        targets.removeOne(s);
                    }
                }

                if (targets.count() == 0){
                    return false;
                }
                ServerPlayer *target = room->askForPlayerChosen(haruhi, targets, objectName(), "@yuanwang", true);
                if (target){
                    room->broadcastSkillInvoke(objectName());

                    if (room->askForChoice(target, objectName(), objectName() + "_accept+cancel", QVariant::fromValue(haruhi)) == objectName() + "_accept"){
                        target->addClub("sos");
                        if (!target->faceUp()){
                            target->turnOver();
                        }
                    }
                    else{
                        LogMessage log;
                        log.type = "$refuse_club";
                        log.from = target;
                        log.arg = "sos";
                        room->sendLog(log);
                    }
                }
            }
            // wear
            if (player->getPhase() == Player::Play  && is_sos_turn && player->hasClub("sos") && qsanRandomBounded(10) == 0){

                QStringList names;
                foreach(int id, room->getDrawPile()){
                    if (Sanguosha->getCard(id)->isKindOf("Armor")){
                        names << Sanguosha->getCard(id)->objectName();
                    }
                }
                if (names.count() > 0){
                    //room->doLightbox(objectName() + "_wear$", 800);
                    QString name = names.at(qsanRandomBounded(names.count()));
                    int card_id = room->getCardFromPile(name);
                    if (card_id != -1){
                        LogMessage log;
                        log.type = "$yuanwang_wear";
                        log.from = player;
                        log.card_str = QString::number(card_id);
                        room->sendLog(log);
                        room->installEquip(player, name);
                    }

                }
            }
            if (player->getPhase() == Player::RoundStart && player->hasSkill(objectName())){
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    if (!p->hasClub("sos")){
                        p->turnOver();
                    }
                }
                room->doLightbox(objectName() + "$", 500);
                room->setTag("sos_status", QVariant(true));
                is_sos_turn = true;
            }

            // endless august
            if (player->getPhase() == Player::RoundStart && is_sos_turn && player->hasClub("sos")){

                int max_rate = 5;
                foreach(const Card *card, player->getJudgingArea()){
                    if (card->isKindOf("Indulgence")){
                        max_rate = 30;
                    }
                }
                if (qsanRandomBounded(100) < max_rate){
                    QList<QVariant> card_ids;
                    QList<QVariant> equip_ids;
                    QList<QVariant> judge_ids;
                    foreach(const Card *card, player->getHandcards()){
                        if (card->isVirtualCard()){
                            foreach(int id, card->getSubcards()){
                                if (id != -1){
                                    card_ids << QVariant(id);
                                }
                            }
                        }
                        else{
                            int id = card->getEffectiveId();
                            if (id != -1){
                                card_ids << QVariant(id);
                            }
                        }

                    }
                    foreach(const Card *card, player->getEquips()){
                        int id = card->getEffectiveId();
                        if (id != -1){
                            equip_ids << QVariant(id);
                        }
                    }
                    foreach(const Card *card, player->getJudgingArea()){
                        int id = card->getEffectiveId();
                        if (id != -1){
                            judge_ids << QVariant(id);
                        }
                    }
                    QVariant cards, equips, judges, marks, piles;
                    cards.setValue(card_ids);
                    equips.setValue(equip_ids);
                    judges.setValue(judge_ids);
                    QMap<QString, QVariant> marks_map;
                    foreach(QString mark, player->getMarkNames()){
                        marks_map[mark] = QVariant(player->getMark(mark));
                    }
                    marks.setValue(marks_map);
                    QMap<QString, QVariant> piles_map;
                    foreach(QString pile, player->getPileNames()){
                        QVariant temp;
                        QList<QVariant> templst;
                        foreach(int id, player->getPile(pile)){
                            templst << QVariant(id);
                        }
                        temp.setValue(templst);
                        piles_map[pile] = temp;
                    }
                    piles.setValue(piles_map);

                    player->setTag("sos_august_turn", true);
                    player->setTag("sos_august_cards", cards);
                    player->setTag("sos_august_equips", equips);
                    player->setTag("sos_august_judges", judges);
                    player->setTag("sos_august_hp", player->getHp());
                    player->setTag("sos_august_max_hp", player->getMaxHp());
                    player->setTag("sos_august_marks", marks);
                    player->setTag("sos_august_piles", piles);
                }

            }
            if (player->getPhase() == Player::RoundStart && is_sos_turn && player->hasClub("sos")){
                int max_rate = 5;
                foreach(const Card *card, haruhi->getJudgingArea()){
                    if (card->isKindOf("SupplyShortage")){
                        max_rate = 30;
                    }
                }
                if (qsanRandomBounded(100) < max_rate){
                    LogMessage log;
                    log.type = "$yuanwang_closed_space";
                    room->sendLog(log);
                    room->doAura(haruhi, "closedSpace");
                }
            }
            if (player->getPhase() == Player::Finish && room->getAura() == "closedSpace"){
                room->clearAura();
            }

        }
        else if (triggerEvent == TurnStart && player->hasSkill(objectName())){
            if (room->getTag("sos_status").toBool()){
                room->setTag("sos_status", QVariant(false));
                is_sos_turn = false;
                foreach(ServerPlayer *p, room->getPlayersByClub("sos")){
                p->turnOver();
                }
            }
        }
        else if (triggerEvent == EventPhaseChanging && is_sos_turn && player->hasClub("sos")){
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::Draw && change.to != Player::Play && change.to != Player::NotActive && is_sos_turn && player->hasClub("sos") && qsanRandomBounded(100) < 3){
                change.to = qsanRandomBounded(2) == 0 ? Player::Draw : Player::Play;
                if (change.to == Player::Play && player->getHandcardNum() < 3 && qsanRandomBounded(5) > 1){
                    change.to = Player::Draw;
                }
                data.setValue(change);
                LogMessage log;
                log.type = change.to == Player::Draw ? "$yuanwang_add_phase_draw" : "$yuanwang_add_phase_play";
                log.from = player;
                room->sendLog(log);
                player->insertPhase(change.to);
            }
            if (change.to == Player::NotActive && player->isAlive() && player->getTag("sos_august_turn").toBool()){
                player->setTag("sos_august_turn", false);

                LogMessage log;
                log.type = "$yuanwang_august";
                log.from = player;
                room->sendLog(log);

                QList<int> handcard_ids;
                foreach(QVariant q, player->getTag("sos_august_cards").toList()){
                    handcard_ids << q.toInt();
                }
                QList<int> equips_ids;
                foreach(QVariant q, player->getTag("sos_august_equips").toList()){
                    equips_ids << q.toInt();
                }
                QList<int> judges_ids;
                foreach(QVariant q, player->getTag("sos_august_judges").toList()){
                    judges_ids << q.toInt();
                }
                QMap<QString, QVariant> marks_raw_map = player->getTag("sos_august_marks").toMap();
                QMap<QString, int> marks_map;
                foreach(QString key, marks_raw_map.keys()){
                    marks_map[key] = marks_raw_map[key].toInt();
                }
                QMap<QString, QVariant> piles_raw_map = player->getTag("sos_august_piles").toMap();
                QMap<QString, QList<int> > piles_map;
                foreach(QString key, piles_raw_map.keys()){
                    QList<int> temp;
                    foreach(QVariant v, piles_raw_map[key].toList()){
                        temp << v.toInt();
                    }
                    piles_map[key] = temp;
                }
                int hp = player->getTag("sos_august_hp").toInt();
                int maxhp = player->getTag("sos_august_max_hp").toInt();



                player->removeTag("sos_august_cards");
                player->removeTag("sos_august_equips");
                player->removeTag("sos_august_judges");
                player->removeTag("sos_august_hp");
                player->removeTag("sos_august_max_hp");
                player->removeTag("sos_august_marks");
                player->removeTag("sos_august_piles");

                QList<int> ids;

                foreach(const Card *card, player->getHandcards()){
                    if (!handcard_ids.contains(card->getEffectiveId()) && !equips_ids.contains(card->getEffectiveId()) && !judges_ids.contains(card->getEffectiveId())){
                        ids << card->getEffectiveId();
                    }
                    if (handcard_ids.contains(card->getEffectiveId())){
                        handcard_ids.removeOne(card->getEffectiveId());
                    }
                }
                room->moveCardsAtomic(CardsMoveStruct(ids, NULL, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, QString())), true);
                room->getThread()->delay(200);

                QList<int> eids;
                foreach(const Card *card, player->getEquips()){
                    if (!handcard_ids.contains(card->getEffectiveId()) && !equips_ids.contains(card->getEffectiveId()) && !judges_ids.contains(card->getEffectiveId())){
                        eids << card->getEffectiveId();
                    }
                    if (equips_ids.contains(card->getEffectiveId())){
                        equips_ids.removeOne(card->getEffectiveId());
                    }
                }
                room->moveCardsAtomic(CardsMoveStruct(eids, player, NULL, Player::PlaceEquip, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, QString())), true);
                room->getThread()->delay(200);

                QList<int> jids;
                foreach(const Card *card, player->getJudgingArea()){
                    if (!handcard_ids.contains(card->getEffectiveId()) && !equips_ids.contains(card->getEffectiveId()) && !judges_ids.contains(card->getEffectiveId())){
                        jids << card->getEffectiveId();
                    }
                    if (judges_ids.contains(card->getEffectiveId())){
                        judges_ids.removeOne(card->getEffectiveId());
                    }
                }
                room->moveCardsAtomic(CardsMoveStruct(jids, player, NULL, Player::PlaceDelayedTrick, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, QString())), true);

                room->getThread()->delay(200);
                foreach(QString mark, player->getMarkNames()){
                    if (!marks_map.keys().contains(mark))
                        room->setPlayerMark(player, mark, 0);
                }
                foreach(QString mark, marks_map.keys()){
                    room->setPlayerMark(player, mark, marks_map[mark]);
                }

                room->getThread()->delay(100);
                room->moveCardsAtomic(CardsMoveStruct(handcard_ids, player, Player::PlaceHand, CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, QString())), true);
                room->getThread()->delay(200);
                room->moveCardsAtomic(CardsMoveStruct(equips_ids, player, Player::PlaceEquip, CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, QString())), true);
                room->getThread()->delay(200);
                room->moveCardsAtomic(CardsMoveStruct(judges_ids, player, Player::PlaceDelayedTrick, CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, QString())), true);

                foreach(QString pile, player->getPileNames()){
                    if (!piles_map.keys().contains(pile)){
                        QList<ServerPlayer *> opens;
                        foreach(ServerPlayer *p, room->getAlivePlayers()){
                            if (player->pileOpen(pile, p->objectName())){
                                opens << p;
                            }
                        }
                        room->getThread()->delay(200);
                        player->clearOnePrivatePile(pile);
                        room->getThread()->delay(200);
                        player->addToPile(pile, piles_map[pile], opens.count() > 1, opens);
                    }
                    else{
                        room->getThread()->delay(200);
                        player->clearOnePrivatePile(pile);
                    }

                }

                foreach(QString pile, piles_map.keys()){
                    room->getThread()->delay(200);
                    if (player->getPile(pile).count() == 0)
                        player->addToPile(pile, piles_map[pile]);
                }


                room->setPlayerProperty(player, "maxhp", maxhp);
                room->setPlayerProperty(player, "hp", hp);
                player->gainAnExtraTurn();
            }
        }
        else if (triggerEvent == TargetConfirmed && is_sos_turn){
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.from && use.from == player && use.from->hasClub("sos") && use.card && use.card->isBlack() && use.to.count() == 1 && use.from != use.to.first() && qsanRandomBounded(20) == 0){
                ServerPlayer *target = use.to.first();
                if (target->hasClub("sos")){
                    return false;
                }
                LogMessage log;
                log.type =  "$yuanwang_mikuru_beam";
                log.from = use.from;
                log.to = use.to;
                room->sendLog(log);
                room->damage(DamageStruct(objectName(), use.from, use.to.first(), 1, DamageStruct::Thunder));
            }

        }
        // card back
        else if (triggerEvent == CardUsed && is_sos_turn){
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.from && use.from->hasClub("sos") && use.card && !use.card->isVirtualCard() && (use.card->isKindOf("BasicCard") || use.card->isKindOf("TrickCard")) && qsanRandomBounded(100) < 6){
                LogMessage log;
                log.type = "$yuanwang_card_back";
                log.from = use.from;
                log.card_str = QString::number(use.card->getEffectiveId());
                room->sendLog(log);
                room->obtainCard(use.from, use.card);
            }

        }
        else if (triggerEvent == TrickCardCanceling && is_sos_turn && room->getAura() == "closedSpace") {
            LogMessage log;
            log.type = "$yuanwang_closed_space_effect";
            room->sendLog(log);
            return true;
        }
        else if (triggerEvent == SlashProceed && is_sos_turn && room->getAura() == "closedSpace") {
            SlashEffectStruct effect = data.value<SlashEffectStruct>();
            LogMessage log;
            log.type = "$yuanwang_closed_space_effect";
            room->sendLog(log);
            room->slashResult(effect, NULL);
            return true;
        }
        return false;
    }
};


MojuCard::MojuCard()
{
}

bool MojuCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.length() == 0;
}

void MojuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.at(0);
    if (!target)
        return;

    int card_id = this->getSubcards().at(0);
    if (card_id == -1)
        return;
    Card *card = Sanguosha->getCard(card_id);
    room->moveCardTo(card, target, Player::PlaceEquip);
    QList<ServerPlayer *> players;
    foreach(ServerPlayer *p, room->getAlivePlayers()){
        foreach(const Card *c, p->getEquips()){
            if (c->getSuit() == card->getSuit()){
                players.append(p);
                break;
            }
        }
        if (!players.contains(p)){
            foreach(const Card *c, p->getJudgingArea()){
                if (c->getSuit() == card->getSuit()){
                    players.append(p);
                    break;
                }
            }
        }
    }
    if (players.count() == 0){
        return;
    }
    ServerPlayer *from = room->askForPlayerChosen(source, players, "moju", "@moju-from:::" + card->getSuitString());
    QList<int> disabled;
    foreach(const Card *c, from->getEquips()){
        if (c->getSuit() != card->getSuit()){
            disabled.append(c->getEffectiveId());
        }
    }
    foreach(const Card *c, from->getJudgingArea()){
        if (c->getSuit() != card->getSuit()){
            disabled.append(c->getEffectiveId());
        }
    }
    int from_id = room->askForCardChosen(source, from, "ej", objectName(), false, Card::MethodNone, disabled);
    Player::Place place = room->getCardPlace(from_id);
    const Card *from_card = Sanguosha->getCard(from_id);
    QList<ServerPlayer *> tos;

    int equip_index = -1;
    if (place == Player::PlaceEquip){
        const EquipCard *equip = qobject_cast<const EquipCard *>(from_card->getRealCard());
        equip_index = static_cast<int>(equip->location());
    }
    foreach(ServerPlayer *p, room->getOtherPlayers(from)){
        if (equip_index != -1) {
            if (p->getEquip(equip_index) == NULL)
                tos << p;
        }
        else {
            if (!source->isProhibited(p, from_card) && !p->containsTrick(from_card->objectName()))
                tos << p;
        }
    }
    ServerPlayer *to = room->askForPlayerChosen(source, tos, "moju_to", "@moju-to:::" + from_card->objectName());
    if (to)
        room->moveCardTo(from_card, from, to, place,
        CardMoveReason(CardMoveReason::S_REASON_TRANSFER,
        source->objectName(), "moju", QString()));
}

class Moju : public OneCardViewAsSkill
{
public:
    Moju() : OneCardViewAsSkill("moju"){

    }

    bool viewFilter(const Card *card) const
    {
        return card->isKindOf("EquipCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        MojuCard *mjc = new MojuCard();
        mjc->addSubcard(originalCard);
        mjc->setSkillName("moju");
        return mjc;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MojuCard");
    }
};


class Jiejie : public TriggerSkill
{
public:
    Jiejie() : TriggerSkill("hayate_jiejie")
    {
        frequency = NotFrequent;
        events << DamageInflicted;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == DamageInflicted){
            DamageStruct damage = data.value<DamageStruct>();
            ServerPlayer *hakaze = room->findPlayerBySkillName(objectName());

            if (damage.to && hakaze && !room->getCurrent()->hasFlag("hayate_jiejie_used") && room->askForSkillInvoke(damage.to, objectName(), data)){
                LogMessage log;
                log.type = "$hayate_jiejie_asked";
                log.from = damage.to;
                room->sendLog(log);
                room->broadcastSkillInvoke(objectName(), 1);
                room->getCurrent()->setFlags("hayate_jiejie_used");
                QList<int> card_ids = room->getNCards(damage.to->getLostHp() + 1, false);
                room->fillAG(card_ids, hakaze);
                int id = room->askForAG(hakaze, card_ids, false, objectName());
                room->clearAG(hakaze);
                room->showCard(hakaze, id);
                room->obtainCard(hakaze, id);
                if (id != -1){
                    const Card *card = Sanguosha->getCard(id);
                    foreach(const Card* c, damage.to->getEquips()){
                        if (card->getColor() == c->getColor()){
                            damage.damage -= 1;
                            data.setValue(damage);
                            room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) + 1);
                            room->doLightbox(objectName() + "$", 800);
                            room->setEmotion(damage.to, "shield");
                            return false;
                        }
                    }

                }
            }
        }

        return false;
    }
};


class Qifen : public TriggerSkill
{
public:
    Qifen() : TriggerSkill("qifen")
    {
        frequency = NotFrequent;
        events << EventPhaseStart;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart){
            if (player->getPhase() == Player::Finish && room->askForSkillInvoke(player, objectName(), data)){
                foreach(ServerPlayer *p, room->getAlivePlayers()){
                    p->drawCards(1);
                }
                room->broadcastSkillInvoke(objectName());
                if (player->getLostHp() == 0)
                    return false;
                room->doLightbox(objectName() + "$", 800);
                foreach(ServerPlayer* p, room->getOtherPlayers(player)){
                    if (!p->isNude()){
                        room->obtainCard(player, room->askForCardChosen(player, p, "he", objectName()));
                    }
                }
            }
        }
        return false;
    }
};

class Mishi : public TriggerSkill
{
public:
    Mishi() : TriggerSkill("mishi")
    {
        frequency = NotFrequent;
        events << Dying;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Dying){
            DyingStruct dying = data.value<DyingStruct>();
            ServerPlayer *iori = room->findPlayerBySkillName(objectName());
            if (!dying.who || dying.who->isDead() || !iori || room->getCurrent()->hasFlag("mishi_used") || !room->askForSkillInvoke(iori, objectName(), data))
                return false;
            room->setPlayerFlag(room->getCurrent(), "mishi_used");
            room->broadcastSkillInvoke(objectName());
            room->doLightbox(objectName() + "$", 800);
            room->showAllCards(iori);
            room->loseHp(iori);
            room->loseHp(dying.who);
            if (dying.who->isAlive() && !dying.who->isNude()){
                room->obtainCard(iori, room->askForCardChosen(iori, dying.who, "he", objectName()));
            }

        }
        return false;
    }
};


ZhufuCard::ZhufuCard()
{
}

bool ZhufuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.length() < 4 && to_select != Self;
}

bool ZhufuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() > 1 && targets.length() < 5;
}

void ZhufuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    source->loseAllMarks("@hayate_zhufu");
    int i = 0;
    while (!source->isKongcheng()){
        room->obtainCard(targets.at(i), room->askForCardChosen(targets.at(i), source, "h", "hayate_zhufu", true));
        i = (i == targets.count() - 1) ? 0 : i + 1;
    }
}

class ZhufuVS : public ZeroCardViewAsSkill
{
public:
    ZhufuVS() : ZeroCardViewAsSkill("hayate_zhufu")
    {
        response_pattern = "@@hayate_zhufu";
    }

    const Card *viewAs() const
    {
        return new ZhufuCard();
    }
};

class Zhufu : public TriggerSkill
{
public:
    Zhufu() : TriggerSkill("hayate_zhufu")
    {
        frequency = Limited;
        limit_mark = "@hayate_zhufu";
        events << EventPhaseStart;
        view_as_skill = new ZhufuVS;
    }


    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Draw && player->getMark("@hayate_zhufu") > 0){
            room->askForUseCard(player, "@@hayate_zhufu", "@hayate_zhufu-use");
        }

        return false;
    }
};

class Vector : public TriggerSkill
{
public:
    Vector() : TriggerSkill("vector")
    {
        frequency = NotFrequent;
        events << CardEffect;
    }


    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardEffectStruct effect = data.value<CardEffectStruct>();
        QList<ServerPlayer *> targets;
        foreach(ServerPlayer *p, room->getOtherPlayers(player)){
            if (!effect.from || !Sanguosha->isProhibited(effect.from, p, effect.card))
                targets.append(p);
        }
        if (targets.count() == 0 || player->isKongcheng() || !effect.to || effect.to != player || !effect.card || !player->hasSkill(objectName()) || !room->askForCard(player, ".", QString("@vector-discard:%1::%2").arg(effect.from != NULL ? effect.from->objectName() : "No Source", effect.card->objectName()), data, objectName()))
            return false;
        effect.to = room->askForPlayerChosen(player, targets, objectName(), QString("@vector-select:%1::%2").arg(effect.from != NULL ? effect.from->objectName() : "No Source", effect.card->objectName()));
        room->broadcastSkillInvoke(objectName());
        room->doLightbox(objectName() + "$", 800);
        data.setValue(effect);

        return false;
    }
};


class Juhe : public TriggerSkill
{
public:
    Juhe() : TriggerSkill("juhe")
    {
        frequency = NotFrequent;
        events << EventPhaseChanging << Damaged;
    }


    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging){
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::Play && player->hasSkill(objectName()) && room->askForSkillInvoke(player, objectName(), data)){
                if (room->askForChoice(player, objectName(), player->getMark("@xuli") > 0 ? "juhe_skip+juhe_lose" : "juhe_skip", data) == "juhe_skip"){
                    room->broadcastSkillInvoke(objectName());
                    player->skip(Player::Play);
                    player->gainMark("@xuli");
                }
                else{
                    room->broadcastSkillInvoke(objectName());
                    room->damage(DamageStruct(objectName(), player, room->askForPlayerChosen(player, room->getAlivePlayers(), objectName()), player->getMark("@xuli")));
                    player->loseAllMarks("@xuli");
                }
            }
        }
        else if (triggerEvent == Damaged){
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.to && damage.to->isAlive() && damage.to->hasSkill(objectName()) && damage.card && !damage.card->isVirtualCard() && damage.damage > 0){
                damage.to->loseAllMarks("@xuli");
            }
        }

        return false;
    }
};


class Qianggang : public TriggerSkill
{
public:
    Qianggang() : TriggerSkill("qianggang")
    {
        frequency = NotFrequent;
        events << CardsMoveOneTime;
    }


    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == CardsMoveOneTime){
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from || !move.from_places.contains(Player::DrawPile) || !move.to || move.to == player || !player->hasSkill(objectName()) || player->isDead() || move.to_place != Player::PlaceHand || !room->askForSkillInvoke(player, objectName(), data)){
                return false;
            }
            
            QStringList choices;
            for (int i = 0; i <= (player->getHandcardNum() > 2 ? 2 : player->getHandcardNum()); i++){
                choices.append(QString::number(i));
            }
            int dis_num = room->askForChoice(player, objectName(), choices.join("+"), data).toInt();
            
            QList<Card::Color> colors;

            QList<int> ids = room->getNCards(3 - dis_num, false);
            if (dis_num > 0){
                for (int i = 0; i < dis_num; i++){
                    int id = room->askForCardChosen(player, player, "h", objectName());
                    if (id == -1){
                        return false;
                    }
                    ids.append(id);
                    room->throwCard(id, player);
                    Card::Color color = Sanguosha->getCard(id)->getColor();
                    if (!colors.contains(color)){
                        colors.append(color);
                    }
                }
            }
            
            room->fillAG(ids);
            room->getThread()->delay(1000);
            room->clearAG();
            foreach(int id, ids){
                room->showCard(player, id);
                Card::Color color = Sanguosha->getCard(id)->getColor();
                if (!colors.contains(color)){
                    colors.append(color);
                }
            }
            if (colors.count() == 1){
                room->broadcastSkillInvoke(objectName());
                room->moveCardsAtomic(CardsMoveStruct(move.card_ids, player, Player::PlaceHand, CardMoveReason()), false);
            }
        }

        return false;
    }
};


HayatePackage::HayatePackage()
    : Package("hayate")
{
    General *hei = new General(this, "hayate_hei", "science", 3);
    skills << new Jiesha << new Gaokang << new Qiubang << new QiubangDis << new Youshui << new LonelinessInvalidity;
    hei->addSkill(new Yingdi);
    hei->addSkill(new YingdiClear);
    related_skills.insert("yingdi", "#yingdi-clear");
    hei->addSkill(new Diansuo);

    General * diarmuid = new General(this, "hayate_diarmuid", "magic", 4);
    diarmuid->addSkill(new Pomo);
    diarmuid->addSkill(new Bimie);

    General *tsukushi = new General(this, "hayate_tsukushi", "real", 3, false);
    tsukushi->addSkill(new Gangqu);
    tsukushi->addSkill(new GangquClear);
    related_skills.insert("gangqu", "#gangqu-clear");
    tsukushi->addSkill(new Tiaojiao);

    General *sheryl = new General(this, "hayate_sheryl", "diva", 3, false);
    sheryl->addSkill(new Yaojing);
    sheryl->addSkill(new YaojingMaxCards);
    sheryl->addSkill(new YaojingClear);
    sheryl->addSkill(new Gongming);
    related_skills.insert("yaojing", "#yaojing");
    related_skills.insert("yaojing", "#yaojing-clear");

    General *sugisaki = new General(this, "hayate_sugisaki", "real", 3);
    sugisaki->addSkill(new Kurimu);
    sugisaki->addSkill(new Minatsu);
    sugisaki->addSkill(new Chizuru);
    sugisaki->addSkill(new Mafuyu);
    sugisaki->addSkill(new MafuyuClear);
    related_skills.insert("mafuyu", "#mafuyu-clear");
    sugisaki->addSkill(new Haremu);
    sugisaki->addSkill(new HaremuMaxCards);
    related_skills.insert("haremu", "#haremu");

    General *eugeo = new General(this, "hayate_eugeo", "science", 3);
    eugeo->addSkill(new Rennai);
    eugeo->addSkill(new RennaiClear);
    related_skills.insert("rennai", "#rennai-clear");
    eugeo->addSkill(new Zhanfang);
    eugeo->addSkill(new Huajian);

    General *k1 = new General(this, "hayate_k1", "real", 4);
    k1->addSkill(new Guiyin);
    k1->addSkill(new GuiyinDis);
    k1->addSkill(new GuiyinClear);
    related_skills.insert("guiyin", "#guiyin");
    related_skills.insert("guiyin", "#guiyin-clear");
    related_skills.insert("qiubang", "#qiubang");

    General *yuri = new General(this, "hayate_yuri", "real", 3, false);
    yuri->addSkill(new Zuozhan);
    yuri->addSkill(new Nishen);

    General *haruhi = new General(this, "hayate_haruhi", "real", 3, false);
    haruhi->addSkill(new Mengxian);
    haruhi->addSkill(new Yuanwang);

    General *hakaze = new General(this, "hayate_hakaze", "magic", 3, false);
    hakaze->addSkill(new Moju);
    hakaze->addSkill(new Jiejie);

    General *nagase = new General(this, "hayate_nagase", "real", 3, false);
    nagase->addSkill(new Qifen);
    nagase->addSkill(new Mishi);
    nagase->addSkill(new Zhufu);

    General *acc = new General(this, "hayate_acc", "science", 3);
    acc->addSkill(new Vector);
    acc->addSkill(new Juhe);

    General *yumi = new General(this, "hayate_yumi", "real", 4, false, true);
    yumi->addSkill(new Qianggang);

    addMetaObject<TiaojiaoCard>();
    addMetaObject<HaremuCard>();
    addMetaObject<YoushuiCard>();
    addMetaObject<MojuCard>();
    addMetaObject<ZhufuCard>();
}

ADD_PACKAGE(Hayate)
