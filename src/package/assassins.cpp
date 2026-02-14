#include "assassins.h"
//#include "skill.h"
#include "standard.h"
//#include "clientplayer.h"
#include "engine.h"
//#include "util.h"
#include "room.h"
#include "roomthread.h"

class Moukui2 : public TriggerSkill
{
public:
    Moukui2() : TriggerSkill("moukui")
    {
        events << TargetSpecified << CardOffset;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
        if(event==TargetSpecified){
            CardUseStruct use = data.value<CardUseStruct>();
            if (owner->isAlive()&&use.card->isKindOf("Slash"))
				return target==owner&&owner->hasSkill(objectName());
        } else if (event == CardOffset) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
            if (effect.card->hasFlag("moukuiBf"+effect.to->objectName())&&effect.to->canDiscard(effect.from, "he"))
				return target==owner&&owner->isAlive()&&owner->hasSkill(objectName());
		}
		return false;
    }

    bool trigger(TriggerEvent event,Room*room,ServerPlayer*player,QVariant&data,ServerPlayer*) const
    {
        if (event == TargetSpecified) {
            CardUseStruct use = data.value<CardUseStruct>();
            foreach (ServerPlayer *p, use.to) {
                if (player->askForSkillInvoke(this, p)) {
                    QString choice = "draw";
                    if (player->canDiscard(p, "he"))
                        choice = room->askForChoice(player, objectName(), "draw+discard", QVariant::fromValue(p));
                    int n = 1;
					if(player->getGeneralName().contains("fuwan"))
						n += 3;
					room->setCardFlag(use.card,"moukuiBf"+p->objectName());
					if (choice == "draw") {
                        room->broadcastSkillInvoke(objectName(), n);
                        player->drawCards(1, objectName());
                    } else {
						n++;
                        room->setTag("MoukuiDiscard", data);
                        room->broadcastSkillInvoke(objectName(), n);
                        int disc = room->askForCardChosen(player, p, "he", objectName(), false, Card::MethodDiscard);
                        room->throwCard(disc, objectName(), p, player);
                    }
                }
            }
        } else if (event == CardOffset) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
			int disc = room->askForCardChosen(effect.to, effect.from, "he", objectName(), false, Card::MethodDiscard);
			room->broadcastSkillInvoke(objectName(), 3);
			room->throwCard(disc, objectName(), effect.from, effect.to);
        }
        return false;
    }
};

class Moukui : public TriggerSkill
{
public:
    Moukui() : TriggerSkill("moukui")
    {
        events << TargetSpecified << CardOffset << CardFinished;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TargetSpecified && TriggerSkill::triggerable(player)) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash")) return false;
            foreach (ServerPlayer *p, use.to) {
                if (player->askForSkillInvoke(this, QVariant::fromValue(p))) {
                    QString choice;
                    if (!player->canDiscard(p, "he"))
                        choice = "draw";
                    else
                        choice = room->askForChoice(player, objectName(), "draw+discard", QVariant::fromValue(p));
                    int n = 1;
					if(player->getGeneralName().contains("fuwan"))
						n += 3;
					if (choice == "draw") {
                        room->broadcastSkillInvoke(objectName(), n);
                        player->drawCards(1, objectName());
                    } else {
						n++;
                        room->broadcastSkillInvoke(objectName(), n);
                        room->setTag("MoukuiDiscard", data);
                        int disc = room->askForCardChosen(player, p, "he", objectName(), false, Card::MethodDiscard);
                        room->removeTag("MoukuiDiscard");
                        room->throwCard(disc, p, player);
                    }
                    room->addPlayerMark(p, objectName() + use.card->toString());
                }
            }
        } else if (triggerEvent == CardOffset) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
            if (!effect.card->isKindOf("Slash")||effect.to->isDead()||effect.to->getMark(objectName()+effect.card->toString())<1)
                return false;
            if (!effect.from->isAlive() || !effect.to->isAlive() || !effect.to->canDiscard(effect.from, "he"))
                return false;
            int disc = room->askForCardChosen(effect.to, effect.from, "he", objectName(), false, Card::MethodDiscard);
            room->broadcastSkillInvoke(objectName(), 3);
            room->throwCard(disc, effect.from, effect.to);
            room->removePlayerMark(effect.to, objectName() + effect.card->toString());
        } else if (triggerEvent == CardFinished) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash"))
                return false;
            foreach(ServerPlayer *p, room->getAllPlayers())
                room->setPlayerMark(p, objectName() + use.card->toString(), 0);
        }

        return false;
    }
};

