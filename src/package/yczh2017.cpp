#include "yczh2017.h"
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
#include "yjcm2013.h"

class Jianzheng : public TriggerSkill
{
public:
	Jianzheng() : TriggerSkill("jianzheng")
	{
		events << TargetSpecifying;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash")) return false;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (!p->hasSkill(objectName())) continue;
			if (use.to.isEmpty() || use.to.contains(p) || !use.from->inMyAttackRange(p) || p->isKongcheng()) continue;
			const Card *card = room->askForCard(p, ".|.|.|hand", "jianzheng-put", data, Card::MethodNone);
			if (!card) continue;

			LogMessage log;
			log.type = "#InvokeSkill";
			log.from = p;
			log.arg = objectName();
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(p, objectName());

			CardMoveReason reason(CardMoveReason::S_REASON_PUT, p->objectName(), "jianzheng", "");
			room->moveCardTo(card, nullptr, Player::DrawPile, reason, false);
			use.to = QList<ServerPlayer *>();
			if (!use.card->isBlack())
				use.to << p;
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class Zhuandui : public TriggerSkill
{
public:
	Zhuandui() : TriggerSkill("zhuandui")
	{
		events << TargetSpecified << TargetConfirmed;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash")) return false;
		if (event == TargetSpecified) {
			QVariantList jink_list = player->tag["Jink_" + use.card->toString()].toList();
			int index = 0;
			foreach (ServerPlayer *p, use.to) {
				if (!player->isAlive()) break;
				if (player->canPindian(p) && player->askForSkillInvoke(this, p)) {
					room->broadcastSkillInvoke(objectName());
					if (player->pindian(p, objectName())) {
						LogMessage log;
						log.type = "#NoJink";
						log.from = p;
						room->sendLog(log);
						jink_list.replace(index, QVariant(0));
					}
				}
				index++;
			}
			player->tag["Jink_" + use.card->toString()] = QVariant::fromValue(jink_list);
		} else {
			if (use.from&&use.from->isAlive() && use.to.contains(player) && player->canPindian(use.from)
				&& player->askForSkillInvoke(this, use.from)) {
				room->broadcastSkillInvoke(objectName());
				if (player->pindian(use.from, objectName())) {
					use.nullified_list << player->objectName();
					data = QVariant::fromValue(use);
				}
			}
		}
		return false;
	}
};

class Tianbian : public TriggerSkill
{
public:
	Tianbian() : TriggerSkill("tianbian")
	{
		events << AskforPindianCard << PindianVerifying;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		PindianStruct *pindian = data.value<PindianStruct *>();
		if (event == AskforPindianCard) {
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->isDead()) continue;
				if (pindian->from != p && pindian->to != p) continue;
				if (!p->hasSkill(objectName())) continue;
				if (pindian->from == p) {
					if (pindian->from_card) continue;
					if (!p->askForSkillInvoke(this, data)) continue;
					room->broadcastSkillInvoke(objectName());
					pindian->from_card = Sanguosha->getCard(room->drawCard());
				} else if (pindian->to == p) {
					if (pindian->to_card) continue;
					if (!p->askForSkillInvoke(this, data)) continue;
					room->broadcastSkillInvoke(objectName());
					pindian->to_card = Sanguosha->getCard(room->drawCard());
				}
			}
		} else {
			QList<ServerPlayer *> pindian_players;
			pindian_players << pindian->from << pindian->to;
			room->sortByActionOrder(pindian_players);

			foreach (ServerPlayer *p, pindian_players) {
				if (p->isDead()) continue;
				LogMessage log;
				log.type = "#TianbianK";
				log.from = p;
				log.arg = objectName();
				if (p->hasSkill(objectName()) && pindian->from_card->getSuit() == Card::Heart && p == pindian->from) {
					room->sendLog(log);
					room->broadcastSkillInvoke(objectName());
					room->notifySkillInvoked(p, objectName());
					pindian->from_number = 13;
					data = QVariant::fromValue(pindian);
				} else if (p->hasSkill(objectName()) && pindian->to_card->getSuit() == Card::Heart && p == pindian->to) {
					room->sendLog(log);
					room->broadcastSkillInvoke(objectName());
					room->notifySkillInvoked(p, objectName());
					pindian->to_number = 13;
					data = QVariant::fromValue(pindian);
				}
			}
		}
		return false;
	}
};

FumianCard::FumianCard()
{
	mute = true;
}

bool FumianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length() < Self->getMark("fumian_extra_target-Clear") && to_select->hasFlag("fumian_canchoose");
}

void FumianCard::onUse(Room *room, CardUseStruct &card_use) const
{
	foreach (ServerPlayer *p, card_use.to)
		room->setPlayerFlag(p, "fumian_extratarget");
}

class FumianVS : public ZeroCardViewAsSkill
{
public:
	FumianVS() : ZeroCardViewAsSkill("fumian")
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
		if (pattern=="@@fumian1")
			return new ExtraCollateralCard;
		return new FumianCard;
	}
};

class Fumian : public TriggerSkill
{
public:
	Fumian() : TriggerSkill("fumian")
	{
		events << EventPhaseStart << DrawNCards << PreCardUsed << PreCardResponded;
		view_as_skill = new FumianVS;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Start) return false;
			if (!player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			int n = 1;
			QString last_choice = player->property("fumian_choice").toString();
			QString choice = room->askForChoice(player, objectName(), "draw+target");
			LogMessage log;
			log.type = last_choice != nullptr ? "#FumianChoice" : "#FumianFirstChoice";
			log.from = player;
			log.arg = "fumian:" + choice;
			log.arg2 = (last_choice != nullptr && choice == last_choice) ? "fumiansame" : "fumiandifferent";
			room->sendLog(log);
			if (last_choice != nullptr && choice != last_choice) n = 2;
			if (choice == "draw")
				room->addPlayerMark(player, "fumian_draw-Clear", n);
			else
				room->addPlayerMark(player, "fumian_target-Clear", n);
			room->setPlayerProperty(player, "fumian_choice", choice);
		} else if (event == PreCardUsed) {
			int n = player->getMark("fumian_target-Clear");
			if (n <= 0) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isRed() || use.card->isKindOf("SkillCard") || player->getPhase() == Player::NotActive) return false;
			room->setPlayerMark(player, "fumian_target-Clear", 0);
			room->setPlayerMark(player, "fumian_extra_target-Clear", n);
			if (!use.card->isKindOf("BasicCard") && !use.card->isNDTrick()) return false;
			if (use.card->isKindOf("Nullification")) return false;
			room->setCardFlag(use.card, "fumian_distance");
			if (use.card->isKindOf("Collateral")) {
				for (int i = 1; i <= n; i++) {
					bool canextra = false;
					foreach (ServerPlayer *p, room->getAlivePlayers()) {
						if (use.to.contains(p)) continue;
						int x = 0;
						if (use.card->targetFilter(QList<const Player *>(), p, player, x)||x>0) {
							canextra = true;
							break;
						}
					}
					if (!canextra) break;
					QStringList tos;
					tos << use.card->toString();
					foreach (ServerPlayer *t, use.to)
						tos << t->objectName();
					tos << objectName();
					room->setPlayerProperty(player, "extra_collateral", tos.join("+"));
					player->tag["FuMianUse"] = data;
					if (!room->askForUseCard(player, "@@fumian1", "@fumian:" + use.card->objectName())) break;
					ServerPlayer *p = player->tag["ExtraCollateralTarget"].value<ServerPlayer *>();
					player->tag.remove("ExtraCollateralTarget");
					if (p) {
						use.to.append(p);
						room->sortByActionOrder(use.to);
						data = QVariant::fromValue(use);
					}
				}
				room->setCardFlag(use.card, "-fumian_distance");
			} else {
				bool canextra = false;
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if (use.to.contains(p)) continue;
					if (player->canUse(use.card,p)) {
						room->setPlayerFlag(p, "fumian_canchoose");
						canextra = true;
					}
				}
				room->setCardFlag(use.card, "-fumian_distance");
				if (!canextra) return false;
				player->tag["FuMianUse"] = data;
				if (!room->askForUseCard(player, "@@fumian", "@fumian:" + use.card->objectName())) return false;
				LogMessage log;
				foreach(ServerPlayer *p, room->getAlivePlayers()) {
					room->setPlayerFlag(p, "-fumian_canchoose");
					if (p->hasFlag("fumian_extratarget")) {
						room->setPlayerFlag(p,"-fumian_extratarget");
						log.to << p;
					}
				}
				if (log.to.isEmpty()) return false;
				log.type = "#QiaoshuiAdd";
				log.from = player;
				log.card_str = use.card->toString();
				log.arg = "fumian";
				room->sendLog(log);
				use.to << log.to;

