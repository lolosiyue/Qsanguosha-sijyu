#include "yjcm2015.h"
#include <QJsonDocument>
#include "skill-declaration.h"
#include "skill-instance-utils.h"
#include <QScopeGuard>
//#include "general.h"
//#include "player.h"
//#include "structs.h"
#include "room.h"
//#include "skill.h"
//#include "standard.h"
#include "engine.h"
#include "clientplayer.h"
//#include "clientstruct.h"
#include "settings.h"
#include "wrapped-card.h"
#include "roomthread.h"
#include "standard-cards.h"
#include "standard-generals.h"
//#include "json.h"

class Huituo : public TriggerSkillV2
{
public:
    Huituo() : TriggerSkillV2("huituo")
    {
        events << Damaged;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(this)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(),
            "@huituo-select", true, false);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner || !ctx.original_data || !target || getEffectiveAmount(ctx) <= 0) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        JudgeStruct judge;
        judge.who = target;
        room->broadcastSkillInvoke(objectName());
        judge.pattern = ".";
        judge.play_animation = false;
        judge.reason = "huituo";
        room->judge(judge);

        if (!judge.card || !target->isAlive()) return false;
        if (judge.card->getColorString() == "red") {
            RecoverStruct recover(objectName(), ctx.owner);
            recover.recover = getEffectiveAmount(ctx);
            room->recover(target, recover);
        }
        else if (judge.card->getColorString() == "black")
            room->drawCards(target, damage.damage * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class NosMingjian : public TriggerSkillV2
{
public:
    NosMingjian() : TriggerSkillV2("nosmingjian") { events << EventPhaseChanging; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(this) && !player->isSkipped(Player::Play)
            && data.value<PhaseChangeStruct>().to == Player::Play ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(),
            "@nosmingjian-give", true, false);
        if (!target) return false;
        ctx.targets = {target};
        ctx.extra_data = false;
        ctx.manual_effect = true;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        for (ServerPlayer *target : ctx.targets) skillEffect(event, room, ctx.owner, ctx, target);
        if (ctx.extra_data.toBool()) throw TurnBroken;
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner || !target) return false;
        // This awarded phase belongs to an applied effect and must survive source removal during the gift.
        QVariantList pending = ctx.owner->getTag("NosMingjianPhases").toList();
        pending << QVariantMap{{"target", target->objectName()}, {"owner", ctx.sourceRef.ownerObjectName},
            {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}};
        ctx.owner->setTag("NosMingjianPhases", pending);
        ctx.extra_data = true;
        const QList<int> ids = ctx.owner->handCards();
        if (!ids.isEmpty()) {
            DummyCard cards(ids);
            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, ctx.owner->objectName(), target->objectName(), objectName(), "");
            room->obtainCard(target, &cards, reason, false);
        }
        return false;
    }
};

class NosMingjianGive : public TriggerSkillV2
{
public:
    NosMingjianGive() : TriggerSkillV2("#nosmingjian-give") { events << EventPhaseStart; global = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || player->getPhase() != Player::NotActive) return true;
        const QVariantList pending = player->getTag("NosMingjianPhases").toList();
        player->removeTag("NosMingjianPhases"); // Consume before phase callbacks can recurse.
        for (const QVariant &entry : pending) {
            ServerPlayer *target = room->findPlayerByObjectName(entry.toMap().value("target").toString());
            if (!target || !target->isAlive()) continue;
            target->changePhase(target->getPhase(), Player::Play);
            target->changePhase(target->getPhase(), Player::NotActive);
        }
        return true;
    }
};
class Xingshuai : public TriggerSkillV2
{
public:
    Xingshuai() : TriggerSkillV2("xingshuai$") { events << EventSkillInvoking << Dying; global = true; limit_mark = "@xingshuaiMark"; frequency = Limited; }
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
        if (ctx.owner->getMark(limit_mark) > 0) room->removePlayerMark(ctx.owner, limit_mark);
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
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Dying) return {};
        return player && data.value<DyingStruct>().who == player && player->isAlive() && player->getHp() <= 0
            && player->hasLordSkill(this) && !room->getLieges("wei", player).isEmpty() ? TriggerList{{player, usableEntries(player, data)}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false; return ctx.owner && ctx.original_data && room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {

        room->doSuperLightbox(ctx.owner, objectName());
        QList<SkillContext> accepted;
        for (ServerPlayer *liege : room->getLieges("wei", ctx.owner)) {
            if (!ctx.owner->isAlive()) break;
            SkillContext assist = ctx;
            assist.choice = "assist";
            assist.extra_data = QStringList();
            skillEffect(event, room, player, assist, liege);
            if (!assist.extra_data.toStringList().isEmpty()) accepted << assist;
        }
        for (const SkillContext &assist : accepted) {
            for (const QString &name : assist.extra_data.toStringList()) {
                ServerPlayer *liege = room->findPlayerByObjectName(name);
                SkillContext damage = assist;
                damage.choice = "damage";
                if (liege) skillEffect(event, room, player, damage, liege);
            }
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "recover") {
            ServerPlayer *liege = room->findPlayerByObjectName(ctx.extra_data.toString(), true);
            RecoverStruct recovery(objectName(), liege);
            recovery.recover = getEffectiveAmount(ctx);
            room->recover(target, recovery);
        } else if (ctx.choice == "damage") room->damage(DamageStruct(objectName(), nullptr, target, getEffectiveAmount(ctx)));
        else if (target->askForSkillInvoke("_xingshuai", "xing:" + ctx.owner->objectName())) {
            // Commit the helper's obligation before nested recovery can retire the source.
            QStringList accepted = ctx.extra_data.toStringList();
            accepted << target->objectName();
            ctx.extra_data = accepted;
            SkillContext recovery = ctx;
            recovery.choice = "recover";
            recovery.extra_data = target->objectName();
            skillEffect(event, room, player, recovery, ctx.owner);
        }
        return false;
    }
};
class Taoxi : public TriggerSkill
{
public:
    Taoxi() : TriggerSkill("taoxi")
    {
        events << TargetSpecified << CardsMoveOneTime << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TargetSpecified && TriggerSkill::triggerable(player)
            && !player->hasFlag("TaoxiUsed") && player->getPhase() == Player::Play) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card && use.card->getTypeId() != Card::TypeSkill && use.to.length() == 1) {
                ServerPlayer *to = use.to.first();
                player->setTag("taoxi_carduse", data);
                if (to != player && !to->isKongcheng() && player->askForSkillInvoke(objectName(), QVariant::fromValue(to))) {
                    room->broadcastSkillInvoke(objectName());
                    room->setPlayerFlag(player, "TaoxiUsed");
                    room->setPlayerFlag(player, "TaoxiRecord");
                    int id = room->askForCardChosen(player, to, "h", objectName(), false);
                    room->showCard(to, id);
                    TaoxiMove(id, true, player);
                    player->setTag("TaoxiId", id);
                }
            }
        } else if (triggerEvent == CardsMoveOneTime && player->hasFlag("TaoxiRecord")) {
            bool ok = false;
            int id = player->getTag("TaoxiId").toInt(&ok);
            if (!ok) {
                room->setPlayerFlag(player, "-TaoxiRecord");
                return false;
            }
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != nullptr && move.card_ids.contains(id)) {
                if (move.from_places[move.card_ids.indexOf(id)] == Player::PlaceHand) {
                    TaoxiMove(id, false, player);
                    if (room->getCardOwner(id) != nullptr)
                        room->showCard(room->getCardOwner(id), id);
                    room->setPlayerFlag(player, "-TaoxiRecord");
                    player->removeTag("TaoxiId");
                }
            }
        } else if (triggerEvent == EventPhaseChanging && player->hasFlag("TaoxiRecord")) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive)
                return false;
            bool ok = false;
            int id = player->getTag("TaoxiId").toInt(&ok);
            if (!ok) {
                room->setPlayerFlag(player, "-TaoxiRecord");
                return false;
            }

            if (TaoxiHere(player))
                TaoxiMove(id, false, player);

            ServerPlayer *owner = room->getCardOwner(id);
            if (owner && room->getCardPlace(id) == Player::PlaceHand) {
                room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                room->showCard(owner, id);
                room->loseHp(HpLostStruct(player, 1, objectName(), player));
                room->setPlayerFlag(player, "-TaoxiRecord");
                player->removeTag("TaoxiId");
            }
        }
        return false;
    }

private:
    static void TaoxiMove(int id, bool movein, ServerPlayer *caoxiu)
    {
        Room *room = caoxiu->getRoom();
		QList<CardsMoveStruct> moves;
        if (movein) {
            CardsMoveStruct move(id, room->getCardOwner(id), caoxiu, Player::PlaceTable, Player::PlaceSpecial,
                CardMoveReason(CardMoveReason::S_REASON_PUT, caoxiu->objectName(), "taoxi", ""));
            move.to_pile_name = "&taoxi";
            moves.append(move);
        } else {
            CardsMoveStruct move(id, caoxiu, nullptr, Player::PlaceSpecial, Player::PlaceTable,
                CardMoveReason(CardMoveReason::S_REASON_PUT, caoxiu->objectName(), "taoxi", ""));
            move.from_pile_name = "&taoxi";
            moves.append(move);
        }
		QList<ServerPlayer *> _caoxiu;
		_caoxiu << caoxiu;
		room->notifyMoveCards(true, moves, false, _caoxiu);
		room->notifyMoveCards(false, moves, false, _caoxiu);
        caoxiu->setTag("TaoxiHere", movein);
    }

    static bool TaoxiHere(ServerPlayer *caoxiu)
    {
        return caoxiu->getTag("TaoxiHere", false).toBool();
    }
};

HuaiyiCard::HuaiyiCard()
{
    setSkillName("huaiyi");
    target_fixed = true;
}

void HuaiyiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->showAllCards(source);

    QList<int> blacks;
    QList<int> reds;
    foreach (const Card *c, source->getHandcards()) {
        if (c->isRed())
            reds << c->getId();
        else
            blacks << c->getId();
    }

    if (reds.isEmpty() || blacks.isEmpty())
        return;

    QString to_discard = room->askForChoice(source, "huaiyi", "black+red");
    QList<int> *pile = nullptr;
    if (to_discard == "black")
        pile = &blacks;
    else
        pile = &reds;

    int n = pile->length();

    room->setPlayerMark(source, "huaiyi_num", n);

    DummyCard dm(*pile);
    room->throwCard(&dm, source);

    room->askForUseCard(source, "@@huaiyi", "@huaiyi:::" + QString::number(n), -1, Card::MethodNone);
}

HuaiyiSnatchCard::HuaiyiSnatchCard()
{
    handling_method = Card::MethodNone;
    m_skillName = "_huaiyi";
}

bool HuaiyiSnatchCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    int n = Self->getMark("huaiyi_num");
    if (targets.length() >= n)
        return false;

    if (to_select == Self)
        return false;

    if (to_select->isNude())
        return false;

    return true;
}

void HuaiyiSnatchCard::onUse(Room *room, CardUseStruct &card_use) const
{
    ServerPlayer *player = card_use.from;

    QList<ServerPlayer *> to = card_use.to;

    room->sortByActionOrder(to);

    int get = 0;
    foreach (ServerPlayer *p, to) {
        if (player->isDead()) return;
        if (p->isDead() || p->isNude()) continue;
        int id = room->askForCardChosen(player, p, "he", "huaiyi");
        player->obtainCard(Sanguosha->getCard(id), false);
        get++;
    }

    if (get >= 2)
        room->loseHp(HpLostStruct(player, 1, "huaiyi", player));
}