class Tianming2 : public TriggerSkill
{
public:
    Tianming2() : TriggerSkill("tianming")
    {
        events << TargetConfirming;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
        if(event==TargetConfirming){
            CardUseStruct use = data.value<CardUseStruct>();
            if (owner->isAlive()&&use.card->isKindOf("Slash"))
				return target==owner&&owner->hasSkill(objectName());
        }
		return false;
    }

    bool trigger(TriggerEvent,Room*room,ServerPlayer*player,QVariant&data,ServerPlayer*) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (player->askForSkillInvoke(objectName()+"$1")) {
            room->askForDiscard(player, objectName(), 2, 2, false, true);
            player->drawCards(2, objectName());

            int max = -1000;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->getHp() > max) max = p->getHp();
            }
            if (player->getHp() == max)
                return false;

            QList<ServerPlayer *> maxs;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->getHp() == max) maxs << p;
                if (maxs.size() > 1) return false;
            }
            ServerPlayer *mosthp = maxs.first();
            if (mosthp->askForSkillInvoke(objectName(), data,false)) {
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), mosthp->objectName());
                int index = 2;
                if (mosthp->isFemale()) index = 3;
                room->broadcastSkillInvoke(objectName(), index);
                room->askForDiscard(mosthp, objectName(), 2, 2, false, true);
                mosthp->drawCards(2, objectName());
            }
        }
        return false;
    }
};

class Tianming : public TriggerSkill
{
public:
    Tianming() : TriggerSkill("tianming")
    {
        events << TargetConfirming;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Slash") && player->askForSkillInvoke(objectName()+"$1")) {
            room->askForDiscard(player, objectName(), 2, 2, false, true);
            player->drawCards(2, objectName());

            int max = -1000;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->getHp() > max)
                    max = p->getHp();
            }
            if (player->getHp() == max)
                return false;

            QList<ServerPlayer *> maxs;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->getHp() == max)
                    maxs << p;
                if (maxs.size() > 1)
                    return false;
            }
            ServerPlayer *mosthp = maxs.first();
            if (room->askForSkillInvoke(mosthp, objectName(), false)) {
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), mosthp->objectName());
                int index = 2;
                if (mosthp->isFemale())
                    index = 3;
                room->broadcastSkillInvoke(objectName(), index);
                room->askForDiscard(mosthp, objectName(), 2, 2, false, true);
                mosthp->drawCards(2, objectName());
            }
        }

        return false;
    }
};

MizhaoCard::MizhaoCard()
{
    mute = true;
}

bool MizhaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void MizhaoCard::onEffect(CardEffectStruct &effect) const
{
    CardMoveReason r(CardMoveReason::S_REASON_GIVE, effect.from->objectName());
    Room *room = effect.from->getRoom();
    DummyCard *handcards = effect.from->wholeHandCards();
    room->obtainCard(effect.to, handcards, r, false);
    if (effect.to->isKongcheng()) return;

    int index = (effect.to->getGeneralName().contains("liubei") || effect.to->getGeneral2Name().contains("liubei")) ? 2 : 1;
    if (effect.from->getGeneralName().startsWith("new_") || effect.from->getGeneral2Name().startsWith("new_"))
        index = qrand() % 2 + 3;
    room->broadcastSkillInvoke("mizhao", index);

    QList<ServerPlayer *> targets;
    foreach (ServerPlayer *p, room->getOtherPlayers(effect.to)) {
        if (effect.to->canPindian(p))
            targets << p;
    }

    if (!targets.isEmpty()) {
        ServerPlayer *target = room->askForPlayerChosen(effect.from, targets, "mizhao", "@mizhao-pindian:" + effect.to->objectName());
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, effect.to->objectName(), target->objectName());
        target->setFlags("MizhaoPindianTarget");
        int i = effect.to->pindianInt(target, "mizhao");
        target->setFlags("-MizhaoPindianTarget");
        if (i == 0 || i == -2) return;
        ServerPlayer *winner = target;
        ServerPlayer *loser = effect.to;
        if (i == 1) {
            winner = effect.to;
            loser = target;
        }
        if (winner->isAlive()) {
            Slash *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName("_mizhao");
            if (winner->canSlash(loser, slash, false))
                room->useCard(CardUseStruct(slash, winner, loser));
			slash->deleteLater();
        }
    }
}

