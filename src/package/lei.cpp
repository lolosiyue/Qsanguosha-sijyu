#include "lei.h"
#include "skill-instance-utils.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
//#include "maneuvering.h"
//#include "util.h"
//#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include <QScopeGuard>
//#include "json.h"

namespace {
static void addAllInstances(TriggerList &result, ServerPlayer *owner, const QString &name)
{
    if (!owner || !owner->hasSkill(name)) return;
    for (int id : owner->getValidSkillInstanceIds(name))
        result[owner] << SkillInstanceUtils::formatName(name, id);
}
static void exchangeRong(SkillContext &ctx, const QString &skill)
{
    ServerPlayer *actor = ctx.invoker;
    if (!actor || !actor->isAlive() || !ctx.use_card) return;
    Room *room = actor->getRoom();
    QList<int> toHand, toPile;
    for (int id : ctx.use_card->getSubcards()) {
        if (actor->getPile("rong").contains(id)) toHand << id;
        else if (actor->handCards().contains(id)) toPile << id;
        else return;
    }
    if (toHand.isEmpty() || toHand.size() != toPile.size()) return;
    CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, actor->objectName(), skill, "");
    CardsMoveStruct gain(toHand, actor, actor, Player::PlaceSpecial, Player::PlaceHand, reason);
    gain.from_pile_name = "rong";
    CardsMoveStruct store(toPile, actor, actor, Player::PlaceHand, Player::PlaceSpecial, reason);
    store.to_pile_name = "rong";
    // Both halves enter one native movement transaction; nested moves cannot reuse a half exchange.
    room->moveCardsAtomic(QList<CardsMoveStruct>() << gain << store, true);
}

}

class Wanglie : public TriggerSkillV2
{
public:
    Wanglie() : TriggerSkillV2("wanglie")
    {
        events << CardUsed;
        global = true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == CardUsed && player && player->isAlive() && player->getPhase() == Player::Play
            && player->hasSkill(objectName()) && data.value<CardUseStruct>().card && !data.value<CardUseStruct>().card->isKindOf("SkillCard"))
            addAllInstances(result, player, objectName());
        return result;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!room || event != CardUsed || !player || player->getPhase() != Player::Play) return false;
        const Card *card = data.value<CardUseStruct>().card;
        if (!card || card->isKindOf("SkillCard")) return false;
        const int count = room->countHistoryCards(player, "phase");
        // Shared UI projection is derived once from history, never incremented per skill instance.
        if (count >= 0) room->setPlayerMark(player, "wanglie-PlayClear", count);
        return false;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && ctx.original_data && ctx.owner->askForSkillInvoke(this, *ctx.original_data); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.original_data || !ctx.invoker) return false;
        // The prohibition covers every potential responder, including third-party Nullification.
        const QList<ServerPlayer *> targets = room->getAlivePlayers();
        ctx.manual_effect = true;
        ctx.choice = "limit";
        skillEffect(event, room, player, ctx, ctx.invoker);
        ctx.choice = "no_response";
        for (ServerPlayer *target : targets) skillEffect(event, room, player, ctx, target);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.original_data || !target || getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "limit") room->setPlayerCardLimitation(target, "use", ".", true, objectName());
        else {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName();
            *ctx.original_data = QVariant::fromValue(use);
        }
        return false;
    }
};

class WanglieMod : public TargetModSkillV2
{
public:
    WanglieMod() : TargetModSkillV2("#wangliemod", ".") { setHolderSelector(CorrectSkill_Primary); setBaseAmount(999); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        const Player *from = context.getHolder();
        if (context.modType == TargetModSkill::DistanceLimit && from && from->getPhase() == Player::Play
            && from->getMark("wanglie-PlayClear") == 0)
            return CorrectSkillResult::useAmount(context.currentAmount);
        return CorrectSkillResult::noEffect();
    }
};

class Zuilun : public TriggerSkillV2
{
public:
    Zuilun() : TriggerSkillV2("zuilun") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Finish)
            addAllInstances(result, player, objectName());
        return result;
    }
    int qualifiedConditions(ServerPlayer *owner) const
    {
        Room *room = owner->getRoom();
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toULongLong() == 0) return -1;
        const QVariantMap damage = room->queryActualDamage({{"turn_id", turn}, {"from", owner->objectName()}, {"limit", 1}});
        if (damage.contains("error") || !damage.value("complete").toBool()) return -1;
        int count = damage.value("items").toList().isEmpty() ? 0 : 1;
        bool discarded = false;
        QVariantMap filter{{"turn_id", turn}, {"from", owner->objectName()}};
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return -1;
            if (!filter.contains("watermark")) filter["watermark"] = page.value("watermark");
            for (const QVariant &item : page.value("items").toList()) {
                const QVariantMap move = item.toMap().value("data").toMap();
                if ((move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)
                    discarded = true;
            }
            if (!page.value("has_more").toBool()) break;
            filter["after"] = page.value("next_after");
        }
        if (!discarded) ++count;
        bool fewest = true;
        for (ServerPlayer *other : room->getAlivePlayers())
            if (other->getHandcardNum() < owner->getHandcardNum()) { fewest = false; break; }
        return count + (fewest ? 1 : 0);
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        const int count = qualifiedConditions(ctx.owner);
        if (count < 0 || !ctx.owner->askForSkillInvoke(this)) return false;
        ctx.extra_data = count;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.invoker;
        if (!owner || !owner->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        const int amount = getEffectiveAmount(ctx);
        const int conditions = ctx.bypass_cost ? qualifiedConditions(ctx.owner) : ctx.extra_data.toInt();
        if (conditions < 0) return false;
        const QList<int> revealed = room->getNCards(3 * amount);
        QList<int> ids = revealed, kept;
        // Detached draw-pile IDs must be returned on cancellation/TurnBroken, but never duplicate a nested move.
        const auto cleanup = qScopeGuard([&] {
            room->clearAG(owner);
            QList<int> pending;
            for (int id : revealed) if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) pending << id;
            if (!pending.isEmpty()) room->returnToTopDrawPile(pending);
        });
        room->fillAG(ids, owner);
        const int count = qMin(conditions * amount, int(ids.size()));
        for (int i = 0; i < count && owner->isAlive(); ++i) {
            const int id = room->askForAG(owner, ids, false, objectName(), "zuilun0");
            if (!ids.removeOne(id)) break;
            room->takeAG(owner, id, false, {owner}); kept << id;
        }
        room->clearAG(owner);
        room->askForGuanxing(owner, ids, Room::GuanxingUpOnly);
        if (!kept.isEmpty()) {
            ctx.choice = "obtain"; ctx.extra_data = ListI2V(kept);
            skillEffect(event, room, player, ctx, owner);
        } else if (count == 0 && owner->isAlive()) {
            ctx.choice = "lose";
            ServerPlayer *target = room->askForPlayerChosen(owner, room->getOtherPlayers(owner), objectName(), "@zuilun-lose");
            ctx.targets << owner; if (target) ctx.targets << target;
            room->sortByActionOrder(ctx.targets);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (ctx.choice == "obtain") {
            QList<int> ids;
            for (int id : ListV2I(ctx.extra_data.toList()))
                if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) ids << id;
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards, false); }
        } else room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        return false;
    }

};
class Fuyin : public TriggerSkillV2
{
public:
    Fuyin() : TriggerSkillV2("fuyin")
    {
        events << TargetConfirmed << EventSkillInvoking;
        frequency = Compulsory;
    }
    LimitScope getLimitScope() const override { return Limit_Turn; }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = QList<ServerPlayer *>() << ctx.owner; return isUsable(ctx); }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != TargetConfirmed || !player || !player->isAlive()) return result;
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.to.contains(player) && use.card && (use.card->isKindOf("Slash") || use.card->isKindOf("Duel"))
            && use.from && !use.from->isDead())
            for (int id : player->getValidSkillInstanceIds(objectName())) {
                SkillContext candidate; candidate.owner = candidate.invoker = player; candidate.skill_name = objectName(); candidate.instanceID = id;
                candidate.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
                if (isUsable(candidate)) result[player] << SkillInstanceUtils::formatName(objectName(), id);
            }
        return result;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.targets.isEmpty() && ctx.owner) ctx.targets << ctx.owner; return false; }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!room || !ctx.owner || !ctx.original_data || !target || getEffectiveAmount(ctx) <= 0) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.from || target->getHandcardNum() > use.from->getHandcardNum()) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        use.nullified_list << target->objectName();
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class Liangyin : public TriggerSkillV2
{
public:
    Liangyin() : TriggerSkillV2("liangyin")
    {
        events << CardsMoveOneTime;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != CardsMoveOneTime || !room || !player || !player->isAlive()) return result;
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        bool possible = false;
        if (move.to_place == Player::PlaceSpecial) {
            if (move.to_pile_name.startsWith("#")) return result;
            bool invoke = false;
            for (int i = 0; i < move.card_ids.length(); i++) {
                if (move.from_places.at(i) == Player::PlaceSpecial) continue;
                invoke = true;
                break;
            }

            if (!invoke) return result;
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getHandcardNum() > player->getHandcardNum())
                    targets << p;
            }
            possible = !targets.isEmpty();
        } else if (move.to && move.to_place == Player::PlaceHand) {
            if (!move.from_places.contains(Player::PlaceSpecial)) return result;
            bool invoke = false;
            for (int i = 0; i < move.card_ids.length(); i++) {
                if (move.from_places.at(i) != Player::PlaceSpecial) continue;
                if (move.from_pile_names.at(i).startsWith("#")) continue;
                invoke = true;
                break;
            }
            if (!invoke) return result;
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getHandcardNum() < player->getHandcardNum() && !p->isNude())
                    targets << p;
            }
            possible = !targets.isEmpty();
        }
        if (possible) addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner || !ctx.original_data) return false;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QList<ServerPlayer *> targets;
        if (move.to_place == Player::PlaceSpecial) {
            for (ServerPlayer *p : room->getOtherPlayers(ctx.owner))
                if (p->getHandcardNum() > ctx.owner->getHandcardNum()) targets << p;
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "@liangyin-draw", true, true);
            if (!target) return false;
            ctx.choice = "draw";
            ctx.targets = QList<ServerPlayer *>() << target;
        } else if (move.to && move.to_place == Player::PlaceHand) {
            for (ServerPlayer *p : room->getOtherPlayers(ctx.owner))
                if (p->getHandcardNum() < ctx.owner->getHandcardNum() && !p->isNude()) targets << p;
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "@liangyin-discard", true, true);
            if (!target || !target->canDiscard(target, "he")) return false;
            ctx.choice = "discard";
            ctx.targets = QList<ServerPlayer *>() << target;
        } else return false;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!room || !ctx.owner) return false;
        if (!target) return false;
        room->broadcastSkillInvoke(objectName());
        if (ctx.choice == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
        else room->askForDiscard(target, objectName(), getEffectiveAmount(ctx), getEffectiveAmount(ctx), false, true);
        return false;
    }
};

