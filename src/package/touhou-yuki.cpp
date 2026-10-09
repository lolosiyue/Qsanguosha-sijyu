#include "touhou-yuki.h"
#include "touhou-utils.h"
#include "engine.h"
#include "general.h"
#include "room.h"
#include "roomthread.h"
#include "standard.h"
#include "maneuvering.h"
#include "util.h"

#include <QScopedPointer>

using namespace TouhouUtils;

namespace {

// ---------------------------------------------------------------- yuki001

class ThJianmo : public TriggerSkillV2
{
public:
    ThJianmo() : TriggerSkillV2("thjianmo") { events << EventPhaseStart << EventPhaseChanging; }

    // Both choices last for this play phase only.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || data.value<PhaseChangeStruct>().from != Player::Play)
            return false;
        if (player->getMark("@imprison") > 0)
            room->setPlayerMark(player, "@imprison", 0);
        if (player->getMark("thjianmo_jian") > 0) {
            room->setPlayerMark(player, "thjianmo_jian", 0);
            room->removePlayerCardLimitationByReason(player, objectName());
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::Play
            || player->getHandcardNum() < player->getMaxHp())
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()))
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(ctx.invoker)))
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {ctx.invoker};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        LogMessage log;
        log.from = target;
        if (room->askForChoice(target, objectName(), "jian+mo", QVariant::fromValue(ctx.owner)) == "jian") {
            log.type = "#thjianmochoose1";
            log.arg = "1";
            room->sendLog(log);
            target->drawCards(1, objectName());
            room->setPlayerMark(target, "thjianmo_jian", 1);
            room->setPlayerCardLimitation(target, "use,response", "Slash", false, objectName());
        } else {
            log.type = "#thjianmochoose2";
            log.arg = "2";
            room->sendLog(log);
            room->setPlayerMark(target, "@imprison", 1);
            room->setPlayerProperty(target, "thjianmo_source", ctx.owner->objectName());
        }
        return false;
    }
};

// A slash used under 缄魔's second choice needs a discard, or it is void.
class ThJianmoDiscard : public TriggerSkillV2
{
public:
    ThJianmoDiscard() : TriggerSkillV2("#thjianmo")
    {
        events << CardUsed;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !use.card || !use.card->isKindOf("Slash") || player->getMark("@imprison") <= 0)
            return TriggerList();
        ServerPlayer *owner = room->findPlayerByObjectName(player->property("thjianmo_source").toString());
        if (!owner || !owner->isAlive() || !owner->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{owner, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ServerPlayer *user = use.from;
        LogMessage log;
        log.type = "#ThJianmo";
        log.from = user;
        log.arg = "thjianmo";
        log.arg2 = use.card->objectName();
        room->sendLog(log);
        if (!user->canDiscard(user, "he") || !room->askForDiscard(user, "thjianmo", 1, 1, true, true, "@thjianmo")) {
            use.nullified_list << "_ALL_TARGETS";
            *ctx.original_data = QVariant::fromValue(use);
        }
        return false;
    }
};

// 二重: counts the other characters' consecutive turns without a slash.
class ThErchong : public TriggerSkillV2
{
public:
    ThErchong() : TriggerSkillV2("therchong")
    {
        events << PreCardUsed << EventPhaseStart << EventPhaseChanging << EventSkillInvoking;
        frequency = Wake;
        waked_skills = "thhuanfa,yicong";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        if (event == PreCardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player && isOwnTurn(player) && use.card && use.card->isKindOf("Slash"))
                room->setPlayerFlag(player, "ThErchongSlash");
        } else if (event == EventPhaseStart && player->getPhase() == Player::RoundStart) {
            room->setPlayerMark(player, "@layer", 0);
        } else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const bool slashed = player->hasFlag("ThErchongSlash");
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (!p->hasSkill(objectName(), true) || p->getMark(objectName()) > 0)
                    continue;
                if (slashed)
                    room->setPlayerMark(p, "@layer", 0);
                else
                    room->addPlayerMark(p, "@layer");
            }
        }
        return false;
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data)
            return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost)
            addUsage(accepted);
    }

    // Wakes at the end of the second such turn.
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != EventPhaseChanging || !player || data.value<PhaseChangeStruct>().to != Player::NotActive)
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player)) {
            if (!owner->hasSkill(objectName()) || !owner->isWounded() || owner->getMark(objectName()) > 0
                || !(owner->getMark("@layer") >= 2 || hasWakeGrant(owner, objectName())))
                continue;
            foreach (int id, owner->getValidSkillInstanceIds(objectName())) {
                SkillContext eligibility;
                eligibility.owner = owner;
                eligibility.invoker = owner;
                eligibility.skill_name = objectName();
                eligibility.instanceID = id;
                eligibility.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(objectName(), id));
                if (isUsable(eligibility))
                    result[owner] << SkillInstanceUtils::formatName(objectName(), id);
            }
        }
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { return isUsable(ctx); }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx))
            return false;
        addUsage(ctx);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (player->getMark("@layer") < 2 && !player->canWake(objectName()))
            return false;
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox(player, objectName());
        room->setPlayerMark(player, "@layer", 0);
        room->setPlayerMark(player, objectName(), 1);
        if (room->changeMaxHpForAwakenSkill(player, -1, objectName())) {
            room->acquireSkillFromEffect(player, "thhuanfa", ctx);
            room->acquireSkillFromEffect(player, "ikzhuji", ctx);
        }
        return false;
    }
};

class ThHuanfa : public ViewAsSkillV2
{
public:
    ThHuanfa() : ViewAsSkillV2("thhuanfa", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card) && card->getSuit() == Card::Heart;
    }

    bool willThrowSelectedCards() const override { return false; }

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

    QString historyKey(const ActiveSkillRequest &) const override { return "ThHuanfaCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        ServerPlayer *payer = ctx.initiator;
        if (!source || !payer || !target || !target->isAlive() || !ctx.use_card || ctx.use_card->subcardsLength() != 1)
            return ContinueEffects;
        Room *room = source->getRoom();
        const int given = ctx.use_card->getSubcards().first();
        if (!payer->handCards().contains(given))
            return ContinueEffects;
        room->giveCard(payer, target, ctx.use_card, objectName(), true);
        if (!source->isAlive() || !target->isAlive() || target->isNude())
            return ContinueEffects;
        const int id = room->askForCardChosen(source, target, "he", objectName());
        if (id < 0)
            return ContinueEffects;
        CardMoveReason extraction(CardMoveReason::S_REASON_EXTRACTION, source->objectName());
        room->obtainCard(source, Sanguosha->getCard(id), extraction, room->getCardPlace(id) != Player::PlaceHand);
        QList<ServerPlayer *> receivers = room->getOtherPlayers(target);
        receivers.removeOne(source);
        if (receivers.isEmpty() || !source->isAlive() || room->getCardOwner(id) != source)
            return ContinueEffects;
        ServerPlayer *receiver = room->askForPlayerChosen(source, receivers, objectName(),
                                                          "@thhuanfa-give:" + target->objectName(), true);
        if (receiver)
            room->giveCard(source, receiver, QList<int>{id}, objectName());
        return ContinueEffects;
    }
};

class ThChundu : public TriggerSkillV2
{
public:
    ThChundu() : TriggerSkillV2("thchundu$") { events << CardsMoveOneTime; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasLordSkill(objectName()) || !player->canDiscard(player, "h"))
            return TriggerList();
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        const CardUseStruct &use = move.reason.m_useStruct;
        if (move.to_place != Player::DiscardPile || !move.from_places.contains(Player::PlaceTable)
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_USE
            || !use.card || !use.from || use.from == player || use.from->getKingdom() != "yuki"
            || use.card->getSuit() != Card::Heart || use.card->getTypeId() != Card::TypeBasic)
            return TriggerList();
        foreach (int id, move.card_ids)
            if (room->getCardPlace(id) == Player::DiscardPile)
                return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room->askForCard(ctx.owner, ".", "@thchundu", *ctx.original_data, objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QList<int> ids;
        foreach (int id, move.card_ids)
            if (room->getCardPlace(id) == Player::DiscardPile)
                ids << id;
        if (ids.isEmpty())
            return false;
        DummyCard dummy(ids);
        room->obtainCard(ctx.owner, &dummy);
        return false;
    }
};

