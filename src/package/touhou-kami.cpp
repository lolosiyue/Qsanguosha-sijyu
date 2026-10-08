#include "touhou-kami.h"
#include "touhou-utils.h"
#include "engine.h"
#include "general.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "maneuvering.h"
#include "util.h"

using namespace TouhouUtils;

namespace {

bool someonesTurn(Room *room)
{
    const ServerPlayer *current = room ? room->getCurrent() : nullptr;
    return current && current->getPhase() != Player::NotActive;
}

QString typeClass(const Card *card)
{
    QString type = card->getType();
    if (!type.isEmpty())
        type[0] = type[0].toUpper();
    return type + "Card";
}

// Shared shape of the 2-card / 1-card selection proxies.
class SelectionViewAs : public ViewAsSkillV2
{
public:
    SelectionViewAs(const QString &name, const QString &pattern, int minCards, int maxCards, bool discard)
        : ViewAsSkillV2(name, maxCards), m_pattern(pattern), m_min(minCards), m_max(maxCards), m_discard(discard)
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == m_pattern
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || card->hasFlag("using") || request.selectedCardIds.size() >= m_max)
            return false;
        const int id = card->getEffectiveId();
        if (!(self->handCards().contains(id) || self->getEquipsId().contains(id)))
            return false;
        return !m_discard || self->canDiscard(self, id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        const int n = request.selectedCardIds.size();
        if (n < m_min || n > m_max)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        foreach (int id, request.selectedCardIds) {
            if (!canSelectCard(selection, Sanguosha->getCard(id)))
                return false;
            selection.selectedCardIds << id;
        }
        return true;
    }

    bool willThrowSelectedCards() const override { return m_discard; }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

private:
    QString m_pattern;
    int m_min;
    int m_max;
    bool m_discard;
};

// ---------------------------------------------------------------- kami001

class ThKexing : public TriggerSkillV2
{
public:
    ThKexing() : TriggerSkillV2("thkexing")
    {
        events << EventPhaseStart;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Picked cards go to the discard pile; the other non-tricks go to the bottom.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const QList<int> shown = room->getNCards(3, false);
        CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, player->objectName(), objectName(), QString());
        room->moveCardsAtomic(CardsMoveStruct(shown, nullptr, Player::PlaceTable, turnover), true);
        QList<int> bottom;
        QList<int> discard;
        foreach (int id, shown) {
            if (Sanguosha->getCard(id)->isKindOf("TrickCard"))
                discard << id;
            else
                bottom << id;
        }
        if (!bottom.isEmpty()) {
            room->fillAG(shown, player, discard);
            while (!bottom.isEmpty()) {
                const int id = room->askForAG(player, bottom, true, objectName());
                if (!bottom.contains(id))
                    break;
                room->takeAG(nullptr, id, false, QList<ServerPlayer *>{player});
                bottom.removeOne(id);
                discard << id;
            }
            room->clearAG(player);
        }
        QList<int> stillThere;
        foreach (int id, bottom)
            if (room->getCardPlace(id) == Player::PlaceTable)
                stillThere << id;
        if (!stillThere.isEmpty()) {
            CardMoveReason put(CardMoveReason::S_REASON_PUT, player->objectName(), objectName(), QString());
            room->moveCardsAtomic(CardsMoveStruct(stillThere, nullptr, nullptr, Player::PlaceTable, Player::DrawPile, put), true);
            room->askForGuanxing(player, stillThere, Room::GuanxingDownOnly);
        }
        QList<int> thrown;
        foreach (int id, discard)
            if (room->getCardPlace(id) == Player::PlaceTable)
                thrown << id;
        if (!thrown.isEmpty()) {
            DummyCard dummy(thrown);
            CardMoveReason toPile(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), objectName(), QString());
            room->throwCard(&dummy, toPile, nullptr);
        }
        return false;
    }
};

class ThShenfeng : public ViewAsSkillV2
{
public:
    ThShenfeng() : ViewAsSkillV2("thshenfeng", 2) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || card->hasFlag("using") || request.selectedCardIds.size() >= 2)
            return false;
        const int id = card->getEffectiveId();
        if (!(self->handCards().contains(id) || self->getEquipsId().contains(id)))
            return false;
        return request.selectedCardIds.isEmpty()
            || Sanguosha->getCard(request.selectedCardIds.first())->getColor() == card->getColor();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 2)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        if (!canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first())))
            return false;
        selection.selectedCardIds << request.selectedCardIds.first();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.last()));
    }

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to->getHp() > qMax(0, request.initiator->getHp());
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThShenfengCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive() || !ctx.use_card)
            return ContinueEffects;
        Room *room = source->getRoom();
        QList<int> ids;
        foreach (int id, ctx.use_card->getSubcards())
            if (room->getCardOwner(id) == source)
                ids << id;
        if (!ids.isEmpty())
            room->giveCard(source, target, ids, objectName());
        QList<ServerPlayer *> victims;
        foreach (ServerPlayer *p, room->getOtherPlayers(target))
            if (target->distanceTo(p) == 1)
                victims << p;
        if (victims.isEmpty() || !source->isAlive() || !target->isAlive())
            return ContinueEffects;
        ServerPlayer *victim = room->askForPlayerChosen(source, victims, objectName(), "@thshenfeng:" + target->objectName());
        if (!victim)
            victim = victims.first();
        room->damage(DamageStruct(objectName(), target, victim));
        return ContinueEffects;
    }
};

class ThKaihai : public TriggerSkillV2
{
public:
    ThKaihai() : TriggerSkillV2("thkaihai")
    {
        events << CardsMoveOneTime;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || move.from != player
            || !move.from_places.contains(Player::PlaceHand) || !move.is_last_handcard)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.owner->drawCards(1, objectName(), false);
        return false;
    }
};

// ---------------------------------------------------------------- kami002

class ThTianbao : public TriggerSkillV2
{
public:
    ThTianbao() : TriggerSkillV2("thtianbao")
    {
        events << GameStart << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == EventPhaseStart && player->getPhase() != Player::Start && player->getPhase() != Player::Finish)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // Swaps the 灵 for a random 灵宝 and its skills.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        room->removeReihouCard(player);
        const Package *pack = Sanguosha->getPackage("tenshi-reihou");
        if (!pack)
            return false;
        const QList<const General *> reihous = pack->findChildren<const General *>();
        if (reihous.isEmpty())
            return false;
        const General *reihou = reihous.at(qsanRandomBounded(reihous.length()));
        LogMessage log;
        log.type = "#RhYaodao";
        log.from = player;
        log.arg = reihou->objectName();
        room->sendLog(log);
        room->attachReihouCard(player, reihou->objectName());
        return false;
    }
};

// ---------------------------------------------------------------- kami003

class ThWudao : public TriggerSkillV2
{
public:
    ThWudao() : TriggerSkillV2("thwudao")
    {
        events << GameStart << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == EventPhaseStart
            && (player->getPhase() != Player::Start || player->getHp() == player->getLostHp()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(player, objectName());
        if (event == GameStart) {
            room->loseHp(player, 4, true, player, objectName());
            return false;
        }
        const int delta = player->getHp() - player->getLostHp();
        if (delta > 0)
            room->loseHp(player, delta, true, player, objectName());
        else if (delta < 0)
            room->recover(player, RecoverStruct(objectName(), player, -delta));
        return false;
    }
};

class ThHuanjun : public FilterSkill
{
public:
    ThHuanjun() : FilterSkill("thhuanjun") {}

    bool viewFilter(const Card *to_select) const override
    {
        if (to_select->isKindOf("Jink") && to_select->getSuit() == Card::Diamond)
            return true;
        Room *room = Sanguosha->currentRoom();
        return room && to_select->isKindOf("Armor")
            && room->getCardPlace(to_select->getEffectiveId()) == Player::PlaceHand;
    }

    const Card *viewAs(const Card *original) const override
    {
        Card *card = original->isKindOf("Jink")
            ? static_cast<Card *>(new Duel(original->getSuit(), original->getNumber()))
            : static_cast<Card *>(new Analeptic(original->getSuit(), original->getNumber()));
        card->setSkillName(objectName());
        return card;
    }
};

// An item of `kind` about to enter the owner's equip area goes to the hand instead.
class EquipToHand : public TriggerSkillV2
{
public:
    EquipToHand(const QString &name, const QString &parent, const char *kind)
        : TriggerSkillV2(name), m_parent(parent), m_kind(kind)
    {
        events << BeforeCardsMove;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.to != player || move.to_place != Player::PlaceEquip || !player->isAlive()
            || !player->hasSkill(m_parent))
            return TriggerList();
        foreach (int id, move.card_ids)
            if (Sanguosha->getEngineCard(id)->isKindOf(m_kind))
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QList<int> items;
        foreach (int id, move.card_ids)
            if (Sanguosha->getEngineCard(id)->isKindOf(m_kind))
                items << id;
        if (items.isEmpty())
            return false;
        room->sendCompulsoryTriggerLog(player, m_parent);
        room->broadcastSkillInvoke(m_parent);
        move.removeCardIds(items);
        *ctx.original_data = QVariant::fromValue(move);
        DummyCard dummy(items);
        room->obtainCard(player, &dummy);
        return false;
    }

private:
    QString m_parent;
    const char *m_kind;
};

// ---------------------------------------------------------------- kami004

class ThGugao : public ViewAsSkillV2
{
public:
    ThGugao() : ViewAsSkillV2("thgugao") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        return self && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !self->isKongcheng()
            && self->getMark("thgugao_used-PlayClear") == 0;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator && request.initiator->canPindian(to);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThGugaoCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator)
            return false;
        room->addPlayerMark(ctx.initiator, "thgugao_used-PlayClear");
        return true;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->isAlive() || !source->canPindian(target))
            return ContinueEffects;
        Room *room = source->getRoom();
        const int result = source->pindianInt(target, objectName());
        if (result == 1) {
            // After 皇仪 a win refreshes 孤高 for this phase.
            if (source->getMark("thhuangyi") > 0)
                room->setPlayerMark(source, "thgugao_used-PlayClear", 0);
            if (target->isAlive())
                room->damage(DamageStruct(objectName(), source, target));
        } else if (result == -1 && source->getMark("thqianyu") == 0 && target->isAlive()) {
            // After 千狱 the damage for not winning is prevented.
            room->damage(DamageStruct(objectName(), target, source));
        }
        return ContinueEffects;
    }
};

class ThQianyu : public WakeSkill
{
public:
    ThQianyu() : WakeSkill("thqianyu")
    {
        events << EventPhaseChanging;
        waked_skills = "thkuangmo";
    }

protected:
    // After anyone's turn.
    QList<ServerPlayer *> wakeOwners(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return QList<ServerPlayer *>();
        return room->getAlivePlayers();
    }

    bool canAwaken(Room *, ServerPlayer *player) const override { return player->getHp() == 1; }

    void awaken(Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (player->getMaxHp() > 1)
            room->changeMaxHpForAwakenSkill(player, 1 - player->getMaxHp(), objectName());
        if (!player->isAlive())
            return;
        room->acquireSkillFromEffect(player, "thkuangmo", ctx);
        room->scheduleExtraTurn(player, ctx.sourceRef);
    }
};

