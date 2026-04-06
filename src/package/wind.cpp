//#include "settings.h"
//#include "standard.h"
//#include "skill.h"
#include "wind.h"
//#include "client.h"
#include "engine.h"
//#include "ai.h"
//#include "general.h"
#include "clientplayer.h"
#include "clientstruct.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "maneuvering.h"

//#include "json.h"

class Guidao : public RetrialSkill
{
public:
    Guidao() : RetrialSkill("guidao", true)
    {
    }

    bool triggerable(const ServerPlayer *target) const
    {
        if (!TriggerSkill::triggerable(target))
            return false;

		foreach (const Card *e, target->getEquips()) {
			if (e->isBlack())
				return true;
		}
		return !target->isKongcheng();
    }

    const Card *onRetrial(ServerPlayer *player, JudgeStruct *judge) const
    {
        QStringList prompt_list;
        prompt_list << "@guidao-card" << judge->who->objectName() << objectName()
			<< judge->reason << QString::number(judge->card->getEffectiveId());
        QString prompt = prompt_list.join(":");

        Room *room = player->getRoom();

        const Card *card = room->askForCard(player, ".|black", prompt, QVariant::fromValue(judge), Card::MethodResponse, judge->who, true);

        if (card != nullptr) {
            int index = qrand() % 2 + 1;
            if (Player::isNostalGeneral(player, "zhangjiao"))
                index += 2;
            room->broadcastSkillInvoke(objectName(), index);
        }
        return card;
    }
};

class Leiji : public TriggerSkill
{
public:
    Leiji() : TriggerSkill("leiji")
    {
        events << CardResponded << DamageCaused << CardUsed;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *zhangjiao, QVariant &data) const
    {
		if (triggerEvent == DamageCaused && zhangjiao->isAlive()) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.reason == objectName() && !damage.chain)
                room->recover(zhangjiao, RecoverStruct("leiji", zhangjiao));
			return false;
        }
        const Card *jink = nullptr;
        if (triggerEvent == CardUsed)
            jink = data.value<CardUseStruct>().card;
        else
            jink = data.value<CardResponseStruct>().m_card;
		if (jink && jink->isKindOf("Jink")) {
			ServerPlayer *target = room->askForPlayerChosen(zhangjiao, room->getAlivePlayers(), objectName(), "leiji-invoke", true, true);
			if (target) {
				room->broadcastSkillInvoke(objectName());

				JudgeStruct judge;
				judge.pattern = ".|black";
				judge.good = false;
				judge.negative = true;
				judge.reason = objectName();
				judge.who = target;

				room->judge(judge);

				if (judge.isBad())
					room->damage(DamageStruct(objectName(), zhangjiao, target, 1, DamageStruct::Thunder));
			}
		}
        return false;
    }
};

HuangtianCard::HuangtianCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
	mute = true;
}

void HuangtianCard::onUse(Room *room, CardUseStruct &use) const
{
	QVariant data = QVariant::fromValue(use);
	room->getThread()->trigger(PreCardUsed, room, use.from, data);
	room->getThread()->trigger(CardUsed, room, use.from, data);
	use = data.value<CardUseStruct>();
	foreach (ServerPlayer *zhangjiao, use.to){
		if (zhangjiao->isWeidi()) {
			room->broadcastSkillInvoke("weidi",-1,zhangjiao);
			room->notifySkillInvoked(zhangjiao, "weidi");
		}else {
            int index = qrand() % 2 + 1;
            if (Player::isNostalGeneral(zhangjiao, "zhangjiao"))
                index += 2;
            else if (zhangjiao->isJieGeneral())
                index += 4;
            room->broadcastSkillInvoke("huangtian", index,zhangjiao);
		}
		use.from->skillInvoked("huangtian",0,zhangjiao);

        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, use.from->objectName(), zhangjiao->objectName(), "huangtian", "");
        room->obtainCard(zhangjiao, this, reason);
    }
	room->getThread()->trigger(CardFinished, room, use.from, data);
	use = data.value<CardUseStruct>();
}

bool HuangtianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && to_select->hasLordSkill("huangtian");
}

class HuangtianViewAsSkill : public OneCardViewAsSkill
{
public:
    HuangtianViewAsSkill() :OneCardViewAsSkill("huangtian_attach")
    {
        attached_lord_skill = true;
        filter_pattern = "Jink,Lightning";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getKingdom() == "qun" && player->usedTimes("HuangtianCard")<1;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        HuangtianCard *card = new HuangtianCard;
        card->addSubcard(originalCard);

        return card;
    }
};

class Huangtian : public TriggerSkill
{
public:
    Huangtian() : TriggerSkill("huangtian$")
    {
        events << EventPhaseStart << EventPhaseEnd << EventAcquireSkill;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (triggerEvent == EventAcquireSkill&&player->hasLordSkill(this,true)) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getPhase()==Player::Play&&!p->hasSkill("huangtian_attach",true)){
					room->attachSkillToPlayer(p, "huangtian_attach");
				}
			}
		}else if(player->getPhase()!=Player::Play)
			return false;
        if (triggerEvent == EventPhaseStart) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->hasLordSkill(this,true)){
					room->attachSkillToPlayer(player, "huangtian_attach");
					break;
				}
			}
        }else{
			if (player->hasSkill("huangtian_attach",true))
				room->detachSkillFromPlayer(player, "huangtian_attach", true);
            room->setPlayerFlag(player, "-ForbidHuangtian");
            foreach (ServerPlayer *p, room->getOtherPlayers(player))
                room->setPlayerFlag(p, "-HuangtianInvoked");
		}
        return false;
    }
};

ShensuCard::ShensuCard()
{
}

bool ShensuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Slash *slash = new Slash(NoSuit, 0);
    slash->setSkillName("shensu");
    slash->deleteLater();
    return slash->targetFilter(targets, to_select, Self);
}

void ShensuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	if(subcards.isEmpty()){
		source->skip(Player::Judge, true);
		source->skip(Player::Draw, true);
	}else
		source->skip(Player::Play, true);
	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->setSkillName("_shensu");
    room->useCard(CardUseStruct(slash, source, targets));
    slash->deleteLater();
}

class ShensuViewAsSkill : public ViewAsSkill
{
public:
    ShensuViewAsSkill() : ViewAsSkill("shensu")
    {
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern.startsWith("@@shensu");
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (Sanguosha->getCurrentCardUsePattern().endsWith("1"))
            return false;
		return selected.isEmpty() && to_select->isKindOf("EquipCard") && !Self->isJilei(to_select);
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (Sanguosha->getCurrentCardUsePattern().endsWith("1")) {
            return cards.isEmpty() ? new ShensuCard : nullptr;
        } else {
            if (cards.length() != 1)
                return nullptr;

            ShensuCard *card = new ShensuCard;
            card->addSubcards(cards);

            return card;
        }
    }
};

class Shensu : public TriggerSkill
{
public:
    Shensu() : TriggerSkill("shensu")
    {
        events << EventPhaseChanging;
        view_as_skill = new ShensuViewAsSkill;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *xiahouyuan, QVariant &data) const
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to == Player::Judge && !xiahouyuan->isSkipped(Player::Judge) && !xiahouyuan->isSkipped(Player::Draw)) {
            room->askForUseCard(xiahouyuan, "@@shensu1", "@shensu1", 1);
        } else if (change.to == Player::Play && !xiahouyuan->isSkipped(Player::Play)) {
            room->askForUseCard(xiahouyuan, "@@shensu2", "@shensu2", 2, Card::MethodDiscard);
        }
        return false;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qrand() % 2 + 1;
        if (player->hasSkill("baobian",true))
            index += 2;
        return index;
    }
};

Jushou::Jushou() : PhaseChangeSkill("jushou")
{
}

int Jushou::getJushouDrawNum(ServerPlayer *) const
{
    return 1;
}

bool Jushou::onPhaseChange(ServerPlayer *target, Room *room) const
{
    if (target->getPhase() == Player::Finish) {
        if (room->askForSkillInvoke(target, objectName())) {
            room->broadcastSkillInvoke(objectName());
            target->drawCards(getJushouDrawNum(target), objectName());
            target->turnOver();
        }
    }

    return false;
}

