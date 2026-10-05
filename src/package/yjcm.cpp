#include "yjcm.h"
#include <QJsonDocument>
#include "skill-declaration.h"
#include "yjcm2012.h"
#include "yjcm2013.h"
#include "yjcm2014.h"
#include "yjcm2015.h"
//#include "skill.h"
//#include "standard.h"
#include "maneuvering.h"
#include "clientplayer.h"
#include "engine.h"
#include "settings.h"
//#include "ai.h"
//#include "general.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "mobile.h"
#include <QScopeGuard>

class Yizhong : public TriggerSkillV2
{
public:
    Yizhong() : TriggerSkillV2("yizhong")
    {
        events << CardEffected;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        return player && player->isAlive() && player->hasSkill(this) && !player->getArmor()
            && effect.to == player && effect.card && effect.card->isKindOf("Slash") && effect.card->isBlack()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        if (effect.card && !ctx.owner->getArmor()) {
            room->broadcastSkillInvoke(objectName());

            LogMessage log;
            log.type = "#SkillNullify";
            log.from = ctx.owner;
            log.arg = objectName();
            log.arg2 = effect.card->objectName();
            room->sendLog(log);
            room->notifySkillInvoked(ctx.owner, objectName());

            return true;
        }
        return false;
    }
};

class Luoying : public TriggerSkillV2
{
public:
    Luoying(const QString &name = "luoying", bool all = false) : TriggerSkillV2(name), obtainAll(all)
    { events << CardsMoveOneTime; frequency = all ? NotFrequent : Frequent; }
    QList<int> available(Room *room, ServerPlayer *owner, const CardsMoveOneTimeStruct &move) const
    {
        QList<int> ids;
        if (move.from == owner || (obtainAll && !move.from) || move.to_place != Player::DiscardPile
            || ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD
                && move.reason.m_reason != CardMoveReason::S_REASON_JUDGEDONE)) return ids;
        for (int i = 0; i < move.card_ids.size() && i < move.from_places.size(); ++i) {
            const int id = move.card_ids[i];
            const Player::Place place = move.from_places[i];
            const bool permitted = obtainAll ? (move.reason.m_reason == CardMoveReason::S_REASON_JUDGEDONE
                ? place == Player::PlaceJudge : place == Player::PlaceHand || place == Player::PlaceEquip)
                : place == Player::PlaceJudge || place == Player::PlaceHand || place == Player::PlaceEquip;
            if (permitted && room->getCardPlace(id) == Player::DiscardPile && Sanguosha->getCard(id)->getSuit() == Card::Club) ids << id;
        }
        return ids;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        // Movement broadcasts may offer only the observing holder's exact copies.
        return player && player->isAlive() && player->hasSkill(this) && !available(room, player, data.value<CardsMoveOneTimeStruct>()).isEmpty()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const QList<int> cards = available(room, ctx.owner, ctx.original_data->value<CardsMoveOneTimeStruct>());
        if (cards.isEmpty() || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        ctx.extra_data = ListI2V(cards);
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        QList<int> cards, selected;
        for (const QVariant &value : ctx.extra_data.toList())
            if (room->getCardPlace(value.toInt()) == Player::DiscardPile) cards << value.toInt();
        if (obtainAll) selected = cards;
        else if (!cards.isEmpty()) {
            const auto cleanup = qScopeGuard([=] { room->clearAG(target); });
            room->fillAG(cards, target);
            while (target->isAlive() && !cards.isEmpty()) {
                const int id = room->askForAG(target, cards, !selected.isEmpty(), objectName(), "@luoying_get");
                if (!cards.contains(id)) break;
                selected << id;
                cards.removeOne(id);
                room->takeAG(target, id, false, QList<ServerPlayer *>{target});
            }
        }
        DummyCard obtained;
        for (int id : selected) if (room->getCardPlace(id) == Player::DiscardPile) obtained.addSubcard(id);
        if (target->isAlive() && obtained.subcardsLength() > 0) {
            CardMoveReason reason = ctx.original_data->value<CardsMoveOneTimeStruct>().reason;
            reason.m_skillName = objectName();
            room->moveCardTo(&obtained, target, Player::PlaceHand, reason, true);
        }
        return false;
    }
private:
    bool obtainAll;
};
class Jiushi : public ViewAsSkillV2
{
public:
    Jiushi() : ViewAsSkillV2("jiushi") { response_or_use = true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !request.initiator->faceUp()) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? Analeptic::IsAvailable(request.initiator)
            : request.pattern.contains("analeptic");
    }
    const Card *createCard(const ActiveSkillRequest &) const override
    {
        Analeptic *card = new Analeptic(Card::NoSuit, 0);
        card->setSkillName(objectName());
        return card;
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!ctx.initiator || !ctx.initiator->isAlive() || !ctx.initiator->faceUp()) return false;
        ctx.initiator->turnOver();
        return true;
    }
    int getEffectIndex(const ServerPlayer *, const Card *) const override { return qsanRandomBounded(2) + 1; }
};

class JiushiFlip : public TriggerSkillV2
{
public:
    JiushiFlip() : TriggerSkillV2("#jiushi-flip")
    { events << DamageDone << DamageComplete << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            for (ServerPlayer *holder : room->getAllPlayers(true))
                for (int id : holder->getValidSkillInstanceIds(objectName()))
                    holder->removeSkillInstanceStateValue(objectName(), id, "damage_faces");
        }
        if (event != DamageDone || !player || data.value<DamageStruct>().to != player) return true;
        const qint64 damageId = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong();
        if (damageId <= 0) return true;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            QVariantMap faces = player->getSkillInstanceStateValue(objectName(), id, "damage_faces").toMap();
            faces.insert(QString::number(damageId), !player->faceUp());
            player->setSkillInstanceStateValue(objectName(), id, "damage_faces", faces);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != DamageComplete || !player || data.value<DamageStruct>().to != player) return true;
        const qint64 damageId = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong();
        if (damageId <= 0) return true;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            QVariantMap faces = player->getSkillInstanceStateValue(objectName(), id, "damage_faces").toMap();
            const bool wasDown = faces.value(QString::number(damageId)).toBool();
            if (!wasDown || !player->isAlive() || player->faceUp()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.instanceID = id;
            ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
            ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
            ctx.amount = room->getSkillInstanceAmount(ctx.activationRef);
            ctx.original_data = &data;
            ctx.current_event = event;
            if (ctx.sourceRef.isValid()) contexts << ctx;
        }
        return true;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || ctx.owner->faceUp() || !room->askForSkillInvoke(ctx.owner, "jiushi", *ctx.original_data)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive() && !target->faceUp() && getEffectiveAmount(ctx) > 0) {
            room->broadcastSkillInvoke("jiushi", 3);
            target->turnOver();
        }
        return false;
    }
};
class Wuyan : public TriggerSkillV2
{
public:
    Wuyan() : TriggerSkillV2("wuyan")
    {
        events << DamageCaused << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *owner = event == DamageCaused ? damage.from : damage.to;
        return owner && owner->isAlive() && owner->hasSkill(this)
            && damage.card && damage.card->getTypeId() == Card::TypeTrick
            ? TriggerList{{owner, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        SkillContext prevent = ctx;
        prevent.extra_data = false;
        skillEffect(event, room, player, prevent, ctx.original_data->value<DamageStruct>().to);
        return prevent.extra_data.toBool();
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.owner || !ctx.original_data || getEffectiveAmount(ctx) <= 0) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.card) return false;
        // The causing and receiving branches preserve their own source identity.
        LogMessage log;
        log.type = event == DamageInflicted ? "#WuyanGood" : "#WuyanBad";
        log.from = ctx.owner;
        log.arg = damage.card->objectName();
        log.arg2 = objectName();
        room->sendLog(log);
        room->broadcastSkillInvoke(objectName(), event == DamageInflicted ? 2 : 1);
        room->notifySkillInvoked(ctx.owner, objectName());
        ctx.extra_data = true;
        return false;
    }
};

JujianCard::JujianCard()
{
    setSkillName("jujian");
    mute = true;
}

bool JujianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void JujianCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if (effect.to->getGeneralName().contains("zhugeliang") || effect.to->getGeneralName() == "wolong")
        room->broadcastSkillInvoke("jujian", 2);
    else
        room->broadcastSkillInvoke("jujian", 1);

    QStringList choicelist;
    choicelist << "draw";
    if (effect.to->isWounded())
        choicelist << "recover";
    if (!effect.to->faceUp() || effect.to->isChained())
        choicelist << "reset";
    QString choice = room->askForChoice(effect.to, "jujian", choicelist.join("+"));

    if (choice == "draw")
        effect.to->drawCards(2, "jujian");
    else if (choice == "recover")
        room->recover(effect.to, RecoverStruct("jujian", effect.from));
    else if (choice == "reset") {
        if (effect.to->isChained())
            room->setPlayerChained(effect.to);
        if (!effect.to->faceUp())
            effect.to->turnOver();
    }
}

class JujianViewAsSkill : public ViewAsSkillV2
{
public:
    JujianViewAsSkill() : ViewAsSkillV2("jujian", 1) { }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY && request.pattern == "@@jujian"; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && !card->isKindOf("BasicCard")
            && !card->hasFlag("using") && !request.initiator->isJilei(card)
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId()));
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return candidate && candidate->isAlive() && candidate != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "JujianCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return ContinueEffects;
        Room *room = target->getRoom();
        QStringList choices{"draw"};
        if (target->isWounded()) choices << "recover";
        if (!target->faceUp() || target->isChained()) choices << "reset";
        const QString choice = room->askForChoice(target, objectName(), choices.join('+'));
        if (choice == "recover") {
            RecoverStruct recovery(objectName(), ctx.invoker);
            recovery.recover = getEffectiveAmount(ctx);
            room->recover(target, recovery);
        } else if (choice == "reset") {
            if (target->isChained()) room->setPlayerChained(target);
            if (target->isAlive() && !target->faceUp()) target->turnOver();
        } else target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
};

class Jujian : public TriggerSkillV2
{
public:
    Jujian() : TriggerSkillV2("jujian") { events << EventPhaseStart; view_as_skill = new JujianViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Finish && player->canDiscard(player, "he")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        Room::BorrowedSkillScope prompt(room, ctx.owner, objectName(), ctx.activationRef);
        if (!prompt.activationRef().isValid()) return false;
        RoomState *state = Sanguosha->currentRoomState();
        const auto reason = state->getCurrentCardUseReason();
        const QString pattern = state->getCurrentCardUsePattern();
        const auto restore = qScopeGuard([=] {
            state->setCurrentCardUseReason(reason);
            state->setCurrentCardUsePattern(pattern);
        });
        room->askForUseCard(ctx.owner, "@@jujian", "@jujian-card", -1, Card::MethodDiscard);
        return false;
    }
};
class Enyuan : public TriggerSkillV2
{
public:
    Enyuan() : TriggerSkillV2("enyuan") { events << CardsMoveOneTime << Damaged; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(this)) return {};
        if (event == Damaged) {
            const DamageStruct damage = data.value<DamageStruct>();
            return damage.from && damage.from != player && damage.from->isAlive() && damage.damage > 0
                ? TriggerList{{player, {objectName() + "*" + QString::number(damage.damage)}}} : TriggerList();
        }
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return move.to == player && move.from && move.from->isAlive() && move.from != player && move.card_ids.size() >= 2
            && move.reason.m_reason != CardMoveReason::S_REASON_PREVIEWGIVE
            && (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        ServerPlayer *target = nullptr;
        if (event == Damaged) target = ctx.original_data->value<DamageStruct>().from;
        else {
            const auto move = ctx.original_data->value<CardsMoveOneTimeStruct>();
            if (move.from) target = room->findPlayerByObjectName(move.from->objectName());
        }
        if (!target || !target->isAlive() || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        const int amount = getEffectiveAmount(ctx);
        if (event == CardsMoveOneTime) {
            target->drawCards(amount, objectName());
            return false;
        }
        const Card *card = nullptr;
        if (amount > 0 && target->getHandcardNum() >= amount) {
            const QVariant old = target->getTag("enyuan_data");
            const auto restore = qScopeGuard([=] {
                if (old.isValid()) target->setTag("enyuan_data", old);
                else target->removeTag("enyuan_data");
            });
            target->setTag("enyuan_data", *ctx.original_data);
            card = room->askForExchange(target, objectName(), amount, amount, false, "EnyuanGive::" + ctx.owner->objectName(), true);
        }
        QList<int> ids;
        if (card) for (int id : card->getSubcards())
            if (room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand && !Sanguosha->getCard(id)->hasFlag("using")) ids << id;
        if (amount > 0 && ids.size() == amount && ctx.owner->isAlive()) {
            DummyCard gift(ids);
            room->giveCard(target, ctx.owner, &gift, objectName());
        } else if (amount > 0) room->loseHp(HpLostStruct(target, amount, objectName(), ctx.owner));
        return false;
    }
};
class Xuanhuo : public TriggerSkillV2
{
public:
    Xuanhuo() : TriggerSkillV2("xuanhuo") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Draw
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner),
            objectName(), "xuanhuo-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        ctx.manual_effect = true;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        SkillContext replacement = ctx;
        replacement.choice = "replace";
        replacement.extra_data = false;
        skillEffect(event, room, player, replacement, ctx.owner);
        if (!replacement.extra_data.toBool()) return false;
        for (ServerPlayer *target : ctx.targets) {
            SkillContext recipient = ctx;
            skillEffect(event, room, player, recipient, target);
        }
        // Acceptance replaces the owner's draw phase even if the recipient effect is cancelled.
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "replace") { ctx.extra_data = true; return false; }
        if (ctx.choice == "gain") {
            const QVariantMap gift = ctx.extra_data.toMap();
            const QString name = gift.value("donor").toString();
            ServerPlayer *donor = name.isEmpty() ? nullptr : room->findPlayerByObjectName(name, true);
            if (!donor) return false;
            DummyCard cards;
            for (const QVariant &entry : gift.value("cards").toList()) {
                const int id = entry.toInt();
                if (room->getCardOwner(id) == donor && (room->getCardPlace(id) == Player::PlaceHand
                    || room->getCardPlace(id) == Player::PlaceEquip) && !Sanguosha->getCard(id)->hasFlag("using")) cards.addSubcard(id);
            }
            if (cards.subcardsLength() > 0) room->moveCardTo(&cards, target, Player::PlaceHand, false);
            return false;
        }
        target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        if (!target->isAlive() || !ctx.owner->isAlive()) return false;
        QList<ServerPlayer *> victims;
        for (ServerPlayer *candidate : room->getOtherPlayers(target))
            if (target->canSlash(candidate)) victims << candidate;
        ServerPlayer *victim = victims.isEmpty() ? nullptr : room->askForPlayerChosen(ctx.owner, victims,
            "xuanhuo_slash", "@dummy-slash2:" + target->objectName());
        RoomState *state = Sanguosha->currentRoomState();
        const auto reason = state->getCurrentCardUseReason();
        const QString pattern = state->getCurrentCardUsePattern();
        const auto restore = qScopeGuard([=] { state->setCurrentCardUseReason(reason); state->setCurrentCardUsePattern(pattern); });
        if (victim && room->askForUseSlashTo(target, victim,
            QString("xuanhuo-slash:%1:%2").arg(ctx.owner->objectName()).arg(victim->objectName()))) return false;
        if (!target->isAlive() || !ctx.owner->isAlive()) return false;
        QList<int> selected;
        for (int i = 0; i < 2 * getEffectiveAmount(ctx) && selected.size() < target->getCardCount(true); ++i) {
            const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodNone, selected);
            if (id < 0 || selected.contains(id) || room->getCardOwner(id) != target) break;
            selected << id;
        }
        QVariantList ids;
        for (int id : selected) ids << id;
        SkillContext gain = ctx;
        gain.choice = "gain";
        gain.extra_data = QVariantMap{{"donor", target->objectName()}, {"cards", ids}};
        skillEffect(event, room, player, gain, ctx.owner);
        return false;
    }
};