class ThKuangmo : public TriggerSkillV2
{
public:
    ThKuangmo() : TriggerSkillV2("thkuangmo")
    {
        events << Damage;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || !player->hasSkill(objectName())
            || (damage.card && damage.card->isKindOf("Slash")) || damage.damage < 1)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const int n = ctx.original_data->value<DamageStruct>().damage;
        for (int i = 0; i < n && player->isAlive(); ++i) {
            room->sendCompulsoryTriggerLog(player, objectName());
            room->broadcastSkillInvoke(objectName());
            room->setPlayerProperty(player, "maxhp", player->getMaxHp() + 1);
            LogMessage log;
            log.type = "#GainMaxHp";
            log.from = player;
            log.arg = "1";
            room->sendLog(log);
            room->recover(player, RecoverStruct(objectName(), player));
        }
        return false;
    }
};

class ThHuangyi : public WakeSkill
{
public:
    ThHuangyi() : WakeSkill("thhuangyi") { events << EventPhaseStart; }

protected:
    bool atTiming(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player->getPhase() == Player::Start;
    }

    bool canAwaken(Room *, ServerPlayer *player) const override
    {
        return player->getMark("thqianyu") > 0 && player->getMaxHp() > player->getGeneralMaxHp();
    }

    void awaken(Room *room, ServerPlayer *player, SkillContext &) const override
    {
        const int n = player->getMaxHp() - player->getGeneralMaxHp();
        if (n > 0 && room->changeMaxHpForAwakenSkill(player, -n, objectName()) && player->isAlive())
            player->drawCards(n, objectName());
        if (player->isAlive() && player->isWounded())
            room->recover(player, RecoverStruct(objectName(), player));
        if (player->isAlive())
            room->detachSkillFromPlayer(player, "thkuangmo", false, true);
    }
};

// ---------------------------------------------------------------- kami005

class ThSuhu : public TriggerSkillV2
{
public:
    ThSuhu() : TriggerSkillV2("thsuhu") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Draw)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Replaces the draw: a 面 from the top, then the owner may use a hand card.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const QList<int> ids = room->getNCards(1, false);
        if (!ids.isEmpty())
            player->addToPile("mask", ids, true);
        if (!player->isAlive())
            return true;
        QStringList names;
        foreach (const Card *card, player->getHandcards()) {
            QString name = card->getClassName();
            if (name.endsWith("Slash"))
                name = "Slash";
            if (!names.contains(name) && card->isAvailable(player))
                names << name;
        }
        if (!names.isEmpty())
            room->askForUseCard(player, names.join(",") + "|.|.|hand", "@thsuhu");
        return true;
    }
};

class ThFenlang : public TriggerSkillV2
{
public:
    ThFenlang() : TriggerSkillV2("thfenlang")
    {
        events << HpChanged;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || data.userType() != qMetaTypeId<DamageStruct>())
            return TriggerList();
        if (data.value<DamageStruct>().damage < 1)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const int n = ctx.original_data->value<DamageStruct>().damage;
        for (int i = 0; i < n && player->isAlive(); ++i) {
            room->broadcastSkillInvoke(objectName());
            room->sendCompulsoryTriggerLog(player, objectName());
            room->recover(player, RecoverStruct(objectName(), player));
            player->drawCards(2, objectName());
        }
        return false;
    }
};

class ThLeshi : public ViewAsSkillV2
{
public:
    ThLeshi() : ViewAsSkillV2("thleshi") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThLeshiCard"; }

    // The chosen type counts as unused this phase (for 净缘 and use limits alike).
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive())
            return FinishSkill;
        Room *room = source->getRoom();
        const QString choice = room->askForChoice(source, objectName(), "BasicCard+EquipCard+TrickCard");
        LogMessage log;
        log.type = "#ThLeshi";
        log.from = source;
        log.arg = choice;
        room->sendLog(log);
        room->removePlayerCardLimitationByReason(source, "thjingyuan_" + choice);
        room->setPlayerMark(source, "thjingyuan_" + choice + "-PlayClear", 0);
        room->addPlayerHistory(source, choice, 0);
        if (choice == "BasicCard") {
            const QStringList keys{"Slash", "FireSlash", "ThunderSlash", "Analeptic"};
            foreach (const QString &key, keys)
                room->addPlayerHistory(source, key, 0);
        }
        source->drawCards(4, objectName());
        return FinishSkill;
    }
};

class ThYouli : public TriggerSkillV2
{
public:
    ThYouli() : TriggerSkillV2("thyouli")
    {
        events << CardsMoveOneTime;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to != player)
            return TriggerList();
        if ((move.to_place == Player::PlaceHand && player->getHandcardNum() > 4)
            || (move.to_place == Player::PlaceSpecial && move.to_pile_name == "mask" && player->getPile("mask").length() >= 2))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(player, objectName());
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        if (move.to_place == Player::PlaceHand) {
            const int n = player->getHandcardNum() - 4;
            if (n <= 0)
                return false;
            const Card *exchange = room->askForExchange(player, objectName(), n, n, false, "@thyouli-hand");
            QList<int> ids = exchange ? exchange->getSubcards() : QList<int>();
            if (ids.size() != n) {
                ids = player->handCards();
                qsanShuffle(ids);
                ids = ids.mid(0, n);
            }
            player->addToPile("mask", ids, true);
            return false;
        }
        while (player->isAlive() && player->getPile("mask").size() >= 2) {
            QList<int> masks = player->getPile("mask");
            QList<int> chosen;
            if (masks.size() == 2) {
                chosen = masks;
            } else {
                room->fillAG(masks, player);
                const int first = room->askForAG(player, masks, false, objectName());
                chosen << (masks.contains(first) ? first : masks.first());
                masks.removeOne(chosen.first());
                room->takeAG(player, chosen.first(), false, QList<ServerPlayer *>{player});
                const int second = room->askForAG(player, masks, false, objectName());
                chosen << (masks.contains(second) ? second : masks.first());
                room->clearAG(player);
            }
            DummyCard dummy(chosen);
            CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), objectName(), QString());
            room->throwCard(&dummy, reason, nullptr);
            room->loseHp(player, 1, true, player, objectName());
        }
        return false;
    }
};

class ThYouliMaxCards : public MaxCardsSkillV2
{
public:
    ThYouliMaxCards() : MaxCardsSkillV2("#thyouli") { frequency = Compulsory; }

    CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.primary->hasSkill("thyouli"))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(4);
    }
};

// One card of each type in the play phase: a used type is locked until the phase ends.
class ThJingyuan : public TriggerSkillV2
{
public:
    ThJingyuan() : TriggerSkillV2("thjingyuan")
    {
        events << PreCardUsed << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().from == Player::Play) {
            const QStringList types{"BasicCard", "EquipCard", "TrickCard"};
            foreach (const QString &type, types)
                room->removePlayerCardLimitationByReason(player, "thjingyuan_" + type);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != PreCardUsed || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Play)
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (use.from != player || !use.card || use.card->getTypeId() == Card::TypeSkill
            || use.card->getHandlingMethod() != Card::MethodUse
            || player->getMark("thjingyuan_" + typeClass(use.card) + "-PlayClear") > 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const QString type = typeClass(ctx.original_data->value<CardUseStruct>().card);
        room->sendCompulsoryTriggerLog(player, objectName());
        room->addPlayerMark(player, "thjingyuan_" + type + "-PlayClear");
        room->setPlayerCardLimitation(player, "use", type, false, "thjingyuan_" + type);
        return false;
    }
};

// ---------------------------------------------------------------- kami006

class ThPanghun : public TriggerSkillV2
{
public:
    ThPanghun() : TriggerSkillV2("thpanghun") { events << EventPhaseEnd << EventPhaseChanging; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::NotActive
            && player->getMark("@stitch") > 0) {
            room->setPlayerMark(player, "@stitch", 0);
            foreach (ServerPlayer *p, room->getOtherPlayers(player, true))
                room->removeFixedDistance(player, p, 1);
            room->filterCards(player, player->getHandcards(), true);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseEnd || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Draw || !player->canDiscard(player, "he"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room->askForCard(ctx.owner, "..", "@thpanghun", QVariant(), objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // Until the turn ends: distance 1 to everyone, and every 杀 reads as 铁索连环.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->addPlayerMark(player, "@stitch");
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            room->setFixedDistance(player, p, 1);
        room->filterCards(player, player->getHandcards(), false);
        return false;
    }
};

class ThPanghunFilter : public FilterSkill
{
public:
    ThPanghunFilter() : FilterSkill("#thpanghun-filter") {}

    bool viewFilter(const Card *to_select) const override
    {
        Room *room = Sanguosha->currentRoom();
        const ServerPlayer *owner = room ? room->getCardOwner(to_select->getEffectiveId()) : nullptr;
        return owner && owner->getMark("@stitch") > 0 && to_select->isKindOf("Slash");
    }

    const Card *viewAs(const Card *original) const override
    {
        IronChain *chain = new IronChain(original->getSuit(), original->getNumber());
        chain->setSkillName("thpanghun");
        return chain;
    }
};

class ThJingwu : public ViewAsSkillV2
{
public:
    ThJingwu() : ViewAsSkillV2("thjingwu", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isKongcheng();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card) && card->isKindOf("TrickCard")
            && card->isBlack();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    bool willThrowSelectedCards() const override { return false; }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && request.initiator->canDiscard(to, "ej");
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThJingwuCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !ctx.use_card || ctx.use_card->subcardsLength() == 0)
            return ContinueEffects;
        Room *room = source->getRoom();
        const int shown = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(shown) == source && room->getCardPlace(shown) == Player::PlaceHand)
            room->showCard(source, shown);
        room->setPlayerFlag(source, "thjingwuInvoke");
        if (!source->canDiscard(target, "ej"))
            return ContinueEffects;
        const int id = room->askForCardChosen(source, target, "ej", objectName(), false, Card::MethodDiscard);
        if (id >= 0)
            room->throwCard(id, room->getCardPlace(id) == Player::PlaceDelayedTrick ? nullptr : target, source);
        return ContinueEffects;
    }
};

// 连舞技: works only after 旁魂 and 镜悟 this turn, once.
class ThLunyu : public TriggerSkillV2
{
public:
    ThLunyu() : TriggerSkillV2("thlunyu") { events << TargetSpecified << BeforeCardsMove << EventPhaseEnd; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == TargetSpecified) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card)
                foreach (ServerPlayer *to, use.to)
                    room->setCardFlag(use.card, "DisableLunyuOf_" + to->objectName());
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventPhaseEnd) {
            foreach (ServerPlayer *p, room->getAlivePlayers())
                if (p->hasFlag("thlunyuDraw"))
                    result[p] << objectName();
            return result;
        }
        if (event != BeforeCardsMove || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getMark("@stitch") == 0 || !player->hasFlag("thjingwuInvoke") || player->hasFlag("thlunyuInvoke"))
            return result;
        if (!isOwnTurn(player))
            return result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.card_ids.isEmpty() || move.to_place != Player::DiscardPile)
            return result;
        const Card *used = move.reason.m_useStruct.card;
        if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_USE && used
            && used->hasFlag("DisableLunyuOf_" + player->objectName()))
            return result;
        result[player] << objectName();
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseEnd)
            return true;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        foreach (int id, move.card_ids) {
            if (ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(Sanguosha->getCard(id)))) {
                ctx.extra_data = id;
                room->broadcastSkillInvoke(objectName());
                return true;
            }
        }
        return false;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (event == EventPhaseEnd) {
            room->setPlayerFlag(player, "-thlunyuDraw");
            player->drawCards(2, objectName(), false);
            return false;
        }
        const int id = ctx.extra_data.toInt();
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        if (!move.card_ids.contains(id))
            return false;
        move.removeCardIds(QList<int>{id});
        *ctx.original_data = QVariant::fromValue(move);
        LogMessage log;
        log.type = "#ThLunyuPut";
        log.from = player;
        log.card_str = QString::number(id);
        room->sendLog(log);
        CardMoveReason reason(CardMoveReason::S_REASON_PUT, player->objectName(), QString(), objectName(), QString());
        room->moveCardsAtomic(CardsMoveStruct(id, nullptr, Player::DrawPile, reason), true);
        room->setPlayerFlag(player, "thlunyuInvoke");
        room->setPlayerFlag(player, "thlunyuDraw");
        return false;
    }
};

