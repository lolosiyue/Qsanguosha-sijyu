#include "yjcm2022.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
#include "standard-cards.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include <QScopeGuard>

class Liandui : public TriggerSkillV2
{
public:
    Liandui() : TriggerSkillV2("liandui") { events << CardUsed << CardResponded; }
    ServerPlayer *previousUser(TriggerEvent event, Room *room) const
    {
        const QString currentKind = event == CardUsed ? "use_card" : "respond_card";
        const QVariantMap current = room->historyParent(room->currentHistoryEventId(), currentKind, true);
        const qint64 eventId = current.value("id").toLongLong();
        if (eventId <= 0) return nullptr;
        const QVariantMap accepted = room->queryHistoryFacts({{"event_id", eventId}, {"kind", currentKind}, {"limit", 1}});
        if (!accepted.value("complete").toBool() || !accepted.value("attribution_complete").toBool()
            || accepted.value("items").toList().isEmpty()) return nullptr;
        const qint64 before = accepted.value("items").toList().first().toMap().value("sequence").toLongLong() - 1;
        if (before <= 0) return nullptr;
        qint64 latest = 0;
        QString user;
        // Compare immutable accepted-use facts across both ordinary use and response-use.
        for (const QString &kind : {QString("use_card"), QString("respond_card")}) {
            QVariantMap filter{{"kind", kind}, {"watermark", before}};
            for (;;) {
                const QVariantMap page = room->queryHistoryFacts(filter);
                if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return nullptr;
                for (const QVariant &entry : page.value("items").toList()) {
                    const QVariantMap fact = entry.toMap(), data = fact.value("data").toMap();
                    const QVariantMap card = data.value("card").toMap();
                    if (!card.contains("type")) return nullptr;
                    if (card.value("type").toInt() == Card::TypeSkill || (kind == "respond_card" && !data.value("is_use").toBool())) continue;
                    const qint64 sequence = fact.value("sequence").toLongLong();
                    if (sequence > latest) {
                        latest = sequence;
                        user = data.value(kind == "respond_card" ? "player" : "from").toString();
                    }
                }
                if (!page.value("has_more").toBool()) break;
                filter["after"] = page.value("next_after");
            }
        }
        return user.isEmpty() ? nullptr : room->findPlayerByObjectName(user);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive()) return {};
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
        if (!card || card->isKindOf("SkillCard") || (event == CardResponded && !data.value<CardResponseStruct>().m_isUse)) return {};
        ServerPlayer *last = previousUser(event, room);
        if (!last || !last->isAlive() || last == player) return {};
        TriggerList result;
        if (player->hasSkill(this)) result[player] << objectName();
        if (last->hasSkill(this)) result[last] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.invoker) return false;
        ServerPlayer *last = previousUser(event, room);
        if (!last || !last->isAlive() || last == ctx.invoker) return false;
        const bool ownUse = ctx.owner == ctx.invoker;
        if (!ownUse && ctx.owner != last) return false;
        const bool invoke = ownUse ? ctx.owner->askForSkillInvoke(this, "liandui:" + last->objectName(), false)
            : ctx.invoker->askForSkillInvoke("lianduiother", "lianduiother:" + last->objectName(), false);
        if (!invoke) return false;
        ctx.targets = {last};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive()) target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        return false;
    }
};
BiejunCard::BiejunCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
	mute = true;
}

bool BiejunCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && to_select->getMark("biejunTarget-PlayClear") <= 0 && to_select->hasSkill("biejun");
}

void BiejunCard::onUse(Room *room, CardUseStruct &use) const
{
	QVariant data = QVariant::fromValue(use);
	room->getThread()->trigger(PreCardUsed, room, use.from, data);
	room->getThread()->trigger(CardUsed, room, use.from, data);
	use = data.value<CardUseStruct>();
	foreach (ServerPlayer *p, use.to){
		use.from->skillInvoked("biejun",-1,p);
		room->giveCard(use.from, p, this, "biejun");
		if(p->hasCard(subcards.last())&&room->hasCurrent())
			room->setCardTip(subcards.last(),"biejun-Clear");
	}
	room->getThread()->trigger(CardFinished, room, use.from, data);
	use = data.value<CardUseStruct>();
}

class BiejunGive : public OneCardViewAsSkill
{
public:
    BiejunGive() : OneCardViewAsSkill("biejun-give&")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getHandcardNum()>0&&player->usedTimes("BiejunCard")<1;
    }

    const Card *viewAs(const Card *c) const
    {
        BiejunCard *card = new BiejunCard;
        card->addSubcard(c);
        return card;
    }
};

class Biejun : public TriggerSkill
{
public:
    Biejun() : TriggerSkill("biejun")
    {
        global = true;
        events << EventPhaseStart << EventPhaseEnd << EventAcquireSkill << DamageInflicted;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target&&target->isAlive();
    }
    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
		if (triggerEvent == DamageInflicted){
			if (player->getMark("biejunDamage-Clear") > 0) return false;
			player->addMark("biejunDamage-Clear");
			foreach (const Card*h, player->getHandcards()) {
				if (h->hasTip("biejun"))
					return false;
			}
			if (player->hasSkill(objectName()) && player->askForSkillInvoke("biejun$-1", data)) {
				player->turnOver();
	
				DamageStruct damage = data.value<DamageStruct>();
				LogMessage log;
				log.type = damage.from ? "#BiejunPrevent1" : "#BiejunPrevent2";
				log.from = player;
				log.to << damage.from;
				log.arg = QString::number(damage.damage);
				room->sendLog(log);
				return true;
			}
		}else if (triggerEvent == EventAcquireSkill)
			foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
				if (p->getPhase()==Player::Play&&!p->hasSkill("biejun-give",true)&&player->hasSkill(objectName(),true)){
					room->attachSkillToPlayer(p, "biejun-give");
			}
		}else if(player->getPhase()==Player::Play){
			if (triggerEvent == EventPhaseStart) {
				foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
					if (p->hasSkill(objectName(),true)){
						room->attachSkillToPlayer(player, "biejun-give");
						break;
					}
				}
			}else{
				if (player->hasSkill("biejun-give",true))
					room->detachSkillFromPlayer(player, "biejun-give", true);
			}
		}
        return false;
    }
};

