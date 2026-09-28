#include "bgm.h"
//#include "skill.h"
//#include "standard.h"
#include "clientplayer.h"
#include "engine.h"
#include "settings.h"
#include "standard-generals.h"
//#include "util.h"
//#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "skill-instance-utils.h"
#include <QScopeGuard>
#include <memory>

namespace {

ActiveSkillCard *bgmProxy(const ViewAsSkillV2 *skill, const ActiveSkillRequest &request, bool mute = false)
{
    ActiveSkillCard *card = new ActiveSkillCard;
    card->setActiveSkill(skill);
    card->setSkillName(skill->objectName());
    card->addSubcards(request.selectedCardIds);
    card->setUserString(request.userString);
    card->setMute(mute);
    return card;
}

int chooseHantongEdict(Room *room, ServerPlayer *player)
{
    if (!player || !player->isAlive()) return -1;
    const QList<int> edict = player->getPile("edict");
    if (edict.isEmpty()) return -1;
    room->fillAG(edict, player);
    const auto clearSelection = qScopeGuard([room, player] { room->clearAG(player); });
    return room->askForAG(player, edict, false, "hantong");
}

bool payHantongEdict(Room *room, ServerPlayer *player, int id)
{
    if (!player || !player->isAlive() || !player->getPile("edict").contains(id)) return false;
    player->peiyin("hantong", 2);
    room->throwCard(Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE,
                    player->objectName(), "hantong_acquire", ""), nullptr);
    return true;
}

int grantHantongSkill(Room *room, ServerPlayer *player, const SkillInstanceRef &source, const QString &name)
{
    const int id = room->acquireSkill(player, name);
    if (id <= 0) return 0;
    // Acquired lord skills remain usable by a non-lord and expire at turn end.
    // Keep their exact IDs as applied receipts, even if Hantong itself is lost.
    QVariantList grants = player->getTag("HantongGrants").toList();
    grants << QVariantMap{{"source_owner", source.ownerObjectName}, {"source_instance", source.key.instanceID},
                          {"skill", name}, {"instance", id}};
    player->setTag("HantongGrants", grants);
    player->setTag("Hantong_use", true);
    return id;
}

}

class Kuiwei : public TriggerSkillV2
{
public:
    Kuiwei() : TriggerSkillV2("kuiwei")
    {
        events << EventPhaseStart << Death;
        global = true;
    }

    Frequency getFrequency(const Player *player = nullptr) const override
    {
        return player ? Compulsory : NotFrequent;
    }

    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override
    { return ctx.activationRef.isValid() ? ctx.owner : ctx.invoker; }

    static int getWeaponCount(ServerPlayer *caoren)
    {
        int n = 0;
        foreach (ServerPlayer *p, caoren->getRoom()->getAlivePlayers()) {
            if (p->getWeapon()) n++;
        }
        return n;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &) const override
    {
        return event == EventPhaseStart && target && target->isAlive()
            && target->getPhase() == Player::Finish && target->hasSkill(objectName())
            ? TriggerList{{target, {objectName()}}} : TriggerList();
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || player->getPhase() != Player::Draw) return false;
        if (!player->isAlive()) return true;
        for (const QVariant &receipt : player->getTag("KuiweiReceipts").toList()) {
            const QVariantMap saved = receipt.toMap();
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = room->findChild<ServerPlayer *>(saved.value("owner").toString());
            if (!ctx.owner) continue;
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = saved.value("instance").toInt();
            ctx.sourceRef = SkillInstanceRef(saved.value("owner").toString(),
                                             SkillInstanceKey(objectName(), ctx.instanceID));
            // The discard is an existing liability, even after its grant is retired.
            ctx.extra_data = receipt;
            ctx.amount = saved.value("amount").toInt();
            ctx.targets << player;
            ctx.original_data = &data;
            ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }

    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return ctx.owner && ctx.owner->isAlive() && TriggerSkillV2::isSourceAvailable(room, ctx);
        return ctx.invoker && ctx.invoker->isAlive() && ctx.invoker->getPhase() == Player::Draw
            && ctx.invoker->getTag("KuiweiReceipts").toList().contains(ctx.extra_data);
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == Death && player && data.value<DeathStruct>().who == player) {
            player->removeTag("KuiweiReceipts");
            room->setPlayerMark(player, "@kuiwei", 0);
        }
        return false;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *caoren, SkillContext &ctx) const override
    {
        if (!ctx.activationRef.isValid()) return true;
        if (!caoren->askForSkillInvoke(objectName() + "$-1")) return false;
        ctx.targets << caoren;
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *caoren, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (ctx.activationRef.isValid()) {
            const int amount = qMax(0, getEffectiveAmount(ctx));
            target->drawCards((getWeaponCount(target) + 2) * amount, objectName());
            if (!target->isAlive()) return false;
            target->turnOver();
            if (!target->isAlive()) return false;
            QVariantList receipts = target->getTag("KuiweiReceipts").toList();
            for (int i = receipts.size() - 1; i >= 0; --i) {
                const QVariantMap receipt = receipts.at(i).toMap();
                if (receipt.value("owner").toString() == ctx.activationRef.ownerObjectName
                    && receipt.value("instance").toInt() == ctx.activationRef.key.instanceID)
                    receipts.removeAt(i);
            }
            receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName},
                                    {"instance", ctx.activationRef.key.instanceID}, {"amount", amount}};
            target->setTag("KuiweiReceipts", receipts);
            room->setPlayerMark(target, "@kuiwei", receipts.size());
        } else {
            ServerPlayer *debtor = ctx.invoker;
            QVariantList receipts = debtor->getTag("KuiweiReceipts").toList();
            if (!receipts.removeOne(ctx.extra_data)) return false;
            debtor->setTag("KuiweiReceipts", receipts);
            room->setPlayerMark(debtor, "@kuiwei", receipts.size());
            const int n = getWeaponCount(target) * qMax(0, getEffectiveAmount(ctx));
            if (n > 0) {
                LogMessage log;
                log.type = "#KuiweiDiscard";
                log.from = target;
                log.arg = QString::number(n);
                log.arg2 = objectName();
                room->sendLog(log);
                room->askForDiscard(target, objectName(), n, n, false, true);
            }
        }
        return false;
    }
};

class Yanzheng : public ViewAsSkillV2
{
public:
    QString historyKey(const ActiveSkillRequest &) const override { return "Nullification"; }
    Yanzheng() : ViewAsSkillV2("yanzheng", 1)
    {
        setResponseOrUse(true);
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
            || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && request.pattern == "nullification" && request.initiator->getHandcardNum() > request.initiator->getHp();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.size() < 1 && card && card->getEffectiveId() >= 0
            && request.initiator && request.initiator->getEquipsId().contains(card->getEffectiveId());
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request) || request.selectedCardIds.size() != 1) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        if (!originalCard) return nullptr;
        Nullification *ncard = new Nullification(originalCard->getSuit(), originalCard->getNumber());
        ncard->addSubcard(originalCard);
        ncard->setSkillName(objectName());
        return ncard;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest empty = request;
        empty.selectedCardIds.clear();
        return canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first()));
    }
};

class Manjuan : public TriggerSkillV2
{
public:
    Manjuan() : TriggerSkillV2("manjuan")
    {
        events << BeforeCardsMove;
        frequency = Compulsory;
    }

    void doManjuan(ServerPlayer *sp_pangtong, int card_id) const
    {
        Room *room = sp_pangtong->getRoom();
        const bool alreadyBypassed = sp_pangtong->hasFlag("ManjuanInvoke");
        sp_pangtong->setFlags("ManjuanInvoke");
        const auto restoreBypass = qScopeGuard([sp_pangtong, alreadyBypassed] {
            if (!alreadyBypassed) sp_pangtong->setFlags("-ManjuanInvoke");
        });
        QList<int> DiscardPile = room->getDiscardPile(), toGainList;
        const Card *card = Sanguosha->getCard(card_id);
        foreach (int id, DiscardPile) {
            const Card *cd = Sanguosha->getCard(id);
            if (cd->getNumber() == card->getNumber())
                toGainList << id;
        }
        if (toGainList.isEmpty()) return;

        room->fillAG(toGainList, sp_pangtong);
        int id = room->askForAG(sp_pangtong, toGainList, true, objectName());
        room->clearAG(sp_pangtong);
        if (id > -1)
            room->moveCardTo(Sanguosha->getCard(id), sp_pangtong, Player::PlaceHand, true);
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *sp_pangtong, QVariant &data) const override
    {
        if (!sp_pangtong || sp_pangtong->hasFlag("ManjuanInvoke")) return TriggerList();
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return sp_pangtong && sp_pangtong->isAlive() && sp_pangtong->hasSkill(objectName())
            && !room->getTag("FirstRound").toBool() && move.to == sp_pangtong
            && move.to_place == Player::PlaceHand && !move.card_ids.isEmpty()
            ? TriggerList{{sp_pangtong, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *sp_pangtong, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        room->broadcastSkillInvoke(objectName());
        LogMessage log;
        log.type = "$ManjuanGot";
        log.from = sp_pangtong;
        log.card_str = ListI2S(move.card_ids).join("+");
        room->sendLog(log);
        room->notifySkillInvoked(sp_pangtong, objectName());
        CardMoveReason reason(CardMoveReason::S_REASON_PUT, sp_pangtong->objectName(), "manjuan", "");
        DummyCard *dummy = new DummyCard(move.card_ids);
        move.card_ids.clear();
        *ctx.original_data = QVariant::fromValue(move);
        room->moveCardTo(dummy, nullptr, nullptr, Player::DiscardPile, reason);
        dummy->deleteLater();
        // Substitution is optional only after the mandatory discard has resolved.
        if (!sp_pangtong->isAlive() || !sp_pangtong->hasFlag("CurrentPlayer")
            || !room->askForSkillInvoke(sp_pangtong, objectName(), *ctx.original_data)) return false;
        foreach (int id, dummy->getSubcards()) {
            doManjuan(sp_pangtong, id);
            if (!sp_pangtong->isAlive()) break;
        }
        return false;
    }
};

class Zuixiang : public TriggerSkillV2
{
public:
    Zuixiang() : TriggerSkillV2("zuixiang")
    {
        events << EventPhaseStart << CardEffected;
        limit_mark = "@sleep";
        frequency = Limited;
        global = true;
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    Frequency getFrequency(const Player *player = nullptr) const override
    {
        return player && !player->getPile("dream").isEmpty() ? Compulsory : Limited;
    }

    void doZuixiang(ServerPlayer *player, int amount) const
    {
        Room *room = player->getRoom();
        room->broadcastSkillInvoke("zuixiang");
        if (player->getPile("dream").isEmpty())
            room->doSuperLightbox(player, "zuixiang");

        QList<int> ids = room->getNCards(3 * qMax(0, amount), false);
        if (ids.isEmpty()) return;
        CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, player->objectName(), "zuixiang", ""));
        room->moveCardsAtomic(move, true);

        room->getThread()->delay();

        player->addToPile("dream", ids, true);

        QSet<int> numbers;
        ids = player->getPile("dream");
        foreach (int id, ids) {
            const Card *card = Sanguosha->getCard(id);
            if (numbers.contains(card->getNumber())) {
				player->addMark("zuixiangHasTrigger");

                player->removeTag("ZuixiangReceipt");
	
				LogMessage log;
				log.type = "$ZuixiangGot";
				log.from = player;
	
				log.card_str = ListI2S(ids).join("+");
				room->sendLog(log);
	
				const bool alreadyBypassed = player->hasFlag("ManjuanInvoke");
				player->setFlags("ManjuanInvoke");
                const auto restoreBypass = qScopeGuard([player, alreadyBypassed] {
                    if (!alreadyBypassed) player->setFlags("-ManjuanInvoke");
                });
				CardsMoveStruct move(ids, player, Player::PlaceHand,
					CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName(), "zuixiang", ""));
				room->moveCardsAtomic(move, true);
                break;
            }
            numbers.insert(card->getNumber());
        }
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &data) const override
    {
        if (!target || !target->isAlive()) return TriggerList();
        if (event == EventPhaseStart)
            return target->getPhase() == Player::Start && target->getPile("dream").isEmpty()
                && target->hasSkill(objectName()) && target->getMark("@sleep") > 0
                ? TriggerList{{target, {objectName()}}} : TriggerList();
        if (event != CardEffected || !target->hasSkill(objectName())) return TriggerList();
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!effect.card) return TriggerList();
        foreach (int id, target->getPile("dream")) {
            if (Sanguosha->getCard(id)->getTypeId() == effect.card->getTypeId())
                return TriggerList{{target, {objectName()}}};
        }
        return TriggerList();
    }

    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || player->getPile("dream").isEmpty()) return false;
        if (!player->isAlive() || player->getPhase() != Player::Start) return true;
        const QVariantMap receipt = player->getTag("ZuixiangReceipt").toMap();
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = player;
        ctx.instanceID = receipt.value("instance").toInt();
        ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(),
                                         SkillInstanceKey(objectName(), ctx.instanceID));
        ctx.amount = receipt.value("amount", getBaseAmount()).toInt();
        ctx.original_data = &data;
        ctx.current_event = event;
        ctx.targets << player;
        contexts << ctx;
        return true;
    }

    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        // An existing dream pile continues at Start even if Zuixiang was lost.
        return ctx.owner->getPhase() == Player::Start && !ctx.owner->getPile("dream").isEmpty();
    }

    bool cost(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart && ctx.activationRef.isValid()
            && (!isUsable(ctx) || player->getMark("@sleep") <= 0 || !player->askForSkillInvoke(objectName())))
            return false;
        if (!ctx.targets.contains(player)) ctx.targets << player;
        return true;
    }

    bool pay(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != EventPhaseStart || !ctx.activationRef.isValid()) return true;
        if (!isUsable(ctx) || player->getMark("@sleep") <= 0) return false;
        room->removePlayerMark(player, "@sleep");
        return true;
    }

    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart && ctx.activationRef.isValid()) addUsage(ctx);
        return false;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *sp_pangtong) const override
    {
        if (event == EventPhaseStart) {
            if (ctx.activationRef.isValid())
                sp_pangtong->setTag("ZuixiangReceipt", QVariantMap{{"owner", ctx.activationRef.ownerObjectName},
                    {"instance", ctx.activationRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}});
            doZuixiang(sp_pangtong, getEffectiveAmount(ctx));
        } else {
			const CardEffectStruct effect = ctx.original_data ? ctx.original_data->value<CardEffectStruct>() : CardEffectStruct();
            foreach (int id, sp_pangtong->getPile("dream")) {
                if (effect.card && Sanguosha->getCard(id)->getTypeId() == effect.card->getTypeId()) {
					LogMessage log;
					log.type = "#ZuiXiang2";
					log.from = effect.to;
					if (effect.from){
						log.to << effect.from;
						log.type = "#ZuiXiang1";
					}
					log.arg = effect.card->objectName();
					log.arg2 = objectName();
					room->sendLog(log);
					room->broadcastSkillInvoke(objectName());
                    return true;
                }
            }
        }
        return false;
    }
};