// ---------------------------------------------------------------- kami007

class ThFanhun : public TriggerSkillV2
{
public:
    ThFanhun() : TriggerSkillV2("thfanhun") { events << AskForPeaches; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DyingStruct dying = data.value<DyingStruct>();
        if (!player || dying.who != player || !player->isAlive() || player->getHp() > 0 || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        player->gainMark("@bloom");
        if (!player->isAlive())
            return false;
        if (player->getHp() < 1)
            room->recover(player, RecoverStruct(objectName(), player, 1 - player->getHp()));
        if (player->isChained())
            room->setPlayerChained(player, false, player);
        if (!player->faceUp())
            player->turnOver();
        return false;
    }
};

class ThYoushang : public TriggerSkillV2
{
public:
    ThYoushang() : TriggerSkillV2("thyoushang") { events << DamageCaused; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.card
            || damage.card->getSuit() == Card::Spade || damage.chain || damage.transfer || !damage.to)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
        if (target && target->isAlive())
            room->loseMaxHp(target, 1, objectName());
        ctx.owner->gainMark("@bloom");
        return true;
    }
};

class ThYouyaViewAs : public ViewAsSkillV2
{
public:
    ThYouyaViewAs() : ViewAsSkillV2("thyouya", 1) {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thyouya"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || card->hasFlag("using") || !request.selectedCardIds.isEmpty())
            return false;
        const int id = card->getEffectiveId();
        return (self->handCards().contains(id) || self->getEquipsId().contains(id)) && self->canDiscard(self, id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && to && selected.length() < request.initiator->getMark("@bloom");
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return request.initiator && !selected.isEmpty() && selected.length() <= request.initiator->getMark("@bloom");
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThYouyaCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        if (room->askForCard(target, "jink", "@thyouya-jink:" + source->objectName(), QVariant(), Card::MethodResponse,
                             source))
            return ContinueEffects;
        if (!target->isNude() && source->isAlive()) {
            const int id = room->askForCardChosen(source, target, "he", objectName());
            if (id >= 0)
                room->obtainCard(source, id, false);
        }
        return ContinueEffects;
    }
};

class ThYouya : public TriggerSkillV2
{
public:
    ThYouya() : TriggerSkillV2("thyouya")
    {
        events << DamageInflicted;
        view_as_skill = new ThYouyaViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<DamageStruct>().to != player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getMark("@bloom") < 1 || !player->canDiscard(player, "he"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->askForUseCard(ctx.owner, "@@thyouya", "@thyouya", -1, Card::MethodDiscard);
        return false;
    }
};

class ThManxiao : public TriggerSkillV2
{
public:
    ThManxiao() : TriggerSkillV2("thmanxiao")
    {
        events << MarkChanged;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const MarkStruct mark = data.value<MarkStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || mark.name != "@bloom"
            || player->getMark("@bloom") < 4)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        room->killPlayer(ctx.owner);
        return false;
    }
};

class ThManxiaoMaxCards : public MaxCardsSkillV2
{
public:
    ThManxiaoMaxCards() : MaxCardsSkillV2("#thmanxiao") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.primary->hasSkill("thmanxiao"))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(ctx.primary->getMark("@bloom"));
    }
};

// ---------------------------------------------------------------- kami008

class ThJinluViewAs : public ViewAsSkillV2
{
public:
    ThJinluViewAs() : ViewAsSkillV2("thjinlu", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "h");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card)
            && request.initiator->canDiscard(request.initiator, card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThJinluCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive() || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        source->setTag("ThJinluTarget", target->objectName());
        room->setPlayerFlag(source, "ThJinluUsed");
        QList<int> ids = target->handCards();
        ids << target->getEquipsId();
        if (ids.isEmpty())
            return ContinueEffects;
        DummyCard dummy(ids);
        CardMoveReason reason(CardMoveReason::S_REASON_TRANSFER, source->objectName(), target->objectName(), objectName(),
                              QString());
        room->moveCardTo(&dummy, target, source, Player::PlaceHand, reason, false);
        return ContinueEffects;
    }
};

class ThJinlu : public TriggerSkillV2
{
public:
    ThJinlu() : TriggerSkillV2("thjinlu")
    {
        events << EventPhaseEnd << EventPhaseStart;
        view_as_skill = new ThJinluViewAs;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasFlag("ThJinluUsed"))
            return TriggerList();
        if (event == EventPhaseEnd && player->getPhase() == Player::Play
            && !player->getTag("ThJinluTarget").toString().isEmpty())
            return TriggerList{{player, {objectName()}}};
        if (event == EventPhaseStart && player->getPhase() == Player::Finish)
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->sendCompulsoryTriggerLog(player, objectName());
        if (event == EventPhaseStart) {
            player->turnOver();
            return false;
        }
        ServerPlayer *target = room->findPlayerByObjectName(player->getTag("ThJinluTarget").toString());
        player->removeTag("ThJinluTarget");
        if (!target || !target->isAlive() || target->getHp() < 1 || player->isNude())
            return false;
        QList<int> ids;
        if (player->getCardCount() <= target->getHp()) {
            ids = player->handCards();
            ids << player->getEquipsId();
        } else {
            const Card *exchange = room->askForExchange(player, objectName(), target->getHp(), target->getHp(), true,
                                                        "@thjinlu-goback:" + target->objectName());
            if (exchange)
                ids = exchange->getSubcards();
        }
        if (!ids.isEmpty())
            room->giveCard(player, target, ids, objectName());
        return false;
    }
};

class ThKuangli : public TriggerSkillV2
{
public:
    ThKuangli() : TriggerSkillV2("thkuangli")
    {
        events << TurnedOver;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.owner->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- kami009

class ThYuxin : public TriggerSkillV2
{
public:
    ThYuxin() : TriggerSkillV2("thyuxin") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Draw)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const QList<int> ids = room->getNCards(2, false);
        bool red = ids.size() == 2;
        foreach (int id, ids)
            red = red && Sanguosha->getCard(id)->isRed();
        CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, player->objectName(), objectName(), QString());
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable, turnover), true);
        QList<int> gain;
        foreach (int id, ids)
            if (room->getCardPlace(id) == Player::PlaceTable)
                gain << id;
        if (!gain.isEmpty()) {
            DummyCard dummy(gain);
            room->obtainCard(player, &dummy);
        }
        if (red && player->isAlive()) {
            ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(),
                                                            "@thyuxin-target", true);
            if (target) {
                if (target->isWounded() && room->askForChoice(target, objectName(), "recover+draw") == "recover")
                    room->recover(target, RecoverStruct(objectName(), player));
                else
                    target->drawCards(2, objectName());
            }
        }
        return true;
    }
};

// Discard one or two cards to take one or both of two skills until the turn ends.
class ThPickSkills : public TriggerSkillV2
{
public:
    ThPickSkills(const QString &name, Player::Phase phase, const QStringList &skills)
        : TriggerSkillV2(name), m_phase(phase), m_skills(skills)
    {
        events << EventPhaseStart << EventPhaseChanging;
        view_as_skill = new SelectionViewAs(name, "@@" + name, 1, 2, true);
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::NotActive)
            revokeTracked(room, player, tagKey());
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != m_phase || !player->canDiscard(player, "he"))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const Card *used = room->askForUseCard(ctx.owner, "@@" + objectName(), "@" + objectName(), -1, Card::MethodDiscard);
        if (!used)
            return false;
        ctx.extra_data = used->subcardsLength();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QStringList chosen = m_skills;
        if (ctx.extra_data.toInt() < 2)
            chosen = QStringList{room->askForChoice(player, objectName(), m_skills.join("+"))};
        foreach (const QString &skill, chosen)
            grantTracked(room, player, tagKey(), skill);
        return false;
    }

private:
    QString tagKey() const { return objectName() + "_grants"; }

    Player::Phase m_phase;
    QStringList m_skills;
};

// 天妒 (TouhouTripleSha): the judge card may go to any character.
class IkTiandu : public TriggerSkillV2
{
public:
    IkTiandu() : TriggerSkillV2("iktiandu") { events << FinishJudge; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        JudgeStruct *judge = data.value<JudgeStruct *>();
        if (!player || !judge || judge->who != player || !player->isAlive() || !player->hasSkill(objectName())
            || !judge->card || room->getCardPlace(judge->card->getEffectiveId()) != Player::PlaceJudge)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "@iktiandu",
                                                        true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (judge && judge->card && target && target->isAlive()
            && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge)
            room->obtainCard(target, judge->card);
        return false;
    }
};

// ---------------------------------------------------------------- kami010

QString lanternSkill(Card::Suit suit)
{
    switch (suit) {
    case Card::Heart: return "biyue";
    case Card::Spade: return "feiying";
    case Card::Diamond: return "yingzi";
    case Card::Club: return "thjifeng";
    default: return QString();
    }
}

// Keeps the 灯 skills in line with the suits present in the pile.
void syncLanterns(Room *room, ServerPlayer *player)
{
    QStringList wanted;
    if (player->isAlive() && player->hasSkill("thrangdeng", true))
        foreach (int id, player->getPile("lantern")) {
            const QString skill = lanternSkill(Sanguosha->getCard(id)->getSuit());
            if (!skill.isEmpty() && !wanted.contains(skill))
                wanted << skill;
        }
    const QVariantMap granted = player->getTag("thrangdeng_grants").toMap();
    foreach (const QString &skill, granted.keys())
        if (!wanted.contains(skill))
            revokeTracked(room, player, "thrangdeng_grants", skill);
    foreach (const QString &skill, wanted)
        grantTracked(room, player, "thrangdeng_grants", skill);
}

class ThRangdeng : public TriggerSkillV2
{
public:
    ThRangdeng() : TriggerSkillV2("thrangdeng")
    {
        events << EventPhaseStart << CardsMoveOneTime << EventAcquireSkill << EventLoseSkill;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            const bool in = move.to == player && move.to_place == Player::PlaceSpecial && move.to_pile_name == "lantern";
            const bool out = move.from == player && move.from_places.contains(Player::PlaceSpecial);
            if ((in || out) && (player->hasSkill(objectName(), true) || player->getTag("thrangdeng_grants").isValid()))
                syncLanterns(room, player);
        } else if ((event == EventAcquireSkill || event == EventLoseSkill)
                   && (data.toString() == objectName() || data.toString().startsWith(objectName() + "#"))) {
            syncLanterns(room, player);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Play || player->isKongcheng())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(player, objectName());
        for (int i = 0; i < 3 && player->isAlive() && !player->isKongcheng(); ++i) {
            QStringList numbers;
            for (int n = 1; n <= 13; ++n)
                numbers << QString::number(n);
            foreach (int id, player->getPile("lantern"))
                numbers.removeAll(QString::number(Sanguosha->getCard(id)->getNumber()));
            const Card *card = nullptr;
            if (!numbers.isEmpty())
                card = room->askForCard(player, ".|.|" + numbers.join(",") + "|hand",
                                        "@thrangdeng:::" + QString::number(i), QVariant(), Card::MethodNone);
            if (!card) {
                if (i == 0 && player->canDiscard(player, "h"))
                    room->askForDiscard(player, objectName(), 1, 1);
                break;
            }
            player->addToPile("lantern", card, true);
        }
        return false;
    }
};

