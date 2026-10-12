#include "thicket.h"
#include "skill-instance-utils.h"
//#include "skill.h"
#include "room.h"
#include "maneuvering.h"
#include "clientplayer.h"
//#include "client.h"
#include "engine.h"
//#include "general.h"
//#include "json.h"
#include "roomthread.h"
#include "settings.h"
#include <QScopeGuard>

class Xingshang : public TriggerSkillV2
{
public:
    Xingshang() : TriggerSkillV2("xingshang") { events << Death; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const ServerPlayer *dead = data.value<DeathStruct>().who;
        return player && player->isAlive() && player->hasSkill(objectName()) && dead && dead != player && !dead->isNude()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const ServerPlayer *dead = ctx.original_data->value<DeathStruct>().who;
        if (!dead || dead->isNude() || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *dead = ctx.original_data->value<DeathStruct>().who;
        if (!dead || dead->isNude()) return false;
        const bool isCaoCao = dead->getGeneralName().contains("caocao");
        room->broadcastSkillInvoke(objectName(), isCaoCao ? 3 : (dead->isMale() ? 1 : 2));
        // The living card recipient is interceptable; the deceased is only the material source.
        DummyCard cards(dead->handCards());
        for (const Card *card : dead->getEquips()) cards.addSubcard(card);
        if (cards.subcardsLength() > 0) {
            CardMoveReason reason(CardMoveReason::S_REASON_RECYCLE, ctx.owner->objectName());
            room->obtainCard(target, &cards, reason, false);
        }
        return false;
    }
};

class Fangzhu : public TriggerSkillV2
{
public:
    Fangzhu() : TriggerSkillV2("fangzhu")
    {
        events << Damaged;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // The direct parent identifies Jilve even when Jilve itself has a borrowed root.
        const SkillInstance *activation = ctx.owner->findSkillInstance(
            ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
        const bool borrowed = activation && (activation->parentRef.key.skillName == "jilve"
            || activation->grantActivationRef.key.skillName == "jilve");
        ctx.choice = borrowed ? "jilve" : "fangzhu";
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner),
            objectName(), "fangzhu-invoke", !borrowed, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "jilve") {
            room->broadcastSkillInvoke("jilve", 2);
        } else {
            int index = target->faceUp() ? 1 : 2;
            if (target->getGeneralName().contains("caozhi") || target->getGeneral2Name().contains("caozhi")) index = 3;
            room->broadcastSkillInvoke(objectName(), index);
        }
        target->drawCards(ctx.invoker->getLostHp() * getEffectiveAmount(ctx), objectName());
        target->turnOver();
        return false;
    }
};

class Songwei : public TriggerSkillV2
{
public:
    Songwei() : TriggerSkillV2("songwei$") { events << FinishJudge; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const JudgeStruct *judge = data.value<JudgeStruct *>();
        if (!player || !player->isAlive() || player->getKingdom() != "wei" || !judge || !judge->card || !judge->card->isBlack()) return result;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->hasLordSkill(objectName())) result[owner] << objectName();
        return result;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room->askForPlayerChosen(ctx.invoker, {ctx.owner}, objectName(), "@songwei-to", true)) return false;
        ctx.targets = {ctx.owner}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner->isLord() && ctx.owner->hasSkill("weidi")) room->broadcastSkillInvoke("weidi");
        else room->broadcastSkillInvoke(objectName(), ctx.invoker->isMale() ? 1 : 2);
        LogMessage log; log.type = "#InvokeOthersSkill"; log.from = ctx.invoker; log.to << target; log.arg = objectName(); room->sendLog(log);
        room->notifySkillInvoked(ctx.owner, objectName());
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};
class Duanliang : public ViewAsSkillV2
{
public:
    Duanliang(const QString &name = "duanliang") : ViewAsSkillV2(name, 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty()
            && !card->hasFlag("using") && card->isBlack()
            && (card->isKindOf("BasicCard") || card->isKindOf("EquipCard"))
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId())
                || request.initiator->getHandPile().contains(card->getEffectiveId()));
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        // Materials enter the ordinary delayed-trick pipeline, never a custom payment.
        SupplyShortage *shortage = new SupplyShortage(originalCard->getSuit(), originalCard->getNumber());
        shortage->setSkillName(objectName());
        shortage->addSubcard(originalCard);

        return shortage;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "SupplyShortage"; }
};

class DuanliangTargetMod : public TargetModSkillV2
{
public:
    DuanliangTargetMod() : TargetModSkillV2("#duanliang-target", "SupplyShortage")
    {
        frequency = NotFrequent;
        pattern = "SupplyShortage";
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == DistanceLimit ? CorrectSkillResult::useAmount(ctx.currentAmount)
                                           : CorrectSkillResult::noEffect();
    }
};