class Huaiyi : public ViewAsSkillV2
{
public:
    Huaiyi() : ViewAsSkillV2("huaiyi") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? !request.initiator->isKongcheng()
            : request.pattern == "@@huaiyi" && selectionLimit(request) > 0;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    {
        return request.reason != CardUseStruct::CARD_USE_REASON_PLAY && candidate && candidate->isAlive()
            && candidate != request.initiator && !candidate->isNude() && !selected.contains(candidate)
            && selected.size() < selectionLimit(request);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? selected.isEmpty()
            : !selected.isEmpty() && selected.size() <= selectionLimit(request);
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    { return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? "HuaiyiCard" : "HuaiyiSnatchCard"; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        const Card *card = ViewAsSkillV2::createCard(request);
        if (card) card->setTag("HuaiyiResponse", request.reason != CardUseStruct::CARD_USE_REASON_PLAY);
        return card;
    }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    { ctx.choice = request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? "prepare" : "snatch"; return true; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.initiator || !ctx.initiator->isAlive()) return FinishSkill;
        if (ctx.choice.isEmpty() && ctx.use_card) ctx.choice = ctx.use_card->getTag("HuaiyiResponse").toBool() ? "snatch" : "prepare";
        if (ctx.choice == "prepare") { skillEffect(ctx, ctx.initiator); return FinishSkill; }
        int obtained = 0;
        for (ServerPlayer *victim : ctx.targets) {
            if (!ctx.initiator->isAlive()) break;
            SkillContext take = ctx;
            take.choice = "select";
            take.extra_data = QVariantMap();
            skillEffect(take, victim);
            if (take.extra_data.toMap().isEmpty()) continue;
            SkillContext gain = ctx;
            gain.choice = "gain";
            gain.extra_data = take.extra_data;
            skillEffect(gain, ctx.initiator);
            obtained += gain.extra_data.toMap().value("obtained").toInt();
        }
        if (obtained >= 2 && ctx.initiator->isAlive()) {
            SkillContext loss = ctx;
            loss.choice = "lose_hp";
            skillEffect(loss, ctx.initiator);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (!target || !target->isAlive() || amount <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice == "lose_hp") {
            room->loseHp(HpLostStruct(target, amount, objectName(), ctx.initiator));
            return ContinueEffects;
        }
        if (ctx.choice == "select") {
            if (!ctx.initiator->isAlive() || target == ctx.initiator || target->isNude()) return ContinueEffects;
            const int id = room->askForCardChosen(ctx.initiator, target, "he", objectName());
            if (room->getCardOwner(id) == target && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                && !Sanguosha->getCard(id)->hasFlag("using"))
                ctx.extra_data = QVariantMap{{"giver", target->objectName()}, {"card", id}};
            return ContinueEffects;
        }
        if (ctx.choice == "gain") {
            QVariantMap gift = ctx.extra_data.toMap();
            const int id = gift.value("card", -1).toInt();
            const QString name = gift.value("giver").toString();
            ServerPlayer *giver = name.isEmpty() ? nullptr : room->findChild<ServerPlayer *>(name);
            if (id >= 0 && giver && room->getCardOwner(id) == giver
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                && !Sanguosha->getCard(id)->hasFlag("using")) {
                const qint64 cause = room->currentHistoryEventId();
                const qint64 before = moveWatermark(room);
                room->obtainCard(target, id, false);
                gift.insert("obtained", committedCards(room, cause, before, QList<int>{id}, giver->objectName(),
                    target->objectName(), Player::PlaceHand));
                ctx.extra_data = gift;
            }
            return ContinueEffects;
        }
        QList<int> red, black;
        for (const Card *card : target->getHandcards())
            if (!card->hasFlag("using")) (card->isRed() ? red : black) << card->getEffectiveId();
        room->showAllCards(target);
        if (!target->isAlive()) return ContinueEffects;
        if (red.isEmpty() || black.isEmpty()) return ContinueEffects;
        QList<int> ids = room->askForChoice(target, objectName(), "black+red") == "black" ? black : red;
        for (int id : QList<int>(ids))
            if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand
                || Sanguosha->getCard(id)->hasFlag("using")) ids.removeOne(id);
        if (ids.isEmpty()) return ContinueEffects;
        // Prepare the exact child before discard callbacks can retire the accepted parent.
        Room::AcceptedViewAsEffectScope prompt(room, target, objectName(), ctx);
        if (!prompt.isValid()) return ContinueEffects;
        const SkillInstanceRef ref = prompt.activationRef();
        RoomState *state = Sanguosha->currentRoomState();
        const auto reason = state->getCurrentCardUseReason();
        const QString pattern = state->getCurrentCardUsePattern();
        const auto restore = qScopeGuard([=] { state->setCurrentCardUseReason(reason); state->setCurrentCardUsePattern(pattern); });
        const qint64 cause = room->currentHistoryEventId();
        const qint64 before = moveWatermark(room);
        DummyCard cards(ids);
        cards.setSkillName(objectName());
        room->throwCard(&cards, target);
        const int discarded = committedCards(room, cause, before, ids, target->objectName(), QString(), Player::DiscardPile);
        if (!target->isAlive() || discarded <= 0) return ContinueEffects;
        target->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "target_limit", discarded);
        room->askForUseCard(target, "@@huaiyi", "@huaiyi:::" + QString::number(discarded), -1, Card::MethodNone);
        return ContinueEffects;
    }
private:
    static qint64 moveWatermark(Room *room)
    {
        const QVariantMap page = room->queryHistoryMoves({{"limit", 1}});
        return room->historyRecordingEnabled() && !page.contains("error") && page.contains("watermark")
            ? page.value("watermark").toLongLong() : -1;
    }
    static int committedCards(Room *room, qint64 cause, qint64 before, const QList<int> &ids,
        const QString &from, const QString &to, Player::Place place)
    {
        if (cause <= 0 || before < 0) return 0;
        QVariantMap filter{{"from", from}, {"after", before}};
        QSet<int> committed;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return 0;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
                const int id = move.value("card_id", -1).toInt();
                // Only this operation's direct move counts; later callbacks can move the same card again.
                if (ids.contains(id) && move.value("to").toString() == to && move.value("to_place").toInt() == int(place)
                    && room->historyEvent(fact.value("event_id").toLongLong()).value("parent_id").toLongLong() == cause
                    && (place != Player::DiscardPile || move.value("from_place").toInt() == int(Player::PlaceHand))) committed.insert(id);
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("after", page.value("next_after"));
            filter.insert("watermark", page.value("watermark"));
        }
        return committed.size();
    }
    static int selectionLimit(const ActiveSkillRequest &request)
    {
        return request.initiator && request.activationRef.isValid()
            ? request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
                request.activationRef.key.instanceID, "target_limit").toInt() : 0;
    }
};
class Jigong : public TriggerSkillV2
{
public:
    Jigong() : TriggerSkillV2("jigong")
    {
        events << EventPhaseStart << EventPhaseChanging << DamageComplete;
        global = true;
    }
    static int phaseDamage(Room *room, const Player *player)
    {
        const QVariant phase = player->property("JigongPhase");
        if (phase.toLongLong() <= 0) return -1;
        QVariantMap filter{{"phase_id", phase}, {"from", player->objectName()}, {"limit", 128}};
        int amount = 0;
        for (;;) {
            const QVariantMap page = room->queryActualDamage(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return -1;
            for (const QVariant &entry : page.value("items").toList())
                amount += entry.toMap().value("data").toMap().value("amount").toInt();
            if (!page.value("has_more").toBool()) return amount;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    static void project(Room *room, ServerPlayer *player)
    {
        if (player && !QJsonDocument::fromJson(player->property("JigongEffects").toByteArray()).toVariant().toList().isEmpty())
            room->setPlayerProperty(player, "JigongDamage", phaseDamage(room, player));
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseStart && player && player->getPhase() == Player::Play
            && !QJsonDocument::fromJson(player->property("JigongEffects").toByteArray()).toVariant().toList().isEmpty()) {
            // A later Play phase replaces the phase whose damage fixes this turn's hand limit.
            room->setPlayerProperty(player, "JigongPhase", room->historyScopes().value("phase_id"));
            project(room, player);
        } else if (event == DamageComplete) {
            project(room, data.value<DamageStruct>().from);
        } else if (event == EventPhaseChanging) {
            const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive) {
                for (ServerPlayer *owner : room->getAllPlayers(true)) {
                    room->setPlayerProperty(owner, "JigongEffects", QString::fromUtf8(QJsonDocument::fromVariant(QVariantList()).toJson(QJsonDocument::Compact)));
                    room->setPlayerProperty(owner, "JigongPhase", QVariant());
                    room->setPlayerProperty(owner, "JigongDamage", -1);
                }
            } else if (change.from == Player::Play) project(room, player);
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Play
            && player->hasSkill(this) && room->historyScopes().value("phase_id").toLongLong() > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->askForSkillInvoke(this)) return false;
        ctx.targets = {ctx.owner};
        ctx.extra_data = room->historyScopes().value("phase_id");
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target->isAlive() || getEffectiveAmount(ctx) <= 0 || ctx.extra_data.toLongLong() <= 0) return false;
        QVariantList effects = QJsonDocument::fromJson(target->property("JigongEffects").toByteArray()).toVariant().toList();
        effects << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}};
        room->setPlayerProperty(target, "JigongEffects", QString::fromUtf8(QJsonDocument::fromVariant(effects).toJson(QJsonDocument::Compact)));
        room->setPlayerProperty(target, "JigongPhase", ctx.extra_data);
        project(room, target);
        target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class JigongMax : public MaxCardsSkillV2
{
public:
    JigongMax() : MaxCardsSkillV2("#jigong") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || QJsonDocument::fromJson(ctx.primary->property("JigongEffects").toByteArray()).toVariant().toList().isEmpty() || ctx.currentAmount <= 0)
            return CorrectSkillResult::noEffect();
        int damage = ctx.primary->property("JigongDamage").toInt();
        if (const ServerPlayer *server = qobject_cast<const ServerPlayer *>(ctx.primary))
            damage = Jigong::phaseDamage(server->getRoom(), server);
        // The client receives a derived projection; server corrections always query the journal.
        return damage < 0 ? CorrectSkillResult::noEffect() : CorrectSkillResult::useAmount(damage * ctx.currentAmount);
    }
};

