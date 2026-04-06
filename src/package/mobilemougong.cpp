#include "mobilemougong.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
//#include "util.h"
//#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
//#include "json.h"

MobileMouZhihengCard::MobileMouZhihengCard()
{
	target_fixed = true;
	will_throw = true;
	mute = true;
}

void MobileMouZhihengCard::onUse(Room *room, CardUseStruct &card_use) const
{
	if (card_use.from->hasSkill("jilve",true))
		room->broadcastSkillInvoke("jilve", 4);
	else
		room->broadcastSkillInvoke("mobilemouzhiheng");

	bool allhand = !card_use.from->isKongcheng();
	if (allhand) {
		foreach(int id, card_use.from->handCards()) {
			if (!subcards.contains(id)) {
				allhand = false;
				break;
			}
		}
	}
	if (allhand)
		room->setCardFlag(this, "mobilemouzhiheng_all_handcard_" + card_use.from->objectName());
	SkillCard::onUse(room, card_use);
}

void MobileMouZhihengCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	int x = subcardsLength();
	bool all = hasFlag("mobilemouzhiheng_all_handcard_" + source->objectName());
	if (all)
		x = x + source->getMark("&mobilemouye") + 1;
	source->drawCards(x, "mobilemouzhiheng");
	if (all)
		source->loseMark("&mobilemouye");
}

class MobileMouZhiheng : public ViewAsSkill
{
public:
	MobileMouZhiheng() : ViewAsSkill("mobilemouzhiheng")
	{
	}

	bool viewFilter(const QList<const Card *> &, const Card *to_select) const
	{
		return !Self->isJilei(to_select);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;

		MobileMouZhihengCard *zhiheng_card = new MobileMouZhihengCard;
		zhiheng_card->addSubcards(cards);
		return zhiheng_card;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->canDiscard(player, "he") && !player->hasUsed("MobileMouZhihengCard");
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern == "@mobilemouzhiheng";
	}
};

class MobileMouTongye : public PhaseChangeSkill
{
public:
	MobileMouTongye() : PhaseChangeSkill("mobilemoutongye")
	{
		frequency = Compulsory;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Finish) return false;
		room->sendCompulsoryTriggerLog(player, this);
		QString choice = room->askForChoice(player, objectName(), "gaibian+bubian");

		LogMessage log;
		log.type = "#FumianFirstChoice";
		log.from = player;
		log.arg = objectName() + ":" + choice;
		room->sendLog(log);

		int equip = 0;
		foreach (ServerPlayer *p, room->getAlivePlayers())
			equip += p->getEquips().length();
		room->setTag("MobileMouTongyeEquipNum", equip);

		int phase = (int)Player::Start;
		room->addPlayerMark(player, "&mobilemoutongye" + choice + "-Self" + QString::number(phase) + "Clear");
		return false;
	}
};

class MobileMouTongyeEquip : public PhaseChangeSkill
{
public:
	MobileMouTongyeEquip() : PhaseChangeSkill("#mobilemoutongye")
	{
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive() && target->getPhase() == Player::Start;
	}

	void sendLog(ServerPlayer *player, bool get) const
	{
		Room *room = player->getRoom();
		LogMessage log;
		log.type = "#ZhenguEffect";
		log.from = player;
		log.arg = "mobilemoutongye";
		room->sendLog(log);
		player->peiyin("mobilemoutongye");
		room->notifySkillInvoked(player, "mobilemoutongye");
		if (get)
			player->gainMark("&mobilemouye");
		else
			player->loseMark("&mobilemouye");
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		int phase = (int)Player::Start;
		int record_equip = room->getTag("MobileMouTongyeEquipNum").toInt();
		int equip = 0;
		foreach (ServerPlayer *p, room->getAlivePlayers())
			equip += p->getEquips().length();

		if (player->getMark("&mobilemoutongyegaibian-Self" + QString::number(phase) + "Clear") > 0) {
			if (record_equip != equip) {
				if (player->getMark("&mobilemouye") < 2)
					sendLog(player, true);
			} else {
				if (player->getMark("&mobilemouye") > 0)
					sendLog(player, false);
			}

		}
		if (player->getMark("&mobilemoutongyebubian-Self" + QString::number(phase) + "Clear") > 0) {
			if (record_equip == equip) {
				if (player->getMark("&mobilemouye") < 2)
					sendLog(player, true);
			} else {
				if (player->getMark("&mobilemouye") > 0)
					sendLog(player, false);
			}

		}
		return false;
	}
};

/*class MobileMouTongyeEquip : public TriggerSkill
{
public:
	MobileMouTongyeEquip() : TriggerSkill("#mobilemoutongye")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		int phase = (int)Player::Start;
		QString pha = "-Self" + QString::number(phase) + "Clear";
		int zengjia = player->getMark("&mobilemoutongyezengjia" + pha);
		int jianshao = player->getMark("&mobilemoutongyejianshao" + pha);
		if (zengjia <= 0 && jianshao <= 0) return false;

		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from_places.contains(Player::PlaceEquip) || move.to_place == Player::PlaceEquip) {
			int _equip = room->getTag("MobileMouTongyeEquipNum").toInt();
			int equip = 0;
			foreach (ServerPlayer *p, room->getAlivePlayers())
				equip += p->getEquips().length();
			room->setTag("MobileMouTongyeEquipNum", equip);

			int ye = player->getMark("&mobilemouye");

			if (zengjia > 0) {
				if (equip > _equip && ye < 4) {
					room->sendCompulsoryTriggerLog(player, "mobilemoutongye", true, true);
					player->gainMark("&mobilemouye", (zengjia + ye < 4) ? zengjia : (4 - ye));
				} else if (equip < _equip && ye > 0) {
					room->sendCompulsoryTriggerLog(player, "mobilemoutongye", true, true);
					player->loseMark("&mobilemouye", zengjia);
				}
			}

			if (jianshao > 0) {
				if (equip < _equip && ye < 4) {
					room->sendCompulsoryTriggerLog(player, "mobilemoutongye", true, true);
					player->gainMark("&mobilemouye", (jianshao + ye < 4) ? jianshao : (4 - ye));
				} else if (equip > _equip && ye > 0) {
					room->sendCompulsoryTriggerLog(player, "mobilemoutongye", true, true);
					player->loseMark("&mobilemouye", jianshao);
				}
			}
		}
		return false;
	}
};*/

class MobileMouJiuyuan : public TriggerSkill
{
public:
	MobileMouJiuyuan() : TriggerSkill("mobilemoujiuyuan$")
	{
		events << CardUsed << PreHpRecover;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (player->getKingdom() != "wu" || !use.card->isKindOf("Peach")) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->isDead() || !p->hasLordSkill(this)) continue;
				if (p->isWeidi())
					room->sendCompulsoryTriggerLog(p, "weidi", true, true);
				else
					room->sendCompulsoryTriggerLog(p, this);
				p->drawCards(1, objectName());
			}
		} else {
			if (!player->hasLordSkill(this)) return false;
			RecoverStruct rec = data.value<RecoverStruct>();
			if (rec.card && rec.card->isKindOf("Peach") && rec.who && rec.who != player && rec.who->getKingdom() == "wu") {
				QString skill = objectName();
				if (player->isWeidi())
					skill = "weidi";

				LogMessage log;
				log.type = "#JiuyuanExtraRecover";
				log.from = player;
				log.to << rec.who;
				log.arg = skill;
				room->sendLog(log);
				room->broadcastSkillInvoke(skill);
				room->notifySkillInvoked(player, skill);

				rec.recover++;
				data = QVariant::fromValue(rec);
			}
		}
		return false;
	}
};

MobileMouLeijiCard::MobileMouLeijiCard()
{
}

bool MobileMouLeijiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select!=Self;
}

void MobileMouLeijiCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	if(!effect.from->hasFlag("CurrentPlayer"))
		room->addPlayerMark(effect.from,"ban_daobing");
	effect.from->loseMark("&mou_daobing",4);
	room->damage(DamageStruct(getSkillName(), effect.from, effect.to, 1,DamageStruct::Thunder));
}

class MobileMouLeiji : public ZeroCardViewAsSkill
{
public:
	MobileMouLeiji() : ZeroCardViewAsSkill("mobilemouleiji")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("&mou_daobing")>=4;
	}

	const Card *viewAs() const
	{
		return new MobileMouLeijiCard;
	}
};

class MobileMouGuidao : public TriggerSkill
{
public:
	MobileMouGuidao() : TriggerSkill("mobilemouguidao")
	{
		events << GameStart << Damaged << DamageInflicted << EventPhaseChanging;
	}
	bool triggerable(const ServerPlayer *player) const
	{
		return player && player->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == GameStart) {
			if (player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				player->gainMark("&mou_daobing",2);
			}
		} else if (triggerEvent == Damaged) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.nature != DamageStruct::Normal){
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					if(p->hasSkill(objectName())&&p->getMark("&mou_daobing")<8&&p->getMark("ban_daobing")<1){
						room->sendCompulsoryTriggerLog(p,this);
						p->gainMark("&mou_daobing");
					}
				}
			}
		} else if (triggerEvent == DamageInflicted) {
			DamageStruct damage = data.value<DamageStruct>();
			if (player->getMark("&mou_daobing")>=2&&player->hasSkill(objectName())&&player->askForSkillInvoke(this,data)){
				player->peiyin(this);
				player->loseMark("&mou_daobing",2);
				if(!player->hasFlag("CurrentPlayer"))
					room->addPlayerMark(player,"ban_daobing");
				return true;
			}
		}else if (triggerEvent == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.from == Player::NotActive)
				room->setPlayerMark(player,"ban_daobing",0);
		}
		return false;
	}
};

class MobileMouHuangtian : public TriggerSkill
{
public:
	MobileMouHuangtian() : TriggerSkill("mobilemouhuangtian$")
	{
		events << EventPhaseStart << Damage;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart) {
			if (player->getPhase()==Player::RoundStart&&room->getTag("TurnLengthCount").toInt()==1&&player->hasLordSkill(this)){
				room->sendCompulsoryTriggerLog(player,this);
				player->getDerivativeCard("_taipingyaoshu");
			}
		} else if (triggerEvent == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (player->getKingdom() == "qun"){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(p->hasLordSkill(this)&&p->getMark("&mou_daobing")<8&&p->getMark("ban_daobing_lun")<4){
						room->sendCompulsoryTriggerLog(p,this);
						int n = qMin(2,8-p->getMark("&mou_daobing"));
						p->gainMark("&mou_daobing",n);
						room->addPlayerMark(player,"ban_daobing_lun",n);
					}
				}
			}
		}
		return false;
	}
};

class MobileMouZishou : public TriggerSkill
{
public:
	MobileMouZishou() : TriggerSkill("mobilemouzishou")
	{
		events << EventPhaseStart << DamageDone;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart) {
			if (player->getPhase()==Player::Finish){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(p->hasSkill(objectName())){
						bool has = player->getCardCount()<1;
						foreach (ServerPlayer *q, room->getOtherPlayers(player,true)) {
							if(q==p||has) continue;
							if(p->getMark(q->objectName()+"mobilemouzishouDamage-Clear")>0)
								has = true;
							if(player->getMark(q->objectName()+"mobilemouzishouDamage-Clear")>0)
								has = true;
						}
						if(has) continue;
						room->sendCompulsoryTriggerLog(p,this);
						const Card*dc = room->askForExchange(player,objectName(),1,1,true,"mobilemouzishou0:"+p->objectName());
						if(dc) room->giveCard(player,p,dc,objectName());
					}
				}
			}
		} else if (triggerEvent == DamageDone) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from) damage.from->addMark(damage.to->objectName()+"mobilemouzishouDamage-Clear");
		}
		return false;
	}
};

class MobileMouZongshi : public TriggerSkill
{
public:
	MobileMouZongshi() : TriggerSkill("mobilemouzongshi")
	{
		events << Damaged;
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == Damaged) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from&&damage.from->getMark("mobilemouzongshiUse")<1){
				damage.from->addMark("mobilemouzongshiUse");
				room->sendCompulsoryTriggerLog(player,this);
				damage.from->throwAllHandCards(objectName());
			}
		}
		return false;
	}
};