class ThBaihun : public ViewAsSkillV2
{
public:
    ThBaihun() : ViewAsSkillV2("thbaihun") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getPile("lantern").length() >= 13;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThBaihunCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target)
            return ContinueEffects;
        Room *room = source->getRoom();
        const QList<int> lanterns = source->getPile("lantern");
        if (lanterns.length() < 13)
            return ContinueEffects;
        DummyCard dummy(lanterns);
        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, source->objectName(), objectName(), QString());
        room->throwCard(&dummy, reason, nullptr);
        revokeTracked(room, source, "thrangdeng_grants");
        if (target->isAlive())
            room->killPlayer(target);
        return ContinueEffects;
    }
};

class ThJifeng : public MaxCardsSkillV2
{
public:
    ThJifeng() : MaxCardsSkillV2("thjifeng") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.primary->hasSkill(objectName()))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(1);
    }
};

// ---------------------------------------------------------------- kami011

class ThHuanzang : public TriggerSkillV2
{
public:
    ThHuanzang() : TriggerSkillV2("thhuanzang")
    {
        events << EventPhaseEnd;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->sendCompulsoryTriggerLog(player, objectName());
        JudgeStruct judge;
        judge.pattern = ".|black";
        judge.good = false;
        judge.reason = objectName();
        judge.who = player;
        room->judge(judge);
        if (judge.isBad() && player->isAlive())
            room->loseHp(player, 1, true, player, objectName());
        return false;
    }
};

class ThXujing : public TriggerSkillV2
{
public:
    ThXujing() : TriggerSkillV2("thxujing") { events << TargetConfirmed << EventPhaseChanging << Death; }

    // The lent skills last until the end of the owner's next turn.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->getTag("ThXujing").toBool())
            return false;
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive)
            return false;
        if (event == Death && data.value<DeathStruct>().who != player)
            return false;
        if (event == TargetConfirmed)
            return false;
        player->removeTag("ThXujing");
        const QString key = "thxujing_grants_" + player->objectName();
        foreach (ServerPlayer *p, room->getAllPlayers(true)) {
            revokeTracked(room, p, key);
            room->setPlayerMark(p, "@xujing_bad", 0);
            room->setPlayerMark(p, "@xujing_good", 0);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetConfirmed || !player || !player->isAlive() || !player->hasSkill(objectName())
            || !player->hasSkill("thlingyun") || isOwnTurn(player))
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.to.contains(player))
            return TriggerList();
        if ((use.card->isNDTrick() && use.card->isBlack())
            || ((use.card->isKindOf("Peach") || use.card->isKindOf("Analeptic")) && use.from == player))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QStringList choices;
        foreach (const Skill *skill, player->getVisibleSkillList()) {
            if (!skill->isAttachedLordSkill() && !choices.contains(skill->objectName()))
                choices << skill->objectName();
        }
        if (choices.isEmpty())
            return false;
        const QString choice = room->askForChoice(player, objectName(), choices.join("+"));
        room->detachSkillFromPlayer(player, choice);
        player->drawCards(1, objectName());
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.from || !use.from->isAlive())
            return false;
        const QString key = "thxujing_grants_" + player->objectName();
        if (use.card->isNDTrick()) {
            grantTracked(room, use.from, key, "thhuanzang");
            grantTracked(room, use.from, key, "thanyue");
            room->setPlayerMark(use.from, "@xujing_bad", 1);
        } else {
            grantTracked(room, use.from, key, "thjifeng");
            grantTracked(room, use.from, key, "thxijing");
            room->setPlayerMark(use.from, "@xujing_good", 1);
        }
        player->setTag("ThXujing", true);
        return false;
    }
};

const QMap<QString, QString> &lingyunSkills()
{
    static const QMap<QString, QString> map{
        {"ex_nihilo", "benghuai"}, {"fire_attack", "thsanling"}, {"dismantlement", "ikxinshang"}, {"iron_chain", "ikjinlian"}};
    return map;
}

class ThLingyun : public ViewAsSkillV2
{
public:
    ThLingyun() : ViewAsSkillV2("thlingyun", 1) {}

    SkillDialogInfo getDialogInfo() const override
    {
        return SkillDialogInfo::guhuo(objectName(), false, true, true);
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || self->isNude())
            return false;
        foreach (const QString &name, lingyunSkills().keys())
            if (allowDeclaration(self, name))
                return true;
        return false;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || card->hasFlag("using") || !request.selectedCardIds.isEmpty())
            return false;
        const int id = card->getEffectiveId();
        return (self->handCards().contains(id) || self->getEquipsId().contains(id)) && self->canDiscard(self, id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    // The selected card is discarded as the cost; the trick itself has no material.
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || !cardSelectionFeasible(request))
            return false;
        DummyCard dummy(request.selectedCardIds);
        CardMoveReason reason(CardMoveReason::S_REASON_THROW, ctx.initiator->objectName(), QString(), objectName(), QString());
        room->throwCard(&dummy, reason, ctx.initiator);
        return true;
    }

    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.use_card || !ctx.invoker)
            return FinishSkill;
        const QString name = ctx.use_card->objectName();
        const QString skill = lingyunSkills().value(name);
        if (skill.isEmpty())
            return FinishSkill;
        if (!ctx.invoker->hasSkill(skill, true))
            ctx.invoker->getRoom()->acquireSkillFromEffect(ctx.invoker, skill, ctx);
        Card *card = Sanguosha->cloneCard(name, Card::NoSuit, 0);
        if (!card)
            return FinishSkill;
        card->setSkillName("_thlingyun");
        card->setCanRecast(false);
        card->deleteLater();
        ctx.updated_card = card;
        return ContinueEffects;
    }

protected:
    bool allowDeclaration(const Player *player, const QString &name) const override
    {
        const QString skill = lingyunSkills().value(name);
        if (skill.isEmpty() || !player || player->hasSkill(skill, true))
            return false;
        QScopedPointer<Card> card(Sanguosha->cloneCard(name, Card::NoSuit, 0));
        if (!card)
            return false;
        card->setSkillName(objectName());
        return !player->isCardLimited(card.data(), Card::MethodUse) && card->isAvailable(player);
    }
};

class ThZhaoai : public TriggerSkillV2
{
public:
    ThZhaoai() : TriggerSkillV2("thzhaoai")
    {
        events << Death;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DeathStruct death = data.value<DeathStruct>();
        if (!player || death.who != player || !player->hasSkill(objectName(), true) || !death.damage || !death.damage->from
            || death.damage->from == player)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const QList<ServerPlayer *> others = room->getOtherPlayers(player);
        if (others.isEmpty())
            return false;
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(player, objectName());
        ServerPlayer *target = room->askForPlayerChosen(player, others, objectName(), "@thzhaoai");
        if (!target)
            target = others.first();
        int n = 0;
        foreach (const Skill *skill, player->getVisibleSkillList())
            if (!skill->isAttachedLordSkill())
                ++n;
        const QStringList gifts{"thshenbao", "thyuhuo"};
        foreach (const QString &skill, gifts)
            if (!target->hasSkill(skill, true))
                room->acquireSkill(target, skill);
        if (n > 0 && target->isAlive())
            target->drawCards(n, objectName());
        return false;
    }
};

class IkXinshang : public TriggerSkillV2
{
public:
    IkXinshang() : TriggerSkillV2("ikxinshang")
    {
        events << Damaged;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || !player->isAlive() || !player->hasSkill(objectName()) || !damage.card
            || !damage.card->isKindOf("Slash") || damage.card->getSuit() == Card::Club)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->loseMaxHp(ctx.owner, 1, objectName());
        return false;
    }
};

// While the owner holds more cards than its HP, a 杀 by someone in whose range it stands
// can target nobody but the owner.
class IkJinlian : public ProhibitSkill
{
public:
    IkJinlian() : ProhibitSkill("ikjinlian") {}

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        if (!from || !to || !card || !card->isKindOf("Slash"))
            return false;
        foreach (const Player *p, from->getAliveSiblings()) {
            if (p != to && p->hasSkill(objectName()) && p->getHandcardNum() > p->getHp() && from->inMyAttackRange(p))
                return true;
        }
        return false;
    }
};

// ---------------------------------------------------------------- kami012

class ThWunan : public TriggerSkillV2
{
public:
    ThWunan() : TriggerSkillV2("thwunan")
    {
        events << CardUsed << CardResponded << HpRecover << Damaged << DamageCaused;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive())
            return result;
        bool hit = false;
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            hit = use.from == player && use.card
                && (use.card->isKindOf("GodSalvation") || use.card->isKindOf("AmazingGrace")
                    || (use.card->isKindOf("Jink") && use.card->getSkillName() == "eight_diagram"));
        } else if (event == CardResponded) {
            const CardResponseStruct resp = data.value<CardResponseStruct>();
            hit = resp.m_card && resp.m_card->isKindOf("Jink") && resp.m_card->getSkillName() == "eight_diagram";
        } else if (event == HpRecover) {
            hit = player->hasFlag("Global_Dying") && player->getHp() >= 1;
        } else if (event == Damaged) {
            hit = data.value<DamageStruct>().nature == DamageStruct::Fire;
        } else if (event == DamageCaused) {
            const DamageStruct damage = data.value<DamageStruct>();
            hit = damage.from == player && damage.card && damage.card->isKindOf("Slash") && damage.to
                && damage.to->isKongcheng();
        }
        if (!hit)
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && owner->getMark("thwunan-Clear") == 0)
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (ctx.owner->getMark("thwunan-Clear") > 0
            || !ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->addPlayerMark(ctx.owner, "thwunan-Clear");
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        QStringList choices{"draw"};
        if (player->isAlive() && owner->canDiscard(player, "he"))
            choices << "throw";
        if (room->askForChoice(owner, objectName(), choices.join("+"), QVariant::fromValue(player)) == "throw") {
            const int id = room->askForCardChosen(owner, player, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, player, owner);
        } else {
            owner->drawCards(1, objectName());
        }
        if (!owner->isAlive() || !owner->isWounded())
            return false;
        foreach (ServerPlayer *p, room->getOtherPlayers(owner))
            if (p->getHp() < owner->getHp())
                return false;
        if (owner->askForSkillInvoke("thwunan_recover", "yes"))
            room->recover(owner, RecoverStruct(objectName(), owner));
        return false;
    }
};

// ---------------------------------------------------------------- kami013

class ThSanling : public TriggerSkillV2
{
public:
    ThSanling() : TriggerSkillV2("thsanling")
    {
        events << EventPhaseChanging;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (data.value<PhaseChangeStruct>().to != Player::NotActive)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && owner->isKongcheng())
                result[owner] << objectName();
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->isAlive() || !ctx.owner->isKongcheng())
            return false;
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->killPlayer(ctx.owner);
        return false;
    }
};

