#include "fire.h"
//#include "general.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "engine.h"
#include "qt-collection-utils.h"
#include "maneuvering.h"
#include "clientplayer.h"
//#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include <QScopeGuard>

QuhuCard::QuhuCard()
{
    mute = true;
    setSkillName("quhu");
}

bool QuhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->getHp() > Self->getHp() && Self->canPindian(to_select);
}

void QuhuCard::use(Room *room, ServerPlayer *xunyu, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *tiger = targets.first();

    int index = 1;
    if (xunyu->isJieGeneral())
        index = 3;
    room->broadcastSkillInvoke("quhu", index);

    bool success = xunyu->pindian(tiger, "quhu", nullptr);
    if (success) {
        index = 2;
        if (xunyu->isJieGeneral())
            index = 4;
        room->broadcastSkillInvoke("quhu", index);

        QList<ServerPlayer *> players = room->getOtherPlayers(tiger), wolves;
        foreach (ServerPlayer *player, players) {
            if (tiger->inMyAttackRange(player))
                wolves << player;
        }

        if (wolves.isEmpty()) {
            LogMessage log;
            log.type = "#QuhuNoWolf";
            log.from = xunyu;
            log.to << tiger;
            room->sendLog(log);

            return;
        }

        ServerPlayer *wolf = room->askForPlayerChosen(xunyu, wolves, "quhu", QString("@quhu-damage:%1").arg(tiger->objectName()));
        room->damage(DamageStruct("quhu", tiger, wolf));
    } else {
        room->damage(DamageStruct("quhu", tiger, xunyu));
    }
}

class Jieming : public TriggerSkillV2
{
public:
    Jieming() : TriggerSkillV2("jieming") { events << Damaged; m_baseAmount = 5; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const int damage = data.value<DamageStruct>().damage;
        return player && player->isAlive() && player->hasSkill(objectName()) && damage > 0
            ? TriggerList{{player, {objectName() + '*' + QString::number(damage)}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        // Choose each recipient after the preceding damage-point activation has finished.
        ServerPlayer *target = room->askForPlayerChosen(owner, room->getAlivePlayers(), objectName(), "jieming-invoke", true, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int count = qMin(getEffectiveAmount(ctx), target->getMaxHp()) - target->getHandcardNum();
        if (count > 0) { room->broadcastSkillInvoke(objectName()); target->drawCards(count, objectName()); }
        return false;
    }
};

class Quhu : public ViewAsSkillV2
{
public:
    Quhu() : ViewAsSkillV2("quhu")
    { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canPindian();
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return request.initiator && selected.isEmpty() && candidate && candidate != request.initiator
            && candidate->getHp() > request.initiator->getHp()
            && request.initiator->canPindian(candidate);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "QuhuCard"; }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new ActiveSkillCard;
        card->setActiveSkill(this);
        card->setSkillName(objectName());
        return card;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target) return ContinueEffects;
        Room *room = source->getRoom();
        if (ctx.choice == "damage") {
            ServerPlayer *tiger = room->findPlayerByObjectName(ctx.extra_data.toString(), true);
            if (tiger) room->damage(DamageStruct(objectName(), tiger, target, getEffectiveAmount(ctx)));
            return ContinueEffects;
        }
        if (!source->canPindian(target)) return ContinueEffects;
        room->broadcastSkillInvoke(objectName(), source->isJieGeneral() ? 3 : 1);
        const bool success = source->pindian(target, objectName(), nullptr);
        if (success) {
            room->broadcastSkillInvoke(objectName(), source->isJieGeneral() ? 4 : 2);
            QList<ServerPlayer *> wolves;
            for (ServerPlayer *player : room->getOtherPlayers(target))
                if (target->inMyAttackRange(player)) wolves << player;
            if (wolves.isEmpty()) {
                LogMessage log;
                log.type = "#QuhuNoWolf";
                log.from = source;
                log.to << target;
                room->sendLog(log);
            } else {
                ServerPlayer *wolf = room->askForPlayerChosen(source, wolves, objectName(),
                    QString("@quhu-damage:%1").arg(target->objectName()));
                ctx.choice = "damage";
                ctx.extra_data = target->objectName();
                skillEffect(ctx, wolf);
            }
        } else {
            ctx.choice = "damage";
            ctx.extra_data = target->objectName();
            skillEffect(ctx, source);
        }
        return ContinueEffects;
    }
};

QiangxiCard::QiangxiCard()
{
    setSkillName("qiangxi");
}

bool QiangxiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return Self->inMyAttackRange(to_select, subcards) && targets.isEmpty() && to_select != Self;
}

void QiangxiCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *from = effect.from, *to = effect.to;
    Room *room = to->getRoom();

    if (subcards.isEmpty())
        room->loseHp(HpLostStruct(from, 1, "qiangxi", from));

    room->damage(DamageStruct("qiangxi", from, to));
}

class Qiangxi : public ViewAsSkillV2
{
public:
    Qiangxi() : ViewAsSkillV2("qiangxi")
    { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        return request.initiator && to_select && request.selectedCardIds.isEmpty()
            && !to_select->hasFlag("using") && to_select->isKindOf("Weapon")
            && request.initiator->canDiscard(to_select->getEffectiveId())
            && request.initiator->hasCard(to_select);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty()) return true;
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
        return card && canSelectCard(selection, card);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new ActiveSkillCard;
        card->setActiveSkill(this);
        card->setSkillName(objectName());
        if (!request.selectedCardIds.isEmpty()) card->addSubcard(Sanguosha->getCard(request.selectedCardIds.first()));
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "QiangxiCard"; }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty()) {
            if (!ctx.invoker) return false;
            room->loseHp(HpLostStruct(ctx.invoker, 1, objectName(), ctx.invoker));
            return true;
        }
        return ViewAsSkillV2::pay(room, ctx, request);
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return request.initiator && candidate && selected.isEmpty() && candidate != request.initiator
            && request.initiator->inMyAttackRange(candidate, request.selectedCardIds);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 1;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target) return ContinueEffects;
        Room *room = source->getRoom();
        room->damage(DamageStruct(objectName(), source, target, getEffectiveAmount(ctx)));
        return ContinueEffects;
    }
};

class Luanji : public ViewAsSkillV2
{
public:
    Luanji() : ViewAsSkillV2("luanji", 2)
    {
        response_or_use = true;
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && (request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
            || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->hasFlag("using")
            || to_select->isEquipped() || request.selectedCardIds.size() >= 2) return false;
        if (!ViewAsSkillV2::canSelectCard(request, to_select)) return false;
        if (request.selectedCardIds.isEmpty()) return true;
        const Card *first = Sanguosha->getCard(request.selectedCardIds.first());
        return first && first->getSuit() == to_select->getSuit();
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 2) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            const Card *card = Sanguosha->getCard(id);
            if (!card || !canSelectCard(selection, card)) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *aa = new ArcheryAttack(Card::SuitToBeDecided, 0);
        aa->addSubcard(Sanguosha->getCard(request.selectedCardIds.at(0)));
        aa->addSubcard(Sanguosha->getCard(request.selectedCardIds.at(1)));
        aa->setSkillName(objectName());
        return aa;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "ArcheryAttack"; }
};

