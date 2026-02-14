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

Chengxiang::Chengxiang() : MasochismSkill("chengxiang")
{
    frequency = Frequent;
    total_point = 13;
}

void Chengxiang::onDamaged(ServerPlayer *target, const DamageStruct &damage) const
{
    Room *room = target->getRoom();
    if (!target->askForSkillInvoke(this, QVariant::fromValue(damage))) return;
    room->broadcastSkillInvoke("chengxiang");

    QList<int> card_ids = room->getNCards(4);
    room->fillAG(card_ids);

    QList<int> to_get, to_throw;
    while (true) {
        int sum = 0;
        foreach(int id, to_get)
            sum += Sanguosha->getCard(id)->getNumber();
        foreach (int id, card_ids) {
            if (sum + Sanguosha->getCard(id)->getNumber() > total_point) {
                room->takeAG(nullptr, id, false);
                card_ids.removeOne(id);
                to_throw << id;
            }
        }
        if (card_ids.isEmpty()) break;

        int card_id = room->askForAG(target, card_ids, card_ids.length() < 4, objectName());
        if (card_id == -1) break;
        card_ids.removeOne(card_id);
        to_get << card_id;
        room->takeAG(target, card_id, false);
        if (card_ids.isEmpty()) break;
    }
	room->getThread()->delay();
    room->clearAG();
    DummyCard *dummy = new DummyCard;
    if (!to_get.isEmpty()) {
        dummy->addSubcards(to_get);
        target->obtainCard(dummy);
    }
    dummy->clearSubcards();
    if (!to_throw.isEmpty() || !card_ids.isEmpty()) {
        dummy->addSubcards(to_throw + card_ids);
        CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, target->objectName(), objectName(), "");
        room->throwCard(dummy, reason, nullptr);
    }
    delete dummy;
}

class Renxin : public TriggerSkill
{
public:
    Renxin() : TriggerSkill("renxin")
    {
        events << DamageInflicted << ChoiceMade;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == DamageInflicted) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.to->getHp() == 1) {
                foreach (ServerPlayer *p, room->getOtherPlayers(damage.to)) {
                    if (TriggerSkill::triggerable(p) && p->canDiscard(p, "he")) {
                        if (room->askForCard(p, ".Equip", "@renxin-card:" + damage.to->objectName(), data, objectName())) {
                            room->broadcastSkillInvoke(objectName());
                            LogMessage log;
                            log.type = "#Renxin";
                            log.from = damage.to;
                            log.arg = objectName();
                            room->sendLog(log);
                            return true;
                        }
                    }
                }
            }
        } else if (triggerEvent == ChoiceMade) {
            QStringList data_list = data.toString().split(":");
            if (data_list.contains("@renxin-card") && data_list.last() != "")
                player->turnOver();
        }
        return false;
    }
};

class Jingce : public TriggerSkill
{
public:
    Jingce() : TriggerSkill("jingce")
    {
        events << EventPhaseEnd;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() == Player::Play && player->getMark("jingce-Clear") >= player->getHp()) {
            if (room->askForSkillInvoke(player, objectName())) {
                room->broadcastSkillInvoke(objectName());
                player->drawCards(2, objectName());
            }
        }
        return false;
    }
};