class Jiewei : public TriggerSkill
{
public:
    Jiewei() : TriggerSkill("jiewei")
    {
        events << TurnedOver;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (!room->askForSkillInvoke(player, objectName())) return false;
        room->broadcastSkillInvoke(objectName());
        player->drawCards(1, objectName());

        const Card *card = room->askForUseCard(player, "TrickCard+^Nullification+^Suijiyingbian,EquipCard|.|.|hand", "@jiewei");
        if (!card) return false;

        QList<ServerPlayer *> targets;
        if (card->getTypeId() == Card::TypeTrick) {
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                bool can_discard = false;
                foreach (const Card *judge, p->getJudgingArea()) {
                    if (judge->getTypeId() == Card::TypeTrick && player->canDiscard(p, judge->getEffectiveId())) {
                        can_discard = true;
                        break;
                    } else if (judge->getTypeId() == Card::TypeSkill) {
                        const Card *real_card = Sanguosha->getEngineCard(judge->getEffectiveId());
                        if (real_card->getTypeId() == Card::TypeTrick && player->canDiscard(p, real_card->getEffectiveId())) {
                            can_discard = true;
                            break;
                        }
                    }
                }
                if (can_discard) targets << p;
            }
        } else if (card->getTypeId() == Card::TypeEquip) {
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (!p->getEquips().isEmpty() && player->canDiscard(p, "e"))
                    targets << p;
                else {
                    foreach (const Card *judge, p->getJudgingArea()) {
                        if (judge->getTypeId() == Card::TypeSkill) {
                            const Card *real_card = Sanguosha->getEngineCard(judge->getEffectiveId());
                            if (real_card->getTypeId() == Card::TypeEquip && player->canDiscard(p, real_card->getEffectiveId())) {
                                targets << p;
                                break;
                            }
                        }
                    }
                }
            }
        }
        ServerPlayer *to_discard = room->askForPlayerChosen(player, targets, objectName(), "@jiewei-discard", true);
        if (to_discard) {
            QList<int> disabled_ids;
            foreach (const Card *c, to_discard->getCards("ej")) {
                const Card *pcard = c;
                if (pcard->getTypeId() == Card::TypeSkill)
                    pcard = Sanguosha->getEngineCard(c->getEffectiveId());
                if (pcard->getTypeId() != card->getTypeId())
                    disabled_ids << pcard->getEffectiveId();
            }
            int id = room->askForCardChosen(player, to_discard, "ej", objectName(), false, Card::MethodDiscard, disabled_ids);
            room->throwCard(id, to_discard, player);
        }
        return false;
    }
};

class Liegong : public TriggerSkill
{
public:
    Liegong() : TriggerSkill("liegong")
    {
        events << TargetSpecified;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (player != use.from || player->getPhase() != Player::Play || !use.card->isKindOf("Slash"))
            return false;
        QVariantList jink_list = player->getTag("Jink_" + use.card->toString()).toList();
        int index = 0;
        foreach (ServerPlayer *p, use.to) {
            int handcardnum = p->getHandcardNum();
            if ((player->getHp() <= handcardnum || player->getAttackRange() >= handcardnum)
                && player->askForSkillInvoke(this, QVariant::fromValue(p))) {
                room->broadcastSkillInvoke(objectName());

                LogMessage log;
                log.type = "#NoJink";
                log.from = p;
                room->sendLog(log);
                jink_list.replace(index, QVariant(0));
            }
            index++;
        }
        player->setTag("Jink_" + use.card->toString(), QVariant::fromValue(jink_list));
        return false;
    }
};

class Kuanggu : public TriggerSkill
{
public:
    Kuanggu() : TriggerSkill("kuanggu")
    {
        frequency = Compulsory;
        events << Damage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (player->getTag("InvokeKuanggu").toBool() && player->isWounded()) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(player, objectName());
            room->recover(player, RecoverStruct(player, nullptr, damage.damage, "kuanggu"));
        }
        return false;
    }
};

class KuangguRecord : public TriggerSkill
{
public:
    KuangguRecord() : TriggerSkill("#kuanggu-record")
    {
        events << DamageDone;
        global = true;
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
		if (damage.from) damage.from->setTag("InvokeKuanggu", (damage.from->distanceTo(damage.to) <= 1));
        return false;
    }
};

class Buqu : public TriggerSkill
{
public:
    Buqu() : TriggerSkill("buqu")
    {
        events << AskForPeaches;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *zhoutai, QVariant &data) const
    {
        DyingStruct dying = data.value<DyingStruct>();
        if (dying.who != zhoutai || zhoutai->getHp() > 0)
            return false;

        room->sendCompulsoryTriggerLog(zhoutai, this);
        int id = room->drawCard();
        zhoutai->addToPile("buqu", id);
        int num = Sanguosha->getCard(id)->getNumber();
        foreach (int card_id, zhoutai->getPile("buqu")) {
            if (card_id!=id&&Sanguosha->getCard(card_id)->getNumber()==num) {
                QList<int> ids;
				ids << id << card_id;
				LogMessage log;
                log.type = "$NosBuquDuplicateItem";
                log.from = zhoutai;
                log.card_str = ListI2S(ids).join("+");
                room->sendLog(log);
				room->throwCard(id, objectName(), nullptr);
                return false;
            }
        }
        room->recover(zhoutai, RecoverStruct(zhoutai, nullptr, 1 - zhoutai->getHp(), objectName()));
        return false;
    }
};

class BuquMaxCards : public MaxCardsSkill
{
public:
    BuquMaxCards() : MaxCardsSkill("#buqu")
    {
    }

    int getFixed(const Player *target) const
    {
        int n = target->getPile("buqu").length();
		if (n>0&&target->hasSkill("buqu"))
            return n;
        return -1;
    }
};

class Fenji : public TriggerSkill
{
public:
    Fenji() : TriggerSkill("fenji")
    {
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (player->getHp() > 0 && move.from && move.from->isAlive() && move.from_places.contains(Player::PlaceHand)
            && ((move.reason.m_reason == CardMoveReason::S_REASON_DISMANTLE
            && move.reason.m_playerId != move.reason.m_targetId)
            || (move.to && move.to != move.from && move.to_place == Player::PlaceHand
            && move.reason.m_reason != CardMoveReason::S_REASON_GIVE
            && move.reason.m_reason != CardMoveReason::S_REASON_SWAP))) {
            move.from->setFlags("FenjiMoveFrom"); // For AI
            bool invoke = room->askForSkillInvoke(player, objectName(), data);
            move.from->setFlags("-FenjiMoveFrom");
            if (invoke) {
                room->broadcastSkillInvoke(objectName());
                room->loseHp(HpLostStruct(player, 1, objectName(), player));
                if (move.from->isAlive())
                    room->drawCards((ServerPlayer *)move.from, 2, "fenji");
            }
        }
        return false;
    }
};

class Hongyan : public FilterSkill
{
public:
    Hongyan() : FilterSkill("hongyan")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->getSuit() == Card::Spade;
    }

    const Card *viewAs(const Card *original) const
    {
        Card *new_card = Sanguosha->cloneCard(original->objectName(),Card::Heart,original->getNumber());
        new_card->setSkillName("hongyan");
        return new_card;
    }

	int getEffectIndex(const ServerPlayer *, const Card *) const
	{
		return -2;
	}
};

TianxiangCard::TianxiangCard()
{
}

void TianxiangCard::onEffect(CardEffectStruct &effect) const
{
    DamageStruct damage = effect.from->getTag("TianxiangDamage").value<DamageStruct>();
    damage.to = effect.to;
    damage.transfer = true;
    damage.transfer_reason = "tianxiang";
    effect.from->setTag("TransferDamage", QVariant::fromValue(damage));
}

class TianxiangViewAsSkill : public OneCardViewAsSkill
{
public:
    TianxiangViewAsSkill() : OneCardViewAsSkill("tianxiang")
    {
        filter_pattern = ".|heart|.|hand!";
        response_pattern = "@@tianxiang";
    }

    const Card *viewAs(const Card *originalCard) const
    {
        TianxiangCard *tianxiangCard = new TianxiangCard;
        tianxiangCard->addSubcard(originalCard);
        return tianxiangCard;
    }
};

class Tianxiang : public TriggerSkill
{
public:
    Tianxiang() : TriggerSkill("tianxiang")
    {
        events << DamageInflicted;
        view_as_skill = new TianxiangViewAsSkill;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *xiaoqiao, QVariant &data) const
    {
        if (xiaoqiao->canDiscard(xiaoqiao, "h")) {
            xiaoqiao->setTag("TianxiangDamage", data);
            return room->askForUseCard(xiaoqiao, "@@tianxiang", "@tianxiang-card", -1, Card::MethodDiscard);
        }
        return false;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qrand() % 2 + 1;
        if (!player->hasInnateSkill(this) && player->hasSkill("luoyan"))
            index += 2;

        return index;
    }
};

class TianxiangDraw : public TriggerSkill
{
public:
    TianxiangDraw() : TriggerSkill("#tianxiang")
    {
        events << DamageComplete;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (player->isAlive() && damage.transfer && damage.transfer_reason == "tianxiang")
            player->drawCards(player->getLostHp(), objectName());
        return false;
    }
};

static QHash<QString,GuhuoDialog *> GuhuoDialogs;

