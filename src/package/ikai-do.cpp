#include "ikai-do.h"
#include "touhou-utils.h"
#include "engine.h"
#include "general.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "maneuvering.h"
#include "util.h"

using namespace TouhouUtils;

// TouhouTripleSha's 异界·土. Skills that match a local one are reused by name; the
// lord skills differ from their 三国 originals only by kingdom.

namespace {

bool ownsCard(const Player *self, const Card *card)
{
    if (!self || !card || card->hasFlag("using"))
        return false;
    const int id = card->getEffectiveId();
    return self->handCards().contains(id) || self->getEquipsId().contains(id);
}

bool hasLieges(const Player *lord, const QString &kingdom)
{
    foreach (const Player *p, lord->getAliveSiblings())
        if (p->getKingdom() == kingdom)
            return true;
    return false;
}

// ---------------------------------------------------------------- lord skills

// 心契: kaze characters answer the lord's 杀 (激将 with the kaze kingdom).
class IkXinqiViewAs : public ViewAsSkillV2
{
public:
    IkXinqiViewAs() : ViewAsSkillV2("ikxinqi$") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        if (!self || !hasLieges(self, "kaze")
            || self->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "failed", false).toBool())
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Slash::IsAvailable(self);
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && request.pattern.contains("slash", Qt::CaseInsensitive);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override { return request.selectedCardIds.isEmpty(); }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName(objectName());
        return slash;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }

    // A liege's 杀 becomes the lord's.
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        ServerPlayer *lord = ctx.invoker;
        if (!lord || !lord->isAlive())
            return false;
        foreach (ServerPlayer *liege, room->getLieges("kaze", lord)) {
            const Card *provided = room->askForCard(liege, "slash", "@ikxinqi-slash:" + lord->objectName(),
                                                    QVariant::fromValue(lord), Card::MethodResponse, lord, false, "", true);
            if (!provided)
                continue;
            Card *slash = Sanguosha->cloneCard(provided->objectName(), provided->getSuit(), provided->getNumber());
            if (!slash)
                return false;
            slash->addSubcard(provided);
            slash->setSkillName(objectName());
            slash->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
            slash->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
            slash->deleteLater();
            ctx.updated_card = slash;
            if (ctx.original_data && ctx.original_data->canConvert<CardUseStruct>()) {
                CardUseStruct use = ctx.original_data->value<CardUseStruct>();
                use.m_isOwnerUse = false;
                *ctx.original_data = QVariant::fromValue(use);
            }
            return true;
        }
        if (ctx.initiator)
            ctx.initiator->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "failed", true);
        return false;
    }
};

// One lord skill answering a response request through its lieges.
class LiegeProvider : public TriggerSkillV2
{
public:
    LiegeProvider(const QString &name, const QString &kingdom, const QString &pattern, bool responseOnly)
        : TriggerSkillV2(name + "$"), m_kingdom(kingdom), m_pattern(pattern), m_responseOnly(responseOnly)
    {
        events << CardAsked << EventPhaseChanging;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *lord, QVariant &data) const override
    {
        if (event != CardAsked || !lord || !lord->isAlive() || !lord->hasLordSkill(objectName()))
            return TriggerList();
        const QStringList asked = data.toStringList();
        if (asked.size() < 3 || asked.first() != m_pattern || asked.at(1).contains(objectName() + "-")
            || (m_responseOnly && asked.at(2) != "response") || room->getLieges(m_kingdom, lord).isEmpty())
            return TriggerList();
        return TriggerList{{lord, {objectName()}}};
    }

    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging && player == ctx.owner)
            ctx.owner->removeSkillInstanceStateValue(objectName(), ctx.instanceID, "failed");
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *lord = ctx.owner;
        room->broadcastSkillInvoke(objectName());
        foreach (ServerPlayer *liege, room->getLieges(m_kingdom, lord)) {
            const Card *card = room->askForCard(liege, m_pattern, "@" + objectName() + "-" + m_pattern + ":" + lord->objectName(),
                                                QVariant::fromValue(lord), Card::MethodResponse, lord, false, "", true);
            if (card) {
                room->provide(card);
                return true;
            }
        }
        return false;
    }