class ZuixiangClear : public CardLimitSkill
{
public:
    ZuixiangClear() : CardLimitSkill("#zuixiang-limit")
    {
    }

    QString limitList(const Player *) const
    {
        return "use,response";
    }

    QString limitPattern(const Player *target) const
    {
        if (target->hasSkill("zuixiang")){
            QStringList trs;
			foreach (int id, target->getPile("dream"))
				trs << Sanguosha->getCard(id)->getType();
            return trs.join(",");
		}
        return "";
    }
};

class Jie : public TriggerSkillV2
{
public:
    Jie() : TriggerSkillV2("jie")
    {
        events << DamageCaused;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(objectName())
            && !damage.chain && !damage.transfer && damage.by_user
            && damage.card && damage.card->isKindOf("Slash") && damage.card->isRed()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        LogMessage log;
        log.type = "#Jie";
        log.from = player;
        log.to << damage.to;
        log.arg = QString::number(damage.damage);
        damage.damage += getEffectiveAmount(ctx);
        log.arg2 = QString::number(damage.damage);
        room->sendLog(log);
        room->notifySkillInvoked(player, objectName());
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

DaheCard::DaheCard()
{
    setSkillName("dahe");
    mute = true;
}

bool DaheCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void DaheCard::use(Room *room, ServerPlayer *zhangfei, QList<ServerPlayer *> &targets) const
{
	if (targets.first()->getGeneralName().contains("lvbu"))
		room->broadcastSkillInvoke("dahe", 2);
	else
		room->broadcastSkillInvoke("dahe", 1);
	PindianStruct *pd = zhangfei->PinDian(targets.first(), "dahe");
	if(pd->success){
		room->addPlayerMark(targets.first(), "&dahe-Clear");
		QList<ServerPlayer *> to_givelist;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->getHp() <= zhangfei->getHp())
				to_givelist << p;
		}
		ServerPlayer *to_give = room->askForPlayerChosen(zhangfei, to_givelist, "dahe", "@dahe-give", true);
		if (!to_give) return;
		CardMoveReason reason(CardMoveReason::S_REASON_GIVE, zhangfei->objectName(), to_give->objectName(), "dahe", "");
		to_give->obtainCard(pd->to_card);
	}else{
		if (!zhangfei->isKongcheng()) {
			room->showAllCards(zhangfei);
			room->askForDiscard(zhangfei, "dahe", 1, 1, false, false);
		}
	}
}

class DaheViewAsSkill : public ViewAsSkillV2
{
public:
    DaheViewAsSkill() : ViewAsSkillV2("dahe")
    {
        setPhaseName("Play");
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canPindian();
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return request.initiator && selected.isEmpty() && candidate
            && request.initiator->canPindian(candidate);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.size() == 1;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return bgmProxy(this, request, true);
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "DaheCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *actor = ctx.invoker;
        if (!actor || !target) return FinishSkill;
        Room *room = actor->getRoom();
        if (ctx.choice == "give-pindian") {
            const int id = ctx.extra_data.toInt();
            if (id >= 0 && room->getCardPlace(id) == Player::DiscardPile)
                room->obtainCard(target, id);
            return FinishSkill;
        }
        if (ctx.choice == "lose-pindian") {
            room->showAllCards(target);
            const int count = getEffectiveAmount(ctx);
            if (count > 0) room->askForDiscard(target, "dahe", count, count, false, false);
            return FinishSkill;
        }
        if (!actor->canPindian(target)) return FinishSkill;
        room->broadcastSkillInvoke("dahe", target->getGeneralName().contains("lvbu") ? 2 : 1);
        const std::unique_ptr<PindianStruct> pd(actor->PinDian(target, "dahe"));
        if (!pd) return FinishSkill;
        if (pd->success) {
            QVariantList receipts = target->getTag("DaheReceipts").toList();
            receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName}, {"instance", ctx.activationRef.key.instanceID}};
            target->setTag("DaheReceipts", receipts);
            room->addPlayerMark(target, "&dahe-Clear");
            QList<ServerPlayer *> candidates;
            foreach (ServerPlayer *p, room->getAlivePlayers())
                if (p->getHp() <= actor->getHp()) candidates << p;
            ServerPlayer *receiver = actor->isAlive()
                ? room->askForPlayerChosen(actor, candidates, "dahe", "@dahe-give", true) : nullptr;
            if (receiver && pd->to_card && room->getCardPlace(pd->to_card->getEffectiveId()) == Player::DiscardPile) {
                SkillContext gift = ctx;
                gift.choice = "give-pindian";
                gift.extra_data = pd->to_card->getEffectiveId();
                gift.targets = {receiver};
                skillEffect(gift, receiver);
            }
        } else if (actor->isAlive() && !actor->isKongcheng()) {
            SkillContext loss = ctx;
            loss.choice = "lose-pindian";
            loss.targets = {actor};
            skillEffect(loss, actor);
        }
        return FinishSkill;
    }
};

class Dahe : public TriggerSkillV2
{
public:
    Dahe() : TriggerSkillV2("dahe")
    {
        events << CardUsed << EventPhaseChanging;
        view_as_skill = new DaheViewAsSkill;
        global = true;
        frequency = Compulsory;
    }

    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != CardUsed || !player || !player->isAlive()) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Jink") || use.card->getSuit() == Card::Heart
            || use.nullified_list.contains("_ALL_TARGETS")) return true;
        for (const QVariant &receipt : player->getTag("DaheReceipts").toList()) {
            const QVariantMap saved = receipt.toMap();
            ServerPlayer *owner = room->findChild<ServerPlayer *>(saved.value("owner").toString());
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = owner;
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = saved.value("instance").toInt();
            ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(objectName(), ctx.instanceID));
            ctx.extra_data = receipt;
            ctx.targets << player;
            ctx.original_data = &data;
            ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }

    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.original_data || !ctx.invoker->isAlive()) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        return use.card && use.card->isKindOf("Jink") && use.card->getSuit() != Card::Heart
            && !use.nullified_list.contains("_ALL_TARGETS")
            && ctx.invoker->getTag("DaheReceipts").toList().contains(ctx.extra_data);
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            for (ServerPlayer *player : room->getAllPlayers(true)) player->removeTag("DaheReceipts");
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (!ctx.original_data || !ctx.owner || target != ctx.invoker) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        use.nullified_list << "_ALL_TARGETS";
        *ctx.original_data = QVariant::fromValue(use);
        LogMessage log;
        log.type = "#DaheEffect";
        log.from = ctx.owner;
        if (ctx.invoker) log.to << ctx.invoker;
        log.arg = use.card->getSuitString();
        log.arg2 = "dahe";
        room->sendLog(log);
        return false;
    }
};

TanhuCard::TanhuCard()
{
    setSkillName("tanhu");
}

bool TanhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void TanhuCard::use(Room *room, ServerPlayer *lvmeng, QList<ServerPlayer *> &targets) const
{
    if (lvmeng->pindian(targets.first(), "tanhu")) {
        room->broadcastSkillInvoke("tanhu", 2);
        targets.first()->setFlags("TanhuTarget");
        lvmeng->setTag("TanhuInvoke", QVariant::fromValue(targets.first()));
        room->setFixedDistance(lvmeng, targets.first(), 1);
    } else
        room->broadcastSkillInvoke("tanhu", 3);
}

class TanhuViewAsSkill : public ViewAsSkillV2
{
public:
    TanhuViewAsSkill() : ViewAsSkillV2("tanhu")
    {
        setPhaseName("Play");
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canPindian();
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return request.initiator && selected.isEmpty() && candidate
            && request.initiator->canPindian(candidate);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.size() == 1;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return bgmProxy(this, request, true);
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "TanhuCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *actor = ctx.invoker;
        if (!actor || !target || !actor->canPindian(target)) return FinishSkill;
        if (actor->pindian(target, "tanhu")) {
            actor->getRoom()->broadcastSkillInvoke("tanhu", 2);
            target->setFlags("TanhuTarget");
            // Applied receipts survive removal of the grant and never store player pointers.
            QVariantList receipts = actor->getTag("TanhuReceipts").toList();
            QVariantMap receipt;
            receipt.insert("owner", ctx.activationRef.ownerObjectName);
            receipt.insert("instance", ctx.activationRef.key.instanceID);
            receipt.insert("target", target->objectName());
            receipts << receipt;
            actor->setTag("TanhuReceipts", receipts);
            actor->getRoom()->setFixedDistance(actor, target, 1);
        } else {
            actor->getRoom()->broadcastSkillInvoke("tanhu", 3);
        }
        return FinishSkill;
    }
};

