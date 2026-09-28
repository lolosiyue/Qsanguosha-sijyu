#include "yczh2016.h"
#include "skill-declaration.h"
#include "settings.h"
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
#include <QScopeGuard>

JiaozhaoCard::JiaozhaoCard()
{
    setSkillName("jiaozhao");
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void JiaozhaoCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->setPlayerMark(source, "ViewAsSkill_jiaozhaoEffect", 1);

    int selfcardid = this->getSubcards().first();
    room->showCard(source, selfcardid);
    room->setPlayerMark(source, "jiaozhao_showid-Clear", selfcardid + 1);

    int level = source->property("jiaozhao_level").toInt();
    if (level < 0)
        level = 0;
    if (level > 2)
        level = 2;

    ServerPlayer *target;
    if (level > 1)
        target = source;
    else {
        int distance = source->distanceTo(source->getNextAlive());
        foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
            if (source->distanceTo(p) < distance)
                distance = source->distanceTo(p);
        }
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
            if (source->distanceTo(p) == distance)
                targets << p;
        }
        if (targets.isEmpty()) return;
        target = room->askForPlayerChosen(source, targets, "jiaozhao", "@jiaozhao-target");
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, source->objectName(), target->objectName());
    }

    QStringList alllist;
    QList<int> ids;
    foreach(int id, Sanguosha->getRandomCards()) {
        const Card *c = Sanguosha->getEngineCard(id);
        if (c->isKindOf("EquipCard") || c->isKindOf("DelayedTrick")) continue;
        if (c->isNDTrick() && level < 1) continue;
        if (alllist.contains(c->objectName())) continue;
        alllist << c->objectName();
        ids << id;
    }
    if (ids.isEmpty()) return;

    room->fillAG(ids, target);
    int id = room->askForAG(target, ids, false, "jiaozhao");
    room->clearAG(target);

    const Card *c = Sanguosha->getEngineCard(id);
    QString name = c->objectName();
    LogMessage log;
    log.type = "#ShouxiChoice";
    log.from = target;
    log.arg = name;
    room->sendLog(log);
    room->setPlayerMark(source, "jiaozhao_id-Clear", id + 1);
}

// Accepted upgrades and conversion grants retain primitive provenance after their source retires.
static QVariantMap jiaozhaoRef(const SkillInstanceRef &ref)
{
    return ref.isValid() ? QVariantMap{{"owner", ref.ownerObjectName}, {"skill", ref.key.skillName}, {"id", ref.key.instanceID}} : QVariantMap();
}
static SkillInstanceRef jiaozhaoReadRef(const QVariant &value)
{
    const QVariantMap ref = value.toMap();
    return SkillInstanceRef(ref.value("owner").toString(), SkillInstanceKey(ref.value("skill").toString(), ref.value("id").toInt()));
}
static QVariantMap jiaozhaoOrigin(const Player *owner, const SkillInstance &instance)
{
    return {{"owner", owner->objectName()}, {"innate", instance.source == SourceInnate}, {"slot", instance.bindHead},
        {"parent", jiaozhaoRef(instance.parentRef)}, {"grant", jiaozhaoRef(instance.grantActivationRef)}, {"root", jiaozhaoRef(instance.frozenSourceRef)}};
}
static bool jiaozhaoConversion(const SkillInstance &instance)
{
    return instance.grantActivationRef.isValid() && instance.grantActivationRef.key.skillName == "jiaozhao";
}
static int jiaozhaoPairRank(const QVariantMap &origin, const Player *owner, const SkillInstance &candidate)
{
    if (jiaozhaoConversion(candidate)) return 0;
    if (origin.value("innate").toBool() && candidate.source == SourceInnate && candidate.bindHead > 0
        && origin.value("slot").toInt() == candidate.bindHead && origin.value("owner").toString() == owner->objectName()) return 4;
    const QVariantMap other = jiaozhaoOrigin(owner, candidate);
    for (const QString &key : {QString("parent"), QString("grant"), QString("root")})
        if (!origin.value(key).toMap().isEmpty() && origin.value(key) == other.value(key)) return key == "parent" ? 3 : key == "grant" ? 2 : 1;
    return 0;
}
static QList<int> jiaozhaoUpgradeCandidates(const Player *owner, const QVariantMap &origin)
{
    QList<int> ids; int rank = 0;
    for (const SkillInstance &instance : owner->getSkillInstances()) {
        if (instance.skillName != "jiaozhao") continue;
        const int candidateRank = jiaozhaoPairRank(origin, owner, instance);
        if (candidateRank <= 0 || candidateRank < rank) continue;
        if (candidateRank > rank) { ids.clear(); rank = candidateRank; }
        ids << instance.instanceID;
    }
    return ids;
}
static int jiaozhaoChooseInstance(Room *room, ServerPlayer *owner, const QList<int> &ids)
{
    if (ids.isEmpty()) return 0;
    if (ids.size() == 1) return ids.first();
    QStringList choices; for (int id : ids) choices << SkillInstanceUtils::formatName("jiaozhao", id);
    const QString choice = room->askForChoice(owner, "danxin", choices.join("+"));
    const int index = choices.indexOf(choice); return index < 0 ? 0 : ids.at(index);
}
static void jiaozhaoSetLevel(Room *room, ServerPlayer *owner, int id, int level)
{
    if (!owner->findSkillInstance("jiaozhao", id)) return;
    level = qBound(0, level, 2);
    owner->setSkillInstanceStateValue("jiaozhao", id, "level", level);
    // These remain public presentation only; all decisions use the selected instance state.
    room->setPlayerProperty(owner, "jiaozhao_level", level);
    room->setPlayerMark(owner, "&jiaozhao_level", level);
    room->changeTranslation(owner, "jiaozhao", level);
}

class JiaozhaoVS : public ViewAsSkillV2
{
public:
    JiaozhaoVS() : ViewAsSkillV2("jiaozhao", 1) { response_or_use = true; }
    TargetMode targetMode() const override { return NoTarget; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    { return ctx.owner && (!qobject_cast<const ActiveSkillCard *>(ctx.use_card) || !ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "phase_spent").toBool()); }
    bool willThrowSelectedCards() const override { return false; }
    static QVariantMap state(const ActiveSkillRequest &request)
    { return request.initiator ? request.initiator->getSkillInstanceState("jiaozhao", request.activationRef.key.instanceID) : QVariantMap(); }
    static bool conversion(const ActiveSkillRequest &request)
    {
        const SkillInstance *instance = request.initiator ? request.initiator->findSkillInstance("jiaozhao", request.activationRef.key.instanceID) : nullptr;
        return instance && jiaozhaoConversion(*instance);
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !request.initiator->isAlive()) return false;
        const SkillInstance *instance = request.initiator->findSkillInstance(objectName(), request.activationRef.key.instanceID);
        if (!instance) return false;
        const QVariantMap saved = state(request);
        if (!conversion(request)) return request.reason == CardUseStruct::CARD_USE_REASON_PLAY && !saved.value("phase_spent").toBool() && !request.initiator->isKongcheng();
        // A committed but not yet initialized grant must never reveal a second card.
        const int material = saved.value("material", -1).toInt();
        if (!saved.value("conversion_only").toBool() || saved.value("declaration").toString().isEmpty()
            || !request.initiator->handCards().contains(material) || Sanguosha->getCard(material)->hasFlag("using")) return false;
        Card *card = Sanguosha->cloneCard(saved.value("declaration").toString());
        if (!card) return false;
        card->addSubcard(material); card->setSkillName(objectName()); card->setCanRecast(false);
        const bool result = request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? card->isAvailable(request.initiator)
            : (request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE || request.pattern == "nullification") && !request.pattern.startsWith(".") && !request.pattern.startsWith("@")
                && request.pattern != "peach+analeptic" && Sanguosha->matchExpPattern(request.pattern, request.initiator, card);
        delete card; return result;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return canActivate(request) && request.selectedCardIds.isEmpty() && card && !card->hasFlag("using")
            && request.initiator->handCards().contains(card->getEffectiveId())
            && (!conversion(request) || card->getEffectiveId() == state(request).value("material", -1).toInt());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { ActiveSkillRequest empty = request; empty.selectedCardIds.clear(); return request.selectedCardIds.size() == 1 && canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first())); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        if (!conversion(request)) {
            ActiveSkillCard *card = new ActiveSkillCard; card->setActiveSkill(this); card->setSkillName(objectName()); card->addSubcards(request.selectedCardIds);
            card->tag["jiaozhao_level"] = qBound(0, state(request).value("level").toInt(), 2); return card;
        }
        Card *card = Sanguosha->cloneCard(state(request).value("declaration").toString());
        if (card) { card->setSkillName(objectName()); card->setCanRecast(false); card->addSubcards(request.selectedCardIds); }
        return card;
    }
    QString historyKey(const ActiveSkillRequest &request) const override
    {
        if (!conversion(request)) return "JiaozhaoCard";
        Card *card = Sanguosha->cloneCard(state(request).value("declaration").toString());
        if (!card) return objectName(); const QString key = card->getClassName(); delete card; return key;
    }
    static void commit(const SkillContext &ctx)
    { if (ctx.owner && qobject_cast<const ActiveSkillCard *>(ctx.use_card)) ctx.owner->setSkillInstanceStateValue("jiaozhao", ctx.activationRef.key.instanceID, "phase_spent", true); }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    { if (!cardSelectionFeasible(request) || !isUsable(ctx)) return false; commit(ctx); addUsage(ctx); return true; }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!qobject_cast<const ActiveSkillCard *>(ctx.use_card)) return ContinueEffects;
        ctx.choice = "reveal"; skillEffect(ctx, ctx.initiator); return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom(); const QVariantMap details = ctx.extra_data.toMap();
        if (ctx.choice == "grant") {
            const int material = details.value("material", -1).toInt();
            if (room->getCardOwner(material) != target || room->getCardPlace(material) != Player::PlaceHand || Sanguosha->getCard(material)->hasFlag("using")) return ContinueEffects;
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
            if (turn <= 0) return ContinueEffects;
            const int serial = room->getTag("JiaozhaoSerial").toInt() + 1; room->setTag("JiaozhaoSerial", serial);
            QVariantMap receipt{{"serial", serial}, {"turn", turn}, {"recipient", target->objectName()}, {"grant_id", 0}};
            QVariantList receipts = room->getTag("JiaozhaoGrants").toList(); receipts << receipt; room->setTag("JiaozhaoGrants", receipts);
            const int id = room->acquireSkillFromEffect(target, objectName(), ctx, [&](int committedID) {
                QVariantList pending = room->getTag("JiaozhaoGrants").toList(); const int index = pending.indexOf(receipt);
                if (index >= 0) { receipt.insert("grant_id", committedID); pending[index] = receipt; room->setTag("JiaozhaoGrants", pending); }
            });
            if (id <= 0) return ContinueEffects;
            if (!room->getTag("JiaozhaoGrants").toList().contains(receipt)) {
                room->detachSkillFromPlayer(target, SkillInstanceUtils::formatName(objectName(), id)); return ContinueEffects;
            }
            if (target->findSkillInstance(objectName(), id)) {
                QVariantMap initialized{{"conversion_only", true}, {"declaration", details.value("declaration")}, {"material", material}};
                target->setSkillInstanceState(objectName(), id, initialized);
            }
            return ContinueEffects;
        }
        if (ctx.choice == "declare") {
            const int level = details.value("level").toInt(); QStringList names; QList<int> ids;
            for (int id : Sanguosha->getRandomCards()) {
                const Card *card = Sanguosha->getEngineCard(id);
                if ((!card->isKindOf("BasicCard") && !(level > 0 && card->isNDTrick())) || names.contains(card->objectName())) continue;
                names << card->objectName(); ids << id;
            }
            if (ids.isEmpty()) return ContinueEffects;
            room->fillAG(ids, target); const auto clear = qScopeGuard([&] { room->clearAG(target); });
            const int id = room->askForAG(target, ids, false, objectName()); if (!ids.contains(id)) return ContinueEffects;
            LogMessage log; log.type = "#ShouxiChoice"; log.from = target; log.arg = Sanguosha->getEngineCard(id)->objectName(); room->sendLog(log);
            SkillContext grant = ctx; grant.choice = "grant"; QVariantMap declared = details; declared.insert("declaration", log.arg); grant.extra_data = declared;
            ServerPlayer *holder = room->findPlayerByObjectName(details.value("holder").toString());
            grant.targets = {holder}; skillEffect(grant, holder); return ContinueEffects;
        }
        if (!ctx.use_card || ctx.use_card->getSubcards().size() != 1 || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
        const int material = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(material) != target || room->getCardPlace(material) != Player::PlaceHand || Sanguosha->getCard(material)->hasFlag("using")) return ContinueEffects;
        const int level = qBound(0, ctx.use_card->tag.value("jiaozhao_level").toInt(), 2);
        room->showCard(target, material);
        ServerPlayer *declarer = target;
        if (level < 2) {
            QList<ServerPlayer *> nearest; int distance = 9999;
            for (ServerPlayer *other : room->getOtherPlayers(target)) {
                const int current = target->distanceTo(other); if (current < distance) { nearest.clear(); distance = current; }
                if (current == distance) nearest << other;
            }
            if (nearest.isEmpty() || target->isDead()) return ContinueEffects;
            declarer = room->askForPlayerChosen(target, nearest, objectName(), "@jiaozhao-target");
            if (!nearest.contains(declarer)) return ContinueEffects;
        }
        SkillContext declaration = ctx; declaration.choice = "declare"; declaration.targets = {declarer};
        declaration.extra_data = QVariantMap{{"holder", target->objectName()}, {"material", material}, {"level", level}}; skillEffect(declaration, declarer);
        return ContinueEffects;
    }
};