class Xueyi : public TriggerSkillV2
{
public:
    Xueyi() : TriggerSkillV2("xueyi$")
    {
        events << EventPhaseChanging;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *target, QVariant &data) const override
    {
        return event == EventPhaseChanging && target && target->isAlive()
            && target->hasLordSkill(objectName())
            && data.value<PhaseChangeStruct>().to == Player::Discard
            ? TriggerList{{target, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner) room->broadcastSkillInvoke(objectName(), ctx.owner);
        return false;
    }
};

class XueyiMCS : public MaxCardsSkillV2
{
public:
    XueyiMCS() : MaxCardsSkillV2("#xueyi")
    { m_baseAmount = 2; }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        const Player *target = context.primary;
        if (target && target->hasLordSkill("xueyi")) {
            int extra = 0;
            foreach (const Player *p, target->getAliveSiblings()) {
                if (p->getKingdom() == "qun")
                    extra += context.currentAmount;
            }
            return CorrectSkillResult::useAmount(extra);
        }
        return CorrectSkillResult::noEffect();
    }
};

class ShuangxiongViewAsSkill : public ViewAsSkillV2
{
public:
    ShuangxiongViewAsSkill() : ViewAsSkillV2("shuangxiong", 1) { response_or_use = true; }
    static QStringList colors(const ActiveSkillRequest &request)
    {
        if (!request.initiator || !request.activationRef.isValid()) return {};
        return request.initiator->getSkillInstanceStateValue("shuangxiong", request.activationRef.key.instanceID, "colors").toStringList();
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && !colors(request).isEmpty();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || !ViewAsSkillV2::canSelectCard(request, card)
            || card->hasFlag("using") || card->isEquipped() || card->getEffectiveId() < 0
            || request.selectedCardIds.contains(card->getEffectiveId())) return false;
        const QStringList available = colors(request);
        return (card->isRed() && available.contains("no_suit_black"))
            || (card->isBlack() && available.contains("no_suit_red"));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *original = Sanguosha->getCard(request.selectedCardIds.first());
        // Retain the original conversion identity and material; ordinary card use pays it.
        auto *duel = new Duel(original->getSuit(), original->getNumber());
        duel->addSubcard(original);
        duel->setSkillName("_shuangxiong");
        return duel;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Duel"; }
};

class Shuangxiong : public TriggerSkillV2
{
public:
    Shuangxiong() : TriggerSkillV2("shuangxiong") { events << EventPhaseStart; view_as_skill = new ShuangxiongViewAsSkill; }
    bool canPreshow() const override { return true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Draw
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { if (!owner->askForSkillInvoke(this)) return false; ctx.targets << owner; return true; }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        // Only an executed target effect replaces the normal draw phase.
        return skillEffect(event, room, owner, ctx, owner);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName(), owner);
        JudgeStruct judge;
        judge.good = true;
        judge.play_animation = false;
        judge.pattern = ".";
        judge.reason = objectName();
        judge.who = target;
        room->judge(judge);
        if (!judge.card) return false;
        const QString color = judge.card->isBlack() ? "no_suit_black" : judge.card->isRed() ? "no_suit_red" : QString();
        if (owner->hasSkillInstance(objectName(), ctx.instanceID) && !color.isEmpty()) {
            // The conversion permission belongs to the exact source, never a sibling Shuangxiong instance.
            QStringList colors = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "colors").toStringList();
            if (!colors.contains(color)) colors << color;
            owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "colors", colors);
            QStringList projection = owner->property("shuangxiong_colors_turn").toString().split('+', Qt::SkipEmptyParts);
            if (!projection.contains(color)) projection << color;
            room->setPlayerProperty(owner, "shuangxiong_colors_turn", projection.join('+'));
            room->setPlayerMark(owner, "ViewAsSkill_shuangxiongEffect", 1);
            room->setPlayerFlag(owner, "shuangxiong");
        }
        return true;
    }
};

class ShuangxiongGet : public TriggerSkillV2
{
public:
    ShuangxiongGet() : TriggerSkillV2("#shuangxiong")
    {
        events << FinishJudge << EventPhaseChanging;
        frequency = Compulsory;
        global = true;
    }
    bool canPreshow() const override { return false; }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent) const override { return 3; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        // Every mode accumulates judgment colors for this turn; cleanup survives skill loss.
        for (ServerPlayer *p : room->getAllPlayers(true)) {
            for (int id : p->getSkillInstanceIds("shuangxiong")) p->removeSkillInstanceStateValue("shuangxiong", id, "colors");
            if (!p->property("shuangxiong_colors_turn").toString().isEmpty())
                room->setPlayerProperty(p, "shuangxiong_colors_turn", QString());
            if (p->hasFlag("shuangxiong")) room->setPlayerFlag(p, "-shuangxiong");
            if (p->getMark("ViewAsSkill_shuangxiongEffect") != 0)
                room->setPlayerMark(p, "ViewAsSkill_shuangxiongEffect", 0);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
        QList<SkillContext> &contexts) const override
    {
        if (event != FinishJudge) return true;
        const JudgeStruct *judge = data.value<JudgeStruct *>();
        // The pending judgment owns its award even if its initiating skill was lost.
        if (player && judge && judge->who == player && judge->reason == "shuangxiong"
            && judge->card && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge) {
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = player;
            ctx.invoker = player;
            ctx.initiator = player;
            ctx.original_data = &data;
            ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        const JudgeStruct *judge = ctx.original_data ? ctx.original_data->value<JudgeStruct *>() : nullptr;
        return ctx.owner && judge && judge->who == ctx.owner && judge->reason == "shuangxiong"
            && judge->card && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets << ctx.invoker; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (judge && judge->card && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge)
            target->obtainCard(judge->card);
        return false;
    }
};

class Mengjin : public TriggerSkillV2
{
public:
    Mengjin() : TriggerSkillV2("mengjin") { events << CardOffset; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && effect.from == player
            && effect.card && effect.card->isKindOf("Slash") && effect.to && effect.to->isAlive() && player->canDiscard(effect.to, "he")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(objectName() + "$-1", *ctx.original_data)) return false;
        ctx.targets << ctx.original_data->value<CardEffectStruct>().to;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive() && owner->canDiscard(target, "he"); ++i) {
            const int id = room->askForCardChosen(owner, target, "he", objectName(), false, Card::MethodDiscard);
            if (!target->handCards().contains(id) && !target->getEquipsId().contains(id)) break;
            if (!owner->canDiscard(target, id)) break;
            room->throwCard(id, objectName(), target, owner);
        }
        return false;
    }
};