GuhuoDialog *GuhuoDialog::getInstance(const QString &object, bool left, bool right, bool play_only, bool slash_combined, bool delayed_tricks, bool update)
{
    if(GuhuoDialogs[object]==nullptr||update){
		if(GuhuoDialogs[object]){
			foreach (Card *c, GuhuoDialogs[object]->findChildren<Card *>())
				delete c;
			delete GuhuoDialogs[object];
		}
		GuhuoDialogs[object] = new GuhuoDialog(object,left,right,play_only,slash_combined,delayed_tricks);
	}
	/*static GuhuoDialog *instance;
    if (instance==nullptr || instance->objectName() != object || update)
		instance = new GuhuoDialog(object,left,right,play_only,slash_combined,delayed_tricks);
	*/
    return GuhuoDialogs[object];
}

GuhuoDialog::GuhuoDialog(const QString &object, bool left, bool right, bool play_only, bool slash_combined, bool delayed_tricks)
    : play_only(play_only), slash_combined(slash_combined), delayed_tricks(delayed_tricks)
{
    setObjectName(object);
    setWindowTitle(Sanguosha->translate(object));
    group = new QButtonGroup(this);

    QHBoxLayout *layout = new QHBoxLayout;
    if (left) layout->addWidget(createLeft());
    if (right) layout->addWidget(createRight());
    setLayout(layout);

    connect(group, SIGNAL(buttonClicked(QAbstractButton *)), this, SLOT(selectCard(QAbstractButton *)));
}

bool GuhuoDialog::isButtonEnabled(const QString &button_name) const
{
    QString str = objectName()+"_guhuo_remove_"+button_name;
	foreach (QString m, Self->getMarkNames()) {
		if (Self->getMark(m)>0&&m.contains(str))
			return false;
	}
	if (play_only||Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_PLAY){
		if(!map[button_name]->isAvailable(Self))
			return false;
	}else if (Self->isLocked(map[button_name]))
		return false;

    if (objectName() == "dunshi") {
        if (map[button_name]->isKindOf("Slash") && Self->getMark("dunshi_used_slash") > 0) return false;
        return Self->getMark("dunshi_used_" + button_name)<1;
    }else if (objectName() == "mtzhihe") {
        foreach (int id, Self->getPile("yhjyye")) {
            if (Sanguosha->getCard(id)->objectName() == button_name)
                return true;
        }
        return false;
    }else if (objectName() == "fuping") {
        return Self->property("SkillDescriptionRecord_fuping").toString().split("+").contains(button_name);
    }else if (objectName() == "fengying") {
        return Self->property("SkillDescriptionRecord_fengying").toString().split("+").contains(button_name);
    }else if (objectName().endsWith("jingong")) {
        return Self->property((objectName()+"_tricks").toStdString().c_str()).toString().split("+").contains(button_name);
    }

    str = Self->property("allowed_guhuo_dialog_buttons").toString();
	return str.isEmpty() || str.split("+").contains(button_name);
}

void GuhuoDialog::popup()
{
    Self->removeTag(objectName());/*
    if (objectName() == "zhanyi" && Self->getMark("ViewAsSkill_zhanyiEffect")<1) {
        emit onButtonClick();
        return;
    }else if (objectName() == "secondzhanyi" && Self->getMark("ViewAsSkill_secondzhanyiEffect")<1) {
        emit onButtonClick();
        return;
    }*/
    if (play_only && Sanguosha->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_PLAY) {
        emit onButtonClick();
        return;
    }

    bool has_enabled_button = false;
    foreach (QAbstractButton *button, group->buttons()) {
        bool enabled = isButtonEnabled(button->objectName());
        if (enabled) has_enabled_button = true;
        button->setEnabled(enabled);
    }
    if (has_enabled_button) {
		exec();
    }else
        emit onButtonClick();
}

void GuhuoDialog::selectCard(QAbstractButton *button)
{
    Self->setTag(objectName(), QVariant::fromValue(map[button->objectName()]));
    if (button->objectName().contains("slash")) {
        if (objectName() == "guhuo")
            Self->setTag("GuhuoSlash", button->objectName());
        else if (objectName() == "nosguhuo")
            Self->setTag("NosGuhuoSlash", button->objectName());
        else if (objectName() == "olguhuo")
            Self->setTag("OLGuhuoSlash", button->objectName());
        else if (objectName() == "zhanyi")
            Self->setTag("ZhanyiSlash", button->objectName());
        else if (objectName() == "yizan")
            Self->setTag("YizanSlash", button->objectName());
    }
    emit onButtonClick();
    accept();
}

QGroupBox *GuhuoDialog::createLeft()
{
    QVBoxLayout *layout = new QVBoxLayout;

    if (objectName() == "fuping") {
        foreach (QString rec, Self->property("SkillDescriptionRecord_fuping").toString().split("+")) {
            Card *dc = Sanguosha->cloneCard(rec);
            if (dc){
				if(dc->isKindOf("BasicCard")) layout->addWidget(createButton(dc));
				else delete dc;
			}
        }
    } else if (objectName() == "wuxinghelingshan") {
		static QList<const NatureSlash *>cards = Sanguosha->findChildren<const NatureSlash *>();
        foreach (const Card *c, cards) {
            if (c->objectName().startsWith("_")||map.contains(c->objectName())||ServerInfo.BanPackages.contains(c->getPackage())) continue;
			layout->addWidget(createButton(Sanguosha->cloneCard(c->objectName())));
        }
    } else if (objectName() == "dunshi") {
        QStringList cards;
        cards << "slash" << "jink" << "peach" << "analeptic";
        foreach (QString cn, cards)
            layout->addWidget(createButton(Sanguosha->cloneCard(cn)));
    } else if (objectName() == "fengying") {
        foreach (QString rec, Self->property("SkillDescriptionRecord_fengying").toString().split("+")) {
            Card *dc = Sanguosha->cloneCard(rec);
            if (dc){
				if(dc->isKindOf("BasicCard")) layout->addWidget(createButton(dc));
				else delete dc;
			}
        }
    } else if (objectName() == "tenyeargue") {
		static QList<const Slash *>cards = Sanguosha->findChildren<const Slash *>();
        foreach (const Card *c, cards) {
            if (c->objectName().startsWith("_")||map.contains(c->objectName())||ServerInfo.BanPackages.contains(c->getPackage())) continue;
			layout->addWidget(createButton(Sanguosha->cloneCard(c->objectName())));
        }
    } else {
		static QList<const BasicCard *>cards = Sanguosha->findChildren<const BasicCard *>();
        foreach (const Card *c, cards) {
            if (c->objectName().startsWith("_")||ServerInfo.BanPackages.contains(c->getPackage())
			||map.contains(c->objectName())||(slash_combined&&c->isKindOf("Slash")&&c->objectName()!="slash")) continue;
			Card *dc = Sanguosha->cloneCard(c->objectName());
			layout->addWidget(createButton(dc));
			if (c->objectName()=="slash"&&!slash_combined) {
				dc->setObjectName("normal_slash");
				layout->addWidget(createButton(dc));
            }
        }
    }

    QGroupBox *box = new QGroupBox;
    box->setTitle(Sanguosha->translate("basic"));
    if(layout->count()>0){
		layout->addStretch();
		box->setLayout(layout);
	}
    return box;
}

QGroupBox *GuhuoDialog::createRight()
{
    QGroupBox *box1 = new QGroupBox(Sanguosha->translate("single_target_trick"));
    QVBoxLayout *layout1 = new QVBoxLayout;

    QGroupBox *box2 = new QGroupBox(Sanguosha->translate("multiple_target_trick"));
    QVBoxLayout *layout2 = new QVBoxLayout;

    QGroupBox *box3 = new QGroupBox(Sanguosha->translate("delayed_trick"));
    QVBoxLayout *layout3 = new QVBoxLayout;

    if (objectName() == "fuping") {
        foreach (QString rec, Self->property("SkillDescriptionRecord_fuping").toString().split("+")) {
            Card *dc = Sanguosha->cloneCard(rec);
            if (dc){
				if(dc->isKindOf("TrickCard")){
					if (dc->isKindOf("DelayedTrick"))
						layout3->addWidget(createButton(dc));
					else if (dc->isKindOf("SingleTargetTrick"))
						layout1->addWidget(createButton(dc));
					else
						layout2->addWidget(createButton(dc));
				}else
					delete dc;
			}
        }
    } else if (objectName() == "fengying") {
        foreach (QString rec, Self->property("SkillDescriptionRecord_fengying").toString().split("+")) {
            Card *dc = Sanguosha->cloneCard(rec);
            if (dc){
				if(dc->isKindOf("TrickCard")){
					if (dc->isKindOf("DelayedTrick"))
						layout3->addWidget(createButton(dc));
					else if (dc->isKindOf("SingleTargetTrick"))
						layout1->addWidget(createButton(dc));
					else
						layout2->addWidget(createButton(dc));
				}else
					delete dc;
			}
        }
    } else {
		static QList<const TrickCard *>cards = Sanguosha->findChildren<const TrickCard *>();
        foreach (const Card *c, cards) {
            if (c->objectName().startsWith("_")||map.contains(c->objectName())||ServerInfo.BanPackages.contains(c->getPackage())) continue;
            if (delayed_tricks || c->isNDTrick()){
				Card *dc = Sanguosha->cloneCard(c->objectName());
				if (dc->isKindOf("DelayedTrick")) layout3->addWidget(createButton(dc));
				else if (c->isKindOf("SingleTargetTrick")) layout1->addWidget(createButton(dc));
				else layout2->addWidget(createButton(dc));
			}
        }
        if (objectName().endsWith("jingong")) {
            Card *dc = Sanguosha->cloneCard("__meirenji");
            if (dc) layout1->addWidget(createButton(dc));
            dc = Sanguosha->cloneCard("__xiaolicangdao");
            if (dc) layout1->addWidget(createButton(dc));
        }
    }
    QGroupBox *box = new QGroupBox(Sanguosha->translate("trick"));
    QHBoxLayout *layout = new QHBoxLayout;

    box->setLayout(layout);
    box1->setLayout(layout1);
    box2->setLayout(layout2);
    box3->setLayout(layout3);

    layout1->addStretch();
    layout2->addStretch();
    layout3->addStretch();

    layout->addWidget(box1);
    layout->addWidget(box2);
    if (delayed_tricks)
        layout->addWidget(box3);
    return box;
}