class Kongsheng : public TriggerSkillV2
{
public:
    Kongsheng() : TriggerSkillV2("kongsheng") { events << EventPhaseStart; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive() && ((player->getPhase() == Player::Start && !player->isNude())
            || (player->getPhase() == Player::Finish && !player->getPile(objectName()).isEmpty()))) addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ctx.choice = ctx.owner->getPhase() == Player::Start ? "put" : "get";
        if (ctx.choice == "put") {
            const Card *card = room->askForExchange(ctx.owner, objectName(), ctx.owner->getCards("he").size(), 1, true, "kongsheng-put", true);
            if (!card) return false;
            ctx.extra_data = ListI2V(card->getSubcards());
        }
        ctx.targets << ctx.owner; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.invoker || getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "put") {
            QList<int> ids;
            for (int id : ListV2I(ctx.extra_data.toList()))
                if (room->getCardOwner(id) == ctx.invoker && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) ids << id;
            if (!ids.isEmpty()) target->addToPile(objectName(), ids);
        } else {
            const QList<int> pile = target->getPile(objectName());
            for (int id : pile) {
                if (!target->isAlive()) break;
                if (!target->getPile(objectName()).contains(id)) continue;
                const Card *card = Sanguosha->getCard(id);
                if (card && card->isKindOf("EquipCard") && card->isAvailable(target) && !target->isProhibited(target, card))
                    room->useCardFromSkillEffect(CardUseStruct(card, target, target), ctx, true);
            }
            // Re-read the pile after every ordinary use and its nested movement callbacks.
            QList<int> remaining;
            for (int id : pile) if (target->getPile(objectName()).contains(id)) remaining << id;
            if (target->isAlive() && !remaining.isEmpty()) { DummyCard card(remaining); room->obtainCard(target, &card, true); }
        }
        return false;
    }
};

class QianjieChain : public TriggerSkillV2
{
public:
    QianjieChain() : TriggerSkillV2("#qianjie-chain")
    {
        events << ChainStateChange;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == ChainStateChange && player && player->isAlive() && !player->isChained())
            addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner != nullptr;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, "qianjie", true, true);
        return true;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.owner) ctx.targets = QList<ServerPlayer *>() << ctx.owner; return false; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    { return getEffectiveAmount(ctx) > 0; }
};

class Qianjie : public ProhibitSkill
{
public:
    Qianjie() : ProhibitSkill("qianjie")
    {
    }

    bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return card->isKindOf("DelayedTrick")&&to->hasSkill("qianjie");
    }
};

class QianjiePindianPro : public ProhibitPindianSkill
{
public:
    QianjiePindianPro() : ProhibitPindianSkill("#qianjiepindianpro")
    {
    }

    bool isPindianProhibited(const Player *from, const Player *to) const
    {
        return to->hasSkill("qianjie") && from != to;
    }
};

JueyanCard::JueyanCard()
{
    setSkillName("jueyan");
    target_fixed = true;
}

void JueyanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QStringList choices;
    if (source->hasEquipArea(0)) choices << "0";
    if (source->hasEquipArea(1)) choices << "1";
    if (source->hasEquipArea(2) || source->hasEquipArea(3)) choices << "23";
    if (source->hasEquipArea(4)) choices << "4";
    if (choices.isEmpty()) return;
    QString choice = room->askForChoice(source, "jueyan", choices.join("+"), QVariant());
    if (choice == "0") {
        source->throwEquipArea(0);
        room->addSlashCishu(source, 3);
    } else if (choice == "1") {
        source->throwEquipArea(1);
        source->drawCards(3, objectName());
        room->addMaxCards(source, 3);
    } else if (choice == "23") {
        QList<int> list;
        list << 2 << 3;
        source->throwEquipArea(list);
        room->setPlayerFlag(source, "jueyan_distance");
    } else {
        source->throwEquipArea(4);
        if (!source->hasSkill("jizhi")) {
            room->acquireOneTurnSkills(source, "jueyan", "tenyearjizhi");
        }
    }
}

class Jueyan : public ViewAsSkillV2
{
public:
    Jueyan() : ViewAsSkillV2("jueyan") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
        && request.initiator->hasEquipArea(); }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
    const Card *createCard(const ActiveSkillRequest &request) const override { return ViewAsSkillV2::createCard(request); }
    bool willThrowSelectedCards() const override { return false; }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.invoker) return false;
        QStringList choices;
        for (int area : {0, 1, 4}) if (ctx.invoker->hasEquipArea(area)) choices << QString::number(area);
        if (ctx.invoker->hasEquipArea(2) || ctx.invoker->hasEquipArea(3)) choices << "23";
        if (choices.isEmpty()) return false;
        ctx.choice = room->askForChoice(ctx.invoker, objectName(), choices.join("+"));
        return choices.contains(ctx.choice);
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator) return false;
        if (ctx.choice == "23") {
            if (!ctx.initiator->hasEquipArea(2) && !ctx.initiator->hasEquipArea(3)) return false;
            ctx.initiator->throwEquipArea(QList<int>() << 2 << 3);
        } else {
            const int area = ctx.choice.toInt();
            if (!ctx.initiator->hasEquipArea(area)) return false;
            ctx.initiator->throwEquipArea(area);
        }
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    { if (ctx.invoker) skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override;
    QString historyKey(const ActiveSkillRequest &) const override { return "JueyanCard"; }
};

class JueyanClear : public TriggerSkillV2
{
public:
    JueyanClear() : TriggerSkillV2("#jueyan-clear") { events << EventPhaseChanging; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        for (ServerPlayer *player : room->getAllPlayers(true)) {
            const QVariantList grants = player->getTag("JueyanGrants").toList();
            player->removeTag("JueyanGrants");
            for (const QVariant &value : grants) {
                const QVariantMap grant = value.toMap();
                const int id = grant.value("id").toInt();
                if (id > 0) room->detachSkillFromPlayer(player, SkillInstanceUtils::formatName("tenyearjizhi", id), false, true);
            }
        }
        return true;
    }
};

class JueyanTargetMod : public TargetModSkillV2
{
public:
    JueyanTargetMod() : TargetModSkillV2("#jueyantargetmod", ".") { setHolderSelector(CorrectSkill_System); setBaseAmount(1000); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        const Player *from = context.getPrimary();
        return context.modType == TargetModSkill::DistanceLimit && from && from->hasFlag("jueyan_distance")
            ? CorrectSkillResult::useAmount(context.currentAmount)
                                                          : CorrectSkillResult::noEffect();
    }
};

class Poshi : public TriggerSkillV2
{
public:
    Poshi() : TriggerSkillV2("poshi")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
        waked_skills = "huairou";
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Start
            && player->hasSkill(objectName()) && ((!player->hasEquipArea() || player->getHp() == 1) || player->canWake(objectName()))) {
            for (int id : player->getValidSkillInstanceIds(objectName()))
            {
                SkillContext eligibility; eligibility.owner = eligibility.invoker = player;
                eligibility.skill_name = objectName(); eligibility.instanceID = id;
                eligibility.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
                if (isUsable(eligibility)) result[player] << SkillInstanceUtils::formatName(objectName(), id);
            }
        }
        return result;
    }

    LimitScope getLimitScope() const override { return Limit_Game; }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = QList<ServerPlayer *>() << ctx.owner; return isUsable(ctx); }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }


    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.targets.isEmpty() && ctx.owner) ctx.targets << ctx.owner; return false; }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
		if (!room || !ctx.owner) return false;
        ServerPlayer *player = target;
        if (!player || !player->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        QList<int> oldJueyan;
        int nativeSlot = -1;
        for (const SkillInstance &instance : ctx.owner->getSkillInstances())
            if (instance.skillName == objectName() && instance.instanceID == ctx.activationRef.key.instanceID && instance.source == SourceInnate)
                nativeSlot = instance.bindHead;
        if (player == ctx.owner && nativeSlot >= 0)
            for (const SkillInstance &instance : player->getSkillInstances())
                if (instance.skillName == "jueyan" && instance.source == SourceInnate && instance.bindHead == nativeSlot) oldJueyan << instance.instanceID;
		{
			room->sendCompulsoryTriggerLog(player, this);
			room->doSuperLightbox(player, "poshi");
			room->setPlayerMark(player, "poshi", 1);
			if (room->changeMaxHpForAwakenSkill(player, -getEffectiveAmount(ctx), objectName())) {
				if (player->getHandcardNum() < player->getMaxHp())
					player->drawCards((player->getMaxHp() - player->getHandcardNum()) * getEffectiveAmount(ctx), objectName());
				for (int id : oldJueyan) room->detachSkillFromPlayer(player, SkillInstanceUtils::formatName("jueyan", id));
                room->acquireSkill(player, "huairou");
			}
		}
        return false;
    }
};

HuairouCard::HuairouCard()
{
    setSkillName("huairou");
    target_fixed = true;
    will_throw = false;
    can_recast = true;
    handling_method = Card::MethodRecast;
}

void HuairouCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    LogMessage log;
    log.type = "#UseCard_Recast";
    log.from = source;
    log.card_str = QString::number(getSubcards().first());
    room->sendLog(log);
    room->moveCardTo(this, source, nullptr, Player::DiscardPile, CardMoveReason(CardMoveReason::S_REASON_RECAST, source->objectName(), getSkillName(), ""));
    source->drawCards(1, "recast");
}

class Huairou : public ViewAsSkillV2
{
public:
    Huairou() : ViewAsSkillV2("huairou", 1) {}
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    { return request.initiator && candidate && candidate->isKindOf("EquipCard")
        && !request.initiator->isCardLimited(candidate, Card::MethodRecast)
        && (request.initiator->handCards().contains(candidate->getEffectiveId()) || candidate->isEquipped()); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.size() == 1; }
    const Card *createCard(const ActiveSkillRequest &request) const override { return ViewAsSkillV2::createCard(request); }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return NoTarget; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || !ctx.use_card || request.selectedCardIds.size() != 1) return false;
        const int id = request.selectedCardIds.first();
        if (room->getCardOwner(id) != ctx.initiator || !canSelectCard(request, Sanguosha->getCard(id))) return false;
        LogMessage log; log.type = "#UseCard_Recast"; log.from = ctx.initiator;
        log.card_str = QString::number(id); room->sendLog(log);
        room->moveCardTo(ctx.use_card, ctx.initiator, nullptr, Player::DiscardPile,
            CardMoveReason(CardMoveReason::S_REASON_RECAST, ctx.initiator->objectName(), objectName(), ""));
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    { if (ctx.invoker) skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override;
    QString historyKey(const ActiveSkillRequest &) const override { return "HuairouCard"; }
};

class Zhengu : public TriggerSkillV2
{
public:
    Zhengu() : TriggerSkillV2("zhengu") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->getPhase() == Player::Finish)
            addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "@zhengu-invoke", true, true);
        if (!target) return false;
        ctx.targets << target; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !ctx.invoker->isAlive() || !target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        const int serial = room->getTag("ZhenguSerial").toInt() + 1;
        room->setTag("ZhenguSerial", serial);
        QVariantMap receipt{{"id", serial}, {"owner", ctx.invoker->objectName()}, {"target", target->objectName()},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}, {"stage", 0}};
        QVariantList receipts = ctx.invoker->getTag("ZhenguReceipts").toList();
        receipts << receipt; ctx.invoker->setTag("ZhenguReceipts", receipts);
        room->addPlayerMark(target, "&zhengu");
        return false;
    }
};

