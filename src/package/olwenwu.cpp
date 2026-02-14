#include "olwenwu.h"
//#include "skill.h"
//#include "standard.h"
#include "clientplayer.h"
#include "engine.h"
#include "settings.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "yingbian.h"
#include "yjcm2013.h"

class JinBuchen : public TriggerSkill
{
public:
	JinBuchen() : TriggerSkill("jinbuchen")
	{
		events << Appear;
		hide_skill = true;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (!room->hasCurrent() || room->getCurrent() == player || room->getCurrent()->isNude()) return false;
		if (!player->askForSkillInvoke(this, room->getCurrent())) return false;
		room->broadcastSkillInvoke(objectName());
		int card_id = room->askForCardChosen(player, room->getCurrent(), "he", "jinbuchen");
		CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
		room->obtainCard(player, Sanguosha->getCard(card_id),
			reason, room->getCardPlace(card_id) != Player::PlaceHand);
		return false;
	}
};

JinYingshiCard::JinYingshiCard()
{
	target_fixed = true;
}

void JinYingshiCard::onUse(Room *room, CardUseStruct &card_use) const
{
	int maxhp = card_use.from->getMaxHp();
	if (maxhp <= 0) return;
	card_use.from->peiyin(getSkillName());
	QList<int> list = room->getNCards(maxhp);
	room->returnToTopDrawPile(list);
	room->fillAG(list, card_use.from);
	room->askForAG(card_use.from, list, true, "jinyingshi");
	room->clearAG(card_use.from);
}

class JinYingshi : public ZeroCardViewAsSkill
{
public:
	JinYingshi() : ZeroCardViewAsSkill("jinyingshi")
	{
		frequency = Compulsory;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMaxHp() > 0;
	}

	const Card *viewAs() const
	{
		return new JinYingshiCard;
	}
};

JinXiongzhiCard::JinXiongzhiCard()
{
	target_fixed = true;
}

void JinXiongzhiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	room->removePlayerMark(source, "@jinxiongzhiMark");
	room->doSuperLightbox(source, "jinxiongzhi");

	while (source->isAlive()) {
		QList<int> list = room->getNCards(1);
		const Card *card = Sanguosha->getCard(list.first());

		LogMessage log;
		log.type = "$ViewDrawPile";
		log.from = source;
		log.card_str = card->toString();
		room->sendLog(log, source);
		room->returnToTopDrawPile(list);
		if(source->canUse(card)) {
			if (card->targetFixed()) {
				room->useCard(CardUseStruct(card, source), true);
			} else {
				room->setPlayerMark(source, "jinxiongzhi_id-PlayClear", list.first());
				room->notifyMoveToPile(source, list, "jinxiongzhi", Player::DrawPile, true);
				const Card *use_card = room->askForUseCard(source, "@@jinxiongzhi!", "@jinxiongzhi:" + card->objectName());
				room->notifyMoveToPile(source, list, "jinxiongzhi", Player::DrawPile, false);
				if (!use_card) {
					foreach (ServerPlayer *p, room->getAlivePlayers()) {
						if (source->canUse(card, p)){
							room->useCard(CardUseStruct(card, source, p), true);
							break;
						}
					}
				}
			}
		}else
			break;
	}
}

class JinXiongzhi : public ViewAsSkill
{
public:
	JinXiongzhi() : ViewAsSkill("jinxiongzhi")
	{
		response_pattern = "@@jinxiongzhi!";
		frequency = Limited;
		limit_mark = "@jinxiongzhiMark";
		expand_pile = "#jinxiongzhi";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("@jinxiongzhiMark") > 0;
	}

	bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
	{
		if (Sanguosha->getCurrentCardUsePattern() == "@@jinxiongzhi!" && selected.isEmpty())
			return to_select->getId() == Self->getMark("jinxiongzhi_id-PlayClear");
		return false;
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if (Sanguosha->getCurrentCardUsePattern() == "@@jinxiongzhi!") {
			if (cards.isEmpty()) return nullptr;
			return cards.first();
		}
		return new JinXiongzhiCard;
	}
};

class JinQuanbian : public TriggerSkill
{
public:
	JinQuanbian(const QString &name) : TriggerSkill(name), name(name)
	{
		events << CardUsed << CardResponded;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	int getPriority(TriggerEvent) const
	{
		return 3;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (player->getPhase() != Player::Play) return false;

		const Card *card = nullptr;
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.m_isHandcard || use.card->isKindOf("SkillCard")) return false;
			card = use.card;
			if (name != "secondjinquanbian" || !use.card->isKindOf("EquipCard"))
				room->addPlayerMark(player, name + "_used-PlayClear");
		} else {
			CardResponseStruct res = data.value<CardResponseStruct>();
			if (!res.m_isHandcard || res.m_card->isKindOf("SkillCard")) return false;
			card = res.m_card;
			if (res.m_isUse)
				room->addPlayerMark(player, name + "_used-PlayClear");
		}

		if (!card || card->isKindOf("SkillCard")) return false;

		QString suitstring = card->getSuitString();
		if (suitstring == "no_suit_black" || suitstring == "no_suit_red")
			suitstring = "no_suit";
		if (player->getMark(name + "_" + suitstring + "-PlayClear") > 0) return false;
		room->addPlayerMark(player, name + "_" + suitstring + "-PlayClear");

		if (!player->hasSkill(objectName()) || player->getMaxHp() <= 0 || !player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		QList<int> list = room->getNCards(player->getMaxHp(), false);
		//room->returnToTopDrawPile(list);  guanxing会放回去，这里不能return回去
		QList<int> enabled, disabled;
		foreach (int id, list) {
			QString suitstr = Sanguosha->getCard(id)->getSuitString();
			if (suitstr == "no_suit_black" || suitstr == "no_suit_red")
				suitstr = "no_suit";
			if (suitstr == suitstring)
				disabled << id;
			else
				enabled << id;
		}
		if (enabled.isEmpty()) {
			room->fillAG(list, player);
			room->askForAG(player, list, true, objectName());
			room->clearAG(player);
			room->askForGuanxing(player, list, Room::GuanxingUpOnly);
			return false;
		}
		room->fillAG(list, player, disabled);
		int id = room->askForAG(player, enabled, false, objectName());
		room->clearAG(player);
		room->obtainCard(player, id, true);
		list.removeOne(id);
		if (player->isDead()) {
			room->returnToTopDrawPile(list);
			return false;
		}
		room->askForGuanxing(player, list, Room::GuanxingUpOnly);

		return false;
	}
private:
	QString name;
};

class JinQuanbianLimit : public CardLimitSkill
{
public:
	JinQuanbianLimit(const QString &name) : CardLimitSkill("#" + name + "-limit"), name(name)
	{
	}

	QString limitList(const Player *) const
	{
		return "use";
	}

	QString limitPattern(const Player *target) const
	{
		if (target->getPhase() == Player::Play && target->getMark(name + "_used-PlayClear") >= target->getMaxHp() && target->hasSkill(name))
			return ".|.|.|hand";
		return "";
	}
private:
	QString name;
};

class JinHuishi : public DrawCardsSkill
{
public:
	JinHuishi() : DrawCardsSkill("jinhuishi")
	{
	}

	int getDrawNum(ServerPlayer *player, int n) const
	{
		Room *room = player->getRoom();
		int draw_num = room->getDrawPile().length();
		if (draw_num >= 10) draw_num = draw_num % 10;
		if (!player->askForSkillInvoke(this, QString("jinhuishi_invoke:%1").arg(QString::number(draw_num)))) return n;
		room->broadcastSkillInvoke(objectName());
		if (draw_num <= 0) return -n;
		int get_num = floor(draw_num / 2);
		QList<int> list = room->getNCards(draw_num);
		if (get_num <= 0) {
			LogMessage log;
			log.type = "$ViewDrawPile";
			log.from = player;
			log.card_str = ListI2S(list).join("+");
			room->sendLog(log, player);
			room->fillAG(list, player);
			room->askForAG(player, list, true, objectName());
			room->clearAG(player);
			room->returnToEndDrawPile(list);
			return -n;
		}

		LogMessage log;
		log.type = "$ViewDrawPile";
		log.from = player;
		log.card_str = ListI2S(list).join("+");
		room->sendLog(log, player);

		room->fillAG(list, player);
		QList<int> enabled = list, disabled;
		while (disabled.length() < get_num) {
			if (player->isDead()||enabled.isEmpty()) break;
			int id = room->askForAG(player, enabled, false, objectName());
			room->takeAG(player,id,false,QList<ServerPlayer *>()<<player);
			enabled.removeOne(id);
			disabled << id;
		}
		room->clearAG(player);
		if (player->isAlive()) {
			DummyCard get(disabled);
			room->obtainCard(player, &get, false);
			list = enabled;
		}
		room->returnToTopDrawPile(list);

		return -n;
	}
};

JinQinglengCard::JinQinglengCard()
{
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodUse;
}

void JinQinglengCard::onUse(Room *room, CardUseStruct &card_use) const
{
	QString name = card_use.from->property("jinqingleng_now_target").toString();
	if (name.isEmpty()) return;
	ServerPlayer *target = room->findChild<ServerPlayer *>(name);
	if (!target || target->isDead()) return;

	const Card *card = Sanguosha->getCard(subcards.first());
	IceSlash *slash = new IceSlash(card->getSuit(), card->getNumber());
	slash->addSubcard(card);
	slash->deleteLater();
	slash->setSkillName("jinqingleng");
	if (!card_use.from->canSlash(target, slash, false)) return;

	room->useCard(CardUseStruct(slash, card_use.from, target));
}

class JinQinglengVS : public OneCardViewAsSkill
{
public:
	JinQinglengVS() : OneCardViewAsSkill("jinqingleng")
	{
		response_pattern = "@@jinqingleng";
		response_or_use = true;
	}

	bool viewFilter(const Card *to_select) const
	{
		QString name = Self->property("jinqingleng_now_target").toString();
		if (name.isEmpty()) return false;
		const Player *player = nullptr;
		foreach (const Player *p, Self->getAliveSiblings()) {
			if (p->objectName() == name) {
				player = p;
				break;
			}
		}
		if (player == nullptr || player->isDead()) return false;
		IceSlash *slash = new IceSlash(to_select->getSuit(), to_select->getNumber());
		slash->addSubcard(to_select);
		slash->deleteLater();
		slash->setSkillName("jinqingleng");
		return Self->canSlash(player, slash, false);
	}

	const Card *viewAs(const Card *originalCard) const
	{
		JinQinglengCard *c = new JinQinglengCard;
		c->addSubcard(originalCard);
		return c;
	}
};

class JinQingleng : public TriggerSkill
{
public:
	JinQingleng() : TriggerSkill("jinqingleng")
	{
		events << CardUsed << EventPhaseChanging;
		view_as_skill = new JinQinglengVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			if (!player->hasSkill(objectName())) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("SkillCard") || !use.card->getSkillNames().contains(objectName())) return false;
			QStringList target_names = player->property("jinqingleng_targets").toStringList();
			int num = 0;
			foreach (ServerPlayer *p, use.to) {
				if (target_names.contains(p->objectName())) continue;
				target_names << p->objectName();
				num++;
			}
			player->setProperty("jinqingleng_targets", target_names);
			if (num > 0) {
				room->sendCompulsoryTriggerLog(player, objectName(), true, true);
				player->drawCards(num, objectName());
			}
		} else if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (player->isDead()) return false;
				if (p->isDead() || !p->hasSkill(objectName())) continue;
				int num = player->getHp() + player->getHandcardNum();
				int draw_num = room->getDrawPile().length();
				if (draw_num >= 10)
					draw_num = draw_num % 10;
				if (num < draw_num) return false;
				room->setPlayerProperty(p, "jinqingleng_now_target", player->objectName());
				room->askForUseCard(p, "@@jinqingleng", "@jinqingleng:" + player->objectName());
			}
		}
		return false;
	}
};

class JinXuanmu : public TriggerSkill
{
public:
	JinXuanmu() : TriggerSkill("jinxuanmu")
	{
		events << Appear << DamageInflicted;
		frequency = Compulsory;
		hide_skill = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==Appear){
			if (room->getCurrent() != player){
				room->sendCompulsoryTriggerLog(player, this);
				room->setPlayerMark(player, "&jinxuanmu-Clear", 1);
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.damage<1||player->getMark("&jinxuanmu-Clear")<1) return false;
			room->broadcastSkillInvoke(objectName());
			LogMessage log;
			log.type = "#MobilejinjiuPrevent";
			log.from = player;
			log.arg = objectName();
			log.arg2 = QString::number(damage.damage);
			room->sendLog(log);
			room->notifySkillInvoked(player, objectName());
			return true;
		}
		return false;
	}
};

class Qiaoyan : public TriggerSkill
{
public:
	Qiaoyan() : TriggerSkill("qiaoyan")
	{
		events << DamageCaused;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.to->isDead() || !damage.to->hasSkill(objectName()) || damage.to == damage.from || damage.to->hasFlag("CurrentPlayer")) return false;
		room->sendCompulsoryTriggerLog(damage.to, objectName(), true, true);
		QList<int> zhu = damage.to->getPile("qyzhu");
		if (zhu.isEmpty()) {
			damage.to->drawCards(1, objectName());
			if (damage.to->isDead() || damage.to->isNude()) return true;
			const Card *card = room->askForExchange(damage.to, objectName(), 1, 1, true, "@qiaoyan-put");
			damage.to->addToPile("qyzhu", card);
			return true;
		} else {
			DummyCard get(zhu);
			room->obtainCard(player, &get);
		}
		return false;
	}
};

class Xianzhu : public PhaseChangeSkill
{
public:
	Xianzhu() : PhaseChangeSkill("xianzhu")
	{
		frequency = Compulsory;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Play) return false;
		QList<int> zhu = player->getPile("qyzhu");
		if (zhu.isEmpty()) return false;
		ServerPlayer *target = room->askForPlayerChosen(player, room->getAllPlayers(), objectName(), "@xianzhu-invoke", false, true);
		room->broadcastSkillInvoke(objectName());

		DummyCard get(zhu);
		if (target == player) {
			LogMessage log;
			log.type = "$KuangbiGet";
			log.from = player;
			log.arg = "qyzhu";
			log.card_str = ListI2S(zhu).join("+");
			room->sendLog(log);
		}
		room->obtainCard(target, &get);
		if (target == player || target->isDead()) return false;