class Tanhu : public TriggerSkillV2
{
public:
    Tanhu() : TriggerSkillV2("tanhu")
    {
        events << EventPhaseChanging << Death << TrickCardCanceling;
        view_as_skill = new TanhuViewAsSkill;
        frequency = Compulsory;
        global = true;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || (event != EventPhaseChanging && event != Death)) return false;
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        if (event == Death && data.value<DeathStruct>().who != player) return false;
        const QVariantList receipts = player->getTag("TanhuReceipts").toList();
        player->removeTag("TanhuReceipts");
        for (const QVariant &value : receipts) {
            ServerPlayer *target = room->findPlayerByObjectName(value.toMap().value("target").toString(), true);
            if (!target) continue;
            room->removeFixedDistance(player, target, 1);
            bool stillApplied = false;
            for (ServerPlayer *other : room->getAllPlayers(true)) {
                for (const QVariant &otherValue : other->getTag("TanhuReceipts").toList())
                    if (otherValue.toMap().value("target").toString() == target->objectName()) stillApplied = true;
            }
            if (!stillApplied) target->setFlags("-TanhuTarget");
        }
        return true;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != TrickCardCanceling) return true;
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        // TrickCardCanceling is dispatched to each potential nullifier, not effect.to.
        if (!effect.from || !player || !player->isAlive()) return true;
        for (const QVariant &value : effect.from->getTag("TanhuReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("target").toString() != player->objectName()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = effect.from;
            ctx.invoker = player;
            ctx.initiator = effect.from;
            ctx.instanceID = receipt.value("instance").toInt();
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(),
                                             SkillInstanceKey(objectName(), ctx.instanceID));
            ctx.targets << player;
            ctx.amount = getBaseAmount();
            ctx.original_data = &data;
            ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }

    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.invoker || !ctx.original_data) return false;
        const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        if (effect.from != ctx.owner || !ctx.invoker->isAlive()) return false;
        for (const QVariant &value : ctx.owner->getTag("TanhuReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("owner").toString() == ctx.sourceRef.ownerObjectName
                && receipt.value("instance").toInt() == ctx.sourceRef.key.instanceID
                && receipt.value("target").toString() == ctx.invoker->objectName()) return true;
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &, ServerPlayer *) const override
    {
        return true;
    }

    int getEffectIndex(const ServerPlayer *, const Card *) const override { return 1; }
};

class Mouduan : public TriggerSkillV2
{
public:
    Mouduan() : TriggerSkillV2("mouduan")
    {
        events << EventPhaseStart << CardsMoveOneTime << GameStart << EventAcquireSkill << EventLoseSkill;
    }

    static QString stance(const ServerPlayer *owner, int id)
    {
        return owner->getSkillInstanceStateValue("mouduan", id, "stance").toString();
    }

    static void projectStances(Room *room, ServerPlayer *owner)
    {
        int wu = 0, wen = 0;
        for (int id : owner->getSkillInstanceIds("mouduan")) {
            const QString current = stance(owner, id);
            if (current == "wu") ++wu;
            else if (current == "wen") ++wen;
        }
        // Public marks are UI/AI projections; the individual instance owns its stance.
        room->setPlayerMark(owner, "@wu", wu);
        room->setPlayerMark(owner, "@wen", wen);
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventLoseSkill || !player || data.toString() != objectName()) return false;
        // The runtime removes exact attached children before destroying their parent.
        projectStances(room, player);
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player) return result;
        QList<ServerPlayer *> owners;
        if (event == EventPhaseStart && player->getPhase() == Player::RoundStart)
            owners = room->findPlayersBySkillName(objectName());
        else if (event == GameStart || event == CardsMoveOneTime
                 || (event == EventAcquireSkill && data.toString() == objectName()))
            owners << player;
        else return result;
        for (ServerPlayer *owner : owners) {
            if (!owner->isAlive()) continue;
            for (int id : owner->getValidSkillInstanceIds(objectName())) {
                const QString current = stance(owner, id);
                bool eligible = (event == GameStart || event == EventAcquireSkill) && current.isEmpty();
                if (event == CardsMoveOneTime)
                    eligible = data.value<CardsMoveOneTimeStruct>().from == owner
                        && owner->getHandcardNum() <= 2 && current != "wen";
                else if (event == EventPhaseStart)
                    eligible = current == "wen" && owner->canDiscard(owner, "he");
                if (eligible) result[owner] << SkillInstanceUtils::formatName(objectName(), id);
            }
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event != EventPhaseStart) return true;
        const Card *card = room->askForCard(owner, "..", "@mouduan", QVariant(), Card::MethodNone,
                                            nullptr, false, objectName());
        const int id = card ? card->getEffectiveId() : -1;
        if (id < 0 || !owner->canDiscard(id)) return false;
        ctx.extra_data = id;
        return true;
    }

    bool pay(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event != EventPhaseStart) return true;
        const int id = ctx.extra_data.toInt();
        if (room->getCardOwner(id) != owner || !owner->canDiscard(id)) return false;
        room->throwCard(id, objectName(), owner, owner);
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner || !ctx.activationRef.isValid()) return false;
        if (event == EventPhaseStart && owner->getHandcardNum() <= 2) return false;
        const QString next = event == CardsMoveOneTime ? "wen" : "wu";
        const int instanceId = ctx.activationRef.key.instanceID;
        const QVariantList previous = owner->getSkillInstanceStateValue(objectName(), instanceId, "grants").toList();
        owner->setSkillInstanceStateValue(objectName(), instanceId, "stance", next);
        owner->setSkillInstanceStateValue(objectName(), instanceId, "grants", QVariantList());
        // Retire only grants from this exact Mouduan instance, preserving innate/sibling copies.
        for (const QVariant &value : previous) {
            const QVariantMap grant = value.toMap();
            room->detachAttachedSkill(SkillInstanceRef(owner->objectName(),
                SkillInstanceKey(grant.value("skill").toString(), grant.value("instance").toInt())));
        }
        QVariantList grants;
        const QStringList names = next == "wu" ? QStringList{"jiang", "qianxun"} : QStringList{"yingzi", "keji"};
        for (const QString &name : names) {
            if (!owner->hasSkillInstance(objectName(), instanceId)) break;
            const SkillInstanceRef child = room->attachSkillToPlayer(owner, name, ctx.activationRef, true);
            if (!child.isValid()) continue;
            QVariantMap grant;
            grant.insert("skill", child.key.skillName);
            grant.insert("instance", child.key.instanceID);
            grants << grant;
        }
        if (owner->hasSkillInstance(objectName(), instanceId))
            owner->setSkillInstanceStateValue(objectName(), instanceId, "grants", grants);
        projectStances(room, owner);
        room->broadcastSkillInvoke(objectName());
        if (event != EventPhaseStart) room->sendCompulsoryTriggerLog(owner, objectName());
        return false;
    }
};

class Zhaolie : public TriggerSkillV2
{
public:
    Zhaolie() : TriggerSkillV2("zhaolie")
    {
        events << DrawNCards;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != DrawNCards || !player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        const DrawStruct draw = data.value<DrawStruct>();
        return draw.reason == "draw_phase" ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *liubei, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> victims;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (liubei->inMyAttackRange(p)) victims << p;
        ServerPlayer *victim = room->askForPlayerChosen(liubei, victims, "zhaolie$-1", "zhaolie-invoke", true, true);
        if (!victim) return false;
        ctx.targets << victim;
        return true;
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num--;
        *ctx.original_data = QVariant::fromValue(draw);
        return true;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *liubei, SkillContext &ctx,
                      ServerPlayer *victim) const override
    {
        if (ctx.choice == "obtain-revealed") {
            QList<int> available;
            for (int id : ctx.extra_data.value<QList<int>>())
                if (room->getCardPlace(id) == Player::PlaceTable) available << id;
            if (!available.isEmpty()) {
                DummyCard cards(available);
                room->obtainCard(victim, &cards);
            }
            return false;
        }
        QList<int> ids;
        int no_basic = 0;
        DummyCard remaining;
        DummyCard *dummy = &remaining;
        const QList<int> revealed = room->showDrawPile(liubei, 3 * getEffectiveAmount(ctx), "zhaolie");
        const auto clearRevealed = qScopeGuard([room, revealed] {
            QList<int> stranded;
            for (int id : revealed)
                if (room->getCardPlace(id) == Player::PlaceTable) stranded << id;
            if (!stranded.isEmpty()) room->throwCard(stranded, "zhaolie", nullptr);
        });
        foreach (int card_id, revealed) {
            const Card *card = Sanguosha->getCard(card_id);
            if (!card->isKindOf("BasicCard") || card->isKindOf("Peach")) {
                if (!card->isKindOf("BasicCard")) no_basic++;
                ids << card_id;
            } else dummy->addSubcard(card_id);
        }
        room->getThread()->delay();
        room->throwCard(ids, "zhaolie", nullptr);
        if (!victim->isAlive()) return false;
        if (no_basic < 1 && dummy->subcardsLength() < 1) {
            return false;
        }
        const auto giveRemaining = [&](ServerPlayer *receiver) {
            SkillContext gift = ctx;
            gift.choice = "obtain-revealed";
            gift.extra_data = QVariant::fromValue(dummy->getSubcards());
            gift.targets = {receiver};
            skillEffect(event, room, liubei, gift, receiver);
        };
        if (no_basic == 0) {
            if (room->askForSkillInvoke(victim, "zhaolie_obtain", "obtain:" + liubei->objectName(), false))
                giveRemaining(liubei);
            else giveRemaining(victim);
        } else if (victim->canDiscard("he")
                   && room->askForDiscard(victim, "zhaolie", no_basic, no_basic, true, true,
                                          "@zhaolie-discard:" + liubei->objectName())) {
            if (liubei->isAlive()) giveRemaining(liubei);
            else room->throwCard(dummy, "zhaolie", nullptr);
        } else {
            room->damage(DamageStruct("zhaolie", liubei, victim, no_basic));
            if (victim->isAlive()) giveRemaining(victim);
            else room->throwCard(dummy, "zhaolie", nullptr);
        }
        return false;
    }
};
ShichouCard::ShichouCard()
{
    setSkillName("shichou");
    will_throw = false;
    mute = true;
    handling_method = Card::MethodNone;
}

bool ShichouCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->getKingdom() == "shu" && to_select != Self;
}

void ShichouCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    ServerPlayer *player = effect.from, *victim = effect.to;

    if (!player->isLord() && player->hasSkill("weidi")) {
        room->broadcastSkillInvoke("weidi");
    } else {
        room->broadcastSkillInvoke("shichou");
    }
	room->doSuperLightbox(player, "shichou");

    room->removePlayerMark(player, "@hate");
    room->setPlayerMark(player, "xhate", 1);
    victim->gainMark("@hate_to");
    room->setPlayerMark(victim, "hate_" + player->objectName(), 1);

    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), victim->objectName(), "shichou", "");
    room->obtainCard(victim, this, reason, false);
}

class ShichouViewAsSkill : public ViewAsSkillV2
{
public:
    ShichouViewAsSkill() : ViewAsSkillV2("shichou", 2)
    {
        response_pattern = "@@shichou";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@shichou"
            && request.initiator->getMark("@hate") > 0 && request.initiator->hasLordSkill("shichou")
            && request.initiator->getPhase() == Player::Start;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.size() < 2
            && card->getEffectiveId() >= 0
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId()));
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 2;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return request.initiator && selected.isEmpty() && candidate && candidate != request.initiator
            && candidate->getKingdom() == "shu";
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.size() == 1;
    }

    bool willThrowSelectedCards() const override { return false; }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? bgmProxy(this, request, true) : nullptr;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ShichouCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        ServerPlayer *payer = ctx.initiator;
        if (!room || !payer || payer->getMark("@hate") <= 0) return false;
        for (int id : request.selectedCardIds)
            if (room->getCardOwner(id) != payer) return false;
        room->removePlayerMark(payer, "@hate");
        return true;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *victim) const override
    {
        ServerPlayer *actor = ctx.invoker;
        if (!actor || !actor->isAlive() || !victim || !ctx.use_card) return FinishSkill;
        Room *room = actor->getRoom();
        if (!actor->isLord() && actor->hasSkill("weidi")) room->broadcastSkillInvoke("weidi");
        else room->broadcastSkillInvoke("shichou");
        room->doSuperLightbox(actor, "shichou");
        QVariantList receipts = actor->getTag("ShichouReceipts").toList();
        receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName},
                                {"instance", ctx.activationRef.key.instanceID}, {"target", victim->objectName()}};
        actor->setTag("ShichouReceipts", receipts);
        room->setPlayerMark(actor, "xhate", 1);
        victim->gainMark("@hate_to");
        room->setPlayerMark(victim, "hate_" + actor->objectName(), 1);
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, ctx.initiator->objectName(), victim->objectName(), "shichou", "");
        room->obtainCard(victim, ctx.use_card, reason, false);
        return FinishSkill;
    }
};

