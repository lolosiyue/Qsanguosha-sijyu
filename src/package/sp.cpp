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
        room->useCard(CardUseStruct(slash, owner, to), !Config.EnableHegemony);
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

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &, ServerPlayer *target) const override
    {
        room->setEmotion(player, "weapon/moonspear");
        if (!room->askForCard(target, "jink", "@moon-spear-jink", QVariant(), Card::MethodResponse, player))
            room->damage(DamageStruct(objectName(), player, target));
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

    // The prompted Slash is the invocation; declining it means the spear did nothing.
    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        player->setFlags("MoonspearUse");
        const bool used = room->askForUseCard(player, "slash", "@moon-spear-slash", -1, Card::MethodUse, false);
        player->setFlags("-MoonspearUse");
        return used;
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

class WeidiViewAsSkill : public ViewAsSkillV2
{
public:
    WeidiViewAsSkill() : ViewAsSkillV2("weidi")
    {
    }

    static QList<const ViewAsSkill *> getLordViewAsSkills(const Player *player)
    {
        QList<const ViewAsSkill *> vs_skills;
        if (!player) return vs_skills;
        foreach (const Player *p, player->getAliveSiblings()) {
            if (p->isLord()) {
				foreach (const Skill *skill, p->getVisibleSkillList()) {
					if (skill->isLordSkill() && player->hasLordSkill(skill->objectName())) {
						const ViewAsSkill *vs = ViewAsSkill::parseViewAsSkill(skill);
						if (vs) vs_skills << vs;
					}
				}
            }
        }
        return vs_skills;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        foreach (const ViewAsSkill *skill, candidates(request)) {
            if (isEnabled(request, skill)) return true;
        }
        return false;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        foreach (const ViewAsSkill *skill, candidates(request)) {
            if (!isEnabled(request, skill)) continue;
            if (const ViewAsSkillV2 *v2 = dynamic_cast<const ViewAsSkillV2 *>(skill)) {
                if (v2->canSelectCard(delegated(request, skill), card)) return true;
            } else if (skill->viewFilter(cardsOf(request.selectedCardIds), card)) {
                return true;
            }
        }
        return false;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return resolve(request) != nullptr;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const ViewAsSkill *skill = resolve(request);
        if (!skill) return nullptr;
        if (const ViewAsSkillV2 *v2 = dynamic_cast<const ViewAsSkillV2 *>(skill))
            return v2->createCard(delegated(request, skill));
        return skill->viewAs(cardsOf(request.selectedCardIds));
    }

    QString historyKey(const ActiveSkillRequest &request) const override
    {
        // A borrowed lord card keeps the history key of the lord skill that produced it.
        const ViewAsSkill *skill = resolve(request);
        if (!skill) return objectName();
        if (const ViewAsSkillV2 *v2 = dynamic_cast<const ViewAsSkillV2 *>(skill))
            return v2->historyKey(delegated(request, skill));
        const Card *card = skill->viewAs(cardsOf(request.selectedCardIds));
        if (!card) return objectName();
        const QString key = card->getClassName();
        if (card->isVirtualCard()) const_cast<Card *>(card)->deleteLater();
        return key;
    }

private:
    static QList<const Card *> cardsOf(const QList<int> &ids)
    {
        QList<const Card *> cards;
        foreach (int id, ids) {
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (card) cards << card;
        }
        return cards;
    }

    static ActiveSkillRequest delegated(const ActiveSkillRequest &request, const ViewAsSkill *skill)
    {
        ActiveSkillRequest child = request;
        child.activationRef.key.skillName = skill->objectName();
        return child;
    }

    static QList<const ViewAsSkill *> candidates(const ActiveSkillRequest &request)
    {
        const QList<const ViewAsSkill *> skills = getLordViewAsSkills(request.initiator);
        // A client declaration narrows the choice; server rebuilds match the selection instead.
        const QString declared = request.userString.isEmpty() && request.initiator
            ? request.initiator->getTag("weidi").toString() : request.userString;
        foreach (const ViewAsSkill *skill, skills) {
            if (skill->objectName() == declared) return QList<const ViewAsSkill *>{skill};
        }
        return skills;
    }

    static bool isEnabled(const ActiveSkillRequest &request, const ViewAsSkill *skill)
    {
        if (const ViewAsSkillV2 *v2 = dynamic_cast<const ViewAsSkillV2 *>(skill))
            return v2->canActivate(delegated(request, skill));
        return skill->isAvailable(request.initiator, request.reason, request.pattern);
    }

    static const ViewAsSkill *resolve(const ActiveSkillRequest &request)
    {
        foreach (const ViewAsSkill *skill, candidates(request)) {
            if (!isEnabled(request, skill)) continue;
            if (const ViewAsSkillV2 *v2 = dynamic_cast<const ViewAsSkillV2 *>(skill)) {
                if (v2->cardSelectionFeasible(delegated(request, skill))) return skill;
                continue;
            }
            QList<const Card *> selected;
            bool accepted = true;
            foreach (const Card *card, cardsOf(request.selectedCardIds)) {
                if (!skill->viewFilter(selected, card)) { accepted = false; break; }
                selected << card;
            }
            if (!accepted || selected.size() != request.selectedCardIds.size()) continue;
            const Card *card = skill->viewAs(selected);
            if (!card) continue;
            if (card->isVirtualCard()) const_cast<Card *>(card)->deleteLater();
            return skill;
        }
        return nullptr;
    }
};

class Weidi : public TriggerSkillV2
{
public:
    Weidi() : TriggerSkillV2("weidi")
    {
        frequency = Compulsory;
        view_as_skill = new WeidiViewAsSkill;
    }

    SkillDialogInfo getDialogInfo() const override
    {
        SkillDialogInfo info = SkillDialogInfo::named("weidi", objectName());
        info.parameters.insert("customDeclaration", true);
        return info;
    }

    QList<SkillDeclarationCandidate> declarationCandidates(
        const Player *self, CardUseStruct::CardUseReason reason,
        const QString &pattern, const QStringList &bannedPackages,
        quint64 requestId) const override
    {
        Q_UNUSED(bannedPackages)
        Q_UNUSED(requestId)
        QList<SkillDeclarationCandidate> result;
        if (!self || !Sanguosha) return result;
        foreach (const ViewAsSkill *skill, WeidiViewAsSkill::getLordViewAsSkills(self)) {
            if (!skill) continue;
            SkillDeclarationCandidate candidate;
            candidate.value = skill->objectName();
            candidate.label = Sanguosha->translate(candidate.value);
            candidate.kind = "skill";
            candidate.enabled = skill->isAvailable(self, reason, pattern);
            candidate.reason = candidate.enabled ? SkillDeclarationReason::None
                                                   : SkillDeclarationReason::CandidateUnavailable;
            result << candidate;
        }
        return result;
    }

    SkillDeclarationReason declarationReason(const Player *self, const QString &value,
        const Card *) const override
    {
        if (!self || !Sanguosha) return SkillDeclarationReason::CandidateUnavailable;
        foreach (const ViewAsSkill *skill, WeidiViewAsSkill::getLordViewAsSkills(self)) {
            if (skill && skill->objectName() == value
                && skill->isAvailable(self, Sanguosha->getCurrentCardUseReason(),
                    Sanguosha->getCurrentCardUsePattern()))
                return SkillDeclarationReason::None;
        }
        return SkillDeclarationReason::CandidateUnavailable;
    }
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
    Yanyu() : TriggerSkillV2("yanyu")
    {
        events << EventPhaseStart << BeforeCardsMove;
    }

    static QList<int> giftable(ServerPlayer *player, const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        if (move.to_place != Player::DiscardPile) return ids;
        foreach (int id, move.card_ids) {
            if (player->getMark(Sanguosha->getCard(id)->getType()+"YanyuDiscard-PlayClear") > 0)
                ids << id;
        }
        return ids;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player) return result;
        if (triggerEvent == EventPhaseStart) {
            if (player->getPhase() != Player::Play) return result;
            // Any player's Play phase opens every Xiahoushi's discard window.
            foreach (ServerPlayer *xiahou, room->findPlayersBySkillName(objectName())) {
                if (xiahou->isAlive() && xiahou->canDiscard(xiahou, "he"))
                    result[xiahou] << objectName();
            }
        } else if (player->isAlive() && player->hasSkill(objectName())
                   && !giftable(player, data.value<CardsMoveOneTimeStruct>()).isEmpty()) {
            result[player] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent triggerEvent, Room *room, ServerPlayer *xiahou, SkillContext &ctx) const override
    {
        if (triggerEvent != EventPhaseStart) return true;
        const Card *card = room->askForCard(xiahou, "..", "@yanyu-discard", *ctx.original_data,
            Card::MethodNone, nullptr, false, objectName());
        if (!card || card->isVirtualCard() || !xiahou->canDiscard(xiahou, card->getEffectiveId())) return false;
        ctx.extra_data = card->getEffectiveId();
        return true;
    }

    bool pay(TriggerEvent triggerEvent, Room *room, ServerPlayer *xiahou, SkillContext &ctx) const override
    {
        if (triggerEvent != EventPhaseStart) return true;
        bool ok = false;
        const int id = ctx.extra_data.toInt(&ok);
        if (!ok || room->getCardOwner(id) != xiahou || !xiahou->canDiscard(xiahou, id)) return false;
        room->throwCard(id, objectName(), xiahou);
        return true;
    }

    bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (triggerEvent == EventPhaseStart) {
            room->broadcastSkillInvoke(objectName(), 1);
            player->addMark(Sanguosha->getCard(ctx.extra_data.toInt())->getType()+"YanyuDiscard-PlayClear", 3);
            return false;
        }
        CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        QList<int> ids = giftable(player, move);
        while (ids.length()>0&&player->isAlive()) {
            room->fillAG(ids, player);
            int card_id = room->askForAG(player, ids, true, objectName());
            room->clearAG(player);
            if (card_id < 0) break;
            player->setMark("YanyuOnlyId", card_id + 1); // For AI
            const Card *card = Sanguosha->getCard(card_id);
            ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(),
                QString("@yanyu-give:::%1:%2\\%3").arg(card->objectName())
                .arg(card->getSuitString() + "_char")
                .arg(card->getNumberString()),
                true, true);
            if (!target) break;
            player->removeMark(card->getType()+"YanyuDiscard-PlayClear");
            Player::Place place = move.from_places.at(move.card_ids.indexOf(card_id));
            QList<int> _card_id;
            _card_id << card_id;
            move.removeCardIds(_card_id);
            *ctx.original_data = QVariant::fromValue(move);
            ids = giftable(player, move);
            if (move.from == target && place != Player::PlaceTable) {
                // just indicate which card she chose...
                LogMessage log;
                log.type = "$MoveCard";
                log.from = target;
                log.to << target;
                log.card_str = QString::number(card_id);
                room->sendLog(log);
            }
            room->broadcastSkillInvoke(objectName(), 2);
            target->obtainCard(card);
        }
        return false;
    }
};