class Sangu : public TriggerSkill
{
public:
    Sangu() : TriggerSkill("sangu")
    {
        events << EventPhaseStart << CardUsed << EventPhaseChanging << CardsMoveOneTime << EventPhaseEnd;
        global = true;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {

        if (event == EventPhaseStart) {
			QStringList sangus = player->getTag("SanguCards").toStringList();
            if (player->getPhase() != Player::Play || sangus.isEmpty()) return false;

			LogMessage log;
			log.from = player;
			log.type = "#SanguCard2";
			log.arg3 = "sangu";
			for (int i = 1; i <= sangus.length(); i++) {
                log.arg = QString::number(i);
                log.arg2 = sangus[i-1];
                room->sendLog(log);
            }
            foreach (const Card *h, player->getHandcards()) {
                Card *c = Sanguosha->cloneCard(sangus.first(), h->getSuit(), h->getNumber());
                c->setSkillName("sangu");
                WrappedCard *card = Sanguosha->getWrappedCard(h->getId());
                card->takeOver(c);
                room->notifyUpdateCard(player, h->getId(), card);
            }
        } else if (event == EventPhaseEnd) {
            if (player->getPhase() != Player::Play || !player->hasSkill(objectName())) return false;
			ServerPlayer *t = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@sangu-invoke", true, true);
			if (!t) return false;
			player->peiyin(this);
	
			QStringList names;
			QList<int> cards,ids = Sanguosha->getRandomCards();
			for (int i = 0; i < Sanguosha->getCardCount(); i++) {
				if (!ids.contains(i)) continue;
				const Card *c = Sanguosha->getEngineCard(i);
				if (names.contains(c->objectName())) continue;
				if (c->isKindOf("Slash")) {
					if (names.contains("slash")) continue;
					names << "slash";
					cards << i;
				} else if (c->isNDTrick() && !c->isKindOf("Collateral") && !c->isKindOf("Nullification")) {
					names << c->objectName();
					cards << i;
				}
			}
	
			QStringList choices;
			for (int i = 0; i < 3; i++) {
				if (cards.isEmpty() || player->isDead()) break;
				room->fillAG(cards, player);
				int id = room->askForAG(player, cards, i != 0, objectName(), "@sangu-card");
				room->clearAG(player);
				if (id < 0) break;
				cards.removeOne(id);
	
				choices << Sanguosha->getEngineCard(id)->objectName();
	
				LogMessage log;
				log.type = "#SanguCard";
				log.arg = choices.last();
				log.from = player;
				room->sendLog(log);
			}
	
			if (choices.isEmpty()) return false;
			t->setTag("SanguCards", choices);
	
			foreach (QString name, choices) {
				if (player->getMark("SanguRecord_" + name + "-PlayClear") <= 0) {
					room->loseHp(HpLostStruct(player, 1, objectName(), player));
					break;
				}
			}
        } else if (event == CardUsed) {
            if (player->getPhase() != Player::Play) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SkillCard")) return false;
			QString name = use.card->objectName();
			if (use.card->isKindOf("Slash")) name = "slash";
			player->addMark("SanguRecord_" + name + "-PlayClear");
			QStringList sangus = player->getTag("SanguCards").toStringList();

            if (sangus.isEmpty()) return false;

            name = sangus.first();
            sangus.removeOne(name);
			player->setTag("SanguCards", sangus);

            if (sangus.isEmpty())
                room->filterCards(player, player->getHandcards(), true);
            else {
				foreach (const Card *h, player->getHandcards()) {
                    Card *c = Sanguosha->cloneCard(sangus.first(), h->getSuit(), h->getNumber());
                    c->setSkillName("sangu");
                    WrappedCard *card = Sanguosha->getWrappedCard(h->getId());
                    card->takeOver(c);
                    room->notifyUpdateCard(player, h->getId(), card);
                }
            }
        } else if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().from != Player::Play) return false;
            player->removeTag("SanguCards");
			QList<const Card *>fc;
			foreach (const Card *h, player->getHandcards()) {
				if(h->getSkillName()=="sangu") fc << h;
			}
            room->filterCards(player, fc, true);
        } else if (event == CardsMoveOneTime) {
			QStringList sangus = player->getTag("SanguCards").toStringList();
            if (sangus.isEmpty()||player->getPhase()!=Player::Play) return false;
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to == player && move.to_place == Player::PlaceHand) {
				foreach (const Card *h, player->getHandcards()) {
                    Card *c = Sanguosha->cloneCard(sangus.first(), h->getSuit(), h->getNumber());
                    c->setSkillName("sangu");
                    WrappedCard *card = Sanguosha->getWrappedCard(h->getId());
                    card->takeOver(c);
                    room->notifyUpdateCard(player, h->getId(), card);
                }
            }
        }
        return false;
    }
};

class Yizu : public TriggerSkillV2
{
public:
    Yizu() : TriggerSkillV2("yizu")
    {
        events << EventSkillInvoking << TargetConfirmed; global = true;
        frequency = Compulsory;
    }

