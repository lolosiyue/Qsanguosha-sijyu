#include "olwenwu.h"
#include "skill-declaration.h"
#include "skill-instance-utils.h"
//#include "skill.h"
//#include "standard.h"
#include "clientplayer.h"
#include "engine.h"
#include "settings.h"
//#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "yingbian.h"
#include "yjcm2013.h"
#include <limits>
#include <QScopeGuard>
#include <memory>

// PhaseChanging already belongs to the next phase; expire only the recorded, closed predecessor.
static bool finishedJinPlayPhase(Room *room, const QVariant &id, const ServerPlayer *actor)
{
    if (!actor || id.toLongLong() <= 0) return false;
    const QVariantMap phase = room->historyEvent(id.toLongLong());
    const QVariantMap value = phase.value("data").toMap();
    return phase.value("status").toString() == "finished" && phase.value("kind").toString() == "phase"
        && value.value("phase").toInt() == Player::Play && value.value("player").toString() == actor->objectName()
        && phase.value("turn_id") == room->historyScopes().value("turn_id");
}

// Build exact candidates before interception: waiving payment must not revive a spent quota.
static TriggerList usableJinCandidates(const Skill *skill, Room *room, TriggerEvent event,
    QVariant &data, const TriggerList &candidates)
{
    TriggerList result;
    for (auto it = candidates.constBegin(); it != candidates.constEnd(); ++it) {
        ServerPlayer *owner = it.key();
        if (!owner) continue;
        foreach (int id, owner->getValidSkillInstanceIds(skill->objectName())) {
            SkillContext ctx;
            ctx.owner = ctx.invoker = ctx.initiator = owner;
            ctx.skill_name = skill->objectName();
            ctx.instanceID = id;
            ctx.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(skill->objectName(), id));
            ctx.sourceRef = room->resolveSkillInstanceRootRef(ctx.activationRef);
            ctx.original_data = &data;
            ctx.current_event = event;
            if (!ctx.sourceRef.isValid() || !skill->isUsable(ctx)) continue;
            foreach (const QString &candidate, it.value()) {
                const int target = candidate.indexOf("->");
                result[owner] << SkillInstanceUtils::formatName(skill->objectName(), id)
                    + (target < 0 ? QString() : candidate.mid(target));
            }
        }
    }
    return result;
}

class JinBuchen : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    JinBuchen() : TriggerSkillV2("jinbuchen") { events << Appear; hide_skill = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        ServerPlayer *current = room->getCurrent();
        if (player && player->isAlive() && player->hasSkill(this) && current && current != player && !current->isNude())
            result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *current = room->getCurrent();
        if (!current || current == ctx.owner || current->isNude()
            || !ctx.invoker->askForSkillInvoke(this, current)) return false;
        ctx.targets << current;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        for (int i = 0; i < getEffectiveAmount(ctx) && !target->isNude() && ctx.invoker->isAlive(); ++i) {
            int id = room->askForCardChosen(ctx.invoker, target, "he", objectName());
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, ctx.invoker->objectName());
            room->obtainCard(ctx.invoker, Sanguosha->getCard(id), reason, room->getCardPlace(id) != Player::PlaceHand);
        }
        return false;
    }
};
JinYingshiCard::JinYingshiCard()
{
    setSkillName("jinyingshi");
	target_fixed = true;
}



class JinYingshi : public ViewAsSkillV2
{
public:
    JinYingshi() : ViewAsSkillV2("jinyingshi") { frequency = Compulsory; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getMaxHp() > 0;
    }
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "JinYingshiCard"; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        const int count = ctx.invoker->getMaxHp() * getEffectiveAmount(ctx);
        if (count <= 0) return ContinueEffects;
        ctx.invoker->peiyin(objectName());
        // Inspection preserves the exact deck order and reveals only to the invoker.
        const QList<int> cards = room->getNCards(count);
        room->returnToTopDrawPile(cards);
        room->fillAG(cards, target);
        try { room->askForAG(target, cards, true, objectName()); }
        catch (...) { room->clearAG(target); throw; }
        room->clearAG(target);
        return ContinueEffects;
    }
};
JinXiongzhiCard::JinXiongzhiCard() { setSkillName("jinxiongzhi"); target_fixed = true; }

class JinXiongzhi : public ViewAsSkillV2
{
public:
    JinXiongzhi() : ViewAsSkillV2("jinxiongzhi", 1)
    { response_pattern = "@@jinxiongzhi!"; frequency = Limited; limit_mark = "@jinxiongzhiMark"; expand_pile = "#jinxiongzhi"; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    TargetMode targetMode() const override { return NoTarget; }
    int responseId(const ActiveSkillRequest &request) const
    { return request.initiator ? request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "response_card", -1).toInt() : -1; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return !request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "accepted_view_as_effect").toBool();
        if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE || request.pattern != response_pattern || responseId(request) < 0) return false;
        const auto *server = dynamic_cast<const ServerPlayer *>(request.initiator);
        return !server || server->getRoom()->getCardPlace(responseId(request)) == Player::DrawPile;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.reason != CardUseStruct::CARD_USE_REASON_PLAY && request.selectedCardIds.isEmpty() && card && card->getEffectiveId() == responseId(request); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? request.selectedCardIds.isEmpty()
        : request.selectedCardIds.isEmpty() || (request.selectedCardIds.size() == 1 && request.selectedCardIds.first() == responseId(request)); }
    static Card *ordinary(int id)
    {
        const Card *material = Sanguosha->getCard(id); if (!material) return nullptr;
        Card *card = Sanguosha->cloneCard(material); if (!card) return nullptr;
        card->setId(-1); card->clearSubcards(); card->addSubcard(id); card->setSkillName("jinxiongzhi"); return card;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !cardSelectionFeasible(request)) return nullptr;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? ViewAsSkillV2::createCard(request) : ordinary(responseId(request));
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    { return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? "JinXiongzhiCard"
        : responseId(request) >= 0 ? Sanguosha->getCard(responseId(request))->getClassName() : QString(); }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.use_card || ctx.use_card->getTypeId() != Card::TypeSkill) return ContinueEffects;
        ctx.manual_effect = true;
        if (ctx.invoker) skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        Room *room = target->getRoom(); room->setPlayerMark(ctx.owner, limit_mark, 0);
        room->doSuperLightbox(target, objectName());
        // Every prompt is a fresh exact child of the accepted limited effect.
        while (target->isAlive()) {
            Room::AcceptedViewAsEffectScope scope(room, target, objectName(), ctx);
            if (!scope.isValid()) break;
            const QList<int> ids = room->getNCards(1); if (ids.isEmpty()) break;
            room->returnToTopDrawPile(ids); const int id = ids.first();
            LogMessage log; log.type = "$ViewDrawPile"; log.from = target; log.card_str = QString::number(id); room->sendLog(log, target);
            std::unique_ptr<Card> card(ordinary(id)); if (!card || !target->canUse(card.get())) break;
            const auto ref = scope.activationRef(); target->setSkillInstanceStateValue(objectName(), ref.key.instanceID, "response_card", id);
            const int previous = target->getMark("jinxiongzhi_id-PlayClear");
            const QString pattern = room->getRoomState()->getCurrentCardUsePattern();
            const auto reason = room->getRoomState()->getCurrentCardUseReason();
            const auto restore = qScopeGuard([&] { room->setPlayerMark(target, "jinxiongzhi_id-PlayClear", previous);
                room->notifyMoveToPile(target, ids, objectName(), Player::PlaceUnknown, false); room->setCurrentCardUse(pattern, reason); });
            room->setPlayerMark(target, "jinxiongzhi_id-PlayClear", id);
            room->notifyMoveToPile(target, ids, objectName(), Player::DrawPile, true);
            room->setCurrentCardUse(response_pattern, CardUseStruct::CARD_USE_REASON_RESPONSE_USE);
            bool used = false;
            if (card->targetFixed()) used = room->useCardFromSkillEffect(CardUseStruct(card.get(), target), ctx, true);
            else if (room->askForUseCard(target, response_pattern, "@jinxiongzhi:" + card->objectName())) used = true;
            else if (target->isAlive() && room->getCardPlace(id) == Player::DrawPile) {
                // Preserve mandatory use, including Collateral's native second target.
                for (ServerPlayer *first : room->getAlivePlayers()) {
                    QList<const Player *> selected;
                    if (!card->targetFilter(selected, first, target) || target->isProhibited(first, card.get(), selected)) continue;
                    selected << first;
                    if (card->targetsFeasible(selected, target)) { used = room->useCardFromSkillEffect(CardUseStruct(card.get(), target, first), ctx, true); break; }
                    for (ServerPlayer *second : room->getAlivePlayers()) {
                        if (second == first || !card->targetFilter(selected, second, target) || target->isProhibited(second, card.get(), selected)) continue;
                        QList<const Player *> pair = selected; pair << second;
                        if (!card->targetsFeasible(pair, target)) continue;
                        CardUseStruct use(card.get(), target); use.to << first << second;
                        used = room->useCardFromSkillEffect(use, ctx, true); break;
                    }
                    if (used) break;
                }
            }
            // Cancellation must not spin forever on the same unconsumed top card.
            if (!used || room->getCardPlace(id) == Player::DrawPile) break;
        }
        return ContinueEffects;
    }
};

class JinQuanbian : public TriggerSkillV2
{
public:
    JinQuanbian(const QString &name) : TriggerSkillV2(name)
    {
        events << CardUsed << CardResponded << EventPhaseStart << EventAcquireSkill;
        global = true;
    }
    int getPriority(TriggerEvent) const override { return 3; }
    bool usesEventPriority() const override { return true; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    static int normalizedSuit(int suit) { return suit == Card::NoSuitBlack || suit == Card::NoSuitRed ? Card::NoSuit : suit; }
    static QVariantMap history(Room *room, const Player *player, const QString &name)
    {
        QVariantMap result{{"complete", false}};
        const QVariant phase = room->historyScopes().value("phase_id");
        if (phase.toLongLong() <= 0) return result;
        QVariantMap first;
        int used = 0;
        QVariantMap filter{{"phase_id", phase}, {"player", player->objectName()}};
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (!page.value("complete").toBool()) return result;
            foreach (const QVariant &entry, page.value("items").toList()) {
                const QVariantMap fact = entry.toMap();
                const QString kind = fact.value("kind").toString();
                if (kind != "use_card" && kind != "respond_card") continue;
                const QVariantMap data = fact.value("data").toMap(), card = data.value("card").toMap();
                if (!card.contains("type")) return result;
                if (card.value("type").toInt() == Card::TypeSkill) continue;
                // Old response facts without the accepted hand-card origin cannot prove first-suit/count eligibility.
                if (!data.contains("is_handcard")) return result;
                if (!data.value("is_handcard").toBool()) continue;
                if (!card.contains("suit") || (kind == "respond_card" && !data.contains("is_use"))) return result;
                const QString suit = QString::number(normalizedSuit(card.value("suit").toInt()));
                if (!first.contains(suit)) first.insert(suit, fact.value("event_id"));
                if ((kind == "use_card" || data.value("is_use").toBool())
                    && (name != "secondjinquanbian" || card.value("type").toInt() != Card::TypeEquip)) ++used;
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("watermark", page.value("watermark"));
            filter.insert("after", page.value("next_after"));
        }
        return QVariantMap{{"complete", true}, {"used", used}, {"first", first}};
    }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player) return true;
        const QVariantMap facts = player->getPhase() == Player::Play ? history(room, player, objectName()) : QVariantMap();
        // Client legality reads this projection. Server legality queries the journal directly, including pre-acquisition uses.
        room->setPlayerMark(player, objectName() + "_used-PlayClear",
            player->getPhase() != Player::Play ? 0 : facts.value("complete").toBool() ? facts.value("used").toInt() : qMax(0, player->getMaxHp()));
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if ((event != CardUsed && event != CardResponded) || !player || !player->isAlive()
            || !player->hasSkill(this) || player->getPhase() != Player::Play || player->getMaxHp() <= 0) return result;
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
        const bool hand = event == CardUsed ? data.value<CardUseStruct>().m_isHandcard : data.value<CardResponseStruct>().m_isHandcard;
        if (!card || card->isKindOf("SkillCard") || !hand) return result;
        const qint64 current = room->historyParent(room->currentHistoryEventId(), event == CardUsed ? "use_card" : "respond_card", true).value("id").toLongLong();
        const QVariantMap facts = history(room, player, objectName());
        if (current > 0 && facts.value("complete").toBool()
            && facts.value("first").toMap().value(QString::number(normalizedSuit(card->getSuit()))).toLongLong() == current)
            result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker->askForSkillInvoke(this)) return false;
        const Card *card = event == CardUsed ? ctx.original_data->value<CardUseStruct>().card : ctx.original_data->value<CardResponseStruct>().m_card;
        if (!card) return false;
        ctx.extra_data = QVariantMap{{"suit", normalizedSuit(card->getSuit())}, {"count", ctx.invoker->getMaxHp()}};
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int count = ctx.extra_data.toMap().value("count").toInt() * getEffectiveAmount(ctx);
        if (count <= 0) return false;
        room->broadcastSkillInvoke(objectName());
        const QList<int> reserved = room->getNCards(count, false);
        QList<int> remaining = reserved, enabled, disabled;
        foreach (int id, reserved) {
            if (normalizedSuit(Sanguosha->getCard(id)->getSuit()) == ctx.extra_data.toMap().value("suit").toInt()) disabled << id;
            else enabled << id;
        }
        auto unclaimed = [&]() {
            QList<int> cards;
            foreach (int id, reserved)
                if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) cards << id;
            return cards;
        };
        try {
            room->fillAG(reserved, target, disabled);
            const int id = room->askForAG(target, enabled.isEmpty() ? reserved : enabled, enabled.isEmpty(), objectName());
            room->clearAG(target);
            if (!enabled.isEmpty() && enabled.contains(id) && room->getCardPlace(id) == Player::DrawPile
                && !room->getDrawPile().contains(id)) {
                room->obtainCard(target, id, true);
                remaining.removeOne(id);
            }
            remaining = unclaimed();
            if (target->isAlive()) room->askForGuanxing(target, remaining, Room::GuanxingUpOnly);
            else room->returnToTopDrawPile(remaining);
        } catch (...) {
            room->clearAG(target);
            const QList<int> cards = unclaimed();
            if (!cards.isEmpty()) room->returnToTopDrawPile(cards);
            throw;
        }
        return false;
    }
};

class JinQuanbianLimit : public CardLimitSkill
{
public:
    JinQuanbianLimit(const QString &name) : CardLimitSkill("#" + name + "-limit"), name(name) {}
    QString limitList(const Player *) const override { return "use"; }
    QString limitPattern(const Player *target) const override
    {
        if (target->getPhase() != Player::Play || !target->hasSkill(name)) return QString();
        int used = target->getMark(name + "_used-PlayClear");
        if (const ServerPlayer *server = qobject_cast<const ServerPlayer *>(target)) {
            const QVariantMap facts = JinQuanbian::history(server->getRoom(), target, name);
            used = facts.value("complete").toBool() ? facts.value("used").toInt() : target->getMaxHp();
        }
        if (used < target->getMaxHp()) return QString();
        return name == "secondjinquanbian" ? "^EquipCard|.|.|hand" : ".|.|.|hand";
    }
private:
    QString name;
};

class JinHuishi : public TriggerSkillV2
{
public:
    JinHuishi() : TriggerSkillV2("jinhuishi") { events << DrawNCards; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(this) && data.value<DrawStruct>().reason == "draw_phase")
            result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int count = room->getDrawPile().length() % 10;
        if (!ctx.invoker->askForSkillInvoke(this, QString("jinhuishi_invoke:%1").arg(count))) return false;
        ctx.extra_data = count;
        ctx.targets << ctx.owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        draw.num = 0;
        ctx.original_data->setValue(draw);
        room->broadcastSkillInvoke(objectName());
        const int count = ctx.extra_data.toInt() * getEffectiveAmount(ctx);
        if (count <= 0) return false;
        QList<int> cards;
        auto returnRemaining = [&](bool bottom) {
            QList<int> remaining;
            foreach (int id, cards)
                if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) remaining << id;
            if (bottom) room->returnToEndDrawPile(remaining);
            else room->returnToTopDrawPile(remaining);
        };
        try {
            for (int i = 0; i < count; ++i) cards << room->getNCards(1);
            const int obtainCount = cards.length() / 2;
            LogMessage log;
            log.type = "$ViewDrawPile";
            log.from = target;
            log.card_str = ListI2S(cards).join("+");
            room->sendLog(log, target);
            room->fillAG(cards, target);
            if (!obtainCount) {
                room->askForAG(target, cards, true, objectName());
                room->clearAG(target);
                returnRemaining(true);
                return false;
            }
            QList<int> enabled = cards, chosen;
            while (chosen.length() < obtainCount && target->isAlive() && !enabled.isEmpty()) {
                const int id = room->askForAG(target, enabled, false, objectName());
                if (!enabled.removeOne(id)) break;
                room->takeAG(target, id, false, QList<ServerPlayer *>() << target);
                chosen << id;
            }
            room->clearAG(target);
            if (target->isAlive() && !chosen.isEmpty()) {
                DummyCard selected(chosen);
                room->obtainCard(target, &selected, false);
            }
            returnRemaining(false);
        } catch (...) {
            // Interrupted inspection must not strand unclaimed cards outside the deck list.
            room->clearAG(target);
            returnRemaining(false);
            throw;
        }
        return false;
    }
};
JinQinglengCard::JinQinglengCard()
{
    setSkillName("jinqingleng");
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodUse;
}

class JinQingleng : public TriggerSkillV2
{
public:
    JinQingleng() : TriggerSkillV2("jinqingleng")
    {
        events << CardUsed << CardFinished << EventPhaseChanging;
        global = true;
    }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    static bool material(Room *room, ServerPlayer *actor, ServerPlayer *target, int id)
    {
        if (!actor || !target || !actor->isAlive() || !target->isAlive() || id < 0 || room->getCardOwner(id) != actor
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        const Card *card = Sanguosha->getCard(id);
        if (!card || card->hasFlag("using")) return false;
        IceSlash slash(card->getSuit(), card->getNumber());
        slash.setSkillName("jinqingleng");
        slash.addSubcard(card);
        return !actor->isCardLimited(&slash, Card::MethodUse) && actor->canSlash(target, &slash, false);
    }
    static int firstTargets(Room *room, const CardUseStruct &use, const QVariantMap &receipt)
    {
        if (use.useHistoryEventId <= 0 || !use.from) return -1;
        QVariantMap filter{{"kind", "use_card"}, {"from", use.from->objectName()}, {"limit", 100}};
        QSet<QString> seen;
        int count = -1;
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (!page.value("complete").toBool()) return -1;
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            foreach (const QVariant &entry, page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), value = fact.value("data").toMap();
                if (value.value("activation_owner") != receipt.value("activation_owner")
                    || value.value("activation_skill") != receipt.value("activation_skill")
                    || value.value("activation_instance_id") != receipt.value("activation_id")) continue;
                if (!value.value("attribution_complete").toBool()) return -1;
                int fresh = 0;
                foreach (const QVariant &name, value.value("targets").toList()) {
                    if (seen.contains(name.toString())) continue;
                    seen.insert(name.toString());
                    ++fresh;
                }
                if (fact.value("event_id").toLongLong() == use.useHistoryEventId) count = fresh;
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("after", page.value("next_after"));
        }
        return count;
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == CardFinished) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.card) use.card->removeTag("jinqingleng_effect");
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.from || !use.from->isAlive()) return false;
        const QVariantMap receipt = use.card->getTag("jinqingleng_effect").toMap();
        return receipt.value("receipt").toInt() == ctx.instanceID && ref(receipt, "source") == ctx.sourceRef
            && ref(receipt, "activation") == ref(ctx.extra_data.toMap(), "activation");
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx);
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != CardUsed) return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.from || !use.from->isAlive()) return true;
        QVariantMap receipt = use.card->getTag("jinqingleng_effect").toMap();
        if (!ref(receipt, "source").isValid() || !ref(receipt, "activation").isValid()) return true;
        const int fresh = firstTargets(room, use, receipt);
        if (fresh <= 0) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = use.from;
        ctx.sourceRef = ref(receipt, "source");
        ctx.instanceID = receipt.value("receipt").toInt();
        ctx.original_data = &data;
        ctx.current_event = event;
        ctx.extra_data = receipt;
        ctx.is_forced = true;
        ctx.setModifiedAmount(fresh * receipt.value("amount").toInt());
        contexts << ctx;
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != EventPhaseChanging || !player || !player->isAlive()
            || data.value<PhaseChangeStruct>().to != Player::NotActive
            || player->getHp() + player->getHandcardNum() < room->getDrawPile().length() % 10) return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player)) {
            if (!owner->hasSkill(this)) continue;
            foreach (const Card *card, owner->getCards("he"))
                if (material(room, owner, player, card->getEffectiveId())) { result[owner] << objectName() + "->" + player->objectName(); break; }
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardUsed) { ctx.targets << ctx.invoker; return true; }
        ServerPlayer *target = ctx.preferredTarget;
        if (!target || !target->isAlive()) return false;
        QList<int> choices;
        foreach (const Card *card, ctx.invoker->getCards("he"))
            if (material(room, ctx.invoker, target, card->getEffectiveId())) choices << card->getEffectiveId();
        if (choices.isEmpty()) return false;
        room->fillAG(choices, ctx.invoker);
        int id = -1;
        try { id = room->askForAG(ctx.invoker, choices, true, objectName(), "@jinqingleng:" + target->objectName()); }
        catch (...) { room->clearAG(ctx.invoker); throw; }
        room->clearAG(ctx.invoker);
        if (!choices.contains(id) || !material(room, ctx.invoker, target, id)) return false;
        ctx.extra_data = id;
        ctx.targets << target;
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        return event == CardUsed || (!ctx.targets.isEmpty() && material(room, ctx.invoker, ctx.targets.first(), ctx.extra_data.toInt()));
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (event == CardUsed) { target->drawCards(amount, objectName()); return false; }
        const int id = ctx.extra_data.toInt();
        if (!material(room, ctx.invoker, target, id)) return false;
        const Card *card = Sanguosha->getCard(id);
        IceSlash *slash = new IceSlash(card->getSuit(), card->getNumber());
        slash->addSubcard(card);
        slash->setSkillName(objectName());
        slash->deleteLater();
        const int sequence = room->getTag("jinqingleng_sequence").toInt() + 1;
        room->setTag("jinqingleng_sequence", sequence);
        slash->setTag("jinqingleng_effect", QVariantMap{{"receipt", sequence}, {"amount", amount},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}});
        room->broadcastSkillInvoke(objectName());
        room->useCardFromSkillEffect(CardUseStruct(slash, ctx.invoker, target), ctx, true);
        return false;
    }
};

