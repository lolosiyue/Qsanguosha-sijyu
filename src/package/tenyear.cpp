#include "tenyear.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
#include "yjcm2013.h"
#include "clientplayer.h"
//#include "clientstruct.h"
#include "engine.h"
#include "maneuvering.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
//#include "json.h"
#include "wind.h"
#include "mobile.h"

class MYJincui : public TriggerSkill
{
public:
	MYJincui() : TriggerSkill("myjincui")
	{
		events << GameStart << EventPhaseStart;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (event == GameStart) {
			if (player->getHandcardNum() >= 7) return false;
			room->sendCompulsoryTriggerLog(player, this);
			player->drawCards(7 - player->getHandcardNum(), objectName());
		} else {
			if (player->getPhase() != Player::Start) return false;
			int seven = 0, maxhp = player->getMaxHp();
			foreach (int id, room->getDrawPile()) {
				if (Sanguosha->getCard(id)->getNumber() == 7)
					seven++;
				if (seven >= maxhp)
					break;
			}
			seven = qMax(1, seven);

			LogMessage log;
			log.type = "#MYJincuiHp";
			log.from = player;
			log.arg = objectName();
			log.arg2 = QString::number(seven);
			room->sendLog(log);
			player->peiyin(this);
			room->notifySkillInvoked(player, objectName());

			room->setPlayerProperty(player, "hp", seven);

			int hp = player->getHp();
			if (hp <= 0) return false;

			QList<int> guanxing = room->getNCards(hp);

			log.type = "$ViewDrawPile";
			log.card_str = ListI2S(guanxing).join("+");
			room->sendLog(log, player);

			room->askForGuanxing(player, guanxing);
		}
		return false;
	}
};

class Qingshi : public TriggerSkill
{
public:
	Qingshi() : TriggerSkill("qingshi")
	{
		events << CardUsed << CardResponded;
		waked_skills = "#qingshi";
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (player->getPhase() != Player::Play || player->getMark("qingshiWuxiao-Clear") > 0) return false;

		QList<ServerPlayer *> targets;
		const Card *c = nullptr;

		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			c = use.card;
			targets = use.to;
		} else {
			CardResponseStruct res = data.value<CardResponseStruct>();
			if (!res.m_isUse) return false;
			c = res.m_card;
		}
		if (!c || c->isKindOf("SkillCard")) return false;

		QString name = c->objectName();
		if (c->isKindOf("Slash")) name = "slash";
		if (player->getMark("qingshiUsed_" + name + "-Clear") > 0) return false;

		bool same = false;
		foreach (const Card *card, player->getHandcards()) {
			if (card->sameNameWith(c)) {
				same = true;
				break;
			}
		}
		if (!same) return false;

		QStringList choices;
		if (!targets.isEmpty())
			choices << "damage=" + c->objectName();
		choices << "draw";
		if (player->getHp() > 0)
			choices << "selfdraw";
		choices << "cancel";

		QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);
		if (choice == "cancel") return false;
		player->peiyin(objectName());

		LogMessage log;
		log.type = "#InvokeSkill";
		log.from = player;
		log.arg = objectName();
		room->sendLog(log);
		room->notifySkillInvoked(player, objectName());
		player->addMark("qingshiUsed_" + name + "-Clear");

		if (choice.startsWith("damage")) {
			ServerPlayer *t = room->askForPlayerChosen(player, targets, objectName(), "@qingshi-damage");
			room->doAnimate(1, player->objectName(), t->objectName());
			log.type = "#QingshiDamage";
			log.to << t;
			log.arg = c->objectName();
			room->sendLog(log);
			room->setCardFlag(c, "QingshiDamage_" + t->objectName());
		} else if (choice == "draw") {
			targets = room->askForPlayersChosen(player, room->getOtherPlayers(player), objectName(), 1, 99, "@qingshi-draw");
			foreach (ServerPlayer *p, targets)
				room->doAnimate(1, player->objectName(), p->objectName());
			room->drawCards(targets, 1, objectName());
		} else {
			player->drawCards(3, objectName());
			player->setMark("qingshiWuxiao-Clear", 1);
		}
		return false;
	}
};

class QingshiDamage : public TriggerSkill
{
public:
	QingshiDamage() : TriggerSkill("#qingshi")
	{
		events << ConfirmDamage;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.card&&damage.card->hasFlag("QingshiDamage_" + damage.to->objectName()))
			player->damageRevises(data,1);
		return false;
	}
};

ZhizheCard::ZhizheCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void ZhizheCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->removePlayerMark(source, "@zhizheMark");
	room->doSuperLightbox(source, "zhizhe");

	int id = -1;
	const Card *c = Sanguosha->getEngineCard(subcards.first());

	if (c->isKindOf("Suijiyingbian"))
		id = source->getDerivativeCard("_zhizhe_suijiyingbian", Player::PlaceTable);
	else if (c->isKindOf("BasicCard"))
		id = source->getDerivativeCard("_zhizhe_basic", Player::PlaceTable);
	else if (c->isKindOf("TrickCard"))
		id = source->getDerivativeCard("_zhizhe_trick", Player::PlaceTable);
	else if (c->isKindOf("Weapon"))
		id = source->getDerivativeCard("_zhizhe_weapon", Player::PlaceTable);
	else if (c->isKindOf("Armor"))
		id = source->getDerivativeCard("_zhizhe_armor", Player::PlaceTable);
	else if (c->isKindOf("DefensiveHorse"))
		id = source->getDerivativeCard("_zhizhe_defensivehorse", Player::PlaceTable);
	else if (c->isKindOf("OffensiveHorse"))
		id = source->getDerivativeCard("_zhizhe_offensivehorse", Player::PlaceTable);
	else if (c->isKindOf("Treasure"))
		id = source->getDerivativeCard("_zhizhe_treasure", Player::PlaceTable);
	if (id < 0) return;

	QVariantList zhizhes = source->getTag("ZhizheIds").toList();
	zhizhes << id;
	source->setTag("ZhizheIds", zhizhes);

	QStringList info;
	info << c->objectName() << c->getSuitString() << QString::number(c->getNumber());
	room->setTag("ZhizheFilter_" + QString::number(id), info.join("+"));

	foreach (ServerPlayer *p, room->getPlayers())
		room->acquireSkill(p, "#zhizhe");

	room->moveCardsAtomic(CardsMoveStruct(id,source,Player::PlaceHand,CardMoveReason(CardMoveReason::S_REASON_EXCLUSIVE,source->objectName(),"zhizhe","")),true);
}

class ZhizheVS : public OneCardViewAsSkill
{
public:
	ZhizheVS() : OneCardViewAsSkill("zhizhe")
	{
	}

	bool viewFilter(const Card *to_select) const
	{
		return !to_select->isEquipped();
	}

	const Card *viewAs(const Card *originalCard) const
	{
		ZhizheCard *c = new ZhizheCard;
		c->addSubcard(originalCard);
		return c;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->isKongcheng() && player->getMark("@zhizheMark") > 0;
	}
};

class Zhizhe : public TriggerSkill
{
public:
	Zhizhe() : TriggerSkill("zhizhe")
	{
		events << CardsMoveOneTime << Revived;
		view_as_skill = new ZhizheVS;
		frequency = Limited;
		limit_mark = "@zhizheMark";
		waked_skills = "#zhizhe-limit";
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to_place!=Player::DiscardPile||player->getTag("ZhizheIds").toList().isEmpty()) return false;
            if ((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)!=CardMoveReason::S_REASON_USE
				&& move.reason.m_reason != CardMoveReason::S_REASON_RESPONSE) return false;
			if (move.from != player && move.reason.m_playerId != player->objectName()) return false;
	
			foreach (int id, ListV2I(player->getTag("ZhizheIds").toList())) {
				if (!move.card_ids.contains(id)||room->getCardOwner(id)) continue;
				room->sendCompulsoryTriggerLog(player, objectName());
				room->setPlayerMark(player, QString::number(id)+"_zhizheLimit-Clear", 1);
				room->obtainCard(player,id);
				if (player->isDead()) break;
			}
		}else
			room->acquireSkill(player, "#zhizhe");
		return false;
	}
};

class ZhizheLimit : public CardLimitSkill
{
public:
	ZhizheLimit() : CardLimitSkill("#zhizhe-limit")
	{
	}

	QString limitList(const Player *) const
	{
		return "use,response";
	}

	QString limitPattern(const Player *target) const
	{
		QStringList limits;
		foreach (QString mark, target->getMarkNames()) {
			if (mark.contains("_zhizheLimit"))
				limits << mark.split("_").first();
		}
		return limits.join(",");
	}
};

class ZhizheFilter : public FilterSkill
{
public:
	ZhizheFilter() : FilterSkill("#zhizhe")
	{
	}

	bool viewFilter(const Card *to_select) const
	{
		//int id = to_select->getEffectiveId();
		//if (Sanguosha->getCardPlace(id) != Player::PlaceHand && Sanguosha->getCardPlace(id) != Player::PlaceEquip) return false;
		return Sanguosha->currentRoom()->getTag("ZhizheFilter_"+to_select->toString()).toString().contains("+");
	}

	const Card *viewAs(const Card *original) const
	{
		QString info = Sanguosha->currentRoom()->getTag("ZhizheFilter_"+original->toString()).toString();
		QStringList infos = info.split("+");
		Card *card = Sanguosha->cloneCard(infos[0], getSuit(infos[1]), infos[2].toInt());
		if (!card) return original;
		if(infos.length()==3) info = "zhizhe";
		else info = infos[3];
		card->setSkillName(info);/*
		WrappedCard *wrapped = Sanguosha->getWrappedCard(original->getEffectiveId());
		wrapped->takeOver(card);*/
		return card;
	}

	Card::Suit getSuit(const QString &str) const
	{
		if (str == "spade")
			return Card::Spade;
		if (str == "heart")
			return Card::Heart;
		if (str == "diamond")
			return Card::Diamond;
		if (str == "club")
			return Card::Club;
		if (str == "no_suit_black")
			return Card::NoSuitBlack;
		if (str == "no_suit_red")
			return Card::NoSuitRed;
		return Card::NoSuit;
	}
};

class TenyearXushen : public TriggerSkill
{
public:
	TenyearXushen() : TriggerSkill("tenyearxushen")
	{
		events << QuitDying;
		frequency = Limited;
		limit_mark = "@tenyearxushenMark";
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (player->getMark("@tenyearxushenMark") <= 0) return false;
		ServerPlayer *saver = player->getSaver();
		if (!saver || saver == player) return false;
		bool guansuo = false;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->getGeneralName().contains("guansuo") || p->getGeneral2Name().contains("guansuo")) {
				guansuo = true;
				break;
			}
		}
		if (guansuo) return false;

		if (!player->askForSkillInvoke(objectName(), QVariant::fromValue(saver))) return false;
		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(player, "tenyearxushen");
		room->removePlayerMark(player, "@tenyearxushenMark");
		if (saver->askForSkillInvoke("tenyearxushenChange", "guansuo",false))
			room->changeHero(saver, "tenyear_guansuo", false, false);
		saver->drawCards(3, objectName());
		room->recover(player, RecoverStruct("tenyearxushen", player));
		room->acquireSkill(player, "tenyearzhennan");
		return false;
	}
};

class TenyearZhennan : public TriggerSkill
{
public:
	TenyearZhennan() : TriggerSkill("tenyearzhennan")
	{
		events << TargetConfirmed;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("TrickCard") || !use.to.contains(player)) return false;
		if (use.to.length() <= 1) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@tenyearzhennan-invoke", true, true);
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());
		room->damage(DamageStruct(objectName(), player, target));
		return false;
	}
};

class SecondWuniang : public TriggerSkill
{
public:
	SecondWuniang() : TriggerSkill("secondwuniang")
	{
		events << CardUsed << CardResponded;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		const Card *card;
		if (triggerEvent == CardUsed) card = data.value<CardUseStruct>().card;
		else card = data.value<CardResponseStruct>().m_card;
		if (!card || !card->isKindOf("Slash")) return false;
		QList<ServerPlayer *> players;
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (!p->isNude())
				players << p;
		}
		if (players.isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, players, objectName(), "@wuniang-invoke", true, true);
		if (!target) return false;
		room->broadcastSkillInvoke(objectName());
		if (target->isNude()) return false;
		int id = room->askForCardChosen(player, target, "he", objectName());
		room->obtainCard(player, id, false);
		if (target->isAlive()) target->drawCards(1, objectName());
		if (!player->getTag("secondxushen_used").toBool()) return false;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->getGeneralName().contains("guansuo") || p->getGeneral2Name().contains("guansuo"))
				p->drawCards(1, objectName());
		}
		return false;
	}
};

class SecondXushen : public TriggerSkill
{
public:
	SecondXushen() : TriggerSkill("secondxushen")
	{
		events << Dying;
		frequency = Limited;
		limit_mark = "@secondxushenMark";
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DyingStruct dying = data.value<DyingStruct>();
		if (player != dying.who || player->getMark("@secondxushenMark") <= 0) return false;
		if (!player->askForSkillInvoke(this)) return false;
		player->setTag("secondxushen_used", true);
		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(player, "secondxushen");
		room->removePlayerMark(player, "@secondxushenMark");
		room->recover(player, RecoverStruct("secondxushen", player));
		if (player->isDead()) return false;
		room->acquireSkill(player, "secondzhennan");
		if (player->isDead()) return false;
		bool guansuo = false;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->getGeneralName().contains("guansuo") || p->getGeneral2Name().contains("guansuo")) {
				guansuo = true;
				break;
			}
		}
		if (guansuo) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@secondxushen-invoke", true);
		if (!target) return false;
		room->doAnimate(1, player->objectName(), target->objectName());
		if (target->askForSkillInvoke("tenyearxushenChange", "guansuo"))
			room->changeHero(target, "tenyear_guansuo", false, false);
		target->drawCards(3, objectName());
		return false;
	}
};

class SecondZhennan : public TriggerSkill
{
public:
	SecondZhennan() : TriggerSkill("secondzhennan")
	{
		events << TargetSpecified;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isNDTrick()||use.to.length() <= 1) return false;
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->isDead() || !p->hasSkill(objectName())) continue;
			ServerPlayer *target = room->askForPlayerChosen(p, room->getOtherPlayers(p), objectName(), "@tenyearzhennan-invoke", true, true);
			if (!target) return false;
			room->broadcastSkillInvoke(objectName());
			room->damage(DamageStruct(objectName(), p, target));
		}
		return false;
	}
};

class TenyearFuhan : public PhaseChangeSkill
{
public:
	TenyearFuhan() : PhaseChangeSkill("tenyearfuhan")
	{
		frequency = Limited;
		limit_mark = "@tenyearfuhanMark";
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::RoundStart || player->getMark("@tenyearfuhanMark") <= 0 ||
				player->getMark("&meiying") <= 0) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(player, "tenyearfuhan");
		room->removePlayerMark(player, "@tenyearfuhanMark");
		int mark = player->getMark("&meiying");
		player->loseAllMarks("&meiying");
		player->drawCards(mark, objectName());

		if (!player->askForSkillInvoke("tenyearfuhan", QString("getskill"), false)) {
			if (!player->isLowestHpPlayer()) return false;
			room->recover(player, RecoverStruct("tenyearfuhan", player));
			return false;
		}

		QStringList all_shus = Sanguosha->getLimitedGeneralNames("shu");
		foreach (QString shu, all_shus) {
			const General *g = Sanguosha->getGeneral(shu);
			if (!g || g->getVisibleSkillList().isEmpty())
				all_shus.removeOne(shu);
		}
		if (all_shus.isEmpty()) return false;

		int n = room->alivePlayerCount();
		n = qMax(n, 4);
		QStringList shus;
		for (int i = 1; i <= n; i++) {
			if (all_shus.isEmpty()) break;
			QString name = all_shus.at((qrand() % all_shus.length()));
			shus << name;
			all_shus.removeOne(name);
		}
		if (shus.isEmpty()) return false;

		for (int i = 1; i <= 2; i++) {
			if (shus.isEmpty() || player->isDead()) break;

			QString shu_general = room->askForGeneral(player, shus);
			const General *g = Sanguosha->getGeneral(shu_general);
			if (!g) {
				shus.removeOne(shu_general);
				continue;
			}
			QList<const Skill *> sks = g->getVisibleSkillList();
			if (sks.isEmpty()) {
				shus.removeOne(shu_general);
				continue;
			}
			QStringList sk_names;
			foreach (const Skill *sk, sks) {
				if (sk_names.contains(sk->objectName()) || player->hasSkill(sk, true)) continue;
				if (sk->isLimitedSkill() || sk->isLordSkill() || sk->getFrequency() == Skill::Wake) continue;
				sk_names << sk->objectName();
			}
			if (sk_names.isEmpty()) {
				shus.removeOne(shu_general);
				continue;
			}
			QString sk = room->askForChoice(player, objectName(), sk_names.join("+"));
			sk_names.removeOne(sk);
			if (sk_names.isEmpty())
				shus.removeOne(shu_general);

			room->acquireSkill(player, sk);
			if (i == 1) {
				if (!player->askForSkillInvoke("tenyearfuhan", QString("continue"), false))
					break;
			}
		}

		if (!player->isLowestHpPlayer()) return false;
		room->recover(player, RecoverStruct("tenyearfuhan", player));
		return false;
	}
};

class TenyearZhengnan : public TriggerSkill
{
public:
	TenyearZhengnan() : TriggerSkill("tenyearzhengnan")
	{
		events << Dying;
		frequency = Frequent;
		waked_skills = "tenyearwusheng,tenyeardangxian,tenyearzhiman";
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *guansuo, QVariant &data) const
	{
		DyingStruct dying = data.value<DyingStruct>();
		QStringList names = guansuo->property("tenyearzhengnan_names").toStringList();
		if (names.contains(dying.who->objectName())) return false;
		if (!guansuo->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		names << dying.who->objectName();
		room->setPlayerProperty(guansuo, "tenyearzhengnan_names", names);
		room->recover(guansuo, RecoverStruct("tenyearzhengnan", guansuo));
		guansuo->drawCards(1, objectName());
		QStringList choices;
		if (!guansuo->hasSkill("tenyearwusheng", true)) choices << "tenyearwusheng";
		if (!guansuo->hasSkill("tenyeardangxian", true)) choices << "tenyeardangxian";
		if (!guansuo->hasSkill("tenyearzhiman", true)) choices << "tenyearzhiman";
		if (choices.isEmpty())
			guansuo->drawCards(3, objectName());
		else {
			QString choice = room->askForChoice(guansuo, "tenyearzhengnan", choices.join("+"), QVariant());
			if (!guansuo->hasSkill(choice, true))
				room->handleAcquireDetachSkills(guansuo, choice);
			if(choice=="tenyeardangxian"){
				room->addPlayerMark(guansuo, "tenyeardangxian");
				room->changeTranslation(guansuo, "tenyeardangxian", 2);
			}
		}
		return false;
	}
};

class TenyearZhenyiVS : public OneCardViewAsSkill
{
public:
	TenyearZhenyiVS() : OneCardViewAsSkill("tenyearzhenyi")
	{
		filter_pattern = ".|.|.|hand";
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return pattern.contains("peach") && player->getMark("Global_PreventPeach")<1
			&& player->getMark("@flhoutu") > 0 && !player->hasFlag("CurrentPlayer");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		Peach *peach = new Peach(originalCard->getSuit(), originalCard->getNumber());
		peach->addSubcard(originalCard->getId());
		peach->setSkillName(objectName());
		return peach;
	}
};

class TenyearZhenyi : public TriggerSkill
{
public:
	TenyearZhenyi() : TriggerSkill("tenyearzhenyi")
	{
		events << AskForRetrial << DamageCaused << Damaged << PreCardUsed;
		view_as_skill = new TenyearZhenyiVS;
	}

	int getPriority(TriggerEvent triggerEvent) const
	{
		if (triggerEvent == PreCardUsed) return 5;
		return TriggerSkill::getPriority(triggerEvent);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == DamageCaused) {
			if (player->getMark("@flyuqing") > 0) {
				player->setTag("flyuqing_tenyear", data);
				DamageStruct damage = data.value<DamageStruct>();
				bool invoke = player->askForSkillInvoke(this, QString("flyuqing:%1").arg(damage.to->objectName()));
				player->removeTag("flyuqing_tenyear");
				if (!invoke) return false;
				room->broadcastSkillInvoke(objectName());
				player->loseMark("@flyuqing");
				++damage.damage;
				data = QVariant::fromValue(damage);
			}
		} else if (event == AskForRetrial) {
			JudgeStruct *judge = data.value<JudgeStruct *>();
			if (player->getMark("@flziwei") > 0) {
				player->setTag("flziwei_tenyear", data);
				bool invoke = player->askForSkillInvoke("tenyearzhenyi", QString("flziwei:%1").arg(judge->who->objectName()));
				player->removeTag("flziwei_tenyear");
				if (!invoke) return false;
				room->broadcastSkillInvoke(objectName());
				player->loseMark("@flziwei");
				QString choice = room->askForChoice(player, "zhenyi", "spade+heart", data);

				WrappedCard *new_card = Sanguosha->getWrappedCard(judge->card->getId());
				new_card->setSkillName("tenyearzhenyi");
				new_card->setNumber(5);
				new_card->setModified(true);
				//new_card->deleteLater();

				if (choice == "spade")
					new_card->setSuit(Card::Spade);
				else
					new_card->setSuit(Card::Heart);

				LogMessage log;
				log.type = "#ZhenyiRetrial";
				log.from = player;
				log.to << judge->who;
				log.arg2 = QString::number(5);
				log.arg = new_card->getSuitString();
				room->sendLog(log);
				room->broadcastUpdateCard(room->getAllPlayers(true), judge->card->getId(), new_card);
				judge->updateResult();
			}
		} else if (event == PreCardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getSkillNames().contains(objectName()))
				player->loseMark("@flhoutu");
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.nature == DamageStruct::Normal) return false;
			if (player->getMark("@flgouchen") <= 0 || !player->askForSkillInvoke("zhenyi", QString("flgouchen"))) return false;
			room->broadcastSkillInvoke(objectName());
			player->loseMark("@flgouchen");

			QList<int> basic, equip, trick;
			foreach (int id, room->getDrawPile()) {
				const Card *c = Sanguosha->getCard(id);
				if (c->isKindOf("BasicCard"))
					basic << id;
				else if (c->isKindOf("EquipCard"))
					equip << id;
				else if (c->isKindOf("TrickCard"))
					trick << id;
			}

			DummyCard *dummy = new DummyCard;
			if (!basic.isEmpty())
				dummy->addSubcard(basic.at(qrand() % basic.length()));
			if (!equip.isEmpty())
				dummy->addSubcard(equip.at(qrand() % equip.length()));
			if (!trick.isEmpty())
				dummy->addSubcard(trick.at(qrand() % trick.length()));

			if (dummy->subcardsLength() > 0)
				room->obtainCard(player, dummy, false);
			delete dummy;
		}
		return false;
	}
};

ZhukouCard::ZhukouCard()
{
}

bool ZhukouCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length() < 2 && to_select != Self;
}

bool ZhukouCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == 2;
}

void ZhukouCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *target, targets) {
		if (target->isAlive())
			room->cardEffect(this, source, target);
	}
}

void ZhukouCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	room->damage(DamageStruct("zhukou", effect.from->isAlive() ? effect.from : nullptr, effect.to, 1));
}

class ZhukouVS : public ZeroCardViewAsSkill
{
public:
	ZhukouVS() : ZeroCardViewAsSkill("zhukou")
	{
		response_pattern = "@@zhukou";
	}

	const Card *viewAs() const
	{
		return new ZhukouCard;
	}
};

class Zhukou : public TriggerSkill
{
public:
	Zhukou() : TriggerSkill("zhukou")
	{
		events << Damage << EventPhaseStart;
		view_as_skill = new ZhukouVS;
		global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(event==Damage){
			ServerPlayer *current = room->getCurrent();
			if (!current||current->getPhase() != Player::Play) return false;
			player->addMark("zhukou-PlayClear");
			if (player->getMark("zhukou-PlayClear")>1||!player->hasSkill(objectName())) return false;
			int used = player->getMark("jingce-Clear");
			if (!player->askForSkillInvoke("zhukou", "zhukou_draw:" + QString::number(used))) return false;
			room->broadcastSkillInvoke("zhukou");
			player->drawCards(used, "zhukou");
		}else{
			if (player->getPhase()!=Player::Finish||player->getMark("damage_point_round")>0) return false;
			if (room->alivePlayerCount()<3||!player->isAlive()||!player->hasSkill(objectName())) return false;
			room->askForUseCard(player, "@@zhukou", "@zhukou");
		}
		return false;
	}
};

class Mengqing : public PhaseChangeSkill
{
public:
	Mengqing() : PhaseChangeSkill("mengqing")
	{
		frequency = Wake;
		waked_skills = "yuyun";
	}

	bool triggerable(const ServerPlayer *player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		int wounded = 0;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->isWounded())
				wounded++;
		}
		if(wounded <= player->getHp()&&!player->canWake("mengqing"))
			return false;
		room->sendCompulsoryTriggerLog(player, this);
		room->doSuperLightbox(player, "mengqing");
		room->setPlayerMark(player, "mengqing", 1);
		if (room->changeMaxHpForAwakenSkill(player, 3, objectName())) {
			int recover = qMin(3, player->getMaxHp() - player->getHp());
			room->recover(player, RecoverStruct(player, nullptr, recover, "mengqing"));
			if (player->isDead()) return false;
			room->handleAcquireDetachSkills(player, "-zhukou|yuyun");
		}
		return false;
	}
};

class Yuyun : public PhaseChangeSkill
{
public:
	Yuyun() : PhaseChangeSkill("yuyun")
	{
		frequency = Compulsory;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Play) return false;
		QString choice = room->askForChoice(player, objectName(), "hp+maxhp");
		if (choice == "hp")
			room->loseHp(HpLostStruct(player, 1, objectName(), player));
		else if (choice == "maxhp" && player->getMaxHp() > 1)
			room->loseMaxHp(player, 1, objectName());

		if (player->isDead()) return false;

		int max = player->getLostHp() + 1;
		QStringList chosen, has_chosen;

		for (int i = 1; i <= max; i++) {
			if (player->isDead()) return false;
			QStringList choices;
			if (!chosen.contains("draw"))
				choices << "draw";
			if (!chosen.contains("damage"))
				choices << "damage";
			if (!chosen.contains("maxcard"))
				choices << "maxcard";

			if (!chosen.contains("obtain")) {
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if (!p->isAllNude()) {
						choices << "obtain";
						break;
					}
				}
			}

			if (!chosen.contains("drawmaxhp"))
				choices << "drawmaxhp";

			if (i > 1)
				choices << "cancel";

			choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant(), has_chosen.join("+"));
			if (choice == "cancel") break;
			chosen.append(choice);
			has_chosen.append(choice);
		}

		foreach (QString cho, chosen) {
			if (player->isDead()) return false;
			if (cho == "draw")
				player->drawCards(2, objectName());
			else if (cho == "damage") {
				ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@yuyun-damage");
				room->doAnimate(1, player->objectName(), t->objectName());
				room->damage(DamageStruct("yuyun", player, t));
				room->addPlayerMark(player, "yuyun_from-Clear");
				room->addPlayerMark(t, "yuyun_to-Clear");
			} else if (cho == "maxcard")
				room->addMaxCards(player, 999999);
			else if (cho == "obtain") {
				QList<ServerPlayer *> targets;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if (!p->isAllNude())
						targets << p;
				}
				if (targets.isEmpty()) continue;
				ServerPlayer *t = room->askForPlayerChosen(player, targets, "yuyun_obtain", "@yuyun-obtain");
				room->doAnimate(1, player->objectName(), t->objectName());
				int id = room->askForCardChosen(player, t, "hej", objectName());
				room->obtainCard(player, id, false);
			} else if (cho == "drawmaxhp") {
				ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), "yuyun_drawmaxhp", "@yuyun-drawmaxhp");
				room->doAnimate(1, player->objectName(), t->objectName());
				int num = qMin(5, t->getMaxHp() - t->getHandcardNum());
				if (num > 0)
					t->drawCards(num, objectName());
			}
		}
		return false;
	}
};

class YuyunTargetMod : public TargetModSkill
{
public:
	YuyunTargetMod() : TargetModSkill("#yuyun")
	{
	}

	int getResidueNum(const Player *from, const Card *, const Player *to) const
	{
		if (from->getMark("yuyun_from-Clear") > 0 && to && to->getMark("yuyun_to-Clear") > 0)
			return 999;
		if (from->hasSkill("sharan"))
			return from->getEquips().length();
		return 0;
	}

	int getDistanceLimit(const Player *from, const Card *, const Player *to) const
	{
		if (from->getMark("yuyun_from-Clear") > 0 && to && to->getMark("yuyun_to-Clear") > 0)
			return 999;
		if (from->getMark("thzhanjue1-PlayClear") > 0)
			return 999;
		return 0;
	}
};

class Tianren : public TriggerSkill
{
public:
	Tianren() : TriggerSkill("tianren")
	{
		events << CardsMoveOneTime << MarkChanged << MaxHpChanged;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to_place != Player::DiscardPile) return false;
            if ((move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE) return false;
			int n = 0;
			foreach (int id, move.card_ids) {
				const Card *c = Sanguosha->getCard(id);
				if (c->isKindOf("BasicCard") || c->isNDTrick())
					n++;
			}
			if (n <= 0) return false;
			room->sendCompulsoryTriggerLog(player, this);
			player->gainMark("&tianren", n);
		} else {
			if (triggerEvent == MarkChanged) {
				MarkStruct mark = data.value<MarkStruct>();
				if (mark.name != "&tianren" || mark.gain < 1) return false;
			}
			while (player->getMark("&tianren") >= player->getMaxHp()) {
				room->sendCompulsoryTriggerLog(player, this);
				player->loseMark("&tianren", player->getMaxHp());
				room->gainMaxHp(player, 1, objectName());
				player->drawCards(2, objectName());
			}
		}
		return false;
	}
};