class Huoshou : public TriggerSkillV2
{
public:
    Huoshou(const QString &name = "huoshou") : TriggerSkillV2(name)
    {
        frequency = Compulsory; global = true;
        if (name == "huoshou") events << TargetSpecified << CardEffected;
        else events << ConfirmDamage << CardFinished;
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardFinished) return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.card && player == use.from) use.card->removeTag("HuoshouReceipts");
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == CardEffected) {
            const CardEffectStruct effect = data.value<CardEffectStruct>();
            if (player && effect.to == player && player->isAlive() && player->hasSkill(objectName())
                && effect.card && effect.card->isKindOf("SavageAssault")) result[player] << objectName();
        } else if (event == TargetSpecified) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!use.from || player != use.from || !use.card || !use.card->isKindOf("SavageAssault")) return result;
            for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
                if (owner != use.from) result[owner] << objectName();
        }
        return result;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != ConfirmDamage) return false;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.to || !damage.card->isKindOf("SavageAssault")) return true;
        const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (useEvent <= 0) return true;
        for (const QVariant &value : damage.card->getTag("HuoshouReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("use_event").toLongLong() != useEvent) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner || !owner->isAlive()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = ctx.initiator = ctx.invoker = owner;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            ctx.instanceID = receipt.value("dispatch").toInt(); ctx.amount = 1;
            ctx.extra_data = value; ctx.targets = {damage.to}; ctx.original_data = &data; ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const Card *card = ctx.original_data ? ctx.original_data->value<DamageStruct>().card : nullptr;
        return ctx.owner && ctx.owner->isAlive() && card && ctx.extra_data.toMap().value("applied_huoshou").toBool()
            && card->getTag("HuoshouReceipts").toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == ConfirmDamage) return true;
        if (event == TargetSpecified
            && room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong() <= 0) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == ConfirmDamage) {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            if (target == damage.to) {
                damage.from = ctx.owner;
                *ctx.original_data = QVariant::fromValue(damage);
            }
            return false;
        }
        int voice = qsanRandomBounded(2) + 1;
        if (ctx.owner->isJieGeneral()) voice += 2;
        room->sendCompulsoryTriggerLog(ctx.owner, this, voice);
        if (event == CardEffected) {
            CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
            if (target == effect.to) {
                effect.nullified = true;
                *ctx.original_data = QVariant::fromValue(effect);
            }
            return false;
        }
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card) return false;
        const qint64 useEvent = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (useEvent <= 0) return false;
        QVariantList receipts = use.card->getTag("HuoshouReceipts").toList();
        for (int i = receipts.size() - 1; i >= 0; --i)
            if (receipts.at(i).toMap().value("use_event").toLongLong() != useEvent) receipts.removeAt(i);
        const int serial = use.card->getTag("HuoshouNextReceipt").toInt() + 1;
        use.card->setTag("HuoshouNextReceipt", serial);
        // Damage attribution is already applied to this card use and survives removal of its granting skill.
        receipts << QVariantMap{{"applied_huoshou", true}, {"owner", ctx.owner->objectName()},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"dispatch", serial}, {"use_event", QString::number(useEvent)}};
        use.card->setTag("HuoshouReceipts", receipts);
        return false;
    }
};
class Lieren : public TriggerSkillV2
{
public:
    Lieren() : TriggerSkillV2("lieren") { events << Damage; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(objectName())
            && damage.to && damage.to->isAlive() && damage.card && damage.card->isKindOf("Slash")
            && player->canPindian(damage.to) && !damage.to->hasFlag("Global_DebutFlag")
            && !damage.chain && !damage.transfer
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
        if (!target || !ctx.owner->canPindian(target)
            || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        // Keep the selected victim in this invocation, including target interception.
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!player || !target || !target->isAlive() || !player->canPindian(target)) return false;
        int index = qsanRandomBounded(2) + 1;
        if (player->isJieGeneral()) index += 2;
        room->broadcastSkillInvoke(objectName(), index);
        if (!player->pindian(target, objectName()) || !player->isAlive() || target->isNude()) return false;
        for (int i = 0; i < getEffectiveAmount(ctx) && player->isAlive() && target->isAlive() && !target->isNude(); ++i) {
            const int id = room->askForCardChosen(player, target, "he", objectName());
            const Player::Place place = room->getCardPlace(id);
            if (id < 0 || room->getCardOwner(id) != target || Sanguosha->getCard(id)->hasFlag("using")
                || (place != Player::PlaceHand && place != Player::PlaceEquip)) break;
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
            room->obtainCard(player, Sanguosha->getCard(id), reason, place != Player::PlaceHand);
        }
        return false;
    }
};

class Zaiqi : public TriggerSkillV2
{
public:
    Zaiqi() : TriggerSkillV2("zaiqi") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
                && player->getPhase() == Player::Draw && player->isWounded()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room->askForSkillInvoke(ctx.owner, objectName())) return false;
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        room->broadcastSkillInvoke(objectName());
        const int count = qMax(0, player->getLostHp() * getEffectiveAmount(ctx));
        const QList<int> ids = room->getNCards(count, false);
        CardsMoveStruct move(ids, nullptr, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, player->objectName(), objectName(), ""));
        room->moveCardsAtomic(move, true);
        room->getThread()->delay();
        room->getThread()->delay();
        QList<int> hearts, others;
        // Nested movement may take a revealed card; only settle cards still on the table.
        for (int id : ids) {
            if (room->getCardPlace(id) != Player::PlaceTable) continue;
            if (Sanguosha->getCard(id)->getSuit() == Card::Heart) hearts << id;
            else others << id;
        }
        if (!hearts.isEmpty()) {
            room->recover(player, RecoverStruct(player, nullptr, qMin(int(hearts.size()), player->getLostHp()), objectName()));
            QList<int> remaining;
            for (int id : hearts)
                if (room->getCardPlace(id) == Player::PlaceTable) remaining << id;
            if (!remaining.isEmpty()) {
                DummyCard discarded(remaining);
                room->throwCard(&discarded,
                    CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), ""), nullptr);
            }
        }
        QList<int> remaining;
        for (int id : others)
            if (room->getCardPlace(id) == Player::PlaceTable) remaining << id;
        if (!remaining.isEmpty()) {
            DummyCard obtained(remaining);
            room->obtainCard(player, &obtained);
        }
        return true;
    }
};

class Juxiang : public TriggerSkillV2
{
public:
    Juxiang() : TriggerSkillV2("juxiang")
    {
        events << BeforeCardsMove << CardEffected;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == CardEffected) {
            const CardEffectStruct effect = data.value<CardEffectStruct>();
            return effect.to == player && effect.card && effect.card->isKindOf("SavageAssault")
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        }
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        const CardUseStruct use = move.reason.m_useStruct;
        return move.from_places.contains(Player::PlaceTable) && move.to_place == Player::DiscardPile
                && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_USE
                && use.card && use.card->isKindOf("SavageAssault") && player != use.from && room->CardInTable(use.card)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == CardEffected) {
            CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
            room->sendCompulsoryTriggerLog(target, this);
            effect.nullified = true;
            *ctx.original_data = QVariant::fromValue(effect);
            return false;
        }
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const Card *card = move.reason.m_useStruct.card;
        if (!card || !room->CardInTable(card)) return false;
        int index = qsanRandomBounded(2) + 1;
        if (target->isJieGeneral()) index += 2;
        room->sendCompulsoryTriggerLog(target, this, index);
        const QList<int> materials = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
        target->obtainCard(card);
        // A combined move can contain unrelated cards; cancel only this assault's material IDs.
        QList<int> intercepted;
        for (int id : materials)
            if (move.card_ids.contains(id) && room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand) intercepted << id;
        move.removeCardIds(intercepted);
        *ctx.original_data = QVariant::fromValue(move);
        return false;
    }
};