		Slash *slash = new Slash(Card::NoSuit, 0);
		slash->setSkillName("_xianzhu");
		slash->deleteLater();
		if (target->isLocked(slash)) return false;
		QList<ServerPlayer *> tos;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (player->inMyAttackRange(p) && target->canSlash(p, slash, false))
				tos << p;
		}
		if (tos.isEmpty()) return false;
		player->tag["xianzhu_slash_from"] = QVariant::fromValue(target);
		ServerPlayer *to = room->askForPlayerChosen(player, tos, "xianzhu_target", "@xianzhu-target");
		player->tag.remove("xianzhu_slash_from");
		room->useCard(CardUseStruct(slash, target, to));
		return false;
	}
};

class JinCaiwangVS : public ZeroCardViewAsSkill
{
public:
	JinCaiwangVS(const QString &jincaiwang) : ZeroCardViewAsSkill(jincaiwang), jincaiwang(jincaiwang)
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Slash::IsAvailable(player) && player->getJudgingArea().length() == 1;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		return (pattern == "jink" && player->getHandcardNum() == 1) ||
			(pattern == "nullification" && player->getEquips().length() == 1 &&
					Sanguosha->currentRoomState()->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_RESPONSE) ||
				((pattern.contains("slash") || pattern.contains("Slash")) && player->getJudgingArea().length() == 1);
	}

	const Card *viewAs() const
	{
		switch (Sanguosha->currentRoomState()->getCurrentCardUseReason()) {
		case CardUseStruct::CARD_USE_REASON_PLAY: {
			Slash *slash = new Slash(Card::SuitToBeDecided, -1);
			slash->addSubcard(Self->getJudgingArea().first());
			slash->setSkillName(objectName());
			return slash;
		}
		case CardUseStruct::CARD_USE_REASON_RESPONSE:
		case CardUseStruct::CARD_USE_REASON_RESPONSE_USE: {
			QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
			if (pattern.contains("slash") || pattern.contains("Slash")) {
				Slash *slash = new Slash(Card::SuitToBeDecided, -1);
				slash->addSubcard(Self->getJudgingArea().first());
				slash->setSkillName(objectName());
				return slash;
			} else if (pattern == "jink") {
				Jink *jink = new Jink(Card::SuitToBeDecided, -1);
				jink->addSubcard(Self->getHandcards().first());
				jink->setSkillName(objectName());
				return jink;
			} else if (pattern == "nullification") {
				Nullification *nullification = new Nullification(Card::SuitToBeDecided, -1);
				nullification->addSubcard(Self->getEquips().first());
				nullification->setSkillName(objectName());
				return nullification;
			}
			return nullptr;
		}
		default:
			return nullptr;
		}
		return nullptr;
	}
private:
	QString jincaiwang;
};

class JinCaiwang : public TriggerSkill
{
public:
	JinCaiwang(const QString &jincaiwang) : TriggerSkill(jincaiwang), jincaiwang(jincaiwang)
	{
		events << CardResponded << CardUsed;
		view_as_skill = (jincaiwang == "jincaiwang") ? nullptr : new JinCaiwangVS(jincaiwang);
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
		if (!card || !tocard || card->isKindOf("SkillCard") || tocard->isKindOf("SkillCard") || !card->sameColorWith(tocard)) return false;
		if (!who || who->isDead() || who == player) return false;

		ServerPlayer *user = room->getCardUser(tocard);
		if (!user || user != who) return false;

		QList<ServerPlayer *> players;
		players << who << player;
		room->sortByActionOrder(players);

		foreach (ServerPlayer *p, players) {
			ServerPlayer *thrower, *victim;
			if (p == who) {
				thrower = who;
				victim = player;
			} else {
				thrower = player;
				victim = who;
			}
			if (thrower && thrower->isAlive() && thrower->hasSkill(objectName()) && victim && victim->isAlive() && !victim->isNude()) {
				QString prompt = QString("jincaiwang_discard:%1").arg(victim->objectName());
				if (victim->getMark("&jinnaxiang+#" + thrower->objectName()) > 0) {
					prompt = QString("jincaiwang_get:%1").arg(victim->objectName());
					if (!thrower->askForSkillInvoke(this, prompt)) continue;
					room->broadcastSkillInvoke(objectName());
					int id = room->askForCardChosen(thrower, victim, "he", objectName());
					CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, thrower->objectName());
					room->obtainCard(thrower, Sanguosha->getCard(id),
						reason, room->getCardPlace(id) != Player::PlaceHand);
				} else {
					if (!thrower->canDiscard(victim, "he")) continue;
					if (!thrower->askForSkillInvoke(this, prompt)) continue;
					room->broadcastSkillInvoke(objectName());
					int id = room->askForCardChosen(thrower, victim, "he", objectName(), false, Card::MethodDiscard);
					room->throwCard(id, victim, thrower);
				}
			}
		}
		return false;
	}
private:
	QString jincaiwang;
};

class JinNaxiang : public TriggerSkill
{
public:
	JinNaxiang() : TriggerSkill("jinnaxiang")
	{
		events << Damage << Damaged;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (event == Damage) {
			if (damage.from == damage.to || damage.to->isDead() || !damage.to->hasSkill(objectName())) return false;
			room->sendCompulsoryTriggerLog(damage.to, objectName(), true, true);
			room->setPlayerMark(damage.from, "&jinnaxiang+#" + damage.to->objectName(), 1);
		} else {
			if (!damage.from || damage.from->isDead() || damage.from == damage.to || !damage.from->hasSkill(objectName())) return false;
			room->sendCompulsoryTriggerLog(damage.from, objectName(), true, true);
			room->setPlayerMark(damage.to, "&jinnaxiang+#" + damage.from->objectName(), 1);
		}
		return false;
	}
};

class JinNaxiangClear : public PhaseChangeSkill
{
public:
	JinNaxiangClear() : PhaseChangeSkill("#jinnaxiang-clear")
	{
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->getPhase() == Player::RoundStart;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->getMark("&jinnaxiang+#" + player->objectName()) > 0)
				room->setPlayerMark(p, "&jinnaxiang+#" + player->objectName(), 0);
		}
		return false;
	}
};

ChexuanCard::ChexuanCard()
{
	target_fixed = true;
}

void ChexuanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	if (source->isDead() || !source->hasTreasureArea() || source->getTreasure()) return;
	QList<int> ids;
	int id1 = source->getDerivativeCard("_sichengliangyu", Player::PlaceTable);
	int id2 = source->getDerivativeCard("_tiejixuanyu", Player::PlaceTable);
	int id3 = source->getDerivativeCard("_feilunzhanyu", Player::PlaceTable);
	if (id1 > 0)
		ids << id1;
	if (id2 > 0)
		ids << id2;
	if (id3 > 0)
		ids << id3;
	if (ids.isEmpty()) return;

	room->fillAG(ids, source);
	int id = room->askForAG(source, ids, false, "chexuan");
	room->clearAG(source);

	CardMoveReason reason(CardMoveReason::S_REASON_PUT, "chexuan");
	CardsMoveStruct move(id, nullptr, source, Player::PlaceTable, Player::PlaceEquip, reason);
	room->moveCardsAtomic(move, true);
}

class ChexuanVS : public OneCardViewAsSkill
{
public:
	ChexuanVS() : OneCardViewAsSkill("chexuan")
	{
		filter_pattern = ".|black!";
	}

	const Card *viewAs(const Card *originalCard) const
	{
		ChexuanCard *c = new ChexuanCard;
		c->addSubcard(originalCard);
		return c;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->getTreasure();
	}
};

class Chexuan : public TriggerSkill
{
public:
	Chexuan() : TriggerSkill("chexuan")
	{
		events << CardsMoveOneTime;
		view_as_skill = new ChexuanVS;
		waked_skills = "_sichengliangyu,_tiejixuanyu,_feilunzhanyu";
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from != player || move.reason.m_reason == CardMoveReason::S_REASON_CHANGE_EQUIP || !move.from_places.contains(Player::PlaceEquip))
			return false;
		for (int i = 0; i < move.card_ids.length(); i++) {
			if (player->isDead()) return false;
			if (move.from_places.at(i) != Player::PlaceEquip) continue;
			const Card *card = Sanguosha->getCard(move.card_ids.at(i));
			if (!card->isKindOf("Treasure")) continue;

			if (!player->askForSkillInvoke(this)) return false;
			room->broadcastSkillInvoke(objectName());

			JudgeStruct judge;
			judge.who = player;
			judge.reason = objectName();
			judge.good = true;
			judge.pattern = ".|black";
			room->judge(judge);

			if (judge.isGood() && player->isAlive() && player->hasTreasureArea() && !player->getTreasure()) {
				QList<int> ids;
				int id1 = player->getDerivativeCard("_sichengliangyu", Player::PlaceTable);
				int id2 = player->getDerivativeCard("_tiejixuanyu", Player::PlaceTable);
				int id3 = player->getDerivativeCard("_feilunzhanyu", Player::PlaceTable);
				if (id1 > 0)
					ids << id1;
				if (id2 > 0)
					ids << id2;
				if (id3 > 0)
					ids << id3;
				if (ids.isEmpty()) continue;
				int id = ids.at(qrand() % ids.length());

				CardMoveReason reason(CardMoveReason::S_REASON_PUT, "chexuan");
				CardsMoveStruct move(id, nullptr, player, Player::PlaceTable, Player::PlaceEquip, reason);
				room->moveCardsAtomic(move, true);
			}
		}
		return false;
	}
};

class Qiangshou : public DistanceSkill
{
public:
	Qiangshou() : DistanceSkill("qiangshou")
	{
	}

	int getCorrect(const Player *from, const Player *) const
	{
		if (from->hasSkill(objectName())&&from->getTreasure())
			return -1;
		return 0;
	}
};

CaozhaoDialog *CaozhaoDialog::getInstance(const QString &object)
{
	static CaozhaoDialog *instance;
	if (instance == nullptr || instance->objectName() != object)
		instance = new CaozhaoDialog(object);

	return instance;
}

CaozhaoDialog::CaozhaoDialog(const QString &object)
	: GuhuoDialog(object)
{
}

bool CaozhaoDialog::isButtonEnabled(const QString &button_name) const
{
	QStringList names = Self->property("CaozhaoNames").toString().split("+");
	return !names.contains(button_name) && button_name != "normal_slash";
}

CaozhaoCard::CaozhaoCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void CaozhaoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	int first = subcards.first();
	room->showCard(source, first);
	QString name = user_string;
	LogMessage log;
	log.type = "#ShouxiChoice";
	log.from = source;
	log.arg = name;
	room->sendLog(log);

	QStringList names = source->property("CaozhaoNames").toString().split("+");
	names << name;
	room->setPlayerProperty(source, "CaozhaoNames", names.join("+"));

	QList<ServerPlayer *> targets;
	foreach (ServerPlayer *p, room->getAlivePlayers()) {
		if (p->getHp() <= source->getHp())
			targets << p;
	}
	if (targets.isEmpty()) return;
	ServerPlayer *target = room->askForPlayerChosen(source, targets, "caozhao", "@caozhao-target");
	room->doAnimate(1, source->objectName(), target->objectName());

	const Card *card = Sanguosha->getEngineCard(first);
	QStringList choices;
	choices << "view=" + card->objectName() + "=" + user_string << "losehp";
	QString choice = room->askForChoice(target, "caozhao", choices.join("+"), QVariant::fromValue(source));

	if (choice == "losehp")
		room->loseHp(HpLostStruct(target, 1, "caozhao", source));
	else {
		if (source->isDead()) return;
		target = room->askForPlayerChosen(source, room->getOtherPlayers(source), "caozhao_give", "@caozhao-give", true);
		if(target)
			room->giveCard(source, target, subcards, "caozhao", true);
		else
			target = source;
		card = Sanguosha->getCard(first);
		Card *view = Sanguosha->cloneCard(name, card->getSuit(), card->getNumber());
		view->setSkillName("caozhao");
		WrappedCard *wr = Sanguosha->getWrappedCard(first);
		wr->takeOver(view);
		room->notifyUpdateCard(target, first, wr);
	}
}

class Caozhao : public OneCardViewAsSkill
{
public:
	Caozhao() : OneCardViewAsSkill("caozhao")
	{
		filter_pattern = ".|.|.|hand";
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("CaozhaoCard");
	}

	QDialog *getDialog() const
	{
		return CaozhaoDialog::getInstance("caozhao");
	}

	const Card *viewAs(const Card *originalcard) const
	{
		const Card *card = Self->tag.value("caozhao").value<const Card *>();
		if (!card) return nullptr;
		CaozhaoCard *c = new CaozhaoCard;
		c->setUserString(card->objectName());
		c->addSubcard(originalcard);
		return c;
	}
};

class OLXibing : public TriggerSkill
{
public:
	OLXibing() : TriggerSkill("olxibing")
	{
		events << DamageInflicted;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (!damage.from || damage.from == player) return false;
		QStringList choices;
		if (player->canDiscard(damage.from, "he"))
			choices << "discard=" + damage.from->objectName();
		if (player->canDiscard(player, "he"))
			choices << "discard_self";
		if (choices.isEmpty()) return false;
		if (!player->askForSkillInvoke(this, data)) return false;
		room->broadcastSkillInvoke(this);
		QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);

		ServerPlayer *thrower = player, *victim = damage.from;
		if (choice == "discard_self") victim = player;
		if (thrower->isDead() || victim->isDead()) return false;

		QList<int> cards;
		for (int i = 0; i < 2; ++i) {
			if (victim->getCardCount()<=i) break;
			int id = room->askForCardChosen(thrower, victim, "he", objectName(), false, Card::MethodDiscard, cards);
			if(id<0) break;
			cards << id;
		}
		DummyCard dummy(cards);
		room->throwCard(&dummy, victim, thrower);
		if (player->isDead() || damage.from->isDead()) return false;
		int hand = player->getHandcardNum(), hand2 = damage.from->getHandcardNum();
		if (hand == hand2) return false;

		room->addPlayerMark(player, "olxibing_to-Clear");
		ServerPlayer *drawer = player;
		if (hand > hand2)
			drawer = damage.from;

		drawer->drawCards(2, objectName());
		if (drawer->isAlive())
			room->addPlayerMark(drawer, "olxibing_from-Clear");

		return false;
	}
};

class OLXibingPro : public ProhibitSkill
{
public:
	OLXibingPro() : ProhibitSkill("#olxibing-pro")
	{
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		return from->getMark("olxibing_from-Clear") > 0 && to->getMark("olxibing_to-Clear") > 0 && !card->isKindOf("SkillCard");
	}
};