private:
    QString m_kingdom;
    QString m_pattern;
    bool m_responseOnly;
};

class IkXinqi : public LiegeProvider
{
public:
    IkXinqi() : LiegeProvider("ikxinqi", "kaze", "slash", true) { view_as_skill = new IkXinqiViewAs; }
};

// 济援: a yuki character's 桃 on itself may heal the lord instead, and it draws one.
class IkJiyuan : public TriggerSkillV2
{
public:
    IkJiyuan() : TriggerSkillV2("ikjiyuan$") { events << CardUsed; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || player->getKingdom() != "yuki" || !use.card
            || !use.card->isKindOf("Peach") || !use.to.contains(player))
            return result;
        foreach (ServerPlayer *lord, room->getOtherPlayers(player))
            if (lord->hasLordSkill(objectName()) && player->getHp() > qMax(0, lord->getHp()))
                result[lord] << objectName();
        return result;
    }

    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player->askForSkillInvoke(objectName(), QVariant::fromValue(ctx.owner)))
            return false;
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(ctx.owner, objectName());
        LogMessage log;
        log.type = "#InvokeOthersSkill";
        log.from = player;
        log.to << ctx.owner;
        log.arg = objectName();
        room->sendLog(log);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->recover(ctx.owner, RecoverStruct(objectName(), player));
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.nullified_list.contains(player->objectName()))
            use.nullified_list << player->objectName();
        *ctx.original_data = QVariant::fromValue(use);
        if (player->isAlive())
            player->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- wind002 / wind003

class IkZhenhong : public TriggerSkillV2
{
public:
    IkZhenhong() : TriggerSkillV2("ikzhenhong")
    {
        events << TargetSpecified;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card
            || !use.card->isKindOf("Slash") || use.card->getSuit() != Card::Diamond)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        foreach (ServerPlayer *p, use.to)
            p->addQinggangTag(use.card);
        return false;
    }
};

class IkZhenhongTargetMod : public TargetModSkillV2
{
public:
    IkZhenhongTargetMod() : TargetModSkillV2("#ikzhenhong-target", "Slash") { frequency = Compulsory; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::DistanceLimit || !ctx.primary || !ctx.card || ctx.card->getSuit() != Card::Heart
            || !ctx.primary->hasSkill("ikzhenhong"))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(1000);
    }
};

class IkShijiu : public ViewAsSkillV2
{
public:
    IkShijiu() : ViewAsSkillV2("ikshijiu", 1) { setResponseOrUse(true); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator)
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Analeptic::IsAvailable(request.initiator);
        return request.pattern.contains("analeptic", Qt::CaseInsensitive);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsCard(request.initiator, card)
            && (card->isKindOf("Weapon") || card->isNDTrick());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1)
            return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        Analeptic *card = new Analeptic(material->getSuit(), material->getNumber());
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }
};

// ---------------------------------------------------------------- wind006