class Yinghun : public TriggerSkillV2
{
public:
    Yinghun() : TriggerSkillV2("yinghun")
    {
        events << EventPhaseStart;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Start && player->isWounded()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner),
            objectName(), "yinghun-invoke", true, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }

    void broadcast(ServerPlayer *sunjian, int index) const
    {
        Room *room = sunjian->getRoom();
        if (!sunjian->isJieGeneral())
            room->broadcastSkillInvoke(objectName(), index);
        else {
            if(sunjian->hasSkill("mobilehunzi",true))
                room->broadcastSkillInvoke(objectName(), index + 11);
			else if (sunjian->isJieGeneral("sunce"))
                room->broadcastSkillInvoke(objectName(), index + 6);
            else
                room->broadcastSkillInvoke(objectName(), index + 4);
        }
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *to) const override
    {
        ServerPlayer *sunjian = ctx.owner;
        if (to && to->isAlive()) {
            const int amount = getEffectiveAmount(ctx);
            int x = sunjian->getLostHp() * amount;

            int index = qsanRandomBounded(2)+1;
            if (!sunjian->hasInnateSkill("yinghun")) {
                if (sunjian->hasSkill("xiongyisy",true))
                    index = 9;
                else if (sunjian->hasSkill("hunzi",true))
                    index += 2;
            }

            if (x == 1) {
                broadcast(sunjian, index);

                to->drawCards(amount, objectName());
                if (to->isAlive()) room->askForDiscard(to, objectName(), amount, amount, false, true);
            } else {
                const bool previous = to->hasFlag("YinghunTarget");
                auto restore = qScopeGuard([&] { if (!previous) to->setFlags("-YinghunTarget"); });
                to->setFlags("YinghunTarget");
                QString choice = room->askForChoice(sunjian, objectName(), "d1tx+dxt1");
                if (!previous) to->setFlags("-YinghunTarget");
                restore.dismiss();
                if (choice == "d1tx") {
                    broadcast(sunjian, index + 1);

                    to->drawCards(amount, objectName());
                    if (to->isAlive()) room->askForDiscard(to, objectName(), x, x, false, true);
                } else {
                    broadcast(sunjian, index);

                    to->drawCards(x, objectName());
                    if (to->isAlive()) room->askForDiscard(to, objectName(), amount, amount, false, true);
                }
            }
        }
        return false;
    }
};

HaoshiCard::HaoshiCard()
{
    will_throw = false;
    mute = true;
    handling_method = Card::MethodNone;
    m_skillName = "_haoshi";
}

bool HaoshiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty() || to_select == Self)
        return false;

    return to_select->getHandcardNum() == Self->getMark("haoshi");
}

void HaoshiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(),
        targets.first()->objectName(), "haoshi", "");
    room->moveCardTo(this, targets.first(), Player::PlaceHand, reason);
}

class HaoshiViewAsSkill : public ViewAsSkillV2
{
public:
    HaoshiViewAsSkill() : ViewAsSkillV2("haoshi") { response_pattern = "@@haoshi!"; }
    TargetMode targetMode() const override { return SelectTargets; }
    QString historyKey(const ActiveSkillRequest &) const override { return "HaoshiCard"; }
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.pattern == "@@haoshi!" && request.reason != CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && !candidate->hasFlag("using")
            && request.initiator->handCards().contains(candidate->getEffectiveId())
            && !request.selectedCardIds.contains(candidate->getEffectiveId())
            && request.selectedCardIds.size() < request.initiator->getHandcardNum() / 2;
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.size() != request.initiator->getHandcardNum() / 2) return false;
        ActiveSkillRequest checked = request; checked.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(checked, Sanguosha->getCard(id))) return false;
            checked.selectedCardIds << id;
        }
        return true;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    {
        if (!request.initiator || !selected.isEmpty() || !candidate || !candidate->isAlive() || candidate == request.initiator) return false;
        for (const Player *other : request.initiator->getAliveSiblings())
            if (other->getHandcardNum() < candidate->getHandcardNum()) return false;
        return true;
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    { return selected.size() == 1 && canSelectTarget(request, {}, selected.first()); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        if (!ctx.use_card) return ContinueEffects;
        const QList<int> ids = ctx.use_card->getSubcards();
        // The selected hand cards are gifts in the accepted effect, never discard payment.
        for (int id : ids)
            if (room->getCardOwner(id) != ctx.initiator || room->getCardPlace(id) != Player::PlaceHand || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        if (!ids.isEmpty()) room->giveCard(ctx.initiator, target, ids, objectName());
        return ContinueEffects;
    }
};

class HaoshiGive : public TriggerSkillV2
{
public:
    HaoshiGive() : TriggerSkillV2("#haoshi-give") { events << AfterDrawNCards; global = true; frequency = Compulsory; }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        const DrawStruct draw = data.value<DrawStruct>();
        if (!player || !player->isAlive() || draw.who != player || draw.historyEventId <= 0) return true;
        for (const QVariant &entry : player->getTag("HaoshiReceipts").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("draw_id").toLongLong() != draw.historyEventId) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt();
            ctx.owner = ctx.initiator = ctx.invoker = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (ctx.instanceID <= 0 || !ctx.sourceRef.isValid()) continue;
            ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event; ctx.manual_effect = true;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return !ctx.activationRef.isValid() && ctx.owner && ctx.owner->isAlive() && ctx.owner->getTag("HaoshiReceipts").toList().contains(ctx.extra_data); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QVariantMap receipt = ctx.extra_data.toMap();
        QVariantList receipts = ctx.owner->getTag("HaoshiReceipts").toList();
        if (!receipts.removeOne(ctx.extra_data)) return false;
        ctx.owner->setTag("HaoshiReceipts", receipts);
        if (ctx.owner->getHandcardNum() <= 5 || room->getOtherPlayers(ctx.owner).isEmpty()) return false;
        // Restore the original activation only inside this accepted continuation's response scope.
        SkillContext accepted = ctx;
        accepted.activationRef = SkillInstanceRef(receipt.value("activation_owner").toString(), SkillInstanceKey(receipt.value("activation_skill").toString(), receipt.value("activation_id").toInt()));
        Room::AcceptedViewAsEffectScope response(room, ctx.owner, "haoshi", accepted);
        if (response.isValid() && room->askForUseCard(ctx.owner, "@@haoshi!", "@haoshi", -1, Card::MethodNone)) return false;
        if (!ctx.owner->isAlive()) return false;
        ServerPlayer *beggar = nullptr;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner))
            if (!beggar || other->getHandcardNum() < beggar->getHandcardNum()) beggar = other;
        if (!beggar) return false;
        ctx.targets = {beggar}; ctx.extra_data = QVariant::fromValue(ctx.owner->handCards().mid(0, ctx.owner->getHandcardNum() / 2));
        skillEffect(event, room, player, ctx, beggar);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QList<int> ids;
        for (int id : ctx.extra_data.value<QList<int>>())
            if (room->getCardOwner(id) == ctx.owner && room->getCardPlace(id) == Player::PlaceHand && !Sanguosha->getCard(id)->hasFlag("using")) ids << id;
        if (!ids.isEmpty()) room->giveCard(ctx.owner, target, ids, "haoshi");
        return false;
    }
};