class Lianhuan : public ViewAsSkillV2
{
public:
    Lianhuan() : ViewAsSkillV2("lianhuan", 1) { response_or_use = true; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !ViewAsSkillV2::canSelectCard(request, card)
            || card->hasFlag("using")) return false;
        // Preserve the original response-accessible hand piles without global Self.
        QStringList places{"hand"};
        for (const QString &pile : request.initiator->getPileNames())
            if (pile.startsWith("&") || pile == "wooden_ox") places << pile;
        return Sanguosha->matchExpPattern(".|club|.|" + places.join(","), request.initiator, card);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "IronChain"; }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        auto *card = new IronChain(material->getSuit(), material->getNumber());
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }
};

class Niepan : public TriggerSkillV2
{
public:
    Niepan() : TriggerSkillV2("niepan")
    {
        events << AskForPeaches;
        m_baseAmount = 3;
        frequency = Limited;
        limit_mark = "@nirvana";
    }

    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->getMark(limit_mark) > 0; }
    void addUsage(const SkillContext &ctx) const override
    { if (ctx.owner) ctx.owner->getRoom()->removePlayerMark(ctx.owner, limit_mark); }
    void resetUsage(const SkillContext &ctx) const override
    { if (ctx.owner) ctx.owner->getRoom()->setPlayerMark(ctx.owner, limit_mark, 1); }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *target, QVariant &data) const override
    {
        return target && target->isAlive() && target->hasSkill(objectName())
            && target->getMark(limit_mark) > 0 && data.value<DyingStruct>().who == target
            ? TriggerList{{target, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << ctx.owner;
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Spend the limited token only after the selected V2 source pays.
        if (!ctx.owner || ctx.owner->getMark(limit_mark) <= 0) return false;
        addUsage(ctx);
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *pangtong) const override
    {
        if (pangtong && pangtong->isAlive()) {
            room->doSuperLightbox(pangtong, "niepan");

            pangtong->throwAllCards(objectName());

            int n = qMin(getEffectiveAmount(ctx) - pangtong->getHp(), pangtong->getMaxHp() - pangtong->getHp());
            if (n > 0)
                room->recover(pangtong, RecoverStruct(pangtong, nullptr, n, objectName()));

            if (pangtong->isAlive()) pangtong->drawCards(getEffectiveAmount(ctx), objectName());

            if (pangtong->isChained())
                room->setPlayerChained(pangtong);

            if (!pangtong->faceUp())
                pangtong->turnOver();
        }

        return false;
    }
};

class Huoji : public ViewAsSkillV2
{
public:
    Huoji() : ViewAsSkillV2("huoji", 1) { response_or_use = true; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !ViewAsSkillV2::canSelectCard(request, card)
            || card->hasFlag("using")) return false;
        // Preserve the original response-accessible hand piles without global Self.
        QStringList places{"hand"};
        for (const QString &pile : request.initiator->getPileNames())
            if (pile.startsWith("&") || pile == "wooden_ox") places << pile;
        return Sanguosha->matchExpPattern(".|red|.|" + places.join(","), request.initiator, card);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "FireAttack"; }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        auto *card = new FireAttack(material->getSuit(), material->getNumber());
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }
};

class Bazhen : public ViewAsEquipSkill
{
public:
    Bazhen() : ViewAsEquipSkill("bazhen")
    {
    }

    QString viewAsEquip(const Player *target) const
    {
        if (target->hasEquipArea(1) && !target->getArmor())
            return "eight_diagram";
        return "";
    }
};

class BazhenTrigger : public TriggerSkillV2
{
public:
    BazhenTrigger() : TriggerSkillV2("#bazhen")
    {
        events << InvokeSkill;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        // The virtual armor pipeline has already selected and revealed its exact source.
        return player && player->isAlive() && player->hasSkill("bazhen")
            && data.toString() == "eight_diagram"
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (!player) return false;
        int index = qsanRandomBounded(2)+1;
        if (player->isJieGeneral("wolong") || player->isJieGeneral("zhugeliang"))
            index += 2;
        else if (player->isJieGeneral("pangtong"))
            index += 4;
        room->sendCompulsoryTriggerLog(player, "bazhen", true, true, index);
        return false;
    }
};

class Kanpo : public ViewAsSkillV2
{
public:
    Kanpo() : ViewAsSkillV2("kanpo", 1)
    {
        response_or_use = true;
        // ServerPlayer::hasNullification still queries the legacy response probe.
        response_pattern = "nullification";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "nullification"
            && (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
                || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !ViewAsSkillV2::canSelectCard(request, card)
            || card->hasFlag("using")) return false;
        // Preserve the original response-accessible hand piles without global Self.
        QStringList places{"hand"};
        for (const QString &pile : request.initiator->getPileNames())
            if (pile.startsWith("&") || pile == "wooden_ox") places << pile;
        return Sanguosha->matchExpPattern(".|black|.|" + places.join(","), request.initiator, card);
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        return canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "Nullification"; }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        auto *card = new Nullification(material->getSuit(), material->getNumber());
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }
};

TianyiCard::TianyiCard()
{
    setSkillName("tianyi");
}

bool TianyiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void TianyiCard::use(Room *room, ServerPlayer *taishici, QList<ServerPlayer *> &targets) const
{
    bool success = taishici->pindian(targets.first(), "tianyi", nullptr);
    if (success)
        room->setPlayerFlag(taishici, "TianyiSuccess");
    else
        room->setPlayerCardLimitation(taishici, "use", "Slash", true);
}

class Tianyi : public ViewAsSkillV2
{
public:
    Tianyi() : ViewAsSkillV2("tianyi")
    { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->canPindian();
    }

    TargetMode targetMode() const override { return SelectTargets; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return request.initiator && selected.isEmpty() && candidate && candidate != request.initiator
            && request.initiator->canPindian(candidate);
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 1;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "TianyiCard"; }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new ActiveSkillCard;
        card->setActiveSkill(this);
        card->setSkillName(objectName());
        return card;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target) return ContinueEffects;
        Room *room = source->getRoom();
        if (ctx.choice == "success") {
            // The turn-long allowance is an applied effect and survives loss of Tianyi.
            room->addPlayerMark(target, "TianyiResidue-Clear", getEffectiveAmount(ctx));
            room->addPlayerMark(target, "TianyiTargets-Clear", getEffectiveAmount(ctx));
            room->setPlayerFlag(target, "TianyiSuccess");
            return ContinueEffects;
        }
        if (ctx.choice == "failure") {
            room->setPlayerCardLimitation(target, "use", "Slash", true, objectName());
            return ContinueEffects;
        }
        if (!source->canPindian(target)) return ContinueEffects;
        ctx.choice = source->pindian(target, objectName(), nullptr) ? "success" : "failure";
        skillEffect(ctx, source);
        return ContinueEffects;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = 1;
        if (player->isJieGeneral())
            index += qsanRandomBounded(2) + 1;
        return index;
    }
};