class Jiaozhao : public TriggerSkillV2
{
public:
    Jiaozhao() : TriggerSkillV2("jiaozhao") { events << EventPhaseChanging << EventSkillInvoking; global = true; view_as_skill = new JiaozhaoVS; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext ctx = data.value<SkillContext>();
            if (ctx.bypass_cost && ctx.activationRef.key.skillName == objectName() && ctx.activationRef.isValid()) JiaozhaoVS::commit(ctx);
            return true;
        }
        if (!player) return true;
        const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.from == Player::Play) for (int id : player->getSkillInstanceIds(objectName())) player->setSkillInstanceStateValue(objectName(), id, "phase_spent", false);
        if (change.to == Player::NotActive) {
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong();
            if (turn <= 0) return true;
            QVariantList receipts, kept;
            for (const QVariant &entry : room->getTag("JiaozhaoGrants").toList()) {
                if (entry.toMap().value("turn").toLongLong() == turn) receipts << entry; else kept << entry;
            }
            room->setTag("JiaozhaoGrants", kept);
            for (const QVariant &entry : receipts) {
                const QVariantMap receipt = entry.toMap(); ServerPlayer *recipient = room->findPlayerByObjectName(receipt.value("recipient").toString(), true);
                const int id = receipt.value("grant_id").toInt();
                if (recipient && id > 0) room->detachSkillFromPlayer(recipient, SkillInstanceUtils::formatName(objectName(), id));
            }
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};

class JiaozhaoPro : public ProhibitSkill
{
public:
    JiaozhaoPro() : ProhibitSkill("#jiaozhaopro") { frequency = NotFrequent; }
    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const override
    { return from == to && card && card->getSkillName() == "jiaozhao"; }
};

class Danxin : public TriggerSkillV2
{
public:
    Danxin() : TriggerSkillV2("danxin") { events << Damaged; frequency = Frequent; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const SkillInstance *instance = ctx.owner->findSkillInstance(objectName(), ctx.activationRef.key.instanceID);
        if (!instance) return false;
        // Freeze origin before any choice callbacks can retire the triggering copy.
        const QVariantMap origin = jiaozhaoOrigin(ctx.owner, *instance);
        if (!room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        const int id = jiaozhaoChooseInstance(room, ctx.owner, jiaozhaoUpgradeCandidates(ctx.owner, origin));
        int level = id > 0 ? ctx.owner->getSkillInstanceStateValue("jiaozhao", id, "level").toInt() : 0;
        if (id <= 0) for (const QVariant &entry : ctx.owner->getTag("DanxinFutureUpgrades").toList())
            if (entry.toMap().value("origin").toMap() == origin) level += entry.toMap().value("amount").toInt();
        ctx.choice = level >= 2 ? "draw" : room->askForChoice(ctx.owner, objectName(), "draw+up");
        ctx.extra_data = QVariantMap{{"origin", origin}, {"jiaozhao_id", id}}; ctx.targets = {ctx.owner}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        if (ctx.choice == "draw") { target->drawCards(amount, objectName()); return false; }
        const QVariantMap details = ctx.extra_data.toMap(); int id = details.value("jiaozhao_id").toInt();
        if (id <= 0) id = jiaozhaoChooseInstance(room, target, jiaozhaoUpgradeCandidates(target, details.value("origin").toMap()));
        if (jiaozhaoUpgradeCandidates(target, details.value("origin").toMap()).contains(id)) {
            jiaozhaoSetLevel(room, target, id, target->getSkillInstanceStateValue("jiaozhao", id, "level").toInt() + amount); return false;
        }
        const int serial = room->getTag("DanxinSerial").toInt() + 1; room->setTag("DanxinSerial", serial);
        QVariantList pending = target->getTag("DanxinFutureUpgrades").toList();
        pending << QVariantMap{{"serial", serial}, {"recipient", target->objectName()}, {"origin", details.value("origin")}, {"amount", qMin(2, amount)},
            {"source", jiaozhaoRef(ctx.sourceRef)}, {"activation", jiaozhaoRef(ctx.activationRef)}, {"issuer", ctx.owner->objectName()}};
        target->setTag("DanxinFutureUpgrades", pending); return false;
    }
};

class DanxinFuture : public TriggerSkillV2
{
public:
    DanxinFuture() : TriggerSkillV2("#danxin-future") { events << EventAcquireSkill << GameStart << EventSkillEffectFinished; global = true; frequency = Compulsory; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if ((event != EventAcquireSkill && event != GameStart) || !player || player->isDead() || (event == EventAcquireSkill && data.value<SkillChangeStruct>().skillName != "jiaozhao")) return true;
        for (const QVariant &entry : player->getTag("DanxinFutureUpgrades").toList()) {
            const QVariantMap receipt = entry.toMap(); if (jiaozhaoUpgradeCandidates(player, receipt.value("origin").toMap()).isEmpty()) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt();
            ctx.owner = room->findPlayerByObjectName(receipt.value("issuer").toString(), true); ctx.invoker = player; ctx.initiator = ctx.owner;
            ctx.sourceRef = jiaozhaoReadRef(receipt.value("source")); ctx.targets = {player}; ctx.extra_data = receipt;
            ctx.amount = receipt.value("amount").toInt(); contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    { ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString()); return !ctx.activationRef.isValid() && holder && holder->isAlive() && holder->getTag("DanxinFutureUpgrades").toList().contains(ctx.extra_data); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString());
        if (!holder) return false;
        const int id = jiaozhaoChooseInstance(room, holder, jiaozhaoUpgradeCandidates(holder, ctx.extra_data.toMap().value("origin").toMap()));
        ctx.choice = QString::number(id); return id > 0;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillEffectFinished) return true;
        const SkillContext ctx = data.value<SkillContext>();
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString(), true);
        if (ctx.skill_name == objectName() && !ctx.activationRef.isValid() && holder) {
            QVariantList pending = holder->getTag("DanxinFutureUpgrades").toList(); pending.removeOne(ctx.extra_data); holder->setTag("DanxinFutureUpgrades", pending);
        }
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString(), true);
        if (holder) { QVariantList pending = holder->getTag("DanxinFutureUpgrades").toList(); pending.removeOne(ctx.extra_data); holder->setTag("DanxinFutureUpgrades", pending); } return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int id = ctx.choice.toInt();
        if (!jiaozhaoUpgradeCandidates(target, ctx.extra_data.toMap().value("origin").toMap()).contains(id)) return false;
        jiaozhaoSetLevel(room, target, id, target->getSkillInstanceStateValue("jiaozhao", id, "level").toInt() + getEffectiveAmount(ctx)); return false;
    }
};
class Guizao : public TriggerSkillV2
{
public:
    Guizao() : TriggerSkillV2("guizao") { events << EventPhaseEnd; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
                && player->getPhase() == Player::Discard && hasDistinctDiscards(room, player)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        ctx.choice = room->askForChoice(ctx.owner, objectName(), ctx.owner->isWounded() ? "draw+recover" : "draw");
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        const int amount = getEffectiveAmount(ctx);
        if (ctx.choice == "draw") target->drawCards(amount, objectName());
        else room->recover(target, RecoverStruct(ctx.owner, nullptr, amount, objectName()));
        return false;
    }

private:
    static bool hasDistinctDiscards(Room *room, ServerPlayer *player)
    {
        const QVariant phase = room->historyScopes().value("phase_id");
        if (phase.toLongLong() <= 0) return false;
        QVariantMap query{{"phase_id", phase}, {"from", player->objectName()}, {"limit", 64}};
        QList<int> suits;
        while (true) {
            const QVariantMap page = room->queryHistoryMoves(query);
            // Incomplete history is unknown; it cannot prove that every suit differs.
            if (!page.value("complete").toBool() || !page.value("error").toString().isEmpty()) return false;
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap move = entry.toMap().value("data").toMap();
                if (!move.contains("reason") || !move.contains("from_place")) return false;
                const int place = move.value("from_place").toInt();
                if ((move.value("reason").toInt() & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DISCARD
                    || (place != Player::PlaceHand && place != Player::PlaceEquip)) continue;
                const QVariantMap card = move.value("card_before").toMap();
                if (!card.contains("suit")) return false;
                const int suit = card.value("suit").toInt();
                if (suits.contains(suit)) return false;
                suits << suit;
            }
            if (!page.value("has_more").toBool()) break;
            query.insert("after", page.value("next_after"));
        }
        return suits.size() >= 2;
    }
};