class Shifei : public TriggerSkillV2
{
public:
    Shifei() : TriggerSkillV2("shifei") { events << CardAsked; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *current = room->getCurrent();
        return player && player->isAlive() && player->hasSkill(this) && data.toStringList().value(0) == "jink"
            && current && current->isAlive() && current->getPhase() != Player::NotActive
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { return ctx.owner && room->askForSkillInvoke(ctx.owner, objectName()); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        ServerPlayer *current = room->getCurrent();
        if (!current || !current->isAlive()) return false;
        SkillContext draw = ctx;
        draw.choice = "draw";
        skillEffect(event, room, player, draw, current);
        if (!ctx.owner->isAlive()) return false;
        QList<ServerPlayer *> mosts;
        int most = -1;
        for (ServerPlayer *candidate : room->getAlivePlayers()) {
            if (candidate->getHandcardNum() > most) { mosts.clear(); most = candidate->getHandcardNum(); }
            if (candidate->getHandcardNum() == most) mosts << candidate;
        }
        if (mosts.size() == 1 && mosts.contains(current)) return false;
        for (ServerPlayer *candidate : QList<ServerPlayer *>(mosts))
            if (!ctx.owner->canDiscard(candidate, "he")) mosts.removeOne(candidate);
        if (mosts.isEmpty()) return false;
        ServerPlayer *victim = room->askForPlayerChosen(ctx.owner, mosts, objectName(), "@shifei-dis");
        if (!victim) return false;
        SkillContext discard = ctx;
        discard.choice = "discard";
        discard.extra_data = false;
        skillEffect(event, room, player, discard, victim);
        if (!discard.extra_data.toBool()) return false;
        Jink *jink = new Jink(Card::NoSuit, 0);
        jink->setSkillName("_shifei");
        jink->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
        jink->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
        jink->setFlags("YUANBEN");
        jink->deleteLater();
        room->provide(jink);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        if (ctx.choice == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
        else {
            if (!ctx.owner || !ctx.owner->isAlive() || !ctx.owner->canDiscard(target, "he")) return false;
            if (ctx.owner == target) ctx.extra_data = room->askForDiscard(target, objectName(), 1, 1, false, true) != nullptr;
            else {
                const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false, Card::MethodDiscard);
                if (id < 0 || room->getCardOwner(id) != target || !ctx.owner->canDiscard(target, id)) return false;
                room->throwCard(id, target, ctx.owner);
                ctx.extra_data = true;
            }
        }
        return false;
    }
};
class ZhanjueVS : public ViewAsSkillV2
{
public:
    explicit ZhanjueVS(const QString &name = "zhanjue") : ViewAsSkillV2(name, 0) {}
    static int drawn(Room *room, const Player *player, const QString &name, int instance)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0 || instance <= 0) return -1;
        QVariantMap filter{{"turn_id", turn}, {"to", player->objectName()}};
        QHash<qint64, int> phases;
        QSet<QString> counted;
        int maximum = 0;
        for (;;) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (page.contains("error") || !page.value("complete").toBool()) return -1;
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), move = fact.value("data").toMap();
                if (move.value("to_place").toInt() != Player::PlaceHand
                    || (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DRAW) continue;
                const QVariantMap moveEvent = room->historyEvent(fact.value("event_id").toLongLong());
                const QVariantMap draw = room->historyEvent(moveEvent.value("parent_id").toLongLong());
                if (draw.value("kind").toString() != "draw" || draw.value("data").toMap().value("player").toString() != player->objectName()) continue;
                const QVariantMap skill = room->historyEvent(draw.value("parent_id").toLongLong());
                const QVariantMap origin = skill.value("data").toMap().value("zhanjue_origin").toMap();
                if (skill.value("kind").toString() != "skill" || origin.value("activation_owner").toString() != player->objectName()
                    || origin.value("activation_skill").toString() != name || origin.value("activation_instance").toInt() != instance
                    || origin.value("serial").toInt() <= 0) continue;
                const qint64 phase = fact.value("phase_id").toLongLong();
                if (phase <= 0) return -1;
                const QString key = fact.value("event_id").toString() + ":" + move.value("card_id").toString();
                if (counted.contains(key)) continue;
                counted.insert(key);
                maximum = qMax(maximum, ++phases[phase]);
            }
            if (!page.value("has_more").toBool()) return maximum;
            filter.insert("after", page.value("next_after")); filter.insert("watermark", page.value("watermark"));
        }
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || request.initiator->isKongcheng()) return false;
        int count = QJsonDocument::fromJson(request.initiator->property((objectName() + "Draws").toLatin1().constData()).toByteArray())
            .toVariant().toMap().value(QString::number(request.activationRef.key.instanceID), 0).toInt();
        if (const ServerPlayer *server = qobject_cast<const ServerPlayer *>(request.initiator))
            count = drawn(server->getRoom(), server, objectName(), request.activationRef.key.instanceID);
        return count >= 0 && count < 2;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && !request.selectedCardIds.contains(card->getEffectiveId()) && request.initiator->getHandcards().contains(card);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.initiator->isKongcheng()) return false;
        // Zero-selection preview auto-fills the hand; a serialized ordinary Duel carries every physical material.
        if (request.selectedCardIds.isEmpty()) return true;
        const QList<int> hand = request.initiator->handCards();
        if (request.selectedCardIds.size() != hand.size()) return false;
        for (int id : request.selectedCardIds) if (!hand.contains(id)) return false;
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.initiator->isKongcheng()) return nullptr;
        Duel *duel = new Duel(Card::SuitToBeDecided, -1);
        duel->addSubcards(request.initiator->getHandcards()); duel->setSkillName(objectName());
        return duel;
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || !ctx.use_card || !canActivate(request)) return false;
        const QList<int> ids = ctx.use_card->getSubcards();
        const QList<int> hand = ctx.initiator->handCards();
        if (ids.isEmpty() || ids.size() != hand.size()) return false;
        for (int id : ids) if (!hand.contains(id) || Sanguosha->getCard(id)->hasFlag("using")) return false;
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.initiator || !ctx.use_card || !ctx.activationRef.isValid() || !ctx.sourceRef.isValid()) return FinishSkill;
        Room *room = ctx.initiator->getRoom();
        const int serial = room->getTag("ZhanjueReceiptSerial").toInt() + 1;
        room->setTag("ZhanjueReceiptSerial", serial);
        ctx.use_card->setTag("ZhanjueEffect", QVariantMap{{"serial", serial}, {"actor", ctx.initiator->objectName()},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
            {"activation_instance", ctx.activationRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}});
        return ContinueEffects;
    }
};

class Zhanjue : public TriggerSkillV2
{
public:
    explicit Zhanjue(const QString &name = "zhanjue") : TriggerSkillV2(name)
    {
        view_as_skill = new ZhanjueVS(name); global = true;
        events << PreCardUsed << CardFinished << CardsMoveOneTime << EventPhaseStart << EventPhaseChanging << EventSkillEffectFinished;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == PreCardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            QVariantMap receipt = use.card ? use.card->getTag("ZhanjueEffect").toMap() : QVariantMap();
            if (use.card && use.from == player && receipt.value("activation_skill").toString() == objectName()
                && receipt.value("actor").toString() == player->objectName()) {
                receipt["use_id"] = use.targetModReveal.useHistoryEventId;
                use.card->setTag("ZhanjueEffect", receipt);
            }
        } else if (event == EventSkillEffectFinished) {
            const SkillContext finished = data.value<SkillContext>();
            if (parseSkillName(finished.skill_name) == objectName() && !finished.activationRef.isValid() && finished.original_data) {
                const CardUseStruct use = finished.original_data->value<CardUseStruct>();
                if (use.card && use.card->getTag("ZhanjueEffect").toMap().value("serial").toInt() == finished.instanceID)
                    use.card->removeTag("ZhanjueEffect");
            }
        } else if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (player && move.to == player && move.to_place == Player::PlaceHand) project(room, player);
        } else if (event == EventPhaseStart && player && player->getPhase() == Player::RoundStart) {
            for (ServerPlayer *holder : room->getAllPlayers(true)) project(room, holder);
        } else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            for (ServerPlayer *holder : room->getAllPlayers(true)) room->setPlayerProperty(holder, (objectName() + "Draws").toLatin1().constData(), "{}");
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != CardFinished) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || use.from != player || !use.card || !use.card->isKindOf("Duel") || use.targetModReveal.useHistoryEventId <= 0) return true;
        const QVariantMap receipt = use.card->getTag("ZhanjueEffect").toMap();
        if (receipt.value("activation_skill").toString() != objectName() || receipt.value("actor").toString() != player->objectName()
            || receipt.value("use_id").toLongLong() != use.targetModReveal.useHistoryEventId || receipt.value("amount").toInt() <= 0) return true;
        const QVariantMap damage = room->queryCardUseDamage(use.targetModReveal.useHistoryEventId);
        if (damage.contains("error") || !damage.value("complete").toBool()) return true;
        QList<ServerPlayer *> recipients;
        for (const QVariant &entry : damage.value("items").toList()) {
            ServerPlayer *target = room->findPlayerByObjectName(entry.toMap().value("data").toMap().value("to").toString(), true);
            if (target) recipients << target;
        }
        // Preserve the canonical C++ version's no-damage boundary; OL always draws for the user.
        if (objectName() == "zhanjue" && recipients.isEmpty()) return true;
        recipients << player;
        room->sortByActionOrder(recipients);
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = ctx.invoker = ctx.initiator = player;
        ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
        if (!ctx.sourceRef.isValid()) return true;
        ctx.instanceID = receipt.value("serial").toInt(); ctx.extra_data = receipt;
        ctx.original_data = &data; ctx.current_event = event; ctx.targets = recipients; ctx.forced = true;
        ctx.setModifiedAmount(receipt.value("amount").toInt()); contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.original_data || !ctx.owner) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        return use.card && use.card->getTag("ZhanjueEffect").toMap() == ctx.extra_data.toMap()
            && ctx.extra_data.toMap().value("serial").toInt() == ctx.instanceID;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.manual_effect = true; return true; }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QVariantMap skill = room->historyParent(room->currentHistoryEventId(), "skill", true);
        if (skill.value("id").toLongLong() <= 0) return false;
        // Annotate the accepted native event; do not replace the dispatcher's empty activation or manufacture draw facts.
        room->resolutionHistory().updateEvent(skill.value("id").toLongLong(), {{"zhanjue_origin", ctx.extra_data}});
        for (ServerPlayer *target : ctx.targets) {
            SkillContext recipient = ctx;
            skillEffect(event, room, ctx.owner, recipient, target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive() && getEffectiveAmount(ctx) > 0) target->drawCardsList(getEffectiveAmount(ctx), objectName());
        return false;
    }
private:
    void project(Room *room, ServerPlayer *player) const
    {
        QVariantMap counts;
        for (const SkillInstance &instance : player->getSkillInstances())
            if (instance.skillName == objectName()) counts[QString::number(instance.instanceID)] = ZhanjueVS::drawn(room, player, objectName(), instance.instanceID);
        room->setPlayerProperty(player, (objectName() + "Draws").toLatin1().constData(), QString::fromUtf8(QJsonDocument::fromVariant(counts).toJson(QJsonDocument::Compact)));
    }
};

QinwangCard::QinwangCard()
{
    setSkillName("qinwang");
}

bool QinwangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Slash *slash = new Slash(NoSuit, 0);
    slash->deleteLater();
    return slash->targetFilter(targets, to_select, Self);
}

const Card *QinwangCard::validate(CardUseStruct &cardUse) const
{
    Room *room = cardUse.from->getRoom();
    room->throwCard(cardUse.card, cardUse.from);
    room->broadcastSkillInvoke("qinwang");

    JijiangCard jj;
    cardUse.from->setFlags("qinwangjijiang");
    try {
        const Card *vs = jj.validate(cardUse);
        if (cardUse.from->hasFlag("qinwangjijiang"))
            cardUse.from->setFlags("-qinwangjijiang");

        return vs;
    }
    catch (TriggerEvent e) {
        if (e == TurnBroken || e == StageChange)
            cardUse.from->setFlags("-qinwangjijiang");

        throw e;
    }

    return nullptr;
}