class MobileMouWansha : public TriggerSkill
{
public:
	MobileMouWansha() : TriggerSkill("mobilemouwansha")
	{
		events << Dying;
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == Dying) {
			if(player->hasFlag("CurrentPlayer"))
				room->sendCompulsoryTriggerLog(player,this);
			DyingStruct dy = data.value<DyingStruct>();
			if (player->getMark("mobilemouwanshaUse_lun")<1&&player->askForSkillInvoke(objectName()+"$-1",dy.who)){
				player->addMark("mobilemouwanshaUse_lun");
				room->doGongxin(player,dy.who,QList<int>(),objectName());
				QList<int>ids;
				bool has = player->getMark("mobilemouwanshaUp")>0;
				for (int i = 0; i < 2; i++) {
					if(ids.length()>=dy.who->getCardCount(has,has)) break;
					int id = room->askForCardChosen(player,dy.who,has?"hej":"h",objectName(),true,Card::MethodNone,ids,true);
					if(id<0) break;
					ids << id;
				}
				if(ids.length()>0){
					if(room->askForChoice(dy.who,objectName(),"mobilemouwansha1+mobilemouwansha2",QVariant::fromValue(player))=="mobilemouwansha1"){
						player->assignmentCards(ids,objectName(),room->getOtherPlayers(dy.who),ids.length(),ids.length());
					}else{
						Card*dc = new DummyCard();
						foreach (const Card *c, dy.who->getCards(has?"hej":"h")) {
							if(ids.contains(c->getId())||!dy.who->canDiscard(dy.who,c->getId())) continue;
							dc->addSubcard(c);
						}
						if(dc->subcardsLength()>0)
							room->throwCard(dc,objectName(),dy.who);
						dc->deleteLater();
					}
				}
			}
		}
		return false;
	}
};

class MobileMouLuanwu : public ZeroCardViewAsSkill
{
public:
	MobileMouLuanwu() : ZeroCardViewAsSkill("mobilemouluanwu")
	{
		frequency = Limited;
		limit_mark = "@chaos";
	}

	const Card *viewAs() const
	{
		return new MobileMouLuanwuCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@chaos") >= 1;
	}
};

MobileMouLuanwuCard::MobileMouLuanwuCard()
{
	target_fixed = true;
}

void MobileMouLuanwuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	LuanwuCard::use(room,source,targets);
	QStringList choices;
	if(source->hasSkill("mobilemouwansha",true))
		choices << "mobilemouwansha1";
	if(source->hasSkill("mobilemouweimu",true))
		choices << "mobilemouweimu1";
	QString choice = room->askForChoice(source,"mobilemouluanwu",choices.join("+"));
	choice.remove("1");
	room->addPlayerMark(source,choice+"Up");
	room->changeTranslation(source,choice,1);
}

class MobileMouWeimu : public TriggerSkill
{
public:
	MobileMouWeimu() : TriggerSkill("mobilemouweimu")
	{
		events << TargetConfirming << RoundStart;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == TargetConfirming) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("TrickCard")&&use.card->isBlack()&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				use.to.removeOne(player);
				data.setValue(use);
			}else if(use.card->getTypeId()>0&&use.from!=player)
				player->addMark("mobilemouweimuUseTo");
		}else{
			if(player->getMark("mobilemouweimuUp")>0
			&&player->hasSkill(objectName())&&player->getMark("mobilemouweimuUseTo")<2){
				room->sendCompulsoryTriggerLog(player,this);
				QList<int>ids = room->getDiscardPile();
				qShuffle(ids);
				foreach (int id, ids) {
					const Card*c = Sanguosha->getCard(id);
					if(c->isKindOf("Armor")||(c->isKindOf("TrickCard")&&c->isBlack())){
						player->obtainCard(c);
						break;
					}
				}
			}
			player->setMark("mobilemouweimuUseTo",0);
		}
		return false;
	}
};

MobileMouQuhuCard::MobileMouQuhuCard()
{
	will_throw = false;
}

bool MobileMouQuhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length()<2&&to_select!=Self&&to_select->getCardCount()>0;
}

bool MobileMouQuhuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length()==2;
}

void MobileMouQuhuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	QHash<ServerPlayer *,QList<int> > p2ids;
	foreach (ServerPlayer *p, targets) {
		const Card*dc = room->askForExchange(p,"mobilemouquhu",p->getCardCount(),1,true,"mobilemouquhu0");
		if(dc) p2ids[p] = dc->getSubcards();
	}
	source->addToPile("mobilemouquhu",subcards,false);
	bool has = true;
	foreach (ServerPlayer *p, targets) {
		p->addToPile("mobilemouquhu",p2ids[p],false);
		if(p2ids[p].length()<=subcardsLength())
			has = false;
	}
	ServerPlayer *tp = nullptr;
	foreach (ServerPlayer *p, targets) {
		if(!tp||p2ids[p].length()>p2ids[tp].length())
			tp = p;
	}
	if(has){
		if(tp->isAlive())
			tp->obtainCard(this,false);
		foreach (ServerPlayer *p, targets) {
			if(p->isAlive()){
				Card*dc = new DummyCard(p2ids[p]);
				room->obtainCard(p,dc,false);
				dc->deleteLater();
			}
		}
	}else{
		foreach (ServerPlayer *p, targets) {
			if(p!=tp)
				room->damage(DamageStruct("mobilemouquhu",tp,p));
		}
		if(tp->isAlive())
			tp->obtainCard(this,false);
		foreach (ServerPlayer *p, targets) {
			if(p->isAlive())
				room->throwCard(p2ids[p],"mobilemouquhu",p);
		}
	}
}

class MobileMouQuhu : public ViewAsSkill
{
public:
	MobileMouQuhu() : ViewAsSkill("mobilemouquhu")
	{
	}

	bool viewFilter(const QList<const Card *> &, const Card *) const
	{
		return true;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty()) return nullptr;
		Card *sc = new MobileMouQuhuCard;
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getCardCount()>0 && !player->hasUsed("MobileMouQuhuCard");
	}
};

class MobileMouJieming : public TriggerSkill
{
public:
	MobileMouJieming() : TriggerSkill("mobilemoujieming")
	{
		events << Damaged;
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (triggerEvent == Damaged) {
			ServerPlayer *tp = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"mobilemoujieming0",true,true);
			if (tp){
				player->peiyin(this);
				tp->drawCards(4,objectName());
				int n = qMax(1,player->getLostHp());
				tp->setTag("mobilemoujieming_target", QVariant::fromValue(player));
				const Card*dc = room->askForDiscard(tp,objectName(),tp->getCardCount(),1,true,true,"mobilemoujieming1:"+player->objectName()+":"+QString::number(n));
				tp->removeTag("mobilemoujieming_target");
				if(dc&&dc->subcardsLength()>=n) return false;
				room->loseHp(player,1,true,player,objectName());
			}
		}
		return false;
	}
};


MobileMouZhiPackage::MobileMouZhiPackage()
	: Package("mobilemouzhi")
{
	General *mobilemou_sunquan = new General(this, "mobilemou_sunquan$", "wu", 4);
	mobilemou_sunquan->addSkill(new MobileMouZhiheng);
	mobilemou_sunquan->addSkill(new MobileMouTongye);
	mobilemou_sunquan->addSkill(new MobileMouTongyeEquip);
	mobilemou_sunquan->addSkill(new MobileMouJiuyuan);
	related_skills.insertMulti("mobilemoutongye", "#mobilemoutongye");

	addMetaObject<MobileMouZhihengCard>();

	General *mobilemou_zhangjiao = new General(this, "mobilemou_zhangjiao$", "qun", 3);
	mobilemou_zhangjiao->addSkill(new MobileMouLeiji);
	mobilemou_zhangjiao->addSkill(new MobileMouGuidao);
	mobilemou_zhangjiao->addSkill(new MobileMouHuangtian);

	addMetaObject<MobileMouLeijiCard>();

	General *mobilemou_liubiao = new General(this, "mobilemou_liubiao", "qun", 3);
	mobilemou_liubiao->addSkill(new MobileMouZishou);
	mobilemou_liubiao->addSkill(new MobileMouZongshi);

	General *mobilemou_jiaxu = new General(this, "mobilemou_jiaxu", "qun", 3);
	mobilemou_jiaxu->addSkill(new MobileMouWansha);
	mobilemou_jiaxu->addSkill(new MobileMouLuanwu);
	mobilemou_jiaxu->addSkill(new MobileMouWeimu);
	addMetaObject<MobileMouLuanwuCard>();
	
	General *mobilemou_xunyu = new General(this, "mobilemou_xunyu", "wei", 3);
	mobilemou_xunyu->addSkill(new MobileMouQuhu);
	mobilemou_xunyu->addSkill(new MobileMouJieming);
	addMetaObject<MobileMouQuhuCard>();

}
ADD_PACKAGE(MobileMouZhi)


MobileMouDuanliangCard::MobileMouDuanliangCard()
{
}

void MobileMouDuanliangCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();

	QString str = "=" + to->objectName();
	QString choice1 = room->askForChoice(from, "mobilemouduanliang", "weicheng" + str + "+leigu" + str, QVariant::fromValue(to));
	str = "=" + from->objectName();
	QString choice2 = room->askForChoice(to, "mobilemouduanliang", "weicheng2" + str + "+leigu2" + str, QVariant::fromValue(from));

	choice1 = choice1.split("=").first();
	choice2 = choice2.split("=").first();
	if (choice2.startsWith(choice1)){
		from->peiyin("mobilemouduanliang",4);
		return;
	}
	from->peiyin("mobilemouduanliang",3);

	if (choice1 == "weicheng") {
		if (to->containsTrick("supply_shortage")) {
			if (to->isNude()) return;
			int card_id = room->askForCardChosen(from, to, "he", "mobilemouduanliang");
			room->obtainCard(from, card_id, false);
		} else {
			SupplyShortage *su = new SupplyShortage(Card::NoSuit, 0);
			su->setSkillName("_mobilemouduanliang");
			su->deleteLater();
			if (from->isProhibited(to,su)) return;
			su->addSubcard(room->drawCard());
			room->useCard(CardUseStruct(su, from, to), true);
		}
	} else {
		Duel *duel = new Duel(Card::NoSuit, 0);
		duel->setSkillName("_mobilemouduanliang");
		duel->deleteLater();
		if (from->canUse(duel, to, true))
			room->useCard(CardUseStruct(duel, from, to), true);
	}
}

class MobileMouDuanliang : public ZeroCardViewAsSkill
{
public:
	MobileMouDuanliang() : ZeroCardViewAsSkill("mobilemouduanliang")
	{
	}

	const Card *viewAs() const
	{
		return new MobileMouDuanliangCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("MobileMouDuanliangCard") < 2;
	}
	int getEffectIndex(const ServerPlayer *, const Card *) const
	{
		return qrand()%2+1;
	}
};

MobileMouShipoCard::MobileMouShipoCard()
{
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void MobileMouShipoCard::onUse(Room *room, CardUseStruct &use) const
{
	ServerPlayer *player = use.from, *to = use.to.first();
	room->setPlayerProperty(player, "mobilemoushipo_card_ids", "");
	room->giveCard(player, to, this, "mobilemoushipo");
}

class MobileMouShipoVS : public ViewAsSkill
{
public:
	MobileMouShipoVS() : ViewAsSkill("mobilemoushipo")
	{
		response_pattern = "@@mobilemoushipo";
	}

	bool viewFilter(const QList<const Card *> &, const Card *to_select) const
	{
		QStringList l = Self->property("mobilemoushipo_card_ids").toString().split("+");
		QList<int> li = ListS2I(l);
		return li.contains(to_select->getId());
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty()) return nullptr;
		MobileMouShipoCard *c = new MobileMouShipoCard;
		c->addSubcards(cards);
		return c;
	}
};

class MobileMouShipo : public PhaseChangeSkill
{
public:
	MobileMouShipo() : PhaseChangeSkill("mobilemoushipo")
	{
		view_as_skill = new MobileMouShipoVS;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Finish) return false;