// ---------------------------------------------------------------- yuki002

// Two cards as Analeptic, shared by 醉觞's owner and, in that owner's turn, everyone else.
static bool canZuishangActivate(const ActiveSkillRequest &request)
{
    if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
        return Analeptic::IsAvailable(request.initiator);
    if (request.pattern.startsWith("@"))
        return false;
    Analeptic card(Card::NoSuit, 0);
    return Sanguosha->matchExpPattern(request.pattern, request.initiator, &card);
}

class ThZuishangCards : public ViewAsSkillV2
{
public:
    explicit ThZuishangCards(const QString &name) : ViewAsSkillV2(name, 2) { response_or_use = true; }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || request.selectedCardIds.length() >= 2 || card->hasFlag("using"))
            return false;
        const int id = card->getEffectiveId();
        return self->handCards().contains(id) || self->getEquipsId().contains(id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 2)
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

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request))
            return nullptr;
        Analeptic *card = new Analeptic(Card::SuitToBeDecided, -1);
        card->addSubcards(request.selectedCardIds);
        card->setSkillName("thzuishang");
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Analeptic"; }
};

class ThZuishangViewAs : public ThZuishangCards
{
public:
    ThZuishangViewAs() : ThZuishangCards("thzuishang") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && isOwnTurn(request.initiator) && canZuishangActivate(request);
    }
};

class ThZuishangGiven : public ThZuishangCards
{
public:
    ThZuishangGiven() : ThZuishangCards("thzuishangv") { attached_lord_skill = true; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *owner = attachedParentOwner(request, "thzuishang");
        return owner && isOwnTurn(owner) && owner->hasSkill("thzuishang") && canZuishangActivate(request);
    }
};

class ThZuishang : public TriggerSkillV2
{
public:
    ThZuishang() : TriggerSkillV2("thzuishang")
    {
        events << GameStart << EventPhaseStart << EventAcquireSkill << EventLoseSkill << Death;
        view_as_skill = new ThZuishangViewAs;
        global = true;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        syncAttachedChildren(room, objectName(), "thzuishangv", false);
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

// In 醉觞's owner's turn, nobody's Analeptic counts toward its limit.
class ThZuishangTargetMod : public TargetModSkillV2
{
public:
    ThZuishangTargetMod() : TargetModSkillV2("#thzuishang", "Analeptic") { setHolderSelector(CorrectSkill_AllHolders); }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::Residue || !ctx.holder || !ctx.card || !ctx.card->isKindOf("Analeptic")
            || !isOwnTurn(ctx.holder) || !ctx.holder->hasSkill("thzuishang"))
            return CorrectSkillResult::noEffect();
        return CorrectSkillResult::unlimitedResidue();
    }
};

class ThXugu : public TriggerSkillV2
{
public:
    ThXugu() : TriggerSkillV2("thxugu") { events << CardFinished; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !isOwnTurn(player)
            || player->getPhase() != Player::Play || player->hasFlag("ThXuguFailed") || !use.card
            || !use.card->isKindOf("Analeptic") || room->getOtherPlayers(player).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(),
                                                        "@thxugu", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = ctx.owner;
        if (room->askForUseCard(target, "analeptic", "@thxugu-use:" + player->objectName(), -1, Card::MethodUse, false))
            return false;
        room->setPlayerFlag(player, "ThXuguFailed");
        if (!player->isAlive() || !target->isAlive())
            return false;
        auto *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_thxugu");
        CardUseStruct use(slash, player, target);
        use.setOwnedCard(slash);
        if (player->canSlash(target, slash, false))
            room->useCardFromSkillEffect(use, ctx);
        return false;
    }
};

// ---------------------------------------------------------------- yuki003

class ThShenzhan : public TriggerSkillV2
{
public:
    ThShenzhan() : TriggerSkillV2("thshenzhan")
    {
        events << Damage;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.from != player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Play || player->getSlashCount() <= 0 || !damage.card
            || damage.card->getTypeId() == Card::TypeSkill || damage.card->getTypeId() == Card::TypeBasic
            || damage.to == player)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        const QStringList keys{"Slash", "FireSlash", "ThunderSlash", "IceSlash"};
        foreach (const QString &key, keys)
            room->addPlayerHistory(ctx.owner, key, 0);
        return false;
    }
};

class ThHunqieViewAs : public ViewAsSkillV2
{
public:
    ThHunqieViewAs() : ViewAsSkillV2("thhunqie", 1) { response_or_use = true; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thhunqie"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || !request.selectedCardIds.isEmpty() || card->hasFlag("using") || !card->isBlack())
            return false;
        const int id = card->getEffectiveId();
        return self->handCards().contains(id) || self->getEquipsId().contains(id);
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
        Slash *slash = new Slash(material->getSuit(), material->getNumber());
        slash->addSubcard(material);
        slash->setSkillName(objectName());
        return slash;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
};

class ThHunqie : public TriggerSkillV2
{
public:
    ThHunqie() : TriggerSkillV2("thhunqie")
    {
        events << EventPhaseEnd;
        view_as_skill = new ThHunqieViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play
            || player->getSlashCount() > 0 || player->isNude())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // The nested @@ response is the whole effect.
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->askForUseCard(ctx.owner, "@@thhunqie", "@thhunqie");
        return false;
    }
};

class ThDaojian : public ViewAsEquipSkill
{
public:
    ThDaojian() : ViewAsEquipSkill("thdaojian") { frequency = Compulsory; markOwnerOnly(this); }

    QString viewAsEquip(const Player *target) const override
    {
        if (target->hasSkill(objectName()) && target->hasEquipArea(0) && !target->getWeapon())
            return "qinggang_sword";
        return QString();
    }
};

// ---------------------------------------------------------------- yuki004

class ThZhancao : public TriggerSkillV2
{
public:
    ThZhancao() : TriggerSkillV2("thzhancao") { events << TargetConfirming << BeforeCardsMove; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player)
            return result;
        if (event == TargetConfirming) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card || !use.card->isKindOf("Slash") || !use.to.contains(player) || !player->isAlive()
                || use.nullified_list.contains(player->objectName()))
                return result;
            foreach (ServerPlayer *owner, room->getAlivePlayers())
                if (owner->hasSkill(objectName()) && !isOwnTurn(owner)
                    && (owner == player || owner->inMyAttackRange(player)))
                    result[owner] << objectName();
            return result;
        }
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        const Card *card = move.reason.m_useStruct.card;
        if (card && card->hasFlag("thzhancao") && player->getTag("ThZhancaoCard").toString() == card->toString()
            && move.from_places.contains(Player::PlaceTable) && move.to_place == Player::DiscardPile
            && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_USE
            && player->isAlive() && room->CardInTable(card))
            result[player] << objectName();
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == BeforeCardsMove)
            return true;
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(ctx.invoker)))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (event == BeforeCardsMove) {
            CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
            const Card *card = move.reason.m_useStruct.card;
            owner->removeTag("ThZhancaoCard");
            if (!card || !room->CardInTable(card))
                return false;
            room->setCardFlag(card, "-thzhancao");
            const QList<int> materials = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
            owner->obtainCard(card);
            QList<int> intercepted;
            foreach (int id, materials)
                if (move.card_ids.contains(id) && room->getCardOwner(id) == owner)
                    intercepted << id;
            move.removeCardIds(intercepted);
            *ctx.original_data = QVariant::fromValue(move);
            return false;
        }
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!room->askForCard(owner, "^BasicCard", "@thzhancao")) {
            room->loseHp(owner, 1, true, owner, objectName());
            const bool hasMaterial = !use.card->isVirtualCard() || use.card->subcardsLength() > 0;
            if (hasMaterial && owner->isAlive()) {
                room->setCardFlag(use.card, "thzhancao");
                owner->setTag("ThZhancaoCard", use.card->toString());
            }
        }
        if (!use.nullified_list.contains(ctx.invoker->objectName()))
            use.nullified_list << ctx.invoker->objectName();
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