class Xiaode : public TriggerSkillV2
{
public:
    Xiaode() : TriggerSkillV2("xiaode")
    {
        events << BuryVictim;
    }

    bool usesEventPriority() const override
    {
        return true;
    }

    int getPriority(TriggerEvent) const override
    {
        return -2;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        TriggerList result;
        foreach (ServerPlayer *xiahoushi, room->findPlayersBySkillName(objectName())) {
            if (xiahoushi->isAlive() && xiahoushi->getTag("XiaodeSkill").toString().isEmpty()
                && !xiahoushi->getTag("XiaodeVictimSkills").toStringList().isEmpty())
                result[xiahoushi] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *xiahoushi, SkillContext &ctx) const override
    {
        QStringList skill_list = xiahoushi->getTag("XiaodeVictimSkills").toStringList();
        if (skill_list.isEmpty()) return false;
        if (!room->askForSkillInvoke(xiahoushi, objectName(), QVariant::fromValue(skill_list))) return false;
        ctx.choice = room->askForChoice(xiahoushi, objectName(), skill_list.join("+"));
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *xiahoushi, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        xiahoushi->setTag("XiaodeSkill", ctx.choice);
        room->acquireSkill(xiahoushi, ctx.choice);
        return false;
    }
};

class XiaodeEx : public TriggerSkillV2
{
public:
    XiaodeEx() : TriggerSkillV2("#xiaode")
    {
        events << EventPhaseChanging << EventLoseSkill << Death;
    }

    // Borrowed-skill lifecycle bookkeeping; it still runs after Xiaode itself is lost.
    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive) {
                QString skill_name = player->getTag("XiaodeSkill").toString();
                if (!skill_name.isEmpty()) {
                    room->detachSkillFromPlayer(player, skill_name, false, true);
                    player->removeTag("XiaodeSkill");
                }
            }
        } else if (triggerEvent == EventLoseSkill && data.value<SkillChangeStruct>().skillName == "xiaode") {
            QString skill_name = player->getTag("XiaodeSkill").toString();
            if (!skill_name.isEmpty()) {
                room->detachSkillFromPlayer(player, skill_name, false, true);
                player->removeTag("XiaodeSkill");
            }
        } else if (triggerEvent == Death && player->isAlive() && player->hasSkill(objectName())) {
            DeathStruct death = data.value<DeathStruct>();
            QStringList skill_list;
            skill_list.append(addSkillList(death.who->getGeneral()));
            skill_list.append(addSkillList(death.who->getGeneral2()));
            player->setTag("XiaodeVictimSkills", QVariant::fromValue(skill_list));
        }
        return true;
    }

private:
    static QStringList addSkillList(const General *general)
    {
        if (!general) return QStringList();
        QStringList skill_list;
        foreach (const Skill *skill, general->getSkillList()) {
            if (skill->isVisible() && !skill->isLordSkill() && skill->getFrequency() != Skill::Wake)
                skill_list.append(skill->objectName());
        }
        return skill_list;
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
    XiemuViewAsSkill() : ViewAsSkillV2("xiemu", 1)
    {
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        return player && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && player->canDiscard(player, "he") && !player->hasUsed("XiemuCard");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ViewAsSkillV2::canSelectCard(request, card) && matchesFilter(request, card, "Slash");
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && replaySelection(this, request);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        XiemuCard *card = new XiemuCard;
        card->addSubcards(request.selectedCardIds);
        card->setSkillName(objectName());
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "XiemuCard";
    }
};

class Xiemu : public TriggerSkillV2
{
public:
    Xiemu() : TriggerSkillV2("xiemu")
    {
        events << TargetConfirmed << EventPhaseStart;
        view_as_skill = new XiemuViewAsSkill;
    }

    // The declared kingdom mark acts on its own until the holder's next round.
    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (triggerEvent == TargetConfirmed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.from && player != use.from && use.card->getTypeId() != Card::TypeSkill
                && use.card->isBlack() && use.to.contains(player)
                && player->getMark("@xiemu_" + use.from->getKingdom()) > 0) {
                LogMessage log;
                log.type = "#InvokeSkill";
                log.from = player;
                log.arg = objectName();
                room->sendLog(log);

                room->notifySkillInvoked(player, objectName());
                player->drawCards(2, objectName());
            }
        } else if (player->getPhase() == Player::RoundStart) {
            foreach (QString kingdom, Sanguosha->getKingdoms()) {
                QString markname = "@xiemu_" + kingdom;
                if (player->getMark(markname) > 0)
                    room->setPlayerMark(player, markname, 0);
            }
        }
        return true;
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

    bool cost(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        return room->askForSkillInvoke(p, objectName(), *ctx.original_data);
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &ctx) const override
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
            log.from = p;
            log.to << damage.to;
            log.arg = QString::number(damage.damage);
            log.arg2 = QString::number(++damage.damage);
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
        events << EventPhaseStart << CardUsed;
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

    // The accepted drawback follows the turn flag, even if Canshi is lost meanwhile.
    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (triggerEvent != CardUsed || !player || !player->isAlive() || !player->hasFlag(objectName())) return true;
        const Card *card = data.value<CardUseStruct>().card;
        if (card&&(card->isKindOf("BasicCard")||card->isKindOf("TrickCard"))&&player->canDiscard(player,"he")) {
            room->sendCompulsoryTriggerLog(player, objectName());
            room->askForDiscard(player, objectName(), 1, 1, false, true, "@canshi-discard");
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (triggerEvent == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Draw && woundedCount(room) > 0)
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
    {
        return player->askForSkillInvoke(this);
    }

    // Returning true replaces the ordinary draw phase.
    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->broadcastSkillInvoke(objectName());
        player->setFlags(objectName());
        player->drawCards(woundedCount(room), objectName());
        return true;
    }
};

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
        ++damage.damage;
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
        return burn > 0 ? CorrectSkillResult::useAmount(burn) : CorrectSkillResult::noEffect();
    }
};

class Zhiri : public TriggerSkillV2
{
public:
    Zhiri() : TriggerSkillV2("zhiri")
    {
        events << EventPhaseStart;
        frequency = Wake;
        waked_skills = "xintan";
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start
            || player->getMark(objectName()) > 0) return TriggerList();
        if (player->getPile("burn").length() < 3 && !hasWakeGrant(player, objectName())) return TriggerList();
        return TriggerList{{player, QStringList{objectName()}}};
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *hanba, SkillContext &) const override
    {
        if (hanba->getPile("burn").length() >= 3) {
            LogMessage log;
            log.from = hanba;
            log.type = "#ZhiriWake";
            log.arg = QString::number(hanba->getPile("burn").length());
            log.arg2 = objectName();
            room->sendLog(log);
        } else if (!hanba->canWake(objectName())) {
            return false;
        }
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(hanba,objectName());
        room->doSuperLightbox(hanba, "zhiri");

        room->setPlayerMark(hanba, objectName(), 1);
        if (room->changeMaxHpForAwakenSkill(hanba, -1, objectName()))
            room->acquireSkill(hanba, "xintan");
        return false;
    }
};

XintanCard::XintanCard()
{
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
    Xintan() : ViewAsSkillV2("xintan", 2)
    {
        expand_pile = "burn";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getPile("burn").length() >= 2 && !request.initiator->hasUsed("XintanCard");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return ViewAsSkillV2::canSelectCard(request, card) && matchesFilter(request, card, ".")
            && request.initiator->getPile("burn").contains(card->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 2 && replaySelection(this, request);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        XintanCard *xt = new XintanCard;
        xt->addSubcards(request.selectedCardIds);
        xt->setSkillName(objectName());
        return xt;
    }

    QString historyKey(const ActiveSkillRequest &) const override
    {
        return "XintanCard";
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
        room->setPlayerMark(player, objectName() + "_id", request.selectedCardIds.first() + 1);
        return true;
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
    FanghunDraw(const QString &fanghun_skill) : TriggerSkillV2("#" + fanghun_skill), fanghun_skill(fanghun_skill)
    {
        events << CardResponded << CardFinished << MarkChanged;
        global = true;
    }

    bool usesEventPriority() const override
    {
        return true;
    }

    int getPriority(TriggerEvent) const override
    {
        return 0;
    }

    // Rewards follow the converted card itself, whoever currently holds Fanghun.
    bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (triggerEvent == CardResponded || triggerEvent == CardFinished) {
            const Card *card = triggerEvent == CardResponded ? data.value<CardResponseStruct>().m_card
                                                             : data.value<CardUseStruct>().card;
            if (!card) return true;
            foreach(ServerPlayer *p, room->getAllPlayers()) {
                int marks = p->getMark(fanghun_skill + "_id") - 1;
                if (marks < 0) continue;
                if (marks == card->getEffectiveId()) {
                    room->setPlayerMark(player, fanghun_skill + "_id", 0);
                    p->drawCards(1, objectName());
                    break;
                }
            }
        } else {
			MarkStruct mark = data.value<MarkStruct>();
			if (mark.name == "&meiying" && mark.gain < 0)
				player->addMark("meiying", -mark.gain);
		}
        return true;
    }
private:
    QString fanghun_skill;
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

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        player->gainMark("&meiying");
        return false;
     }
};