class Shichou : public TriggerSkillV2
{
public:
    Shichou() : TriggerSkillV2("shichou$")
    {
        events << EventPhaseStart << DamageInflicted;
        frequency = Limited;
        limit_mark = "@hate";
        view_as_skill = new ShichouViewAsSkill;
    }

    Frequency getFrequency(const Player *player = nullptr) const override
    { return player ? Compulsory : Limited; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return TriggerList();
        if (event == EventPhaseStart)
            return player->isAlive() && player->getPhase() == Player::Start
                && player->getMark("@hate") > 0 && player->hasLordSkill(this) && player->getCards("he").length() > 1
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        if (event == DamageInflicted) {
            if (!player->isAlive() || !player->hasLordSkill(this) || player->getMark("ShichouTarget") != 0)
                return TriggerList();
            TriggerList result;
            for (const QVariant &value : player->getTag("ShichouReceipts").toList()) {
                const QVariantMap receipt = value.toMap();
                const int id = receipt.value("instance").toInt();
                ServerPlayer *recipient = player->getRoom()->findChild<ServerPlayer *>(receipt.value("target").toString());
                if (receipt.value("owner").toString() == player->objectName()
                    && player->getValidSkillInstanceIds(objectName()).contains(id)
                    && recipient && recipient->isAlive() && recipient->getMark("@hate_to") > 0)
                    result[player] << SkillInstanceUtils::formatName(objectName(), id);
            }
            return result;
        }
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
            const int previous = player->getMark(selector);
            room->setPlayerMark(player, selector, ctx.activationRef.key.instanceID);
            const auto restoreSource = qScopeGuard([room, player, selector, previous] {
                room->setPlayerMark(player, selector, previous);
            });
            room->askForUseCard(player, "@@shichou", "@shichou-give", -1, Card::MethodNone);
        } else if (event == DamageInflicted) {
            ServerPlayer *target = nullptr;
            for (const QVariant &value : player->getTag("ShichouReceipts").toList()) {
                const QVariantMap receipt = value.toMap();
                if (receipt.value("owner").toString() == ctx.activationRef.ownerObjectName
                    && receipt.value("instance").toInt() == ctx.activationRef.key.instanceID) {
                    target = room->findChild<ServerPlayer *>(receipt.value("target").toString());
                    break;
                }
            }
            if (!target || target->isDead() || target->getMark("@hate_to") <= 0 || !ctx.original_data) return false;
            ctx.targets << target;
            ctx.manual_effect = true;
            return skillEffect(event, room, player, ctx, target);
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
            if (!ctx.original_data || !target->isAlive() || target->getMark("@hate_to") <= 0) return false;
            LogMessage log;
            log.type = "#ShichouProtect";
            log.arg = objectName();
            log.from = player;
            log.to << target;
            room->sendLog(log);
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            damage.to = target;
            damage.transfer = true;
            damage.transfer_reason = "shichou";
            *ctx.original_data = QVariant::fromValue(damage);
            player->setTag("TransferDamage", QVariant::fromValue(damage));
            return true;
    }
};

class ShichouEffect : public TriggerSkillV2
{
public:
    ShichouEffect() : TriggerSkillV2("#shichou-effect")
    {
        events << Dying << DamageComplete;
        global = true;
        frequency = Compulsory;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (!player || !player->isAlive()) return true;
        if (event == Dying) {
            if (data.value<DyingStruct>().who != player || player->getMark("@hate_to") <= 0) return true;
        } else {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.to != player || !damage.transfer || damage.transfer_reason != "shichou") return true;
        }
        // The recipient need not own Shichou. The transferred damage is the rule's source.
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = player;
        ctx.targets << player;
        ctx.original_data = &data;
        ctx.current_event = event;
        ctx.amount = event == DamageComplete ? data.value<DamageStruct>().damage : getBaseAmount();
        contexts << ctx;
        return true;
    }

    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive() || !ctx.original_data) return false;
        if (ctx.current_event == Dying)
            return ctx.original_data->value<DyingStruct>().who == ctx.owner && ctx.owner->getMark("@hate_to") > 0;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        return damage.to == ctx.owner && damage.transfer && damage.transfer_reason == "shichou";
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (event == DamageComplete) {
            target->drawCards(getEffectiveAmount(ctx), "shichou");
        } else {
            player->loseAllMarks("@hate_to");
            for (ServerPlayer *owner : room->getAllPlayers(true)) {
                QVariantList receipts = owner->getTag("ShichouReceipts").toList();
                for (int i = receipts.size() - 1; i >= 0; --i)
                    if (receipts.at(i).toMap().value("target").toString() == player->objectName()) receipts.removeAt(i);
                owner->setTag("ShichouReceipts", receipts);
                room->setPlayerMark(player, "hate_" + owner->objectName(), 0);
            }
        }
        return false;
    }
};
YanxiaoCard::YanxiaoCard(Suit suit, int number)
    : DelayedTrick(suit, number)
{
    mute = true;
    handling_method = Card::MethodNone;
    setObjectName("YanxiaoCard");
    will_throw = false;
}

bool YanxiaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (Self->isProhibited(to_select,this))
        return false;
    return targets.isEmpty();
}

void YanxiaoCard::onUse(Room *room, CardUseStruct &card_use) const
{
    int x = qsanRandomBounded(2)+1;
	if(card_use.from->getGeneralName().contains("daqiao")){
		x = 1;
		foreach(ServerPlayer *to, card_use.to)
			if (to->getGeneralName().contains("sunce"))
				x = 2;
	}else if(hasFlag("JINGYIN"))
		x = 0;
    room->broadcastSkillInvoke("yanxiao", x, card_use.from);
    CardMoveReason reason(CardMoveReason::S_MASK_BASIC_REASON, card_use.from->objectName(), "yanxiao", "");
	room->moveCardTo(this,nullptr,Player::PlaceTable,reason,true);
    DelayedTrick::onUse(room, card_use);
}

void YanxiaoCard::takeEffect(ServerPlayer *) const
{
}

class YanxiaoViewAsSkill : public ViewAsSkillV2
{
public:
    YanxiaoViewAsSkill() : ViewAsSkillV2("yanxiao", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty()
            && card->getSuit() == Card::Diamond && card->getEffectiveId() >= 0
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId()));
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return nullptr;
        const Card *selected = Sanguosha->getCard(request.selectedCardIds.first());
        if (!selected) return nullptr;
        // This remains a delayed trick; its ordinary card pipeline owns targets and movement.
        YanxiaoCard *card = new YanxiaoCard(selected->getSuit(), selected->getNumber());
        card->setSkillName(objectName());
        card->addSubcard(selected);
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "YanxiaoCard"; }
};

class Yanxiao : public TriggerSkillV2
{
public:
    Yanxiao() : TriggerSkillV2("yanxiao")
    {
        events << EventPhaseStart;
        view_as_skill = new YanxiaoViewAsSkill;
        frequency = Compulsory;
        global = true;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive()
            || player->getPhase() != Player::Judge || !player->containsTrick("YanxiaoCard")) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = player;
        ctx.targets << player;
        ctx.original_data = &data;
        ctx.current_event = event;
        ctx.amount = getBaseAmount();
        contexts << ctx;
        return true;
    }

    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        // The delayed trick applies to its recipient without granting them Yanxiao.
        return ctx.owner && ctx.owner->isAlive() && ctx.owner->getPhase() == Player::Judge
            && ctx.owner->containsTrick("YanxiaoCard");
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    {
        CardsMoveStruct move;
        LogMessage log;
        log.type = "$YanxiaoGot";
        log.from = target;
        foreach (const Card *delayed_trick, target->getJudgingArea())
            move.card_ids << delayed_trick->getEffectiveId();
        log.card_str = ListI2S(move.card_ids).join("+");
        room->sendLog(log);
        move.to = target;
        move.to_place = Player::PlaceHand;
        room->moveCardsAtomic(move, true);
        return false;
    }
};
class Anxian : public TriggerSkillV2
{
public:
    Anxian() : TriggerSkillV2("anxian") { events << DamageCaused << TargetConfirming; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        if (event == DamageCaused) {
            const DamageStruct damage = data.value<DamageStruct>();
            return damage.from == player && damage.card && damage.card->isKindOf("Slash")
                && damage.by_user && !damage.chain && !damage.transfer
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        }
        const CardUseStruct use = data.value<CardUseStruct>();
        return event == TargetConfirming && use.to.contains(player) && use.card
            && use.card->isKindOf("Slash") && player->canDiscard(player, "h")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        if (event == DamageCaused) {
            if (!room->askForSkillInvoke(player, objectName() + "$1", *ctx.original_data)) return false;
            ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
            if (target) ctx.targets << target;
            return true;
        }
        const Card *card = room->askForCard(player, ".", "@anxian-discard", *ctx.original_data,
                                            Card::MethodNone, nullptr, false, objectName() + "$1");
        const int id = card ? card->getEffectiveId() : -1;
        if (id < 0 || !player->handCards().contains(id) || !player->canDiscard(id)) return false;
        ctx.extra_data = id;
        ServerPlayer *source = ctx.original_data->value<CardUseStruct>().from;
        if (source) ctx.targets << source;
        return true;
    }

    bool pay(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != TargetConfirming) return true;
        const int id = ctx.extra_data.toInt();
        if (!player->handCards().contains(id) || !player->canDiscard(id)) return false;
        room->throwCard(id, objectName(), player, player);
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        if (event == DamageCaused) {
            LogMessage log;
            log.type = "#Anxian";
            log.from = player;
            log.arg = objectName();
            room->sendLog(log);
            // Preserve discard, then draw, then prevent damage while exposing target interception.
            ctx.manual_effect = true;
            for (ServerPlayer *target : ctx.targets) {
                SkillContext recipient = ctx;
                skillEffect(event, room, player, recipient, target);
            }
            SkillContext draw = ctx;
            draw.choice = "owner-draw";
            draw.targets = {player};
            skillEffect(event, room, player, draw, player);
            return true;
        }
        ctx.manual_effect = true;
        for (ServerPlayer *target : ctx.targets) {
            SkillContext recipient = ctx;
            skillEffect(event, room, player, recipient, target);
        }
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        use.nullified_list << player->objectName();
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (event == DamageCaused && ctx.choice != "owner-draw") {
            if (target->canDiscard(target, "h")) room->askForDiscard(target, objectName(), amount, amount);
        } else target->drawCards(amount, objectName());
        return false;
    }
};

YinlingCard::YinlingCard()
{
    setSkillName("yinling");
}

bool YinlingCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void YinlingCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    if (!effect.from->canDiscard(effect.to, "he") || effect.from->getPile("brocade").length() >= 4)
        return;
    int card_id = room->askForCardChosen(effect.from, effect.to, "he", "yinling", false, Card::MethodDiscard);
    effect.from->addToPile("brocade", card_id);
}