class Mizhao : public ZeroCardViewAsSkill
{
public:
    Mizhao() : ZeroCardViewAsSkill("mizhao")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->isKongcheng() && !player->hasUsed("MizhaoCard");
    }

    const Card *viewAs() const
    {
        return new MizhaoCard;
    }
};

class Jieyuan2 : public TriggerSkill
{
public:
    Jieyuan2() : TriggerSkill("jieyuan")
    {
        events << DamageCaused << DamageInflicted;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
        if(event==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
            if (damage.to->isAlive()&&damage.to!=owner&&(damage.to->getHp()>=owner->getHp()||owner->getMark("jieyuan_rebel-Keep")>0)){
				QString flag = "h";
				if (owner->getMark("jieyuan_renegade-Keep")>0)
					flag = "he";
				if(owner->isAlive()&&owner->canDiscard(owner, flag))
					return target==owner&&owner->hasSkill(objectName());
			}
        }else{
			DamageStruct damage = data.value<DamageStruct>();
            if (damage.from && damage.from->isAlive() && damage.from != owner) {
                if (damage.from->getHp() >= owner->getHp() || owner->getMark("jieyuan_loyalist-Keep") > 0) {
                    QString flag = "h";
                    if (owner->getMark("jieyuan_renegade-Keep")>0)
                        flag = "he";
                    if (owner->isAlive()&&owner->canDiscard(owner, flag))
						return target==owner&&owner->hasSkill(objectName());
				}
			}
		}
		return false;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (triggerEvent == DamageCaused) {
            QString pattern = ".black";
			QString prompt = "@jieyuan-increase:" + damage.to->objectName();
			if (player->getMark("jieyuan_renegade-Keep") > 0) {
				prompt = "@jieyuan-increase2:" + damage.to->objectName();
				pattern = "..";
			}
			if (room->askForCard(player, pattern, prompt, data, objectName())) {
				player->peiyin(this, 1);
				player->damageRevises(data,1);
            }
        } else if (triggerEvent == DamageInflicted) {
			QString prompt = "@jieyuan-decrease:" + damage.from->objectName();
            QString pattern = ".red";
			if (player->getMark("jieyuan_renegade-Keep") > 0) {
				prompt = "@jieyuan-decrease2:" + damage.from->objectName();
				pattern = "..";
			}
			if (room->askForCard(player, pattern, prompt, data, objectName())) {
				player->peiyin(this, 2);
				return player->damageRevises(data,-1);
            }
        }
        return false;
    }
};

class Jieyuan : public TriggerSkill
{
public:
    Jieyuan() : TriggerSkill("jieyuan")
    {
        events << DamageCaused << DamageInflicted;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (triggerEvent == DamageCaused) {
            if (damage.to && damage.to->isAlive() && damage.to != player) {
                if (damage.to->getHp() >= player->getHp() || player->getMark("jieyuan_rebel-Keep") > 0) {
                    QString pattern = ".black";
                    QString prompt = "@jieyuan-increase:" + damage.to->objectName();
                    QString flag = "h";
                    if (player->getMark("jieyuan_renegade-Keep") > 0) {
                        pattern = "..";
                        prompt = "@jieyuan-increase2:" + damage.to->objectName();
                        flag = "he";
                    }
                    if (player->canDiscard(player, flag)&&room->askForCard(player, pattern, prompt, data, objectName())) {
                        player->peiyin(this, 1);
                        player->damageRevises(data,1);
                    }
                }
            }
        } else if (triggerEvent == DamageInflicted) {
            if (damage.from && damage.from->isAlive() && damage.from != player) {
                if (damage.from->getHp() >= player->getHp() || player->getMark("jieyuan_loyalist-Keep") > 0) {
                    QString pattern = ".red";
                    QString prompt = "@jieyuan-decrease:" + damage.from->objectName();
                    QString flag = "h";
                    if (player->getMark("jieyuan_renegade-Keep") > 0) {
                        pattern = "..";
                        prompt = "@jieyuan-decrease2:" + damage.from->objectName();
                        flag = "he";
                    }
                    if (player->canDiscard(player, flag)&&room->askForCard(player, pattern, prompt, data, objectName())) {
                        player->peiyin(this, 2);
                        return player->damageRevises(data,-1);
                    }
                }
            }
        }
        return false;
    }
};