class QinwangVS : public ViewAsSkillV2
{
public:
    QinwangVS() : ViewAsSkillV2("qinwang$", 1) { setResponseOrUse(true); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !request.initiator->hasLordSkill(objectName())
            || request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "failed", false).toBool()) return false;
        bool hasLiege = false;
        for (const Player *other : request.initiator->getAliveSiblings())
            if (other->getKingdom() == "shu") hasLiege = true;
        if (!hasLiege) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? Slash::IsAvailable(request.initiator)
            : request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern.contains("slash", Qt::CaseInsensitive);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty()
            && (request.initiator->handCards().contains(card->getEffectiveId())
                || request.initiator->getEquipsId().contains(card->getEffectiveId()))
            && !request.initiator->isJilei(card);
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        // The selected card is a discard cost, never a material of the eventual Slash.
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName(objectName());
        return slash;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        ServerPlayer *payer = ctx.initiator;
        if (!payer || request.selectedCardIds.size() != 1) return false;
        const int id = request.selectedCardIds.first();
        if (room->getCardOwner(id) != payer || !payer->canDiscard(payer, id)) return false;
        RoomState *state = Sanguosha->currentRoomState();
        const auto reason = state->getCurrentCardUseReason();
        const QString pattern = state->getCurrentCardUsePattern();
        const auto requestGuard = qScopeGuard([=] {
            state->setCurrentCardUseReason(reason);
            state->setCurrentCardUsePattern(pattern);
        });
        // Pay first; a declined liege request does not refund the discarded card.
        room->throwCard(id, objectName(), payer);
        ServerPlayer *user = ctx.invoker;
        if (!user || !user->isAlive()) return false;
        for (ServerPlayer *liege : room->getLieges("shu", user)) {
            const Card *provided = room->askForCard(liege, "slash", "@jijiang-slash:" + user->objectName(),
                QVariant::fromValue(user), Card::MethodResponse, user, false, "", true);
            if (!provided) continue;
            Card *slash = Sanguosha->cloneCard(provided->objectName(), provided->getSuit(), provided->getNumber());
            if (!slash) return false;
            slash->addSubcard(provided);
            slash->setSkillName(objectName());
            slash->setFlags("YUANBEN");
            slash->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
            slash->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
            slash->deleteLater();
            ctx.updated_card = slash;
            if (ctx.original_data && ctx.original_data->canConvert<CardUseStruct>()) {
                CardUseStruct use = ctx.original_data->value<CardUseStruct>();
                use.m_isOwnerUse = false;
                ctx.original_data->setValue(use);
            }
            // Reward is owed to this exact provider even if the lord grant was lost during payment.
            skillEffect(ctx, liege);
            return true;
        }
        payer->setSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "failed", true);
        return false;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
};

class Qinwang : public TriggerSkillV2
{
public:
    Qinwang() : TriggerSkillV2("qinwang$")
    {
        view_as_skill = new QinwangVS;
        events << CardAsked << EventPhaseChanging;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging && player == ctx.owner)
            ctx.owner->removeSkillInstanceStateValue(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID, "failed");
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const QStringList request = data.toStringList();
        return event == CardAsked && player && player->isAlive() && player->hasLordSkill(objectName())
            && request.size() >= 3 && request.first() == "slash" && request[2] == "response"
            && !request[1].contains("jijiang-slash") && !room->getLieges("shu", player).isEmpty()
            && player->canDiscard(player, "he") ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const Card *card = room->askForCard(ctx.owner, "..", "@qinwang-discard", *ctx.original_data,
            Card::MethodNone, nullptr, false, objectName());
        if (!card || card->getEffectiveId() < 0) return false;
        ctx.extra_data = card->getEffectiveId();
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (!ctx.owner || room->getCardOwner(id) != ctx.owner || !ctx.owner->canDiscard(ctx.owner, id)) return false;
        room->throwCard(id, objectName(), ctx.owner);
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        RoomState *state = Sanguosha->currentRoomState();
        const auto reason = state->getCurrentCardUseReason();
        const QString pattern = state->getCurrentCardUsePattern();
        const auto requestGuard = qScopeGuard([=] {
            state->setCurrentCardUseReason(reason);
            state->setCurrentCardUsePattern(pattern);
        });
        room->broadcastSkillInvoke(objectName());
        for (ServerPlayer *liege : room->getLieges("shu", ctx.owner)) {
            const Card *provided = room->askForCard(liege, "slash", "@jijiang-slash:" + ctx.owner->objectName(),
                QVariant::fromValue(ctx.owner), Card::MethodResponse, ctx.owner, false, "", true);
            if (!provided) continue;
            Card *slash = Sanguosha->cloneCard(provided->objectName(), provided->getSuit(), provided->getNumber());
            if (!slash) return false;
            slash->addSubcard(provided);
            slash->setSkillName(objectName());
            slash->setFlags("YUANBEN");
            slash->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
            slash->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
            slash->deleteLater();
            skillEffect(event, room, ctx.owner, ctx, liege);
            room->provide(slash);
            return true;
        }
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

// Provider rewards now belong to the exact paid request, rather than an owner-wide flag.
class QinwangDraw : public TriggerSkillV2
{
public:
    QinwangDraw() : TriggerSkillV2("#qinwang-draw") {}
};
ZhenshanCard::ZhenshanCard()
{
    setSkillName("zhenshan");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool ZhenshanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
        return card->targetFilter(targets, to_select, Self);
	}
    return false;
}

bool ZhenshanCard::targetFixed() const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
        return card->targetFixed();
	}
    return false;
}

bool ZhenshanCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
        return card->targetsFeasible(targets, Self);
	}
    return false;
}

const Card *ZhenshanCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer *quancong = card_use.from;
    Room *room = quancong->getRoom();

    QString user_str = user_string;
    if ((user_string.contains("slash") || user_string.contains("Slash")) && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        QStringList use_list = Sanguosha->getSlashNames();
        if (use_list.isEmpty())
            use_list << "slash";
        user_str = room->askForChoice(quancong, "zhenshan_slash", use_list.join("+"));
    }

    askForExchangeHand(quancong);

    Card *c = Sanguosha->cloneCard(user_str);
    c->setSkillName("zhenshan");
	c->deleteLater();
    return c;
}

const Card *ZhenshanCard::validateInResponse(ServerPlayer *quancong) const
{
    Room *room = quancong->getRoom();

    QString user_str = user_string.split("+").first();
    if (user_string == "peach+analeptic") {
        QStringList use_list;
        use_list << "peach";
        if (Sanguosha->hasCard("analeptic")) use_list << "analeptic";
        user_str = room->askForChoice(quancong, "zhenshan_saveself", use_list.join("+"));
    } else if (user_string.contains("slash") || user_string.contains("Slash")) {
        QStringList use_list = Sanguosha->getSlashNames();
        if (use_list.isEmpty()) use_list << "slash";
        user_str = room->askForChoice(quancong, "zhenshan_slash", use_list.join("+"));
    }

    askForExchangeHand(quancong);

    Card *c = Sanguosha->cloneCard(user_str);
    c->setSkillName("zhenshan");
	c->deleteLater();
    return c;
}

void ZhenshanCard::askForExchangeHand(ServerPlayer *quancong)
{
    Room *room = quancong->getRoom();
    QList<ServerPlayer *> targets;
    foreach (ServerPlayer *p, room->getOtherPlayers(quancong)) {
        if (quancong->getHandcardNum() > p->getHandcardNum())
            targets << p;
    }
    ServerPlayer *target = room->askForPlayerChosen(quancong, targets, "zhenshan", "@zhenshan");
    QList<CardsMoveStruct> moves;
    if (!quancong->isKongcheng()) {
        CardMoveReason reason(CardMoveReason::S_REASON_SWAP, quancong->objectName(), target->objectName(), "zhenshan", "");
        CardsMoveStruct move(quancong->handCards(), target, Player::PlaceHand, reason);
        moves << move;
    }
    if (!target->isKongcheng()) {
        CardMoveReason reason(CardMoveReason::S_REASON_SWAP, target->objectName(), quancong->objectName(), "zhenshan", "");
        CardsMoveStruct move(target->handCards(), quancong, Player::PlaceHand, reason);
        moves << move;
    }
    room->moveCardsAtomic(moves, false);
	room->addPlayerMark(quancong,"ZhenshanUsed-Clear");
}

class ZhenshanVS : public ViewAsSkillV2
{
public:
    ZhenshanVS() : ViewAsSkillV2("zhenshan", 0) { setResponseOrUse(true); }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, false); }
    static bool canExchange(const Player *player)
    {
        if (!player || player->isKongcheng()) return false;
        bool activeTurn = player->getPhase() != Player::NotActive, less = false;
        for (const Player *other : player->getAliveSiblings()) {
            activeTurn = activeTurn || other->getPhase() != Player::NotActive;
            less = less || other->getHandcardNum() < player->getHandcardNum();
        }
        return activeTurn && less;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return canExchange(request.initiator) && !usableNames(request).isEmpty(); }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::cost(room, ctx, request) || !ctx.initiator) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getOtherPlayers(ctx.initiator))
            if (target->getHandcardNum() < ctx.initiator->getHandcardNum()) candidates << target;
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.initiator, candidates, objectName(), "@zhenshan");
        if (!target) return false;
        ctx.extra_data = target->objectName();
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        ServerPlayer *from = ctx.initiator;
        ServerPlayer *to = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!from || !from->isAlive() || !to || !to->isAlive() || from == to || from->getHandcardNum() <= to->getHandcardNum()) return false;
        for (const Card *card : from->getHandcards() + to->getHandcards()) if (card->hasFlag("using")) return false;
        QList<CardsMoveStruct> moves;
        // Exchange is the payment. The ordinary zero-material basic card is used only after the swap completes.
        if (!from->isKongcheng()) moves << CardsMoveStruct(from->handCards(), from, to, Player::PlaceHand, Player::PlaceHand,
            CardMoveReason(CardMoveReason::S_REASON_SWAP, from->objectName(), to->objectName(), objectName(), ""));
        if (!to->isKongcheng()) moves << CardsMoveStruct(to->handCards(), to, from, Player::PlaceHand, Player::PlaceHand,
            CardMoveReason(CardMoveReason::S_REASON_SWAP, to->objectName(), from->objectName(), objectName(), ""));
        room->moveCardsAtomic(moves, false);
        return true;
    }
};

class Zhenshan : public TriggerSkillV2
{
public:
    Zhenshan() : TriggerSkillV2("zhenshan") { view_as_skill = new ZhenshanVS; }
    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, false); }
};

YanzhuCard::YanzhuCard()
{
    setSkillName("yanzhu");
}

bool YanzhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && !to_select->isNude();
}

void YanzhuCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *target = effect.to;
    Room *r = target->getRoom();

    if (!r->askForDiscard(target, "yanzhu", 1, 1, !target->getEquips().isEmpty(), true, "@yanzhu-discard")) {
        if (!target->getEquips().isEmpty()) {
            DummyCard dummy;
            dummy.addSubcards(target->getEquips());
            r->obtainCard(effect.from, &dummy);
        }

        if (effect.from->hasSkill("yanzhu", true)) {
            r->setPlayerMark(effect.from, "yanzhu_lost", 1);
            r->handleAcquireDetachSkills(effect.from, "-yanzhu");
        }
    }
}

class Yanzhu : public ViewAsSkillV2
{
public:
    Yanzhu() : ViewAsSkillV2("yanzhu") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "YanzhuCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return candidate && candidate->isAlive() && candidate != request.initiator && selected.isEmpty() && !candidate->isNude(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive() || !ctx.initiator || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        if (ctx.choice == "gain") {
            const QVariantMap gift = ctx.extra_data.toMap();
            const QString name = gift.value("giver").toString();
            ServerPlayer *giver = name.isEmpty() ? nullptr : room->findChild<ServerPlayer *>(name);
            if (!giver) return ContinueEffects;
            DummyCard cards;
            for (const QVariant &entry : gift.value("cards").toList()) {
                const int id = entry.toInt();
                if (room->getCardOwner(id) == giver && room->getCardPlace(id) == Player::PlaceEquip
                    && !Sanguosha->getCard(id)->hasFlag("using")) cards.addSubcard(id);
            }
            if (cards.subcardsLength() > 0) room->obtainCard(target, &cards);
            return ContinueEffects;
        }
        if (ctx.choice == "retire") {
            // Retire only the accepted entry; the permanent upgrade records the frozen source.
            if (target->objectName() != ctx.activationRef.ownerObjectName) return ContinueEffects;
            QVariantList upgrades = target->getTag("YanzhuLostEffects").toList();
            upgrades << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
                {"instance", ctx.sourceRef.key.instanceID}, {"activation", ctx.activationRef.key.instanceID}};
            target->setTag("YanzhuLostEffects", upgrades);
            room->setPlayerMark(target, "yanzhu_lost", 1);
            room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID), false, false);
            return ContinueEffects;
        }
        if (room->askForDiscard(target, objectName(), getEffectiveAmount(ctx), getEffectiveAmount(ctx),
            !target->getEquips().isEmpty(), true, "@yanzhu-discard")) return ContinueEffects;
        SkillContext gain = ctx;
        gain.choice = "gain";
        gain.extra_data = QVariantMap{{"giver", target->objectName()}, {"cards", ListI2V(target->getEquipsId())}};
        if (ctx.initiator->isAlive()) skillEffect(gain, ctx.initiator);
        SkillContext retire = ctx;
        retire.choice = "retire";
        if (ctx.initiator->isAlive()) skillEffect(retire, ctx.initiator);
        return ContinueEffects;
    }
};
/*
class YanzhuTrig : public TriggerSkill
{
public:
    YanzhuTrig() : TriggerSkill("yanzhu")
    {
        events << EventLoseSkill;
        view_as_skill = new Yanzhu;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != nullptr && target->isAlive();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (data.toString() == "yanzhu")
            room->setPlayerMark(player, "yanzhu_lost", 1);

        return false;
    }
};
*/