QAbstractButton *GuhuoDialog::createButton(Card *card)
{
	card->setSkillName(objectName());
	card->setCanRecast(false);
	card->setParent(this);
	map.insert(card->objectName(), card);
	QCommandLinkButton *button = new QCommandLinkButton(Sanguosha->translate(card->objectName()));
	button->setToolTip(card->getDescription());
	button->setObjectName(card->objectName());
	group->addButton(button);
	return button;
}

GuhuoCard::GuhuoCard()
{
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool GuhuoCard::guhuo(ServerPlayer *yuji) const
{
    Room *room = yuji->getRoom();

    room->setTag("GuhuoType", user_string);

    ServerPlayer *questioned = nullptr;
    foreach (ServerPlayer *player, room->getOtherPlayers(yuji)) {
        QString choice = "noquestion+question";
        if (player->hasSkill("chanyuan")) {
            room->sendCompulsoryTriggerLog(player, "chanyuan", true, true);
            choice = "noquestion";
        }
        choice = room->askForChoice(player, "guhuo", choice, yuji->objectName()+":"+user_string);
        LogMessage log;
        log.type = "#GuhuoQuery";
        log.from = player;
        log.arg = choice;
        room->sendLog(log);
        if (choice == "question"){
            room->setEmotion(player, "question");
            questioned = player;
            break;
		}else
            room->setEmotion(player, "no-question");
    }

    LogMessage log;
    log.type = "$GuhuoResult";
    log.from = yuji;
    log.card_str = QString::number(subcards.first());
    room->sendLog(log);
    room->addPlayerMark(yuji, "guhuoUsed-Clear");
	foreach(ServerPlayer *player, room->getAlivePlayers())
		room->setEmotion(player, ".");

    bool success = false;
    if (!questioned) {
        success = true;
        CardMoveReason reason(CardMoveReason::S_REASON_USE, yuji->objectName(), "", "guhuo");
        CardsMoveStruct move(subcards, yuji, nullptr, Player::PlaceUnknown, Player::PlaceTable, reason);
        room->moveCardsAtomic(move, true);
    } else {
        const Card *card = Sanguosha->getCard(subcards.first());
        if (user_string == "peach+analeptic")
            success = card->objectName() == yuji->getTag("GuhuoSaveSelf").toString();
        else if (user_string == "slash")
            success = card->objectName().contains("slash");
        else if (user_string == "normal_slash")
            success = card->objectName() == "slash";
        else
            success = card->match(user_string);

        if (success) {
            CardMoveReason reason(CardMoveReason::S_REASON_USE, yuji->objectName(), "", "guhuo");
            CardsMoveStruct move(subcards, yuji, nullptr, Player::PlaceUnknown, Player::PlaceTable, reason);
            room->moveCardsAtomic(move, true);
			room->acquireSkill(questioned, "chanyuan");
        } else {
            room->moveCardTo(this, yuji, nullptr, Player::DiscardPile,
                CardMoveReason(CardMoveReason::S_REASON_PUT, yuji->objectName(), "", "guhuo"), true);
        }
    }
    yuji->removeTag("GuhuoSaveSelf");
    yuji->removeTag("GuhuoSlash");
    return success;
}

bool GuhuoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
        return card && card->targetFilter(targets, to_select, Self);
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return false;
    }

    const Card *_card = Self->getTag("guhuo").value<const Card *>();
    if (_card == nullptr)
        return false;

    Card *card = Sanguosha->cloneCard(_card);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetFilter(targets, to_select, Self);
}

bool GuhuoCard::targetFixed() const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
        return card && card->targetFixed();
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return true;
    }

    const Card *_card = Self->getTag("guhuo").value<const Card *>();
    if (_card == nullptr)
        return false;

    Card *card = Sanguosha->cloneCard(_card);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetFixed();
}

bool GuhuoCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
        return card && card->targetsFeasible(targets, Self);
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return true;
    }

    const Card *_card = Self->getTag("guhuo").value<const Card *>();
    if (_card == nullptr)
        return false;

    Card *card = Sanguosha->cloneCard(_card);
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetsFeasible(targets, Self);
}

const Card *GuhuoCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer *yuji = card_use.from;
    Room *room = yuji->getRoom();

    QString to_guhuo = user_string;
    if ((user_string.contains("slash") || (user_string.contains("Slash")))
        && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        QStringList guhuo_list;
		static QList<const Slash *>cards = Sanguosha->findChildren<const Slash *>();
        foreach (const Slash *slash, cards) {
            QString name = slash->objectName();
            if (guhuo_list.contains(name) || ServerInfo.BanPackages.contains(slash->getPackage())) continue;
            guhuo_list << name;
        }

        if (guhuo_list.isEmpty()) guhuo_list << "slash";
        to_guhuo = room->askForChoice(yuji, "guhuo_slash", guhuo_list.join("+"));
        yuji->setTag("GuhuoSlash", QVariant(to_guhuo));
    }
    room->broadcastSkillInvoke("guhuo");

    LogMessage log;
    log.type = card_use.to.isEmpty() ? "#GuhuoNoTarget" : "#Guhuo";
    log.from = yuji;
    log.to = card_use.to;
    log.arg = to_guhuo;
    log.arg2 = "guhuo";

    room->sendLog(log);

    if (guhuo(card_use.from)) {
        Card *card = Sanguosha->getCard(subcards.first());
		Card *use_card;
		if (to_guhuo == "slash") {
			if (card->isKindOf("Slash"))
				to_guhuo = card->objectName();
		} else if (to_guhuo == "normal_slash")
			to_guhuo = "slash";
		if (to_guhuo.startsWith(card->objectName()))
			use_card = card;
		else{
			use_card = Sanguosha->cloneCard(to_guhuo, card->getSuit(), card->getNumber());
			use_card->setSkillName("guhuo");
			use_card->addSubcard(subcards.first());
			use_card->deleteLater();
		}
        foreach (ServerPlayer *to, card_use.to) {
            const Skill *skill = room->isProhibited(card_use.from, to, use_card);
            if (skill) {
				log.type = "#SkillAvoid";
				log.from = to;
                if (skill->isVisible()) {
                    log.arg = skill->objectName();
                    log.arg2 = use_card->objectName();
                    room->sendLog(log);

                    room->broadcastSkillInvoke(skill->objectName());
                    room->notifySkillInvoked(to, skill->objectName());
                } else {
                    skill = Sanguosha->getMainSkill(skill->objectName());
                    if (skill && skill->isVisible()) {
						log.arg = skill->objectName();
						log.arg2 = objectName();
                        if (to->hasSkill(skill)) {
                            room->sendLog(log);

                            room->broadcastSkillInvoke(skill->objectName());
                            room->notifySkillInvoked(to, skill->objectName());
                        } else if (yuji->hasSkill(skill)) {
                            log.type = "#SkillAvoidFrom";
                            log.from = yuji;
                            log.to.clear();
                            log.to << to;
                            room->sendLog(log);

                            room->broadcastSkillInvoke(skill->objectName());
                            room->notifySkillInvoked(yuji, skill->objectName());
                        }
                    }
                }
                card_use.to.removeOne(to);
            }
        }
        return use_card;
    }
	return nullptr;
}