class TianyiTargetMod : public TargetModSkillV2
{
public:
    TianyiTargetMod() : TargetModSkillV2("#tianyi-target", "Slash")
    {
        frequency = NotFrequent;
        setHolderSelector(CorrectSkill_System);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &context) const override
    {
        if (!context.primary || !context.primary->hasFlag("TianyiSuccess"))
            return CorrectSkillResult::noEffect();
        if (context.modType == TargetModSkill::Residue)
            return CorrectSkillResult::useAmount(context.primary->getMark("TianyiResidue-Clear"));
        if (context.modType == TargetModSkill::DistanceLimit)
            return CorrectSkillResult::useAmount(1000);
        if (context.modType == TargetModSkill::ExtraTarget)
            return CorrectSkillResult::useAmount(context.primary->getMark("TianyiTargets-Clear"));
        return CorrectSkillResult::noEffect();
    }
};

void YeyanCard::damage(ServerPlayer *shenzhouyu, ServerPlayer *target, int point) const
{
    shenzhouyu->getRoom()->damage(DamageStruct("yeyan", shenzhouyu, target, point, DamageStruct::Fire));
}

GreatYeyanCard::GreatYeyanCard()
{
    mute = true;
    setSkillName("yeyan");
}

bool GreatYeyanCard::targetFilter(const QList<const Player *> &, const Player *, const Player *) const
{
    Q_ASSERT(false);
    return false;
}

bool GreatYeyanCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length()==3;
}

bool GreatYeyanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select,
    const Player *, int &maxVotes) const
{
	if(qsanToSet(targets).size()==2&&!targets.contains(to_select))
		return false;
	int i = 0;
	foreach(const Player *player, targets)
		if (player == to_select) i++;
	maxVotes = qMax(3 - targets.size(), 0) + i;
	return maxVotes > 0;
}

void GreatYeyanCard::onUse(Room *room, CardUseStruct &card_use) const
{
    QList<ServerPlayer *> targets;
	foreach(ServerPlayer *sp, card_use.to){
		sp->addMark("yeyan_damage");
		if(!targets.contains(sp))
			targets << sp;
	}
	card_use.to = targets;
    YeyanCard::onUse(room, card_use);
}

void GreatYeyanCard::use(Room *room, ServerPlayer *shenzhouyu, QList<ServerPlayer *> &targets) const
{
    room->removePlayerMark(shenzhouyu, "@flame");
	room->broadcastSkillInvoke("yeyan", (targets.length() > 1) ? 2 : 3);
	room->doSuperLightbox(shenzhouyu, "yeyan");
	room->loseHp(HpLostStruct(shenzhouyu, 3, "yeyan", shenzhouyu));

	foreach(ServerPlayer *sp, targets){
		damage(shenzhouyu, sp, sp->getMark("yeyan_damage"));
		sp->setMark("yeyan_damage",0);
	}
}

SmallYeyanCard::SmallYeyanCard()
{
    mute = true;
    setSkillName("yeyan");
}

bool SmallYeyanCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.length() < 3;
}

void SmallYeyanCard::use(Room *room, ServerPlayer *shenzhouyu, QList<ServerPlayer *> &targets) const
{
    room->removePlayerMark(shenzhouyu, "@flame");
    room->broadcastSkillInvoke("yeyan", 1);
    room->doSuperLightbox(shenzhouyu, "yeyan");
    YeyanCard::use(room, shenzhouyu, targets);
}

void SmallYeyanCard::onEffect(CardEffectStruct &effect) const
{
    damage(effect.from, effect.to, 1);
}

YeyanV2Card::YeyanV2Card() { setSkillName("yeyan"); }

bool YeyanV2Card::targetFilter(const QList<const Player *> &targets, const Player *candidate,
                               const Player *source, int &maxVotes) const
{
    // Keep the native vote selector while all eligibility and effects remain in the V2 skill.
    QList<const Player *> others = targets;
    others.removeAll(candidate);
    const bool allowed = ActiveSkillCard::targetFilter(others, candidate, source);
    maxVotes = allowed ? (getSubcards().isEmpty() ? 1 : qMax(0, 3 - int(others.size()))) : 0;
    return allowed;
}

class Yeyan : public ViewAsSkillV2
{
public:
    Yeyan() : ViewAsSkillV2("yeyan", 4)
    {
        frequency = Limited;
        limit_mark = "@flame";
    }

    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    { return ctx.owner && ctx.owner->getMark(limit_mark) > 0; }
    void addUsage(const SkillContext &ctx) const override
    { if (ctx.owner) ctx.owner->getRoom()->removePlayerMark(ctx.owner, limit_mark); }
    void resetUsage(const SkillContext &ctx) const override
    { if (ctx.owner) ctx.owner->getRoom()->setPlayerMark(ctx.owner, limit_mark, 1); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMark("@flame") >= 1;
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->hasFlag("using")
            || to_select->isEquipped() || request.selectedCardIds.size() >= 4
            || !request.initiator->handCards().contains(to_select->getEffectiveId())
            || !request.initiator->canDiscard(to_select->getEffectiveId())) return false;
        if (!ViewAsSkillV2::canSelectCard(request, to_select)) return false;
        for (int id : request.selectedCardIds) {
            const Card *selected = Sanguosha->getCard(id);
            if (selected && selected->getSuit() == to_select->getSuit()) return false;
        }
        return true;
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty()) return true;
        if (request.selectedCardIds.size() != 4) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            const Card *card = Sanguosha->getCard(id);
            if (!card || !canSelectCard(selection, card)) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new YeyanV2Card;
        card->setActiveSkill(this);
        card->setSkillName(objectName());
        for (int id : request.selectedCardIds) card->addSubcard(Sanguosha->getCard(id));
        return card;
    }

    QString historyKey(const ActiveSkillRequest &request) const override
    {
        return request.selectedCardIds.isEmpty() ? "SmallYeyanCard" : "GreatYeyanCard";
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        if (!request.initiator || !candidate || !candidate->isAlive()) return false;
        if (request.selectedCardIds.isEmpty()) return selected.size() < 3 && !selected.contains(candidate);
        if (qsanToSet(selected).size() == 2 && !selected.contains(candidate)) return false;
        return selected.size() < 3;
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        return request.selectedCardIds.isEmpty() ? !targets.isEmpty() && targets.size() <= 3 && qsanToSet(targets).size() == targets.size()
            : targets.size() == 3 && qsanToSet(targets).size() <= 2;
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!isUsable(ctx) || !cardSelectionFeasible(request)) return false;
        // The limited resource is consumed before discard/HP callbacks can re-enter this activation.
        addUsage(ctx);
        if (!ViewAsSkillV2::pay(room, ctx, request)) return false;
        if (!request.selectedCardIds.isEmpty()) room->loseHp(HpLostStruct(ctx.invoker, 3, objectName(), ctx.invoker));
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.use_card) return FinishSkill;
        Room *room = ctx.invoker->getRoom();
        QList<ServerPlayer *> targets;
        QVariantMap votes;
        for (ServerPlayer *target : ctx.targets) {
            if (!target) continue;
            votes.insert(target->objectName(), votes.value(target->objectName()).toInt() + 1);
            if (!targets.contains(target)) targets << target;
        }
        room->broadcastSkillInvoke(objectName(), ctx.use_card->getSubcards().isEmpty() ? 1 : targets.size() > 1 ? 2 : 3);
        room->doSuperLightbox(ctx.invoker, objectName());
        // Accumulated votes live only in this execution, not on recipient marks shared by nested uses.
        ctx.extra_data = votes;
        for (ServerPlayer *target : targets) {
            ctx.choice = QString::number(votes.value(target->objectName()).toInt());
            skillEffect(ctx, target);
        }
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        const int points = ctx.choice.toInt() * getEffectiveAmount(ctx);
        if (ctx.invoker && points > 0) target->getRoom()->damage(DamageStruct(objectName(), ctx.invoker, target, points, DamageStruct::Fire));
        return ContinueEffects;
    }
};