LiPackage::LiPackage()
	: Package("li")
{
	new General(this, "yinni_hide", "jin", 1, true, true, true);

	General *jin_simayi = new General(this, "jin_simayi", "jin", 3);
	jin_simayi->addSkill(new JinBuchen);
	jin_simayi->addSkill(new JinYingshi);
	jin_simayi->addSkill(new JinXiongzhi);
	jin_simayi->addSkill(new JinQuanbian("jinquanbian"));
	jin_simayi->addSkill(new JinQuanbianLimit("jinquanbian"));
	related_skills.insertMulti("jinquanbian", "#jinquanbian-limit");

	General *second_jin_simayi = new General(this, "second_jin_simayi", "jin", 3);
	second_jin_simayi->addSkill("jinbuchen");
	second_jin_simayi->addSkill("jinyingshi");
	second_jin_simayi->addSkill("jinxiongzhi");
	second_jin_simayi->addSkill(new JinQuanbian("secondjinquanbian"));
	second_jin_simayi->addSkill(new JinQuanbianLimit("secondjinquanbian"));
	related_skills.insertMulti("secondjinquanbian", "#secondjinquanbian-limit");

	General *jin_zhangchunhua = new General(this, "jin_zhangchunhua", "jin", 3, false);
	jin_zhangchunhua->addSkill(new JinHuishi);
	jin_zhangchunhua->addSkill(new JinQingleng);
	jin_zhangchunhua->addSkill(new JinXuanmu);

	General *ol_lisu = new General(this, "ol_lisu", "qun", 3);
	ol_lisu->addSkill(new Qiaoyan);
	ol_lisu->addSkill(new Xianzhu);

	General *jin_simazhou = new General(this, "jin_simazhou", "jin", 4);
	jin_simazhou->addSkill(new JinCaiwang("jincaiwang"));
	jin_simazhou->addSkill(new JinNaxiang);
	jin_simazhou->addSkill(new JinNaxiangClear);
	related_skills.insertMulti("jinnaxiang", "#jinnaxiang-clear");

	General *second_jin_simazhou = new General(this, "second_jin_simazhou", "jin", 4);
	second_jin_simazhou->addSkill(new JinCaiwang("secondjincaiwang"));
	second_jin_simazhou->addSkill("jinnaxiang");

	General *cheliji = new General(this, "cheliji", "qun", 4);
	cheliji->addSkill(new Chexuan);
	cheliji->addSkill(new Qiangshou);
	cheliji->addRelateSkill("_sichengliangyu");
	cheliji->addRelateSkill("_tiejixuanyu");
	cheliji->addRelateSkill("_feilunzhanyu");

	General *ol_huaxin = new General(this, "ol_huaxin", "wei", 3);
	ol_huaxin->addSkill(new Caozhao);
	ol_huaxin->addSkill(new OLXibing);
	ol_huaxin->addSkill(new OLXibingPro);
	related_skills.insertMulti("olxibing", "#olxibing-pro");

	addMetaObject<JinYingshiCard>();
	addMetaObject<JinXiongzhiCard>();
	addMetaObject<JinQinglengCard>();
	addMetaObject<ChexuanCard>();
	addMetaObject<CaozhaoCard>();
}
ADD_PACKAGE(Li)


class JinXijue : public TriggerSkill
{
public:
	JinXijue() : TriggerSkill("jinxijue")
	{
		events << GameStart << EventPhaseChanging << DrawNCards << EventPhaseStart;
	}

	int getPriority(TriggerEvent event) const
	{
		if (event == DrawNCards) return 1;
		return TriggerSkill::getPriority(event);
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Finish) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (player->isDead()) return false;
				if (p->isDead() || !p->hasSkill(objectName()) || p->getMark("&jxjjue") <= 0 || !p->canDiscard(p, "h")) continue;
				if (room->askForCard(p, ".Basic", "@xiaoguo", data, "xiaoguo")) {
					p->loseMark("&jxjjue");
					room->broadcastSkillInvoke("xiaoguo", 1);
					if (!room->askForCard(player, ".Equip", "@xiaoguo-discard", QVariant())) {
						room->broadcastSkillInvoke("xiaoguo", 2);
						room->damage(DamageStruct("xiaoguo", p, player));
					} else {
						room->broadcastSkillInvoke("xiaoguo", 3);
						if (p->isAlive())
							p->drawCards(1, "xiaoguo");
					}
				}
			}
		}
		if (!player->hasSkill(objectName())) return false;
		if (event == GameStart) {
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			player->gainMark("&jxjjue", 4);
		} else if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			int damage = player->getMark("damage_point_round");
			if (damage <= 0) return false;
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			player->gainMark("&jxjjue", damage);
		} else {
			if (player->getMark("&jxjjue") <= 0) return false;
			DrawStruct draw = data.value<DrawStruct>();
			if (draw.reason!="draw_phase") return false;
			int num = 0;
			foreach(ServerPlayer *p, room->getOtherPlayers(player))
				if (p->getHandcardNum() >= player->getHandcardNum())
					num++;
			num = qMin(num, draw.num);

			if (num > 0) {
				player->setMark("TuxiTarget",0);
				room->setPlayerMark(player, "tuxi", num);
				if (room->askForUseCard(player, "@@tuxi", "@tuxi-card:::" + QString::number(num))) {
					player->loseMark("&jxjjue");
					draw.num -= player->getMark("TuxiTarget");
					data = QVariant::fromValue(draw);
				}
			}
		}
		return false;
	}
};

class JinBaoQie : public TriggerSkill
{
public:
	JinBaoQie() : TriggerSkill("jinbaoqie")
	{
		events << Appear;
		frequency = Compulsory;
		hide_skill = true;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		room->sendCompulsoryTriggerLog(player, objectName(), true, true);
		QList<int> list = room->getDrawPile() + room->getDiscardPile();
		qShuffle(list);
		foreach (int id, list) {
			const Card *card = Sanguosha->getCard(id);
			if (card->isKindOf("Treasure")){
				room->obtainCard(player, id);
				if(player->handCards().contains(id)&&card->isAvailable(player)){
					if(player->askForSkillInvoke(this,"jinbaoqie_use:"+card->objectName(),false))
						room->useCard(CardUseStruct(card, player));
				}
				break;
			}
		}
		return false;
	}
};

JinYishiCard::JinYishiCard()
{
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

void JinYishiCard::onUse(Room *, CardUseStruct &) const
{
}

class JinYishiVS : public OneCardViewAsSkill
{
public:
	JinYishiVS() : OneCardViewAsSkill("jinyishi")
	{
		response_pattern = "@@jinyishi";
		expand_pile = "#jinyishi";
	}

	bool viewFilter(const Card *to_select) const
	{
		return Self->getPile("#jinyishi").contains(to_select->getEffectiveId());
	}

	const Card *viewAs(const Card *originalCard) const
	{
		JinYishiCard *c = new JinYishiCard;
		c->addSubcard(originalCard);
		return c;
	}
};

class JinYishi : public TriggerSkill
{
public:
	JinYishi() : TriggerSkill("jinyishi")
	{
		events << CardsMoveOneTime;
		view_as_skill = new JinYishiVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (!player->hasTurn() || player->getMark("jinyishi-Clear") > 0) return false;
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (!move.from || move.from == player || move.from->getPhase() != Player::Play || !move.from_places.contains(Player::PlaceHand))
			return false;

		if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
			int i = 0;
			QVariantList dis;
			foreach (int card_id, move.card_ids) {
				if (move.from_places[i] == Player::PlaceHand && room->getCardPlace(card_id) == Player::DiscardPile)
					dis << card_id;
				i++;
			}
			if (dis.isEmpty()) return false;
			QList<int> discard = ListV2I(dis);
			player->tag["jinyishi_from"] = QVariant::fromValue((ServerPlayer *)move.from);
			room->notifyMoveToPile(player, discard, objectName(), Player::DiscardPile, true);
			const Card *card = room->askForUseCard(player, "@@jinyishi", "@jinyishi:" + move.from->objectName(), -1, Card::MethodNone);
			room->notifyMoveToPile(player, discard, objectName(), Player::DiscardPile, false);
			player->tag.remove("jinyishi_from");
			if (!card) return false;

			LogMessage log;
			log.type = "#InvokeSkill";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			room->broadcastSkillInvoke(objectName());
			room->notifySkillInvoked(player, objectName());

			room->addPlayerMark(player, "jinyishi-Clear");
			room->obtainCard((ServerPlayer *)move.from, card);
			if (player->isAlive()) {
				discard.removeOne(card->getSubcards().first());
				if (discard.isEmpty()) return false;
				DummyCard get(discard);
				room->obtainCard(player, &get);
			}
		}
		return false;
	}
};

JinShiduCard::JinShiduCard()
{
	will_throw = false;
	handling_method = Card::MethodPindian;
}

bool JinShiduCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && Self->canPindian(to_select);
}

void JinShiduCard::onEffect(CardEffectStruct &effect) const
{
	if (!effect.from->canPindian(effect.to, false)) return;
	bool pindian = effect.from->pindian(effect.to, "jinshidu");
	if (!pindian) return;

	Room *room = effect.from->getRoom();
	DummyCard *handcards = effect.to->wholeHandCards();
	room->obtainCard(effect.from, handcards, false);

	if (effect.from->isDead() || effect.to->isDead()) return;
	int give = floor(effect.from->getHandcardNum() / 2);
	if (give <= 0) return;
	const Card *card = room->askForExchange(effect.from, "jinshidu", give, give, false, QString("jinshidu-give:%1::%2").arg(effect.to->objectName())
											.arg(QString::number(give)));
	room->giveCard(effect.from, effect.to, card, "jinshidu");
}

class JinShidu : public ZeroCardViewAsSkill
{
public:
	JinShidu() : ZeroCardViewAsSkill("jinshidu")
	{
	}

	const Card *viewAs() const
	{
		return new JinShiduCard;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->canPindian() && !player->hasUsed("JinShiduCard");
	}
};

class JinTaoyin : public TriggerSkill
{
public:
	JinTaoyin() : TriggerSkill("jintaoyin")
	{
		events << Appear;
		hide_skill = true;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		ServerPlayer *cp = room->getCurrent();
		if (cp == player) return false;
		if (!player->askForSkillInvoke(this, cp)) return false;
		room->broadcastSkillInvoke(objectName());
		room->addPlayerMark(cp, "&jintaoyin-Clear");
		room->addMaxCards(cp, -2);
		return false;
	}
};

class JinYimie : public TriggerSkill
{
public:
	JinYimie() : TriggerSkill("jinyimie")
	{
		events << DamageCaused;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (!player->hasTurn()||player->getMark("jinyimie-Clear") > 0) return false;
		DamageStruct damage = data.value<DamageStruct>();
		if (damage.to == player || damage.to->isDead()) return false;
		int n = damage.to->getHp() - damage.damage;
		if (n < 0) n = 0;
		if (!player->askForSkillInvoke(this, QString("jinyimie:%1::%2").arg(damage.to->objectName()).arg(n))) return false;
		room->broadcastSkillInvoke(objectName());
		room->addPlayerMark(player, "jinyimie-Clear");
		damage.tips << "jinyimie_damage_" + QString::number(n)
					<< "jinyimie_from_" + player->objectName()
					<<"jinyimie_to_" + damage.to->objectName();
		room->loseHp(HpLostStruct(player, 1, objectName(), player));
		damage.damage += n;
		data = QVariant::fromValue(damage);
		return false;
	}
};

class JinYimieRecover : public TriggerSkill
{
public:
	JinYimieRecover() : TriggerSkill("#jinyimie-recover")
	{
		events << DamageComplete;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (!damage.tips.contains("jinyimie_to_" + player->objectName())) return false;
		int n = -1;
		ServerPlayer *from = nullptr;
		foreach (QString tip, damage.tips) {
			if (tip.startsWith("jinyimie_damage_")) {
				QStringList tips = tip.split("_");
				if (tips.length() < 3) continue;
				n = tips.last().toInt();
			} else if (tip.startsWith("jinyimie_from_")) {
				QStringList tips = tip.split("_");
				if (tips.length() < 3) continue;
				from = room->findPlayerByObjectName(tips.last(), true);
			}

			if (n >= 0 && from) break;
		}
		if (n < 0 || !from) return false;

		int recover = qMin(player->getMaxHp() - player->getHp(), n);
		if (recover <= 0) return false;
		if (from->isDead()) from = nullptr;
		room->recover(player, RecoverStruct("jinyimie", from, recover));
		return false;
	}
};

class JinTairan : public TriggerSkill
{
public:
	JinTairan() : TriggerSkill("jintairan")
	{
		events << EventPhaseStart << EventPhaseChanging;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Play) return false;
			int mark = player->getMark("&jintairanrecover");
			bool dis = false;
			foreach (const Card*h, player->getHandcards()) {
				if (!h->hasTip("jintairan")) continue;
				if (player->canDiscard(player, h->getId())) {
					dis = true;
					break;
				}
			}
			if (mark > 0 || dis)
				room->sendCompulsoryTriggerLog(player, this);

			room->setPlayerMark(player, "&jintairanrecover", 0);
			room->setPlayerMark(player, "&jintairan+draw", 0);

			if (mark > 0)
				room->loseHp(HpLostStruct(player, mark, "jintairan", player));
			QList<int>ids;
			foreach (const Card*h, player->getHandcards()) {
				if (!h->hasTip("jintairan")) continue;
				room->setCardTip(h->getId(), "-jintairan");
				if (player->canDiscard(player, h->getId()))
					ids << h->getId();
			}
			room->throwCard(ids, objectName(), player);
		} else {
			if (!TriggerSkill::triggerable(player)) return false;
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive) return false;
			int lost = player->getMaxHp() - player->getHp();
			if (lost > 0 || player->getHandcardNum() < player->getMaxCards())
				room->sendCompulsoryTriggerLog(player, this);

			if (lost > 0){
				room->addPlayerMark(player, "&jintairanrecover", lost);
				room->recover(player, RecoverStruct(objectName(), player, lost));
			}
			lost = player->getMaxCards() - player->getHandcardNum();
			if (lost>0) {
				QList<int> draws = room->drawCardsList(player, lost);
				QList<int> hands = player->handCards();
				int i = 0;
				foreach (int id, draws) {
					if (!hands.contains(id)) continue;
					room->setCardTip(id, objectName());
					i++;
				}
				room->addPlayerMark(player, "&jintairan+draw", i);
			}
		}
		return false;
	}
};

JinRuilveGiveCard::JinRuilveGiveCard()
{
	will_throw = false;
	handling_method = Card::MethodNone;
	mute = true;
}