    QStringList usableEntries(ServerPlayer *player, QVariant &data) const
    {
        QStringList result;
        if (!player) return result;
        Room *room = player->getRoom();
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            SkillContext candidate;
            candidate.owner = candidate.invoker = candidate.initiator = player;
            candidate.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
            candidate.sourceRef = room->resolveSkillInstanceRootRef(candidate.activationRef);
            candidate.instanceID = id;
            candidate.skill_name = objectName() + "#" + QString::number(id);
            candidate.amount = room->getSkillInstanceAmount(candidate.activationRef);
            candidate.original_data = &data;
            if (candidate.sourceRef.isValid() && isUsable(candidate)) result << candidate.skill_name;
        }
        return result;
    }
    void commitAccepted(Room *room, SkillContext &ctx) const
    {
        if (!ctx.owner || !ctx.activationRef.isValid() || ctx.extra_data.toMap().value("quota_committed").toBool()) return;
        QVariantMap receipt = ctx.extra_data.toMap();
        receipt["quota_committed"] = true;
        ctx.extra_data = receipt;
        addUsage(ctx);

    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; commitAccepted(room, ctx); return true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return true;
        SkillContext accepted = data.value<SkillContext>();
        if (accepted.activationRef.key.skillName == objectName() && TriggerSkillV2::parseSkillName(accepted.skill_name) == objectName()) {
            commitAccepted(room, accepted);
            data = QVariant::fromValue(accepted);
        }
        return true;
    }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetConfirmed) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        return room->hasCurrent() && player && player->isAlive() && player->hasSkill(this)
            && use.from && use.from->isAlive() && player->getHp() <= use.from->getHp()
            && player->isWounded() && use.to.contains(player) && use.card
            && (use.card->isKindOf("Slash") || use.card->isKindOf("Duel"))
            ? TriggerList{{player, usableEntries(player, data)}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        ctx.targets = {ctx.owner};
        return ctx.owner != nullptr;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        // This compulsory trigger consumes only the selected copy's turn quota.
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        RecoverStruct recover(objectName(), ctx.owner);
        recover.recover = getEffectiveAmount(ctx);
        room->recover(target, recover);
        return false;
    }
};

class BushiLK : public TriggerSkillV2
{
public:
    BushiLK() : TriggerSkillV2("bushilk")
    {
        events << EventPhaseStart << CardUsed << CardFinished << PostCardResponded << TargetConfirmed;
        waked_skills = "#bushilk";
    }
    QStringList suits(const SkillContext &ctx) const
    {
        const QStringList defaults{"spade", "heart", "club", "diamond"};
        if (!ctx.owner) return defaults;
        const auto &key = ctx.sourceRef.key;
        const QStringList saved = ctx.owner->getSkillInstanceStateValue(key.skillName, key.instanceID, "suits").toStringList();
        return saved.size() == 4 ? saved : defaults;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(this)) return {};
        if (event == EventPhaseStart)
            return player->getPhase() == Player::Start || player->getPhase() == Player::Finish
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        const Card *card = event == PostCardResponded ? data.value<CardResponseStruct>().m_card : data.value<CardUseStruct>().card;
        if (!card || card->isKindOf("SkillCard")) return {};
        if (event == TargetConfirmed && !data.value<CardUseStruct>().to.contains(player)) return {};
        if ((event == CardUsed || event == CardFinished) && data.value<CardUseStruct>().from != player) return {};
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const QStringList assigned = suits(ctx);
        if (event == EventPhaseStart) {
            if (ctx.owner->getPhase() == Player::Start)
                return ctx.owner->askForSkillInvoke(this, "bushilk", false);
            ctx.choice = assigned[3];
            for (int id : room->getDrawPile())
                if (Sanguosha->getCard(id)->getSuitString() == ctx.choice) return true;
            return false;
        }
        const Card *card = event == PostCardResponded ? ctx.original_data->value<CardResponseStruct>().m_card
            : ctx.original_data->value<CardUseStruct>().card;
        if (!card) return false;
        const int index = event == CardUsed ? 0 : event == TargetConfirmed ? 2 : 1;
        if (card->getSuitString() != assigned[index]) return false;
        if (event != TargetConfirmed) return true;
        if (!ctx.owner->canDiscard(ctx.owner, "h")) return false;
        const Card *selected = room->askForCard(ctx.owner, ".|.|.|hand", "@bushilk:" + card->objectName(),
            *ctx.original_data, Card::MethodNone, nullptr, false, objectName());
        if (!selected || selected->getEffectiveId() < 0) return false;
        ctx.extra_data = selected->getEffectiveId();
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != TargetConfirmed) return true;
        const int id = ctx.extra_data.toInt();
        if (!ctx.owner || room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceHand
            || !ctx.owner->canDiscard(ctx.owner, id)) return false;
        room->throwCard(id, objectName(), ctx.owner);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        if (event == EventPhaseStart && ctx.owner->getPhase() == Player::Start) {
            QStringList available{"heart", "club", "diamond", "spade"};
            const QStringList changes{"cishu", "shiyong", "wuxiao", "huode"};
            QStringList assigned;
            for (const QString &change : changes) {
                QStringList choices;
                for (const QString &suit : available) choices << change + "=" + suit;
                QString suit = room->askForChoice(ctx.owner, objectName(), choices.join("+")).section('=', -1);
                if (!available.contains(suit)) suit = available.first();
                assigned << suit;
                available.removeOne(suit);
            }
            const auto &ref = ctx.sourceRef;
            ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "suits", assigned);
            // Publish only the matching helper's correction; other copies keep their own assignment.
            for (const SkillInstance &instance : ctx.owner->getSkillInstances())
                if (instance.skillName == "#bushilk" && instance.parentRef == ref)
                    ctx.owner->setSkillInstanceCorrectStateValue(instance.skillName, instance.instanceID, "suit", assigned[0]);
            LogMessage log;
            log.type = "#BushiChange";
            log.from = ctx.owner;
            log.arg = objectName();
            log.arg2 = assigned[0]; log.arg3 = assigned[1]; log.arg4 = assigned[2]; log.arg5 = assigned[3];
            room->sendLog(log);
        } else if (event == EventPhaseStart) {
            QList<int> ids;
            for (int id : room->getDrawPile())
                if (Sanguosha->getCard(id)->getSuitString() == ctx.choice) ids << id;
            room->sendCompulsoryTriggerLog(ctx.owner, this);
            QList<int> chosen;
            for (int n = getEffectiveAmount(ctx); n > 0 && !ids.isEmpty(); --n)
                chosen << ids.takeAt(qsanRandomBounded(ids.size()));
            if (!chosen.isEmpty()) {
                DummyCard cards(chosen);
                room->obtainCard(ctx.owner, &cards);
            }
        } else if (event == CardUsed) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            use.m_addHistory = false;
            ctx.original_data->setValue(use);
        } else if (event == TargetConfirmed) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            use.nullified_list << ctx.owner->objectName();
            ctx.original_data->setValue(use);
        } else {
            room->sendCompulsoryTriggerLog(ctx.owner, this);
            ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        }
        return false;
    }
};