const Card *GuhuoCard::validateInResponse(ServerPlayer *yuji) const
{
    Room *room = yuji->getRoom();
    room->broadcastSkillInvoke("guhuo");

    QString to_guhuo;
    if (user_string == "peach+analeptic") {
        QStringList guhuo_list;
		static QList<const Peach *>Peachs = Sanguosha->findChildren<const Peach *>();
        foreach (const Peach *peach, Peachs) {
            QString name = peach->objectName();
            if (guhuo_list.contains(name) || ServerInfo.BanPackages.contains(peach->getPackage())) continue;
            guhuo_list << name;
            break;
        }
		static QList<const Analeptic *> anas = Sanguosha->findChildren<const Analeptic *>();
        foreach (const Analeptic *ana, anas) {
            QString name = ana->objectName();
            if (guhuo_list.contains(name) || ServerInfo.BanPackages.contains(ana->getPackage())) continue;
            guhuo_list << name;
            break;
        }

        if (guhuo_list.isEmpty())
            guhuo_list << "peach";
        to_guhuo = room->askForChoice(yuji, "guhuo_saveself", guhuo_list.join("+"));
        yuji->setTag("GuhuoSaveSelf", QVariant(to_guhuo));
    } else if (user_string.contains("slash") || user_string.contains("Slash")) {
        QStringList guhuo_list;
		static QList<const Slash *> slashs = Sanguosha->findChildren<const Slash *>();
        foreach (const Slash *slash, slashs) {
            QString name = slash->objectName();
            if (guhuo_list.contains(name) || ServerInfo.BanPackages.contains(slash->getPackage())) continue;
            guhuo_list << name;
        }

        if (guhuo_list.isEmpty())
            guhuo_list << "slash";
        to_guhuo = room->askForChoice(yuji, "guhuo_slash", guhuo_list.join("+"));
        yuji->setTag("GuhuoSlash", QVariant(to_guhuo));
    } else
        to_guhuo = user_string;

    LogMessage log;
    log.type = "#GuhuoNoTarget";
    log.from = yuji;
    log.arg = to_guhuo;
    log.arg2 = "guhuo";
    room->sendLog(log);

    if (guhuo(yuji)) {
        Card *card = Sanguosha->getCard(subcards.first());
		if (to_guhuo == "slash") {
			if (card->isKindOf("Slash"))
				to_guhuo = card->objectName();
		} else if (to_guhuo == "normal_slash")
			to_guhuo = "slash";
		if (to_guhuo.startsWith(card->objectName()))
			return card;
		else{
			if (to_guhuo == "slash") {
				if (card->isKindOf("Slash"))
					to_guhuo = card->objectName();
			} else if (to_guhuo == "normal_slash")
				to_guhuo = "slash";
			Card *use_card = Sanguosha->cloneCard(to_guhuo, card->getSuit(), card->getNumber());
			use_card->setSkillName("guhuo");
			use_card->addSubcard(subcards.first());
			use_card->deleteLater();
			return use_card;
		}
    }
	return nullptr;
}

class Guhuo : public OneCardViewAsSkill
{
public:
    Guhuo() : OneCardViewAsSkill("guhuo")
    {
        filter_pattern = ".|.|.|hand";
        response_or_use = true;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        if (player->isKongcheng() || player->getMark("guhuoUsed-Clear")>0
            || pattern.startsWith(".") || pattern.startsWith("@"))
            return false;
        foreach (QString cn, pattern.split("+")) {
            Card *c = Sanguosha->cloneCard(cn);
			if (c) {
                c->deleteLater();
				if(c->isKindOf("BasicCard")||c->isNDTrick())
					return player->hasTurn();
            }
        }
        return false;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->isKongcheng() && player->getMark("guhuoUsed-Clear")<1;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE
            || Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
            GuhuoCard *card = new GuhuoCard;
            card->setUserString(Sanguosha->currentRoomState()->getCurrentCardUsePattern());
            card->addSubcard(originalCard);
            return card;
        }

        const Card *c = Self->getTag("guhuo").value<const Card *>();
        if (c) {
            GuhuoCard *card = new GuhuoCard;
            if (!c->objectName().contains("slash"))
                card->setUserString(c->objectName());
            else
                card->setUserString(Self->getTag("GuhuoSlash").toString());
            card->addSubcard(originalCard);
            return card;
        }
		return nullptr;
    }

    QDialog *getDialog() const
    {
        return GuhuoDialog::getInstance("guhuo");
    }

    int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        if (!card->isKindOf("GuhuoCard"))
            return -2;
		return -1;
    }

    bool isEnabledAtNullification(const ServerPlayer *player) const
    {
        return player->hasTurn()&&(!player->isKongcheng() || !player->getHandPile().isEmpty()) && player->getMark("guhuoUsed-Clear")<1;
    }
};

class Chanyuan : public TriggerSkill
{
public:
    Chanyuan() : TriggerSkill("chanyuan")
    {
        events << GameStart << HpChanged << MaxHpChanged << EventAcquireSkill << EventLoseSkill;
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
        if (triggerEvent == EventLoseSkill) {
            if (data.toString() != objectName()) return false;
            room->removePlayerMark(player, "@chanyuan");
        } else if (triggerEvent == EventAcquireSkill) {
            if (data.toString() != objectName()) return false;
            room->addPlayerMark(player, "@chanyuan");
        }
        if (triggerEvent != EventLoseSkill && !player->hasSkill(objectName())) return false;

        foreach(ServerPlayer *p, room->getOtherPlayers(player))
            room->filterCards(p, p->getCards("he"), true);
        JsonArray args;
        args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
        return false;
    }
};

class ChanyuanInvalidity : public InvaliditySkill
{
public:
    ChanyuanInvalidity() : InvaliditySkill("#chanyuan-inv")
    {
    }

    bool isSkillValid(const Player *player, const Skill *skill) const
    {
        return player->getHp() != 1 || skill->objectName() == "chanyuan" || !player->hasSkill("chanyuan");
    }
};

class TenyearLeiji : public TriggerSkill
{
public:
    TenyearLeiji() : TriggerSkill("tenyearleiji")
    {
        events << CardUsed << CardResponded;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        const Card *jink = nullptr;
        if (triggerEvent == CardUsed) jink = data.value<CardUseStruct>().card;
        else jink = data.value<CardResponseStruct>().m_card;

        if (jink && jink->isKindOf("Jink")) {
            QList<ServerPlayer *> others = room->getOtherPlayers(player);
            ServerPlayer *victim = room->askForPlayerChosen(player, others, "tenyearleiji", "@tenyearleiji", true, true);
            if (!victim) return false;

            room->broadcastSkillInvoke("tenyearleiji");
            JudgeStruct judge;
            judge.who = victim;
            judge.pattern = ".|black";
            judge.good = false;
            judge.reason = "tenyearleiji";
            room->judge(judge);

            if (judge.card->getSuit() == Card::Spade) {
                DamageStruct damage;
                damage.from = player;
                damage.to = victim;
                damage.reason = "tenyearleiji";
                damage.damage = 2;
                damage.nature = DamageStruct::Thunder;
                room->damage(damage);
            } else if (judge.card->getSuit() == Card::Club) {
                RecoverStruct recover;
                recover.who = player;
                recover.recover = 1;
                recover.reason = "tenyearleiji";
                room->recover(player, recover);

                DamageStruct damage;
                damage.from = player;
                damage.to = victim;
                damage.reason = "tenyearleiji";
                damage.damage = 1;
                damage.nature = DamageStruct::Thunder;
                room->damage(damage);
            }
        }
        return false;
    }
};

class NosLeiji : public TriggerSkill
{
public:
    NosLeiji() : TriggerSkill("nosleiji")
    {
        events << CardResponded << CardUsed;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *zhangjiao, QVariant &data) const
    {
        const Card *jink = nullptr;
        if (triggerEvent == CardUsed)
            jink = data.value<CardUseStruct>().card;
        else
            jink = data.value<CardResponseStruct>().m_card;
        if (jink && jink->isKindOf("Jink")) {
            ServerPlayer *target = room->askForPlayerChosen(zhangjiao, room->getAlivePlayers(), objectName(), "leiji-invoke", true, true);
            if (target) {
                room->broadcastSkillInvoke("nosleiji");

                JudgeStruct judge;
                judge.pattern = ".|spade";
                judge.good = false;
                judge.negative = true;
                judge.reason = objectName();
                judge.who = target;

                room->judge(judge);

                if (judge.isBad())
                    room->damage(DamageStruct(objectName(), zhangjiao, target, 2, DamageStruct::Thunder));
            }
        }
        return false;
    }
};

class NosJushou : public Jushou
{
public:
    NosJushou() : Jushou()
    {
        setObjectName("nosjushou");
    }

    int getJushouDrawNum(ServerPlayer *) const
    {
        return 3;
    }
};