// Per target: its non-locked skills stop until the turn ends, then it discards a card of the
// judge suit or cannot dodge.
class IkYufeng : public TriggerSkillV2
{
public:
    IkYufeng() : TriggerSkillV2("ikyufeng")
    {
        events << TargetSpecified << EventPhaseChanging;
        frequency = Frequent;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return false;
        foreach (ServerPlayer *p, room->getAllPlayers(true)) {
            const QStringList blocked = p->getTag("IkYufengBlocked").toStringList();
            if (blocked.isEmpty())
                continue;
            p->removeTag("IkYufengBlocked");
            foreach (const QString &entry, blocked) {
                const QStringList parts = entry.split("|");
                if (parts.size() == 2)
                    room->removeSkillInvalidity(p, parts.first(), parts.last(), objectName());
            }
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetSpecified)
            return TriggerList();
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card
            || !use.card->isKindOf("Slash") || use.to.isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &) const override { return true; }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        bool changed = false;
        foreach (ServerPlayer *target, use.to) {
            if (!player->isAlive() || !target->isAlive()
                || !player->askForSkillInvoke(objectName(), QVariant::fromValue(target)))
                continue;
            room->broadcastSkillInvoke(objectName());
            QStringList blocked = target->getTag("IkYufengBlocked").toStringList();
            foreach (const Skill *skill, target->getVisibleSkillList()) {
                if (skill->getFrequency() == Skill::Compulsory)
                    continue;
                const QString entry = skill->objectName() + "|" + player->objectName();
                if (blocked.contains(entry))
                    continue;
                room->addSkillInvalidity(target, skill->objectName(), player->objectName(), objectName());
                blocked << entry;
            }
            target->setTag("IkYufengBlocked", blocked);
            JudgeStruct judge;
            judge.pattern = ".";
            judge.good = true;
            judge.reason = objectName();
            judge.who = player;
            judge.play_animation = false;
            room->judge(judge);
            const QString suit = judge.card ? judge.card->getSuitString() : QString();
            if (target->isAlive() && target->canDiscard(target, "he") && !suit.isEmpty()
                && room->askForCard(target, ".|" + suit, "@ikyufeng-discard:::" + suit, *ctx.original_data,
                                    Card::MethodDiscard))
                continue;
            if (!use.no_respond_list.contains(target->objectName())) {
                LogMessage log;
                log.type = "#NoJink";
                log.from = target;
                room->sendLog(log);
                use.no_respond_list << target->objectName();
                changed = true;
            }
        }
        if (changed)
            *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

// ---------------------------------------------------------------- wind042

class IkYehua : public WakeSkill
{
public:
    IkYehua() : WakeSkill("ikyehua")
    {
        events << Damage;
        waked_skills = "ikxingyu";
    }

protected:
    bool atTiming(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return event == Damage && data.value<DamageStruct>().from == player;
    }

    bool canAwaken(Room *, ServerPlayer *player) const override { return player->isWounded(); }

    void awaken(Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (room->changeMaxHpForAwakenSkill(player, -1, objectName()) && player->isAlive())
            room->acquireSkillFromEffect(player, "ikxingyu", ctx);
    }
};

class IkXingyu : public ViewAsSkillV2
{
public:
    IkXingyu() : ViewAsSkillV2("ikxingyu") { setPhaseName("Play"); }

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

    QString historyKey(const ActiveSkillRequest &) const override { return "IkXingyuCard"; }

    // Reveal until a card of the named type or colour turns up; it goes to someone.
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive())
            return FinishSkill;
        Room *room = source->getRoom();
        const QStringList choices{"basic", "trick", "equip", "red", "black"};
        const QStringList patterns{"BasicCard", "TrickCard", "EquipCard", ".|red", ".|black"};
        const QString choice = room->askForChoice(source, objectName(), choices.join("+"));
        const QString pattern = patterns.value(choices.indexOf(choice), "BasicCard");
        LogMessage log;
        log.type = "#IkXingyuChoice";
        log.from = source;
        log.arg = choice;
        room->sendLog(log);
        QList<int> revealed;
        int hit = -1;
        for (int guard = 0; guard < 200; ++guard) {
            const QList<int> ids = room->getNCards(1, false);
            if (ids.isEmpty())
                break;
            const int id = ids.first();
            revealed << id;
            CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, source->objectName(), objectName(), QString());
            room->moveCardsAtomic(CardsMoveStruct(id, nullptr, Player::PlaceTable, turnover), true);
            if (Sanguosha->matchExpPattern(pattern, nullptr, Sanguosha->getCard(id))) {
                hit = id;
                break;
            }
        }
        if (hit >= 0) {
            revealed.removeOne(hit);
            ServerPlayer *target = room->askForPlayerChosen(source, room->getAlivePlayers(), objectName(), "@ikxingyu-give");
            if (target && room->getCardPlace(hit) == Player::PlaceTable)
                room->obtainCard(target, hit);
            else if (room->getCardPlace(hit) == Player::PlaceTable)
                revealed << hit;
        }
        QList<int> rest;
        foreach (int id, revealed)
            if (room->getCardPlace(id) == Player::PlaceTable)
                rest << id;
        if (!rest.isEmpty()) {
            DummyCard dummy(rest);
            CardMoveReason toPile(CardMoveReason::S_REASON_NATURAL_ENTER, source->objectName(), objectName(), QString());
            room->throwCard(&dummy, toPile, nullptr);
        }
        return FinishSkill;
    }
};