JunxingCard::JunxingCard()
{
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

class Junxing : public ViewAsSkill
{
public:
    Junxing() : ViewAsSkill("junxing")
    {
    }

    bool viewFilter(const QList<const Card *> &, const Card *to_select) const
    {
        return !to_select->isEquipped() && !Self->isJilei(to_select);
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return nullptr;

        JunxingCard *card = new JunxingCard;
        card->addSubcards(cards);
        card->setSkillName(objectName());
        return card;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->canDiscard(player, "h") && !player->hasUsed("JunxingCard");
    }
};

class Yuce : public MasochismSkill
{
public:
    Yuce() : MasochismSkill("yuce")
    {
    }

    void onDamaged(ServerPlayer *target, const DamageStruct &damage) const
    {
        if (target->isKongcheng()) return;

        Room *room = target->getRoom();
        QVariant data = QVariant::fromValue(damage);
        const Card *card = room->askForCard(target, ".", "@yuce-show", data, Card::MethodNone);
        if (card) {
            room->broadcastSkillInvoke(objectName());
            LogMessage log;
            log.from = target;
            log.type = "#InvokeSkill";
            log.arg = objectName();
            room->sendLog(log);
            room->notifySkillInvoked(target, objectName());

            room->showCard(target, card->getEffectiveId());
            if (!damage.from || damage.from->isDead()) return;

            QString type_name[4] = { "", "BasicCard", "TrickCard", "EquipCard" };
            QStringList types;
            types << "BasicCard" << "TrickCard" << "EquipCard";
            types.removeOne(type_name[card->getTypeId()]);
            if (!damage.from->canDiscard(damage.from, "h")
                || !room->askForCard(damage.from, types.join(",") + "|.|.|hand",
                QString("@yuce-discard:%1::%2:%3")
                .arg(target->objectName())
                .arg(types.first()).arg(types.last()),
                data)) {
                room->recover(target, RecoverStruct("yuce", target));
            }
        }
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
				if (p->getAcquiredSkills().contains(objectName())){
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
    use.from->tag["ExtraCollateralTarget"] = QVariant::fromValue(killer);
    killer->tag["attachTarget"] = QVariant::fromValue(victim);

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
						extra = jianyong->tag["ExtraCollateralTarget"].value<ServerPlayer *>();
						jianyong->tag.remove("ExtraCollateralTarget");
						if (!extra) {
							QList<ServerPlayer *> victims;
							extra = available_targets.at(qrand() % available_targets.length());
							foreach (ServerPlayer *p, room->getOtherPlayers(extra)) {
								if (extra->canSlash(p))
									victims << p;
							}
							if(victims.length()>0)
								extra->tag["attachTarget"] = QVariant::fromValue(victims.at(qrand() % victims.length()));
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

class Zongshih : public TriggerSkill
{
public:
    Zongshih() : TriggerSkill("zongshih")
    {
        events << Pindian;
        frequency = Frequent;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        PindianStruct *pindian = data.value<PindianStruct *>();
        const Card *to_obtain = pindian->from_card;
		if (pindian->success) to_obtain = pindian->to_card;
        if (TriggerSkill::triggerable(pindian->from)) {
			if(room->getCardPlace(to_obtain->getEffectiveId())==Player::PlaceTable&&pindian->from->askForSkillInvoke(this,data)){
				room->broadcastSkillInvoke(objectName());
				pindian->from->obtainCard(to_obtain);
			}
        }
		if (TriggerSkill::triggerable(pindian->to)) {
			if (room->getCardPlace(to_obtain->getEffectiveId())==Player::PlaceTable&&pindian->to->askForSkillInvoke(this,data)) {
				room->broadcastSkillInvoke(objectName());
				pindian->to->obtainCard(to_obtain);
			}
        }
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
        int index = qrand() % 2 + 1;
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
            || (triggerEvent == EventAcquireSkill && data.toString() == "xiansi")) {
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (!p->hasSkill("xiansi_slash", true))
                    room->attachSkillToPlayer(p, "xiansi_slash");
            }
        } else if (triggerEvent == EventLoseSkill && data.toString() == "xiansi") {
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

class Duodao : public MasochismSkill
{
public:
    Duodao() : MasochismSkill("duodao")
    {
    }

    void onDamaged(ServerPlayer *target, const DamageStruct &damage) const
    {
        if (!damage.card || !damage.card->isKindOf("Slash") || !target->canDiscard(target, "he"))
            return;
        QVariant data = QVariant::fromValue(damage);
        Room *room = target->getRoom();
        if (room->askForCard(target, "..", "@duodao-get", data, objectName())) {
            if (damage.from && damage.from->getWeapon()) {
                room->broadcastSkillInvoke(objectName());
                target->obtainCard(damage.from->getWeapon());
            }
        }
    }
};

class Anjian : public TriggerSkill
{
public:
    Anjian() : TriggerSkill("anjian")
    {
        events << DamageCaused;
        frequency = NotCompulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.chain || damage.transfer || !damage.by_user) return false;
        if (damage.from && !damage.to->inMyAttackRange(damage.from)
            && damage.card && damage.card->isKindOf("Slash")) {
            room->broadcastSkillInvoke(objectName());

            LogMessage log;
            log.type = "#AnjianBuff";
            log.from = damage.from;
            log.to << damage.to;
            log.arg = QString::number(damage.damage);
            log.arg2 = QString::number(++damage.damage);
            room->sendLog(log);
            room->notifySkillInvoked(damage.from, objectName());

            data = QVariant::fromValue(damage);
        }

        return false;
    }
};

ZongxuanCard::ZongxuanCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
    target_fixed = true;
}

void ZongxuanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	CardMoveReason reason(CardMoveReason::S_REASON_PUT, source->objectName(), "zongxuan", "");
	room->moveCardTo(this, source, nullptr, Player::DrawPile, reason, true);
}

class ZongxuanViewAsSkill : public ViewAsSkill
{
public:
    ZongxuanViewAsSkill() : ViewAsSkill("zongxuan")
    {
        response_pattern = "@@zongxuan";
		expand_pile = "#zongxuan";
    }

    bool viewFilter(const QList<const Card *> &, const Card *to_select) const
    {
        return Self->getPileName(to_select->getEffectiveId())=="#zongxuan";
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty()) return nullptr;

        ZongxuanCard *card = new ZongxuanCard;
        card->addSubcards(cards);
        return card;
    }
};

class Zongxuan : public TriggerSkill
{
public:
    Zongxuan() : TriggerSkill("zongxuan")
    {
        events << CardsMoveOneTime;
        view_as_skill = new ZongxuanViewAsSkill;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!move.from || move.from != player) return false;
        if (move.to_place == Player::DiscardPile && ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)) {

            int i = 0;
            QList<int> ids;
            foreach (int card_id, move.card_ids) {
                if (!room->getCardOwner(card_id) && (move.from_places[i] == Player::PlaceHand || move.from_places[i] == Player::PlaceEquip)) {
                    ids << card_id;
                }
                i++;
            }
			if (ids.isEmpty()) return false;
			room->notifyMoveToPile(player,ids,"zongxuan");
            room->askForUseCard(player, "@@zongxuan", "@zongxuan-put");
		}
        return false;
    }
};