class BushiLKTargetMod : public TargetModSkillV2
{
public:
    BushiLKTargetMod() : TargetModSkillV2("#bushilk", ".") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == Residue && ctx.card && ctx.card->getSuitString() == ctx.getStateValue("suit", "spade").toString()
            ? CorrectSkillResult::unlimitedResidue() : CorrectSkillResult::noEffect();
    }
};
class Zhongzhuang : public TriggerSkillV2
{
public:
    Zhongzhuang() : TriggerSkillV2("zhongzhuang")
    {
        events << DamageCaused;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(this) && damage.from == player
            && damage.card && damage.card->isKindOf("Slash") && damage.by_user
            && player->getAttackRange() != 3
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const int amount = getEffectiveAmount(ctx);
        const int attack = ctx.owner->getAttackRange();
        if (attack > 3) {
            if (amount == 0) return false;
            room->sendCompulsoryTriggerLog(ctx.owner, this);
            damage.damage += amount;
            ctx.original_data->setValue(damage);
        } else if (attack < 3) {
            if (damage.damage <= amount) return false;
            room->sendCompulsoryTriggerLog(ctx.owner, this);
            damage.damage = amount;
            ctx.original_data->setValue(damage);
        }
        return false;
    }
};

class Koujing : public TriggerSkill
{
public:
    Koujing() : TriggerSkill("koujing")
    {
        events << Damaged << EventPhaseChanging << EventPhaseStart;
        waked_skills = "#koujing-target";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash") || use.card->getSkillNames().contains("koujing")) return false;
            use.m_addHistory = false;
            data = QVariant::fromValue(use);
        } else if (event == EventPhaseStart) {
			if(player->isAlive()&&player->getPhase()==Player::Play&&!player->isKongcheng()&&player->hasSkill(objectName())){
				const Card *c = room->askForExchange(player, objectName(), 999, 1, false, "@koujing-slash", true);
				if (!c) return false;
		
				LogMessage log;
				log.type = "#InvokeSkill";
				log.from = player;
				log.arg = objectName();
				room->sendLog(log);
				player->peiyin(this);
				room->notifySkillInvoked(player, objectName());
				foreach (int id, c->getSubcards()) {
					room->addPlayerMark(player, "koujing_slash_" + QString::number(id) + "-Clear");
					const Card *cc = Sanguosha->getCard(id);
					Slash *c = new Slash(cc->getSuit(), cc->getNumber());
					c->setSkillName("koujing");
					WrappedCard *card = Sanguosha->getWrappedCard(id);
					card->takeOver(c);
					room->notifyUpdateCard(player, id, card);
				}
			}
        } else if (event == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card||!damage.card->isKindOf("Slash")||!damage.card->getSkillNames().contains("koujing")) return false;

            ServerPlayer *user = room->getCardUser(damage.card);
            if (!user || user->isDead()) return false;

            QList<int> shows;
            foreach (int id, user->handCards()) {
                if (user->getMark("koujing_slash_" + QString::number(id) + "-Clear") > 0)
                    shows << id;
            }
            if (shows.isEmpty()) return false;
            room->sendCompulsoryTriggerLog(user, "koujing", true, true);
            room->showCard(user, shows);

            if (player->isDead() || player->isKongcheng()) return false;
            room->fillAG(shows, player);
            player->setTag("KoujingShowCards", ListI2V(shows));
            bool invoke = player->askForSkillInvoke("koujing", "koujing", false);
            room->clearAG(player);
            if (!invoke) return false;

            QList<CardsMoveStruct> exchangeMove;
            CardsMoveStruct move1(player->handCards(), user, Player::PlaceHand,
                CardMoveReason(CardMoveReason::S_REASON_SWAP, player->objectName(), user->objectName(), "koujing", ""));
            CardsMoveStruct move2(shows, player, Player::PlaceHand,
                CardMoveReason(CardMoveReason::S_REASON_SWAP, user->objectName(), player->objectName(), "koujing", ""));
            exchangeMove.push_back(move1);
            exchangeMove.push_back(move2);
            room->moveCardsAtomic(exchangeMove, false);
        } else if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
            QList<const Card *> shows;
            foreach (const Card *c, player->getCards("he")) {
				if(c->getSkillName()=="koujing")
					shows << c;
			}
            room->filterCards(player, shows, true);
        } else if (event == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from == player && move.from_places.contains(Player::PlaceHand)) {
                for (int i = 0; i < move.card_ids.length(); i++) {
                    if (move.from_places.at(i) == Player::PlaceHand) {
                        int id = move.card_ids.at(i);
                        room->setPlayerMark(player, "koujing_slash_" + QString::number(id) + "-Clear", 0);
                    }
                }
            }
        }
        return false;
    }
};