class Huilei : public TriggerSkillV2
{
public:
    Huilei() : TriggerSkillV2("huilei") { events << Death; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DeathStruct death = data.value<DeathStruct>();
        // Death is broadcast to every observer: only the victim's dispatch owns this trigger.
        return player && player == death.who && player->hasSkill(this) && death.damage
            && death.damage->from && death.damage->from != player ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        const DeathStruct death = ctx.original_data->value<DeathStruct>();
        if (!death.damage || !death.damage->from) return false;
        ctx.targets = {death.damage->from};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner || !target) return false;
        LogMessage log;
        log.type = "#HuileiThrow";
        log.from = ctx.owner;
        log.to << target;
        log.arg = objectName();
        room->sendLog(log);
        const QString general = target->getGeneralName();
        room->broadcastSkillInvoke(objectName(), general.contains("zhugeliang") || general == "wolong" ? 1 : 2);
        target->throwAllHandCardsAndEquips(objectName());
        return false;
    }
};
class Xuanfeng : public TriggerSkillV2
{
public:
    Xuanfeng() : TriggerSkillV2("xuanfeng") { events << CardsMoveOneTime << EventPhaseEnd; }
    static int discarded(Room *room, ServerPlayer *player)
    {
        const QVariant phase = room->historyScopes().value("phase_id");
        if (phase.toLongLong() <= 0) return -1;
        QVariantMap filter{{"phase_id", phase}, {"from", player->objectName()}, {"limit", 128}};
        int count = 0;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return -1;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap move = entry.toMap().value("data").toMap();
                if (!move.contains("reason")) return -1;
                if ((move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD)
                    ++count;
            }
            if (!page.value("has_more").toBool()) return count;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *owner)
    {
        QList<ServerPlayer *> targets;
        for (ServerPlayer *target : room->getOtherPlayers(owner))
            if (owner->canDiscard(target, "he")) targets << target;
        return targets;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(this) || candidates(room, player).isEmpty()) return {};
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        const bool eligible = event == CardsMoveOneTime
            ? move.from == player && move.from_places.contains(Player::PlaceEquip)
            : player->getPhase() == Player::Discard && discarded(room, player) >= 2;
        return eligible ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner && !candidates(room, ctx.owner).isEmpty() && ctx.owner->askForSkillInvoke(this);
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (getEffectiveAmount(ctx) <= 0) return false;
        // Recompute the second choice after the first discard and its nested effects.
        for (int i = 0; i < 2 && ctx.owner->isAlive(); ++i) {
            const QList<ServerPlayer *> targets = candidates(room, ctx.owner);
            if (targets.isEmpty()) break;
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName());
            if (!target) break;
            SkillContext discard = ctx;
            skillEffect(event, room, player, discard, target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive() && ctx.owner->isAlive()
             && ctx.owner->canDiscard(target, "he"); ++i) {
            const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodDiscard);
            if (room->getCardOwner(id) != target || !ctx.owner->canDiscard(target, id)
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || Sanguosha->getCard(id)->hasFlag("using")) break;
            room->throwCard(id, target, ctx.owner);
        }
        return false;
    }
};

class Pojun : public TriggerSkillV2
{
public:
    Pojun() : TriggerSkillV2("pojun") { events << Damage; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player == damage.from && player->isAlive() && player->hasSkill(this) && damage.card
            && damage.card->isKindOf("Slash") && !damage.chain && !damage.transfer && damage.to && damage.to->isAlive()
            && !damage.to->hasFlag("Global_DebutFlag") ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data || !room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        ctx.targets = {ctx.original_data->value<DamageStruct>().to};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        target->drawCards(qMax(0, qMin(5, target->getHp())) * getEffectiveAmount(ctx), objectName());
        if (target->isAlive()) target->turnOver();
        return false;
    }
};
XianzhenCard::XianzhenCard()
{
    setSkillName("xianzhen");
}

bool XianzhenCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void XianzhenCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if (effect.from->pindian(effect.to, "xianzhen", nullptr)) {
        effect.from->setTag("XianzhenTarget", QVariant::fromValue(effect.to));
        room->setPlayerFlag(effect.from, "XianzhenSuccess");
        room->addPlayerMark(effect.from, effect.to->objectName()+"xianzhen-Clear");
        room->addPlayerMark(effect.to, "Armor_Nullified");
    } else
        room->setPlayerCardLimitation(effect.from, "use", "Slash", true);
}

class XianzhenViewAsSkill : public ViewAsSkillV2
{
public:
    XianzhenViewAsSkill() : ViewAsSkillV2("xianzhen", 0) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "XianzhenCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->canPindian(); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return request.initiator && target && target->isAlive() && selected.isEmpty() && request.initiator->canPindian(target); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        for (ServerPlayer *target : ctx.targets) {
            SkillContext duel = ctx; duel.choice = "duel"; duel.extra_data = QVariantMap{{"outcome", -1}};
            skillEffect(duel, target);
            const QVariantMap result = duel.extra_data.toMap();
            if (result.value("outcome", -1).toInt() < 0) continue;
            SkillContext self = ctx;
            self.choice = result.value("outcome").toInt() == 1 ? "win" : "lose";
            self.extra_data = result;
            skillEffect(self, ctx.initiator);
        }
        return FinishSkill;
    }
    static QVariantMap newReceipt(Room *room, const SkillContext &ctx)
    {
        const int serial = room->getTag("XianzhenReceiptSerial").toInt() + 1;
        room->setTag("XianzhenReceiptSerial", serial);
        return {{"serial", serial}, {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"reason", "xianzhen:" + QString::number(serial)}};
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.initiator || !ctx.initiator->isAlive() || !target || !target->isAlive() || getEffectiveAmount(ctx) <= 0)
            return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice == "duel") {
            if (!ctx.initiator->canPindian(target)) return ContinueEffects;
            const bool won = ctx.initiator->pindian(target, objectName(), nullptr);
            if (!ctx.initiator->isAlive() || !target->isAlive()) return ContinueEffects;
            QVariantMap result{{"outcome", won ? 1 : 0}};
            if (won) {
                QVariantMap receipt = newReceipt(room, ctx);
                receipt["target"] = target->objectName(); receipt["amount"] = 0;
                receipt["armor_pattern"] = "Armor|.|.|.|target:" + ctx.initiator->objectName();
                QVariantList receipts = QJsonDocument::fromJson(ctx.initiator->property("XianzhenEffects").toByteArray()).toVariant().toList();
                receipts << receipt;
                room->setPlayerProperty(ctx.initiator, "XianzhenEffects", QString::fromUtf8(QJsonDocument::fromVariant(receipts).toJson(QJsonDocument::Compact)));
                // The native pattern limits armor suppression to the duelist, and the unique reason owns cleanup.
                room->setPlayerEquipsNullified(target, receipt.value("armor_pattern").toString(), receipt.value("reason").toString(), false);
                result["serial"] = receipt.value("serial");
            }
            ctx.extra_data = result;
        } else if (ctx.choice == "win") {
            QVariantList receipts = QJsonDocument::fromJson(target->property("XianzhenEffects").toByteArray()).toVariant().toList();
            for (QVariant &entry : receipts) {
                QVariantMap receipt = entry.toMap();
                if (receipt.value("serial") == ctx.extra_data.toMap().value("serial")) {
                    receipt["amount"] = getEffectiveAmount(ctx); entry = receipt;
                }
            }
            room->setPlayerProperty(target, "XianzhenEffects", QString::fromUtf8(QJsonDocument::fromVariant(receipts).toJson(QJsonDocument::Compact)));
        } else {
            QVariantMap receipt = newReceipt(room, ctx); receipt["failed"] = true;
            QVariantList receipts = QJsonDocument::fromJson(target->property("XianzhenEffects").toByteArray()).toVariant().toList(); receipts << receipt;
            room->setPlayerProperty(target, "XianzhenEffects", QString::fromUtf8(QJsonDocument::fromVariant(receipts).toJson(QJsonDocument::Compact)));
            room->setPlayerCardLimitation(target, "use", "Slash", false, receipt.value("reason").toString());
        }
        return ContinueEffects;
    }
};

class Xianzhen : public TriggerSkillV2
{
public:
    Xianzhen() : TriggerSkillV2("xianzhen")
    { events << EventPhaseChanging << Death; global = true; view_as_skill = new XianzhenViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *dead = event == Death ? data.value<DeathStruct>().who : nullptr;
        if (event == Death ? player != dead : data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        for (ServerPlayer *holder : room->getAllPlayers(true)) {
            QVariantList kept;
            for (const QVariant &entry : QJsonDocument::fromJson(holder->property("XianzhenEffects").toByteArray()).toVariant().toList()) {
                const QVariantMap receipt = entry.toMap();
                ServerPlayer *target = room->findPlayerByObjectName(receipt.value("target").toString());
                if (dead && dead != holder && dead != target) { kept << entry; continue; }
                if (receipt.value("failed").toBool()) room->removePlayerCardLimitationByReason(holder, receipt.value("reason").toString());
                else if (target) room->removePlayerEquipsNullified(target, receipt.value("armor_pattern").toString(), receipt.value("reason").toString());
            }
            room->setPlayerProperty(holder, "XianzhenEffects", QString::fromUtf8(QJsonDocument::fromVariant(kept).toJson(QJsonDocument::Compact)));
        }
        return true;
    }
};

class XianzhenTargetMod : public TargetModSkillV2
{
public:
    XianzhenTargetMod() : TargetModSkillV2("#xianzhen_target", "^SkillCard") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.secondary || !ctx.card || ctx.card->getTypeId() == Card::TypeSkill || ctx.currentAmount <= 0)
            return CorrectSkillResult::noEffect();
        if (ctx.modType != DistanceLimit && (ctx.modType != Residue || !ctx.card->isKindOf("Slash")))
            return CorrectSkillResult::noEffect();
        for (const QVariant &entry : QJsonDocument::fromJson(ctx.primary->property("XianzhenEffects").toByteArray()).toVariant().toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("target").toString() == ctx.secondary->objectName() && receipt.value("amount").toInt() > 0)
                return CorrectSkillResult::useAmount(1000 * ctx.currentAmount * receipt.value("amount").toInt());
        }
        return CorrectSkillResult::noEffect();
    }
};

class Jinjiu : public FilterSkill
{
public:
    Jinjiu() : FilterSkill("jinjiu")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->objectName() == "analeptic";
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->setSkillName(objectName());/*
        WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
        card->takeOver(slash);*/
        return slash;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qsanRandomBounded(2) + 1;
        if (player->isJieGeneral())
            index += 2;
        return index;
    }
};

MingceCard::MingceCard()
{
    setSkillName("mingce");
    will_throw = false;
    handling_method = Card::MethodNone;
}

void MingceCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    QList<ServerPlayer *> targets;
    if (Slash::IsAvailable(effect.to)) {
        foreach (ServerPlayer *p, room->getOtherPlayers(effect.to)) {
            if (effect.to->canSlash(p))
                targets << p;
        }
    }

    ServerPlayer *target = nullptr;
    QStringList choicelist;
    choicelist << "draw";
    if (!targets.isEmpty() && effect.from->isAlive()) {
        target = room->askForPlayerChosen(effect.from, targets, "mingce", "@dummy-slash2:" + effect.to->objectName());
        target->setFlags("MingceTarget"); // For AI

        LogMessage log;
        log.type = "#CollateralSlash";
        log.from = effect.from;
        log.to << target;
        room->sendLog(log);

        choicelist << "use";
    }

    try {
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "mingce", "");
        room->obtainCard(effect.to, this, reason);
    }
    catch (TriggerEvent triggerEvent) {
        if (triggerEvent == TurnBroken || triggerEvent == StageChange)
            if (target && target->hasFlag("MingceTarget")) target->setFlags("-MingceTarget");
        throw triggerEvent;
    }

    QString choice = room->askForChoice(effect.to, "mingce", choicelist.join("+"));
    if (target && target->hasFlag("MingceTarget")) target->setFlags("-MingceTarget");

    if (choice == "use") {
        if (effect.to->canSlash(target, nullptr, false)) {
            Slash *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName("_mingce");
			slash->deleteLater();
            room->useCard(CardUseStruct(slash, effect.to, target));
        }
    } else if (choice == "draw") {
        effect.to->drawCards(1, "mingce");
    }
}

class Mingce : public ViewAsSkillV2
{
public:
    Mingce() : ViewAsSkillV2("mingce", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MingceCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && !card->hasFlag("using")
            && (card->isKindOf("EquipCard") || card->isKindOf("Slash"))
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId()));
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return candidate && candidate->isAlive() && candidate != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest empty = request;
        empty.selectedCardIds.clear();
        return canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (!target || !target->isAlive() || !ctx.initiator || amount <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice == "attack") {
            const QString name = ctx.extra_data.toString();
            ServerPlayer *actor = name.isEmpty() ? nullptr : room->findChild<ServerPlayer *>(name);
            for (int i = 0; i < amount && actor && actor->isAlive() && target->isAlive(); ++i) {
                Slash slash(Card::NoSuit, 0);
                slash.setSkillName("_mingce");
                if (!actor->canSlash(target, &slash, false)) break;
                room->useCardFromSkillEffect(CardUseStruct(&slash, actor, target), ctx, true);
            }
            return ContinueEffects;
        }
        if (!ctx.use_card || ctx.use_card->subcardsLength() != 1) return ContinueEffects;
        const int id = ctx.use_card->getSubcards().first();
        QList<ServerPlayer *> candidates;
        if (Slash::IsAvailable(target))
            for (ServerPlayer *other : room->getOtherPlayers(target)) if (target->canSlash(other)) candidates << other;
        ServerPlayer *victim = candidates.isEmpty() || !ctx.initiator->isAlive() ? nullptr
            : room->askForPlayerChosen(ctx.initiator, candidates, objectName(), "@dummy-slash2:" + target->objectName());
        const bool previous = victim && victim->hasFlag("MingceTarget");
        if (victim) victim->setFlags("MingceTarget");
        const auto cleanup = qScopeGuard([=] { if (victim && !previous) victim->setFlags("-MingceTarget"); });
        if (!target->isAlive() || room->getCardOwner(id) != ctx.initiator
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
            || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        if (victim) {
            LogMessage log;
            log.type = "#CollateralSlash";
            log.from = ctx.initiator;
            log.to << victim;
            room->sendLog(log);
        }
        room->obtainCard(target, Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_GIVE,
            ctx.initiator->objectName(), target->objectName(), objectName(), ""));
        if (!target->isAlive()) return ContinueEffects;
        if (room->askForChoice(target, objectName(), victim ? "draw+use" : "draw") == "use") {
            SkillContext attack = ctx;
            attack.choice = "attack";
            attack.extra_data = target->objectName();
            if (victim) skillEffect(attack, victim);
        } else target->drawCards(amount, objectName());
        return ContinueEffects;
    }
};
class Zhichi : public TriggerSkillV2
{
public:
    Zhichi() : TriggerSkillV2("zhichi") { events << Damaged; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(this) && room->getCurrent() && room->getCurrent() != player
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return ctx.owner != nullptr; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        const int serial = room->getTag("ZhichiReceiptSerial").toInt() + 1;
        room->setTag("ZhichiReceiptSerial", serial);
        QVariantList receipts = target->getTag("ZhichiEffects").toList();
        receipts << QVariantMap{{"serial", serial}, {"owner", ctx.sourceRef.ownerObjectName},
            {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}};
        target->setTag("ZhichiEffects", receipts);
        room->setPlayerMark(target, "@late", 1);
        target->peiyin(this, target->isJieGeneral() ? 3 : 1);
        LogMessage log;
        log.type = "#ZhichiDamaged";
        log.from = target;
        room->sendLog(log);
        return false;
    }
};

class ZhichiProtect : public TriggerSkillV2
{
public:
    ZhichiProtect() : TriggerSkillV2("#zhichi-protect") { events << CardEffected; global = true; }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!player || !player->isAlive() || effect.to != player || !effect.card
            || (!effect.card->isKindOf("Slash") && !effect.card->isNDTrick())) return true;
        for (const QVariant &entry : player->getTag("ZhichiEffects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("amount").toInt() <= 0) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(),
                SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            if (!ctx.sourceRef.isValid()) continue;
            ctx.instanceID = receipt.value("serial").toInt();
            ctx.extra_data = receipt;
            ctx.current_event = event;
            ctx.original_data = &data;
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        return ctx.owner && ctx.owner->isAlive()
            && ctx.owner->getTag("ZhichiEffects").toList().contains(ctx.extra_data);
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        SkillContext prevent = ctx;
        prevent.extra_data = false;
        skillEffect(event, room, player, prevent, ctx.original_data->value<CardEffectStruct>().to);
        return prevent.extra_data.toBool();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        target->peiyin("zhichi", target->isJieGeneral() ? 4 : 2);
        LogMessage log;
        log.type = "#ZhichiAvoid";
        log.from = target;
        log.arg = "zhichi";
        room->sendLog(log);
        ctx.extra_data = true;
        return false;
    }
};

class ZhichiClear : public TriggerSkillV2
{
public:
    ZhichiClear() : TriggerSkillV2("#zhichi-clear") { events << EventPhaseChanging << Death; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        } else if (!player || data.value<DeathStruct>().who != player || player != room->getCurrent()) return true;
        // Clear applied receipts independently of whether their granting entries still exist.
        for (ServerPlayer *target : room->getAllPlayers(true)) {
            target->setTag("ZhichiEffects", QVariantList());
            if (target->getMark("@late") > 0) room->setPlayerMark(target, "@late", 0);
        }
        return true;
    }
};
GanluCard::GanluCard()
{
    setSkillName("ganlu");
}

void GanluCard::swapEquip(ServerPlayer *first, ServerPlayer *second) const
{
	Room *room = first->getRoom();
	room->swapEquips(first, second, "ganlu");
}

bool GanluCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == 2;
}

bool GanluCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    switch (targets.length()) {
    case 0: return true;
    case 1: {
        int n1 = targets.first()->getEquips().length();
        int n2 = to_select->getEquips().length();
        return qAbs(n1 - n2) <= Self->getLostHp();
    }
    default:
        return false;
    }
}

void GanluCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    LogMessage log;
    log.type = "#GanluSwap";
    log.from = source;
    log.to = targets;
    room->sendLog(log);

    swapEquip(targets.first(), targets[1]);
}

class Ganlu : public ViewAsSkillV2
{
public:
    Ganlu() : ViewAsSkillV2("ganlu") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    QString historyKey(const ActiveSkillRequest &) const override { return "GanluCard"; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    {
        return request.initiator && candidate && candidate->isAlive() && !selected.contains(candidate)
            && (selected.isEmpty() || (selected.size() == 1
                && qAbs(selected.first()->getEquips().size() - candidate->getEquips().size()) <= request.initiator->getLostHp()));
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 2; }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        QStringList accepted = ctx.extra_data.toStringList();
        accepted << target->objectName();
        ctx.extra_data = accepted;
        return ContinueEffects;
    }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        if (!ctx.invoker || targets.size() != 2) return ContinueEffects;
        ctx.extra_data = QStringList();
        // A swap is atomic and requires both recipients to pass target interception.
        for (ServerPlayer *target : targets) skillEffect(ctx, target);
        const QStringList accepted = ctx.extra_data.toStringList();
        if (accepted.size() != 2 || accepted[0] == accepted[1]) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        ServerPlayer *first = room->findPlayerByObjectName(accepted[0]);
        ServerPlayer *second = room->findPlayerByObjectName(accepted[1]);
        if (!first || !second || !first->isAlive() || !second->isAlive()) return ContinueEffects;
        LogMessage log;
        log.type = "#GanluSwap";
        log.from = ctx.invoker;
        log.to = {first, second};
        room->sendLog(log);
        room->swapEquips(first, second, objectName());
        return ContinueEffects;
    }
};

class Buyi : public TriggerSkillV2
{
public:
    Buyi() : TriggerSkillV2("buyi") { events << Dying; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DyingStruct dying = data.value<DyingStruct>();
        // Dying already visits each observer, so only that observer's instances participate.
        return player && player->isAlive() && player->hasSkill(this) && dying.who && dying.who->getHp() < 1
            && !dying.who->isKongcheng() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        ServerPlayer *target = ctx.original_data->value<DyingStruct>().who;
        if (!target || target->getHp() >= 1 || target->isKongcheng()
            || !ctx.owner->askForSkillInvoke(this, *ctx.original_data, false)) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner || !target || target->isKongcheng()) return false;
        const Card *card = nullptr;
        if (target == ctx.owner) card = room->askForCardShow(target, ctx.owner, objectName());
        else {
            const int id = room->askForCardChosen(ctx.owner, target, "h", objectName());
            if (id >= 0) card = Sanguosha->getCard(id);
        }
        if (!card) return false;
        const int id = card->getEffectiveId();
        const bool basic = card->getTypeId() == Card::TypeBasic;
        room->showCard(target, id);
        if (!basic) {
            if (target->handCards().contains(id) && !target->isJilei(Sanguosha->getCard(id))) room->throwCard(id, objectName(), target);
            RecoverStruct recover(objectName(), ctx.owner);
            recover.recover = getEffectiveAmount(ctx);
            if (target->isAlive()) room->recover(target, recover);
        }
        return false;
    }
};
XinzhanCard::XinzhanCard()
{
    setSkillName("xinzhan");
    target_fixed = true;
}

void XinzhanCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QList<int> cards = room->getNCards(3), left;

    LogMessage log;
    log.type = "$ViewDrawPile";
    log.from = source;
    log.card_str = ListI2S(cards).join("+");
    room->sendLog(log, source);

    left = cards;

    QList<int> hearts, non_hearts;
    foreach (int card_id, cards) {
        const Card *card = Sanguosha->getCard(card_id);
        if (card->getSuit() == Card::Heart)
            hearts << card_id;
        else
            non_hearts << card_id;
    }

    if (!hearts.isEmpty()) {
        DummyCard *dummy = new DummyCard;
        do {
            room->fillAG(left, source, non_hearts);
            int card_id = room->askForAG(source, hearts, true, "xinzhan");
            if (card_id == -1) {
                room->clearAG(source);
                break;
            }

            hearts.removeOne(card_id);
            left.removeOne(card_id);

            dummy->addSubcard(card_id);
            room->clearAG(source);
        } while (!hearts.isEmpty());

        if (dummy->subcardsLength() > 0) {
            room->doBroadcastNotify(QSanProtocol::S_COMMAND_UPDATE_PILE, QVariant(room->getDrawPile().length() + dummy->subcardsLength()));
            source->obtainCard(dummy);
            foreach(int id, dummy->getSubcards())
                room->showCard(source, id);
        }
        dummy->deleteLater();
    }

    if (!left.isEmpty())
        room->askForGuanxing(source, left, Room::GuanxingUpOnly);
}