class Jiufa : public TriggerSkill
{
public:
	Jiufa() : TriggerSkill("jiufa")
	{
		events << CardFinished << PostCardResponded;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		const Card *card = nullptr;
		if (triggerEvent == CardFinished)
			card = data.value<CardUseStruct>().card;
		else
			card = data.value<CardResponseStruct>().m_card;

		if (!card || card->isKindOf("SkillCard")) return false;
		QString name = card->objectName();
		if (card->isKindOf("Slash"))
			name = "slash";

		QStringList records, _records;
		QString record = player->property("SkillDescriptionRecord_jiufa").toString();
		if (!record.isEmpty()) records = record.split("+");
		if (records.contains(name)) return false;
		records << name;
		foreach (QString str, records)
			_records << str << "|";
		room->setPlayerProperty(player, "SkillDescriptionRecord_jiufa", records.join("+"));
		player->setSkillDescriptionSwap(objectName(),"%arg11",_records.join("+"));
		room->changeTranslation(player, objectName(), 1);

		if (records.length() < 9 || !player->askForSkillInvoke(this)) return false;
		player->peiyin(this);
		room->setPlayerProperty(player, "SkillDescriptionRecord_jiufa", "");
		player->setSkillDescriptionSwap(objectName(),"%arg11","");

		QList<int> liangchu = room->showDrawPile(player, 9, objectName()), chongfu;
		foreach (int id, liangchu) {
			int num = Sanguosha->getCard(id)->getNumber();
			foreach (int id2, liangchu) {
				if (id2 == id) continue;
				int num2 = Sanguosha->getCard(id2)->getNumber();
				if (num != num2) continue;
				chongfu << id;
				break;
			}
		}
		if (chongfu.isEmpty()) {
			DummyCard *dummy = new DummyCard(liangchu);
			dummy->deleteLater();
			CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), "");
			room->throwCard(dummy, reason, nullptr);
		} else {
			DummyCard *get = new DummyCard();
			DummyCard *throww = new DummyCard();
			get->deleteLater();
			throww->deleteLater();

			while (!chongfu.isEmpty()) {
				if (player->isDead()) break;
				room->fillAG(chongfu, player);   //偷懒用AG，而且九张牌移进手里，牌太多操作也麻烦
				int id = room->askForAG(player, chongfu, false, objectName());
				room->clearAG(player);

				get->addSubcard(id);
				chongfu.removeOne(id);

				int num = Sanguosha->getCard(id)->getNumber();
				foreach (int id2, chongfu) {
					int num2 = Sanguosha->getCard(id2)->getNumber();
					if (num == num2)
						chongfu.removeOne(id2);
				}
			}

			QList<int> subcards1 = get->getSubcards();
			foreach (int id, liangchu) {
				if (subcards1.contains(id)) continue;
				throww->addSubcard(id);
			}

			if (get->subcardsLength() > 0)
				room->obtainCard(player, get);
			if (throww->subcardsLength() > 0) {
				CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), "");
				room->throwCard(throww, reason, nullptr);
			}
		}
		return false;
	}
};

PingxiangCard::PingxiangCard()
{
	target_fixed = true;
}

void PingxiangCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->removePlayerMark(source, "@pingxiangMark");
	room->doSuperLightbox(source, "pingxiang");

	room->loseMaxHp(source, 9, "pingxiang");
	if (source->isDead()) return;

	for (int i = 0; i < 9; i++) {
		if (source->isDead()) break;
		if (!room->askForUseCard(source, "@@pingxiang", "@pingxiang:" + QString::number(i + 1))) break;
	}

	if (source->isDead()) return;

	room->detachSkillFromPlayer(source, "jiufa");
	room->setPlayerMark(source, "&pingxiangmax", 1);
}

class Pingxiang : public ZeroCardViewAsSkill
{
public:
	Pingxiang() : ZeroCardViewAsSkill("pingxiang")
	{
		frequency = Limited;
		limit_mark = "@pingxiangMark";
		response_pattern = "@@pingxiang";
	}

	const Card *viewAs() const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if (pattern == "@@pingxiang") {
			FireSlash *f = new FireSlash(Card::NoSuit, 0);
			f->setSkillName("_pingxiang");
			return f;
		}
		return new PingxiangCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@pingxiangMark") > 0 && player->getMaxHp() > 9;
	}
};

class PingxiangMaxCards : public MaxCardsSkill
{
public:
	PingxiangMaxCards() : MaxCardsSkill("#pingxiang")
	{
		frequency = Limited;
	}

	int getFixed(const Player *target) const
	{
		if (target->getMark("&pingxiangmax") > 0)
			return target->getMaxHp();
		return -1;
	}
};

ShouliCard::ShouliCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool ShouliCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Slash *slash = new Slash(Card::SuitToBeDecided, -1);
	slash->setSkillName("shouli");
	slash->addSubcards(subcards);
	slash->deleteLater();
	return slash->targetFilter(targets, to_select, Self);
}

bool ShouliCard::targetFixed() const
{
	if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;

	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card) {
		card->addSubcards(subcards);
		card->setSkillName("shouli");
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool ShouliCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Slash *slash = new Slash(Card::SuitToBeDecided, -1);
	slash->setSkillName("shouli");
	slash->addSubcards(subcards);
	slash->deleteLater();
	return slash->targetsFeasible(targets, Self);
}

void ShouliCard::addMark(ServerPlayer *player1, ServerPlayer *player2) const
{
	QList<ServerPlayer *> players;
	players << player1;
	if (player2 != player1)
		players << player2;
	Room *room = player1->getRoom();
	room->sortByActionOrder(players);
	foreach (ServerPlayer *p, players) {
		if (p->isDead()) continue;
		room->addPlayerMark(p, "&shouli_debuff-Clear");
	}
}

const Card *ShouliCard::validate(CardUseStruct &card_use) const
{
	Room *room = card_use.from->getRoom();
	ServerPlayer *owner = room->getCardOwner(subcards.first());

	if (user_string.contains("jink") || user_string.contains("Jink")) {
		Jink *jink = new Jink(Card::SuitToBeDecided, -1);
		jink->setSkillName("shouli");
		jink->addSubcards(subcards);
		jink->deleteLater();
		if (card_use.from->isLocked(jink)) return nullptr;

		if (room->hasCurrent() && owner) {
			addMark(card_use.from, owner);
			room->addPlayerMark(owner, "@skill_invalidity");
			foreach(ServerPlayer *p, room->getAllPlayers())
				room->filterCards(p, p->getCards("he"), true);
			JsonArray args;
			args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
			room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
		}
		return jink;
	}else{
		Slash *slash = new Slash(Card::SuitToBeDecided, -1);
		slash->setSkillName("shouli");
		slash->addSubcards(subcards);
		slash->deleteLater();
		if (card_use.from->isLocked(slash)) return nullptr;

		if (room->hasCurrent() && owner) {
			addMark(card_use.from, owner);
			room->addPlayerMark(owner, "@skill_invalidity");
			foreach(ServerPlayer *p, room->getAllPlayers())
				room->filterCards(p, p->getCards("he"), true);
			JsonArray args;
			args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
			room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
		}
		card_use.m_addHistory = false;
		return slash;
	}
	return nullptr;
}

const Card *ShouliCard::validateInResponse(ServerPlayer *player) const
{
	Room *room = player->getRoom();
	ServerPlayer *owner = room->getCardOwner(subcards.first());

	if (user_string.contains("slash") || user_string.contains("Slash")) {
		Slash *slash = new Slash(Card::SuitToBeDecided, -1);
		slash->setSkillName("shouli");
		slash->addSubcards(subcards);
		slash->deleteLater();
		if (player->isLocked(slash)) return nullptr;

		if (room->hasCurrent() && owner) {
			addMark(player, owner);
			room->addPlayerMark(owner, "@skill_invalidity");
			foreach(ServerPlayer *p, room->getAlivePlayers())
				room->filterCards(p, p->getCards("he"), true);
			JsonArray args;
			args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
			room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
		}
		return slash;
	} else if (user_string.contains("jink") || user_string.contains("Jink")) {
		Jink *jink = new Jink(Card::SuitToBeDecided, -1);
		jink->setSkillName("shouli");
		jink->addSubcards(subcards);
		jink->deleteLater();
		if (player->isLocked(jink)) return nullptr;

		if (room->hasCurrent() && owner) {
			addMark(player, owner);
			room->addPlayerMark(owner, "@skill_invalidity");
			foreach(ServerPlayer *p, room->getAlivePlayers())
				room->filterCards(p, p->getCards("he"), true);
			JsonArray args;
			args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
			room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
		}
		return jink;
	}
	return nullptr;
}

class ShouliVS : public OneCardViewAsSkill
{
public:
	ShouliVS() : OneCardViewAsSkill("shouli")
	{
		expand_pile = "/Horse/shouli";
        response_or_use = true;
	}

	bool viewFilter(const Card *to_select) const
	{
		if (Self->getHandcards().contains(to_select)) return false;

		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY){
			if (!to_select->isKindOf("OffensiveHorse")) return false;
			Slash *slash = new Slash(Card::SuitToBeDecided, -1);
			slash->setSkillName("shouli");
			slash->addSubcard(to_select);
			slash->deleteLater();
			return !Self->isLocked(slash);
		} else {
			QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();

			if (pattern.contains("slash") || pattern.contains("Slash")) {
				if (!to_select->isKindOf("OffensiveHorse")) return false;
				Slash *slash = new Slash(Card::SuitToBeDecided, -1);
				slash->setSkillName("shouli");
				slash->addSubcard(to_select);
				slash->deleteLater();
				return !Self->isLocked(slash);
			} else if (pattern.contains("jink") || pattern.contains("Jink")) {
				if (!to_select->isKindOf("DefensiveHorse")) return false;
				Jink *jink = new Jink(Card::SuitToBeDecided, -1);
				jink->setSkillName("shouli");
				jink->addSubcard(to_select);
				jink->deleteLater();
				return !Self->isLocked(jink);
			}
		}
		return false;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Slash::IsAvailable(player);
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.contains("jink") || pattern.contains("Jink") || pattern.contains("slash") || pattern.contains("Slash");
	}

	const Card *viewAs(const Card *card) const
	{
		ShouliCard *c = new ShouliCard();
		c->setUserString(Sanguosha->getCurrentCardUsePattern());
		c->addSubcard(card);
		return c;
	}
};

class Shouli : public GameStartSkill
{
public:
	Shouli() : GameStartSkill("shouli")
	{
		frequency = Compulsory;
		view_as_skill = new ShouliVS;
	}

	void onGameStart(ServerPlayer *player) const
	{
		Room *room = player->getRoom();
		room->sendCompulsoryTriggerLog(player, this);

		QList<ServerPlayer *> players;
		ServerPlayer *p = player->getNextAlive();
		while (p!=player){
			players << p;
			p = p->getNextAlive();
		}
		players << player;

		foreach (ServerPlayer *to, players) {
			if(to->isDead()) continue;
			QList<int> ids = room->getDrawPile();
			qShuffle(ids);
			foreach (int id, ids) {
				const Card *c = Sanguosha->getCard(id);
				if (c->isKindOf("Horse")&&c->isAvailable(to)){
					room->useCard(CardUseStruct(c, to));
					break;
				}
			}
		}
	}
};

class ShouliBuff : public TriggerSkill
{
public:
	ShouliBuff() : TriggerSkill("#shouli")
	{
		events << ConfirmDamage << EventPhaseChanging << Death;
		frequency =  Compulsory;
	}
	int getPriority(TriggerEvent event) const
	{
		if(event==ConfirmDamage)
			return TriggerSkill::getPriority(event);
		return 5;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==ConfirmDamage){
			if (!room->hasCurrent()) return false;
			DamageStruct damage = data.value<DamageStruct>();
			int mark = damage.to->getMark("&shouli_debuff-Clear");
			if (mark > 0) {
				damage.damage += mark;
				damage.nature = DamageStruct::Thunder;
	
				LogMessage log;
				log.type = "#ZhenguEffect";
				log.from = damage.to;
				log.arg = "shouli";
				room->sendLog(log);
	
				data = QVariant::fromValue(damage);
			}
		}else{
			if (event == EventPhaseChanging) {
				PhaseChangeStruct change = data.value<PhaseChangeStruct>();
				if (change.to != Player::NotActive) return false;
			} else if (event == Death) {
				DeathStruct death = data.value<DeathStruct>();
				if (death.who != player || player != room->getCurrent())
					return false;
			}
			foreach (ServerPlayer *p, room->getAllPlayers(true)) {
				int mark = p->getMark("&shouli_debuff-Clear");
				if (mark<1) continue;
				room->removePlayerMark(p, "@skill_invalidity", mark);
				room->setPlayerMark(p, "&shouli_debuff-Clear", 0);
	
				foreach(ServerPlayer *p2, room->getAllPlayers())
					room->filterCards(p2, p2->getCards("he"), false);
				JsonArray args;
				args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
				room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
			}
		}
		return false;
	}
};

class Hengwu : public TriggerSkill
{
public:
	Hengwu() : TriggerSkill("hengwu")
	{
		events << CardUsed << CardResponded;
		frequency = Frequent;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		const Card *c = nullptr;
		if (event == CardUsed) c = data.value<CardUseStruct>().card;
		else c = data.value<CardResponseStruct>().m_card;
		if (!c || c->isKindOf("SkillCard")) return false;

		foreach (const Card *card, player->getHandcards()) {
			if (card->getSuit() == c->getSuit()) {
				return false;
			}
		}

		int num = 0;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			foreach (const Card *card, p->getEquips()) {
				if (card->getSuit() == c->getSuit())
					num++;
			}
		}
		if (num == 0) return false;

		if (!player->askForSkillInvoke(this, "draw:" + QString::number(num))) return false;
		player->peiyin(this);
		player->drawCards(num, objectName());
		return false;
	}
};

ShencaiCard::ShencaiCard()
{
}

void ShencaiCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();

	JudgeStruct judge;
	judge.who = to;
	judge.reason = "shencai";
	judge.play_animation = false;
	judge.pattern = ".";
	room->judge(judge);

	if (room->CardInPlace(judge.card, Player::DiscardPile))
		room->obtainCard(from, judge.card);

	QStringList marks;
	QString trans = Sanguosha->translate(":" + judge.card->objectName());
	if (trans.contains("体力"))
		marks << "&szfscchi";
	if (trans.contains("武器"))
		marks << "&szfsczhang";
	if (trans.contains("打出"))
		marks << "&szfsctu";
	if (trans.contains("距离"))
		marks << "&szfscliu";

	if (marks.isEmpty()) {
		to->gainMark("&szfscsi");
		if (to->isAllNude()) return;
		int id = room->askForCardChosen(from, to, "hej", "shencai");
		room->obtainCard(from, id, false);
	} else {
		room->setPlayerMark(to, "&szfscchi", 0);
		room->setPlayerMark(to, "&szfsczhang", 0);
		room->setPlayerMark(to, "&szfsctu", 0);
		room->setPlayerMark(to, "&szfscliu", 0);
		foreach (QString mark, marks)
			to->gainMark(mark);
	}
}

class ShencaiVS : public ZeroCardViewAsSkill
{
public:
	ShencaiVS() : ZeroCardViewAsSkill("shencai")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if (player->getMark("SkillDescriptionArg1_shencai") <= 0)
			return !player->hasUsed("ShencaiCard");
		else
			return player->usedTimes("ShencaiCard") < player->getMark("SkillDescriptionArg1_shencai");
	}

	const Card *viewAs() const
	{
		return new ShencaiCard;
	}
};

class Shencai : public TriggerSkill
{
public:
	Shencai() : TriggerSkill("shencai")
	{
		events << Damaged << CardEffected << CardsMoveOneTime << EventPhaseStart << EventPhaseChanging;
		view_as_skill = new ShencaiVS;
		waked_skills = "#shencai";
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	void sendLog(ServerPlayer *player, const QString &mark) const
	{
		LogMessage log;
		log.type = "#ShencaiEffect";
		log.from = player;
		log.arg = mark;
		player->getRoom()->sendLog(log);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == Damaged) {
			DamageStruct damage = data.value<DamageStruct>();
			for (int i = 0; i < player->getMark("&szfscchi"); i++) {
				if (player->isDead()) break;
				sendLog(player, "szfscchi");
				room->loseHp(HpLostStruct(player, damage.damage, objectName(), player));
			}
		} else if (event == CardEffected) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if (player->getMark("&szfsczhang") <= 0 || !effect.card->isKindOf("Slash")) return false;
			sendLog(player, "szfsczhang");
			effect.no_respond = true;
			data = QVariant::fromValue(effect);
		} else if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Finish) return false;
			for (int i = 0; i < player->getMark("&szfscliu"); i++) {
				if (player->isDead()) break;
				sendLog(player, "szfscliu");
				player->turnOver();
			}
		} else if (event == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (!move.from || move.from != player || !move.from_places.contains(Player::PlaceHand) || move.reason.m_skillName == objectName()) return false;
			ServerPlayer *from = (ServerPlayer *)move.from;
			for (int i = 0; i < from->getMark("&szfsctu"); i++) {
				if (from->isDead()) break;

				QList<int> discards;
				foreach (int id, from->handCards()) {
					if (from->canDiscard(from, id))
						discards << id;
				}
				if (discards.isEmpty()) break;

				sendLog(player, "szfsctu");

				int id = discards.at(qrand() % discards.length());
				CardMoveReason reason(CardMoveReason::S_REASON_THROW, from->objectName(), objectName(), "");
				room->throwCard(Sanguosha->getCard(id), reason, from);
			}
		} else if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			if (player->getMark("&szfscsi") <= room->alivePlayerCount()) return false;
			sendLog(player, "szfscsi");
			room->killPlayer(player);
		}
		return false;
	}
};

class ShencaiKeep : public MaxCardsSkill
{
public:
	ShencaiKeep() : MaxCardsSkill("#shencai")
	{
		frequency = NotFrequent;
	}

	int getExtra(const Player *target) const
	{
		int n = -target->getMark("&szfscsi");
		if(target->hasSkill("duzhang"))
			n += target->getMark("&thlin");
		return n;
	}
};

class Xunshi : public FilterSkill
{
public:
	Xunshi() : FilterSkill("xunshi")
	{
		waked_skills = "#xunshi-target,#xunshi";
	}

	bool viewFilter(const Card *to_select) const
	{
		return to_select->isKindOf("TrickCard")
		&& !to_select->isKindOf("SingleTargetTrick") && !to_select->isKindOf("DelayedTrick");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		Slash *slash = new Slash(Card::NoSuit, originalCard->getNumber());
		slash->setSkillName(objectName());/*
		WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
		card->takeOver(slash);*/
		return slash;
	}
};

class XunshiTarget : public TargetModSkill
{
public:
	XunshiTarget() : TargetModSkill("#xunshi-target")
	{
	}

	int getResidueNum(const Player *from, const Card *card, const Player *) const
	{
		if (card->getSuit()>5&&from->hasSkill("xunshi"))
			return 999;
		return 0;
	}

	int getDistanceLimit(const Player *from, const Card *card, const Player *) const
	{
		if (card->getSuit()>5&&from->hasSkill("xunshi"))
			return 999;
		return 0;
	}
};

class XunshiTrigger : public TriggerSkill
{
public:
	XunshiTrigger() : TriggerSkill("#xunshi")
	{
		events << CardFinished << PreCardUsed;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("SkillCard")) return false;

		if (event == CardFinished) {
			if (!player->hasFlag("xunshi"+use.card->toString())) return false;
			player->setFlags("-xunshi"+use.card->toString());
			int mark = player->getMark("SkillDescriptionArg1_shencai");
			if (mark >= 5) return false;
			LogMessage log;
			log.type = "#XunshiAdd";
			log.from = player;
			log.arg = "xunshi";
			log.arg2 = "shencai";
			room->sendLog(log);
			room->addPlayerMark(player, "SkillDescriptionArg1_shencai", mark <= 0 ? 2 : 1);
			mark = player->getMark("SkillDescriptionArg1_shencai");
			player->setSkillDescriptionSwap("shencai", "%arg1", QString::number(mark));
			room->changeTranslation(player, "shencai");
		} else {
			if (use.card->getSuit()<6) return false;
			player->setFlags("xunshi"+use.card->toString());
			LogMessage log;
			log.to = room->getCardTargets(player, use.card, use.to);
			log.to = room->askForPlayersChosen(player, log.to, objectName(), 0, 9, "@xunshi-add:" + use.card->objectName());
			if (log.to.isEmpty()) return false;

			log.type = "#QiaoshuiAdd";
			log.from = player;
			log.card_str = use.card->toString();
			log.arg = "xunshi";
			room->sendLog(log);
			use.to << log.to;
			room->sortByActionOrder(use.to);
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

TuoyuCard::TuoyuCard()
{
	mute = true;
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void TuoyuCard::onUse(Room *, CardUseStruct &) const
{
}

class TuoyuVS : public ViewAsSkill
{
public:
	TuoyuVS() : ViewAsSkill("tuoyu")
	{
	}

	bool viewFilter(const QList<const Card *> &, const Card *to_select) const
	{
		return !to_select->isEquipped() && !to_select->hasTip("sdatyfengtian") &&
				!to_select->hasTip("sdatyqingqu") && !to_select->hasTip("sdatyjunshan");
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty()) return nullptr;
		TuoyuCard *c = new TuoyuCard;
		c->addSubcards(cards);
		return c;
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.startsWith("@@tuoyu");
	}
};

class Tuoyu : public TriggerSkill
{
public:
	Tuoyu() : TriggerSkill("tuoyu")
	{
		events << EventPhaseStart << EventPhaseEnd << PreCardUsed;
		view_as_skill = new TuoyuVS;
		waked_skills = "#tuoyu,#tuoyu-target";
	}

	static QStringList getTuoyuAreas(ServerPlayer *player)
	{
		QStringList areas;
		foreach (QString mark, player->getMarkNames()) {
			if (!mark.startsWith("&") || !mark.endsWith("+#tuoyu") || player->getMark(mark) <= 0) continue;
			if (mark.contains("sdatyfengtian"))
				areas << "sdatyfengtian";
			if (mark.contains("sdatyqingqu"))
				areas << "sdatyqingqu";
			if (mark.contains("sdatyjunshan"))
				areas << "sdatyjunshan";
			break;
		}
		return areas;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == PreCardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard") || !use.m_isHandcard) return false;
			if (use.card->isVirtualCard() || !use.card->getSkillName().isEmpty()) {
				if (use.card->subcardsLength() != 1)
					return false;
			}
			if (use.card->hasTip("sdatyfengtian"))
				room->setCardFlag(use.card, "sdatyfengtian");
			if (use.card->hasTip("sdatyqingqu"))
				room->setCardFlag(use.card, "sdatyqingqu");
			if (use.card->hasTip("sdatyjunshan"))
				room->setCardFlag(use.card, "sdatyjunshan");
		} else {
			if (player->getPhase() != Player::Play || player->isKongcheng()) return false;
			QStringList areas = getTuoyuAreas(player);
			if (areas.isEmpty()) return false;

			room->sendCompulsoryTriggerLog(player, this);

			QList<int> hands = player->handCards();
			foreach (int id, hands) {
				room->setCardTip(id, "-sdatyfengtian");
				room->setCardTip(id, "-sdatyqingqu");
				room->setCardTip(id, "-sdatyjunshan");
			}

			int length = areas.length();

			if (length == 1) {
				QString area = areas.first();
				foreach (int id, hands)
					room->setCardTip(id, area);
			} else if (length == 2) {
				QString area1 = areas.first(), area2 = areas.last();
				QString prompt = "@tuoyu1";

				if (area1 == "sdatyqingqu")
					prompt = "@tuoyu2";
				else if (area1 == "sdatyjunshan")
					prompt = "@tuoyu3";

				const Card *c = room->askForCard(player, "@" + prompt, prompt, data, Card::MethodNone);
				if (c) {
					QList<int> subcards = c->getSubcards();
					foreach (int id, subcards)
						room->setCardTip(id, area1);
					foreach (int id, hands) {
						if (subcards.contains(id)) continue;
						room->setCardTip(id, area2);
					}
				} else {
					foreach (int id, hands)
						room->setCardTip(id, area2);
				}
			} else {
				for (int i = 0; i < 2; i++) {
					if (hands.isEmpty() || player->isDead()) break;

					QString prompt = "@tuoyu" + QString::number(i + 1), area = areas.at(i);
					const Card *c = room->askForCard(player, "@" + prompt, prompt, data, Card::MethodNone);

					if (c) {
						QList<int> subcards = c->getSubcards();
						foreach (int id, subcards) {
							hands.removeOne(id);
							room->setCardTip(id, area);
						}
					}
				}

				if (!hands.isEmpty()) {
					foreach (int id, hands)
						room->setCardTip(id, "sdatyjunshan");
				}
			}
		}
		return false;
	}
};

class TuoyuEffect : public TriggerSkill
{
public:
	TuoyuEffect() : TriggerSkill("#tuoyu")
	{
		events << EventLoseSkill << PreHpRecover << ConfirmDamage << CardUsed;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventLoseSkill) {
			if (data.toString() != "tuoyu") return false;
			QList<int> hands = player->handCards();
			foreach (int id, hands) {
				room->setCardTip(id, "-sdatyfengtian");
				room->setCardTip(id, "-sdatyqingqu");
				room->setCardTip(id, "-sdatyjunshan");
			}
			foreach (QString mark, player->getMarkNames()) {
				if (!mark.endsWith("+#tuoyu") || player->getMark(mark) <= 0) continue;
				room->setPlayerMark(player, mark, 0);
			}
		} else if (event == PreHpRecover) {
			RecoverStruct rec = data.value<RecoverStruct>();
			if (!rec.who || !rec.card) return false;
			if (!rec.card->hasTip("sdatyfengtian") && !rec.card->hasFlag("sdatyfengtian")) return false;

			if (player->getMaxHp() - player->getHp() > rec.recover) {
				rec.recover++;

				LogMessage log;
				log.type = "#TuoyuRecover";
				log.arg = "sdatyfengtian";
				log.arg2 = rec.card->objectName();
				room->sendLog(log);
				rec.who->peiyin("tuoyu");
				room->notifySkillInvoked(rec.who, "tuoyu");
				data = QVariant::fromValue(rec);
			}
		} else if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card) return false;
			if (!damage.card->hasTip("sdatyfengtian") && !damage.card->hasFlag("sdatyfengtian")) return false;

			damage.damage++;

			LogMessage log;
			log.type = "#TuoyuDamage";
			log.arg = "sdatyfengtian";
			log.arg2 = damage.card->objectName();
			room->sendLog(log);
			if (damage.from) {
				damage.from->peiyin("tuoyu");
				room->notifySkillInvoked(damage.from, "tuoyu");
			}
			data = QVariant::fromValue(damage);
		} else if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card || use.card->isKindOf("SkillCard")) return false;

			if (use.card->hasTip("sdatyqingqu") || use.card->hasFlag("sdatyqingqu")) {
				LogMessage log;
				log.type = "#TuoyuQingqu";
				log.arg = "sdatyqingqu";
				log.arg2 = use.card->objectName();
				room->sendLog(log);
				player->peiyin("tuoyu");
				room->notifySkillInvoked(player, "tuoyu");
				use.m_addHistory = false;
				data = QVariant::fromValue(use);
			}

			if (use.card->hasTip("sdatyjunshan") || use.card->hasFlag("sdatyjunshan")) {
				LogMessage log;
				log.type = "#TuoyuJunshan";
				log.arg = "sdatyjunshan";
				log.arg2 = use.card->objectName();
				room->sendLog(log);
				player->peiyin("tuoyu");
				room->notifySkillInvoked(player, "tuoyu");

				use.no_respond_list << "_ALL_TARGETS";
				data = QVariant::fromValue(use);
			}
		}
		return false;
	}
};

class TuoyuTargetMod : public TargetModSkill
{
public:
	TuoyuTargetMod() : TargetModSkill("#tuoyu-target")
	{
		pattern = ".";
	}

	int getResidueNum(const Player *from, const Card *card, const Player *) const
	{
		if (card->hasTip("sdatyqingqu")||card->hasFlag("sdatyqingqu")||card->hasTip("dianlun"))
			return 999;
		if(from->getMark("&dixian")>0&&from->hasSkill("dixian",true)&&from->getMark("&dixian")>=card->getNumber())
			return 999;
		if (card->hasTip("lianjie"))
			return 999;
		if (card->getSkillName()=="peiniang"&&card->isKindOf("Analeptic"))
			return 999;
		if (from->getMark("&chongzhu+1")>0)
			return 999;
		if (card->getSkillName()=="chouxi")
			return 999;
		return 0;
	}

	int getDistanceLimit(const Player *from, const Card *card, const Player *) const
	{
		if (card->hasTip("sdatyqingqu")||card->hasFlag("sdatyqingqu")||card->hasTip("dianlun"))
			return 999;
		if(from->getMark("&dixian")>0&&from->hasSkill("dixian",true)&&from->getMark("&dixian")>=card->getNumber())
			return 999;
		if (card->hasTip("lianjie"))
			return 999;
		if (from->getMark("&chongzhu+1")>0)
			return 999;
		if (card->getSkillName()=="chouxi")
			return 999;
		return 0;
	}

	int getExtraTargetNum(const Player *, const Card *card) const
	{
		if (card->isKindOf("Duel")&&card->getSkillName()=="zhifeng")
			return 1;
		return 0;
	}
};

class Xianjin : public TriggerSkill
{
public:
	Xianjin() : TriggerSkill("xianjin")
	{
		events << Damage << Damaged;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		QStringList areas = Tuoyu::getTuoyuAreas(player);

		QString mark = "&xianjin_damage";
		if (event == Damaged)
			mark = "&xianjin_damaged";
		room->addPlayerMark(player, mark);

		if (player->getMark(mark) >= 2) {
			room->removePlayerMark(player, mark, 2);
			room->sendCompulsoryTriggerLog(player, this);

			if (areas.length() < 3) {
				QStringList choices;
				if (!areas.contains("sdatyfengtian"))
					choices << "sdatyfengtian";
				if (!areas.contains("sdatyqingqu"))
					choices << "sdatyqingqu";
				if (!areas.contains("sdatyjunshan"))
					choices << "sdatyjunshan";
				if (choices.isEmpty()) return false;

				QString choice = room->askForChoice(player, objectName(), choices.join("+"));
				LogMessage log;
				log.type = "#FumianFirstChoice";
				log.from = player;
				log.arg = choice;
				room->sendLog(log);

				areas << choice;

				mark = "&" + areas.join("+") + "+#tuoyu";

				foreach (QString mark, player->getMarkNames()) {
					if (mark.endsWith("+#tuoyu"))
						room->setPlayerMark(player, mark, 0);
				}
				room->setPlayerMark(player, mark, 1);
			}

			int draw = Tuoyu::getTuoyuAreas(player).length(), hand = player->getHandcardNum();

			foreach (ServerPlayer *p, room->getOtherPlayers(player))
				hand = qMax(hand, p->getHandcardNum());
			if (hand == player->getHandcardNum())
				draw = 1;

			player->drawCards(draw, objectName());
		}
		return false;
	}
};

class Qijing : public PhaseChangeSkill
{
public:
	Qijing() : PhaseChangeSkill("qijing")
	{
		frequency = Wake;
		waked_skills = "cuixin";
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive()
		&&target->getPhase() == Player::NotActive;
	}

	bool onPhaseChange(ServerPlayer *, Room *room) const
	{
		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (p->isDead() || p->getMark(objectName()) > 0||!p->hasSkill(objectName())) continue;
			if (p->canWake(objectName()) || Tuoyu::getTuoyuAreas(p).length() >= 3) {
				room->sendCompulsoryTriggerLog(p, this);
				room->doSuperLightbox(p, objectName());
				room->setPlayerMark(p, objectName(), 1);

				if (room->changeMaxHpForAwakenSkill(p, -1, objectName())) {
					ServerPlayer *t = room->askForPlayerChosen(p, room->getOtherPlayers(p), objectName(), "@qijing-move");
					room->doAnimate(1, p->objectName(), t->objectName());
					room->getThread()->delay();

					QList<ServerPlayer *> copy = room->getAllPlayers();
					if (t != copy.first()) {
						foreach (ServerPlayer *player, copy) {
							room->swapSeat(p, player);
							if (player == t)
								break;
						}
					}

					room->acquireSkill(p, "cuixin");
					p->gainAnExtraTurn();
				}
			}
		}
		return false;
	}
};

