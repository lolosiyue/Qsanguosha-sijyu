#include "sp.h"
#include "util.h"
#include <QScopeGuard>
#include "skill-instance-utils.h"
#include "skill-declaration.h"
//#include "client.h"
//#include "general.h"
//#include "skill.h"
//#include "standard-generals.h"
#include "engine.h"
#include "maneuvering.h"
//#include "json.h"
#include "settings.h"
#include "clientplayer.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "yjcm2013.h"
#include "wind.h"

namespace {

// MethodNone prompts carry UNKNOWN but still require the exact selector.
bool isPromptRequest(const ActiveSkillRequest &request, const QString &pattern)
{
    return request.initiator && request.pattern == pattern
        && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
            || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            || request.reason == CardUseStruct::CARD_USE_REASON_UNKNOWN);
}

bool matchesFilter(const ActiveSkillRequest &request, const Card *card, const QString &pattern)
{
    return request.initiator && card && card->getEffectiveId() >= 0 && !card->hasFlag("using")
        && !request.selectedCardIds.contains(card->getEffectiveId())
        && Sanguosha->matchExpPattern(pattern, request.initiator, card);
}

// Replay the selection in order, exactly as the server rebuilds it.
bool replaySelection(const ViewAsSkillV2 *skill, const ActiveSkillRequest &request)
{
    ActiveSkillRequest prefix = request;
    prefix.selectedCardIds.clear();
    foreach (int id, request.selectedCardIds) {
        const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
        if (!card || !skill->canSelectCard(prefix, card)) return false;
        prefix.selectedCardIds << id;
    }
    return true;
}

// canWake() consumes the grant and logs it; triggerable() may only look.
bool hasWakeGrant(const Player *player, const QString &skill)
{
    return !player->getTag(skill + "_SKILLCANWAKE").toStringList().isEmpty();
}

// Query committed damage, including damage dealt before the skill was acquired.
int spDamageThisTurn(Room *room, const ServerPlayer *player, bool playOnly = false)
{
    const QVariant turn = room->historyScopes().value("turn_id");
    if (turn.toLongLong() <= 0) return -1;
    QVariantMap filter{{"turn_id", turn}, {"from", player->objectName()}, {"limit", 100}};
    int total = 0;
    for (;;) {
        const QVariantMap page = room->queryActualDamage(filter);
        if (!page.value("complete").toBool()) return -1;
        for (const QVariant &entry : page.value("facts").toList()) {
            const QVariantMap fact = entry.toMap();
            if (playOnly) {
                const QVariantMap phase = room->historyEvent(fact.value("phase_id").toLongLong());
                if (phase.isEmpty()) return -1;
                if (phase.value("data").toMap().value("phase").toInt() != Player::Play) continue;
            }
            total += fact.value("data").toMap().value("amount").toInt();
        }
        if (!page.value("has_more").toBool()) break;
        filter.insert("watermark", page.value("watermark"));
        filter.insert("after", page.value("next_after"));
    }
    return total;
}

// V2 records history under the activation skill; conversions keep the card's own key.
QString cardHistoryKey(const QString &cardName, const QString &fallback)
{
    Card *card = Sanguosha->cloneCard(cardName);
    if (!card) return fallback;
    const QString key = card->getClassName();
    card->deleteLater();
    return key;
}

}

class Lirang : public TriggerSkillV2 {
public:
    Lirang() : TriggerSkillV2("lirang") {
        events << CardsMoveOneTime;
        frequency = Frequent;
    }

    // The retired identity skill resolved discards at priority 3.
    bool usesEventPriority() const override { return !Config.EnableHegemony; }
    int getPriority(TriggerEvent) const override { return 3; }

    void record(TriggerEvent, Room *, ServerPlayer *player, SkillContext &ctx) const override {
        // CardsMoveOneTime is delivered once to each seat; update each owner only at its seat.
        if (!ctx.owner || ctx.owner != player || !ctx.original_data) return;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QList<int> pending = ListV2I(ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "discarded").toList());
        const bool discard = (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD;
        // Track only card IDs per instance, including direct-to-discard V2 payments.
        for (int i = 0; i < move.card_ids.size(); ++i) {
            const int id = move.card_ids.at(i);
            const bool owned = move.from == ctx.owner && i < move.from_places.size()
                && (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip);
            if (discard && (owned || pending.contains(id))
                && (move.to_place == Player::PlaceTable || move.to_place == Player::DiscardPile)) {
                if (!pending.contains(id)) pending << id;
            } else {
                pending.removeAll(id);
            }
        }
        const QVariantList state = ListI2V(pending);
        if (ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "discarded").toList() != state)
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "discarded", state);
    }

    QList<int> available(Room *room, ServerPlayer *owner, int instanceID,
                         const CardsMoveOneTimeStruct &move) const {
        QList<int> cards;
        if (move.to_place != Player::DiscardPile
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) return cards;
        const QList<int> pending = ListV2I(owner->getSkillInstanceStateValue(objectName(), instanceID, "discarded").toList());
        for (int id : move.card_ids) {
            if (pending.contains(id) && room->getCardPlace(id) == Player::DiscardPile) cards << id;
        }
        return cards;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            if (!available(room, player, id, move).isEmpty())
                result[player] << SkillInstanceUtils::formatName(objectName(), id);
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!ctx.owner || !ctx.original_data) return false;
        const QList<int> cards = available(room, ctx.owner, ctx.instanceID,
            ctx.original_data->value<CardsMoveOneTimeStruct>());
        if (cards.isEmpty() || room->getOtherPlayers(ctx.owner).isEmpty()) return false;
        if (!Config.EnableHegemony
            && !ctx.owner->askForSkillInvoke(objectName() + "$-1", *ctx.original_data)) return false;
        // An accepted concealed activation must distribute at least once after reveal.
        ctx.extra_data = QVariantMap{{"cards", ListI2V(cards)},
            {"optional", Config.EnableHegemony && ctx.owner->isSkillInstanceEffectAvailable(objectName(), ctx.instanceID)}};
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        ServerPlayer *kongrong = ctx.owner;
        if (!kongrong || !kongrong->isAlive()) return false;
        QList<int> cards = ListV2I(ctx.extra_data.toMap().value("cards").toList());
        for (int id : QList<int>(cards)) {
            if (room->getCardPlace(id) != Player::DiscardPile) cards.removeAll(id);
        }
        if (cards.isEmpty()) return false;
        room->broadcastSkillInvoke(objectName(), kongrong);
        const CardMoveReason previewReason(CardMoveReason::S_REASON_PREVIEW, kongrong->objectName(), objectName(), QString());
        const QList<ServerPlayer *> viewers{kongrong};
        QList<CardsMoveStruct> preview{CardsMoveStruct(cards, nullptr, kongrong,
            Player::DiscardPile, Player::PlaceHand, previewReason)};
        room->notifyMoveCards(true, preview, false, viewers);
        room->notifyMoveCards(false, preview, false, viewers);
        // Always retract the private preview, including interrupted distribution.
        const auto clearPreview = qScopeGuard([&] {
            if (cards.isEmpty()) return;
            QList<CardsMoveStruct> cleanup{CardsMoveStruct(cards, kongrong, nullptr,
                Player::PlaceHand, Player::DiscardPile, previewReason)};
            room->notifyMoveCards(true, cleanup, true, viewers);
            room->notifyMoveCards(false, cleanup, false, viewers);
        });
        bool optional = ctx.extra_data.toMap().value("optional").toBool();
        const CardMoveReason reason(CardMoveReason::S_REASON_PREVIEWGIVE, kongrong->objectName());
        while (kongrong->isAlive() && !cards.isEmpty()) {
            const QList<int> before = cards;
            if (!room->askForYiji(kongrong, cards, objectName(), true, true, optional, -1,
                    room->getOtherPlayers(kongrong), reason, "@lirang-distribute", optional)) break;
            optional = true;
            QList<int> moved;
            for (int id : before) {
                if (room->getCardPlace(id) != Player::DiscardPile) {
                    moved << id;
                    cards.removeAll(id);
                }
            }
            if (moved.isEmpty()) break;
            QList<CardsMoveStruct> given{CardsMoveStruct(moved, kongrong, nullptr,
                Player::PlaceHand, Player::PlaceTable, previewReason)};
            room->notifyMoveCards(true, given, true, viewers);
            room->notifyMoveCards(false, given, true, viewers);
        }
        return false;
    }
};

class Xiongyi : public ViewAsSkillV2 {
public:
    Xiongyi() : ViewAsSkillV2("xiongyi") {
        frequency = Limited;
        limit_mark = "@arise";
        m_baseAmount = 3;
    }

    bool canActivate(const ActiveSkillRequest &request) const override {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark(limit_mark) > 0;
    }
    TargetMode targetMode() const override { return Config.EnableHegemony ? NoTarget : SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected,
                         const Player *target) const override {
        return !Config.EnableHegemony && target && target->isAlive() && !selected.contains(target);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override {
        return !Config.EnableHegemony || targets.isEmpty();
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "XiongyiCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override {
        // The activation owner pays even if an interceptor delegates the effect.
        if (!ctx.initiator || ctx.initiator->getMark(limit_mark) <= 0) return false;
        room->removePlayerMark(ctx.initiator, limit_mark);
        return true;
    }

    EffectFlow effect(SkillContext &ctx) const override {
        ServerPlayer *source = ctx.invoker;
        if (!source || !source->isAlive()) return FinishSkill;
        Room *room = source->getRoom();
        room->broadcastSkillInvoke(objectName(), source);
        room->doSuperLightbox(source, objectName());
        QList<ServerPlayer *> friends;
        if (Config.EnableHegemony) {
            for (ServerPlayer *p : room->getAlivePlayers()) {
                if (p->isFriendWith(source)) friends << p;
            }
        } else {
            friends = ctx.targets;
            if (!friends.contains(source)) friends << source;
        }
        room->sortByActionOrder(friends);
        // Preserve the dynamically determined friends while using V2 target interception.
        for (ServerPlayer *p : friends) skillEffect(ctx, p);

        if (!source->isAlive() || !source->isWounded()) return FinishSkill;
        if (!Config.EnableHegemony) {
            if (friends.size() <= room->getAlivePlayers().size() / 2)
                room->recover(source, RecoverStruct(objectName(), source));
            // Targets were processed above; do not let the dispatcher draw a second time.
            return FinishSkill;
        }
        const int count = source->getPlayerNumWithSameKingdom(objectName(), QString(), MaxCardsType::Normal);
        for (const QString &kingdom : Sanguosha->getKingdoms()) {
            if (kingdom == "god" || (source->getRole() == "careerist"
                    ? kingdom == "careerist" : kingdom == source->getKingdom())) continue;
            const int other = source->getPlayerNumWithSameKingdom(objectName(), kingdom, MaxCardsType::Normal);
            if (other > 0 && other < count) return FinishSkill;
        }
        room->recover(source, RecoverStruct(objectName(), source));
        return FinishSkill;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
};

class Sijian : public TriggerSkillV2 {
public:
    Sijian() : TriggerSkillV2("sijian") { events << CardsMoveOneTime; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        // CardsMoveOneTime is dispatched per seat; only the losing owner may trigger.
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from == player && move.from_places.contains(Player::PlaceHand) && move.is_last_handcard) {
            for (ServerPlayer *p : room->getOtherPlayers(player)) {
                if (player->canDiscard(p, "he")) return TriggerList{{player, QStringList{objectName()}}};
            }
        }
        return TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!ctx.owner) return false;
        QList<ServerPlayer *> targets;
        for (ServerPlayer *p : room->getOtherPlayers(ctx.owner)) {
            if (ctx.owner->canDiscard(p, "he")) targets << p;
        }
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "sijian-invoke", true);
        if (!target) return false;
        ctx.targets = QList<ServerPlayer *>{target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override {
        if (ctx.owner && ctx.owner->isAlive() && target && target->isAlive()
            && ctx.owner->canDiscard(target, "he")) {
            room->broadcastSkillInvoke(objectName(), target->isLord() ? 2 : 1, ctx.owner);
            const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodDiscard);
            if (id >= 0) room->throwCard(id, target, ctx.owner);
        }
        return false;
    }
};

class Shuangren : public TriggerSkillV2 {
public:
    Shuangren() : TriggerSkillV2("shuangren") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Play)
            return TriggerList();
        for (ServerPlayer *p : room->getOtherPlayers(player)) {
            if (player->canPindian(p)) return TriggerList{{player, QStringList{objectName()}}};
        }
        return TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!ctx.owner) return false;
        QList<ServerPlayer *> targets;
        for (ServerPlayer *p : room->getOtherPlayers(ctx.owner)) {
            if (ctx.owner->canPindian(p)) targets << p;
        }
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "@shuangren", true);
        if (!target) return false;
        ctx.targets = QList<ServerPlayer *>{target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override {
        ServerPlayer *owner = ctx.owner;
        if (!owner || !target || !owner->canPindian(target)) return false;
        room->broadcastSkillInvoke(objectName(), 1, owner);
        // Pindian is the effect; never pay/move its cards inside the cancellable cost.
        if (!owner->pindian(target, objectName())) {
            room->broadcastSkillInvoke(objectName(), 3, owner);
            return true;
        }
        if (!owner->isAlive()) return false;
        QList<ServerPlayer *> targets;
        for (ServerPlayer *p : room->getAlivePlayers()) {
            if (owner->canSlash(p, nullptr, false) && (!Config.EnableHegemony || p == target || p->isFriendWith(target))) targets << p;
        }
        if (targets.isEmpty()) return false;
        ServerPlayer *to = room->askForPlayerChosen(owner, targets, "shuangren-slash", "@dummy-slash");
        if (!to) return false;
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_shuangren");
        // Identity keeps its Slash history; Hegemony grants a use outside the count.
        slash->deleteLater();
        room->useCardFromSkillEffect(CardUseStruct(slash, owner, to), ctx, !Config.EnableHegemony);
        return false;
    }
    int getEffectIndex(const ServerPlayer *, const Card *) const override { return 2; }
};

class ShuangrenTargetMod : public TargetModSkillV2 {
public:
    ShuangrenTargetMod() : TargetModSkillV2("#shuangren-slash-ndl") { setBaseAmount(999); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override {
        if (ctx.modType == TargetModSkill::DistanceLimit && ctx.card
            && ctx.card->getSkillName() == "shuangren") return CorrectSkillResult::useAmount(ctx.currentAmount);
        return CorrectSkillResult::noEffect();
    }
};

class Shushen : public TriggerSkillV2 {
public:
    Shushen() : TriggerSkillV2("shushen") { events << HpRecover; m_baseAmount = 2; }
    int getBaseAmount() const override { return Config.EnableHegemony ? 1 : 2; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override {
        const int count = data.value<RecoverStruct>().recover;
        if (player && player->isAlive() && player->hasSkill(objectName()) && count > 0
            && !room->getOtherPlayers(player).isEmpty())
            return TriggerList{{player, {objectName() + "*" + QString::number(count)}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!ctx.owner) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner),
            objectName(), "shushen-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override {
        room->broadcastSkillInvoke(objectName(), target->getGeneralName().contains("liubei") ? 2 : 1, ctx.owner);
        if (!Config.EnableHegemony && target->isWounded()
            && room->askForChoice(ctx.owner, objectName(), "recover+draw", QVariant::fromValue(target)) == "recover")
            room->recover(target, RecoverStruct(objectName(), ctx.owner));
        else
            target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class Shenzhi : public TriggerSkillV2 {
public:
    Shenzhi() : TriggerSkillV2("shenzhi") { events << EventPhaseStart; }
    bool canPreshow() const override { return !Config.EnableHegemony; }
    Frequency getFrequency(const Player *target = nullptr) const override {
        return Config.EnableHegemony ? Frequent : TriggerSkillV2::getFrequency(target);
    }
    static bool canPay(ServerPlayer *owner) {
        if (!owner || !owner->isAlive() || owner->isKongcheng()) return false;
        if (Config.EnableHegemony) return true;
        for (const Card *card : owner->getHandcards())
            if (!owner->canDiscard(owner, card->getEffectiveId())) return false;
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override {
        return player && player->hasSkill(objectName()) && player->getPhase() == Player::Start && canPay(player)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!canPay(ctx.owner) || !room->askForSkillInvoke(ctx.owner, objectName())) return false;
        ctx.extra_data = ctx.owner->getHandcardNum();
        if (Config.EnableHegemony) {
            // A waived payment must retain the same prospective discard count.
            int count = 0;
            for (const Card *card : ctx.owner->getHandcards())
                if (!ctx.owner->isJilei(card)) ++count;
            ctx.extra_data = count;
        }
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!canPay(ctx.owner)) return false;
        if (Config.EnableHegemony) {
            // Hegemony pays all discardable cards; identity requires the whole hand.
            int count = 0;
            for (const Card *card : ctx.owner->getHandcards())
                if (!ctx.owner->isJilei(card)) ++count;
            ctx.extra_data = count;
            ctx.owner->throwAllHandCards();
            return true;
        }
        // Freeze the whole payment before nested discard events alter the hand or HP.
        DummyCard payment(ctx.owner->handCards());
        ctx.extra_data = payment.subcardsLength();
        room->throwCard(&payment, ctx.owner);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        if (ctx.owner && ctx.owner->isAlive() && ctx.extra_data.toInt() >= ctx.owner->getHp())
            room->recover(ctx.owner, RecoverStruct(ctx.owner, nullptr, getEffectiveAmount(ctx), objectName()));
        return false;
    }
};

static const Card *respondedCard(TriggerEvent triggerEvent, const QVariant &data)
{
    if (triggerEvent == CardUsed)
        return data.value<CardUseStruct>().card;
    if (triggerEvent == CardResponded)
        return data.value<CardResponseStruct>().m_card;
    return nullptr;
}

class SPMoonSpearSkill : public WeaponSkillV2
{
public:
    SPMoonSpearSkill() : WeaponSkillV2("sp_moonspear", "sp_moonspear")
    {
        events << CardUsed << CardResponded;
    }

    static QList<ServerPlayer *> targets(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *tmp, room->getAlivePlayers()) {
            if (player->inMyAttackRange(tmp))
                targets << tmp;
        }
        return targets;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = respondedCard(triggerEvent, data);
        if (!player || !WeaponSkillV2::triggerable(player) || player->hasFlag("CurrentPlayer")
            || !card || card->getTypeId()<1 || !card->isBlack() || targets(room, player).isEmpty())
            return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(player, targets(room, player), objectName(), "@sp_moonspear", true, true);
        if (!target) return false;
        ctx.targets = QList<ServerPlayer *>{target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->setEmotion(player, "weapon/moonspear");
        if (!room->askForCard(target, "jink", "@moon-spear-jink", QVariant(), Card::MethodResponse, player))
            room->damage(DamageStruct(objectName(), player, target, getEffectiveAmount(ctx)));
        return false;
    }
};

SPMoonSpear::SPMoonSpear(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("sp_moonspear");
}

class MoonSpearSkill : public WeaponSkillV2
{
public:
    MoonSpearSkill() : WeaponSkillV2("moon_spear", "moon_spear")
    {
        events << PreCardUsed << CardUsed << CardResponded;
    }

    // Announces the Slash the spear prompted, once it is actually used.
    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent != PreCardUsed || !player || player->hasFlag("CurrentPlayer")) return true;
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Slash")&&player->hasFlag("MoonspearUse")){
            player->setFlags("-MoonspearUse");
            room->setEmotion(player, "weapon/moonspear");
            LogMessage log;
            log.type = "#InvokeSkill";
            log.from = player;
            log.arg = "moon_spear";
            room->sendLog(log);
            room->notifySkillInvoked(player, "moon_spear");
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = respondedCard(triggerEvent, data);
        if (!player || !WeaponSkillV2::triggerable(player) || player->hasFlag("CurrentPlayer")
            || !card || card->getTypeId()<1 || !card->isBlack())
            return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        const bool previous = player->hasFlag("MoonspearUse");
        player->setFlags("MoonspearUse");
        const auto restore = qScopeGuard([&]() { player->setFlags(previous ? "MoonspearUse" : "-MoonspearUse"); });
        room->askForUseCard(player, "slash", "@moon-spear-slash", -1, Card::MethodUse, false);
        return false;
    }
};

MoonSpear::MoonSpear(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("moon_spear");
}

SPCardPackage::SPCardPackage()
    : Package("sp_cards")
{
    (new SPMoonSpear)->setParent(this);

    Card *moon_spear = new MoonSpear;
    moon_spear->setParent(this);

    skills << new SPMoonSpearSkill << new MoonSpearSkill;

    type = CardPack;
}

ADD_PACKAGE(SPCard)

HegemonySPPackage::HegemonySPPackage()
: Package("hegemony_sp")
{
    General *sp_heg_zhouyu = new General(this, "sp_heg_zhouyu", "wu", 3, true); // GSP 001
    sp_heg_zhouyu->addSkill("nosyingzi");
    sp_heg_zhouyu->addSkill("nosfanjian");

    General *sp_heg_xiaoqiao = new General(this, "sp_heg_xiaoqiao", "wu", 3, false); // GSP 002
    sp_heg_xiaoqiao->addSkill("tianxiang");
    sp_heg_xiaoqiao->addSkill("hongyan");
}
ADD_PACKAGE(HegemonySP)


Yongsi::Yongsi() : TriggerSkillV2("yongsi")
{
    events << DrawNCards << EventPhaseStart;
    frequency = Compulsory;
}

int Yongsi::getKingdoms(ServerPlayer *yuanshu) const
{
    QSet<QString> kingdom_set;
    Room *room = yuanshu->getRoom();
    foreach(ServerPlayer *p, room->getAlivePlayers())
        kingdom_set << p->getKingdom();

    return kingdom_set.size();
}

TriggerList Yongsi::triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const
{
    if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
    if (triggerEvent == DrawNCards ? data.value<DrawStruct>().reason == "draw_phase"
                                   : player->getPhase() == Player::Discard)
        return TriggerList{{player, QStringList{objectName()}}};
    return TriggerList();
}

bool Yongsi::effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *yuanshu, SkillContext &ctx) const
{
    const int x = getKingdoms(yuanshu);
    if (triggerEvent == DrawNCards) {
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num += x;
        *ctx.original_data = QVariant::fromValue(draw);

        LogMessage log;
        log.type = "#YongsiGood";
        log.from = yuanshu;
        log.arg = QString::number(x);
        log.arg2 = objectName();
        room->sendLog(log);

        room->broadcastSkillInvoke("yongsi", x % 2 + 1);
        room->notifySkillInvoked(yuanshu, objectName());
    } else {
        LogMessage log;
        log.type = yuanshu->getCardCount() > x ? "#YongsiBad" : "#YongsiWorst";
        log.from = yuanshu;
        log.arg = QString::number(log.type == "#YongsiBad" ? x : yuanshu->getCardCount());
        log.arg2 = objectName();
        room->sendLog(log);
        room->notifySkillInvoked(yuanshu, objectName());
        if (x > 0)
            room->askForDiscard(yuanshu, "yongsi", x, x, false, true);
    }
    return false;
}

class Weidi : public TriggerSkillV2
{
public:
    Weidi() : TriggerSkillV2("weidi")
    {
        frequency = Compulsory;
        events << GameStart << EventAcquireSkill << EventLoseSkill << DFDebut << Death << GeneralShown << GeneralHidden;
        global = true;
    }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        // Lord skills are real children of each Weidi copy. Their own V2 admission,
        // payment, quota and target effects therefore run with a valid activation ref.
        for (ServerPlayer *holder : room->getAllPlayers(true)) {
            QStringList names;
            QList<SkillInstanceRef> parents;
            if (holder->isAlive() && holder->hasSkill(objectName())) {
                for (int id : holder->getValidSkillInstanceIds(objectName()))
                    parents << SkillInstanceRef(holder->objectName(), SkillInstanceKey(objectName(), id));
                for (ServerPlayer *lord : room->getOtherPlayers(holder)) {
                    if (!lord->isLord()) continue;
                    for (const Skill *skill : lord->getVisibleSkillList())
                        if (skill->isLordSkill() && holder->hasLordSkill(skill->objectName())) names << skill->objectName();
                }
                names.removeDuplicates();
            }
            for (const SkillInstance &entry : holder->getSkillInstances()) {
                if (entry.source == SourceAttached && entry.parentRef.key.skillName == objectName()
                    && (!parents.contains(entry.parentRef) || !names.contains(entry.skillName)))
                    room->detachAttachedSkill(SkillInstanceRef(holder->objectName(), entry.key()));
            }
            for (const SkillInstanceRef &parent : parents)
                for (const QString &name : names)
                    if (holder->hasSkill(objectName()) && holder->hasLordSkill(name)
                        && !holder->isSkillInvalid(parent.key.skillName, parent.key.instanceID))
                        room->attachSkillToPlayer(holder, name, parent);
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};

class Yicong : public DistanceSkillV2
{
public:
    Yicong() : DistanceSkillV2("yicong")
    {
        setHolderSelector(CorrectSkill_Participants);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        int correct = 0;
        if (ctx.holder == ctx.primary && ctx.primary->getHp() > 2)
            correct -= ctx.currentAmount;
        if (ctx.holder == ctx.secondary && ctx.secondary->getHp() <= 2)
            correct += ctx.currentAmount;
        return correct != 0 ? CorrectSkillResult::useAmount(correct) : CorrectSkillResult::noEffect();
    }
};

class YicongEffect : public TriggerSkillV2
{
public:
    YicongEffect() : TriggerSkillV2("#yicong-effect")
    {
        events << HpChanged;
    }