class Fuhan : public TriggerSkillV2
{
public:
    Fuhan() : TriggerSkillV2("fuhan")
    {
        events << EventPhaseStart;
        frequency = Limited;
        limit_mark = "@fuhanMark";
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::RoundStart
            && player->getMark("@fuhanMark") > 0 && player->getMark("&meiying") > 0)
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
    {
        QString num = QString::number(player->getMark("meiying") + player->getMark("&meiying"));
        return player->askForSkillInvoke("fuhan", QString("fuhan_invoke:%1").arg(num));
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (player->getMark("@fuhanMark") <= 0) return false;
        room->removePlayerMark(player, "@fuhanMark");
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
        events << EventPhaseStart;
        frequency = Limited;
        limit_mark = "@olfuhanMark";
    }

    static int bounded(int n)
    {
        return qMax(2, qMin(8, n));
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        if (player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::RoundStart
            && player->getMark("@olfuhanMark") > 0 && player->getMark("&meiying") > 0)
            return TriggerList{{player, QStringList{objectName()}}};
        return TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
    {
        QString num = QString::number(bounded(player->getMark("meiying") + player->getMark("&meiying")));
        return player->askForSkillInvoke("olfuhan", QString("olfuhan_invoke:%1").arg(num));
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (player->getMark("@olfuhanMark") <= 0) return false;
        room->removePlayerMark(player, "@olfuhanMark");
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

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        player->gainMark("&meiying", 1);
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

    bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        player->gainMark("&meiying", 1);
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

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        if (target->isNude()) return false;
        int id = room->askForCardChosen(player, target, "he", objectName());
        room->obtainCard(player, id, false);
        if (target->isAlive()) target->drawCards(1, objectName());
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getGeneralName().contains("guansuo") || p->getGeneral2Name().contains("guansuo"))
                p->drawCards(1, objectName());
        }
        return false;
    }
};

class Xushen : public TriggerSkillV2
{
public:
    Xushen() : TriggerSkillV2("xushen")
    {
        events << QuitDying;
        frequency = Limited;
        limit_mark = "@xushenMark";
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getMark("@xushenMark") <= 0)
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

    bool pay(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        if (player->getMark("@xushenMark") <= 0) return false;
        room->removePlayerMark(player, "@xushenMark");
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

// The paid turn flag, not skill ownership, grants the range.
class ZhanyiNoDistanceLimit : public TargetModSkillV2
{
public:
    ZhanyiNoDistanceLimit() : TargetModSkillV2("#zhanyi-trick", ".")
    {
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == TargetModSkill::DistanceLimit && ctx.primary && ctx.primary->hasFlag("zhanyiTrick")
            ? CorrectSkillResult::useAmount(999) : CorrectSkillResult::noEffect();
    }
};

class ZhanyiDiscard2 : public TriggerSkillV2
{
public:
    ZhanyiDiscard2() : TriggerSkillV2("#zhanyi-equip")
    {
        events << TargetSpecified;
    }

    // The paid turn flag carries the effect; the discards are forced on the targets.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasFlag("zhanyiEquip")) return true;
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card == nullptr || !use.card->isKindOf("Slash"))
            return true;

        foreach (ServerPlayer *p, use.to) {
            if (p->isNude())
                continue;

            if (p->getCardCount() <= 2) {
                DummyCard dummy;
                dummy.addSubcards(p->getCards("he"));
                room->throwCard(&dummy, p);
            } else
                room->askForDiscard(p, "zhanyi_equip", 2, 2, false, true, "@zhanyiequip_discard");
        }
        return true;
    }
};

class Zhanyi : public ViewAsSkillV2
{
public:
    Zhanyi() : ViewAsSkillV2("zhanyi", 1)
    {
        // Basic conversions may use response piles; ZhanyiCard is limited to owned cards below.
        response_or_use = true;
    }

    static bool basicMode(const Player *player)
    {
        return player->getMark("ViewAsSkill_zhanyiEffect") > 0;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return !player->hasUsed("ZhanyiCard") || basicMode(player);
        if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
            && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
        const QString &pattern = request.pattern;
        if (!basicMode(player)) return false;
        if (pattern.startsWith(".") || pattern.startsWith("@")) return false;
        if (pattern == "peach" && player->getMark("Global_PreventPeach") > 0) return false;
        for (int i = 0; i < pattern.length(); i++) {
            QChar ch = pattern[i];
            if (ch.isUpper() || ch.isDigit()) return false; // This is an extremely dirty hack!! For we need to prevent patterns like 'BasicCard'
        }
        return !(pattern == "nullification");
    }

    SkillDialogInfo getDialogInfo() const override
    {
        return SkillDialogInfo::guhuo("zhanyi", true, false);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!ViewAsSkillV2::canSelectCard(request, card) || !matchesFilter(request, card, ".")) return false;
        if (basicMode(request.initiator))
            return card->isKindOf("BasicCard");
        return request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->hasEquip(card);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.size() == 1 && replaySelection(this, request);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        if (!basicMode(request.initiator)) {
            ZhanyiCard *zy = new ZhanyiCard;
            zy->addSubcards(request.selectedCardIds);
            zy->setSkillName(objectName());
            return zy;
        }
        // A response preview has no dialog declaration; the pattern head is declared.
        QString name = request.userString;
        if (name.isEmpty() && request.reason != CardUseStruct::CARD_USE_REASON_PLAY)
            name = request.pattern;
        Card *card = basicCard(request, name);
        if (!card) return nullptr;
        const bool usable = request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            ? card->isAvailable(request.initiator)
            : Sanguosha->matchPattern(request.pattern, request.initiator, card);
        if (!usable) {
            delete card;
            return nullptr;
        }
        return card;
    }

    QString historyKey(const ActiveSkillRequest &request) const override
    {
        return cardHistoryKey(request.userString, "ZhanyiCard");
    }

    // The legacy validate choices for ambiguous responses now replace the previewed card.
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY || !basicMode(request.initiator)) return true;
        const bool maneuvering = !Sanguosha->getBanPackages().contains("maneuvering");
        QStringList choices;
        QString key;
        if (request.pattern == "slash") {
            key = "zhanyi_slash";
            choices << "slash";
            if (maneuvering) choices << "normal_slash" << "thunder_slash" << "fire_slash";
        } else if (request.pattern == "peach+analeptic") {
            key = "zhanyi_saveself";
            choices << "peach";
            if (maneuvering) choices << "analeptic";
        } else {
            return true;
        }
        Card *card = basicCard(request, room->askForChoice(ctx.invoker, key, choices.join("+")));
        if (!card) return false;
        card->deleteLater();
        card->setActivationSkill(objectName(), request.getActivationInstanceId());
        if (ctx.use_card)
            card->setSourceSkill(ctx.use_card->getSourceSkillName(), ctx.use_card->getSourceSkillInstanceId());
        ctx.updated_card = card;
        return true;
    }

private:
    static Card *basicCard(const ActiveSkillRequest &request, QString name)
    {
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        name = name.split("+").first();
        // "slash" keeps a Slash material's nature; "normal_slash" forces a plain Slash.
        if (name == "slash" && material->isKindOf("Slash"))
            name = material->objectName();
        else if (name == "normal_slash")
            name = "slash";
        Card *card = Sanguosha->cloneCard(name, material->getSuit(), material->getNumber());
        if (!card) return nullptr;
        if (!card->isKindOf("BasicCard") || Sanguosha->getBanPackages().contains(card->getPackage())) {
            delete card;
            return nullptr;
        }
        card->addSubcard(material);
        card->setSkillName("_zhanyi");
        return card;
    }
};