class Yinling : public ViewAsSkillV2
{
public:
    QString historyKey(const ActiveSkillRequest &) const override { return "YinlingCard"; }
    Yinling() : ViewAsSkillV2("yinling", 1)
    {
        setResponseOrUse(false);
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getPile("brocade").length() < 4;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && card && card->isBlack()
            && request.initiator && card->getEffectiveId() >= 0
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId()))
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return request.initiator && selected.isEmpty() && candidate && candidate != request.initiator
            && candidate->isAlive() && request.initiator->canMove(candidate, "he");
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.size() == 1;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? bgmProxy(this, request) : nullptr;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *actor = ctx.invoker;
        if (!actor || !target) return FinishSkill;
        Room *room = actor->getRoom();
        for (int i = 0; i < getEffectiveAmount(ctx) && actor->isAlive() && target->isAlive()
             && actor->getPile("brocade").size() < 4 && actor->canMove(target, "he"); ++i) {
            const int id = room->askForCardChosen(actor, target, "he", objectName(), false, Card::MethodMove);
            if (id < 0 || room->getCardOwner(id) != target || !actor->canMove(target, id)) break;
            actor->addToPile("brocade", id);
        }
        return ContinueEffects;
    }
};

JunweiCard::JunweiCard()
{
    setSkillName("junwei");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool JunweiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.length() == 0;
}

void JunweiCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();

    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", objectName(), "");
    room->throwCard(this, reason, nullptr);

    ServerPlayer *ganning = effect.from;
    ServerPlayer *target = effect.to;

    QVariant ai_data = QVariant::fromValue(ganning);
    const Card *card = room->askForCard(target, "Jink", "@junwei-show", ai_data, Card::MethodNone);
    if (card) {
        room->showCard(target, card->getEffectiveId());
        ServerPlayer *receiver = room->askForPlayerChosen(ganning, room->getAllPlayers(), "junweigive", "@junwei-give");
        if (receiver != target)
            receiver->obtainCard(card);
    } else {
        room->loseHp(HpLostStruct(target, 1, objectName(), ganning));
        if (!target->isAlive())
            return;
        if (target->hasEquip()) {
            int card_id = room->askForCardChosen(ganning, target, "e", objectName());
            target->addToPile("junwei_equip", card_id);
        }
    }
}

class JunweiVS : public ViewAsSkillV2
{
public:
    JunweiVS() : ViewAsSkillV2("junwei", 3)
    {
        expand_pile = "brocade";
    }

    bool willThrowSelectedCards() const override { return false; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@junwei"
            && request.initiator->getPile("brocade").size() >= 3;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.size() < 3
            && request.initiator->getPile("brocade").contains(card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 3;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return request.initiator && selected.isEmpty() && candidate && candidate->isAlive();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.size() == 1;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? bgmProxy(this, request) : nullptr;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "JunweiCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || request.selectedCardIds.size() != 3) return false;
        QList<int> available = ctx.initiator->getPile("brocade");
        for (int id : request.selectedCardIds)
            if (!available.removeOne(id)) return false;
        DummyCard cards(request.selectedCardIds);
        room->throwCard(&cards, CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE,
            ctx.initiator->objectName(), objectName(), ""), nullptr);
        return true;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *actor = ctx.invoker;
        if (!actor || !actor->isAlive() || !target) return FinishSkill;
        Room *room = actor->getRoom();
        if (ctx.choice == "give-jink") {
            const QVariantMap gift = ctx.extra_data.toMap();
            ServerPlayer *giver = room->findPlayerByObjectName(gift.value("giver").toString(), true);
            const int id = gift.value("card").toInt();
            if (giver && id >= 0 && giver->handCards().contains(id)) room->obtainCard(target, id);
            return FinishSkill;
        }
        const Card *card = room->askForCard(target, "Jink", "@junwei-show",
            QVariant::fromValue(actor), Card::MethodNone);
        if (card) {
            room->showCard(target, card->getEffectiveId());
            ServerPlayer *receiver = room->askForPlayerChosen(actor, room->getAlivePlayers(),
                "junweigive", "@junwei-give");
            if (receiver && receiver != target && target->handCards().contains(card->getEffectiveId())) {
                SkillContext gift = ctx;
                gift.choice = "give-jink";
                gift.extra_data = QVariantMap{{"giver", target->objectName()}, {"card", card->getEffectiveId()}};
                gift.targets = {receiver};
                skillEffect(gift, receiver);
            }
        } else {
            room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), actor));
            if (actor->isAlive() && target->isAlive() && target->hasEquip()) {
                const int card_id = room->askForCardChosen(actor, target, "e", objectName());
                if (target->getEquipsId().contains(card_id)) target->addToPile("junwei_equip", card_id);
            }
        }
        return FinishSkill;
    }
};

class Junwei : public TriggerSkillV2
{
public:
    Junwei() : TriggerSkillV2("junwei")
    {
        events << EventPhaseStart;
        view_as_skill = new JunweiVS;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *ganning, QVariant &) const override
    {
        return ganning && ganning->isAlive() && ganning->hasSkill(objectName())
            && ganning->getPhase() == Player::Finish && ganning->getPile("brocade").length() >= 3
            ? TriggerList{{ganning, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *ganning, SkillContext &ctx) const override
    {
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int previous = ganning->getMark(selector);
        room->setPlayerMark(ganning, selector, ctx.activationRef.key.instanceID);
        const auto restoreSource = qScopeGuard([room, ganning, selector, previous] {
            room->setPlayerMark(ganning, selector, previous);
        });
        room->askForUseCard(ganning, "@@junwei", "junwei-invoke", -1, Card::MethodNone);
        return false;
    }
};

class JunweiGot : public TriggerSkillV2
{
public:
    JunweiGot() : TriggerSkillV2("#junwei-got")
    {
        events << EventPhaseChanging;
        frequency = Compulsory;
        global = true;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *target, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseChanging || !target || !target->isAlive()
            || data.value<PhaseChangeStruct>().to != Player::NotActive
            || target->getPile("junwei_equip").isEmpty()) return true;
        // The equipment owner need not own Junwei: returning this pile is an applied rule.
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = target;
        ctx.targets << target;
        ctx.original_data = &data;
        ctx.current_event = event;
        ctx.amount = getBaseAmount();
        contexts << ctx;
        return true;
    }

    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        return ctx.owner && ctx.owner->isAlive() && ctx.original_data
            && ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive
            && !ctx.owner->getPile("junwei_equip").isEmpty();
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *player) const override
    {
        if (!ctx.original_data) return false;
        PhaseChangeStruct change = ctx.original_data->value<PhaseChangeStruct>();
        if (change.to != Player::NotActive || player->getPile("junwei_equip").length() == 0)
            return false;
        foreach (int card_id, player->getPile("junwei_equip")) {
            const Card *card = Sanguosha->getCard(card_id);
            if (!player->isAlive() || !player->getPile("junwei_equip").contains(card_id) || !card) continue;

            const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
            if (!equip) continue;

            QList<CardsMoveStruct> exchangeMove;
            CardsMoveStruct move1(card_id, player, Player::PlaceEquip, CardMoveReason(CardMoveReason::S_REASON_PUT, player->objectName()));
            exchangeMove.push_back(move1);
			card = player->getEquip(equip->location());
            if (card) {
                CardsMoveStruct move2(card->getId(), nullptr, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP, player->objectName()));
                exchangeMove.push_back(move2);
            }
            LogMessage log;
            log.from = player;
            log.type = "$JunweiGot";
            log.card_str = QString::number(card_id);
            room->sendLog(log);

            room->moveCardsAtomic(exchangeMove, true);
        }
        return false;
    }
};

class Fenyong : public TriggerSkillV2
{
public:
    Fenyong() : TriggerSkillV2("fenyong")
    {
        events << Damaged << DamageInflicted << EventPhaseStart << EventLoseSkill;
        frequency = Frequent;
    }

    Frequency getFrequency(const Player *player = nullptr) const override
    {
        return player && player->getMark("@fenyong") > 0 ? Compulsory : Frequent;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventLoseSkill && player && data.toString() == objectName()
            && !player->hasSkill(objectName(), true)) room->setPlayerMark(player, "@fenyong", 0);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return TriggerList();
        if (event == Damaged)
            return player->isAlive() && player->hasSkill(objectName()) && player->getMark("@fenyong") == 0
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        if (event == DamageInflicted)
            return player->isAlive() && player->hasSkill(objectName()) && player->getMark("@fenyong") > 0
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        if (event == EventPhaseStart && player->getPhase() == Player::Finish) {
            TriggerList result;
            foreach (ServerPlayer *owner, room->getAllPlayers())
                if (owner->hasSkill(objectName(), true) && !owner->hasSkill("xuehen") && owner->getMark("@fenyong") > 0)
                    result[owner] << objectName();
            return result;
        }
        return TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == Damaged) {
            if (!room->askForSkillInvoke(player, objectName() + "%1")) return false;
        }
        ctx.targets << player;
        return true;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &,
                      ServerPlayer *player) const override
    {
        if (event == Damaged) {
            room->addPlayerMark(player, "@fenyong");
        } else if (event == DamageInflicted) {
            room->broadcastSkillInvoke(objectName(), 2);
            LogMessage log;
            log.type = "#FenyongAvoid";
            log.from = player;
            log.arg = objectName();
            room->sendLog(log);
            return true;
        } else if (event == EventPhaseStart) {
            if (!player->hasSkill("xuehen")) room->setPlayerMark(player, "@fenyong", 0);
        }
        return false;
    }
};

class FenyongDetach : public DetachEffectSkill
{
public:
    FenyongDetach() : DetachEffectSkill("fenyong")
    {
    }

    void onSkillDetached(Room *room, ServerPlayer *player) const
    {
        if (!player->hasSkill("fenyong", true) && player->getMark("@fenyong") > 0)
            room->setPlayerMark(player, "@fenyong", 0);
    }
};

class Xuehen : public TriggerSkillV2
{
public:
    Xuehen() : TriggerSkillV2("xuehen")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *current, QVariant &) const override
    {
        if (event != EventPhaseStart || !current || current->getPhase() != Player::Finish) return TriggerList();
        TriggerList result;
        for (ServerPlayer *owner : room->getAllPlayers())
            if (owner->isAlive() && owner->hasSkill(objectName()) && owner->getMark("@fenyong") > 0)
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner || owner->getMark("@fenyong") <= 0) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getOtherPlayers(owner))
            if (owner->canSlash(target, false)) candidates << target;
        ctx.choice = candidates.isEmpty() ? "discard" : room->askForChoice(owner, objectName(), "discard+slash");
        ServerPlayer *target = ctx.choice == "slash"
            ? room->askForPlayerChosen(owner, candidates, objectName(), "@dummy-slash") : ctx.invoker;
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &) const override
    {
        if (!owner || owner->getMark("@fenyong") <= 0) return false;
        // Consume the protection before resolving the chosen retaliation.
        room->setPlayerMark(owner, "@fenyong", 0);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(owner, objectName());
        room->broadcastSkillInvoke(objectName(), ctx.choice == "slash" ? 2 : 1);
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (!owner || !owner->isAlive() || !target || !target->isAlive()) return false;
        const int amount = getEffectiveAmount(ctx);
        if (ctx.choice == "slash") {
            for (int i = 0; i < amount && owner->isAlive() && target->isAlive(); ++i) {
                Slash *slash = new Slash(Card::NoSuit, 0);
                slash->setSkillName(objectName());
                CardUseStruct use(slash, owner, target);
                use.setOwnedCard(slash);
                if (!owner->canSlash(target, slash, false)) break;
                room->useCard(use);
            }
        } else {
            DummyCard discarded;
            const int count = qMin(owner->getLostHp() * amount, target->getCardCount());
            for (int i = 0; i < count && owner->canDiscard(target, "he"); ++i) {
                const int id = room->askForCardChosen(owner, target, "he", objectName(), false,
                                                      Card::MethodDiscard, discarded.getSubcards(), i > 0);
                if (id < 0) break;
                discarded.addSubcard(id);
            }
            if (discarded.subcardsLength() > 0) room->throwCard(&discarded, target, owner);
        }
        return false;
    }
};