class Qinyin : public TriggerSkillV2
{
public:
    Qinyin() : TriggerSkillV2("qinyin") { events << EventPhaseEnd; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Discard) return {};
        QVariantMap filter{{"phase_id", room->historyScopes().value("phase_id")}, {"from", player->objectName()}, {"limit", 128}};
        int discarded = 0;
        while (true) {
            const QVariantMap page = room->queryHistoryMoves(filter);
            if (!page.value("complete").toBool()) return {};
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap move = value.toMap().value("data").toMap();
                if ((move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) ++discarded;
            }
            // Each move fact represents one physical card, including discards before skill acquisition.
            if (discarded >= 2) return TriggerList{{player, {objectName()}}};
            if (!page.value("has_more").toBool()) return {};
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        QStringList choices{"down", "cancel"};
        for (ServerPlayer *player : room->getAlivePlayers()) if (player->isWounded()) { choices.prepend("up"); break; }
        ctx.choice = room->askForChoice(owner, objectName(), choices.join('+'));
        if (ctx.choice == "cancel") return false;
        ctx.targets = room->getAllPlayers();
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const int index = ctx.choice == "up" ? 2 : room->findPlayer("caocao+shencaocao+yt_shencaocao") ? 3 : 1;
        room->broadcastSkillInvoke(objectName(), index);
        room->notifySkillInvoked(owner, objectName());
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "up") room->recover(target, RecoverStruct(objectName(), owner, getEffectiveAmount(ctx)));
        else if (ctx.choice == "down") room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), owner));
        return false;
    }
};

QixingCard::QixingCard()
{
    setSkillName("qixing");
    will_throw = false;
    handling_method = Card::MethodNone;
    target_fixed = true;
}

void QixingCard::onUse(Room *room, CardUseStruct &card_use) const
{
    QList<int> pile = card_use.from->getPile("stars");
    QList<int> subCards = card_use.card->getSubcards();
    QList<int> to_handcard, to_pile;
    foreach (int id, subCards) {
        if (pile.contains(id))
            to_handcard << id;
        else
            to_pile << id;
    }

    if (to_handcard.length() != to_pile.length())
        return;

    room->broadcastSkillInvoke("qixing");
    room->notifySkillInvoked(card_use.from, "qixing");

    card_use.from->addToPile("stars", to_pile, false);

    DummyCard to_handcard_x(to_handcard);
    CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, card_use.from->objectName());
    room->obtainCard(card_use.from, &to_handcard_x, reason, false);

    LogMessage log;
    log.type = "#QixingExchange";
    log.from = card_use.from;
    log.arg = QString::number(to_pile.length());
    log.arg2 = "qixing";
    room->sendLog(log);
}

class QixingVS : public ViewAsSkillV2
{
public:
    QixingVS() : ViewAsSkillV2("qixing")
    {
        response_pattern = "@@qixing";
        expand_pile = "stars";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@qixing"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && !request.initiator->getPile("stars").isEmpty();
    }

    bool willThrowSelectedCards() const override { return false; }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *to_select) const override
    {
        if (!request.initiator || !to_select || to_select->isEquipped() || to_select->hasFlag("using")) return false;
        if (request.selectedCardIds.size() >= 2 * request.initiator->getPile("stars").size()) return false;
        const int id = to_select->getEffectiveId();
        return id >= 0 && !request.selectedCardIds.contains(id) && (request.initiator->handCards().contains(id)
            || request.initiator->getPile("stars").contains(id));
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty() || request.selectedCardIds.size() % 2 != 0) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        int hand = 0, pile = 0;
        for (int id : request.selectedCardIds) {
            const Card *card = Sanguosha->getCard(id);
            if (!card || !canSelectCard(selection, card)) return false;
            selection.selectedCardIds << id;
            if (request.initiator->getPile("stars").contains(id)) ++pile; else ++hand;
        }
        return hand == pile;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new ActiveSkillCard;
        card->setActiveSkill(this);
        card->setSkillName(objectName());
        for (int id : request.selectedCardIds) card->addSubcard(Sanguosha->getCard(id));
        return card;
    }

    TargetMode targetMode() const override { return NoTarget; }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.isEmpty(); }
    QString historyKey(const ActiveSkillRequest &) const override { return "QixingCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.invoker) skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.use_card) return ContinueEffects;
        QList<int> toHand, toPile;
        const QList<int> pile = target->getPile("stars");
        for (int id : ctx.use_card->getSubcards()) {
            if (pile.contains(id)) toHand << id;
            else if (target->handCards().contains(id)) toPile << id;
            else return ContinueEffects;
        }
        if (toHand.isEmpty() || toHand.size() != toPile.size()) return ContinueEffects;
        Room *room = target->getRoom();
        room->broadcastSkillInvoke(objectName());
        // Exchange in one move batch: nested move triggers must not see only half of the exchange.
        const CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, target->objectName(), objectName(), "");
        CardsMoveStruct out(toHand, target, target, Player::PlaceSpecial, Player::PlaceHand, reason);
        out.from_pile_name = "stars";
        CardsMoveStruct in(toPile, target, target, Player::PlaceHand, Player::PlaceSpecial, reason);
        in.to_pile_name = "stars";
        room->moveCardsAtomic({out, in}, false);
        LogMessage log;
        log.type = "#QixingExchange";
        log.from = target;
        log.arg = QString::number(toPile.size());
        log.arg2 = objectName();
        room->sendLog(log);
        return ContinueEffects;
    }
};