class ThBingzhang : public TriggerSkillV2
{
public:
    ThBingzhang() : TriggerSkillV2("thbingzhang")
    {
        events << DamageForseen << PreHpLost;
        view_as_skill = new SelectionViewAs("thbingzhang", "@@thbingzhang", 2, 2, true);
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getCardCount() < 2)
            return TriggerList();
        if (event == DamageForseen && data.value<DamageStruct>().to != player)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForUseCard(ctx.owner, "@@thbingzhang", "@thbingzhang", -1, Card::MethodDiscard) != nullptr;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }
};

class ThJiwu : public FilterSkill
{
public:
    ThJiwu() : FilterSkill("thjiwu") {}

    bool viewFilter(const Card *to_select) const override
    {
        return to_select->isKindOf("Peach") || to_select->isKindOf("Analeptic");
    }

    const Card *viewAs(const Card *original) const override
    {
        Jink *jink = new Jink(original->getSuit(), original->getNumber());
        jink->setSkillName(objectName());
        return jink;
    }
};

class ThJiwuDraw : public TriggerSkillV2
{
public:
    ThJiwuDraw() : TriggerSkillV2("#thjiwu")
    {
        events << CardsMoveOneTime;
        frequency = Compulsory;
    }

    static int count(const ServerPlayer *player, const CardsMoveOneTimeStruct &move)
    {
        int n = 0;
        for (int i = 0; i < move.card_ids.length(); ++i) {
            const Player::Place from = move.from_places.value(i);
            const Card *card = Sanguosha->getEngineCard(move.card_ids.at(i));
            if ((from == Player::PlaceHand && move.to && move.to != player && move.to_place == Player::PlaceHand)
                || (move.to_place == Player::DiscardPile && from != Player::PlaceJudge && from != Player::PlaceSpecial
                    && (card->isKindOf("Jink") || card->isKindOf("Peach") || card->isKindOf("Analeptic"))))
                ++n;
        }
        return n;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player || move.from != player || !player->isAlive() || !player->hasSkill("thjiwu") || isOwnTurn(player)
            || count(player, move) == 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int n = count(ctx.owner, ctx.original_data->value<CardsMoveOneTimeStruct>());
        room->sendCompulsoryTriggerLog(ctx.owner, "thjiwu");
        ctx.owner->drawCards(n, "thjiwu");
        return false;
    }
};

class ThSisui : public TriggerSkillV2
{
public:
    ThSisui() : TriggerSkillV2("thsisui") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start)
            return TriggerList();
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (!p->isKongcheng())
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        QList<int> ids;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->isKongcheng() || !p->canDiscard(p, "h"))
                continue;
            const Card *card = room->askForCard(p, ".|.|.|hand", "@thsisui", QVariant(), Card::MethodDiscard);
            if (!card) {
                card = p->getRandomHandCard();
                if (!card)
                    continue;
                room->throwCard(card, p);
            }
            ids << card->getEffectiveId();
        }
        QList<int> available;
        foreach (int id, ids)
            if (room->getCardPlace(id) == Player::DiscardPile)
                available << id;
        if (available.isEmpty() || !player->isAlive())
            return false;
        QList<int> gain;
        room->fillAG(available, nullptr);
        for (int i = 0; i < 2 && !available.isEmpty(); ++i) {
            const int id = room->askForAG(player, available, true, objectName());
            if (!available.contains(id))
                break;
            room->takeAG(player, id, false);
            available.removeOne(id);
            gain << id;
        }
        room->clearAG();
        if (gain.isEmpty())
            return false;
        DummyCard dummy(gain);
        room->obtainCard(player, &dummy);
        QList<int> give;
        foreach (int id, gain)
            if (room->getCardOwner(id) == player && room->getCardPlace(id) == Player::PlaceHand)
                give << id;
        while (!give.isEmpty() && player->isAlive()) {
            if (!room->askForYiji(player, give, objectName(), false, true, true, -1, room->getOtherPlayers(player)))
                break;
        }
        return false;
    }
};

class ThZhanying : public TriggerSkillV2
{
public:
    ThZhanying() : TriggerSkillV2("thzhanying")
    {
        events << EventPhaseChanging;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || data.value<PhaseChangeStruct>().to != Player::Draw || player->isSkipped(Player::Draw))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        ctx.owner->skip(Player::Draw);
        return false;
    }
};

class ThZhanyingMaxCards : public MaxCardsSkillV2
{
public:
    ThZhanyingMaxCards() : MaxCardsSkillV2("#thzhanying") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.primary->hasSkill("thzhanying"))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(4);
    }
};

// ---------------------------------------------------------------- kami014

class ThLuli : public TriggerSkillV2
{
public:
    ThLuli() : TriggerSkillV2("thluli") { events << DamageCaused << DamageInflicted; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !player->canDiscard(player, "h"))
            return TriggerList();
        const DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *other = event == DamageCaused ? damage.to : damage.from;
        if ((event == DamageCaused && damage.from != player) || (event == DamageInflicted && damage.to != player))
            return TriggerList();
        if (!other || other == player || !other->isAlive() || qMax(other->getHp(), 0) < player->getHp())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (event == DamageCaused)
            return room->askForCard(ctx.owner, ".|black|.|hand", "@thluli-increase:" + damage.to->objectName(),
                                    *ctx.original_data, objectName());
        return room->askForCard(ctx.owner, ".|red|.|hand", "@thluli-decrease:" + damage.from->objectName(),
                                *ctx.original_data, objectName());
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        LogMessage log;
        log.type = event == DamageCaused ? "#ThLuliIncrease" : "#ThLuliDecrease";
        log.from = ctx.owner;
        log.arg = QString::number(damage.damage);
        damage.damage += event == DamageCaused ? 1 : -1;
        log.arg2 = QString::number(damage.damage);
        room->sendLog(log);
        *ctx.original_data = QVariant::fromValue(damage);
        return damage.damage < 1;
    }
};

class ThGuihuan : public TriggerSkillV2
{
public:
    ThGuihuan() : TriggerSkillV2("thguihuan")
    {
        events << BeforeGameOverJudge;
        frequency = Limited;
        limit_mark = "@guihuan";
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !isNormalGameMode(room->getMode()))
            return TriggerList();
        const DeathStruct death = data.value<DeathStruct>();
        ServerPlayer *killer = death.damage ? death.damage->from : nullptr;
        if (death.who != player || !killer || killer == player || !killer->isAlive() || killer->isLord() || player->isLord()
            || !killer->hasSkill(objectName()) || killer->getMark(limit_mark) == 0)
            return TriggerList();
        return TriggerList{{killer, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    // The killer takes the victim's role card before it is revealed.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *killer = ctx.owner;
        room->removePlayerMark(killer, limit_mark);
        room->doSuperLightbox(killer, objectName());
        const QString mine = killer->getRole();
        killer->setRole(player->getRole());
        room->notifyProperty(killer, killer, "role", player->getRole());
        room->setPlayerProperty(player, "role", mine);
        return false;
    }
};

// ---------------------------------------------------------------- kami015

class ThZhizun : public TriggerSkillV2
{
public:
    ThZhizun() : TriggerSkillV2("thzhizun") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || (player->getPhase() != Player::Start && player->getPhase() != Player::Finish))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@thzhizun", true);
        if (target) {
            QStringList kingdoms = Sanguosha->getKingdoms();
            kingdoms.removeAll("god");
            const QString old = target->getKingdom();
            kingdoms.removeAll(old);
            if (!kingdoms.isEmpty()) {
                const QString kingdom = room->askForChoice(player, "thzhizun_kingdom", kingdoms.join("+"),
                                                           QVariant::fromValue(target));
                room->setPlayerProperty(target, "kingdom", kingdom);
                LogMessage log;
                log.type = "#ChangeKingdom";
                log.from = player;
                log.to << target;
                log.arg = old;
                log.arg2 = kingdom;
                room->sendLog(log);
            }
        }
        // Upstream's eight lord skills; the 異界 ones join once ported.
        static const QStringList lordSkills{"ikxinqi", "ikhuanwei", "ikjiyuan", "ikyuji",
                                            "thhuazhi", "iksongwei", "thchundu", "ikwuhua"};
        QStringList choices;
        foreach (const QString &name, lordSkills) {
            if (!Sanguosha->getSkill(name) || player->hasSkill(name, true))
                continue;
            bool held = false;
            foreach (ServerPlayer *p, room->getAlivePlayers())
                if (p->hasLordSkill(name)) {
                    held = true;
                    break;
                }
            if (!held)
                choices << name;
        }
        if (choices.isEmpty() || !player->isAlive())
            return false;
        choices << "cancel";
        const QString choice = room->askForChoice(player, "thzhizun_lordskills", choices.join("+"));
        if (choice != "cancel")
            room->acquireSkillFromEffect(player, choice, ctx);
        return false;
    }
};

// ---------------------------------------------------------------- kami016

class ThLijian : public TriggerSkillV2
{
public:
    ThLijian() : TriggerSkillV2("thlijian")
    {
        events << EventPhaseStart;
        frequency = Limited;
        limit_mark = "@lijian";
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start
            || player->isKongcheng() || player->getMark(limit_mark) == 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        room->removePlayerMark(ctx.owner, limit_mark);
        room->doSuperLightbox(ctx.owner, objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (player->isKongcheng())
            return false;
        const Card *card = room->askForCardShow(player, player, "@thlijian-show");
        if (!card || card->getEffectiveId() < 0)
            card = player->getRandomHandCard();
        if (!card)
            return false;
        room->showCard(player, card->getEffectiveId());
        const QString pattern = typeClass(card);
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!player->isAlive())
                break;
            const Card *given = room->askForCard(p, pattern, "@thlijian-give:" + player->objectName(), QVariant(),
                                                 Card::MethodNone);
            if (given)
                room->giveCard(p, player, given, objectName(), true);
            else
                room->damage(DamageStruct(objectName(), player, p));
        }
        return false;
    }
};

class ThSiqiangViewAs : public ViewAsSkillV2
{
public:
    ThSiqiangViewAs() : ViewAsSkillV2("thsiqiang")
    {
        frequency = Limited;
        limit_mark = "@siqiang";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark(limit_mark) > 0;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThSiqiangCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator || ctx.initiator->getMark(limit_mark) <= 0)
            return false;
        room->removePlayerMark(ctx.initiator, limit_mark);
        room->doSuperLightbox(ctx.initiator, objectName());
        return true;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !target || !target->isAlive())
            return ContinueEffects;
        ctx.invoker->setTag("ThSiqiangTarget", target->objectName());
        target->getRoom()->setPlayerCardLimitation(target, "use,response", ".", false, objectName());
        return ContinueEffects;
    }
};

class ThSiqiang : public TriggerSkillV2
{
public:
    ThSiqiang() : TriggerSkillV2("thsiqiang")
    {
        events << EventPhaseChanging << Death;
        view_as_skill = new ThSiqiangViewAs;
        frequency = Limited;
        limit_mark = "@siqiang";
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->getTag("ThSiqiangTarget").toString().isEmpty())
            return false;
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive)
            return false;
        if (event == Death && data.value<DeathStruct>().who != player)
            return false;
        ServerPlayer *target = room->findPlayerByObjectName(player->getTag("ThSiqiangTarget").toString(), true);
        player->removeTag("ThSiqiangTarget");
        if (target)
            room->removePlayerCardLimitationByReason(target, objectName());
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class ThJiefuViewAs : public ViewAsSkillV2
{
public:
    ThJiefuViewAs() : ViewAsSkillV2("thjiefu")
    {
        frequency = Limited;
        limit_mark = "@jiefu";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark(limit_mark) > 0;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && to != request.initiator;
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThJiefuCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator || ctx.initiator->getMark(limit_mark) <= 0)
            return false;
        room->removePlayerMark(ctx.initiator, limit_mark);
        room->doSuperLightbox(ctx.initiator, objectName());
        return true;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        if (target->hasEquip()) {
            DummyCard dummy(target->getEquipsId());
            room->throwCard(&dummy, target, source);
        }
        if (!target->isAlive())
            return ContinueEffects;
        room->addSkillInvalidity(target, "all", source->objectName(), objectName());
        room->setPlayerMark(target, "@jiefu_null", 1);
        return ContinueEffects;
    }
};