bool JinRuilveGiveCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && Self != to_select && to_select->hasLordSkill("jinruilve") && to_select->getMark("jinruilve-PlayClear") <= 0;
}

void JinRuilveGiveCard::onUse(Room *room, CardUseStruct &use) const
{
	QVariant data = QVariant::fromValue(use);
	room->getThread()->trigger(PreCardUsed, room, use.from, data);
	room->getThread()->trigger(CardUsed, room, use.from, data);
	use = data.value<CardUseStruct>();
	foreach (ServerPlayer*p, use.to) {
		if (p->isWeidi()){
			room->broadcastSkillInvoke("weidi",-1,p);
			room->notifySkillInvoked(p, "weidi");
		}else
			room->broadcastSkillInvoke("jinruilve",-1,p);
		use.from->skillInvoked("jinruilve",0,p);
		room->giveCard(use.from, p, this, "jinruilve", true);
	}
	room->getThread()->trigger(CardFinished, room, use.from, data);
	use = data.value<CardUseStruct>();
}

class JinRuilveGive : public OneCardViewAsSkill
{
public:
	JinRuilveGive() : OneCardViewAsSkill("jinruilve_give")
	{
		attached_lord_skill = true;
	}

	bool viewFilter(const Card *to_select) const
	{
		return to_select->isKindOf("Slash") || (to_select->isDamageCard() && to_select->isKindOf("TrickCard"));
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getKingdom() == "jin"&&hasTarget(player);
	}

	bool hasTarget(const Player *player) const
	{
		foreach (const Player *p, player->getAliveSiblings()) {
			if (p->hasLordSkill("jinruilve") && player->usedTimes("JinRuilveGiveCard") <= 0)
				return true;
		}
		return false;
	}

	const Card *viewAs(const Card *card) const
	{
		JinRuilveGiveCard *c = new JinRuilveGiveCard;
		c->addSubcard(card);
		return c;
	}
};

class JinRuilve : public TriggerSkill
{
public:
	JinRuilve() : TriggerSkill("jinruilve$")
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
				if (p->getPhase()==Player::Play&&!p->hasSkill("jinruilve_give",true)){
					room->attachSkillToPlayer(p, "jinruilve_give");
				}
			}
		}else if(player->getPhase()!=Player::Play)
			return false;
		if (triggerEvent == EventPhaseStart) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->hasLordSkill(this,true)){
					room->attachSkillToPlayer(player, "jinruilve_give");
					break;
				}
			}
		}else{
			if (player->hasSkill("jinruilve_give",true))
				room->detachSkillFromPlayer(player, "jinruilve_give", true);
		}
		return false;
	}
};

class JinHuirong : public TriggerSkill
{
public:
	JinHuirong() : TriggerSkill("jinhuirong")
	{
		events << Appear;
		frequency = Compulsory;
		hide_skill = true;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@jinhuirong-invoke", false, true);
		room->broadcastSkillInvoke(objectName());
		if (target->getHandcardNum() > target->getHp())
			room->askForDiscard(target, objectName(), target->getHandcardNum() - target->getHp(), target->getHandcardNum() - target->getHp());
		else if (target->getHandcardNum() < target->getHp()) {
			int num = qMin(5, target->getHp()) - target->getHandcardNum();
			target->drawCards(num, objectName());
		}
		return false;
	}
};

class JinCiwei : public TriggerSkill
{
public:
	JinCiwei() : TriggerSkill("jinciwei")
	{
		events << CardUsed;
        global = true;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->hasFlag("CurrentPlayer");
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("BasicCard") || use.card->isNDTrick()){
				player->addMark("jinciwei_use_time-Clear");
				if (player->getMark("jinciwei_use_time-Clear") != 2) return false;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if (!p->hasSkill(objectName()) || !p->canDiscard(p, "he")) continue;
					QString prompt = QString("@jinciwei-discard:%1::%2").arg(player->objectName()).arg(use.card->objectName());
					if (!room->askForCard(p, "..", prompt, data, objectName())) continue;
					room->broadcastSkillInvoke(objectName());
					use.nullified_list << "_ALL_TARGETS"; 
					data = QVariant::fromValue(use);
					if (player->isDead()) break;
				}
			}
		}
		return false;
	}
};

class JinCaiyuan : public TriggerSkill
{
public:
	JinCaiyuan() : TriggerSkill("jincaiyuan")
	{
		events << HpLost << DamageDone << EventPhaseStart;
		frequency = Compulsory;
        global = true;
	}
	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (event == HpLost || event == DamageDone)
			player->addMark("jincaiyuan_hpchanged-SelfClear");
		else if (event == EventPhaseStart) {
			if (player->getPhase()!=Player::Finish||player->getMark("jincaiyuan_hpchanged-SelfClear")>0) return false;
			if (!player->hasSkill(objectName())) return false;
			room->sendCompulsoryTriggerLog(player, this);
			player->drawCards(2, objectName());
		}
		return false;
	}
};

class JinZhuoshengVS : public ZeroCardViewAsSkill
{
public:
	JinZhuoshengVS() : ZeroCardViewAsSkill("jinzhuosheng")
	{
		response_pattern = "@@jinzhuosheng!";
	}

	const Card *viewAs() const
	{
		return new ExtraCollateralCard;
	}
};

class JinZhuosheng : public TriggerSkill
{
public:
	JinZhuosheng() : TriggerSkill("jinzhuosheng")
	{
		events << CardUsed;
		view_as_skill = new JinZhuoshengVS;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardUsed) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->hasFlag("jinzhuoshengBf")) return false;

			if (use.card->isKindOf("EquipCard")) {
				if (!player->askForSkillInvoke(this, data)) return false;
				room->broadcastSkillInvoke(objectName());
				player->drawCards(1, objectName());
			} else if (use.card->isKindOf("BasicCard")) {
				use.m_addHistory = false;
				data = QVariant::fromValue(use);
			} else if (use.card->isNDTrick()) {
				QList<ServerPlayer *> available_targets;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if (use.to.contains(p)) continue;
					if (player->canUse(use.card,p))
						available_targets << p;
				}
				QStringList choices;
				if (use.to.length() > 1) choices.prepend("remove");
				if (!available_targets.isEmpty()) choices.prepend("add");
				if (choices.isEmpty()) return false;
				choices << "cancel";

				QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);
				if (choice == "cancel") return false;
				if (choice == "add") {
					ServerPlayer *extra;
					if (use.card->isKindOf("Collateral")){
						QStringList tos;
						tos << use.card->toString();
						foreach(ServerPlayer *t, use.to)
							tos << t->objectName();
						tos << objectName();
						room->setPlayerProperty(player, "extra_collateral", tos.join("+"));
						room->askForUseCard(player, "@@jinzhuosheng!", "@qiaoshui-add:::collateral");
						extra = player->tag["ExtraCollateralTarget"].value<ServerPlayer *>();
						player->tag.remove("ExtraCollateralTarget");
						if (!extra) {
							QList<ServerPlayer *> victims;
							extra = available_targets.at(qrand() % available_targets.length());
							room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), extra->objectName());
							foreach (ServerPlayer *p, room->getOtherPlayers(extra)) {
								if (extra->canSlash(p)) victims << p;
							}
							extra->tag["attachTarget"] = QVariant::fromValue(victims.at(qrand() % victims.length()));
							LogMessage log;
							log.type = "#QiaoshuiAdd";
							log.from = player;
							log.to << extra;
							log.card_str = use.card->toString();
							log.arg = "jinzhuosheng";
							room->sendLog(log);
						}
					}else{
						extra = room->askForPlayerChosen(player, available_targets, objectName(), "@qiaoshui-add:::" + use.card->objectName());
						LogMessage log;
						log.type = "#QiaoshuiAdd";
						log.from = player;
						log.to << extra;
						log.card_str = use.card->toString();
						log.arg = "jinzhuosheng";
						room->sendLog(log);
						room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), extra->objectName());
					}
					use.to.append(extra);
					room->sortByActionOrder(use.to);
				} else {
					ServerPlayer *removed = room->askForPlayerChosen(player, use.to, "jinzhuosheng", "@qiaoshui-remove:::" + use.card->objectName());
					use.to.removeOne(removed);
					LogMessage log;
					log.type = "#QiaoshuiRemove";
					log.from = player;
					log.to << removed;
					log.card_str = use.card->toString();
					log.arg = "jinzhuosheng";
					room->sendLog(log);
				}
				room->broadcastSkillInvoke(objectName());
				room->notifySkillInvoked(player, objectName());
				data = QVariant::fromValue(use);
			}
		}
		return false;
	}
};

class JinZhuoshengTargetMod : public TargetModSkill
{
public:
	JinZhuoshengTargetMod() : TargetModSkill("#jinzhuosheng-target")
	{
		frequency = NotFrequent;
		pattern = "BasicCard";
	}

	int getDistanceLimit(const Player *from, const Card *card, const Player *) const
	{
		if (card->hasFlag("jinzhuoshengBf")&&from->hasSkill("jinzhuosheng"))
			return 999;
		return 0;
	}
};

class JinZhuoshengRecord : public TriggerSkill
{
public:
	JinZhuoshengRecord() : TriggerSkill("#jinzhuosheng-record")
	{
		events << CardsMoveOneTime << RoundEnd;
        global = true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == CardsMoveOneTime) {
			if (room->getTag("FirstRound").toBool()) return false;
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to != player || move.to_place != Player::PlaceHand) return false;
			if (move.reason.m_skillName == "jinzhuosheng") return false;
			QVariantList ids = room->getTag("jinzhuoshengIds").toList();
			foreach (int id, player->handCards()) {
				if (move.card_ids.contains(id)){
					if (player->hasSkill("jinzhuosheng", true))
						room->setCardTip(id, "jinzhuosheng_lun");
					room->setCardFlag(id,"jinzhuoshengBf");
					ids << id;
				}
			}
			room->setTag("jinzhuoshengIds",ids);
		}else{
			QVariantList ids = room->getTag("jinzhuoshengIds").toList();
			room->removeTag("jinzhuoshengIds");
			foreach (int id, ListV2I(ids))
				room->setCardFlag(id,"-jinzhuoshengBf");
		}
		return false;
	}
};

TousuiCard::TousuiCard()
{
	will_throw = false;
	handling_method = Card::MethodUse;
}

bool TousuiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *use_card = Sanguosha->cloneCard("slash");
	use_card->setSkillName("tousui");
	use_card->deleteLater();
	return use_card->targetFilter(targets, to_select, Self);
}

const Card *TousuiCard::validate(CardUseStruct &use) const
{
	Room *room = use.from->getRoom();
	room->moveCardsToEndOfDrawpile(use.from,subcards,getSkillName(),false);

	Card *use_card = Sanguosha->cloneCard("slash");
	use_card->setSkillName("tousui");
	room->setCardMark(use_card,"tousuiNum",subcardsLength());
	use_card->deleteLater();
	return use_card;
}

class TousuiVs : public ViewAsSkill
{
public:
	TousuiVs() : ViewAsSkill("tousui")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return Slash::IsAvailable(player);
	}

	bool viewFilter(const QList<const Card *> &, const Card *) const
	{
		return true;
	}

	bool isEnabledAtResponse(const Player *, const QString &pattern) const
	{
		return Sanguosha->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_RESPONSE
		&& pattern.contains("slash");
	}

	const Card *viewAs(const QList<const Card *> &cards) const
	{
		if(cards.isEmpty()) return nullptr;
		Card*sc = new TousuiCard;
		sc->addSubcards(cards);
		return sc;
	}
};

class Tousui : public TriggerSkill
{
public:
	Tousui() : TriggerSkill("tousui")
	{
		events << CardEffect;
		view_as_skill = new TousuiVs;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const
	{
		if (event == CardEffect) {
			CardEffectStruct effect = data.value<CardEffectStruct>();
			if(effect.card->getMark("tousuiNum")>0){
				effect.offset_num = effect.card->getMark("tousuiNum");
				data.setValue(effect);
			}
		}
		return false;
	}
};

class ChumingVs : public ZeroCardViewAsSkill
{
public:
	ChumingVs() : ZeroCardViewAsSkill("chuming")
	{
		response_pattern = "@@chuming!";
	}

	const Card *viewAs() const
	{
		Card*dc = Sanguosha->cloneCard(Self->property("chumingUse").toString());
		QString subcards = Self->property("chumingSubcard").toString();
		if(subcards!="."){
			foreach (QString t, subcards.split("+")) {
				dc->addSubcard(t.toInt());
			}
		}
		dc->setSkillName("_"+objectName());
		return dc;
	}
};

class Chuming : public TriggerSkill
{
public:
	Chuming() : TriggerSkill("chuming")
	{
		events << DamageCaused << DamageInflicted << EventPhaseChanging;
		view_as_skill = new ChumingVs;
		waked_skills = "#chuming_pro";
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseChanging) {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if(change.to==Player::NotActive){
				foreach (ServerPlayer *p, room->getAllPlayers()) {
					foreach (QString qv, p->tag["chumingUse"].toStringList()) {
						QStringList tp = qv.split("&");
						ServerPlayer *from = room->findPlayerByObjectName(tp.first(),true),*to = room->findPlayerByObjectName(tp.at(1),true);
						if(!from||from->isDead()||!to||to->isDead()) continue;
						if(from==p){
							from = to;
							to = p;
						}
						const Card*pc = Card::Parse(tp.last());
						if(room->getCardOwner(pc->getEffectiveId())) continue;
						Card*dc = Sanguosha->cloneCard("collateral");
						dc->addSubcards(pc->getSubcards());
						dc->setSkillName(objectName());
						dc->deleteLater();
						QStringList choices;
						room->addPlayerMark(to,"chumingUse-Clear");
						if(from->canUse(dc,to))
							choices << "collateral";
						dc = Sanguosha->cloneCard("dismantlement");
						dc->addSubcards(pc->getSubcards());
						dc->setSkillName(objectName());
						dc->deleteLater();
						if(from->canUse(dc,to))
							choices << "dismantlement";
						if(!choices.isEmpty()){
							room->setPlayerProperty(from,"chumingSubcard",pc->subcardString());
							QString choice = room->askForChoice(from,objectName(),choices.join("+"));
							room->setPlayerProperty(from,"chumingUse",choice);
							room->askForUseCard(from,"@@chuming!","chuming0:"+choice);
						}
						room->removePlayerMark(to,"chumingUse-Clear");
					}
					p->tag.remove("chumingUse");
				}
			}
		}else{
			DamageStruct damage = data.value<DamageStruct>();
			if(!damage.from||damage.from==damage.to) return false;
			if(damage.card&&damage.card->getEffectiveId()>=0){
				if(player->hasSkill(objectName(),true)){
					QStringList uses = player->tag["chumingUse"].toStringList();
					uses << damage.from->objectName()+"&"+damage.to->objectName()+"&"+damage.card->toString();
					player->tag["chumingUse"] = uses;
				}
			}else{
				if(player->hasSkill(objectName())){
					room->sendCompulsoryTriggerLog(player,this);
					player->damageRevises(data,1);
				}
			}
		}
		return false;
	}
};