				room->sortByActionOrder(use.to);
				data = QVariant::fromValue(use);
			}
		} else if (event == DrawNCards) {
			DrawStruct draw = data.value<DrawStruct>();
			if (draw.reason!="draw_phase") return false;
			int n = player->getMark("fumian_draw-Clear");
			if (n <= 0) return false;
			draw.num += n;
			data = QVariant::fromValue(draw);
		} else {
			int n = player->getMark("fumian_target-Clear");
			if (n <= 0) return false;
			CardResponseStruct res = data.value<CardResponseStruct>();
			if (!res.m_isUse) return false;
			if (!res.m_card->isRed() || res.m_card->isKindOf("SkillCard") || player->getPhase() == Player::NotActive) return false;
			room->setPlayerMark(player, "fumian_target-Clear", 0);
		}
		return false;
	}
};

class FumianTargetMod : public TargetModSkill
{
public:
	FumianTargetMod() : TargetModSkill("#fumian-target")
	{
		frequency = NotFrequent;
		pattern = ".";
	}

	int getDistanceLimit(const Player *, const Card *card, const Player *) const
	{
		if (card->hasFlag("fumian_distance") && card->isRed())
			return 1000;
		return 0;
	}
};

class Daiyan : public TriggerSkill
{
public:
	Daiyan() : TriggerSkill("daiyan")
	{
		events << EventPhaseStart << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->isDead() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@daiyan-invoke", true, true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());
			room->addPlayerMark(player, "daiyan-Clear");

			QList<ServerPlayer *> last_targets;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->getMark("&daiyan+#" + player->objectName()) > 0) {
					last_targets << p;
					room->setPlayerMark(p, "&daiyan+#" + player->objectName(), 0);
				}
			}
			room->addPlayerMark(target, "&daiyan+#" + player->objectName());

			QList<int> list;
			foreach (int id, room->getDrawPile()) {
				const Card *card = Sanguosha->getCard(id);
				if (card->getSuit() == Card::Heart && card->isKindOf("BasicCard"))
					list << id;
			}
			if (!list.isEmpty()) {
				int id = list.at(qrand() % list.length());
				room->obtainCard(target, id, true);
			}
			if (last_targets.isEmpty() || !last_targets.contains(target) || target->isDead()) return false;
			room->loseHp(HpLostStruct(target, 1, "daiyan", player));
		} else {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			if (player->getMark("daiyan-Clear") > 0) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->getMark("&daiyan+#" + player->objectName()) > 0)
					room->setPlayerMark(p, "&daiyan+#" + player->objectName(), 0);
			}
		}
		return false;
	}
};

ZhongjianCard::ZhongjianCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool ZhongjianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && to_select->getHandcardNum() > to_select->getHp();
}

void ZhongjianCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	room->showCard(effect.from, getEffectiveId());
	int n = effect.to->getHandcardNum() - effect.to->getHp();
	if (n <= 0) return;
	QList<int> list;
	QStringList slist;
	for (int i = 0; i < n; i++) {
		int id = room->askForCardChosen(effect.from,effect.to,"h","zhongjian",false,Card::MethodNone,list);
		list << id;
		slist << Sanguosha->getCard(id)->toString();
	}/*
	LogMessage log;
	log.type = "$ZhongjianShow";
	log.from = effect.from;
	log.to << effect.to;
	log.arg = QString::number(n);
	log.card_str = slist.join("+");
	room->sendLog(log);
	room->fillAG(list);
	room->getThread()->delay(2000);
	room->clearAG();*/
	room->showCard(effect.to, list);
	room->getThread()->delay();
	bool samecolour = false, samenumber = false;
	foreach (int id, list) {
		if (sameColorWith(Sanguosha->getCard(id)))
			samecolour = true;
		if (getNumber() == Sanguosha->getCard(id)->getNumber())
			samenumber = true;
	}
	LogMessage newlog;
	if (!samecolour && !samenumber) {
		newlog.type = "#ZhongjianResult_NoSame";
		room->sendLog(newlog);
		effect.from->addMark("zhongjian_debuff");
		room->addMaxCards(effect.from,1,false);
		return;
	}
	newlog.type = "#ZhongjianResult";
	newlog.arg = samecolour == true ? "zhongjiansamecolour" : "zhongjiandifferentcolour";
	newlog.arg2 = samenumber == true ? "zhongjiansamenumber" : "zhongjiandifferentnumber";
	room->sendLog(newlog);
	if (samecolour) {
		QStringList choices;
		choices << "draw";
		if (effect.from->canDiscard(effect.to, "he"))
			choices << "discard";
		if (room->askForChoice(effect.from, "zhongjian", choices.join("+")) == "draw")
			effect.from->drawCards(1, "zhongjian");
		else {
			int card_id = room->askForCardChosen(effect.from, effect.to, "he", "zhongjian", false, Card::MethodDiscard);
			room->throwCard(card_id, effect.to, effect.from);
		}
	}
	if (samenumber)
		room->setPlayerMark(effect.from, "zhongjian-PlayClear",1);
}