class Fenxin2 : public TriggerSkill
{
public:
    Fenxin2() : TriggerSkill("fenxin")
    {
        events << BeforeGameOverJudge;
        frequency = Limited;
        limit_mark = "@burnheart";
    }

    bool triggerable(ServerPlayer*,Room*room,TriggerEvent,ServerPlayer*owner,QVariant data) const
    {
		DeathStruct death = data.value<DeathStruct>();
		if (death.damage&&death.damage->from==owner&&isNormalGameMode(room->getMode()))
			return owner->getMark(limit_mark)>0&&owner->isAlive()&&owner->hasSkill(objectName());
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &, ServerPlayer*owner) const
    {
        player->setFlags("FenxinTarget");
        bool invoke = room->askForSkillInvoke(owner, objectName()+"$-1", QVariant::fromValue(player));
        player->setFlags("-FenxinTarget");
        if (invoke) {
            //room->doLightbox("$FenxinAnimate");
            room->doSuperLightbox(owner, "fenxin");
            room->removePlayerMark(owner, limit_mark);
            QString role1 = owner->getRole();
            owner->setRole(player->getRole());
            room->notifyProperty(owner, owner, "role", player->getRole());
            room->setPlayerProperty(player, "role", role1);
        }
        return false;
    }
};

class Fenxin : public TriggerSkill
{
public:
    Fenxin() : TriggerSkill("fenxin")
    {
        events << BeforeGameOverJudge;
        frequency = Limited;
        limit_mark = "@burnheart";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (!isNormalGameMode(room->getMode()))
            return false;
        DeathStruct death = data.value<DeathStruct>();
        if (death.damage == nullptr)
            return false;
        ServerPlayer *killer = death.damage->from;
        if (killer == nullptr || killer->isLord() || player->isLord() || player->getHp() > 0)
            return false;
        if (!TriggerSkill::triggerable(killer) || killer->getMark("@burnheart") == 0)
            return false;
        player->setFlags("FenxinTarget");
        bool invoke = room->askForSkillInvoke(killer, objectName()+"$-1", QVariant::fromValue(player));
        player->setFlags("-FenxinTarget");
        if (invoke) {
            //room->doLightbox("$FenxinAnimate");
            room->doSuperLightbox(killer, "fenxin");
            room->removePlayerMark(killer, "@burnheart");
            QString role1 = killer->getRole();
            killer->setRole(player->getRole());
            room->notifyProperty(killer, killer, "role", player->getRole());
            room->setPlayerProperty(player, "role", role1);
        }
        return false;
    }
};

MixinCard::MixinCard()
{
    will_throw = false;
    mute = true;
    handling_method = Card::MethodNone;
}

bool MixinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void MixinCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *source = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = source->getRoom();
    room->broadcastSkillInvoke("mixin", 1);
    target->obtainCard(this, false);
    QList<ServerPlayer *> others;
    foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
        if (target->canSlash(p, false))
            others << p;
    }
    if (others.isEmpty())
        return;

    ServerPlayer *target2 = room->askForPlayerChosen(source, others, "mixin");
    LogMessage log;
    log.type = "#CollateralSlash";
    log.from = source;
    log.to << target2;
    room->sendLog(log);
    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, target->objectName(), target2->objectName());
    if (room->askForUseSlashTo(target, target2, "#mixin", false)) {
        room->broadcastSkillInvoke("mixin", 2);
    } else {
        room->broadcastSkillInvoke("mixin", 3);
        QList<int> card_ids = target->handCards();
        room->fillAG(card_ids, target2);
        int cdid = room->askForAG(target2, card_ids, false, objectName());
        room->obtainCard(target2, cdid, false);
        room->clearAG(target2);
    }
}

