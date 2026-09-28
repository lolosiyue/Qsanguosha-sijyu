#include "yjcm2013.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include <QScopeGuard>

Chengxiang::Chengxiang() : TriggerSkillV2("chengxiang")
{
    events << Damaged;
    frequency = Frequent;
    total_point = 13;
}

TriggerList Chengxiang::triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const
{
    return player && player->isAlive() && player->hasSkill(this) ? TriggerList{{player, {objectName()}}} : TriggerList();
}

bool Chengxiang::cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    if (!ctx.owner || !ctx.original_data || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
    ctx.targets = {ctx.owner};
    return true;
}

bool Chengxiang::effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const
{
    if (!target || !target->isAlive()) return false;
    const QList<int> cards = room->getNCards(4 * getEffectiveAmount(ctx));
    if (cards.isEmpty()) return false;
    // Unwinding an AG request must never strand IDs removed from the draw pile.
    const auto cleanup = qScopeGuard([&] {
        room->clearAG();
        QList<int> remaining;
        for (int id : cards)
            if (!room->getCardOwner(id) && room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) remaining << id;
        if (!remaining.isEmpty()) room->returnToTopDrawPile(remaining);
    });
    QMap<int, int> points;
    for (int id : cards) points[id] = Sanguosha->getCard(id)->getNumber();
    room->fillAG(cards);
    QList<int> available = cards, chosen;
    int sum = 0;
    while (target->isAlive() && !available.isEmpty()) {
        for (int id : QList<int>(available)) {
            if (sum + points.value(id) > total_point) {
                room->takeAG(nullptr, id, false);
                available.removeOne(id);
            }
        }
        if (available.isEmpty()) break;
        const int id = room->askForAG(target, available, !chosen.isEmpty(), objectName());
        if (!available.contains(id)) break;
        chosen << id;
        available.removeOne(id);
        sum += points.value(id);
        room->takeAG(target, id, false);
    }
    room->clearAG();
    QList<int> obtain, discard;
    for (int id : cards) {
        if (room->getCardOwner(id) || room->getCardPlace(id) != Player::DrawPile || room->getDrawPile().contains(id)) continue;
        if (chosen.contains(id) && target->isAlive()) obtain << id;
        else discard << id;
    }
    if (!obtain.isEmpty()) {
        DummyCard selected(obtain);
        room->obtainCard(target, &selected);
    }
    // Obtaining selected cards can move another revealed card through nested callbacks.
    for (int id : QList<int>(discard))
        if (room->getCardOwner(id) || room->getCardPlace(id) != Player::DrawPile || room->getDrawPile().contains(id)) discard.removeOne(id);
    if (!discard.isEmpty()) {
        DummyCard rejected(discard);
        const CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, target->objectName(), objectName(), "");
        room->throwCard(&rejected, reason, nullptr);
    }
    return false;
}
class Renxin : public TriggerSkillV2
{
public:
    Renxin() : TriggerSkillV2("renxin") { events << DamageInflicted; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        TriggerList list;
        if (!damage.to || !damage.to->isAlive() || damage.to->getHp() != 1) return list;
        for (ServerPlayer *holder : room->getOtherPlayers(damage.to))
            if (holder->isAlive() && holder->hasSkill(this) && holder->canDiscard(holder, "he")) list[holder] << objectName();
        return list;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.to || !damage.to->isAlive() || damage.to->getHp() != 1) return false;
        const Card *card = room->askForCard(ctx.owner, ".Equip", "@renxin-card:" + damage.to->objectName(),
            *ctx.original_data, Card::MethodNone, nullptr, false, objectName());
        if (!card) return false;
        ctx.extra_data = card->getEffectiveId();
        ctx.targets = {damage.to};
        ctx.manual_effect = true;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (!ctx.owner || room->getCardOwner(id) != ctx.owner || !ctx.owner->canDiscard(ctx.owner, id)
            || Sanguosha->getCard(id)->hasFlag("using") || !Sanguosha->getCard(id)->isKindOf("EquipCard")
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        room->throwCard(id, objectName(), ctx.owner);
        // Turning over is part of the payment, even when prevention is intercepted.
        ctx.owner->turnOver();
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.extra_data = false;
        if (!ctx.targets.isEmpty()) skillEffect(event, room, player, ctx, ctx.targets.first());
        return ctx.extra_data.toBool();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        LogMessage log;
        log.type = "#Renxin";
        log.from = target;
        log.arg = objectName();
        room->sendLog(log);
        ctx.extra_data = true;
        return false;
    }
};
class Jingce : public TriggerSkillV2
{
public:
    Jingce() : TriggerSkillV2("jingce")
    {
        events << EventPhaseEnd;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(this)
            || player->getPhase() != Player::Play) return {};
        const int count = room->countHistoryCards(player, "turn");
        // Unknown history (-1) cannot establish the use-count prerequisite.
        return count >= 0 && count >= player->getHp() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !room->askForSkillInvoke(ctx.owner, objectName(), QVariant(), false)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        if (target->isAlive() && getEffectiveAmount(ctx) > 0) target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

JunxingCard::JunxingCard()
{
    setSkillName("junxing");
}

void JunxingCard::use(Room *room, ServerPlayer *, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.first();
    if (!target->isAlive()) return;

    QString type_name[4] = { "", "BasicCard", "TrickCard", "EquipCard" };
    QStringList types;
    types << "BasicCard" << "TrickCard" << "EquipCard";
    foreach (int id, subcards) {
        const Card *c = Sanguosha->getCard(id);
        types.removeOne(type_name[c->getTypeId()]);
        if (types.isEmpty()) break;
    }
    if (!target->canDiscard(target, "h") || types.isEmpty()
        || !room->askForCard(target, types.join(",") + "|.|.|hand", "@junxing-discard")) {
        target->turnOver();
        target->drawCards(subcards.length(), "junxing");
    }
}

class Junxing : public ViewAsSkillV2
{
public:
    Junxing() : ViewAsSkillV2("junxing")
    {
        setPhaseName("Play");
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && !request.selectedCardIds.contains(candidate->getEffectiveId())
            && request.initiator->handCards().contains(candidate->getEffectiveId()) && !request.initiator->isJilei(candidate);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return !request.selectedCardIds.isEmpty();
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "h");
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "JunxingCard"; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return candidate && candidate->isAlive() && candidate != request.initiator && selected.isEmpty();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.size() == 1;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card = ViewAsSkillV2::createCard(request);
        QStringList types{"BasicCard", "TrickCard", "EquipCard"};
        const QStringList names{"", "BasicCard", "TrickCard", "EquipCard"};
        for (int id : request.selectedCardIds) {
            const int type = Sanguosha->getCard(id)->getTypeId();
            if (type >= 0 && type < names.size()) types.removeAll(names[type]);
        }
        if (card) card->setTag("JunxingMaterials", QVariantMap{{"types", types}, {"count", request.selectedCardIds.size()}});
        return card;
    }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        QStringList types{"BasicCard", "TrickCard", "EquipCard"};
        const QStringList names{"", "BasicCard", "TrickCard", "EquipCard"};
        for (int id : request.selectedCardIds) {
            const Card *card = Sanguosha->getCard(id);
            if (!card) return false;
            const int type = card->getTypeId();
            if (type >= 0 && type < names.size()) types.removeAll(names[type]);
        }
        // Snapshot material types before pay moves and refilters their wrapped faces.
        ctx.extra_data = QVariantMap{{"types", types}, {"count", request.selectedCardIds.size()}};
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        if (!ctx.extra_data.isValid() && ctx.use_card) ctx.extra_data = ctx.use_card->getTag("JunxingMaterials");
        const QVariantMap paid = ctx.extra_data.toMap();
        const QStringList types = paid.value("types").toStringList();
        if (!target->canDiscard(target, "h") || types.isEmpty()
            || !target->getRoom()->askForCard(target, types.join(",") + "|.|.|hand", "@junxing-discard")) {
            target->turnOver();
            target->drawCards(paid.value("count").toInt() * getEffectiveAmount(ctx), objectName());
        }
        return ContinueEffects;
    }
};