class Cuixin : public TriggerSkill
{
public:
	Cuixin() : TriggerSkill("cuixin")
	{
		events << CardFinished << CardUsed;
	}

	ServerPlayer *getAdjacentPlayer(ServerPlayer *player, bool next) const
	{
		int seat = player->getSeat(), alive_length = player->aliveCount();
		Room *room = player->getRoom();
		if (next) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getSeat() == seat + 1 || (alive_length == seat && p->getSeat() == 1))
					return p;
			}
		} else {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getSeat() == seat - 1 || (seat == 1 && p->getSeat() == alive_length))
					return p;
			}
		}
		return nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->getSkillNames().contains(objectName())) return false;
		if(event==CardUsed){
			if(use.card->isKindOf("BasicCard") || use.card->isNDTrick()){
				room->setCardFlag(use.card,"cuixinBf");
				player->setTag("cuixinU"+use.card->toString(), use.card->objectName());
			}
			return false;
		}

		if (use.card->hasFlag("cuixinBf")) {
			ServerPlayer *last = getAdjacentPlayer(player, false), *next = getAdjacentPlayer(player, true);

			Card *card = Sanguosha->cloneCard(player->getTag("cuixinU"+use.card->toString()).toString());
			card->setSkillName(objectName());
			card->deleteLater();

			foreach (ServerPlayer *p, use.to) {
				if (player->isDead()) break;

				ServerPlayer *adjacent = nullptr;
				QString prompt = "xiajia:" + card->objectName();
				if (p == last)
					adjacent = next;
				else if (p == next) {
					adjacent = last;
					prompt = "shangjia:" + card->objectName();
				}
				if (!adjacent || adjacent->isDead()) continue;

				if (player->isProhibited(adjacent, card)) continue;

				bool can_invoke = false;

				if (card->targetFixed()) {
					can_invoke = !card->isKindOf("Peach") || p->isWounded();
				} else {
					player->addMark("&chongzhu+1");
					can_invoke = card->targetFilter(QList<const Player *>(), adjacent, player);
					player->removeMark("&chongzhu+1");
				}
				if (!can_invoke||!player->askForSkillInvoke(this, prompt, false)) continue;

				room->useCard(CardUseStruct(card, player, adjacent));
			}
		}
		return false;
	}
};

class Juanjia : public GameStartSkill
{
public:
	Juanjia() : GameStartSkill("juanjia")
	{
		frequency = Compulsory;
	}

	void onGameStart(ServerPlayer *player) const{
		Room *room = player->getRoom();

		room->sendCompulsoryTriggerLog(player, this);
		player->throwEquipArea(1);
		player->addEquipArea(0);

	}
};








TenyearGusheCard::TenyearGusheCard() : GusheCard("tenyeargushe")
{
}

class TenyearGusheVS : public ZeroCardViewAsSkill
{
public:
	TenyearGusheVS() : ZeroCardViewAsSkill("tenyeargushe")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("tenyeargushe_pindian_win-Clear") < 7 - player->getMark("&raoshe") && player->canPindian();
	}

	const Card *viewAs() const
	{
		return new TenyearGusheCard;
	}
};

class TenyearGushe : public TriggerSkill
{
public:
	TenyearGushe() : TriggerSkill("tenyeargushe")
	{
		events << MarkChanged << Pindian;
		view_as_skill = new TenyearGusheVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == MarkChanged) {
			MarkStruct mark = data.value<MarkStruct>();
			if (mark.name == "&raoshe" && player->getMark("&raoshe") >= 7 && player->hasSkill(objectName()))
				room->killPlayer(player);
		} else {
			PindianStruct *pindian = data.value<PindianStruct *>();
			if ((pindian->from == player && pindian->from_number > pindian->to_number)
				|| (pindian->to == player && pindian->to_number > pindian->from_number))
				room->addPlayerMark(player, "tenyeargushe_pindian_win-Clear");
		}
		return false;
	}
};

class TenyearJici : public TriggerSkill
{
public:
	TenyearJici() : TriggerSkill("tenyearjici")
	{
		events << PindianVerifying << Death;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == PindianVerifying) {
			PindianStruct *pindian = data.value<PindianStruct *>();
			QList<ServerPlayer *> pindian_players;
			pindian_players << pindian->from << pindian->to;
			room->sortByActionOrder(pindian_players);

			foreach (ServerPlayer *p, pindian_players) {
				if (p && p->isAlive() && p->hasSkill(objectName())) {
					int n = p->getMark("&raoshe");
					int number = (p == pindian->from) ? pindian->from_number : pindian->to_number;
					if (number <= n) {
						int num = 0;
						if (p == pindian->from) {
							pindian->from_number = qMin(13, pindian->from_number + n);
							num = pindian->from_number;
						} else {
							pindian->to_number = qMin(13, pindian->to_number + n);
							num = pindian->to_number;
						}

						LogMessage log;
						log.type = "#TenyearJiciUp";
						log.from = p;
						log.arg = objectName();
						log.arg2 = QString::number(num);
						room->sendLog(log);
						room->broadcastSkillInvoke(objectName());
						room->notifySkillInvoked(p, objectName());

						data = QVariant::fromValue(pindian);

						QList<int> pindian_ids;
						if (pindian->from_number >= pindian->to_number) {
							if (room->CardInTable(pindian->from_card))
								pindian_ids << pindian->from_card->getEffectiveId();
							if (!pindian_ids.contains(pindian->to_card->getEffectiveId()) && room->CardInTable(pindian->to_card) &&
									pindian->from_number == pindian->to_number)
								pindian_ids << pindian->to_card->getEffectiveId();
						} else {
							if (room->CardInTable(pindian->to_card))
								pindian_ids << pindian->to_card->getEffectiveId();
						}
						if (pindian_ids.isEmpty()) continue;
						DummyCard dummy(pindian_ids);
						room->obtainCard(p, &dummy);
					}
				}
			}
		} else {
			DeathStruct death = data.value<DeathStruct>();
			if (!death.who->hasSkill(objectName()) || player != death.who) return false;
			if (!death.damage || !death.damage->from || death.damage->from->isDead()) return false;
			room->sendCompulsoryTriggerLog(death.who, objectName(), true, true);
			int mark = 7 - death.who->getMark("&raoshe");
			if (mark > 0 && death.damage->from->canDiscard(death.damage->from, "he"))
				room->askForDiscard(death.damage->from, objectName(), mark, mark, false, true);
			if (death.damage->from->isAlive())
				room->loseHp(HpLostStruct(death.damage->from, 1, objectName(), death.who));
		}
		return false;
	}
};

class Qizhou : public TriggerSkill
{
public:
	Qizhou(const QString &skill_name) : TriggerSkill(skill_name), skill_name(skill_name)
	{
		events << CardsMoveOneTime << EventAcquireSkill;
		frequency = Compulsory;
		waked_skills = "mashu,yingzi";
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		bool flag = false;
		if (event == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from && move.from == player && move.from_places.contains(Player::PlaceEquip))
				flag = true;
			if (move.to && move.to == player && move.to_place == Player::PlaceEquip)
				flag = true;
		} else {
			if (data.toString() == objectName())
				flag = true;
		}

		if (flag) {
			int n = QizhouNum(player);
			QStringList get_or_lose, skills = player->getTag(skill_name + "_skills").toStringList();
			if (n >= 1 && !player->hasSkill("mashu", true) && !skills.contains("mashu")) {
				skills << "mashu";
				player->setTag(skill_name + "_skills", skills);
				get_or_lose << "mashu";
			}
			if (n < 1 && player->hasSkill("mashu", true) && skills.contains("mashu")) {
				skills.removeOne("mashu");
				player->setTag(skill_name + "_skills", skills);
				get_or_lose << "-mashu";
			}
			if (n >= 2 && !player->hasSkill("yingzi", true) && !skills.contains("yingzi")) {
				skills << "yingzi";
				player->setTag(skill_name + "_skills", skills);
				get_or_lose << "yingzi";
			}
			if (n < 2 && player->hasSkill("yingzi", true) && skills.contains("yingzi")) {
				skills.removeOne("yingzi");
				player->setTag(skill_name + "_skills", skills);
				get_or_lose << "-yingzi";
			}
			QString duanbing = "duanbing";
			if (skill_name == "olqizhou") duanbing = "olduanbing";
			if (n >= 3 && !player->hasSkill(duanbing, true) && !skills.contains(duanbing)) {
				skills << duanbing;
				player->setTag(skill_name + "_skills", skills);
				get_or_lose << duanbing;
			}
			if (n < 3 && player->hasSkill(duanbing, true) && skills.contains(duanbing)) {
				skills.removeOne(duanbing);
				player->setTag(skill_name + "_skills", skills);
				get_or_lose << "-" + duanbing;
			}
			if (n >= 4 && !player->hasSkill("fenwei", true) && !skills.contains("fenwei")) {
				skills << "fenwei";
				player->setTag(skill_name + "_skills", skills);
				int x = player->property("qizhou_fenwei_got").toInt();
				room->setPlayerProperty(player, "qizhou_fenwei_got", x + 1);
				get_or_lose << "fenwei";
			}
			if (n < 4 && player->hasSkill("fenwei", true) && skills.contains("fenwei")) {
				skills.removeOne("fenwei");
				player->setTag(skill_name + "_skills", skills);
				get_or_lose << "-fenwei";
			}
			if (!get_or_lose.isEmpty()) {
				room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				bool flag = true;
				if (player->property("qizhou_fenwei_got").toInt() > 1)
					flag = false;
				room->handleAcquireDetachSkills(player, get_or_lose, false, flag);
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

private:
	QString skill_name;
};

class QizhouLose : public TriggerSkill
{
public:
	QizhouLose(const QString &skill_name) : TriggerSkill("#" + skill_name + "-lose"), skill_name(skill_name)
	{
		events << EventLoseSkill;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (data.toString() != skill_name) return false;
		QStringList new_list, skills = player->getTag(skill_name + "_skills").toStringList();
		player->removeTag(skill_name + "_skills");
		foreach (QString str, skills) {
			if (player->hasSkill(str, true))
				new_list << "-" + str;
		}
		if (new_list.isEmpty()) return false;
		room->handleAcquireDetachSkills(player, new_list);
		return false;
	}
private:
	QString skill_name;
};

ShanxiCard::ShanxiCard()
{
	target_fixed = true;
}

void ShanxiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	if (source->isDead()) return;
	QList<ServerPlayer *> players;
	foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
		if (source->inMyAttackRange(p) && source->canDiscard(p, "he"))
			players << p;
	}
	if (players.isEmpty()) return;

	ServerPlayer *target = room->askForPlayerChosen(source, players, "shanxi", "@shanxi-choose");
	room->doAnimate(1, source->objectName(), target->objectName());
	int card_id = room->askForCardChosen(source, target, "he", "shanxi", false, Card::MethodDiscard);
	room->throwCard(card_id, "shanxi", target, source);

	ServerPlayer *watcher, *watched;
	if (Sanguosha->getCard(card_id)->isKindOf("Jink")) {
		watcher = source;
		watched = target;
	} else {
		watcher = target;
		watched = source;
	}
	if (!watcher || !watched || watcher->isDead() || watched->isDead()) return;
	if (watched->isKongcheng()) return;
	room->doGongxin(watcher, watched, QList<int>(), "shanxi");
}

class Shanxi : public OneCardViewAsSkill
{
public:
	Shanxi() : OneCardViewAsSkill("shanxi")
	{
		filter_pattern = "BasicCard|red";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("ShanxiCard");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		ShanxiCard *card = new ShanxiCard;
		card->addSubcard(originalCard);
		return card;
	}
};


class Jingyu : public TriggerSkill
{
public:
	Jingyu() : TriggerSkill("jingyu")
	{
		events << ChoiceMade;// << CardUsed << CardResponded;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive()&&target->hasTurn();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == ChoiceMade) {
			QString srt = data.toString();
			if (srt.isEmpty()) return false;
			QStringList srts = srt.split(":");
			if (srts.length()<2||!player->hasSkill(srts[1],true)) return false;
			if (player->hasEquipSkill(srts[1])) return false;
			if(srts[0]=="notifyInvoked"){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(p->hasSkill(objectName())&&p->getMark(srts[1]+"jingyuUse-Clear")<1){
						p->addMark(srts[1]+"jingyuUse-Clear");
						room->sendCompulsoryTriggerLog(p, this);
						p->drawCards(1,objectName());
					}
				}
			}
		} else{
			const Card *card = nullptr;
			if (event == CardUsed) card = data.value<CardUseStruct>().card;
			else card = data.value<CardResponseStruct>().m_card;
			if (!card) return false;
			foreach (QString sk, card->getSkillNames()) {
				if (sk.isEmpty()||!player->hasSkill(sk,true)||player->hasEquipSkill(sk)) continue;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(p->hasSkill(objectName())&&p->getMark(sk+"jingyuUse-Clear")<1){
						p->addMark(sk+"jingyuUse-Clear");
						room->sendCompulsoryTriggerLog(p, this);
						p->drawCards(1,objectName());
					}
				}
			}
		}
		return false;
	}
};

LvxinCard::LvxinCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool LvxinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self;
}

void LvxinCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	room->giveCard(from,to,this,"lvxin");
	int x = room->getTag("TurnLengthCount").toInt();
	if(x>5) x = 5;
	QString n = QString::number(x);
	QString choices = "lvxin1="+n+"+lvxin2="+n;
	choices = room->askForChoice(from,"lvxin",choices,QVariant::fromValue(effect));
	n = Sanguosha->getCard(getEffectiveId())->objectName();
	if(choices.startsWith("lvxin1")){
		foreach (int id, to->drawCardsList(x,"lvxin")) {
			if(Sanguosha->getCard(id)->sameNameWith(n)){
				room->setPlayerMark(to,"&lvxin_huifu+#"+from->objectName(),1);
				break;
			}
		}
	}else{
		QList<int> ids = to->handCards();
		qShuffle(ids);
		DummyCard *dummy = new DummyCard();
		foreach (int id, ids) {
			if(dummy->subcardsLength()<x&&to->canDiscard(to,id))
				dummy->addSubcard(id);
		}
		if(dummy->subcardsLength()>0){
			room->throwCard(dummy,"lvxin",to);
			foreach (int id, dummy->getSubcards()) {
				if(Sanguosha->getCard(id)->sameNameWith(n)){
					room->setPlayerMark(to,"&lvxin_shiqu+#"+from->objectName(),1);
					break;
				}
			}
		}
		delete dummy;
	}
}

class Lvxinvs : public OneCardViewAsSkill
{
public:
	Lvxinvs() : OneCardViewAsSkill("lvxin")
	{
		filter_pattern = ".";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("LvxinCard");
	}

	const Card *viewAs(const Card *originalCard) const
	{
		LvxinCard *card = new LvxinCard;
		card->addSubcard(originalCard);
		return card;
	}
};

class Lvxin : public TriggerSkill
{
public:
	Lvxin() : TriggerSkill("lvxin")
	{
		events << ChoiceMade;// << CardUsed << CardResponded;
		view_as_skill = new Lvxinvs;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == ChoiceMade) {
			QString srt = data.toString();
			if (srt.isEmpty()) return false;
			QStringList srts = srt.split(":");
			if (srts.length()<2||!player->hasSkill(srts[1],true)) return false;
			if(srts[0]!="notifyInvoked") return false;
			if (player->hasEquipSkill(srts[1])) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(player->getMark("&lvxin_shiqu+#"+p->objectName())>0){
					room->setPlayerMark(player,"&lvxin_shiqu+#"+p->objectName(),0);
					room->loseHp(player,1,true,p,objectName());
				}
				if(player->getMark("&lvxin_huifu+#"+p->objectName())>0){
					room->setPlayerMark(player,"&lvxin_huifu+#"+p->objectName(),0);
					room->recover(player,RecoverStruct(objectName(),p));
				}
			}
		} else{
			const Card *card = nullptr;
			if (event == CardUsed) card = data.value<CardUseStruct>().card;
			else card = data.value<CardResponseStruct>().m_card;
			if (!card) return false;
			foreach (QString sk, card->getSkillNames()) {
				if (sk.isEmpty()||!player->hasSkill(sk,true)) continue;
				if (player->hasEquipSkill(sk)) continue;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(player->getMark("&lvxin_shiqu+#"+p->objectName())>0){
						room->setPlayerMark(player,"&lvxin_shiqu+#"+p->objectName(),0);
						room->loseHp(player,1,true,p,objectName());
					}
					if(player->getMark("&lvxin_huifu+#"+p->objectName())>0){
						room->setPlayerMark(player,"&lvxin_huifu+#"+p->objectName(),0);
						room->recover(player,RecoverStruct(objectName(),p));
					}
				}
			}
		}
		return false;
	}
};

HuandaoCard::HuandaoCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool HuandaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self;
}

void HuandaoCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	room->removePlayerMark(from, "@huandaoMark");
	room->doSuperLightbox(from, "huandao");
	if (!to->faceUp()) to->turnOver();
	room->setPlayerChained(to, false);
	QStringList generals, sks, lgn = Sanguosha->getLimitedGeneralNames();
	QHash<QString, QStringList> lgns;
	foreach (QString gn, lgn) {
		foreach (QString gn2, lgn) {
			if(gn2.endsWith(gn))
				lgns[gn] << gn2;
		}
	}
	QString general = to->getGeneralName();
	foreach (QString key, lgns.keys()){
		if (lgns[key].length()>1&&lgns[key].contains(general)){
			generals << lgns[key];
			generals.removeOne(general);
		}
	}
	general = to->getGeneral2Name();
	if(!general.isEmpty()){
		foreach (QString key, lgns.keys()){
			if (lgns[key].length()>1&&lgns[key].contains(general)){
				generals << lgns[key];
				generals.removeOne(general);
			}
		}
	}
	foreach (QString gn, generals) {
		const General*gt = Sanguosha->getGeneral(gn);
		if (!gt) continue;
		foreach (const Skill *skill, gt->getVisibleSkillList()) {
			if (sks.contains(skill->objectName())) continue;
			sks << skill->objectName();
		}
	}
	if(sks.isEmpty()) return;
	sks << "cancel";
	general = room->askForChoice(to,"huandao_acquire",sks.join("+"));
	if(general=="cancel") return;
	generals.clear();
	generals << general;
	sks.clear();
	foreach (const Skill *skill, to->getVisibleSkillList()) {
		if (sks.contains(skill->objectName())) continue;
		sks << skill->objectName();
	}
	if(sks.length()>0){
		general = room->askForChoice(to,"huandao_detach",sks.join("+"));
		generals << "-"+general;
	}
	room->handleAcquireDetachSkills(to,generals);
}

class Huandao : public ZeroCardViewAsSkill
{
public:
	Huandao() : ZeroCardViewAsSkill("huandao")
	{
		frequency = Limited;
		limit_mark = "@huandaoMark";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@huandaoMark")>0;
	}

	const Card *viewAs() const
	{
		return new HuandaoCard;
	}
};

class Lieqiong : public TriggerSkill
{
public:
	Lieqiong() : TriggerSkill("lieqiong")
	{
		events << CardUsed << DamageForseen << Damage;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()<1||player->getMark("&lqjishang+:+lq_zhongshu-SelfClear")<1) return false;
			room->sendCompulsoryTriggerLog(player,objectName());
			room->setPlayerMark(player,"&lqjishang+:+lq_zhongshu-SelfClear",0);
			use.nullified_list << "_ALL_TARGETS";
			data.setValue(use);
		} else if (event == DamageForseen){
			DamageStruct damage = data.value<DamageStruct>();
			if(player->getMark("&lqjishang+:+lq_diji-SelfClear")>0){
				room->sendCompulsoryTriggerLog(player,objectName());
				player->damageRevises(data,1);
				room->setPlayerMark(player,"&lqjishang+:+lq_diji-SelfClear",0);
			}
		} else if (event == Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.to!=player&&damage.to->isAlive()&&player->hasSkill(objectName())
				&&player->askForSkillInvoke(this,damage.to)){
				player->peiyin(this);
				QStringList choices;
				if(damage.to->getMark("lieqiongDamage-Clear")>0)
					choices << "lq_tianchong";
				if(damage.to->canDiscard(damage.to,"h"))
					choices << "lq_lifeng";
				choices << "lq_diji" << "lq_zhongshu" << "lq_qihai";
				QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
				if(choice=="lq_lifeng"){
					QList<int>ids = damage.to->handCards();
					qShuffle(ids);
					Card*dc = dummyCard();
					foreach (int id, ids) {
						if(damage.to->canDiscard(damage.to,id))
							dc->addSubcard(id);
						if(dc->subcardsLength()>=ids.length()/2.0) break;
					}
					room->throwCard(dc,objectName(),damage.to);
				}else if(choice=="lq_tianchong"){
					room->loseHp(damage.to,damage.to->getHp(),true,damage.to,objectName());
					if(damage.to->isDead())
						room->gainMaxHp(player,1,objectName());
				}else{
					room->setPlayerMark(damage.to,"&lqjishang+:+"+choice+"-SelfClear",1);
					if(choice=="lq_qihai")
						room->setPlayerCardLimitation(damage.to,"use,response",".|heart",true);
				}
				damage.to->addMark("lieqiongDamage-Clear");
			}
		}
		return false;
	}
};

class ThZhanjue : public TriggerSkill
{
public:
	ThZhanjue() : TriggerSkill("thzhanjue")
	{
		events << CardUsed << EventPhaseStart << Damage;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive() && target->getPhase()==Player::Play;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash")||player->getMark("thzhanjue1-PlayClear")<1) return false;
			room->sendCompulsoryTriggerLog(player,objectName());
			room->setPlayerMark(player,"thzhanjue1-PlayClear",0);
			use.no_respond_list << "_ALL_TARGETS";
			data.setValue(use);
		} else if (event == EventPhaseStart){
			if(player->hasSkill(objectName())&&player->askForSkillInvoke(this)){
				player->peiyin(this);
				if(player->getLostHp()>0&&room->askForChoice(player,objectName(),"thzhanjue1+thzhanjue2")=="thzhanjue2"){
					player->addMark("thzhanjue2-PlayClear");
					player->drawCards(player->getLostHp(),objectName());
				}else{
					room->addPlayerMark(player,"thzhanjue1-PlayClear");
					player->drawCards(player->getHp(),objectName());
				}
			}
		} else if (event == Damage){
			if(player->getMark("thzhanjue2-PlayClear")>0){
				room->sendCompulsoryTriggerLog(player,objectName());
				player->setMark("thzhanjue2-PlayClear",0);
				DamageStruct damage = data.value<DamageStruct>();
				room->recover(player,RecoverStruct(objectName(),player,damage.damage));
			}
		}
		return false;
	}
};

class Luansuo : public TriggerSkill
{
public:
	Luansuo() : TriggerSkill("luansuo")
	{
		events << EventPhaseStart << CardsMoveOneTime;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if(player->getPhase() == Player::RoundStart&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					p->addMark("luansuoBf");
					room->setPlayerCardLimitation(p,"discard",".|.|.|hand",true);
					foreach (const Card*c, p->getHandcards()) {
						Card*dc = Sanguosha->cloneCard("iron_chain",c->getSuit(),c->getNumber());
						dc->setSkillName(objectName());
						WrappedCard *card = Sanguosha->getWrappedCard(c->getId());
						card->takeOver(dc);
						room->notifyUpdateCard(p, c->getId(), card);
					}
				}
			}else if(player->getPhase() == Player::NotActive){
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->getMark("luansuoBf")<1) continue;
					p->setMark("luansuoBf",0);
					room->removePlayerCardLimitation(p,"discard",".|.|.|hand$1");
					QList<const Card*>hs;
					foreach (const Card*h, p->getHandcards()) {
						if(h->getSkillName()==objectName())
							hs << h;
					}
					room->filterCards(p,hs,true);
				}
			}
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to_place==Player::DiscardPile && player->hasFlag("CurrentPlayer")&&player->hasSkill(objectName(),true)) {
				foreach (int id, move.card_ids) {
					const Card*c = Sanguosha->getCard(id);
					if(player->getMark(c->getSuitString()+"luansuoSuit-Clear")<1){
						player->addMark(c->getSuitString()+"luansuoSuit-Clear");
						foreach (ServerPlayer *p, room->getAlivePlayers()) {
							QList<const Card*>hs;
							foreach (const Card*h, p->getHandcards()) {
								if(h->getSkillName()==objectName()&&h->getSuit()==c->getSuit())
									hs << h;
							}
							room->filterCards(p,hs,true);
						}
					}
				}
			}
		}
		return false;
	}
};

class Fengliao : public TriggerSkill
{
public:
	Fengliao() : TriggerSkill("fengliao")
	{
		events << TargetSpecified;
		frequency = Compulsory;
		change_skill = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()<1||use.to.length()!=1) return false;
			room->sendCompulsoryTriggerLog(player,this);
			if(player->getChangeSkillState(objectName())==1){
				room->setChangeSkillState(player, objectName(), 2);
				use.to.last()->drawCards(1,objectName());
			}else{
				room->setChangeSkillState(player, objectName(), 1);
				room->damage(DamageStruct(objectName(),player,use.to.last(),1,DamageStruct::Fire));
			}
		}
		return false;
	}
};

class Kunyu : public TriggerSkill
{
public:
	Kunyu() : TriggerSkill("kunyu")
	{
		events << AskForPeachesDone << MaxHpChange << DrawNCards;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == AskForPeachesDone){
			DyingStruct dy = data.value<DyingStruct>();
			if (dy.who!=player||player->getHp()>0) return false;
			foreach (int id, room->getDrawPile()) {
				const Card*c = Sanguosha->getCard(id);
				if(c->isDamageCard()){
					if(c->objectName().startsWith("fire_")||c->objectName().startsWith("huo")){
						room->sendCompulsoryTriggerLog(player,this);
						room->breakCard(c,player);
						room->recover(player,RecoverStruct(objectName(),player,1-player->getHp()));
						break;
					}
				}
			}
		}else if(event == MaxHpChange){
			MaxHpStruct maxhp = data.value<MaxHpStruct>();
			room->sendCompulsoryTriggerLog(player,this);
			if(player->getMaxHp()==1){
				return true;
			}else{
				maxhp.change = 1-player->getMaxHp();
				data.setValue(maxhp);
			}
		}else{
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="InitialHandCards") return false;
			if(player->getMaxHp()!=1)
				room->setPlayerProperty(player,"maxhp",1);
		}
		return false;
	}
};

class Chaozhen : public TriggerSkill
{
public:
	Chaozhen() : TriggerSkill("chaozhen")
	{
		events << Dying << EventPhaseStart;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == Dying){
			DyingStruct dy = data.value<DyingStruct>();
			if (dy.who!=player) return false;
		}else if(player->getPhase()!=Player::Start)
			return false;
		if(player->getMark("chaozhenBan-Clear")<1&&player->askForSkillInvoke(this)){
			player->peiyin(this);
			QList<const Card*>cs = player->getCards("ej");
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				cs << p->getCards("ej");
			}
			qShuffle(cs);
			const Card*nc = cs.last();
			foreach (const Card*c, cs) {
				if(c->getNumber()<nc->getNumber())
					nc = c;
			}
			foreach (int id, room->getDrawPile()) {
				const Card*c = Sanguosha->getCard(id);
				if(nc==nullptr||c->getNumber()<nc->getNumber())
					nc = c;
			}
			player->obtainCard(nc);
			if(nc->getNumber()==1){
				room->recover(player,RecoverStruct(objectName(),player));
				player->addMark("chaozhenBan-Clear");
			}else
				room->loseMaxHp(player,1,objectName());
		}
		return false;
	}
};

class Lianjie : public TriggerSkill
{
public:
	Lianjie() : TriggerSkill("lianjie")
	{
		events << TargetSpecified;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()<1||!use.m_isHandcard||player->isKongcheng()) return false;
			foreach (const Card*c, player->getHandcards()) {
				if(use.card->getNumber()>c->getNumber())
					return false;
			}
			if(player->getMark(use.card->getNumberString()+"lianjieNumber-Clear")<1
			&&player->getHandcardNum()<player->getMaxHp()&&player->askForSkillInvoke(this,data)){
				player->peiyin(this);
				player->addMark(use.card->getNumberString()+"lianjieNumber-Clear");
				foreach (int id, player->drawCardsList(player->getMaxHp()-player->getHandcardNum(),objectName())) {
					if(player->handCards().contains(id)){
						room->setCardTip(id,"lianjie-Clear");
					}
				}
			}
		}
		return false;
	}
};

JiangxianCard::JiangxianCard()
{
	target_fixed = true;
}

void JiangxianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->removePlayerMark(source, "@jiangxian");
	room->doSuperLightbox(source, "jiangxian");
	if(room->askForChoice(source,"jiangxian","jiangxian1+jiangxian2")=="jiangxian1"){
		room->detachSkillFromPlayer(source,"chaozhen");
		int n = 5-source->getMaxHp();
		if(n>0) room->gainMaxHp(source,n,"jiangxian");
		n = 5-source->getMaxCards();
		if(n>0) room->addMaxCards(source,n,false);
	}else
		source->addMark("jiangxian2-Clear");
}

class JiangxianVs : public ZeroCardViewAsSkill
{
public:
	JiangxianVs() : ZeroCardViewAsSkill("jiangxian")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@jiangxian")>0;
	}

	const Card *viewAs() const
	{
		return new JiangxianCard;
	}
};

class Jiangxian : public TriggerSkill
{
public:
	Jiangxian() : TriggerSkill("jiangxian")
	{
		events << EventPhaseChanging << DamageCaused << PreCardUsed << DamageDone;
		view_as_skill = new JiangxianVs;
		frequency = Limited;
		limit_mark = "@jiangxian";
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->hasTip("lianjie")&&player->getMark("jiangxian2-Clear")>0)
				room->setCardFlag(use.card,"jiangxianDamage");
		} else if (event == EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to==Player::NotActive&&player->getMark("jiangxian2-Clear")>0){
				room->detachSkillFromPlayer(player,"lianjie");
			}
		} else if (event == DamageCaused){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("jiangxianDamage")){
				int n = player->getMark("jiangxianDamage-Clear");
				if(n>0){
					room->sendCompulsoryTriggerLog(player,objectName());
					player->damageRevises(data,qMin(5,n));
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from) damage.from->addMark("jiangxianDamage-Clear");
		}
		return false;
	}
};