class Mixin :public OneCardViewAsSkill
{
public:
    Mixin() :OneCardViewAsSkill("mixin")
    {
        filter_pattern = ".|.|.|hand";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MixinCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        MixinCard *card = new MixinCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class Cangni2 : public TriggerSkill
{
public:
    Cangni2() :TriggerSkill("cangni")
    {
        events << EventPhaseStart;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant) const
    {
        if(event==EventPhaseStart){
            if (target->getPhase() == Player::Discard)
				return target==owner&&owner->isAlive()&&owner->hasSkill(objectName());
        }
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &, ServerPlayer*owner) const
    {
        if (owner->askForSkillInvoke(objectName()+"$1")) {
            QStringList choices;
            choices << "draw";
            if (owner->isWounded()) choices << "recover";
            if (room->askForChoice(player, objectName(), choices.join("+")) == "recover") {
                RecoverStruct recover;
                recover.who = player;
                recover.reason = objectName();
                room->recover(player, recover);
            } else
                player->drawCards(2,objectName());
            player->turnOver();
        }
        return false;
    }
};

class Cangni2Bf : public TriggerSkill
{
public:
    Cangni2Bf() :TriggerSkill("cangni")
    {
        events << CardsMoveOneTime;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
		if (event==CardsMoveOneTime&&!owner->faceUp()&&target!=owner&&target->isAlive()) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from==owner&&move.to!=owner){
				if(move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip))
					return owner->isAlive()&&owner->hasSkill(objectName());
			}else if(move.from!=owner&&move.to==owner){
				if(move.to_place==Player::PlaceHand||move.to_place==Player::PlaceEquip)
					return owner->isAlive()&&owner->hasSkill(objectName());
			}
		}
		return false;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data, ServerPlayer*owner) const
    {
		if (triggerEvent == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from == owner && move.to != owner) {
                room->setPlayerFlag(owner, "cangnilose");    //for AI
                if (!player->isNude() && owner->askForSkillInvoke(objectName()+"$3")) {
                    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, owner->objectName(), player->objectName());
                    room->askForDiscard(player, objectName(), 1, 1, false, true);
                }
                room->setPlayerFlag(owner, "-cangnilose");    //for AI
                return false;
            }
            if (move.to == owner && move.from != owner) {
				room->setPlayerFlag(owner, "cangniget");    //for AI
				if (!player->hasFlag("cangni_used") && owner->askForSkillInvoke(objectName()+"$2")) {
					room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, owner->objectName(), player->objectName());
					room->setPlayerFlag(player, "cangni_used");
					player->drawCards(1,objectName());
				}
				room->setPlayerFlag(owner, "-cangniget");    //for AI
            }
        }
        return false;
    }
};

class Cangni : public TriggerSkill
{
public:
    Cangni() :TriggerSkill("cangni")
    {
        events << EventPhaseStart << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Discard
		&& player->askForSkillInvoke(objectName()+"$1")) {
            QStringList choices;
            choices << "draw";
            if (player->isWounded())
                choices << "recover";

            QString choice;
            if (choices.size() == 1)
                choice = choices.first();
            else
                choice = room->askForChoice(player, objectName(), choices.join("+"));

            if (choice == "recover") {
                RecoverStruct recover;
                recover.who = player;
                recover.reason = objectName();
                room->recover(player, recover);
            } else
                player->drawCards(2,objectName());

            player->turnOver();
            return false;
        } else if (triggerEvent == CardsMoveOneTime && !player->faceUp()) {
            if (!player->hasFlag("CurrentPlayer"))
                return false;

            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            ServerPlayer *target = room->getCurrent();
            if (target->isDead())
                return false;

            if (move.from == player && move.to != player) {
                bool invoke = false;
                for (int i = 0; i < move.card_ids.size(); i++) {
                    if (move.from_places[i] == Player::PlaceHand || move.from_places[i] == Player::PlaceEquip) {
                        invoke = true;
                        break;
                    }
                }

                if (invoke && !target->isNude() && player->askForSkillInvoke(objectName()+"$3","cangnilose")) {
                    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
                    room->askForDiscard(target, objectName(), 1, 1, false, true);
                }
                return false;
            }

            if (move.to == player && move.from != player) {
                if (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip) {

                    if (target->getMark("cangni_used-Clear")<1 && player->askForSkillInvoke(objectName()+"$2","cangniget")) {
                        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
                        target->addMark("cangni_used-Clear");
                        target->drawCards(1,objectName());
                    }
                }
            }
        }
        return false;
    }
};