class Yuce : public TriggerSkillV2
{
public:
    Yuce() : TriggerSkillV2("yuce") { events << Damaged; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(this) && !player->isKongcheng() ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const Card *card = room->askForCard(ctx.owner, ".|.|.|hand", "@yuce-show", *ctx.original_data, Card::MethodNone);
        if (!card || room->getCardOwner(card->getEffectiveId()) != ctx.owner || room->getCardPlace(card->getEffectiveId()) != Player::PlaceHand) return false;
        // Freeze the displayed category before show-card callbacks can refilter it.
        ctx.extra_data = card->getTypeId();
        room->showCard(ctx.owner, card->getEffectiveId());
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.from && damage.from->isAlive()) ctx.targets = {damage.from};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (ctx.choice == "recover") {
            RecoverStruct recover(objectName(), ctx.owner);
            recover.recover = getEffectiveAmount(ctx);
            room->recover(target, recover);
            return false;
        }
        QStringList types{"BasicCard", "TrickCard", "EquipCard"};
        const int type = ctx.extra_data.toInt();
        if (type >= Card::TypeBasic && type <= Card::TypeEquip) types.removeAt(type - Card::TypeBasic);
        const QString prompt = QString("@yuce-discard:%1::%2:%3").arg(ctx.owner->objectName()).arg(types.first()).arg(types.last());
        if (!target->canDiscard(target, "h") || !room->askForCard(target, types.join(',') + "|.|.|hand", prompt, *ctx.original_data)) {
            SkillContext recovery = ctx;
            recovery.choice = "recover";
            skillEffect(event, room, player, recovery, ctx.owner);
        }
        return false;
    }
};
class Longyin : public TriggerSkill
{
public:
    Longyin() : TriggerSkill("longyin")
    {
        events << CardUsed;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->getPhase() == Player::Play;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Slash")) {
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->isDead() || !p->hasSkill(objectName()) || !p->canDiscard(p, "he")) continue;
				if (p->hasAcquiredSkill(objectName())){
					QString ww = p->property("manweiwoFrom").toString();
					if(!ww.isEmpty()&&use.from->objectName()!=ww) continue;
				}
				if (!room->askForCard(p, "..", "@longyin", data, objectName())) continue;
                room->broadcastSkillInvoke(objectName(), use.card->isRed() ? 2 : 1);
                use.m_addHistory = false;
                data = QVariant::fromValue(use);
                if (use.card->isRed())
                    p->drawCards(1, objectName());
            }
        }
        return false;
    }
};

ExtraCollateralCard::ExtraCollateralCard()
{
}

bool ExtraCollateralCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    QStringList tos = Self->property("extra_collateral").toString().split("+");
    const Card *coll = Card::Parse(tos.first());
    if (!coll||targets.length()>1||tos.contains(to_select->objectName())) return false;
    int n = 0;
    return coll->targetFilter(targets, to_select, Self, n) || n>0;
}

bool ExtraCollateralCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length()>1;
}

void ExtraCollateralCard::onUse(Room *room, CardUseStruct &use) const
{
    ServerPlayer *killer = use.to.first(), *victim = use.to.last();

    QStringList tos = use.from->property("extra_collateral").toString().split("+");
    use.from->setTag("ExtraCollateralTarget", QVariant::fromValue(killer));
    killer->setTag("attachTarget", QVariant::fromValue(victim));

	LogMessage log;
	log.type = "#QiaoshuiAdd";
	log.from = use.from;
	log.to << killer;
	log.card_str = tos.first();
	log.arg = tos.last();
	room->sendLog(log);
}

class ExtraCollateral : public ZeroCardViewAsSkill
{
public:
    ExtraCollateral() : ZeroCardViewAsSkill("extraCollateral")
    {
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern.startsWith("@@extra_collateral");
    }

    const Card *viewAs() const
    {
        return new ExtraCollateralCard;
    }
};

QiaoshuiCard::QiaoshuiCard()
{
}

bool QiaoshuiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void QiaoshuiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    if (!source->canPindian(targets.first(), false)) return;
    if (source->pindian(targets.first(), "qiaoshui"))
        source->setFlags("QiaoshuiSuccess");
    else
        room->setPlayerCardLimitation(source, "use", "TrickCard", true);
}

class QiaoshuiViewAsSkill : public ZeroCardViewAsSkill
{
public:
    QiaoshuiViewAsSkill() : ZeroCardViewAsSkill("qiaoshui")
    {
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern.startsWith("@@qiaoshui");
    }

    const Card *viewAs() const
    {
        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern.endsWith("!"))
            return new ExtraCollateralCard;
        return new QiaoshuiCard;
    }
};

class Qiaoshui : public TriggerSkill
{
public:
    Qiaoshui() : TriggerSkill("qiaoshui")
    {
        events << PreCardUsed << EventPhaseStart;
        view_as_skill = new QiaoshuiViewAsSkill;
    }
    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->getPhase() == Player::Play;
    }
    bool trigger(TriggerEvent event, Room *room, ServerPlayer *jianyong, QVariant &data) const
    {
        if(event==EventPhaseStart){
			if (jianyong->hasSkill(objectName())) {
				foreach (ServerPlayer *p, room->getOtherPlayers(jianyong)) {
					if (jianyong->canPindian(p)) {
						room->askForUseCard(jianyong, "@@qiaoshui", "@qiaoshui-card", 1);
						break;
					}
				}
			}
		}else{
			if (!jianyong->hasFlag("QiaoshuiSuccess")) return false;
			CardUseStruct use = data.value<CardUseStruct>();
			if (use.card->isNDTrick() || use.card->isKindOf("BasicCard")) {
				jianyong->setFlags("-QiaoshuiSuccess");
				QList<ServerPlayer *> available_targets;
				if (!use.card->isKindOf("AOE") && !use.card->isKindOf("GlobalEffect")) {
					room->setPlayerFlag(jianyong, "QiaoshuiExtraTarget");
					foreach (ServerPlayer *p, room->getAlivePlayers()) {
						if (use.to.contains(p)) continue;
						if (jianyong->canUse(use.card,p))
							available_targets << p;
					}
					room->setPlayerFlag(jianyong, "-QiaoshuiExtraTarget");
				}
				QStringList choices;
				if (available_targets.length()>0) choices.append("add");
				if (use.to.length() > 1) choices.append("remove");
				choices << "cancel";
				QString choice = room->askForChoice(jianyong, "qiaoshui", choices.join("+"), data);
				if (choice == "cancel")
					return false;
				else if (choice == "add") {
					ServerPlayer *extra = nullptr;
					if (use.card->isKindOf("Collateral")){
						QStringList tos;
						tos << use.card->toString();
						foreach(ServerPlayer *t, use.to)
							tos << t->objectName();
						tos << "qiaoshui";
						room->setPlayerProperty(jianyong, "extra_collateral", tos.join("+"));
						room->askForUseCard(jianyong, "@@qiaoshui!", "@qiaoshui-add:::collateral");
						extra = jianyong->getTag("ExtraCollateralTarget").value<ServerPlayer *>();
						jianyong->removeTag("ExtraCollateralTarget");
						if (!extra) {
							QList<ServerPlayer *> victims;
							extra = available_targets.at(qsanRandomBounded(available_targets.length()));
							foreach (ServerPlayer *p, room->getOtherPlayers(extra)) {
								if (extra->canSlash(p))
									victims << p;
							}
							if(victims.length()>0)
								extra->setTag("attachTarget", QVariant::fromValue(victims.at(qsanRandomBounded(victims.length()))));
						}
					}else{
						extra = room->askForPlayerChosen(jianyong, available_targets, "qiaoshui", "@qiaoshui-add:::" + use.card->objectName());
					}
					if(extra){
						LogMessage log;
						log.type = "#QiaoshuiAdd";
						log.from = jianyong;
						log.to << extra;
						log.card_str = use.card->toString();
						log.arg = "qiaoshui";
						room->sendLog(log);
						use.to.append(extra);
						room->sortByActionOrder(use.to);
					}
				} else {
					ServerPlayer *removed = room->askForPlayerChosen(jianyong, use.to, "qiaoshui", "@qiaoshui-remove:::" + use.card->objectName());
					use.to.removeOne(removed);
					LogMessage log;
					log.type = "#QiaoshuiRemove";
					log.from = jianyong;
					log.to << removed;
					log.card_str = use.card->toString();
					log.arg = "qiaoshui";
					room->sendLog(log);
				}
				data = QVariant::fromValue(use);
			}
		}
        return false;
    }
};

class QiaoshuiTargetMod : public TargetModSkill
{
public:
    QiaoshuiTargetMod() : TargetModSkill("#qiaoshui-target")
    {
        frequency = NotFrequent;
        pattern = "Slash,TrickCard+^DelayedTrick";
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *) const
    {
        if (from->hasFlag("QiaoshuiExtraTarget"))
            return 1000;
        return 0;
    }
};

class Zongshih : public TriggerSkillV2
{
public:
    Zongshih() : TriggerSkillV2("zongshih") { events << Pindian; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        PindianStruct *pindian = data.value<PindianStruct *>();
        TriggerList list;
        if (!pindian) return list;
        const Card *card = pindian->success ? pindian->to_card : pindian->from_card;
        if (!card || room->getCardPlace(card->getEffectiveId()) != Player::PlaceTable) return list;
        for (ServerPlayer *holder : QList<ServerPlayer *>{pindian->from, pindian->to})
            if (holder && holder->isAlive() && holder->hasSkill(this)) list[holder] << objectName();
        return list;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        PindianStruct *pindian = ctx.original_data->value<PindianStruct *>();
        if (!pindian) return false;
        const Card *card = pindian->success ? pindian->to_card : pindian->from_card;
        if (!card || room->getCardPlace(card->getEffectiveId()) != Player::PlaceTable
            || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        ctx.extra_data = card->getEffectiveId();
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int id = ctx.extra_data.toInt();
        if (target && target->isAlive() && room->getCardPlace(id) == Player::PlaceTable) room->obtainCard(target, id);
        return false;
    }
};
XiansiCard::XiansiCard()
{
}

bool XiansiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.length() < 2 && !to_select->isNude();
}

