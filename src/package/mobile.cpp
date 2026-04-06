#include "mobile.h"
//#include "client.h"
//#include "general.h"
//#include "skill.h"
//#include "standard-generals.h"
#include "engine.h"
#include "maneuvering.h"
#include "ol.h"
#include "wind.h"
#include "clientplayer.h"
#include "yjcm2013.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "settings.h"

class YingjianVS : public ZeroCardViewAsSkill
{
public:
	YingjianVS() : ZeroCardViewAsSkill("yingjian")
	{
		response_pattern = "@@yingjian";
	}

	const Card *viewAs() const
	{
		Card *slash = Sanguosha->cloneCard("slash");
		slash->setSkillName("yingjian");
		return slash;
	}
};

class Yingjian : public PhaseChangeSkill
{
public:
	Yingjian() : PhaseChangeSkill("yingjian")
	{
		view_as_skill = new YingjianVS;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Start) return false;
		Card *slash = Sanguosha->cloneCard("slash");
		slash->setSkillName("yingjian");
		slash->deleteLater();

		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (player->canSlash(p, slash, false)) {
				room->askForUseCard(player, "@@yingjian", "@yingjian");
				return false;
			}
		}
		return false;
	}
};

class Fenyin : public TriggerSkill
{
public:
	Fenyin() : TriggerSkill("fenyin")
	{
		events << CardUsed;
		frequency = Frequent;
		global = true;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(!player->hasFlag("CurrentPlayer")) return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("SkillCard"))
			return false;

		int n = player->getMark("fenyinColor-Clear");
		player->setMark("fenyinColor-Clear",use.card->getColor()+1);
		if (n>0&&player->hasSkill(objectName())){
			foreach (QString m, player->getMarkNames()) {
				if(m.contains("&fenyin+:+"))
					room->setPlayerMark(player,m,0);
			}
			room->setPlayerMark(player,"&fenyin+:+"+use.card->getColorString()+"-Clear",1);
			n--;
			if(n!=use.card->getColor()&&player->askForSkillInvoke(this, data)){
				room->broadcastSkillInvoke(objectName());
				player->drawCards(1,objectName());
			}
		}
		return false;
	}
};

ShanjiaCard::ShanjiaCard(QString shanjia) : shanjia(shanjia)
{
}

bool ShanjiaCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *slash = Sanguosha->cloneCard("slash");
	slash->setSkillName("_" + shanjia);
	slash->deleteLater();
	return slash->targetFilter(targets, to_select, Self);
}

void ShanjiaCard::onUse(Room *room, CardUseStruct &card_use) const
{
	Card *slash = Sanguosha->cloneCard("slash");
	slash->setSkillName("_" + shanjia);
	slash->deleteLater();

	room->useCard(CardUseStruct(slash, card_use.from, card_use.to), false);
}

OLShanjiaCard::OLShanjiaCard() : ShanjiaCard("olshanjia")
{
}

class ShanjiaViewAsSkill : public ZeroCardViewAsSkill
{
public:
	ShanjiaViewAsSkill(const QString &shanjia) : ZeroCardViewAsSkill(shanjia), shanjia(shanjia)
	{
		response_pattern = "@@" + shanjia;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	const Card *viewAs() const
	{
		if (shanjia == "shanjia")
			return new ShanjiaCard;
		else if (shanjia == "olshanjia")
			return new OLShanjiaCard;
		return nullptr;
	}

private:
	QString shanjia;
};

class Shanjia : public PhaseChangeSkill
{
public:
	Shanjia(const QString &shanjia) : PhaseChangeSkill(shanjia), shanjia(shanjia)
	{
		view_as_skill = new ShanjiaViewAsSkill(shanjia);
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Play) return false;
		if (!player->askForSkillInvoke(objectName())) return false;
		room->broadcastSkillInvoke(objectName());
		player->drawCards(3, objectName());

		int n = 3 - player->getMark("&" + shanjia) - player->getMark(shanjia + "Mark");
		bool flag = true;
		if (n > 0) {
			const Card *card = room->askForDiscard(player, objectName(), n, n , false, true, shanjia + "-discard:" + QString::number(n));
			foreach(int id, card->getSubcards()) {
				const Card *c = Sanguosha->getCard(id);
				if (c->isKindOf("BasicCard") || c->isKindOf("TrickCard")) {
					flag = false;
					break;
				}
			}
		}

		if (flag) {
			Card *slash = Sanguosha->cloneCard("slash");
			slash->setSkillName("_" + shanjia);
			slash->deleteLater();

			foreach(ServerPlayer *p, room->getAlivePlayers()) {
				if (player->canSlash(p, slash, shanjia=="shanjia"?true:false)) {
					room->askForUseCard(player, "@@" + shanjia, "@" + shanjia);
					break;
				}
			}
		}
		return false;
	}

private:
	QString shanjia;
};

PingcaiCard::PingcaiCard()
{
	target_fixed = true;
	mute = true;
}

bool PingcaiCard::isOK(Room *room, const QString &name) const
{
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if (p->getGeneralName().contains(name)||p->getGeneral2Name().contains(name))
			return true;
	}
	return false;
}

bool PingcaiCard::shuijingJudge(Room *room) const
{
	if (isOK(room, "simahui") && room->canMoveField("e"))
		return true;
	else {
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getArmor()){
				foreach (ServerPlayer *p2, room->getOtherPlayers(p)) {
					if (!p2->getArmor() && p2->hasEquipArea(1))
						return true;
				}
			}
		}
	}
	return false;
}

void PingcaiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->broadcastSkillInvoke("pingcai", 1);
	QString choices = "pcwolong+pcfengchu+pcxuanjian";
	if (shuijingJudge(room))
		choices = "pcwolong+pcfengchu+pcshuijing+pcxuanjian";
	QString choice = room->askForChoice(source, "pingcai", choices);
	LogMessage log;
	log.type = "#FumianFirstChoice";
	log.from = source;
	log.arg = choice;
	room->sendLog(log);
	if (source->isDead()) return;

	QList<ServerPlayer *> targets, tos;
	if (choice == "pcwolong") {
		int n = 1;
		if (isOK(room, "wolong")) n++;
		tos = room->askForPlayersChosen(source,room->getAlivePlayers(),"pcwolong",1,n,"pcwolong0:"+QString::number(n));
		room->broadcastSkillInvoke("pingcai", 2);
		foreach (ServerPlayer *p, tos)
			room->doAnimate(1, source->objectName(), p->objectName());
		foreach (ServerPlayer *p, tos)
			room->damage(DamageStruct("pingcai", source, p, 1, DamageStruct::Fire));
	} else if (choice == "pcfengchu") {
		int n = 3;
		if (isOK(room, "pangtong")) n++;
		tos = room->askForPlayersChosen(source,room->getAlivePlayers(),"pcfengchu",1,n,"pcfengchu0:"+QString::number(n));
		room->broadcastSkillInvoke("pingcai", 3);
		foreach (ServerPlayer *p, tos)
			room->doAnimate(1, source->objectName(), p->objectName());
		foreach (ServerPlayer *p, tos) {
			if (p->isChained()) continue;
			room->setPlayerChained(p);
		}
	} else if (choice == "pcshuijing") {
		if (isOK(room, "simahui"))
			room->moveField(source, "pingcai", false, "e");
		else {
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (p->getArmor())
					targets << p;
			}
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (!p->getArmor() && p->hasEquipArea(1))
					tos << p;
			}
			if (targets.isEmpty() || tos.isEmpty()) return;
			ServerPlayer *from = room->askForPlayerChosen(source, targets, "pingcai_shuijing_from", "@pingcai-shuijing");
			const Card *armor = from->getArmor();
			if (!armor) return;
			ServerPlayer *to = room->askForPlayerChosen(source, tos, "pingcai_shuijing_to", "@movefield-to:" + armor->objectName());
			if (!to->hasEquipArea(1) || to->getArmor()) return;
			room->moveCardTo(armor, to, Player::PlaceEquip, true);
		}
		room->broadcastSkillInvoke("pingcai", 4);
	} else {
		ServerPlayer *target = room->askForPlayerChosen(source, room->getAlivePlayers(), "pcxuanjian", "@pingcai-xuanjian");
		room->doAnimate(1, source->objectName(), target->objectName());
		room->broadcastSkillInvoke("pingcai", 5);
		target->drawCards(1, "pingcai");
		room->recover(target, RecoverStruct("pingcai", source));
		if (isOK(room, "xushu"))
			source->drawCards(1, "pingcai");
	}
}

class Pingcai : public ZeroCardViewAsSkill
{
public:
	Pingcai() : ZeroCardViewAsSkill("pingcai")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("PingcaiCard");
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.startsWith("@@pingcai");
	}

	const Card *viewAs() const
	{
		return new PingcaiCard;
	}
};

class Yinshiy : public TriggerSkill
{
public:
	Yinshiy() : TriggerSkill("yinshiy")
	{
		events << EventPhaseChanging;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		Player::Phase phase = data.value<PhaseChangeStruct>().to;
		if (phase != Player::Start && phase != Player::Judge && phase != Player::Finish) return false;
		if (player->isSkipped(phase)) return false;
		room->sendCompulsoryTriggerLog(player, objectName(), true, true);
		player->skip(phase);
		return false;
	}
};

class YinshiyPro : public ProhibitSkill
{
public:
	YinshiyPro() : ProhibitSkill("#yinshiy-pro")
	{
	}

	bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		return card->isKindOf("DelayedTrick")&&to->hasSkill("yinshiy");
	}
};

BaiyiCard::BaiyiCard()
{
}

bool BaiyiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return to_select != Self && targets.length() < 2;
}

bool BaiyiCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == 2;
}

void BaiyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	source->setTag("BaiyiUsed", true);
	room->doSuperLightbox(source, "baiyi");
	room->removePlayerMark(source, "@baiyiMark");
	if (targets.first()->isDead() || targets.last()->isDead()) return;
	room->swapSeat(targets.first(), targets.last());
}

class Baiyi : public ZeroCardViewAsSkill
{
public:
	Baiyi() : ZeroCardViewAsSkill("baiyi")
	{
		limit_mark = "@baiyiMark";
		frequency = Limited;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@baiyiMark") > 0 && player->isWounded();
	}

	const Card *viewAs() const
	{
		return new BaiyiCard;
	}
};

JinglveCard::JinglveCard()
{
}

bool JinglveCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && !to_select->isKongcheng() && to_select != Self;
}

void JinglveCard::onEffect(CardEffectStruct &effect) const
{
	QStringList names = effect.from->getTag("Jinglve_targets").toStringList();
	if (!names.contains(effect.to->objectName())) {
		names << effect.to->objectName();
		effect.from->setTag("Jinglve_targets", names);
	}
	if (effect.to->isDead() || effect.to->isKongcheng() || effect.from->isDead()) return;
	Room *room = effect.from->getRoom();
	int id = room->doGongxin(effect.from, effect.to, effect.to->handCards(), "jinglve");
	if (id < 0) return;

	LogMessage log;
	log.type = "$JinglveMark";
	log.from = effect.from;
	log.to << effect.to;
	log.card_str = QString::number(id);
	room->sendLog(log, effect.from);

	const Card *card = Sanguosha->getEngineCard(id);
	QString mark = "&jinglve+:+" + card->objectName() + "+" + card->getSuitString() + "_char" + "+" + card->getNumberString();
	room->addPlayerMark(effect.to, mark, 1, QList<ServerPlayer *>() << effect.from);

	QVariantList sishi = effect.to->getTag("Sishi" + effect.from->objectName()).toList();
	if (!sishi.contains(id)) {
		sishi << id;
		effect.to->setTag("Sishi" + effect.from->objectName(), sishi);
	}
}

class JinglveVS : public ZeroCardViewAsSkill
{
public:
	JinglveVS() : ZeroCardViewAsSkill("jinglve")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("JinglveCard");
	}

	const Card *viewAs() const
	{
		return new JinglveCard;
	}
};

class Jinglve : public TriggerSkill
{
public:
	Jinglve() : TriggerSkill("jinglve")
	{
		events << CardUsed << CardsMoveOneTime << EventPhaseChanging;
		view_as_skill = new JinglveVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			QList<int> subcards;
			if (use.card->isVirtualCard())
				subcards = use.card->getSubcards();
			else
				subcards << use.card->getId();
			if (subcards.isEmpty()) return false;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				QVariantList sishi = player->getTag("Sishi" + p->objectName()).toList();
				if (sishi.isEmpty()) continue;
				foreach (int id, subcards) {
					if (sishi.contains(QVariant(id))) {
						sishi.removeOne(id);
						player->setTag("Sishi" + p->objectName(), sishi);
						SishiRemoveMark(id, player);
						LogMessage log;
						log.type = "#JinglveUse";
						log.from = p;
						log.to << player;
						log.arg = objectName();
						log.arg2 = use.card->objectName();
						log.card_str = QString::number(id);
						room->sendLog(log);
						room->broadcastSkillInvoke(objectName());
						room->notifySkillInvoked(p, objectName());
						use.nullified_list <<"_ALL_TARGETS";
						data = QVariant::fromValue(use);
					}
				}
			}
		} else if (event == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->isAlive() && p->hasSkill(objectName())) {
					QVariantList sishi = player->getTag("Sishi" + p->objectName()).toList();
					if (sishi.isEmpty()) continue;
					QList<int> sishi_ids = ListV2I(sishi);
					DummyCard *dummy = new DummyCard;
					foreach (const Card *c, player->getCards("hej")) {
						if (sishi_ids.contains(c->getEffectiveId()))
							dummy->addSubcard(c);
					}
					dummy->deleteLater();
					if (dummy->subcardsLength() <= 0) continue;
					player->removeTag("Sishi" + p->objectName());
					room->sendCompulsoryTriggerLog(p, objectName());
					p->obtainCard(dummy, false);
				}
			}
		} else if (event == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place == Player::DiscardPile){
				foreach (ServerPlayer *p, room->getPlayers()) {
					QVariantList sishi = p->getTag("Sishi" + player->objectName()).toList();
					if (sishi.isEmpty()) continue;
					DummyCard *dummy = new DummyCard;
					foreach (int id, move.card_ids) {
						if(sishi.contains(id)){
							sishi.removeOne(id);
							p->setTag("Sishi" + player->objectName(), sishi);
							SishiRemoveMark(id, p);
							if (room->getCardPlace(id) == Player::DiscardPile)
								dummy->addSubcard(id);
						}
					}
					dummy->deleteLater();
                    if((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)!=CardMoveReason::S_REASON_USE){
						if(dummy->subcardsLength()>0&&player->isAlive()&&player->hasSkill(objectName())){
							room->sendCompulsoryTriggerLog(player, objectName());
							room->obtainCard(player, dummy, true);
						}
					}
				}
			}
		}
		return false;
	}

	void SishiRemoveMark(int id, ServerPlayer *owner) const
	{
		const Card *card = Sanguosha->getEngineCard(id);
		QString mark = "&jinglve+:+" + card->objectName() + "+" + card->getSuitString() + "_char" + "+" + card->getNumberString();
		owner->getRoom()->removePlayerMark(owner, mark);
	}
};

class Shanli : public PhaseChangeSkill
{
public:
	Shanli() : PhaseChangeSkill("shanli")
	{
		frequency = Wake;
	}

	bool triggerable(const ServerPlayer *player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::RoundStart
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getTag("BaiyiUsed").toBool() && player->getTag("Jinglve_targets").toStringList().length()>=2){}
		else if(!player->canWake(objectName()))
			return false;
		room->sendCompulsoryTriggerLog(player, this);
		room->doSuperLightbox(player, objectName());
		room->addPlayerMark(player, objectName());

		if (room->changeMaxHpForAwakenSkill(player, -1, objectName())) {
			if (player->isDead()) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@shanli-invoke");
			room->doAnimate(1, player->objectName(), target->objectName());
			QStringList all_lord_skills;
			foreach (QString lord, Sanguosha->getLords()) {
				foreach (const Skill *skill, Sanguosha->getGeneral(lord)->getVisibleSkillList()) {
					if (skill->isLordSkill() && skill->isVisible() && !all_lord_skills.contains(skill->objectName()))
						all_lord_skills << skill->objectName();
				}
			}

			QStringList lord_skills;
			for (int i = 0; i < 3; i++) {
				if (all_lord_skills.isEmpty()) break;
				QString lordskill = all_lord_skills.at(qrand() % all_lord_skills.length());
				all_lord_skills.removeOne(lordskill);
				lord_skills << lordskill;
			}
			if (lord_skills.isEmpty()) return false;

			QString skill = room->askForChoice(player, objectName(), lord_skills.join("+"), QVariant::fromValue(target));
			if (target->hasLordSkill(skill, true)) return false;
			room->acquireSkill(target, skill);
		}
		return false;
	}
};

class MobileKuangcai : public TriggerSkill
{
public:
	MobileKuangcai() : TriggerSkill("mobilekuangcai")
	{
		events << CardUsed << EventPhaseChanging << EventPhaseStart;
		waked_skills = "#mobilekuangcai_mod";
		frequency = Frequent;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")||player->getMark("MobileKuangcaiUse-PlayClear")<1) return false;
			room->sendCompulsoryTriggerLog(player, this);
			player->drawCards(1,objectName());
			Config.OperationTimeout -= 1;
			if(Config.OperationTimeout<1) room->setPlayerFlag(player, "Global_PlayPhaseTerminated");
			else room->doNotify(player,QSanProtocol::S_COMMAND_OPERATION_TIMEOUT, Config.OperationTimeout);
		} else if (event == EventPhaseStart) {
			if (player->getPhase()==Player::Play&&player->isAlive()
				&&player->hasSkill(objectName())&&player->askForSkillInvoke(this)){
				room->broadcastSkillInvoke(objectName());//NullificationCountDown
				room->addPlayerMark(player, "MobileKuangcaiUse-PlayClear",999);
				player->setTag("MobileKuangcaiUse", true);
				player->setTag("MobileKuangcaiTimeout", Config.OperationTimeout);
				Config.OperationTimeout = 5;
				room->doNotify(player,QSanProtocol::S_COMMAND_OPERATION_TIMEOUT, 5);
			}
		} else if (event == EventPhaseChanging) {
			//PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (player->getTag("MobileKuangcaiUse").toBool()){
				player->setTag("MobileKuangcaiUse", false);
				Config.OperationTimeout = player->getTag("MobileKuangcaiTimeout").toInt();
				room->doNotify(player,QSanProtocol::S_COMMAND_OPERATION_TIMEOUT, Config.OperationTimeout);
			}
		}
		return false;
	}
};

class MobileKuangcaiMod : public TargetModSkill
{
public:
	MobileKuangcaiMod() : TargetModSkill("#mobilekuangcai_mod")
	{
		pattern = ".";
	}

	int getResidueNum(const Player *from, const Card *, const Player *) const
	{
		return from->getMark("MobileKuangcaiUse-PlayClear");
	}

	int getDistanceLimit(const Player *from, const Card *, const Player *) const
	{
		return from->getMark("MobileKuangcaiUse-PlayClear");
	}
};

class MobileShejian : public TriggerSkill
{
public:
	MobileShejian() : TriggerSkill("mobileshejian")
	{
		events << CardsMoveOneTime << EventPhaseEnd;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->getPhase() == Player::Discard;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from==player && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
				QVariantList ids = player->getTag("MobileShejianIds").toList();
				foreach (int card_id, move.card_ids) {
					ids << card_id;
				}
				player->setTag("MobileShejianIds", ids);
			}
		} else if (triggerEvent == EventPhaseEnd) {
			QVariantList Vids = player->getTag("MobileShejianIds").toList();
			player->removeTag("MobileShejianIds");
			if (!player->hasSkill(objectName()))
				return false;
			QList<int> ids;

			foreach (int id, ListV2I(Vids)) {
				if (!ids.contains(id)&&room->getCardPlace(id) == Player::DiscardPile)
					ids << id;
			}

			if (ids.length()<2)
				return false;
			QStringList sus;
			foreach (int id, ids) {
				const Card*c = Sanguosha->getCard(id);
				if (sus.contains(c->getSuitString()))
					return false;
				sus << c->getSuitString();
			}
			QList<ServerPlayer *>tos;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(player->canDiscard(p,"he"))
					tos << p;
			}
			ServerPlayer *to = room->askForPlayerChosen(player,tos,objectName(),"MobileShejian0:",true,true);
			if(to){
				room->broadcastSkillInvoke(objectName());
				int id = room->askForCardChosen(player,to,"he",objectName(),false,Card::MethodDiscard);
				if(id>-1){
					room->throwCard(id,objectName(),to,player);
				}
			}
		}
		return false;
	}
};

static QHash<QString, QString> ChangshiSkills;

class Danggu : public TriggerSkill
{
public:
	Danggu() : TriggerSkill("danggu")
	{
		events << GameStart << Revived;
		frequency = Compulsory;
	}

	void jieDang(ServerPlayer *player, Room *room, QStringList cs) const
	{
		room->setPlayerMark(player,"&chang_shi",cs.length());
		QStringList cs2s,cssk,taunts;
		foreach (QString s, player->getTag("DangguSkills").toStringList()) {
			if(s.startsWith("-")) continue;
			cssk << "-"+s;
		}
		QString cs1 = cs[qrand()%cs.length()];
		if(cs1=="cs_gaowang")
			taunts << "cs_hanli" << "cs_duangui" << "cs_guosheng" << "cs_bilan";
		else if(cs1=="cs_duangui")
			taunts << "cs_guosheng";
		else if(cs1=="cs_guosheng")
			taunts << "cs_duangui";
		else if(cs1=="cs_bilan")
			taunts << "cs_hanli";
		else if(cs1=="cs_hanli")
			taunts << "cs_bilan";
		player->setAvatarIcon(cs1);
		cssk << ChangshiSkills[cs1];
		cs.removeOne(cs1);
		qShuffle(cs);
		foreach (QString g, cs) {
			cs2s << g;
			if(!taunts.contains(g)) cs1 = "OK";
			if(cs2s.length()>=4) break;
		}
		if(cs1!="OK") taunts.clear();
		while(cs2s.length()>0){
			QString cs2 = room->askForGeneral(player,cs2s);
			cs2s.removeOne(cs2);
			if(taunts.contains(cs2)){
				room->playAudioEffect("audio/card/common/"+cs2+"_taunt.ogg");
			}else{
				cssk << ChangshiSkills[cs2];
				cs.removeOne(cs2);
				player->setAvatarIcon(cs2,true);
				break;
			}
		}
		player->setTag("DangguSkills", cssk);
		room->handleAcquireDetachSkills(player,cssk,true);
		player->setTag("ChangshiCards", cs);
		room->setPlayerMark(player,"&chang_shi",cs.length());
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (triggerEvent == GameStart) {
			room->sendCompulsoryTriggerLog(player,this);
			ChangshiSkills.insert("cs_zhangrang","cstaoluan");
			ChangshiSkills.insert("cs_zhaozhong","cschiyan");
			ChangshiSkills.insert("cs_sunzhang","cszimou");
			ChangshiSkills.insert("cs_bilan","cspicai");
			ChangshiSkills.insert("cs_xiayun","csyaozhuo");
			ChangshiSkills.insert("cs_hanli","csxiaolu");
			ChangshiSkills.insert("cs_lisong","cskuiji");
			ChangshiSkills.insert("cs_duangui","cschihe");
			ChangshiSkills.insert("cs_guosheng","csniqu");
			ChangshiSkills.insert("cs_gaowang","csmiaoyu");
			QStringList cs = ChangshiSkills.keys();
			foreach (QString g, cs) {
				room->doAnimate(QSanProtocol::S_ANIMATE_HUASHEN, player->objectName(), g);
				room->addPlayerMark(player,"&chang_shi");
				room->getThread()->delay(233);
			}
			player->setTag("ChangshiCards", cs);
			jieDang(player,room,cs);
		} else if (triggerEvent == Revived) {
			QStringList cs = player->getTag("ChangshiCards").toStringList();
			if(cs.isEmpty()) return false;
			room->sendCompulsoryTriggerLog(player,this);
			jieDang(player,room,cs);
			player->drawCards(1,objectName());
		}
		return false;
	}
};

class Mowang : public TriggerSkill
{
public:
	Mowang() : TriggerSkill("mowang")
	{
		events << BeforeGameOverJudge << EventPhaseChanging << EventPhaseStart;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}

	int getPriority(TriggerEvent event) const
	{
		if (event != BeforeGameOverJudge) return -2;
		return TriggerSkill::getPriority(event);
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == BeforeGameOverJudge) {
			DeathStruct death = data.value<DeathStruct>();
			if(death.who==player&&player->getMaxHp()>0&&player->hasSkill(objectName())
				&&!player->getTag("ChangshiCards").toStringList().isEmpty()){
				room->sendCompulsoryTriggerLog(player,this);
				room->restPlayer(player, objectName(), true);
				QString csai = player->property("avatarIcon").toString();
				if(!csai.isEmpty()) player->setAvatarIcon(csai+"2");
				csai = player->property("avatarIcon2").toString();
				if(!csai.isEmpty()) player->setAvatarIcon(csai+"2",true);
				player->setTag("RestTurn", room->getTag("TurnLengthCount"));
				player->setTag("MowangRestActive", true);
				return true;
			}
		} else if (triggerEvent == EventPhaseStart) {
			if (player->getPhase() != Player::RoundStart) return false;
			foreach (ServerPlayer *p, room->getAllPlayers(true)) {
				if (!p->property("RestPlayer").toBool()) continue;
				if (!p->getTag("MowangRestActive").toBool()) continue;
				if (p->getTag("RestTurn").toInt() < room->getTag("TurnLengthCount").toInt()) {
					room->sendCompulsoryTriggerLog(p,this);
					room->unrestPlayer(p, true);
					p->removeTag("MowangRestActive");
					p->removeTag("RestTurn");
					break;
				}
			}
		} else if (triggerEvent == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive || player->isDead() || !player->hasSkill(objectName())) return false;
			room->sendCompulsoryTriggerLog(player,this);
			room->killPlayer(player);
		}
		return false;
	}
};

class CsTaoluanVs : public OneCardViewAsSkill
{
public:
	CsTaoluanVs() : OneCardViewAsSkill("cstaoluan")
	{
		filter_pattern = ".";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("cstaoluanUse-PlayClear")<1&&player->getCardCount()>0;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if(player->getPhase()==Player::Play&&player->getCardCount()>0
			&&player->getMark("cstaoluanUse-PlayClear")<1){
			foreach (QString pn, pattern.split("+")) {
				Card *c = Sanguosha->cloneCard(pn);
				if(c){
					c->deleteLater();
					if(c->isKindOf("BasicCard")||c->isNDTrick()){
						return true;
					}
				}
			}
		}
		return false;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			const Card *dc = Self->getTag(objectName()).value<const Card *>();
			if(dc) pattern = dc->objectName();
			else return nullptr;
		}
		foreach (QString pn, pattern.split("+")) {
			Card *c = Sanguosha->cloneCard(pn);
			c->setSkillName(objectName());
			c->addSubcard(originalCard);
			if(Self->isLocked(c)) c->deleteLater();
			else return c;
		}
		return nullptr;
	}
};

class CsTaoluan : public TriggerSkill
{
public:
	CsTaoluan() : TriggerSkill("cstaoluan")
	{
		events << PreCardUsed;
		view_as_skill = new CsTaoluanVs;
	}

	QDialog *getDialog() const
	{
		return GuhuoDialog::getInstance(objectName(), true, true);
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->getPhase() == Player::Play;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->getSkillNames().contains(objectName())) return false;
		room->addPlayerMark(player,"cstaoluanUse-PlayClear");
		return false;
	}
};

class CsChiyan : public TriggerSkill
{
public:
	CsChiyan() : TriggerSkill("cschiyan")
	{
		events << TargetSpecified << EventPhaseChanging << DamageCaused;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Slash") && TriggerSkill::triggerable(player)) {
				foreach (ServerPlayer *t, use.to) {
					if (player->isDead()) return false;
					if (t->isDead()) continue;
					if (t->getCardCount() > 0 && player->askForSkillInvoke(this, t)) {
						room->broadcastSkillInvoke(objectName());
						int id = room->askForCardChosen(player, t, "he", objectName(), false, Card::MethodNone);
						t->addToPile(objectName(), id, false);
					}
				}
			}
		} else if (triggerEvent == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				QList<int> to_obtain = p->getPile(objectName());
				if (!to_obtain.isEmpty()) {
					DummyCard dummy(to_obtain);
					room->obtainCard(p, &dummy, false);
				}
			}
		} else if (triggerEvent == DamageCaused) {
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.to->isAlive()&&TriggerSkill::triggerable(damage.from)
			&&damage.card&&damage.card->isKindOf("Slash")&&damage.by_user
			&&damage.from->getHandcardNum()>=damage.to->getHandcardNum()
			&&damage.from->getEquips().length()>=damage.to->getEquips().length()){
				room->sendCompulsoryTriggerLog(damage.from, objectName());
				damage.damage++;
				data = QVariant::fromValue(damage);
			}
		}
		return false;
	}
};

class CsZimou : public TriggerSkill
{
public:
	CsZimou() : TriggerSkill("cszimou")
	{
		events << CardUsed;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		const Card *card = data.value<CardUseStruct>().card;
		if (!card || card->isKindOf("SkillCard")) return false;
		int mark = player->getMark("&cszimou-PlayClear") + 1;
		room->setPlayerMark(player, "&cszimou-PlayClear", mark);
		if (mark==2||mark==4||mark==6) {
			room->sendCompulsoryTriggerLog(player, this);
			QList<int> card_ids;
			foreach (int id, room->getDrawPile()) {
				const Card *card = Sanguosha->getCard(id);
				if (mark == 2 && card->isKindOf("Analeptic"))
					card_ids << id;
				else if (mark == 4 && card->isKindOf("Slash"))
					card_ids << id;
				else if (mark == 6 && card->isKindOf("Duel"))
					card_ids << id;
			}
			if (card_ids.isEmpty()) return false;
			int id = card_ids.at(qrand() % card_ids.length());
			room->obtainCard(player, id, true);
		}
		return false;
	}
};

CsPicaiCard::CsPicaiCard()
{
	target_fixed = true;
}

void CsPicaiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QStringList pcj;
	DummyCard *dummy = new DummyCard;
	while(source->isAlive()){
		JudgeStruct judge;
		judge.who = source;
		judge.reason = getSkillName();
		judge.throw_card = false;
		if(pcj.isEmpty())
			judge.pattern = ".|.";
		else{
			judge.good = false;
			judge.pattern = ".|"+pcj.join(",");
		}
		room->judge(judge);
		pcj << judge.card->getSuitString();
		dummy->addSubcard(judge.card->getEffectiveId());
		if(judge.isGood()&&source->isAlive()&&source->askForSkillInvoke(getSkillName())){
		}else break;
	}
	dummy->deleteLater();
	if(dummy->subcardsLength()>0){
		if(source->isAlive()){
			ServerPlayer *to = room->askForPlayerChosen(source,room->getAlivePlayers(),getSkillName(),"cspicai0:",true);
			if(to){
				to->obtainCard(dummy);
				return;
			}
		}
		room->throwCard(dummy,nullptr);
	}
}

class CsPicai : public ZeroCardViewAsSkill
{
public:
	CsPicai() : ZeroCardViewAsSkill("cspicai")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("CsPicaiCard");
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.startsWith("@@cspicai");
	}

	const Card *viewAs() const
	{
		return new CsPicaiCard;
	}
};

CsYaozhuoCard::CsYaozhuoCard()
{
}

bool CsYaozhuoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && Self->canPindian(to_select);
}

void CsYaozhuoCard::onEffect(CardEffectStruct &effect) const
{
	if(effect.from->canPindian(effect.to)){
		Room*room = effect.to->getRoom();
		if(effect.from->pindian(effect.to,getSkillName())){
			room->setPlayerMark(effect.to,"&csyaozhuo",1);
		}else{
			room->askForDiscard(effect.from,getSkillName(),2,2,false,true);
		}
	}
}

class CsYaozhuoVS : public ZeroCardViewAsSkill
{
public:
	CsYaozhuoVS() : ZeroCardViewAsSkill("csyaozhuo")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		foreach (const Player *p, player->getAliveSiblings()) {
			if(player->canPindian(p))
				return !player->hasUsed("CsYaozhuoCard");
		}
		return false;
	}

	const Card *viewAs() const
	{
		return new CsYaozhuoCard;
	}
};

class CsYaozhuo : public TriggerSkill
{
public:
	CsYaozhuo() : TriggerSkill("csyaozhuo")
	{
		events << EventPhaseChanging;
		view_as_skill = new CsYaozhuoVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->getMark("&csyaozhuo")>0;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if (change.to == Player::Draw){
			player->skip(Player::Draw);
			room->setPlayerMark(player,"&csyaozhuo",0);
		}
		return false;
	}
};

CsXiaoluCard::CsXiaoluCard()
{
	target_fixed = true;
}

void CsXiaoluCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	source->drawCards(2,getSkillName());
	if(source->isDead()||room->askForUseCard(source,"@@csxiaolu!","csxiaolu0:",-1,Card::MethodNone))
		return;
	DummyCard *dummy = new DummyCard;
	foreach (int id, source->handCards()) {
		if(dummy->subcardsLength()<2&&source->canDiscard(source,id))
			dummy->addSubcard(id);
	}
	if(dummy->subcardsLength()>0){
		room->throwCard(dummy,getSkillName(),source);
	}
	dummy->deleteLater();
}

CsXiaolu2Card::CsXiaolu2Card()
{
}

bool CsXiaolu2Card::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self;
}

bool CsXiaolu2Card::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	return !targets.isEmpty()||!Self->isJilei(this);
}

void CsXiaolu2Card::onUse(Room *room, CardUseStruct &use) const
{
	if(use.to.isEmpty())
		room->throwCard(this,"csxiaolu",use.from);
	else
		room->giveCard(use.from,use.to.first(),this,"csxiaolu");
}

class CsXiaolu : public ViewAsSkill
{
public:
	CsXiaolu() : ViewAsSkill("csxiaolu")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("CsXiaoluCard");
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.startsWith("@@csxiaolu");
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.startsWith("@@csxiaolu")){
			return selected.length()<2&&!to_select->isEquipped();
		}
		return false;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if(pattern.startsWith("@@csxiaolu")){
			if(cards.length()<2) return nullptr;
			CsXiaolu2Card*dc = new CsXiaolu2Card;
			dc->addSubcards(cards);
			return dc;
		}
		return new CsXiaoluCard;
	}

};

CsKuijiCard::CsKuijiCard()
{
}

bool CsKuijiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void CsKuijiCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	QList<int> hand = effect.to->handCards();
	if (!hand.isEmpty()) {
		LogMessage log;
		log.type = "$ViewAllCards";
		log.from = effect.from;
		log.to << effect.to;
		log.card_str = ListI2S(hand).join("+");
		room->sendLog(log, effect.from);
		room->notifyMoveToPile(effect.from, hand, "cskuiji", Player::PlaceHand, true);
	}
	const Card *c = room->askForUseCard(effect.from, "@@cskuiji", "cskuiji0:" + effect.to->objectName());
	if (!hand.isEmpty())
		room->notifyMoveToPile(effect.from, hand, "cskuiji", Player::PlaceHand, false);
	if (!c) return;
	QList<int> from_ids, to_ids;
	foreach (int id, c->getSubcards()) {
		if (hand.contains(id))
			to_ids << id;
		else
			from_ids << id;
	}
	QList<CardsMoveStruct> moves;
	if (!from_ids.isEmpty()) {
		CardMoveReason reason1(CardMoveReason::S_REASON_THROW, effect.from->objectName(), "cskuiji", "");
		CardsMoveStruct move1(from_ids, effect.from, nullptr, Player::PlaceHand, Player::DiscardPile, reason1);
		moves << move1;
		LogMessage log;
		log.type = "$DiscardCard";
		log.from = effect.from;
		log.card_str = ListI2S(from_ids).join("+");
		room->sendLog(log);
	}
	if (!to_ids.isEmpty()) {
		CardMoveReason reason2(CardMoveReason::S_REASON_DISMANTLE, effect.from->objectName(), effect.to->objectName(), "cskuiji", "");
		CardsMoveStruct move2(to_ids, effect.to, nullptr, Player::PlaceHand, Player::DiscardPile, reason2);
		moves << move2;
		LogMessage log;
		log.type = "$DiscardCardByOther";
		log.from = effect.from;
		log.to << effect.to;
		log.card_str = ListI2S(to_ids).join("+");
		room->sendLog(log);
	}
	room->moveCardsAtomic(moves, true);
}

CsKuijiDisCard::CsKuijiDisCard()
{
	target_fixed = true;
	m_skillName = "cskuiji";
}

void CsKuijiDisCard::onUse(Room *, CardUseStruct &) const
{
}

class CsKuiji : public ViewAsSkill
{
public:
	CsKuiji() : ViewAsSkill("cskuiji")
	{
		expand_pile = "#cskuiji";
		response_pattern = "@@cskuiji";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUsePattern() == "@@cskuiji") {
			if (to_select->isEquipped() || selected.length() > 3 || Self->isJilei(to_select)) return false;
			foreach (const Card *c, selected) {
				if (c->getSuit() == to_select->getSuit())
					return false;
			}
			return true;
		};
		return false;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUsePattern() == "@@cskuiji") {
			if (cards.length() != 4)
				return nullptr;
			CsKuijiDisCard *c = new CsKuijiDisCard;
			c->addSubcards(cards);
			return c;
		}
		return new CsKuijiCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("CsKuijiCard");
	}
};

class CsChihe : public TriggerSkill
{
public:
	CsChihe() : TriggerSkill("cschihe")
	{
		events << TargetSpecified;
		waked_skills = "#cschihe,#cschihe_limit";
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash") || use.to.length() != 1) return false;
			ServerPlayer *to = use.to.first();
			if (!player->askForSkillInvoke(this, to)) return false;
			player->peiyin(this);
			room->setCardFlag(use.card, "cschiheUsed");
			QStringList records;
			int damage = 0;
			foreach (int id, room->showDrawPile(player, 2, objectName(), false)) {
				QString su = Sanguosha->getCard(id)->getSuitString();
				records << su;
				if (su==use.card->getSuitString())
					damage++;
			}
			room->getThread()->delay(999);
			if (damage > 0)
				room->setCardFlag(use.card, "cschiheAddDamage_" + QString::number(damage));
			room->setPlayerProperty(to, "CsChiheTargetRecords", records.join(","));
		}
		return false;
	}
};

class CsChiheEffect : public TriggerSkill
{
public:
	CsChiheEffect() : TriggerSkill("#cschihe")
	{
		events << ConfirmDamage << CardOffset << CardOnEffect;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	int getPriority(TriggerEvent event) const
	{
		if (event == CardOffset || event == CardOnEffect)
			return 5;
		return TriggerSkill::getPriority(event);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardOnEffect || event == CardOffset) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if (!effect.card->hasFlag("cschiheUsed")) return false;
			room->setPlayerProperty(effect.to, "CsChiheTargetRecords", "");
		}else if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->hasFlag("cschiheUsed") || damage.to->isDead()) return false;
			int d = 0;
			foreach (QString flag, damage.card->getFlags()) {
				if (!flag.startsWith("cschiheAddDamage_")) continue;
				QStringList flags = flag.split("_");
				d = flags.last().toInt();
				if (d > 0) break;
			}
			if (d <= 0) return false;
			LogMessage log;
			log.type = "#YHHankaiDamage";
			log.from = player;
			log.to << damage.to;
			log.arg = "cschihe";
			log.arg2 = QString::number(damage.damage);
			log.arg3 = QString::number(damage.damage += d);
			room->sendLog(log);
			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

class CsChiheLimit : public CardLimitSkill
{
public:
	CsChiheLimit() : CardLimitSkill("#cschihe_limit")
	{
		frequency = NotFrequent;
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target) const
	{
		QString record = target->property("CsChiheTargetRecords").toString();
		if (!record.isEmpty()) return "Jink|" + record;
		return "";
	}
};

CsNiquCard::CsNiquCard()
{
}

bool CsNiquCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.isEmpty();
}

void CsNiquCard::onEffect(CardEffectStruct &effect) const
{
	if(effect.to->isAlive()){
		Room*room = effect.to->getRoom();
		room->damage(DamageStruct(getSkillName(),effect.from,effect.to,1,DamageStruct::Fire));
	}
}

class CsNiqu : public ZeroCardViewAsSkill
{
public:
	CsNiqu() : ZeroCardViewAsSkill("csniqu")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("CsNiquCard");
	}

	const Card *viewAs() const
	{
		return new CsNiquCard;
	}
};

class CsMiaoyuVS : public ViewAsSkill
{
public:
	CsMiaoyuVS() : ViewAsSkill("csmiaoyu")
	{
		response_or_use = true;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return (pattern.contains("slash") || pattern.contains("Slash"))
			|| pattern == "jink"
			|| (pattern.contains("peach") && player->getMark("Global_PreventPeach") == 0)
			|| pattern == "nullification";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->isWounded() || Slash::IsAvailable(player);
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *card) const
	{
		if (selected.length() >= 2 || card->hasFlag("using"))
			return false;

		if (!selected.isEmpty()) {
			return card->getSuit() == selected.first()->getSuit();
		}

		switch (Sanguosha->currentRoomState()->getCurrentCardUseReason()) {
		case CardUseStruct::CARD_USE_REASON_PLAY: {
			if (Self->isWounded() && card->getSuit() == Card::Heart)
				return true;
			else if (card->getSuit() == Card::Diamond) {
				FireSlash *slash = new FireSlash(Card::SuitToBeDecided, -1);
				slash->addSubcards(selected);
				slash->addSubcard(card->getEffectiveId());
				slash->deleteLater();
				return slash->isAvailable(Self);
			} else
				return false;
		}
		case CardUseStruct::CARD_USE_REASON_RESPONSE:
		case CardUseStruct::CARD_USE_REASON_RESPONSE_USE: {
			QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
			if (pattern == "jink")
				return card->getSuit() == Card::Club;
			else if (pattern == "nullification")
				return card->getSuit() == Card::Spade;
			else if (pattern.contains("peach"))
				return card->getSuit() == Card::Heart;
			else if (pattern.contains("slash") || pattern.contains("Slash"))
				return card->getSuit() == Card::Diamond;
		}
		default:
			break;
		}
		return false;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;

		Card *new_card = nullptr;

		switch (cards.first()->getSuit()) {
		case Card::Spade: {
			new_card = new Nullification(Card::SuitToBeDecided, 0);
			break;
		}
		case Card::Heart: {
			new_card = new Peach(Card::SuitToBeDecided, 0);
			break;
		}
		case Card::Club: {
			new_card = new Jink(Card::SuitToBeDecided, 0);
			break;
		}
		case Card::Diamond: {
			new_card = new FireSlash(Card::SuitToBeDecided, 0);
			break;
		}
		default:
			break;
		}
		if (new_card) {
			new_card->setSkillName(objectName());
			new_card->addSubcards(cards);
		}
		return new_card;
	}
};

class CsMiaoyu : public TriggerSkill
{
public:
	CsMiaoyu() : TriggerSkill("csmiaoyu")
	{
		events << PreHpRecover << ConfirmDamage << CardUsed << CardResponded;
		view_as_skill = new CsMiaoyuVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == PreHpRecover) {
			RecoverStruct recover = data.value<RecoverStruct>();
			if (!recover.card || !recover.card->getSkillNames().contains(objectName())) return false;
			if (recover.card->subcardsLength() != 2) return false;
			foreach (int id, recover.card->getSubcards()) {
				if (!Sanguosha->getCard(id)->isRed()) return false;
			}
			int old = recover.recover;
			++recover.recover;
			int now = qMin(recover.recover, player->getMaxHp() - player->getHp());
			if (now <= 0) return true;
			if (recover.who && now > old) {
				LogMessage log;
				log.type = "#NewlonghunRecover";
				log.from = recover.who;
				log.to << player;
				log.arg = objectName();
				log.arg2 = QString::number(now);
				room->sendLog(log);
			}

			recover.recover = now;
			data = QVariant::fromValue(recover);
		} else if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->getSkillNames().contains(objectName()) || damage.to->isDead()) return false;
			if (damage.card->subcardsLength() != 2) return false;
			foreach (int id, damage.card->getSubcards()) {
				if (!Sanguosha->getCard(id)->isRed()) return false;
			}

			LogMessage log;
			log.type = "#NewlonghunDamage";
			log.from = player;
			log.to << damage.to;
			log.arg = objectName();
			log.arg2 = QString::number(++damage.damage);
			room->sendLog(log);

			data = QVariant::fromValue(damage);
		} else {
			if (!room->hasCurrent() || !player->canDiscard(room->getCurrent(), "he")) return false;
			const Card *card = nullptr;
			if (event == CardUsed)
				card = data.value<CardUseStruct>().card;
			else
				card = data.value<CardResponseStruct>().m_card;

			if (!card || card->isKindOf("SkillCard") || !card->getSkillNames().contains(objectName())) return false;
			if (card->subcardsLength() != 2) return false;
			foreach (int id, card->getSubcards()) {
				if (!Sanguosha->getCard(id)->isBlack()) return false;
			}
			int id = room->askForCardChosen(player, room->getCurrent(), "he", objectName(), false, Card::MethodDiscard);
			room->throwCard(id, room->getCurrent(), player);
		}
		return false;
	}
};

class ZGGongli : public TriggerSkill
{
public:
	ZGGongli() : TriggerSkill("zggongli")
	{
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const{
		return false;
	}
	static bool GlTrigger(Player *player,const QString &mt)
	{
		if(!isNormalGameMode(player->getGameMode())&&player->hasSkill("zggongli")){
			foreach (const Player *p, player->getAliveSiblings()) {
				if(p->getGeneralName().contains(mt)&&player->isYourFriend(p))
					return true;
			}
		}
		return false;
	}
};

class Yance : public TriggerSkill
{
public:
	Yance() : TriggerSkill("yance")
	{
		events << RoundStart << EventPhaseStart << CardUsed << ChoiceMade;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}
	void yanceUse(ServerPlayer *player, Room *room) const
	{
		if(room->askForChoice(player,objectName(),"yance1+yance2")=="yance1"){
			QList<int>ids = room->getDrawPile();
			qShuffle(ids);
			foreach (int id, ids) {
				if (Sanguosha->getCard(id)->isKindOf("TrickCard")){
					room->obtainCard(player,id);
					break;
				}
			}
		}else{
			int n = player->getTag("yanceNum").toInt();
			if(!player->getTag("yanceUse").toBool()){
				n = 3;
				if(ZGGongli::GlTrigger(player,"you_pangtong")) n++;
				player->setTag("yanceNum", n);
				player->setTag("yanceUse", true);
			}
			if(n<1) return;
			QStringList choices;
			player->setMark("yanceTrue",0);
			player->setMark("yanceDraw",0);
			player->setMark("yanceUse",0);
			for (int i = 0; i < n; i++) {
				QString choice = room->askForChoice(player,objectName(),"red+black+BasicCard+TrickCard+EquipCard");
				choices << choice;
			}
			player->setTag("yanceChoice", choices);
			QVariant data = "yanceUsed";
			room->getThread()->trigger(EventForDiy,room,player,data);
		}
	}
	void yanceFinished(ServerPlayer *player, Room *room) const
	{
		QStringList choices = player->getTag("yanceChoice").toStringList();
		int n = player->getMark("yanceTrue");
		int x = player->getMark("fangqiuNum");
		if(n==0){
			room->loseHp(player,1+x,true,player,objectName());
			player->setTag("yanceNum", player->getTag("yanceNum").toInt()-1-x);
		}
		if(choices.length()/2>n)
			room->askForDiscard(player,objectName(),2+x,2+x,false,true);
		else{
			int m = x;
			foreach (int id, room->getDrawPile()) {
				const Card*c = Sanguosha->getCard(id);
				foreach (QString cn, choices) {
					if(c->getColorString()==cn||c->isKindOf(cn.toLocal8Bit().data())){
						room->obtainCard(player,c);
						if(m>0) m--;
						else c = nullptr;
						break;
					}
				}
				if(c==nullptr) break;
			}
			if(n==choices.length()){
				player->drawCards(2+x,objectName());
				player->setTag("yanceNum", qMin(7,player->getTag("yanceNum").toInt()+1+x));
				if(x>0&&n>3&&player->hasSkill("fangqiu",true))
					room->setPlayerMark(player,"@fangqiu",1);
			}
		}
		player->setMark("fangqiuNum",0);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == RoundStart) {
			if(data.toInt()!=1||player->getMark("yanceUse_lun")>0||!player->hasSkill(objectName())) return false;
			if(player->askForSkillInvoke(this,data)){
				player->addMark("yanceUse_lun");
				player->peiyin(this);
				yanceUse(player,room);
			}
		} else if (event == EventPhaseStart) {
			if(player->getPhase()!=Player::Start||player->getMark("yanceUse_lun")>0||!player->hasSkill(objectName())) return false;
			if(player->askForSkillInvoke(this,data)){
				player->addMark("yanceUse_lun");
				player->peiyin(this);
				yanceUse(player,room);
			}
		} else if (event == ChoiceMade) {
			QStringList choices = data.toString().split(":");
			if(choices[0]=="skillChoice"&&choices[1]==objectName()&&choices[2]=="yance2"){
				choices = player->getTag("yanceChoice").toStringList();
				if(player->getMark("yanceUse")>=choices.length()) return false;
				yanceFinished(player,room);
			}
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					QStringList choices = p->getTag("yanceChoice").toStringList();
					if(p->getMark("yanceUse")>=choices.length()) continue;
					LogMessage log;
					log.type = "#yanceChoice";
					log.from = p;
					log.arg = objectName();
					log.arg2 = choices[p->getMark("yanceUse")];
					room->sendLog(log);
					if(use.card->getColorString()==log.arg2||(p->getMark("yanceTrue")<1&&ZGGongli::GlTrigger(p,"you_xushu"))
					||use.card->isKindOf(log.arg2.toLocal8Bit().data())){
						if(p->getMark("yanceDraw")<5){
							p->addMark("yanceDraw");
							p->drawCards(1,objectName());
						}
						p->addMark("yanceTrue");
					}
					p->addMark("yanceUse");
					if(p->getMark("yanceUse")>=choices.length())
						yanceFinished(p,room);
				}
			}
		}
		return false;
	}
};

class Fangqiu : public TriggerSkill
{
public:
	Fangqiu() : TriggerSkill("fangqiu")
	{
		events << ChoiceMade << EventForDiy;
		limit_mark = "@fangqiu";
		frequency = Limited;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventForDiy) {
			if (data.toString()=="yanceUsed"&&player->getMark("@fangqiu")>0&&player->askForSkillInvoke(this)){
				player->peiyin(this);
				room->doSuperLightbox(player, "fangqiu");
				room->removePlayerMark(player,"@fangqiu");
				player->addMark("fangqiuNum");
				LogMessage log;
				log.type = "#yanceChoice";
				log.from = player;
				log.arg = "yance";
				foreach (QString cn, player->getTag("yanceChoice").toStringList()) {
					log.arg2 = cn;
					room->sendLog(log);
				}
			}
		}
		return false;
	}
};

class MobileManjuan : public TriggerSkill
{
public:
	MobileManjuan() : TriggerSkill("mobile_manjuan")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to_place==Player::PlaceHand&&move.card_ids.length()>1&&player->getMark("mobile_manjuan_lun")<5
			&&player==move.to&&move.reason.m_skillName!=objectName()&&move.reason.m_skillName!="InitialHandCards"){
				QStringList ids = ListI2S(move.card_ids);
				player->setTag("mobile_manjuanIds", ids);
				const Card*dc = room->askForExchange(player,objectName(),ids.length(),1,true,"mobile_manjuan0",true,ids.join(","));
				if(dc){
					player->addMark("mobile_manjuan_lun");
					player->skillInvoked(this);
					room->moveCardTo(dc,nullptr,Player::DrawPile,false);
					if(player->isDead()) return false;
					QList<int>dps = room->getDiscardPile();
					qShuffle(dps);
					Card*sc = new DummyCard;
					foreach (int cid, dc->getSubcards()) {
						const Card*c = Sanguosha->getCard(cid);
						foreach (int id, dps) {
							if (c->getType()!=Sanguosha->getCard(id)->getType()){
								sc->addSubcard(id);
								break;
							}
						}
						if(sc->subcardsLength()>4) break;
					}
					room->moveCardTo(sc,player,Player::PlaceHand,CardMoveReason(CardMoveReason::S_REASON_GOTBACK, player->objectName(), objectName(), ""),true);
					sc->deleteLater();
				}
			}
		}
		return false;
	}
};

class PTGongli : public TriggerSkill
{
public:
	PTGongli() : TriggerSkill("ptgongli")
	{
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const{
		return false;
	}
	static bool GlTrigger(Player *player,const QString &mt)
	{
		if(!isNormalGameMode(player->getGameMode())&&player->hasSkill("ptgongli")){
			foreach (const Player *p, player->getAliveSiblings()) {
				if(p->getGeneralName().contains(mt)&&player->isYourFriend(p))
					return true;
			}
		}
		return false;
	}
};

class YangmingVs : public OneCardViewAsSkill
{
public:
	YangmingVs() : OneCardViewAsSkill("yangming")
	{
		response_pattern = "@@yangming";
		expand_pile = "yangming";
	}
	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		return selected.isEmpty()&&Self->getPile("yangming").contains(to_select->getEffectiveId())
		&&to_select->isAvailable(Self);
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		return originalCard;
	}
};

class Yangming : public TriggerSkill
{
public:
	Yangming() : TriggerSkill("yangming")
	{
		events << CardsMoveOneTime << EventPhaseEnd;
		view_as_skill = new YangmingVs;
		global = true;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(player->getPhase()!=Player::Play) return false;
		if (event == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from_places.contains(Player::PlaceHand)&&player==move.from){
				foreach (Player::Place p, move.from_places) {
					if(p==Player::PlaceHand)
						player->addMark("yangmingHand-PlayClear");
				}
			}
			if(move.to_place==Player::DiscardPile){
				foreach (int id, move.card_ids) {
					const Card*c = Sanguosha->getCard(id);
					if(player->getMark(c->getSuitString()+"yangmingSuit-Clear")<1){
						player->addMark(c->getSuitString()+"yangmingSuit-Clear");
						foreach (QString m, player->getMarkNames()) {
							if(m.contains("&yangming+:+")&&player->getMark(m)>0){
								room->setPlayerMark(player,m,0);
								m.remove("-Clear");
								QStringList ms = m.split("+");
								ms << c->getSuitString()+"_char";
								room->setPlayerMark(player,ms.join("+")+"-Clear",1);
								c = nullptr;
								break;
							}
						}
						if(c&&player->hasSkill(objectName(),true))
							room->setPlayerMark(player,"&yangming+:+"+c->getSuitString()+"_char-Clear",1);
					}
				}
			}
		}else if(player->getMark("yangmingHand-PlayClear")>=3&&player->hasSkill(objectName())){
			int n = 0;
			foreach (QString m, player->getMarkNames())
				if(m.contains("yangmingSuit-Clear")&&player->getMark(m)>0)
					n++;
			if(PTGongli::GlTrigger(player,"you_zhugeliang")) n++;
			if(player->askForSkillInvoke(this,data)){
				player->peiyin(this);
				QList<int> ids = room->getNCards(n);
				QStringList qrs,ms;
				foreach (int id, ids) {
					const Card*c = Sanguosha->getCard(id);
					foreach (int ld, ids) {
						if(id!=ld&&c->getSuit()==Sanguosha->getCard(ld)->getSuit()){
							qrs << c->toString();
							room->setPlayerCardLimitation(player,"use",qrs.last(),false);
							break;
						}
					}
				}
				player->addToPile("yangming",ids);
				while(player->getPile("yangming").length()>0){
					const Card*c = room->askForUseCard(player,"@@yangming","yangming0");
					if(c) ms << c->getSuitString();
					else break;
				}
				foreach (QString l, qrs)
					room->removePlayerCardLimitation(player,"use",l);
				ids = player->getPile("yangming");
				Card*dc = new DummyCard(ids);
				room->throwCard(dc,objectName(),nullptr);
				dc->deleteLater();
				if(PTGongli::GlTrigger(player,"you_xushu")){
					foreach (int id, ids) {
						if(!room->getCardOwner(id)&&!ms.contains(Sanguosha->getCard(id)->getSuitString())){
							room->obtainCard(player,id);
							break;
						}
					}
				}
			}
		}
		return false;
	}
};

class Xiaxing : public TriggerSkill
{
public:
	Xiaxing() : TriggerSkill("xiaxing")
	{
		events << CardsMoveOneTime << GameStart;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile){
				foreach (int id, move.card_ids) {
					const Card*c = Sanguosha->getCard(id);
					if(c->objectName()=="_xuanjian"&&!room->getCardOwner(id)){
						foreach (QString m, player->getMarkNames()) {
							if(m.contains("&qihui+:+")&&player->getMark(m)>0){
								QStringList choices,ms = m.split("+");
								if(ms.length()>3&&player->askForSkillInvoke(this,data)){
									room->setPlayerMark(player,m,0);
									player->peiyin(this);
									if(ms.length()>4){
										foreach (QString t, ms){
											if(t.contains("_char"))
												choices << "qihui0="+t;
										}
										for (int i = 0; i < 2; i++) {
											QString choice = room->askForChoice(player,objectName(),choices.join("+"));
											choices.removeOne(choice);
											choice.remove("qihui0=");
											ms.removeOne(choice);
											player->loseMark(choice+"qihuiType");
										}
										room->setPlayerMark(player,ms.join("+"),1);
									}else{
										foreach (QString t, ms){
											if(t.contains("_char"))
												player->loseMark(t+"qihuiType");
										}
									}
									player->obtainCard(c);
								}
								break;
							}
						}
						break;
					}
				}
			}
		}else{
			foreach (int id, Sanguosha->getRandomCards(true)) {
				const Card*c = Sanguosha->getCard(id);
				if(c->objectName()=="_xuanjian"&&!room->getCardOwner(id)){
					room->sendCompulsoryTriggerLog(player,this);
					player->obtainCard(c);
					if(player->handCards().contains(id)&&c->isAvailable(player))
						room->useCard(CardUseStruct(c,player));
				}
			}
		}
		return false;
	}
};

class Qihui : public TriggerSkill
{
public:
	Qihui() : TriggerSkill("qihui")
	{
		events << CardUsed;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()<1) return false;
			if(player->getMark("qihui3Bf")>0){
				room->setPlayerMark(player,"qihui3Bf",0);
				use.m_addHistory = false;
				data.setValue(use);
			}
			if(player->getMark(use.card->getType()+"_charqihuiType")<1){
				room->sendCompulsoryTriggerLog(player,this);
				player->gainMark(use.card->getType()+"_charqihuiType");
				QStringList ms,choices;
				foreach (QString m, player->getMarkNames()) {
					if(m.contains("&qihui+:+")&&player->getMark(m)>0){
						ms = m.split("+");
						room->setPlayerMark(player,m,0);
						ms << use.card->getType()+"_char";
						room->setPlayerMark(player,ms.join("+"),1);
						break;
					}
				}
				if(ms.isEmpty()){
					room->setPlayerMark(player,"&qihui+:+"+use.card->getType()+"_char",1);
				}else if(ms.length()>4){
					foreach (QString t, ms){
						if(t.contains("_char"))
							choices << "qihui0="+t;
					}
					room->setPlayerMark(player,ms.join("+"),0);
					for (int i = 0; i < 2; i++) {
						QString choice = room->askForChoice(player,objectName(),choices.join("+"));
						choices.removeOne(choice);
						choice.remove("qihui0=");
						ms.removeOne(choice);
						player->loseMark(choice+"qihuiType");
					}
					room->setPlayerMark(player,ms.join("+"),1);
					QString choice = room->askForChoice(player,objectName(),"qihui1+qihui2+qihui3");
					if(choice=="qihui1")
						room->recover(player,RecoverStruct(objectName(),player));
					else if(choice=="qihui2")
						player->drawCards(2,objectName());
					else
						room->setPlayerMark(player,"qihui3Bf",998);
				}
			}
		}
		return false;
	}
};

QinyingCard::QinyingCard()
{
	handling_method = Card::MethodRecast;
	will_throw = false;
}

bool QinyingCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	Card*dc = Sanguosha->cloneCard("duel");
	dc->setSkillName("qinying");
	dc->deleteLater();
	return dc->targetFilter(targets,to,Self);
}

void QinyingCard::onUse(Room *room, CardUseStruct &use) const
{
	room->setTag("QinyingUse",QVariant::fromValue(use));
	use.to.clear();
	SkillCard::onUse(room,use);
}

void QinyingCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	LogMessage log;
	log.type = "$RecastCard";
	log.from = source;
	log.card_str = ListI2S(subcards).join("+");
	room->sendLog(log);
	room->moveCardTo(this, source, nullptr, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_RECAST, source->objectName(), getSkillName(), ""));
	source->drawCards(subcardsLength(), "recast");
	if (source->isDead()) return;
	source->setMark("QinyingNum",subcardsLength());
	room->setTag("QinyingFp",QVariant::fromValue(source));
	CardUseStruct use = room->getTag("QinyingUse").value<CardUseStruct>();
	Card*dc = Sanguosha->cloneCard("duel");
	dc->setSkillName("_qinying");
	dc->deleteLater();
	use.card = dc;
	room->useCard(use);
}

class QinyingVs : public ViewAsSkill
{
public:
	QinyingVs() : ViewAsSkill("qinying")
	{
	}

	bool viewFilter(const QList<const Card *> &, const Card *to_select) const
	{
		return !Self->isCardLimited(to_select,Card::MethodRecast);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty()) return nullptr;
		Card *sc = new QinyingCard;
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("QinyingCard");
	}
};

class Qinying : public TriggerSkill
{
public:
	Qinying() : TriggerSkill("qinying")
	{
		events << CardAsked;
		view_as_skill = new QinyingVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardAsked) {
			QStringList ask = data.toStringList();
			if(ask.contains("slash")&&ask.contains("response")&&ask.last().contains("qinying")){
				CardUseStruct use = room->getTag("QinyingUse").value<CardUseStruct>();
				if(use.from->getMark("QinyingNum")>0){
					QList<int>ids;
					QStringList ban = use.from->getTag("stgongliBan").toStringList();
					foreach (const Card*c, player->getCards("hej")) {
						if(ban.contains(c->getType()))
							ids << c->getId();
					}
					if(ids.length()<player->getCardCount(true,true)&&player->askForSkillInvoke("qinying0","qinying",false)){
						int id = room->askForCardChosen(player,player,"hej",objectName(),true,Card::MethodDiscard,ids);
						if(id>-1){
							use.from->removeMark("QinyingNum");
							room->throwCard(id,objectName(),player);
							Card*dc = Sanguosha->cloneCard("slash");
							dc->setSkillName("_qinying");
							dc->deleteLater();
							room->provide(dc);
							return true;
						}
					}
				}
			}
		}
		return false;
	}
};

class Lunxiong : public TriggerSkill
{
public:
	Lunxiong() : TriggerSkill("lunxiong")
	{
		events << Damage << Damaged;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		QList<const Card*>hs = player->getHandcards();
		if(hs.isEmpty()) return false;
		const Card*mc = hs.last();
		foreach (const Card*c, hs) {
			if(c->getNumber()>mc->getNumber())
				mc = c;
		}
		foreach (const Card*c, hs) {
			if(c->getId()!=mc->getId()&&c->getNumber()>=mc->getNumber())
				return false;
		}
		if(mc->getNumber()>player->getMark("&lunxiong")
			&&room->askForCard(player,mc->toString(),"lunxiong0",data,objectName())){
			room->setPlayerMark(player,"&lunxiong",mc->getNumber());
			player->drawCards(3,objectName());
		}
		return false;
	}
};

class STGongli : public TriggerSkill
{
public:
	STGongli() : TriggerSkill("stgongli")
	{
		events << GameStart;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (event == GameStart) {
			if(player->hasSkill("qinying",true)){
				room->sendCompulsoryTriggerLog(player,this);
				int n = 0;
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->getGeneralName().startsWith("mobileyou_")) n++;
				}
				QStringList ban,choices;
				choices << "basic" << "trick" << "equip";
				for (int i = 0; i < qMin(n,3); i++) {
					QString choice = room->askForChoice(player,objectName(),choices.join("+"));
					choices.removeOne(choice);
					ban << choice;
				}
				player->setTag("stgongliBan", ban);
			}
		}
		return false;
	}
};

class Shunyi : public TriggerSkill
{
public:
	Shunyi() : TriggerSkill("shunyi")
	{
		events << CardUsed << EventPhaseChanging;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()<1||use.card->isVirtualCard()) return false;
			if(use.m_isHandcard&&use.card->getNumber()>player->getMark("shunyiUse-Clear")&&player->hasSkill(objectName())){
				if(use.card->getSuit()==2||player->getTag("CJgongli").toStringList().contains(use.card->getSuitString())){
					Card*dc = new DummyCard;
					foreach (const Card*c, player->getHandcards()) {
						if(c->getSuit()==use.card->getSuit())
							dc->addSubcard(c);
					}
					dc->deleteLater();
					if(dc->subcardsLength()>0&&player->askForSkillInvoke(this,data)){
						player->peiyin(this);
						player->addToPile(objectName(),dc,false);
						player->drawCards(1,objectName());
					}
				}
			}
		}else{
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive){
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					if(p->getPile(objectName()).length()>0){
						Card*dc = new DummyCard(p->getPile(objectName()));
						p->obtainCard(dc,false);
						dc->deleteLater();
					}
				}
			}
		}
		return false;
	}
};

BiweiCard::BiweiCard()
{
}

bool BiweiCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to!=Self;
}

void BiweiCard::onEffect(CardEffectStruct &effect) const
{
	if(effect.to->isAlive()){
		Room*room = effect.to->getRoom();
		Card*dc = new DummyCard;
		foreach (const Card*c, effect.to->getHandcards()) {
			if(c->getNumber()>=getNumber()&&!effect.to->isJilei(c))
				dc->addSubcard(c);
		}
		if(dc->subcardsLength()>0){
			room->throwCard(dc,getSkillName(),effect.to);
		}else{
			room->addPlayerHistory(effect.from,"BiweiCard",-1);
		}
		dc->deleteLater();
	}
}

class Biwei : public ZeroCardViewAsSkill
{
public:
	Biwei() : ZeroCardViewAsSkill("biwei")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("BiweiCard")&&!player->isKongcheng();
	}

	const Card *viewAs() const
	{
		QList<const Card*>hs = Self->getHandcards();
		if(hs.isEmpty()) return nullptr;
		const Card*mc = hs.last();
		foreach (const Card*c, hs) {
			if(c->getNumber()>mc->getNumber())
				mc = c;
		}
		foreach (const Card*c, hs) {
			if(c->getId()!=mc->getId()&&c->getNumber()>=mc->getNumber())
				return nullptr;
		}
		if(Self->isJilei(mc)) return nullptr;
		Card*dc = new BiweiCard;
		dc->addSubcard(mc);
		return dc;
	}
};

class CJGongli : public TriggerSkill
{
public:
	CJGongli() : TriggerSkill("cjgongli")
	{
		events << GameStart;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (event == GameStart) {
			if(player->hasSkill("shunyi",true)){
				room->sendCompulsoryTriggerLog(player,this);
				int n = 0;
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->getGeneralName().startsWith("mobileyou_")) n++;
				}
				QStringList ban,choices;
				choices << "diamond" << "spade" << "club";
				for (int i = 0; i < qMin(n,3); i++) {
					QString choice = room->askForChoice(player,objectName(),choices.join("+"));
					choices.removeOne(choice);
					ban << choice;
				}
				player->setTag("CJgongli", ban);
			}
		}
		return false;
	}
};

class XSGongli : public TriggerSkill
{
public:
	XSGongli() : TriggerSkill("xsgongli")
	{
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &) const{
		return false;
	}
	static bool GlTrigger(const Player *player,const QString &mt)
	{
		if(!isNormalGameMode(player->getGameMode())&&player->hasSkill("xsgongli")){
			foreach (const Player *p, player->getAliveSiblings()) {
				if(p->getGeneralName().contains(mt)&&player->isYourFriend(p))
					return true;
			}
		}
		return false;
	}
};

class XuanjianVS : public OneCardViewAsSkill
{
public:
	XuanjianVS() : OneCardViewAsSkill("_xuanjian")
	{
		filter_pattern = ".|.|.|hand";
	}

	const Card *viewAs(const Card *c) const
	{
		Card *card = Sanguosha->cloneCard("slash");
		card->setSkillName(objectName());
		if(XSGongli::GlTrigger(Self,"you_zhugeliang")){
			card->addSubcard(c);
		}else{
			foreach (const Card *h, Self->getHandcards()) {
				if(h->getSuit()==c->getSuit())
					card->addSubcard(h);
			}
		}
		return card;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->isKongcheng()&&Slash::IsAvailable(player);
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return !player->isKongcheng()&&pattern.contains("slash")
		&&Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
	}
};

Xuanjian::Xuanjian(Suit suit, int number)
	: Weapon(suit, number, 3)
{
	setObjectName("_xuanjian");
}

class Caiqiu : public TriggerSkill
{
public:
	Caiqiu() : TriggerSkill("caiqiu")
	{
		events << CardFinished << RoundStart;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()<1) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(p->getMark(use.card->objectName()+"caiqiuCn_lun")>0&&p->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(p,this,qrand()%2+3);
					room->loseHp(p,1,true,p,objectName());
				}
			}
		}else{
			if (player->isAlive()&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this,qrand()%2+1);
				QList<int>ids = room->getNCards(room->getPlayers().length());
				room->fillAG(ids,player);
				Card*dc = dummyCard();
				room->returnToTopDrawPile(ids);
				while (ids.length()>0) {
					int id = room->askForAG(player,ids,true,objectName());
					if(id<0) break;
					ids.removeAll(id);
					dc->addSubcard(id);
					room->takeAG(player,id,false,QList<ServerPlayer*>()<<player);
					player->addMark(Sanguosha->getCard(id)->objectName()+"caiqiuCn_lun");
				}
				room->clearAG(player);
				player->obtainCard(dc);
			}
		}
		return false;
	}
};

class Xichang : public TriggerSkill
{
public:
	Xichang() : TriggerSkill("xichang")
	{
		events << GameStart << CardsMoveOneTime;
		frequency = Compulsory;
		waked_skills = "weizhuang";
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == GameStart) {
			QString choice = room->askForChoice(player,objectName(),"guidian+dongjiao+xiuge");
			int n = qrand()%2+1;
			if(choice=="dongjiao") n += 2;
			else if(choice=="xiuge") n += 4;
			room->sendCompulsoryTriggerLog(player,this,n);
			if(choice!="guidian"&&player->getGeneralName().endsWith("cuifu")){
				player->setAvatarIcon(choice+"_cuifu");
			}
			room->setPlayerProperty(player,"xichangBf",choice);
			room->setPlayerMark(player,"&xichang+:+"+choice,1);
			room->attachSkillToPlayer(player,"weizhuang");
		}else{
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&move.to==player&&move.reason.m_reason!=CardMoveReason::S_REASON_DRAW){
				QList<int>ids;
				foreach (int id, player->handCards()) {
					if(move.card_ids.contains(id)) ids << id;
				}
				if(ids.isEmpty()) return false;
				room->sendCompulsoryTriggerLog(player,this);
				foreach (int id, ids){
					room->setCardFlag(id,"visible");
					room->setCardTip(id,"mingzhi");
				}
				QList<CardsMoveStruct> moves;
				CardsMoveStruct move1(ids,player,nullptr,Player::PlaceHand,Player::PlaceTable,
				CardMoveReason(CardMoveReason::S_REASON_SHOW,player->objectName(),objectName(),""));
				moves.append(move1);
				CardsMoveStruct move2(ids,nullptr,player,Player::PlaceTable,Player::PlaceHand,
				CardMoveReason(CardMoveReason::S_REASON_SHOW,player->objectName(),objectName(),""));
				moves.append(move2);
				room->notifyMoveCards(true, moves, true);
				room->notifyMoveCards(false, moves, true);
				QVariant data2 = "mingzhi:"+ListI2S(ids).join("+");
				room->getThread()->trigger(EventForDiy,room,player,data2);
			}
		}
		return false;
	}
};

WeizhuangCard::WeizhuangCard()
{
}

bool WeizhuangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("weizhuang");
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool WeizhuangCard::targetFixed() const
{
	if (Sanguosha->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_RESPONSE){
		Card *card = Sanguosha->cloneCard(user_string.split("+").first());
		if(card){
			card->setSkillName("_weizhuang");
			card->deleteLater();
			return card->targetFixed();
		}
	}
	return true;
}

bool WeizhuangCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("weizhuang");
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

const Card *WeizhuangCard::validate(CardUseStruct &use) const
{
	Room *room = use.from->getRoom();
	int n = qrand()%6+13;
	use.from->skillInvoked("weizhuang",n);
	QList<int> suit;
	foreach (const Card*h, use.from->getHandcards()) {
		if(h->hasTip("mingzhi")){
			if(suit.contains(h->getSuit())) continue;
			suit.append(h->getSuit());
		}
	}
	foreach (const Card*c, use.from->getEquips()) {
		if(suit.contains(c->getSuit())) continue;
		suit.append(c->getSuit());
	}
	if(suit.length()==4)
		room->showCard(use.from,subcards);
	else
		room->throwCard(subcards,"weizhuang",use.from);

	QString cn = user_string;
	if (user_string.contains("+")){
		QStringList cns;
		foreach (QString tp, user_string.split("+")) {
			if(use.from->getMark("weizhuang_juguan_remove_"+tp+"-Clear")<1)
				cns << tp;
		}
		if (cns.isEmpty()) return nullptr;
		cn = room->askForChoice(use.from, "weizhuang", cns.join("+"));
	}
	use.m_addHistory = false;

	room->addPlayerMark(use.from, "weizhuang_juguan_remove_"+cn+"-Clear");
	Card *use_card = Sanguosha->cloneCard(cn);
	use_card->setSkillName("_weizhuang");
	use_card->setFlags("weizhuangSuit"+getSuitString());
	use_card->deleteLater();
	return use_card;
}

const Card *WeizhuangCard::validateInResponse(ServerPlayer *source) const
{
	Room *room = source->getRoom();
	int n = qrand()%6+13;
	source->skillInvoked("weizhuang",n);
	QList<int> suit;
	foreach (const Card*h, source->getHandcards()) {
		if(h->hasTip("mingzhi")){
			if(suit.contains(h->getSuit())) continue;
			suit.append(h->getSuit());
		}
	}
	foreach (const Card*c, source->getEquips()) {
		if(suit.contains(c->getSuit())) continue;
		suit.append(c->getSuit());
	}
	if(suit.length()==4)
		room->showCard(source,subcards);
	else
		room->throwCard(subcards,"weizhuang",source);

	QString cn = user_string;
	if (user_string.contains("+")){
		QStringList cns;
		foreach (QString tp, user_string.split("+")) {
			if(source->getMark("weizhuang_juguan_remove_"+tp+"-Clear")<1)
				cns << tp;
		}
		if (cns.isEmpty()) return nullptr;
		cn = room->askForChoice(source, "weizhuang", cns.join("+"));
	}
	room->addPlayerMark(source, "weizhuang_juguan_remove_"+cn+"-Clear");

	Card *use_card = Sanguosha->cloneCard(cn);
	use_card->setSkillName("_weizhuang");
	use_card->deleteLater();
	return use_card;
}

class Weizhuangvs : public OneCardViewAsSkill
{
public:
	Weizhuangvs() : OneCardViewAsSkill("weizhuang")
	{
	}

	bool viewFilter(const Card *to_select) const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			const Card *dc = Self->getTag(objectName()).value<const Card *>();
			if(dc) pattern = dc->objectName();
			else return false;
		}
		QList<int> suit;
		foreach (const Card*h, Self->getHandcards()) {
			if(h->hasTip("mingzhi")){
				if(suit.contains(h->getSuit())) continue;
				suit.append(h->getSuit());
			}
		}
		foreach (const Card*c, Self->getEquips()) {
			if(suit.contains(c->getSuit())) continue;
			suit.append(c->getSuit());
		}
		if(pattern.contains("slash")&&Self->getMark("weizhuang_juguan_remove_slash-Clear")<1&&to_select->isKindOf("Weapon"))
			return suit.length()==4||!Self->isJilei(to_select);
		if(pattern.contains("jink")&&Self->getMark("weizhuang_juguan_remove_jink-Clear")<1&&to_select->isKindOf("Armor"))
			return suit.length()==4||!Self->isJilei(to_select);
		if(pattern.contains("peach")&&Self->getMark("weizhuang_juguan_remove_peach-Clear")<1&&to_select->isKindOf("DefensiveHorse"))
			return suit.length()==4||!Self->isJilei(to_select);
		if(pattern.contains("analeptic")&&Self->getMark("weizhuang_juguan_remove_analeptic-Clear")<1&&to_select->isKindOf("OffensiveHorse"))
			return suit.length()==4||!Self->isJilei(to_select);
		return false;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if(player->property("xichangBf").toString()!="xiuge") return false;
		if(player->getMark("weizhuang_juguan_remove_slash-Clear")<1){
			Card*dc = Sanguosha->cloneCard("slash");
			dc->setSkillName("weizhuang");
			dc->deleteLater();
			if(dc->isAvailable(player))
				return true;
		}
		if(player->getMark("weizhuang_juguan_remove_peach-Clear")<1){
			Card*dc = Sanguosha->cloneCard("peach");
			dc->setSkillName("weizhuang");
			dc->deleteLater();
			if(dc->isAvailable(player))
				return true;
		}
		if(player->getMark("weizhuang_juguan_remove_analeptic-Clear")<1){
			Card*dc = Sanguosha->cloneCard("analeptic");
			dc->setSkillName("weizhuang");
			dc->deleteLater();
			if(dc->isAvailable(player))
				return true;
		}
		return false;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if(player->property("xichangBf").toString()!="xiuge"||!player->hasTurn()) return false;
		if(pattern.contains("slash")&&player->getMark("weizhuang_juguan_remove_slash-Clear")<1)
			return Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
		if(pattern.contains("jink")&&player->getMark("weizhuang_juguan_remove_jink-Clear")<1)
			return Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
		if(pattern.contains("peach")&&player->getMark("weizhuang_juguan_remove_peach-Clear")<1)
			return Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
		if(pattern.contains("analeptic")&&player->getMark("weizhuang_juguan_remove_analeptic-Clear")<1)
			return Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
		return false;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			const Card *dc = Self->getTag(objectName()).value<const Card *>();
			if(dc) pattern = dc->objectName();
			else return nullptr;
		}
		SkillCard*sc = new WeizhuangCard;
		sc->addSubcard(originalCard);
		sc->setUserString(pattern);
		return sc;
	}
};

class Weizhuang : public TriggerSkill
{
public:
	Weizhuang() : TriggerSkill("weizhuang&")
	{
		events << CardFinished << EventPhaseStart << TargetSpecified
		<< EventForDiy << DrawNCards << ConfirmDamage << HpRecover;
		view_as_skill = new Weizhuangvs;
	}
	QDialog *getDialog() const
	{
		return JuguanDialog::getInstance(objectName(), "slash,jink,peach,analeptic");
	}
	int getEffectIndex(const ServerPlayer *, const Card *) const
	{
		return qrand()%6+11;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())&&player->isAlive()){
				foreach (int id, room->getDrawPile()+room->getDiscardPile()) {
					if(use.card->hasFlag("weizhuangSuit"+Sanguosha->getCard(id)->getSuitString())){
						room->obtainCard(player,id);
						break;
					}
				}
			}
			if(use.card->isKindOf("EquipCard")&&player->property("xichangBf").toString()=="dongjiao"
			&&player->isAlive()&&player->getMark("dongjiao3-Clear")<1&&player->hasTurn()){
				QStringList ts;
				foreach (const Card*c, player->getHandcards()) {
					if(c->hasTip("mingzhi")){
						if(ts.contains(c->getType())) continue;
						ts.append(c->getType());
					}
				}
				foreach (const Card*c, player->getEquips()) {
					if(ts.contains(c->getType())) continue;
					ts.append(c->getType());
				}
				if(ts.length()<3) return false;
				QList<ServerPlayer *>tps;
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->hasEquip()){
						tps << p;
						continue;
					}
					foreach (const Card*ch, p->getHandcards()) {
						if(ch->hasTip("mingzhi")){
							tps << p;
							break;
						}
					}
				}
				ServerPlayer *tp = room->askForPlayerChosen(player,tps,"dongjiao3","dongjiao3",true);
				if(tp){
					int n = qrand()%6+5;
					player->peiyin(objectName(),n);
					player->addMark("dongjiao3-Clear");
					room->doAnimate(1,player->objectName(),tp->objectName());
					tp->drawCards(2,objectName());
				}
			}
		}else if(event == TargetSpecified){
			if(player->property("xichangBf").toString()!="dongjiao") return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("TrickCard")&&player->hasTurn()&&player->getMark("dongjiao2-Clear")<1){
				QStringList ts;
				foreach (const Card*c, player->getHandcards()) {
					if(c->hasTip("mingzhi")){
						if(ts.contains(c->getType())) continue;
						ts.append(c->getType());
					}
				}
				foreach (const Card*c, player->getEquips()) {
					if(ts.contains(c->getType())) continue;
					ts.append(c->getType());
				}
				if(ts.length()<2) return false;
				foreach (ServerPlayer *p, use.to) {
					if(p->isNude()) use.to.removeOne(p);
				}
				ServerPlayer *tp = room->askForPlayerChosen(player,use.to,"dongjiao2","dongjiao2",true);
				if(tp){
					int n = qrand()%6+5;
					player->peiyin(objectName(),n);
					player->addMark("dongjiao2-Clear");
					room->doAnimate(1,player->objectName(),tp->objectName());
					int id = room->askForCardChosen(player,tp,"he",objectName());
					if(id>=0) room->obtainCard(player,id,false);
				}
			}
		}else if(event == HpRecover){
			RecoverStruct recover = data.value<RecoverStruct>();
			if(recover.card&&recover.card->isKindOf("BasicCard")&&recover.who&&recover.who->isAlive()&&recover.who->property("xichangBf").toString()=="dongjiao"
			&&(recover.card->hasFlag("dongjiao1")||(player->hasTurn()&&recover.who->getMark("dongjiao1-Clear")<1))){
				bool has = recover.who->hasEquip();
				foreach (const Card*h, recover.who->getHandcards()) {
					if(h->hasTip("mingzhi")){
						has = true;
						break;
					}
				}
				if(has||recover.card->hasFlag("dongjiao1")){
					room->setCardFlag(recover.card,"dongjiao1");
					int n = qrand()%6+5;
					recover.who->peiyin(objectName(),n);
					recover.who->addMark("dongjiao1-Clear");
					recover.recover++;
					data.setValue(recover);
				}
			}
		}else if(event == ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("BasicCard")&&player->property("xichangBf").toString()=="dongjiao"
			&&(damage.card->hasFlag("dongjiao1")||(player->hasTurn()&&player->getMark("dongjiao1-Clear")<1))){
				bool has = player->hasEquip();
				foreach (const Card*h, player->getHandcards()) {
					if(h->hasTip("mingzhi")){
						has = true;
						break;
					}
				}
				if(has||damage.card->hasFlag("dongjiao1")){
					room->setCardFlag(damage.card,"dongjiao1");
					int n = qrand()%6+5;
					player->peiyin(objectName(),n);
					player->addMark("dongjiao1-Clear");
					player->damageRevises(data,1);
				}
			}
		}else if(event == DrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
			if (draw.reason=="draw_phase"&&player->getMark("guidian1")!=0) {
				draw.num += player->getMark("guidian1");
				data.setValue(draw);
			}
		}else if(event == EventForDiy){
			if(data.toString().contains("mingzhi:")){
				QStringList ids = data.toString().split(":").last().split("+");
				int x = room->getPlayers().length();
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					for (int i = 0; i < ids.length(); i++) {
						p->addMark("mingzhiNum");
						if(p->getMark("mingzhiNum")<=x) continue;
						p->setMark("mingzhiNum",0);
						if(p->getMark("guidianNum")>x||p->property("xichangBf").toString()!="guidian") continue;
						p->addMark("guidianNum");
						int n = qrand()%4+1;
						p->peiyin(objectName(),n);
						QString choice = room->askForChoice(p,"guidian","1+2+3+4");
						if(choice=="1")
							p->addMark("guidian"+choice);
						else if(choice=="2")
							room->addPlayerMark(p,"guidian"+choice);
						else if(choice=="3")
							room->addMaxCards(p,1,false);
						else
							room->setPlayerProperty(p,"hp",p->getHp()+1);
						if(p->isDead()) break;
					}
				}
			}
		}else if(event == EventPhaseStart){
			if (player->getPhase()==Player::Finish){
				bool has = player->hasEquip();
				foreach (const Card*h, player->getHandcards()) {
					if(h->hasTip("mingzhi")){
						has = true;
						break;
					}
				}
				if(has){
					foreach (ServerPlayer *p, room->getAllPlayers()) {
						if(p->property("xichangBf").toString()!="guidian") continue;
						if(p->askForSkillInvoke("guidian",data,false)){
							int n = qrand()%4+1;
							p->peiyin(objectName(),n);
							QString choice = room->askForChoice(p,"guidian","1+2+3+4");
							if(choice=="1")
								p->addMark("guidian"+choice,-1);
							else if(choice=="2")
								room->addPlayerMark(p,"guidian"+choice,-1);
							else if(choice=="3")
								room->addMaxCards(p,-1,false);
							else{
								room->setPlayerProperty(p,"hp",p->getHp()-1);
							}
							if(p->isDead()) continue;
							const TriggerSkill*cq = Sanguosha->getTriggerSkill("caiqiu");
							if(cq) cq->trigger(RoundStart,room,p,data);
						}
					}
				}
			}
		}
		return false;
	}
};

class Shiju : public TriggerSkill
{
public:
	Shiju() : TriggerSkill("shiju")
	{
		events << CardFinished << CardUsed;
		frequency = Compulsory;
		waked_skills = "kubai,#KubaiBf";
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			QStringList cs = room->getTag("shijuUseCard").toStringList();
			if(cs.length()>1){
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(!p->hasSkill(objectName(),true)) continue;
					foreach (QString m, p->getMarkNames()) {
						if(m.contains("&shiju+"))
							room->setPlayerMark(p,m,0);
					}
					room->setPlayerMark(p,"&shiju+"+cs[1]+"_char+"+cs[2],1);
				}
			}
			if(use.card->hasFlag("shijuBf")&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				if(use.card->hasFlag("shijuBf1"))
					room->obtainCard(player,room->drawCard());
				if(use.card->hasFlag("shijuBf2"))
					room->obtainCard(player,room->drawCard(false));
				if(use.card->hasFlag("shijuBf1")&&use.card->hasFlag("shijuBf2")&&use.card->hasFlag("shijuBf3")){
					if(player->hasSkill("kubai",true)){
						int n = player->property("kubaiUp").toInt()+1;
						room->setPlayerProperty(player,"kubaiUp",n);
						if(n<4) room->changeTranslation(player,"kubai",n);
					}else{
						room->setPlayerProperty(player,"kubaiUp",1);
						room->acquireSkill(player,"kubai");
					}
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				QStringList cs,cs2 = room->getTag("shijuUseCard").toStringList();
				cs << use.card->getType() << use.card->getSuitString() << use.card->objectName();
				room->setTag("shijuUseCard",cs);
				if(cs.length()==cs2.length()){
					for (int i = 0; i < cs.length(); i++) {
						if(cs.at(i)==cs2.at(i)||(i==2&&use.card->sameNameWith(cs2.at(i)))){
							use.card->setFlags("shijuBf");
							use.card->setFlags("shijuBf"+QString::number(i+1));
						}
					}
				}
			}
		}
		return false;
	}
};

class Kubai : public TriggerSkill
{
public:
	Kubai() : TriggerSkill("kubai")
	{
		events << CardUsed;
		frequency = Compulsory;
		waked_skills = "#KubaiBf,#KubaiLimit";
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->hasFlag("CurrentPlayer")){
				if(player->hasSkill(objectName())){
					int n = player->property("kubaiUp").toInt();
					if(n<=1){
						if(player->getMark(use.card->getColorString()+"_kubaiColor-Clear")==1){
							room->sendCompulsoryTriggerLog(player,this,qrand()%2+1);
							player->drawCards(1,objectName());
						}
					}else if(n==2){
						if(player->getMark(use.card->getSuitString()+"_kubaiSuit-Clear")==1){
							room->sendCompulsoryTriggerLog(player,this,qrand()%2+3);
							player->drawCards(1,objectName());
						}
					}else{
						if(player->getMark(use.card->getNumberString()+"_kubaiNumber-Clear")==1){
							room->sendCompulsoryTriggerLog(player,this,qrand()%2+5);
							player->drawCards(1,objectName());
						}
					}
				}
			}
		}
		return false;
	}
};

class KubaiBf : public TriggerSkill
{
public:
	KubaiBf() : TriggerSkill("#KubaiBf")
	{
		events << CardUsed << EventAcquireSkill;
	}
	int getPriority(TriggerEvent) const
	{
		return 4;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->hasFlag("CurrentPlayer")){
				player->addMark(use.card->getColorString()+"_kubaiColor-Clear");
				player->addMark(use.card->getSuitString()+"_kubaiSuit-Clear");
				player->addMark(use.card->getNumberString()+"_kubaiNumber-Clear");
				if(player->hasSkill("kubai",true)){
					int n = player->property("kubaiUp").toInt();
					QStringList strs;
					foreach (QString m, player->getMarkNames()) {
						if(m.contains("&kubai+"))
							room->setPlayerMark(player,m,0);
						else if(m.contains("_kubai")){
							QStringList ms = m.split("_");
							if(n<=1){
								if(m.contains("_kubaiColor"))
									strs << ms.first();
							}else if(n==2){
								if(m.contains("_kubaiSuit"))
									strs << ms.first()+"_char";
							}else{
								if(m.contains("_kubaiNumber"))
									strs << ms.first();
							}
						}
					}
					room->setPlayerMark(player,"&kubai+"+strs.join("+")+"-Clear",1);
				}
			}
		}else{
			if(player->hasSkill("kubai",true)){
				int n = player->property("kubaiUp").toInt();
				QStringList strs;
				foreach (QString m, player->getMarkNames()) {
					if(m.contains("&kubai+"))
						room->setPlayerMark(player,m,0);
					else if(m.contains("_kubai")){
						QStringList ms = m.split("_");
						if(n<=1){
							if(m.contains("_kubaiColor"))
								strs << ms.first();
						}else if(n==2){
							if(m.contains("_kubaiSuit"))
								strs << ms.first()+"_char";
						}else{
							if(m.contains("_kubaiNumber"))
								strs << ms.first();
						}
					}
				}
				if(strs.length()>0)
					room->setPlayerMark(player,"&kubai+"+strs.join("+")+"-Clear",1);
			}
		}
		return false;
	}
};

class KubaiLimit : public CardLimitSkill
{
public:
	KubaiLimit() : CardLimitSkill("#KubaiLimit")
	{
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target, const Card*card) const
	{
		foreach (const Player *p, target->getAliveSiblings()) {
			if(p->hasFlag("CurrentPlayer")&&p->hasSkill("kubai")){
				int n = p->property("kubaiUp").toInt();
				foreach (QString m, p->getMarkNames()) {
					if(m.contains("&kubai+")&&p->getMark(m)>0){
						m.remove("_char");
						m.remove("-Clear");
						QStringList ms = m.split("+");
						if(n>=3){
							if(ms.contains(card->getNumberString()))
								break;
						}else if(n==2){
							if(ms.contains(card->getSuitString()))
								break;
						}else{
							if(ms.contains(card->getColorString()))
								break;
						}
						return card->toString();
					}
				}
			}
		}
		return "";
	}
};

/*static double calculate_pi(int num_terms) {
    double pi = 0.0;
    for (int k = 0; k < num_terms; k++) {
        pi += (1.0 / pow(16, k)) * 
			(4.0 / (8 * k + 1) - 
			2.0 / (8 * k + 4) - 
			1.0 / (8 * k + 5) - 
			1.0 / (8 * k + 6));
    }//可恶，神杀承受不了这个圆周率计算量.....
    return pi;
}*/
static QString sxstr = "1415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679821480865132823066470938446095505822317253594081284811174502841027019385211055596446229489549303819644288109756659334461284756482337867831652712019091456485669234603486104543266482133936072602491412737245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094330572703657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566430860213949463952247371907021798609437027705392171762931767523846748184676694051320005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235420199561121290219608640344181598136297747713099605187072113499999983729780499510597317328160963185950244594553469083026425223082533446850352619311881710100031378387528865875332083814206171776691473035982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989";

class MobileGeyuan : public TriggerSkill
{
public:
	MobileGeyuan() : TriggerSkill("mobilegeyuan")
	{
		events << CardUsed;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				int n = use.card->getNumber();
				if(n<1) return false;
				int x = player->getTag("geyuanNum").toInt();
				if(n>9&&QString(sxstr.at(x))=="0"&&player->hasSkill("chongcha")){
					room->sendCompulsoryTriggerLog(player,"chongcha",true,true);
					n = 0;
				}
				if(QString::number(n)==QString(sxstr.at(x))&&player->askForSkillInvoke(objectName()+"$-1",data)){
					player->addMark("mobilegeyuanUse-Clear");
					n = player->getMark("mobilegeyuanUse-Clear");
					player->drawCards(qMin(n,3),objectName());
					player->setTag("geyuanNum", x+1);
					foreach (QString m, player->getMarkNames()) {
						if(m.contains("&mobilegeyuan+:+"))
							room->setPlayerMark(player,m,0);
					}
					room->setPlayerMark(player,QString("&mobilegeyuan+:+%1").arg(sxstr.at(x+1)),1);
				}
			}
		}
		return false;
	}
};

ChongchaCard::ChongchaCard()
{
	target_fixed = true;
}

void ChongchaCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	int x = source->getTag("geyuanNum").toInt()+1;
	source->setTag("geyuanNum", x);
	foreach (QString m, source->getMarkNames()) {
		if(m.contains("&mobilegeyuan+:+"))
			room->setPlayerMark(source,m,0);
	}
	room->setPlayerMark(source,QString("&mobilegeyuan+:+%1").arg(sxstr.at(x)),1);
}

class Chongcha : public OneCardViewAsSkill
{
public:
	Chongcha() : OneCardViewAsSkill("chongcha")
	{
		waked_skills = "#ChongchaLimit";
	}

	bool viewFilter(const Card *to_select) const
	{
		return !Self->isJilei(to_select);
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("ChongchaCard")<1&&player->canDiscard("he");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		SkillCard*sc = new ChongchaCard;
		sc->addSubcard(originalCard);
		return sc;
	}
};

class ChongchaLimit : public CardLimitSkill
{
public:
	ChongchaLimit() : CardLimitSkill("#ChongchaLimit")
	{
	}

	QString limitList(const Player *) const
	{
		return "ignore";
	}

	QString limitPattern(const Player *target, const Card*card) const
	{
		if(card->getNumber()>9&&target->hasSkill("chongcha")){
			return card->toString();
		}
		return "";
	}
};

class Shouyue : public TriggerSkill
{
public:
	Shouyue() : TriggerSkill("shouyue")
	{
		events << EventPhaseStart << HpChanged;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if(player->getPhase()!=Player::Draw)
				return false;
		}else{
			if(data.canConvert<RecoverStruct>())
				return false;
		}
		ServerPlayer *tp = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName()+"$-1","shouyue0",true,true);
		if(tp){
			if((tp->isChained()||!tp->faceUp())&&room->askForChoice(player,objectName(),"shouyue1+shouyue2")=="shouyue2"){
				room->setPlayerChained(tp,true);
				if(!tp->faceUp()) tp->turnOver();
			}else{
				player->drawCards(1,objectName());
				if(tp->hasSkill("qinyin",true))
					tp->drawCards(1,objectName());
				else
					room->acquireSkill(tp,"qinyin");
			}
		}
		return false;
	}
};

DieyinCard::DieyinCard()
{
	target_fixed = true;
}

void DieyinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	source->turnOver();
	QStringList choices;
	for (int i = 1; i < 7; i++) {
		choices << QString("phase%1").arg(i);
	}
	QString choice = room->askForChoice(source,"dieyin",choices.join("+"));
	choice = QString(choice.at(5));
	source->insertPhase((Player::Phase)choice.toInt());
}

class Dieyin : public ZeroCardViewAsSkill
{
public:
	Dieyin() : ZeroCardViewAsSkill("dieyin")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("DieyinCard");
	}

	const Card *viewAs() const
	{
		return new DieyinCard;
	}
};

JinchuCard::JinchuCard()
{
	will_throw = false;
}

bool JinchuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && to_select->getHandcardNum()>1;
}

void JinchuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		Card*dc = room->askForExchange(p,"jinchu",2,2,false,"jinchu0");
		room->showCard(source,subcards);
		if(dc){
			room->showCard(p,dc->getSubcards());
			QList<int>cns;
			cns << getNumber();
			foreach (int id, dc->getSubcards())
				cns << Sanguosha->getCard(id)->getNumber();
			int max = cns.last(),min = cns.last();
			foreach (int n, cns){
				max = qMax(max,n);
				min = qMax(min,n);
			}
			if(getNumber()==max||getNumber()==min){
				cns.clear();
				if(source->canDiscard(getEffectiveId())){
					cns << getEffectiveId();
					room->addPlayerMark(source,"jinchuNum-PlayClear");
				}
				foreach (int id, dc->getSubcards()){
					if(source->canDiscard(p,id))
						cns << id;
				}
				room->throwCard(cns,"jinchu",source);
			}else{
				room->setPlayerMark(p,"&jinchu+#"+source->objectName()+"-Clear",1);
			}
		}
	}
}

class Jinchuvs : public OneCardViewAsSkill
{
public:
	Jinchuvs() : OneCardViewAsSkill("jinchu")
	{
	}

	bool viewFilter(const Card *to_select) const
	{
		return !to_select->isEquipped();
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("JinchuCard")<=player->getMark("jinchuNum-PlayClear")&&player->getHandcardNum()>0;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		SkillCard*sc = new JinchuCard;
		sc->addSubcard(originalCard);
		return sc;
	}
};

class Jinchu : public TriggerSkill
{
public:
	Jinchu() : TriggerSkill("jinchu")
	{
		events << CardUsed << ConfirmDamage;
		view_as_skill = new Jinchuvs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")){
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->getMark("&jinchu+#"+player->objectName()+"-Clear")>0){
						room->setPlayerMark(p,"&jinchu+#"+player->objectName()+"-Clear",0);
						use.no_respond_list << p->objectName();
						room->setCardFlag(use.card,"jinchuBf"+p->objectName());
						data.setValue(use);
					}
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("jinchuBf"+damage.to->objectName()))
				damage.to->damageRevises(data,1);
		}
		return false;
	}
};

class MobileAnxian : public TriggerSkill
{
public:
	MobileAnxian() : TriggerSkill("mobileanxian")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from == player&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD
			&& move.from_places.contains(Player::PlaceHand)&&player->getMark("mobileanxianHas-Clear")<1) {
				QList<int>ids;
				for (int i = 0; i < move.card_ids.length(); i++) {
					if (move.from_places.at(i) == Player::PlaceHand) {
						player->addMark("mobileanxianHas-Clear");
						if(Sanguosha->getCard(move.card_ids.at(i))->isKindOf("Slash"))
							ids << move.card_ids.at(i);
					}
				}
				if(ids.length()>0){
					room->fillAG(ids,player);
					if(player->askForSkillInvoke(objectName()+"$-1",data)){
						int id = room->askForAG(player,ids,false,objectName());
						room->obtainCard(player,id);
						if(player->handCards().contains(id)){
							room->setPlayerFlag(player,"mobileanxianBf"+QString::number(id));
							room->askForUseCard(player,QString("%1!").arg(id),"mobileanxian0",-1,Card::MethodUse,false);
							room->setPlayerFlag(player,"-mobileanxianBf"+QString::number(id));
						}
					}
					room->clearAG(player);
				}
			}
		}
		return false;
	}
};

class Dingfa : public TriggerSkill
{
public:
	Dingfa() : TriggerSkill("dingfa")
	{
		events << RoundStart << RoundEnd << Damage << CardsMoveOneTime << HpChanged;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == RoundStart) {
			if(player->hasSkill(objectName())&&player->askForSkillInvoke(objectName()+"$-1")){
				QStringList choices,choices2;
				choices << "dingfa1" << "dingfa2" << "dingfa3" << "dingfa4";
				for (int i = 0; i < 2; i++) {
					QString choice = room->askForChoice(player,objectName(),choices.join("+"));
					choices.removeOne(choice);
					choices2 << choice;
				}
				choices.clear();
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					if(p->faceUp()){
						if(p->getMark("shuxingBf"+player->objectName())>0){
							p->setMark("shuxingBf"+player->objectName(),0);
							p = player;
						}
						QString choice = room->askForChoice(p,objectName(),choices2.join("+"),QVariant::fromValue(player));
						choices << choice;
					}
				}
				player->setTag("dingfa_lv", choices);
			}
		}else if(event == RoundEnd){
			if(player->hasSkill(objectName())){
				QHash<QString,int>s2n;
				int n = 0;
				foreach (QString ch, player->getTag("dingfa_lv").toStringList()){
					s2n[ch]++;
					n = qMax(n,s2n[ch]);
				}
				foreach (QString ch, s2n.keys()){
					if(s2n[ch]>=n){
						if(ch=="dingfa4"){
							int x = 0;
							foreach (ServerPlayer *p, room->getAlivePlayers())
								x = qMax(x,p->getCardCount(true,true));
							QList<ServerPlayer *>tps;
							foreach (ServerPlayer *p, room->getAllPlayers()) {
								if(p->getCardCount(true,true)>=x) tps << p;
							}
							foreach (ServerPlayer *p, tps) {
								foreach (ServerPlayer *q, room->getOtherPlayers(p)) {
									if(p->isDead()) break;
									Card*dc = room->askForExchange(p,objectName(),1,1,true,"dingfa40:"+q->objectName());
									if(dc) room->giveCard(p,q,dc,objectName());
								}
							}
						}else{
							int x = 0;
							foreach (ServerPlayer *p, room->getAlivePlayers())
								x = qMax(x,p->getMark(ch+"_lun"));
							QList<ServerPlayer *>tps;
							foreach (ServerPlayer *p, room->getAllPlayers()) {
								if(p->getMark(ch+"_lun")>=x) tps << p;
							}
							foreach (ServerPlayer *p, tps) {
								if(ch=="dingfa1"){
									room->damage(DamageStruct(objectName(),nullptr,p,2));
								}else if(ch=="dingfa2"){
									room->addMaxCards(p,2,false);
								}else if(ch=="dingfa3"){
									room->loseMaxHp(p,1,objectName());
								}
							}
						}
					}
				}
			}
		}else if(event == Damage){
			DamageStruct damage = data.value<DamageStruct>();
			player->addMark("dingfa1_lun",damage.damage);
		}else if (event == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&move.to==player&&move.reason.m_skillName!="InitialHandCards")
				player->addMark("dingfa2_lun",move.card_ids.length());
		}else if(event == HpChanged){
			player->addMark("dingfa3_lun");
		}
		return false;
	}
};

class Shuxing : public TriggerSkill
{
public:
	Shuxing() : TriggerSkill("shuxing")
	{
		events << TargetSpecified;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetConfirming) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(p->hasFlag("CurrentPlayer")&&player->getMark(p->objectName()+"shuxingUse-Clear")<1
						&&p->hasSkill(objectName())&&p->askForSkillInvoke(objectName()+"$-1",player)){
						player->addMark(p->objectName()+"shuxingUse-Clear");
						use.nullified_list << p->objectName();
						data.setValue(use);
						room->showAllCards(player);
						Card*dc = dummyCard();
						foreach(const Card*h,player->getHandcards()){
							if(h->isKindOf("Jink")) dc->addSubcard(h);
						}
						if(dc->subcardsLength()>0){
							if(player->askForSkillInvoke("shuxing0","0:"+p->objectName(),false)){
								room->giveCard(player,p,dc,objectName(),true);
								player->addMark("shuxingBf"+p->objectName());
							}else
								room->loseHp(player,1,true,p,objectName());
						}
						if(player->isDead()) break;
					}
				}
			}
		}
		return false;
	}
};









mobilePackage::mobilePackage()
	: Package("mobile")
{
	General *mobile_zhouyu = new General(this, "mobile_zhouyu", "wu", 3);
	mobile_zhouyu->addSkill(new Shouyue);
	mobile_zhouyu->addSkill(new Dieyin);
	addMetaObject<DieyinCard>();

	General *mobile_caoxing = new General(this, "mobile_caoxing", "qun", 4);
	mobile_caoxing->addSkill(new Jinchu);
	mobile_caoxing->addSkill(new MobileAnxian);
	addMetaObject<JinchuCard>();




	General *mobile_sunru = new General(this, "mobile_sunru", "wu", 3, false);
	mobile_sunru->addSkill(new Yingjian);
	mobile_sunru->addSkill(new SlashNoDistanceLimitSkill("yingjian"));
	mobile_sunru->addSkill("shixin");
	related_skills.insertMulti("yingjian", "#yingjian-slash-ndl");

	General *liuzan = new General(this, "liuzan", "wu");
	liuzan->addSkill(new Fenyin);

	General *caochun = new General(this, "caochun", "wei", 4);
	caochun->addSkill(new Shanjia("shanjia"));

	skills << new Shanjia("olshanjia") << new SlashNoDistanceLimitSkill("olshanjia");
	addMetaObject<ShanjiaCard>();
	addMetaObject<OLShanjiaCard>();

	General *pangdegong = new General(this, "pangdegong", "qun", 3);
	pangdegong->addSkill(new Pingcai);
	pangdegong->addSkill(new Yinshiy);
	pangdegong->addSkill(new YinshiyPro);
	related_skills.insertMulti("yinshiy", "#yinshiy-pro");
	addMetaObject<PingcaiCard>();

	General *simashi = new General(this, "simashi", "wei", 4);
	simashi->addSkill(new Jinglve);
	simashi->addSkill(new Baiyi);
	simashi->addSkill(new Shanli);
	addMetaObject<BaiyiCard>();
	addMetaObject<JinglveCard>();

	General *mobile_miheng = new General(this, "mobile_miheng", "qun", 3);
	mobile_miheng->addSkill(new MobileKuangcai);
	mobile_miheng->addSkill(new MobileKuangcaiMod);
	mobile_miheng->addSkill(new MobileShejian);

	General *shichangshi = new General(this, "shichangshi", "qun", 1);
	shichangshi->setGender(General::Sexless);
	shichangshi->addSkill(new Danggu);
	shichangshi->addSkill(new Mowang);

	General *cs_zhangrang = new General(this, "cs_zhangrang", "qun", 0, true, true);
	cs_zhangrang->setGender(General::Sexless);
	cs_zhangrang->addSkill(new CsTaoluan);

	General *cs_zhaozhong = new General(this, "cs_zhaozhong", "qun", 0, true, true);
	cs_zhaozhong->setGender(General::Sexless);
	cs_zhaozhong->addSkill(new CsChiyan);

	General *cs_sunzhang = new General(this, "cs_sunzhang", "qun", 0, true, true);
	cs_sunzhang->setGender(General::Sexless);
	cs_sunzhang->addSkill(new CsZimou);

	General *cs_bilan = new General(this, "cs_bilan", "qun", 0, true, true);
	cs_bilan->setGender(General::Sexless);
	cs_bilan->addSkill(new CsPicai);
	addMetaObject<CsPicaiCard>();

	General *cs_xiayun = new General(this, "cs_xiayun", "qun", 0, true, true);
	cs_xiayun->setGender(General::Sexless);
	cs_xiayun->addSkill(new CsYaozhuo);
	addMetaObject<CsYaozhuoCard>();

	General *cs_hanli = new General(this, "cs_hanli", "qun", 0, true, true);
	cs_hanli->setGender(General::Sexless);
	cs_hanli->addSkill(new CsXiaolu);
	addMetaObject<CsXiaoluCard>();
	addMetaObject<CsXiaolu2Card>();

	General *cs_lisong = new General(this, "cs_lisong", "qun", 0, true, true);
	cs_lisong->setGender(General::Sexless);
	cs_lisong->addSkill(new CsKuiji);
	addMetaObject<CsKuijiCard>();
	addMetaObject<CsKuijiDisCard>();

	General *cs_duangui = new General(this, "cs_duangui", "qun", 0, true, true);
	cs_duangui->setGender(General::Sexless);
	cs_duangui->addSkill(new CsChihe);
	cs_duangui->addSkill(new CsChiheEffect);
	cs_duangui->addSkill(new CsChiheLimit);

	General *cs_guosheng = new General(this, "cs_guosheng", "qun", 0, true, true);
	cs_guosheng->setGender(General::Sexless);
	cs_guosheng->addSkill(new CsNiqu);
	addMetaObject<CsNiquCard>();

	General *cs_gaowang = new General(this, "cs_gaowang", "qun", 0, true, true);
	cs_gaowang->setGender(General::Sexless);
	cs_gaowang->addSkill(new CsMiaoyu);

	General *mobileyou_zhugeliang = new General(this, "mobileyou_zhugeliang", "qun", 3);
	mobileyou_zhugeliang->addSkill(new Yance);
	mobileyou_zhugeliang->addSkill(new Fangqiu);
	mobileyou_zhugeliang->addSkill(new ZGGongli);

	General *mobileyou_pangtong = new General(this, "mobileyou_pangtong", "qun", 3);
	mobileyou_pangtong->addSkill(new MobileManjuan);
	mobileyou_pangtong->addSkill(new Yangming);
	mobileyou_pangtong->addSkill(new PTGongli);

	General *mobileyou_xushu = new General(this, "mobileyou_xushu", "qun", 3);
	mobileyou_xushu->addSkill(new Xiaxing);
	mobileyou_xushu->addSkill(new Qihui);
	mobileyou_xushu->addSkill(new XSGongli);

	General *mobileyou_shitao = new General(this, "mobileyou_shitao", "qun", 3);
	mobileyou_shitao->addSkill(new Qinying);
	mobileyou_shitao->addSkill(new Lunxiong);
	mobileyou_shitao->addSkill(new STGongli);
	addMetaObject<QinyingCard>();

	General *mobileyou_cuijun = new General(this, "mobileyou_cuijun", "qun", 3);
	mobileyou_cuijun->addSkill(new Shunyi);
	mobileyou_cuijun->addSkill(new Biwei);
	mobileyou_cuijun->addSkill(new CJGongli);
	addMetaObject<BiweiCard>();

	Card*c = new Xuanjian(Card::Spade,9);
	c->setParent(this);
	skills << new XuanjianVS;

	General *cuifu = new General(this, "cuifu", "wei", 3, false);
	cuifu->addSkill(new Caiqiu);
	cuifu->addSkill(new Xichang);
	skills << new Weizhuang;
	addMetaObject<WeizhuangCard>();

	General *mobile_zhangzhi = new General(this, "mobile_zhangzhi", "qun", 3);
	mobile_zhangzhi->addSkill(new Shiju);
	skills << new Kubai << new KubaiBf << new KubaiLimit;

	General *mobile_liuhui = new General(this, "mobile_liuhui", "qun", 3);
	mobile_liuhui->addSkill(new MobileGeyuan);
	mobile_liuhui->addSkill(new Chongcha);
	mobile_liuhui->addSkill(new ChongchaLimit);
	addMetaObject<ChongchaCard>();

	General *luyu = new General(this, "luyu", "wei", 3);
	luyu->addSkill(new Dingfa);
	luyu->addSkill(new Shuxing);










}
ADD_PACKAGE(mobile)

class XingWeifeng : public TriggerSkill
{
public:
	XingWeifeng() : TriggerSkill("xingweifeng")
	{
		events << CardFinished << EventPhaseStart << DamageInflicted << Death;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardFinished) {
			if (player->isDead() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play) return false;
			if (player->getMark("xingweifeng-PlayClear") > 0) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash") && !use.card->isKindOf("FireAttack") && !use.card->isKindOf("Duel") &&
					!use.card->isKindOf("ArcheryAttack") && !use.card->isKindOf("SavageAssault")) return false;
			QList<ServerPlayer *> targets;
			foreach (ServerPlayer *p, use.to) {
				if (p == player || p->isDead() || hasJuMark(p)) continue;
				targets << p;
			}
			if (targets.isEmpty()) return false;

			room->addPlayerMark(player, "xingweifeng-PlayClear");
			ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@xingweifeng-invoke", false, true);
			room->broadcastSkillInvoke(objectName());
			QString name = use.card->objectName();
			if (use.card->isKindOf("Slash"))
				name = "slash";
			target->gainMark("&xingju+[+" + name + "+]");
		} else if (event == DamageInflicted) {
			DamageStruct damage = data.value<DamageStruct>();
			const Card *card = damage.card;
			if (!card || player->isDead() || !hasJuMark(player)) return false;
			if (!card->isKindOf("Slash") && !card->isKindOf("FireAttack") && !card->isKindOf("Duel")
				&& !card->isKindOf("ArcheryAttack") && !card->isKindOf("SavageAssault")) return false;

			QString name = card->objectName();
			if (card->isKindOf("Slash"))
				name = "slash";

			foreach (QString mark, player->getMarkNames()) {
				if (mark.startsWith("&xingju") && player->getMark(mark) > 0) {
					QStringList m = mark.split("+");
					QString mm = m.at(2);

					player->loseAllMarks(mark);

					int n = damage.damage;
					int x = damage.damage;
					if (mm == name) {
						foreach (ServerPlayer *p, room->getAllPlayers()) {
							if (p->isDead() || !p->hasSkill(objectName())) continue;
							room->sendCompulsoryTriggerLog(p, objectName(), true, true);
							n++;
						}
						if (x == n) continue;
						LogMessage log;
						log.type = "#XingWeifeng";
						log.from = player;
						log.arg = QString::number(x);
						log.arg2 = QString::number(n);
						room->sendLog(log);

						damage.damage = n;
						data = QVariant::fromValue(damage);
					} else {
						foreach (ServerPlayer *p, room->getAllPlayers()) {
							if (p->isDead() || !p->hasSkill(objectName()) || player->isKongcheng()) continue;
							room->sendCompulsoryTriggerLog(p, objectName(), true, true);

							int id = room->askForCardChosen(p, player, "he", objectName(), false);
							CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, p->objectName());
							room->obtainCard(p, Sanguosha->getCard(id), reason, room->getCardPlace(id) != Player::PlaceHand);
						}
					}
				}
			}
		} else {
			if (event == EventPhaseStart) {
				if (player->isDead() || !player->hasSkill(objectName())) return false;
				if (player->getPhase() != Player::Start) return false;
			} else if (event == Death) {
				ServerPlayer *who = data.value<DeathStruct>().who;
				if (who != player || !who->hasSkill(objectName())) return false;
			}

			bool flag = false;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (hasJuMark(p)) {
					flag = true;
					break;
				}
			}
			if (!flag) return false;

			room->sendCompulsoryTriggerLog(player, objectName(), true, true);

			foreach (ServerPlayer *p, room->getAllPlayers()) {
				foreach (QString mark, p->getMarkNames()) {
					if (mark.startsWith("&xingju") && p->getMark(mark) > 0)
						p->loseAllMarks(mark);
				}
			}
		}
		return false;
	}
private:
	bool hasJuMark(ServerPlayer *player) const
	{
		foreach (QString mark, player->getMarkNames()) {
			if (mark.startsWith("&xingju") && player->getMark(mark) > 0)
				return true;
		}
		return false;
	}
};

XingZhilveSlashCard::XingZhilveSlashCard()
{
	mute = true;
	m_skillName = "xingzhilve";
}

bool XingZhilveSlashCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *slash = Sanguosha->cloneCard("slash");
	slash->setSkillName("_xingzhilve");
	slash->deleteLater();
	return slash->targetFilter(targets, to_select, Self);
}

void XingZhilveSlashCard::onUse(Room *room, CardUseStruct &card_use) const
{
	if (card_use.from->isDead()) return;
	Card *slash = Sanguosha->cloneCard("slash");
	slash->setSkillName("_xingzhilve");
	slash->deleteLater();
	room->useCard(CardUseStruct(slash, card_use.from, card_use.to), false);
}

XingZhilveCard::XingZhilveCard()
{
	target_fixed = true;
}

void XingZhilveCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->loseHp(HpLostStruct(source, 1, "xingzhilve", source));
	if (source->isDead()) return;
	room->addMaxCards(source, 1);

	QStringList choices;
	if (room->canMoveField())
		choices << "move";
	choices << "draw";

	if (room->askForChoice(source, "xingzhilve", choices.join("+")) == "move")
		room->moveField(source, "xingzhilve");
	else {
		source->drawCards(1, "xingzhilve");
		if (source->isDead()) return;

		Card *slash = Sanguosha->cloneCard("slash");
		slash->setSkillName("_xingzhilve");
		slash->deleteLater();

		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
			if (source->canSlash(p, slash, false))
				targets << p;
		}
		if (targets.isEmpty()) return;
		if (!room->askForUseCard(source, "@@xingzhilve!", "@xingzhilve")) {
			ServerPlayer *target = targets.at(qrand() % targets.length());
			room->useCard(CardUseStruct(slash, source, target), false);
		}
	}
}

class XingZhilve : public ZeroCardViewAsSkill
{
public:
	XingZhilve() : ZeroCardViewAsSkill("xingzhilve")
	{
		response_pattern = "@@xingzhilve!";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("XingZhilveCard");
	}

	const Card *viewAs() const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUsePattern() == "@@xingzhilve!")
			return new XingZhilveSlashCard;
		return new XingZhilveCard;
	}
};

XingZhiyanCard::XingZhiyanCard()
{
	handling_method = Card::MethodNone;
	will_throw = false;
}

bool XingZhiyanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if(getSubcards().isEmpty()) return false;
	return targets.isEmpty() && to_select != Self;
}

bool XingZhiyanCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return !targets.isEmpty()||getSubcards().isEmpty();
}

void XingZhiyanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &tos) const
{
	if(getSubcards().isEmpty()){
		if (source->isDead()) return;
		room->addPlayerMark(source, "xingzhiyan_draw-PlayClear");
		int draw = source->getMaxHp()-source->getHandcardNum();
		if (draw <= 0) return;
		source->drawCards(draw, "xingzhiyan");
	}else{
		room->addPlayerMark(source, "xingzhiyan_give-PlayClear");
		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(), tos.first()->objectName(), "xingzhiyan", "");
		room->obtainCard(tos.first(), this, reason, false);
	}
}

class XingZhiyan : public ViewAsSkill
{
public:
	XingZhiyan() : ViewAsSkill("xingzhiyan")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (Self->getMark("xingzhiyan_give-PlayClear")<1)
			return selected.length()<(Self->getHandcardNum()-Self->getHp())&&!to_select->isEquipped();
		return false;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.isEmpty()){
			if(Self->getMark("xingzhiyan_draw-PlayClear")<1&&Self->getMaxHp()>Self->getHandcardNum())
				return new XingZhiyanCard;
		}else{
			if(cards.length()==Self->getHandcardNum()-Self->getHp()&&Self->getMark("xingzhiyan_give-PlayClear")<1){
				XingZhiyanCard *c = new XingZhiyanCard;
				c->addSubcards(cards);
				return c;
			}
		}
		return nullptr;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return (player->getMark("xingzhiyan_draw-PlayClear")<1&&player->getMaxHp()>player->getHandcardNum())
		||(player->getMark("xingzhiyan_give-PlayClear")<1&&player->getHandcardNum()>player->getHp());
	}
};

class XingZhiyanPro : public ProhibitSkill
{
public:
	XingZhiyanPro() : ProhibitSkill("#xingzhiyan-pro")
	{
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		return from != to && from->getMark("xingzhiyan_draw-PlayClear") > 0 && !card->isKindOf("SkillCard");
	}
};

XingJinfanCard::XingJinfanCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void XingJinfanCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	source->addToPile("&xingling", this);
}

class XingJinfanVS : public ViewAsSkill
{
public:
	XingJinfanVS(const QString &xingjinfan_skill) : ViewAsSkill(xingjinfan_skill), xingjinfan_skill(xingjinfan_skill)
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		QStringList suits;
		foreach (int id, Self->getPile("&xingling"))
			suits << Sanguosha->getCard(id)->getSuitString();
		foreach (const Card *c, selected)
			suits << c->getSuitString();
		return !to_select->isEquipped() && !suits.contains(to_select->getSuitString());
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty()) return nullptr;

		XingJinfanCard *c = new XingJinfanCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@" + xingjinfan_skill;
	}

private:
	QString xingjinfan_skill;
};

class XingJinfan : public TriggerSkill
{
public:
	XingJinfan() : TriggerSkill("xingjinfan")
	{
		events << EventPhaseStart << CardsMoveOneTime;
		view_as_skill = new XingJinfanVS("xingjinfan");
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Discard || player->isKongcheng()) return false;
			room->askForUseCard(player, "@@xingjinfan", "@xingjinfan");
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from == player && move.from_pile_names.contains("&xingling")) {
				for (int i = 0; i < move.card_ids.length(); i++) {
					if (move.from_pile_names.at(i) == "&xingling") {
						QList<int> list;
						foreach (int id, room->getDrawPile()) {
							if (Sanguosha->getCard(id)->getSuit() == Sanguosha->getCard(move.card_ids.at(i))->getSuit())
								list << id;
						}
						if (list.isEmpty()) continue;
						room->sendCompulsoryTriggerLog(player, objectName(), true, true);
						int id = list.at(qrand() % list.length());
						room->obtainCard(player, id, true);
					}
				}
			}
		}
		return false;
	}
};

class XingJinfanLose : public TriggerSkill
{
public:
	XingJinfanLose(const QString &xingjinfan_skill) : TriggerSkill("#" + xingjinfan_skill +"-lose"), xingjinfan_skill(xingjinfan_skill)
	{
		events << EventLoseSkill;
	}

	bool trigger(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const
	{
		if (data.toString() != xingjinfan_skill) return false;
		if (player->getPile("&xingling").isEmpty()) return  false;
		player->clearOnePrivatePile("&xingling");
		return false;
	}

private:
	QString xingjinfan_skill;
};

class XingSheque : public TriggerSkill
{
public:
	XingSheque() : TriggerSkill("xingsheque")
	{
		events << PreChangeSlash << EventPhaseProceeding;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==PreChangeSlash){
			CardUseStruct use = data.value<CardUseStruct>();
			if (player->hasFlag("XingshequeSlash")&&use.card->hasFlag("SlashIgnoreArmor")) {
				room->broadcastSkillInvoke("xingsheque");
	
				LogMessage log;
				log.type = "#InvokeSkill";
				log.from = player;
				log.arg = "xingsheque";
				room->sendLog(log);
				room->notifySkillInvoked(player, "xingsheque");
				player->setFlags("-XingshequeSlash");
			}
		}else if(player->getPhase()==Player::Start){
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (player->isDead() || player->getEquips().isEmpty()) break;
				if (p->isDead() || !p->hasSkill(objectName())) continue;
				if (!p->canSlash(player, nullptr, false)) continue;
				p->setFlags("XingshequeSlash");
				room->askForUseSlashTo(p,player,"@xingsheque:"+player->objectName(),false,false,false,nullptr,nullptr,"SlashIgnoreArmor");
				p->setFlags("-XingshequeSlash");
			}
		}
		return false;
	}
};

class SecondXingJinfan : public TriggerSkill
{
public:
	SecondXingJinfan() : TriggerSkill("secondxingjinfan")
	{
		events << EventPhaseStart << CardsMoveOneTime;
		view_as_skill = new XingJinfanVS("secondxingjinfan");
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() == Player::Discard && !player->isKongcheng())
				room->askForUseCard(player, "@@secondxingjinfan", "@xingjinfan");
			else if (player->getPhase() == Player::RoundStart && !player->getPile("&xingling").isEmpty()) {
				room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				LogMessage log;
				log.type = "$KuangbiGet";
				log.from = player;
				log.arg = "&xingling";
				log.card_str = ListI2S(player->getPile("&xingling")).join("+");
				room->sendLog(log);
				DummyCard ling(player->getPile("&xingling"));
				room->obtainCard(player, &ling, true);
			}
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from == player && move.from_pile_names.contains("&xingling") && !move.from->hasFlag("CurrentPlayer")) {
				for (int i = 0; i < move.card_ids.length(); i++) {
					if (move.from_pile_names.at(i) == "&xingling") {
						QList<int> list;
						foreach (int id, room->getDrawPile()) {
							if (Sanguosha->getCard(id)->getSuit() == Sanguosha->getCard(move.card_ids.at(i))->getSuit())
								list << id;
						}
						if (list.isEmpty()) continue;
						room->sendCompulsoryTriggerLog(player, objectName(), true, true);
						int id = list.at(qrand() % list.length());
						room->obtainCard(player, id, true);
					}
				}
			}
		}
		return false;
	}
};

class Gulivs : public ZeroCardViewAsSkill
{
public:
	Gulivs() : ZeroCardViewAsSkill("guli")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("guliUse-PlayClear")<1&&player->getHandcardNum()>0&&Slash::IsAvailable(player);
	}

	const Card *viewAs() const
	{
		Card*c = Sanguosha->cloneCard("slash");
		c->setSkillName(objectName());
		c->addSubcards(Self->getHandcards());
		return c;
	}
};

class Guli : public TriggerSkill
{
public:
	Guli() : TriggerSkill("guli")
	{
		events << PreCardUsed << TargetSpecified << CardFinished;
		view_as_skill = new Gulivs;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(!use.card->getSkillNames().contains(objectName())) return false;
		if(event==PreCardUsed){
			room->addPlayerMark(player,"guliUse-PlayClear");
		}else if(event==TargetSpecified){
			foreach (ServerPlayer *p, use.to){
				p->addQinggangTag(use.card);
			}
		}else{
			if(use.card->hasFlag("DamageDone")&&player->askForSkillInvoke(this,data)){
				room->loseHp(player,1,true,player,objectName());
				if(player->isAlive()){
					player->drawCards(player->getMaxHp()-player->getHandcardNum(),objectName());
				}
			}
		}
		return false;
	}
};

class Aoshi : public TriggerSkill
{
public:
	Aoshi() : TriggerSkill("aoshi")
	{
		events << Damage;
		frequency = Compulsory;
		waked_skills = "#aoshibf";
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.to==player||damage.to->isDead()||player->getPhase()!=Player::Play||!player->inMyAttackRange(damage.to)) return false;
		room->sendCompulsoryTriggerLog(player,this);
		room->setPlayerMark(damage.to,"&aoshi+#"+player->objectName()+"-PlayClear",1);
		return false;
	}
};

class AoshiBf : public TargetModSkill
{
public:
	AoshiBf() : TargetModSkill("#aoshibf")
	{
		pattern = ".";
	}
	int getResidueNum(const Player *from, const Card *, const Player *to) const
	{
		if (to&&to->getMark("&aoshi+#"+from->objectName()+"-PlayClear")>0)
			return 999;
		return from->getMark("qihui3Bf");
	}
};

class Shidi : public TriggerSkill
{
public:
	Shidi() : TriggerSkill("shidi")
	{
		events << CardUsed << EventPhaseStart;
		frequency = Compulsory;
		change_skill = true;
		waked_skills = "#shidi";
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")){
				if(use.card->isBlack()){
					if(player->getMark("shidiEffect")==1&&player->hasSkill(objectName(),true)){
						use.no_respond_list << "_ALL_TARGETS";
						room->sendCompulsoryTriggerLog(player,this,1);
						data.setValue(use);
					}
				}else if(use.card->isRed()){
					foreach (ServerPlayer *p, use.to){
						if(p->getMark("shidiEffect")==2&&p->hasSkill(objectName(),true)){
							room->sendCompulsoryTriggerLog(p,this,2);
							use.no_respond_list << p->objectName();
						}
					}
					data.setValue(use);
				}
			}
		}else if(event==EventPhaseStart&&player->hasSkill(objectName())){
			if(player->getPhase()==Player::Start){
				room->sendCompulsoryTriggerLog(player,objectName());
				room->setChangeSkillState(player,objectName(),2);
				room->setPlayerMark(player,"shidiEffect",1);
			}else if(player->getPhase()==Player::Finish){
				room->sendCompulsoryTriggerLog(player,objectName());
				room->setChangeSkillState(player,objectName(),1);
				room->setPlayerMark(player,"shidiEffect",2);
			}
		}
		return false;
	}
};

class ShidiDistance : public DistanceSkill
{
public:
	ShidiDistance() : DistanceSkill("#shidi")
	{
	}

	int getCorrect(const Player *from, const Player *to) const
	{
		int n = 0;
		if (to->getMark("shidiEffect")==2&&to->hasSkill("shidi",true)) {
			n += 1;
		}
		if (from->getMark("shidiEffect")==1&&from->hasSkill("shidi",true)) {
			n -= 1;
		}
		return n;
	}
};

class Yishihz : public TriggerSkill
{
public:
	Yishihz() : TriggerSkill("yishihz")
	{
		events << DamageCaused;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.to==player||!damage.to->hasEquip()||!player->askForSkillInvoke(this,damage.to)) return false;
		room->broadcastSkillInvoke(objectName());
		int id = room->askForCardChosen(player,damage.to,"e",objectName());
		if(id>-1) room->obtainCard(player,id);
		damage.damage--;
		data.setValue(damage);
		return damage.damage<1;
	}
};

class Qishe : public OneCardViewAsSkill
{
public:
	Qishe() : OneCardViewAsSkill("qishe")
	{
		filter_pattern = "EquipCard";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Analeptic::IsAvailable(player);
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return pattern.contains("analeptic")&&player->getCardCount()>0;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		Card *c = Sanguosha->cloneCard("analeptic");
		c->setSkillName(objectName());
		c->addSubcard(originalCard);
		return c;
	}
};

class QisheMax : public MaxCardsSkill
{
public:
	QisheMax() : MaxCardsSkill("#qishe")
	{
	}
	int getExtra(const Player *target) const
	{
		if (target->hasSkill("qishe"))
			return target->getEquips().length();
		return 0;
	}
	int getFixed(const Player *target) const
	{
		if (target->hasFlag("fengji"))
			return target->getMaxHp();
		if (target->hasSkill("zhenbian"))
			return target->getMaxHp();
		return -1;
	}
};

class Xiongjin : public TriggerSkill
{
public:
	Xiongjin() : TriggerSkill("xiongjin")
	{
		events << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Play){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->getMark("xiongjinUse_lun")<1&&p->hasSkill(objectName())){
						int n = qMax(1,p->getLostHp());
						n = qMin(3,n);
						if(p==player){
							if(p->askForSkillInvoke(objectName()+"$-1")){
								p->drawCards(n,objectName());
								p->addMark("xiongjinUse1-Clear");
								p->addMark("xiongjinUse_lun");
							}
						}else{
							if(p->askForSkillInvoke(objectName()+"$-1",player)){
								player->drawCards(n,objectName());
								player->addMark("xiongjinUse2-Clear");
								p->addMark("xiongjinUse_lun");
							}
						}
					}
				}
			}else if(player->getPhase()==Player::Discard){
				if(player->getMark("xiongjinUse1-Clear")>0){
					room->sendCompulsoryTriggerLog(player,objectName());
					QList<int>ids;
					foreach (const Card*c, player->getCards("he")){
						if(!c->isKindOf("BasicCard")&&player->canDiscard(player,c->getId()))
							ids << c->getId();
					}
					room->throwCard(ids,objectName(),player);
				}
				if(player->getMark("xiongjinUse2-Clear")>0){
					room->sendCompulsoryTriggerLog(player,objectName());
					QList<int>ids;
					foreach (const Card*c, player->getHandcards()){
						if(c->isKindOf("BasicCard")&&player->canDiscard(player,c->getId()))
							ids << c->getId();
					}
					room->throwCard(ids,objectName(),player);
				}
			}
		}
		return false;
	}
};

class Zhenbian : public TriggerSkill
{
public:
	Zhenbian() : TriggerSkill("zhenbian")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place!=Player::DiscardPile||(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE) return false;
		foreach (int id, move.card_ids){
			const Card*c = Sanguosha->getCard(id);
			if(player->getMark(c->getSuitString()+"_charzhenbianSuit")<1){
				player->addMark(c->getSuitString()+"_charzhenbianSuit");
				QStringList mst;
				foreach (QString m, player->getMarkNames()){
					if(m.contains("&zhenbian+:+")&&player->getMark(m)>0){
						room->setPlayerMark(player,m,0);
						m.remove("&zhenbian+:+");
						mst = m.split("+");
					}
				}
				mst << c->getSuitString()+"_char";
				if(mst.length()>3){
					foreach (QString s, mst)
						player->setMark(s+"zhenbianSuit",0);
					if(player->getMaxHp()<9){
						room->sendCompulsoryTriggerLog(player,this);
						room->gainMaxHp(player,1,objectName());
					}
				}else
					room->setPlayerMark(player,"&zhenbian+:+"+mst.join("+"),1);
			}
		}
		return false;
	}
};

class Baoxivs : public OneCardViewAsSkill
{
public:
	Baoxivs() : OneCardViewAsSkill("baoxi")
	{
		filter_pattern = ".|.|.|hand";
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.contains("@@baoxi");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		if (Sanguosha->getCurrentCardUsePattern() == "@@baoxi1"){
			Card *c = Sanguosha->cloneCard("duel");
			c->setSkillName(objectName());
			c->addSubcard(originalCard);
			return c;
		}
		Card *c = Sanguosha->cloneCard("slash");
		c->setSkillName(objectName());
		c->addSubcard(originalCard);
		return c;
	}
};

class Baoxi : public TriggerSkill
{
public:
	Baoxi() : TriggerSkill("baoxi")
	{
		events << CardsMoveOneTime << PreCardUsed;
		view_as_skill = new Baoxivs;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.card->getSkillNames().contains(objectName())){
				if(use.card->isKindOf("Slash"))
					player->addMark("baoxinobasic_lun");
				else
					player->addMark("baoxiisbasic_lun");
				room->loseMaxHp(player,1,objectName());
			}
			return false;
		}
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to_place!=Player::DiscardPile||player->isKongcheng()) return false;
		int isbasic = 0,nobasic = 0;
		foreach (int id, move.card_ids){
			if(Sanguosha->getCard(id)->isKindOf("BasicCard")){
				isbasic++;
			}else
				nobasic++;
		}
		if(isbasic>1&&player->getMark("baoxiisbasic_lun")<1){
			room->askForUseCard(player,"@@baoxi1","baoxi1");
		}
		if(nobasic>1&&player->getMark("baoxinobasic_lun")<1){
			room->askForUseCard(player,"@@baoxi2","baoxi2",-1,Card::MethodUse,false);
		}
		return false;
	}
};

class Cangjia : public TriggerSkill
{
public:
	Cangjia() : TriggerSkill("cangjia")
	{
		events << CardsMoveOneTime << TargetConfirming;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==TargetConfirming){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.from&&use.from!=player){
				foreach (QString m, player->getMarkNames()){
					if(m.contains("&cangjia+")){
						if(m.contains(use.card->getSuitString())) continue;
						room->sendCompulsoryTriggerLog(player,this);
						if(room->askForCard(use.from,"..","cangjia0:"+player->objectName(),data)) break;
						use.nullified_list << player->objectName();
						data.setValue(use);
						break;
					}
				}
			}
		}else{
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to==player&&move.to_place==Player::PlaceHand&&move.reason.m_skillName!="InitialHandCards"
			&&move.to!=move.from&&player->getPhase()!=Player::Play){
				QStringList suits;
				suits << "&cangjia";
				foreach (QString m, player->getMarkNames()){
					if(m.contains("&cangjia+")){
						suits = m.split("+");
						room->setPlayerMark(player,m,0);
					}
				}
				bool has = true;
				foreach (int id, move.card_ids){
					QString s = Sanguosha->getCard(id)->getSuitString()+"_char";
					if(suits.contains(s)) continue;
					suits.append(s);
					if(has){
						has = false;
						room->sendCompulsoryTriggerLog(player,objectName());
					}
				}
				room->setPlayerMark(player,suits.join("+"),1);
			}
		}
		return false;
	}
};

class CangjiaLimit : public CardLimitSkill
{
public:
	CangjiaLimit() : CardLimitSkill("#CangjiaLimit")
	{
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player*target,const Card*card) const
	{
		if (target->getPhase()==Player::Play&&target->hasSkill("cangjia")){
			foreach (QString m, target->getMarkNames()){
				if(m.contains("&cangjia+")){
					if(m.contains(card->getSuitString()))
						return "";
				}
			}
			return card->toString();
		}
		return "";
	}
};

class Duohui : public TriggerSkill
{
public:
	Duohui() : TriggerSkill("duohui")
	{
		events << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Start){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(player->getCardCount()>0&&p->hasSkill(objectName())){
						const Card*c = room->askForCard(player,"..","duohui0:"+p->objectName(),QVariant::fromValue(p),Card::MethodNone);
						if(c){
							player->skillInvoked(objectName(),-1,p);
							room->giveCard(player,p,c,objectName());
							if(player->isDead()) break;
							if(p->isDead()) continue;
							c = room->askForCard(p,"^"+c->toString()+"|"+c->getSuitString(),"duohui1:"+player->objectName(),QVariant::fromValue(player),Card::MethodNone);
							if(c) room->giveCard(p,player,c,objectName());
							else player->drawCards(1,objectName());
						}
					}
				}
			}
		}
		return false;
	}
};

YueyuanCard::YueyuanCard()
{
	target_fixed = true;
}

void YueyuanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QString sm,cm;
	foreach (QString m, source->getMarkNames()){
		if(m.contains("&cangjia+")){
			sm = m;
			cm = m;
			cm.remove("&cangjia+");
			cm.remove("_char");
		}
	}
	QStringList suits = sm.split("+");
	source->drawCards(suits.length()-1,"yueyuan");
	QString suit = room->askForChoice(source, "yueyuan", cm);
	suits.removeOne(suit+"_char");
	room->setPlayerMark(source,sm,0);
	if(suits.length()>1)
		room->setPlayerMark(source,suits.join("+"),1);
}

class Yueyuan : public ZeroCardViewAsSkill
{
public:
	Yueyuan() : ZeroCardViewAsSkill("yueyuan")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		foreach (QString m, player->getMarkNames()){
			if(m.contains("&cangjia+"))
				return player->usedTimes("YueyuanCard")<1;
		}
		return false;
	}

	const Card *viewAs() const
	{
		return new YueyuanCard;
	}
};







mobileStarPackage::mobileStarPackage()
	: Package("mobile_star")
{
	General *xing_zhangliao = new General(this, "xing_zhangliao", "qun", 4);
	xing_zhangliao->addSkill(new XingWeifeng);

	General *xing_zhanghe = new General(this, "xing_zhanghe", "qun", 4);
	xing_zhanghe->addSkill(new XingZhilve);
	xing_zhanghe->addSkill(new SlashNoDistanceLimitSkill("xingzhilve"));
	related_skills.insertMulti("xingzhilve", "#xingzhilve-slash-ndl");
	addMetaObject<XingZhilveCard>();
	addMetaObject<XingZhilveSlashCard>();

	General *xing_xuhuang = new General(this, "xing_xuhuang", "qun", 4);
	xing_xuhuang->addSkill(new XingZhiyan);
	xing_xuhuang->addSkill(new XingZhiyanPro);
	related_skills.insertMulti("xingzhiyan", "#xingzhiyan-pro");
	addMetaObject<XingZhiyanCard>();

	General *xing_ganning = new General(this, "xing_ganning", "qun", 4);
	xing_ganning->addSkill(new XingJinfan);
	xing_ganning->addSkill(new XingJinfanLose("xingjinfan"));
	xing_ganning->addSkill(new XingSheque);
	related_skills.insertMulti("xingjinfan", "#xingjinfan-lose");
	addMetaObject<XingJinfanCard>();

	General *secondxing_ganning = new General(this, "secondxing_ganning", "qun", 4);
	secondxing_ganning->addSkill(new SecondXingJinfan);
	secondxing_ganning->addSkill(new XingJinfanLose("secondxingjinfan"));
	secondxing_ganning->addSkill("xingsheque");
	related_skills.insertMulti("secondxingjinfan", "#secondxingjinfan-lose");

	General *xing_weiyan = new General(this, "xing_weiyan", "qun", 4);
	xing_weiyan->addSkill(new Guli);
	xing_weiyan->addSkill(new Aoshi);
	xing_weiyan->addSkill(new AoshiBf);

	General *xing_huangzhong = new General(this, "xing_huangzhong", "qun", 4);
	xing_huangzhong->addSkill(new Shidi);
	xing_huangzhong->addSkill(new ShidiDistance);
	xing_huangzhong->addSkill(new Yishihz);
	xing_huangzhong->addSkill(new Qishe);
	xing_huangzhong->addSkill(new QisheMax);

	General *xing_dongzuo = new General(this, "xing_dongzuo", "qun", 4,true,false,false,3);
	xing_dongzuo->addSkill(new Xiongjin);
	xing_dongzuo->addSkill(new Zhenbian);
	xing_dongzuo->addSkill(new Baoxi);

	General *xing_fazheng = new General(this, "xing_fazheng", "qun", 4);
	xing_fazheng->addSkill(new Cangjia);
	xing_fazheng->addSkill(new CangjiaLimit);
	xing_fazheng->addSkill(new Duohui);
	xing_fazheng->addSkill(new Yueyuan);
	addMetaObject<YueyuanCard>();





}
ADD_PACKAGE(mobileStar)

class Gongao : public TriggerSkill
{
public:
	Gongao() : TriggerSkill("gongao")
	{
		events << Death;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		room->broadcastSkillInvoke(objectName());
		room->sendCompulsoryTriggerLog(player, objectName());
		room->gainMaxHp(player, 1, objectName());
		room->recover(player, RecoverStruct("gongao", player));
		return false;
	}
};

class Juyi : public PhaseChangeSkill
{
public:
	Juyi() : PhaseChangeSkill("juyi")
	{
		frequency = Wake;
		waked_skills = "benghuai,weizhong";
	}

	bool triggerable(const ServerPlayer *player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer *zhugedan, Room *room) const
	{
		if (zhugedan->isWounded() && zhugedan->getMaxHp() > zhugedan->aliveCount()) {
			LogMessage log;
			log.type = "#JuyiWake";
			log.from = zhugedan;
			log.arg = QString::number(zhugedan->getMaxHp());
			log.arg2 = QString::number(zhugedan->aliveCount());
			log.arg3 = objectName();
			room->sendLog(log);
		}else if(!zhugedan->canWake(objectName()))
			return false;
		zhugedan->peiyin(objectName());
		room->notifySkillInvoked(zhugedan, objectName());
		room->doSuperLightbox(zhugedan, "juyi");

		room->setPlayerMark(zhugedan, "juyi", 1);
		if (room->changeMaxHpForAwakenSkill(zhugedan, 0, objectName())) {
			int diff = zhugedan->getHandcardNum() - zhugedan->getMaxHp();
			if (diff < 0)
				room->drawCards(zhugedan, -diff, objectName());
			if (zhugedan->getMark("juyi") == 1)
				room->handleAcquireDetachSkills(zhugedan, "benghuai|weizhong");
		}

		return false;
	}
};

class Weizhong : public TriggerSkill
{
public:
	Weizhong() : TriggerSkill("weizhong")
	{
		events << MaxHpChanged;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		room->broadcastSkillInvoke(objectName());
		room->sendCompulsoryTriggerLog(player, objectName());

		player->drawCards(1, objectName());
		return false;
	}
};

ZhoufuCard::ZhoufuCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool ZhoufuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && to_select->getPile("incantation").isEmpty();
}

void ZhoufuCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ServerPlayer *target = targets.first();
	target->setTag("ZhoufuSource" + QString::number(getEffectiveId()), QVariant::fromValue(source));
	target->addToPile("incantation", this);
}

class ZhoufuViewAsSkill : public OneCardViewAsSkill
{
public:
	ZhoufuViewAsSkill() : OneCardViewAsSkill("zhoufu")
	{
		filter_pattern = ".|.|.|hand";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("ZhoufuCard");
	}

	const Card *viewAs(const Card *originalcard) const
	{
		Card *card = new ZhoufuCard;
		card->addSubcard(originalcard);
		return card;
	}
};

class Zhoufu : public TriggerSkill
{
public:
	Zhoufu() : TriggerSkill("zhoufu")
	{
		events << StartJudge << EventPhaseChanging;
		view_as_skill = new ZhoufuViewAsSkill;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->getPile("incantation").length() > 0;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == StartJudge) {
			int card_id = player->getPile("incantation").first();

			JudgeStruct *judge = data.value<JudgeStruct *>();
			judge->card = Sanguosha->getCard(card_id);

			LogMessage log;
			log.type = "$ZhoufuJudge";
			log.from = player;
			log.arg = objectName();
			log.card_str = QString::number(card_id);
			room->sendLog(log);

			room->moveCardTo(judge->card, nullptr, judge->who, Player::PlaceJudge,
				CardMoveReason(CardMoveReason::S_REASON_JUDGE, judge->who->objectName(), "zhoufu", judge->reason), true);
			judge->updateResult();
			data.setValue(judge);
			room->setTag("SkipGameRule", (int)triggerEvent);
		} else {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive) {
				int id = player->getPile("incantation").first();
				ServerPlayer *zhangbao = player->getTag("ZhoufuSource" + QString::number(id)).value<ServerPlayer *>();
				if (zhangbao && zhangbao->isAlive())
					zhangbao->obtainCard(Sanguosha->getCard(id));
			}
		}
		return false;
	}
};

class Yingbing : public TriggerSkill
{
public:
	Yingbing() : TriggerSkill("yingbing")
	{
		events << StartJudge;
		frequency = Frequent;
	}

	int getPriority(TriggerEvent) const
	{
		return -2;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		JudgeStruct *judge = data.value<JudgeStruct *>();
		int id = judge->card->getEffectiveId();
		ServerPlayer *zhangbao = player->getTag("ZhoufuSource" + QString::number(id)).value<ServerPlayer *>();
		if (zhangbao&&TriggerSkill::triggerable(zhangbao)&&zhangbao->askForSkillInvoke(this,data)) {
			room->broadcastSkillInvoke(objectName());
			zhangbao->drawCards(2, objectName());
		}
		return false;
	}
};

XuejiCard::XuejiCard()
{
}

bool XuejiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return Self->inMyAttackRange(to_select, subcards) && targets.length() < Self->getLostHp() && to_select != Self;
}

void XuejiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	DamageStruct damage;
	damage.from = source;
	damage.reason = "xueji";

	foreach (ServerPlayer *p, targets) {
		damage.to = p;
		room->damage(damage);
	}
	foreach (ServerPlayer *p, targets) {
		if (p->isAlive())
			p->drawCards(1, "xueji");
	}
}

class Xueji : public OneCardViewAsSkill
{
public:
	Xueji() : OneCardViewAsSkill("xueji")
	{
		filter_pattern = ".|red!";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getLostHp() > 0 && player->canDiscard(player, "he") && !player->hasUsed("XuejiCard");
	}

	const Card *viewAs(const Card *originalcard) const
	{
		XuejiCard *first = new XuejiCard;
		first->addSubcard(originalcard->getId());
		first->setSkillName(objectName());
		return first;
	}
};

class Huxiao : public TargetModSkill
{
public:
	Huxiao() : TargetModSkill("huxiao")
	{
	}

	int getResidueNum(const Player *from, const Card *, const Player *) const
	{
		if (from->hasSkill(objectName()))
			return from->getMark("huxiao-PlayClear");
		return 0;
	}
};

class HuxiaoCount : public TriggerSkill
{
public:
	HuxiaoCount() : TriggerSkill("#huxiao-count")
	{
		events << CardOffset;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardOffset) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if (player->getPhase() == Player::Play&&effect.card->isKindOf("Slash"))
				room->addPlayerMark(player, "huxiao-PlayClear");
		}
		return false;
	}
};

class Wuji : public PhaseChangeSkill
{
public:
	Wuji() : PhaseChangeSkill("wuji")
	{
		frequency = Wake;
	}

	bool triggerable(const ServerPlayer *player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Finish
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getMark("damage_point_round") >= 3) {
			LogMessage log;
			log.type = "#WujiWake";
			log.from = player;
			log.arg = QString::number(player->getMark("damage_point_round"));
			log.arg2 = objectName();
			room->sendLog(log);
		}else if(!player->canWake(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());
		room->doSuperLightbox(player, "wuji");

		room->setPlayerMark(player, "wuji", 1);
		if (room->changeMaxHpForAwakenSkill(player, 1, objectName())) {
			room->recover(player, RecoverStruct("wuji", player));
			if (player->getMark("wuji") == 1)
				room->detachSkillFromPlayer(player, "huxiao");
		}

		return false;
	}
};

class Xingwu : public TriggerSkill
{
public:
	Xingwu() : TriggerSkill("xingwu")
	{
		events << EventPhaseStart << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart) {
			if (player->getPhase() == Player::Discard) {
				int n = player->getMark(objectName());
				bool red_avail = ((n & 2) == 0), black_avail = ((n & 1) == 0);
				if (player->isKongcheng() || (!red_avail && !black_avail))
					return false;
				QString pattern = ".|.|.|hand";
				if (red_avail != black_avail)
					pattern = QString(".|%1|.|hand").arg(red_avail ? "red" : "black");
				const Card *card = room->askForCard(player, pattern, "@xingwu", QVariant(), Card::MethodNone);
				if (card) {
					room->broadcastSkillInvoke(objectName(), 1);

					LogMessage log;
					log.type = "#InvokeSkill";
					log.from = player;
					log.arg = objectName();
					room->sendLog(log);
					room->notifySkillInvoked(player, objectName());

					player->addToPile(objectName(), card);
				}
			} else if (player->getPhase() == Player::RoundStart) {
				player->setMark(objectName(), 0);
			}
		} else if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to == player && move.to_place == Player::PlaceSpecial && player->getPile(objectName()).length() >= 3) {
				player->clearOnePrivatePile(objectName());
				QList<ServerPlayer *> males;
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if (p->isMale())
						males << p;
				}
				if (males.isEmpty()) return false;

				ServerPlayer *target = room->askForPlayerChosen(player, males, objectName(), "@xingwu-choose");
				room->broadcastSkillInvoke(objectName(), 2);
				room->damage(DamageStruct(objectName(), player, target, 2));

				if (!player->isAlive()) return false;
				QList<const Card *> equips = target->getEquips();
				if (!equips.isEmpty()) {
					DummyCard *dummy = new DummyCard;
					foreach (const Card *equip, equips) {
						if (player->canDiscard(target, equip->getEffectiveId()))
							dummy->addSubcard(equip);
					}
					if (dummy->subcardsLength() > 0)
						room->throwCard(dummy, target, player);
					delete dummy;
				}
			}
		}
		return false;
	}
};

class Luoyan : public TriggerSkill
{
public:
	Luoyan(const QString &luoyan) : TriggerSkill(luoyan), luoyan(luoyan)
	{
		events << CardsMoveOneTime << EventAcquireSkill << EventLoseSkill;
		frequency = Compulsory;
		waked_skills = "timobileanxiang,liuli";
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventLoseSkill && data.toString() == objectName()) {
			player->setMark(luoyan + "_help", 0);
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			QString skills = "-timobileanxiang|-liuli";
			if (luoyan == "olluoyan")
				skills = "-oltimobileanxiang|-liuli";

			room->handleAcquireDetachSkills(player, skills, true);
		} else if (triggerEvent == EventAcquireSkill && data.toString() == objectName()) {
			if (!player->getPile("xingwu").isEmpty()) {
				player->setMark(luoyan + "_help", 1);
				room->sendCompulsoryTriggerLog(player, objectName(), true, true);

				QString skills = "timobileanxiang|liuli";
				if (luoyan == "olluoyan")
					skills = "oltimobileanxiang|liuli";

				room->handleAcquireDetachSkills(player, skills);
			}
		} else if (triggerEvent == CardsMoveOneTime && player->isAlive() && player->hasSkill(objectName(), true)) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to == player && move.to_place == Player::PlaceSpecial && move.to_pile_name == "xingwu") {
				if (!player->getPile("xingwu").isEmpty() && player->getMark(luoyan + "_help") <= 0) {
					player->setMark(luoyan + "_help", 1);
					room->sendCompulsoryTriggerLog(player, objectName(), true, true);

					QString skills = "timobileanxiang|liuli";
					if (luoyan == "olluoyan")
						skills = "oltimobileanxiang|liuli";

					room->handleAcquireDetachSkills(player, skills);
				}
			} else if (move.from == player && move.from_places.contains(Player::PlaceSpecial)
				&& move.from_pile_names.contains("xingwu")) {
				if (player->getPile("xingwu").isEmpty() && player->getMark(luoyan + "_help") > 0) {
					player->setMark(luoyan + "_help", 0);
					room->sendCompulsoryTriggerLog(player, objectName(), true, true);

					QString skills = "-timobileanxiang|-liuli";
					if (luoyan == "olluoyan")
						skills = "-oltimobileanxiang|-liuli";

					room->handleAcquireDetachSkills(player, skills, true);
				}
			}
		}
		return false;
	}

private:
	QString luoyan;
};

class MobileQizhou : public TriggerSkill
{
public:
	MobileQizhou() : TriggerSkill("mobileqizhou")
	{
		events << CardsMoveOneTime << EventAcquireSkill;
		frequency = Compulsory;
		waked_skills = "nosyingzi,qixi,xuanfeng";
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		bool flag = false;
		if (event == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from == player && move.from_places.contains(Player::PlaceEquip))
				flag = true;
			if (move.to == player && move.to_place == Player::PlaceEquip)
				flag = true;
		} else {
			if (data.toString() == objectName())
				flag = true;
		}

		if (flag) {
			int n = QizhouNum(player);
			QStringList get_or_lose, skills = player->property("mobileqizhou_skills").toStringList();
			if (QizhouNum(player) >= 1 && !player->hasSkill("nosyingzi", true) && !skills.contains("nosyingzi")) {
				skills << "nosyingzi";
				room->setPlayerProperty(player, "mobileqizhou_skills", skills);
				get_or_lose << "nosyingzi";
			}
			if (n < 1 && player->hasSkill("nosyingzi", true) && skills.contains("nosyingzi")) {
				skills.removeOne("nosyingzi");
				room->setPlayerProperty(player, "mobileqizhou_skills", skills);
				get_or_lose << "-nosyingzi";
			}
			if (n >= 2 && !player->hasSkill("qixi", true) && !skills.contains("qixi")) {
				skills << "qixi";
				room->setPlayerProperty(player, "mobileqizhou_skills", skills);
				get_or_lose << "qixi";
			}
			if (n < 2 && player->hasSkill("qixi", true) && skills.contains("qixi")) {
				skills.removeOne("qixi");
				room->setPlayerProperty(player, "mobileqizhou_skills", skills);
				get_or_lose << "-qixi";
			}
			if (n >= 3 && !player->hasSkill("xuanfeng", true) && !skills.contains("xuanfeng")) {
				skills << "xuanfeng";
				room->setPlayerProperty(player, "mobileqizhou_skills", skills);
				get_or_lose << "xuanfeng";
			}
			if (n < 3 && player->hasSkill("xuanfeng", true) && skills.contains("xuanfeng")) {
				skills.removeOne("xuanfeng");
				room->setPlayerProperty(player, "mobileqizhou_skills", skills);
				get_or_lose << "-xuanfeng";
			}
			if (!get_or_lose.isEmpty()) {
				room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				room->handleAcquireDetachSkills(player, get_or_lose);
			}
		}
		return false;
	}
	static int QizhouNum(ServerPlayer *player)
	{
		QStringList suits;
		foreach (const Card *c, player->getEquips()) {
			if (!suits.contains(c->getSuitString()))
				suits << c->getSuitString();
		}
		return suits.length();
	}
};

class MobileQizhouLose : public TriggerSkill
{
public:
	MobileQizhouLose() : TriggerSkill("#mobileqizhou-lose")
	{
		events << EventLoseSkill;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (data.toString() != "mobileqizhou") return false;
		QStringList new_list, skills = player->property("mobileqizhou_skills").toStringList();
		room->setPlayerProperty(player, "mobileqizhou_skills", QStringList());
		foreach (QString str, skills) {
			if (player->hasSkill(str, true))
				new_list << "-" + str;
		}
		if (new_list.isEmpty()) return false;
		room->handleAcquireDetachSkills(player, new_list);
		return false;
	}
};

MobileShanxiCard::MobileShanxiCard()
{
	handling_method = Card::MethodDiscard;
}

void MobileShanxiCard::onEffect(CardEffectStruct &effect) const
{
	int n = qMin(effect.to->getCards("he").length(), effect.from->getHp());
	if (n > 0) {
		Room *room = effect.from->getRoom();

		QList<int> cards;

		for (int i = 0; i < n; ++i) {
			if(effect.to->getCardCount()<=i) break;
			int id = room->askForCardChosen(effect.from, effect.to, "he", "mobileshanxi", false, Card::MethodNone, cards, i>0);
			if(id<0) break;
			cards << id;
		}

		DummyCard dummy(cards);
		effect.to->addToPile("mobileshanxi", &dummy, false);

		// for record
		if (!effect.to->getTag("mobileshanxi").isValid() || !effect.to->getTag("mobileshanxi").canConvert(QVariant::Map))
			effect.to->setTag("mobileshanxi", QVariantMap());

		QVariantMap vm = effect.to->getTag("mobileshanxi").toMap();
		foreach (int id, cards)
			vm[QString::number(id)] = effect.from->objectName();

		effect.to->setTag("mobileshanxi", vm);
	}
}

class MobileShanxiVS :public OneCardViewAsSkill
{
public:
	MobileShanxiVS() :OneCardViewAsSkill("mobileshanxi")
	{
		filter_pattern = "BasicCard|red!";
		response_pattern = "@@mobileshanxi";
	}

	const Card *viewAs(const Card *originalCard) const
	{
		MobileShanxiCard *card = new MobileShanxiCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class MobileShanxi : public PhaseChangeSkill
{
public:
	MobileShanxi() : PhaseChangeSkill("mobileshanxi")
	{
		view_as_skill = new MobileShanxiVS;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Play || !player->canDiscard(player, "h")) return false;
		room->askForUseCard(player, "@@mobileshanxi", "@mobileshanxi", -1, Card::MethodDiscard);
		return false;
	}
};

class MobileShanxiGet : public TriggerSkill
{
public:
	MobileShanxiGet() : TriggerSkill("#mobileshanxi-get")
	{
		events << EventPhaseChanging << Death;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive)
				return false;
		} else if (triggerEvent == Death) {
			DeathStruct death = data.value<DeathStruct>();
			if (death.who != player)
				return false;
		}

		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->getTag("mobileshanxi").isValid()) {
				QVariantMap vm = p->getTag("mobileshanxi", QVariantMap()).toMap();
				if (vm.values().contains(player->objectName())) {
					QList<int> to_obtain;
					foreach (const QString &key, vm.keys()) {
						if (vm.value(key) == player->objectName())
							to_obtain << key.toInt();
					}

					DummyCard dummy(to_obtain);
					room->obtainCard(p, &dummy, false);

					foreach (int id, to_obtain)
						vm.remove(QString::number(id));

					p->setTag("mobileshanxi", vm);
				}
			}
		}
		return false;
	}
};

LuanzhanCard::LuanzhanCard()
{
	mute = true;
}

bool LuanzhanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	int n = Self->getMark("luanzhan_target_num-Clear");
	return targets.length() < n && to_select->hasFlag("luanzhan_canchoose");
}

void LuanzhanCard::onUse(Room *room, CardUseStruct &card_use) const
{
	foreach (ServerPlayer *p, card_use.to)
		room->setPlayerFlag(p, "luanzhan_extratarget");
}

class LuanzhanVS : public ZeroCardViewAsSkill
{
public:
	LuanzhanVS() : ZeroCardViewAsSkill("luanzhan")
	{
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.contains("@@luanzhan");
	}

	const Card *viewAs() const
	{
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if (pattern=="@@luanzhan1")
			return new ExtraCollateralCard;
		return new LuanzhanCard;
	}
};

class Luanzhan : public TriggerSkill
{
public:
	Luanzhan() : TriggerSkill("luanzhan")
	{
		events << TargetSpecified << CardUsed;
		view_as_skill = new LuanzhanVS;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (event == CardUsed) {
			int n = player->getMark("&luanzhanMark") + player->getMark("luanzhanMark");
			if (use.to.length()>=n) return false;
			if (!(use.card->isBlack() && use.card->isNDTrick())) return false;
			if(use.card->targetFixed()){
				bool canextra = false;
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if (use.to.contains(p) || player->isProhibited(p, use.card)) continue;
					room->setPlayerFlag(p, "luanzhan_canchoose");
					canextra = true;
				}
				if (!canextra) return false;
				player->setTag("luanzhanData", data);
				room->setPlayerMark(player, "luanzhan_target_num-Clear", n);
				if (!room->askForUseCard(player, "@@luanzhan", QString("@luanzhan:%1::%2").arg(use.card->objectName()).arg(n)))
					return false;
				LogMessage log;
				foreach(ServerPlayer *p, room->getAlivePlayers()) {
					room->setPlayerFlag(p, "-luanzhan_canchoose");
					if (p->hasFlag("luanzhan_extratarget")) {
						room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), p->objectName());
						room->setPlayerFlag(p,"-luanzhan_extratarget");
						use.to.append(p);
						log.to << p;
					}
				}
				if (log.to.isEmpty()) return false;
				log.type = "#QiaoshuiAdd";
				log.from = player;
				log.card_str = use.card->toString();
				log.arg = "luanzhan";
				room->sendLog(log);
				room->sortByActionOrder(use.to);
				data = QVariant::fromValue(use);
			}
		} else {
			if (!use.card->isKindOf("Slash") && !(use.card->isBlack() && use.card->isNDTrick())) return false;
			int n = player->getMark("&luanzhanMark") + player->getMark("luanzhanMark");
			if (use.to.length() < n) {
				room->setPlayerMark(player, "&luanzhanMark", 0);
				room->setPlayerMark(player, "luanzhanMark", 0);
			}
		}
		return false;
	}
};

class LuanzhanTargetMod : public TargetModSkill
{
public:
	LuanzhanTargetMod() : TargetModSkill("#luanzhan-target")
	{
		frequency = NotFrequent;
		pattern = ".";
	}

	int getExtraTargetNum(const Player *from, const Card *card) const
	{
		if ((card->isKindOf("Slash") || (card->isBlack() && card->isNDTrick()))&&from->hasSkill("luanzhan"))
			return from->getMark("&luanzhanMark") + from->getMark("luanzhanMark");
		return 0;
	}
};

class Shenxian : public TriggerSkill
{
public:
	Shenxian() : TriggerSkill("shenxian")
	{
		events << CardsMoveOneTime;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!player->hasFlag("CurrentPlayer")&&(move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip))
			&&move.from!=player&&move.from->isAlive()&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD) {
			foreach (int id, move.card_ids) {
				if (Sanguosha->getCard(id)->getTypeId() == Card::TypeBasic) {
					if (room->askForSkillInvoke(player, objectName(), data)) {
						room->broadcastSkillInvoke(objectName());
						player->drawCards(1, "shenxian");
					}
					break;
				}
			}
		}
		return false;
	}
};

QiangwuCard::QiangwuCard()
{
	target_fixed = true;
}

void QiangwuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	JudgeStruct judge;
	judge.pattern = ".";
	judge.who = source;
	judge.reason = "qiangwu";
	judge.play_animation = false;
	room->judge(judge);

	room->setPlayerMark(source, "qiangwu-Clear", judge.card->getNumber());
}

class QiangwuViewAsSkill : public ZeroCardViewAsSkill
{
public:
	QiangwuViewAsSkill() : ZeroCardViewAsSkill("qiangwu")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("QiangwuCard");
	}

	const Card *viewAs() const
	{
		return new QiangwuCard;
	}
};

class Qiangwu : public TriggerSkill
{
public:
	Qiangwu() : TriggerSkill("qiangwu")
	{
		events << PreCardUsed;
		view_as_skill = new QiangwuViewAsSkill;
	}
	bool trigger(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("Slash") && player->getMark("qiangwu-Clear") > 0
			&& use.card->getNumber() > player->getMark("qiangwu-Clear")) {
			use.m_addHistory = false;
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class QiangwuTargetMod : public TargetModSkill
{
public:
	QiangwuTargetMod() : TargetModSkill("#qiangwu-target")
	{
	}

	int getDistanceLimit(const Player *from, const Card *card, const Player *) const
	{
		if (card->getNumber()<from->getMark("qiangwu-Clear"))
			return 999;
		if(card->getSkillName().contains("xuanjian")&&XSGongli::GlTrigger(from,"you_pangtong"))
			return 999;
		if (from->hasFlag("mobileanxianBf"+card->toString()))
			return 999;
		return 0;
	}

	int getResidueNum(const Player *from, const Card *card, const Player *) const
	{
		if (from->getMark("qiangwu-Clear")>0&&(card->getNumber()>from->getMark("qiangwu-Clear")||card->hasFlag("Global_SlashAvailabilityChecker")))
			return 999;
		if (from->getPile("yangming").contains(card->getEffectiveId()))
			return 999;
		return from->getMark("guidian2");
	}
};

class NewFengpo : public TriggerSkill
{
public:
	NewFengpo() : TriggerSkill("newfengpo")
	{
		events << TargetSpecified;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.to.length()==1 && use.card->hasFlag("newfengpoUse")) {
			if (!player->askForSkillInvoke(this, data)) return false;
			room->broadcastSkillInvoke(objectName());

			int n = 0;
			foreach (const Card *card, use.to.first()->getHandcards()) {
				if (card->getSuit() == Card::Diamond)
					++n;
			}

			QString choice = room->askForChoice(player, objectName(), "drawCards+addDamage", data);
			if (choice == "drawCards") {
				if (n > 0)
					player->drawCards(n, objectName());
			} else
				room->setCardFlag(use.card,"newfengpoaddDamage_" + QString::number(n));
		}
		return false;
	}
};

class NewFengpoEffect : public TriggerSkill
{
public:
	NewFengpoEffect() : TriggerSkill("#newfengpo-effect")
	{
		events << DamageCaused << PreCardUsed;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == DamageCaused) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card){
				foreach (QString f, damage.card->getFlags()) {
					if(f.contains("newfengpoaddDamage_")){
						QStringList fs = f.split("_");
						int n = fs.last().toInt();
						if(n<1) continue;
						player->damageRevises(data,n);
					}
				}
			}
		}else if (event == PreCardUsed) {
			if (!player->hasFlag("CurrentPlayer")) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Slash") || use.card->isKindOf("Duel")){
				player->addMark("newfengpo-Clear");
				if(player->getMark("newfengpo-Clear")==1)
					room->setCardFlag(use.card,"newfengpoUse");
			}
		}
		return false;
	}
};

FumanCard::FumanCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool FumanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && to_select->getMark("fuman_target-PlayClear") <= 0;
}

void FumanCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	room->addPlayerMark(effect.to, "fuman_target-PlayClear");
	CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "fuman", "");
	room->obtainCard(effect.to, this, reason, true);
	room->addPlayerMark(effect.to, "fuman_" + QString::number(getSubcards().first()) + effect.from->objectName());
}

class FumanVS : public OneCardViewAsSkill
{
public:
	FumanVS() : OneCardViewAsSkill("fuman")
	{
		filter_pattern = "Slash";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		foreach (const Player *p, player->getAliveSiblings()) {
			if (p->getMark("fuman_target-PlayClear") <= 0)
				return true;
		}
		return false;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		FumanCard *c = new FumanCard();
		c->addSubcard(originalCard);
		return c;
	}
};

class Fuman : public TriggerSkill
{
public:
	Fuman() : TriggerSkill("fuman")
	{
		events << CardUsed << EventPhaseChanging;
		view_as_skill = new FumanVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;
			foreach (QString mark, player->getMarkNames()) {
				if (mark.startsWith("fuman_") && player->getMark(mark) > 0)
					room->setPlayerMark(player, mark ,0);
			}
		} else {
			const Card *card = data.value<CardUseStruct>().card;;
			
			if (card->isKindOf("SkillCard")) return false;

			QList<int> ids;
			if (card->isVirtualCard())
				ids = card->getSubcards();
			else
				ids << card->getEffectiveId();
			if (ids.isEmpty()) return false;

			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (!p->hasSkill(objectName()) || p->isDead()) continue;
				foreach (int id, ids) {
					if (player->getMark("fuman_" + QString::number(id) + p->objectName()) > 0) {
						room->setPlayerMark(player, "fuman_" + QString::number(id) + p->objectName(), 0);
						room->sendCompulsoryTriggerLog(p, objectName(), true, true);
						p->drawCards(1, objectName());
					}
				}
			}
		}
		return false;
	}
};

class MobileFuhan : public PhaseChangeSkill
{
public:
	MobileFuhan() : PhaseChangeSkill("mobilefuhan")
	{
		frequency = Limited;
		limit_mark = "@mobilefuhanMark";
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::RoundStart) return false;
		if (player->getMark("@mobilefuhanMark") <= 0) return false;

		int nn = player->getMark("meiying") + player->getMark("&meiying");
		if (nn <= 0) return false;
		nn = qMin(room->getPlayers().length(), nn);
		QString num = QString::number(nn);
		if (!player->askForSkillInvoke("mobilefuhan", QString("mobilefuhan_invoke:%1").arg(num))) return false;
		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(player, "mobilefuhan");
		room->removePlayerMark(player, "@mobilefuhanMark");
		player->loseAllMarks("&meiying");

		QStringList five_shus, shus = Sanguosha->getLimitedGeneralNames("shu");
		foreach (QString name, shus) {
			if (hasshu(name, room))
				shus.removeOne(name);
		}
		for (int i = 1; i < 6; i++) {
			if (shus.isEmpty()) break;
			QString name = shus.at((qrand() % shus.length()));
			five_shus << name;
			shus.removeOne(name);
		}
		if (five_shus.isEmpty()) return false;
		QString shu_general = room->askForGeneral(player, five_shus);
		room->changeHero(player, shu_general, false, false, (player->getGeneralName() != "mobile_zhaoxiang" && player->getGeneral2Name() == "mobile_zhaoxiang"));
		int n = player->getMark("meiying");
		n = qMin(room->getPlayers().length(), n);
		room->setPlayerProperty(player, "maxhp", n);
		if (!player->isLowestHpPlayer()) return false;
		room->recover(player, RecoverStruct("mobilefuhan", player));
		return false;
	}

	bool hasshu(const QString name, Room *room) const
	{
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getGeneralName() == name || p->getGeneral2Name() == name)
				return true;
		}
		return false;
	}
};

MobileFuhaiCard::MobileFuhaiCard()
{
	target_fixed = true;
}

void MobileFuhaiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QStringList choices;
	foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
		if (p->isDead()) continue;
		QString choice = room->askForChoice(p, "mobilefuhai", "up+down");
		if (p->isAlive())
			choices << choice;
	}
	if (choices.isEmpty()) return;

	int i = 0;
	foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
		if  (i > choices.length()) break;
		if (p->isDead()) continue;
		LogMessage log;
		log.type = "#ShouxiChoice";
		log.from = p;
		log.arg = "mobilefuhai:" + choices.at(i);
		room->sendLog(log);
		i++;
	}

	int draw = 1;
	while (draw < choices.length()) {
		if (choices.at(draw - 1) != choices.at(draw)) break;
		draw++;
	}
	if (draw <= 1) return;
	source->drawCards(draw, "mobilefuhai");
}

class MobileFuhai : public ZeroCardViewAsSkill
{
public:
	MobileFuhai() : ZeroCardViewAsSkill("mobilefuhai")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("MobileFuhaiCard");
	}

	const Card *viewAs() const
	{
		MobileFuhaiCard *c = new MobileFuhaiCard;
		return c;
	}
};

JixuCard::JixuCard()
{
}

bool JixuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (targets.isEmpty())
		return to_select != Self;
	else {
		int hp = targets.first()->getHp();
		return to_select != Self && to_select->getHp() == hp;
	}
}

void JixuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	bool has_slash = false;
	foreach (const Card *c, source->getCards("h")) {
		if (c->isKindOf("Slash")) {
			has_slash = true;
			break;
		}
	}

	QStringList choices;
	QList<ServerPlayer *> players;
	foreach (ServerPlayer *p, targets) {
		if (source->isDead()) return;
		if (p->isDead()) continue;
		QString choice = room->askForChoice(p, "jixu", "has+not", QVariant::fromValue(source));
		if (p->isAlive()) {
			choices << choice;
			players << p;
		}
	}
	if (choices.isEmpty() || players.isEmpty()) return;

	QList<ServerPlayer *> nots, hass;

	int i = -1;
	foreach (ServerPlayer *p, players) {
		i++;
		if (i > choices.length()) break;
		if (p->isDead()) continue;

		QString str = choices.at(i);
		QString result = "wrong";
		if ((str == "has" && has_slash) || (str == "not" && !has_slash))
			result = "correct";

		LogMessage log;
		log.type = "#JixuChoice";
		log.from = p;
		log.arg = "jixu:" + str;
		log.arg2 = "jixu:" + result;
		room->sendLog(log);
		if (str == "has")
			hass << p;
		else
			nots << p;
	}

	if (has_slash) {
		if (nots.isEmpty()) {
			LogMessage log;
			log.type = "#JixuStop";
			log.from = source;
			room->sendLog(log);

			source->endPlayPhase(false);
			return;
		}
		foreach (ServerPlayer *p, nots) {
			room->addPlayerMark(p, "jixu_choose_not" + source->objectName() + "-PlayClear");
			room->addPlayerMark(p, "&jixu_wrong-PlayClear");
		}
		source->drawCards(nots.length(), "jixu");
	} else {
		if (hass.isEmpty()) {
			LogMessage log;
			log.type = "#JixuStop";
			log.from = source;
			room->sendLog(log);

			source->endPlayPhase(false);
			return;
		}
		foreach (ServerPlayer *p, hass) {
			if (source->isDead()) return;
			if (p->isDead() || !source->canDiscard(p, "he")) continue;
			int id = room->askForCardChosen(source, p, "he", "jixu", false, Card::MethodDiscard);
			room->throwCard(id, p, source);
		}
		source->drawCards(hass.length(), "jixu");
	}
}

class JixuVS : public ZeroCardViewAsSkill
{
public:
	JixuVS() : ZeroCardViewAsSkill("jixu")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("JixuCard");
	}

	const Card *viewAs() const
	{
		return new JixuCard;
	}
};

class Jixu : public TriggerSkill
{
public:
	Jixu() :TriggerSkill("jixu")
	{
		//events << TargetSpecified; 锟斤拷锟绞憋拷锟斤拷锟侥匡拷辏拷锟斤拷
		events << CardUsed;
		view_as_skill = new JixuVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash")) return false;

		LogMessage log;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->getMark("jixu_choose_not" + player->objectName() + "-PlayClear") > 0
					&& !use.to.contains(p) && player->canSlash(p, use.card, false)) {
				room->doAnimate(1, player->objectName(), p->objectName());
				use.to.append(p);
				log.to << p;
			}
		}

		if (!log.to.isEmpty()) {
			log.type = "#JixuSlash";
			log.from = player;
			log.arg = objectName();
			log.card_str = use.card->toString();
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(player, objectName());

			room->sortByActionOrder(use.to);
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class Chengzhao : public PhaseChangeSkill
{
public:
	Chengzhao() : PhaseChangeSkill("chengzhao")
	{
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive() && target->getPhase() == Player::Finish;
	}

	bool onPhaseChange(ServerPlayer *, Room *room) const
	{
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->isDead() || !p->hasSkill(objectName())) continue;
			if (p->getMark("chengzhao-Clear") < 2) continue;
			QList<ServerPlayer *> targets;
			foreach (ServerPlayer *t, room->getOtherPlayers(p)) {
				if (p->canPindian(t)) targets << t;
			}
			ServerPlayer *target = room->askForPlayerChosen(p, targets, objectName(), "@chengzhao-invoke", true, true);
			if (!target) continue;
			room->broadcastSkillInvoke(objectName());
			if (!p->pindian(target, objectName())) continue;
			Card *slash = Sanguosha->cloneCard("slash");
			slash->setSkillName("_chengzhao");
			slash->deleteLater();
			if (!p->canSlash(target, slash, false)) continue;
			target->addQinggangTag(slash);
			room->useCard(CardUseStruct(slash, p, target));
			target->removeQinggangTag(slash);
		}
		return false;
	}
};

class Xiefang : public DistanceSkill
{
public:
	Xiefang() : DistanceSkill("xiefang")
	{
	}

	int getCorrect(const Player *from, const Player *) const
	{
		int x = 0;
		if (from->hasSkill(objectName())) {
			if (from->isFemale()) x--;
			foreach (const Player *p, from->getAliveSiblings()) {
				if (p->isFemale()) x--;
			}
		}
		return x;
	}
};

class Zhengnan : public TriggerSkill
{
public:
	Zhengnan() : TriggerSkill("zhengnan")
	{
		events << Death;
		frequency = Frequent;
		waked_skills = "wusheng,dangxian,zhiman";
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *guansuo, QVariant &data) const
	{
		DeathStruct death = data.value<DeathStruct>();
		ServerPlayer *player = death.who;
		if (guansuo == player) return false;
		if (guansuo->isAlive() && room->askForSkillInvoke(guansuo, objectName(), data)) {
			room->broadcastSkillInvoke(objectName());
			QStringList choices;
			choices << "draw";
			if (!guansuo->hasSkill("wusheng", true)) choices << "wusheng";
			if (!guansuo->hasSkill("dangxian", true)) choices << "dangxian";
			if (!guansuo->hasSkill("zhiman"), true) choices << "zhiman";
			if (choices.isEmpty()) return false;
			QString choice = room->askForChoice(guansuo, "zhengnan", choices.join("+"), QVariant());
			if (choice == "draw")
				guansuo->drawCards(3, objectName());
			else {
				if (!guansuo->hasSkill(choice, true))
					room->handleAcquireDetachSkills(guansuo, choice);
			}
		}
		return false;
	}
};

class Duoduan : public TriggerSkill
{
public:
	Duoduan() : TriggerSkill("duoduan")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (!player->hasTurn()) return false;
		if (player->getMark("duoduan-Clear") > 0) return false;

		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.to.contains(player) || !use.card->isKindOf("Slash")) return false;
		if (player->isNude()) return false;

		const Card *c = room->askForCard(player, "..", "@duoduan-card", data, Card::MethodRecast, nullptr, false, objectName());
		if (!c) return false;
		room->addPlayerMark(player, "duoduan-Clear");

		LogMessage log;
		log.type = "$DuoduanRecast";
		log.from = player;
		log.arg = objectName();
		log.card_str = c->toString();
		room->sendLog(log);
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());
		CardMoveReason reason(CardMoveReason::S_REASON_RECAST, player->objectName());
		reason.m_skillName = objectName();
		room->moveCardTo(c, player, nullptr, Player::DiscardPile, reason);
		player->drawCards(1, "recast");

		if (use.from->isDead()) return false;
		if (use.from->canDiscard(use.from, "he")) {
			use.from->setTag("duoduanForAI", data);
			const Card *dis = room->askForDiscard(use.from, objectName(), 1, 1, true, true, "@duoduan-discard");
			use.from->removeTag("duoduanForAI");
			if (dis)
				use.no_respond_list << "_ALL_TARGETS";
			else {
				use.from->drawCards(2, objectName());
				use.nullified_list << "_ALL_TARGETS";
			}
		} else {
			use.from->drawCards(2, objectName());
			use.nullified_list << "_ALL_TARGETS";
		}
		data = QVariant::fromValue(use);
		return false;
	}
};

GongsunCard::GongsunCard()
{
	handling_method = Card::MethodDiscard;
}

void GongsunCard::onEffect(CardEffectStruct &effect) const
{
	QList<int> ids;
	QStringList names;
	for (int id = 0; id < Sanguosha->getCardCount(); id++) {
		const Card *c = Sanguosha->getEngineCard(id);
		if (c->isKindOf("DelayedTrick") || c->isKindOf("EquipCard")) continue;
		if (c->isKindOf("Slash") && c->objectName() != "slash") continue;
		if (names.contains(c->objectName())||Config.BanPackages.contains(c->getPackage())) continue;
		names << c->objectName();
		ids << id;
	}
	if (ids.isEmpty()) return;

	ServerPlayer *player = effect.from, *target = effect.to;
	Room *room = player->getRoom();

	room->fillAG(ids, player);
	int id = room->askForAG(player, ids, false, objectName());
	room->clearAG(player);

	LogMessage log;
	log.type = "#GongsunLimit";
	log.from = player;
	log.to << target;
	log.arg = Sanguosha->getEngineCard(id)->objectName();
	room->sendLog(log);

	names = player->getTag("GongsunLimited" + target->objectName()).toStringList();
	names << Sanguosha->getEngineCard(id)->getClassName();
	player->setTag("GongsunLimited" + target->objectName(), names);
	room->setPlayerCardLimitation(player, "use,response,discard", names.last()+"|.|.|hand", false);
	room->setPlayerCardLimitation(target, "use,response,discard", names.last()+"|.|.|hand", false);
	room->setPlayerMark(player, "&gongsun+" + log.arg,1);
	room->setPlayerMark(target, "&gongsun+" + log.arg,1);
}

class GongsunVS : public ViewAsSkill
{
public:
	GongsunVS() : ViewAsSkill("gongsun")
	{
		response_pattern = "@@gongsun";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		return !Self->isJilei(to_select) && selected.length() < 2;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.length() != 2)
			return nullptr;

		GongsunCard *c = new GongsunCard;
		c->addSubcards(cards);
		return c;
	}
};

class Gongsun : public TriggerSkill
{
public:
	Gongsun() : TriggerSkill("gongsun")
	{
		events << EventPhaseStart << EventPhaseChanging << Death;
		view_as_skill = new GongsunVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->isDead() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play) return false;
			if (player->canDiscard(player, "he"))
				room->askForUseCard(player, "@@gongsun", "@gongsun");
		} else {
			if (event == EventPhaseChanging) {
				if (data.value<PhaseChangeStruct>().to != Player::RoundStart) return false;
			} else {
				DeathStruct death = data.value<DeathStruct>();
				if (player != death.who) return false;
			}
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				QStringList limit_names = player->getTag("GongsunLimited" + p->objectName()).toStringList();
				if (limit_names.isEmpty()) continue;
				player->removeTag("GongsunLimited" + p->objectName());
				foreach (QString classname, limit_names) {
					room->removePlayerCardLimitation(player, "use,response,discard", classname + "|.|.|hand");
					room->removePlayerCardLimitation(p, "use,response,discard", classname + "|.|.|hand");
					for (int id = 0; id < Sanguosha->getCardCount(); id++) {
						const Card *c = Sanguosha->getEngineCard(id);
						if (c->getClassName() == classname) {
							room->removePlayerMark(player, "&gongsun+" + c->objectName());
							room->removePlayerMark(p, "&gongsun+" + c->objectName());
							break;
						}
					}
				}
			}
		}
		return false;
	}
};

class Andong : public TriggerSkill
{
public:
	Andong() : TriggerSkill("andong")
	{
		events << DamageInflicted << EventPhaseProceeding;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == DamageInflicted) {
			if (!player->hasSkill(objectName())) return false;
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.from || damage.from->isDead() || damage.from == player) return false;
			if (!player->askForSkillInvoke(this, QVariant::fromValue(damage.from))) return false;
			room->broadcastSkillInvoke(objectName());
			QStringList choices;
			choices << "prevent" << "get";
			QString choice = room->askForChoice(damage.from, objectName(), choices.join("+"), data);
			LogMessage log;
			log.type = "#FumianFirstChoice";
			log.from = damage.from;
			log.arg = "andong:" + choice;
			room->sendLog(log);
			if (choice == "prevent") {
				room->addPlayerMark(damage.from, "andong_heart-Clear");
				return true;
			} else {
				room->doGongxin(player, damage.from, QList<int>(), objectName());
				QList<int> hearts;
				foreach (int id, damage.from->handCards()) {
					if (Sanguosha->getCard(id)->getSuit() == Card::Heart) {
						hearts << id;
					}
				}
				if (hearts.isEmpty()) return false;
				DummyCard get(hearts);
				CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
				room->obtainCard(player, &get, reason, false);
			}
		} else {
			if (player->getPhase() != Player::Discard) return false;
			if (player->getMark("andong_heart-Clear") <= 0) return false;
			QList<int> hearts;
			foreach (int id, player->handCards()) {
				if (Sanguosha->getCard(id)->getSuit() == Card::Heart)
					hearts << id;
			}
			room->ignoreCards(player, hearts);
		}
		return false;
	}
};

YingshiCard::YingshiCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void YingshiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->setPlayerProperty(source, "yingshi_name", "");
	LogMessage log;
	log.type = "$KuangbiGet";
	log.from = source;
	log.arg = "yschou";
	log.card_str = QString::number(getEffectiveId());
	room->sendLog(log);
	room->obtainCard(source, this, true);
}

class YingshiVS : public OneCardViewAsSkill
{
public:
	YingshiVS() : OneCardViewAsSkill("yingshi")
	{
		expand_pile = "%yschou";
		response_pattern = "@@yingshi";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (selected.length() >= 2)
			return false;
		QString name = Self->property("yingshi_name").toString();
		foreach (const Player *p, Self->getAliveSiblings(true)) {
			if (p->objectName() == name)
				return p->getPile("yschou").contains(to_select->getId());
		}
		return false;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		YingshiCard *card = new YingshiCard;
		card->addSubcard(originalCard);
		return card;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}
};

class Yingshi : public TriggerSkill
{
public:
	Yingshi() : TriggerSkill("yingshi")
	{
		events << EventPhaseStart << Damage;
		view_as_skill = new YingshiVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Play || !player->hasSkill(objectName())) return false;
			bool has_chou = false;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (!p->getPile("yschou").isEmpty()) {
					has_chou = true;
					break;
				}
			}
			if (has_chou) return false;
			QList<int> hearts;
			foreach (const Card *c, player->getCards("he")) {
				if (c->getSuit() == Card::Heart)
					hearts << c->getEffectiveId();
			}
			if (hearts.isEmpty()) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@yingshi-invoke", true, true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());
			target->addToPile("yschou", hearts);
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->isKindOf("Slash")) return false;
			if (damage.to->isDead() || damage.to->getPile("yschou").isEmpty()) return false;
			room->setPlayerProperty(player, "yingshi_name", damage.to->objectName());
			if (!room->askForUseCard(player, "@@yingshi", "@yingshi:" + damage.to->objectName()))
				room->setPlayerProperty(player, "yingshi_name", "");
		}
		return false;
	}
};

class YingshiDeath : public TriggerSkill
{
public:
	YingshiDeath() : TriggerSkill("#yingshi-death")
	{
		events << Death;
		view_as_skill = new YingshiVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DeathStruct death = data.value<DeathStruct>();
		if (death.who == player || death.who->getPile("yschou").isEmpty()) return false;
		room->sendCompulsoryTriggerLog(player, "yingshi", true, true);
		DummyCard get(death.who->getPile("yschou"));
		room->obtainCard(player, &get, true);
		return false;
	}
};

QinguoCard::QinguoCard()
{
	mute = true;
}

bool QinguoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *slash = Sanguosha->cloneCard("slash");
	slash->setSkillName("qinguo");
	slash->deleteLater();
	return slash->targetFilter(targets, to_select, Self);
}

void QinguoCard::onUse(Room *room, CardUseStruct &card_use) const
{
	Card *slash = Sanguosha->cloneCard("slash");
	slash->setSkillName("qinguo");
	slash->deleteLater();
	room->useCard(CardUseStruct(slash, card_use.from, card_use.to), false);
}

class QinguoVS : public ZeroCardViewAsSkill
{
public:
	QinguoVS() : ZeroCardViewAsSkill("qinguo")
	{
		response_pattern = "@@qinguo";
	}

	const Card *viewAs() const
	{
		return new QinguoCard;
	}
};

class Qinguo : public TriggerSkill
{
public:
	Qinguo() : TriggerSkill("qinguo")
	{
		events << CardFinished << BeforeCardsMove << CardsMoveOneTime;
		view_as_skill = new QinguoVS;
	}

	int getPriority(TriggerEvent event) const
	{
		if (event == BeforeCardsMove) return -2;
		return TriggerSkill::getPriority(event);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardFinished) {
			if (!player->hasFlag("CurrentPlayer")) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("EquipCard")) return false;
			bool can_slash = false;
			Card *slash = Sanguosha->cloneCard("slash");
			slash->setSkillName(objectName());
			slash->deleteLater();
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (player->canSlash(p, slash, true)) {
					can_slash = true;
					break;
				}
			}
			if (!can_slash) return false;
			room->askForUseCard(player, "@@qinguo", "@qinguo");
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if ((move.to && move.to == player && move.to_place == Player::PlaceEquip)
				|| (move.from && move.from == player && move.from_places.contains(Player::PlaceEquip))) {
				if (event == BeforeCardsMove)
					room->setPlayerMark(player, "qinguo_equip_num", player->getEquips().length());
				else {
					int num = player->getEquips().length();
					int mark = player->getMark("qinguo_equip_num");
					if (num == player->getHp() && num != mark && player->getLostHp() > 0) {
						room->sendCompulsoryTriggerLog(player, objectName(), true, true);
						room->recover(player, RecoverStruct("qinguo", player));
					}
				}
			}
		}
		return false;
	}
};

KannanCard::KannanCard()
{
}

bool KannanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return Self->canPindian(to_select) && targets.isEmpty() && to_select->getMark("kannan_target-PlayClear") <= 0;
}

void KannanCard::onEffect(CardEffectStruct &effect) const
{
	if (!effect.from->canPindian(effect.to, false)) return;

	Room *room = effect.from->getRoom();
	int n = effect.from->pindianInt(effect.to, "kannan");
	if (n == 1) {
		room->addPlayerMark(effect.from, "kannan-PlayClear");
		room->addPlayerMark(effect.from, "&kannan");
	} else if (n == -1) {
		room->addPlayerMark(effect.to, "kannan_target-PlayClear");
		room->addPlayerMark(effect.to, "&kannan");
	}
}

class KannanVS : public ZeroCardViewAsSkill
{
public:
	KannanVS() : ZeroCardViewAsSkill("kannan")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if (player->getMark("kannan-PlayClear") > 0) return false;
		if (player->usedTimes("KannanCard") >= player->getHp()) return false;
		foreach (const Player *p, player->getAliveSiblings()) {
			if (player->canPindian(p) && p->getMark("kannan_target-PlayClear") <= 0)
				return true;
		}
		return false;
	}

	const Card *viewAs() const
	{
		return new KannanCard;
	}
};

class Kannan : public TriggerSkill
{
public:
	Kannan() :TriggerSkill("kannan")
	{
		events << CardUsed << ConfirmDamage << CardFinished;
		view_as_skill = new KannanVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash") || use.from->isDead() || use.from->getMark("&kannan") <= 0) return false;
			int n = use.from->getMark("&kannan");
			room->setPlayerMark(use.from, "&kannan", 0);
			room->setTag("kannan_damage" + use.card->toString() + use.from->objectName(), n);
		} else if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card) return false;
			int n = room->getTag("kannan_damage" + damage.card->toString() + damage.from->objectName()).toInt();
			if (n <= 0 || damage.from->isDead() || damage.to->isDead()) return false;
			LogMessage log;
			log.type = "#KannanDamage";
			log.from = damage.from;
			log.arg = QString::number(damage.damage);
			log.arg2 = QString::number(damage.damage += n);
			log.to << damage.to;
			room->sendLog(log);
			data = QVariant::fromValue(damage);
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			room->removeTag("kannan_damage" + use.card->toString() + use.from->objectName());
		}
		return false;
	}
};

class Zhaohuo : public TriggerSkill
{
public:
	Zhaohuo() : TriggerSkill("zhaohuo")
	{
		events << Dying;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DyingStruct dying = data.value<DyingStruct>();
		if (dying.who == player) return false;
		if (player->getMaxHp() == 1) return false;
		int n = qrand() % 2 + 1;
		if (player->isJieGeneral())
			n += 2;
		room->sendCompulsoryTriggerLog(player, objectName(), true, true, n);
		if (player->getMaxHp() < 1) {
			room->gainMaxHp(player, 1 - player->getMaxHp(), objectName());
		} else {
			int change = player->getMaxHp() - 1;
			room->loseMaxHp(player, change, objectName());
			player->drawCards(change, objectName());
		}
		return false;
	}
};

class Yixiang : public TriggerSkill
{
public:
	Yixiang() : TriggerSkill("yixiang")
	{
		events << TargetConfirmed;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (player->getMark("yixiang-Clear") > 0 || !room->hasCurrent()) return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("SkillCard")) return false;
		if (!use.from || use.from->isDead() || use.from->getHp() <= player->getHp() || !use.to.contains(player)) return false;
		QList<int> get;
		foreach (int id, room->getDrawPile()) {
			const Card *c = Sanguosha->getCard(id);
			if (!c->isKindOf("BasicCard")) continue;
			foreach (const Card *card, player->getCards("he")) {
				if (card->sameNameWith(c)) continue;
				get << id;
				break;
			}
		}
		if (get.isEmpty()) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		room->addPlayerMark(player, "yixiang-Clear");
		int id = get.at(qrand() % get.length());
		player->obtainCard(Sanguosha->getCard(id), false);
		return false;
	}
};

class Yirang : public PhaseChangeSkill
{
public:
	Yirang() : PhaseChangeSkill("yirang")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Play) return false;
		DummyCard *dummy = new DummyCard();
		foreach (const Card *c, player->getCards("he")) {
			if (!c->isKindOf("BasicCard"))
				dummy->addSubcard(c);
		}
		if (dummy->subcardsLength() > 0) {
			QList<ServerPlayer *> players;
			foreach(ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getMaxHp() > player->getMaxHp())
					players << p;
			}
			if (!players.isEmpty()) {
				ServerPlayer *target = room->askForPlayerChosen(player, players, objectName(), "@yirang-invoke", true, true);
				if (target) {
					room->broadcastSkillInvoke(objectName());
					CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "yirang", "");
					room->obtainCard(target, dummy, reason, false);
					if (target->getMaxHp() > player->getMaxHp())
						room->gainMaxHp(player, target->getMaxHp() - player->getMaxHp(), objectName());
					else if (target->getMaxHp() < player->getMaxHp())
						room->loseMaxHp(player, player->getMaxHp() - target->getMaxHp(), objectName());
					QList<int> types;
					foreach (int id, dummy->getSubcards()) {
						if (!types.contains(Sanguosha->getCard(id)->getTypeId()))
							types << Sanguosha->getCard(id)->getTypeId();
					}
					if (types.length() > 0) {
						int n = qMin(types.length(), player->getMaxHp() - player->getHp());
						if (n > 0)
							room->recover(player, RecoverStruct(player, nullptr, n, "yirang"));
					}
				}
			}
		}
		delete dummy;
		return false;
	}
};

class Tushe : public TriggerSkill
{
public:
	Tushe() : TriggerSkill("tushe")
	{
		events << TargetSpecified;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("SkillCard") || use.card->isKindOf("EquipCard") || use.to.isEmpty()) return false;
		foreach (const Card *c, player->getCards("he"))
			if (c->isKindOf("BasicCard")) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		player->drawCards(use.to.length(), objectName());
		return false;
	}
};

LimuCard::LimuCard()
{
	mute = true;
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodUse;
}

void LimuCard::onUse(Room *room, CardUseStruct &card_use) const
{
	Card *indulgence = Sanguosha->cloneCard("indulgence");
	indulgence->addSubcard(this);
	indulgence->setSkillName("limu");
	indulgence->deleteLater();
	if (card_use.from->isProhibited(card_use.from, indulgence)) return;
	room->useCard(CardUseStruct(indulgence, card_use.from, card_use.from), true);
	room->recover(card_use.from, RecoverStruct("limu", card_use.from));
}

class Limu : public OneCardViewAsSkill
{
public:
	Limu() : OneCardViewAsSkill("limu")
	{
		response_or_use = true;
	}

	bool viewFilter(const Card *to_select) const
	{
		if (to_select->getSuit() != Card::Diamond) return false;
		Card *indulgence = Sanguosha->cloneCard("indulgence");
		indulgence->addSubcard(to_select);
		indulgence->setSkillName("limu");
		indulgence->deleteLater();
		return !Self->isLocked(indulgence) && !Self->isProhibited(Self, indulgence);
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->hasJudgeArea() && !player->containsTrick("indulgence");
	}

	const Card *viewAs(const Card *originalcard) const
	{
		LimuCard *c = new LimuCard;
		c->addSubcard(originalcard);
		return c;
	}
};

class LimuTargetMod : public TargetModSkill
{
public:
	LimuTargetMod() : TargetModSkill("#limu-target")
	{
		pattern = ".";
	}

	int getResidueNum(const Player *from, const Card *card, const Player *to) const
	{
		if (from->getJudgingArea().length()>0&&to&&from->inMyAttackRange(to)&&from->hasSkill("limu"))
			return 999;
		if(card->getSkillName()=="weizhuang")
			return 999;
		return 0;
	}

	int getDistanceLimit(const Player *from, const Card *, const Player *to) const
	{
		if (from->getJudgingArea().length()>0&&to&&from->inMyAttackRange(to)&&from->hasSkill("limu"))
			return 999;
		return 0;
	}
};

class MobileShouye : public TriggerSkill
{
public:
	MobileShouye() : TriggerSkill("mobileshouye")
	{
		events << TargetConfirmed << BeforeCardsMove;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetConfirmed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard") || use.to.length() != 1 || !use.to.contains(player)) return false;
			if (!use.from || use.from->isDead() || use.from == player) return false;
			if (player->getMark("mobileshouye-Clear") > 0 || !room->hasCurrent()) return false;
			player->setTag("mobileshouyeForAI", data);
			bool invoke = player->askForSkillInvoke(this, QVariant::fromValue(use.from));
			player->removeTag("mobileshouyeForAI");
			if (!invoke) return false;
			room->broadcastSkillInvoke(objectName());
			room->addPlayerMark(player, "mobileshouye-Clear");

			QString shouce = room->askForChoice(player, objectName(), "kaicheng+qixi");
			QString gongce = room->askForChoice(use.from, objectName(), "quanjun+fenbing");
			LogMessage log;
			log.type = "#MobileshouyeDuice";
			log.from = player;
			log.arg = "mobileshouye:" + shouce;
			log.to << use.from;
			log.arg2 = "mobileshouye:" + gongce;
			room->sendLog(log);

			if ((shouce == "kaicheng" && gongce == "fenbing") || (shouce == "qixi" && gongce == "quanjun")) {
				log.type = "#MobileshouyeDuiceSucceed";
				room->sendLog(log);
				use.nullified_list << player->objectName();
				data = QVariant::fromValue(use);
				room->setCardFlag(use.card, objectName() + player->objectName());
			} else {
				log.type = "#MobileshouyeDuiceFail";
				room->sendLog(log);
			}
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from_places.contains(Player::PlaceTable) && move.to_place == Player::DiscardPile
                    && (move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE) {
				const Card *card = move.reason.m_extraData.value<const Card *>();
				if (!card || !card->hasFlag(objectName() + player->objectName())) return false;
				room->setCardFlag(card, "-" + objectName() + player->objectName());
				QList<int> ids;
				if (card->isVirtualCard())
					ids = card->getSubcards();
				else
					ids << card->getEffectiveId();

				if (ids.isEmpty() || !room->CardInTable(card)) return false;
				player->obtainCard(card, true);
				move.removeCardIds(ids);
				data = QVariant::fromValue(move);
			}
		}
		return false;
	}
};

MobileLiezhiCard::MobileLiezhiCard()
{
}

bool MobileLiezhiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length() < 2 && Self->canDiscard(to_select, "hej") && to_select != Self;
}

void MobileLiezhiCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.from->isDead() || effect.to->isDead()) return;
	if (!effect.from->canDiscard(effect.to, "hej")) return;
	Room *room = effect.from->getRoom();
	int card_id = room->askForCardChosen(effect.from, effect.to, "hej", "mobileliezhi", false, Card::MethodDiscard);
	room->throwCard(card_id, room->getCardPlace(card_id) == Player::PlaceDelayedTrick ? nullptr : effect.to, effect.from);
}

class MobileLiezhiVS : public ZeroCardViewAsSkill
{
public:
	MobileLiezhiVS() : ZeroCardViewAsSkill("mobileliezhi")
	{
		response_pattern = "@@mobileliezhi";
	}

	const Card *viewAs() const
	{
		return new MobileLiezhiCard;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}
};

class MobileLiezhi : public TriggerSkill
{
public:
	MobileLiezhi() : TriggerSkill("mobileliezhi")
	{
		events << EventPhaseStart << Death << EventLoseSkill << Damaged;
		view_as_skill = new MobileLiezhiVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->hasSkill(objectName()) && player->getPhase() == Player::Start && player->isAlive()) {
				if (player->getMark("mobileliezhi_disabled") > 0) return false;
				bool candis = false;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if (player->canDiscard(p, "hej")) {
						candis = true;
						break;
					}
				}
				if (!candis) return false;
				room->askForUseCard(player, "@@mobileliezhi", "@mobileliezhi");
			} else if (player->getPhase() == Player::Finish && player->hasSkill(objectName(), true)) {
				if (player->getMark("mobileliezhi_disabled") <= 0) return false;
				room->setPlayerMark(player, "mobileliezhi_disabled", 0);
			}
		} else if (event == Damaged) {
			if (!player->isAlive() || !player->hasSkill(objectName())) return false;
			LogMessage log;
			log.type = "#MobileliezhiDisabled";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(player, objectName());
			room->addPlayerMark(player, "mobileliezhi_disabled");
		} else {
			if (event == Death) {
				DeathStruct death = data.value<DeathStruct>();
				if (death.who != player || !player->hasSkill(objectName())) return false;
			} else if (event == EventLoseSkill) {
				if (data.toString() != objectName() || player->isDead()) return false;
			}
			if (player->getMark("mobileliezhi_disabled") <= 0) return false;
			room->setPlayerMark(player, "mobileliezhi_disabled", 0);
		}
		return false;
	}
};

class Jijun : public TriggerSkill
{
public:
	Jijun() :TriggerSkill("jijun")
	{
		events << CardsMoveOneTime << TargetSpecified;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetSpecified) {
			if (player->getPhase() != Player::Play) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			if (use.card->isKindOf("Weapon") || !use.card->isKindOf("EquipCard")) {
				if (!use.to.contains(player)) return false;
				if (!player->askForSkillInvoke(this)) return false;
				room->broadcastSkillInvoke(objectName());
				JudgeStruct judge;
				judge.pattern = ".";
				judge.play_animation = false;
				judge.who = player;
				judge.reason = objectName();
				room->judge(judge);
			}
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (!move.to && move.to_place == Player::DiscardPile && move.reason.m_skillName == objectName() &&
					move.reason.m_reason == CardMoveReason::S_REASON_JUDGEDONE)
				player->addToPile("jjfang", move.card_ids);
		}
		return false;
	}
};

FangtongCard::FangtongCard()
{
	will_throw = false;
	target_fixed = true;
	mute = true;
	handling_method = Card::MethodNone;
}

void FangtongCard::onUse(Room *, CardUseStruct &) const
{
}

class FangtongVS : public ViewAsSkill
{
public:
	FangtongVS() : ViewAsSkill("fangtong")
	{
		expand_pile = "jjfang";
	}

	bool viewFilter(const QList<const Card *> &, const Card *to_select) const
	{
		return Self->getPile("jjfang").contains(to_select->getEffectiveId());
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;

		FangtongCard *c = new FangtongCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@fangtong!";
	}
};

class Fangtong : public PhaseChangeSkill
{
public:
	Fangtong() : PhaseChangeSkill("fangtong")
	{
		view_as_skill = new FangtongVS;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Finish) return false;
		if (player->getPile("jjfang").isEmpty()) return false;

		if (!player->canDiscard(player, "he")) return false;
		const Card *c = room->askForCard(player, "..", "fangtong-invoke", QVariant(), objectName());
		if (!c) return false;
		room->broadcastSkillInvoke(objectName());

		if (player->isDead() || player->getPile("jjfang").isEmpty()) return false;

		CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, player->objectName(), "fangtong", "");
		const Card *card = nullptr;
		QList<int> fang = player->getPile("jjfang");
		if (fang.length() == 1)
			card = Sanguosha->getCard(fang.first());
		else {
			card = room->askForUseCard(player, "@@fangtong!", "@fangtong");
			if (!card) {
				card = Sanguosha->getCard(fang.at(qrand() % fang.length()));
			}
		}
		if (!card) return false;
		room->throwCard(card, reason, nullptr);

		if (player->isDead()) return false;

		int num = Sanguosha->getCard(c->getSubcards().first())->getNumber();
		QList<int> ids;
		if (card->isVirtualCard())
			ids = card->getSubcards();
		else
			ids << card->getEffectiveId();
		foreach (int id, ids) {
			num += Sanguosha->getCard(id)->getNumber();
		}
		if (num != 36) return false;

		ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@fangtong-damage");
		room->doAnimate(1, player->objectName(), target->objectName());
		room->damage(DamageStruct(objectName(), player, target, 3, DamageStruct::Thunder));
		return false;
	}
};

class Shuyong : public TriggerSkill
{
public:
	Shuyong() : TriggerSkill("shuyong")
	{
		events << CardUsed << CardResponded;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		const Card *card;
		if (triggerEvent == CardUsed)
			card = data.value<CardUseStruct>().card;
		else
			card = data.value<CardResponseStruct>().m_card;
		if (!card || !card->isKindOf("Slash")) return false;
		QList<ServerPlayer *> players;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (!p->isAllNude())
				players << p;
		}
		ServerPlayer *target = room->askForPlayerChosen(player, players, objectName(), "@shuyong-invoke", true, true);
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());
		if (target->isAllNude()) return false;
		int id = room->askForCardChosen(player, target, "hej", objectName());
		room->obtainCard(player, id, false);
		if (target->isAlive())
			target->drawCards(1, objectName());
		return false;
	}
};

MobileXushenCard::MobileXushenCard()
{
	target_fixed = true;
}

void MobileXushenCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->doSuperLightbox(source, "mobilexushen");
	room->removePlayerMark(source, "@mobilexushenMark");
	int male = 0;
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if (!p->isMale()) continue;
		male++;
	}
	if (male <= 0) return;
	room->loseHp(HpLostStruct(source, male, "mobilexushen", source));
}

class MobileXushenVS : public ZeroCardViewAsSkill
{
public:
	MobileXushenVS() : ZeroCardViewAsSkill("mobilexushen")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if (player->getMark("@mobilexushenMark") <= 0) return false;
		QList<const Player *> as = player->getAliveSiblings();
		as << player;
		foreach (const Player *p, as) {
			if (p->isMale())
				return true;
		}
		return false;
	}

	const Card *viewAs() const
	{
		return new MobileXushenCard;
	}
};

class MobileXushen : public TriggerSkill
{
public:
	MobileXushen() : TriggerSkill("mobilexushen")
	{
		events << QuitDying;
		frequency = Limited;
		limit_mark = "@mobilexushenMark";
		view_as_skill = new MobileXushenVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DyingStruct dying = data.value<DyingStruct>();
		if (!dying.hplost || dying.hplost->reason != objectName() || dying.hplost->from != player) return false;
		ServerPlayer *saver = player->getSaver();
		if (!saver) return false;
		if (!player->askForSkillInvoke(objectName(), QString("mobilexushen:%1").arg(saver->objectName()))) return false;
		QStringList skills;
		skills << "wusheng" << "dangxian";
		room->handleAcquireDetachSkills(saver, skills);
		return false;
	}
};

class MoboleZhennan : public TriggerSkill
{
public:
	MoboleZhennan() : TriggerSkill("mobolezhennan")
	{
		events << TargetSpecified;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("SkillCard")) return false;
		if (use.to.length() <= 1) return false;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (player->isDead()) return false;
			if (use.to.length() <= player->getHp()) return false;
			if (p->isDead() || !p->hasSkill(objectName()) || !use.to.contains(p)) continue;
			if (!p->canDiscard(p, "he")) continue;
			if (!room->askForCard(p, "..", "@mobolezhennan-discard:" + player->objectName(), data, objectName())) continue;
			room->broadcastSkillInvoke(objectName());
			room->damage(DamageStruct(objectName(), p, player));
		}
		return false;
	}
};

MobileSpQianxinCard::MobileSpQianxinCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void MobileSpQianxinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QList<int> ids = getSubcards();
	QList<ServerPlayer *> players = room->getOtherPlayers(source);

	int n = 0;
	QList<CardsMoveStruct> moves;
	while (n < 2) {
		if (ids.isEmpty() || players.isEmpty()) break;

		int id = ids.at(qrand() % ids.length());
		ids.removeOne(id);

		ServerPlayer *to = players.at(qrand() % players.length());
		players.removeOne(to);

		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(), to->objectName(), "mobilespqianxin", "");
		CardsMoveStruct move(QList<int>() << id, to, Player::PlaceHand, reason);
		moves << move;
	}
	if (moves.isEmpty()) return;
	room->moveCardsAtomic(moves, false);
}

class MobileSpQianxinVS : public ViewAsSkill
{
public:
	MobileSpQianxinVS() : ViewAsSkill("mobilespqianxin")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (to_select->isEquipped()) return false;
		int n = Self->getAliveSiblings().length();
		n = qMin(2, n);
		return selected.length() < n;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("MobileSpQianxinCard");
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;

		MobileSpQianxinCard *card = new MobileSpQianxinCard;
		card->addSubcards(cards);
		return card;
	}
};

class MobileSpQianxin : public PhaseChangeSkill
{
public:
	MobileSpQianxin() : PhaseChangeSkill("mobilespqianxin")
	{
		view_as_skill = new MobileSpQianxinVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive() && target->getPhase() == Player::RoundStart && !target->getTag("mobilespqianxin_xin").toList().isEmpty();
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		player->removeTag("mobilespqianxin_xin");
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (player->isDead()) return false;
			if (p->isDead() || !p->hasSkill(objectName())) continue;
			QStringList choices;
			choices << "draw";
			if (player->getMaxCards() > 0)
				choices << "maxcards";
			QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(p));
			LogMessage log;
			log.type = "#FumianFirstChoice";
			log.from = player;
			log.arg = "mobilespqianxin:" + choice;
			room->sendLog(log);

			if (choice == "draw")
				p->drawCards(2, objectName());
			else
				room->addMaxCards(player, -2);
		}
		return false;
	}
};

class MobileSpQianxinMove : public TriggerSkill
{
public:
	MobileSpQianxinMove() : TriggerSkill("#mobilespqianxin-move")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from && move.from_places.contains(Player::PlaceHand) && move.to && move.to_place == Player::PlaceHand
				&& move.reason.m_skillName == "mobilespqianxin") {
			QVariantList xin = move.to->getTag("mobilespqianxin_xin").toList();
			foreach (int id, move.card_ids) {
				if (xin.contains(id)) continue;
				xin << id;
			}
			move.to->setTag("mobilespqianxin_xin", xin);
		} else if (move.from && move.from_places.contains(Player::PlaceHand)) {
			QVariantList xin = move.from->getTag("mobilespqianxin_xin").toList();
			for (int i = 0; i < move.card_ids.length(); i++) {
				if (move.from_places.at(i) == Player::PlaceHand) {
					if (!xin.contains(move.card_ids.at(i))) continue;
					xin.removeOne(move.card_ids.at(i));
				}
			}
			move.from->setTag("mobilespqianxin_xin", xin);
		}
		return false;
	}
};

class MobileZhenxing : public TriggerSkill
{
public:
	MobileZhenxing() : TriggerSkill("mobilezhenxing")
	{
		events << EventPhaseStart << Damaged;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Finish) return false;
		}
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());

		QList<int> views = room->getNCards(3, false);

		LogMessage log;
		log.type = "$ViewDrawPile";
		log.from = player;
		log.arg = QString::number(3);
		log.card_str = ListI2S(views).join("+");
		room->sendLog(log, player);
		log.type = "#ViewDrawPile";
		room->sendLog(log, room->getOtherPlayers(player, true));

		QStringList suits, duplication;
		foreach (int id, views) {
			QString suit = Sanguosha->getCard(id)->getSuitString();
			if (!suits.contains(suit)) suits << suit;
			else duplication << suit;
		}

		QList<int> enabled, disabled;
		foreach (int id, views) {
			if (duplication.contains(Sanguosha->getCard(id)->getSuitString()))
				disabled << id;
			else
				enabled << id;
		}

		room->fillAG(views, player, disabled);
		int id = room->askForAG(player, enabled, enabled.length()<=1, objectName());
		room->clearAG(player);
		room->returnToTopDrawPile(views);
		if(!enabled.isEmpty()){
			if(id<0) id = enabled.first();
			room->obtainCard(player, id, false);
		}
		return false;
	}
};

class Zhongzuo : public TriggerSkill
{
public:
	Zhongzuo() : TriggerSkill("zhongzuo")
	{
		events << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if (change.to != Player::NotActive) return false;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->isDead()||!p->hasSkill(objectName())||(p->getMark("zhongzuo_damaged-Clear")<1&&p->getMark("zhongzuo_damage-Clear")<1)) continue;
			ServerPlayer *target = room->askForPlayerChosen(p, room->getAlivePlayers(), objectName(), "@zhongzuo-invoke", true, true);
			if (!target) continue;
			room->broadcastSkillInvoke(objectName());
			target->drawCards(2, objectName());
			if (target->isWounded())
				p->drawCards(1, objectName());
		}
		return false;
	}
};

class Wanlan : public TriggerSkill
{
public:
	Wanlan() : TriggerSkill("wanlan")
	{
		events << Dying;
		frequency = Limited;
		limit_mark = "@wanlanMark";
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DyingStruct dying = data.value<DyingStruct>();
		ServerPlayer *who = dying.who;
		if (player->getMark("@wanlanMark") <= 0 || !player->canDiscard(player, "h")
			|| !player->askForSkillInvoke(this, QVariant::fromValue(who))) return false;
		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(player, "wanlan");
		room->removePlayerMark(player, "@wanlanMark");
		player->throwAllHandCards();
		if (who->getHp() < 1) {
			int n = qMin(1 - who->getHp(), who->getMaxHp() - who->getHp());
			if (n > 0) room->recover(who, RecoverStruct(player, nullptr, n, "wanlan"));
		}
		ServerPlayer *current = room->getCurrent();
		if (current && current->isAlive() && current->hasFlag("CurrentPlayer"))
			room->addPlayerMark(current, "wanlan_" + player->objectName() + "-Clear");
		return false;
	}
};

class WanlanDamage : public TriggerSkill
{
public:
	WanlanDamage() : TriggerSkill("#wanlan-damage")
	{
		events << QuitDying;
		frequency = Limited;
		//limit_mark = "@wanlan";
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const
	{
		ServerPlayer *current = room->getCurrent();
		if (current && current->isAlive() && current->hasFlag("CurrentPlayer")) {
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (current->getMark("wanlan_" + p->objectName() + "-Clear") > 0) {
					room->setPlayerMark(current, "wanlan_" + p->objectName() + "-Clear", 0);
					room->damage(DamageStruct("wanlan", p ,current));
					break;
				}
			}
		}
		return false;
	}
};

TongquCard::TongquCard()
{
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool TongquCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select->getMark("&tqqu") > 0 && to_select != Self;
}

bool TongquCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	int id = getSubcards().first();
	if (Self->canDiscard(Self, id))
		return true;
	return !targets.isEmpty();
}

void TongquCard::onUse(Room *room, CardUseStruct &card_use) const
{
	int id = getSubcards().first();
	const Card *c = Sanguosha->getCard(id);
	if (card_use.to.isEmpty()) {
		if (card_use.from->canDiscard(card_use.from, id)) {
			CardMoveReason reason(CardMoveReason::S_REASON_THROW, card_use.from->objectName(), "tongqu", "");
			room->throwCard(this, reason, card_use.from, nullptr);
		} else {
			QList<ServerPlayer *> targets;
			foreach (ServerPlayer *p, room->getOtherPlayers(card_use.from)) {
				if (p->getMark("&tqqu") <= 0) continue;
				targets << p;
			}
			if (targets.isEmpty()) return;
			ServerPlayer *target = targets.at(qrand() % targets.length());
			CardMoveReason reason(CardMoveReason::S_REASON_GIVE, card_use.from->objectName(), target->objectName(), "tongqu", "");
			room->obtainCard(target, this, reason, false);
			if (target->isAlive() && c->isKindOf("EquipCard") && c->isAvailable(target) && !target->isProhibited(target, c))
				room->useCard(CardUseStruct(c, target));
		}
	} else {
		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, card_use.from->objectName(), card_use.to.first()->objectName(), "tongqu", "");
		room->obtainCard(card_use.to.first(), this, reason, false);
		if (card_use.to.first()->isAlive() && c->isKindOf("EquipCard") && c->isAvailable(card_use.to.first())
				&& !card_use.to.first()->isProhibited(card_use.to.first(), c))
			room->useCard(CardUseStruct(c, card_use.to.first()));
	}
}

class TongquVS : public OneCardViewAsSkill
{
public:
	TongquVS() : OneCardViewAsSkill("tongqu")
	{
		filter_pattern = ".";
		response_pattern = "@@tongqu!";
	}

	const Card *viewAs(const Card *originalcard) const
	{
		TongquCard *c = new TongquCard;
		c->addSubcard(originalcard->getId());
		return c;
	}
};

class Tongqu : public TriggerSkill
{
public:
	Tongqu() : TriggerSkill("tongqu")
	{
		events << DrawNCards << AfterDrawNCards;
		view_as_skill = new TongquVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == DrawNCards) {
			DrawStruct draw = data.value<DrawStruct>();
			if (draw.reason!="draw_phase"||player->getMark("&tqqu") <= 0) return false;
			int length = 0;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->isDead() || !p->hasSkill("tongqu")) continue;
				room->broadcastSkillInvoke("tongqu");
				room->notifySkillInvoked(p, "tongqu");
				length++;
			}
			if (length <= 0) return false;
			room->setPlayerFlag(player, "tongqu");
			room->addPlayerMark(player, "tongqu-Clear", length);
			draw.num += length;
			LogMessage log;
			log.type = "#HuaijuDraw";
			log.from = player;
			log.arg = "tongqu";
			log.arg2 = QString::number(length);
			room->sendLog(log);
			data = QVariant::fromValue(draw);
		} else {
			DrawStruct draw = data.value<DrawStruct>();
			if (draw.reason!="draw_phase"||!player->hasFlag("tongqu")) return false;
			room->setPlayerFlag(player, "-tongqu");
			int n = player->getMark("tongqu-Clear");
			room->addPlayerMark(player, "tongqu-Clear", 0);
			for (int i = 0; i < n; i++) {
				if (player->isDead() || player->isNude()) return false;
				if (!room->askForUseCard(player, "@@tongqu!", "@tongqu")) {
					QList<int> dis;
					foreach (const Card *c, player->getCards("he")) {
						if (!player->canDiscard(player, c->getEffectiveId())) continue;
						dis << c->getEffectiveId();
					}
					if (!dis.isEmpty()) {
						int id = dis.at(qrand() % dis.length());
						room->throwCard(id, player);
					} else {
						const Card *c = player->getCards("he").at(qrand() % player->getCards("he").length());
						QList<ServerPlayer *> targets;
						foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
							if (p->getMark("&tqqu") <= 0) continue;
							targets << p;
						}
						if (targets.isEmpty()) return false;
						ServerPlayer *target = targets.at(qrand() % targets.length());
						CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "tongqu", "");
						room->obtainCard(target, c, reason, false);
						if (target->isAlive() && c->isKindOf("EquipCard") && c->isAvailable(target) && !target->isProhibited(target, c))
							room->useCard(CardUseStruct(c, target));
					}
				}
			}
		}
		return false;
	}
};

class TongquTrigger : public TriggerSkill
{
public:
	TongquTrigger() : TriggerSkill("#tongqu-trigger")
	{
		events << GameStart << EventPhaseStart << Dying;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == GameStart) {
			room->sendCompulsoryTriggerLog(player, "tongqu", true, true);
			player->gainMark("&tqqu");
		} else if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Start) return false;
			QList<ServerPlayer *> targets;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (p->getMark("&tqqu") > 0) continue;
				targets << p;
			}
			if (targets.isEmpty()) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, targets, "tongqu", "@tongqu-invoke", true, true);
			if (!target) return false;
			room->broadcastSkillInvoke("tongqu");
			room->loseHp(HpLostStruct(player, 1, "tongqu", player));
			target->gainMark("&tqqu");
		} else {
			DyingStruct dying = data.value<DyingStruct>();
			if (dying.who->getMark("&tqqu") <= 0) return false;
			room->sendCompulsoryTriggerLog(player, "tongqu", true, true);
			dying.who->loseAllMarks("&tqqu");
		}
		return false;
	}
};

class NewWanlan : public TriggerSkill
{
public:
	NewWanlan() : TriggerSkill("newwanlan")
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
		if (damage.damage < player->getHp()) return false;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (player->isDead()) return false;
			if (p->isDead() || !p->hasSkill(objectName()) || !p->canDiscard(p, "e")) continue;
			if (!p->askForSkillInvoke(this, QVariant::fromValue(player))) continue;
			room->broadcastSkillInvoke(objectName());
			p->throwAllEquips();
			return true;
		}
		return false;
	}
};

class Biaozhao : public TriggerSkill
{
public:
	Biaozhao() : TriggerSkill("biaozhao")
	{
		events << EventPhaseStart << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() == Player::Finish) {
				if (player->isNude() || !player->getPile("bzbiao").isEmpty()) return false;
				const Card *card = room->askForCard(player, "..", "biaozhao-put", QVariant(), Card::MethodNone);
				if (!card) return false;
				LogMessage log;
				log.type = "#InvokeSkill";
				log.from = player;
				log.arg = objectName();
				room->sendLog(log);
				room->broadcastSkillInvoke(objectName());
				room->notifySkillInvoked(player, objectName());
				player->addToPile("bzbiao", card);
			} else if (player->getPhase() == Player::Start) {
				if (player->getPile("bzbiao").isEmpty()) return false;
				room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				player->clearOnePrivatePile("bzbiao");
				if (player->isDead()) return false;
				ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@biaozhao-invoke");
				room->doAnimate(1, player->objectName(), target->objectName());
				room->recover(target, RecoverStruct("biaozhao", player));
				int n = target->getHandcardNum();
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if (p->getHandcardNum() > n)
						n = p->getHandcardNum();
				}
				int draw = qMin(5, n - target->getHandcardNum());
				if (draw <= 0) return false;
				target->drawCards(draw, objectName());
			}
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (!move.to && move.to_place == Player::DiscardPile) {
				foreach (int id, move.card_ids) {
					const Card *card = Sanguosha->getCard(id);
					foreach (ServerPlayer *p, room->getAllPlayers()) {
						if (p->isDead() || !p->hasSkill(objectName()) || p->getPile("bzbiao").isEmpty()) continue;
						foreach (int idd, p->getPile("bzbiao")) {
							if (p->isDead() || !p->hasSkill(objectName())) continue;
							const Card *c = Sanguosha->getCard(idd);
							if (card->getSuit() == c->getSuit() && card->getNumber() == c->getNumber()) {
								room->sendCompulsoryTriggerLog(p, objectName() ,true, true);
								if (((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) &&
										move.from && move.from->isAlive()) {
									ServerPlayer *from = room->findPlayerByObjectName(move.from->objectName());
									if (!from || from->isDead()) {
										CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", p->objectName(), "biaozhao", "");
										room->throwCard(c, reason, nullptr);
									} else {
										room->obtainCard(from, c, true);
									}
								} else {
									CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", p->objectName(), "biaozhao", "");
									room->throwCard(c, reason, nullptr);
								}
								room->loseHp(HpLostStruct(p, 1, objectName(), p));
							}
						}
					}
				}
			}
		}
		return false;
	}
};

class Yechou : public TriggerSkill
{
public:
	Yechou() : TriggerSkill("yechou")
	{
		events << Death << EventPhaseChanging << EventPhaseStart;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == Death) {
			DeathStruct death = data.value<DeathStruct>();
			if (death.who != player || !death.who->hasSkill(objectName())) return false;
			QList<ServerPlayer *> targets;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (p->getLostHp() > 1)
					targets << p;
			}
			if (targets.isEmpty()) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@yechou-invoke", true, true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());
			room->addPlayerMark(target, "&yechou");
		} else if (event == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->isDead() || p->getMark("&yechou") <= 0) continue;
				for (int i = 0; i < p->getMark("&yechou"); i++) {
					if (p->isDead()) break;
					LogMessage log;
					log.type = "#YechouEffect";
					log.from = p;
					log.arg = objectName();
					room->sendLog(log);
					room->loseHp(HpLostStruct(p, 1, objectName(), p));
			}
			}
		} else {
			if (player->getPhase() != Player::RoundStart || player->getMark("&yechou") <= 0) return false;
			room->setPlayerMark(player, "&yechou", 0);
		}
		return false;
	}
};

class Zhengjian : public TriggerSkill
{
public:
	Zhengjian() : TriggerSkill("zhengjian")
	{
		events << EventPhaseStart << EventLoseSkill << PreCardUsed << PreCardResponded << Death;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->isDead() || !player->hasSkill(objectName())) return false;
			if (player->getPhase() == Player::Finish) {
				ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@zhengjian-invoke", false, true);
				room->broadcastSkillInvoke(objectName());
				QStringList names = player->property("ZhengjianTargets").toStringList();
				if (names.contains(target->objectName())) return false;
				names << target->objectName();
				room->setPlayerProperty(player, "ZhengjianTargets", names);
				target->gainMark("&zhengjian");
			} else if (player->getPhase() == Player::RoundStart) {
				QStringList names = player->property("ZhengjianTargets").toStringList();
				if (names.isEmpty()) return false;
				room->setPlayerProperty(player, "ZhengjianTargets", QStringList());

				QList<ServerPlayer *> sp;
				foreach (QString name, names) {
					ServerPlayer *target = room->findPlayerByObjectName(name);
					if (target && target->isAlive() && !sp.contains(target))
						sp << target;
				}
				if (sp.isEmpty()) return false;

				bool peiyin = false;
				room->sortByActionOrder(sp);
				foreach (ServerPlayer *p, sp) {
					int mark = p->getMark("&zhengjiandraw");
					room->setPlayerMark(p, "&zhengjiandraw", 0);
					mark = qMin(5, mark);
					mark = qMin(mark, p->getMaxHp());
					if (mark > 0) {
						if (!peiyin) {
							peiyin = true;
							room->sendCompulsoryTriggerLog(player, objectName(), true, true);
						}
						p->drawCards(mark, objectName());
					}
					if (p->getMark("&zhengjian") > 0) {
						if (!peiyin) {
							peiyin = true;
							room->sendCompulsoryTriggerLog(player, objectName(), true, true);
						}
						p->loseAllMarks("&zhengjian");
					}
				}
			}
		} else if (event == PreCardUsed) {
			if (player->getMark("&zhengjian") <= 0) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			room->addPlayerMark(player, "&zhengjiandraw");
		} else if (event == PreCardResponded) {
			if (player->getMark("&zhengjian") <= 0) return false;
			CardResponseStruct res = data.value<CardResponseStruct>();
			if (res.m_card->isKindOf("SkillCard")) return false;
			room->addPlayerMark(player, "&zhengjiandraw");
		} else {
			if (event == EventLoseSkill) {
				if (player->isDead() || data.toString() != objectName()) return false;
			} else if (event == Death) {
				if (data.value<DeathStruct>().who != player) return false;
			}

			QStringList names = player->property("ZhengjianTargets").toStringList();
			if (names.isEmpty()) return false;
			room->setPlayerProperty(player, "ZhengjianTargets", QStringList());

			QList<ServerPlayer *> sp;
			foreach (QString name, names) {
				ServerPlayer *target = room->findPlayerByObjectName(name);
				if (target && target->isAlive() && !sp.contains(target))
					sp << target;
			}
			if (sp.isEmpty()) return false;

			room->sortByActionOrder(sp);
			foreach (ServerPlayer *p, sp) {
				room->setPlayerMark(p, "&zhengjian", 0);
				room->setPlayerMark(p, "&zhengjiandraw", 0);
			}
		}
		return false;
	}
};

GaoyuanCard::GaoyuanCard()
{
}

bool GaoyuanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (!targets.isEmpty()||to_select == Self||to_select->getMark("&zhengjian")<1) return false;
	const Card *slash = Card::Parse(Self->property("gaoyuanData").toString());
	foreach (const Player *p, Self->getAliveSiblings()) {
		if(p->hasFlag("GaoyuanFrom")&&p->canSlash(to_select, slash, false))
			return true;
	}
	return false;
}

void GaoyuanCard::onEffect(CardEffectStruct &effect) const
{
	effect.to->setFlags("GaoyuanTarget");
}

class GaoyuanVS : public OneCardViewAsSkill
{
public:
	GaoyuanVS() : OneCardViewAsSkill("gaoyuan")
	{
		filter_pattern = ".";
		response_pattern = "@@gaoyuan";
	}

	const Card *viewAs(const Card *originalCard) const
	{
		GaoyuanCard *c = new GaoyuanCard;
		c->addSubcard(originalCard);
		return c;
	}
};

class Gaoyuan : public TriggerSkill
{
public:
	Gaoyuan() : TriggerSkill("gaoyuan")
	{
		events << TargetConfirming;
		view_as_skill = new GaoyuanVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();

		if (use.card->isKindOf("Slash") && player->canDiscard(player, "he")) {
			QList<ServerPlayer *> players = room->getOtherPlayers(player);
			players.removeOne(use.from);

			bool can_invoke = false;
			foreach (ServerPlayer *p, players) {
				if (p->getMark("&zhengjian") > 0 && use.from->canSlash(p, use.card, false)) {
					can_invoke = true;
					break;
				}
			}

			if (can_invoke) {
				room->setPlayerFlag(use.from,"GaoyuanFrom");
				QString prompt = "@gaoyuan:" + use.from->objectName();
				room->setPlayerProperty(player, "gaoyuanData", use.card->toString());
				if (room->askForUseCard(player, "@@gaoyuan", prompt, -1, Card::MethodDiscard)) {
					foreach (ServerPlayer *p, players) {
						if (p->hasFlag("GaoyuanTarget")) {
							p->setFlags("-GaoyuanTarget");
							use.to.removeOne(player);
							use.to.append(p);
							room->sortByActionOrder(use.to);
							data = QVariant::fromValue(use);
							break;
						}
					}
				}
				room->setPlayerFlag(use.from,"-GaoyuanFrom");
			}
		}
		return false;
	}
};

class Xuewei : public TriggerSkill
{
public:
	Xuewei() : TriggerSkill("xuewei")
	{
		events << EventPhaseStart << EventPhaseChanging << DamageInflicted << Death << EventLoseSkill;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->isDead() || player->getPhase() != Player::Start || !player->hasSkill(objectName())) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@xuewei-invoke", true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(player, objectName());

			LogMessage log;
			log.type = "#ChoosePlayerWithSkill";
			log.from = player;
			log.to << target;
			log.arg = objectName();
			room->sendLog(log, player);

			log.type = "#InvokeSkill";
			room->sendLog(log, room->getOtherPlayers(player, true));

			room->doAnimate(1, player->objectName(), target->objectName(), QList<ServerPlayer *>() << player);

			QStringList names = player->property("Xuewei_targets").toStringList();
			if (!names.contains(target->objectName()))
				names << target->objectName();
			room->setPlayerProperty(player, "Xuewei_targets", names);

			QList<ServerPlayer *> viewers;
			viewers << player;
			room->addPlayerMark(target, "&xuewei", 1, viewers);
		} else if (event == DamageInflicted) {
			if (player->isDead()) return false;
			DamageStruct damage = data.value<DamageStruct>();
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->isDead() || !p->hasSkill(objectName())) continue;
				QStringList names = p->property("Xuewei_targets").toStringList();
				if (!names.contains(player->objectName())) continue;
				names.removeOne(player->objectName());
				room->setPlayerProperty(p, "Xuewei_targets", names);
				room->setPlayerMark(player, "&xuewei", 0);

				LogMessage log;
				log.type = "#XueweiPrevent";
				log.from = p;
				log.to << player;
				log.arg = objectName();
				log.arg2 = QString::number(damage.damage);
				room->sendLog(log);
				room->broadcastSkillInvoke(objectName());
				room->notifySkillInvoked(p, objectName());

				room->damage(DamageStruct(objectName(), nullptr, p, damage.damage));
				if (p->isAlive() && damage.from && damage.from->isAlive()) {
					room->doAnimate(1, p->objectName(), damage.from->objectName());
					room->damage(DamageStruct(objectName(), p, damage.from, damage.damage, damage.nature));
				}
				return true;
			}
		} else {
			if (event == EventPhaseChanging) {
				if (player->isDead()) return false;
				PhaseChangeStruct change = data.value<PhaseChangeStruct>();
				if (change.to != Player::RoundStart) return false;
			} else if (event == EventLoseSkill) {
				if (player->isDead()) return false;
				if (data.toString() != objectName()) return false;
			} else if (event == Death)
				if (data.value<DeathStruct>().who != player) return false;

			QStringList names = player->property("Xuewei_targets").toStringList();
			foreach (QString name, names) {
				ServerPlayer *p = room->findPlayerByObjectName(name);
				if (p) room->setPlayerMark(p, "&xuewei", 0);
			}
			room->setPlayerProperty(player, "Xuewei_targets", QStringList());
		}
		return false;
	}
};

class Liechi : public TriggerSkill
{
public:
	Liechi() : TriggerSkill("liechi")
	{
		events << EnterDying;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DyingStruct dying = data.value<DyingStruct>();
		if (!dying.damage) return false;
		ServerPlayer *from = dying.damage->from;
		if (from && from->isAlive() && from->canDiscard(from, "he")) {
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			room->askForDiscard(from, objectName(), 1, 1, false, true);
		}
		return false;
	}
};

static QHash<QString,TiansuanDialog*>TiansuanDialogs;

TiansuanDialog *TiansuanDialog::getInstance(const QString &name, const QString &choices)
{
	if (TiansuanDialogs[name] == nullptr)
		TiansuanDialogs[name] = new TiansuanDialog(name, choices);

	return TiansuanDialogs[name];
}

TiansuanDialog::TiansuanDialog(const QString &name, const QString &choices)
	: tiansuan_choices(choices)
{
	setObjectName(name);
	setWindowTitle(Sanguosha->translate(name));
	group = new QButtonGroup(this);

	button_layout = new QVBoxLayout;
	setLayout(button_layout);
	connect(group, SIGNAL(buttonClicked(QAbstractButton *)), this, SLOT(selectChoice(QAbstractButton *)));
}

bool TiansuanDialog::MarkJudge(const QString &choice)
{
	QString mark = objectName() + "_tiansuan_remove_" + choice;
	foreach (QString m, Self->getMarkNames()) {
		if (m.startsWith(mark) && Self->getMark(m) > 0)
			return false;
	}
	return true;
}

void TiansuanDialog::popup()
{
	Self->removeTag(objectName());
	foreach (QAbstractButton *button, group->buttons()) {
		button_layout->removeWidget(button);
		group->removeButton(button);
		delete button;
	}
	QStringList choices;

	if (objectName() == "tiansuan") {
		for (int i = 0; i < 6; i++)
			choices << QString::number(i);
	} else if (objectName() == "olsanyao") {
		if (Self->getMark("olsanyao_hp-PlayClear") <= 0)
			choices << "hp";
		if (Self->getMark("olsanyao_hand-PlayClear") <= 0)
			choices << "hand";
	} else if (objectName() == "tenyearjiaozhao") {
		int basic = Self->getMark("tenyearjiaozhao_basic-Clear") - 1;
		int trick = Self->getMark("tenyearjiaozhao_trick-Clear") - 1;

		if (Self->property("tenyearjiaozhao_level").toInt() < 2) {
			if (!Self->hasUsed("TenyearJiaozhaoCard") && Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY)
				choices << "show";
			if (basic >= 0)
				choices << "use=" + Self->property("tenyearjiaozhao_basic_name").toString();
			else if (trick >= 0)
				choices << "use=" + Self->property("tenyearjiaozhao_trick_name").toString();
		} else {
			if ((basic < 0 || trick < 0)&&Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY)
				choices << "show";
			if (basic >= 0)
				choices << "basic=" + Self->property("tenyearjiaozhao_basic_name").toString();
			if (trick >= 0)
				choices << "trick=" + Self->property("tenyearjiaozhao_trick_name").toString();
		}
	} else if (objectName() == "mtjieli") {
		foreach (const Card *c, Self->getHandcards()) {
			if (c->isRed()){
				if(choices.contains("red")) continue;
				choices << "red";
			}else if (c->isBlack()){
				if(choices.contains("black")) continue;
				choices << "black";
			}
			if (choices.length() >= 2)
				break;
		}
	} else {
		foreach (QString choice, tiansuan_choices.split(",")) {
			if (MarkJudge(choice)) choices << choice;
		}
	}
	if (choices.isEmpty()) return;
	foreach (QString choice, choices) {
		QAbstractButton *button = createChoiceButton(choice);
		button_layout->addWidget(button);
		button->setEnabled(true);
	}
	exec();
}

void TiansuanDialog::selectChoice(QAbstractButton *button)
{
	Self->setTag(objectName(), button->objectName());
	emit onButtonClick();
	accept();
}

QAbstractButton *TiansuanDialog::createChoiceButton(const QString &choice)
{
	QStringList choices = choice.split("=");
	QString _choice = objectName() + ":" + choices.first();
	QString translate = Sanguosha->translate(_choice);
	translate.replace("%src", Sanguosha->translate(choices.last()));

	QCommandLinkButton *button = new QCommandLinkButton(translate);
	button->setObjectName(choice);

	translate = Sanguosha->translate(_choice + ":effect");
	if (!translate.endsWith(":effect")) button->setToolTip(translate);

	group->addButton(button);
	return button;
}

TiansuanCard::TiansuanCard()
{
	//target_fixed = true;
}

bool TiansuanCard::targetFilter(const QList<const Player *> &, const Player *, const Player *) const
{
	return false;
}

bool TiansuanCard::targetsFeasible(const QList<const Player *> &, const Player *) const
{
	return true;
}

void TiansuanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->addPlayerMark(source, "tiansuan_lun");
	room->setEmotion(source, "chouqian");

	QList<int> mingyunqians;
	int num = -1;
	if (!user_string.isEmpty())
		num = user_string.split(":").last().toInt();
	if (num > 0)
		mingyunqians << num;
	mingyunqians << 1 << 2 << 2 << 3 << 3 << 3 << 4 << 4 << 5;

	int mingyunqian = mingyunqians.at(qrand() % mingyunqians.length());

	LogMessage log;
	log.from = source;
	log.type = "#TiansuanMingyunqian";
	log.arg = "tiansuan" + QString::number(mingyunqian);
	room->sendLog(log);

	ServerPlayer *target = room->askForPlayerChosen(source, room->getAlivePlayers(), "tiansuan_"  + QString::number(mingyunqian), "@tiansuan-mingyunqian:" + log.arg);
	room->doAnimate(1, source->objectName(), target->objectName());

	log.type = "#TiansuanMingyunqianTarget";
	log.to << target;
	room->sendLog(log);

	room->addPlayerMark(target, "&" + log.arg + "+#" + source->objectName());

	if (mingyunqian == 1) {
		room->doGongxin(source, target, QList<int>(), "tiansuan");
		QString flags = "hej";
		if (source == target)
			flags = "ej";
		if (target->getCards(flags).isEmpty()) return;
		int id = room->askForCardChosen(source, target, flags, "tiansuan", true);
		CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, source->objectName());
		room->obtainCard(source, Sanguosha->getCard(id), reason);
	} else if (mingyunqian == 2) {
		QString flags = "he";
		if (source == target)
			flags = "e";
		if (target->getCards(flags).isEmpty()) return;
		int id = room->askForCardChosen(source, target, flags, "tiansuan");
		CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, source->objectName());
		room->obtainCard(source, Sanguosha->getCard(id), reason);
	} else if (mingyunqian == 5)
		room->setPlayerCardLimitation(target, "use", "Peach,Analeptic", false);

}

class TiansuanVS : public ZeroCardViewAsSkill
{
public:
	TiansuanVS() : ZeroCardViewAsSkill("tiansuan")
	{
	}

	const Card *viewAs() const
	{
		QString choice = Self->getTag("tiansuan").toString();
		if (choice.isEmpty()) return nullptr;
		TiansuanCard *card = new TiansuanCard;
		card->setUserString(choice);
		return card;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("tiansuan_lun") <= 0;
	}
};

class Tiansuan : public TriggerSkill
{
public:
	Tiansuan() : TriggerSkill("tiansuan")
	{
		events << Death << EventPhaseStart;
		view_as_skill = new TiansuanVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	void removeMingyunqian(ServerPlayer *player, ServerPlayer *player2) const
	{
		Room *room = player->getRoom();
		foreach (QString mark, player->getMarkNames()) {
			if (player->getMark(mark) <= 0) continue;
			if (mark.startsWith("&tiansuan") && mark.endsWith("+#" + player2->objectName()))
				room->setPlayerMark(player, mark, 0);
		}

		bool limit = false;
		foreach (QString mark, player->getMarkNames()) {
			if (player->getMark(mark) <= 0) continue;
			if (mark.startsWith("&tiansuan5")) {
				limit = true;
				break;
			}
		}
		if (!limit)
			room->removePlayerCardLimitation(player, "use", "Peach,Analeptic");
	}

	QDialog *getDialog() const
	{
		return TiansuanDialog::getInstance(objectName());
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == Death) {
			ServerPlayer *who = data.value<DeathStruct>().who;
			foreach (ServerPlayer *p, room->getAllPlayers())
				removeMingyunqian(p, who);

			foreach (QString mark, who->getMarkNames()) {
				if (who->getMark(mark) <= 0) continue;
				if (mark.startsWith("&tiansuan5"))
					room->removePlayerCardLimitation(who, "use", "Peach,Analeptic");
			}
		} else if (event == EventPhaseStart) {
			if (player->getPhase() != Player::RoundStart) return false;
			foreach (ServerPlayer *p, room->getAllPlayers())
				removeMingyunqian(p, player);
		}
		return false;
	}
};

class TiansuanEffect : public TriggerSkill
{
public:
	TiansuanEffect() : TriggerSkill("#tiansuan")
	{
		events << DamageInflicted << Damaged;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	int MingyunqianNum(ServerPlayer *player, int i) const
	{
		int num = 0;
		foreach (QString mark, player->getMarkNames()) {
			if (player->getMark(mark) <= 0) continue;
			if (mark.startsWith("&tiansuan" + QString::number(i)))
				num++;
		}
		return num;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();

		LogMessage log;
		log.from = player;

		if (event == DamageInflicted) {
			log.arg2 = QString::number(damage.damage);
			if (MingyunqianNum(player, 1) > 0) {
				log.type = "#TiansuanMingyunqianEffect1";
				log.arg = "tiansuan1";
				room->sendLog(log);
				return true;
			}

			if (MingyunqianNum(player, 2) > 0) {
				log.type = "#TiansuanMingyunqianEffect2";
				log.arg = "tiansuan2";
				room->sendLog(log);
				damage.damage = 1;
			}

			if (MingyunqianNum(player, 3) > 0) {
				log.type = "#TiansuanMingyunqianEffect3";
				log.arg = "tiansuan3";
				room->sendLog(log);
				damage.nature = DamageStruct::Fire;
				if (damage.damage > 1)
					damage.damage = 1;
			}

			for (int i = 0; i < MingyunqianNum(player, 4); i++) {
				log.type = "#TiansuanMingyunqianEffect4";
				log.arg = "tiansuan4";
				room->sendLog(log);
				++damage.damage;
			}

			for (int i = 0; i < MingyunqianNum(player, 5); i++) {
				log.type = "#TiansuanMingyunqianEffect4";
				log.arg = "tiansuan4";
				room->sendLog(log);
				++damage.damage;
			}

			data = QVariant::fromValue(damage);
		} else {
			for (int i = 0; i < damage.damage * MingyunqianNum(player, 2); i ++)
				player->drawCards(1, "tiansuan");
		}
		return false;
	}
};

class ZhiyiVS : public ZeroCardViewAsSkill
{
public:
	ZhiyiVS() : ZeroCardViewAsSkill("zhiyi")
	{
		response_pattern = "@@zhiyi!";
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	const Card *viewAs() const
	{
		QString name = Self->property("zhiyi_card_name").toString();
		Card *c = Sanguosha->cloneCard(name);
		c->setSkillName("_zhiyi");
		return c;
	}
};

class Zhiyi : public TriggerSkill
{
public:
	Zhiyi() : TriggerSkill("zhiyi")
	{
		events << CardUsed << CardResponded << CardFinished;
		frequency = Compulsory;
		view_as_skill = new ZhiyiVS;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (!room->hasCurrent()) return false;
		if (event == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->hasFlag("zhiyi_card")) return false;
			Card *c = Sanguosha->cloneCard(use.card->objectName());
			c->setSkillName("_" + objectName());
			c->deleteLater();
			if (player->isLocked(c)) return false;
			room->setPlayerProperty(player, "zhiyi_card_name", c->objectName());

			if (room->askForUseCard(player, "@@zhiyi!", "@zhiyi:" + c->objectName(), -1, Card::MethodUse, false)) return false;

			QList<ServerPlayer *> targets = room->getCardTargets(player,c);
			if (targets.isEmpty()) return false;
			room->useCard(CardUseStruct(c, player, targets.at(qrand() % targets.length())), false);
		} else if (event == CardUsed) {
			if (player->getMark("zhiyi-Clear") > 0) return false;
			const Card *card = data.value<CardUseStruct>().card;
			if (!card || !card->isKindOf("BasicCard")) return false;

			room->sendCompulsoryTriggerLog(player, this);
			room->addPlayerMark(player, "zhiyi-Clear");

			QStringList choices;
			Card *c = Sanguosha->cloneCard(card);
			c->setSkillName("_" + objectName());
			int n = player->usedTimes(c->getClassName());
			player->clearHistory(c->getClassName());
			if (c->isAvailable(player))
				choices << "use";
			choices << "draw";

			QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(card));
			player->addHistory(c->getClassName(),n);
			if (choice == "use")
				room->setCardFlag(card, "zhiyi_card");
			else
				player->drawCards(1, objectName());
			delete c;
		} else {
			if (player->getMark("zhiyi-Clear") > 0) return false;
			const Card *card = data.value<CardResponseStruct>().m_card;
			if (!card || !card->isKindOf("BasicCard")) return false;

			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			room->addPlayerMark(player, "zhiyi-Clear");

			QStringList choices;
			Card *c = Sanguosha->cloneCard(card->objectName());
			c->setSkillName("_" + objectName());
			if (c->isAvailable(player))
				choices << "use";
			choices << "draw";

			QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(card));
			if (choice == "use") {
				room->setPlayerProperty(player, "zhiyi_card_name", c->objectName());

				if (room->askForUseCard(player, "@@zhiyi!", "@zhiyi:" + c->objectName(), -1, Card::MethodUse, false)) return false;

				QList<ServerPlayer *> targets = room->getCardTargets(player,c);
				if(!targets.isEmpty())
					room->useCard(CardUseStruct(c, player, targets.at(qrand() % targets.length())), false);
			}else
				player->drawCards(1, objectName());
			delete c;
		}
		return false;
	}
};

class SecondZhiyiVS : public ZeroCardViewAsSkill
{
public:
	SecondZhiyiVS() : ZeroCardViewAsSkill("secondzhiyi")
	{
		response_pattern = "@@secondzhiyi!";
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	const Card *viewAs() const
	{
		QString name = Self->property("secondzhiyi_card_name").toString();

		Card *c = Sanguosha->cloneCard(name);
		c->setSkillName("_secondzhiyi");
		return c;
	}
};

class SecondZhiyi : public PhaseChangeSkill
{
public:
	SecondZhiyi() : PhaseChangeSkill("secondzhiyi")
	{
		frequency = Compulsory;
		view_as_skill = new SecondZhiyiVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->getPhase() == Player::Finish;
	}

	bool onPhaseChange(ServerPlayer *, Room *room) const
	{
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->isDead() || !p->hasSkill(objectName())) continue;
			QStringList used_cards = p->getTag("SecondZhiyiUsedCard").toStringList();
			if (used_cards.isEmpty()) continue;

			room->sendCompulsoryTriggerLog(p, objectName(), true, true);
			QStringList choices;

			foreach (QString card_name, used_cards) {
				Card *c = Sanguosha->cloneCard(card_name);
				if (!c) continue;
				c->setSkillName("_secondzhiyi");
				if (c->isAvailable(p))
					choices << card_name;
				delete c;
			}
			choices << "draw";
			QString choice = room->askForChoice(p, objectName(), choices.join("+"));
			if (choice == "draw")
				p->drawCards(1, objectName());
			else {
				Card *c = Sanguosha->cloneCard(choice);
				c->setSkillName("_secondzhiyi");
				if (c->targetFixed())
					room->useCard(CardUseStruct(c, p), false);
				else {
					room->setPlayerProperty(p, "secondzhiyi_card_name", choice);
					if (!room->askForUseCard(p, "@@secondzhiyi!", "@secondzhiyi:" + choice)) {
						foreach (ServerPlayer *t, room->getAlivePlayers()) {
							if (c->targetFilter(QList<const Player *>(), t, p)){
								room->useCard(CardUseStruct(c, p, t), false);
								break;
							}
						}
					}
				}
				c->deleteLater();
			}
		}
		return false;
	}
};

class SecondZhiyiRecord : public TriggerSkill
{
public:
	SecondZhiyiRecord() : TriggerSkill("#secondzhiyi-record")
	{
		events << CardUsed << CardResponded << EventPhaseChanging;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAlivePlayers())
				p->removeTag("SecondZhiyiUsedCard");
		} else {
			if (player->isDead()) return false;
			const Card *c = nullptr;
			if (event == CardResponded)
				c = data.value<CardResponseStruct>().m_card;
			else
				c = data.value<CardUseStruct>().card;
			if (!c || !c->isKindOf("BasicCard")) return false;
			QStringList used_cards = player->getTag("SecondZhiyiUsedCard").toStringList();
			if (used_cards.contains(c->objectName())) return false;
			used_cards << c->objectName();
			player->setTag("SecondZhiyiUsedCard", used_cards);
		}
		return false;
	}
};

class Jimeng : public PhaseChangeSkill
{
public:
	Jimeng() : PhaseChangeSkill("jimeng")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Play) return false;
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (!p->isNude())
				targets << p;
		}
		if (targets.isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@jimeng-invoke", true, true);
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());
		if (target->isNude()) return false;
		int id = room->askForCardChosen(player, target, "he", objectName());
		room->obtainCard(player, id, false);
		if (player->isNude() || player->getHp() <= 0) return false;

		int n = qMin(player->getCardCount(), player->getHp());
		QString prompt = QString("@jimeng-give:%1::%2").arg(target->objectName()).arg(QString::number(n));
		const Card *c = room->askForExchange(player, objectName(), n, n, true, prompt);
		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "jimeng", "");
		room->obtainCard(target, c, reason, false);
		return false;
	}
};

class Shuaiyan : public PhaseChangeSkill
{
public:
	Shuaiyan() : PhaseChangeSkill("shuaiyan")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Discard) return false;
		if (player->getHandcardNum() <= 1) return false;
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (!p->isNude())
				targets << p;
		}

		if (targets.isEmpty()) {
			if (!player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			room->showAllCards(player);
			return false;
		}

		ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@shuaiyan-invoke", true, true);
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());
		room->showAllCards(player);
		if (target->isNude()) return false;
		const Card *c = room->askForExchange(target, objectName(), 1, 1, true, "@shuaiyan-give:" + player->objectName());
		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), "shuaiyan", "");
		room->obtainCard(player, c, reason, false);
		return false;
	}
};

BeizhuCard::BeizhuCard()
{
}

bool BeizhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void BeizhuCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.from->isDead() || effect.to->isDead() || effect.to->isKongcheng()) return;

	Room *room = effect.from->getRoom();
	room->doGongxin(effect.from, effect.to, QList<int>(), "beizhu");

	QList<int> slashs;
	foreach (const Card *c, effect.to->getCards("h")) {
		if (c->isKindOf("Slash")) {
			slashs << c->getEffectiveId();
		}
	}
	if (slashs.isEmpty()) {
		if (!effect.from->canDiscard(effect.to, "he")) return;
		int card_id = room->askForCardChosen(effect.from, effect.to, "he", "beizhu", true, Card::MethodDiscard);
		room->throwCard(card_id, effect.to, effect.from);
		if (effect.from->isDead() || effect.to->isDead()) return;
		QList<int> slash_ids;
		foreach (int id, room->getDrawPile()) {
			if (Sanguosha->getCard(id)->isKindOf("Slash"))
				slash_ids << id;
		}
		if (slash_ids.isEmpty()) return;
		if (!effect.from->askForSkillInvoke("beizhu", QString("beizhu:" + effect.to->objectName()), false)) return;
		room->obtainCard(effect.to, slash_ids.at(qrand() % slash_ids.length()), true);
	} else {
		try {
			QVariantList list = effect.from->getTag("beizhu_slash").toList();
			foreach (int id, slashs) {
				//if (list.contains(QVariant(id))) continue;
				list << id;
			}
			effect.from->setTag("beizhu_slash", list);
			foreach (int id, slashs) {
				const Card *slash = Sanguosha->getCard(id);
				if (!effect.to->canSlash(effect.from, slash, false)) continue;
				room->useCard(CardUseStruct(slash, effect.to, effect.from));
			}
			effect.from->removeTag("beizhu_slash");
		}
		catch (TriggerEvent triggerEvent) {
			if (triggerEvent == TurnBroken || triggerEvent == StageChange)
				effect.from->removeTag("beizhu_slash");
			throw triggerEvent;
		}
	}
}

class BeizhuVS : public ZeroCardViewAsSkill
{
public:
	BeizhuVS() : ZeroCardViewAsSkill("beizhu")
	{
	}

	const Card *viewAs() const
	{
		return new BeizhuCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("BeizhuCard");
	}
};

class Beizhu : public MasochismSkill
{
public:
	Beizhu() : MasochismSkill("beizhu")
	{
		view_as_skill = new BeizhuVS;
	}

	void onDamaged(ServerPlayer *player, const DamageStruct &damage) const
	{
		if (!damage.card || !damage.card->isKindOf("Slash")) return;
		QVariantList list = player->getTag("beizhu_slash").toList();
		if (!list.contains(damage.card->getEffectiveId())) return;
		list.removeOne(damage.card->getEffectiveId());
		player->setTag("beizhu_slash", list);
		player->getRoom()->sendCompulsoryTriggerLog(player, objectName(), true, true);
		player->drawCards(damage.damage, objectName());
	}
};

class Juliao : public DistanceSkill
{
public:
	Juliao() : DistanceSkill("juliao")
	{
	}

	int getCorrect(const Player *, const Player *to) const
	{
		if (to->hasSkill(objectName())) {
			QSet<QString> kingdom_set;
			foreach(const Player *player, to->parent()->findChildren<const Player *>()){
				if (player->isAlive())
					kingdom_set << player->getKingdom();
			}
			return qMax(0, kingdom_set.size() - 1);
		}
		return 0;
	}
};

class Taomie : public TriggerSkill
{
public:
	Taomie() : TriggerSkill("taomie")
	{
		events << Damage << Damaged << DamageCaused;
	}

	bool transferMark(ServerPlayer *to, Room *room) const
	{
		int n = 0;
		foreach (ServerPlayer *p, room->getOtherPlayers(to)) {
			if (to->isDead()) break;
			if (p->isAlive() && p->getMark("&taomie") > 0) {
				n++;
				int mark = p->getMark("&taomie");
				p->loseAllMarks("&taomie");
				to->gainMark("&taomie", mark);
			}
		}
		return n > 0;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (event == Damage) {
			if (damage.to->isDead() || damage.to->getMark("&taomie") > 0 || !player->askForSkillInvoke(this, damage.to)) return false;
			room->broadcastSkillInvoke(objectName());
			if (transferMark(damage.to, room)) return false;
			damage.to->gainMark("&taomie", 1);
		} else if (event == Damaged) {
			if (!damage.from || damage.from->isDead() || damage.from->getMark("&taomie") > 0 ||
					!player->askForSkillInvoke(this, damage.from)) return false;
			room->broadcastSkillInvoke(objectName());
			if (transferMark(damage.from, room)) return false;
			damage.from->gainMark("&taomie", 1);
		} else {
			if (damage.to->isDead() || damage.to->getMark("&taomie") <= 0) return false;
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			QStringList choices;
			choices << "damage=" + damage.to->objectName();
			if (!damage.to->isAllNude())
				choices << "get=" + damage.to->objectName();
			choices << "all=" + damage.to->objectName();
			QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);

			/*LogMessage log;
			log.type = "#FumianFirstChoice";
			log.from = player;
			log.arg = "taomie:" + choice.split("=").first();
			room->sendLog(log);*/

			if (choice.startsWith("damage")) {
				++damage.damage;
				data = QVariant::fromValue(damage);
			} else if (choice.startsWith("get")) {
				if (damage.to->isAllNude()) return false;
				int id = room->askForCardChosen(player, damage.to, "hej", objectName());
				room->obtainCard(player, id, false);
				if (player->isDead() || room->getCardPlace(id) != Player::PlaceHand || room->getCardOwner(id) != player) return false;
				QList<int> list;
				list << id;
				room->askForYiji(player, list, objectName());
			} else {
				damage.tips << "taomie_throwmark_" + damage.to->objectName();
				++damage.damage;
				data = QVariant::fromValue(damage);
				if (damage.to->isAllNude()) return false;
				int id = room->askForCardChosen(player, damage.to, "hej", objectName());
				room->obtainCard(player, id, false);
				if (player->isDead() || room->getCardPlace(id) != Player::PlaceHand || room->getCardOwner(id) != player) return false;
				QList<int> list;
				list << id;
				room->askForYiji(player, list, objectName());
			}
		}
		return false;
	}
};

class TaomieMark : public TriggerSkill
{
public:
	TaomieMark() : TriggerSkill("#taomie-mark")
	{
		events << DamageComplete;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive() && target->getMark("&taomie") > 0;
	}

	bool trigger(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (!damage.tips.contains("taomie_throwmark_" + player->objectName())) return false;
		player->loseAllMarks("&taomie");
		return false;
	}
};

DaojiCard::DaojiCard()
{
	handling_method = Card::MethodDiscard;
}

bool DaojiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && !to_select->getEquips().isEmpty();
}

void DaojiCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.to->getEquips().isEmpty() || effect.from->isDead()) return;
	Room *room = effect.from->getRoom();
	int id = room->askForCardChosen(effect.from, effect.to, "e", "daoji");
	room->obtainCard(effect.from, id);

	const Card *equip = Sanguosha->getCard(id);
	if (!equip->isAvailable(effect.from)) return;

	effect.from->setTag("daoji_equip", id + 1);
	effect.from->setTag("daoji_target_" + QString::number(id + 1), QVariant::fromValue(effect.to));

	room->useCard(CardUseStruct(equip, effect.from));

	effect.from->removeTag("daoji_equip");
	effect.from->removeTag("daoji_target_" + QString::number(id + 1));
}

class DaojiVS : public OneCardViewAsSkill
{
public:
	DaojiVS() : OneCardViewAsSkill("daoji")
	{
	}

	bool viewFilter(const Card *to_select) const
	{
		return !Self->isJilei(to_select) && !to_select->isKindOf("BasicCard");
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("DaojiCard");
	}

	const Card *viewAs(const Card *originalcard) const
	{
		DaojiCard *card = new DaojiCard;
		card->addSubcard(originalcard);
		return card;
	}
};

class Daoji : public TriggerSkill
{
public:
	Daoji() : TriggerSkill("daoji")
	{
		events << CardFinished;
		view_as_skill = new DaojiVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Weapon")) return false;

		int use_id = use.card->getEffectiveId();
		int id = player->getTag("daoji_equip").toInt() - 1;
		if (id < 0 || use_id != id) return false;

		ServerPlayer *target = player->getTag("daoji_target_" + QString::number(id + 1)).value<ServerPlayer *>();
		if (!target) return false;

		player->removeTag("daoji_equip");
		player->removeTag("daoji_target_" + QString::number(id + 1));

		if (target->isDead()) return false;
		room->damage(DamageStruct("daoji", player, target));
		return false;
	}
};

ZhouxuanCard::ZhouxuanCard()
{
	target_fixed = true;
}

void ZhouxuanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	if (source->isDead()) return;
	ServerPlayer *target = room->askForPlayerChosen(source, room->getOtherPlayers(source), "zhouxuan", "@zhouxuan-invoke");
	room->doAnimate(1, source->objectName(), target->objectName());
	QStringList names;
	names << "EquipCard" << "TrickCard";
	foreach (int id, Sanguosha->getRandomCards()) {
		const Card *c = Sanguosha->getEngineCard(id);
		if (!c->isKindOf("BasicCard") || c->isKindOf("FireSlash") || c->isKindOf("ThunderSlash")) continue;
		QString name = c->objectName();
		if (names.contains(name)) continue;
		names << name;
	}
	if (names.isEmpty()) return;

	QString name = room->askForChoice(source, "zhouxuan", names.join("+"), QVariant::fromValue(target));

	/*LogMessage log;
	log.type = "#ZhouxuanChoice";
	log.from = source;
	log.to << target;
	log.arg = name;
	room->sendLog(log);*/

	/*QStringList zhouxuan = target->getTag("Zhouxuan" + source->objectName()).toStringList();
	if (!zhouxuan.contains(name)) {
		zhouxuan << name;
		target->setTag("Zhouxuan" + source->objectName(), zhouxuan);
		room->addPlayerMark(target, "&zhouxuan+" + name);
	}*/
	if (target->getTag("Zhouxuan" + source->objectName()).toString() == name) return;
	target->setTag("Zhouxuan" + source->objectName(), name);
	foreach (QString mark, target->getMarkNames()) {
		if (!mark.startsWith("&zhouxuan+") && !mark.endsWith("+#" + source->objectName())) continue;
		if (target->getMark(mark) <= 0) continue;
		room->setPlayerMark(target, mark, 0);
	}
	room->setPlayerMark(target, "&zhouxuan+" + name + "+#" + source->objectName(), 1, QList<ServerPlayer *>() << source);
}

class ZhouxuanVS : public OneCardViewAsSkill
{
public:
	ZhouxuanVS() :OneCardViewAsSkill("zhouxuan")
	{
		filter_pattern = ".";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("ZhouxuanCard");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		ZhouxuanCard *c = new ZhouxuanCard;
		c->addSubcard(originalCard);
		return c;
	}
};

class Zhouxuan : public TriggerSkill
{
public:
	Zhouxuan() : TriggerSkill("zhouxuan")
	{
		events << CardUsed << CardResponded << Death;
		view_as_skill = new ZhouxuanVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == Death) {
			if (player != data.value<DeathStruct>().who) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player))
				player->removeTag("Zhouxuan" + p->objectName());
		} else {
			const Card *card = nullptr;
			if (event == CardUsed)
				card = data.value<CardUseStruct>().card;
			else
				card = data.value<CardResponseStruct>().m_card;
			if (card == nullptr || card->isKindOf("SkillCard")) return false;

			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->isDead() || !p->hasSkill(objectName(), true)) continue;
				QString zhouxuan = player->getTag("Zhouxuan" + p->objectName()).toString();
				if (zhouxuan.isEmpty()) continue;
				player->removeTag("Zhouxuan" + p->objectName());
				room->setPlayerMark(player, "&zhouxuan+" + zhouxuan + "+#" + p->objectName(), 0);

				if (p->isDead() || !p->hasSkill(objectName())) continue;
				bool same = false;
				if (card->isKindOf("EquipCard")) {
					if (zhouxuan != "EquipCard")
						continue;
					else
						same = true;
				}
				if (card->isKindOf("TrickCard")) {
					if (zhouxuan != "TrickCard")
						continue;
					else
						same = true;
				}
				if (!same) {
					if (card->sameNameWith(zhouxuan))
						same = true;
				}
				if (!same) continue;
				room->sendCompulsoryTriggerLog(p, objectName(), true, true);

				QList<ServerPlayer *> _player;
				_player.append(p);
				QList<int> yiji_cards = room->getNCards(3, false);

				CardsMoveStruct move(yiji_cards, nullptr, p, Player::PlaceTable, Player::PlaceHand,
					CardMoveReason(CardMoveReason::S_REASON_PREVIEW, p->objectName(), objectName(), ""));
				QList<CardsMoveStruct> moves;
				moves.append(move);
				room->notifyMoveCards(true, moves, false, _player);
				room->notifyMoveCards(false, moves, false, _player);

				QList<int> origin_yiji = yiji_cards;
				while (room->askForYiji(p, yiji_cards, objectName(), true, false, true, -1, room->getAlivePlayers())) {
					CardsMoveStruct move(QList<int>(), p, nullptr, Player::PlaceHand, Player::PlaceTable,
						CardMoveReason(CardMoveReason::S_REASON_PREVIEW, p->objectName(), objectName(), ""));
					foreach (int id, origin_yiji) {
						if (room->getCardPlace(id) != Player::DrawPile) {
							move.card_ids << id;
							yiji_cards.removeOne(id);
						}
					}
					origin_yiji = yiji_cards;
					QList<CardsMoveStruct> moves;
					moves.append(move);
					room->notifyMoveCards(true, moves, false, _player);
					room->notifyMoveCards(false, moves, false, _player);
					if (!p->isAlive())
						return false;
				}

				if (!yiji_cards.isEmpty()) {
					CardsMoveStruct move(yiji_cards, p, nullptr, Player::PlaceHand, Player::PlaceTable,
										CardMoveReason(CardMoveReason::S_REASON_PREVIEW, p->objectName(), objectName(), ""));
					QList<CardsMoveStruct> moves;
					moves.append(move);
					room->notifyMoveCards(true, moves, false, _player);
					room->notifyMoveCards(false, moves, false, _player);

					DummyCard *dummy = new DummyCard(yiji_cards);
					p->obtainCard(dummy, false);
					delete dummy;
				}
			}
		}
		return false;
	}
};

class Fengji : public TriggerSkill
{
public:
	Fengji() : TriggerSkill("fengji")
	{
		events << EventPhaseStart << EventAcquireSkill << EventLoseSkill;
		frequency = Compulsory;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase()==Player::RoundStart){
				if (player->getTag("FengjiLastTurn").toBool()){
					room->setPlayerMark(player, "&fengji", 0);
					int n = player->getTag("FengjiHandNum").toInt();
					if (player->getHandcardNum()<n||!player->hasSkill(objectName())) return false;
					room->sendCompulsoryTriggerLog(player, objectName(), true, true);
					player->drawCards(2, objectName());
				}
			}else if(player->getPhase()==Player::NotActive){
				player->setTag("FengjiLastTurn", true);
				player->setTag("FengjiHandNum", player->getHandcardNum());
				if (player->isAlive()&&player->hasSkill(objectName(), true))
					room->setPlayerMark(player, "&fengji", player->getHandcardNum());
			}
		} else {
			if (player->hasSkill(objectName(), true)) {
				int n = player->getTag("FengjiHandNum").toInt();
				room->setPlayerMark(player, "&fengji", n);
			} else
				room->setPlayerMark(player, "&fengji", 0);
		}
		return false;
	}
};

class Bingqing : public TriggerSkill
{
public:
	Bingqing() : TriggerSkill("bingqing")
	{
		events << CardFinished;
		global = true;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (player->getPhase() != Player::Play) return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("SkillCard") || !use.card->hasSuit()) return false;
		QString suit_str = use.card->getSuitString();
		if (player->getMark("bingqing_" + suit_str + "-PlayClear") > 0) return false;
		player->addMark("bingqing_" + suit_str + "-PlayClear");
		player->addMark("bingqing_suit-PlayClear");

		if (!player->hasSkill(objectName())) return false;

		int mark = player->getMark("bingqing_suit-PlayClear");
		if (mark == 4) {
			ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@bingqing-damage", true, true);
			if (!t) return false;
			room->broadcastSkillInvoke(this);
			room->damage(DamageStruct(objectName(), player, t));
		} else if (mark == 3) {
			QList<ServerPlayer *>players;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (player->canDiscard(p, "hej"))
					players << p;
			}
			ServerPlayer *t = room->askForPlayerChosen(player, players, objectName(), "@bingqing-discard", true, true);
			if (!t) return false;
			room->broadcastSkillInvoke(this);
			int id = room->askForCardChosen(player, t, "hej", objectName(), false, Card::MethodDiscard);
			room->throwCard(id, t, player);
		} else if (mark == 2) {
			ServerPlayer *t = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@bingqing-draw", true, true);
			if (!t) return false;
			room->broadcastSkillInvoke(this);
			t->drawCards(2, objectName());
		}
		return false;
	}
};

class Yingfeng : public PhaseChangeSkill
{
public:
	Yingfeng() : PhaseChangeSkill("yingfeng")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Start) return false;

		bool has_mark = false;
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getMark("&mjyffeng") > 0)
				has_mark = true;
			else
				targets << p;
		}
		if (targets.isEmpty()) return false;

		if (has_mark) {
			ServerPlayer *t = room->askForPlayerChosen(player, targets, objectName(), "@yingfeng-transfer", true, true);
			if (!t) return false;
			room->broadcastSkillInvoke(this);
			int mark = 0;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				mark += p->getMark("&mjyffeng");
				p->loseAllMarks("&mjyffeng");
			}
			if (t->isAlive() && mark > 0)
				t->gainMark("&mjyffeng", mark);
		} else {
			ServerPlayer *t = room->askForPlayerChosen(player, targets, objectName(), "@yingfeng-gain", true, true);
			if (!t) return false;
			room->broadcastSkillInvoke(this);
			t->gainMark("&mjyffeng");
		}
		return false;
	}
};

class YingfengTarget : public TargetModSkill
{
public:
	YingfengTarget() : TargetModSkill("#yingfeng")
	{
		pattern = ".";
	}

	int getDistanceLimit(const Player *from, const Card *, const Player *) const
	{
		if(from->getMark("&mjyffeng") > 0){
			if(from->hasSkill("yingfeng")) return 999;
			foreach (const Player *p, from->getAliveSiblings()) {
				if (p->hasSkill("yingfeng")) return 999;
			}
		}
		return 0;
	}
};

class Huantu : public TriggerSkill
{
public:
	Huantu() : TriggerSkill("huantu")
	{
		events << EventPhaseChanging << EventPhaseEnd;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to != Player::Draw || player->isSkipped(Player::Draw)) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (player->isDead()) return false;
				if (p->isDead() || !p->hasSkill(objectName()) || p->isNude() || p->getMark("huantu_lun") > 0) continue;
				if (!p->inMyAttackRange(player)) continue;
				const Card *card = room->askForCard(p, "..", "@huantu-invoke:" + player->objectName(), data, Card::MethodNone);
				if (!card) continue;

				LogMessage log;
				log.type = "#InvokeSkill";
				log.from = p;
				log.arg = objectName();
				room->sendLog(log);
				room->broadcastSkillInvoke(this);
				room->notifySkillInvoked(p, objectName());

				room->addPlayerMark(p, "huantu_lun");
				room->giveCard(p, player, card, objectName());
				player->addMark("huantu_players_" + p->objectName() + "-Clear");
				player->skip(Player::Draw);
			}
		} else {
			if (player->getPhase() != Player::Finish) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->isDead()) continue;
				int mark = player->getMark("huantu_players_" + p->objectName() + "-Clear");
				if (mark <= 0) continue;

				if (!p->askForSkillInvoke(this, player)) continue;
				room->broadcastSkillInvoke(this);

				QStringList choices;
				if (player->isAlive())
					choices << "recover=" + player->objectName();
				choices << "draw=" + player->objectName();

				QString choice = room->askForChoice(p, objectName(), choices.join("+"), QVariant::fromValue(player));
				if (choice.startsWith("recover")) {
					room->recover(player, RecoverStruct("huantu", p));
					player->drawCards(2, objectName());
				} else {
					p->drawCards(3, objectName());
					if (p->isDead() || p->isKongcheng() || player->isDead()) continue;
					const Card *cards = room->askForExchange(p, objectName(), 2, 2, false, "@huantu-give:" + player->objectName());
					room->giveCard(p, player, cards, objectName());
				}
			}
		}
		return false;
	}
};

class Bihuo : public TriggerSkill
{
public:
	Bihuo() : TriggerSkill("bihuo")
	{
		events << QuitDying;
		frequency = Limited;
		limit_mark = "@bihuoMark";
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (player->isDead()) return false;
			if (!p->hasSkill(objectName()) || p->getMark("@bihuoMark") <= 0) continue;
			if (!p->askForSkillInvoke(this, player)) continue;
			room->broadcastSkillInvoke(this);

			room->removePlayerMark(p, "@bihuoMark");
			room->doSuperLightbox(p, "bihuo");

			player->drawCards(3, objectName());
			room->addPlayerMark(player, "&bihuo_lun");
		}
		return false;
	}
};

class BihuoDistance : public DistanceSkill
{
public:
	BihuoDistance() : DistanceSkill("#bihuo")
	{
		frequency = Limited;
	}

	int getCorrect(const Player *, const Player *to) const
	{
		if (to->getMark("&bihuo_lun") > 0) {
			QSet<QString> kingdom_set;
			foreach(const Player *player, to->parent()->findChildren<const Player *>()) {
				if (player->isAlive()) kingdom_set << player->getKingdom();
			}
			return to->getMark("&bihuo_lun") * kingdom_set.size();
		}
		return 0;
	}
};

class JibingVS : public OneCardViewAsSkill
{
public:
	JibingVS() : OneCardViewAsSkill("jibing")
	{
		filter_pattern = ".|.|.|jbbing";
		expand_pile = "jbbing";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if(player->getPile("jbbing").isEmpty())
			return false;
		Card *slash = Sanguosha->cloneCard("slash");
		slash->setSkillName("jibing");
		slash->deleteLater();
		return slash->isAvailable(player);
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if (player->getPile("jbbing").isEmpty()) return false;
		return pattern == "jink" || pattern.contains("slash") || pattern.contains("Slash");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		switch (Sanguosha->currentRoomState()->getCurrentCardUseReason()) {
		case CardUseStruct::CARD_USE_REASON_PLAY: {
			Card *slash = Sanguosha->cloneCard("slash");
			slash->setSkillName("jibing");
			slash->addSubcard(originalCard);
			return slash;
		}case CardUseStruct::CARD_USE_REASON_RESPONSE:
		case CardUseStruct::CARD_USE_REASON_RESPONSE_USE: {
			QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
			if (pattern.contains("slash") || pattern.contains("Slash")) {
				Card *slash = Sanguosha->cloneCard("slash");
				slash->setSkillName("jibing");
				slash->addSubcard(originalCard);
				return slash;
			} else if (pattern == "jink") {
				Card *jink = Sanguosha->cloneCard("jink");
				jink->setSkillName("jibing");
				jink->addSubcard(originalCard);
				return jink;
			}
			break;
		}default:
			break;
		}
		return nullptr;
	}
};

class Jibing : public PhaseChangeSkill
{
public:
	Jibing() : PhaseChangeSkill("jibing")
	{
		view_as_skill = new JibingVS;
	}

	static int getKingdoms(const ServerPlayer *player)
	{
		QStringList kingdoms;
		foreach(ServerPlayer *p, player->getRoom()->getAlivePlayers()) {
			QString kingdom = p->getKingdom();
			if (kingdoms.contains(kingdom)) continue;
			kingdoms << kingdom;
		}
		return kingdoms.length();
	}

	bool onPhaseChange(ServerPlayer *target, Room *room) const
	{
		if (target->getPhase() != Player::Draw) return false;
		if (target->getPile("jbbing").length() >= getKingdoms(target)) return false;
		if (!target->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(this);
		QList<int> ids = room->getNCards(2, false);
		target->addToPile("jbbing", ids);
		return true;
	}
};

class Wangjing : public TriggerSkill
{
public:
	Wangjing() : TriggerSkill("wangjing")
	{
		events << CardUsed << CardResponded;
		frequency = Compulsory;
	}

	bool isHighHpPlayer(ServerPlayer *player) const
	{
		int hp = player->getHp();
		Room *room = player->getRoom();
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getHp() > hp)
				return false;
		}
		return true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardResponded) {
			CardResponseStruct res = data.value<CardResponseStruct>();
			if (!res.m_card->getSkillNames().contains("jibing") || !res.m_who || res.m_who->isDead() || res.m_card->isKindOf("SkillCard")) return false;
			if (!isHighHpPlayer(res.m_who)) return false;
			room->sendCompulsoryTriggerLog(player, this);
			player->drawCards(1, objectName());
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			if (!use.card->hasFlag("jibing_slash") && !use.card->getSkillNames().contains("jibing")) return false;
			foreach (ServerPlayer *p, use.to) {
				if (player->isDead()) return false;
				if (p->isDead() || !isHighHpPlayer(p)) continue;
				room->sendCompulsoryTriggerLog(player, this);
				player->drawCards(1, objectName());
			}
		}
		return false;
	}
};

class Moucuan : public PhaseChangeSkill
{
public:
	Moucuan() : PhaseChangeSkill("moucuan")
	{
		frequency = Wake;
		waked_skills = "binghuo";
	}

	bool triggerable(const ServerPlayer *player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer *target, Room *room) const
	{
		if (target->getPile("jbbing").length() >= Jibing::getKingdoms(target)){
			LogMessage log;
			log.type = "#ZaoxianWake";
			log.from = target;
			log.arg = QString::number(target->getPile("jbbing").length());
			log.arg2 = objectName();
			log.arg3 = "jbbing";
			room->sendLog(log);
		}else if(!target->canWake(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(target, objectName());
		room->doSuperLightbox(target, "moucuan");
		room->setPlayerMark(target, "moucuan", 1);
		if (room->changeMaxHpForAwakenSkill(target, -1, objectName()))
			room->acquireSkill(target, "binghuo");
		return false;
	}
};

class Binghuo : public TriggerSkill
{
public:
	Binghuo() : TriggerSkill("binghuo")
	{
		events << PreCardUsed << PreCardResponded << EventPhaseStart;
		global = true;//
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == PreCardResponded) {
			CardResponseStruct res = data.value<CardResponseStruct>();
			if (res.m_card->getSkillNames().contains("jibing"))
				player->addMark("jibing_used-Clear");
			if (res.m_who&&res.m_toCard)
				res.m_who->addMark("tenyearsigong_xiangying-Clear");
			if (res.m_card->getSkillNames().contains("yizan"))
				player->addMark("tenyearqingren_yizan-Clear");
			if (res.m_isUse&&player->getPhase()<=Player::Play){
				player->addMark("jingce-Clear");
			}
			if (player->getPhase() == Player::Play)
				player->addMark("dev_zhuaji_use-Clear");
		} else if (event == PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard")) return false;
			if (use.card->getSkillNames().contains("jibing"))
				player->addMark("jibing_used-Clear");
			if (use.card->getSkillNames().contains("yizan"))
				player->addMark("tenyearqingren_yizan-Clear");
			if (use.whocard&&use.who)
				use.who->addMark("tenyearsigong_xiangying-Clear");
			foreach(ServerPlayer *p, use.to)
				p->addMark("secondsouying_num_" + use.from->objectName() + p->objectName() + "-Clear");
			if (use.card->isKindOf("Analeptic"))
				player->addMark("mtchuanjiu_Analeptic-Clear");
			if (use.card->getHandlingMethod() == Card::MethodUse) {
				int n = player->getMark("xingwu");
				if (use.card->isBlack())
					n |= 1;
				else if (use.card->isRed())
					n |= 2;
				player->setMark("xingwu", n);
			}
			if (player->getPhase() <= Player::Play) 
				player->addMark("jingce-Clear");
			if (player->getPhase() == Player::Play){
				player->addMark("dev_zhuaji_use-Clear");
				player->addMark("secondmobilexinzifu-PlayClear");
				if(player->getMark("fengporec-PlayClear")<1){
					player->removeTag("fengpoaddDamage" + use.card->toString());
					room->setCardFlag(use.card, "fengporecc");
					player->addMark("fengporec-PlayClear");
				}
			}
			if (use.card->isKindOf("TrickCard"))
				player->addMark("olcangzhuo_usedtrick-Clear");
			if (use.card->isKindOf("Slash")){
				if(player->getPhase() == Player::Play){
					room->setPlayerFlag(player, "ForbidFuluan");
					foreach(ServerPlayer *p, use.to)
						room->addPlayerMark(p,"chixin-PlayClear");
				}
				if (!room->getTag("XinghanRecord").toBool()) {
					room->setCardFlag(use.card, "xinghan_first_slash");
					room->setTag("XinghanRecord", true);
				}
			}
			player->addMark("tenyearjingce-Clear");
			player->addMark("mobilerentianyin_" + use.card->getType() + "-Clear");
			QString s = use.card->getSuitString();
			if (s == "no_suit_black" || s == "no_suit_red")
				s = "no_suit";
			player->setMark("secondtenyearjingce" + s + "-Clear", 1);
			if (player->getMark("secondtenyearlihuo-Clear")<1){
				room->setCardFlag(use.card, "first_card_in_one_turn");
				player->addMark("secondtenyearlihuo-Clear");
			}
		} else {
			if (player->getPhase() == Player::Finish){
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					if (p->getMark("jibing_used-Clear")<1||!p->hasSkill(objectName())) continue;
					ServerPlayer *target = room->askForPlayerChosen(p, room->getAlivePlayers(), objectName(), "@binghuo-invoke", true, true);
					if (!target) continue;
					p->peiyin(this);
					JudgeStruct judge;
					judge.who = target;
					judge.reason = objectName();
					judge.play_animation = true;
					judge.pattern = ".|black";
					judge.good = false;
					judge.negative = true;
					room->judge(judge);
					if (judge.isBad())
						room->damage(DamageStruct("binghuo", p, target, 1, DamageStruct::Thunder));
				}
			}else if (player->getPhase() == Player::NotActive){
				room->setTag("XinghanRecord", false);
			}else if(player->getPhase() == Player::RoundStart){
				player->addMark("tenyearjinjieTurn_lun");
				foreach (ServerPlayer *p, room->getAlivePlayers())
				p->setMark("mtzhongyi_hp-Keep", p->getHp());
			}else if (player->getPhase() == Player::Start){
				if (player->property("second_mobilexin_wangling_bei").isNull())
					player->addMark("secondmobilexinmibei-Clear");
			}
		}
		return false;
	}
};

class Renshi : public TriggerSkill
{
public:
	Renshi(const QString &renshi) : TriggerSkill(renshi), renshi(renshi)
	{
		events << DamageInflicted;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (!damage.card || !damage.card->isKindOf("Slash") || damage.damage <= 0) return false;
		if (!player->isWounded()) return false;
		LogMessage log;
		log.type = "#RenshiPrevent";
		log.from = player;
		log.to << damage.from;
		log.arg = objectName();
		log.arg2 = QString::number(damage.damage);
		room->sendLog(log);
		player->peiyin(this);
		room->notifySkillInvoked(player, objectName());

		if (objectName() == "renshi") {
			if (room->CardInTable(damage.card))
				player->obtainCard(damage.card, true);
		} else if (objectName() == "tenyeardeshi") {
			QList<int> slashs;
			foreach (int id, room->getDrawPile()) {
				if (Sanguosha->getCard(id)->isKindOf("Slash"))
					slashs << id;
			}
			if (!slashs.isEmpty()) {
				int slash = slashs.at(qrand() % slashs.length());
				room->obtainCard(player, slash);
			}
		}

		room->loseMaxHp(player, 1, objectName());
		return true;
	}
private:
	QString renshi;
};

class Huaizi : public MaxCardsSkill
{
public:
	Huaizi() : MaxCardsSkill("huaizi")
	{
	}

	int getFixed(const Player *target) const
	{
		if (target->hasSkill("huaizi"))
			return target->getMaxHp();
		return -1;
	}
};

WuyuanCard::WuyuanCard(const QString &wuyuan) : wuyuan(wuyuan)
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

void WuyuanCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();

	const Card *card = Sanguosha->getCard(subcards.first());
	bool red = card->isRed() ? true : false;
	bool nature = card->isKindOf("NatureSlash") ? true : false;

	CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), wuyuan, "");
	room->obtainCard(effect.to, this, reason, true);

	room->recover(effect.from, RecoverStruct(wuyuan, effect.from));

	QList<ServerPlayer *> drawers;
	drawers << effect.to;
	if (wuyuan == "tenyearwuyuan")
		drawers << effect.from;
	room->sortByActionOrder(drawers);

	QList<int> draw_num;
	foreach (ServerPlayer *p, drawers) {
		if (p == effect.from)
			draw_num << 1;
		else {
			int n = 1;
			if (nature)
				n = 2;
			draw_num << n;
		}
	}

	room->drawCards(drawers, draw_num, wuyuan);

	if (red)
		room->recover(effect.to, RecoverStruct(wuyuan, effect.from));
}

TenyearWuyuanCard::TenyearWuyuanCard() : WuyuanCard("tenyearwuyuan")
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

class Wuyuan : public OneCardViewAsSkill
{
public:
	Wuyuan(const QString &wuyuan) : OneCardViewAsSkill(wuyuan), wuyuan(wuyuan)
	{
		filter_pattern = "Slash";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		QString classname = wuyuan == "wuyuan" ? "WuyuanCard" : "TenyearWuyuanCard";
		return !player->hasUsed(classname);
	}

	const Card *viewAs(const Card *originalCard) const
	{
		if (wuyuan == "wuyuan") {
			WuyuanCard *card = new WuyuanCard;
			card->addSubcard(originalCard);
			return card;
		} else {
			TenyearWuyuanCard *card = new TenyearWuyuanCard;
			card->addSubcard(originalCard);
			return card;
		}

		return nullptr;
	}
private:
	QString wuyuan;
};

class NewTunchu : public DrawCardsSkill
{
public:
	NewTunchu() : DrawCardsSkill("newtunchu")
	{
	}

	int getDrawNum(ServerPlayer *player, int n) const
	{
		if (player->getPile("food").isEmpty() && player->askForSkillInvoke("newtunchu")) {
			player->getRoom()->broadcastSkillInvoke("newtunchu");
			player->setFlags("newtunchu");
			return n + 2;
		}
		return n;
	}
};

class NewTunchuPut : public TriggerSkill
{
public:
	NewTunchuPut() : TriggerSkill("#newtunchu-put")
	{
		events << AfterDrawNCards;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive() && target->hasFlag("newtunchu");
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DrawStruct draw = data.value<DrawStruct>();
		if (draw.reason=="draw_phase"&&player->hasFlag("newtunchu")) {
			player->setFlags("-newtunchu");
			const Card *c = room->askForExchange(player, "newtunchu", 999, 1, false, "@newtunchu-put", true);
			if (c != nullptr) player->addToPile("food", c);
		}
		return false;
	}
};

class NewTunchuLimit : public CardLimitSkill
{
public:
	NewTunchuLimit() : CardLimitSkill("#newtunchu-limit")
	{
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target) const
	{
		if (target->getPile("food").length()>0&&target->hasSkill("newtunchu"))
			return "Slash";
		return "";
	}
};

NewShuliangCard::NewShuliangCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void NewShuliangCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	CardMoveReason r(CardMoveReason::S_REASON_REMOVE_FROM_PILE, source->objectName(), "newshuliang", "");
	room->moveCardTo(this, nullptr, Player::DiscardPile, r, true);
}

class NewShuliangVS : public OneCardViewAsSkill
{
public:
	NewShuliangVS() : OneCardViewAsSkill("newshuliang")
	{
		response_pattern = "@@newshuliang";
		filter_pattern = ".|.|.|food";
		expand_pile = "food";
	}

	const Card *viewAs(const Card *originalCard) const
	{
		NewShuliangCard *c = new NewShuliangCard;
		c->addSubcard(originalCard);
		return c;
	}
};

class NewShuliang : public PhaseChangeSkill
{
public:
	NewShuliang() : PhaseChangeSkill("newshuliang")
	{
		view_as_skill = new NewShuliangVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive() && target->getPhase() == Player::Finish && target->getHandcardNum() < target->getHp();
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (player->isDead()) return false;
			if (p->isDead() || !p->hasSkill(objectName()) || p->getPile("food").isEmpty()) continue;
			if (!room->askForUseCard(p, "@@newshuliang", "@newshuliang:" + player->objectName(), -1, Card::MethodNone)) continue;
			player->drawCards(2, objectName());
		}
		return false;
	}
};

YizanCard::YizanCard()
{
	target_fixed = true;
	will_throw = false;
	mute = true;
	handling_method = Card::MethodNone;
}

bool YizanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->addSubcards(subcards);
		card->setSkillName("yizan");
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}

	const Card *dc = Self->getTag("yizan").value<const Card *>();
	return dc && dc->targetFilter(targets, to_select, Self);
}

bool YizanCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetFixed();
	}

	const Card *dc = Self->getTag("yizan").value<const Card *>();
	return dc && dc->targetFixed();
}

bool YizanCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->addSubcards(subcards);
		card->setSkillName("yizan");
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}

	const Card *dc = Self->getTag("yizan").value<const Card *>();
	return dc && dc->targetsFeasible(targets, Self);
}

const Card *YizanCard::validate(CardUseStruct &card_use) const
{
	ServerPlayer *player = card_use.from;
	Room *room = player->getRoom();

	QString to_yizan = user_string;
	if ((user_string.contains("slash") || user_string.contains("Slash")) && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		QStringList guhuo_list = Sanguosha->getSlashNames();
		if (guhuo_list.isEmpty())
			guhuo_list << "slash";
		to_yizan = room->askForChoice(player, "yizan_slash", guhuo_list.join("+"));
	}

	Card *use_card = Sanguosha->cloneCard(to_yizan);
	use_card->addSubcards(getSubcards());
	use_card->setSkillName("yizan");
	use_card->deleteLater();
	return use_card;
}

const Card *YizanCard::validateInResponse(ServerPlayer *player) const
{
	Room *room = player->getRoom();

	QString to_yizan = user_string;
	if (user_string == "peach+analeptic") {
		QStringList guhuo_list;
		guhuo_list << "peach";
		if (Sanguosha->hasCard("analeptic")) guhuo_list << "analeptic";
		to_yizan = room->askForChoice(player, "yizan_saveself", guhuo_list.join("+"));
	} else if (user_string.contains("slash") || user_string.contains("Slash")) {
		QStringList guhuo_list = Sanguosha->getSlashNames();
		if (guhuo_list.isEmpty()) guhuo_list << "slash";
		to_yizan = room->askForChoice(player, "yizan_slash", guhuo_list.join("+"));
	}

	Card *use_card = Sanguosha->cloneCard(to_yizan);
	use_card->addSubcards(getSubcards());
	use_card->setSkillName("yizan");
	use_card->deleteLater();
	return use_card;
}

class YizanVS : public ViewAsSkill
{
public:
	YizanVS() : ViewAsSkill("yizan")
	{
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return true;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if (pattern.startsWith(".") || pattern.startsWith("@")) return false;
		if (pattern == "peach" && player->getMark("Global_PreventPeach") > 0) return false;
		foreach (QString name, pattern.split("+")) {
			Card *card = Sanguosha->cloneCard(name.toLower());
			if (!card) continue;
			card->deleteLater();
			if (card->isKindOf("BasicCard"))
				return true;
		}
		foreach (QString name, pattern.split(",")) {
			Card *card = Sanguosha->cloneCard(name.toLower());
			if (!card) continue;
			card->deleteLater();
			if (card->isKindOf("BasicCard"))
				return true;
		}
		return false;
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		int level = Self->property("yizan_level").toInt();
		if (level < 1) {
			level = selected.length();
			if (level<1) return true;
			else if (level >= 2) return false;
			else if (selected.first()->isKindOf("BasicCard"))
				return true;
			return to_select->isKindOf("BasicCard");
		}
		return selected.isEmpty() && to_select->isKindOf("BasicCard");
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		int level = Self->property("yizan_level").toInt();
		if (level < 1) level = 2;
		if (cards.length() != level) return nullptr;
		if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE
			|| Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
			YizanCard *card = new YizanCard;
			card->setUserString(Sanguosha->getCurrentCardUsePattern());
			card->addSubcards(cards);
			return card;
		}

		const Card *c = Self->getTag("yizan").value<const Card *>();
		if (c && c->isAvailable(Self)) {
			YizanCard *card = new YizanCard;
			card->setUserString(c->objectName());
			card->addSubcards(cards);
			return card;
		}
		return nullptr;
	}
};

class Yizan : public TriggerSkill
{
public:
	Yizan() : TriggerSkill("yizan")
	{
		events << PreCardResponded << PreCardUsed;
		view_as_skill = new YizanVS;
	}

	QDialog *getDialog() const
	{
		return GuhuoDialog::getInstance(objectName(), true, false);
	}

	int getPriority(TriggerEvent) const
	{
		return 5;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (player->getMark("longyuan") > 0 || player->getMark("tenyearlongyuan") > 0) return false;
		const Card *card = nullptr;
		if (event == PreCardResponded)
			card = data.value<CardResponseStruct>().m_card;
		else
			card = data.value<CardUseStruct>().card;
		if (card == nullptr || card->isKindOf("SkillCard") || !card->getSkillNames().contains("yizan")) return false;
		room->addPlayerMark(player, "&yizan");
		return false;
	}
};

class Longyuan : public PhaseChangeSkill
{
public:
	Longyuan() : PhaseChangeSkill("longyuan")
	{
		frequency = Wake;
	}

	bool triggerable(const ServerPlayer *player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getMark("&yizan")>2){
			LogMessage log;
			log.type = "#LongyuanWake";
			log.from = player;
			log.arg = QString::number(player->getMark("&yizan"));
			log.arg2 = objectName();
			room->sendLog(log);
		}else if(!player->canWake(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());
		room->doSuperLightbox(player, "longyuan");
		room->setPlayerMark(player, "longyuan", 1);
		room->setPlayerProperty(player, "yizan_level", 1);
		if (room->changeMaxHpForAwakenSkill(player, 0, objectName())) {
			QString translate = Sanguosha->translate(":yizan2");
			room->changeTranslation(player, "yizan", translate);
		}
		room->setPlayerMark(player, "&yizan", 0);
		return false;
	}
};

class Qianchong : public TriggerSkill
{
public:
	Qianchong() : TriggerSkill("qianchong")
	{
		events << CardsMoveOneTime << EventPhaseStart << EventAcquireSkill;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Play) return false;
			if (QianchongJudge(player, "black") || QianchongJudge(player, "red")) return false;
			QString choice = room->askForChoice(player, objectName(), "basic+trick+equip");
			LogMessage log;
			log.type = "#QianchongChoice";
			log.from = player;
			log.arg = objectName();
			log.arg2 = choice;
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName(), 3);
			room->notifySkillInvoked(player, objectName());
			int n = 3;
			if (choice == "basic")
				n = 1;
			else if (choice == "trick")
				n = 2;
			room->addPlayerMark(player, "qianchong-Clear", n);
		} else {
			bool flag = false;
			if (event == CardsMoveOneTime) {
				CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
				if (move.from == player && move.from_places.contains(Player::PlaceEquip))
					flag = true;
				if (move.to == player && move.to_place == Player::PlaceEquip)
					flag = true;
			} else
				flag = data.toString() == objectName();
			if (flag) {
				int index = 1;
				QStringList skills;
				QString skill = player->property("qianchong_skill").toString();
				if(QianchongJudge(player, "black")){
					if(!player->hasSkill("weimu", true) && skill.isEmpty()){
						room->setPlayerProperty(player, "qianchong_skill", "weimu");
						skills << "weimu";
					}
				}else if(player->hasSkill("weimu", true) && skill == "weimu"){
					room->setPlayerProperty(player, "qianchong_skill", "");
					skills << "-weimu";
				}
				if(QianchongJudge(player, "red")){
					if(!player->hasSkill("mingzhe", true) && skill.isEmpty()){
						room->setPlayerProperty(player, "qianchong_skill", "mingzhe");
						skills << "mingzhe";
						index = 2;
					}
				}else if(player->hasSkill("mingzhe", true) && skill == "mingzhe"){
					room->setPlayerProperty(player, "qianchong_skill", "");
					skills << "-mingzhe";
					index = 2;
				}
				if (!skills.isEmpty()) {
					room->sendCompulsoryTriggerLog(player, objectName(), true, true, index);
					room->handleAcquireDetachSkills(player, skills);
				}
			}
		}
		return false;
	}
private:
	bool QianchongJudge(ServerPlayer *player, const QString &type) const
	{
		QList<const Card *>equips = player->getEquips();
		if (equips.isEmpty()) return false;
		if (type == "red") {
			foreach (const Card *c, equips) {
				if (!c->isRed())
					return false;
			}
		} else if (type == "black") {
			foreach (const Card *c, equips) {
				if (!c->isBlack())
					return false;
			}
		}
		return true;
	}
};

class QianchongLose : public TriggerSkill
{
public:
	QianchongLose() : TriggerSkill("#qianchong-lose")
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
		if (data.toString() != "qianchong") return false;
		QString skill = player->property("qianchong_skill").toString();
		room->setPlayerProperty(player, "qianchong_skill", "");
		if (skill == "") return false;
		if (!player->hasSkill(skill)) return false;
		room->handleAcquireDetachSkills(player, "-" + skill);
		return false;
	}
};

class QianchongTargetMod : public TargetModSkill
{
public:
	QianchongTargetMod() : TargetModSkill("#qianchong-target")
	{
		pattern = ".";
	}

	int getResidueNum(const Player *from, const Card *card, const Player *) const
	{
		if (card->getTypeId() == from->getMark("qianchong-Clear"))
			return 999;
		return 0;
	}

	int getDistanceLimit(const Player *from, const Card *card, const Player *) const
	{
		if (card->getTypeId() == from->getMark("qianchong-Clear"))
			return 999;
		if (card->hasTip("bswanglie"))
			return 999;
		if (from->getPhase()==Player::Play&&from->getMark("&zhuangshi+1-PlayClear")>0)
			return 999;
		if (from->getPile("bstun_tian").contains(card->getId()))
			return 999;
		if (card->isDamageCard()&&from->hasSkill("chizhang"))
			return 999;
		return 0;
	}
};

class Shangjian : public PhaseChangeSkill
{
public:
	Shangjian() : PhaseChangeSkill("shangjian")
	{
		frequency = Frequent;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
	if (player->getPhase() != Player::Finish) return false;
	foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
		int n = p->getMark("shangjian-Clear");
		if (p->isDead() || n > p->getHp() || n <= 0 || !p->hasSkill(objectName())) continue;
		if (!p->askForSkillInvoke(this)) continue;
		room->broadcastSkillInvoke(objectName());
		p->drawCards(n, objectName());
	}
	return false;
	}
};

HongyiCard::HongyiCard()
{
	handling_method = Card::MethodDiscard;
}

void HongyiCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	QStringList names = effect.from->property("hongyi_targets").toStringList();
	names << effect.to->objectName();
	room->setPlayerProperty(effect.from, "hongyi_targets", names);
	room->addPlayerMark(effect.to, "&hongyi");
}

class HongyiVS : public ViewAsSkill
{
public:
	HongyiVS() : ViewAsSkill("hongyi")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		int n = 0;
		foreach (const Player *p, Self->getSiblings()) {
			if (p->isDead()) {
				n++;
				if (n >= 2)
					break;
			}
		}
		if (n == 0) return false;
		return !Self->isJilei(to_select) && selected.length() < n;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		int n = 0;
		foreach (const Player *p, Self->getSiblings()) {
			if (p->isDead()) {
				n++;
				if (n >= 2)
					break;
			}
		}
		if (cards.length() != n) return nullptr;

		HongyiCard *c = new HongyiCard;
		if (!cards.isEmpty())
			c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("HongyiCard");
	}
};

class Hongyi : public TriggerSkill
{
public:
	Hongyi() : TriggerSkill("hongyi")
	{
		events << DamageCaused << EventPhaseStart << Death << EventLoseSkill;
		view_as_skill = new HongyiVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	int getPriority(TriggerEvent event) const
	{
		if (event == EventPhaseStart)
			return 5;
		return TriggerSkill::getPriority(event);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == DamageCaused) {
			if (player->getMark("&hongyi") <= 0) return false;
			DamageStruct damage = data.value<DamageStruct>();
			int n = 0;
			for (int i = 1; i <= player->getMark("&hongyi"); i++) {
				LogMessage log;
				log.type = "#ZhenguEffect";
				log.from = player;
				log.arg = objectName();
				room->sendLog(log);

				JudgeStruct judge;
				judge.who = player;
				judge.reason = objectName();
				judge.pattern = ".";
				judge.play_animation = false;
				room->judge(judge);

				if (judge.card->getColor() == Card::Red)
					room->drawCards(damage.to, 1, objectName());
				else if (judge.card->getColor() == Card::Black)
					n--;
				if (player->isDead()) break;
			}
			if (n < 0) return player->damageRevises(data,n);
		} else{
			if (event == Death) {
				DeathStruct death = data.value<DeathStruct>();
				if (player != death.who) return false;
			} else if (event == EventLoseSkill) {
				if (data.toString() != objectName()) return false;
			}else if (event == EventPhaseStart) {
				if (player->getPhase() != Player::RoundStart) return false;
			}
			QStringList names = player->property("hongyi_targets").toStringList();
			if (names.isEmpty()) return false;
			room->setPlayerProperty(player, "hongyi_targets", QStringList());
			foreach (QString name, names) {
				ServerPlayer *p = room->findChild<ServerPlayer *>(name);
				if (p) room->removePlayerMark(p, "&hongyi");
			}
		}
		return false;
	}
};

class Quanfeng : public TriggerSkill
{
public:
	Quanfeng() : TriggerSkill("quanfeng")
	{
		frequency = Compulsory;
		limited_skill = true;
		limit_mark = "@quanfengMark";
		events << Death;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (player->getMark(getLimitMark()) <= 0) return false;
		room->sendCompulsoryTriggerLog(player, objectName(), true, true);
		room->doSuperLightbox(player, objectName());
		room->removePlayerMark(player, getLimitMark());

		DeathStruct death = data.value<DeathStruct>();
		QStringList gets;
		foreach (const Skill *skill, death.who->getSkillList()) {
			if (skill->isLimitedSkill()) continue;
			if (skill->getFrequency() == Skill::Wake) continue;
			if (skill->isLordSkill() || skill->isAttachedLordSkill()) continue;
			if (gets.contains(skill->objectName())) continue;
			gets << skill->objectName();
		}
		if (!gets.isEmpty()) {
			QString skill = room->askForChoice(player, objectName(), gets.join("+"));
			room->acquireSkill(player, skill);
		}
		room->gainMaxHp(player, 1, objectName());
		room->recover(player, RecoverStruct("quanfeng", player));
		return false;
	}
};

SecondHongyiCard::SecondHongyiCard()
{
}

void SecondHongyiCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	QStringList names = effect.from->property("secondhongyi_targets").toStringList();
	names << effect.to->objectName();
	room->setPlayerProperty(effect.from, "secondhongyi_targets", names);
	room->addPlayerMark(effect.to, "&secondhongyi");
}

class SecondHongyiVS : public ZeroCardViewAsSkill
{
public:
	SecondHongyiVS() : ZeroCardViewAsSkill("secondhongyi")
	{
	}

	const Card *viewAs() const
	{
		return new SecondHongyiCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("SecondHongyiCard");
	}
};

class SecondHongyi : public TriggerSkill
{
public:
	SecondHongyi() : TriggerSkill("secondhongyi")
	{
		events << DamageCaused << EventPhaseStart << Death << EventLoseSkill;
		view_as_skill = new SecondHongyiVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	int getPriority(TriggerEvent event) const
	{
		if (event == EventPhaseStart)
			return 5;
		return TriggerSkill::getPriority(event);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::RoundStart) return false;
			QStringList names = player->property("secondhongyi_targets").toStringList();
			if (names.isEmpty()) return false;
			room->setPlayerProperty(player, "secondhongyi_targets", QStringList());
			foreach (QString name, names) {
				ServerPlayer *p = room->findChild<ServerPlayer *>(name);
				if (p) room->removePlayerMark(p, "&secondhongyi");
			}
		} else if (event == DamageCaused) {
			if (player->isDead() || player->getMark("&secondhongyi") <= 0) return false;
			DamageStruct damage = data.value<DamageStruct>();
			int n = 0;
			for (int i = 1; i <= player->getMark("&secondhongyi"); i++) {
				if (player->isDead()) break;
				LogMessage log;
				log.type = "#ZhenguEffect";
				log.from = player;
				log.arg = objectName();
				room->sendLog(log);

				JudgeStruct judge;
				judge.who = player;
				judge.reason = objectName();
				judge.pattern = ".";
				judge.play_animation = false;
				room->judge(judge);

				if (judge.card->getColor() == Card::Red)
					room->drawCards(damage.to, 1, objectName());
				else if (judge.card->getColor() == Card::Black)
					n--;
			}
			if (n<0) player->damageRevises(data,n);
		} else if (event == Death) {
			DeathStruct death = data.value<DeathStruct>();
			if (player != death.who) return false;
			QStringList names = player->property("secondhongyi_targets").toStringList();
			if (names.isEmpty()) return false;
			room->setPlayerProperty(player, "secondhongyi_targets", QStringList());
			foreach (QString name, names) {
				ServerPlayer *p = room->findChild<ServerPlayer *>(name);
				if (p) room->removePlayerMark(p, "&secondhongyi");
			}
		} else if (event == EventLoseSkill) {
			if (data.toString() != objectName()) return false;
			QStringList names = player->property("secondhongyi_targets").toStringList();
			if (names.isEmpty()) return false;
			room->setPlayerProperty(player, "secondhongyi_targets", QStringList());
			foreach (QString name, names) {
				ServerPlayer *p = room->findChild<ServerPlayer *>(name);
				if (p) room->removePlayerMark(p, "&secondhongyi");
			}
		}
		return false;
	}
};

class SecondQuanfeng : public TriggerSkill
{
public:
	SecondQuanfeng() : TriggerSkill("secondquanfeng")
	{
		events << Death << AskForPeaches;
		frequency = Limited;
		limit_mark = "@secondquanfengMark";
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (player->getMark("@secondquanfengMark") <= 0) return false;
		if (event == Death) {
			DeathStruct death = data.value<DeathStruct>();
			if (!player->askForSkillInvoke(this, death.who)) return false;
			room->broadcastSkillInvoke(objectName());
			room->doSuperLightbox(player, "secondquanfeng");
			room->removePlayerMark(player, "@secondquanfengMark");
			room->handleAcquireDetachSkills(player, "-secondhongyi");

			QStringList skills;
			const General *general = Sanguosha->getGeneral(death.who->getGeneralName());
			foreach (const Skill *sk, general->getSkillList()) {
				if (!sk->isVisible() || sk->isLordSkill()) continue;
				if (skills.contains(sk->objectName())) continue;
				skills << sk->objectName();
			}
			if (death.who->getGeneral2()) {
				const General *general2 = Sanguosha->getGeneral(death.who->getGeneral2Name());
				foreach (const Skill *sk, general2->getSkillList()) {
					if (!sk->isVisible() || sk->isLordSkill()) continue;
					if (skills.contains(sk->objectName())) continue;
					skills << sk->objectName();
				}
			}
			if (!skills.isEmpty())
				room->handleAcquireDetachSkills(player, skills);
			room->gainMaxHp(player, 1, objectName());
			room->recover(player, RecoverStruct("secondquanfeng", player));
		} else {
			DyingStruct dying = data.value<DyingStruct>();
			if (dying.who != player) return false;
			if (!player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			room->doSuperLightbox(player, "secondquanfeng");
			room->removePlayerMark(player, "@secondquanfengMark");
			room->gainMaxHp(player, 2, objectName());
			int num = qMin(4, player->getMaxHp() - player->getHp());
			room->recover(player, RecoverStruct(player, nullptr, num, "secondquanfeng"));
		}
		return false;
	}
};

class Polu : public TriggerSkill
{
public:
	Polu(const QString &polu) : TriggerSkill(polu), polu(polu)
	{
		events << EventPhaseStart << Damaged;
		frequency = Compulsory;
		if (polu == "secondpolu")
			waked_skills = "_secondpiliche";
		else
			waked_skills = "_piliche";
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		QString piliche = "_piliche";
		if (polu == "secondpolu")
			piliche = "_secondpiliche";

		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::RoundStart) return false;

			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				foreach (const Card *c, p->getCards("ej")) {
					const Card *card = Sanguosha->getEngineCard(c->getEffectiveId());
					if (card->objectName() == piliche)
						return false;
				}
			}

			if (polu == "secondpolu") {
				foreach (int id, room->getDrawPile() + room->getDiscardPile()) {
					const Card *card = Sanguosha->getEngineCard(id);
					if (card->objectName() == piliche)
						return false;
				}
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					foreach (const Card *c, p->getCards("h")) {
						const Card *card = Sanguosha->getEngineCard(c->getEffectiveId());
						if (card->objectName() == piliche)
							return false;
					}
				}
			}

			int id = player->getDerivativeCard(piliche, Player::PlaceTable);
			if (id < 0) {
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					QStringList piles = p->getPileNames();
					piles.removeOne("wooden_ox");
					foreach (const QString &pile, piles) {
						foreach (int pile_id, p->getPile(pile)) {
							const Card *card = Sanguosha->getEngineCard(pile_id);
							if (card->objectName() == piliche) {
								id = pile_id;
								break;
							}
						}
						if (id > 0)
							break;
					}
					if (id > 0)
						break;
				}
			}
			if (id < 0) return false;

			const Card *card = Sanguosha->getCard(id);

			if (polu == "polu") {
				if (!card->isAvailable(player)) return false;
				room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				room->useCard(CardUseStruct(card, player));
			} else {
				room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				bool in_table = room->getCardPlace(id) == Player::PlaceTable;
				CardMoveReason reason(CardMoveReason::S_REASON_EXCLUSIVE, player->objectName());
				room->obtainCard(player, card, in_table ? reason : CardMoveReason(), false);
				if (player->isDead() || !card->isAvailable(player)) return false;
				room->useCard(CardUseStruct(card, player));
			}
		} else {
			if (player->getWeapon() && player->getWeapon()->objectName() == piliche) return false;
			int damage = data.value<DamageStruct>().damage;
			for (int i = 0; i < damage; i++) {
				if (player->isDead() || !player->hasSkill(objectName())) return false;
				room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				player->drawCards(1, objectName());
				if (player->isDead()) return false;

				if (polu == "secondpolu") {
					QList<const Card *> weapons;
					foreach (int id, room->getDrawPile()) {
						const Card *card = Sanguosha->getCard(id);
						if (card->isKindOf("Weapon"))
							weapons << card;
					}
					if (weapons.isEmpty()) continue;
					const Card *weapon = weapons.at(qrand() % weapons.length());
					room->obtainCard(player, weapon, true);
					if (player->isDead()) return false;
					if (weapon->isAvailable(player))
						room->useCard(CardUseStruct(weapon, player));
				}
			}
		}
		return false;
	}

private:
	QString polu;
};

class ChoulveVS : public ZeroCardViewAsSkill
{
public:
	ChoulveVS() :ZeroCardViewAsSkill("choulve")
	{
		response_pattern = "@@choulve!";
	}

	const Card *viewAs() const
	{
		QString name = Self->property("choulve_damage_card").toString();
		if (name.isEmpty()) return nullptr;
		Card *use_card = Sanguosha->cloneCard(name);
		if (!use_card) return nullptr;
		use_card->setSkillName("_choulve");
		return use_card;
	}
};

class Choulve : public PhaseChangeSkill
{
public:
	Choulve() : PhaseChangeSkill("choulve")
	{
		view_as_skill = new ChoulveVS;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Play) return false;
		QString name = player->property("choulve_damage_card").toString();
		Card *use_card = Sanguosha->cloneCard(name);
		if (!use_card) return false;
		QList<ServerPlayer *> tos;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (!p->isNude())
				tos << p;
		}
		if (tos.isEmpty()) return false;
		ServerPlayer *to = room->askForPlayerChosen(player, tos, objectName(), "@choulve-invoke", true, true);
		if (!to) return false;
		room->broadcastSkillInvoke(objectName());
		const Card *card = room->askForExchange(to, objectName(), 1, 1, true, "@choulve-give:" + player->objectName(), true);
		if (!card) return false;
		room->giveCard(to, player, card, objectName());
		use_card->setSkillName("_choulve");
		use_card->deleteLater();
		if (!player->canUse(use_card)) return false;

		if (use_card->targetFixed())
			room->useCard(CardUseStruct(use_card, player), true);
		else {
			if (room->askForUseCard(player, "@@choulve!", "@choulve:" + name)) return false;
			QList<ServerPlayer *> targets;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (player->canUse(use_card, p))
					targets << p;
			}
			if (targets.isEmpty()) return false;
			ServerPlayer *target = targets.at(qrand() % targets.length());
			room->useCard(CardUseStruct(use_card, player, target), true);
		}
		return false;
	}
};

class ChoulveRecord : public TriggerSkill
{
public:
	ChoulveRecord() : TriggerSkill("#choulve-record")
	{
		events << DamageDone << EventLoseSkill;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || damage.card->isKindOf("SkillCard")) return false;
			QString name = damage.card->objectName();
			if (!damage.card->isKindOf("DelayedTrick")) {
				room->setPlayerProperty(player, "choulve_damage_card", name);
	
				foreach (QString mark, player->getMarkNames()) {
					if (mark.startsWith("&choulve+"))
						room->setPlayerMark(player, mark, 0);
				}
	
				if (player->hasSkill("choulve", true))
					room->setPlayerMark(damage.to, "&choulve+" + name, 1);
			}
	
			QList<ServerPlayer *> players;
			players << player;
			if (damage.from && damage.from->isAlive())
				players << damage.from;
	
			foreach (ServerPlayer *p, players) {
				room->setPlayerProperty(p, "yhduwei_damage_card", name);
	
				foreach (QString mark, p->getMarkNames()) {
					if (mark.startsWith("&yhduwei+"))
						room->setPlayerMark(p, mark, 0);
				}
	
				if (p->hasSkill("yhduwei", true))
					room->setPlayerMark(p, "&yhduwei+" + name, 1);
			}
		}else {
			if(data.toString()=="choulve"){
				foreach (QString mark, player->getMarkNames()) {
					if (mark.startsWith("&choulve+"))
						room->setPlayerMark(player, mark, 0);
				}
			}else if(data.toString()=="yhduwei"){
				foreach (QString mark, player->getMarkNames()) {
					if (mark.startsWith("&yhduwei+"))
						room->setPlayerMark(player, mark, 0);
				}
			}
		}
		return false;
	}
};

class Daigong : public TriggerSkill
{
public:
	Daigong() : TriggerSkill("daigong")
	{
		events << DamageInflicted;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (player->isKongcheng()) return false;
		if (!room->hasCurrent() || player->getMark("daigong-Clear") > 0) return false;
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.damage <= 0) return false;
		QVariant d = QVariant();
		if (damage.from && damage.from->isAlive())
			d = QVariant::fromValue(damage.from);
		if (!player->askForSkillInvoke(this, d)) return false;
		room->broadcastSkillInvoke(objectName());
		room->addPlayerMark(player, "daigong-Clear");
		room->showAllCards(player);

		if (!damage.from || damage.from->isDead()) return false;
		QStringList suits;
		foreach (const Card *c, player->getCards("h")) {
			if (!suits.contains(c->getSuitString()))
				suits << c->getSuitString();
		}

		bool has = false;
		foreach (const Card *c, damage.from->getCards("h")) {
			QString str = c->getSuitString();
			if (!suits.contains(str)) {
				has = true;
				break;
			}
		}

		if (!has) {
			LogMessage log;
			log.type = "#Daigong";
			log.from = damage.from;
			log.to << player;
			log.arg = QString::number(damage.damage);
			room->sendLog(log);
			return true;
		} else {
			QStringList all_suits;
			all_suits << "spade" << "club" << "heart" << "diamond" << "no_suit_black" << "no_suit_red" << "no_suit";
			foreach (QString str, suits) {
				all_suits.removeOne(str);
			}
			if (all_suits.isEmpty()) {
				LogMessage log;
				log.type = "#Daigong";
				log.from = damage.from;
				log.to << player;
				log.arg = QString::number(damage.damage);
				room->sendLog(log);
				return true;
			} else {
				QString suitt = all_suits.join(",");
				QString pattern = ".|" + suitt + "|.|.";
				QStringList data_list;
				data_list << player->objectName() << suitt;
				const Card *give = room->askForCard(damage.from, pattern, "daigong-give:" + player->objectName(), data_list, Card::MethodNone);
				if (!give) {
					LogMessage log;
					log.type = "#Daigong";
					log.from = damage.from;
					log.to << player;
					log.arg = QString::number(damage.damage);
					room->sendLog(log);
					return true;
				} else {
					CardMoveReason reason(CardMoveReason::S_REASON_GIVE, damage.from->objectName(), player->objectName(), "daigong", "");
					room->obtainCard(player, give, reason, true);
				}
			}
		}
		return false;
	}
};

SpZhaoxinCard::SpZhaoxinCard()
{
	target_fixed= true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void SpZhaoxinCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	source->addToPile("zxwang", this);
	source->drawCards(getSubcards().length(), "spzhaoxin");
}

SpZhaoxinChooseCard::SpZhaoxinChooseCard()
{
	m_skillName = "spzhaoxin";
	target_fixed= true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

class SpZhaoxinVS : public ViewAsSkill
{
public:
	SpZhaoxinVS() : ViewAsSkill("spzhaoxin")
	{
	expand_pile = "zxwang";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
			return selected.length() < 3 - Self->getPile("zxwang").length() && Self->hasCard(to_select);
		}
		return Self->getPile("zxwang").contains(to_select->getEffectiveId());
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
			if (cards.isEmpty())
				return nullptr;
			SpZhaoxinCard *c = new SpZhaoxinCard;
			c->addSubcards(cards);
			return c;
		}
		if (cards.length() != 1)
			return nullptr;
		SpZhaoxinChooseCard *c = new SpZhaoxinChooseCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("SpZhaoxinCard") && player->getPile("zxwang").length() < 3;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return pattern == "@@spzhaoxin" && !player->getPile("zxwang").isEmpty();
	}
};

class SpZhaoxin : public TriggerSkill
{
public:
	SpZhaoxin() : TriggerSkill("spzhaoxin")
	{
		events << EventPhaseEnd;
		view_as_skill = new SpZhaoxinVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (player->getPhase() != Player::Draw) return false;
		foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
			if (player->isDead() || !p->hasSkill(objectName())) return false;
			if (p->isDead() || p->getPile("zxwang").isEmpty()) continue;
			if (p != player && !p->inMyAttackRange(player)) continue;
			const Card *card = room->askForUseCard(p, "@@spzhaoxin", "@spzhaoxin:" + player->objectName());
			if (!card) continue;
			room->fillAG(card->getSubcards(), player);
			if (!player->askForSkillInvoke(this, QString("spzhaoxin_get:%1::%2").arg(card->getSubcards().first()).arg(p->objectName()), false)) {
				room->clearAG(player);
				continue;
			}
			room->clearAG(player);
			if (p == player) {
				LogMessage log;
				log.type = "$KuangbiGet";
				log.from = player;
				log.arg = "zxwang";
				log.card_str = ListI2S(card->getSubcards()).join("+");
				room->sendLog(log);
			}
			player->obtainCard(card, true);
			if (!p->askForSkillInvoke(this, "spzhaoxin_damage:"+player->objectName(), false)) continue;
			room->damage(DamageStruct("spzhaoxin", p, player));
		}
		return false;
	}
};

SecondZhanyiViewAsBasicCard::SecondZhanyiViewAsBasicCard()
{
	m_skillName = "secondzhanyi";
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool SecondZhanyiViewAsBasicCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("secondzhanyi");
		card->addSubcard(getEffectiveId());
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool SecondZhanyiViewAsBasicCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("secondzhanyi");
		card->addSubcard(getEffectiveId());
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool SecondZhanyiViewAsBasicCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("secondzhanyi");
		card->addSubcard(getEffectiveId());
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

const Card *SecondZhanyiViewAsBasicCard::validate(CardUseStruct &card_use) const
{
	ServerPlayer *zhuling = card_use.from;
	Room *room = zhuling->getRoom();

	QString to_zhanyi = user_string;
	if (user_string == "slash" && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		QStringList guhuo_list;
		guhuo_list << "slash";
		if (!Config.BanPackages.contains("maneuvering"))
			guhuo_list << "normal_slash" << "thunder_slash" << "fire_slash";
		to_zhanyi = room->askForChoice(zhuling, "secondzhanyi_slash", guhuo_list.join("+"));
	}

	if (to_zhanyi == "slash") {
		const Card *card = Sanguosha->getCard(subcards.first());
		if (card->isKindOf("Slash"))
			to_zhanyi = card->objectName();
	} else if (to_zhanyi == "normal_slash")
		to_zhanyi = "slash";
	Card *use_card = Sanguosha->cloneCard(to_zhanyi);
	use_card->setSkillName("secondzhanyi");
	use_card->addSubcard(subcards.first());
	use_card->deleteLater();
	return use_card;
}

const Card *SecondZhanyiViewAsBasicCard::validateInResponse(ServerPlayer *zhuling) const
{
	Room *room = zhuling->getRoom();

	QString to_zhanyi = user_string;
	if (user_string == "peach+analeptic") {
		QStringList guhuo_list;
		guhuo_list << "peach";
		if (!Config.BanPackages.contains("maneuvering"))
			guhuo_list << "analeptic";
		to_zhanyi = room->askForChoice(zhuling, "secondzhanyi_saveself", guhuo_list.join("+"));
	} else if (user_string == "slash") {
		QStringList guhuo_list;
		guhuo_list << "slash";
		if (!Config.BanPackages.contains("maneuvering"))
			guhuo_list << "normal_slash" << "thunder_slash" << "fire_slash";
		to_zhanyi = room->askForChoice(zhuling, "secondzhanyi_slash", guhuo_list.join("+"));
	}
	if (to_zhanyi == "slash") {
		const Card *card = Sanguosha->getCard(subcards.first());
		if (card->isKindOf("Slash"))
			to_zhanyi = card->objectName();
	} else if (to_zhanyi == "normal_slash")
		to_zhanyi = "slash";
	Card *use_card = Sanguosha->cloneCard(to_zhanyi);
	use_card->setSkillName("secondzhanyi");
	use_card->addSubcard(subcards.first());
	use_card->deleteLater();
	return use_card;
}

SecondZhanyiCard::SecondZhanyiCard()
{
	target_fixed = true;
}

void SecondZhanyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->loseHp(HpLostStruct(source, 1, "secondzhanyi", source));
	if (source->isAlive()) {
		const Card *c = Sanguosha->getCard(subcards.first());
		if (c->getTypeId() == Card::TypeBasic) {
			room->setPlayerMark(source, "ViewAsSkill_secondzhanyiEffect", 1);
			room->setPlayerMark(source, "Secondzhanyieffect-PlayClear", 1);
		} else if (c->getTypeId() == Card::TypeEquip)
			room->setPlayerMark(source, "secondzhanyiEquip-PlayClear", 1);
		else if (c->getTypeId() == Card::TypeTrick) {
			source->drawCards(3, "secondzhanyi");
			room->setPlayerMark(source, "secondzhanyiTrick-PlayClear", 1);
		}
	}
}

class SecondZhanyiVS : public OneCardViewAsSkill
{
public:
	SecondZhanyiVS() : OneCardViewAsSkill("secondzhanyi")
	{
	}

	bool isResponseOrUse() const
	{
		return Self->getMark("ViewAsSkill_secondzhanyiEffect") > 0;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if (!player->hasUsed("SecondZhanyiCard"))
			return true;
		return player->getMark("ViewAsSkill_secondzhanyiEffect") > 0;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if (Sanguosha->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
		if (pattern == "peach" && player->getMark("Global_PreventPeach") > 0) return false;
		if (player->getMark("ViewAsSkill_secondzhanyiEffect")<1) return false;
		if (pattern.startsWith(".") || pattern.startsWith("@")) return false;
		foreach (QString pn, pattern.split("+")) {
			Card*dc = Sanguosha->cloneCard(pn);
			if(dc){
				dc->deleteLater();
				if(dc->isKindOf("BasicCard"))
					return true;
			}
		}
		return false;
	}

	bool viewFilter(const Card *to_select) const
	{
		return Self->getMark("ViewAsSkill_secondzhanyiEffect")<1||to_select->isKindOf("BasicCard");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		if (Self->getMark("ViewAsSkill_secondzhanyiEffect") <1) {
			SecondZhanyiCard *zy = new SecondZhanyiCard;
			zy->addSubcard(originalCard);
			return zy;
		}
		QString pattern = Sanguosha->getCurrentCardUsePattern();

		if (pattern.isEmpty()) {
			const Card *c = Self->getTag("secondzhanyi").value<const Card *>();
			if(c) pattern = c->objectName();
			else return nullptr;
		}
		SecondZhanyiViewAsBasicCard *card = new SecondZhanyiViewAsBasicCard;
		card->addSubcard(originalCard);
		card->setUserString(pattern);
		return card;
	}
};

class SecondZhanyi : public TriggerSkill
{
public:
	SecondZhanyi() : TriggerSkill("secondzhanyi")
	{
		events << PreHpRecover << ConfirmDamage << PreCardUsed << EventPhaseChanging
			<< PreCardResponded << TrickCardCanceling << TargetSpecified;
		view_as_skill = new SecondZhanyiVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	QDialog *getDialog() const
	{
		return GuhuoDialog::getInstance("secondzhanyi", true, false);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TrickCardCanceling) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if (!effect.card->isKindOf("TrickCard")) return false;
			return effect.from && effect.from->getMark("secondzhanyiTrick-PlayClear")>0;
		}else if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().from != Player::Play) return false;
			room->setPlayerMark(player, "ViewAsSkill_secondzhanyiEffect", 0);
		} else if (event == PreHpRecover) {
			RecoverStruct recover = data.value<RecoverStruct>();
			if (!recover.card || !recover.card->hasFlag("secondzhanyi_effect")) return false;
			int old = recover.recover;
			++recover.recover;
			int now = qMin(recover.recover, player->getMaxHp() - player->getHp());
			if (now <= 0) return true;
			if (recover.who && now > old) {
				LogMessage log;
				log.type = "#NewlonghunRecover";
				log.from = recover.who;
				log.to << player;
				log.arg = objectName();
				log.arg2 = QString::number(now);
				room->sendLog(log);
			}
			recover.recover = now;
			data = QVariant::fromValue(recover);
		} else if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->hasFlag("secondzhanyi_effect") || !damage.by_user) return false;

			LogMessage log;
			log.type = "#NewlonghunDamage";
			log.from = player;
			log.to << damage.to;
			log.arg = objectName();
			log.arg2 = QString::number(++damage.damage);
			room->sendLog(log);

			data = QVariant::fromValue(damage);
		} else if (event == TargetSpecified) {
			if (player->getMark("secondzhanyiEquip-PlayClear") <= 0) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash") || use.to.isEmpty()) return false;
			room->sendCompulsoryTriggerLog(player, objectName());
			foreach (ServerPlayer *p, use.to) {
				if (p->isDead() || !p->canDiscard(p, "he")) continue;
				const Card *c = room->askForDiscard(p, "secondzhanyi", 2, 2, false, true);
				if (!c || player->isDead()) continue;
				room->fillAG(c->getSubcards(), player);
				int id = room->askForAG(player, c->getSubcards(), false, "secondzhanyi");
				room->clearAG(player);
				room->obtainCard(player, id);
			}
		} else {
			if (player->getMark("Secondzhanyieffect-PlayClear") <= 0) return false;
			const Card *card = nullptr;
			if (event == PreCardUsed)
				card = data.value<CardUseStruct>().card;
			else {
				CardResponseStruct res = data.value<CardResponseStruct>();
				if (!res.m_isUse) return false;
				card = res.m_card;
			}
			if (!card || !card->isKindOf("BasicCard")) return false;
			room->setPlayerMark(player, "Secondzhanyieffect-PlayClear", 0);
			room->setCardFlag(card, "secondzhanyi_effect");
		}
		return false;
	}
};

class Zhaohan : public PhaseChangeSkill
{
public:
	Zhaohan() : PhaseChangeSkill("zhaohan")
	{
		frequency = Compulsory;
		global = true;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Start) return false;
		int n = 1+player->getTag("PlayerStartPhaseNum").toInt();
		player->setTag("PlayerStartPhaseNum", n);
		if(!player->hasSkill(objectName())) return false;
		if (n <= 4) {
			room->sendCompulsoryTriggerLog(player, this, 1);
			room->gainMaxHp(player, 1, objectName());
			room->recover(player, RecoverStruct(objectName(), player));
		} else if (n <= 7) {
			room->sendCompulsoryTriggerLog(player, this, 2);
			room->loseMaxHp(player, 1, objectName());
		}
		return false;
	}
};

class Rangjie : public MasochismSkill
{
public:
	Rangjie() : MasochismSkill("rangjie")
	{
		frequency = Frequent;
	}

	void onDamaged(ServerPlayer *player, const DamageStruct &damage) const
	{
		Room *room = player->getRoom();
		for (int i = 0; i < damage.damage; i++) {
			if (player->isDead() || !player->askForSkillInvoke(this)) return;
			room->broadcastSkillInvoke(objectName());

			QStringList choices;
			if (room->canMoveField("ej"))
				choices << "move";
			choices << "BasicCard" << "TrickCard" << "EquipCard";

			QString choice = room->askForChoice(player, objectName(), choices.join("+"));
			if (choice == "move")
				room->moveField(player, objectName(), false, "ej");
			else {
				QList<int> ids;
				const std::string chStr = choice.toStdString();
				const char *ch = chStr.c_str();
				foreach (int id, room->getDrawPile()) {
					if (Sanguosha->getCard(id)->isKindOf(ch))
						ids << id;
				}
				if (ids.isEmpty()) continue;
				room->obtainCard(player, ids.at(qrand() % ids.length()), true);
			}
			player->drawCards(1, objectName());
		}
	}
};

YizhengCard::YizhengCard()
{
}

bool YizhengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && Self->canPindian(to_select) && to_select->getHp() <= Self->getHp();
}

void YizhengCard::onEffect(CardEffectStruct &effect) const
{
	if (!effect.from->canPindian(effect.to, false)) return;
	Room *room = effect.from->getRoom();
	if (effect.from->pindian(effect.to, "yizheng"))
		room->addPlayerMark(effect.to, "&yizheng");
	else
		room->loseMaxHp(effect.from, 1, "yizheng");
}

class YizhengVS : public ZeroCardViewAsSkill
{
public:
	YizhengVS() : ZeroCardViewAsSkill("yizheng")
	{
	}

	const Card *viewAs() const
	{
		return new YizhengCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("YizhengCard") && player->canPindian();
	}
};

class Yizheng : public TriggerSkill
{
public:
	Yizheng() : TriggerSkill("yizheng")
	{
		events << EventPhaseChanging;
		view_as_skill = new YizhengVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->getMark("&yizheng")>0;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if (change.to != Player::Draw || player->isSkipped(Player::Draw)) return false;
		LogMessage log;
		log.type = "#YizhengEffect";
		log.from = player;
		log.arg = objectName();
		room->sendLog(log);
		room->broadcastSkillInvoke(objectName());
		room->setPlayerMark(player, "&yizheng", 0);
		player->skip(Player::Draw);
		return false;
	}
};

class MobileNiluan : public PhaseChangeSkill
{
public:
	MobileNiluan() : PhaseChangeSkill("mobileniluan")
	{
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->getPhase() == Player::Finish && target->getMark("qieting-Clear") > 0;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (player->isDead()) break;
			if (!p->hasSkill(objectName())) continue;
			if (!p->canSlash(player, false)) continue;

			if (!room->askForUseSlashTo(p,player,"@mobileniluan:"+player->objectName(),false,false,false,nullptr,nullptr,"mobileniluan_slash"))
				continue;
			if (!p->hasFlag("mobileniluan_damage_" + player->objectName())) continue;
			room->setPlayerFlag(p, "-mobileniluan_damage_" + player->objectName());
			if (!p->canDiscard(player, "he")) continue;
			int id = room->askForCardChosen(p, player, "he", objectName(), false, Card::MethodDiscard);
			room->throwCard(id, player, p);
		}
		return false;
	}
};

class MobileNiluanLog : public TriggerSkill
{
public:
	MobileNiluanLog() : TriggerSkill("#mobileniluan")
	{
		events << PreChangeSlash << DamageDone << TargetSpecified;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event==PreChangeSlash) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->hasFlag("mobileniluan_slash")) return false;
			room->broadcastSkillInvoke("mobileniluan");
			room->notifySkillInvoked(player, "mobileniluan");
			LogMessage log;
			log.type = "#InvokeSkill";
			log.from = player;
			log.arg = "mobileniluan";
			room->sendLog(log);
		}else if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId() != Card::TypeSkill) {
				foreach (ServerPlayer *p, use.to) {
					if (p != player)
						player->addMark("qieting-Clear");
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card && damage.card->isKindOf("Slash")) {
				if (!damage.card->hasFlag("mobileniluan_slash")) return false;
				room->setPlayerFlag(damage.from, "mobileniluan_damage_" + damage.to->objectName());
			}
		}
		return false;
	}
};

class MobileXiaoxi : public OneCardViewAsSkill
{
public:
	MobileXiaoxi() : OneCardViewAsSkill("mobilexiaoxi")
	{
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		Card *slash = Sanguosha->cloneCard("slash");
		slash->setSkillName(objectName());
		slash->deleteLater();
		return slash->isAvailable(player);
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.contains("slash") || pattern.contains("Slash");
	}

	bool viewFilter(const Card *card) const
	{
		if (!card->isBlack())
			return false;

		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
			Card *slash = Sanguosha->cloneCard("slash");
			slash->addSubcard(card->getEffectiveId());
			slash->setSkillName(objectName());
			slash->deleteLater();
			return slash->isAvailable(Self);
		}
		return !Self->isLocked(card);
	}

	const Card *viewAs(const Card *originalCard) const
	{
		Card *slash = Sanguosha->cloneCard("slash");
		slash->addSubcard(originalCard->getId());
		slash->setSkillName(objectName());
		return slash;
	}
};

MobileLianjiCard::MobileLianjiCard()
{
}

bool MobileLianjiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length() < 2 && to_select != Self;
}

bool MobileLianjiCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == 2;
}

void MobileLianjiCard::onUse(Room *room, CardUseStruct &card_use) const
{
	QVariant data = QVariant::fromValue(card_use);
	RoomThread *thread = room->getThread();

	thread->trigger(PreCardUsed, room, card_use.from, data);
	card_use = data.value<CardUseStruct>();

	LogMessage log;
	log.from = card_use.from;
	log.to << card_use.to;
	log.type = "#UseCard";
	log.card_str = toString();
	room->sendLog(log);

	thread->trigger(CardUsed, room, card_use.from, data);
	card_use = data.value<CardUseStruct>();
	thread->trigger(CardFinished, room, card_use.from, data);
}

void MobileLianjiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ServerPlayer *first = targets.first(), *second = targets.last();
	QList<const Card *> weapons;
	foreach (int id, room->getDrawPile()) {
		const Card *c = Sanguosha->getCard(id);
		if (c->isKindOf("Weapon")&&first->canUse(c,first))
			weapons << c;
	}
	if (weapons.isEmpty()) return;

	const Card *weapon = weapons.at(qrand() % weapons.length());
	if(weapon->isKindOf("QinggangSword")){
		for (int i = 0; i < Sanguosha->getCardCount(); i++) {
			const Card *c = Sanguosha->getEngineCard(i);
			if(c->objectName().endsWith("qibaodao")&&!room->getCardOwner(i)&&first->canUse(c,first)){
				room->setCardMapping(weapon->getId(), nullptr, Player::PlaceTable);
				room->getDrawPile().removeOne(weapon->getId());
				weapon = c;
				break;
			}
		}
	}
	room->useCard(CardUseStruct(weapon, first));

	if (first->isDead() || second->isDead()) return;

	QStringList names;
	names << "duel" << "savage_assault" << "archery_attack" << "slash";
	if (!Config.BanPackages.contains("maneuvering"))
		names << "fire_attack";

	QList<Card *> cards;
	foreach (QString name, names) {
		Card *card = Sanguosha->cloneCard(name);
		if (!card) continue;
		card->setSkillName("_mobilelianji");
		card->deleteLater();
		if (!first->canUse(card, second, true)) continue;
		cards << card;
	}
	if (cards.isEmpty()) return;

	Card *card = cards.at(qrand() % cards.length());
	room->setCardFlag(card, "mobilelianji_card_" + source->objectName());
	room->useCard(CardUseStruct(card, first, second));
}

class MobileLianjiVS : public ZeroCardViewAsSkill
{
public:
	MobileLianjiVS() : ZeroCardViewAsSkill("mobilelianji")
	{
	}

	const Card *viewAs() const
	{
		return new MobileLianjiCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("MobileLianjiCard");
	}
};

class MobileLianji : public TriggerSkill
{
public:
	MobileLianji() : TriggerSkill("mobilelianji")
	{
		events << CardFinished << DamageDone;
		view_as_skill = new MobileLianjiVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		if (event == DamageDone) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card) return false;
			foreach (QString flag, damage.card->getFlags()) {
				if (!flag.startsWith("mobilelianji_card_")) continue;
				int n = room->getTag("mobilelianji_card_damage_point_" + damage.card->toString()).toInt();
				n += damage.damage;
				room->setTag("mobilelianji_card_damage_point_" + damage.card->toString(), n);
				break;
			}
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			foreach (QString flag, use.card->getFlags()) {
				if (!flag.startsWith("mobilelianji_card_")) continue;

				int n = room->getTag("mobilelianji_card_damage_point_" + use.card->toString()).toInt();
				room->removeTag("mobilelianji_card_damage_point_" + use.card->toString());
				if (n <= 0) break;

				QString name = flag.split("_").last();
				ServerPlayer *source = room->findChild<ServerPlayer *>(name);
				if (!source || source->isDead()) break;

				source->gainMark("&mobilelianji", n);
				break;
			}
		}
		return false;
	}
};

class MobileMoucheng : public TriggerSkill
{
public:
	MobileMoucheng() : TriggerSkill("mobilemoucheng")
	{
		events << Damage;
		frequency = Wake;
		waked_skills = "jingong";
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const
	{
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->isDead() || !p->hasSkill(objectName()) || p->getMark(objectName()) > 0) continue;

			if(p->getMark("&mobilelianji")>2){
				LogMessage log;
				log.type = "#MobileMouchengWake";
				log.from = p;
				log.arg = QString::number(p->getMark("&mobilelianji"));
				log.arg2 = objectName();
				room->sendLog(log);
			}else if(!p->canWake(objectName()))
				continue;
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(p, objectName());

			room->setPlayerMark(p, "&mobilelianji", 0);

			room->doSuperLightbox(p, "mobilemoucheng");
			room->setPlayerMark(p, "mobilemoucheng", 1);

			if (room->changeMaxHpForAwakenSkill(p, 1, objectName())){
				room->recover(p, RecoverStruct(p, nullptr, 1, "mengqing"));
				room->handleAcquireDetachSkills(p, "-mobilelianji|jingong");
			}
		}
		return false;
	}
};

class Xunde : public MasochismSkill
{
public:
	Xunde() : MasochismSkill("xunde")
	{
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	void onDamaged(ServerPlayer *player, const DamageStruct &damage) const
	{
		ServerPlayer *from = damage.from;
		Room *room = player->getRoom();
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (player->isDead()) return;
			if (p->isDead() || !p->hasSkill(objectName()) || p->distanceTo(player) > 1) continue;
			if (!p->askForSkillInvoke(this, player)) continue;
			room->broadcastSkillInvoke(this);

			JudgeStruct judge;
			judge.who = p;
			judge.reason = objectName();
			judge.play_animation = false;
			judge.pattern = ".";
			room->judge(judge);

			QStringList choices;
			const Card *jcard = judge.card;
			if (jcard->getNumber() >= 6 && room->CardInPlace(jcard, Player::DiscardPile) && player->isAlive())
				choices << "obtain=" + player->objectName() + "=" + jcard->objectName();
			if (jcard->getNumber() <= 6 && from && from->isAlive() && !from->isKongcheng())
				choices << "discard=" + from->objectName();
			if (choices.isEmpty()) continue;

			p->setTag("XundeJudgeForAI", QVariant::fromValue(&judge));
			QString choice = room->askForChoice(p, objectName(), choices.join("+"), QVariant::fromValue(damage));
			p->removeTag("XundeJudgeForAI");

			if (choice.startsWith("obtain")) {
				if (player->isDead()) continue;
				room->obtainCard(player, jcard);
			} else {
				if (from && from->isAlive() && from->canDiscard(from, "h"))
					room->askForDiscard(from, objectName(), 1, 1);
			}
		}
	}
};

class Chenjie : public TriggerSkill
{
public:
	Chenjie() : TriggerSkill("chenjie")
	{
		events << AskForRetrial;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (player->isNude()) return false;
		JudgeStruct *judge = data.value<JudgeStruct *>();
		QStringList prompt_list;
		prompt_list << "@chenjie-card" << judge->who->objectName()
			<< objectName() << judge->reason << QString::number(judge->card->getEffectiveId());
		QString prompt = prompt_list.join(":");

		const Card *card = room->askForCard(player, ".|" + judge->card->getSuitString(), prompt, QVariant::fromValue(judge),
											Card::MethodResponse, judge->who, true, objectName());
		if (!card) return false;
		room->broadcastSkillInvoke(objectName());
		room->retrial(card, player, judge, objectName());
		player->drawCards(2, objectName());
		return false;
	}
};

class MobileYingyuan : public TriggerSkill
{
public:
	MobileYingyuan() : TriggerSkill("mobileyingyuan")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (!player->hasFlag("CurrentPlayer")) return false;

		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.to_place != Player::DiscardPile||move.reason.m_playerId != player->objectName()) return false;
        if ((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE) {
			const Card *card = move.reason.m_extraData.value<const Card *>();
			if (!card || card->isKindOf("SkillCard") || !room->CardInPlace(card, Player::DiscardPile)) return false;

			QString name = card->objectName();
			if (card->isKindOf("Slash"))
				name = "slash";
			if (player->getMark("mobileyingyuan" + name + "-Clear") > 0) return false;

			player->setTag("mobileyingyuanCard", move.reason.m_extraData);
			ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(),
															"@mobileyingyuan:" + card->objectName(), true, true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());
			room->addPlayerMark(player, "mobileyingyuan" + name + "-Clear");

			CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "mobileyingyuan", "");
			room->obtainCard(target, card, reason, true);
		}
		return false;
	}
};

NewxuehenCard::NewxuehenCard()
{
}

bool NewxuehenCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
	return targets.length() < Self->getLostHp();
}

void NewxuehenCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		if (p->isAlive() && !p->isChained()) {
			room->setPlayerChained(p);
		}
	}
	if (source->isDead()) return;

	ServerPlayer *to = room->askForPlayerChosen(source, targets, "newxuehen", "@newxuehen-invoke");
	room->doAnimate(1, source->objectName(), to->objectName());
	room->damage(DamageStruct("newxuehen", source, to, 1, DamageStruct::Fire));
}

class Newxuehen : public OneCardViewAsSkill
{
public:
	Newxuehen() : OneCardViewAsSkill("newxuehen")
	{
		filter_pattern = ".|red";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("NewxuehenCard") && player->getLostHp() > 0;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		NewxuehenCard *card = new NewxuehenCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class NewHuxiao : public TriggerSkill
{
public:
	NewHuxiao() : TriggerSkill("newhuxiao")
	{
		events << Damage;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.nature != DamageStruct::Fire || !damage.from || damage.from->isDead() || !damage.from->hasSkill(objectName()) ||
			damage.to->isDead()) return false;
		room->sendCompulsoryTriggerLog(damage.from, objectName(), true, true);
		damage.to->drawCards(1, objectName());
		room->addPlayerMark(damage.from, "newhuxiao_from-Clear");
		room->addPlayerMark(damage.to, "newhuxiao_to-Clear");
		return false;
	}
};

class NewHuxiaoTargetMod : public TargetModSkill
{
public:
	NewHuxiaoTargetMod() : TargetModSkill("#newhuxiao-target")
	{
		pattern = ".";
	}

	int getResidueNum(const Player *from, const Card *card, const Player *to) const
	{
		if(from->getMark("newhuxiao_from-Clear") > 0 && to && to->getMark("newhuxiao_to-Clear") > 0)
			return 999;
		if(from->hasFlag("CurrentPlayer")&&from->hasSkill("bsxianshuai")&&from->getMark(card->getSuitString()+"bsxianshuai-Clear")<1)
			return 999;
		if(from->getMark("&jiejie+"+card->getSuitString()+"_char-Clear")>0)
			return 999;
		return 0;
	}
};

class NewWuji : public PhaseChangeSkill
{
public:
	NewWuji() : PhaseChangeSkill("newwuji")
	{
		frequency = Wake;
	}

	bool triggerable(const ServerPlayer *player) const
	{
		return player&&player->isAlive()&&player->getMark(objectName())<1
		&&player->getPhase() == Player::Finish&&player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getMark("damage_point_round") >= 3) {
			LogMessage log;
			log.type = "#WujiWake";
			log.from = player;
			log.arg = QString::number(player->getMark("damage_point_round"));
			log.arg2 = objectName();
			room->sendLog(log);
		}else if(!player->canWake(objectName()))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());

		room->doSuperLightbox(player, "newwuji");

		room->setPlayerMark(player, "newwuji", 1);
		if (room->changeMaxHpForAwakenSkill(player, 1, objectName())) {
			room->recover(player, RecoverStruct("newwuji", player));

			if (player->isAlive())
				room->handleAcquireDetachSkills(player, "-newhuxiao");

			if (player->isDead()) return false;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				foreach (const Card *c, p->getCards("ej")) {
					if (Sanguosha->getEngineCard(c->getEffectiveId())->objectName() == "blade") {
						room->obtainCard(player, c, true);
						return false;
					}
				}
			}

			foreach (int id, room->getDrawPile() + room->getDiscardPile()) {
				if (Sanguosha->getEngineCard(id)->objectName() == "blade") {
					room->obtainCard(player, id, true);
					return false;
				}
			}
		}
		return false;
	}
};

class NewYongdi : public MasochismSkill
{
public:
	NewYongdi() : MasochismSkill("newyongdi")
	{
		frequency = Limited;
		limit_mark = "@newyongdiMark";
	}

	void onDamaged(ServerPlayer *player, const DamageStruct &) const
	{
		if (player->getMark("@newyongdiMark") <= 0) return;
		Room *room = player->getRoom();

		QList<ServerPlayer *> males;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->isMale())
				males << p;
		}
		if (males.isEmpty()) return;

		ServerPlayer *male = room->askForPlayerChosen(player, males, objectName(), "@newyongdi-invoke", true, true);
		if (!male) return;

		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(player, "newyongdi");
		room->removePlayerMark(player, "@newyongdiMark");

		room->gainMaxHp(male, 1, objectName());

		if (male->isLord()) return;
		QStringList skills;

		foreach (const Skill *skill, male->getGeneral()->getVisibleSkillList()) {
			if (skill->isLordSkill() && !male->hasLordSkill(skill, true) && !skills.contains(skill->objectName()))
				skills << skill->objectName();
		}

		if (male->getGeneral2()) {
			foreach (const Skill *skill, male->getGeneral2()->getVisibleSkillList()) {
				if (skill->isLordSkill() && !male->hasLordSkill(skill, true) && !skills.contains(skill->objectName()))
					skills << skill->objectName();
			}
		}

		if (skills.isEmpty()) return;
		room->handleAcquireDetachSkills(male, skills);
		return;
	}
};

class Chijiec : public GameStartSkill
{
public:
	Chijiec() : GameStartSkill("chijiec")
	{
	}

	void onGameStart(ServerPlayer *player) const
	{
		if (!player->askForSkillInvoke(this)) return;
		Room *room = player->getRoom();
		room->broadcastSkillInvoke(objectName());

		QStringList kingdoms;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (!kingdoms.contains(p->getKingdom()))
				kingdoms << p->getKingdom();
		}
		if (kingdoms.isEmpty()) return;
		QString kingdom = room->askForChoice(player, objectName(), kingdoms.join("+"));
		LogMessage log;
		log.type = "#ChijieKingdom";
		log.from = player;
		log.arg = kingdom;
		room->sendLog(log);
		room->setPlayerProperty(player, "kingdom", kingdom);
	}
};

WaishiCard::WaishiCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool WaishiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && to_select->getHandcardNum() >= subcardsLength();
}

void WaishiCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *a = effect.from;
	ServerPlayer *b = effect.to;
	if (a->isDead() || b->isDead()) return;

	QList<int> subcards = getSubcards();
	QList<int> hand = b->handCards();
	QList<int> ids;
	for (int i = 1; i <= subcards.length(); i++) {
		int id = hand.at(qrand() % hand.length());
		hand.removeOne(id);
		ids << id;
		if (hand.isEmpty()) break;
	}

	Room *room = a->getRoom();
	QList<CardsMoveStruct> exchangeMove;
	CardsMoveStruct move1(subcards, b, Player::PlaceHand,
		CardMoveReason(CardMoveReason::S_REASON_SWAP, a->objectName(), b->objectName(), "waishi", ""));
	CardsMoveStruct move2(ids, a, Player::PlaceHand,
		CardMoveReason(CardMoveReason::S_REASON_SWAP, b->objectName(), a->objectName(), "waishi", ""));
	exchangeMove.push_back(move1);
	exchangeMove.push_back(move2);
	room->moveCardsAtomic(exchangeMove, false);

	if (a->isDead() || b->isDead()) return;
	if (a->getKingdom() == b->getKingdom() || b->getHandcardNum() > a->getHandcardNum())
		a->drawCards(1, "waishi");
}

class Waishi : public ViewAsSkill
{
public:
	Waishi() : ViewAsSkill("waishi")
	{
	}

	int getKingdom(const Player *player) const
	{
		QStringList kingdoms;
		foreach (const Player *p, player->getAliveSiblings()) {
			if (!kingdoms.contains(p->getKingdom()))
				kingdoms << p->getKingdom();
		}
		if (!kingdoms.contains(player->getKingdom()))
			kingdoms << player->getKingdom();
		return kingdoms.length();
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *) const
	{
		return selected.length() < getKingdom(Self);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;

		WaishiCard *c = new WaishiCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("WaishiCard") < 1 + player->getMark("&waishi_extra-SelfPlayClear");
	}
};

class Renshe : public TriggerSkill
{
public:
	Renshe() : TriggerSkill("renshe")
	{
		events << Damaged;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(event==Damaged){
			if (!player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			QStringList kingdoms, choices;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (p->getKingdom() != player->getKingdom())
					kingdoms << p->getKingdom();
			}
			if (!kingdoms.isEmpty())
				choices << "change";
			choices << "extra" << "draw";
			QString choice = room->askForChoice(player, objectName(), choices.join("+"));
			if (choice == "change") {
				choice = room->askForChoice(player, "renshe_change", kingdoms.join("+"));
				room->changeKingdom(player, choice);
			} else if (choice == "extra") {
				LogMessage log;
				log.type = "#FumianFirstChoice";
				log.from = player;
				log.arg = "renshe:extra";
				room->sendLog(log);
				room->addPlayerMark(player, "&waishi_extra-SelfPlayClear");
			} else {
				ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@renshe-target");
				room->doAnimate(1, player->objectName(), target->objectName());
				QList<ServerPlayer *> all;
				all << player << target;
				room->sortByActionOrder(all);
				room->drawCards(all, 1, objectName());
			}
		}
		return false;
	}
};

class Xingluan : public TriggerSkill
{
public:
	Xingluan(const QString &xingluan) : TriggerSkill(xingluan), xingluan(xingluan)
	{
		events << CardFinished;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (player->getPhase() != Player::Play || player->getMark(xingluan + "-PlayClear") > 0) return false;
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("SkillCard") || use.to.length() != 1) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		room->addPlayerMark(player, xingluan + "-PlayClear");
		QList<int> ids;
		foreach (int id, room->getDrawPile()) {
			if (Sanguosha->getCard(id)->getNumber() == 6)
				ids << id;
		}
		if(xingluan=="olxingluan"){
			QList<ServerPlayer *> tos;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(p->getCardCount()>0)
					tos << p;
				else{
					foreach (const Card *c, player->getCards("j")) {
						if(c->getNumber()==6){
							tos << player;
							break;
						}
					}
				}
			}
			foreach (const Card *c, player->getCards("ej")) {
				if(c->getNumber()==6){
					tos << player;
					break;
				}
			}
			ServerPlayer *to = room->askForPlayerChosen(player,tos,xingluan,"olxingluan0:",true);
			if(to){
				QString choices = "olxingluan3";
				foreach (const Card *c, to->getCards("ej")) {
					if(c->getNumber()==6){
						choices = "olxingluan1+olxingluan3";
						break;
					}
				}
				if(room->askForChoice(player,xingluan,choices)=="olxingluan1"){
					QList<int> disabled_ids;
					foreach (const Card *c, to->getCards("ej")) {
						if(c->getNumber()!=6)
							disabled_ids << c->getId();
					}
					int id = room->askForCardChosen(player,to,"ej",xingluan,false,Card::MethodNone,disabled_ids);
					room->obtainCard(player, id, true);
				}else{
					if(!room->askForDiscard(to,xingluan,1,1,true,true,"olxingluan01:",".|.|6")){
						const Card *c = room->askForExchange(to,xingluan,1,1,true);
						if (c) room->obtainCard(player, c, false);
					}
				}
			}else{
				QList<int> ag_ids;
				if(!ids.isEmpty()){
					int id = ids.at(qrand() % ids.length());
					ag_ids << id;
					ids.removeOne(id);
					if(!ids.isEmpty())
						ag_ids << ids.at(qrand() % ids.length());
				}
				if(ag_ids.isEmpty()){
					player->drawCards(1,xingluan);
				}else{
					room->fillAG(ag_ids);
					int id = room->askForAG(player, ag_ids, true, xingluan);
					room->clearAG();
					room->obtainCard(player, id, true);
				}
			}
			return false;
		}
		if (ids.isEmpty()) {
			if (xingluan == "xingluan") {
				LogMessage log;
				log.type = "#XingluanNoSix";
				log.arg = QString::number(6);
				room->sendLog(log);
			} else if (xingluan == "tenyearxingluan")
				player->drawCards(6, xingluan);
			return false;
		}
		int id = ids.at(qrand() % ids.length());
		room->obtainCard(player, id, true);
		return false;
	}
private:
	QString xingluan;
};

JiaohuaCard::JiaohuaCard()
{
}

bool JiaohuaCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.isEmpty();
}

void JiaohuaCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	room->addPlayerMark(from, "jiaohua_tiansuan_remove_" + user_string);
	QStringList ts = from->getTag("jiaohuaType").toStringList();
	ts << user_string;
	from->setTag("jiaohuaType", ts);

	foreach (int id, room->getDrawPile()) {
		if (Sanguosha->getCard(id)->getType()==user_string){
			room->obtainCard(to,id);
			break;
		}
	}
	if(ts.length()>2){
		foreach (QString t, ts) {
			room->removePlayerMark(from, "jiaohua_tiansuan_remove_" + t);
		}
		from->setTag("jiaohuaType", QStringList());
	}
}

class jiaohua : public ZeroCardViewAsSkill
{
public:
	jiaohua() : ZeroCardViewAsSkill("jiaohua")
	{
	}

	QDialog *getDialog() const
	{
		return TiansuanDialog::getInstance("jiaohua", "basic,trick,equip");
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("JiaohuaCard") < 2;
	}

	const Card *viewAs() const
	{
		JiaohuaCard *c = new JiaohuaCard;
		c->setUserString(Self->getTag("jiaohua").toString());
		return c;
	}
};

class Yichong : public TriggerSkill
{
public:
	Yichong() : TriggerSkill("yichong")
	{
		events << EventPhaseStart << BeforeCardsMove << EventLoseSkill << Death;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==BeforeCardsMove){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&player->hasSkill(objectName())&&player->getMark("que_ycNum")<1){
				QList<int> ids;
				foreach (int id, move.card_ids) {
					const Card *c = Sanguosha->getCard(id);
					if(move.to->getMark("&que_yc+-+"+c->getSuitString()+"_char+#"+player->objectName())>0){
						player->addMark("que_ycNum");
						ids << id;
						break;
					}
				}
				if(ids.isEmpty()) return false;
				move.removeCardIds(ids);
				data = QVariant::fromValue(move);
				CardsMoveStruct move1 = CardsMoveStruct(ids,move.from,player,room->getCardPlace(ids.last()),move.to_place,move.reason);
				room->moveCardsAtomic(move1,move.open.last());
			}
		}else if(event==EventPhaseStart&&player->getPhase()==Player::Start){
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				foreach (QString m, p->getMarkNames()) {
					if(m.startsWith("&que_yc+-+")&&m.endsWith(player->objectName()))
						p->loseMark(m);
				}
			}
			if(!player->hasSkill(objectName())) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "yichong0", true, true);
			if(target){
				room->broadcastSkillInvoke(objectName());
				Card::Suit suit = room->askForSuit(player,objectName());
				QString st = "no_suit";
				if(suit==Card::Spade)
					st = "spade";
				else if(suit==Card::Club)
					st = "club";
				else if(suit==Card::Heart)
					st = "heart";
				else if(suit==Card::Diamond)
					st = "diamond";
				DummyCard *dummy = new DummyCard();
				foreach (const Card *c, target->getEquips()) {
					if (c->getSuit()==suit)
						dummy->addSubcard(c);
				}
				QList<const Card *> cards = target->getHandcards();
				qShuffle(cards);
				foreach (const Card *c, cards) {
					if (c->getSuit()==suit){
						dummy->addSubcard(c);
						break;
					}
				}
				player->obtainCard(dummy, false);
				dummy->deleteLater();
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					foreach (QString m, p->getMarkNames()) {
						if(m.startsWith("&que_yc+-+")&&m.endsWith(player->objectName()))
							p->loseMark(m);
					}
				}
				target->gainMark("&que_yc+-+"+st+"_char+#"+player->objectName());
				player->setMark("que_ycNum",0);
			}
		}else if(event==EventLoseSkill&&data.toString()==objectName()){
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				foreach (QString m, p->getMarkNames()) {
					if(m.startsWith("&que_yc+-+")&&m.endsWith(player->objectName()))
						room->setPlayerMark(p,m,0);
				}
			}
		}else if(event==Death){
			DeathStruct death = data.value<DeathStruct>();
			foreach (ServerPlayer *p, room->getOtherPlayers(death.who)) {
				foreach (QString m, p->getMarkNames()) {
					if(m.startsWith("&que_yc+-+")&&m.endsWith(death.who->objectName()))
						room->setPlayerMark(p,m,0);
				}
			}
		}
		return false;
	}
};

class Wufei : public TriggerSkill
{
public:
	Wufei() : TriggerSkill("wufei")
	{
		events << DamageCaused << Damaged;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			foreach (ServerPlayer *p, room->getOtherPlayers(player)){
				foreach (QString m, p->getMarkNames()) {
					if(m.startsWith("&que_yc+-+")&&m.endsWith(player->objectName())&&p->getMark(m)>0&&p->getHp()>3&&player->askForSkillInvoke(this,p)){
						room->broadcastSkillInvoke(objectName());
						room->damage(DamageStruct(objectName(),nullptr,p));
					}
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isDamageCard()){
				CardUseStruct card_use = room->getTag("UseHistory"+damage.card->toString()).value<CardUseStruct>();
				if(card_use.from!=player) return false;
				if(damage.card->isKindOf("Slash")){
					if(card_use.to.length()!=1)
						return false;
				}else if(card_use.to.length()<2)
					return false;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					foreach (QString m, p->getMarkNames()) {
						if(m.startsWith("&que_yc+-+")&&m.endsWith(player->objectName())&&p->getMark(m)>0){
							room->sendCompulsoryTriggerLog(player,this);
							damage.from = p;
							data.setValue(damage);
						}
					}
				}
			}
		}
		return false;
	}
};

ShiheCard::ShiheCard()
{
}

bool ShiheCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty()&&to_select!=Self&&Self->canPindian(to_select);
}

void ShiheCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	if(from->canPindian(to)){
		if(from->pindian(to,"shihe")){
			room->setPlayerMark(to,"&shihe+#"+from->objectName(),1);
		}else{
			QList<const Card*> cs = from->getHandcards();
			qShuffle(cs);
			foreach (const Card*c, cs) {
				if(from->canDiscard(from,c->getId())){
					room->throwCard(c,from);
					break;
				}
			}
		}
	}
}

class Shihevs : public ZeroCardViewAsSkill
{
public:
	Shihevs() : ZeroCardViewAsSkill("shihe")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("ShiheCard") < 1;
	}

	const Card *viewAs() const
	{
		return new ShiheCard;
	}
};

class Shihe : public TriggerSkill
{
public:
	Shihe() : TriggerSkill("shihe")
	{
		events << EventPhaseStart << Predamage;
		view_as_skill = new Shihevs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==Predamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(player->getMark("&shihe+#"+damage.to->objectName())>0){
				room->sendCompulsoryTriggerLog(damage.to,objectName());
				return damage.to->damageRevises(data,-damage.damage);
			}
		}else if(player->getPhase()==Player::NotActive){
			foreach (QString m, player->getMarkNames()) {
				if(m.startsWith("&shihe+#"))
					room->setPlayerMark(player,m,0);
			}
		}
		return false;
	}
};

class Zhenfu : public TriggerSkill
{
public:
	Zhenfu() : TriggerSkill("zhenfu")
	{
		events << CardsMoveOneTime << EventPhaseStart;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();

			if (player != room->getCurrent()) return false;
			if (player==move.from&&(move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
				player->setFlags("zhenfuDISCARD");
			}
		} else if (player->getPhase()==Player::Discard&&player->hasFlag("zhenfuDISCARD")&&player->hasSkill(objectName())) {

			ServerPlayer *to = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"zhenfu0:",true,true);

			if(to){
				room->broadcastSkillInvoke(objectName());
				to->gainHujia(1,5);
			}
		}
		return false;
	}
};

class Guimou : public TriggerSkill
{
public:
	Guimou() : TriggerSkill("guimou")
	{
		events << CardsMoveOneTime << EventPhaseStart << GameStart << CardUsed << Death << EventLoseSkill;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (player==move.from&&(move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->getMark("&guimou+-+discard")>0){
						room->addPlayerMark(player,"&guimou_discard",move.card_ids.length());
						break;
					}
				}
			}
			if (player==move.to&&move.to_place == Player::PlaceHand) {
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->getMark("&guimou+-+gain")>0){
						room->addPlayerMark(player,"&guimou_gain",move.card_ids.length());
						break;
					}
				}
			}
		}else if(triggerEvent==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isKindOf("SkillCard")){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->getMark("&guimou+-+use")>0){
						room->addPlayerMark(player,"&guimou_use");
						break;
					}
				}
			}
		}else if(triggerEvent==Death||triggerEvent==EventLoseSkill){
			if(room->findPlayerBySkillName(objectName(),true)) return false;
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				foreach (QString m, p->getMarkNames()) {
					if(m.startsWith("&guimou"))
						room->setPlayerMark(player,m,0);
				}
			}
		} else{
			if(!player->hasSkill(objectName())) return false;
			QString choice,choices = "use+discard+gain";
			if(triggerEvent == GameStart){
				room->sendCompulsoryTriggerLog(player,this);
				choice = choices.split("+").at(qrand()%3);
				room->setPlayerMark(player,"&guimou+-+"+choice,1);
			}else if(player->getPhase()==Player::NotActive){
				room->sendCompulsoryTriggerLog(player,this);
				choice = room->askForChoice(player,objectName(),choices);
				room->setPlayerMark(player,"&guimou+-+"+choice,1);
			}else if(player->getPhase()==Player::Start){
				foreach (QString m, player->getMarkNames()) {
					if(m.startsWith("&guimou+-+")&&player->getMark(m)>0){
						room->setPlayerMark(player,m,0);
						choice = m.split("+").last();
					}
				}
				if(choice.isEmpty()) return false;
				room->sendCompulsoryTriggerLog(player,this);
				int n = 998;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					n = qMin(n,p->getMark("&guimou_"+choice));
				}
				QList<ServerPlayer *> tos;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->getMark("&guimou_"+choice)<=n)
						tos << p;
					room->setPlayerMark(p,"&guimou_"+choice,0);
				}
				ServerPlayer *to = room->askForPlayerChosen(player,tos,objectName(),"guimou0:",false,true);
				if(to){
					int id = room->doGongxin(player,to,to->handCards(),objectName());
					if(id>=0){
						tos = room->getOtherPlayers(player);
						tos.removeOne(to);
						ServerPlayer *to1 = room->askForPlayerChosen(player,tos,"guimou1","guimou1:",player->canDiscard(to,id));
						if(to1){
							room->giveCard(player,to1,Sanguosha->getCard(id),objectName());
							n = -1;
						}else if(player->canDiscard(to,id)){
							room->throwCard(id,objectName(),to,player);
							n = -1;
						}
					}
				}
			}
		}
		return false;
	}
};

class Zhouxian : public TriggerSkill
{
public:
	Zhouxian() : TriggerSkill("zhouxian")
	{
		events << TargetConfirming;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == TargetConfirming) {
			CardUseStruct use = data.value<CardUseStruct>();

			if (use.from&&use.from!=player&&use.card->isDamageCard()){
				room->sendCompulsoryTriggerLog(player,this);
				QList<int> ids = room->showDrawPile(player,3,objectName(),true);
				QStringList sts;
				foreach (int id, ids){
					sts << Sanguosha->getCard(id)->getType();
				}
				use.from->setTag("zhouxianUse", data);
				if(!room->askForDiscard(use.from,objectName(),1,1,true,true,"zhouxian0:"+player->objectName(),sts.join(","))){
					use.to.removeOne(player);
					data.setValue(use);
				}
				room->throwCard(ids,objectName(),nullptr);
			}
		}
		return false;
	}
};

class Shoufa : public TriggerSkill
{
public:
	Shoufa() : TriggerSkill("shoufa")
	{
		events << Damage << Damaged;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (triggerEvent == Damage) {
			player->addMark("shoufaDamage-Clear");
			if (player->getMark("shoufaDamage-Clear")==1){
				QList<ServerPlayer *> tos;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(player->distanceTo(p)<=2)
						tos << p;
				}
				ServerPlayer *to = room->askForPlayerChosen(player,tos,objectName(),"shoufa0:",true,true);
				if(to){
					room->broadcastSkillInvoke(objectName());
					QString yeshou = player->getTag("zhoulin_yeshou").toString();
					if(yeshou.isEmpty()){
						yeshou = "yeshou_bao+yeshou_ying+yeshou_xiong+yeshou_tu";
						yeshou = yeshou.split("+").at(qrand()%4);
					}
					LogMessage log;
					log.type = "#shoufa";
					log.from = player;
					log.to << to;
					log.arg = yeshou;
					log.arg2 = "shoufa";
					room->sendLog(log);
					if(yeshou=="yeshou_bao"){
						room->damage(DamageStruct(yeshou,nullptr,to));
					}else if(yeshou=="yeshou_ying"){
						QList<const Card *> cs = to->getCards("he");
						qShuffle(cs);
						foreach (const Card *c, cs){
							room->obtainCard(player,c,false);
							break;
						}
					}else if(yeshou=="yeshou_xiong"){
						QList<const Card *> cs = to->getCards("e");
						qShuffle(cs);
						foreach (const Card *c, cs){
							if(player->canDiscard(to,c->getId())){
								room->throwCard(c,yeshou,to,player);
								break;
							}
						}
					}else{
						to->drawCards(1,yeshou);
					}
				}
			}
		}else if(player->getMark("shoufaDamaged-Clear")<=5){
			QList<ServerPlayer *> tos;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)){
				if(p->distanceTo(player)>2)
					tos << p;
			}
			ServerPlayer *to = room->askForPlayerChosen(player,tos,objectName(),"shoufa0:",true,true);
			if(to){
				room->broadcastSkillInvoke(objectName());
				player->addMark("shoufaDamaged-Clear");
				QString yeshou = player->getTag("zhoulin_yeshou").toString();
				if(yeshou.isEmpty()){
					yeshou = "yeshou_bao+yeshou_ying+yeshou_xiong+yeshou_tu";
					yeshou = yeshou.split("+").at(qrand()%4);
				}
				LogMessage log;
				log.type = "#shoufa";
				log.from = player;
				log.to << to;
				log.arg = yeshou;
				log.arg2 = "shoufa";
				room->sendLog(log);
				if(yeshou=="yeshou_bao"){
					room->damage(DamageStruct(yeshou,nullptr,to));
				}else if(yeshou=="yeshou_ying"){
					QList<const Card *> cs = to->getCards("he");
					qShuffle(cs);
					foreach (const Card *c, cs){
						room->obtainCard(player,c,false);
						break;
					}
				}else if(yeshou=="yeshou_xiong"){
					QList<const Card *> cs = to->getCards("e");
					qShuffle(cs);
					foreach (const Card *c, cs){
						if(player->canDiscard(to,c->getId())){
							room->throwCard(c,yeshou,to,player);
							break;
						}
					}
				}else{
					to->drawCards(1,yeshou);
				}
			}
		}
		return false;
	}
};

ZhoulinCard::ZhoulinCard()
{
	target_fixed = true;
}

void ZhoulinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->removePlayerMark(source,"@zhoulin");
	room->doSuperLightbox(source,getSkillName());
	source->gainHujia(2,5);
	QString choices = "yeshou_bao+yeshou_ying+yeshou_xiong+yeshou_tu";
	choices = room->askForChoice(source,getSkillName(),choices);
	source->setTag("zhoulin_yeshou", choices);
	
}

class Zhoulinvs : public ZeroCardViewAsSkill
{
public:
	Zhoulinvs() : ZeroCardViewAsSkill("zhoulin")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@zhoulin")>0;
	}

	const Card *viewAs() const
	{
		return new ZhoulinCard;
	}
};

class Zhoulin : public TriggerSkill
{
public:
	Zhoulin() : TriggerSkill("zhoulin")
	{
		events << EventPhaseStart;
		view_as_skill = new Zhoulinvs;
		frequency = Limited;
		limit_mark = "@zhoulin";
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent , Room *, ServerPlayer *player, QVariant &) const
	{
		if(player->getPhase()<Player::Start){
			player->setTag("zhoulin_yeshou", "");
		}
		return false;
	}
};

class Yuxiang : public TriggerSkill
{
public:
	Yuxiang() : TriggerSkill("yuxiang")
	{
		events << DamageInflicted;
		frequency = Compulsory;
		waked_skills = "#yuxiang";
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (triggerEvent == DamageInflicted) {
			if (damage.nature==DamageStruct::Fire&&player->getHujia()>0){
				room->sendCompulsoryTriggerLog(player,this);
				damage.damage += 1;
				data.setValue(damage);
			}
		}
		return false;
	}
};

class YuxiangDistance : public DistanceSkill
{
public:
	YuxiangDistance() : DistanceSkill("#yuxiang")
	{
	}

	int getCorrect(const Player *from, const Player *to) const
	{
		int n = 0;
		if (to->hasSkill("yuxiang")&&to->getHujia()>0) {
			n += 1;
		}
		if (from->hasSkill("yuxiang")&&from->getHujia()>0) {
			n -= 1;
		}
		return n;
	}
};

class spYilie : public TriggerSkill
{
public:
	spYilie() : TriggerSkill("spyilie")
	{
		events << DamageInflicted << GameStart << EventPhaseStart << Damage;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == DamageInflicted) {
			DamageStruct damage = data.value<DamageStruct>();
			foreach (ServerPlayer *p, room->getOtherPlayers(player)){
				if(player->getMark("&spyilie+#"+p->objectName())>0&&p->hasSkill(objectName())&&p->getMark("&yi_lie")<1){
					room->sendCompulsoryTriggerLog(p,this,qrand()%2+1);
					p->gainMark("&yi_lie",damage.damage);
					return true;
				}
			}
		}else if(triggerEvent == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			foreach (ServerPlayer *p, room->getOtherPlayers(player)){
				if(damage.to!=p&&player->getMark("&spyilie+#"+p->objectName())>0&&p->hasSkill(objectName())&&p->isWounded()){
					room->sendCompulsoryTriggerLog(p,this,qrand()%2+1);
					room->recover(p,RecoverStruct(objectName()));
				}
			}
		}else if(triggerEvent == GameStart) {
			if(player->hasSkill(objectName())){
				ServerPlayer *to = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"spyilie0:",false,true);
				if(to){
					room->broadcastSkillInvoke(objectName(),player,qrand()%2+1);
					room->setPlayerMark(to,"&spyilie+#"+player->objectName(),1);
				}
			}
		}else if(player->getPhase()==Player::Finish){
			int n = player->getMark("&yi_lie");
			if(n>0){
				room->sendCompulsoryTriggerLog(player,this,3);
				player->drawCards(1,objectName());
				room->loseHp(player,n,true,player,objectName());
				player->loseAllMarks("&yi_lie");
			}
		}
		return false;
	}
};

class Laishou : public TriggerSkill
{
public:
	Laishou() : TriggerSkill("laishou")
	{
		events << DamageInflicted << EventPhaseStart;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == DamageInflicted) {
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.damage>=player->getHp()+player->getHujia()&&player->getMaxHp()<9){
				room->sendCompulsoryTriggerLog(player,this,qrand()%2+1);
				room->gainMaxHp(player,damage.damage,objectName());
				return true;
			}
		}else if(player->getPhase()==Player::Start){
			if(player->getMaxHp()>=9){
				room->sendCompulsoryTriggerLog(player,this,3);
				room->killPlayer(player);
			}
		}
		return false;
	}
};

LuanqunCard::LuanqunCard()
{
	target_fixed = true;
}

void LuanqunCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	foreach (ServerPlayer *p, room->getOtherPlayers(source)){
		room->doAnimate(1,source->objectName(),p->objectName());
	}
	QHash<ServerPlayer *, const Card* > pc;
	foreach (ServerPlayer *p, room->getAllPlayers()){
		if(p->getHandcardNum()>0)
			pc[p] = room->askForCardShow(p,source,getSkillName());
	}
	QList<int> ids;
	foreach (ServerPlayer *p, pc.keys()){
		const Card*c = pc[p];
		if(c) room->showCard(p,c->getEffectiveId());
		if(source!=p&&pc.contains(source)){
			if(c->getColor()==pc[source]->getColor())
				ids << c->getEffectiveId();
			else{
				room->setPlayerMark(p,"&luanqun+#"+source->objectName(),1);
			}
		}
	}
	if(ids.length()>0){
		room->fillAG(ids,source);
		int id = room->askForAG(source,ids,true,getSkillName());
		room->clearAG(source);
		if(id>=0){
			room->obtainCard(source,id);
		}
	}
}

class Luanqunvs : public ZeroCardViewAsSkill
{
public:
	Luanqunvs() : ZeroCardViewAsSkill("luanqun")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("LuanqunCard")<1&&player->getHandcardNum()>0;
	}

	const Card *viewAs() const
	{
		return new LuanqunCard;
	}
};

class Luanqun : public TriggerSkill
{
public:
	Luanqun() : TriggerSkill("luanqun")
	{
		events << EventPhaseEnd << CardEffect << CardUsed;
		view_as_skill = new Luanqunvs;
		waked_skills = "#luanqun-pro";
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardEffect){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->isKindOf("Slash")&&effect.from->hasFlag("CurrentPlayer")&&effect.from->getMark("&luanqun+#"+player->objectName())>0){
				effect.no_respond = true;
				data.setValue(effect);
			}
		}else if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&player->hasFlag("CurrentPlayer")&&player->getPhase()==Player::Play){
				foreach (ServerPlayer *p, use.to)
					room->setPlayerMark(player,"&luanqun+#"+p->objectName(),0);
			}
		}else if(player->getPhase()==Player::Play&&player->hasFlag("CurrentPlayer")){
			foreach (ServerPlayer *p, room->getPlayers())
				room->setPlayerMark(player,"&luanqun+#"+p->objectName(),0);
		}
		return false;
	}
};

class LuanqunPro : public ProhibitSkill
{
public:
	LuanqunPro() : ProhibitSkill("#luanqun-pro")
	{
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		if(card->isKindOf("Slash")&&from->getPhase()==Player::Play){
			foreach (const Player *p, to->getAliveSiblings()){
				if(from->getMark("&luanqun+#"+p->objectName())>0)
					return true;
			}
		}
		return false;
	}
};

NaxueCard::NaxueCard()
{
}

bool NaxueCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length()<2&&to_select!=Self;
}

bool NaxueCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == subcardsLength();
}

void NaxueCard::onUse(Room *room, CardUseStruct &use) const
{
	int i = 0;
	foreach (ServerPlayer *p, use.to){
		room->giveCard(use.from,p,Sanguosha->getCard(subcards.at(i)),getSkillName());
		i++;
	}
}

class Naxuevs : public ViewAsSkill
{
public:
	Naxuevs() : ViewAsSkill("naxue")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *) const
	{
		return selected.length()<2;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.length()<2) return nullptr;
		NaxueCard *c = new NaxueCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@naxue";
	}
};

class Naxue : public TriggerSkill
{
public:
	Naxue() : TriggerSkill("naxue")
	{
		events << EventPhaseChanging;
		view_as_skill = new Naxuevs;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		Player::Phase phase = data.value<PhaseChangeStruct>().to;
		if (phase != Player::Play||player->isSkipped(phase)) return false;
		if (player->askForSkillInvoke(this,data)){
			room->broadcastSkillInvoke(objectName());
			player->skip(phase);
			const Card*dc = room->askForDiscard(player,objectName(),999,1,true,true,"naxue0:");
			if(dc){
				player->drawCards(dc->subcardsLength(),objectName());
				room->askForUseCard(player,"@@naxue","naxue1:");
			}
		}
		return false;
	}
};

class Yijie : public TriggerSkill
{
public:
	Yijie() : TriggerSkill("yijie")
	{
		events << Death;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->hasSkill(objectName());
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DeathStruct death = data.value<DeathStruct>();
		if (death.who != player) return false;
		room->sendCompulsoryTriggerLog(player,this);
		int n = 0,m = 0;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)){
			room->doAnimate(1,player->objectName(),p->objectName());
			m += p->getHp();
			n++;
		}
		n = qMax(1,m/n);
		foreach (ServerPlayer *p, room->getOtherPlayers(player)){
			room->setPlayerProperty(p,"hp",qMin(n,p->getMaxHp()));
		}
		return false;
	}
};

XietuCard::XietuCard()
{
	mute = true;
}

bool XietuCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.isEmpty();
}

void XietuCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	if(from->getMark("weimingShiming")==1){
		QStringList choices;
		if(from->getMark("xietu1-PlayClear")<1)
			choices << "xietu1";
		if(from->getMark("xietu2-PlayClear")<1)
			choices << "xietu2";
		if(choices.isEmpty()) return;
		from->peiyin(getSkillName(),qrand()%2+1);
		if(room->askForChoice(from,getSkillName(),choices.join("+"),QVariant::fromValue(to))=="xietu1"){
			from->addMark("xietu1-PlayClear");
			room->recover(to,RecoverStruct(getSkillName(),from));
		}else{
			from->addMark("xietu2-PlayClear");
			to->drawCards(2,getSkillName());
		}
	}else if(from->getChangeSkillState(getSkillName())==1){
		room->setChangeSkillState(from,getSkillName(),2);
		if(from->getMark("weimingShiming")==2){
			from->peiyin(getSkillName(),qrand()%2+3);
			room->recover(from,RecoverStruct(getSkillName(),from));
			room->askForDiscard(to,getSkillName(),2,2,false,true);
		}else{
			from->peiyin(getSkillName(),qrand()%2+1);
			room->recover(to,RecoverStruct(getSkillName(),from));
		}
	}else{
		room->setChangeSkillState(from,getSkillName(),1);
		if(from->getMark("weimingShiming")==2){
			from->peiyin(getSkillName(),qrand()%2+3);
			from->drawCards(1,getSkillName());
			room->damage(DamageStruct(getSkillName(),from,to));
		}else{
			from->peiyin(getSkillName(),qrand()%2+1);
			to->drawCards(2,getSkillName());
		}
	}
}

class Xietu : public ZeroCardViewAsSkill
{
public:
	Xietu() : ZeroCardViewAsSkill("xietu")
	{
		change_skill = true;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		int m = 1;
		if(player->getMark("weimingShiming")==1)
			m = 2;
		return player->usedTimes("XietuCard") < m;
	}

	const Card *viewAs() const
	{
		return new XietuCard;
	}
};

class Weiming : public TriggerSkill
{
public:
	Weiming() : TriggerSkill("weiming")
	{
		events << Death << EventPhaseStart;
		shiming_skill = true;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Play&&player->isAlive()
				&&player->getMark("weimingShiming")<1&&player->hasSkill(objectName())){
				QList<ServerPlayer *> tos;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->getMark("&weiming+#"+player->objectName())<1){
						tos << p;
					}
				}
				ServerPlayer *to = room->askForPlayerChosen(player,tos,objectName(),"weiming0",false,true);
				if(to){
					room->broadcastSkillInvoke(objectName(),player,2);
					room->setPlayerMark(to,"&weiming+#"+player->objectName(),1);
				}
			}
		}else{
			DeathStruct death = data.value<DeathStruct>();
			if(death.who==player){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->getMark("weimingShiming")<1&&p->hasSkill(objectName())){
						if(player->getMark("&weiming+#"+p->objectName())>0){
							room->setPlayerMark(p,"weimingShiming",2);
							room->sendShimingLog(player,objectName(),false,1);
							foreach (ServerPlayer *q, room->getOtherPlayers(p)){
								room->setPlayerMark(q,"&weiming+#"+p->objectName(),0);
							}
							room->changeTranslation(p,"xietu1",Sanguosha->translate(":xietu4"));
							room->changeTranslation(p,"xietu2",Sanguosha->translate(":xietu5"));
							room->changeTranslation(p,"xietu",p->getChangeSkillState("xietu"));
						}else if(death.damage&&death.damage->from==p){
							room->setPlayerMark(p,"weimingShiming",1);
							room->sendShimingLog(player,objectName(),true,3);
							foreach (ServerPlayer *q, room->getOtherPlayers(p)){
								room->setPlayerMark(q,"&weiming+#"+p->objectName(),0);
							}
							int n = p->getChangeSkillState("xietu");
							room->setPlayerMark(p, QString("&xietu+%1_num").arg(n), 0);
							room->changeTranslation(p,"xietu",3);
						}
					}
				}
			}
		}
		return false;
	}
};

class Chengxiong : public TriggerSkill
{
public:
	Chengxiong() : TriggerSkill("chengxiong")
	{
		events << TargetSpecified << CardUsed;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("TrickCard")&&player->hasSkill(objectName())){
				if(use.to.size()==1&&use.to.contains(player)) return false;
				QList<ServerPlayer *>tos;
				int n = player->getMark("chengxiongUse-PlayClear");
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->getCardCount()>=n&&player->canDiscard(p,"he"))
						tos << p;
				}
				ServerPlayer *to = room->askForPlayerChosen(player,tos,objectName(),"chengxiong0:"+QString::number(n),true,true);
				if(to){
					room->broadcastSkillInvoke(objectName());
					int id = room->askForCardChosen(player,to,"he",objectName(),false,Card::MethodDiscard);
					if(id>-1){
						room->throwCard(id,objectName(),to,player);
						if(use.card->getColor()==Sanguosha->getCard(id)->getColor())
							room->damage(DamageStruct(objectName(),player,to));
					}
				}
			}
		}else if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				room->addPlayerMark(player,"chengxiongUse-PlayClear");
			}
		}else{
			room->setPlayerMark(player,"chengxiongUse-PlayClear",0);
		}
		return false;
	}
};

class Wangzhuan : public TriggerSkill
{
public:
	Wangzhuan() : TriggerSkill("wangzhuan")
	{
		events << Damaged << EventPhaseChanging;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.card){
				if(player->hasSkill(objectName())&&player->askForSkillInvoke(this,data)){
					room->broadcastSkillInvoke(objectName());
					player->drawCards(2,objectName());
					ServerPlayer *cp = room->getCurrent();
					if(cp&&!cp->hasFlag("wangzhuanUse")){
						cp->setFlags("wangzhuanUse");
						room->addPlayerMark(cp,"@skill_invalidity");
					}
				}
				if(damage.from&&damage.from->isAlive()&&damage.from->hasSkill(objectName())&&damage.from->askForSkillInvoke(this,data)){
					room->broadcastSkillInvoke(objectName());
					damage.from->drawCards(2,objectName());
					ServerPlayer *cp = room->getCurrent();
					if(cp&&!cp->hasFlag("wangzhuanUse")){
						cp->setFlags("wangzhuanUse");
						room->addPlayerMark(cp,"@skill_invalidity");
					}
				}
			}
		}else if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::NotActive&&player->hasFlag("wangzhuanUse")){
				player->setFlags("-wangzhuanUse");
				room->removePlayerMark(player,"@skill_invalidity");
			}
		}
		return false;
	}
};





class Cuizhen : public TriggerSkill
{
public:
	Cuizhen() : TriggerSkill("cuizhen")
	{
		events << TargetSpecified << GameStart << DrawNCards;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")||use.card->isDamageCard()){
				if(use.to.size()==1&&use.to.contains(player)) return false;
				foreach (ServerPlayer *p, use.to){
					if(p!=player&&p->getHandcardNum()>=p->getHp()&&player->askForSkillInvoke(this,p)){
						room->broadcastSkillInvoke(objectName());
						p->throwEquipArea(0);
					}
				}
			}
		}else if(event==GameStart){
			QList<ServerPlayer *> tos = room->askForPlayersChosen(player,room->getOtherPlayers(player),objectName(),0,2,"cuizhen0:",true);
			if(tos.length()>0){
				room->broadcastSkillInvoke(objectName());
				foreach (ServerPlayer *p, tos){
					p->throwEquipArea(0);
				}
			}
		}else{
			DrawStruct draw = data.value<DrawStruct>();
			if (draw.reason=="draw_phase") {
				int n = 0;
				foreach (ServerPlayer *p, room->getAlivePlayers()){
					if(!p->hasEquipArea(0))
						n++;
				}
				if(n>0)
					room->sendCompulsoryTriggerLog(player,objectName());
				draw.num += qMin(2,n);
				data.setValue(draw);
			}
		}
		return false;
	}
};

class Kuili : public TriggerSkill
{
public:
	Kuili() : TriggerSkill("kuili")
	{
		events << Damaged;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from&&damage.from->isAlive()&&!damage.from->hasEquipArea(0)){
				room->sendCompulsoryTriggerLog(player,this);
				damage.from->obtainEquipArea(0);
			}
		}
		return false;
	}
};

ZuoyouCard::ZuoyouCard()
{
}

bool ZuoyouCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if(targets.length()>0) return false;
	if(Self->getChangeSkillState(getSkillName())==2&&!to_select->canDiscard(to_select,"h"))
		return false;
	return true;
}

void ZuoyouCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	if(from->getChangeSkillState(getSkillName())==1){
		room->setChangeSkillState(from,getSkillName(),2);
		to->drawCards(3,getSkillName());
		room->askForDiscard(to,getSkillName(),2,2);
	}else{
		room->setChangeSkillState(from,getSkillName(),1);
		room->askForDiscard(to,getSkillName(),1,1);
		to->gainHujia(1,5);
	}
}

class Zuoyou : public ZeroCardViewAsSkill
{
public:
	Zuoyou() : ZeroCardViewAsSkill("zuoyou")
	{
		change_skill = true;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("ZuoyouCard") < 1;
	}

	const Card *viewAs() const
	{
		return new ZuoyouCard;
	}
};

class ShishouLJ : public TriggerSkill
{
public:
	ShishouLJ() : TriggerSkill("shishoulj")
	{
		events << CardFinished;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("SkillCard")&&use.card->getSkillNames().contains("zuoyou")){
				foreach (ServerPlayer *p, use.to){
					if(p!=player){
						room->sendCompulsoryTriggerLog(player,this);
						if(player->getChangeSkillState("zuoyou")==1){
							player->drawCards(3,"zuoyou");
							room->askForDiscard(player,"zuoyou",2,2);
						}else if(player->canDiscard(player,"h")){
							room->askForDiscard(player,"zuoyou",1,1);
							player->gainHujia(1,5);
						}
					}
				}
			}
		}
		return false;
	}
};

class MobileQianlong : public TriggerSkill
{
public:
	MobileQianlong() : TriggerSkill("mobile_qianlong")
	{
		events << Damage << GameStart << Damaged << CardsMoveOneTime << MarkChanged;
		waked_skills = "qlqingzheng,qljiushi,qlfangzhu,qljuejin";
		setProperty("IgnoreInvalidity",true);
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive() && target->hasSkill(objectName(),true);
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			for (int i = 0; i < damage.damage; i++) {
				if(player->getMark("&daoxin")>=99) continue;
				room->sendCompulsoryTriggerLog(player,this);
				player->gainMark("&daoxin",qMin(99-player->getMark("&daoxin"),15));
			}
		}else if(event==GameStart){
			room->sendCompulsoryTriggerLog(player,this);
			player->gainMark("&daoxin",qMin(99-player->getMark("&daoxin"),20));
		}else if(event==Damaged){
			DamageStruct damage = data.value<DamageStruct>();
			for (int i = 0; i < damage.damage; i++) {
				if(player->getMark("&daoxin")>=99) continue;
				room->sendCompulsoryTriggerLog(player,this);
				player->gainMark("&daoxin",qMin(99-player->getMark("&daoxin"),10));
			}
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.reason.m_skillName!="InitialHandCards"&&move.to_place==Player::PlaceHand&&move.to==player&&player->getMark("&daoxin")<99){
				room->sendCompulsoryTriggerLog(player,this);
				player->gainMark("&daoxin",qMin(99-player->getMark("&daoxin"),5));
			}
		}else if(event==MarkChanged){
			MarkStruct mark = data.value<MarkStruct>();
			if(mark.name=="&daoxin"){
				int n = player->getMark("&daoxin");
				if(n>=99){
					room->acquireSkill(player,"qljuejin");
				}
				if(n>=75){
					room->acquireSkill(player,"qlfangzhu");
				}
				if(n>=50){
					room->acquireSkill(player,"qljiushi");
				}
				if(n>=25){
					room->acquireSkill(player,"qlqingzheng");
				}
			}
		}
		return false;
	}
};

QlQingzhengCard::QlQingzhengCard()
{
}

bool QlQingzhengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty()&&to_select!=Self&&Self->canDiscard(to_select,"h");
}

void QlQingzhengCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	int id = room->doGongxin(from,to,to->handCards(),"qlqingzheng");
	DummyCard *dummy = new DummyCard();
	if(id>-1){
		foreach (const Card *h, to->getHandcards()){
			if(Sanguosha->getCard(id)->getSuit()==h->getSuit()&&from->canDiscard(to,h->getId()))
				dummy->addSubcard(h);
		}
	}
	dummy->deleteLater();
	if(dummy->subcardsLength()>0)
		room->throwCard(dummy,"qlqingzheng",to,from);
	if(subcardsLength()>dummy->subcardsLength())
		room->damage(DamageStruct("qlqingzheng",from,to));
}

class QlQingzhengVs : public ViewAsSkill
{
public:
	QlQingzhengVs() : ViewAsSkill("qlqingzheng")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *card) const
	{
		return selected.isEmpty()&&!Self->isJilei(card)&&!card->isEquipped();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		QlQingzhengCard *sc = new QlQingzhengCard;
		foreach (const Card *c, cards){
			foreach (const Card *h, Self->getHandcards()){
				if(c->getSuit()==h->getSuit()&&!Self->isJilei(h))
					sc->addSubcard(h);
			}
		}
		return sc;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@qlqingzheng";
	}
};

class QlQingzheng : public TriggerSkill
{
public:
	QlQingzheng() : TriggerSkill("qlqingzheng")
	{
		events << EventPhaseStart;
		view_as_skill = new QlQingzhengVs;
		setProperty("IgnoreInvalidity",true);
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive() && target->hasSkill(objectName(),true);
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(player->getPhase()==Player::Play&&player->canDiscard(player,"h")){
			room->askForUseCard(player,"@@qlqingzheng","qlqingzheng0:",-1,Card::MethodDiscard);
		}
		return false;
	}
};

class QlJiushiVS : public ZeroCardViewAsSkill
{
public:
	QlJiushiVS() : ZeroCardViewAsSkill("qljiushi")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Analeptic::IsAvailable(player) && player->faceUp();
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return pattern.contains("analeptic") && player->faceUp();
	}

	const Card *viewAs() const
	{
		Analeptic *analeptic = new Analeptic(Card::NoSuit, 0);
		analeptic->setSkillName(objectName());
		return analeptic;
	}
};

class QlJiushi : public TriggerSkill
{
public:
	QlJiushi() : TriggerSkill("qljiushi")
	{
		events << PreCardUsed << DamageDone << Damaged << TurnedOver;
		view_as_skill = new QlJiushiVS;
		setProperty("IgnoreInvalidity",true);
	}
	int getPriority(TriggerEvent triggerEvent) const
	{
		if (triggerEvent == PreCardUsed)
			return 5;
		return TriggerSkill::getPriority(triggerEvent);
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive() && target->hasSkill(objectName(),true);
	}
	void getTrick(ServerPlayer *player) const
	{
		Room *room = player->getRoom();
		QList<int> tricks;
		foreach (int id, room->getDrawPile()) {
			if (Sanguosha->getCard(id)->isKindOf("TrickCard"))
				tricks << id;
		}
		if (tricks.isEmpty()) return;
		int id = tricks.at(qrand() % tricks.length());
		room->obtainCard(player, id, true);
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == PreCardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getSkillNames().contains(objectName()))
				player->turnOver();
		} else if (triggerEvent == TurnedOver) {
			room->sendCompulsoryTriggerLog(player, this);
			getTrick(player);
		} else if (triggerEvent == Damaged) {
			bool facedown = player->getTag("qljiushiFace").toBool();
			player->removeTag("qljiushiFace");
			if (facedown && !player->faceUp() && player->askForSkillInvoke(this, data)) {
				room->broadcastSkillInvoke(objectName());
				player->turnOver();
				//getTrick(player);
			}
		} else if (triggerEvent == DamageDone)
			player->setTag("qljiushiFace", !player->faceUp());
		return false;
	}
};

QlFangzhuCard::QlFangzhuCard()
{
}

bool QlFangzhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty()&&to_select!=Self;
}

void QlFangzhuCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	if(room->askForChoice(from,getSkillName(),"qlfangzhu1+qlfangzhu2",QVariant::fromValue(to))=="qlfangzhu1"){
		room->setPlayerMark(to,"&qlfangzhu-SelfClear",1);
	}else{
		room->setPlayerCardLimitation(to,"use","^TrickCard|.|.|hand",true);
	}
}

class QlFangzhu : public ZeroCardViewAsSkill
{
public:
	QlFangzhu() : ZeroCardViewAsSkill("qlfangzhu")
	{
		setProperty("IgnoreInvalidity",true);
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("QlFangzhuCard") < 1;
	}

	const Card *viewAs() const
	{
		return new QlFangzhuCard;
	}
};

class QlFangzhuBf : public InvaliditySkill
{
public:
	QlFangzhuBf() : InvaliditySkill("#qlfangzhu_inv")
	{
	}

	bool isSkillValid(const Player *player, const Skill *) const
	{
		if(player->getMark("&mobilemoufangzhu+6-SelfClear")>0) return false;
		return player->getMark("&qlfangzhu-SelfClear")<1;
	}
};

class Weitong : public TriggerSkill
{
public:
	Weitong() : TriggerSkill("weitong$")
	{
		events << MarkChange << GameOverJudge;
		setProperty("IgnoreInvalidity",true);
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==GameOverJudge){
			DeathStruct death = data.value<DeathStruct>();
			if(death.who==player&&player->getGeneralName().startsWith("mobile_caomao")){
				room->doLightbox("image=image/animate/mobile_caomao_death.png",4444);
			}
		}else {
			MarkStruct mark = data.value<MarkStruct>();
			if(mark.name=="&daoxin"&&mark.gain==20&&player->hasLordSkill(this)
				&&player->hasSkill("mobile_qianlong",true)&&room->getLieges("wei",player).length()>0){
				room->sendCompulsoryTriggerLog(player,this);
				mark.count = qMin(99-player->getMark("&daoxin"),60);
				data.setValue(mark);
			}
		}
		return false;
	}
};

QlJuejinCard::QlJuejinCard()
{
	target_fixed = true;
}

void QlJuejinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	foreach (ServerPlayer *p, room->getOtherPlayers(source)){
		room->doAnimate(1,source->objectName(),p->objectName());
	}
	QString gn = source->getGeneralName();
	if(gn.endsWith("caomao")){
		gn = "mobile_caomao2";
		source->setAvatarIcon(gn);
	}
	room->changeBGM("caomaoBGM",true);
	room->doSuperLightbox(gn, "qljuejin");
	room->removePlayerMark(source, "@qljuejin");
	foreach (ServerPlayer *p, room->getAllPlayers()){
		int n = p->getHp()-1;
		if(n>0){
			room->loseHp(p,n,true,source,"qljuejin");
			p->gainHujia(p==source?n+2:n,5);
		}
	}
	room->getThread()->delay();
	room->doLightbox("xiangsicunwei", 2000, 100);
	QList<CardsMoveStruct> moves;
	foreach (int id, Sanguosha->getRandomCards()){
		const Card *c = Sanguosha->getCard(id);
		if(c->isKindOf("Jink")||c->isKindOf("Peach")||c->isKindOf("Analeptic")){
			if(room->getCardPlace(id)!=Player::PlaceSpecial&&room->getCardPlace(id)!=Player::PlaceTable){
				CardsMoveStruct move = CardsMoveStruct(id,nullptr,Player::PlaceTable,CardMoveReason(CardMoveReason::S_MASK_BASIC_REASON, source->objectName()));
				move.reason.m_skillName = "xiangsicunwei";
				moves << move;
			}
		}
	}
	room->setTag("xiangsicunwei",true);
	room->moveCardsAtomic(moves,true);
}

class QlJuejinVs : public ZeroCardViewAsSkill
{
public:
	QlJuejinVs() : ZeroCardViewAsSkill("qljuejin")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@qljuejin")>0;
	}

	const Card *viewAs() const
	{
		return new QlJuejinCard;
	}
};

class QlJuejin : public TriggerSkill
{
public:
	QlJuejin() : TriggerSkill("qljuejin")
	{
		events << CardsMoveOneTime;
		view_as_skill = new QlJuejinVs;
		frequency = Limited;
		limit_mark = "@qljuejin";
		setProperty("IgnoreInvalidity",true);
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place!=Player::PlaceSpecial&&move.to_place!=Player::PlaceTable&&room->getTag("xiangsicunwei").toBool()){
				QList<CardsMoveStruct> moves;
				foreach (int id, move.card_ids){
					const Card *c = Sanguosha->getCard(id);
					if(c->isKindOf("Jink")||c->isKindOf("Peach")||c->isKindOf("Analeptic")){
						if(room->getCardPlace(id)!=Player::PlaceSpecial&&room->getCardPlace(id)!=Player::PlaceTable){
							CardsMoveStruct move1 = CardsMoveStruct(id,nullptr,Player::PlaceTable,CardMoveReason(CardMoveReason::S_MASK_BASIC_REASON, ""));
							move1.reason.m_skillName = "xiangsicunwei";
							moves << move1;
						}
					}
				}
				room->moveCardsAtomic(moves,true);
			}
		}
		return false;
	}
};

class Kuangli : public TriggerSkill
{
public:
	Kuangli() : TriggerSkill("kuangli")
	{
		events << EventPhaseStart << TargetSpecified;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(player->getPhase()!=Player::Play)
			return false;
		if(event==EventPhaseStart){
			room->sendCompulsoryTriggerLog(player,this);
			QList<ServerPlayer *>aps = room->getOtherPlayers(player);
			qShuffle(aps);
			int n = qrand()%aps.length();
			for (int i = 0; i < n; i++) {
				room->doAnimate(1,player->objectName(),aps[i]->objectName());
				room->setPlayerMark(aps[i],"&kuangli-Clear",1);
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach (ServerPlayer *p, use.to){
					if(p!=player&&p->getMark("&kuangli-Clear")>0&&player->isAlive()&&player->getMark("kuangliUse-PlayClear")<2){
						room->sendCompulsoryTriggerLog(player,this);
						player->addMark("kuangliUse-PlayClear");
						QList<const Card *>cs = player->getCards("he");
						qShuffle(cs);
						foreach (const Card *c, cs){
							if(player->canDiscard(player,c->getId())){
								room->throwCard(c,objectName(),player);
								break;
							}
						}
						cs = p->getCards("he");
						qShuffle(cs);
						foreach (const Card *c, cs){
							if(p->canDiscard(p,c->getId())){
								room->throwCard(c,objectName(),p);
								break;
							}
						}
						if(player->isAlive())
							player->drawCards(1,objectName());
					}
				}
			}
		}
		return false;
	}
};

XiongshiCard::XiongshiCard()
{
	target_fixed = true;
}

void XiongshiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	foreach (ServerPlayer *p, room->getOtherPlayers(source)){
		room->doAnimate(1,source->objectName(),p->objectName());
	}
	room->doSuperLightbox(source, "xiongshi");
	room->removePlayerMark(source, "@xiongshi");
	foreach (ServerPlayer *p, room->getOtherPlayers(source)){
		room->loseHp(p,1,true,source,getSkillName());
	}
}

class Xiongshi : public ZeroCardViewAsSkill
{
public:
	Xiongshi() : ZeroCardViewAsSkill("xiongshi")
	{
		frequency = Limited;
		limit_mark = "@xiongshi";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@xiongshi")>0&&player->getHandcardNum()>2;
	}

	const Card *viewAs() const
	{
		XiongshiCard *sc = new XiongshiCard;
		sc->addSubcards(Self->getHandcards());
		return sc;
	}
};

class Pmobileanxiang : public TriggerSkill
{
public:
	Pmobileanxiang() : TriggerSkill("pmobileanxiang")
	{
		events << DamageInflicted;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			foreach (ServerPlayer *p, room->getAllPlayers()){
				if(p->isAlive()&&p->hasSkill(objectName())){
					QString ban = player->getTag("pmobileanxiangChoice").toString();
					QStringList choices;
					choices << "pmobileanxiang1" << "pmobileanxiang2";
					if(!ban.isEmpty()) choices.removeOne(ban);
					ban = "info:"+player->objectName();
					if(choices.length()<2){
						if(choices.contains("pmobileanxiang1"))
							ban = "info1:"+player->objectName();
						else
							ban = "info2:"+player->objectName();
					}
					p->setTag("pmobileanxiangData", data);
					if(p->askForSkillInvoke(this,ban)){
						ban = room->askForChoice(p,objectName(),choices.join("+"),data);
						player->setTag("pmobileanxiangChoice", ban);
						if(ban.contains("1")){
							damage.damage--;
							if(damage.from&&damage.from->isAlive())
								damage.from->drawCards(2,objectName());
						}else{
							damage.damage++;
							if(damage.to->isAlive())
								damage.to->drawCards(3,objectName());
						}
						data.setValue(damage);
					}
				}
			}
			return damage.damage<1;
		}
		return false;
	}
};

class NewChenjie : public TriggerSkill
{
public:
	NewChenjie() : TriggerSkill("newchenjie")
	{
		events << Death;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DeathStruct death = data.value<DeathStruct>();
		if(death.who->getTag("pmobileanxiangChoice").toString()!=""&&player->hasSkill("pmobileanxiang",true)){
			room->sendCompulsoryTriggerLog(player,this);
			player->throwAllCards(objectName());
			if(player->isAlive())
				player->drawCards(4,objectName());
		}
		return false;
	}
};

class ZhujinVs : public ViewAsSkill
{
public:
	ZhujinVs() : ViewAsSkill("zhujin")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *card) const
	{
		return selected.isEmpty()&&card->isKindOf("BasicCard");
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
		if((pattern.isEmpty()||pattern.contains("slash"))&&Self->getMark("slashzhujin-Clear")<1){
			foreach (const Player *p, Self->getAliveSiblings()){
				if(Self->getHp()>p->getHp()){
					Card*dc = Sanguosha->cloneCard("slash");
					dc->setSkillName(objectName());
					dc->addSubcards(cards);
					return dc;
				}
			}
			if(!Self->isWounded()){
				Card*dc = Sanguosha->cloneCard("slash");
				dc->setSkillName(objectName());
				dc->addSubcards(cards);
				return dc;
			}
		}
		if(pattern.contains("jink")&&Self->getMark("jinkzhujin-Clear")<1){
			if(Self->isWounded()){
				Card*dc = Sanguosha->cloneCard("jink");
				dc->setSkillName(objectName());
				dc->addSubcards(cards);
				return dc;
			}
		}
		if(pattern.contains("nullification")&&Self->getMark("nullificationzhujin-Clear")<1){
			if(Self->isWounded()){
				Card*dc = Sanguosha->cloneCard("nullification");
				dc->setSkillName(objectName());
				dc->addSubcards(cards);
				return dc;
			}
		}
		return nullptr;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if(player->getMark("slashzhujin-Clear")<1){
			bool can = !player->isWounded();
			foreach (const Player *p, player->getAliveSiblings()){
				if(player->getHp()>p->getHp())
					can = true;
			}
			return can&&Slash::IsAvailable(player);
		}
		return false;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if(pattern.contains("slash")&&player->getMark("slashzhujin-Clear")<1){
			foreach (const Player *p, player->getAliveSiblings()){
				if(player->getHp()>p->getHp())
					return true;
			}
			if(!player->isWounded())
				return true;
		}
		if(pattern.contains("jink")&&player->getMark("jinkzhujin-Clear")<1){
			if(player->isWounded())
				return true;
		}
		if(pattern.contains("nullification")&&player->getMark("nullificationzhujin-Clear")<1){
			if(player->isWounded())
				return true;
		}
		return false;
	}
};

class Zhujin : public TriggerSkill
{
public:
	Zhujin() : TriggerSkill("zhujin")
	{
		events << PreCardUsed;
		view_as_skill = new ZhujinVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive() && target->hasSkill(objectName(),true);
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->getSkillNames().contains(objectName()))
			room->addPlayerMark(player,use.card->objectName()+"zhujin-Clear");
		return false;
	}
};

JiejianCard::JiejianCard()
{
}

static QHash<QString, int> jiejianNum;

bool JiejianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if(to_select!=Self&&targets.length()<subcardsLength()){
		int n = 0;
		foreach (const Player *p, targets){
			n += jiejianNum[p->objectName()];
		}
		jiejianNum[to_select->objectName()] = subcardsLength()-n;
		return true;
	}
	return false;
}

bool JiejianCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	int n = 0;
	foreach (const Player *p, targets){
		n += jiejianNum[p->objectName()];
	}
	return n == subcardsLength();
}

void JiejianCard::onUse(Room *room, CardUseStruct &use) const
{
	foreach (ServerPlayer *p, use.to){
		room->setPlayerMark(p,"&jiejianNum",jiejianNum[p->objectName()]);
	}
}

class JiejianVs : public ViewAsSkill
{
public:
	JiejianVs() : ViewAsSkill("jiejian")
	{
	}

	bool viewFilter(const QList<const Card *> &, const Card *card) const
	{
		return !card->isEquipped();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		JiejianCard *sc = new JiejianCard;
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@jiejian";
	}
};

class Jiejian : public TriggerSkill
{
public:
	Jiejian() : TriggerSkill("jiejian")
	{
		events << EventPhaseStart << EventPhaseChanging << TargetConfirming;
		//view_as_skill = new JiejianVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::NotActive&&player->getMark("&jiejian")>0){
				player->loseMark("&jiejian");
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->hasSkill(objectName(),true)){
						if(player->getMark("jiejianHp+#"+p->objectName())<=player->getHp()){
							room->sendCompulsoryTriggerLog(p,this);
							p->drawCards(2,objectName());
						}
					}
				}
			}
		}else if(event==TargetConfirming){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&use.to.size()==1){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(player->getMark("&jiejian")>0&&p->hasSkill(objectName())&&p->askForSkillInvoke(this,data)){
						p->peiyin(this);
						use.to.removeOne(player);
						use.to.append(p);
						data.setValue(use);
						p->drawCards(1,objectName());
					}
				}
			}
		}else if(player->getPhase()==Player::Start&&player->getHandcardNum()>0&&player->hasSkill(objectName())&&player->askForSkillInvoke(this)){
			player->peiyin(this);
			QList<int> ids = player->handCards();
			QList<ServerPlayer *> tos = player->assignmentCards(ids,"jiejian",room->getOtherPlayers(player),-1,1);
			foreach (ServerPlayer *p, tos){
				p->gainMark("&jiejian");
				room->setPlayerMark(p,"jiejianHp+#"+player->objectName(),p->getHp());
			}
		}
		return false;
	}
};

class Jueyong : public TriggerSkill
{
public:
	Jueyong() : TriggerSkill("jueyong")
	{
		events << EventPhaseStart << TargetConfirming;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==TargetConfirming){
			CardUseStruct use = data.value<CardUseStruct>();
			if(!use.card->isVirtualCard()&&use.to.size()==1){
				if(use.card->isKindOf("Peach")||use.card->isKindOf("Analeptic")||use.card->hasFlag("jueyongUse"))
					return false;
				QList<int>ids = player->getPile("jue_yong");
				if(ids.length()>=player->getHp()) return false;
				room->sendCompulsoryTriggerLog(player,this);
				player->setTag("jueyong"+use.card->toString(), data);
				player->addToPile("jue_yong",use.card);
				use.to.removeAll(player);
				data.setValue(use);
			}
		}else if(player->getPhase()==Player::Finish){
			QList<int>ids = player->getPile("jue_yong");
			if(ids.isEmpty()) return false;
			room->sendCompulsoryTriggerLog(player,this);
			foreach (int id, ids){
				CardUseStruct use = player->getTag("jueyong"+QString::number(id)).value<CardUseStruct>();
				if(use.from&&use.from->isAlive()){
					use.card->setFlags("jueyongUse");
					room->useCard(use);
				}else
					room->throwCard(id,objectName(),nullptr);
				if(player->isDead()) break;
			}
		}
		return false;
	}
};

PoxiangCard::PoxiangCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool PoxiangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty()&&to_select!=Self;
}

void PoxiangCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	room->giveCard(from,to,this,getSkillName());
	if(from->isDead()) return;
	room->ignoreCards(from,room->drawCardsList(from,3,getSkillName()));
	DummyCard *dummy = new DummyCard();
	dummy->addSubcards(from->getPile("jue_yong"));
	if(dummy->subcardsLength()>0)
		room->throwCard(dummy,getSkillName(),nullptr);
	dummy->deleteLater();
	room->loseHp(from,1,true,from,getSkillName());
}

class Poxiang : public ViewAsSkill
{
public:
	Poxiang() : ViewAsSkill("poxiang")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *) const
	{
		return selected.isEmpty();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		PoxiangCard *sc = new PoxiangCard;
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("PoxiangCard")<1&&player->getCardCount()>0;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@poxiang";
	}
};

ZhujianCard::ZhujianCard()
{
}

bool ZhujianCard::targetFilter(const QList<const Player *> &, const Player *to_select, const Player *) const
{
	return to_select->hasEquip();
}

bool ZhujianCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length()>1;
}

void ZhujianCard::onEffect(CardEffectStruct &effect) const
{
	effect.to->drawCards(1,getSkillName());
}

class Zhujian : public ZeroCardViewAsSkill
{
public:
	Zhujian() : ZeroCardViewAsSkill("zhujian")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("ZhujianCard")<1;
	}

	const Card *viewAs() const
	{
		return new ZhujianCard;
	}
};

DuansuoCard::DuansuoCard()
{
}

bool DuansuoCard::targetFilter(const QList<const Player *> &, const Player *to, const Player *) const
{
	return to->isChained();
}

void DuansuoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		if (p->isChained())
			room->setPlayerChained(p);
	}
	foreach (ServerPlayer *p, targets) {
		if (p->isAlive())
			room->damage(DamageStruct(getSkillName(),source,p,1,DamageStruct::Fire));
	}
}

class Duansuo : public ZeroCardViewAsSkill
{
public:
	Duansuo() : ZeroCardViewAsSkill("duansuo")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("DuansuoCard")<1;
	}

	const Card *viewAs() const
	{
		return new DuansuoCard;
	}
};

class Chengye : public TriggerSkill
{
public:
	Chengye() : TriggerSkill("chengye")
	{
		events << EventPhaseStart << CardsMoveOneTime << CardFinished;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from&&move.to_place==Player::DiscardPile&&move.from!=player&&player->hasSkill(objectName())){
				int i = 0;
				foreach (int id, move.card_ids){
					if(move.from_places.at(i)==Player::PlaceHand
					||move.from_places.at(i)==Player::PlaceEquip
					||move.from_places.at(i)==Player::PlaceDelayedTrick){
						if(room->getCardPlace(id)==Player::DiscardPile){
							const Card *c = Sanguosha->getCard(id);
							if(c->isKindOf("EquipCard")){
								bool can = true;
								foreach (int d, player->getPile("cy_dian")){
									if(Sanguosha->getCard(d)->isKindOf("EquipCard"))
										can = false;
								}
								if(can){
									room->sendCompulsoryTriggerLog(player,this);
									player->addToPile("cy_dian",c);
								}
							}else if(c->isKindOf("DelayedTrick")){
								bool can = c->isKindOf("Indulgence");
								foreach (int d, player->getPile("cy_dian")){
									if(Sanguosha->getCard(d)->isKindOf("Indulgence"))
										can = false;
								}
								if(can){
									room->sendCompulsoryTriggerLog(player,this);
									player->addToPile("cy_dian",c);
								}
								can = c->isDamageCard();
								foreach (int d, player->getPile("cy_dian")){
									if(Sanguosha->getCard(d)->isDamageCard())
										can = false;
								}
								if(can){
									room->sendCompulsoryTriggerLog(player,this);
									player->addToPile("cy_dian",c);
								}
							}
						}
					}
					i++;
				}
			}
		}else if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isVirtualCard()||room->getCardOwner(use.card->getEffectiveId())) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)){
				if(p->isAlive()&&p->hasSkill(objectName())){
					bool can = use.card->isKindOf("TrickCard")&&use.card->isDamageCard();
					foreach (int d, p->getPile("cy_dian")){
						if(Sanguosha->getCard(d)->isKindOf("TrickCard")&&Sanguosha->getCard(d)->isDamageCard())
							can = false;
					}
					if(can){
						room->sendCompulsoryTriggerLog(p,this);
						p->addToPile("cy_dian",use.card);
						break;
					}
					can = use.card->isKindOf("BasicCard");
					foreach (int d, p->getPile("cy_dian")){
						if(Sanguosha->getCard(d)->isKindOf("BasicCard"))
							can = false;
					}
					if(can){
						room->sendCompulsoryTriggerLog(p,this);
						p->addToPile("cy_dian",use.card);
						break;
					}
					can = use.card->isKindOf("Nullification");
					foreach (int d, p->getPile("cy_dian")){
						if(Sanguosha->getCard(d)->isKindOf("Nullification"))
							can = false;
					}
					if(can){
						room->sendCompulsoryTriggerLog(p,this);
						p->addToPile("cy_dian",use.card);
						break;
					}
					can = use.card->isKindOf("ExNihilo");
					foreach (int d, p->getPile("cy_dian")){
						if(Sanguosha->getCard(d)->isKindOf("ExNihilo"))
							can = false;
					}
					if(can){
						room->sendCompulsoryTriggerLog(p,this);
						p->addToPile("cy_dian",use.card);
						break;
					}
					can = use.card->isKindOf("Indulgence");
					foreach (int d, p->getPile("cy_dian")){
						if(Sanguosha->getCard(d)->isKindOf("Indulgence"))
							can = false;
					}
					if(can){
						room->sendCompulsoryTriggerLog(p,this);
						p->addToPile("cy_dian",use.card);
						break;
					}
					can = use.card->isKindOf("EquipCard");
					foreach (int d, p->getPile("cy_dian")){
						if(Sanguosha->getCard(d)->isKindOf("EquipCard"))
							can = false;
					}
					if(can){
						room->sendCompulsoryTriggerLog(p,this);
						p->addToPile("cy_dian",use.card);
						break;
					}
				}
			}
		}else if(player->getPhase()==Player::Play&&player->hasSkill(objectName())){
			QList<int>ids = player->getPile("cy_dian");
			if(ids.length()<6) return false;
			room->sendCompulsoryTriggerLog(player,this);
			DummyCard *dummy = new DummyCard(ids);
			player->obtainCard(dummy);
			dummy->deleteLater();
		}
		return false;
	}
};

BuxuCard::BuxuCard()
{
	target_fixed = true;
}

void BuxuCard::use(Room *room, ServerPlayer *player, QList<ServerPlayer *> &) const
{
	QStringList choices;
	bool can = true;
	foreach (int d, player->getPile("cy_dian")){
		if(Sanguosha->getCard(d)->isKindOf("TrickCard")&&Sanguosha->getCard(d)->isDamageCard())
			can = false;
	}
	if(can){
		choices << "d_shi";
	}
	can = true;
	foreach (int d, player->getPile("cy_dian")){
		if(Sanguosha->getCard(d)->isKindOf("BasicCard"))
			can = false;
	}
	if(can){
		choices << "d_shu";
	}
	can = true;
	foreach (int d, player->getPile("cy_dian")){
		if(Sanguosha->getCard(d)->isKindOf("Nullification"))
			can = false;
	}
	if(can){
		choices << "d_li";
	}
	can = true;
	foreach (int d, player->getPile("cy_dian")){
		if(Sanguosha->getCard(d)->isKindOf("Indulgence"))
			can = false;
	}
	if(can){
		choices << "d_yue";
	}
	can = true;
	foreach (int d, player->getPile("cy_dian")){
		if(Sanguosha->getCard(d)->isKindOf("ExNihilo"))
			can = false;
	}
	if(can){
		choices << "d_yi";
	}
	can = true;
	foreach (int d, player->getPile("cy_dian")){
		if(Sanguosha->getCard(d)->isKindOf("EquipCard"))
			can = false;
	}
	if(can){
		choices << "d_chunqiu";
	}
	if(choices.isEmpty()||player->isDead()) return;
	QString choice = room->askForChoice(player,getSkillName(),choices.join("+"));
	foreach (int id, room->getDrawPile()+room->getDiscardPile()){
		const Card *c = Sanguosha->getCard(id);
		if(choice=="d_shi"&&c->isKindOf("TrickCard")&&c->isDamageCard()){
			player->addToPile("cy_dian",c);
			break;
		}else if(choice=="d_shu"&&c->isKindOf("BasicCard")){
			player->addToPile("cy_dian",c);
			break;
		}else if(choice=="d_li"&&c->isKindOf("Nullification")){
			player->addToPile("cy_dian",c);
			break;
		}else if(choice=="d_yue"&&c->isKindOf("Indulgence")){
			player->addToPile("cy_dian",c);
			break;
		}else if(choice=="d_yi"&&c->isKindOf("ExNihilo")){
			player->addToPile("cy_dian",c);
			break;
		}else if(choice=="d_chunqiu"&&c->isKindOf("EquipCard")){
			player->addToPile("cy_dian",c);
			break;
		}
	}
}

class Buxu : public ViewAsSkill
{
public:
	Buxu() : ViewAsSkill("buxu")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *c) const
	{
		return selected.length()<=Self->usedTimes("BuxuCard")&&!Self->isJilei(c);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.length()<=Self->usedTimes("BuxuCard")) return nullptr;
		BuxuCard *sc = new BuxuCard;
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("BuxuCard")<player->getCardCount()
			&&player->getPile("cy_dian").length()<6&&player->hasSkill("chengye",true);
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@@buxu";
	}
};

class Mingcha : public TriggerSkill
{
public:
	Mingcha() : TriggerSkill("mingcha")
	{
		events << EventPhaseStart;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(player->getPhase()==Player::Draw){
			room->sendCompulsoryTriggerLog(player,objectName());
			QList<int>ids = room->showDrawPile(player,3,objectName());
			DummyCard *dummy = new DummyCard();
			foreach (int id, ids){
				if(Sanguosha->getCard(id)->getNumber()<=8)
					dummy->addSubcard(id);
			}
			room->getThread()->delay();
			if(dummy->subcardsLength()>0&&player->askForSkillInvoke(this,dummy->subcardsLength())){
				foreach (int id, dummy->getSubcards())
					ids.removeOne(id);
				player->peiyin(this);
				player->obtainCard(dummy);
				if(player->isAlive()){
					ServerPlayer *to = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"mingcha0:");
					if(to){
						room->doAnimate(1,player->objectName(),to->objectName());
						QList<int> toids = to->handCards()+to->getEquipsId();
						if(toids.length()>0){
							room->obtainCard(player,toids.at(qrand()%toids.length()));
						}
					}
				}
			}
			dummy->deleteLater();
			dummy = new DummyCard(ids);
			room->throwCard(dummy,objectName(),nullptr);
			dummy->deleteLater();
			return ids.length()<3;
		}
		return false;
	}
};

class Jingzhong : public TriggerSkill
{
public:
	Jingzhong() : TriggerSkill("jingzhong")
	{
		events << CardsMoveOneTime << EventPhaseEnd << CardFinished;
		global = true;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from==player&&player->getPhase()==Player::Discard&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD) {
				QVariantList ids = player->getTag("jingzhongIds").toList();
				foreach (int card_id, move.card_ids)
					ids << card_id;
				player->setTag("jingzhongIds", ids);
			}
		}else if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(!player->hasFlag("CurrentPlayer")||player->getPhase()!=Player::Play
				||room->getCardPlace(use.card->getEffectiveId())!=Player::DiscardPile) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (player->getMark("&jingzhong+#"+p->objectName()+"-SelfClear")>0&&p->getMark("jingzhongOC-PlayClear")<3){
					p->addMark("jingzhongOC-PlayClear");
					p->obtainCard(use.card);
					break;
				}
			}
		} else if (triggerEvent == EventPhaseEnd&&player->getPhase()==Player::Discard) {
			QVariantList Vids = player->getTag("jingzhongIds").toList();
			player->removeTag("jingzhongIds");
			if (player->isDead()||!player->hasSkill(objectName()))
				return false;
			QVariantList ids;
			foreach (QVariant v, Vids) {
				if (!ids.contains(v)&&Sanguosha->getCard(v.toInt())->isBlack())
					ids << v;
			}
			if (ids.length()<2) return false;
			ServerPlayer *to = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"jingzhong0:",true,true);
			if(to){
				room->broadcastSkillInvoke(objectName());
				room->setPlayerMark(to,"&jingzhong+#"+player->objectName()+"-SelfClear",1);
			}
		}
		return false;
	}
};

QuchongCard::QuchongCard()
{
	target_fixed = true;
	will_throw = false;
	can_recast = true;
	handling_method = Card::MethodRecast;
}

void QuchongCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	LogMessage log;
	log.type = "#UseCard_Recast";
	log.from = source;
	log.card_str = QString::number(getSubcards().first());
	room->sendLog(log);
	room->moveCardTo(this, source, nullptr, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_RECAST, source->objectName(), getSkillName(), ""));
	source->drawCards(1, "recast");
}

class QuchongVs : public OneCardViewAsSkill
{
public:
	QuchongVs() : OneCardViewAsSkill("quchong")
	{
		filter_pattern = "EquipCard";
	}

	const Card *viewAs(const Card *c) const
	{
		QuchongCard *card = new QuchongCard;
		card->addSubcard(c);
		return card;
	}
};

class Quchong : public TriggerSkill
{
public:
	Quchong() : TriggerSkill("quchong")
	{
		events << EventPhaseStart << EventPhaseChanging;
		waked_skills = "_dagongche_jinji,_dagongche_shouyu";
		view_as_skill = new QuchongVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::NotActive){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->hasSkill(objectName())){
						DummyCard*dc = new DummyCard();
						foreach (int id, room->getDiscardPile()){
							if(Sanguosha->getCard(id)->isKindOf("EquipCard"))
								dc->addSubcard(id);
						}
						if(dc->subcardsLength()>0){
							room->sendCompulsoryTriggerLog(p,this);
							room->moveCardTo(dc,nullptr,Player::PlaceTable);
							p->gainMark("&zhuzhaoNum",dc->subcardsLength());
						}
						dc->deleteLater();
					}
				}
			}
		}else if(player->getPhase()==Player::Play&&player->hasSkill(objectName())){
			const Card *ec = nullptr;
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				foreach (const Card *c, p->getCards("ej")){
					if(c->objectName().contains("dagongche"))
						ec = c;
				}
			}
			if(ec){
				ServerPlayer *to = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"quchong0:",true,true);
				if(to){
					player->peiyin(this);
					room->giveCard(player,to,ec,objectName());
					if(to->hasCard(ec)&&ec->isAvailable(to))
						room->useCard(CardUseStruct(ec,to));
				}
			}else if(player->getMark("&zhuzhaoNum")>=player->getMark("&quchong")&&player->askForSkillInvoke(this)){
				player->peiyin(this);
				int n = player->getMark("&quchong");
				player->loseMark("&zhuzhaoNum",n);
				n += 5;
				QStringList cht;
				QList<int>ids;
				room->setPlayerMark(player,"&quchong",qMin(n,10));
				foreach (int id, Sanguosha->getRandomCards(true)){
					ec = Sanguosha->getCard(id);
					if(ec->isKindOf("EquipCard")&&ec->objectName().contains("dagongche")&&room->getCardPlace(id)==Player::PlaceTable){
						if(!cht.contains(ec->objectName())){
							cht.append(ec->objectName());
							ids << id;
						}
					}
				}
				if(ids.isEmpty()) return false;
				room->fillAG(ids,player);
				n = room->askForAG(player,ids,ids.length()<2,objectName());
				if (n<0) n = ids.first();
				room->clearAG(player);
				cht.clear();
				cht << Sanguosha->getCard(n)->objectName();
				cht << Card::Suit2String(room->askForSuit(player,objectName()));
				cht << room->askForChoice(player,objectName(),"1+2+3+4+5+6+7+8+9+10+11+12+13");
				cht << "quchong";
				foreach (ServerPlayer *p, room->getPlayers())
					room->acquireSkill(p, "#zhizhe");
				n = -1;
				foreach (int id, Sanguosha->getRandomCards(true)){
					ec = Sanguosha->getCard(id);
					if(ec->isKindOf("EquipCard")&&ec->objectName().contains("_zhizhe_")&&room->getCardPlace(id)==Player::PlaceTable){
						n = id;
						break;
					}
				}
				if(n>-1){
					ServerPlayer *to = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"quchong1:");
					if(to){
						room->setTag("ZhizheFilter_"+QString::number(n),cht.join("+"));
						room->setTag("dagongche"+QString::number(n),player->objectName());
						room->doAnimate(1,player->objectName(),to->objectName());
						room->giveCard(player,to,ec,objectName());
						ec = Sanguosha->getCard(n);
						if(to->hasCard(ec)&&ec->isAvailable(to))
							room->useCard(CardUseStruct(ec,to));
					}
				}
			}
		}
		return false;
	}
};

class Xunjie : public TriggerSkill
{
public:
	Xunjie() : TriggerSkill("xunjie")
	{
		events << DamageInflicted;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from&&damage.from->getHp()>player->getHp()){
				foreach (ServerPlayer *p, room->getAlivePlayers()){
					foreach (const Card *c, p->getCards("ej")){
						if(c->objectName().contains("dagongche")&&room->getTag("dagongche"+c->toString()).toString()==player->objectName())
							return false;
					}
				}
				room->sendCompulsoryTriggerLog(player,this);
				JudgeStruct judge;
				judge.who = player;
				judge.reason = objectName();
				judge.pattern = ".|spade";
				judge.good = false;
				room->judge(judge);
				if(judge.isGood())
					return player->damageRevises(data,-1);
			}
		}
		return false;
	}
};

class Bojian : public TriggerSkill
{
public:
	Bojian() : TriggerSkill("bojian")
	{
		events << EventPhaseEnd << CardUsed;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive()&&target->getPhase()==Player::Play;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==EventPhaseEnd){
			QStringList sus = player->getTag("bojianSuit").toStringList();
			int n = player->getMark("bojianUse-PlayClear");
			if(player->hasSkill(objectName())){
				if(player->getMark("bojianSuit")!=sus.size()&&n!=player->getMark("bojianUse")){
					room->sendCompulsoryTriggerLog(player,this);
					player->drawCards(2,objectName());
				}else{
					QList<int> ids,ids2 = ListV2I(player->getTag("bojianIds").toList());
					foreach (int id, room->getDiscardPile()){
						if(ids2.contains(id))
							ids << id;
					}
					if(ids.length()>0){
						room->sendCompulsoryTriggerLog(player,this);
						room->fillAG(ids,player);
						int id = room->askForAG(player,ids,ids.length()<2,objectName());
						if(id<0) id = ids.first();
						room->clearAG(player);
						ServerPlayer *to = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"bojian0:");
						if(to){
							room->doAnimate(1,player->objectName(),to->objectName());
							room->giveCard(player,to,Sanguosha->getCard(id),objectName());
						}
					}
				}
				foreach (QString m, player->getMarkNames()){
					if(m.contains("&bojian+use+"))
						room->setPlayerMark(player,m,0);
				}
				room->setPlayerMark(player,"&bojian+use+"+QString::number(n)+"+suit+"+QString::number(sus.size()),1);
			}
			player->setMark("bojianUse",n);
			player->setMark("bojianSuit",sus.size());
			player->removeTag("bojianSuit");
			player->removeTag("bojianIds");
		}else if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				player->addMark("bojianUse-PlayClear");
				QStringList sus = player->getTag("bojianSuit").toStringList();
				if(!sus.contains(use.card->getSuitString())) sus << use.card->getSuitString();
				player->setTag("bojianSuit", sus);
				if(player->hasSkill(objectName(),true)){
					int n = player->getMark("bojianUse-PlayClear");
					foreach (QString m, player->getMarkNames()){
						if(m.contains("&bojian2+use+"))
							room->setPlayerMark(player,m,0);
					}
					room->setPlayerMark(player,"&bojian2+use+"+QString::number(n)+"+suit+"+QString::number(sus.size())+"-PlayClear",1);
				}
				QVariantList ids = player->getTag("bojianIds").toList();
				foreach (int id, use.card->getSubcards()){
					ids << id;
				}
				player->setTag("bojianIds", ids);
			}
		}
		return false;
	}
};

class Jiwei : public TriggerSkill
{
public:
	Jiwei() : TriggerSkill("jiwei")
	{
		events << EventPhaseStart << EventPhaseChanging << DamageDone << CardsMoveOneTime;
		frequency = Compulsory;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Start&&player->isAlive()){
				int r = 0, b = player->getHandcardNum();
				if(b>=room->getAlivePlayers().length()&&b>=player->getHp()&&player->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(player,this);
					b = 0;
					foreach (const Card *h, player->getHandcards()){
						if(h->isRed()) r++;
						else if(h->isBlack()) b++;
					}
					QString rb = "red+black";
					if(r>b) rb = "red";
					else if(r<b) rb = "black";
					else rb = room->askForChoice(player,objectName(),rb);
					QList<int>ids;
					foreach (const Card *h, player->getHandcards()){
						if(h->getColorString()==rb) ids << h->getId();
					}
					player->assignmentCards(ids,objectName(),room->getOtherPlayers(player),ids.length(),ids.length());
				}
			}
		}else if(event==EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to==Player::NotActive) {
				int n = 0;
				if(room->getTag("jiweiDamage").toBool()) n++;
				if(room->getTag("jiweiMove").toBool()) n++;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(n>0&&p->hasSkill(objectName())){
						room->sendCompulsoryTriggerLog(p,this);
						p->drawCards(n,objectName());
					}
				}
				room->setTag("jiweiDamage",false);
				room->setTag("jiweiMove",false);
			}
		}else if(event==DamageDone){
			room->setTag("jiweiDamage",true);
		}else if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from==player&&(move.to!=player||(move.to_place!=Player::PlaceHand&&move.to_place!=Player::PlaceEquip)))
				room->setTag("jiweiMove",true);
		}
		return false;
	}
};

class Biluan : public PhaseChangeSkill
{
public:
	Biluan() : PhaseChangeSkill("biluan")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if(player->getPhase()==Player::Draw){
			foreach (ServerPlayer *p, room->getOtherPlayers(player)){
				if(p->distanceTo(player)==1){
					if (player->askForSkillInvoke(this)){
						room->broadcastSkillInvoke(objectName());
						room->addPlayerMark(player, "biluanUse");
						return true;
					}
					break;
				}
			}
		}
		return false;
	}
};

class BiluanDist : public DistanceSkill
{
public:
	BiluanDist() : DistanceSkill("#biluan-dist")
	{
	}

	int getCorrect(const Player *, const Player *to) const
	{
		int n = -to->getMark("lixiaUse");
		if (to->getMark("biluanUse")>0){
			QSet<QString> kingdoms;
			foreach(const Player *p, to->getAliveSiblings(true))
				kingdoms.insert(p->getKingdom());
			n += kingdoms.count()*to->getMark("biluanUse");
		}
		return n;
	}
};

class Lixia : public PhaseChangeSkill
{
public:
	Lixia() : PhaseChangeSkill("lixia")
	{
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool onPhaseChange(ServerPlayer *target, Room *room) const
	{
		if(target->getPhase()==Player::Finish){
			foreach(ServerPlayer *p, room->getOtherPlayers(target)){
				if (p->hasSkill(objectName()) && !target->inMyAttackRange(p)){
					room->sendCompulsoryTriggerLog(p,this);
					p->drawCards(1, objectName());
					room->addPlayerMark(p, "lixiaUse");
				}
			}
		}
		return false;
	}
};

MobileJiyuCard::MobileJiyuCard()
{
	target_fixed = true;
}

void MobileJiyuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	const Card*sc = Sanguosha->getCard(getEffectiveId());
	QList<int>ids = room->getDiscardPile();
	ids << room->getDrawPile();
	qShuffle(ids);
	QStringList types;
	types << sc->getType();
	Card*dc = new DummyCard;
	foreach (int id, ids){
		const Card *c = Sanguosha->getCard(id);
		if(types.contains(c->getType())) continue;
		types << c->getType();
		dc->addSubcard(id);
	}
	source->obtainCard(dc);
	dc->deleteLater();
	foreach (int id, dc->getSubcards()){
		if(source->handCards().contains(id))
			room->setCardTip(id,"mobilejiyu-PlayClear");
	}
}

class MobileJiyuVs : public OneCardViewAsSkill
{
public:
	MobileJiyuVs() : OneCardViewAsSkill("mobilejiyu")
	{
		filter_pattern = ".|.|.|hand";
	}

	const Card *viewAs(const Card *c) const
	{
		MobileJiyuCard *card = new MobileJiyuCard;
		card->addSubcard(c);
		return card;
	}
	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("MobileJiyuCard");
	}
};

class MobileJiyu : public TriggerSkill
{
public:
	MobileJiyu() : TriggerSkill("mobilejiyu")
	{
		events << PreCardUsed;
		view_as_skill = new MobileJiyuVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->getPhase()==Player::Play;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasTip("mobilejiyu")){
				player->addMark("sgsmobilejiyuUse-PlayClear");
				if(player->getMark("sgsmobilejiyuUse-PlayClear")==2)
					room->addPlayerHistory(player,"MobileJiyuCard",-1);
			}
		}
		return false;
	}
};

class Guansha : public TriggerSkill
{
public:
	Guansha() : TriggerSkill("guansha")
	{
		events << EventPhaseEnd;
		limit_mark = "@guansha";
		frequency = Limited;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(player->getPhase()==Player::Play&&player->getMark("@guansha")>0
		&&player->getCardCount()>0&&player->askForSkillInvoke(this,data)){
			player->peiyin(this);
			room->doSuperLightbox(player, objectName());
			room->removePlayerMark(player, "@guansha");
			QList<int>ids = room->getDrawPile();
			qShuffle(ids);
			QStringList cns;
			Card*dc = new DummyCard;
			foreach (int id, ids){
				const Card*c = Sanguosha->getCard(id);
				if(c->isKindOf("BasicCard")){
					dc->addSubcard(id);
					if(!cns.contains(c->objectName())) cns << c->objectName();
					if(dc->subcardsLength()>=player->getCardCount()) break;
				}
			}
			ids = player->handCards()+player->getEquipsId();
			room->moveCardsInToDrawpile(player,ids,objectName());
			player->obtainCard(dc,false);
			room->addMaxCards(player,cns.size());
			dc->deleteLater();
		}
		return false;
	}
};

MZengouCard::MZengouCard()
{
}

bool MZengouCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to!=Self&&!to->isKongcheng()&&to->getMark(Self->objectName()+"mzengouBan")<1;
}

void MZengouCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		room->doGongxin(source,p,p->handCards(),"mzengou");
		QList<int>ids = room->getAvailableCardList(source,"basic","mzengou");
		QStringList choices,cns;
		foreach (int id, ids) {
			const Card*c = Sanguosha->getCard(id);
			foreach (const Card*h, p->getHandcards()) {
				if(c->sameNameWith(h)){
					ids.removeOne(id);
					break;
				}
			}
			if(ids.contains(id))
				choices << "mzengou1="+c->objectName();
		}
		foreach (const Card*c, source->getHandcards()) {
			foreach (const Card*h, p->getHandcards()) {
				if(c->sameNameWith(h)){
					cns << c->objectName();
					break;
				}
			}
		}
		if(cns.length()>0) choices << "mzengou2";
		if(choices.isEmpty()) continue;
		QString choice = room->askForChoice(source,"mzengou",choices.join("+"));
		if(choice!="mzengou2"){
			choice = choice.split("=").last();
			room->setPlayerProperty(source,"mzengouUse",choice);
			room->askForUseCard(source,"@@mzengou!","mzengou0:"+choice,-1,Card::MethodUse,false);
		}else{
			QList<CardsMoveStruct> moves;
			CardMoveReason reason(CardMoveReason::S_REASON_RECYCLE, source->objectName(), "mzengou", "");
			foreach (const Card*c, source->getHandcards()) {
				if(cns.contains(c->objectName())){
					CardsMoveStruct move(c->getId(), source, nullptr, Player::PlaceHand, Player::DrawPile, reason);
					moves << move;
				}
			}
			ids.clear();
			int n = moves.length()*2;
			foreach (int id, room->getDrawPile()) {
				const Card*c = Sanguosha->getCard(id);
				if(c->isKindOf("Slash")){
					CardsMoveStruct move(id, nullptr,source, Player::DrawPile, Player::PlaceHand, reason);
					moves << move;
					ids << id;
					if(moves.length()>=n) break;
				}
			}
			room->moveCardsAtomic(moves,false);
			foreach (int id, ids) {
				if(source->handCards().contains(id)){
					room->ignoreCards(source,id);
					room->setCardTip(id,"mzengou-SelfClear");
				}
			}
			if(p->isDead()) continue;
			moves.clear();
			reason.m_playerId = p->objectName();
			foreach (const Card*c, p->getHandcards()) {
				if(cns.contains(c->objectName())){
					CardsMoveStruct move(c->getId(), p, nullptr, Player::PlaceHand, Player::DrawPile, reason);
					moves << move;
				}
			}
			n = moves.length()*2;
			foreach (int id, room->getDrawPile()) {
				const Card*c = Sanguosha->getCard(id);
				if(c->isKindOf("Slash")){
					CardsMoveStruct move(id, nullptr,p, Player::DrawPile, Player::PlaceHand, reason);
					moves << move;
					ids << id;
					if(moves.length()>=n) break;
				}
			}
			room->moveCardsAtomic(moves,false);
			foreach (int id, ids) {
				if(p->handCards().contains(id)){
					room->ignoreCards(p,id);
					room->setCardTip(id,"mzengou-SelfClear");
				}
			}
		}
		if(source->isDead()||p->isDead()) continue;
		cns = Sanguosha->getCardNames("BasicCard");
		choice = room->askForChoice(source,"mzengou",cns.join("+"));
		room->setPlayerMark(p,"&mzengou_wu+:+"+choice,1);
	}
}

class MZengouVs : public ZeroCardViewAsSkill
{
public:
	MZengouVs() : ZeroCardViewAsSkill("mzengou")
	{
		response_pattern = "@@mzengou!";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("MZengouCard")<1;
	}

	const Card *viewAs() const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUsePattern() == "@@mzengou!"){
			Card*dc = Sanguosha->cloneCard(Self->property("mzengouUse").toString());
			dc->setSkillName("_mzengou");
			return dc;
		}
		return new MZengouCard;
	}
};

class MZengou : public TriggerSkill
{
public:
	MZengou() : TriggerSkill("mzengou")
	{
		events << CardFinished;
		view_as_skill = new MZengouVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getMark("mzengou-Clear")<1){
				player->addMark("mzengou-Clear");
				if(player->getMark("&mzengou_wu+:+"+use.card->objectName())>0){
					room->sendCompulsoryTriggerLog(player,objectName());
					room->loseHp(player,1,true,player,objectName());
					room->setPlayerMark(player,"&mzengou_wu+:+"+use.card->objectName(),0);
				}
			}
		}
		return false;
	}
};

class Feili : public TriggerSkill
{
public:
	Feili() : TriggerSkill("feili")
	{
		events << DamageInflicted;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == DamageInflicted&&player->hasSkill("mzengou",true)) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from) {
				foreach (QString m, damage.from->getMarkNames()) {
					if(m.contains("&mzengou_wu+:+")&&damage.from->getMark(m)>0){
						if(player->askForSkillInvoke(this,damage.from)){
							player->peiyin(this);
							room->setPlayerMark(damage.from,m,0);
							player->damageRevises(data,-damage.damage);
							player->drawCards(2,objectName());
							room->setPlayerMark(damage.from,player->objectName()+"mzengouBan",1);
							return true;
						}
						break;
					}
				}
			}
			if(player->getCardCount()>1&&player->canDiscard(player,"he")
				&&room->askForDiscard(player,objectName(),2,2,true,true,"feili0",".",objectName())){
				player->peiyin(this);
				return player->damageRevises(data,-damage.damage);
			}
		}
		return false;
	}
};

LvemingCard::LvemingCard()
{
}

bool LvemingCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && to_select->getEquips().length() < Self->getEquips().length();
}

void LvemingCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	room->addPlayerMark(effect.from, "&lveming");
	QStringList nums;
	for (int i = 1; i < 14; i++) {
		nums << QString::number(i);
	}
	QString choice = room->askForChoice(effect.to, "lveming", nums.join("+"));
	LogMessage log;
	log.type = "#FumianFirstChoice";
	log.from = effect.to;
	log.arg = choice;
	room->sendLog(log);

	JudgeStruct judge;
	judge.pattern = ".";
	judge.who = effect.from;
	judge.reason = "lveming";
	judge.play_animation = false;
	room->judge(judge);

	int number = judge.pattern.toInt();

	if (number == choice.toInt()) {
		room->damage(DamageStruct("lveming", effect.from, effect.to, 2));
	} else {
		if (effect.to->isAllNude()) return;
		const Card *card = effect.to->getCards("hej").at(qrand() % effect.to->getCards("hej").length());
		CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName());
		room->obtainCard(effect.from, card, reason, room->getCardPlace(card->getEffectiveId()) != Player::PlaceHand);
	}
}

class LvemingVS : public ZeroCardViewAsSkill
{
public:
	LvemingVS() : ZeroCardViewAsSkill("lveming")
	{
	}

	const Card *viewAs() const
	{
		return new LvemingCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("LvemingCard") && !player->getEquips().isEmpty();
	}
};

class Lveming : public TriggerSkill
{
public:
	Lveming() : TriggerSkill("lveming")
	{
		events << FinishJudge;
		view_as_skill = new LvemingVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const
	{
		JudgeStruct *judge = data.value<JudgeStruct *>();
		if (judge->reason != objectName()) return false;
		judge->pattern = QString::number(judge->card->getNumber());
		return false;
	}
};

TunjunCard::TunjunCard()
{
}

bool TunjunCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.isEmpty();
}

QList<int> TunjunCard::removeList(QList<int> equips, QList<int> ids) const
{
	foreach (int id, ids)
		equips.removeOne(id);
	return equips;
}

void TunjunCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	room->doSuperLightbox(effect.from, "tunjun");
	room->removePlayerMark(effect.from, "@tunjunMark");

	int n = effect.from->getMark("&lveming");
	if (n <= 0) return;

	QList<int> weapon, armor, defensivehorse, offensivehorse, treasure, equips;
	foreach (int id, room->getDrawPile()) {
		equips << id;
		const Card *card = Sanguosha->getCard(id);
		if (card->isKindOf("Weapon"))
			weapon << id;
		else if (card->isKindOf("Armor"))
			armor << id;
		else if (card->isKindOf("DefensiveHorse"))
			defensivehorse << id;
		else if (card->isKindOf("OffensiveHorse"))
			offensivehorse << id;
		else if (card->isKindOf("Treasure"))
			treasure << id;
		else
			equips.removeOne(id);
	}
	if (equips.isEmpty()) return;

	QList<int> use_cards;
	for (int i = 0; i < n; i++) {
		if (equips.isEmpty()) break;
		int id = equips.at(qrand() % equips.length());
		use_cards << id;
		if (weapon.contains(id))
			equips = removeList(equips, weapon);
		else if (armor.contains(id))
			equips = removeList(equips, armor);
		else if (defensivehorse.contains(id))
			equips = removeList(equips, defensivehorse);
		else if (offensivehorse.contains(id))
			equips = removeList(equips, offensivehorse);
		else if (treasure.contains(id))
			equips = removeList(equips, treasure);
	}
	if (use_cards.isEmpty()) return;

	foreach (int id, use_cards) {
		if (effect.from->isDead()) return;
		const Card *card = Sanguosha->getCard(id);
		if (!card->isAvailable(effect.to)) continue;
		room->useCard(CardUseStruct(card, effect.to), true);
	}
}

class Tunjun : public ZeroCardViewAsSkill
{
public:
	Tunjun() : ZeroCardViewAsSkill("tunjun")
	{
		frequency = Limited;
		limit_mark = "@tunjunMark";
	}

	const Card *viewAs() const
	{
		return new TunjunCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@tunjunMark") > 0 && player->getMark("&lveming") > 0;
	}
};

ZhuguoCard::ZhuguoCard()
{
}

bool ZhuguoCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.isEmpty();
}

void ZhuguoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		int n = p->getMaxHp()-p->getHandcardNum();
		if(n>0){
			p->drawCards(n,"zhuguo");
		}else{
			if(n<0) room->askForDiscard(p,"zhuguo",-n,-n);
			room->recover(p,RecoverStruct("zhuguo",source));
		}
		if(p->isAlive()){
			foreach (ServerPlayer *q, room->getAlivePlayers()) {
				if(q->getHandcardNum()>p->getHandcardNum()) n = 999;
			}
			if(n<999){
				ServerPlayer *tp = room->askForPlayerChosen(source,room->getOtherPlayers(p),"zhuguo","zhuguo0:"+p->objectName(),true);
				if(tp){
					room->askForUseSlashTo(p,tp,"zhuguo1:"+tp->objectName(),false);
				}
			}
		}
	}
}

class Zhuguo : public ZeroCardViewAsSkill
{
public:
	Zhuguo() : ZeroCardViewAsSkill("zhuguo")
	{
		response_pattern = "@@zhuguo!";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("ZhuguoCard")<1;
	}

	const Card *viewAs() const
	{
		return new ZhuguoCard;
	}
};

class AndaVs : public ViewAsSkill
{
public:
	AndaVs() : ViewAsSkill("anda")
	{
		response_pattern = "@@anda";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *c) const
	{
		return selected.isEmpty()||(selected.length()<2&&selected[0]->getColor()!=c->getColor());
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.length()<2) return nullptr;
		DummyCard *sc = new DummyCard;
		sc->addSubcards(cards);
		return sc;
	}
};

class Anda : public TriggerSkill
{
public:
	Anda() : TriggerSkill("anda")
	{
		events << Dying;
		view_as_skill = new AndaVs;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (player->getMark("andaUse-Clear")<1&&player->hasTurn()) {
			DyingStruct dying = data.value<DyingStruct>();
			if (dying.damage&&dying.damage->from&&player->askForSkillInvoke(objectName()+"$-1",data)) {
				player->addMark("andaUse-Clear");
				if(dying.damage->from->getCardCount()>1){
					const Card*sc = room->askForUseCard(dying.damage->from,"@@anda","anda0:"+dying.who->objectName());
					if(sc){
						dying.who->obtainCard(sc,false);
						return false;
					}
				}
				room->recover(dying.who,RecoverStruct(objectName(),player));
			}
		}
		return false;
	}
};

MobileJianjiCard::MobileJianjiCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool MobileJianjiCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *) const
{
	return targets.isEmpty()||(targets.length()<2&&targets[0]->canPindian(to));
}

bool MobileJianjiCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length()==2;
}

void MobileJianjiCard::onUse(Room *room, CardUseStruct &use) const
{
	room->setTag("MobileJianjiData",QVariant::fromValue(use));
	SkillCard::onUse(room,use);
}

void MobileJianjiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	CardUseStruct use = room->getTag("MobileJianjiData").value<CardUseStruct>();
	if(use.to.first()->canPindian(use.to.last())){
		PindianStruct *pd = use.to.first()->PinDian(use.to.last(),"mobilejianji");
		if(pd->from_number>pd->to_number){
			room->askForUseSlashTo(use.to.first(),use.to.last(),"mobilejianji1:"+use.to.last()->objectName(),false);
		}else if(pd->from_number<pd->to_number){
			room->askForUseSlashTo(use.to.last(),use.to.first(),"mobilejianji1:"+use.to.first()->objectName(),false);
		}
		if(Sanguosha->getCard(getEffectiveId())->isKindOf("Slash")){
			if(pd->from_card->getEffectiveId()==getEffectiveId())
				room->damage(DamageStruct("mobilejianji",source,pd->from));
			if(pd->to_card->getEffectiveId()==getEffectiveId())
				room->damage(DamageStruct("mobilejianji",source,pd->to));
		}
	}
}

class MobileJianjiVs : public OneCardViewAsSkill
{
public:
	MobileJianjiVs() : OneCardViewAsSkill("mobilejianji")
	{
		filter_pattern = ".|.|.|hand";
	}

	const Card *viewAs(const Card *c) const
	{
		MobileJianjiCard *card = new MobileJianjiCard;
		card->addSubcard(c);
		return card;
	}
	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("MobileJianjiCard");
	}
};

class MobileJianji : public TriggerSkill
{
public:
	MobileJianji() : TriggerSkill("mobilejianji")
	{
		events << AskforPindianCard;
		view_as_skill = new MobileJianjiVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		if(event==AskforPindianCard){
			PindianStruct *pd = data.value<PindianStruct*>();
			if(pd->reason==objectName()){
				CardUseStruct use = room->getTag("MobileJianjiData").value<CardUseStruct>();
				if(pd->from->askForSkillInvoke(this,"mobilejianji0:"+use.from->objectName(),false))
					pd->from_card = use.card;
				if(pd->to->askForSkillInvoke(this,"mobilejianji0:"+use.from->objectName(),false))
					pd->to_card = use.card;
				data.setValue(pd);
			}
		}
		return false;
	}
};

class MobileYuanmo : public TriggerSkill
{
public:
	MobileYuanmo() : TriggerSkill("mobileyuanmo")
	{
		events << Damaged << EventPhaseStart;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(player->getMark("mobileyuanmoUse")>1||!player->hasTurn()) return false;
		if (triggerEvent==Damaged||player->getPhase()==Player::Start) {
			QList<ServerPlayer *>tps;
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				foreach (const Card *c, p->getCards("ej")){
					foreach (ServerPlayer *q, room->getOtherPlayers(p)){
						if(p->isProhibited(q,c)) continue;
						if(c->isKindOf("EquipCard")){
							const EquipCard *equip = qobject_cast<const EquipCard *>(c->getRealCard());
							if(q->getEquip(equip->location())) continue;
						}
						tps << p;
						break;
					}
				}
			}
			ServerPlayer *tp = room->askForPlayerChosen(player,tps,objectName(),"mobileyuanmo0",true,true);
			if(tp){
				player->peiyin(this);
				QList<int>ids;
				player->addMark("mobileyuanmoUse");
				foreach (const Card *c, tp->getCards("ej")){
					bool has = false;
					foreach (ServerPlayer *q, room->getOtherPlayers(tp)){
						if(tp->isProhibited(q,c)) continue;
						if(c->isKindOf("EquipCard")){
							const EquipCard *equip = qobject_cast<const EquipCard *>(c->getRealCard());
							if(q->getEquip(equip->location())) continue;
						}
						has = true;
						break;
					}
					if(!has)
						ids << c->getId();
				}
				int id = room->askForCardChosen(player,tp,"ej",objectName(),false,Card::MethodNone,ids);
				if(id<0) return false;
				tps.clear();
				const Card *c = Sanguosha->getCard(id);
				foreach (ServerPlayer *q, room->getOtherPlayers(tp)){
					if(tp->isProhibited(q,c)) continue;
					if(c->isKindOf("EquipCard")){
						const EquipCard *equip = qobject_cast<const EquipCard *>(c->getRealCard());
						if(q->getEquip(equip->location())) continue;
					}
					tps << q;
				}
				ServerPlayer *qp = room->askForPlayerChosen(player,tps,objectName(),"mobileyuanmo1:"+c->objectName());
				if(qp){
					room->doAnimate(1,player->objectName(),qp->objectName());
					int x = 0;
					foreach (ServerPlayer *p, room->getAlivePlayers()){
						if(tp->inMyAttackRange(p)) x++;
					}
					room->moveCardTo(c,qp,room->getCardPlace(id),true);
					id = 0;
					foreach (ServerPlayer *p, room->getAlivePlayers()){
						if(tp->inMyAttackRange(p)) id++;
					}
					x -= id;
					if(x>0&&tp->isAlive()&&player->isAlive()
						&&player->askForSkillInvoke(this,"mobileyuanmo2:"+tp->objectName(),false)){
						tp->drawCards(qMin(x,5),objectName());
					}
				}
			}
		}
		return false;
	}
};

class Xuye : public TriggerSkill
{
public:
	Xuye() : TriggerSkill("xuye")
	{
		events << Damaged;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(event==Damaged){
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				if(player->getHandcardNum()>p->getHandcardNum())
					return false;
			}
			foreach (ServerPlayer *p, room->getAllPlayers()){
				if(TriggerSkill::triggerable(p)&&p->askForSkillInvoke(this,player)){
					p->peiyin(this);
					player->drawCards(2,objectName());
					int has = 1;
					foreach (ServerPlayer *q, room->getAlivePlayers()){
						if(player->getHandcardNum()<q->getHandcardNum())
							has = 0;
					}
					if(has>0&&p->isAlive()&&player->getCards("ej").length()>0){
						has = room->askForCardChosen(p,player,"ej",objectName());
						if(has>=0) room->moveCardTo(Sanguosha->getCard(has),nullptr,Player::DrawPile,false);
					}
				}
			}
		}
		return false;
	}
};

MobileKuangxiangCard::MobileKuangxiangCard()
{
}

bool MobileKuangxiangCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&Self->getHandcardNum()>to->getHandcardNum();
}

void MobileKuangxiangCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		room->swapCards(source,p,"h","mobilekuangxiang");
		foreach (int id, source->handCards()){
			room->setCardTip(id,"mobilekuangxiang");
			room->setCardFlag(id,"mobilekuangxiang");
		}
		foreach (int id, p->handCards()){
			room->setCardTip(id,"mobilekuangxiang");
			room->setCardFlag(id,"mobilekuangxiang");
		}
	}
}

class MobileKuangxiangVs : public ZeroCardViewAsSkill
{
public:
	MobileKuangxiangVs() : ZeroCardViewAsSkill("mobilekuangxiang")
	{
		response_pattern = "@@mobilekuangxiang!";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("MobileKuangxiangCard")<1&&player->getHandcardNum()>0;
	}

	const Card *viewAs() const
	{
		return new MobileKuangxiangCard;
	}
};

class MobileKuangxiang : public TriggerSkill
{
public:
	MobileKuangxiang() : TriggerSkill("mobilekuangxiang")
	{
		events << CardsMoveOneTime << EventPhaseStart;
		view_as_skill = new MobileKuangxiangVs;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from_places.contains(Player::PlaceHand)){
				foreach (int id, move.card_ids){
					if(Sanguosha->getCard(id)->hasFlag("mobilekuangxiang")){
						bool has = false;
						foreach (const Card*h, move.from->getHandcards()){
							if(h->hasTip("mobilekuangxiang")) has = true;
						}
						if(!has){
							const TriggerSkill*xy = Sanguosha->getTriggerSkill("xuye");
							if(xy) xy->trigger(Damaged,room,player,data);
						}
						break;
					}
				}
			}
		}else if(player->getPhase()==Player::Play){
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				foreach (int id, p->handCards()){
					room->setCardTip(id,"-mobilekuangxiang");
					room->setCardFlag(id,"-mobilekuangxiang");
				}
			}
		}
		return false;
	}
};

GanjueCard::GanjueCard()
{
	handling_method = Card::MethodUse;
}

bool GanjueCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	Card*dc = Sanguosha->cloneCard("slash");
	dc->setSkillName("ganjue");
	dc->addSubcards(subcards);
	dc->deleteLater();
	return Self->canSlash(to,dc,false)&&targets.length()<=Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, dc);
}

const Card *GanjueCard::validate(CardUseStruct &card_use) const
{
	card_use.m_addHistory = false;
	Card*dc = Sanguosha->cloneCard("slash");
	dc->setSkillName("ganjue");
	dc->addSubcards(subcards);
	dc->deleteLater();
	return dc;
}

class GanjueVs : public OneCardViewAsSkill
{
public:
	GanjueVs() : OneCardViewAsSkill("ganjue")
	{
		filter_pattern = ".|.|.|equipped";
		response_or_use = true;
	}

	const Card *viewAs(const Card *c) const
	{
		GanjueCard *card = new GanjueCard;
		card->addSubcard(c);
		return card;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("GanjueCard");
	}
};

class Ganjue : public TriggerSkill
{
public:
	Ganjue() : TriggerSkill("ganjue")
	{
		events << TargetSpecified;
		view_as_skill = new GanjueVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())){
				foreach (ServerPlayer *p, use.to){
					bool has = false;
					foreach (const Card*h, p->getHandcards()){
						if(h->getSuit()==use.card->getSuit())
							has = true;
					}
					if(has) continue;
					use.no_respond_list << p->objectName();
				}
				data.setValue(use);
			}
		}
		return false;
	}
};

class Zhuhe : public TriggerSkill
{
public:
	Zhuhe() : TriggerSkill("zhuhe")
	{
		events << EventPhaseEnd;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==EventPhaseEnd){
			if(player->getPhase()==Player::Play&&player->canDiscard(player,"h")){
				const Card*c = room->askForCard(player,".","zhuhe0:",data,Card::MethodNone);
				if(c){
					player->skillInvoked(this);
					Card*dc = dummyCard();
					foreach (const Card*h, player->getHandcards()){
						if(h->getSuit()==c->getSuit()&&!player->isJilei(h))
							dc->addSubcard(h);
					}
					room->throwCard(dc,objectName(),player);
					bool has = dc->subcardsLength()>=player->getEquips().length();
					dc->clearSubcards();
					foreach (int id, room->getDiscardPile()){
						const Card*sc = Sanguosha->getCard(id);
						if(sc->getSuit()==c->getSuit()&&sc->isKindOf("EquipCard"))
							dc->addSubcard(id);
					}
					if(dc->subcardsLength()>0&&player->isAlive()){
						room->fillAG(dc->getSubcards(),player);
						int id = room->askForAG(player,dc->getSubcards(),false,objectName());
						room->clearAG(player);
						c = Sanguosha->getCard(id);
						if(c->isAvailable(player))
							room->useCard(CardUseStruct(c,player));
					}
					if(has&&player->isAlive()){
						QString choice = "zhuhe1+zhuhe3";
						if(player->isWounded()) choice = "zhuhe1+zhuhe2+zhuhe3";
						choice = room->askForChoice(player,objectName(),choice,data);
						if(choice=="zhuhe1")
							player->drawCards(2,objectName());
						else if(choice=="zhuhe2")
							room->recover(player,RecoverStruct(objectName(),player));
						else if(choice=="zhuhe2")
							player->gainHujia();
					}
				}
			}
		}
		return false;
	}
};

MobileDaoshuCard::MobileDaoshuCard()
{
	mute = true;
}

bool MobileDaoshuCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to->getHandcardNum()>1&&to!=Self;
}

void MobileDaoshuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	source->peiyin("mobiledaoshu",1);
	foreach (ServerPlayer *p, targets) {
		const Card*sc = room->askForExchange(p,"mobiledaoshu",1,1,false,"mobiledaoshu0");
		if(!sc) continue;
		QStringList pns,pns2 = Sanguosha->getCardNames(".");
		qShuffle(pns2);
		pns << pns2[0] << pns2[1] << pns2[2];
		QString choice = room->askForChoice(p,"mobiledaoshu",pns.join("+"),QVariant::fromValue(source));
		pns.clear();
		foreach (const Card*h, p->getHandcards()) {
			if(h->getId()==sc->getEffectiveId())
				pns << "mobiledaoshu1="+choice+"="+h->getSuitString()+"_char="+h->getNumberString();
			else
				pns << "mobiledaoshu1="+h->objectName()+"="+h->getSuitString()+"_char="+h->getNumberString();
		}
		QString choice2 = room->askForChoice(source,"mobiledaoshu",pns.join("+"),QVariant::fromValue(p),"","mobiledaoshu2");
		if(choice2.contains(choice)&&choice2.contains(sc->getSuitString())&&choice2.contains("_char="+sc->getNumberString())){
			source->peiyin("mobiledaoshu",2);
			room->damage(DamageStruct("mobiledaoshu",source,p));
		}else{
			source->peiyin("mobiledaoshu",3);
			if(source->getHandcardNum()<2){
				room->loseHp(source,1,true,source,objectName());
				continue;
			}
			QList<const Card*>hs = source->getHandcards();
			qShuffle(hs);
			Card*dc = dummyCard();
			foreach (const Card*h,hs) {
				if(source->isJilei(h)) continue;
				dc->addSubcard(h);
				if(dc->subcardsLength()>1) break;
			}
			room->throwCard(dc,"mobiledaoshu",source);
		}
	}
}

class MobileDaoshu : public ZeroCardViewAsSkill
{
public:
	MobileDaoshu() : ZeroCardViewAsSkill("mobiledaoshu")
	{
		response_pattern = "@@mobiledaoshu";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("MobileDaoshuCard")<1;
	}

	const Card *viewAs() const
	{
		return new MobileDaoshuCard;
	}
};

class Daizui : public TriggerSkill
{
public:
	Daizui() : TriggerSkill("daizui")
	{
		events << DamageInflicted << EventPhaseChanging;
		frequency = Limited;
		limit_mark = "@daizui";
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.damage>=player->getHp()+player->getHujia()&&player->hasSkill(objectName())
				&&player->getMark("@daizui")>0&&player->askForSkillInvoke(this,data)){
				player->peiyin("daizui");
				room->doSuperLightbox(player, "daizui");
				room->removePlayerMark(player, "@daizui");
				player->damageRevises(data,-damage.damage);
				if(damage.from&&damage.from->isAlive()&&damage.card&&room->getCardOwner(damage.card->getEffectiveId())==nullptr)
					damage.from->addToPile("dz_shi",damage.card);
				return true;
			}
		}else{
			if (data.value<PhaseChangeStruct>().to==Player::NotActive) {
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->getPile("dz_shi").length()>0){
						p->obtainCard(dummyCard(p->getPile("dz_shi")));
					}
				}
			}
		}
		return false;
	}
};

class Kuangwu : public TriggerSkill
{
public:
	Kuangwu() : TriggerSkill("kuangwu")
	{
		events << CardFinished << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())){
				foreach (ServerPlayer *p, room->getAlivePlayers()){
					if(player->getMark(p->objectName()+"kuangwuUse-PlayClear")>0){
						player->setMark(p->objectName()+"kuangwuUse-PlayClear",0);
						if(use.card->hasFlag("DamageDone_"+p->objectName())) continue;
						player->addMark("kuangwuBan_lun");
						player->peiyin(objectName(),qrand()%2+3);
						room->loseHp(player,1,true,player,objectName());
					}
				}
			}
		}else{
			if (player->getPhase()==Player::Play&&player->isAlive()) {
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(player->getHandcardNum()!=p->getHandcardNum()
						&&p->getMark("kuangwuBan_lun")<1&&p->hasSkill(objectName())){
						int n = player->getHandcardNum();
						int x = p->getHandcardNum();
						int m = qrand()%2+1;
						if(n>x&&x<5){
							if(!p->askForSkillInvoke(objectName()+"$"+QString::number(m),player)) continue;
							p->drawCards(qMin(5-x,n-x),objectName());
						}else if(x>n&&x>1){
							if(!p->askForSkillInvoke(objectName()+"$"+QString::number(m),player)) continue;
							x = x-qMax(n,1);
							room->askForDiscard(p,objectName(),x,x);
						}else continue;
						if(p->isDead()||player->isDead()) continue;
						Card*dc = Sanguosha->cloneCard("duel");
						dc->setSkillName("_kuangwu");
						if(p->canUse(dc,player)){
							p->addMark(player->objectName()+"kuangwuUse-PlayClear");
							room->useCard(CardUseStruct(dc,p,player));
						}
						dc->deleteLater();
					}
				}
			}
		}
		return false;
	}
};

class Futu : public TriggerSkill
{
public:
	Futu() : TriggerSkill("futu")
	{
		events << DamageDone << HpRecover << DamageInflicted << EventPhaseChanging;
		global = true;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from) damage.from->addMark("futuDamage-Clear",damage.damage);
			player->addMark("jiebianDone-PlayClear");
		}else if(event==DamageInflicted){
			QList<int>ids = player->getPile("futu_yue");
			if(ids.length()>0&&player->hasSkill(objectName())
				&&player->askForSkillInvoke(objectName(),data)){
				room->fillAG(ids,player);
				int id = room->askForAG(player,ids,false,objectName());
				room->clearAG(player);
				room->throwCard(id,objectName(),player);
				DamageStruct damage = data.value<DamageStruct>();
				return player->damageRevises(data,-damage.damage);
			}
		}else if(event==HpRecover){
			RecoverStruct recover = data.value<RecoverStruct>();
			if(recover.who) recover.who->addMark("futuRecover-Clear",recover.recover);
		}else{
			if (data.value<PhaseChangeStruct>().to==Player::NotActive) {
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->hasSkill(objectName())){
						int d = 0,r = 0;
						foreach (ServerPlayer *q, room->getOtherPlayers(p)){
							d = qMax(d,q->getMark("futuDamage-Clear"));
							r = qMax(r,q->getMark("futuRecover-Clear"));
						}
						if(p->getMark("futuDamage-Clear")>d){
							room->sendCompulsoryTriggerLog(p,this);
							QList<int>ids;
							while(true){
								ids << room->getNCards(1,false);
								if(Sanguosha->getCard(ids.last())->isBlack()) break;
							}
							room->returnToTopDrawPile(ids);
							p->addToPile("futu_yue",ids.last());
							d = -1;
						}
						if(p->getMark("futuRecover-Clear")>r){
							if(d>=0) room->sendCompulsoryTriggerLog(p,this);
							QList<int>ids;
							while(true){
								ids << room->getNCards(1,false);
								if(Sanguosha->getCard(ids.last())->isRed()) break;
							}
							room->returnToTopDrawPile(ids);
							p->addToPile("futu_yue",ids.last());
						}
					}
				}
			}
		}
		return false;
	}
};

JingtuCard::JingtuCard()
{
	will_throw = false;
	mute = true;
}

bool JingtuCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *, int &m) const
{
	bool b = false,r = false;
	foreach (int id, subcards){
		const Card*c = Sanguosha->getEngineCard(id);
		if(c->isBlack()) b = true;
		else if(c->isRed()) r = true;
	}
	int n = 0;
	if(b) n++;
	if(r) n++;
	if(targets.length()<n)
		m = 1;
	return false;
}

bool JingtuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	bool b = false,r = false;
	foreach (int id, subcards){
		const Card*c = Sanguosha->getEngineCard(id);
		if(c->isBlack()) b = true;
		else if(c->isRed()) r = true;
	}
	int n = 0;
	if(b) n++;
	if(r) n++;
	return targets.length()==n;
}

void JingtuCard::onUse(Room *room, CardUseStruct &use) const
{
	room->setTag("JingtuCardData",QVariant::fromValue(use));
	SkillCard::onUse(room,use);
}

void JingtuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	int b = 0,r = 0,x = qrand()%2+1;
	QStringList choices;
	foreach (int id, subcards){
		const Card*c = Sanguosha->getEngineCard(id);
		if(c->isBlack()) b++;
		else if(c->isRed()) r++;
		if(choices.contains(c->getColorString())) continue;
		choices << c->getColorString();
	}
	if(b>0&&r>0)
		x += 4;
	else if(r>0)
		x += 2;
	source->peiyin("jingtu",x);
	room->removePlayerMark(source,"@jingtu");
	room->doSuperLightbox(source, "jingtu");
	if(source->getGeneralName().contains("zerong")){
		if(b>0) source->setAvatarIcon("mobile_zerong2");
		else if(r>0) source->setAvatarIcon("mobile_zerong3");
		if(b>0&&r>0) source->setAvatarIcon("mobile_zerong4");
	}
	source->obtainCard(this);
	CardUseStruct use = room->getTag("JingtuCardData").value<CardUseStruct>();
	if(b>0) room->damage(DamageStruct("jingtu",source,use.to.first(),b));
	if(r>0){
		room->gainMaxHp(use.to.last(),r,"jingtu");
		room->recover(use.to.last(),RecoverStruct("jingtu",source,r));
	}
	room->setPlayerProperty(source,"mobilefozhongColor",choices.join("+"));
	room->handleAcquireDetachSkills(source,"-futu|mobilefozhong");
}

class Jingtu : public ViewAsSkill
{
public:
	Jingtu() : ViewAsSkill("jingtu")
	{
		response_pattern = "@@jingtu";
		expand_pile = "futu_yue";
		limit_mark = "@jingtu";
		frequency = Limited;
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *card) const
	{
		QList<int>ids = Self->getPile("futu_yue");
		if(ids.contains(card->getId())){
			if(selected.length()>0){
				int n = 0,m = 0;
				foreach (int id, ids){
					const Card*c = Sanguosha->getEngineCard(id);
					if(c->isBlack()) m++;
					else if(c->isRed()) n++;
				}
				return (n==m&&n>1&&m>1)
				||selected.first()->getColor()==card->getColor();
			}
			return selected.isEmpty();
		}
		return false;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		QList<int>ids = Self->getPile("futu_yue");
		int n = 0;
		foreach (int id, ids){
			const Card*c = Sanguosha->getEngineCard(id);
			if(cards.last()->getColor()==c->getColor()) n++;
		}
		if(cards.length()==n||cards.length()==ids.length()){
			JingtuCard *jc = new JingtuCard;
			jc->addSubcards(cards);
			return jc;
		}
		return nullptr;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@jingtu")>0&&player->getPile("futu_yue").length()>0;
	}
};

JiebianCard::JiebianCard()
{
	will_throw = false;
}

bool JiebianCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	int n = 999;
	foreach (const Player *p, Self->getAliveSiblings())
		n = qMin(n,p->getHp());
	return targets.isEmpty()&&(n>=to->getHp()||to->hasFlag("CurrentPlayer"))&&Self->canPindian(to);
}

void JiebianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		if(source->pindian(p,"jiebian",Sanguosha->getCard(getEffectiveId()))){
			if(p->isDead()) continue;
			if(source->isDead()) break;
			if(room->askForChoice(source,"jiebian","jiebian1+jiebian2")=="jiebian1"){
				room->damage(DamageStruct("jiebian",source,p));
			}else{
				room->recover(p,RecoverStruct("jiebian",source));
				p->drawCards(1,"jiebian");
				if(p->isDead()) continue;
				if(source->isDead()) break;
				Card*dc = dummyCard();
				for(int i=0;i<2;i++){
					int n = room->askForCardChosen(source,p,"he","jiebian",false,Card::MethodNone,dc->getSubcards());
					if(n<0) break;
					dc->addSubcard(n);
					if(dc->subcardsLength()>=p->getCardCount()) break;
				}
				source->obtainCard(dc,false);
			}
		}
	}
}

class Jiebianvs : public ViewAsSkill
{
public:
	Jiebianvs() : ViewAsSkill("jiebian")
	{
		response_pattern = "@@jiebian";
		expand_pile = "futu_yue";
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *card) const
	{
		return cards.isEmpty()&&Self->getPile("futu_yue").contains(card->getId());
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		JiebianCard *jc = new JiebianCard;
		jc->addSubcards(cards);
		return jc;
	}
};

class Jiebian : public TriggerSkill
{
public:
	Jiebian() : TriggerSkill("jiebian")
	{
		events << EventPhaseEnd;
		view_as_skill = new Jiebianvs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(event==EventPhaseEnd){
			if(player->getPhase()==Player::Play){
				foreach (ServerPlayer *p, room->getAlivePlayers()){
					if(p->getMark("jiebianDone-PlayClear")>0) return false;
				}
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->hasSkill(objectName())&&p->canPindian()){
						room->askForUseCard(p,"@@jiebian","jiebian0");
					}
				}
			}
		}
		return false;
	}
};

class MobileFozhong : public TriggerSkill
{
public:
	MobileFozhong() : TriggerSkill("mobilefozhong")
	{
		events << ConfirmDamage << HpRecover << EventPhaseProceeding;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==EventPhaseProceeding){
			if(player->getPhase()==Player::Discard&&player->getHandcardNum()>0&&player->hasSkill(objectName())){
				QString str = player->property("mobilefozhongColor").toString();
				int n = qrand()%2+1;
				if(str.contains("+"))
					n += 4;
				else if(str=="red")
					n += 2;
				room->sendCompulsoryTriggerLog(player,this,n);
				foreach (const Card *h, player->getHandcards()){
					if(str.contains(h->getColorString()))
						room->ignoreCards(player,h);
				}
			}
		}else if(event==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->getTypeId()>0){
				QString str = player->property("mobilefozhongColor").toString();
				if(str.contains(damage.card->getColorString())&&player->hasSkill(objectName())){
					int n = qrand()%2+1;
					if(str.contains("+"))
						n += 4;
					else if(str=="red")
						n += 2;
					room->sendCompulsoryTriggerLog(player,this,n);
					player->damageRevises(data,1);
				}
			}
		}else if(event==HpRecover){
			RecoverStruct recover = data.value<RecoverStruct>();
			if(recover.card&&recover.card->getTypeId()>0&&recover.who){
				QString str = recover.who->property("mobilefozhongColor").toString();
				if(str.contains(recover.card->getColorString())&&recover.who->hasSkill(objectName())){
					int n = qrand()%2+1;
					if(str.contains("+"))
						n += 4;
					else if(str=="red")
						n += 2;
					room->sendCompulsoryTriggerLog(recover.who,this,n);
					recover.recover++;
					data.setValue(recover);
				}
			}
		}
		return false;
	}
};

class Shishu : public TriggerSkill
{
public:
	Shishu() : TriggerSkill("shishu")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(player==move.from&&move.to_place==Player::PlaceHand&&player!=move.to&&move.to->hasFlag("CurrentPlayer")){
				QStringList Types;
				QList<int>ids,hids = move.to->handCards();
				for(int i=0;i<move.card_ids.length();i++){
					if(move.from_places.at(i)==Player::PlaceHand||move.from_places.at(i)==Player::PlaceEquip){
						Types << "^"+Sanguosha->getCard(move.card_ids.at(i))->getType();
						if(hids.contains(move.card_ids.at(i))&&move.to->canDiscard(move.card_ids.at(i)))
							ids << move.card_ids.at(i);
					}
				}
				if(Types.isEmpty()) return false;
				room->sendCompulsoryTriggerLog(player,this);
				ServerPlayer *tp = (ServerPlayer *)move.to;
				const Card*dc = room->askForCard(tp,Types.join("+"),"shishu0:"+player->objectName(),data,Card::MethodNone);
				if(dc){
					room->giveCard(tp,player,dc,objectName());
				}else{
					room->throwCard(ids,objectName(),tp);
				}
			}
		}
		return false;
	}
};

class MobileJili : public TriggerSkill
{
public:
	MobileJili() : TriggerSkill("mobilejili")
	{
		events << TargetSpecified << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach (ServerPlayer *p, use.to)
					p->addMark(player->objectName()+"mobilejiliUse-Clear");
			}
		}else{
			if(player->getPhase()==Player::RoundStart) {
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->hasSkill(objectName())&&p->inMyAttackRange(player)){
						QStringList choices;
						for(int i=0;i<=2;i++){
							if(p->getMark(QString("mobilejili%1_lun").arg(i))<1)
								choices << QString::number(i);
						}
						if(choices.length()>0&&p->askForSkillInvoke(objectName()+"$-1",player)){
							QString choice = room->askForChoice(p,objectName(),choices.join("+"));
							p->addMark(QString("mobilejili%1_lun").arg(choice));
							choice = QString("&mobilejili+%1+#%2-Clear").arg(choice).arg(p->objectName());
							room->setPlayerMark(player,choice,1,QList<ServerPlayer*>()<<p);
						}
						
					}
				}
			}else if(player->getPhase()==Player::Finish){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					foreach (QString m, player->getMarkNames()){
						if(m.contains("&mobilejili+")&&m.contains("+#"+p->objectName()+"-Clear")){
							int n = m.split("+")[1].toInt();
							int x = p->getMark(player->objectName()+"mobilejiliUse-Clear");
							if(x<n){
								p->drawCards(4-x,objectName());
							}else if(x==n){
								m = QString("mobilejili0:%1::%2").arg(player->objectName()).arg(x);
								Card*dc = room->askForExchange(p,objectName(),x,x,true,m);
								if(dc) room->giveCard(p,player,dc,objectName());
							}else{
								Card*dc = Sanguosha->cloneCard("slash");
								dc->setSkillName("_mobilejili");
								if(p->canSlash(player,dc,false)&&p->askForSkillInvoke(objectName(),player,false)){
									room->useCard(CardUseStruct(dc,p,player));
								}
								dc->deleteLater();
							}
						}
					}
				}
			}
		}
		return false;
	}
};

class BsGongmou : public TriggerSkill
{
public:
	BsGongmou() : TriggerSkill("bsgongmou")
	{
		events << EventPhaseChanging << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to==Player::NotActive) {
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->getMark("bsgongmou_qice-Clear")>0)
						room->detachSkillFromPlayer(p,"qice");
					if(p->getMark("bsgongmou_kanpo-Clear")>0)
						room->detachSkillFromPlayer(p,"kanpo");
				}
			}
		}else{
			if(player->getPhase()==Player::Start&&player->hasSkill(objectName())) {
				ServerPlayer *tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName()+"$-1","bsgongmou0",true,true);
				if(tp){
					room->swapCards(player,tp,"h",objectName());
					if(player->isAlive()&&!player->hasSkill("qice")){
						player->addMark("bsgongmou_qice-Clear");
						room->acquireSkill(player,"qice");
					}
					if(tp->isAlive()&&!tp->hasSkill("kanpo")){
						tp->addMark("bsgongmou_kanpo-Clear");
						room->acquireSkill(tp,"kanpo");
					}
				}
			}
		}
		return false;
	}
};

ZhengshuoCard::ZhengshuoCard()
{
	target_fixed = true;
}

void ZhengshuoCard::onUse(Room *room, CardUseStruct &use) const
{
	use.to = room->getAlivePlayers();
	SkillCard::onUse(room,use);
}

void ZhengshuoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	room->removePlayerMark(source,"@zhengshuo");
	room->doSuperLightbox(source, "zhengshuo");
	foreach (ServerPlayer *p, targets) {
		p->throwAllHandCardsAndEquips("zhengshuo");
	}
	room->swapPile();
	room->drawCards(targets,4,"zhengshuo");
}

class Zhengshuo : public ZeroCardViewAsSkill
{
public:
	Zhengshuo() : ZeroCardViewAsSkill("zhengshuo")
	{
		limit_mark = "@zhengshuo";
		frequency = Limited;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		foreach (const Player *p, player->getAliveSiblings(true)) {
			if(p->getHandcardNum()==4) return false;
		}
		return player->getMark(limit_mark)>0;
	}

	const Card *viewAs() const
	{
		return new ZhengshuoCard;
	}
};

class Chizhang : public TriggerSkill
{
public:
	Chizhang() : TriggerSkill("chizhang")
	{
		events << TargetSpecified << PostCardEffected;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isDamageCard()&&use.m_isHandcard&&player->canDiscard("h")){
				Card*dc = room->askForDiscard(player,objectName(),99,1,true,false,"",".","chizhang$-1");
				if(dc){
					QStringList colors;
					foreach (int id, dc->getSubcards())
						colors << Sanguosha->getCard(id)->getColorString();
					foreach (ServerPlayer *p, use.to){
						p->setTag("chizhangColors"+use.card->toString(), colors.join(","));
						room->setPlayerCardLimitation(p,"use,response",colors.join(","),false);
					}
				}
			}
		}else{
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(player->getTag("chizhangColors"+effect.card->toString()).isValid()) {
				room->removePlayerCardLimitation(player,"use,response",player->getTag("chizhangColors"+effect.card->toString()).toString());
				player->removeTag("chizhangColors"+effect.card->toString());
			}
		}
		return false;
	}
};

class Duanyangvs : public OneCardViewAsSkill
{
public:
	Duanyangvs() : OneCardViewAsSkill("duanyang")
	{
		filter_pattern = ".|.|.|duanyang";
		response_pattern = "@@duanyang";
		expand_pile = "duanyang";
	}

	const Card *viewAs(const Card *c) const
	{
		return c;
	}
};

class Duanyang : public TriggerSkill
{
public:
	Duanyang() : TriggerSkill("duanyang")
	{
		events << CardsMoveOneTime << EventPhaseEnd << Damage;
		view_as_skill = new Duanyangvs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from_places.contains(Player::PlaceHand)&&player->getMark("duanyangUse-Clear")<1
            &&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)!=CardMoveReason::S_REASON_USE){
				QList<int>ids;
				for(int i=0;i<move.card_ids.length();i++){
					if(move.from_places.at(i)==Player::PlaceHand
					&&Sanguosha->getCard(move.card_ids.at(i))->isKindOf("Slash"))
						ids << move.card_ids.at(i);
				}
				if(ids.length()>0){
					player->addMark("duanyangUse-Clear");
					room->sendCompulsoryTriggerLog(player,this);
					qShuffle(ids);
					player->addToPile("duanyang",ids.last(),true);
				}
			}
		}else if(event==Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("duanyangUse")
				&&damage.to->getCardCount(true,true)>0&&player->askForSkillInvoke("duanyang$-1",damage.to)){
				QList<int>ids;
				for(int i=0;i<2;i++){
					int id = room->askForCardChosen(player,damage.to,"hej",objectName(),false,Card::MethodRecast,ids);
					if(id<0) break;
					ids << id;
				}
				LogMessage log;
				log.type = "$RecastCard";
				log.from = damage.to;
				log.card_str = ListI2S(ids).join("+");
				room->sendLog(log);
				room->moveCardTo(dummyCard(ids), damage.to, nullptr, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_RECAST, player->objectName(), damage.to->objectName(), "duanyang",""));
				damage.to->drawCards(ids.length(), "recast");
				player->drawCards(4, "duanyang");
			}
		}else{
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				while(p->getPile("duanyang").length()>0
				&&room->askForUseCard(p,"@@duanyang","duanyang0",-1,Card::MethodUse,true,nullptr,nullptr,"duanyangUse")){
					
				}
			}
		}
		return false;
	}
};











mobileSpPackage::mobileSpPackage()
	: Package("mobile_sp")
{
	General *mobile_huangzhu = new General(this, "mobile_huangzhu", "qun", 4);
	mobile_huangzhu->addSkill(new Chizhang);
	mobile_huangzhu->addSkill(new Duanyang);







	General *zhugedan = new General(this, "zhugedan", "wei", 4); // SP 032
	zhugedan->addSkill(new Gongao);
	zhugedan->addSkill(new Juyi);
	skills << new Weizhong;
	
	General *sp_dingfeng = new General(this, "sp_dingfeng*xh_huben", "wu", 4, true); // SP 031
	sp_dingfeng->addSkill("duanbing");
	sp_dingfeng->addSkill("fenxun");
	
	General *fuwan = new General(this, "fuwan", "qun", 4);
	fuwan->addSkill("moukui");
	
	General *sp_hetaihou = new General(this, "sp_hetaihou*xh_tianji", "qun", 3, false); // SP 033
	sp_hetaihou->addSkill("zhendu");
	sp_hetaihou->addSkill("qiluan");
	
	General *zhangbao = new General(this, "zhangbao", "qun", 3); // SP 025
	zhangbao->addSkill(new Zhoufu);
	zhangbao->addSkill(new Yingbing);
	addMetaObject<ZhoufuCard>();
	
	General *new_mobile_maliang = new General(this, "new_mobile_maliang", "shu", 3);
	new_mobile_maliang->addSkill("zishu");
	new_mobile_maliang->addSkill(new MobileYingyuan);


	General *guanyinping = new General(this, "guanyinping", "shu", 3, false); // SP 014
	guanyinping->addSkill(new Xueji);
	guanyinping->addSkill(new Huxiao);
	guanyinping->addSkill(new HuxiaoCount);
	guanyinping->addSkill(new Wuji);
	related_skills.insertMulti("huxiao", "#huxiao-count");
	addMetaObject<XuejiCard>();

	General *new_guanyinping = new General(this, "new_guanyinping", "shu", 3, false);
	new_guanyinping->addSkill(new Newxuehen);
	new_guanyinping->addSkill(new NewHuxiao);
	new_guanyinping->addSkill(new NewHuxiaoTargetMod);
	new_guanyinping->addSkill(new NewWuji);
	related_skills.insertMulti("newhuxiao", "#newhuxiao-target");
	addMetaObject<NewxuehenCard>();

	General *second_new_sp_jiaxu = new General(this, "second_new_sp_jiaxu", "wei", 3);
	second_new_sp_jiaxu->addSkill("zhenlve");
	second_new_sp_jiaxu->addSkill("jianshu");
	second_new_sp_jiaxu->addSkill(new NewYongdi);


	General *erqiao = new General(this, "erqiao", "wu", 3, false); // SP 021
	erqiao->addSkill(new Xingwu);
	erqiao->addSkill(new Luoyan("luoyan"));
	skills << new Luoyan("olluoyan");


	General *mobile_heqi = new General(this, "mobile_heqi", "wu", 4);
	mobile_heqi->addSkill(new MobileQizhou);
	mobile_heqi->addSkill(new MobileQizhouLose);
	mobile_heqi->addSkill(new MobileShanxi);
	mobile_heqi->addSkill(new MobileShanxiGet);
	related_skills.insertMulti("mobileqizhou", "#mobileqizhou-lose");
	related_skills.insertMulti("mobileshanxi", "#mobileshanxi-get");
	addMetaObject<MobileShanxiCard>();

	General *tadun = new General(this, "tadun", "qun", 4);
	tadun->addSkill(new Luanzhan);
	tadun->addSkill(new LuanzhanTargetMod);
	related_skills.insertMulti("luanzhan", "#luanzhan-target");
	addMetaObject<LuanzhanCard>();


	General *xingcai = new General(this, "xingcai", "shu", 3, false); // SP 028
	xingcai->addSkill(new Shenxian);
	xingcai->addSkill(new Qiangwu);
	xingcai->addSkill(new QiangwuTargetMod);
	related_skills.insertMulti("qiangwu", "#qiangwu-target");
	addMetaObject<QiangwuCard>();

	General *new_mayunlu = new General(this, "new_mayunlu*xh_nvshi", "shu", 4, false);
	new_mayunlu->addSkill(new NewFengpo);
	new_mayunlu->addSkill(new NewFengpoEffect);
	new_mayunlu->addSkill("mashu");
	related_skills.insertMulti("newfengpo", "#newfengpo-effect");

	General *mazhong = new General(this, "mazhong", "shu", 4);
	mazhong->addSkill(new Fuman);
	addMetaObject<FumanCard>();


	General *mobile_zhaoxiang = new General(this, "mobile_zhaoxiang", "shu", 4, false);
	mobile_zhaoxiang->addSkill("mobilefanghun");
	mobile_zhaoxiang->addSkill(new MobileFuhan);

	General *mobile_weiwenzhugezhi = new General(this, "mobile_weiwenzhugezhi", "wu", 4);
	mobile_weiwenzhugezhi->addSkill(new MobileFuhai);
	addMetaObject<MobileFuhaiCard>();

	General *sp_taishici = new General(this, "sp_taishici", "qun", 4);
	sp_taishici->addSkill(new Jixu);
	addMetaObject<JixuCard>();

	General *dongcheng = new General(this, "dongcheng", "qun", 4);
	dongcheng->addSkill(new Chengzhao);

	General *guansuo = new General(this, "guansuo", "shu", 4);
	guansuo->addSkill(new Xiefang);
	guansuo->addSkill(new Zhengnan);

	General *yangyi = new General(this, "yangyi", "shu", 3);
	yangyi->addSkill(new Duoduan);
	yangyi->addSkill(new Gongsun);
	addMetaObject<GongsunCard>();

	General *duji = new General(this, "duji", "wei", 3);
	duji->addSkill(new Andong);
	duji->addSkill(new Yingshi);
	duji->addSkill(new YingshiDeath);
	related_skills.insertMulti("yingshi", "#yingshi-death");
	addMetaObject<YingshiCard>();

	General *lvdai = new General(this, "lvdai", "wu", 4);
	lvdai->addSkill(new Qinguo);
	addMetaObject<QinguoCard>();

	General *liuyao = new General(this, "liuyao", "qun", 4);
	liuyao->addSkill(new Kannan);
	addMetaObject<KannanCard>();

	General *shixie = new General(this, "shixie*xh_sibi", "qun", 3);
	shixie->addSkill(new Biluan);
	shixie->addSkill(new BiluanDist);
	shixie->addSkill(new Lixia);
	related_skills.insertMulti("biluan", "#biluan-dist");

	General *taoqian = new General(this, "taoqian", "qun", 3);
	taoqian->addSkill(new Zhaohuo);
	taoqian->addSkill(new Yixiang);
	taoqian->addSkill(new Yirang);

	General *fanchou = new General(this, "fanchou", "qun", 4);
	fanchou->addSkill(new Xingluan("xingluan"));
	skills << new Xingluan("tenyearxingluan");


	General *liuyan = new General(this, "liuyan", "qun", 3);
	liuyan->addSkill(new Tushe);
	liuyan->addSkill(new Limu);
	liuyan->addSkill(new LimuTargetMod);
	related_skills.insertMulti("limu", "#limu-target");
	addMetaObject<LimuCard>();

	General *sp_zhangji = new General(this, "sp_zhangji*xh_tianzhu", "qun", 4);
	sp_zhangji->addSkill(new Lveming);
	sp_zhangji->addSkill(new Tunjun);
	addMetaObject<LvemingCard>();
	addMetaObject<TunjunCard>();

	General *mobile_shenpei = new General(this, "mobile_shenpei", "qun", 3, true, false, false, 2);
	mobile_shenpei->addSkill(new MobileShouye);
	mobile_shenpei->addSkill(new MobileLiezhi);
	addMetaObject<MobileLiezhiCard>();

	General *zhangliang = new General(this, "zhangliang", "qun", 4);
	zhangliang->addSkill(new Jijun);
	zhangliang->addSkill(new Fangtong);
	addMetaObject<FangtongCard>();

	General *mobile_wangyun = new General(this, "mobile_wangyun", "qun", 4);
	mobile_wangyun->addSkill(new MobileLianji);
	mobile_wangyun->addSkill(new MobileMoucheng);
	mobile_wangyun->addRelateSkill("jingong");
	addMetaObject<MobileLianjiCard>();


	General *mobile_baosanniang = new General(this, "mobile_baosanniang", "shu", 3, false);
	mobile_baosanniang->addSkill(new Shuyong);
	mobile_baosanniang->addSkill(new MobileXushen);
	mobile_baosanniang->addSkill(new MoboleZhennan);
	addMetaObject<MobileXushenCard>();

	General *mobile_zhanggong = new General(this, "mobile_zhanggong", "wei", 3);
	mobile_zhanggong->addSkill(new MobileSpQianxin);
	mobile_zhanggong->addSkill(new MobileSpQianxinMove);
	mobile_zhanggong->addSkill(new MobileZhenxing);
	related_skills.insertMulti("mobilespqianxin", "#mobilespqianxin-move");
	addMetaObject<MobileSpQianxinCard>();

	General *jiakui = new General(this, "jiakui", "wei", 3);
	jiakui->addSkill(new Zhongzuo);
	jiakui->addSkill(new Wanlan);
	jiakui->addSkill(new WanlanDamage);
	related_skills.insertMulti("wanlan", "#wanlan-damage");

	General *new_jiakui = new General(this, "new_jiakui", "wei", 4);
	new_jiakui->addSkill(new Tongqu);
	new_jiakui->addSkill(new TongquTrigger);
	new_jiakui->addSkill(new NewWanlan);
	related_skills.insertMulti("tongqu", "#tongqu-trigger");
	addMetaObject<TongquCard>();

	General *xugong = new General(this, "xugong", "wu", 3);
	xugong->addSkill(new Biaozhao);
	xugong->addSkill(new Yechou);

	General *mobile_sufei = new General(this, "mobile_sufei", "qun", 4);
	mobile_sufei->addSkill(new Zhengjian);
	mobile_sufei->addSkill(new Gaoyuan);
	addMetaObject<GaoyuanCard>();


	General *furong = new General(this, "furong", "shu", 4);
	furong->addSkill(new Xuewei);
	furong->addSkill(new Liechi);

	General *zhouqun = new General(this, "zhouqun", "shu", 3);
	zhouqun->addSkill(new Tiansuan);
	zhouqun->addSkill(new TiansuanEffect);
	related_skills.insertMulti("tiansuan", "#tiansuan");
	addMetaObject<TiansuanCard>();

	General *sp_zhangyi = new General(this, "sp_zhangyi", "shu", 4);
	sp_zhangyi->addSkill(new Zhiyi);

	General *second_sp_zhangyi = new General(this, "second_sp_zhangyi", "shu", 4);
	second_sp_zhangyi->addSkill(new SecondZhiyi);
	second_sp_zhangyi->addSkill(new SecondZhiyiRecord);
	related_skills.insertMulti("secondzhiyi", "#secondzhiyi-record");

	General *dengzhi = new General(this, "dengzhi", "shu", 3);
	dengzhi->addSkill(new Jimeng);
	dengzhi->addSkill(new Shuaiyan);

	General *dingyuan = new General(this, "dingyuan", "qun", 4);
	dingyuan->addSkill(new Beizhu);
	addMetaObject<BeizhuCard>();

	General *gongsunkang = new General(this, "gongsunkang", "qun", 4);
	gongsunkang->addSkill(new Juliao);
	gongsunkang->addSkill(new Taomie);
	gongsunkang->addSkill(new TaomieMark);
	related_skills.insertMulti("taomie", "#taomie-mark");

	General *hucheer = new General(this, "hucheer", "qun", 4);
	hucheer->addSkill(new Daoji);
	addMetaObject<DaojiCard>();

	General *chendeng = new General(this, "chendeng", "qun", 3);
	chendeng->addSkill(new Zhouxuan);
	chendeng->addSkill(new Fengji);
	addMetaObject<ZhouxuanCard>();


	General *maojie = new General(this, "maojie", "wei", 3);
	maojie->addSkill(new Bingqing);
	maojie->addSkill(new Yingfeng);
	maojie->addSkill(new YingfengTarget);
	related_skills.insertMulti("yingfeng", "#yingfeng");

	General *lingju = new General(this, "lingju*xh_nvshi", "qun", 3, false);
	lingju->addSkill("jieyuan");
	lingju->addSkill("fenxin");

	General *yanpu = new General(this, "yanpu*xh_sibi", "qun", 3);
	yanpu->addSkill(new Huantu);
	yanpu->addSkill(new Bihuo);
	yanpu->addSkill(new BihuoDistance);
	related_skills.insertMulti("bihuo", "#bihuo");

	General *mayuanyi = new General(this, "mayuanyi", "qun", 4);
	mayuanyi->addSkill(new Jibing);
	mayuanyi->addSkill(new Wangjing);
	mayuanyi->addSkill(new Moucuan);
	skills << new Binghuo;


	General *hujinding = new General(this, "hujinding", "shu", 6, false, false, false, 2);
	hujinding->addSkill(new Renshi("renshi"));
	hujinding->addSkill(new Wuyuan("wuyuan"));
	hujinding->addSkill(new Huaizi);
	skills << new Renshi("tenyeardeshi") << new Wuyuan("tenyearwuyuan");
	addMetaObject<WuyuanCard>();
	addMetaObject<TenyearWuyuanCard>();

	General *new_lifeng = new General(this, "new_lifeng", "shu", 3);
	new_lifeng->addSkill(new NewTunchu);
	new_lifeng->addSkill(new NewTunchuPut);
	new_lifeng->addSkill(new NewTunchuLimit);
	new_lifeng->addSkill(new NewShuliang);
	related_skills.insertMulti("newtunchu", "#newtunchu-put");
	related_skills.insertMulti("newtunchu", "#newtunchu-limit");
	addMetaObject<NewShuliangCard>();

	General *zhaotongzhaoguang = new General(this, "zhaotongzhaoguang", "shu", 4);
	zhaotongzhaoguang->addSkill(new Yizan);
	zhaotongzhaoguang->addSkill(new Longyuan);
	addMetaObject<YizanCard>();

	General *wangyuanji = new General(this, "wangyuanji", "wei", 3, false);
	wangyuanji->addSkill(new Qianchong);
	wangyuanji->addSkill(new QianchongTargetMod);
	wangyuanji->addSkill(new QianchongLose);
	wangyuanji->addSkill(new Shangjian);
	related_skills.insertMulti("qianchong", "#qianchong-target");
	related_skills.insertMulti("qianchong", "#qianchong-lose");

	General *yanghuiyu = new General(this, "yanghuiyu", "wei", 3, false);
	yanghuiyu->addSkill(new Hongyi);
	yanghuiyu->addSkill(new Quanfeng);

	General *second_yanghuiyu = new General(this, "second_yanghuiyu", "wei", 3, false);
	second_yanghuiyu->addSkill(new SecondHongyi);
	second_yanghuiyu->addSkill(new SecondQuanfeng);
	addMetaObject<HongyiCard>();
	addMetaObject<SecondHongyiCard>();

	General *liuye = new General(this, "liuye", "wei", 3);
	liuye->addSkill(new Polu("polu"));
	liuye->addSkill(new Choulve);
	liuye->addSkill(new ChoulveRecord);
	related_skills.insertMulti("choulve", "#choulve-record");

	General *second_liuye = new General(this, "second_liuye", "wei", 3);
	second_liuye->addSkill(new Polu("secondpolu"));
	second_liuye->addSkill("choulve");

	General *simazhao = new General(this, "simazhao", "wei", 3);
	simazhao->addSkill(new Daigong);
	simazhao->addSkill(new SpZhaoxin);
	addMetaObject<SpZhaoxinCard>();
	addMetaObject<SpZhaoxinChooseCard>();

	General *second_zhuling = new General(this, "second_zhuling", "wei", 4);
	second_zhuling->addSkill(new SecondZhanyi);
	addMetaObject<SecondZhanyiViewAsBasicCard>();
	addMetaObject<SecondZhanyiCard>();

	General *fuqian = new General(this, "fuqian", "shu", 4);
	fuqian->addSkill(new Jueyong);
	fuqian->addSkill(new Poxiang);
	addMetaObject<PoxiangCard>();

	General *wangjun = new General(this, "wangjun", "qun", 4);
	wangjun->addSkill(new Zhujian);
	wangjun->addSkill(new Duansuo);
	addMetaObject<ZhujianCard>();
	addMetaObject<DuansuoCard>();

	General *mobile_mamidi = new General(this, "mobile_mamidi", "qun", 3);
	mobile_mamidi->addSkill(new Chengye);
	mobile_mamidi->addSkill(new Buxu);
	addMetaObject<BuxuCard>();

	General *ruanhui = new General(this, "ruanhui", "wei", 3, false);
	ruanhui->addSkill(new Mingcha);
	ruanhui->addSkill(new Jingzhong);

	General *yangbiao = new General(this, "yangbiao", "qun", 3);
	yangbiao->addSkill(new Zhaohan);
	yangbiao->addSkill(new Rangjie);
	yangbiao->addSkill(new Yizheng);
	addMetaObject<YizhengCard>();

	General *mobile_hansui = new General(this, "mobile_hansui", "qun", 4);
	mobile_hansui->addSkill(new MobileNiluan);
	mobile_hansui->addSkill(new MobileNiluanLog);
	mobile_hansui->addSkill(new MobileXiaoxi);
	related_skills.insertMulti("mobileniluan", "#mobileniluan");

	General *nanshengmi = new General(this, "nanshengmi", "qun", 3);
	nanshengmi->addSkill(new Chijiec);
	nanshengmi->addSkill(new Waishi);
	nanshengmi->addSkill(new Renshe);
	addMetaObject<WaishiCard>();

	General *simafu = new General(this, "simafu", "wei", 3);
	simafu->addSkill(new Xunde);
	simafu->addSkill(new Chenjie);

	General *liyi = new General(this, "liyi", "shu", 4);
	liyi->addSkill(new jiaohua);
	addMetaObject<JiaohuaCard>();

	General *guonvwang = new General(this, "guonvwang", "wei", 3, false);
	guonvwang->addSkill(new Yichong);
	guonvwang->addSkill(new Wufei);

	General *qianzhao = new General(this, "qianzhao", "wei", 4);
	qianzhao->addSkill(new Shihe);
	qianzhao->addSkill(new Zhenfu);
	addMetaObject<ShiheCard>();

	General *mobile_chengui = new General(this, "mobile_chengui", "qun", 3);
	mobile_chengui->addSkill(new Guimou);
	mobile_chengui->addSkill(new Zhouxian);

	General *muludawang = new General(this, "muludawang", "qun" ,3);
	muludawang->setStartHujia(1);
	muludawang->addSkill(new Shoufa);
	muludawang->addSkill(new Zhoulin);
	muludawang->addSkill(new Yuxiang);
	muludawang->addSkill(new YuxiangDistance);
	addMetaObject<ZhoulinCard>();

	General *mobile_huban = new General(this, "mobile_huban", "wei", 4);
	mobile_huban->addSkill(new spYilie);

	General *mobile_jianggan = new General(this, "mobile_jianggan", "wei", 3);
	mobile_jianggan->addSkill(new MobileDaoshu);
	mobile_jianggan->addSkill(new Daizui);
	addMetaObject<MobileDaoshuCard>();

	General *laimin = new General(this, "laimin", "shu", 3);
	laimin->addSkill(new Laishou);
	laimin->addSkill(new Luanqun);
	laimin->addSkill(new LuanqunPro);
	addMetaObject<LuanqunCard>();

	General *mobile_xianglang = new General(this, "mobile_xianglang", "shu", 3);
	mobile_xianglang->addSkill(new Naxue);
	mobile_xianglang->addSkill(new Yijie);
	addMetaObject<NaxueCard>();

	General *yangfeng = new General(this, "yangfeng", "qun", 4);
	yangfeng->addSkill(new Xietu);
	yangfeng->addSkill(new Weiming);
	addMetaObject<XietuCard>();

	General *zhangbu = new General(this, "zhangbu", "wu", 4);
	zhangbu->addSkill(new Chengxiong);
	zhangbu->addSkill(new Wangzhuan);	

	General *mobile_zhangfen = new General(this, "mobile_zhangfen", "wu", 4);
	mobile_zhangfen->addSkill(new Quchong);
	mobile_zhangfen->addSkill(new Xunjie);
	addMetaObject<QuchongCard>();

	General *mobilesp_zhenji = new General(this, "mobilesp_zhenji", "qun", 3, false);
	mobilesp_zhenji->addSkill(new Bojian);
	mobilesp_zhenji->addSkill(new Jiwei);

	General *mobile_qinghe = new General(this, "mobile_qinghe", "wei", 3,false);
	mobile_qinghe->addSkill(new MZengou);
	mobile_qinghe->addSkill(new Feili);
	addMetaObject<MZengouCard>();

	General *wuke = new General(this, "wuke", "wu", 3,false);
	wuke->addSkill(new Zhuguo);
	wuke->addSkill(new Anda);
	addMetaObject<ZhuguoCard>();

	General *mobile_xingdaorong = new General(this, "mobile_xingdaorong", "qun", 4);
	mobile_xingdaorong->addSkill(new Kuangwu);

	General *mobile_zerong = new General(this, "mobile_zerong", "qun", 4);
	mobile_zerong->addSkill(new Futu);
	mobile_zerong->addSkill(new Jingtu);
	mobile_zerong->addSkill(new Jiebian);
	skills << new MobileFozhong;
	addMetaObject<JingtuCard>();
	addMetaObject<JiebianCard>();

	General *mobile_mengda = new General(this, "mobile_mengda", "qun", 4);
	mobile_mengda->addSkill(new Shishu);
	mobile_mengda->addSkill(new MobileJili);





}
ADD_PACKAGE(mobileSp)


class Beiming : public TriggerSkill
{
public:
	Beiming() : TriggerSkill("beiming")
	{
		events << GameStart;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(event==GameStart){
			QList<ServerPlayer *> tos = room->askForPlayersChosen(player,room->getAlivePlayers(),objectName(),0,2,"beiming0:",true);
			if(tos.length()>0){
				room->broadcastSkillInvoke(objectName());
				foreach (ServerPlayer *p, tos){
					QStringList suits;
					foreach (const Card *c, p->getHandcards()){
						if(!suits.contains(c->getSuitString()))
							suits.append(c->getSuitString());
					}
					foreach (int id, room->getDrawPile()){
						const Card *c = Sanguosha->getCard(id);
						if(c->isKindOf("Weapon")){
							const Weapon *w = qobject_cast<const Weapon *>(c->getRealCard());
							if(w->getRange()==suits.length()){
								p->obtainCard(c);
								break;
							}
						}
					}
				}
			}
		}
		return false;
	}
};

class Choumang : public TriggerSkill
{
public:
	Choumang() : TriggerSkill("choumang")
	{
		events << TargetSpecified << TargetConfirmed;
		waked_skills = "#choumang_bf";
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(player->getMark("choumangUse-Clear")>0||!player->hasTurn()) return false;
		if(event==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&use.to.size()==1&&player->askForSkillInvoke(this,data)){
				room->addPlayerMark(player,"choumangUse-Clear");
				room->broadcastSkillInvoke(objectName());
				QStringList choices;
				choices << "choumang1" << "choumang2";
				if(player->getWeapon()||use.to.first()->getWeapon())
					choices << "choumang3";
				QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
				if(choice=="choumang3"){
					const Card *w = player->getWeapon();
					if(w) room->throwCard(w,objectName(),player);
					w = use.to.first()->getWeapon();
					if(w) room->throwCard(w,objectName(),use.to.first(),player);
				}
				if(choice!="choumang2"){
					room->setCardFlag(use.card,"choumangDamage");
				}
				if(choice!="choumang1"){
					room->setCardFlag(use.card,"choumangJink");
					player->setFlags("choumangJink"+use.card->toString());
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&use.to.size()==1&&use.to.contains(player)&&player->askForSkillInvoke(this,data)){
				room->addPlayerMark(player,"choumangUse-Clear");
				room->broadcastSkillInvoke(objectName());
				QStringList choices;
				choices << "choumang1" << "choumang2";
				if(player->getWeapon()||use.to.first()->getWeapon())
					choices << "choumang3";
				QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
				if(choice=="choumang3"){
					const Card *w = player->getWeapon();
					if(w) room->throwCard(w,objectName(),player);
					w = use.to.first()->getWeapon();
					if(w) room->throwCard(w,objectName(),use.to.first(),player);
				}
				if(choice!="choumang2"){
					room->setCardFlag(use.card,"choumangDamage");
				}
				if(choice!="choumang1"){
					room->setCardFlag(use.card,"choumangJink");
					player->setFlags("choumangJink"+use.card->toString());
				}
			}
		}
		return false;
	}
};

class ChoumangBf : public TriggerSkill
{
public:
	ChoumangBf() : TriggerSkill("#choumang_bf")
	{
		events << ConfirmDamage << CardOffset;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		if (triggerEvent == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card&&damage.card->hasFlag("choumangDamage")) {
				damage.damage++;
				data.setValue(damage);
			}
		}else if(triggerEvent==CardOffset){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->hasFlag("choumangJink")&&effect.offset_card->isKindOf("Jink")){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->hasFlag("choumangJink"+effect.card->toString())){
						p->setFlags("-choumangJink"+effect.card->toString());
						QList<ServerPlayer *>tos;
						foreach (ServerPlayer *q, room->getAlivePlayers()){
							if(p->distanceTo(q)==1&&q->getCardCount()>0)
								tos << q;
						}
						ServerPlayer *to = room->askForPlayerChosen(p,tos,"choumang","choumang_bf0:",true);
						if(to){
							int id = room->askForCardChosen(p,to,"hej","choumang");
							if(id>-1) room->obtainCard(p,id,false);
						}
					}
				}
			}
		}
		return false;
	}
};

class Bifeng : public TriggerSkill
{
public:
	Bifeng() : TriggerSkill("bifeng")
	{
		events << TargetConfirming << CardFinished << PreCardResponded;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == TargetConfirming) {
			CardUseStruct use = data.value<CardUseStruct>();
			if ((use.card->isKindOf("BasicCard")||use.card->isNDTrick())&&use.to.length()<=4) {
				if(player->isAlive()&&player->hasSkill(objectName())&&player->askForSkillInvoke(this,data)){
					room->setCardFlag(use.card,"bifengUse");
					use.nullified_list << player->objectName();
					player->setFlags("bifengUse"+use.card->toString());
					data.setValue(use);
				}
			}
		}else if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("bifengUse")){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->hasFlag("bifengUse"+use.card->toString())){
						p->setFlags("-bifengUse"+use.card->toString());
						if(use.card->hasFlag("bifengUseWho"))
							p->drawCards(2,objectName());
						else
							room->loseHp(p,1,true,p,objectName());
					}
				}
			}else if(use.whocard&&use.whocard->hasFlag("bifengUse")){
				room->setCardFlag(use.whocard,"bifengUseWho");
			}
		}else{
			CardResponseStruct res = data.value<CardResponseStruct>();
			if(res.m_toCard&&res.m_toCard->hasFlag("bifengUse")){
				room->setCardFlag(res.m_toCard,"bifengUseWho");
			}
		}
		return false;
	}
};

class Suwang : public TriggerSkill
{
public:
	Suwang() : TriggerSkill("suwang")
	{
		events << EventPhaseChanging << CardFinished << Damaged << EventPhaseProceeding;
		global = true;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to==Player::NotActive) {
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->getMark("suwangUse-Clear")>0&&p->getMark("suwangDamage-Clear")<1&&p->hasSkill(objectName())){
						room->sendCompulsoryTriggerLog(p,this);
						p->addToPile("suwang",room->getNCards(1));
					}
				}
			}
		}else if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->hasFlag("CurrentPlayer")){
				foreach (ServerPlayer *p, use.to){
					if(p->isAlive())
						p->addMark("suwangUse-Clear");
				}
			}
		}else if(triggerEvent==EventPhaseProceeding){
			if(player->getPhase()==Player::Draw&&player->hasSkill(objectName())){
				QList<int>ids = player->getPile("suwang");
				if(ids.length()>0&&player->askForSkillInvoke(this,"suwang0:")){
					DummyCard*dummy = new DummyCard(ids);
					player->obtainCard(dummy);
					if(dummy->subcardsLength()>=3){
						ServerPlayer *p = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"suwang1",true);
						if(p){
							room->doAnimate(1,player->objectName(),p->objectName());
							p->drawCards(2,objectName());
						}
					}
					dummy->deleteLater();
					return true;
				}
			}
		}else{
			player->addMark("suwangDamage-Clear");
		}
		return false;
	}
};

XiezhengCard::XiezhengCard()
{
	handling_method = Card::MethodUse;
}

bool XiezhengCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	Card*dc = Sanguosha->cloneCard("_ov_binglinchengxia");
	dc->setSkillName("xiezheng");
	dc->deleteLater();
	if(targets.isEmpty()){
		if(dc->targetFilter(targets,to,Self)){
			if(to->getKingdom()==Self->getKingdom()||Self->getMark("ZXChangeXiezheng")>0) return true;
			foreach (const Player *p, to->getAliveSiblings()){
				if(to->getKingdom()==p->getKingdom()&&dc->targetFilter(targets,p,Self)) return false;
			}
			return true;
		}
		return false;
	}
	return dc->targetFilter(targets,to,Self);
}

const Card *XiezhengCard::validate(CardUseStruct &) const
{
	Card*dc = Sanguosha->cloneCard("_ov_binglinchengxia");
	dc->setSkillName("_xiezheng");
	dc->deleteLater();
	return dc;
}

class XiezhengVs : public ZeroCardViewAsSkill
{
public:
	XiezhengVs() : ZeroCardViewAsSkill("xiezheng")
	{
		response_pattern = "@@xiezheng";
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	const Card *viewAs() const
	{
		return new XiezhengCard;
	}
};

class Xiezheng : public TriggerSkill
{
public:
	Xiezheng() : TriggerSkill("xiezheng")
	{
		events << EventPhaseStart << DamageDone;
		view_as_skill = new XiezhengVs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart) {
			if (player->getPhase()==Player::Finish&&player->hasSkill(objectName())) {
				if(room->getMode()=="03_1v2"&&player->getMark("xiezhengUse")>0) return false;
				int n = room->getMode()=="03_1v2"?2:1;
				QList<ServerPlayer *>tps = room->askForPlayersChosen(player,room->getAlivePlayers(),objectName(),0,n,"xiezheng0",true);
				if(tps.length()>0){
					player->peiyin("xingxiezheng");
					player->addMark("xiezhengUse");
					room->removeTag("xiezhengDamage");
					room->setPlayerMark(player,"xiezhengMode",n);
					foreach (ServerPlayer *p, tps){
						n = p->getRandomHandCardId();
						if(n>-1) room->moveCardTo(Sanguosha->getCard(n),nullptr,Player::DrawPile,false);
					}
					if(player->isAlive()){
						room->askForUseCard(player,"@@xiezheng","xiezheng1");
						if(room->getTag("xiezhengDamage").toBool()) return false;
						room->loseHp(player,1,true,player,objectName());
					}
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash"))
				room->setTag("xiezhengDamage",true);
		}
		return false;
	}
};

QiantunCard::QiantunCard()
{
	handling_method = Card::MethodUse;
	mute = true;
}

bool QiantunCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to->getHandcardNum()>0&&to!=Self;
}

void QiantunCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	source->peiyin("xingqiantun");
	foreach (ServerPlayer *p, targets) {
		Card*sc = room->askForExchange(p,"qiantun",998,1,false,"qiantun0");
		p->removeTag("qiantunCard");
		if(sc){
			room->showCard(p,sc->getSubcards());
			p->setTag("qiantunCard", QVariant::fromValue(sc));
			if(source->canPindian(p)){
				if(source->pindian(p,"qiantun")){
					if(source->isDead()) continue;
					Card*dc = dummyCard();
					if(room->getMode()=="03_1v2"){
						QList<int>ids;
						foreach (int id, p->handCards()) {
							if(sc->getSubcards().contains(id))
								ids << id;
						}
						if(ids.length()<3){
							dc->addSubcards(ids);
						}else{
							room->fillAG(ids,source);
							for (int i = 0; i < 2; i++) {
								int id = room->askForAG(source,ids,false,"qiantun");
								room->takeAG(source,id,false,QList<ServerPlayer*>()<<source);
								dc->addSubcard(id);
								ids.removeOne(id);
							}
							room->clearAG(source);
						}
					}else{
						foreach (int id, p->handCards()) {
							if(sc->getSubcards().contains(id))
								dc->addSubcard(id);
						}
					}
					source->obtainCard(dc);
				}else{
					if(source->isDead()) continue;
					Card*dc = dummyCard();
					if(room->getMode()=="03_1v2"){
						QList<int>ids;
						foreach (int id, p->handCards()) {
							if(sc->getSubcards().contains(id)) continue;
							ids << id;
						}
						if(ids.length()<3){
							dc->addSubcards(ids);
						}else{
							room->fillAG(ids,source);
							for (int i = 0; i < 2; i++) {
								int id = room->askForAG(source,ids,false,"qiantun");
								room->takeAG(source,id,false,QList<ServerPlayer*>()<<source);
								dc->addSubcard(id);
								ids.removeOne(id);
							}
							room->clearAG(source);
						}
					}else{
						foreach (int id, p->handCards()) {
							if(sc->getSubcards().contains(id)) continue;
							dc->addSubcard(id);
						}
					}
					source->obtainCard(dc);
				}
			}
			room->showAllCards(source);
		}
	}
}

class QiantunVs : public ZeroCardViewAsSkill
{
public:
	QiantunVs() : ZeroCardViewAsSkill("qiantun")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("QiantunCard")<1&&player->getKingdom()=="wei";
	}

	const Card *viewAs() const
	{
		return new QiantunCard;
	}
};

class Qiantun : public TriggerSkill
{
public:
	Qiantun() : TriggerSkill("qiantun")
	{
		events << AskforPindianCard;
		view_as_skill = new QiantunVs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		if (triggerEvent == AskforPindianCard) {
			PindianStruct *pd = data.value<PindianStruct*>();
			if(pd->reason==objectName()){
				const Card*sc = pd->to->getTag("qiantunCard").value<const Card*>();
				if(sc){
					sc = room->askForExchange(pd->to,objectName(),1,1,false,"qiantun1",false,ListI2S(sc->getSubcards()).join(","));
					if(sc){
						pd->to_card = sc;
						data.setValue(pd);
					}
				}
			}
		}
		return false;
	}
};




WeisiCard::WeisiCard()
{
	handling_method = Card::MethodUse;
	mute = true;
}

bool WeisiCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to!=Self;
}

void WeisiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	source->peiyin("xingweisi");
	foreach (ServerPlayer *p, targets) {
		const Card*sc = room->askForExchange(p,"weisi",998,1,false,"weisi0",true);
		if(sc) p->addToPile("weisi",sc,false);
		Card*dc = Sanguosha->cloneCard("duel");
		dc->setSkillName("_weisi");
		source->addMark(p->objectName()+"weisiUse-PlayClear");
		if(source->canUse(dc,p))
			room->useCard(CardUseStruct(dc,source,p));
	}
}

class WeisiVs : public ZeroCardViewAsSkill
{
public:
	WeisiVs() : ZeroCardViewAsSkill("weisi")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("WeisiCard")<1&&player->getKingdom()=="qun";
	}

	const Card *viewAs() const
	{
		return new WeisiCard;
	}
};

class Weisi : public TriggerSkill
{
public:
	Weisi() : TriggerSkill("weisi")
	{
		events << Damage << EventPhaseChanging;
		view_as_skill = new WeisiVs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->getSkillNames().contains(objectName())){
				if(player->getMark(damage.to->objectName()+"weisiUse-PlayClear")>0){
					if(room->getMode().contains("p"))
						player->obtainCard(dummyCard(damage.to->handCards()),false);
					else if(damage.to->getHandcardNum()>0){
						int id = room->askForCardChosen(player,damage.to,"h",objectName());
						room->obtainCard(player,id,false);
					}
				}
			}
		}else{
			if (data.value<PhaseChangeStruct>().to==Player::NotActive) {
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->getPile("weisi").length()>0){
						p->obtainCard(dummyCard(p->getPile("weisi")),false);
					}
				}
			}
		}
		return false;
	}
};

class Zhaoxiong : public TriggerSkill
{
public:
	Zhaoxiong() : TriggerSkill("zhaoxiong")
	{
		events << EventPhaseStart;
		frequency = Limited;
		limit_mark = "@zhaoxiong";
		setProperty("IgnoreInvalidity",true);
		waked_skills = "dangyi";
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Start&&player->isWounded()&&player->getMark("@zhaoxiong")>0){
				if(player->askForSkillInvoke(this)){
					player->peiyin("xingzhaoxiong");
					room->doSuperLightbox(player, "zhaoxiong");
					room->removePlayerMark(player, "@zhaoxiong");
					room->changeKingdom(player,"qun");
					room->acquireSkill(player,"dangyi");
					if(room->getMode().contains("p")){
						room->changeTranslation(player,"xiezheng",1);
						room->addPlayerMark(player,"ZXChangeXiezheng");
					}
					if(player->getGeneralName().contains("simazhao"))
						player->setAvatarIcon("xing_simazhao2");
				}
			}
		}
		return false;
	}
};

class Dangyi : public TriggerSkill
{
public:
	Dangyi() : TriggerSkill("dangyi$")
	{
		events << DamageCaused;
		setProperty("IgnoreInvalidity",true);
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == DamageCaused) {
			if(player->getMark("dangyiUse-Clear")<1&&player->hasTurn()
			&&player->getMark("&dangyi")<2&&player->hasLordSkill(this,true)){
				DamageStruct damage = data.value<DamageStruct>();
				if(player->askForSkillInvoke(this,damage.to)){
					player->peiyin("xingdangyi");
					player->addMark("dangyiUse-Clear");
					room->addPlayerMark(player,"&dangyi");
					player->damageRevises(data,1);
				}
			}
		}
		return false;
	}
};




mobileXhPackage::mobileXhPackage()
	: Package("mobile_xh")
{





	General *xuanguanqiujian = new General(this, "xuanguanqiujian", "wei", 4);
	xuanguanqiujian->addSkill(new Cuizhen);
	xuanguanqiujian->addSkill(new Kuili);

	General *lizhaojiaobo = new General(this, "lizhaojiaobo", "wei", 4);
	lizhaojiaobo->addSkill(new Zuoyou);
	lizhaojiaobo->addSkill(new ShishouLJ);
	addMetaObject<ZuoyouCard>();

	General *mobile_caomao = new General(this, "mobile_caomao$", "wei", 3);
	mobile_caomao->addSkill(new MobileQianlong);
	mobile_caomao->addSkill(new Weitong);
	skills << new QlQingzheng << new QlJiushi << new QlFangzhu << new QlJuejin << new QlFangzhuBf;
	addMetaObject<QlQingzhengCard>();
	addMetaObject<QlFangzhuCard>();
	addMetaObject<QlJuejinCard>();

	General *chengji = new General(this, "chengji", "wei", 4);
	chengji->addSkill(new Kuangli);
	chengji->addSkill(new Xiongshi);
	addMetaObject<XiongshiCard>();

	General *nsw_simafu = new General(this, "nsw_simafu", "wei", 3);
	nsw_simafu->addSkill(new Pmobileanxiang);
	nsw_simafu->addSkill(new NewChenjie);

	General *mobile_wangjing = new General(this, "mobile_wangjing", "wei", 4);
	mobile_wangjing->addSkill(new Zhujin);
	mobile_wangjing->addSkill(new Jiejian);
	addMetaObject<JiejianCard>();

	General *mobile_wenqin = new General(this, "mobile_wenqin", "wei", 4);
	mobile_wenqin->addSkill(new Beiming);
	mobile_wenqin->addSkill(new Choumang);
	mobile_wenqin->addSkill(new ChoumangBf);

	General *mobile_simazhou = new General(this, "mobile_simazhou", "wei", 4);
	mobile_simazhou->addSkill(new Bifeng);
	mobile_simazhou->addSkill(new Suwang);

	General *mobile_simazhao = new General(this, "mobile_simazhao", "wei", 4);
	mobile_simazhao->addSkill(new Xiezheng);
	mobile_simazhao->addSkill(new Qiantun);
	mobile_simazhao->addSkill(new Weisi);
	mobile_simazhao->addSkill(new Zhaoxiong);
	addMetaObject<XiezhengCard>();
	addMetaObject<QiantunCard>();
	addMetaObject<WeisiCard>();
	skills << new Dangyi;



}
ADD_PACKAGE(mobileXh)





BsHanzhanCard::BsHanzhanCard()
{
}

bool BsHanzhanCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to!=Self;
}

void BsHanzhanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	int n = source->getMaxHp();
	QString xs = source->getTag("bshanzhanXzhenfeng").toString();
	if(xs.contains("2hp")) n = source->getHp();
	else if(xs.contains("2lp")) n = source->getLostHp();
	else if(xs.contains("2ap")) n = source->aliveCount();
	n -= source->getHandcardNum();
	if(n>0) source->drawCards(qMin(n,3),"bshanzhan");
	foreach (ServerPlayer *p, targets) {
		n = p->getMaxHp();
		if(xs.contains("2hp")) n = p->getHp();
		else if(xs.contains("2lp")) n = p->getLostHp();
		else if(xs.contains("2ap")) n = p->aliveCount();
		n -= p->getHandcardNum();
		if(n>0) p->drawCards(qMin(n,3),"bshanzhan");
		Card*dc = Sanguosha->cloneCard("duel");
		dc->setSkillName("_bshanzhan");
		if(source->canUse(dc,p))
			room->useCard(CardUseStruct(dc,source,p));
		dc->deleteLater();
	}
}

class BsHanzhan : public ZeroCardViewAsSkill
{
public:
	BsHanzhan() : ZeroCardViewAsSkill("bshanzhan")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("BsHanzhanCard")<1;
	}

	const Card *viewAs() const
	{
		return new BsHanzhanCard;
	}
};

class Zhanlie : public TriggerSkill
{
public:
	Zhanlie() : TriggerSkill("zhanlie")
	{
		events << EventPhaseChanging << CardFinished << TargetSpecified
		<< ConfirmDamage << CardsMoveOneTime << EventPhaseEnd;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().from==Player::NotActive) {
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(p->hasSkill(objectName())){
						int n = p->getAttackRange();
						QString xs = p->getTag("bshanzhanXzhenfeng").toString();
						if(xs.contains("2hp")) n = p->getHp();
						else if(xs.contains("2lp")) n = p->getLostHp();
						else if(xs.contains("2ap")) n = p->aliveCount();
						room->setPlayerMark(p,"&zhanlie-Clear",n);
					}
				}
			}
		}else if(triggerEvent==EventPhaseEnd){
			int n = player->getMark("&zhan_lie");
			if(n>0&&player->getPhase()==Player::Play&&player->hasSkill(objectName())&&player->askForSkillInvoke(this)){
				player->peiyin(this);
				player->loseMark("&zhan_lie",n);
				player->setMark("zhan_lie",n);
				QList<ServerPlayer *>aps;
				Card*dc = Sanguosha->cloneCard("slash");
				dc->setSkillName("_zhanlie");
				foreach (ServerPlayer *p, room->getAlivePlayers()){
					if(player->canSlash(p,dc)) aps << p;
				}
				int x = 1+Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, player, dc);
				QList<ServerPlayer *>tps = room->askForPlayersChosen(player,aps,objectName(),1,x,"zhanlie0");
				if(tps.length()>0){
					if(n>2){
						n = n/3;
						QStringList choices;
						if(aps.length()>tps.length()) choices << "";
						choices << "zhanlie2" << "zhanlie3" << "zhanlie4";
						for (int i = 0; i < n; i++) {
							QString choice = room->askForChoice(player,objectName(),choices.join("+"));
							if(choice=="cancel") break;
							room->setCardFlag(dc,choice);
							choices.removeOne(choice);
							if(!choices.contains("cancel"))
								choices.append("cancel");
							if(choice=="zhanlie1"){
								foreach (ServerPlayer *p, aps){
									if(tps.contains(p)) aps.removeOne(p);
								}
								ServerPlayer *tp = room->askForPlayerChosen(player,aps,objectName(),"zhanlie10");
								if(tp) tps << tp;
							}
						}
					}
					room->useCard(CardUseStruct(dc,player,tps));
				}
				dc->deleteLater();
			}
		}else if(triggerEvent==TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())){
				if(use.card->hasFlag("zhanlie3")){
					foreach (ServerPlayer *p, use.to){
						if(room->askForCard(p,"..","zhanlie30",data)) continue;
						use.no_respond_list << p->objectName();
					}
				}
				data.setValue(use);
			}
		}else if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())){
				if(use.card->hasFlag("zhanlie4")){
					player->drawCards(2,objectName());
				}
			}
		}else if(triggerEvent==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile&&player->getMark("&zhanlie-Clear")>0){
				foreach (int id, move.card_ids) {
					const Card*c = Sanguosha->getEngineCard(id);
					if(c->isKindOf("Slash")){
						room->removePlayerMark(player,"&zhanlie-Clear");
						if(room->getCardPlace(id)==move.to_place&&player->getMark("&zhan_lie")<6){
							room->sendCompulsoryTriggerLog(player,objectName());
							player->gainMark("&zhan_lie");
						}
					}
				}
			}
		}else if(triggerEvent==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->getSkillNames().contains(objectName())){
				if(damage.card->hasFlag("zhanlie2")){
					player->damageRevises(data,1);
				}
			}
		}
		return false;
	}
};

ZhenfengCard::ZhenfengCard()
{
	target_fixed = true;
}

void ZhenfengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->doSuperLightbox(source, "zhenfeng");
	room->removePlayerMark(source, "@zhenfeng");
	QStringList choices;
	if(source->isWounded())
		choices << "zhenfeng1";
	if(source->hasSkill("bshanzhan",true))
		choices << "zhenfeng2hp=bshanzhan" << "zhenfeng2lp=bshanzhan" << "zhenfeng2ap=bshanzhan";
	if(source->hasSkill("zhanlie",true))
		choices << "zhenfeng2hp=zhanlie" << "zhenfeng2lp=zhanlie" << "zhenfeng2ap=zhanlie";
	if(choices.length()>0){
		for (int i = 0; i < 2; i++) {
			QString choice = room->askForChoice(source,"zhenfeng",choices.join("+"));
			if(choice=="zhenfeng1"){
				room->recover(source,RecoverStruct("zhenfeng",source,2));
				break;
			}else
				choices.removeOne("zhenfeng1");
			QStringList ms = choice.split("=");
			foreach (QString m, choices){
				if(m.contains(ms.last()))
					choices.removeOne(m);
			}
			source->setTag(ms.last()+"Xzhenfeng", ms.first());
		}
	}
}

class Zhenfeng : public ZeroCardViewAsSkill
{
public:
	Zhenfeng() : ZeroCardViewAsSkill("zhenfeng")
	{
		frequency = Limited;
		limit_mark = "@zhenfeng";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@zhenfeng")>0;
	}

	const Card *viewAs() const
	{
		return new ZhenfengCard;
	}
};

DaozhuanCard::DaozhuanCard()
{
	m_skillName = "daozhuan";
	handling_method = Card::MethodUse;
}

bool DaozhuanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("secondzhanyi");
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool DaozhuanCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("secondzhanyi");
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool DaozhuanCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->setSkillName("secondzhanyi");
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

const Card *DaozhuanCard::validate(CardUseStruct &use) const
{
	Room *room = use.from->getRoom();

	QStringList list;
	foreach (QString pn, user_string.split("+")){
		if(use.from->getMark("daozhuan_guhuo_remove_"+pn+"_lun")<1)
			list << pn;
	}
	QString to_pn = room->askForChoice(use.from, "daozhuan", list.join("+"));
	room->addPlayerMark(use.from,"daozhuan_guhuo_remove_"+to_pn+"_lun");
	int x = 0,n = use.from->getMark("daozhuanType-Clear");
	if(n>0){
		ServerPlayer *cp = room->getCurrent();
		if(cp!=use.from){
			const Card*sc = room->askForExchange(use.from,"daozhuan",n,1,true,"daozhuan0:"+QString::number(n),true);
			Card*dc = dummyCard();
			if(sc){
				x = n-sc->subcardsLength();
				dc->addSubcards(sc->getSubcards());
			}else x = n;
			for (int i = 0; i < x; i++) {
				int id = room->askForCardChosen(use.from,cp,"he","daozhuan",false,Card::MethodNone,dc->getSubcards());
				if(id<0) break;
				dc->addSubcard(id);
			}
			room->throwCard(dc,objectName(),nullptr);
		}else{
			const Card*sc = room->askForExchange(use.from,"daozhuan",n,n,true,"daozhuan1:"+QString::number(n));
			if(sc) room->throwCard(sc,objectName(),nullptr);
			room->addPlayerMark(use.from,"daozhuanBan_lun");
			x = n;
		}
	}
	if(x>=n-1)
		room->addPlayerMark(use.from,"daozhuanBan_lun");

	Card *use_card = Sanguosha->cloneCard(to_pn);
	use_card->setSkillName("daozhuan");
	use_card->deleteLater();
	return use_card;
}

const Card *DaozhuanCard::validateInResponse(ServerPlayer *from) const
{
	Room *room = from->getRoom();

	QStringList list;
	foreach (QString pn, user_string.split("+")){
		if(from->getMark("daozhuan_guhuo_remove_"+pn+"_lun")<1)
			list << pn;
	}
	QString to_pn = room->askForChoice(from, "daozhuan", list.join("+"));
	room->addPlayerMark(from,"daozhuan_guhuo_remove_"+to_pn+"_lun");
	int x = 0,n = from->getMark("daozhuanType-Clear");
	if(n>0){
		ServerPlayer *cp = room->getCurrent();
		if(cp!=from){
			const Card*sc = room->askForExchange(from,"daozhuan",n,1,true,"daozhuan0:"+QString::number(n),true);
			Card*dc = dummyCard();
			if(sc){
				dc->addSubcards(sc->getSubcards());
				x = n-sc->subcardsLength();
			}else x = n;
			for (int i = 0; i < x; i++) {
				int id = room->askForCardChosen(from,cp,"he","daozhuan",false,Card::MethodNone,dc->getSubcards());
				if(id<0) break;
				dc->addSubcard(id);
			}
			room->throwCard(dc,objectName(),nullptr);
		}else{
			const Card*sc = room->askForExchange(from,"daozhuan",n,n,true,"daozhuan1:"+QString::number(n));
			if(sc) room->throwCard(sc,objectName(),nullptr);
			room->addPlayerMark(from,"daozhuanBan_lun");
			x = n;
		}
	}
	if(x>=n-1)
		room->addPlayerMark(from,"daozhuanBan_lun");

	Card *use_card = Sanguosha->cloneCard(to_pn);
	use_card->setSkillName("daozhuan");
	use_card->deleteLater();
	return use_card;
}

class DaozhuanVs : public ZeroCardViewAsSkill
{
public:
	DaozhuanVs() : ZeroCardViewAsSkill("daozhuan")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if(player->getMark("daozhuanBan_lun")>0) return false;
		int n = player->getMark("daozhuanType-Clear");
		foreach (const Player *p, player->getAliveSiblings(true)){
			if(p->hasFlag("CurrentPlayer")){
				if(p==player) return p->getCardCount()>=n;
				return p->getCardCount()+player->getCardCount()>=n;
			}
		}
		return false;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if(Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
		&&isEnabledAtPlay(player)){
			foreach (QString pn, pattern.split("+")){
				if(player->getMark("daozhuan_guhuo_remove_"+pn+"_lun")<1){
					Card*dc = Sanguosha->cloneCard(pn);
					if(dc){
						dc->deleteLater();
						if(dc->isKindOf("BasicCard"))
							return true;
					}
				}
			}
		}
		return false;
	}

	const Card *viewAs() const
	{
		QString pn = Sanguosha->getCurrentCardUsePattern();
		if(pn.isEmpty()){
			const Card *c = Self->getTag(objectName()).value<const Card *>();
			if(c) pn = c->objectName();
			else return nullptr;
		}
		DaozhuanCard *sc = new DaozhuanCard;
		sc->setUserString(pn);
		return sc;
	}
};

class Daozhuan : public TriggerSkill
{
public:
	Daozhuan() : TriggerSkill("daozhuan")
	{
		events << PreCardUsed;
		view_as_skill = new DaozhuanVs;
	}
	QDialog *getDialog() const
	{
		return GuhuoDialog::getInstance(objectName(), true, false);
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getMark(use.card->getType()+"daozhuanType-Clear")<1){
				foreach (ServerPlayer *p, room->getAlivePlayers()){
					player->addMark(use.card->getType()+"daozhuanType-Clear");
					room->addPlayerMark(p,"daozhuanType-Clear");
				}
			}
		}
		return false;
	}
};

FujiCard::FujiCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool FujiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length()<subcardsLength()&&to_select!=Self;
}

bool FujiCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == subcardsLength();
}

void FujiCard::onUse(Room *room, CardUseStruct &use) const
{
	room->setTag("FujiData",QVariant::fromValue(use));
	SkillCard::onUse(room,use);
}

void FujiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	int i = 0;
	room->showCard(source,getSubcards());
	CardUseStruct use = room->getTag("FujiData").value<CardUseStruct>();
	foreach (ServerPlayer *p, use.to){
		if(p->isAlive()){
			const Card*c = Sanguosha->getCard(subcards.at(i));
			room->giveCard(source,p,c,"fuji",true);
			if(p->handCards().contains(c->getId())){
				room->setCardTip(c->getId(),"fuji");
				room->setCardFlag(c,"fujiF"+source->objectName());
			}
		}
		i++;
	}
	foreach (ServerPlayer *p, room->getAlivePlayers()){
		if(p->getHandcardNum()<source->getHandcardNum()) return;
	}
	if(source->getGeneralName().endsWith("yuji"))
		source->setAvatarIcon("mobilebs_yuji2");
	source->drawCards(1,objectName());
	room->setPlayerMark(source,"&fuji",1);
}

class FujiVs : public ViewAsSkill
{
public:
	FujiVs() : ViewAsSkill("fuji")
	{
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *) const
	{
		return cards.length()<=Self->getAliveSiblings().length();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		FujiCard *sc = new FujiCard;
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("FujiCard")<1;
	}
};

class Fuji : public TriggerSkill
{
public:
	Fuji() : TriggerSkill("fuji")
	{
		events << CardFinished << ConfirmDamage << EventPhaseChanging << CardUsed;
		view_as_skill = new FujiVs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Jink")){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(use.card->hasFlag("fujiF"+p->objectName())){
						room->sendCompulsoryTriggerLog(p,objectName());
						player->drawCards(1,objectName());
					}
				}
			}
		}else if(triggerEvent==ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(damage.card->hasFlag("fujiF"+p->objectName())){
						room->sendCompulsoryTriggerLog(p,objectName());
						player->damageRevises(data,1);
					}
				}
			}
		} else if (triggerEvent == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(use.card->hasFlag("fujiF"+p->objectName())){
						room->sendCompulsoryTriggerLog(p,objectName());
						QList<int>ids = room->getDiscardPile()+room->getDrawPile();
						qShuffle(ids);
						foreach (int id, ids){
							if(Sanguosha->getCard(id)->getSuit()==use.card->getSuit()){
								room->obtainCard(player,id);
								break;
							}
						}
					}
				}
				if(player->getMark("&fuji")>0){
					if(use.card->isKindOf("Slash")){
						if(player->getMark("fujiSlash")<1){
							player->addMark("fujiSlash");
							room->setCardFlag(use.card,"fujiF"+player->objectName());
						}
					}else if(use.card->isKindOf("Jink")){
						if(player->getMark("fujiJink")<1){
							player->addMark("fujiJink");
							room->setCardFlag(use.card,"fujiF"+player->objectName());
						}
					}
				}
			}
		}else if (triggerEvent == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().from==Player::NotActive&&player->getMark("&fuji")>0) {
				if(player->getGeneralName().endsWith("yuji"))
					player->setAvatarIcon("");
				room->setPlayerMark(player,"&fuji",0);
				player->removeMark("fujiSlash");
				player->removeMark("fujiJink");
			}
		}
		return false;
	}
};

class BsWanglie : public TriggerSkill
{
public:
	BsWanglie() : TriggerSkill("bswanglie")
	{
		events << CardFinished << EventPhaseStart << PreCardUsed;
		waked_skills = "#BsWangliePro";
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("bswanglieBf")){
				room->setPlayerMark(player,"&bswanglie-PlayClear",1);
			}
		} else if (triggerEvent == PreCardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasTip("bswanglie")&&player->getMark(use.card->toString()+"bswanglieId-PlayClear")>0){
				room->sendCompulsoryTriggerLog(player,objectName());
				room->setCardFlag(use.card,"bswanglieBf");
				use.no_respond_list << "_ALL_TARGETS";
				data.setValue(use);
			}
		}else if (triggerEvent == EventPhaseStart) {
			if (player->getPhase()==Player::Play&&player->getHandcardNum()>0&&player->hasSkill(objectName())) {
				const Card*c = room->askForCard(player,".","bswanglie0:",data,Card::MethodNone);
				if(c){
					player->skillInvoked(this);
					room->setCardTip(c->getEffectiveId(),"bswanglie-Clear");
					player->addMark(c->toString()+"bswanglieId-PlayClear");
				}
			}
		}
		return false;
	}
};

class BsWangliePro : public ProhibitSkill
{
public:
	BsWangliePro() : ProhibitSkill("#BsWangliePro")
	{
	}

	bool isProhibited(const Player *from, const Player *to, const Card *, const QList<const Player *> &) const
	{
		return from!=to&&from->getMark("&bswanglie-PlayClear")>0&&from->hasSkill("bswanglie");
	}
};

class BsHongyi : public TriggerSkill
{
public:
	BsHongyi() : TriggerSkill("bshongyi")
	{
		events << EventPhaseStart << GameStart << Damage << Damaged;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart) {
			if (player->getPhase()==Player::Start) {
				int n = player->getMark("&bshong_yi");
				if(n>0){
					room->sendCompulsoryTriggerLog(player,this);
					QString choice = "bshongyi1="+QString::number(n)+"+bshongyi2";
					if(room->askForChoice(player,objectName(),choice)!="bshongyi2"){
						player->drawCards(n,objectName());
						player->addMark("bshongyi1-Clear");
					}else{
						player->setMark("bshong_yi-Clear",n);
						player->loseAllMarks("&bshong_yi");
					}
				}
			}else if(player->getPhase()==Player::Finish){
				int n = player->getMark("bshong_yi-Clear");
				if(n>0) player->drawCards(n,objectName());
				if(player->getMark("bshongyi1-Clear")>0)
					player->loseAllMarks("&bshong_yi");
			}
		}else if(triggerEvent==GameStart){
			room->sendCompulsoryTriggerLog(player,this);
			player->gainMark("&bshong_yi",qMin(2,4-player->getMark("&bshong_yi")));
		}else if(player->getMark("&bshong_yi")<4){
			DamageStruct damage = data.value<DamageStruct>();
			room->sendCompulsoryTriggerLog(player,this);
			player->gainMark("&bshong_yi",qMin(damage.damage,4-player->getMark("&bshong_yi")));
		}
		return false;
	}
};

class BsHaoshi : public TriggerSkill
{
public:
	BsHaoshi() : TriggerSkill("bshaoshi")
	{
		events << EventPhaseStart << CardsMoveOneTime;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	static void BsHaoshiMove(QList<int> ids, bool movein, ServerPlayer *target)
	{
		if(ids.isEmpty()) return;
		QList<CardsMoveStruct> moves;
		Room *room = target->getRoom();
		if (movein) {
			CardsMoveStruct move(ids,room->getCardOwner(ids.first()),target,Player::PlaceHand,Player::PlaceSpecial,
			CardMoveReason(CardMoveReason::S_REASON_PUT,target->objectName(),"bshaoshi",""));
			move.to_pile_name = "&bshaoshi";
			moves.append(move);
		} else {
			CardsMoveStruct move(ids,target,nullptr,Player::PlaceSpecial,Player::PlaceTable,
			CardMoveReason(CardMoveReason::S_REASON_PUT,target->objectName(),"bshaoshi",""));
			move.from_pile_name = "&bshaoshi";
			moves.append(move);
		}
		QList<ServerPlayer *> _caoxiu;
		_caoxiu << target;
		room->notifyMoveCards(true, moves, true, _caoxiu);
		room->notifyMoveCards(false, moves, true, _caoxiu);
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&player->getMark("&bshaoshi+#"+move.to->objectName())>0){
				QList<int> ids;
				foreach (int id, move.to->handCards()){
					if(move.card_ids.contains(id)) ids << id;
				}
				BsHaoshiMove(ids,true,player);
			}
			if(move.from_places.contains(Player::PlaceHand)){
				if(player->getMark("&bshaoshi+#"+move.from->objectName())>0){
					QList<int> ids;
					int n = 0;
					foreach (int id, move.card_ids){
						if(move.from->handCards().contains(id)) continue;
						if(move.from_places.at(n)==Player::PlaceHand) ids << id;
						n++;
					}
					BsHaoshiMove(ids,false,player);
				}
				if(move.is_last_handcard&&player==move.from&&player->objectName()!=move.reason.m_playerId&&player->hasSkill(objectName())
                &&((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE||move.reason.m_reason==CardMoveReason::S_REASON_RESPONSE)){
					ServerPlayer *tp = room->findPlayerByObjectName(move.reason.m_playerId);
					if(tp&&tp->getMark("&bshaoshi+#"+player->objectName())>0){
						room->sendCompulsoryTriggerLog(player,this);
						player->drawCards(player->getMaxHp()-player->getHandcardNum(),objectName());
					}
				}
			}
		}else if (triggerEvent == EventPhaseStart) {
			if (player->getPhase()==Player::Finish&&player->hasSkill(objectName())) {
				QList<ServerPlayer *>tps;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					if(p->getHp()<=player->getHp()) tps << p;
				}
				ServerPlayer *tp = room->askForPlayerChosen(player,tps,objectName(),"bshaoshi0:",true,true);
				if(tp){
					player->peiyin(this);
					room->setPlayerMark(tp,"&bshaoshi+#"+player->objectName(),1);
					BsHaoshiMove(player->handCards(),true,tp);
				}
			}else if(player->getPhase()==Player::RoundStart){
				foreach (ServerPlayer *p, room->getAlivePlayers()){
					if(p->getMark("&bshaoshi+#"+player->objectName())>0){
						//BsHaoshiMove(player->handCards(),false,p);
						QList<CardsMoveStruct> moves;
						CardsMoveStruct move(player->handCards(),p,player,Player::PlaceSpecial,Player::PlaceHand,
						CardMoveReason(CardMoveReason::S_REASON_PUT,p->objectName(),"bshaoshi",""));
						move.from_pile_name = "&bshaoshi";
						moves.append(move);
						QList<ServerPlayer *> _caoxiu;
						_caoxiu << p;
						room->notifyMoveCards(true, moves, true, _caoxiu);
						room->notifyMoveCards(false, moves, true, _caoxiu);
					}
					room->setPlayerMark(p,"&bshaoshi+#"+player->objectName(),0);
				}
			}
		}
		return false;
	}
};

BsDimengCard::BsDimengCard()
{
}

bool BsDimengCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	if(targets.length()==1){
		if(qAbs(targets.first()->getHandcardNum()-to->getHandcardNum())>Self->getLostHp()) return false;
	}
	return targets.length()<2;
}

bool BsDimengCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == 2;
}

void BsDimengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	int n = source->getLostHp();
	room->swapCards(targets.first(),targets.last(),"h","bsdimeng");
	if(n<1) return;
	if(source->canDiscard(source,"he")&&room->askForChoice(source,"bsdimeng","1+2")=="1"){
		room->askForDiscard(source,"bsdimeng",n,n,false,true);
	}else{
		int x = 999;
		foreach (ServerPlayer *p, targets){
			if(p->isAlive()) x = qMin(x,p->getHandcardNum());
		}
		foreach (ServerPlayer *p, targets){
			if(p->isAlive()&&p->getHandcardNum()<=x)
				p->drawCards(n,"bsdimeng");
		}
	}
}

class BsDimeng : public ZeroCardViewAsSkill
{
public:
	BsDimeng() : ZeroCardViewAsSkill("bsdimeng")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("BsDimengCard")<1;
	}

	const Card *viewAs() const
	{
		return new BsDimengCard;
	}
};

class BsXianshuai : public TriggerSkill
{
public:
	BsXianshuai() : TriggerSkill("bsxianshuai")
	{
		events << CardUsed;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->hasFlag("CurrentPlayer")&&use.m_isHandcard
				&&use.card->hasSuit()&&player->getMark(use.card->getSuitString()+"bsxianshuai-Clear")<1){
				room->addPlayerMark(player,use.card->getSuitString()+"bsxianshuai-Clear");
				room->sendCompulsoryTriggerLog(player,this);
				use.m_addHistory = false;
				data.setValue(use);
				foreach (QString m, player->getMarkNames()){
					if(m.contains("&bsxianshuai+:+")){
						room->setPlayerMark(player,m,0);
						m.remove("-Clear");
						QStringList ms = m.split("+");
						ms << use.card->getSuitString()+"_char";
						room->setPlayerMark(player,ms.join("+")+"-Clear",1);
						return false;
					}
				}
				room->setPlayerMark(player,"&bsxianshuai+:+"+use.card->getSuitString()+"_char-Clear",1);
			}
		}
		return false;
	}
};

XiongtuCard::XiongtuCard()
{
}

bool XiongtuCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to!=Self&&to->getHandcardNum()>0;
}

void XiongtuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		int id = room->askForCardChosen(source,p,"h","xiongtu");
		if(id<0) continue;
		room->showCard(p,id);
		int n = 4;
		foreach (QString m, source->getMarkNames()){
			if(m.contains("&xiongtu+:+")){
				QStringList ms = m.split("+");
				n = 6-ms.length();
				break;
			}
		}
		QStringList choices;
		if(source->canDiscard(p,id)) choices << "xiongtu1";
		if(source->getCardCount()>=n) choices << "xiongtu2="+QString::number(n);
		if(choices.isEmpty()) continue;
		if(room->askForChoice(source,"xiongtu",choices.join("+"),id)=="xiongtu1")
			room->throwCard(id,"xiongtu",p,source);
		else{
			room->askForDiscard(source,"xiongtu",n,n,false,true);
			room->damage(DamageStruct("xiongtu",source,p));
		}
	}
}

class Xiongtuvs : public ZeroCardViewAsSkill
{
public:
	Xiongtuvs() : ZeroCardViewAsSkill("xiongtu")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("XiongtuCard")<1;
	}

	const Card *viewAs() const
	{
		return new XiongtuCard;
	}
};

class Xiongtu : public TriggerSkill
{
public:
	Xiongtu() : TriggerSkill("xiongtu")
	{
		events << CardsMoveOneTime;
		view_as_skill = new Xiongtuvs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile&&player->hasSkill(objectName(),true)){
				foreach (int id, move.card_ids){
					const Card*c = Sanguosha->getEngineCard(id);
					if(player->getMark(c->getSuitString()+"xiongtu-Clear")<1){
						player->addMark(c->getSuitString()+"xiongtu-Clear");
						foreach (QString m, player->getMarkNames()){
							if(m.contains("&xiongtu+:+")){
								room->setPlayerMark(player,m,0);
								m.remove("-Clear");
								QStringList ms = m.split("+");
								ms << c->getSuitString()+"_char";
								room->setPlayerMark(player,ms.join("+")+"-Clear",1);
								c = nullptr;
								break;
							}
						}
						if(c)
							room->setPlayerMark(player,"&xiongtu+:+"+c->getSuitString()+"_char-Clear",1);
					}
				}
			}
		}
		return false;
	}
};

class Xiaoge : public TriggerSkill
{
public:
	Xiaoge() : TriggerSkill("xiaoge")
	{
		events << DamageCaused << CardFinished;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("feijingBf"+damage.to->objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				player->damageRevises(data,-damage.damage);
				room->recover(player,RecoverStruct(objectName(),player));
				int id = damage.to->getMark("feijingId");
				if(!room->getCardOwner(id))
					room->obtainCard(player,id);
				return true;
			}
		}else if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("feijingBf")&&use.to.length()==1&&use.to.first()->isAlive()){
				Card*dc = Sanguosha->cloneCard("duel");
				dc->setSkillName("_xiaoge");
				dc->deleteLater();
				if(player->canUse(dc,use.to.first())){
					room->sendCompulsoryTriggerLog(player,this);
					room->useCard(CardUseStruct(dc,player,use.to));
				}
			}
		}
		return false;
	}
};

class FeijingVs : public OneCardViewAsSkill
{
public:
	FeijingVs() : OneCardViewAsSkill("ganjue")
	{
		response_pattern = "slash";
	}

	bool viewFilter(const Card *to_select) const
	{
		return to_select->isDamageCard()&&to_select->isKindOf("TrickCard");
	}

	const Card *viewAs(const Card *c) const
	{
		Card *card = Sanguosha->cloneCard("slash");
		card->addSubcard(c);
		return card;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Slash::IsAvailable(player);
	}
};

class Feijing : public TriggerSkill
{
public:
	Feijing() : TriggerSkill("feijing")
	{
		events << TargetSpecifying;
		view_as_skill = new FeijingVs;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==TargetSpecifying){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")&&use.to.length()==1&&player->askForSkillInvoke(this,data)){
				player->peiyin(this);
				QList<ServerPlayer *>aps;
				room->setCardFlag(use.card,"feijingBf");
				if(room->askForChoice(player,objectName(),"1+2",data)=="1"){
					ServerPlayer *ap = use.to.first()->getNextAlive();
					while(ap!=player){
						aps << ap;
						ap = ap->getNextAlive();
					}
				}else{
					ServerPlayer *ap = player->getNextAlive();
					while(ap!=use.to.first()){
						aps << ap;
						ap = ap->getNextAlive();
					}
				}
				foreach (ServerPlayer *p, aps){
					const Card*c = room->askForCardShow(p,player,objectName());
					if(c) p->setMark("feijingId",c->getId());
					else aps.removeOne(p);
				}
				foreach (ServerPlayer *p, aps)
					room->showCard(p,p->getMark("feijingId"));
				foreach (ServerPlayer *p, aps){
					int id = p->getMark("feijingId");
					if(p->canDiscard(p,id))
						room->throwCard(id,objectName(),p);
					else aps.removeOne(p);
				}
				QString choice = room->askForChoice(player,objectName(),"red+black+cancel",data);
				foreach (ServerPlayer *p, aps){
					const Card*c = Sanguosha->getCard(p->getMark("feijingId"));
					if(c->getColorString()==choice){
						use.to << p;
						room->doAnimate(1,player->objectName(),p->objectName());
						room->setCardFlag(use.card,"feijingBf"+p->objectName());
					}
				}
				if(choice!="cancel"){
					room->sortByActionOrder(use.to);
					data.setValue(use);
				}
			}
		}
		return false;
	}
};

class Zhuangshi : public TriggerSkill
{
public:
	Zhuangshi() : TriggerSkill("zhuangshi")
	{
		events << CardUsed << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getPhase()==Player::Play){
				if(player->getMark("&zhuangshi+1-PlayClear")>0){
					room->removePlayerMark(player,"&zhuangshi+1-PlayClear");
					use.no_respond_list << "_ALL_TARGETS";
					data.setValue(use);
				}
				if(player->getMark("&zhuangshi+2-PlayClear")>0){
					room->removePlayerMark(player,"&zhuangshi+2-PlayClear");
					use.m_addHistory = false;
					data.setValue(use);
				}
			}
		}else if(player->getPhase()==Player::Play){
			if(player->hasSkill(objectName())&&player->askForSkillInvoke(this)){
				int n = qrand()%2+1;
				if(player->property("avatarIcon").toString().endsWith("weiyan2"))
					n += 2;
				player->peiyin(this,n);
				const Card*sc = room->askForDiscard(player,objectName(),999,1,true,false,"zhuangshi1");
				if(sc){
					player->setMark("zhuangshi1-PlayClear",sc->subcardsLength());
					room->setPlayerMark(player,"&zhuangshi+1-PlayClear",sc->subcardsLength());
				}
				QStringList choices;
				for (int i = 1; i <= player->getHp(); i++)
					choices << QString("zhuangshi2=%1").arg(i);
				if(sc) choices << "cancel";
				QString choice = room->askForChoice(player,"zhuangshiLoseHp",choices.join("+"));
				if(choice!="cancel"){
					int n = choice.split("=").last().toInt();
					player->setMark("zhuangshi2-PlayClear",n);
					room->setPlayerMark(player,"&zhuangshi+2-PlayClear",n);
					room->loseHp(player,n,true,player,objectName());
				}
			}
		}
		return false;
	}
};

class Yinzhan : public TriggerSkill
{
public:
	Yinzhan() : TriggerSkill("yinzhan")
	{
		events << DamageCaused << CardFinished;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")){
				bool hp = player->getHp()<=damage.to->getHp();
				bool hn = player->getCardCount()<=damage.to->getCardCount();
				int n = qrand()%3+1;
				if(player->property("avatarIcon").toString().endsWith("weiyan2"))
					n += 3;
				else if(player->property("avatarIcon").toString().endsWith("weiyan3"))
					n += 3;
				if(hp){
					room->sendCompulsoryTriggerLog(player,this,n);
					player->damageRevises(data,1);
				}
				if(hn){
					if(!hp) room->sendCompulsoryTriggerLog(player,this,n);
					room->setCardFlag(damage.card,player->objectName()+"yinzhanBf"+damage.to->objectName());
				}
				if(hp&&hn){
					room->setCardFlag(damage.card,"yinzhanBf");
					room->recover(player,RecoverStruct(objectName(),player));
				}
			}
		}else if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(use.card->hasFlag(player->objectName()+"yinzhanBf"+p->objectName())&&player->canDiscard(p,"he")){
						int id = room->askForCardChosen(player,p,"h",objectName(),false,Card::MethodDiscard);
						if(id>=0){
							room->throwCard(id,objectName(),p,player);
							if(player->isDead()) break;
							if(use.card->hasFlag("yinzhanBf")&&!room->getCardOwner(id)){
								room->obtainCard(player,id);
							}
						}
					}
				}
			}
		}
		return false;
	}
};

class Zhongao : public TriggerSkill
{
public:
	Zhongao() : TriggerSkill("zhongao")
	{
		events << GameStart << Death << CardUsed << Dying << ChoiceMade;
		waked_skills = "tenyearkuanggu,kunfen";
		shiming_skill = true;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(player->getMark("zhongaoBan")>0)
			return false;
		if(triggerEvent==GameStart){
			if(player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this,1);
				room->acquireSkill(player,"tenyearkuanggu");
			}
		}else if(triggerEvent==Death){
			DeathStruct death = data.value<DeathStruct>();
			if(death.damage&&death.damage->from==player&&player->hasSkill(objectName())){
				player->addMark("zhongaoBan");
				if(player->getGeneralName().contains("weiyan"))
					player->setAvatarIcon("mobilebs_weiyan2");
				room->sendShimingLog(player,this,true,qrand()%2+2);
				player->addMark("zhongaoUptenyearkuanggu");
				room->changeTranslation(player,"tenyearkuanggu",1);
				if(player->getMark("zhongaoUse-PlayClear")<player->getMark("zhuangshi1-PlayClear")){
					player->drawCards(1,objectName());
				}
				if(player->getMark("zhongaoUse-PlayClear")<player->getMark("zhuangshi2-PlayClear")){
					if(player->getLostHp()>0) room->recover(player,RecoverStruct(objectName(),player));
					else player->drawCards(1,objectName());
				}
			}
		}else if(triggerEvent==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getPhase()==Player::Play)
				player->addMark("zhongaoUse-PlayClear");
		}else {
			if(triggerEvent==Dying){
				DyingStruct dy = data.value<DyingStruct>();
				if(dy.who!=player) return false;
			}else{
				QString str = data.toString();
				if(!str.contains("skillInvoke:zhuangshi")||str.contains(":yes")) return false;
			}
			if(player->hasSkill(objectName())){
				player->addMark("zhongaoBan");
				room->sendShimingLog(player,this,false,qrand()%2+4);
				room->handleAcquireDetachSkills(player,"-zhuangshi|kunfen");
				if(player->getGeneralName().contains("weiyan"))
					player->setAvatarIcon("mobilebs_weiyan3");
			}
		}
		return false;
	}
};

BsRunweiCard::BsRunweiCard()
{
}

bool BsRunweiCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to->getMark(Self->objectName()+"bsrunweiTo-Clear")<1;
}

void BsRunweiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		room->addPlayerMark(p,source->objectName()+"bsrunweiTo-Clear");
		QStringList choices;
		for (int i = 1; i <= 5; i++)
			choices << QString("bsrunwei1=%1").arg(i);
		QString choice = room->askForChoice(source,"bsrunwei",choices.join("+"));
		choice.remove("bsrunwei1=");
		int n = choice.toInt();
		QList<int>ids = room->showDrawPile(source,n,"bsrunwei",false);
		room->fillAG(ids,source);
		n = room->askForAG(source,ids,false,"bsrunwei","bsrunwei2");
		room->clearAG(source);
		Card*dc = dummyCard();
		foreach (int id, ids) {
			if(Sanguosha->getCard(id)->getColor()==Sanguosha->getCard(n)->getColor())
				dc->addSubcard(id);
		}
		p->obtainCard(dc);
		if(source->isDead()) break;
		if(p==source){
			foreach (int id, p->handCards()) {
				if(dc->getSubcards().contains(id))
					room->setCardTip(id,"bsrunwei-PlayClear");
			}
		}
		source->addMark("bsrunweiUse-PlayClear");
		if(source->getMark("bsrunweiUse-PlayClear")<2)
			source->setMark("bsrunweiNum-PlayClear",dc->subcardsLength());
	}
}

class BsRunweivs : public ZeroCardViewAsSkill
{
public:
	BsRunweivs() : ZeroCardViewAsSkill("bsrunwei")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("BsRunweiCard")<1;
	}

	const Card *viewAs() const
	{
		return new BsRunweiCard;
	}
};

class BsRunwei : public TriggerSkill
{
public:
	BsRunwei() : TriggerSkill("bsrunwei")
	{
		events << CardsMoveOneTime << EventPhaseEnd;
		view_as_skill = new BsRunweivs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from==player&&player->getMark("bsrunweiNum-PlayClear")>0){
				if(move.to!=move.from||(move.to_place!=Player::PlaceHand&&move.to_place!=Player::PlaceEquip)){
					if(move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip)){
						for (int i = 0; i < move.from_places.length(); i++){
							if(move.from_places.at(i)==Player::PlaceHand||move.from_places.at(i)==Player::PlaceEquip){
								player->removeMark("bsrunweiNum-PlayClear");
							}
						}
						if(player->getMark("bsrunweiNum-PlayClear")<1)
							room->addPlayerHistory(player,"BsRunweiCard",0);
					}
				}
			}
		}else{
			if(player->getPhase()==Player::Play&&player->getMark("bsrunweiUse-PlayClear")>0){
				QList<int>ids;
				foreach (const Card*h, player->getHandcards()) {
					if(h->hasTip(objectName())&&player->canDiscard(h->getId()))
						ids << h->getId();
				}
				room->throwCard(ids,objectName(),player);
			}
		}
		return false;
	}
};

class Shuanghuai : public TriggerSkill
{
public:
	Shuanghuai() : TriggerSkill("shuanghuai")
	{
		events << DamageInflicted;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			bool has = false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)){
				if(p->isAlive()&&p->hasTurn()&&p->hasSkill(objectName())&&p->getMark("shuanghuaiUse-Clear")<1
				&&p->distanceTo(player)==1&&p->askForSkillInvoke(objectName()+"$-1",player)){
					p->addMark("shuanghuaiUse-Clear");
					QList<int>ids;
					foreach (int id, room->getDiscardPile()) {
						if(Sanguosha->getCard(id)->isKindOf("Peach"))
							ids << id;
					}
					QString tp = p->getTag("shuanghuaiTo").toString();
					if(ids.length()>0&&room->askForChoice(p,objectName(),"1+2",data)=="2"){
						room->fillAG(ids,player);
						int id = room->askForAG(player,ids,false,objectName(),"shuanghuai2");
						room->clearAG(player);
						room->obtainCard(player,id);
					}else{
						has = player->damageRevises(data,-damage.damage);
					}
					p->setTag("shuanghuaiTo", player->objectName());
					if(p->isDead()) continue;
					if(tp!=player->objectName()){
						room->loseHp(p,1,true,p,objectName());
					}else{
						QList<ServerPlayer *>aps;
						aps << p << player;
						room->sortByActionOrder(aps);
						room->drawCards(aps,1,objectName());
					}
				}
			}
			return has;
		}
		return false;
	}
};

GanggengCard::GanggengCard()
{
	will_throw = false;
}

bool GanggengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty()&&to_select!=Self;
}

void GanggengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &tos) const
{
	foreach (ServerPlayer *p, tos){
		room->setPlayerMark(p,"&ganggeng+#"+source->objectName()+"-Clear",1);
		room->giveCard(source,p,this,"ganggeng");
	}
}

class Ganggengvs : public ViewAsSkill
{
public:
	Ganggengvs() : ViewAsSkill("ganggeng")
	{
	}

	bool viewFilter(const QList<const Card *> &, const Card *card) const
	{
		return !card->isEquipped();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.length()<2) return nullptr;
		GanggengCard *sc = new GanggengCard;
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("GanggengCard")<1&&player->getHandcardNum()>1;
	}
};

class Ganggeng : public TriggerSkill
{
public:
	Ganggeng() : TriggerSkill("ganggeng")
	{
		events << EventPhaseChanging;
		view_as_skill = new Ganggengvs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to==Player::NotActive) {
				foreach (ServerPlayer *q, room->getOtherPlayers(player)){
					if(q->getMark("&ganggeng+#"+player->objectName()+"-Clear")>0){
						bool has = true;
						foreach (ServerPlayer *t, room->getAlivePlayers()){
							if(t->getHandcardNum()>q->getHandcardNum()){
								has = false;
								break;
							}
						}
						if(has){
							player->drawCards(1,objectName());
						}else if(player->canDiscard(q,"hej")){
							room->doAnimate(1,player->objectName(),q->objectName());
							int id = room->askForCardChosen(player,q,"hej",objectName(),false,Card::MethodDiscard);
							if(id>=0) room->throwCard(id,objectName(),q,player);
						}
						if(player->isDead()) break;
					}
				}
			}
		}
		return false;
	}
};

class BsSijian : public TriggerSkill
{
public:
	BsSijian() : TriggerSkill("bssijian")
	{
		events << CardsMoveOneTime << EnterDying << CardFinished;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getMark("&bssijian+1")>0){
				room->setPlayerMark(player,"&bssijian+1",0);
				room->askForDiscard(player,objectName(),1,1,false,true);
			}
			return false;
		}
		if(player->getMark("&bssijianUse-Clear")>1||!player->hasTurn()||!player->hasSkill(objectName())) return false;
		if(triggerEvent==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(!move.is_last_handcard||move.from!=player) return false;
		}
		if(!player->askForSkillInvoke(objectName()+"$-1",data)) return false;
		player->addMark("&bssijianUse-Clear");
		QString choice = "bssijian1+bssijian2+beishui";
		foreach (ServerPlayer *q, room->getAlivePlayers()){
			if(q->hasFlag("Global_Dying")){
				choice = "bssijian1+bssijian2";
				break;
			}
		}
		choice = room->askForChoice(player,objectName(),choice,data);
		if(choice=="beishui"){
			int n = player->getMark("bssijian_beishui");
			if(n>0){
				room->loseHp(player,n,true,player,objectName());
				if(player->isDead()) return false;
			}
			player->addMark("bssijian_beishui");
		}
		if(choice!="bssijian2"){
			ServerPlayer *tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"bssijian10");
			if(tp){
				room->doAnimate(1,player->objectName(),tp->objectName());
				room->setPlayerMark(tp,"&bssijian+1",1);
			}
		}
		if(choice!="bssijian1"){
			room->getCurrent()->drawCards(2,objectName());
		}
		return false;
	}
};

class Quanchong : public TriggerSkill
{
public:
	Quanchong() : TriggerSkill("quanchong")
	{
		events << EventPhaseStart;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(triggerEvent==EventPhaseStart){
			if(player->getPhase()==Player::Finish){
				if(player->getMark("quanchongUse_lun")==0&&player->hasSkill(objectName())&&player->canDiscard("he")){
					room->sendCompulsoryTriggerLog(player,this);
					player->addMark("quanchongUse_lun");
					player->throwAllHandCardsAndEquips(objectName());
				}
			}else if(player->getPhase()==Player::NotActive){
				if(player->getMark("quanchongUse_lun")==1){
					player->addMark("quanchongUse_lun");
					player->gainAnExtraTurn();
				}
			}else if(player->getPhase()==Player::RoundStart){
				if(player->getMark("quanchongUse_lun")==2){
					foreach (ServerPlayer *p, room->getAlivePlayers()){
						if(p!=player&&p->getHp()>=player->getHp()){
							room->loseHp(player,1,true,player,objectName());
							break;
						}
					}
				}
			}
		}
		return false;
	}
};

class Renxing : public TriggerSkill
{
public:
	Renxing() : TriggerSkill("renxing")
	{
		events << CardsMoveOneTime << CardResponded << CardFinished;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile&&room->getCurrent()->getPhase()!=Player::Discard
			&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD){
				player->addMark("renxingDis-Clear");
				if(player->getMark("renxingDis-Clear")<2&&player->hasSkill(objectName())
				&&player->askForSkillInvoke(objectName()+"$-1",data)){
					QList<ServerPlayer *>tps;
					foreach (ServerPlayer *p, room->getAlivePlayers()){
						if(p->getMark("renxingSlash-Clear")<1&&player->canDiscard(p,"he"))
							tps << p;
					}
					ServerPlayer *tp = room->askForPlayerChosen(player,tps,objectName(),"renxing0",true);
					if(tp){
						room->doAnimate(1,player->objectName(),tp->objectName());
						int id = room->askForCardChosen(player,tp,"he",objectName(),false,Card::MethodDiscard);
						if(id>=0) room->throwCard(id,objectName(),tp,player);
					}else{
						tps.clear();
						tps << room->getCurrent() << player;
						room->drawCards(tps,1,objectName());
					}
				}
			}
		}else{
			const Card*c = nullptr;
			if(triggerEvent==CardFinished){
				c = data.value<CardUseStruct>().card;
			}else
				c = data.value<CardResponseStruct>().m_card;
			if(c&&c->isKindOf("Slash")) player->addMark("renxingSlash-Clear");
		}
		return false;
	}
};

	static void bsqingshiUse(Room *room, ServerPlayer *player, ServerPlayer *to)
	{
		LogMessage log;
		log.type = "#ChoosePlayerWithSkill";
		log.from = player;
		log.arg = "bsqingshi";
		log.to << to;
		room->sendLog(log);
		player->peiyin("bsqingshi");
		room->doAnimate(1,player->objectName(),to->objectName());
		room->notifySkillInvoked(player, "bsqingshi");
		if(player->isYourFriend(to)){
			QList<ServerPlayer *>aps;
			aps << player << to;
			room->sortByActionOrder(aps);
			room->drawCards(aps,1,"bsqingshi");
		}else{
			room->askForDiscard(player,"bsqingshi",1,1,false,true);
			if(player->canDiscard(to,"he")){
				int id = room->askForCardChosen(player,to,"he","bsqingshi",false,Card::MethodDiscard);
				if(id>=0) room->throwCard(id,"bsqingshi",to,player);
			}
		}
	}

JiejieCard::JiejieCard()
{
	mute = true;
}

bool JiejieCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *) const
{
	return targets.isEmpty()&&to->hasSkill("jiejie");
}

void JiejieCard::onUse(Room *room, CardUseStruct &use) const
{
	QVariant data = QVariant::fromValue(use);
	room->getThread()->trigger(PreCardUsed, room, use.from, data);
	room->getThread()->trigger(CardUsed, room, use.from, data);
	use = data.value<CardUseStruct>();
	foreach (ServerPlayer *p, use.to) {
		use.from->skillInvoked("jiejie",-1,p);
		if(p!=use.from) room->doGongxin(use.from,p,p->handCards(),"jiejie");
		QString choice = "heart+diamond+spade+club+cancel";
		choice = room->askForChoice(p,"jiejie",choice,QVariant::fromValue(use.from));
		if(choice=="cancel") continue;
		bool has = false;
		QList<int>ids;
		QStringList suits;
		foreach (const Card*h, use.from->getHandcards()) {
			if(h->getSuitString()==choice){
				has = true;
			}else if(use.from->canDiscard(h->getId())){
				ids << h->getId();
			}
			if(suits.contains(h->getSuitString())) continue;
			suits.append(h->getSuitString());
		}
		if(has){
			room->setPlayerMark(use.from,"&jiejie+"+choice+"_char-Clear",1);
			room->throwCard(ids,objectName(),use.from);
		}else{
			foreach (int id, room->getDrawPile()+room->getDiscardPile()) {
				Card*c = Sanguosha->getCard(id);
				if(c->getSuitString()==choice){
					room->obtainCard(use.from,c);
					break;
				}
			}
		}
		if(use.from->isDead()||p->isDead()||p->getMark("jiejievsUse_lun")>1) continue;
		if(p->getMark("jiejievsNum_lun")<suits.length()){
			p->setMark("jiejievsNum_lun",suits.length());
			p->addMark("jiejievsUse_lun");
			bsqingshiUse(room,p,use.from);
		}
	}
	room->getThread()->trigger(CardFinished, room, use.from, data);
	use = data.value<CardUseStruct>();
}

class Jiejievs : public ZeroCardViewAsSkill
{
public:
	Jiejievs() : ZeroCardViewAsSkill("jiejievs&")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("JiejieCard")<1;
	}

	const Card *viewAs() const
	{
		return new JiejieCard;
	}
};

class Jiejie : public TriggerSkill
{
public:
	Jiejie() : TriggerSkill("jiejie")
	{
		events << EventPhaseProceeding << EventPhaseEnd << EventAcquireSkill;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==EventPhaseEnd){
			if(player->getPhase()==Player::Play)
				room->detachSkillFromPlayer(player,"jiejievs",true);
		}else if(triggerEvent==EventPhaseProceeding){
			if(player->getPhase()==Player::Play){
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->hasSkill(objectName(),true)){
						room->attachSkillToPlayer(player,"jiejievs");
						break;
					}
				}
			}
		}else if(data.toString()==objectName()){
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if(p->getPhase()==Player::Play)
					room->attachSkillToPlayer(p,"jiejievs");
			}
		}
		return false;
	}
};

class BsQingshi : public TriggerSkill
{
public:
	BsQingshi() : TriggerSkill("bsqingshi")
	{
		events << Damaged;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		ServerPlayer *tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"bsqingshi0",true);
		if(tp){
			bsqingshiUse(room,player,tp);
		}
		return false;
	}
};

class Qingdao : public TriggerSkill
{
public:
	Qingdao() : TriggerSkill("qingdao")
	{
		events << PreCardUsed << CardFinished;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isDamageCard()){
				foreach (ServerPlayer *p, use.to){
					if(p->isAlive()&&player!=p&&p->hasSkill(objectName())
					&&p->askForSkillInvoke(objectName()+"$-1",data)){
						if(use.card->hasFlag("DamageDone_"+p->objectName())){
							QList<ServerPlayer *>tps;
							foreach (ServerPlayer *q, room->getAlivePlayers()){
								if(p->canDiscard(q,"hej")) tps << q;
							}
							ServerPlayer *tp = room->askForPlayerChosen(p,tps,objectName(),"qingdao0",true);
							if(tp){
								room->doAnimate(1,p->objectName(),tp->objectName());
								int id = room->askForCardChosen(p,tp,"hej",objectName(),false,Card::MethodDiscard);
								if(id>=0) room->throwCard(id,objectName(),tp,p);
							}else{
								foreach (int id, room->getDrawPile()+room->getDiscardPile()){
									if(Sanguosha->getCard(id)->isKindOf("Jink")){
										room->obtainCard(p,id);
										break;
									}
								}
							}
						}else{
							room->setPlayerFlag(p,"qingdaoBf");
							const Card*h = room->askForUseCard(p,".|.|.|hand","qingdao1");
							room->setPlayerFlag(p,"-qingdaoBf");
							if(h==nullptr){
								foreach (int id, room->getDrawPile()+room->getDiscardPile()){
									if(Sanguosha->getCard(id)->isKindOf("Slash")){
										room->obtainCard(p,id);
										break;
									}
								}
							}
						}
					}
				}
			}
		}else{
			if(player->hasFlag("qingdaoBf"))
				room->setPlayerFlag(player,"-qingdaoBf");
		}
		return false;
	}
};

class Xiugeng : public TriggerSkill
{
public:
	Xiugeng() : TriggerSkill("xiugeng")
	{
		events << EventPhaseStart;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(triggerEvent==EventPhaseStart){
			if(player->getPhase()==Player::Draw){
				foreach (QString m, player->getMarkNames()) {
					if(m.contains("&xiugeng+")){
						foreach (ServerPlayer *p, room->getAlivePlayers()) {
							if(m.endsWith(p->objectName())){
								QStringList ms = m.split("+");
								int n = ms[1].toInt();
								if(n<=player->getHandcardNum())
									player->drawCards(2,objectName());
								if(n>=player->getHandcardNum())
									room->addMaxCards(player,1,false);
								break;
							}
						}
						if(player->isDead()) break;
						room->setPlayerMark(player,m,0);
					}
				}
			}else if(player->getPhase()==Player::RoundStart){
				if(player->hasSkill(objectName())){
					foreach (ServerPlayer *p, room->askForPlayersChosen(player,room->getAlivePlayers(),objectName()+"$-1",0,2,"xiugeng0",true,true))
						room->setPlayerMark(p,QString("&xiugeng+%1+#%2").arg(p->getHandcardNum()).arg(player->objectName()),1);
				}
			}
		}
		return false;
	}
};

class Chenshe : public TriggerSkill
{
public:
	Chenshe() : TriggerSkill("chenshe")
	{
		events << Dying;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==Dying){
			DyingStruct dy = data.value<DyingStruct>();
			if(dy.who!=player&&player->askForSkillInvoke(objectName()+"$-1",dy.who)){
				QList<ServerPlayer*>tps;
				tps << player << dy.who;
				if(dy.damage&&dy.damage->from)
					tps << dy.damage->from;
				QStringList suits;
				foreach (ServerPlayer *p, tps) {
					if(player->canDiscard(p,"he")){
						int id = room->askForCardChosen(player,p,"he",objectName(),false,Card::MethodDiscard);
						if(id>=0){
							room->throwCard(id,objectName(),p,player);
							suits << Sanguosha->getCard(id)->getSuitString();
						}
					}
				}
				if(suits.length()==tps.length()){
					foreach (QString s, suits) {
						if(s!=suits.last()) return false;
					}
					room->recover(dy.who,RecoverStruct(objectName(),player,dy.who->getMaxHp()-dy.who->getHp()));
					room->detachSkillFromPlayer(player,objectName());
				}
			}
		}
		return false;
	}
};

	static int getChargeMax(const Player *player)
	{
		int n = 0;
		foreach (const Skill *s, player->getVisibleSkillList()){
			QString cn = s->property("ChargeNum").toString();
			if(cn.contains("/")) n += cn.split("/").last().toInt();
		}
		return n;
	}

BsTuntianCard::BsTuntianCard()
{
}

bool BsTuntianCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *) const
{
	return targets.isEmpty()&&to->getCardCount()>0;
}

bool BsTuntianCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	return targets.length()>0||Self->getCardCount()>0;
}

void BsTuntianCard::onUse(Room *room, CardUseStruct &use) const
{
	if(use.to.isEmpty()) use.to << use.from;
	SkillCard::onUse(room,use);
}

void BsTuntianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
        room->removePlayerMark(source,"&charge_num");
		Card*dc = room->askForExchange(p,"bstuntian",1,1,true,"bstuntian0:"+source->objectName());
		if(dc) source->addToPile("bstun_tian",dc);
	}
}

class BsTuntianvs : public ZeroCardViewAsSkill
{
public:
	BsTuntianvs() : ZeroCardViewAsSkill("bstuntian")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("BsTuntianCard")<1&&player->getMark("&charge_num")>0;
	}

	const Card *viewAs() const
	{
		return new BsTuntianCard;
	}
};

class BsTuntian : public TriggerSkill
{
public:
	BsTuntian() : TriggerSkill("bstuntian")
	{
		events << EventPhaseChanging << CardsMoveOneTime << MarkChanged;
		view_as_skill = new BsTuntianvs;
		setProperty("ChargeNum","1/3");
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from==player&&!player->hasFlag("CurrentPlayer")&&player->hasSkill(objectName())){
				if(move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip)||move.from_pile_names.contains("bstun_tian")){
					int n = getChargeMax(player)-player->getMark("&charge_num");
					if(n>0) player->gainMark("&charge_num");
				}
			}
		}else if(triggerEvent==EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to==Player::NotActive
			&&player->getMark("bstuntianNum-Clear")>0&&player->hasSkill(objectName())) {
				room->sendCompulsoryTriggerLog(player,objectName());
				player->drawCards(player->getMark("bstuntianNum-Clear"),objectName());
			}
		}else{
			MarkStruct mark = data.value<MarkStruct>();
			if(mark.name=="&charge_num"&&mark.gain<0){
				player->addMark("bstuntianNum-Clear",-mark.gain);
			}
		}
		return false;
	}
};

class BsJixivs : public OneCardViewAsSkill
{
public:
	BsJixivs() : OneCardViewAsSkill("bsjixi")
	{
		expand_pile = "bstun_tian";
	}

	bool viewFilter(const Card *to_select) const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			return Self->getPile("bstun_tian").contains(to_select->getId())&&to_select->isAvailable(Self);
		}
		foreach (QString cn, pattern.split("+")) {
			if(to_select->objectName().endsWith(cn))
				return Self->getPile("bstun_tian").contains(to_select->getId());
		}
		return false;
	}

	const Card *viewAs(const Card *c) const
	{
		return c;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		foreach (QString cn, pattern.split("+")) {
			foreach (int id, player->getPile("bstun_tian")) {
				if(Sanguosha->getEngineCard(id)->objectName().endsWith(cn))
					return player->hasSkill("bstuntian",true)&&player->getMark("&zheng_rong")>0;
			}
		}
		return pattern == "@@bsjixi";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		foreach (int id, player->getPile("bstun_tian")) {
			if(Sanguosha->getEngineCard(id)->isAvailable(player))
				return player->hasSkill("bstuntian",true)&&player->getMark("&zheng_rong")>0;
		}
		return false;
	}
};

class BsJixi : public TriggerSkill
{
public:
	BsJixi() : TriggerSkill("bsjixi")
	{
		events << PreCardUsed;
		view_as_skill = new BsJixivs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(player->getPile("bstun_tian").contains(use.card->getId())){
				player->skillInvoked(objectName());
				player->loseMark("&zheng_rong");
				use.m_addHistory = false;
				data.setValue(use);
			}
		}
		return false;
	}
};

class BsZaoxian : public TriggerSkill
{
public:
	BsZaoxian() : TriggerSkill("bszaoxian")
	{
		events << EventPhaseChanging;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		if(triggerEvent==EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to==Player::NotActive){
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					if(p->hasSkill(objectName())&&p->hasSkill("bstuntian",true)){
						int n = p->getMark("&charge_num");
						if(n==0||n==3){
							room->sendCompulsoryTriggerLog(p,this);
							p->gainMark("&zheng_rong");
						}
					}
				}
			}
		}
		return false;
	}
};

BsQingyanCard::BsQingyanCard()
{
	m_skillName = "bsqingyan";
	target_fixed = true;
	handling_method = Card::MethodNone;
}

const Card *BsQingyanCard::validate(CardUseStruct &use) const
{
	Room *room = use.from->getRoom();
	use.from->skillInvoked("bsqingyan");
	room->addPlayerMark(use.from,"&bsqingyan_lun");
	room->showCard(use.from,subcards);
	foreach (int id, subcards) {
		room->setCardTip(id,"bsqingyan");
	}
	Card *use_card = nullptr;
	if(user_string.contains("jink"))
		use_card = Sanguosha->cloneCard("jink");
	else
		use_card = Sanguosha->cloneCard("nullification");
	use_card->setSkillName("_bsqingyan");
	use_card->deleteLater();
	return use_card;
}

const Card *BsQingyanCard::validateInResponse(ServerPlayer *from) const
{
	Room *room = from->getRoom();
	from->skillInvoked("bsqingyan");
	room->addPlayerMark(from,"&bsqingyan_lun");
	room->showCard(from,subcards);
	foreach (int id, subcards) {
		room->setCardTip(id,"bsqingyan");
	}
	Card *use_card = nullptr;
	if(user_string.contains("jink"))
		use_card = Sanguosha->cloneCard("jink");
	else
		use_card = Sanguosha->cloneCard("nullification");
	use_card->setSkillName("_bsqingyan");
	use_card->deleteLater();
	return use_card;
}

class BsQingyan : public ViewAsSkill
{
public:
	BsQingyan() : ViewAsSkill("bsqingyan")
	{
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *card) const
	{
		return cards.length()<=Self->getMark("&bsqingyan_lun")&&!card->isEquipped();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.length()<=Self->getMark("&bsqingyan_lun")) return nullptr;
		BsQingyanCard *sc = new BsQingyanCard;
		sc->setUserString(Sanguosha->getCurrentCardUsePattern());
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		foreach (const Card *h, player->getHandcards()) {
			if(h->hasFlag("bsqingyan")) return false;
		}
		return Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE_USE
		&&(pattern.contains("jink")||pattern.contains("nullification"))&&player->getHandcardNum()>qMin(4,player->getMark("&bsqingyan_lun"));
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}
};

CeduanCard::CeduanCard()
{
}

bool CeduanCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&to->inMyAttackRange(Self);
}

void CeduanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		QHash<QString,int> cs;
		int x = 0;
		foreach (ServerPlayer *q, room->getAllPlayers()) {
			q->setMark("ceduanId",-1);
			if(p->inMyAttackRange(q)){
				const Card*dc = room->askForCardShow(q,p,"ceduan");
				if(dc){
					cs[dc->getColorString()]++;
					x = qMax(x,cs[dc->getColorString()]);
					q->setMark("ceduanId",dc->getEffectiveId());
				}
			}
		}
		foreach (ServerPlayer *q, room->getAllPlayers()) {
			int id = q->getMark("ceduanId");
			if(id<0) continue;
			room->showCard(q,id);
		}
		QStringList cts;
		foreach (QString m, cs.keys()) {
			if(cs[m]>=x) cts << m;
		}
		Card*dc = Sanguosha->cloneCard("slash");
		dc->setSkillName("_ceduan");
		foreach (const Card*h, source->getHandcards()) {
			if(cts.contains(h->getColorString())) dc->addSubcard(h);
		}
		if(source->canSlash(p,dc,false)){
			room->useCard(CardUseStruct(dc,source,p));
		}
		dc->deleteLater();
	}
}

class Ceduanvs : public ZeroCardViewAsSkill
{
public:
	Ceduanvs() : ZeroCardViewAsSkill("ceduan")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("CeduanCard")<1;
	}

	const Card *viewAs() const
	{
		return new CeduanCard;
	}
};

class Ceduan : public TriggerSkill
{
public:
	Ceduan() : TriggerSkill("ceduan")
	{
		events << CardFinished;
		view_as_skill = new Ceduanvs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const
	{
		if(triggerEvent==CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getSkillNames().contains(objectName())&&use.card->hasFlag("DamageDone")){
				player->drawCards(1,objectName());
			}
		}
		return false;
	}
};








mobileBsQiPackage::mobileBsQiPackage()
	: Package("mobilebs_qi")
{
	General *mobilebs_yuji = new General(this, "mobilebs_yuji", "qun", 3);
	mobilebs_yuji->addSkill(new Daozhuan);
	mobilebs_yuji->addSkill(new Fuji);
	addMetaObject<DaozhuanCard>();
	addMetaObject<FujiCard>();
	
	General *mobilebs_yanghong = new General(this, "mobilebs_yanghong", "qun", 4);
	mobilebs_yanghong->addSkill(new MobileJianji);
	mobilebs_yanghong->addSkill(new MobileYuanmo);
	addMetaObject<MobileJianjiCard>();

	General *mobilebs_lougui = new General(this, "mobilebs_lougui", "wei", 3);
	mobilebs_lougui->addSkill(new MobileJiyu);
	mobilebs_lougui->addSkill(new Guansha);
	addMetaObject<MobileJiyuCard>();

	General *mobilebs_sunshao = new General(this, "mobilebs_sunshao", "wu", 4);
	mobilebs_sunshao->addSkill(new Ganjue);
	mobilebs_sunshao->addSkill(new Zhuhe);
	addMetaObject<GanjueCard>();

	General *mobilebs_dengai = new General(this, "mobilebs_dengai", "wei", 3);
	mobilebs_dengai->addSkill(new BsTuntian);
	mobilebs_dengai->addSkill(new BsJixi);
	mobilebs_dengai->addSkill(new BsZaoxian);
	addMetaObject<BsTuntianCard>();









}
ADD_PACKAGE(mobileBsQi)

mobileBsZhengPackage::mobileBsZhengPackage()
	: Package("mobilebs_zheng")
{
	General *mobilebs_taishici = new General(this, "mobilebs_taishici", "wu", 4);
	mobilebs_taishici->addSkill(new BsHanzhan);
	mobilebs_taishici->addSkill(new Zhanlie);
	mobilebs_taishici->addSkill(new Zhenfeng);
	addMetaObject<BsHanzhanCard>();
	addMetaObject<ZhenfengCard>();

	General *mobilebs_chendao = new General(this, "mobilebs_chendao", "shu", 4);
	mobilebs_chendao->addSkill(new BsWanglie);
	mobilebs_chendao->addSkill(new BsWangliePro);
	mobilebs_chendao->addSkill(new BsHongyi);

	General *mobilebs_sunjun = new General(this, "mobilebs_sunjun", "wu", 3);
	mobilebs_sunjun->addSkill(new BsXianshuai);
	mobilebs_sunjun->addSkill(new Xiongtu);
	addMetaObject<XiongtuCard>();
	
	General *mobilebs_tianfeng = new General(this, "mobilebs_tianfeng", "qun", 3);
	mobilebs_tianfeng->addSkill(new Ganggeng);
	mobilebs_tianfeng->addSkill(new BsSijian);
	addMetaObject<GanggengCard>();

	General *mobilebs_guoyuan = new General(this, "mobilebs_guoyuan", "wei", 3);
	mobilebs_guoyuan->addSkill(new Qingdao);
	mobilebs_guoyuan->addSkill(new Xiugeng);
	mobilebs_guoyuan->addSkill(new Chenshe);




}
ADD_PACKAGE(mobileBsZheng)

mobileBsShiPackage::mobileBsShiPackage()
	: Package("mobilebs_shi")
{
	General *mobilebs_zhangyan = new General(this, "mobilebs_zhangyan", "qun", 4);
	mobilebs_zhangyan->addSkill(new Xiaoge);
	mobilebs_zhangyan->addSkill(new Feijing);

	General *mobilebs_weiyan = new General(this, "mobilebs_weiyan", "shu", 4);
	mobilebs_weiyan->addSkill(new Zhuangshi);
	mobilebs_weiyan->addSkill(new Yinzhan);
	mobilebs_weiyan->addSkill(new Zhongao);
	//Sanguosha->setAudioType("mobilebs_weiyan","kunfen","5,6");

	General *mobilebs_pangxi = new General(this, "mobilebs_pangxi", "shu", 3);
	mobilebs_pangxi->addSkill(new Xuye);
	mobilebs_pangxi->addSkill(new MobileKuangxiang);
	addMetaObject<MobileKuangxiangCard>();

	General *mobilebs_chenzhi = new General(this, "mobilebs_chenzhi", "shu", 3);
	mobilebs_chenzhi->addSkill(new Quanchong);
	mobilebs_chenzhi->addSkill(new Renxing);

	General *mobilebs_chenjiao = new General(this, "mobilebs_chenjiao", "wei", 3);
	mobilebs_chenjiao->addSkill(new BsQingyan);
	mobilebs_chenjiao->addSkill(new Ceduan);
	addMetaObject<BsQingyanCard>();
	addMetaObject<CeduanCard>();



}
ADD_PACKAGE(mobileBsShi)

mobileBsJiePackage::mobileBsJiePackage()
	: Package("mobilebs_jie")
{
	General *mobilebs_lusu = new General(this, "mobilebs_lusu", "wu", 3);
	mobilebs_lusu->addSkill(new BsHaoshi);
	mobilebs_lusu->addSkill(new BsDimeng);
	addMetaObject<BsDimengCard>();

	General *mobilebs_luyusheng = new General(this, "mobilebs_luyusheng", "wu", 3,false);
	mobilebs_luyusheng->addSkill(new BsRunwei);
	mobilebs_luyusheng->addSkill(new Shuanghuai);
	addMetaObject<BsRunweiCard>();

	General *mobilebs_xinxianying = new General(this, "mobilebs_xinxianying", "wei", 3,false);
	mobilebs_xinxianying->addSkill(new Jiejie);
	mobilebs_xinxianying->addSkill(new BsQingshi);
	skills << new Jiejievs;
	addMetaObject<JiejieCard>();

	General *mobilebs_huanjie = new General(this, "mobilebs_huanjie", "wei", 3);
	mobilebs_huanjie->addSkill(new BsGongmou);
	mobilebs_huanjie->addSkill(new Zhengshuo);
	addMetaObject<ZhengshuoCard>();

}
ADD_PACKAGE(mobileBsJie)