class ThJiefu : public TriggerSkillV2
{
public:
    ThJiefu() : TriggerSkillV2("thjiefu")
    {
        events << EventPhaseChanging << Death;
        view_as_skill = new ThJiefuViewAs;
        frequency = Limited;
        limit_mark = "@jiefu";
    }

    // Until the end of this turn.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive)
            return false;
        if (event == Death && (!player || data.value<DeathStruct>().who != player || player != room->getCurrent()))
            return false;
        foreach (ServerPlayer *p, room->getAllPlayers(true)) {
            if (p->getMark("@jiefu_null") == 0)
                continue;
            room->setPlayerMark(p, "@jiefu_null", 0);
            foreach (ServerPlayer *source, room->getAllPlayers(true))
                room->removeSkillInvalidity(p, "all", source->objectName(), objectName());
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class ThHuanxiang : public TriggerSkillV2
{
public:
    ThHuanxiang() : TriggerSkillV2("thhuanxiang")
    {
        events << EventPhaseStart << DamageForseen;
        frequency = Limited;
        limit_mark = "@huanxiang";
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventPhaseStart && player && player->getPhase() == Player::RoundStart && player->getMark("@quiet") > 0)
            room->setPlayerMark(player, "@quiet", 0);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != DamageForseen || !player || data.value<DamageStruct>().to != player || !player->isAlive())
            return TriggerList();
        if (player->getMark("@quiet") > 0
            || (player->hasSkill(objectName()) && player->getMark(limit_mark) > 0))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (player->getMark("@quiet") > 0)
            return true;
        if (!player->askForSkillInvoke(objectName(), *ctx.original_data))
            return false;
        room->removePlayerMark(player, limit_mark);
        room->doSuperLightbox(player, objectName());
        ctx.extra_data = true;
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        room->broadcastSkillInvoke(objectName());
        if (!ctx.extra_data.toBool()) {
            room->sendCompulsoryTriggerLog(player, objectName());
            return true;
        }
        if (player->isWounded() && player->getHp() < 3)
            room->recover(player, RecoverStruct(objectName(), player, 3 - player->getHp()));
        room->addPlayerMark(player, "@quiet");
        return true;
    }
};

class ThHuanxiangProhibit : public ProhibitSkill
{
public:
    ThHuanxiangProhibit() : ProhibitSkill("#thhuanxiang-prohibit") {}

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        return from && to && from != to && to->getMark("@quiet") > 0 && card && card->getTypeId() != Card::TypeSkill;
    }
};

// ---------------------------------------------------------------- kami017

// 转换技: the 山 side, then the 湖 side, alternately.
class ThShuangfeng : public ViewAsSkillV2
{
public:
    ThShuangfeng() : ViewAsSkillV2("thshuangfeng") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThShuangfengCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive())
            return FinishSkill;
        Room *room = source->getRoom();
        const bool lake = source->getMark("thshuangfeng_lake") > 0;
        room->setPlayerMark(source, "thshuangfeng_lake", lake ? 0 : 1);
        QList<int> ids;
        if (!lake) {
            ids = room->getNCards(4 + source->getMark("@mountain"), false);
        } else {
            ids = room->getDiscardPile();
            qsanShuffle(ids);
            ids = ids.mid(0, 4 + source->getMark("@lake"));
        }
        if (ids.isEmpty())
            return FinishSkill;
        CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, source->objectName(), objectName(), QString());
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable, turnover), true);
        QMap<QString, QList<int>> groups;
        foreach (int id, ids) {
            const Card *card = Sanguosha->getCard(id);
            const QString key = lake ? card->getSuitString() : card->getType();
            if (key == "skill" || key == "no_suit" || key.isEmpty())
                continue;
            groups[key] << id;
        }
        QList<int> picked;
        if (!groups.isEmpty()) {
            const QString choice = room->askForChoice(source, objectName(), groups.keys().join("+"),
                                                      QVariant(ListI2V(ids)));
            picked = groups.value(choice);
        }
        QList<int> onTable;
        foreach (int id, picked)
            if (room->getCardPlace(id) == Player::PlaceTable)
                onTable << id;
        if (!onTable.isEmpty()) {
            DummyCard dummy(onTable);
            if (lake) {
                room->obtainCard(source, &dummy, true);
            } else {
                CardMoveReason toPile(CardMoveReason::S_REASON_NATURAL_ENTER, source->objectName(), objectName(), QString());
                room->throwCard(&dummy, toPile, nullptr);
            }
        }
        QList<int> rest;
        foreach (int id, ids)
            if (room->getCardPlace(id) == Player::PlaceTable)
                rest << id;
        if (!rest.isEmpty()) {
            CardMoveReason put(CardMoveReason::S_REASON_PUT, source->objectName(), objectName(), QString());
            room->moveCardsAtomic(CardsMoveStruct(rest, nullptr, nullptr, Player::PlaceTable, Player::DrawPile, put), true);
            room->askForGuanxing(source, rest, Room::GuanxingDownOnly);
        }
        return FinishSkill;
    }
};

class ThXingyong : public TriggerSkillV2
{
public:
    ThXingyong() : TriggerSkillV2("thxingyong")
    {
        events << Damaged;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || !player->isAlive() || !player->hasSkill(objectName()) || damage.damage < 1)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return ctx.owner->askForSkillInvoke(objectName());
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const int n = ctx.original_data->value<DamageStruct>().damage;
        for (int i = 0; i < n && player->isAlive(); ++i) {
            if (i > 0 && !player->askForSkillInvoke(objectName()))
                break;
            room->broadcastSkillInvoke(objectName());
            player->gainMark(room->askForChoice(player, objectName(), "@mountain+@lake"));
        }
        return false;
    }
};

class ThZaishen : public TriggerSkillV2
{
public:
    ThZaishen() : TriggerSkillV2("thzaishen")
    {
        events << EventPhaseChanging << CardsMoveOneTime << SwappedPile;
        frequency = Limited;
        limit_mark = "@zaishen";
    }

    // Tracks which draw-pile cards each character put there.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == SwappedPile) {
            foreach (ServerPlayer *p, room->getAllPlayers(true))
                p->removeTag("ThZaishenCards");
            return false;
        }
        if (event != CardsMoveOneTime)
            return false;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        // Every player sees the move; record it once, on the first living player's dispatch.
        const QList<ServerPlayer *> alive = room->getAlivePlayers();
        if (alive.isEmpty() || player != alive.first())
            return false;
        if (move.from_places.contains(Player::DrawPile))
            foreach (ServerPlayer *p, room->getAllPlayers(true)) {
                QVariantList list = p->getTag("ThZaishenCards").toList();
                for (int i = 0; i < move.card_ids.length(); ++i)
                    if (move.from_places.value(i) == Player::DrawPile)
                        list.removeAll(move.card_ids.at(i));
                p->setTag("ThZaishenCards", list);
            }
        if (move.to_place == Player::DrawPile) {
            ServerPlayer *from = room->findPlayerByObjectName(move.reason.m_playerId);
            if (from) {
                QVariantList list = from->getTag("ThZaishenCards").toList();
                foreach (int id, move.card_ids)
                    if (!list.contains(id))
                        list << id;
                from->setTag("ThZaishenCards", list);
            }
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == SwappedPile) {
            if (player && player->hasSkill(objectName()) && player->getMark(limit_mark) == 0
                && player->getMark("thzaishen_used") > 0)
                result[player] << objectName();
            return result;
        }
        if (event != EventPhaseChanging || !player || !player->isAlive()
            || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return result;
        const QList<int> &pile = room->getDrawPile();
        if (pile.isEmpty())
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers()) {
            if (!owner->hasSkill(objectName()) || owner->getMark(limit_mark) == 0)
                continue;
            const QVariantList mine = owner->getTag("ThZaishenCards").toList();
            bool all = true;
            foreach (int id, pile)
                if (!mine.contains(id)) {
                    all = false;
                    break;
                }
            if (all && !usableNames(room, owner, player).isEmpty())
                result[owner] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == SwappedPile)
            return true;
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
            return false;
        room->broadcastSkillInvoke(objectName());
        room->removePlayerMark(ctx.owner, limit_mark);
        room->addPlayerMark(ctx.owner, "thzaishen_used");
        room->doSuperLightbox(ctx.owner, objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (event == SwappedPile) {
            // Reset when the draw pile is reshuffled.
            room->sendCompulsoryTriggerLog(owner, objectName());
            room->setPlayerMark(owner, "thzaishen_used", 0);
            room->setPlayerMark(owner, limit_mark, 1);
            return false;
        }
        const QStringList names = usableNames(room, owner, player);
        if (names.isEmpty())
            return false;
        const QString choice = room->askForChoice(owner, objectName(), names.join("+"));
        const QList<int> pile = room->getDrawPile();
        foreach (int id, pile) {
            if (!owner->isAlive() || !player->isAlive())
                break;
            if (room->getCardPlace(id) != Player::DrawPile)
                continue;
            const Card *card = Sanguosha->getCard(id);
            if (nameOf(card) != choice || !canUseOn(owner, player, card))
                continue;
            QList<ServerPlayer *> targets{player};
            if (card->isKindOf("Collateral")) {
                QList<ServerPlayer *> victims;
                foreach (ServerPlayer *victim, room->getOtherPlayers(player))
                    if (card->targetFilter(QList<const Player *>{player}, victim, owner))
                        victims << victim;
                if (victims.isEmpty())
                    continue;
                ServerPlayer *victim = room->askForPlayerChosen(owner, victims, objectName(),
                                                                "@thzaishen-collateral:" + player->objectName());
                if (!victim)
                    continue;
                targets << victim;
            }
            room->useCard(CardUseStruct(card, owner, targets), false);
        }
        return false;
    }

private:
    static QString nameOf(const Card *card)
    {
        return card->isKindOf("Slash") ? QStringLiteral("slash") : card->objectName();
    }

    static bool canUseOn(ServerPlayer *owner, ServerPlayer *target, const Card *card)
    {
        if (card->getTypeId() == Card::TypeEquip)
            return false;
        if (card->isKindOf("Slash") && !owner->canSlash(target, card, false))
            return false;
        if (card->isKindOf("DelayedTrick") && target->containsTrick(card->objectName()))
            return false;
        return !owner->isCardLimited(card, Card::MethodUse, false) && !owner->isProhibited(target, card);
    }

    static QStringList usableNames(Room *room, ServerPlayer *owner, ServerPlayer *target)
    {
        QStringList names;
        foreach (int id, room->getDrawPile()) {
            const Card *card = Sanguosha->getCard(id);
            const QString name = nameOf(card);
            if (!names.contains(name) && canUseOn(owner, target, card))
                names << name;
        }
        return names;
    }
};

// ---------------------------------------------------------------- kami018

