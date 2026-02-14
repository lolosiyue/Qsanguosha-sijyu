#include "hegemony.h"
//#include "skill.h"
//#include "client.h"
#include "engine.h"
//#include "general.h"
#include "room.h"
//#include "standard-generals.h"
//#include "json.h"
//#include "util.h"
#include "standard.h"
#include "roomthread.h"

class Xiaoguo : public TriggerSkill
{
public:
    Xiaoguo() : TriggerSkill("xiaoguo")
    {
        events << EventPhaseStart;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() != Player::Finish)
            return false;
        ServerPlayer *yuejin = room->findPlayerBySkillName(objectName());
        if (!yuejin || yuejin == player)
            return false;
        if (yuejin->canDiscard(yuejin, "h") && room->askForCard(yuejin, ".Basic", "@xiaoguo", QVariant(), objectName())) {
            room->broadcastSkillInvoke(objectName(), 1);
            if (!room->askForCard(player, ".Equip", "@xiaoguo-discard", QVariant())) {
                room->broadcastSkillInvoke(objectName(), 2);
                room->damage(DamageStruct("xiaoguo", yuejin, player));
            } else {
                room->broadcastSkillInvoke(objectName(), 3);
                if (yuejin->isAlive())
                    yuejin->drawCards(1, objectName());
            }
        }
        return false;
    }
};

class Shushen : public TriggerSkill
{
public:
    Shushen() : TriggerSkill("shushen")
    {
        events << HpRecover;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        RecoverStruct recover_struct = data.value<RecoverStruct>();
        int recover = recover_struct.recover;
        for (int i = 0; i < recover; i++) {
            ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "shushen-invoke", true, true);
            if (target) {
                room->broadcastSkillInvoke(objectName(), target->getGeneralName().contains("liubei") ? 2 : 1);
                if (target->isWounded() && room->askForChoice(player, objectName(), "recover+draw", QVariant::fromValue(target)) == "recover")
                    room->recover(target, RecoverStruct(objectName(), player));
                else
                    target->drawCards(2, objectName());
            } else {
                break;
            }
        }
        return false;
    }
};

class Shenzhi : public PhaseChangeSkill
{
public:
    Shenzhi() : PhaseChangeSkill("shenzhi")
    {
    }

    bool onPhaseChange(ServerPlayer *ganfuren, Room *room) const
    {
        if (ganfuren->getPhase() != Player::Start || ganfuren->isKongcheng())
            return false;
        if (room->askForSkillInvoke(ganfuren, objectName())) {
            // As the cost, if one of her handcards cannot be throwed, the skill is unable to invoke
            foreach (const Card *card, ganfuren->getHandcards()) {
                if (ganfuren->isJilei(card))
                    return false;
            }
            //==================================
            int handcard_num = ganfuren->getHandcardNum();
            room->broadcastSkillInvoke(objectName());
            ganfuren->throwAllHandCards();
            if (handcard_num >= ganfuren->getHp())
                room->recover(ganfuren, RecoverStruct(objectName(), ganfuren));
        }
        return false;
    }
};

DuoshiCard::DuoshiCard()
{
    mute = true;
}

bool DuoshiCard::targetFilter(const QList<const Player *> &, const Player *, const Player *) const
{
    return true;
}

bool DuoshiCard::targetsFeasible(const QList<const Player *> &, const Player *) const
{
    return true;
}

void DuoshiCard::onUse(Room *room, CardUseStruct &card_use) const
{
    CardUseStruct use = card_use;
    if (!use.to.contains(use.from))
        use.to << use.from;
    use.from->getRoom()->broadcastSkillInvoke("duoshi", qMin(2, use.to.length()));
    SkillCard::onUse(room, use);
}

void DuoshiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    effect.to->drawCards(2, "duoshi");
    room->askForDiscard(effect.to, "duoshi", 2, 2, false, true);
}

class Duoshi : public OneCardViewAsSkill
{
public:
    Duoshi() : OneCardViewAsSkill("duoshi")
    {
        filter_pattern = ".|red|.|hand!";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->usedTimes("DuoshiCard") < 4;
    }