    // Audio only: nothing is invoked, so it never enters the trigger-order menu.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return true;
        int hp = player->getHp();
        int index = 0;
        int reduce = 0;
        if (data.canConvert<RecoverStruct>()) {
            int rec = data.value<RecoverStruct>().recover;
            if (hp > 2 && hp - rec <= 2)
                index = 1;
        } else {
            if (data.canConvert<DamageStruct>()) {
                DamageStruct damage = data.value<DamageStruct>();
                reduce = damage.damage;
            } else if (!data.isNull()) {
                reduce = data.toInt();
            }
            if (hp <= 2 && hp + reduce > 2)
                index = 2;
        }

        if (index > 0) {
            if (player->getGeneralName() == "gongsunzan"
                || (player->getGeneralName() != "st_gongsunzan" && player->getGeneral2Name() == "gongsunzan"))
                index += 2;
            room->broadcastSkillInvoke("yicong", index);
        }
        return true;
    }
};

class Yanyu : public TriggerSkillV2
{
public:
    Yanyu() : TriggerSkillV2("yanyu") { events << EventPhaseStart << BeforeCardsMove << EventPhaseChanging; global = true; m_baseAmount = 3; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().from == Player::Play)
            for (ServerPlayer *player : room->getAllPlayers(true)) room->setPlayerProperty(player, "yanyu_receipts", QVariantMap());
        return true;
    }
    static QList<int> giftable(const CardsMoveOneTimeStruct &move, const QVariantMap &receipt)
    {
        QList<int> result;
        if (move.to_place != Player::DiscardPile || receipt.value("remaining").toInt() <= 0) return result;
        for (int id : move.card_ids) {
            const Card *card = Sanguosha->getCard(id);
            if (card && card->getType() == receipt.value("type").toString()) result << id;
        }
        return result;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event == EventPhaseStart) return false;
        if (event != BeforeCardsMove) return true;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        for (ServerPlayer *holder : room->getAlivePlayers()) {
            const QVariantMap receipts = holder->property("yanyu_receipts").toMap();
            for (auto it = receipts.cbegin(); it != receipts.cend(); ++it) {
                const QVariantMap receipt = it.value().toMap();
                if (giftable(move, receipt).isEmpty()) continue;
                SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = holder; ctx.invoker = actor ? actor : holder; ctx.initiator = holder;
                ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
                ctx.extra_data = QVariantMap{{"receipt", it.key()}}; ctx.amount = receipt.value("remaining").toInt();
                ctx.original_data = &data; ctx.current_event = event; contexts << ctx;
            }
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.extra_data.toMap().contains("receipt"))
            return ctx.owner && ctx.owner->property("yanyu_receipts").toMap().contains(ctx.extra_data.toMap().value("receipt").toString());
        return TriggerSkillV2::isSourceAvailable(room, ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (event != EventPhaseStart || !player || player->getPhase() != Player::Play) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner->isAlive() && owner->canDiscard(owner, "he")) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventPhaseStart) return true;
        const Card *card = room->askForCard(ctx.owner, "..", "@yanyu-discard", *ctx.original_data, Card::MethodNone, nullptr, false, objectName());
        if (!card || card->isVirtualCard() || !ctx.owner->canDiscard(ctx.owner, card->getEffectiveId())) return false;
        ctx.extra_data = QVariantMap{{"id", card->getEffectiveId()}, {"type", card->getType()}};
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventPhaseStart) return true;
        const int id = ctx.extra_data.toMap().value("id").toInt();
        if (room->getCardOwner(id) != ctx.owner || !ctx.owner->canDiscard(ctx.owner, id)) return false;
        room->throwCard(id, objectName(), ctx.owner);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        QVariantMap receipts = ctx.owner->property("yanyu_receipts").toMap();
        if (event == EventPhaseStart) {
            const QString type = ctx.extra_data.toMap().value("type").toString();
            receipts.insert(QString::number(ctx.activationRef.key.instanceID), QVariantMap{{"type", type}, {"remaining", getEffectiveAmount(ctx)},
                {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}});
            room->setPlayerProperty(ctx.owner, "yanyu_receipts", receipts);
            room->addPlayerMark(ctx.owner, type + "YanyuDiscard-PlayClear", getEffectiveAmount(ctx));
            return false;
        }
        const QString key = ctx.extra_data.toMap().value("receipt").toString();
        const int previous = ctx.owner->getMark("YanyuOnlyId");
        const auto restore = qScopeGuard([&]() { room->setPlayerMark(ctx.owner, "YanyuOnlyId", previous); });
        for (int count = 0; count < qMax(0, getEffectiveAmount(ctx)) && ctx.owner->isAlive(); ++count) {
            const QVariantMap receipt = ctx.owner->property("yanyu_receipts").toMap().value(key).toMap();
            const QList<int> ids = giftable(ctx.original_data->value<CardsMoveOneTimeStruct>(), receipt);
            if (ids.isEmpty()) break;
            int id = -1;
            {
                room->fillAG(ids, ctx.owner);
                const auto clearAG = qScopeGuard([&]() { room->clearAG(ctx.owner); });
                id = room->askForAG(ctx.owner, ids, true, objectName());
            }
            if (!ids.contains(id)) break;
            room->setPlayerMark(ctx.owner, "YanyuOnlyId", id + 1);
            const Card *card = Sanguosha->getCard(id);
            ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(),
                QString("@yanyu-give:::%1:%2\\%3").arg(card->objectName()).arg(card->getSuitString() + "_char").arg(card->getNumberString()), true, true);
            if (!target) break;
            ctx.extra_data = QVariantMap{{"receipt", key}, {"id", id}};
            skillEffect(ctx.current_event, ctx.invoker->getRoom(), ctx.invoker, ctx, target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap state = ctx.extra_data.toMap();
        const QString key = state.value("receipt").toString();
        const int id = state.value("id").toInt();
        QVariantMap receipts = ctx.owner->property("yanyu_receipts").toMap();
        QVariantMap receipt = receipts.value(key).toMap();
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        if (!giftable(move, receipt).contains(id)) return false;
        // Commit the individual allowance before obtaining can cause another move callback.
        receipt.insert("remaining", receipt.value("remaining").toInt() - 1);
        receipts.insert(key, receipt);
        room->setPlayerProperty(ctx.owner, "yanyu_receipts", receipts);
        room->removePlayerMark(ctx.owner, receipt.value("type").toString() + "YanyuDiscard-PlayClear");
        move.removeCardIds(QList<int>{id});
        *ctx.original_data = QVariant::fromValue(move);
        room->broadcastSkillInvoke(objectName(), 2);
        room->obtainCard(target, id);
        return false;
    }
};

class Xiaode : public TriggerSkillV2
{
public:
    Xiaode() : TriggerSkillV2("xiaode") { events << BuryVictim; }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return -2; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    { return ctx.owner && !ctx.owner->property("xiaode_grants").toMap().contains(QString::number(ctx.activationRef.key.instanceID)); }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        ServerPlayer *victim = data.value<DeathStruct>().who;
        if (!victim) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner->isAlive() && owner != victim && !owner->getTag("XiaodeVictimSkills").toMap().value(victim->objectName()).toStringList().isEmpty())
                result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *victim = ctx.original_data->value<DeathStruct>().who;
        if (!victim || !checkCustomUsage(ctx)) return false;
        const QStringList choices = ctx.owner->getTag("XiaodeVictimSkills").toMap().value(victim->objectName()).toStringList();
        if (choices.isEmpty() || !room->askForSkillInvoke(ctx.owner, objectName(), QVariant::fromValue(choices))) return false;
        ctx.choice = room->askForChoice(ctx.owner, objectName(), choices.join('+'));
        ctx.targets = {ctx.owner};
        return choices.contains(ctx.choice);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        const int id = room->acquireSkillFromEffect(target, ctx.choice, ctx);
        if (id <= 0) return false;
        QVariantMap grants = target->property("xiaode_grants").toMap();
        grants.insert(QString::number(ctx.activationRef.key.instanceID), QVariantMap{{"skill", ctx.choice}, {"id", id}});
        room->setPlayerProperty(target, "xiaode_grants", grants);
        room->addSkillInvalidity(target, "xiaode", "xiaode_grant_" + QString::number(id), "duration", ctx.activationRef.key.instanceID);
        target->setTag("XiaodeSkill", ctx.choice); // Legacy AI projection only.
        return false;
    }
};

class XiaodeEx : public TriggerSkillV2
{
public:
    XiaodeEx() : TriggerSkillV2("#xiaode") { events << EventPhaseChanging << EventLoseSkill << Death; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (event == Death) {
            ServerPlayer *victim = data.value<DeathStruct>().who;
            if (!victim) return true;
            QStringList choices = skills(victim->getGeneral()) + skills(victim->getGeneral2());
            choices.removeDuplicates();
            for (ServerPlayer *owner : room->findPlayersBySkillName("xiaode")) {
                QVariantMap cache = owner->getTag("XiaodeVictimSkills").toMap();
                cache.insert(victim->objectName(), choices);
                owner->setTag("XiaodeVictimSkills", cache);
            }
            return true;
        }
        const bool ending = event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive;
        const SkillChangeStruct change = data.value<SkillChangeStruct>();
        if (!ending && (event != EventLoseSkill || change.skillName != "xiaode" || change.instanceID <= 0)) return true;
        QVariantMap grants = player->property("xiaode_grants").toMap();
        const QVariantMap previous = grants;
        for (auto it = previous.cbegin(); it != previous.cend(); ++it) {
            if (!ending && it.key().toInt() != change.instanceID) continue;
            grants.remove(it.key());
            // Publish consumption before detach can synchronously re-enter this recorder.
            room->setPlayerProperty(player, "xiaode_grants", grants);
            const QVariantMap grant = it.value().toMap();
            room->removeSkillInvalidity(player, "xiaode", "xiaode_grant_" + QString::number(grant.value("id").toInt()), "duration", it.key().toInt());
            room->detachSkillFromPlayer(player, grant.value("skill").toString() + "#" + QString::number(grant.value("id").toInt()));
        }
        room->setPlayerProperty(player, "xiaode_grants", grants);
        if (grants.isEmpty()) player->removeTag("XiaodeSkill");
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
private:
    static QStringList skills(const General *general)
    {
        QStringList result;
        if (general) for (const Skill *skill : general->getSkillList())
            if (skill->isVisible() && !skill->isLordSkill() && skill->getFrequency() != Skill::Wake) result << skill->objectName();
        return result;
    }
};

MeibuFilter::MeibuFilter(const QString &skill_name)
 : FilterSkill(QString("#%1-filter").arg(skill_name)), n(skill_name)
{
}

bool MeibuFilter::viewFilter(const Card *to_select) const
{
    return to_select->getTypeId() == Card::TypeTrick;
}

const Card * MeibuFilter::viewAs(const Card *originalCard) const
{
    Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
    slash->setSkillName("_" + n);/*
    WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
    card->takeOver(slash);*/
    return slash;
}

XiemuCard::XiemuCard()
{
    setSkillName("xiemu");
    target_fixed = true;
}

void XiemuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QString kingdom = room->askForKingdom(source, "xiemu");
    room->setPlayerMark(source, "@xiemu_" + kingdom, 1);
}

class XiemuViewAsSkill : public ViewAsSkillV2
{
public:
    XiemuViewAsSkill() : ViewAsSkillV2("xiemu", 1) { setPhaseName("Play"); m_baseAmount = 2; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "he");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ViewAsSkillV2::canSelectCard(request, card) && matchesFilter(request, card, "Slash")
            && !request.initiator->isJilei(card);
    }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
    QString historyKey(const ActiveSkillRequest &) const override { return "XiemuCard"; }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        ctx.choice = room->askForKingdom(ctx.invoker, objectName());
        return !ctx.choice.isEmpty();
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.choice.isEmpty()) ctx.choice = ctx.invoker->getRoom()->askForKingdom(ctx.invoker, objectName());
        QVariantMap receipts = ctx.invoker->property("xiemu_receipts").toMap();
        receipts[QString::number(ctx.activationRef.key.instanceID)] = QVariantMap{{"kingdom", ctx.choice},
            {"amount", getEffectiveAmount(ctx)}};
        Room *room = ctx.invoker->getRoom();
        room->setPlayerProperty(ctx.invoker, "xiemu_receipts", receipts);
        room->setPlayerMark(ctx.invoker, "@xiemu_" + ctx.choice, 1);
        return ContinueEffects;
    }
};

class Xiemu : public TriggerSkillV2
{
public:
    Xiemu() : TriggerSkillV2("xiemu") {
        events << TargetConfirmed << EventPhaseStart;
        view_as_skill = new XiemuViewAsSkill;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventPhaseStart && player && player->getPhase() == Player::RoundStart) {
            room->setPlayerProperty(player, "xiemu_receipts", QVariantMap());
            for (const QString &kingdom : Sanguosha->getKingdoms())
                if (player->getMark("@xiemu_" + kingdom) > 0) room->setPlayerMark(player, "@xiemu_" + kingdom, 0);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != TargetConfirmed || !player || !player->isAlive()) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.from || use.from == player || !use.card || use.card->getTypeId() == Card::TypeSkill
            || !use.card->isBlack() || !use.to.contains(player)) return true;
        const QVariantMap receipts = player->property("xiemu_receipts").toMap();
        for (auto it = receipts.cbegin(); it != receipts.cend(); ++it) {
            const QVariantMap receipt = it.value().toMap();
            if (receipt.value("kingdom").toString() != use.from->getKingdom()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.instanceID = it.key().toInt();
            ctx.sourceRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), ctx.instanceID));
            ctx.extra_data = it.key();
            ctx.amount = receipt.value("amount").toInt();
            ctx.original_data = &data;
            ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        return ctx.owner && ctx.owner->isAlive() && ctx.owner->property("xiemu_receipts").toMap().contains(ctx.extra_data.toString());
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->notifySkillInvoked(ctx.owner, objectName());
        ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class Naman : public TriggerSkillV2
{
public:
    Naman() : TriggerSkillV2("naman")
    {
        events << CardResponded;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        CardResponseStruct response = data.value<CardResponseStruct>();
        if (!player || response.m_isUse || !response.m_card || !response.m_card->isKindOf("Slash")
            || room->getCardPlace(response.m_card->getEffectiveId()) != Player::DiscardPile) return result;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->hasSkill(objectName()))
                result[p] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(p, objectName(), *ctx.original_data);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        CardResponseStruct response = ctx.original_data->value<CardResponseStruct>();
        room->broadcastSkillInvoke(objectName());
        room->obtainCard(p, response.m_card);
        return false;
    }
};

class FuluVS : public ViewAsSkillV2
{
public:
    FuluVS() : ViewAsSkillV2("fulu", 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return Slash::IsAvailable(request.initiator);
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && (request.pattern.contains("slash") || request.pattern.contains("Slash"));
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!ViewAsSkillV2::canSelectCard(request, card) || !matchesFilter(request, card, "%slash")) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return true;
        ThunderSlash slash(card->getSuit(), card->getNumber());
        slash.addSubcard(card);
        slash.setSkillName(objectName());
        return slash.isAvailable(request.initiator);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && replaySelection(this, request);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        ThunderSlash *acard = new ThunderSlash(originalCard->getSuit(), originalCard->getNumber());
        acard->addSubcard(originalCard->getId());
        acard->setSkillName(objectName());
        return acard;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "ThunderSlash";
    }
};

class Fulu : public TriggerSkillV2
{
public:
    Fulu() : TriggerSkillV2("fulu")
    {
        events << ChangeSlash;
        view_as_skill = new FuluVS;
    }

    static void convert(ThunderSlash *thunder_slash, const Card *card)
    {
        if (!card->isVirtualCard() || card->subcardsLength() > 0)
            thunder_slash->addSubcard(card);
        thunder_slash->setSkillName("fulu");
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.card->objectName() != "slash") return TriggerList();
        bool has_changed = false;
        QString skill_name = use.card->getSkillName();
        if (!skill_name.isEmpty()) {
            const Skill *skill = Sanguosha->getSkill(skill_name);
            if (skill && !skill->inherits("FilterSkill") && !skill->objectName().contains("guhuo"))
                has_changed = true;
        }
        if (has_changed && !(use.card->isVirtualCard() && use.card->subcardsLength() == 0)) return TriggerList();
        ThunderSlash thunder_slash(use.card->getSuit(), use.card->getNumber());
        convert(&thunder_slash, use.card);
        bool can_use = true;
        foreach (ServerPlayer *p, use.to) {
            if (!player->canSlash(p, &thunder_slash, false)) {
                can_use = false;
                break;
            }
        }
        return can_use ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(player, "fulu", *ctx.original_data, false);
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        //room->broadcastSkillInvoke("fulu");
        ThunderSlash *thunder_slash = new ThunderSlash(use.card->getSuit(), use.card->getNumber());
        convert(thunder_slash, use.card);
        use.changeCard(thunder_slash);
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class Zhuji : public TriggerSkillV2
{
public:
    Zhuji() : TriggerSkillV2("zhuji")
    {
        events << DamageCaused << FinishJudge;
    }

    // The judge colour is recorded for every Zhuji judge, whoever is judging.
    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent != FinishJudge || !player) return true;
        JudgeStruct *judge = data.value<JudgeStruct *>();
        if (judge->reason == objectName()) {
            judge->pattern = (judge->card->isRed() ? "red" : "black");
            if (room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge && judge->card->isRed())
                player->obtainCard(judge->card);
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (triggerEvent != DamageCaused) return result;
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.nature != DamageStruct::Thunder || !damage.from) return result;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isAlive() && p->hasSkill(objectName()))
                result[p] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        room->broadcastSkillInvoke(objectName());
        JudgeStruct judge;
        judge.good = true;
        judge.play_animation = false;
        judge.reason = objectName();
        judge.pattern = ".";
        judge.who = damage.from;

        room->judge(judge);
        if (judge.pattern == "black") {
            LogMessage log;
            log.type = "#ZhujiBuff";
            log.from = ctx.owner;
            log.to << damage.to;
            log.arg = QString::number(damage.damage);
            damage.damage += getEffectiveAmount(ctx);
            log.arg2 = QString::number(damage.damage);
            room->sendLog(log);

            *ctx.original_data = QVariant::fromValue(damage);
        }
        return false;
    }
};

class Canshi : public TriggerSkillV2
{
public:
    Canshi() : TriggerSkillV2("canshi")
    {
        events << EventPhaseStart << CardUsed << EventPhaseChanging;
        global = true;
    }

    static int woundedCount(Room *room)
    {
        int n = 0;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isWounded())
                ++n;
        }
        return n;
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::NotActive)
            room->setPlayerProperty(player, "canshi_receipts", QVariantMap());
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != CardUsed) return false;
        const Card *card = data.value<CardUseStruct>().card;
        if (!player || !player->isAlive() || !card || (!card->isKindOf("BasicCard") && !card->isKindOf("TrickCard"))) return true;
        const QVariantMap receipts = player->property("canshi_receipts").toMap();
        for (auto it = receipts.cbegin(); it != receipts.cend(); ++it) {
            const QVariantMap receipt = it.value().toMap();
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(),
                SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.owner = room->findPlayerByObjectName(ctx.sourceRef.ownerObjectName, true);
            if (!ctx.owner) continue;
            ctx.invoker = player;
            ctx.initiator = player;
            ctx.targets = {player};
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.amount = receipt.value("amount", 1).toInt();
            ctx.extra_data = it.key();
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.current_event != CardUsed) return TriggerSkillV2::isSourceAvailable(room, ctx);
        return ctx.invoker && ctx.invoker->isAlive()
            && ctx.invoker->property("canshi_receipts").toMap().contains(ctx.extra_data.toString());
    }
    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Draw && woundedCount(room) > 0)
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        return event == CardUsed || ctx.owner->askForSkillInvoke(this);
    }

    // Returning true replaces the ordinary draw phase.
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == CardUsed) return false;
        room->broadcastSkillInvoke(objectName());
        // Accepted drawbacks retain their source and amount through skill removal.
        QVariantMap receipts = player->property("canshi_receipts").toMap();
        const QString key = ctx.sourceRef.ownerObjectName + ":" + QString::number(ctx.sourceRef.key.instanceID);
        receipts[key] = QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}};
        room->setPlayerProperty(player, "canshi_receipts", receipts);
        player->setFlags(objectName());
        player->drawCards(woundedCount(room) * getEffectiveAmount(ctx), objectName());
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event != CardUsed || !target->canDiscard(target, "he")) return false;
        room->sendCompulsoryTriggerLog(target, objectName());
        const int count = qMax(0, getEffectiveAmount(ctx));
        if (count > 0) room->askForDiscard(target, objectName(), count, count, false, true, "@canshi-discard");
        return false;
    }};

class Chouhai : public TriggerSkillV2
{
public:
    Chouhai() : TriggerSkillV2("chouhai")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (player && player->isAlive() && player->hasSkill(objectName()) && player->isKongcheng())
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName(), true);
        room->broadcastSkillInvoke(objectName());

        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        damage.damage += getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

class Guiming : public TriggerSkillV2 // play audio effect only. This skill is coupled in Player::isWounded().
{
public:
    Guiming() : TriggerSkillV2("guiming$")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    bool usesEventPriority() const override
    {
        return true;
    }

    int getPriority(TriggerEvent) const override
    {
        return 6;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasLordSkill(this) || player->getPhase() != Player::RoundStart)
            return true;
        foreach (const ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getKingdom() == "wu" && p->isWounded() && p->getHp() == p->getMaxHp()) {
                if (player->hasSkill("weidi"))
                    room->broadcastSkillInvoke("weidi");
                else
                    room->broadcastSkillInvoke(objectName());
                return true;
            }
        }

        return true;
    }
};

class Conqueror : public TriggerSkillV2
{
public:
    Conqueror() : TriggerSkillV2("conqueror")
    {
        events << TargetSpecified;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (player && player->isAlive() && player->hasSkill(objectName()) && use.card != nullptr
            && use.card->isKindOf("Slash") && !use.to.isEmpty())
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    // Each target has its own optional prompt, in use order.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        int n = 0;
        foreach (ServerPlayer *target, use.to) {
            if (player->askForSkillInvoke(this, QVariant::fromValue(target))) {
                QString choice = room->askForChoice(player, objectName(), "BasicCard+EquipCard+TrickCard", QVariant::fromValue(target));

                room->broadcastSkillInvoke(objectName(), 1);

                const Card *c = room->askForCard(target, choice, QString("@conqueror-exchange:%1::%2").arg(player->objectName()).arg(choice), choice, Card::MethodNone);
                if (c != nullptr) {
                    room->broadcastSkillInvoke(objectName(), 2);
                    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), objectName(), "");
                    room->obtainCard(player, c, reason);
                    use.nullified_list << target->objectName();
                    *ctx.original_data = QVariant::fromValue(use);
                } else {
                    room->broadcastSkillInvoke(objectName(), 3);
                    QVariantList jink_list = player->getTag("Jink_" + use.card->toString()).toList();
                    jink_list[n] = QVariant(0);
                    player->setTag("Jink_" + use.card->toString(), QVariant::fromValue(jink_list));
                    LogMessage log;
                    log.type = "#NoJink";
                    log.from = target;
                    room->sendLog(log);
                }
            }
            ++n;
        }
        return false;
    }
};

class Fentian : public TriggerSkillV2
{
public:
    Fentian() : TriggerSkillV2("fentian")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    static QList<ServerPlayer *> targets(Room *room, ServerPlayer *hanba)
    {
        QList<ServerPlayer*> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(hanba)) {
            if (hanba->inMyAttackRange(p) && !p->isNude())
                targets << p;
        }
        return targets;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *hanba, QVariant &) const override
    {
        if (hanba && hanba->isAlive() && hanba->hasSkill(objectName()) && hanba->getPhase() == Player::Finish
            && hanba->getHandcardNum() < hanba->getHp() && !targets(room, hanba).isEmpty())
            return TriggerList{{hanba, QStringList{objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *hanba, SkillContext &) const override
    {
        QList<ServerPlayer*> candidates = targets(room, hanba);
        if (candidates.isEmpty())
            return false;

        room->broadcastSkillInvoke(objectName());
        ServerPlayer *target = room->askForPlayerChosen(hanba, candidates, objectName(), "@fentian-choose", false, true);
        int id = room->askForCardChosen(hanba, target, "he", objectName());
        hanba->addToPile("burn", id);
        return false;
    }
};

class FentianRange : public AttackRangeSkillV2
{
public:
    FentianRange() : AttackRangeSkillV2("#fentian")
    {
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const int burn = ctx.holder ? ctx.holder->getPile("burn").length() : 0;
        return burn > 0 ? CorrectSkillResult::useAmount(burn * ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

class Zhiri : public TriggerSkillV2
{
public:
    Zhiri() : TriggerSkillV2("zhiri")
    {
        events << EventPhaseStart << EventSkillInvoking;
        global = true;
        frequency = Wake;
        waked_skills = "xintan";
    }

    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *, QVariant &, QList<SkillContext> &) const override
    { return event == EventSkillInvoking; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return false;
        const SkillContext accepted = data.value<SkillContext>();
        // Normal payment commits the quota; bypassed payment consumes the same exact source.
        if (accepted.bypass_cost && accepted.activationRef.isValid()
            && accepted.activationRef.key.skillName == objectName() && !accepted.use_card) addUsage(accepted);
        return true;
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start) return TriggerList();
        if (player->getPile("burn").length() < 3 && !hasWakeGrant(player, objectName())) return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || (ctx.owner->getPile("burn").length() < 3 && !hasWakeGrant(ctx.owner, objectName()))) return false;
        addUsage(ctx);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *hanba, SkillContext &ctx) const override
    {
        if (hanba->getPile("burn").length() >= 3) {
            LogMessage log;
            log.from = hanba;
            log.type = "#ZhiriWake";
            log.arg = QString::number(hanba->getPile("burn").length());
            log.arg2 = objectName();
            room->sendLog(log);
        }
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(hanba,objectName());
        room->doSuperLightbox(hanba, "zhiri");

        room->setPlayerMark(hanba, objectName(), 1);
        if (room->changeMaxHpForAwakenSkill(hanba, -getEffectiveAmount(ctx), objectName()))
            room->acquireSkill(hanba, "xintan");
        return false;
    }
};

XintanCard::XintanCard()
{
    setSkillName("xintan");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool XintanCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}

void XintanCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *hanba = effect.from;
    Room *room = hanba->getRoom();

    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, hanba->objectName(), objectName(), "");
    room->moveCardTo(this, nullptr, Player::DiscardPile, reason, true);

    room->loseHp(HpLostStruct(effect.to, 1, "xintan", hanba));
}