		QStringList choices;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->getHp() < player->getHp() && !choices.contains("hp"))
				choices << "hp";
			if (p->containsTrick("supply_shortage") && !choices.contains("judge"))
				choices << "judge";
			if (choices.length() == 2)
				break;
		}
		if (choices.isEmpty()) return false;
		choices << "cancel";

		QString choice = room->askForChoice(player, objectName(), choices.join("+"));
		if (choice == "cancel") return false;

		LogMessage log;
		log.type = "#MobileMouShipoInvoke";
		log.from = player;
		log.arg = objectName();

		QList<ServerPlayer *> choose;
		if (choice == "hp") {
			QList<ServerPlayer *> targets;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getHp() < player->getHp())
					targets << p;
			}
			if (targets.isEmpty()) return false;
			ServerPlayer *t = room->askForPlayerChosen(player, targets, objectName(), "@mobilemoushipo-target");
			choose << t;
		} else {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->containsTrick("supply_shortage"))
					choose << p;
			}
			if (choose.isEmpty()) return false;
		}

		foreach (ServerPlayer *p, choose)
			room->doAnimate(1, player->objectName(), p->objectName());

		log.to = choose;
		room->sendLog(log);
		room->broadcastSkillInvoke(this);
		room->notifySkillInvoked(player, objectName());

		QList<int> cards;
		foreach (ServerPlayer *p, choose) {
			if (p->isDead()) continue;
			if (player->isDead()) {
				room->damage(DamageStruct(objectName(), nullptr, p));
				continue;
			}
			const Card *c = room->askForExchange(p, objectName(), 1, 1, false, "@mobilemoushipo-give:" + player->objectName(), true);
			if (c) {
				cards << c->getSubcards();
				room->giveCard(p, player, c, "mobilemoushipo");
			} else
				room->damage(DamageStruct(objectName(), nullptr, p));
		}

		if (player->isDead()) return false;

		QList<int> ids;
		foreach (int id, cards) {
			if (!player->hasCard(id)) continue;
			ids << id;
		}
		if (ids.isEmpty()) return false;

		room->setPlayerProperty(player, "mobilemoushipo_card_ids", ListI2S(ids).join("+"));
		room->askForUseCard(player, "@@mobilemoushipo", "@mobilemoushipo", -1, Card::MethodNone);
		room->setPlayerProperty(player, "mobilemoushipo_card_ids", "");
		return false;
	}
};

class MobileMouTieqi : public TriggerSkill
{
public:
	MobileMouTieqi() : TriggerSkill("mobilemoutieqi")
	{
		events << TargetSpecifying;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash")) return false;
		QList<ServerPlayer *> tos;
		foreach (ServerPlayer *p, use.to) {
			if (!player->isAlive()) break;
			if (p->isDead()) continue;
			if (player->askForSkillInvoke(this, p)) {
				room->broadcastSkillInvoke(objectName());
				if (!tos.contains(p)) {
					p->addMark("mobilemoutieqi");
					room->addPlayerMark(p, "@skill_invalidity");
					tos << p;

					foreach(ServerPlayer *pl, room->getAllPlayers())
						room->filterCards(pl, pl->getCards("he"), true);
					JsonArray args;
					args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
					room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
				}

				LogMessage log;
				log.type = "#NoJink";
				log.from = p;
				room->sendLog(log);

				use.no_respond_list << p->objectName();
				data = QVariant::fromValue(use);

				if (player->isDead() || p->isDead()) continue;

				QString str = "=" + p->objectName();
				QString choice1 = room->askForChoice(player, objectName(), "zhiqu" + str + "+raozhen" + str, QVariant::fromValue(p));
				if (p->isDead()) continue;
				str = "=" + player->objectName();
				QString choice2 = room->askForChoice(p, objectName(), "zhiqu2" + str + "+raozhen2" + str, QVariant::fromValue(player));

				choice1 = choice1.split("=").first();
				choice2 = choice2.split("=").first();
				if (choice2.startsWith(choice1) || p->isDead() || player->isDead()) continue;

				if (choice1 == "zhiqu") {
					if (p->isNude()) continue;
					int id = room->askForCardChosen(player, p, "he", objectName());
					CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
					room->obtainCard(player, Sanguosha->getCard(id), reason, room->getCardPlace(id) != Player::PlaceHand);
				} else
					player->drawCards(2, objectName());
			}
		}
		return false;
	}
};

class MobileMouTieqiClear : public TriggerSkill
{
public:
	MobileMouTieqiClear() : TriggerSkill("#mobilemoutieqi")
	{
		events << EventPhaseChanging << Death;
	}

	int getPriority(TriggerEvent) const
	{
		return 5;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data) const
	{
		if (triggerEvent == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive)
				return false;
		} else if (triggerEvent == Death) {
			DeathStruct death = data.value<DeathStruct>();
			if (death.who != target || target != room->getCurrent())
				return false;
		}
		QList<ServerPlayer *> players = room->getAllPlayers(true);
		foreach (ServerPlayer *player, players) {
			if (player->getMark("mobilemoutieqi") == 0) continue;
			room->removePlayerMark(player, "@skill_invalidity", player->getMark("mobilemoutieqi"));
			player->setMark("mobilemoutieqi", 0);

			foreach(ServerPlayer *p, room->getAllPlayers())
				room->filterCards(p, p->getCards("he"), false);
			JsonArray args;
			args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
			room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
		}
		return false;
	}
};

MobileMouXingshangCard::MobileMouXingshangCard()
{
}

bool MobileMouXingshangCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.isEmpty();
}

void MobileMouXingshangCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	QStringList choices,tos;
	foreach (ServerPlayer *p, room->getPlayers()) {
		if(p->isDead()){
			if(!p->property("ZhuisiPlayer").toBool())
				tos << "ZhuisiPlayer="+p->objectName();
		}
	}
	if(effect.from->getMark("&mobilemouxingshang_song")>1)
		choices << "mobilemouxingshang0=2";
	if(effect.to->getMaxHp()<=9&&effect.from->getMark("&mobilemouxingshang_song")>2)
		choices << "mobilemouxingshang0=3";
	if(effect.from->getMark("&mobilemouxingshang_song")>3&&tos.length()>0
	&&effect.from==effect.to&&effect.from->hasSkill("mobilemouxingshang",true))
		choices << "mobilemouxingshang0=4";
	QString choice = room->askForChoice(effect.from,"mobilemouxingshang",choices.join("+"),QVariant::fromValue(effect.to));
	if(choice.contains("=2")){
		effect.from->loseMark("&mobilemouxingshang_song",2);
		effect.to->drawCards(3,"mobilemouxingshang");
		if(!effect.to->faceUp()) effect.to->turnOver();
		if(effect.to->isChained()) room->setPlayerChained(effect.to);
	}else if(choice.contains("=3")){
		effect.from->loseMark("&mobilemouxingshang_song",3);
		room->recover(effect.to,RecoverStruct("mobilemouxingshang",effect.from));
		room->gainMaxHp(effect.to,1,"mobilemouxingshang");
		QList<int>ids;
		for (int i = 0; i < 4; i++) {
			if(!effect.to->hasEquipArea(i))
				ids << i;
		}
		if(ids.length()>0){
			qShuffle(ids);
			effect.to->obtainEquipArea(ids.first());
		}
	}else{
		effect.from->loseMark("&mobilemouxingshang_song",4);
		choice = room->askForChoice(effect.from,"mobilemouxingshangZhuisi",tos.join("+"));
		ServerPlayer *t = room->findChild<ServerPlayer *>(choice.split("=").last());
		room->setPlayerProperty(t,"ZhuisiPlayer",true);
		QStringList sks;
		foreach (const Skill *s, t->getGeneral()->getVisibleSkillList())
			sks << s->objectName();
		if(t->getGeneral2()){
			foreach (const Skill *s, t->getGeneral2()->getVisibleSkillList())
				sks << s->objectName();
		}
		sks << "-mobilemouxingshang";
		sks << "-mobilemoufangzhu";
		sks << "-mobilemousongwei";
		room->handleAcquireDetachSkills(effect.to,sks);
	}
}

class MobileMouXingshangvs : public ZeroCardViewAsSkill
{
public:
	MobileMouXingshangvs() : ZeroCardViewAsSkill("mobilemouxingshang")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("MobileMouXingshangCard")<2
			&&player->getMark("&mobilemouxingshang_song")>1;
	}

	const Card *viewAs() const
	{
		return new MobileMouXingshangCard;
	}
};

class MobileMouXingshang : public TriggerSkill
{
public:
	MobileMouXingshang() : TriggerSkill("mobilemouxingshang")
	{
		events << Damaged << Death;
		view_as_skill = new MobileMouXingshangvs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (triggerEvent == Damaged) {
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->getMark("mobilemouxingshangUse-Clear")>0||!player->hasTurn()
				||!p->hasSkill(objectName())||p->getMark("&mobilemouxingshang_song")>8) continue;
				p->addMark("mobilemouxingshangUse-Clear");
				room->sendCompulsoryTriggerLog(p, objectName());
				p->gainMark("&mobilemouxingshang_song", qMin(2,9-p->getMark("&mobilemouxingshang_song")));
			}
		} else if (triggerEvent == Death) {
			if (!player->hasSkill(objectName())||player->getMark("&mobilemouxingshang_song")>8)
				return false;
			room->sendCompulsoryTriggerLog(player, objectName());
			player->gainMark("&mobilemouxingshang_song", qMin(2,9-player->getMark("&mobilemouxingshang_song")));
		}
		return false;
	}
};

MobileMouFangzhuCard::MobileMouFangzhuCard()
{
}

bool MobileMouFangzhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select!=Self;
}

void MobileMouFangzhuCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	QStringList choices;
	if(effect.from->getMark("&mobilemouxingshang_song")>1)
		choices << "mobilemouxingshang0=2";
	if(effect.from->getMark("&mobilemouxingshang_song")>3)
		choices << "mobilemouxingshang0=4";
	if(effect.from->getMark("&mobilemouxingshang_song")>5)
		choices << "mobilemouxingshang0=6";
	if(effect.from->getMark("&mobilemouxingshang_song")>7)
		choices << "mobilemouxingshang0=8";
	QString choice = room->askForChoice(effect.from,"mobilemoufangzhu",choices.join("+"),QVariant::fromValue(effect.to));
	if(choice.contains("=2")){
		effect.from->loseMark("&mobilemouxingshang_song",2);
		room->setPlayerMark(effect.to,"&mobilemoufangzhu+2-SelfClear",1);
	}else if(choice.contains("=4")){
		effect.from->loseMark("&mobilemouxingshang_song",4);
		ServerPlayer *tp = room->askForPlayerChosen(effect.from,room->getOtherPlayers(effect.to),"mobilemoufangzhu","mobilemoufangzhu4:"+effect.to->objectName());
		if(tp) effect.to->addMark(tp->objectName()+"mobilemoufangzhu4-SelfClear");
		room->setPlayerMark(effect.to,"&mobilemoufangzhu+4-SelfClear",1);
    }else if(choice.contains("=6")){
		effect.from->loseMark("&mobilemouxingshang_song",6);
		room->setPlayerMark(effect.to,"&mobilemoufangzhu+6-SelfClear",1);
	}else{
		effect.from->loseMark("&mobilemouxingshang_song",8);
		effect.to->turnOver();
		room->setPlayerMark(effect.to,"&mobilemoufangzhu+8-SelfClear",1);
	}
}

class MobileMouFangzhuvs : public ZeroCardViewAsSkill
{
public:
	MobileMouFangzhuvs() : ZeroCardViewAsSkill("mobilemoufangzhu")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("MobileMouFangzhuCard")<1
		&&player->hasSkill("mobilemouxingshang",true)
		&&player->getMark("&mobilemouxingshang_song")>1;
	}

	const Card *viewAs() const
	{
		return new MobileMouFangzhuCard;
	}
};

class MobileMouFangzhu : public TriggerSkill
{
public:
	MobileMouFangzhu() : TriggerSkill("mobilemoufangzhu")
	{
		events << CardUsed;
		view_as_skill = new MobileMouFangzhuvs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if (p->getMark("&mobilemoufangzhu+4-SelfClear")<1
					||p->getMark(player->objectName()+"mobilemoufangzhu4-SelfClear")) continue;
					use.no_respond_list << p->objectName();
					data.setValue(use);
				}
			}
		}
		return false;
	}
};

