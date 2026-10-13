// GU-audit2026 2026-07d: 界马超（台服重置）.
// Skill text: finaldebug-md-ascii/audit2026.md ### 2026-07 (translations in lang/zh_CN/Package/Mobile.lua).
// The repo machao (2014 界限突破) is another server's version and stays untouched; this is a separate general.
#include "gu-audit2026.h"
#include "general.h"
#include "engine.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "serverplayer.h"
#include "clientplayer.h"

// ---------------- 界马超（台服重置） 铁骑 ----------------
// Existing machao 铁骑 text with only the quoted delta applied:
// 「该角色须弃置一张与判定牌花色相同的牌，否则其不能使用【闪】响应此【杀】」 →
// 「除非该角色交给你一张同花色牌，否则不能响应且此【杀】对其造成的伤害+1」.
// "同花色" = the judge card's suit (the only suit in the sentence). "不能响应" uses no_respond_list,
// so every response to this 【杀】 is blocked for that target, not only 【闪】.
class Gu07dTieji : public TriggerSkill
{
public:
	Gu07dTieji() : TriggerSkill("gu_twtieji")
	{
		events << TargetSpecified << ConfirmDamage << CardFinished << EventPhaseChanging << Death;
	}

	bool triggerable(const ServerPlayer *target) const override
	{
		return target != nullptr;
	}

	static void clearInvalidity(Room *room)
	{
		bool changed = false;
		foreach (ServerPlayer *p, room->getAllPlayers(true)) {
			const int n = p->getMark("gu_twtieji");
			if (n <= 0) continue;
			p->setMark("gu_twtieji", 0);
			room->removePlayerMark(p, "@skill_invalidity", n);
			changed = true;
		}
		if (!changed) return;
		foreach (ServerPlayer *p, room->getAllPlayers())
			room->filterCards(p, p->getCards("he"), false);
		JsonArray args;
		args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
		room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
	}

	bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (event == EventPhaseChanging) {
			if (data.value<PhaseChangeStruct>().to == Player::NotActive) clearInvalidity(room);
			return false;
		}
		if (event == Death) {
			if (data.value<DeathStruct>().who == player && player == room->getCurrent()) clearInvalidity(room);
			return false;
		}
		if (event == ConfirmDamage) {
			DamageStruct damage = data.value<DamageStruct>();
			if (!damage.card || !damage.to || !damage.card->hasFlag("gu_twtieji_" + damage.to->objectName())) return false;
			LogMessage log;
			log.type = "#gu_twtieji_damage";
			log.from = damage.from;
			log.to << damage.to;
			log.arg = QString::number(damage.damage);
			log.arg2 = QString::number(damage.damage + 1);
			room->sendLog(log);
			damage.damage++;
			data = QVariant::fromValue(damage);
			return false;
		}
		CardUseStruct use = data.value<CardUseStruct>();
		if (event == CardFinished) {
			if (!use.card) return false;
			foreach (ServerPlayer *p, room->getAllPlayers(true))
				if (use.card->hasFlag("gu_twtieji_" + p->objectName()))
					room->setCardFlag(use.card, "-gu_twtieji_" + p->objectName());
			return false;
		}
		if (use.from != player || !player->isAlive() || !player->hasSkill(this) || !use.card || !use.card->isKindOf("Slash"))
			return false;
		bool changed = false;
		for (int i = 0; i < use.to.length(); i++) {
			ServerPlayer *t = use.to.at(i);
			if (player->isDead()) break;
			if (t->isDead() || !player->askForSkillInvoke(this, QVariant::fromValue(t))) continue;
			room->broadcastSkillInvoke(objectName());
			t->addMark("gu_twtieji");
			room->addPlayerMark(t, "@skill_invalidity");
			foreach (ServerPlayer *p, room->getAllPlayers()) room->filterCards(p, p->getCards("he"), true);
			JsonArray args;
			args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
			room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
			JudgeStruct judge;
			judge.pattern = ".";
			judge.good = true;
			judge.reason = objectName();
			judge.who = player;
			judge.play_animation = false;
			room->judge(judge);
			const QString suit = judge.card ? judge.card->getSuitString() : QString();
			const Card *given = nullptr;
			if (!suit.isEmpty() && t->isAlive() && player->isAlive() && t != player && !t->isNude()) {
				given = room->askForCard(t, ".|" + suit + "|.|hand,equipped",
					"@gu_twtieji-give:" + player->objectName() + "::" + suit, data, Card::MethodNone);
			}
			if (given) {
				room->giveCard(t, player, given, objectName(), true);
				continue;
			}
			LogMessage log;
			log.type = "#gu_twtieji_norespond";
			log.from = t;
			log.card_str = use.card->toString();
			room->sendLog(log);
			if (!use.no_respond_list.contains(t->objectName())) use.no_respond_list << t->objectName();
			room->setCardFlag(use.card, "gu_twtieji_" + t->objectName());
			changed = true;
		}
		if (changed) data = QVariant::fromValue(use);
		return false;
	}
};

void guAudit2026M07d(Package *pkg)
{
	General *machao = new General(pkg, "gu_twjiemachao", "shu", 4, true);
	machao->addSkill("mashu");
	machao->addSkill(new Gu07dTieji);
}