class JinXuanmu : public TriggerSkillV2
{
public:
    JinXuanmu() : TriggerSkillV2("jinxuanmu")
    { global = true; events << Appear << DamageInflicted << EventPhaseChanging; frequency = Compulsory; hide_skill = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        QVariantList kept; for (const QVariant &value : room->getTag("jinxuanmu_effects").toList())
            if (value.toMap().value("turn") != room->historyScopes().value("turn_id")) kept << value;
        room->setTag("jinxuanmu_effects",kept); return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != DamageInflicted) return false;
        if (!actor || actor->isDead() || data.value<DamageStruct>().damage <= 0) return true;
        for (const QVariant &value : room->getTag("jinxuanmu_effects").toList()) {
            const QVariantMap row = value.toMap();
            if (row.value("target").toString() != actor->objectName() || row.value("turn") != room->historyScopes().value("turn_id")) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.owner = room->findPlayerByObjectName(row.value("owner").toString(),true);
            ctx.invoker = actor; ctx.instanceID = row.value("serial").toInt(); ctx.original_data = &data; ctx.current_event = event;
            ctx.sourceRef = SkillInstanceRef(row.value("source_owner").toString(),SkillInstanceKey(row.value("source_skill").toString(),row.value("source_id").toInt()));
            ctx.amount = row.value("amount").toInt(); ctx.extra_data = row; ctx.is_forced = true;
            if (ctx.owner && ctx.sourceRef.isValid()) contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room,const SkillContext &ctx) const override
    { return ctx.activationRef.isValid() ? TriggerSkillV2::isSourceAvailable(room,ctx) : room->getTag("jinxuanmu_effects").toList().contains(ctx.extra_data); }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    { return event == Appear && player && player->isAlive() && player->hasSkill(this) && room->getCurrent() != player
        ? TriggerList{{player,{objectName()}}} : TriggerList(); }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        ctx.manual_effect = true; ctx.choice.clear();
        skillEffect(event,room,actor,ctx,ctx.invoker);
        return event == DamageInflicted && ctx.choice == "prevented";
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        if (event == DamageInflicted) { ctx.choice = "prevented"; room->sendCompulsoryTriggerLog(target,this); return false; }
        const qint64 serial = room->getTag("jinxuanmu_sequence").toLongLong()+1; room->setTag("jinxuanmu_sequence",serial);
        QVariantList rows = room->getTag("jinxuanmu_effects").toList();
        rows << QVariantMap{{"serial",serial},{"turn",room->historyScopes().value("turn_id")},{"target",target->objectName()},
            {"owner",ctx.activationRef.ownerObjectName},{"activation_id",ctx.activationRef.key.instanceID},{"amount",amount},
            {"source_owner",ctx.sourceRef.ownerObjectName},{"source_skill",ctx.sourceRef.key.skillName},{"source_id",ctx.sourceRef.key.instanceID}};
        room->setTag("jinxuanmu_effects",rows); room->setPlayerMark(target,"&jinxuanmu-Clear",1); return false;
    }
};
class Qiaoyan : public TriggerSkillV2
{
public:
    Qiaoyan() : TriggerSkillV2("qiaoyan") { events << DamageCaused; frequency = Compulsory; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.from && damage.from->isAlive() && damage.to && damage.to->isAlive()
            && damage.to != damage.from && damage.to->hasSkill(this) && !damage.to->hasFlag("CurrentPlayer"))
            result[damage.to] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const bool prevent = ctx.owner->getPile("qyzhu").isEmpty();
        ctx.extra_data = prevent;
        ctx.targets << (prevent ? damage.to : damage.from);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        room->sendCompulsoryTriggerLog(ctx.invoker, objectName(), true, true);
        if (!ctx.extra_data.toBool()) {
            const QList<int> pearls = ctx.owner->getPile("qyzhu");
            if (!pearls.isEmpty()) {
                DummyCard cards(pearls);
                room->obtainCard(target, &cards);
            }
            return false;
        }
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (target != damage.to) return false;
        target->drawCards(amount, objectName());
        if (target->isAlive() && !target->isNude()) {
            const int count = qMin(amount, target->getCardCount());
            const Card *selected = room->askForExchange(target, objectName(), count, count, true, "@qiaoyan-put");
            QList<int> ids;
            if (selected) foreach (int id, selected->getSubcards()) {
                if (ids.contains(id) || room->getCardOwner(id) != target || Sanguosha->getCard(id)->hasFlag("using")
                    || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) continue;
                ids << id;
            }
            // The pearl pile is a public physical resource shared with Xianzhu, not a hidden quota.
            if (!ids.isEmpty()) target->addToPile("qyzhu", ids);
        }
        return true;
    }
};
class Xianzhu : public TriggerSkillV2
{
public:
    Xianzhu() : TriggerSkillV2("xianzhu") { events << EventPhaseStart; frequency = Compulsory; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Play
            && !player->getPile("qyzhu").isEmpty()) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.owner->getPile("qyzhu").isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, room->getAlivePlayers(), objectName(), "@xianzhu-invoke", false, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        const QList<int> pearls = ctx.owner->getPile("qyzhu");
        if (pearls.isEmpty()) return false;
        room->broadcastSkillInvoke(objectName());
        DummyCard card(pearls);
        room->obtainCard(target, &card);
        if (target == ctx.owner || !target->isAlive() || !ctx.invoker->isAlive()) return false;
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName("_xianzhu");
        // The forced ordinary use keeps this activation's provenance while its user is the recipient.
        CardUseStruct use(slash, target);
        use.setOwnedCard(slash);
        use.sourceRef = ctx.sourceRef;
        use.activationRef = ctx.activationRef;
        if (target->isLocked(slash)) return false;
        QList<ServerPlayer *> candidates;
        foreach (ServerPlayer *other, room->getAlivePlayers())
            if (ctx.invoker->inMyAttackRange(other) && target->canSlash(other, slash, false)) candidates << other;
        if (candidates.isEmpty()) return false;
        ServerPlayer *victim = room->askForPlayerChosen(ctx.invoker, candidates, "xianzhu_target", "@xianzhu-target");
        if (!victim || !victim->isAlive() || !target->isAlive() || !ctx.invoker->inMyAttackRange(victim)
            || !target->canSlash(victim, slash, false)) return false;
        use.to << victim;
        room->useCardFromSkillEffect(use, ctx);
        return false;
    }
};

class JinCaiwangVS : public ViewAsSkillV2
{
public:
    JinCaiwangVS(const QString &name) : ViewAsSkillV2(name) { response_or_use = true; }
    QString conversionName(const ActiveSkillRequest &request) const
    {
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return "slash";
        if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE
            && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return QString();
        if (request.pattern == "jink") return "jink";
        if (request.pattern == "nullification" && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE) return "nullification";
        if (request.pattern.contains("slash") || request.pattern.contains("Slash")) return "slash";
        return QString();
    }
    const Card *material(const Player *player, const QString &name) const
    {
        if (!player) return nullptr;
        QList<const Card *> cards;
        if (name == "slash") cards = player->getJudgingArea();
        else if (name == "jink") cards = player->getHandcards();
        else if (name == "nullification") cards = player->getEquips();
        return cards.length() == 1 && cards.first()->getEffectiveId() >= 0 && !cards.first()->hasFlag("using") ? cards.first() : nullptr;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const QString name = conversionName(request);
        const Card *selected = material(request.initiator, name);
        if (!selected) return false;
        Card *card = Sanguosha->cloneCard(name, selected->getSuit(), selected->getNumber());
        if (!card) return false;
        card->setSkillName(objectName());
        const bool valid = request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? card->isAvailable(request.initiator)
            : Sanguosha->matchPattern(request.pattern, request.initiator, card);
        delete card;
        return valid;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const QString name = conversionName(request);
        const Card *selected = material(request.initiator, name);
        if (!selected) return nullptr;
        // Freeze the sole physical identity in the authoritative rebuilt preview. A judging-area
        // material is bound at acceptance rather than exposed as a selectable hand/equipment card.
        Card *card = Sanguosha->cloneCard(name, selected->getSuit(), selected->getNumber());
        if (card) {
            card->setSkillName(objectName());
            card->setTag("jincaiwang_material", QVariantMap{{"name", name}, {"id", selected->getEffectiveId()}});
        }
        return card;
    }
    bool bindMaterial(Room *room, SkillContext &ctx, const QString &name, int id) const
    {
        const Card *selected = material(ctx.initiator, name);
        if (!selected || selected->getEffectiveId() != id || room->getCardOwner(id) != ctx.initiator) return false;
        const Player::Place zone = name == "slash" ? Player::PlaceDelayedTrick
            : name == "jink" ? Player::PlaceHand : Player::PlaceEquip;
        if (room->getCardPlace(id) != zone) return false;
        Card *card = Sanguosha->cloneCard(name, selected->getSuit(), selected->getNumber());
        if (!card) return false;
        card->addSubcard(id);
        card->setSkillName(objectName());
        card->setTag("jincaiwang_material", QVariantMap{{"name", name}, {"id", id}});
        card->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
        card->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
        card->deleteLater();
        ctx.updated_card = card;
        return true;
    }
    bool cost(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        const QString name = conversionName(request);
        const Card *selected = material(request.initiator, name);
        if (!selected) return false;
        QVariantMap state;
        state.insert("name", name);
        state.insert("id", selected->getEffectiveId());
        ctx.extra_data = state;
        // Bind normally during cost; accepted effect repeats the identity check if cost/payment were bypassed.
        return bindMaterial(room, ctx, name, selected->getEffectiveId());
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        const QVariantMap state = ctx.extra_data.toMap();
        const QString name = state.value("name").toString();
        return name == conversionName(request) && bindMaterial(room, ctx, name, state.value("id", -1).toInt());
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        const QVariantMap material = ctx.use_card ? ctx.use_card->getTag("jincaiwang_material").toMap() : QVariantMap();
        if (!ctx.initiator || !material.contains("id")
            || !bindMaterial(ctx.initiator->getRoom(), ctx, material.value("name").toString(), material.value("id").toInt()))
            return FinishSkill;
        return ContinueEffects;
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    {
        const QString name = conversionName(request);
        return name == "slash" ? "Slash" : name == "jink" ? "Jink" : "Nullification";
    }
};
class JinCaiwang : public TriggerSkillV2
{
public:
    JinCaiwang(const QString &name) : TriggerSkillV2(name)
    {
        events << CardResponded << CardUsed;
        view_as_skill = name == "jincaiwang" ? nullptr : new JinCaiwangVS(name);
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        const Card *card = nullptr, *responded = nullptr;
        ServerPlayer *other = nullptr;
        if (event == CardUsed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            card = use.card;
            responded = use.whocard;
            other = use.who;
        } else {
            const CardResponseStruct response = data.value<CardResponseStruct>();
            if (response.m_isRetrial) return result;
            card = response.m_card;
            responded = response.m_toCard;
            other = response.m_who;
        }
        if (!card || !responded || card->isKindOf("SkillCard") || responded->isKindOf("SkillCard")
            || !card->sameColorWith(responded) || !other || !other->isAlive() || other == player
            || room->getCardUser(responded) != other) return result;
        if (player->hasSkill(this) && !other->isNude()) result[player] << objectName() + "->" + other->objectName();
        if (other->hasSkill(this) && !player->isNude()) result[other] << objectName() + "->" + player->objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.preferredTarget;
        if (!target || !target->isAlive() || target->isNude()) return false;
        const bool obtain = target->getMark("&jinnaxiang+#" + ctx.owner->objectName()) > 0;
        if (!obtain && !ctx.invoker->canDiscard(target, "he")) return false;
        const QString prompt = (obtain ? "jincaiwang_get:" : "jincaiwang_discard:") + target->objectName();
        if (!ctx.invoker->askForSkillInvoke(this, prompt)) return false;
        ctx.extra_data = obtain;
        ctx.targets = QList<ServerPlayer *>() << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        const bool obtain = ctx.extra_data.toBool();
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive() && !target->isNude() && ctx.invoker->isAlive(); ++i) {
            if (!obtain && !ctx.invoker->canDiscard(target, "he")) break;
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false,
                obtain ? Card::MethodNone : Card::MethodDiscard);
            if (id < 0 || room->getCardOwner(id) != target
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) break;
            if (obtain) {
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, ctx.invoker->objectName());
                room->obtainCard(ctx.invoker, Sanguosha->getCard(id), reason, room->getCardPlace(id) != Player::PlaceHand);
            } else if (ctx.invoker->canDiscard(target, id)) room->throwCard(id, objectName(), target, ctx.invoker);
        }
        return false;
    }
};
class JinNaxiang : public TriggerSkillV2
{
public:
    JinNaxiang() : TriggerSkillV2("jinnaxiang")
    {
        events << Damage << Damaged;
        frequency = Compulsory;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!damage.from || !damage.to || damage.from == damage.to
            || !damage.from->isAlive() || !damage.to->isAlive()) return result;
        ServerPlayer *owner = event == Damage ? damage.to : damage.from;
        ServerPlayer *other = event == Damage ? damage.from : damage.to;
        if (owner->hasSkill(this)) result[owner] << objectName() + "->" + other->objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.preferredTarget || !ctx.preferredTarget->isAlive()) return false;
        ctx.targets = QList<ServerPlayer *>() << ctx.preferredTarget;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        room->sendCompulsoryTriggerLog(ctx.invoker, objectName(), true, true);
        // This applied relationship lasts until the holder's next turn even if its grant disappears.
        room->setPlayerMark(target, "&jinnaxiang+#" + ctx.owner->objectName(), 1);
        return false;
    }
};

class JinNaxiangClear : public TriggerSkillV2
{
public:
    JinNaxiangClear() : TriggerSkillV2("#jinnaxiang-clear")
    {
        events << EventPhaseStart;
        global = true;
    }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || player->getPhase() != Player::RoundStart) return true;
        foreach (ServerPlayer *target, room->getAllPlayers(true))
            room->setPlayerMark(target, "&jinnaxiang+#" + player->objectName(), 0);
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

ChexuanCard::ChexuanCard()
{
    setSkillName("chexuan");
    target_fixed = true;
}

class ChexuanVS : public ViewAsSkillV2
{
public:
    ChexuanVS() : ViewAsSkillV2("chexuan", 1) {}
    TargetMode targetMode() const override { return NoTarget; }
    QString historyKey(const ActiveSkillRequest &) const override { return "ChexuanCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->hasTreasureArea() && !request.initiator->getTreasure();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && request.selectedCardIds.isEmpty() && candidate->isBlack()
            && candidate->getEffectiveId() >= 0 && !candidate->hasFlag("using")
            && request.initiator->getCards("he").contains(candidate) && !request.initiator->isJilei(candidate);
    }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        ctx.targets = QList<ServerPlayer *>() << ctx.invoker;
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || request.selectedCardIds.length() != 1) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        if (!canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()))
            || !ctx.invoker->canDiscard(ctx.invoker, request.selectedCardIds.first())) return false;
        return ViewAsSkillV2::pay(room, ctx, request);
    }
    static QList<int> available(ServerPlayer *player)
    {
        QList<int> ids;
        foreach (const QString &name, QStringList() << "_sichengliangyu" << "_tiejixuanyu" << "_feilunzhanyu") {
            const int id = player->getDerivativeCard(name, Player::PlaceTable);
            if (id >= 0 && !ids.contains(id)) ids << id;
        }
        return ids;
    }
    static void equip(Room *room, ServerPlayer *player, int id)
    {
        if (!player->isAlive() || !player->hasTreasureArea() || player->getTreasure() || !available(player).contains(id)) return;
        // Recheck the derivative's physical reservation immediately before moving it.
        CardMoveReason reason(CardMoveReason::S_REASON_PUT, "chexuan");
        CardsMoveStruct move(id, nullptr, player, Player::PlaceTable, Player::PlaceEquip, reason);
        room->moveCardsAtomic(move, true);
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0 || !target->hasTreasureArea() || target->getTreasure()) return ContinueEffects;
        Room *room = target->getRoom();
        const QList<int> ids = available(target);
        if (ids.isEmpty()) return ContinueEffects;
        room->fillAG(ids, ctx.invoker);
        int id = -1;
        try { id = room->askForAG(ctx.invoker, ids, false, objectName()); }
        catch (...) { room->clearAG(ctx.invoker); throw; }
        room->clearAG(ctx.invoker);
        if (ids.contains(id)) equip(room, target, id);
        return ContinueEffects;
    }
};

class Chexuan : public TriggerSkillV2
{
public:
    Chexuan() : TriggerSkillV2("chexuan")
    {
        events << CardsMoveOneTime;
        view_as_skill = new ChexuanVS;
        waked_skills = "_sichengliangyu,_tiejixuanyu,_feilunzhanyu";
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(this)) return result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player || move.reason.m_reason == CardMoveReason::S_REASON_CHANGE_EQUIP) return result;
        for (int i = 0; i < move.card_ids.length(); ++i)
            if (move.from_places.value(i) == Player::PlaceEquip && Sanguosha->getCard(move.card_ids.at(i))->isKindOf("Treasure"))
                result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker->askForSkillInvoke(this)) return false;
        ctx.targets = QList<ServerPlayer *>() << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        room->broadcastSkillInvoke(objectName());
        JudgeStruct judge;
        judge.who = target;
        judge.reason = objectName();
        judge.good = true;
        judge.pattern = ".|black";
        room->judge(judge);
        if (judge.isGood() && target->isAlive()) {
            const QList<int> ids = ChexuanVS::available(target);
            if (!ids.isEmpty()) ChexuanVS::equip(room, target, ids.at(qsanRandomBounded(ids.length())));
        }
        return false;
    }
};

class Qiangshou : public DistanceSkillV2
{
public:
    Qiangshou() : DistanceSkillV2("qiangshou") { setBaseAmount(-1); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.holder->getTreasure() ? CorrectSkillResult::useAmount(ctx.currentAmount)
            : CorrectSkillResult::noEffect();
    }
};
CaozhaoCard::CaozhaoCard()
{
    setSkillName("caozhao");
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

class Caozhao : public ViewAsSkillV2
{
public:
    Caozhao() : ViewAsSkillV2("caozhao", 1) {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "CaozhaoCard"; }
    TargetMode targetMode() const override { return NoTarget; }
    bool willThrowSelectedCards() const override { return false; }
    SkillDialogInfo getDialogInfo() const override
    {
        SkillDialogInfo info = SkillDialogInfo::named("caozhao", objectName());
        info.parameters.insert("declarationType", "guhuo");
        // This declares a future identity, so Jink and currently unavailable tricks remain candidates.
        info.parameters.insert("checkAvailability", false);
        info.parameters.insert("checkLocked", false);
        return info;
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !request.initiator->isKongcheng();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && request.selectedCardIds.isEmpty() && candidate->getEffectiveId() >= 0
            && !candidate->hasFlag("using") && request.initiator->handCards().contains(candidate->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.length() != 1) return false;
        ActiveSkillRequest empty = request;
        empty.selectedCardIds.clear();
        return canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        SkillDeclarationSession declaration(getDialogInfo(), const_cast<Player *>(request.initiator), request.reason,
            request.pattern, Sanguosha->getBanPackages(), 0, objectName(), request.activationRef);
        const SkillDeclarationValidation choice = declaration.validate(request.userString);
        if (!choice.accepted) return nullptr;
        ActiveSkillCard *card = new ActiveSkillCard;
        card->setActiveSkill(this);
        card->setSkillName(objectName());
        card->addSubcards(request.selectedCardIds);
        card->setUserString(choice.canonicalValue);
        return card;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) && room->getCardOwner(request.selectedCardIds.first()) == ctx.invoker
            && room->getCardPlace(request.selectedCardIds.first()) == Player::PlaceHand;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0 || !ctx.use_card) return ContinueEffects;
        const SkillCard *proxy = qobject_cast<const SkillCard *>(ctx.use_card);
        if (!proxy || proxy->getSubcards().length() != 1) return ContinueEffects;
        const int id = proxy->getSubcards().first();
        const QString name = proxy->getUserString();
        const QVariantMap state = ctx.extra_data.toMap();
        if (state.value("transform").toBool()) {
            if (room->getCardOwner(id) != ctx.invoker || room->getCardPlace(id) != Player::PlaceHand
                || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
            if (target != ctx.invoker) room->giveCard(ctx.invoker, target, Sanguosha->getCard(id), objectName(), true);
            if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand) return ContinueEffects;
            const Card *original = Sanguosha->getCard(id);
            Card *view = Sanguosha->cloneCard(name, original->getSuit(), original->getNumber());
            if (!view) return ContinueEffects;
            view->setSkillName(objectName());
            view->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
            view->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
            WrappedCard *wrapped = Sanguosha->getWrappedCard(id);
            wrapped->takeOver(view);
            if (wrapped->getRealCard() == view && room->setPhysicalCardEffectSource(id, ctx))
                room->notifyUpdateCard(target, id, wrapped);
            return ContinueEffects;
        }
        if (state.value("choose").toBool()) {
            const Card *original = Sanguosha->getEngineCard(id);
            const QString transform = "view=" + original->objectName() + "=" + name;
            if (room->askForChoice(target, objectName(), transform + "+losehp", QVariant::fromValue(ctx.invoker)) == "losehp")
                room->loseHp(HpLostStruct(target, amount, objectName(), ctx.invoker));
            else if (ctx.invoker->isAlive()) {
                ServerPlayer *recipient = room->askForPlayerChosen(ctx.invoker, room->getOtherPlayers(ctx.invoker), "caozhao_give", "@caozhao-give", true);
                SkillContext conversion = ctx;
                conversion.extra_data = QVariantMap{{"transform", true}};
                skillEffect(conversion, recipient ? recipient : ctx.invoker);
            }
            return ContinueEffects;
        }
        if (target != ctx.invoker || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand
            || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        const SkillInstanceRef usage = getUsageRef(ctx);
        ServerPlayer *holder = room->findPlayerByObjectName(usage.ownerObjectName, true);
        if (holder && holder->hasSkillInstance(usage.key.skillName, usage.key.instanceID)) {
            QStringList names = holder->getSkillInstanceStateValue(usage.key.skillName, usage.key.instanceID, "declared_names").toStringList();
            if (!names.contains(name)) names << name;
            holder->setSkillInstanceStateValue(usage.key.skillName, usage.key.instanceID, "declared_names", names);
        }
        room->showCard(target, id);
        LogMessage log;
        log.type = "#ShouxiChoice";
        log.from = target;
        log.arg = name;
        room->sendLog(log);
        QList<ServerPlayer *> candidates;
        foreach (ServerPlayer *other, room->getAlivePlayers()) if (other->getHp() <= target->getHp()) candidates << other;
        if (candidates.isEmpty()) return ContinueEffects;
        ServerPlayer *chooser = room->askForPlayerChosen(target, candidates, objectName(), "@caozhao-target");
        if (chooser) {
            SkillContext choice = ctx;
            choice.extra_data = QVariantMap{{"choose", true}};
            skillEffect(choice, chooser);
        }
        return ContinueEffects;
    }
protected:
    bool allowDeclaration(const ActiveSkillRequest &request, const QString &name) const override
    {
        const auto &ref = request.activationRef;
        return request.initiator && ref.isValid() && ref.ownerObjectName == request.initiator->objectName()
            && name != "normal_slash" && !request.initiator->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "declared_names").toStringList().contains(name);
    }
};

class OLXibing : public TriggerSkillV2
{
public:
    OLXibing() : TriggerSkillV2("olxibing") { global = true; events << DamageInflicted << EventPhaseChanging; m_baseAmount = 2; }
    static void project(Room *room)
    {
        for (ServerPlayer *player : room->getAllPlayers(true)) {
            QStringList names;
            for (const QVariant &value : room->getTag("olxibing_pairs").toList()) {
                const QVariantMap row = value.toMap();
                if (row.value("from").toString() == player->objectName() && !names.contains(row.value("to").toString())) names << row.value("to").toString();
            }
            room->setPlayerProperty(player,"olxibing_targets",names);
        }
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return false;
        QVariantList kept; for (const QVariant &value : room->getTag("olxibing_pairs").toList())
            if (value.toMap().value("turn") != room->historyScopes().value("turn_id")) kept << value;
        room->setTag("olxibing_pairs",kept); project(room); return false;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result; if (event != DamageInflicted) return result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (player && player->isAlive() && player->hasSkill(this) && damage.to == player && damage.from
            && damage.from->isAlive() && damage.from != player
            && (player->canDiscard(player, "he") || player->canDiscard(damage.from, "he"))) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.from || !damage.from->isAlive()) return false;
        QStringList choices;
        if (ctx.invoker->canDiscard(damage.from, "he")) choices << "discard=" + damage.from->objectName();
        if (ctx.invoker->canDiscard(ctx.invoker, "he")) choices << "discard_self";
        if (choices.isEmpty() || !ctx.invoker->askForSkillInvoke(this, *ctx.original_data)) return false;
        const QString choice = room->askForChoice(ctx.invoker, objectName(), choices.join("+"), *ctx.original_data);
        if (!choices.contains(choice)) return false;
        ctx.targets = QList<ServerPlayer *>() << (choice == "discard_self" ? ctx.invoker : damage.from);
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        const QString stage = ctx.choice;
        if (stage == "protect") {
            ctx.extra_data = true;
            return false;
        }
        if (stage == "draw") {
            const QString victim = ctx.extra_data.toString();
            if (!victim.isEmpty()) {
                QVariantList rows = room->getTag("olxibing_pairs").toList();
                rows << QVariantMap{{"from",target->objectName()},{"to",victim},{"turn",room->historyScopes().value("turn_id")},
                    {"source_owner",ctx.sourceRef.ownerObjectName},{"source_skill",ctx.sourceRef.key.skillName},{"source_id",ctx.sourceRef.key.instanceID},
                    {"activation_owner",ctx.activationRef.ownerObjectName},{"activation_id",ctx.activationRef.key.instanceID}};
                room->setTag("olxibing_pairs",rows);
            }
            const auto sync = qScopeGuard([&] { project(room); });
            target->drawCards(amount, objectName());
            return false;
        }
        room->broadcastSkillInvoke(this);
        QList<int> ids;
        for (int i = 0; i < amount && ctx.invoker->isAlive() && target->isAlive() && target->getCardCount() > ids.length(); ++i) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard, ids);
            if (id < 0 || ids.contains(id) || room->getCardOwner(id) != target || !ctx.invoker->canDiscard(target, id)
                || Sanguosha->getCard(id)->hasFlag("using")
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) break;
            ids << id;
        }
        QList<int> valid;
        foreach (int id, ids)
            if (room->getCardOwner(id) == target && ctx.invoker->canDiscard(target, id) && !Sanguosha->getCard(id)->hasFlag("using")
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) valid << id;
        if (!valid.isEmpty()) { DummyCard discarded(valid); room->throwCard(&discarded, target, ctx.invoker); }
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!ctx.invoker->isAlive() || !damage.from || !damage.from->isAlive()) return false;
        const int selfHand = ctx.invoker->getHandcardNum(), otherHand = damage.from->getHandcardNum();
        if (selfHand == otherHand) return false;
        // These public turn receipts survive losing the original grant; the prohibit helper consumes them.
        SkillContext protect = ctx;
        protect.choice = "protect"; protect.extra_data = false;
        protect.targets = QList<ServerPlayer *>() << ctx.invoker;
        skillEffect(event, room, owner, protect, ctx.invoker);
        ServerPlayer *drawer = selfHand < otherHand ? ctx.invoker : damage.from;
        SkillContext draw = ctx;
        draw.choice = "draw"; draw.extra_data = protect.extra_data.toBool() ? ctx.invoker->objectName() : QString();
        draw.targets = QList<ServerPlayer *>() << drawer;
        skillEffect(event, room, owner, draw, drawer);
        return false;
    }
};