LihunCard::LihunCard()
{
    setSkillName("lihun");
    mute = true;
}

bool LihunCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->isMale() && to_select != Self;
}

void LihunCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    effect.to->setFlags("LihunTarget");
    effect.from->setFlags("LihunSource");
    effect.from->turnOver();
    room->broadcastSkillInvoke("lihun", 1);
    if (!effect.to->isKongcheng()) {
        DummyCard dummy(effect.to->handCards());
        CardMoveReason reason(CardMoveReason::S_REASON_TRANSFER, effect.from->objectName(),
                              effect.to->objectName(), "lihun", "");
        room->moveCardTo(&dummy, effect.to, effect.from, Player::PlaceHand, reason, false);
    }
    effect.from->setFlags("-LihunSource");
}

class LihunSelect : public ViewAsSkillV2
{
public:
    LihunSelect() : ViewAsSkillV2("lihun", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "he");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && card && request.initiator
            && card->getEffectiveId() >= 0
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId()))
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return request.initiator && selected.isEmpty() && candidate && candidate->isMale() && candidate != request.initiator;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 ? bgmProxy(this, request, true) : nullptr;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "LihunCard"; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || request.selectedCardIds.size() != 1
            || !ctx.initiator->canDiscard(request.selectedCardIds.first())) return false;
        if (!ViewAsSkillV2::pay(room, ctx, request)) return false;
        if (!ctx.initiator->isAlive()) return false;
        // Both discarding and turning over are the printed cost, before target effects.
        const bool sourceFlag = ctx.initiator->hasFlag("LihunSource");
        ctx.initiator->setFlags("LihunSource");
        const auto restoreSource = qScopeGuard([&ctx, sourceFlag] {
            if (!sourceFlag) ctx.initiator->setFlags("-LihunSource");
        });
        ctx.initiator->turnOver();
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *actor = ctx.invoker;
        if (!actor || !actor->isAlive() || !target || !target->isAlive()) return FinishSkill;
        Room *room = actor->getRoom();
        // Returning cards belongs to the applied action, not the continued ownership of Lihun.
        QVariantList receipts = actor->getTag("LihunReceipts").toList();
        receipts << QVariantMap{{"owner", ctx.activationRef.ownerObjectName},
                                {"instance", ctx.activationRef.key.instanceID},
                                {"target", target->objectName()}, {"amount", getEffectiveAmount(ctx)}};
        actor->setTag("LihunReceipts", receipts);
        target->setFlags("LihunTarget");
        const bool sourceFlag = actor->hasFlag("LihunSource");
        actor->setFlags("LihunSource");
        const auto restoreSource = qScopeGuard([actor, sourceFlag] {
            if (!sourceFlag) actor->setFlags("-LihunSource");
        });
        room->broadcastSkillInvoke("lihun", 1);
        if (actor->isAlive() && target->isAlive() && !target->isKongcheng()) {
            DummyCard dummy(target->handCards());
            room->moveCardTo(&dummy, target, actor, Player::PlaceHand,
                             CardMoveReason(CardMoveReason::S_REASON_TRANSFER, actor->objectName(), target->objectName(), "lihun", ""), false);
        }
        return FinishSkill;
    }
};

class Lihun : public TriggerSkillV2
{
public:
    Lihun() : TriggerSkillV2("lihun")
    {
        events << EventPhaseChanging << EventPhaseEnd << Death;
        global = true;
        frequency = Compulsory;
        view_as_skill = new LihunSelect;
    }

    static void refreshTargets(Room *room)
    {
        QSet<QString> names;
        for (ServerPlayer *owner : room->getAllPlayers(true))
            for (const QVariant &receipt : owner->getTag("LihunReceipts").toList())
                names.insert(receipt.toMap().value("target").toString());
        for (ServerPlayer *target : room->getAllPlayers(true))
            target->setFlags(names.contains(target->objectName()) ? "LihunTarget" : "-LihunTarget");
    }

    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseEnd || !player || !player->isAlive() || player->getPhase() != Player::Play) return true;
        for (const QVariant &receipt : player->getTag("LihunReceipts").toList()) {
            const QVariantMap saved = receipt.toMap();
            ServerPlayer *target = room->findChild<ServerPlayer *>(saved.value("target").toString());
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = room->findChild<ServerPlayer *>(saved.value("owner").toString());
            if (!ctx.owner) continue;
            ctx.invoker = ctx.initiator = player;
            ctx.instanceID = saved.value("instance").toInt();
            ctx.sourceRef = SkillInstanceRef(saved.value("owner").toString(),
                                             SkillInstanceKey(objectName(), ctx.instanceID));
            ctx.extra_data = receipt;
            ctx.amount = saved.value("amount").toInt();
            if (target) ctx.targets << target;
            ctx.original_data = &data;
            ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }

    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        return ctx.invoker && ctx.invoker->isAlive()
            && ctx.invoker->getTag("LihunReceipts").toList().contains(ctx.extra_data);
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player && ((event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            || (event == Death && data.value<DeathStruct>().who == player))) {
            player->removeTag("LihunReceipts");
            refreshTargets(room);
        }
        return false;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        QVariantList receipts = player->getTag("LihunReceipts").toList();
        receipts.removeOne(ctx.extra_data);
        player->setTag("LihunReceipts", receipts);
        refreshTargets(room);
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        ServerPlayer *diaochan = ctx.invoker;
        const int count = qMax(0, target->getHp()) * getEffectiveAmount(ctx);
        if (count == 0 || diaochan->isNude()) return false;
        room->broadcastSkillInvoke(objectName(), 2);
        const Card *cards = room->askForExchange(diaochan, objectName(), count, count, true, "LihunGoBack");
        if (cards && diaochan->isAlive() && target->isAlive())
            room->moveCardTo(cards, diaochan, target, Player::PlaceHand,
                             CardMoveReason(CardMoveReason::S_REASON_GIVE, diaochan->objectName(), target->objectName(), objectName(), ""), false);
        return false;
    }
};
class Chongzhen : public TriggerSkillV2
{
public:
	Chongzhen() : TriggerSkillV2("chongzhen")
	{
		events << CardResponded << CardUsed;
	}

	bool collectTriggerContexts(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
	{
		if (!player || !player->isAlive()) return true;
        QList<ServerPlayer *> targets;
		if (triggerEvent == CardResponded) {
			const CardResponseStruct resp = data.value<CardResponseStruct>();
			if (resp.m_card && resp.m_card->getSkillNames().contains("longdan") && resp.m_who) targets << resp.m_who;
		} else {
			const CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || !use.card->getSkillNames().contains("longdan")) return true;
			targets = use.to;
			if (targets.isEmpty() && use.who && use.who != player) targets << use.who;
		}
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            for (ServerPlayer *target : targets) {
                if (!target || !target->isAlive() || target->isKongcheng()) continue;
                SkillContext ctx;
                ctx.skill_name = objectName() + "->" + target->objectName();
                ctx.owner = ctx.invoker = ctx.initiator = player;
                ctx.instanceID = id;
                ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
                ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
                if (!ctx.sourceRef.isValid()) continue;
                ctx.targets << target;
                ctx.preferredTarget = target;
                ctx.preferredTargetSeat = target->getSeat();
                bool ok = false;
                ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &ok);
                if (!ok) ctx.amount = getBaseAmount();
                ctx.current_event = triggerEvent;
                ctx.original_data = &data;
                contexts << ctx;
            }
        }
        return true;
	}

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        return ctx.preferredTarget && ctx.preferredTarget->isAlive() && !ctx.preferredTarget->isKongcheng()
            && player->askForSkillInvoke(objectName(), ctx.preferredTarget);
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        room->doAnimate(1, player->objectName(), target->objectName());
        room->broadcastSkillInvoke(objectName(), event == CardResponded ? 1 : 2);
        for (int i = 0; i < getEffectiveAmount(ctx) && player->isAlive() && target->isAlive()
             && player->canGet(target, "h"); ++i) {
            const int id = room->askForCardChosen(player, target, "h", objectName(), false, Card::MethodGet);
            if (!target->handCards().contains(id) || !player->canGet(target, id)) break;
            room->obtainCard(player, Sanguosha->getCard(id),
                CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, player->objectName()), false);
        }
        return false;
    }
};

BGMPackage::BGMPackage()
 : Package("BGM")
{
    General *bgm_zhaoyun = new General(this, "bgm_zhaoyun", "qun", 3); // *SP 001
    bgm_zhaoyun->addSkill("longdan");
    bgm_zhaoyun->addSkill(new Chongzhen);

    General *bgm_diaochan = new General(this, "bgm_diaochan", "qun", 3, false); // *SP 002
    bgm_diaochan->addSkill(new Lihun);
    bgm_diaochan->addSkill("biyue");
    addMetaObject<LihunCard>();

    General *bgm_caoren = new General(this, "bgm_caoren", "wei"); // *SP 003
    bgm_caoren->addSkill(new Kuiwei);
    bgm_caoren->addSkill(new Yanzheng);

    General *bgm_pangtong = new General(this, "bgm_pangtong", "qun", 3); // *SP 004
    bgm_pangtong->addSkill(new Manjuan);
    bgm_pangtong->addSkill(new Zuixiang);
    bgm_pangtong->addSkill(new ZuixiangClear);
    related_skills.insert("zuixiang", "#zuixiang-limit");

    General *bgm_zhangfei = new General(this, "bgm_zhangfei", "shu"); // *SP 005
    bgm_zhangfei->addSkill(new Jie);
    bgm_zhangfei->addSkill(new Dahe);

    General *bgm_lvmeng = new General(this, "bgm_lvmeng", "wu", 3); // *SP 006
    bgm_lvmeng->addSkill(new Tanhu);
    bgm_lvmeng->addSkill(new Mouduan);

    General *bgm_liubei = new General(this, "bgm_liubei$", "shu"); // *SP 007
    bgm_liubei->addSkill(new Zhaolie);
    bgm_liubei->addSkill(new Shichou);
    bgm_liubei->addSkill(new ShichouEffect);
    related_skills.insert("shichou", "#shichou-effect");

    General *bgm_daqiao = new General(this, "bgm_daqiao", "wu", 3, false); // *SP 008
    bgm_daqiao->addSkill(new Yanxiao);
    bgm_daqiao->addSkill(new Anxian);

    General *bgm_ganning = new General(this, "bgm_ganning", "qun"); // *SP 009
    bgm_ganning->addSkill(new Yinling);
    bgm_ganning->addSkill(new Junwei);
    bgm_ganning->addSkill(new JunweiGot);
    related_skills.insert("junwei", "#junwei-got");

    General *bgm_xiahoudun = new General(this, "bgm_xiahoudun", "wei"); // *SP 010
    bgm_xiahoudun->addSkill(new Fenyong);
    bgm_xiahoudun->addSkill(new FenyongDetach);
    bgm_xiahoudun->addSkill(new Xuehen);
    bgm_xiahoudun->addSkill(new SlashNoDistanceLimitSkill("xuehen"));
    related_skills.insert("fenyong", "#fenyong-clear");
    related_skills.insert("xuehen", "#xuehen-slash-ndl");

    addMetaObject<DaheCard>();
    addMetaObject<TanhuCard>();
    addMetaObject<ShichouCard>();
    addMetaObject<YanxiaoCard>();
    addMetaObject<YinlingCard>();
    addMetaObject<JunweiCard>();
}
ADD_PACKAGE(BGM)

// DIY Generals
ZhaoxinCard::ZhaoxinCard()
{
    setSkillName("zhaoxin");
}

bool ZhaoxinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Slash slash(NoSuit, 0);
    return slash.targetFilter(targets, to_select, Self);
}

void ZhaoxinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->showAllCards(source);

    Slash *slash = new Slash(Card::NoSuit, 0);
    slash->setSkillName("_zhaoxin");
    CardUseStruct use(slash, source, targets);
    use.setOwnedCard(slash);
    room->useCard(use);
}