class ZhanyiRemove : public TriggerSkillV2
{
public:
    ZhanyiRemove() : TriggerSkillV2("#zhanyi-basic")
    {
        events << EventPhaseChanging;
    }

    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player && player->isAlive() && player->getMark("ViewAsSkill_zhanyiEffect") > 0
            && data.value<PhaseChangeStruct>().to == Player::NotActive)
            room->setPlayerMark(player, "ViewAsSkill_zhanyiEffect", 0);
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
        player->setFlags("tunchu");
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
    TunchuEffect() : TriggerSkillV2("#tunchu-effect")
    {
        events << AfterDrawNCards;
    }

    // Completes an accepted Tunchu draw; the turn flag carries the obligation.
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasFlag("tunchu")) return true;
        DrawStruct draw = data.value<DrawStruct>();
		if (draw.reason=="draw_phase" && !player->isKongcheng()) {
            player->setFlags("-tunchu");
            const Card *c = room->askForExchange(player, "tunchu", 1, 1, false, "@tunchu-put");
            if (c != nullptr) player->addToPile("food", c);
        }

        return true;
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

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        ShuliangCard *c = new ShuliangCard;
        c->addSubcards(request.selectedCardIds);
        c->setSkillName(objectName());
        return c;
    }

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

    // Declining the prompt means Shuliang was never invoked; the card use is its own activation.
    bool cost(TriggerEvent, Room *room, ServerPlayer *p, SkillContext &ctx) const override
    {
        return ctx.invoker->isAlive()
            && room->askForUseCard(p, "@@shuliang", "@shuliang:" + ctx.invoker->objectName(), -1, Card::MethodNone);
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.invoker->isAlive())
            ctx.invoker->drawCards(2, objectName());
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

    // The virtual Slash is the invocation; skipping both phases is its price.
    bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
    {
        return room->askForUseCard(player, "@@qingyi", "@qingyi-slash");
    }

    bool effect(TriggerEvent, Room *, ServerPlayer *player, SkillContext &) const override
    {
        player->skip(Player::Judge, true);
        player->skip(Player::Draw, true);
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
	Xueji() : ViewAsSkillV2("xueji", 1)
	{
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const Player *player = request.initiator;
		return player && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && player->getLostHp() > 0
			&& player->canDiscard(player, "he") && !player->hasUsed("XuejiCard");
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return ViewAsSkillV2::canSelectCard(request, card) && matchesFilter(request, card, ".|red")
			&& !request.initiator->isJilei(card);
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.selectedCardIds.size() == 1 && replaySelection(this, request);
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		XuejiCard *first = new XuejiCard;
		first->addSubcards(request.selectedCardIds);
		first->setSkillName(objectName());
		return first;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "XuejiCard";
	}
};

class Huxiao : public TargetModSkillV2
{
public:
	Huxiao() : TargetModSkillV2("huxiao")
	{
	}

	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		if (ctx.modType != TargetModSkill::Residue || !ctx.holder) return CorrectSkillResult::noEffect();
		return CorrectSkillResult::useAmount(ctx.holder->getMark("huxiao-PlayClear"));
	}
};

class HuxiaoCount : public TriggerSkillV2
{
public:
	HuxiaoCount() : TriggerSkillV2("#huxiao-count")
	{
		events << CardOffset;
	}

	// Counting is not an invocation; it only feeds Huxiao's residue.
	bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return true;
		CardEffectStruct effect = data.value<CardEffectStruct>();
		if (player->getPhase() == Player::Play && effect.card && effect.card->isKindOf("Slash"))
			room->addPlayerMark(player, "huxiao-PlayClear");
		return true;
	}
};

class Wuji : public TriggerSkillV2
{
public:
	Wuji() : TriggerSkillV2("wuji")
	{
		events << EventPhaseStart;
		frequency = Wake;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish
			|| player->getMark(objectName()) > 0) return TriggerList();
		if (player->getMark("damage_point_round") < 3 && !hasWakeGrant(player, objectName())) return TriggerList();
		return TriggerList{{player, QStringList{objectName()}}};
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (player->getMark("damage_point_round") >= 3) {
			LogMessage log;
			log.type = "#WujiWake";
			log.from = player;
			log.arg = QString::number(player->getMark("damage_point_round"));
			log.arg2 = objectName();
			room->sendLog(log);
		} else if (!player->canWake(objectName())) {
			return false;
		}
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());
		room->doSuperLightbox(player, "wuji");

		room->setPlayerMark(player, "wuji", 1);
		if (room->changeMaxHpForAwakenSkill(player, 1, objectName())) {
			room->recover(player, RecoverStruct("wuji", player));
			if (player->getMark("wuji") == 1)
				room->detachSkillFromPlayer(player, "huxiao");
		}

		return false;
	}
};

NewxuehenCard::NewxuehenCard()
{
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
	Newxuehen() : ViewAsSkillV2("newxuehen", 1)
	{
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& !request.initiator->hasUsed("NewxuehenCard") && request.initiator->getLostHp() > 0;
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return ViewAsSkillV2::canSelectCard(request, card) && matchesFilter(request, card, ".|red");
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.selectedCardIds.size() == 1 && replaySelection(this, request);
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		NewxuehenCard *card = new NewxuehenCard;
		card->addSubcards(request.selectedCardIds);
		card->setSkillName(objectName());
		return card;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "NewxuehenCard";
	}
};

class NewHuxiao : public TriggerSkillV2
{
public:
	NewHuxiao() : TriggerSkillV2("newhuxiao")
	{
		events << Damage;
		frequency = Compulsory;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		DamageStruct damage = data.value<DamageStruct>();
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || damage.nature != DamageStruct::Fire
			|| damage.from != player || !damage.to || damage.to->isDead()) return TriggerList();
		return TriggerList{{player, QStringList{objectName()}}};
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		DamageStruct damage = ctx.original_data->value<DamageStruct>();
		if (damage.to->isDead()) return false;
		room->sendCompulsoryTriggerLog(player, objectName(), true, true);
		damage.to->drawCards(1, objectName());
		room->addPlayerMark(player, "newhuxiao_from-Clear");
		room->addPlayerMark(damage.to, "newhuxiao_to-Clear");
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
		if(from->getMark("newhuxiao_from-Clear") > 0 && to && to->getMark("newhuxiao_to-Clear") > 0)
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
		events << EventPhaseStart;
		frequency = Wake;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish
			|| player->getMark(objectName()) > 0) return TriggerList();
		if (player->getMark("damage_point_round") < 3 && !hasWakeGrant(player, objectName())) return TriggerList();
		return TriggerList{{player, QStringList{objectName()}}};
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (player->getMark("damage_point_round") >= 3) {
			LogMessage log;
			log.type = "#WujiWake";
			log.from = player;
			log.arg = QString::number(player->getMark("damage_point_round"));
			log.arg2 = objectName();
			room->sendLog(log);
		} else if (!player->canWake(objectName())) {
			return false;
		}
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(player, objectName());

		room->doSuperLightbox(player, "newwuji");

		room->setPlayerMark(player, "newwuji", 1);
		if (room->changeMaxHpForAwakenSkill(player, 1, objectName())) {
			room->recover(player, RecoverStruct("newwuji", player));

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
        room->useCard(CardUseStruct(analeptic, target, QList<ServerPlayer *>()), true);
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
		events << EventPhaseStart;
		frequency = Wake;
		waked_skills = "benghuai,weizhong";
	}

	static bool naturalWake(ServerPlayer *zhugedan)
	{
		return zhugedan->isWounded() && zhugedan->getMaxHp() > zhugedan->aliveCount();
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start
			|| player->getMark(objectName()) > 0) return TriggerList();
		if (!naturalWake(player) && !hasWakeGrant(player, objectName())) return TriggerList();
		return TriggerList{{player, QStringList{objectName()}}};
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *zhugedan, SkillContext &) const override
	{
		if (naturalWake(zhugedan)) {
			LogMessage log;
			log.type = "#JuyiWake";
			log.from = zhugedan;
			log.arg = QString::number(zhugedan->getMaxHp());
			log.arg2 = QString::number(zhugedan->aliveCount());
			log.arg3 = objectName();
			room->sendLog(log);
		} else if (!zhugedan->canWake(objectName())) {
			return false;
		}
		zhugedan->peiyin(objectName());
		room->notifySkillInvoked(zhugedan, objectName());
		room->doSuperLightbox(zhugedan, "juyi");

		room->setPlayerMark(zhugedan, "juyi", 1);
		if (room->changeMaxHpForAwakenSkill(zhugedan, 0, objectName())) {
			int diff = zhugedan->getHandcardNum() - zhugedan->getMaxHp();
			if (diff < 0)
				room->drawCards(zhugedan, -diff, objectName());
			if (zhugedan->getMark("juyi") == 1)
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

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		room->broadcastSkillInvoke(objectName());
		room->sendCompulsoryTriggerLog(player, objectName());

		player->drawCards(1, objectName());
		return false;
	}
};

ZhoufuCard::ZhoufuCard()
{
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
	ZhoufuViewAsSkill() : ViewAsSkillV2("zhoufu", 1)
	{
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& !request.initiator->hasUsed("ZhoufuCard");
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return ViewAsSkillV2::canSelectCard(request, card) && matchesFilter(request, card, ".|.|.|hand");
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.selectedCardIds.size() == 1 && replaySelection(this, request);
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		Card *card = new ZhoufuCard;
		card->addSubcards(request.selectedCardIds);
		card->setSkillName(objectName());
		return card;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "ZhoufuCard";
	}
};

class Zhoufu : public TriggerSkillV2
{
public:
	Zhoufu() : TriggerSkillV2("zhoufu")
	{
		events << StartJudge << EventPhaseChanging;
		view_as_skill = new ZhoufuViewAsSkill;
	}

	// The incantation pile acts for its holder, who never owns Zhoufu.
	bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || player->getPile("incantation").isEmpty()) return true;
		if (triggerEvent == StartJudge) {
			int card_id = player->getPile("incantation").first();

			JudgeStruct *judge = data.value<JudgeStruct *>();
			judge->card = Sanguosha->getCard(card_id);

			LogMessage log;
			log.type = "$ZhoufuJudge";
			log.from = player;
			log.arg = objectName();
			log.card_str = QString::number(card_id);
			room->sendLog(log);

			room->moveCardTo(judge->card, nullptr, judge->who, Player::PlaceJudge,
				CardMoveReason(CardMoveReason::S_REASON_JUDGE, judge->who->objectName(), "zhoufu", judge->reason), true);
			judge->updateResult();
			data.setValue(judge);
			room->setTag("SkipGameRule", (int)triggerEvent);
		} else {
			PhaseChangeStruct change = data.value<PhaseChangeStruct>();
			if (change.to == Player::NotActive) {
				int id = player->getPile("incantation").first();
				ServerPlayer *zhangbao = player->getTag("ZhoufuSource" + QString::number(id)).value<ServerPlayer *>();
				if (zhangbao && zhangbao->isAlive())
					zhangbao->obtainCard(Sanguosha->getCard(id));
			}
		}
		return true;
	}
};