// ---------------------------------------------------------------- bloom001 / bloom002

class IkJiaoman : public TriggerSkillV2
{
public:
    IkJiaoman() : TriggerSkillV2("ikjiaoman") { events << Damaged; }

    static bool onTable(Room *room, const Card *card)
    {
        if (!card)
            return false;
        const QList<int> ids = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
        if (ids.isEmpty())
            return false;
        foreach (int id, ids)
            if (room->getCardPlace(id) != Player::PlaceTable)
                return false;
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || !player->isAlive() || !player->hasSkill(objectName())
            || !onTable(room, damage.card))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const QList<ServerPlayer *> targets = damage.from ? room->getOtherPlayers(damage.from) : room->getAlivePlayers();
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "@ikjiaoman", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (target && target->isAlive() && onTable(room, damage.card))
            room->obtainCard(target, damage.card);
        return false;
    }
};

class IkHuanwei : public LiegeProvider
{
public:
    IkHuanwei() : LiegeProvider("ikhuanwei", "hana", "jink", false) {}
};

class IkZhimen : public TriggerSkillV2
{
public:
    IkZhimen() : TriggerSkillV2("ikzhimen")
    {
        events << BeforeCardsMove;
        frequency = Limited;
        limit_mark = "@zhimen";
    }

    static QList<int> tricks(Room *room, const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        if (move.to_place != Player::DiscardPile)
            return ids;
        foreach (int id, move.card_ids) {
            const Card *card = Sanguosha->getEngineCard(id);
            if (!card->isKindOf("DelayedTrick"))
                continue;
            foreach (ServerPlayer *p, room->getAlivePlayers())
                if (!p->containsTrick(card->objectName()) && p->hasJudgeArea()) {
                    ids << id;
                    break;
                }
        }
        return ids;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getMark(limit_mark) == 0
            || tricks(room, data.value<CardsMoveOneTimeStruct>()).isEmpty())
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
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const QList<int> ids = tricks(room, move);
        if (ids.isEmpty())
            return false;
        int id = ids.first();
        if (ids.size() > 1) {
            room->fillAG(ids, player);
            const int chosen = room->askForAG(player, ids, false, objectName());
            room->clearAG(player);
            if (ids.contains(chosen))
                id = chosen;
        }
        const Card *card = Sanguosha->getCard(id);
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (!p->containsTrick(card->objectName()) && p->hasJudgeArea())
                targets << p;
        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@ikzhimen");
        if (!target)
            return false;
        move.removeCardIds(QList<int>{id});
        *ctx.original_data = QVariant::fromValue(move);
        room->moveCardTo(card, target, Player::PlaceDelayedTrick, true);
        return false;
    }
};

// ---------------------------------------------------------------- snow003

class IkBiju : public MaxCardsSkillV2
{
public:
    IkBiju() : MaxCardsSkillV2("ikbiju") { frequency = Compulsory; }

    // +4 unless cards of two colours were used in this turn's play phase.
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || !ctx.primary->hasSkill(objectName()))
            return CorrectSkillResult::noEffect();
        int colours = 0;
        for (int i = 0; i < 3; ++i)
            if (ctx.primary->getMark("ikbiju_colour" + QString::number(i) + "-Clear") > 0)
                ++colours;
        if (colours > 1)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(4);
    }
};