class Xinzhan : public ViewAsSkillV2
{
public:
    Xinzhan() : ViewAsSkillV2("xinzhan") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getHandcardNum() > request.initiator->getMaxHp();
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "XinzhanCard"; }
    TargetMode targetMode() const override { return NoTarget; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.invoker) return FinishSkill;
        Room *room = ctx.invoker->getRoom();
        const QList<int> cards = room->getNCards(3 * getEffectiveAmount(ctx));
        if (cards.isEmpty()) return FinishSkill;
        // Draw-pile peeks temporarily remove physical IDs; unwind must return every unconsumed ID.
        const auto restore = qScopeGuard([&] {
            room->clearAG(ctx.invoker);
            QList<int> remaining;
            for (int id : cards)
                if (!room->getCardOwner(id) && room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) remaining << id;
            if (!remaining.isEmpty()) room->returnToTopDrawPile(remaining);
        });
        LogMessage log;
        log.type = "$ViewDrawPile";
        log.from = ctx.invoker;
        log.card_str = ListI2S(cards).join("+");
        room->sendLog(log, ctx.invoker);
        QList<int> left = cards, hearts, disabled, chosen;
        for (int id : cards) {
            if (Sanguosha->getCard(id)->getSuit() == Card::Heart) hearts << id;
            else disabled << id;
        }
        while (ctx.invoker->isAlive() && !hearts.isEmpty()) {
            room->fillAG(left, ctx.invoker, disabled);
            const int id = room->askForAG(ctx.invoker, hearts, true, objectName());
            room->clearAG(ctx.invoker);
            if (!hearts.contains(id)) break;
            hearts.removeOne(id);
            left.removeOne(id);
            chosen << id;
        }
        if (!chosen.isEmpty()) {
            DummyCard obtained(chosen);
            room->obtainCard(ctx.invoker, &obtained);
            for (int id : chosen) if (ctx.invoker->handCards().contains(id)) room->showCard(ctx.invoker, id);
        }
        if (ctx.invoker->isAlive() && !left.isEmpty()) room->askForGuanxing(ctx.invoker, left, Room::GuanxingUpOnly);
        return FinishSkill;
    }
};
class Quanji : public TriggerSkillV2
{
public:
    Quanji() : TriggerSkillV2("quanji") { events << Damaged; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const int points = data.value<DamageStruct>().damage;
        return player && player->isAlive() && player->hasSkill(this) && points > 0
            ? TriggerList{{player, {objectName() + "*" + QString::number(points)}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->askForSkillInvoke(this, QVariant(), false); }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        room->broadcastSkillInvoke(objectName());
        ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        if (!ctx.owner->isAlive() || ctx.owner->isKongcheng()) return false;
        const Card *chosen = room->askForExchange(ctx.owner, objectName(), 1, 1, false, "QuanjiPush");
        if (!chosen) return false;
        const int id = chosen->getEffectiveId();
        if (ctx.owner->handCards().contains(id)) ctx.owner->addToPile("power", id);
        return false;
    }
};

class QuanjiKeep : public MaxCardsSkillV2
{
public:
    QuanjiKeep() : MaxCardsSkillV2("#quanji") { frequency = Frequent; }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.holder ? CorrectSkillResult::useAmount(ctx.holder->getPile("power").size() * ctx.currentAmount)
            : CorrectSkillResult::noEffect();
    }
};
class Zili : public TriggerSkillV2
{
public:
    Zili() : TriggerSkillV2("zili") { events << EventSkillInvoking << EventPhaseStart; global = true; frequency = Wake; }
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
        room->setPlayerMark(ctx.owner, objectName(), 1);
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
    LimitScope getLimitScope() const override { return Limit_Game; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseStart) return {};
        return player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Start
            && (player->getPile("power").size() >= 3 || player->canWake(objectName()))
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
        if (!target) return false;
        if (target->getPile("power").size() >= 3) {
            LogMessage log;
            log.type = "#ZiliWake";
            log.from = target;
            log.arg = QString::number(target->getPile("power").size());
            log.arg2 = objectName();
            room->sendLog(log);
        }
        room->doSuperLightbox(target, objectName());
        if (!room->changeMaxHpForAwakenSkill(target, -1, objectName()) || !target->isAlive()) return false;
        if (target->isWounded() && room->askForChoice(target, objectName(), "recover+draw") == "recover") {
            RecoverStruct recover(objectName(), target);
            recover.recover = getEffectiveAmount(ctx);
            room->recover(target, recover);
        } else target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        if (!target->isAlive()) return false;
        // Awakening creates an independent permanent grant; it outlives the awakened source.
        const int id = room->acquireSkillFromEffect(target, "paiyi", ctx);
        if (id > 0) target->setSkillInstanceStateValue("paiyi", id, "zili_origin",
            QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
                {"instance", ctx.sourceRef.key.instanceID}});
        return false;
    }
};
PaiyiCard::PaiyiCard()
{
    setSkillName("paiyi");
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool PaiyiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}

void PaiyiCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *zhonghui = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = zhonghui->getRoom();
    QList<int> powers = zhonghui->getPile("power");
    if (powers.isEmpty()) return;

    room->broadcastSkillInvoke("paiyi", target == zhonghui ? 1 : 2);

    int card_id = subcards.first();

    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, zhonghui->objectName(), target->objectName(), "paiyi", "");
    room->throwCard(Sanguosha->getCard(card_id), reason, nullptr);
    room->drawCards(target, 2, "paiyi");
    if (target->getHandcardNum() > zhonghui->getHandcardNum())
        room->damage(DamageStruct("paiyi", zhonghui, target));
}

class Paiyi : public ViewAsSkillV2
{
public:
    Paiyi() : ViewAsSkillV2("paiyi", 1)
    {
        expand_pile = "power";
        setPhaseName("Play");
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->getPile("power").isEmpty();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && request.selectedCardIds.isEmpty()
            && request.initiator->getPile("power").contains(candidate->getEffectiveId());
    }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "PaiyiCard"; }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return candidate && candidate->isAlive() && selected.isEmpty();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.size() == 1;
    }
    bool pay(Room *room, SkillContext &, const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.size() != 1) return false;
        ServerPlayer *payer = room->findPlayerByObjectName(request.initiator->objectName());
        if (!payer) return false;
        const int id = request.selectedCardIds.first();
        if (!payer->getPile("power").contains(id)) return false;
        // Pile removal is an activation cost, before any recipient can intercept the draw.
        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, payer->objectName(), objectName(), "");
        room->throwCard(Sanguosha->getCard(id), reason, nullptr);
        return !payer->getPile("power").contains(id);
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target) return ContinueEffects;
        Room *room = source->getRoom();
        room->broadcastSkillInvoke(objectName(), target == source ? 1 : 2);
        const int amount = getEffectiveAmount(ctx);
        target->drawCards(2 * amount, objectName());
        if (target->isAlive() && target->getHandcardNum() > source->getHandcardNum() && amount > 0)
            room->damage(DamageStruct(objectName(), source, target, amount));
        return ContinueEffects;
    }
};

class Jueqing : public TriggerSkillV2
{
public:
    Jueqing() : TriggerSkillV2("jueqing") { frequency = Compulsory; events << Predamage; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(this) && damage.from == player && damage.to
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.to) return false;
        ctx.targets = {damage.to};
        ctx.extra_data = damage.damage;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        // Cancel the damage even if a target hook prevents its replacement HP loss.
        for (ServerPlayer *target : ctx.targets) skillEffect(event, room, ctx.owner, ctx, target);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive())
            room->loseHp(HpLostStruct(target, ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName(), ctx.owner));
        return false;
    }
};
Shangshi::Shangshi() : TriggerSkillV2("shangshi")
{
    events << HpChanged << MaxHpChanged << CardsMoveOneTime;
    frequency = Frequent;
}

int Shangshi::getMaxLostHp(ServerPlayer *player) const
{
    return qMin(qMin(player->getLostHp(), 2), player->getMaxHp());
}

TriggerList Shangshi::triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const
{
    if (!player || !player->isAlive() || !player->hasSkill(this)
        || player->getHandcardNum() >= getMaxLostHp(player)) return {};
    if (event == CardsMoveOneTime) {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!(move.from == player && move.from_places.contains(Player::PlaceHand))
            && !(move.to == player && move.to_place == Player::PlaceHand)) return {};
    } else if (player->getPhase() == Player::Discard) return {};
    return {{player, {objectName()}}};
}

bool Shangshi::cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const
{
    if (!ctx.owner || ctx.owner->getHandcardNum() >= getMaxLostHp(ctx.owner)
        || !ctx.owner->askForSkillInvoke(this)) return false;
    ctx.targets = {ctx.owner};
    return true;
}

bool Shangshi::effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const
{
    if (!target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
    const int count = getMaxLostHp(target) - target->getHandcardNum();
    if (count <= 0) return false;
    int sound = qsanRandomBounded(2) + 1;
    if (target->isJieGeneral()) sound += 2;
    room->broadcastSkillInvoke("shangshi", sound);
    target->drawCards(count * getEffectiveAmount(ctx), objectName());
    return false;
}

OLSanyaoCard::OLSanyaoCard()
{
    setSkillName("olsanyao");
}

bool OLSanyaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty()) return false;
    int max = 0;
    if (user_string == "hp") {
        foreach (const Player *p, Self->getAliveSiblings(true)) {
            if (max < p->getHp())
                max = p->getHp();
        }
        return to_select->getHp() == max;
    } else if (user_string == "hand") {
        foreach (const Player *p, Self->getAliveSiblings(true)) {
            if (max < p->getHandcardNum())
                max = p->getHandcardNum();
        }
        return to_select->getHandcardNum() == max;
    }
    return false;
}

void OLSanyaoCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->addPlayerMark(effect.from, "olsanyao_tiansuan_remove_" + user_string + "-PlayClear");
    room->damage(DamageStruct("olsanyao", effect.from, effect.to));
}

class OLSanyaoVS : public ViewAsSkillV2
{
public:
    OLSanyaoVS() : ViewAsSkillV2("olsanyao", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }
    QString historyKey(const ActiveSkillRequest &) const override { return "OLSanyaoCard"; }
    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::tiansuan(objectName(), "hp,hand"); }
    bool modeAvailable(const ActiveSkillRequest &request, const QString &mode) const
    {
        return request.initiator && request.activationRef.isValid() && (mode == "hp" || mode == "hand")
            && !request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
                request.activationRef.key.instanceID, "used_modes").toStringList().contains(mode);
    }
    SkillDeclarationReason declarationReason(const ActiveSkillRequest &request, const QString &mode, const Card *) const override
    { return modeAvailable(request, mode) ? SkillDeclarationReason::None : SkillDeclarationReason::CandidateUnavailable; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "he")
            && (modeAvailable(request, "hp") || modeAvailable(request, "hand"));
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && request.selectedCardIds.isEmpty() && request.initiator->hasCard(card)
            && !request.initiator->isJilei(card);
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        if (!target || !target->isAlive() || !selected.isEmpty() || !modeAvailable(request, request.userString)) return false;
        const int value = request.userString == "hp" ? target->getHp() : target->getHandcardNum();
        for (const Player *other : request.initiator->getAliveSiblings(true))
            if ((request.userString == "hp" ? other->getHp() : other->getHandcardNum()) > value) return false;
        return true;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    { return modeAvailable(request, request.userString) ? ViewAsSkillV2::createCard(request) : nullptr; }
    static void commitAccepted(SkillContext &ctx)
    {
        if (!ctx.initiator || !ctx.use_card || !ctx.activationRef.isValid() || ctx.extra_data.toBool()) return;
        const QString mode = (qobject_cast<const SkillCard *>(ctx.use_card) ? qobject_cast<const SkillCard *>(ctx.use_card)->getUserString() : QString());
        if (mode != "hp" && mode != "hand") return;
        const auto &key = ctx.activationRef.key;
        QStringList modes = ctx.initiator->getSkillInstanceStateValue(key.skillName, key.instanceID, "used_modes").toStringList();
        if (!modes.contains(mode)) modes << mode;
        ctx.initiator->setSkillInstanceStateValue(key.skillName, key.instanceID, "used_modes", modes);
        ctx.extra_data = true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || !modeAvailable(request, request.userString)) return false;
        ActiveSkillRequest current = request; current.selectedCardIds.clear();
        if (!canSelectCard(current, Sanguosha->getCard(request.selectedCardIds.first()))) return false;
        if (!ViewAsSkillV2::pay(room, ctx, request)) return false;
        commitAccepted(ctx);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive() && ctx.initiator && getEffectiveAmount(ctx) > 0)
            target->getRoom()->damage(DamageStruct(objectName(), ctx.initiator, target, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }
};

class OLSanyao : public TriggerSkillV2
{
public:
    OLSanyao() : TriggerSkillV2("olsanyao")
    { view_as_skill = new OLSanyaoVS; events << EventSkillInvoking << EventPhaseChanging; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            SkillContext ctx = data.value<SkillContext>();
            if (ctx.activationRef.key.skillName == objectName() && ctx.use_card && ctx.use_card->getTypeId() == Card::TypeSkill) {
                // Mode use is committed on acceptance, including waived payment and canceled effects.
                OLSanyaoVS::commitAccepted(ctx);
                data = QVariant::fromValue(ctx);
            }
        } else if (player && data.value<PhaseChangeStruct>().from == Player::Play) {
            for (const SkillInstance &instance : player->getSkillInstances()) if (instance.skillName == objectName())
                player->removeSkillInstanceStateValue(instance.skillName, instance.instanceID, "used_modes");
        }
        return true;
    }
};