MobileMouSongweiCard::MobileMouSongweiCard()
{
}

bool MobileMouSongweiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select!=Self&&to_select->getKingdom()=="wei";
}

void MobileMouSongweiCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	room->addPlayerMark(effect.from,"mobilemousongweiUse");
	QStringList sks;
	foreach (const Skill *s, effect.to->getVisibleSkillList()) {
		if(!s->isAttachedLordSkill())
			sks << "-"+s->objectName();
	}
	room->handleAcquireDetachSkills(effect.to,sks);
}

class MobileMouSongweivs : public ZeroCardViewAsSkill
{
public:
	MobileMouSongweivs() : ZeroCardViewAsSkill("mobilemousongwei")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("mobilemousongweiUse")<1&&player->hasLordSkill("mobilemousongwei");
	}

	const Card *viewAs() const
	{
		return new MobileMouSongweiCard;
	}
};

class MobileMouSongwei : public TriggerSkill
{
public:
	MobileMouSongwei() : TriggerSkill("mobilemousongwei$")
	{
		events << EventPhaseStart;
		//view_as_skill = new MobileMouSongweivs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->hasLordSkill(this);
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (triggerEvent == EventPhaseStart) {
			if(player->getPhase()==Player::Play&&player->hasSkill("mobilemouxingshang",true)&&player->getMark("&mobilemouxingshang_song")<9){
				int n = 0;
				foreach (ServerPlayer *p, room->getOtherPlayers(player,true)) {
					if (p->getKingdom()=="wei") n += 2;
				}
				if(n>0){
					room->sendCompulsoryTriggerLog(player,this);
					player->gainMark("&mobilemouxingshang_song",qMin(n,9-player->getMark("&mobilemouxingshang_song")));
				}
			}
		}
		return false;
	}
};

class MobileMouQiaobian : public TriggerSkill
{
public:
	MobileMouQiaobian() : TriggerSkill("mobilemouqiaobian")
	{
		events << EventPhaseChanging << EventPhaseStart;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to>Player::Start&&change.to<Player::Discard&&!player->isSkipped(change.to)){
				if(player->getMark("mobilemouqiaobianUse-Clear")<1
				&&player->askForSkillInvoke(objectName()+"$-1",QString("b%1").arg(change.to))){
					player->skip(change.to);
					player->addMark("mobilemouqiaobianUse-Clear");
					if(change.to==Player::Judge){
						room->loseHp(player,1,true,player,objectName());
						QList<const Card*>js = player->getJudgingArea();
						if(js.isEmpty()) return false;
						ServerPlayer *tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"mobilemouqiaobian1");
						if(tp){
							foreach (const Card*c, js) {
								if(player->isProhibited(tp,c)){
									room->throwCard(c,objectName(),nullptr);
								}else{
									room->moveCardTo(c,tp,Player::PlaceDelayedTrick,true);
								}
							}
						}
					}else if(change.to==Player::Draw){
						room->setPlayerMark(player,"&mobilemouqiaobian",1);
					}else if(change.to==Player::Play){
						int n = player->getHandcardNum()-6;
						if(n>0)
							room->askForDiscard(player,objectName(),n,n);
						room->moveField(player,objectName(),true);
						player->skip(Player::Discard);
					}
				}
			}
		}else if(player->getPhase()==Player::Start&&player->getMark("&mobilemouqiaobian")>0){
			room->setPlayerMark(player,"&mobilemouqiaobian",0);
			room->sendCompulsoryTriggerLog(player,objectName());
			player->drawCards(5,objectName());
			room->recover(player,RecoverStruct(objectName(),player));
		}
		return false;
	}
};

class MobileMouGongqi : public TriggerSkill
{
public:
	MobileMouGongqi() : TriggerSkill("mobilemougongqi")
	{
		events << EventPhaseStart << CardUsed << CardFinished;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == EventPhaseStart) {
			if(player->getPhase()==Player::Play&&player->hasSkill(objectName())&&player->canDiscard(player,"he")){
				const Card*dc = room->askForCard(player,"..","mobilemougongqi0",data,objectName());
				if(dc){
					player->peiyin(this);
					room->setPlayerMark(player,"mobilemougongqiUse-PlayClear",1);
					room->setPlayerMark(player,"&mobilemougongqi+"+dc->getColorString()+"-PlayClear",1);
				}
			}
		}else if(triggerEvent == CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getMark("mobilemougongqiUse-PlayClear")>0){
				room->setPlayerMark(player,"mobilemougongqiUseCard",1);
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getMark("mobilemougongqiUse-PlayClear")>0){
				room->setPlayerMark(player,"mobilemougongqiUseCard",0);
			}
		}
		return false;
	}
};

class MobileMouGongqiBf : public AttackRangeSkill
{
public:
	MobileMouGongqiBf() : AttackRangeSkill("#MobileMouGongqiBf")
	{
		frequency = NotFrequent;
	}

	int getExtra(const Player *target, bool) const
	{
		if(target->hasSkill("mobilemougongqi"))
			return 4;
		return 0;
	}
};

class MobileMouGongqiLimit : public CardLimitSkill
{
public:
	MobileMouGongqiLimit() : CardLimitSkill("#MobileMouGongqiLimit")
	{
		frequency = NotFrequent;
	}

	QString limitList(const Player *) const
	{
		return "use,response";
	}

	QString limitPattern(const Player *target,const Card*card) const
	{
		if(target->handCards().contains(card->getEffectiveId())){
			foreach (const Player *p, target->getAliveSiblings()) {
				if(p->getMark("mobilemougongqiUse-PlayClear")>0&&p->getMark("mobilemougongqiUseCard")>0
				&&p->getMark("&mobilemougongqi+"+card->getColorString()+"-PlayClear")<1){
					return card->toString();
				}
			}
		}
		return "";
	}
};

MobileMouJiefanCard::MobileMouJiefanCard()
{
}

bool MobileMouJiefanCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.isEmpty();
}

void MobileMouJiefanCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	QList<ServerPlayer *>tps;
	foreach (ServerPlayer *p, room->getAllPlayers()) {
		if(p->inMyAttackRange(effect.to)) tps << p;
	}
	QString choice = "mobilemoujiefan1+mobilemoujiefan2="+QString::number(tps.length())+"+beishui";
	choice = room->askForChoice(effect.to,"mobilemoujiefan",choice,QVariant::fromValue(effect));
	if(!choice.contains("mobilemoujiefan2")){
		foreach (ServerPlayer *p, tps)
			room->askForDiscard(p,"mobilemoujiefan",1,1,false,true);
	}
	if(choice!="mobilemoujiefan1"){
		effect.to->drawCards(tps.length(),objectName());
	}
	if(choice=="beishui")
		room->setPlayerMark(effect.from,"&mobilemoujiefan+beishui",1);
}

class MobileMouJiefanvs : public ZeroCardViewAsSkill
{
public:
	MobileMouJiefanvs() : ZeroCardViewAsSkill("mobilemoujiefan")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("MobileMouJiefanCard")<1
		&&player->getMark("&mobilemoujiefan+beishui")<1;
	}

	const Card *viewAs() const
	{
		return new MobileMouJiefanCard;
	}
};

class MobileMouJiefan : public TriggerSkill
{
public:
	MobileMouJiefan() : TriggerSkill("mobilemoujiefan")
	{
		events << Death;
		view_as_skill = new MobileMouJiefanvs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == Death) {
			DeathStruct death = data.value<DeathStruct>();
			if(death.damage&&death.damage->from==player){
				room->setPlayerMark(player,"&mobilemoujiefan+beishui",0);
			}
		}
		return false;
	}
};



MobileMouShiPackage::MobileMouShiPackage()
	: Package("mobilemoushi")
{
	General *mobilemou_xuhuang = new General(this, "mobilemou_xuhuang", "wei", 4);
	mobilemou_xuhuang->addSkill(new MobileMouDuanliang);
	mobilemou_xuhuang->addSkill(new MobileMouShipo);

	General *mobilemou_machao = new General(this, "mobilemou_machao", "shu", 4);
	mobilemou_machao->addSkill(new MobileMouTieqi);
	mobilemou_machao->addSkill(new MobileMouTieqiClear);
	mobilemou_machao->addSkill("mashu");
	related_skills.insertMulti("mobilemoutieqi", "#mobilemoutieqi");

	addMetaObject<MobileMouDuanliangCard>();
	addMetaObject<MobileMouShipoCard>();
	
	General *mobilemou_caopi = new General(this, "mobilemou_caopi$", "wei", 3);
	mobilemou_caopi->addSkill(new MobileMouXingshang);
	mobilemou_caopi->addSkill(new MobileMouFangzhu);
	mobilemou_caopi->addSkill(new MobileMouSongwei);
	addMetaObject<MobileMouXingshangCard>();
	addMetaObject<MobileMouFangzhuCard>();
	addMetaObject<MobileMouSongweiCard>();

	General *mobilemou_zhanghe = new General(this, "mobilemou_zhanghe", "wei", 4);
	mobilemou_zhanghe->addSkill(new MobileMouQiaobian);

	General *mobilemou_handang = new General(this, "mobilemou_handang", "wu", 4);
	mobilemou_handang->addSkill(new MobileMouGongqi);
	mobilemou_handang->addSkill(new MobileMouGongqiBf);
	mobilemou_handang->addSkill(new MobileMouGongqiLimit);
	mobilemou_handang->addSkill(new MobileMouJiefan);
	addMetaObject<MobileMouJiefanCard>();

}
ADD_PACKAGE(MobileMouShi)


class MobileMouLiegong : public TriggerSkill
{
public:
	MobileMouLiegong() : TriggerSkill("mobilemouliegong")
	{
		events << TargetSpecified << CardUsed << TargetConfirmed;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash") || use.to.length() != 1) return false;
			ServerPlayer *to = use.to.first();
			if (!player->askForSkillInvoke(this, to)) return false;
			player->peiyin(this);

			room->setCardFlag(use.card, "mobilemouliegongUsed");
			room->setCardFlag(use.card, "mobilemouliegongUsed_" + player->objectName());

			QStringList records = player->getTag("MobileMouLiegongRecords").toStringList();

			int attack = records.length() - 1;
			if (attack <= 0) return false;

			QList<int> shows = room->showDrawPile(player, attack, objectName(), false);
			room->getThread()->delay(1000);
			int damage = 0;
			foreach (int id, shows) {
				if (records.contains(Sanguosha->getCard(id)->getSuitString()))
					damage++;
			}
			if (damage > 0)
				room->setCardFlag(use.card, "mobilemouliegongAddDamage_" + QString::number(damage));
			room->setPlayerProperty(to, "MobileMouLiegongTargetRecords", records.join(","));
		} else {
			const Card *card = nullptr;
			if (event == CardUsed) {
				CardUseStruct use = data.value<CardUseStruct>();
				card = use.card;
			} else if (event == TargetConfirmed) {
				CardUseStruct use = data.value<CardUseStruct>();
				if (use.from == player || !use.to.contains(player)) return false;
				card = use.card;
			}
			if (!card || card->isKindOf("SkillCard") || !card->hasSuit()) return false;
			
			QStringList records = player->getTag("MobileMouLiegongRecords").toStringList();
			QString suit = card->getSuitString();
			if (records.contains(suit)) return false;
			records << suit;
			player->setTag("MobileMouLiegongRecords", records);
			foreach (QString mark, player->getMarkNames()) {
				if (mark.startsWith("&mobilemouliegong+"))
					room->setPlayerMark(player, mark, 0);
			}
			suit = "&mobilemouliegong";
			foreach (QString s, records)
				suit = suit + "+" + s + "_char";
			room->setPlayerMark(player, suit, 1);
		}
		return false;
	}
};