class Yitong : public TriggerSkill
{
public:
	Yitong() : TriggerSkill("yitong")
	{
		events << GameStart << CardsMoveOneTime;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == GameStart) {
			room->sendCompulsoryTriggerLog(player,this);
			QString suit = Card::Suit2String(room->askForSuit(player,objectName()));
			room->setPlayerMark(player,"&yitong+:+"+suit+"_char",1);
			room->setPlayerProperty(player,"yitongSuit",suit);
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to_place==Player::DiscardPile&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE&&player->getMark("yitongUse-Clear")<1) {
				foreach (int id, move.card_ids) {
					const Card*c = Sanguosha->getCard(id);
					if(c->getSuitString()==player->property("yitongSuit").toString()){
						room->sendCompulsoryTriggerLog(player,this);
						player->addMark("yitongUse-Clear");
						QList<int> ids = room->getDrawPile();
						ids << room->getDiscardPile();
						qShuffle(ids);
						QStringList suits;
						suits << c->getSuitString();
						Card*sc = dummyCard();
						foreach (int did, ids) {
							const Card*dc = Sanguosha->getCard(did);
							if(suits.contains(dc->getSuitString())) continue;
							suits.append(dc->getSuitString());
							sc->addSubcard(did);
						}
						player->obtainCard(sc);
						break;
					}
				}
			}
		}
		return false;
	}
};

class Peiniang : public OneCardViewAsSkill
{
public:
	Peiniang() : OneCardViewAsSkill("peiniang")
	{
	}
	bool viewFilter(const Card *to_select) const
	{
		if(Sanguosha->getCurrentCardUsePattern().contains("peach"))
			return to_select->isKindOf("Analeptic")||Self->property("yitongSuit").toString()==to_select->getSuitString();
		return Self->property("yitongSuit").toString()==to_select->getSuitString();
	}
	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return pattern.contains("peach")&&player->getCardCount()>0;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getCardCount()>0;
	}

	const Card *viewAs(const Card *oc) const
	{
		if(Sanguosha->getCurrentCardUsePattern().contains("peach")&&oc->isKindOf("Analeptic"))
			return oc;
		Card *card = Sanguosha->cloneCard("analeptic");
		card->setSkillName("peiniang");
		card->addSubcard(oc);
		return card;
	}
};

class ThShenduan : public TriggerSkill
{
public:
	ThShenduan() : TriggerSkill("thshenduan")
	{
		events << AskforPindianCard << Pindian;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	const Card *getDrawCard(Room *room, bool max) const
	{
		if(max){
			int n = 0;
			foreach (int id, room->getDrawPile()) {
				const Card*c = Sanguosha->getCard(id);
				if(c->getNumber()>n) n = c->getNumber();
			}
			foreach (int id, room->getDrawPile()) {
				const Card*c = Sanguosha->getCard(id);
				if(c->getNumber()>=n)
					return c;
			}
		}else{
			int n = 998;
			foreach (int id, room->getDrawPile()) {
				const Card*c = Sanguosha->getCard(id);
				if(c->getNumber()<n) n = c->getNumber();
			}
			foreach (int id, room->getDrawPile()) {
				const Card*c = Sanguosha->getCard(id);
				if(c->getNumber()<=n)
					return c;
			}
		}
		return nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		if (event == AskforPindianCard) {
			PindianStruct *pd = data.value<PindianStruct*>();
			if(pd->from->hasSkill(objectName())&&pd->from->canDiscard(pd->from,"he")
				&&room->askForCard(pd->from,"..","thshenduan0",data,objectName())){
				pd->from->peiyin(this);
				pd->from_card = getDrawCard(room,true);
				data.setValue(pd);
			} 
			if(pd->to->hasSkill(objectName())&&pd->to->canDiscard(pd->to,"he")
				&&room->askForCard(pd->to,"..","thshenduan0",data,objectName())){
				pd->to->peiyin(this);
				pd->to_card = getDrawCard(room,true);
				data.setValue(pd);
			}
		} else {
			PindianStruct *pd = data.value<PindianStruct*>();
			if (pd->from_card->getNumber()==13) {
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					if(p->hasSkill(objectName())&&pd->from->isAlive()&&p->askForSkillInvoke(this,pd->from)){
						p->peiyin(this);
						const Card*c = getDrawCard(room,false);
						if(c) room->moveCardTo(c,p,Player::PlaceHand,CardMoveReason(CardMoveReason::S_REASON_DRAW,p->objectName(),objectName(),""),false);
						c = getDrawCard(room,false);
						if(c) room->moveCardTo(c,pd->from,Player::PlaceHand,CardMoveReason(CardMoveReason::S_REASON_DRAW,pd->from->objectName(),objectName(),""),false);
						
					}
				}
			}
			if (pd->to_card->getNumber()==13) {
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					if(p->hasSkill(objectName())&&pd->to->isAlive()&&p->askForSkillInvoke(this,pd->to)){
						p->peiyin(this);
						const Card*c = getDrawCard(room,false);
						if(c) room->moveCardTo(c,p,Player::PlaceHand,CardMoveReason(CardMoveReason::S_REASON_DRAW,p->objectName(),objectName(),""),false);
						c = getDrawCard(room,false);
						if(c) room->moveCardTo(c,pd->to,Player::PlaceHand,CardMoveReason(CardMoveReason::S_REASON_DRAW,pd->to->objectName(),objectName(),""),false);
					}
				}
			}
			if(pd->from_number>pd->to_number&&room->getCardOwner(pd->from_card->getEffectiveId())==nullptr){
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->hasSkill(objectName())){
						room->moveCardsToEndOfDrawpile(pd->from,pd->from_card->getSubcards(),objectName(),true);
						break;
					}
				}
			}else if(pd->from_number<pd->to_number&&room->getCardOwner(pd->to_card->getEffectiveId())==nullptr){
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->hasSkill(objectName())){
						room->moveCardsToEndOfDrawpile(pd->to,pd->to_card->getSubcards(),objectName(),true);
						break;
					}
				}
			}
		}
		return false;
	}
};

ThKegouCard::ThKegouCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool ThKegouCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && Self->canPindian(to_select);
}

void ThKegouCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	Room *room = from->getRoom();
	if(from->canPindian(to)){
		PindianStruct*pd = from->PinDian(to,"thkegou");
		if(pd->success){
			QList<int>nts;
			int x = qMin(3,pd->from_number-pd->to_number);
			foreach (int id, room->getDrawPile()) {
				const Card*c = Sanguosha->getCard(id);
				if(nts.contains(c->getNumber())) continue;
				nts.append(c->getNumber());
			}
			Card*dc = dummyCard();
			foreach (int id, room->getDrawPile()) {
				int n = 998;
				foreach (int nt, nts){
					if(nt<n) n = nt;
				}
				const Card*c = Sanguosha->getCard(id);
				if(c->getNumber()<=n){
					dc->addSubcard(id);
					nts.removeOne(n);
					if(dc->subcardsLength()>=x) break;
				}
			}
			from->obtainCard(dc);
		}else{
			Card*dc = Sanguosha->cloneCard("slash");
			dc->setSkillName("_thkegou");
			dc->deleteLater();
			if(to->canSlash(from,dc,false))
				room->useCard(CardUseStruct(dc,to,from));
			if(from->canPindian())
				room->askForUseCard(from,"@@thkegou","thkegou0");
		}
	}
}

class ThKegouVs : public ZeroCardViewAsSkill
{
public:
	ThKegouVs() : ZeroCardViewAsSkill("thkegou")
	{
		response_pattern = "@@thkegou";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("ThKegouCard")<1&&player->canPindian();
	}

	const Card *viewAs() const
	{
		return new ThKegouCard;
	}
};

class ThKegou : public TriggerSkill
{
public:
	ThKegou() : TriggerSkill("thkegou")
	{
		events << CardUsed << CardResponded << EventPhaseChanging;
		view_as_skill = new ThKegouVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()<1) return false;
			player->addMark("thkegouNum-Clear");
		}else if(event == CardResponded){
			CardResponseStruct res = data.value<CardResponseStruct>();
			if (res.m_card->getTypeId()<1) return false;
			player->addMark("thkegouNum-Clear");
		}else if(event == EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if(p->getMark("thkegouNum-Clear")>0&&!p->hasFlag("CurrentPlayer")
				&&p->canPindian()&&p->hasSkill(objectName())){
					room->askForUseCard(p,"@@thkegou","thkegou1");
				}
			}
		}
		return false;
	}
};

DixianCard::DixianCard()
{
	target_fixed = true;
}

void DixianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->removePlayerMark(source, "@dixian");
	room->doSuperLightbox(source, "dixian");
	int n = room->askForChoice(source,"dixian","1+2+3+4+5+6+7+8+9+10+11+12+13").toInt();
	bool has = false;
	foreach (int id, room->getDrawPile()) {
		const Card*c = Sanguosha->getCard(id);
		if(c->getNumber()<n){
			has = true;
			break;
		}
	}
	if(has){
		Card*dc = dummyCard();
		foreach (int id, room->getDrawPile()+room->getDiscardPile()) {
			const Card*c = Sanguosha->getCard(id);
			if(c->getNumber()==13){
				dc->addSubcard(id);
			}
		}
		source->obtainCard(dc);
	}else{
		source->drawCards(n,"dixian");
		room->setPlayerMark(source,"&dixian",n);
	}
}

class Dixian : public ZeroCardViewAsSkill
{
public:
	Dixian() : ZeroCardViewAsSkill("dixian")
	{
		frequency = Limited;
		limit_mark = "@dixian";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@dixian")>0;
	}

	const Card *viewAs() const
	{
		return new DixianCard;
	}
};

class ThLinjie : public TriggerSkill
{
public:
	ThLinjie() : TriggerSkill("thlinjie")
	{
		events << RoundStart << Damaged;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (event == RoundStart){
			if (player->hasSkill(objectName())){
				QList<ServerPlayer *> tps;
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->getMark("&thlin")<1) tps << p;
				}
				ServerPlayer *tp = room->askForPlayerChosen(player,tps,objectName(),"thlinjie0",true,true);
				if(tp){
					player->peiyin(this);
					room->damage(DamageStruct(objectName(),player,tp));
					tp->gainMark("&thlin");
				}
			}
		}else if(event == Damaged){
			if(player->getMark("&thlin")>0){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(p->hasSkill(objectName())&&player->canDiscard(player,"h")){
						room->sendCompulsoryTriggerLog(p,objectName());
						QList<const Card*>hs = player->getHandcards();
						qShuffle(hs);
						foreach (const Card*h, hs) {
							if(player->canDiscard(player,h->getId())){
								room->throwCard(h,objectName(),player);
								if(hs.length()==1){
									room->damage(DamageStruct(objectName(),p,player));
									player->loseAllMarks("&thlin");
								}
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

class Duzhang : public TriggerSkill
{
public:
	Duzhang() : TriggerSkill("duzhang")
	{
		events << TargetSpecified << TargetConfirmed;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if(use.card->getTypeId()<1||use.to.length()!=1||!player->hasTurn()) return false;
		if(event == TargetConfirmed){
			if (!use.to.contains(player)) return false;
		}
		if(use.card->isBlack()&&player->getMark("duzhangUse-Clear")<1){
			player->addMark("duzhangUse-Clear");
			room->sendCompulsoryTriggerLog(player,this);
			player->gainMark("&thlin");
		}
		return false;
	}
};

class Jianghuo : public TriggerSkill
{
public:
	Jianghuo() : TriggerSkill("jianghuo")
	{
		events << EventPhaseStart;
		waked_skills = "lishi";
		frequency = Wake;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (event == EventPhaseStart){
			if(player->getPhase()==Player::RoundStart&&player->getMark("thlinjie")<1){
				bool can = true;
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->getMark("jianghuoDamage")<1) can = false;
				}
				if(can||player->canWake(objectName())){
					room->addPlayerMark(player,"thlinjie");
					room->sendCompulsoryTriggerLog(player,this);
					room->doSuperLightbox(player, objectName());
					foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
						int n = p->getMark("&thlin");
						if(n<1) continue;
						room->doAnimate(1,p->objectName(),player->objectName());
						p->loseAllMarks("&thlin");
						player->gainMark("&thlin",n);
						if(player->isDead()) return false;
					}
					player->drawCards(player->getMark("&thlin"),objectName());
					room->gainMaxHp(player,1,objectName());
					room->handleAcquireDetachSkills(player,"-thlinjie|lishi");
				}
			}
		}
		return false;
	}
};

class JianghuoBf : public TriggerSkill
{
public:
	JianghuoBf() : TriggerSkill("#JianghuoBf")
	{
		events << Damaged;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const
	{
		if(event == Damaged){
			player->addMark("jianghuoDamage");
		}
		return false;
	}
};

class Lishi : public TriggerSkill
{
public:
	Lishi() : TriggerSkill("lishi")
	{
		events << EventPhaseStart;
		waked_skills = "#lishibf,#lishibf2";
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart){
			if(player->getPhase()==Player::Finish){
				room->sendCompulsoryTriggerLog(player,this);
				if(player->getMark("&thlin")<1){
					room->damage(DamageStruct(objectName(),nullptr,player,1,DamageStruct::Thunder));
				}else{
					QStringList choices,has;
					choices << "lishi1" << "lishi2" << "lishi3" << "lishi4" << "lishi5";
					for (int i = 0; i < qMin(5,player->getMark("&thlin")); i++) {
						QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
						if(choice=="cancel") break;
						choices.removeOne(choice);
						if(i==0) choices << "cancel";
						has << choice;
						foreach (ServerPlayer *p, room->getOtherPlayers(player))
							p->setMark(choice,1);
					}
					player->loseMark("&thlin",has.length());
					foreach (QString ch, has) {
						LogMessage log;
						log.type = "#lishiLog";
						log.from = player;
						log.arg = ch;
						room->sendLog(log);
					}
				}
			}
		}
		return false;
	}
};

class LishiBf : public TriggerSkill
{
public:
	LishiBf() : TriggerSkill("#lishibf")
	{
		events << EventPhaseChanging << EventPhaseProceeding << CardUsed << BeforeCardsMove << AfterDrawNCards;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseChanging){
				PhaseChangeStruct change = data.value<PhaseChangeStruct>();
				if((change.to==Player::Start||change.to==Player::Finish)&&player->getMark("lishi1")>0){
					room->addPlayerMark(player,"@skill_invalidity");
				}
				if((change.from==Player::Start||change.from==Player::Finish)&&player->getMark("lishi1")>0){
					if(change.from==Player::Finish) player->setMark("lishi1",0);
					room->removePlayerMark(player,"@skill_invalidity");
				}
				if(change.from==Player::Play&&player->getMark("lishi4")>0){
					player->setMark("lishi4",0);
				}
				if(change.from==Player::Discard&&player->getMark("lishi5")>0){
					player->setMark("lishi5",0);
				}
		}else if (event == AfterDrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="draw_phase"||player->getMark("lishi3")<1) return false;
			player->setMark("lishi3",0);
			foreach (int id, draw.card_ids) {
				if(Sanguosha->getCard(id)->getColor()!=Sanguosha->getCard(draw.card_ids.first())->getColor())
					return false;
				if(player->isJilei(Sanguosha->getCard(id)))
					draw.card_ids.removeOne(id);
			}
			room->throwCard(draw.card_ids,"lishi",player);
		}else if (event == BeforeCardsMove){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.from==player&&player->getMark("lishi5")>0&&player->getPhase()==Player::Discard
			&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(p->hasSkill("lishi",true)){
						move.to_place = Player::PlaceHand;
						move.to = p;
						data.setValue(move);
					}
				}
			}
		}else if (event == CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()<1||player->getMark("lishi4")<1||player->getPhase()!=Player::Play) return false;
			room->addPlayerMark(player,use.card->getType()+"lishi4-PlayClear");
		}else if(event == EventPhaseProceeding){
			if(player->getPhase()==Player::Judge&&player->getMark("lishi2")>0){
				player->setMark("lishi2",0);
				QStringList choices,has;
				choices << "lightning" << "indulgence" << "supply_shortage";
				for (int i = 0; i < 2; i++) {
					QString choice = room->askForChoice(player,"lishi2",choices.join("+"),data);
					choices.removeOne(choice);
					has << choice;
				}
				CardEffectStruct effect;
				effect.to = player;
				foreach (QString cn, has) {
					effect.card = Sanguosha->cloneCard(cn);
					effect.card->onEffect(effect);
					delete effect.card;
					if(player->isDead()) break;
				}
			}
		}
		return false;
	}
};

class LishiBf2 : public CardLimitSkill
{
public:
	LishiBf2() : CardLimitSkill("#lishibf2")
	{
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target, const Card *c) const
	{
		if (target->getMark(c->getType()+"lishi4-PlayClear")>0)
			return c->toString();
		return "";
	}
};

class Kanyu : public TriggerSkill
{
public:
	Kanyu() : TriggerSkill("kanyu")
	{
		events << StartJudge << DamageInflicted;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==StartJudge){
			JudgeStruct *judge = data.value<JudgeStruct*>();
			QString jp = judge->pattern,kanyu_p = player->getTag("kanyu_p").toString();
			if(!kanyu_p.isEmpty()&&judge->reason=="lightning"){
				judge->pattern += kanyu_p;
				data.setValue(judge);
			}
			foreach (ServerPlayer *p, room->getAllPlayers()){
				if(p->isAlive()&&p->hasSkill(objectName())&&p->askForSkillInvoke(objectName(),data)){
					p->peiyin(objectName());
					QList<int>ids = room->getNCards(1);
					ids << room->getNCards(1,true,false);
					Card*dc = dummyCard();
					room->fillAG(ids,p);
					kanyu_p = p->getTag("kanyu_p").toString();
					for (int i = 0; i < 2; i++) {
						int id = room->askForAG(p,ids,true,objectName(),"kanyu0");
						if(id<0) break;
						ids.removeOne(id);
						dc->addSubcard(id);
						room->takeAG(p,id,false,QList<ServerPlayer *>()<<p);
						const Card*c = Sanguosha->getCard(id);
						kanyu_p += "#.|"+c->getSuitString()+"|"+c->getNumberString();
					}
					room->clearAG(p);
					p->setTag("kanyu_p", kanyu_p);
					p->obtainCard(dc,false);
					if(ids.isEmpty()) continue;
					if(p->isAlive()){
						room->askForGuanxing(p,ids,Room::GuanxingUpOnly);
					}else
						room->returnToTopDrawPile(ids);
				}
			}
			kanyu_p = player->getTag("kanyu_p").toString();
			if(kanyu_p.isEmpty()||judge->reason!="lightning") return false;
			judge->pattern = jp+kanyu_p;
			data.setValue(judge);
		}else{
			if(player->hasSkill(objectName())&&player->askForSkillInvoke(objectName(),data)){
				player->peiyin(objectName());
				QList<int>ids = room->getNCards(1);
				ids << room->getNCards(1,true,false);
				Card*dc = dummyCard();
				room->fillAG(ids,player);
				QString kanyu_p = player->getTag("kanyu_p").toString();
				for (int i = 0; i < 2; i++) {
					int id = room->askForAG(player,ids,true,objectName(),"kanyu0");
					if(id<0) break;
					ids.removeOne(id);
					dc->addSubcard(id);
					room->takeAG(player,id,false,QList<ServerPlayer *>()<<player);
					const Card*c = Sanguosha->getCard(id);
					kanyu_p += "#.|"+c->getSuitString()+"|"+c->getNumberString();
				}
				room->clearAG(player);
				player->setTag("kanyu_p", kanyu_p);
				player->obtainCard(dc,false);
				if(ids.isEmpty()) return false;
				if(player->isAlive()){
					room->askForGuanxing(player,ids,Room::GuanxingUpOnly);
				}else
					room->returnToTopDrawPile(ids);
			}
		}
		return false;
	}
};

class Zhee : public TriggerSkill
{
public:
	Zhee() : TriggerSkill("zhee")
	{
		events << GameStart << EventPhaseChanging;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==GameStart){
			if(player->hasSkill(objectName())){
				foreach (int id, room->getDrawPile()){
					Card*c = Sanguosha->getCard(id);
					if(c->isKindOf("Lightning")){
						if(player->isProhibited(player,c))continue;
						room->sendCompulsoryTriggerLog(player,this);
						room->moveCardTo(c,player,Player::PlaceDelayedTrick,true);
						break;
					}
				}
			}
		}else{
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()){
				if(p->isAlive()&&p->hasSkill(objectName())){
					foreach (int id, room->getDiscardPile()){
						Card*c = Sanguosha->getCard(id);
						if(c->isKindOf("Lightning")){
							if(p->isProhibited(p,c))continue;
							room->sendCompulsoryTriggerLog(p,this);
							room->moveCardTo(c,p,Player::PlaceDelayedTrick,true);
							break;
						}
					}
				}
			}
		}
		return false;
	}
};

class Cangming : public TriggerSkill
{
public:
	Cangming() : TriggerSkill("cangming")
	{
		events << CardsMoveOneTime << Damaged << EventPhaseChanging << AfterDrawNCards;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_pile_name=="cang_ming"&&player->hasSkill(objectName())){
				QStringList Types;
				foreach (int id, move.card_ids){
					Card*c = Sanguosha->getCard(id);
					if(Types.contains(c->getType())) continue;
					Types.append(c->getType());
				}
				room->sendCompulsoryTriggerLog(player,this);
				player->drawCards(Types.length(),objectName());
			}
		}else if(event==AfterDrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="InitialHandCards"||!player->hasSkill(objectName())) return false;
			room->sendCompulsoryTriggerLog(player,this);
			foreach (ServerPlayer *p, room->getAlivePlayers())
				room->doAnimate(1,player->objectName(),p->objectName());
			foreach (ServerPlayer *p, room->getAllPlayers())
				p->addToPile("cang_ming",p->handCards());
		}else{
			if(event==EventPhaseChanging){
				if (data.value<PhaseChangeStruct>().to!=Player::RoundStart) return false;
			}
			QList<int>ids = player->getPile("cang_ming");
			if(ids.isEmpty()) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()){
				if(p->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(p,objectName());
					player->obtainCard(dummyCard(ids));
					break;
				}
			}
		}
		return false;
	}
};

ChouxiCard::ChouxiCard()
{
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void ChouxiCard::onUse(Room *room, CardUseStruct &use) const
{
	QStringList cns;
	foreach (ServerPlayer *p, room->getAlivePlayers()){
		foreach (int id, p->getPile("cang_ming")){
			Card*c = Sanguosha->getCard(id);
			if(cns.contains(c->objectName())||use.from->getMark(c->objectName()+"chouxiUse-Clear")>0) continue;
			if(c->isKindOf("BasicCard")||c->isNDTrick()){
				Card*dc = Sanguosha->cloneCard(c->objectName());
				dc->setSkillName("chouxi");
				if(dc->isAvailable(use.from))
					cns.append(c->objectName());
				dc->deleteLater();
			}
		}
	}
	if(cns.isEmpty()) return;
	QString cn = room->askForChoice(use.from,"chouxi",cns.join("+"));
	room->setPlayerProperty(use.from,"chouxiCn",cn);
	if(room->askForUseCard(use.from,"@@chouxi","chouxi0:"+cn))
		room->addPlayerMark(use.from,cn+"chouxiUse-Clear");
}

class Chouxi : public ViewAsSkill
{
public:
	Chouxi() : ViewAsSkill("chouxi")
	{
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *to_select) const
	{
		if(cards.isEmpty()&&Sanguosha->getCurrentCardUsePattern()=="@@chouxi"){
			Card*dc = Sanguosha->cloneCard(Self->property("chouxiCn").toString());
			dc->setSkillName("chouxi");
			dc->addSubcard(to_select);
			dc->deleteLater();
			return dc->isAvailable(Self);
		}
		return false;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(Sanguosha->getCurrentCardUsePattern()=="@@chouxi"){
			if(cards.isEmpty()) return nullptr;
			Card*dc = Sanguosha->cloneCard(Self->property("chouxiCn").toString());
			dc->setSkillName("chouxi");
			dc->addSubcards(cards);
			return dc;
		}
		return new ChouxiCard();
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getCardCount()>0;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.contains("@@chouxi");
	}
};

JichaoCard::JichaoCard()
{
}

bool JichaoCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&Self!=to;
}

bool JichaoCard::targetsFeasible(const QList<const Player *> &, const Player *) const
{
	return true;
}

void JichaoCard::onUse(Room *room, CardUseStruct &use) const
{
	if(use.to.isEmpty()){
		use.to = room->getOtherPlayers(use.from);
		setFlags("getOtherPlayers");
	}
	SkillCard::onUse(room,use);
}

void JichaoCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		if(hasFlag("getOtherPlayers")){
			QList<int>ids = p->handCards()+p->getEquipsId();
			p->addToPile("cang_ming",ids);
			p->setTag(source->objectName()+"jichaoToPile", ListI2V(ids));
			source->setMark("jichaoNum",3);
		}else{
			QList<int>ids,hids = p->handCards();
			qShuffle(hids);
			foreach (int id, hids) {
				ids << id;
				if(ids.length()>=hids.length()/2)
					break;
			}
			ids << p->getEquipsId();
			p->addToPile("cang_ming",ids);
		}
	}
}

class Jichaovs : public ZeroCardViewAsSkill
{
public:
	Jichaovs() : ZeroCardViewAsSkill("jichao")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("JichaoCard")<1;
	}

	const Card *viewAs() const
	{
		return new JichaoCard;
	}
};

class Jichao : public TriggerSkill
{
public:
	Jichao() : TriggerSkill("jichao")
	{
		events << DamageComplete;
		view_as_skill = new Jichaovs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		if(event==DamageComplete){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from&&damage.from->getMark("jichaoNum")>0){
				damage.from->removeMark("jichaoNum",damage.damage);
				if(damage.from->getMark("jichaoNum")>0) return false;
				room->sendCompulsoryTriggerLog(damage.from,objectName());
				foreach (ServerPlayer *p, room->getOtherPlayers(damage.from)) {
					QList<int>ids = p->getPile("cang_ming");
					if(ids.isEmpty()) continue;
					Card*dc = dummyCard();
					foreach (int id, ListV2I(p->getTag(damage.from->objectName()+"jichaoToPile").toList())) {
						if(ids.contains(id)) dc->addSubcard(id);
					}
					p->obtainCard(dc);
				}
			}
		}
		return false;
	}
};





TenyearXdPackage::TenyearXdPackage()
	: Package("tenyear_xd")
{

//神·武
	General *shenjiangwei = new General(this, "shenjiangwei*xd_shenwu", "god");
	shenjiangwei->addSkill(new Tianren);
	shenjiangwei->addSkill(new Jiufa);
	shenjiangwei->addSkill(new Pingxiang);
	shenjiangwei->addSkill(new PingxiangMaxCards);
	related_skills.insertMulti("pingxiang", "#pingxiang");
	addMetaObject<PingxiangCard>();

	General *shenmachao = new General(this, "shenmachao*xd_shenwu", "god");
	shenmachao->addSkill(new Shouli);
	shenmachao->addSkill(new ShouliBuff);
	shenmachao->addSkill(new Hengwu);
	related_skills.insertMulti("shouli", "#shouli");
	addMetaObject<ShouliCard>();

	General *shenzhangfei = new General(this, "shenzhangfei*xd_shenwu", "god");
	shenzhangfei->addSkill(new Shencai);
	shenzhangfei->addSkill(new ShencaiKeep);
	shenzhangfei->addSkill(new Xunshi);
	shenzhangfei->addSkill(new XunshiTarget);
	shenzhangfei->addSkill(new XunshiTrigger);
	addMetaObject<ShencaiCard>();

	General *shendengai = new General(this, "shendengai*xd_shenwu", "god");
	shendengai->addSkill(new Tuoyu);
	shendengai->addSkill(new TuoyuEffect);
	shendengai->addSkill(new TuoyuTargetMod);
	shendengai->addSkill(new Xianjin);
	shendengai->addSkill(new Qijing);
	addMetaObject<TuoyuCard>();
	skills << new Cuixin;

	General *shenhuatuo = new General(this, "shenhuatuo*xd_shenwu", "god", 3);
	shenhuatuo->addSkill(new Jingyu);
	shenhuatuo->addSkill(new Lvxin);
	shenhuatuo->addSkill(new Huandao);
	addMetaObject<LvxinCard>();
	addMetaObject<HuandaoCard>();

	General *shenhuangzhong = new General(this, "shenhuangzhong*xd_shenwu", "god", 4);
	shenhuangzhong->addSkill(new Lieqiong);
	shenhuangzhong->addSkill(new ThZhanjue);

	General *shenpangtong = new General(this, "shenpangtong*xd_shenwu", "god", 1);
	shenpangtong->addSkill(new Luansuo);
	shenpangtong->addSkill(new Fengliao);
	shenpangtong->addSkill(new Kunyu);

	General *shenzhonghui = new General(this, "shenzhonghui*xd_shenwu", "god");
	shenzhonghui->addSkill(new ThLinjie);
	shenzhonghui->addSkill(new Duzhang);
	shenzhonghui->addSkill(new Jianghuo);
	shenzhonghui->addSkill(new JianghuoBf);
	skills << new Lishi << new LishiBf << new LishiBf2;

	General *th_shensunquan = new General(this, "th_shensunquan*xd_shenwu", "god");
	th_shensunquan->addSkill(new Cangming);
	th_shensunquan->addSkill(new Chouxi);
	th_shensunquan->addSkill(new Jichao);
	addMetaObject<ChouxiCard>();
	addMetaObject<JichaoCard>();





	General *heqi = new General(this, "heqi", "wu", 4);
	heqi->addSkill(new Qizhou("qizhou"));
	heqi->addSkill(new QizhouLose("qizhou"));
	heqi->addSkill(new Shanxi);
	heqi->addRelateSkill("duanbing");
	heqi->addRelateSkill("fenwei");
	related_skills.insertMulti("qizhou", "#qizhou-lose");
	related_skills.insertMulti("olqizhou", "#olqizhou-lose");
	skills << new Qizhou("olqizhou") << new QizhouLose("olqizhou");
	addMetaObject<ShanxiCard>();

//祈福
	General *tenyear_baosanniang = new General(this, "tenyear_baosanniang*qifu", "shu", 3, false);
	tenyear_baosanniang->addSkill("wuniang");
	tenyear_baosanniang->addSkill(new TenyearXushen);
	tenyear_baosanniang->addRelateSkill("tenyearzhennan");

	General *second_tenyear_baosanniang = new General(this, "second_tenyear_baosanniang*qifu", "shu", 3, false);
	second_tenyear_baosanniang->addSkill(new SecondWuniang);
	second_tenyear_baosanniang->addSkill(new SecondXushen);
	second_tenyear_baosanniang->addRelateSkill("secondzhennan");
	skills << new TenyearZhennan << new SecondZhennan;

	General *tenyear_zhaoxiang = new General(this, "tenyear_zhaoxiang*qifu", "shu", 4, false);
	tenyear_zhaoxiang->addSkill("tenyearfanghun");
	tenyear_zhaoxiang->addSkill(new TenyearFuhan);

	General *tenyear_guansuo = new General(this, "tenyear_guansuo*qifu", "shu", 4);
	tenyear_guansuo->addSkill("xiefang");
	tenyear_guansuo->addSkill(new TenyearZhengnan);

	General *tenyear_zhangqiying = new General(this, "tenyear_zhangqiying*qifu", "qun", 3, false);
	tenyear_zhangqiying->addSkill("falu");
	tenyear_zhangqiying->addSkill(new TenyearZhenyi);
	tenyear_zhangqiying->addSkill("dianhua");

//隐山之玉
	General *zhouyi = new General(this, "zhouyi*xd_yinyu", "wu", 3, false);
	zhouyi->addSkill(new Zhukou);
	zhouyi->addSkill(new Mengqing);
	zhouyi->addRelateSkill("yuyun");
	zhouyi->addSkill("#jingce-record");
	related_skills.insertMulti("zhukou", "#jingce-record");
	related_skills.insertMulti("yuyun", "#yuyun");
	addMetaObject<ZhukouCard>();
	skills << new Yuyun << new YuyunTargetMod;

	General *pangfengyi = new General(this, "pangfengyi", "shu", 3, false);
	pangfengyi->addSkill(new Yitong);
	pangfengyi->addSkill(new Peiniang);

	General *guotiying = new General(this, "guotiying", "wei", 3,false);
	guotiying->addSkill(new Kanyu);
	guotiying->addSkill(new Zhee);

//高山仰止
	General *tenyear_wanglang = new General(this, "tenyear_wanglang", "wei", 3);
	tenyear_wanglang->addSkill(new TenyearGushe);
	tenyear_wanglang->addSkill(new TenyearJici);
	addMetaObject<TenyearGusheCard>();

//武庙
	General *wumiao_zhugeliang = new General(this, "wumiao_zhugeliang", "shu", 7);
	wumiao_zhugeliang->setStartHp(4);
	wumiao_zhugeliang->addSkill(new MYJincui);
	wumiao_zhugeliang->addSkill(new Qingshi);
	wumiao_zhugeliang->addSkill(new QingshiDamage);
	wumiao_zhugeliang->addSkill(new Zhizhe);
	wumiao_zhugeliang->addSkill(new ZhizheLimit);
	addMetaObject<ZhizheCard>();
	skills << new ZhizheFilter;

	General *wumiao_huangfusong = new General(this, "wumiao_huangfusong", "qun");
	wumiao_huangfusong->addSkill(new Chaozhen);
	wumiao_huangfusong->addSkill(new Lianjie);
	wumiao_huangfusong->addSkill(new Jiangxian);
	addMetaObject<JiangxianCard>();

	General *wumiao_lukang = new General(this, "wumiao_lukang", "wu");
	wumiao_lukang->addSkill(new ThShenduan);
	wumiao_lukang->addSkill(new ThKegou);
	wumiao_lukang->addSkill(new Dixian);
	addMetaObject<ThKegouCard>();
	addMetaObject<DixianCard>();








}
ADD_PACKAGE(TenyearXd)


