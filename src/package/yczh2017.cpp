#include "yczh2017.h"
//#include "settings.h"
//#include "skill.h"
//#include "standard.h"
//#include "client.h"
#include "clientplayer.h"
#include "engine.h"
#include "maneuvering.h"
//#include "util.h"
//#include "wrapped-card.h"
#include "room.h"
#include "skill-instance-utils.h"
#include "roomthread.h"
#include "yjcm2013.h"
#include <QScopeGuard>

class Jianzheng : public TriggerSkillV2
{
public:
    Jianzheng() : TriggerSkillV2("jianzheng") { events << TargetSpecifying; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        TriggerList result;
        if (!use.card || !use.card->isKindOf("Slash") || !use.from || player != use.from || use.to.isEmpty()) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (!use.to.contains(owner) && use.from->inMyAttackRange(owner) && !owner->isKongcheng()) result[owner] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.from || use.to.isEmpty() || use.to.contains(ctx.owner) || !use.from->inMyAttackRange(ctx.owner)) return false;
        const Card *card = room->askForCard(ctx.owner, ".|.|.|hand", "jianzheng-put", *ctx.original_data, Card::MethodNone);
        if (!card || card->getEffectiveId() < 0 || (card->isVirtualCard() && card->subcardsLength() != 1)) return false;
        ctx.extra_data = card->getEffectiveId();
        ctx.targets = {ctx.owner};
        return true;
    }

    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
        if (!card || card->hasFlag("using") || room->getCardOwner(id) != ctx.owner
            || room->getCardPlace(id) != Player::PlaceHand) return false;
        // The selected hand card is put on the draw pile once, before target rewriting.
        room->moveCardTo(card, ctx.owner, nullptr, Player::DrawPile,
            CardMoveReason(CardMoveReason::S_REASON_PUT, ctx.owner->objectName(), objectName(), ""), false);
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || use.to.isEmpty()) return false;
        room->broadcastSkillInvoke(objectName());
        for (ServerPlayer *old : use.to) old->removeQinggangTag(use.card);
        use.to.clear();
        if (!use.card->isBlack()) use.to << target;
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};
class Zhuandui : public TriggerSkillV2
{
public:
    Zhuandui() : TriggerSkillV2("zhuandui") { events << TargetSpecified << TargetConfirmed; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !use.card || !use.card->isKindOf("Slash")) return {};
        if (event == TargetSpecified) {
            if (use.from != player) return {};
            for (ServerPlayer *target : use.to) if (player->canPindian(target)) return {{player, {objectName()}}};
        } else if (use.to.contains(player) && use.from && player->canPindian(use.from)) return {{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (event == TargetConfirmed) {
            if (!use.from || !ctx.owner->canPindian(use.from) || !ctx.owner->askForSkillInvoke(this, use.from)) return false;
            ctx.targets = {ctx.owner};
            return true;
        }
        QStringList pending;
        ServerPlayer *first = nullptr;
        for (ServerPlayer *target : use.to) {
            if (!first) {
                if (ctx.owner->canPindian(target) && ctx.owner->askForSkillInvoke(this, target)) first = target;
            } else pending << target->objectName();
        }
        if (!first) return false;
        ctx.targets = {first}; ctx.extra_data = pending; ctx.manual_effect = true;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != TargetSpecified) return false;
        const QStringList pending = ctx.extra_data.toStringList();
        const QList<ServerPlayer *> first = ctx.targets;
        for (ServerPlayer *target : first) skillEffect(event, room, player, ctx, target);
        // Keep each optional pindian in card target order, after the previous one has settled.
        for (const QString &name : pending) {
            if (!ctx.owner->isAlive() || !isSourceAvailable(room, ctx)) break;
            ServerPlayer *target = room->findPlayerByObjectName(name);
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!target || !use.to.contains(target) || !ctx.owner->canPindian(target)
                || !ctx.owner->askForSkillInvoke(this, target)) continue;
            SkillContext part = ctx; part.targets = {target};
            skillEffect(event, room, player, part, target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ServerPlayer *opponent = event == TargetSpecified ? target : use.from;
        ServerPlayer *duelist = event == TargetSpecified ? ctx.owner : target;
        if (!duelist || !opponent || !duelist->canPindian(opponent)) return false;
        room->broadcastSkillInvoke(objectName());
        if (!duelist->pindian(opponent, objectName())) return false;
        use = ctx.original_data->value<CardUseStruct>();
        if (event == TargetConfirmed) {
            use.nullified_list << target->objectName();
            *ctx.original_data = QVariant::fromValue(use);
        } else if (use.card) {
            const int index = use.to.indexOf(target);
            const QString key = "Jink_" + use.card->toString();
            QVariantList jinks = ctx.owner->getTag(key).toList();
            if (index >= 0 && index < jinks.size()) {
                jinks[index] = 0; ctx.owner->setTag(key, jinks);
                LogMessage log; log.type = "#NoJink"; log.from = target; room->sendLog(log);
            }
        }
        return false;
    }
};
class Tianbian : public TriggerSkillV2
{
public:
    Tianbian(const QString &name = "tianbian") : TriggerSkillV2(name)
    {
        if (name == "tianbian") events << AskforPindianCard;
        else { events << PindianVerifying; frequency = Compulsory; m_baseAmount = 13; }
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const PindianStruct *pindian = data.value<PindianStruct *>();
        TriggerList result;
        if (!pindian || player != pindian->from) return result;
        for (ServerPlayer *owner : {pindian->from, pindian->to}) {
            if (!owner || !owner->isAlive() || !owner->hasSkill("tianbian")) continue;
            const Card *card = owner == pindian->from ? pindian->from_card : pindian->to_card;
            if (event == AskforPindianCard ? !card : card && card->getSuit() == Card::Heart)
                result[owner] << objectName();
        }
        return result;
    }

    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == AskforPindianCard && !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        PindianStruct *pindian = ctx.original_data->value<PindianStruct *>();
        if (!pindian || (target != pindian->from && target != pindian->to)) return false;
        const bool from = target == pindian->from;
        const Card *card = from ? pindian->from_card : pindian->to_card;
        if (event == AskforPindianCard) {
            if (card) return false;
            const int id = room->drawCard();
            if (id < 0) return false;
            // The ordinary pindian pipeline moves this physical top-deck card to the table.
            if (from) pindian->from_card = Sanguosha->getCard(id);
            else pindian->to_card = Sanguosha->getCard(id);
        } else {
            if (!card || card->getSuit() != Card::Heart) return false;
            LogMessage log; log.type = "#TianbianK"; log.from = target; log.arg = "tianbian";
            room->sendLog(log);
            if (from) pindian->from_number = getEffectiveAmount(ctx);
            else pindian->to_number = getEffectiveAmount(ctx);
        }
        room->broadcastSkillInvoke("tianbian");
        room->notifySkillInvoked(ctx.owner, "tianbian");
        *ctx.original_data = QVariant::fromValue(pindian);
        return false;
    }
};
FumianCard::FumianCard()
{
	mute = true;
}

bool FumianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length() < Self->getMark("fumian_extra_target-Clear") && to_select->hasFlag("fumian_canchoose");
}

void FumianCard::onUse(Room *room, CardUseStruct &card_use) const
{
	foreach (ServerPlayer *p, card_use.to)
		room->setPlayerFlag(p, "fumian_extratarget");
}

class Fumian : public TriggerSkillV2
{
public:
    Fumian() : TriggerSkillV2("fumian") { events << EventPhaseStart << DrawNCards << PreCardUsed << PreCardResponded << EventPhaseChanging << TurnBroken << EventSkillInvoking << EventSkillEffectFinished; global = true; }
    static QList<ServerPlayer *> extraTargets(Room *room, const CardUseStruct &use)
    {
        QList<ServerPlayer *> result; if (!use.from || !use.card) return result;
        for (ServerPlayer *candidate : room->getAlivePlayers()) {
            if (use.to.contains(candidate) || use.from->isProhibited(candidate, use.card)) continue;
            if (use.card->isKindOf("Collateral") ? use.card->targetFilter(QList<const Player *>(), candidate, use.from) : use.from->canUse(use.card, candidate)) result << candidate;
        }
        return result;
    }
    static QString previousChoice(Room *room, const SkillContext &ctx)
    {
        const qint64 current = room->historyScopes().value("turn_id").toLongLong(); if (current <= 0) return {};
        QVariantMap query{{"kind", "turn"}, {"player", ctx.owner->objectName()}, {"limit", 64}}; qint64 previous = 0;
        while (true) {
            const QVariantMap page = room->queryHistoryEvents(query); if (!page.value("complete").toBool()) return {};
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &entry : page.value("items").toList()) { const qint64 id = entry.toMap().value("id").toLongLong(); if (id < current && id > previous) previous = id; }
            if (!page.value("has_more").toBool()) break; query.insert("after", page.value("next_after"));
        }
        if (previous <= 0) return {};
        query = QVariantMap{{"kind", "skill"}, {"turn_id", previous}, {"limit", 64}}; QString result; qint64 latest = 0;
        while (true) {
            const QVariantMap page = room->queryHistoryEvents(query); if (!page.value("complete").toBool()) return {};
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap event = entry.toMap(), data = event.value("data").toMap(); const qint64 id = event.value("id").toLongLong();
                if (id > latest && data.value("activation_owner").toString() == ctx.activationRef.ownerObjectName && data.value("activation_skill").toString() == "fumian"
                    && data.value("activation_instance_id").toInt() == ctx.activationRef.key.instanceID && data.contains("fumian_choice")) { latest = id; result = data.value("fumian_choice").toString(); }
            }
            if (!page.value("has_more").toBool()) return result; query.insert("after", page.value("next_after"));
        }
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext ctx = data.value<SkillContext>();
            if (ctx.activationRef.key.skillName == objectName() && (ctx.choice == "draw" || ctx.choice == "target")) {
                const QVariantMap skill = room->historyParent(room->currentHistoryEventId(), "skill", true);
                const QVariantMap history = skill.value("data").toMap();
                if (skill.value("id").toLongLong() > 0 && history.value("activation_owner").toString() == ctx.activationRef.ownerObjectName && history.value("activation_instance_id").toInt() == ctx.activationRef.key.instanceID)
                    room->resolutionHistory().updateEvent(skill.value("id").toLongLong(), {{"fumian_choice", ctx.choice}});
            }
        } else if (event == EventSkillEffectFinished) {
            const SkillContext ctx = data.value<SkillContext>();
            if (ctx.skill_name == objectName() && !ctx.activationRef.isValid() && ctx.extra_data.toMap().value("choice").toString() == "target") {
                QVariantList receipts = room->getTag("FumianEffects").toList(); receipts.removeOne(ctx.extra_data); room->setTag("FumianEffects", receipts);
            }
        } else if (event == TurnBroken || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)) {
            // TurnBroken stays inside the interrupted turn. Drop that turn's receipts before another card resolves in it.
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong(); if (turn <= 0) return true;
            QVariantList kept; for (const QVariant &entry : room->getTag("FumianEffects").toList()) if (entry.toMap().value("turn").toLongLong() != turn) kept << entry;
            room->setTag("FumianEffects", kept);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event == EventPhaseStart) return false;
        if (!player || player->isDead() || (event != DrawNCards && event != PreCardUsed && event != PreCardResponded)) return true;
        if (event == DrawNCards && data.value<DrawStruct>().reason != "draw_phase") return true;
        const Card *card = event == PreCardUsed ? data.value<CardUseStruct>().card : event == PreCardResponded ? data.value<CardResponseStruct>().m_card : nullptr;
        if (event != DrawNCards && (!card || !card->isRed() || card->isKindOf("SkillCard") || player->getPhase() == Player::NotActive || (event == PreCardResponded && !data.value<CardResponseStruct>().m_isUse))) return true;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong(); if (turn <= 0) return true;
        for (const QVariant &entry : room->getTag("FumianEffects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("recipient").toString() != player->objectName() || receipt.value("turn").toLongLong() != turn
                || receipt.value("choice").toString() != (event == DrawNCards ? "draw" : "target")) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt(); ctx.invoker = player; ctx.initiator = player;
            ctx.owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true); ctx.targets = {player}; ctx.extra_data = receipt; ctx.amount = receipt.value("amount").toInt();
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt())); contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room, ctx) : room->getTag("FumianEffects").toList().contains(ctx.extra_data); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    { return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Start ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventPhaseStart) return true;
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        const QString last = previousChoice(room, ctx); ctx.choice = room->askForChoice(ctx.owner, objectName(), "draw+target");
        ctx.extra_data = !last.isEmpty() && ctx.choice != last ? 2 : 1; ctx.targets = {ctx.owner}; return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == PreCardUsed || event == PreCardResponded) {
            QVariantList receipts = room->getTag("FumianEffects").toList(); receipts.removeOne(ctx.extra_data); room->setTag("FumianEffects", receipts);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == EventPhaseStart) {
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong(); if (turn <= 0) return false;
            const int serial = room->getTag("FumianSerial").toInt() + 1; room->setTag("FumianSerial", serial);
            QVariantList receipts = room->getTag("FumianEffects").toList();
            receipts << QVariantMap{{"serial", serial}, {"turn", turn}, {"recipient", target->objectName()}, {"choice", ctx.choice}, {"amount", getEffectiveAmount(ctx) * ctx.extra_data.toInt()}, {"owner", ctx.owner->objectName()},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
            room->setTag("FumianEffects", receipts); room->setPlayerProperty(target, "fumian_choice", ctx.choice); return false;
        }
        if (event == DrawNCards) { DrawStruct draw = ctx.original_data->value<DrawStruct>(); if (draw.who == target) { draw.num += getEffectiveAmount(ctx); *ctx.original_data = QVariant::fromValue(draw); } return false; }
        if (event != PreCardUsed) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || (!use.card->isKindOf("BasicCard") && !use.card->isNDTrick()) || use.card->isKindOf("Nullification")) return false;
        const bool hadFlag = use.card->hasFlag("fumian_distance"); room->setCardFlag(use.card, "fumian_distance");
        const auto clear = qScopeGuard([&] { if (!hadFlag) room->setCardFlag(use.card, "-fumian_distance"); });
        if (ctx.choice == "add") {
            if (use.to.contains(target) || !extraTargets(room, use).contains(target)) return false;
            if (use.card->isKindOf("Collateral")) {
                QList<ServerPlayer *> victims;
                for (ServerPlayer *other : room->getOtherPlayers(target)) if (use.card->targetFilter(QList<const Player *>{target}, other, use.from)) victims << other;
                if (victims.isEmpty()) return false;
                ServerPlayer *victim = room->askForPlayerChosen(use.from, victims, objectName(), "@fumian:" + use.card->objectName());
                if (!victims.contains(victim) || !use.card->targetFilter(QList<const Player *>{target}, victim, use.from)) return false;
                // Collateral pairs the victim from attachTarget. A private tag name is never read.
                target->setTag("attachTarget", QVariant::fromValue(victim));
            }
            use.to << target; room->sortByActionOrder(use.to); *ctx.original_data = QVariant::fromValue(use); return false;
        }
        const QList<ServerPlayer *> candidates = extraTargets(room, use); if (candidates.isEmpty()) return false;
        const QList<ServerPlayer *> chosen = room->askForPlayersChosen(target, candidates, objectName(), 0, getEffectiveAmount(ctx), "@fumian:" + use.card->objectName());
        for (ServerPlayer *extra : chosen) { SkillContext part = ctx; part.choice = "add"; part.targets = {extra}; skillEffect(event, room, player, part, extra); }
        return false;
    }
};
class FumianTargetMod : public TargetModSkillV2
{
public:
    FumianTargetMod() : TargetModSkillV2("#fumian-target", ".") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    { return ctx.modType == DistanceLimit && ctx.card && ctx.card->isRed() && ctx.card->hasFlag("fumian_distance") ? CorrectSkillResult::useAmount(1000 * ctx.currentAmount) : CorrectSkillResult::noEffect(); }
};
class Daiyan : public TriggerSkillV2
{
public:
    Daiyan() : TriggerSkillV2("daiyan") { events << EventPhaseStart; }
    static bool previousTarget(Room *room, const SkillContext &ctx, ServerPlayer *target)
    {
        const qint64 current = room->historyScopes().value("turn_id").toLongLong(); if (current <= 0) return false;
        QVariantMap query{{"kind", "turn"}, {"player", ctx.owner->objectName()}, {"limit", 64}}; qint64 previous = 0;
        while (true) {
            const QVariantMap page = room->queryHistoryEvents(query); if (!page.value("complete").toBool()) return false;
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &entry : page.value("items").toList()) { const qint64 id = entry.toMap().value("id").toLongLong(); if (id > previous && id < current) previous = id; }
            if (!page.value("has_more").toBool()) break; query.insert("after", page.value("next_after"));
        }
        if (previous <= 0) return false;
        query = QVariantMap{{"kind", "skill_invoked"}, {"turn_id", previous}, {"limit", 64}};
        bool matched = false;
        while (true) {
            const QVariantMap page = room->queryHistoryFacts(query); if (!page.value("complete").toBool()) return false;
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap used = entry.toMap().value("data").toMap();
                if (used.value("activation_owner").toString() != ctx.activationRef.ownerObjectName || used.value("activation_skill").toString() != ctx.activationRef.key.skillName || used.value("activation_instance_id").toInt() != ctx.activationRef.key.instanceID) continue;
                if (!used.contains("targets")) return false;
                for (const QVariant &name : used.value("targets").toList()) if (name.toString() == target->objectName()) matched = true;
            }
            if (!page.value("has_more").toBool()) return matched; query.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "@daiyan-invoke", true, true); if (!target) return false; ctx.targets = {target}; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const bool repeated = previousTarget(room, ctx, target);
        // Prior targets come from the exact previous turn's accepted invocation facts, including a turn with no use.
        for (ServerPlayer *other : room->getAllPlayers(true)) room->setPlayerMark(other, "&daiyan+#" + ctx.owner->objectName(), 0);
        room->setPlayerMark(target, "&daiyan+#" + ctx.owner->objectName(), 1);
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            QList<int> candidates; for (int id : room->getDrawPile()) { const Card *card = Sanguosha->getCard(id); if (card->getSuit() == Card::Heart && card->isKindOf("BasicCard")) candidates << id; }
            if (candidates.isEmpty()) break;
            room->obtainCard(target, candidates.at(qsanRandomBounded(candidates.size())), true);
        }
        if (repeated && target->isAlive()) room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        return false;
    }
};
ZhongjianCard::ZhongjianCard()
{
    setSkillName("zhongjian");
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool ZhongjianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && to_select->getHandcardNum() > to_select->getHp();
}

void ZhongjianCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	room->showCard(effect.from, getEffectiveId());
	int n = effect.to->getHandcardNum() - effect.to->getHp();
	if (n <= 0) return;
	QList<int> list;
	QStringList slist;
	for (int i = 0; i < n; i++) {
		int id = room->askForCardChosen(effect.from,effect.to,"h","zhongjian",false,Card::MethodNone,list);
		list << id;
		slist << Sanguosha->getCard(id)->toString();
	}/*
	LogMessage log;
	log.type = "$ZhongjianShow";
	log.from = effect.from;
	log.to << effect.to;
	log.arg = QString::number(n);
	log.card_str = slist.join("+");
	room->sendLog(log);
	room->fillAG(list);
	room->getThread()->delay(2000);
	room->clearAG();*/
	room->showCard(effect.to, list);
	room->getThread()->delay();
	bool samecolour = false, samenumber = false;
	foreach (int id, list) {
		if (sameColorWith(Sanguosha->getCard(id)))
			samecolour = true;
		if (getNumber() == Sanguosha->getCard(id)->getNumber())
			samenumber = true;
	}
	LogMessage newlog;
	if (!samecolour && !samenumber) {
		newlog.type = "#ZhongjianResult_NoSame";
		room->sendLog(newlog);
		effect.from->addMark("zhongjian_debuff");
		room->addMaxCards(effect.from,1,false);
		return;
	}
	newlog.type = "#ZhongjianResult";
	newlog.arg = samecolour == true ? "zhongjiansamecolour" : "zhongjiandifferentcolour";
	newlog.arg2 = samenumber == true ? "zhongjiansamenumber" : "zhongjiandifferentnumber";
	room->sendLog(newlog);
	if (samecolour) {
		QStringList choices;
		choices << "draw";
		if (effect.from->canDiscard(effect.to, "he"))
			choices << "discard";
		if (room->askForChoice(effect.from, "zhongjian", choices.join("+")) == "draw")
			effect.from->drawCards(1, "zhongjian");
		else {
			int card_id = room->askForCardChosen(effect.from, effect.to, "he", "zhongjian", false, Card::MethodDiscard);
			room->throwCard(card_id, effect.to, effect.from);
		}
	}
	if (samenumber)
		room->setPlayerMark(effect.from, "zhongjian-PlayClear",1);
}