class ThSanjie : public TriggerSkillV2
{
public:
    ThSanjie() : TriggerSkillV2("thsanjie")
    {
        events << GameStart << CardFinished << CardsMoveOneTime << HpRecover << Damaged;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player)
            return result;
        if (event == HpRecover) {
            foreach (ServerPlayer *p, room->getOtherPlayers(player))
                if (p->hasSkill(objectName()) && (in(p, "chikai") || in(p, "getsukai")))
                    result[p] << objectName();
            return result;
        }
        if (!player->isAlive() || !player->hasSkill(objectName()))
            return result;
        bool hit = false;
        if (event == GameStart) {
            hit = true;
        } else if (event == CardFinished) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from != player || !use.card)
                return result;
            if (use.card->isKindOf("BasicCard")) {
                if (use.card->isRed())
                    hit = in(player, "chikai") && canDiscardOther(room, player);
                else if (use.card->isBlack())
                    hit = in(player, "getsukai") || in(player, "ikai");
            } else if (use.card->getTypeId() != Card::TypeSkill) {
                if (use.card->isNDTrick() && in(player, "getsukai"))
                    hit = canDiscardOther(room, player);
                else
                    hit = in(player, "ikai");
            }
        } else if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (discarded(player, move))
                hit = in(player, "chikai") || (in(player, "ikai") && canDiscardOther(room, player));
            else if (gained(player, move))
                hit = in(player, "chikai");
        } else if (event == Damaged) {
            hit = data.value<DamageStruct>().to == player && (in(player, "getsukai") || in(player, "ikai"));
        }
        if (hit)
            result[player] << objectName();
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        room->sendCompulsoryTriggerLog(owner, objectName());
        if (event == GameStart) {
            enter(room, owner, "chikai");
        } else if (event == CardFinished) {
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (use.card->isKindOf("BasicCard")) {
                if (use.card->isRed()) {
                    discardOther(room, owner);
                } else if (in(owner, "getsukai")) {
                    owner->drawCards(1, objectName());
                } else if (in(owner, "ikai")) {
                    enter(room, owner, "chikai");
                }
            } else if (in(owner, "getsukai")) {
                discardOther(room, owner);
            } else if (in(owner, "ikai")) {
                enter(room, owner, "getsukai");
            }
        } else if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
            if (gained(owner, move) && !discarded(owner, move)) {
                enter(room, owner, "ikai");
            } else if (in(owner, "chikai")) {
                enter(room, owner, "getsukai");
            } else if (in(owner, "ikai")) {
                discardOther(room, owner);
            }
        } else if (event == HpRecover) {
            if (in(owner, "chikai"))
                owner->drawCards(1, objectName());
            else if (in(owner, "getsukai"))
                enter(room, owner, "chikai");
        } else if (event == Damaged) {
            if (in(owner, "getsukai"))
                enter(room, owner, "ikai");
            else if (in(owner, "ikai"))
                owner->drawCards(1, objectName());
        }
        return false;
    }

private:
    static bool in(const Player *player, const QString &state) { return player->getMark("@sanjie_" + state) > 0; }

    static void enter(Room *room, ServerPlayer *player, const QString &state)
    {
        const QStringList states{"chikai", "getsukai", "ikai"};
        foreach (const QString &s, states)
            room->setPlayerMark(player, "@sanjie_" + s, s == state ? 1 : 0);
    }

    static bool discarded(const ServerPlayer *player, const CardsMoveOneTimeStruct &move)
    {
        return move.from == player
            && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
            && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD;
    }

    static bool gained(const ServerPlayer *player, const CardsMoveOneTimeStruct &move)
    {
        if (move.to != player || move.to_place != Player::PlaceHand || !move.from || move.from == move.to)
            return false;
        foreach (Player::Place place, move.from_places)
            if (place == Player::PlaceHand || place == Player::PlaceEquip)
                return true;
        return false;
    }

    static bool canDiscardOther(Room *room, ServerPlayer *player)
    {
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->canDiscard(p, "he"))
                return true;
        return false;
    }

    void discardOther(Room *room, ServerPlayer *owner) const
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(owner))
            if (owner->canDiscard(p, "he"))
                targets << p;
        if (targets.isEmpty())
            return;
        ServerPlayer *target = room->askForPlayerChosen(owner, targets, objectName(), "@thsanjie");
        if (!target)
            return;
        const int id = room->askForCardChosen(owner, target, "he", objectName(), false, Card::MethodDiscard);
        if (id >= 0)
            room->throwCard(id, target, owner);
    }
};

// ---------------------------------------------------------------- kami019

class ThJiesha : public ViewAsSkillV2
{
public:
    ThJiesha() : ViewAsSkillV2("thjiesha") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || self->getEquips().isEmpty())
            return false;
        foreach (int id, self->getEquipsId())
            if (!self->canDiscard(self, id))
                return false;
        return true;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    // As many characters as equipment, the owner first.
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        const Player *self = request.initiator;
        return self && to && selected.length() < self->getEquips().length() && (!selected.isEmpty() || to == self);
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return request.initiator && selected.length() == request.initiator->getEquips().length()
            && selected.contains(request.initiator);
    }

    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThJieshaCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator || !ctx.initiator->hasEquip())
            return false;
        DummyCard dummy(ctx.initiator->getEquipsId());
        room->throwCard(&dummy, ctx.initiator);
        return true;
    }

    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source)
            return FinishSkill;
        Room *room = source->getRoom();
        QList<ServerPlayer *> alive;
        foreach (ServerPlayer *p, targets)
            if (p->isAlive())
                alive << p;
        room->drawCards(alive, 1, objectName());
        foreach (ServerPlayer *p, alive)
            if (p->isAlive())
                room->damage(DamageStruct(objectName(), source->isAlive() ? source : nullptr, p));
        return FinishSkill;
    }
};

class ThChuntie : public TriggerSkillV2
{
public:
    ThChuntie() : TriggerSkillV2("thchuntie") { events << BeforeCardsMove; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getMark("thchuntie-Clear") > 0
            || !player->getWeapon())
            return TriggerList();
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player)
            return TriggerList();
        const int weapon = player->getWeapon()->getEffectiveId();
        for (int i = 0; i < move.card_ids.length(); ++i)
            if (move.card_ids.at(i) == weapon && move.from_places.value(i) == Player::PlaceEquip
                && room->getCardOwner(weapon) == player)
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(),
                                                        "@thchuntie", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive() || !player->getWeapon())
            return false;
        room->addPlayerMark(player, "thchuntie-Clear");
        const int weapon = player->getWeapon()->getEffectiveId();
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        move.removeCardIds(QList<int>{weapon});
        *ctx.original_data = QVariant::fromValue(move);
        room->giveCard(player, target, QList<int>{weapon}, objectName(), true);
        player->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- kami020

const QStringList &moonPhases()
{
    static const QStringList phases{"newmoon", "quarter", "fullmoon", "lastquarter"};
    return phases;
}

QString currentMoon(const Player *owner)
{
    foreach (const QString &phase, moonPhases() + QStringList{"lunareclipse"})
        if (owner->getMark("@yuexiang_" + phase) > 0)
            return phase;
    return QString();
}

void setMoon(Room *room, ServerPlayer *owner, const QString &phase)
{
    foreach (const QString &p, moonPhases() + QStringList{"lunareclipse"})
        room->setPlayerMark(owner, "@yuexiang_" + p, p == phase ? 1 : 0);
}

bool fieldCardMovable(Room *room)
{
    foreach (ServerPlayer *sp, room->getAlivePlayers()) {
        foreach (const Card *c, sp->getJudgingArea())
            foreach (ServerPlayer *op, room->getOtherPlayers(sp))
                if (!op->containsTrick(c->objectName()))
                    return true;
        foreach (const Card *c, sp->getEquips()) {
            const EquipCard *equip = qobject_cast<const EquipCard *>(c->getRealCard());
            if (!equip)
                continue;
            foreach (ServerPlayer *op, room->getOtherPlayers(sp))
                if (!op->getEquip(equip->location()) && op->hasEquipArea(equip->location()))
                    return true;
        }
    }
    return false;
}

class ThYuexiang : public TriggerSkillV2
{
public:
    ThYuexiang() : TriggerSkillV2("thyuexiang")
    {
        events << GameStart << EventPhaseStart << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().from == Player::Play
            && player->hasFlag("ThYuexiangExtraPlay")) {
            room->setPlayerFlag(player, "-ThYuexiangExtraPlay");
            room->setPlayerFlag(player, "ThYuexiangExtraDone");
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player)
            return result;
        if (event == GameStart) {
            if (player->isAlive() && player->hasSkill(objectName()))
                result[player] << objectName();
        } else if (event == EventPhaseChanging) {
            // Each turn's end moves the moon on.
            if (data.value<PhaseChangeStruct>().to == Player::NotActive)
                foreach (ServerPlayer *owner, room->getAlivePlayers())
                    if (owner->hasSkill(objectName()) && !currentMoon(owner).isEmpty())
                        result[owner] << objectName();
        } else if (event == EventPhaseStart && player->getPhase() == Player::Play && player->isAlive()) {
            // The extra play phase of 下弦 is free of every moon.
            if (player->hasFlag("ThYuexiangExtraPlay"))
                return result;
            foreach (ServerPlayer *owner, room->getAlivePlayers()) {
                const QString moon = currentMoon(owner);
                if (!owner->hasSkill(objectName()) || moon.isEmpty())
                    continue;
                if (moon == "quarter" && !fieldCardMovable(room))
                    continue;
                if (moon == "lastquarter" && player->hasFlag("ThYuexiangExtraDone"))
                    continue;
                result[owner] << objectName();
            }
        }
        return result;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        room->sendCompulsoryTriggerLog(owner, objectName());
        if (event == GameStart) {
            setMoon(room, owner, "newmoon");
            return false;
        }
        const QString moon = currentMoon(owner);
        if (event == EventPhaseChanging) {
            if (moon == "lunareclipse") {
                setMoon(room, owner, "newmoon");
            } else {
                const int index = moonPhases().indexOf(moon);
                setMoon(room, owner, moonPhases().at((index + 1) % moonPhases().size()));
            }
            return false;
        }
        if (moon == "newmoon") {
            room->setPlayerFlag(player, "thyuexiang_newmoon");
        } else if (moon == "quarter") {
            moveFieldCard(room, player);
        } else if (moon == "fullmoon") {
            if (!player->isKongcheng())
                room->showAllCards(player);
            room->setPlayerFlag(player, "thyuexiang_fullmoon");
        } else if (moon == "lastquarter") {
            room->setPlayerFlag(player, "ThYuexiangExtraPlay");
            player->insertPhase(Player::Play);
        } else if (moon == "lunareclipse") {
            if (!player->isSkipped(Player::Discard))
                player->skip(Player::Discard);
            room->setPlayerFlag(player, "thyuexiang_lunareclipse");
        }
        return false;
    }