class KoujingTargetMod : public TargetModSkill
{
public:
    KoujingTargetMod() : TargetModSkill("#koujing-target")
    {
        frequency = NotFrequent;
    }

    int getResidueNum(const Player *from, const Card *card, const Player *) const
    {
        if (card->getSkillName() == "koujing")
            return 1000;
		if(card->isKindOf("Slash")&&from->getMark("diezhang")<1&&from->hasSkill("diezhang"))
			return 1;
        return 0;
    }
};

class Diezhang : public TriggerSkill
{
public:
    Diezhang() : TriggerSkill("diezhang")
    {
        events << CardOffset;
		change_skill = true;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }

    void diezhangEffect(ServerPlayer *player, ServerPlayer *to, int n, int x) const
    {
        Room *room = player->getRoom();
		if(n==1){
			if(player->canDiscard(player,"he")
			&&room->askForCard(player,"..","diezhang0:1:"+to->objectName(),QVariant(),objectName()+"%-1")){
				if(x==1) room->setChangeSkillState(player, objectName(), 2);
				else player->addMark("diezhangUse-Clear");
				Card*dc = Sanguosha->cloneCard("slash");
				dc->setSkillName("_diezhang");
				for (int i = 0; i < x; i++) {
					if(player->canSlash(to,dc,false))
						room->useCard(CardUseStruct(dc,player,to));
				}
				dc->deleteLater();
			}
		}else{
			if(player->askForSkillInvoke(this,to)){
				if(x==1) room->setChangeSkillState(player, objectName(), 2);
				else player->addMark("diezhangUse-Clear");
				player->peiyin(this);
				player->drawCards(x,objectName());
				Card*dc = Sanguosha->cloneCard("slash");
				dc->setSkillName("_diezhang");
				if(player->canSlash(to,dc,false))
					room->useCard(CardUseStruct(dc,player,to));
				dc->deleteLater();
			}
		}
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardOffset) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
            CardUseStruct use = room->getUseStruct(effect.offset_card);
            if (use.from!=player&&use.from->isAlive()&&player->hasTurn()){
				if(player->hasSkill(objectName())){
					if(player->getMark("diezhang")<1){
						if(player->getChangeSkillState(objectName()) == 1)
							diezhangEffect(player,use.from,1,1);
					}else if(player->getMark("diezhangUse-Clear")<1){
						if(player->getChangeSkillState(objectName()) == 2)
							diezhangEffect(player,use.from,1,2);
					}
				}
				if(use.from->hasSkill(objectName())){
					if(use.from->getMark("diezhang")<1){
						if(use.from->getChangeSkillState(objectName()) == 2)
							diezhangEffect(use.from,player,2,1);
					}else if(use.from->getMark("diezhangUse-Clear")<1){
						if(use.from->getChangeSkillState(objectName()) == 1)
							diezhangEffect(use.from,player,2,2);
					}
				}
			}
        }
        return false;
    }
};