class YjcmZhiman : public TriggerSkillV2
{
public:
    YjcmZhiman(const QString &name, const QString &zones, bool excludeSelf) : TriggerSkillV2(name), zones(zones), excludeSelf(excludeSelf)
    { events << DamageCaused; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(this) && damage.from == player && damage.to && damage.to->isAlive()
            && (!excludeSelf || player != damage.to) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
        if (!target || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target), false)) return false;
        ctx.targets = {target}; ctx.manual_effect = true;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        bool prevented = false;
        for (ServerPlayer *target : ctx.targets) {
            SkillContext prevent = ctx; prevent.choice = "prevent"; prevent.extra_data = false;
            skillEffect(event, room, ctx.owner, prevent, target);
            if (!prevent.extra_data.toBool()) continue;
            prevented = true;
            LogMessage log; log.type = "#Yishi"; log.from = ctx.owner; log.arg = objectName(); log.to = {target}; room->sendLog(log);
            for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive(); ++i) {
                SkillContext select = ctx; select.choice = "select"; select.extra_data = -1;
                skillEffect(event, room, ctx.owner, select, target);
                const int id = select.extra_data.toInt(); if (id < 0) break;
                SkillContext gain = ctx; gain.choice = "gain";
                gain.extra_data = QVariantMap{{"id", id}, {"from", target->objectName()}};
                skillEffect(event, room, ctx.owner, gain, ctx.owner);
            }
        }
        return prevented;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "prevent") { ctx.extra_data = true; return false; }
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        if (ctx.choice == "select") {
            if (!target->getCards(zones).isEmpty()) {
                const int id = room->askForCardChosen(ctx.owner, target, zones, objectName());
                if (id >= 0 && target->getCards(zones).contains(Sanguosha->getCard(id)) && !Sanguosha->getCard(id)->hasFlag("using")) ctx.extra_data = id;
            }
        } else {
            const QVariantMap selected = ctx.extra_data.toMap();
            ServerPlayer *from = room->findPlayerByObjectName(selected.value("from").toString());
            const int id = selected.value("id", -1).toInt();
            if (from && id >= 0 && from->getCards(zones).contains(Sanguosha->getCard(id)) && !Sanguosha->getCard(id)->hasFlag("using"))
                room->obtainCard(target, Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, ctx.owner->objectName(), objectName(), ""));
        }
        return false;
    }
private:
    QString zones;
    bool excludeSelf;
};

class OLZhiman : public YjcmZhiman
{
public:
    OLZhiman() : YjcmZhiman("olzhiman", "hej", true) {}
};

class NosEnyuan : public TriggerSkillV2
{
public:
    NosEnyuan() : TriggerSkillV2("nosenyuan") { events << HpRecover << Damaged; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(this)) return {};
        ServerPlayer *other = event == HpRecover ? data.value<RecoverStruct>().who : data.value<DamageStruct>().from;
        return other && other != player && other->isAlive() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        ServerPlayer *other = event == HpRecover ? ctx.original_data->value<RecoverStruct>().who : ctx.original_data->value<DamageStruct>().from;
        if (!other) return false;
        ctx.targets = {other};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, this);
        if (event == HpRecover) target->drawCards(ctx.original_data->value<RecoverStruct>().recover * getEffectiveAmount(ctx), objectName());
        else {
            const Card *card = room->askForCard(target, ".|heart|.|hand", "@nosenyuan-heart", *ctx.original_data, Card::MethodNone);
            // This optional transfer is an effect, so validate its material after the prompt.
            if (card && ctx.owner->isAlive() && room->getCardOwner(card->getEffectiveId()) == target
                && room->getCardPlace(card->getEffectiveId()) == Player::PlaceHand && !card->hasFlag("using")) room->obtainCard(ctx.owner, card);
            else room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.owner));
        }
        return false;
    }
};
NosXuanhuoCard::NosXuanhuoCard()
{
    setSkillName("nosxuanhuo");
    will_throw = false;
    handling_method = Card::MethodNone;
}

void NosXuanhuoCard::onEffect(CardEffectStruct &effect) const
{
    effect.to->obtainCard(this);

    Room *room = effect.from->getRoom();
    int card_id = room->askForCardChosen(effect.from, effect.to, "he", "nosxuanhuo");
    CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName());
    room->obtainCard(effect.from, Sanguosha->getCard(card_id), reason, room->getCardPlace(card_id) != Player::PlaceHand);

    QList<ServerPlayer *> targets = room->getOtherPlayers(effect.to);
    ServerPlayer *target = room->askForPlayerChosen(effect.from, targets, "nosxuanhuo", "@nosxuanhuo-give:" + effect.to->objectName());
    if (target != effect.from) {
        CardMoveReason reason2(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), target->objectName(), "nosxuanhuo", "");
        room->obtainCard(target, Sanguosha->getCard(card_id), reason2, false);
    }
}

class NosXuanhuo : public ViewAsSkillV2
{
public:
    NosXuanhuo() : ViewAsSkillV2("nosxuanhuo", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "NosXuanhuoCard"; }
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isKongcheng(); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && card->getSuit() == Card::Heart && request.selectedCardIds.isEmpty()
            && request.initiator->handCards().contains(card->getEffectiveId());
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
        const Player *target) const override
    { return target && target->isAlive() && target != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest current = request; current.selectedCardIds.clear();
        return canSelectCard(current, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.initiator || !ctx.use_card || ctx.use_card->subcardsLength() != 1) return FinishSkill;
        Room *room = ctx.initiator->getRoom();
        for (ServerPlayer *donor : ctx.targets) {
            SkillContext gift = ctx; gift.choice = "gift";
            gift.extra_data = QVariantMap{{"id", ctx.use_card->getSubcards().first()}, {"done", false}};
            skillEffect(gift, donor);
            if (!gift.extra_data.toMap().value("done").toBool()) continue;
            for (int n = 0; n < getEffectiveAmount(ctx) && ctx.initiator->isAlive() && donor->isAlive(); ++n) {
                SkillContext take = ctx; take.choice = "choose"; take.extra_data = -1;
                skillEffect(take, donor);
                const int id = take.extra_data.toInt();
                if (id < 0) break;
                SkillContext gain = ctx; gain.choice = "gain";
                gain.extra_data = QVariantMap{{"id", id}, {"donor", donor->objectName()}};
                skillEffect(gain, ctx.initiator);
                // A redirected or subsequently moved extraction cannot become a gift of somebody else's card.
                if (!gain.extra_data.toMap().value("done").toBool() || !ctx.initiator->isAlive() || room->getCardOwner(id) != ctx.initiator
                    || room->getCardPlace(id) != Player::PlaceHand || Sanguosha->getCard(id)->hasFlag("using")) continue;
                ServerPlayer *recipient = room->askForPlayerChosen(ctx.initiator, room->getOtherPlayers(donor),
                    objectName(), "@nosxuanhuo-give:" + donor->objectName());
                if (recipient && recipient != ctx.initiator) {
                    SkillContext pass = ctx; pass.choice = "gift"; pass.extra_data = QVariantMap{{"id", id}};
                    skillEffect(pass, recipient);
                }
            }
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.initiator || !ctx.initiator->isAlive()
            || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice == "choose") {
            if (target->isNude()) return ContinueEffects;
            const int id = room->askForCardChosen(ctx.initiator, target, "he", objectName());
            if (id >= 0 && room->getCardOwner(id) == target
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                && !Sanguosha->getCard(id)->hasFlag("using")) ctx.extra_data = id;
            return ContinueEffects;
        }
        QVariantMap details = ctx.extra_data.toMap();
        const int id = details.value("id", -1).toInt();
        ServerPlayer *from = ctx.choice == "gain" ? room->findPlayerByObjectName(details.value("donor").toString()) : ctx.initiator;
        if (id < 0 || !from || room->getCardOwner(id) != from || Sanguosha->getCard(id)->hasFlag("using")
            || (room->getCardPlace(id) != Player::PlaceHand && (ctx.choice != "gain" || room->getCardPlace(id) != Player::PlaceEquip)))
            return ContinueEffects;
        CardMoveReason reason(ctx.choice == "gain" ? CardMoveReason::S_REASON_EXTRACTION : CardMoveReason::S_REASON_GIVE,
            ctx.initiator->objectName(), target->objectName(), objectName(), "");
        const qint64 cause = room->currentHistoryEventId();
        const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
        room->obtainCard(target, Sanguosha->getCard(id), reason, ctx.choice == "gain" && room->getCardPlace(id) != Player::PlaceHand);
        details["done"] = transferred(room, cause, before, from, target, id);
        ctx.extra_data = details;
        return ContinueEffects;
    }
private:
    static bool transferred(Room *room, qint64 cause, const QVariantMap &before, ServerPlayer *from, ServerPlayer *to, int id)
    {
        if (cause <= 0 || before.contains("error") || !before.value("complete").toBool() || !before.contains("watermark")) return false;
        QVariantMap filter{{"from", from->objectName()}, {"after", before.value("watermark")}};
        bool found = false;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return false;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
                if (move.value("card_id", -1).toInt() == id && move.value("to").toString() == to->objectName()
                    && move.value("to_place").toInt() == int(Player::PlaceHand)
                    && room->historyEvent(fact.value("event_id").toLongLong()).value("parent_id").toLongLong() == cause) found = true;
            }
            if (!page.value("has_more").toBool()) return found;
            filter.insert("after", page.value("next_after"));
            filter.insert("watermark", page.value("watermark"));
        }
    }
};

class NosXuanfeng : public TriggerSkillV2
{
public:
    NosXuanfeng() : TriggerSkillV2("nosxuanfeng") { events << CardsMoveOneTime; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        return player && player->isAlive() && player->hasSkill(this) && move.from == player && move.from_places.contains(Player::PlaceEquip)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        Slash preview(Card::NoSuit, 0);
        preview.setSkillName(objectName());
        QList<ServerPlayer *> slashes, damages;
        for (ServerPlayer *target : room->getOtherPlayers(ctx.owner)) {
            if (ctx.owner->canSlash(target, &preview, false) && !ctx.owner->isCardLimited(&preview, Card::MethodUse)) slashes << target;
            if (ctx.owner->distanceTo(target) <= 1) damages << target;
        }
        QStringList choices{"nothing"};
        if (!slashes.isEmpty()) choices << "slash";
        if (!damages.isEmpty()) choices << "damage";
        ctx.choice = room->askForChoice(ctx.owner, objectName(), choices.join('+'));
        ServerPlayer *target = nullptr;
        if (ctx.choice == "slash") target = room->askForPlayerChosen(ctx.owner, slashes, "nosxuanfeng_slash", "@dummy-slash");
        else if (ctx.choice == "damage") target = room->askForPlayerChosen(ctx.owner, damages, "nosxuanfeng_damage", "@nosxuanfeng-damage");
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive() || !target || !target->isAlive()) return false;
        if (ctx.choice == "damage") room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        else for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive(); ++i) {
            Slash slash(Card::NoSuit, 0);
            slash.setSkillName(objectName());
            if (ctx.owner->canSlash(target, &slash, false)) room->useCardFromSkillEffect(CardUseStruct(&slash, ctx.owner, target), ctx);
        }
        return false;
    }
};
class NosWuyan : public TriggerSkillV2
{
public:
    NosWuyan() : TriggerSkillV2("noswuyan") { events << CardEffected; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!effect.card || !effect.card->isNDTrick() || !effect.from || !effect.to || effect.from == effect.to) return {};
        ServerPlayer *holder = effect.from->hasSkill(this) ? effect.from : effect.to->hasSkill(this) ? effect.to : nullptr;
        return holder ? TriggerList{{holder, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        SkillContext prevent = ctx; prevent.extra_data = false;
        skillEffect(event, room, ctx.owner, prevent, ctx.original_data->value<CardEffectStruct>().to);
        return prevent.extra_data.toBool();
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || getEffectiveAmount(ctx) <= 0) return false;
        const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        LogMessage log;
        log.type = ctx.owner == effect.from ? "#WuyanBaD" : "#WuyanGooD";
        log.from = ctx.owner; log.to << (ctx.owner == effect.from ? effect.to : effect.from);
        log.arg = effect.card->objectName(); log.arg2 = objectName(); room->sendLog(log);
        ctx.extra_data = true;
        return false;
    }
};
NosJujianCard::NosJujianCard()
{
    setSkillName("nosjujian");
}

void NosJujianCard::onEffect(CardEffectStruct &effect) const
{
    int n = subcardsLength();
    effect.to->drawCards(n, "nosjujian");
    Room *room = effect.from->getRoom();

    if (effect.from->isAlive() && n == 3) {
        QSet<Card::CardType> types;
        foreach(int card_id, effect.card->getSubcards())
            types << Sanguosha->getCard(card_id)->getTypeId();

        if (types.size() == 1) {
            LogMessage log;
            log.type = "#JujianRecover";
            log.from = effect.from;
            const Card *card = Sanguosha->getCard(subcards.first());
            log.arg = card->getType();
            room->sendLog(log);
            room->recover(effect.from, RecoverStruct("nosjujian", effect.from));
        }
    }
}

class NosJujian : public ViewAsSkillV2
{
public:
    NosJujian() : ViewAsSkillV2("nosjujian", 3) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->canDiscard(request.initiator, "he"); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.size() < 3 && !request.selectedCardIds.contains(card->getEffectiveId())
            && !card->hasFlag("using") && !request.initiator->isJilei(card)
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId()));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return !request.selectedCardIds.isEmpty() && request.selectedCardIds.size() <= 3; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return candidate && candidate->isAlive() && candidate != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "NosJujianCard"; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card = ViewAsSkillV2::createCard(request);
        QSet<int> types;
        for (int id : request.selectedCardIds) types << Sanguosha->getCard(id)->getTypeId();
        if (card) card->setTag("NosJujianRecover", request.selectedCardIds.size() == 3 && types.size() == 1);
        return card;
    }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        QSet<int> types;
        for (int id : request.selectedCardIds) types << Sanguosha->getCard(id)->getTypeId();
        // Freeze material categories before the pay move resets converted card faces.
        ctx.extra_data = request.selectedCardIds.size() == 3 && types.size() == 1;
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        if (ctx.choice == "recover") {
            RecoverStruct recovery(objectName(), ctx.invoker);
            recovery.recover = getEffectiveAmount(ctx);
            target->getRoom()->recover(target, recovery);
            return ContinueEffects;
        }
        if (!ctx.use_card) return ContinueEffects;
        if (!ctx.extra_data.isValid()) ctx.extra_data = ctx.use_card->getTag("NosJujianRecover");
        target->drawCards(ctx.use_card->subcardsLength() * getEffectiveAmount(ctx), objectName());
        if (ctx.extra_data.toBool() && ctx.invoker && ctx.invoker->isAlive()) {
            SkillContext recovery = ctx;
            recovery.choice = "recover";
            skillEffect(recovery, ctx.invoker);
        }
        return ContinueEffects;
    }
};
class NosShangshi : public Shangshi
{
public:
    NosShangshi() : Shangshi()
    {
        setObjectName("nosshangshi");
    }