class ZhongjianVS : public OneCardViewAsSkill
{
public:
	ZhongjianVS() : OneCardViewAsSkill("zhongjian")
	{
		filter_pattern = ".|.|.|hand";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("ZhongjianCard") <= player->getMark("zhongjian-PlayClear");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		ZhongjianCard *card = new ZhongjianCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class Zhongjian : public TriggerSkill
{
public:
	Zhongjian() : TriggerSkill("zhongjian")
	{
		events << EventLoseSkill;
		view_as_skill = new ZhongjianVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventLoseSkill) {
			if (data.toString() != objectName()) return false;
			room->addMaxCards(player, player->getMark("zhongjian_debuff"), false);
			player->setMark("zhongjian_debuff",0);
		}
		return false;
	}
};

class Caishi : public TriggerSkill
{
public:
	Caishi() : TriggerSkill("caishi")
	{
		events << Death << EventLoseSkill << EventPhaseStart;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->isDead() || !player->hasSkill(objectName()) || player->getPhase() != Player::Draw) return false;
			if (!player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			QStringList choices;
			choices << "max";
			if (player->getLostHp() > 0) choices << "recover";
			QString choice = room->askForChoice(player, "caishi", choices.join("+"));
			LogMessage log;
			log.type ="#FumianFirstChoice";
			log.from = player;
			log.arg = "caishi:" + choice;
			room->sendLog(log);
			if (choice == "max") {
				room->addPlayerMark(player, "caishi_buff");
				room->addPlayerMark(player, "caishi_others-Clear");
			} else {
				room->recover(player, RecoverStruct("caishi", player));
				room->addPlayerMark(player, "caishi_self-Clear");
			}
		} else if (event == EventLoseSkill) {
			if (data.toString() != objectName() || player->isDead()) return false;
			room->setPlayerMark(player, "caishi_buff", 0);
		} else {
			DeathStruct death = data.value<DeathStruct>();
			if (death.who != player || !player->hasSkill(objectName())) return false;
			room->setPlayerMark(player, "caishi_buff", 0);
		}
		return false;
	}
};

class CaishiMax : public MaxCardsSkill
{
public:
	CaishiMax() : MaxCardsSkill("#caishimax")
	{
		frequency = NotFrequent;
	}

	int getExtra(const Player *target) const
	{
		if (target->hasSkill("caishi"))
			return target->getMark("caishi_buff");
		return 0;
	}
};

class CaishiPro : public ProhibitSkill
{
public:
	CaishiPro() : ProhibitSkill("#caishipro")
	{
		frequency = NotFrequent;
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		if (from->getMark("caishi_others-Clear") > 0 && !card->isKindOf("SkillCard"))
			return from != to;
		if (from->getMark("caishi_self-Clear") > 0 && !card->isKindOf("SkillCard"))
			return from == to;
		return false;
	}
};

class Qingxian : public TriggerSkill
{
public:
	Qingxian() : TriggerSkill("qingxian")
	{
		events << Damaged << HpRecover;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getHp() <= 0)
				return false;
		}
		ServerPlayer *target = nullptr;
		if (event == Damaged) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.from || damage.from->isDead()) return false;
			if (!player->askForSkillInvoke(this, QVariant::fromValue(damage.from))) return false;
			target = damage.from;
		} else {
			ServerPlayer *sp = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@qingxian-invoke", true, true);
			if (!sp) return false;
			target = sp;
		}
		if (target) {
			room->broadcastSkillInvoke(objectName());
			QStringList choices;
			choices << "losehp";
			if (target->getLostHp() > 0) choices << "recover";
			if (room->askForChoice(player, "qingxian", choices.join("+"))== "losehp") {
				room->loseHp(HpLostStruct(target, 1, "qingxian", player));
				if (target->isDead()) return false;
				QList<int> equiplist;
				foreach (int id, room->getDrawPile()) {
					const Card* card = Sanguosha->getCard(id);
					if (card->isKindOf("EquipCard") && card->isAvailable(target) && !target->isProhibited(target, card))
						equiplist << id;
				}
				if (equiplist.isEmpty()) return false;
				const Card* equip = Sanguosha->getCard(equiplist.at(qrand() % equiplist.length()));
				if (!equip->isAvailable(target) || target->isProhibited(target, equip)) return false;
				room->useCard(CardUseStruct(equip, target, target), true);
				if (equip->getSuit() != Card::Club) return false;
				player->drawCards(1, objectName());
			} else {
				room->recover(target, RecoverStruct("qingxian", player));
				QList<const Card *> equipcards;
				foreach (const Card *card, target->getCards("he")) {
					if (card->isKindOf("EquipCard"))
						equipcards << card;
				}
				if (equipcards.isEmpty()) {
					room->showAllCards(target);
					return false;
				}
				const Card *card = room->askForDiscard(target, objectName(), 1, 1, false, true, "qingxian-discard", "EquipCard");
				if (!card) {
					card = equipcards.at(qrand() % equipcards.length());
					room->throwCard(card, target, nullptr);
				} else
					card = Sanguosha->getCard(card->getSubcards().first());

				if (card->getSuit() != Card::Club) return false;
				player->drawCards(1, objectName());
			}
		}
		return false;
	}
};

class Juexiang : public TriggerSkill
{
public:
	Juexiang() : TriggerSkill("juexiang")
	{
		events << Death << EventPhaseChanging;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == Death) {
			DeathStruct death = data.value<DeathStruct>();
			if (death.who->getMark("juexiang_buff") > 0)
				room->setPlayerMark(player, "juexiang_buff", 0);
			if (death.who != player || !player->hasSkill(objectName())) return false;
			ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@juexiang-invoke", true, true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());
			QStringList skills;
			if (!target->hasSkill("jixian")) skills << "jixian";
			if (!target->hasSkill("liexian")) skills << "liexian";
			if (!target->hasSkill("rouxian")) skills << "rouxian";
			if (!target->hasSkill("hexian")) skills << "hexian";
			if (skills.isEmpty()) return false;
			QString skill_name = skills.at(qrand() % skills.length());
			room->handleAcquireDetachSkills(target, skill_name);
			room->addPlayerMark(target, "juexiang_buff");
		} else {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::RoundStart) return false;
			if (player->isAlive() && player->getMark("juexiang_buff") <= 0) return false;
			room->setPlayerMark(player, "juexiang_buff", 0);
		}
		return false;
	}
};

class JuexiangPro : public ProhibitSkill
{
public:
	JuexiangPro() : ProhibitSkill("#juexiangpro")
	{
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		return to && from != to && to->getMark("juexiang_buff") > 0 && card->getSuit() == Card::Club;
	}
};

class Jixian : public TriggerSkill
{
public:
	Jixian() : TriggerSkill("jixian")
	{
		events << Damaged;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getHp() <= 0)
				return false;
		}
		DamageStruct damage = data.value<DamageStruct>();
		if (!damage.from || damage.from->isDead()) return false;
		if (!player->askForSkillInvoke(this, QVariant::fromValue(damage.from))) return false;
		room->broadcastSkillInvoke(objectName());
		room->loseHp(HpLostStruct(damage.from, 1, "jixian", player));
		if (damage.from->isDead()) return false;
		QList<int> equiplist;
		foreach (int id, room->getDrawPile()) {
			const Card* card = Sanguosha->getCard(id);
			if (card->isKindOf("EquipCard") && card->isAvailable(damage.from) && !damage.from->isProhibited(damage.from, card))
				equiplist << id;
		}
		if (equiplist.isEmpty()) return false;
		const Card* equip = Sanguosha->getCard(equiplist.at(qrand() % equiplist.length()));
		if (!equip->isAvailable(damage.from) || damage.from->isProhibited(damage.from, equip)) return false;
		room->useCard(CardUseStruct(equip, damage.from, damage.from), true);
		return false;
	}
};