class Duanwan : public TriggerSkillV2
{
public:
    Duanwan() : TriggerSkillV2("duanwan")
    {
        events << EventSkillInvoking << AskForPeaches;
        frequency = Limited;
        limit_mark = "@duanwan";
        global = true;
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    QStringList usableEntries(ServerPlayer *player, QVariant &data) const
    {
        QStringList result;
        if (!player) return result;
        Room *room = player->getRoom();
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            SkillContext candidate;
            candidate.owner = candidate.invoker = candidate.initiator = player;
            candidate.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
            candidate.sourceRef = room->resolveSkillInstanceRootRef(candidate.activationRef);
            candidate.instanceID = id;
            candidate.skill_name = objectName() + "#" + QString::number(id);
            candidate.amount = room->getSkillInstanceAmount(candidate.activationRef);
            candidate.original_data = &data;
            if (candidate.sourceRef.isValid() && isUsable(candidate)) result << candidate.skill_name;
        }
        return result;
    }
    void commitAccepted(Room *room, SkillContext &ctx) const
    {
        if (!ctx.owner || !ctx.activationRef.isValid() || ctx.extra_data.toMap().value("quota_committed").toBool()) return;
        QVariantMap receipt = ctx.extra_data.toMap();
        receipt.insert("quota_committed", true);
        ctx.extra_data = receipt;
        addUsage(ctx);
        if (ctx.owner->getMark(limit_mark) > 0) room->removePlayerMark(ctx.owner, limit_mark);
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        commitAccepted(room, ctx);
        return true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return true;
        SkillContext accepted = data.value<SkillContext>();
        if (accepted.activationRef.key.skillName == objectName() && TriggerSkillV2::parseSkillName(accepted.skill_name) == objectName()) {
            commitAccepted(room, accepted);
            data = QVariant::fromValue(accepted);
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != AskForPeaches) return {};
        const DyingStruct dying = data.value<DyingStruct>();
        return player && player == dying.who && player->isAlive() && player->getHp() <= 0 && player->hasSkill(this)
            ? TriggerList{{player, usableEntries(player, data)}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || !ctx.owner || !ctx.original_data) return false;
        if (!room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox(ctx.owner, objectName());
        int n = qMin(2, ctx.owner->getMaxHp()) - ctx.owner->getHp();
        room->recover(ctx.owner, RecoverStruct(ctx.owner, nullptr, n, objectName()));
        n = ctx.owner->getChangeSkillState("diezhang");
        room->addPlayerMark(ctx.owner, "diezhang");
        n += 2;
        room->changeTranslation(ctx.owner, "diezhang", n);
        for (const QString &mark : ctx.owner->getMarkNames()) {
            if (mark.contains("&diezhang+")) room->setPlayerMark(ctx.owner, mark, 0);
        }
        return false;
    }
};

class Duwang : public TriggerSkillV2
{
public:
    Duwang() : TriggerSkillV2("duwang")
    {
        events << GameStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(this)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner != nullptr; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        const int count = 5 * getEffectiveAmount(ctx);
        if (count <= 0) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        DummyCard cards;
        for (int id : room->getDrawPile()) {
            if (!Sanguosha->getCard(id)->isKindOf("Slash")) {
                cards.addSubcard(id);
                if (cards.subcardsLength() >= count) break;
            }
        }
        // These physical pile cards are applied effects and survive removal of the grant.
        if (!cards.getSubcards().isEmpty()) target->addToPile("dw_ci", &cards);
        return false;
    }
};

class DuwangBf : public DistanceSkillV2
{
public:
    DuwangBf() : DistanceSkillV2("#duwangbf")
    {
        setHolderSelector(CorrectSkill_Participants);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        // Each endpoint contributes its own instance amount to the ordinary distance.
        return ctx.holder && !ctx.holder->getPile("dw_ci").isEmpty()
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

class Cibei : public TriggerSkillV2
{
public:
    Cibei() : TriggerSkillV2("cibei") { events << CardFinished << EventPhaseChanging; waked_skills = "#cibei_limit,#cibei_mod"; }
    QList<int> nonSlashSpines(ServerPlayer *player) const
    {
        QList<int> result;
        for (int id : player->getPile("dw_ci")) if (!Sanguosha->getCard(id)->isKindOf("Slash")) result << id;
        return result;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (event == CardFinished) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || !use.card->isKindOf("Slash") || use.card->getEffectiveId() < 0
                || room->getCardOwner(use.card->getEffectiveId())) return result;
            if (use.targetModReveal.useHistoryEventId <= 0) return result;
            const QVariantMap damage = room->queryCardUseDamage(use.targetModReveal.useHistoryEventId);
            if (!damage.value("complete").toBool() || !damage.value("attribution_complete").toBool()
                || damage.value("items").toList().isEmpty()) return result;
        } else if (data.value<PhaseChangeStruct>().to != Player::NotActive) return result;
        for (ServerPlayer *holder : room->getAlivePlayers()) {
            if (!holder->hasSkill(this) || holder->getPile("dw_ci").isEmpty()) continue;
            const bool hasOther = !nonSlashSpines(holder).isEmpty();
            if (event == CardFinished ? hasOther : !hasOther) result[holder] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        if (event != CardFinished) return !ctx.owner->getPile("dw_ci").isEmpty() && nonSlashSpines(ctx.owner).isEmpty();
        const Card *used = ctx.original_data->value<CardUseStruct>().card;
        if (!used || used->getEffectiveId() < 0 || room->getCardOwner(used->getEffectiveId())) return false;
        const QList<int> choices = nonSlashSpines(ctx.owner);
        if (choices.isEmpty() || !ctx.owner->askForSkillInvoke(this, QVariant(), false)) return false;
        room->fillAG(choices, ctx.owner);
        const auto clear = qScopeGuard([&] { room->clearAG(ctx.owner); });
        int id = room->askForAG(ctx.owner, choices, false, objectName());
        if (!choices.contains(id)) id = choices.first();
        ctx.extra_data = QVariantMap{{"payment", id}, {"slash", used->getEffectiveId()}};
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != CardFinished) return true;
        const int id = ctx.extra_data.toMap().value("payment", -1).toInt();
        if (!ctx.owner || !ctx.owner->getPile("dw_ci").contains(id) || Sanguosha->getCard(id)->isKindOf("Slash")) return false;
        room->throwCard(id, objectName(), nullptr);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        if (event == CardFinished) {
            const int id = ctx.extra_data.toMap().value("slash", -1).toInt();
            if (id < 0 || room->getCardOwner(id)
                || (room->getCardPlace(id) != Player::PlaceTable && room->getCardPlace(id) != Player::DiscardPile)) return false;
            ctx.owner->addToPile("dw_ci", id);
            if (!ctx.owner->isAlive()) return false;
            QList<ServerPlayer *> candidates;
            for (ServerPlayer *target : room->getAlivePlayers())
                if (ctx.owner->canDiscard(target, "hej")) candidates << target;
            if (!candidates.isEmpty()) {
                ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "cibei0:", false, false);
                if (target) skillEffect(event, room, ctx.owner, ctx, target);
            }
        } else {
            const QList<int> ids = ctx.owner->getPile("dw_ci");
            if (ids.isEmpty() || !nonSlashSpines(ctx.owner).isEmpty()) return false;
            ctx.choice = "collect";
            ctx.extra_data = ListI2V(ids);
            skillEffect(event, room, ctx.owner, ctx, ctx.owner);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner || !target) return false;
        if (ctx.choice == "collect") {
            QList<int> ids;
            for (const QVariant &entry : ctx.extra_data.toList())
                if (ctx.owner->getPile("dw_ci").contains(entry.toInt())) ids << entry.toInt();
            if (ids.isEmpty()) return false;
            QStringList protectedIds = target->property("cibeiIds").toString().split(',', Qt::SkipEmptyParts);
            QVariantMap receipts = target->getTag("CibeiCards").toMap();
            for (int id : ids) {
                const QString key = QString::number(id);
                if (!protectedIds.contains(key)) protectedIds << key;
                receipts[key] = QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
                    {"instance", ctx.sourceRef.key.instanceID}};
            }
            // These permanent physical-card benefits survive loss of the granting instance.
            target->setTag("CibeiCards", receipts);
            room->setPlayerProperty(target, "cibeiIds", protectedIds.join(','));
            DummyCard cards(ids);
            room->obtainCard(target, &cards);
            return false;
        }
        for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive() && ctx.owner->canDiscard(target, "hej"); ++i) {
            const int id = room->askForCardChosen(ctx.owner, target, "hej", objectName(), false, Card::MethodDiscard);
            if (id < 0) break;
            room->throwCard(id, objectName(), target, ctx.owner);
        }
        return false;
    }
};