    const Card *viewAs(const Card *originalcard) const
    {
        DuoshiCard *await = new DuoshiCard;
        await->addSubcard(originalcard->getId());
        return await;
    }
};

FenxunCard::FenxunCard()
{
}

bool FenxunCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void FenxunCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    effect.from->tag["FenxunTarget"] = QVariant::fromValue(effect.to);
    room->setFixedDistance(effect.from, effect.to, 1);
}

class FenxunViewAsSkill : public OneCardViewAsSkill
{
public:
    FenxunViewAsSkill() : OneCardViewAsSkill("fenxun")
    {
        filter_pattern = ".!";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->canDiscard(player, "he") && !player->hasUsed("FenxunCard");
    }

    const Card *viewAs(const Card *originalcard) const
    {
        FenxunCard *first = new FenxunCard;
        first->addSubcard(originalcard->getId());
        first->setSkillName(objectName());
        return first;
    }
};

class Fenxun : public TriggerSkill
{
public:
    Fenxun() : TriggerSkill("fenxun")
    {
        events << EventPhaseChanging << Death;
        view_as_skill = new FenxunViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->tag["FenxunTarget"].value<ServerPlayer *>() != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *dingfeng, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive)
                return false;
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who != dingfeng)
                return false;
        }
        ServerPlayer *target = dingfeng->tag["FenxunTarget"].value<ServerPlayer *>();

        if (target) {
            room->removeFixedDistance(dingfeng, target, 1);
            dingfeng->tag.remove("FenxunTarget");
        }
        return false;
    }
};

class Mingshi : public TriggerSkill
{
public:
    Mingshi() : TriggerSkill("mingshi")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.from&&damage.from->getEquips().length() <= qMin(2, player->getEquips().length())) {
			room->broadcastSkillInvoke(objectName());

			LogMessage log;
			log.type = "#Mingshi";
			log.from = player;
			log.arg = QString::number(damage.damage);
			log.arg2 = QString::number(--damage.damage);
			room->sendLog(log);
			room->notifySkillInvoked(player, objectName());
			data = QVariant::fromValue(damage);

			if (damage.damage < 1)
				return true;
        }
        return false;
    }
};

class Lirang : public TriggerSkill
{
public:
    Lirang() : TriggerSkill("lirang")
    {
        events << CardsMoveOneTime;
    }
    int getPriority(TriggerEvent) const
    {
        return 3;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *kongrong, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place == Player::DiscardPile && move.from == kongrong
            && ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)) {

            int i = 0;
            QList<int> lirang_card;
            foreach (int card_id, move.card_ids) {
                if (!room->getCardOwner(card_id)
                    && (move.from_places[i] == Player::PlaceHand || move.from_places[i] == Player::PlaceEquip)) {
                    lirang_card << card_id;
                }
                i++;
            }
            if (lirang_card.length()>0&&kongrong->askForSkillInvoke(objectName()+"$-1",data))
				kongrong->assignmentCards(lirang_card,"lirang|@lirang-distribute",room->getOtherPlayers(kongrong),-1,1);
        }
        return false;
    }
};

class Sijian : public TriggerSkill
{
public:
    Sijian() : TriggerSkill("sijian")
    {
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *tianfeng, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from == tianfeng && move.from_places.contains(Player::PlaceHand) && move.is_last_handcard) {
            QList<ServerPlayer *> other_players = room->getOtherPlayers(tianfeng);
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, other_players) {
                if (tianfeng->canDiscard(p, "he"))
                    targets << p;
            }
            if (targets.isEmpty()) return false;
            ServerPlayer *to = room->askForPlayerChosen(tianfeng, targets, objectName(), "sijian-invoke", true, true);
            if (to) {
                room->broadcastSkillInvoke(objectName(), to->isLord() ? 2 : 1);
                int card_id = room->askForCardChosen(tianfeng, to, "he", objectName(), false, Card::MethodDiscard);
                room->throwCard(card_id, to, tianfeng);
            }
        }
        return false;
    }
};