class OLXibingPro : public ProhibitSkill
{
public:
	OLXibingPro() : ProhibitSkill("#olxibing-pro")
	{
	}

	bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
	{
		return from && to && card && !card->isKindOf("SkillCard")
            && from->property("olxibing_targets").toStringList().contains(to->objectName());
	}
};

LiPackage::LiPackage()
	: Package("li")
{
	new General(this, "yinni_hide", "jin", 1, true, true, true);

	General *jin_simayi = new General(this, "jin_simayi", "jin", 3);
	jin_simayi->addSkill(new JinBuchen);
	jin_simayi->addSkill(new JinYingshi);
	jin_simayi->addSkill(new JinXiongzhi);
	jin_simayi->addSkill(new JinQuanbian("jinquanbian"));
	jin_simayi->addSkill(new JinQuanbianLimit("jinquanbian"));
	related_skills.insert("jinquanbian", "#jinquanbian-limit");

	General *second_jin_simayi = new General(this, "second_jin_simayi", "jin", 3);
	second_jin_simayi->addSkill("jinbuchen");
	second_jin_simayi->addSkill("jinyingshi");
	second_jin_simayi->addSkill("jinxiongzhi");
	second_jin_simayi->addSkill(new JinQuanbian("secondjinquanbian"));
	second_jin_simayi->addSkill(new JinQuanbianLimit("secondjinquanbian"));
	related_skills.insert("secondjinquanbian", "#secondjinquanbian-limit");

	General *jin_zhangchunhua = new General(this, "jin_zhangchunhua", "jin", 3, false);
	jin_zhangchunhua->addSkill(new JinHuishi);
	jin_zhangchunhua->addSkill(new JinQingleng);
	jin_zhangchunhua->addSkill(new JinXuanmu);

	General *ol_lisu = new General(this, "ol_lisu", "qun", 3);
	ol_lisu->addSkill(new Qiaoyan);
	ol_lisu->addSkill(new Xianzhu);

	General *jin_simazhou = new General(this, "jin_simazhou", "jin", 4);
	jin_simazhou->addSkill(new JinCaiwang("jincaiwang"));
	jin_simazhou->addSkill(new JinNaxiang);
	jin_simazhou->addSkill(new JinNaxiangClear);
	related_skills.insert("jinnaxiang", "#jinnaxiang-clear");

	General *second_jin_simazhou = new General(this, "second_jin_simazhou", "jin", 4);
	second_jin_simazhou->addSkill(new JinCaiwang("secondjincaiwang"));
	second_jin_simazhou->addSkill("jinnaxiang");

	General *cheliji = new General(this, "cheliji", "qun", 4);
	cheliji->addSkill(new Chexuan);
	cheliji->addSkill(new Qiangshou);
	cheliji->addRelateSkill("_sichengliangyu");
	cheliji->addRelateSkill("_tiejixuanyu");
	cheliji->addRelateSkill("_feilunzhanyu");

	General *ol_huaxin = new General(this, "ol_huaxin", "wei", 3);
	ol_huaxin->addSkill(new Caozhao);
	ol_huaxin->addSkill(new OLXibing);
	ol_huaxin->addSkill(new OLXibingPro);
	related_skills.insert("olxibing", "#olxibing-pro");

	addMetaObject<JinYingshiCard>();
	addMetaObject<JinXiongzhiCard>();
	addMetaObject<JinQinglengCard>();
	addMetaObject<ChexuanCard>();
	addMetaObject<CaozhaoCard>();
}
ADD_PACKAGE(Li)


class JinXijue : public TriggerSkillV2
{
public:
    JinXijue() : TriggerSkillV2("jinxijue")
    {
        events << GameStart << EventPhaseChanging << DrawNCards << EventPhaseStart;
    }
    int getPriority(TriggerEvent event) const override
    {
        return event == DrawNCards ? 1 : TriggerSkillV2::getPriority(event);
    }
    static int turnDamage(Room *room, ServerPlayer *owner)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return -1;
        QVariantMap filter{{"turn_id", turn}, {"from", owner->objectName()}};
        int amount = 0;
        for (;;) {
            const QVariantMap page = room->queryActualDamage(filter);
            if (!page.value("complete").toBool()) return -1;
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            foreach (const QVariant &entry, page.value("items").toList()) amount += entry.toMap().value("data").toMap().value("amount").toInt();
            if (!page.value("has_more").toBool()) return amount;
            filter.insert("after", page.value("next_after"));
        }
    }
    static QList<ServerPlayer *> stealTargets(Room *room, ServerPlayer *owner)
    {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *other, room->getOtherPlayers(owner))
            if (!other->isKongcheng() && other->getHandcardNum() >= owner->getHandcardNum()) targets << other;
        return targets;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        if (ctx.current_event == EventPhaseStart && ctx.invoker)
            ctx.extra_data = QVariantMap{{"actor", ctx.invoker->objectName()}};
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive()) return result;
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Finish) return result;
            foreach (ServerPlayer *owner, room->getOtherPlayers(player)) {
                if (!owner->hasSkill(this) || owner->getMark("&jxjjue") <= 0) continue;
                foreach (const Card *card, owner->getHandcards())
                    if (card->getTypeId() == Card::TypeBasic && !card->hasFlag("using") && owner->canDiscard(owner, card->getEffectiveId())) {
                        result[owner] << objectName(); break;
                    }
            }
        } else if (player->hasSkill(this)) {
            if (event == GameStart || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive && turnDamage(room, player) > 0))
                result[player] << objectName();
            else if (event == DrawNCards && player->getMark("&jxjjue") > 0 && data.value<DrawStruct>().reason == "draw_phase"
                && data.value<DrawStruct>().num > 0 && !stealTargets(room, player).isEmpty()) result[player] << objectName();
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == GameStart || event == EventPhaseChanging) {
            const int gain = event == GameStart ? 4 : turnDamage(room, ctx.owner);
            if (gain <= 0) return false;
            ctx.extra_data = QVariantMap{{"gain", gain}};
            ctx.targets << ctx.owner;
            return true;
        }
        if (ctx.owner->getMark("&jxjjue") <= 0) return false;
        if (event == DrawNCards) {
            const QList<ServerPlayer *> candidates = stealTargets(room, ctx.owner);
            const int count = qMin(int(candidates.length()), ctx.original_data->value<DrawStruct>().num);
            if (count <= 0) return false;
            ctx.targets = room->askForPlayersChosen(ctx.owner, candidates, "tuxi", 0, count, "@tuxi-card:::" + QString::number(count), true);
            ctx.extra_data = QVariantMap{{"steal", true}};
            return !ctx.targets.isEmpty();
        }
        ServerPlayer *actor = room->findPlayerByObjectName(ctx.extra_data.toMap().value("actor").toString());
        if (!actor || !actor->isAlive() || actor == ctx.owner || actor->getPhase() != Player::Finish) return false;
        const Card *card = room->askForCard(ctx.owner, ".Basic", "@xiaoguo", *ctx.original_data, Card::MethodNone);
        if (!card || card->getEffectiveId() < 0 || !ctx.owner->getHandcards().contains(card)
            || card->getTypeId() != Card::TypeBasic || card->hasFlag("using") || !ctx.owner->canDiscard(ctx.owner, card->getEffectiveId())) return false;
        ctx.extra_data = QVariantMap{{"card", card->getEffectiveId()}};
        ctx.targets << actor;
        return true;
    }
    bool pay(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == GameStart || event == EventPhaseChanging) return true;
        if (ctx.owner->getMark("&jxjjue") <= 0) return false;
        const int id = ctx.extra_data.toMap().value("card", -1).toInt();
        if (event == EventPhaseStart) {
            const Card *card = Sanguosha->getCard(id);
            if (!card || room->getCardOwner(id) != ctx.owner || room->getCardPlace(id) != Player::PlaceHand
                || card->getTypeId() != Card::TypeBasic || card->hasFlag("using") || !ctx.owner->canDiscard(ctx.owner, id)) return false;
        }
        // Jue is a visible spendable resource. Payment precedes every resulting card move.
        ctx.owner->loseMark("&jxjjue");
        if (id >= 0) room->throwCard(Sanguosha->getCard(id), ctx.owner, nullptr);
        return true;
    }
    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == DrawNCards && getEffectiveAmount(ctx) > 0) {
            DrawStruct draw = ctx.original_data->value<DrawStruct>();
            draw.num = qMax(0, draw.num - ctx.targets.length());
            ctx.original_data->setValue(draw);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        const QVariantMap state = ctx.extra_data.toMap();
        if (state.value("draw").toBool()) { target->drawCards(amount, objectName()); return false; }
        if (state.contains("gain")) { target->gainMark("&jxjjue", state.value("gain").toInt() * amount); return false; }
        if (state.contains("obtain")) {
            const int id = state.value("obtain").toInt();
            ServerPlayer *victim = room->findPlayerByObjectName(state.value("victim").toString());
            if (victim && room->getCardOwner(id) == victim && room->getCardPlace(id) == Player::PlaceHand && !Sanguosha->getCard(id)->hasFlag("using"))
                room->obtainCard(target, id, false);
            return false;
        }
        if (event == DrawNCards) {
            for (int i = 0; i < amount && ctx.owner->isAlive() && target->isAlive() && !target->isKongcheng(); ++i) {
                const int id = room->askForCardChosen(ctx.owner, target, "h", "tuxi");
                if (id < 0) break;
                SkillContext obtain = ctx;
                obtain.extra_data = QVariantMap{{"obtain", id}, {"victim", target->objectName()}};
                skillEffect(event, room, ctx.owner, obtain, ctx.owner);
            }
        } else if (!room->askForCard(target, ".Equip", "@xiaoguo-discard", QVariant()))
            room->damage(DamageStruct(objectName(), ctx.owner, target, amount));
        else if (ctx.owner->isAlive()) {
            SkillContext draw = ctx;
            draw.extra_data = QVariantMap{{"draw", true}};
            skillEffect(event, room, ctx.owner, draw, ctx.owner);
        }
        return false;
    }
};

class JinBaoQie : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    JinBaoQie() : TriggerSkillV2("jinbaoqie") { events << Appear; frequency = Compulsory; hide_skill = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(this)) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.invoker, objectName(), true, true);
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
            QList<int> cards = room->getDrawPile() + room->getDiscardPile();
            qsanShuffle(cards);
            foreach (int id, cards) {
                const Card *card = Sanguosha->getCard(id);
                if (!card->isKindOf("Treasure")) continue;
                room->obtainCard(target, id);
                if (target->isAlive() && target->handCards().contains(id) && card->isAvailable(target)
                    && target->askForSkillInvoke(this, "jinbaoqie_use:" + card->objectName(), false))
                    room->useCardFromSkillEffect(CardUseStruct(card, target), ctx);
                break;
            }
        }
        return false;
    }
};
JinYishiCard::JinYishiCard()
{
    setSkillName("jinyishi");
	target_fixed = true;
	will_throw = false;
	handling_method = Card::MethodNone;
}

class JinYishi : public TriggerSkillV2
{
public:
    JinYishi() : TriggerSkillV2("jinyishi") { events << CardsMoveOneTime << EventSkillInvoking; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    static QList<int> available(Room *room, const CardsMoveOneTimeStruct &move)
    {
        QList<int> ids;
        if (!move.from || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD) return ids;
        for (int i = 0; i < move.card_ids.length(); ++i)
            if (move.from_places.value(i) == Player::PlaceHand && room->getCardPlace(move.card_ids.at(i)) == Player::DiscardPile)
                ids << move.card_ids.at(i);
        return ids;
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext active = ctx.original_data->value<SkillContext>();
        if (active.bypass_cost && active.activationRef == ctx.activationRef) addUsage(active);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != CardsMoveOneTime || !player || !player->isAlive() || !player->hasSkill(this) || !player->hasTurn()) return result;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        // Movement dispatch already enumerates players; only this event's owner is a candidate.
        if (move.from && move.from != player && move.from->isAlive() && move.from->getPhase() == Player::Play
            && !available(room, move).isEmpty()) result[player] << objectName();
        return usableJinCandidates(this, room, event, data, result);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const CardsMoveOneTimeStruct move = ctx.original_data->value<CardsMoveOneTimeStruct>();
        const QList<int> ids = available(room, move);
        ServerPlayer *target = move.from ? room->findPlayerByObjectName(move.from->objectName()) : nullptr;
        if (!target || !target->isAlive() || ids.isEmpty()) return false;
        room->fillAG(ids, ctx.invoker);
        int id = -1;
        try { id = room->askForAG(ctx.invoker, ids, true, objectName()); }
        catch (...) { room->clearAG(ctx.invoker); throw; }
        room->clearAG(ctx.invoker);
        if (!ids.contains(id) || room->getCardPlace(id) != Player::DiscardPile) return false;
        ctx.extra_data = QVariantMap{{"chosen", id}, {"cards", ListI2V(ids)}};
        ctx.targets << target;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || !ctx.extra_data.toMap().contains("chosen")
            || room->getCardPlace(ctx.extra_data.toMap().value("chosen").toInt()) != Player::DiscardPile) return false;
        addUsage(ctx);
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        const QVariantMap state = ctx.extra_data.toMap();
        if (state.value("collect").toBool()) {
            QList<int> ids;
            foreach (int id, ListV2I(state.value("cards").toList()))
                if (id != state.value("chosen").toInt() && room->getCardPlace(id) == Player::DiscardPile) ids << id;
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target, &cards); }
            return false;
        }
        if (!state.contains("chosen") || room->getCardPlace(state.value("chosen").toInt()) != Player::DiscardPile) return false;
        room->broadcastSkillInvoke(objectName());
        room->obtainCard(target, state.value("chosen").toInt());
        // Returning the selected card and collecting the remainder have different recipients.
        if (ctx.invoker->isAlive()) {
            SkillContext collect = ctx;
            QVariantMap remaining = state;
            remaining.insert("collect", true);
            collect.extra_data = remaining;
            skillEffect(event, room, owner, collect, ctx.invoker);
        }
        return false;
    }
};

JinShiduCard::JinShiduCard()
{
    setSkillName("jinshidu");
	will_throw = false;
	handling_method = Card::MethodPindian;
}

bool JinShiduCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	return targets.isEmpty() && Self->canPindian(to_select);
}


class JinShidu : public ViewAsSkillV2
{
public:
    JinShidu() : ViewAsSkillV2("jinshidu") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->canPindian();
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "JinShiduCard"; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    {
        return selected.isEmpty() && request.initiator && candidate && request.initiator->canPindian(candidate);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.length() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom(); const int amount = getEffectiveAmount(ctx);
        if (amount <= 0 || !ctx.invoker || ctx.invoker->isDead()) return ContinueEffects;
        if (ctx.choice == "receive") {
            const QVariantMap row = ctx.extra_data.toMap(); ServerPlayer *from = room->findPlayerByObjectName(row.value("from").toString());
            if (!from || from->isDead()) return ContinueEffects;
            QList<int> ids; for (const QVariant &value : row.value("ids").toList()) {
                const int id = value.toInt(); if (room->getCardOwner(id) == from && room->getCardPlace(id) == Player::PlaceHand
                    && !Sanguosha->getCard(id)->hasFlag("using")) ids << id;
            }
            if (!ids.isEmpty()) { DummyCard cards(ids); room->obtainCard(target,&cards,false); }
            return ContinueEffects;
        }
        if (ctx.choice == "give") {
            const int count = qMin(ctx.invoker->getHandcardNum(),ctx.invoker->getHandcardNum()/2*amount);
            if (count <= 0) return ContinueEffects;
            const QList<int> selected = room->askForExchangeCards(ctx.invoker,objectName(),count,count,
                QString("jinshidu-give:%1::%2").arg(target->objectName()).arg(count),QString(),".|.|.|hand");
            QList<int> ids; for (int id : selected) if (ctx.invoker->handCards().contains(id) && !Sanguosha->getCard(id)->hasFlag("using") && !ids.contains(id)) ids << id;
            if (!ids.isEmpty() && target->isAlive()) { DummyCard cards(ids); room->giveCard(ctx.invoker,target,&cards,objectName()); }
            return ContinueEffects;
        }
        if (!ctx.invoker->canPindian(target,false) || !ctx.invoker->pindian(target,objectName())) return ContinueEffects;
        SkillContext gain = ctx; gain.choice = "receive";
        gain.extra_data = QVariantMap{{"from",target->objectName()},{"ids",ListI2V(target->handCards())}};
        skillEffect(gain,ctx.invoker);
        if (ctx.invoker->isAlive() && target->isAlive()) { SkillContext give = ctx; give.choice = "give"; skillEffect(give,target); }
        return ContinueEffects;
    }
};
class JinTaoyin : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    JinTaoyin() : TriggerSkillV2("jintaoyin") { events << Appear; hide_skill = true; m_baseAmount = 2; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        ServerPlayer *current = room->getCurrent();
        if (player && player->isAlive() && player->hasSkill(this) && current && current != player)
            result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *current = room->getCurrent();
        if (!current || current == ctx.owner || !ctx.invoker->askForSkillInvoke(this, current)) return false;
        ctx.targets << current;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        // This is an applied turn effect; it persists even if its grant is removed.
        room->addPlayerMark(target, "&jintaoyin-Clear");
        room->addMaxCards(target, -getEffectiveAmount(ctx));
        return false;
    }
};
class JinYimie : public TriggerSkillV2
{
public:
    JinYimie() : TriggerSkillV2("jinyimie") { events << DamageCaused << EventSkillInvoking; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext active = ctx.original_data->value<SkillContext>();
        if (active.bypass_cost && active.activationRef == ctx.activationRef) addUsage(active);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != DamageCaused || !player || !player->isAlive() || !player->hasSkill(this) || !player->hasTurn()) return result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.from == player && damage.to && damage.to != player && damage.to->isAlive()
            && room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong() > 0)
            result[player] << objectName();
        return usableJinCandidates(this, room, event, data, result);
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.to || !damage.to->isAlive()) return false;
        const int count = qMax(0, damage.to->getHp() - damage.damage);
        if (!ctx.invoker->askForSkillInvoke(this, QString("jinyimie:%1::%2").arg(damage.to->objectName()).arg(count))) return false;
        ctx.extra_data = count;
        ctx.targets << damage.to;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        room->loseHp(HpLostStruct(ctx.invoker, 1, objectName(), ctx.invoker));
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const qint64 damageId = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong();
        if (damage.to != target || damageId <= 0) return false;
        const int count = ctx.extra_data.toInt() * getEffectiveAmount(ctx);
        if (count <= 0) return false;
        room->broadcastSkillInvoke(objectName());
        damage.damage += count;
        ctx.original_data->setValue(damage);
        const int sequence = room->getTag("jinyimie_sequence").toInt() + 1;
        room->setTag("jinyimie_sequence", sequence);
        // Correlate the deferred recovery to this exact damage event, not tips or a mutable card flag.
        QVariantMap receipt{{"receipt", sequence}, {"damage", damageId}, {"amount", count}, {"from", ctx.invoker->objectName()},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        QVariantList receipts = target->getTag("jinyimie_effects").toList();
        receipts << receipt;
        target->setTag("jinyimie_effects", receipts);
        return false;
    }
};

class JinYimieRecover : public TriggerSkillV2
{
public:
    JinYimieRecover() : TriggerSkillV2("#jinyimie-recover") { events << DamageComplete << EventPhaseChanging; global = true; frequency = Compulsory; }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        foreach (ServerPlayer *player, room->getAllPlayers(true)) {
            QVariantList receipts = player->getTag("jinyimie_effects").toList();
            for (int i = receipts.length() - 1; i >= 0; --i)
                if (room->historyEvent(receipts.at(i).toMap().value("damage").toLongLong()).value("status").toString() == "finished")
                    receipts.removeAt(i);
            player->setTag("jinyimie_effects", receipts);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != DamageComplete || !player || !player->isAlive() || data.value<DamageStruct>().to != player) return true;
        const qint64 damageId = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong();
        if (damageId <= 0) return true;
        foreach (const QVariant &entry, player->getTag("jinyimie_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("damage").toLongLong() != damageId || !ref(receipt, "source").isValid() || !ref(receipt, "activation").isValid()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.sourceRef = ref(receipt, "source");
            ctx.instanceID = receipt.value("receipt").toInt();
            ctx.current_event = event;
            ctx.original_data = &data;
            ctx.extra_data = receipt;
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        foreach (const QVariant &entry, ctx.owner->getTag("jinyimie_effects").toList())
            if (entry.toMap() == ctx.extra_data.toMap() && ref(entry.toMap(), "source") == ctx.sourceRef) return true;
        return false;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.invoker; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantList receipts = ctx.owner->getTag("jinyimie_effects").toList();
        for (int i = receipts.length() - 1; i >= 0; --i)
            if (receipts.at(i).toMap().value("receipt") == ctx.extra_data.toMap().value("receipt")) receipts.removeAt(i);
        ctx.owner->setTag("jinyimie_effects", receipts);
        ServerPlayer *from = room->findPlayerByObjectName(ctx.extra_data.toMap().value("from").toString());
        if (from && !from->isAlive()) from = nullptr;
        const int amount = qMin(target->getLostHp(), getEffectiveAmount(ctx));
        if (amount > 0) room->recover(target, RecoverStruct("jinyimie", from, amount));
        return false;
    }
};

class JinTairan : public TriggerSkillV2
{
public:
    JinTairan() : TriggerSkillV2("jintairan")
    {
        events << EventPhaseStart << EventPhaseChanging;
        frequency = Compulsory;
        global = true;
    }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    // Only commits below this accepted skill event count, including interrupted draws/recovery.
    static QVariantMap committed(Room *room, ServerPlayer *player, const QVariantMap &receipt)
    {
        const qint64 parent = receipt.value("parent").toLongLong();
        if (parent <= 0) return QVariantMap();
        QVariantMap filter{{"to", player->objectName()}, {"limit", 100}};
        QVariantList cards;
        int recovered = 0;
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(filter);
            if (!page.value("complete").toBool() || !page.value("error").toString().isEmpty()) return QVariantMap();
            if (!filter.contains("watermark")) filter.insert("watermark", page.value("watermark"));
            foreach (const QVariant &entry, page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), value = fact.value("data").toMap();
                const qint64 event = fact.value("event_id").toLongLong();
                if (room->historyParent(event, "skill", true).value("id").toLongLong() != parent) continue;
                if (fact.value("kind").toString() == "actual_recover") recovered += value.value("amount").toInt();
                else if (fact.value("kind").toString() == "move" && value.value("to_place").toInt() == Player::PlaceHand) {
                    const QVariantMap draw = room->historyParent(event, "draw", true);
                    if (draw.isEmpty() || room->historyParent(draw.value("id").toLongLong(), "skill", true).value("id").toLongLong() != parent) continue;
                    const int id = value.value("card_id", -1).toInt();
                    if (id >= 0 && !cards.contains(id)) cards << id;
                }
            }
            if (!page.value("has_more").toBool()) break;
            filter.insert("after", page.value("next_after"));
        }
        return QVariantMap{{"complete", true}, {"recovered", recovered}, {"cards", cards}};
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        foreach (const QVariant &entry, ctx.owner->getTag("jintairan_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("receipt").toInt() == ctx.instanceID && ref(receipt, "source") == ctx.sourceRef
                && ref(receipt, "activation") == ref(ctx.extra_data.toMap(), "activation")) return true;
        }
        return false;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx);
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart) return false;
        if (!player || !player->isAlive() || player->getPhase() != Player::Play) return true;
        foreach (const QVariant &entry, player->getTag("jintairan_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (!ref(receipt, "source").isValid() || !ref(receipt, "activation").isValid()
                || !committed(room, player, receipt).value("complete").toBool()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = player;
            ctx.instanceID = receipt.value("receipt").toInt();
            ctx.sourceRef = ref(receipt, "source");
            ctx.extra_data = receipt;
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.is_forced = true;
            contexts << ctx;
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventPhaseChanging && player && player->isAlive() && player->hasSkill(this)
            && data.value<PhaseChangeStruct>().to == Player::NotActive && room->currentHistoryEventId() > 0
            && (player->isWounded() || player->getHandcardNum() < player->getMaxCards())) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.invoker; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (event == EventPhaseStart) {
            const QVariantMap committedCards = committed(room, ctx.owner, ctx.extra_data.toMap());
            if (!committedCards.value("complete").toBool()) return false;
            QVariantList receipts = ctx.owner->getTag("jintairan_effects").toList();
            for (int i = receipts.length() - 1; i >= 0; --i)
                if (receipts.at(i).toMap().value("receipt").toInt() == ctx.instanceID) receipts.removeAt(i);
            // Consume before HP loss can enter another phase or kill/revive the recipient.
            ctx.owner->setTag("jintairan_effects", receipts);
            room->setPlayerMark(ctx.owner, "&jintairanrecover", 0);
            room->setPlayerMark(ctx.owner, "&jintairan+draw", 0);
            room->sendCompulsoryTriggerLog(ctx.invoker, this);
            const int recovered = committedCards.value("recovered").toInt();
            if (recovered > 0) room->loseHp(HpLostStruct(target, recovered * amount, objectName(), ctx.invoker));
            QList<int> discard;
            foreach (const QVariant &entry, committedCards.value("cards").toList()) {
                const int id = entry.toInt();
                room->setCardTip(id, "-jintairan");
                if (target->isAlive() && room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand
                    && !Sanguosha->getCard(id)->hasFlag("using") && target->canDiscard(target, id)) discard << id;
            }
            if (!discard.isEmpty()) room->throwCard(discard, objectName(), target);
            return false;
        }
        const qint64 parent = room->currentHistoryEventId();
        if (parent <= 0) return false;
        const int sequence = room->getTag("jintairan_sequence").toInt() + 1;
        room->setTag("jintairan_sequence", sequence);
        const QVariantMap receipt{{"receipt", sequence}, {"parent", parent},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        QVariantList receipts = target->getTag("jintairan_effects").toList();
        receipts << receipt;
        target->setTag("jintairan_effects", receipts);
        room->sendCompulsoryTriggerLog(ctx.invoker, this);
        if (target->isWounded()) room->recover(target, RecoverStruct(objectName(), ctx.invoker, target->getLostHp() * amount));
        const int count = target->getMaxCards() - target->getHandcardNum();
        if (target->isAlive() && count > 0) room->drawCardsList(target, count * amount, objectName());
        const QVariantMap result = committed(room, target, receipt);
        if (result.value("complete").toBool()) {
            room->addPlayerMark(target, "&jintairanrecover", result.value("recovered").toInt());
            int hands = 0;
            foreach (const QVariant &entry, result.value("cards").toList()) {
                const int id = entry.toInt();
                if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand) continue;
                room->setCardTip(id, objectName());
                ++hands;
            }
            room->addPlayerMark(target, "&jintairan+draw", hands);
        }
        return false;
    }
};