class MobileMouLiegongEffect : public TriggerSkill
{
public:
	MobileMouLiegongEffect() : TriggerSkill("#mobilemouliegong")
	{
		events << ConfirmDamage << CardFinished << CardOffset << CardOnEffect << EventLoseSkill; // << JinkEffect;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	int getPriority(TriggerEvent event) const
	{
		if (event == CardOffset || event == CardOnEffect || event == CardFinished)
			return 5;
		return TriggerSkill::getPriority(event);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventLoseSkill) {
			if (data.toString() != "mobilemouliegong") return false;
			foreach (QString mark, player->getMarkNames()) {
				if (mark.startsWith("&mobilemouliegong+"))
					room->setPlayerMark(player, mark, 0);
			}
		} else if (event == CardOnEffect || event == CardOffset) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if (!effect.card->hasFlag("mobilemouliegongUsed")) return false;
			room->setPlayerProperty(effect.to, "MobileMouLiegongTargetRecords", "");
		} else if (event == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->hasFlag("mobilemouliegongUsed")) return false;
			foreach (QString flag, use.card->getFlags()) {
				if (flag.startsWith("mobilemouliegongUsed_")){
					QStringList flags = flag.split("_");
					ServerPlayer *from = room->findPlayerByObjectName(flags.last(), true);
					if (from){
						from->setTag("MobileMouLiegongRecords", QStringList());
						foreach (QString mark, from->getMarkNames()) {
							if (mark.startsWith("&mobilemouliegong+"))
								room->setPlayerMark(from, mark, 0);
						}
					}
				}
			}
		} else if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->hasFlag("mobilemouliegongUsed") || !damage.to || damage.to->isDead()) return false;
			int d = 0;
			foreach (QString flag, damage.card->getFlags()) {
				if (!flag.startsWith("mobilemouliegongAddDamage_")) continue;
				QStringList flags = flag.split("_");
				d = flags.last().toInt();
				if (d > 0) break;
			}
			if (d <= 0) return false;

			LogMessage log;
			log.type = "#YHHankaiDamage";
			log.from = player;
			log.to << damage.to;
			log.arg = "mobilemouliegong";
			log.arg2 = QString::number(damage.damage);
			log.arg3 = QString::number(damage.damage += d);
			room->sendLog(log);

			data = QVariant::fromValue(damage);
		}
		return false;
	}
};

class MobileMouLiegongLimit : public CardLimitSkill
{
public:
	MobileMouLiegongLimit() : CardLimitSkill("#mobilemouliegong-limit")
	{
		frequency = NotFrequent;
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target, const Card *card) const
	{
		if(target->getMark("&mobilemoufangzhu+2-SelfClear")>0&&card->getTypeId()!=1&&target->handCards().contains(card->getId()))
			return card->toString();
		if(target->getMark("&mobilemoufangzhu+6-SelfClear")>0&&card->getTypeId()!=2&&target->handCards().contains(card->getId()))
			return card->toString();
		if(target->getMark("&mobilemoufangzhu+8-SelfClear")>0&&card->getTypeId()!=3&&target->handCards().contains(card->getId()))
			return card->toString();
		QString record = target->property("MobileMouLiegongTargetRecords").toString();
		if (record.isEmpty()) return "";
		return "Jink|" + record;
	}
};

MobileMouKejiCard::MobileMouKejiCard()
{
	target_fixed = true;
}

void MobileMouKejiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->addPlayerMark(source, "mobilemoukeji-PlayClear", subcardsLength() + 1);
	if (subcards.isEmpty()) {
		room->loseHp(HpLostStruct(source, 1, "mobilemoukeji", source));
		if (source->isAlive())
			source->gainHujia(2, 5);
	} else
		source->gainHujia(1, 5);
}

class MobileMouKeji : public ViewAsSkill
{
public:
	MobileMouKeji() : ViewAsSkill("mobilemoukeji")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		return selected.isEmpty() && !Self->isJilei(to_select);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		int mark = Self->getMark("mobilemoukeji-PlayClear");
		if (mark <= 0 && !cards.isEmpty() && cards.length() != 1) return nullptr;
		if (mark == 1 && cards.length() != 1) return nullptr;
		if (mark >= 2 && !cards.isEmpty()) return nullptr;
		MobileMouKejiCard *c = new MobileMouKejiCard;
		if (!cards.isEmpty())
			c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if (player->getMark("mobilemoudujiang") > 0)
			return !player->hasUsed("MobileMouKejiCard");
		return player->getMark("mobilemoukeji-PlayClear") < 3;
	}
};

class MobileMouKejiMax : public MaxCardsSkill
{
public:
	MobileMouKejiMax() : MaxCardsSkill("#mobilemoukeji-max")
	{
		frequency = NotFrequent;
	}

	int getExtra(const Player *target) const
	{
		if (target->hasSkill("mobilemoukeji"))
			return qMax(target->getHujia(), 0);
		return 0;
	}
};

class MobileMouKejiLimit : public CardLimitSkill
{
public:
	MobileMouKejiLimit() : CardLimitSkill("#mobilemoukeji-limit")
	{
		frequency = NotFrequent;
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target) const
	{
		if (!target->hasFlag("Global_Dying") && target->hasSkill("mobilemoukeji"))
			return "Peach";
		return "";
	}
};

class MobileMouDujiang : public PhaseChangeSkill
{
public:
	MobileMouDujiang() : PhaseChangeSkill("mobilemoudujiang")
	{
		frequency = Wake;
		waked_skills = "mobilemouduojing";
	}

	bool triggerable(const ServerPlayer *player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getHujia() >= 3) {
			LogMessage log;
			log.type = "#MobileMouDujiang";
			log.from = player;
			log.arg = QString::number(player->getHujia());
			log.arg2 = objectName();
			room->sendLog(log);
		}else if(!player->canWake(objectName()))
			return false;
		player->peiyin(this);
		room->notifySkillInvoked(player, objectName());
		room->doSuperLightbox(player, objectName());

		room->setPlayerMark(player, "mobilemoudujiang", 1);
		if (room->changeMaxHpForAwakenSkill(player, 0, objectName()))
			room->acquireSkill(player, "mobilemouduojing");
		return false;
	}
};

class MobileMouDuojiang : public TriggerSkill
{
public:
	MobileMouDuojiang() : TriggerSkill("mobilemouduojing")
	{
		events << TargetSpecifying;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash")) return false;
		foreach (ServerPlayer *p, use.to) {
			if (player->isDead() || player->getHujia() <= 0) return false;
			if (p == player || p->isDead()) continue;
			if (!player->askForSkillInvoke(this, p)) continue;
			player->peiyin(this);
			player->loseHujia(1);
			if (p->isAlive())
				p->addQinggangTag(use.card);
			if (player->isAlive() && p->isAlive() && !p->isKongcheng()) {
				int card_id = room->askForCardChosen(player, p, "h", "mobilemouduojing");
				CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
				room->obtainCard(player, Sanguosha->getCard(card_id),
					reason, room->getCardPlace(card_id) != Player::PlaceHand);
			}
			if (player->isAlive())
				room->addSlashCishu(player, 1);
		}
		return false;
	}
};

class MobileMouXiayuan : public MasochismSkill
{
public:
	MobileMouXiayuan() : MasochismSkill("mobilemouxiayuan")
	{
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	void onDamaged(ServerPlayer *player, const DamageStruct &damage) const
	{
		Room *room = player->getRoom();
		int d = 0;
		foreach (QString tip, damage.tips) {
			if (!tip.startsWith("MobileMouDuojiangDamage_")) continue;
			QStringList tips = tip.split("_");
			d = tips.last().toInt();
			if (d > 0) break;
		}
		if (d <= 0) return;

		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (player->isDead()) return;
			if (p->isDead() || !p->hasSkill(objectName()) || p->getMark("mobilemouxiayuan_lun") > 0
			|| !p->canDiscard(p, "he") || p->getCardCount() < 2) continue;
			if (!room->askForDiscard(p, objectName(), 2, 2, true, false, "@mobilemouxiayuan:" + player->objectName() + "::" + QString::number(d),
									".|.|.|hand", objectName())) continue;
			room->addPlayerMark(p, "mobilemouxiayuan_lun");
			if (player->isAlive())
				player->gainHujia(d, 5);
		}
	}
};

class MobileMouJieyue : public PhaseChangeSkill
{
public:
	MobileMouJieyue() : PhaseChangeSkill("mobilemoujieyue")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Finish) return false;
		ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@mobilemoujieyue-invoke", true, true);
		if (!t) return false;
		room->broadcastSkillInvoke(this);
		t->gainHujia(1, 5);
		t->drawCards(2, objectName());
		if (t->isAlive() && !t->isNude() && player->isAlive()) {
			const Card *ex = room->askForExchange(t, objectName(), 2, 2, true, "@mobilemoujieyue-give:" + player->objectName());
			room->giveCard(t, player, ex, objectName());
		}
		return false;
	}
};

MobileMouXianzhenCard::MobileMouXianzhenCard()
{
}

bool MobileMouXianzhenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select->getHp()<Self->getHp();
}

void MobileMouXianzhenCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	room->setPlayerMark(effect.from,effect.to->objectName()+"mobilemouxianzhenTo-PlayClear",1);
	room->setPlayerMark(effect.to,"&mobilemouxianzhen+#"+effect.from->objectName(),1);
}

class MobileMouXianzhenvs : public ZeroCardViewAsSkill
{
public:
	MobileMouXianzhenvs() : ZeroCardViewAsSkill("mobilemouxianzhen")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("MobileMouXianzhenCard")<1;
	}

	const Card *viewAs() const
	{
		return new MobileMouXianzhenCard;
	}
};

class MobileMouXianzhen : public TriggerSkill
{
public:
	MobileMouXianzhen() : TriggerSkill("mobilemouxianzhen")
	{
		events << TargetSpecified << Pindian;
		view_as_skill = new MobileMouXianzhenvs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Slash")){
				foreach (ServerPlayer *p, use.to){
					if(p->getMark("&mobilemouxianzhen+#"+player->objectName())>0
					&&player->canPindian(p)&&player->askForSkillInvoke(this,p)){
						if(player->pindian(p,objectName())){
							if(player->getMark("mobilemouxianzhenPindianDamage")<1){
								player->addMark("mobilemouxianzhenPindianDamage");
								room->damage(DamageStruct(objectName(),player,p));
							}
							foreach (ServerPlayer *q, use.to)
								q->addQinggangTag(use.card);
							use.m_addHistory = false;
							data.setValue(use);
						}
					}
				}
			}
		}else{
			PindianStruct *pd = data.value<PindianStruct*>();
			if(pd->from->getMark("&mobilemouxianzhen+#"+pd->to->objectName())>0&&pd->to->isAlive()){
				if(pd->from_card->isKindOf("Slash")&&!room->getCardOwner(pd->from_card->getEffectiveId()))
					pd->to->obtainCard(pd->from_card);
			}
			if(pd->to->getMark("&mobilemouxianzhen+#"+pd->from->objectName())>0&&pd->from->isAlive()){
				if(pd->to_card->isKindOf("Slash")&&!room->getCardOwner(pd->to_card->getEffectiveId()))
					pd->from->obtainCard(pd->to_card);
			}
		}
		return false;
	}
};

class MobileMouJinjiuvs : public ViewAsSkill
{
public:
	MobileMouJinjiuvs() : ViewAsSkill("mobilemoujinjiu")
	{
		response_pattern = "slash";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		return selected.isEmpty() && to_select->isKindOf("Analeptic");
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty()) return nullptr;
		Card *c = Sanguosha->cloneCard("slash");
		c->setSkillName("mobilemoujinjiu");
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getHandcardNum()>0;
	}
};

class MobileMouJinjiu : public TriggerSkill
{
public:
	MobileMouJinjiu() : TriggerSkill("mobilemoujinjiu")
	{
		events << DamageInflicted << PindianVerifying;
		view_as_skill = new MobileMouJinjiuvs;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")&&damage.card->hasFlag("drank")&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				damage.damage = 1;
				data.setValue(damage);
			}
		}else{
			PindianStruct *pd = data.value<PindianStruct*>();
			if(pd->to_card->isKindOf("Analeptic")&&pd->from->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(pd->from,this);
				pd->to_number = 1;
				data.setValue(pd);
			}
			if(pd->from_card->isKindOf("Analeptic")&&pd->to->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(pd->to,this);
				pd->from_number = 1;
				data.setValue(pd);
			}
		}
		return false;
	}
};