class Ruijun : public TriggerSkill
{
public:
	Ruijun() :TriggerSkill("ruijun")
	{
		events << TargetSpecified << ConfirmDamage << Damage << EventPhaseChanging;
	}
	bool triggerable(const ServerPlayer *player) const
	{
		return player&&player->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event == TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getPhase()==Player::Play&&player->getMark("ruijunUse-PlayClear")<1){
				use.to.removeOne(player);
				if(use.to.length()>0){
					player->addMark("ruijunUse-PlayClear");
					if(player->hasSkill(objectName())){
						ServerPlayer *tp = room->askForPlayerChosen(player,use.to,objectName(),"ruijun0",true,true);
						if(tp){
							player->peiyin(this);
							player->drawCards(player->getLostHp()+1,objectName());
							QStringList tps = player->property("BanAttackRange").toString().split("+");
							foreach (ServerPlayer *p, room->getAlivePlayers()){
								if(p!=tp) tps << p->objectName();
							}
							room->insertAttackRangePair(player,tp);
							room->setPlayerProperty(player,"BanAttackRange",tps.join("+"));
							room->addPlayerMark(tp,"&ruijun+#"+player->objectName()+"-PlayClear");
						}
					}
				}
			}
		}else if(event == ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			int n = player->getMark(damage.to->objectName()+"ruijundamage-PlayClear");
			if(n>0){
				damage.damage = qMin(5,n+1);
				data.setValue(damage);
			}
		}else if(event == EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to != Player::Play) return false;
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				if(p->getMark("&ruijun+#"+player->objectName()+"-PlayClear")>0){
					room->removeAttackRangePair(player,p);
					QStringList tps = player->property("BanAttackRange").toString().split("+");
					foreach (ServerPlayer *tp, room->getAlivePlayers()){
						if(p!=tp) tps.removeOne(tp->objectName());
					}
					room->setPlayerProperty(player,"BanAttackRange",tps.join("+"));
				}
			}
		}else if(event == Damage){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.to->getMark("&ruijun+#"+player->objectName()+"-PlayClear")>0)
				room->setPlayerMark(player,damage.to->objectName()+"ruijundamage-PlayClear",damage.damage);
		}
		return false;
	}
};

class Gangyi : public TriggerSkill
{
public:
	Gangyi() :TriggerSkill("gangyi")
	{
		events << PreHpRecover << Damage;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *player) const
	{
		return player&&player->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event == PreHpRecover){
			RecoverStruct rec = data.value<RecoverStruct>();
			if(rec.card&&(rec.card->isKindOf("Peach")||rec.card->isKindOf("Analeptic"))
				&&player->hasFlag("Global_Dying")&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				rec.recover++;
				data.setValue(rec);
			}
		}else if(event == Damage){
			if(player->hasFlag("CurrentPlayer"))
				room->addPlayerMark(player,"gangyiCanPeach-Clear");
		}
		return false;
	}
};

class GangyiLimit : public CardLimitSkill
{
public:
	GangyiLimit() : CardLimitSkill("#GangyiLimit")
	{
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target) const
	{
		if (target->hasFlag("CurrentPlayer")&&target->getMark("gangyiCanPeach-Clear")<1&&target->hasSkill("gangyi"))
			return "Peach";
		return "";
	}
};

class ThZhiji : public TriggerSkill
{
public:
	ThZhiji() :TriggerSkill("thzhiji")
	{
		events << EventPhaseStart << CardUsed;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if(player->getPhase()==Player::Start&&player->hasSkill(objectName())&&player->canDiscard(player,"h")){
				const Card*dc = room->askForDiscard(player,objectName(),99,1,true,false,"thzhiji0:",".",objectName());
				if(dc){
					player->peiyin(this);
					int n = dc->subcardsLength();
					int x = 5-player->getHandcardNum();
					if(x>0) player->drawCards(x,objectName());
					if(n>x){
						x = n-x;
						QList<ServerPlayer *>tps = room->askForPlayersChosen(player,room->getOtherPlayers(player),objectName(),0,x,QString("thzhiji1:%1").arg(x));
						if(tps.length()>0){
							foreach (ServerPlayer *p, tps) {
								room->doAnimate(1,player->objectName(),p->objectName());
							}
							foreach (ServerPlayer *p, tps)
								room->damage(DamageStruct(objectName(),player,p));
						}
					}else if(n==x){
						room->addPlayerMark(player,"&tyzhiji-Clear");
					}else{
						room->addMaxCards(player,2,true);
					}
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->getMark("&tyzhiji-Clear")>0){
				room->sendCompulsoryTriggerLog(player,objectName());
				use.no_respond_list << "_ALL_TARGETS";
				data.setValue(use);
			}
		}
		return false;
	}
};

class Anji : public TriggerSkill
{
public:
	Anji() :TriggerSkill("anji")
	{
		events << CardFinished;
		frequency = Compulsory;
		global = true;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		if (event == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					p->addMark(use.card->getSuitString()+"anjiSuit_lun");
					if(p->hasSkill(objectName())){
						int n = p->getMark(use.card->getSuitString()+"anjiSuit_lun");
						foreach (QString m, p->getMarkNames()){
							if(m.contains("anjiSuit_lun")&&p->getMark(m)>0){
								if(m.contains(use.card->getSuitString())) continue;
								if(p->getMark(m)<n) return false;
							}
						}
						room->sendCompulsoryTriggerLog(p,this);
						p->drawCards(1,objectName());
					}
				}
			}
		}
		return false;
	}
};

ZhongyanCard::ZhongyanCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool ZhongyanCard::targetFixed() const
{
	return Sanguosha->getCurrentCardUsePattern()=="@@zhongyan!";
}

bool ZhongyanCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return targets.isEmpty();
}

void ZhongyanCard::onUse(Room *room, CardUseStruct &use) const
{
	if(Sanguosha->getCurrentCardUsePattern()!="@@zhongyan!")
		SkillCard::onUse(room,use);
	else{
		QList<CardsMoveStruct>moves;
		foreach (int id, subcards){
			if(use.from->handCards().contains(id)){
				moves << CardsMoveStruct(id, nullptr, Player::DrawPile,CardMoveReason(CardMoveReason::S_REASON_RECYCLE, use.from->objectName()));
			}else{
				moves << CardsMoveStruct(id, use.from, Player::PlaceHand,CardMoveReason(CardMoveReason::S_REASON_GOTBACK, use.from->objectName()));
			}
		}
		room->moveCardsAtomic(moves, false);
	}
}

void ZhongyanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets){
		QList<int>ids = room->showDrawPile(source,3,"zhongyan",false);
		if(p->getHandcardNum()>0){
			room->notifyMoveToPile(p,ids,"zhongyan");
			const Card*c = room->askForUseCard(p,"@@zhongyan!","zhongyan0");
			if(c){
				foreach (int id, c->getSubcards()){
					if(ids.contains(id))
						ids.removeOne(id);
					else
						ids.append(id);
				}
				foreach (int id, ids){
					c = Sanguosha->getCard(id);
					if(c->getColor()!=Sanguosha->getCard(ids.last())->getColor()){
						ids.clear();
						break;
					}
				}
				if(ids.length()>0&&p->isAlive()){
					QList<ServerPlayer *> tps;
					foreach (ServerPlayer *tp, room->getAlivePlayers()){
						if(tp->getCards("ej").length()>0) tps << tp;
					}
					ServerPlayer *tp = room->askForPlayerChosen(p,tps,"zhongyan","zhongyan1",true);
					if(tp){
						room->doAnimate(1,p->objectName(),tp->objectName());
						int id = room->askForCardChosen(p,tp,"ej","zhongyan");
						if(id>-1) room->obtainCard(p,id);
					}else
						room->recover(p,RecoverStruct("zhongyan",source));
					if(p!=source&&source->isAlive()){
						if(tp)
							room->recover(source,RecoverStruct("zhongyan",source));
						else{
							tp = room->askForPlayerChosen(source,tps,"zhongyan","zhongyan1");
							if(tp){
								room->doAnimate(1,source->objectName(),tp->objectName());
								int id = room->askForCardChosen(source,tp,"ej","zhongyan");
								if(id>-1) room->obtainCard(source,id);
							}
						}
					}
				}
			}
		}
	}
}

class Zhongyan : public ViewAsSkill
{
public:
	Zhongyan() : ViewAsSkill("zhongyan")
	{
		expand_pile = "#zhongyan";
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *to_select) const
	{
		if(Sanguosha->getCurrentCardUsePattern()!="@@zhongyan!"||to_select->isEquipped())
			return false;
		if(cards.length()==1)
			return Self->getPileName(to_select->getId())!=Self->getPileName(cards.last()->getId());
		return cards.isEmpty();
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(Sanguosha->getCurrentCardUsePattern()!="@@zhongyan!")
			return new ZhongyanCard();
		if (cards.length()>1){
			Card*c = new ZhongyanCard();
			c->addSubcards(cards);
			return c;
		}
		return nullptr;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("ZhongyanCard")<1;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.contains("@@zhongyan");
	}
};

class Jinglun : public TriggerSkill
{
public:
	Jinglun() :TriggerSkill("jinglun")
	{
		events << Damage;
	}
	bool triggerable(const ServerPlayer *player) const
	{
		return player&&player->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(event == Damage){
			foreach (ServerPlayer *p, room->getAllPlayers()){
				if(p->getMark("jinglunUse-Clear")<1&&p->hasTurn()&&p->hasSkill(objectName())
					&&p->distanceTo(player)<2&&p->askForSkillInvoke(objectName()+"$-1",player)){
					p->addMark("jinglunUse-Clear");
					player->drawCards(player->getEquips().length(),objectName());
					room->useCard(CardUseStruct(new ZhongyanCard(),p,player));
				}
			}
		}
		return false;
	}
};

SayingCard::SayingCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool SayingCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return false;
	Card *card = Sanguosha->cloneCard(user_string.split("+").last());
	card->deleteLater();
	return card->targetFilter(targets, to_select, Self);
}

bool SayingCard::targetFixed() const
{
	if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").last());
	card->deleteLater();
	return card->targetFixed();
}

bool SayingCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").last());
	card->deleteLater();
	return card->targetsFeasible(targets, Self);
}

const Card *SayingCard::validate(CardUseStruct &use) const
{
	QStringList choices = user_string.split("+");
	foreach (QString cn, choices){
		if(use.from->getMark("saying_juguan_remove_"+cn+"_lun")>0)
			choices.removeOne(cn);
	}
	if(choices.isEmpty()) return nullptr;
	Room *room = use.from->getRoom();
	QString pattern = room->askForChoice(use.from,"saying",choices.join("+"));
	room->addPlayerMark(use.from,"saying_juguan_remove_"+pattern+"_lun");
	if(pattern.contains("peach")||pattern.contains("analeptic")) use.from->obtainCard(this);
	else room->useCard(CardUseStruct(Sanguosha->getCard(getEffectiveId()),use.from));
	Card *c = Sanguosha->cloneCard(pattern);
	c->setSkillName("saying");
	c->deleteLater();
	return c;
}

const Card *SayingCard::validateInResponse(ServerPlayer *source) const
{
	QStringList choices = user_string.split("+");
	foreach (QString cn, choices){
		if(source->getMark("saying_juguan_remove_"+cn+"_lun")>0)
			choices.removeOne(cn);
	}
	if(choices.isEmpty()) return nullptr;
	Room *room = source->getRoom();
	QString pattern = room->askForChoice(source,"saying",choices.join("+"));
	room->addPlayerMark(source,"saying_juguan_remove_"+pattern+"_lun");
	if(pattern.contains("peach")||pattern.contains("analeptic")) source->obtainCard(this);
	else room->useCard(CardUseStruct(Sanguosha->getCard(getEffectiveId()),source));
	Card *c = Sanguosha->cloneCard(pattern);
	c->setSkillName("saying");
	c->deleteLater();
	return c;
}

class Saying : public OneCardViewAsSkill
{
public:
	Saying() : OneCardViewAsSkill("saying")
	{
		response_or_use = true;
	}

	QDialog *getDialog() const
	{
		return JuguanDialog::getInstance("saying", "slash,peach,analeptic");
	}

	bool viewFilter(const Card *to_select) const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			const Card *c = Self->getTag("saying").value<const Card *>();
			if (c) pattern = c->objectName();
			else return false;
		}
		if(pattern.contains("peach")||pattern.contains("analeptic")) return to_select->isEquipped();
		return !to_select->isEquipped()&&to_select->isKindOf("EquipCard")&&to_select->isAvailable(Self);
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if(Sanguosha->getCurrentCardUseReason()!=CardUseStruct::CARD_USE_REASON_RESPONSE_USE||player->getCardCount()<1)
			return false;
		if(pattern.contains("peach")){
			return player->getMark("saying_juguan_remove_peach_lun")<1&&player->hasEquip();
		}else if(pattern.contains("analeptic")){
			return player->getMark("saying_juguan_remove_analeptic_lun")<1&&player->hasEquip();
		}else if(pattern.contains("slash")){
			return player->getMark("saying_juguan_remove_slash_lun")<1&&player->getHandcardNum()>0;
		}else if(pattern.contains("jink")){
			return player->getMark("saying_juguan_remove_jink_lun")<1&&player->getHandcardNum()>0;
		}
		return false;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getCardCount()>0;
	}

	const Card *viewAs(const Card *originalCard) const
	{
		if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
			SayingCard *card = new SayingCard;
			card->setUserString(Sanguosha->getCurrentCardUsePattern());
			card->addSubcard(originalCard);
			return card;
		}

		const Card *c = Self->getTag("saying").value<const Card *>();
		if (c) {
			SayingCard *card = new SayingCard;
			card->setUserString(c->objectName());
			card->addSubcard(originalCard);
			return card;
		}
		return nullptr;
	}
};

JiaohaoCard::JiaohaoCard()
{
}

bool JiaohaoCard::targetFilter(const QList<const Player *> &targets, const Player *to, const Player *Self) const
{
	return targets.isEmpty()&&Self!=to
	&&Self->getEquips().length()>=to->getEquips().length()&&Self->canPindian(to);
}

void JiaohaoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		if(source->canPindian(p)){
			PindianStruct *pd = source->PinDian(p,"jiaohao");
			if(pd->success){
				if(source->isAlive()&&source->askForSkillInvoke("jiaohao",source)){
					if(room->askForUseCard(source,"Slash","jiaohao0:"))
						continue;
					Card*dc = dummyCard();
					if(!room->getCardOwner(pd->from_card->getEffectiveId()))
						dc->addSubcard(pd->from_card);
					if(!room->getCardOwner(pd->to_card->getEffectiveId()))
						dc->addSubcard(pd->to_card);
					source->obtainCard(dc);
				}
			}else if(pd->to_number>pd->from_number){
				if(source->isAlive()&&p->isAlive()&&source->askForSkillInvoke("jiaohao",p)){
					if(room->askForUseCard(p,"Slash","jiaohao0:"))
						continue;
					Card*dc = dummyCard();
					if(!room->getCardOwner(pd->from_card->getEffectiveId()))
						dc->addSubcard(pd->from_card);
					if(!room->getCardOwner(pd->to_card->getEffectiveId()))
						dc->addSubcard(pd->to_card);
					p->obtainCard(dc);
				}
			}
		}
	}
}

class Jiaohao : public ZeroCardViewAsSkill
{
public:
	Jiaohao() : ZeroCardViewAsSkill("jiaohao")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("JiaohaoCard")<1&&player->canPindian();
	}

	const Card *viewAs() const
	{
		return new JiaohaoCard;
	}
};

ShimouCard::ShimouCard()
{
	handling_method = Card::MethodUse;
}

bool ShimouCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return false;
	QStringList pnts = Self->property("shimouPN").toString().split(":");
	Card *card = Sanguosha->cloneCard(pnts.last());
	card->setSkillName("shimou");
	card->deleteLater();
	foreach (const Player *p, Self->getAliveSiblings(true)){
		if(pnts[1]==p->objectName())
			return card->targetFilter(targets, to_select, p);
	}
	return card->targetFilter(targets, to_select, Self);
}

bool ShimouCard::targetFixed() const
{
	if (user_string.isEmpty()||!Self) return true;
	QStringList pnts = Self->property("shimouPN").toString().split(":");
	Card *card = Sanguosha->cloneCard(pnts.last());
	card->setSkillName("shimou");
	card->deleteLater();
	return card->targetFixed();
}

bool ShimouCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	QStringList pnts = Self->property("shimouPN").toString().split(":");
	Card *card = Sanguosha->cloneCard(pnts.last());
	card->setSkillName("shimou");
	card->deleteLater();
	foreach (const Player *p, Self->getAliveSiblings(true)){
		if(pnts[1]==p->objectName())
			return card->targetsFeasible(targets, p);
	}
	return card->targetsFeasible(targets, Self);
}

void ShimouCard::onUse(Room *room, CardUseStruct &use) const
{
	if (user_string.isEmpty()){
		if(use.from->getChangeSkillState("shimou")==1){
			int x = 998;
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				if(p->getHandcardNum()<x) x = p->getHandcardNum();
			}
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				if(p->getHandcardNum()<=x) use.to << p;
			}
			room->setChangeSkillState(use.from, "shimou", 2);
			if(use.from->getGeneralName().endsWith("xunyu"))
				use.from->setAvatarIcon("thmou_xunyu2");
		}else{
			int x = 0;
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				if(p->getHandcardNum()>x) x = p->getHandcardNum();
			}
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				if(p->getHandcardNum()>=x) use.to << p;
			}
			room->setChangeSkillState(use.from, "shimou", 1);
			if(use.from->getGeneralName().endsWith("xunyu"))
				use.from->setAvatarIcon("");
		}
		SkillCard::onUse(room,use);
	}else{
		QStringList pnts = use.from->property("shimouPN").toString().split(":");
		foreach (ServerPlayer *p, room->getAlivePlayers()){
			if(pnts[1]==p->objectName()) use.from = p;
		}
		Card *card = Sanguosha->cloneCard(pnts.last());
		card->setSkillName("_shimou");
		use.card = card;
		use.extra_use++;
		card->onUse(room,use);
		card->deleteLater();
		use.extra_use--;
	}
}

void ShimouCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets){
		int n = p->getMaxHp()-p->getHandcardNum();
		if(n>0){
			p->drawCards(qMin(n,5),"shimou");
			room->setPlayerMark(p,"shimouBf",1);
		}else if(n<0){
			room->askForDiscard(p,"shimou",-n,-n);
			room->setPlayerMark(p,"shimouBf",2);
		}
		if(source->isAlive()&&p->isAlive()){
			QList<int>ids = room->getAvailableCardList(p,"trick","shimou",nullptr,true);
			if(ids.length()>0){
				room->fillAG(ids,source);
				n = room->askForAG(source,ids,ids.length()<2,"shimou","shimou0");
				room->clearAG(source);
				QString pnt = "shimou1:"+p->objectName()+":"+Sanguosha->getEngineCard(n)->objectName();
				room->setPlayerProperty(source,"shimouPN",pnt);
				room->askForUseCard(source,"@@shimou!",pnt);
			}
		}
		room->setPlayerMark(p,"shimouBf",0);
	}
}

class ShimouVs : public ZeroCardViewAsSkill
{
public:
	ShimouVs() : ZeroCardViewAsSkill("shimou")
	{
		change_skill = true;
		response_pattern = "@@shimou!";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("ShimouCard")<1;
	}

	const Card *viewAs() const
	{
		ShimouCard *sc = new ShimouCard;
		sc->setUserString(Sanguosha->getCurrentCardUsePattern());
		return sc;
	}
};

class Shimou : public TriggerSkill
{
public:
	Shimou() :TriggerSkill("shimou")
	{
		events << GameStart;
		change_skill = true;
		view_as_skill = new ShimouVs;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(event == GameStart){
			if(room->askForChoice(player,objectName(),"1_num+2_num")=="2_num"){
				room->setChangeSkillState(player,objectName(),2);
				if(player->getGeneralName().endsWith("xunyu"))
					player->setAvatarIcon("thmou_xunyu2");
			}
		}
		return false;
	}
};

class Bizuo : public TriggerSkill
{
public:
	Bizuo() :TriggerSkill("bizuo")
	{
		events << EventPhaseStart << CardsMoveOneTime;
		frequency = Limited;
		limit_mark = "@bizuo";
	}
	bool triggerable(const ServerPlayer *player) const
	{
		return player!=nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event == EventPhaseStart){
			if (player->getPhase() != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				room->removePlayerMark(p,"@skill_invalidity");
			}
			foreach (ServerPlayer *p, room->getAllPlayers()){
				if(p->getMark("bizuoTo"+player->objectName())>0){
					p->setMark("bizuoTo"+player->objectName(),0);
					QList<int> ids = ListV2I(p->getTag("bizuoIds").toList());
					foreach (int id, ids){
						if(room->getCardOwner(id))
							ids.removeOne(id);
					}
					if(ids.length()>0)
						p->assignmentCards(ids,"bizuo|bizuo1",room->getAlivePlayers(),ids.length(),ids.length());
				}
				p->removeTag("bizuoIds");
			}
			if (player->isDead()) return false;
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				if(p->getHp()<room->getAlivePlayers().first()->getHp())
					return false;
			}
			foreach (ServerPlayer *p, room->getAllPlayers()){
				if(p->getMark("@bizuo")>0&&p->hasSkill(objectName())){
					ServerPlayer *tp = room->askForPlayerChosen(p,room->getAlivePlayers(),objectName(),"bizuo0",true,true);
					if(tp){
						p->peiyin(this);
						room->removePlayerMark(p, "@bizuo");
						room->doSuperLightbox(p, objectName());
						p->addMark("bizuoTo"+tp->objectName());
						foreach (ServerPlayer *q, room->getAlivePlayers()){
							if(q==p||q==tp) continue;
							room->addPlayerMark(q,"@skill_invalidity");
						}
						tp->gainAnExtraTurn();
					}
				}
			}
		}else if(event == CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile&&(move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD){
				QVariantList ids = player->getTag("bizuoIds").toList();
				foreach (int id, move.card_ids)
					ids << id;
				player->setTag("bizuoIds", ids);
			}
		}
		return false;
	}
};

class Lieji : public TriggerSkill
{
public:
	Lieji() :TriggerSkill("lieji")
	{
		events << CardFinished << ConfirmDamage;
	}
	bool triggerable(const ServerPlayer *player) const
	{
		return player&&player->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event == CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			player->setMark(use.card->toString()+"liejiDamage-Clear",0);
			if(use.card->isKindOf("TrickCard")&&player->hasSkill(objectName())&&player->askForSkillInvoke(this,data)){
				player->peiyin(this);
				foreach (const Card *h, player->getHandcards()){
					if(h->isDamageCard()){
						player->addMark(h->toString()+"liejiDamage-Clear");
						room->setCardFlag(h,"liejiDamage");
					}
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("liejiDamage")){
				int n = player->getMark(damage.card->toString()+"liejiDamage-Clear");
				if(n>0) player->damageRevises(data,n);
			}
		}
		return false;
	}
};

QuzhouCard::QuzhouCard()
{
	target_fixed = true;
}

void QuzhouCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	Card*dc = dummyCard();
	QList<int>ids = room->getNCards(1);
	while(source->isAlive()){
		CardsMoveStruct move(ids,nullptr,Player::PlaceTable,CardMoveReason(CardMoveReason::S_REASON_TURNOVER,source->objectName(), "quzhou", ""));
		room->moveCardsAtomic(move, true);
		const Card*c = Sanguosha->getCard(ids.last());
		if(c->isKindOf("Slash")){
			room->notifyMoveToPile(source,ids,"quzhou");
			c = room->askForUseCard(source,"@@quzhou","quzhou0");
			room->notifyMoveToPile(source,ids,"quzhou",Player::PlaceTable,false);
			if(!c) dc->addSubcards(ids);
			room->throwCard(dc,"quzhou",nullptr);
			break;
		}else{
			dc->addSubcards(ids);
			if(source->isDead()){
				room->throwCard(dc,"quzhou",nullptr);
				break;
			}
			if(dc->subcardsLength()<room->getPlayers().length()&&room->askForChoice(source,"quzhou","quzhou1+quzhou2")=="quzhou1"){
				ids = room->getNCards(1);
			}else{
				source->obtainCard(dc);
				break;
			}
		}
	}
}

class Quzhou : public ViewAsSkill
{
public:
	Quzhou() : ViewAsSkill("quzhou")
	{
		expand_pile = "#quzhou";
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *to_select) const
	{
		return cards.isEmpty()&&Self->getPileName(to_select->getId()).contains("quzhou");
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (cards.isEmpty()){
			if(Sanguosha->getCurrentCardUsePattern().isEmpty())
				return new QuzhouCard();
			return nullptr;
		}
		return cards.first();
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("QuzhouCard")<1;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.contains("@@quzhou");
	}
};

class Baojia : public TriggerSkill
{
public:
	Baojia() :TriggerSkill("baojia")
	{
		events << CardFinished << GameStart << DamageInflicted;
	}
	bool triggerable(const ServerPlayer *player) const
	{
		return player!=nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event == CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->hasFlag("baojiaDamage")){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(use.card->hasFlag("baojiaUse"+p->objectName())&&room->getCardOwner(use.card->getEffectiveId())==nullptr)
						p->obtainCard(use.card);
				}
			}
		}else if(event == DamageInflicted){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->getTypeId()>0&&player->getMark("baojiaDamage-Clear")<1){
				player->addMark("baojiaDamage-Clear");
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if((player->getMark("&baojia+#"+p->objectName())>0||player==p)&&p->hasSkill(objectName())
						&&p->hasEquipArea()&&p->askForSkillInvoke(this,data)){
						p->peiyin(this);
						QStringList choices;
						for (int i = 0; i < 5; i++) {
							if(p->hasEquipArea(i))
								choices << QString("EquipArea%1").arg(i);
						}
						QString choice = room->askForChoice(p,objectName(),choices.join("+"));
						p->throwEquipArea(choice.split("ea")[1].toInt());
						room->setCardFlag(damage.card,"baojiaDamage");
						room->setCardFlag(damage.card,"baojiaUse"+p->objectName());
						return player->damageRevises(data,-damage.damage);
					}
				}
			}
		}else{
			if(player->isAlive()&&player->hasSkill(objectName())){
				ServerPlayer *tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"baojia0",false,true);
				if(tp){
					player->peiyin(this);
					room->setPlayerMark(tp,"&baojia+#"+player->objectName(),1);
				}
			}
		}
		return false;
	}
};

DouweiCard::DouweiCard()
{
}

bool DouweiCard::targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const
{
	if(to_select->inMyAttackRange(Self)){
		const Card*c = Sanguosha->getCard(getEffectiveId());
		Card*dc = Sanguosha->cloneCard(c->objectName());
		dc->setSkillName("douwei");
		dc->deleteLater();
		return !Self->isProhibited(to_select,dc);
	}
	return false;
}

void DouweiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	const Card*c = Sanguosha->getCard(getEffectiveId());
	Card*dc = Sanguosha->cloneCard(c->objectName());
	dc->setSkillName("_douwei");
	room->useCard(CardUseStruct(dc,source,targets));
	dc->deleteLater();
}

class DouweiVs : public OneCardViewAsSkill
{
public:
	DouweiVs() : OneCardViewAsSkill("douwei")
	{
	}
	bool viewFilter(const Card *to_select) const
	{
		return to_select->isDamageCard() && !Self->isJilei(to_select);
	}
	const Card *viewAs(const Card *originalCard) const
	{
		Card *c = new DouweiCard;
		c->addSubcard(originalCard);
		return c;
	}
	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("douweiBan-Clear")<1&&player->canDiscard(player,"h");
	}
};

class Douwei : public TriggerSkill
{
public:
	Douwei() :TriggerSkill("douwei")
	{
		events << Dying;
		view_as_skill = new DouweiVs;
	}
	bool triggerable(const ServerPlayer *player) const
	{
		return player&&player->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event == Dying){
			DyingStruct dying = data.value<DyingStruct>();
			if(dying.damage&&dying.damage->card&&dying.damage->card->getSkillNames().contains(objectName())){
				if(player->hasSkill(objectName(),true)){
					room->recover(player,RecoverStruct(objectName(),player));
					room->addPlayerMark(player,"douweiBan-Clear");
				}
			}
		}
		return false;
	}
};