class Liexian : public TriggerSkill
{
public:
	Liexian() : TriggerSkill("liexian")
	{
		events << HpRecover;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getHp() <= 0)
				return false;
		}
		ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@liexian-invoke", true, true);
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());
		room->loseHp(HpLostStruct(target, 1, objectName(), player));
		if (target->isDead()) return false;
		QList<int> equiplist;
		foreach (int id, room->getDrawPile()) {
			const Card* card = Sanguosha->getCard(id);
			if (card->isKindOf("EquipCard") && card->isAvailable(target) && !target->isProhibited(target, card))
				equiplist << id;
		}
		if (equiplist.isEmpty()) return false;
		const Card* equip = Sanguosha->getCard(equiplist.at(qrand() % equiplist.length()));
		if (!equip->isAvailable(target) || target->isProhibited(target, equip)) return false;
		room->useCard(CardUseStruct(equip, target, target), true);
		return false;
	}
};

class Rouxian : public TriggerSkill
{
public:
	Rouxian() : TriggerSkill("rouxian")
	{
		events << Damaged;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getHp() <= 0)
				return false;
		}
		DamageStruct damage = data.value<DamageStruct>();
		if (!damage.from || damage.from->isDead()) return false;
		if (!player->askForSkillInvoke(this, QVariant::fromValue(damage.from))) return false;
		room->broadcastSkillInvoke(objectName());
		room->recover(damage.from, RecoverStruct("rouxian", player));
		QList<const Card *> equipcards;
		foreach (const Card *card, damage.from->getCards("he")) {
			if (card->isKindOf("EquipCard"))
				equipcards << card;
		}
		if (equipcards.isEmpty()) {
			room->showAllCards(damage.from);
			return false;
		}
		const Card *card = room->askForCard(damage.from, "EquipCard!", "qingxian-discard"); //rouxian-discard
		if (!card) {
			card = equipcards.at(qrand() % equipcards.length());
			room->throwCard(card, damage.from, nullptr);
		}
		return false;
	}
};

class Hexian : public TriggerSkill
{
public:
	Hexian() : TriggerSkill("hexian")
	{
		events << HpRecover;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getHp() <= 0)
				return false;
		}
		ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@hexian-invoke", true, true);
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());
		room->recover(target, RecoverStruct("hexian", player));
		QList<const Card *> equipcards;
		foreach (const Card *card, target->getCards("he")) {
			if (card->isKindOf("EquipCard"))
				equipcards << card;
		}
		if (equipcards.isEmpty()) {
			room->showAllCards(target);
			return false;
		}
		const Card *card = room->askForCard(target, "EquipCard!", "qingxian-discard"); //hexian-discard
		if (!card) {
			card = equipcards.at(qrand() % equipcards.length());
			room->throwCard(card, target, nullptr);
		}
		return false;
	}
};

WenguagiveCard::WenguagiveCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
	mute = true;
}

bool WenguagiveCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select->hasSkill("wengua") && to_select->getMark("wengua-PlayClear") <= 0 && Self != to_select;
}

void WenguagiveCard::onUse(Room *room, CardUseStruct &card_use) const
{
	QVariant data = QVariant::fromValue(card_use);
	room->getThread()->trigger(PreCardUsed, room, card_use.from, data);
	room->getThread()->trigger(CardUsed, room, card_use.from, data);
	card_use = data.value<CardUseStruct>();
	foreach (ServerPlayer *p, card_use.to){
		card_use.from->skillInvoked("wengua",-1,p);
		if (card_use.from != p) {
			CardMoveReason reason(CardMoveReason::S_REASON_GIVE, card_use.from->objectName(), p->objectName(), "wengua", "");
			room->obtainCard(p, this, reason, false);
		}
	
		if (!p->hasCard(subcards.first())) continue;
	
		QStringList list;
		list << card_use.from->objectName() << QString::number(subcards.first());
		QString choice = room->askForChoice(p, "wengua", "top+bottom+cancel", list);
		if (choice == "cancel") continue;
		QList<ServerPlayer *> sp;
		sp << card_use.from;
		if (!sp.contains(p))
			sp << p;
		room->sortByActionOrder(sp);
		if (choice == "top") {
			CardMoveReason reason(CardMoveReason::S_REASON_PUT, p->objectName(), "wengua", "");
			room->moveCardTo(this, nullptr, Player::DrawPile, reason);
			room->drawCards(sp, 1, "wengua", false);
		} else {
			room->moveCardsToEndOfDrawpile(card_use.from, subcards, "wengua", false);
			room->drawCards(sp, 1, "wengua");
		}
	}
	room->getThread()->trigger(CardFinished, room, card_use.from, data);
	card_use = data.value<CardUseStruct>();
}

class Wenguagive : public OneCardViewAsSkill
{
public:
	Wenguagive() : OneCardViewAsSkill("wenguagive&")
	{
		filter_pattern = ".";
	}

	const Card *viewAs(const Card *originalCard) const
	{
		WenguagiveCard *card = new WenguagiveCard;
		card->addSubcard(originalCard);
		return card;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		foreach (const Player *p, player->getAliveSiblings(true)) {
			if (p->hasSkill("wengua") && player->usedTimes("WenguagiveCard") <= 0)
				return true;
		}
		return false;
	}
};

WenguaCard::WenguaCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void WenguaCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	int id = this->getSubcards().first();
	QStringList list;
	list << source->objectName() << QString::number(id);
	QString choice = room->askForChoice(source, "wengua", "top+bottom", list);
	if (choice == "top") {
		CardMoveReason reason(CardMoveReason::S_REASON_PUT, source->objectName(), "wengua", "");
		room->moveCardTo(Sanguosha->getCard(id), nullptr, Player::DrawPile, reason);
		room->drawCards(source, 1, "wengua", false);
	} else {
		QList<int> card_ids;
		card_ids << id;
		room->moveCardsToEndOfDrawpile(source, card_ids, "wengua", false);
		room->drawCards(source, 1, "wengua");
	}
}

class WenguaVS : public OneCardViewAsSkill
{
public:
	WenguaVS() : OneCardViewAsSkill("wengua")
	{
		filter_pattern = ".";
	}

	const Card *viewAs(const Card *originalCard) const
	{
		WenguaCard *card = new WenguaCard;
		card->addSubcard(originalCard);
		return card;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("WenguaCard");
	}
};

class Wengua : public TriggerSkill
{
public:
	Wengua() : TriggerSkill("wengua")
	{
		view_as_skill = new WenguaVS;
		events << EventPhaseStart << EventPhaseEnd << EventAcquireSkill;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (triggerEvent == EventAcquireSkill&&player->hasSkill(objectName(),true)) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getPhase()==Player::Play&&!p->hasSkill("wenguagive",true)){
					room->attachSkillToPlayer(p, "wenguagive");
				}
			}
		}else if(player->getPhase()!=Player::Play)
			return false;
		if (triggerEvent == EventPhaseStart) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->hasSkill(objectName(),true)){
					room->attachSkillToPlayer(player, "wenguagive");
					break;
				}
			}
		}else{
			if (player->hasSkill("wenguagive",true))
				room->detachSkillFromPlayer(player, "wenguagive", true);
		}
		return false;
	}
};