JinRuilveGiveCard::JinRuilveGiveCard()
{
    setSkillName("jinruilve_give");
    will_throw = false;
    handling_method = Card::MethodNone;
    mute = true;
}

class JinRuilveGive : public ViewAsSkillV2
{
public:
    JinRuilveGive() : ViewAsSkillV2("jinruilve_give", 1) { attached_lord_skill = true; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "JinRuilveGiveCard"; }
    bool willThrowSelectedCards() const override { return false; }
    static SkillInstanceRef parent(const ActiveSkillRequest &request)
    {
        if (!request.initiator || !request.activationRef.isValid()) return SkillInstanceRef();
        const SkillInstance *instance = request.initiator->findSkillInstance(request.activationRef.key.skillName, request.activationRef.key.instanceID);
        return instance ? instance->parentRef : SkillInstanceRef();
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || request.initiator->getKingdom() != "jin") return false;
        foreach (const Player *other, request.initiator->getAliveSiblings())
            if (canSelectTarget(request, QList<const Player *>(), other)) return true;
        return false;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && request.selectedCardIds.isEmpty() && candidate->getEffectiveId() >= 0
            && !candidate->hasFlag("using") && request.initiator->handCards().contains(candidate->getEffectiveId())
            && (candidate->isKindOf("Slash") || (candidate->isKindOf("TrickCard") && candidate->isDamageCard()));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.length() != 1) return false;
        ActiveSkillRequest empty = request;
        empty.selectedCardIds.clear();
        return canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    {
        const SkillInstanceRef lord = parent(request);
        return request.initiator && selected.isEmpty() && candidate && candidate != request.initiator && candidate->isAlive()
            && lord.isValid() && lord.key.skillName == "jinruilve" && lord.ownerObjectName == candidate->objectName()
            && candidate->hasLordSkill("jinruilve") && candidate->getValidSkillInstanceIds("jinruilve").contains(lord.key.instanceID);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.length() == 1; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        return cardSelectionFeasible(request) && room->getCardOwner(request.selectedCardIds.first()) == ctx.invoker
            && room->getCardPlace(request.selectedCardIds.first()) == Player::PlaceHand;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0 || !ctx.use_card || ctx.use_card->getSubcards().length() != 1) return ContinueEffects;
        Room *room = target->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (id < 0 || room->getCardOwner(id) != ctx.invoker || room->getCardPlace(id) != Player::PlaceHand
            || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        room->broadcastSkillInvoke(target->isWeidi() ? "weidi" : "jinruilve", -1, target);
        ctx.invoker->skillInvoked("jinruilve", 0, target);
        room->giveCard(ctx.invoker, target, Sanguosha->getCard(id), "jinruilve", true);
        return ContinueEffects;
    }
};

class JinRuilve : public TriggerSkillV2
{
public:
    JinRuilve() : TriggerSkillV2("jinruilve$")
    {
        events << EventPhaseStart << EventPhaseEnd << EventAcquireSkill << EventLoseSkill << EventPhaseChanging;
        global = true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const QVariant phase = room->historyScopes().value("phase_id");
        QVariantList leases = room->getTag("jinruilve_attached").toList();
        const bool expire = (event == EventPhaseEnd && player && player->getPhase() == Player::Play)
            || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().from == Player::Play);
        QList<SkillInstanceRef> retired;
        for (int i = leases.length() - 1; i >= 0; --i) {
            const QVariantMap lease = leases.at(i).toMap();
            ServerPlayer *holder = room->findPlayerByObjectName(lease.value("owner").toString(), true);
            const SkillInstanceRef child(lease.value("owner").toString(), SkillInstanceKey("jinruilve_give", lease.value("id").toInt()));
            if (!holder || !holder->hasSkillInstance(child.key.skillName, child.key.instanceID) || (expire && (event == EventPhaseEnd ? lease.value("phase") == phase
                    : finishedJinPlayPhase(room, lease.value("phase"), player)))) {
                retired << child;
                leases.removeAt(i);
            }
        }
        room->setTag("jinruilve_attached", leases);
        foreach (const SkillInstanceRef &child, retired) {
            bool retained = false;
            foreach (const QVariant &entry, leases)
                if (entry.toMap().value("owner").toString() == child.ownerObjectName && entry.toMap().value("id").toInt() == child.key.instanceID) retained = true;
            if (!retained) room->detachAttachedSkill(child);
        }
        if (expire || phase.toLongLong() <= 0 || (event != EventPhaseStart && event != EventAcquireSkill)) return true;
        // These are live lord affordances, linked to the exact parent grant rather than permanent acquisitions.
        foreach (ServerPlayer *holder, room->getAlivePlayers()) {
            if (holder->getPhase() != Player::Play
                || (event == EventPhaseStart ? holder != player
                    : room->historyEvent(phase.toLongLong()).value("data").toMap().value("player").toString() != holder->objectName())) continue;
            foreach (ServerPlayer *lord, room->getOtherPlayers(holder)) {
                if (!lord->hasLordSkill(this, true)) continue;
                foreach (int id, lord->getValidSkillInstanceIds(objectName())) {
                    const SkillInstanceRef parent(lord->objectName(), SkillInstanceKey(objectName(), id));
                    const SkillInstanceRef child = room->attachSkillToPlayer(holder, "jinruilve_give", parent);
                    if (!child.isValid()) continue;
                    const QVariantMap lease{{"owner", holder->objectName()}, {"id", child.key.instanceID}, {"phase", phase}};
                    leases = room->getTag("jinruilve_attached").toList();
                    if (!leases.contains(lease)) leases << lease;
                    room->setTag("jinruilve_attached", leases);
                }
            }
        }
        return true;
    }
};

class JinHuirong : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    JinHuirong() : TriggerSkillV2("jinhuirong") { events << Appear; frequency = Compulsory; hide_skill = true; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(this)) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, room->getAlivePlayers(), objectName(), "@jinhuirong-invoke", false, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        const int excess = target->getHandcardNum() - target->getHp();
        if (excess > 0) room->askForDiscard(target, objectName(), excess, excess);
        else if (excess < 0) {
            const int count = qMin(5, target->getHp()) - target->getHandcardNum();
            if (count > 0) target->drawCards(count, objectName());
        }
        return false;
    }
};
class JinCiwei : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    JinCiwei() : TriggerSkillV2("jinciwei") { events << CardUsed; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->hasFlag("CurrentPlayer") || !use.card
            || (!use.card->isKindOf("BasicCard") && !use.card->isNDTrick())) return result;
        const QVariantMap history = room->queryCardHistory(player, "turn");
        if (history.contains("error") || !history.value("complete").toBool() || !history.value("attribution_complete").toBool()) return result;
        int count = 0;
        foreach (const QVariant &entry, history.value("items").toList()) {
            const QVariantMap card = entry.toMap();
            if (card.value("type").toInt() == Card::TypeBasic || card.value("ndtrick").toBool()) ++count;
        }
        if (count != 2) return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->isAlive() && owner->hasSkill(this) && owner->canDiscard(owner, "he")) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.from || !use.from->isAlive()) return false;
        const Card *card = room->askForCard(ctx.invoker, "..",
            QString("@jinciwei-discard:%1::%2").arg(use.from->objectName(), use.card->objectName()),
            *ctx.original_data, Card::MethodNone, nullptr, false, objectName());
        if (!card || card->getEffectiveId() < 0 || !ctx.invoker->canDiscard(ctx.invoker, card->getEffectiveId())) return false;
        ctx.extra_data = card->getEffectiveId();
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (room->getCardOwner(id) != ctx.invoker || !ctx.invoker->canDiscard(ctx.invoker, id)) return false;
        room->throwCard(id, objectName(), ctx.invoker);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        room->broadcastSkillInvoke(objectName());
        if (!use.nullified_list.contains("_ALL_TARGETS")) use.nullified_list << "_ALL_TARGETS";
        ctx.original_data->setValue(use);
        return false;
    }
};
class JinCaiyuan : public TriggerSkillV2
{
public:
    JinCaiyuan() : TriggerSkillV2("jincaiyuan")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
        setBaseAmount(2);
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    static bool unchangedSincePreviousEnd(Room *room, ServerPlayer *player)
    {
        const qint64 current = room->historyScopes().value("turn_id").toLongLong();
        if (current <= 0) return false;
        qint64 previous = 0;
        QVariantMap turns{{"kind", "turn"}, {"player", player->objectName()}};
        for (;;) {
            const QVariantMap page = room->queryHistoryEvents(turns);
            if (!page.value("complete").toBool()) return false;
            foreach (const QVariant &entry, page.value("items").toList()) {
                const qint64 id = entry.toMap().value("id").toLongLong();
                if (id > previous && id < current) previous = id;
            }
            if (!page.value("has_more").toBool()) break;
            turns.insert("watermark", page.value("watermark"));
            turns.insert("after", page.value("next_after"));
        }
        qint64 baseline = 0, end = 0, lastDecrease = 0;
        QVariantMap facts{{"player", player->objectName()}};
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(facts);
            if (!page.value("complete").toBool()) return false;
            foreach (const QVariant &entry, page.value("items").toList()) {
                const QVariantMap fact = entry.toMap(), data = fact.value("data").toMap();
                const qint64 sequence = fact.value("sequence").toLongLong();
                if (fact.value("kind").toString() == "player_state") {
                    if (data.value("boundary").toString() == "baseline") baseline = sequence;
                    else if (data.value("boundary").toString() == "commit") {
                        if (!data.contains("hp_before") || !data.contains("hp_after")) return false;
                        if (data.value("hp_after").toInt() < data.value("hp_before").toInt()) lastDecrease = sequence;
                    }
                } else if (fact.value("kind").toString() == "turn_hp_snapshot"
                    && fact.value("event_id").toLongLong() == previous && data.value("boundary").toString() == "end"
                    && data.value("turn_owner").toString() == player->objectName()) end = sequence;
            }
            if (!page.value("has_more").toBool()) break;
            facts.insert("watermark", page.value("watermark"));
            facts.insert("after", page.value("next_after"));
        }
        // Old snapshots without the setter baseline cannot prove a negative. The first own turn starts at the game baseline.
        return baseline > 0 && (previous == 0 || end > 0) && lastDecrease <= (previous == 0 ? baseline : end);
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Finish
            && unchangedSincePreviousEnd(room, player)) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!unchangedSincePreviousEnd(room, ctx.owner)) return false;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.invoker, this);
        const int amount = getEffectiveAmount(ctx);
        if (amount > 0) target->drawCards(amount, objectName());
        return false;
    }
};

static QVariantList jinZhuoshengRoundCards(Room *room, const Player *player)
{
    QVariantList ids; const QVariant round = room->historyScopes().value("round_id");
    if (!player || round.toLongLong() <= 0 || room->getTag("FirstRound").toBool()) return ids;
    QVariantMap query{{"round_id",round},{"to",player->objectName()}};
    for (;;) {
        const QVariantMap page = room->queryHistoryMoves(query);
        if (!page.value("complete").toBool() || page.contains("error")) return {};
        for (const QVariant &value : page.value("items").toList()) {
            const QVariantMap fact = value.toMap().value("data").toMap();
            const int id = fact.value("card_id",-1).toInt();
            if (id >= 0 && fact.value("to_place",-1).toInt() == Player::PlaceHand
                && fact.value("reason_skill").toString() != "jinzhuosheng" && !ids.contains(id)) ids << id;
        }
        if (!page.value("has_more").toBool()) return ids;
        query["watermark"] = page.value("watermark"); query["after"] = page.value("next_after");
    }
}

static bool jinZhuoshengEligible(const Card *card, const QVariantList &obtained)
{
    if (!card || card->getTypeId() == Card::TypeSkill) return false;
    const QList<int> ids = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
    if (ids.isEmpty()) return false;
    for (int id : ids) if (!obtained.contains(id)) return false;
    return true;
}