class Yingjia : public TriggerSkill
{
public:
	Yingjia() :TriggerSkill("yingjia")
	{
		events << CardFinished << EventPhaseChanging;
	}
	bool triggerable(const ServerPlayer *player) const
	{
		return player!=nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event == CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0&&player->isAlive()&&player->hasSkill(objectName())){
				bool has = true;
				foreach (ServerPlayer *p, use.to){
					if(p->isAlive()&&player->distanceTo(p)>1){
						if(has){
							has = false;
							room->sendCompulsoryTriggerLog(player,this);
						}
						player->addMark(p->objectName()+"yingjiaBf-Clear");
						room->setFixedDistance(player,p,1);
						bool distance = true;
						foreach (ServerPlayer *q, room->getAlivePlayers()){
							if(player->distanceTo(q)>1){
								distance = false;
								break;
							}
						}
						if(distance){
							ServerPlayer *tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"yingjia0",true);
							if(tp){
								room->doAnimate(1,player->objectName(),tp->objectName());
								Card*dc = dummyCard(tp->handCards());
								room->obtainCard(player,dc,false);
								if(player->isDead()||tp->isDead()||dc->subcardsLength()<1) continue;
								const Card*sc = room->askForExchange(player,objectName(),dc->subcardsLength(),dc->subcardsLength(),true,"yingjia1:"+tp->objectName());
								if(sc) tp->obtainCard(sc,false);
							}
						}
					}
				}
			}
		}else{
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				foreach (ServerPlayer *q, room->getAlivePlayers()){
					if(p->getMark(q->objectName()+"yingjiaBf-Clear")>0)
						room->removeFixedDistance(p,q,1);
				}
			}
		}
		return false;
	}
};

XianjuCard::XianjuCard()
{
	target_fixed = true;
}

void XianjuCard::onUse(Room *room, CardUseStruct &use) const
{
	foreach (ServerPlayer *p, room->getAlivePlayers()){
		if(use.from->inMyAttackRange(p)) use.to << p;
	}
	SkillCard::onUse(room,use);
}

void XianjuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets){
		if(p->isKongcheng()||source->isDead()) continue;
		int id = room->askForCardChosen(source,p,"h","xianju");
		if(id>=0) room->obtainCard(source,id,false);
	}
	source->addMark("xianjuUse-PlayClear");
}

class XianjuVs : public ZeroCardViewAsSkill
{
public:
	XianjuVs() : ZeroCardViewAsSkill("xianju")
	{
		response_pattern = "@@xianju!";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("XianjuCard")<1;
	}

	const Card *viewAs() const
	{
		XianjuCard *sc = new XianjuCard;
		sc->setUserString(Sanguosha->getCurrentCardUsePattern());
		return sc;
	}
};

class Xianju : public TriggerSkill
{
public:
	Xianju() :TriggerSkill("xianju")
	{
		events << EventPhaseEnd;
		view_as_skill = new XianjuVs;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(event == EventPhaseEnd){
			if(player->getPhase()==Player::Play&&player->getMark("xianjuUse-PlayClear")>0){
				room->sendCompulsoryTriggerLog(player,objectName());
				int n = 0;
				foreach (ServerPlayer *p, room->getAlivePlayers()){
					if(!player->inMyAttackRange(p)) n++;
				}
				room->askForDiscard(player,objectName(),n,n,false,true);
			}
		}
		return false;
	}
};

WohengCard::WohengCard()
{
}

bool WohengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty()&&to_select!=Self;
}

void WohengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	room->addPlayerMark(source,"&woheng_lun");
	int n = source->getMark("&woheng_lun");
	foreach (ServerPlayer *p, targets){
		if(n>0){
			QString choices = "woheng1="+QString::number(n)+"+woheng2="+QString::number(n);
			if(p->canDiscard(p,"he")&&room->askForChoice(source,"woheng",choices,QVariant::fromValue(p)).contains("woheng2")){
				room->askForDiscard(p,"woheng",n,n,false,true);
			}else
				p->drawCards(n,"woheng");
		}
		if(p->getHandcardNum()!=source->getHandcardNum()||n>3){
			room->addPlayerMark(source,"sgswohengBan-Clear");
			source->drawCards(2,"woheng");
		}
	}
}

class WohengVs : public ZeroCardViewAsSkill
{
public:
	WohengVs() : ZeroCardViewAsSkill("woheng")
	{
		response_pattern = "@@woheng";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("sgswohengBan-Clear")<1;
	}

	const Card *viewAs() const
	{
		return new WohengCard;
	}
};

class Woheng : public TriggerSkill
{
public:
	Woheng() :TriggerSkill("woheng")
	{
		events << Damaged;
		view_as_skill = new WohengVs;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(event == Damaged){
			if(player->getMark("sgswohengBan-Clear")<1){
				room->askForUseCard(player,"@@woheng","woheng0:");
			}
		}
		return false;
	}
};

class Yugui : public TriggerSkill
{
public:
	Yugui() :TriggerSkill("yugui")
	{
		events << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer *player) const
	{
		return player&&player->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(event == EventPhaseStart){
			if(player->getPhase()==Player::Finish&&player->hasSkill(objectName())){
				QList<ServerPlayer *>tps;
				foreach (ServerPlayer *p, room->getAlivePlayers()){
					if(player!=p&&p->getKingdom()=="wu") tps << p;
				}
				ServerPlayer *tp = room->askForPlayerChosen(player,tps,objectName(),"yugui0",true,true);
				if(tp){
					tp->peiyin(objectName());
					room->setPlayerMark(tp,"&yugui+#"+player->objectName()+"-SelfPlayClear",1);
				}
			}else if(player->getPhase()==Player::Play){
				foreach (ServerPlayer *p, room->getAllPlayers()){
					if(player->getMark("&yugui+#"+p->objectName()+"-SelfPlayClear")>0){
						const Card*sc = room->askForCard(player,"BasicCard|red","yugui1:"+p->objectName(),QVariant::fromValue(p),Card::MethodNone);
						if(sc){
							p->obtainCard(sc);
							int n = player->getMark("&woheng_lun");
							room->setPlayerMark(player,"&woheng_lun",0);
							sc = room->askForUseCard(player,"@@woheng","woheng0:1");
							if(sc&&player->hasSkill("woheng",true)) n++;
							room->setPlayerMark(player,"&woheng_lun",n);
						}
					}
				}
			}
		}
		return false;
	}
};

class Guangyong : public TriggerSkill
{
public:
	Guangyong() :TriggerSkill("guangyong")
	{
		events << TargetSpecified;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event == TargetSpecified){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()>0){
				if(use.to.contains(player)){
					room->sendCompulsoryTriggerLog(player,this);
					room->gainMaxHp(player,1,objectName());
					use.to.removeOne(player);
				}
				if(player->isAlive()&&use.to.length()>0){
					room->sendCompulsoryTriggerLog(player,this);
					room->loseMaxHp(player,1,objectName());
					if(!player->isAlive()) return false;
					ServerPlayer *tp = room->askForPlayerChosen(player,use.to,objectName(),"guangyong0");
					if(tp){
						room->doAnimate(1,player->objectName(),tp->objectName());
						if(tp->getHandcardNum()>0){
							int id = room->askForCardChosen(player,tp,"h",objectName());
							if(id>-1) room->obtainCard(player,id,false);
						}
					}
				}
			}
		}
		return false;
	}
};

class Juchui : public TriggerSkill
{
public:
	Juchui() :TriggerSkill("juchui")
	{
		events << CardUsed << EventPhaseChanging;
	}
	bool triggerable(const ServerPlayer *player) const
	{
		return player&&player->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event == CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if(use.card->getTypeId()==2&&!use.card->hasFlag("ComboMoves")){
				const Card *cmc = player->getTag("ComboMovesCard").value<const Card*>();
				if(cmc&&cmc->getTypeId()==3&&player->hasSkill(objectName())){
					ServerPlayer *tp = room->askForPlayerChosen(player,use.to,objectName(),"juchui0",false,true);
					if(tp){
						player->peiyin(objectName());
						use.card->setFlags("ComboMoves");
						if(tp->getMaxHp()<=player->getMaxHp()){
							QString choice = "juchui1+cancel";
							if(tp->isWounded()) choice = "juchui1+juchui2+cancel";
							choice = room->askForChoice(player,objectName(),choice,QVariant::fromValue(tp));
							if(choice=="juchui1")
								room->loseHp(tp,1,true,player,objectName());
							else if(choice=="juchui2")
								room->recover(tp,RecoverStruct(objectName(),player));
						}else{
							QString choice = "BasicCard+TrickCard+EquipCard";
							choice = room->askForChoice(player,objectName(),choice,QVariant::fromValue(tp));
							foreach (int id, room->getDrawPile()){
								if(Sanguosha->getCard(id)->isKindOf(choice.toStdString().c_str())){
									room->obtainCard(player,id);
									break;
								}
							}
							tp->setTag("juchuiChoice", choice);
							room->setPlayerCardLimitation(tp,"use",choice,true);
						}
					}
				}
			}
		}else{
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getAlivePlayers()){
				QString choice = p->getTag("juchuiChoice").toString();
				if(choice.isEmpty()) continue;
				room->removePlayerCardLimitation(p,"use",choice+"$1");
				p->removeTag("juchuiChoice");
			}
		}
		return false;
	}
};

ZhanpanCard::ZhanpanCard()
{
	target_fixed = true;
}

void ZhanpanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	if(subcardsLength()<1){
		int n = room->askForChoice(source,"zhanpan","1+2+3").toInt();
		source->drawCards(n,"zhanpan");
	}
	QList<ServerPlayer *>aps = room->getOtherPlayers(source);
	if(source->hasLordSkill("tiancheng")){
		QList<ServerPlayer *>tps;
		foreach (ServerPlayer *p, aps){
			if(p->getKingdom()=="qun")
				tps << p;
		}
		tps = room->askForPlayersChosen(source,tps,"tiancheng",0,tps.length(),"tiancheng0",true);
		if(tps.length()>0){
			source->peiyin("tiancheng");
			foreach (ServerPlayer *p, tps)
				aps.removeOne(p);
		}
	}
	foreach (ServerPlayer *p, aps)
		room->doAnimate(1,source->objectName(),p->objectName());
	foreach (ServerPlayer *p, aps){
		int x = p->getHandcardNum();
		int n = source->getHandcardNum()-x;
		if(n>0){
			p->drawCards(n,"zhanpan");
			room->askForDiscard(p,"zhanpan",3,3,false,true);
		}else if(n<0){
			room->askForDiscard(p,"zhanpan",-n,-n);
			p->drawCards(3,"zhanpan");
		}
		if(x==p->getHandcardNum())
			room->damage(DamageStruct("zhanpan",source,p));
	}
}

class Zhanpan : public ViewAsSkill
{
public:
	Zhanpan() : ViewAsSkill("zhanpan")
	{
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *to_select) const
	{
		return cards.length()<3&&!Self->isJilei(to_select);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		ZhanpanCard *sc = new ZhanpanCard();
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("ZhanpanCard")<1;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.contains("@@zhanpan");
	}
};

class Chensheng : public TriggerSkill
{
public:
	Chensheng() :TriggerSkill("chensheng")
	{
		events << EventPhaseChanging;
	}
	bool triggerable(const ServerPlayer *player) const
	{
		return player&&player->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event == EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			bool has = false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)){
				if(p->getHandcardNum()>=player->getHandcardNum())
					has = true;
			}
			if (!has) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)){
				if(p->hasSkill(objectName())){
					has = false;
					foreach (ServerPlayer *q, room->getOtherPlayers(p)){
						if(q->getHandcardNum()>=p->getHandcardNum())
							has = true;
					}
					if(!has) continue;
					room->sendCompulsoryTriggerLog(p,this);
					p->drawCards(1,objectName());
				}
			}
		}
		return false;
	}
};

class Duhai : public TriggerSkill
{
public:
	Duhai() : TriggerSkill("duhai")
	{
		events << TargetConfirmed << EventPhaseChanging;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetConfirmed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0&&use.from&&use.from!=player&&use.to.contains(player)&&player->hasSkill(objectName())){
				QStringList choices;
				choices << "heart" << "diamond" << "spade" << "club";
				foreach (QString ch, choices) {
					if(use.from->getMark(ch+"duhaiSuit")>0)
						choices.removeOne(ch);
				}
				if(choices.length()>0&&player->askForSkillInvoke(this,data)){
					QString choice = room->askForChoice(player,objectName(),choices.join("+"));
					use.from->addMark(choice+"duhaiSuit");
					foreach (QString m, use.from->getMarkNames()) {
						if(m.contains("&du_hai+")&&use.from->getMark(m)>0){
							room->setPlayerMark(use.from,m,0);
							choices = m.split("+");
							choices << choice+"_char";
							room->setPlayerMark(use.from,choices.join("+"),1);
							return false;
						}
					}
					room->setPlayerMark(use.from,"&du_hai+"+choice+"_char",1);
				}
			}
		} else if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			QStringList choices;
			foreach (const Card *h, player->getHandcards()) {
				if(!choices.contains(h->getSuitString())&&player->getMark(h->getSuitString()+"duhaiSuit")>0)
					choices << h->getSuitString();
			}
			if(choices.length()>0){
				room->loseHp(player,choices.length(),true,nullptr,objectName());
				foreach (QString m, player->getMarkNames()) {
					if(m.contains("&du_hai+")&&player->getMark(m)>0){
						room->setPlayerMark(player,m,0);
						QStringList ms = m.split("+");
						foreach (QString s, choices) {
							ms.removeOne(s+"_char");
							player->setMark(s+"duhaiSuit",0);
						}
						if(ms.length()>1)
							room->setPlayerMark(player,ms.join("+"),1);
						break;
					}
				}
			}
		}
		return false;
	}
};

LingseCard::LingseCard()
{
	will_throw = false;
}

bool LingseCard::targetFilter(const QList<const Player *> &targets, const Player *target, const Player *Self) const
{
	return targets.isEmpty()&&target!=Self;
}

void LingseCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		room->giveCard(source,p,this,"lingse");
		if(p->isDead()) continue;
		const Card *c = Sanguosha->getCard(getEffectiveId());
		QList<const Card *>cards = p->getHandcards()+p->getEquips();
		qShuffle(cards);
		Card*dc = dummyCard();
		foreach (const Card *h, cards) {
			if(h->getType()==c->getType()){
				dc->addSubcard(h);
				if(dc->subcardsLength()>1){
					source->obtainCard(dc);
					break;
				}
			}
		}
		if(dc->subcardsLength()<2){
			dc = Sanguosha->cloneCard("slash");
			dc->setSkillName("_lingse");
			if(p->canSlash(source,dc,false))
				room->useCard(CardUseStruct(dc,p,source));
		}
	}
}

class Lingsevs : public OneCardViewAsSkill
{
public:
	Lingsevs() : OneCardViewAsSkill("lingse")
	{
		filter_pattern = ".|.|.|.";
	}
	const Card *viewAs(const Card *originalCard) const
	{
		LingseCard *card = new LingseCard();
		card->addSubcard(originalCard);
		return card;
	}
	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("LingseCard")<1&&player->getCardCount()>0;
	}
};

class Lingse : public TriggerSkill
{
public:
	Lingse() : TriggerSkill("lingse")
	{
		events << CardFinished;
		view_as_skill = new Lingsevs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		if (event == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getSkillNames().contains(objectName())
				&&use.card->isKindOf("Slash")&&use.card->hasFlag("DamageDone")){
				foreach (ServerPlayer *p, use.to) {
					if(p->hasSkill(objectName(),true))
						room->addPlayerHistory(p,"LingseCard",-1);
				}
			}
		}
		return false;
	}
};

class Lianzhan : public TriggerSkill
{
public:
	Lianzhan() : TriggerSkill("lianzhan")
	{
		events << CardFinished << TargetSpecifying << DamageDone;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->hasFlag("lianzhanUse")){
				int n = 0;
				foreach (ServerPlayer *p, use.to)
					n += use.card->getMark("lianzhanDamage"+p->objectName());
				if(n==2){
					if(player->isWounded())
						room->recover(player,RecoverStruct(objectName(),player));
					else
						player->drawCards(2,objectName());
				}else if(n==0){
					foreach (ServerPlayer *p, use.to) {
						if(p->isAlive()&&player->isAlive()){
							Card*dc = Sanguosha->cloneCard(use.card->objectName());
							dc->setSkillName("_lianzhan");
							dc->deleteLater();
							if(p->canUse(dc,player)){
								room->useCard(CardUseStruct(dc,p,player));
							}
						}
					}
				}
			}
		}else if(event == DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("lianzhanUse"))
				room->addCardMark(damage.card,"lianzhanDamage"+damage.to->objectName());
		}else if(event == TargetSpecifying){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isDamageCard()&&use.to.length()==1&&player->hasSkill(objectName())&&player->askForSkillInvoke(this,data)){
				player->peiyin(this);
				QList<ServerPlayer *>tps = room->getCardTargets(player,use.card,use.to);
				ServerPlayer *tp = room->askForPlayerChosen(player,tps,objectName(),"lianzhan0:"+use.card->objectName(),true);
				room->setCardFlag(use.card,"lianzhanUse");
				if(tp){
					use.to << tp;
					room->sortByActionOrder(use.to);
					data.setValue(use);
					room->doAnimate(1,player->objectName(),tp->objectName());
				}else{
					use.extra_use++;
					data.setValue(use);
				}
			}
		}
		return false;
	}
};

class ThWeiming : public TriggerSkill
{
public:
	ThWeiming() : TriggerSkill("thweiming")
	{
		events << CardUsed << DamageDone;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0){
				foreach (ServerPlayer *p, use.to) {
					if(p!=player&&p->hasSkill(objectName())){
						bool hasHp = p->getHp()>player->getHp();
						bool hasDamage = p->getMark(player->objectName()+"thweimingDamage_lun")>0;
						if(hasHp||hasDamage){
							room->sendCompulsoryTriggerLog(p,this);
							QList<const Card*> hs = player->getHandcards();
							qShuffle(hs);
							foreach (const Card*h, hs) {
								if(player->canDiscard(player,h->getId())){
									room->throwCard(h,objectName(),player);
									break;
								}
							}
							if(hasHp&&hasDamage)
								p->drawCards(1,objectName());
						}
					}
				}
			}
		}else if(event == DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from) damage.from->addMark(player->objectName()+"thweimingDamage_lun");
		}
		return false;
	}
};

class Fengmin : public TriggerSkill
{
public:
	Fengmin() : TriggerSkill("fengmin")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event == CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from_places.contains(Player::PlaceEquip)
				&&player->hasTurn()&&player->getMark("fengminUse-Clear")<1){
				int n = 0;
				for (int i = 0; i < 5; i++) {
					if(move.from->hasEquipArea(i)&&move.from->getEquip(i)==nullptr)
						n++;
				}
				if(n>0){
					room->sendCompulsoryTriggerLog(player,this);
					player->addMark("fengminUse-Clear");
					player->drawCards(n,objectName());
				}
			}
		}
		return false;
	}
};

class Zhiwangvs : public ZeroCardViewAsSkill
{
public:
	Zhiwangvs() : ZeroCardViewAsSkill("zhiwang")
	{
		response_pattern = "@@zhiwang!";
	}

	const Card *viewAs() const
	{
		return Sanguosha->getCard(Self->getMark("zhiwangId"));
	}
};

class Zhiwang : public TriggerSkill
{
public:
	Zhiwang() : TriggerSkill("zhiwang")
	{
		events << EventPhaseStart << DamageForseen;
		view_as_skill = new Zhiwangvs;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event == EventPhaseStart){
			if (player->getPhase()==Player::Finish&&player->canDiscard(player,"e")){
				room->sendCompulsoryTriggerLog(player,this);
				player->throwAllEquips(objectName());
				QList<int>ids;
				foreach (int id, room->getDiscardPile()) {
					const Card*c = Sanguosha->getCard(id);
					if(c->isDamageCard()) ids << id;
				}
				if(ids.length()>0){
					room->fillAG(ids,player);
					int id = room->askForAG(player,ids,true,objectName(),"zhiwang0");
					room->clearAG(player);
					if(id<0) id = ids.first();
					const Card*c = Sanguosha->getCard(id);
					QList<ServerPlayer *>tps;
					foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
						if(c->isAvailable(p)) tps << p;
					}
					ServerPlayer *tp = room->askForPlayerChosen(player,tps,objectName(),"zhiwang1");
					if(tp){
						room->setPlayerMark(tp,"zhiwangId",id);
						room->doAnimate(1,player->objectName(),tp->objectName());
						room->askForUseCard(tp,"@@zhiwang!","zhiwang2:"+c->objectName());
					}
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.from&&damage.from->getMark("zhiwangId")==damage.card->getId()){
				damage.from = nullptr;
				data.setValue(damage);
			}
		}
		return false;
	}
};

ShugangCard::ShugangCard()
{
	target_fixed = true;
}

void ShugangCard::onUse(Room *room, CardUseStruct &use) const
{
	QStringList choices;
	foreach (QString cn, Sanguosha->getCardNames("BasicCard,TrickCard")) {
		Card*dc = Sanguosha->cloneCard(cn);
		if(dc->isDamageCard()){
			dc->setSkillName("shugang");
			dc->addSubcards(subcards);
			if(dc->isAvailable(use.from))
				choices << cn;
		}
		dc->deleteLater();
	}
	if(choices.isEmpty()) return;
	QString cn = room->askForChoice(use.from,"shugang",choices.join("+"));
	room->setPlayerProperty(use.from,"shugangCn",cn);
	room->setPlayerMark(use.from,"shugangId",getEffectiveId());
	room->askForUseCard(use.from,"@@shugang","shugang0:"+cn);
}

class Shugangvs : public ViewAsSkill
{
public:
	Shugangvs() : ViewAsSkill("shugang")
	{
		response_pattern = "@@shugang";
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *to_select) const
	{
		if(Sanguosha->getCurrentCardUsePattern().isEmpty())
			return cards.isEmpty()&&to_select->hasTip("shugang");
		return false;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(Sanguosha->getCurrentCardUsePattern()==response_pattern){
			Card*dc = Sanguosha->cloneCard(Self->property("shugangCn").toString());
			dc->addSubcard(Self->getMark("shugangId"));
			dc->setSkillName(objectName());
			return dc;
		}
		if(cards.isEmpty()) return nullptr;
		Card*dc = new ShugangCard();
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		foreach (const Card *h, player->getHandcards()) {
			if(h->hasTip("shugang"))
				return true;
		}
		return false;
	}
};

class Shugang : public TriggerSkill
{
public:
	Shugang() : TriggerSkill("shugang")
	{
		events << EventPhaseStart;
		view_as_skill = new Shugangvs;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event == EventPhaseStart){
			if (player->getPhase()==Player::Play&&player->askForSkillInvoke(objectName()+"$-1",data)){
				JudgeStruct judge;
				judge.who = player;
				judge.reason = objectName();
				judge.throw_card = false;
				judge.pattern = ".";
				room->judge(judge);
				if(room->getCardPlace(judge.card->getEffectiveId())==Player::PlaceJudge&&player->isAlive()){
					player->obtainCard(judge.card);
					QStringList choices;
					choices << "1";
					if(player->getHandcardNum()>0) choices << "2";
					choices << "3";
					for (int i = 0; i < 2; i++) {
						QString choice = room->askForChoice(player,objectName(),choices.join("+"));
						choices.removeOne(choice);
						if(choice=="2"){
							const Card*c = room->askForCardShow(player,player,objectName());
							room->showCard(player,c->getId());
							QString m = "&shugang+2:+"+c->getNumberString()+"~"+judge.card->getNumberString()+"-Clear";
							if(judge.card->getNumber()<c->getNumber())
								m = "&shugang+2:+"+judge.card->getNumberString()+"~"+c->getNumberString()+"-Clear";
							room->setPlayerMark(player,m,1);
						}else{
							room->setPlayerMark(player,"&shugang+"+choice+"-Clear",1);
							if(choice=="1"){
								if(player->hasCard(judge.card))
									room->setCardTip(judge.card->getEffectiveId(),"shugang-Clear");
							}else{
								room->acquireOneTurnSkills(player,objectName(),"xingshang");
							}
						}
						if(player->isDead()) break;
					}
				}
			}
		}
		return false;
	}
};

class ShugangLimit : public CardLimitSkill
{
public:
	ShugangLimit() : CardLimitSkill("#ShugangLimit")
	{
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target, const Card *card) const
	{
		if(!card->hasTip("dianlun")&&target->handCards().contains(card->getId())){
			foreach (const Player *p, target->getAliveSiblings(true)) {
				if(p->hasSkill("shugang",true)){
					foreach (QString m, p->getMarkNames()) {
						if(m.contains("&shugang+2:+")){
							m.remove("&shugang+2:+");
							m.remove("-Clear");
							return ".|.|"+m;
						}
					}
				}
			}
		}
		return "";
	}
};

DianlunCard::DianlunCard()
{
	target_fixed = true;
}

void DianlunCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	int n = subcardsLength();
	if(source->getMark("&shugang+3-Clear")>0)
		n += n;
	QList<int>ids = room->drawCardsList(source,n,"dianlun");
	foreach (int id, source->handCards()) {
		if(ids.contains(id))
			room->setCardTip(id,"dianlun-Clear");
	}
}

class Dianlun : public ViewAsSkill
{
public:
	Dianlun() : ViewAsSkill("dianlun")
	{
		response_pattern = "@@dianlun";
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *to_select) const
	{
		foreach (const Card *c, cards) {
			if(c->getNumber()==to_select->getNumber())
				return false;
		}
		if(cards.length()>1){
			if(cards.length()>2&&Sanguosha->getCurrentCardUsePattern()==response_pattern)
				return false;
			int x = qAbs(cards[0]->getNumber()-cards[1]->getNumber());
			if(x!=qAbs(cards.last()->getNumber()-to_select->getNumber())&&x!=qAbs(cards.first()->getNumber()-to_select->getNumber()))
				return false;
		}
		return !Self->isJilei(to_select);
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		DianlunCard *sc = new DianlunCard();
		sc->addSubcards(cards);
		return sc;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("DianlunCard")<1;
	}
};

class ThJiwei : public TriggerSkill
{
public:
	ThJiwei() : TriggerSkill("thjiwei")
	{
		events << EventPhaseProceeding << EventPhaseEnd << EventAcquireSkill;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event == EventPhaseEnd){
			if (player->getPhase()==Player::Play){
				room->detachSkillFromPlayer(player,"thjiweivs",true,false,false);
			}
		}else if(event == EventPhaseProceeding){
			if (player->getPhase()==Player::Play){
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(p->hasSkill(objectName(),true)){
						room->attachSkillToPlayer(player,"thjiweivs");
						break;
					}
				}
			}
		}else if(data.toString()==objectName()){
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(p->getPhase()==Player::Play){
					room->attachSkillToPlayer(p,"thjiweivs");
					break;
				}
			}
		}
		return false;
	}
};

ThJiweiCard::ThJiweiCard()
{
	will_throw = false;
	mute = true;
}

bool ThJiweiCard::targetFilter(const QList<const Player *> &targets, const Player *target, const Player *Self) const
{
	return targets.isEmpty()&&target!=Self&&target->hasSkill("thjiwei");
}

void ThJiweiCard::onUse(Room *room, CardUseStruct &use) const
{
	QVariant data = QVariant::fromValue(use);
	room->getThread()->trigger(PreCardUsed, room, use.from, data);
	room->getThread()->trigger(CardUsed, room, use.from, data);
	use = data.value<CardUseStruct>();
	foreach (ServerPlayer *p, use.to) {
		use.from->skillInvoked("thjiwei",-1,p);
        room->giveCard(use.from,p,this,"thjiwei");
        if(p->askForSkillInvoke("thjiwei",use.from,false)){
            room->askForUseCard(use.from,"@@dianlun","thjiwei0");
		}
	}
	room->getThread()->trigger(CardFinished, room, use.from, data);
	use = data.value<CardUseStruct>();
}

class ThJiweivs : public OneCardViewAsSkill
{
public:
	ThJiweivs() : OneCardViewAsSkill("thjiweivs&")
	{
		filter_pattern = ".|.|.|hand";
	}
	const Card *viewAs(const Card *originalCard) const
	{
		ThJiweiCard *card = new ThJiweiCard();
		card->addSubcard(originalCard);
		return card;
	}
	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getHandcardNum()>0&&player->usedTimes("ThJiweiCard")<1&&player->getKingdom()=="wei";
	}
};

QiaoduiCard::QiaoduiCard()
{
	will_throw = false;
}

bool QiaoduiCard::targetFilter(const QList<const Player *> &targets, const Player *target, const Player *Self) const
{
	return targets.isEmpty()&&target!=Self;
}

void QiaoduiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	source->addMark("qiaoduiUse-Clear");
	foreach (ServerPlayer *p, targets) {
		room->giveCard(source,p,this,"qiaodui");
	}
}

class Qiaoduivs : public ViewAsSkill
{
public:
	Qiaoduivs() : ViewAsSkill("qiaodui")
	{
		response_pattern = "@@qiaodui";
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *) const
	{
		return cards.length()<2;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		QiaoduiCard *sc = new QiaoduiCard();
		sc->addSubcards(cards);
		return sc;
	}
};

class Qiaodui : public TriggerSkill
{
public:
	Qiaodui() : TriggerSkill("qiaodui")
	{
		events << TargetSpecified << TargetConfirmed;
		view_as_skill = new Qiaoduivs;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0&&player->hasTurn()
			&&player->getMark("qiaoduiUse-Clear")<1&&player->getCardCount()>0){
				foreach (ServerPlayer *p, use.to){
					if(p!=player){
						player->setTag("qiaodui_data", data);
						if(room->askForUseCard(player,"@@qiaodui","qiaodui0")){
							use.extra_use++;
							data.setValue(use);
						}
						break;
					}
				}
			}
		}else if(event == TargetConfirmed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0&&player->hasTurn()
				&&use.to.contains(player)&&player->getCardCount()>0
				&&use.from&&use.from!=player&&player->getMark("qiaoduiUse-Clear")<1){
				player->setTag("qiaodui_data", data);
				if(room->askForUseCard(player,"@@qiaodui","qiaodui1")){
					use.nullified_list << "_ALL_TARGETS";
					data.setValue(use);
				}
			}
		}
		return false;
	}
};

class Tuicheng : public TriggerSkill
{
public:
	Tuicheng() : TriggerSkill("tuicheng")
	{
		events << CardsMoveOneTime;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event == CardsMoveOneTime){
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.from&&move.reason.m_reason==CardMoveReason::S_REASON_GIVE&&player==move.to){
				foreach (int id, move.card_ids){
					player->addMark(QString::number(id)+"tuichengId-Clear");
				}
				if(move.from->isAlive()&&move.from->hasSkill(objectName())){
					ServerPlayer *fp = room->findPlayerByObjectName(move.from->objectName());
					room->sendCompulsoryTriggerLog(fp,this);
					QStringList choices;
					foreach (const Card*c, player->getCards("he")){
						if(player->getMark(c->toString()+"tuichengId-Clear")>0) continue;
						choices << c->toString();
					}
					int n = move.card_ids.length();
					QString prompt = QString("tuicheng0:%1:%2").arg(fp->objectName()).arg(n);
					const Card*dc = room->askForExchange(player,objectName(),n,n,true,prompt,true,choices.join(","));
					if(dc){
						room->giveCard(player,fp,dc,objectName());
					}else
						fp->drawCards(n,objectName());
				}
			}
		}
		return false;
	}
};