class MobileMouJinjiuLimit : public CardLimitSkill
{
public:
	MobileMouJinjiuLimit() : CardLimitSkill("#MobileMouJinjiuLimit")
	{
	}

	QString limitList(const Player *) const
	{
		return "use,response";
	}

	QString limitPattern(const Player *target) const
	{
		if(target->hasSkill("mobilemoujinjiu"))
			return "Analeptic";
		return "";
	}
};

class MobileMouJinjiuLimit2 : public CardLimitSkill
{
public:
	MobileMouJinjiuLimit2() : CardLimitSkill("#MobileMouJinjiuLimit2")
	{
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target) const
	{
		foreach (const Player *p, target->getAliveSiblings()){
			if(p->hasFlag("CurrentPlayer")&&p->hasSkill("mobilemoujinjiu"))
				return "Analeptic";
		}
		return "";
	}
};

class MobileMouQianxunvs : public ZeroCardViewAsSkill
{
public:
	MobileMouQianxunvs() : ZeroCardViewAsSkill("mobilemouqianxun")
	{
		response_pattern = "@@mobilemouqianxun";
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	const Card *viewAs() const
	{
		Card*dc = Sanguosha->cloneCard(Self->property("mobilemouqianxunTrick").toString());
		dc->setSkillName("_mobilemouqianxun");
		return dc;
	}
};

class MobileMouQianxun : public TriggerSkill
{
public:
	MobileMouQianxun() : TriggerSkill("mobilemouqianxun")
	{
		events << CardEffected << EventPhaseStart << EventPhaseChanging;
		view_as_skill = new MobileMouQianxunvs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardEffected){
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->isKindOf("TrickCard")&&player->isAlive()&&effect.from!=player&&player->hasSkill(objectName())){
				QStringList tricks = player->getTag("mobilemouqianxunTricks").toStringList();
				if(tricks.contains(effect.card->objectName())) return false;
				room->sendCompulsoryTriggerLog(player,this);
				tricks << effect.card->objectName();
				player->setTag("mobilemouqianxunTricks", tricks);
				int n = qMin(5,tricks.length());
				const Card*dc = room->askForExchange(player,objectName(),n,1,true,"mobilemouqianxun0:"+QString::number(n),true);
				if(dc) player->addToPile(objectName(),dc,false);
			}
		}else if (event == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					QList<int>ids = p->getPile(objectName());
					if(ids.isEmpty()) continue;
					Card*dc = new DummyCard(ids);
					p->obtainCard(dc,false);
					dc->deleteLater();
				}
			}
		}else{
			if(player->getPhase()==Player::Play&&player->isAlive()&&player->hasSkill(objectName())){
				QStringList tricks = player->getTag("mobilemouqianxunTricks").toStringList();
				if(tricks.isEmpty()||!player->askForSkillInvoke(objectName()+"$-1",tricks)) return false;
				QString choice = room->askForChoice(player,objectName(),tricks.join("+"));
				tricks.removeOne(choice);
				player->setTag("mobilemouqianxunTricks", tricks);
				Card*dc = Sanguosha->cloneCard(choice);
				dc->deleteLater();
				if(dc->isNDTrick()){
					room->setPlayerProperty(player,"mobilemouqianxunTrick",choice);
					room->askForUseCard(player,"@@mobilemouqianxun","mobilemouqianxun1:"+choice);
				}
			}
		}
		return false;
	}
};

class MobileMouLianying : public TriggerSkill
{
public:
	MobileMouLianying() : TriggerSkill("mobilemoulianying")
	{
		events << CardsMoveOneTime << EventPhaseChanging;
		view_as_skill = new MobileMouQianxunvs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from==player){
				foreach (Player::Place p, move.from_places){
					if(p==Player::PlaceHand||p==Player::PlaceEquip)
						player->addMark("mobilemoulianyingNum-Clear");
				}
			}
		}else if (event == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)){
					int n = p->getMark("mobilemoulianyingNum-Clear");
					if(n>0&&p->hasSkill(objectName())&&p->askForSkillInvoke(this)){
						p->peiyin(this);
						QList<int>ids = room->getNCards(qMin(5,n));
						p->assignmentCards(ids,"mobilemoulianying|mobilemoulianying0",room->getAlivePlayers(),ids.length(),ids.length());
					}
				}
			}
		}
		return false;
	}
};

MobileMouJingceCard::MobileMouJingceCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool MobileMouJingceCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.length()<3;
}

bool MobileMouJingceCard::targetsFeasible(const QList<const Player *> &, const Player *) const
{
	return true;
}

void MobileMouJingceCard::onUse(Room *room, CardUseStruct &use) const
{
	int n = 0;
	QHash<int,QString>id2p;
	foreach (ServerPlayer *p, use.to){
		id2p[subcards[n]] = p->objectName();
		room->setCardTip(subcards[n],p->getGeneralName());
		n++;
	}
	QStringList id2ps;
	QList<int>ids = room->askForGuanxing(use.from,subcards,Room::GuanxingUpOnly,false);
	for (int i = 0; i < 3; i++) {
		if(id2p.contains(ids[i])){
			id2ps << QString("%1|%2").arg(ids[i]).arg(id2p[ids[i]]);
		}else{
			id2ps << QString::number(ids[i]);
		}
		room->moveCardsInToDrawpile(use.from,ids[i],"mobilemoujingce",(i+1)*3,true);
	}
	use.from->setTag("mobilemoujingceId2ps", id2ps);
}

class MobileMouJingcevs : public ViewAsSkill
{
public:
	MobileMouJingcevs() : ViewAsSkill("mobilemoujingce")
	{
		response_pattern = "@@mobilemoujingce";
		expand_pile = "mobilemoujingce";
	}

	bool viewFilter(const QList<const Card *> &, const Card *to_select) const
	{
		return Self->getPileName(to_select->getId())=="mobilemoujingce";
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.length()<3) return nullptr;
		Card *c = new MobileMouJingceCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}
};

class MobileMouJingce : public TriggerSkill
{
public:
	MobileMouJingce() : TriggerSkill("mobilemoujingce")
	{
		events << CardsMoveOneTime << EventPhaseChanging;
		view_as_skill = new MobileMouJingcevs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::PlaceHand&&player->hasSkill(objectName())){
				QStringList id2ps = player->getTag("mobilemoujingceId2ps").toStringList();
				int n = 1;
				foreach (QString ip, id2ps){
					if(ip.split("|").contains(move.to->objectName())){
						foreach (int id, move.card_ids){
							if(ip.split("|").contains(QString::number(id))){
								id2ps[n-1] = "has_id2p";
								player->setTag("mobilemoujingceId2ps", id2ps);
								room->sendCompulsoryTriggerLog(player,objectName());
								player->drawCards(n,objectName());
								break;
							}
						}
					}
					n++;
				}
			}
		}else if (event == EventPhaseChanging&&player->hasSkill(objectName())) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive){
				if(player->getPile(objectName()).isEmpty()) return false;
				room->askForUseCard(player,"@@mobilemoujingce","mobilemoujingce0");
			}else if(change.from == Player::NotActive){
				if(player->getPile(objectName()).isEmpty()){
					room->sendCompulsoryTriggerLog(player,this);
					player->removeTag("mobilemoujingceId2ps");
					player->addToPile(objectName(),room->getNCards(3));
				}
			}
		}
		return false;
	}
};











MobileMouYuPackage::MobileMouYuPackage()
	: Package("mobilemouyu")
{
	General *mobilemou_huangzhong = new General(this, "mobilemou_huangzhong", "shu", 4);
	mobilemou_huangzhong->addSkill(new MobileMouLiegong);
	mobilemou_huangzhong->addSkill(new MobileMouLiegongEffect);
	mobilemou_huangzhong->addSkill(new MobileMouLiegongLimit);
	mobilemou_huangzhong->addSkill("#tenyearliegongmod");
	related_skills.insertMulti("mobilemouliegong", "#mobilemouliegong");
	related_skills.insertMulti("mobilemouliegong", "#mobilemouliegong-limit");
	related_skills.insertMulti("mobilemouliegong", "#tenyearliegongmod");

	General *mobilemou_lvmeng = new General(this, "mobilemou_lvmeng", "wu", 4);
	mobilemou_lvmeng->addSkill(new MobileMouKeji);
	mobilemou_lvmeng->addSkill(new MobileMouKejiMax);
	mobilemou_lvmeng->addSkill(new MobileMouKejiLimit);
	mobilemou_lvmeng->addSkill(new MobileMouDujiang);
	mobilemou_lvmeng->addRelateSkill("mobilemouduojing");
	related_skills.insertMulti("mobilemoukeji", "#mobilemoukeji-max");
	related_skills.insertMulti("mobilemoukeji", "#mobilemoukeji-limit");

	General *mobilemou_yujin = new General(this, "mobilemou_yujin", "wei", 4);
	mobilemou_yujin->addSkill(new MobileMouXiayuan);
	mobilemou_yujin->addSkill(new MobileMouJieyue);

	addMetaObject<MobileMouKejiCard>();

	skills << new MobileMouDuojiang;

	General *mobilemou_gaoshun = new General(this, "mobilemou_gaoshun", "qun", 4);
	mobilemou_gaoshun->addSkill(new MobileMouXianzhen);
	mobilemou_gaoshun->addSkill(new MobileMouJinjiu);
	mobilemou_gaoshun->addSkill(new MobileMouJinjiuLimit);
	mobilemou_gaoshun->addSkill(new MobileMouJinjiuLimit2);
	addMetaObject<MobileMouXianzhenCard>();

	General *mobilemou_luxun = new General(this, "mobilemou_luxun", "wu", 3);
	mobilemou_luxun->addSkill(new MobileMouQianxun);
	mobilemou_luxun->addSkill(new MobileMouLianying);

	General *mobilemou_guohuai = new General(this, "mobilemou_guohuai", "wei", 4);
	mobilemou_guohuai->addSkill(new MobileMouJingce);
	addMetaObject<MobileMouJingceCard>();

}
ADD_PACKAGE(MobileMouYu)


MobileMouYangweiCard::MobileMouYangweiCard()
{
	target_fixed = true;
}

void MobileMouYangweiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	source->drawCards(2, "mobilemouyangwei");
	room->addPlayerMark(source, "mobilemouyangwei-PlayClear");
	room->setPlayerMark(source, "mobilemouyangweiUsed", 2);
}

class MobileMouYangweiVS : public ZeroCardViewAsSkill
{
public:
	MobileMouYangweiVS() : ZeroCardViewAsSkill("mobilemouyangwei")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("MobileMouYangweiCard") && player->getMark("mobilemouyangweiUsed") <= 0;
	}

	const Card *viewAs() const
	{
		return new MobileMouYangweiCard;
	}
};

class MobileMouYangwei : public PhaseChangeSkill
{
public:
	MobileMouYangwei() : PhaseChangeSkill("mobilemouyangwei")
	{
		view_as_skill = new MobileMouYangweiVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->getMark("mobilemouyangweiUsed") > 0 && target->getPhase() == Player::Finish;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Finish) return false;
		room->removePlayerMark(player, "mobilemouyangweiUsed");
		return false;
	}
};

class MobileMouYangweiEffect : public TriggerSkill
{
public:
	MobileMouYangweiEffect() : TriggerSkill("#mobilemouyangwei")
	{
		events << CardUsed;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->getMark("mobilemouyangwei-PlayClear") > 0 && target->getPhase() == Player::Play;
	}

	bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash")) return false;
		foreach (ServerPlayer *p, use.to)
			p->addQinggangTag(use.card);
		return false;
	}
};

class MobileMouYangweiTargetMod : public TargetModSkill
{
public:
	MobileMouYangweiTargetMod() : TargetModSkill("#mobilemouyangwei-target")
	{
		frequency = NotFrequent;
	}

	int getResidueNum(const Player *from, const Card *, const Player *) const
	{
		if (from->getPhase() == Player::Play)
			return from->getMark("mobilemouyangwei-PlayClear");
		return 0;
	}

	int getDistanceLimit(const Player *from, const Card *, const Player *to) const
	{
		if(from->getPhase() == Player::Play){
			if(from->getMark("mobilemouyangwei-PlayClear")>0)
				return 1000;
			if(from->getMark(to->objectName()+"mobilemouxianzhenTo-PlayClear")>0)
				return 1000;
		}
		return 0;
	}
};