class JinZhuosheng : public TriggerSkillV2
{
public:
    JinZhuosheng() : TriggerSkillV2("jinzhuosheng") { events << CardUsed; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Play
            && jinZhuoshengEligible(use.card,jinZhuoshengRoundCards(room,player)) ? TriggerList{{player,{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (use.card->isKindOf("BasicCard")) { ctx.choice = "basic"; ctx.targets = {ctx.owner}; return true; }
        if (use.card->isKindOf("EquipCard")) {
            if (!ctx.owner->askForSkillInvoke(this,*ctx.original_data)) return false;
            ctx.choice = "draw"; ctx.targets = {ctx.owner}; return true;
        }
        if (!use.card->isNDTrick()) return false;
        QStringList choices;
        if (!room->getUseExtraTargets(use).isEmpty()) choices << "add";
        if (use.to.size() > 1) choices << "remove";
        if (choices.isEmpty()) return false;
        choices << "cancel"; ctx.choice = room->askForChoice(ctx.owner,objectName(),choices.join("+"),*ctx.original_data);
        return ctx.choice != "cancel" && choices.contains(ctx.choice);
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        if (ctx.choice == "basic" || ctx.choice == "draw") return false;
        ctx.manual_effect = true;
        for (int n = getEffectiveAmount(ctx); n > 0 && ctx.invoker->isAlive(); --n) {
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            QList<ServerPlayer *> candidates = ctx.choice == "add" ? room->getUseExtraTargets(use) : use.to;
            if (candidates.isEmpty() || (ctx.choice == "remove" && candidates.size() <= 1)) break;
            ServerPlayer *target = room->askForPlayerChosen(ctx.invoker,candidates,objectName(),
                (ctx.choice == "add" ? "@qiaoshui-add:::" : "@qiaoshui-remove:::") + use.card->objectName());
            if (!target || !candidates.contains(target)) break;
            skillEffect(event,room,actor,ctx,target);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (ctx.choice == "basic") use.m_addHistory = false;
        else if (ctx.choice == "draw") { target->drawCards(amount,objectName()); return false; }
        else if (ctx.choice == "remove") { if (use.to.size() > 1) use.to.removeOne(target); }
        else if (ctx.choice == "collateral-victim") {
            QVariantMap result = ctx.extra_data.toMap(); result["victim"] = target->objectName(); ctx.extra_data = result; return false;
        } else {
            if (!room->getUseExtraTargets(use).contains(target)) return false;
            if (use.card->isKindOf("Collateral")) {
                QList<ServerPlayer *> victims;
                for (ServerPlayer *candidate : room->getOtherPlayers(target)) if (target->canSlash(candidate)) victims << candidate;
                if (victims.isEmpty()) return false;
                ServerPlayer *victim = room->askForPlayerChosen(ctx.invoker,victims,objectName(),"@qiaoshui-add:::collateral");
                SkillContext pair = ctx; pair.choice = "collateral-victim"; pair.extra_data = QVariantMap();
                skillEffect(event,room,actor,pair,victim);
                if (pair.extra_data.toMap().value("victim").toString() != (victim ? victim->objectName() : QString())
                    || !victim || !target->canSlash(victim)) return false;
                target->setTag("attachTarget",QVariant::fromValue(victim));
            }
            use = ctx.original_data->value<CardUseStruct>();
            if (!room->getUseExtraTargets(use).contains(target)) return false;
            use.to << target; room->sortByActionOrder(use.to);
        }
        *ctx.original_data = QVariant::fromValue(use); return false;
    }
};

class JinZhuoshengTargetMod : public TargetModSkillV2
{
public:
    JinZhuoshengTargetMod() : TargetModSkillV2("#jinzhuosheng-target", "BasicCard") { setBaseAmount(999); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.primary || ctx.primary->getPhase() != Player::Play || ctx.modType != DistanceLimit) return CorrectSkillResult::noEffect();
        const auto *server = dynamic_cast<const ServerPlayer *>(ctx.primary);
        const QVariantList ids = server ? jinZhuoshengRoundCards(server->getRoom(),server)
            : ctx.holder->getSkillInstanceStateValue(objectName(),ctx.instanceRef.key.instanceID,"round_cards").toList();
        return jinZhuoshengEligible(ctx.card,ids) ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

class JinZhuoshengRecord : public TriggerSkillV2
{
public:
    JinZhuoshengRecord() : TriggerSkillV2("#jinzhuosheng-record")
    { global = true; events << CardsMoveOneTime << RoundEnd << EventAcquireSkill; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
    {
        // Owner-only client projection; server legality reads immutable history directly.
        for (ServerPlayer *holder : room->getAllPlayers(true)) {
            const auto instances = holder->getSkillInstanceIds("#jinzhuosheng-target");
            if (instances.isEmpty()) continue;
            const QVariantList ids = event == RoundEnd ? QVariantList() : jinZhuoshengRoundCards(room,holder);
            for (int id : instances) holder->setSkillInstanceStateValue("#jinzhuosheng-target",id,"round_cards",ids);
        }
        return false;
    }
};

TousuiCard::TousuiCard()
{
    setSkillName("tousui");
    will_throw = false;
    handling_method = Card::MethodUse;
}

class TousuiVs : public ViewAsSkillV2
{
public:
    TousuiVs() : ViewAsSkillV2("tousui", 999) { response_or_use = true; }
    QString historyKey(const ActiveSkillRequest &) const override { return "Slash"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return Slash::IsAvailable(request.initiator);
        Slash slash(Card::NoSuit, 0);
        return request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && !request.pattern.startsWith('@')
            && Sanguosha->matchPattern(request.pattern, request.initiator, &slash);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && card && card->getEffectiveId() >= 0 && !card->hasFlag("using")
            && !request.selectedCardIds.contains(card->getEffectiveId())
            && (request.initiator->getCards("he").contains(card) || request.initiator->getHandPile().contains(card->getEffectiveId()));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty()) return false;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        foreach (int id, request.selectedCardIds) {
            if (!canSelectCard(selection, Sanguosha->getCard(id))) return false;
            selection.selectedCardIds << id;
        }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName(objectName());
        slash->addSubcards(request.selectedCardIds);
        slash->setTag("tousui_count", request.selectedCardIds.length());
        return slash;
    }
    bool pay(Room *room, SkillContext &, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return false;
        ServerPlayer *payer = room->findPlayerByObjectName(request.initiator->objectName());
        if (!payer || !payer->isAlive()) return false;
        // These materials are payment to the deck bottom, not the resulting Slash's physical materials.
        room->moveCardsToEndOfDrawpile(payer, request.selectedCardIds, objectName(), false);
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        const int count = ctx.use_card ? ctx.use_card->getTag("tousui_count").toInt() : 0;
        if (count <= 0 || !ctx.sourceRef.isValid() || !ctx.activationRef.isValid()) return FinishSkill;
        Room *room = ctx.invoker->getRoom();
        const int sequence = room->getTag("tousui_sequence").toInt() + 1;
        room->setTag("tousui_sequence", sequence);
        Slash *slash = new Slash(Card::NoSuit, 0);
        slash->setSkillName(objectName());
        slash->setSourceSkill(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID);
        slash->setActivationSkill(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID);
        slash->setTag("tousui_effect", QVariantMap{{"receipt", sequence}, {"amount", count * getEffectiveAmount(ctx)},
            {"actor", ctx.invoker->objectName()}, {"source_owner", ctx.sourceRef.ownerObjectName},
            {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
            {"activation_id", ctx.activationRef.key.instanceID}});
        slash->deleteLater();
        // Also needed when payment is waived: the accepted Slash must not discard the preview's selection.
        ctx.updated_card = slash;
        return ContinueEffects;
    }
};

class Tousui : public TriggerSkillV2
{
public:
    Tousui() : TriggerSkillV2("tousui")
    {
        events << CardEffect << CardFinished;
        view_as_skill = new TousuiVs;
        frequency = Compulsory;
        global = true;
    }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == CardFinished) {
            const Card *card = data.value<CardUseStruct>().card;
            if (card) card->removeTag("tousui_effect");
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != CardEffect) return true;
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        if (!effect.card || !effect.from || !effect.to || !effect.to->isAlive()) return true;
        const QVariantMap receipt = effect.card->getTag("tousui_effect").toMap();
        if (receipt.value("actor").toString() != effect.from->objectName() || receipt.value("receipt").toInt() <= 0
            || !ref(receipt, "source").isValid() || !ref(receipt, "activation").isValid()) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = effect.from;
        ctx.sourceRef = ref(receipt, "source");
        ctx.instanceID = receipt.value("receipt").toInt();
        ctx.current_event = event;
        ctx.original_data = &data;
        ctx.extra_data = receipt;
        ctx.setModifiedAmount(receipt.value("amount").toInt());
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.original_data) return false;
        const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        if (!effect.card) return false;
        const QVariantMap receipt = effect.card->getTag("tousui_effect").toMap();
        return receipt.value("receipt") == ctx.extra_data.toMap().value("receipt") && ref(receipt, "source") == ctx.sourceRef
            && ref(receipt, "activation").isValid();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets << ctx.original_data->value<CardEffectStruct>().to;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        const int amount = getEffectiveAmount(ctx);
        if (effect.to == target && amount > 0) { effect.offset_num = amount; ctx.original_data->setValue(effect); }
        return false;
    }
};

class ChumingVs : public ViewAsSkillV2
{
public:
    ChumingVs() : ViewAsSkillV2("chuming") { response_pattern = "@@chuming!"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == response_pattern
        && request.initiator->getSkillInstanceStateValue(objectName(),request.activationRef.key.instanceID,"response").toMap().contains("name"); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !request.selectedCardIds.isEmpty()) return nullptr;
        const QVariantMap state = request.initiator->getSkillInstanceStateValue(objectName(),request.activationRef.key.instanceID,"response").toMap();
        Card *card = Sanguosha->cloneCard(state.value("name").toString()); if (!card) return nullptr;
        for (const QVariant &value : state.value("ids").toList()) card->addSubcard(value.toInt());
        card->setSkillName(objectName()); card->setTag("chuming_target",state.value("target")); return card;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.initiator || !ctx.use_card) return FinishSkill;
        Room *room = ctx.initiator->getRoom();
        const QVariantMap state = ctx.initiator->getSkillInstanceStateValue(objectName(),ctx.activationRef.key.instanceID,"response").toMap();
        if (state.value("name").toString() != ctx.use_card->objectName() || state.value("ids").toList().isEmpty()) return FinishSkill;
        for (const QVariant &value : state.value("ids").toList()) {
            const int id = value.toInt(); if (room->getCardOwner(id) || Sanguosha->getCard(id)->hasFlag("using")) return FinishSkill;
        }
        return ContinueEffects;
    }
};

class Chuming : public TriggerSkillV2
{
public:
    Chuming() : TriggerSkillV2("chuming")
    { global = true; frequency = Compulsory; events << DamageCaused << DamageInflicted << EventPhaseChanging << EventSkillEffectFinished << Death;
        view_as_skill = new ChumingVs; waked_skills = "#chuming_pro"; }
    static void consume(Room *room,const QVariant &serial)
    {
        QVariantList kept; for (const QVariant &value : room->getTag("chuming_receipts").toList())
            if (value.toMap().value("serial") != serial) kept << value;
        room->setTag("chuming_receipts",kept);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext done = data.value<SkillContext>();
            if (done.skill_name == objectName() && done.extra_data.toMap().contains("serial")) consume(room,done.extra_data.toMap().value("serial"));
        } else if (event == Death && data.value<DeathStruct>().who) {
            const QString dead = data.value<DeathStruct>().who->objectName(); QVariantList kept;
            for (const QVariant &value : room->getTag("chuming_receipts").toList()) {
                const QVariantMap row = value.toMap(); if (row.value("actor").toString() != dead && row.value("target").toString() != dead) kept << row;
            }
            room->setTag("chuming_receipts",kept);
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseChanging) return false;
        if (!actor || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        for (const QVariant &value : room->getTag("chuming_receipts").toList()) {
            const QVariantMap row = value.toMap(); if (row.value("turn") != room->historyScopes().value("turn_id")) continue;
            ServerPlayer *from = room->findPlayerByObjectName(row.value("actor").toString());
            ServerPlayer *to = room->findPlayerByObjectName(row.value("target").toString());
            if (!from || from->isDead() || !to || to->isDead()) continue;
            SkillContext ctx; ctx.owner = room->findPlayerByObjectName(row.value("activation_owner").toString(),true);
            ctx.invoker = from; ctx.initiator = ctx.owner; ctx.skill_name = objectName(); ctx.instanceID = row.value("serial").toInt();
            ctx.sourceRef = SkillInstanceRef(row.value("source_owner").toString(),SkillInstanceKey(row.value("source_skill").toString(),row.value("source_id").toInt()));
            ctx.amount = row.value("amount").toInt(); ctx.extra_data = row; ctx.original_data = &data; ctx.current_event = event; ctx.is_forced = true;
            ctx.targets = {from}; if (ctx.owner && ctx.sourceRef.isValid()) contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { return ctx.extra_data.toMap().contains("serial") ? room->getTag("chuming_receipts").toList().contains(ctx.extra_data) : TriggerSkillV2::isSourceAvailable(room,ctx); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event != DamageCaused && event != DamageInflicted) return {};
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(this) && damage.from && damage.to && damage.from != damage.to
            ? TriggerList{{player,{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == EventPhaseChanging) return true;
        if (room->isGeneralHiddenForSkill(ctx.activationRef) && !ctx.owner->askForSkillInvoke(this,*ctx.original_data)) return false;
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        const QList<int> ids = !damage.card ? QList<int>() : damage.card->isVirtualCard() ? damage.card->getSubcards() : QList<int>{damage.card->getEffectiveId()};
        ctx.choice = ids.isEmpty() || ids.contains(-1) ? "damage" : "queue";
        ctx.extra_data = ListI2V(ids); ctx.targets = {ctx.choice == "damage" ? damage.to : damage.from == ctx.owner ? damage.to : damage.from}; return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx) const override
    {
        if (event != EventPhaseChanging) return false;
        consume(room,ctx.extra_data.toMap().value("serial")); ctx.manual_effect = true;
        skillEffect(event,room,actor,ctx,ctx.invoker); return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        if (event == EventPhaseChanging) {
            const QVariantMap row = ctx.extra_data.toMap(); ServerPlayer *victim = room->findPlayerByObjectName(row.value("target").toString());
            if (!victim || victim->isDead()) return false;
            QList<int> ids; for (const QVariant &value : row.value("ids").toList()) {
                const int id = value.toInt(); if (room->getCardOwner(id) || Sanguosha->getCard(id)->hasFlag("using")) return false; ids << id;
            }
            if (ids.isEmpty()) return false;
            QStringList choices;
            for (const QString &name : {QStringLiteral("collateral"),QStringLiteral("dismantlement")}) {
                std::unique_ptr<Card> card(Sanguosha->cloneCard(name)); card->addSubcards(ids); card->setSkillName(objectName()); card->setTag("chuming_target",victim->objectName());
                if (target->canUse(card.get(),victim)) choices << name;
            }
            if (choices.isEmpty()) return false;
            SkillContext accepted = ctx;
            accepted.activationRef = SkillInstanceRef(row.value("activation_owner").toString(),SkillInstanceKey(objectName(),row.value("activation_id").toInt()));
            Room::AcceptedViewAsEffectScope scope(room,target,objectName(),accepted); if (!scope.isValid()) return false;
            const QString choice = room->askForChoice(target,objectName(),choices.join("+")); if (!choices.contains(choice)) return false;
            target->setSkillInstanceStateValue(objectName(),scope.activationRef().key.instanceID,"response",
                QVariantMap{{"name",choice},{"ids",ListI2V(ids)},{"target",victim->objectName()}});
            const QVariant oldName = target->property("chumingUse"), oldIds = target->property("chumingSubcard");
            const auto restore = qScopeGuard([&] { room->setPlayerProperty(target,"chumingUse",oldName); room->setPlayerProperty(target,"chumingSubcard",oldIds); });
            room->setPlayerProperty(target,"chumingUse",choice); room->setPlayerProperty(target,"chumingSubcard",ListI2S(ids).join("+"));
            room->askForUseCard(target,"@@chuming!","chuming0:"+choice); return false;
        }
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (ctx.choice == "damage") { damage.damage += amount; *ctx.original_data = QVariant::fromValue(damage); return false; }
        const qint64 serial = room->getTag("chuming_sequence").toLongLong()+1; room->setTag("chuming_sequence",serial);
        QVariantList rows = room->getTag("chuming_receipts").toList();
        rows << QVariantMap{{"serial",serial},{"turn",room->historyScopes().value("turn_id")},{"actor",target->objectName()},{"target",ctx.owner->objectName()},
            {"ids",ctx.extra_data},{"amount",amount},{"activation_owner",ctx.activationRef.ownerObjectName},{"activation_id",ctx.activationRef.key.instanceID},
            {"source_owner",ctx.sourceRef.ownerObjectName},{"source_skill",ctx.sourceRef.key.skillName},{"source_id",ctx.sourceRef.key.instanceID}};
        room->setTag("chuming_receipts",rows); return false;
    }
};

class ChumingPro : public ProhibitSkill
{
public:
    ChumingPro() : ProhibitSkill("#chuming_pro") {}
    bool isProhibited(const Player *, const Player *to, const Card *card, const QList<const Player *> &selected) const override
    {
        if (!card || card->getSkillName() != "chuming") return false;
        if (card->isKindOf("Collateral") && !selected.isEmpty()) return false;
        return card->getTag("chuming_target").toString() != to->objectName();
    }
};

BeiPackage::BeiPackage()
	: Package("bei")
{
	General *jin_zhanghuyuechen = new General(this, "jin_zhanghuyuechen", "jin", 4);
	jin_zhanghuyuechen->addSkill(new JinXijue);

	General *jin_xiahouhui = new General(this, "jin_xiahouhui", "jin", 3, false);
	jin_xiahouhui->addSkill(new JinBaoQie);
	jin_xiahouhui->addSkill(new JinYishi);
	jin_xiahouhui->addSkill(new JinShidu);

	General *jin_simashi = new General(this, "jin_simashi$", "jin", 4, true, false, false, 3);
	jin_simashi->addSkill(new JinTaoyin);
	jin_simashi->addSkill(new JinYimie);
	jin_simashi->addSkill(new JinYimieRecover);
	jin_simashi->addSkill(new JinTairan);
	jin_simashi->addSkill(new JinRuilve);
    skills << new JinRuilveGive;
    related_skills.insert("jinruilve", "jinruilve_give");
	related_skills.insert("jinyimie", "#jinyimie-recover");

	General *jin_yanghuiyu = new General(this, "jin_yanghuiyu", "jin", 3, false);
	jin_yanghuiyu->addSkill(new JinHuirong);
	jin_yanghuiyu->addSkill(new JinCiwei);
	jin_yanghuiyu->addSkill(new JinCaiyuan);

	General *jin_shibao = new General(this, "jin_shibao", "jin", 4);
	jin_shibao->addSkill(new JinZhuosheng);
	jin_shibao->addSkill(new JinZhuoshengTargetMod);
	jin_shibao->addSkill(new JinZhuoshengRecord);
	related_skills.insert("jinzhuosheng", "#jinzhuosheng-target");
	related_skills.insert("jinzhuosheng", "#jinzhuosheng-record");

	General *ol_ercheng = new General(this, "ol_ercheng", "wei", 6);
	ol_ercheng->addSkill(new Tousui);
	ol_ercheng->addSkill(new Chuming);
	ol_ercheng->addSkill(new ChumingPro);
	addMetaObject<TousuiCard>();

	addMetaObject<JinYishiCard>();
	addMetaObject<JinShiduCard>();
	addMetaObject<JinRuilveGiveCard>();


}
ADD_PACKAGE(Bei)


class JinTuishi : public TriggerSkillV2
{
public:
    JinTuishi() : TriggerSkillV2("jintuishi")
    {
        events << Appear << EventPhaseChanging;
        hide_skill = true;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event == Appear && player == ctx.owner && room->getCurrent() != player) {
            // Eligibility belongs to this apparition's exact instance and turn.
            const QVariant turn = room->historyScopes().value("turn_id");
            ctx.owner->setSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "appear_turn", turn);
            room->setPlayerMark(ctx.owner, "&jintuishi-Clear", 1);
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != EventPhaseChanging || !player || !player->isAlive()
            || data.value<PhaseChangeStruct>().to != Player::NotActive) return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(this)) result[owner] << objectName() + "->" + player->objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.preferredTarget;
        const QString turn = room->historyScopes().value("turn_id").toString();
        const auto &ref = ctx.activationRef;
        if (!player || !player->isAlive() || turn.isEmpty() || turn == "0"
            || ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "appear_turn").toString() != turn) return false;
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *target, room->getOtherPlayers(player))
            if (player->inMyAttackRange(target) && player->canSlash(target)) targets << target;
        ServerPlayer *victim = room->askForPlayerChosen(ctx.invoker, targets, objectName(),
            "@jintuishi-invoke:" + player->objectName(), true, true);
        if (!victim) return false;
        ctx.extra_data = victim->objectName();
        ctx.targets = QList<ServerPlayer *>() << player;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *victim = room->findPlayerByObjectName(ctx.extra_data.toString());
        if (!victim || !victim->isAlive() || !target->isAlive()) return false;
        room->broadcastSkillInvoke(objectName());
        if (!room->askForUseSlashTo(target, victim, "@jintuishi_slash:" + victim->objectName(), true, false, true, ctx.invoker)) {
            const int amount = getEffectiveAmount(ctx);
            if (target->isAlive() && amount > 0) room->damage(DamageStruct(objectName(), ctx.invoker, target, amount));
        }
        return false;
    }
};
JinChoufaCard::JinChoufaCard() { setSkillName("jinchoufa"); }

class JinChoufaVS : public ViewAsSkillV2
{
public:
    JinChoufaVS() : ViewAsSkillV2("jinchoufa") { setCardCount(0); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "JinChoufaCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    {
        return request.initiator && target && target->isAlive() && target != request.initiator
            && targets.isEmpty() && !target->isKongcheng();
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        return targets.length() == 1 && canSelectTarget(request, QList<const Player *>(), targets.first());
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0 || target->isKongcheng()) return ContinueEffects;
        Room *room = target->getRoom();
        const Card *shown = target->getRandomHandCard();
        if (!shown || shown->hasFlag("using")) return ContinueEffects;
        const int type = shown->getTypeId();
        room->showCard(target, shown->getEffectiveId());
        const QList<const Card *> hand = target->getHandcards();
        foreach (const Card *card, hand) {
            const int id = card->getEffectiveId();
            if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand
                || card->getTypeId() == type || card->hasFlag("using")) continue;
            Slash *slash = new Slash(card->getSuit(), card->getNumber());
            slash->setSkillName(objectName());
            WrappedCard *wrapped = Sanguosha->getWrappedCard(id);
            wrapped->takeOver(slash);
            if (wrapped->getRealCard() != slash || !room->setPhysicalCardEffectSource(id, ctx)) continue;
            // Cleanup owns this exact applied transformation, never a later replacement of the same card.
            QVariantList receipts = target->getTag("jinchoufa_effects").toList();
            receipts << QVariantMap{{"card", id}, {"effect", wrapped->appliedPhysicalEffectSource()}};
            target->setTag("jinchoufa_effects", receipts);
            room->notifyUpdateCard(target, id, wrapped);
        }
        return ContinueEffects;
    }
};

class JinChoufa : public TriggerSkillV2
{
public:
    JinChoufa() : TriggerSkillV2("jinchoufa")
    {
        events << EventPhaseChanging;
        view_as_skill = new JinChoufaVS;
        global = true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || data.value<PhaseChangeStruct>().to != Player::NotActive) return true;
        const QVariantList receipts = player->getTag("jinchoufa_effects").toList();
        player->removeTag("jinchoufa_effects");
        foreach (const QVariant &entry, receipts) {
            const QVariantMap receipt = entry.toMap();
            const int id = receipt.value("card", -1).toInt();
            const Card *card = Sanguosha->getCard(id);
            if (card && room->getCardOwner(id) == player && room->getCardPlace(id) == Player::PlaceHand
                && card->appliedPhysicalEffectSource() == receipt.value("effect").toMap())
                room->filterCards(player, QList<const Card *>() << card, true);
        }
        return true;
    }
};

class JinZhaoran : public TriggerSkillV2
{
public:
    JinZhaoran() : TriggerSkillV2("jinzhaoran") { events << EventPhaseStart; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Play) result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker->askForSkillInvoke(this)) return false;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        const QVariant phase = room->historyScopes().value("phase_id");
        if (amount <= 0 || phase.toLongLong() <= 0) return false;
        const int sequence = room->getTag("jinzhaoran_sequence").toInt() + 1;
        room->setTag("jinzhaoran_sequence", sequence);
        QVariantMap receipt{{"receipt", sequence}, {"amount", amount}, {"phase", phase}, {"suits", QStringList()},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        QVariantList receipts = target->getTag("jinzhaoran_effects").toList();
        receipts << receipt;
        target->setTag("jinzhaoran_effects", receipts);
        room->broadcastSkillInvoke(objectName());
        room->addPlayerMark(target, "HandcardVisible_ALL-PlayClear");
        if (!target->isKongcheng()) room->showAllCards(target);
        return false;
    }
};

class JinZhaoranEffect : public TriggerSkillV2
{
public:
    JinZhaoranEffect() : TriggerSkillV2("#jinzhaoran-effect")
    {
        events << CardsMoveOneTime << EventSkillInvoking << EventPhaseChanging;
        global = true;
        frequency = Compulsory;
    }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    QVariantMap receipt(const SkillContext &ctx) const
    {
        if (!ctx.owner) return QVariantMap();
        foreach (const QVariant &entry, ctx.owner->getTag("jinzhaoran_effects").toList())
            if (entry.toMap().value("receipt").toInt() == ctx.instanceID && ref(entry.toMap(), "source") == ctx.sourceRef
                && ref(entry.toMap(), "activation") == ref(ctx.extra_data.toMap(), "activation")) return entry.toMap();
        return QVariantMap();
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        const QVariantMap active = receipt(ctx);
        return ctx.owner && ctx.owner->isAlive() && !active.isEmpty() && active.value("phase") == room->historyScopes().value("phase_id");
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        const QVariantMap active = receipt(ctx);
        const QString suit = ctx.extra_data.toMap().value("suit").toString();
        return !active.isEmpty() && !suit.isEmpty() && !active.value("suits").toStringList().contains(suit);
    }
    void addUsage(const SkillContext &ctx) const override
    {
        if (!ctx.owner) return;
        QVariantList receipts = ctx.owner->getTag("jinzhaoran_effects").toList();
        for (int i = 0; i < receipts.length(); ++i) {
            QVariantMap active = receipts.at(i).toMap();
            if (active.value("receipt").toInt() != ctx.instanceID || !(ref(active, "source") == ctx.sourceRef)) continue;
            QStringList suits = active.value("suits").toStringList();
            const QString suit = ctx.extra_data.toMap().value("suit").toString();
            if (!suit.isEmpty() && !suits.contains(suit)) suits << suit;
            active.insert("suits", suits);
            receipts[i] = active;
        }
        ctx.owner->setTag("jinzhaoran_effects", receipts);
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override { return isSourceAvailable(room, ctx) && isUsable(ctx); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *phaseActor, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext active = data.value<SkillContext>();
            if (active.skill_name == objectName() && active.bypass_cost && !active.activationRef.isValid()) addUsage(active);
        } else if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().from == Player::Play) {
            foreach (ServerPlayer *player, room->getAllPlayers(true)) {
                QVariantList receipts = player->getTag("jinzhaoran_effects").toList();
                for (int i = receipts.length() - 1; i >= 0; --i)
                    if (finishedJinPlayPhase(room, receipts.at(i).toMap().value("phase"), phaseActor)) receipts.removeAt(i);
                player->setTag("jinzhaoran_effects", receipts);
            }
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != CardsMoveOneTime || !player || !player->isAlive()) return true;
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from != player || !move.from_places.contains(Player::PlaceHand)) return true;
        QStringList suits = move.last_hand_suits;
        suits.removeDuplicates();
        foreach (const QVariant &entry, player->getTag("jinzhaoran_effects").toList()) {
            const QVariantMap active = entry.toMap();
            if (active.value("phase") != room->historyScopes().value("phase_id") || !ref(active, "source").isValid()
                || !ref(active, "activation").isValid()) continue;
            foreach (const QString &suit, suits) {
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = ctx.invoker = ctx.initiator = player;
                ctx.sourceRef = ref(active, "source");
                ctx.instanceID = active.value("receipt").toInt();
                ctx.original_data = &data;
                ctx.current_event = event;
                QVariantMap state = active;
                state.insert("suit", suit);
                ctx.extra_data = state;
                ctx.setModifiedAmount(active.value("amount").toInt());
                if (isUsable(ctx)) contexts << ctx;
            }
        }
        return true;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *other, room->getOtherPlayers(ctx.invoker)) if (ctx.invoker->canDiscard(other, "he")) targets << other;
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, targets, "jinzhaoran", "@jinzhaoran-discard", true);
        QVariantMap state = ctx.extra_data.toMap();
        state.insert("draw", !target);
        ctx.extra_data = state;
        ctx.targets << (target ? target : ctx.invoker);
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        room->sendCompulsoryTriggerLog(ctx.invoker, "jinzhaoran", true, true);
        if (ctx.extra_data.toMap().value("draw").toBool()) { target->drawCards(amount, "jinzhaoran"); return false; }
        for (int i = 0; i < amount && target->isAlive() && ctx.invoker->isAlive() && ctx.invoker->canDiscard(target, "he"); ++i) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", "jinzhaoran", false, Card::MethodDiscard);
            if (id < 0 || room->getCardOwner(id) != target || Sanguosha->getCard(id)->hasFlag("using")
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || !ctx.invoker->canDiscard(target, id)) break;
            room->throwCard(id, "jinzhaoran", target, ctx.invoker);
        }
        return false;
    }
};

// Keep reserved draw-pile IDs scoped across target hooks and nested prompts.
static void revealJinYanxi(Room *room, ServerPlayer *actor, ServerPlayer *victim, SkillContext &ctx,
    int amount, const std::function<void(SkillContext &, ServerPlayer *)> &receive)
{
    for (int n = amount; n > 0 && actor->isAlive() && victim->isAlive() && !victim->isKongcheng(); --n) {
        const int hand = victim->getRandomHandCardId(); const QList<int> deck = room->getNCards(2);
        const auto cleanup = qScopeGuard([&] { room->clearAG(actor); QList<int> remaining;
            for (int id : deck) if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) remaining << id;
            room->returnToTopDrawPile(remaining); });
        QList<int> display = deck; display << hand; qsanShuffle(display);
        room->fillAG(display, actor); const int choice = room->askForAG(actor, display, false, "jinyanxi"); room->clearAG(actor);
        if (!display.contains(choice)) continue;
        QVariantList gained; for (int id : (choice == hand ? display : QList<int>{choice})) gained << id;
        SkillContext gain = ctx; gain.choice = "receive";
        gain.extra_data = QVariantMap{{"cards", gained}, {"hand", hand}, {"victim", victim->objectName()}, {"deck", ListI2V(deck)}};
        receive(gain, actor);
    }
}

static void receiveJinYanxi(Room *room, SkillContext &ctx, ServerPlayer *target, int amount)
{
    if (amount <= 0) return;
    const QVariantMap receipt = ctx.extra_data.toMap();
    ServerPlayer *victim = room->findPlayerByObjectName(receipt.value("victim").toString(), true);
    QList<int> deck, hand;
    for (const QVariant &value : receipt.value("cards").toList()) {
        const int id = value.toInt(); if (Sanguosha->getCard(id)->hasFlag("using")) continue;
        if (id == receipt.value("hand", -1).toInt()) {
            if (victim && room->getCardOwner(id) == victim && room->getCardPlace(id) == Player::PlaceHand) hand << id;
        } else if (receipt.value("deck").toList().contains(id) && room->getCardPlace(id) == Player::DrawPile
            && !room->getDrawPile().contains(id)) deck << id;
    }
    QList<CardsMoveStruct> moves;
    if (!hand.isEmpty()) moves << CardsMoveStruct(hand, victim, target, Player::PlaceHand, Player::PlaceHand,
        CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, target->objectName(), "jinyanxi", QString()));
    if (!deck.isEmpty()) moves << CardsMoveStruct(deck, target, Player::PlaceHand,
        CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, target->objectName(), "jinyanxi", QString()));
    if (!moves.isEmpty()) room->moveCardsAtomic(moves, false);
}