class ZhenguEffect : public TriggerSkillV2
{
public:
    ZhenguEffect() : TriggerSkillV2("#zhengueffect") { global = true; frequency = Compulsory; events << EventPhaseChanging << Death << EventSkillEffectFinished; }
    void advanceReceipt(Room *room, const SkillContext &ctx) const
    {
        if (!ctx.owner) return;
        QVariantList receipts = ctx.owner->getTag("ZhenguReceipts").toList();
        if (!receipts.removeOne(ctx.extra_data)) return;
        QVariantMap receipt = ctx.extra_data.toMap();
        const bool expires = receipt.value("stage").toInt() != 0;
        if (!expires) { receipt["stage"] = 1; receipts << receipt; }
        // Publish consumption before MarkChange can re-enter this dispatcher.
        ctx.owner->setTag("ZhenguReceipts", receipts);
        if (expires) {
            ServerPlayer *target = room->findPlayerByObjectName(receipt.value("target").toString(), true);
            if (target) room->removePlayerMark(target, "&zhengu");
        }
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext finished = data.value<SkillContext>();
            if (finished.skill_name == objectName() && !finished.activationRef.isValid()
                && finished.instanceID == finished.extra_data.toMap().value("id").toInt()) advanceReceipt(room, finished);
            return true;
        }
        if (event != Death) return false;
        const ServerPlayer *dead = data.value<DeathStruct>().who;
        if (!dead) return false;
        QList<ServerPlayer *> expiredTargets;
        for (ServerPlayer *owner : room->getAllPlayers(true)) {
            QVariantList kept;
            for (const QVariant &value : owner->getTag("ZhenguReceipts").toList()) {
                const QVariantMap receipt = value.toMap();
                if (owner == dead || receipt.value("target").toString() == dead->objectName()) {
                    ServerPlayer *target = room->findPlayerByObjectName(receipt.value("target").toString(), true);
                    if (target) expiredTargets << target;
                } else kept << value;
            }
            owner->setTag("ZhenguReceipts", kept);
        }
        // Retire all affected receipts before external MarkChange callbacks.
        for (ServerPlayer *target : expiredTargets) room->removePlayerMark(target, "&zhengu");
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseChanging || !player || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        for (ServerPlayer *owner : room->getAlivePlayers()) {
            for (const QVariant &value : owner->getTag("ZhenguReceipts").toList()) {
                const QVariantMap receipt = value.toMap();
                ServerPlayer *target = room->findPlayerByObjectName(receipt.value("target").toString());
                if (!target || (receipt.value("stage").toInt() == 0 ? player != owner : player != target)) continue;
                SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = owner; ctx.invoker = ctx.initiator = owner;
                ctx.original_data = &data; ctx.current_event = event; ctx.instanceID = receipt.value("id").toInt();
                ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
                ctx.is_forced = true; ctx.amount = receipt.value("amount").toInt(); ctx.extra_data = receipt; ctx.targets << target; contexts << ctx;
            }
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->isAlive() && ctx.owner->getTag("ZhenguReceipts").toList().contains(ctx.extra_data); }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Target cancellation consumes this boundary; Finished also covers whole-effect cancellation.
        advanceReceipt(room, ctx); return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !ctx.invoker->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        const int count = ctx.invoker->getHandcardNum();
        if (target->getHandcardNum() < count) target->drawCards(qMax(0, qMin(count, 5) - target->getHandcardNum()), "zhengu");
        else if (target->getHandcardNum() > count) {
            const int discard = target->getHandcardNum() - count;
            room->askForDiscard(target, "zhengu", discard, discard, false, false);
        }
        return false;
    }
};

class Zhengrong : public TriggerSkillV2
{
public:
    Zhengrong() : TriggerSkillV2("zhengrong")
    {
        events << Damage;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != Damage || !player || !player->isAlive()) return result;
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to && !damage.to->isDead() && damage.to != player && damage.to->getHandcardNum() > player->getHandcardNum() && !damage.to->isNude())
            addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.to || !ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(damage.to))) return false;
        ctx.targets << damage.to; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "receive") {
            const QVariantMap selected = ctx.extra_data.toMap();
            ServerPlayer *holder = room->findPlayerByObjectName(selected.value("holder").toString());
            const int id = selected.value("card", -1).toInt();
            if (getEffectiveAmount(ctx) > 0 && holder && holder->isAlive() && id >= 0
                && room->getCardOwner(id) == holder && !Sanguosha->getCard(id)->hasFlag("using")
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip))
                target->addToPile("rong", id);
            return false;
        }
        const int amount = getEffectiveAmount(ctx);
        for (int n = 0; ctx.invoker && ctx.invoker->isAlive() && target && target->isAlive()
            && !target->isNude() && n < amount; ++n) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName());
            if (id < 0) break;
            // The losing holder and the pile recipient are separate effect targets.
            SkillContext receive = ctx;
            receive.choice = "receive";
            receive.extra_data = QVariantMap{{"holder", target->objectName()}, {"card", id}};
            skillEffect(event, room, owner, receive, ctx.invoker);
        }
        return false;
    }

};

HongjuCard::HongjuCard()
{
    setSkillName("hongju");
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
    target_fixed = true;
}

void HongjuCard::onUse(Room *room, CardUseStruct &card_use) const
{
    QList<int> pile = card_use.from->getPile("rong");
    QList<int> subCards = card_use.card->getSubcards();
    QList<int> to_handcard;
    QList<int> to_pile;
    foreach (int id, subCards) {
        if (pile.contains(id))
            to_handcard << id;
        else
            to_pile << id;
    }

    Q_ASSERT(to_handcard.length() == to_pile.length());

    if (to_pile.length() == 0 || to_handcard.length() != to_pile.length())
        return;

    LogMessage log;
    log.type = "#QixingExchange";
    log.from = card_use.from;
    log.arg = QString::number(to_pile.length());
    log.arg2 = "hongju";
    room->sendLog(log);

    card_use.from->addToPile("rong", to_pile);

    DummyCard to_handcard_x(to_handcard);
    CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, card_use.from->objectName());
    room->obtainCard(card_use.from, &to_handcard_x, reason, true);
}

class HongjuVS : public ViewAsSkillV2
{
public:
    HongjuVS() : ViewAsSkillV2("hongju") { expand_pile = "rong"; }
    TargetMode targetMode() const override { return NoTarget; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.getPattern() == "@@hongju"; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->isEquipped()) return false;
        const int limit = 2 * request.initiator->getPile("rong").length();
        if (request.selectedCardIds.size() >= limit) return false;
        const int id = to_select->getEffectiveId();
        return request.initiator->handCards().contains(id) || request.initiator->getPile("rong").contains(id);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.isEmpty()) return false;
        int hand = 0, pile = 0;
        for (int id : request.selectedCardIds) {
            if (request.initiator->handCards().contains(id)) ++hand;
            else if (request.initiator->getPile("rong").contains(id)) ++pile;
        }
        return hand == pile && hand > 0 && hand + pile == request.selectedCardIds.size();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override { return ViewAsSkillV2::createCard(request); }
    bool willThrowSelectedCards() const override { return false; }
    EffectFlow effect(SkillContext &ctx) const override
    { if (ctx.invoker) skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override;
};

class Hongju : public TriggerSkillV2
{
public:
    Hongju() : TriggerSkillV2("hongju")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
        view_as_skill = new HongjuVS;
        waked_skills = "qingce";
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Start
            && player->hasSkill(objectName())
            && (player->getPile("rong").length() >= 3 && player->aliveCount() < player->getSiblings(true).length() + 1
                || player->canWake("hongju"))) {
            for (int id : player->getValidSkillInstanceIds(objectName()))
            {
                SkillContext eligibility; eligibility.owner = eligibility.invoker = player;
                eligibility.skill_name = objectName(); eligibility.instanceID = id;
                eligibility.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
                if (isUsable(eligibility)) result[player] << SkillInstanceUtils::formatName(objectName(), id);
            }
        }
        return result;
    }

    LimitScope getLimitScope() const override { return Limit_Game; }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = QList<ServerPlayer *>() << ctx.owner; return isUsable(ctx); }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }


    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.targets.isEmpty() && ctx.owner) ctx.targets << ctx.owner; return false; }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
		if (!room || !ctx.owner) return false;
		ServerPlayer *player = target;
        if (!player || !player->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        Room::AcceptedViewAsEffectScope prompt(room, player, objectName(), ctx);
        room->sendCompulsoryTriggerLog(player, this);
        room->doSuperLightbox(player, "hongju");
        room->setPlayerMark(player, "hongju", 1);
        if (prompt.isValid() && !player->isKongcheng())
            room->askForUseCard(player, "@@hongju", "@hongju");
        if (player->isAlive() && room->changeMaxHpForAwakenSkill(player, -getEffectiveAmount(ctx), objectName()))
            room->handleAcquireDetachSkills(player, "qingce");
        return false;
    }
};

QingceCard::QingceCard()
{
    setSkillName("qingce");
    handling_method = Card::MethodNone;
    will_throw = false;
}

bool QingceCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.isEmpty() && to_select->getCardCount(true, true) > to_select->getHandcardNum();
}

void QingceCard::onEffect(CardEffectStruct &effect) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", "qingce", "");
    Room *room = effect.to->getRoom();
    room->throwCard(this, reason, nullptr);
    if (effect.to->getCards("ej").isEmpty()) return;
    int card_id = room->askForCardChosen(effect.from, effect.to, "ej", "qingce", false, Card::MethodDiscard);
    room->throwCard(card_id, room->getCardPlace(card_id) == Player::PlaceDelayedTrick ? nullptr : effect.to, effect.from);
}

class Qingce : public ViewAsSkillV2
{
public:
    Qingce() : ViewAsSkillV2("qingce", 1) { expand_pile = "rong"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->getPile("rong").isEmpty(); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    { return request.initiator && candidate && request.initiator->getPile("rong").contains(candidate->getEffectiveId()); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.size() == 1; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return request.initiator && selected.isEmpty() && candidate && candidate->getCardCount(true, true) > candidate->getHandcardNum(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    const Card *createCard(const ActiveSkillRequest &request) const override { return ViewAsSkillV2::createCard(request); }
    bool willThrowSelectedCards() const override { return false; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || request.selectedCardIds.size() != 1) return false;
        const int id = request.selectedCardIds.first();
        if (!ctx.initiator->getPile("rong").contains(id)) return false;
        room->throwCard(Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE,
            ctx.initiator->objectName(), objectName(), ""), nullptr);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override;
    QString historyKey(const ActiveSkillRequest &) const override { return "QingceCard"; }
};

class Leiyongsi : public TriggerSkillV2
{
public:
    Leiyongsi() : TriggerSkillV2("leiyongsi") { events << DrawNCards << EventPhaseEnd; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (player && player->isAlive() && ((event == DrawNCards && data.value<DrawStruct>().reason == "draw_phase")
            || (event == EventPhaseEnd && player->getPhase() == Player::Play))) addAllInstances(result, player, objectName());
        return result;
    }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.targets.isEmpty()) ctx.targets << ctx.owner; return false; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.original_data) return false;
        const int amount = getEffectiveAmount(ctx);
        if (event == DrawNCards) {
            QStringList kingdoms; for (ServerPlayer *p : room->getAlivePlayers()) if (!kingdoms.contains(p->getKingdom())) kingdoms << p->getKingdom();
            DrawStruct draw = ctx.original_data->value<DrawStruct>(); draw.num = kingdoms.size() * amount;
            *ctx.original_data = QVariant::fromValue(draw); return false;
        }
        const QVariant phase = room->historyScopes().value("phase_id");
        if (phase.toLongLong() <= 0) return false;
        QVariantMap filter{{"phase_id", phase}, {"from", ctx.owner->objectName()}}; int points = 0;
        for (;;) {
            const QVariantMap page = room->queryActualDamage(filter);
            if (page.contains("error") || !page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return false;
            if (!filter.contains("watermark")) filter["watermark"] = page.value("watermark");
            for (const QVariant &item : page.value("items").toList()) points += item.toMap().value("data").toMap().value("amount").toInt();
            if (!page.value("has_more").toBool()) break;
            filter["after"] = page.value("next_after");
        }
        if (points == 0) target->drawCards(qMax(0, target->getHp() - target->getHandcardNum()) * amount, objectName());
        else if (points > 1) room->setPlayerMark(target, "leiyongsi-Clear", amount);
        return false;
    }
};
class LeiyongsiMaxCards : public MaxCardsSkillV2
{
public:
    LeiyongsiMaxCards() : MaxCardsSkillV2("#leiyongsimaxmards") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override
    {
        const Player *target = ctx.primary;
        return target && target->getMark("leiyongsi-Clear") > 0
            ? CorrectSkillResult::useAmount(target->getLostHp() * target->getMark("leiyongsi-Clear") * ctx.currentAmount)
            : CorrectSkillResult::noEffect();
    }
};