// Colours, suits and types the character used during its play phase this turn.
class IkPlayRecord : public TriggerSkillV2
{
public:
    IkPlayRecord() : TriggerSkillV2("#ikbiju-record") { events << PreCardUsed << CardResponded; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->getPhase() != Player::Play)
            return false;
        const Card *card = nullptr;
        if (event == PreCardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player)
                card = use.card;
        } else {
            const CardResponseStruct resp = data.value<CardResponseStruct>();
            if (resp.m_isUse)
                card = resp.m_card;
        }
        if (!card || card->getTypeId() == Card::TypeSkill)
            return false;
        room->setPlayerMark(player, "ikbiju_colour" + QString::number(int(card->getColor())) + "-Clear", 1);
        const int suit = int(card->getSuit());
        if (suit >= 0 && suit <= 3)
            room->setPlayerMark(player, "ikpojian_suits-Clear", player->getMark("ikpojian_suits-Clear") | (1 << suit));
        const int type = int(card->getTypeId());
        if (type > 0)
            room->setPlayerMark(player, "ikpojian_types-Clear", player->getMark("ikpojian_types-Clear") | (1 << type));
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

bool fieldMoveSource(Room *room, ServerPlayer *mover, ServerPlayer *from, QList<int> *disabled)
{
    bool any = false;
    foreach (const Card *c, from->getJudgingArea()) {
        bool ok = false;
        foreach (ServerPlayer *op, room->getOtherPlayers(from))
            if (!mover->isProhibited(op, c) && !op->containsTrick(c->objectName()) && op->hasJudgeArea())
                ok = true;
        any = any || ok;
        if (!ok && disabled)
            *disabled << c->getEffectiveId();
    }
    foreach (const Card *c, from->getEquips()) {
        const EquipCard *equip = qobject_cast<const EquipCard *>(c->getRealCard());
        bool ok = false;
        if (equip)
            foreach (ServerPlayer *op, room->getOtherPlayers(from))
                if (!op->getEquip(equip->location()) && op->hasEquipArea(equip->location()))
                    ok = true;
        any = any || ok;
        if (!ok && disabled)
            *disabled << c->getEffectiveId();
    }
    return any;
}

class IkPojian : public TriggerSkillV2
{
public:
    IkPojian() : TriggerSkillV2("ikpojian") { events << EventPhaseStart; }

    static QList<ServerPlayer *> sources(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (fieldMoveSource(room, player, p, nullptr))
                result << p;
        return result;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish
            || (player->getMark("ikpojian_suits-Clear") != 0xF && player->getMark("ikpojian_types-Clear") != 0xE)
            || sources(room, player).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, sources(room, ctx.owner), objectName(), "@ikpojian",
                                                        true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        ServerPlayer *from = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!from || !from->isAlive())
            return false;
        QList<int> disabled;
        if (!fieldMoveSource(room, player, from, &disabled))
            return false;
        const int id = room->askForCardChosen(player, from, "ej", objectName(), false, Card::MethodNone, disabled);
        if (id < 0 || disabled.contains(id))
            return false;
        const Card *card = Sanguosha->getCard(id);
        const Player::Place place = room->getCardPlace(id);
        const EquipCard *equip = place == Player::PlaceEquip ? qobject_cast<const EquipCard *>(card->getRealCard()) : nullptr;
        QList<ServerPlayer *> tos;
        foreach (ServerPlayer *p, room->getOtherPlayers(from)) {
            if (equip) {
                if (!p->getEquip(equip->location()) && p->hasEquipArea(equip->location()))
                    tos << p;
            } else if (!player->isProhibited(p, card) && !p->containsTrick(card->objectName()) && p->hasJudgeArea()) {
                tos << p;
            }
        }
        ServerPlayer *to = tos.isEmpty() ? nullptr
                                         : room->askForPlayerChosen(player, tos, objectName(),
                                                                    "@ikpojian-to:::" + card->objectName());
        if (to)
            room->moveCardTo(card, from, to, place,
                             CardMoveReason(CardMoveReason::S_REASON_TRANSFER, player->objectName(), objectName(), QString()));
        return false;
    }
};