class ZhaoxinViewAsSkill : public ViewAsSkillV2
{
public:
    ZhaoxinViewAsSkill() : ViewAsSkillV2("zhaoxin")
    {
        setResponseOrUse(true);
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@zhaoxin"
            && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng() && Slash::IsAvailable(request.initiator);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!request.selectedCardIds.isEmpty()) return nullptr;
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName(objectName());
        return slash;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator || !ctx.initiator->isAlive() || ctx.initiator->isKongcheng()) return false;
        // Revealing the hand is the cost; the returned Slash owns its normal card lifecycle.
        room->showAllCards(ctx.initiator);
        return true;
    }
};

class Zhaoxin : public TriggerSkillV2
{
public:
    Zhaoxin() : TriggerSkillV2("zhaoxin")
    {
        events << EventPhaseEnd;
        view_as_skill = new ZhaoxinViewAsSkill;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *simazhao, QVariant &) const override
    {
        if (!simazhao || !simazhao->isAlive() || !simazhao->hasSkill(objectName())
            || simazhao->getPhase() != Player::Draw || simazhao->isKongcheng() || !Slash::IsAvailable(simazhao))
            return TriggerList();
        QList<ServerPlayer *> targets;
        foreach(ServerPlayer *p, room->getAllPlayers())
            if (simazhao->canSlash(p))
                targets << p;

        if (targets.isEmpty())
            return TriggerList();
        return TriggerList{{simazhao, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *simazhao, SkillContext &ctx) const override
    {
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int previous = simazhao->getMark(selector);
        room->setPlayerMark(simazhao, selector, ctx.activationRef.key.instanceID);
        const auto restoreSource = qScopeGuard([room, simazhao, selector, previous] {
            room->setPlayerMark(simazhao, selector, previous);
        });
        room->askForUseCard(simazhao, "@@zhaoxin", "@zhaoxin");
        return false;
    }
};

class Langgu : public TriggerSkillV2
{
public:
    Langgu() : TriggerSkillV2("langgu")
    {
        events << Damaged << AskForRetrial;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == Damaged && player && player->isAlive() && player->hasSkill(objectName()))
            result[player] << objectName();
        else if (event == AskForRetrial) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge && judge->reason == objectName() && judge->who == player
                && player->isAlive() && player->hasSkill(objectName()) && !player->isKongcheng())
                result[player] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        if (event == Damaged) return player->askForSkillInvoke(objectName() + "$-1", *ctx.original_data);
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (!judge || !judge->card || judge->who != player) return false;
        const QString prompt = QStringList{"@langgu-card", judge->who->objectName(), objectName(),
            judge->reason, QString::number(judge->card->getEffectiveId())}.join(":");
        // isRetrial selects without moving the card; Room::retrial owns the replacement movement.
        const Card *card = room->askForCard(player, ".", prompt, *ctx.original_data,
                                            Card::MethodResponse, judge->who, true);
        if (!card || !player->handCards().contains(card->getEffectiveId())) return false;
        ctx.extra_data = card->getEffectiveId();
        ctx.targets << judge->who;
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *simazhao, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        if (event == Damaged) {
            const DamageStruct damage = ctx.original_data->value<DamageStruct>();
            ctx.manual_effect = true;
            const int count = damage.damage * getEffectiveAmount(ctx);
            for (int i = 0; i < count; i++) {
                if (!simazhao->isAlive() || (i > 0 && !simazhao->askForSkillInvoke(objectName() + "$-1", *ctx.original_data))) break;
                JudgeStruct judge;
                judge.good = true;
                judge.play_animation = false;
                judge.who = simazhao;
                judge.reason = objectName();
                room->judge(judge);
                if (simazhao->isAlive() && damage.from && damage.from->isAlive() && judge.card) {
                    ctx.extra_data = int(judge.card->getSuit());
                    skillEffect(event, room, simazhao, ctx, damage.from);
                }
            }
        }
        return false;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *simazhao, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (event == AskForRetrial) {
            JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
            const int id = ctx.extra_data.toInt();
            if (judge && judge->who == target && simazhao->handCards().contains(id))
                room->retrial(Sanguosha->getCard(id), simazhao, judge, objectName());
            return false;
        }
        if (!simazhao->isAlive() || target->isKongcheng()) return false;
        QList<int> matching, other, selected;
        for (int id : target->handCards()) {
            if (simazhao->canDiscard(target, id) && int(Sanguosha->getCard(id)->getSuit()) == ctx.extra_data.toInt()) matching << id;
            else other << id;
        }
        LogMessage log;
        log.type = "$ViewAllCards";
        log.from = simazhao;
        log.to << target;
        log.card_str = ListI2S(target->handCards()).join("+");
        room->sendLog(log, simazhao);
        const auto clearSelection = qScopeGuard([room, simazhao] { room->clearAG(simazhao); });
        do {
            room->fillAG(matching + other, simazhao, other);
            const int id = room->askForAG(simazhao, matching, true, objectName());
            room->clearAG(simazhao);
            if (!matching.removeOne(id)) break;
            selected << id;
            other.prepend(id);
        } while (!matching.isEmpty());
        // Only the chosen cards are discarded, after checking their current ownership.
        QList<int> discard;
        for (int id : selected)
            if (target->handCards().contains(id) && simazhao->canDiscard(target, id)) discard << id;
        if (!discard.isEmpty()) room->throwCard(discard, objectName(), target, simazhao);
        return false;
    }
};
FuluanCard::FuluanCard()
{
    setSkillName("fuluan");
}

bool FuluanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->inMyAttackRange(to_select, subcards);
}

void FuluanCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    effect.to->turnOver();
    room->setPlayerCardLimitation(effect.from, "use", "Slash", true);
}

class Fuluan : public ViewAsSkillV2
{
public:
    Fuluan() : ViewAsSkillV2("fuluan", 3)
    {
        setPhaseName("Play");
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        return player && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && player->getCardCount() >= 3
            && !player->hasFlag("ForbidFuluan");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || request.selectedCardIds.size() >= 3
            || card->getEffectiveId() < 0 || request.selectedCardIds.contains(card->getEffectiveId())
            || (!request.initiator->handCards().contains(card->getEffectiveId())
                && !request.initiator->getEquipsId().contains(card->getEffectiveId()))
            || !request.initiator->canDiscard(card->getEffectiveId())) return false;
        return request.selectedCardIds.isEmpty()
            || card->getSuit() == Sanguosha->getCard(request.selectedCardIds.first())->getSuit();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 3) return false;
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        foreach (int id, request.selectedCardIds) {
            const Card *card = Sanguosha->getCard(id);
            if (!card || !canSelectCard(prefix, card)) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return request.initiator && selected.isEmpty() && candidate
            && request.initiator->inMyAttackRange(candidate, request.selectedCardIds);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.size() == 1;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? bgmProxy(this, request) : nullptr;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "FuluanCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !target) return FinishSkill;
        target->turnOver();
        ctx.invoker->getRoom()->setPlayerCardLimitation(ctx.invoker, "use", "Slash", true, objectName());
        return FinishSkill;
    }
};

class Shude : public TriggerSkillV2
{
public:
    Shude() : TriggerSkillV2("shude")
    {
        events << EventPhaseStart;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *wangyuanji, QVariant &) const override
    {
        return wangyuanji && wangyuanji->isAlive() && wangyuanji->hasSkill(objectName())
            && wangyuanji->getPhase() == Player::Finish && wangyuanji->getHandcardNum() < wangyuanji->getMaxHp()
            ? TriggerList{{wangyuanji, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *wangyuanji, SkillContext &) const override
    {
        return wangyuanji->askForSkillInvoke(objectName()+"$-1");
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *wangyuanji, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, wangyuanji, ctx, wangyuanji);
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(qMax(0, target->getMaxHp() - target->getHandcardNum()) * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

HuangenCard::HuangenCard()
{
    setSkillName("huangen");
}

bool HuangenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (targets.length() >= Self->getHp()) return false;
    QStringList targetslist = Self->property("huangen_targets").toString().split("+");
    return targetslist.contains(to_select->objectName());
}

void HuangenCard::onEffect(CardEffectStruct &effect) const
{
    CardUseStruct use = effect.from->getTag("huangen").value<CardUseStruct>();
    use.nullified_list << effect.to->objectName();
    effect.from->setTag("huangen", QVariant::fromValue(use));
    effect.to->drawCards(1, "huangen");
}

class HuangenViewAsSkill : public ViewAsSkillV2
{
public:
    QString historyKey(const ActiveSkillRequest &) const override { return "HuangenCard"; }
    HuangenViewAsSkill() : ViewAsSkillV2("huangen")
    {
        response_pattern = "@@huangen";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@huangen" && request.initiator->getHp() > 0;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        if (!request.initiator || !candidate || selected.size() >= request.initiator->getHp()) return false;
        const QStringList allowed = request.initiator->property("huangen_targets").toString().split("+");
        return allowed.contains(candidate->objectName());
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return !selected.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return bgmProxy(this, request, true);
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.initiator || !target) return ContinueEffects;
        Room *room = ctx.initiator->getRoom();
        const qint64 executionId = ctx.initiator->getTag("HuangenExecution").toLongLong();
        SkillContext parent = room->getSkillExecutionContext(executionId);
        if (!parent.original_data || !(parent.activationRef == ctx.activationRef)) return ContinueEffects;
        CardUseStruct use = parent.original_data->value<CardUseStruct>();
        if (!use.to.contains(target)) return ContinueEffects;
        if (!use.nullified_list.contains(target->objectName())) use.nullified_list << target->objectName();
        *parent.original_data = QVariant::fromValue(use);
        target->drawCards(getEffectiveAmount(ctx), "huangen");
        return ContinueEffects;
    }
};

class Huangen : public TriggerSkillV2
{
public:
    Huangen() : TriggerSkillV2("huangen")
    {
        events << TargetConfirmed;
        view_as_skill = new HuangenViewAsSkill;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetConfirmed || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getHp() <= 0) return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        return use.card && use.to.length() > 1 && use.card->isNDTrick()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *liuxie, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QStringList names;
        foreach (ServerPlayer *p, use.to) names << p->objectName();
        const QVariant oldTargets = liuxie->property("huangen_targets");
        const QVariant oldExecution = liuxie->getTag("HuangenExecution");
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int oldSelector = liuxie->getMark(selector);
        SkillContext execution = ctx;
        const auto executionGuard = room->beginSkillExecution(execution, *ctx.original_data);
        // Only an execution ID crosses the nested request; no live event/card is kept in player state.
        liuxie->setTag("HuangenExecution", execution.executionID);
        room->setPlayerProperty(liuxie, "huangen_targets", names.join("+"));
        room->setPlayerMark(liuxie, selector, ctx.activationRef.key.instanceID);
        const auto restoreRequest = qScopeGuard([room, liuxie, oldTargets, oldExecution, selector, oldSelector] {
            room->setPlayerProperty(liuxie, "huangen_targets", oldTargets);
            if (oldExecution.isValid()) liuxie->setTag("HuangenExecution", oldExecution);
            else liuxie->removeTag("HuangenExecution");
            room->setPlayerMark(liuxie, selector, oldSelector);
        });
        room->askForUseCard(liuxie, "@@huangen", "@huangen-card");
        CardUseStruct updated = ctx.original_data->value<CardUseStruct>();
        const CardUseStruct resolved = execution.original_data->value<CardUseStruct>();
        for (const QString &name : resolved.nullified_list)
            if (!use.nullified_list.contains(name) && !updated.nullified_list.contains(name)) updated.nullified_list << name;
        *ctx.original_data = QVariant::fromValue(updated);
        return false;
    }
};

HantongCard::HantongCard()
{
    setSkillName("hantong");
    target_fixed = true;
    mute = true;
}

class HantongViewAsSkill : public ViewAsSkillV2
{
public:
    QString historyKey(const ActiveSkillRequest &) const override { return "HantongCard"; }
    HantongViewAsSkill() : ViewAsSkillV2("hantong")
    {
        setResponseOrUse(true);
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.initiator->getPile("edict").isEmpty()) return false;
        JijiangViewAsSkill jijiang;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            ? jijiang.isEnabledAtPlay(request.initiator)
            : jijiang.isEnabledAtResponse(request.initiator, request.pattern);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return bgmProxy(this, request, true);
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        const int id = chooseHantongEdict(room, ctx.initiator);
        ctx.extra_data = id;
        return id >= 0;
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        return payHantongEdict(room, ctx.initiator, ctx.extra_data.toInt());
    }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *actor = ctx.invoker;
        if (!actor || !actor->isAlive()) return FinishSkill;
        Room *room = actor->getRoom();
        const int id = grantHantongSkill(room, actor, ctx.activationRef, "jijiang");
        if (id <= 0 || !actor->isAlive()) return FinishSkill;
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName("jijiang");
        const int previous = actor->getMark(selector);
        room->setPlayerMark(actor, selector, id);
        const auto restoreSource = qScopeGuard([room, actor, selector, previous] {
            room->setPlayerMark(actor, selector, previous);
        });
        if (!room->askForUseCard(actor, "@jijiang", "@hantong-jijiang"))
            room->setPlayerFlag(actor, "Global_JijiangFailed");
        return FinishSkill;
    }
};