class Leiweidi : public TriggerSkillV2
{
public:
    Leiweidi() : TriggerSkillV2("leiweidi$") { events << EventPhaseEnd; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->getPhase() == Player::Discard && player->hasLordSkill(this))
            addAllInstances(result, player, objectName());
        return result;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.invoker) return false;
        const QVariant phase = room->historyScopes().value("phase_id");
        if (phase.toLongLong() <= 0) return false;
        QVariantMap filter{{"phase_id", phase}, {"from", ctx.owner->objectName()}};
        QList<int> pool;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return false;
            if (!filter.contains("watermark")) filter["watermark"] = page.value("watermark");
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap move = value.toMap().value("data").toMap(); const int id = move.value("card_id", -1).toInt();
                if ((move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD
                    && id >= 0 && room->getCardPlace(id) == Player::DiscardPile && !pool.contains(id)) pool << id;
            }
            if (!page.value("has_more").toBool()) break;
            filter["after"] = page.value("next_after");
        }
        QList<ServerPlayer *> targets; for (ServerPlayer *p : room->getOtherPlayers(ctx.invoker)) if (p->getKingdom() == "qun") targets << p;
        while (!pool.isEmpty() && !targets.isEmpty() && ctx.invoker->isAlive()) {
            ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, targets, objectName(), "leiweidi-give", true, true);
            if (!target) break;
            targets.removeOne(target);
            QList<int> selected;
            for (int n = 0; n < getEffectiveAmount(ctx) && !pool.isEmpty(); ++n) {
                for (int i = pool.size() - 1; i >= 0; --i) if (room->getCardPlace(pool.at(i)) != Player::DiscardPile) pool.removeAt(i);
                if (pool.isEmpty()) break;
                room->fillAG(pool, ctx.invoker);
                const auto clear = qScopeGuard([&] { room->clearAG(ctx.invoker); });
                const int id = room->askForAG(ctx.invoker, pool, false, objectName());
                if (!pool.removeOne(id)) break; selected << id;
            }
            ctx.extra_data = ListI2V(selected);
            skillEffect(event, room, player, ctx, target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QList<int> ids; for (int id : ListV2I(ctx.extra_data.toList())) if (room->getCardPlace(id) == Player::DiscardPile) ids << id;
        if (!ids.isEmpty()) {
            DummyCard cards(ids);
            room->obtainCard(target, &cards, CardMoveReason(CardMoveReason::S_REASON_RECYCLE, ctx.invoker->objectName(), target->objectName(), objectName(), ""), true);
        }
        return false;
    }
};

class Congjian : public TriggerSkillV2
{
public:
    Congjian() : TriggerSkillV2("congjian") { events << TargetConfirmed; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result; const CardUseStruct use = data.value<CardUseStruct>();
        if (player && player->isAlive() && !player->isNude() && use.card && use.card->isKindOf("TrickCard")
            && use.to.size() > 1 && use.to.contains(player)) addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        QList<ServerPlayer *> targets = ctx.original_data->value<CardUseStruct>().to; targets.removeAll(ctx.owner);
        const Card *gift = room->askForExchange(ctx.owner, objectName(), 1, 1, true, "@congjian-give", true);
        if (!gift || gift->subcardsLength() != 1) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "@congjian-give");
        if (!target) return false;
        ctx.extra_data = gift->getSubcards().first(); ctx.targets << target; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !ctx.invoker->isAlive()) return false;
        if (ctx.choice == "draw") { target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName()); return false; }
        const int id = ctx.extra_data.toInt(); const Card *card = Sanguosha->getCard(id);
        if (!card || room->getCardOwner(id) != ctx.invoker || !ctx.invoker->getCards("he").contains(card) || card->hasFlag("using")) return false;
        const int draw = card->isKindOf("EquipCard") ? 2 : 1;
        room->obtainCard(target, card, CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.invoker->objectName(), target->objectName(), objectName(), ""), false);
        ctx.choice = "draw"; ctx.extra_data = draw;
        skillEffect(event, room, player, ctx, ctx.invoker);
        return false;
    }
    int getEffectIndex(const ServerPlayer *player, const Card *) const override
    { return player->getGeneralName().contains("sp_tongyuan") || player->getGeneral2Name().contains("sp_tongyuan") ? 3 : qsanRandomBounded(2) + 1; }
};

XiongluanCard::XiongluanCard()
{
    setSkillName("xiongluan");
}

bool XiongluanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void XiongluanCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    room->removePlayerMark(effect.from, "@xiongluanMark");
    room->doSuperLightbox(effect.from, "xiongluan");
    effect.from->throwJudgeArea();
    effect.from->throwEquipArea();

    room->addPlayerMark(effect.from, "xiongluan_from-Clear");
    room->addPlayerMark(effect.to, "xiongluan_to-Clear");
}

class Xiongluan : public ViewAsSkillV2
{
public:
    Xiongluan() : ViewAsSkillV2("xiongluan")
    {
        frequency = Limited;
        limit_mark = "@xiongluanMark";
    }
    LimitScope getLimitScope() const override { return Limit_Game; }

    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
        && (request.initiator->hasJudgeArea() || request.initiator->hasEquipArea()); }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return request.initiator && selected.isEmpty() && candidate && candidate != request.initiator; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    const Card *createCard(const ActiveSkillRequest &request) const override { return ViewAsSkillV2::createCard(request); }
    bool willThrowSelectedCards() const override { return false; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.invoker || (!ctx.invoker->hasJudgeArea() && !ctx.invoker->hasEquipArea())) return false;
        if (ctx.invoker->getMark("@xiongluanMark") > 0) room->removePlayerMark(ctx.invoker, "@xiongluanMark");
        ctx.invoker->throwJudgeArea();
        ctx.invoker->throwEquipArea();
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override;
    QString historyKey(const ActiveSkillRequest &) const override { return "XiongluanCard"; }
};

class XiongluanTargetMod : public TargetModSkillV2
{
public:
    XiongluanTargetMod() : TargetModSkillV2("#xiongluan-target", "^SkillCard")
    {
        frequency = Limited;
        setHolderSelector(CorrectSkill_System);
        setBaseAmount(1000);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        const Player *from = context.getPrimary();
        const Player *to = context.getSecondary();
        if (from && to && from->property("XiongluanTargets").toStringList().contains(to->objectName())) {
            if (context.modType == TargetModSkill::Residue) return CorrectSkillResult::unlimitedResidue();
            if (context.modType == TargetModSkill::DistanceLimit) return CorrectSkillResult::useAmount(context.currentAmount);
        }
        return CorrectSkillResult::noEffect();
    }
};

class XiongluanClear : public TriggerSkillV2
{
public:
    XiongluanClear() : TriggerSkillV2("#xiongluan-clear") { global = true; events << EventPhaseChanging; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (data.value<PhaseChangeStruct>().to == Player::NotActive)
            for (ServerPlayer *player : room->getAllPlayers())
                room->setPlayerProperty(player, "XiongluanTargets", QStringList());
        return true;
    }
};

class OLZhengrong : public TriggerSkillV2
{
public:
    OLZhengrong() : TriggerSkillV2("olzhengrong")
    {
        events << TargetSpecified;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != TargetSpecified || !player || !player->isAlive()) return result;
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || (!use.card->isKindOf("Slash") && !(use.card->isKindOf("TrickCard") && use.card->isDamageCard()))) return result;
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, use.to) {
            if (p->getHandcardNum() >= player->getHandcardNum() && !p->isNude())
                targets << p;
        }
        if (targets.isEmpty()) return result;
        addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner || !ctx.original_data) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QList<ServerPlayer *> targets;
        for (ServerPlayer *p : use.to) if (p && p->getHandcardNum() >= ctx.owner->getHandcardNum() && !p->isNude()) targets << p;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "@olzhengrong-invoke", true, true);
        if (!target) return false;
        ctx.targets = QList<ServerPlayer *>() << target;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "receive") {
            const QVariantMap selected = ctx.extra_data.toMap();
            ServerPlayer *holder = room->findPlayerByObjectName(selected.value("holder").toString());
            const int id = selected.value("card", -1).toInt();
            if (getEffectiveAmount(ctx) > 0 && holder && holder->isAlive() && id >= 0
                && room->getCardOwner(id) == holder && !Sanguosha->getCard(id)->hasFlag("using")
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip))
                target->addToPile("rong", id);
            return false;
        }
        const int amount = getEffectiveAmount(ctx);
        for (int n = 0; ctx.invoker && ctx.invoker->isAlive() && target && target->isAlive()
            && !target->isNude() && n < amount; ++n) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName());
            if (id < 0) break;
            // The losing holder and the pile recipient are separate effect targets.
            SkillContext receive = ctx;
            receive.choice = "receive";
            receive.extra_data = QVariantMap{{"holder", target->objectName()}, {"card", id}};
            skillEffect(event, room, owner, receive, ctx.invoker);
        }
        return false;
    }

};

OLHongjuCard::OLHongjuCard()
{
    setSkillName("olhongju");
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
    target_fixed = true;
}

void OLHongjuCard::onUse(Room *room, CardUseStruct &card_use) const
{
    QList<int> pile = card_use.from->getPile("rong");
    QList<int> subCards = card_use.card->getSubcards();
    QList<int> to_handcard;
    QList<int> to_pile;
    foreach (int id, subCards) {
        if (pile.contains(id))
            to_handcard << id;
        else
            to_pile << id;
    }

    Q_ASSERT(to_handcard.length() == to_pile.length());

    if (to_pile.length() == 0 || to_handcard.length() != to_pile.length())
        return;

    LogMessage log;
    log.type = "#QixingExchange";
    log.from = card_use.from;
    log.arg = QString::number(to_pile.length());
    log.arg2 = "olhongju";
    room->sendLog(log);

    card_use.from->addToPile("rong", to_pile);

    DummyCard to_handcard_x(to_handcard);
    CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, card_use.from->objectName());
    room->obtainCard(card_use.from, &to_handcard_x, reason, true);
}

class OLHongjuVS : public ViewAsSkillV2
{
public:
    OLHongjuVS() : ViewAsSkillV2("olhongju") { expand_pile = "rong"; }
    TargetMode targetMode() const override { return NoTarget; }
    bool canActivate(const ActiveSkillRequest &request) const override { return request.initiator && request.getPattern() == "@@olhongju"; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->isEquipped()) return false;
        if (request.selectedCardIds.size() >= 2 * request.initiator->getPile("rong").length()) return false;
        const int id = to_select->getEffectiveId();
        return request.initiator->handCards().contains(id) || request.initiator->getPile("rong").contains(id);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.isEmpty()) return false;
        int hand = 0, pile = 0; for (int id : request.selectedCardIds) { if (request.initiator->handCards().contains(id)) ++hand; else if (request.initiator->getPile("rong").contains(id)) ++pile; }
        return hand == pile && hand > 0 && hand + pile == request.selectedCardIds.size();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override { return ViewAsSkillV2::createCard(request); }
    bool willThrowSelectedCards() const override { return false; }
    EffectFlow effect(SkillContext &ctx) const override
    { if (ctx.invoker) skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override;
};