class Yingbing : public TriggerSkillV2
{
public:
	Yingbing() : TriggerSkillV2("yingbing")
	{
		events << StartJudge;
		frequency = Frequent;
	}

	bool usesEventPriority() const override
	{
		return true;
	}

	int getPriority(TriggerEvent) const override
	{
		return -2;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		JudgeStruct *judge = data.value<JudgeStruct *>();
		if (!player || !judge || !judge->card) return TriggerList();
		ServerPlayer *zhangbao = player->getTag("ZhoufuSource" + QString::number(judge->card->getEffectiveId())).value<ServerPlayer *>();
		if (zhangbao && zhangbao->isAlive() && zhangbao->hasSkill(objectName()))
			return TriggerList{{zhangbao, QStringList{objectName()}}};
		return TriggerList();
	}

	bool cost(TriggerEvent, Room *, ServerPlayer *zhangbao, SkillContext &ctx) const override
	{
		return zhangbao->askForSkillInvoke(this, *ctx.original_data);
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *zhangbao, SkillContext &) const override
	{
		room->broadcastSkillInvoke(objectName());
		zhangbao->drawCards(2, objectName());
		return false;
	}
};

class Xingwu : public TriggerSkillV2
{
public:
	Xingwu() : TriggerSkillV2("xingwu")
	{
		events << EventPhaseStart << CardsMoveOneTime;
	}

	static QString pilePattern(ServerPlayer *player)
	{
		int n = player->getMark("xingwu");
		bool red_avail = ((n & 2) == 0), black_avail = ((n & 1) == 0);
		if (player->isKongcheng() || (!red_avail && !black_avail))
			return QString();
		if (red_avail != black_avail)
			return QString(".|%1|.|hand").arg(red_avail ? "red" : "black");
		return ".|.|.|hand";
	}

	bool recordEvent(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (triggerEvent == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
			&& player->getPhase() == Player::RoundStart)
			player->setMark(objectName(), 0);
		return true;
	}

	TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
		if (triggerEvent == EventPhaseStart) {
			if (player->getPhase() != Player::Discard || pilePattern(player).isEmpty()) return TriggerList();
		} else {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to != player || move.to_place != Player::PlaceSpecial || player->getPile(objectName()).length() < 3)
				return TriggerList();
		}
		return TriggerList{{player, QStringList{objectName()}}};
	}

	bool cost(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (triggerEvent != EventPhaseStart) return true;
		const Card *card = room->askForCard(player, pilePattern(player), "@xingwu", QVariant(), Card::MethodNone);
		if (!card) return false;
		ctx.extra_data = card->getEffectiveId();
		return true;
	}

	bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		if (triggerEvent == EventPhaseStart) {
			room->broadcastSkillInvoke(objectName(), 1);

			LogMessage log;
			log.type = "#InvokeSkill";
			log.from = player;
			log.arg = objectName();
			room->sendLog(log);
			room->notifySkillInvoked(player, objectName());

			player->addToPile(objectName(), ctx.extra_data.toInt());
			return false;
		}

		player->clearOnePrivatePile(objectName());
		QList<ServerPlayer *> males;
		foreach (ServerPlayer *p, room->getAlivePlayers()) {
			if (p->isMale())
				males << p;
		}
		if (males.isEmpty()) return false;

		ServerPlayer *target = room->askForPlayerChosen(player, males, objectName(), "@xingwu-choose");
		room->broadcastSkillInvoke(objectName(), 2);
		room->damage(DamageStruct(objectName(), player, target, 2));

		if (!player->isAlive()) return false;
		QList<const Card *> equips = target->getEquips();
		if (!equips.isEmpty()) {
			DummyCard *dummy = new DummyCard;
			foreach (const Card *equip, equips) {
				if (player->canDiscard(target, equip->getEffectiveId()))
					dummy->addSubcard(equip);
			}
			if (dummy->subcardsLength() > 0)
				room->throwCard(dummy, target, player);
			dummy->deleteLater();
		}
		return false;
	}
};

class Luoyan : public TriggerSkillV2
{
public:
	Luoyan(const QString &luoyan) : TriggerSkillV2(luoyan), luoyan(luoyan)
	{
		events << CardsMoveOneTime << EventAcquireSkill << EventLoseSkill;
		frequency = Compulsory;
		waked_skills = "timobileanxiang,liuli";
	}

	// Losing Luoyan must still retract the lent skills, so this path needs no live instance.
	bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (triggerEvent == EventLoseSkill && player && data.value<SkillChangeStruct>().skillName == objectName()) {
			player->setMark(luoyan + "_help", 0);
			room->sendCompulsoryTriggerLog(player, objectName(), true, true);
			room->handleAcquireDetachSkills(player, detachList(), true);
		}
		return true;
	}

	TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName(), true)) return TriggerList();
		bool changed = false;
		if (triggerEvent == EventAcquireSkill) {
			changed = data.value<SkillChangeStruct>().skillName == objectName() && !player->getPile("xingwu").isEmpty();
		} else if (triggerEvent == CardsMoveOneTime) {
			CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
			if (move.to == player && move.to_place == Player::PlaceSpecial && move.to_pile_name == "xingwu")
				changed = !player->getPile("xingwu").isEmpty() && player->getMark(luoyan + "_help") <= 0;
			else if (move.from == player && move.from_places.contains(Player::PlaceSpecial)
				&& move.from_pile_names.contains("xingwu"))
				changed = player->getPile("xingwu").isEmpty() && player->getMark(luoyan + "_help") > 0;
		}
		return changed ? TriggerList{{player, QStringList{objectName()}}} : TriggerList();
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		room->sendCompulsoryTriggerLog(player, objectName(), true, true);
		if (player->getPile("xingwu").isEmpty()) {
			player->setMark(luoyan + "_help", 0);
			room->handleAcquireDetachSkills(player, detachList(), true);
		} else {
			player->setMark(luoyan + "_help", 1);
			room->handleAcquireDetachSkills(player, luoyan == "olluoyan" ? "oltimobileanxiang|liuli" : "timobileanxiang|liuli");
		}
		return false;
	}

private:
	QString detachList() const
	{
		return luoyan == "olluoyan" ? "-oltimobileanxiang|-liuli" : "-timobileanxiang|-liuli";
	}

	QString luoyan;
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

	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		room->broadcastSkillInvoke(objectName());
		player->drawCards(1, "shenxian");
		return false;
	}
};

QiangwuCard::QiangwuCard()
{
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
	QiangwuViewAsSkill() : ViewAsSkillV2("qiangwu")
	{
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& !request.initiator->hasUsed("QiangwuCard");
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		QiangwuCard *card = new QiangwuCard;
		card->setSkillName(objectName());
		return card;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "QiangwuCard";
	}
};

class Qiangwu : public TriggerSkillV2
{
public:
	Qiangwu() : TriggerSkillV2("qiangwu")
	{
		events << PreCardUsed;
		view_as_skill = new QiangwuViewAsSkill;
	}

	// A silent history exemption, never a separate invocation.
	bool recordEvent(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return true;
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.card->isKindOf("Slash") && player->getMark("qiangwu-Clear") > 0
			&& use.card->getNumber() > player->getMark("qiangwu-Clear")) {
			use.m_addHistory = false;
			data = QVariant::fromValue(use);
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
			if (card->getNumber()<from->getMark("qiangwu-Clear"))
				return CorrectSkillResult::useAmount(999);
			if(card->getSkillName().contains("xuanjian")&&XSGongliTrigger(from,"you_pangtong"))
				return CorrectSkillResult::useAmount(999);
			if (from->hasFlag("mobileanxianBf"+card->toString()))
				return CorrectSkillResult::useAmount(999);
		} else if (ctx.modType == TargetModSkill::Residue) {
			if (from->getMark("qiangwu-Clear")>0&&(card->getNumber()>from->getMark("qiangwu-Clear")||card->hasFlag("Global_SlashAvailabilityChecker")))
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
            if (ctx.owner->isAlive()) ctx.owner->drawCards(1, objectName());
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
		events << EventPhaseStart;
		frequency = Wake;
	}