    int getMaxLostHp(ServerPlayer *zhangchunhua) const
    {
        return qMin(zhangchunhua->getLostHp(), zhangchunhua->getMaxHp());
    }
};

class OLLuoying : public Luoying
{
public:
    OLLuoying() : Luoying("olluoying", true) { }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
};
JieyueCard::JieyueCard()
{
    setSkillName("jieyue");
}

void JieyueCard::onEffect(CardEffectStruct &effect) const
{
    if (!effect.to->isNude()) {
        Room *room = effect.to->getRoom();
        const Card *card = room->askForExchange(effect.to, "jieyue", 1, 1, true, QString("@jieyue_put:%1").arg(effect.from->objectName()), true);

        if (card != nullptr)
            effect.from->addToPile("jieyue_pile", card);
        else if (effect.from->canDiscard(effect.to, "he")) {
            int id = room->askForCardChosen(effect.from, effect.to, "he", objectName(), false, Card::MethodDiscard);
            room->throwCard(id, effect.to, effect.from);
        }
    }
}

class JieyueVS : public ViewAsSkillV2
{
public:
    JieyueVS() : ViewAsSkillV2("jieyue", 1) { setResponseOrUse(true); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return false;
        if (request.pattern == "@@jieyue") return request.activationRef.isValid()
            && request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
                request.activationRef.key.instanceID, "prompt").toBool();
        return !request.initiator->getPile("jieyue_pile").isEmpty()
            && (request.pattern == "jink" || request.pattern == "nullification");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->isVirtualCard() || card->hasFlag("using") || !request.selectedCardIds.isEmpty()) return false;
        const bool hand = request.initiator->handCards().contains(card->getEffectiveId());
        if (request.pattern == "@@jieyue") return hand && !request.initiator->isJilei(card);
        if (!hand && !request.initiator->getHandPile().contains(card->getEffectiveId())) return false;
        return request.pattern == "jink" ? card->isRed() : request.pattern == "nullification" && card->isBlack();
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return nullptr;
        if (request.pattern == "@@jieyue") return ViewAsSkillV2::createCard(request);
        if (request.pattern != "jink" && request.pattern != "nullification") return nullptr;
        Card *card = Sanguosha->cloneCard(request.pattern, Card::SuitToBeDecided, 0);
        if (card) { card->addSubcards(request.selectedCardIds); card->setSkillName(objectName()); }
        return card;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return request.pattern == "@@jieyue" && target && target->isAlive() && target != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    { return request.pattern == "@@jieyue" && selected.size() == 1; }
    QString historyKey(const ActiveSkillRequest &request) const override
    { return request.pattern == "@@jieyue" ? "JieyueCard" : request.pattern == "jink" ? "Jink" : "Nullification"; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest current = request; current.selectedCardIds.clear();
        if (!canSelectCard(current, Sanguosha->getCard(request.selectedCardIds.first()))) return false;
        return ViewAsSkillV2::pay(room, ctx, request);
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.initiator || !ctx.initiator->isAlive() || !target || !target->isAlive() || getEffectiveAmount(ctx) <= 0)
            return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice == "store") {
            const QVariantMap selected = ctx.extra_data.toMap();
            const int id = selected.value("id", -1).toInt();
            ServerPlayer *from = room->findPlayerByObjectName(selected.value("from").toString());
            if (!from || id < 0 || room->getCardOwner(id) != from
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
            const int serial = room->getTag("JieyueReceiptSerial").toInt() + 1;
            room->setTag("JieyueReceiptSerial", serial);
            QVariantList receipts = target->getTag("JieyueEffects").toList();
            receipts << QVariantMap{{"serial", serial}, {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
                {"instance", ctx.sourceRef.key.instanceID}, {"id", id}, {"amount", getEffectiveAmount(ctx)}};
            target->setTag("JieyueEffects", receipts);
            target->addToPile("jieyue_pile", id);
            return ContinueEffects;
        }
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive() && ctx.initiator->isAlive() && !target->isNude(); ++i) {
            const Card *choice = room->askForExchange(target, objectName(), 1, 1, true,
                "@jieyue_put:" + ctx.initiator->objectName(), true);
            if (choice && choice->subcardsLength() == 1) {
                SkillContext store = ctx; store.choice = "store";
                store.extra_data = QVariantMap{{"id", choice->getSubcards().first()}, {"from", target->objectName()}};
                skillEffect(store, ctx.initiator);
            } else if (ctx.initiator->canDiscard(target, "he")) {
                const int id = room->askForCardChosen(ctx.initiator, target, "he", objectName(), false, Card::MethodDiscard);
                if (id >= 0 && room->getCardOwner(id) == target && !Sanguosha->getCard(id)->hasFlag("using")
                    && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                    && ctx.initiator->canDiscard(target, id)) room->throwCard(id, target, ctx.initiator);
            }
        }
        return ContinueEffects;
    }
};

class Jieyue : public TriggerSkillV2
{
public:
    Jieyue() : TriggerSkillV2("jieyue") { events << EventPhaseStart << CardsMoveOneTime; global = true; view_as_skill = new JieyueVS; }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || player->getPhase() != Player::Start) return false;
        if (!player->isAlive()) return true;
        for (const QVariant &entry : player->getTag("JieyueEffects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (!player->getPile("jieyue_pile").contains(receipt.value("id", -1).toInt())) continue;
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(),
                SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            if (!ctx.sourceRef.isValid()) continue;
            ctx.instanceID = receipt.value("serial").toInt(); ctx.extra_data = receipt;
            ctx.current_event = event; ctx.original_data = &data; ctx.choice = "return";
            ctx.setModifiedAmount(receipt.value("amount").toInt()); contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.choice == "return" ? ctx.owner && ctx.owner->isAlive()
            && ctx.owner->getTag("JieyueEffects").toList().contains(ctx.extra_data)
            : TriggerSkillV2::isSourceAvailable(room, ctx);
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardsMoveOneTime || !player) return true;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player) return true;
        QSet<int> departed;
        for (int i = 0; i < move.card_ids.size(); ++i)
            if (move.from_places.value(i) == Player::PlaceSpecial && move.from_pile_names.value(i) == "jieyue_pile")
                departed.insert(move.card_ids.at(i));
        if (departed.isEmpty()) return true;
        QVariantList kept;
        for (const QVariant &entry : player->getTag("JieyueEffects").toList())
            if (!departed.contains(entry.toMap().value("id", -1).toInt())) kept << entry;
        player->setTag("JieyueEffects", kept);
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Finish && !player->isKongcheng()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (ctx.choice == "return") ctx.targets = {ctx.owner}; else ctx.manual_effect = true; return ctx.owner != nullptr; }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice == "return" || getEffectiveAmount(ctx) <= 0) return false;
        Room::AcceptedViewAsEffectScope prompt(room, ctx.owner, objectName(), ctx);
        if (!prompt.isValid()) return false;
        const SkillInstanceRef ref = prompt.activationRef();
        ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "prompt", true);
        RoomState *state = Sanguosha->currentRoomState();
        const auto reason = state->getCurrentCardUseReason(); const QString pattern = state->getCurrentCardUsePattern();
        const auto restore = qScopeGuard([=] { state->setCurrentCardUseReason(reason); state->setCurrentCardUsePattern(pattern); });
        room->askForUseCard(ctx.owner, "@@jieyue", "@jieyue", -1, Card::MethodDiscard, false);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice != "return" || !target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        QVariantList receipts = target->getTag("JieyueEffects").toList(); receipts.removeOne(ctx.extra_data);
        target->setTag("JieyueEffects", receipts);
        const int id = ctx.extra_data.toMap().value("id", -1).toInt();
        if (target->getPile("jieyue_pile").contains(id)) room->obtainCard(target, id, false);
        return false;
    }
};

SanyaoCard::SanyaoCard()
{
    setSkillName("sanyao");
}

bool SanyaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty()) return false;
    QList<const Player *> players = Self->getAliveSiblings();
    players << Self;
    int max = -1000;
    foreach (const Player *p, players) {
        if (max < p->getHp())
            max = p->getHp();
    }
    return to_select->getHp() == max;
}

void SanyaoCard::onEffect(CardEffectStruct &effect) const
{
    effect.from->getRoom()->damage(DamageStruct("sanyao", effect.from, effect.to));
}

class Sanyao : public ViewAsSkillV2
{
public:
    Sanyao() : ViewAsSkillV2("sanyao", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->canDiscard(request.initiator, "he"); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty() && !card->hasFlag("using") && !request.initiator->isJilei(card)
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId()));
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    {
        if (!request.initiator || !candidate || !candidate->isAlive() || !selected.isEmpty() || request.initiator->getHp() > candidate->getHp()) return false;
        for (const Player *other : request.initiator->getAliveSiblings()) if (other->getHp() > candidate->getHp()) return false;
        return true;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "SanyaoCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive()) target->getRoom()->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }
};
class Zhiman : public YjcmZhiman
{
public:
    Zhiman() : YjcmZhiman("zhiman", "ej", false) {}
};

class OlPojun : public TriggerSkillV2
{
public:
    OlPojun() : TriggerSkillV2("olpojun") { events << TargetSpecified << EventPhaseStart << Death << CardsMoveOneTime; global = true; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event == TargetSpecified) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!player || !player->isAlive() || use.from != player || !use.card || !use.card->isKindOf("Slash")
                || player->getPhase() != Player::Play) return true;
            for (int id : player->getValidSkillInstanceIds(objectName())) for (ServerPlayer *target : use.to) {
                if (!target->isAlive() || target->isNude() || target->getHp() <= 0) continue;
                SkillContext ctx;
                ctx.owner = player; ctx.invoker = player; ctx.instanceID = id;
                ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
                ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
                if (!ctx.sourceRef.isValid()) continue;
                ctx.skill_name = objectName() + "->" + target->objectName();
                ctx.amount = room->getSkillInstanceAmount(ctx.activationRef);
                ctx.targets = {target}; ctx.preferredTarget = target; ctx.preferredTargetSeat = target->getSeat();
                ctx.current_event = event; ctx.original_data = &data; contexts << ctx;
            }
            return true;
        }
        if ((event == EventPhaseStart && player && player->getPhase() == Player::Finish)
            || (event == Death && player && data.value<DeathStruct>().who == player)) {
            for (ServerPlayer *target : room->getAlivePlayers())
                for (const QVariant &entry : target->getTag("OlPojunEffects").toList()) {
                    const QVariantMap receipt = entry.toMap();
                    SkillContext ctx;
                    ctx.owner = room->findPlayerByObjectName(receipt.value("holder").toString(), true);
                    if (!ctx.owner) continue;
                    ctx.invoker = player; ctx.skill_name = objectName(); ctx.choice = "return";
                    ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(),
                        SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
                    if (!ctx.sourceRef.isValid()) continue;
                    ctx.instanceID = receipt.value("serial").toInt(); ctx.extra_data = receipt;
                    ctx.targets = {target}; ctx.preferredTarget = target; ctx.preferredTargetSeat = target->getSeat();
                    ctx.current_event = event; ctx.original_data = &data; ctx.is_forced = true;
                    ctx.setModifiedAmount(receipt.value("amount").toInt()); contexts << ctx;
                }
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override
    { return ctx.choice == "return" ? ctx.preferredTarget : ctx.owner; }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.choice == "return" ? ctx.preferredTarget && ctx.preferredTarget->isAlive()
            && ctx.preferredTarget->getTag("OlPojunEffects").toList().contains(ctx.extra_data)
            : TriggerSkillV2::isSourceAvailable(room, ctx);
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardsMoveOneTime || !player) return true;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player) return true;
        QSet<int> departed;
        for (int i = 0; i < move.card_ids.size(); ++i)
            if (move.from_places.value(i) == Player::PlaceSpecial && move.from_pile_names.value(i) == "olpojun") departed.insert(move.card_ids.at(i));
        if (departed.isEmpty()) return true;
        QVariantList kept;
        for (const QVariant &entry : player->getTag("OlPojunEffects").toList()) {
            QVariantMap receipt = entry.toMap(); QVariantList ids;
            for (const QVariant &id : receipt.value("ids").toList()) if (!departed.contains(id.toInt())) ids << id;
            if (!ids.isEmpty()) { receipt["ids"] = ids; kept << receipt; }
        }
        player->setTag("OlPojunEffects", kept);
        return true;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice == "return") return true;
        return ctx.owner && ctx.preferredTarget
            && room->askForSkillInvoke(ctx.owner, objectName(), QVariant::fromValue(ctx.preferredTarget));
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "return") {
            QVariantList kept = target->getTag("OlPojunEffects").toList(); kept.removeOne(ctx.extra_data);
            target->setTag("OlPojunEffects", kept);
            QList<int> ids;
            for (const QVariant &id : ctx.extra_data.toMap().value("ids").toList())
                if (target->getPile("olpojun").contains(id.toInt())) ids << id.toInt();
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards, false); }
            return false;
        }
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        const int maximum = qMin(int(target->getCards("he").size()), qMax(0, target->getHp()) * getEffectiveAmount(ctx));
        QList<int> ids;
        for (int i = 0; i < maximum && target->isAlive() && ctx.owner->isAlive(); ++i) {
            const int id = room->askForCardChosen(ctx.owner, target, "he", objectName() + "_dis", false, Card::MethodNone, ids, i > 0);
            if (id < 0) break;
            if (ids.contains(id) || room->getCardOwner(id) != target || Sanguosha->getCard(id)->hasFlag("using")
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) break;
            ids << id;
        }
        for (int id : QList<int>(ids))
            if (room->getCardOwner(id) != target || Sanguosha->getCard(id)->hasFlag("using")
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) ids.removeOne(id);
        if (ids.isEmpty()) return false;
        const int serial = room->getTag("OlPojunReceiptSerial").toInt() + 1;
        room->setTag("OlPojunReceiptSerial", serial);
        QVariantList stored; for (int id : ids) stored << id;
        QVariantList receipts = target->getTag("OlPojunEffects").toList();
        receipts << QVariantMap{{"serial", serial}, {"holder", ctx.owner->objectName()}, {"owner", ctx.sourceRef.ownerObjectName},
            {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}, {"ids", stored}, {"amount", getEffectiveAmount(ctx)}};
        // Private physical receipts retain the accepted source even if the granting instance disappears.
        target->setTag("OlPojunEffects", receipts);
        DummyCard cards(ids); target->addToPile("olpojun", &cards, false);
        return false;
    }
};