JiyuCard::JiyuCard()
{
    setSkillName("jiyu");
}

bool JiyuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.isEmpty() && to_select->getMark("jiyu-PlayClear") <= 0 && !to_select->isKongcheng();
}

void JiyuCard::onEffect(CardEffectStruct &effect) const
{
    ServerPlayer *source = effect.from;
    ServerPlayer *target = effect.to;
    Room *room = source->getRoom();
    room->addPlayerMark(target, "jiyu-PlayClear");
    if (target->canDiscard(target, "h")) {
        const Card *c = room->askForDiscard(target, "jiyu", 1, 1);
        if (!c) {
            c = target->getCards("h").at(qsanRandomBounded(target->getCards("h").length()));
            room->throwCard(c, target, nullptr);
        } else
            c = Sanguosha->getCard(c->getSubcards().first());

        room->setPlayerCardLimitation(source, "use", QString(".|%1|.|.").arg(c->getSuitString()), true);
        if (c->getSuit() == Card::Spade) {
            room->loseHp(HpLostStruct(target, 1, "jiyu", source));
            source->turnOver();
        }
    }
}

class Jiyu : public ViewAsSkillV2
{
public:
    Jiyu() : ViewAsSkillV2("jiyu") {}
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &) const override { return true; }
    TargetMode targetMode() const override { return SelectTargets; }
    QString historyKey(const ActiveSkillRequest &) const override { return "JiyuCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason != CardUseStruct::CARD_USE_REASON_PLAY) return false;
        for (const Card *card : request.initiator->getHandcards()) if (card->isAvailable(request.initiator)) return true;
        return false;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    {
        return request.initiator && selected.isEmpty() && candidate && candidate->isAlive() && !candidate->isKongcheng()
            && !request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "used_targets").toStringList().contains(candidate->objectName());
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    { return selected.size() == 1 && canSelectTarget(request, {}, selected.first()); }
    static void commit(const SkillContext &ctx)
    {
        QStringList used = ctx.owner->getSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "used_targets").toStringList();
        for (ServerPlayer *target : ctx.targets) if (!used.contains(target->objectName())) used << target->objectName();
        ctx.owner->setSkillInstanceStateValue(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID, "used_targets", used);
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        QList<const Player *> targets; for (ServerPlayer *target : ctx.targets) targets << target;
        if (!targetsFeasible(request, targets)) return false;
        commit(ctx); addUsage(ctx); return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        if (ctx.choice == "restriction") {
            const QString suit = ctx.extra_data.toString();
            room->setPlayerCardLimitation(target, "use", QString(".|%1|.|.").arg(suit), true, objectName());
            return ContinueEffects;
        }
        if (ctx.choice == "turnover") {
            target->turnOver();
            return ContinueEffects;
        }
        if (!target->canDiscard(target, "h")) return ContinueEffects;
        const Card *discard = room->askForDiscard(target, objectName(), 1, 1);
        if (!discard || discard->getSubcards().isEmpty()) return ContinueEffects;
        const Card *physical = Sanguosha->getCard(discard->getSubcards().first());
        const QString suit = physical->getSuitString();
        // The imposed use restriction and turnover affect the activating player separately.
        SkillContext restriction = ctx; restriction.choice = "restriction"; restriction.extra_data = suit; restriction.targets = {ctx.invoker};
        skillEffect(restriction, ctx.invoker);
        if (suit == "spade") {
            if (target->isAlive()) room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
            SkillContext turnover = ctx; turnover.choice = "turnover"; turnover.targets = {ctx.invoker};
            skillEffect(turnover, ctx.invoker);
        }
        return ContinueEffects;
    }
};

class JiyuRecord : public TriggerSkillV2
{
public:
    JiyuRecord() : TriggerSkillV2("#jiyu-record") { events << EventPhaseChanging << EventSkillInvoking; global = true; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext accepted = data.value<SkillContext>();
            if (accepted.owner && accepted.bypass_cost && accepted.activationRef.isValid()
                && accepted.activationRef.ownerObjectName == accepted.owner->objectName() && accepted.activationRef.key.skillName == "jiyu") Jiyu::commit(accepted);
        } else if (player && data.value<PhaseChangeStruct>().from == Player::Play) {
            // Reset each concrete copy, including temporarily invalid ones, at the phase boundary.
            for (int id : player->getSkillInstanceIds("jiyu")) player->setSkillInstanceStateValue("jiyu", id, "used_targets", QStringList());
        }
        return true;
    }
};
DuliangCard::DuliangCard()
{
    setSkillName("duliang");
}

bool DuliangCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && !to_select->isKongcheng() && to_select != Self;
}

void DuliangCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if (effect.to->isKongcheng()) return;
    int card_id = room->askForCardChosen(effect.from, effect.to, "h", "duliang");
    CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName());
    room->obtainCard(effect.from, Sanguosha->getCard(card_id), reason, room->getCardPlace(card_id) != Player::PlaceHand);
    if (room->askForChoice(effect.from, "duliang", "watch+draw") == "draw")
        room->addPlayerMark(effect.to, "&duliang");
    else {
        QList<int> ids = room->getNCards(2);
        LogMessage log;
        log.type = "$ViewDrawPile";
        log.from = effect.to;
        log.card_str = ListI2S(ids).join("+");
        room->sendLog(log, effect.to);
        LogMessage newlog;
        newlog.type = "#ViewDrawPile";
        newlog.from = effect.to;
        newlog.arg = "2";
		room->sendLog(newlog, room->getOtherPlayers(effect.to,true));
        room->fillAG(ids, effect.to);
        room->askForAG(effect.to, ids, true, "duliang");
        room->clearAG(effect.to);
        QList<int> to_obtain;
        foreach (int card_id, ids) {
            if (Sanguosha->getCard(card_id)->getTypeId() == Card::TypeBasic)
                to_obtain << card_id;
        }
        room->returnToTopDrawPile(ids);
        if (!to_obtain.isEmpty()) {
            DummyCard get(to_obtain);
            effect.to->obtainCard(&get, false);
        }
    }
}

class DuliangViewAsSkill : public ViewAsSkillV2
{
public:
    DuliangViewAsSkill() : ViewAsSkillV2("duliang") { setPhaseName("Play");}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "DuliangCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return selected.isEmpty() && candidate && candidate->isAlive() && candidate != request.initiator && !candidate->isKongcheng(); }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    { return selected.size() == 1 && canSelectTarget(request, {}, selected.first()); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        const int amount = getEffectiveAmount(ctx);
        if (amount <= 0 || target->isKongcheng()) return ContinueEffects;
        for (int i = 0; i < amount && ctx.invoker->isAlive() && target->isAlive() && !target->isKongcheng(); ++i) {
            const int id = room->askForCardChosen(ctx.invoker, target, "h", objectName());
            if (id < 0 || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand
                || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
            room->obtainCard(ctx.invoker, Sanguosha->getCard(id),
                CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, ctx.invoker->objectName()), false);
        }
        if (!target->isAlive() || !ctx.invoker->isAlive()) return ContinueEffects;
        if (room->askForChoice(ctx.invoker, objectName(), "watch+draw") == "draw") {
            const int serial = room->getTag("DuliangNextReceipt").toInt() + 1;
            room->setTag("DuliangNextReceipt", serial);
            QVariantList receipts = target->getTag("DuliangReceipts").toList();
            receipts << QVariantMap{{"serial", serial}, {"recipient", target->objectName()}, {"issuer", ctx.invoker->objectName()}, {"amount", amount},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
                {"source_id", ctx.sourceRef.key.instanceID}, {"activation_owner", ctx.activationRef.ownerObjectName},
                {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
            target->setTag("DuliangReceipts", receipts);
            int total = 0; for (const QVariant &receipt : receipts) total += receipt.toMap().value("amount").toInt();
            room->setPlayerMark(target, "&duliang", total);
            return ContinueEffects;
        }
        const QList<int> cards = room->getNCards(2 * amount);
        if (cards.isEmpty()) return ContinueEffects;
        bool returned = false;
        auto restore = qScopeGuard([&] {
            room->clearAG(target);
            if (!returned) {
                QList<int> pending;
                for (int id : cards)
                    if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) pending << id;
                if (!pending.isEmpty()) room->returnToTopDrawPile(pending);
            }
        });
        LogMessage log; log.type = "$ViewDrawPile"; log.from = target; log.card_str = ListI2S(cards).join("+"); room->sendLog(log, target);
        LogMessage publicLog; publicLog.type = "#ViewDrawPile"; publicLog.from = target; publicLog.arg = QString::number(cards.size());
        room->sendLog(publicLog, room->getOtherPlayers(target, true));
        room->fillAG(cards, target); room->askForAG(target, cards, true, objectName()); room->clearAG(target);
        QList<int> pending;
        for (int id : cards)
            if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) pending << id;
        room->returnToTopDrawPile(pending); returned = true; restore.dismiss();
        QList<int> obtain;
        for (int id : pending)
            if (room->getCardPlace(id) == Player::DrawPile && Sanguosha->getCard(id)->isKindOf("BasicCard")) obtain << id;
        if (!obtain.isEmpty() && target->isAlive()) { DummyCard basic(obtain); room->obtainCard(target, &basic, false); }
        return ContinueEffects;
    }
};