XingxueCard::XingxueCard()
{
    setSkillName("xingxue");
}

bool XingxueCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
    int n = Self->getMark("yanzhu_lost") == 0 ? Self->getHp() : Self->getMaxHp();

    return targets.length() < n;
}

void XingxueCard::use(Room *room, ServerPlayer *, QList<ServerPlayer *> &targets) const
{
    foreach (ServerPlayer *t, targets) {
        room->drawCards(t, 1, "xingxue");
        if (t->isAlive() && !t->isNude()) {
            const Card *c = room->askForExchange(t, "xingxue", 1, 1, true, "@xingxue-put");
            int id = c->getSubcards().first();
            CardsMoveStruct m(id, nullptr, Player::DrawPile, CardMoveReason(CardMoveReason::S_REASON_PUT, t->objectName()));
            room->setPlayerFlag(t, "Global_GongxinOperator");
            room->moveCardsAtomic(m, false);
            room->setPlayerFlag(t, "-Global_GongxinOperator");
        }
    }
}

class XingxueVS : public ViewAsSkillV2
{
public:
    XingxueVS() : ViewAsSkillV2("xingxue") { response_pattern = "@@xingxue"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && request.pattern == "@@xingxue" && targetLimit(request) > 0;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return candidate && candidate->isAlive() && !selected.contains(candidate) && selected.size() < targetLimit(request); }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    { return !selected.isEmpty() && selected.size() <= targetLimit(request); }
    QString historyKey(const ActiveSkillRequest &) const override { return "XingxueCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (!target || !target->isAlive() || amount <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        target->drawCards(amount, objectName());
        if (!target->isAlive()) return ContinueEffects;
        QList<int> available;
        for (const Card *card : target->getCards("he")) if (!card->hasFlag("using")) available << card->getEffectiveId();
        const int count = qMin(amount, int(available.size()));
        if (count <= 0) return ContinueEffects;
        const Card *choice = room->askForExchange(target, objectName(), count, count, true, "@xingxue-put");
        QList<int> ids;
        if (choice) for (int id : choice->getSubcards()) if (available.contains(id) && !ids.contains(id)) ids << id;
        if (ids.size() != count) ids = available.mid(0, count);
        for (int id : QList<int>(ids))
            if (room->getCardOwner(id) != target || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || Sanguosha->getCard(id)->hasFlag("using")) ids.removeOne(id);
        if (ids.isEmpty()) return ContinueEffects;
        // The private top-deck operation must restore its outer visibility scope on interruption.
        const bool previous = target->hasFlag("Global_GongxinOperator");
        target->setFlags("Global_GongxinOperator");
        const auto cleanup = qScopeGuard([=] { if (!previous) target->setFlags("-Global_GongxinOperator"); });
        room->moveCardsAtomic(CardsMoveStruct(ids, target, nullptr, Player::PlaceUnknown, Player::DrawPile,
            CardMoveReason(CardMoveReason::S_REASON_PUT, target->objectName(), objectName(), "")), false);
        return ContinueEffects;
    }
private:
    static int targetLimit(const ActiveSkillRequest &request)
    {
        if (!request.initiator || !request.activationRef.isValid()) return 0;
        return request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
            request.activationRef.key.instanceID, "target_limit").toInt();
    }
};

class Xingxue : public TriggerSkillV2
{
public:
    Xingxue() : TriggerSkillV2("xingxue") { events << EventPhaseStart; view_as_skill = new XingxueVS; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Finish
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive() || getEffectiveAmount(ctx) <= 0) return false;
        Room::AcceptedViewAsEffectScope prompt(room, ctx.owner, objectName(), ctx);
        if (!prompt.isValid()) return false;
        const SkillInstanceRef ref = prompt.activationRef();
        ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "target_limit",
            ctx.owner->getTag("YanzhuLostEffects").toList().isEmpty() ? ctx.owner->getHp() : ctx.owner->getMaxHp());
        RoomState *state = Sanguosha->currentRoomState();
        const auto reason = state->getCurrentCardUseReason();
        const QString pattern = state->getCurrentCardUsePattern();
        const auto restore = qScopeGuard([=] { state->setCurrentCardUseReason(reason); state->setCurrentCardUsePattern(pattern); });
        room->askForUseCard(ctx.owner, "@@xingxue", "@xingxue");
        return false;
    }
};
class Qiaoshi : public TriggerSkillV2
{
public:
    Qiaoshi() : TriggerSkillV2("qiaoshi") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList list;
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish) return list;
        for (ServerPlayer *holder : room->getOtherPlayers(player))
            if (holder->isAlive() && holder->hasSkill(this) && holder->getHandcardNum() == player->getHandcardNum()) list[holder] << objectName();
        return list;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.invoker || !ctx.invoker->isAlive() || ctx.invoker->getHandcardNum() != ctx.owner->getHandcardNum()
            || !room->askForSkillInvoke(ctx.owner, objectName(), QVariant::fromValue(ctx.invoker))) return false;
        ctx.targets = {ctx.owner, ctx.invoker};
        room->sortByActionOrder(ctx.targets);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};
YjYanyuCard::YjYanyuCard()
{
    setSkillName("yjyanyu");
    will_throw = false;
    can_recast = true;
    handling_method = Card::MethodRecast;
    target_fixed = true;
}

void YjYanyuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    source->broadcastSkillInvoke("@recast");
    CardMoveReason reason(CardMoveReason::S_REASON_RECAST, source->objectName());
    reason.m_skillName = getSkillName();
    room->moveCardTo(this, source, nullptr, Player::DiscardPile, reason);

    LogMessage log;
    log.type = "#UseCard_Recast";
    log.from = source;
    log.card_str = QString::number(subcards.first());
    room->sendLog(log);

    source->drawCards(1, "recast");

    source->addMark("yjyanyu");
}

static int yanyuLostSlashes(Room *room, const SkillContext &ctx, bool recastsOnly)
{
    if (!ctx.owner) return -1;
    const qint64 phase = room->historyScopes().value("phase_id").toLongLong();
    if (phase <= 0) return -1;
    QVariantMap filter{{"phase_id", phase}, {"from", ctx.owner->objectName()}, {"limit", 128}};
    int count = 0;
    for (;;) {
        const QVariantMap page = room->queryHistoryMoves(filter);
        if (page.contains("error") || !page.value("complete").toBool()) return -1;
        for (const QVariant &entry : page.value("items").toList()) {
            const QVariantMap move = entry.toMap().value("data").toMap();
            if (recastsOnly) {
                if (move.value("reason").toInt() != CardMoveReason::S_REASON_RECAST
                    || move.value("reason_skill").toString() != ctx.activationRef.key.skillName) continue;
                if (!move.value("attribution_complete").toBool()) return -1;
                if (move.value("activation_owner").toString() != ctx.activationRef.ownerObjectName
                    || move.value("activation_skill").toString() != ctx.activationRef.key.skillName
                    || move.value("activation_instance_id").toInt() != ctx.activationRef.key.instanceID) continue;
            }
            const QVariantMap card = move.value("card_before").toMap();
            if (!card.contains("classes")) return -1;
            if (card.value("classes").toList().contains(QVariant("Slash"))) ++count;
        }
        if (!page.value("has_more").toBool()) return count;
        filter.insert("watermark", page.value("watermark"));
        filter.insert("after", page.value("next_after"));
    }
}

class YjYanyuVS : public ViewAsSkillV2
{
public:
    explicit YjYanyuVS(const QString &name = "yjyanyu") : ViewAsSkillV2(name, 1) { setPhaseName("Play"); }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && card->isKindOf("Slash") && request.selectedCardIds.isEmpty()
            && request.initiator->handCards().contains(card->getEffectiveId())
            && !request.initiator->isCardLimited(card, Card::MethodRecast);
    }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override
    { return objectName() == "yjyanyu" ? "YjYanyuCard" : "OLJieYanyuCard"; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest current = request;
        current.selectedCardIds.clear();
        const int id = request.selectedCardIds.first();
        if (!canSelectCard(current, Sanguosha->getCard(id))) return false;
        room->recastCardWithDraw(ctx.initiator, id, 0, objectName());
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.initiator) skillEffect(ctx, ctx.initiator);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive() && getEffectiveAmount(ctx) > 0)
            target->drawCards(getEffectiveAmount(ctx), "recast");
        return ContinueEffects;
    }
};

class YjYanyu : public TriggerSkillV2
{
public:
    YjYanyu() : TriggerSkillV2("yjyanyu") { view_as_skill = new YjYanyuVS; events << EventPhaseEnd; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Play
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        // Only committed recasts of this activation instance qualify; waived payment creates no move fact.
        if (yanyuLostSlashes(room, ctx, true) < 2) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *player : room->getAlivePlayers()) if (player->isMale()) candidates << player;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName(), "@yjyanyu-give", true, false);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive()) target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        return false;
    }
};
WurongCard::WurongCard()
{
    setSkillName("wurong");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool WurongCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (targets.length() > 0 || to_select == Self)
        return false;
    return !to_select->isKongcheng();
}

void WurongCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();

    const Card *c = room->askForExchange(effect.to, "wurong", 1, 1, false, "@wurong-show");

    room->showCard(effect.from, subcards.first());
    room->showCard(effect.to, c->getSubcards().first());

    const Card *card1 = Sanguosha->getCard(subcards.first());
    const Card *card2 = Sanguosha->getCard(c->getSubcards().first());

    if (card1->isKindOf("Slash") && !card2->isKindOf("Jink")) {
        room->throwCard(this, effect.from);
        room->damage(DamageStruct(objectName(), effect.from, effect.to));
    } else if (!card1->isKindOf("Slash") && card2->isKindOf("Jink")) {
        room->throwCard(this, effect.from);
        if (!effect.to->isNude()) {
            int id = room->askForCardChosen(effect.from, effect.to, "he", objectName());
            room->obtainCard(effect.from, id, false);
        }
    }
}