void XiansiCard::onEffect(CardEffectStruct &effect) const
{
    if (effect.to->isNude()) return;
    int id = effect.from->getRoom()->askForCardChosen(effect.from, effect.to, "he", "xiansi");
    effect.from->addToPile("counter", id);
}

class XiansiViewAsSkill : public ZeroCardViewAsSkill
{
public:
    XiansiViewAsSkill() : ZeroCardViewAsSkill("xiansi")
    {
        response_pattern = "@@xiansi";
    }

    const Card *viewAs() const
    {
        return new XiansiCard;
    }
};

class Xiansi : public TriggerSkill
{
public:
    Xiansi() : TriggerSkill("xiansi")
    {
        events << EventPhaseStart;
        view_as_skill = new XiansiViewAsSkill;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() == Player::Start)
            room->askForUseCard(player, "@@xiansi", "@xiansi-card");
        return false;
    }

    int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        int index = qsanRandomBounded(2) + 1;
        if (card->isKindOf("Slash"))
            index += 2;
        return index;
    }
};

class XiansiAttach : public TriggerSkill
{
public:
    XiansiAttach() : TriggerSkill("#xiansi-attach")
    {
        events << GameStart << EventAcquireSkill << EventLoseSkill << Debut;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if ((triggerEvent == GameStart && TriggerSkill::triggerable(player))
            || (triggerEvent == EventAcquireSkill && data.value<SkillChangeStruct>().skillName == "xiansi")) {
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (!p->hasSkill("xiansi_slash", true))
                    room->attachSkillToPlayer(p, "xiansi_slash");
            }
        } else if (triggerEvent == EventLoseSkill && data.value<SkillChangeStruct>().skillName == "xiansi") {
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->hasSkill("xiansi_slash", true))
                    room->detachSkillFromPlayer(p, "xiansi_slash", true);
            }
        } else if (triggerEvent == Debut) {
            foreach (ServerPlayer *liufeng, room->findPlayersBySkillName("xiansi")) {
                if (player != liufeng && !player->hasSkill("xiansi_slash", true)) {
                    room->attachSkillToPlayer(player, "xiansi_slash");
                    break;
                }
            }
        }
        return false;
    }
};

XiansiSlashCard::XiansiSlashCard()
{
    m_skillName = "xiansi_slash";
}

bool XiansiSlashCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->setSkillName("_xiansi");
	slash->deleteLater();
	return slash->targetsFeasible(targets, Self);
}

bool XiansiSlashCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Slash *slash = new Slash(Card::NoSuit, 0);
    slash->setSkillName("_xiansi");
	slash->deleteLater();
    if (targets.isEmpty()) {
        return to_select->getPile("counter").length() >= 2 && to_select->hasSkill("xiansi")
            && slash->targetFilter(targets, to_select, Self);
    }
    return slash->targetFilter(targets, to_select, Self);
}

const Card *XiansiSlashCard::validate(CardUseStruct &cardUse) const
{
    Room *room = cardUse.from->getRoom();

	CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, cardUse.from->objectName(), cardUse.to.first()->objectName(), "xiansi", "");
    room->throwCard(this, reason, nullptr);

    Slash *slash = new Slash(Card::SuitToBeDecided, -1);
    slash->setSkillName("_xiansi");
	slash->deleteLater();
	return slash;
}

class XiansiSlashViewAsSkill : public ViewAsSkill
{
public:
    XiansiSlashViewAsSkill() : ViewAsSkill("xiansi_slash")
    {
        attached_lord_skill = true;
        expand_pile = "%counter";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return Slash::IsAvailable(player) && canSlashLiufeng(player);
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return (pattern.contains("slash") || pattern.contains("Slash"))
            && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && canSlashLiufeng(player);
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (selected.length() >= 2)
            return false;
        foreach (const Player *p, Self->getAliveSiblings()) {
            if (p->hasSkill("xiansi") && p->getPile("counter").length() > 1) {
                return p->getPile("counter").contains(to_select->getId());
            }
        }
        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() == 2) {
            XiansiSlashCard *xs = new XiansiSlashCard;
            xs->addSubcards(cards);
            return xs;
        }
        return nullptr;
    }

private:
    static bool canSlashLiufeng(const Player *player)
    {
        foreach (const Player *p, player->getAliveSiblings()) {
            if (p->hasSkill("xiansi") && p->getPile("counter").length() > 1) {
    		    Slash *slash = new Slash(Card::SuitToBeDecided, -1);
				slash->setSkillName("_xiansi");
				slash->deleteLater();
                if (slash->targetFilter(QList<const Player *>(), p, player)) {
                    return true;
                }
            }
        }
        return false;
    }
};

class Duodao : public TriggerSkillV2
{
public:
    Duodao() : TriggerSkillV2("duodao") { events << Damaged; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(this) && player->canDiscard(player, "he")
            && damage.card && damage.card->isKindOf("Slash") ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const Card *card = room->askForCard(ctx.owner, "..", "@duodao-get", *ctx.original_data, Card::MethodNone, nullptr, false, objectName());
        if (!card) return false;
        ctx.extra_data = card->getEffectiveId();
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.from) ctx.targets = {damage.from};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (!ctx.owner || room->getCardOwner(id) != ctx.owner || !ctx.owner->canDiscard(ctx.owner, id)
            || Sanguosha->getCard(id)->hasFlag("using")
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        room->throwCard(id, objectName(), ctx.owner);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        // Select the weapon after payment; nested discard effects may replace it.
        if (ctx.owner && ctx.owner->isAlive() && target && target->getWeapon()) room->obtainCard(ctx.owner, target->getWeapon());
        return false;
    }
};
class Anjian : public TriggerSkillV2
{
public:
    Anjian() : TriggerSkillV2("anjian")
    {
        events << DamageCaused;
        frequency = NotCompulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return damage.from && damage.from->isAlive() && damage.from->hasSkill(this) && damage.to
            && !damage.chain && !damage.transfer && damage.by_user && !damage.to->inMyAttackRange(damage.from)
            && damage.card && damage.card->isKindOf("Slash")
            ? TriggerList{{damage.from, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const int amount = getEffectiveAmount(ctx);
        if (amount > 0) {
            room->broadcastSkillInvoke(objectName());

            LogMessage log;
            log.type = "#AnjianBuff";
            log.from = damage.from;
            log.to << damage.to;
            log.arg = QString::number(damage.damage);
            damage.damage += amount;
            log.arg2 = QString::number(damage.damage);
            room->sendLog(log);
            room->notifySkillInvoked(damage.from, objectName());

            ctx.original_data->setValue(damage);
        }

        return false;
    }
};

ZongxuanCard::ZongxuanCard()
{
    setSkillName("zongxuan");
    will_throw = false;
    handling_method = Card::MethodNone;
    target_fixed = true;
}

void ZongxuanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	CardMoveReason reason(CardMoveReason::S_REASON_PUT, source->objectName(), "zongxuan", "");
	room->moveCardTo(this, source, nullptr, Player::DrawPile, reason, true);
}

class ZongxuanViewAsSkill : public ViewAsSkillV2
{
public:
    ZongxuanViewAsSkill() : ViewAsSkillV2("zongxuan") { expand_pile = "#zongxuan"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && request.pattern == "@@zongxuan" && !materials(request).isEmpty();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->isVirtualCard() || card->hasFlag("using")
            || request.selectedCardIds.contains(card->getEffectiveId()) || !materials(request).contains(card->getEffectiveId())) return false;
        if (const ServerPlayer *server = qobject_cast<const ServerPlayer *>(request.initiator))
            return !server->getRoom()->getCardOwner(card->getEffectiveId())
                && server->getRoom()->getCardPlace(card->getEffectiveId()) == Player::DiscardPile;
        return true;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return !request.selectedCardIds.isEmpty(); }
    TargetMode targetMode() const override { return NoTarget; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        ZongxuanCard *card = new ZongxuanCard;
        card->addSubcards(request.selectedCardIds);
        return card;
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &accepted) const override
    {
        ActiveSkillRequest request;
        request.initiator = ctx.initiator;
        request.activationRef = ctx.activationRef;
        if (accepted.selectedCardIds.isEmpty()) return false;
        for (int id : accepted.selectedCardIds) {
            if (!canSelectCard(request, Sanguosha->getCard(id))) return false;
            request.selectedCardIds << id;
        }
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override { return skillEffect(ctx, ctx.initiator); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target->isAlive() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        DummyCard cards;
        for (int id : ctx.use_card->getSubcards())
            if (!room->getCardOwner(id) && room->getCardPlace(id) == Player::DiscardPile
                && !Sanguosha->getCard(id)->hasFlag("using")) cards.addSubcard(id);
        if (cards.subcardsLength() > 0) {
            CardMoveReason reason(CardMoveReason::S_REASON_PUT, target->objectName(), objectName(), "");
            room->moveCardTo(&cards, nullptr, nullptr, Player::DrawPile, reason, true);
        }
        return ContinueEffects;
    }
private:
    QVariantList materials(const ActiveSkillRequest &request) const
    {
        return request.initiator ? request.initiator->getSkillInstanceStateValue(objectName(),
            request.activationRef.key.instanceID, "materials").toList() : QVariantList();
    }
};

class Zongxuan : public TriggerSkillV2
{
public:
    Zongxuan() : TriggerSkillV2("zongxuan") { events << CardsMoveOneTime; view_as_skill = new ZongxuanViewAsSkill; }
    static QList<int> materials(Room *room, ServerPlayer *player, const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        if (!player || move.from != player || move.to_place != Player::DiscardPile
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) return ids;
        for (int i = 0; i < move.card_ids.size(); ++i) {
            const int id = move.card_ids[i];
            if ((move.from_places.value(i) == Player::PlaceHand || move.from_places.value(i) == Player::PlaceEquip)
                && !room->getCardOwner(id) && room->getCardPlace(id) == Player::DiscardPile) ids << id;
        }
        return ids;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(this)
            && !materials(room, player, data.value<CardsMoveOneTimeStruct>()).isEmpty()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return !materials(room, ctx.owner, ctx.original_data->value<CardsMoveOneTimeStruct>()).isEmpty();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        const QList<int> ids = materials(room, target, ctx.original_data->value<CardsMoveOneTimeStruct>());
        if (ids.isEmpty()) return false;
        Room::AcceptedViewAsEffectScope borrowed(room, target, objectName(), ctx);
        if (!borrowed.isValid()) return false;
        const SkillInstanceRef ref = borrowed.activationRef();
        QVariantList values;
        for (int id : ids) values << id;
        target->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "materials", values);
        RoomState *state = Sanguosha->currentRoomState();
        const auto reason = state->getCurrentCardUseReason();
        const QString pattern = state->getCurrentCardUsePattern();
        // Restore both the temporary selection surface and the outer response request.
        const auto restore = qScopeGuard([=] {
            room->notifyMoveToPile(target, ids, "zongxuan", Player::PlaceUnknown, false);
            state->setCurrentCardUseReason(reason);
            state->setCurrentCardUsePattern(pattern);
        });
        room->notifyMoveToPile(target, ids, "zongxuan");
        room->askForUseCard(target, "@@zongxuan", "@zongxuan-put", -1, Card::MethodNone);
        return false;
    }
};