class Fuzhu : public PhaseChangeSkill
{
public:
	Fuzhu() : PhaseChangeSkill("fuzhu")
	{
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (player->getPhase() != Player::Finish || !player->isMale() || player->isDead()) break;
			if (room->getDrawPile().isEmpty()) break;
			if (p->isDead() || !p->hasSkill(objectName()) || !p->canSlash(player, nullptr, false)) continue;
			if (room->getDrawPile().length() > 10 * p->getHp()) continue;
			Slash *slash = new Slash(Card::NoSuit, 0);
			slash->deleteLater();
			if (p->isLocked(slash)) continue;
			if (!p->askForSkillInvoke(this, QVariant::fromValue(player))) continue;
			room->broadcastSkillInvoke(objectName());
			int i = 0;
			foreach (int id, room->getDrawPile()) {
				if (p->isDead() || player->isDead()) break;
				const Card *card = Sanguosha->getCard(id);
				if (!card->isKindOf("Slash")) continue;
				if (!p->isLocked(card) && p->canSlash(player, card, false)) {
					room->useCard(CardUseStruct(card, p, player));
					i++;
					if (i == room->getAllPlayers(true).length()) break;
				}
			}
			room->swapPile();
		}
		return false;
	}
};

class Funan : public TriggerSkill
{
public:
	Funan() : TriggerSkill("funan")
	{
		events << CardResponded << CardUsed;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		const Card *card = nullptr;
		const Card *tocard = nullptr;
		ServerPlayer *who;
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			card = use.card;
			tocard = use.whocard;
			who = use.who;
		} else {
			CardResponseStruct res = data.value<CardResponseStruct>();
			if (res.m_isRetrial) return false;  //不加改判会崩，不知为啥
			card = res.m_card;
			tocard = res.m_toCard;
			who = res.m_who;
		}
		if (card == nullptr || tocard == nullptr || card->isKindOf("SkillCard") || tocard->isKindOf("SkillCard")) return false;
		if (!who || who->isDead() || who == player || !who->hasSkill(objectName())) return false;

		if (room->getCardUser(tocard) != who) return false;

		who->tag["FunanCard"] = QVariant::fromValue(tocard);

		if (who->property("funan_level_up").toBool()) {
			if (room->getCardOwner(card->getEffectiveId())) return false;
			if (!who->askForSkillInvoke(objectName()+"$-1", player)) return false;
		}else{
			if (room->getCardOwner(tocard->getEffectiveId())) return false;
			if (!who->askForSkillInvoke(objectName()+"$-1", player)) return false;
			QStringList list;
			if (tocard->isVirtualCard()) {
				foreach (int id, tocard->getSubcards()) {
					list << Sanguosha->getCard(id)->toString();
					room->setPlayerCardLimitation(player, "use,response", list.last(), true);
				}
			} else {
				list << tocard->toString();
				room->setPlayerCardLimitation(player, "use,response", tocard->toString(), true);
			}
			player->setProperty("funan_limitlist", list);
			player->obtainCard(tocard, true);
			if (room->getCardOwner(card->getEffectiveId())||who->isDead()) return false;
		}

		who->obtainCard(card);
		return false;
	}
};

class FunanRemove : public TriggerSkill
{
public:
	FunanRemove() : TriggerSkill("#funanremove")
	{
		events << EventPhaseChanging << Death;
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
		} else {
			DeathStruct death = data.value<DeathStruct>();
			if (death.who != player || player != room->getCurrent()) return false;
		}
		foreach(ServerPlayer *p, room->getPlayers()) {
			foreach (QString str, p->property("funan_limitlist").toStringList())
				room->removePlayerCardLimitation(p, "use,response", str + "$1");
			p->setProperty("funan_limitlist", QVariant());
		}
		return false;
	}
};

class Jiexun : public TriggerSkill
{
public:
	Jiexun() : TriggerSkill("jiexun")
	{
		events << EventPhaseStart;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Finish) return false;
			int num = 0;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				foreach (const Card *c, p->getCards("ej")) {
					if (c->getSuit() == Card::Diamond)
						num++;
				}
			}
			ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName()+"$-1", "@jiexun-invoke:" + QString::number(num), true, true);
			if (!target) return false;
			int n = player->getMark("&jiexun-Keep");
			room->addPlayerMark(player, "&jiexun-Keep");
			if (num > 0) target->drawCards(num, objectName());
			if (n > 0){
				num = target->getCardCount();
				Card *dc = room->askForDiscard(target, objectName(), n, n, false, true);
				if(dc&&dc->subcardsLength()>=num) n = -1;
			}
			if (n!=-1) return false;
			room->detachSkillFromPlayer(player, "jiexun");
			if (player->hasSkill("funan", true)) {
				LogMessage log;
				log.type = "#JiexunChange";
				log.from = player;
				log.arg = "funan";
				room->sendLog(log);
			}
			room->setPlayerMark(player, "&funan", 1);
			player->setProperty("funan_level_up", true);
			room->changeTranslation(player,"funan", 2);
		}
		return false;
	}
};

class Shouxi : public TriggerSkill
{
public:
	Shouxi() : TriggerSkill("shouxi")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash") || !use.to.contains(player)) return false;
		QStringList alllist;
		QList<int> ids;
		QStringList list = player->property("shouxi_names").toStringList();
		foreach(int id, Sanguosha->getRandomCards()) {
			const Card *c = Sanguosha->getEngineCard(id);
			if (c->isKindOf("EquipCard")) continue;
			if (alllist.contains(c->objectName()) || list.contains(c->objectName())) continue;
			alllist << c->objectName();
			ids << id;
		}
		if (alllist.isEmpty() || !player->askForSkillInvoke(this, data)) return false;
		room->broadcastSkillInvoke(objectName());
		room->fillAG(ids, player);
		int id = room->askForAG(player, ids, false, objectName());
		room->clearAG(player);

		const Card *c = Sanguosha->getEngineCard(id);
		QString name = c->objectName();
		QString classname = c->getClassName();
		if (!list.contains(name)) {
			list << name;
			room->setPlayerProperty(player, "shouxi_names", list);
		}
		LogMessage log;
		log.type = "#ShouxiChoice";
		log.from = player;
		log.arg = name;
		room->sendLog(log);
		if (use.from->isDead() || !use.from->canDiscard(use.from, "h") || !room->askForCard(use.from, classname, "shouxi-discard:" + name, data)) {
			use.nullified_list << player->objectName();
			data = QVariant::fromValue(use);
		} else {
			if (player->isNude()) return false;
			int card_id = room->askForCardChosen(use.from, player, "he", "shouxi");
			CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, use.from->objectName());
			room->obtainCard(use.from, Sanguosha->getCard(card_id),
				reason, room->getCardPlace(card_id) != Player::PlaceHand);
		}
		return false;
	}
};

HuiminCard::HuiminCard()
{
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool HuiminCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
	return targets.isEmpty() && to_select->hasFlag("huimin_target");
}

void HuiminCard::onUse(Room *room, CardUseStruct &card_use) const
{
	foreach (ServerPlayer *p, card_use.to)
		room->setPlayerFlag(p, "huimin_start_target");
}