private:
    void moveFieldCard(Room *room, ServerPlayer *player) const
    {
        QList<ServerPlayer *> targets;
        QMap<ServerPlayer *, QList<int>> disabled;
        foreach (ServerPlayer *sp, room->getAlivePlayers()) {
            foreach (const Card *c, sp->getJudgingArea()) {
                bool ok = false;
                foreach (ServerPlayer *op, room->getOtherPlayers(sp))
                    if (!op->containsTrick(c->objectName()))
                        ok = true;
                if (ok && !targets.contains(sp))
                    targets << sp;
                if (!ok)
                    disabled[sp] << c->getEffectiveId();
            }
            foreach (const Card *c, sp->getEquips()) {
                const EquipCard *equip = qobject_cast<const EquipCard *>(c->getRealCard());
                bool ok = false;
                if (equip)
                    foreach (ServerPlayer *op, room->getOtherPlayers(sp))
                        if (!op->getEquip(equip->location()) && op->hasEquipArea(equip->location()))
                            ok = true;
                if (ok && !targets.contains(sp))
                    targets << sp;
                if (!ok)
                    disabled[sp] << c->getEffectiveId();
            }
        }
        if (targets.isEmpty())
            return;
        ServerPlayer *from = room->askForPlayerChosen(player, targets, objectName(), "@thyuexiang-move", true);
        if (!from)
            return;
        const int id = room->askForCardChosen(player, from, "ej", objectName(), false, Card::MethodNone, disabled.value(from));
        if (id < 0 || disabled.value(from).contains(id))
            return;
        const Card *card = Sanguosha->getCard(id);
        const Player::Place place = room->getCardPlace(id);
        const EquipCard *equip = place == Player::PlaceEquip ? qobject_cast<const EquipCard *>(card->getRealCard()) : nullptr;
        QList<ServerPlayer *> tos;
        foreach (ServerPlayer *p, room->getOtherPlayers(from)) {
            if (equip) {
                if (!p->getEquip(equip->location()) && p->hasEquipArea(equip->location()))
                    tos << p;
            } else if (!player->isProhibited(p, card) && !p->containsTrick(card->objectName())) {
                tos << p;
            }
        }
        if (tos.isEmpty())
            return;
        ServerPlayer *to = room->askForPlayerChosen(player, tos, objectName(), "@thyuexiang-to:::" + card->objectName());
        if (to)
            room->moveCardTo(card, from, to, place,
                             CardMoveReason(CardMoveReason::S_REASON_TRANSFER, player->objectName(), objectName(), QString()));
    }
};

class ThYuexiangProhibit : public ProhibitSkill
{
public:
    ThYuexiangProhibit() : ProhibitSkill("#thyuexiang-prohibit") {}

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        if (!from || !to || !card || !card->isKindOf("Slash"))
            return false;
        if (from->hasFlag("thyuexiang_lunareclipse"))
            return true;
        if (from->hasFlag("thyuexiang_newmoon"))
            foreach (const Player *p, from->getAliveSiblings())
                if (from->distanceTo(p) < from->distanceTo(to))
                    return true;
        return false;
    }
};

class ThYuexiangTargetMod : public TargetModSkillV2
{
public:
    ThYuexiangTargetMod() : TargetModSkillV2("#thyuexiang-tar", "Slash") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::DistanceLimit || !ctx.primary || !ctx.primary->hasFlag("thyuexiang_fullmoon"))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(1000);
    }
};

class ThMishu : public TriggerSkillV2
{
public:
    ThMishu() : TriggerSkillV2("thmishu") { events << EventPhaseStart; }

    LimitScope getLimitScope() const override { return Limit_Round; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->getPhase() != Player::Start)
            return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers())
            if (owner->hasSkill(objectName()) && !currentMoon(owner).isEmpty())
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || !ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx))
            return false;
        addUsage(ctx);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        setMoon(room, ctx.owner, "lunareclipse");
        return false;
    }
};

}

ThShenfengCard::ThShenfengCard() { setSkillName("thshenfeng"); mute = true; }
ThGugaoCard::ThGugaoCard() { setSkillName("thgugao"); mute = true; }
ThLeshiCard::ThLeshiCard() { setSkillName("thleshi"); mute = true; }
ThJingwuCard::ThJingwuCard() { setSkillName("thjingwu"); mute = true; }
ThYouyaCard::ThYouyaCard() { setSkillName("thyouya"); mute = true; }
ThJinluCard::ThJinluCard() { setSkillName("thjinlu"); mute = true; }
ThChuangxinCard::ThChuangxinCard() { setSkillName("thchuangxin"); mute = true; }
ThTianxinCard::ThTianxinCard() { setSkillName("thtianxin"); mute = true; }
ThBaihunCard::ThBaihunCard() { setSkillName("thbaihun"); mute = true; }
ThSiqiangCard::ThSiqiangCard() { setSkillName("thsiqiang"); mute = true; }
ThJiefuCard::ThJiefuCard() { setSkillName("thjiefu"); mute = true; }
ThShuangfengCard::ThShuangfengCard() { setSkillName("thshuangfeng"); mute = true; }
ThJieshaCard::ThJieshaCard() { setSkillName("thjiesha"); mute = true; }
ThBingzhangCard::ThBingzhangCard() { setSkillName("thbingzhang"); mute = true; }

TouhouKamiPackage::TouhouKamiPackage()
    : Package("touhou-kami")
{
    General *kami001 = new General(this, "kami001", "god", 3);
    kami001->addSkill(new ThKexing);
    kami001->addSkill(new ThShenfeng);
    kami001->addSkill(new ThKaihai);

    General *kami002 = new General(this, "kami002", "god", 4);
    kami002->addSkill(new ThTianbao);
    kami002->addSkill(new PendingSkill("thyanmeng"));

    General *kami003 = new General(this, "kami003", "god", 8);
    kami003->addSkill(new ThWudao);
    kami003->addSkill(new ThHuanjun);
    kami003->addSkill(new EquipToHand("#thhuanjun", "thhuanjun", "Armor"));
    related_skills.insert("thhuanjun", "#thhuanjun");
    kami003->addSkill("thyanmeng");

    General *kami004 = new General(this, "kami004", "god");
    kami004->addSkill(new ThGugao);
    kami004->addSkill(new ThQianyu);
    kami004->addRelateSkill("thkuangmo");
    kami004->addSkill(new ThHuangyi);

    General *kami005 = new General(this, "kami005", "god", 3, false);
    kami005->addSkill(new ThSuhu);
    kami005->addSkill(new ThFenlang);
    kami005->addSkill(new ThLeshi);
    kami005->addSkill(new ThYouli);
    kami005->addSkill(new ThYouliMaxCards);
    related_skills.insert("thyouli", "#thyouli");
    kami005->addSkill(new ThJingyuan);

    General *kami006 = new General(this, "kami006", "god", 3);
    kami006->addSkill(new ThPanghun);
    kami006->addSkill(new ThPanghunFilter);
    related_skills.insert("thpanghun", "#thpanghun-filter");
    kami006->addSkill(new ThJingwu);
    kami006->addSkill(new ThLunyu);

    General *kami007 = new General(this, "kami007", "god", 2, false);
    kami007->addSkill(new ThFanhun);
    kami007->addSkill(new ThYoushang);
    kami007->addSkill(new ThYouya);
    kami007->addSkill(new ThManxiao);
    kami007->addSkill(new ThManxiaoMaxCards);
    related_skills.insert("thmanxiao", "#thmanxiao");

    General *kami008 = new General(this, "kami008", "god", 3, false);
    kami008->addSkill(new ThJinlu);
    kami008->addSkill(new ThKuangli);

    // 灵视 is 攻心, 闭月 is 闭月, 虚视 is 观星; 天妒 differs and is ported.
    General *kami009 = new General(this, "kami009", "god");
    kami009->addSkill(new ThYuxin);
    kami009->addSkill(new ThPickSkills("thchuangxin", Player::Play, {"gongxin", "biyue"}));
    kami009->addSkill(new ThPickSkills("thtianxin", Player::RoundStart, {"guanxing", "iktiandu"}));
    kami009->addRelateSkill("gongxin");
    kami009->addRelateSkill("biyue");
    kami009->addRelateSkill("guanxing");
    kami009->addRelateSkill("iktiandu");

    // 沉红 is 英姿 and 飞影 is the local 飞影.
    General *kami010 = new General(this, "kami010", "god", 3, false);
    kami010->addSkill(new ThRangdeng);
    kami010->addSkill(new ThBaihun);
    kami010->addRelateSkill("biyue");
    kami010->addRelateSkill("feiying");
    kami010->addRelateSkill("yingzi");
    kami010->addRelateSkill("thjifeng");

    General *kami011 = new General(this, "kami011", "god");
    kami011->addSkill(new ThXujing);
    kami011->addSkill(new ThLingyun);
    kami011->addSkill(new ThZhaoai);

    General *kami012 = new General(this, "kami012", "god");
    kami012->addSkill(new ThWunan);

    General *kami013 = new General(this, "kami013", "god", 1, false);
    kami013->addSkill(new ThSanling);
    kami013->addSkill(new ThBingzhang);
    kami013->addSkill(new ThJiwu);
    kami013->addSkill(new ThJiwuDraw);
    related_skills.insert("thjiwu", "#thjiwu");
    kami013->addSkill(new ThSisui);
    kami013->addSkill(new ThZhanying);
    kami013->addSkill(new ThZhanyingMaxCards);
    related_skills.insert("thzhanying", "#thzhanying");

    General *kami014 = new General(this, "kami014", "god", 3, false);
    kami014->addSkill(new ThLuli);
    kami014->addSkill(new ThGuihuan);

    General *kami015 = new General(this, "kami015", "god");
    kami015->addSkill(new ThZhizun);
    kami015->addSkill("feiying");

    General *kami016 = new General(this, "kami016", "god", 3, false);
    kami016->addSkill(new ThLijian);
    kami016->addSkill(new ThSiqiang);
    kami016->addSkill(new ThJiefu);
    kami016->addSkill(new ThHuanxiang);
    kami016->addSkill(new ThHuanxiangProhibit);
    related_skills.insert("thhuanxiang", "#thhuanxiang-prohibit");

    General *kami017 = new General(this, "kami017", "god");
    kami017->addSkill(new ThShuangfeng);
    kami017->addSkill(new ThXingyong);
    kami017->addSkill(new ThZaishen);

    General *kami018 = new General(this, "kami018", "god", 3);
    kami018->addSkill(new ThSanjie);

    General *kami019 = new General(this, "kami019", "god");
    kami019->addSkill(new ThJiesha);
    kami019->addSkill(new PendingSkill("thmingren"));
    kami019->addSkill(new ThChuntie);

    General *kami020 = new General(this, "kami020", "god", 3);
    kami020->addSkill(new ThYuexiang);
    kami020->addSkill(new ThYuexiangProhibit);
    kami020->addSkill(new ThYuexiangTargetMod);
    related_skills.insert("thyuexiang", "#thyuexiang-prohibit");
    related_skills.insert("thyuexiang", "#thyuexiang-tar");
    kami020->addSkill(new ThMishu);

    skills << new ThKuangmo << new IkTiandu << new ThJifeng << new ThHuanzang << new IkXinshang << new IkJinlian;

    addMetaObject<ThShenfengCard>();
    addMetaObject<ThGugaoCard>();
    addMetaObject<ThLeshiCard>();
    addMetaObject<ThJingwuCard>();
    addMetaObject<ThYouyaCard>();
    addMetaObject<ThJinluCard>();
    addMetaObject<ThChuangxinCard>();
    addMetaObject<ThTianxinCard>();
    addMetaObject<ThBaihunCard>();
    addMetaObject<ThSiqiangCard>();
    addMetaObject<ThJiefuCard>();
    addMetaObject<ThShuangfengCard>();
    addMetaObject<ThJieshaCard>();
    addMetaObject<ThBingzhangCard>();
}

ADD_PACKAGE(TouhouKami)