class Zhiyan : public TriggerSkillV2
{
public:
    Zhiyan() : TriggerSkillV2("zhiyan") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Finish ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "@zhiyan-invoke", true, false);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        const QList<int> ids = room->drawCardsList(target, getEffectiveAmount(ctx), objectName(), true, true);
        for (int id : ids) {
            if (!target->isAlive()) break;
            if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand) continue;
            const bool equipment = Sanguosha->getCard(id)->isKindOf("EquipCard");
            room->showCard(target, id);
            if (!equipment) continue;
            room->recover(target, RecoverStruct(objectName(), ctx.owner));
            // Only the drawn physical card, still held after recovery, may be used.
            const Card *card = Sanguosha->getCard(id);
            if (target->isAlive() && room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand
                && !card->hasFlag("using") && target->canUse(card)) room->useCardFromSkillEffect(CardUseStruct(card, target, target), ctx);
        }
        return false;
    }
};
DanshouCard::DanshouCard()
{
    setSkillName("danshou");
}

bool DanshouCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return Self->inMyAttackRange(to_select, subcards) && targets.isEmpty();
}

void DanshouCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    int len = subcardsLength();
    switch (len) {
    case 0:
        Q_ASSERT(false);
        break;
    case 1:
        if (effect.from->canDiscard(effect.to, "he")) {
            int id = room->askForCardChosen(effect.from, effect.to, "he", "danshou", false, Card::MethodDiscard);
            room->throwCard(id, effect.to, effect.from);
        }
        break;
    case 2:
        if (!effect.to->isNude()) {
            const Card *card = room->askForExchange(effect.to, "danshou", 1, 1, true, "@danshou-give::" + effect.from->objectName());
            if (card) {
                CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.to->objectName(), effect.from->objectName(), "danshou", "");
                room->obtainCard(effect.from, card, reason, false);
            }
        }
        break;
    case 3:
        room->damage(DamageStruct("danshou", effect.from, effect.to));
        break;
    default:
        room->drawCards(effect.from, 2, "danshou");
        room->drawCards(effect.to, 2, "danshou");
        break;
    }
}

class DanshouViewAsSkill : public ViewAsSkillV2
{
public:
    DanshouViewAsSkill() : ViewAsSkillV2("danshou") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 0x7fffffff; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isNude(); }
    int requiredCards(const ActiveSkillRequest &request) const
    {
        if (!request.initiator || !request.activationRef.isValid()) return 0;
        // Read the coordinator's exact-instance Phase quota, never a second owner-wide counter.
        return request.initiator->getMark(SkillInstanceUtils::formatUsageMarkKey(request.activationRef.key.skillName,
            request.activationRef.key.instanceID, "-PlayClear")) + 1;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && !request.initiator->isJilei(card) && request.selectedCardIds.size() < requiredCards(request)
            && !request.selectedCardIds.contains(card->getEffectiveId())
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId()));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return requiredCards(request) > 0 && request.selectedCardIds.size() == requiredCards(request); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return request.initiator && target && target->isAlive() && selected.isEmpty()
        && request.initiator->inMyAttackRange(target, request.selectedCardIds); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        DanshouCard *card = new DanshouCard;
        card->addSubcards(request.selectedCardIds);
        return card;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &accepted) const override
    {
        ActiveSkillRequest request;
        request.initiator = ctx.initiator;
        request.activationRef = ctx.activationRef;
        for (int id : accepted.selectedCardIds) {
            if (!canSelectCard(request, Sanguosha->getCard(id))) return false;
            request.selectedCardIds << id;
        }
        if (!cardSelectionFeasible(request)) return false;
        DummyCard payment(accepted.selectedCardIds);
        room->throwCard(&payment, objectName(), ctx.initiator);
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.use_card->subcardsLength() < 4) return ContinueEffects;
        SkillContext own = ctx;
        skillEffect(own, ctx.initiator);
        for (ServerPlayer *target : ctx.targets) {
            SkillContext recipient = ctx;
            skillEffect(recipient, target);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        ServerPlayer *source = ctx.initiator;
        if (!source || !target->isAlive() || amount <= 0) return ContinueEffects;
        Room *room = source->getRoom();
        if (ctx.choice == "gain") {
            DummyCard gift;
            const QString donorName = ctx.extra_data.toMap().value("donor").toString();
            ServerPlayer *donor = donorName.isEmpty() ? nullptr : room->findPlayerByObjectName(donorName, true);
            if (!donor) return ContinueEffects;
            for (const QVariant &value : ctx.extra_data.toMap().value("cards").toList()) {
                const int id = value.toInt();
                if (room->getCardOwner(id) == donor && (room->getCardPlace(id) == Player::PlaceHand
                    || room->getCardPlace(id) == Player::PlaceEquip) && !Sanguosha->getCard(id)->hasFlag("using")) gift.addSubcard(id);
            }
            if (gift.subcardsLength() > 0) {
                CardMoveReason reason(CardMoveReason::S_REASON_GIVE, donor->objectName(), target->objectName(), objectName(), "");
                room->obtainCard(target, &gift, reason, false);
            }
        } else if (ctx.use_card->subcardsLength() == 1) {
            for (int i = 0; i < amount && target->isAlive() && source->canDiscard(target, "he"); ++i) {
                const int id = room->askForCardChosen(source, target, "he", objectName(), false, Card::MethodDiscard);
                if (room->getCardOwner(id) != target || !source->canDiscard(target, id)) break;
                room->throwCard(id, target, source);
            }
        } else if (ctx.use_card->subcardsLength() == 2) {
            if (target->isNude() || !source->isAlive()) return ContinueEffects;
            const int count = qMin(amount, target->getCardCount(true));
            const Card *card = room->askForExchange(target, objectName(), count, count, true, "@danshou-give::" + source->objectName());
            if (!card) return ContinueEffects;
            QVariantList ids;
            for (int id : card->getSubcards()) ids << id;
            SkillContext gain = ctx;
            gain.choice = "gain";
            gain.extra_data = QVariantMap{{"donor", target->objectName()}, {"cards", ids}};
            skillEffect(gain, source);
        } else if (ctx.use_card->subcardsLength() == 3) {
            room->damage(DamageStruct(objectName(), source, target, amount));
        } else if (ctx.use_card->subcardsLength() >= 4) target->drawCards(2 * amount, objectName());
        return ContinueEffects;
    }
};