class Suishi : public TriggerSkill
{
public:
    Suishi() : TriggerSkill("suishi")
    {
        events << Dying << Death;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        ServerPlayer *target = nullptr;
        if (triggerEvent == Dying) {
            DyingStruct dying = data.value<DyingStruct>();
            if (dying.damage && dying.damage->from)
                target = dying.damage->from;
            if (dying.who != player && target
                && target->askForSkillInvoke(this,QString("draw:%1").arg(player->objectName()),target==player)) {
                room->broadcastSkillInvoke(objectName(), 1);
                if (target != player) {
                    LogMessage log;
                    log.type = "#InvokeOthersSkill";
                    log.from = target;
                    log.to << player;
                    log.arg = objectName();
                    room->sendLog(log);
                    room->notifySkillInvoked(player, objectName());
                }
                player->drawCards(1, objectName());
            }
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.damage && death.damage->from)
                target = death.damage->from;
            if (target && target->askForSkillInvoke(this, QString("losehp:%1").arg(player->objectName()),target==player)) {
                room->broadcastSkillInvoke(objectName(), 2);
                if (target != player) {
                    LogMessage log;
                    log.type = "#InvokeOthersSkill";
                    log.from = target;
                    log.to << player;
                    log.arg = objectName();
                    room->sendLog(log);
                    room->notifySkillInvoked(player, objectName());
                }
                room->loseHp(HpLostStruct(player, 1, "suishi", player));
            }
        }
        return false;
    }
};

ShuangrenCard::ShuangrenCard()
{
}

bool ShuangrenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void ShuangrenCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    bool success = effect.from->pindian(effect.to, "shuangren", nullptr);
    if (success) {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *target, room->getAlivePlayers()) {
            if (effect.from->canSlash(target, nullptr, false))
                targets << target;
        }
        if (targets.isEmpty())
            return;

        ServerPlayer *target = room->askForPlayerChosen(effect.from, targets, "shuangren", "@dummy-slash");

        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_shuangren");
		slash->deleteLater();
        room->useCard(CardUseStruct(slash, effect.from, target));
    } else {
        room->broadcastSkillInvoke("shuangren", 3);
        room->setPlayerFlag(effect.from, "ShuangrenSkipPlay");
    }
}

class ShuangrenViewAsSkill : public ZeroCardViewAsSkill
{
public:
    ShuangrenViewAsSkill() : ZeroCardViewAsSkill("shuangren")
    {
        response_pattern = "@@shuangren";
    }

    const Card *viewAs() const
    {
        return new ShuangrenCard;
    }
};

class Shuangren : public PhaseChangeSkill
{
public:
    Shuangren() : PhaseChangeSkill("shuangren")
    {
        view_as_skill = new ShuangrenViewAsSkill;
    }

    bool onPhaseChange(ServerPlayer *jiling, Room *room) const
    {
        if (jiling->getPhase() == Player::Play) {
            bool can_invoke = false;
            QList<ServerPlayer *> other_players = room->getOtherPlayers(jiling);
            foreach (ServerPlayer *player, other_players) {
                if (jiling->canPindian(player)) {
                    can_invoke = true;
                    break;
                }
            }

            if (can_invoke)
                room->askForUseCard(jiling, "@@shuangren", "@shuangren-card");
            if (jiling->hasFlag("ShuangrenSkipPlay"))
                return true;
        }

        return false;
    }

    int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        if (card->isKindOf("Slash"))
            return 2;
        else
            return 1;
    }
};

XiongyiCard::XiongyiCard()
{
    mute = true;
}

bool XiongyiCard::targetFilter(const QList<const Player *> &, const Player *, const Player *) const
{
    return true;
}

bool XiongyiCard::targetsFeasible(const QList<const Player *> &, const Player *) const
{
    return true;
}

void XiongyiCard::onUse(Room *room, CardUseStruct &card_use) const
{
    CardUseStruct use = card_use;
    if (!use.to.contains(use.from))
        use.to << use.from;
    room->removePlayerMark(use.from, "@arise");
    room->broadcastSkillInvoke("xiongyi");
    //room->doLightbox("$XiongyiAnimate", 4500);
    room->doSuperLightbox(use.from, "xiongyi");
    SkillCard::onUse(room, use);
}

void XiongyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    foreach(ServerPlayer *p, targets)
        p->drawCards(3, "xiongyi");
    if (targets.length() <= room->getAlivePlayers().length() / 2 && source->isWounded())
        room->recover(source, RecoverStruct("xiongyi", source));
}

class Xiongyi : public ZeroCardViewAsSkill
{
public:
    Xiongyi() : ZeroCardViewAsSkill("xiongyi")
    {
        frequency = Limited;
        limit_mark = "@arise";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("@arise") >= 1;
    }

    const Card *viewAs() const
    {
        return new XiongyiCard;
    }
};

class Kuangfu : public TriggerSkill
{
public:
    Kuangfu() : TriggerSkill("kuangfu")
    {
        events << Damage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *panfeng, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *target = damage.to;
        if (damage.card && damage.card->isKindOf("Slash") && target->hasEquip()
            && !target->hasFlag("Global_DebutFlag") && !damage.chain && !damage.transfer) {
            QStringList equiplist;
            for (int i = 0; i < S_EQUIP_AREA_LENGTH; i++) {
                if (!target->getEquip(i)) continue;
                if (panfeng->canDiscard(target, target->getEquip(i)->getEffectiveId()) || panfeng->getEquip(i) == nullptr)
                    equiplist << QString::number(i);
            }
            if (equiplist.isEmpty() || !panfeng->askForSkillInvoke(this, data))
                return false;
            int equip_index = room->askForChoice(panfeng, "kuangfu_equip", equiplist.join("+"), QVariant::fromValue(target)).toInt();
            const Card *card = target->getEquip(equip_index);
            int card_id = card->getEffectiveId();

            QStringList choicelist;
            if (panfeng->canDiscard(target, card_id))
                choicelist << "throw";
            if (equip_index > -1 && panfeng->getEquip(equip_index) == nullptr)
                choicelist << "move";

            QString choice = room->askForChoice(panfeng, "kuangfu", choicelist.join("+"));

            if (choice == "move") {
                room->broadcastSkillInvoke(objectName(), 1);
                room->moveCardTo(card, panfeng, Player::PlaceEquip);
            } else {
                room->broadcastSkillInvoke(objectName(), 2);
                room->throwCard(card, target, panfeng);
            }
        }

        return false;
    }
};

class Huoshui : public TriggerSkill
{
public:
    Huoshui() : TriggerSkill("huoshui")
    {
        events << EventPhaseStart << Death
            << EventLoseSkill << EventAcquireSkill
            << HpChanged << MaxHpChanged;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    int getPriority(TriggerEvent) const
    {
        return 5;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
            if (!TriggerSkill::triggerable(player)
                || (player->getPhase() != Player::RoundStart && player->getPhase() != Player::NotActive)) return false;
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who != player || !player->hasSkill(objectName())) return false;
        } else if (triggerEvent == EventLoseSkill) {
            if (data.toString() != objectName() || player->getPhase() == Player::NotActive) return false;
        } else if (triggerEvent == EventAcquireSkill) {
            if (data.toString() != objectName() || !player->hasSkill(objectName()) || player->getPhase() == Player::NotActive)
                return false;
        } else if (triggerEvent == MaxHpChanged || triggerEvent == HpChanged) {
            if (!room->getCurrent() || !room->getCurrent()->hasSkill(objectName())) return false;
        }

        foreach(ServerPlayer *p, room->getAllPlayers())
            room->filterCards(p, p->getCards("he"), true);

        JsonArray args;
        args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);

        return false;
    }
};

class HuoshuiInvalidity : public InvaliditySkill
{
public:
    HuoshuiInvalidity() : InvaliditySkill("#huoshui-inv")
    {
    }

    bool isSkillValid(const Player *player, const Skill *) const
    {
        if (player->getHp() >= (player->getMaxHp() + 1) / 2) {
            foreach (const Player *p, player->getAliveSiblings()) {
                if (p->hasFlag("CurrentPlayer")&&p->hasSkill("huoshui"))
                    return false;
            }
        }
        return true;
    }
};