class ZhongjianVS : public ViewAsSkillV2
{
public:
    ZhongjianVS(const QString &name = "zhongjian") : ViewAsSkillV2(name, 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.activationRef.isValid()) return 1;
        // The cached flag is the client mirror. The server quota follows the live turn, so an inserted turn does not keep or erase the outer one.
        const ServerPlayer *owner = dynamic_cast<const ServerPlayer *>(ctx.owner);
        Room *room = owner ? owner->getRoom() : nullptr;
        if (!room) return ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "twice").toBool() ? 2 : 1;
        const qint64 turn = room->historyScopes().value(QStringLiteral("turn_id")).toLongLong();
        if (turn <= 0) return 1;
        return ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "twice_turns").toMap().value(QString::number(turn)).toBool() ? 2 : 1;
    }
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isKongcheng(); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.initiator && request.selectedCardIds.isEmpty() && card && !card->hasFlag("using") && request.initiator->handCards().contains(card->getEffectiveId()); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { ActiveSkillRequest empty = request; empty.selectedCardIds.clear(); return request.selectedCardIds.size() == 1 && canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first())); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return request.initiator && selected.isEmpty() && target && target != request.initiator && target->isAlive() && (objectName() == "olzhongjian" ? !target->isKongcheng() : target->getHandcardNum() > target->getHp()); }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return targets.size() == 1 && canSelectTarget(request, {}, targets.first()); }
    QString historyKey(const ActiveSkillRequest &) const override { return objectName() == "zhongjian" ? "ZhongjianCard" : "OLZhongjianCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        SkillContext reveal = ctx; reveal.choice = "reveal_material"; reveal.extra_data = QVariantMap(); reveal.targets = {ctx.initiator}; skillEffect(reveal, ctx.initiator);
        if (reveal.is_canceled || !reveal.extra_data.toMap().value("revealed").toBool()) return FinishSkill;
        ctx.extra_data = reveal.extra_data; return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom(); const int amount = getEffectiveAmount(ctx);
        if (ctx.choice == "reveal_material") {
            if (!ctx.use_card || ctx.use_card->subcardsLength() != 1) return ContinueEffects;
            const int id = ctx.use_card->getSubcards().first();
            if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
            const Card *card = Sanguosha->getCard(id);
            ctx.extra_data = QVariantMap{{"red", card->isRed()}, {"black", card->isBlack()}, {"number", card->getNumber()}, {"revealed", true}};
            room->showCard(target, id); return ContinueEffects;
        }
        if (ctx.choice == "draw") { target->drawCards(amount, objectName()); return ContinueEffects; }
        if (ctx.choice == "discard") {
            for (int i = 0; i < amount && ctx.invoker->isAlive() && target->isAlive() && ctx.invoker->canDiscard(target, "he"); ++i) {
                const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard);
                if (id < 0 || room->getCardOwner(id) != target || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip) || Sanguosha->getCard(id)->hasFlag("using") || !ctx.invoker->canDiscard(target, id)) break;
                room->throwCard(id, target, ctx.invoker);
            }
            return ContinueEffects;
        }
        if (ctx.choice == "debuff") {
            if (target->getMaxCards() <= 0 || amount <= 0) return ContinueEffects;
            const QString helper = objectName() == "zhongjian" ? "#zhongjian-max" : "#olzhongjian-max";
            for (const SkillInstanceKey &key : ctx.owner->getChildSkillInstanceKeys(ctx.activationRef.key)) if (key.skillName == helper) {
                QVariantMap penalties = ctx.owner->getSkillInstanceCorrectStateValue(key.skillName, key.instanceID, "penalties").toMap();
                penalties.insert(target->objectName(), penalties.value(target->objectName()).toInt() + qMin(amount, target->getMaxCards()));
                room->setSkillInstanceCorrectState(ctx.owner, SkillInstanceRef(ctx.owner->objectName(), key), "penalties", penalties); break;
            }
            return ContinueEffects;
        }
        const int count = qMin(target->getHandcardNum(), qMax(0, objectName() == "olzhongjian" ? target->getHp() : target->getHandcardNum() - target->getHp()) * amount);
        if (count <= 0) return ContinueEffects;
        QList<int> ids;
        for (int i = 0; i < count && target->isAlive(); ++i) {
            const int id = room->askForCardChosen(ctx.invoker, target, "h", objectName(), false, Card::MethodNone, ids);
            if (id < 0 || ids.contains(id) || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand || Sanguosha->getCard(id)->hasFlag("using")) break;
            ids << id;
        }
        if (ids.isEmpty()) return ContinueEffects;
        const QVariantMap material = ctx.extra_data.toMap(); bool sameColor = false, sameNumber = false;
        for (int id : ids) { const Card *card = Sanguosha->getCard(id); sameColor |= (card->isRed() && material.value("red").toBool()) || (card->isBlack() && material.value("black").toBool()); sameNumber |= card->getNumber() == material.value("number").toInt(); }
        room->showCard(target, ids);
        if (sameNumber && ctx.owner->findSkillInstance(objectName(), ctx.activationRef.key.instanceID)) {
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
            if (turn > 0) {
                QVariantMap state = ctx.owner->getSkillInstanceState(objectName(), ctx.activationRef.key.instanceID);
                QVariantMap turns = state.value("twice_turns").toMap(); turns.insert(QString::number(turn), true);
                state.insert("twice_turns", turns); state.insert("twice", true);
                ctx.owner->setSkillInstanceState(objectName(), ctx.activationRef.key.instanceID, state);
            }
        }
        if (sameColor) {
            ServerPlayer *discard = nullptr;
            if (objectName() == "olzhongjian") {
                QList<ServerPlayer *> candidates; for (ServerPlayer *other : room->getOtherPlayers(ctx.invoker)) if (ctx.invoker->canDiscard(other, "he")) candidates << other;
                if (!candidates.isEmpty()) discard = room->askForPlayerChosen(ctx.invoker, candidates, objectName(), "@olzhongjian-discard", true);
            } else if (ctx.invoker->canDiscard(target, "he") && room->askForChoice(ctx.invoker, objectName(), "draw+discard") == "discard") discard = target;
            SkillContext reward = ctx; reward.choice = discard ? "discard" : "draw"; ServerPlayer *recipient = discard ? discard : ctx.invoker; reward.targets = {recipient}; skillEffect(reward, recipient);
        } else if (!sameNumber) { SkillContext penalty = ctx; penalty.choice = "debuff"; penalty.targets = {ctx.invoker}; skillEffect(penalty, ctx.invoker); }
        return ContinueEffects;
    }
};
class Zhongjian : public TriggerSkillV2
{
public:
    Zhongjian(const QString &name = "zhongjian") : TriggerSkillV2(name) { events << EventPhaseChanging << TurnStart << TurnBroken; global = true; view_as_skill = new ZhongjianVS(name); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const bool turnStart = event == TurnStart;
        const bool broken = event == TurnBroken;
        const bool ending = event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive;
        if (!turnStart && !broken && !ending) return true;
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong(); if (turn <= 0) return true;
        // NotActive publishes the suspended turn for the client flag. TurnBroken is still that interrupted turn.
        qint64 shown = turn;
        if (ending || (broken && room->getCurrent() && room->getCurrent()->getPhase() == Player::NotActive))
            shown = room->historyParent(turn, "turn", false).value("id").toLongLong();
        for (ServerPlayer *holder : room->getAllPlayers(true)) for (int id : holder->getSkillInstanceIds(objectName())) {
            QVariantMap state = holder->getSkillInstanceState(objectName(), id), turns = state.value("twice_turns").toMap();
            if (!turnStart) turns.remove(QString::number(turn));
            state.insert("twice_turns", turns); state.insert("twice", turns.value(QString::number(shown)).toBool());
            holder->setSkillInstanceState(objectName(), id, state);
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};
class ZhongjianMax : public MaxCardsSkillV2
{
public:
    ZhongjianMax(const QString &name = "#zhongjian-max") : MaxCardsSkillV2(name) { setHolderSelector(CorrectSkill_AllHolders); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const int penalty = ctx.primary ? ctx.getStateValue("penalties").toMap().value(ctx.primary->objectName()).toInt() : 0;
        return penalty > 0 ? CorrectSkillResult::signedAmount(-penalty * ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};
static void writeCaishiRestrictions(Room *room, const QString &skill, qint64 turn)
{
    if (!room) return;
    static bool projecting = false;
    if (projecting) return;
    projecting = true;
    const auto stop = qScopeGuard([&] { projecting = false; });
    const QVariantList receipts = room->getTag(skill + "Restrictions").toList();
    for (ServerPlayer *recipient : room->getAllPlayers(true)) {
        QStringList restrictions;
        for (const QVariant &entry : receipts) {
            const QVariantMap receipt = entry.toMap(); const QString choice = receipt.value("choice").toString();
            if (receipt.value("turn").toLongLong() == turn && receipt.value("recipient").toString() == recipient->objectName() && !choice.isEmpty() && !restrictions.contains(choice)) restrictions << choice;
        }
        const QByteArray property = (skill + "_restrictions").toLatin1();
        if (recipient->property(property.constData()).toStringList() != restrictions) room->setPlayerProperty(recipient, property.constData(), restrictions);
    }
}

class Caishi : public TriggerSkillV2
{
public:
    explicit Caishi(const QString &name = "caishi") : TriggerSkillV2(name)
    {
        events << EventPhaseStart << EventPhaseChanging << TurnStart << TurnBroken;
        if (name == "caishi") events << Death;
        else frequency = Frequent;
        global = true;
    }
    QString correctionName() const { return objectName() == "caishi" ? "#caishimax" : "#olcaishimax"; }
    void projectRestrictions(Room *room, qint64 turn) const
    { writeCaishiRestrictions(room, objectName(), turn); }
    void retainRestriction(Room *room, const SkillContext &ctx, ServerPlayer *target, const QString &choice) const
    {
        const qint64 turn = room->historyScopes().value("turn_id").toLongLong(); if (turn <= 0) return;
        QVariantList receipts = room->getTag(objectName() + "Restrictions").toList();
        receipts << QVariantMap{{"turn", turn}, {"recipient", target->objectName()}, {"choice", choice},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        room->setTag(objectName() + "Restrictions", receipts); projectRestrictions(room, turn);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != Death) {
            qint64 turn = room->historyScopes().value("turn_id").toLongLong();
            const bool broken = event == TurnBroken;
            const bool ending = broken
                || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
                || (event == EventPhaseStart && player && player->getPhase() == Player::NotActive);
            if (ending) {
                QVariantList kept;
                for (const QVariant &entry : room->getTag(objectName() + "Restrictions").toList()) if (entry.toMap().value("turn").toLongLong() != turn) kept << entry;
                room->setTag(objectName() + "Restrictions", kept);
                // A broken turn that never opens NotActive has no later phase event. Publish the suspended turn here.
                const bool resumeParent = !broken
                    || (room->getCurrent() && room->getCurrent()->getPhase() == Player::NotActive);
                if (resumeParent) turn = room->historyParent(turn, "turn", false).value("id").toLongLong();
            }
            projectRestrictions(room, turn); return true;
        }
        if (!player || data.value<DeathStruct>().who != player) return true;
        // Death retires this holder's accumulated normal-Caishi corrections, including suppressed instances.
        for (int id : player->getSkillInstanceIds(correctionName()))
            room->clearSkillInstanceCorrectState(player, SkillInstanceRef(player->objectName(), SkillInstanceKey(correctionName(), id)));
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    {
        return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::Draw ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        ctx.choice = room->askForChoice(ctx.owner, objectName(), ctx.owner->isWounded() ? "max+recover" : "max");
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        LogMessage log; log.type = "#FumianFirstChoice"; log.from = ctx.owner; log.arg = objectName() + ":" + ctx.choice; room->sendLog(log);
        if (ctx.choice == "max") {
            for (const SkillInstanceKey &key : ctx.owner->getChildSkillInstanceKeys(ctx.activationRef.key)) {
                if (key.skillName != correctionName()) continue;
                QVariantMap bonuses = ctx.owner->getSkillInstanceCorrectStateValue(key.skillName, key.instanceID, "bonuses").toMap();
                bonuses[target->objectName()] = bonuses.value(target->objectName()).toInt() + getEffectiveAmount(ctx);
                room->setSkillInstanceCorrectState(ctx.owner, SkillInstanceRef(ctx.owner->objectName(), key), "bonuses", bonuses);
            }
            if (objectName() == "caishi") retainRestriction(room, ctx, target, "others");
        } else {
            room->recover(target, RecoverStruct(ctx.owner, nullptr, getEffectiveAmount(ctx), objectName()));
            retainRestriction(room, ctx, target, "self");
        }
        return false;
    }
};

class CaishiMax : public MaxCardsSkillV2
{
public:
    explicit CaishiMax(const QString &name = "#caishimax") : MaxCardsSkillV2(name)
    { setHolderSelector(CorrectSkill_AllHolders); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary) return CorrectSkillResult::noEffect();
        const int bonus = ctx.getStateValue("bonuses").toMap().value(ctx.primary->objectName()).toInt();
        return bonus > 0 ? CorrectSkillResult::useAmount(bonus * ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};
class CaishiPro : public ProhibitSkill
{
public:
	CaishiPro() : ProhibitSkill("#caishipro")
	{
		frequency = NotFrequent;
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		if (!from || !to || !card || card->isKindOf("SkillCard")) return false;
		if (const ServerPlayer *server = dynamic_cast<const ServerPlayer *>(from))
			if (Room *room = server->getRoom())
				writeCaishiRestrictions(room, "caishi", room->historyScopes().value(QStringLiteral("turn_id")).toLongLong());
		const QStringList restrictions = from->property("caishi_restrictions").toStringList();
		// Two instances can accept both options in one turn. Each choice bans its own side.
		if (restrictions.contains("others") && from != to) return true;
		if (restrictions.contains("self") && from == to) return true;
		return false;
	}
};

class Qingxian : public TriggerSkillV2
{
public:
    Qingxian() : TriggerSkillV2("qingxian") { events << Damaged << HpRecover; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        for (ServerPlayer *other : room->getAlivePlayers()) if (other->getHp() <= 0) return {};
        if (event == Damaged) {
            ServerPlayer *from = data.value<DamageStruct>().from;
            if (!from || !from->isAlive()) return {};
        }
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = nullptr;
        if (event == Damaged) {
            target = ctx.original_data->value<DamageStruct>().from;
            if (!target || !target->isAlive() || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target))) return false;
        } else target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "@qingxian-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "reward") { target->drawCards(getEffectiveAmount(ctx), objectName()); return false; }
        room->broadcastSkillInvoke(objectName());
        const QString choice = room->askForChoice(ctx.owner, objectName(), target->isWounded() ? "losehp+recover" : "losehp");
        bool club = false;
        if (choice == "losehp") {
            room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.owner));
            if (!target->isAlive()) return false;
            QList<int> candidates;
            for (int id : room->getDrawPile()) {
                const Card *card = Sanguosha->getCard(id);
                if (card->isKindOf("EquipCard") && card->isAvailable(target) && !target->isProhibited(target, card)) candidates << id;
            }
            if (candidates.isEmpty()) return false;
            const int id = candidates.at(qsanRandomBounded(candidates.size()));
            const Card *equip = Sanguosha->getCard(id);
            if (room->getCardPlace(id) != Player::DrawPile || !equip->isAvailable(target) || target->isProhibited(target, equip)) return false;
            club = equip->getSuit() == Card::Club;
            room->useCardFromSkillEffect(CardUseStruct(equip, target, target), ctx, true);
        } else {
            room->recover(target, RecoverStruct(ctx.owner, nullptr, getEffectiveAmount(ctx), objectName()));
            if (!target->isAlive()) return false;
            bool hasEquip = false;
            for (const Card *card : target->getCards("he")) if (card->isKindOf("EquipCard")) hasEquip = true;
            if (!hasEquip) { room->showAllCards(target); return false; }
            const Card *discarded = room->askForDiscard(target, objectName(), 1, 1, false, true, "qingxian-discard", "EquipCard");
            if (discarded && discarded->subcardsLength() == 1) {
                club = Sanguosha->getCard(discarded->getSubcards().first())->getSuit() == Card::Club;
            } else {
                // Refresh after the prompt; stale equipment pointers cannot become fallback payment.
                QList<int> candidates;
                for (const Card *card : target->getCards("he"))
                    if (card->isKindOf("EquipCard") && !card->hasFlag("using") && target->canDiscard(target, card->getEffectiveId()))
                        candidates << card->getEffectiveId();
                if (candidates.isEmpty()) return false;
                const int id = candidates.at(qsanRandomBounded(candidates.size()));
                club = Sanguosha->getCard(id)->getSuit() == Card::Club;
                room->throwCard(id, target, nullptr);
            }
        }
        if (club && ctx.owner->isAlive()) {
            SkillContext reward = ctx;
            reward.choice = "reward"; reward.targets = {ctx.owner};
            skillEffect(event, room, player, reward, ctx.owner);
        }
        return false;
    }
};
class Juexiang : public TriggerSkillV2
{
public:
    Juexiang() : TriggerSkillV2("juexiang") { events << Death << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (player && ((event == Death && data.value<DeathStruct>().who == player)
            || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::RoundStart))) room->setPlayerMark(player, "juexiang_buff", 0);
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    { return event == Death && player && data.value<DeathStruct>().who == player && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "@juexiang-invoke", true, true); if (!target) return false; ctx.targets = {target}; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        bool acquired = false;
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            QStringList skills; for (const QString &name : {QString("jixian"), QString("liexian"), QString("rouxian"), QString("hexian")}) if (!target->hasSkill(name)) skills << name;
            if (skills.isEmpty()) break;
            acquired = room->acquireSkillFromEffect(target, skills.at(qsanRandomBounded(skills.size())), ctx) > 0 || acquired;
        }
        // The club protection is an accepted recipient resource and expires at that recipient's next turn.
        if (acquired && target->isAlive()) room->setPlayerMark(target, "juexiang_buff", 1);
        return false;
    }
};
class JuexiangPro : public ProhibitSkill
{
public:
	JuexiangPro() : ProhibitSkill("#juexiangpro")
	{
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		return to && from != to && to->getMark("juexiang_buff") > 0 && card->getSuit() == Card::Club;
	}
};

class Jixian : public TriggerSkillV2
{
public:
    Jixian() : TriggerSkillV2("jixian") { events << Damaged; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        for (ServerPlayer *other : room->getAlivePlayers())
            if (other->getHp() <= 0) return {};
        ServerPlayer *from = data.value<DamageStruct>().from;
        if (!from || !from->isAlive()) return {};
        return {{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().from;
        if (!target || !target->isAlive() || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target))) return false;
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        if (!target->isAlive()) return false;
        QList<int> candidates;
        for (int id : room->getDrawPile()) {
            const Card *card = Sanguosha->getCard(id);
            if (card->isKindOf("EquipCard") && card->isAvailable(target) && !target->isProhibited(target, card)) candidates << id;
        }
        if (candidates.isEmpty()) return false;
        const int id = candidates.at(qsanRandomBounded(candidates.size()));
        const Card *equip = Sanguosha->getCard(id);
        if (room->getCardPlace(id) == Player::DrawPile && equip->isAvailable(target) && !target->isProhibited(target, equip))
            room->useCardFromSkillEffect(CardUseStruct(equip, target, target), ctx, true);
        return false;
    }
};