class Danshou : public TriggerSkillV2
{
public:
    Danshou() : TriggerSkillV2("danshou") { view_as_skill = new DanshouViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};

class Juece : public TriggerSkillV2
{
public:
    Juece() : TriggerSkillV2("juece") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(this) || player->getPhase() != Player::Finish) return {};
        for (ServerPlayer *target : room->getAlivePlayers()) if (target->isKongcheng()) return {{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getAlivePlayers()) if (target->isKongcheng()) candidates << target;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@juece", true, false);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive()) room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        return false;
    }
};
MiejiCard::MiejiCard()
{
    setSkillName("mieji");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool MiejiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void MiejiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    CardMoveReason reason(CardMoveReason::S_REASON_PUT, effect.from->objectName(), "", "mieji", "");
    room->moveCardTo(this, effect.from, nullptr, Player::DrawPile, reason, true);

    QList<const Card *> cards = effect.to->getCards("he");

    foreach (const Card *c, cards) {
        if (effect.to->isJilei(c))
            cards.removeOne(c);
    }

    if (cards.isEmpty())
        return;

    bool instanceDiscard = false;
    int instanceDiscardId = -1;

    if (cards.length() == 1)
        instanceDiscard = true;
    else if (cards.length() == 2) {
        bool bothTrick = true;
        int trickId = -1;
        
        foreach (const Card *c, cards) {
            if (c->getTypeId() != Card::TypeTrick)
                bothTrick = false;
            else
                trickId = c->getId();
        }
        
        instanceDiscard = !bothTrick;
        instanceDiscardId = trickId;
    }

    if (instanceDiscard) {
        DummyCard d;
        if (instanceDiscardId == -1)
            d.addSubcards(cards);
        else
            d.addSubcard(instanceDiscardId);
        room->throwCard(&d, effect.to);
    } else if (!room->askForCard(effect.to, "@@miejidiscard!", "@mieji-discard")) {
        DummyCard d;
        qsanShuffle(cards);
        int trickId = -1;
        foreach (const Card *c, cards) {
            if (c->getTypeId() == Card::TypeTrick) {
                trickId = c->getId();
                break;
            }
        }
        if (trickId > -1)
            d.addSubcard(trickId);
        else {
            d.addSubcard(cards.first());
            d.addSubcard(cards.last());
        }

        room->throwCard(&d, effect.to);
    }
}

class Mieji : public ViewAsSkillV2
{
public:
    Mieji() : ViewAsSkillV2("mieji", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MiejiCard"; }
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty() && card && !card->isVirtualCard() && !card->hasFlag("using")
            && card->isBlack() && card->isKindOf("TrickCard") && request.initiator->getHandcards().contains(card);
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return target && target->isAlive() && target != request.initiator && selected.isEmpty() && !target->isKongcheng(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest current = request; current.selectedCardIds.clear();
        const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
        if (!canSelectCard(current, card)) return false;
        room->moveCardTo(card, ctx.initiator, nullptr, Player::DrawPile,
            CardMoveReason(CardMoveReason::S_REASON_PUT, ctx.initiator->objectName(), objectName(), ""), true);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            QList<const Card *> cards = discardable(target);
            if (cards.isEmpty()) break;
            bool immediate = cards.size() == 1;
            int trickId = -1;
            for (const Card *card : cards) if (card->isKindOf("TrickCard")) trickId = card->getEffectiveId();
            if (cards.size() == 2) immediate = !(cards.first()->isKindOf("TrickCard") && cards.last()->isKindOf("TrickCard"));
            bool replied = false;
            if (!immediate) {
                // The accepted continuation remains valid after the original Mieji grant is removed during payment.
                Room::AcceptedViewAsEffectScope prompt(room, target, "miejidiscard", ctx);
                if (!prompt.isValid()) break;
                const SkillInstanceRef ref = prompt.activationRef();
                target->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "prompt", true);
                RoomState *state = Sanguosha->currentRoomState();
                const auto reason = state->getCurrentCardUseReason();
                const QString pattern = state->getCurrentCardUsePattern();
                const auto restore = qScopeGuard([=] { state->setCurrentCardUseReason(reason); state->setCurrentCardUsePattern(pattern); });
                replied = room->askForUseCard(target, "@@miejidiscard!", "@mieji-discard", -1, Card::MethodDiscard) != nullptr;
            }
            if (replied || !target->isAlive()) continue;
            // A prompt can run arbitrary callbacks; rebuild the fallback from the current legal physical cards.
            cards = discardable(target);
            if (cards.isEmpty()) continue;
            if (!immediate) qsanShuffle(cards);
            trickId = -1;
            for (const Card *card : cards) if (card->isKindOf("TrickCard")) { trickId = card->getEffectiveId(); break; }
            DummyCard discard;
            if (trickId >= 0) discard.addSubcard(trickId);
            else {
                discard.addSubcard(cards.first());
                if (cards.size() > 1) discard.addSubcard(cards.last());
            }
            room->throwCard(&discard, CardMoveReason(CardMoveReason::S_REASON_THROW, target->objectName(), objectName(), ""), target);
        }
        return ContinueEffects;
    }
private:
    static QList<const Card *> discardable(const ServerPlayer *target)
    {
        QList<const Card *> result;
        for (const Card *card : target->getCards("he"))
            if (!card->isVirtualCard() && !card->hasFlag("using") && !target->isJilei(card)) result << card;
        return result;
    }
};

class MiejiDiscard : public ViewAsSkillV2
{
public:
    MiejiDiscard() : ViewAsSkillV2("miejidiscard", 2) {}
    TargetMode targetMode() const override { return NoTarget; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY && request.pattern == "@@miejidiscard!"
            && request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "prompt").toBool();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->isVirtualCard() || card->hasFlag("using") || request.initiator->isJilei(card)
            || !request.initiator->getCards("he").contains(card) || request.selectedCardIds.contains(card->getEffectiveId())) return false;
        if (request.selectedCardIds.isEmpty()) return true;
        return request.selectedCardIds.size() == 1 && !Sanguosha->getCard(request.selectedCardIds.first())->isKindOf("TrickCard")
            && !card->isKindOf("TrickCard");
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return (request.selectedCardIds.size() == 1 && Sanguosha->getCard(request.selectedCardIds.first())->isKindOf("TrickCard"))
            || (request.selectedCardIds.size() == 2 && !Sanguosha->getCard(request.selectedCardIds.first())->isKindOf("TrickCard")
                && !Sanguosha->getCard(request.selectedCardIds.last())->isKindOf("TrickCard"));
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        ActiveSkillRequest current = request; current.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (!canSelectCard(current, Sanguosha->getCard(id))) return false;
            current.selectedCardIds << id;
        }
        return cardSelectionFeasible(current) && ViewAsSkillV2::pay(room, ctx, request);
    }
    EffectFlow effect(SkillContext &) const override { return FinishSkill; }
};

class Fencheng : public ViewAsSkillV2
{
public:
    explicit Fencheng(const QString &name = "fencheng") : ViewAsSkillV2(name, 0)
    {
        frequency = Limited;
        limit_mark = name == "fencheng" ? "@burn" : "@nosburn";
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TargetMode targetMode() const override { return NoTarget; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.initiator;
        Room *room = source->getRoom();
        const bool classic = objectName() == "fencheng";
        room->setPlayerMark(source, classic ? "@burn" : "@nosburn", 0);
        room->broadcastSkillInvoke("fencheng", classic ? (source->getGeneralName().contains("dongzhuo") ? 3 : qsanRandomBounded(2) + 1) : -1, source);
        room->doSuperLightbox(source, objectName());
        const QString flag = classic ? "FenchengUsing" : "NosFenchengUsing";
        const bool hadFlag = source->hasFlag(flag);
        source->setFlags(flag);
        const auto restore = qScopeGuard([source, flag, hadFlag] { if (!hadFlag) source->setFlags("-" + flag); });
        int previous = 0;
        for (ServerPlayer *target : room->getOtherPlayers(source)) {
            if (!source->isAlive()) break;
            if (!target->isAlive()) continue;
            // Each recipient starts from the accepted root amount; nested activations cannot overwrite this chain.
            SkillContext recipient = ctx;
            recipient.extra_data = QVariantMap{{"previous", previous}};
            skillEffect(recipient, target);
            const QVariantMap result = recipient.extra_data.toMap();
            if (result.value("history_unknown").toBool()) break;
            if (result.contains("discarded")) previous = result.value("discarded").toInt();
            room->getThread()->delay();
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (!target || !target->isAlive() || amount <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        const bool classic = objectName() == "fencheng";
        QVariantMap result = ctx.extra_data.toMap();
        const int required = (classic ? result.value("previous").toInt() + 1 : qMax(1, int(target->getEquips().size()))) * amount;
        int discarded = 0;
        if (target->canDiscard(target, "he")) {
            const qint64 cause = room->currentHistoryEventId();
            const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
            const Card *paid = room->askForDiscard(target, objectName(), classic ? qMax(999, required) : required,
                required, true, true, classic ? "@fencheng:::" + QString::number(required) : QString());
            if (paid) discarded = committedDiscards(room, target, cause, before, paid->getSubcards());
        }
        if (discarded < 0) {
            result["history_unknown"] = true;
            ctx.extra_data = result;
            return ContinueEffects;
        }
        result["discarded"] = discarded >= required ? discarded : 0;
        ctx.extra_data = result;
        if (discarded < required && target->isAlive())
            room->damage(DamageStruct(objectName(), ctx.initiator, target, (classic ? 2 : 1) * amount, DamageStruct::Fire));
        return ContinueEffects;
    }
private:
    static int committedDiscards(Room *room, ServerPlayer *target, qint64 cause, const QVariantMap &before, const QList<int> &ids)
    {
        if (cause <= 0 || before.contains("error") || !before.value("complete").toBool() || !before.contains("watermark")) return -1;
        QVariantMap filter{{"from", target->objectName()}, {"after", before.value("watermark")}};
        QSet<int> discarded;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return -1;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
                const int id = move.value("card_id", -1).toInt(), place = move.value("from_place").toInt();
                if (ids.contains(id) && (place == Player::PlaceHand || place == Player::PlaceEquip)
                    && move.value("to_place").toInt() == Player::DiscardPile
                    && (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
                    && room->historyEvent(fact.value("event_id").toLongLong()).value("parent_id").toLongLong() == cause)
                    discarded.insert(id);
            }
            if (!page.value("has_more").toBool()) return discarded.size();
            filter.insert("after", page.value("next_after"));
            filter.insert("watermark", page.value("watermark"));
        }
    }
};

FenchengCard::FenchengCard()
{
    setSkillName("fencheng");
    mute = true;
    target_fixed = true;
}

void FenchengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	int n = qsanRandomBounded(2)+1;
	if(source->getGeneralName().contains("dongzhuo"))
		n = 3;
    room->broadcastSkillInvoke("fencheng",n,source);
    room->removePlayerMark(source, "@burn");
    //room->doLightbox("$FenchengAnimate", 3000);
    room->doSuperLightbox(source, "fencheng");
    room->setTag("FenchengDiscard", 0);

    source->setFlags("FenchengUsing");
    try {
        foreach (ServerPlayer *player, room->getOtherPlayers(source)) {
            if (player->isAlive()) {
                room->cardEffect(this, source, player);
                room->getThread()->delay();
				if(source->isDead()) return;
            }
        }
        source->setFlags("-FenchengUsing");
    }
    catch (TriggerEvent triggerEvent) {
        if (triggerEvent == TurnBroken || triggerEvent == StageChange)
            source->setFlags("-FenchengUsing");
        throw triggerEvent;
    }
}

void FenchengCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
	if(effect.to->canDiscard(effect.to, "he")){
		int length = room->getTag("FenchengDiscard").toInt() + 1;
		const Card*dc = room->askForDiscard(effect.to, "fencheng", 999, length, true, true, "@fencheng:::" + QString::number(length));
		if(dc){
			room->setTag("FenchengDiscard", dc->subcardsLength());
			return;
		}
	}
	room->setTag("FenchengDiscard", 0);
	room->damage(DamageStruct("fencheng", effect.from, effect.to, 2, DamageStruct::Fire));
}