// ---------------------------------------------------------------- yuki005

class ThMoji : public ViewAsSkillV2
{
public:
    ThMoji() : ViewAsSkillV2("thmoji") { response_or_use = true; }

    SkillDialogInfo getDialogInfo() const override
    {
        return SkillDialogInfo::guhuo(objectName(), true, false, false, false, false);
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || player->getHp() < 1 || player->getCardCount() < materialCount(player))
            return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) {
            Slash slash(Card::NoSuit, 0);
            Analeptic analeptic(Card::NoSuit, 0);
            return slash.isAvailable(player) || analeptic.isAvailable(player);
        }
        if (request.pattern.startsWith("@") || request.pattern.startsWith("."))
            return false;
        return (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            && !usableNames(request).isEmpty();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || card->hasFlag("using") || request.selectedCardIds.length() >= materialCount(self))
            return false;
        const int id = card->getEffectiveId();
        return self->handCards().contains(id) || self->getEquipsId().contains(id);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.length() != materialCount(request.initiator))
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

    // The preview carries the chosen IDs only so the server can rebuild it. They go
    // on top of the draw pile in order, and the card itself is used without material.
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || !cardSelectionFeasible(request))
            return false;
        CardMoveReason reason(CardMoveReason::S_REASON_PUT, ctx.initiator->objectName(), objectName(), QString());
        foreach (int id, request.selectedCardIds)
            room->moveCardTo(Sanguosha->getCard(id), ctx.initiator, nullptr, Player::DrawPile, reason, false);
        return true;
    }

    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.use_card || !ctx.use_card->isKindOf("BasicCard"))
            return FinishSkill;
        Card *card = Sanguosha->cloneCard(ctx.use_card->objectName(), Card::NoSuit, 0);
        if (!card)
            return FinishSkill;
        card->setSkillName(objectName());
        card->setCanRecast(false);
        card->deleteLater();
        ctx.updated_card = card;
        return ContinueEffects;
    }

protected:
    bool allowDeclaration(const Player *, const QString &name) const override
    {
        QScopedPointer<Card> card(Sanguosha->cloneCard(name));
        return card && (card->isKindOf("Slash") || card->isKindOf("Jink") || card->isKindOf("Analeptic"));
    }

private:
    static int materialCount(const Player *player) { return qMin(2, qMax(0, player->getHp())); }
};

class ThYuanqi : public ViewAsSkillV2
{
public:
    ThYuanqi() : ViewAsSkillV2("thyuanqi") { setPhaseName("Play"); }

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

    QString historyKey(const ActiveSkillRequest &) const override { return "ThYuanqiCard"; }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive())
            return FinishSkill;
        Room *room = source->getRoom();
        const Card::Suit suit = room->askForSuit(source, objectName());
        LogMessage log;
        log.type = "#ChooseSuit";
        log.from = source;
        log.arg = Card::Suit2String(suit);
        room->sendLog(log);
        const QList<int> ids = room->getNCards(1, false);
        CardMoveReason turnover(CardMoveReason::S_REASON_TURNOVER, source->objectName(), objectName(), QString());
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable, turnover), true);
        const Card *card = Sanguosha->getCard(ids.first());
        if (card->getSuit() == suit) {
            if (source->askForSkillInvoke("thyuanqi_draw", "yes"))
                source->drawCards(2, objectName());
        } else if (card->isRed() == (suit == Card::Heart || suit == Card::Diamond)
                   && (card->isRed() || card->isBlack())) {
            ServerPlayer *target = room->askForPlayerChosen(source, room->getAlivePlayers(), objectName(), "@thyuanqi", true);
            if (target && room->getCardPlace(ids.first()) == Player::PlaceTable) {
                CardMoveReason give(CardMoveReason::S_REASON_GIVE, source->objectName(), target->objectName(), objectName(),
                                    QString());
                room->moveCardsAtomic(CardsMoveStruct(ids, target, Player::PlaceHand, give), true);
            }
        }
        if (room->getCardPlace(ids.first()) == Player::PlaceTable) {
            CardMoveReason put(CardMoveReason::S_REASON_PUT, source->objectName(), objectName(), QString());
            room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::DiscardPile, put), true);
        }
        return FinishSkill;
    }
};

// ---------------------------------------------------------------- yuki006

class ThDunjia : public TriggerSkillV2
{
public:
    ThDunjia() : TriggerSkillV2("thdunjia")
    {
        events << TargetConfirmed;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card
            || use.card->getTypeId() == Card::TypeSkill || use.to.length() != 1 || use.to.first() != player || !use.from
            || use.from == player || !use.from->isAlive()
            || use.from->getEquips().length() >= player->getEquips().length())
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

class ThQingming : public TriggerSkillV2
{
public:
    ThQingming() : TriggerSkillV2("thqingming") { events << TargetSpecified; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card
            || !use.card->isKindOf("Slash") || targets(player, use).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        QList<ServerPlayer *> chosen;
        foreach (ServerPlayer *to, targets(ctx.owner, use))
            if (ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(to)))
                chosen << to;
        if (chosen.isEmpty())
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = chosen;
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = ctx.owner;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.nullified_list.contains(target->objectName()))
            use.nullified_list << target->objectName();
        *ctx.original_data = QVariant::fromValue(use);
        if (!player->isAlive())
            return false;
        // Upstream also offers its own 木隐妖岚, which this engine does not have.
        QStringList choices;
        const QStringList names{"snatch", "dismantlement", "duel"};
        foreach (const QString &name, names) {
            QScopedPointer<Card> card(Sanguosha->cloneCard(name));
            if (!card)
                continue;
            card->setSkillName("_thqingming");
            if (!player->isProhibited(target, card.data()) && card->targetFilter(QList<const Player *>(), target, player))
                choices << name;
        }
        if (choices.isEmpty())
            return false;
        const QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(target));
        Card *card = Sanguosha->cloneCard(choice);
        if (!card)
            return false;
        card->setSkillName("_thqingming");
        CardUseStruct derived(card, player, target);
        derived.setOwnedCard(card);
        room->useCardFromSkillEffect(derived, ctx);
        return false;
    }

private:
    static QList<ServerPlayer *> targets(ServerPlayer *player, const CardUseStruct &use)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *to, use.to)
            if (to != player && to->isAlive() && to->getEquips().length() > player->getEquips().length()
                && !result.contains(to))
                result << to;
        return result;
    }
};

// ---------------------------------------------------------------- yuki007

// The card is asked for under the generic cardIgnoreLegality flag, so its first target may be
// any character. A response use keeps the server from re-checking isAvailable(), whose target
// conditions the flag lifts; the pattern keeps the 杀 and 酒 use limits and the number rule.
// #thchouce clears the flag at PreCardUsed. ThChouceUse marks the request for #thchouce.
class ThChouce : public ViewAsSkillV2
{
public:
    ThChouce() : ViewAsSkillV2("thchouce") { setPhaseName("Play"); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *self = request.initiator;
        return self && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && self->getMark("thchouce_number-PlayClear") < 13 && !self->hasFlag("Global_ThChouceFailed");
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!player || !player->isAlive())
            return FinishSkill;
        Room *room = player->getRoom();
        room->setPlayerFlag(player, "ThChouceUse");
        room->setPlayerFlag(player, "cardIgnoreLegality");
        QString pattern = "^Jink+^Nullification";
        if (!player->canSlashWithoutCrossbow())
            pattern.append("+^Slash");
        if (!Analeptic::IsAvailable(player))
            pattern.append("+^Analeptic");
        // Only the first card of the phase, or one above the previous card's number.
        const int last = player->getMark("thchouce_number-PlayClear");
        QString prompt = "@thchouce";
        if (last > 0) {
            pattern.append(QString("|.|%1~").arg(last + 1));
            prompt = QString("@thchouce-number:::%1").arg(last);
        }
        room->askForUseCard(player, pattern, prompt);
        if (player->hasFlag("ThChouceUse")) {
            room->setPlayerFlag(player, "-ThChouceUse");
            room->setPlayerFlag(player, "-cardIgnoreLegality");
            room->setPlayerFlag(player, "Global_ThChouceFailed");
        }
        return FinishSkill;
    }
};