class Xintan : public ViewAsSkillV2
{
public:
    Xintan() : ViewAsSkillV2("xintan", 2) { expand_pile = "burn"; setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->getPile("burn").size() >= 2;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return card && request.selectedCardIds.size() < 2 && !request.selectedCardIds.contains(card->getEffectiveId())
            && request.initiator->getPile("burn").contains(card->getEffectiveId());
    }
    bool willThrowSelectedCards() const override { return false; }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *target) const override
    {
        return selected.isEmpty() && target && target->isAlive();
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "XintanCard"; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request)) return false;
        for (int id : request.selectedCardIds) if (!ctx.invoker->getPile("burn").contains(id)) return false;
        DummyCard cards(request.selectedCardIds);
        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, ctx.invoker->objectName(), objectName(), "");
        room->moveCardTo(&cards, nullptr, Player::DiscardPile, reason, true);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        target->getRoom()->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        return ContinueEffects;
    }
};

FanghunCard::FanghunCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
    m_skillName = "fanghun";
}

bool FanghunCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
    CardUseStruct::CardUseReason reason = Sanguosha->currentRoomState()->getCurrentCardUseReason();

    if (reason == CardUseStruct::CARD_USE_REASON_PLAY || pattern.contains("slash") || pattern.contains("Slash")) {
        Slash *slash = new Slash(NoSuit, 0);
        slash->setSkillName("_longdan");
        slash->addSubcards(getSubcards());
        slash->deleteLater();
        return slash->targetFilter(targets, to_select, Self);
    }
    return false;
}

bool FanghunCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    CardUseStruct::CardUseReason reason = Sanguosha->currentRoomState()->getCurrentCardUseReason();
    QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();

    if (reason == CardUseStruct::CARD_USE_REASON_PLAY || pattern.contains("slash") || pattern.contains("Slash")) {
        Slash *slash = new Slash(NoSuit, 0);
        slash->setSkillName("_longdan");
        slash->addSubcards(getSubcards());
        slash->deleteLater();
        return slash->targetsFeasible(targets, Self);
    }
    return targets.length() == 0;
}

const Card *FanghunCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer *player = card_use.from;
    Room *room = player->getRoom();

    CardUseStruct::CardUseReason reason = Sanguosha->currentRoomState()->getCurrentCardUseReason();
    QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();

    if (reason == CardUseStruct::CARD_USE_REASON_PLAY || pattern.contains("slash") || pattern.contains("Slash")) {
        Slash *slash = new Slash(NoSuit, 0);
        slash->addSubcards(getSubcards());
        slash->setSkillName("_longdan");
        slash->setFlags("JINGYIN");
		slash->deleteLater();
        for (int i = card_use.to.length() - 1; i >=0 ; i--) {
            if (!player->canSlash(card_use.to.at(i)))
                card_use.to.removeOne(card_use.to.at(i));
        }
        if (card_use.to.isEmpty()||player->isLocked(slash)) return nullptr;
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = player;
        log.arg = m_skillName;
        room->sendLog(log);
        room->broadcastSkillInvoke(m_skillName);
        room->notifySkillInvoked(player, m_skillName);
        player->loseMark("&meiying");
        room->setPlayerMark(player, m_skillName + "_id", getSubcards().first() + 1);
        //room->useCard(CardUseStruct(slash, player, card_use.to), player->getPhase() == Player::Play);
       // player->drawCards(1, objectName());
        return slash;
    } else {
        Jink *jink = new Jink(NoSuit, 0);
        jink->addSubcards(getSubcards());
        jink->setSkillName("_longdan");
        jink->setFlags("JINGYIN");
		jink->deleteLater();
        if (player->isLocked(jink)) return nullptr;
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = player;
        log.arg = m_skillName;
        room->sendLog(log);
        room->broadcastSkillInvoke(m_skillName);
        room->notifySkillInvoked(player, m_skillName);
        player->loseMark("&meiying");
        room->setPlayerMark(player, m_skillName + "_id", getSubcards().first() + 1);
        return jink;
    }
    return nullptr;
}

const Card *FanghunCard::validateInResponse(ServerPlayer *player) const
{
    Room *room = player->getRoom();
    QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();

    if (pattern == "jink") {
        Jink *jink = new Jink(NoSuit, 0);
        jink->addSubcards(getSubcards());
        jink->setSkillName("_longdan");
        jink->setFlags("JINGYIN");
		jink->deleteLater();
        if (player->isLocked(jink)) return nullptr;
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = player;
        log.arg = m_skillName;
        room->sendLog(log);
        room->broadcastSkillInvoke(m_skillName);
        room->notifySkillInvoked(player, m_skillName);
        player->loseMark("&meiying");
        room->setPlayerMark(player, m_skillName + "_id", getSubcards().first() + 1);
        return jink;
    } else {
        Slash *slash = new Slash(NoSuit, 0);
        slash->addSubcards(getSubcards());
        slash->setSkillName("_longdan");
        slash->setFlags("JINGYIN");
		slash->deleteLater();
        if (player->isLocked(slash)) return nullptr;
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = player;
        log.arg = m_skillName;
        room->sendLog(log);
        room->broadcastSkillInvoke(m_skillName);
        room->notifySkillInvoked(player, m_skillName);
        player->loseMark("&meiying");
        room->setPlayerMark(player, m_skillName + "_id", getSubcards().first() + 1);
        return slash;
    }
    return nullptr;
}

// Shared by every Fanghun version; the SkillCards above only let legacy AI strings parse.
class FanghunViewAsSkill : public ViewAsSkillV2
{
public:
    FanghunViewAsSkill(const QString &fanghun_skill) : ViewAsSkillV2(fanghun_skill, 1)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.initiator->getMark("&meiying") <= 0) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            ? Slash::IsAvailable(request.initiator)
            : !materialClass(request).isEmpty();
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!ViewAsSkillV2::canSelectCard(request, card) || !matchesFilter(request, card, ".")) return false;
        const QString material = materialClass(request);
        if (material.isEmpty() || !card->isKindOf(material.toLatin1().constData())) return false;
        if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return true;
        Slash slash(Card::NoSuit, 0);
        slash.addSubcard(card);
        slash.setSkillName("_longdan");
        return slash.isAvailable(request.initiator);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && replaySelection(this, request);
    }

    QString historyKey(const ActiveSkillRequest &request) const override
    {
        return materialClass(request) == "Slash" ? "Jink" : "Slash";
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *originalCard = Sanguosha->getCard(request.selectedCardIds.first());
        // The converted card is suitless, as the legacy FanghunCard validation produced.
        Card *card = nullptr;
        if (originalCard->isKindOf("Slash"))
            card = new Jink(Card::NoSuit, 0);
        else
            card = new Slash(Card::NoSuit, 0);
        card->addSubcard(originalCard);
        card->setSkillName("_longdan");
        card->setFlags("JINGYIN");
        return card;
    }

    // The mark and the draw bookkeeping belong to an accepted conversion.
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        ServerPlayer *player = ctx.initiator;
        if (!player || player->getMark("&meiying") <= 0 || !cardSelectionFeasible(request)) return false;
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = player;
        log.arg = objectName();
        room->sendLog(log);
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(player, objectName());
        player->loseMark("&meiying");

        return true;
    }

    EffectFlow effect(SkillContext &ctx) const override
    {
        // Acceptance owns this continuation, including when its material cost is bypassed.
        ServerPlayer *player = ctx.initiator;
        ctx.use_card->setTag("fanghun_receipt", QVariantMap{{"family", objectName()}, {"actor", player->objectName()},
            {"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
            {"activation", ctx.activationRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}});
        return ContinueEffects;
    }

private:
    static QString materialClass(const ActiveSkillRequest &request)
    {
        switch (request.reason) {
        case CardUseStruct::CARD_USE_REASON_PLAY:
            return "Jink";
        case CardUseStruct::CARD_USE_REASON_RESPONSE:
        case CardUseStruct::CARD_USE_REASON_RESPONSE_USE:
            if (request.pattern.contains("slash") || request.pattern.contains("Slash"))
                return "Jink";
            if (request.pattern == "jink")
                return "Slash";
            return QString();
        default:
            return QString();
        }
    }
};

class FanghunDraw : public TriggerSkillV2
{
public:
    FanghunDraw(const QString &family) : TriggerSkillV2("#" + family), family(family)
    { events << EventSkillInvoking << CardResponded << CardFinished << MarkChanged; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            SkillContext accepted = data.value<SkillContext>();
            if (accepted.bypass_cost && accepted.skill_name == objectName()) {
                // Retiring an accepted receipt is bookkeeping, never a bypassable resource cost.
                if (!pay(accepted.current_event, room, accepted.invoker, accepted)) accepted.is_canceled = true;
                data.setValue(accepted);
            }
            return true;
        }
        if (event == MarkChanged && player && family == "fanghun") {
            const MarkStruct mark = data.value<MarkStruct>();
            if (mark.name == "&meiying" && mark.gain < 0) player->addMark("meiying", -mark.gain);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event == EventSkillInvoking) return true;
        if (event == MarkChanged || (event == CardResponded && data.value<CardResponseStruct>().m_isUse)) return true;
        const Card *card = event == CardResponded ? data.value<CardResponseStruct>().m_card : data.value<CardUseStruct>().card;
        if (!card) return true;
        const QVariantMap receipt = card->getTag("fanghun_receipt").toMap();
        if (receipt.value("family").toString() != family) return true;
        SkillContext ctx; ctx.skill_name = objectName(); ctx.use_card = card;
        ctx.invoker = ctx.initiator = room->findPlayerByObjectName(receipt.value("actor").toString());
        ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
        ctx.owner = room->findPlayerByObjectName(ctx.sourceRef.ownerObjectName, true);
        if (!ctx.invoker || !ctx.owner) return true;
        ctx.extra_data = receipt;
        ctx.targets = {ctx.invoker}; ctx.amount = receipt.value("amount").toInt(); ctx.current_event = event; ctx.original_data = &data;
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.use_card && ctx.sourceRef.isValid() && ctx.extra_data.toMap().value("family").toString() == family; }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.use_card || ctx.use_card->getTag("fanghun_receipt").toMap().value("family").toString() != family) return false;
        ctx.use_card->removeTag("fanghun_receipt");
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { target->drawCards(getEffectiveAmount(ctx), family); return false; }
private:
    QString family;
};

class Fanghun : public TriggerSkillV2
{
public:
    Fanghun() : TriggerSkillV2("fanghun")
    {
        events << Damage << Damaged;
        view_as_skill = new FanghunViewAsSkill("fanghun");
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || !damage.card || !damage.card->isKindOf("Slash")) return TriggerList();
        if ((triggerEvent == Damage && damage.by_user) || triggerEvent == Damaged)
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        player->gainMark("&meiying", getEffectiveAmount(ctx));
        return false;
     }
};

class Fuhan : public TriggerSkillV2
{
public:
    Fuhan() : TriggerSkillV2("fuhan")
    {
        events << EventPhaseStart << EventSkillInvoking;
        global = true;
        frequency = Limited;
        limit_mark = "@fuhanMark";
    }

    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *, QVariant &, QList<SkillContext> &) const override
    { return event == EventSkillInvoking; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return false;
        const SkillContext accepted = data.value<SkillContext>();
        // Normal payment commits the quota; bypassed payment consumes the same exact source.
        if (accepted.bypass_cost && accepted.activationRef.isValid()
            && accepted.activationRef.key.skillName == objectName() && !accepted.use_card) addUsage(accepted);
        return true;
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::RoundStart
            && player->getMark("&meiying") > 0)
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
    {
        QString num = QString::number(player->getMark("meiying") + player->getMark("&meiying"));
        return player->askForSkillInvoke("fuhan", QString("fuhan_invoke:%1").arg(num));
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        // Public tokens project usage; the exact owning instance controls its quota.
        if (player->getMark("@fuhanMark") > 0) room->removePlayerMark(player, "@fuhanMark");
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox(player, "fuhan");
        int meiying = player->getMark("&meiying");
        player->loseAllMarks("&meiying");
        player->drawCards(meiying, objectName());
        QStringList shus = Sanguosha->getLimitedGeneralNames("shu");
        QStringList five_shus;
        for (int i = 1; i < 6; i++) {
            if (shus.isEmpty()) break;
            QString name = shus.at((qsanRandomBounded(shus.length())));
            five_shus << name;
            shus.removeOne(name);
        }
        if (five_shus.isEmpty()) return false;
        QString shu_general = room->askForGeneral(player, five_shus);
        room->changeHero(player, shu_general, false, false, (player->getGeneralName() != "zhaoxiang" && player->getGeneral2Name() == "zhaoxiang"));
        int n = player->getMark("meiying");
        room->setPlayerProperty(player, "maxhp", n);
        int hp = player->getHp();
        bool recover = true;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getHp() < hp) {
                recover = false;
                break;
            }
        }
        if (recover == false) return false;
        room->recover(player, RecoverStruct("fuhan", player));
        return false;
    }
};

OLFanghunCard::OLFanghunCard() : FanghunCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
    m_skillName = "olfanghun";
}

class OLFanghun : public TriggerSkillV2
{
public:
    OLFanghun() : TriggerSkillV2("olfanghun")
    {
        events << Damage << Damaged;
        view_as_skill = new FanghunViewAsSkill("olfanghun");
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName())
            || !damage.card || !damage.card->isKindOf("Slash")) return TriggerList();
        if ((triggerEvent == Damage && damage.by_user) || triggerEvent == Damaged)
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        player->gainMark("&meiying", ctx.original_data->value<DamageStruct>().damage);
        return false;
     }
};

class OLFuhan : public TriggerSkillV2
{
public:
    OLFuhan() : TriggerSkillV2("olfuhan")
    {
        events << EventPhaseStart << EventSkillInvoking;
        global = true;
        frequency = Limited;
        limit_mark = "@olfuhanMark";
    }

    static int bounded(int n)
    {
        return qMax(2, qMin(8, n));
    }

    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *, QVariant &, QList<SkillContext> &) const override
    { return event == EventSkillInvoking; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return false;
        const SkillContext accepted = data.value<SkillContext>();
        // Normal payment commits the quota; bypassed payment consumes the same exact source.
        if (accepted.bypass_cost && accepted.activationRef.isValid()
            && accepted.activationRef.key.skillName == objectName() && !accepted.use_card) addUsage(accepted);
        return true;
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::RoundStart
            && player->getMark("&meiying") > 0)
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
    {
        QString num = QString::number(bounded(player->getMark("meiying") + player->getMark("&meiying")));
        return player->askForSkillInvoke("olfuhan", QString("olfuhan_invoke:%1").arg(num));
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        // Public tokens project usage; the exact owning instance controls its quota.
        if (player->getMark("@olfuhanMark") > 0) room->removePlayerMark(player, "@olfuhanMark");
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox(player, "olfuhan");
        player->loseAllMarks("&meiying");
        QStringList shus = Sanguosha->getLimitedGeneralNames("shu");
        QStringList five_shus;
        for (int i = 1; i < 6; i++) {
            if (shus.isEmpty()) break;
            QString name = shus.at((qsanRandomBounded(shus.length())));
            five_shus << name;
            shus.removeOne(name);
        }
        if (five_shus.isEmpty()) return false;
        QString shu_general = room->askForGeneral(player, five_shus);
        room->changeHero(player, shu_general, false, false, (player->getGeneralName() != "ol_zhaoxiang" && player->getGeneral2Name() == "ol_zhaoxiang"));
        room->setPlayerProperty(player, "maxhp", bounded(player->getMark("meiying")));
        room->recover(player, RecoverStruct("olfuhan", player));
        return false;
    }
};

MobileFanghunCard::MobileFanghunCard() : FanghunCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
    m_skillName = "mobilefanghun";
}

class MobileFanghun : public TriggerSkillV2
{
public:
    MobileFanghun() : TriggerSkillV2("mobilefanghun")
    {
        events << Damage;
        view_as_skill = new FanghunViewAsSkill("mobilefanghun");
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (player && player->isAlive() && player->hasSkill(objectName())
            && damage.card && damage.card->isKindOf("Slash") && damage.by_user)
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        player->gainMark("&meiying", getEffectiveAmount(ctx));
        return false;
     }
};

TenyearFanghunCard::TenyearFanghunCard() : FanghunCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
    m_skillName = "tenyearfanghun";
}

class TenyearFanghun : public TriggerSkillV2
{
public:
    TenyearFanghun() : TriggerSkillV2("tenyearfanghun")
    {
        events << TargetSpecified << TargetConfirmed;
        view_as_skill = new FanghunViewAsSkill("tenyearfanghun");
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card || !use.card->isKindOf("Slash"))
            return TriggerList();
        if (triggerEvent == TargetSpecified || (triggerEvent == TargetConfirmed && use.to.contains(player)))
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        player->gainMark("&meiying", getEffectiveAmount(ctx));
        return false;
     }
};

class Wuniang : public TriggerSkillV2
{
public:
    Wuniang() : TriggerSkillV2("wuniang")
    {
        events << CardUsed << CardResponded;
    }

    static QList<ServerPlayer *> targets(Room *room, ServerPlayer *player)
    {
        QList<ServerPlayer *> players;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!p->isNude())
                players << p;
        }
        return players;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
        const Card *card = triggerEvent == CardUsed ? data.value<CardUseStruct>().card
                                                    : data.value<CardResponseStruct>().m_card;
        if (!card || !card->isKindOf("Slash") || targets(room, player).isEmpty()) return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(player, targets(room, player), objectName(), "@wuniang-invoke", true, true);
        if (!target) return false;
        ctx.targets = QList<ServerPlayer *>{target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "guansuo") { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
        room->broadcastSkillInvoke(objectName());
        if (target->isNude()) return false;
        int id = room->askForCardChosen(player, target, "he", objectName());
        room->obtainCard(player, id, false);
        if (target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
        ctx.choice = "guansuo";
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->getGeneralName().contains("guansuo") || p->getGeneral2Name().contains("guansuo")) skillEffect(ctx.current_event, ctx.invoker->getRoom(), ctx.invoker, ctx, p);
        }
        ctx.choice.clear();
        return false;
    }
};

class Xushen : public TriggerSkillV2
{
public:
    Xushen() : TriggerSkillV2("xushen")
    {
        events << QuitDying << EventSkillInvoking;
        global = true;
        frequency = Limited;
        limit_mark = "@xushenMark";
    }

    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *, QVariant &, QList<SkillContext> &) const override
    { return event == EventSkillInvoking; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return false;
        const SkillContext accepted = data.value<SkillContext>();
        // Normal payment commits the quota; bypassed payment consumes the same exact source.
        if (accepted.bypass_cost && accepted.activationRef.isValid()
            && accepted.activationRef.key.skillName == objectName() && !accepted.use_card) addUsage(accepted);
        return true;
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()))
            return TriggerList();
        ServerPlayer *saver = player->getSaver();
        if (!saver || !saver->isMale() || saver == player) return TriggerList();
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getGeneralName().contains("guansuo") || p->getGeneral2Name().contains("guansuo"))
                return TriggerList();
        }
        return TriggerList{{player, QStringList{objectName()}}};
    }

    // The saver, not the skill owner, accepts the transformation.
    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
    {
        ServerPlayer *saver = player->getSaver();
        return saver && saver->askForSkillInvoke(objectName(), player, false);
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        // Public tokens project usage; the exact owning instance controls its quota.
        if (player->getMark("@xushenMark") > 0) room->removePlayerMark(player, "@xushenMark");
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        ServerPlayer *saver = player->getSaver();
        if (!saver) return false;
        LogMessage log;
        log.type = "#InvokeOthersSkill";
        log.from = saver;
        log.to << player;
        log.arg = "xushen";
        room->sendLog(log);
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(player, objectName());
        room->doSuperLightbox(player, "xushen");
        room->changeHero(saver, "guansuo", false, false);
        room->recover(player, RecoverStruct("xushen", player));
        if (!player->hasSkill("zhennan", true))
            room->handleAcquireDetachSkills(player, "zhennan");
        return false;
    }
};

class Zhennan : public TriggerSkillV2
{
public:
    Zhennan() : TriggerSkillV2("zhennan")
    {
        events << TargetConfirmed;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = data.value<CardUseStruct>().card;
        if (player && player->isAlive() && player->hasSkill(objectName()) && card && card->isKindOf("SavageAssault"))
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@zhennan-invoke", true, true);
        if (!target) return false;
        ctx.targets = QList<ServerPlayer *>{target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        int n = qsanRandomBounded(3) + 1;
        room->damage(DamageStruct(objectName(), player, target, n));
        return false;
    }
};

ZhanyiViewAsBasicCard::ZhanyiViewAsBasicCard()
{
    m_skillName = "_zhanyi";
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool ZhanyiViewAsBasicCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card) card->deleteLater();
        return card && card->targetFilter(targets, to_select, Self);
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return false;
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *card = Self ? Self->getTag("zhanyi").value<const Card *>() : nullptr;
    if (!card) {
        Card *declared = Sanguosha->cloneCard(user_string.split("+").first());
        if (declared) declared->deleteLater();
        card = declared;
    }
    return card && card->targetFilter(targets, to_select, Self);
}

bool ZhanyiViewAsBasicCard::targetFixed() const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card) card->deleteLater();
        return card && card->targetFixed();
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return true;
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *card = Self ? Self->getTag("zhanyi").value<const Card *>() : nullptr;
    if (!card) {
        Card *declared = Sanguosha->cloneCard(user_string.split("+").first());
        if (declared) declared->deleteLater();
        card = declared;
    }
    return card && card->targetFixed();
}

bool ZhanyiViewAsBasicCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        const Card *card = nullptr;
        if (!user_string.isEmpty())
            card = Sanguosha->cloneCard(user_string.split("+").first());
        return card && card->targetsFeasible(targets, Self);
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return true;
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *card = Self ? Self->getTag("zhanyi").value<const Card *>() : nullptr;
    if (!card) {
        Card *declared = Sanguosha->cloneCard(user_string.split("+").first());
        if (declared) declared->deleteLater();
        card = declared;
    }
    return card && card->targetsFeasible(targets, Self);
}

const Card *ZhanyiViewAsBasicCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer *zhuling = card_use.from;
    Room *room = zhuling->getRoom();

    QString to_zhanyi = user_string;
    if (user_string == "slash" && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        QStringList guhuo_list;
        guhuo_list << "slash";
        if (!Sanguosha->getBanPackages().contains("maneuvering"))
            guhuo_list << "normal_slash" << "thunder_slash" << "fire_slash";
        to_zhanyi = room->askForChoice(zhuling, "zhanyi_slash", guhuo_list.join("+"));
    }

    const Card *card = Sanguosha->getCard(subcards.first());
    QString user_str;
    if (to_zhanyi == "slash") {
        if (card->isKindOf("Slash"))
            user_str = card->objectName();
        else
            user_str = "slash";
    } else if (to_zhanyi == "normal_slash")
        user_str = "slash";
    else
        user_str = to_zhanyi;
    Card *use_card = Sanguosha->cloneCard(user_str, card->getSuit(), card->getNumber());
    use_card->setSkillName("_zhanyi");
    use_card->addSubcard(subcards.first());
    use_card->deleteLater();
    return use_card;
}

const Card *ZhanyiViewAsBasicCard::validateInResponse(ServerPlayer *zhuling) const
{
    Room *room = zhuling->getRoom();

    QString to_zhanyi;
    if (user_string == "peach+analeptic") {
        QStringList guhuo_list;
        guhuo_list << "peach";
        if (!Sanguosha->getBanPackages().contains("maneuvering"))
            guhuo_list << "analeptic";
        to_zhanyi = room->askForChoice(zhuling, "zhanyi_saveself", guhuo_list.join("+"));
    } else if (user_string == "slash") {
        QStringList guhuo_list;
        guhuo_list << "slash";
        if (!Sanguosha->getBanPackages().contains("maneuvering"))
            guhuo_list << "normal_slash" << "thunder_slash" << "fire_slash";
        to_zhanyi = room->askForChoice(zhuling, "zhanyi_slash", guhuo_list.join("+"));
    } else
        to_zhanyi = user_string;

    QString user_str;
    const Card *card = Sanguosha->getCard(subcards.first());
    if (to_zhanyi == "slash") {
        if (card->isKindOf("Slash"))
            user_str = card->objectName();
        else
            user_str = "slash";
    } else if (to_zhanyi == "normal_slash")
        user_str = "slash";
    else
        user_str = to_zhanyi;
    Card *use_card = Sanguosha->cloneCard(user_str, card->getSuit(), card->getNumber());
    use_card->setSkillName("_zhanyi");
    use_card->addSubcard(subcards.first());
    use_card->deleteLater();
    return use_card;
}