class Zhuikong : public TriggerSkill
{
public:
    Zhuikong() : TriggerSkill("zhuikong")
    {
        events << EventPhaseStart;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() != Player::RoundStart)
            return false;

        foreach (ServerPlayer *fuhuanghou, room->getAllPlayers()) {
            if (TriggerSkill::triggerable(fuhuanghou)
                && fuhuanghou->isWounded() && fuhuanghou->canPindian(player)
                && room->askForSkillInvoke(fuhuanghou, objectName())) {
                room->broadcastSkillInvoke("zhuikong");
                if (fuhuanghou->pindian(player, objectName(), nullptr)) {
                    room->setPlayerFlag(player, "zhuikong");
                } else {
                    room->setFixedDistance(player, fuhuanghou, 1);
                    QVariantList zhuikonglist = player->getTag(objectName()).toList();
                    zhuikonglist.append(QVariant::fromValue(fuhuanghou));
                    player->setTag(objectName(), QVariant::fromValue(zhuikonglist));
                }
            }
        }
        return false;
    }
};

class ZhuikongClear : public TriggerSkill
{
public:
    ZhuikongClear() : TriggerSkill("#zhuikong-clear")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to != Player::NotActive)
            return false;

        QVariantList zhuikonglist = player->getTag("zhuikong").toList();
        if (zhuikonglist.isEmpty()) return false;
        foreach (QVariant p, zhuikonglist) {
            ServerPlayer *fuhuanghou = p.value<ServerPlayer *>();
            room->removeFixedDistance(player, fuhuanghou, 1);
        }
        player->removeTag("zhuikong");
        return false;
    }
};

class ZhuikongProhibit : public ProhibitSkill
{
public:
    ZhuikongProhibit() : ProhibitSkill("#zhuikong")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return card->getTypeId() != Card::TypeSkill && to != from && from->hasFlag("zhuikong");
    }
};

class Qiuyuan : public TriggerSkillV2
{
public:
    explicit Qiuyuan(const QString &name = "qiuyuan") : TriggerSkillV2(name) { events << TargetConfirming; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(this) && use.card && use.card->isKindOf("Slash")
            && use.from && use.to.contains(player) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getOtherPlayers(ctx.owner))
            if (target != use.from && (objectName() == "nosqiuyuan" ? !target->isKongcheng() : !use.to.contains(target))) candidates << target;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), objectName() + "-invoke", true, false);
        if (!target) return false;
        ctx.targets = {target}; ctx.manual_effect = true;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.targets.isEmpty()) return false;
        ServerPlayer *target = ctx.targets.first();
        room->broadcastSkillInvoke("qiuyuan", target->getGeneralName().contains("fuwan") || target->getGeneral2Name().contains("fuwan") ? 2 : 1);
        SkillContext offer = ctx; offer.choice = "offer"; offer.extra_data.clear();
        skillEffect(event, room, ctx.owner, offer, target);
        const QVariantMap reply = offer.extra_data.toMap();
        if (!reply.value("answered").toBool()) return false;
        bool addTarget = reply.value("id", -1).toInt() < 0;
        if (!addTarget) {
            SkillContext receive = ctx; receive.choice = "receive"; receive.extra_data = reply;
            skillEffect(event, room, ctx.owner, receive, ctx.owner);
            addTarget = objectName() == "nosqiuyuan" && !reply.value("jink").toBool()
                && receive.extra_data.toMap().value("received").toBool();
        }
        if (addTarget) {
            SkillContext additional = ctx; additional.choice = "add";
            skillEffect(event, room, ctx.owner, additional, target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0 || !ctx.original_data) return false;
        if (ctx.choice == "offer") {
            const bool old = objectName() == "nosqiuyuan";
            const Card *card = room->askForCard(target, old ? ".!" : "Jink", "@" + objectName() + "-give:" + ctx.owner->objectName(),
                *ctx.original_data, Card::MethodNone);
            if (!card && old) {
                for (const Card *hand : target->getHandcards())
                    if (!hand->hasFlag("using")) { card = hand; break; }
            }
            const bool valid = card && !card->isVirtualCard() && !card->hasFlag("using") && target->getHandcards().contains(card)
                && (old || card->isKindOf("Jink"));
            ctx.extra_data = QVariantMap{{"answered", !old || valid}, {"id", valid ? card->getEffectiveId() : -1},
                {"jink", valid && card->isKindOf("Jink")}, {"donor", target->objectName()}};
        } else if (ctx.choice == "receive") {
            QVariantMap reply = ctx.extra_data.toMap();
            ServerPlayer *donor = room->findPlayerByObjectName(reply.value("donor").toString());
            const int id = reply.value("id", -1).toInt();
            if (!donor || !donor->isAlive() || id < 0 || room->getCardOwner(id) != donor
                || room->getCardPlace(id) != Player::PlaceHand || Sanguosha->getCard(id)->hasFlag("using")) return false;
            const qint64 cause = room->currentHistoryEventId();
            const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
            room->obtainCard(target, Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_GIVE,
                donor->objectName(), target->objectName(), objectName(), ""));
            const bool received = receivedCard(room, donor, target, id, cause, before);
            reply["received"] = received; ctx.extra_data = reply;
            if (received && objectName() == "nosqiuyuan" && room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand)
                room->showCard(target, id);
        } else if (ctx.choice == "add") {
            // Work on the current payload: nested recipient effects may already have changed the Slash targets.
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.from || !use.from->isAlive() || !use.card || !use.card->isKindOf("Slash") || use.to.contains(target)
                || !use.from->canSlash(target, use.card, false)) return false;
            LogMessage log; log.type = "#BecomeTarget"; log.from = target; log.card_str = use.card->toString(); room->sendLog(log);
            use.to << target; room->sortByActionOrder(use.to);
            *ctx.original_data = QVariant::fromValue(use);
        }
        return false;
    }
private:
    static bool receivedCard(Room *room, ServerPlayer *donor, ServerPlayer *recipient, int id, qint64 cause, const QVariantMap &before)
    {
        if (cause <= 0 || before.contains("error") || !before.value("complete").toBool() || !before.contains("watermark")) return false;
        QVariantMap filter{{"from", donor->objectName()}, {"after", before.value("watermark")}};
        bool found = false;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return false;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
                if (move.value("card_id", -1).toInt() == id && move.value("to").toString() == recipient->objectName()
                    && move.value("to_place").toInt() == Player::PlaceHand && move.value("from_place").toInt() == Player::PlaceHand
                    && room->historyEvent(fact.value("event_id").toLongLong()).value("parent_id").toLongLong() == cause) found = true;
            }
            if (!page.value("has_more").toBool()) return found;
            filter.insert("after", page.value("next_after")); filter.insert("watermark", page.value("watermark"));
        }
    }
};