class Qixing : public TriggerSkillV2
{
public:
    Qixing() : TriggerSkillV2("qixing")
    { view_as_skill = new QixingVS; events << EventPhaseEnd << DrawNCards << AfterDrawNCards; global = true; m_baseAmount = 7; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == DrawNCards && data.value<DrawStruct>().reason == "InitialHandCards") return {{player, {objectName()}}};
        if (event == EventPhaseEnd && player->getPhase() == Player::Draw && !player->getPile("stars").isEmpty()) return {{player, {objectName()}}};
        return {};
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != AfterDrawNCards) return false;
        if (!player || !player->isAlive() || data.value<DrawStruct>().reason != "InitialHandCards") return true;
        for (const QVariant &value : player->getTag("QixingInitialDraw").toList()) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.initiator = owner;
            ctx.invoker = player;
            ctx.instanceID = receipt.value("instance").toInt();
            ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(objectName(), ctx.instanceID));
            ctx.amount = receipt.value("amount").toInt();
            ctx.extra_data = receipt;
            ctx.targets << player;
            ctx.original_data = &data;
            ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx)
            : ctx.invoker && ctx.invoker->getTag("QixingInitialDraw").toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event != EventPhaseEnd) { if (ctx.targets.isEmpty()) ctx.targets << ctx.invoker; return true; }
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int previous = owner->getMark(selector);
        auto restore = qScopeGuard([&] { room->setPlayerMark(owner, selector, previous); });
        room->setPlayerMark(owner, selector, ctx.activationRef.key.instanceID);
        room->askForUseCard(owner, "@@qixing", "@qixing-exchange", -1, Card::MethodNone);
        return false;
    }
    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == AfterDrawNCards) {
            QVariantList receipts = ctx.invoker->getTag("QixingInitialDraw").toList();
            receipts.removeOne(ctx.extra_data);
            ctx.invoker->setTag("QixingInitialDraw", receipts);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == DrawNCards) {
            if (target != ctx.invoker) return false;
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            draw.num += getEffectiveAmount(ctx);
            ctx.original_data->setValue(draw);
            QVariantList receipts = target->getTag("QixingInitialDraw").toList();
            receipts << QVariantMap{{"owner", owner->objectName()}, {"instance", ctx.instanceID}, {"amount", getEffectiveAmount(ctx)}};
            target->setTag("QixingInitialDraw", receipts);
            room->sendCompulsoryTriggerLog(owner, this);
        } else {
            const int count = qMin(getEffectiveAmount(ctx), target->getHandcardNum());
            if (count <= 0) return false;
            const Card *cards = room->askForExchange(target, objectName(), count, count);
            if (!cards || cards->subcardsLength() != count) return false;
            for (int id : cards->getSubcards()) if (!target->handCards().contains(id)) return false;
            target->addToPile("stars", cards->getSubcards(), false);
        }
        return false;
    }
};

KuangfengCard::KuangfengCard()
{
    setSkillName("kuangfeng");
    handling_method = Card::MethodNone;
    will_throw = false;
}

bool KuangfengCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}

void KuangfengCard::onEffect(CardEffectStruct &effect) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, effect.from->objectName(), "kuangfeng", "");
    effect.to->getRoom()->throwCard(this, reason, nullptr);
    effect.to->gainMark("&kuangfeng");
	effect.from->setTag("kuangfengUse", true);
}

// Weather receipts describe already-applied effects; they survive loss of the granting skill.
static void applyFireWeather(Room *room, const QString &name, const SkillContext &ctx, ServerPlayer *target, int amount)
{
    const QString key = "FireWeather_" + name;
    QVariantList receipts = target->getTag(key).toList();
    const QString owner = ctx.activationRef.ownerObjectName;
    const QString activationSkill = ctx.activationRef.key.skillName;
    const int instance = ctx.activationRef.key.instanceID;
    if (!ctx.activationRef.isValid() || !ctx.invoker || !ctx.sourceRef.isValid()) return;
    for (int i = receipts.size() - 1; i >= 0; --i) {
        const QVariantMap old = receipts.at(i).toMap();
        if (old.value("owner").toString() == owner
            && old.value("activation_skill").toString() == activationSkill
            && old.value("instance").toInt() == instance) {
            receipts.removeAt(i);
            room->removePlayerMark(target, "&" + name);
        }
    }
    // Instance numbers are per skill. A separate receipt ID keeps different granting
    // skills distinct when both continue through the same weather definition.
    const QString dispatchKey = "FireWeatherNextReceipt_" + name;
    const int dispatch = target->getTag(dispatchKey).toInt() + 1;
    target->setTag(dispatchKey, dispatch);
    // Keep the paid activation, actual caster and immutable borrowed provenance distinct.
    receipts << QVariantMap{{"owner", owner}, {"activation_skill", activationSkill},
        {"instance", instance}, {"dispatch", dispatch}, {"amount", amount},
        {"actor", ctx.invoker->objectName()}, {"source_owner", ctx.sourceRef.ownerObjectName},
        {"source_skill", ctx.sourceRef.key.skillName}, {"source_instance", ctx.sourceRef.key.instanceID}};
    target->setTag(key, receipts);
    room->addPlayerMark(target, "&" + name);
}

static void clearFireWeather(Room *room, const QString &name, ServerPlayer *source)
{
    if (!source) return;
    const QString key = "FireWeather_" + name;
    for (ServerPlayer *target : room->getAllPlayers(true)) {
        QVariantList receipts = target->getTag(key).toList();
        int removed = 0;
        for (int i = receipts.size() - 1; i >= 0; --i)
            if (receipts.at(i).toMap().value("actor").toString() == source->objectName()) { receipts.removeAt(i); ++removed; }
        target->setTag(key, receipts);
        if (removed > 0) room->removePlayerMark(target, "&" + name, removed);
    }
}

class KuangfengViewAsSkill : public ViewAsSkillV2
{
public:
    KuangfengViewAsSkill() : ViewAsSkillV2("kuangfeng", 1)
    {
        response_pattern = "@@kuangfeng";
        expand_pile = "stars";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@kuangfeng"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool willThrowSelectedCards() const override { return false; }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && request.selectedCardIds.isEmpty()
            && !request.selectedCardIds.contains(candidate->getEffectiveId()) && !candidate->hasFlag("using") && getExpandPileCardIds(request.initiator).contains(candidate->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
        return card && canSelectCard(selection, card);
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new ActiveSkillCard;
        card->setActiveSkill(this);
        card->setSkillName(objectName());
        card->addSubcard(Sanguosha->getCard(request.selectedCardIds.first()));
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "KuangfengCard"; }

    bool canSelectTarget(const ActiveSkillRequest &, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return candidate && candidate->isAlive() && selected.isEmpty();
    }

    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 1;
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.invoker || !cardSelectionFeasible(request)) return false;
        for (int id : request.selectedCardIds) if (!ctx.invoker->getPile("stars").contains(id)) return false;
        DummyCard stars(request.selectedCardIds);
        const CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, ctx.invoker->objectName(), objectName(), "");
        room->throwCard(&stars, reason, nullptr);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        applyFireWeather(target->getRoom(), objectName(), ctx, target, getEffectiveAmount(ctx));
        return ContinueEffects;
    }
};