class Duliang : public TriggerSkillV2
{
public:
    Duliang() : TriggerSkillV2("duliang") { events << DrawNCards << EventSkillEffectFinished; global = true; frequency = Compulsory; view_as_skill = new DuliangViewAsSkill; }
    static bool consume(Room *room, const SkillContext &ctx)
    {
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString(), true);
        if (!holder) return false;
        QVariantList receipts = holder->getTag("DuliangReceipts").toList();
        if (!receipts.removeOne(ctx.extra_data)) return false;
        holder->setTag("DuliangReceipts", receipts);
        int total = 0; for (const QVariant &entry : receipts) total += entry.toMap().value("amount").toInt();
        room->setPlayerMark(holder, "&duliang", total); return true;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext ctx = data.value<SkillContext>();
            if (ctx.skill_name == objectName() && !ctx.activationRef.isValid()) consume(room, ctx);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != DrawNCards) return true;
        const DrawStruct draw = data.value<DrawStruct>();
        if (!player || !player->isAlive() || draw.who != player || draw.reason != "draw_phase") return true;
        for (const QVariant &entry : player->getTag("DuliangReceipts").toList()) {
            const QVariantMap receipt = entry.toMap();
            SkillContext ctx; ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt();
            ctx.owner = room->findPlayerByObjectName(receipt.value("issuer").toString(), true);
            if (!ctx.owner || ctx.instanceID <= 0) continue;
            ctx.initiator = ctx.owner; ctx.invoker = player; ctx.targets = {player};
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(),
                SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            ctx.amount = receipt.value("amount").toInt(); ctx.extra_data = receipt;
            ctx.original_data = &data; ctx.current_event = event; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString(), true);
        return !ctx.activationRef.isValid() && holder && holder->isAlive()
            && holder->getTag("DuliangReceipts").toList().contains(ctx.extra_data);
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Consume this next-draw opportunity before recipient interception; source retirement does not revoke it.
        if (!consume(room, ctx)) ctx.targets.clear();
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        DrawStruct draw = ctx.original_data->value<DrawStruct>();
        if (draw.who != target) return false;
        LogMessage log; log.type = "#DuliangEffect"; log.from = target; log.arg = objectName();
        log.arg2 = QString::number(getEffectiveAmount(ctx)); room->sendLog(log);
        draw.num += getEffectiveAmount(ctx); *ctx.original_data = QVariant::fromValue(draw);
        return false;
    }
};
class Fulin : public TriggerSkillV2
{
public:
    Fulin() : TriggerSkillV2("fulin") { events << EventPhaseChanging << EventAcquireSkill; frequency = Compulsory; }

    static bool receivedHandCards(Room *room, ServerPlayer *player, QList<int> &cards)
    {
        const QVariant turn = room->historyScopes().value("turn_id");
        if (turn.toLongLong() <= 0) return false;
        QVariantMap query{{"turn_id", turn}, {"to", player->objectName()}, {"limit", 64}};
        const QList<int> hands = player->handCards();
        while (true) {
            const QVariantMap page = room->queryHistoryMoves(query);
            if (!page.value("complete").toBool() || !page.value("error").toString().isEmpty()) return false;
            if (!query.contains("watermark")) query.insert("watermark", page.value("watermark"));
            for (const QVariant &entry : page.value("items").toList()) {
                const QVariantMap move = entry.toMap().value("data").toMap();
                if (!move.contains("to_place") || !move.contains("card_id")) return false;
                const int id = move.value("card_id").toInt();
                if (move.value("to_place").toInt() == Player::PlaceHand && hands.contains(id) && !cards.contains(id)) cards << id;
            }
            if (!page.value("has_more").toBool()) return true;
            query.insert("after", page.value("next_after"));
        }
    }

    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventAcquireSkill) return false;
        if (player && player->hasFlag("CurrentPlayer") && data.value<SkillChangeStruct>().skillName == objectName()) {
            QList<int> cards;
            if (receivedHandCards(room, player, cards))
                for (int id : cards) room->setCardTip(id, "fulin-Clear");
        }
        return true;
    }

    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return event == EventPhaseChanging && player && player->isAlive() && player->hasSkill(objectName())
            && player->hasFlag("CurrentPlayer") && data.value<PhaseChangeStruct>().to == Player::Discard
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        QList<int> cards;
        if (!receivedHandCards(room, ctx.owner, cards) || cards.isEmpty()) return false;
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QList<int> cards;
        // Historical hand gains are queried on demand; no owner-wide list grants discard immunity.
        if (!receivedHandCards(room, target, cards) || cards.isEmpty()) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        room->ignoreCards(target, cards);
        return false;
    }
};

class FulinBF : public TriggerSkillV2
{
public:
    FulinBF() : TriggerSkillV2("#fulinbf") { events << CardsMoveOneTime << EventPhaseChanging; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        if (event == EventPhaseChanging) {
            if (data.value<PhaseChangeStruct>().to == Player::NotActive) room->setPlayerProperty(player, "fulin_list", "");
            return true;
        }
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!player->hasFlag("CurrentPlayer") || (move.from != player && move.to != player)) return true;
        // Compatibility consumers receive a projection rebuilt from complete facts, never an incremented history cache.
        QList<int> received;
        if (Fulin::receivedHandCards(room, player, received))
            room->setPlayerProperty(player, "fulin_list", ListI2S(received).join("+"));
        else room->setPlayerProperty(player, "fulin_list", "");
        if (move.to == player && move.to_place == Player::PlaceHand && player->hasSkill("fulin", true))
            for (int id : move.card_ids)
                if (room->getCardOwner(id) == player && room->getCardPlace(id) == Player::PlaceHand)
                    room->setCardTip(id, "fulin-Clear");
        return true;
    }
};QinqingCard::QinqingCard()
{
    setSkillName("qinqing");
}

bool QinqingCard::targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const
{
    const Player *lord = nullptr;
    QList<const Player *> as = Self->getAliveSiblings();
    as << Self;
    foreach (const Player *p, as) {
        if (p->getRole() == "lord") {
            lord = p;
            break;
        }
    }
    if (lord == nullptr) return false;
    return to_select->inMyAttackRange(lord);
}

void QinqingCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    foreach (ServerPlayer *p, targets) {
        if (source->isDead()) return;
        if (p->isDead()) continue;
        room->cardEffect(this, source, p);
    }
    ServerPlayer *lord = room->getLord();
    if (!lord) return;
    int num = 0;
    foreach (ServerPlayer *p, targets) {
        if (p->isAlive() && p->getHandcardNum() > lord->getHandcardNum())
            num++;
    }
    if (num > 0)
        source->drawCards(num, "qinqing");
}

void QinqingCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if (effect.from->canDiscard(effect.to, "he")) {
        int card_id = room->askForCardChosen(effect.from, effect.to, "he", "qinqing", false, Card::MethodDiscard);
        room->throwCard(card_id, effect.to, effect.from);
    }
    effect.to->drawCards(1, "qinqing");
}

class QinqingVS : public ViewAsSkillV2
{
public:
    QinqingVS() : ViewAsSkillV2("qinqing") { response_pattern = "@@qinqing"; }
    QString historyKey(const ActiveSkillRequest &) const override { return "QinqingCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY && request.pattern == "@@qinqing"; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        const Player *lord = request.initiator ? request.initiator->getLord() : nullptr;
        return lord && candidate && candidate->isAlive() && !selected.contains(candidate) && candidate->inMyAttackRange(lord);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        if (selected.isEmpty()) return false;
        QList<const Player *> checked;
        for (const Player *target : selected) {
            if (!canSelectTarget(request, checked, target)) return false;
            checked << target;
        }
        return true;
    }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        for (ServerPlayer *target : targets) {
            if (!ctx.invoker->isAlive()) return FinishSkill;
            if (skillEffect(ctx, target) == FinishSkill) return FinishSkill;
        }
        const Player *lord = ctx.invoker->getLord();
        if (!lord || !ctx.invoker->isAlive()) return ContinueEffects;
        int count = 0;
        for (ServerPlayer *target : targets)
            if (target->isAlive() && target->getHandcardNum() > lord->getHandcardNum()) ++count;
        if (count > 0) {
            SkillContext reward = ctx;
            reward.choice = "reward"; reward.extra_data = count; reward.targets = {ctx.invoker};
            skillEffect(reward, ctx.invoker);
        }
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "reward") {
            target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName());
            return ContinueEffects;
        }
        Room *room = ctx.invoker->getRoom();
        if (ctx.invoker->canDiscard(target, "he")) {
            const int id = room->askForCardChosen(ctx.invoker, target, "he", objectName(), false, Card::MethodDiscard);
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (card && !card->hasFlag("using") && room->getCardOwner(id) == target
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)
                && ctx.invoker->canDiscard(target, id)) room->throwCard(id, target, ctx.invoker);
        }
        if (target->isAlive()) target->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
};