class Haoshi : public TriggerSkillV2
{
public:
    Haoshi() : TriggerSkillV2("haoshi") { events << DrawNCards; setBaseAmount(2); view_as_skill = new HaoshiViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DrawStruct draw = data.value<DrawStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && draw.who == player
            && draw.reason == "draw_phase" && draw.historyEventId > 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return room->askForSkillInvoke(ctx.owner, objectName()); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        if (draw.who != target || draw.historyEventId <= 0) return false;
        room->broadcastSkillInvoke(objectName());
        const int serial = room->getTag("HaoshiNextReceipt").toInt() + 1; room->setTag("HaoshiNextReceipt", serial);
        QVariantList receipts = target->getTag("HaoshiReceipts").toList();
        receipts << QVariantMap{{"serial", serial}, {"draw_id", draw.historyEventId},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        target->setTag("HaoshiReceipts", receipts);
        draw.num += getEffectiveAmount(ctx); *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};
DimengCard::DimengCard()
{
    setSkillName("dimeng");
}

bool DimengCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (to_select == Self)
        return false;

    if (targets.isEmpty())
        return true;

    if (targets.length() == 1) {
        return qAbs(to_select->getHandcardNum() - targets.first()->getHandcardNum()) == subcardsLength();
    }

    return false;
}

bool DimengCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

void DimengCard::use(Room *room, ServerPlayer *, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *a = targets.at(0);
    ServerPlayer *b = targets.at(1);
    a->setFlags("DimengTarget");
    b->setFlags("DimengTarget");

    int n1 = a->getHandcardNum();
    int n2 = b->getHandcardNum();

    try {/*
        QList<CardsMoveStruct> exchangeMove;
        CardsMoveStruct move1(a->handCards(), b, Player::PlaceHand,
            CardMoveReason(CardMoveReason::S_REASON_SWAP, a->objectName(), b->objectName(), "dimeng", ""));
        CardsMoveStruct move2(b->handCards(), a, Player::PlaceHand,
            CardMoveReason(CardMoveReason::S_REASON_SWAP, b->objectName(), a->objectName(), "dimeng", ""));
        exchangeMove.push_back(move1);
        exchangeMove.push_back(move2);
        room->moveCardsAtomic(exchangeMove, false);*/
		room->swapCards(a,b,"h","dimeng");

        LogMessage log;
        log.type = "#Dimeng";
        log.from = a;
        log.to << b;
        log.arg = QString::number(n1);
        log.arg2 = QString::number(n2);
        room->sendLog(log);
        room->getThread()->delay();

        a->setFlags("-DimengTarget");
        b->setFlags("-DimengTarget");
    }
    catch (TriggerEvent triggerEvent) {
        if (triggerEvent == TurnBroken || triggerEvent == StageChange) {
            a->setFlags("-DimengTarget");
            b->setFlags("-DimengTarget");
        }
        throw triggerEvent;
    }
}

class Dimeng : public ViewAsSkillV2
{
public:
    Dimeng() : ViewAsSkillV2("dimeng") { setPhaseName("Play");}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    TargetMode targetMode() const override { return SelectTargets; }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        if (!request.initiator || !candidate || candidate->hasFlag("using")
            || request.selectedCardIds.contains(candidate->getEffectiveId())
            || request.initiator->isJilei(candidate)) return false;
        if (request.initiator->handCards().contains(candidate->getEffectiveId())) return true;
        for (const Card *equip : request.initiator->getEquips())
            if (equip->getEffectiveId() == candidate->getEffectiveId()) return true;
        return false;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (id < 0 || !canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        if (!candidate || !candidate->isAlive() || candidate == request.initiator || selected.contains(candidate)) return false;
        if (selected.isEmpty()) return true;
        return selected.size() == 1 && qAbs(candidate->getHandcardNum() - selected.first()->getHandcardNum()) == request.selectedCardIds.size();
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return selected.size() == 2 && canSelectTarget(request, {}, selected.first())
            && canSelectTarget(request, {selected.first()}, selected.last());
    }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        if (targets.size() != 2) return FinishSkill;
        ctx.extra_data = QVariant();
        ctx.choice = "first";
        skillEffect(ctx, targets.first());
        if (ctx.extra_data.toString().isEmpty()) return FinishSkill;
        ctx.choice = "second";
        skillEffect(ctx, targets.last());
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "first") { ctx.extra_data = target->objectName(); return ContinueEffects; }
        Room *room = ctx.invoker->getRoom();
        ServerPlayer *first = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!first || !first->isAlive() || first == target) return FinishSkill;
        // Swap once only after both concrete recipients have accepted their effect hooks.
        const bool firstFlag = first->hasFlag("DimengTarget"), secondFlag = target->hasFlag("DimengTarget");
        first->setFlags("DimengTarget"); target->setFlags("DimengTarget");
        const auto restore = qScopeGuard([&] {
            if (!firstFlag) first->setFlags("-DimengTarget");
            if (!secondFlag) target->setFlags("-DimengTarget");
        });
        const int firstCount = first->getHandcardNum(), secondCount = target->getHandcardNum();
        room->swapCards(first, target, "h", objectName());
        LogMessage log; log.type = "#Dimeng"; log.from = first; log.to << target;
        log.arg = QString::number(firstCount); log.arg2 = QString::number(secondCount); room->sendLog(log);
        return ContinueEffects;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "DimengCard"; }
};

class Wansha : public TriggerSkillV2
{
public:
    Wansha() : TriggerSkillV2("wansha")
    {
        events << Dying;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && player->hasFlag("CurrentPlayer")
            && (!Config.EnableHegemony || player->getPhase() != Player::NotActive)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (!player || !ctx.original_data) return false;
        // The V2 pipeline handles concealed invocation; retain Jilve's borrowed-skill audio.
        if (player->hasInnateSkill(objectName()) || !player->hasSkill("jilve"))
            room->broadcastSkillInvoke(objectName());
        else
            room->broadcastSkillInvoke("jilve", 3);

        const DyingStruct dying = ctx.original_data->value<DyingStruct>();
        LogMessage log;
        log.from = player;
        log.arg = objectName();
        log.type = "#WanshaOne";
        if (player != dying.who) {
            log.type = "#WanshaTwo";
            log.to << dying.who;
        }
        room->sendLog(log);
        room->notifySkillInvoked(player, objectName());
        return false;
    }
};

class WanshaLimit : public CardLimitSkill
{
public:
    WanshaLimit() : CardLimitSkill("#wansha-limit") {}

    QString limitList(const Player *) const override { return "use"; }

    QString limitPattern(const Player *target) const override
    {
        if (!target || target->hasFlag("Global_Dying")) return QString();
        for (const Player *player : target->getAliveSiblings()) {
            if (!player->hasFlag("CurrentPlayer")) continue;
            // Passive limits require both validity and revelation of an innate hegemony skill.
            const bool wanshaActive = player->hasSkill("wansha")
                && (!Config.EnableHegemony || (player->getPhase() != Player::NotActive
                    && (player->hasShownSkill("wansha") || player->hasAcquiredSkill("wansha"))));
            if (wanshaActive || player->hasSkill("mobilemouwansha")) return "Peach";
        }
        return QString();
    }
};