// ---------------------------------------------------------------- snow004 / snow007

class IkZaiqi : public TriggerSkillV2
{
public:
    IkZaiqi() : TriggerSkillV2("ikzaiqi")
    {
        events << HpRecover;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play
            || !player->hasFlag("Global_Dying"))
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
        const int n = qMax(1, ctx.original_data->value<RecoverStruct>().recover);
        ctx.owner->drawCards(n, objectName());
        return false;
    }
};

class IkYuanhe : public ViewAsSkillV2
{
public:
    IkYuanhe() : ViewAsSkillV2("ikyuanhe", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card) && card->isRed()
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

    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkYuanheCard"; }

    // The user and the target each draw two, then each discard two.
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || targets.isEmpty())
            return FinishSkill;
        Room *room = source->getRoom();
        QList<ServerPlayer *> both{source, targets.first()};
        room->sortByActionOrder(both);
        foreach (ServerPlayer *p, both)
            if (p->isAlive())
                p->drawCards(2, objectName());
        foreach (ServerPlayer *p, both)
            if (p->isAlive() && p->canDiscard(p, "he"))
                room->askForDiscard(p, objectName(), 2, 2, false, true);
        return FinishSkill;
    }
};

// ---------------------------------------------------------------- luna002 / luna034

class IkWudi : public ViewAsSkillV2
{
public:
    IkWudi() : ViewAsSkillV2("ikwudi", 2) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
            return false;
        Duel duel(Card::SuitToBeDecided, -1);
        duel.setSkillName(objectName());
        return duel.isAvailable(request.initiator);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (request.selectedCardIds.size() >= 2 || !ownsHandCard(request.initiator, card))
            return false;
        return request.selectedCardIds.isEmpty()
            || Sanguosha->getCard(request.selectedCardIds.first())->sameColorWith(card);
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

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        Duel *duel = new Duel(Card::SuitToBeDecided, -1);
        duel->addSubcards(request.selectedCardIds);
        duel->setSkillName(objectName());
        return duel;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IkWudiCard"; }
};

class IkGuijiao : public TriggerSkillV2
{
public:
    IkGuijiao() : TriggerSkillV2("ikguijiao") { events << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Start)
            return result;
        if (player->getMark("@wayward") > 0) {
            foreach (ServerPlayer *owner, room->getAlivePlayers())
                if (owner->hasSkill(objectName()))
                    result[owner] << objectName();
            return result;
        }
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->getMark("@wayward") > 0)
                return result;
        if (player->hasSkill(objectName()))
            result[player] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (player->getMark("@wayward") > 0) {
            if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(player)))
                return false;
            ctx.extra_data = QString();
        } else {
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(),
                                                            "@ikguijiao", true, true);
            if (!target)
                return false;
            ctx.extra_data = target->objectName();
        }
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const QString name = ctx.extra_data.toString();
        if (name.isEmpty()) {
            ctx.owner->drawCards(1, objectName());
            room->addPlayerMark(player, "ikguijiao_less-Clear");
            return false;
        }
        ServerPlayer *target = room->findPlayerByObjectName(name);
        if (target && target->isAlive())
            target->gainMark("@wayward");
        return false;
    }
};

class IkGuijiaoMaxCards : public MaxCardsSkillV2
{
public:
    IkGuijiaoMaxCards() : MaxCardsSkillV2("#ikguijiao")
    {
        frequency = Compulsory;
        setHolderSelector(CorrectSkill_System); // the turn player need not hold it
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || ctx.primary->getMark("ikguijiao_less-Clear") == 0)
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::signedAmount(-ctx.primary->getMark("ikguijiao_less-Clear"));
    }
};

}