class OLHongju : public TriggerSkillV2
{
public:
    OLHongju() : TriggerSkillV2("olhongju")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
        view_as_skill = new OLHongjuVS;
        waked_skills = "olqingce";
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Start
            && player->hasSkill(objectName()) && ((player->getPile("rong").size() >= 3) || player->canWake(objectName()))) {
            for (int id : player->getValidSkillInstanceIds(objectName()))
            {
                SkillContext eligibility; eligibility.owner = eligibility.invoker = player;
                eligibility.skill_name = objectName(); eligibility.instanceID = id;
                eligibility.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
                if (isUsable(eligibility)) result[player] << SkillInstanceUtils::formatName(objectName(), id);
            }
        }
        return result;
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = QList<ServerPlayer *>() << ctx.owner; return isUsable(ctx); }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }


    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.targets.isEmpty() && ctx.owner) ctx.targets << ctx.owner; return false; }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
		if (!room || !ctx.owner) return false;
		ServerPlayer *player = target;
        if (!player || !player->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        Room::AcceptedViewAsEffectScope prompt(room, player, objectName(), ctx);
        room->sendCompulsoryTriggerLog(player, this);
        room->doSuperLightbox(player, "olhongju");
        room->setPlayerMark(player, "olhongju", 1);
        if (prompt.isValid() && !player->isKongcheng())
            room->askForUseCard(player, "@@olhongju", "@olhongju");
        if (player->isAlive() && room->changeMaxHpForAwakenSkill(player, -getEffectiveAmount(ctx), objectName()))
            room->handleAcquireDetachSkills(player, "olqingce");
        return false;
    }
};

OLQingceCard::OLQingceCard()
{
    setSkillName("olqingce");
    will_throw = false;
}

bool OLQingceCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.isEmpty() && to_select->getCardCount(true, true) > to_select->getHandcardNum();
}

void OLQingceCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    //把顺序调整成先获得“荣”，再弃牌
    QList<int> list;
    foreach (int id, this->getSubcards()) {
        if (effect.from->getPile("rong").contains(id))
            list << id;
    }
    foreach (int id, this->getSubcards()) {
        if (!list.contains(id))
            list << id;
    }

    foreach (int id, list) {
        if (effect.from->getPile("rong").contains(id)) {
            LogMessage log;
            log.type = "$KuangbiGet";
            log.from = effect.from;
            log.arg = "rong";
            log.card_str = Sanguosha->getCard(id)->toString();
            room->sendLog(log);
            room->obtainCard(effect.from, id, true);
        }
        else {
            CardMoveReason reason(CardMoveReason::S_REASON_THROW, effect.from->objectName(), "olqingce", "");
            room->throwCard(Sanguosha->getCard(id), reason, effect.from, nullptr);
        }
    }

    if (effect.to->getCards("ej").isEmpty()) return;
    int card_id = room->askForCardChosen(effect.from, effect.to, "ej", "olqingce", false, Card::MethodDiscard);
    room->throwCard(card_id, room->getCardPlace(card_id) == Player::PlaceDelayedTrick ? nullptr : effect.to, effect.from);
}

class OLQingce : public ViewAsSkillV2
{
public:
    OLQingce() : ViewAsSkillV2("olqingce", 2) { expand_pile = "rong"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->getPile("rong").isEmpty(); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->isEquipped() || request.selectedCardIds.size() >= 2) return false;
        const bool firstPile = !request.selectedCardIds.isEmpty() && request.initiator->getPile("rong").contains(request.selectedCardIds.first());
        const int id = to_select->getEffectiveId();
        if (request.selectedCardIds.isEmpty()) return request.initiator->handCards().contains(id) || request.initiator->getPile("rong").contains(id);
        return firstPile ? request.initiator->handCards().contains(id) && !request.initiator->isJilei(to_select)
                         : request.initiator->getPile("rong").contains(id);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.size() != 2) return false;
        const bool firstPile = request.initiator->getPile("rong").contains(request.selectedCardIds.first());
        const bool secondPile = request.initiator->getPile("rong").contains(request.selectedCardIds.last());
        const int handId = request.selectedCardIds.at(firstPile ? 1 : 0);
        return firstPile != secondPile && request.initiator->handCards().contains(handId)
            && !request.initiator->isJilei(Sanguosha->getCard(handId));
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return request.initiator && selected.isEmpty() && candidate && candidate->getCardCount(true, true) > candidate->getHandcardNum(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    const Card *createCard(const ActiveSkillRequest &request) const override { return ViewAsSkillV2::createCard(request); }
    bool willThrowSelectedCards() const override { return false; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || !cardSelectionFeasible(request)) return false;
        const bool firstPile = ctx.initiator->getPile("rong").contains(request.selectedCardIds.first());
        const int pileId = request.selectedCardIds.at(firstPile ? 0 : 1);
        const int handId = request.selectedCardIds.at(firstPile ? 1 : 0);
        // Move the cost atomically so nested movement triggers cannot reuse either material.
        QList<CardsMoveStruct> moves;
        moves << CardsMoveStruct(QList<int>() << pileId, ctx.initiator, ctx.initiator,
            Player::PlaceSpecial, Player::PlaceHand,
            CardMoveReason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, ctx.initiator->objectName(), objectName(), ""));
        moves << CardsMoveStruct(QList<int>() << handId, ctx.initiator, nullptr,
            Player::PlaceHand, Player::DiscardPile,
            CardMoveReason(CardMoveReason::S_REASON_THROW, ctx.initiator->objectName(), objectName(), ""));
        room->moveCardsAtomic(moves, true);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override;
    QString historyKey(const ActiveSkillRequest &) const override { return "OLQingceCard"; }
};

class MobileZhengrong : public TriggerSkillV2
{
public:
    MobileZhengrong() : TriggerSkillV2("mobilezhengrong")
    {
        events << CardUsed;
        frequency = Compulsory;
        global = true;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == CardUsed && player && player->isAlive() && player->getPhase() == Player::Play
            && player->hasSkill(objectName()) && data.value<CardUseStruct>().card
            && !data.value<CardUseStruct>().card->isKindOf("SkillCard")
            && room->countHistoryCards(player, "phase") > 0 && room->countHistoryCards(player, "phase") % 2 == 0) {
            const CardUseStruct use = data.value<CardUseStruct>(); bool hasOther = false;
            for (ServerPlayer *p : use.to) if (p != player) { hasOther = true; break; }
            if (hasOther) addAllInstances(result, player, objectName());
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room || !ctx.owner) return false;
        QList<ServerPlayer *> targets; for (ServerPlayer *p : room->getOtherPlayers(ctx.owner)) if (!p->isNude()) targets << p;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "@mobilezhengrong-invoke", false, true);
        if (!target) return false; ctx.targets = QList<ServerPlayer *>() << target; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "receive") {
            const QVariantMap selected = ctx.extra_data.toMap();
            ServerPlayer *holder = room->findPlayerByObjectName(selected.value("holder").toString());
            const int id = selected.value("card", -1).toInt();
            if (getEffectiveAmount(ctx) > 0 && holder && holder->isAlive() && id >= 0
                && room->getCardOwner(id) == holder && !Sanguosha->getCard(id)->hasFlag("using")
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip))
                target->addToPile("rong", id);
            return false;
        }
        const int amount = getEffectiveAmount(ctx);
        for (int n = 0; ctx.invoker && ctx.invoker->isAlive() && target && target->isAlive()
            && !target->isNude() && n < amount; ++n) {
            const QList<const Card *> cards = target->getCards("he");
            const int id = cards.at(qsanRandomBounded(cards.size()))->getEffectiveId();
            if (id < 0) break;
            // The losing holder and the pile recipient are separate effect targets.
            SkillContext receive = ctx;
            receive.choice = "receive";
            receive.extra_data = QVariantMap{{"holder", target->objectName()}, {"card", id}};
            skillEffect(event, room, owner, receive, ctx.invoker);
        }
        return false;
    }

};

MobileHongjuCard::MobileHongjuCard()
{
    setSkillName("mobilehongju");
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
    target_fixed = true;
}

void MobileHongjuCard::onUse(Room *room, CardUseStruct &card_use) const
{
    QList<int> pile = card_use.from->getPile("rong");
    QList<int> subCards = card_use.card->getSubcards();
    QList<int> to_handcard;
    QList<int> to_pile;
    foreach (int id, subCards) {
        if (pile.contains(id))
            to_handcard << id;
        else
            to_pile << id;
    }

    Q_ASSERT(to_handcard.length() == to_pile.length());

    if (to_pile.length() == 0 || to_handcard.length() != to_pile.length())
        return;

    LogMessage log;
    log.type = "#QixingExchange";
    log.from = card_use.from;
    log.arg = QString::number(to_pile.length());
    log.arg2 = "mobilehongju";
    room->sendLog(log);

    card_use.from->addToPile("rong", to_pile);

    DummyCard to_handcard_x(to_handcard);
    CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, card_use.from->objectName());
    room->obtainCard(card_use.from, &to_handcard_x, reason, true);
}

class MobileHongjuVS : public ViewAsSkillV2
{
public:
    MobileHongjuVS() : ViewAsSkillV2("mobilehongju") { expand_pile = "rong"; }
    TargetMode targetMode() const override { return NoTarget; }
    bool canActivate(const ActiveSkillRequest &request) const override { return request.initiator && request.getPattern() == "@@mobilehongju"; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->isEquipped() || request.selectedCardIds.size() >= 2 * request.initiator->getPile("rong").length()) return false;
        const int id = to_select->getEffectiveId(); return request.initiator->handCards().contains(id) || request.initiator->getPile("rong").contains(id);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.isEmpty()) return false; int hand = 0, pile = 0;
        for (int id : request.selectedCardIds) { if (request.initiator->handCards().contains(id)) ++hand; else if (request.initiator->getPile("rong").contains(id)) ++pile; }
        return hand == pile && hand > 0 && hand + pile == request.selectedCardIds.size();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override { return ViewAsSkillV2::createCard(request); }
    bool willThrowSelectedCards() const override { return false; }
    EffectFlow effect(SkillContext &ctx) const override
    { if (ctx.invoker) skillEffect(ctx, ctx.invoker); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override;
};

class MobileHongju : public TriggerSkillV2
{
public:
    MobileHongju() : TriggerSkillV2("mobilehongju")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
        view_as_skill = new MobileHongjuVS;
        waked_skills = "qingce";
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Start
            && player->hasSkill(objectName()) && ((player->getPile("rong").size() >= 3 && player->aliveCount() < player->getSiblings(true).size() + 1) || player->canWake(objectName()))) {
            for (int id : player->getValidSkillInstanceIds(objectName()))
            {
                SkillContext eligibility; eligibility.owner = eligibility.invoker = player;
                eligibility.skill_name = objectName(); eligibility.instanceID = id;
                eligibility.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
                if (isUsable(eligibility)) result[player] << SkillInstanceUtils::formatName(objectName(), id);
            }
        }
        return result;
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = QList<ServerPlayer *>() << ctx.owner; return isUsable(ctx); }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }


    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.targets.isEmpty() && ctx.owner) ctx.targets << ctx.owner; return false; }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
		if (!room || !ctx.owner) return false; ServerPlayer *player = target;
        if (!player || !player->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        Room::AcceptedViewAsEffectScope prompt(room, player, objectName(), ctx);
        room->sendCompulsoryTriggerLog(player, this);
        room->doSuperLightbox(player, "mobilehongju");
        room->setPlayerMark(player, "mobilehongju", 1);
        player->drawCards(player->getPile("rong").length() * getEffectiveAmount(ctx), objectName());
        if (prompt.isValid() && !player->isKongcheng())
            room->askForUseCard(player, "@@mobilehongju", "@mobilehongju");
        if (player->isAlive() && room->changeMaxHpForAwakenSkill(player, -getEffectiveAmount(ctx), objectName()))
            room->handleAcquireDetachSkills(player, "qingce");
        return false;
    }
};