ZhanyiCard::ZhanyiCard()
{
    setSkillName("zhanyi");
    target_fixed = true;
}

void ZhanyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->loseHp(HpLostStruct(source, 1, "zhanyi", source));
    if (source->isAlive()) {
        const Card *c = Sanguosha->getCard(subcards.first());
        if (c->getTypeId() == Card::TypeBasic) {
            room->setPlayerMark(source, "ViewAsSkill_zhanyiEffect", 1);
        } else if (c->getTypeId() == Card::TypeEquip)
            source->setFlags("zhanyiEquip");
        else if (c->getTypeId() == Card::TypeTrick) {
            source->drawCards(2, "zhanyi");
            room->setPlayerFlag(source, "zhanyiTrick");
        }
    }
}

class ZhanyiNoDistanceLimit : public TargetModSkillV2
{
public:
    ZhanyiNoDistanceLimit() : TargetModSkillV2("#zhanyi-trick", ".") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::DistanceLimit || !ctx.primary) return CorrectSkillResult::noEffect();
        for (const QVariant &entry : ctx.primary->property("zhanyi_receipts").toMap())
            if (entry.toMap().value("type").toInt() == Card::TypeTrick) return CorrectSkillResult::useAmount(999);
        return CorrectSkillResult::noEffect();
    }
};

class ZhanyiDiscard2 : public TriggerSkillV2
{
public:
    ZhanyiDiscard2() : TriggerSkillV2("#zhanyi-equip") { events << TargetSpecified; global = true; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !use.card || !use.card->isKindOf("Slash")) return true;
        const QVariantMap receipts = player->property("zhanyi_receipts").toMap();
        for (auto it = receipts.cbegin(); it != receipts.cend(); ++it) {
            const QVariantMap receipt = it.value().toMap();
            if (receipt.value("type").toInt() != Card::TypeEquip) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.owner = room->findPlayerByObjectName(ctx.sourceRef.ownerObjectName, true);
            if (!ctx.owner) continue;
            ctx.invoker = ctx.initiator = player;
            ctx.targets = use.to;
            ctx.amount = 2 * receipt.value("amount").toInt();
            ctx.extra_data = it.key();
            ctx.original_data = &data;
            ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->property("zhanyi_receipts").toMap().contains(ctx.extra_data.toString()); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int count = getEffectiveAmount(ctx);
        if (target->isNude()) return false;
        if (target->getCardCount() <= count) {
            DummyCard cards; cards.addSubcards(target->getCards("he")); room->throwCard(&cards, target);
        } else room->askForDiscard(target, "zhanyi_equip", count, count, false, true, "@zhanyiequip_discard");
        return false;
    }
};

class Zhanyi : public ViewAsSkillV2
{
public:
    Zhanyi() : ViewAsSkillV2("zhanyi", 1) { response_or_use = true; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    TargetMode targetMode() const override { return NoTarget; }
    static bool basicMode(const Player *player, int instanceId = 0)
    {
        if (!player) return false;
        const QVariantMap receipts = player->property("zhanyi_receipts").toMap();
        if (instanceId > 0) return receipts.value(QString::number(instanceId)).toMap().value("type").toInt() == Card::TypeBasic;
        for (const QVariant &entry : receipts)
            if (entry.toMap().value("type").toInt() == Card::TypeBasic) return true;
        return false;
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.activationRef.isValid()) return false;
        if (basicMode(ctx.invoker, ctx.activationRef.key.instanceID)) return true;
        const qint64 phase = ctx.invoker->getRoom()->historyScopes().value("phase_id").toLongLong();
        return phase > 0 && ctx.invoker->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "paid_phase").toLongLong() != phase;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return true;
        if ((request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE) || !basicMode(player, request.activationRef.key.instanceID)) return false;
        if (request.pattern == "peach" && player->getMark("Global_PreventPeach") > 0) return false;
        return !usableNames(request).isEmpty();
    }
    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo("zhanyi", true, false); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!ViewAsSkillV2::canSelectCard(request, card) || !matchesFilter(request, card, ".")) return false;
        if (basicMode(request.initiator, request.activationRef.key.instanceID)) return card->isKindOf("BasicCard");
        return !request.initiator->isJilei(card) && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->hasEquip(card));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return request.selectedCardIds.size() == 1 && replaySelection(this, request); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (basicMode(request.initiator, request.activationRef.key.instanceID)) return ViewAsSkillV2::createCard(request);
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new ActiveSkillCard;
        card->setActiveSkill(this);
        card->setSkillName(objectName());
        card->addSubcards(request.selectedCardIds);
        card->setTag("v2_effect_input", int(Sanguosha->getCard(request.selectedCardIds.first())->getTypeId()));
        return card;
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    { return basicMode(request.initiator, request.activationRef.key.instanceID) ? ViewAsSkillV2::historyKey(request) : QStringLiteral("ZhanyiCard"); }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (basicMode(request.initiator, request.activationRef.key.instanceID)) return ViewAsSkillV2::cost(room, ctx, request);
        if (!cardSelectionFeasible(request)) return false;
        ctx.extra_data = Sanguosha->getCard(request.selectedCardIds.first())->getTypeId();
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!qobject_cast<const ActiveSkillCard *>(ctx.use_card)) return ViewAsSkillV2::pay(room, ctx, request);
        if (!checkCustomUsage(ctx)) return false;
        const QVariant previous = ctx.invoker->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "paid_phase");
        ctx.invoker->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "paid_phase", room->historyScopes().value("phase_id"));
        if (!ViewAsSkillV2::pay(room, ctx, request)) {
            ctx.invoker->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "paid_phase", previous);
            return false;
        }
        room->loseHp(HpLostStruct(ctx.invoker, 1, objectName(), ctx.invoker));
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!qobject_cast<const ActiveSkillCard *>(ctx.use_card) || !ctx.invoker->isAlive()) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        const int type = (ctx.extra_data.isValid() ? ctx.extra_data : ctx.use_card->getTag("v2_effect_input")).toInt();
        QVariantMap receipts = ctx.invoker->property("zhanyi_receipts").toMap();
        receipts.insert(QString::number(ctx.activationRef.key.instanceID), QVariantMap{{"owner", ctx.sourceRef.ownerObjectName},
            {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}, {"type", type}});
        room->setPlayerProperty(ctx.invoker, "zhanyi_receipts", receipts);
        // Public flags remain UI/AI projections; continuations read the accepted receipt.
        if (type == Card::TypeBasic) {
            // The paid conversion is a true independent grant until turn end, even if its source retires.
            const int grantId = room->acquireSkillFromEffect(ctx.invoker, objectName(), ctx);
            if (grantId > 0) {
                QVariantMap current = ctx.invoker->property("zhanyi_receipts").toMap();
                const QVariant receipt = current.take(QString::number(ctx.activationRef.key.instanceID));
                current.insert(QString::number(grantId), receipt);
                room->setPlayerProperty(ctx.invoker, "zhanyi_receipts", current);
                QVariantList grants = ctx.invoker->property("zhanyi_basic_grants").toList();
                grants << grantId;
                room->setPlayerProperty(ctx.invoker, "zhanyi_basic_grants", grants);
            }
            room->setPlayerMark(ctx.invoker, "ViewAsSkill_zhanyiEffect", 1);
        }
        else if (type == Card::TypeEquip) room->setPlayerFlag(ctx.invoker, "zhanyiEquip");
        else if (type == Card::TypeTrick) { ctx.invoker->drawCards(2 * getEffectiveAmount(ctx), objectName()); room->setPlayerFlag(ctx.invoker, "zhanyiTrick"); }
        return ContinueEffects;
    }
protected:
    Card *buildCard(const ActiveSkillRequest &request, const QString &name) const override
    {
        Card *card = ViewAsSkillV2::buildCard(request, name);
        if (card) card->setSkillName("_zhanyi");
        return card;
    }
};

class ZhanyiRemove : public TriggerSkillV2
{
public:
    ZhanyiRemove() : TriggerSkillV2("#zhanyi-basic") { events << EventPhaseChanging << EventSkillInvoking; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext accepted = data.value<SkillContext>();
            if (accepted.bypass_cost && accepted.invoker && accepted.activationRef.isValid()
                && accepted.activationRef.key.skillName == "zhanyi" && qobject_cast<const ActiveSkillCard *>(accepted.use_card))
                accepted.invoker->setSkillInstanceStateValue("zhanyi", accepted.activationRef.key.instanceID,
                    "paid_phase", room->historyScopes().value("phase_id"));
            return true;
        }
        if (player && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const QVariantList grants = player->property("zhanyi_basic_grants").toList();
            room->setPlayerProperty(player, "zhanyi_basic_grants", QVariantList());
            room->setPlayerProperty(player, "zhanyi_receipts", QVariantMap());
            for (const QVariant &grant : grants)
                room->detachSkillFromPlayer(player, "zhanyi#" + QString::number(grant.toInt()));
            room->setPlayerMark(player, "ViewAsSkill_zhanyiEffect", 0);
            room->setPlayerFlag(player, "-zhanyiEquip");
            room->setPlayerFlag(player, "-zhanyiTrick");
        }
        return true;
    }
};

class Tunchu : public TriggerSkillV2
{
public:
    Tunchu() : TriggerSkillV2("tunchu")
    {
        events << DrawNCards;
        m_baseAmount = 2;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (player && player->isAlive() && player->hasSkill(objectName())
            && data.value<DrawStruct>().reason == "draw_phase")
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
    {
        return player->askForSkillInvoke("tunchu");
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        QVariantMap receipts = player->property("tunchu_receipts").toMap();
        receipts.insert(QString::number(ctx.activationRef.key.instanceID), QVariantMap{{"owner", ctx.sourceRef.ownerObjectName},
            {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}, {"activation", ctx.activationRef.key.instanceID}});
        room->setPlayerProperty(player, "tunchu_receipts", receipts);
        room->broadcastSkillInvoke("tunchu");
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num += getEffectiveAmount(ctx);
        *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};

class TunchuEffect : public TriggerSkillV2
{
public:
    TunchuEffect() : TriggerSkillV2("#tunchu-effect") { events << EventSkillInvoking << AfterDrawNCards << EventPhaseChanging; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            SkillContext accepted = data.value<SkillContext>();
            if (accepted.bypass_cost && accepted.skill_name == objectName()) {
                // Retiring an accepted receipt is bookkeeping, never a bypassable resource cost.
                if (!pay(accepted.current_event, room, accepted.invoker, accepted)) accepted.is_canceled = true;
                data.setValue(accepted);
            }
            return true;
        }
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::NotActive)
            room->setPlayerProperty(player, "tunchu_receipts", QVariantMap());
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event == EventSkillInvoking) return true;
        if (event != AfterDrawNCards || !player || !player->isAlive() || data.value<DrawStruct>().reason != "draw_phase") return true;
        const QVariantMap receipts = player->property("tunchu_receipts").toMap();
        for (auto it = receipts.cbegin(); it != receipts.cend(); ++it) {
            const QVariantMap receipt = it.value().toMap();
            SkillContext ctx; ctx.skill_name = objectName(); ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            ctx.owner = room->findPlayerByObjectName(ctx.sourceRef.ownerObjectName, true);
            ctx.extra_data = QVariantMap{{"key", it.key()}, {"receipt", receipt}}; ctx.targets = {player}; ctx.original_data = &data; ctx.current_event = event;
            if (ctx.owner) contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.sourceRef.isValid() && !ctx.extra_data.toMap().value("receipt").toMap().isEmpty(); }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariantMap receipts = ctx.invoker->property("tunchu_receipts").toMap();
        if (!receipts.remove(ctx.extra_data.toMap().value("key").toString())) return false;
        room->setPlayerProperty(ctx.invoker, "tunchu_receipts", receipts);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        const int count = qMin(player->getHandcardNum(), getEffectiveAmount(ctx));
        if (count <= 0) return false;
        const Card *chosen = room->askForExchange(player, "tunchu", count, count, false, "@tunchu-put");
        if (chosen) player->addToPile("food", chosen);
        return false;
    }
};

class TunchuLimit : public CardLimitSkill
{
public:
    TunchuLimit() : CardLimitSkill("#tunchu-limit")
    {
    }

    QString limitList(const Player *) const
    {
        return "use";
    }

    QString limitPattern(const Player *target) const
    {
        if (target->getPile("food").length()>0&&target->hasSkill("tunchu"))
            return "Slash,Duel";
        return "";
    }
};

ShuliangCard::ShuliangCard()
{
    setSkillName("shuliang");
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void ShuliangCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    CardMoveReason r(CardMoveReason::S_REASON_REMOVE_FROM_PILE, source->objectName(), "shuliang", "");
    room->moveCardTo(this, nullptr, Player::DiscardPile, r, true);
}

class ShuliangVS : public ViewAsSkillV2
{
public:
    ShuliangVS() : ViewAsSkillV2("shuliang", 1)
    {
        expand_pile = "food";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@shuliang");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ViewAsSkillV2::canSelectCard(request, card) && matchesFilter(request, card, ".")
            && request.initiator->getPile("food").contains(card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && replaySelection(this, request);
    }

    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "ShuliangCard";
    }
};

class Shuliang : public TriggerSkillV2
{
public:
    Shuliang() : TriggerSkillV2("shuliang")
    {
        events << EventPhaseStart;
        m_baseAmount = 2;
        view_as_skill = new ShuliangVS;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish || !player->isKongcheng()) return result;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isAlive() && p->hasSkill(objectName()) && !p->getPile("food").isEmpty())
                result[p] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!player || !player->isAlive() || !ctx.owner) return false;
        ctx.targets = {player};
        Room::BorrowedSkillScope scope(room, ctx.owner, objectName(), ctx.activationRef);
        const CardUseStruct use = room->askForUseCardStruct(ctx.owner, "@@shuliang", "@shuliang:" + player->objectName(), -1, Card::MethodNone);
        if (!use.card || use.card->subcardsLength() != 1) return false;
        ctx.extra_data = use.card->getSubcards().first();
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (!ctx.owner->getPile("food").contains(id)) return false;
        room->moveCardTo(Sanguosha->getCard(id), nullptr, Player::DiscardPile,
            CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, ctx.owner->objectName(), objectName(), ""), true);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class QingyiViewAsSkill : public ViewAsSkillV2
{
public:
    QingyiViewAsSkill() : ViewAsSkillV2("qingyi")
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return isPromptRequest(request, "@@qingyi");
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
		Slash *slash = new Slash(Card::NoSuit, 0);
		slash->setSkillName("qingyi");
        return slash;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "Slash";
    }
};

class Qingyi : public TriggerSkillV2
{
public:
    Qingyi() : TriggerSkillV2("qingyi")
    {
        events << EventPhaseChanging;
        view_as_skill = new QingyiViewAsSkill;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (player && player->isAlive() && player->hasSkill(objectName()) && change.to == Player::Judge
            && !player->isSkipped(Player::Judge) && !player->isSkipped(Player::Draw) && Slash::IsAvailable(player))
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        Room::BorrowedSkillScope source(room, ctx.owner, objectName(), ctx.activationRef);
        const CardUseStruct use = room->askForUseCardStruct(ctx.owner, "@@qingyi", "@qingyi-slash", -1, Card::MethodNone);
        if (!use.card || use.to.isEmpty()) return false;
        ctx.targets = use.to;
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.owner->skip(Player::Judge, true);
        ctx.owner->skip(Player::Draw, true);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        auto *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName(objectName());
        slash->deleteLater();
        room->useCardFromSkillEffect(CardUseStruct(slash, ctx.owner, ctx.targets), ctx, true);
        return false;
    }
};

class QingyiSlashNoDistanceLimit : public TargetModSkillV2
{
public:
    QingyiSlashNoDistanceLimit() : TargetModSkillV2("#qingyi-slash-ndl")
    {
        setBaseAmount(999);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType == TargetModSkill::DistanceLimit && ctx.card && ctx.card->getSkillName() == "qingyi")
            return CorrectSkillResult::useAmount(ctx.currentAmount);
        return CorrectSkillResult::noEffect();
    }
};

class Shixin : public TriggerSkillV2
{
public:
    Shixin() : TriggerSkillV2("shixin")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (player && player->isAlive() && player->hasSkill(objectName())
            && data.value<DamageStruct>().nature == DamageStruct::Fire)
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    // Returning true prevents the fire damage.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        room->broadcastSkillInvoke(objectName());

        LogMessage log;
        log.type = "#ShixinProtect";
        log.from = player;
        log.arg = QString::number(damage.damage);
        log.arg2 = "fire_nature";
        room->sendLog(log);
        room->notifySkillInvoked(player, objectName());
        return true;
    }
};

XuejiCard::XuejiCard()
{
    setSkillName("xueji");
}

bool XuejiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return Self->inMyAttackRange(to_select, subcards) && targets.length() < Self->getLostHp() && to_select != Self;
}

void XuejiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	DamageStruct damage;
	damage.from = source;
	damage.reason = "xueji";

	foreach (ServerPlayer *p, targets) {
		damage.to = p;
		room->damage(damage);
	}
	foreach (ServerPlayer *p, targets) {
		if (p->isAlive())
			p->drawCards(1, "xueji");
	}
}

class Xueji : public ViewAsSkillV2
{
public:
    Xueji() : ViewAsSkillV2("xueji", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->getLostHp() > 0 && request.initiator->canDiscard(request.initiator, "he"); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return ViewAsSkillV2::canSelectCard(request, card) && matchesFilter(request, card, ".|red") && !request.initiator->isJilei(card); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    {
        return target && target->isAlive() && target != request.initiator && !targets.contains(target)
            && targets.size() < request.initiator->getLostHp() && request.initiator->inMyAttackRange(target, request.selectedCardIds);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return !targets.isEmpty() && targets.size() <= request.initiator->getLostHp(); }
    QString historyKey(const ActiveSkillRequest &) const override { return "XuejiCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        const QList<ServerPlayer *> targets = ctx.targets;
        // Resolve every damage before any draw, retaining the original two passes.
        ctx.choice = "damage";
        for (ServerPlayer *target : targets) skillEffect(ctx, target);
        ctx.choice = "draw";
        for (ServerPlayer *target : targets) skillEffect(ctx, target);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "damage") ctx.invoker->getRoom()->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx)));
        else target->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
};

class Huxiao : public TargetModSkillV2
{
public:
    Huxiao() : TargetModSkillV2("huxiao") {}
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (ctx.modType != TargetModSkill::Residue || !ctx.holder) return CorrectSkillResult::noEffect();
        return CorrectSkillResult::useAmount(ctx.getStateValue("dodged_slashes").toInt() * ctx.currentAmount);
    }
};

class HuxiaoCount : public TriggerSkillV2
{
public:
    HuxiaoCount() : TriggerSkillV2("#huxiao-count") { events << CardOffset << EventPhaseChanging; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().from == Player::Play)
            for (int id : player->getSkillInstanceIds("huxiao")) player->removeSkillInstanceStateValue("huxiao", id, "dodged_slashes");
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != CardOffset) return {};
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        return effect.from && effect.from->isAlive() && effect.from->getPhase() == Player::Play
            && effect.card && effect.card->isKindOf("Slash") && effect.from->hasSkill(objectName())
            ? TriggerList{{effect.from, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // The helper's exact parent is the correcting Huxiao copy, not necessarily the root grant.
        for (const SkillInstance &entry : ctx.owner->getSkillInstances()) {
            if (entry.key() != ctx.activationRef.key || entry.parentRef.key.skillName != "huxiao") continue;
            const int id = entry.parentRef.key.instanceID;
            const int count = ctx.owner->getSkillInstanceStateValue("huxiao", id, "dodged_slashes").toInt();
            ctx.owner->setSkillInstanceStateValue("huxiao", id, "dodged_slashes", count + getEffectiveAmount(ctx));
            room->addPlayerMark(ctx.owner, "huxiao-PlayClear", getEffectiveAmount(ctx));
            break;
        }
        return false;
    }
};

class Wuji : public TriggerSkillV2
{
public:
	Wuji() : TriggerSkillV2("wuji")
	{
		events << EventPhaseStart << EventSkillInvoking;
        global = true;
		frequency = Wake;
	}

    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *, QVariant &, QList<SkillContext> &) const override
    { return event == EventSkillInvoking; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return false;
        const SkillContext accepted = data.value<SkillContext>();
        // Normal payment commits the quota; bypassed payment consumes the same exact source.
        if (accepted.bypass_cost && accepted.activationRef.isValid()
            && accepted.activationRef.key.skillName == objectName() && !accepted.use_card) addUsage(accepted);
        return true;
    }

	LimitScope getLimitScope() const override { return Limit_Game; }

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish) return TriggerList();
		if (spDamageThisTurn(room, player) < 3 && !hasWakeGrant(player, objectName())) return TriggerList();
		return TriggerList{{player, QStringList{objectName()}}};
	}

	bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || (spDamageThisTurn(room, ctx.owner) < 3 && !hasWakeGrant(ctx.owner, objectName()))) return false;
        addUsage(ctx);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const int damage = spDamageThisTurn(room, player);
		if (damage >= 3) {
			LogMessage log;
			log.type = "#WujiWake";
			log.from = player;
			log.arg = QString::number(damage);
			log.arg2 = objectName();
			room->sendLog(log);
		} else {
            player->canWake(objectName());
        }
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());
		room->doSuperLightbox(player, "wuji");

		room->setPlayerMark(player, "wuji", 1);
		if (room->changeMaxHpForAwakenSkill(player, getEffectiveAmount(ctx), objectName())) {
			RecoverStruct recover(objectName(), player); recover.recover = getEffectiveAmount(ctx); room->recover(player, recover);
			room->detachSkillFromPlayer(player, "huxiao");
		}

		return false;
	}
};

NewxuehenCard::NewxuehenCard()
{
    setSkillName("newxuehen");
}

bool NewxuehenCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
	return targets.length() < Self->getLostHp();
}

void NewxuehenCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		if (p->isAlive() && !p->isChained()) {
			room->setPlayerChained(p);
		}
	}
	if (source->isDead()) return;

	ServerPlayer *to = room->askForPlayerChosen(source, targets, "newxuehen", "@newxuehen-invoke");
	room->doAnimate(1, source->objectName(), to->objectName());
	room->damage(DamageStruct("newxuehen", source, to, 1, DamageStruct::Fire));
}

class Newxuehen : public ViewAsSkillV2
{
public:
    Newxuehen() : ViewAsSkillV2("newxuehen", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->getLostHp() > 0; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return ViewAsSkillV2::canSelectCard(request, card) && matchesFilter(request, card, ".|red") && !request.initiator->isJilei(card); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    { return target && target->isAlive() && !targets.contains(target) && targets.size() < request.initiator->getLostHp(); }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return !targets.isEmpty() && targets.size() <= request.initiator->getLostHp(); }
    QString historyKey(const ActiveSkillRequest &) const override { return "NewxuehenCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ctx.choice = "chain";
        ctx.extra_data = QStringList();
        const QList<ServerPlayer *> targets = ctx.targets;
        for (ServerPlayer *target : targets) skillEffect(ctx, target);
        if (!ctx.invoker->isAlive()) return FinishSkill;
        Room *room = ctx.invoker->getRoom();
        QList<ServerPlayer *> eligible;
        for (const QString &name : ctx.extra_data.toStringList())
            if (ServerPlayer *target = room->findPlayerByObjectName(name)) eligible << target;
        if (!eligible.isEmpty()) {
            ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, eligible, objectName(), "@newxuehen-invoke");
            ctx.choice = "damage";
            if (target) skillEffect(ctx, target);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        if (ctx.choice == "chain") {
            QStringList names = ctx.extra_data.toStringList(); names << target->objectName(); ctx.extra_data = names;
            if (!target->isChained()) room->setPlayerChained(target);
        } else {
            room->doAnimate(1, ctx.invoker->objectName(), target->objectName());
            room->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx), DamageStruct::Fire));
        }
        return ContinueEffects;
    }
};

class NewHuxiao : public TriggerSkillV2
{
public:
    NewHuxiao() : TriggerSkillV2("newhuxiao") { events << Damage << EventPhaseChanging; frequency = Compulsory; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            for (ServerPlayer *player : room->getAllPlayers(true)) room->setPlayerProperty(player, "newhuxiao_receipts", QVariantMap());
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Damage) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && damage.nature == DamageStruct::Fire
            && damage.from == player && damage.to && damage.to->isAlive() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.original_data->value<DamageStruct>().to}; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        target->drawCards(getEffectiveAmount(ctx), objectName());
        QVariantMap receipts = ctx.owner->property("newhuxiao_receipts").toMap();
        const QString key = QString::number(ctx.activationRef.key.instanceID);
        QVariantMap receipt = receipts.value(key).toMap();
        QStringList targets = receipt.value("targets").toStringList();
        if (!targets.contains(target->objectName())) targets << target->objectName();
        receipt.insert("targets", targets); receipt.insert("owner", ctx.sourceRef.ownerObjectName);
        receipt.insert("skill", ctx.sourceRef.key.skillName); receipt.insert("instance", ctx.sourceRef.key.instanceID);
        receipts.insert(key, receipt); room->setPlayerProperty(ctx.owner, "newhuxiao_receipts", receipts);
        room->addPlayerMark(ctx.owner, "newhuxiao_from-Clear"); room->addPlayerMark(target, "newhuxiao_to-Clear");
        return false;
    }
};