// Records the number of each card the owner uses in its play phase, how many were 筹策 uses,
// and whether any was not (for 占筮).
class ThChouceRecord : public TriggerSkillV2
{
public:
    ThChouceRecord() : TriggerSkillV2("#thchouce")
    {
        events << PreCardUsed << PreCardResponded;
        frequency = Compulsory;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = nullptr;
        if (event == PreCardUsed) {
            card = data.value<CardUseStruct>().card;
        } else {
            const CardResponseStruct resp = data.value<CardResponseStruct>();
            if (resp.m_isUse)
                card = resp.m_card;
        }
        if (!card || card->getTypeId() == Card::TypeSkill || !player || player->getPhase() != Player::Play)
            return true;
        if (event == PreCardUsed && player->hasFlag("ThChouceUse")) {
            room->setPlayerFlag(player, "-ThChouceUse");
            room->setPlayerFlag(player, "-cardIgnoreLegality");
            room->addPlayerMark(player, "thchouce_count-PlayClear");
        } else if (player->hasSkill("thchouce")) {
            room->setPlayerMark(player, "thchouce_broken-PlayClear", 1);
        } else {
            return true;
        }
        room->setPlayerMark(player, "thchouce_number-PlayClear", card->getNumber());
        return true;
    }
};

class ThZhanshi : public TriggerSkillV2
{
public:
    ThZhanshi() : TriggerSkillV2("thzhanshi")
    {
        events << EventPhaseEnd << EventPhaseStart << EventPhaseChanging;
        frequency = Compulsory;
    }

    // 幻葬 lasts for the extra turn only, even if 占筮 is lost meanwhile.
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player)
            return false;
        if (event == EventPhaseStart && player->getPhase() == Player::RoundStart
            && player->getMark("thzhanshi_pending") > 0) {
            room->setPlayerMark(player, "thzhanshi_pending", 0);
            grantTracked(room, player, "thzhanshi_grants", "thhuanzang");
        } else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            revokeTracked(room, player, "thzhanshi_grants");
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseEnd || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Play || player->getMark("thchouce_count-PlayClear") < 3
            || player->getMark("thchouce_broken-PlayClear") > 0)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        room->setPlayerMark(player, "thzhanshi_pending", 1);
        room->scheduleExtraTurn(player, ctx.sourceRef);
        return false;
    }
};

// ---------------------------------------------------------------- yuki008

class ThZiyun : public ProhibitSkill
{
public:
    ThZiyun() : ProhibitSkill("thziyun") { frequency = Compulsory; }

    bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &) const override
    {
        return to && card && to->hasSkill(objectName()) && (card->isKindOf("SupplyShortage") || card->isKindOf("Lightning"));
    }
};

class ThChuiji : public TriggerSkillV2
{
public:
    ThChuiji() : TriggerSkillV2("thchuiji") { events << EventPhaseStart << CardsMoveOneTime << EventPhaseEnd; }

    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->getPhase() != Player::Discard || !player->hasSkill(objectName(), true))
            return false;
        if (event == EventPhaseStart) {
            player->removeTag("ThChuijiCount");
        } else if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from == player
                && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
                int n = player->getTag("ThChuijiCount").toInt();
                for (int i = 0; i < move.card_ids.length(); ++i)
                    if (move.from_places.value(i) == Player::PlaceHand)
                        ++n;
                player->setTag("ThChuijiCount", n);
            }
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        if (event == EventPhaseEnd) {
            if (player->getPhase() == Player::Discard && player->getTag("ThChuijiCount").toInt() >= 2)
                return TriggerList{{player, {objectName()}}};
        } else if (event == CardsMoveOneTime && !isOwnTurn(player)) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from == player
                && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
                && (move.to != player || (move.to_place != Player::PlaceHand && move.to_place != Player::PlaceEquip)))
                return TriggerList{{player, {objectName()}}};
        }
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
        JudgeStruct judge;
        judge.pattern = ".";
        judge.good = true;
        judge.play_animation = false;
        judge.reason = objectName();
        judge.who = player;
        room->judge(judge);
        if (!judge.card || !player->isAlive())
            return false;
        if (judge.card->isRed()) {
            QList<ServerPlayer *> wounded;
            foreach (ServerPlayer *p, room->getAlivePlayers())
                if (p->isWounded())
                    wounded << p;
            ServerPlayer *target = wounded.isEmpty()
                ? nullptr : room->askForPlayerChosen(player, wounded, objectName(), "@thchuiji-recover");
            if (target)
                room->recover(target, RecoverStruct(objectName(), player));
        } else if (judge.card->isBlack()) {
            QList<ServerPlayer *> victims;
            foreach (ServerPlayer *p, room->getOtherPlayers(player))
                if (player->canDiscard(p, "he"))
                    victims << p;
            ServerPlayer *target = victims.isEmpty()
                ? nullptr : room->askForPlayerChosen(player, victims, objectName(), "@thchuiji-discard");
            if (target) {
                const int id = room->askForCardChosen(player, target, "he", objectName(), false, Card::MethodDiscard);
                if (id >= 0)
                    room->throwCard(id, target, player);
            }
        }
        return false;
    }
};

// ---------------------------------------------------------------- yuki009

class ThLingya : public TriggerSkillV2
{
public:
    ThLingya() : TriggerSkillV2("thlingya") { events << CardFinished; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        ServerPlayer *current = room->getCurrent();
        if (!player || use.from != player || !player->isAlive() || !use.card || !use.card->isRed()
            || use.card->getTypeId() == Card::TypeSkill || !current || current == player || !current->isAlive()
            || !isOwnTurn(current) || !current->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{current, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(ctx.invoker)))
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {ctx.invoker};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *owner = ctx.owner;
        QStringList choices{"letdraw"};
        if (owner->canDiscard(target, "he"))
            choices << "discard";
        if (room->askForChoice(target, objectName(), choices.join("+"), QVariant::fromValue(owner)) == "discard") {
            const int id = room->askForCardChosen(owner, target, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, target, owner);
        } else {
            owner->drawCards(1, objectName());
        }
        return false;
    }
};

class ThHeimu : public TriggerSkillV2
{
public:
    ThHeimu() : TriggerSkillV2("thheimu")
    {
        events << TargetSpecifying;
        setPhaseName("Play");
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Play || !use.card || use.card->getTypeId() == Card::TypeSkill
            || candidates(room, player, use).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx))
            return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates(room, ctx.owner, use), objectName(),
                                                        "@thheimu", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx))
            return false;
        addUsage(ctx);
        return true;
    }

    // The chosen character becomes the user of the card for the rest of its resolution.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive())
            return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        LogMessage log;
        log.type = "#BecomeUser";
        log.from = target;
        log.card_str = use.card->toString();
        room->sendLog(log);
        use.from = target;
        use.m_isOwnerUse = false;
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }

private:
    static QList<ServerPlayer *> candidates(Room *room, ServerPlayer *player, const CardUseStruct &use)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            bool allowed = true;
            foreach (ServerPlayer *to, use.to)
                if (p->isProhibited(to, use.card))
                    allowed = false;
            if (allowed)
                result << p;
        }
        return result;
    }
};

// ---------------------------------------------------------------- yuki010