	static bool naturalWake(Room *room, ServerPlayer *guanyu)
	{
		if (guanyu->getHandcardNum() <= guanyu->getHp()) return false;
		ServerPlayer *the_lord = room->getLord();
		return the_lord && (the_lord->getGeneralName().contains("caocao") || the_lord->getGeneral2Name().contains("caocao"));
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Start
			|| player->getMark(objectName()) > 0) return TriggerList();
		if (!naturalWake(room, player) && !hasWakeGrant(player, objectName())) return TriggerList();
		return TriggerList{{player, QStringList{objectName()}}};
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *guanyu, SkillContext &) const override
	{
		if (naturalWake(room, guanyu)) {
			LogMessage log;
			log.type = "#DanjiWake";
			log.from = guanyu;
			log.arg = QString::number(guanyu->getHandcardNum());
			log.arg2 = QString::number(guanyu->getHp());
			room->sendLog(log);
		} else if (!guanyu->canWake(objectName())) {
			return false;
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
	Kangkai() : TriggerSkillV2("kangkai")
	{
		events << TargetConfirmed;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
		CardUseStruct use = data.value<CardUseStruct>();
		if (!use.card || !use.card->isKindOf("Slash")) return TriggerList();
		foreach (ServerPlayer *to, use.to) {
			if (player->distanceTo(to) <= 1)
				return TriggerList{{player, QStringList{objectName()}}};
		}
		return TriggerList();
	}

	// Each nearby target has its own optional prompt, in use order.
	bool effect(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &ctx) const override
	{
		const QVariant data = *ctx.original_data;
		CardUseStruct use = data.value<CardUseStruct>();
		foreach(ServerPlayer*to, use.to){
			if (!player->isAlive()) break;
			if (player->distanceTo(to) <= 1 && player->hasSkill(objectName())){
				player->setTag("KangkaiSlash", data);
				bool will_use = room->askForSkillInvoke(player, objectName(), QVariant::fromValue(to));
				player->removeTag("KangkaiSlash");
				if (!will_use) continue;

				room->broadcastSkillInvoke(objectName());

				player->drawCards(1, "kangkai");
				if (!player->isNude() && player != to){
					const Card*card = nullptr;
					if (player->getCardCount() > 1){
						card = room->askForCard(player, "..!", "@kangkai-give:" + to->objectName(), data, Card::MethodNone);
						if (!card)
							card = player->getCards("he").at(qsanRandomBounded(player->getCardCount()));
					} else {
						Q_ASSERT(player->getCardCount() == 1);
						card = player->getCards("he").first();
					}
					CardMoveReason r(CardMoveReason::S_REASON_GIVE, player->objectName(), objectName(), "");
					room->obtainCard(to, card, r);
					if (card->getTypeId() == Card::TypeEquip && room->getCardOwner(card->getEffectiveId()) == to && !to->isLocked(card)){
						to->setTag("KangkaiSlash", data);
						to->setTag("KangkaiGivenCard", QVariant::fromValue(card));
						bool will_use = room->askForSkillInvoke(to, "kangkai_use", "use");
						to->removeTag("KangkaiSlash");
						to->removeTag("KangkaiGivenCard");
						if (will_use)
							room->useCard(CardUseStruct(card, to));
					}
				}
			}
		}
		return false;
	}
};

class Meibu : public TriggerSkillV2
{
public:
	Meibu() : TriggerSkillV2("meibu")
	{
		events << EventPhaseStart << EventPhaseChanging;
	}

	// The Play-phase grant is undone for its holder, who never owns Meibu.
	bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (triggerEvent != EventPhaseChanging || !player) return true;
		PhaseChangeStruct change = data.value<PhaseChangeStruct>();
		if (change.to != Player::NotActive) return true;

		QVariantList sunluyus = player->getTag(objectName()).toList();
		foreach(QVariant sunluyu, sunluyus){
			ServerPlayer*s = sunluyu.value<ServerPlayer*>();
			room->removeAttackRangePair(player, s);
		}
		room->detachSkillFromPlayer(player, "#meibu-filter");

		player->setTag(objectName(), QVariantList());

		room->filterCards(player, player->getCards("he"), true);
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

	bool cost(TriggerEvent, Room *room, ServerPlayer *sunluyu, SkillContext &) const override
	{
		return room->askForSkillInvoke(sunluyu, objectName());
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *sunluyu, SkillContext &ctx) const override
	{
		ServerPlayer *player = ctx.invoker;
		room->broadcastSkillInvoke(objectName());
		if (!player->hasSkill("#meibu-filter", true)){
			room->acquireSkill(player, "#meibu-filter", false);
			room->filterCards(player, player->getCards("he"), false);
		}
		QVariantList sunluyus = player->getTag(objectName()).toList();
		sunluyus << QVariant::fromValue(sunluyu);
		player->setTag(objectName(), sunluyus);
		room->insertAttackRangePair(player, sunluyu);
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
			|| player->getMark("damage_point_play_phase") != 0) return TriggerList();
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
			player->drawCards(1, objectName());
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
	Junbing() : TriggerSkillV2("junbing")
	{
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive() || player->getPhase() != Player::Finish || player->getHandcardNum() > 1)
			return TriggerList();
		ServerPlayer*simalang = room->findPlayerBySkillName(objectName());
		if (!simalang || !simalang->isAlive())
			return TriggerList();
		return TriggerList{{simalang, QStringList{objectName()}}};
	}

	// The finishing player decides; Sima Lang only owns the skill.
	bool cost(TriggerEvent, Room *, ServerPlayer *simalang, SkillContext &ctx) const override
	{
		return ctx.invoker->askForSkillInvoke(this, QString("junbing_invoke:%1").arg(simalang->objectName()));
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *simalang, SkillContext &ctx) const override
	{
		ServerPlayer *player = ctx.invoker;
		room->broadcastSkillInvoke(objectName());
		room->notifySkillInvoked(simalang, objectName());
		player->drawCards(1,objectName());
		if (player->objectName() != simalang->objectName()){
			CardMoveReason reason = CardMoveReason(CardMoveReason::S_REASON_GIVE, player->objectName());
			DummyCard*cards = player->wholeHandCards();
			room->moveCardTo(cards, simalang, Player::PlaceHand, reason);

			int x = qMin(cards->subcardsLength(), simalang->getHandcardNum());

			if (x > 0){
				const Card*return_cards = room->askForExchange(simalang, objectName(), x, x, false, QString("@junbing-return:%1::%2").arg(player->objectName()).arg(cards->subcardsLength()));
				CardMoveReason return_reason = CardMoveReason(CardMoveReason::S_REASON_GIVE, simalang->objectName());
				room->moveCardTo(return_cards, player, Player::PlaceHand, return_reason);
			}
		}
		return false;
	}
};

QujiCard::QujiCard()
{
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
	Quji() : ViewAsSkillV2("quji")
	{
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& request.initiator->isWounded() && !request.initiator->hasUsed("QujiCard");
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return matchesFilter(request, card, ".") && request.selectedCardIds.length() < request.initiator->getLostHp();
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.initiator && request.selectedCardIds.length() == request.initiator->getLostHp()
			&& replaySelection(this, request);
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		QujiCard*quji = new QujiCard;
		quji->addSubcards(request.selectedCardIds);
		quji->setSkillName(objectName());
		return quji;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "QujiCard";
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
		player->drawCards(1, objectName());
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
	BifaViewAsSkill() : ViewAsSkillV2("bifa", 1)
	{
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return isPromptRequest(request, "@@bifa");
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return ViewAsSkillV2::canSelectCard(request, card) && matchesFilter(request, card, ".|.|.|hand");
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.selectedCardIds.size() == 1 && replaySelection(this, request);
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		// Other packages still recognise Bifa uses by the BifaCard class.
		Card *card = new BifaCard;
		card->addSubcards(request.selectedCardIds);
		card->setSkillName(objectName());
		return card;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "BifaCard";
	}
};

class Bifa : public TriggerSkillV2
{
public:
	Bifa() : TriggerSkillV2("bifa")
	{
		events << EventPhaseStart;
		view_as_skill = new BifaViewAsSkill;
	}

	// The buried card resolves for its holder even after Chen Lin is gone.
	bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || player->getPhase() != Player::RoundStart || player->getPile("bifa").isEmpty()) return true;
		int card_id = player->getPile("bifa").first();
		ServerPlayer*chenlin = player->getTag("BifaSource" + QString::number(card_id)).value<ServerPlayer*>();
		QList<int> ids;
		ids << card_id;

		LogMessage log;
		log.type = "$BifaView";
		log.from = player;
		log.card_str = QString::number(card_id);
		log.arg = "bifa";
		room->sendLog(log, player);

		room->fillAG(ids, player);
		const Card*cd = Sanguosha->getCard(card_id);
		QString pattern;
		if (cd->isKindOf("BasicCard"))
			pattern = "BasicCard";
		else if (cd->isKindOf("TrickCard"))
			pattern = "TrickCard";
		else if (cd->isKindOf("EquipCard"))
			pattern = "EquipCard";
		QVariant data_for_ai = QVariant::fromValue(pattern);
		pattern.append("|.|.|hand");
		const Card*to_give = nullptr;
		if (!player->isKongcheng() && chenlin && chenlin->isAlive())
			to_give = room->askForCard(player, pattern, "@bifa-give", data_for_ai, Card::MethodNone, chenlin);
		if (chenlin && to_give){
			room->broadcastSkillInvoke(objectName(), 2);
			CardMoveReason reasonG(CardMoveReason::S_REASON_GIVE, player->objectName(), chenlin->objectName(), "bifa", "");
			room->obtainCard(chenlin, to_give, reasonG, false);
			CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, player->objectName(), "bifa", "");
			room->obtainCard(player, cd, reason, false);
		} else {
			room->broadcastSkillInvoke(objectName(), 3);
			CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, "", objectName(), "");
			room->throwCard(cd, reason, nullptr);
			room->loseHp(HpLostStruct(player, 1, "bifa", chenlin));
		}
		room->clearAG(player);
		player->removeTag("BifaSource" + QString::number(card_id));
		return true;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (player && player->isAlive() && player->hasSkill(objectName())
			&& player->getPhase() == Player::Finish && !player->isKongcheng())
			return TriggerList{{player, QStringList{objectName()}}};
		return TriggerList();
	}

	// Declining the prompt means Bifa was never invoked; the card use is its own activation.
	bool cost(TriggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		return room->askForUseCard(player, "@@bifa", "@bifa-remove", -1, Card::MethodNone);
	}

	int getEffectIndex(const ServerPlayer*, const Card*) const override
	{
		return 1;
	}
};