MobileMouTiaoxinCard::MobileMouTiaoxinCard()
{
}

bool MobileMouTiaoxinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length()<Self->getMark("&charge_num") && to_select!=Self;
}

void MobileMouTiaoxinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	source->loseMark("&charge_num",targets.length());
	foreach (ServerPlayer *p, targets){
		source->addMark("mobilemoutiaoxinTo"+p->objectName());
		if(room->askForUseSlashTo(p,source,"mobilemoutiaoxin0:"+source->objectName(),false))
			continue;
		const Card*dc = room->askForExchange(p,"mobilemoutiaoxin",1,1,true,"mobilemoutiaoxin1:"+source->objectName());
		if(dc) room->giveCard(p,source,dc,"mobilemoutiaoxin");
	}
}

class MobileMouTiaoxinvs : public ZeroCardViewAsSkill
{
public:
	MobileMouTiaoxinvs() : ZeroCardViewAsSkill("mobilemoutiaoxin")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("MobileMouTiaoxinCard")<1&&player->getMark("&charge_num")>0;
	}

	const Card *viewAs() const
	{
		return new MobileMouTiaoxinCard;
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

class MobileMouTiaoxin : public TriggerSkill
{
public:
	MobileMouTiaoxin() : TriggerSkill("mobilemoutiaoxin")
	{
		events << CardsMoveOneTime;
		view_as_skill = new MobileMouTiaoxinvs;
		setProperty("ChargeNum","4/4");
	}
	bool trigger(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from==player&&player->getPhase()==Player::Discard
			&&(move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD){
				int n = getChargeMax(player)-player->getMark("&charge_num");
				if(n>0) player->gainMark("&charge_num",qMin(move.card_ids.length(),n));
			}
		}
		return false;
	}
};

class MobileMouZhiji : public PhaseChangeSkill
{
public:
	MobileMouZhiji() : PhaseChangeSkill("mobilemouzhiji")
	{
		frequency = Wake;
		waked_skills = "#MobileMouZhijiBf,#MobileMouZhijiPro";
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->getMark("mobilemouzhiji")<1
		&&target->getPhase()==Player::Start&&target->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		int n = 0;
		foreach (ServerPlayer *p, room->getAlivePlayers()){
			if(player->getMark("mobilemoutiaoxinTo"+p->objectName())>0)
				n++;
		}
		if(n>3||player->canWake(objectName())){
			room->sendCompulsoryTriggerLog(player, this);
			room->addPlayerMark(player, "mobilemouzhiji");
			room->doSuperLightbox(player, objectName());
			room->changeMaxHpForAwakenSkill(player, -1, objectName());
			foreach (ServerPlayer *p, room->askForPlayersChosen(player,room->getAlivePlayers(),objectName(),1,99,"mobilemouzhiji0")){
				room->doAnimate(1,player->objectName(),p->objectName());
				p->gainMark("&mou_beifa+#"+player->objectName());
			}
		}
		return false;
	}
};

class MobileMouZhijiBf : public PhaseChangeSkill
{
public:
	MobileMouZhijiBf() : PhaseChangeSkill("#MobileMouZhijiBf")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if(player->getPhase()==Player::RoundStart){
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				p->loseAllMarks("&mou_beifa+#"+player->objectName());
			}
		}
		return false;
	}
};

class MobileMouZhijiPro : public ProhibitSkill
{
public:
	MobileMouZhijiPro() : ProhibitSkill("#MobileMouZhijiPro")
	{
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		if(card->getTypeId()>0&&from!=to){
			foreach (const Player *p, to->getAliveSiblings()){
				if(from->getMark("&mou_beifa+#"+p->objectName())>0)
					return true;
			}
		}
		return false;
	}
};

class MobileMouYicong : public TriggerSkill
{
public:
	MobileMouYicong() : TriggerSkill("mobilemouyicong")
	{
		events << RoundStart;
		setProperty("ChargeNum","2/4");
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (triggerEvent == RoundStart) {
			int n = player->getMark("&charge_num");
			int x = 4-player->getPile("&mou_hu").length();
			if(n>0&&x>0&&player->askForSkillInvoke(this)){
				player->peiyin(this);
				QStringList choices;
				for (int i = 1; i <= qMin(x,n); i++)
					choices << QString::number(i);
				x = room->askForChoice(player,objectName(),choices.join("+")).toInt();
				player->loseMark("&charge_num",x);
				QString choice = room->askForChoice(player,objectName(),"mouyicong1+mouyicong2");
				room->setPlayerMark(player,choice+"_lun",1);
				QList<int>ids;
				foreach (int id, room->getDrawPile()){
					const Card*c = Sanguosha->getCard(id);
					if(c->isKindOf("Slash")){
						if(choice=="mouyicong1"){
							x--;
							ids << id;
							if(x<1) break;
						}
					}else if(c->isKindOf("Jink")){
						if(choice=="mouyicong2"){
							x--;
							ids << id;
							if(x<1) break;
						}
					}
				}
				player->addToPile("&mou_hu",ids);
			}
		}
		return false;
	}
};

class MobileMouYicongBf : public DistanceSkill
{
public:
	MobileMouYicongBf() : DistanceSkill("#MobileMouYicongBf")
	{
	}

	int getCorrect(const Player *from, const Player *to) const
	{
		int n = 0;
		if (from->getMark("mouyicong1_lun")>0)
			n--;
		if (to->getMark("mouyicong2_lun")>0)
			n++;
		return n;
	}
};

class MobileMouQiaomeng : public TriggerSkill
{
public:
	MobileMouQiaomeng() : TriggerSkill("mobilemouqiaomeng")
	{
		events << Damage;
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == Damage) {
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->isKindOf("Slash")&&player->hasSkill("mobilemouyicong",true)){
				int n = getChargeMax(player)-player->getMark("&charge_num");
				if(n>0||player->canDiscard(damage.to,"hej")){
					if(player->askForSkillInvoke(this)){
						player->peiyin(this);
						int id = room->askForCardChosen(player,damage.to,"hej",objectName(),false,Card::MethodDiscard,QList<int>(),true);
						if(id>-1){
							room->throwCard(id,objectName(),damage.to,player);
							player->drawCards(1,objectName());
						}else
							player->gainMark("&charge_num",qMin(3,n));
					}
				}
			}
		}
		return false;
	}
};

class MobileMouHuanshi : public TriggerSkill
{
public:
	MobileMouHuanshi() : TriggerSkill("mobilemouhuanshi")
	{
		events << AskForRetrial;
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == AskForRetrial) {
			QList<int>ids = room->getNCards(1);
			room->fillAG(ids,player);
			if(player->askForSkillInvoke(this,data,false)){
				room->clearAG(player);
				QList<int>hids = player->handCards();
				hids << ids;
				player->setTag("mobilemouhuanshiJudge", data);
				room->fillAG(hids,player);
				int id = room->askForAG(player,hids,false,objectName(),"mobilemouhuanshi0");
				player->removeTag("mobilemouhuanshiJudge");
				JudgeStruct *judge = data.value<JudgeStruct *>();
				player->peiyin(this);
				room->notifySkillInvoked(player,objectName());
				if(id==hids.last()){
					ids.removeOne(id);
					room->moveCardTo(judge->card,nullptr,Player::DrawPile,true);
				}
				room->retrial(Sanguosha->getCard(id),player,judge,objectName(),id!=hids.last());
			}
			room->clearAG(player);
			room->returnToTopDrawPile(ids);
		}
		return false;
	}
};

class MobileMouHongyuan : public TriggerSkill
{
public:
	MobileMouHongyuan() : TriggerSkill("mobilemouhongyuan")
	{
		events << CardsMoveOneTime;
		setProperty("ChargeNum","1/3");
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime&&player->getMark("&charge_num")>0) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to==player&&move.to_place==Player::PlaceHand&&move.card_ids.length()>1){
				QList<ServerPlayer *>tps = room->askForPlayersChosen(player,room->getAlivePlayers(),objectName(),0,2,"mobilemouhongyuan0",true);
				if(tps.length()>0){
					player->peiyin(this);
					player->loseMark("&charge_num");
					room->drawCards(tps,1,objectName());
				}
			}else if(move.from&&move.from!=player&&move.from->isAlive()){
				int n = 0;
				foreach (Player::Place p, move.from_places){
					if(p==Player::PlaceHand||p==Player::PlaceEquip)
						n++;
				}
				ServerPlayer *from = (ServerPlayer *)move.from;
				if(n>1&&player->askForSkillInvoke(this,from)){
					player->peiyin(this);
					player->loseMark("&charge_num");
					if(isNormalGameMode(room->getMode()))
						n = 1;
					else
						n = 2;
					from->drawCards(n,objectName());
				}
			}
		}
		return false;
	}
};

class MobileMouMingzhe : public TriggerSkill
{
public:
	MobileMouMingzhe() : TriggerSkill("mobilemoumingzhe")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event==CardsMoveOneTime&&player->getMark("mobilemoumingzheUse_lun")<2&&!player->hasFlag("CurrentPlayer")) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from==player&&(move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip))){
				room->sendCompulsoryTriggerLog(player,this);
				player->addMark("mobilemoumingzheUse_lun");
				ServerPlayer *tp = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"mobilemoumingzhe0");
				room->doAnimate(1,player->objectName(),tp->objectName());
				foreach (const Skill *s, tp->getVisibleSkillList()){
					QString cn = s->property("ChargeNum").toString();
					if(cn.contains("/")){
						tp->gainMark("&charge_num");
						break;
					}
				}
				foreach (int id, move.card_ids){
					if(Sanguosha->getCard(id)->getTypeId()>1){
						tp->drawCards(1,objectName());
						break;
					}
				}
			}
		}
		return false;
	}
};

class MobileMouWushuang : public TriggerSkill
{
public:
	MobileMouWushuang() : TriggerSkill("mobilemouwushuang")
	{
		events << TargetSpecified << CardEffected << CardResponded << CardUsed << ConfirmDamage;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Slash")) {
				if (player->hasSkill(objectName())) {
					room->sendCompulsoryTriggerLog(player, this);
					QVariantList jink_list = player->getTag("Jink_" + use.card->toString()).toList();
					for (int i = 0; i < use.to.length(); i++) {
						if (jink_list.at(i).toInt()==1)
							jink_list.replace(i, QVariant(2));
					}
					player->setTag("Jink_" + use.card->toString(), jink_list);
					room->setCardFlag(use.card,"mobilemouwushuangBf");
				}
			} else if (use.card->isKindOf("Duel")) {
				QStringList mobilemouwushuang_tag;
				if (player->hasSkill(objectName())) {
					room->sendCompulsoryTriggerLog(player, this);
					foreach(ServerPlayer *to, use.to)
						mobilemouwushuang_tag << to->objectName();
					room->setCardFlag(use.card,"mobilemouwushuangBf");
				}
				foreach(ServerPlayer *to, use.to){
					if(to->hasSkill(objectName())){
						room->sendCompulsoryTriggerLog(player, this);
						mobilemouwushuang_tag << player->objectName();
						room->setCardFlag(use.card,"mobilemouwushuangBf");
					}
				}
				room->setTag("MobileMouWushuang_"+use.card->toString(), mobilemouwushuang_tag);
			}
		} else if (triggerEvent == CardEffected) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->isKindOf("Duel")){
				QStringList mobilemouwushuang_tag = room->getTag("MobileMouWushuang_"+effect.card->toString()).toStringList();
				if(mobilemouwushuang_tag.contains(effect.to->objectName())||mobilemouwushuang_tag.contains(effect.from->objectName()))
					room->setTag("mobilemouwushuangData",data);
			}
		} else if (triggerEvent == CardResponded) {
			CardResponseStruct resp = data.value<CardResponseStruct>();
			if(resp.m_toCard&&resp.m_toCard->isKindOf("Duel")&&!player->hasFlag("mobilemouwushuangSlash")){
				room->setCardFlag(resp.m_toCard,"mobilemouwushuangUse"+player->objectName());
				QStringList mobilemouwushuang_tag = room->getTag("MobileMouWushuang_"+resp.m_toCard->toString()).toStringList();
				if(mobilemouwushuang_tag.contains(player->objectName())){
					room->setPlayerFlag(player,"mobilemouwushuangSlash");
					if(!room->askForCard(player,"slash","duel-slash:"+resp.m_who->objectName(),room->getTag("mobilemouwushuangData"),
						Card::MethodResponse,resp.m_who,false,"",false,resp.m_toCard)){
						resp.nullified = true;
						data.setValue(resp);
					}
					room->setPlayerFlag(player,"-mobilemouwushuangSlash");
				}
			}
		} else if (triggerEvent == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->isKindOf("Jink")){
				if(use.whocard)
					room->setCardFlag(use.whocard,"mobilemouwushuangUse"+player->objectName());
			}
		} else if (triggerEvent == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("mobilemouwushuangBf")){
				if(damage.card->isKindOf("Slash")){
					if(damage.card->hasFlag("mobilemouwushuangUse"+damage.to->objectName())) return false;
					player->damageRevises(data,1);
				}else if(damage.card->isKindOf("Duel")){
					QStringList mobilemouwushuang_tag = room->getTag("MobileMouWushuang_"+damage.card->toString()).toStringList();
					if(mobilemouwushuang_tag.contains(damage.to->objectName())){
						if(damage.card->hasFlag("mobilemouwushuangUse"+damage.to->objectName())) return false;
						player->damageRevises(data,1);
					}
				}
			}
		}
		return false;
	}
};