class Luanwu : public ViewAsSkillV2
{
public:
    Luanwu() : ViewAsSkillV2("luanwu") { frequency = Limited; limit_mark = "@chaos"; }
    TargetMode targetMode() const override { return NoTarget; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    QString historyKey(const ActiveSkillRequest &) const override { return "LuanwuCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.invoker->getRoom();
        room->removePlayerMark(ctx.invoker, limit_mark);
        room->doSuperLightbox(ctx.invoker, objectName());
        // Accepted chaos continues around the table even if the original source dies.
        const QList<ServerPlayer *> targets = room->getOtherPlayers(ctx.invoker);
        for (ServerPlayer *target : targets) {
            SkillContext part = ctx; part.targets = {target};
            skillEffect(part, target);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        const QList<ServerPlayer *> others = room->getOtherPlayers(target);
        int nearest = 1000;
        for (ServerPlayer *other : others) nearest = qMin(nearest, target->distanceTo(other));
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : others)
            if (target->distanceTo(other) == nearest && target->canSlash(other, nullptr, false)) candidates << other;
        if (candidates.isEmpty() || !room->askForUseSlashTo(target, candidates, "@luanwu-slash"))
            room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        return ContinueEffects;
    }
};
LuanwuCard::LuanwuCard()
{
    setSkillName("luanwu");
    target_fixed = true;
}

void LuanwuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    // Legacy subclasses (MobileMouLuanwuCard) still pay through their old card path.
    if (!getTag("luanwu_v2").toBool()) room->removePlayerMark(source, "@chaos");
    QList<ServerPlayer *> players = room->getOtherPlayers(source);
    foreach (ServerPlayer *player, players)
		room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, source->objectName(), player->objectName());
    room->doSuperLightbox(source, "luanwu");
    foreach (ServerPlayer *player, players) {
        if (player->isAlive())
            room->cardEffect(this, source, player);
        room->getThread()->delay();
    }
}

void LuanwuCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();

    QList<ServerPlayer *> players = room->getOtherPlayers(effect.to);
    QList<int> distance_list;
    int nearest = 1000;
    foreach (ServerPlayer *player, players) {
        int distance = effect.to->distanceTo(player);
        distance_list << distance;
        nearest = qMin(nearest, distance);
    }

    QList<ServerPlayer *> luanwu_targets;
    for (int i = 0; i < distance_list.length(); i++) {
        if (distance_list[i] == nearest && effect.to->canSlash(players[i], nullptr, false))
            luanwu_targets << players[i];
    }

    if (luanwu_targets.isEmpty() || !room->askForUseSlashTo(effect.to, luanwu_targets, "@luanwu-slash"))
        room->loseHp(HpLostStruct(effect.to, 1, "luanwu", effect.from));
}

class Weimu : public ProhibitSkill
{
public:
    Weimu() : ProhibitSkill("weimu")
    {
    }

    bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return card->isKindOf("TrickCard")
            && card->isBlack() && to->hasSkill(objectName()); // Be care!!!!!!
    }
};

class Jiuchi : public ViewAsSkillV2
{
public:
    Jiuchi() : ViewAsSkillV2("jiuchi", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return Analeptic::IsAvailable(request.initiator);
        if (request.pattern.startsWith("@")) return false;
        Analeptic card(Card::NoSuit, 0);
        return Sanguosha->matchExpPattern(request.pattern, request.initiator, &card);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && !card->hasFlag("using")
            && card->getSuit() == Card::Spade
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getHandPile().contains(card->getEffectiveId()));
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        Analeptic *analeptic = new Analeptic(originalCard->getSuit(), originalCard->getNumber());
        analeptic->setSkillName(objectName());
        analeptic->addSubcard(originalCard->getId());
        return analeptic;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Analeptic"; }
};

class Roulin : public TriggerSkillV2
{
public:
    Roulin() : TriggerSkillV2("roulin")
    {
        events << TargetConfirmed << TargetSpecified;
        frequency = Compulsory;
        m_baseAmount = 2;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card || !use.from
            || !use.card->isKindOf("Slash")) return {};
        if (event == TargetConfirmed)
            return use.from->isFemale() && use.to.contains(player) ? TriggerList{{player, {objectName()}}} : TriggerList();
        if (use.from != player) return {};
        for (ServerPlayer *target : use.to)
            if (target->isFemale()) return {{player, {objectName()}}};
        return {};
    }

    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (event == TargetConfirmed) ctx.targets = {ctx.owner};
        else for (ServerPlayer *target : use.to)
            if (target->isFemale()) ctx.targets << target;
        return !ctx.targets.isEmpty();
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.from) return false;
        const QString key = "Jink_" + use.card->toString();
        QVariantList jinks = use.from->getTag(key).toList();
        const int index = use.to.indexOf(target);
        if (index < 0 || index >= jinks.size() || jinks.at(index).toInt() != 1) return false;
        // Keep the per-card Jink protocol authoritative and apply this instance's amount.
        jinks[index] = getEffectiveAmount(ctx);
        use.from->setTag(key, jinks);
        int voice = qsanRandomBounded(2) + 1;
        if (ctx.invoker->isJieGeneral()) voice += 2;
        room->broadcastSkillInvoke(objectName(), voice);
        room->sendCompulsoryTriggerLog(ctx.invoker, objectName());
        return false;
    }
};

class Benghuai : public TriggerSkillV2
{
public:
    Benghuai() : TriggerSkillV2("benghuai")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Finish || player->getMark("benghuai_nullification-Clear") > 0)
            return {};
        for (ServerPlayer *other : room->getOtherPlayers(player)) {
            if (player->getHp() > other->getHp()) return TriggerList{{player, {objectName()}}};
        }
        return {};
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *dongzhuo = target;
        if (!dongzhuo || !dongzhuo->isAlive()) return false;
        room->sendCompulsoryTriggerLog(dongzhuo, objectName());
        // Losing HP is the compulsory result, not a payment that bypass_cost waives.
        const QString result = room->askForChoice(dongzhuo, objectName(), "hp+maxhp");
        int index = dongzhuo->isFemale() ? 2 : 1;
        if (dongzhuo->isJieGeneral("dongzhuo"))
            index = qsanRandomBounded(2) + 6;
        else {
            if (!dongzhuo->hasInnateSkill(this) && (dongzhuo->getMark("juyi") > 0 || dongzhuo->getMark("oljuyi") > 0))
                index = 3;
            if (!dongzhuo->hasInnateSkill(this) && dongzhuo->getMark("baoling") > 0)
                index = result == "hp" ? 4 : 5;
        }
        room->broadcastSkillInvoke(objectName(), index);
        const int amount = getEffectiveAmount(ctx);
        if (result == "hp")
            room->loseHp(HpLostStruct(dongzhuo, amount, objectName(), dongzhuo));
        else
            room->loseMaxHp(dongzhuo, amount, objectName());
        return false;
    }
};