class Wurong : public ViewAsSkillV2
{
public:
    Wurong() : ViewAsSkillV2("wurong", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && request.selectedCardIds.isEmpty()
            && request.initiator->handCards().contains(card->getEffectiveId());
    }
    bool willThrowSelectedCards() const override { return false; }
    QString historyKey(const ActiveSkillRequest &) const override { return "WurongCard"; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return candidate && candidate->isAlive() && candidate != request.initiator && !candidate->isKongcheng() && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.invoker || !ctx.initiator || !target || target->isKongcheng() || !ctx.use_card || ctx.use_card->subcardsLength() != 1)
            return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        const int ownId = ctx.use_card->getSubcards().first();
        if (!ctx.initiator->handCards().contains(ownId)) return ContinueEffects;
        const Card *chosen = room->askForExchange(target, objectName(), 1, 1, false, "@wurong-show");
        if (!chosen || chosen->getSubcards().size() != 1) return ContinueEffects;
        const int otherId = chosen->getSubcards().first();
        if (!target->handCards().contains(otherId)) return ContinueEffects;
        // Freeze both faces before reveal callbacks can move or transform either physical card.
        const bool slash = Sanguosha->getCard(ownId)->isKindOf("Slash");
        const bool jink = Sanguosha->getCard(otherId)->isKindOf("Jink");
        room->showCard(ctx.initiator, ownId);
        room->showCard(target, otherId);
        if (slash == jink || !ctx.initiator->handCards().contains(ownId)) return ContinueEffects;
        room->throwCard(ownId, objectName(), ctx.initiator);
        const int amount = getEffectiveAmount(ctx);
        if (slash) {
            if (target->isAlive() && amount > 0) room->damage(DamageStruct(objectName(), ctx.invoker, target, amount));
        } else {
            for (int i = 0; i < amount && ctx.invoker->isAlive() && target->isAlive() && !target->isNude(); ++i) {
                const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName());
                if (id < 0) break;
                room->obtainCard(ctx.invoker, id, false);
            }
        }
        return ContinueEffects;
    }
};
class Shizhi : public TriggerSkillV2
{
public:
    Shizhi() : TriggerSkillV2("#shizhi") { events << HpChanged << MaxHpChanged << Revived; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player) return true;
        // Recompute the face from present HP and live skill validity; no owner-wide transition flag is needed.
        if (player->getHp() == 1 && player->hasSkill("shizhi")) room->filterCards(player, player->getHandcards(), false);
        else {
            QList<const Card *> cards;
            for (const Card *card : player->getHandcards())
                if (card->getSkillName() == "shizhi") cards << card;
            if (!cards.isEmpty()) room->filterCards(player, cards, true);
        }
        return true;
    }
};
class ShizhiFilter : public FilterSkill
{
public:
    ShizhiFilter() : FilterSkill("shizhi")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        if(to_select->isKindOf("Jink")){
			const Player *player = Sanguosha->getCardOwner(to_select->getId());
			return player && player->getHp() == 1;
		}
		return false;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->setSkillName(objectName());/*
        WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
        card->takeOver(slash);*/
        return slash;
    }
};

HuomoCard::HuomoCard()
{
    setSkillName("huomo");
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool HuomoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
        return card && card->targetFilter(targets, to_select, Self);
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *_card = Self ? Self->getTag("huomo").value<const Card *>() : nullptr;
    Card *card = _card ? Sanguosha->cloneCard(_card) : Sanguosha->cloneCard(user_string.split("+").first());
    if (card == nullptr)
        return false;
    card->setCanRecast(false);
    card->deleteLater();
    return card->targetFilter(targets, to_select, Self);
}

bool HuomoCard::targetFixed() const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
        return card && card->targetFixed();
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *_card = Self ? Self->getTag("huomo").value<const Card *>() : nullptr;
    Card *card = _card ? Sanguosha->cloneCard(_card) : Sanguosha->cloneCard(user_string.split("+").first());
    if (card == nullptr)
        return false;
    card->setCanRecast(false);
    card->deleteLater();
    return card->targetFixed();
}

bool HuomoCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
        return card && card->targetsFeasible(targets, Self);
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *_card = Self ? Self->getTag("huomo").value<const Card *>() : nullptr;
    Card *card = _card ? Sanguosha->cloneCard(_card) : Sanguosha->cloneCard(user_string.split("+").first());
    if (card == nullptr)
        return false;
    card->setCanRecast(false);
    card->deleteLater();
    return card->targetsFeasible(targets, Self);
}

const Card *HuomoCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer *zhongyao = card_use.from;
    Room *room = zhongyao->getRoom();

    QString to_guhuo = user_string;
    if (user_string == "slash" && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        QStringList guhuo_list;
        guhuo_list << "slash";
        if (!Config.BanPackages.contains("maneuvering"))
            guhuo_list = QStringList() << "normal_slash" << "thunder_slash" << "fire_slash";
        to_guhuo = room->askForChoice(zhongyao, "huomo_slash", guhuo_list.join("+"));
    }

    CardMoveReason reason(CardMoveReason::S_REASON_PUT, zhongyao->objectName(), "huomo", "");
    room->moveCardTo(this, nullptr, Player::DrawPile, reason, true);

    QString user_str;
    if (to_guhuo == "normal_slash")
        user_str = "slash";
    else
        user_str = to_guhuo;

    Card *c = Sanguosha->cloneCard(user_str, Card::NoSuit, 0);

    QString classname;
    if (c->isKindOf("Slash"))
        classname = "Slash";
    else
        classname = c->getClassName();

    room->setPlayerMark(zhongyao, "Huomo_" + classname, 1);

    QStringList huomoList = zhongyao->getTag("huomoClassName").toStringList();
    huomoList << classname;
    zhongyao->setTag("huomoClassName", huomoList);

    c->setSkillName("huomo");
    c->deleteLater();
    return c;
}

const Card *HuomoCard::validateInResponse(ServerPlayer *zhongyao) const
{
    Room *room = zhongyao->getRoom();

    QString to_guhuo = user_string;
    if (user_string == "peach+analeptic") {
        bool can_use_peach = zhongyao->getMark("Huomo_Peach") == 0;
        bool can_use_analeptic = zhongyao->getMark("Huomo_Analeptic") == 0;
        QStringList guhuo_list;
        if (can_use_peach)
            guhuo_list << "peach";
        if (can_use_analeptic && !Config.BanPackages.contains("maneuvering"))
            guhuo_list << "analeptic";
        to_guhuo = room->askForChoice(zhongyao, "huomo_saveself", guhuo_list.join("+"));
    } else if (user_string == "slash") {
        QStringList guhuo_list;
        guhuo_list << "slash";
        if (!Config.BanPackages.contains("maneuvering"))
            guhuo_list = QStringList() << "normal_slash" << "thunder_slash" << "fire_slash";
        to_guhuo = room->askForChoice(zhongyao, "huomo_slash", guhuo_list.join("+"));
    } else
        to_guhuo = user_string;

    CardMoveReason reason(CardMoveReason::S_REASON_PUT, zhongyao->objectName(), "huomo", "");
    room->moveCardTo(this, nullptr, Player::DrawPile, reason, true);

    QString user_str;
    if (to_guhuo == "normal_slash")
        user_str = "slash";
    else
        user_str = to_guhuo;

    Card *c = Sanguosha->cloneCard(user_str, Card::NoSuit, 0);

    QString classname;
    if (c->isKindOf("Slash"))
        classname = "Slash";
    else
        classname = c->getClassName();

    room->setPlayerMark(zhongyao, "Huomo_" + classname, 1);

    QStringList huomoList = zhongyao->getTag("huomoClassName").toStringList();
    huomoList << classname;
    zhongyao->setTag("huomoClassName", huomoList);

    c->setSkillName("huomo");
    c->deleteLater();
    return c;

}

class HuomoVS : public ViewAsSkillV2
{
public:
    HuomoVS() : ViewAsSkillV2("huomo", 1) { setResponseOrUse(true); }
    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, false); }
    static QStringList usedBasics(Room *room, const Player *player)
    {
        if (room->historyScopes().value("turn_id").toLongLong() <= 0) return {};
        const QVariantMap page = room->queryCardHistory(player, "turn", "BasicCard");
        if (page.contains("error") || !page.value("complete").toBool()) return {};
        QStringList names{"known"};
        for (const QVariant &entry : page.value("items").toList()) {
            const QVariantMap card = entry.toMap();
            if (!card.contains("classes") || !card.contains("class_name")) return {};
            const QString name = card.value("classes").toList().contains(QVariant("Slash")) ? "Slash" : card.value("class_name").toString();
            if (!names.contains(name)) names << name;
        }
        return names;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || (request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)) return false;
        bool activeTurn = false;
        for (const Player *player : request.initiator->getAliveSiblings(true)) activeTurn = activeTurn || player->getPhase() != Player::NotActive;
        return activeTurn && !usableNames(request).isEmpty();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && !card->isVirtualCard() && !card->hasFlag("using")
            && request.selectedCardIds.isEmpty() && card->isBlack() && card->getTypeId() != Card::TypeBasic
            && request.initiator->getCards("he").contains(card);
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest current = request; current.selectedCardIds.clear();
        const int id = request.selectedCardIds.first();
        if (!canSelectCard(current, Sanguosha->getCard(id))) return false;
        room->moveCardTo(Sanguosha->getCard(id), ctx.initiator, nullptr, Player::DrawPile,
            CardMoveReason(CardMoveReason::S_REASON_PUT, ctx.initiator->objectName(), objectName(), ""), true);
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.use_card || !ctx.use_card->isKindOf("BasicCard")) return FinishSkill;
        // The preview carries payment IDs for reconstruction only. Even waived payment produces a material-free basic card.
        Card *card = Sanguosha->cloneCard(ctx.use_card->objectName(), Card::NoSuit, 0);
        if (!card) return FinishSkill;
        card->setSkillName(objectName()); card->setCanRecast(false); card->deleteLater();
        ctx.updated_card = card;
        return ContinueEffects;
    }
protected:
    bool allowDeclaration(const Player *player, const QString &name) const override
    {
        if (!player) return false;
        Card *card = Sanguosha->cloneCard(name);
        if (!card) return false;
        const bool basic = card->isKindOf("BasicCard");
        const QString className = card->isKindOf("Slash") ? "Slash" : card->getClassName();
        delete card;
        if (!basic) return false;
        QStringList used = player->property("HuomoUsedBasics").toString().split("+", Qt::SkipEmptyParts);
        if (const ServerPlayer *server = qobject_cast<const ServerPlayer *>(player)) used = usedBasics(server->getRoom(), server);
        return used.contains("known") && !used.contains(className);
    }
    Card *buildCard(const ActiveSkillRequest &request, const QString &name) const override
    {
        Card *card = Sanguosha->cloneCard(name, Card::NoSuit, 0);
        if (card) { card->addSubcards(request.selectedCardIds); card->setSkillName(objectName()); card->setCanRecast(false); }
        return card;
    }
};

class Huomo : public TriggerSkillV2
{
public:
    Huomo() : TriggerSkillV2("huomo")
    { view_as_skill = new HuomoVS; events << CardUsed << CardResponded << EventPhaseStart << EventPhaseChanging; global = true; }
    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, false); }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            for (ServerPlayer *target : room->getAllPlayers(true)) room->setPlayerProperty(target, "HuomoUsedBasics", "known");
        } else if (event == EventPhaseStart && player && player->getPhase() == Player::RoundStart) {
            for (ServerPlayer *target : room->getAllPlayers(true))
                room->setPlayerProperty(target, "HuomoUsedBasics", HuomoVS::usedBasics(room, target).join("+"));
        } else if (player && (event == CardUsed || event == CardResponded))
            room->setPlayerProperty(player, "HuomoUsedBasics", HuomoVS::usedBasics(room, player).join("+"));
        return true;
    }
};

class Zuoding : public TriggerSkillV2
{
public:
    Zuoding() : TriggerSkillV2("zuoding")
    {
        events << TargetSpecified;
    }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || player->getPhase() != Player::Play || use.from != player
            || !use.card || use.card->isKindOf("SkillCard") || use.card->getSuit() != Card::Spade || use.to.isEmpty()) return result;
        const QVariant phase = room->historyScopes().value("phase_id");
        if (phase.toLongLong() == 0) return result;
        const QVariantMap history = room->queryActualDamage({{"phase_id", phase}, {"limit", 1}});
        // Only a complete empty journal establishes that this Play phase has dealt no damage.
        if (history.contains("error") || !history.value("complete").toBool()
            || !history.value("attribution_complete").toBool() || !history.value("items").toList().isEmpty()) return result;
        for (ServerPlayer *owner : room->getOtherPlayers(player))
            if (owner->hasSkill(this)) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, use.to, objectName(), "@zuoding", true, false);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    {
        room->broadcastSkillInvoke(objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class ZuodingRecord : public TriggerSkillV2
{
public:
    ZuodingRecord() : TriggerSkillV2("#zuoding")
    {
        // Preserve the helper name without maintaining a parallel damage-history tag.
    }
};

AnguoCard::AnguoCard()
{
    setSkillName("anguo");
}

bool AnguoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty() || to_select == Self)
        return false;

    return !to_select->getEquips().isEmpty();
}

void AnguoCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    int beforen = 0;
    foreach (ServerPlayer *p, room->getAlivePlayers()) {
        if (effect.to->inMyAttackRange(p))
            beforen++;
    }

    int id = room->askForCardChosen(effect.from, effect.to, "e", "anguo");
    effect.to->obtainCard(Sanguosha->getCard(id));

    int aftern = 0;
    foreach (ServerPlayer *p, room->getAlivePlayers()) {
        if (effect.to->inMyAttackRange(p))
            aftern++;
    }

    if (aftern < beforen)
        effect.from->drawCards(1, "anguo");
}

class Anguo : public ViewAsSkillV2
{
public:
    Anguo() : ViewAsSkillV2("anguo") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return candidate && candidate->isAlive() && candidate != request.initiator && selected.isEmpty() && !candidate->getEquips().isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "AnguoCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return ContinueEffects;
        if (ctx.choice == "draw") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            return ContinueEffects;
        }
        if (!ctx.invoker || target->getEquips().isEmpty()) return ContinueEffects;
        Room *room = target->getRoom();
        int before = 0;
        for (ServerPlayer *other : room->getAlivePlayers()) if (target->inMyAttackRange(other)) ++before;
        const int id = room->askForCardChosen(ctx.invoker, target, "e", objectName());
        if (id < 0 || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceEquip) return ContinueEffects;
        room->obtainCard(target, id);
        int after = 0;
        for (ServerPlayer *other : room->getAlivePlayers()) if (target->inMyAttackRange(other)) ++after;
        if (after < before && ctx.invoker->isAlive()) {
            SkillContext reward = ctx;
            reward.choice = "draw";
            skillEffect(reward, ctx.invoker);
        }
        return ContinueEffects;
    }
};
class Qianju : public DistanceSkillV2
{
public:
    Qianju() : DistanceSkillV2("qianju")
    {
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.holder || !ctx.holder->isWounded()) return CorrectSkillResult::noEffect();
        // Each source contributes its own amount; do not collapse duplicate skills.
        return CorrectSkillResult::useAmount(-ctx.holder->getLostHp() * ctx.currentAmount);
    }
};

class Qingxi : public TriggerSkillV2
{
public:
    Qingxi() : TriggerSkillV2("qingxi") { events << DamageCaused; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(this) && damage.from == player
            && player->getWeapon() && damage.to && damage.to->isAlive() && damage.card
            && damage.card->isKindOf("Slash") && !damage.chain && !damage.transfer
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.original_data) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const Weapon *weapon = ctx.owner->getWeapon() ? qobject_cast<const Weapon *>(ctx.owner->getWeapon()->getRealCard()) : nullptr;
        if (!weapon || !damage.to || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(damage.to), false)) return false;
        ctx.targets = {damage.to};
        ctx.extra_data = QVariantMap{{"weapon", ctx.owner->getWeapon()->getEffectiveId()}, {"range", weapon->getRange(ctx.owner)}};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.owner || !target || !ctx.original_data) return false;
        const QVariantMap payment = ctx.extra_data.toMap();
        const int count = payment.value("range").toInt();
        const bool discarded = count > 0
            ? room->askForDiscard(target, objectName(), count, count, true, false, "@qingxi-throw:" + QString::number(count))
            : room->askForChoice(target, objectName(), "discard+damage", *ctx.original_data) == "discard";
        if (!discarded) {
            DamageStruct damage = ctx.original_data->value<DamageStruct>();
            damage.damage += getEffectiveAmount(ctx);
            ctx.original_data->setValue(damage);
        } else {
            // Only the weapon named by this invocation can be discarded after nested payment triggers.
            const int id = payment.value("weapon").toInt();
            if (room->getCardOwner(id) == ctx.owner && room->getCardPlace(id) == Player::PlaceEquip
                && target->canDiscard(ctx.owner, id)) room->throwCard(id, objectName(), ctx.owner, target);
        }
        return false;
    }
};
MingjianCard::MingjianCard()
{
    setSkillName("mingjian");
    will_throw = false;
    handling_method = Card::MethodNone;
}

void MingjianCard::onEffect(CardEffectStruct &effect) const
{
    if (effect.to->isDead() || effect.from->isDead() || effect.from->isKongcheng()) return;
    CardMoveReason r(CardMoveReason::S_REASON_GIVE, effect.from->objectName());
    Room *room = effect.from->getRoom();
    DummyCard *handcards = effect.from->wholeHandCards();
    room->obtainCard(effect.to, handcards, r, false);
    room->addPlayerMark(effect.to, "&mingjian");
}

class MingjianVS : public ViewAsSkillV2
{
public:
    MingjianVS() : ViewAsSkillV2("mingjian", 0) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MingjianCard"; }
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !request.initiator->isKongcheng();
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
        const Player *target) const override
    { return target && target->isAlive() && target != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return selected.size() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (!target || !target->isAlive() || !ctx.initiator || !ctx.initiator->isAlive()
            || ctx.initiator->isKongcheng() || amount <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        const QList<int> ids = ctx.initiator->handCards();
        for (int id : ids) if (Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        DummyCard cards(ids);
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, ctx.initiator->objectName(),
            target->objectName(), objectName(), "");
        room->obtainCard(target, &cards, reason, false);
        if (!target->isAlive()) return ContinueEffects;
        // An accepted award lasts through the recipient's turn, independently of its original grant.
        QVariantList effects = QJsonDocument::fromJson(target->property("MingjianEffects").toByteArray()).toVariant().toList();
        effects << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"amount", amount}};
        room->setPlayerProperty(target, "MingjianEffects", QString::fromUtf8(QJsonDocument::fromVariant(effects).toJson(QJsonDocument::Compact)));
        room->addPlayerMark(target, "&mingjian", amount);
        return ContinueEffects;
    }
};

class Mingjian : public TriggerSkillV2
{
public:
    Mingjian() : TriggerSkillV2("mingjian")
    { events << EventPhaseChanging; global = true; view_as_skill = new MingjianVS; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            room->setPlayerProperty(player, "MingjianEffects", QString::fromUtf8(QJsonDocument::fromVariant(QVariantList()).toJson(QJsonDocument::Compact)));
            room->setPlayerMark(player, "&mingjian", 0);
        }
        return true;
    }
    static int appliedAmount(const Player *player)
    {
        int amount = 0;
        if (player) for (const QVariant &entry : QJsonDocument::fromJson(player->property("MingjianEffects").toByteArray()).toVariant().toList())
            amount += qMax(0, entry.toMap().value("amount").toInt());
        return amount;
    }
};

class MingjianTargetMod : public TargetModSkillV2
{
public:
    MingjianTargetMod() : TargetModSkillV2("#mingjian-target") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == Residue && ctx.card && ctx.card->isKindOf("Slash") && ctx.currentAmount > 0
            ? CorrectSkillResult::useAmount(Mingjian::appliedAmount(ctx.primary) * ctx.currentAmount)
            : CorrectSkillResult::noEffect();
    }
};

class MingjianKeep : public MaxCardsSkillV2
{
public:
    MingjianKeep() : MaxCardsSkillV2("#mingjian-keep") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.currentAmount > 0 ? CorrectSkillResult::useAmount(Mingjian::appliedAmount(ctx.primary) * ctx.currentAmount)
            : CorrectSkillResult::noEffect();
    }
};

NewAnguoCard::NewAnguoCard()
{
    setSkillName("newanguo");
}

bool NewAnguoCard::isOK(ServerPlayer *player, const QString &flag) const
{
    Room *room = player->getRoom();
    if (flag == "hand") {
        int hand = player->getHandcardNum();
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getHandcardNum() < hand)
                return false;
        }
    } else if (flag == "equip") {
        int equip = player->getEquips().length();
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getEquips().length() < equip)
                return false;
        }
    }
    return true;
}

void NewAnguoCard::onEffect(CardEffectStruct &effect) const
{
    if (effect.to->isDead()) return;
    Room *room = effect.to->getRoom();
    bool hand = true;
    bool recover = true;
    bool equip = true;

    if (isOK(effect.to, "hand"))
        effect.to->drawCards(1, "newanguo");
    else
        hand = false;

    if (effect.to->isAlive() && effect.to->isLowestHpPlayer())
        room->recover(effect.to, RecoverStruct("newanguo", effect.from));
    else
        recover = false;

    QList<int> equips;
    foreach (int id, room->getDrawPile()) {
        if (Sanguosha->getCard(id)->isKindOf("EquipCard"))
            equips << id;
    }
    if (isOK(effect.to, "equip")) {
        if (!equips.isEmpty()) {
            int id = equips.at(qsanRandomBounded(equips.length()));
            const Card *c = Sanguosha->getCard(id);
            if (c->isAvailable(effect.to))
                room->useCard(CardUseStruct(c, effect.to, effect.to));
        }
    } else
        equip = false;

    if (!hand && effect.from->isAlive() && isOK(effect.from, "hand"))
        effect.from->drawCards(1, "newanguo");

    if (!recover && effect.from->isAlive() && effect.from->isLowestHpPlayer())
        room->recover(effect.from, RecoverStruct("newanguo", effect.from));

    if (!equip && effect.from->isAlive() && isOK(effect.from, "equip")) {
        if (!equips.isEmpty()) {
            int id = equips.at(qsanRandomBounded(equips.length()));
            const Card *c = Sanguosha->getCard(id);
            if (c->isAvailable(effect.from))
                room->useCard(CardUseStruct(c, effect.from, effect.from));
        }
    }
}