class MobileMouLiyu : public TriggerSkill
{
public:
	MobileMouLiyu() : TriggerSkill("mobilemouliyu")
	{
		events << Damage;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.to->isAlive() && player != damage.to && !damage.to->hasFlag("Global_DebutFlag")
			&& !damage.to->isAllNude() && damage.card && damage.card->isKindOf("Slash")) {
			if(player->askForSkillInvoke(this,damage.to)){
				player->peiyin(this);
				int n = qMin(damage.damage,damage.to->getCardCount(true,true));
				Card*dc = dummyCard();
				for (int i = 0; i < n; i++) {
					int id = room->askForCardChosen(player,damage.to,"hej",objectName(),false,Card::MethodNone,dc->getSubcards(),i>0);
					if(id<0) break;
					dc->addSubcard(id);
				}
				player->obtainCard(dc,false);
				if(damage.to->isAlive())
					dc->addSubcards(damage.to->drawCardsList(dc->subcardsLength(),objectName()));
				if(damage.to->isAlive()){
					QStringList ts;
					foreach (int id, dc->getSubcards()) {
						QString type = Sanguosha->getCard(id)->getType();
						if(ts.contains(type)) continue;
						ts.append(type);
					}
					if(ts.length()>2){
						Duel *duel = new Duel(Card::NoSuit, 0);
						duel->setSkillName("_mobilemouliyu");
						QList<ServerPlayer *> targets;
						foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
							if(player->isDead()) break;
							if (p != damage.to && player->canUse(duel, p))
								targets << p;
						}
						duel->deleteLater();
						ServerPlayer *target = room->askForPlayerChosen(damage.to, targets, objectName(), "mobilemouliyu0:" + player->objectName(), true);
						if(target){
							room->useCard(CardUseStruct(duel, player, target));
						}else{
							room->acquireOneTurnSkills(damage.to,objectName(),"wushuang");
						}
					}
				}
			}
		}
		return false;
	}
};



MobileMouNengPackage::MobileMouNengPackage()
	: Package("mobilemouneng")
{
	General *mobilemou_huaxiong = new General(this, "mobilemou_huaxiong", "qun", 4);
	mobilemou_huaxiong->setStartHp(2);
	mobilemou_huaxiong->setStartHujia(1);
	mobilemou_huaxiong->addSkill("tenyearyaowu");
	mobilemou_huaxiong->addSkill(new MobileMouYangwei);
	mobilemou_huaxiong->addSkill(new MobileMouYangweiEffect);
	mobilemou_huaxiong->addSkill(new MobileMouYangweiTargetMod);
	related_skills.insertMulti("mobilemouyangwei", "#mobilemouyangwei");
	related_skills.insertMulti("mobilemouyangwei", "#mobilemouyangwei-target");

	addMetaObject<MobileMouYangweiCard>();

	General *mobilemou_jiangwei = new General(this, "mobilemou_jiangwei", "shu", 4);
	mobilemou_jiangwei->setStartHujia(1);
	mobilemou_jiangwei->addSkill(new MobileMouTiaoxin);
	mobilemou_jiangwei->addSkill(new MobileMouZhiji);
	mobilemou_jiangwei->addSkill(new MobileMouZhijiBf);
	mobilemou_jiangwei->addSkill(new MobileMouZhijiPro);
	addMetaObject<MobileMouTiaoxinCard>();

	General *mobilemou_gongsunzan = new General(this, "mobilemou_gongsunzan", "qun", 4);
	mobilemou_gongsunzan->addSkill(new MobileMouYicong);
	mobilemou_gongsunzan->addSkill(new MobileMouQiaomeng);
	mobilemou_gongsunzan->addSkill(new MobileMouYicongBf);

	General *mobilemou_zhugejin = new General(this, "mobilemou_zhugejin", "wu", 3);
	mobilemou_zhugejin->addSkill(new MobileMouHuanshi);
	mobilemou_zhugejin->addSkill(new MobileMouHongyuan);
	mobilemou_zhugejin->addSkill(new MobileMouMingzhe);

	General *mobilemou_lvbu = new General(this, "mobilemou_lvbu", "qun", 4);
	mobilemou_lvbu->addSkill(new MobileMouWushuang);
	mobilemou_lvbu->addSkill(new MobileMouLiyu);

}
ADD_PACKAGE(MobileMouNeng)




MobileMouGangLieCard::MobileMouGangLieCard()
{
}

bool MobileMouGangLieCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select->getMark("MobileMouGangLieDamage"+Self->objectName())>0
		&& to_select->getMark("MobileMouGangLieDamaged"+Self->objectName()) <= 0;
}

void MobileMouGangLieCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	room->addPlayerMark(effect.to, "MobileMouGangLieDamaged"+effect.from->objectName());

	room->damage(DamageStruct("mobilemouganglie", effect.from, effect.to, 2));
}

class MobileMouGangLieVS : public ZeroCardViewAsSkill
{
public:
	MobileMouGangLieVS() : ZeroCardViewAsSkill("mobilemouganglie")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		foreach (const Player *p, player->getAliveSiblings()) {
			if(p->getMark("MobileMouGangLieDamaged"+player->objectName())<1&&p->getMark("MobileMouGangLieDamage"+player->objectName())>0)
				return true;
		}
		return false;
	}

	const Card *viewAs() const
	{
		return new MobileMouGangLieCard;
	}
};

class MobileMouGangLie : public TriggerSkill
{
public:
	MobileMouGangLie() : TriggerSkill("mobilemouganglie")
	{
		events << DamageDone;
		view_as_skill = new MobileMouGangLieVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.from)
			room->addPlayerMark(damage.from,"MobileMouGangLieDamage"+player->objectName());
		
		return false;
	}
};

class MobileMouTongQingjian : public TriggerSkill
{
public:
	MobileMouTongQingjian() : TriggerSkill("mobilemouqingjian")
	{
		events << CardsMoveOneTime << EventPhaseEnd;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if(move.to_place==Player::DiscardPile&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)!=CardMoveReason::S_REASON_USE){
				QList<int>ids;
				int n = qMax(1,player->getHp())-player->getPile("mobilemouqingjian").length();
				foreach (int id, move.card_ids) {
					if(n>0&&room->getCardPlace(id)==Player::DiscardPile){
						ids << id;
						n--;
					}
				}
				if(ids.length()>0){
					room->sendCompulsoryTriggerLog(player, this);
					player->addToPile("mobilemouqingjian",ids);
				}
			}
		}else if(player->getPhase()==Player::Play){
			QList<int> ids = player->getPile("mobilemouqingjian");
			if(ids.length()>0)
				player->assignmentCards(ids,objectName()+"|mobilemouqingjian0:",room->getAlivePlayers(),ids.length(),ids.length());
		}
		return false;
	}
};

MobileMouShensuCard::MobileMouShensuCard()
{
}

bool MobileMouShensuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if(targets.length()>Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this)) return false;
	return to_select->getMark("mobilemoushensuBan-Clear")<1&&Self->canSlash(to_select,false,0,targets);
}

const Card *MobileMouShensuCard::validate(CardUseStruct &use) const
{
	Room *room = use.from->getRoom();
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		room->setPlayerMark(p,"mobilemoushensuBan-Clear",use.to.contains(p)?1:0);
	}
	if(use.from->getTag("mobilemoushensu_choice").toString()!="mobilemoushensu1")
		use.no_respond_list << "_ALL_TARGETS";
	Card *card = Sanguosha->cloneCard("slash");
	card->setSkillName("mobilemoushensu");
	card->deleteLater();
	return card;
}

class MobileMouShensuVs : public ZeroCardViewAsSkill
{
public:
	MobileMouShensuVs() : ZeroCardViewAsSkill("mobilemoushensu")
	{
		response_pattern = "@@mobilemoushensu";
	}

	const Card *viewAs() const
	{
		return new MobileMouShensuCard;
	}
};

class MobileMouShensu : public TriggerSkill
{
public:
	MobileMouShensu() : TriggerSkill("mobilemoushensu")
	{
		events << EventPhaseStart;
		view_as_skill = new MobileMouShensuVs;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (player->getPhase()==Player::RoundStart&&player->askForSkillInvoke(objectName()+"$-1")) {
			QStringList choices;
			choices << "mobilemoushensu1" << "mobilemoushensu2" << "mobilemoushensu3";
			while(choices.length()>1&&player->isAlive()){
				QString choice = room->askForChoice(player,objectName(),choices.join("+"));
				choices.removeOne(choice);
				if(choice=="cancel") break;
				if(choice=="mobilemoushensu1"){
					player->skip(Player::Judge);
					player->skip(Player::Draw);
				}else if(choice=="mobilemoushensu2"){
					player->skip(Player::Draw);
					player->skip(Player::Play);
				}else{
					player->skip(Player::Play);
					player->skip(Player::Discard);
				}
				player->setTag("mobilemoushensu_choice", choice);
				room->askForUseCard(player,"@@mobilemoushensu","mobilemoushensu0");
				if(!choices.contains("cancel")) choices << "cancel";
			}
			if(!choices.contains("mobilemoushensu2")&&choices.length()<2)
				player->turnOver();
		}
		return false;
	}
};

class MobileMouZhengzi : public TriggerSkill
{
public:
	MobileMouZhengzi() : TriggerSkill("mobilemouzhengzi")
	{
		events << DamageDone << EventPhaseChanging;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == DamageDone) {
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from)
				damage.from->addMark("mobilemouzhengziDamage-Clear",damage.damage);
		}else{
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to!=Player::NotActive||player->getMark("mobilemouzhengziDamage-Clear")<player->getHp()||player->isDead()) return false;
			if(player->hasSkill(objectName())&&player->askForSkillInvoke(this,data)){
				player->peiyin(this);
				room->recover(player,RecoverStruct(objectName(),player));
				if(!player->faceUp()) player->turnOver();
				if(player->isChained()) room->setPlayerChained(player);
			}
		}
		return false;
	}
};



MobileMouTongPackage::MobileMouTongPackage()
	: Package("mobilemoutong")
{
	General *mobilemou_xiahoudun = new General(this, "mobilemou_xiahoudun", "wei", 4);
	mobilemou_xiahoudun->addSkill(new MobileMouGangLie);
	mobilemou_xiahoudun->addSkill(new MobileMouTongQingjian);

	General *mobilemou_xiahouyuan = new General(this, "mobilemou_xiahouyuan", "wei", 4);
	mobilemou_xiahouyuan->addSkill(new MobileMouShensu);
	mobilemou_xiahouyuan->addSkill(new MobileMouZhengzi);
	addMetaObject<MobileMouShensuCard>();

	addMetaObject<MobileMouGangLieCard>();
}
ADD_PACKAGE(MobileMouTong)