class Kuangfeng : public TriggerSkillV2
{
public:
    Kuangfeng() : TriggerSkillV2("kuangfeng")
    { events << DamageForseen << EventPhaseStart << Death; view_as_skill = new KuangfengViewAsSkill; global = true; }
    Frequency getFrequency(const Player *player = nullptr) const override
    { return player ? Compulsory : NotFrequent; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseStart && player && player->getPhase() == Player::RoundStart) clearFireWeather(room, objectName(), player);
        if (event == Death) clearFireWeather(room, objectName(), data.value<DeathStruct>().who);
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Finish && !player->getPile("stars").isEmpty()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != DamageForseen) return false;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || damage.damage <= 0 || damage.nature != DamageStruct::Fire) return true;
        for (const QVariant &value : player->getTag("FireWeather_" + objectName()).toList()) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner || receipt.value("dispatch").toInt() <= 0) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = owner;
            ctx.initiator = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
            ctx.invoker = player;
            ctx.instanceID = receipt.value("dispatch").toInt();
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.amount = receipt.value("amount").toInt();
            ctx.extra_data = receipt;
            ctx.targets << player;
            ctx.current_event = event;
            ctx.original_data = &data;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx)
            : ctx.invoker && ctx.invoker->getTag("FireWeather_" + objectName()).toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == DamageForseen) return true;
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int previous = owner->getMark(selector);
        auto restore = qScopeGuard([&] { room->setPlayerMark(owner, selector, previous); });
        room->setPlayerMark(owner, selector, ctx.activationRef.key.instanceID);
        room->askForUseCard(owner, "@@kuangfeng", "@kuangfeng-card", -1, Card::MethodNone);
        return false; // The selected active request owns payment and the applied receipt.
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (target != damage.to || damage.damage <= 0 || damage.nature != DamageStruct::Fire) return false;
        LogMessage log;
        log.type = "#GalePower";
        log.from = target;
        log.arg = QString::number(damage.damage);
        log.arg2 = QString::number(damage.damage + getEffectiveAmount(ctx));
        room->sendLog(log);
        target->damageRevises(*ctx.original_data, getEffectiveAmount(ctx));
        return false;
    }
};

DawuCard::DawuCard()
{
    setSkillName("dawu");
    handling_method = Card::MethodNone;
    will_throw = false;
}

bool DawuCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.length() < subcards.length();
}

bool DawuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() == subcards.length();
}

void DawuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, source->objectName(), "dawu", "");
    room->throwCard(this, reason, nullptr);

    source->setTag("dawuUse", true);

    foreach(ServerPlayer *target, targets)
        target->gainMark("&dawu");
}

class DawuViewAsSkill : public ViewAsSkillV2
{
public:
    DawuViewAsSkill() : ViewAsSkillV2("dawu", 1)
    {
        response_pattern = "@@dawu";
        expand_pile = "stars";
    }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.pattern == "@@dawu"
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    }

    bool willThrowSelectedCards() const override { return false; }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && !request.selectedCardIds.contains(candidate->getEffectiveId()) && !candidate->hasFlag("using")
            && getExpandPileCardIds(request.initiator).contains(candidate->getEffectiveId());
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty()) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            const Card *card = Sanguosha->getCard(id);
            if (!card || !canSelectCard(selection, card)) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }

    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        auto *card = new ActiveSkillCard;
        card->setActiveSkill(this);
        card->setSkillName(objectName());
        for (int id : request.selectedCardIds)
            card->addSubcard(Sanguosha->getCard(id));
        return card;
    }

    QString historyKey(const ActiveSkillRequest &) const override { return "DawuCard"; }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return candidate && candidate->isAlive() && !selected.contains(candidate) && selected.size() < request.selectedCardIds.size();
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        return targets.size() == request.selectedCardIds.size();
    }

    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.invoker || !cardSelectionFeasible(request)) return false;
        for (int id : request.selectedCardIds) if (!ctx.invoker->getPile("stars").contains(id)) return false;
        DummyCard stars(request.selectedCardIds);
        const CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, ctx.invoker->objectName(), objectName(), "");
        room->throwCard(&stars, reason, nullptr);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        applyFireWeather(target->getRoom(), objectName(), ctx, target, getEffectiveAmount(ctx));
        return ContinueEffects;
    }
};

class Dawu : public TriggerSkillV2
{
public:
    Dawu() : TriggerSkillV2("dawu")
    { events << DamageForseen << EventPhaseStart << Death; view_as_skill = new DawuViewAsSkill; global = true; }
    Frequency getFrequency(const Player *player = nullptr) const override
    { return player ? Compulsory : NotFrequent; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseStart && player && player->getPhase() == Player::RoundStart) clearFireWeather(room, objectName(), player);
        if (event == Death) clearFireWeather(room, objectName(), data.value<DeathStruct>().who);
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Finish && !player->getPile("stars").isEmpty()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != DamageForseen) return false;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || damage.to != player || damage.damage <= 0 || damage.nature == DamageStruct::Thunder) return true;
        for (const QVariant &value : player->getTag("FireWeather_" + objectName()).toList()) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner || receipt.value("dispatch").toInt() <= 0) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = owner;
            ctx.initiator = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
            ctx.invoker = player;
            ctx.instanceID = receipt.value("dispatch").toInt();
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_instance").toInt()));
            ctx.amount = receipt.value("amount").toInt();
            ctx.extra_data = receipt;
            ctx.targets << player;
            ctx.current_event = event;
            ctx.original_data = &data;
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx)
            : ctx.invoker && ctx.invoker->getTag("FireWeather_" + objectName()).toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == DamageForseen) return true;
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int previous = owner->getMark(selector);
        auto restore = qScopeGuard([&] { room->setPlayerMark(owner, selector, previous); });
        room->setPlayerMark(owner, selector, ctx.activationRef.key.instanceID);
        room->askForUseCard(owner, "@@dawu", "@dawu-card", -1, Card::MethodNone);
        return false; // The selected active request owns payment and the applied receipt.
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (target != damage.to || damage.damage <= 0 || damage.nature == DamageStruct::Thunder) return false;
        LogMessage log;
        log.type = "#FogProtect";
        log.from = target;
        log.arg = QString::number(damage.damage);
        log.arg2 = damage.nature == DamageStruct::Fire ? "fire_nature" : "normal_nature";
        room->sendLog(log);
        return target->damageRevises(*ctx.original_data, -damage.damage);
    }
};