class NosZhenggong : public TriggerSkillV2
{
public:
    NosZhenggong() : TriggerSkillV2("noszhenggong") { events << Damaged; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(this)
            && damage.from && damage.from->hasEquip() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!ctx.owner || !damage.from || !damage.from->hasEquip()
            || !ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(damage.from))) return false;
        ctx.targets = {damage.from};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice != "equip") {
            for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive() && ctx.owner->isAlive() && target->hasEquip(); ++i) {
                const int id = room->askForCardChosen(ctx.owner, target, "e", objectName());
                if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceEquip
                    || Sanguosha->getCard(id)->hasFlag("using")) break;
                SkillContext gain = ctx;
                gain.choice = "equip";
                gain.extra_data = QVariantMap{{"id", id}, {"donor", target->objectName()}};
                skillEffect(event, room, player, gain, ctx.owner);
            }
            return false;
        }
        const QVariantMap selected = ctx.extra_data.toMap();
        const int id = selected.value("id", -1).toInt();
        const QString name = selected.value("donor").toString();
        ServerPlayer *donor = name.isEmpty() ? nullptr : room->findPlayerByObjectName(name, true);
        if (!donor || id < 0 || room->getCardOwner(id) != donor || room->getCardPlace(id) != Player::PlaceEquip) return false;
        const Card *card = Sanguosha->getCard(id);
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        if (!equip || card->hasFlag("using")) return false;
        const int area = equip->location();
        room->obtainCard(target, card);
        if (!target->isAlive() || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand
            || !target->hasEquipArea(area) || card->hasFlag("using")) return false;
        QList<CardsMoveStruct> moves;
        moves << CardsMoveStruct(id, target, Player::PlaceEquip,
            CardMoveReason(CardMoveReason::S_REASON_ROB, target->objectName()));
        if (target->getEquip(area))
            moves << CardsMoveStruct(target->getEquip(area)->getId(), nullptr, Player::DiscardPile,
                CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP, target->objectName()));
        room->moveCardsAtomic(moves, true);
        return false;
    }
};

class NosQuanji : public TriggerSkillV2
{
public:
    NosQuanji() : TriggerSkillV2("nosquanji") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList list;
        if (!player || !player->isAlive() || player->getPhase() != Player::RoundStart) return list;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->hasSkill(this) && owner->canPindian(player)) list[owner] << objectName();
        return list;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.invoker || !ctx.owner->canPindian(ctx.invoker)
            || !ctx.owner->askForSkillInvoke(this, ctx.invoker)) return false;
        ctx.targets = {ctx.invoker};
        ctx.manual_effect = true;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ctx.extra_data = false;
        if (!ctx.targets.isEmpty()) skillEffect(event, room, player, ctx, ctx.targets.first());
        return ctx.extra_data.toBool();
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0 || !target->isAlive() || !ctx.owner->canPindian(target)) return false;
        if (ctx.owner->pindian(target, objectName())) {
            target->skip(Player::Start);
            target->skip(Player::Judge);
            ctx.extra_data = true;
        }
        return false;
    }
};

class NosZhonghuiWake : public TriggerSkillV2
{
public:
    NosZhonghuiWake(const QString &name, bool first) : TriggerSkillV2(name), firstWake(first)
    {
        events << EventPhaseStart << EventSkillInvoking;
        global = true;
        frequency = Wake;
        waked_skills = first ? "nosyexin" : "nospaiyi";
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    QStringList usableEntries(ServerPlayer *owner, QVariant &data) const
    {
        QStringList entries;
        for (int id : owner->getValidSkillInstanceIds(objectName())) {
            SkillContext ctx;
            ctx.owner = ctx.invoker = ctx.initiator = owner;
            ctx.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(objectName(), id));
            ctx.sourceRef = owner->getRoom()->resolveSkillInstanceRootRef(ctx.activationRef);
            ctx.instanceID = id;
            ctx.skill_name = objectName() + "#" + QString::number(id);
            ctx.original_data = &data;
            if (ctx.sourceRef.isValid() && isUsable(ctx)) entries << ctx.skill_name;
        }
        return entries;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Start) return {};
        const bool ready = firstWake ? player->getEquips().size() >= 3 : player->getPile("nospower").size() >= 4;
        return ready || player->canWake(objectName()) ? TriggerList{{player, usableEntries(player, data)}} : TriggerList();
    }
    void commit(SkillContext &ctx) const
    {
        QVariantMap metadata = ctx.extra_data.toMap();
        if (metadata.value("committed").toBool()) return;
        metadata.insert("committed", true);
        ctx.extra_data = metadata;
        addUsage(ctx);
        ctx.owner->getRoom()->setPlayerMark(ctx.owner, objectName(), 1);
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return true;
        SkillContext ctx = data.value<SkillContext>();
        if (ctx.activationRef.key.skillName == objectName() && parseSkillName(ctx.skill_name) == objectName()) {
            commit(ctx);
            data = QVariant::fromValue(ctx);
        }
        return true;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !isUsable(ctx)) return false;
        ctx.targets = {ctx.owner};
        QVariantList removals;
        if (firstWake) {
            const QList<SkillInstance> instances = ctx.owner->getSkillInstances();
            for (const SkillInstance &origin : instances) {
                if (origin.key() != ctx.activationRef.key) continue;
                for (const SkillInstance &instance : instances)
                    if ((instance.skillName == "noszhenggong" || instance.skillName == "nosquanji") && sameOrigin(origin, instance))
                        removals << QVariant::fromValue(instance);
                break;
            }
        }
        ctx.extra_data = QVariantMap{{"remove", removals}};
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; commit(ctx); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        room->doSuperLightbox(target, objectName());
        if (!room->changeMaxHpForAwakenSkill(target, firstWake ? getEffectiveAmount(ctx) : -getEffectiveAmount(ctx), objectName())
            || !target->isAlive()) return false;
        if (firstWake) {
            room->recover(target, RecoverStruct(objectName(), target, getEffectiveAmount(ctx)));
            // Only the copies paired with this awakening before callbacks are retired.
            for (const QVariant &entry : ctx.extra_data.toMap().value("remove").toList()) {
                const SkillInstance frozen = entry.value<SkillInstance>();
                for (const SkillInstance &current : target->getSkillInstances())
                    if (current.key() == frozen.key() && current.source == frozen.source && current.bindHead == frozen.bindHead
                        && current.parentRef == frozen.parentRef && current.frozenSourceRef == frozen.frozenSourceRef
                        && current.grantActivationRef == frozen.grantActivationRef) {
                        room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName(current.skillName, current.instanceID), false, false);
                        break;
                    }
            }
        }
        if (target->isAlive()) room->acquireSkillFromEffect(target, firstWake ? "nosyexin" : "nospaiyi", ctx);
        return false;
    }
private:
    static bool sameOrigin(const SkillInstance &a, const SkillInstance &b)
    {
        if (a.source != b.source) return false;
        if (a.source == SourceInnate) return a.bindHead == b.bindHead;
        if (a.parentRef.isValid() && a.parentRef == b.parentRef) return true;
        if (a.grantActivationRef.isValid() || b.grantActivationRef.isValid())
            return a.grantActivationRef.isValid() && a.grantActivationRef == b.grantActivationRef
                && a.frozenSourceRef == b.frozenSourceRef;
        return a.frozenSourceRef.isValid() && a.frozenSourceRef == b.frozenSourceRef;
    }
    bool firstWake;
};

class NosBaijiang : public NosZhonghuiWake
{
public:
    NosBaijiang() : NosZhonghuiWake("nosbaijiang", true) { }
};

NosYexinCard::NosYexinCard()
{
    setSkillName("nosyexin");
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void NosYexinCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	Card*dc = new DummyCard;
	Card*dc2 = new DummyCard;
	foreach (int id, subcards) {
		if(source->getPile("nospower").contains(id))
			dc->addSubcard(id);
		else if(source->handCards().contains(id))
			dc2->addSubcard(id);
	}
	source->addToPile("nospower", dc2);
	source->obtainCard(dc);
}

class NosYexinViewAsSkill : public ViewAsSkillV2
{
public:
    NosYexinViewAsSkill() : ViewAsSkillV2("nosyexin") { expand_pile = "nospower"; setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "NosYexinCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng() && !request.initiator->getPile("nospower").isEmpty();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && !request.selectedCardIds.contains(card->getEffectiveId())
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getPile("nospower").contains(card->getEffectiveId()));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.isEmpty()) return false;
        int hand = 0, pile = 0;
        for (int id : request.selectedCardIds) {
            if (request.initiator->handCards().contains(id)) ++hand;
            else if (request.initiator->getPile("nospower").contains(id)) ++pile;
            else return false;
        }
        return hand > 0 && hand == pile;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card = ViewAsSkillV2::createCard(request);
        if (!card || !request.initiator) return card;
        QVariantList hand, pile;
        for (int id : request.selectedCardIds)
            (request.initiator->handCards().contains(id) ? hand : pile) << id;
        card->setTag("NosYexinHand", hand);
        card->setTag("NosYexinPile", pile);
        return card;
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    { return validMaterials(ctx); }
    EffectFlow effect(SkillContext &ctx) const override { return skillEffect(ctx, ctx.initiator); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target->isAlive() || getEffectiveAmount(ctx) <= 0 || !validMaterials(ctx)) return ContinueEffects;
        Room *room = target->getRoom();
        const qint64 cause = room->currentHistoryEventId();
        const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
        QList<int> hand, pile;
        for (const QVariant &id : ctx.use_card->getTag("NosYexinHand").toList()) hand << id.toInt();
        for (const QVariant &id : ctx.use_card->getTag("NosYexinPile").toList()) pile << id.toInt();
        DummyCard put(hand);
        target->addToPile("nospower", &put);
        // Preserve put-before-obtain ordering; only a committed put pays for the exchange.
        if (cause <= 0 || before.contains("error") || !before.contains("watermark")) return ContinueEffects;
        QVariantMap filter{{"from", target->objectName()}, {"to", target->objectName()},
            {"after", before.value("watermark")}, {"limit", 128}};
        QSet<int> committed;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return ContinueEffects;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), data = fact.value("data").toMap();
                const int id = data.value("card_id", -1).toInt();
                if (hand.contains(id) && data.value("from_place").toInt() == Player::PlaceHand
                    && data.value("to_place").toInt() == Player::PlaceSpecial && data.value("to_pile").toString() == "nospower"
                    && room->historyEvent(fact.value("event_id").toLongLong()).value("parent_id").toLongLong() == cause)
                    committed.insert(id);
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
        if (committed.size() != hand.size() || !target->isAlive()) return ContinueEffects;
        DummyCard obtain;
        for (int id : pile)
            if (target->getPile("nospower").contains(id) && room->getCardOwner(id) == target) obtain.addSubcard(id);
        if (obtain.subcardsLength() > 0) room->obtainCard(target, &obtain);
        return ContinueEffects;
    }
private:
    static bool validMaterials(const SkillContext &ctx)
    {
        if (!ctx.initiator || !ctx.use_card) return false;
        const QVariantList hand = ctx.use_card->getTag("NosYexinHand").toList();
        const QVariantList pile = ctx.use_card->getTag("NosYexinPile").toList();
        if (hand.isEmpty() || hand.size() != pile.size()) return false;
        for (const QVariant &entry : hand)
            if (!ctx.initiator->handCards().contains(entry.toInt()) || Sanguosha->getCard(entry.toInt())->hasFlag("using")) return false;
        for (const QVariant &entry : pile)
            if (!ctx.initiator->getPile("nospower").contains(entry.toInt()) || Sanguosha->getCard(entry.toInt())->hasFlag("using")) return false;
        return true;
    }
};