class ThHanpo : public TriggerSkillV2
{
public:
    ThHanpo() : TriggerSkillV2("thhanpo")
    {
        events << DamageCaused << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || damage.nature != DamageStruct::Fire)
            return TriggerList();
        if (event == DamageCaused && (damage.from != player || damage.to == player))
            return TriggerList();
        if (event == DamageInflicted && damage.to != player)
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        return true;
    }
};

class ThJidong : public TriggerSkillV2
{
public:
    ThJidong() : TriggerSkillV2("thjidong") { events << EventPhaseStart << CardsMoveOneTime << EventPhaseEnd; }

    // Counts the cards the frozen character loses during this play phase.
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != CardsMoveOneTime || !player || player->getMark("thjidong_target") <= 0)
            return false;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player || (move.to == player && (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceEquip)))
            return false;
        int n = 0;
        foreach (Player::Place place, move.from_places)
            if (place == Player::PlaceHand || place == Player::PlaceEquip)
                ++n;
        if (n > 0)
            player->setMark("thjidong_lost", player->getMark("thjidong_lost") + n);
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play)
            return TriggerList();
        if (event == EventPhaseStart && !room->getOtherPlayers(player).isEmpty())
            return TriggerList{{player, {objectName()}}};
        if (event == EventPhaseEnd)
            foreach (ServerPlayer *p, room->getOtherPlayers(player))
                if (p->getMark("thjidong_target") > 0)
                    return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseEnd)
            return true;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(),
                                                        "@thjidong", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = target->objectName();
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseEnd) {
            foreach (ServerPlayer *p, room->getOtherPlayers(ctx.owner)) {
                if (p->getMark("thjidong_target") <= 0)
                    continue;
                const int lost = p->getMark("thjidong_lost");
                p->setMark("thjidong_target", 0);
                p->setMark("thjidong_lost", 0);
                if (lost >= 2 && p->isAlive()) {
                    room->sendCompulsoryTriggerLog(ctx.owner, objectName());
                    p->turnOver();
                    p->drawCards(1, objectName());
                }
            }
            return false;
        }
        ServerPlayer *target = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!target || !target->isAlive())
            return false;
        target->setMark("thjidong_target", 1);
        target->setMark("thjidong_lost", 0);
        int n = 0;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            foreach (const Card *card, p->getCards("ej"))
                if (card->getSuit() == Card::Diamond)
                    ++n;
        if (n > 0) {
            target->drawCards(n, objectName());
            if (target->isAlive())
                room->askForDiscard(target, objectName(), n, n, false, true);
        }
        return false;
    }
};

class ThBingpu : public ViewAsSkillV2
{
public:
    ThBingpu() : ViewAsSkillV2("thbingpu")
    {
        frequency = Limited;
        limit_mark = "@bingpu";
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark(limit_mark) > 0;
    }

    TargetMode targetMode() const override { return NoTarget; }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.isEmpty();
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThBingpuCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request) || !ctx.initiator || ctx.initiator->getMark(limit_mark) <= 0)
            return false;
        room->removePlayerMark(ctx.initiator, limit_mark);
        room->doSuperLightbox(ctx.initiator, objectName());
        return true;
    }

    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source)
            return FinishSkill;
        Room *room = source->getRoom();
        foreach (ServerPlayer *target, room->getOtherPlayers(source)) {
            if (!source->isAlive())
                break;
            if (!target->isAlive() || target->isNude())
                continue;
            if (room->askForCard(target, "jink", "@thbingpu:" + source->objectName(), QVariant(), Card::MethodResponse))
                continue;
            for (int i = 0; i < 2 && source->isAlive() && target->isAlive() && source->canDiscard(target, "he"); ++i) {
                const int id = room->askForCardChosen(source, target, "he", objectName(), false, Card::MethodDiscard);
                if (id < 0)
                    break;
                room->throwCard(id, target, source);
            }
        }
        return FinishSkill;
    }
};

// ---------------------------------------------------------------- yuki011

class ThDongmoViewAs : public ViewAsSkillV2
{
public:
    ThDongmoViewAs() : ViewAsSkillV2("thdongmo") {}

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@thdongmo"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && to && to != request.initiator
            && selected.length() < request.initiator->getLostHp();
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return request.initiator && !selected.isEmpty() && selected.length() <= request.initiator->getLostHp();
    }

    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThDongmoCard"; }

    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source)
            return FinishSkill;
        Room *room = source->getRoom();
        QList<ServerPlayer *> all = targets;
        if (source->isAlive() && !all.contains(source))
            all << source;
        room->sortByActionOrder(all);
        foreach (ServerPlayer *p, all)
            if (p->isAlive())
                p->turnOver();
        foreach (ServerPlayer *p, all)
            if (p->isAlive())
                p->drawCards(1, objectName());
        return FinishSkill;
    }
};

class ThDongmo : public TriggerSkillV2
{
public:
    ThDongmo() : TriggerSkillV2("thdongmo")
    {
        events << EventPhaseStart;
        view_as_skill = new ThDongmoViewAs;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish
            || !player->isWounded())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->askForUseCard(ctx.owner, "@@thdongmo", "@thdongmo");
        return false;
    }
};

class ThLinhan : public TriggerSkillV2
{
public:
    ThLinhan() : TriggerSkillV2("thlinhan")
    {
        events << CardUsed << CardResponded;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        const Card *card = nullptr;
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player)
                card = use.card;
        } else {
            card = data.value<CardResponseStruct>().m_card;
        }
        if (!card || !card->isKindOf("Jink"))
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

// ---------------------------------------------------------------- yuki012

class ThFusheng : public TriggerSkillV2
{
public:
    ThFusheng() : TriggerSkillV2("thfusheng") { events << EventPhaseChanging << EventPhaseStart; }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive())
            return TriggerList();
        if (event == EventPhaseChanging) {
            if (player->hasSkill(objectName()) && data.value<PhaseChangeStruct>().to == Player::Draw
                && !player->isSkipped(Player::Draw))
                return TriggerList{{player, {objectName()}}};
        } else if (player->getPhase() == Player::Discard && player->hasFlag("ThFushengUsed")
                   && player->getHandcardNum() != qMax(player->getHp(), 0)) {
            return TriggerList{{player, {objectName()}}};
        }
        return TriggerList();
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart)
            return true;
        if (!ctx.owner->askForSkillInvoke(objectName()))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (event == EventPhaseChanging) {
            player->skip(Player::Draw, true);
            room->setPlayerFlag(player, "ThFushengUsed");
            QStringList draws;
            QStringList discards;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (!player->isAlive())
                    break;
                QStringList choices{"draw"};
                if (player->canDiscard(player, "he"))
                    choices << "discard";
                const QString choice = room->askForChoice(p, objectName(), choices.join("+"), QVariant::fromValue(player));
                LogMessage log;
                log.type = "#ThFusheng";
                log.from = p;
                log.arg = objectName() + ":" + choice;
                room->sendLog(log);
                if (choice == "discard") {
                    discards << p->objectName();
                    room->askForDiscard(player, objectName(), 1, 1, false, true);
                } else {
                    draws << p->objectName();
                    player->drawCards(1, objectName());
                }
            }
            player->setTag("ThFushengDraws", draws);
            player->setTag("ThFushengDiscards", discards);
            return false;
        }

        room->setPlayerFlag(player, "-ThFushengUsed");
        room->sendCompulsoryTriggerLog(player, objectName());
        const bool give = player->getHandcardNum() > qMax(player->getHp(), 0);
        const QStringList names = player->getTag(give ? "ThFushengDraws" : "ThFushengDiscards").toStringList();
        player->removeTag("ThFushengDraws");
        player->removeTag("ThFushengDiscards");
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!names.contains(p->objectName()) || !p->isAlive() || !player->isAlive())
                continue;
            ServerPlayer *giver = give ? player : p;
            ServerPlayer *receiver = give ? p : player;
            if (giver->isNude())
                continue;
            const Card *card = room->askForExchange(giver, objectName(), 1, 1, true,
                                                    "@thfusheng-give:" + receiver->objectName(), false);
            if (card && !card->getSubcards().isEmpty())
                room->giveCard(giver, receiver, card, objectName());
        }
        return false;
    }
};