namespace {
static bool duoruiPending(Room *room, const Player *owner, int activation)
{
    for (ServerPlayer *target : room->getAllPlayers(true))
        for (const QVariant &value : target->getTag("DuoruiReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("owner").toString() == owner->objectName() && receipt.value("activation").toInt() == activation)
                return true;
        }
    return false;
}
static QStringList duoruiChoices(const ServerPlayer *target, bool ol)
{
    QStringList choices;
    for (const SkillInstance &instance : target->getSkillInstances()) {
        const Skill *skill = Sanguosha->getSkill(instance.skillName);
        if (instance.source != SourceInnate || !instance.visible || !skill || !skill->isVisible()) continue;
        if (!ol && (skill->isLimitedSkill() || skill->getFrequency() == Skill::Wake || skill->isLordSkill())) continue;
        choices << SkillInstanceUtils::formatName(instance.skillName, instance.instanceID);
    }
    return choices;
}
static void applyDuorui(Room *room, SkillContext &ctx, ServerPlayer *target, bool ol)
{
    if (!ctx.invoker || !ctx.invoker->isAlive() || !target || !target->isAlive()) return;
    const QVariantMap choice = ctx.extra_data.toMap();
    const QString skill = choice.value("skill").toString();
    const int instance = choice.value("instance").toInt();
    if (!target->hasSkillInstance(skill, instance)) return;
    const int serial = room->getTag("DuoruiSerial").toInt() + 1; room->setTag("DuoruiSerial", serial);
    const QString reason = QString(ol ? "olduorui:%1" : "duorui:%1").arg(serial);
    QVariantMap receipt{{"id", serial}, {"owner", ctx.invoker->objectName()}, {"activation", ctx.activationRef.key.instanceID},
        {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
        {"source_instance", ctx.sourceRef.key.instanceID}, {"skill", skill}, {"instance", instance}, {"reason", reason}, {"ol", ol}};
    // Register before callbacks: an accepted silence is independent of its granting instance.
    QVariantList receipts = target->getTag("DuoruiReceipts").toList(); receipts << receipt; target->setTag("DuoruiReceipts", receipts);
    room->addSkillInvalidity(target, skill, ctx.invoker->objectName(), reason, instance);
    if (!ol) {
        // Invalidity observers may already have ended this exact receipt.
        if (!target->getTag("DuoruiReceipts").toList().contains(receipt)) return;
        const int grant = room->acquireSkillFromEffect(ctx.invoker, skill, ctx, [&](int committedId) {
            // Publish the exact grant before any acquisition callback can expire the silence.
            QVariantList committed = target->getTag("DuoruiReceipts").toList();
            const int index = committed.indexOf(receipt);
            if (index >= 0) {
                receipt["grant"] = committedId;
                committed[index] = receipt;
                target->setTag("DuoruiReceipts", committed);
            }
        });
        QVariantList latest = target->getTag("DuoruiReceipts").toList();
        const int index = latest.indexOf(receipt);
        if (index >= 0) { receipt["grant"] = grant; latest[index] = receipt; target->setTag("DuoruiReceipts", latest); }
        else if (grant > 0) room->detachSkillFromPlayer(ctx.invoker, SkillInstanceUtils::formatName(skill, grant), false, true);
    } else ctx.invoker->endPlayPhase();
}
static void clearDuorui(Room *room, ServerPlayer *target)
{
    const QVariantList receipts = target->getTag("DuoruiReceipts").toList(); target->removeTag("DuoruiReceipts");
    for (const QVariant &value : receipts) {
        const QVariantMap receipt = value.toMap();
        const QString skill = receipt.value("skill").toString();
        room->removeSkillInvalidity(target, skill, receipt.value("owner").toString(), receipt.value("reason").toString(), receipt.value("instance").toInt());
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        const int grant = receipt.value("grant").toInt();
        if (owner && grant > 0) room->detachSkillFromPlayer(owner, SkillInstanceUtils::formatName(skill, grant), false, true);
    }
}
}
class Duorui : public TriggerSkillV2
{
public:
    Duorui() : TriggerSkillV2("duorui") { events << Damage; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result; const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || player->getPhase() != Player::Play || !player->hasEquipArea()
            || !damage.to || !damage.to->isAlive() || damage.to == player || duoruiChoices(damage.to, false).isEmpty()) return result;
        for (int id : player->getValidSkillInstanceIds(objectName()))
            if (!duoruiPending(room, player, id)) result[player] << SkillInstanceUtils::formatName(objectName(), id);
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || duoruiPending(room, ctx.owner, ctx.activationRef.key.instanceID)) return false;
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
        if (!target || !target->isAlive()) return false;
        const QStringList skills = duoruiChoices(target, false);
        if (skills.isEmpty() || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target))) return false;
        QStringList areas; for (int i = 0; i < S_EQUIP_AREA_LENGTH; ++i) if (ctx.owner->hasEquipArea(i)) areas << QString::number(i);
        if (areas.isEmpty()) return false;
        const QString selected = room->askForChoice(ctx.owner, objectName(), skills.join("+"));
        if (!skills.contains(selected)) return false;
        QString skill; const int instance = SkillInstanceUtils::parseName(selected, skill);
        ctx.extra_data = QVariantMap{{"skill", skill}, {"instance", instance},
            {"area", room->askForChoice(ctx.owner, "duorui_area", areas.join("+")).toInt()}};
        ctx.targets << target; return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const int area = ctx.extra_data.toMap().value("area", -1).toInt();
        if (!ctx.invoker || area < 0 || !ctx.invoker->hasEquipArea(area)) return false;
        ctx.invoker->throwEquipArea(area); return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { if (getEffectiveAmount(ctx) > 0) applyDuorui(room, ctx, target, false); return false; }
};
class DuoruiClear : public TriggerSkillV2
{
public:
    DuoruiClear() : TriggerSkillV2("#duorui-clear") { global = true; events << EventPhaseChanging << Death; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == Death) player = data.value<DeathStruct>().who;
        if (player && (event == Death || data.value<PhaseChangeStruct>().to == Player::NotActive)) clearDuorui(room, player);
        return true;
    }
};
class DuoruiInvalidity : public InvaliditySkill
{
public:
    DuoruiInvalidity() : InvaliditySkill("#duorui-inv") {}
    bool isSkillValid(const Player *, const Skill *) const override { return true; } // Exact instance records own the silence.
};

class Zhiti : public MaxCardsSkillV2
{
public:
    Zhiti() : MaxCardsSkillV2("zhiti") { setHolderSelector(CorrectSkill_AllHolders); setBaseAmount(-1); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.holder && ctx.primary && ctx.primary->isWounded() && ctx.holder->inMyAttackRange(ctx.primary)
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};
class ZhitiEffect : public TriggerSkillV2
{
public:
    ZhitiEffect() : TriggerSkillV2("#zhiti-effect") { events << Pindian << Damage << Damaged; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        auto eligible = [&](ServerPlayer *owner, ServerPlayer *other) {
            if (!owner || !owner->isAlive() || !other || !other->isAlive() || !other->isWounded()
                || !owner->inMyAttackRange(other)) return;
            for (int area = 0; area < S_EQUIP_AREA_LENGTH; ++area)
                if (!owner->hasEquipArea(area)) { addAllInstances(result, owner, objectName()); break; }
        };
        if (event == Pindian) {
            const PindianStruct *pd = data.value<PindianStruct *>();
            if (!pd) return result;
            // Pindian is broadcast once on its initiator, but either participant may win.
            if (pd->from_number > pd->to_number) eligible(pd->from, pd->to);
            else if (pd->to_number > pd->from_number) eligible(pd->to, pd->from);
        } else {
            const DamageStruct damage = data.value<DamageStruct>();
            if (event == Damaged) eligible(player, damage.from);
            else if (damage.card && damage.card->isKindOf("Duel")) eligible(player, damage.to);
        }
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.invoker = ctx.owner; return ctx.owner != nullptr; }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.owner) ctx.targets = QList<ServerPlayer *>() << ctx.owner; return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int n = 0; target && target->isAlive() && n < getEffectiveAmount(ctx); ++n) {
            QStringList areas;
            for (int i = 0; i < S_EQUIP_AREA_LENGTH; ++i) if (!target->hasEquipArea(i)) areas << QString::number(i);
            if (areas.isEmpty()) break;
            const QString choice = room->askForChoice(ctx.invoker, "zhiti", areas.join("+"));
            if (areas.contains(choice) && !target->hasEquipArea(choice.toInt())) target->obtainEquipArea(choice.toInt());
        }
        return false;
    }
};

PoxiCard::PoxiCard()
{
    setSkillName("poxi");
}

bool PoxiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void PoxiCard::onEffect(CardEffectStruct &effect) const
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
        room->notifyMoveToPile(effect.from, hand, "poxi", Player::PlaceHand, true);
    }

    const Card *c = room->askForUseCard(effect.from, "@@poxi", "@poxi:" + effect.to->objectName());
    if (!hand.isEmpty()) room->notifyMoveToPile(effect.from, hand, "poxi", Player::PlaceHand, false);

    if (!c) return;

    QList<int> from_ids, to_ids;
    foreach (int id, c->getSubcards()) {
        if (hand.contains(id)) to_ids << id;
        else from_ids << id;
    }

    QList<CardsMoveStruct> moves;
    if (!from_ids.isEmpty()) {
        CardMoveReason reason1(CardMoveReason::S_REASON_THROW, effect.from->objectName(), nullptr, "poxi", "");
        moves << CardsMoveStruct(from_ids, effect.from, nullptr, Player::PlaceHand, Player::DiscardPile, reason1);
        LogMessage log;
        log.type = "$DiscardCard";
        log.from = effect.from;
        log.card_str = ListI2S(from_ids).join("+");
        room->sendLog(log);
    }
    if (!to_ids.isEmpty()) {
        CardMoveReason reason2(CardMoveReason::S_REASON_DISMANTLE, effect.from->objectName(), effect.to->objectName(), "poxi", "");
        moves << CardsMoveStruct(to_ids, effect.to, nullptr, Player::PlaceHand, Player::DiscardPile, reason2);
        LogMessage log;
        log.type = "$DiscardCardByOther";
        log.from = effect.from;
        log.to << effect.to;
        log.card_str = ListI2S(to_ids).join("+");
        room->sendLog(log);
    }

    if (!moves.isEmpty()) {
        room->moveCardsAtomic(moves, true);
        switch (from_ids.length()) {
        case 0:
            if (effect.from->getMaxHp() > 0)
                room->loseMaxHp(effect.from, 1, "poxi");
            break;
        case 1:
            room->addMaxCards(effect.from, -1);
            effect.from->endPlayPhase();
            break;
        case 3:
            room->recover(effect.from, RecoverStruct("poxi", effect.from));
            break;
        case 4:
            effect.from->drawCards(4, "poxi");
            break;
        default:
            break;
        }
    }
}

PoxiDisCard::PoxiDisCard()
{
    mute = true;
    handling_method = Card::MethodDiscard;
    will_throw = false;
    target_fixed = true;
    m_skillName = "poxi";
}

void PoxiDisCard::onUse(Room *, CardUseStruct &) const
{
}