class NewAnguo : public ViewAsSkillV2
{
public:
    NewAnguo() : ViewAsSkillV2("newanguo") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return candidate && candidate->isAlive() && candidate != request.initiator && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "NewAnguoCard"; }
    bool lowest(ServerPlayer *target, bool hand) const
    {
        const int value = hand ? target->getHandcardNum() : target->getEquips().size();
        for (ServerPlayer *other : target->getRoom()->getOtherPlayers(target))
            if ((hand ? other->getHandcardNum() : other->getEquips().size()) < value) return false;
        return true;
    }
    void equip(SkillContext &ctx, ServerPlayer *target) const
    {
        Room *room = target->getRoom();
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            // Rebuild after every ordinary use; use-card callbacks can move the remaining pile.
            QList<int> cards;
            for (int id : room->getDrawPile()) if (Sanguosha->getCard(id)->isKindOf("EquipCard")) cards << id;
            if (cards.isEmpty()) break;
            const Card *card = Sanguosha->getCard(cards.at(qsanRandomBounded(cards.size())));
            if (card->isAvailable(target)) room->useCardFromSkillEffect(CardUseStruct(card, target, target), ctx);
        }
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target || !target->isAlive()) return ContinueEffects;
        Room *room = target->getRoom();
        const int allowed = ctx.choice == "fallback" ? ctx.extra_data.toInt() : 7;
        int missing = 0;
        if (allowed & 1) {
            if (lowest(target, true)) target->drawCards(getEffectiveAmount(ctx), objectName());
            else missing |= 1;
        }
        if (allowed & 2) {
            if (target->isAlive() && target->isLowestHpPlayer()) {
                RecoverStruct recover(objectName(), ctx.invoker);
                recover.recover = getEffectiveAmount(ctx);
                room->recover(target, recover);
            } else missing |= 2;
        }
        if (allowed & 4) {
            if (target->isAlive() && lowest(target, false)) equip(ctx, target);
            else missing |= 4;
        }
        if (ctx.choice != "fallback" && missing && ctx.invoker && ctx.invoker->isAlive()) {
            SkillContext fallback = ctx;
            fallback.choice = "fallback";
            fallback.extra_data = missing;
            skillEffect(fallback, ctx.invoker);
        }
        return ContinueEffects;
    }
};
class Yaoming : public TriggerSkillV2
{
public:
    Yaoming() : TriggerSkillV2("yaoming")
    {
        events << EventSkillInvoking << Damage << Damaged; global = true;
    }

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
    LimitScope getLimitScope() const override { return Limit_Turn; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Damage && event != Damaged) return {};
        return player && player->isAlive() && player->hasSkill(this) && player->hasTurn()
            ? TriggerList{{player, usableEntries(player, data)}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        ServerPlayer *player = ctx.owner;
        if (!player) return false;
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if ((p->getHandcardNum() > player->getHandcardNum() && player->canDiscard(p, "h")) || p->getHandcardNum() < player->getHandcardNum())
                targets << p;
        }
        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@yaoming-invoke", true, false);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *player = ctx.owner;
        if (!player) return false;
        const int amount = getEffectiveAmount(ctx);
        if (target->getHandcardNum() > player->getHandcardNum()) {
            for (int i = 0; i < amount && player->isAlive() && target->isAlive() && player->canDiscard(target, "h"); ++i) {
                const int card_id = room->askForCardChosen(player, target, "h", objectName(), false, Card::MethodDiscard);
                room->throwCard(Sanguosha->getCard(card_id), target, player);
            }
        } else if (target->getHandcardNum() < player->getHandcardNum()) {
            target->drawCards(amount, objectName());
        }
        return false;
    }
};

class OLZhanjue : public Zhanjue
{
public:
    OLZhanjue() : Zhanjue("olzhanjue") {}
};

OLzhaofuCard::OLzhaofuCard()
{
    setSkillName("olzhaofu");
}

bool OLzhaofuCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.length() < 2;
}

void OLzhaofuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->removePlayerMark(source, "@olzhaofuMark");
    room->doSuperLightbox(source, "olzhaofu");

    foreach (ServerPlayer *player, targets) {
        if (player->isAlive()) {
            room->cardEffect(this, source, player);
        }
    }
}

void OLzhaofuCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    room->setPlayerMark(effect.to, "&olzhaofu", 1);
    QString kingdoms = effect.to->property("inMyAttackRangeKingdoms").toString();
    QStringList _kingdoms;
    if (!kingdoms.isEmpty())
        _kingdoms = kingdoms.split("+");
    if (_kingdoms.contains("wu")) return;
    _kingdoms << "wu";
    room->setPlayerProperty(effect.to, "inMyAttackRangeKingdoms", _kingdoms.join("+"));
}

class OLzhaofu : public ViewAsSkillV2
{
public:
    OLzhaofu() : ViewAsSkillV2("olzhaofu$", 0)
    {
        frequency = Limited;
        limit_mark = "@olzhaofuMark";
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected, const Player *target) const override
    { return target && target->isAlive() && selected.size() < 2 && !selected.contains(target); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override
    { return !selected.isEmpty() && selected.size() <= 2; }
    const Card *createCard(const ActiveSkillRequest &) const override { return new OLzhaofuCard; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.initiator->getRoom();
        // Limit_Game is committed by the active coordinator; the mark only projects it.
        room->setPlayerMark(ctx.initiator, "@olzhaofuMark", 0);
        room->doSuperLightbox(ctx.initiator, objectName());
        return ViewAsSkillV2::effect(ctx);
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target->isAlive() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom();
        QVariantList effects = target->getTag("OLZhaofuEffects").toList();
        effects << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"amount", getEffectiveAmount(ctx)}};
        target->setTag("OLZhaofuEffects", effects);
        room->setPlayerMark(target, "&olzhaofu", 1);
        QStringList kingdoms = target->property("inMyAttackRangeKingdoms").toString().split("+", Qt::SkipEmptyParts);
        if (!kingdoms.contains("wu")) {
            kingdoms << "wu";
            room->setPlayerProperty(target, "inMyAttackRangeKingdoms", kingdoms.join("+"));
        }
        return ContinueEffects;
    }
};

class OLJieQianshi : public TriggerSkillV2
{
public:
    OLJieQianshi() : TriggerSkillV2("oljieqianshi") { events << EventPhaseStart; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        const QString round = ctx.owner->getRoom()->historyScopes().value("round_id").toString();
        return !round.isEmpty() && round != "0"
            && ctx.owner->getSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "blocked_round").toString() != round;
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Finish) return result;
        for (ServerPlayer *holder : room->getAlivePlayers())
            if (holder->hasSkill(this)) result[holder] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.invoker || !isUsable(ctx) || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(ctx.invoker), false)) return false;
        ctx.targets = {ctx.invoker, ctx.owner};
        ctx.extra_data = ctx.invoker->objectName();
        ctx.manual_effect = true;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QList<ServerPlayer *> targets = ctx.targets;
        for (ServerPlayer *target : targets) {
            SkillContext recipient = ctx;
            skillEffect(event, room, ctx.owner, recipient, target);
        }
        ServerPlayer *actor = room->findPlayerByObjectName(ctx.extra_data.toString(), true);
        if (ctx.owner && actor && actor->getHandcardNum() != ctx.owner->getHandcardNum())
            ctx.owner->setSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID,
                "blocked_round", room->historyScopes().value("round_id"));
        return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};
OLJieYanyuCard::OLJieYanyuCard()
{
    setSkillName("oljieyanyu");
	will_throw = false;
	target_fixed = true;
}

void OLJieYanyuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	LogMessage log;
	log.type = "#UseCard_Recast";
	log.from = source;
	log.card_str = QString::number(getEffectiveId());
	room->sendLog(log);
	CardMoveReason reason(CardMoveReason::S_REASON_RECAST,source->objectName(),"anliao","");
	room->moveCardTo(this,source,nullptr,Player::DiscardPile,reason);
	//from->broadcastSkillInvoke("@recast");
	source->drawCards(1,"recast");
}

class OLJieYanyu : public TriggerSkillV2
{
public:
    OLJieYanyu() : TriggerSkillV2("oljieyanyu")
    { view_as_skill = new YjYanyuVS(objectName()); events << EventPhaseEnd; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->getPhase() == Player::Play && player->hasSkill(this)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (yanyuLostSlashes(room, ctx, false) < 2) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *candidate : room->getAlivePlayers()) if (candidate->isMale()) candidates << candidate;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, candidates, objectName() + "$-1", "oljieyanyu0", true, false);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target && target->isAlive() && getEffectiveAmount(ctx) > 0)
            target->drawCards(2 * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

YJCM2015Package::YJCM2015Package()
    : Package("YJCM2015")
{
    General *caorui = new General(this, "caorui$", "wei", 3);
    caorui->addSkill(new Huituo);
    caorui->addSkill(new Mingjian);
    caorui->addSkill(new MingjianTargetMod);
    caorui->addSkill(new MingjianKeep);
    related_skills.insert("mingjian", "#mingjian-target");
    related_skills.insert("mingjian", "#mingjian-keep");
    caorui->addSkill(new Xingshuai);

    General *caoxiu = new General(this, "caoxiu", "wei");
    caoxiu->addSkill(new Qianju);
    caoxiu->addSkill(new Qingxi);

    General *gongsun = new General(this, "gongsunyuan", "qun");
    gongsun->addSkill(new Huaiyi);

    General *guofeng = new General(this, "guotufengji", "qun", 3);
    guofeng->addSkill(new Jigong);
    guofeng->addSkill(new JigongMax);
    related_skills.insert("jigong", "#jigong");
    guofeng->addSkill(new Shifei);

    General *liuchen = new General(this, "liuchen$", "shu");
    liuchen->addSkill(new Zhanjue);
    liuchen->addSkill(new Qinwang);

    General *quancong = new General(this, "quancong", "wu");
    quancong->addSkill(new Zhenshan);

    General *ol_quancong = new General(this, "ol_quancong", "wu");
    ol_quancong->addSkill(new Yaoming);

    General *sunxiu = new General(this, "sunxiu$", "wu", 3);
    sunxiu->addSkill(new Yanzhu);
    sunxiu->addSkill(new Xingxue);
    sunxiu->addSkill(new Skill("zhaofu$", Skill::Compulsory));

    General *xiahou = new General(this, "yj_xiahoushi", "shu", 3, false);
    xiahou->addSkill(new Qiaoshi);
    xiahou->addSkill(new YjYanyu);

    General *zhangyi = new General(this, "zhangyi", "shu", 4);
    zhangyi->addSkill(new Wurong);
    zhangyi->addSkill(new Shizhi);
    zhangyi->addSkill(new ShizhiFilter);
    related_skills.insert("shizhi", "#shizhi");

    General *zhongyao = new General(this, "zhongyao", "wei", 3);
    zhongyao->addSkill(new Huomo);
    zhongyao->addSkill(new Zuoding);
    zhongyao->addSkill(new ZuodingRecord);
    related_skills.insert("zuoding", "#zuoding");

    General *zhuzhi = new General(this, "zhuzhi", "wu");
    zhuzhi->addSkill(new Anguo);

    General *ol_zhuzhi = new General(this, "ol_zhuzhi", "wu", 4);
    ol_zhuzhi->addSkill(new NewAnguo);

    addMetaObject<HuaiyiCard>();
    addMetaObject<HuaiyiSnatchCard>();
    addMetaObject<QinwangCard>();
    addMetaObject<ZhenshanCard>();
    addMetaObject<YanzhuCard>();
    addMetaObject<XingxueCard>();
    addMetaObject<YjYanyuCard>();
    addMetaObject<WurongCard>();
    addMetaObject<HuomoCard>();
    addMetaObject<AnguoCard>();
    addMetaObject<NewAnguoCard>();
    addMetaObject<MingjianCard>();

    skills << new QinwangDraw;
}
ADD_PACKAGE(YJCM2015)

OLStYJ2015Package::OLStYJ2015Package()
    : Package("OLStYJ2015")
{
    General *ol_liuchen = new General(this, "ol_liuchen$", "shu");
    ol_liuchen->addSkill(new OLZhanjue);
    ol_liuchen->addSkill("qinwang");

    General *ol_sunxiu = new General(this, "ol_sunxiu$", "wu", 3);
    ol_sunxiu->addSkill("yanzhu");
    ol_sunxiu->addSkill("xingxue");
    ol_sunxiu->addSkill(new OLzhaofu);

    General *oljie_xiahoushi = new General(this, "oljie_xiahoushi", "shu", 3, false);
    oljie_xiahoushi->addSkill(new OLJieQianshi);
    oljie_xiahoushi->addSkill(new OLJieYanyu);
    addMetaObject<OLJieYanyuCard>();

    addMetaObject<OLzhaofuCard>();
}
ADD_PACKAGE(OLStYJ2015)

void MigrateToNostalgiaYJCM2015(Package *pkg)
{
    General *nos_caorui = new General(pkg, "nos_caorui$", "wei", 3);
    nos_caorui->addSkill("huituo");
    nos_caorui->addSkill(new NosMingjian);
    nos_caorui->addSkill(new NosMingjianGive);
    nos_caorui->addSkill("xingshuai");
    pkg->insertRelatedSkills("nosmingjian", "#nosmingjian-give");

    General *nos_caoxiu = new General(pkg, "nos_caoxiu", "wei");
    nos_caoxiu->addSkill(new Taoxi);
}