class Qinqing : public TriggerSkillV2
{
public:
    Qinqing() : TriggerSkillV2("qinqing") { events << EventPhaseStart; view_as_skill = new QinqingVS; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getPhase() != Player::Finish) return {};
        const ServerPlayer *lord = room->getLord();
        if (!lord) return {};
        for (ServerPlayer *target : room->getAlivePlayers())
            if (target->inMyAttackRange(lord)) return {{player, {objectName()}}};
        return {};
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Pin this accepted trigger's root while the response uses the active V2 pipeline.
        Room::AcceptedViewAsEffectScope accepted(room, ctx.owner, objectName(), ctx);
        if (accepted.isValid()) room->askForUseCard(ctx.owner, "@@qinqing", "@qinqing");
        return false;
    }
};
HuishengCard::HuishengCard()
{
    setSkillName("huisheng");
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

class HuishengVS : public ViewAsSkillV2
{
public:
    HuishengVS() : ViewAsSkillV2("huisheng", 999) { response_pattern = "@@huisheng"; }
    TargetMode targetMode() const override { return NoTarget; }
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override { return request.initiator && request.pattern == "@@huisheng" && !request.initiator->isNude(); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.initiator && card && !request.selectedCardIds.contains(card->getEffectiveId()) && !card->hasFlag("using") && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId())); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { if (request.selectedCardIds.isEmpty()) return false; ActiveSkillRequest checked = request; checked.selectedCardIds.clear(); for (int id : request.selectedCardIds) { if (!canSelectCard(checked, Sanguosha->getCard(id))) return false; checked.selectedCardIds << id; } return true; }
    QString historyKey(const ActiveSkillRequest &) const override { return "HuishengCard"; }
    EffectFlow effect(SkillContext &ctx) const override { skillEffect(ctx, ctx.initiator); return FinishSkill; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    { QVariantList ids; if (ctx.use_card) for (int id : ctx.use_card->getSubcards()) ids << id; target->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "selection", ids); return ContinueEffects; }
};
class Huisheng : public TriggerSkillV2
{
public:
    Huisheng() : TriggerSkillV2("huisheng") { events << DamageInflicted; view_as_skill = new HuishengVS; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>(); TriggerList result;
        if (!player || player->isDead() || !player->hasSkill(objectName()) || player->isNude() || damage.to != player || !damage.from || damage.from == player || damage.from->isDead()) return result;
        for (int id : player->getValidSkillInstanceIds(objectName())) if (!player->getSkillInstanceStateValue(objectName(), id, "settled_attackers").toStringList().contains(damage.from->objectName())) result[player] << SkillInstanceUtils::formatName(objectName(), id);
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.manual_effect = true; return true; }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        const DamageStruct damage = ctx.original_data->value<DamageStruct>(); if (!damage.from || damage.from->isDead()) return false;
        const QVariant previous = room->getTag("HuishengDamage"); room->setTag("HuishengDamage", *ctx.original_data);
        const auto restore = qScopeGuard([&] { room->setTag("HuishengDamage", previous); });
        Room::AcceptedViewAsEffectScope response(room, ctx.owner, objectName(), ctx); if (!response.isValid()) return false;
        room->askForUseCard(ctx.owner, "@@huisheng", "@huisheng:" + damage.from->objectName());
        if (ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "settled_attackers").toStringList().contains(damage.from->objectName())) return false;
        const QVariantList selected = ctx.owner->getSkillInstanceStateValue(objectName(), response.activationRef().key.instanceID, "selection").toList();
        QList<int> ids;
        for (const QVariant &value : selected) { const int id = value.toInt(); if (room->getCardOwner(id) != ctx.owner || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip) || Sanguosha->getCard(id)->hasFlag("using")) return false; ids << id; }
        if (ids.isEmpty()) return false;
        ctx.targets = {damage.from}; ctx.extra_data = selected; return skillEffect(event, room, player, ctx, damage.from);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QList<int> ids; for (const QVariant &value : ctx.extra_data.toList()) { const int id = value.toInt(); if (room->getCardOwner(id) == ctx.owner && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip) && !Sanguosha->getCard(id)->hasFlag("using")) ids << id; }
        if (ids.isEmpty() || getEffectiveAmount(ctx) <= 0) return false;
        room->fillAG(ids, target); const QVariant previous = target->getTag("huisheng_ag_ids"); target->setTag("huisheng_ag_ids", ctx.extra_data);
        const auto cleanup = qScopeGuard([&] { room->clearAG(target); target->setTag("huisheng_ag_ids", previous); });
        int discardable = 0; for (const Card *card : target->getCards("he")) if (!card->hasFlag("using") && target->canDiscard(target, card->getEffectiveId())) ++discardable;
        if (discardable >= ids.size() && room->askForDiscard(target, objectName(), ids.size(), ids.size(), true, true, "huisheng-discard:" + QString::number(ids.size()))) return false;
        QList<int> obtained;
        for (int i = 0; i < getEffectiveAmount(ctx) && !ids.isEmpty(); ++i) {
            const int id = room->askForAG(target, ids, false, objectName());
            if (!ids.contains(id) || room->getCardOwner(id) != ctx.owner || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip) || Sanguosha->getCard(id)->hasFlag("using")) return false;
            obtained << id; ids.removeOne(id);
        }
        if (obtained.isEmpty()) return false;
        // Choosing the offered cards establishes this exact instance's permanent attacker exclusion.
        QStringList settled = ctx.owner->getSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "settled_attackers").toStringList();
        if (!settled.contains(target->objectName())) settled << target->objectName();
        ctx.owner->setSkillInstanceStateValue(objectName(), ctx.activationRef.key.instanceID, "settled_attackers", settled);
        room->setPlayerProperty(ctx.owner, "huisheng_targets", settled);
        DummyCard gift(obtained); room->obtainCard(target, &gift, CardMoveReason(CardMoveReason::S_REASON_EXTRACTION, target->objectName()), false); return true;
    }
};
KuangbiCard::KuangbiCard()
{
    setSkillName("kuangbi");
}

bool KuangbiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && !to_select->isNude();
}

void KuangbiCard::onEffect(CardEffectStruct &effect) const{
    if (effect.to->isNude()) return;
    Room *room = effect.from->getRoom();
    const Card *card = room->askForExchange(effect.to, "kuangbi", 3, 1, true, "kuangbi-put:" + effect.from->objectName());
    QVariantList ids = effect.to->getTag("kuangbi_ids" + effect.from->objectName()).toList();
    foreach (int id, card->getSubcards()) {
        if (ids.contains(id)) continue;
        ids << id;
    }
    effect.to->setTag("kuangbi_ids" + effect.from->objectName(), ids);
    effect.from->addToPile("kuangbi", card, true);
}

class KuangbiVS : public ViewAsSkillV2
{
public:
    KuangbiVS() : ViewAsSkillV2("kuangbi") { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return target && target->isAlive() && target != request.initiator && !target->isNude() && selected.isEmpty(); }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &selected) const override { return selected.size() == 1; }
    QString historyKey(const ActiveSkillRequest &) const override { return "KuangbiCard"; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = target->getRoom();
        if (ctx.choice != "store") {
            if (target->isNude() || getEffectiveAmount(ctx) <= 0) return ContinueEffects;
            const Card *gift = room->askForExchange(target, objectName(), 3 * getEffectiveAmount(ctx), 1, true, "kuangbi-put:" + ctx.invoker->objectName());
            if (!gift) return ContinueEffects;
            QVariantList ids; for (int id : gift->getSubcards()) ids << id;
            SkillContext store = ctx; store.choice = "store"; store.targets = {ctx.invoker}; store.extra_data = QVariantMap{{"donor", target->objectName()}, {"ids", ids}}; skillEffect(store, ctx.invoker); return ContinueEffects;
        }
        const QVariantMap gift = ctx.extra_data.toMap(); ServerPlayer *donor = room->findPlayerByObjectName(gift.value("donor").toString(), true);
        QList<int> ids;
        for (const QVariant &value : gift.value("ids").toList()) {
            const int id = value.toInt();
            if (!donor || ids.contains(id) || room->getCardOwner(id) != donor || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip) || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
            ids << id;
        }
        if (ids.isEmpty()) return ContinueEffects;
        const int serial = room->getTag("KuangbiSerial").toInt() + 1; room->setTag("KuangbiSerial", serial);
        const QVariantMap receipt{{"serial", serial}, {"donor", donor->objectName()}, {"recipient", target->objectName()},
            {"selected", ListI2V(ids)}, {"ids", QVariantList()},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        QVariantList receipts = target->getTag("KuangbiReceipts").toList(); receipts << receipt; target->setTag("KuangbiReceipts", receipts);
        // Movement recording commits exact arrivals before any nested next-turn opportunity.
        const auto settle = qScopeGuard([&] {
            QVariantList kept;
            for (const QVariant &entry : target->getTag("KuangbiReceipts").toList()) {
                QVariantMap saved = entry.toMap();
                if (saved.value("serial").toInt() != serial) kept << entry;
                else if (!saved.value("ids").toList().isEmpty()) { saved.remove("selected"); kept << saved; }
            }
            target->setTag("KuangbiReceipts", kept);
        });
        target->addToPile("kuangbi", ids, true); return ContinueEffects;
    }
};
class Kuangbi : public TriggerSkillV2
{
public:
    Kuangbi() : TriggerSkillV2("kuangbi") { events << EventPhaseStart << CardsMoveOneTime << EventSkillEffectFinished; view_as_skill = new KuangbiVS; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == EventSkillEffectFinished) {
            const SkillContext ctx = data.value<SkillContext>();
            if (ctx.skill_name == objectName() && !ctx.activationRef.isValid() && ctx.owner) {
                QVariantList due = ctx.owner->getTag("KuangbiDue").toList(); due.removeOne(ctx.extra_data); ctx.owner->setTag("KuangbiDue", due);
            }
        } else if (event == CardsMoveOneTime && player) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to != player || move.to_place != Player::PlaceSpecial || move.to_pile_name != "kuangbi") return true;
            QVariantList receipts;
            for (const QVariant &entry : player->getTag("KuangbiReceipts").toList()) {
                QVariantMap receipt = entry.toMap(); QVariantList arrived = receipt.value("ids").toList();
                for (int id : ListV2I(receipt.value("selected").toList()))
                    if (!arrived.contains(id) && move.card_ids.contains(id) && player->getPile("kuangbi").contains(id)) arrived << id;
                receipt["ids"] = arrived; receipts << receipt;
            }
            player->setTag("KuangbiReceipts", receipts);
        } else if (event == EventPhaseStart && player && player->getPhase() == Player::RoundStart) {
            QVariantList pending, due = player->getTag("KuangbiDue").toList();
            for (const QVariant &entry : player->getTag("KuangbiReceipts").toList()) {
                if (entry.toMap().value("ids").toList().isEmpty()) pending << entry;
                else due << entry;
            }
            player->setTag("KuangbiDue", due); player->setTag("KuangbiReceipts", pending);
        }
        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || player->isDead() || player->getPhase() != Player::RoundStart) return true;
        for (const QVariant &entry : player->getTag("KuangbiDue").toList()) {
            const QVariantMap receipt = entry.toMap(); SkillContext ctx; ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt();
            ctx.owner = ctx.initiator = ctx.invoker = player; ctx.targets = {player}; ctx.original_data = &data; ctx.current_event = event; ctx.extra_data = receipt;
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            if (ctx.instanceID > 0 && ctx.sourceRef.isValid()) contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return !ctx.activationRef.isValid() && ctx.owner && ctx.owner->isAlive() && ctx.owner->getTag("KuangbiDue").toList().contains(ctx.extra_data); }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { QVariantList due = ctx.owner->getTag("KuangbiDue").toList(); due.removeOne(ctx.extra_data); ctx.owner->setTag("KuangbiDue", due); return false; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") { target->drawCards(ctx.extra_data.toInt() * getEffectiveAmount(ctx), objectName()); return false; }
        const QVariantMap receipt = ctx.extra_data.toMap(); QList<int> ids;
        ServerPlayer *holder = room->findPlayerByObjectName(receipt.value("recipient").toString(), true);
        for (const QVariant &value : receipt.value("ids").toList()) if (holder && holder->getPile("kuangbi").contains(value.toInt())) ids << value.toInt();
        if (ids.isEmpty()) return false;
        DummyCard cards(ids); room->obtainCard(target, &cards, true);
        ServerPlayer *donor = room->findPlayerByObjectName(receipt.value("donor").toString());
        // The donor's draw has a separate recipient boundary from returning the actual pile cards.
        if (donor) { SkillContext draw = ctx; draw.choice = "draw"; draw.targets = {donor}; draw.extra_data = ids.size(); skillEffect(event, room, player, draw, donor); }
        return false;
    }
};
JisheDrawCard::JisheDrawCard()
{
    m_skillName = "jishe";
    target_fixed = true;
}

void JisheDrawCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    source->drawCards(1, "jishe");
    room->addMaxCards(source, -1);
}

JisheCard::JisheCard()
{
    setSkillName("jishe");
}

bool JisheCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
    return targets.length() < Self->getHp();
}

void JisheCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    foreach (ServerPlayer *p, targets) {
        if (!p->isAlive() || p->isChained()) continue;
        room->cardEffect(this, source, p);
    }
}

void JisheCard::onEffect(CardEffectStruct &effect) const
{
    if (effect.to->isChained()) return;
    effect.from->getRoom()->setPlayerChained(effect.to);
}

class JisheVS : public ViewAsSkillV2
{
public:
    JisheVS() : ViewAsSkillV2("jishe") { response_pattern = "@@jishe"; }
    TargetMode targetMode() const override { return SelectTargets; }
    QString historyKey(const ActiveSkillRequest &request) const override
    { return request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? "JisheDrawCard" : "JisheCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return request.initiator->getMaxCards() > 0;
        return request.pattern == "@@jishe" && request.initiator->isKongcheng() && request.initiator->getHp() > 0;
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return request.initiator && request.reason != CardUseStruct::CARD_USE_REASON_PLAY && candidate
            && candidate->isAlive() && !selected.contains(candidate) && selected.size() < request.initiator->getHp();
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return selected.isEmpty();
        if (selected.isEmpty()) return false;
        QList<const Player *> checked;
        for (const Player *target : selected) {
            if (!canSelectTarget(request, checked, target)) return false;
            checked << target;
        }
        return true;
    }
    bool cost(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        ctx.choice = request.reason == CardUseStruct::CARD_USE_REASON_PLAY ? "draw" : "chain";
        return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (ctx.choice != "draw") return ContinueEffects;
        // The no-selection play action still has a real self recipient.
        ctx.targets = {ctx.invoker};
        skillEffect(ctx, ctx.invoker);
        return FinishSkill;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        if (ctx.choice == "draw") {
            target->drawCards(getEffectiveAmount(ctx), objectName());
            room->addMaxCards(target, -getEffectiveAmount(ctx));
        } else if (!target->isChained()) room->setPlayerChained(target);
        return ContinueEffects;
    }
};

class Jishe : public TriggerSkillV2
{
public:
    Jishe() : TriggerSkillV2("jishe") { events << EventPhaseStart; view_as_skill = new JisheVS; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
            && player->isKongcheng() && player->getHp() > 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        Room::AcceptedViewAsEffectScope source(room, ctx.owner, objectName(), ctx);
        if (source.isValid()) room->askForUseCard(ctx.owner, "@@jishe", "@jishe:" + QString::number(ctx.owner->getHp()));
        return false;
    }
};
class Lianhuo : public TriggerSkillV2
{
public:
    Lianhuo() : TriggerSkillV2("lianhuo")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && player->isChained()
                && damage.nature == DamageStruct::Fire && !damage.chain
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (target != damage.to) return false;
        LogMessage log;
        log.type = "#LianhuoDamage";
        log.from = ctx.invoker;
        log.arg = QString::number(damage.damage);
        damage.damage += getEffectiveAmount(ctx);
        log.arg2 = QString::number(damage.damage);
        room->sendLog(log);
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(ctx.invoker, objectName());
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

ZhigeCard::ZhigeCard()
{
    setSkillName("zhige");
}

bool ZhigeCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->inMyAttackRange(Self) && to_select != Self;
}

void ZhigeCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    bool use_slash = false;
    QList<ServerPlayer *> targets;
    foreach(ServerPlayer *p, room->getAlivePlayers()) {
        if (effect.to->canSlash(p, nullptr, true))
            targets << p;
    }
    if (!targets.isEmpty())
        use_slash = room->askForUseSlashTo(effect.to, targets, "zhige-slash");
    if (!use_slash && effect.to->hasEquip()) {
        const Card *card = room->askForCard(effect.to, ".|.|.|equipped!", "zhige-give:" + effect.from->objectName(),
              QVariant::fromValue(effect.from), Card::MethodNone);
        if (!card)
            card = effect.to->getCards("e").at(qsanRandomBounded(effect.to->getCards("e").length()));
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "zhige", "");
        room->obtainCard(effect.from, card, reason, true);
    }
}

class Zhige : public ViewAsSkillV2
{
public:
    Zhige() : ViewAsSkillV2("zhige") { setPhaseName("Play");}
    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "ZhigeCard"; }
    TargetMode targetMode() const override { return SelectTargets; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getHandcardNum() > request.initiator->getHp();
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return request.initiator && selected.isEmpty() && candidate && candidate->isAlive()
            && candidate != request.initiator && candidate->inMyAttackRange(request.initiator);
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return selected.size() == 1 && canSelectTarget(request, {}, selected.first());
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        QList<ServerPlayer *> slashTargets;
        for (ServerPlayer *player : room->getAlivePlayers())
            if (target->canSlash(player, nullptr, true)) slashTargets << player;
        if (!slashTargets.isEmpty() && room->askForUseSlashTo(target, slashTargets, "zhige-slash")) return ContinueEffects;
        if (!target->isAlive() || !ctx.invoker->isAlive() || !target->hasEquip()) return ContinueEffects;
        const Card *selected = room->askForCard(target, ".|.|.|equipped!", "zhige-give:" + ctx.invoker->objectName(),
            QVariant::fromValue(ctx.invoker), Card::MethodNone);
        int id = selected ? selected->getEffectiveId() : -1;
        // Refusal chooses a current equipment card; stale replies cannot give away another owner's material.
        QList<int> equips;
        for (int equipId : target->getEquipsId())
            if (!Sanguosha->getCard(equipId)->hasFlag("using")) equips << equipId;
        if (!equips.contains(id)) {
            if (equips.isEmpty()) return ContinueEffects;
            id = equips.at(qsanRandomBounded(equips.size()));
        }
        room->obtainCard(ctx.invoker, Sanguosha->getCard(id), CardMoveReason(CardMoveReason::S_REASON_GIVE,
            target->objectName(), ctx.invoker->objectName(), objectName(), ""), true);
        return ContinueEffects;
    }
};
class Zongzuo : public TriggerSkillV2
{
public:
    Zongzuo() : TriggerSkillV2("zongzuo") { events << GameStart; frequency = Compulsory; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QStringList kingdoms;
        for (ServerPlayer *player : room->getAlivePlayers())
            if (!kingdoms.contains(player->getKingdom())) kingdoms << player->getKingdom();
        if (kingdoms.isEmpty()) return false;
        room->sendCompulsoryTriggerLog(ctx.owner, objectName(), true, true);
        const int gain = kingdoms.size() * getEffectiveAmount(ctx);
        room->gainMaxHp(target, gain, objectName());
        room->recover(target, RecoverStruct(ctx.owner, nullptr, qMin(gain, target->getLostHp()), objectName()));
        return false;
    }
};

class ZongzuoDeath : public TriggerSkillV2
{
public:
    ZongzuoDeath() : TriggerSkillV2("#zongzuodeath") { events << Death; frequency = Compulsory; }

    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const DeathStruct death = data.value<DeathStruct>();
        // Death is broadcast per observer; only this holder supplies its own source.
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !death.who || death.who == player) return {};
        for (ServerPlayer *other : room->getAlivePlayers())
            if (other->getKingdom() == death.who->getKingdom()) return {};
        return {{player, {objectName()}}};
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, "zongzuo", true, true);
        room->loseMaxHp(target, getEffectiveAmount(ctx), "zongzuo");
        return false;
    }
};