class HuiminVS : public ViewAsSkill
{
public:
	HuiminVS() : ViewAsSkill("huimin")
	{
		response_pattern = "@@huimin!";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		return !to_select->isEquipped() && selected.length() < Self->getMark("huimin_length");
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.length() != Self->getMark("huimin_length")) return nullptr;

		Card *acard = new HuiminCard;
		acard->addSubcards(cards);
		return acard;
	}
};

class Huimin : public PhaseChangeSkill
{
public:
	Huimin() : PhaseChangeSkill("huimin")
	{
		view_as_skill = new HuiminVS;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Finish) return false;
		QList<ServerPlayer *> sp;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getHandcardNum() < p->getHp())
				sp << p;
		}
		if (sp.isEmpty()) return false;
		int length = sp.length();
		if (!player->askForSkillInvoke(this, QString("huimin_invoke:%1").arg(length))) return false;
		room->broadcastSkillInvoke(objectName());
		player->drawCards(length, objectName());
		QStringList cardlist;
		QList<int> ids;
		ServerPlayer *start_player = nullptr;
		if (player->getHandcardNum() <= length) {
			foreach (int id, player->handCards()) {
				cardlist << Sanguosha->getCard(id)->toString();
				ids << id;
			}
		} else {
			foreach (ServerPlayer *p, sp)
				room->setPlayerFlag(p, "huimin_target");
			room->setPlayerMark(player, "huimin_length", length);
			const Card *card = room->askForUseCard(player, "@@huimin!", "@huimin:" + QString::number(length));
			room->setPlayerMark(player, "huimin_length", 0);
			if (!card) {
				foreach (ServerPlayer *p, sp) {
					if (p->hasFlag("huimin_target"))
						room->setPlayerFlag(p, "-huimin_target");
				}
				start_player = sp.at(qrand() % length);
				QList<int> handids;
				foreach (int id, player->handCards())
					handids << id;
				for (int i = 1; i <= length; i++) {
					int id = handids.at(qrand() % handids.length());
					handids.removeOne(id);
					ids << id;
					cardlist << Sanguosha->getCard(id)->toString();
					if (handids.isEmpty()) break;
				}
			} else {
				foreach (ServerPlayer *p, sp) {
					if (p->hasFlag("huimin_target"))
						room->setPlayerFlag(p, "-huimin_target");
					if (p->hasFlag("huimin_start_target")) {
						room->setPlayerFlag(p, "-huimin_start_target");
						start_player = p;
					}
				}
				ids = card->getSubcards();
				foreach (int id, ids)
					cardlist << Sanguosha->getCard(id)->toString();
			}
		}
		LogMessage log;
		log.type = "$ShowCard";
		log.from = player;
		log.card_str = cardlist.join("+");
		room->sendLog(log);
		room->fillAG(ids);
		if (start_player == nullptr)
			start_player = room->askForPlayerChosen(player, sp, objectName(), "@huimin-chooseplayer");
		LogMessage newlog;
		newlog.type = "#HuiminPlayer";
		newlog.from = player;
		newlog.to << start_player;
		room->sendLog(newlog);
		room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), start_player->objectName());
		while (!ids.isEmpty()) {
			int id = room->askForAG(start_player, ids, false, objectName());
			ids.removeOne(id);
			//room->takeAG(start_player, id, true); //牌会卡住
			if (start_player != player)
				room->obtainCard(start_player, id, true);
			if (ids.isEmpty()) break;
			start_player = start_player->getNextAlive();
			while (!sp.contains(start_player)) {
				start_player = start_player->getNextAlive();
			}
		}
		room->clearAG();
		return false;
	}
};

class Bizhuan : public TriggerSkill
{
public:
	Bizhuan() : TriggerSkill("bizhuan")
	{
		events << TargetConfirmed << CardUsed;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->getSuit() !=Card::Spade || use.card->isKindOf("SkillCard")) return false;
		if (event == TargetConfirmed && (use.from == player || !use.to.contains(player))) return false;
		if (player->getPile("book").length() >= 4) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		player->addToPile("book", room->drawCard());
		return false;
	}
};

class BizhuanKeep : public MaxCardsSkill
{
public:
	BizhuanKeep() : MaxCardsSkill("#bizhuankeep")
	{
		frequency = Frequent;
	}

	int getExtra(const Player *target) const
	{
		if (target->hasSkill("bizhuan"))
			return target->getPile("book").length();
		return 0;
	}
};

TongboCard::TongboCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
	target_fixed = true;
}

void TongboCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QList<int> pile = source->getPile("book");
	QList<int> to_handcard;
	QList<int> to_pile;
	foreach (int id, subcards) {
		if (pile.contains(id))
			to_handcard << id;
		else
			to_pile << id;
	}

	LogMessage log;
	log.type = "#QixingExchange";
	log.from = source;
	log.arg = QString::number(to_pile.length());
	log.arg2 = "tongbo";
	//room->sendLog(log);

	source->addToPile("book", to_pile, false);

	DummyCard to_handcard_x(to_handcard);
	CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, source->objectName());
	room->obtainCard(source, &to_handcard_x, reason, true);
	to_handcard_x.deleteLater();

	QStringList suitlist;
	pile = source->getPile("book");
	foreach (int id, pile) {
		QString str = Sanguosha->getCard(id)->getSuitString();
		if (!suitlist.contains(str)) suitlist << str;
	}
	if (suitlist.length() < 4) return;
	QList<ServerPlayer *> _player;
	_player.append(source);

	CardsMoveStruct move(pile, source, source, Player::PlaceTable, Player::PlaceHand,
		CardMoveReason(CardMoveReason::S_REASON_PUT, source->objectName(), "tongbo", ""));
	QList<CardsMoveStruct> moves;
	moves.append(move);
	room->notifyMoveCards(true, moves, false, _player);
	room->notifyMoveCards(false, moves, false, _player);

	QList<int> origin_ids = pile;
	while (source->isAlive()&&room->askForYiji(source, pile, "tongbo", false, true, false, -1, room->getOtherPlayers(source))) {
		CardsMoveStruct move(QList<int>(), source, source, Player::PlaceHand, Player::PlaceTable,
			CardMoveReason(CardMoveReason::S_REASON_PUT, source->objectName(), "tongbo", ""));
		foreach (int id, origin_ids) {
			if (!pile.contains(id))
				move.card_ids << id;
		}
		origin_ids = pile;
		QList<CardsMoveStruct> moves;
		moves.append(move);
		room->notifyMoveCards(true, moves, false, _player);
		room->notifyMoveCards(false, moves, false, _player);
	}
}

class TongboVS : public ViewAsSkill
{
public:
	TongboVS() : ViewAsSkill("tongbo")
	{
		response_pattern = "@@tongbo";
		expand_pile = "book";
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *) const
	{
		return selected.length() < 2 * Self->getPile("book").length();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		int notpile = 0;
		int pile = 0;
		foreach (const Card *card, cards) {
			if (Self->getPile("book").contains(card->getEffectiveId()))
				pile++;
			else
				notpile++;
		}

		if (notpile == pile) {
			TongboCard *c = new TongboCard;
			c->addSubcards(cards);
			return c;
		}

		return nullptr;
	}
};