// Also hosts unrelated global residue rules; it is evaluated once, not per holder.
class NewHuxiaoTargetMod : public TargetModSkillV2
{
public:
	NewHuxiaoTargetMod() : TargetModSkillV2("#newhuxiao-target", ".")
	{
		setHolderSelector(CorrectSkill_System);
	}

	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		const Player *from = ctx.primary;
		const Player *to = ctx.secondary;
		if (ctx.modType != TargetModSkill::Residue || !from || !ctx.card) return CorrectSkillResult::noEffect();
        if (to) for (const QVariant &value : from->property("newhuxiao_receipts").toMap())
            if (value.toMap().value("targets").toStringList().contains(to->objectName()))
                return CorrectSkillResult::useAmount(999);
		if(from->hasFlag("CurrentPlayer")&&from->hasSkill("bsxianshuai")&&from->getMark(ctx.card->getSuitString()+"bsxianshuai-Clear")<1)
			return CorrectSkillResult::useAmount(999);
		if(from->getMark("&jiejie+"+ctx.card->getSuitString()+"_char-Clear")>0)
			return CorrectSkillResult::useAmount(999);
		return CorrectSkillResult::noEffect();
	}
};

class NewWuji : public TriggerSkillV2
{
public:
	NewWuji() : TriggerSkillV2("newwuji")
	{
		events << EventPhaseStart << EventSkillInvoking;
        global = true;
		frequency = Wake;
	}

    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *, QVariant &, QList<SkillContext> &) const override
    { return event == EventSkillInvoking; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return false;
        const SkillContext accepted = data.value<SkillContext>();
        // Normal payment commits the quota; bypassed payment consumes the same exact source.
        if (accepted.bypass_cost && accepted.activationRef.isValid()
            && accepted.activationRef.key.skillName == objectName() && !accepted.use_card) addUsage(accepted);
        return true;
    }

	LimitScope getLimitScope() const override { return Limit_Game; }

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish) return TriggerList();
		if (spDamageThisTurn(room, player) < 3 && !hasWakeGrant(player, objectName())) return TriggerList();
		return TriggerList{{player, QStringList{objectName()}}};
	}

	bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || (spDamageThisTurn(room, ctx.owner) < 3 && !hasWakeGrant(ctx.owner, objectName()))) return false;
        addUsage(ctx);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        const int damage = spDamageThisTurn(room, player);
		if (damage >= 3) {
			LogMessage log;
			log.type = "#WujiWake";
			log.from = player;
			log.arg = QString::number(damage);
			log.arg2 = objectName();
			room->sendLog(log);
		} else {
            player->canWake(objectName());
        }
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());

		room->doSuperLightbox(player, "newwuji");

		room->setPlayerMark(player, "newwuji", 1);
		if (room->changeMaxHpForAwakenSkill(player, getEffectiveAmount(ctx), objectName())) {
			RecoverStruct recover(objectName(), player); recover.recover = getEffectiveAmount(ctx); room->recover(player, recover);

			if (player->isAlive())
				room->handleAcquireDetachSkills(player, "-newhuxiao");

			if (player->isDead()) return false;
			foreach (ServerPlayer *p, room->getAlivePlayers()) {
				foreach (const Card *c, p->getCards("ej")) {
					if (Sanguosha->getEngineCard(c->getEffectiveId())->objectName() == "blade") {
						room->obtainCard(player, c, true);
						return false;
					}
				}
			}

			foreach (int id, room->getDrawPile() + room->getDiscardPile()) {
				if (Sanguosha->getEngineCard(id)->objectName() == "blade") {
					room->obtainCard(player, id, true);
					return false;
				}
			}
		}
		return false;
	}
};

class Duanbing : public TargetModSkillV2 {
public:
    Duanbing() : TargetModSkillV2("duanbing") { frequency = NotCompulsory; }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override {
        if (ctx.modType != TargetModSkill::ExtraTarget || !ctx.primary || !ctx.secondary || !ctx.card)
            return CorrectSkillResult::noEffect();
        // Consuming an offensive horse must not lend its distance reduction to the extra target.
        int rangefix = 0;
        const Card *horse = ctx.primary->getOffensiveHorse();
        if (horse && ctx.card->getSubcards().contains(horse->getId())
            && ctx.primary->hasOffensiveHorse(horse->objectName()))
            rangefix -= qobject_cast<const Horse *>(horse->getRealCard())->getCorrect(ctx.primary);
        return ctx.primary->distanceTo(ctx.secondary, rangefix) == 1
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
    bool requiresShowForUse(const CardUseStruct &use) const override {
        if (!use.from || !use.card || !use.card->isKindOf("Slash")
            || !use.from->hasSkill(objectName()) || use.from->hasShownSkill(objectName())) return false;
        const int ordinaryTargets = 1 + Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, use.from, use.card);
        return ordinaryTargets > 0 && use.to.size() > ordinaryTargets;
    }
};

class Fenxun : public ViewAsSkillV2 {
public:
    Fenxun() : ViewAsSkillV2("fenxun", 1) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "FenxunCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canDiscard(request.initiator, "he");
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override {
        if (!request.initiator || !card || card->hasFlag("using") || !request.selectedCardIds.isEmpty()) return false;
        const int id = card->getEffectiveId();
        return id >= 0 && (request.initiator->handCards().contains(id) || request.initiator->hasEquip(card))
            && request.initiator->canDiscard(request.initiator, id);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override {
        return cardSelectionFeasible(request) ? ViewAsSkillV2::createCard(request) : nullptr;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override {
        return cardSelectionFeasible(request) && ViewAsSkillV2::pay(room, ctx, request);
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override {
        return selected.isEmpty() && target && target->isAlive() && target != request.initiator;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override {
        return targets.size() == 1;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override {
        if (!ctx.invoker || !ctx.invoker->isAlive()) return ContinueEffects;
        // This paid turn-long effect survives skill loss. Multiple instances may choose different targets.
        QStringList targets = ctx.invoker->getTag("FenxunTargets").toStringList();
        if (!targets.contains(target->objectName())) {
            targets << target->objectName();
            ctx.invoker->setTag("FenxunTargets", targets);
            ctx.invoker->getRoom()->setFixedDistance(ctx.invoker, target, 1);
        }
        return ContinueEffects;
    }
};

// A global lifecycle recorder owns already-paid distances even after Fenxun is detached.
class FenxunClear : public TriggerSkillV2 {
public:
    FenxunClear() : TriggerSkillV2("#fenxun-clear") { events << EventPhaseChanging << Death; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override {
        if (!player || player->getTag("FenxunTargets").toStringList().isEmpty()) return true;
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        if (event == Death && data.value<DeathStruct>().who != player) return true;
        const QStringList targets = player->getTag("FenxunTargets").toStringList();
        player->removeTag("FenxunTargets");
        for (ServerPlayer *target : room->getAllPlayers(true))
            if (targets.contains(target->objectName())) room->removeFixedDistance(player, target, 1);
        return true;
    }
};

class Zhendu : public TriggerSkillV2 {
public:
    Zhendu() : TriggerSkillV2("zhendu") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Play) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != player && owner->isAlive() && owner->getPhase() != Player::Play && owner->canDiscard(owner, "h"))
                result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override {
        if (!ctx.owner || !player || !player->isAlive()) return false;
        const Card *card = room->askForCard(ctx.owner, ".|.|.|hand", "@zhendu-discard", QVariant(),
            Card::MethodNone, nullptr, false, objectName());
        if (!card || card->isVirtualCard() || !canPay(room, ctx.owner, card->getEffectiveId())) return false;
        ctx.extra_data = card->getEffectiveId();
        ctx.targets = {player};
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        bool ok = false;
        const int id = ctx.extra_data.toInt(&ok);
        if (!ok || !canPay(room, ctx.owner, id)) return false;
        room->throwCard(id, objectName(), ctx.owner);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override {
        Analeptic *analeptic = new Analeptic(Card::NoSuit, 0);
        analeptic->setSkillName("_zhendu");
        analeptic->deleteLater();
        ctx.owner->peiyin(this);
        room->useCardFromSkillEffect(CardUseStruct(analeptic, target, QList<ServerPlayer *>()), ctx, true);
        if (target->isAlive()) room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        return false;
    }
private:
    static bool canPay(Room *room, ServerPlayer *owner, int id) {
        return owner && owner->isAlive() && id >= 0 && room->getCardOwner(id) == owner
            && room->getCardPlace(id) == Player::PlaceHand && owner->canDiscard(owner, id);
    }
};

class Qiluan : public TriggerSkillV2 {
public:
    Qiluan() : TriggerSkillV2("qiluan") {
        events << Death << EventPhaseChanging;
        frequency = Frequent;
        m_baseAmount = 3;
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override {
        if (!ctx.owner || !ctx.original_data) return;
        if (event == Death) {
            const DeathStruct death = ctx.original_data->value<DeathStruct>();
            ServerPlayer *current = room->getCurrent();
            // Death visits every seat; count once at the victim, separately for each owning instance.
            if (death.who != player || !death.damage || death.damage->from != ctx.owner || !current
                || (!current->isAlive() && death.who != current) || current->getPhase() == Player::NotActive) return;
            const int count = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "kills").toInt();
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "kills", count + 1);
        } else if (ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive) {
            const int count = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "kills").toInt();
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "pending", count);
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "kills", 0);
        } else {
            ctx.owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "pending", 0);
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override {
        TriggerList result;
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) {
            for (int id : owner->getValidSkillInstanceIds(objectName())) {
                const int count = owner->getSkillInstanceStateValue(objectName(), id, "pending").toInt();
                if (owner->isAlive() && count > 0)
                    result[owner] << SkillInstanceUtils::formatName(objectName(), id) + "*" + QString::number(count);
            }
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        return ctx.owner && ctx.owner->isAlive() && room->askForSkillInvoke(ctx.owner, objectName());
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        room->broadcastSkillInvoke(objectName(), ctx.owner);
        if (ctx.owner && ctx.owner->isAlive()) ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class Gongao : public TriggerSkillV2
{
public:
	Gongao() : TriggerSkillV2("gongao")
	{
		events << Death;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		// Death visits every seat; the owner's own seat reports each other death.
		if (player && player->isAlive() && player->hasSkill(objectName()))
			return TriggerList{{player, QStringList{objectName()}}};
		return TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		room->broadcastSkillInvoke(objectName());
		room->sendCompulsoryTriggerLog(player, objectName());
		room->gainMaxHp(player, 1, objectName());
		room->recover(player, RecoverStruct("gongao", player));
		return false;
	}
};

class Juyi : public TriggerSkillV2
{
public:
	Juyi() : TriggerSkillV2("juyi")
	{
		events << EventPhaseStart << EventSkillInvoking;
        global = true;
		frequency = Wake;
		waked_skills = "benghuai,weizhong";
	}

	static bool naturalWake(ServerPlayer *zhugedan)
	{
		return zhugedan->isWounded() && zhugedan->getMaxHp() > zhugedan->aliveCount();
	}

    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *, QVariant &, QList<SkillContext> &) const override
    { return event == EventSkillInvoking; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return false;
        const SkillContext accepted = data.value<SkillContext>();
        // Normal payment commits the quota; bypassed payment consumes the same exact source.
        if (accepted.bypass_cost && accepted.activationRef.isValid()
            && accepted.activationRef.key.skillName == objectName() && !accepted.use_card) addUsage(accepted);
        return true;
    }

	LimitScope getLimitScope() const override { return Limit_Game; }

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start) return TriggerList();
		if (!naturalWake(player) && !hasWakeGrant(player, objectName())) return TriggerList();
		return TriggerList{{player, QStringList{objectName()}}};
	}

	bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || (!naturalWake(ctx.owner) && !hasWakeGrant(ctx.owner, objectName()))) return false;
        addUsage(ctx);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *zhugedan = ctx.owner;
		if (naturalWake(zhugedan)) {
			LogMessage log;
			log.type = "#JuyiWake";
			log.from = zhugedan;
			log.arg = QString::number(zhugedan->getMaxHp());
			log.arg2 = QString::number(zhugedan->aliveCount());
			log.arg3 = objectName();
			room->sendLog(log);
		} else {
            zhugedan->canWake(objectName());
        }
		zhugedan->peiyin(objectName());
		room->notifySkillInvoked(zhugedan, objectName());
		room->doSuperLightbox(zhugedan, "juyi");

		room->setPlayerMark(zhugedan, "juyi", 1);
		if (room->changeMaxHpForAwakenSkill(zhugedan, 0, objectName())) {
			int diff = zhugedan->getHandcardNum() - zhugedan->getMaxHp();
			if (diff < 0)
				room->drawCards(zhugedan, -diff * getEffectiveAmount(ctx), objectName());
			room->handleAcquireDetachSkills(zhugedan, "benghuai|weizhong");
		}

		return false;
	}
};

class Weizhong : public TriggerSkillV2
{
public:
	Weizhong() : TriggerSkillV2("weizhong")
	{
		events << MaxHpChanged;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (player && player->isAlive() && player->hasSkill(objectName()))
			return TriggerList{{player, QStringList{objectName()}}};
		return TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		room->broadcastSkillInvoke(objectName());
		room->sendCompulsoryTriggerLog(player, objectName());

		player->drawCards(getEffectiveAmount(ctx), objectName());
		return false;
	}
};

ZhoufuCard::ZhoufuCard()
{
    setSkillName("zhoufu");
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool ZhoufuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && to_select->getPile("incantation").isEmpty();
}

void ZhoufuCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	ServerPlayer *target = targets.first();
	target->setTag("ZhoufuSource" + QString::number(getEffectiveId()), QVariant::fromValue(source));
	target->addToPile("incantation", this);
}

class ZhoufuViewAsSkill : public ViewAsSkillV2
{
public:
    ZhoufuViewAsSkill() : ViewAsSkillV2("zhoufu", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return ViewAsSkillV2::canSelectCard(request, card) && matchesFilter(request, card, ".|.|.|hand"); }
    bool willThrowSelectedCards() const override { return false; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    { return target && target->isAlive() && target != request.initiator && targets.isEmpty() && target->getPile("incantation").isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "ZhoufuCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target->getPile("incantation").isEmpty()) return ContinueEffects;
        const int id = ctx.use_card->getSubcards().value(0, -1);
        if (id < 0 || !ctx.invoker->handCards().contains(id)) return ContinueEffects;
        QVariantMap receipts = target->property("zhoufu_receipts").toMap();
        receipts[QString::number(id)] = QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"giver", ctx.invoker->objectName()}};
        ctx.invoker->getRoom()->setPlayerProperty(target, "zhoufu_receipts", receipts);
        target->setTag("ZhoufuSource" + QString::number(id), QVariant::fromValue(ctx.invoker));
        target->addToPile("incantation", id);
        return ContinueEffects;
    }
};

class Zhoufu : public TriggerSkillV2
{
public:
    Zhoufu() : TriggerSkillV2("zhoufu")
    { events << StartJudge << FinishJudge << EventPhaseChanging; view_as_skill = new ZhoufuViewAsSkill; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == FinishJudge && player) {
            QVariantMap receipts = player->property("zhoufu_receipts").toMap();
            const QList<int> pending = player->getPile("incantation");
            // Retrials may replace judge.card; retire every consumed incantation receipt.
            for (const QString &key : receipts.keys())
                if (!pending.contains(key.toInt())) receipts.remove(key);
            room->setPlayerProperty(player, "zhoufu_receipts", receipts);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (!player || player->getPile("incantation").isEmpty()
            || (event != StartJudge && !(event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive))) return true;
        const int id = player->getPile("incantation").first();
        const QVariantMap receipt = player->property("zhoufu_receipts").toMap().value(QString::number(id)).toMap();
        if (receipt.isEmpty()) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
        ctx.owner = room->findPlayerByObjectName(ctx.sourceRef.ownerObjectName, true);
        ctx.invoker = ctx.initiator = player;
        ServerPlayer *recipient = event == StartJudge ? player : room->findPlayerByObjectName(receipt.value("giver").toString());
        if (!ctx.owner || !recipient) return true;
        ctx.targets = {recipient};
        ctx.extra_data = id;
        ctx.original_data = &data;
        ctx.current_event = event;
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->getPile("incantation").contains(ctx.extra_data.toInt()); }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int id = ctx.extra_data.toInt();
        if (event == StartJudge) {
            JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
            if (!judge) return false;
            judge->card = Sanguosha->getCard(id);
            LogMessage log; log.type = "$ZhoufuJudge"; log.from = target; log.arg = objectName(); log.card_str = QString::number(id); room->sendLog(log);
            room->moveCardTo(judge->card, nullptr, target, Player::PlaceJudge,
                CardMoveReason(CardMoveReason::S_REASON_JUDGE, target->objectName(), objectName(), judge->reason), true);
            if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceJudge) return false;
            judge->updateResult();
            ctx.original_data->setValue(judge);
            room->setTag("SkipGameRule", static_cast<int>(event));
        } else {
            target->obtainCard(Sanguosha->getCard(id));
            if (!ctx.invoker->getPile("incantation").contains(id)) {
                QVariantMap receipts = ctx.invoker->property("zhoufu_receipts").toMap();
                receipts.remove(QString::number(id));
                room->setPlayerProperty(ctx.invoker, "zhoufu_receipts", receipts);
            }
        }
        return false;
    }
};

class Yingbing : public TriggerSkillV2
{
public:
	Yingbing() : TriggerSkillV2("yingbing")
	{
		events << StartJudge;
		frequency = Frequent;
        m_baseAmount = 2;
	}

	bool usesEventPriority() const override
	{
		return true;
	}

	int getPriority(TriggerEvent) const override
	{
		return -2;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		JudgeStruct *judge = data.value<JudgeStruct *>();
		if (!player || !judge || !judge->card) return TriggerList();
		const QVariantMap receipt = player->property("zhoufu_receipts").toMap().value(QString::number(judge->card->getEffectiveId())).toMap();
        ServerPlayer *zhangbao = room->findPlayerByObjectName(receipt.value("giver").toString());
		if (zhangbao && zhangbao->isAlive() && zhangbao->hasSkill(objectName()))
			return TriggerList{{zhangbao, QStringList{objectName()}}};
		return TriggerList();
	}

	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		return ctx.owner->askForSkillInvoke(this, *ctx.original_data);
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		room->broadcastSkillInvoke(objectName());
		ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
		return false;
	}
};

class Xingwu : public TriggerSkillV2
{
public:
    Xingwu() : TriggerSkillV2("xingwu") { events << EventPhaseStart << CardsMoveOneTime; m_baseAmount = 2; }
    static QString pilePattern(ServerPlayer *player)
    {
        if (player->isKongcheng()) return QString();
        const QVariantMap history = player->getRoom()->queryCardHistory(player, "turn");
        if (!history.value("complete").toBool()) return QString();
        bool red = false, black = false;
        for (const QVariant &entry : history.value("items").toList()) {
            const QVariantMap card = entry.toMap();
            if (!card.contains("red") || !card.contains("black")) return QString();
            red = red || card.value("red").toBool();
            black = black || card.value("black").toBool();
        }
        if (red && black) return QString();
        return red != black ? QString(".|%1|.|hand").arg(red ? "black" : "red") : ".|.|.|hand";
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Discard || pilePattern(player).isEmpty()) return {};
        } else {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to != player || move.to_place != Player::PlaceSpecial || move.to_pile_name != objectName()
                || player->getPile(objectName()).size() < 3) return {};
        }
        return TriggerList{{player, {objectName()}}};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            const Card *card = room->askForCard(ctx.owner, pilePattern(ctx.owner), "@xingwu", QVariant(), Card::MethodNone);
            if (!card) return false;
            ctx.extra_data = card->getEffectiveId();
        } else {
            QList<ServerPlayer *> targets;
            for (ServerPlayer *target : room->getAlivePlayers()) if (target->isMale()) targets << target;
            if (!targets.isEmpty()) {
                ServerPlayer *target = room->askForPlayerChosen(ctx.owner, targets, objectName(), "@xingwu-choose");
                if (target) ctx.targets = {target};
            }
        }
        return true;
    }
    bool pay(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) return ctx.owner->handCards().contains(ctx.extra_data.toInt());
        if (ctx.owner->getPile(objectName()).size() < 3) return false;
        ctx.owner->clearOnePrivatePile(objectName());
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            room->broadcastSkillInvoke(objectName(), 1);
            ctx.owner->addToPile(objectName(), ctx.extra_data.toInt());
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName(), 2);
        room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        if (!ctx.owner->isAlive() || !target->isAlive()) return false;
        DummyCard discarded;
        for (const Card *card : target->getEquips())
            if (ctx.owner->canDiscard(target, card->getEffectiveId())) discarded.addSubcard(card);
        if (discarded.subcardsLength() > 0) room->throwCard(&discarded, target, ctx.owner);
        return false;
    }
};

class Luoyan : public TriggerSkillV2
{
public:
    Luoyan(const QString &name) : TriggerSkillV2(name)
    { events << CardsMoveOneTime << EventAcquireSkill; frequency = Compulsory; waked_skills = "timobileanxiang,liuli"; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName(), true)) return {};
        if (event == EventAcquireSkill) {
            if (data.value<SkillChangeStruct>().skillName != objectName()) return {};
        } else {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (!(move.to == player && move.to_place == Player::PlaceSpecial && move.to_pile_name == "xingwu")
                && !(move.from == player && move.from_places.contains(Player::PlaceSpecial) && move.from_pile_names.contains("xingwu"))) return {};
        }
        return TriggerList{{player, {objectName()}}};
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QStringList names{objectName() == "olluoyan" ? QStringLiteral("oltimobileanxiang") : QStringLiteral("timobileanxiang"), QStringLiteral("liuli")};
        // Each living Luoyan instance lends its own children; retiring it cannot strip native skills.
        if (!ctx.owner->getPile("xingwu").isEmpty()) {
            for (const QString &name : names) room->attachSkillToPlayer(ctx.owner, name, ctx.activationRef);
        } else {
            for (const SkillInstance &entry : ctx.owner->getSkillInstances())
                if (entry.parentRef == ctx.activationRef && names.contains(entry.skillName))
                    room->detachAttachedSkill(SkillInstanceRef(ctx.owner->objectName(), entry.key()));
        }
        return false;
    }
};

class Shenxian : public TriggerSkillV2
{
public:
	Shenxian() : TriggerSkillV2("shenxian")
	{
		events << CardsMoveOneTime;
		frequency = Frequent;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->hasFlag("CurrentPlayer"))
			return TriggerList();
		CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
		if ((move.from_places.contains(Player::PlaceHand)||move.from_places.contains(Player::PlaceEquip))
			&&move.from&&move.from!=player&&move.from->isAlive()&&(move.reason.m_reason&CardMoveReason::S_MASK_BASIC_REASON)==CardMoveReason::S_REASON_DISCARD) {
			foreach (int id, move.card_ids) {
				if (Sanguosha->getCard(id)->getTypeId() == Card::TypeBasic)
					return TriggerList{{player, QStringList{objectName()}}};
			}
		}
		return TriggerList();
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		return room->askForSkillInvoke(player, objectName(), *ctx.original_data);
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		room->broadcastSkillInvoke(objectName());
		player->drawCards(getEffectiveAmount(ctx), objectName());
		return false;
	}
};

QiangwuCard::QiangwuCard()
{
    setSkillName("qiangwu");
	target_fixed = true;
}

void QiangwuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	JudgeStruct judge;
	judge.pattern = ".";
	judge.who = source;
	judge.reason = "qiangwu";
	judge.play_animation = false;
	room->judge(judge);

	room->setPlayerMark(source, "qiangwu-Clear", judge.card->getNumber());
}

class QiangwuViewAsSkill : public ViewAsSkillV2
{
public:
    QiangwuViewAsSkill() : ViewAsSkillV2("qiangwu") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
    QString historyKey(const ActiveSkillRequest &) const override { return "QiangwuCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.invoker->getRoom();
        JudgeStruct judge;
        judge.pattern = ".";
        judge.who = ctx.invoker;
        judge.reason = objectName();
        judge.play_animation = false;
        room->judge(judge);
        if (!judge.card) return ContinueEffects;
        QVariantMap receipts = ctx.invoker->property("qiangwu_receipts").toMap();
        const QString key = ctx.sourceRef.ownerObjectName + ":" + QString::number(ctx.sourceRef.key.instanceID);
        receipts[key] = judge.card->getNumber();
        room->setPlayerProperty(ctx.invoker, "qiangwu_receipts", receipts);
        room->setPlayerMark(ctx.invoker, "qiangwu-Clear", judge.card->getNumber());
        return ContinueEffects;
    }
};

class Qiangwu : public TriggerSkillV2
{
public:
    Qiangwu() : TriggerSkillV2("qiangwu")
    {
        events << PreCardUsed << EventPhaseChanging;
        view_as_skill = new QiangwuViewAsSkill;
        global = true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to == Player::NotActive)
                room->setPlayerProperty(player, "qiangwu_receipts", QVariantMap());
            return true;
        }
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash")) return true;
        // The accepted judgement remains active after its granting skill disappears.
        for (const QVariant &receipt : player->property("qiangwu_receipts").toMap()) {
            if (receipt.toInt() > 0 && use.card->getNumber() > receipt.toInt()) {
                use.m_addHistory = false;
                data = QVariant::fromValue(use);
                break;
            }
        }
        return true;
    }
};