class Baonue : public TriggerSkillV2
{
public:
    Baonue() : TriggerSkillV2("baonue$") { events << Damage; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getKingdom() != "qun") return result;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->hasLordSkill(objectName())) result[owner] << objectName();
        return result;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room->askForPlayerChosen(ctx.invoker, {ctx.owner}, objectName(), "@baonue-to", true)) return false;
        ctx.targets = {ctx.invoker}; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "recover") {
            if (!ctx.owner->isLord() && ctx.owner->hasSkill("weidi")) room->broadcastSkillInvoke("weidi");
            else room->broadcastSkillInvoke(objectName(), ctx.invoker->isMale() ? 1 : 2);
            room->recover(target, RecoverStruct(objectName(), ctx.invoker, getEffectiveAmount(ctx)));
            return false;
        }
        LogMessage log; log.type = "#InvokeOthersSkill"; log.from = ctx.invoker; log.to << ctx.owner; log.arg = objectName(); room->sendLog(log);
        room->notifySkillInvoked(ctx.owner, objectName());
        JudgeStruct judge; judge.pattern = ".|spade"; judge.good = true; judge.reason = objectName(); judge.who = target;
        room->judge(judge);
        if (judge.isGood()) {
            // Judgement and recovery affect different recipients and each receives its own hook.
            SkillContext recover = ctx; recover.choice = "recover"; recover.targets = {ctx.owner};
            skillEffect(event, room, player, recover, ctx.owner);
        }
        return false;
    }
};
class Guixin : public TriggerSkillV2
{
public:
    Guixin(const QString &name = "guixin") : TriggerSkillV2(name) { events << Damaged; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && data.value<DamageStruct>().damage > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QString key = objectName() + "Times";
        const int previous = ctx.owner->getMark(key);
        ctx.owner->setMark(key, 1); // Scoped AI projection, never activation authority.
        const auto restore = qScopeGuard([&] { ctx.owner->setMark(key, previous); });
        if (!ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets = room->getOtherPlayers(ctx.owner);
        ctx.manual_effect = true;
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QString times = objectName() + "Times", usingFlag = objectName() + "Using";
        const int previousTimes = ctx.owner->getMark(times);
        const bool previousUsing = ctx.owner->hasFlag(usingFlag);
        const auto restore = qScopeGuard([&] {
            ctx.owner->setMark(times, previousTimes);
            if (!previousUsing) ctx.owner->setFlags("-" + usingFlag);
        });
        const int rounds = ctx.original_data->value<DamageStruct>().damage;
        for (int round = 0; round < rounds && ctx.owner->isAlive(); ++round) {
            ctx.owner->setMark(times, round + 1);
            if (round > 0 && (!isSourceAvailable(room, ctx) || !ctx.owner->askForSkillInvoke(this, *ctx.original_data))) break;
            room->broadcastSkillInvoke(objectName());
            ctx.owner->setFlags(usingFlag);
            room->doSuperLightbox(ctx.owner, "newguixin");
            ctx.choice = "take";
            for (ServerPlayer *target : ctx.targets) {
                if (ctx.owner->isDead()) break;
                if (target->isAlive() && !target->isAllNude()) skillEffect(event, room, player, ctx, target);
            }
            if (ctx.owner->isAlive()) {
                // Turning the invoker over is itself a recipient effect, after all extractions.
                SkillContext turnover = ctx;
                turnover.choice = "turn_over";
                turnover.targets = {ctx.owner};
                skillEffect(event, room, player, turnover, ctx.owner);
            }
            if (!previousUsing) ctx.owner->setFlags("-" + usingFlag);
        }
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "turn_over") { target->turnOver(); return false; }
        for (int i = 0; i < qMax(0, getEffectiveAmount(ctx)); ++i) {
            if (ctx.owner->isDead() || target->isAllNude()) break;
            int id = -1;
            if (objectName() == "guixin") id = room->askForCardChosen(ctx.owner, target, "hej", objectName());
            else {
                const QList<const Card *> cards = target->getCards("hej");
                if (!cards.isEmpty()) id = cards.at(qsanRandomBounded(cards.size()))->getEffectiveId();
            }
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            const Player::Place place = room->getCardPlace(id);
            if (!card || card->hasFlag("using") || room->getCardOwner(id) != target
                || (place != Player::PlaceHand && place != Player::PlaceEquip && place != Player::PlaceDelayedTrick)) break;
            room->obtainCard(ctx.owner, card, CardMoveReason(CardMoveReason::S_REASON_EXTRACTION,
                ctx.owner->objectName()), place != Player::PlaceHand);
        }
        return false;
    }
};

class NewGuixin : public Guixin
{
public:
    NewGuixin() : Guixin("newguixin") {}
};
class Feiying : public DistanceSkillV2
{
public:
    Feiying() : DistanceSkillV2("feiying")
    {
        setHolderSelector(CorrectSkill_Secondary);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return CorrectSkillResult::useAmount(ctx.currentAmount);
    }
};

class Kuangbao : public TriggerSkillV2
{
public:
    Kuangbao() : TriggerSkillV2("kuangbao")
    {
        events << Damage << Damaged;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent triggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ServerPlayer *player = target;
        const int amount = damage.damage * getEffectiveAmount(ctx);
        LogMessage log;
        log.type = triggerEvent == Damage ? "#KuangbaoDamage" : "#KuangbaoDamaged";
        log.from = player;
        log.arg = QString::number(amount);
        log.arg2 = objectName();
        room->sendLog(log);
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(player, objectName());

        // Wrath is a shared, spendable game resource, not an instance activation counter.
        room->addPlayerMark(player, "&wrath", amount);
        return false;
    }
};

class Wumou : public TriggerSkillV2
{
public:
    Wumou() : TriggerSkillV2("wumou")
    {
        frequency = Compulsory;
        events << CardUsed;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && use.card && use.card->isNDTrick()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = target;
        const int amount = qMax(0, getEffectiveAmount(ctx));
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(player, objectName());
        // This compulsory penalty remains effective even when activation costs are waived.
        if (player->getMark("&wrath") >= amount
            && room->askForChoice(player, objectName(), "discard+losehp") == "discard")
            player->loseMark("&wrath", amount);
        else
            room->loseHp(HpLostStruct(player, amount, objectName(), player));
        return false;
    }
};