class Tongbo : public TriggerSkill
{
public:
	Tongbo() : TriggerSkill("tongbo")
	{
		events << EventPhaseEnd;
		view_as_skill = new TongboVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (player->getPhase() != Player::Draw) return false;
		if (player->isKongcheng() || player->getPile("book").isEmpty()) return false;
		room->askForUseCard(player, "@@tongbo", "@tongbo");
		return false;
	}
};

MobileQingxianCard::MobileQingxianCard()
{
}

bool MobileQingxianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length() < getSubcards().length() && to_select != Self;
}

bool MobileQingxianCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == getSubcards().length();
}

void MobileQingxianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		if (source->isDead()) break;
		if (p->isDead()) continue;
		room->cardEffect(this, source, p);
	}
	if (targets.length() == source->getHp() && source->isAlive())
		source->drawCards(1, "mobileqingxian");
}

void MobileQingxianCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.from->isDead() || effect.to->isDead()) return;
	Room *room = effect.from->getRoom();
	if (effect.to->getEquips().length() > effect.from->getEquips().length())
		room->loseHp(HpLostStruct(effect.to, 1, "mobileqingxian", effect.from));
	else if (effect.to->getEquips().length() == effect.from->getEquips().length())
		effect.to->drawCards(1, "mobileqingxian");
	else
		room->recover(effect.to, RecoverStruct("mobileqingxian", effect.from));
}

class MobileQingxian : public ViewAsSkill
{
public:
	MobileQingxian() : ViewAsSkill("mobileqingxian")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		int n = qMin(Self->getHp(), Self->getAliveSiblings().length());
		return selected.length() < n && !Self->isJilei(to_select);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;

		MobileQingxianCard *c = new MobileQingxianCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->canDiscard(player, "he") && !player->hasUsed("MobileQingxianCard") && player->getHp() > 0;
	}
};

class MobileJuexiang : public TriggerSkill
{
public:
	MobileJuexiang() : TriggerSkill("mobilejuexiang")
	{
		events << Death;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->hasSkill(objectName());
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DeathStruct death = data.value<DeathStruct>();
		if (death.who != player)
			return false;

		if (death.damage && death.damage->from) {
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			death.damage->from->throwAllEquips();
			room->loseHp(HpLostStruct(death.damage->from, 1, "mobilejuexiang", player));
			ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@mobilejuexiang-invoke", true, true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());
			if (!target->hasSkill("mobilecanyun"))
				room->handleAcquireDetachSkills(target, "mobilecanyun");
			if (target->isDead()) return false;
			QList<ServerPlayer *> clubs;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				foreach (const Card *c, p->getCards("ej")) {
					if (c->getSuit() == Card::Club && target->canDiscard(p, c->getEffectiveId()))
						clubs << p;
				}
			}
			if (clubs.isEmpty()) return false;
			ServerPlayer *club = room->askForPlayerChosen(target, clubs, "mobilejuexiang_discard", "@mobilejuexiang-discard", true);
			if (!club) return false;
			room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, target->objectName(), club->objectName());
			QList<int> disabled_ids;
			foreach (const Card *c, club->getCards("ej")) {
				if (c->getSuit() != Card::Club || !target->canDiscard(club, c->getEffectiveId()))
					disabled_ids << c->getEffectiveId();
			}
			if (disabled_ids.length() == club->getCards("ej").length()) return false;
			int card_id = room->askForCardChosen(target, club, "ej", objectName(), false, Card::MethodDiscard, disabled_ids);
			room->throwCard(card_id, room->getCardPlace(card_id) == Player::PlaceDelayedTrick ? nullptr : club, target);
			if (target->hasSkill("mobilejuexiang")) return false;
			room->handleAcquireDetachSkills(target, "mobilejuexiang");
		}
		return false;
	}
};

MobileCanyunCard::MobileCanyunCard()
{
}

bool MobileCanyunCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length() < getSubcards().length() && to_select != Self && to_select->getMark("mobilecanyun_used" + Self->objectName()) <= 0;
}

bool MobileCanyunCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == getSubcards().length();
}

void MobileCanyunCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets)
		room->addPlayerMark(p, "mobilecanyun_used" + source->objectName());
	foreach (ServerPlayer *p, targets) {
		if (source->isDead()) break;
		if (p->isDead()) continue;
		room->cardEffect(this, source, p);
	}
	if (targets.length() == source->getHp() && source->isAlive())
		source->drawCards(1, "mobilecanyun");
}

void MobileCanyunCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.from->isDead() || effect.to->isDead()) return;
	Room *room = effect.from->getRoom();
	if (effect.to->getEquips().length() > effect.from->getEquips().length())
		room->loseHp(HpLostStruct(effect.to, 1, "mobilecanyun", effect.from));
	else if (effect.to->getEquips().length() == effect.from->getEquips().length())
		effect.to->drawCards(1, "mobilecanyun");
	else
		room->recover(effect.to, RecoverStruct("mobilecanyun", effect.from));
}

class MobileCanyun : public ViewAsSkill
{
public:
	MobileCanyun() : ViewAsSkill("mobilecanyun")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		int m = 0;
		foreach (const Player *p, Self->getAliveSiblings()) {
			if (p->getMark("mobilecanyun_used" + Self->objectName()) <= 0)
				m++;
		}
		int n = qMin(Self->getHp(), m);
		return selected.length() < n && !Self->isJilei(to_select);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty())
			return nullptr;

		MobileCanyunCard *c = new MobileCanyunCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->canDiscard(player, "he") && !player->hasUsed("MobileCanyunCard") && player->getHp() > 0 && hastarget(player);
	}

	bool hastarget(const Player *player) const
	{
		foreach (const Player *p, player->getAliveSiblings()) {
			if (p->getMark("mobilecanyun_used" + player->objectName()) <= 0)
				return true;
		}
		return false;
	}
};

OLZhongjianCard::OLZhongjianCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool OLZhongjianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void OLZhongjianCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	room->showCard(effect.from, getEffectiveId());
	int n = effect.to->getHp();
	if (n <= 0) return;
	QList<int> list;
	QStringList slist;
	for (int i = 0; i < n; i++) {
		if(list.length()>=effect.to->getHandcardNum()) break;
		int id = room->askForCardChosen(effect.from,effect.to,"h","olzhongjian",false,Card::MethodNone,list);
		list << id;
		slist << Sanguosha->getCard(id)->toString();
	}/*
	LogMessage log;
	log.type = "$ZhongjianShow";
	log.from = effect.from;
	log.to << effect.to;
	log.arg = QString::number(n);
	log.card_str = slist.join("+");
	room->sendLog(log);
	room->fillAG(list);
	room->getThread()->delay(2000);
	room->clearAG();*/
	room->showCard(effect.to, list);
	room->getThread()->delay();
	bool samecolour = false, samenumber = false;
	foreach (int id, list) {
		if (sameColorWith(Sanguosha->getCard(id)))
			samecolour = true;
		if (getNumber() == Sanguosha->getCard(id)->getNumber())
			samenumber = true;
		if (samecolour && samenumber) break;
	}
	LogMessage newlog;
	if (!samecolour && !samenumber) {
		newlog.type = "#ZhongjianResult_NoSame";
		room->sendLog(newlog);
		effect.from->addMark("olzhongjian_debuff");
		room->addMaxCards(effect.from, 1,false);
		return;
	}
	newlog.type = "#ZhongjianResult";
	newlog.arg = samecolour == true ? "zhongjiansamecolour" : "zhongjiandifferentcolour";
	newlog.arg2 = samenumber == true ? "zhongjiansamenumber" : "zhongjiandifferentnumber";
	room->sendLog(newlog);
	if (samecolour) {
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getOtherPlayers(effect.from)) {
			if (effect.from->canDiscard(p, "he")) targets << p;
		}
		ServerPlayer *target = room->askForPlayerChosen(effect.from, targets, "olzhongjian", "@olzhongjian-discard", true);
		if (target){
			int id = room->askForCardChosen(effect.from, target, "he", "olzhongjian", false, Card::MethodDiscard);
			room->throwCard(id, target, effect.from);
		}else
			effect.from->drawCards(1, "olzhongjian");
	}
	if (samenumber)
		room->setPlayerMark(effect.from, "olzhongjian-PlayClear",1);
}