IkXingyuCard::IkXingyuCard() { setSkillName("ikxingyu"); mute = true; }
IkYuanheCard::IkYuanheCard() { setSkillName("ikyuanhe"); mute = true; }

IkaiDoPackage::IkaiDoPackage()
    : Package("ikai-do")
{
    // 神爱 is OL 仁德; 心契 is 激将 for kaze.
    General *wind001 = new General(this, "wind001$", "kaze");
    wind001->addSkill("olrende");
    wind001->addSkill(new IkXinqi);

    General *wind002 = new General(this, "wind002", "kaze");
    wind002->addSkill("ikchilian");
    wind002->addSkill(new IkZhenhong);
    wind002->addSkill(new IkZhenhongTargetMod);
    related_skills.insert("ikzhenhong", "#ikzhenhong-target");

    // 翼咆 is 咆哮.
    General *wind003 = new General(this, "wind003", "kaze");
    wind003->addSkill("paoxiao");
    wind003->addSkill(new IkShijiu);

    General *wind006 = new General(this, "wind006", "kaze");
    wind006->addSkill("mashu");
    wind006->addSkill(new IkYufeng);

    // 暴殴 is 诛害.
    General *wind042 = new General(this, "wind042", "kaze");
    wind042->addSkill("zhuhai");
    wind042->addSkill(new IkYehua);
    wind042->addRelateSkill("ikxingyu");

    General *bloom001 = new General(this, "bloom001$", "hana");
    bloom001->addSkill(new IkJiaoman);
    bloom001->addSkill(new IkHuanwei);

    // 天锁 is 鬼才; 幻姬 is 反馈.
    General *bloom002 = new General(this, "bloom002", "hana", 3);
    bloom002->addSkill("guicai");
    bloom002->addSkill("fankui");
    bloom002->addSkill(new IkZhimen);

    // 羽梦 is 十周年 遗计.
    General *bloom006 = new General(this, "bloom006", "hana", 3);
    bloom006->addSkill("iktiandu");
    bloom006->addSkill("tenyearyiji");

    // 制衡 is 十周年 制衡.
    General *snow001 = new General(this, "snow001$", "yuki");
    snow001->addSkill("tenyearzhiheng");
    snow001->addSkill(new IkJiyuan);

    General *snow003 = new General(this, "snow003", "yuki");
    snow003->addSkill(new IkBiju);
    snow003->addSkill(new IkPlayRecord);
    related_skills.insert("ikbiju", "#ikbiju-record");
    snow003->addSkill(new IkPojian);

    General *snow004 = new General(this, "snow004", "yuki");
    snow004->addSkill("noskurou");
    snow004->addSkill(new IkZaiqi);

    // 无竭 is the old 连营.
    General *snow007 = new General(this, "snow007", "yuki", 3);
    snow007->addSkill("noslianying");
    snow007->addSkill(new IkYuanhe);

    General *luna002 = new General(this, "luna002", "tsuki");
    luna002->addSkill("wushuang");
    luna002->addSkill(new IkWudi);

    // 本音 needs upstream's 紫莲圣咏, which has no local card.
    General *luna018 = new General(this, "luna018", "tsuki", 3);
    luna018->addSkill("yicong");
    luna018->addSkill(new PendingSkill("ikbenyin"));

    General *luna034 = new General(this, "luna034", "tsuki");
    luna034->addSkill(new IkGuijiao);
    luna034->addSkill(new IkGuijiaoMaxCards);
    related_skills.insert("ikguijiao", "#ikguijiao");
    luna034->addSkill("ikjinlian");

    skills << new IkXingyu;

    addMetaObject<IkXingyuCard>();
    addMetaObject<IkYuanheCard>();
}

ADD_PACKAGE(IkaiDo)