// ---------------------------------------------------------------- yuki013

class ThSaozang : public TriggerSkillV2
{
public:
    ThSaozang() : TriggerSkillV2("thsaozang") { events << EventPhaseStart << CardsMoveOneTime << EventPhaseEnd; }

    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || player->getPhase() != Player::Discard || !player->hasSkill(objectName(), true))
            return false;
        if (event == EventPhaseStart) {
            player->removeTag("ThSaozang");
        } else if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != player || move.to_place != Player::DiscardPile
                || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD)
                return false;
            QStringList types = player->getTag("ThSaozang").toStringList();
            foreach (int id, move.card_ids) {
                const QString type = Sanguosha->getEngineCard(id)->getType();
                if (!types.contains(type))
                    types << type;
            }
            player->setTag("ThSaozang", types);
        }
        return false;
    }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event != EventPhaseEnd || !player || !player->isAlive() || !player->hasSkill(objectName())
            || player->getPhase() != Player::Discard || player->getTag("ThSaozang").toStringList().isEmpty()
            || victims(room, player).isEmpty())
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    // One discard per type; a refusal ends the rest.
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const int times = player->getTag("ThSaozang").toStringList().length();
        player->removeTag("ThSaozang");
        for (int i = 0; i < times && player->isAlive(); ++i) {
            const QList<ServerPlayer *> targets = victims(room, player);
            if (targets.isEmpty())
                break;
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@thsaozang", true, true);
            if (!target)
                break;
            room->broadcastSkillInvoke(objectName());
            const int id = room->askForCardChosen(player, target, "h", objectName(), false, Card::MethodDiscard);
            if (id >= 0)
                room->throwCard(id, target, player);
        }
        return false;
    }

private:
    static QList<ServerPlayer *> victims(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> result;
        foreach (ServerPlayer *p, room->getOtherPlayers(player))
            if (player->canDiscard(p, "h"))
                result << p;
        return result;
    }
};

class ThXuqu : public TriggerSkillV2
{
public:
    ThXuqu() : TriggerSkillV2("thxuqu") { events << CardsMoveOneTime; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || isOwnTurn(player)
            || room->getOtherPlayers(player).isEmpty())
            return TriggerList();
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player || !move.from_places.contains(Player::PlaceHand)
            || (move.to == player && move.to_place == Player::PlaceEquip))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(),
                                                        "@thxuqu", true, true);
        if (!target)
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    {
        target->drawCards(1, objectName());
        return false;
    }
};

// ---------------------------------------------------------------- yuki014

class ThQiebao : public TriggerSkillV2
{
public:
    ThQiebao() : TriggerSkillV2("thqiebao") { events << CardUsed << BeforeCardsMove; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player)
            return result;
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from != player || !use.card || !use.card->isKindOf("Peach"))
                return result;
            bool other = false;
            foreach (ServerPlayer *to, use.to)
                if (to != use.from)
                    other = true;
            if (!other)
                return result;
            foreach (ServerPlayer *owner, room->getAlivePlayers())
                if (owner->hasSkill(objectName()) && !owner->isKongcheng())
                    result[owner] << objectName();
            return result;
        }
        // Someone other than the owner gaining another character's hand or equipped cards.
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player->isAlive() || !player->hasSkill(objectName()) || player->isKongcheng() || !move.from || !move.to
            || move.from == move.to || move.to == player || move.to_place != Player::PlaceHand || stolen(move).isEmpty())
            return result;
        result[player] << objectName();
        return result;
    }

    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QString prompt = "@thqiebao";
        if (event == BeforeCardsMove) {
            const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
            prompt = QString("@thqiebaomove:%1:%2:%3")
                         .arg(move.from->objectName())
                         .arg(move.to->objectName())
                         .arg(stolen(move).length());
        }
        const Card *card = room->askForCard(ctx.owner, "slash", prompt, *ctx.original_data, objectName());
        if (!card)
            return false;
        ctx.extra_data = card->isRed();
        return true;
    }

    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        if (event == CardUsed) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            foreach (ServerPlayer *to, use.to)
                if (to != use.from && !use.nullified_list.contains(to->objectName()))
                    use.nullified_list << to->objectName();
            *ctx.original_data = QVariant::fromValue(use);
            if (ctx.extra_data.toBool() && owner->isAlive()) {
                const QList<int> ids = use.card->isVirtualCard() ? use.card->getSubcards()
                                                                 : QList<int>{use.card->getEffectiveId()};
                QList<int> available;
                foreach (int id, ids)
                    if (room->getCardPlace(id) == Player::PlaceTable)
                        available << id;
                if (!available.isEmpty()) {
                    DummyCard dummy(available);
                    room->obtainCard(owner, &dummy);
                }
            }
            return false;
        }
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const QList<int> ids = stolen(move);
        if (ids.isEmpty() || !owner->isAlive())
            return false;
        move.removeCardIds(ids);
        *ctx.original_data = QVariant::fromValue(move);
        DummyCard dummy(ids);
        CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, owner->objectName(), objectName(), QString());
        room->obtainCard(owner, &dummy, reason, false);
        return false;
    }

private:
    static QList<int> stolen(const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        for (int i = 0; i < move.card_ids.length(); ++i) {
            const Player::Place place = move.from_places.value(i);
            if (place == Player::PlaceHand || place == Player::PlaceEquip)
                ids << move.card_ids.at(i);
        }
        return ids;
    }
};

// ---------------------------------------------------------------- yuki015

class ThLingta : public TriggerSkillV2
{
public:
    ThLingta() : TriggerSkillV2("thlingta") { events << EventPhaseChanging; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || (change.to != Player::Draw && change.to != Player::Play) || player->isSkipped(change.to))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const PhaseChangeStruct change = ctx.original_data->value<PhaseChangeStruct>();
        if (!ctx.owner->askForSkillInvoke(objectName(), QString::number(int(change.to))))
            return false;
        room->broadcastSkillInvoke(objectName());
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.owner->skip(ctx.original_data->value<PhaseChangeStruct>().to, true);
        ctx.owner->gainMark("@bright");
        return false;
    }
};