class OLZhongjianVS : public OneCardViewAsSkill
{
public:
	OLZhongjianVS() : OneCardViewAsSkill("olzhongjian")
	{
		filter_pattern = ".|.|.|hand";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("OLZhongjianCard") <= player->getMark("olzhongjian-PlayClear");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		OLZhongjianCard *card = new OLZhongjianCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class OLZhongjian : public TriggerSkill
{
public:
	OLZhongjian() : TriggerSkill("olzhongjian")
	{
		events << EventLoseSkill;
		view_as_skill = new OLZhongjianVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventLoseSkill) {
			if (data.toString() != objectName()) return false;
			room->addMaxCards(player, player->getMark("olzhongjian_debuff"), false);
			player->setMark("olzhongjian_debuff",0);
		}
		return false;
	}
};

class OLCaishi : public TriggerSkill
{
public:
	OLCaishi() : TriggerSkill("olcaishi")
	{
		events << EventLoseSkill << EventPhaseStart;
		frequency = Frequent;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase()!=Player::Draw||!player->hasSkill(objectName())) return false;
			if (!player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());
			QStringList choices;
			choices << "max";
			if (player->getLostHp() > 0) choices << "recover";
			QString choice = room->askForChoice(player, "olcaishi", choices.join("+"));
			LogMessage log;
			log.type ="#FumianFirstChoice";
			log.from = player;
			log.arg = "olcaishi:" + choice;
			room->sendLog(log);
			if (choice == "max") {
				room->addPlayerMark(player, "olcaishi_buff");
			} else {
				room->recover(player, RecoverStruct("olcaishi", player));
				room->addPlayerMark(player, "olcaishi_self-Clear");
			}
		} else if (event == EventLoseSkill) {
			if (data.toString() != objectName()) return false;
			room->setPlayerMark(player, "olcaishi_buff", 0);
		}
		return false;
	}
};

class OLCaishiMax : public MaxCardsSkill
{
public:
	OLCaishiMax() : MaxCardsSkill("#olcaishimax")
	{
		frequency = Frequent;
	}

	int getExtra(const Player *target) const
	{
		if (target->hasSkill("olcaishi"))
			return target->getMark("olcaishi_buff");
		return 0;
	}
};

class OLCaishiPro : public ProhibitSkill
{
public:
	OLCaishiPro() : ProhibitSkill("#olcaishipro")
	{
		frequency = Frequent;
	}

    bool isProhibited(const Player *from, const Player *to, const Card *, const QList<const Player *> &) const
	{
		return from->getMark("olcaishi_self-Clear")>0&&from==to;
	}
};

YCZH2017Package::YCZH2017Package()
	: Package("YCZH2017")
{
	General *qinmi = new General(this, "qinmi", "shu", 3);
	qinmi->addSkill(new Jianzheng);
	qinmi->addSkill(new Zhuandui);
	qinmi->addSkill(new Tianbian);

	General *wuxian = new General(this, "wuxian", "shu", 3, false);
	wuxian->addSkill(new Fumian);
	wuxian->addSkill(new FumianTargetMod);
	wuxian->addSkill(new Daiyan);
	related_skills.insertMulti("fumian", "#fumian-target");

	General *xinxianying = new General(this, "xinxianying", "wei", 3, false);
	xinxianying->addSkill(new Zhongjian);
	xinxianying->addSkill(new Caishi);
	xinxianying->addSkill(new CaishiMax);
	xinxianying->addSkill(new CaishiPro);
	related_skills.insertMulti("caishi", "#caishimax");
	related_skills.insertMulti("caishi", "#caishipro");

	General *ol_xinxianying = new General(this, "ol_xinxianying", "wei", 3, false);
	ol_xinxianying->addSkill(new OLZhongjian);
	ol_xinxianying->addSkill(new OLCaishi);
	ol_xinxianying->addSkill(new OLCaishiMax);
	ol_xinxianying->addSkill(new OLCaishiPro);
	related_skills.insertMulti("olcaishi", "#olcaishimax");
	related_skills.insertMulti("olcaishi", "#olcaishipro");

	General *jikang = new General(this, "jikang", "wei", 3);
	jikang->addSkill(new Qingxian);
	jikang->addSkill(new Juexiang);
	jikang->addSkill(new JuexiangPro);
	jikang->addRelateSkill("jixian");
	jikang->addRelateSkill("liexian");
	jikang->addRelateSkill("rouxian");
	jikang->addRelateSkill("hexian");
	related_skills.insertMulti("juexiang", "#juexiangpro");

	General *mobile_jikang = new General(this, "mobile_jikang", "wei", 3);
	mobile_jikang->addSkill(new MobileQingxian);
	mobile_jikang->addSkill(new MobileJuexiang);
	mobile_jikang->addRelateSkill("mobilecanyun");

	General *xushi = new General(this, "xushi", "wu", 3, false);
	xushi->addSkill(new Wengua);
	xushi->addSkill(new Fuzhu);

	General *xuezong = new General(this, "xuezong", "wu", 3);
	xuezong->addSkill(new Funan);
	xuezong->addSkill(new FunanRemove);
	xuezong->addSkill(new Jiexun);
	related_skills.insertMulti("funan", "#funanremove");

	General *caojie = new General(this, "caojie", "qun", 3, false);
	caojie->addSkill(new Shouxi);
	caojie->addSkill(new Huimin);

	General *caiyong = new General(this, "caiyong", "qun", 3);
	caiyong->addSkill(new Bizhuan);
	caiyong->addSkill(new BizhuanKeep);
	caiyong->addSkill(new Tongbo);
	related_skills.insertMulti("bizhuan", "#bizhuankeep");

	addMetaObject<FumianCard>();
	addMetaObject<ZhongjianCard>();
	addMetaObject<WenguagiveCard>();
	addMetaObject<WenguaCard>();
	addMetaObject<HuiminCard>();
	addMetaObject<TongboCard>();
	addMetaObject<MobileQingxianCard>();
	addMetaObject<MobileCanyunCard>();
	addMetaObject<OLZhongjianCard>();

	skills << new Jixian << new Liexian << new Rouxian << new Hexian << new Wenguagive << new MobileCanyun;
}
ADD_PACKAGE(YCZH2017)