class Zhiyan : public PhaseChangeSkill
{
public:
    Zhiyan() : PhaseChangeSkill("zhiyan")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish)
            return false;

        ServerPlayer *to = room->askForPlayerChosen(target, room->getAlivePlayers(), objectName(), "@zhiyan-invoke", true, true);
        if (to) {
            room->broadcastSkillInvoke(objectName());
            QList<int> ids = room->drawCardsList(to, 1, objectName(), true, true);
            int id = ids.first();
            const Card *card = Sanguosha->getCard(id);
            if (!to->isAlive())
                return false;
            room->showCard(to, id);

            if (card->isKindOf("EquipCard")) {
                room->recover(to, RecoverStruct("zhiyan", target));
                if (to->isAlive() && to->canUse(card) && !to->getEquipsId().contains(id))
                    room->useCard(CardUseStruct(card, to, to));
            }
        }
        return false;
    }
};

DanshouCard::DanshouCard()
{
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

class DanshouViewAsSkill : public ViewAsSkill
{
public:
    DanshouViewAsSkill() : ViewAsSkill("danshou")
    {
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        return !Self->isJilei(to_select) && selected.length() <= Self->getMark("danshou");
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() != Self->getMark("danshou") + 1) return nullptr;
        DanshouCard *danshou = new DanshouCard;
        danshou->addSubcards(cards);
        return danshou;
    }
};