static bool XSGongliTrigger(const Player *player, const QString &mt)
{
	if (!isNormalGameMode(player->getGameMode()) && player->hasSkill("xsgongli")) {
		foreach (const Player *p, player->getAliveSiblings()) {
			if (p->getGeneralName().contains(mt) && player->isYourFriend(p))
				return true;
		}
	}
	return false;
}

// Also hosts unrelated global Slash rules; it is evaluated once, not per holder.
class QiangwuTargetMod : public TargetModSkillV2
{
public:
	QiangwuTargetMod() : TargetModSkillV2("#qiangwu-target")
	{
		setHolderSelector(CorrectSkill_System);
	}

	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		const Player *from = ctx.primary;
		const Card *card = ctx.card;
		if (!from || !card) return CorrectSkillResult::noEffect();
		if (ctx.modType == TargetModSkill::DistanceLimit) {
			for (const QVariant &receipt : from->property("qiangwu_receipts").toMap())
            if (card->getNumber() < receipt.toInt())
				return CorrectSkillResult::useAmount(999);
			if(card->getSkillName().contains("xuanjian")&&XSGongliTrigger(from,"you_pangtong"))
				return CorrectSkillResult::useAmount(999);
		} else if (ctx.modType == TargetModSkill::Residue) {
			for (const QVariant &receipt : from->property("qiangwu_receipts").toMap())
            if (receipt.toInt() > 0 && (card->getNumber() > receipt.toInt() || card->hasFlag("Global_SlashAvailabilityChecker")))
				return CorrectSkillResult::useAmount(999);
			if (from->getPile("yangming").contains(card->getEffectiveId()))
				return CorrectSkillResult::useAmount(999);
			if (from->getMark("guidian2") != 0)
				return CorrectSkillResult::useAmount(from->getMark("guidian2"));
		}
		return CorrectSkillResult::noEffect();
	}
};

class Xiaoguo : public TriggerSkillV2 {
public:
    Xiaoguo() : TriggerSkillV2("xiaoguo") {
        events << EventPhaseStart;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) {
            if (owner != player && owner->isAlive() && owner->canDiscard(owner, "h"))
                result[owner] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        if (!ctx.owner || !ctx.invoker || !ctx.invoker->isAlive()) return false;
        // Selection is cancellable; only pay() may discard the chosen basic card.
        const Card *card = room->askForCard(ctx.owner, ".Basic", "@xiaoguo",
            QVariant::fromValue(ctx.invoker), Card::MethodNone, nullptr, false, objectName());
        if (!card || card->isVirtualCard() || !canPay(room, ctx.owner, card->getEffectiveId()))
            return false;
        ctx.extra_data = card->getEffectiveId();
        ctx.targets = QList<ServerPlayer *>{ctx.invoker};
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        bool ok = false;
        const int id = ctx.extra_data.toInt(&ok);
        if (!ok || !canPay(room, ctx.owner, id)) return false;
        room->throwCard(id, objectName(), ctx.owner);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override {
        room->broadcastSkillInvoke(objectName(), 1, ctx.owner);
        return false;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override {
        if (!target || !target->isAlive() || !ctx.owner) return false;
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, ctx.owner->objectName(), target->objectName());
        if (!room->askForCard(target, ".Equip", "@xiaoguo-discard", QVariant::fromValue(ctx.owner))) {
            room->broadcastSkillInvoke(objectName(), 2, ctx.owner);
            room->damage(DamageStruct(objectName(), ctx.owner, target, getEffectiveAmount(ctx)));
        } else {
            // The identity version rewards the owner when the target discards equipment.
            room->broadcastSkillInvoke(objectName(), 3, ctx.owner);
            if (ctx.owner->isAlive()) ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        }
        return false;
    }

private:
    static bool canPay(Room *room, ServerPlayer *owner, int id) {
        if (!owner || !owner->isAlive() || id < 0 || room->getCardOwner(id) != owner
            || room->getCardPlace(id) != Player::PlaceHand || !owner->canDiscard(owner, id))
            return false;
        const Card *card = room->getCard(id);
        return card && card->isKindOf("BasicCard");
    }
};

class Kuangfu : public TriggerSkillV2 {
public:
    Kuangfu() : TriggerSkillV2("kuangfu") { events << Damage; }
    QStringList equipment(ServerPlayer *owner, ServerPlayer *target) const {
        QStringList result;
        if (!owner || !target || !target->isAlive()) return result;
        for (int i = 0; i < S_EQUIP_AREA_LENGTH; ++i) {
            const Card *card = target->getEquip(i);
            if (card && (owner->canDiscard(target, card->getEffectiveId()) || !owner->getEquip(i)))
                result << QString::number(i);
        }
        return result;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override {
        const DamageStruct damage = data.value<DamageStruct>();
        if (player && player->isAlive() && player->hasSkill(objectName()) && damage.card
            && damage.card->isKindOf("Slash") && damage.to && !damage.to->hasFlag("Global_DebutFlag")
            && !damage.chain && !damage.transfer && !equipment(player, damage.to).isEmpty())
            return TriggerList{{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override {
        if (!ctx.owner || !ctx.original_data || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets = {ctx.original_data->value<DamageStruct>().to};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override {
        const QStringList available = equipment(ctx.owner, target);
        if (available.isEmpty()) return false;
        const QString selected = room->askForChoice(ctx.owner, "kuangfu_equip", available.join("+"), QVariant::fromValue(target));
        if (!available.contains(selected)) return false;
        const int index = selected.toInt();
        const Card *card = target->getEquip(index);
        if (!card) return false;
        QStringList choices;
        if (ctx.owner->canDiscard(target, card->getEffectiveId())) choices << "throw";
        if (!ctx.owner->getEquip(index)) choices << "move";
        if (choices.isEmpty()) return false;
        const QString choice = room->askForChoice(ctx.owner, objectName(), choices.join("+"));
        // Interceptors and nested decisions may change equipment; recheck before moving it.
        if (target->getEquip(index) != card || !ctx.owner->isAlive()) return false;
        if (choice == "move" && !ctx.owner->getEquip(index)) {
            room->broadcastSkillInvoke(objectName(), 1, ctx.owner);
            room->moveCardTo(card, ctx.owner, Player::PlaceEquip);
        } else if (choice == "throw" && ctx.owner->canDiscard(target, card->getEffectiveId())) {
            room->broadcastSkillInvoke(objectName(), 2, ctx.owner);
            room->throwCard(card, target, ctx.owner);
        }
        return false;
    }
};

class Danji : public TriggerSkillV2
{
public:
	Danji() : TriggerSkillV2("danji")
	{ // What a silly skill!
		events << EventPhaseStart << EventSkillInvoking;
        global = true;
		frequency = Wake;
	}

	static bool naturalWake(Room *room, ServerPlayer *guanyu)
	{
		if (guanyu->getHandcardNum() <= guanyu->getHp()) return false;
		ServerPlayer *the_lord = room->getLord();
		return the_lord && (the_lord->getGeneralName().contains("caocao") || the_lord->getGeneral2Name().contains("caocao"));
	}

    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *, QVariant &, QList<SkillContext> &) const override
    { return event == EventSkillInvoking; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillInvoking) return false;
        const SkillContext accepted = data.value<SkillContext>();
        // Normal payment commits the quota; bypassed payment consumes the same exact source.
        if (accepted.bypass_cost && accepted.activationRef.isValid()
            && accepted.activationRef.key.skillName == objectName() && !accepted.use_card) addUsage(accepted);
        return true;
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start) return TriggerList();
		if (!naturalWake(room, player) && !hasWakeGrant(player, objectName())) return TriggerList();
		return TriggerList{{player, QStringList{objectName()}}};
	}

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || (!naturalWake(room, ctx.owner) && !hasWakeGrant(ctx.owner, objectName()))) return false;
        addUsage(ctx);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *guanyu = ctx.owner;
		if (naturalWake(room, guanyu)) {
			LogMessage log;
			log.type = "#DanjiWake";
			log.from = guanyu;
			log.arg = QString::number(guanyu->getHandcardNum());
			log.arg2 = QString::number(guanyu->getHp());
			room->sendLog(log);
		} else {
            guanyu->canWake(objectName());
        }
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(guanyu, objectName());

		//room->doLightbox("$DanjiAnimate", 5000);
		room->doSuperLightbox(guanyu, "danji");

		room->setPlayerMark(guanyu, "danji", 1);
		if (room->changeMaxHpForAwakenSkill(guanyu, -1, objectName()))
			room->acquireSkill(guanyu, "mashu");
		return false;
	}
};

class Kangkai : public TriggerSkillV2
{
public:
    Kangkai() : TriggerSkillV2("kangkai") { events << TargetConfirmed; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Slash")) return true;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            const SkillInstanceRef activation(player->objectName(), SkillInstanceKey(objectName(), id));
            for (ServerPlayer *target : use.to) {
                if (!target || !target->isAlive() || player->distanceTo(target) > 1) continue;
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = ctx.invoker = ctx.initiator = player;
                ctx.instanceID = id;
                ctx.activationRef = activation;
                ctx.sourceRef = room->resolveSkillInstanceRootRef(activation);
                ctx.targets = {target};
                ctx.preferredTarget = target;
                ctx.preferredTargetSeat = target->getSeat();
                bool ok = false; ctx.amount = room->getSkillInstanceAmount(activation, &ok); if (!ok) ctx.amount = getBaseAmount();
                ctx.original_data = &data;
                ctx.current_event = event;
                contexts << ctx;
            }
        }
        return true;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.targets.size() != 1) return false;
        const QVariant previous = ctx.owner->getTag("KangkaiSlash");
        ctx.owner->setTag("KangkaiSlash", *ctx.original_data);
        const auto restore = qScopeGuard([&]() { ctx.owner->setTag("KangkaiSlash", previous); });
        return room->askForSkillInvoke(ctx.owner, objectName(), QVariant::fromValue(ctx.targets.first()));
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        ctx.owner->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner->isAlive() || ctx.owner->isNude() || ctx.owner == target) return false;
        const Card *card = nullptr;
        if (ctx.owner->getCardCount() > 1) card = room->askForCard(ctx.owner, "..!", "@kangkai-give:" + target->objectName(), *ctx.original_data, Card::MethodNone);
        if (!card) card = ctx.owner->getCards("he").at(qsanRandomBounded(ctx.owner->getCardCount()));
        const int id = card->getEffectiveId();
        room->obtainCard(target, card, CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.owner->objectName(), objectName(), ""));
        card = Sanguosha->getCard(id);
        if (!target->isAlive() || room->getCardOwner(id) != target || card->getTypeId() != Card::TypeEquip || target->isLocked(card)) return false;
        const QVariant previousSlash = target->getTag("KangkaiSlash"), previousCard = target->getTag("KangkaiGivenCard");
        target->setTag("KangkaiSlash", *ctx.original_data);
        target->setTag("KangkaiGivenCard", QVariant::fromValue(card));
        const auto restore = qScopeGuard([&]() { target->setTag("KangkaiSlash", previousSlash); target->setTag("KangkaiGivenCard", previousCard); });
        if (room->askForSkillInvoke(target, "kangkai_use", "use"))
            room->useCardFromSkillEffect(CardUseStruct(card, target), ctx);
        return false;
    }
};

class Meibu : public TriggerSkillV2
{
public:
	Meibu() : TriggerSkillV2("meibu")
	{
		events << EventPhaseStart << EventPhaseChanging;
        global = true;
	}

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        const QVariantList grants = player->property("meibu_grants").toList();
        room->setPlayerProperty(player, "meibu_grants", QVariantList());
        for (const QVariant &value : grants) {
            const QVariantMap grant = value.toMap();
            room->detachSkillFromPlayer(player, "#meibu-filter#" + QString::number(grant.value("id").toInt()));
            if (ServerPlayer *owner = room->findPlayerByObjectName(grant.value("owner").toString(), true)) room->removeAttackRangePair(player, owner);
        }
        if (!grants.isEmpty()) room->filterCards(player, player->getCards("he"), true);
        return true;
    }

	TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		TriggerList result;
		if (triggerEvent != EventPhaseStart || !player || player->getPhase() != Player::Play) return result;
		foreach(ServerPlayer*sunluyu, room->getOtherPlayers(player)){
			if (!player->inMyAttackRange(sunluyu) && sunluyu->isAlive() && sunluyu->hasSkill(objectName()))
				result[sunluyu] << objectName();
		}
		return result;
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *sunluyu, SkillContext &ctx) const override
	{
        ctx.targets = {ctx.invoker};
		return room->askForSkillInvoke(ctx.owner, objectName());
	}

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *player) const override
    {
        room->broadcastSkillInvoke(objectName());
        const int id = room->acquireSkillFromEffect(player, "#meibu-filter", ctx, false);
        if (id <= 0) return false;
        QVariantList grants = player->property("meibu_grants").toList();
        grants << QVariantMap{{"id", id}, {"owner", ctx.owner->objectName()}};
        room->setPlayerProperty(player, "meibu_grants", grants);
        room->filterCards(player, player->getCards("he"), false);
        room->insertAttackRangePair(player, ctx.owner);
        return false;
    }

	int getEffectIndex(const ServerPlayer*, const Card*card) const override
	{
		if (card->isKindOf("Slash"))
			return -2;
		return -1;
	}
};

class Mumu : public TriggerSkillV2
{
public:
	Mumu() : TriggerSkillV2("mumu")
	{
		events << EventPhaseStart;
	}

	static void candidates(Room *room, ServerPlayer *player, QList<ServerPlayer *> &weapon_players,
		QList<ServerPlayer *> &armor_players)
	{
		foreach(ServerPlayer*p, room->getAlivePlayers()){
			if (p->getWeapon() && player->canDiscard(p, p->getWeapon()->getEffectiveId()))
				weapon_players << p;
			if (p != player && p->getArmor())
				armor_players << p;
		}
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish
			|| spDamageThisTurn(room, player, true) != 0) return TriggerList();
		QList<ServerPlayer*> weapon_players, armor_players;
		candidates(room, player, weapon_players, armor_players);
		if (weapon_players.isEmpty() && armor_players.isEmpty()) return TriggerList();
		return TriggerList{{player, QStringList{objectName()}}};
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		QList<ServerPlayer*> weapon_players, armor_players;
		candidates(room, player, weapon_players, armor_players);
		ServerPlayer*victim = room->askForPlayerChosen(player, weapon_players+armor_players, objectName(), "@mumu",true,true);
		if (!victim) return false;
		QStringList choices;
		if (armor_players.contains(victim)) choices.append("armor");
		if (weapon_players.contains(victim)) choices.append("weapon");
		ctx.choice = room->askForChoice(player, objectName(), choices.join("+"));
		ctx.targets = QList<ServerPlayer *>{victim};
		return true;
	}

	bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *victim) const override
	{
		if (ctx.choice == "weapon"){
			if (!victim->getWeapon()) return false;
			room->broadcastSkillInvoke(objectName(), 1);
			room->throwCard(victim->getWeapon(), victim, player);
			player->drawCards(getEffectiveAmount(ctx), objectName());
		} else {
			if (!victim->getArmor()) return false;
			room->broadcastSkillInvoke(objectName(), 2);
			int equip = victim->getArmor()->getEffectiveId();
			QList<CardsMoveStruct> exchangeMove;
			CardsMoveStruct move1(equip, player, Player::PlaceEquip, CardMoveReason(CardMoveReason::S_REASON_ROB, player->objectName()));
			exchangeMove.push_back(move1);
			if (player->getArmor()){
				CardsMoveStruct move2(player->getArmor()->getEffectiveId(), nullptr, Player::DiscardPile,
					CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP, player->objectName()));
				exchangeMove.push_back(move2);
			}
			room->moveCardsAtomic(exchangeMove, true);
		}
		return false;
	}
};

class Junbing : public TriggerSkillV2
{
public:
    Junbing() : TriggerSkillV2("junbing") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish || player->getHandcardNum() > 1) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) if (owner->isAlive()) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->askForSkillInvoke(this, QString("junbing_invoke:%1").arg(ctx.owner->objectName())); }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        room->broadcastSkillInvoke(objectName());
        ctx.choice = "draw"; ctx.extra_data = false;
        skillEffect(ctx.current_event, ctx.invoker->getRoom(), ctx.invoker, ctx, ctx.invoker);
        if (!ctx.extra_data.toBool() || ctx.invoker == ctx.owner || !ctx.invoker->isAlive() || !ctx.owner->isAlive()) return false;
        ctx.choice = "give"; ctx.extra_data = 0;
        skillEffect(ctx.current_event, ctx.invoker->getRoom(), ctx.invoker, ctx, ctx.owner);
        if (ctx.extra_data.toInt() > 0 && ctx.invoker->isAlive() && ctx.owner->isAlive()) {
            ctx.choice = "return";
            skillEffect(ctx.current_event, ctx.invoker->getRoom(), ctx.invoker, ctx, ctx.invoker);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            ctx.extra_data = true;
        } else if (ctx.choice == "give") {
            const QList<int> ids = ctx.invoker->handCards();
            if (ids.isEmpty()) return false;
            DummyCard gift(ids);
            room->obtainCard(target, &gift, CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.invoker->objectName(), objectName(), QString()), false);
            ctx.extra_data = ids.size();
        } else {
            const int n = qMin(ctx.extra_data.toInt(), ctx.owner->getHandcardNum());
            if (n <= 0) return false;
            const Card *cards = room->askForExchange(ctx.owner, objectName(), n, n, false,
                QString("@junbing-return:%1::%2").arg(target->objectName()).arg(n));
            if (cards) room->obtainCard(target, cards, CardMoveReason(CardMoveReason::S_REASON_GIVE, ctx.owner->objectName(), objectName(), QString()), false);
        }
        return false;
    }
};

QujiCard::QujiCard()
{
    setSkillName("quji");
}

bool QujiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*) const
{
	if (subcardsLength() <= targets.length())
		return false;
	return to_select->isWounded();
}

bool QujiCard::targetsFeasible(const QList<const Player*> &targets, const Player*) const
{
	if (targets.length() > 0){
		foreach(const Player*p, targets){
			if (!p->isWounded())
				return false;
		}
		return true;
	}
	return false;
}

void QujiCard::use(Room*room, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	foreach(ServerPlayer*p, targets)
		room->cardEffect(this, source, p);

	foreach(int id, getSubcards()){
		if (Sanguosha->getCard(id)->isBlack()){
			room->loseHp(HpLostStruct(source, 1, "quji", source));
			break;
		}
	}
}

void QujiCard::onEffect(CardEffectStruct &effect) const
{
	RecoverStruct recover;
	recover.who = effect.from;
	recover.recover = 1;
	recover.reason = "quji";
	effect.to->getRoom()->recover(effect.to, recover);
}

class Quji : public ViewAsSkillV2
{
public:
    Quji() : ViewAsSkillV2("quji") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->isWounded(); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !request.selectedCardIds.contains(card->getEffectiveId()) && !card->hasFlag("using")
            && !request.initiator->isJilei(card) && request.selectedCardIds.size() < request.initiator->getLostHp()
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->hasEquip(card));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return request.initiator && !request.selectedCardIds.isEmpty() && request.selectedCardIds.size() == request.initiator->getLostHp() && replaySelection(this, request); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    { return target && target->isAlive() && target->isWounded() && !targets.contains(target) && targets.size() < request.selectedCardIds.size(); }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return !targets.isEmpty() && targets.size() <= request.selectedCardIds.size(); }
    QString historyKey(const ActiveSkillRequest &) const override { return "QujiCard"; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card = ViewAsSkillV2::createCard(request);
        if (!card) return nullptr;
        bool black = false;
        for (int id : request.selectedCardIds) black = black || Sanguosha->getCard(id)->isBlack();
        // The authoritative rebuild freezes effect inputs even when cost is bypassed.
        card->setTag("v2_effect_input", black);
        return card;
    }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        bool black = false;
        for (int id : request.selectedCardIds) black = black || Sanguosha->getCard(id)->isBlack();
        ctx.extra_data = black;
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.extra_data.isValid() && ctx.use_card) ctx.extra_data = ctx.use_card->getTag("v2_effect_input");
        const bool black = ctx.extra_data.toBool();
        ctx.manual_effect = true;
        const QList<ServerPlayer *> targets = ctx.targets;
        for (ServerPlayer *target : targets) skillEffect(ctx, target);
        // The accepted material penalty follows all recoveries, even if a target was canceled.
        if (black && ctx.invoker->isAlive()) ctx.invoker->getRoom()->loseHp(HpLostStruct(ctx.invoker, 1, objectName(), ctx.invoker));
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.invoker->getRoom()->recover(target, RecoverStruct(ctx.invoker, nullptr, getEffectiveAmount(ctx), objectName()));
        return ContinueEffects;
    }
};

class Jilei : public TriggerSkillV2
{
public:
	Jilei() : TriggerSkillV2("jilei")
	{
		events << Damaged;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		ServerPlayer *current = room->getCurrent();
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !data.value<DamageStruct>().from
			|| !current || current->getPhase() == Player::NotActive || current->isDead())
			return TriggerList();
		return TriggerList{{player, QStringList{objectName()}}};
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *yangxiu, SkillContext &ctx) const override
	{
		if (!room->askForSkillInvoke(yangxiu, objectName(), *ctx.original_data)) return false;
		ctx.choice = room->askForChoice(yangxiu, objectName(), "BasicCard+EquipCard+TrickCard");
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		DamageStruct damage = ctx.original_data->value<DamageStruct>();
		QString choice = ctx.choice;
		room->broadcastSkillInvoke(objectName());

		LogMessage log;
		log.type = "#Jilei";
		log.from = damage.from;
		log.arg = choice;
		room->sendLog(log);

		QStringList jilei_list = damage.from->getTag(objectName()).toStringList();
		if (jilei_list.contains(choice)) return false;
		jilei_list.append(choice);
		damage.from->setTag(objectName(), QVariant::fromValue(jilei_list));
		QString _type = choice + "|.|.|hand"; // Handcards only
		room->setPlayerCardLimitation(damage.from, "use,response,discard", _type, true);

		QString type_name = choice.replace("Card", "").toLower();
		if (damage.from->getMark("@jilei_" + type_name) == 0)
			room->addPlayerMark(damage.from, "@jilei_" + type_name);
		return false;
	}
};

class JileiClear : public TriggerSkillV2
{
public:
	JileiClear() : TriggerSkillV2("#jilei-clear")
	{
		events << EventPhaseChanging << Death;
	}

	bool usesEventPriority() const override
	{
		return true;
	}

	int getPriority(TriggerEvent) const override
	{
		return 5;
	}

	// Turn-end cleanup applies to every limited player, not only the skill owner.
	bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data) const override
	{
		if (triggerEvent == EventPhaseChanging){
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to != Player::NotActive)
				return true;
		} else if (triggerEvent == Death){
			DeathStruct death = data.value<DeathStruct>();
			if (death.who != target || target != room->getCurrent())
				return true;
		}
		QList<ServerPlayer*> players = room->getAllPlayers();
		foreach(ServerPlayer*player, players){
			QStringList jilei_list = player->getTag("jilei").toStringList();
			if (!jilei_list.isEmpty()){
				LogMessage log;
				log.type = "#JileiClear";
				log.from = player;
				room->sendLog(log);

				foreach(QString jilei_type, jilei_list){
					room->removePlayerCardLimitation(player, "use,response,discard", jilei_type + "|.|.|hand$1");
					QString type_name = jilei_type.replace("Card", "").toLower();
					room->setPlayerMark(player, "@jilei_" + type_name, 0);
				}
				player->removeTag("jilei");
			}
		}

		return true;
	}
};

class Danlao : public TriggerSkillV2
{
public:
	Danlao() : TriggerSkillV2("danlao")
	{
		events << TargetConfirmed;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		CardUseStruct use = data.value<CardUseStruct>();
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || use.to.length() <= 1
			|| !use.to.contains(player) || !use.card || !use.card->isKindOf("TrickCard"))
			return TriggerList();
		return TriggerList{{player, QStringList{objectName()}}};
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		return room->askForSkillInvoke(player, objectName(), *ctx.original_data);
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		room->broadcastSkillInvoke(objectName());
		player->setFlags("-DanlaoTarget");
		player->setFlags("DanlaoTarget");
		player->drawCards(getEffectiveAmount(ctx), objectName());
		if (player->isAlive() && player->hasFlag("DanlaoTarget")){
			player->setFlags("-DanlaoTarget");
			use.nullified_list << player->objectName();
			*ctx.original_data = QVariant::fromValue(use);
		}
		return false;
	}
};

BifaCard::BifaCard()
{
    setSkillName("bifa");
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool BifaCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && to_select->getPile("bifa").isEmpty() && to_select != Self;
}

void BifaCard::use(Room*, ServerPlayer*source, QList<ServerPlayer*> &targets) const
{
	ServerPlayer*target = targets.first();
	target->setTag("BifaSource" + QString::number(getEffectiveId()), QVariant::fromValue(source));
	target->addToPile("bifa", this, false);
}

class BifaViewAsSkill : public ViewAsSkillV2
{
public:
    BifaViewAsSkill() : ViewAsSkillV2("bifa", 1) {}
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override { return isPromptRequest(request, "@@bifa"); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return ViewAsSkillV2::canSelectCard(request, card) && matchesFilter(request, card, ".|.|.|hand") && request.initiator->handCards().contains(card->getEffectiveId()); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    { return target && target->isAlive() && target != request.initiator && targets.isEmpty() && target->getPile(objectName()).isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "BifaCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int id = ctx.use_card->getSubcards().first();
        if (!ctx.invoker->handCards().contains(id) || !target->getPile(objectName()).isEmpty()) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        QVariantMap receipts = target->property("bifa_receipts").toMap();
        receipts.insert(QString::number(id), QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"giver", ctx.invoker->objectName()}, {"amount", getEffectiveAmount(ctx)}});
        room->setPlayerProperty(target, "bifa_receipts", receipts);
        target->setTag("BifaSource" + QString::number(id), QVariant::fromValue(ctx.invoker));
        target->addToPile(objectName(), id, false);
        return ContinueEffects;
    }
};