class Juce : public TriggerSkill
{
public:
	Juce() : TriggerSkill("juce")
	{
		events << TargetSpecified;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("BasicCard")||use.card->isNDTrick()){
				player->addMark("juceUse-Clear");
				if(player->getMark("juceUse-Clear")==1&&player->hasFlag("CurrentPlayer")){
					foreach (ServerPlayer *p, room->getAlivePlayers()){
						if(use.to.contains(p)||player->getMark("&juce+#"+p->objectName()+"-SelfClear")<1)
							continue;
						room->doAnimate(1,player->objectName(),p->objectName());
						use.to << p;
						room->sortByActionOrder(use.to);
						data.setValue(use);
					}
					if(player->hasSkill(objectName())){
						player->setTag("juce_data", data);
						ServerPlayer *tp = room->askForPlayerChosen(player,room->getCardTargets(player,use.card,use.to),objectName()+"$-1","juce0",true,true);
						if(tp){
							use.to << tp;
							room->sortByActionOrder(use.to);
							data.setValue(use);
							room->setPlayerMark(tp,"&juce+#"+player->objectName()+"-SelfClear",1);
						}
					}
				}
			}
		}
		return false;
	}
};

class Kangming : public TriggerSkill
{
public:
	Kangming() : TriggerSkill("kangming")
	{
		events << CardFinished << EventPhaseStart << ShowCards;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0){
				foreach (ServerPlayer *p, use.to){
					if(p->isAlive()&&p->getMark("kangmingBan-SelfClear")<1
					&&p->hasSkill(objectName())&&p->askForSkillInvoke(objectName()+"$-1",data)){
						p->drawCards(1,objectName());
						if(p->isAlive()&&player->isAlive()){
							QStringList ids;
							foreach (const Card *h, p->getHandcards()){
								if(p->getMark(h->toString()+"kangmingShow-Clear")>0) continue;
								if(h->isKindOf("Slash")||h->isNDTrick()){
									Card*dc = Sanguosha->cloneCard(h->objectName());
									dc->setSkillName("_kangming");
									if(p->canUse(dc,player)) ids << h->toString();
									dc->deleteLater();
								}
							}
							const Card *c = room->askForCard(p,ids.join(","),"kangming0:"+player->objectName(),data,Card::MethodNone);
							if(c){
								room->showCard(p,c->getId());
								Card*dc = Sanguosha->cloneCard(c->objectName());
								dc->setSkillName("_kangming");
								room->useCard(CardUseStruct(dc,p,player));
								dc->deleteLater();
								continue;
							}
						}
						p->addMark("kangmingBan-SelfClear");
						p->drawCards(3,objectName());
					}
				}
			}
		}else if(event == ShowCards){
			QString str = data.toString();
			if(str.contains(":"))
				str = str.split(":").first();
			QStringList ids = str.split("+");
			foreach (const Card *h, player->getHandcards()){
				if(ids.contains(h->toString()))
					player->addMark(h->toString()+"kangmingShow-Clear");
			}
		}else{
			if(player->getPhase()==Player::RoundStart)
				player->setMark("kangmingBan-SelfClear",0);
		}
		return false;
	}
};

class Duoyue : public TriggerSkill
{
public:
	Duoyue() : TriggerSkill("duoyue")
	{
		events << EventPhaseStart;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (event == EventPhaseStart) {
			if(player->getPhase()==Player::Play){
				for (int i = 1; i <= 3; i++) {
					QList<ServerPlayer *>tps;
					foreach (ServerPlayer *p, room->getOtherPlayers(player)){
						if(player->canPindian(p)) tps << p;
					}
					ServerPlayer*tp = room->askForPlayerChosen(player,tps,objectName(),"duoyue0",true);
					if(tp){
						room->doAnimate(1,player->objectName(),tp->objectName());
						if(i==1) player->skillInvoked(objectName());
						PindianStruct*pd = player->PinDian(tp,objectName());
						if(player->isDead()) break;
						if(pd->from_number==pd->to_number) continue;
						if(pd->from_number>pd->to_number){
							if(!player->askForSkillInvoke(objectName(),"draw:"+pd->from->objectName(),false)) continue;
							tp = room->askForPlayerChosen(player,room->getAlivePlayers(),objectName(),"duoyue1",true);
							if(tp){
								room->damage(DamageStruct(objectName(),player,tp));
								break;
							}else
								pd->from->drawCards(i,objectName());
						}else{
							if(pd->to->isDead()) break;
							if(!player->askForSkillInvoke(objectName(),"draw:"+pd->to->objectName(),false)) continue;
							pd->to->drawCards(i,objectName());
						}
						if(player->isDead()) break;
					}else
						break;
				}
			}
		}
		return false;
	}
};

JunheCard::JunheCard()
{
	target_fixed = true;
	will_throw = false;
}

void JunheCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->setPlayerMark(source,"&junhe",subcardsLength()+1);
	bool Color = true,Type = true;
	foreach (int id, subcards){
		const Card *c = Sanguosha->getCard(id);
		if(c->getColor()!=Sanguosha->getCard(subcards.last())->getColor())
			Color = false;
		if(c->getType()!=Sanguosha->getCard(subcards.last())->getType())
			Type = false;
	}
	room->showCard(source,subcards);
	QStringList strs;
	if(Color) strs << Sanguosha->getCard(subcards.last())->getColorString();
	if(Type) strs << Sanguosha->getCard(subcards.last())->getType();
	source->setTag("junheDis", strs.join("#"));
}

class Junhevs : public ViewAsSkill
{
public:
	Junhevs() : ViewAsSkill("junhe")
	{
		response_pattern = "@@junhe";
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *vc) const
	{
		if(cards.length()>0){
			bool Color = true,Type = true;
			foreach (const Card *dc, cards){
				if(dc->getColor()!=cards.last()->getColor())
					Color = false;
				if(dc->getType()!=cards.last()->getType())
					Type = false;
			}
			if(!Color){
				if(vc->getColor()!=cards.first()->getColor())
					return false;
			}
			if(!Type){
				if(vc->getType()!=cards.first()->getType())
					return false;
			}
		}
		return true;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		JunheCard *sc = new JunheCard();
		sc->addSubcards(cards);
		return sc;
	}
};

class Junhe : public TriggerSkill
{
public:
	Junhe() : TriggerSkill("junhe")
	{
		events << EventPhaseStart << DamageCaused << Damaged;
		view_as_skill = new Junhevs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == DamageCaused) {
			if (player->getMark("&junhe")>0){
				room->removePlayerMark(player,"&junhe");
				if(player->getMark("&junhe")<1&&room->askForCard(player,player->getTag("junheDis").toString(),"junhe1",data))
					player->damageRevises(data,1);
			}
		}else if(event == Damaged){
			if (player->getMark("&junhe")>0){
				room->removePlayerMark(player,"&junhe");
				if(player->getMark("&junhe")<1)
					player->drawCards(2,objectName());
			}
		}else{
			if(player->getPhase()==Player::Start&&player->hasSkill(objectName())&&player->canDiscard(player,"he")){
				room->askForUseCard(player,"@@junhe","junhe0");
			}
		}
		return false;
	}
};

class Xiongwei : public TriggerSkill
{
public:
	Xiongwei() : TriggerSkill("xiongwei")
	{
		events << Pindian;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == Pindian) {
			PindianStruct *pd = data.value<PindianStruct *>();
			if(pd->to->isAlive()&&pd->to->getKingdom()=="wei"
			&&room->getCardOwner(pd->to_card->getEffectiveId())==nullptr
			&&player->hasSkill(objectName())){
                room->sendCompulsoryTriggerLog(player,this);
				player->obtainCard(pd->to_card);
				if(pd->to->isAlive()&&pd->to->getMark("&junhe")<1&&player->getMark("&junhe")>0){
					QStringList choices;
					for (int i = 1; i <= player->getMark("&junhe"); i++) {
						choices << QString("xiongwei0=%1").arg(i);
					}
					choices << "cancel";
					QString choice = room->askForChoice(player,objectName(),choices.join("+"),QVariant::fromValue(pd->to));
					if(choice!="cancel"){
						choice.remove("xiongwei0=");
						int x = choice.toInt();
						room->doAnimate(1,player->objectName(),pd->to->objectName());
						room->removePlayerMark(player,"&junhe",x);
						room->setPlayerMark(pd->to,"&junhe",x);
						pd->to->setTag("junheDis", player->getTag("junheDis"));
					}
				}
			}
			if(pd->from->isAlive()&&pd->from->getKingdom()=="wei"
			&&room->getCardOwner(pd->from_card->getEffectiveId())==nullptr
			&&pd->to->isAlive()&&pd->to->hasSkill(objectName())){
                room->sendCompulsoryTriggerLog(pd->to,this);
				pd->to->obtainCard(pd->from_card);
				if(pd->from->isAlive()&&pd->from->getMark("&junhe")<1&&pd->to->getMark("&junhe")>0){
					QStringList choices;
					for (int i = 1; i <= pd->to->getMark("&junhe"); i++) {
						choices << QString("xiongwei0=%1").arg(i);
					}
					choices << "cancel";
					QString choice = room->askForChoice(pd->to,objectName(),choices.join("+"),QVariant::fromValue(pd->from));
					if(choice!="cancel"){
						choice.remove("xiongwei0=");
						int x = choice.toInt();
						room->doAnimate(1,pd->to->objectName(),pd->from->objectName());
						room->removePlayerMark(pd->to,"&junhe",x);
						room->setPlayerMark(pd->from,"&junhe",x);
						pd->from->setTag("junheDis", pd->to->getTag("junheDis"));
					}
				}
			}
		}
		return false;
	}
};

class Chongwei : public TriggerSkill
{
public:
	Chongwei() : TriggerSkill("chongwei")
	{
		events << GameStart << EventAcquireSkill << Damage;
		frequency = Compulsory;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event==Damage) {
			if(player->getMark("&chongwei")>0){
				room->sendCompulsoryTriggerLog(player,this);
				room->removePlayerMark(player,"&chongwei");
				if(player->getMark("&chongwei")<1){
					room->recover(player,RecoverStruct(objectName(),player));
					player->drawCards(player->getHp(),objectName());
					player->addMark("chongzhu3");
					room->changeTranslation(player,"chongzhu",1);
					room->detachSkillFromPlayer(player,objectName());
				}
			}
		}else{
			if(event==EventAcquireSkill){
				if(data.toString()!=objectName())
					return false;
			}
			room->setPlayerMark(player,"&chongwei",3);
		}
		return false;
	}
};

class ChongweiDistance : public DistanceSkill
{
public:
	ChongweiDistance() : DistanceSkill("#ChongweiDistance")
	{
	}

	int getCorrect(const Player *from, const Player *) const
	{
		if (from->getMark("&chongwei")>0&&from->hasSkill("chongwei"))
			return from->getMark("&chongwei");
		return 0;
	}
};

class Chongzhu : public TriggerSkill
{
public:
	Chongzhu() : TriggerSkill("chongzhu")
	{
		events << TargetSpecified << CardFinished << CardUsed;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0){
				if(player->getMark("&chongzhu+3")>0){
					room->setPlayerMark(player,"&chongzhu+3",0);
					use.to.removeOne(player);
					ServerPlayer *tp = room->askForPlayerChosen(player,use.to,objectName(),"chongzhu30",true);
					if(tp){
						room->damage(DamageStruct(objectName(),player,tp));
					}
				}
			}
		}else if(event==CardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0){
				room->setPlayerMark(player,"&chongzhu+1",0);
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0){
				if(use.to.contains(player)&&player->hasSkill(objectName())
					&&player->askForSkillInvoke(objectName()+"$-1",data)){
					QStringList choices;
					choices << "chongzhu1";
					if(player->getMark("chongzhu2-Clear")<1)
						choices << "chongzhu2";
					if(player->getMark("chongzhu3")>0)
						choices << "chongzhu3";
					QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
					if(choice=="chongzhu1"){
						room->setPlayerMark(player,"&chongzhu+1",1);
					}else if(choice=="chongzhu2"){
						player->addMark("chongzhu2-Clear");
						player->drawCards(2,objectName());
					}else{
						room->setPlayerMark(player,"&chongzhu+3",1);
					}
				}
			}
		}
		return false;
	}
};

class Zhenting : public TriggerSkill
{
public:
	Zhenting() : TriggerSkill("zhenting")
	{
		events << EventPhaseChanging << DamageDone << CardsMoveOneTime;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == DamageDone) {
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from) damage.from->addMark("zhentingDamaged-Clear");
			QStringList dps = room->getTag("zhentingDamage").toStringList();
			if(dps.contains(player->objectName())) return false;
			dps.append(player->objectName());
			room->setTag("zhentingDamage",dps);
		}else if(event == EventPhaseChanging){
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			QStringList dps = room->getTag("zhentingDamage").toStringList();
			QList<int> ids = ListV2I(room->getTag("zhentingIds").toList());
			room->removeTag("zhentingDamage");
			room->removeTag("zhentingIds");
			if(dps.length()<2) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()){
				if(!p->hasSkill(objectName())) continue;
				QList<int> ids2;
				foreach (int id, room->getDiscardPile()){
					if(ids.contains(id)) ids2 << id;
				}
				QList<ServerPlayer *>tps1,tps2;
				foreach (ServerPlayer *q, room->getAlivePlayers()){
					if(dps.contains(q->objectName())) tps1 << q;
					if(q->getMark("zhentingDamaged-Clear")>0&&ids2.length()>0) tps2 << q;
				}
				QStringList choices;
				if(tps1.length()>0) choices << "zhenting1";
				if(tps2.length()>0) choices << "zhenting2";
				if(choices.isEmpty()||!player->askForSkillInvoke(objectName(),dps)) continue;
				if(room->askForChoice(player,objectName(),choices.join("+"),dps)=="zhenting1"){
					ServerPlayer *tp = room->askForPlayerChosen(player,tps1,objectName(),"zhenting10");
					if(tp){
						room->doAnimate(1,player->objectName(),tp->objectName());
						room->recover(tp,RecoverStruct(objectName(),player));
						tp->drawCards(1,objectName());
					}
				}else{
					ServerPlayer *tp = room->askForPlayerChosen(player,tps2,objectName(),"zhenting20");
					if(tp){
						room->doAnimate(1,player->objectName(),tp->objectName());
						room->fillAG(ids2,tp);
						Card*dc = dummyCard();
						for (int i = 0; i < 2; i++) {
							int id = room->askForAG(tp,ids2,false,objectName(),"zhenting21");
							ids2.removeOne(id);
							dc->addSubcard(id);
							room->takeAG(tp,id,false,QList<ServerPlayer*>()<<tp);
							if(ids2.isEmpty()) break;
						}
						room->clearAG(tp);
						tp->obtainCard(dc);
					}
				}
			}
		}else{
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(move.to_place==Player::DiscardPile&&player->hasFlag("CurrentPlayer")){
				QVariantList ids = room->getTag("zhentingIds").toList();
				foreach (int id, move.card_ids)
					ids << id;
				room->setTag("zhentingIds",ids);
			}
		}
		return false;
	}
};

class Chiguo : public TriggerSkill
{
public:
	Chiguo() : TriggerSkill("chiguo")
	{
		events << CardUsed << EventPhaseStart;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0&&player->getMark("chiguoBf-PlayClear")>0){
				QList<int>ids = room->showDrawPile(player,1,objectName(),true,false);
				if(Sanguosha->getCard(ids[0])->getSuit()==use.card->getSuit()){
					QList<ServerPlayer *>tps = room->getCardTargets(player,use.card,use.to);
					if(use.to.length()>1) tps << use.to;
					player->setTag("chiguo_data", data);
					ServerPlayer *tp = room->askForPlayerChosen(player,tps,"chiguo0","chiguo0");
					if(tp){
						room->doAnimate(1,player->objectName(),tp->objectName());
						if(use.to.contains(tp))
							use.to.removeOne(tp);
						else{
							use.to.append(tp);
							room->sortByActionOrder(use.to);
						}
						data.setValue(use);
					}
				}else{
					ServerPlayer *tp = room->askForPlayerChosen(player,use.to,"chiguo1","chiguo1");
					if(tp){
						room->doAnimate(1,player->objectName(),tp->objectName());
						room->giveCard(player,tp,ids,objectName());
						return false;
					}
				}
				room->throwCard(ids,objectName(),nullptr);
			}
		}else if(player->getPhase()==Player::Play
		&&player->hasSkill(objectName())&&player->askForSkillInvoke(objectName()+"$-1")){
			QList<int>ids = room->getNCards(3,false,false);
			room->fillAG(ids,player);
			room->askForAG(player,ids,true,objectName());
			room->clearAG(player);
			room->returnToEndDrawPile(ids);
			player->addMark("chiguoBf-PlayClear");
			player->setTag("chiguoBf", ListI2V(ids));
		}
		return false;
	}
};

class Junmou : public TriggerSkill
{
public:
	Junmou() : TriggerSkill("junmou")
	{
		events << CardFinished << GameStart;
		change_skill = true;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardFinished) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0){
				foreach (ServerPlayer *p, use.to){
					if(p->isAlive()&&p->getHandcardNum()>0&&p->hasSkill(objectName())){
						QString m = QString("junmouBan-%1Clear").arg(room->getCurrent()->getPhase());
						if(p->getMark(m)>0) continue;
						const Card*c = room->askForCard(p,".","junmou0",data,Card::MethodNone);
						if(c==nullptr) continue;
						int n = qrand()%2+1;
						if(p->getChangeSkillState(objectName())!=1)
							n += 2;
						p->skillInvoked(objectName(),n);
						if(p->getChangeSkillState(objectName())==1){
							room->setChangeSkillState(p,objectName(),2);
							if(player->getGeneralName().endsWith("luxun"))
								player->setAvatarIcon("thmou_luxun2");
							Card*dc = Sanguosha->cloneCard("fire_slash",c->getSuit(),c->getNumber());
							dc->setSkillName(objectName());
							WrappedCard *card = Sanguosha->getWrappedCard(c->getId());
							card->takeOver(dc);
							room->notifyUpdateCard(p, c->getId(), card);
							int n = 1;
							if(p->askForSkillInvoke(objectName(),"junmou1",false)){
								p->addMark(m);
								n++;
							}
							p->drawCards(n,objectName());
						}else{
							room->setChangeSkillState(p,objectName(),1);
							if(player->getGeneralName().endsWith("luxun"))
								player->setAvatarIcon(player->getGeneralName());
							foreach (const Card*h, p->getHandcards()){
								if(c->getSuit()==h->getSuit())
									room->setCardTip(h->getId(),objectName());
							}
							QList<ServerPlayer *>tps;
							foreach (ServerPlayer *q, room->getAlivePlayers()){
								if(q->isChained()) continue;
								tps << q;
							}
							tps = room->askForPlayersChosen(p,tps,objectName(),0,2,"junmou2");
							if(tps.length()>1) p->addMark(m);
							foreach (ServerPlayer *q, tps){
								room->doAnimate(1,p->objectName(),q->objectName());
								room->setPlayerChained(q);
							}
						}
					}
				}
			}
		}else if(player->hasSkill(objectName())){
			if(room->askForChoice(player,objectName(),"1_num+2_num")=="2_num"){
				room->setChangeSkillState(player,objectName(),2);
				if(player->getGeneralName().endsWith("luxun"))
					player->setAvatarIcon("thmou_luxun2");
			}
		}
		return false;
	}
};

ZhanyanCard::ZhanyanCard()
{
	will_throw = false;
	mute = true;
}

bool ZhanyanCard::targetFilter(const QList<const Player *> &, const Player *target, const Player *Self) const
{
	return target->isChained()&&target!=Self;
}

void ZhanyanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	int n = qrand()%2+1;
	if(source->getChangeSkillState("junmou")!=1)
		n += 2;
	source->peiyin("zhanyan",n);
	room->removePlayerMark(source, "@zhanyan");
	room->doSuperLightbox(source, "zhanyan");
	source->setMark("zhanyanUse",1);
	room->recover(source,RecoverStruct("zhanyan",source,targets.length()));
	foreach (ServerPlayer *p, targets) {
		const Card*c = room->askForCardShow(p,source,"zhanyan");
		if(c) p->setMark("zhanyanId",c->getId()+1);
	}
	foreach (ServerPlayer *p, targets){
		int n = p->getMark("zhanyanId");
		if(n>0) room->showCard(p,n-1);
		p->setTag("zhanyanDamage", false);
	}
	foreach (ServerPlayer *p, targets){
		if(source->isDead()) return;
		int n = p->getMark("zhanyanId");
		if(n>0&&source->canDiscard("he")){
			const Card*c = Sanguosha->getCard(n-1);
			if(room->askForCard(source,".|"+c->getSuitString(),"zhanyan0:"+p->objectName()+":"+c->getSuitString(),QVariant::fromValue(p))){
				room->damage(DamageStruct("zhanyan",source,p,1,DamageStruct::Fire));
			}
		}
	}
	source->setMark("zhanyanUse",0);
	foreach (ServerPlayer *p, targets){
		if(p->getTag("zhanyanDamage").toBool()) continue;
		return;
	}
	room->askForUseCard(source,"@@zhanyan","zhanyan1");
}

class Zhanyanvs : public ZeroCardViewAsSkill
{
public:
	Zhanyanvs() : ZeroCardViewAsSkill("zhanyan")
	{
		response_pattern = "@@zhanyan";
	}

	const Card *viewAs() const
	{
		return new ZhanyanCard();
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@zhanyan")>0;
	}
};

class Zhanyan : public TriggerSkill
{
public:
	Zhanyan() : TriggerSkill("zhanyan")
	{
		events << DamageDone << CardsMoveOneTime;
		view_as_skill = new Zhanyanvs;
		frequency = Limited;
		limit_mark = "@zhanyan";
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const
	{
		if(event==DamageDone){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.reason==objectName())
				player->setTag("zhanyanDamage", true);
		}else{
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if(player==move.from&&player->getMark("zhanyanUse")>0){
				int n = 0;
				for (int i = 0; i < move.from_places.length(); i++) {
					if(move.from_places[i]==Player::PlaceHand||move.from_places[i]==Player::PlaceEquip)
						n++;
				}
				if(n>0)
					player->drawCards(n,objectName());
			}
		}
		return false;
	}
};

class Qinqiang : public TriggerSkill
{
public:
	Qinqiang() : TriggerSkill("qinqiang")
	{
		events << EventPhaseStart;
	}
	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(player->getPhase()==Player::Finish){
			Card*dc = room->askForDiscard(player,objectName(),4,1,true,false,"qinqiang0",".",objectName()+"$-1");
			if(dc){
				player->drawCards(dc->subcardsLength()*2,objectName());
			}
		}
		return false;
	}
};

class Yizhen : public TriggerSkill
{
public:
	Yizhen() : TriggerSkill("yizhen")
	{
		events << TargetSpecified << CardFinished;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		if (event == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0){
				foreach (ServerPlayer *p, use.to) {
					if(use.from!=p&&p->hasSkill(objectName())){
						QHash<QString,int> s2i;
						foreach (const Card*c, p->getHandcards()) {
							s2i[c->getSuitString()]++;
						}
						bool hasmax = true;
						bool hasmin = true;
						foreach (QString s, s2i.keys()) {
							if(s!=use.card->getSuitString()&&s2i[s]>=s2i[use.card->getSuitString()])
								hasmax = false;
							if(s2i[s]<s2i[use.card->getSuitString()])
								hasmin = false;
						}
						if(hasmax){
							room->sendCompulsoryTriggerLog(p,this);
							room->askForCard(p,".|"+use.card->getSuitString()+"!","yizhen0:"+use.card->getSuitString(),data);
						}
						if(hasmin){
							if(!hasmax)
								room->sendCompulsoryTriggerLog(p,this);
							room->setCardFlag(use.card,"yizhenBf"+p->objectName());
						}
					}
				}
			}
		}else{
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0){
				foreach (ServerPlayer *p, use.to) {
					if(p->isAlive()&&use.card->hasFlag("yizhenBf"+p->objectName())){
						if(room->getCardOwner(use.card->getEffectiveId())) continue;
						p->obtainCard(use.card);
					}
				}
			}
		}
		return false;
	}
};

class ThJizhan : public TriggerSkill
{
public:
	ThJizhan() : TriggerSkill("thjizhan")
	{
		events << TargetSpecified << PreCardUsed << CardFinished << ConfirmDamage;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target!=nullptr;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isDamageCard()&&player->isAlive()){
				player->addMark("thjizhanUse-Clear");
				if(player->hasFlag("CurrentPlayer")&&player->getMark("thjizhanUse-Clear")<2&&player->hasSkill(objectName())){
					int n = 2-player->getMark("thjizhanNum-Clear");
					if(n<1) return false;
					ServerPlayer *tp = room->askForPlayerChosen(player,use.to,objectName()+"$-1","thjizhan0:"+QString::number(n),true,true);
					if(tp){
						room->setCardFlag(use.card,"thjizhanBf");
						room->setCardFlag(use.card,"thjizhanBf"+tp->objectName());
						room->setCardFlag(use.card,"thjizhanNum:"+QString::number(n));
					}
				}
			}
		}else if(event == ConfirmDamage){
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.card&&damage.card->hasFlag("thjizhanBf")){
				foreach (QString f, damage.card->getFlags()) {
					if(f.contains("thjizhanNum:")){
						int n = f.split(":").last().toInt();
						if(n<1) continue;
						return player->damageRevises(data,n);
					}
				}
			}
		}else if(event == CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->hasFlag("thjizhanBf")){
				if(use.card->hasFlag("DamageDone")) return false;
				foreach (ServerPlayer *p, use.to) {
					if(p->isAlive()&&player->isAlive()&&use.card->hasFlag("thjizhanBf"+p->objectName())){
						room->damage(DamageStruct(objectName(),p,player));
					}
				}
			}
		}else if(event == PreCardUsed){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0&&!use.card->isDamageCard()){
				player->addMark("thjizhanNum-Clear");
			}
		}
		return false;
	}
};

class Chuxia : public TriggerSkill
{
public:
	Chuxia() : TriggerSkill("chuxia")
	{
		events << CardUsed << DamageDone;
		frequency = Compulsory;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0){
				foreach (ServerPlayer *p, use.to) {
					if(player!=p&&p->hasSkill(objectName())){
						bool hasHp = player->getHp()>p->getHp();
						bool hasDamage = p->getMark(player->objectName()+"chuxiaFrom_lun");
						if(hasHp||hasDamage){
							room->sendCompulsoryTriggerLog(p,this);
							QList<int>ids = player->handCards();
							qShuffle(ids);
							foreach (int id, ids) {
								if(player->canDiscard(id)){
									room->throwCard(id,objectName(),player);
									break;
								}
							}
						}
						if(hasHp&&hasDamage)
							p->drawCards(1,objectName());
					}
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.from){
				player->addMark(damage.from->objectName()+"chuxiaFrom_lun");
			}
		}
		return false;
	}
};

ZongtaoCard::ZongtaoCard()
{
	target_fixed = true;
}

void ZongtaoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	foreach (QString m, source->getMarkNames()) {
		if(m.contains("zongtaoType"))
			source->setMark(m,0);
	}
	QStringList suits,has;
	suits << "heart" << "diamond" << "spade" << "club";
	for (int i = 0; i <= source->getLostHp(); i++) {
		QString s = room->askForChoice(source,"zongtao",suits.join("+"));
		if(s=="cancel") break;
		suits.removeOne(s);
		has << s;
		if(suits.isEmpty()) break;
		if(i==0) suits << "cancel";
	}
	QList<const Card*>cs;
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		cs << p->getCards("ej");
	}
	foreach (int id, room->getDiscardPile()+room->getDrawPile()) {
		cs << Sanguosha->getCard(id);
	}
	qShuffle(cs);
	Card*dc = dummyCard();
	foreach (const Card*c, cs) {
		if(has.contains(c->getSuitString())){
			has.removeOne(c->getSuitString());
			dc->addSubcard(c);
		}
	}
	source->obtainCard(dc);
}

class Zongtaovs : public ZeroCardViewAsSkill
{
public:
	Zongtaovs() : ZeroCardViewAsSkill("zongtao")
	{
	}

	const Card *viewAs() const
	{
		return new ZongtaoCard();
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("ZongtaoCard")<1;
	}
};

class Zongtao : public TriggerSkill
{
public:
	Zongtao() : TriggerSkill("zongtao")
	{
		events << CardUsed;
		view_as_skill = new Zongtaovs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0&&player->usedTimes("ZongtaoCard")>0&&player->getMark(use.card->getType()+"zongtaoType")<1){
				player->addMark(use.card->getType()+"zongtaoType");
				int n = 0;
				foreach (QString m, player->getMarkNames()) {
					if(m.contains("zongtaoType")) n++;
				}
				if(n>2) room->addPlayerHistory(player,"ZongtaoCard",0);
			}
		}
		return false;
	}
};

class ThJizhanmc : public TriggerSkill
{
public:
	ThJizhanmc() : TriggerSkill("thjizhanmc")
	{
		events << CardUsed;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getTypeId()>0&&use.card->isBlack()&&!use.card->hasFlag("ComboMoves")&&player->getMark("thjizhanmcBan-Clear")<1){
				const Card*cmc = player->getTag("ComboMovesCard").value<const Card*>();
				if(cmc&&cmc->isKindOf("EquipCard")){
					use.card->setFlags("ComboMoves");
					player->addMark("thjizhanmcUse-Clear");
					QString cho = "thjizhanmc2";
					QList<ServerPlayer *>tps;
					foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
						if(player->canDiscard(p,"he")){
							cho = "thjizhanmc1+thjizhanmc2";
							tps << p;
						}
					}
					room->sendCompulsoryTriggerLog(player,this);
					int n = player->getMark("thjizhanmcUse-Clear");
					if(room->askForChoice(player,objectName(),cho,data)=="thjizhanmc1"){
						for (int i = 0; i < n; i++) {
							ServerPlayer *tp = room->askForPlayerChosen(player,tps,objectName(),"thjizhanmc10:"+QString::number(n-i),true);
							if(tp){
								int id = room->askForCardChosen(player,tp,"he",objectName(),false,Card::MethodDiscard);
								if(id>=0){
									room->throwCard(id,objectName(),tp,player);
									if(player->isDead()) break;
								}
								tps.clear();
								foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
									if(player->canDiscard(p,"he")) tps << p;
								}
							}else
								break;
						}
					}else{
						ServerPlayer *tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"thjizhanmc20:"+QString::number(n));
						if(tp){
							player->addMark("thjizhanmcBan-Clear");
							room->damage(DamageStruct(objectName(),player,tp,n));
						}
					}
				}
			}
		}
		return false;
	}
};

class Zhifengvs : public ViewAsSkill
{
public:
	Zhifengvs() : ViewAsSkill("zhifeng")
	{
	}