class JinShiren : public TriggerSkillV2
{
public:
    JinShiren() : TriggerSkillV2("jinshiren") { events << Appear; hide_skill = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        ServerPlayer *current = room->getCurrent();
        return player && player->isAlive() && player->hasSkill(this) && current && current != player && !current->isKongcheng()
            ? TriggerList{{player,{objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *current = room->getCurrent();
        if (!current || current == ctx.owner || current->isKongcheng() || !ctx.owner->askForSkillInvoke(this, current)) return false;
        ctx.targets = {current}; return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *actor, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "receive") receiveJinYanxi(room, ctx, target, getEffectiveAmount(ctx));
        else if (ctx.invoker) revealJinYanxi(room, ctx.invoker, target, ctx, getEffectiveAmount(ctx),
            [&](SkillContext &gain, ServerPlayer *recipient) { skillEffect(event, room, actor, gain, recipient); });
        return false;
    }
};

JinYanxiCard::JinYanxiCard() { setSkillName("jinyanxi"); }
bool JinYanxiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{ return targets.isEmpty() && to_select != Self && !to_select->isKongcheng(); }

class JinYanxiVS : public ViewAsSkillV2
{
public:
    JinYanxiVS() : ViewAsSkillV2("jinyanxi") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *candidate) const override
    { return request.initiator && candidate && candidate->isAlive() && candidate != request.initiator && targets.isEmpty() && !candidate->isKongcheng(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "JinYanxiCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        if (ctx.choice == "receive") receiveJinYanxi(room, ctx, target, getEffectiveAmount(ctx));
        else if (ctx.invoker) revealJinYanxi(room, ctx.invoker, target, ctx, getEffectiveAmount(ctx),
            [&](SkillContext &gain, ServerPlayer *recipient) { skillEffect(gain, recipient); });
        return ContinueEffects;
    }
};

class JinYanxi : public TriggerSkillV2
{
public:
    JinYanxi() : TriggerSkillV2("jinyanxi") { events << CardsMoveOneTime; global = true; view_as_skill = new JinYanxiVS; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.reason.m_skillName != objectName() || !player || move.to != player
            || move.to_place != Player::PlaceHand || player->getPhase() == Player::NotActive) return false;
        // This is an already-applied card property, independent of the live grant.
        QList<int> ids; for (int id : move.card_ids) if (room->getCardOwner(id) == player && room->getCardPlace(id) == Player::PlaceHand) ids << id;
        if (!ids.isEmpty()) room->ignoreCards(player, ids);
        return false;
    }
};

JinSanchenCard::JinSanchenCard() { setSkillName("jinsanchen"); }

class JinSanchenVS : public ViewAsSkillV2
{
public:
    JinSanchenVS() : ViewAsSkillV2("jinsanchen") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    int getMaxUsageLimit(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.owner || !ref.isValid()) return 0;
        ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return 0;
        const QVariantMap extra = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "extra_uses").toMap();
        const QString phase = holder->getRoom()->historyScopes().value("phase_id").toString();
        if (phase.isEmpty() || phase == "0") return 0;
        return 1 + (extra.value("phase").toString() == phase ? extra.value("count").toInt() : 0);
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "JinSanchenCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    {
        const auto &ref = request.activationRef;
        return request.initiator && candidate && candidate->isAlive() && selected.isEmpty() && ref.isValid()
            && !request.initiator->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "targets").toStringList().contains(candidate->objectName());
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.length() == 1; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *holder = room->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!ref.isValid()) return ContinueEffects;
        const QString phase = room->historyScopes().value("phase_id").toString();
        if (holder && holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) {
            QStringList targets = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "targets").toStringList();
            if (targets.contains(target->objectName())) return ContinueEffects;
            targets << target->objectName();
            holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "targets", targets);
        }
        // An accepted effect still resolves if its source grant retired during interception.
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return ContinueEffects;
        target->drawCards(3 * amount, objectName());
        if (!target->isAlive() || !target->canDiscard(target, "he")) return ContinueEffects;
        const Card *discarded = room->askForDiscard(target, objectName(), 3 * amount, 3 * amount, false, true, "jinsanchen-discard");
        if (!discarded) return ContinueEffects;
        QSet<int> types;
        foreach (int id, discarded->getSubcards()) {
            const int type = Sanguosha->getCard(id)->getTypeId();
            if (types.contains(type)) return ContinueEffects;
            types.insert(type);
        }
        if (types.isEmpty()) return ContinueEffects;
        if (target->isAlive()) target->drawCards(amount, objectName());
        if (holder && holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) {
            QVariantMap extra = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "extra_uses").toMap();
            const int count = extra.value("phase").toString() == phase ? extra.value("count").toInt() : 0;
            extra.insert("phase", phase);
            extra.insert("count", count + 1);
            holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "extra_uses", extra);
        }
        return ContinueEffects;
    }
};

class JinSanchen : public TriggerSkillV2
{
public:
    JinSanchen() : TriggerSkillV2("jinsanchen") { events << TurnStart; view_as_skill = new JinSanchenVS; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    void record(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const auto &ref = ctx.activationRef;
        if (ctx.owner && ref.isValid()) ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "targets", QStringList());
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return TriggerList(); }
};

class JinZhaotao : public TriggerSkillV2
{
public:
    JinZhaotao() : TriggerSkillV2("jinzhaotao")
    {
        events << EventPhaseStart << EventSkillInvoking;
        frequency = Wake;
        waked_skills = "jinpozhu";
    }
    LimitScope getLimitScope() const override { return Limit_Game; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    void record(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext active = ctx.original_data->value<SkillContext>();
        if (active.activationRef.ownerObjectName == ctx.activationRef.ownerObjectName
            && active.activationRef.key.skillName == ctx.activationRef.key.skillName
            && active.activationRef.key.instanceID == ctx.activationRef.key.instanceID) {
            if (active.bypass_cost) addUsage(active);
            room->setPlayerMark(ctx.owner, objectName(), 1);
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Start
            && (player->canWake(objectName()) || room->countHistoryCards(player, "game", "JinSanchenCard") >= 3))
            result[player] << objectName();
        return usableJinCandidates(this, room, event, data, result);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const int count = room->countHistoryCards(ctx.owner, "game", "JinSanchenCard");
        if (count < 3 && !ctx.owner->canWake(objectName())) return false;
        ctx.extra_data = count;
        ctx.targets = QList<ServerPlayer *>() << ctx.invoker;
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (ctx.extra_data.toInt() >= 3) {
            LogMessage log;
            log.type = "#JinZhaotaoWake";
            log.from = ctx.invoker;
            log.arg = QString::number(ctx.extra_data.toInt());
            log.arg2 = objectName();
            room->sendLog(log);
        }
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox(ctx.invoker, objectName());
        // This permanent awakening outcome is independent of the source instance's later lifetime.
        if (room->changeMaxHpForAwakenSkill(target, -amount, objectName())) room->acquireSkill(target, "jinpozhu");
        return false;
    }
};

class JinPozhuVS : public ViewAsSkillV2
{
public:
    JinPozhuVS() : ViewAsSkillV2("jinpozhu", 1) { response_or_use = true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const auto &ref = request.activationRef;
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY || !ref.isValid()
            || request.initiator->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "locked").toBool()) return false;
        Chuqibuyi card(Card::NoSuit, 0);
        card.setSkillName(objectName());
        return card.isAvailable(request.initiator);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *candidate) const override
    {
        return request.initiator && candidate && request.selectedCardIds.isEmpty() && !candidate->hasFlag("using")
            && candidate->getEffectiveId() >= 0 && (request.initiator->handCards().contains(candidate->getEffectiveId())
                || request.initiator->getHandPile().contains(candidate->getEffectiveId()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.length() != 1) return nullptr;
        ActiveSkillRequest selection = request;
        selection.selectedCardIds.clear();
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        if (!canSelectCard(selection, material)) return nullptr;
        Chuqibuyi *card = new Chuqibuyi(material->getSuit(), material->getNumber());
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }
    bool pay(Room *, SkillContext &, const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request)) return false;
        const Card *card = createCard(request);
        const bool valid = card != nullptr;
        delete card;
        return valid;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "Chuqibuyi"; }
};

class JinPozhu : public TriggerSkillV2
{
public:
    JinPozhu() : TriggerSkillV2("jinpozhu") { events << CardFinished << TurnStart; view_as_skill = new JinPozhuVS; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != TurnStart) return;
        const auto &ref = ctx.activationRef;
        if (ctx.owner && ref.isValid()) ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "locked", false);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (event != CardFinished) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isKindOf("Chuqibuyi") || !use.from || !use.from->isAlive()
            || use.from->getPhase() != Player::Play || !use.activationRef.isValid()
            || use.activationRef.ownerObjectName != use.from->objectName() || use.activationRef.key.skillName != objectName()
            || !use.from->hasSkillInstance(objectName(), use.activationRef.key.instanceID)) return result;
        const QVariantMap damage = room->queryCardUseDamage();
        // Incomplete history cannot prove that this exact ordinary use failed to cause damage.
        if (damage.value("complete").toBool() && damage.value("attribution_complete").toBool()
            && damage.value("items").toList().isEmpty())
            result[use.from] << SkillInstanceUtils::formatName(objectName(), use.activationRef.key.instanceID);
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = QList<ServerPlayer *>() << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *) const override
    {
        const auto &ref = ctx.activationRef;
        if (getEffectiveAmount(ctx) > 0 && ctx.owner && ref.isValid() && ctx.owner->hasSkillInstance(ref.key.skillName, ref.key.instanceID))
            ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "locked", true);
        return false;
    }
};

class JinZhongyun : public TriggerSkillV2
{
public:
    JinZhongyun() : TriggerSkillV2("jinzhongyun")
    {
        events << Damaged << HpRecover << CardsMoveOneTime << EventSkillInvoking;
        frequency = Compulsory;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    QString quotaKey(const SkillContext &ctx) const
    {
        return ctx.original_data && ctx.original_data->canConvert<CardsMoveOneTimeStruct>() ? "move_turn" : "hp_turn";
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.activationRef.isValid()) return false;
        const QString turn = ctx.owner->getRoom()->historyScopes().value("turn_id").toString();
        const auto &key = ctx.activationRef.key;
        return !turn.isEmpty() && turn != "0"
            && !ctx.owner->getSkillInstanceStateValue(key.skillName, key.instanceID, quotaKey(ctx)).toStringList().contains(turn);
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const auto &ref = getUsageRef(ctx);
        if (!ctx.owner || !ref.isValid()) return;
        ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (holder && holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) {
            QStringList used = holder->getSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,quotaKey(ctx)).toStringList();
            const QString turn = holder->getRoom()->historyScopes().value("turn_id").toString();
            if (!used.contains(turn)) used << turn;
            holder->setSkillInstanceStateValue(ref.key.skillName,ref.key.instanceID,quotaKey(ctx),used);
        }
    }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking) return;
        const SkillContext active = ctx.original_data->value<SkillContext>();
        // Waiving payment never waives the quota, including an intercepted/skipped effect.
        if (active.bypass_cost && active.activationRef.ownerObjectName == ctx.activationRef.ownerObjectName
            && active.activationRef.key.skillName == ctx.activationRef.key.skillName
            && active.activationRef.key.instanceID == ctx.activationRef.key.instanceID)
            addUsage(active);
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event != Damaged && event != HpRecover && event != CardsMoveOneTime) return result;
        if (!player || !player->isAlive() || !player->hasSkill(this) || !player->hasTurn()
            || player->getHp() != player->getHandcardNum()) return result;
        if (event == CardsMoveOneTime) {
            if (room->getTag("FirstRound").toBool()) return result;
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (!((move.from == player && move.from_places.contains(Player::PlaceHand))
                || (move.to == player && move.to_place == Player::PlaceHand))) return result;
        }
        result[player] << objectName();
        return usableJinCandidates(this, room, event, data, result);
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const bool move = event == CardsMoveOneTime;
        QStringList choices;
        if (move || ctx.invoker->isWounded()) choices << (move ? "draw" : "recover");
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *target, room->getOtherPlayers(ctx.invoker)) {
            if (move ? ctx.invoker->canDiscard(target, "he") : ctx.invoker->inMyAttackRange(target))
                targets << target;
        }
        if (!targets.isEmpty()) choices << (move ? "discard" : "damage");
        if (choices.isEmpty()) return false;
        const QString choice = room->askForChoice(ctx.invoker, objectName(), choices.join("+"));
        ServerPlayer *target = ctx.invoker;
        if (choice == "discard" || choice == "damage")
            target = room->askForPlayerChosen(ctx.invoker, targets, choice == "discard" ? "jinzhongyun_discard" : objectName(),
                choice == "discard" ? "@jinzhongyun-discard" : "@jinzhongyun-damage");
        if (!target) return false;
        ctx.extra_data = choice;
        ctx.targets << target;
        return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        // The two branches reserve independent quotas before nested damage/draw events.
        addUsage(ctx);
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->sendCompulsoryTriggerLog(ctx.invoker, this);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QString choice = ctx.extra_data.toString();
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (choice == "draw") target->drawCards(amount, objectName());
        else if (choice == "recover") room->recover(target, RecoverStruct(objectName(), ctx.invoker, amount));
        else if (choice == "damage") room->damage(DamageStruct(objectName(), ctx.invoker, target, amount));
        else for (int i = 0; i < amount && target->isAlive() && ctx.invoker->isAlive()
            && ctx.invoker->canDiscard(target, "he"); ++i) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard);
            if (id < 0) break;
            room->throwCard(id, objectName(), target, ctx.invoker);
        }
        return false;
    }
};
class JinShenpin : public TriggerSkillV2
{
public:
    JinShenpin() : TriggerSkillV2("jinshenpin") { events << AskForRetrial; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        JudgeStruct *judge = data.value<JudgeStruct *>();
        if (player && player->isAlive() && player->hasSkill(this) && !player->isNude()
            && judge && judge->who && judge->card) result[player] << objectName();
        return result;
    }
    bool materialValid(Room *room, const SkillContext &ctx) const
    {
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        const int id = ctx.extra_data.toMap().value("id", -1).toInt();
        if (!judge || !judge->card || id < 0 || room->getCardOwner(id) != ctx.invoker
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
        const Card *card = Sanguosha->getCard(id);
        return !card->hasFlag("using") && !ctx.invoker->isCardLimited(card, Card::MethodResponse) && ((card->isRed() && judge->card->isBlack()) || (card->isBlack() && judge->card->isRed()));
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (!judge || !judge->who || !judge->card) return false;
        const QString color = judge->card->isRed() ? "black" : judge->card->isBlack() ? "red" : QString();
        if (color.isEmpty()) return false;
        const QString prompt = QStringList({"@jinshenpin-card", judge->who->objectName(), objectName(), judge->reason,
            QString::number(judge->card->getEffectiveId())}).join(":");
        // Retrial owns the atomic card movement; selection must not consume the response material yet.
        const Card *card = room->askForCard(ctx.invoker, ".|" + color, prompt, *ctx.original_data,
            Card::MethodNone, judge->who, true, objectName());
        if (!card || (card->isVirtualCard() && card->subcardsLength() != 1)) return false;
        QVariantMap details;
        details.insert("id", card->getEffectiveId());
        ctx.extra_data = details;
        ctx.targets = QList<ServerPlayer *>() << judge->who;
        return materialValid(room, ctx);
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override { return materialValid(room, ctx); }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        Q_UNUSED(event);
        Q_UNUSED(owner);
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (getEffectiveAmount(ctx) <= 0 || !judge || judge->who != target || !materialValid(room, ctx)) return false;
        const Card *card = Sanguosha->getCard(ctx.extra_data.toMap().value("id").toInt());
        room->broadcastSkillInvoke(objectName());
        room->retrial(card, ctx.invoker, judge, objectName(), false);

        return false;
    }
};

class JinGaoling : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    JinGaoling() : TriggerSkillV2("jingaoling") { events << Appear; hide_skill = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(this) || !room->hasCurrent() || room->getCurrent() == player)
            return result;
        foreach (ServerPlayer *target, room->getAlivePlayers()) {
            if (target->isWounded()) { result[player] << objectName(); break; }
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<ServerPlayer *> wounded;
        foreach (ServerPlayer *target, room->getAlivePlayers()) if (target->isWounded()) wounded << target;
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, wounded, objectName(), "@jingaoling-invoke", true, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(this);
        const int amount = getEffectiveAmount(ctx);
        if (amount > 0) room->recover(target, RecoverStruct(objectName(), ctx.invoker, amount));
        return false;
    }
};
class JinQimei : public TriggerSkillV2
{
public:
    JinQimei() : TriggerSkillV2("jinqimei") { events << EventPhaseStart; }
    static SkillInstanceRef receiptRef(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Start)
            result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, room->getOtherPlayers(ctx.invoker),
            objectName(), "@jinqimei-invoke", true, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0 || target == ctx.invoker) return false;
        ctx.invoker->peiyin(this);
        const int sequence = room->getTag("jinqimei_receipt_sequence").toInt() + 1;
        room->setTag("jinqimei_receipt_sequence", sequence);
        // An applied pair belongs to this activation, independently of later grant removal/reacquisition.
        QVariantMap receipt{{"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID},
            {"receipt", sequence}, {"actor", ctx.invoker->objectName()}, {"partner", target->objectName()}, {"amount", amount}};
        QVariantList receipts = room->getTag("jinqimei_effects").toList();
        receipts << receipt;
        room->setTag("jinqimei_effects", receipts);
        room->setPlayerMark(ctx.invoker, "&jinqimei_self+#" + target->objectName(), 1);
        room->setPlayerMark(target, "&jinqimei+#" + ctx.invoker->objectName(), 1);
        return false;
    }
};

class JinQimeiEffect : public TriggerSkillV2
{
public:
    JinQimeiEffect() : TriggerSkillV2("#jinqimei-effect")
    {
        events << EventPhaseStart << HpChanged << Death << CardsMoveOneTime << EventSkillInvoking;
        global = true;
        frequency = Compulsory;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        const QVariantMap pending = ctx.extra_data.toMap();
        foreach (const QVariant &entry, room->getTag("jinqimei_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("receipt") != pending.value("receipt")) continue;
            ServerPlayer *actor = room->findPlayerByObjectName(receipt.value("actor").toString(), true);
            ServerPlayer *partner = room->findPlayerByObjectName(receipt.value("partner").toString(), true);
            return actor && partner && actor->isAlive() && partner->isAlive() && (ctx.owner == actor || ctx.owner == partner)
                && JinQimei::receiptRef(receipt, "source") == ctx.sourceRef
                && JinQimei::receiptRef(receipt, "activation") == JinQimei::receiptRef(pending, "activation");
        }
        return false;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override { return isSourceAvailable(room, ctx); }
    static bool commitQuota(Room *room, const SkillContext &ctx)
    {
        const QVariantMap pending = ctx.extra_data.toMap();
        const QString key = pending.value("quota_key").toString();
        const QVariant turn = pending.value("turn");
        if ((key != "hp_turn" && key != "hand_turn") || turn.toLongLong() <= 0
            || turn != room->historyScopes().value("turn_id")) return false;
        QVariantList receipts = room->getTag("jinqimei_effects").toList();
        for (int i = 0; i < receipts.length(); ++i) {
            QVariantMap receipt = receipts.at(i).toMap();
            if (receipt.value("receipt") != pending.value("receipt")) continue;
            QVariantList used = receipt.value(key).toList();
            if (used.contains(turn)) return false;
            used << turn; receipt.insert(key, used);
            receipts[i] = receipt;
            room->setTag("jinqimei_effects", receipts);
            return true;
        }
        return false;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext active = data.value<SkillContext>();
            if (active.skill_name == objectName() && active.bypass_cost && isSourceAvailable(room, active)) commitQuota(room, active);
            return true;
        }
        if (!player || !((event == EventPhaseStart && player->getPhase() == Player::RoundStart)
            || (event == Death && data.value<DeathStruct>().who == player))) return true;
        QVariantList kept, expired;
        for (const QVariant &value : room->getTag("jinqimei_effects").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("actor").toString() == player->objectName()
                || (event == Death && receipt.value("partner").toString() == player->objectName())) expired << value;
            else kept << value;
        }
        room->setTag("jinqimei_effects",kept);
        for (const QVariant &value : expired) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("actor").toString(),true);
            ServerPlayer *partner = room->findPlayerByObjectName(receipt.value("partner").toString(),true);
            if (owner) room->setPlayerMark(owner,"&jinqimei_self+#"+receipt.value("partner").toString(),0);
            if (partner) room->setPlayerMark(partner,"&jinqimei+#"+receipt.value("actor").toString(),0);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (!player || !player->isAlive() || (event != HpChanged && event != CardsMoveOneTime)) return true;
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (!((move.to == player && move.to_place == Player::PlaceHand)
                || (move.from == player && move.from_places.contains(Player::PlaceHand)))) return true;
        }
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return true;
        const QString key = event == HpChanged ? "hp_turn" : "hand_turn";
        foreach (const QVariant &entry, room->getTag("jinqimei_effects").toList()) {
            QVariantMap receipt = entry.toMap();
            const QString actor = receipt.value("actor").toString(), partner = receipt.value("partner").toString();
            if (player->objectName() != actor && player->objectName() != partner) continue;
            ServerPlayer *drawer = room->findPlayerByObjectName(player->objectName() == actor ? partner : actor);
            if (!drawer || !drawer->isAlive() || receipt.value(key).toList().contains(turn) || receipt.value("amount").toInt() <= 0) continue;
            if (event == HpChanged ? player->getHp() != drawer->getHp() : player->getHandcardNum() != drawer->getHandcardNum()) continue;
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = drawer;
            ctx.sourceRef = JinQimei::receiptRef(receipt, "source");
            // No activationRef: this continues an applied pair without re-revealing its original grant.
            if (!ctx.sourceRef.isValid() || !JinQimei::receiptRef(receipt, "activation").isValid()) continue;
            ctx.instanceID = receipt.value("receipt").toInt();
            ctx.original_data = &data;
            ctx.current_event = event;
            receipt.insert("quota_key", key);
            receipt.insert("turn", turn);
            ctx.extra_data = receipt;
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            contexts << ctx;
        }
        return true;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QVariantMap pending = ctx.extra_data.toMap();
        foreach (const QVariant &entry, room->getTag("jinqimei_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("receipt") == pending.value("receipt")
                && !receipt.value(pending.value("quota_key").toString()).toList().contains(pending.value("turn"))) {
                ctx.targets << ctx.invoker;
                return true;
            }
        }
        return false;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override { return commitQuota(room, ctx); }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount > 0) target->drawCards(amount, "jinqimei");
        return false;
    }
};

class JinZhuiji : public TriggerSkillV2
{
public:
    JinZhuiji() : TriggerSkillV2("jinzhuiji") { events << EventPhaseStart; }
    static SkillInstanceRef receiptRef(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Play)
            result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.invoker->askForSkillInvoke(this)) return false;
        QStringList choices;
        if (ctx.invoker->isWounded()) choices << "recover";
        choices << "draw";
        ctx.extra_data = room->askForChoice(ctx.invoker, objectName(), choices.join("+"));
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        ctx.invoker->peiyin(this);
        const int sequence = room->getTag("jinzhuiji_receipt_sequence").toInt() + 1;
        room->setTag("jinzhuiji_receipt_sequence", sequence);
        // Record the accepted benefit's later obligation before its nested recovery/card movement.
        QVariantMap receipt{{"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID},
            {"receipt", sequence}, {"amount", amount}, {"choice", ctx.extra_data.toString()},
            {"phase", room->historyScopes().value("phase_id")}};
        QVariantList receipts = target->getTag("jinzhuiji_effects").toList();
        receipts << receipt;
        target->setTag("jinzhuiji_effects", receipts);
        if (ctx.extra_data.toString() == "recover") room->recover(target, RecoverStruct(objectName(), ctx.invoker, amount));
        else target->drawCards(2 * amount, objectName());
        return false;
    }
};

class JinZhuijiEffect : public TriggerSkillV2
{
public:
    JinZhuijiEffect() : TriggerSkillV2("#jinzhuiji-effect")
    {
        events << EventPhaseEnd << EventPhaseChanging;
        frequency = Compulsory;
        global = true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        const QVariantMap pending = ctx.extra_data.toMap();
        foreach (const QVariant &entry, ctx.owner->getTag("jinzhuiji_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("receipt") == pending.value("receipt")
                && receipt.value("phase") == room->historyScopes().value("phase_id")
                && JinZhuiji::receiptRef(receipt, "source") == ctx.sourceRef
                && JinZhuiji::receiptRef(receipt, "activation") == JinZhuiji::receiptRef(pending, "activation")) return true;
        }
        return false;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override { return isSourceAvailable(room, ctx); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().from == Player::Play) {
            foreach (ServerPlayer *recipient, room->getAllPlayers(true)) {
                QVariantList receipts = recipient->getTag("jinzhuiji_effects").toList();
                for (int i = receipts.length() - 1; i >= 0; --i)
                    if (finishedJinPlayPhase(room, receipts.at(i).toMap().value("phase"), player)) receipts.removeAt(i);
                recipient->setTag("jinzhuiji_effects", receipts);
            }
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseEnd || !player || !player->isAlive() || player->getPhase() != Player::Play) return true;
        const QVariant phase = room->historyScopes().value("phase_id");
        if (phase.toLongLong() <= 0) return true;
        // A target interceptor may redirect the benefit; its recipient still pays at this original phase end.
        foreach (ServerPlayer *recipient, room->getAlivePlayers()) {
            foreach (const QVariant &entry, recipient->getTag("jinzhuiji_effects").toList()) {
                const QVariantMap receipt = entry.toMap();
                if (receipt.value("phase") != phase || receipt.value("amount").toInt() <= 0) continue;
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = ctx.invoker = ctx.initiator = recipient;
                ctx.sourceRef = JinZhuiji::receiptRef(receipt, "source");
                // The original activation is provenance in the receipt, not a fresh activation/reveal requirement.
                if (!ctx.sourceRef.isValid() || !JinZhuiji::receiptRef(receipt, "activation").isValid()) continue;
                ctx.instanceID = receipt.value("receipt").toInt();
                ctx.original_data = &data;
                ctx.current_event = event;
                ctx.extra_data = receipt;
                ctx.setModifiedAmount(receipt.value("amount").toInt());
                contexts << ctx;
            }
        }
        return true;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        const QVariantMap pending = ctx.extra_data.toMap();
        QVariantList receipts = ctx.owner->getTag("jinzhuiji_effects").toList();
        for (int i = receipts.length() - 1; i >= 0; --i)
            if (receipts.at(i).toMap().value("receipt") == pending.value("receipt")) receipts.removeAt(i);
        ctx.owner->setTag("jinzhuiji_effects", receipts);
        if (pending.value("choice").toString() == "recover") {
            if (target->canDiscard(target, "he")) room->askForDiscard(target, "jinzhuiji", 2 * amount, 2 * amount, false, true);
        } else room->loseHp(HpLostStruct(target, amount, "jinzhuiji", ctx.invoker));
        return false;
    }
};