class Bifa : public TriggerSkillV2
{
public:
    Bifa() : TriggerSkillV2("bifa") { events << EventPhaseStart; view_as_skill = new BifaViewAsSkill; global = true; }
    int getEffectIndex(const ServerPlayer *, const Card *) const override { return 1; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish && !player->isKongcheng()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (!player || player->getPhase() != Player::RoundStart) return false;
        if (!player->isAlive() || player->getPile(objectName()).isEmpty()) return true;
        const int id = player->getPile(objectName()).first();
        const QVariantMap receipt = player->property("bifa_receipts").toMap().value(QString::number(id)).toMap();
        if (receipt.isEmpty()) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
        ctx.owner = room->findPlayerByObjectName(ctx.sourceRef.ownerObjectName, true);
        if (!ctx.owner) return true;
        ctx.invoker = ctx.initiator = player;
        ctx.targets = {player};
        ctx.amount = receipt.value("amount").toInt();
        ctx.choice = "settle";
        ctx.extra_data = QVariantMap{{"id", id}, {"receipt", receipt}};
        ctx.original_data = &data;
        ctx.current_event = event;
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.choice == "settle" ? ctx.invoker && ctx.invoker->isAlive() && ctx.invoker->getPile(objectName()).contains(ctx.extra_data.toMap().value("id").toInt())
            : TriggerSkillV2::isSourceAvailable(room, ctx);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice == "settle") return true;
        Room::BorrowedSkillScope prompt(room, ctx.owner, objectName(), ctx.activationRef);
        const CardUseStruct use = room->askForUseCardStruct(ctx.owner, "@@bifa", "@bifa-remove", -1, Card::MethodNone);
        if (!use.card || use.card->getSubcards().size() != 1 || use.to.size() != 1) return false;
        ctx.extra_data = QVariantMap{{"id", use.card->getSubcards().first()}, {"target", use.to.first()->objectName()}};
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice == "settle") return false;
        ctx.manual_effect = true;
        Room::AcceptedViewAsEffectScope accepted(room, ctx.owner, objectName(), ctx);
        if (!accepted.isValid()) return false;
        const QVariantMap selected = ctx.extra_data.toMap();
        ServerPlayer *target = room->findPlayerByObjectName(selected.value("target").toString());
        if (!target || !target->isAlive()) return false;
        ActiveSkillRequest request;
        request.initiator = ctx.owner;
        request.activationRef = accepted.activationRef();
        request.reason = CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
        request.pattern = "@@bifa";
        request.selectedCardIds = {selected.value("id").toInt()};
        request.selectedTargetNames = QStringList{target->objectName()};
        // Resume the selection through a real accepted use, preserving CardUsed consumers.
        const Card *card = room->resolveActiveSkillRequest(ctx.owner, static_cast<const ViewAsSkillV2 *>(view_as_skill), request);
        if (!card) return false;
        const_cast<Card *>(card)->deleteLater();
        const auto oldReason = room->getRoomState()->getCurrentCardUseReason();
        const QString oldPattern = room->getRoomState()->getCurrentCardUsePattern();
        const auto restore = qScopeGuard([&]() { room->setCurrentCardUse(oldPattern, oldReason); });
        room->setCurrentCardUse(request.pattern, request.reason);
        CardUseStruct use(card, ctx.owner, target);
        room->useCard(use);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap state = ctx.extra_data.toMap();
        const int id = state.value("id").toInt();
        if (!target->getPile(objectName()).contains(id)) return false;
        const Card *card = Sanguosha->getCard(id);
        ServerPlayer *giver = room->findPlayerByObjectName(state.value("receipt").toMap().value("giver").toString());
        LogMessage log; log.type = "$BifaView"; log.from = target; log.card_str = QString::number(id); log.arg = objectName(); room->sendLog(log, target);
        room->fillAG(QList<int>{id}, target);
        const auto clear = qScopeGuard([&]() { room->clearAG(target); });
        const QString type = card->isKindOf("BasicCard") ? "BasicCard" : card->isKindOf("TrickCard") ? "TrickCard" : "EquipCard";
        const Card *give = giver && giver->isAlive() && !target->isKongcheng()
            ? room->askForCard(target, type + "|.|.|hand", "@bifa-give", type, Card::MethodNone, giver) : nullptr;
        if (give && giver && giver->isAlive()) {
            room->broadcastSkillInvoke(objectName(), 2);
            room->obtainCard(giver, give, CardMoveReason(CardMoveReason::S_REASON_GIVE, target->objectName(), giver->objectName(), objectName(), ""), false);
            if (target->getPile(objectName()).contains(id)) room->obtainCard(target, card, CardMoveReason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, target->objectName(), objectName(), ""), false);
        } else {
            room->broadcastSkillInvoke(objectName(), 3);
            room->throwCard(card, CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", objectName(), ""), nullptr);
            room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), giver));
        }
        QVariantMap receipts = target->property("bifa_receipts").toMap(); receipts.remove(QString::number(id));
        room->setPlayerProperty(target, "bifa_receipts", receipts);
        target->removeTag("BifaSource" + QString::number(id));
        return false;
    }
};

SongciCard::SongciCard()
{
    setSkillName("songci");
	mute = true;
}

bool SongciCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && to_select->getMark("songci" + Self->objectName()) == 0 && to_select->getHandcardNum() != to_select->getHp();
}

void SongciCard::onEffect(CardEffectStruct &effect) const
{
	int handcard_num = effect.to->getHandcardNum();
	int hp = effect.to->getHp();
	Room*room = effect.from->getRoom();
	room->setPlayerMark(effect.to, "@songci", 1);
	room->addPlayerMark(effect.to, "songci" + effect.from->objectName());
	if (handcard_num > hp){
		room->broadcastSkillInvoke("songci", 2);
		room->askForDiscard(effect.to, "songci", 2, 2, false, true);
	} else if (handcard_num < hp){
		room->broadcastSkillInvoke("songci", 1);
		effect.to->drawCards(2, "songci");
	}
}

class SongciViewAsSkill : public ViewAsSkillV2
{
public:
    SongciViewAsSkill() : ViewAsSkillV2("songci") { m_baseAmount = 2; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    {
        return request.initiator && target && target->isAlive() && targets.isEmpty() && target->getHandcardNum() != target->getHp()
            && !request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "recipients").toStringList().contains(target->objectName());
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "SongciCard"; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (ctx.targets.size() != 1 || !ViewAsSkillV2::pay(room, ctx, request)) return false;
        QStringList recipients = ctx.invoker->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "recipients").toStringList();
        if (recipients.contains(ctx.targets.first()->objectName())) return false;
        recipients << ctx.targets.first()->objectName();
        ctx.invoker->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "recipients", recipients);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        room->setPlayerMark(target, "@songci", 1);
        room->addPlayerMark(target, "songci" + ctx.invoker->objectName());
        if (target->getHandcardNum() > target->getHp()) {
            room->broadcastSkillInvoke(objectName(), 2);
            room->askForDiscard(target, objectName(), getEffectiveAmount(ctx), getEffectiveAmount(ctx), false, true);
        } else if (target->getHandcardNum() < target->getHp()) {
            room->broadcastSkillInvoke(objectName(), 1);
            target->drawCards(getEffectiveAmount(ctx), objectName());
        }
        return ContinueEffects;
    }
};

class Songci : public TriggerSkillV2
{
public:
	Songci() : TriggerSkillV2("songci")
	{
		events << Death;
		view_as_skill = new SongciViewAsSkill;
	}

	// Chen Lin's own death clears the per-source marks; nothing is invoked.
	bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return true;
		DeathStruct death = data.value<DeathStruct>();
		if (death.who != player) return true;
        for (int id : player->getSkillInstanceIds(objectName())) player->removeSkillInstanceStateValue(objectName(), id, "recipients");
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (p->getMark("@songci") > 0)
				room->setPlayerMark(p, "@songci", 0);
			if (p->getMark("songci" + player->objectName()) > 0)
				room->setPlayerMark(p, "songci" + player->objectName(), 0);
		}
		return true;
	}
};

YinbingCard::YinbingCard()
{
    setSkillName("yinbing");
	will_throw = false;
	target_fixed = true;
	handling_method = Card::MethodNone;
}

void YinbingCard::use(Room*, ServerPlayer*source, QList<ServerPlayer*> &) const
{
	source->addToPile("yinbing", this);
}

class YinbingViewAsSkill : public ViewAsSkillV2
{
public:
    YinbingViewAsSkill() : ViewAsSkillV2("yinbing") {}
    TargetMode targetMode() const override { return NoTarget; }
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override { return isPromptRequest(request, "@@yinbing"); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return matchesFilter(request, card, ".") && card->getTypeId() != Card::TypeBasic
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->hasEquip(card));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return !request.selectedCardIds.isEmpty() && replaySelection(this, request); }
    QString historyKey(const ActiveSkillRequest &) const override { return "YinbingCard"; }
};

class Yinbing : public TriggerSkillV2
{
public:
    Yinbing() : TriggerSkillV2("yinbing") { events << EventPhaseStart << Damaged; view_as_skill = new YinbingViewAsSkill; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Finish || player->isNude()) return {};
        } else {
            const DamageStruct damage = data.value<DamageStruct>();
            if (player->getPile(objectName()).isEmpty() || !damage.card
                || !(damage.card->isKindOf("Slash") || damage.card->isKindOf("Duel"))) return {};
        }
        return TriggerList{{player, {objectName()}}};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            Room::BorrowedSkillScope source(room, ctx.owner, objectName(), ctx.activationRef);
            const CardUseStruct use = room->askForUseCardStruct(ctx.owner, "@@yinbing", "@yinbing", -1, Card::MethodNone);
            if (!use.card || use.card->getSubcards().isEmpty()) return false;
            ctx.extra_data = ListI2V(use.card->getSubcards());
        } else {
            const QList<int> ids = ctx.owner->getPile(objectName());
            room->fillAG(ids, ctx.owner);
            const auto clear = qScopeGuard([&]() { room->clearAG(ctx.owner); });
            const int id = room->askForAG(ctx.owner, ids, false, objectName());
            if (!ids.contains(id)) return false;
            ctx.extra_data = id;
        }
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseStart) {
            QList<int> ids;
            for (int id : ListV2I(ctx.extra_data.toList()))
                if (room->getCardOwner(id) == ctx.owner && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) ids << id;
            if (!ids.isEmpty()) ctx.owner->addToPile(objectName(), ids);
        } else if (ctx.owner->getPile(objectName()).contains(ctx.extra_data.toInt())) {
            room->throwCard(ctx.extra_data.toInt(), nullptr);
        }
        return false;
    }
};

class Juedi : public TriggerSkillV2
{
public:
    Juedi() : TriggerSkillV2("juedi") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Start
        && !player->getPile("yinbing").isEmpty() ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room->askForSkillInvoke(ctx.owner, objectName())) return false;
        QList<ServerPlayer *> recipients;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner)) if (other->getHp() <= ctx.owner->getHp()) recipients << other;
        ServerPlayer *target = recipients.isEmpty() ? nullptr : room->askForPlayerChosen(ctx.owner, recipients, objectName(), "@juedi", true);
        ctx.choice = target ? "give" : "draw";
        ctx.targets = {target ? target : ctx.owner};
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice == "draw") {
            ctx.extra_data = ctx.owner->getPile("yinbing").size();
            ctx.owner->clearOnePrivatePile("yinbing");
        }
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        if (ctx.choice == "draw") target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName());
        else {
            room->recover(target, RecoverStruct(ctx.owner, nullptr, getEffectiveAmount(ctx), objectName()));
            const QList<int> ids = ctx.owner->getPile("yinbing");
            if (!ids.isEmpty()) { DummyCard gift(ids); room->obtainCard(target, &gift); }
        }
        return false;
    }
};

class SpZhenwei : public TriggerSkillV2
{
public:
    SpZhenwei() : TriggerSkillV2("spzhenwei")
    { events << EventSkillInvoking << TargetConfirming << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            SkillContext accepted = data.value<SkillContext>();
            if (accepted.bypass_cost && accepted.skill_name == objectName() && accepted.choice == "return") {
                // Retiring an accepted receipt is bookkeeping, never a bypassable resource cost.
                if (!pay(accepted.current_event, room, accepted.invoker, accepted)) accepted.is_canceled = true;
                data.setValue(accepted);
            }
            return true;
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event == EventSkillInvoking) return true;
        if (event != EventPhaseChanging) return false;
        if (data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        for (ServerPlayer *target : room->getAlivePlayers()) {
            const QVariantMap receipts = target->property("spzhenwei_receipts").toMap();
            for (auto it = receipts.cbegin(); it != receipts.cend(); ++it) {
                const QVariantMap receipt = it.value().toMap();
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(),
                    SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
                ctx.owner = room->findPlayerByObjectName(ctx.sourceRef.ownerObjectName, true);
                if (!ctx.owner) continue;
                ctx.invoker = ctx.initiator = target;
                ctx.targets = {target};
                ctx.choice = "return";
                ctx.is_forced = true;
                ctx.extra_data = QVariantMap{{"key", it.key()}, {"receipt", receipt}};
                ctx.amount = getBaseAmount();
                ctx.current_event = event;
                ctx.original_data = &data;
                contexts << ctx;
            }
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.choice == "return")
            return ctx.invoker && ctx.sourceRef.isValid()
                && !ctx.extra_data.toMap().value("receipt").toMap().isEmpty();
        return TriggerSkillV2::isSourceAvailable(room, ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetConfirming || !player || !player->isAlive()) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.to.size() != 1 || !use.to.contains(player)
            || !(use.card->isKindOf("Slash") || (use.card->getTypeId() == Card::TypeTrick && use.card->isBlack()))) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner->getHp() > player->getHp() && owner->canDiscard(owner, "he")) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice == "return") return true;
        const Card *card = room->askForCard(ctx.owner, "..", "@sp_zhenwei:" + ctx.invoker->objectName(),
            *ctx.original_data, Card::MethodNone, nullptr, false, objectName());
        if (!card || card->isVirtualCard() || !ctx.owner->canDiscard(ctx.owner, card->getEffectiveId())) return false;
        ctx.extra_data = card->getEffectiveId();
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice == "return") {
            QVariantMap receipts = ctx.invoker->property("spzhenwei_receipts").toMap();
            const QVariant receipt = receipts.take(ctx.extra_data.toMap().value("key").toString());
            if (!receipt.isValid()) return false;
            room->setPlayerProperty(ctx.invoker, "spzhenwei_receipts", receipts);
            ctx.extra_data = receipt;
            return true;
        }
        const int id = ctx.extra_data.toInt();
        if (room->getCardOwner(id) != ctx.owner || !ctx.owner->canDiscard(ctx.owner, id)) return false;
        room->throwCard(id, objectName(), ctx.owner);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (ctx.choice == "return") return false;
        ctx.manual_effect = true;
        ctx.choice = "approve";
        ctx.extra_data = false;
        skillEffect(event, room, player, ctx, ctx.invoker);
        if (!ctx.extra_data.toBool() || !ctx.owner->isAlive()) return false;
        room->broadcastSkillInvoke(objectName());
        ctx.choice = room->askForChoice(ctx.owner, objectName(), "draw+null", *ctx.original_data);
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ServerPlayer *recipient = ctx.choice == "draw" ? ctx.owner : use.from;
        if (recipient) skillEffect(event, room, player, ctx, recipient);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "approve") { ctx.extra_data = true; return false; }
        if (ctx.choice == "return") {
            // Consume this accepted duration before obtaining cards, so nested moves cannot return it twice.
            DummyCard cards;
            for (const QVariant &value : ctx.extra_data.toMap().value("cards").toList())
                if (target->getPile("zhenweipile").contains(value.toInt())) cards.addSubcard(value.toInt());
            if (cards.subcardsLength() > 0) room->obtainCard(target, &cards);
            return false;
        }
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.from) return false;
        if (ctx.choice == "draw") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            if (!target->isAlive() || use.from->isProhibited(target, use.card)
                || (use.card->isKindOf("Slash") && !use.from->canSlash(target, use.card, false))) return false;
            if (use.card->isKindOf("DelayedTrick")) room->moveCardTo(use.card, target, Player::PlaceDelayedTrick, true);
            else { use.to = {target}; *ctx.original_data = QVariant::fromValue(use); }
        } else {
            const qint64 serial = room->getTag("SpZhenweiReceiptSerial").toLongLong() + 1;
            room->setTag("SpZhenweiReceiptSerial", serial);
            QVariantList ids;
            const QList<int> materials = use.card->isVirtualCard() ? use.card->getSubcards() : QList<int>{use.card->getEffectiveId()};
            for (int id : materials) if (id >= 0) ids << id;
            QVariantMap receipts = target->property("spzhenwei_receipts").toMap();
            receipts[QString::number(serial)] = QVariantMap{{"cards", ids}, {"owner", ctx.sourceRef.ownerObjectName},
                {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}};
            room->setPlayerProperty(target, "spzhenwei_receipts", receipts);
            room->setCardFlag(use.card, "zhenweinull");
            target->addToPile("zhenweipile", use.card);
            use.nullified_list << "_ALL_TARGETS";
            *ctx.original_data = QVariant::fromValue(use);
        }
        return false;
    }
};

class AocaiViewAsSkill : public ViewAsSkillV2
{
public:
    AocaiViewAsSkill() : ViewAsSkillV2("aocai") { m_baseAmount = 2; }
    static Card *preview(const ActiveSkillRequest &request)
    {
        if (!request.initiator) return nullptr;
        if (request.pattern == "@@aocai") {
            const QVariantMap receipt = request.initiator->property("aocai_selection").toMap();
            if (receipt.isEmpty()) return nullptr;
            const Card *revealed = Sanguosha->getCard(receipt.value("id").toInt());
            if (!revealed) return nullptr;
            Card *card = Sanguosha->cloneCard(revealed->objectName(), revealed->getSuit(), revealed->getNumber());
            if (card) card->addSubcard(revealed);
            return card;
        }
        for (const QString &name : request.pattern.split('+')) {
            Card *card = Sanguosha->cloneCard(name);
            if (!card) continue;
            const Card::HandlingMethod method = request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                ? Card::MethodResponse : Card::MethodUse;
            if (card->getTypeId() == Card::TypeBasic && !request.initiator->isCardLimited(card, method)
                && !(card->isKindOf("Peach") && request.initiator->hasFlag("Global_PreventPeach"))) return card;
            delete card;
        }
        return nullptr;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.initiator->hasFlag("CurrentPlayer")) return false;
        if (request.pattern == "@@aocai") return !request.initiator->property("aocai_selection").toMap().isEmpty();
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return true;
        if (request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "failed").toBool()) return false;
        Card *card = preview(request);
        const bool valid = card != nullptr;
        delete card;
        return valid;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    {
        Card *card = preview(request);
        const bool valid = card && target && card->targetFilter(targets, target, request.initiator)
            && !request.initiator->isProhibited(target, card, targets);
        delete card;
        return valid;
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.pattern != "@@aocai") return targets.isEmpty();
        Card *card = preview(request);
        const bool valid = card && card->targetsFeasible(targets, request.initiator);
        delete card;
        return valid;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !cardSelectionFeasible(request)) return nullptr;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY || request.pattern == "@@aocai") {
            AocaiCard *proxy = new AocaiCard;
            proxy->setActiveSkill(this);
            if (request.pattern == "@@aocai") {
                Card *selected = preview(request);
                if (!selected) { delete proxy; return nullptr; }
                proxy->setUserString(selected->objectName());
                delete selected;
            }
            proxy->setTag("v2_effect_input", QVariantMap{{"pattern", request.pattern}, {"reason", int(request.reason)}});
            return proxy;
        }
        Card *card = preview(request);
        if (card) {
            card->setSkillName(objectName());
            card->setTag("v2_effect_input", QVariantMap{{"pattern", request.pattern}, {"reason", int(request.reason)}});
        }
        return card;
    }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        ctx.extra_data = QVariantMap{{"pattern", request.pattern}, {"reason", int(request.reason)}};
        return canActivate(request) && cardSelectionFeasible(request);
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "AocaiCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.invoker;
        if (!player || !player->isAlive()) return FinishSkill;
        Room *room = player->getRoom();
        const QVariantMap state = (ctx.extra_data.isValid() ? ctx.extra_data : ctx.use_card->getTag("v2_effect_input")).toMap();
        const QString pattern = state.value("pattern").toString();
        if (pattern == "@@aocai") return ContinueEffects;
        const auto reason = CardUseStruct::CardUseReason(state.value("reason").toInt());
        const bool play = reason == CardUseStruct::CARD_USE_REASON_PLAY;
        const Card::HandlingMethod method = reason == CardUseStruct::CARD_USE_REASON_RESPONSE ? Card::MethodResponse : Card::MethodUse;
        const QList<int> revealed = room->getNCards(qMax(0, getEffectiveAmount(ctx)));
        QList<int> enabled, disabled;
        // Only these exact cards belong to this reveal. Restore their order even on cancellation.
        QList<int> pending = revealed;
        const auto restorePile = qScopeGuard([&]() {
            QList<int> remaining;
            for (int id : pending)
                if (!room->getCardOwner(id) && room->getCardPlace(id) == Player::DrawPile) remaining << id;
            if (!remaining.isEmpty()) room->returnToTopDrawPile(remaining);
        });
        for (int id : revealed) {
            const Card *card = Sanguosha->getCard(id);
            const bool matches = card && card->getTypeId() == Card::TypeBasic
                && (play ? card->isAvailable(player) : Sanguosha->matchPattern(pattern, player, card));
            if (matches && !player->isCardLimited(card, method)
                && !(card->isKindOf("Peach") && player->hasFlag("Global_PreventPeach"))) enabled << id;
            else disabled << id;
        }
        LogMessage log;
        log.type = "$ViewDrawPile"; log.from = player; log.card_str = ListI2S(revealed).join("+");
        room->sendLog(log, player);
        int chosen = -1;
        if (enabled.isEmpty()) {
            JsonArray args; args << "." << false << JsonUtils::toJsonArray(revealed);
            room->doNotify(player, QSanProtocol::S_COMMAND_SHOW_ALL_CARDS, args);
        } else {
            room->fillAG(revealed, player, disabled);
            const auto clearAG = qScopeGuard([&]() { room->clearAG(player); });
            chosen = room->askForAG(player, enabled, true, objectName());
        }
        if (!enabled.contains(chosen) || room->getCardOwner(chosen) || room->getCardPlace(chosen) != Player::DrawPile) {
            player->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "failed", true);
            room->setPlayerFlag(player, "Global_AocaiFailed");
            return FinishSkill;
        }
        const Card *physical = Sanguosha->getCard(chosen);
        Card *card = Sanguosha->cloneCard(physical->objectName(), physical->getSuit(), physical->getNumber());
        if (!card) return FinishSkill;
        card->addSubcard(chosen); card->setSkillName(objectName()); card->deleteLater();
        log = LogMessage(); log.type = "#AocaiUse"; log.from = player;
        log.arg = objectName(); log.arg2 = QString::number(revealed.indexOf(chosen) + 1); room->sendLog(log);
        // Put the reserved cards back before ordinary movement removes the chosen id.
        // A canceled downstream use therefore cannot strand a card outside the draw list.
        room->returnToTopDrawPile(pending);
        pending.clear();
        if (!play) {
            if (ctx.original_data && ctx.original_data->canConvert<CardUseStruct>()) {
                CardUseStruct use = ctx.original_data->value<CardUseStruct>();
                use.m_isOwnerUse = false;
                *ctx.original_data = QVariant::fromValue(use);
            }
            ctx.updated_card = card;
            return ContinueEffects;
        }
        const QVariant oldSelection = player->property("aocai_selection");
        const int oldId = player->getMark("aocaiId");
        room->setPlayerProperty(player, "aocai_selection", QVariantMap{{"id", chosen}});
        room->setPlayerMark(player, "aocaiId", chosen);
        const auto restoreSelection = qScopeGuard([&]() {
            room->setPlayerProperty(player, "aocai_selection", oldSelection);
            room->setPlayerMark(player, "aocaiId", oldId);
        });
        Room::AcceptedViewAsEffectScope accepted(room, player, objectName(), ctx);
        if (!accepted.isValid()) return FinishSkill;
        const CardUseStruct selected = room->askForUseCardStruct(player, "@@aocai", "aocai0:" + card->objectName(), -1, Card::MethodNone);
        if (!selected.card || !selected.card->getSubcards().isEmpty()
            || room->getCardOwner(chosen) || room->getCardPlace(chosen) != Player::DrawPile) return FinishSkill;
        CardUseStruct use(card, player, selected.to);
        use.m_isOwnerUse = false;
        room->useCardFromSkillEffect(use, ctx, true);
        return FinishSkill;
    }
};