class ChumingPro : public ProhibitSkill
{
public:
	ChumingPro() : ProhibitSkill("#chuming_pro")
	{
	}

	bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		return card->getSkillName()=="chuming"
		&&to->getMark("chumingUse-Clear")<1;
	}
};

BeiPackage::BeiPackage()
	: Package("bei")
{
	General *jin_zhanghuyuechen = new General(this, "jin_zhanghuyuechen", "jin", 4);
	jin_zhanghuyuechen->addSkill(new JinXijue);

	General *jin_xiahouhui = new General(this, "jin_xiahouhui", "jin", 3, false);
	jin_xiahouhui->addSkill(new JinBaoQie);
	jin_xiahouhui->addSkill(new JinYishi);
	jin_xiahouhui->addSkill(new JinShidu);

	General *jin_simashi = new General(this, "jin_simashi$", "jin", 4, true, false, false, 3);
	jin_simashi->addSkill(new JinTaoyin);
	jin_simashi->addSkill(new JinYimie);
	jin_simashi->addSkill(new JinYimieRecover);
	jin_simashi->addSkill(new JinTairan);
	jin_simashi->addSkill(new JinRuilve);
	related_skills.insertMulti("jinyimie", "#jinyimie-recover");

	General *jin_yanghuiyu = new General(this, "jin_yanghuiyu", "jin", 3, false);
	jin_yanghuiyu->addSkill(new JinHuirong);
	jin_yanghuiyu->addSkill(new JinCiwei);
	jin_yanghuiyu->addSkill(new JinCaiyuan);

	General *jin_shibao = new General(this, "jin_shibao", "jin", 4);
	jin_shibao->addSkill(new JinZhuosheng);
	jin_shibao->addSkill(new JinZhuoshengTargetMod);
	jin_shibao->addSkill(new JinZhuoshengRecord);
	related_skills.insertMulti("jinzhuosheng", "#jinzhuosheng-target");
	related_skills.insertMulti("jinzhuosheng", "#jinzhuosheng-record");

	General *ol_ercheng = new General(this, "ol_ercheng", "wei", 6);
	ol_ercheng->addSkill(new Tousui);
	ol_ercheng->addSkill(new Chuming);
	ol_ercheng->addSkill(new ChumingPro);
	addMetaObject<TousuiCard>();

	addMetaObject<JinYishiCard>();
	addMetaObject<JinShiduCard>();
	addMetaObject<JinRuilveGiveCard>();

	skills << new JinRuilveGive;
}
ADD_PACKAGE(Bei)


class JinTuishi : public TriggerSkill
{
public:
	JinTuishi() : TriggerSkill("jintuishi")
	{
		events << Appear << EventPhaseChanging;
		hide_skill = true;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if(event==Appear){
			if(room->getCurrent() == player) return false;
			if(player->hasSkill(objectName(),true))
				room->setPlayerMark(player, "&jintuishi-Clear", 1);
		}else{
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (player->isDead()) break;
				if (p->getMark("&jintuishi-Clear")<1||!p->hasSkill(objectName())) continue;
				QList<ServerPlayer *> targets;
				foreach (ServerPlayer *pl, room->getOtherPlayers(player)) {
					if (player->inMyAttackRange(pl) && player->canSlash(pl))
						targets << pl;
				}
				p->tag["jintuishi_from"] = QVariant::fromValue(player);
				ServerPlayer *target = room->askForPlayerChosen(p, targets, "jintuishi", "@jintuishi-invoke:" + player->objectName(), true, true);
				if (!target) continue;
				room->broadcastSkillInvoke("jintuishi");
	
				if (room->askForUseSlashTo(player, target, "@jintuishi_slash:" + target->objectName(), true, false, true, p)) continue;
				room->damage(DamageStruct("jintuishi", p, player));
			}
		}
		return false;
	}
};

JinChoufaCard::JinChoufaCard()
{
}

bool JinChoufaCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void JinChoufaCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.to->isKongcheng()) return;
	const Card *card = effect.to->getRandomHandCard();
	Room *room = effect.from->getRoom();
	room->showCard(effect.to, card->getEffectiveId());

	room->addPlayerMark(effect.to, "jinchoufa_target");
	foreach (const Card *c, effect.to->getCards("h")) {
		if (c->getTypeId() == card->getTypeId()) continue;
		Slash *slash = new Slash(c->getSuit(), c->getNumber());
		slash->setSkillName("jinchoufa");
		WrappedCard *card = Sanguosha->getWrappedCard(c->getId());
		card->takeOver(slash);
		room->notifyUpdateCard(effect.to, c->getEffectiveId(), card);
	}
}

class JinChoufaVS : public ZeroCardViewAsSkill
{
public:
	JinChoufaVS() : ZeroCardViewAsSkill("jinchoufa")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("JinChoufaCard");
	}

	const Card *viewAs() const
	{
		return new JinChoufaCard;
	}
};

class JinChoufa : public TriggerSkill
{
public:
	JinChoufa() : TriggerSkill("jinchoufa")
	{
		events << EventPhaseChanging;
		view_as_skill = new JinChoufaVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->getMark("jinchoufa_target")>0;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
		room->setPlayerMark(player, "jinchoufa_target", 0);
		foreach (const Card *c, player->getCards("h")) {
			if(c->getSkillName()==objectName())
				room->filterCards(player, QList<const Card*>() << c, true);
		}
		return false;
	}
};

class JinZhaoran : public PhaseChangeSkill
{
public:
	JinZhaoran() : PhaseChangeSkill("jinzhaoran")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Play) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(objectName());
		room->addPlayerMark(player, "HandcardVisible_ALL-PlayClear");
		room->addPlayerMark(player, "jinzhaoran-PlayClear");
		if (!player->isKongcheng())
			room->showAllCards(player);
		return false;
	}
};

class JinZhaoranEffect : public TriggerSkill
{
public:
	JinZhaoranEffect() : TriggerSkill("#jinzhaoran-effect")
	{
		events << CardsMoveOneTime;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.from != player || move.from->getPhase() != Player::Play || !move.from_places.contains(Player::PlaceHand)) return false;
		if (player->getMark("jinzhaoran-PlayClear") <= 0) return false;

		for (int i = 0; i < move.card_ids.length(); i++) {
			if (player->isDead()) return false;
			const Card *card = Sanguosha->getCard(move.card_ids.at(i));
			if (move.from_places.at(i) != Player::PlaceHand) continue;
			if (player->getMark("jinzhaoran_suit" + card->getSuitString() + "-PlayClear") > 0) continue;
			if (!move.last_hand_suits.contains(card->getSuitString())) continue;

			room->sendCompulsoryTriggerLog(player, "jinzhaoran", true, true);
			room->addPlayerMark(player, "jinzhaoran_suit" + card->getSuitString() + "-PlayClear");

			QList<ServerPlayer *> targets;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (player->canDiscard(p, "he"))
					targets << p;
			}
			ServerPlayer *target = room->askForPlayerChosen(player, targets, "jinzhaoran", "@jinzhaoran-discard", true);
			if (target){
				room->doAnimate(1, player->objectName(), target->objectName());
				int id = room->askForCardChosen(player, target, "he", "jinzhaoran", false, Card::MethodDiscard);
				room->throwCard(Sanguosha->getCard(id), "jinzhaoran", target, player);
			}else
				player->drawCards(1, "jinzhaoran");
		}
		return false;
	}
};

class JinShiren : public TriggerSkill
{
public:
	JinShiren() : TriggerSkill("jinshiren")
	{
		events << Appear;
		hide_skill = true;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		ServerPlayer *cp = room->getCurrent();
		if (!cp || cp == player) return false;
		if (cp->isKongcheng()) return false;
		if (!player->askForSkillInvoke(this, cp)) return false;
		JinYanxiCard *yanxi_card = new JinYanxiCard;
		yanxi_card->setSkillName("jinyanxi");
		yanxi_card->deleteLater();
		room->broadcastSkillInvoke(objectName());
		room->useCard(CardUseStruct(yanxi_card, player, cp), true);
		return false;
	}
};

JinYanxiCard::JinYanxiCard()
{
}

bool JinYanxiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void JinYanxiCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.to->isKongcheng()) return;
	Room *room = effect.from->getRoom();
	int hand_id = effect.to->getRandomHandCardId();
	QList<int> list = room->getNCards(2);
	QList<int> new_list;
	list << hand_id;
	//qShuffle(list);
	for (int i = 0; i < 3; i++) {
		int id = list.at(qrand() % list.length());
		new_list << id;
		list.removeOne(id);
		if (list.isEmpty()) break;
	}
	room->returnToTopDrawPile(list);
	if (new_list.isEmpty()) return;

	room->fillAG(new_list, effect.from);
	int id = room->askForAG(effect.from, new_list, false, "jinyanxi");
	room->clearAG(effect.from);

	CardMoveReason reason1(CardMoveReason::S_REASON_UNKNOWN, effect.from->objectName(), "jinyanxi", "");
	CardMoveReason reason2(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName(), "jinyanxi", "");
	if (id == hand_id) {
		QList<CardsMoveStruct> exchangeMove;
		new_list.removeOne(hand_id);
		CardsMoveStruct move1(QList<int>() << hand_id, effect.to,  effect.from, Player::PlaceHand, Player::PlaceHand, reason2);
		CardsMoveStruct move2(new_list, effect.from, Player::PlaceHand, reason1);
		exchangeMove.append(move1);
		exchangeMove.append(move2);
		room->moveCardsAtomic(exchangeMove, false);
	} else {
		DummyCard dummy;
		dummy.addSubcard(id);
		room->obtainCard(effect.from, &dummy, reason1, false);
	}
}

class JinYanxiVS : public ZeroCardViewAsSkill
{
public:
	JinYanxiVS() : ZeroCardViewAsSkill("jinyanxi")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("JinYanxiCard");
	}

	const Card *viewAs() const
	{
		return new JinYanxiCard;
	}
};

class JinYanxi : public TriggerSkill
{
public:
	JinYanxi() : TriggerSkill("jinyanxi")
	{
		events << CardsMoveOneTime;
		view_as_skill = new JinYanxiVS;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if (move.reason.m_skillName != objectName()) return false;
		if (!move.to || move.to != player || move.to_place != Player::PlaceHand || move.to->getPhase() == Player::NotActive) return false;
		room->ignoreCards(player, move.card_ids);
		return false;
	}
};

JinSanchenCard::JinSanchenCard()
{
}

bool JinSanchenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
	return targets.isEmpty() && to_select->getMark("jinsanchen_target-Clear") <= 0;
}

void JinSanchenCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.from->getRoom();
	room->addPlayerMark(effect.from, "&jinsanchen");
	room->addPlayerMark(effect.to, "jinsanchen_target-Clear");
	effect.to->drawCards(3, "jinsanchen");
	if (effect.to->isDead() || !effect.to->canDiscard(effect.to, "he")) return;
	const Card *card = room->askForDiscard(effect.to, "jinsanchen", 3, 3, false, true, "jinsanchen-discard");
	if (!card) return;
	QList<int> types;
	bool flag = true;
	foreach (int id, card->getSubcards()) {
		int type_id = Sanguosha->getCard(id)->getTypeId();
		if (!types.contains(type_id))
			types << type_id;
		else {
			flag = false;
			break;
		}
	}
	if (!flag) return;
	effect.to->drawCards(1, "jinsanchen");
	room->addPlayerMark(effect.from, "jinsanchen_times-PlayClear");
}

class JinSanchen : public ZeroCardViewAsSkill
{
public:
	JinSanchen() : ZeroCardViewAsSkill("jinsanchen")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->usedTimes("JinSanchenCard") < 1 + player->getMark("jinsanchen_times-PlayClear");
	}

	const Card *viewAs() const
	{
		return new JinSanchenCard;
	}
};

class JinZhaotao : public PhaseChangeSkill
{
public:
	JinZhaotao() : PhaseChangeSkill("jinzhaotao")
	{
		frequency = Wake;
		waked_skills = "jinpozhu";
	}

	bool triggerable(const ServerPlayer *player) const
	{
		return player && player->isAlive()&&player->getPhase() == Player::Start
		&& player->getMark(objectName())<1 && player->hasSkill(objectName());
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getMark("&jinsanchen") >= 3) {
			LogMessage log;
			log.type = "#JinZhaotaoWake";
			log.from = player;
			log.arg = QString::number(player->getMark("&jinsanchen"));
			log.arg2 = objectName();
			room->sendLog(log);
		}else if(!player->canWake("jinzhaotao"))
			return false;
		room->broadcastSkillInvoke(objectName());
		room->doSuperLightbox(player, "jinzhaotao");
		room->setPlayerMark(player, "jinzhaotao", 1);

		if (room->changeMaxHpForAwakenSkill(player, -1, objectName()))
			room->handleAcquireDetachSkills(player, "jinpozhu");
		return false;
	}
};

class JinPozhuVS : public OneCardViewAsSkill
{
public:
	JinPozhuVS() : OneCardViewAsSkill("jinpozhu")
	{
		filter_pattern = ".|.|.|hand";
		response_or_use = true;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->getMark("jinpozhu_wuxiao-Clear") <= 0;
	}

	const Card *viewAs(const Card *card) const
	{
		Chuqibuyi *c = new Chuqibuyi(card->getSuit(), card->getNumber());
		c->addSubcard(card);
		c->setSkillName(objectName());
		return c;
	}
};

class JinPozhu : public TriggerSkill
{
public:
	JinPozhu() : TriggerSkill("jinpozhu")
	{
		events << DamageDone << CardFinished;
		view_as_skill = new JinPozhuVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
	{
		if (event == DamageDone) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->isKindOf("Chuqibuyi") || !damage.card->getSkillNames().contains("jinpozhu") ||
					!damage.from || damage.from->isDead() || damage.from->getPhase() != Player::Play) return false;
			room->setCardFlag(damage.card, "jinpozhu_damage");
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Chuqibuyi") || !use.from || use.from->isDead() || !use.card->getSkillNames().contains("jinpozhu") ||
					use.from->getPhase() != Player::Play) return false;
			if (use.card->hasFlag("jinpozhu_damage")) return false;
			room->addPlayerMark(use.from, "jinpozhu_wuxiao-Clear");
		}
		return false;
	}
};