GuoPackage::GuoPackage()
	: Package("guo")
{

	General *jin_simazhao = new General(this, "jin_simazhao$", "jin", 3);
	jin_simazhao->addSkill(new JinTuishi);
	jin_simazhao->addSkill(new JinChoufa);
	jin_simazhao->addSkill(new JinZhaoran);
	jin_simazhao->addSkill(new JinZhaoranEffect);
	jin_simazhao->addSkill(new Skill("jinchengwu$", Skill::Compulsory));
	related_skills.insert("jinzhaoran", "#jinzhaoran-effect");

	General *jin_wangyuanji = new General(this, "jin_wangyuanji", "jin", 3, false);
	jin_wangyuanji->addSkill(new JinShiren);
	jin_wangyuanji->addSkill(new JinYanxi);

	General *jin_duyu = new General(this, "jin_duyu", "jin", 4);
	jin_duyu->addSkill(new JinSanchen);
	jin_duyu->addSkill(new JinZhaotao);
	jin_duyu->addRelateSkill("jinpozhu");

	General *jin_weiguan = new General(this, "jin_weiguan", "jin", 3);
	jin_weiguan->addSkill(new JinZhongyun);
	jin_weiguan->addSkill(new JinShenpin);

	General *jin_xuangongzhu = new General(this, "jin_xuangongzhu", "jin", 3, false);
	jin_xuangongzhu->addSkill(new JinGaoling);
	jin_xuangongzhu->addSkill(new JinQimei);
	jin_xuangongzhu->addSkill(new JinQimeiEffect);
	jin_xuangongzhu->addSkill(new JinZhuiji);
	jin_xuangongzhu->addSkill(new JinZhuijiEffect);
	related_skills.insert("jinqimei", "#jinqimei-effect");
	related_skills.insert("jinzhuiji", "#jinzhuiji-effect");

	addMetaObject<JinChoufaCard>();
	addMetaObject<JinYanxiCard>();
	addMetaObject<JinSanchenCard>();

	skills << new JinPozhu;
}
ADD_PACKAGE(Guo)


static QStringList jinBolanChoices(ServerPlayer *player, int maximum)
	{
		QStringList skill_names, skills;
		foreach (QString skn, Sanguosha->getSkillNames()) {
			if (skn == "jinbolan" || skill_names.contains(skn)||skn.startsWith("&")) continue;
			if (Sanguosha->getViewAsSkill(skn)==nullptr) continue;
			if (player->hasSkill(skn, true)) continue;

			QString translation = Sanguosha->getSkill(skn)->getDescription();
			if (!translation.contains("出牌阶段限一次，") && !translation.contains("阶段技，") && !translation.contains("出牌阶段限一次。")
					&& !translation.contains("阶段技。")) continue;
			if (translation.contains("，出牌阶段限一次") || translation.contains("，阶段技") || translation.contains("（出牌阶段限一次") ||
					translation.contains("（阶段技")) continue;

			skill_names << skn;
		}
		qsanShuffle(skill_names);

		for (int i = 0; i < maximum; i++) {
			if (skill_names.isEmpty()) break;
			skills << skill_names.takeFirst();
		}

		return skills;
	}

static void grantJinBolan(Room *room, ServerPlayer *recipient, const QString &name, const SkillContext &ctx)
{
    const QVariant phase = room->historyScopes().value("phase_id");
    if (!recipient || recipient->isDead() || recipient->getPhase() != Player::Play || phase.toLongLong() <= 0) return;
    room->acquireSkillFromEffect(recipient, name, ctx, [&](int id) {
        QVariantList receipts = room->getTag("jinbolan_grants").toList();
        receipts << QVariantMap{{"owner", recipient->objectName()}, {"skill", name}, {"id", id}, {"phase", phase}};
        room->setTag("jinbolan_grants", receipts);
    });
}

class JinBolan : public TriggerSkillV2
{
public:
    JinBolan() : TriggerSkillV2("jinbolan")
    { global = true; events << EventPhaseStart << EventPhaseEnd << EventPhaseChanging << EventAcquireSkill << EventLoseSkill << Death; frequency = Frequent; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *actor, QVariant &data) const override
    {
        const QVariant phase = room->historyScopes().value("phase_id");
        const bool expire = actor && ((event == EventPhaseEnd && actor->getPhase() == Player::Play)
            || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().from == Player::Play));
        const QString dead = event == Death && data.value<DeathStruct>().who ? data.value<DeathStruct>().who->objectName() : QString();
        for (const QString &key : {QStringLiteral("jinbolan_grants"), QStringLiteral("jinbolan_attached")}) {
            QVariantList kept, retired;
            for (const QVariant &value : room->getTag(key).toList()) {
                const QVariantMap row = value.toMap();
                const bool end = expire && row.value("owner").toString() == actor->objectName()
                    && (event == EventPhaseEnd ? row.value("phase") == phase : finishedJinPlayPhase(room, row.value("phase"), actor));
                if (end || (!dead.isEmpty() && row.value("owner").toString() == dead)) retired << row; else kept << row;
            }
            // Remove receipts before detach notifications can issue another grant.
            room->setTag(key, kept);
            for (const QVariant &value : retired) {
                const QVariantMap row = value.toMap(); ServerPlayer *holder = room->findPlayerByObjectName(row.value("owner").toString(), true);
                const QString skill = key == "jinbolan_attached" ? "jinbolan_skill" : row.value("skill").toString();
                const int id = row.value("id").toInt();
                if (holder && holder->hasSkillInstance(skill, id)) {
                    if (key == "jinbolan_attached") room->detachAttachedSkill(SkillInstanceRef(holder->objectName(), SkillInstanceKey(skill,id)));
                    else room->detachSkillFromPlayer(holder, SkillInstanceUtils::formatName(skill,id));
                }
            }
        }
        if (expire || phase.toLongLong() <= 0 || (event != EventPhaseStart && event != EventAcquireSkill)) return false;
        const QString phaseOwner = room->historyEvent(phase.toLongLong()).value("data").toMap().value("player").toString();
        for (ServerPlayer *holder : room->getAlivePlayers()) {
            if (holder->getPhase() != Player::Play || holder->objectName() != phaseOwner) continue;
            for (ServerPlayer *provider : room->getOtherPlayers(holder)) for (int id : provider->getValidSkillInstanceIds(objectName())) {
                const SkillInstanceRef parent(provider->objectName(), SkillInstanceKey(objectName(),id));
                const SkillInstanceRef child = room->attachSkillToPlayer(holder,"jinbolan_skill",parent);
                if (!child.isValid()) continue;
                const QVariantMap row{{"owner", holder->objectName()}, {"id", child.key.instanceID}, {"phase", phase}};
                QVariantList rows = room->getTag("jinbolan_attached").toList(); if (!rows.contains(row)) rows << row;
                room->setTag("jinbolan_attached", rows);
            }
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &) const override
    { return event == EventPhaseStart && player && player->isAlive() && player->hasSkill(this) && player->getPhase() == Player::Play
        ? TriggerList{{player,{objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!ctx.owner->askForSkillInvoke(objectName()+"$-1")) return false; ctx.targets = {ctx.owner}; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QStringList choices = jinBolanChoices(target, 3 * getEffectiveAmount(ctx));
        for (int n = getEffectiveAmount(ctx); n > 0 && target->isAlive() && !choices.isEmpty(); --n) {
            const QString choice = room->askForChoice(target, objectName(), choices.join("+"));
            if (!choices.contains(choice)) break; choices.removeOne(choice);
            grantJinBolan(room, target, choice, ctx);
        }
        return false;
    }
};

JinBolanSkillCard::JinBolanSkillCard() { setSkillName("jinbolan_skill"); }
bool JinBolanSkillCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{ return targets.isEmpty() && to_select != Self && to_select->hasSkill("jinbolan"); }

class JinBolanSkill : public ViewAsSkillV2
{
public:
    JinBolanSkill() : ViewAsSkillV2("jinbolan_skill") { attached_lord_skill = true; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "JinBolanSkillCard"; }
    static SkillInstanceRef provider(const ActiveSkillRequest &request)
    {
        const SkillInstance *instance = request.initiator ? request.initiator->findSkillInstance(request.activationRef.key.skillName, request.activationRef.key.instanceID) : nullptr;
        return instance ? instance->parentRef : SkillInstanceRef();
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && provider(request).isValid(); }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *candidate) const override
    {
        const auto parent = provider(request);
        return request.initiator && candidate && candidate->isAlive() && candidate != request.initiator && targets.isEmpty()
            && parent.key.skillName == "jinbolan" && candidate->objectName() == parent.ownerObjectName
            && candidate->getValidSkillInstanceIds("jinbolan").contains(parent.key.instanceID);
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override { return targets.size() == 1; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    { if (!ctx.initiator || ctx.initiator->isDead()) return false; room->loseHp(HpLostStruct(ctx.initiator,1,"jinbolan",ctx.initiator)); return true; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom(); const int amount = getEffectiveAmount(ctx); if (amount <= 0) return ContinueEffects;
        if (ctx.choice == "grant") { grantJinBolan(room,target,ctx.extra_data.toString(),ctx); return ContinueEffects; }
        if (!ctx.invoker || ctx.invoker->isDead()) return ContinueEffects;
        QStringList choices = jinBolanChoices(ctx.invoker,3*amount);
        for (int n = amount; n > 0 && ctx.invoker->isAlive() && target->isAlive() && !choices.isEmpty(); --n) {
            const QString choice = room->askForChoice(target,objectName(),choices.join("+"),QVariant::fromValue(ctx.invoker));
            if (!choices.contains(choice)) break; choices.removeOne(choice);
            SkillContext grant = ctx; grant.choice = "grant"; grant.extra_data = choice; skillEffect(grant,ctx.invoker);
        }
        return ContinueEffects;
    }
};

class JinYifa : public TriggerSkillV2
{
public:
    JinYifa() : TriggerSkillV2("jinyifa")
    {
        events << TargetSpecified << EventPhaseChanging;
        frequency = Compulsory;
        global = true;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        // This is an applied penalty, so its expiry must survive the originating grant's removal.
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::NotActive)
            room->setPlayerMark(player, "&jinyifa", 0);
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (event != TargetSpecified) return result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.from || !use.from->isAlive() || !use.card
            || !(use.card->isKindOf("Slash") || (use.card->isBlack() && use.card->isNDTrick()))) return result;
        foreach (ServerPlayer *owner, use.to)
            if (owner && owner->isAlive() && owner != use.from && owner->hasSkill(this)) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *user = ctx.original_data->value<CardUseStruct>().from;
        if (!user || !user->isAlive()) return false;
        ctx.targets << user;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.invoker, objectName(), true, true);
        const int amount = getEffectiveAmount(ctx);
        if (amount > 0) room->addPlayerMark(target, "&jinyifa", amount);
        return false;
    }
};

class JinYifaMax : public MaxCardsSkillV2
{
public:
    JinYifaMax() : MaxCardsSkillV2("#jinyifa") { setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.primary && ctx.primary->getMark("&jinyifa") > 0
            ? CorrectSkillResult::useAmount(-ctx.primary->getMark("&jinyifa") * ctx.currentAmount)
            : CorrectSkillResult::noEffect();
    }
};
class JinCanmou : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    JinCanmou() : TriggerSkillV2("jincanmou") { events << TargetSpecifying; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !use.card || !use.card->isNDTrick() || use.card->isKindOf("Collateral")) return result;
        foreach (ServerPlayer *other, room->getOtherPlayers(player))
            if (other->getHandcardNum() >= player->getHandcardNum()) return result;
        if (room->getCardTargets(player, use.card, use.to).isEmpty()) return result;
        foreach (ServerPlayer *owner, room->getAlivePlayers()) if (owner->hasSkill(this)) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.from || !use.from->isAlive()) return false;
        const QList<ServerPlayer *> candidates = room->getCardTargets(use.from, use.card, use.to);
        if (candidates.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, candidates, objectName(), "@jincanmou-target:" + use.card->objectName(), true, true);
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.to.contains(target)) {
            room->broadcastSkillInvoke(this);
            use.to << target;
            room->sortByActionOrder(use.to);
            ctx.original_data->setValue(use);
        }
        return false;
    }
};
class JinCongjian : public TriggerSkillV2
{
public:
    JinCongjian() : TriggerSkillV2("jincongjian") { events << TargetConfirming; }
    static SkillInstanceRef receiptRef(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!player || !player->isAlive() || !use.card || !use.card->isNDTrick() || use.card->isKindOf("Collateral")
            || use.to.length() != 1 || use.to.first() != player) return result;
        foreach (ServerPlayer *other, room->getOtherPlayers(player))
            if (other->getHp() >= player->getHp()) return result;
        foreach (ServerPlayer *owner, room->getOtherPlayers(player))
            if (owner->hasSkill(this) && (!use.from || use.from->canUse(use.card, owner, true))) result[owner] << objectName();
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || use.to.length() != 1 || use.to.contains(ctx.invoker)
            || !ctx.invoker->askForSkillInvoke(this, "jincongjian:" + use.card->objectName())) return false;
        ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (amount <= 0 || !use.card || use.to.length() != 1 || use.to.contains(target)
            || (use.from && !use.from->canUse(use.card, target, true))) return false;
        const qint64 useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (useId <= 0) return false;
        use.to << target;
        room->sortByActionOrder(use.to);
        ctx.original_data->setValue(use);
        const int sequence = room->getTag("jincongjian_receipt_sequence").toInt() + 1;
        room->setTag("jincongjian_receipt_sequence", sequence);
        // The later reward follows this accepted use and recipient, never a reusable Card flag.
        QVariantMap receipt{{"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
            {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
            {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID},
            {"receipt", sequence}, {"amount", amount}, {"use_id", QString::number(useId)}};
        QVariantList receipts = target->getTag("jincongjian_effects").toList();
        receipts << receipt;
        target->setTag("jincongjian_effects", receipts);
        return false;
    }
};

class JinCongjianEffect : public TriggerSkillV2
{
public:
    JinCongjianEffect() : TriggerSkillV2("#jincongjian-effect")
    {
        events << CardFinished << EventPhaseChanging;
        global = true;
        frequency = Compulsory;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        const QVariantMap pending = ctx.extra_data.toMap();
        foreach (const QVariant &entry, ctx.owner->getTag("jincongjian_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("receipt") == pending.value("receipt") && receipt.value("use_id") == pending.value("use_id")
                && JinCongjian::receiptRef(receipt, "source") == ctx.sourceRef
                && JinCongjian::receiptRef(receipt, "activation") == JinCongjian::receiptRef(pending, "activation")) return true;
        }
        return false;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override { return isSourceAvailable(room, ctx); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            foreach (ServerPlayer *player, room->getAllPlayers(true)) {
                QVariantList receipts = player->getTag("jincongjian_effects").toList();
                // A nested turn boundary must not retire the still-resolving outer use's reward.
                for (int i = receipts.length() - 1; i >= 0; --i)
                    if (room->historyEvent(receipts.at(i).toMap().value("use_id").toLongLong()).value("status").toString() == "finished")
                        receipts.removeAt(i);
                player->setTag("jincongjian_effects", receipts);
            }
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != CardFinished) return true;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.card->isNDTrick()) return true;
        const qint64 useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        if (useId <= 0) return true;
        const QVariantMap damage = room->queryCardUseDamage(useId);
        if (!damage.value("complete").toBool() || !damage.value("attribution_complete").toBool()) return true;
        QSet<QString> damaged;
        foreach (const QVariant &entry, damage.value("items").toList())
            damaged.insert(entry.toMap().value("data").toMap().value("to").toString());
        foreach (ServerPlayer *player, room->getAlivePlayers()) {
            if (!damaged.contains(player->objectName())) continue;
            foreach (const QVariant &entry, player->getTag("jincongjian_effects").toList()) {
                const QVariantMap receipt = entry.toMap();
                if (receipt.value("use_id").toLongLong() != useId || receipt.value("amount").toInt() <= 0) continue;
                SkillContext ctx;
                ctx.skill_name = objectName();
                ctx.owner = ctx.invoker = ctx.initiator = player;
                ctx.sourceRef = JinCongjian::receiptRef(receipt, "source");
                if (!ctx.sourceRef.isValid() || !JinCongjian::receiptRef(receipt, "activation").isValid()) continue;
                ctx.instanceID = receipt.value("receipt").toInt();
                ctx.original_data = &data;
                ctx.current_event = event;
                ctx.extra_data = receipt;
                ctx.setModifiedAmount(receipt.value("amount").toInt());
                contexts << ctx;
            }
        }
        return true;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.invoker; return true; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        QVariantList receipts = ctx.owner->getTag("jincongjian_effects").toList();
        const QVariant id = ctx.extra_data.toMap().value("receipt");
        for (int i = receipts.length() - 1; i >= 0; --i)
            if (receipts.at(i).toMap().value("receipt") == id) receipts.removeAt(i);
        ctx.owner->setTag("jincongjian_effects", receipts);
        target->drawCards(2 * amount, "jincongjian");
        return false;
    }
};

class JinXiongshu : public TriggerSkillV2
{
public:
    JinXiongshu() : TriggerSkillV2("jinxiongshu") { events << EventPhaseStart << EventSkillInvoking; }
    LimitScope getLimitScope() const override { return Limit_Round; }
    int getMaxUsageLimit(const SkillContext &) const override { return std::numeric_limits<int>::max(); }
    int paidCount(const SkillContext &ctx) const
    {
        ServerPlayer *holder = getUsageHolder(ctx);
        return holder ? holder->getMark(getUsageTagKey(ctx)) : -1;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx) && isUsable(ctx);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext active = data.value<SkillContext>();
            if (active.skill_name == objectName() && active.activationRef.isValid()) {
                if (active.bypass_cost) addUsage(active);
                if (active.owner) room->setPlayerMark(active.owner, "&jinxiongshu_num_lun", qMax(0, paidCount(active)));
            }
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventPhaseStart && player && player->isAlive() && player->getPhase() == Player::Play
            && !player->isKongcheng() && room->historyScopes().value("phase_id").toLongLong() > 0)
            foreach (ServerPlayer *owner, room->getOtherPlayers(player)) if (owner->hasSkill(this)) result[owner] << objectName() + "->" + player->objectName();
        return usableJinCandidates(this, room, event, data, result);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || !ctx.preferredTarget || !ctx.preferredTarget->isAlive() || ctx.preferredTarget->isKongcheng()) return false;
        const int count = paidCount(ctx);
        if (count < 0) return false;
        QVariantList ids;
        if (count == 0) {
            if (!ctx.invoker->askForSkillInvoke(this, ctx.preferredTarget)) return false;
        } else {
            const Card *chosen = room->askForExchange(ctx.invoker, objectName(), count, count, true,
                QString("@jinxiongshu-discard:%1::%2").arg(ctx.preferredTarget->objectName()).arg(count), true);
            if (!chosen || chosen->getSubcards().length() != count) return false;
            foreach (int id, chosen->getSubcards()) ids << id;
        }
        ctx.extra_data = QVariantMap{{"cost", count}, {"cards", ids}, {"phase", room->historyScopes().value("phase_id")}};
        ctx.targets << ctx.preferredTarget;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || paidCount(ctx) != ctx.extra_data.toMap().value("cost").toInt()) return false;
        QList<int> ids;
        foreach (const QVariant &entry, ctx.extra_data.toMap().value("cards").toList()) {
            const int id = entry.toInt();
            if (id < 0 || ids.contains(id) || room->getCardOwner(id) != ctx.invoker || Sanguosha->getCard(id)->hasFlag("using")
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || !ctx.invoker->canDiscard(ctx.invoker, id)) return false;
            ids << id;
        }
        if (ids.length() != paidCount(ctx)) return false;
        addUsage(ctx);
        if (!ids.isEmpty()) room->throwCard(ids, objectName(), ctx.invoker);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0 || target->isKongcheng() || !ctx.invoker->isAlive()) return false;
        const int id = room->askForCardChosen(ctx.invoker, target, "h", objectName());
        if (id < 0 || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand
            || Sanguosha->getCard(id)->hasFlag("using")) return false;
        const Card *card = Sanguosha->getCard(id);
        const QString name = card->isKindOf("Slash") ? QString("slash") : card->objectName();
        ctx.invoker->peiyin(this);
        room->showCard(target, id);
        const bool guess = ctx.invoker->askForSkillInvoke("jinxiongshu_guess",
            QString("jinxiongshu_guess:%1::%2").arg(target->objectName()).arg(name), false);
        const int sequence = room->getTag("jinxiongshu_sequence").toInt() + 1;
        room->setTag("jinxiongshu_sequence", sequence);
        const QVariantMap receipt{{"receipt", sequence}, {"phase", ctx.extra_data.toMap().value("phase")}, {"amount", amount},
            {"actor", target->objectName()}, {"guesser", ctx.invoker->objectName()}, {"card", id}, {"name", name}, {"guess", guess},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        QVariantList receipts = room->getTag("jinxiongshu_effects").toList();
        receipts << receipt;
        room->setTag("jinxiongshu_effects", receipts);
        return false;
    }
};

class JinXiongshuEffect : public TriggerSkillV2
{
public:
    JinXiongshuEffect() : TriggerSkillV2("#jinxiongshu")
    {
        events << EventPhaseEnd << EventPhaseChanging;
        global = true;
        frequency = Compulsory;
    }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        foreach (const QVariant &entry, room->getTag("jinxiongshu_effects").toList()) {
            const QVariantMap receipt = entry.toMap();
            if (receipt.value("receipt").toInt() == ctx.instanceID && ref(receipt, "source") == ctx.sourceRef
                && ref(receipt, "activation") == ref(ctx.extra_data.toMap(), "activation")) return true;
        }
        return false;
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override { return isSourceAvailable(room, ctx); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *phaseActor, QVariant &data) const override
    {
        if (event != EventPhaseChanging || data.value<PhaseChangeStruct>().from != Player::Play) return true;
        QVariantList receipts = room->getTag("jinxiongshu_effects").toList();
        for (int i = receipts.length() - 1; i >= 0; --i)
            if (finishedJinPlayPhase(room, receipts.at(i).toMap().value("phase"), phaseActor)) receipts.removeAt(i);
        room->setTag("jinxiongshu_effects", receipts);
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseEnd || !player || !player->isAlive() || player->getPhase() != Player::Play) return true;
        const QVariantMap history = room->queryCardHistory(player, "phase");
        if (!history.value("complete").toBool()) return true;
        QSet<QString> names;
        foreach (const QVariant &entry, history.value("items").toList()) {
            const QVariantMap card = entry.toMap();
            names.insert(card.value("classes").toList().contains("Slash") ? QString("slash") : card.value("name").toString());
        }
        foreach (const QVariant &entry, room->getTag("jinxiongshu_effects").toList()) {
            QVariantMap receipt = entry.toMap();
            if (receipt.value("phase") != room->historyScopes().value("phase_id") || receipt.value("actor").toString() != player->objectName()) continue;
            ServerPlayer *guesser = room->findPlayerByObjectName(receipt.value("guesser").toString());
            if (!guesser || !guesser->isAlive() || !ref(receipt, "source").isValid() || !ref(receipt, "activation").isValid()) continue;
            receipt.insert("correct", names.contains(receipt.value("name").toString()) == receipt.value("guess").toBool());
            SkillContext ctx;
            ctx.skill_name = objectName();
            ctx.owner = ctx.invoker = ctx.initiator = guesser;
            ctx.sourceRef = ref(receipt, "source");
            ctx.instanceID = receipt.value("receipt").toInt();
            ctx.extra_data = receipt;
            ctx.original_data = &data;
            ctx.current_event = event;
            ctx.setModifiedAmount(receipt.value("amount").toInt());
            contexts << ctx;
        }
        return true;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QVariantMap receipt = ctx.extra_data.toMap();
        ServerPlayer *recipient = receipt.value("correct").toBool() ? room->findPlayerByObjectName(receipt.value("actor").toString()) : ctx.invoker;
        if (!recipient || !recipient->isAlive()) return false;
        ctx.targets << recipient;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        QVariantList receipts = room->getTag("jinxiongshu_effects").toList();
        for (int i = receipts.length() - 1; i >= 0; --i)
            if (receipts.at(i).toMap().value("receipt").toInt() == ctx.instanceID) receipts.removeAt(i);
        room->setTag("jinxiongshu_effects", receipts);
        room->sendCompulsoryTriggerLog(ctx.invoker, "jinxiongshu");
        const QVariantMap receipt = ctx.extra_data.toMap();
        if (receipt.value("correct").toBool()) room->damage(DamageStruct("jinxiongshu", ctx.invoker, target, getEffectiveAmount(ctx)));
        else {
            const int id = receipt.value("card", -1).toInt();
            if (id >= 0 && room->getCardPlace(id) != Player::PlaceUnknown && !Sanguosha->getCard(id)->hasFlag("using")) room->obtainCard(target, id);
        }
        return false;
    }
};