TaoluanCard::TaoluanCard(QString this_skill_name) : this_skill_name(this_skill_name)
{
    setSkillName(this_skill_name);
    mute = true;
    handling_method = Card::MethodUse;
    will_throw = false;
}

bool TaoluanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        if (!user_string.isEmpty()){
            Card *card = Sanguosha->cloneCard(user_string.split("+").first());
			card->deleteLater();
			return card->targetFilter(targets, to_select, Self);
		}
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return false;
    }
    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *card = Self ? Self->getTag(this_skill_name).value<const Card *>() : nullptr;
    if (!card) {
        Card *declared = Sanguosha->cloneCard(user_string.split("+").first());
        if (declared) declared->deleteLater();
        card = declared;
    }
    return card && card->targetFilter(targets, to_select, Self);
}

bool TaoluanCard::targetFixed() const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        if (!user_string.isEmpty()){
            Card *card = Sanguosha->cloneCard(user_string.split("+").first());
			card->deleteLater();
			return card->targetFixed();
		}
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return true;
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *card = Self ? Self->getTag(this_skill_name).value<const Card *>() : nullptr;
    if (!card) {
        Card *declared = Sanguosha->cloneCard(user_string.split("+").first());
        if (declared) declared->deleteLater();
        card = declared;
    }
    return card && card->targetFixed();
}

bool TaoluanCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        if (!user_string.isEmpty()){
            Card *card = Sanguosha->cloneCard(user_string.split("+").first());
			card->deleteLater();
			return card->targetsFeasible(targets, Self);
		}
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return true;
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *card = Self ? Self->getTag(this_skill_name).value<const Card *>() : nullptr;
    if (!card) {
        Card *declared = Sanguosha->cloneCard(user_string.split("+").first());
        if (declared) declared->deleteLater();
        card = declared;
    }
    return card && card->targetsFeasible(targets, Self);
}

const Card *TaoluanCard::validate(CardUseStruct &card_use) const
{
    Room *room = card_use.from->getRoom();

    QString tl = user_string;
    if (user_string == "slash" && Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        QStringList tl_list;
        if (card_use.from->getMark(this_skill_name + "_slash") <= 0)
            tl_list << "slash";
        if (!Config.BanPackages.contains("maneuvering")) {
            if (card_use.from->getMark(this_skill_name + "_thunder_slash") <= 0)
                tl_list << "thunder_slash";
            if (card_use.from->getMark(this_skill_name + "_fire_slash") <= 0)
                tl_list << "fire_slash";
        }
        if (tl_list.isEmpty()) return nullptr;
        tl = room->askForChoice(card_use.from, "taoluan_slash", tl_list.join("+"));
    }
    if (card_use.from->getMark(this_skill_name + "_" + tl) > 0) return nullptr;

    Card *use_card = Sanguosha->cloneCard(tl);
    use_card->setSkillName(this_skill_name);
    use_card->addSubcard(subcards.first());
	use_card->deleteLater();

    room->setCardFlag(use_card, this_skill_name);
    room->addPlayerMark(card_use.from, this_skill_name + "_" + tl);
    room->addPlayerMark(card_use.from, this_skill_name + "_" + getSuitString() + "-Clear");

    return use_card;
}

const Card *TaoluanCard::validateInResponse(ServerPlayer *zhangrang) const
{
    Room *room = zhangrang->getRoom();

    QString tl = user_string;
    if (user_string == "peach+analeptic") {
        QStringList tl_list;
        if (zhangrang->getMark(this_skill_name + "_peach") <= 0)
            tl_list << "peach";
        if (!Config.BanPackages.contains("maneuvering") && zhangrang->getMark(this_skill_name + "_analeptic") <= 0)
            tl_list << "analeptic";
        if (tl_list.isEmpty()) return nullptr;
        tl = room->askForChoice(zhangrang, "taoluan_saveself", tl_list.join("+"));
    } else if (user_string == "slash") {
        QStringList tl_list;
        if (zhangrang->getMark(this_skill_name + "_slash") <= 0)
            tl_list << "slash";
        if (!Config.BanPackages.contains("maneuvering")) {
            if (zhangrang->getMark(this_skill_name + "_thunder_slash") <= 0)
                tl_list << "thunder_slash";
            if (zhangrang->getMark(this_skill_name + "_fire_slash") <= 0)
                tl_list << "fire_slash";
        }
        if (tl_list.isEmpty()) return nullptr;
        tl = room->askForChoice(zhangrang, "taoluan_slash", tl_list.join("+"));
    }

    if (zhangrang->getMark(this_skill_name + "_" + tl) > 0) return nullptr;

    Card *use_card = Sanguosha->cloneCard(tl);
    use_card->setSkillName(this_skill_name);
    use_card->addSubcard(subcards.first());
	use_card->deleteLater();

    room->setCardFlag(use_card, this_skill_name);
    room->addPlayerMark(zhangrang, this_skill_name + "_" + tl);
    room->addPlayerMark(zhangrang, this_skill_name + "_" + getSuitString() + "-Clear");

    return use_card;
}

class TaoluanVS : public ViewAsSkillV2
{
public:
    TaoluanVS(const QString &name = "taoluan") : ViewAsSkillV2(name, 1) { response_or_use = true; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        if (!ctx.owner || !ctx.use_card) return false;
        const QVariantMap state = ctx.owner->getSkillInstanceState(objectName(), ctx.activationRef.key.instanceID);
        return !state.value("invalid").toBool() && !state.value("used_names").toStringList().contains(ctx.use_card->objectName());
    }
    SkillDialogInfo getDialogInfo() const override
    { SkillDialogInfo info = SkillDialogInfo::named("taoluan", objectName()); info.parameters.insert("declarationType", "guhuo"); return info; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.initiator->isNude() || request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "invalid").toBool()) return false;
        return request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            || ((request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE || request.pattern == "nullification")
                && !request.pattern.startsWith(".") && !request.pattern.startsWith("@") && !usableNames(request).isEmpty());
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty() && card && !card->hasFlag("using")
            && (request.initiator->handCards().contains(card->getEffectiveId()) || request.initiator->getEquipsId().contains(card->getEffectiveId()))
            && (objectName() != "tenyeartaoluan" || !request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "used_suits").toStringList().contains(card->getSuitString()));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    { ActiveSkillRequest empty = request; empty.selectedCardIds.clear(); return request.selectedCardIds.size() == 1 && canSelectCard(empty, Sanguosha->getCard(request.selectedCardIds.first())); }
    static void commit(const SkillContext &ctx)
    {
        if (!ctx.owner || !ctx.use_card || !ctx.activationRef.isValid()) return;
        const QString name = ctx.activationRef.key.skillName; const int id = ctx.activationRef.key.instanceID;
        QVariantMap state = ctx.owner->getSkillInstanceState(name, id);
        QStringList names = state.value("used_names").toStringList(); if (!names.contains(ctx.use_card->objectName())) names << ctx.use_card->objectName(); state.insert("used_names", names);
        if (name == "tenyeartaoluan" && ctx.use_card->getSubcards().size() == 1) {
            const QString suit = Sanguosha->getCard(ctx.use_card->getSubcards().first())->getSuitString();
            QStringList suits = state.value("used_suits").toStringList(); if (!suits.contains(suit)) suits << suit; state.insert("used_suits", suits);
            const QString turn = QString::number(ctx.owner->getRoom()->historyScopes().value("turn_id").toLongLong());
            QVariantMap turns = state.value("turn_usage").toMap(), current = turns.value(turn).toMap(); current.insert("suits", suits); turns.insert(turn, current); state.insert("turn_usage", turns);
        }
        ctx.owner->setSkillInstanceState(name, id, state);
    }
    bool pay(Room *, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request) || !ctx.use_card || !canDeclare(request, ctx.use_card->objectName()) || !isUsable(ctx)) return false;
        commit(ctx); addUsage(ctx); return true;
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        if (!ctx.invoker || !ctx.use_card || ctx.executionID <= 0) return FinishSkill;
        Room *room = ctx.invoker->getRoom(); const int serial = room->getTag("TaoluanSerial").toInt() + 1; room->setTag("TaoluanSerial", serial);
        const QVariantMap receipt{{"serial", serial}, {"turn", room->historyScopes().value("turn_id")}, {"execution", ctx.executionID}, {"skill", objectName()},
            {"actor", ctx.invoker->objectName()}, {"issuer", ctx.owner->objectName()}, {"source", jiaozhaoRef(ctx.sourceRef)},
            {"activation", jiaozhaoRef(ctx.activationRef)}, {"type", ctx.use_card->getType()}, {"type_id", ctx.use_card->getTypeId()}, {"amount", getEffectiveAmount(ctx)}};
        QVariantList receipts = room->getTag("TaoluanContinuations").toList(); receipts << receipt; room->setTag("TaoluanContinuations", receipts);
        return ContinueEffects;
    }
protected:
    bool allowDeclaration(const ActiveSkillRequest &request, const QString &name) const override
    { return request.initiator && name != "normal_slash" && !request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "used_names").toStringList().contains(name); }
};