class JinZhongyun : public TriggerSkill
{
public:
	JinZhongyun() : TriggerSkill("jinzhongyun")
	{
		events << Damaged << HpRecover << CardsMoveOneTime;
		frequency = Compulsory;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
        if (!player->hasTurn()) return false;
		if (event == Damaged || event == HpRecover) {
			if (player->getMark("jinzhongyun_hp-Clear") > 0 || player->getHp() != player->getHandcardNum()) return false;
			QStringList choices;
			if (player->isWounded())
				choices << "recover";
			QList<ServerPlayer *> targets;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (player->inMyAttackRange(p))
					targets << p;
			}
			if (!targets.isEmpty())
				choices << "damage";
			if (choices.isEmpty()) return false;

			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			room->addPlayerMark(player, "jinzhongyun_hp-Clear");

			QString choice = room->askForChoice(player, objectName(), choices.join("+"));
			if (choice == "recover")
				room->recover(player, RecoverStruct(objectName(), player));
			else {
				ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@jinzhongyun-damage");
				room->doAnimate(1, player->objectName(), target->objectName());
				room->damage(DamageStruct(objectName(), player, target));
			}
		} else {
			if (room->getTag("FirstRound").toBool()) return false;
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (player->getMark("jinzhongyun_move-Clear") > 0 || player->getHp() != player->getHandcardNum()) return false;
			bool can_trigger = false;
			if (move.from == player && move.from_places.contains(Player::PlaceHand))
				can_trigger = true;
			else if (move.to == player && move.to_place == Player::PlaceHand)
				can_trigger = true;
			if (!can_trigger) return false;

			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			room->addPlayerMark(player, "jinzhongyun_move-Clear");

			QStringList choices;
			choices << "draw";
			QList<ServerPlayer *> targets;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (player->canDiscard(p, "he"))
					targets << p;
			}
			if (!targets.isEmpty())
				choices << "discard";
			QString choice = room->askForChoice(player, objectName(), choices.join("+"));
			if (choice == "draw")
				player->drawCards(1, objectName());
			else {
				ServerPlayer *target = room->askForPlayerChosen(player, targets, "jinzhongyun_discard", "@jinzhongyun-discard");
				if (!player->canDiscard(target, "he")) return false;
				room->doAnimate(1, player->objectName(), target->objectName());
				int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
				room->throwCard(id, target, player);
			}
		}
		return false;
	}
};

class JinShenpin : public RetrialSkill
{
public:
	JinShenpin() : RetrialSkill("jinshenpin")
	{
	}

	const Card *onRetrial(ServerPlayer *player, JudgeStruct *judge) const
	{
		if (player->isNude())
			return nullptr;

		QStringList prompt_list;
		prompt_list << "@jinshenpin-card" << judge->who->objectName()
			<< objectName() << judge->reason << QString::number(judge->card->getEffectiveId());
		QString prompt = prompt_list.join(":");

		Room *room = player->getRoom();
		QString color;
		if (judge->card->isRed())
			color = "black";
		else if (judge->card->isBlack())
			color = "red";
		if (color.isEmpty()) return nullptr;

		const Card *card = room->askForCard(player, ".|" + color, prompt, QVariant::fromValue(judge), Card::MethodResponse, judge->who, true);

		if (card)
			room->broadcastSkillInvoke(objectName());
		return card;
	}
};

class JinGaoling : public TriggerSkill
{
public:
	JinGaoling() : TriggerSkill("jingaoling")
	{
		events << Appear;
		hide_skill = true;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (!room->hasCurrent() || room->getCurrent() == player) return false;
		QList<ServerPlayer *> wounded;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->isWounded())
				wounded << p;
		}

		ServerPlayer *to = room->askForPlayerChosen(player, wounded, objectName(), "@jingaoling-invoke", true, true);
		if (!to) return false;
		room->broadcastSkillInvoke(this);

		room->recover(to, RecoverStruct(objectName(), player));
		return false;
	}
};

class JinQimei : public PhaseChangeSkill
{
public:
	JinQimei() : PhaseChangeSkill("jinqimei")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Start) return false;
		ServerPlayer *to = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@jinqimei-invoke", true, true);
		if (!to) return false;
		room->broadcastSkillInvoke(this);

		room->setPlayerMark(player, "&jinqimei_self+#" + to->objectName(), 1);
		room->setPlayerMark(to, "&jinqimei+#" + player->objectName(), 1);
		return false;
	}
};

class JinQimeiEffect : public TriggerSkill
{
public:
	JinQimeiEffect() : TriggerSkill("#jinqimei-effect")
	{
		events << EventPhaseStart << HpChanged << Death << CardsMoveOneTime;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseStart) {
			if (player->getPhase() != Player::RoundStart) return false;
			foreach (QString mark, player->getMarkNames()) {
				if (mark.startsWith("&jinqimei_self+#"))
					room->setPlayerMark(player, mark, 0);
			}
			foreach (ServerPlayer *p, room->getOtherPlayers(player))
				room->setPlayerMark(p, "&jinqimei+#" + player->objectName(), 0);
		} else if (event == Death) {
			DeathStruct death = data.value<DeathStruct>();
			if (death.who != player) return false;
			foreach (ServerPlayer *p, room->getOtherPlayers(player))
				room->setPlayerMark(p, "&jinqimei+#" + player->objectName(), 0);
		} else if (event == HpChanged) {
			if (player->isDead()) return false;
			QList<ServerPlayer *> drawers;
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				int mark = p->getMark("&jinqimei_self+#" + player->objectName());
				if (mark > 0 && player->getMark("jinqimei_self_hp" + p->objectName() + "-Clear") <= 0 &&
						p->getMark("jinqimei_hp" + player->objectName() + "-Clear") <= 0 && player->getHp() == p->getHp()) {
					drawers << p;
					room->addPlayerMark(player, "jinqimei_self_hp" + p->objectName() + "-Clear");
					room->addPlayerMark(p, "jinqimei_hp" + player->objectName() + "-Clear");
				}

			}
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				int mark = p->getMark("&jinqimei+#" + player->objectName());
				if (mark > 0 && p->getMark("jinqimei_self_hp" + player->objectName() + "-Clear") <= 0 &&
						player->getMark("jinqimei_hp" + p->objectName() + "-Clear") <= 0 && player->getHp() == p->getHp()) {
					drawers << p;
					room->addPlayerMark(p, "jinqimei_self_hp" + player->objectName() + "-Clear");
					room->addPlayerMark(player, "jinqimei_hp" + p->objectName() + "-Clear");
				}

			}
			if (!drawers.isEmpty()) {
				LogMessage log;
				log.type = "#ZhenguEffect";
				log.from = player;
				log.arg = "jinqimei";
				room->sendLog(log);
				room->broadcastSkillInvoke("jinqimei");
			}
			room->drawCards(drawers, 1, objectName());
		} else {
			if (player->isDead()) return false;
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if ((move.to == player && move.to_place == Player::PlaceHand) ||
					(move.from == player && move.from_places.contains(Player::PlaceHand))) {
				QList<ServerPlayer *> drawers;
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					int mark = p->getMark("&jinqimei_self+#" + player->objectName());
					if (mark > 0 && player->getMark("jinqimei_self_move" + p->objectName() + "-Clear") <= 0 &&
					p->getMark("jinqimei_move" + player->objectName() + "-Clear") <= 0 && player->getHandcardNum() == p->getHandcardNum()) {
						drawers << p;
						room->addPlayerMark(player, "jinqimei_self_move" + p->objectName() + "-Clear");
						room->addPlayerMark(p, "jinqimei_move" + player->objectName() + "-Clear");
					}
				}
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					int mark = p->getMark("&jinqimei+#" + player->objectName());
					if (mark > 0 && p->getMark("jinqimei_self_move" + player->objectName() + "-Clear") <= 0 &&
					player->getMark("jinqimei_move" + p->objectName() + "-Clear") <= 0 && player->getHandcardNum() == p->getHandcardNum()) {
						drawers << p;
						room->addPlayerMark(p, "jinqimei_self_move" + player->objectName() + "-Clear");
						room->addPlayerMark(player, "jinqimei_move" + p->objectName() + "-Clear");
					}
				}
				if (!drawers.isEmpty()) {
					LogMessage log;
					log.type = "#ZhenguEffect";
					log.from = player;
					log.arg = "jinqimei";
					room->sendLog(log);
					room->broadcastSkillInvoke("jinqimei");
				}
				room->drawCards(drawers, 1, objectName());
			}
		}
		return false;
	}
};

class JinZhuiji : public PhaseChangeSkill
{
public:
	JinZhuiji() : PhaseChangeSkill("jinzhuiji")
	{
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		if (player->getPhase() != Player::Play) return false;
		if (!player->askForSkillInvoke(this)) return false;
		room->broadcastSkillInvoke(this);

		QStringList choices;
		if (player->isWounded())
			choices << "recover";
		choices << "draw";

		QString choice = room->askForChoice(player, objectName(), choices.join("+"));
		room->addPlayerMark(player, "jinzhuiji_" + choice + "-PlayClear");

		if (choice == "recover")
			room->recover(player, RecoverStruct(objectName(), player));
		else
			player->drawCards(2, objectName());
		return false;
	}
};

class JinZhuijiEffect : public TriggerSkill
{
public:
	JinZhuijiEffect() : TriggerSkill("#jinzhuiji-effect")
	{
		events << EventPhaseEnd;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (player->getPhase() != Player::Play) return false;
		int recover = player->getMark("jinzhuiji_recover-PlayClear"), draw = player->getMark("jinzhuiji_draw-PlayClear");
		bool send = true;

		for (int i = 0; i < recover; i++) {
			if (!player->canDiscard(player, "he")) break;
			if (i == 0) {
				send = false;
				LogMessage log;
				log.type = "#ZhenguEffect";
				log.from = player;
				log.arg = "jinzhuiji";
				room->sendLog(log);
				room->broadcastSkillInvoke("jinzhuiji");
				room->notifySkillInvoked(player, "jinzhuiji");
			}
			room->askForDiscard(player, "jinzhuiji", 2, 2, false, true);
		}

		for (int i = 0; i < draw; i++) {
			if (player->isDead()) break;
			if (i == 0 && send) {
				LogMessage log;
				log.type = "#ZhenguEffect";
				log.from = player;
				log.arg = "jinzhuiji";
				room->sendLog(log);
				room->broadcastSkillInvoke("jinzhuiji");
				room->notifySkillInvoked(player, "jinzhuiji");
			}
			room->loseHp(HpLostStruct(player, 1, "jinzhuiji", player));
		}
		return false;
	}
};

GuoPackage::GuoPackage()
	: Package("guo")
{

	General *jin_simazhao = new General(this, "jin_simazhao$", "jin", 3);
	jin_simazhao->addSkill(new JinTuishi);
	jin_simazhao->addSkill(new JinChoufa);
	jin_simazhao->addSkill(new JinZhaoran);
	jin_simazhao->addSkill(new JinZhaoranEffect);
	jin_simazhao->addSkill(new Skill("jinchengwu$", Skill::Compulsory));
	related_skills.insertMulti("jinzhaoran", "#jinzhaoran-effect");

	General *jin_wangyuanji = new General(this, "jin_wangyuanji", "jin", 3, false);
	jin_wangyuanji->addSkill(new JinShiren);
	jin_wangyuanji->addSkill(new JinYanxi);

	General *jin_duyu = new General(this, "jin_duyu", "jin", 4);
	jin_duyu->addSkill(new JinSanchen);
	jin_duyu->addSkill(new JinZhaotao);
	jin_duyu->addRelateSkill("jinpozhu");

	General *jin_weiguan = new General(this, "jin_weiguan", "jin", 3);
	jin_weiguan->addSkill(new JinZhongyun);
	jin_weiguan->addSkill(new JinShenpin);

	General *jin_xuangongzhu = new General(this, "jin_xuangongzhu", "jin", 3, false);
	jin_xuangongzhu->addSkill(new JinGaoling);
	jin_xuangongzhu->addSkill(new JinQimei);
	jin_xuangongzhu->addSkill(new JinQimeiEffect);
	jin_xuangongzhu->addSkill(new JinZhuiji);
	jin_xuangongzhu->addSkill(new JinZhuijiEffect);
	related_skills.insertMulti("jinqimei", "#jinqimei-effect");
	related_skills.insertMulti("jinzhuiji", "#jinzhuiji-effect");

	addMetaObject<JinChoufaCard>();
	addMetaObject<JinYanxiCard>();
	addMetaObject<JinSanchenCard>();

	skills << new JinPozhu;
}
ADD_PACKAGE(Guo)


class JinBolan : public TriggerSkill
{
public:
	JinBolan() : TriggerSkill("jinbolan")
	{
		events << EventPhaseStart << EventPhaseEnd << EventAcquireSkill;
		frequency = Frequent;
	}

	static QStringList getSkills(ServerPlayer *player)
	{
		QStringList skill_names, skills;
		foreach (QString skn, Sanguosha->getSkillNames()) {
			if (skn == "jinbolan" || skill_names.contains(skn)||skn.startsWith("&")) continue;
			if (Sanguosha->getViewAsSkill(skn)==nullptr) continue;
			if (player->hasSkill(skn, true)) continue;

			QString translation = Sanguosha->getSkill(skn)->getDescription();
			if (!translation.contains("出牌阶段限一次，") && !translation.contains("阶段技，") && !translation.contains("出牌阶段限一次。")
					&& !translation.contains("阶段技。")) continue;
			if (translation.contains("，出牌阶段限一次") || translation.contains("，阶段技") || translation.contains("（出牌阶段限一次") ||
					translation.contains("（阶段技")) continue;

			skill_names << skn;
		}
		qShuffle(skill_names);

		for (int i = 0; i < 3; i++) {
			if (skill_names.isEmpty()) break;
			skills << skill_names.takeFirst();
		}

		return skills;
	}
	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
	{
		if (event == EventAcquireSkill) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getPhase()==Player::Play&&!p->hasSkill("jinbolan_skill",true)&&player->hasSkill(objectName(),true)){
					room->attachSkillToPlayer(p, "jinbolan_skill");
				}
			}
		}else if(player->getPhase()!=Player::Play)
			return false;
		if (event == EventPhaseStart) {
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->hasSkill(objectName(),true)){
					room->attachSkillToPlayer(player, "jinbolan_skill");
					break;
				}
			}
			if (player->hasSkill(objectName())&&player->askForSkillInvoke(objectName()+"$-1")){
				QStringList skill_names = getSkills(player);
				if (skill_names.isEmpty()) return false;
				QString skill = room->askForChoice(player, objectName(), skill_names.join("+"));
				player->tag["jinbolan_skill_get_skill"] = skill;
				room->handleAcquireDetachSkills(player, skill);
			}
		} else {
			if (player->hasSkill("jinbolan_skill",true))
				room->detachSkillFromPlayer(player, "jinbolan_skill", true);
			QString skill = player->tag["jinbolan_skill_get_skill"].toString();
			player->tag.remove("jinbolan_skill_get_skill");
			if (!skill.isEmpty())
				room->detachSkillFromPlayer(player, skill);
		}
		return false;
	}
};