class JinJianhui : public TriggerSkillV2
{
public:
    JinJianhui() : TriggerSkillV2("jinjianhui") { events << Damage << Damaged; frequency = Compulsory; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    static bool previousSource(Room *room, ServerPlayer *owner, QString *source)
    {
        const qint64 current = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong();
        if (current <= 0) return false;
        QVariantMap query{{"to", owner->objectName()}};
        source->clear();
        for (;;) {
            const QVariantMap page = room->queryActualDamage(query);
            if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return false;
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            foreach (const QVariant &entry, page.value("items").toList()) {
                const QVariantMap fact = entry.toMap();
                // Damage is already journaled before Damage/Damaged; exclude exactly this occurrence, including self-damage.
                if (fact.value("event_id").toLongLong() == current) continue;
                const QVariantMap details = fact.value("data").toMap();
                if (!details.contains("from")) return false;
                if (!details.value("from").toString().isEmpty()) *source = details.value("from").toString();
            }
            if (!page.value("has_more").toBool()) return true;
            query.insert("after", page.value("next_after"));
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(this)) return result;
        const DamageStruct damage = data.value<DamageStruct>();
        QString last;
        if (!previousSource(room, player, &last) || last.isEmpty()) return result;
        if (event == Damage && damage.from == player && damage.to && damage.to->objectName() == last)
            result[player] << objectName() + "->" + player->objectName();
        else if (event == Damaged && damage.to == player && damage.from && damage.from->isAlive()
            && damage.from->objectName() == last && damage.from->canDiscard(damage.from, "he"))
            result[player] << objectName() + "->" + damage.from->objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.preferredTarget || !ctx.preferredTarget->isAlive()) return false;
        ctx.extra_data = event == Damage;
        ctx.targets = QList<ServerPlayer *>() << ctx.preferredTarget;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        room->sendCompulsoryTriggerLog(ctx.invoker, this);
        if (ctx.extra_data.toBool()) target->drawCards(amount, objectName());
        else if (target->canDiscard(target, "he")) room->askForDiscard(target, objectName(), amount, amount, false, true);
        return false;
    }
};

JinBingxinCard::JinBingxinCard()
{
    setSkillName("jinbingxin");
	mute = true;
	handling_method = Card::MethodUse;
	//target_fixed = true;
}

bool JinBingxinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetFilter(targets, to_select, Self);
	}
	card = Self->getTag("jinbingxin").value<Card *>();
	return card && card->targetFilter(targets, to_select, Self);
}

bool JinBingxinCard::targetFixed() const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
		return true;
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetFixed();
	}
	card = Self ? Self->getTag("jinbingxin").value<Card *>() : nullptr;
	return card && card->targetFixed();
}

bool JinBingxinCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	Card *card = Sanguosha->cloneCard(user_string.split("+").first());
	if(card){
		card->deleteLater();
		return card->targetsFeasible(targets, Self);
	}
	card = Self->getTag("jinbingxin").value<Card *>();
	return card && card->targetsFeasible(targets, Self);
}


class JinBingxinVS : public ViewAsSkillV2
{
public:
    JinBingxinVS() : ViewAsSkillV2("jinbingxin") { response_or_use = true; }
    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo("jinbingxin", true, false); }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        const Player *player = request.initiator;
        if (!player || (request.reason != CardUseStruct::CARD_USE_REASON_PLAY
            && request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE)
            || player->getHandcardNum() != player->getHp()) return false;
        const QList<const Card *> hand = player->getHandcards();
        foreach (const Card *card, hand) if (!card->sameColorWith(hand.first())) return false;
        return !usableNames(request).isEmpty();
    }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.activationRef.isValid()) return false;
        // Admission before createCard has no card yet; the exact declaration gate checks the name.
        if (!ctx.use_card) return true;
        ActiveSkillRequest request;
        request.initiator = ctx.initiator ? ctx.initiator : ctx.owner;
        request.activationRef = ctx.activationRef;
        return allowDeclaration(request, ctx.use_card->objectName());
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const auto &ref = getUsageRef(ctx);
        if (!ref.isValid() || !ctx.use_card || !ctx.owner) return;
        ServerPlayer *holder = ctx.owner->getRoom()->findPlayerByObjectName(ref.ownerObjectName, true);
        if (!holder || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return;
        QStringList names = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "used_names").toStringList();
        if (!names.contains(ctx.use_card->objectName())) names << ctx.use_card->objectName();
        if (ctx.use_card->isKindOf("Slash") && !names.contains("normal_slash")) names << "normal_slash";
        holder->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "used_names", names);
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        if (!checkCustomUsage(ctx)) return false;
        // Reserve before nested reveal/use callbacks; committing again is idempotent.
        addUsage(ctx);
        return true;
    }
protected:
    bool allowDeclaration(const ActiveSkillRequest &request, const QString &name) const override
    {
        const auto &ref = request.activationRef;
        const Player *holder = request.initiator;
        if (!holder || !ref.isValid() || ref.ownerObjectName != holder->objectName()
            || !holder->hasSkillInstance(ref.key.skillName, ref.key.instanceID)) return false;
        const QStringList used = holder->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "used_names").toStringList();
        return !used.contains(name) && !(name == "slash" && used.contains("normal_slash"))
            && (name != "peach" || holder->getMark("Global_PreventPeach") == 0);
    }
};

class JinBingxin : public TriggerSkillV2
{
public:
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    JinBingxin() : TriggerSkillV2("jinbingxin")
    {
        events << EventSkillInvoking << EventPhaseChanging << TurnStart;
        view_as_skill = new JinBingxinVS;
    }
    SkillDialogInfo getDialogInfo() const override { return view_as_skill->getDialogInfo(); }
    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == TurnStart || (event == EventPhaseChanging && ctx.original_data->value<PhaseChangeStruct>().to == Player::NotActive))
            ctx.owner->removeSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "used_names");
        else if (event == EventSkillInvoking) {
            const SkillContext active = ctx.original_data->value<SkillContext>();
            // Accepted activation consumes its name even when payment or the draw effect is waived.
            if (active.use_card && active.use_card->isKindOf("BasicCard")
                && active.activationRef.ownerObjectName == ctx.activationRef.ownerObjectName
                && active.activationRef.key.skillName == ctx.activationRef.key.skillName
                && active.activationRef.key.instanceID == ctx.activationRef.key.instanceID)
                view_as_skill->addUsage(active);
        }
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        if (event != EventSkillInvoking) return result;
        const SkillContext active = data.value<SkillContext>();
        if (!active.use_card || !active.use_card->isKindOf("BasicCard") || active.activationRef.key.skillName != objectName()) return result;
        ServerPlayer *owner = room->findPlayerByObjectName(active.activationRef.ownerObjectName, true);
        if (owner && owner->hasSkillInstance(objectName(), active.activationRef.key.instanceID))
            result[owner] << SkillInstanceUtils::formatName(objectName(), active.activationRef.key.instanceID);
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const SkillContext active = ctx.original_data->value<SkillContext>();
        if (!active.invoker || !active.invoker->isAlive()) return false;
        ctx.targets << active.invoker;
        ctx.setModifiedAmount(static_cast<const ViewAsSkillV2 *>(view_as_skill)->getEffectiveAmount(active));
        return true;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};
JiePackage::JiePackage()
	: Package("jie_package")
{
	General *jin_zhongyan = new General(this, "jin_zhongyan", "jin", 3, false);
	jin_zhongyan->addSkill(new JinBolan);
	jin_zhongyan->addSkill(new JinYifa);
	jin_zhongyan->addSkill(new JinYifaMax);
	related_skills.insert("jinyifa", "#jinyifa");

	General *jin_xinchang = new General(this, "jin_xinchang", "jin", 3);
	jin_xinchang->addSkill(new JinCanmou);
	jin_xinchang->addSkill(new JinCongjian);
	jin_xinchang->addSkill(new JinCongjianEffect);
	related_skills.insert("jincongjian", "#jincongjian-effect");

	General *jin_jiachong = new General(this, "jin_jiachong", "jin", 3);
	jin_jiachong->addSkill(new JinXiongshu);
	jin_jiachong->addSkill(new JinXiongshuEffect);
	jin_jiachong->addSkill(new JinJianhui);
	related_skills.insert("jinxiongshu", "#jinxiongshu");

	General *jin_wangxiang = new General(this, "jin_wangxiang", "jin", 3);
	jin_wangxiang->addSkill(new JinBingxin);

	addMetaObject<JinBolanSkillCard>();
	addMetaObject<JinBingxinCard>();

	skills << new JinBolanSkill;
}
ADD_PACKAGE(Jie)

JinXuanbeiCard::JinXuanbeiCard() { setSkillName("jinxuanbei"); }

class JinXuanbeiVS : public ViewAsSkillV2
{
public:
    JinXuanbeiVS() : ViewAsSkillV2("jinxuanbei") {}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "JinXuanbeiCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &targets, const Player *target) const override
    {
        return request.initiator && target && target->isAlive() && target != request.initiator && targets.isEmpty() && !target->isAllNude();
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        return targets.length() == 1 && canSelectTarget(request, QList<const Player *>(), targets.first());
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0 || target->isAllNude() || !ctx.invoker->isAlive()) return ContinueEffects;
        Room *room = target->getRoom();
        const int id = room->askForCardChosen(ctx.invoker, target, "hej", objectName());
        if (id < 0 || room->getCardOwner(id) != target || Sanguosha->getCard(id)->hasFlag("using")
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip
                && room->getCardPlace(id) != Player::PlaceDelayedTrick)) return ContinueEffects;
        Slash *slash = new Slash(Card::SuitToBeDecided, -1);
        slash->addSubcard(id);
        slash->setSkillName("_jinxuanbei");
        slash->deleteLater();
        if (!target->canSlash(ctx.invoker, slash, false)) return ContinueEffects;
        const int sequence = room->getTag("jinxuanbei_sequence").toInt() + 1;
        room->setTag("jinxuanbei_sequence", sequence);
        // The actual card stores the granted draw obligation, independent of the original grant's lifetime.
        slash->setTag("jinxuanbei_effect", QVariantMap{{"receipt", sequence}, {"amount", getEffectiveAmount(ctx)},
            {"drawer", ctx.invoker->objectName()}, {"source_owner", ctx.sourceRef.ownerObjectName},
            {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName},
            {"activation_id", ctx.activationRef.key.instanceID}});
        room->useCardFromSkillEffect(CardUseStruct(slash, target, ctx.invoker), ctx, true);
        return ContinueEffects;
    }
};

class JinXuanbei : public TriggerSkillV2
{
public:
    JinXuanbei() : TriggerSkillV2("jinxuanbei") { events << CardFinished; view_as_skill = new JinXuanbeiVS; global = true; frequency = Compulsory; }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &contexts) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || use.useHistoryEventId <= 0) return true;
        const QVariantMap receipt = use.card->getTag("jinxuanbei_effect").toMap();
        ServerPlayer *drawer = room->findPlayerByObjectName(receipt.value("drawer").toString());
        if (!drawer || !drawer->isAlive() || !ref(receipt, "source").isValid() || !ref(receipt, "activation").isValid()
            || receipt.value("receipt").toInt() <= 0) return true;
        const QVariantMap damage = room->queryCardUseDamage(use.useHistoryEventId);
        if (!damage.value("complete").toBool() || !damage.value("attribution_complete").toBool()) return true;
        bool wounded = false;
        foreach (const QVariant &fact, damage.value("items").toList())
            if (fact.toMap().value("data").toMap().value("to").toString() == drawer->objectName()) wounded = true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = drawer;
        ctx.sourceRef = ref(receipt, "source");
        ctx.instanceID = receipt.value("receipt").toInt();
        ctx.current_event = event;
        ctx.original_data = &data;
        ctx.extra_data = receipt;
        ctx.setModifiedAmount(receipt.value("amount").toInt() * (wounded ? 2 : 1));
        contexts << ctx;
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        if (!ctx.original_data || !ctx.owner || !ctx.owner->isAlive()) return false;
        const Card *card = ctx.original_data->value<CardUseStruct>().card;
        const QVariantMap receipt = card ? card->getTag("jinxuanbei_effect").toMap() : QVariantMap();
        return receipt == ctx.extra_data.toMap() && receipt.value("receipt").toInt() > 0 && ref(receipt, "source") == ctx.sourceRef;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets << ctx.invoker; return true; }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const Card *card = ctx.original_data->value<CardUseStruct>().card;
        if (card) card->removeTag("jinxuanbei_effect");
        const int amount = getEffectiveAmount(ctx);
        if (amount > 0) target->drawCards(amount, objectName());
        return false;
    }
};

JinXianwanCard::JinXianwanCard()
{
    setSkillName("jinxianwan");
}

bool JinXianwanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		Card *c = Sanguosha->cloneCard(user_string.split("+").first());
		if (c) {
			c->setSkillName("jinxianwan");
			c->deleteLater();
			if (c->targetFixed())
				return c->isAvailable(Self);
		}
		return c && c->targetFilter(targets, to_select, Self);
	}

	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->deleteLater();
	slash->setSkillName("jinxianwan");
	return slash->targetFilter(targets, to_select, Self);
}

bool JinXianwanCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
	if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
		Card *c = Sanguosha->cloneCard(user_string.split("+").first());
		if (c) {
			c->setSkillName("jinxianwan");
			c->deleteLater();
		}
		return c && c->targetsFeasible(targets, Self);
	}

	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->deleteLater();
	slash->setSkillName("jinxianwan");
	return slash->targetsFeasible(targets, Self);
}





class JinXianwan : public ViewAsSkillV2
{
public:
    JinXianwan() : ViewAsSkillV2("jinxianwan") { response_or_use = true; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY)
            return request.initiator->isChained() && Slash::IsAvailable(request.initiator);
        if (request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE) return false;
        Card *card = Sanguosha->cloneCard(request.initiator->isChained() ? "slash" : "jink");
        const bool allowed = card && Sanguosha->matchPattern(request.pattern, request.initiator, card);
        delete card;
        return allowed;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request) || !canActivate(request)) return nullptr;
        Card *card = Sanguosha->cloneCard(request.initiator->isChained() ? "slash" : "jink");
        if (card) card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    {
        return request.userString == "jink" ? "Jink" : "Slash";
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &) const override
    {
        // Chaining is paid once after interception; the generated ordinary card has no material.
        if (!ctx.initiator || !ctx.use_card || !ctx.initiator->isAlive()) return false;
        if (ctx.initiator->isChained() != ctx.use_card->isKindOf("Slash")) return false;
        room->setPlayerChained(ctx.initiator);
        return ctx.initiator->isAlive();
    }
};
class JinWanyi : public TriggerSkillV2
{
public:
    JinWanyi() : TriggerSkillV2("jinwanyi") { events << TargetSpecified << EventPhaseStart << Damaged; }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.owner;
        ctx.initiator = ctx.owner;
        return TriggerSkillV2::prepareSource(room, ctx);
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || !player->hasSkill(this)) return result;
        if (event == TargetSpecified) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from == player && use.card && (use.card->isKindOf("Slash") || use.card->isNDTrick())
                && use.to.length() == 1 && use.to.first() != player && use.to.first()->isAlive() && !use.to.first()->isNude())
                result[player] << objectName() + "->" + use.to.first()->objectName();
        } else if (!player->getPile(objectName()).isEmpty() && (event == Damaged || player->getPhase() == Player::Finish))
            result[player] << objectName();
        return result;
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = nullptr;
        const bool take = event == TargetSpecified;
        if (take) {
            target = ctx.preferredTarget;
            if (!target || !target->isAlive() || target->isNude() || !ctx.invoker->askForSkillInvoke(this, target)) return false;
        } else {
            if (ctx.owner->getPile(objectName()).isEmpty()) return false;
            target = room->askForPlayerChosen(ctx.invoker, room->getAlivePlayers(), objectName(), "@jinwanyi-target", false, true);
        }
        if (!target) return false;
        ctx.extra_data = take;
        ctx.targets = QList<ServerPlayer *>() << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ctx.invoker->peiyin(this);
        if (ctx.extra_data.toBool()) {
            for (int i = 0; i < getEffectiveAmount(ctx) && ctx.owner->isAlive() && ctx.invoker->isAlive() && target->isAlive() && !target->isNude(); ++i) {
                const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName());
                if (id < 0 || room->getCardOwner(id) != target || Sanguosha->getCard(id)->hasFlag("using")
                    || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) break;
                // This is the skill's public physical pile, shared by its removal/limitation rules.
                ctx.owner->addToPile(objectName(), id);
            }
        } else {
            for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive(); ++i) {
                const QList<int> pile = ctx.owner->getPile(objectName());
                if (pile.isEmpty()) break;
                room->fillAG(pile, target);
                int id = -1;
                try { id = room->askForAG(target, pile, false, objectName(), "@jinwanyi-get"); }
                catch (...) { room->clearAG(target); throw; }
                room->clearAG(target);
                if (!pile.contains(id) || !ctx.owner->getPile(objectName()).contains(id)) break;
                if (target == ctx.owner) {
                    LogMessage log;
                    log.type = "$KuangbiGet";
                    log.from = ctx.owner;
                    log.arg = objectName();
                    log.card_str = QString::number(id);
                    room->sendLog(log);
                }
                room->obtainCard(target, id);
            }
        }
        return false;
    }
};

class JinWanyiLimit : public CardLimitSkill
{
public:
	JinWanyiLimit() : CardLimitSkill("#jinwanyi-limit")
	{
	}

	QString limitList(const Player *) const
	{
		return "use,response,discard";
	}

	QString limitPattern(const Player *target) const
	{
		if (target->getPile("jinwanyi").length()>0 && target->hasSkill("jinwanyi")) {
			QStringList suits;
			foreach (int id, target->getPile("jinwanyi")) {
				QString str = Sanguosha->getCard(id)->getSuitString();
				if (suits.contains(str)) continue;
				suits << str;
			}
			return ".|" + suits.join(",");
		}
		return "";
	}
};

class JinMaihuo : public TriggerSkillV2
{
public:
    JinMaihuo() : TriggerSkillV2("jinmaihuo")
    {
        events << TargetSpecified << EventPhaseStart << Damage;
        global = true;
    }
    static SkillInstanceRef ref(const QVariantMap &receipt, const QString &prefix)
    {
        return SkillInstanceRef(receipt.value(prefix + "_owner").toString(),
            SkillInstanceKey(receipt.value(prefix + "_skill").toString(), receipt.value(prefix + "_id").toInt()));
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.activationRef.isValid()) return TriggerSkillV2::isSourceAvailable(room, ctx);
        if (!ctx.owner || !ctx.owner->isAlive()) return false;
        const QVariantMap receipt = ctx.owner->getTag("jinmaihuo_effect").toMap();
        return receipt.value("receipt").toInt() == ctx.instanceID && ref(receipt, "source") == ctx.sourceRef
            && ref(receipt, "activation") == ref(ctx.extra_data.toMap(), "activation");
    }
    bool prepareSource(Room *room, SkillContext &ctx) const override
    {
        ctx.invoker = ctx.initiator = ctx.owner;
        return isSourceAvailable(room, ctx);
    }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart) return false;
        if (!player || !player->isAlive() || player->getPhase() != Player::Play) return true;
        const QVariantMap receipt = player->getTag("jinmaihuo_effect").toMap();
        if (!ref(receipt, "source").isValid() || !ref(receipt, "activation").isValid()) return true;
        SkillContext ctx;
        ctx.skill_name = objectName();
        ctx.owner = ctx.invoker = ctx.initiator = player;
        ctx.instanceID = receipt.value("receipt").toInt();
        ctx.sourceRef = ref(receipt, "source");
        ctx.extra_data = receipt;
        ctx.original_data = &data;
        ctx.current_event = event;
        ctx.is_forced = true;
        contexts << ctx;
        return true;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == TargetSpecified) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!use.from || !use.from->isAlive() || !use.from->getPile("jinmhhuo").isEmpty() || !use.card
                || !use.card->isKindOf("Slash") || use.card->isVirtualCard() || !use.card->getSkillName().isEmpty()
                || use.to.length() != 1 || use.activationRef.key.skillName == objectName()) return result;
            ServerPlayer *defender = use.to.first();
            if (defender && defender != use.from && defender->isAlive() && defender->hasSkill(this)) result[defender] << objectName();
        } else if (event == Damage && player && player->isAlive() && player->hasSkill(this)) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.from == player && damage.to && damage.to != player && !damage.to->getPile("jinmhhuo").isEmpty())
                result[player] << objectName() + "->" + damage.to->objectName();
        }
        return result;
    }
    bool cost(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == TargetSpecified) {
            const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.from || !use.from->getPile("jinmhhuo").isEmpty() || !ctx.invoker->askForSkillInvoke(this, *ctx.original_data)) return false;
            ctx.targets << ctx.invoker;
        } else if (event == Damage) {
            ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
            if (!target || target->getPile("jinmhhuo").isEmpty()) return false;
            ctx.targets << target;
        } else ctx.targets << ctx.invoker;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0) return false;
        if (event == TargetSpecified) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (!use.card || !use.from || !use.from->isAlive() || !use.from->getPile("jinmhhuo").isEmpty()
                || !use.to.contains(target) || target == use.from || use.card->isVirtualCard()) return false;
            const int id = use.card->getEffectiveId();
            if (id < 0 || room->getCardPlace(id) != Player::PlaceTable) return false;
            // Freeze the protected recipient and exact material before moving the active Slash.
            const int sequence = room->getTag("jinmaihuo_sequence").toInt() + 1;
            room->setTag("jinmaihuo_sequence", sequence);
            const QVariantMap receipt{{"receipt", sequence}, {"card", id}, {"defender", target->objectName()},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
                {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
            use.nullified_list << target->objectName();
            *ctx.original_data = QVariant::fromValue(use);
            use.from->setTag("jinmaihuo_effect", receipt);
            ctx.invoker->peiyin(this);
            use.from->addToPile("jinmhhuo", id);
        } else if (event == Damage) {
            const QList<int> pile = target->getPile("jinmhhuo");
            target->removeTag("jinmaihuo_effect");
            if (pile.isEmpty()) return false;
            room->sendCompulsoryTriggerLog(ctx.invoker, this);
            DummyCard cards;
            cards.addSubcards(pile);
            room->throwCard(&cards, CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, target->objectName(), objectName(), ""), nullptr);
        } else {
            const QVariantMap receipt = ctx.extra_data.toMap();
            // Retire before ordinary use: nested phases cannot spend the same retained card twice.
            ctx.owner->removeTag("jinmaihuo_effect");
            const int id = receipt.value("card", -1).toInt();
            if (id < 0 || !ctx.owner->getPile("jinmhhuo").contains(id)) return false;
            ServerPlayer *defender = room->findPlayerByObjectName(receipt.value("defender").toString());
            const Card *slash = Sanguosha->getCard(id);
            if (target == ctx.owner && defender && defender->isAlive() && slash->isKindOf("Slash")
                && !slash->hasFlag("using") && Slash::IsAvailable(target) && target->canSlash(defender, slash)) {
                SkillContext accepted = ctx;
                accepted.activationRef = ref(receipt, "activation");
                room->useCardFromSkillEffect(CardUseStruct(slash, target, defender), accepted, true);
            } else {
                room->throwCard(slash, CardMoveReason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, ctx.owner->objectName(), objectName(), ""), nullptr);
            }
        }
        return false;
    }
};

YuePackage::YuePackage()
	: Package("yue")
{
	General *jin_yangyan = new General(this, "jin_yangyan", "jin", 3, false);
	jin_yangyan->addSkill(new JinXuanbei);
	jin_yangyan->addSkill(new JinXianwan);

	General *jin_yangzhi = new General(this, "jin_yangzhi", "jin", 3, false);
	jin_yangzhi->addSkill(new JinWanyi);
	jin_yangzhi->addSkill(new JinWanyiLimit);
	jin_yangzhi->addSkill(new JinMaihuo);
	related_skills.insert("jinwanyi", "#jinwanyi-limit");

	addMetaObject<JinXuanbeiCard>();
	addMetaObject<JinXianwanCard>();
}
ADD_PACKAGE(Yue)