class OLJingce : public TriggerSkillV2
{
public:
    OLJingce() : TriggerSkillV2("oljingce")
    {
        events << CardUsed << CardResponded << EventPhaseEnd << EventPhaseChanging;
        frequency = Frequent;
        global = true;
    }
    static int handSuits(Room *room, const Player *player)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return -1;
        const QVariantMap head = room->queryHistoryFacts({{"limit", 1}});
        if (head.contains("error") || !head.value("complete").toBool()) return -1;
        QSet<int> suits;
        for (const QString &kind : {QString("use_card"), QString("respond_card")}) {
            QVariantMap filter{{"kind", kind}, {"turn_id", turn}, {"watermark", head.value("watermark")},
                {kind == "use_card" ? "from" : "player", player->objectName()}, {"limit", 128}};
            for (;;) {
                const QVariantMap page = room->queryHistoryFacts(filter);
                if (page.contains("error") || !page.value("complete").toBool()) return -1;
                for (const QVariant &entry : page.value("items").toList()) {
                    const QVariantMap fact = entry.toMap(), data = fact.value("data").toMap();
                    if (kind == "respond_card" && !data.value("is_use").toBool()) continue;
                    const QVariantMap phase = room->historyEvent(fact.value("phase_id").toLongLong()).value("data").toMap();
                    if (!phase.contains("phase")) return -1;
                    if (phase.value("phase").toInt() != Player::Play) continue;
                    const QVariantMap card = data.value("card").toMap();
                    if (!card.contains("type") || !card.contains("suit") || !data.contains("is_handcard")) return -1;
                    if (card.value("type").toInt() == Card::TypeSkill || !data.value("is_handcard").toBool()) continue;
                    const int suit = card.value("suit").toInt();
                    if (suit >= Card::Spade && suit <= Card::Diamond) suits.insert(suit);
                }
                if (!page.value("has_more").toBool()) break;
                filter.insert("after", page.value("next_after"));
            }
        }
        return suits.size();
    }
    static int cardTypes(Room *room, const Player *player)
    {
        const QVariantMap page = room->queryCardHistory(player, "turn");
        if (page.contains("error") || !page.value("complete").toBool()) return -1;
        QSet<int> types;
        for (const QVariant &entry : page.value("items").toList()) {
            const QVariantMap card = entry.toMap();
            if (!card.contains("type")) return -1;
            types.insert(card.value("type").toInt());
        }
        return types.size();
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            for (ServerPlayer *owner : room->getAllPlayers(true)) room->setPlayerProperty(owner, "OLJingceSuits", 0);
        } else if (player && (event == CardUsed || event == CardResponded)) {
            room->setPlayerProperty(player, "OLJingceSuits", handSuits(room, player));
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseEnd && player && player->isAlive() && player->getPhase() == Player::Play
            && player->hasSkill(this) && cardTypes(room, player) > 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int count = cardTypes(room, ctx.owner);
        if (count <= 0 || !ctx.owner->askForSkillInvoke(objectName())) return false;
        ctx.extra_data = count;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target->isAlive() && getEffectiveAmount(ctx) > 0)
            target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class OLJingceKeep : public MaxCardsSkillV2
{
public:
    OLJingceKeep() : MaxCardsSkillV2("#oljingce-keep") { }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.holder) return CorrectSkillResult::noEffect();
        int suits = ctx.holder->property("OLJingceSuits").toInt();
        if (const ServerPlayer *server = qobject_cast<const ServerPlayer *>(ctx.holder))
            suits = OLJingce::handSuits(server->getRoom(), server);
        return suits > 0 ? CorrectSkillResult::useAmount(suits * ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

class NosChengxiang : public Chengxiang
{
public:
    NosChengxiang() : Chengxiang()
    {
        setObjectName("noschengxiang");
        total_point = 12;
    }
};

NosRenxinCard::NosRenxinCard()
{
    setSkillName("nosrenxin");
    target_fixed = true;
    mute = true;
}

void NosRenxinCard::use(Room *room, ServerPlayer *player, QList<ServerPlayer *> &) const
{
    if (player->isKongcheng()) return;
    ServerPlayer *who = room->getCurrentDyingPlayer();
    if (!who) return;

    room->broadcastSkillInvoke("renxin");
    DummyCard *handcards = player->wholeHandCards();
    player->turnOver();
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), who->objectName(), "nosrenxin", "");
    room->obtainCard(who, handcards, reason, false);
    handcards->deleteLater();
    room->recover(who, RecoverStruct("nosrenxin", player));
}

class NosRenxin : public ViewAsSkillV2
{
public:
    NosRenxin() : ViewAsSkillV2("nosrenxin") { }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && request.pattern == "peach" && !request.initiator->isKongcheng();
    }
    TargetMode targetMode() const override { return NoTarget; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        NosRenxinCard *card = new NosRenxinCard;
        if (const ServerPlayer *player = qobject_cast<const ServerPlayer *>(request.initiator)) {
            const ServerPlayer *dying = player->getRoom()->getCurrentDyingPlayer();
            card->tag["NosRenxinDying"] = dying ? dying->objectName() : QString();
            QVariantList ids;
            for (int id : player->handCards()) ids << id;
            card->tag["NosRenxinHand"] = ids;
        }
        return card;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator || !ctx.use_card) return false;
        const QString name = ctx.use_card->tag.value("NosRenxinDying").toString();
        ServerPlayer *dying = name.isEmpty() ? nullptr : room->findPlayerByObjectName(name, true);
        const QVariantList ids = ctx.use_card->tag.value("NosRenxinHand").toList();
        if (!dying || !dying->isAlive() || ids.isEmpty()) return false;
        for (const QVariant &value : ids) {
            const int id = value.toInt();
            if (room->getCardOwner(id) != ctx.initiator || room->getCardPlace(id) != Player::PlaceHand
                || Sanguosha->getCard(id)->hasFlag("using")) return false;
        }
        // Freeze the original whole hand before the turnover callback, preserving payment order.
        ctx.initiator->turnOver();
        DummyCard hand;
        for (const QVariant &value : ids) {
            const int id = value.toInt();
            if (room->getCardOwner(id) == ctx.initiator && room->getCardPlace(id) == Player::PlaceHand
                && !Sanguosha->getCard(id)->hasFlag("using")) hand.addSubcard(id);
        }
        if (dying->isAlive() && hand.subcardsLength() > 0) {
            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, ctx.initiator->objectName(), dying->objectName(), objectName(), "");
            room->obtainCard(dying, &hand, reason, false);
        }
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.initiator || !ctx.use_card) return FinishSkill;
        const QString name = ctx.use_card->tag.value("NosRenxinDying").toString();
        ServerPlayer *dying = name.isEmpty() ? nullptr : ctx.initiator->getRoom()->findPlayerByObjectName(name, true);
        if (dying) skillEffect(ctx, dying);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target->isAlive() && getEffectiveAmount(ctx) > 0)
            target->getRoom()->recover(target, RecoverStruct(objectName(), ctx.initiator, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }
};

class NosZhuikong : public TriggerSkill
{
public:
    NosZhuikong() : TriggerSkill("noszhuikong")
    {
        events << EventPhaseStart;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() != Player::RoundStart)
            return false;

        bool skip = false;
        foreach (ServerPlayer *fuhuanghou, room->getAllPlayers()) {
            if (TriggerSkill::triggerable(fuhuanghou)
                && fuhuanghou->isWounded() && fuhuanghou->canPindian(player)
                && room->askForSkillInvoke(fuhuanghou, objectName())) {
                room->broadcastSkillInvoke("zhuikong");
                if (fuhuanghou->pindian(player, objectName(), nullptr)) {
                    if (!skip) {
                        player->skip(Player::Play);
                        skip = true;
                    }
                } else {
                    room->setFixedDistance(player, fuhuanghou, 1);
                    QVariantList zhuikonglist = player->getTag(objectName()).toList();
                    zhuikonglist.append(QVariant::fromValue(fuhuanghou));
                    player->setTag(objectName(), QVariant::fromValue(zhuikonglist));
                }
            }
        }
        return false;
    }
};

class NosZhuikongClear : public TriggerSkill
{
public:
    NosZhuikongClear() : TriggerSkill("#noszhuikong-clear")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to != Player::NotActive)
            return false;

        QVariantList zhuikonglist = player->getTag("noszhuikong").toList();
        if (zhuikonglist.isEmpty()) return false;
        foreach (QVariant p, zhuikonglist) {
            ServerPlayer *fuhuanghou = p.value<ServerPlayer *>();
            room->removeFixedDistance(player, fuhuanghou, 1);
        }
        player->removeTag("noszhuikong");
        return false;
    }
};

class NosQiuyuan : public Qiuyuan
{
public:
    NosQiuyuan() : Qiuyuan("nosqiuyuan") {}
};