class Aocai : public TriggerSkillV2
{
public:
    Aocai() : TriggerSkillV2("aocai") { events << CardUsed; global = true; view_as_skill = new AocaiViewAsSkill; }
    bool collectTriggerContexts(TriggerEvent, Room *, ServerPlayer *, QVariant &, QList<SkillContext> &) const override { return true; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        // Any actual card use opens a new failure window for every concrete copy.
        for (ServerPlayer *player : room->getPlayers()) {
            for (int id : player->getSkillInstanceIds(objectName()))
                player->setSkillInstanceStateValue(objectName(), id, "failed", false);
            room->setPlayerFlag(player, "-Global_AocaiFailed");
        }
        return true;
    }
};

AocaiCard::AocaiCard()
{
    setSkillName("aocai");
}

bool AocaiCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card){
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	return false;
}

bool AocaiCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;

	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if (card){
		card->deleteLater();
		return card->targetFixed();
	}
	return true;
}

bool AocaiCard::targetsFeasible(const QList<const Player*> &targets, const Player*Self) const
{
	Card*card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	return true;
}

DuwuCard::DuwuCard()
{
    setSkillName("duwu");
	mute = true;
}

bool DuwuCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*Self) const
{
	return targets.isEmpty() && qMax(0, to_select->getHp()) == subcardsLength() && Self->inMyAttackRange(to_select, subcards);
}

void DuwuCard::onEffect(CardEffectStruct &effect) const
{
	Room*room = effect.to->getRoom();

	if (subcards.length() <= 1)
		room->broadcastSkillInvoke("duwu", 2);
	else
		room->broadcastSkillInvoke("duwu", 1);

	room->damage(DamageStruct("duwu", effect.from, effect.to));
}

class DuwuViewAsSkill : public ViewAsSkillV2
{
public:
    DuwuViewAsSkill() : ViewAsSkillV2("duwu") {}
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.activationRef.isValid()) return false;
        const qint64 phase = ctx.invoker->getRoom()->historyScopes().value("phase_id").toLongLong();
        return phase > 0 && ctx.invoker->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "blocked_phase").toLongLong() != phase;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->canDiscard(request.initiator, "he"); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return matchesFilter(request, card, ".") && !request.initiator->isJilei(card)
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->hasEquip(card));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return request.initiator && replaySelection(this, request); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    {
        return target && target->isAlive() && targets.isEmpty() && qMax(0, target->getHp()) == request.selectedCardIds.size()
            && request.initiator->inMyAttackRange(target, request.selectedCardIds);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "DuwuCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        const QVariant previous = ctx.invoker->property("duwu_pending_receipts");
        QVariantList pending = previous.toList();
        pending << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"activation", ctx.activationRef.key.instanceID},
            {"phase", room->historyScopes().value("phase_id")}, {"amount", getEffectiveAmount(ctx)}};
        room->setPlayerProperty(ctx.invoker, "duwu_pending_receipts", pending);
        // Damage and its dying callbacks are synchronous; nested Duwu restores the outer receipt.
        const auto restore = qScopeGuard([&]() { room->setPlayerProperty(ctx.invoker, "duwu_pending_receipts", previous); });
        room->broadcastSkillInvoke(objectName(), ctx.use_card->subcardsLength() <= 1 ? 2 : 1);
        room->damage(DamageStruct(objectName(), ctx.invoker, target, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }
};

class Duwu : public TriggerSkillV2
{
public:
    Duwu() : TriggerSkillV2("duwu") { events << QuitDying; view_as_skill = new DuwuViewAsSkill; global = true; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &contexts) const override
    {
        const DyingStruct dying = data.value<DyingStruct>();
        if (!dying.damage || dying.damage->getReason() != objectName() || dying.damage->chain || dying.damage->transfer) return true;
        ServerPlayer *actor = dying.damage->from;
        if (!actor || !actor->isAlive()) return true;
        const QVariantList pending = actor->property("duwu_pending_receipts").toList();
        if (pending.isEmpty()) return true;
        const QVariantMap receipt = pending.last().toMap();
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
        ctx.owner = room->findPlayerByObjectName(ctx.sourceRef.ownerObjectName, true);
        if (!ctx.owner) return true;
        ctx.invoker = ctx.initiator = actor;
        ctx.targets = {actor};
        ctx.amount = receipt.value("amount").toInt();
        ctx.extra_data = receipt;
        ctx.original_data = &data;
        ctx.current_event = event;
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->isAlive() && !ctx.invoker->property("duwu_pending_receipts").toList().isEmpty(); }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QVariantMap receipt = ctx.extra_data.toMap();
        const int activation = receipt.value("activation").toInt();
        if (ctx.invoker->hasSkillInstance(objectName(), activation))
            ctx.invoker->setSkillInstanceStateValue(objectName(), activation, "blocked_phase", receipt.value("phase"));
        room->setPlayerFlag(ctx.invoker, "DuwuEnterDying");
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        return false;
    }
};

YuanhuCard::YuanhuCard()
{
    setSkillName("yuanhu");
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool YuanhuCard::targetFilter(const QList<const Player*> &targets, const Player*to_select, const Player*) const
{
	if (!targets.isEmpty())
		return false;

	const Card*card = Sanguosha->getCard(subcards.first());
	const EquipCard*equip = qobject_cast<const EquipCard*>(card->getRealCard());
	return to_select->getEquip(equip->location()) == nullptr;
}

void YuanhuCard::onUse(Room*room, CardUseStruct &card_use) const
{
	int index = -1;
	if (card_use.to.first() == card_use.from)
		index = 5;
	else if (card_use.to.first()->getGeneralName().contains("caocao"))
		index = 4;
	else {
		const Card*card = Sanguosha->getCard(card_use.card->getSubcards().first());
		if (card->isKindOf("Weapon"))
			index = 1;
		else if (card->isKindOf("Armor"))
			index = 2;
		else if (card->isKindOf("Horse"))
			index = 3;
	}
	room->broadcastSkillInvoke("yuanhu", index);
	SkillCard::onUse(room, card_use);
}

void YuanhuCard::onEffect(CardEffectStruct &effect) const
{
	ServerPlayer*caohong = effect.from;
	Room*room = caohong->getRoom();
	room->moveCardTo(this, caohong, effect.to, Player::PlaceEquip,
		CardMoveReason(CardMoveReason::S_REASON_PUT, caohong->objectName(), "yuanhu", ""));

	const Card*card = Sanguosha->getCard(subcards.first());

	LogMessage log;
	log.type = "$ZhijianEquip";
	log.from = effect.to;
	log.card_str = QString::number(card->getEffectiveId());
	room->sendLog(log);

	if (card->isKindOf("Weapon")){
		QList<ServerPlayer*> targets;
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (effect.to->distanceTo(p) == 1 && caohong->canDiscard(p, "hej"))
				targets << p;
		}
		if (!targets.isEmpty()){
			ServerPlayer*to_dismantle = room->askForPlayerChosen(caohong, targets, "yuanhu", "@yuanhu-discard:" + effect.to->objectName());
			int card_id = room->askForCardChosen(caohong, to_dismantle, "hej", "yuanhu", false, Card::MethodDiscard);
			room->throwCard(card_id, to_dismantle, caohong);
		}
	} else if (card->isKindOf("Armor")){
		effect.to->drawCards(1, "yuanhu");
	} else if (card->isKindOf("Horse")){
		room->recover(effect.to, RecoverStruct("yuanhu", effect.from));
	}
}

class YuanhuViewAsSkill : public ViewAsSkillV2
{
public:
    YuanhuViewAsSkill() : ViewAsSkillV2("yuanhu", 1) {}
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override { return isPromptRequest(request, "@@yuanhu"); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ViewAsSkillV2::canSelectCard(request, card) && matchesFilter(request, card, "EquipCard")
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->hasEquip(card));
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    {
        if (!target || !target->isAlive() || !targets.isEmpty() || request.selectedCardIds.size() != 1) return false;
        const auto *equip = qobject_cast<const EquipCard *>(Sanguosha->getCard(request.selectedCardIds.first())->getRealCard());
        return equip && !target->getEquip(equip->location());
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "YuanhuCard"; }
};

class Yuanhu : public TriggerSkillV2
{
public:
    Yuanhu() : TriggerSkillV2("yuanhu") { events << EventPhaseStart; view_as_skill = new YuanhuViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish && !player->isNude()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        Room::BorrowedSkillScope source(room, ctx.owner, objectName(), ctx.activationRef);
        const CardUseStruct use = room->askForUseCardStruct(ctx.owner, "@@yuanhu", "@yuanhu-equip", -1, Card::MethodNone);
        if (!use.card || use.card->getSubcards().size() != 1 || use.to.size() != 1) return false;
        ctx.extra_data = use.card->getSubcards().first();
        ctx.targets = use.to;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "discard") {
            for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->isAlive() && target->isAlive() && ctx.owner->canDiscard(target, "hej"); ++i) {
                const int id = room->askForCardChosen(ctx.owner, target, "hej", objectName(), false, Card::MethodDiscard);
                room->throwCard(id, target, ctx.owner);
            }
            return false;
        }
        const int id = ctx.extra_data.toInt();
        const Card *card = Sanguosha->getCard(id);
        const auto *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        if (!equip || room->getCardOwner(id) != ctx.owner || target->getEquip(equip->location())) return false;
        const bool weapon = card->isKindOf("Weapon"), armor = card->isKindOf("Armor"), horse = card->isKindOf("Horse");
        const int audio = target == ctx.owner ? 5 : target->getGeneralName().contains("caocao") ? 4 : weapon ? 1 : armor ? 2 : horse ? 3 : -1;
        room->broadcastSkillInvoke(objectName(), audio);
        room->moveCardTo(card, ctx.owner, target, Player::PlaceEquip, CardMoveReason(CardMoveReason::S_REASON_PUT, ctx.owner->objectName(), objectName(), ""));
        if (!target->isAlive() || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceEquip) return false;
        LogMessage log; log.type = "$ZhijianEquip"; log.from = target; log.card_str = QString::number(id); room->sendLog(log);
        if (weapon && ctx.owner->isAlive()) {
            QList<ServerPlayer *> targets;
            for (ServerPlayer *candidate : room->getAlivePlayers())
                if (target->distanceTo(candidate) == 1 && ctx.owner->canDiscard(candidate, "hej")) targets << candidate;
            if (!targets.isEmpty()) {
                ServerPlayer *discard = room->askForPlayerChosen(ctx.owner, targets, objectName(), "@yuanhu-discard:" + target->objectName());
                ctx.choice = "discard";
                if (discard) skillEffect(event, room, player, ctx, discard);
                ctx.choice.clear();
            }
        } else if (armor) target->drawCards(getEffectiveAmount(ctx), objectName());
        else if (horse) { RecoverStruct recover(objectName(), ctx.owner); recover.recover = getEffectiveAmount(ctx); room->recover(target, recover); }
        return false;
    }
};

class Baobian : public TriggerSkillV2
{
public:
    Baobian() : TriggerSkillV2("baobian")
    { events << GameStart << HpChanged << MaxHpChanged << EventAcquireSkill; frequency = Compulsory; }
    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (!ctx.owner || ctx.owner != player || !player->isAlive() || !ctx.activationRef.isValid()) return;
        if (event == EventAcquireSkill && (!ctx.original_data || ctx.original_data->value<SkillChangeStruct>().skillName != objectName())) return;
        const QStringList names{QStringLiteral("shensu"), QStringLiteral("paoxiao"), QStringLiteral("tiaoxin")};
        for (int i = 0; i < names.size(); ++i) {
            const QString name = names.at(i);
            const bool active = player->getHp() <= i + 1;
            bool attached = false;
            for (const SkillInstance &entry : player->getSkillInstances()) {
                if (entry.skillName != name || entry.parentRef != ctx.activationRef) continue;
                attached = true;
                if (!active) room->detachAttachedSkill(SkillInstanceRef(player->objectName(), entry.key()));
            }
            // Parent retirement handles source loss without touching any native copy.
            if (active && !attached) {
                room->attachSkillToPlayer(player, name, ctx.activationRef);
                room->notifySkillInvoked(player, objectName());
                if (player->getHp() == i + 1) room->broadcastSkillInvoke(objectName(), 3 - i);
            }
        }
    }
};

SPPackage::SPPackage()
: Package("sp")
{
    // Shared skill restored from the retired identity hegemony package.
    addSkills(new Lirang);
    addSkills(new Xiongyi);
    addSkills(new Sijian);
    addSkills(new Shuangren);
    addSkills(new ShuangrenTargetMod);
    insertRelatedSkills("shuangren", "#shuangren-slash-ndl");

    General *yangxiu = new General(this, "yangxiu*xh_sibi", "wei", 3); // SP 001
    yangxiu->addSkill(new Jilei);
    yangxiu->addSkill(new JileiClear);
    yangxiu->addSkill(new Danlao);
    related_skills.insert("jilei", "#jilei-clear");

    General *sp_diaochan = new General(this, "sp_diaochan", "qun", 3, false); // SP 002
    sp_diaochan->addSkill("noslijian");
    sp_diaochan->addSkill("biyue");

    General *gongsunzan = new General(this, "gongsunzan", "qun"); // SP 003
    gongsunzan->addSkill(new Yicong);
    gongsunzan->addSkill(new YicongEffect);
    related_skills.insert("yicong", "#yicong-effect");

    General *yuanshu = new General(this, "yuanshu", "qun"); // SP 004
    yuanshu->addSkill(new Yongsi);
    yuanshu->addSkill(new Weidi);

    General *sp_sunshangxiang = new General(this, "sp_sunshangxiang", "shu", 3, false); // SP 005
    sp_sunshangxiang->addSkill("jieyin");
    sp_sunshangxiang->addSkill("xiaoji");

    General *sp_pangde = new General(this, "sp_pangde", "wei", 4, true); // SP 006
    sp_pangde->addSkill("mashu");
    sp_pangde->addSkill("mengjin");

    General *sp_guanyu = new General(this, "sp_guanyu", "wei", 4); // SP 007
    sp_guanyu->addSkill("wusheng");
    sp_guanyu->addSkill(new Danji);

    General *sp_caiwenji = new General(this, "sp_caiwenji", "wei", 3, false); // SP 009
    sp_caiwenji->addSkill("beige");
    sp_caiwenji->addSkill("duanchang");

    General *sp_machao = new General(this, "sp_machao", "qun", 4, true); // SP 011
    sp_machao->addSkill("mashu");
    sp_machao->addSkill("nostieji");

    General *sp_jiaxu = new General(this, "sp_jiaxu", "wei", 3, true); // SP 012
    sp_jiaxu->addSkill("wansha");
    sp_jiaxu->addSkill("luanwu");
    sp_jiaxu->addSkill("weimu");

    General *caohong = new General(this, "caohong*xh_huben", "wei"); // SP 013
    caohong->addSkill(new Yuanhu);
    addMetaObject<YuanhuCard>();

    General *nos_guanyinping = new General(this, "nos_guanyinping", "shu", 3, false); // SP 014
    nos_guanyinping->addSkill(new Xueji);
    nos_guanyinping->addSkill(new Huxiao);
    nos_guanyinping->addSkill(new HuxiaoCount);
    nos_guanyinping->addSkill(new Wuji);
    related_skills.insert("huxiao", "#huxiao-count");
    addMetaObject<XuejiCard>();

    General *new_guanyinping = new General(this, "new_guanyinping", "shu", 3, false);
    new_guanyinping->addSkill(new Newxuehen);
    new_guanyinping->addSkill(new NewHuxiao);
    new_guanyinping->addSkill(new NewHuxiaoTargetMod);
    new_guanyinping->addSkill(new NewWuji);
    related_skills.insert("newhuxiao", "#newhuxiao-target");
    addMetaObject<NewxuehenCard>();

    General *sp_zhenji = new General(this, "sp_zhenji", "wei", 3, false); // SP 015
    sp_zhenji->addSkill("qingguo");
    sp_zhenji->addSkill("luoshen");

    General *liuxie = new General(this, "liuxie*xh_tianji", "qun", 3); // SP 016
    liuxie->addSkill("tianming");
    liuxie->addSkill("mizhao");

    General *xiahouba = new General(this, "xiahouba*xh_huben", "shu"); // SP 019
    xiahouba->addSkill(new Baobian);
    xiahouba->addRelateSkill("tiaoxin");
    xiahouba->addRelateSkill("paoxiao");
    xiahouba->addRelateSkill("shensu");

    General *chenlin = new General(this, "chenlin*xh_sibi", "wei", 3); // SP 020
    chenlin->addSkill(new Bifa);
    chenlin->addSkill(new Songci);
    addMetaObject<BifaCard>();
    addMetaObject<SongciCard>();

    General *erqiao = new General(this, "erqiao", "wu", 3, false); // SP 021
    erqiao->addSkill(new Xingwu);
    erqiao->addSkill(new Luoyan("luoyan"));
    skills << new Luoyan("olluoyan");

    General *sp_shenlvbu = new General(this, "sp_shenlvbu", "god", 5, true); // SP 022
    sp_shenlvbu->addSkill("kuangbao");
    sp_shenlvbu->addSkill("wumou");
    sp_shenlvbu->addSkill("wuqian");
    sp_shenlvbu->addSkill("shenfen");

    General *xiahoushi = new General(this, "xiahoushi", "shu", 3, false); // SP 023
    xiahoushi->addSkill(new Yanyu);
    xiahoushi->addSkill(new Xiaode);
    xiahoushi->addSkill(new XiaodeEx);
    related_skills.insert("xiaode", "#xiaode");

    General *sp_yuejin = new General(this, "sp_yuejin*xh_huben", "wei", 4, true); // SP 024
    sp_yuejin->addSkill(new Xiaoguo);

    General *zhangbao = new General(this, "zhangbao", "qun", 3); // SP 025
    zhangbao->addSkill(new Zhoufu);
    zhangbao->addSkill(new Yingbing);
    addMetaObject<ZhoufuCard>();

    General *caoang = new General(this, "caoang*xh_tianji", "wei"); // SP 026
    caoang->addSkill(new Kangkai);

    General *sp_zhugejin = new General(this, "sp_zhugejin*xh_sibi", "wu", 3, true); // SP 027
    sp_zhugejin->addSkill("hongyuan");
    sp_zhugejin->addSkill("huanshi");
    sp_zhugejin->addSkill("mingzhe");

    General *xingcai = new General(this, "xingcai", "shu", 3, false); // SP 028
    xingcai->addSkill(new Shenxian);
    xingcai->addSkill(new Qiangwu);
    xingcai->addSkill(new QiangwuTargetMod);
    related_skills.insert("qiangwu", "#qiangwu-target");
    addMetaObject<QiangwuCard>();

    General *sp_panfeng = new General(this, "sp_panfeng*xh_tianzhu", "qun", 4, true); // SP 029
    sp_panfeng->addSkill(new Kuangfu);

    General *zumao = new General(this, "zumao*xh_huben", "wu"); // SP 030
    zumao->addSkill(new Yinbing);
    zumao->addSkill(new Juedi);
    addMetaObject<YinbingCard>();

    General *sp_dingfeng = new General(this, "sp_dingfeng*xh_huben", "wu", 4, true); // SP 031
    sp_dingfeng->addSkill(new Duanbing);
    sp_dingfeng->addSkill(new Fenxun);
    skills << new FenxunClear;
    related_skills.insert("fenxun", "#fenxun-clear");

    General *zhugedan = new General(this, "zhugedan", "wei", 4); // SP 032
    zhugedan->addSkill(new Gongao);
    zhugedan->addSkill(new Juyi);

    General *sp_hetaihou = new General(this, "sp_hetaihou*xh_tianji", "qun", 3, false); // SP 033
    sp_hetaihou->addSkill(new Zhendu);
    sp_hetaihou->addSkill(new Qiluan);

    General *sunluyu = new General(this, "sunluyu*xh_tianji", "wu", 3, false); // SP 034
    sunluyu->addSkill(new Meibu);
    sunluyu->addSkill(new Mumu);

    General *fuwan = new General(this, "fuwan", "qun", 4);
    fuwan->addSkill("moukui");

    General *zhugeke = new General(this, "zhugeke*xh_huben", "wu", 3);
    zhugeke->addSkill(new Aocai);
    zhugeke->addSkill(new Duwu);
    addMetaObject<AocaiCard>();
    addMetaObject<DuwuCard>();

    General *zhuling = new General(this, "zhuling", "wei");
    zhuling->addSkill(new Zhanyi);
    zhuling->addSkill(new ZhanyiDiscard2);
    zhuling->addSkill(new ZhanyiNoDistanceLimit);
    zhuling->addSkill(new ZhanyiRemove);
    related_skills.insert("zhanyi", "#zhanyi-basic");
    related_skills.insert("zhanyi", "#zhanyi-equip");
    related_skills.insert("zhanyi", "#zhanyi-trick");
    addMetaObject<ZhanyiCard>();
    addMetaObject<ZhanyiViewAsBasicCard>();

    General *maliang = new General(this, "maliang", "shu", 3); // SP 035
    maliang->addSkill(new Xiemu);
    maliang->addSkill(new Naman);

    General *sp_ganfuren = new General(this, "sp_ganfuren", "shu", 3, false); // SP 037
    sp_ganfuren->addSkill(new Shushen);
    sp_ganfuren->addSkill(new Shenzhi);

    General *huangjinleishi = new General(this, "huangjinleishi", "qun", 3, false); // SP 038
    huangjinleishi->addSkill(new Fulu);
    huangjinleishi->addSkill(new Zhuji);

    General *sp_wenpin = new General(this, "sp_wenpin*xh_huben", "wei"); // SP 039
    sp_wenpin->addSkill(new SpZhenwei);

    General *simalang = new General(this, "simalang*xh_sibi", "wei", 3); // SP 040
    simalang->addSkill(new Quji);
    simalang->addSkill(new Junbing);
    addMetaObject<QujiCard>();

    General *sunhao = new General(this, "sunhao$", "wu", 5); // SP 041, SE 
    sunhao->addSkill(new Canshi);
    sunhao->addSkill(new Chouhai);
    sunhao->addSkill(new Guiming);

    General *zhaoxiang = new General(this, "zhaoxiang", "shu", 4, false);
    zhaoxiang->addSkill(new Fanghun);
    zhaoxiang->addSkill(new FanghunDraw("fanghun"));
    zhaoxiang->addSkill(new Fuhan);
    related_skills.insert("fanghun", "#fanghun");

    related_skills.insert("olfanghun", "#olfanghun");
	skills << new OLFanghun << new FanghunDraw("olfanghun") << new OLFuhan
	<< new MobileFanghun << new FanghunDraw("mobilefanghun");
    related_skills.insert("mobilefanghun", "#mobilefanghun");
    addMetaObject<MobileFanghunCard>();

    skills << new TenyearFanghun << new FanghunDraw("tenyearfanghun");
    related_skills.insert("tenyearfanghun", "#tenyearfanghun");
    addMetaObject<TenyearFanghunCard>();

    General *baosanniang = new General(this, "baosanniang", "shu", 3, false);
    baosanniang->addSkill(new Wuniang);
    baosanniang->addSkill(new Xushen);
    baosanniang->addRelateSkill("zhennan");

    addMetaObject<XiemuCard>();
    addMetaObject<FanghunCard>();
    addMetaObject<OLFanghunCard>();

    skills << new MeibuFilter("meibu")
           << new Zhennan;

    General *sunru = new General(this, "sunru", "wu", 3, false);
    sunru->addSkill(new Qingyi);
    sunru->addSkill(new QingyiSlashNoDistanceLimit);
    sunru->addSkill(new Shixin);
    related_skills.insert("qingyi", "#qingyi-slash-ndl");

    General *lifeng = new General(this, "lifeng", "shu", 3);
    lifeng->addSkill(new Tunchu);
    lifeng->addSkill(new TunchuEffect);
    lifeng->addSkill(new TunchuLimit);
    lifeng->addSkill(new Shuliang);
    related_skills.insert("tunchu", "#tunchu-effect");
    related_skills.insert("tunchu", "#tunchu-limit");
    addMetaObject<ShuliangCard>();

    General *lingju = new General(this, "lingju", "qun", 3, false);
    lingju->addSkill("jieyuan");
    lingju->addSkill("fenxin");

}
ADD_PACKAGE(SP)

MiscellaneousPackage::MiscellaneousPackage()
: Package("miscellaneous")
{
    General *wz_daqiao = new General(this, "wz_nos_daqiao", "wu", 3, false); // WZ 001
    wz_daqiao->addSkill("nosguose");
    wz_daqiao->addSkill("liuli");

    General *wz_xiaoqiao = new General(this, "wz_xiaoqiao", "wu", 3, false); // WZ 002
    wz_xiaoqiao->addSkill("tianxiang");
    wz_xiaoqiao->addSkill("hongyan");

    General *pr_shencaocao = new General(this, "pr_shencaocao", "god", 3, true); // PR LE 005
    pr_shencaocao->addSkill("guixin");
    pr_shencaocao->addSkill("feiying");

    General *pr_nos_simayi = new General(this, "pr_nos_simayi", "wei", 3, true); // PR WEI 002
    pr_nos_simayi->addSkill("nosfankui");
    pr_nos_simayi->addSkill("nosguicai");

    General *Caesar = new General(this, "caesar", "god", 4); // E.SP 001
    Caesar->addSkill(new Conqueror);

    General *hanba = new General(this, "hanba", "qun", 4, false);
    hanba->addSkill(new Fentian);
    hanba->addSkill(new Zhiri);
    hanba->addSkill(new FentianRange);
    related_skills.insert("fentian", "#fentian");
    hanba->addRelateSkill("xintan");

    skills << new Xintan;

    addMetaObject<XintanCard>();
}
ADD_PACKAGE(Miscellaneous)