class TenyearJianchu : public TriggerSkillV2
{
public:
    TenyearJianchu() : TriggerSkillV2("tenyearjianchu")
    {
        events << TargetSpecified;
    }

    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || use.from != player || !use.card || !use.card->isKindOf("Slash")) return true;
        for (int id : player->getValidSkillInstanceIds(objectName())) {
            for (ServerPlayer *target : use.to) {
                if (!target->isAlive() || !player->canDiscard(target, "he")) continue;
                SkillContext ctx;
                ctx.skill_name = objectName() + "->" + target->objectName();
                ctx.owner = ctx.invoker = ctx.initiator = player;
                ctx.instanceID = id;
                ctx.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(objectName(), id));
                ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
                if (!ctx.sourceRef.isValid()) continue;
                bool ok = false;
                ctx.amount = room->getSkillInstanceAmount(ctx.activationRef, &ok);
                if (!ok) ctx.amount = getBaseAmount();
                ctx.targets << target;
                ctx.preferredTarget = target;
                ctx.preferredTargetSeat = target->getSeat();
                ctx.current_event = event;
                ctx.original_data = &data;
                contexts << ctx;
            }
        }
        return true;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        return ctx.preferredTarget && ctx.preferredTarget->isAlive() && owner->canDiscard(ctx.preferredTarget, "he")
            && owner->askForSkillInvoke(objectName() + "$-1", QVariant::fromValue(ctx.preferredTarget));
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx,
                      ServerPlayer *target) const override
    {
        if (!ctx.owner || !ctx.original_data || !target || !target->isAlive()
            || !ctx.owner->canDiscard(target, "he")) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.from || !use.to.contains(target)) return false;

        const int id = room->askForCardChosen(ctx.owner, target, "he", objectName(), false,
                                              Card::MethodDiscard);
        const Card *chosen = id >= 0 ? Sanguosha->getCard(id) : nullptr;
        if (!chosen || (!target->handCards().contains(id) && !target->getEquipsId().contains(id))
            || !ctx.owner->canDiscard(target, id)) return false;
        const bool equipment = chosen->isKindOf("EquipCard");
        const CardMoveReason reason(CardMoveReason::S_REASON_DISMANTLE,
            ctx.owner->objectName(), target->objectName(), objectName(), QString());
        const QVariantList moved = room->moveCardsSub(
            CardsMoveStruct(id, nullptr, Player::DiscardPile, reason), true).toList();
        bool committed = false;
        for (const QVariant &entry : moved) {
            const CardsMoveOneTimeStruct move = entry.value<CardsMoveOneTimeStruct>();
            if (move.from != target || move.reason.m_reason != CardMoveReason::S_REASON_DISMANTLE)
                continue;
            for (int i = 0; i < move.card_ids.size(); ++i) {
                if (move.card_ids.at(i) == id
                    && (move.from_places.value(i) == Player::PlaceHand
                        || move.from_places.value(i) == Player::PlaceEquip)) {
                    committed = true;
                    break;
                }
            }
        }
        if (!committed) return false;

        if (equipment) {
            use = ctx.original_data->value<CardUseStruct>();
            if (!use.card || !use.to.contains(target)) return false;
            LogMessage log;
            log.type = "#NoJink";
            log.from = target;
            room->sendLog(log);
            use.no_respond_list << target->objectName();
            *ctx.original_data = QVariant::fromValue(use);
        } else if (target->isAlive() && room->CardInTable(use.card)) {
            target->obtainCard(use.card, true);
        }
        return false;
    }
};





FirePackage::FirePackage()
    : Package("fire")
{
    General *dianwei = new General(this, "dianwei", "wei"); // WEI 012
    dianwei->addSkill(new Qiangxi);

    General *xunyu = new General(this, "xunyu", "wei", 3); // WEI 013
    xunyu->addSkill(new Quhu);
    xunyu->addSkill(new Jieming);

    General *pangtong = new General(this, "pangtong", "shu", 3); // SHU 010
    pangtong->addSkill(new Lianhuan);
    pangtong->addSkill(new Niepan);

    General *wolong = new General(this, "wolong", "shu", 3); // SHU 011
    wolong->addSkill(new Bazhen);
    wolong->addSkill(new BazhenTrigger);
    wolong->addSkill(new Huoji);
    wolong->addSkill(new Kanpo);
    related_skills.insert("bazhen", "#bazhen");

    General *taishici = new General(this, "taishici", "wu"); // WU 012
    taishici->addSkill(new Tianyi);
    taishici->addSkill(new TianyiTargetMod);
    related_skills.insert("tianyi", "#tianyi-target");

    General *yuanshao = new General(this, "yuanshao$", "qun"); // QUN 004
    yuanshao->addSkill(new Luanji);
    yuanshao->addSkill(new Xueyi);
    yuanshao->addSkill(new XueyiMCS);

    General *yanliangwenchou = new General(this, "yanliangwenchou", "qun"); // QUN 005
    yanliangwenchou->addSkill(new Shuangxiong);
    addSkills(new ShuangxiongGet);
    insertRelatedSkills("shuangxiong", "#shuangxiong");

    General *pangde = new General(this, "pangde", "qun"); // QUN 008
    pangde->addSkill("mashu");
    pangde->addSkill(new Mengjin);

    General *shenzhouyu = new General(this, "shenzhouyu", "god"); // LE 003
    shenzhouyu->addSkill(new Qinyin);
    shenzhouyu->addSkill(new Yeyan);
    addMetaObject<YeyanCard>();
    addMetaObject<YeyanV2Card>();
    addMetaObject<GreatYeyanCard>();
    addMetaObject<SmallYeyanCard>();

    General *shenzhugeliang = new General(this, "shenzhugeliang", "god", 3); // LE 004
    shenzhugeliang->addSkill(new Qixing);
    shenzhugeliang->addSkill(new Kuangfeng);
    shenzhugeliang->addSkill(new Dawu);
    addMetaObject<QixingCard>();
    addMetaObject<KuangfengCard>();
    addMetaObject<DawuCard>();


    addMetaObject<QuhuCard>();
    addMetaObject<QiangxiCard>();
    addMetaObject<TianyiCard>();
}
ADD_PACKAGE(Fire)

TenyearStFirePackage::TenyearStFirePackage()
    : Package("TenyearStFire")
{
    General *tenyear_pangde = new General(this, "tenyear_pangde", "qun", 4);
    tenyear_pangde->addSkill(new TenyearJianchu);
    tenyear_pangde->addSkill("mashu");
}
ADD_PACKAGE(TenyearStFire)