class Danshou : public TriggerSkill
{
public:
    Danshou() : TriggerSkill("danshou")
    {
        events << EventPhaseStart << PreCardUsed;
        view_as_skill = new DanshouViewAsSkill;
    }

    int getPriority(TriggerEvent) const
    {
        return 6;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Play) {
            room->setPlayerMark(player, "danshou", 0);
        } else if (triggerEvent == PreCardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("DanshouCard"))
                room->addPlayerMark(use.from, "danshou");
        }
        return false;
    }
};

class Juece : public PhaseChangeSkill
{
public:
    Juece() : PhaseChangeSkill("juece")
    {
    }

    bool onPhaseChange(ServerPlayer *target, Room *room) const
    {
        if (target->getPhase() != Player::Finish) return false;
        QList<ServerPlayer *> kongcheng_players;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->isKongcheng())
                kongcheng_players << p;
        }
        if (kongcheng_players.isEmpty()) return false;

        ServerPlayer *to_damage = room->askForPlayerChosen(target, kongcheng_players, objectName(),
            "@juece", true, true);
        if (to_damage) {
            int index = qrand() % 2 + 1;
            if (target->isJieGeneral())
                index += 2;
            target->peiyin(this, index);
            room->damage(DamageStruct(objectName(), target, to_damage));
        }
        return false;
    }
};

MiejiCard::MiejiCard()
{
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
        qShuffle(cards);
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

class Mieji : public OneCardViewAsSkill
{
public:
    Mieji() : OneCardViewAsSkill("mieji")
    {
        filter_pattern = "TrickCard|black";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MiejiCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        MiejiCard *card = new MiejiCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class MiejiDiscard : public ViewAsSkill
{
public:
    MiejiDiscard() : ViewAsSkill("miejidiscard")
    {

    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern == "@@miejidiscard!";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (Self->isJilei(to_select))
            return false;

        if (selected.length() == 0)
            return true;
        else if (selected.length() == 1) {
            if (selected.first()->getTypeId() == Card::TypeTrick)
                return false;
            else
                return to_select->getTypeId() != Card::TypeTrick;
        } else
            return false;

        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        bool ok = false;
        if (cards.length() == 1)
            ok = cards.first()->getTypeId() == Card::TypeTrick;
        else if (cards.length() == 2) {
            ok = true;
            foreach (const Card *c, cards) {
                if (c->getTypeId() == Card::TypeTrick) {
                    ok = false;
                    break;
                }
            }
        }

        if (!ok)
            return nullptr;

        DummyCard *dummy = new DummyCard;
        dummy->addSubcards(cards);
        return dummy;
    }
};

class Fencheng : public ZeroCardViewAsSkill
{
public:
    Fencheng() : ZeroCardViewAsSkill("fencheng")
    {
        frequency = Limited;
        limit_mark = "@burn";
    }

    const Card *viewAs() const
    {
        return new FenchengCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("@burn") >= 1;
    }
};

FenchengCard::FenchengCard()
{
    mute = true;
    target_fixed = true;
}

void FenchengCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	int n = qrand()%2+1;
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
                    QVariantList zhuikonglist = player->tag[objectName()].toList();
                    zhuikonglist.append(QVariant::fromValue(fuhuanghou));
                    player->tag[objectName()] = QVariant::fromValue(zhuikonglist);
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

        QVariantList zhuikonglist = player->tag["zhuikong"].toList();
        if (zhuikonglist.isEmpty()) return false;
        foreach (QVariant p, zhuikonglist) {
            ServerPlayer *fuhuanghou = p.value<ServerPlayer *>();
            room->removeFixedDistance(player, fuhuanghou, 1);
        }
        player->tag.remove("zhuikong");
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

class Qiuyuan : public TriggerSkill
{
public:
    Qiuyuan() : TriggerSkill("qiuyuan")
    {
        events << TargetConfirming;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Slash")) {
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(use.from)) {
                if (!use.to.contains(p)) targets << p;
            }
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "qiuyuan-invoke", true, true);
            if (target) {
                if (target->getGeneralName().contains("fuwan") || target->getGeneral2Name().contains("fuwan"))
                    room->broadcastSkillInvoke("qiuyuan", 2);
                else
                    room->broadcastSkillInvoke("qiuyuan", 1);
                const Card *card = room->askForCard(target, "Jink", "@qiuyuan-give:" + player->objectName(), data, Card::MethodNone);
                if (card) {
 					CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), "nosqiuyuan", "");
					room->obtainCard(player, card, reason);
                } else {
					if (use.from->canSlash(target, use.card, false)) {
                        LogMessage log;
                        log.type = "#BecomeTarget";
                        log.from = target;
                        log.card_str = use.card->toString();
                        room->sendLog(log);

                        //room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());

                        use.to.append(target);
                        room->sortByActionOrder(use.to);
                        data = QVariant::fromValue(use);
                        //room->getThread()->trigger(TargetConfirming, room, target, data);
                    }
                }
            }
        }
        return false;
    }
};