QingchengCard::QingchengCard()
{
    handling_method = Card::MethodDiscard;
}

void QingchengCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *player = card_use.from, *to = card_use.to.first();

    LogMessage log;
    log.from = player;
    log.to = card_use.to;
    log.type = "#UseCard";
    log.card_str = card_use.card->toString();
    room->sendLog(log);

    QStringList skill_list;
    foreach (const Skill *skill, to->getVisibleSkillList()) {
        if (!skill_list.contains(skill->objectName()) && !skill->isAttachedLordSkill()) {
            skill_list << skill->objectName();
        }
    }
    QString skill_qc;
    if (!skill_list.isEmpty()) {
        QVariant data_for_ai = QVariant::fromValue(to);
        skill_qc = room->askForChoice(player, "qingcheng", skill_list.join("+"), data_for_ai);
    }

    if (!skill_qc.isEmpty()) {
        LogMessage log;
        log.type = "$QingchengNullify";
        log.from = player;
        log.to << to;
        log.arg = skill_qc;
        room->sendLog(log);

        QStringList Qingchenglist = to->tag["Qingcheng"].toStringList();
        Qingchenglist << skill_qc;
        to->tag["Qingcheng"] = QVariant::fromValue(Qingchenglist);
        room->addPlayerMark(to, "Qingcheng" + skill_qc);

        foreach(ServerPlayer *p, room->getAllPlayers())
            room->filterCards(p, p->getCards("he"), true);

        JsonArray args;
        args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
    }

    QVariant data = QVariant::fromValue(card_use);
    RoomThread *thread = room->getThread();
    thread->trigger(PreCardUsed, room, player, data);
    card_use = data.value<CardUseStruct>();

    CardMoveReason reason(CardMoveReason::S_REASON_THROW, player->objectName(), "", card_use.card->getSkillName(), "");
    room->moveCardTo(this, player, nullptr, Player::DiscardPile, reason, true);

    thread->trigger(CardUsed, room, card_use.from, data);
    card_use = data.value<CardUseStruct>();
    thread->trigger(CardFinished, room, card_use.from, data);
}

bool QingchengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

class QingchengViewAsSkill : public OneCardViewAsSkill
{
public:
    QingchengViewAsSkill() : OneCardViewAsSkill("qingcheng")
    {
        filter_pattern = "EquipCard!";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->canDiscard(player, "he");
    }

    const Card *viewAs(const Card *originalcard) const
    {
        QingchengCard *first = new QingchengCard;
        first->addSubcard(originalcard->getId());
        first->setSkillName(objectName());
        return first;
    }
};

class Qingcheng : public TriggerSkill
{
public:
    Qingcheng() : TriggerSkill("qingcheng")
    {
        events << EventPhaseStart;
        view_as_skill = new QingchengViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    int getPriority(TriggerEvent) const
    {
        return 6;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() == Player::RoundStart) {
            QStringList Qingchenglist = player->tag["Qingcheng"].toStringList();
            if (Qingchenglist.isEmpty()) return false;
            foreach (QString skill_name, Qingchenglist) {
                room->setPlayerMark(player, "Qingcheng" + skill_name, 0);
                if (player->hasSkill(skill_name)) {
                    LogMessage log;
                    log.type = "$QingchengReset";
                    log.from = player;
                    log.arg = skill_name;
                    room->sendLog(log);
                }
            }
            player->tag.remove("Qingcheng");
            foreach(ServerPlayer *p, room->getAllPlayers())
                room->filterCards(p, p->getCards("he"), false);

            JsonArray args;
            args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
            room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
        }
        return false;
    }
};

class QingchengInvalidity : public InvaliditySkill
{
public:
    QingchengInvalidity() : InvaliditySkill("#qingcheng-inv")
    {
    }

    bool isSkillValid(const Player *player, const Skill *skill) const
    {
        return player->getMark("Qingcheng" + skill->objectName())<1;
    }
};