// 威光: spend 炜 to skip the judge or discard phase, or to repeat the draw or play phase.
class ThWeiguang : public TriggerSkillV2
{
public:
    ThWeiguang() : TriggerSkillV2("thweiguang") { events << EventPhaseChanging; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getMark("@bright") <= 0)
            return TriggerList();
        const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if ((change.from == Player::Draw || change.from == Player::Play) && !player->hasFlag(flagFor(change.from)))
            return TriggerList{{player, {objectName()}}};
        if ((change.to == Player::Judge || change.to == Player::Discard) && !player->isSkipped(change.to)
            && !player->hasFlag(flagFor(change.to)))
            return TriggerList{{player, {objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const PhaseChangeStruct change = ctx.original_data->value<PhaseChangeStruct>();
        const bool repeat = change.from == Player::Draw || change.from == Player::Play;
        const Player::Phase phase = repeat ? change.from : change.to;
        if (!ctx.owner->askForSkillInvoke(objectName(), QString::number(int(phase))))
            return false;
        room->setPlayerFlag(ctx.owner, flagFor(phase));
        ctx.owner->loseMark("@bright");
        room->broadcastSkillInvoke(objectName());
        ctx.extra_data = int(phase);
        return true;
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const Player::Phase phase = Player::Phase(ctx.extra_data.toInt());
        if (phase == Player::Judge || phase == Player::Discard) {
            ctx.owner->skip(phase);
            return false;
        }
        PhaseChangeStruct change = ctx.original_data->value<PhaseChangeStruct>();
        ctx.owner->insertPhase(change.from);
        change.to = change.from;
        *ctx.original_data = QVariant::fromValue(change);
        return false;
    }

private:
    static QString flagFor(Player::Phase phase) { return "thweiguang" + QString::number(int(phase)); }
};

class ThChuhui : public TriggerSkillV2
{
public:
    ThChuhui() : TriggerSkillV2("thchuhui")
    {
        events << GameStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        ctx.owner->gainMark("@bright");
        return false;
    }
};

// ---------------------------------------------------------------- yuki016

class ThKujieViewAs : public ViewAsSkillV2
{
public:
    ThKujieViewAs() : ViewAsSkillV2("thkujiev", 1)
    {
        setPhaseName("Play");
        attached_lord_skill = true;
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *owner = attachedParentOwner(request, "thkujie");
        return owner && owner->hasSkill("thkujie") && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        const Player *self = request.initiator;
        if (!self || !card || !request.selectedCardIds.isEmpty() || card->hasFlag("using") || !card->isRed()
            || !card->isKindOf("BasicCard"))
            return false;
        const int id = card->getEffectiveId();
        return self->handCards().contains(id) && self->canDiscard(self, id);
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return selected.isEmpty() && candidate && candidate == attachedParentOwner(request, "thkujie");
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return selected.size() == 1 && canSelectTarget(request, {}, selected.first());
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThKujieCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *owner) const override
    {
        if (!owner || !owner->isAlive() || !owner->hasSkill("thkujie"))
            return ContinueEffects;
        Room *room = owner->getRoom();
        room->broadcastSkillInvoke("thkujie");
        room->notifySkillInvoked(owner, "thkujie");
        room->loseHp(owner, 1, true, ctx.initiator, "thkujie");
        if (owner->isAlive())
            room->addPlayerMark(owner, "kujie-invoke");
        return ContinueEffects;
    }
};

class ThKujie : public TriggerSkillV2
{
public:
    ThKujie() : TriggerSkillV2("thkujie")
    {
        events << GameStart << EventPhaseStart << EventAcquireSkill << EventLoseSkill << Death;
        global = true;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        syncAttachedChildren(room, objectName(), "thkujiev", false);
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

// After the turn in which 苦戒 was used, its owner recovers 2 per use.
class ThKujieRecover : public TriggerSkillV2
{
public:
    ThKujieRecover() : TriggerSkillV2("#thkujie")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || player->getPhase() != Player::NotActive)
            return result;
        foreach (ServerPlayer *p, room->getAlivePlayers())
            if (p->getMark("kujie-invoke") > 0 && p->hasSkill(objectName()))
                result[p] << objectName();
        return result;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *owner = ctx.owner;
        const int times = owner->getMark("kujie-invoke");
        room->setPlayerMark(owner, "kujie-invoke", 0);
        for (int i = 0; i < times && owner->isAlive() && owner->isWounded(); ++i) {
            room->sendCompulsoryTriggerLog(owner, "thkujie");
            room->recover(owner, RecoverStruct("thkujie", owner, 2));
        }
        return false;
    }
};

class ThYinbi : public TriggerSkillV2
{
public:
    ThYinbi() : TriggerSkillV2("thyinbi") { events << HpChanged; markOwnerOnly(this); }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || data.userType() != qMetaTypeId<DamageStruct>())
            return result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.to != player || damage.damage <= 0)
            return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()) && player->getHp() + damage.damage <= owner->getHp())
                result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(objectName(), QVariant::fromValue(ctx.invoker)))
            return false;
        room->broadcastSkillInvoke(objectName());
        ctx.targets = {ctx.invoker};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        ServerPlayer *owner = ctx.owner;
        room->recover(target, RecoverStruct(objectName(), owner, damage.damage));
        if (!owner->isAlive())
            return false;
        DamageStruct redirected(damage.reason, damage.from && damage.from->isAlive() ? damage.from : nullptr, owner,
                                damage.damage, damage.nature);
        redirected.card = damage.card;
        room->damage(redirected);
        return false;
    }
};

// ---------------------------------------------------------------- yuki017

class ThMingling : public TriggerSkillV2
{
public:
    ThMingling() : TriggerSkillV2("thmingling")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || !player->isAlive() || !player->hasSkill(objectName())
            || (damage.nature != DamageStruct::Fire && damage.nature != DamageStruct::Thunder))
            return TriggerList();
        return TriggerList{{player, {objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->broadcastSkillInvoke(objectName());
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.nature == DamageStruct::Fire)
            return true;
        ++damage.damage;
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

class ThChuanshangViewAs : public ViewAsSkillV2
{
public:
    ThChuanshangViewAs() : ViewAsSkillV2("thchuanshang") { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *to) const override
    {
        return request.initiator && selected.isEmpty() && to && request.initiator->inMyAttackRange(to);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    {
        return selected.length() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThChuanshangCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive())
            return ContinueEffects;
        const bool had = target->getMark("@drowning") > 0;
        target->gainMark("@drowning");
        if (had && source->isAlive())
            source->getRoom()->loseHp(source, 1, true, source, objectName());
        return ContinueEffects;
    }
};

class ThChuanshang : public TriggerSkillV2
{
public:
    ThChuanshang() : TriggerSkillV2("thchuanshang")
    {
        events << EventPhaseStart;
        view_as_skill = new ThChuanshangViewAs;
    }

    // Any character with 溺 judges at its finish phase while a 船殇 owner lives.
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish || player->getMark("@drowning") <= 0)
            return TriggerList();
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(objectName()))
                return TriggerList{{owner, {objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.invoker;
        if (!target || !target->isAlive() || target->getMark("@drowning") <= 0)
            return false;
        LogMessage log;
        log.type = "#ThChuanshang";
        log.from = target;
        log.arg = objectName();
        room->sendLog(log);
        JudgeStruct judge;
        judge.pattern = ".|heart";
        judge.good = true;
        judge.reason = objectName();
        judge.who = target;
        room->judge(judge);
        if (judge.isGood())
            target->loseMark("@drowning", qMin(2, target->getMark("@drowning")));
        else if (judge.card && judge.card->isBlack())
            target->gainMark("@drowning");
        return false;
    }
};

class ThChuanshangMaxCards : public MaxCardsSkillV2
{
public:
    ThChuanshangMaxCards() : MaxCardsSkillV2("#thchuanshang") { setHolderSelector(CorrectSkill_System); }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const int marks = ctx.primary ? ctx.primary->getMark("@drowning") : 0;
        return marks > 0 ? CorrectSkillResult::useAmount(-marks) : CorrectSkillResult::noEffect();
    }
};

// ---------------------------------------------------------------- yuki018

class ThLingdieViewAs : public ViewAsSkillV2
{
public:
    ThLingdieViewAs() : ViewAsSkillV2("thlingdie", 1) { setPhaseName("Play"); }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 2; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->hasFlag("ThLingdieDisabled");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && ownsHandCard(request.initiator, card);
    }

    bool willThrowSelectedCards() const override { return false; }

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

    QString historyKey(const ActiveSkillRequest &) const override { return "ThLingdieCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        ServerPlayer *payer = ctx.initiator;
        if (!source || !payer || !target || !target->isAlive() || !ctx.use_card || ctx.use_card->subcardsLength() != 1)
            return ContinueEffects;
        Room *room = source->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (!payer->handCards().contains(id))
            return ContinueEffects;
        const bool heart = Sanguosha->getCard(id)->getSuit() == Card::Heart;
        room->giveCard(payer, target, ctx.use_card, objectName());
        QList<ServerPlayer *> victims;
        foreach (ServerPlayer *p, room->getOtherPlayers(target))
            if (!p->isKongcheng())
                victims << p;
        if (!victims.isEmpty() && target->isAlive()) {
            ServerPlayer *victim = room->askForPlayerChosen(target, victims, objectName(), "@thlingdie");
            if (victim) {
                LogMessage log;
                log.type = "#ThXinhuaView";
                log.from = target;
                log.to << victim;
                room->sendLog(log);
                room->showAllCards(victim, target);
            }
        }
        if (heart) {
            if (target->isAlive())
                target->drawCards(1, objectName());
        } else if (source->isAlive()) {
            room->setPlayerFlag(source, "ThLingdieDisabled");
        }
        return ContinueEffects;
    }
};