JinBolanSkillCard::JinBolanSkillCard()
{
}

bool JinBolanSkillCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && to_select->hasSkill("jinbolan");
}

void JinBolanSkillCard::onUse(Room *room, CardUseStruct &card_use) const
{
	foreach (ServerPlayer*p, card_use.to) {
		card_use.from->skillInvoked("jinbolan",-1,p);
		room->loseHp(HpLostStruct(card_use.from, 1, "jinbolan", card_use.from));
	
		QStringList skill_names = JinBolan::getSkills(card_use.from);
		if (skill_names.isEmpty() || card_use.from->isDead()) continue;
	
		QString skill = room->askForChoice(p, "jinbolan_skill", skill_names.join("+"), QVariant::fromValue(card_use.from));
	
		card_use.from->tag["jinbolan_skill_get_skill"] = skill;
		room->handleAcquireDetachSkills(card_use.from, skill);
	}
}

class JinBolanSkill : public ZeroCardViewAsSkill
{
public:
	JinBolanSkill() : ZeroCardViewAsSkill("jinbolan_skill")
	{
		attached_lord_skill = true;
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return hasTarget(player);
	}

    bool hasTarget(const Player *player) const
	{
		foreach (const Player *p, player->getAliveSiblings()) {
			if (p->hasSkill("jinbolan") && player->usedTimes("JinBolanSkillCard") <= 0)
				return true;
		}
		return false;
	}

	const Card *viewAs() const
	{
		return new JinBolanSkillCard;
	}
};

class JinYifa : public TriggerSkill
{
public:
	JinYifa() : TriggerSkill("jinyifa")
	{
		events << TargetSpecified << EventPhaseChanging;
		frequency = Compulsory;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isKindOf("Slash") || (use.card->isBlack() && use.card->isNDTrick())) {
				foreach (ServerPlayer *p, use.to) {
					if (player->isDead()) return false;
					if (p->isDead() || !p->hasSkill(objectName()) || p == use.from) continue;
					room->sendCompulsoryTriggerLog(p, objectName(), true, true);
					room->addPlayerMark(player, "&jinyifa");
				}
			}
		} else {
			if (player->getMark("&jinyifa") <= 0) return false;
			if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
			room->setPlayerMark(player, "&jinyifa", 0);
		}
		return false;
	}
};

class JinYifaMax : public MaxCardsSkill
{
public:
	JinYifaMax() : MaxCardsSkill("#jinyifa")
	{
	}

	int getExtra(const Player *target) const
	{
		return -target->getMark("&jinyifa");
	}
};

class JinCanmou : public TriggerSkill
{
public:
	JinCanmou() : TriggerSkill("jincanmou")
	{
		events << TargetSpecifying;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isNDTrick() || use.card->isKindOf("Collateral")) return false;

		foreach (ServerPlayer *p, room->getAllPlayers()) {
			if (player->isDead()) return false;
			if (p->isDead() || !p->hasSkill(objectName())) continue;

			int hand = player->getHandcardNum();
			foreach (ServerPlayer *q, room->getOtherPlayers(player)) {
				if (q->getHandcardNum() >= hand) return false;
			}
			QList<ServerPlayer *> targets = room->getCardTargets(player, use.card, use.to);
			if (targets.isEmpty()) return false;

			p->tag["JincanmouData"] = data;
			ServerPlayer *t = room->askForPlayerChosen(p, targets, objectName(), "@jincanmou-target:" + use.card->objectName(), true, true);
			p->tag.remove("JincanmouData");
			if (!t) continue;
			room->broadcastSkillInvoke(this);
			use.to << t;
			room->sortByActionOrder(use.to);
			data = QVariant::fromValue(use);
		}
		return false;
	}
};

class JinCongjian : public TriggerSkill
{
public:
	JinCongjian() : TriggerSkill("jincongjian")
	{
		events << TargetConfirming;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isNDTrick() || use.card->isKindOf("Collateral")) return false;

		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (use.to.length() != 1) return false;
			if (p->isDead() || !p->hasSkill(objectName())) continue;

			int hp = player->getHp();
			foreach (ServerPlayer *q, room->getOtherPlayers(player)) {
				if (q->getHp() >= hp) return false;
			}

			if (use.from && !use.from->canUse(use.card, p, true)) continue;
			p->tag["JincongjianData"] = data;
			bool invoke = p->askForSkillInvoke(this, "jincongjian:" + use.card->objectName());
			p->tag.remove("JincongjianData");
			if (!invoke) continue;
			use.to << p;
			room->sortByActionOrder(use.to);
			data = QVariant::fromValue(use);
			room->setCardFlag(use.card, "jincongjian_" + p->objectName());
		}
		return false;
	}
};

class JinCongjianEffect : public TriggerSkill
{
public:
	JinCongjianEffect() : TriggerSkill("#jincongjian-effect")
	{
		events << DamageDone << CardFinished;
	}

	bool triggerable(const ServerPlayer *) const
	{
		return true;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == DamageDone) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.card->hasFlag("jincongjian_" + player->objectName())) return false;
			room->setCardFlag(damage.card, "jincongjian_damage_" + player->objectName());
		} else {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isNDTrick()) return false;
			foreach (ServerPlayer *p, room->getAllPlayers()) {
				if (p->isDead()) continue;
				if (!use.card->hasFlag("jincongjian_damage_" + p->objectName())) continue;
				p->drawCards(2, "jincongjian");
			}
		}
		return false;
	}
};

class JinXiongshu : public PhaseChangeSkill
{
public:
	JinXiongshu() : PhaseChangeSkill("jinxiongshu")
	{
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target && target->getPhase() == Player::Play;
	}

	bool onPhaseChange(ServerPlayer *player, Room *room) const
	{
		foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
			if (p->isDead() || !p->hasSkill(objectName())) continue;
			int mark = p->getMark("&jinxiongshu_num_lun");
			bool invoke = false;
			if (mark == 0)
				invoke = p->askForSkillInvoke(this, player);
			else
				invoke = room->askForDiscard(p, objectName(), mark, mark, true, true,
								QString("@jinxiongshu-discard:%1::%2").arg(player->objectName()).arg(mark), ".", objectName());
			if (!invoke) continue;
			room->addPlayerMark(p, "&jinxiongshu_num_lun");
			p->peiyin(this);

			if (p->isDead() || player->isKongcheng()) continue;

			int id = room->askForCardChosen(p, player, "h", objectName());
			room->showCard(player, id);
			player->tag["JinXiongshuShowCard_" + p->objectName()] = id + 1;

			const Card *c = Sanguosha->getCard(id);
			QString name = c->objectName();
			if (c->isKindOf("Slash"))
				name = "slash";

			int guess = p->askForSkillInvoke("jinxiongshu_guess", QString("jinxiongshu_guess:%1::%2").arg(player->objectName()).arg(name),false) ? 2 : 1;
			room->setPlayerMark(p, "jinxiongshu_show_" + name + "-PlayClear", guess);
		}
		return false;
	}
};

class JinXiongshuEffect : public TriggerSkill
{
public:
	JinXiongshuEffect() : TriggerSkill("#jinxiongshu")
	{
		events << PreCardUsed << EventPhaseEnd;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	int getMark(ServerPlayer *player, QString _mark) const
	{
		int mark = 0;
		QString mark_name = "jinxiongshu_" + _mark + "_";
		foreach (QString m, player->getMarkNames()) {
			if (!m.startsWith(mark_name) || player->getMark(m) <= 0 || !m.endsWith("-PlayClear")) continue;
			mark = player->getMark(m);
			break;
		}
		return mark;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == EventPhaseEnd) {
			if (player->getPhase() != Player::Play) return false;

			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				int id = player->tag["JinXiongshuShowCard_" + p->objectName()].toInt() - 1;
				player->tag.remove("JinXiongshuShowCard_" + p->objectName());

				int guess = getMark(p, "show");
				if (guess <= 0) continue;

				room->sendCompulsoryTriggerLog(p, "jinxiongshu");

				int use = getMark(p, "used");
				if ((guess == 2 && use == 1) || (guess == 1 && use <= 0))
					room->damage(DamageStruct("jinxiongshu", p, player));
				else {
					if (id < 0) continue;
					room->obtainCard(p, id);
				}
			}
		} else {
			const Card *card = nullptr;
			if (event == PreCardUsed)
				card = data.value<CardUseStruct>().card;
			if (!card || card->isKindOf("SkillCard")) return false;

			QString name = card->objectName();
			if (card->isKindOf("Slash")) name = "slash";

			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getMark("jinxiongshu_show_" + name + "-PlayClear") <= 0) continue;
				room->setPlayerMark(p, "jinxiongshu_used_" + name + "-PlayClear", 1);
			}
		}
		return false;
	}
};

class JinJianhui : public TriggerSkill
{
public:
	JinJianhui() : TriggerSkill("jinjianhui")
	{
		events << Damage << Damaged;
		frequency = Compulsory;
        global = true;//
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target&&target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		DamageStruct damage = data.value<DamageStruct>();
		ServerPlayer *last = player->tag["JinJianhuiDamageYou"].value<ServerPlayer *>();
        if (player->getMark("mobilechengzhang")<1&&player->hasSkill("mobilechengzhang", true))
            room->addPlayerMark(player, "&mobilechengzhang", damage.damage);
        else
            player->addMark("mobilechengzhang_num", damage.damage);
		if (event == Damage) {
			if (damage.to == last&&player->hasSkill(objectName())) {
				room->sendCompulsoryTriggerLog(player, this);
				player->drawCards(1, objectName());
			}
		} else {
			if (!damage.from) return false;
			player->tag["JinJianhuiDamageYou"] = QVariant::fromValue(damage.from);
			if (damage.from == last && last->canDiscard(last, "he")&&player->hasSkill(objectName())) {
				room->sendCompulsoryTriggerLog(player, this);
				room->askForDiscard(damage.from, objectName(), 1, 1, false, true);
			}
		}
		return false;
	}
};

JinBingxinCard::JinBingxinCard()
{
	mute = true;
	handling_method = Card::MethodUse;
	//target_fixed = true;
}

bool JinBingxinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	card = Self->tag.value("jinbingxin").value<Card *>();
	return card && card->targetFilter(targets, to_select, Self);
}

bool JinBingxinCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetFixed();
	}
	card = Self->tag.value("jinbingxin").value<Card *>();
	return card && card->targetFixed();
}

bool JinBingxinCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	card = Self->tag.value("jinbingxin").value<Card *>();
	return card && card->targetsFeasible(targets, Self);
}

const Card *JinBingxinCard::validate(CardUseStruct &card_use) const
{
	Room *room = card_use.from->getRoom();

	QString to_yizan = user_string;
	if ((user_string.contains("slash") || user_string.contains("Slash"))
		&& Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		QStringList guhuo_list = Sanguosha->getSlashNames();
		if (guhuo_list.isEmpty()) guhuo_list << "slash";
		to_yizan = room->askForChoice(card_use.from, "jinbingxin_slash", guhuo_list.join("+"));
	}

	Card *use_card = Sanguosha->cloneCard(to_yizan);
	use_card->setSkillName("jinbingxin");
	use_card->deleteLater();/*
	if(!use_card->targetFixed()&&card_use.to.isEmpty()){
		room->setPlayerProperty(card_use.from,"jinbingxinUse",to_yizan);
		if(room->askForCard(card_use.from,"@@jinbingxin","jinbingxin0:"+to_yizan,QVariant::fromValue(card_use),Card::MethodUse,nullptr,true)){
			card_use.clientReply();
		}else
			return nullptr;
	}*/
	card_use.from->drawCards(1, "jinbingxin");
	room->addPlayerMark(card_use.from, "jinbingxin_guhuo_remove_" + to_yizan + "-Clear");
	if (use_card->isKindOf("Slash"))
		room->addPlayerMark(card_use.from, "jinbingxin_guhuo_remove_normal_slash-Clear");
	return use_card;
}

const Card *JinBingxinCard::validateInResponse(ServerPlayer *player) const
{
	Room *room = player->getRoom();

	QString to_yizan;
	if (user_string == "peach+analeptic") {
		QStringList guhuo_list;
		guhuo_list << "peach";
		if (Sanguosha->hasCard("analeptic")) guhuo_list << "analeptic";
		to_yizan = room->askForChoice(player, "jinbingxin_saveself", guhuo_list.join("+"));
	} else if (user_string.contains("slash") || user_string.contains("Slash")) {
		QStringList guhuo_list = Sanguosha->getSlashNames();
		if (guhuo_list.isEmpty()) guhuo_list << "slash";
		to_yizan = room->askForChoice(player, "jinbingxin_slash", guhuo_list.join("+"));
	} else
		to_yizan = user_string;

	Card *use_card = Sanguosha->cloneCard(to_yizan);
	use_card->setSkillName("jinbingxin");

	player->drawCards(1, "jinbingxin");
	room->addPlayerMark(player, "jinbingxin_guhuo_remove_" + to_yizan + "-Clear");
	if (use_card->isKindOf("Slash"))
		room->addPlayerMark(player, "jinbingxin_guhuo_remove_normal_slash-Clear");
	use_card->deleteLater();
	return use_card;
}

class JinBingxin : public ZeroCardViewAsSkill
{
public:
	JinBingxin() : ZeroCardViewAsSkill("jinbingxin")
	{
	}