class Liexian : public TriggerSkillV2
{
public:
    Liexian() : TriggerSkillV2("liexian") { events << HpRecover; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        for (ServerPlayer *other : room->getAlivePlayers())
            if (other->getHp() <= 0) return {};

        return {{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "@liexian-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        if (!target->isAlive()) return false;
        QList<int> candidates;
        for (int id : room->getDrawPile()) {
            const Card *card = Sanguosha->getCard(id);
            if (card->isKindOf("EquipCard") && card->isAvailable(target) && !target->isProhibited(target, card)) candidates << id;
        }
        if (candidates.isEmpty()) return false;
        const int id = candidates.at(qsanRandomBounded(candidates.size()));
        const Card *equip = Sanguosha->getCard(id);
        if (room->getCardPlace(id) == Player::DrawPile && equip->isAvailable(target) && !target->isProhibited(target, equip))
            room->useCardFromSkillEffect(CardUseStruct(equip, target, target), ctx, true);
        return false;
    }
};

class Rouxian : public TriggerSkillV2
{
public:
    Rouxian() : TriggerSkillV2("rouxian") { events << Damaged; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        for (ServerPlayer *other : room->getAlivePlayers())
            if (other->getHp() <= 0) return {};
        ServerPlayer *from = data.value<DamageStruct>().from;
        if (!from || !from->isAlive()) return {};
        return {{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().from;
        if (!target || !target->isAlive() || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target))) return false;
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->recover(target, RecoverStruct(ctx.invoker, nullptr, getEffectiveAmount(ctx), objectName()));
        if (!target->isAlive()) return false;
        QList<const Card *> equips;
        for (const Card *card : target->getCards("he"))
            if (card->isKindOf("EquipCard")) equips << card;
        if (equips.isEmpty()) { room->showAllCards(target); return false; }
        const Card *discarded = room->askForCard(target, "EquipCard!", "qingxian-discard");
        if (!discarded) {
            // A nested response can move an equipment; refresh before forced fallback.
            equips.clear();
            for (const Card *card : target->getCards("he"))
                if (card->isKindOf("EquipCard")) equips << card;
            if (!equips.isEmpty()) room->throwCard(equips.at(qsanRandomBounded(equips.size())), target, nullptr);
        }
        return false;
    }
};

class Hexian : public TriggerSkillV2
{
public:
    Hexian() : TriggerSkillV2("hexian") { events << HpRecover; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        for (ServerPlayer *other : room->getAlivePlayers())
            if (other->getHp() <= 0) return {};

        return {{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "@hexian-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->recover(target, RecoverStruct(ctx.invoker, nullptr, getEffectiveAmount(ctx), objectName()));
        if (!target->isAlive()) return false;
        QList<const Card *> equips;
        for (const Card *card : target->getCards("he"))
            if (card->isKindOf("EquipCard")) equips << card;
        if (equips.isEmpty()) { room->showAllCards(target); return false; }
        const Card *discarded = room->askForCard(target, "EquipCard!", "qingxian-discard");
        if (!discarded) {
            // A nested response can move an equipment; refresh before forced fallback.
            equips.clear();
            for (const Card *card : target->getCards("he"))
                if (card->isKindOf("EquipCard")) equips << card;
            if (!equips.isEmpty()) room->throwCard(equips.at(qsanRandomBounded(equips.size())), target, nullptr);
        }
        return false;
    }
};

WenguagiveCard::WenguagiveCard()
{
    setSkillName("wenguagive");
	will_throw = false;
	handling_method = Card::MethodNone;
	mute = true;
}

bool WenguagiveCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select->hasSkill("wengua") && to_select->getMark("wengua-PlayClear") <= 0 && Self != to_select;
}

void WenguagiveCard::onUse(Room *room, CardUseStruct &card_use) const
{
	QVariant data = QVariant::fromValue(card_use);
	room->getThread()->trigger(PreCardUsed, room, card_use.from, data);
	room->getThread()->trigger(CardUsed, room, card_use.from, data);
	card_use = data.value<CardUseStruct>();
	foreach (ServerPlayer *p, card_use.to){
		card_use.from->skillInvoked("wengua",-1,p);
		if (card_use.from != p) {
			CardMoveReason reason(CardMoveReason::S_REASON_GIVE, card_use.from->objectName(), p->objectName(), "wengua", "");
			room->obtainCard(p, this, reason, false);
		}
	
		if (!p->hasCard(subcards.first())) continue;
	
		QStringList list;
		list << card_use.from->objectName() << QString::number(subcards.first());
		QString choice = room->askForChoice(p, "wengua", "top+bottom+cancel", list);
		if (choice == "cancel") continue;
		QList<ServerPlayer *> sp;
		sp << card_use.from;
		if (!sp.contains(p))
			sp << p;
		room->sortByActionOrder(sp);
		if (choice == "top") {
			CardMoveReason reason(CardMoveReason::S_REASON_PUT, p->objectName(), "wengua", "");
			room->moveCardTo(this, nullptr, Player::DrawPile, reason);
			room->drawCards(sp, 1, "wengua", false);
		} else {
			room->moveCardsToEndOfDrawpile(card_use.from, subcards, "wengua", false);
			room->drawCards(sp, 1, "wengua");
		}
	}
	room->getThread()->trigger(CardFinished, room, card_use.from, data);
	card_use = data.value<CardUseStruct>();
}

WenguaCard::WenguaCard()
{
    setSkillName("wengua"); target_fixed = true; will_throw = false; handling_method = Card::MethodNone;
}
void WenguaCard::use(Room *, ServerPlayer *, QList<ServerPlayer *> &) const {}

class WenguaVS : public ViewAsSkillV2
{
public:
    WenguaVS(const QString &name = "wengua") : ViewAsSkillV2(name, 1) { setPhaseName("Play"); attached_lord_skill = name == "wenguagive"; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    TargetMode targetMode() const override { return objectName() == "wengua" ? NoTarget : SelectTargets; }
    QString historyKey(const ActiveSkillRequest &) const override { return objectName() == "wengua" ? "WenguaCard" : "WenguagiveCard"; }
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isNude(); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.initiator && request.selectedCardIds.isEmpty() && card && !card->hasFlag("using") && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId())); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { ActiveSkillRequest empty = request; empty.selectedCardIds.clear(); return request.selectedCardIds.size() == 1 && canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first())); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        if (!request.initiator || objectName() != "wenguagive" || !selected.isEmpty() || !target || target == request.initiator || !target->isAlive()) return false;
        const SkillInstance *instance = request.initiator->findSkillInstance(objectName(), request.activationRef.key.instanceID);
        return instance && instance->parentRef.isValid() && instance->parentRef.key.skillName == "wengua" && instance->parentRef.ownerObjectName == target->objectName();
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return objectName() == "wengua" ? targets.isEmpty() : targets.size() == 1 && canSelectTarget(request, {}, targets.first()); }
    EffectFlow effect(SkillContext &ctx) const override
    { if (objectName() != "wengua") return ContinueEffects; skillEffect(ctx, ctx.initiator); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        if (ctx.choice == "draw_top" || ctx.choice == "draw_bottom") {
            room->drawCards(target, getEffectiveAmount(ctx), "wengua", ctx.choice == "draw_top"); return ContinueEffects;
        }
        if (!ctx.use_card || ctx.use_card->subcardsLength() != 1 || !ctx.initiator) return ContinueEffects;
        const int id = ctx.use_card->getSubcards().first(); ServerPlayer *donor = ctx.initiator;
        if (room->getCardOwner(id) != donor || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip) || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        if (target != donor) room->giveCard(donor, target, QList<int>{id}, "wengua");
        if (room->getCardOwner(id) != target || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip) || Sanguosha->getCard(id)->hasFlag("using") || target->isDead()) return ContinueEffects;
        const QString choice = room->askForChoice(target, "wengua", target == donor ? "top+bottom" : "top+bottom+cancel", QStringList{donor->objectName(), QString::number(id)});
        if (choice != "top" && choice != "bottom") return ContinueEffects;
        if (room->getCardOwner(id) != target || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip) || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        const QVariantMap before = room->queryHistoryMoves({{"from", target->objectName()}, {"limit", 1}});
        const qint64 after = before.value("watermark").toLongLong();
        if (choice == "top") room->moveCardTo(Sanguosha->getCard(id), nullptr, Player::DrawPile, CardMoveReason(CardMoveReason::S_REASON_PUT, target->objectName(), "wengua", QString()));
        else room->moveCardsToEndOfDrawpile(target, QList<int>{id}, "wengua", false);
        bool moved = false; QVariantMap query{{"after", after}, {"from", target->objectName()}, {"limit", 64}};
        while (after > 0 && before.value("complete").toBool()) {
            const QVariantMap page = room->queryHistoryMoves(query); if (!page.value("complete").toBool() || !page.value("error").toString().isEmpty()) return ContinueEffects;
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &entry : page.value("items").toList()) { const QVariantMap move = entry.toMap().value("data").toMap(); if (move.value("card_id", -1).toInt() == id && move.value("to_place").toInt() == Player::DrawPile) moved = true; }
            if (!page.value("has_more").toBool()) break; query.insert("after", page.value("next_after"));
        }
        if (!moved) return ContinueEffects;
        QList<ServerPlayer *> recipients{donor}; if (target != donor) recipients << target; room->sortByActionOrder(recipients);
        for (ServerPlayer *recipient : recipients) { SkillContext draw = ctx; draw.choice = choice == "top" ? "draw_bottom" : "draw_top"; draw.targets = {recipient}; skillEffect(draw, recipient); }
        return ContinueEffects;
    }
};
class Wenguagive : public WenguaVS
{
public:
    Wenguagive() : WenguaVS("wenguagive") {}
};
class Wengua : public TriggerSkillV2
{
public:
    Wengua() : TriggerSkillV2("wengua") { events << GameStart << EventPhaseStart << EventAcquireSkill << EventLoseSkill << Death; global = true; view_as_skill = new WenguaVS; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        QList<SkillInstanceRef> parents;
        for (ServerPlayer *owner : room->getAlivePlayers()) for (int id : owner->getSkillInstanceIds(objectName())) parents << SkillInstanceRef(owner->objectName(), SkillInstanceKey(objectName(), id));
        for (ServerPlayer *donor : room->getAllPlayers(true)) {
            for (const SkillInstance &instance : donor->getSkillInstances()) {
                if (instance.skillName != "wenguagive" || instance.source != SourceAttached || instance.parentRef.key.skillName != objectName()) continue;
                if (donor->isDead() || !parents.contains(instance.parentRef) || instance.parentRef.ownerObjectName == donor->objectName()) room->detachAttachedSkill(SkillInstanceRef(donor->objectName(), instance.key()));
            }
            if (donor->isDead()) continue;
            for (const SkillInstanceRef &parent : parents) if (parent.ownerObjectName != donor->objectName()) room->attachSkillToPlayer(donor, "wenguagive", parent);
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};
class Fuzhu : public TriggerSkillV2
{
public:
    Fuzhu() : TriggerSkillV2("fuzhu") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->isMale() || player->getPhase() != Player::Finish
            || room->getDrawPile().isEmpty()) return result;
        Slash slash(Card::NoSuit, 0);
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (room->getDrawPile().size() <= 10 * owner->getHp() && !owner->isLocked(&slash)
                && owner->canSlash(player, nullptr, false)) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.invoker->isAlive() || room->getDrawPile().isEmpty()
            || room->getDrawPile().size() > 10 * ctx.owner->getHp()
            || !ctx.owner->askForSkillInvoke(this, QVariant::fromValue(ctx.invoker))) return false;
        ctx.targets = {ctx.invoker};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        const QList<int> cards = room->getDrawPile();
        const int limit = room->getAllPlayers(true).size() * getEffectiveAmount(ctx);
        int used = 0;
        for (int id : cards) {
            if (!ctx.owner->isAlive() || !target->isAlive() || used >= limit) break;
            // Earlier Slashes can move later candidates; only current draw-pile material may be used.
            if (room->getCardPlace(id) != Player::DrawPile) continue;
            const Card *card = Sanguosha->getCard(id);
            if (!card || !card->isKindOf("Slash") || card->hasFlag("using") || ctx.owner->isLocked(card)
                || !ctx.owner->canSlash(target, card, false)) continue;
            room->useCardFromSkillEffect(CardUseStruct(card, ctx.owner, target), ctx, true);
            ++used;
        }
        room->swapPile();
        return false;
    }
};
static QVariantMap jiexunRef(const SkillInstanceRef &ref)
{ return ref.isValid() ? QVariantMap{{"owner", ref.ownerObjectName}, {"skill", ref.key.skillName}, {"id", ref.key.instanceID}} : QVariantMap(); }
static SkillInstanceRef jiexunReadRef(const QVariant &value)
{ const QVariantMap map = value.toMap(); return SkillInstanceRef(map.value("owner").toString(), SkillInstanceKey(map.value("skill").toString(), map.value("id").toInt())); }
static QVariantMap jiexunOrigin(const Player *owner, const SkillInstance &instance)
{ return {{"owner", owner->objectName()}, {"innate", instance.source == SourceInnate}, {"slot", instance.bindHead}, {"parent", jiexunRef(instance.parentRef)}, {"grant", jiexunRef(instance.grantActivationRef)}, {"root", jiexunRef(instance.frozenSourceRef)}}; }
static QList<int> jiexunCandidates(const Player *owner, const QVariantMap &origin)
{
    QList<int> result; int rank = 0;
    for (const SkillInstance &instance : owner->getSkillInstances()) {
        if (instance.skillName != "funan") continue;
        const QVariantMap other = jiexunOrigin(owner, instance); int current = 0;
        if (origin.value("innate").toBool() && instance.source == SourceInnate && instance.bindHead > 0 && origin.value("slot").toInt() == instance.bindHead && origin.value("owner").toString() == owner->objectName()) current = 4;
        else for (const QString &key : {QString("parent"), QString("grant"), QString("root")}) if (!origin.value(key).toMap().isEmpty() && origin.value(key) == other.value(key)) { current = key == "parent" ? 3 : key == "grant" ? 2 : 1; break; }
        if (current <= 0 || current < rank) continue; if (current > rank) { result.clear(); rank = current; } result << instance.instanceID;
    }
    return result;
}
static int jiexunChoose(Room *room, ServerPlayer *owner, const QList<int> &ids)
{
    if (ids.isEmpty()) return 0; if (ids.size() == 1) return ids.first();
    QStringList choices; for (int id : ids) choices << SkillInstanceUtils::formatName("funan", id);
    const int index = choices.indexOf(room->askForChoice(owner, "jiexun", choices.join("+"))); return index < 0 ? 0 : ids.at(index);
}
static void jiexunUpgrade(Room *room, ServerPlayer *target, int id)
{
    if (!target->findSkillInstance("funan", id)) return;
    target->setSkillInstanceStateValue("funan", id, "upgraded", true);
    room->setPlayerMark(target, "&funan", 1); room->safeSetPlayerProperty(target, "funan_level_up", true); room->changeTranslation(target, "funan", 2);
}
static void applyFunanReceipt(Room *room, const QVariantMap &receipt)
{
    ServerPlayer *recipient = room->findPlayerByObjectName(receipt.value("recipient").toString(), true);
    const QString reason = receipt.value("reason").toString();
    if (!recipient || reason.isEmpty()) return;
    for (const QVariant &value : receipt.value("cards").toList()) {
        const int id = value.toInt();
        if (id >= 0) room->setPlayerCardLimitation(recipient, "use,response", QString::number(id), false, reason);
    }
}
static void clearFunanReceipt(Room *room, const QVariantMap &receipt)
{
    ServerPlayer *recipient = room->findPlayerByObjectName(receipt.value("recipient").toString(), true);
    const QString reason = receipt.value("reason").toString();
    if (recipient && !reason.isEmpty()) room->removePlayerCardLimitationByReason(recipient, reason);
}
static void syncFunanRestrictions(Room *room, qint64 turn)
{
    if (!room) return;
    static bool syncing = false;
    if (syncing) return;
    syncing = true;
    const auto stop = qScopeGuard([&] { syncing = false; });
    const QVariantList receipts = room->getTag("FunanRestrictions").toList();
    QVariantList updated;
    bool changed = false;
    for (const QVariant &entry : receipts) {
        QVariantMap receipt = entry.toMap();
        const bool should = turn > 0 && receipt.value("turn").toLongLong() == turn;
        if (should != receipt.value("applied").toBool()) {
            if (should) applyFunanReceipt(room, receipt);
            else clearFunanReceipt(room, receipt);
            receipt.insert("applied", should);
            changed = true;
        }
        updated << receipt;
    }
    if (changed) room->setTag("FunanRestrictions", updated);
}
static void expireFunanTurn(Room *room, qint64 turn)
{
    if (!room || turn <= 0) return;
    QVariantList kept;
    for (const QVariant &entry : room->getTag("FunanRestrictions").toList()) {
        const QVariantMap receipt = entry.toMap();
        if (receipt.value("turn").toLongLong() != turn) { kept << entry; continue; }
        if (receipt.value("applied").toBool()) clearFunanReceipt(room, receipt);
    }
    room->setTag("FunanRestrictions", kept);
}