class NosBuquRemove : public TriggerSkill
{
public:
    NosBuquRemove() : TriggerSkill("#nosbuqu-remove")
    {
        events << HpRecover;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *zhoutai, QVariant &) const
    {
        QList<int> nosbuqu = zhoutai->getPile("nosbuqu");
        if (nosbuqu.isEmpty()) return false;

        int need = 1 - zhoutai->getHp();
        if (need <= 0) {
            // clear all the buqu cards
			LogMessage log;
			log.type = "$NosBuquRemove";
			log.from = zhoutai;
			log.card_str = ListI2S(nosbuqu).join("+");
			room->sendLog(log);
			room->throwCard(nosbuqu, "nosbuqu", nullptr);
        } else {
            QList<int> ids;
			need = nosbuqu.length() - need;
            for (int i = 0; i < need; i++) {
				room->fillAG(nosbuqu,zhoutai);
                int card_id = room->askForAG(zhoutai, nosbuqu, false, "nosbuqu");
                nosbuqu.removeOne(card_id);
                room->clearAG(zhoutai);
				ids << card_id;
            }
			LogMessage log;
			log.type = "$NosBuquRemove";
			log.from = zhoutai;
			log.card_str = ListI2S(ids).join("+");
			room->sendLog(log);
			room->throwCard(ids, "nosbuqu", nullptr);
        }

        return false;
    }
};

class NosBuqu : public TriggerSkill
{
public:
    NosBuqu() : TriggerSkill("nosbuqu")
    {
        events << HpChanged << AskForPeachesDone;
    }

    int getPriority(TriggerEvent triggerEvent) const
    {
        if (triggerEvent == HpChanged) return 1;
        return TriggerSkill::getPriority(triggerEvent);
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *zhoutai, QVariant &data) const
    {
        if(triggerEvent == HpChanged){
			if(data.canConvert<RecoverStruct>()){
			}else if(zhoutai->getHp()<1&&room->askForSkillInvoke(zhoutai, objectName(), data)){
                room->broadcastSkillInvoke(objectName(),-1,zhoutai);
				int need = 1 - zhoutai->getHp(); // the buqu cards that should be turned over
                QList<int> ids,nosbuqu = zhoutai->getPile("nosbuqu");
				need -= nosbuqu.length();
				zhoutai->addToPile("nosbuqu", room->getNCards(need));
                nosbuqu = zhoutai->getPile("nosbuqu");
                foreach (int id1, nosbuqu) {
                    int number = Sanguosha->getCard(id1)->getNumber();
					bool has = false;
					nosbuqu.removeOne(id1);
					foreach (int id2, nosbuqu) {
						if(number==Sanguosha->getCard(id2)->getNumber()){
							ids << id2;
							has = true;
						}
					}
					if(has)
						ids << id1;
				}
				if(ids.isEmpty())
                    return true;
                LogMessage log;
                log.type = "$NosBuquDuplicateItem";
                log.from = zhoutai;
                log.card_str = ListI2S(ids).join("+");
                room->sendLog(log);
			}
		}else if (triggerEvent == AskForPeachesDone) {
			DyingStruct dying = data.value<DyingStruct>();
			if (dying.who!=zhoutai||zhoutai->getHp()>0)
				return false;
			QList<int>nosbuqu = zhoutai->getPile("nosbuqu");
			foreach (int id1, nosbuqu) {
				int number = Sanguosha->getCard(id1)->getNumber();
				nosbuqu.removeOne(id1);
				foreach (int id2, nosbuqu) {
					if(number==Sanguosha->getCard(id2)->getNumber()){
						return false;
					}
				}
			}
			room->setPlayerFlag(zhoutai, "-Global_Dying");
			return true;
        }
        return false;
    }
};

class NosBuquClear : public DetachEffectSkill
{
public:
    NosBuquClear() : DetachEffectSkill("nosbuqu")
    {
    }

    void onSkillDetached(Room *room, ServerPlayer *player) const
    {
        if (player->getHp() <= 0)
            room->enterDying(player, nullptr);
    }
};

NosGuhuoCard::NosGuhuoCard()
{
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool NosGuhuoCard::nosguhuo(ServerPlayer *yuji) const
{
    Room *room = yuji->getRoom();
    QList<ServerPlayer *> questioned;

    room->setTag("NosGuhuoType", user_string);

    foreach (ServerPlayer *player, room->getOtherPlayers(yuji)) {
        QString choice = "question+noquestion";
        if (player->getHp() <= 0) {
            LogMessage log;
            log.type = "#GuhuoCannotQuestion";
            log.from = player;
            log.arg = QString::number(player->getHp());
            room->sendLog(log);
			choice = "noquestion";
        }

        choice = room->askForChoice(player, "nosguhuo", choice, yuji->objectName()+":"+user_string);
        if (choice == "question") {
            room->setEmotion(player, "question");
            questioned << player;
        } else
            room->setEmotion(player, "no-question");

        LogMessage log;
        log.type = "#GuhuoQuery";
        log.from = player;
        log.arg = choice;
        room->sendLog(log);
    }

    LogMessage log;
    log.type = "$GuhuoResult";
    log.from = yuji;
    log.card_str = QString::number(subcards.first());
    room->sendLog(log);
	foreach(ServerPlayer *player, room->getAlivePlayers())
		room->setEmotion(player, ".");

    bool success = questioned.isEmpty();
    if (!success) {
        const Card *card = Sanguosha->getCard(subcards.first());
        bool real = card->match(user_string);
        if (user_string == "peach+analeptic")
            real = card->objectName() == yuji->getTag("NosGuhuoSaveSelf").toString();
        else if (user_string == "slash")
            real = card->objectName().contains("slash");
        else if (user_string == "normal_slash")
            real = card->objectName() == "slash";

        success = real && card->getSuit() == Card::Heart;
        if (!success) {
            room->moveCardTo(this, yuji, nullptr, Player::DiscardPile,
                CardMoveReason(CardMoveReason::S_REASON_PUT, yuji->objectName(), "", "nosguhuo"), true);
        }
        foreach (ServerPlayer *player, questioned) {
			if (real) room->loseHp(HpLostStruct(player, 1, "nosguhuo", yuji));
            else player->drawCards(1, "nosguhuo");
        }
    }
    if (success) {
        CardMoveReason reason(CardMoveReason::S_REASON_USE, yuji->objectName(), "", "nosguhuo");
        CardsMoveStruct move(getSubcards(), yuji, nullptr, Player::PlaceUnknown, Player::PlaceTable, reason);
        room->moveCardsAtomic(move, true);
    }
    yuji->removeTag("NosGuhuoSaveSelf");
    yuji->removeTag("NosGuhuoSlash");
    return success;
}

bool NosGuhuoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
        return card && card->targetFilter(targets, to_select, Self);
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return false;
    }

    const Card *_card = Self->getTag("nosguhuo").value<const Card *>();
    if (_card == nullptr)
        return false;

    Card *card = Sanguosha->cloneCard(_card);
    card->setCanRecast(false);
    card->deleteLater();
    return card->targetFilter(targets, to_select, Self);
}

bool NosGuhuoCard::targetFixed() const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
        return card && card->targetFixed();
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return true;
    }

    const Card *_card = Self->getTag("nosguhuo").value<const Card *>();
    if (_card == nullptr)
        return false;

    Card *card = Sanguosha->cloneCard(_card);
    card->setCanRecast(false);
    card->deleteLater();
    return card->targetFixed();
}

bool NosGuhuoCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
        return card && card->targetsFeasible(targets, Self);
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return true;
    }

    const Card *_card = Self->getTag("nosguhuo").value<const Card *>();
    if (_card == nullptr)
        return false;

    Card *card = Sanguosha->cloneCard(_card);
    card->setCanRecast(false);
    card->deleteLater();
    return card->targetsFeasible(targets, Self);
}

const Card *NosGuhuoCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer *yuji = card_use.from;
    Room *room = yuji->getRoom();

    QString to_nosguhuo = user_string;
    if ((user_string.contains("slash") || (user_string.contains("Slash")))
        && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        QStringList nosguhuo_list;
		static QList<const Slash *> slashs = Sanguosha->findChildren<const Slash *>();
        foreach (const Slash *slash, slashs) {
            QString name = slash->objectName();
            if (nosguhuo_list.contains(name) || ServerInfo.BanPackages.contains(slash->getPackage())) continue;
            nosguhuo_list << name;
        }

        if (nosguhuo_list.isEmpty())
            nosguhuo_list << "slash";
        to_nosguhuo = room->askForChoice(yuji, "nosguhuo_slash", nosguhuo_list.join("+"));
        yuji->setTag("NosGuhuoSlash", QVariant(to_nosguhuo));
    }
    room->broadcastSkillInvoke("nosguhuo");

    LogMessage log;
    log.type = card_use.to.isEmpty() ? "#GuhuoNoTarget" : "#Guhuo";
    log.from = yuji;
    log.to = card_use.to;
    log.arg = to_nosguhuo;
    log.arg2 = "nosguhuo";

    room->sendLog(log);

    if (nosguhuo(card_use.from)) {
        Card *card = Sanguosha->getCard(subcards.first());
		if (to_nosguhuo == "slash") {
			if (card->isKindOf("Slash"))
				to_nosguhuo = card->objectName();
		} else if (to_nosguhuo == "normal_slash")
			to_nosguhuo = "slash";
		if (to_nosguhuo.startsWith(card->objectName()))
			return card;
		else{
			Card *use_card = Sanguosha->cloneCard(to_nosguhuo, card->getSuit(), card->getNumber());
			use_card->setSkillName("guhuo");
			use_card->addSubcard(subcards.first());
			use_card->deleteLater();
			return use_card;
		}
    }
	return nullptr;
}