// CardLimitSkill has no V2 counterpart; this predicate describes retained physical-card effects.
class CibeiLimit : public CardLimitSkill
{
public:
    CibeiLimit() : CardLimitSkill("#cibei_limit") {}
    QString limitList(const Player *) const override { return "discard,ignore"; }
    QString limitPattern(const Player *target) const override { return target->property("cibeiIds").toString(); }
};

class CibeiMod : public TargetModSkillV2
{
public:
    CibeiMod() : TargetModSkillV2("#cibei_mod") { setHolderSelector(CorrectSkill_System); setBaseAmount(1000); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.card || !ctx.card->isKindOf("Slash") || ctx.card->getEffectiveId() < 0
            || !ctx.primary->property("cibeiIds").toString().split(',').contains(QString::number(ctx.card->getEffectiveId())))
            return CorrectSkillResult::noEffect();
        if (ctx.modType == Residue) return CorrectSkillResult::unlimitedResidue();
        return ctx.modType == DistanceLimit ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};
ShujianCard::ShujianCard()
{
    setSkillName("shujian");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool ShujianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void ShujianCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *from = effect.from, *to = effect.to;
    Room *room = from->getRoom();
    room->giveCard(from, to, this, getSkillName());
	int x = 3, n = from->getMark("shujianNum-PlayClear");
	x -= n;
	Card*dc = Sanguosha->cloneCard("dismantlement");
	dc->setSkillName("_shujian");
	QString p = "shujian1="+QString::number(x)+"+shujian2="+QString::number(x);
	if(to->canUse(dc)&&room->askForChoice(to,getSkillName(),p,QVariant::fromValue(from)).contains("shujian2")){
		for (int i = 0; i < x; i++) {
			room->askForUseCard(to,"@@shujian","shujian0:");
		}
		room->setPlayerMark(from,"shujianBan-PlayClear",1);
	}else{
		from->drawCards(x,getSkillName());
		x--;
		if(x>0)
			room->askForDiscard(from,getSkillName(),x,x,false,true);
	}
	dc->deleteLater();
	foreach (QString m, from->getMarkNames()) {
		if(m.contains("&shujian+-"))
			room->setPlayerMark(from,m,0);
	}
	n++;
	from->addMark("shujianNum-PlayClear");
	room->setPlayerMark(from,"&shujian+-"+QString::number(n)+"-PlayClear",1);
}