class OLJingce : public TriggerSkill
{
public:
    OLJingce() : TriggerSkill("oljingce")
    {
        events << PreCardUsed << PreCardResponded << EventPhaseEnd;
        frequency = Frequent;
        global = true;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseEnd) {
            if (player->getPhase() != Player::Play) return false;
            int n = 0;
			foreach (QString m, player->getMarkNames()) {
				if(m.contains("oljingce_type")) n++;
			}
            if (n<1) return false;
            if (!player->hasSkill(objectName()) || !player->askForSkillInvoke(objectName())) return false;
            room->broadcastSkillInvoke(objectName());
            player->drawCards(n, objectName());
        }else {
            if (player->getPhase() != Player::Play) return false;
            const Card *card = nullptr,*used_card = nullptr;
            if (event == PreCardUsed) {
                CardUseStruct use = data.value<CardUseStruct>();
                if (use.card->isKindOf("SkillCard")) return false;
                if (use.m_isHandcard)
                    card = use.card;
                used_card = use.card;
            } else {
                CardResponseStruct res = data.value<CardResponseStruct>();
                if (res.m_card->isKindOf("SkillCard")||!res.m_isUse) return false;
                if (res.m_isHandcard)
                    card = res.m_card;
                used_card = res.m_card;
            }
            if (used_card != nullptr) {
				player->addMark(used_card->getSuitString()+"oljingce_type-Clear");
            }
            if (card != nullptr) {
                if (player->getMark(card->getSuitString()+"oljingce_suit-Clear")<1) {
                    player->addMark(card->getSuitString()+"oljingce_suit-Clear");
                    room->addPlayerMark(player, "oljingce_maxcard-Clear");
                }
            }
        }
        return false;
    }
};

class OLJingceKeep : public MaxCardsSkill
{
public:
    OLJingceKeep() : MaxCardsSkill("#oljingce-keep")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->hasSkill("oljingce"))
            return target->getMark("oljingce_maxcard-Clear");
        return 0;
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
    delete handcards;
    room->recover(who, RecoverStruct("nosrenxin", player));
}

class NosRenxin : public ZeroCardViewAsSkill
{
public:
    NosRenxin() : ZeroCardViewAsSkill("nosrenxin")
    {
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return pattern == "peach" && !player->isKongcheng();
    }