class Hantong : public TriggerSkillV2
{
public:
    Hantong() : TriggerSkillV2("hantong")
    {
        events << CardsMoveOneTime << CardAsked << TargetConfirmed << EventPhaseStart << EventPhaseChanging;
        view_as_skill = new HantongViewAsSkill;
        global = true;
    }

    static void RemoveEdict(ServerPlayer *liuxie)
    {
        Room *room = liuxie->getRoom();
        payHantongEdict(room, liuxie, chooseHantongEdict(room, liuxie));
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        for (ServerPlayer *owner : room->getAllPlayers(true)) {
            const QVariantList grants = owner->getTag("HantongGrants").toList();
            owner->removeTag("HantongGrants");
            owner->removeTag("Hantong_use");
            for (const QVariant &value : grants) {
                const QVariantMap grant = value.toMap();
                room->detachSkillFromPlayer(owner, SkillInstanceUtils::formatName(
                    grant.value("skill").toString(), grant.value("instance").toInt()), false, true);
            }
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player) return result;
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Discard
                && move.from == player && move.to_place == Player::DiscardPile
                && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)
                result[player] << objectName();
        } else if (event == CardAsked) {
            if (player->isAlive() && player->hasSkill(objectName()) && !player->getPile("edict").isEmpty()) {
                const QString pattern = data.toStringList().value(0);
                if (pattern == "jink" || (pattern.contains("slash", Qt::CaseInsensitive)
                    && !player->hasFlag("Global_JijiangFailed"))) result[player] << objectName();
            }
        } else if (event == TargetConfirmed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card && use.card->isKindOf("Peach") && use.from && use.from->getKingdom() == "wu"
                && player->hasFlag("Global_Dying") && player->isAlive() && player->hasSkill(objectName())
                && !player->getPile("edict").isEmpty() && player != use.from) result[player] << objectName();
        } else if (event == EventPhaseStart) {
            if (player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Discard
                && !player->getPile("edict").isEmpty()) result[player] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
            QList<int> ids;
            for (int i = 0; i < move.card_ids.size() && i < move.from_places.size(); i++)
                if (move.from_places[i] == Player::PlaceHand && room->getCardPlace(move.card_ids[i]) == Player::DiscardPile) ids << move.card_ids[i];
            ctx.extra_data = QVariant::fromValue(ids);
            return !ids.isEmpty() && player->askForSkillInvoke(objectName() + "$1");
        }
        QString name;
        if (event == CardAsked) {
            const QString pattern = ctx.original_data->toStringList().value(0);
            name = pattern == "jink" ? "hujia" : "jijiang";
        } else if (event == TargetConfirmed) name = "jiuyuan";
        else if (event == EventPhaseStart) name = "xueyi";
        if (name.isEmpty()) return false;
        // The legacy AI may inspect the current CardAsked data only during this prompt.
        const QVariant previous = player->getTag("HantongOriginData");
        if (event == CardAsked) player->setTag("HantongOriginData", *ctx.original_data);
        const auto restorePrompt = qScopeGuard([player, previous] {
            if (previous.isValid()) player->setTag("HantongOriginData", previous);
            else player->removeTag("HantongOriginData");
        });
        if (!player->askForSkillInvoke("hantong_acquire", name, false)) return false;
        const int id = chooseHantongEdict(room, player);
        ctx.extra_data = QVariantMap{{"skill", name}, {"card", id}};
        return id >= 0;
    }

    bool pay(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        return event == CardsMoveOneTime || payHantongEdict(room, player, ctx.extra_data.toMap().value("card").toInt());
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == CardsMoveOneTime) {
            QList<int> ids;
            for (int id : ctx.extra_data.value<QList<int>>())
                if (room->getCardPlace(id) == Player::DiscardPile) ids << id;
            if (!ids.isEmpty()) player->addToPile("edict", ids, true,
                              QList<ServerPlayer *>(), ctx.original_data->value<CardsMoveOneTimeStruct>().reason);
        } else if (player->isAlive())
            grantHantongSkill(room, player, ctx.activationRef, ctx.extra_data.toMap().value("skill").toString());
        return false;
    }
};

const Card *HantongCard::validate(CardUseStruct &cardUse) const
{
    cardUse.m_isOwnerUse = false;
    Room *room = cardUse.from->getRoom();
    Hantong::RemoveEdict(cardUse.from);
    cardUse.from->setTag("Hantong_use", true);
    room->acquireSkill(cardUse.from, "jijiang");
    if (!room->askForUseCard(cardUse.from, "@jijiang", "@hantong-jijiang")) {
        room->setPlayerFlag(cardUse.from, "Global_JijiangFailed");
        return nullptr;
    }
    return this;
}

void HantongCard::onUse(Room *, CardUseStruct &) const
{
}

DIYYicongCard::DIYYicongCard()
{
    setSkillName("diyyicong");
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
}

void DIYYicongCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    source->addToPile("retinue", this);
}

class DIYYicongViewAsSkill : public ViewAsSkillV2
{
public:
    QString historyKey(const ActiveSkillRequest &) const override { return "DIYYicongCard"; }
    DIYYicongViewAsSkill() : ViewAsSkillV2("diyyicong")
    {
        setResponseOrUse(true);
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@diyyicong"
            && request.reason != CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && card->getEffectiveId() >= 0
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId()));
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return !request.selectedCardIds.isEmpty();
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) ? bgmProxy(this, request) : nullptr;
    }

    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return NoTarget; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.targets = {ctx.invoker};
        return ContinueEffects;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && ctx.use_card && ctx.initiator) {
            QList<int> ids;
            for (int id : ctx.use_card->getSubcards())
                if (ctx.initiator->handCards().contains(id) || ctx.initiator->getEquipsId().contains(id)) ids << id;
            if (!ids.isEmpty()) target->addToPile("retinue", ids, true);
        }
        return FinishSkill;
    }
};

class DIYYicong : public TriggerSkillV2
{
public:
    DIYYicong() : TriggerSkillV2("diyyicong")
    {
        events << EventPhaseEnd;
        view_as_skill = new DIYYicongViewAsSkill;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *gongsunzan, QVariant &) const override
    {
        return gongsunzan && gongsunzan->isAlive() && gongsunzan->hasSkill(objectName())
            && gongsunzan->getPhase() == Player::Discard && !gongsunzan->isNude()
            ? TriggerList{{gongsunzan, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *gongsunzan, SkillContext &ctx) const override
    {
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int previous = gongsunzan->getMark(selector);
        room->setPlayerMark(gongsunzan, selector, ctx.activationRef.key.instanceID);
        const auto restoreSource = qScopeGuard([room, gongsunzan, selector, previous] {
            room->setPlayerMark(gongsunzan, selector, previous);
        });
        room->askForUseCard(gongsunzan, "@@diyyicong", "@diyyicong", -1, Card::MethodNone);
        return false;
    }
};

class DIYYicongDistance : public DistanceSkillV2
{
public:
    DIYYicongDistance() : DistanceSkillV2("#diyyicong-dist")
    {
        setHolderSelector(CorrectSkill_Secondary);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const Player *holder = ctx.holder;
        const int n = holder ? holder->getPile("retinue").length() : 0;
        return holder && holder->hasSkill("diyyicong") && n > 0
            ? CorrectSkillResult::useAmount(n * ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

class Tuqi : public TriggerSkillV2
{
public:
    Tuqi() : TriggerSkillV2("tuqi")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *gongsunzan, QVariant &) const override
    {
        return gongsunzan && gongsunzan->isAlive() && gongsunzan->hasSkill(objectName())
            && gongsunzan->getPhase() == Player::Start && !gongsunzan->getPile("retinue").isEmpty()
            ? TriggerList{{gongsunzan, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *gongsunzan, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, gongsunzan, ctx, gongsunzan);
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *gongsunzan) const override
    {
        room->sendCompulsoryTriggerLog(gongsunzan, objectName());
        const int n = gongsunzan->getPile("retinue").length();
        room->setPlayerMark(gongsunzan, "tuqi_dist-Clear", n * getEffectiveAmount(ctx));
        gongsunzan->clearOnePrivatePile("retinue");
        const int index = n <= 2 ? 2 : 1;
        if (n <= 2) gongsunzan->drawCards(getEffectiveAmount(ctx), objectName());
        room->broadcastSkillInvoke(objectName(), index);
        return false;
    }
};

class TuqiDistance : public DistanceSkillV2
{
public:
    TuqiDistance() : DistanceSkillV2("#tuqi-dist")
    {
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        // The mark is the one applied distance effect; helper copies must not multiply it.
        const Player *holder = ctx.primary;
        const int n = holder ? holder->getMark("tuqi_dist-Clear") : 0;
        return holder && holder->hasSkill("tuqi") && n > 0
            ? CorrectSkillResult::useAmount(-n) : CorrectSkillResult::noEffect();
    }
};

BGMDIYPackage::BGMDIYPackage()
 : Package("BGMDIY")
{
    General *diy_simazhao = new General(this, "diy_simazhao", "wei", 3); // DIY 001
    diy_simazhao->addSkill(new Zhaoxin);
    diy_simazhao->addSkill(new Langgu);

    General *diy_wangyuanji = new General(this, "diy_wangyuanji", "wei", 3, false); // DIY 002
    diy_wangyuanji->addSkill(new Fuluan);
    diy_wangyuanji->addSkill(new Shude);

    General *diy_liuxie = new General(this, "diy_liuxie", "qun"); // DIY 003
    diy_liuxie->addSkill(new Huangen);
    diy_liuxie->addSkill(new Hantong);

    General *diy_gongsunzan = new General(this, "diy_gongsunzan", "qun"); // DIY 004
    diy_gongsunzan->addSkill(new DIYYicong);
    diy_gongsunzan->addSkill(new DIYYicongDistance);
    diy_gongsunzan->addSkill(new Tuqi);
    diy_gongsunzan->addSkill(new TuqiDistance);
    related_skills.insert("diyyicong", "#diyyicong-clear");
    related_skills.insert("diyyicong", "#diyyicong-dist");
    related_skills.insert("tuqi", "#tuqi-dist");

    General *diy_zhugeke = new General(this, "diy_zhugeke", "wu", 3, true);
    diy_zhugeke->addSkill("aocai");
    diy_zhugeke->addSkill("duwu");

    addMetaObject<ZhaoxinCard>();
    addMetaObject<FuluanCard>();
    addMetaObject<HuangenCard>();
    addMetaObject<HantongCard>();
    addMetaObject<DIYYicongCard>();
}
ADD_PACKAGE(BGMDIY)