class Shujian : public ViewAsSkillV2
{
public:
    Shujian() : ViewAsSkillV2("shujian", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool isResponse(const Player *player, const SkillInstanceRef &ref) const
    {
        return player && player->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "response", false).toBool();
    }
    QVariantMap usageState(const SkillContext &ctx) const
    {
        if (!ctx.initiator) return {};
        return ctx.initiator->getSkillInstanceState(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.initiator) return false;
        if (isResponse(ctx.initiator, ctx.activationRef)) return true;
        const QVariantMap state = usageState(ctx), scopes = ctx.initiator->getRoom()->historyScopes();
        const QString phase = scopes.value("phase_id").toString(), turn = scopes.value("turn_id").toString();
        if (phase.isEmpty() || phase == "0" || turn.isEmpty() || turn == "0") return false;
        if (state.value("blocked_turn").toString() == turn) return false;
        return state.value("phase").toString() != phase || state.value("used").toInt() < 3;
    }
    void addUsage(const SkillContext &ctx) const override
    {
        if (!ctx.initiator || isResponse(ctx.initiator, ctx.activationRef)) return;
        QVariantMap state = usageState(ctx);
        const QString phase = ctx.initiator->getRoom()->historyScopes().value("phase_id").toString();
        const int used = state.value("phase").toString() == phase ? state.value("used").toInt() : 0;
        state["phase"] = phase;
        state["used"] = used + 1;
        ctx.initiator->setSkillInstanceState(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, state);
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        const bool response = isResponse(request.initiator, request.activationRef);
        return response ? request.reason != CardUseStruct::CARD_USE_REASON_PLAY && request.pattern == "@@shujian"
            : request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator && card && !card->hasFlag("using") && request.selectedCardIds.isEmpty()
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId()));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? request.selectedCardIds.size() == 1
            : request.pattern == "@@shujian" && request.selectedCardIds.isEmpty();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return ViewAsSkillV2::createCard(request);
        Card *card = Sanguosha->cloneCard("dismantlement");
        if (card) card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    { return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? "ShujianCard" : "Dismantlement"; }
    bool willThrowSelectedCards() const override { return false; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return candidate && candidate->isAlive() && candidate != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return true;
        const QVariantMap state = usageState(ctx);
        const QString phase = ctx.initiator->getRoom()->historyScopes().value("phase_id").toString();
        const int remaining = 3 - (state.value("phase").toString() == phase ? state.value("used").toInt() : 0);
        ctx.extra_data = QVariantMap{{"remaining", remaining}, {"committed", false}};
        return remaining > 0;
    }
    void commitAccepted(SkillContext &ctx) const
    {
        if (!ctx.initiator || isResponse(ctx.initiator, ctx.activationRef)) return;
        QVariantMap receipt = ctx.extra_data.toMap();
        if (receipt.value("committed").toBool()) return;
        if (!receipt.contains("remaining")) {
            const QVariantMap state = usageState(ctx);
            const QString phase = ctx.initiator->getRoom()->historyScopes().value("phase_id").toString();
            receipt["remaining"] = 3 - (state.value("phase").toString() == phase ? state.value("used").toInt() : 0);
        }
        receipt["committed"] = true;
        ctx.extra_data = receipt;
        addUsage(ctx);
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
            if (request.selectedCardIds.size() != 1) return false;
            const int id = request.selectedCardIds.first();
            ActiveSkillRequest selection = request;
            selection.selectedCardIds.clear();
            // Cost and invoking callbacks may have moved the promised material.
            if (room->getCardOwner(id) != ctx.initiator || !canSelectCard(selection, Sanguosha->getCard(id))) return false;
            commitAccepted(ctx);
        }
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.extra_data.toMap().value("operation").toString() == "draw") {
            if (!target || !target->isAlive()) return ContinueEffects;
            const int remaining = ctx.extra_data.toMap().value("remaining").toInt(), amount = getEffectiveAmount(ctx);
            target->drawCards(remaining * amount, objectName());
            const int discard = qMax(0, remaining - 1) * amount;
            if (discard > 0 && target->isAlive()) target->getRoom()->askForDiscard(target, objectName(), discard, discard, false, true);
            return ContinueEffects;
        }
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !ctx.initiator || !ctx.use_card || ctx.use_card->subcardsLength() != 1) return ContinueEffects;
        Room *room = source->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) != ctx.initiator || Sanguosha->getCard(id)->hasFlag("using")
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return ContinueEffects;
        Room::AcceptedViewAsEffectScope prompt(room, target, objectName(), ctx);
        const SkillInstanceRef ref = prompt.activationRef();
        if (!prompt.isValid()) return ContinueEffects;
        target->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "response", true);
        room->giveCard(ctx.initiator, target, Sanguosha->getCard(id), objectName());
        if (!target->isAlive()) return ContinueEffects;
        const int remaining = ctx.extra_data.toMap().value("remaining").toInt(), amount = getEffectiveAmount(ctx);
        const int count = remaining * amount;
        Dismantlement preview(Card::NoSuit, 0);
        const QString choices = "shujian1=" + QString::number(count) + "+shujian2=" + QString::number(count);
        if (target->canUse(&preview) && room->askForChoice(target, objectName(), choices, QVariant::fromValue(source)).startsWith("shujian2")) {
            // This is a turn ban, not merely the remainder of this Play phase.
            ctx.initiator->setSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID,
                "blocked_turn", room->historyScopes().value("turn_id"));
            target->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "response", true);
            RoomState *state = Sanguosha->currentRoomState();
            const auto reason = state->getCurrentCardUseReason();
            const QString pattern = state->getCurrentCardUsePattern();
            const auto requestGuard = qScopeGuard([=] {
                state->setCurrentCardUseReason(reason);
                state->setCurrentCardUsePattern(pattern);
            });
            for (int i = 0; i < count && target->isAlive(); ++i)
                room->askForUseCard(target, "@@shujian", "shujian0:");
        } else if (source->isAlive()) {
            SkillContext reward = ctx;
            QVariantMap operation = ctx.extra_data.toMap();
            operation["operation"] = "draw";
            reward.extra_data = operation;
            skillEffect(reward, source);
        }
        return ContinueEffects;
    }
};
class ShujianRecord : public TriggerSkillV2
{
public:
    ShujianRecord() : TriggerSkillV2("#shujian-record") { events << EventSkillInvoking; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        SkillContext active = data.value<SkillContext>();
        if (active.activationRef.key.skillName != "shujian" || !active.use_card || active.use_card->getTypeId() != Card::TypeSkill) return true;
        const auto *skill = dynamic_cast<const Shujian *>(Sanguosha->getViewAsSkill("shujian"));
        if (skill) skill->commitAccepted(active);
        data = QVariant::fromValue(active);
        return true;
    }
};
YJCM2022Package::YJCM2022Package()
    : Package("YJCM2022")
{
    General *liwan = new General(this, "liwan", "wei", 3, false);
    liwan->addSkill(new Liandui);
    liwan->addSkill(new Biejun);


    General *zhugeshang = new General(this, "zhugeshang", "shu", 3);
    zhugeshang->addSkill(new Sangu);
    zhugeshang->addSkill(new Yizu);

    General *lukai = new General(this, "lukai", "wu", 4);
    lukai->addSkill(new BushiLK);
    lukai->addSkill(new BushiLKTargetMod);
    related_skills.insert("bushilk", "#bushilk");
    lukai->addSkill(new Zhongzhuang);

    General *kebineng = new General(this, "kebineng", "qun", 4);
    kebineng->addSkill(new Koujing);
    kebineng->addSkill(new KoujingTargetMod);

    General *wuanguo = new General(this, "wuanguo", "qun", 4);
    wuanguo->addSkill(new Diezhang);
    wuanguo->addSkill(new Duanwan);

    General *hanlong = new General(this, "hanlong", "wei", 4);
    hanlong->addSkill(new Duwang);
    hanlong->addSkill(new DuwangBf);
    related_skills.insert("duwang", "#duwangbf");
    hanlong->addSkill(new Cibei);
    hanlong->addSkill(new CibeiLimit);
    hanlong->addSkill(new CibeiMod);

    General *th_sufei = new General(this, "th_sufei", "wu", 4);
    th_sufei->addSkill(new Shujian);
    th_sufei->addSkill(new ShujianRecord);
    related_skills.insert("shujian", "#shujian-record");
    addMetaObject<ShujianCard>();

    addMetaObject<BiejunCard>();

    skills << new BiejunGive;
}

ADD_PACKAGE(YJCM2022)