    const Card *viewAs() const
    {
        return new NosRenxinCard;
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
                    QVariantList zhuikonglist = player->tag[objectName()].toList();
                    zhuikonglist.append(QVariant::fromValue(fuhuanghou));
                    player->tag[objectName()] = QVariant::fromValue(zhuikonglist);
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

        QVariantList zhuikonglist = player->tag["noszhuikong"].toList();
        if (zhuikonglist.isEmpty()) return false;
        foreach (QVariant p, zhuikonglist) {
            ServerPlayer *fuhuanghou = p.value<ServerPlayer *>();
            room->removeFixedDistance(player, fuhuanghou, 1);
        }
        player->tag.remove("noszhuikong");
        return false;
    }
};

class NosQiuyuan : public TriggerSkill
{
public:
    NosQiuyuan() : TriggerSkill("nosqiuyuan")
    {
        events << TargetConfirming;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Slash")) {
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (!p->isKongcheng() && p != use.from)
                    targets << p;
            }
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "nosqiuyuan-invoke", true, true);
            if (target) {
                if (target->getGeneralName().contains("fuwan") || target->getGeneral2Name().contains("fuwan"))
                    room->broadcastSkillInvoke("qiuyuan", 2);
                else
                    room->broadcastSkillInvoke("qiuyuan", 1);
                const Card *card = room->askForCard(target, ".!", "@nosqiuyuan-give:" + player->objectName(), data, Card::MethodNone);
                if (!card) card = target->getHandcards().first();
                CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), "nosqiuyuan", "");
                room->obtainCard(player, card, reason);
                room->showCard(player, card->getEffectiveId());
                if (!card->isKindOf("Jink")) {
                    if (use.from->canSlash(target, use.card, false)) {
                        LogMessage log;
                        log.type = "#BecomeTarget";
                        log.from = target;
                        log.card_str = use.card->toString();
                        room->sendLog(log);

                        //room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());

                        use.to.append(target);
                        room->sortByActionOrder(use.to);
                        data = QVariant::fromValue(use);
                        //room->getThread()->trigger(TargetConfirming, room, target, data);
                    }
                }
            }
        }
        return false;
    }
};

class NosJuece : public TriggerSkill
{
public:
    NosJuece() : TriggerSkill("nosjuece")
    {
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (player->hasFlag("CurrentPlayer") && move.from_places.contains(Player::PlaceHand)
            && move.is_last_handcard) {
            if (move.from->getHp() > 0 && move.from->isAlive() && room->askForSkillInvoke(player, objectName(), data)) {
                room->broadcastSkillInvoke("juece");
                room->damage(DamageStruct(objectName(), player, (ServerPlayer *)move.from));
            }
        }
        return false;
    }
};

class NosMieji : public TargetModSkill
{
public:
    NosMieji() : TargetModSkill("#nosmieji")
    {
        pattern = "SingleTargetTrick|black"; // deal with Ex Nihilo and Collateral later
    }