class Shenfen : public ViewAsSkillV2
{
public:
    Shenfen() : ViewAsSkillV2("shenfen") { setPhaseName("Play");}
    TargetMode targetMode() const override { return NoTarget; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "ShenfenCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark("&wrath") >= 6;
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator || ctx.initiator->getMark("&wrath") < 6) return false;
        ctx.initiator->loseMark("&wrath", 6);
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.invoker->getRoom();
        const bool previous = ctx.invoker->hasFlag("ShenfenUsing");
        ctx.invoker->setFlags("ShenfenUsing");
        const auto restore = qScopeGuard([&] { if (!previous) ctx.invoker->setFlags("-ShenfenUsing"); });
        const QList<ServerPlayer *> targets = room->getOtherPlayers(ctx.invoker);
        room->doSuperLightbox(ctx.invoker, objectName());
        // Preserve the three ordered passes; each actual recipient gets its own effect hook.
        for (const QString &stage : {QString("damage"), QString("equip"), QString("hand")}) {
            for (ServerPlayer *target : targets) {
                if (stage == "damage" && ctx.invoker->isDead()) break;
                SkillContext part = ctx;
                part.choice = stage; part.targets = {target};
                skillEffect(part, target);
            }
        }
        if (ctx.invoker->isAlive()) {
            SkillContext turnover = ctx;
            turnover.choice = "turn_over"; turnover.targets = {ctx.invoker};
            skillEffect(turnover, ctx.invoker);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        if (ctx.choice == "damage") room->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx)));
        else if (ctx.choice == "equip") target->throwAllEquips();
        else if (ctx.choice == "hand") room->askForDiscard(target, objectName(), 4 * getEffectiveAmount(ctx), 4 * getEffectiveAmount(ctx));
        else if (ctx.choice == "turn_over") target->turnOver();
        return ContinueEffects;
    }
};
ShenfenCard::ShenfenCard()
{
    setSkillName("shenfen");
    target_fixed = true;
}

void ShenfenCard::use(Room *room, ServerPlayer *shenlvbu, QList<ServerPlayer *> &) const
{
    shenlvbu->setFlags("ShenfenUsing");
	QList<ServerPlayer *> players = room->getOtherPlayers(shenlvbu);
	foreach (ServerPlayer *player, players)
		room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, shenlvbu->objectName(), player->objectName());
    room->doSuperLightbox(shenlvbu, "shenfen");
    shenlvbu->loseMark("&wrath", 6);
    try {
        foreach (ServerPlayer *player, players) {
            room->damage(DamageStruct("shenfen", shenlvbu, player));
			if(shenlvbu->isDead()) break;
            room->getThread()->delay();
        }

        foreach (ServerPlayer *player, players) {
            bool has = player->hasEquip();
            player->throwAllEquips();
            if (has) room->getThread()->delay();
        }

        foreach (ServerPlayer *player, players) {
            bool has = !player->isKongcheng();
            room->askForDiscard(player, "shenfen", 4, 4);
            if (has) room->getThread()->delay();
        }

        shenlvbu->turnOver();
        shenlvbu->setFlags("-ShenfenUsing");
    }
    catch (TriggerEvent triggerEvent) {
        if (triggerEvent == TurnBroken || triggerEvent == StageChange)
            shenlvbu->setFlags("-ShenfenUsing");
        throw triggerEvent;
    }
}

WuqianCard::WuqianCard()
{
    setSkillName("wuqian");
}

bool WuqianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void WuqianCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();

    effect.from->loseMark("&wrath", 2);
    room->acquireSkill(effect.from, "wushuang");
    effect.from->setFlags("WuqianSource");
    effect.to->setFlags("WuqianTarget");
    room->addPlayerMark(effect.to, "Armor_Nullified");
}

class WuqianViewAsSkill : public ViewAsSkillV2
{
public:
    WuqianViewAsSkill() : ViewAsSkillV2("wuqian") {}
    QString historyKey(const ActiveSkillRequest &) const override { return "WuqianCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->getMark("&wrath") >= 2; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return selected.isEmpty() && candidate && candidate->isAlive() && candidate != request.initiator; }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    { return selected.size() == 1 && canSelectTarget(request, {}, selected.first()); }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator || ctx.initiator->getMark("&wrath") < 2) return false;
        ctx.initiator->loseMark("&wrath", 2);
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        SkillContext grant = ctx; grant.choice = "grant"; grant.targets = {ctx.invoker};
        skillEffect(grant, ctx.invoker);
        if (!ctx.invoker->isAlive()) return FinishSkill;
        const QList<ServerPlayer *> targets = ctx.targets;
        for (ServerPlayer *target : targets) {
            SkillContext armor = ctx; armor.choice = "armor"; armor.targets = {target};
            skillEffect(armor, target);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        if (ctx.choice == "armor" && getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        if (turn <= 0) return ContinueEffects;
        const int serial = room->getTag("WuqianNextReceipt").toInt() + 1;
        room->setTag("WuqianNextReceipt", serial);
        QVariantMap receipt{{"serial", serial}, {"turn", turn}, {"recipient", target->objectName()},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID},
            {"grant_id", 0}, {"armor", 0}};
        QVariantList receipts = ctx.invoker->getTag("WuqianReceipts").toList();
        receipts << receipt;
        ctx.invoker->setTag("WuqianReceipts", receipts);
        if (ctx.choice == "armor") {
            ServerPlayer *issuer = ctx.invoker;
            // MarkChange can expire the pending effect; only the committed delta belongs to this receipt.
            room->setPlayerMarkWithReceipt(target, "Armor_Nullified", target->getMark("Armor_Nullified") + getEffectiveAmount(ctx),
                [issuer, target, receipt](const QString &mark, int before, int after) {
                    return mark == "Armor_Nullified" && after > before && issuer->isAlive() && target->isAlive()
                        && issuer->getTag("WuqianReceipts").toList().contains(receipt);
                },
                [issuer, receipt](const QString &, int before, int after) {
                    QVariantList committedReceipts = issuer->getTag("WuqianReceipts").toList();
                    const int index = committedReceipts.indexOf(receipt);
                    if (index < 0) return;
                    QVariantMap committed = receipt; committed.insert("armor", after - before);
                    committedReceipts[index] = committed;
                    issuer->setTag("WuqianReceipts", committedReceipts);
                });
        } else {
            const int id = room->acquireSkillFromEffect(target, "wushuang", ctx, [&](int committedId) {
                // Store the exact committed grant before acquire callbacks can expire or interrupt this effect.
                QVariantList committedReceipts = ctx.invoker->getTag("WuqianReceipts").toList();
                const int pending = committedReceipts.indexOf(receipt);
                if (pending < 0) return;
                receipt.insert("grant_id", committedId); committedReceipts[pending] = receipt;
                ctx.invoker->setTag("WuqianReceipts", committedReceipts);
            });
            // Acquisition callbacks may already have expired this issuer's pending receipt.
            receipts = ctx.invoker->getTag("WuqianReceipts").toList();
            const int index = receipts.indexOf(receipt);
            if (id > 0 && index >= 0 && ctx.invoker->isAlive()) {
                receipt.insert("grant_id", id); receipts[index] = receipt;
                ctx.invoker->setTag("WuqianReceipts", receipts);
            } else {
                if (index >= 0) receipts.removeAt(index);
                ctx.invoker->setTag("WuqianReceipts", receipts);
                if (id > 0) room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName("wushuang", id), false, true);
            }
        }
        return ContinueEffects;
    }
};