const Card *NosGuhuoCard::validateInResponse(ServerPlayer *yuji) const
{
    Room *room = yuji->getRoom();
    room->broadcastSkillInvoke("nosguhuo");

    QString to_nosguhuo;
    if (user_string == "peach+analeptic") {
        QStringList nosguhuo_list;
        static QList<const Peach *> peachs = Sanguosha->findChildren<const Peach *>();
        foreach (const Peach *peach, peachs) {
            QString name = peach->objectName();
            if (nosguhuo_list.contains(name) || ServerInfo.BanPackages.contains(peach->getPackage())) continue;
            nosguhuo_list << name;
            break;
        }
        static QList<const Analeptic *> anas = Sanguosha->findChildren<const Analeptic *>();
        foreach (const Analeptic *ana, anas) {
            QString name = ana->objectName();
            if (nosguhuo_list.contains(name) || ServerInfo.BanPackages.contains(ana->getPackage())) continue;
            nosguhuo_list << name;
            break;
        }

        if (nosguhuo_list.isEmpty())
            nosguhuo_list << "peach";
        to_nosguhuo = room->askForChoice(yuji, "nosguhuo_saveself", nosguhuo_list.join("+"));
        yuji->setTag("NosGuhuoSaveSelf", QVariant(to_nosguhuo));
    } else if (user_string.contains("slash") || user_string.contains("Slash")) {
        QStringList nosguhuo_list;
        static QList<const Slash *> slashs = Sanguosha->findChildren<const Slash *>();
        foreach (const Slash *slash, slashs) {
            QString name = slash->objectName();
            if (nosguhuo_list.contains(name) || ServerInfo.BanPackages.contains(slash->getPackage())) continue;
            nosguhuo_list << name;
        }

        if (nosguhuo_list.isEmpty())
            nosguhuo_list << "slash";
        to_nosguhuo = room->askForChoice(yuji, "nosguhuo_slash", nosguhuo_list.join("+"));
        yuji->setTag("NosGuhuoSlash", QVariant(to_nosguhuo));
    } else
        to_nosguhuo = user_string;

    LogMessage log;
    log.type = "#GuhuoNoTarget";
    log.from = yuji;
    log.arg = to_nosguhuo;
    log.arg2 = "nosguhuo";
    room->sendLog(log);

    if (nosguhuo(yuji)) {
        Card *card = Sanguosha->getCard(subcards.first());
		if (to_nosguhuo == "slash") {
			if (card->isKindOf("Slash"))
				to_nosguhuo = card->objectName();
		} else if (to_nosguhuo == "normal_slash")
			to_nosguhuo = "slash";
		if (to_nosguhuo.startsWith(card->objectName()))
			return card;
		else{
			Card *use_card = Sanguosha->cloneCard(to_nosguhuo, card->getSuit(), card->getNumber());
			use_card->setSkillName("guhuo");
			use_card->addSubcard(subcards.first());
			use_card->deleteLater();
			return use_card;
		}
    }
	return nullptr;
}

class NosGuhuo : public OneCardViewAsSkill
{
public:
    NosGuhuo() : OneCardViewAsSkill("nosguhuo")
    {
        filter_pattern = ".|.|.|hand";
        response_or_use = true;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        if (player->isKongcheng() || pattern.startsWith(".") || pattern.startsWith("@")) return false;
        foreach (QString cn, pattern.split("+")) {
            Card *c = Sanguosha->cloneCard(cn);
			if (c) {
                c->deleteLater();
				if(c->isKindOf("BasicCard")||c->isNDTrick())
					return true;
            }
        }
        return false;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->isKongcheng();
    }

    const Card *viewAs(const Card *originalCard) const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE
            || Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
            NosGuhuoCard *card = new NosGuhuoCard;
            card->setUserString(Sanguosha->currentRoomState()->getCurrentCardUsePattern());
            card->addSubcard(originalCard);
            return card;
        }

        const Card *c = Self->getTag("nosguhuo").value<const Card *>();
        if (c) {
            NosGuhuoCard *card = new NosGuhuoCard;
			card->setUserString(c->objectName());
            if (c->objectName().contains("slash"))
                card->setUserString(Self->getTag("NosGuhuoSlash").toString());
            card->addSubcard(originalCard);
            return card;
        }
		return nullptr;
    }

    QDialog *getDialog() const
    {
        return GuhuoDialog::getInstance("nosguhuo");
    }

    int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        if (!card->isKindOf("NosGuhuoCard"))
            return -2;
        return -1;
    }

    bool isEnabledAtNullification(const ServerPlayer *player) const
    {
        return !player->isKongcheng() && !player->getHandPile().isEmpty();
    }
};

class Wushen : public FilterSkill
{
public:
    Wushen() : FilterSkill("wushen")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->getSuit() == Card::Heart
		&&Sanguosha->getCardPlace(to_select->getId()) == Player::PlaceHand;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->setSkillName(objectName());/*
        WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
        card->takeOver(slash);*/
        return slash;
    }
};

class WushenTargetMod : public TargetModSkill
{
public:
    WushenTargetMod() : TargetModSkill("#wushen-target")
    {
    }

    int getDistanceLimit(const Player *from, const Card *card, const Player *) const
    {
        if (card->getSuit() == Card::Heart&&from->hasSkill("wushen"))
            return 1000;
        return 0;
    }
};

class Wuhun : public TriggerSkill
{
public:
    Wuhun() : TriggerSkill("wuhun")
    {
        events << DamageDone << Death;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->hasSkill(objectName());
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from && damage.from != player) {
				room->sendCompulsoryTriggerLog(player, objectName());
				int index = qrand() % 2 + 4;
				if (player->getGeneralName() == "shenguanyu" || (player->getGeneral2() && player->getGeneral2Name() == "shenguanyu"))
					index = 1;
				room->broadcastSkillInvoke(objectName(), index);
				damage.from->gainMark("&nightmare+#"+player->objectName(), damage.damage);
			}
		}else{
			DeathStruct death = data.value<DeathStruct>();
			if (death.who != player) return false;
		
			int max = 0;
			foreach(ServerPlayer *p, room->getOtherPlayers(player))
				max = qMax(max, p->getMark("&nightmare+#"+player->objectName()));
			if (max == 0) return false;
	
			QList<ServerPlayer *> foes;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getMark("&nightmare+#"+player->objectName()) >= max)
					foes << p;
			}
			room->sendCompulsoryTriggerLog(player, this, 4);
			ServerPlayer *foe = room->askForPlayerChosen(player, foes, "wuhun", "@wuhun-revenge");

			JudgeStruct judge;
			judge.pattern = "Peach,GodSalvation";
			judge.good = true;
			judge.negative = true;
			judge.reason = "wuhun";
			judge.who = foe;
	
			room->judge(judge);
	
			if (judge.isBad()) {
				int index = qrand() % 2 + 4;
				if (player->getGeneralName() == "shenguanyu" || (player->getGeneral2() && player->getGeneral2Name() == "shenguanyu"))
					index = 2;
				room->broadcastSkillInvoke("wuhun", index);
				room->doSuperLightbox(player, "wuhun");
	
				LogMessage log;
				log.type = "#WuhunRevenge";
				log.from = player;
				log.to << foe;
				log.arg = QString::number(max);
				log.arg2 = "wuhun";
				room->sendLog(log);
	
				room->killPlayer(foe);
			} else {
				int index = qrand() % 2 + 4;
				if (player->getGeneralName() == "shenguanyu" || (player->getGeneral2() && player->getGeneral2Name() == "shenguanyu"))
					index = 3;
				room->broadcastSkillInvoke("wuhun", index);
			}
			foreach(ServerPlayer *p, room->getAllPlayers())
				p->loseAllMarks("&nightmare+#"+player->objectName());
		}
        return false;
    }
};

static bool CompareBySuit(int card1, int card2)
{
    const Card *c1 = Sanguosha->getCard(card1);
    const Card *c2 = Sanguosha->getCard(card2);

    int a = static_cast<int>(c1->getSuit());
    int b = static_cast<int>(c2->getSuit());

    return a < b;
}

class OLWushen : public FilterSkill
{
public:
    OLWushen() : FilterSkill("olwushen")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->getSuit() == Card::Heart
		&& Sanguosha->getCardPlace(to_select->getId()) == Player::PlaceHand;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->setSkillName(objectName());/*
        WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
        card->takeOver(slash);*/
        return slash;
    }
};

class OLWushenTargetMod : public TargetModSkill
{
public:
    OLWushenTargetMod() : TargetModSkill("#olwushen-target")
    {
    }

    int getResidueNum(const Player *from, const Card *card, const Player *) const
    {
        if (card->getSuit() == Card::Heart&&from->hasSkill("olwushen"))
            return 1000;
        return 0;
    }