class Poxi : public ViewAsSkillV2
{
public:
    Poxi() : ViewAsSkillV2("poxi") { expand_pile = "#poxi"; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && ((request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.getPattern().isEmpty()) || (request.getPattern() == "@@poxi" && request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "accepted_view_as_effect").toBool())); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (request.getPattern() != "@@poxi" || !request.initiator || !to_select || to_select->isEquipped()
            || request.selectedCardIds.size() >= 4 || request.initiator->isJilei(to_select)) return false;
        for (int id : request.selectedCardIds) if (Sanguosha->getCard(id)->getSuit() == to_select->getSuit()) return false;
        return getExpandPileCardIds(request.initiator).contains(to_select->getEffectiveId()) || request.initiator->handCards().contains(to_select->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.getPattern() == "@@poxi" ? request.selectedCardIds.size() == 4 : request.selectedCardIds.isEmpty();
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return request.getPattern().isEmpty() && request.initiator && selected.isEmpty() && candidate && candidate != request.initiator && !candidate->isKongcheng(); }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return request.getPattern() == "@@poxi" ? targets.isEmpty() : targets.size() == 1; }
    const Card *createCard(const ActiveSkillRequest &request) const override { return ViewAsSkillV2::createCard(request); }
    bool willThrowSelectedCards() const override { return false; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override;
    QString historyKey(const ActiveSkillRequest &) const override { return "PoxiCard"; }
};

ViewAsSkillV2::EffectFlow Jueyan::effectOnTarget(SkillContext &ctx, ServerPlayer *target) const
{
    ServerPlayer *source = target;
    if (!source || !source->isAlive() || getEffectiveAmount(ctx) <= 0) return FinishSkill;
    Room *room = source->getRoom();
    const int amount = getEffectiveAmount(ctx);
    if (ctx.choice == "0") room->addSlashCishu(source, 3 * amount);
    else if (ctx.choice == "1") { source->drawCards(3 * amount, objectName()); room->addMaxCards(source, 3 * amount); }
    else if (ctx.choice == "23") room->setPlayerFlag(source, "jueyan_distance");
    else if (!source->hasSkill("jizhi") && !source->hasSkill("tenyearjizhi")) {
        const int serial = room->getTag("JueyanGrantSerial").toInt() + 1; room->setTag("JueyanGrantSerial", serial);
        QVariantList pending = source->getTag("JueyanGrants").toList();
        QVariantMap receipt{{"serial", serial}, {"id", 0}}; pending << receipt; source->setTag("JueyanGrants", pending);
        const int id = room->acquireSkillFromEffect(source, "tenyearjizhi", ctx, [&](int committedId) {
            // Expiry observes the committed ID even if an acquire observer unwinds.
            QVariantList committed = source->getTag("JueyanGrants").toList();
            for (int i = 0; i < committed.size(); ++i) {
                if (committed.at(i).toMap().value("serial").toInt() != serial) continue;
                receipt["id"] = committedId;
                committed[i] = receipt;
                source->setTag("JueyanGrants", committed);
                break;
            }
        });
        // Expiry can run inside EventAcquireSkill; a late returned grant must not escape that boundary.
        pending = source->getTag("JueyanGrants").toList(); int index = -1;
        for (int i = 0; i < pending.size(); ++i) if (pending.at(i).toMap().value("serial").toInt() == serial) { index = i; break; }
        if (id > 0 && index >= 0) { receipt["id"] = id; pending[index] = receipt; source->setTag("JueyanGrants", pending); }
        else if (id > 0) room->detachSkillFromPlayer(source, SkillInstanceUtils::formatName("tenyearjizhi", id), false, true);
        else if (index >= 0) { pending.removeAt(index); source->setTag("JueyanGrants", pending); }

    }
    return FinishSkill;
}

ViewAsSkillV2::EffectFlow Huairou::effectOnTarget(SkillContext &ctx, ServerPlayer *target) const
{
    if (target && target->isAlive()) target->drawCards(getEffectiveAmount(ctx), "recast");
    return FinishSkill;
}
ViewAsSkillV2::EffectFlow HongjuVS::effectOnTarget(SkillContext &ctx, ServerPlayer *target) const
{
    if (target == ctx.invoker && getEffectiveAmount(ctx) > 0) exchangeRong(ctx, objectName());
    return FinishSkill;
}

ViewAsSkillV2::EffectFlow Qingce::effectOnTarget(SkillContext &ctx, ServerPlayer *target) const
{
    if (!ctx.invoker || !target || !target->isAlive()) return FinishSkill;
    Room *room = ctx.invoker->getRoom();
    for (int n = 0; n < getEffectiveAmount(ctx) && ctx.invoker->isAlive() && target->isAlive()
         && ctx.invoker->canDiscard(target, "ej"); ++n) {
        const int id = room->askForCardChosen(ctx.invoker, target, "ej", objectName(), false, Card::MethodDiscard);
        room->throwCard(id, room->getCardPlace(id) == Player::PlaceDelayedTrick ? nullptr : target, ctx.invoker);
    }
    return FinishSkill;
}

ViewAsSkillV2::EffectFlow Xiongluan::effectOnTarget(SkillContext &ctx, ServerPlayer *target) const
{
    if (!ctx.invoker || !target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return FinishSkill;
    Room *room = ctx.invoker->getRoom();
    room->doSuperLightbox(ctx.invoker, "xiongluan");
    // This applied source/recipient relation survives removal of the granting skill.
    QStringList targets = ctx.invoker->property("XiongluanTargets").toStringList();
    if (!targets.contains(target->objectName())) targets << target->objectName();
    room->setPlayerProperty(ctx.invoker, "XiongluanTargets", targets);
    room->setPlayerCardLimitation(target, "use,response", ".|.|.|hand", true, objectName());
    room->addPlayerMark(ctx.invoker, "xiongluan_from-Clear");
    room->addPlayerMark(target, "xiongluan_to-Clear");
    return FinishSkill;
}

ViewAsSkillV2::EffectFlow OLHongjuVS::effectOnTarget(SkillContext &ctx, ServerPlayer *target) const
{
    if (target == ctx.invoker && getEffectiveAmount(ctx) > 0) exchangeRong(ctx, objectName());
    return FinishSkill;
}

ViewAsSkillV2::EffectFlow OLQingce::effectOnTarget(SkillContext &ctx, ServerPlayer *target) const
{
    if (!ctx.invoker || !target || !target->isAlive()) return FinishSkill;
    Room *room = ctx.invoker->getRoom();
    for (int n = 0; n < getEffectiveAmount(ctx) && ctx.invoker->isAlive() && target->isAlive()
         && ctx.invoker->canDiscard(target, "ej"); ++n) {
        const int id = room->askForCardChosen(ctx.invoker, target, "ej", objectName(), false, Card::MethodDiscard);
        room->throwCard(id, room->getCardPlace(id) == Player::PlaceDelayedTrick ? nullptr : target, ctx.invoker);
    }
    return FinishSkill;
}

ViewAsSkillV2::EffectFlow MobileHongjuVS::effectOnTarget(SkillContext &ctx, ServerPlayer *target) const
{
    if (target == ctx.invoker && getEffectiveAmount(ctx) > 0) exchangeRong(ctx, objectName());
    return FinishSkill;
}

ViewAsSkillV2::EffectFlow Poxi::effectOnTarget(SkillContext &ctx, ServerPlayer *target) const
{
    ServerPlayer *actor = ctx.invoker;
    if (!actor || !actor->isAlive() || !target || !target->isAlive()) return FinishSkill;
    Room *room = actor->getRoom();
    const int amount = getEffectiveAmount(ctx);
    if (ctx.choice == "reward") {
        if (amount <= 0) return FinishSkill;
        switch (ctx.extra_data.toInt()) {
        case 0: room->loseMaxHp(target, amount, objectName()); break;
        case 1: room->addMaxCards(target, -amount); target->endPlayPhase(); break;
        case 3: room->recover(target, RecoverStruct(objectName(), actor, amount)); break;
        case 4: target->drawCards(4 * amount, objectName()); break;
        default: break;
        }
        return FinishSkill;
    }
    Room::AcceptedViewAsEffectScope prompt(room, actor, objectName(), ctx);
    if (!prompt.isValid()) return FinishSkill;
    const QList<int> hand = target->handCards();
    if (hand.isEmpty()) return FinishSkill;
    const QVariant previous = actor->getTag("poxiForAI");
    const auto restore = qScopeGuard([&] {
        room->notifyMoveToPile(actor, hand, "poxi", Player::PlaceUnknown, false);
        if (previous.isValid()) room->notifyMoveToPile(actor, ListV2I(previous.toList()), "poxi", Player::PlaceUnknown, true);
    });
    LogMessage log; log.type = "$ViewAllCards"; log.from = actor; log.to << target;
    log.card_str = ListI2S(hand).join("+"); room->sendLog(log, actor);
    room->notifyMoveToPile(actor, hand, "poxi", Player::PlaceHand, true);
    const Card *chosen = room->askForUseCard(actor, "@@poxi", "@poxi:" + target->objectName());
    if (!chosen || !actor->isAlive() || !target->isAlive()) return FinishSkill;
    const QList<int> ids = chosen->getSubcards();
    QList<int> fromIds, toIds; QSet<Card::Suit> suits;
    for (int id : ids) {
        const Card *card = Sanguosha->getCard(id);
        if (!card || suits.contains(card->getSuit()) || room->getCardPlace(id) != Player::PlaceHand) return FinishSkill;
        suits.insert(card->getSuit());
        if (actor->handCards().contains(id) && !actor->isJilei(card)) fromIds << id;
        else if (hand.contains(id) && target->handCards().contains(id) && actor->canDiscard(target, id)) toIds << id;
        else return FinishSkill;
    }
    if (ids.size() != 4 || suits.size() != 4) return FinishSkill;
    QList<CardsMoveStruct> moves;
    if (!fromIds.isEmpty()) moves << CardsMoveStruct(fromIds, actor, nullptr, Player::PlaceHand, Player::DiscardPile,
        CardMoveReason(CardMoveReason::S_REASON_THROW, actor->objectName(), objectName(), ""));
    if (!toIds.isEmpty()) moves << CardsMoveStruct(toIds, target, nullptr, Player::PlaceHand, Player::DiscardPile,
        CardMoveReason(CardMoveReason::S_REASON_DISMANTLE, actor->objectName(), target->objectName(), objectName(), ""));
    room->moveCardsAtomic(moves, true);
    // The self reward is a separate actual recipient, with its own target interception.
    ctx.choice = "reward"; ctx.extra_data = fromIds.size();
    skillEffect(ctx, actor);
    return FinishSkill;
}

class Jieyingg : public TriggerSkillV2
{
public:
    Jieyingg() : TriggerSkillV2("jieyingg") { events << EventPhaseStart; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        bool camp = false; for (ServerPlayer *p : room->getAlivePlayers()) if (p->getMark("&jygying") > 0) camp = true;
        if ((player->getPhase() == Player::RoundStart && !camp)
            || (player->getPhase() == Player::Finish && player->getMark("&jygying") > 0)) addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ctx.choice = ctx.owner->getPhase() == Player::RoundStart ? "gain" : "give";
        ServerPlayer *target = ctx.choice == "gain" ? ctx.owner
            : room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "@jieyingg-mark", true, true);
        if (!target) return false;
        ctx.targets << target; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.invoker || getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "gain") {
            for (ServerPlayer *p : room->getAlivePlayers()) if (p->getMark("&jygying") > 0) return false;
        } else {
            if (ctx.invoker->getMark("&jygying") <= 0) return false;
            ctx.invoker->loseAllMarks("&jygying");
        }
        target->gainMark("&jygying", getEffectiveAmount(ctx)); return false;
    }
};