	bool viewFilter(const QList<const Card *> &cards, const Card *to_select) const
	{
		int n = Self->getHandcardNum()-Self->getHp();
		if(n>0){
			return to_select->isBlack();
		}else if(n<0){
			return cards.isEmpty()&&to_select->isRed();
		}
		return true;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		int n = Self->getHandcardNum()-Self->getHp();
		if(n>0){
			if(cards.isEmpty()) return nullptr;
			Card*dc = Sanguosha->cloneCard("analeptic");
			dc->setSkillName(objectName());
			dc->addSubcards(cards);
			return dc;
		}else if(n<0){
			if(cards.isEmpty()) return nullptr;
			Card*dc = Sanguosha->cloneCard("slash");
			dc->setSkillName(objectName());
			dc->addSubcards(cards);
			return dc;
		}
		Card*dc = Sanguosha->cloneCard("duel");
		dc->setSkillName(objectName());
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if(pattern.contains("@@zhifeng")) return true;
		if(!player->hasTurn()||player->getMark("zhifengUse-Clear")>player->getSiblings().length()) return false;
		int n = player->getHandcardNum()-player->getHp();
		if(n>0){
			return pattern.contains("analeptic");
		}else if(n<0){
			return pattern.contains("slash");
		}
		return pattern.contains("duel");
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if(player->getMark("zhifengUse-Clear")>player->getSiblings().length()) return false;
		int n = player->getHandcardNum()-player->getHp();
		if(n>0){
			return Analeptic::IsAvailable(player);
		}else if(n<0){
			Card*dc = Sanguosha->cloneCard("slash");
			dc->setSkillName(objectName());
			dc->deleteLater();
			return dc->isAvailable(player);
		}
		Card*dc = Sanguosha->cloneCard("duel");
		dc->setSkillName(objectName());
		dc->deleteLater();
		return dc->isAvailable(player);
	}
};

class Zhifeng : public TriggerSkill
{
public:
	Zhifeng() : TriggerSkill("zhifeng")
	{
		events << CardUsed;
		view_as_skill = new Zhifengvs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->getSkillNames().contains(objectName())){
				room->addPlayerMark(player,"zhifengUse-Clear");
				if(use.card->isKindOf("Slash")){
					int n = player->getMaxHp()-player->getHandcardNum();
					if(n>0) player->drawCards(n,objectName());
				}else if(use.card->isKindOf("Analeptic")){
					use.m_addHistory = false;
					data.setValue(use);
				}
			}
		}
		return false;
	}
};

class ThWeijing : public TriggerSkill
{
public:
	ThWeijing() : TriggerSkill("thweijing")
	{
		events << EventPhaseChanging;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive()&&target->getKingdom()=="wu";
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().from != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(p->isAlive()&&p->hasSkill(objectName())&&p->askForSkillInvoke(objectName()+"$-1",player)){
					QString choices = "thweijing1";
					if(player->getCardCount()>0) choices = "thweijing1+thweijing2";
					if(room->askForChoice(p,objectName(),choices,QVariant::fromValue(player))=="thweijing1"){
						room->damage(DamageStruct(objectName(),p,player));
					}else{
						Card*dc = room->askForExchange(player,objectName(),1,1,true,"thweijing20:"+p->objectName(),false);
						if(dc){
							room->giveCard(player,p,dc,objectName());
							if(player->isAlive())
								room->askForUseCard(player,"@@zhifeng","zhifeng0");
						}
					}
					if(player->isDead()) break;
				}
			}
		}
		return false;
	}
};

ChuanyuCard::ChuanyuCard()
{
	will_throw = false;
}

bool ChuanyuCard::targetFilter(const QList<const Player *> &targets, const Player *target, const Player *Self) const
{
	if(user_string=="@@chuanyu1"){
		return targets.isEmpty()||(target->getMark("chuanyuBf_lun")>0&&target->canSlash(targets.first(),false));
	}
	return targets.isEmpty()&&target!=Self;
}

bool ChuanyuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	if(user_string=="@@chuanyu1"){
		return targets.length()>1;
	}
	return targets.length()>0;
}

void ChuanyuCard::onUse(Room *room, CardUseStruct &use) const
{
	if(user_string=="@@chuanyu1"){
		ServerPlayer *tp = use.to.first();
		use.to.removeOne(tp);
		foreach (ServerPlayer *p, use.to) {
			if(p->isAlive()&&tp->isAlive())
				room->askForUseSlashTo(p,tp,"chuanyu1:"+tp->objectName(),false);
		}
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if(p->getMark("chuanyuBf_lun")>0){
				foreach (const Card *h, p->getHandcards()) {
					if(h->hasTip("chuan_yu"))
						room->throwCard(h,"chuanyu",nullptr);
				}
			}
		}
	}else{
		foreach (ServerPlayer *p, use.to) {
			room->addPlayerMark(p,"chuanyuBf_lun");
			room->giveCard(use.from,p,this,"chuanyu");
			if(p->handCards().contains(getEffectiveId())){
				room->setCardFlag(getEffectiveId(),"chuanyuBf");
				room->setCardTip(getEffectiveId(),"chuan_yu");
			}
		}
	}
}

class Chuanyuvs : public ViewAsSkill
{
public:
	Chuanyuvs() : ViewAsSkill("chuanyu")
	{
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *) const
	{
		return selected.isEmpty()&&Sanguosha->getCurrentCardUsePattern()=="@@chuanyu";
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(Sanguosha->getCurrentCardUsePattern()=="@@chuanyu1"){
			ChuanyuCard*dc = new ChuanyuCard;
			dc->setUserString("@@chuanyu1");
			return dc;
		}
		if(cards.isEmpty()) return nullptr;
		ChuanyuCard*dc = new ChuanyuCard;
		dc->addSubcards(cards);
		return dc;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return pattern.contains("@@chuanyu");
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return false;
	}
};

class Chuanyu : public TriggerSkill
{
public:
	Chuanyu() : TriggerSkill("chuanyu")
	{
		events << CardsMoveOneTime << RoundStart << RoundEnd;
		view_as_skill = new Chuanyuvs;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if(move.to_place==Player::DiscardPile&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_USE){
				foreach (int id, move.card_ids) {
					if(room->getCardOwner(id)) continue;
					const Card *c = Sanguosha->getCard(id);
					if(c->hasFlag("chuanyuBf")){
						QList<ServerPlayer *>tps;
						foreach (ServerPlayer *p, room->getAlivePlayers()) {
							if(p->getMark("chuanyuBf_lun")<1) tps << p;
						}
						ServerPlayer *tp = room->askForPlayerChosen(player,tps,objectName(),"chuanyu0",true,true);
						if(tp){
							player->peiyin(this);
							room->addPlayerMark(tp,"chuanyuBf_lun");
							room->giveCard(player,tp,c,objectName());
							if(tp->handCards().contains(id)){
								room->setCardFlag(id,"chuanyuBf");
								room->setCardTip(id,"chuan_yu");
							}
						}
					}
				}
			}
		}else if(event == RoundStart){
			if(player->askForSkillInvoke(this)){
				player->peiyin(this);
				player->drawCards(1,objectName());
				room->askForUseCard(player,"@@chuanyu","chuanyu0");
			}
		}else if(event == RoundEnd){
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if(p->getMark("chuanyuBf_lun")>0){
					room->askForUseCard(player,"@@chuanyu1","chuanyu1");
					break;
				}
			}
		}
		return false;
	}
};

class Yitou : public TriggerSkill
{
public:
	Yitou() : TriggerSkill("yitou")
	{
		events << EventPhaseStart << Damage;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if(event==EventPhaseStart){
			if(player->getPhase()==Player::Play){
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					if(p->getHandcardNum()>player->getHandcardNum())
						return false;
				}
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if(p->hasSkill(objectName())&&p->getHandcardNum()>0
					&&p->askForSkillInvoke(this,player)){
						p->peiyin(this);
						Card*dc = dummyCard(p->getHandcards());
						room->giveCard(p,player,dc,objectName());
						room->setPlayerMark(player,"&yitou+#"+p->objectName(),1);
					}
				}
			}else if(player->getPhase()==Player::RoundStart){
				foreach (ServerPlayer *p, room->getAlivePlayers()) {
					room->setPlayerMark(p,"&yitou+#"+player->objectName(),0);
				}
			}
		}else{
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if(player->getMark("&yitou+#"+p->objectName())>0){
					room->sendCompulsoryTriggerLog(p,objectName());
					p->drawCards(1,objectName());
				}
			}
		}
		return false;
	}
};

ShurenCard::ShurenCard()
{
}

bool ShurenCard::targetFilter(const QList<const Player *> &targets, const Player *target, const Player *Self) const
{
	return targets.isEmpty()&&target!=Self;
}

void ShurenCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	QStringList choices;
	for (int i = 0; i < 5; i++) {
		if(source->hasEquipArea(i))
			choices << QString("EquipArea%1").arg(i);
	}
	QString choice = room->askForChoice(source,"shuren",choices.join("+"));
	choice.remove("EquipArea");
	int n = choice.toInt();
	bool has = source->getEquip(n)!=nullptr;
	source->throwEquipArea(n);
	foreach (ServerPlayer *p, targets){
		QList<int>ids = room->showDrawPile(source,3,"shuren");
		room->fillAG(ids);
		n = room->askForAG(source,ids,false,"shuren");
		room->takeAG(source,n);
		ids.removeOne(n);
		if(p->isAlive()){
			n = room->askForAG(p,ids,false,"shuren");
			room->takeAG(p,n);
			ids.removeOne(n);
		}
		room->clearAG();
		room->throwCard(ids,"shuren",nullptr);
	}
	if(has&&source->isAlive()){
		room->addPlayerHistory(source,"ShurenCard",0);
		room->recover(source,RecoverStruct("shuren",source));
	}
}

class Shuren : public ZeroCardViewAsSkill
{
public:
	Shuren() : ZeroCardViewAsSkill("shuren")
	{
	}

	const Card *viewAs() const
	{
		return new ShurenCard();
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("ShurenCard")<1&&player->hasEquipArea();
	}
};

class Sharan : public TriggerSkill
{
public:
	Sharan() : TriggerSkill("sharan")
	{
		events << Damage << Damaged;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		for (int i = 0; i < damage.damage; i++) {
			QList<int>ids = room->getDrawPile()+room->getDiscardPile();
			qShuffle(ids);
			foreach (int id, ids){
				Card*c = Sanguosha->getCard(id);
				if(c->isKindOf("EquipCard")&&c->isAvailable(player)){
					room->sendCompulsoryTriggerLog(player,this);
					room->useCard(CardUseStruct(c,player));
					QStringList choices;
					for (int i = 0; i < 5; i++) {
						if(player->hasEquipArea(i)) continue;
						choices << QString("EquipArea%1").arg(i);
					}
					if(choices.isEmpty()||player->isDead()) break;
					choices << "cancel";
					QString choice = room->askForChoice(player,objectName(),choices.join("+"));
					if(choice=="cancel") break;
					choice.remove("EquipArea");
					player->obtainEquipArea(choice.toInt());
					break;
				}
			}
			if(player->isDead()) break;
		}
		return false;
	}
};

class Hengye : public TriggerSkill
{
public:
	Hengye() : TriggerSkill("hengye")
	{
		events << Death << Damage << DrawNCards << EventPhaseChanging;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==Death){
			DeathStruct death = data.value<DeathStruct>();
			if(death.damage&&death.damage->from==player&&player->hasSkill(objectName())){
				room->sendCompulsoryTriggerLog(player,this);
				foreach (QString m, player->getMarkNames()){
					if(m.contains("&hengye+:+"))
						room->setPlayerMark(player,m,0);
				}
				room->addSlashCishu(player,-player->getMark("hengye2"),false);
				room->addAttackRange(player,-player->getMark("hengye3"),false);
				room->addMaxCards(player,-player->getMark("hengye4"),false);
				for (int i = 1; i <= 4; i++) {
					player->setMark(QString("hengye%1").arg(i),0);
				}
			}
		}else if (event == DrawNCards){
			DrawStruct draw = data.value<DrawStruct>();
			if(draw.reason!="draw_phase"||player->getMark("hengye1")<1) return false;
			if(player->hasSkill(objectName())){
				draw.num += player->getMark("hengye1");
				data.setValue(draw);
			}
		}else if (event == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.from != Player::NotActive) return false;
			if(player->hasSkill(objectName())){
				for (int i = 1; i <= 4; i++) {
					if(player->getMark(QString("hengye%1").arg(i))>2){
						room->sendCompulsoryTriggerLog(player,this);
						room->recover(player,RecoverStruct(objectName(),player));
						break;
					}
				}
			}
		}else if(player->hasSkill(objectName())){
			QStringList choices;
			if(player->getMark("hengye1")<3)
				choices << "hengye1";
			if(player->getMark("hengye2")<3)
				choices << "hengye2";
			if(player->getMark("hengye3")<3)
				choices << "hengye3";
			if(player->getMark("hengye4")<3)
				choices << "hengye4";
			if(choices.isEmpty()) return false;
			room->sendCompulsoryTriggerLog(player,this);
			QString choice = room->askForChoice(player,objectName(),choices.join("+"),data);
			player->addMark(choice);
			foreach (QString m, player->getMarkNames()){
				if(m.contains("&hengye+:+"))
					room->setPlayerMark(player,m,0);
			}
			choices.clear();
			for (int i = 1; i <= 4; i++) {
				choices << QString::number(player->getMark(QString("hengye%1").arg(i)));
			}
			room->setPlayerMark(player,"&hengye+:+"+choices.join("+"),1);
			if(choice.contains("2"))
				room->addSlashCishu(player,1,false);
			else if(choice.contains("3"))
				room->addAttackRange(player,1,false);
			else if(choice.contains("4"))
				room->addMaxCards(player,1,false);
		}
		return false;
	}
};

class Yingbo : public TriggerSkill
{
public:
	Yingbo() : TriggerSkill("yingbo")
	{
		events << CardUsed << CardFinished << ConfirmDamage;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isDamageCard()){
				QString cn = use.card->objectName(false);
				foreach (ServerPlayer *p, room->getAlivePlayers()){
					p->addMark(cn+"yingboUse_lun");
				}
				if(player->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(player,this);
					if(player->getMark(cn+"yingboUse_lun")>1){
						room->setCardFlag(use.card,"yingboBf1");
					}else{
						use.no_respond_list << "_ALL_TARGETS";
						room->setCardFlag(use.card,"yingboBf2");
						data.setValue(use);
					}
				}
			}
		}else if(event == CardFinished){
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->hasFlag("yingboBf2")&&!room->getCardOwner(use.card->getEffectiveId())){
				ServerPlayer *tp = room->askForPlayerChosen(player,room->getOtherPlayers(player),objectName(),"yingboBf2",true);
				if(tp){
					room->doAnimate(1,player->objectName(),tp->objectName());
					room->giveCard(player,tp,use.card,objectName());
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(damage.card&&damage.card->hasFlag("yingboBf1")){
				damage.damage++;
				damage.nature = DamageStruct::Fire;
				data.setValue(damage);
			}
		}
		return false;
	}
};

class Mingfang : public TriggerSkill
{
public:
	Mingfang() : TriggerSkill("mingfang")
	{
		events << EventPhaseStart << DamageForseen;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==DamageForseen){
			foreach (const Card*c, player->getHandcards()){
				if(c->hasTip("mingfang")){
					player->damageRevises(data,1);
					break;
				}
			}
		}else if(player->hasSkill(objectName())){
			if(player->getPhase()==Player::RoundStart){
				QList<ServerPlayer*>tps;
				foreach (ServerPlayer*p,room->getOtherPlayers(player)){
					if(p->getHandcardNum()>0) tps << p;
				}
				ServerPlayer*tp = room->askForPlayerChosen(player,tps,objectName()+"$-1","mingfang0",true,true);
				if(tp){
					const Card*c = room->askForCardShow(tp,player,objectName());
					if(c){
						room->showCard(tp,c->getEffectiveId());
						room->setCardTip(c->getEffectiveId(),"mingfang");
					}
				}
			}else if(player->getPhase()==Player::Play||(player->getPhase()==Player::Finish&&player->getMark("mingfangBf-Clear")>0)){
				QList<ServerPlayer*>tps;
				foreach (ServerPlayer*p,room->getOtherPlayers(player)){
					if(player->canPindian(p)) tps << p;
				}
				ServerPlayer*tp = room->askForPlayerChosen(player,tps,objectName()+"$-1","mingfang1",true,true);
				if(tp){
					if(player->pindian(tp,objectName())){
						player->addMark("mingfangBf-Clear");
						room->damage(DamageStruct(objectName(),player,tp));
					}else{
						player->drawCards(1,objectName());
						room->setPlayerChained(player,true);
						room->setPlayerChained(tp,true);
					}
				}
			}
		}
		return false;
	}
};

class Jibian : public TriggerSkill
{
public:
	Jibian() : TriggerSkill("jibian")
	{
		events << PindianVerifying;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		if (event == PindianVerifying) {
			PindianStruct*pindian = data.value<PindianStruct*>();
			if (pindian->from->hasSkill(objectName())){
				if(pindian->from->askForSkillInvoke(objectName()+"$-1",data)){
					int n = room->askForChoice(pindian->from,objectName(),"1+2+3",data).toInt();
					pindian->from->drawCards(n,objectName());
					pindian->from_number = qMax(1,pindian->from_number-n);
					data.setValue(pindian);
				}
			}
			if (pindian->to->hasSkill(objectName())){
				if(pindian->to->askForSkillInvoke(objectName()+"$-1",data)){
					int n = room->askForChoice(pindian->to,objectName(),"1+2+3",data).toInt();
					pindian->to->drawCards(n,objectName());
					pindian->to_number = qMax(1,pindian->to_number-n);
					data.setValue(pindian);
				}
			}
		}
		return false;
	}
};

ZhenyuCard::ZhenyuCard()
{
}

bool ZhenyuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return false;
	Card *card = Sanguosha->cloneCard(user_string.split("+").last());
	card->deleteLater();
	return card->targetFilter(targets, to_select, Self);
}

bool ZhenyuCard::targetFixed() const
{
	if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").last());
	card->deleteLater();
	return card->targetFixed();
}

bool ZhenyuCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").last());
	card->deleteLater();
	return card->targetsFeasible(targets, Self);
}

const Card *ZhenyuCard::validate(CardUseStruct &use) const
{
	Room *room = use.from->getRoom();
	QList<ServerPlayer *>tps;
	foreach (ServerPlayer*p,room->getAlivePlayers()){
		if(p->isChained()) continue;
		tps << p;
	}
	ServerPlayer*tp = room->askForPlayerChosen(use.from,tps,"zhenyu","zhenyu0",false,true);
	if(tp){
		room->setPlayerChained(tp,true);
		QString cn = room->askForChoice(use.from,"zhenyu",user_string);
		room->setPlayerMark(use.from,"&zhenyu+:+"+cn,1);
		room->addPlayerMark(use.from,"zhenyuBan");
		Card *c = Sanguosha->cloneCard(cn);
		c->setSkillName("_zhenyu");
		c->deleteLater();
		return c;
	}
	return nullptr;
}

const Card *ZhenyuCard::validateInResponse(ServerPlayer *source) const
{
	Room *room = source->getRoom();
	QList<ServerPlayer *>tps;
	foreach (ServerPlayer*p,room->getAlivePlayers()){
		if(p->isChained()) continue;
		tps << p;
	}
	ServerPlayer*tp = room->askForPlayerChosen(source,tps,"zhenyu","zhenyu0",false,true);
	if(tp){
		room->setPlayerChained(tp,true);
		QString cn = room->askForChoice(source,"zhenyu",user_string);
		room->setPlayerMark(source,"&zhenyu+:+"+cn,1);
		room->addPlayerMark(source,"zhenyuBan");
		Card *c = Sanguosha->cloneCard(cn);
		c->setSkillName("_zhenyu");
		c->deleteLater();
		return c;
	}
	return nullptr;
}

class Zhenyuvs : public ZeroCardViewAsSkill
{
public:
	Zhenyuvs() : ZeroCardViewAsSkill("zhenyu")
	{
	}

	const Card *viewAs() const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern.isEmpty()){
			const Card *c = Self->getTag(objectName()).value<const Card *>();
			if(c==nullptr) return nullptr;
			pattern = c->objectName();
		}
		SkillCard*sc = new ZhenyuCard();
		sc->setUserString(pattern);
		return sc;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if(Sanguosha->getCurrentCardUseReason()==CardUseStruct::CARD_USE_REASON_RESPONSE
		||player->getMark("zhenyuBan")>0||pattern.contains("@")) return false;
		foreach (QString cn, pattern.split("+")){
			Card*dc = Sanguosha->cloneCard(cn);
			if(dc){
				dc->deleteLater();
				if(dc->isKindOf("BasicCard")){
					foreach (const Player*p,player->getAliveSiblings(true)){
						if(p->isChained()) continue;
						return true;
					}
				}
			}
		}
		return false;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		foreach (const Player*p,player->getAliveSiblings(true)){
			if(p->isChained()) continue;
			return player->getMark("zhenyuBan")<1;
		}
		return false;
	}
};

class Zhenyu : public TriggerSkill
{
public:
	Zhenyu() : TriggerSkill("zhenyu")
	{
		events << CardUsed;
		view_as_skill = new Zhenyuvs;
	}
	QDialog*getDialog()const
	{
		return GuhuoDialog::getInstance(objectName(),true,false);
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("BasicCard")&&use.m_isHandcard){
				QString cn = use.card->objectName(false);
				if(player->getMark("&zhenyu+:+"+cn)>0){
					room->setPlayerMark(player,"&zhenyu+:+"+cn,0);
					room->setPlayerMark(player,"zhenyuBan",0);
				}
			}
		}
		return false;
	}
};

JieluCard::JieluCard()
{
}

bool JieluCard::targetFilter(const QList<const Player *> &targets, const Player *target, const Player *) const
{
	return targets.isEmpty()&&target->isChained();
}

void JieluCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets){
		room->setPlayerChained(p,false);
		if(source->canDiscard(p,"he")){
			int id = room->askForCardChosen(source,p,"he","jielu",false,Card::MethodDiscard);
			if(id>=0){
				room->throwCard(id,"jielu",p,source);
				room->setPlayerMark(p,"&jielu+#"+source->objectName()+"-Clear",Sanguosha->getEngineCard(id)->getNumber());
			}
		}
	}
}

class Jieluvs : public ZeroCardViewAsSkill
{
public:
	Jieluvs() : ZeroCardViewAsSkill("jielu")
	{
	}

	const Card *viewAs() const
	{
		return new JieluCard();
	}

	bool isEnabledAtPlay(const Player *) const
	{
		return true;
	}
};

class Jielu : public TriggerSkill
{
public:
	Jielu() : TriggerSkill("jielu")
	{
		events << CardsMoveOneTime;
		view_as_skill = new Jieluvs;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if(move.to_place==Player::DiscardPile){
			foreach (ServerPlayer *p, room->getAllPlayers()){
				int n = p->getMark("&jielu+#"+player->objectName()+"-Clear");
				if(n<1) continue;
				room->setPlayerMark(p,"&jielu+#"+player->objectName()+"-Clear",0);
				if(Sanguosha->getEngineCard(move.card_ids.first())->getNumber()>n){
					room->damage(DamageStruct(objectName(),player,p));
				}
				if(player->isDead()) break;
			}
		}
		return false;
	}
};






TenyearXhPackage::TenyearXhPackage()
	: Package("tenyear_xh")
{
	General *thxing_sunjian = new General(this, "thxing_sunjian", "qun", 5,true,false,false,4);
	thxing_sunjian->addSkill(new Ruijun);
	thxing_sunjian->addSkill(new Gangyi);
	thxing_sunjian->addSkill(new GangyiLimit);

	General *thxing_fazheng = new General(this, "thxing_fazheng", "shu", 3);
	thxing_fazheng->addSkill(new ThZhiji);
	thxing_fazheng->addSkill(new Anji);

	General *thxing_zhangzhao = new General(this, "thxing_zhangzhao", "wu", 3);
	thxing_zhangzhao->addSkill(new Zhongyan);
	thxing_zhangzhao->addSkill(new Jinglun);
	addMetaObject<ZhongyanCard>();

	General *thxing_sunshangxiang = new General(this, "thxing_sunshangxiang", "wu", 4,false);
	thxing_sunshangxiang->addSkill(new Saying);
	thxing_sunshangxiang->addSkill(new Jiaohao);
	addMetaObject<SayingCard>();
	addMetaObject<JiaohaoCard>();

	General *thxing_zhangrang = new General(this, "thxing_zhangrang", "qun", 3);
	thxing_zhangrang->addSkill(new Duhai);
	thxing_zhangrang->addSkill(new Lingse);
	addMetaObject<LingseCard>();

	General *thxing_wenchou = new General(this, "thxing_wenchou", "qun", 4);
	thxing_wenchou->addSkill(new Lianzhan);
	thxing_wenchou->addSkill(new ThWeiming);

	General *thxing_taishici = new General(this, "thxing_taishici", "qun", 4);
	thxing_taishici->addSkill(new Chongwei);
	thxing_taishici->addSkill(new ChongweiDistance);
	thxing_taishici->addSkill(new Chongzhu);

	General *thxing_jiangwan = new General(this, "thxing_jiangwan", "shu", 3);
	thxing_jiangwan->addSkill(new Zhenting);
	thxing_jiangwan->addSkill(new Chiguo);

	General *thxing_yanliang = new General(this, "thxing_yanliang", "qun", 4);
	thxing_yanliang->addSkill(new ThJizhan);
	thxing_yanliang->addSkill(new Chuxia);




}
ADD_PACKAGE(TenyearXh)


TenyearMouPackage::TenyearMouPackage()
	: Package("tenyear_mou")
{
	General *thmou_huanggai = new General(this, "thmou_huanggai", "wu", 4);
	thmou_huanggai->addSkill(new Lieji);
	thmou_huanggai->addSkill(new Quzhou);
	addMetaObject<QuzhouCard>();

	General *thmou_dongcheng = new General(this, "thmou_dongcheng", "qun", 4);
	thmou_dongcheng->addSkill(new Baojia);
	thmou_dongcheng->addSkill(new Douwei);
	addMetaObject<DouweiCard>();

	General *thmou_caohong = new General(this, "thmou_caohong", "wei", 4);
	thmou_caohong->addSkill(new Yingjia);
	thmou_caohong->addSkill(new Xianju);
	addMetaObject<XianjuCard>();

	General *thmou_xunyu = new General(this, "thmou_xunyu", "wei", 3);
	thmou_xunyu->addSkill(new Shimou);
	thmou_xunyu->addSkill(new Bizuo);
	addMetaObject<ShimouCard>();

	General *thmou_liuxie = new General(this, "thmou_liuxie$", "qun", 3);
	thmou_liuxie->addSkill(new Zhanpan);
	thmou_liuxie->addSkill(new Chensheng);
	thmou_liuxie->addSkill(new Skill("tiancheng$", Skill::Compulsory));
	addMetaObject<ZhanpanCard>();

	General *hulie = new General(this, "hulie", "wei", 4);
	hulie->addSkill(new Chuanyu);
	hulie->addSkill(new Yitou);
	addMetaObject<ChuanyuCard>();

	General *thmou_caoang = new General(this, "thmou_caoang", "wei", 4);
	thmou_caoang->addSkill(new Fengmin);
	thmou_caoang->addSkill(new Zhiwang);

	General *thmou_huangquan = new General(this, "thmou_huangquan", "wei", 4);
	thmou_huangquan->addSkill(new Qiaodui);
	thmou_huangquan->addSkill(new Tuicheng);
	addMetaObject<QiaoduiCard>();

	General *thmou_luotong = new General(this, "thmou_luotong", "wu", 3);
	thmou_luotong->addSkill(new Juce);
	thmou_luotong->addSkill(new Kangming);

	General *thmou_luxun = new General(this, "thmou_luxun", "wu", 3);
	thmou_luxun->addSkill(new Junmou);
	thmou_luxun->addSkill(new Zhanyan);
	addMetaObject<ZhanyanCard>();

	General *thmou_xusheng = new General(this, "thmou_xusheng", "wu", 4);
	thmou_xusheng->addSkill(new Qinqiang);
	thmou_xusheng->addSkill(new Yizhen);

	General *thmou_lvmeng = new General(this, "thmou_lvmeng", "wu", 4);
	thmou_lvmeng->addSkill(new Hengye);
	thmou_lvmeng->addSkill(new Yingbo);

	General *thmou_jiangguan = new General(this, "thmou_jiangguan", "wei", 3);
	thmou_jiangguan->addSkill(new Mingfang);
	thmou_jiangguan->addSkill(new Jibian);

	General *thmou_zhuran = new General(this, "thmou_zhuran", "wu", 4);
	thmou_zhuran->addSkill(new Zhenyu);
	thmou_zhuran->addSkill(new Jielu);
	addMetaObject<ZhenyuCard>();
	addMetaObject<JieluCard>();









}
ADD_PACKAGE(TenyearMou)


TenyearWeiPackage::TenyearWeiPackage()
	: Package("tenyear_wei")
{
	General *thwei_sunquan = new General(this, "thwei_sunquan", "wu", 4);
	thwei_sunquan->addSkill(new Woheng);
	thwei_sunquan->addSkill(new Yugui);
	addMetaObject<WohengCard>();

	General *thwei_dongzhuo = new General(this, "thwei_dongzhuo", "qun", 5);
	thwei_dongzhuo->addSkill(new Guangyong);
	thwei_dongzhuo->addSkill(new Juchui);

	General *thwei_caopi = new General(this, "thwei_caopi", "wei", 4);
	thwei_caopi->addSkill(new Shugang);
	thwei_caopi->addSkill(new Dianlun);
	thwei_caopi->addSkill(new ThJiwei);
	addMetaObject<ShugangCard>();
	addMetaObject<ThJiweiCard>();
	addMetaObject<DianlunCard>();
	skills << new ThJiweivs;

	General *thwei_caocao = new General(this, "thwei_caocao", "wei", 4);
	thwei_caocao->addSkill(new Duoyue);
	thwei_caocao->addSkill(new Junhe);
	thwei_caocao->addSkill(new Xiongwei);
	addMetaObject<JunheCard>();

	General *thwei_machao = new General(this, "thwei_machao", "qun", 4);
	thwei_machao->addSkill(new Zongtao);
	thwei_machao->addSkill(new ThJizhanmc);
	addMetaObject<ZongtaoCard>();

	General *thwei_sunce = new General(this, "thwei_sunce", "wu", 4);
	thwei_sunce->addSkill(new Zhifeng);
	thwei_sunce->addSkill(new ThWeijing);

	General *thwei_sunshangxiang = new General(this, "thwei_sunshangxiang", "wu", 3,false);
	thwei_sunshangxiang->addSkill(new Shuren);
	thwei_sunshangxiang->addSkill(new Sharan);
	addMetaObject<ShurenCard>();



}
ADD_PACKAGE(TenyearWei)