class Funan : public TriggerSkillV2
{
public:
    Funan() : TriggerSkillV2("funan") { events << CardResponded << CardUsed; }
    static QVariantList material(const Card *card)
    { QVariantList ids; if (!card) return ids; if (!card->isVirtualCard()) ids << card->getEffectiveId(); else for (int id : card->getSubcards()) ids << id; return ids; }
    static bool obtainable(Room *room, const QVariantList &ids)
    {
        if (ids.isEmpty()) return false;
        for (const QVariant &value : ids) { const int id = value.toInt(); if (id < 0 || room->getCardOwner(id) || (room->getCardPlace(id) != Player::PlaceTable && room->getCardPlace(id) != Player::DiscardPile)) return false; } return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = nullptr, *responded = nullptr; ServerPlayer *owner = nullptr;
        if (event == CardUsed) { const CardUseStruct use = data.value<CardUseStruct>(); card = use.card; responded = use.whocard; owner = use.who; }
        else { const CardResponseStruct response = data.value<CardResponseStruct>(); if (response.m_isRetrial) return {}; card = response.m_card; responded = response.m_toCard; owner = response.m_who; }
        if (!player || !player->isAlive() || !owner || owner == player || !owner->isAlive() || !owner->hasSkill(objectName()) || !card || !responded || card->isKindOf("SkillCard") || responded->isKindOf("SkillCard") || room->getCardUser(responded) != owner) return {};
        TriggerList result;
        for (int id : owner->getValidSkillInstanceIds(objectName()))
            if (obtainable(room, material(owner->getSkillInstanceStateValue(objectName(), id, "upgraded").toBool() ? card : responded))) result[owner] << SkillInstanceUtils::formatName(objectName(), id);
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const Card *card = nullptr, *responded = nullptr;
        if (event == CardUsed) { const CardUseStruct use = ctx.original_data->value<CardUseStruct>(); card = use.card; responded = use.whocard; }
        else { const CardResponseStruct response = ctx.original_data->value<CardResponseStruct>(); card = response.m_card; responded = response.m_toCard; }
        const bool upgraded = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "upgraded").toBool();
        ctx.extra_data = QVariantMap{{"upgraded", upgraded}, {"response", material(card)}, {"original", material(responded)}, {"responder", player->objectName()}};
        const QVariant previous = ctx.owner->getTag("FunanCard"); ctx.owner->setTag("FunanCard", QVariant::fromValue(responded));
        const auto restore = qScopeGuard([&] { if (previous.isValid()) ctx.owner->setTag("FunanCard", previous); else ctx.owner->removeTag("FunanCard"); });
        if (!room->askForSkillInvoke(ctx.owner, objectName(), QVariant::fromValue(player))) return false;
        ctx.targets = {upgraded ? ctx.owner : player}; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap details = ctx.extra_data.toMap(); const bool receive = details.value("upgraded").toBool() || ctx.choice == "receive";
        const QVariantList ids = details.value(receive ? "response" : "original").toList();
        if (!obtainable(room, ids) || getEffectiveAmount(ctx) <= 0) return false;
        if (!receive) {
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong(); if (turn <= 0) return false;
            const int serial = room->getTag("FunanSerial").toInt() + 1; room->setTag("FunanSerial", serial);
            const QString reason = QString("funan:%1").arg(serial);
            QVariantList restrictions = room->getTag("FunanRestrictions").toList();
            QVariantMap receipt{{"turn", turn}, {"recipient", target->objectName()}, {"reason", reason}, {"cards", ids}, {"applied", true},
                {"source", jiexunRef(ctx.sourceRef)}, {"activation", jiexunRef(ctx.activationRef)}};
            restrictions << receipt;
            room->setTag("FunanRestrictions", restrictions);
            // The limit skill outlives the granting instance. Losing 复难 does not lift an accepted restriction.
            if (target->isAlive() && !target->hasSkill("#funan-limit", true)) room->acquireSkill(target, "#funan-limit", false, false, false);
            applyFunanReceipt(room, receipt);
        }
        DummyCard cards; for (const QVariant &id : ids) cards.addSubcard(id.toInt()); room->obtainCard(target, &cards, true);
        if (!receive && ctx.owner->isAlive()) { SkillContext next = ctx; next.choice = "receive"; next.targets = {ctx.owner}; skillEffect(event, room, player, next, ctx.owner); }
        return false;
    }
};

class FunanRemove : public TriggerSkillV2
{
public:
    FunanRemove() : TriggerSkillV2("#funanremove") { events << EventPhaseChanging << EventPhaseStart << TurnStart << TurnBroken << TurnedOver << Death << EventAcquireSkill << GameStart << EventSkillEffectFinished; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext ctx = data.value<SkillContext>();
            if (ctx.skill_name == objectName() && !ctx.activationRef.isValid()) {
                ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString(), true);
                if (holder) { QVariantList pending = holder->getTag("JiexunUpgrades").toList(); pending.removeOne(ctx.extra_data); holder->setTag("JiexunUpgrades", pending); }
            }
            return true;
        }
        const bool phaseEvent = event == EventPhaseChanging || event == EventPhaseStart;
        const bool turnEvent = event == TurnStart || event == TurnBroken || event == TurnedOver || event == Death;
        if (!phaseEvent && !turnEvent) return true;
        const QVariantMap scope = room->historyScopes();
        const qint64 turn = scope.value(QStringLiteral("turn_id")).toLongLong();
        const qint64 phase = scope.value(QStringLiteral("phase_id")).toLongLong();
        const bool ending = event == TurnBroken
            || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)
            || (event == EventPhaseStart && player && player->getPhase() == Player::NotActive)
            || (event == Death && player && data.value<DeathStruct>().who == player && player == room->getCurrent());
        // A face-down extra turn never opens NotActive. Drop only that inserted turn.
        const bool skippedExtra = event == TurnedOver && room->isCurrentExtraTurn() && phase <= 0 && turn > 0
            && player && player == room->getCurrent() && player->faceUp();
        if (event == Death && !ending) return true;
        qint64 active = turn;
        if (ending || skippedExtra) {
            expireFunanTurn(room, turn);
            // NotActive will not run for a broken turn that never opened a phase. Publish the suspended turn then.
            const bool resumeParent = (ending && event != TurnBroken) || skippedExtra
                || (event == TurnBroken && room->getCurrent() && room->getCurrent()->getPhase() == Player::NotActive);
            if (resumeParent) active = room->historyParent(turn, "turn", false).value("id").toLongLong();
        }
        syncFunanRestrictions(room, active);
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (!player || player->isDead() || (event != EventAcquireSkill && event != GameStart) || (event == EventAcquireSkill && data.value<SkillChangeStruct>().skillName != "funan")) return true;
        for (const QVariant &entry : player->getTag("JiexunUpgrades").toList()) {
            const QVariantMap receipt = entry.toMap(); if (jiexunCandidates(player, receipt.value("origin").toMap()).isEmpty()) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt(); ctx.owner = room->findPlayerByObjectName(receipt.value("issuer").toString(), true);
            ctx.initiator = ctx.owner; ctx.invoker = player; ctx.targets = {player}; ctx.sourceRef = jiexunReadRef(receipt.value("source")); ctx.extra_data = receipt; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString()); return holder && holder->getTag("JiexunUpgrades").toList().contains(ctx.extra_data); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString()); if (!holder) return false;
        ctx.choice = QString::number(jiexunChoose(room, holder, jiexunCandidates(holder, ctx.extra_data.toMap().value("origin").toMap()))); return ctx.choice.toInt() > 0;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString(), true); if (holder) { QVariantList pending = holder->getTag("JiexunUpgrades").toList(); pending.removeOne(ctx.extra_data); holder->setTag("JiexunUpgrades", pending); } return false; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { const int id = ctx.choice.toInt(); if (getEffectiveAmount(ctx) > 0 && jiexunCandidates(target, ctx.extra_data.toMap().value("origin").toMap()).contains(id)) jiexunUpgrade(room, target, id); return false; }
};