    int getExtraTargetNum(const Player *from, const Card *) const
    {
        if (from->hasSkill("nosmieji"))
            return 1;
        return 0;
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

class NosFencheng : public ZeroCardViewAsSkill
{
public:
    NosFencheng() : ZeroCardViewAsSkill("nosfencheng")
    {
        frequency = Limited;
        limit_mark = "@nosburn";
    }

    const Card *viewAs() const
    {
        return new NosFenchengCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("@nosburn") >= 1;
    }
};

NosFenchengCard::NosFenchengCard()
{
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

class NosDanshou : public TriggerSkill
{
public:
    NosDanshou() : TriggerSkill("nosdanshou")
    {
        events << Damage;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (room->askForSkillInvoke(player, objectName(), data)) {
            player->drawCards(1, objectName());
            ServerPlayer *current = room->getCurrent();
            if (current && current->isAlive() && current->getPhase() != Player::NotActive) {
                room->broadcastSkillInvoke("danshou");
                LogMessage log;
                log.type = "#SkipAllPhase";
                log.from = current;
                room->sendLog(log);
            }
            throw TurnBroken;
        }
        return false;
    }
};

YJCM2013Package::YJCM2013Package()
    : Package("YJCM2013")
{
    General *nos_caochong = new General(this, "nos_caochong", "wei", 3);
    nos_caochong->addSkill(new NosChengxiang);
    nos_caochong->addSkill(new NosRenxin);
    addMetaObject<NosRenxinCard>();
    General *caochong = new General(this, "caochong", "wei", 3); // YJ 201
    caochong->addSkill(new Chengxiang);
    caochong->addSkill(new Renxin);

    General *nos_fuhuanghou = new General(this, "nos_fuhuanghou", "qun", 3, false);
    nos_fuhuanghou->addSkill(new NosZhuikong);
    nos_fuhuanghou->addSkill(new NosZhuikongClear);
    nos_fuhuanghou->addSkill(new NosQiuyuan);
    related_skills.insertMulti("noszhuikong", "#noszhuikong-clear");
    General *fuhuanghou = new General(this, "fuhuanghou", "qun", 3, false); // YJ 202
    fuhuanghou->addSkill(new Zhuikong);
    fuhuanghou->addSkill(new ZhuikongClear);
    fuhuanghou->addSkill(new ZhuikongProhibit);
    fuhuanghou->addSkill(new Qiuyuan);
    related_skills.insertMulti("zhuikong", "#zhuikong");
    related_skills.insertMulti("zhuikong", "#zhuikong-clear");

    General *guohuai = new General(this, "guohuai", "wei"); // YJ 203
    guohuai->addSkill(new Jingce);

    General *ol_guohuai = new General(this, "ol_guohuai", "wei", 3);
    ol_guohuai->addSkill(new OLJingce);
    ol_guohuai->addSkill(new OLJingceKeep);
    related_skills.insertMulti("oljingce", "#oljingce-keep");

    General *guanping = new General(this, "guanping", "shu", 4); // YJ 204
    guanping->addSkill(new Longyin);

    General *jianyong = new General(this, "jianyong", "shu", 3); // YJ 205
    jianyong->addSkill(new Qiaoshui);
    jianyong->addSkill(new QiaoshuiTargetMod);
    jianyong->addSkill(new Zongshih);
    related_skills.insertMulti("qiaoshui", "#qiaoshui-target");

    General *nos_liru = new General(this, "nos_liru", "qun", 3);
    nos_liru->addSkill(new NosJuece);
    nos_liru->addSkill(new NosMieji);
    nos_liru->addSkill(new NosMiejiForExNihiloAndCollateral);
    nos_liru->addSkill(new NosMiejiEffect);
    nos_liru->addSkill(new NosFencheng);
    related_skills.insertMulti("nosmieji", "#nosmieji");
    related_skills.insertMulti("nosmieji", "#nosmieji-effect");
    addMetaObject<NosFenchengCard>();
    General *liru = new General(this, "liru", "qun", 3); // YJ 206
    liru->addSkill(new Juece);
    liru->addSkill(new Mieji);
    liru->addSkill(new Fencheng);

    General *liufeng = new General(this, "liufeng", "shu"); // YJ 207
    liufeng->addSkill(new Xiansi);
    liufeng->addSkill(new XiansiAttach);
    related_skills.insertMulti("xiansi", "#xiansi-attach");

    General *manchong = new General(this, "manchong", "wei", 3); // YJ 208
    manchong->addSkill(new Junxing);
    manchong->addSkill(new Yuce);

    General *panzhangmazhong = new General(this, "panzhangmazhong", "wu"); // YJ 209
    panzhangmazhong->addSkill(new Duodao);
    panzhangmazhong->addSkill(new Anjian);

    General *yufan = new General(this, "yufan", "wu", 3); // YJ 210
    yufan->addSkill(new Zongxuan);
    yufan->addSkill(new Zhiyan);

    General *nos_zhuran = new General(this, "nos_zhuran", "wu");
    nos_zhuran->addSkill(new NosDanshou);

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