SongciCard::SongciCard()
{
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
	SongciViewAsSkill() : ViewAsSkillV2("songci")
	{
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const Player *player = request.initiator;
		if (!player || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
		if (player->getMark("songci" + player->objectName()) == 0 && player->getHandcardNum() != player->getHp()) return true;
		foreach(const Player*sib, player->getAliveSiblings())
			if (sib->getMark("songci" + player->objectName()) == 0 && sib->getHandcardNum() != sib->getHp())
				return true;
		return false;
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		SongciCard *card = new SongciCard;
		card->setSkillName(objectName());
		return card;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "SongciCard";
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
		if (!player || !player->hasSkill(objectName())) return true;
		DeathStruct death = data.value<DeathStruct>();
		if (death.who != player) return true;
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
	YinbingViewAsSkill() : ViewAsSkillV2("yinbing")
	{
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return isPromptRequest(request, "@@yinbing");
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return matchesFilter(request, card, ".") && card->getTypeId() != Card::TypeBasic;
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return !request.selectedCardIds.isEmpty() && replaySelection(this, request);
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		Card*acard = new YinbingCard;
		acard->addSubcards(request.selectedCardIds);
		acard->setSkillName(objectName());
		return acard;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "YinbingCard";
	}
};

class Yinbing : public TriggerSkillV2
{
public:
	Yinbing() : TriggerSkillV2("yinbing")
	{
		events << EventPhaseStart << Damaged;
		view_as_skill = new YinbingViewAsSkill;
	}

	TriggerList triggerable(TriggerEvent triggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
	{
		if (!player || !player->isAlive() || !player->hasSkill(objectName())) return TriggerList();
		if (triggerEvent == EventPhaseStart) {
			if (player->getPhase() != Player::Finish || player->isNude()) return TriggerList();
		} else {
			DamageStruct damage = data.value<DamageStruct>();
			if (player->getPile("yinbing").isEmpty() || !damage.card
				|| !(damage.card->isKindOf("Slash") || damage.card->isKindOf("Duel"))) return TriggerList();
		}
		return TriggerList{{player, QStringList{objectName()}}};
	}

	// Declining the Finish prompt means Yinbing was never invoked; the loss is mandatory.
	bool cost(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (triggerEvent != EventPhaseStart) return true;
		return room->askForUseCard(player, "@@yinbing", "@yinbing", -1, Card::MethodNone);
	}

	bool effect(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, SkillContext &) const override
	{
		if (triggerEvent != Damaged || player->getPile("yinbing").isEmpty()) return false;
		room->sendCompulsoryTriggerLog(player, objectName());

		QList<int> ids = player->getPile("yinbing");
		room->fillAG(ids, player);
		int id = room->askForAG(player, ids, false, objectName());
		room->clearAG(player);
		room->throwCard(id, nullptr);
		return false;
	}
};

class Juedi : public TriggerSkillV2
{
public:
	Juedi() : TriggerSkillV2("juedi")
	{
		events << EventPhaseStart;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *target, QVariant &) const override
	{
		if (target && target->isAlive() && target->hasSkill(objectName()) && target->getPhase() == Player::Start
			&& !target->getPile("yinbing").isEmpty())
			return TriggerList{{target, QStringList{objectName()}}};
		return TriggerList();
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *target, SkillContext &) const override
	{
		return room->askForSkillInvoke(target, objectName());
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *target, SkillContext &) const override
	{
		room->broadcastSkillInvoke(objectName());

		QList<ServerPlayer*> playerlist;
		foreach(ServerPlayer*p, room->getOtherPlayers(target)){
			if (p->getHp() <= target->getHp())
				playerlist << p;
		}
		ServerPlayer*to_give = nullptr;
		if (!playerlist.isEmpty())
			to_give = room->askForPlayerChosen(target, playerlist, objectName(), "@juedi", true);
		if (to_give){
			room->recover(to_give, RecoverStruct("juedi", target));
			DummyCard*dummy = new DummyCard(target->getPile("yinbing"));
			room->obtainCard(to_give, dummy);
			dummy->deleteLater();
		} else {
			int len = target->getPile("yinbing").length();
			target->clearOnePrivatePile("yinbing");
			if (target->isAlive())
				room->drawCards(target, len, objectName());
		}
		return false;
	}
};

class SpZhenwei : public TriggerSkillV2
{
public:
	SpZhenwei() : TriggerSkillV2("spzhenwei")
	{
		events << TargetConfirming << EventPhaseChanging;
	}

	// Cards set aside by Zhenwei return to their users at every turn end.
	bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
	{
		if (triggerEvent != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
		foreach(ServerPlayer*p, room->getAllPlayers()){
			if (!p->getPile("zhenweipile").isEmpty()){
				DummyCard*dummy = new DummyCard(p->getPile("zhenweipile"));
				room->obtainCard(p, dummy);
				dummy->deleteLater();
			}
		}
		return true;
	}

	TriggerList triggerable(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (triggerEvent != TargetConfirming || !player) return TriggerList();
		CardUseStruct use = data.value<CardUseStruct>();
		if (use.to.length() != 1 || !(use.card->isKindOf("Slash") || (use.card->getTypeId() == Card::TypeTrick && use.card->isBlack())))
			return TriggerList();
		ServerPlayer*wp = room->findPlayerBySkillName(objectName());
		if (wp == nullptr || wp->getHp() <= player->getHp() || !wp->canDiscard(wp, "he"))
			return TriggerList();
		return TriggerList{{wp, QStringList{objectName()}}};
	}

	bool cost(TriggerEvent, Room *room, ServerPlayer *wp, SkillContext &ctx) const override
	{
		const Card *card = room->askForCard(wp, "..", QString("@sp_zhenwei:%1").arg(ctx.invoker->objectName()),
			*ctx.original_data, Card::MethodNone, nullptr, false, objectName());
		if (!card || card->isVirtualCard() || !wp->canDiscard(wp, card->getEffectiveId())) return false;
		ctx.extra_data = card->getEffectiveId();
		return true;
	}

	bool pay(TriggerEvent, Room *room, ServerPlayer *wp, SkillContext &ctx) const override
	{
		bool ok = false;
		const int id = ctx.extra_data.toInt(&ok);
		if (!ok || room->getCardOwner(id) != wp || !wp->canDiscard(wp, id)) return false;
		room->throwCard(id, objectName(), wp);
		return true;
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *wp, SkillContext &ctx) const override
	{
		CardUseStruct use = ctx.original_data->value<CardUseStruct>();
		room->broadcastSkillInvoke(objectName());
		if (room->askForChoice(wp, objectName(), "draw+null", *ctx.original_data) == "draw"){
			room->drawCards(wp, 1, objectName());
			if (use.card->isKindOf("Slash")){
				if (!use.from->canSlash(wp, use.card, false))
					return false;
			}
			if (use.card->isKindOf("DelayedTrick")){
				if (!use.from||use.from->isProhibited(wp, use.card))
					return false;
				room->moveCardTo(use.card, wp, Player::PlaceDelayedTrick, true);
			} else {
				if (use.from->isProhibited(wp, use.card))
					return false;
				use.to.clear();
				use.to << wp;
				*ctx.original_data = QVariant::fromValue(use);
			}
		} else {
			room->setCardFlag(use.card, "zhenweinull");
			if(use.from)
				use.from->addToPile("zhenweipile", use.card);
			use.nullified_list << "_ALL_TARGETS";
			*ctx.original_data = QVariant::fromValue(use);
		}
		return false;
	}
};

class AocaiViewAsSkill : public ViewAsSkillV2
{
public:
	AocaiViewAsSkill() : ViewAsSkillV2("aocai")
	{
	}

	static bool hasBasicName(const QString &names)
	{
		foreach(QString cn, names.split("+")){
			Card*c = Sanguosha->cloneCard(cn);
			if (c){
				c->deleteLater();
				if (c->getTypeId()==Card::TypeBasic)
					return true;
			}
		}
		return false;
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const Player *player = request.initiator;
		if (!player || player->hasFlag("CurrentPlayer")) return false;
		if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return true;
		if (player->hasFlag("Global_AocaiFailed")) return false;
		return request.pattern == "@@aocai" || hasBasicName(request.pattern);
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		if (request.pattern == "@@aocai") {
			// Use the revealed card through a conversion; the server must never own an engine card.
			const Card *revealed = Sanguosha->getCard(request.initiator->getMark("aocaiId"));
			if (!revealed) return nullptr;
			Card *card = Sanguosha->cloneCard(revealed->objectName(), revealed->getSuit(), revealed->getNumber());
			if (!card) return nullptr;
			card->addSubcard(revealed);
			card->setSkillName(objectName());
			return card;
		}
		// AocaiCard::validate still reveals the draw pile when the card is actually used.
		AocaiCard*aocai_card = new AocaiCard;
		aocai_card->setSkillName(objectName());
		if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY) {
			// Clients declare the asked pattern; server rebuilds keep the submitted names.
			const QString names = request.userString.isEmpty() ? request.pattern : request.userString;
			if (!hasBasicName(names)) {
				delete aocai_card;
				return nullptr;
			}
			aocai_card->setUserString(names);
		}
		return aocai_card;
	}

	QString historyKey(const ActiveSkillRequest &request) const override
	{
		return cardHistoryKey(request.userString, "AocaiCard");
	}
};

class Aocai : public TriggerSkillV2
{
public:
	Aocai() : TriggerSkillV2("aocai")
	{
		events << CardUsed;
		view_as_skill = new AocaiViewAsSkill;
	}

	// Any card use re-opens every failed Aocai window.
	bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
	{
		if (!player || !player->isAlive()) return true;
		foreach(ServerPlayer*p, room->getPlayers()){
			room->setPlayerFlag(p, "-Global_AocaiFailed");
		}
		return true;
	}

	static int view(Room*room, ServerPlayer*player, QList<int> &ids, QList<int> &enabled, QList<int> &disabled)
	{
		int result = -1, index = -1;
		LogMessage log;
		log.type = "$ViewDrawPile";
		log.from = player;
		log.card_str = ListI2S(ids).join("+");
		room->sendLog(log, player);

		room->broadcastSkillInvoke("aocai");
		room->notifySkillInvoked(player, "aocai");
		if (enabled.isEmpty()){
			JsonArray arg;
			arg << "." << false << JsonUtils::toJsonArray(ids);
			room->doNotify(player, QSanProtocol::S_COMMAND_SHOW_ALL_CARDS, arg);
		} else {
			room->fillAG(ids, player, disabled);
			int id = room->askForAG(player, enabled, true, "aocai");
			if (id > -1){
				index = ids.indexOf(id);
				ids.removeOne(id);
				result = id;
			}
			room->clearAG(player);
		}

		room->returnToTopDrawPile(ids);
		if (result == -1)
			room->setPlayerFlag(player, "Global_AocaiFailed");
		else {
			LogMessage log;
			log.type = "#AocaiUse";
			log.from = player;
			log.arg = "aocai";
			log.arg2 = QString::number(index + 1);
			room->sendLog(log);
		}
		return result;
	}
};

AocaiCard::AocaiCard()
{
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

const Card*AocaiCard::validateInResponse(ServerPlayer*user) const
{
	Room*room = user->getRoom();
	QList<int> ids = room->getNCards(2);
	QStringList names = user_string.split("+");
	//if (names.contains("slash")) names << "fire_slash" << "thunder_slash";

	QList<int> enabled, disabled;
	foreach(int id, ids){
		const Card*c = Sanguosha->getCard(id);
		if (user->isCardLimited(c,Card::MethodResponse)){
			disabled << id;
			continue;
		}
		foreach(QString cn, names){
			if (c->objectName().endsWith(cn)){
				enabled << id;
				break;
			}
		}
		if (enabled.contains(id)) continue;
		disabled << id;
	}

	LogMessage log;
	log.type = "#InvokeSkill";
	log.from = user;
	log.arg = "aocai";
	room->sendLog(log);

	int id = Aocai::view(room, user, ids, enabled, disabled);
	return Sanguosha->getCard(id);
}

const Card*AocaiCard::validate(CardUseStruct &cardUse) const
{
	cardUse.m_isOwnerUse = false;
	Room*room = cardUse.from->getRoom();
	QList<int> ids = room->getNCards(2);
	QStringList names = user_string.split("+");
	//if (names.contains("slash")) names << "fire_slash" << "thunder_slash";

	QList<int> enabled, disabled;
	foreach(int id, ids){
		const Card*c = Sanguosha->getCard(id);
		if (cardUse.from->isLocked(c)){
			disabled << id;
			continue;
		}
		foreach(QString cn, names){
			if (user_string.isEmpty()){
				if (c->getTypeId()==1&&c->isAvailable(cardUse.from)){
					enabled << id;
					break;
				}
			}else if (c->objectName().endsWith(cn)){
				enabled << id;
				break;
			}
		}
		if (enabled.contains(id)) continue;
		disabled << id;
	}

	LogMessage log;
	log.type = "#InvokeSkill";
	log.from = cardUse.from;
	log.arg = "aocai";
	room->sendLog(log);
	int id = Aocai::view(room, cardUse.from, ids, enabled, disabled);
	if (user_string.isEmpty()&&id>=0){
		room->setPlayerMark(cardUse.from,"aocaiId",id);
		room->askForUseCard(cardUse.from,"@@aocai","aocai0:"+Sanguosha->getCard(id)->objectName());
		return nullptr;
	}
	return Sanguosha->getCard(id);
}

DuwuCard::DuwuCard()
{
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
	DuwuViewAsSkill() : ViewAsSkillV2("duwu")
	{
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		const Player *player = request.initiator;
		return player && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
			&& player->canDiscard(player, "he") && !player->hasFlag("DuwuEnterDying");
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return matchesFilter(request, card, ".") && !request.initiator->isJilei(card);
	}

	// Any number of cards, including none: DuwuCard matches the count to the target's HP.
	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.initiator && replaySelection(this, request);
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		DuwuCard*duwu = new DuwuCard;
		duwu->addSubcards(request.selectedCardIds);
		duwu->setSkillName(objectName());
		return duwu;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "DuwuCard";
	}
};

class Duwu : public TriggerSkillV2
{
public:
	Duwu() : TriggerSkillV2("duwu")
	{
		events << QuitDying;
		view_as_skill = new DuwuViewAsSkill;
	}

	// The backlash follows the Duwu damage itself, not the source's current skills.
	bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
	{
		DyingStruct dying = data.value<DyingStruct>();
		if (dying.damage && dying.damage->getReason() == "duwu" && !dying.damage->chain && !dying.damage->transfer){
			ServerPlayer*from = dying.damage->from;
			if (from && from->isAlive()){
				room->setPlayerFlag(from, "DuwuEnterDying");
				room->loseHp(HpLostStruct(from, 1, "duwu", from));
			}
		}
		return true;
	}
};

YuanhuCard::YuanhuCard()
{
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
	YuanhuViewAsSkill() : ViewAsSkillV2("yuanhu", 1)
	{
	}

	bool canActivate(const ActiveSkillRequest &request) const override
	{
		return isPromptRequest(request, "@@yuanhu");
	}

	bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
	{
		return ViewAsSkillV2::canSelectCard(request, card) && matchesFilter(request, card, "EquipCard");
	}

	bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
	{
		return request.selectedCardIds.size() == 1 && replaySelection(this, request);
	}

	const Card *createCard(const ActiveSkillRequest &request) const override
	{
		if (!cardSelectionFeasible(request)) return nullptr;
		// YuanhuCard still owns the target rule, equipment movement and reward.
		YuanhuCard *first = new YuanhuCard;
		first->addSubcards(request.selectedCardIds);
		first->setSkillName(objectName());
		return first;
	}

	QString historyKey(const ActiveSkillRequest &) const override
	{
		return "YuanhuCard";
	}
};

class Yuanhu : public TriggerSkillV2
{
public:
	Yuanhu() : TriggerSkillV2("yuanhu")
	{
		events << EventPhaseStart;
		view_as_skill = new YuanhuViewAsSkill;
	}

	TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
	{
		if (player && player->isAlive() && player->hasSkill(objectName())
			&& player->getPhase() == Player::Finish && !player->isNude())
			return TriggerList{{player, QStringList{objectName()}}};
		return TriggerList();
	}

	// Declining the prompt means Yuanhu was never invoked; the card use is its own activation.
	bool cost(TriggerEvent, Room *room, ServerPlayer *target, SkillContext &) const override
	{
		return room->askForUseCard(target, "@@yuanhu", "@yuanhu-equip", -1, Card::MethodNone);
	}
};

class Baobian : public TriggerSkillV2
{
public:
	Baobian() : TriggerSkillV2("baobian")
	{
		events << GameStart << HpChanged << MaxHpChanged << EventAcquireSkill << EventLoseSkill;
		frequency = Compulsory;
	}

	// Lent skills follow HP as state, and must also be retracted after Baobian is lost.
	bool recordEvent(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
	{
		if (!player) return true;
		if (triggerEvent == EventLoseSkill){
			if (data.value<SkillChangeStruct>().skillName == objectName()){
				QStringList baobian_skills = player->getTag("BaobianSkills").toStringList();
				QStringList detachList;
				foreach(QString skill_name, baobian_skills)
					detachList.append("-" + skill_name);
				room->handleAcquireDetachSkills(player, detachList);
				player->setTag("BaobianSkills", QVariant());
			}
			return true;
		} else if (triggerEvent == EventAcquireSkill){
			if (data.value<SkillChangeStruct>().skillName != objectName()) return true;
		}

		if (!player->isAlive() || !player->hasSkill(objectName(), true)) return true;

		QStringList acquired_skills, detached_skills;
		BaobianChange(room, player, 1, "shensu", acquired_skills, detached_skills);
		BaobianChange(room, player, 2, "paoxiao", acquired_skills, detached_skills);
		BaobianChange(room, player, 3, "tiaoxin", acquired_skills, detached_skills);
		if (!acquired_skills.isEmpty() || !detached_skills.isEmpty())
			room->handleAcquireDetachSkills(player, acquired_skills + detached_skills);
		return true;
	}

private:
	static void BaobianChange(Room*room, ServerPlayer*player, int hp, const QString &skill_name,
		QStringList &acquired_skills, QStringList &detached_skills)
	{
		QStringList baobian_skills = player->getTag("BaobianSkills").toStringList();
		if (player->getHp() <= hp){
			if (!baobian_skills.contains(skill_name)){
				room->notifySkillInvoked(player, "baobian");
				if (player->getHp() == hp)
					room->broadcastSkillInvoke("baobian", 4 - hp);
				acquired_skills.append(skill_name);
				baobian_skills << skill_name;
			}
		} else {
			if (baobian_skills.contains(skill_name)){
				detached_skills.append("-" + skill_name);
				baobian_skills.removeOne(skill_name);
			}
		}
		player->setTag("BaobianSkills", QVariant::fromValue(baobian_skills));
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