class jieyinggEffect : public TriggerSkillV2
{
public:
    jieyinggEffect() : TriggerSkillV2("#jieyingg-effect") { events << DrawNCards << EventPhaseChanging; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getMark("&jygying") <= 0) return result;
        if (event == DrawNCards && data.value<DrawStruct>().reason != "draw_phase") return result;
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive) return result;
        // Camp is shared public state; each live holder contributes its own exact source.
        for (ServerPlayer *holder : room->getAlivePlayers())
            if (event == DrawNCards || holder != player) addAllInstances(result, holder, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.invoker) return false;
        ctx.extra_data = ctx.invoker->objectName(); // Preserve the camp holder; callbacks receive the skill owner.
        ctx.invoker = ctx.owner;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!player || player->getMark("&jygying") <= 0) return false;
        ctx.targets = QList<ServerPlayer *>() << (event == DrawNCards ? player : ctx.invoker);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive() || !player || player->getMark("&jygying") <= 0 || getEffectiveAmount(ctx) <= 0) return false;
        if (event == DrawNCards) {
            DrawStruct draw = ctx.original_data->value<DrawStruct>(); draw.num += getEffectiveAmount(ctx);
            *ctx.original_data = QVariant::fromValue(draw);
        } else {
            // Consume before movement callbacks so another holder cannot collect the same camp.
            player->loseAllMarks("&jygying"); target->gainMark("&jygying");
            if (!player->isKongcheng()) {
                DummyCard hand(player->handCards());
                room->obtainCard(target, &hand, CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, target->objectName(), "jieyingg", ""), false);
            }
        }
        return false;
    }
};

class JieyinggKeep : public MaxCardsSkillV2
{
public:
    JieyinggKeep() : MaxCardsSkillV2("#jieyingg-keep") { setHolderSelector(CorrectSkill_AllHolders); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.primary && ctx.primary->getMark("&jygying") > 0
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};
class JieyinggTargetMod : public TargetModSkillV2
{
public:
    JieyinggTargetMod() : TargetModSkillV2("#jieyingg-target", "Slash") { setHolderSelector(CorrectSkill_AllHolders); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == TargetModSkill::Residue && ctx.primary && ctx.primary->getMark("&jygying") > 0
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};
class OLDuorui : public TriggerSkillV2
{
public:
    OLDuorui() : TriggerSkillV2("olduorui") { events << Damage; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result; ServerPlayer *target = data.value<DamageStruct>().to;
        if (target) for (const QVariant &v : target->getTag("DuoruiReceipts").toList()) if (v.toMap().value("ol").toBool()) return result;
        if (player && player->isAlive() && player->getPhase() == Player::Play && target && target->isAlive()
            && target != player && !duoruiChoices(target, true).isEmpty())
            addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
        if (!target || !target->isAlive()) return false;
        for (const QVariant &v : target->getTag("DuoruiReceipts").toList()) if (v.toMap().value("ol").toBool()) return false;
        const QStringList skills = duoruiChoices(target, true);
        if (skills.isEmpty() || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target))) return false;
        const QString selected = room->askForChoice(ctx.owner, objectName(), skills.join("+"));
        if (!skills.contains(selected)) return false;
        QString skill; const int instance = SkillInstanceUtils::parseName(selected, skill);
        ctx.extra_data = QVariantMap{{"skill", skill}, {"instance", instance}}; ctx.targets << target; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { if (getEffectiveAmount(ctx) > 0) applyDuorui(room, ctx, target, true); return false; }
};
class OLDuoruiClear : public TriggerSkillV2
{
public:
    OLDuoruiClear() : TriggerSkillV2("#olduorui-clear") { global = true; events << EventPhaseChanging << Death; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == Death) player = data.value<DeathStruct>().who;
        if (player && (event == Death || data.value<PhaseChangeStruct>().to == Player::NotActive)) clearDuorui(room, player);
        return true;
    }
};

class OLZhiti : public MaxCardsSkillV2
{
public:
    OLZhiti() : MaxCardsSkillV2("olzhiti") { setHolderSelector(CorrectSkill_AllHolders); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.holder || !ctx.primary) return CorrectSkillResult::noEffect();
        int result = ctx.primary->isWounded() && ctx.holder->inMyAttackRange(ctx.primary) ? -ctx.currentAmount : 0;
        if (ctx.holder == ctx.primary) {
            for (const Player *other : ctx.primary->getAliveSiblings())
                if (other->isWounded()) { result += ctx.currentAmount; return CorrectSkillResult::useAmount(result); }
            if (ctx.primary->isWounded()) result += ctx.currentAmount;
        }
        return result ? CorrectSkillResult::useAmount(result) : CorrectSkillResult::noEffect();
    }
};
class OLZhitiEffect : public TriggerSkillV2
{
public:
    OLZhitiEffect() : TriggerSkillV2("#olzhiti-effect") { events << DrawNCards << EventPhaseChanging; frequency = Compulsory; }
    int wounded(Room *room) const
    { int count = 0; for (ServerPlayer *p : room->getAlivePlayers()) if (p->isWounded()) ++count; return count; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (player && player->isAlive() && ((event == DrawNCards && data.value<DrawStruct>().reason == "draw_phase" && wounded(room) >= 3)
            || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive && wounded(room) >= 5)))
            addAllInstances(result, player, objectName());
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ServerPlayer *target = ctx.owner;
        if (event == EventPhaseChanging) {
            QList<ServerPlayer *> candidates; for (ServerPlayer *p : room->getAlivePlayers()) if (p->hasEquipArea()) candidates << p;
            if (candidates.isEmpty()) return false;
            target = room->askForPlayerChosen(ctx.owner, candidates, "olzhiti", "@olzhiti-throw", true, true);
        }
        if (!target) return false;
        ctx.targets << target; return true;
    }
    bool effectTarget(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        if (event == DrawNCards) {
            DrawStruct draw = ctx.original_data->value<DrawStruct>(); draw.num += getEffectiveAmount(ctx);
            *ctx.original_data = QVariant::fromValue(draw);
        } else for (int n = 0; n < getEffectiveAmount(ctx) && target->isAlive(); ++n) {
            QList<int> areas; for (int i = 0; i < S_EQUIP_AREA_LENGTH; ++i) if (target->hasEquipArea(i)) areas << i;
            if (areas.isEmpty()) break;
            target->throwEquipArea(areas.at(qsanRandomBounded(areas.size())));
        }
        return false;
    }
};

LeiPackage::LeiPackage()
    : Package("Lei")
{
    General *chendao = new General(this, "chendao", "shu", 4);
    chendao->addSkill(new Wanglie);
    chendao->addSkill(new WanglieMod);
    related_skills.insert("wanglie", "#wangliemod");

    General *zhugezhan = new General(this, "zhugezhan", "shu", 3);
    zhugezhan->addSkill(new Zuilun);
    zhugezhan->addSkill(new Fuyin);

    General *zhoufei = new General(this, "zhoufei", "wu", 3, false);
    zhoufei->addSkill(new Liangyin);
    zhoufei->addSkill(new Kongsheng);

    General *lei_lukang = new General(this, "lei_lukang", "wu", 4);
    lei_lukang->addSkill(new Qianjie);
    lei_lukang->addSkill(new QianjieChain);
    lei_lukang->addSkill(new QianjiePindianPro);
    lei_lukang->addSkill(new Jueyan);
    lei_lukang->addSkill(new JueyanTargetMod);
    lei_lukang->addSkill(new JueyanClear);
    lei_lukang->addSkill(new Poshi);
    related_skills.insert("qianjie", "#qianjie-chain");
    related_skills.insert("qianjie", "#qianjiepindianpro");
    related_skills.insert("jueyan", "#jueyantargetmod");
    related_skills.insert("jueyan", "#jueyan-clear");

    General *haozhao = new General(this, "haozhao", "wei", 4);
    haozhao->addSkill(new Zhengu);
    haozhao->addSkill(new ZhenguEffect);
    related_skills.insert("zhengu", "#zhengueffect");

    General *guanqiujian = new General(this, "guanqiujian", "wei", 4);
    guanqiujian->addSkill(new Zhengrong);
    guanqiujian->addSkill(new Hongju);

    General *lei_yuanshu = new General(this, "lei_yuanshu$", "qun", 4);
    lei_yuanshu->addSkill(new Leiyongsi);
    lei_yuanshu->addSkill(new LeiyongsiMaxCards);
    lei_yuanshu->addSkill(new Leiweidi);
    related_skills.insert("leiyongsi", "#leiyongsimaxmards");

    General *zhangxiu = new General(this, "zhangxiu", "qun", 4);
    zhangxiu->addSkill(new Congjian);
    zhangxiu->addSkill(new Xiongluan);
    zhangxiu->addSkill(new XiongluanTargetMod);
    zhangxiu->addSkill(new XiongluanClear);
    related_skills.insert("xiongluan", "#xiongluan-target");
    related_skills.insert("xiongluan", "#xiongluan-clear");

    General *shenzhangliao = new General(this, "shenzhangliao", "god");
    shenzhangliao->addSkill(new Duorui);
    shenzhangliao->addSkill(new DuoruiClear);
    shenzhangliao->addSkill(new DuoruiInvalidity);
    shenzhangliao->addSkill(new Zhiti);
    shenzhangliao->addSkill(new ZhitiEffect);
    related_skills.insert("duorui", "#duorui-clear");
    related_skills.insert("duorui", "#duorui-inv");
    related_skills.insert("zhiti", "#zhiti-effect");

    General *shenganning = new General(this, "shenganning", "god", 6, true, false, false, 3);
    shenganning->addSkill(new Poxi);
    shenganning->addSkill(new Jieyingg);
    shenganning->addSkill(new jieyinggEffect);
    shenganning->addSkill(new JieyinggKeep);
    shenganning->addSkill(new JieyinggTargetMod);
    related_skills.insert("jieyingg", "#jieyingg-effect");
    related_skills.insert("jieyingg", "#jieyingg-keep");
    related_skills.insert("jieyingg", "#jieyingg-target");
    addMetaObject<PoxiCard>();
    addMetaObject<PoxiDisCard>();


    addMetaObject<JueyanCard>();
    addMetaObject<HuairouCard>();
    addMetaObject<HongjuCard>();
    addMetaObject<QingceCard>();
    addMetaObject<XiongluanCard>();
    addMetaObject<OLQingceCard>();

    skills << new Huairou << new Qingce << new OLQingce;
}
ADD_PACKAGE(Lei)

OLStLeiPackage::OLStLeiPackage()
    : Package("OLStLei")
{
    General *ol_guanqiujian = new General(this, "ol_guanqiujian", "wei", 4);
    ol_guanqiujian->addSkill(new OLZhengrong);
    ol_guanqiujian->addSkill(new OLHongju);

    General *ol_shenzhangliao = new General(this, "ol_shenzhangliao", "god");
    ol_shenzhangliao->addSkill(new OLDuorui);
    ol_shenzhangliao->addSkill(new OLDuoruiClear);
    ol_shenzhangliao->addSkill(new OLZhiti);
    ol_shenzhangliao->addSkill(new OLZhitiEffect);
    related_skills.insert("olduorui", "#olduorui-clear");
    related_skills.insert("olzhiti", "#olzhiti-effect");

    addMetaObject<OLHongjuCard>();
}
ADD_PACKAGE(OLStLei)

MobileStLeiPackage::MobileStLeiPackage()
    : Package("MobileStLei")
{
    General *mobile_guanqiujian = new General(this, "mobile_guanqiujian", "wei", 4);
    mobile_guanqiujian->addSkill(new MobileZhengrong);
    mobile_guanqiujian->addSkill(new MobileHongju);

    addMetaObject<MobileHongjuCard>();
}
ADD_PACKAGE(MobileStLei)