DuyiCard::DuyiCard()
{
    target_fixed = true;
    mute = true;
}

void DuyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QList<int> card_ids = room->getNCards(1);
    int id = card_ids.first();
    room->fillAG(card_ids, nullptr);
    room->getThread()->delay();
    ServerPlayer *target = room->askForPlayerChosen(source, room->getAlivePlayers(), "duyi");
    const Card *card = Sanguosha->getCard(id);
    target->obtainCard(card);
    room->clearAG();
    if (card->isBlack()) {
        room->setPlayerCardLimitation(target, "use,response", ".|.|.|hand", false);
        target->addMark("duyi_target");
        LogMessage log;
        log.type = "#duyi_eff";
        log.from = source;
        log.to << target;
        log.arg = "duyi";
        room->sendLog(log);
        room->broadcastSkillInvoke("duyi", 1);
    } else
        room->broadcastSkillInvoke("duyi", 2);
}

class DuyiViewAsSkill :public ZeroCardViewAsSkill
{
public:
    DuyiViewAsSkill() :ZeroCardViewAsSkill("duyi")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("DuyiCard");
    }

    const Card *viewAs() const
    {
        return new DuyiCard;
    }
};

class Duyi :public TriggerSkill
{
public:
    Duyi() :TriggerSkill("duyi")
    {
        view_as_skill = new DuyiViewAsSkill;
        events << EventPhaseChanging << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who != player)
                return false;
        } else {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive)
                return false;
        }

        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->getMark("duyi_target") > 0) {
                room->removePlayerCardLimitation(p, "use,response", ".|.|.|hand$0");
                room->setPlayerMark(p, "duyi_target", 0);
                LogMessage log;
                log.type = "#duyi_clear";
                log.from = p;
                log.arg = objectName();
                room->sendLog(log);
            }
        }

        return false;
    }
};

class Duyi2 :public TriggerSkill
{
public:
    Duyi2() :TriggerSkill("#duyi")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive) return false;
        }
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->getMark("duyi_target") > 0) {
                for (int i = 0; i < p->getMark("duyi_target"); i++) {
					room->removePlayerCardLimitation(p, "use,response", ".|.|.|hand$0");
					p->removeMark("duyi_target");
				}
                LogMessage log;
                log.type = "#duyi_clear";
                log.from = p;
                log.arg = objectName();
                room->sendLog(log);
            }
        }
        return false;
    }
};

class Duanzhi : public TriggerSkill
{
public:
    Duanzhi() : TriggerSkill("duanzhi")
    {
        events << TargetConfirmed;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->getTypeId() == Card::TypeSkill || !use.from || use.from == player || !use.to.contains(player))
            return false;

        if (player->askForSkillInvoke(objectName()+"$-1", data)) {
            DummyCard *dummy = new DummyCard;
            for (int i = 1; i <= 2; i++) {
                if (use.from->getCardCount()<i) break;
				int id = room->askForCardChosen(player, use.from, "he", objectName(), false, Card::MethodDiscard, dummy->getSubcards(), i>1);
                if (id<0) break;
				dummy->addSubcard(id);
            }
            if (dummy->subcardsLength() > 0)
                room->throwCard(dummy, objectName(), use.from, player);
            room->loseHp(HpLostStruct(player, 1, objectName(), player));
            delete dummy;
        }
        return false;
    }
};

class Duanzhi2 : public TriggerSkill
{
public:
    Duanzhi2() : TriggerSkill("duanzhi")
    {
        events << TargetConfirmed;
    }
    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
        if(event==TargetConfirmed){
			CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->getTypeId()>0&&use.from&&use.from!=owner&&use.to.contains(owner))
				return target==owner&&owner->isAlive()&&owner->hasSkill(objectName());
        }
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->askForSkillInvoke(objectName()+"$-1", data)) {
            DummyCard *dummy = new DummyCard;
			CardUseStruct use = data.value<CardUseStruct>();
            for (int i = 0; i < 2; i++) {
                if (use.from->getCardCount()<i) break;
				int id = room->askForCardChosen(player, use.from, "he", objectName(), false, Card::MethodDiscard, dummy->getSubcards(), i>1);
                if (id<0) break;
				dummy->addSubcard(id);
            }
            if (dummy->subcardsLength() > 0)
                room->throwCard(dummy, objectName(), use.from, player);
            room->loseHp(HpLostStruct(player, 1, objectName(), player));
            delete dummy;
        }
        return false;
    }
};