HegemonyPackage::HegemonyPackage()
    : Package("hegemony")
{
    General *yuejin = new General(this, "yuejin", "wei", 4, true); // WEI 016
    yuejin->addSkill(new Xiaoguo);

    General *ganfuren = new General(this, "ganfuren", "shu", 3, false); // SHU 016
    ganfuren->addSkill(new Shushen);
    ganfuren->addSkill(new Shenzhi);

    General *heg_luxun = new General(this, "heg_luxun", "wu", 3); // WU 007 G
    heg_luxun->addSkill("nosqianxun");
    heg_luxun->addSkill(new Duoshi);

    General *dingfeng = new General(this, "dingfeng", "wu", 4, true); // WU 016
    dingfeng->addSkill(new Skill("duanbing", Skill::NotCompulsory));
    dingfeng->addSkill(new Fenxun);

    General *mateng = new General(this, "mateng", "qun"); // QUN 013
    mateng->addSkill("mashu");
    mateng->addSkill(new Xiongyi);

    General *kongrong = new General(this, "kongrong", "qun", 3); // QUN 014
    kongrong->addSkill(new Mingshi);
    kongrong->addSkill(new Lirang);

    General *jiling = new General(this, "jiling", "qun", 4); // QUN 015
    jiling->addSkill(new Shuangren);
    jiling->addSkill(new SlashNoDistanceLimitSkill("shuangren"));
    related_skills.insertMulti("shuangren", "#shuangren-slash-ndl");

    General *tianfeng = new General(this, "tianfeng", "qun", 3); // QUN 016
    tianfeng->addSkill(new Sijian);
    tianfeng->addSkill(new Suishi);

    General *panfeng = new General(this, "panfeng", "qun", 4, true); // QUN 017
    panfeng->addSkill(new Kuangfu);

    General *zoushi = new General(this, "zoushi", "qun", 3, false); // QUN 018
    zoushi->addSkill(new Huoshui);
    zoushi->addSkill(new HuoshuiInvalidity);
    zoushi->addSkill(new Qingcheng);
    zoushi->addSkill(new QingchengInvalidity);
    related_skills.insertMulti("huoshui", "#huoshui-inv");
    related_skills.insertMulti("qingcheng", "#qingcheng-inv");

    General *heg_caopi = new General(this, "heg_caopi$", "wei", 3, true); // WEI 014 G
    heg_caopi->addSkill("fangzhu");
    heg_caopi->addSkill("xingshang");
    heg_caopi->addSkill("songwei");

    General *heg_zhenji = new General(this, "heg_zhenji", "wei", 3, false); // WEI 007 G
    heg_zhenji->addSkill("qingguo");
    heg_zhenji->addSkill("luoshen");

    General *heg_zhugeliang = new General(this, "heg_zhugeliang", "shu", 3, true); // SHU 004 G
    heg_zhugeliang->addSkill("guanxing");
    heg_zhugeliang->addSkill("kongcheng");

    General *heg_huangyueying = new General(this, "heg_huangyueying", "shu", 3, false); // SHU 007 G
    heg_huangyueying->addSkill("nosjizhi");
    heg_huangyueying->addSkill("nosqicai");

    General *heg_zhouyu = new General(this, "heg_zhouyu", "wu", 3, true); // WU 005 G
    heg_zhouyu->addSkill("nosyingzi");
    heg_zhouyu->addSkill("nosfanjian");

    General *heg_xiaoqiao = new General(this, "heg_xiaoqiao", "wu", 3, false); // WU 011 G
    heg_xiaoqiao->addSkill("tianxiang");
    heg_xiaoqiao->addSkill("hongyan");

    General *heg_lvbu = new General(this, "heg_lvbu", "qun", 4, true); // QUN 002 G
    heg_lvbu->addSkill("wushuang");

    General *heg_diaochan = new General(this, "heg_diaochan", "qun", 3, false); // QUN 003 G
    heg_diaochan->addSkill("lijian");
    heg_diaochan->addSkill("biyue");

    addMetaObject<DuoshiCard>();
    addMetaObject<FenxunCard>();
    addMetaObject<ShuangrenCard>();
    addMetaObject<XiongyiCard>();
    addMetaObject<QingchengCard>();
}
ADD_PACKAGE(Hegemony)