class ThLingdie : public TriggerSkillV2
{
public:
    ThLingdie() : TriggerSkillV2("thlingdie")
    {
        events << EventPhaseChanging;
        view_as_skill = new ThLingdieViewAs;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (player && player->hasFlag("ThLingdieDisabled"))
            room->setPlayerFlag(player, "-ThLingdieDisabled");
        return false;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class ThWushou : public TriggerSkillV2
{
public:
    ThWushou() : TriggerSkillV2("thwushou")
    {
        events << Damaged;
        frequency = Frequent;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getHandcardNum() >= 4)
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
        const int n = 4 - ctx.owner->getHandcardNum();
        if (n > 0)
            ctx.owner->drawCards(n, objectName());
        return false;
    }
};

class ThFuyueViewAs : public ViewAsSkillV2
{
public:
    ThFuyueViewAs() : ViewAsSkillV2("thfuyuev")
    {
        setPhaseName("Play");
        attached_lord_skill = true;
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *lord = attachedParentOwner(request, "thfuyue");
        return lord && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->getKingdom() == "yuki"
            && !request.initiator->isKongcheng() && lord->getHp() == 1 && !lord->isKongcheng();
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        const Player *lord = attachedParentOwner(request, "thfuyue");
        return selected.isEmpty() && candidate && candidate == lord && lord->hasLordSkill("thfuyue")
            && lord->getHp() == 1 && request.initiator->canPindian(lord);
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return selected.size() == 1 && canSelectTarget(request, {}, selected.first());
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ThFuyueCard"; }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *lord) const override
    {
        ServerPlayer *source = ctx.initiator;
        if (!source || !lord || !lord->isAlive() || !source->isAlive())
            return ContinueEffects;
        Room *room = source->getRoom();
        room->broadcastSkillInvoke("thfuyue");
        room->notifySkillInvoked(lord, "thfuyue");
        if (room->askForChoice(lord, "thfuyue", "accept+reject", QVariant::fromValue(source)) != "accept"
            || !source->canPindian(lord))
            return ContinueEffects;
        if (!source->pindian(lord, "thfuyue") && lord->isAlive())
            room->recover(lord, RecoverStruct("thfuyue", source));
        return ContinueEffects;
    }
};

class ThFuyue : public TriggerSkillV2
{
public:
    ThFuyue() : TriggerSkillV2("thfuyue$")
    {
        events << GameStart << EventPhaseStart << EventAcquireSkill << EventLoseSkill << Death << GeneralShown
               << GeneralHidden;
        global = true;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        syncAttachedChildren(room, objectName(), "thfuyuev", true);
        return true;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

}

ThHuanfaCard::ThHuanfaCard() { setSkillName("thhuanfa"); mute = true; }
ThYuanqiCard::ThYuanqiCard() { setSkillName("thyuanqi"); mute = true; }
ThBingpuCard::ThBingpuCard() { setSkillName("thbingpu"); mute = true; }
ThDongmoCard::ThDongmoCard() { setSkillName("thdongmo"); mute = true; }
ThKujieCard::ThKujieCard() { setSkillName("thkujiev"); mute = true; }
ThChuanshangCard::ThChuanshangCard() { setSkillName("thchuanshang"); mute = true; }
ThLingdieCard::ThLingdieCard() { setSkillName("thlingdie"); mute = true; }
ThFuyueCard::ThFuyueCard() { setSkillName("thfuyuev"); mute = true; }

TouhouYukiPackage::TouhouYukiPackage()
    : Package("touhou-yuki")
{
    General *yuki001 = new General(this, "yuki001$", "yuki");
    yuki001->addSkill(new ThJianmo);
    yuki001->addSkill(new ThJianmoDiscard);
    related_skills.insert("thjianmo", "#thjianmo");
    yuki001->addSkill(new ThErchong);
    yuki001->addSkill(new ThChundu);
    yuki001->addRelateSkill("thhuanfa");
    yuki001->addRelateSkill("ikzhuji");

    General *yuki002 = new General(this, "yuki002", "yuki");
    yuki002->addSkill(new ThZuishang);
    yuki002->addSkill(new ThZuishangTargetMod);
    related_skills.insert("thzuishang", "#thzuishang");
    yuki002->addSkill(new ThXugu);

    General *yuki003 = new General(this, "yuki003", "yuki");
    yuki003->addSkill(new ThShenzhan);
    yuki003->addSkill(new ThHunqie);
    yuki003->addSkill(new ThDaojian);

    General *yuki004 = new General(this, "yuki004", "yuki");
    yuki004->addSkill(new ThZhancao);

    General *yuki005 = new General(this, "yuki005", "yuki", 3, false);
    yuki005->addSkill(new ThMoji);
    yuki005->addSkill(new ThYuanqi);

    General *yuki006 = new General(this, "yuki006", "yuki", 3);
    yuki006->addSkill(new ThDunjia);
    yuki006->addSkill(new ThQingming);

    General *yuki007 = new General(this, "yuki007", "yuki", 3);
    yuki007->addSkill(new ThChouce);
    yuki007->addSkill(new ThChouceRecord);
    yuki007->addSkill(new ThZhanshi);
    related_skills.insert("thchouce", "#thchouce");

    General *yuki008 = new General(this, "yuki008", "yuki", 3);
    yuki008->addSkill(new ThZiyun);
    yuki008->addSkill(new ThChuiji);

    General *yuki009 = new General(this, "yuki009", "yuki");
    yuki009->addSkill(new ThLingya);
    yuki009->addSkill(new ThHeimu);

    General *yuki010 = new General(this, "yuki010", "yuki");
    yuki010->addSkill(new ThHanpo);
    yuki010->addSkill(new ThJidong);
    yuki010->addSkill(new ThBingpu);

    General *yuki011 = new General(this, "yuki011", "yuki", 3);
    yuki011->addSkill(new ThDongmo);
    yuki011->addSkill(new ThLinhan);

    General *yuki012 = new General(this, "yuki012", "yuki");
    yuki012->addSkill(new ThFusheng);

    General *yuki013 = new General(this, "yuki013", "yuki", 3);
    yuki013->addSkill(new ThSaozang);
    yuki013->addSkill(new ThXuqu);

    General *yuki014 = new General(this, "yuki014", "yuki");
    yuki014->addSkill(new ThQiebao);

    General *yuki015 = new General(this, "yuki015", "yuki");
    yuki015->addSkill(new ThLingta);
    yuki015->addSkill(new ThWeiguang);
    yuki015->addSkill(new ThChuhui);

    General *yuki016 = new General(this, "yuki016", "yuki");
    yuki016->addSkill(new ThKujie);
    yuki016->addSkill(new ThKujieRecover);
    related_skills.insert("thkujie", "#thkujie");
    yuki016->addSkill(new ThYinbi);

    General *yuki017 = new General(this, "yuki017", "yuki");
    yuki017->addSkill(new ThMingling);
    yuki017->addSkill(new ThChuanshang);
    yuki017->addSkill(new ThChuanshangMaxCards);
    related_skills.insert("thchuanshang", "#thchuanshang");

    General *yuki018 = new General(this, "yuki018$", "yuki", 3, false);
    yuki018->addSkill(new ThLingdie);
    yuki018->addSkill(new ThWushou);
    yuki018->addSkill(new ThFuyue);

    skills << new ThHuanfa << new ThZuishangGiven << new ThKujieViewAs << new ThFuyueViewAs;

    addMetaObject<ThHuanfaCard>();
    addMetaObject<ThYuanqiCard>();
    addMetaObject<ThBingpuCard>();
    addMetaObject<ThDongmoCard>();
    addMetaObject<ThKujieCard>();
    addMetaObject<ThChuanshangCard>();
    addMetaObject<ThLingdieCard>();
    addMetaObject<ThFuyueCard>();
}

ADD_PACKAGE(TouhouYuki)