class Jiexun : public TriggerSkillV2
{
public:
    Jiexun() : TriggerSkillV2("jiexun") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const SkillInstance *instance = ctx.owner->findSkillInstance(objectName(), ctx.activationRef.key.instanceID); if (!instance) return false;
        const QVariantMap origin = jiexunOrigin(ctx.owner, *instance);
        int count = 0; QVariantMap query{{"kind", "skill_invoked"}, {"limit", 64}};
        while (true) {
            const QVariantMap page = room->queryHistoryFacts(query); if (!page.value("complete").toBool()) return false;
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap fact = entry.toMap().value("data").toMap();
                if (fact.value("activation_owner").toString() == ctx.activationRef.ownerObjectName && fact.value("activation_skill").toString() == objectName() && fact.value("activation_instance_id").toInt() == ctx.activationRef.key.instanceID) ++count;
            }
            if (!page.value("has_more").toBool()) break; query.insert("after", page.value("next_after"));
        }
        int diamonds = 0; for (ServerPlayer *holder : room->getAlivePlayers()) for (const Card *card : holder->getCards("ej")) if (card->getSuit() == Card::Diamond) ++diamonds;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner), objectName(), "@jiexun-invoke:" + QString::number(diamonds), true, true);
        if (!target) return false; ctx.targets = {target}; ctx.extra_data = QVariantMap{{"previous", count}, {"draw", diamonds}, {"origin", origin}}; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap details = ctx.extra_data.toMap();
        if (ctx.choice == "upgrade") {
            room->detachSkillFromPlayer(ctx.owner, SkillInstanceUtils::formatName(objectName(), ctx.activationRef.key.instanceID));
            const int id = jiexunChoose(room, target, jiexunCandidates(target, details.value("origin").toMap()));
            if (jiexunCandidates(target, details.value("origin").toMap()).contains(id)) { jiexunUpgrade(room, target, id); return false; }
            const int serial = room->getTag("JiexunSerial").toInt() + 1; room->setTag("JiexunSerial", serial);
            QVariantList pending = target->getTag("JiexunUpgrades").toList(); pending << QVariantMap{{"serial", serial}, {"recipient", target->objectName()}, {"issuer", ctx.owner->objectName()}, {"origin", details.value("origin")}, {"source", jiexunRef(ctx.sourceRef)}, {"activation", jiexunRef(ctx.activationRef)}}; target->setTag("JiexunUpgrades", pending); return false;
        }
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        const int draw = details.value("draw").toInt() * amount; if (draw > 0) target->drawCards(draw, objectName());
        const int discard = details.value("previous").toInt() * amount; if (target->isDead() || discard <= 0) return false;
        QList<int> original; for (const Card *card : target->getCards("he")) original << card->getEffectiveId(); if (original.isEmpty()) return false;
        const QVariantMap before = room->queryHistoryMoves({{"from", target->objectName()}, {"limit", 1}});
        const Card *paid = room->askForDiscard(target, objectName(), discard, discard, false, true);
        if (!paid || !before.value("complete").toBool()) return false;
        QList<int> actual; QVariantMap query{{"from", target->objectName()}, {"after", before.value("watermark")}, {"limit", 64}};
        while (true) {
            const QVariantMap page = room->queryHistoryMoves(query); if (!page.value("complete").toBool()) return false;
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap move = entry.toMap().value("data").toMap(); const int id = move.value("card_id", -1).toInt();
                if (paid->getSubcards().contains(id) && original.contains(id) && move.value("to_place").toInt() == Player::DiscardPile && (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) actual << id;
            }
            if (!page.value("has_more").toBool()) break; query.insert("after", page.value("next_after"));
        }
        for (int id : original) if (!actual.contains(id)) return false;
        SkillContext upgrade = ctx; upgrade.choice = "upgrade"; upgrade.targets = {ctx.owner}; skillEffect(event, room, player, upgrade, ctx.owner); return false;
    }
};
class Shouxi : public TriggerSkillV2
{
public:
    Shouxi() : TriggerSkillV2("shouxi") { events << TargetConfirmed << EventSkillInvoking; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != TargetConfirmed) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && use.card && use.card->isKindOf("Slash")
            && use.to.contains(player) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QStringList used = ctx.owner->getSkillInstanceStateValue(ctx.activationRef.key.skillName,
            ctx.activationRef.key.instanceID, "declared_names").toStringList();
        QStringList names;
        QList<int> ids;
        for (int id : Sanguosha->getRandomCards()) {
            const Card *card = Sanguosha->getEngineCard(id);
            if (card->isKindOf("EquipCard") || names.contains(card->objectName()) || used.contains(card->objectName())) continue;
            names << card->objectName(); ids << id;
        }
        if (ids.isEmpty() || !ctx.owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        room->fillAG(ids, ctx.owner);
        const auto close = qScopeGuard([&] { room->clearAG(ctx.owner); });
        const int id = room->askForAG(ctx.owner, ids, false, objectName());
        if (!ids.contains(id)) return false;
        ctx.choice = Sanguosha->getEngineCard(id)->objectName();
        ctx.extra_data = Sanguosha->getEngineCard(id)->getClassName();
        ctx.targets = {ctx.owner};
        return true;
    }
    void commitDeclaration(Room *room, const SkillContext &ctx) const
    {
        QStringList used = ctx.owner->getSkillInstanceStateValue(ctx.activationRef.key.skillName,
            ctx.activationRef.key.instanceID, "declared_names").toStringList();
        if (!used.contains(ctx.choice)) used << ctx.choice;
        ctx.owner->setSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "declared_names", used);
        room->setPlayerProperty(ctx.owner, "shouxi_names", used);
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner->getSkillInstanceStateValue(ctx.activationRef.key.skillName,
            ctx.activationRef.key.instanceID, "declared_names").toStringList().contains(ctx.choice)) return false;
        commitDeclaration(room, ctx);
        return true;
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        // The accepted declaration remains spent even when its whole effect is cancelled.
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) commitDeclaration(room, accepted);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        LogMessage log; log.type = "#ShouxiChoice"; log.from = ctx.owner; log.arg = ctx.choice; room->sendLog(log);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        ServerPlayer *attacker = use.from;
        const bool paid = attacker && attacker->isAlive() && attacker->canDiscard(attacker, "h")
            && room->askForCard(attacker, ctx.extra_data.toString(), "shouxi-discard:" + ctx.choice, *ctx.original_data);
        use = ctx.original_data->value<CardUseStruct>();
        if (!paid) {
            use.nullified_list << target->objectName();
            *ctx.original_data = QVariant::fromValue(use);
            return false;
        }
        for (int i = 0; i < getEffectiveAmount(ctx) && attacker->isAlive() && target->isAlive() && !target->isNude(); ++i) {
            const int id = room->askForCardChosen(attacker, target, "he", objectName());
            const Player::Place place = room->getCardPlace(id);
            if (id < 0 || room->getCardOwner(id) != target || Sanguosha->getCard(id)->hasFlag("using")
                || (place != Player::PlaceHand && place != Player::PlaceEquip)) break;
            room->obtainCard(attacker, Sanguosha->getCard(id),
                CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, attacker->objectName()), place != Player::PlaceHand);
        }
        return false;
    }
};
HuiminCard::HuiminCard()
{
    setSkillName("huimin");
	mute = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool HuiminCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
	return targets.isEmpty() && to_select->hasFlag("huimin_target");
}

void HuiminCard::onUse(Room *room, CardUseStruct &card_use) const
{
	foreach (ServerPlayer *p, card_use.to)
		room->setPlayerFlag(p, "huimin_start_target");
}

class HuiminVS : public ViewAsSkillV2
{
public:
    HuiminVS() : ViewAsSkillV2("huimin", 999) { response_pattern = "@@huimin!"; }
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.pattern == "@@huimin!" && request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "count").toInt() > 0; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return canActivate(request) && card && !card->hasFlag("using") && request.initiator->handCards().contains(card->getEffectiveId())
            && !request.selectedCardIds.contains(card->getEffectiveId()) && request.selectedCardIds.size() < request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "count").toInt();
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.selectedCardIds.size() != request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "count").toInt()) return false;
        ActiveSkillRequest checked = request; checked.selectedCardIds.clear();
        for (int id : request.selectedCardIds) { if (!canSelectCard(checked, Sanguosha->getCard(id))) return false; checked.selectedCardIds << id; } return true;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return request.initiator && selected.isEmpty() && target && target->isAlive() && request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "recipients").toStringList().contains(target->objectName()); }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return targets.size() == 1 && canSelectTarget(request, {}, targets.first()); }
    QString historyKey(const ActiveSkillRequest &) const override { return "HuiminCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantList ids; if (ctx.use_card) for (int id : ctx.use_card->getSubcards()) ids << id;
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "selection", ids);
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "start", target->objectName()); return ContinueEffects;
    }
};

class Huimin : public TriggerSkillV2
{
public:
    Huimin() : TriggerSkillV2("huimin") { events << EventPhaseStart; view_as_skill = new HuiminVS; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || player->isDead() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish) return {};
        for (ServerPlayer *target : room->getAlivePlayers()) if (target->getHandcardNum() < target->getHp()) return {{player, {objectName()}}}; return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QStringList recipients;
        for (ServerPlayer *target : room->getAlivePlayers()) if (target->getHandcardNum() < target->getHp()) recipients << target->objectName();
        if (recipients.isEmpty() || !ctx.owner->askForSkillInvoke(this, QString("huimin_invoke:%1").arg(recipients.size()))) return false;
        ctx.extra_data = recipients; ctx.targets = {ctx.owner}; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "take") {
            QVariantMap details = ctx.extra_data.toMap(); ServerPlayer *holder = room->findPlayerByObjectName(details.value("holder").toString()); QList<int> ids;
            for (const QVariant &value : details.value("ids").toList()) {
                const int id = value.toInt(); if (holder && room->getCardOwner(id) == holder && room->getCardPlace(id) == Player::PlaceHand && !Sanguosha->getCard(id)->hasFlag("using")) ids << id;
            }
            if (!ids.isEmpty()) {
                const int id = room->askForAG(target, ids, false, objectName());
                if (ids.contains(id) && room->getCardOwner(id) == holder && room->getCardPlace(id) == Player::PlaceHand && !Sanguosha->getCard(id)->hasFlag("using")) {
                    ids.removeOne(id);
                    if (target != holder) room->obtainCard(target, Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_GIVE, holder->objectName(), target->objectName(), objectName(), QString()), true);
                }
            }
            QVariantList remaining; for (int id : ids) remaining << id; details.insert("ids", remaining); ctx.extra_data = details; return false;
        }
        const QStringList names = ctx.extra_data.toStringList(); const int count = names.size() * getEffectiveAmount(ctx);
        if (count <= 0) return false; target->drawCards(count, objectName()); if (target->isDead()) return false;
        QList<ServerPlayer *> recipients;
        for (ServerPlayer *recipient : room->getAlivePlayers()) if (names.contains(recipient->objectName())) recipients << recipient;
        if (recipients.isEmpty()) return false;
        QList<int> ids; ServerPlayer *start = nullptr;
        if (target->getHandcardNum() > count) {
            Room::AcceptedViewAsEffectScope scope(room, target, objectName(), ctx);
            if (scope.isValid()) {
                const int instance = scope.activationRef().key.instanceID;
                target->setSkillInstanceStateValue(objectName(), instance, "count", count);
                target->setSkillInstanceStateValue(objectName(), instance, "recipients", names);
                room->askForUseCard(target, "@@huimin!", "@huimin:" + QString::number(count));
                for (const QVariant &value : target->getSkillInstanceStateValue(objectName(), instance, "selection").toList()) ids << value.toInt();
                start = room->findPlayerByObjectName(target->getSkillInstanceStateValue(objectName(), instance, "start").toString());
            }
        } else ids = target->handCards();
        QList<int> valid;
        for (int id : ids) if (!valid.contains(id) && room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand && !Sanguosha->getCard(id)->hasFlag("using")) valid << id;
        ids = valid;
        if (ids.isEmpty()) {
            QList<int> hand = target->handCards();
            while (!hand.isEmpty() && ids.size() < count) { const int index = qsanRandomBounded(hand.size()); const int id = hand.takeAt(index); if (!Sanguosha->getCard(id)->hasFlag("using")) ids << id; }
        }
        if (ids.isEmpty()) return false;
        if (!recipients.contains(start) || start->isDead()) start = room->askForPlayerChosen(target, recipients, objectName(), "@huimin-chooseplayer");
        if (!recipients.contains(start)) return false;
        room->showCard(target, ids);
        room->fillAG(ids); const auto clear = qScopeGuard([&] { room->clearAG(); });
        // Traverse a bounded snapshot. Death during a recipient callback cannot create an endless next-alive loop.
        room->sortByActionOrder(recipients); const int offset = recipients.indexOf(start);
        QVariantList remaining; for (int id : ids) remaining << id;
        int stalled = 0;
        for (int i = 0; i < recipients.size() * getEffectiveAmount(ctx) && !remaining.isEmpty() && stalled < recipients.size(); ++i) {
            ServerPlayer *recipient = recipients.at((offset + i) % recipients.size());
            const int before = remaining.size();
            if (recipient->isAlive()) {
                SkillContext part = ctx; part.choice = "take"; part.targets = {recipient}; part.extra_data = QVariantMap{{"holder", target->objectName()}, {"ids", remaining}};
                skillEffect(event, room, player, part, recipient); remaining = part.extra_data.toMap().value("ids").toList();
            }
            stalled = remaining.size() == before ? stalled + 1 : 0;
        }
        return false;
    }
};
class Bizhuan : public TriggerSkillV2
{
public:
	Bizhuan() : TriggerSkillV2("bizhuan")
	{
		events << TargetConfirmed << CardUsed;
		frequency = Frequent;
	}

	TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
	{
		const CardUseStruct use = data.value<CardUseStruct>();
		return player && player->isAlive() && player->hasSkill(objectName()) && use.card
		        && use.card->getSuit() == Card::Spade && !use.card->isKindOf("SkillCard")
		        && (event != TargetConfirmed || (use.from != player && use.to.contains(player)))
		        && player->getPile("book").size() < 4
		    ? TriggerList{{player, {objectName()}}} : TriggerList();
	}

	bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
	{
		return ctx.owner->getPile("book").size() < 4 && ctx.owner->askForSkillInvoke(this);
	}

	bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
	{
		room->broadcastSkillInvoke(objectName());
		const int count = qMin(qMax(0, getEffectiveAmount(ctx)), 4 - int(ctx.invoker->getPile("book").size()));
		for (int i = 0; i < count && ctx.invoker->isAlive(); ++i)
			if (ctx.invoker->getPile("book").size() < 4) ctx.invoker->addToPile("book", room->drawCard());
		return false;
	}
};

class BizhuanKeep : public MaxCardsSkillV2
{
public:
	BizhuanKeep() : MaxCardsSkillV2("#bizhuankeep")
	{
		frequency = Frequent;
	}

	CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
	{
		return ctx.holder ? CorrectSkillResult::useAmount(ctx.holder->getPile("book").length() * ctx.currentAmount)
		                  : CorrectSkillResult::noEffect();
	}
};

TongboCard::TongboCard()
{
    setSkillName("tongbo");
	will_throw = false;
	handling_method = Card::MethodNone;
	target_fixed = true;
}

void TongboCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
	QList<int> pile = source->getPile("book");
	QList<int> to_handcard;
	QList<int> to_pile;
	foreach (int id, subcards) {
		if (pile.contains(id))
			to_handcard << id;
		else
			to_pile << id;
	}

	LogMessage log;
	log.type = "#QixingExchange";
	log.from = source;
	log.arg = QString::number(to_pile.length());
	log.arg2 = "tongbo";
	//room->sendLog(log);

	source->addToPile("book", to_pile, false);

	DummyCard to_handcard_x(to_handcard);
	CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, source->objectName());
	room->obtainCard(source, &to_handcard_x, reason, true);
	to_handcard_x.deleteLater();

	QStringList suitlist;
	pile = source->getPile("book");
	foreach (int id, pile) {
		QString str = Sanguosha->getCard(id)->getSuitString();
		if (!suitlist.contains(str)) suitlist << str;
	}
	if (suitlist.length() < 4) return;
	QList<ServerPlayer *> _player;
	_player.append(source);

	CardsMoveStruct move(pile, source, source, Player::PlaceTable, Player::PlaceHand,
		CardMoveReason(CardMoveReason::S_REASON_PUT, source->objectName(), "tongbo", ""));
	QList<CardsMoveStruct> moves;
	moves.append(move);
	room->notifyMoveCards(true, moves, false, _player);
	room->notifyMoveCards(false, moves, false, _player);

	QList<int> origin_ids = pile;
	while (source->isAlive()&&room->askForYiji(source, pile, "tongbo", false, true, false, -1, room->getOtherPlayers(source))) {
		CardsMoveStruct move(QList<int>(), source, source, Player::PlaceHand, Player::PlaceTable,
			CardMoveReason(CardMoveReason::S_REASON_PUT, source->objectName(), "tongbo", ""));
		foreach (int id, origin_ids) {
			if (!pile.contains(id))
				move.card_ids << id;
		}
		origin_ids = pile;
		QList<CardsMoveStruct> moves;
		moves.append(move);
		room->notifyMoveCards(true, moves, false, _player);
		room->notifyMoveCards(false, moves, false, _player);
	}
}

class TongboVS : public ViewAsSkillV2
{
public:
    TongboVS() : ViewAsSkillV2("tongbo", 999) { response_pattern = "@@tongbo"; expand_pile = "book"; }
    TargetMode targetMode() const override { return NoTarget; }
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.pattern == "@@tongbo" && !request.initiator->getPile("book").isEmpty(); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return canActivate(request) && card && !card->hasFlag("using") && !request.selectedCardIds.contains(card->getEffectiveId())
            && request.selectedCardIds.size() < 2 * request.initiator->getPile("book").size()
            && (request.initiator->getPile("book").contains(card->getEffectiveId()) || request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId()));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request)) return false;
        int pile = 0; ActiveSkillRequest checked = request; checked.selectedCardIds.clear();
        for (int id : request.selectedCardIds) { if (!canSelectCard(checked, Sanguosha->getCard(id))) return false; checked.selectedCardIds << id; if (request.initiator->getPile("book").contains(id)) ++pile; }
        return pile * 2 == request.selectedCardIds.size();
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "TongboCard"; }
    EffectFlow effect(SkillContext &ctx) const override { skillEffect(ctx, ctx.initiator); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        if (ctx.choice == "give") {
            const QVariantMap details = ctx.extra_data.toMap(); ServerPlayer *holder = room->findPlayerByObjectName(details.value("holder").toString());
            DummyCard gift; if (!holder) return ContinueEffects;
            for (const QVariant &value : details.value("ids").toList()) {
                const int id = value.toInt(); if (!holder->getPile("book").contains(id) || room->getCardOwner(id) != holder || room->getCardPlace(id) != Player::PlaceSpecial || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
                gift.addSubcard(id);
            }
            if (!gift.getSubcards().isEmpty()) room->obtainCard(target, &gift, CardMoveReason(CardMoveReason::S_REASON_GIVE, holder->objectName(), target->objectName(), objectName(), QString()), true);
            return ContinueEffects;
        }
        if (!ctx.use_card || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        QList<int> incoming, outgoing;
        for (int id : ctx.use_card->getSubcards()) {
            if (room->getCardOwner(id) != target || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
            if (target->getPile("book").contains(id)) outgoing << id;
            else if (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip) incoming << id;
            else return ContinueEffects;
        }
        if (incoming.size() != outgoing.size()) return ContinueEffects;
        if (!incoming.isEmpty()) {
            // Exchange both directions in one native movement, before checking the resulting pile.
            CardsMoveStruct put(incoming, target, target, Player::PlaceUnknown, Player::PlaceSpecial, CardMoveReason(CardMoveReason::S_REASON_PUT, target->objectName(), objectName(), QString())); put.to_pile_name = "book";
            CardsMoveStruct take(outgoing, target, target, Player::PlaceSpecial, Player::PlaceHand, CardMoveReason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, target->objectName(), objectName(), QString())); take.from_pile_name = "book";
            room->moveCardsAtomic(QList<CardsMoveStruct>{put, take}, false);
        }
        QStringList suits; for (int id : target->getPile("book")) if (!suits.contains(Sanguosha->getCard(id)->getSuitString())) suits << Sanguosha->getCard(id)->getSuitString();
        if (suits.size() < 4 || target->isDead()) return ContinueEffects;
        QList<int> remaining = target->getPile("book");
        while (!remaining.isEmpty() && target->isAlive()) {
            const QList<ServerPlayer *> others = room->getOtherPlayers(target); if (others.isEmpty()) break;
            QList<int> offered = remaining;
            const CardsMoveStruct proposal = room->askForYijiStruct(target, offered, objectName(), false, true, false, -1, others, CardMoveReason(), QString(), false, false);
            ServerPlayer *recipient = qobject_cast<ServerPlayer *>(proposal.to);
            const QList<int> allocation = recipient && others.contains(recipient) && !proposal.card_ids.isEmpty() ? proposal.card_ids : remaining;
            if (!recipient || !others.contains(recipient)) recipient = others.at(qsanRandomBounded(others.size()));
            QVariantList chosen;
            for (int id : allocation) if (remaining.contains(id) && target->getPile("book").contains(id)) { chosen << id; remaining.removeOne(id); }
            if (chosen.isEmpty()) break;
            SkillContext part = ctx; part.choice = "give"; part.targets = {recipient}; part.extra_data = QVariantMap{{"holder", target->objectName()}, {"ids", chosen}}; skillEffect(part, recipient);
            // A cancelled recipient hook consumes that allocation; never repeatedly prompt the same cards.
            QList<int> current; for (int id : remaining) if (target->getPile("book").contains(id)) current << id; remaining = current;
        }
        return ContinueEffects;
    }
};
class Tongbo : public TriggerSkillV2
{
public:
    Tongbo() : TriggerSkillV2("tongbo") { events << EventPhaseEnd; view_as_skill = new TongboVS; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Draw && !player->getPile("book").isEmpty() ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    { Room::AcceptedViewAsEffectScope accepted(room, ctx.owner, objectName(), ctx); if (accepted.isValid()) room->askForUseCard(ctx.owner, "@@tongbo", "@tongbo"); return false; }
};
MobileQingxianCard::MobileQingxianCard()
{
    setSkillName("mobileqingxian");
}

bool MobileQingxianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length() < getSubcards().length() && to_select != Self;
}

bool MobileQingxianCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == getSubcards().length();
}

void MobileQingxianCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets) {
		if (source->isDead()) break;
		if (p->isDead()) continue;
		room->cardEffect(this, source, p);
	}
	if (targets.length() == source->getHp() && source->isAlive())
		source->drawCards(1, "mobileqingxian");
}

void MobileQingxianCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.from->isDead() || effect.to->isDead()) return;
	Room *room = effect.from->getRoom();
	if (effect.to->getEquips().length() > effect.from->getEquips().length())
		room->loseHp(HpLostStruct(effect.to, 1, "mobileqingxian", effect.from));
	else if (effect.to->getEquips().length() == effect.from->getEquips().length())
		effect.to->drawCards(1, "mobileqingxian");
	else
		room->recover(effect.to, RecoverStruct("mobileqingxian", effect.from));
}

class MobileQingxian : public ViewAsSkillV2
{
public:
    explicit MobileQingxian(const QString &name = "mobileqingxian") : ViewAsSkillV2(name) {}
    LimitScope getLimitScope() const override { return Limit_Turn; }
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileQingxianCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getHp() > 0 && request.initiator->canDiscard(request.initiator, "he");
    }

    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!request.initiator || !card || card->hasFlag("using") || request.initiator->isJilei(card)) return false;
        const int id = card->getEffectiveId();
        return id >= 0 && !request.selectedCardIds.contains(id)
            && request.selectedCardIds.size() < qMin(request.initiator->getHp(), int(request.initiator->getAliveSiblings().size()))
            && (request.initiator->handCards().contains(id) || request.initiator->getEquipsId().contains(id));
    }

    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty()) return false;
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
        return candidate && candidate->isAlive() && candidate != request.initiator && !selected.contains(candidate)
            && selected.size() < request.selectedCardIds.size();
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        if (selected.isEmpty() || selected.size() != request.selectedCardIds.size()) return false;
        QList<const Player *> checked;
        for (const Player *target : selected) {
            if (!canSelectTarget(request, checked, target)) return false;
            checked << target;
        }
        return true;
    }

    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        // Each paid target settles before the optional final draw recipient.
        ctx.choice = "target";
        for (ServerPlayer *target : targets) {
            if (!ctx.invoker->isAlive()) return FinishSkill;
            if (skillEffect(ctx, target) == FinishSkill) return FinishSkill;
        }
        if (ctx.invoker->isAlive() && targets.size() == ctx.invoker->getHp()) {
            SkillContext reward = ctx;
            reward.choice = "reward";
            reward.targets = {ctx.invoker};
            skillEffect(reward, ctx.invoker);
        }
        return ContinueEffects;
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        if (ctx.choice == "reward") target->drawCards(getEffectiveAmount(ctx), objectName());
        else if (target->getEquips().size() > ctx.invoker->getEquips().size())
            room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        else if (target->getEquips().size() == ctx.invoker->getEquips().size())
            target->drawCards(getEffectiveAmount(ctx), objectName());
        else room->recover(target, RecoverStruct(ctx.invoker, nullptr, getEffectiveAmount(ctx), objectName()));
        return ContinueEffects;
    }
};
class MobileJuexiang : public TriggerSkillV2
{
public:
    MobileJuexiang() : TriggerSkillV2("mobilejuexiang") { events << Death; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    { return player && data.value<DeathStruct>().who == player && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.manual_effect = true; return true; }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const DeathStruct death = ctx.original_data->value<DeathStruct>();
        if (death.damage && death.damage->from && death.damage->from->isAlive()) { SkillContext punish = ctx; punish.choice = "punish"; punish.targets = {death.damage->from}; skillEffect(event, room, player, punish, death.damage->from); }
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "@mobilejuexiang-invoke", true, true);
        if (target) { SkillContext grant = ctx; grant.choice = "canyun"; grant.targets = {target}; skillEffect(event, room, player, grant, target); }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "punish") { target->throwAllEquips(); if (target->isAlive()) room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.owner)); return false; }
        if (ctx.choice == "inherit") { if (!target->hasSkill(objectName())) room->acquireSkillFromEffect(target, objectName(), ctx); return false; }
        if (ctx.choice == "discard") {
            const QVariantMap choice = ctx.extra_data.toMap(); const int id = choice.value("id", -1).toInt();
            ServerPlayer *heir = room->findPlayerByObjectName(choice.value("heir").toString());
            if (!heir || id < 0 || room->getCardOwner(id) != target || (room->getCardPlace(id) != Player::PlaceEquip && room->getCardPlace(id) != Player::PlaceDelayedTrick)
                || Sanguosha->getCard(id)->hasFlag("using") || Sanguosha->getCard(id)->getSuit() != Card::Club || !heir->canDiscard(target, id)) return false;
            const QVariantMap before = room->queryHistoryMoves({{"from", target->objectName()}, {"limit", 1}});
            room->throwCard(id, room->getCardPlace(id) == Player::PlaceDelayedTrick ? nullptr : target, heir);
            if (!before.value("complete").toBool()) return false;
            QVariantMap query{{"from", target->objectName()}, {"after", before.value("watermark")}, {"limit", 64}}; bool discarded = false;
            while (true) {
                const QVariantMap page = room->queryHistoryMoves(query); if (!page.value("complete").toBool()) return false;
                if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
                for (const QVariant &entry : page.value("items").toList()) { const QVariantMap move = entry.toMap().value("data").toMap(); if (move.value("card_id", -1).toInt() == id && move.value("to_place").toInt() == Player::DiscardPile && (move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) discarded = true; }
                if (!page.value("has_more").toBool()) break; query.insert("after", page.value("next_after"));
            }
            if (discarded && heir->isAlive()) { SkillContext inherit = ctx; inherit.choice = "inherit"; inherit.targets = {heir}; skillEffect(event, room, player, inherit, heir); }
            return false;
        }
        if (!target->hasSkill("mobilecanyun")) room->acquireSkillFromEffect(target, "mobilecanyun", ctx);
        if (target->isDead()) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *other : room->getAlivePlayers()) for (const Card *card : other->getCards("ej")) if (card->getSuit() == Card::Club && target->canDiscard(other, card->getEffectiveId())) { candidates << other; break; }
        if (candidates.isEmpty()) return false;
        ServerPlayer *victim = room->askForPlayerChosen(target, candidates, "mobilejuexiang_discard", "@mobilejuexiang-discard", true);
        if (!victim) return false;
        QList<int> disabled; for (const Card *card : victim->getCards("ej")) if (card->getSuit() != Card::Club || card->hasFlag("using") || !target->canDiscard(victim, card->getEffectiveId())) disabled << card->getEffectiveId();
        const int id = room->askForCardChosen(target, victim, "ej", objectName(), false, Card::MethodDiscard, disabled);
        SkillContext discard = ctx; discard.choice = "discard"; discard.targets = {victim}; discard.extra_data = QVariantMap{{"id", id}, {"heir", target->objectName()}}; skillEffect(event, room, player, discard, victim);
        return false;
    }
};
MobileCanyunCard::MobileCanyunCard()
{
    setSkillName("mobilecanyun");
}