    int getDistanceLimit(const Player *from, const Card *card, const Player *) const
    {
        if (card->getSuit() == Card::Heart&&from->hasSkill("olwushen"))
            return 1000;
        return 0;
    }
};

class OLWushenSlash : public TriggerSkill
{
public:
    OLWushenSlash() : TriggerSkill("#olwushen-slash")
    {
        events << TargetSpecified;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("Slash") || use.card->getSuit() != Card::Heart) return false;
        use.no_respond_list << "_ALL_TARGETS";
        LogMessage log;
        log.type = "#OLwushenSlash";
        log.from = player;
        log.arg = "olwushen";
        room->sendLog(log);
        //room->broadcastSkillInvoke("olwushen");
        //room->notifySkillInvoked(player, "olwushen");
        data = QVariant::fromValue(use);
        return false;
    }
};

class Shelie : public PhaseChangeSkill
{
public:
    Shelie() : PhaseChangeSkill("shelie")
    {
    }

    bool onPhaseChange(ServerPlayer *shenlvmeng, Room *room) const
    {
        if (shenlvmeng->getPhase() != Player::Draw)
            return false;

        if (!shenlvmeng->askForSkillInvoke(this))
            return false;

        room->broadcastSkillInvoke(objectName());

        QList<int> card_ids = room->getNCards(5);
        std::sort(card_ids.begin(), card_ids.end(), CompareBySuit);
        room->fillAG(card_ids);

        QList<int> to_get, to_throw;
        while (!card_ids.isEmpty()) {
            int card_id = room->askForAG(shenlvmeng, card_ids, false, "shelie");
            card_ids.removeOne(card_id);
            to_get << card_id;
            // throw the rest cards that matches the same suit
            const Card *card = Sanguosha->getCard(card_id);
            Card::Suit suit = card->getSuit();

            room->takeAG(shenlvmeng, card_id, false);

            foreach (int id, card_ids) {
                const Card *c = Sanguosha->getCard(id);
                if (c->getSuit() == suit) {
                    card_ids.removeOne(id);
                    room->takeAG(nullptr, id, false);
                    to_throw.append(id);
                }
            }
        }
		room->getThread()->delay();
        room->clearAG();
        DummyCard *dummy = new DummyCard;
        if (!to_get.isEmpty()) {
            dummy->addSubcards(to_get);
            shenlvmeng->obtainCard(dummy);
        }
        dummy->clearSubcards();
        if (!to_throw.isEmpty()) {
            dummy->addSubcards(to_throw);
            CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, shenlvmeng->objectName(), objectName(), "");
            room->throwCard(dummy, reason, nullptr);
        }
        delete dummy;
        return true;
    }
};

GongxinCard::GongxinCard()
{
}

bool GongxinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void GongxinCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if (!effect.to->isKongcheng()) {
        QList<int> ids;
        foreach (const Card *card, effect.to->getHandcards()) {
            if (card->getSuit() == Card::Heart)
                ids << card->getEffectiveId();
        }

        int card_id = room->doGongxin(effect.from, effect.to, ids);
        if (card_id == -1) return;

        QString result = room->askForChoice(effect.from, "gongxin", "discard+put");
        effect.from->removeTag("gongxin");
        if (result == "discard") {
            CardMoveReason reason(CardMoveReason::S_REASON_DISMANTLE, effect.from->objectName(), "", "gongxin", "");
            room->throwCard(Sanguosha->getCard(card_id), reason, effect.to, effect.from);
        } else {
            effect.from->setFlags("Global_GongxinOperator");
            CardMoveReason reason(CardMoveReason::S_REASON_PUT, effect.from->objectName(), "", "gongxin", "");
            room->moveCardTo(Sanguosha->getCard(card_id), effect.to, nullptr, Player::DrawPile, reason, true);
            effect.from->setFlags("-Global_GongxinOperator");
        }
    }
}

class Gongxin : public ZeroCardViewAsSkill
{
public:
    Gongxin() : ZeroCardViewAsSkill("gongxin")
    {
    }

    const Card *viewAs() const
    {
        return new GongxinCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("GongxinCard");
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qrand() % 2 + 1;
        if (!player->hasInnateSkill(this))
            index += 2;
        return index;
    }
};

WindPackage::WindPackage()
    :Package("wind")
{
    General *xiahouyuan = new General(this, "xiahouyuan", "wei"); // WEI 008
    xiahouyuan->addSkill(new Shensu);
    xiahouyuan->addSkill(new SlashNoDistanceLimitSkill("shensu"));
    related_skills.insertMulti("shensu", "#shensu-slash-ndl");

    General *noscaoren = new General(this, "nos_caoren", "wei");
    noscaoren->addSkill(new NosJushou);

    General *caoren = new General(this, "caoren", "wei"); // WEI 011
    caoren->addSkill(new Jushou);
    caoren->addSkill(new Jiewei);

    General *huangzhong = new General(this, "huangzhong", "shu"); // SHU 008
    huangzhong->addSkill(new Liegong);

    General *weiyan = new General(this, "weiyan", "shu"); // SHU 009
    weiyan->addSkill(new Kuanggu);
    weiyan->addSkill(new KuangguRecord);
    related_skills.insertMulti("kuanggu", "#kuanggu-record");

    General *xiaoqiao = new General(this, "xiaoqiao", "wu", 3, false); // WU 011
    xiaoqiao->addSkill(new Tianxiang);
    xiaoqiao->addSkill(new TianxiangDraw);
    xiaoqiao->addSkill(new Hongyan);
    related_skills.insertMulti("tianxiang", "#tianxiang");

    General *nos_zhoutai = new General(this, "nos_zhoutai", "wu");
    nos_zhoutai->addSkill(new NosBuqu);
    nos_zhoutai->addSkill(new NosBuquRemove);
    nos_zhoutai->addSkill(new NosBuquClear);
    related_skills.insertMulti("nosbuqu", "#nosbuqu-remove");
    related_skills.insertMulti("nosbuqu", "#nosbuqu-clear");

    General *zhoutai = new General(this, "zhoutai", "wu"); // WU 013
    zhoutai->addSkill(new Buqu);
    zhoutai->addSkill(new BuquMaxCards);
    zhoutai->addSkill(new Fenji);
    related_skills.insertMulti("buqu", "#buqu");

    General *nos_zhangjiao = new General(this, "nos_zhangjiao$", "qun", 3);
    nos_zhangjiao->addSkill(new NosLeiji);
    nos_zhangjiao->addSkill("guidao");
    nos_zhangjiao->addSkill("huangtian");

    General *zhangjiao = new General(this, "zhangjiao$", "qun", 3); // QUN 010
    zhangjiao->addSkill(new Leiji);
    zhangjiao->addSkill(new Guidao);
    zhangjiao->addSkill(new Huangtian);

    General *tenyear_zhangjiao = new General(this, "tenyear_zhangjiao$", "qun", 3);
    tenyear_zhangjiao->addSkill(new TenyearLeiji);
    tenyear_zhangjiao->addSkill("guidao");
    tenyear_zhangjiao->addSkill("huangtian");

    General *nos_yuji = new General(this, "nos_yuji", "qun", 3);
    nos_yuji->addSkill(new NosGuhuo);

    addMetaObject<NosGuhuoCard>();

    General *yuji = new General(this, "yuji", "qun", 3); // QUN 011
    yuji->addSkill(new Guhuo);
    yuji->addRelateSkill("chanyuan");

    General *shenguanyu = new General(this, "shenguanyu", "god", 5); // LE 001
    shenguanyu->addSkill(new Wushen);
    shenguanyu->addSkill(new WushenTargetMod);
    shenguanyu->addSkill(new Wuhun);
    related_skills.insertMulti("wushen", "#wushen-target");

    General *ol_shenguanyu = new General(this, "ol_shenguanyu", "god", 5);
    ol_shenguanyu->addSkill(new OLWushen);
    ol_shenguanyu->addSkill(new OLWushenTargetMod);
    ol_shenguanyu->addSkill(new OLWushenSlash);
    ol_shenguanyu->addSkill("wuhun");
    related_skills.insertMulti("olwushen", "#olwushen-target");
    related_skills.insertMulti("olwushen", "#olwushen-slash");

    General *shenlvmeng = new General(this, "shenlvmeng", "god", 3); // LE 002
    shenlvmeng->addSkill(new Shelie);
    shenlvmeng->addSkill(new Gongxin);
    addMetaObject<GongxinCard>();


    addMetaObject<ShensuCard>();
    addMetaObject<TianxiangCard>();
    addMetaObject<HuangtianCard>();
    addMetaObject<GuhuoCard>();

    skills << new HuangtianViewAsSkill << new Chanyuan << new ChanyuanInvalidity;
    related_skills.insertMulti("chanyuan", "#chanyuan-inv");
}
ADD_PACKAGE(Wind)