class Taoluan : public TriggerSkillV2
{
public:
    Taoluan(const QString &name = "taoluan") : TriggerSkillV2(name)
    {
        view_as_skill = new TaoluanVS(name); global = true; frequency = Compulsory;
        events << CardFinished << PostCardResponded << EventPhaseChanging << TurnStart << EventSkillInvoking << EventSkillEffectFinished;
    }
    SkillDialogInfo getDialogInfo() const override { return view_as_skill->getDialogInfo(); }
    SkillDeclarationReason declarationReason(const ActiveSkillRequest &request, const QString &name, const Card *card) const override
    { return view_as_skill->declarationReason(request, name, card); }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == EventSkillInvoking) {
            const SkillContext ctx = data.value<SkillContext>();
            if (ctx.bypass_cost && ctx.activationRef.key.skillName == objectName()) TaoluanVS::commit(ctx);
        } else if (event == EventSkillEffectFinished) {
            const SkillContext ctx = data.value<SkillContext>();
            if (ctx.skill_name == objectName() && !ctx.activationRef.isValid()) {
                const QString tag = ctx.choice == "penalty" ? "TaoluanPenalties" : "TaoluanContinuations";
                QVariantList pending = room->getTag(tag).toList(); pending.removeOne(ctx.extra_data); room->setTag(tag, pending); return true;
            }
            if (ctx.activationRef.key.skillName != objectName() || ctx.executionID <= 0) return true;
            QVariantList kept;
            for (const QVariant &entry : room->getTag("TaoluanContinuations").toList()) {
                const QVariantMap receipt = entry.toMap();
                if (receipt.value("execution").toLongLong() != ctx.executionID || jiaozhaoReadRef(receipt.value("activation")) != ctx.activationRef) kept << entry;
            }
            room->setTag("TaoluanContinuations", kept);
        } else if (event == TurnStart || (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive)) {
            const qint64 turn = room->historyScopes().value("turn_id").toLongLong(); if (turn <= 0) return true;
            const qint64 active = event == TurnStart ? turn : room->historyParent(turn, "turn", false).value("id").toLongLong();
            for (ServerPlayer *owner : room->getAllPlayers(true)) for (int id : owner->getSkillInstanceIds(objectName())) {
                QVariantMap state = owner->getSkillInstanceState(objectName(), id), turns = state.value("turn_usage").toMap();
                if (event != TurnStart) turns.remove(QString::number(turn));
                const QVariantMap current = turns.value(QString::number(active)).toMap();
                state.insert("turn_usage", turns); state.insert("invalid", current.value("invalid").toBool()); state.insert("used_suits", current.value("suits").toStringList());
                owner->setSkillInstanceState(objectName(), id, state);
            }
        }        return true;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        const bool penalty = event == EventPhaseChanging && objectName() == "tenyeartaoluan" && data.value<PhaseChangeStruct>().to == Player::NotActive;
        qint64 execution = 0; SkillInstanceRef activation;
        if (event == CardFinished) { const CardUseStruct use = data.value<CardUseStruct>(); execution = use.skillExecutionID; activation = use.activationRef; }
        else if (event == PostCardResponded) { const CardResponseStruct response = data.value<CardResponseStruct>(); if (!response.m_isUse) return true; execution = response.skillExecutionID; activation = response.activationRef; }
        else if (!penalty) return true;
        for (const QVariant &entry : room->getTag(penalty ? "TaoluanPenalties" : "TaoluanContinuations").toList()) {
            const QVariantMap receipt = entry.toMap(); if (receipt.value("skill").toString() != objectName() || (penalty && receipt.value("turn").toLongLong() != room->historyScopes().value("turn_id").toLongLong())) continue;
            ServerPlayer *actor = room->findPlayerByObjectName(receipt.value("actor").toString());
            if (!actor || actor->isDead() || (!penalty && (player != actor || execution <= 0 || receipt.value("execution").toLongLong() != execution || jiaozhaoReadRef(receipt.value("activation")) != activation))) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.instanceID = receipt.value("serial").toInt(); ctx.invoker = actor;
            ctx.owner = room->findPlayerByObjectName(receipt.value("issuer").toString(), true); ctx.initiator = ctx.owner;
            ctx.sourceRef = jiaozhaoReadRef(receipt.value("source")); ctx.extra_data = receipt; ctx.amount = receipt.value("amount").toInt();
            ctx.choice = penalty ? "penalty" : "request"; if (penalty) ctx.targets = {actor}; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        return !ctx.activationRef.isValid() && ctx.invoker && ctx.invoker->isAlive()
            && room->getTag(ctx.choice == "penalty" ? "TaoluanPenalties" : "TaoluanContinuations").toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (ctx.choice == "penalty") return true;
        const QList<ServerPlayer *> others = room->getOtherPlayers(ctx.invoker);
        if (others.isEmpty()) { ctx.choice = "failure"; ctx.targets = {ctx.invoker}; return true; }
        ServerPlayer *target = room->askForPlayerChosen(ctx.invoker, others, objectName(), "@taoluan-choose");
        if (!others.contains(target)) return false; ctx.targets = {target}; return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QString tag = ctx.choice == "penalty" ? "TaoluanPenalties" : "TaoluanContinuations";
        QVariantList receipts = room->getTag(tag).toList(); receipts.removeOne(ctx.extra_data); room->setTag(tag, receipts); return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        const QVariantMap receipt = ctx.extra_data.toMap();
        if (ctx.choice == "penalty") { room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker)); return false; }
        if (ctx.choice == "failure") {
            const SkillInstanceRef activation = jiaozhaoReadRef(receipt.value("activation"));
            ServerPlayer *owner = room->findPlayerByObjectName(activation.ownerObjectName, true);
            if (owner && owner->findSkillInstance(activation.key.skillName, activation.key.instanceID)) {
                QVariantMap state = owner->getSkillInstanceState(activation.key.skillName, activation.key.instanceID), turns = state.value("turn_usage").toMap();
                const QString turn = QString::number(receipt.value("turn").toLongLong()); QVariantMap current = turns.value(turn).toMap(); current.insert("invalid", true); turns.insert(turn, current);
                state.insert("turn_usage", turns); state.insert("invalid", true); owner->setSkillInstanceState(activation.key.skillName, activation.key.instanceID, state);
            }
            if (objectName() == "tenyeartaoluan") {
                QVariantMap delayed = receipt; delayed.insert("actor", target->objectName()); delayed.insert("amount", getEffectiveAmount(ctx));
                QVariantList penalties = room->getTag("TaoluanPenalties").toList(); penalties << delayed; room->setTag("TaoluanPenalties", penalties);
            } else room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
            return false;
        }
        if (ctx.choice == "receive") {
            ServerPlayer *donor = room->findPlayerByObjectName(receipt.value("donor").toString());
            DummyCard gift;
            if (!donor) return false;
            for (const QVariant &value : receipt.value("gifts").toList()) {
                const int id = value.toInt();
                if (id < 0 || room->getCardOwner(id) != donor || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                    || Sanguosha->getCard(id)->hasFlag("using") || Sanguosha->getCard(id)->getTypeId() == receipt.value("type_id").toInt()) return false;
                gift.addSubcard(id);
            }
            if (!gift.getSubcards().isEmpty()) room->obtainCard(target, &gift, CardMoveReason(CardMoveReason::S_REASON_GIVE, donor->objectName(), target->objectName(), objectName(), QString()), true);
            return false;
        }
        const int amount = getEffectiveAmount(ctx); if (amount <= 0) return false;
        QStringList types{"BasicCard", "TrickCard", "EquipCard"}; const int type = receipt.value("type_id").toInt(); if (type >= 1 && type <= 3) types.removeAt(type - 1);
        const Card *card = room->askForExchange(target, objectName(), amount, amount, true,
            "@taoluan-give:" + ctx.invoker->objectName() + "::" + receipt.value("type").toString(), true, types.join(","));
        QVariantList ids; bool given = card && card->subcardsLength() == amount;
        if (given) for (int id : card->getSubcards()) {
            if (room->getCardOwner(id) != target || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
                || Sanguosha->getCard(id)->hasFlag("using") || Sanguosha->getCard(id)->getTypeId() == type || ids.contains(id)) { given = false; break; }
            ids << id;
        }
        SkillContext follow = ctx; follow.choice = given ? "receive" : "failure"; follow.targets = {ctx.invoker};
        QVariantMap next = receipt; if (given) { next.insert("gifts", ids); next.insert("donor", target->objectName()); } follow.extra_data = next;        skillEffect(event, room, player, follow, ctx.invoker); return false;
    }
};

TenyearTaoluanCard::TenyearTaoluanCard() : TaoluanCard("tenyeartaoluan")
{
    mute = true; handling_method = Card::MethodUse; will_throw = false;
}
class TenyearTaoluan : public Taoluan
{
public:
    TenyearTaoluan() : Taoluan("tenyeartaoluan") {}
};
YCZH2016Package::YCZH2016Package()
    : Package("YCZH2016")
{
    General *guohuanghou = new General(this, "guohuanghou", "wei", 3, false);
    guohuanghou->addSkill(new Jiaozhao);
    guohuanghou->addSkill(new JiaozhaoPro);
    guohuanghou->addSkill(new Danxin);
    guohuanghou->addSkill(new DanxinFuture);
    related_skills.insert("danxin", "#danxin-future");
    related_skills.insert("jiaozhao", "#jiaozhaopro");

    General *sunziliufang = new General(this, "sunziliufang", "wei", 3);
    sunziliufang->addSkill(new Guizao);
    sunziliufang->addSkill(new Jiyu);
    sunziliufang->addSkill(new JiyuRecord);
    related_skills.insert("jiyu", "#jiyu-record");

    General *liyan = new General(this, "liyan", "shu", 3);
    liyan->addSkill(new Duliang);
    liyan->addSkill(new Fulin);
    liyan->addSkill(new FulinBF);
    related_skills.insert("fulin", "#fulinbf");

    General *huanghao = new General(this, "huanghao", "shu", 3);
    huanghao->addSkill(new Qinqing);
    huanghao->addSkill(new Huisheng);

    General *sundeng = new General(this, "sundeng", "wu", 4);
    sundeng->addSkill(new Kuangbi);

    General *cenhun = new General(this, "cenhun", "wu", 3);
    cenhun->addSkill(new Jishe);
    cenhun->addSkill(new Lianhuo);

    General *liuyu = new General(this, "liuyu", "qun", 2);
    liuyu->addSkill(new Zhige);
    liuyu->addSkill(new Zongzuo);
    liuyu->addSkill(new ZongzuoDeath);
    related_skills.insert("zongzuo", "#zongzuodeath");

    General *zhangrang = new General(this, "zhangrang", "qun", 3);
    zhangrang->addSkill(new Taoluan);
	skills << new TenyearTaoluan;


    addMetaObject<JiaozhaoCard>();
    addMetaObject<JiyuCard>();
    addMetaObject<DuliangCard>();
    addMetaObject<QinqingCard>();
    addMetaObject<HuishengCard>();
    addMetaObject<KuangbiCard>();
    addMetaObject<JisheDrawCard>();
    addMetaObject<JisheCard>();
    addMetaObject<ZhigeCard>();
    addMetaObject<TaoluanCard>();
    addMetaObject<TenyearTaoluanCard>();

}

ADD_PACKAGE(YCZH2016)