bool MobileCanyunCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.length() < getSubcards().length() && to_select != Self && to_select->getMark("mobilecanyun_used" + Self->objectName()) <= 0;
}

bool MobileCanyunCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
	return targets.length() == getSubcards().length();
}

void MobileCanyunCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	foreach (ServerPlayer *p, targets)
		room->addPlayerMark(p, "mobilecanyun_used" + source->objectName());
	foreach (ServerPlayer *p, targets) {
		if (source->isDead()) break;
		if (p->isDead()) continue;
		room->cardEffect(this, source, p);
	}
	if (targets.length() == source->getHp() && source->isAlive())
		source->drawCards(1, "mobilecanyun");
}

void MobileCanyunCard::onEffect(CardEffectStruct &effect) const
{
	if (effect.from->isDead() || effect.to->isDead()) return;
	Room *room = effect.from->getRoom();
	if (effect.to->getEquips().length() > effect.from->getEquips().length())
		room->loseHp(HpLostStruct(effect.to, 1, "mobilecanyun", effect.from));
	else if (effect.to->getEquips().length() == effect.from->getEquips().length())
		effect.to->drawCards(1, "mobilecanyun");
	else
		room->recover(effect.to, RecoverStruct("mobilecanyun", effect.from));
}

class MobileCanyun : public MobileQingxian
{
public:
    MobileCanyun() : MobileQingxian("mobilecanyun") {}
    QString historyKey(const ActiveSkillRequest &) const override { return "MobileCanyunCard"; }
    QStringList usedTargets(const ActiveSkillRequest &request) const
    {
        return request.initiator ? request.initiator->getSkillInstanceStateValue(request.activationRef.key.skillName,
            request.activationRef.key.instanceID, "used_targets").toStringList() : QStringList();
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!MobileQingxian::canActivate(request)) return false;
        const QStringList used = usedTargets(request);
        for (const Player *target : request.initiator->getAliveSiblings()) if (!used.contains(target->objectName())) return true;
        return false;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (!MobileQingxian::canSelectCard(request, card)) return false;
        const QStringList used = usedTargets(request);
        int available = 0;
        for (const Player *target : request.initiator->getAliveSiblings()) if (!used.contains(target->objectName())) ++available;
        return request.selectedCardIds.size() < available;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return MobileQingxian::canSelectTarget(request, selected, candidate) && !usedTargets(request).contains(candidate->objectName());
    }
    static void commitTargets(const SkillContext &ctx)
    {
        QStringList used = ctx.invoker->getSkillInstanceStateValue(ctx.activationRef.key.skillName,
            ctx.activationRef.key.instanceID, "used_targets").toStringList();
        for (ServerPlayer *target : ctx.targets) if (!used.contains(target->objectName())) used << target->objectName();
        ctx.invoker->setSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "used_targets", used);
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ViewAsSkillV2::pay(room, ctx, request)) return false;
        commitTargets(ctx);
        return true;
    }
};

class MobileCanyunRecord : public TriggerSkillV2
{
public:
    MobileCanyunRecord() : TriggerSkillV2("#mobilecanyun-record") { events << EventSkillInvoking; global = true; }
    bool recordEvent(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const SkillContext accepted = data.value<SkillContext>();
        // A waived payment still reserves the accepted targets; no new source is looked up by name.
        if (accepted.bypass_cost && accepted.invoker && player == accepted.invoker && accepted.activationRef.isValid()
            && accepted.activationRef.ownerObjectName == accepted.invoker->objectName()
            && accepted.activationRef.key.skillName == "mobilecanyun") MobileCanyun::commitTargets(accepted);
        return true;
    }
};
OLZhongjianCard::OLZhongjianCard()
{
    setSkillName("olzhongjian");
	will_throw = false;
	handling_method = Card::MethodNone;
}

bool OLZhongjianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && to_select != Self && !to_select->isKongcheng();
}

void OLZhongjianCard::onEffect(CardEffectStruct &effect) const
{
	Room *room = effect.to->getRoom();
	room->showCard(effect.from, getEffectiveId());
	int n = effect.to->getHp();
	if (n <= 0) return;
	QList<int> list;
	QStringList slist;
	for (int i = 0; i < n; i++) {
		if(list.length()>=effect.to->getHandcardNum()) break;
		int id = room->askForCardChosen(effect.from,effect.to,"h","olzhongjian",false,Card::MethodNone,list);
		list << id;
		slist << Sanguosha->getCard(id)->toString();
	}/*
	LogMessage log;
	log.type = "$ZhongjianShow";
	log.from = effect.from;
	log.to << effect.to;
	log.arg = QString::number(n);
	log.card_str = slist.join("+");
	room->sendLog(log);
	room->fillAG(list);
	room->getThread()->delay(2000);
	room->clearAG();*/
	room->showCard(effect.to, list);
	room->getThread()->delay();
	bool samecolour = false, samenumber = false;
	foreach (int id, list) {
		if (sameColorWith(Sanguosha->getCard(id)))
			samecolour = true;
		if (getNumber() == Sanguosha->getCard(id)->getNumber())
			samenumber = true;
		if (samecolour && samenumber) break;
	}
	LogMessage newlog;
	if (!samecolour && !samenumber) {
		newlog.type = "#ZhongjianResult_NoSame";
		room->sendLog(newlog);
		effect.from->addMark("olzhongjian_debuff");
		room->addMaxCards(effect.from, 1,false);
		return;
	}
	newlog.type = "#ZhongjianResult";
	newlog.arg = samecolour == true ? "zhongjiansamecolour" : "zhongjiandifferentcolour";
	newlog.arg2 = samenumber == true ? "zhongjiansamenumber" : "zhongjiandifferentnumber";
	room->sendLog(newlog);
	if (samecolour) {
		QList<ServerPlayer *> targets;
		foreach (ServerPlayer *p, room->getOtherPlayers(effect.from)) {
			if (effect.from->canDiscard(p, "he")) targets << p;
		}
		ServerPlayer *target = room->askForPlayerChosen(effect.from, targets, "olzhongjian", "@olzhongjian-discard", true);
		if (target){
			int id = room->askForCardChosen(effect.from, target, "he", "olzhongjian", false, Card::MethodDiscard);
			room->throwCard(id, target, effect.from);
		}else
			effect.from->drawCards(1, "olzhongjian");
	}
	if (samenumber)
		room->setPlayerMark(effect.from, "olzhongjian-PlayClear",1);
}

class OLZhongjian : public Zhongjian
{
public:
    OLZhongjian() : Zhongjian("olzhongjian") {}
};
class OLCaishi : public Caishi
{
public:
    OLCaishi() : Caishi("olcaishi") {}
};

class OLCaishiMax : public CaishiMax
{
public:
    OLCaishiMax() : CaishiMax("#olcaishimax") { frequency = Frequent; }
};
class FunanLimit : public CardLimitSkill
{
public:
    FunanLimit() : CardLimitSkill("#funan-limit") {}
    QString limitList(const Player *, const Card *) const override { return "use,response"; }
    QString limitPattern(const Player *target, const Card *card) const override
    {
        const ServerPlayer *player = dynamic_cast<const ServerPlayer *>(target);
        if (!player || !card) return {};
        Room *room = player->getRoom();
        const int id = card->getEffectiveId();
        if (!room || id < 0) return {};
        const qint64 turn = room->historyScopes().value(QStringLiteral("turn_id")).toLongLong();
        if (turn <= 0) return {};
        for (const QVariant &entry : room->getTag("FunanRestrictions").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("turn").toLongLong() != turn || receipt.value("recipient").toString() != player->objectName()) continue;
            for (const QVariant &value : receipt.value("cards").toList()) if (value.toInt() == id) return QString::number(id);
        }
        return {};
    }
};

class OLCaishiPro : public ProhibitSkill
{
public:
	OLCaishiPro() : ProhibitSkill("#olcaishipro")
	{
		frequency = Frequent;
	}

    bool isProhibited(const Player *from, const Player *to, const Card *, const QList<const Player *> &) const
	{
		if (!from || !to) return false;
		if (const ServerPlayer *server = dynamic_cast<const ServerPlayer *>(from))
			if (Room *room = server->getRoom())
				writeCaishiRestrictions(room, "olcaishi", room->historyScopes().value(QStringLiteral("turn_id")).toLongLong());
		return from->property("olcaishi_restrictions").toStringList().contains("self") && from == to;
	}
};

YCZH2017Package::YCZH2017Package()
	: Package("YCZH2017")
{
	General *qinmi = new General(this, "qinmi", "shu", 3);
	qinmi->addSkill(new Jianzheng);
	qinmi->addSkill(new Zhuandui);
	qinmi->addSkill(new Tianbian);
	qinmi->addSkill(new Tianbian("#tianbian-number"));
	related_skills.insert("tianbian", "#tianbian-number");

	General *wuxian = new General(this, "wuxian", "shu", 3, false);
	wuxian->addSkill(new Fumian);
	wuxian->addSkill(new FumianTargetMod);
	wuxian->addSkill(new Daiyan);
	related_skills.insert("fumian", "#fumian-target");

	General *xinxianying = new General(this, "xinxianying", "wei", 3, false);
	xinxianying->addSkill(new Zhongjian);
    xinxianying->addSkill(new ZhongjianMax);
    related_skills.insert("zhongjian", "#zhongjian-max");
	xinxianying->addSkill(new Caishi);
	xinxianying->addSkill(new CaishiMax);
	xinxianying->addSkill(new CaishiPro);
	related_skills.insert("caishi", "#caishimax");
	related_skills.insert("caishi", "#caishipro");

	General *jikang = new General(this, "jikang", "wei", 3);
	jikang->addSkill(new Qingxian);
	jikang->addSkill(new Juexiang);
	jikang->addSkill(new JuexiangPro);
	jikang->addRelateSkill("jixian");
	jikang->addRelateSkill("liexian");
	jikang->addRelateSkill("rouxian");
	jikang->addRelateSkill("hexian");
	related_skills.insert("juexiang", "#juexiangpro");

	General *xushi = new General(this, "xushi", "wu", 3, false);
	xushi->addSkill(new Wengua);
	xushi->addSkill(new Fuzhu);

	General *xuezong = new General(this, "xuezong", "wu", 3);
	xuezong->addSkill(new Funan);
	xuezong->addSkill(new FunanRemove);
	xuezong->addSkill(new Jiexun);
	related_skills.insert("funan", "#funanremove");

	General *caojie = new General(this, "caojie", "qun", 3, false);
	caojie->addSkill(new Shouxi);
	caojie->addSkill(new Huimin);

	General *caiyong = new General(this, "caiyong", "qun", 3);
	caiyong->addSkill(new Bizhuan);
	caiyong->addSkill(new BizhuanKeep);
	caiyong->addSkill(new Tongbo);
	related_skills.insert("bizhuan", "#bizhuankeep");

	addMetaObject<FumianCard>();
	addMetaObject<ZhongjianCard>();
	addMetaObject<WenguagiveCard>();
	addMetaObject<WenguaCard>();
	addMetaObject<HuiminCard>();
	addMetaObject<TongboCard>();

	skills << new Jixian << new Liexian << new Rouxian << new Hexian << new Wenguagive << new MobileCanyun << new FunanLimit;
}
ADD_PACKAGE(YCZH2017)

OLStYC2017Package::OLStYC2017Package()
	: Package("OLStYC2017")
{
	General *ol_xinxianying = new General(this, "ol_xinxianying", "wei", 3, false);
	ol_xinxianying->addSkill(new OLZhongjian);
    ol_xinxianying->addSkill(new ZhongjianMax("#olzhongjian-max"));
    related_skills.insert("olzhongjian", "#olzhongjian-max");
	ol_xinxianying->addSkill(new OLCaishi);
	ol_xinxianying->addSkill(new OLCaishiMax);
	ol_xinxianying->addSkill(new OLCaishiPro);
	related_skills.insert("olcaishi", "#olcaishimax");
	related_skills.insert("olcaishi", "#olcaishipro");

	addMetaObject<OLZhongjianCard>();
}
ADD_PACKAGE(OLStYC2017)

MobileStYC2017Package::MobileStYC2017Package()
	: Package("MobileStYC2017")
{
	General *mobile_jikang = new General(this, "mobile_jikang", "wei", 3);
	mobile_jikang->addSkill(new MobileQingxian);
	mobile_jikang->addSkill(new MobileJuexiang);
	mobile_jikang->addRelateSkill("mobilecanyun");
    skills << new MobileCanyunRecord;
    related_skills.insert("mobilecanyun", "#mobilecanyun-record");

	addMetaObject<MobileQingxianCard>();
	addMetaObject<MobileCanyunCard>();
}
ADD_PACKAGE(MobileStYC2017)