class NosYexin : public TriggerSkillV2
{
public:
    NosYexin() : TriggerSkillV2("nosyexin") { events << Damage << Damaged; view_as_skill = new NosYexinViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(this) ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->askForSkillInvoke(objectName())) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target->isAlive() && getEffectiveAmount(ctx) > 0)
            target->addToPile("nospower", room->getNCards(getEffectiveAmount(ctx), false));
        return false;
    }
    int getEffectIndex(const ServerPlayer *, const Card *) const override { return 2; }
};

NosPaiyiCard::NosPaiyiCard()
{
    setSkillName("nospaiyi");
    will_throw = false;
	mute = true;
    handling_method = Card::MethodNone;
}

bool NosPaiyiCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
	return !targets.isEmpty();
}

void NosPaiyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &tos) const
{
    const Card *c = Sanguosha->getCard(getEffectiveId());
	foreach (ServerPlayer *p, tos) {
        int index = 1;
        if (p != source) index++;
        source->peiyin("nospaiyi", index);
		QString choice = "hand_area";
		if(c->isKindOf("EquipCard")||c->isKindOf("DelayedTrick")){
			if (!source->isProhibited(p, c)){
				if (c->isKindOf("EquipCard")){
					if(!p->getEquip(qobject_cast<const EquipCard *>(c->getRealCard())->location()))
						choice = "hand_area+equip_area";
				}else
					choice = "hand_area+judge_area";
			}
		}
		choice = room->askForChoice(source,"nospaiyi",choice,QVariant::fromValue(p));
		if(choice=="equip_area")
			room->moveCardTo(c,p,Player::PlaceEquip);
		else if(choice=="judge_area")
			room->moveCardTo(c,p,Player::PlaceDelayedTrick);
		else
			p->obtainCard(c);
		if(index>1)
			room->drawCards(source, 1, "nospaiyi");
	}
}

class NosPaiyiViewAsSkill : public ViewAsSkillV2
{
public:
    NosPaiyiViewAsSkill() : ViewAsSkillV2("nospaiyi", 1) { expand_pile = "nospower"; }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "NosPaiyiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY && request.pattern == "@@nospaiyi"
            && request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "prompt").toBool()
            && !request.initiator->getPile("nospower").isEmpty();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && request.selectedCardIds.isEmpty() && request.initiator->getPile("nospower").contains(card->getEffectiveId());
    }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *target) const override
    { return target && target->isAlive() && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest empty = request;
        empty.selectedCardIds.clear();
        return canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        for (ServerPlayer *target : ctx.targets) {
            SkillContext transfer = ctx;
            transfer.extra_data = false;
            skillEffect(transfer, target);
            if (transfer.extra_data.toBool() && ctx.initiator->isAlive()) {
                SkillContext reward = ctx;
                reward.choice = "draw";
                skillEffect(reward, ctx.initiator);
            }
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target->isAlive() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        if (ctx.choice == "draw") { target->drawCards(getEffectiveAmount(ctx), objectName()); return ContinueEffects; }
        if (!ctx.use_card || ctx.use_card->subcardsLength() != 1) return ContinueEffects;
        ServerPlayer *source = ctx.initiator;
        Room *room = source->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (!source->getPile("nospower").contains(id)) return ContinueEffects;
        const Card *card = Sanguosha->getCard(id);
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        QStringList choices{"hand_area"};
        if (!source->isProhibited(target, card)) {
            if (equip && target->hasEquipArea(equip->location()) && !target->getEquip(equip->location())) choices << "equip_area";
            else if (card->isKindOf("DelayedTrick") && !target->containsTrick(card->objectName())) choices << "judge_area";
        }
        const QString choice = room->askForChoice(source, objectName(), choices.join("+"), QVariant::fromValue(target));
        if (!choices.contains(choice) || !target->isAlive() || !source->getPile("nospower").contains(id)
            || card->hasFlag("using")) return ContinueEffects;
        Player::Place place = Player::PlaceHand;
        if (choice == "equip_area") {
            if (!equip || !target->hasEquipArea(equip->location()) || target->getEquip(equip->location())
                || source->isProhibited(target, card)) return ContinueEffects;
            place = Player::PlaceEquip;
        } else if (choice == "judge_area") {
            if (target->containsTrick(card->objectName()) || source->isProhibited(target, card)) return ContinueEffects;
            place = Player::PlaceDelayedTrick;
        }
        const qint64 cause = room->currentHistoryEventId();
        const QVariantMap before = room->queryHistoryMoves({{"limit", 1}});
        CardMoveReason reason(CardMoveReason::S_REASON_PUT, source->objectName(), target->objectName(), objectName(), "");
        room->moveCardTo(card, source, target, place, reason, true);
        if (target == source || !source->isAlive() || !moved(room, cause, before, id, target, place)) return ContinueEffects;
        ctx.extra_data = true;
        return ContinueEffects;
    }
private:
    static bool moved(Room *room, qint64 cause, const QVariantMap &before, int id, ServerPlayer *target, Player::Place place)
    {
        if (cause <= 0 || before.contains("error") || !before.contains("watermark")) return false;
        QVariantMap filter{{"to", target->objectName()}, {"after", before.value("watermark")}, {"limit", 128}};
        bool found = false;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return false;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), data = fact.value("data").toMap();
                if (data.value("card_id", -1).toInt() == id && data.value("to_place").toInt() == place
                    && room->historyEvent(fact.value("event_id").toLongLong()).value("parent_id").toLongLong() == cause) found = true;
            }
            if (!page.value("has_more").toBool()) return found;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
};

class NosPaiyi : public TriggerSkillV2
{
public:
    NosPaiyi() : TriggerSkillV2("nospaiyi") { events << EventPhaseStart; view_as_skill = new NosPaiyiViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Finish
            && !player->getPile("nospower").isEmpty() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets = {ctx.owner}; return ctx.owner != nullptr; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target->isAlive() || getEffectiveAmount(ctx) <= 0 || target->getPile("nospower").isEmpty()) return false;
        Room::AcceptedViewAsEffectScope borrowed(room, target, objectName(), ctx);
        if (!borrowed.isValid()) return false;
        const SkillInstanceRef ref = borrowed.activationRef();
        target->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "prompt", true);
        RoomState *state = Sanguosha->currentRoomState();
        const auto reason = state->getCurrentCardUseReason();
        const QString pattern = state->getCurrentCardUsePattern();
        const auto restore = qScopeGuard([=] { state->setCurrentCardUseReason(reason); state->setCurrentCardUsePattern(pattern); });
        room->askForUseCard(target, "@@nospaiyi", "nospaiyi0", -1, Card::MethodNone);
        return false;
    }
};

class NosZili : public NosZhonghuiWake
{
public:
    NosZili() : NosZhonghuiWake("noszili", false) { }
};

YJCMPackage::YJCMPackage()
    : Package("YJCM")
{
    General *caozhi = new General(this, "caozhi", "wei", 3); // YJ 001
    caozhi->addSkill(new Luoying);
    caozhi->addSkill(new Jiushi);
    caozhi->addSkill(new JiushiFlip);
    related_skills.insert("jiushi", "#jiushi-flip");

    General *chengong = new General(this, "chengong", "qun", 3); // YJ 002
    chengong->addSkill(new Zhichi);
    chengong->addSkill(new ZhichiProtect);
    chengong->addSkill(new ZhichiClear);
    chengong->addSkill(new Mingce);
    related_skills.insert("zhichi", "#zhichi-protect");
    related_skills.insert("zhichi", "#zhichi-clear");

    General *fazheng = new General(this, "fazheng", "shu", 3); // YJ 003
    fazheng->addSkill(new Enyuan);
    fazheng->addSkill(new Xuanhuo);

    General *gaoshun = new General(this, "gaoshun", "qun"); // YJ 004
    gaoshun->addSkill(new Xianzhen);
    gaoshun->addSkill(new Jinjiu);
    gaoshun->addSkill(new XianzhenTargetMod);
    related_skills.insert("xianzhen", "#xianzhen_target");

    General *lingtong = new General(this, "lingtong", "wu"); // YJ 005
    lingtong->addSkill(new Xuanfeng);

    General *masu = new General(this, "masu", "shu", 3); // YJ 006
    masu->addSkill(new Xinzhan);
    masu->addSkill(new Huilei);

    General *wuguotai = new General(this, "wuguotai", "wu", 3, false); // YJ 007
    wuguotai->addSkill(new Ganlu);
    wuguotai->addSkill(new Buyi);

    General *xusheng = new General(this, "xusheng", "wu"); // YJ 008
    xusheng->addSkill(new Pojun);

    General *xushu = new General(this, "xushu", "shu", 3); // YJ 009
    xushu->addSkill(new Wuyan);
    xushu->addSkill(new Jujian);

    General *yujin = new General(this, "yujin", "wei"); // YJ 010
    yujin->addSkill(new Yizhong);

    General *zhangchunhua = new General(this, "zhangchunhua", "wei", 3, false); // YJ 011
    zhangchunhua->addSkill(new Jueqing);
    zhangchunhua->addSkill(new Shangshi);

    General *zhonghui = new General(this, "zhonghui", "wei"); // YJ 012
    zhonghui->addSkill(new Quanji);
    zhonghui->addSkill(new QuanjiKeep);
    zhonghui->addSkill(new Zili);
    zhonghui->addRelateSkill("paiyi");
    related_skills.insert("quanji", "#quanji");

    addMetaObject<MingceCard>();
    addMetaObject<GanluCard>();
    addMetaObject<XianzhenCard>();
    addMetaObject<XinzhanCard>();
    addMetaObject<JujianCard>();
    addMetaObject<PaiyiCard>();

    skills << new Paiyi << new NosPaiyi << new NosYexin;
}

ADD_PACKAGE(YJCM)

NostalgiaYJCMPackage::NostalgiaYJCMPackage()
    : Package("nostal_yjcm")
{
    General *nos_fazheng = new General(this, "nos_fazheng", "shu", 3);
    nos_fazheng->addSkill(new NosEnyuan);
    nos_fazheng->addSkill(new NosXuanhuo);

    General *nos_lingtong = new General(this, "nos_lingtong", "wu");
    nos_lingtong->addSkill(new NosXuanfeng);
    nos_lingtong->addSkill(new SlashNoDistanceLimitSkill("nosxuanfeng"));
    related_skills.insert("nosxuanfeng", "#nosxuanfeng-slash-ndl");

    General *nos_xushu = new General(this, "nos_xushu", "shu", 3);
    nos_xushu->addSkill(new NosWuyan);
    nos_xushu->addSkill(new NosJujian);

    General *nos_zhangchunhua = new General(this, "nos_zhangchunhua", "wei", 3, false);
    nos_zhangchunhua->addSkill("jueqing");
    nos_zhangchunhua->addSkill(new NosShangshi);

    General *nos_zhonghui = new General(this, "nos_zhonghui", "wei", 3, true);
    nos_zhonghui->addSkill(new NosZhenggong);
    nos_zhonghui->addSkill(new NosQuanji);
    nos_zhonghui->addSkill(new NosBaijiang);
    nos_zhonghui->addSkill(new NosZili);

    addMetaObject<NosXuanhuoCard>();
    addMetaObject<NosJujianCard>();
    addMetaObject<NosYexinCard>();
    addMetaObject<NosPaiyiCard>();

    MigrateToNostalgiaYJCM2012(this);
    MigrateToNostalgiaYJCM2013(this);
    MigrateToNostalgiaYJCM2014(this);
    MigrateToNostalgiaYJCM2015(this);
}
ADD_PACKAGE(NostalgiaYJCM)

void MigrateToOLStYJ2011(Package *pkg)
{
    General *ol_caozhi = new General(pkg, "ol_caozhi", "wei", 3);
    ol_caozhi->addSkill(new OLLuoying);
    ol_caozhi->addSkill("jiushi");

    /*General *ol_fazheng = new General(pkg, "ol_fazheng", "shu", 3, true);
    ol_fazheng->addSkill("enyuan");
    ol_fazheng->addSkill("xuanhuo");*/

    General *ol_masu = new General(pkg, "ol_masu", "shu", 3);
    ol_masu->addSkill(new OLSanyao);
    ol_masu->addSkill(new OLZhiman);

    General *ol_xusheng = new General(pkg, "ol_xusheng", "wu");
    ol_xusheng->addSkill(new OlPojun);

    /*General *ol_xushu = new General(pkg, "ol_xushu", "shu", 3);
    ol_xushu->addSkill("wuyan");
    ol_xushu->addSkill("jujian");*/

    General *ol_yujin = new General(pkg, "ol_yujin", "wei");
    ol_yujin->addSkill(new Jieyue);

    pkg->addMetaObject<OLSanyaoCard>();
    pkg->addMetaObject<JieyueCard>();
}

void MigrateToMobileStYJ2011(Package *pkg)
{
    General *mobile_masu = new General(pkg, "mobile_masu", "shu", 3);
    mobile_masu->addSkill(new Sanyao);
    mobile_masu->addSkill(new Zhiman);

    pkg->addMetaObject<SanyaoCard>();
}