class Wuqian : public TriggerSkillV2
{
public:
    Wuqian() : TriggerSkillV2("wuqian")
    { events << EventPhaseChanging << Death; global = true; view_as_skill = new WuqianViewAsSkill; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (event == Death) {
            if (data.value<DeathStruct>().who != player) return true;
        } else if (data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
        if (event != Death && turn <= 0) return true;
        // Expiry follows the originating turn even when interception redirected the actor.
        for (ServerPlayer *issuer : room->getAllPlayers(true)) {
            if (event == Death && issuer != player) continue;
            QVariantList receipts, kept;
            for (const QVariant &entry : issuer->getTag("WuqianReceipts").toList()) {
                if (event == Death || entry.toMap().value("turn").toLongLong() == turn) receipts << entry;
                else kept << entry;
            }
            issuer->setTag("WuqianReceipts", kept);
            for (const QVariant &entry : receipts) {
                const QVariantMap receipt = entry.toMap();
                ServerPlayer *recipient = room->findPlayerByObjectName(receipt.value("recipient").toString(), true);
                if (!recipient) continue;
                const int armor = receipt.value("armor").toInt(), grant = receipt.value("grant_id").toInt();
                if (armor > 0) room->removePlayerMark(recipient, "Armor_Nullified", armor);
                if (grant > 0) room->detachSkillFromPlayer(recipient, SkillInstanceUtils::formatName("wushuang", grant), false, true);
            }
        }
        return true;
    }
};
class TenyearDuanliang : public Duanliang
{
public:
    TenyearDuanliang() : Duanliang("tenyearduanliang") {}
};

class TenyearDuanliangTargetMod : public TargetModSkillV2
{
public:
    TenyearDuanliangTargetMod() : TargetModSkillV2("#tenyearduanliang-target", "SupplyShortage")
    {
        frequency = NotFrequent;
        pattern = "SupplyShortage";
        setBaseAmount(999);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == DistanceLimit && ctx.primary && ctx.secondary
                && ctx.primary->getHandcardNum() <= ctx.secondary->getHandcardNum()
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

class TenyearJiezi : public TriggerSkillV2
{
public:
    TenyearJiezi() : TriggerSkillV2("tenyearjiezi")
    {
        events << EventPhaseSkipped;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->getPhase() != Player::Draw) return result;
        // The skipped phase belongs to the actor; each other holder owns its trigger.
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->isAlive() && owner->hasSkill(objectName())) result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

ThicketPackage::ThicketPackage()
    : Package("thicket")
{
    General *xuhuang = new General(this, "xuhuang", "wei"); // WEI 010
    xuhuang->addSkill(new Duanliang);
    xuhuang->addSkill(new DuanliangTargetMod);
    related_skills.insert("duanliang", "#duanliang-target");

    General *caopi = new General(this, "caopi$", "wei", 3); // WEI 014
    caopi->addSkill(new Xingshang);
    caopi->addSkill(new Fangzhu);
    caopi->addSkill(new Songwei);

    General *menghuo = new General(this, "menghuo", "shu"); // SHU 014
    menghuo->addSkill(new Huoshou);
    menghuo->addSkill(new Huoshou("#huoshou-source"));
    related_skills.insert("huoshou", "#huoshou-source");
    menghuo->addSkill(new Zaiqi);

    General *zhurong = new General(this, "zhurong", "shu", 4, false); // SHU 015
    zhurong->addSkill(new Juxiang);
    zhurong->addSkill(new Lieren);

    General *sunjian = new General(this, "sunjian", "wu"); // WU 009
    sunjian->addSkill(new Yinghun);

    General *lusu = new General(this, "lusu", "wu", 3); // WU 014
    lusu->addSkill(new Haoshi);
    lusu->addSkill(new HaoshiViewAsSkill);
    lusu->addSkill(new HaoshiGive);
    lusu->addSkill(new Dimeng);
    related_skills.insert("haoshi", "#haoshi-give");

    General *dongzhuo = new General(this, "dongzhuo$", "qun", 8); // QUN 006
    dongzhuo->addSkill(new Jiuchi);
    dongzhuo->addSkill(new Roulin);
    dongzhuo->addSkill(new Benghuai);
    dongzhuo->addSkill(new Baonue);

    General *jiaxu = new General(this, "jiaxu", "qun", 3); // QUN 007
    jiaxu->addSkill(new Wansha);
    jiaxu->addSkill(new WanshaLimit);
    jiaxu->addSkill(new Luanwu);
    jiaxu->addSkill(new Weimu);
    related_skills.insert("wansha", "#wansha-limit");

    General *shencaocao = new General(this, "shencaocao", "god", 3); // LE 005
    shencaocao->addSkill(new Guixin);
    shencaocao->addSkill(new Feiying);

    General *shenlvbu = new General(this, "shenlvbu", "god", 5); // LE 006
    shenlvbu->addSkill(new Kuangbao);
    shenlvbu->addSkill(new MarkAssignSkill("&wrath", 2));
    shenlvbu->addSkill(new Wumou);
    shenlvbu->addSkill(new Wuqian);
    shenlvbu->addSkill(new Shenfen);
    related_skills.insert("kuangbao", "#&wrath-2");
    addMetaObject<ShenfenCard>();
    addMetaObject<WuqianCard>();

    addMetaObject<DimengCard>();
    addMetaObject<LuanwuCard>();
    addMetaObject<HaoshiCard>();
}
ADD_PACKAGE(Thicket)

TenyearStThicketPackage::TenyearStThicketPackage()
    : Package("TenyearStThicket")
{
    General *tenyear_xuhuang = new General(this, "tenyear_xuhuang", "wei", 4);
    tenyear_xuhuang->addSkill(new TenyearDuanliang);
    tenyear_xuhuang->addSkill(new TenyearDuanliangTargetMod);
    tenyear_xuhuang->addSkill(new TenyearJiezi);
    related_skills.insert("tenyearduanliang", "#tenyearduanliang-target");
}
ADD_PACKAGE(TenyearStThicket)

void RegisterNewShencaocao(Package *pkg)
{
    General *new_shencaocao = new General(pkg, "new_shencaocao", "god", 3);
    new_shencaocao->addSkill(new NewGuixin);
    new_shencaocao->addSkill("feiying");
}