class NosJuece : public TriggerSkillV2
{
public:
    NosJuece() : TriggerSkillV2("nosjuece") { events << CardsMoveOneTime; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        // CardsMoveOneTime is delivered once per observer; offer only this observer's copies.
        return player && player->isAlive() && player->hasSkill(this) && player->hasFlag("CurrentPlayer")
            && move.from && move.from->isAlive() && move.from->getHp() > 0
            && move.from_places.contains(Player::PlaceHand) && move.is_last_handcard
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        ServerPlayer *target = move.from ? room->findPlayerByObjectName(move.from->objectName(), true) : nullptr;
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive()) room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        return false;
    }
};
class NosMieji : public TargetModSkillV2
{
public:
    NosMieji() : TargetModSkillV2("#nosmieji")
    {
        pattern = "SingleTargetTrick|black"; // deal with Ex Nihilo and Collateral later
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        // Related helper sources are resolved through their exact parent by Engine.
        return ctx.modType == ExtraTarget ? CorrectSkillResult::useAmount(ctx.currentAmount)
                                           : CorrectSkillResult::noEffect();
    }
};

class NosMiejiForExNihiloAndCollateral : public TriggerSkill
{
public:
    NosMiejiForExNihiloAndCollateral() : TriggerSkill("nosmieji")
    {
        events << PreCardUsed;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isBlack() && use.card->isNDTrick()) {
			int et = 1+Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, player, use.card);
			if (use.to.length()>=et) return false;
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
				if (!use.to.contains(p) && player->canUse(use.card, p))
                    targets << p;
			}
            ServerPlayer *extra = room->askForPlayerChosen(player, targets, objectName(), "@qiaoshui-add:::" + use.card->objectName(), true);
            if (!extra) return false;
            room->broadcastSkillInvoke(objectName());
            use.to.append(extra);
            room->sortByActionOrder(use.to);
            data = QVariant::fromValue(use);

            LogMessage log;
            log.type = "#QiaoshuiAdd";
            log.from = player;
            log.to << extra;
            log.arg = objectName();
            log.card_str = use.card->toString();
            room->sendLog(log);
        }
        return false;
    }
};

class NosMiejiEffect : public TriggerSkill
{
public:
    NosMiejiEffect() : TriggerSkill("#nosmieji-effect")
    {
        events << PreCardUsed;
    }

    int getPriority(TriggerEvent) const
    {
        return 6;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("SingleTargetTrick") && !use.card->targetFixed() && use.to.length() > 1
            && use.card->isBlack() && use.from->hasSkill("nosmieji"))
            room->broadcastSkillInvoke("mieji");
        return false;
    }
};

class NosFencheng : public Fencheng
{
public:
    NosFencheng() : Fencheng("nosfencheng") {}
};

NosFenchengCard::NosFenchengCard()
{
    setSkillName("nosfencheng");
    mute = true;
    target_fixed = true;
}

void NosFenchengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->removePlayerMark(source, "@nosburn");
    room->broadcastSkillInvoke("fencheng");
    //room->doLightbox("$NosFenchengAnimate", 3000);

    room->doSuperLightbox(source, "nosfencheng");

    source->setFlags("NosFenchengUsing");
    try {
        foreach (ServerPlayer *player, room->getOtherPlayers(source)) {
            if (player->isAlive()) {
                room->cardEffect(this, source, player);
                room->getThread()->delay();
				if(source->isDead()) return;
            }
        }
        source->setFlags("-NosFenchengUsing");
    }
    catch (TriggerEvent triggerEvent) {
        if (triggerEvent == TurnBroken || triggerEvent == StageChange)
            source->setFlags("-NosFenchengUsing");
        throw triggerEvent;
    }
}

void NosFenchengCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();

    int length = qMax(1, effect.to->getEquips().length());
    if (!effect.to->canDiscard(effect.to, "he") || !room->askForDiscard(effect.to, "nosfencheng", length, length, true, true))
        room->damage(DamageStruct("nosfencheng", effect.from, effect.to, 1, DamageStruct::Fire));
}

class NosDanshou : public TriggerSkillV2
{
public:
    NosDanshou() : TriggerSkillV2("nosdanshou") { events << Damage; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player == damage.from && player->isAlive() && player->hasSkill(this)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && ctx.original_data && room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        SkillContext draw = ctx;
        draw.choice = "draw";
        skillEffect(event, room, player, draw, ctx.owner);
        ServerPlayer *current = room->getCurrent();
        if (current && current->isAlive() && current->getPhase() != Player::NotActive) {
            SkillContext end = ctx;
            end.choice = "end";
            skillEffect(event, room, player, end, current);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
        else {
            // The current turn's recipient hook may intercept termination independently of drawing.
            LogMessage log;
            log.type = "#SkipAllPhase";
            log.from = target;
            room->sendLog(log);
            throw TurnBroken;
        }
        return false;
    }
};
YJCM2013Package::YJCM2013Package()
    : Package("YJCM2013")
{
    General *caochong = new General(this, "caochong", "wei", 3); // YJ 201
    caochong->addSkill(new Chengxiang);
    caochong->addSkill(new Renxin);

    General *fuhuanghou = new General(this, "fuhuanghou", "qun", 3, false); // YJ 202
    fuhuanghou->addSkill(new Zhuikong);
    fuhuanghou->addSkill(new ZhuikongClear);
    fuhuanghou->addSkill(new ZhuikongProhibit);
    fuhuanghou->addSkill(new Qiuyuan);
    related_skills.insert("zhuikong", "#zhuikong");
    related_skills.insert("zhuikong", "#zhuikong-clear");

    General *guohuai = new General(this, "guohuai", "wei"); // YJ 203
    guohuai->addSkill(new Jingce);

    General *guanping = new General(this, "guanping", "shu", 4); // YJ 204
    guanping->addSkill(new Longyin);

    General *jianyong = new General(this, "jianyong", "shu", 3); // YJ 205
    jianyong->addSkill(new Qiaoshui);
    jianyong->addSkill(new QiaoshuiTargetMod);
    jianyong->addSkill(new Zongshih);
    related_skills.insert("qiaoshui", "#qiaoshui-target");

    General *liru = new General(this, "liru", "qun", 3); // YJ 206
    liru->addSkill(new Juece);
    liru->addSkill(new Mieji);
    liru->addSkill(new Fencheng);

    General *liufeng = new General(this, "liufeng", "shu"); // YJ 207
    liufeng->addSkill(new Xiansi);
    liufeng->addSkill(new XiansiAttach);
    related_skills.insert("xiansi", "#xiansi-attach");

    General *manchong = new General(this, "manchong", "wei", 3); // YJ 208
    manchong->addSkill(new Junxing);
    manchong->addSkill(new Yuce);

    General *panzhangmazhong = new General(this, "panzhangmazhong", "wu"); // YJ 209
    panzhangmazhong->addSkill(new Duodao);
    panzhangmazhong->addSkill(new Anjian);

    General *yufan = new General(this, "yufan", "wu", 3); // YJ 210
    yufan->addSkill(new Zongxuan);
    yufan->addSkill(new Zhiyan);

    General *zhuran = new General(this, "zhuran", "wu"); // YJ 211
    zhuran->addSkill(new Danshou);

    addMetaObject<JunxingCard>();
    addMetaObject<QiaoshuiCard>();
    addMetaObject<XiansiCard>();
    addMetaObject<XiansiSlashCard>();
    addMetaObject<ZongxuanCard>();
    addMetaObject<MiejiCard>();
    addMetaObject<FenchengCard>();
    addMetaObject<ExtraCollateralCard>();
    addMetaObject<DanshouCard>();

    skills << new XiansiSlashViewAsSkill << new MiejiDiscard << new ExtraCollateral;
}

ADD_PACKAGE(YJCM2013)

void MigrateToNostalgiaYJCM2013(Package *pkg)
{
    General *nos_caochong = new General(pkg, "nos_caochong", "wei", 3);
    nos_caochong->addSkill(new NosChengxiang);
    nos_caochong->addSkill(new NosRenxin);
    pkg->addMetaObject<NosRenxinCard>();

    General *nos_fuhuanghou = new General(pkg, "nos_fuhuanghou", "qun", 3, false);
    nos_fuhuanghou->addSkill(new NosZhuikong);
    nos_fuhuanghou->addSkill(new NosZhuikongClear);
    nos_fuhuanghou->addSkill(new NosQiuyuan);
    pkg->insertRelatedSkills("noszhuikong", "#noszhuikong-clear");

    General *nos_liru = new General(pkg, "nos_liru", "qun", 3);
    nos_liru->addSkill(new NosJuece);
    nos_liru->addSkill(new NosMieji);
    nos_liru->addSkill(new NosMiejiForExNihiloAndCollateral);
    nos_liru->addSkill(new NosMiejiEffect);
    nos_liru->addSkill(new NosFencheng);
    pkg->insertRelatedSkills("nosmieji", "#nosmieji");
    pkg->insertRelatedSkills("nosmieji", "#nosmieji-effect");
    pkg->addMetaObject<NosFenchengCard>();

    General *nos_zhuran = new General(pkg, "nos_zhuran", "wu");
    nos_zhuran->addSkill(new NosDanshou);
}

void MigrateToOLStYJ2013(Package *pkg)
{
    General *ol_guohuai = new General(pkg, "ol_guohuai", "wei", 3);
    ol_guohuai->addSkill(new OLJingce);
    ol_guohuai->addSkill(new OLJingceKeep);
    pkg->insertRelatedSkills("oljingce", "#oljingce-keep");
}