class Fengyin :public TriggerSkill
{
public:
    Fengyin() :TriggerSkill("fengyin")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::Start) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(p->getHp()<=player->getHp()&&p->hasSkill(objectName())){
					const Card *card = room->askForCard(p, "Slash|.|.|hand", "@fengyin", data, Card::MethodNone);
					if (card) {
						p->skillInvoked(this);
						room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, p->objectName(), player->objectName());
						room->giveCard(p,player,card,objectName(),true);
						player->skip(Player::Play);
						player->skip(Player::Discard);
					}
				}
			}
        }
        return false;
    }
};

class Fengyin2 :public TriggerSkill
{
public:
    Fengyin2() :TriggerSkill("fengyin")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
        if(event==EventPhaseChanging){
            if (data.value<PhaseChangeStruct>().to==Player::Start)
				return owner->getHp()<=target->getHp()&&owner->isAlive()&&owner->hasSkill(objectName());
        }
		return false;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data, ServerPlayer*owner) const
    {
        if (triggerEvent == EventPhaseChanging) {
			const Card *card = room->askForCard(owner, "Slash|.|.|hand", "@fengyin", data, Card::MethodNone);
			if (card) {
				owner->skillInvoked(this);
				room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, owner->objectName(), player->objectName());
				room->giveCard(owner,player,card,objectName(),true);
				player->skip(Player::Play);
				player->skip(Player::Discard);
			}
        }
        return false;
    }
};

class Chizhong : public MaxCardsSkill
{
public:
    Chizhong() :MaxCardsSkill("#chizhong")
    {
    }

    int getFixed(const Player *target) const
    {
        if (target->hasSkill("chizhong"))
            return target->getMaxHp();
        return -1;
    }
};

class ChizhongKeep : public TriggerSkill
{
public:
    ChizhongKeep() :TriggerSkill("chizhong")
    {
        events << Death;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DeathStruct death = data.value<DeathStruct>();
        if (death.who == player) return false;

        room->sendCompulsoryTriggerLog(player,"chizhong",true,true);
        room->gainMaxHp(player, 1, objectName());
        return false;
    }
};

class Chizhong2 : public TriggerSkill
{
public:
    Chizhong2() :TriggerSkill("chizhong")
    {
        events << Death;
    }
    bool triggerable(ServerPlayer*target,Room*,TriggerEvent event,ServerPlayer*owner,QVariant data) const
    {
        if(event==Death){
            if (data.value<DeathStruct>().who==target)
				return owner!=target&&owner->isAlive()&&owner->hasSkill(objectName());
        }
		return false;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &,ServerPlayer*owner) const
    {
        room->sendCompulsoryTriggerLog(owner,"chizhong",true,true);
        room->gainMaxHp(owner, 1, objectName());
        return false;
    }
};

AssassinsPackage::AssassinsPackage() : Package("assassins")
{
    General *fuhuanghou = new General(this, "as_fuhuanghou", "qun", 3, false);
    fuhuanghou->addSkill(new Mixin);
    fuhuanghou->addSkill(new Cangni);

    General *jiben = new General(this, "as_jiben", "qun", 3);
    jiben->addSkill(new Duyi);
    jiben->addSkill(new Duanzhi);

    General *fuwan = new General(this, "as_fuwan", "qun", 3);
    fuwan->addSkill(new Fengyin);
    fuwan->addSkill(new ChizhongKeep);
    fuwan->addSkill(new Chizhong);
    related_skills.insertMulti("chizhong", "#chizhong");

    General *mushun = new General(this, "as_mushun", "qun");
    mushun->addSkill(new Moukui);

    General *hanxiandi = new General(this, "as_liuxie", "qun", 3);
    hanxiandi->addSkill(new Tianming);
    hanxiandi->addSkill(new Mizhao);

    General *lingju = new General(this, "as_lingju", "qun", 3, false);
    lingju->addSkill(new Jieyuan);
    lingju->addSkill(new Fenxin);

    addMetaObject<MizhaoCard>();
    addMetaObject<MixinCard>();
    addMetaObject<DuyiCard>();
}
ADD_PACKAGE(Assassins)