	QDialog *getDialog() const
	{
		return GuhuoDialog::getInstance("jinbingxin", true, false);
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		if (player->getHandcardNum() != player->getHp()) return false;
		if (player->isKongcheng()) return true;
		const Card *card = player->getHandcards().first();
		foreach (const Card *c, player->getHandcards()) {
			if (!c->sameColorWith(card))
				return false;
		}
		return true;
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) return false;
		if (pattern == "peach" && player->getMark("Global_PreventPeach") > 0) return false;
		if (player->getHandcardNum() != player->getHp()) return false;
		if (!player->isKongcheng()) {
			const Card *card = player->getHandcards().first();
			foreach (const Card *c, player->getHandcards()) {
				if (!c->sameColorWith(card)) return false;
			}
		}
		if(pattern.contains(",")){
			foreach (QString name, pattern.split(",")) {
				if(player->getMark("jinbingxin_guhuo_remove_"+name+"-Clear")>0)
					continue;
				Card *card = Sanguosha->cloneCard(name);
				if (!card) continue;
				card->setSkillName(objectName());
				card->deleteLater();
				if (card->isKindOf("BasicCard"))
					return true;
			}
		}else{
			foreach (QString name, pattern.split("+")) {
				if(player->getMark("jinbingxin_guhuo_remove_"+name+"-Clear")>0)
					continue;
				Card *card = Sanguosha->cloneCard(name);
				if (!card) continue;
				card->setSkillName(objectName());
				card->deleteLater();
				if (card->isKindOf("BasicCard"))
					return true;
			}
		}
		return pattern=="@@jinbingxin";
	}

	const Card *viewAs() const
	{
		QString pattern = Sanguosha->getCurrentCardUsePattern();
		if(pattern=="@@jinbingxin"){
			pattern = Self->property("jinbingxinUse").toString();
			Card *use_card = Sanguosha->cloneCard(pattern);
			use_card->setSkillName("jinbingxin");
			return use_card;
		}else if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
			JinBingxinCard *c = new JinBingxinCard;
			c->setUserString(pattern);
			return c;
		}
		const Card *c = Self->tag.value("jinbingxin").value<const Card *>();
		if (c && c->isAvailable(Self)) {
			JinBingxinCard *cc = new JinBingxinCard;
			cc->setUserString(c->objectName());
			return cc;
		}
		return nullptr;
	}
};

JiePackage::JiePackage()
	: Package("jie_package")
{
	General *jin_zhongyan = new General(this, "jin_zhongyan", "jin", 3, false);
	jin_zhongyan->addSkill(new JinBolan);
	jin_zhongyan->addSkill(new JinYifa);
	jin_zhongyan->addSkill(new JinYifaMax);
	related_skills.insertMulti("jinyifa", "#jinyifa");

	General *jin_xinchang = new General(this, "jin_xinchang", "jin", 3);
	jin_xinchang->addSkill(new JinCanmou);
	jin_xinchang->addSkill(new JinCongjian);
	jin_xinchang->addSkill(new JinCongjianEffect);
	related_skills.insertMulti("jincongjian", "#jincongjian-effect");

	General *jin_jiachong = new General(this, "jin_jiachong", "jin", 3);
	jin_jiachong->addSkill(new JinXiongshu);
	jin_jiachong->addSkill(new JinXiongshuEffect);
	jin_jiachong->addSkill(new JinJianhui);
	related_skills.insertMulti("jinxiongshu", "#jinxiongshu");

	General *jin_wangxiang = new General(this, "jin_wangxiang", "jin", 3);
	jin_wangxiang->addSkill(new JinBingxin);

	addMetaObject<JinBolanSkillCard>();
	addMetaObject<JinBingxinCard>();

	skills << new JinBolanSkill;
}
ADD_PACKAGE(Jie)

JinXuanbeiCard::JinXuanbeiCard()
{
}

bool JinXuanbeiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && !to_select->isAllNude();
}

void JinXuanbeiCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer *from = effect.from, *to = effect.to;
	if (to->isAllNude()) return;
	Room *room = from->getRoom();

	int id = room->askForCardChosen(from, to, "hej", "jinxuanbei");
	Slash *slash = new Slash(Card::SuitToBeDecided, -1);
	slash->deleteLater();
	slash->addSubcard(id);
	slash->setSkillName("_jinxuanbei");
	if (!to->canSlash(from, slash, false)) return;

	room->setCardFlag(slash, "jinxuanbei_slash_to_" + from->objectName());
	room->useCard(CardUseStruct(slash, to, from));
}

class JinXuanbeiVS : public ZeroCardViewAsSkill
{
public:
	JinXuanbeiVS() : ZeroCardViewAsSkill("jinxuanbei")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return !player->hasUsed("JinXuanbeiCard");
	}

	const Card *viewAs() const
	{
		return new JinXuanbeiCard;
	}
};

class JinXuanbei : public TriggerSkill
{
public:
	JinXuanbei() : TriggerSkill("jinxuanbei")
	{
		events << CardFinished;
		view_as_skill = new JinXuanbeiVS;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr;
	}

	bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card->isKindOf("Slash")) return false;
		if (!use.card->getSkillNames().contains(objectName())) return false;

		ServerPlayer *drawer = nullptr;
		foreach (QString flag, use.card->getFlags()) {
			if (!flag.startsWith("jinxuanbei_slash_to_")) continue;
			QStringList flags = flag.split("_");
			if (flags.length() != 4) continue;
			drawer = room->findChild<ServerPlayer *>(flags.last());
			if (drawer) break;
		}
		if (!drawer) return false;

		int num = use.card->hasFlag("DamageDone_" + drawer->objectName()) ? 2 : 1;
		drawer->drawCards(num, objectName());
		return false;
	}
};

JinXianwanCard::JinXianwanCard()
{
}

bool JinXianwanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		Card *c = Sanguosha->cloneCard(user_string.split("+").first());
		if (c) {
			c->setSkillName("jinxianwan");
			c->deleteLater();
			if (c->targetFixed())
				return c->isAvailable(Self);
		}
		return c && c->targetFilter(targets, to_select, Self);
	}

	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->deleteLater();
	slash->setSkillName("jinxianwan");
	return slash->targetFilter(targets, to_select, Self);
}

bool JinXianwanCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		Card *c = Sanguosha->cloneCard(user_string.split("+").first());
		if (c) {
			c->setSkillName("jinxianwan");
			c->deleteLater();
		}
		return c && c->targetsFeasible(targets, Self);
	}

	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->deleteLater();
	slash->setSkillName("jinxianwan");
	return slash->targetsFeasible(targets, Self);
}

const Card *JinXianwanCard::validate(CardUseStruct &cardUse) const
{
	ServerPlayer *source = cardUse.from;

	QString str = user_string;
	if (user_string.contains("Slash") || user_string.contains("slash"))
		str = "slash";
	if (user_string.contains("Jink") || user_string.contains("jink"))
		str = "jink";

	if (source->isChained() && str == "jink") return nullptr;
	if (!source->isChained() && str == "slash") return nullptr;

	Card *c = Sanguosha->cloneCard(str);
	if (!c) return nullptr;
	c->setSkillName("jinxianwan");
	c->deleteLater();
	if (source->isLocked(c)) return nullptr;

	Room *room = source->getRoom();
	room->setPlayerChained(source);

	if (source->isDead()) return nullptr;

	return c;
}

const Card *JinXianwanCard::validateInResponse(ServerPlayer *user) const
{
	QString str = user_string;
	if (user_string.contains("Slash") || user_string.contains("slash"))
		str = "slash";
	if (user_string.contains("Jink") || user_string.contains("jink"))
		str = "jink";

	if (user->isChained() && str == "jink") return nullptr;
	if (!user->isChained() && str == "slash") return nullptr;

	Card *c = Sanguosha->cloneCard(str);
	if (!c) return nullptr;
	c->setSkillName("jinxianwan");
	c->deleteLater();

	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
		if (user->isCardLimited(c, Card::MethodResponse))
			return nullptr;
	} else {
		if (user->isLocked(c))
			return nullptr;
	}

	Room *room = user->getRoom();
	room->setPlayerChained(user);

	if (user->isDead()) return nullptr;

	return c;
}

class JinXianwan : public ZeroCardViewAsSkill
{
public:
	JinXianwan() : ZeroCardViewAsSkill("jinxianwan")
	{
	}

	bool isEnabledAtPlay(const Player *player) const
	{
		return player->isChained() && Slash::IsAvailable(player);
	}

	bool isEnabledAtResponse(const Player *player, const QString &pattern) const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) return false;
		if (pattern.contains("Jink") || pattern.contains("jink"))
			return !player->isChained();
		if (pattern.contains("Slash") || pattern.contains("slash"))
			return player->isChained();
		return false;
	}

	const Card *viewAs() const
	{
		if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
			JinXianwanCard *c = new JinXianwanCard;
			c->setUserString(Sanguosha->currentRoomState()->getCurrentCardUsePattern());
			return c;
		}

		Slash *slash = new Slash(Card::NoSuit, 0);
		slash->deleteLater();
		slash->setSkillName("jinxianwan");
		if (slash->IsAvailable(Self)) {
			JinXianwanCard *c = new JinXianwanCard;
			c->setUserString("slash");
			return c;
		}
		return nullptr;
	}
};

class JinWanyi : public TriggerSkill
{
public:
	JinWanyi() : TriggerSkill("jinwanyi")
	{
		events << TargetSpecified << EventPhaseStart << Damaged;
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash") && !use.card->isNDTrick()) return false;
			if (use.to.length() != 1 || use.to.first() == player) return false;
			ServerPlayer *to = use.to.first();
			if (to->isNude() || !player->askForSkillInvoke(this, to)) return false;
			player->peiyin(this);
			int id = room->askForCardChosen(player, to, "he", objectName());
			player->addToPile(objectName(), id);
		} else {
			if (player->getPile(objectName()).isEmpty()) return false;
			if (event == EventPhaseStart) {
				if (player->getPhase() != Player::Finish) return false;
			}
			ServerPlayer *t = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@jinwanyi-target", false, true);
			player->peiyin(this);
			QList<int> wanyi = player->getPile(objectName());
			if (wanyi.isEmpty()) return false;
			room->fillAG(wanyi, t);
			int id = room->askForAG(t, wanyi, false, objectName(), "@jinwanyi-get");
			room->clearAG(t);

			if (t == player) {
				LogMessage log;
				log.type = "$KuangbiGet";
				log.from = player;
				log.arg = objectName();
				log.card_str = QString::number(id);
				room->sendLog(log);
			}

			room->obtainCard(t, id);
		}
		return false;
	}
};

class JinWanyiLimit : public CardLimitSkill
{
public:
	JinWanyiLimit() : CardLimitSkill("#jinwanyi-limit")
	{
	}

	QString limitList(const Player *) const
	{
		return "use,response,discard";
	}

	QString limitPattern(const Player *target) const
	{
		if (target->getPile("jinwanyi").length()>0 && target->hasSkill("jinwanyi")) {
			QStringList suits;
			foreach (int id, target->getPile("jinwanyi")) {
				QString str = Sanguosha->getCard(id)->getSuitString();
				if (suits.contains(str)) continue;
				suits << str;
			}
			return ".|" + suits.join(",");
		}
		return "";
	}
};

class JinMaihuo : public TriggerSkill
{
public:
	JinMaihuo() : TriggerSkill("jinmaihuo")
	{
		events << TargetSpecified << EventPhaseStart << Damage;
	}

	bool triggerable(const ServerPlayer *target) const
	{
		return target != nullptr && target->isAlive();
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
	{
		if (event == TargetSpecified) {
			CardUseStruct use = data.value<CardUseStruct>();
			if (!use.card->isKindOf("Slash") || use.card->isVirtualCard() || !use.card->getSkillName().isEmpty() ||
				use.card->hasFlag("jinmaihuo_using")) return false;
			foreach (ServerPlayer *p, use.to) {
				if (!use.from || !use.from->getPile("jinmhhuo").isEmpty()) return false;
				if (p->isDead() || !p->hasSkill(objectName()) || use.from == p) continue;
				if (!p->askForSkillInvoke(this, data)) continue;
				p->peiyin(this);

				use.nullified_list << p->objectName();
				data = QVariant::fromValue(use);

				use.from->tag["JinmaihuoPlayer"] = QVariant::fromValue(p);
				use.from->addToPile("jinmhhuo", use.card);
			}
		} else if (event == EventPhaseStart) {
			if (player->getPhase() != Player::Play) return false;
			QList<int> pile = player->getPile("jinmhhuo");
			if (pile.isEmpty()) return false;

			ServerPlayer *to = player->tag["JinmaihuoPlayer"].value<ServerPlayer *>();
			player->tag.remove("JinmaihuoPlayer");

			int id = pile.first();
			const Card *slash = Sanguosha->getCard(id);
			DummyCard dummy;
			dummy.addSubcards(pile);

			LogMessage log;
			log.type = "#ZhenguEffect";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			if (to)
				to->peiyin(this);
			else
				room->broadcastSkillInvoke(objectName());

			if (!to || to->isDead() || !slash->isKindOf("Slash") || !Slash::IsAvailable(player) || !player->canSlash(to, slash)) {
				CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, player->objectName(), objectName(), "");
				room->throwCard(&dummy, reason, nullptr);
			} else {
				room->setCardFlag(slash, "jinmaihuo_using");
				room->useCard(CardUseStruct(slash, player, to), true);
			}
		} else {
			if (!player->hasSkill(objectName())) return false;
			DamageStruct damage = data.value<DamageStruct>();
			if (damage.from == damage.to || !damage.to) return false;
			QList<int> pile = damage.to->getPile("jinmhhuo");
			if (pile.isEmpty()) return false;

			room->sendCompulsoryTriggerLog(player, this);

			DummyCard dummy;
			dummy.addSubcards(pile);
			CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, damage.to->objectName(), objectName(), "");
			room->throwCard(&dummy, reason, nullptr);
		}
		return false;
	}
};

YuePackage::YuePackage()
	: Package("yue")
{
	General *jin_yangyan = new General(this, "jin_yangyan", "jin", 3, false);
	jin_yangyan->addSkill(new JinXuanbei);
	jin_yangyan->addSkill(new JinXianwan);

	General *jin_yangzhi = new General(this, "jin_yangzhi", "jin", 3, false);
	jin_yangzhi->addSkill(new JinWanyi);
	jin_yangzhi->addSkill(new JinWanyiLimit);
	jin_yangzhi->addSkill(new JinMaihuo);
	related_skills.insertMulti("jinwanyi", "#jinwanyi-limit");

	addMetaObject<JinXuanbeiCard>();
	addMetaObject<JinXianwanCard>();
}
ADD_PACKAGE(Yue)
