#include "hayate.h"
#include "serverplayer.h"
#include "room.h"
#include "skill.h"
#include "maneuvering.h"
#include "clientplayer.h"
#include "engine.h"
#include "client.h"
#include "exppattern.h"
#include "roomthread.h"
#include "wrapped-card.h"
#include "json.h"
#include "settings.h"
#include "game-rng.h"
#include <QScopeGuard>

namespace {
ActiveSkillCard *hayateProxy(const ViewAsSkillV2 *skill, const ActiveSkillRequest &request)
{
    ActiveSkillCard *card = new ActiveSkillCard;
    card->setActiveSkill(skill);
    card->setSkillName(skill->objectName());
    card->addSubcards(request.selectedCardIds);
    card->setUserString(request.userString);
    return card;
}
}

namespace {
int yingdiHasDamaged(Room *room, const ServerPlayer *owner, const ServerPlayer *target)
{
    const QVariantMap page = room->queryActualDamage({{"from", owner->objectName()},
        {"to", target->objectName()}, {"limit", 1}});
    if (!page.value("items").toList().isEmpty()) return 1;
    return page.value("complete").toBool() ? 0 : -1;
}
}

class Yingdi : public TriggerSkillV2
{
public:
    Yingdi() : TriggerSkillV2("yingdi")
    { frequency = Compulsory; events << TargetConfirmed << Damage << EventPhaseStart; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event == Damage) {
            const DamageStruct damage = data.value<DamageStruct>();
            // Retain the old AI/UI hint only as a projection; rule admission is per actor history.
            if (damage.from && damage.from->hasSkill(objectName()) && damage.to)
                room->setPlayerMark(damage.to, "@real_hei", 1);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == TargetConfirmed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !use.to.contains(player)
                || !use.card || !use.from || use.from->getAI() || (!use.card->isKindOf("Slash") && !use.card->isKindOf("Duel"))) return {};
            return yingdiHasDamaged(room, player, use.from) == 0 ? TriggerList{{player, {objectName()}}} : TriggerList();
        }
        if (event == EventPhaseStart && player && player->isAlive() && player->hasSkill(objectName())
            && player->getPhase() == Player::RoundStart) {
            QStringList roles;
            for (ServerPlayer *alive : room->getAlivePlayers())
                if (!roles.contains(alive->getRole())) roles << alive->getRole();
            if (roles.size() == 2) return {{player, {objectName()}}};
        }
        return {};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets << owner; return true; }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == TargetConfirmed) {
            CardUseStruct use = ctx.original_data->value<CardUseStruct>();
            if (target != owner || !use.to.contains(target) || !use.from || yingdiHasDamaged(room, owner, use.from) != 0) return false;
            if (!use.nullified_list.contains(target->objectName())) use.nullified_list << target->objectName();
            room->broadcastSkillInvoke(objectName(), qsanRandomBounded(3) + 1);
            *ctx.original_data = QVariant::fromValue(use);
        } else if (target == owner && owner->hasSkillInstance(ctx.activationRef.key.skillName, ctx.activationRef.key.instanceID)) {
            room->broadcastSkillInvoke(objectName(), 4);
            // Transform only the invoking grant; another Yingdi copy keeps its own identity.
            room->detachSkillFromPlayer(owner, SkillInstanceUtils::formatName(ctx.activationRef.key.skillName,
                ctx.activationRef.key.instanceID), false, false);
            if (owner->isAlive()) room->acquireSkill(owner, "jiesha");
        }
        return false;
    }
};

class YingdiClear : public DetachEffectSkill
{
public:
    YingdiClear() : DetachEffectSkill("yingdi") {}
    void onSkillDetached(Room *room, ServerPlayer *) const override
    {
        if (!room->findPlayersBySkillName("yingdi").isEmpty()) return;
        for (ServerPlayer *target : room->getAllPlayers(true)) room->setPlayerMark(target, "@real_hei", 0);
    }
};

class Diansuo : public TriggerSkillV2
{
public:
    Diansuo() : TriggerSkillV2("diansuo") { events << DamageCaused; global = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) {
            const bool movingSource = damage.from == owner;
            if (owner->isChained() == movingSource) continue;
            for (ServerPlayer *target : room->getOtherPlayers(owner))
                if (target->isChained() != movingSource) { result.insert(owner, {objectName()}); break; }
        }
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const bool movingSource = ctx.original_data->value<DamageStruct>().from == owner;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getOtherPlayers(owner))
            if (target->isChained() != movingSource) candidates << target;
        if (candidates.isEmpty() || !owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ServerPlayer *target = room->askForPlayerChosen(owner, candidates, objectName(),
            movingSource ? "@diansuo-prompt" : "@diansuo-prompt-remove");
        if (!target) return false;
        ctx.targets << target;
        ctx.extra_data = movingSource;
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        const bool chained = ctx.extra_data.toBool();
        if (ctx.choice == "ownerChain") {
            if (target->isChained() != chained) room->setPlayerChained(target);
            return false;
        }
        if (target->isChained() == chained || owner->isChained() == chained) return false;
        ctx.choice = "ownerChain";
        skillEffect(event, room, owner, ctx, owner);
        if (owner->isChained() != chained) return false;
        if (target->isChained() != chained) room->setPlayerChained(target);
        if (target->isChained() != chained) return false;
        room->broadcastSkillInvoke(objectName(), chained ? qsanRandomBounded(2) + 1 : 3);
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (chained) damage.from = target;
        else damage.to = target;
        *ctx.original_data = QVariant::fromValue(damage);
        return false;
    }
};

class Jiesha : public TriggerSkillV2
{
public:
    Jiesha() : TriggerSkillV2("jiesha") { frequency = Compulsory; events << SlashProceed; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const override
    {
        const SlashEffectStruct effect = data.value<SlashEffectStruct>();
        return effect.from && effect.to && effect.from->hasSkill(objectName()) && effect.from->getWeapon()
            && effect.from->getWeapon()->isKindOf("DoubleSword")
            ? TriggerList{{effect.from, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<SlashEffectStruct>().to;
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.manual_effect = true; return !ctx.targets.isEmpty() && skillEffect(event, room, owner, ctx, ctx.targets.first()); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        SlashEffectStruct effect = ctx.original_data->value<SlashEffectStruct>();
        if (target != effect.to || !effect.slash) return false;
        LogMessage log; log.type = "$jiesha_effect"; log.from = target; log.arg = effect.slash->objectName();
        room->sendLog(log);
        room->slashResult(effect, nullptr);
        room->broadcastSkillInvoke(objectName());
        return true;
    }
};

// for oumashu
class LonelinessInvalidity : public InvaliditySkill
{
public:
    LonelinessInvalidity() : InvaliditySkill("#loneliness-inv")
    {
    }

    bool isSkillValid(const Player *player, const Skill *skill) const
    {
        return player->getMark("Loneliness" + skill->objectName()) == 0;
    }
};



// akame


// diarmuid
class Pomo : public TriggerSkillV2
{
public:
    Pomo() : TriggerSkillV2("pomo")
    { frequency = Compulsory; events << TargetSpecified << CardFinished; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != CardFinished) return false;
        const Card *card = data.value<CardUseStruct>().card;
        if (!card) return false;
        const qint64 useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        QVariantList remaining, expired;
        for (const QVariant &value : card->getTag("PomoRestrictions").toList())
            if (value.toMap().value("use").toLongLong() == useId) expired << value;
            else remaining << value;
        card->setTag("PomoRestrictions", remaining);
        for (const QVariant &value : expired) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *target = room->findPlayerByObjectName(receipt.value("target").toString(), true);
            if (!target) continue;
            const QString reason = receipt.value("reason").toString();
            target->removeEquipsNullified("Armor", reason, false);
            room->removeSkillInvalidity(target, "all", objectName(), reason);
            room->filterCards(target, target->getCards("he"), true);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return event == TargetSpecified && player && player == use.from && player->hasSkill(objectName())
            && use.card && use.card->isKindOf("Slash") ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = ctx.original_data->value<CardUseStruct>().to; return !ctx.targets.isEmpty(); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const Card *card = ctx.original_data->value<CardUseStruct>().card;
        if (!card) return false;
        const qint64 useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        // Trigger contexts do not allocate an active-skill execution ID. Keep each
        // applied restriction distinct even when two instances affect one target.
        const quint64 serial = room->getTag("PomoRestrictionSerial").toULongLong() + 1;
        room->setTag("PomoRestrictionSerial", QVariant::fromValue(serial));
        const QString reason = objectName() + ":" + ctx.sourceRef.ownerObjectName + ":"
            + QString::number(ctx.sourceRef.key.instanceID) + ":" + QString::number(serial);
        QVariantList receipts = card->getTag("PomoRestrictions").toList();
        receipts << QVariantMap{{"target", target->objectName()}, {"reason", reason}, {"use", useId}};
        card->setTag("PomoRestrictions", receipts);
        // Named applied effects survive source loss and never clear another skill's restriction.
        target->addEquipsNullified("Armor", reason, false);
        room->addSkillInvalidity(target, "all", objectName(), reason);
        room->filterCards(target, target->getCards("he"), true);
        return false;
    }
};

class Bimie : public TriggerSkillV2
{
public:
    Bimie() : TriggerSkillV2("bimie") { events << Damage; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        return player && player->isAlive() && player == damage.from && player->hasSkill(objectName())
            && damage.to && damage.to->isAlive() && damage.to->getMark("@zhou") == 0
            && damage.card && damage.card->isKindOf("Slash") && !damage.chain && !damage.transfer
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
        if (!target) return false;
        ctx.targets << target;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target->getMark("@zhou") > 0 || getEffectiveAmount(ctx) <= 0) return false;
        room->broadcastSkillInvoke(objectName());
        room->doLightbox("bimie$", 1500);
        target->setTag("BimieCurse", QVariantMap{{"owner", ctx.sourceRef.ownerObjectName},
            {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}});
        room->setPlayerMark(target, "@zhou", getEffectiveAmount(ctx));
        return false;
    }
};

class BimieCurse : public TriggerSkillV2
{
public:
    BimieCurse() : TriggerSkillV2("#bimie-curse")
    { events << PreHpRecover << Death; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != Death || !data.value<DeathStruct>().who) return false;
        const QString owner = data.value<DeathStruct>().who->objectName();
        for (ServerPlayer *target : room->getAllPlayers(true))
            if (target->getTag("BimieCurse").toMap().value("owner").toString() == owner) {
                target->removeTag("BimieCurse");
                room->setPlayerMark(target, "@zhou", 0);
            }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != PreHpRecover || !player || player->getMark("@zhou") <= 0) return true;
        const QVariantMap receipt = player->getTag("BimieCurse").toMap();
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        if (!owner) return true;
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = ctx.initiator = owner; ctx.invoker = player;
        ctx.instanceID = receipt.value("instance").toInt();
        ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), ctx.instanceID));
        ctx.extra_data = receipt; ctx.targets << player; ctx.original_data = &data; ctx.current_event = event;
        contexts << ctx;
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        // A committed curse remains until its original owner dies, even after the skill is lost.
        return ctx.invoker && ctx.invoker->getMark("@zhou") > 0
            && ctx.invoker->getTag("BimieCurse").toMap() == ctx.extra_data.toMap();
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.manual_effect = true; return skillEffect(event, room, owner, ctx, ctx.invoker); }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { return target == ctx.invoker; }
};

class Gangqu : public TriggerSkillV2
{
public:
    Gangqu() : TriggerSkillV2("gangqu") { events << EventPhaseStart << CardEffected; }
    Frequency getFrequency(const Player *player = nullptr) const override
    { return player && player->getPhase() == Player::Start ? Compulsory : NotFrequent; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const QList<int> pile = player->getPile("gang");
        if (event == EventPhaseStart) {
            if ((player->getPhase() == Player::Finish && pile.isEmpty() && !player->isKongcheng())
                || (player->getPhase() == Player::Start && !pile.isEmpty())) return {{player, {objectName()}}};
        } else {
            const CardEffectStruct effect = data.value<CardEffectStruct>();
            if (effect.to == player && effect.card && !pile.isEmpty()
                && effect.card->getTypeId() == Sanguosha->getCard(pile.first())->getTypeId())
                return {{player, {objectName()}}};
        }
        return {};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.targets << owner;
        if (event == CardEffected) return owner->askForSkillInvoke(objectName() + "Prevent", *ctx.original_data);
        if (owner->getPhase() == Player::Finish) {
            if (!owner->askForSkillInvoke(this, *ctx.original_data)) return false;
            const int id = room->askForCardChosen(owner, owner, "h", objectName());
            if (!owner->handCards().contains(id)) return false;
            ctx.extra_data = id;
        } else {
            const QList<int> pile = owner->getPile("gang");
            if (pile.isEmpty()) return false;
            int id = pile.first();
            if (pile.size() > 1) {
                room->fillAG(pile, owner);
                id = room->askForAG(owner, pile, false, objectName());
                room->clearAG(owner);
            }
            if (!pile.contains(id)) return false;
            ctx.extra_data = id;
        }
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event != CardEffected) return false;
        ctx.manual_effect = true;
        return skillEffect(event, room, owner, ctx, owner);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (event == CardEffected) {
            const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
            const QList<int> pile = owner->getPile("gang");
            return target == effect.to && effect.card && !pile.isEmpty()
                && effect.card->getTypeId() == Sanguosha->getCard(pile.first())->getTypeId();
        }
        const int id = ctx.extra_data.toInt();
        if (owner->getPhase() == Player::Finish) {
            if (target == owner && owner->handCards().contains(id) && owner->getPile("gang").isEmpty())
                owner->addToPile("gang", id);
        } else if (owner->getPile("gang").contains(id)) room->obtainCard(target, id);
        return false;
    }
};

class GangquClear : public DetachEffectSkill
{
public:
    GangquClear() : DetachEffectSkill("gangqu") {}
    void onSkillDetached(Room *, ServerPlayer *player) const override
    {
        // The physical pile is shared; detaching a sibling grant must not discard it.
        if (!player->hasSkill("gangqu", true)) player->clearOnePrivatePile("gang");
    }
};

TiaojiaoCard::TiaojiaoCard()
{
    setSkillName("tiaojiao");
}

bool TiaojiaoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty()) return false;
    return to_select != Self;
}

void TiaojiaoCard::use(Room *room, ServerPlayer *tsukushi, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.first();
    ServerPlayer *slashTarget = room->askForPlayerChosen(tsukushi, room->getAlivePlayers(), "tiaojiao");
    if (!room->askForUseSlashTo(target, slashTarget, "@TiaojiaoSlash:" + tsukushi->getGeneralName() + ":" + target->getGeneralName() + ":" + slashTarget->getGeneralName(), false)){
        if (!target->isNude()){
            room->obtainCard(tsukushi, room->askForCardChosen(tsukushi, target, "hej", objectName()));
        }
    }
}

class Tiaojiao : public ViewAsSkillV2
{
public:
    Tiaojiao() : ViewAsSkillV2("tiaojiao")
    { setPhaseName("Play"); }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->isAlive();
    }
    LimitScope getLimitScope() const override { return Limit_Phase; }

    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *target) const override
    {
        return selected.isEmpty() && target && target->isAlive() && request.initiator;
    }
    bool targetsFeasible(const ActiveSkillRequest &, const QList<const Player *> &targets) const override
    {
        return targets.size() == 1;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "TiaojiaoCard"; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        return canActivate(request) && request.selectedCardIds.isEmpty() ? hayateProxy(this, request) : nullptr;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.initiator || !target) return FinishSkill;
        Room *room = target->getRoom();
        ServerPlayer *slashTarget = room->askForPlayerChosen(ctx.initiator, room->getOtherPlayers(target), objectName());
        if (!slashTarget) return ContinueEffects;
        if (!room->askForUseSlashTo(target, slashTarget, "@TiaojiaoSlash:" + ctx.initiator->getGeneralName()
            + ":" + target->getGeneralName() + ":" + slashTarget->getGeneralName(), false)) {
            for (int i = 0; i < getEffectiveAmount(ctx) && ctx.initiator->canGet(target, "hej"); ++i) {
                const int id = room->askForCardChosen(ctx.initiator, target, "hej", objectName());
                if (id < 0 || room->getCardOwner(id) != target || !ctx.initiator->canGet(target, id)) break;
                room->obtainCard(ctx.initiator, id);
            }
        }
        return ContinueEffects;
    }
};


class Gongming : public TriggerSkillV2
{
public:
    Gongming() : TriggerSkillV2("gongming") { events << HpRecover; global = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (player && player->isAlive())
            for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
                if (owner->getPhase() != Player::NotActive && owner->getLostHp() > 0) result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.choice = owner->askForChoice(objectName(), "youdraw+hedraws");
        ctx.targets << (ctx.choice == "youdraw" ? owner : ctx.invoker);
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        target->drawCards(owner->getLostHp() * getEffectiveAmount(ctx), objectName());
        return false;
    }
};

namespace {
void clearYaojingAura(Room *room, ServerPlayer *owner, bool dead)
{
    const QVariantMap source = room->getTag("YaojingAuraSource").toMap();
    if (source.value("owner").toString() != owner->objectName()
        || (!dead && owner->hasSkillInstance("yaojing", source.value("instance").toInt()))) return;
    room->removeTag("YaojingAuraSource");
    // Only the instance that established this component may retire it.
    if (room->getAura() == "MacrossF") {
        const QVariantMap other = room->getTag("XingjianAuraSource").toMap();
        ServerPlayer *ranka = room->findPlayerByObjectName(other.value("owner").toString());
        if (ranka && ranka->isAlive()
            && ranka->hasSkillInstance("xingjian", other.value("instance").toInt())) {
            room->doAura(ranka, "xingjian");
            return;
        }
    }
    if (room->getAura() == "yaojing" || room->getAura() == "MacrossF") room->clearAura();
}

void projectYaojingPenalty(Room *room, ServerPlayer *owner)
{
    const QVariantMap history = room->queryCardHistory(owner, "turn", "GodSalvation");
    // An incomplete journal cannot establish a new total; retain the last complete projection.
    if (!history.value("complete").toBool()) return;
    int count = 0;
    for (const QVariant &item : history.value("items").toList())
        if (item.toMap().value("skill_name").toString() == "yaojing") ++count;
    room->setPlayerProperty(owner, "YaojingHandPenalty", count);
}
}

class YaojingVS : public ViewAsSkillV2
{
public:
    YaojingVS() : ViewAsSkillV2("yaojing", 1) {}
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.activationRef.isValid()
            && request.reason == CardUseStruct::CARD_USE_REASON_PLAY
            && request.initiator->getSkillInstanceStateValue(objectName(),
                request.activationRef.key.instanceID, "active").toBool();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && card && card->getEffectiveId() >= 0
            && !card->hasFlag("using") && request.initiator
            && request.initiator->getCards("h").contains(card);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest prefix = request;
        prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    {
        if (!cardSelectionFeasible(request)) return nullptr;
        const Card *material = Sanguosha->getCard(request.selectedCardIds.first());
        auto *card = new GodSalvation(material->getSuit(), material->getNumber());
        card->addSubcard(material);
        card->setSkillName(objectName());
        return card;
    }
    QString historyKey(const ActiveSkillRequest &) const override { return "GodSalvation"; }
};

class Yaojing : public TriggerSkillV2
{
public:
    Yaojing() : TriggerSkillV2("yaojing")
    {
        events << EventPhaseChanging << CardFinished << Death << EventLoseSkill;
        global = true;
        view_as_skill = new YaojingVS;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == Death) {
            if (ServerPlayer *dead = data.value<DeathStruct>().who) clearYaojingAura(room, dead, true);
        } else if (event == EventLoseSkill && player) {
            clearYaojingAura(room, player, false);
        } else if (event == CardFinished) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.from && use.card && use.card->isKindOf("GodSalvation") && use.card->getSkillName() == objectName())
                projectYaojingPenalty(room, use.from);
        } else if (player && event == EventPhaseChanging) {
            const PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::Discard) projectYaojingPenalty(room, player);
            if (change.to == Player::NotActive) {
                room->setPlayerProperty(player, "YaojingHandPenalty", 0);
                for (int id : player->getSkillInstanceIds(objectName()))
                    player->setSkillInstanceStateValue(objectName(), id, "active", false);
            }
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event != EventPhaseChanging || !player || !player->isAlive() || !player->hasSkill(objectName())
            || data.value<PhaseChangeStruct>().to != Player::Play) return {};
        ServerPlayer *previous = room->getAuraPlayer();
        return previous && previous->getHandcardNum() > player->getHandcardNum()
            ? TriggerList() : TriggerList{{player, {objectName()}}};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target != owner || !owner->hasSkillInstance(objectName(), ctx.instanceID)) return false;
        ServerPlayer *previous = room->getAuraPlayer();
        if (previous && previous->getHandcardNum() > owner->getHandcardNum()) return false;
        owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "active", true);
        if (room->getAura() == objectName() || room->getAura() == "MacrossF") return false;
        room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) * 2 + 1);
        const QVariantMap companion = room->getTag("XingjianAuraSource").toMap();
        ServerPlayer *ranka = room->findPlayerByObjectName(companion.value("owner").toString());
        const bool merges = room->getAura() == "xingjian" && ranka && ranka->isAlive()
            && ranka->hasSkillInstance("xingjian", companion.value("instance").toInt());
        const QString aura = merges ? "MacrossF" : objectName();
        if (room->doAura(owner, aura))
            room->setTag("YaojingAuraSource", QVariantMap{{"owner", ctx.activationRef.ownerObjectName},
                {"skill", ctx.activationRef.key.skillName}, {"instance", ctx.activationRef.key.instanceID},
                {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName},
                {"source_instance", ctx.sourceRef.key.instanceID}});
        return false;
    }
};

class YaojingClear : public DetachEffectSkill
{
public:
    YaojingClear() : DetachEffectSkill("yaojing") {}
    void onSkillDetached(Room *room, ServerPlayer *player) const override
    { clearYaojingAura(room, player, false); }
};

class YaojingMaxCards : public MaxCardsSkillV2
{
public:
    YaojingMaxCards() : MaxCardsSkillV2("#yaojing")
    { m_baseAmount = 0; setHolderSelector(CorrectSkill_System); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        // This is a consequence of actual card uses and remains after the granting skill is lost.
        const int times = ctx.getPrimary() ? ctx.getPrimary()->property("YaojingHandPenalty").toInt() : 0;
        return times > 0 ? CorrectSkillResult::useAmount(-times) : CorrectSkillResult::noEffect();
    }
};

namespace {
int hayateLostSuitCount(ServerPlayer *owner, const CardsMoveOneTimeStruct &move, Card::Suit suit)
{
    if (!owner || !owner->isAlive() || move.from != owner || owner->getPhase() != Player::NotActive) return 0;
    int count = 0;
    for (int i = 0; i < move.card_ids.size(); ++i) {
        // Match each card to its own origin zone, not another card in a mixed move.
        const Player::Place place = move.from_places.value(i, Player::PlaceUnknown);
        if ((place == Player::PlaceHand || place == Player::PlaceEquip)
            && Sanguosha->getCard(move.card_ids.at(i))->getSuit() == suit) ++count;
    }
    return count;
}
}

class Kurimu : public TriggerSkillV2
{
public:
    Kurimu() : TriggerSkillV2("kurimu") { events << CardsMoveOneTime; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const int count = hayateLostSuitCount(player, data.value<CardsMoveOneTimeStruct>(), Card::Diamond);
        return count > 0 && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName() + '*' + QString::number(count)}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        const QList<ServerPlayer *> candidates = room->getOtherPlayers(owner);
        if (candidates.isEmpty()) return false;
        ctx.targets << owner << room->askForPlayerChosen(owner, candidates, objectName());
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->doLightbox(objectName() + "$", 800);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};

class Minatsu : public TriggerSkillV2
{
public:
    Minatsu() : TriggerSkillV2("minatsu") { events << CardsMoveOneTime; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const int count = hayateLostSuitCount(player, data.value<CardsMoveOneTimeStruct>(), Card::Club);
        return count > 0 && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName() + '*' + QString::number(count)}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        FireSlash slash(Card::NoSuit, 0);
        slash.setSkillName(objectName());
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getOtherPlayers(owner))
            if (owner->canSlash(target, &slash, false)) candidates << target;
        if (candidates.isEmpty() || !owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << room->askForPlayerChosen(owner, candidates, objectName());
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->doLightbox(objectName() + "$", 800);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int i = 0; i < getEffectiveAmount(ctx) && owner->isAlive() && target->isAlive(); ++i) {
            auto *slash = new FireSlash(Card::NoSuit, 0);
            slash->setSkillName(objectName());
            CardUseStruct use(slash, owner, target, false);
            use.setOwnedCard(slash);
            if (owner->canSlash(target, slash, false)) room->useCardFromSkillEffect(use, ctx, false);
        }
        return false;
    }
};

class Chizuru : public TriggerSkillV2
{
public:
    Chizuru() : TriggerSkillV2("chizuru") { events << CardsMoveOneTime; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const int count = hayateLostSuitCount(player, data.value<CardsMoveOneTimeStruct>(), Card::Heart);
        return count > 0 && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName() + '*' + QString::number(count)}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getAlivePlayers())
            if (target->isWounded()) candidates << target;
        if (candidates.isEmpty()) return false;
        ctx.targets << room->askForPlayerChosen(owner, candidates, objectName());
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->doLightbox(objectName() + "$", 800);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->recover(target, RecoverStruct(owner, nullptr, getEffectiveAmount(ctx)), true);
        return false;
    }
};

class Mafuyu : public TriggerSkillV2
{
public:
    Mafuyu() : TriggerSkillV2("mafuyu") { events << CardsMoveOneTime; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const int count = hayateLostSuitCount(player, data.value<CardsMoveOneTimeStruct>(), Card::Spade);
        return count > 0 && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName() + '*' + QString::number(count)}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << room->askForPlayerChosen(owner, room->getAlivePlayers(), objectName());
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->doLightbox(objectName() + "$", 800);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (getEffectiveAmount(ctx) <= 0) return false;
        QVariantList receipts = target->getTag("MafuyuReceipts").toList();
        receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName},
            {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
            {"count", getEffectiveAmount(ctx)}};
        target->setTag("MafuyuReceipts", receipts);
        room->addPlayerMark(target, "@mafuyu", getEffectiveAmount(ctx));
        return false;
    }
};

class MafuyuEffect : public TriggerSkillV2
{
public:
    MafuyuEffect() : TriggerSkillV2("#mafuyu-effect")
    { events << EventPhaseStart << EventLoseSkill << Death; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
    {
        if (event != Death && event != EventLoseSkill) return false;
        for (ServerPlayer *target : room->getAllPlayers(true)) {
            QVariantList live;
            int count = 0;
            for (const QVariant &item : target->getTag("MafuyuReceipts").toList()) {
                const QVariantMap receipt = item.toMap();
                ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
                if (!owner || !owner->isAlive() || !owner->hasSkillInstance(receipt.value("skill").toString(),
                        receipt.value("instance").toInt())) continue;
                live << receipt;
                count += receipt.value("count").toInt();
            }
            target->setTag("MafuyuReceipts", live);
            room->setPlayerMark(target, "@mafuyu", count);
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseStart || !player || !player->isAlive() || player->getPhase() != Player::RoundStart
            || player->isSkipped(Player::Play)) return true;
        const QVariantList receipts = player->getTag("MafuyuReceipts").toList();
        if (receipts.isEmpty()) return true;
        const QVariantMap receipt = receipts.first().toMap();
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        if (!owner) return true;
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = ctx.initiator = owner; ctx.invoker = player;
        ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(),
            receipt.value("instance").toInt()));
        // One aggregate opportunity per recipient/turn, even when several instances left receipts.
        ctx.instanceID = 0; ctx.extra_data = receipt; ctx.targets << player;
        ctx.original_data = &data; ctx.current_event = event;
        contexts << ctx;
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    {
        return ctx.invoker && ctx.invoker->isAlive() && !ctx.invoker->isSkipped(Player::Play)
            && ctx.invoker->getTag("MafuyuReceipts").toList().contains(ctx.extra_data);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target != ctx.invoker || target->isSkipped(Player::Play)) return false;
        QVariantList receipts = target->getTag("MafuyuReceipts").toList();
        const int index = receipts.indexOf(ctx.extra_data);
        if (index < 0) return false;
        QVariantMap receipt = receipts.at(index).toMap();
        const int remaining = receipt.value("count").toInt() - 1;
        if (remaining > 0) { receipt["count"] = remaining; receipts[index] = receipt; }
        else receipts.removeAt(index);
        target->setTag("MafuyuReceipts", receipts);
        room->removePlayerMark(target, "@mafuyu");
        room->broadcastSkillInvoke("mafuyu");
        target->skip(Player::Play);
        return false;
    }
};

HaremuCard::HaremuCard()
{
    setSkillName("haremu");
    will_throw = false;
}

bool HaremuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return to_select->hasFlag("haremu_target") && targets.length() == 0;
}

void HaremuCard::use(Room *room, ServerPlayer *player, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.at(0);
    if (!target)
        return;
    QStringList used = player->getTag("heremu_targets").toStringList();
    used.append(target->objectName());
    player->setTag("heremu_targets", used);
    room->obtainCard(target, this, true);

    if (!target->hasClub() && room->askForChoice(target, "haremu", "haremu_accept+cancel", QVariant::fromValue(player)) == "haremu_accept"){
        target->addClub("bekiyou");
    }
    else{
        LogMessage log;
        log.type = "$refuse_club";
        log.from = target;
        log.arg = "haremu";
        room->sendLog(log);
    }
}

class HaremuVS : public ViewAsSkillV2
{
public:
    HaremuVS() : ViewAsSkillV2("haremu", 1) { setResponseOrUse(true); }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.owner || !ref.isValid()) return false;
        const QString target = ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "pending").toString();
        return !target.isEmpty() && !ctx.owner->getSkillInstanceStateValue(ref.key.skillName,
            ref.key.instanceID, "invited").toStringList().contains(target);
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (!ctx.owner || !ref.isValid()) return;
        QStringList used = ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "invited").toStringList();
        used << ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "pending").toString();
        used.removeDuplicates();
        ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "invited", used);
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (ctx.owner && ref.isValid()) ctx.owner->removeSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "invited");
    }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || !request.activationRef.isValid()
            || request.reason != CardUseStruct::CARD_USE_REASON_RESPONSE_USE || request.pattern != "@@haremu") return false;
        const int id = request.activationRef.key.instanceID;
        const QString pending = request.initiator->getSkillInstanceStateValue(objectName(), id, "pending").toString();
        return !pending.isEmpty()
            && !request.initiator->getSkillInstanceStateValue(objectName(), id, "invited").toStringList().contains(pending);
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.selectedCardIds.isEmpty() && card && card->getEffectiveId() >= 0 && !card->hasFlag("using")
            && request.initiator && request.initiator->getCards("h").contains(card);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest prefix = request; prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        return selected.isEmpty() && target && target->isAlive() && request.initiator && request.activationRef.isValid()
            && target->objectName() == request.initiator->getSkillInstanceStateValue(objectName(),
                request.activationRef.key.instanceID, "pending").toString();
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return targets.size() == 1 && canSelectTarget(request, {}, targets.first()); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? hayateProxy(this, request) : nullptr; }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!checkCustomUsage(ctx) || !ctx.initiator || request.selectedCardIds.size() != 1) return false;
        const int id = request.selectedCardIds.first();
        if (room->getCardOwner(id) != ctx.initiator || room->getCardPlace(id) != Player::PlaceHand) return false;
        addUsage(ctx);
        return true;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.initiator || !target || !ctx.use_card || ctx.use_card->subcardsLength() != 1) return FinishSkill;
        Room *room = target->getRoom();
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) != ctx.initiator || room->getCardPlace(id) != Player::PlaceHand) return ContinueEffects;
        room->obtainCard(target, ctx.use_card, true);
        if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceHand || !target->isAlive()) return ContinueEffects;
        if (!target->hasClub() && room->askForChoice(target, "haremu", "haremu_accept+cancel",
            QVariant::fromValue(ctx.initiator)) == "haremu_accept") target->addClub("bekiyou");
        else {
            LogMessage log; log.type = "$refuse_club"; log.from = target; log.arg = "haremu"; room->sendLog(log);
        }
        return ContinueEffects;
    }
};

class Haremu : public TriggerSkillV2
{
public:
    Haremu() : TriggerSkillV2("haremu")
    { frequency = Club; club_name = "bekiyou"; events << EventPhaseStart; global = true; view_as_skill = new HaremuVS; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (!player || !player->isAlive() || !player->isFemale() || player->getPhase() != Player::Play) return {};
        TriggerList result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != player && !owner->isKongcheng()) result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!ctx.invoker || owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "invited")
                .toStringList().contains(ctx.invoker->objectName())) return false;
        const QVariant previous = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "pending");
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int previousSelector = owner->getMark(selector);
        const bool wasTarget = ctx.invoker->hasFlag("haremu_target");
        auto restore = qScopeGuard([&] {
            if (owner->hasSkillInstance(objectName(), ctx.instanceID))
                owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "pending", previous);
            room->setPlayerMark(owner, selector, previousSelector);
            if (!wasTarget) room->setPlayerFlag(ctx.invoker, "-haremu_target");
        });
        owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "pending", ctx.invoker->objectName());
        room->setPlayerMark(owner, selector, ctx.activationRef.key.instanceID);
        // Keep the legacy AI hint scoped to this request; server legality uses the exact source state.
        room->setPlayerFlag(ctx.invoker, "haremu_target");
        room->askForUseCard(owner, "@@haremu", "@haremu-use", -1, Card::MethodNone);
        return false;
    }
};

class HaremuMaxCards : public MaxCardsSkillV2
{
public:
    HaremuMaxCards() : MaxCardsSkillV2("#haremu") { m_baseAmount = 1; setHolderSelector(CorrectSkill_AllHolders); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        const Player *target = ctx.getPrimary();
        if (!target || (target != ctx.holder && !target->hasClub("bekiyou"))) return CorrectSkillResult::noEffect();
        int count = target->isAlive() && target->hasClub("bekiyou") ? 1 : 0;
        for (const Player *player : target->getSiblings())
            if (player->isAlive() && player->hasClub("bekiyou")) ++count;
        return CorrectSkillResult::useAmount(count * ctx.getCurrentAmount());
    }
};

// redo  gaokang
class Gaokang : public TriggerSkillV2
{
public:
    Gaokang() : TriggerSkillV2("gaokang") { events << DamageInflicted; global = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        TriggerList result;
        if (!damage.to || damage.damage < 1 || damage.nature != DamageStruct::Normal) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner->isAlive() && !owner->isKongcheng() && owner->distanceTo(damage.to) <= 1
                && owner->canDiscard(owner, "he")) result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        const int id = room->askForCardChosen(owner, owner, "he", objectName(), false, Card::MethodDiscard);
        if (id < 0) return false;
        ctx.extra_data = QVariantMap{{"card", id}};
        ctx.targets << ctx.original_data->value<DamageStruct>().to;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        QVariantMap paid = ctx.extra_data.toMap();
        const int id = paid.value("card").toInt();
        const DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (!damage.to || damage.damage <= 0 || damage.nature != DamageStruct::Normal
            || owner->isKongcheng() || owner->distanceTo(damage.to) > 1
            || room->getCardOwner(id) != owner || !owner->canDiscard(owner, id)) return false;
        const bool lastHand = room->getCardPlace(id) == Player::PlaceHand && owner->getHandcardNum() == 1;
        room->throwCard(id, owner, owner);
        // Only discarding the final hand card pays the extra draw, not an equipment discard.
        paid["empty_hand"] = lastHand && owner->isKongcheng();
        ctx.extra_data = paid;
        return room->getCardOwner(id) != owner || room->getCardPlace(id) == Player::DiscardPile;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        return skillEffect(event, room, owner, ctx, ctx.original_data->value<DamageStruct>().to);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (damage.to != target || damage.nature != DamageStruct::Normal || damage.damage <= 0) return false;
        damage.damage = qMax(0, damage.damage - getEffectiveAmount(ctx));
        *ctx.original_data = QVariant::fromValue(damage);
        room->broadcastSkillInvoke(objectName());
        if (ctx.extra_data.toMap().value("empty_hand").toBool()) target->drawCards(2, objectName());
        return damage.damage == 0;
    }
};

// eugeo
namespace {
void projectFrozen(Room *room, ServerPlayer *target, const QVariantList &receipts)
{
    int count = 0;
    for (const QVariant &value : receipts) count += value.toMap().value("count").toInt();
    target->setTag("FrozenReceipts", receipts);
    room->setPlayerMark(target, "@Frozen_Eu", count);
}
void removeFrozen(Room *room, ServerPlayer *target, int count)
{
    QVariantList receipts = target->getTag("FrozenReceipts").toList();
    while (count > 0 && !receipts.isEmpty()) {
        QVariantMap receipt = receipts.first().toMap();
        const int removed = qMin(count, receipt.value("count").toInt());
        count -= removed;
        const int remaining = receipt.value("count").toInt() - removed;
        if (remaining <= 0) receipts.removeFirst();
        else { receipt["count"] = remaining; receipts[0] = receipt; }
    }
    projectFrozen(room, target, receipts);
}
}

class Rennai : public TriggerSkillV2
{
public:
    Rennai() : TriggerSkillV2("rennai")
    { frequency = Compulsory; events << DamageInflicted << PreHpLost << EventPhaseStart << Death << EventLoseSkill; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    {
        if (event == EventPhaseStart && player && player->getPhase() == Player::Start) {
            for (int id : player->getSkillInstanceIds(objectName()))
                player->setSkillInstanceStateValue(objectName(), id, "protected", false);
            room->setPlayerMark(player, "@Patience", 0);
        }
        if (event == Death || event == EventLoseSkill) {
            for (ServerPlayer *target : room->getAllPlayers(true)) {
                QVariantList live;
                for (const QVariant &item : target->getTag("FrozenReceipts").toList()) {
                    const QVariantMap receipt = item.toMap();
                    ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
                    if (owner && owner->isAlive() && owner->hasSkillInstance(receipt.value("skill").toString(),
                            receipt.value("instance").toInt())) live << receipt;
                }
                projectFrozen(room, target, live);
                bool protectedNow = false;
                for (int id : target->getSkillInstanceIds(objectName()))
                    protectedNow |= target->getSkillInstanceStateValue(objectName(), id, "protected").toBool();
                room->setPlayerMark(target, "@Patience", protectedNow ? 1 : 0);
            }
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == DamageInflicted && data.value<DamageStruct>().damage > 0) return {{player, {objectName()}}};
        if (event == PreHpLost && data.value<HpLostStruct>().lose > 0) {
            QStringList sources;
            for (int id : player->getValidSkillInstanceIds(objectName()))
                if (player->getSkillInstanceStateValue(objectName(), id, "protected").toBool())
                    sources << SkillInstanceUtils::formatName(objectName(), id);
            if (!sources.isEmpty()) return {{player, sources}};
        }
        return {};
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.targets << owner; return true; }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    { ctx.manual_effect = true; return skillEffect(event, room, owner, ctx, owner); }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "rennai_gain") {
            const int count = getEffectiveAmount(ctx);
            if (count <= 0) return false;
            QVariantList receipts = target->getTag("FrozenReceipts").toList();
            receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
                {"instance", ctx.sourceRef.key.instanceID}, {"count", count}};
            projectFrozen(room, target, receipts);
            return false;
        }
        if (ctx.choice == "rennai_lose") { removeFrozen(room, target, getEffectiveAmount(ctx)); return false; }
        if (target != owner) return false;
        const bool protectedNow = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "protected").toBool();
        if (event == PreHpLost && !protectedNow) return false;
        if (event == DamageInflicted && !protectedNow) {
            room->loseHp(HpLostStruct(owner, 1, objectName(), owner));
            if (owner->isAlive() && owner->hasSkillInstance(objectName(), ctx.instanceID)) {
                owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "protected", true);
                room->setPlayerMark(owner, "@Patience", 1);
            }
        }
        if (!owner->isAlive()) return true;
        room->doLightbox(objectName() + "$", 800);
        const bool hp = room->askForChoice(owner, objectName(), "rennai_hp+rennai_handcardnum") == "rennai_hp";
        QStringList choices;
        for (ServerPlayer *candidate : room->getAlivePlayers()) {
            const QString value = QString::number(hp ? candidate->getHp() : candidate->getHandcardNum());
            if (!choices.contains(value)) choices << value;
        }
        const QString kind = hp ? "hp" : "handcardnum";
        const int value = room->askForChoice(owner, objectName(), choices.join('+'), kind).toInt();
        SkillContext freeze = ctx;
        freeze.targets.clear();
        freeze.choice = room->askForChoice(owner, objectName(), "rennai_gain+rennai_lose", kind + '+' + QString::number(value));
        for (ServerPlayer *candidate : room->getAlivePlayers())
            if ((hp ? candidate->getHp() : candidate->getHandcardNum()) == value) freeze.targets << candidate;
        // Every chosen recipient participates in the V2 target interception path.
        for (ServerPlayer *candidate : freeze.targets) skillEffect(event, room, owner, freeze, candidate);
        return true;
    }
};

class Zhanfang : public TriggerSkillV2
{
public:
    Zhanfang() : TriggerSkillV2("zhanfang") { events << PreCardUsed << CardFinished; global = true; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return event == PreCardUsed && player && use.from == player && player->isAlive() && player->hasSkill(objectName())
            && use.card && !use.card->isKindOf("Collateral") && !use.card->isKindOf("EquipCard")
            && !use.card->isKindOf("DelayedTrick") && use.to.size() == 1 && use.to.first()->getMark("@Frozen_Eu") > 0
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (event != CardFinished) return false;
        const CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card || !use.from) return true;
        const qint64 useId = room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id").toLongLong();
        for (const QVariant &value : use.card->getTag("ZhanfangReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            if (receipt.value("use").toLongLong() != useId) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = ctx.initiator = owner; ctx.invoker = use.from;
            ctx.instanceID = receipt.value("instance").toInt(); ctx.is_forced = true;
            ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(objectName(), ctx.instanceID));
            ctx.extra_data = receipt; ctx.targets = use.to; ctx.original_data = &data; ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override
    {
        if (ctx.current_event == CardFinished)
            for (ServerPlayer *target : ctx.targets) if (target->isAlive()) return target;
        return ctx.owner;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        if (ctx.current_event != CardFinished) return TriggerSkillV2::isSourceAvailable(room, ctx);
        const Card *card = ctx.original_data->value<CardUseStruct>().card;
        return card && card->getTag("ZhanfangReceipts").toList().contains(ctx.extra_data);
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == CardFinished) return true;
        if (!owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        const Card *card = ctx.original_data->value<CardUseStruct>().card;
        for (ServerPlayer *target : room->getAlivePlayers())
            if (target->getMark("@Frozen_Eu") > 0 && !room->isProhibited(owner, target, card)
                && (!card->isKindOf("Slash") || owner->canSlash(target, card, false))) ctx.targets << target;
        return !ctx.targets.isEmpty();
    }
    bool effect(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == CardFinished) {
            // Consume the retained continuation before prompting; nested card uses cannot replay it.
            const Card *card = ctx.original_data->value<CardUseStruct>().card;
            QVariantList receipts = card->getTag("ZhanfangReceipts").toList();
            receipts.removeOne(ctx.extra_data);
            card->setTag("ZhanfangReceipts", receipts);
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (event == CardFinished) {
            if (target->getMark("@Frozen_Eu") <= 0 || !target->canDiscard(target, "e")
                || room->askForChoice(target, objectName(), "zhanfang_discard+cancel", *ctx.original_data) != "zhanfang_discard") return false;
            const int id = room->askForCardChosen(target, target, "e", objectName(), false, Card::MethodDiscard);
            if (id < 0 || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceEquip
                || !target->canDiscard(target, id)) return false;
            room->throwCard(id, target, target);
            if (room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceEquip) removeFrozen(room, target, 1);
            return false;
        }
        if (!use.card || room->isProhibited(owner, target, use.card)) return false;
        if (!use.to.contains(target)) use.to << target;
        room->sortByActionOrder(use.to);
        *ctx.original_data = QVariant::fromValue(use);
        QVariantList receipts = use.card->getTag("ZhanfangReceipts").toList();
        const QVariantMap receipt{{"use", room->historyParent(room->currentHistoryEventId(), "use_card", true).value("id")},
            {"owner", ctx.sourceRef.ownerObjectName}, {"instance", ctx.sourceRef.key.instanceID}, {"execution", ctx.executionID}};
        if (!receipts.contains(receipt)) {
            receipts << receipt;
            use.card->setTag("ZhanfangReceipts", receipts);
            room->doLightbox(objectName() + "$", 800);
        }
        return false;
    }
};

class ZhanfangNoRespond : public TriggerSkillV2
{
public:
    ZhanfangNoRespond() : TriggerSkillV2("#zhanfang-no-respond") { events << TargetSpecified; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player == use.from && player->hasSkill("zhanfang") && use.card
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        for (ServerPlayer *target : room->getAlivePlayers()) if (target->getMark("@Frozen_Eu") > 0) ctx.targets << target;
        return !ctx.targets.isEmpty();
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.no_respond_list.contains(target->objectName())) use.no_respond_list << target->objectName();
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class Huajian : public TriggerSkillV2
{
public:
    Huajian() : TriggerSkillV2("huajian") { frequency = Limited; events << Death; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *dead = data.value<DeathStruct>().who;
        return player && dead == player && player->hasSkill(objectName()) ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        int id = -1;
        for (int i = 0; i < Sanguosha->getCardCount(); ++i)
            if (Sanguosha->getCard(i)->objectName() == "GreenRose") { id = i; break; }
        if (id < 0 || !owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getAlivePlayers()) if (target->hasEquipArea(0)) candidates << target;
        if (candidates.isEmpty()) return false;
        ctx.extra_data = id;
        ctx.targets << room->askForPlayerChosen(owner, candidates, objectName());
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
        if (!target->hasEquipArea(0)) return false;
        const int id = ctx.extra_data.toInt();
        const Card *weapon = Sanguosha->getCard(id);
        if (!weapon || weapon->objectName() != "GreenRose") return false;
        room->doLightbox(objectName() + "$", 3000);
        if (target->getWeapon()) room->obtainCard(target, target->getWeapon());
        if (!target->isAlive() || !target->hasEquipArea(0)) return false;
        // The continuing return belongs to the physical weapon and survives its donor's death.
        const QVariantMap receipt{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"target", target->objectName()}, {"card", id}, {"execution", ctx.executionID}};
        weapon->setTag("HuajianRecipient", receipt);
        CardsMoveStruct move;
        move.card_ids << id; move.to = target; move.to_place = Player::PlaceEquip;
        move.reason = CardMoveReason(CardMoveReason::S_REASON_RECYCLE, target->objectName(), objectName(), "");
        room->moveCardsAtomic(move, true);
        return false;
    }
};

class HuajianReturn : public TriggerSkillV2
{
public:
    HuajianReturn() : TriggerSkillV2("#huajian-return")
    { events << CardsMoveOneTime << CardUsed << CardResponded; global = true; frequency = Compulsory; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        QList<int> candidates;
        ServerPlayer *actor = player;
        if (event == CardsMoveOneTime) {
            const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (!move.from) return true;
            actor = room->findPlayerByObjectName(move.from->objectName(), true);
            for (int i = 0; i < move.card_ids.size(); ++i)
                if (move.from_places.value(i) == Player::PlaceEquip
                    || (move.from_places.value(i) == Player::PlaceHand && move.to_place == Player::DiscardPile))
                    candidates << move.card_ids.at(i);
        } else {
            const Card *card = nullptr;
            if (event == CardUsed) {
                const CardUseStruct use = data.value<CardUseStruct>(); actor = use.from; card = use.card;
            } else card = data.value<CardResponseStruct>().m_card;
            if (card && (event == CardResponded || card->isVirtualCard()))
                candidates = card->isVirtualCard() ? card->getSubcards() : QList<int>{card->getEffectiveId()};
        }
        if (!actor || !actor->isAlive()) return true;
        for (int id : candidates) {
            if (id < 0) continue;
            const Card *weapon = Sanguosha->getCard(id);
            const QVariantMap receipt = weapon->getTag("HuajianRecipient").toMap();
            if (receipt.value("target").toString() != actor->objectName()
                || (room->getCardOwner(id) == actor && room->getCardPlace(id) == Player::PlaceHand)) continue;
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = ctx.initiator = owner; ctx.invoker = actor;
            ctx.instanceID = receipt.value("instance").toInt();
            ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), ctx.instanceID));
            ctx.extra_data = receipt; ctx.targets << actor; ctx.original_data = &data; ctx.current_event = event;
            contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        const QVariantMap receipt = ctx.extra_data.toMap();
        const int id = receipt.value("card", -1).toInt();
        return id >= 0 && ctx.invoker && ctx.invoker->isAlive()
            && Sanguosha->getCard(id)->getTag("HuajianRecipient").toMap() == receipt
            && !(room->getCardOwner(id) == ctx.invoker && room->getCardPlace(id) == Player::PlaceHand);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target == ctx.invoker && isSourceAvailable(room, ctx)) room->obtainCard(target, ctx.extra_data.toMap().value("card").toInt());
        return false;
    }
};

//k1
namespace {
int oniCount(const ServerPlayer *target, const SkillInstanceRef &source)
{
    int count = 0;
    for (const QVariant &value : target->getTag("OniReceipts").toList()) {
        const QVariantMap receipt = value.toMap();
        if (receipt.value("owner").toString() == source.ownerObjectName
            && receipt.value("skill").toString() == source.key.skillName
            && receipt.value("instance").toInt() == source.key.instanceID) count += receipt.value("count").toInt();
    }
    return count;
}
void projectOni(Room *room, ServerPlayer *target, const QVariantList &receipts)
{
    int count = 0;
    for (const QVariant &value : receipts) count += value.toMap().value("count").toInt();
    target->setTag("OniReceipts", receipts);
    room->setPlayerMark(target, "@Oni", count);
}
void clearOniSource(Room *room, ServerPlayer *target, const SkillInstanceRef &source)
{
    QVariantList remaining;
    for (const QVariant &value : target->getTag("OniReceipts").toList()) {
        const QVariantMap receipt = value.toMap();
        if (receipt.value("owner").toString() != source.ownerObjectName
            || receipt.value("skill").toString() != source.key.skillName
            || receipt.value("instance").toInt() != source.key.instanceID) remaining << value;
    }
    projectOni(room, target, remaining);
}
}

class Guiyin : public TriggerSkillV2
{
public:
    Guiyin() : TriggerSkillV2("guiyin")
    { frequency = Compulsory; events << Damaged << EventPhaseStart << EventPhaseEnd << TargetConfirmed << Death << EventLoseSkill; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &) const override
    {
        if (event != Death && event != EventLoseSkill) return false;
        for (ServerPlayer *target : room->getAllPlayers(true)) {
            QVariantList remaining;
            for (const QVariant &value : target->getTag("OniReceipts").toList()) {
                const QVariantMap receipt = value.toMap();
                ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
                if (owner && owner->isAlive() && owner->hasSkillInstance(receipt.value("skill").toString(),
                        receipt.value("instance").toInt())) remaining << value;
            }
            projectOni(room, target, remaining);
        }
        return false;
    }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == Damaged) {
            const DamageStruct damage = data.value<DamageStruct>();
            if (damage.to == player && damage.from && damage.from != player && damage.from->isAlive()) return {{player, {objectName()}}};
        } else if (event == TargetConfirmed) {
            const CardUseStruct use = data.value<CardUseStruct>();
            if (use.to.contains(player) && use.from && use.from->isAlive() && use.card && !use.card->isKindOf("SkillCard"))
                return {{player, {objectName()}}};
        } else if ((event == EventPhaseStart && player->getPhase() == Player::Finish)
            || (event == EventPhaseEnd && player->getPhase() == Player::Play)) {
            const int threshold = event == EventPhaseStart ? 3 : 5;
            QStringList sources;
            for (int id : player->getValidSkillInstanceIds(objectName()))
                if (player->getSkillInstanceCorrectStateValue(objectName(), id, "level").toInt() >= threshold)
                    sources << SkillInstanceUtils::formatName(objectName(), id);
            if (!sources.isEmpty()) return {{player, sources}};
        }
        return {};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == Damaged) ctx.targets << ctx.original_data->value<DamageStruct>().from;
        else if (event == TargetConfirmed) ctx.targets << ctx.original_data->value<CardUseStruct>().from;
        else if (event == EventPhaseStart) {
            for (ServerPlayer *target : room->getOtherPlayers(owner))
                if (target->inMyAttackRange(owner)) ctx.targets << target;
        } else {
            ctx.choice = "burst";
            for (ServerPlayer *target : room->getAlivePlayers())
                if (oniCount(target, ctx.sourceRef) > 0) ctx.targets << target;
        }
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event != EventPhaseEnd) return false;
        ctx.manual_effect = true;
        room->broadcastSkillInvoke(objectName(), 4);
        room->doLightbox(objectName() + "$", 2000);
        for (ServerPlayer *target : ctx.targets) skillEffect(event, room, owner, ctx, target);
        if (!owner->isAlive() || !owner->hasSkillInstance(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID)) return false;
        SkillContext offer = ctx; offer.choice = "offer";
        for (ServerPlayer *donor : room->getOtherPlayers(owner)) {
            skillEffect(event, room, owner, offer, donor);
            if (!owner->isAlive() || !owner->hasSkillInstance(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID)) break;
        }
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "burst") {
            const int count = oniCount(target, ctx.sourceRef);
            if (count <= 0) return false;
            // Consume only this source's tokens before damage can produce new nested marks.
            clearOniSource(room, target, ctx.sourceRef);
            room->damage(DamageStruct(objectName(), owner, target, qMin(count, target == owner ? 1 : 2) * getEffectiveAmount(ctx)));
            return false;
        }
        if (ctx.choice == "offer") {
            if (!owner->isAlive()) return false;
            QList<int> hearts;
            for (const Card *card : target->getCards("he")) if (card->getSuit() == Card::Heart) hearts << card->getEffectiveId();
            if (hearts.isEmpty() || room->askForChoice(target, objectName(), "guiyin_give+cancel", *ctx.original_data) != "guiyin_give") return false;
            room->fillAG(hearts, target);
            const int id = room->askForAG(target, hearts, true, objectName());
            room->clearAG(target);
            if (!hearts.contains(id) || room->getCardOwner(id) != target
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) return false;
            room->obtainCard(owner, id);
            if (room->getCardOwner(id) != owner || room->getCardPlace(id) != Player::PlaceHand || !owner->isAlive()) return false;
            room->broadcastSkillInvoke(objectName(), 5);
            room->doLightbox(objectName() + "_detach$", 3000);
            room->detachSkillFromPlayer(owner, SkillInstanceUtils::formatName(ctx.sourceRef.key.skillName,
                ctx.sourceRef.key.instanceID), false, false);
            room->acquireSkill(owner, "qiubang"); room->acquireSkill(owner, "youshui");
            return false;
        }
        const int count = getEffectiveAmount(ctx);
        if (count <= 0 || !owner->hasSkillInstance(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID)) return false;
        QVariantList receipts = target->getTag("OniReceipts").toList();
        receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"count", count}};
        projectOni(room, target, receipts);
        const int level = owner->getSkillInstanceCorrectStateValue(ctx.sourceRef.key.skillName,
            ctx.sourceRef.key.instanceID, "level").toInt() + count;
        owner->setSkillInstanceCorrectStateValue(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID, "level", level);
        room->setPlayerMark(owner, "OniLv", level); // Legacy display hint; rules use the exact public instance state.
        room->broadcastSkillInvoke(objectName(), event == Damaged ? 2 : event == TargetConfirmed ? 1 : 3);
        return false;
    }
};

class GuiyinDis : public DistanceSkillV2
{
public:
    GuiyinDis() : DistanceSkillV2("#guiyin") { m_baseAmount = -2; setHolderSelector(CorrectSkill_Primary); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.getHolder()) return CorrectSkillResult::noEffect();
        const SkillInstance *instance = ctx.getHolder()->findSkillInstance(ctx.instanceRef.key.skillName, ctx.instanceRef.key.instanceID);
        if (!instance || !instance->parentRef.isValid()) return CorrectSkillResult::noEffect();
        const SkillInstanceRef parent = instance->parentRef;
        return ctx.getHolder()->getSkillInstanceCorrectStateValue(parent.key.skillName, parent.key.instanceID, "level").toInt() > 0
            ? CorrectSkillResult::useAmount(ctx.getCurrentAmount()) : CorrectSkillResult::noEffect();
    }
};

class Qiubang : public TriggerSkillV2
{
public:
    Qiubang() : TriggerSkillV2("qiubang") { events << TargetConfirming; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const CardUseStruct use = data.value<CardUseStruct>();
        return use.to.size() == 1 && use.to.first() == player && use.from && use.from != player && use.card
            && use.card->isBlack() && !use.card->isKindOf("SkillCard") ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        QVariantMap query{{"kind", "skill_invoked"}, {"player", owner->objectName()},
            {"skill_name", ctx.sourceRef.key.skillName}, {"skill_owner", ctx.sourceRef.ownerObjectName}};
        int count = 0;
        for (;;) {
            const QVariantMap page = room->queryHistoryFacts(query);
            if (!page.value("complete").toBool() || !page.value("attribution_complete").toBool()) return false;
            for (const QVariant &value : page.value("items").toList()) {
                const QVariantMap invocation = value.toMap().value("data").toMap();
                if (invocation.value("instance_id").toInt() == ctx.sourceRef.key.instanceID
                    && invocation.value("invoked_skill").toString() == objectName()) ++count;
            }
            if (!page.value("has_more").toBool()) break;
            query["watermark"] = page.value("watermark"); query["after"] = page.value("next_after");
        }
        if (!owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        // The accepted activation includes this use; prior uses alone were off by one.
        ctx.amount = qBound(1, count + 1 - 3, 3);
        ctx.targets << owner;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (target != owner || use.to.size() != 1 || use.to.first() != target || !use.card) return false;
        room->broadcastSkillInvoke(objectName());
        target->drawCards(getEffectiveAmount(ctx), objectName());
        use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !target->isAlive()) return false;
        auto *duel = new Duel(use.card->getSuit(), use.card->getNumber());
        duel->addSubcard(use.card);
        duel->setSkillName(objectName());
        for (const QString &flag : use.card->getFlags()) duel->setFlags(flag);
        use.setOwnedCard(duel);
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};

class QiubangDis : public DistanceSkillV2
{
public:
    QiubangDis() : DistanceSkillV2("#qiubang") { m_baseAmount = -2; setHolderSelector(CorrectSkill_Primary); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.getHolder() ? CorrectSkillResult::useAmount(ctx.getCurrentAmount()) : CorrectSkillResult::noEffect();
    }
};



YoushuiCard::YoushuiCard()
{
    setSkillName("youshui");
    will_throw = false;
}

bool YoushuiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return to_select->hasFlag("youshui_target") && targets.length() == 0;
}

void YoushuiCard::use(Room *room, ServerPlayer *, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.at(0);
    if (!target)
        return;
    target->setTag("youshuiNum", this->subcardsLength());
    room->obtainCard(target, this, false);
}

class YoushuiVS : public ViewAsSkillV2
{
public:
    YoushuiVS() : ViewAsSkillV2("youshui", 0) { setResponseOrUse(true); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.activationRef.isValid()
            && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && request.pattern == "@@youshui"
            && !request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "recipient").toString().isEmpty();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return card && card->getEffectiveId() >= 0 && !card->hasFlag("using") && request.initiator
            && !request.selectedCardIds.contains(card->getEffectiveId()) && request.initiator->getCards("he").contains(card);
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.isEmpty()) return false;
        ActiveSkillRequest prefix = request; prefix.selectedCardIds.clear();
        for (int id : request.selectedCardIds) {
            if (!canSelectCard(prefix, Sanguosha->getCard(id))) return false;
            prefix.selectedCardIds << id;
        }
        return true;
    }
    bool willThrowSelectedCards() const override { return false; }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        return selected.isEmpty() && target && target->isAlive() && request.initiator && request.activationRef.isValid()
            && target->objectName() == request.initiator->getSkillInstanceStateValue(objectName(), request.activationRef.key.instanceID, "recipient").toString();
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return targets.size() == 1 && canSelectTarget(request, {}, targets.first()); }
    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? hayateProxy(this, request) : nullptr; }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!ctx.use_card || !target || !ctx.initiator) return FinishSkill;
        Room *room = target->getRoom();
        QList<int> material;
        for (int id : ctx.use_card->getSubcards())
            if (room->getCardOwner(id) == ctx.initiator
                && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip)) material << id;
        if (material.size() != ctx.use_card->subcardsLength()) return FinishSkill;
        room->obtainCard(target, ctx.use_card, false);
        int count = 0;
        for (int id : material) if (room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand) ++count;
        if (!count) return ContinueEffects;
        QVariantList receipts = target->getTag("YoushuiReceipts").toList();
        receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"count", count}, {"execution", ctx.executionID}};
        target->setTag("YoushuiReceipts", receipts);
        return ContinueEffects;
    }
};

class Youshui : public TriggerSkillV2
{
public:
    Youshui() : TriggerSkillV2("youshui")
    { events << EnterDying; global = true; view_as_skill = new YoushuiVS; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        const DyingStruct dying = data.value<DyingStruct>();
        if (!dying.damage || !dying.who || !dying.who->isAlive() || !dying.damage->card
            || (!dying.damage->card->isKindOf("Slash") && !dying.damage->card->isKindOf("Duel"))) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != dying.who && !owner->isNude()) result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<DyingStruct>().who;
        if (!target) return false;
        const QVariant previous = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "recipient");
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int previousSelector = owner->getMark(selector);
        const bool wasTarget = target->hasFlag("youshui_target");
        auto restore = qScopeGuard([&] {
            if (owner->hasSkillInstance(objectName(), ctx.instanceID)) owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "recipient", previous);
            room->setPlayerMark(owner, selector, previousSelector);
            if (!wasTarget) room->setPlayerFlag(target, "-youshui_target");
        });
        owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "recipient", target->objectName());
        room->setPlayerMark(owner, selector, ctx.activationRef.key.instanceID);
        room->setPlayerFlag(target, "youshui_target");
        room->askForUseCard(owner, "@@youshui", "@youshui-use", -1, Card::MethodNone);
        return false;
    }
};

class YoushuiReturn : public TriggerSkillV2
{
public:
    YoushuiReturn() : TriggerSkillV2("#youshui-return") { events << QuitDying << Death; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == Death && data.value<DeathStruct>().who) data.value<DeathStruct>().who->removeTag("YoushuiReceipts");
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != QuitDying) return true;
        ServerPlayer *debtor = data.value<DyingStruct>().who;
        if (!debtor || !debtor->isAlive()) return true;
        for (const QVariant &value : debtor->getTag("YoushuiReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = ctx.initiator = owner; ctx.invoker = debtor;
            ctx.instanceID = receipt.value("instance").toInt();
            ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), ctx.instanceID));
            ctx.extra_data = receipt; ctx.amount = 2 * receipt.value("count").toInt(); ctx.targets << owner;
            ctx.original_data = &data; ctx.current_event = event; contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->isAlive() && ctx.invoker->getTag("YoushuiReceipts").toList().contains(ctx.extra_data); }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariantList receipts = ctx.invoker->getTag("YoushuiReceipts").toList(); receipts.removeOne(ctx.extra_data);
        ctx.invoker->setTag("YoushuiReceipts", receipts);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int i = 0; i < getEffectiveAmount(ctx) && target->isAlive() && ctx.invoker->isAlive() && !ctx.invoker->isNude(); ++i) {
            const int id = room->askForCardChosen(ctx.invoker, ctx.invoker, "he", "youshui", false, Card::MethodNone);
            if (id < 0 || room->getCardOwner(id) != ctx.invoker
                || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)) break;
            room->obtainCard(target, id, false);
        }
        return false;
    }
};

// yuri
class Zuozhan : public TriggerSkillV2
{
public:
    Zuozhan() : TriggerSkillV2("zuozhan") { events << EventPhaseStart; global = true; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const override
    {
        TriggerList result;
        if (!player || !player->isAlive() || player->getPhase() != Player::Start) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner->getHp() <= player->getHp() || player->hasClub("sss")) result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        QStringList choices{"1_Zuozhan", "2_Zuozhan", "3_Zuozhan", "4_Zuozhan"}, order;
        while (choices.size() > 1) {
            const QString choice = room->askForChoice(owner, QString("zuozhan%1%from:").arg(order.size() + 1)
                + ctx.invoker->objectName(), choices.join('+'));
            if (!choices.contains(choice)) return false;
            order << choice; choices.removeAll(choice);
        }
        order << choices.first() << "0_Zuozhan";
        ctx.extra_data = order; ctx.targets << ctx.invoker; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        // A player's turn has one physical order; a later accepted arrangement replaces it atomically.
        target->setTag("ZuozhanOrder", QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"order", ctx.extra_data.toStringList()}});
        if (target->hasClub("sss")) room->doLightbox(objectName() + "$", 800);
        Q_UNUSED(owner);
        return false;
    }
};

class ZuozhanOrder : public TriggerSkillV2
{
public:
    ZuozhanOrder() : TriggerSkillV2("#zuozhan-order")
    { events << EventPhaseChanging << Death; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (event == Death && data.value<DeathStruct>().who) data.value<DeathStruct>().who->removeTag("ZuozhanOrder");
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::NotActive) player->removeTag("ZuozhanOrder");
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != EventPhaseChanging || !player || !player->isAlive() || data.value<PhaseChangeStruct>().to == Player::NotActive) return true;
        const QVariantMap receipt = player->getTag("ZuozhanOrder").toMap();
        if (receipt.value("order").toStringList().isEmpty()) return true;
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        if (!owner) return true;
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = ctx.initiator = owner; ctx.invoker = player;
        ctx.instanceID = receipt.value("instance").toInt();
        ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), ctx.instanceID));
        ctx.extra_data = receipt; ctx.targets << player; ctx.original_data = &data; ctx.current_event = event; contexts << ctx;
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->getTag("ZuozhanOrder").toMap() == ctx.extra_data.toMap(); }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target != ctx.invoker) return false;
        QVariantMap receipt = target->getTag("ZuozhanOrder").toMap();
        QStringList order = receipt.value("order").toStringList();
        if (order.isEmpty()) return false;
        PhaseChangeStruct change = ctx.original_data->value<PhaseChangeStruct>();
        const QString next = order.takeFirst();
        if (next == "1_Zuozhan") change.to = Player::Judge;
        else if (next == "2_Zuozhan") change.to = Player::Draw;
        else if (next == "3_Zuozhan") change.to = Player::Play;
        else if (next == "4_Zuozhan") change.to = Player::Discard;
        else change.to = Player::Finish;
        if (order.isEmpty()) target->removeTag("ZuozhanOrder");
        else { receipt["order"] = order; target->setTag("ZuozhanOrder", receipt); }
        *ctx.original_data = QVariant::fromValue(change);
        return false;
    }
};

class Nishen : public TriggerSkillV2
{
public:
    Nishen() : TriggerSkillV2("nishen") { frequency = Club; club_name = "sss"; events << EnterDying; global = true; }
    LimitScope getLimitScope() const override { return Limit_Custom; }
    bool checkCustomUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *target = ctx.original_data ? ctx.original_data->value<DyingStruct>().who : nullptr;
        return ctx.owner && target && ref.isValid() && !ctx.owner->getSkillInstanceStateValue(ref.key.skillName,
            ref.key.instanceID, "invited").toStringList().contains(target->objectName());
    }
    void addUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        ServerPlayer *target = ctx.original_data->value<DyingStruct>().who;
        QStringList invited = ctx.owner->getSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "invited").toStringList();
        invited << target->objectName(); invited.removeDuplicates();
        ctx.owner->setSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "invited", invited);
    }
    void resetUsage(const SkillContext &ctx) const override
    {
        const SkillInstanceRef ref = getUsageRef(ctx);
        if (ctx.owner && ref.isValid()) ctx.owner->removeSkillInstanceStateValue(ref.key.skillName, ref.key.instanceID, "invited");
    }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        ServerPlayer *target = data.value<DyingStruct>().who;
        if (!target || !target->isAlive() || target->hasClub()) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != target) result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!checkCustomUsage(ctx) || !owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << ctx.original_data->value<DyingStruct>().who; return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!checkCustomUsage(ctx)) return false; addUsage(ctx); return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target->hasClub()) return false;
        room->broadcastSkillInvoke(objectName());
        if (room->askForChoice(target, objectName(), "nishen_accept+cancel", QVariant::fromValue(owner)) == "nishen_accept") {
            target->addClub("sss");
            target->setTag("NishenMembership", QVariantMap{{"owner", ctx.sourceRef.ownerObjectName},
                {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID}});
        } else {
            LogMessage log; log.type = "$refuse_club"; log.from = target; log.arg = "sss"; room->sendLog(log);
        }
        return false;
    }
};

class NishenClub : public TriggerSkillV2
{
public:
    NishenClub() : TriggerSkillV2("#nishen-club") { events << Death; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        ServerPlayer *dead = data.value<DeathStruct>().who;
        if (dead && player == dead && dead->hasClub("sss")) room->setTag("no_reward_or_punish", dead->objectName());
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        ServerPlayer *dead = data.value<DeathStruct>().who;
        if (!dead || player != dead || !dead->hasClub("sss")) return true;
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = ctx.initiator = ctx.invoker = dead;
        const QVariantMap receipt = dead->getTag("NishenMembership").toMap();
        ctx.sourceRef = SkillInstanceRef(receipt.value("owner").toString(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
        if (ctx.sourceRef.isValid())
            if (ServerPlayer *source = room->findPlayerByObjectName(ctx.sourceRef.ownerObjectName, true)) ctx.owner = ctx.initiator = source;
        for (ServerPlayer *target : room->getPlayersByClub("sss")) if (target != dead && target->isAlive()) ctx.targets << target;
        ctx.original_data = &data; ctx.current_event = event;
        if (!ctx.targets.isEmpty()) contexts << ctx;
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.targets.value(0, ctx.invoker); }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->hasClub("sss"); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (!target->hasClub("sss")) return false;
        if (target->isWounded() && room->askForChoice(target, "nishen", "draw+recover", *ctx.original_data) == "recover")
            room->recover(target, RecoverStruct(ctx.invoker, nullptr, getEffectiveAmount(ctx)));
        else target->drawCards(2 * getEffectiveAmount(ctx), "nishen");
        return false;
    }
};

class Mengxian : public TriggerSkillV2
{
public:
    Mengxian() : TriggerSkillV2("mengxian") { frequency = Frequent; events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Draw
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << owner; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target != owner) return false;
        const QVariant previous = owner->getTag("MengxianJudge");
        auto restore = qScopeGuard([&] { if (previous.isValid()) owner->setTag("MengxianJudge", previous); else owner->removeTag("MengxianJudge"); });
        // A pending judgement keeps the original source even if retrial removes its grant.
        owner->setTag("MengxianJudge", QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
            {"instance", ctx.sourceRef.key.instanceID}, {"execution", ctx.executionID}});
        do {
            const QString choice = room->askForChoice(owner, objectName(), owner->canDiscard(owner, "h") ? "basic+trick+equip" : "basic+trick");
            if (choice == "equip") {
                const int id = room->askForCardChosen(owner, owner, "h", objectName(), false, Card::MethodDiscard);
                if (id < 0 || room->getCardOwner(id) != owner || room->getCardPlace(id) != Player::PlaceHand
                    || !owner->canDiscard(owner, id)) break;
                room->throwCard(id, owner, owner);
                if (room->getCardOwner(id) == owner && room->getCardPlace(id) == Player::PlaceHand) break;
            }
            if (!owner->isAlive()) break;
            JudgeStruct judge;
            judge.who = owner; judge.negative = false; judge.play_animation = false; judge.time_consuming = true;
            judge.reason = objectName();
            judge.pattern = choice == "equip" ? "EquipCard" : choice == "trick" ? "TrickCard" : "BasicCard";
            room->judge(judge);
            if (!judge.card) break;
            if (judge.card->isKindOf(judge.pattern.toLatin1().constData())) {
                room->broadcastSkillInvoke(objectName(), choice == "basic" ? 1 : choice == "trick" ? 2 : 3);
                room->doLightbox(objectName() + "$", 500); break;
            }
        } while (owner->isAlive() && owner->hasSkillInstance(ctx.sourceRef.key.skillName, ctx.sourceRef.key.instanceID)
            && owner->askForSkillInvoke(this, *ctx.original_data));
        return false;
    }
};

class MengxianJudge : public TriggerSkillV2
{
public:
    MengxianJudge() : TriggerSkillV2("#mengxian-judge") { events << FinishJudge; global = true; frequency = Compulsory; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        const JudgeStruct *judge = data.value<JudgeStruct *>();
        if (!judge || judge->reason != "mengxian" || !judge->card || !player || judge->who != player) return true;
        const QVariantMap receipt = player->getTag("MengxianJudge").toMap();
        ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
        if (!owner) return true;
        SkillContext ctx;
        ctx.skill_name = objectName(); ctx.owner = ctx.initiator = owner; ctx.invoker = player;
        ctx.instanceID = receipt.value("instance").toInt();
        ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), ctx.instanceID));
        ctx.extra_data = receipt; ctx.targets << player; ctx.original_data = &data; ctx.current_event = event; contexts << ctx;
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->isAlive() && ctx.invoker->getTag("MengxianJudge").toMap() == ctx.extra_data.toMap(); }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (target == ctx.invoker && judge && judge->card && room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge)
            room->obtainCard(target, judge->card);
        return false;
    }
};

namespace {
QVariantList yuanwangIds(const QList<int> &ids)
{
    QVariantList values;
    for (int id : ids) values << id;
    return values;
}
QList<int> yuanwangCardIds(const QVariant &value)
{
    QList<int> ids;
    for (const QVariant &id : value.toList()) if (id.toInt() >= 0 && !ids.contains(id.toInt())) ids << id.toInt();
    return ids;
}
QVariantMap yuanwangSnapshot(ServerPlayer *player)
{
    QVariantMap state;
    state["hand"] = yuanwangIds(player->handCards());
    QList<int> equips, judges;
    for (const Card *card : player->getEquips()) equips << card->getEffectiveId();
    for (const Card *card : player->getJudgingArea()) judges << card->getEffectiveId();
    state["equip"] = yuanwangIds(equips); state["judge"] = yuanwangIds(judges);
    state["hp"] = player->getHp(); state["maxhp"] = player->getMaxHp();
    QVariantMap marks, piles;
    for (const QString &name : player->getMarkNames()) marks[name] = player->getMark(name);
    for (const QString &name : player->getPileNames()) {
        QStringList viewers;
        for (ServerPlayer *viewer : player->getRoom()->getAllPlayers(true))
            if (player->pileOpen(name, viewer->objectName())) viewers << viewer->objectName();
        piles[name] = QVariantMap{{"cards", yuanwangIds(player->getPile(name))}, {"viewers", viewers}};
    }
    state["marks"] = marks; state["piles"] = piles;
    QVariantList skills;
    for (const SkillInstance &instance : player->getSkillInstances())
        skills << QVariantMap{{"name", instance.skillName}, {"id", instance.instanceID},
            {"state", player->getSkillInstanceState(instance.skillName, instance.instanceID)},
            {"correct", instance.correctState}, {"has_amount", instance.hasAmountOverride}, {"amount", instance.amountOverride}};
    state["skills"] = skills;
    return state;
}
void yuanwangRestore(Room *room, ServerPlayer *player, const QVariantMap &state)
{
    const QList<int> hand = yuanwangCardIds(state.value("hand"));
    const QList<int> equip = yuanwangCardIds(state.value("equip"));
    const QList<int> judge = yuanwangCardIds(state.value("judge"));
    const QVariantMap piles = state.value("piles").toMap();
    QList<int> retained = hand + equip + judge, discard;
    for (auto it = piles.cbegin(); it != piles.cend(); ++it)
        retained << yuanwangCardIds(it.value().toMap().value("cards"));
    // Rewind only this participant's board. The immutable Room journal is never erased.
    for (const Card *card : player->getCards("hej"))
        if (!retained.contains(card->getEffectiveId())) discard << card->getEffectiveId();
    for (const QString &pile : player->getPileNames())
        for (int id : player->getPile(pile)) if (!retained.contains(id)) discard << id;
    if (!discard.isEmpty()) room->moveCardsAtomic(CardsMoveStruct(discard, nullptr, Player::DiscardPile,
        CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, player->objectName(), "yuanwang", QString())), true);
    const auto restoreZone = [&](const QList<int> &ids, Player::Place place) {
        QList<int> missing;
        for (int id : ids) if (room->getCardOwner(id) != player || room->getCardPlace(id) != place) missing << id;
        if (!missing.isEmpty()) room->moveCardsAtomic(CardsMoveStruct(missing, player, place,
            CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, player->objectName(), "yuanwang", QString())), place != Player::PlaceHand);
    };
    restoreZone(hand, Player::PlaceHand); restoreZone(equip, Player::PlaceEquip); restoreZone(judge, Player::PlaceDelayedTrick);
    for (auto it = piles.cbegin(); it != piles.cend(); ++it) {
        const QVariantMap pile = it.value().toMap();
        QList<int> missing;
        for (int id : yuanwangCardIds(pile.value("cards"))) if (!player->getPile(it.key()).contains(id)) missing << id;
        QList<ServerPlayer *> viewers;
        for (const QString &name : pile.value("viewers").toStringList())
            if (ServerPlayer *viewer = room->findPlayerByObjectName(name, true)) viewers << viewer;
        player->setPileOpen(it.key(), ".");
        for (ServerPlayer *viewer : viewers) player->setPileOpen(it.key(), viewer->objectName());
        if (!missing.isEmpty()) {
            // Preserve the exact viewer set, including an empty set. addToPile's
            // empty-viewer fallback would expose it to the material's current owners.
            CardsMoveStruct move(missing, player, Player::PlaceSpecial,
                CardMoveReason(CardMoveReason::S_REASON_UNKNOWN, player->objectName(), "yuanwang", QString()));
            move.to_pile_name = it.key();
            room->moveCardsAtomic(move, false);
        }
    }
    room->setPlayerProperty(player, "maxhp", state.value("maxhp"));
    room->setPlayerProperty(player, "hp", state.value("hp"));
    // Quotas are marks; custom quotas are instance state. Restore both for the
    // exact surviving grant, never copy a retired grant's private state to its replacement.
    const QVariantMap marks = state.value("marks").toMap();
    QStringList usagePrefixes;
    for (const QVariant &value : state.value("skills").toList()) {
        const QVariantMap saved = value.toMap();
        const QString name = saved.value("name").toString(); const int id = saved.value("id").toInt();
        if (!player->hasSkillInstance(name, id)) continue;
        usagePrefixes << SkillInstanceUtils::formatUsageMarkKey(name, id, "-")
            << SkillInstanceUtils::formatUsageMarkKey(name, id, "_");
    }
    const auto rewindsMark = [&](const QString &name) {
        if (!name.startsWith("Usage_")) return true;
        for (const QString &prefix : usagePrefixes) if (name.startsWith(prefix)) return true;
        return false; // Neither revive a retired quota nor reset a newly acquired grant.
    };
    for (const QString &name : player->getMarkNames())
        if (rewindsMark(name) && !marks.contains(name)) room->setPlayerMark(player, name, 0);
    for (auto it = marks.cbegin(); it != marks.cend(); ++it)
        if (rewindsMark(it.key())) room->setPlayerMark(player, it.key(), it.value().toInt());
    for (const QVariant &value : state.value("skills").toList()) {
        const QVariantMap saved = value.toMap();
        const QString name = saved.value("name").toString(); const int id = saved.value("id").toInt();
        if (!player->hasSkillInstance(name, id)) continue;
        player->clearSkillInstanceCorrectState(name, id);
        const QVariantMap correct = saved.value("correct").toMap();
        for (auto it = correct.cbegin(); it != correct.cend(); ++it)
            player->setSkillInstanceCorrectStateValue(name, id, it.key(), it.value());
        if (saved.value("has_amount").toBool()) player->setSkillInstanceAmountOverride(name, id, saved.value("amount").toInt());
        else player->resetSkillInstanceAmountOverride(name, id);
        if (const SkillInstance *instance = player->findSkillInstance(name, id)) {
            room->notifySkillInstanceCorrectState(player, *instance, "clear");
            for (auto it = correct.cbegin(); it != correct.cend(); ++it)
                room->notifySkillInstanceCorrectState(player, *instance, "set", it.key(), it.value());
            room->notifySkillInstanceAmount(player, *instance);
        }
        // Public correction/amount setters clear description provenance on clients;
        // the private snapshot must be the last notification for this grant.
        player->setSkillInstanceState(name, id, saved.value("state").toMap());
    }
    player->gainAnExtraTurn();
}
bool yuanwangHasEpisode(Room *room, quint64 id)
{
    for (const QVariant &value : room->getTag("YuanwangEpisodes").toList())
        if (value.toMap().value("id").toULongLong() == id) return true;
    return false;
}
void yuanwangEndEpisode(Room *room, quint64 id)
{
    QVariantList remaining;
    for (const QVariant &value : room->getTag("YuanwangEpisodes").toList())
        if (value.toMap().value("id").toULongLong() != id) remaining << value;
    room->setTag("YuanwangEpisodes", remaining);
    room->setTag("sos_status", !remaining.isEmpty()); // Compatibility/UI projection only.
    if (room->getTag("YuanwangClosedSpace").toMap().value("id").toULongLong() == id) {
        room->removeTag("YuanwangClosedSpace");
        if (room->getAura() == "closedSpace") room->clearAura();
    }
}
}

class Yuanwang : public TriggerSkillV2
{
public:
    Yuanwang() : TriggerSkillV2("yuanwang")
    { frequency = Club; club_name = "sos"; events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            && (player->getPhase() == Player::RoundStart || player->getPhase() == Player::Play)
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (owner->getPhase() == Player::RoundStart) {
            ctx.choice = "start";
            for (ServerPlayer *target : room->getAlivePlayers()) if (!target->hasClub("sos")) ctx.targets << target;
            return true;
        }
        ctx.choice = "play";
        QVariantMap selection;
        if (qsanRandomBounded(10) < 3) {
            QList<ServerPlayer *> victims;
            for (ServerPlayer *target : room->getAlivePlayers())
                if (target->hasClub() && !target->hasClub("sos") && !target->isKongcheng() && owner->canGet(target, "h")) victims << target;
            if (!victims.isEmpty()) {
                ServerPlayer *victim = victims.at(qsanRandomBounded(victims.size()));
                const int id = victim->getRandomHandCardId();
                if (id >= 0 && owner->canGet(victim, id)) {
                    selection["victim"] = victim->objectName(); selection["card"] = id; ctx.targets << victim;
                }
            }
        }
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getPlayersWithNoClub())
            if (target != owner && target->getKingdom() != "real" && target->getKingdom() != "wu") candidates << target;
        if (!candidates.isEmpty())
            if (ServerPlayer *target = room->askForPlayerChosen(owner, candidates, objectName(), "@yuanwang", true)) {
                selection["invite"] = target->objectName(); if (!ctx.targets.contains(target)) ctx.targets << target;
            }
        ctx.extra_data = selection;
        return !ctx.targets.isEmpty();
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (ctx.choice == "start") {
            const int id = room->getTag("YuanwangSerial").toInt() + 1;
            room->setTag("YuanwangSerial", QVariant::fromValue(id));
            QVariantList episodes = room->getTag("YuanwangEpisodes").toList();
            episodes << QVariantMap{{"id", QVariant::fromValue(id)}, {"owner", ctx.sourceRef.ownerObjectName},
                {"skill", ctx.sourceRef.key.skillName}, {"instance", ctx.sourceRef.key.instanceID},
                {"founder", owner->objectName()}, {"amount", getEffectiveAmount(ctx)}};
            room->setTag("YuanwangEpisodes", episodes); room->setTag("sos_status", true);
            room->doLightbox("yuanwang$", 500);
        }
        for (ServerPlayer *target : ctx.targets) skillEffect(event, room, owner, ctx, target);
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "start") { if (!target->hasClub("sos")) target->turnOver(); return false; }
        const QVariantMap selection = ctx.extra_data.toMap();
        if (ctx.choice == "obtain") {
            const int id = selection.value("card", -1).toInt();
            ServerPlayer *victim = room->findPlayerByObjectName(selection.value("victim").toString());
            if (victim && room->getCardOwner(id) == victim && room->getCardPlace(id) == Player::PlaceHand && target->canGet(victim, id))
                room->obtainCard(target, id, false);
            return false;
        }
        if (target->objectName() == selection.value("victim").toString()) {
            const int id = selection.value("card", -1).toInt();
            if (room->getCardOwner(id) == target && room->getCardPlace(id) == Player::PlaceHand && owner->canGet(target, id)) {
                LogMessage log; log.type = "$yuanwang_obtain"; log.from = target; log.arg = target->getClubName();
                log.card_str = QString::number(id); room->sendLog(log);
                SkillContext obtain = ctx; obtain.choice = "obtain"; skillEffect(event, room, owner, obtain, owner);
            }
        }
        if (target->objectName() == selection.value("invite").toString() && !target->hasClub()) {
            room->broadcastSkillInvoke(objectName());
            if (room->askForChoice(target, objectName(), "yuanwang_accept+cancel", QVariant::fromValue(owner)) == "yuanwang_accept") {
                target->addClub("sos"); if (!target->faceUp()) target->turnOver();
            } else {
                LogMessage log; log.type = "$refuse_club"; log.from = target; log.arg = "sos"; room->sendLog(log);
            }
        }
        return false;
    }
};

class YuanwangEffects : public TriggerSkillV2
{
public:
    YuanwangEffects() : TriggerSkillV2("#yuanwang-effects")
    {
        frequency = Compulsory; global = true;
        events << EventPhaseStart << TurnStart << EventPhaseChanging << TargetSpecified << CardUsed
            << TrickCardCanceling << SlashProceed << Death << TurnBroken;
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (event == Death && player && player == data.value<DeathStruct>().who) {
            for (const QVariant &value : room->getTag("YuanwangEpisodes").toList())
                if (value.toMap().value("founder").toString() == player->objectName())
                    yuanwangEndEpisode(room, value.toMap().value("id").toULongLong());
            player->removeTag("YuanwangAugust");
            if (room->getTag("YuanwangClosedSpace").toMap().value("actor").toString() == player->objectName()) {
                room->removeTag("YuanwangClosedSpace"); if (room->getAura() == "closedSpace") room->clearAura();
            }
        }
        if (event == EventPhaseChanging && player && data.value<PhaseChangeStruct>().to == Player::NotActive
            && room->getTag("YuanwangClosedSpace").toMap().value("actor").toString() == player->objectName()) {
            room->removeTag("YuanwangClosedSpace"); if (room->getAura() == "closedSpace") room->clearAura();
        }
        if (event == TurnBroken && player) {
            // An aborted turn cannot leave a rewind armed for a later unrelated turn.
            player->removeTag("YuanwangAugust");
            if (room->getTag("YuanwangClosedSpace").toMap().value("actor").toString() == player->objectName()) {
                room->removeTag("YuanwangClosedSpace"); if (room->getAura() == "closedSpace") room->clearAura();
            }
        }
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data,
                                QList<SkillContext> &contexts) const override
    {
        if (!player || !player->isAlive()) return true;
        const auto append = [&](const QVariantMap &receipt, const QString &choice, ServerPlayer *target) {
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) return;
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = ctx.initiator = owner; ctx.invoker = player;
            ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), receipt.value("instance").toInt()));
            // One retained context per episode. Its activation grant may already be gone.
            // The root may sponsor multiple independent episodes. Dispatch by the
            // stable episode ID while retaining the original grant in sourceRef.
            ctx.instanceID = receipt.value("id").toInt(); ctx.amount = receipt.value("amount", 1).toInt();
            ctx.choice = choice; ctx.extra_data = receipt; ctx.original_data = &data; ctx.current_event = event;
            if (target) ctx.targets << target;
            contexts << ctx;
        };
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::NotActive) {
            const QVariantMap snapshot = player->getTag("YuanwangAugust").toMap();
            if (!snapshot.isEmpty()) append(snapshot, "rewind", player);
        }
        for (const QVariant &value : room->getTag("YuanwangEpisodes").toList()) {
            const QVariantMap receipt = value.toMap();
            const quint64 id = receipt.value("id").toULongLong();
            if (event == TurnStart && receipt.value("founder").toString() == player->objectName()) { append(receipt, "end", player); continue; }
            if (event == EventPhaseStart) {
                if (player->getPhase() == Player::Finish
                    && room->getTag("YuanwangClosedSpace").toMap().value("id").toULongLong() == id) append(receipt, "close", player);
                if (!player->hasClub("sos")) continue;
                if (player->getPhase() == Player::Play) append(receipt, "armor", player);
                if (player->getPhase() == Player::RoundStart) append(receipt, "round", player);
            } else if (event == EventPhaseChanging && player->hasClub("sos")) {
                const Player::Phase phase = data.value<PhaseChangeStruct>().to;
                if (phase != Player::Draw && phase != Player::Play && phase != Player::NotActive) append(receipt, "phase", player);
            } else if (event == TargetSpecified) {
                const CardUseStruct use = data.value<CardUseStruct>();
                if (player == use.from && player->hasClub("sos") && use.card && use.card->isBlack() && use.to.size() == 1
                    && use.to.first() != player && !use.to.first()->hasClub("sos")) append(receipt, "beam", use.to.first());
            } else if (event == CardUsed) {
                const CardUseStruct use = data.value<CardUseStruct>();
                if (player == use.from && player->hasClub("sos") && use.card && !use.card->isVirtualCard()
                    && (use.card->isKindOf("BasicCard") || use.card->isKindOf("TrickCard"))) append(receipt, "return", player);
            } else if ((event == TrickCardCanceling || event == SlashProceed) && room->getAura() == "closedSpace"
                && room->getTag("YuanwangClosedSpace").toMap().value("id").toULongLong() == id)
                append(receipt, event == SlashProceed ? "slash" : "trick",
                    event == SlashProceed ? data.value<SlashEffectStruct>().to : data.value<CardEffectStruct>().to);
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        const quint64 id = ctx.extra_data.toMap().value("id").toULongLong();
        return ctx.choice == "rewind" ? ctx.invoker && ctx.invoker->getTag("YuanwangAugust").toMap().value("id").toULongLong() == id
            : yuanwangHasEpisode(room, id);
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        // Collection may run repeatedly while other skills resolve. Roll only once
        // when this retained source is actually selected for its event.
        if (ctx.choice == "armor") return qsanRandomBounded(10) == 0;
        if (ctx.choice == "phase") return qsanRandomBounded(100) < 3;
        if (ctx.choice == "beam") return qsanRandomBounded(20) == 0;
        if (ctx.choice == "return") return qsanRandomBounded(100) < 6;
        if (ctx.choice == "round") {
            QVariantMap receipt = ctx.extra_data.toMap();
            int augustRate = 5, spaceRate = 5;
            for (const Card *card : ctx.invoker->getJudgingArea()) if (card->isKindOf("Indulgence")) augustRate = 30;
            ServerPlayer *founder = room->findPlayerByObjectName(receipt.value("founder").toString());
            if (founder) for (const Card *card : founder->getJudgingArea()) if (card->isKindOf("SupplyShortage")) spaceRate = 30;
            receipt["snapshot_roll"] = ctx.invoker->getTag("YuanwangAugust").toMap().isEmpty() && qsanRandomBounded(100) < augustRate;
            receipt["space_roll"] = qsanRandomBounded(100) < spaceRate;
            ctx.extra_data = receipt;
            return receipt.value("snapshot_roll").toBool() || receipt.value("space_roll").toBool();
        }
        return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        if (ctx.choice == "round") {
            const QVariantMap receipt = ctx.extra_data.toMap();
            for (ServerPlayer *target : ctx.targets) {
                if (receipt.value("snapshot_roll").toBool()) {
                    SkillContext snapshot = ctx; snapshot.choice = "snapshot";
                    skillEffect(event, room, owner, snapshot, target);
                }
                if (receipt.value("space_roll").toBool()) {
                    SkillContext space = ctx; space.choice = "space";
                    skillEffect(event, room, owner, space, target);
                }
            }
            return false;
        }
        if (ctx.choice == "end") {
            yuanwangEndEpisode(room, ctx.extra_data.toMap().value("id").toULongLong());
            for (ServerPlayer *target : room->getPlayersByClub("sos")) skillEffect(event, room, owner, ctx, target);
            return false;
        }
        if (ctx.choice == "rewind" && ctx.invoker)
            ctx.invoker->removeTag("YuanwangAugust"); // Expire even if the recipient hook vetoes the rewind.
        bool canceled = false;
        for (ServerPlayer *target : ctx.targets) {
            SkillContext recipient = ctx;
            canceled |= skillEffect(event, room, owner, recipient, target);
        }
        return canceled;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        QVariantMap receipt = ctx.extra_data.toMap();
        if (ctx.choice == "end") { target->turnOver(); return false; }
        if (ctx.choice == "snapshot") {
            if (target->getTag("YuanwangAugust").toMap().isEmpty()) {
                receipt["snapshot"] = yuanwangSnapshot(target); receipt["target"] = target->objectName();
                target->setTag("YuanwangAugust", receipt);
            }
        } else if (ctx.choice == "rewind") {
            if (target != ctx.invoker || receipt.value("target").toString() != target->objectName()) return false;
            target->removeTag("YuanwangAugust"); // Consume before nested moves/extra turns can re-enter.
            LogMessage log; log.type = "$yuanwang_august"; log.from = target; room->sendLog(log);
            yuanwangRestore(room, target, receipt.value("snapshot").toMap());
        } else if (ctx.choice == "armor") {
            QStringList names;
            for (int id : room->getDrawPile()) if (Sanguosha->getCard(id)->isKindOf("Armor")) names << Sanguosha->getCard(id)->objectName();
            if (!names.isEmpty() && target->hasEquipArea(1)) {
                const QString name = names.at(qsanRandomBounded(names.size()));
                LogMessage log; log.type = "$yuanwang_wear"; log.from = target;
                log.card_str = QString::number(room->getCardFromPile(name)); room->sendLog(log); room->installEquip(target, name);
            }
        } else if (ctx.choice == "space") {
            if (room->doAura(target, "closedSpace")) {
                receipt["actor"] = target->objectName(); room->setTag("YuanwangClosedSpace", receipt);
                LogMessage log; log.type = "$yuanwang_closed_space"; room->sendLog(log);
            }
        } else if (ctx.choice == "close") {
            room->removeTag("YuanwangClosedSpace"); if (room->getAura() == "closedSpace") room->clearAura();
        } else if (ctx.choice == "phase" && target == ctx.invoker) {
            PhaseChangeStruct change = ctx.original_data->value<PhaseChangeStruct>();
            change.to = qsanRandomBounded(2) == 0 ? Player::Draw : Player::Play;
            if (change.to == Player::Play && target->getHandcardNum() < 3 && qsanRandomBounded(5) > 1) change.to = Player::Draw;
            *ctx.original_data = QVariant::fromValue(change);
            LogMessage log; log.type = change.to == Player::Draw ? "$yuanwang_add_phase_draw" : "$yuanwang_add_phase_play";
            log.from = target; room->sendLog(log); target->insertPhase(change.to);
        } else if (ctx.choice == "beam") {
            LogMessage log; log.type = "$yuanwang_mikuru_beam"; log.from = ctx.invoker; log.to << target; room->sendLog(log);
            room->damage(DamageStruct("yuanwang", ctx.invoker, target, getEffectiveAmount(ctx), DamageStruct::Thunder));
        } else if (ctx.choice == "return") {
            const Card *card = ctx.original_data->value<CardUseStruct>().card;
            if (card && !card->isVirtualCard() && room->getCardPlace(card->getEffectiveId()) == Player::PlaceTable) {
                LogMessage log; log.type = "$yuanwang_card_back"; log.from = target;
                log.card_str = QString::number(card->getEffectiveId()); room->sendLog(log); room->obtainCard(target, card);
            }
        } else if (ctx.choice == "slash" || ctx.choice == "trick") {
            if (ctx.choice == "slash") {
                const SlashEffectStruct slash = ctx.original_data->value<SlashEffectStruct>();
                if (target != slash.to) return false;
                room->slashResult(slash, nullptr);
            } else if (target != ctx.original_data->value<CardEffectStruct>().to) return false;
            LogMessage log; log.type = "$yuanwang_closed_space_effect"; room->sendLog(log); return true;
        }
        return false;
    }
};

MojuCard::MojuCard()
{
    setSkillName("moju");
}

bool MojuCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.length() == 0;
}

void MojuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.at(0);
    if (!target)
        return;

    int card_id = this->getSubcards().at(0);
    if (card_id == -1)
        return;
    Card *card = Sanguosha->getCard(card_id);
    room->moveCardTo(card, target, Player::PlaceEquip);
    QList<ServerPlayer *> players;
    foreach(ServerPlayer *p, room->getAlivePlayers()){
        foreach(const Card *c, p->getEquips()){
            if (c->getSuit() == card->getSuit()){
                players.append(p);
                break;
            }
        }
        if (!players.contains(p)){
            foreach(const Card *c, p->getJudgingArea()){
                if (c->getSuit() == card->getSuit()){
                    players.append(p);
                    break;
                }
            }
        }
    }
    if (players.count() == 0){
        return;
    }
    ServerPlayer *from = room->askForPlayerChosen(source, players, "moju", "@moju-from:::" + card->getSuitString());
    QList<int> disabled;
    foreach(const Card *c, from->getEquips()){
        if (c->getSuit() != card->getSuit()){
            disabled.append(c->getEffectiveId());
        }
    }
    foreach(const Card *c, from->getJudgingArea()){
        if (c->getSuit() != card->getSuit()){
            disabled.append(c->getEffectiveId());
        }
    }
    int from_id = room->askForCardChosen(source, from, "ej", objectName(), false, Card::MethodNone, disabled);
    Player::Place place = room->getCardPlace(from_id);
    const Card *from_card = Sanguosha->getCard(from_id);
    QList<ServerPlayer *> tos;

    int equip_index = -1;
    if (place == Player::PlaceEquip){
        const EquipCard *equip = qobject_cast<const EquipCard *>(from_card->getRealCard());
        equip_index = static_cast<int>(equip->location());
    }
    foreach(ServerPlayer *p, room->getOtherPlayers(from)){
        if (equip_index != -1) {
            if (p->getEquip(equip_index) == NULL)
                tos << p;
        }
        else {
            if (!source->isProhibited(p, from_card) && !p->containsTrick(from_card->objectName()))
                tos << p;
        }
    }
    ServerPlayer *to = room->askForPlayerChosen(source, tos, "moju_to", "@moju-to:::" + from_card->objectName());
    if (to)
        room->moveCardTo(from_card, from, to, place,
        CardMoveReason(CardMoveReason::S_REASON_TRANSFER,
        source->objectName(), "moju", QString()));
}

class Moju : public ViewAsSkillV2
{
public:
    Moju() : ViewAsSkillV2("moju", 1) { setPhaseName("Play"); }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY && request.initiator->isAlive(); }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    { return request.selectedCardIds.isEmpty() && card && card->isKindOf("EquipCard") && card->getEffectiveId() >= 0
        && !card->hasFlag("using") && request.initiator && request.initiator->getCards("he").contains(card); }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest prefix = request; prefix.selectedCardIds.clear();
        return canSelectCard(prefix, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    TargetMode targetMode() const override { return SelectTargets; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    {
        if (!selected.isEmpty() || !target || !target->isAlive() || !cardSelectionFeasible(request)) return false;
        const Card *card = Sanguosha->getCard(request.selectedCardIds.first());
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        return equip && target->hasEquipArea(int(equip->location())) && target->getEquip(int(equip->location())) != card;
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    { return targets.size() == 1 && canSelectTarget(request, {}, targets.first()); }
    bool willThrowSelectedCards() const override { return false; }
    const Card *createCard(const ActiveSkillRequest &request) const override
    { return cardSelectionFeasible(request) ? hayateProxy(this, request) : nullptr; }
    static QList<ServerPlayer *> destinations(Room *room, ServerPlayer *source, ServerPlayer *from, const Card *card)
    {
        QList<ServerPlayer *> result;
        const Player::Place place = room->getCardPlace(card->getEffectiveId());
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        for (ServerPlayer *target : room->getOtherPlayers(from)) {
            if (place == Player::PlaceEquip && equip && target->hasEquipArea(int(equip->location()))
                && !target->getEquip(int(equip->location()))) result << target;
            else if (place == Player::PlaceJudge && target->hasJudgeArea() && !target->containsTrick(card->objectName())
                && !room->isProhibited(source, target, card)) result << target;
        }
        return result;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.initiator;
        if (!source || !ctx.use_card || ctx.use_card->subcardsLength() != 1) return FinishSkill;
        Room *room = source->getRoom();
        if (ctx.choice == "move") {
            const QVariantMap selected = ctx.extra_data.toMap();
            const int id = selected.value("card").toInt();
            ServerPlayer *from = room->findPlayerByObjectName(selected.value("from").toString(), true);
            const Card *card = Sanguosha->getCard(id);
            if (!from || room->getCardOwner(id) != from || room->getCardPlace(id) != Player::Place(selected.value("place").toInt())
                || !destinations(room, source, from, card).contains(target)) return ContinueEffects;
            room->moveCardTo(card, from, target, room->getCardPlace(id), CardMoveReason(CardMoveReason::S_REASON_TRANSFER,
                source->objectName(), objectName(), QString()));
            return ContinueEffects;
        }
        const int id = ctx.use_card->getSubcards().first();
        const Card *card = Sanguosha->getCard(id);
        const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
        if (!equip || room->getCardOwner(id) != source
            || (room->getCardPlace(id) != Player::PlaceHand && room->getCardPlace(id) != Player::PlaceEquip)
            || !target->hasEquipArea(int(equip->location()))) return ContinueEffects;
        QList<CardsMoveStruct> placement;
        if (const Card *old = target->getEquip(int(equip->location()))) {
            if (old->getEffectiveId() == id) return ContinueEffects;
            placement << CardsMoveStruct(old->getEffectiveId(), target, nullptr, Player::PlaceEquip, Player::DiscardPile,
                CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP, target->objectName()));
        }
        placement << CardsMoveStruct(id, source, target, room->getCardPlace(id), Player::PlaceEquip,
            CardMoveReason(CardMoveReason::S_REASON_TRANSFER, source->objectName(), objectName(), QString()));
        room->moveCardsAtomic(placement, true);
        if (!source->isAlive() || room->getCardOwner(id) != target || room->getCardPlace(id) != Player::PlaceEquip) return ContinueEffects;
        const Card::Suit suit = card->getSuit();
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *from : room->getAlivePlayers())
            for (const Card *candidate : from->getCards("ej"))
                if (candidate->getSuit() == suit && !destinations(room, source, from, candidate).isEmpty()) {
                    candidates << from; break;
                }
        if (candidates.isEmpty()) return ContinueEffects;
        ServerPlayer *from = room->askForPlayerChosen(source, candidates, objectName(), "@moju-from:::" + card->getSuitString());
        QList<int> disabled;
        for (const Card *candidate : from->getCards("ej"))
            if (candidate->getSuit() != suit || destinations(room, source, from, candidate).isEmpty()) disabled << candidate->getEffectiveId();
        const int chosen = room->askForCardChosen(source, from, "ej", objectName(), false, Card::MethodNone, disabled);
        if (chosen < 0 || disabled.contains(chosen) || room->getCardOwner(chosen) != from) return ContinueEffects;
        const Card *moving = Sanguosha->getCard(chosen);
        const QList<ServerPlayer *> targets = destinations(room, source, from, moving);
        if (targets.isEmpty()) return ContinueEffects;
        ServerPlayer *to = room->askForPlayerChosen(source, targets, "moju_to", "@moju-to:::" + moving->objectName());
        SkillContext transfer = ctx;
        transfer.choice = "move"; transfer.targets = {to};
        transfer.extra_data = QVariantMap{{"card", chosen}, {"from", from->objectName()}, {"place", int(room->getCardPlace(chosen))}};
        skillEffect(transfer, to);
        return ContinueEffects;
    }
};

class HayateJiejie : public TriggerSkillV2
{
public:
    HayateJiejie() : TriggerSkillV2("hayate_jiejie") { events << DamageInflicted; global = true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        const DamageStruct damage = data.value<DamageStruct>();
        if (damage.to && damage.to->isAlive() && damage.damage > 0)
            for (ServerPlayer *owner : room->findPlayersBySkillName(objectName())) result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = ctx.original_data->value<DamageStruct>().to;
        if (!target || !isUsable(ctx) || !target->askForSkillInvoke(objectName(), *ctx.original_data)) return false;
        ctx.targets << target; return true;
    }
    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { if (!isUsable(ctx)) return false; addUsage(ctx); return true; }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.manual_effect = true;
        ctx.choice = "peek"; skillEffect(event, room, owner, ctx, owner);
        if (!ctx.extra_data.isValid()) return false;
        ctx.choice = "reduce";
        bool prevented = false;
        for (ServerPlayer *target : ctx.targets) prevented |= skillEffect(event, room, owner, ctx, target);
        return prevented;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        DamageStruct damage = ctx.original_data->value<DamageStruct>();
        if (ctx.choice == "peek") {
            if (!damage.to || target != owner) return false;
            const QList<int> cards = room->getNCards(qMax(0, damage.to->getLostHp() + getEffectiveAmount(ctx)), false);
            if (cards.isEmpty()) return false;
            auto restore = qScopeGuard([&] {
                room->clearAG(owner);
                QList<int> unclaimed;
                for (int id : cards)
                    if (room->getCardPlace(id) == Player::DrawPile && !room->getDrawPile().contains(id)) unclaimed << id;
                if (!unclaimed.isEmpty()) room->returnToTopDrawPile(unclaimed);
            });
            room->fillAG(cards, owner);
            const int id = room->askForAG(owner, cards, false, objectName());
            room->clearAG(owner);
            if (!cards.contains(id) || room->getCardPlace(id) != Player::DrawPile) return false;
            const int color = int(Sanguosha->getCard(id)->getColor());
            room->obtainCard(owner, id, false);
            if (room->getCardOwner(id) == owner && room->getCardPlace(id) == Player::PlaceHand) ctx.extra_data = color;
            room->broadcastSkillInvoke(objectName(), 1);
            return false;
        }
        if (damage.to != target || damage.damage <= 0) return false;
        for (const Card *equip : target->getEquips()) if (int(equip->getColor()) == ctx.extra_data.toInt()) {
            damage.damage = qMax(0, damage.damage - getEffectiveAmount(ctx));
            *ctx.original_data = QVariant::fromValue(damage);
            room->broadcastSkillInvoke(objectName(), qsanRandomBounded(2) + 1);
            room->setEmotion(target, "shield");
            return damage.damage == 0;
        }
        return false;
    }
};

class Qifen : public TriggerSkillV2
{
public:
    Qifen() : TriggerSkillV2("qifen") { events << EventPhaseStart; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
        ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets = room->getAlivePlayers(); return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.manual_effect = true; ctx.choice = "draw";
        room->broadcastSkillInvoke(objectName());
        for (ServerPlayer *target : ctx.targets) skillEffect(event, room, owner, ctx, target);
        if (!owner->isAlive() || !owner->isWounded()) return false;
        ctx.choice = "obtain";
        room->doLightbox(objectName() + "$", 800);
        for (ServerPlayer *target : room->getOtherPlayers(owner)) skillEffect(event, room, owner, ctx, target);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
        else for (int i = 0; i < getEffectiveAmount(ctx) && owner->isAlive() && owner->canGet(target, "he"); ++i) {
            const int id = room->askForCardChosen(owner, target, "he", objectName(), false, Card::MethodGet);
            if (id < 0 || room->getCardOwner(id) != target || !owner->canGet(target, id)) break;
            room->obtainCard(owner, id, room->getCardPlace(id) != Player::PlaceHand);
        }
        return false;
    }
};

class Mishi : public TriggerSkillV2
{
public:
    Mishi() : TriggerSkillV2("mishi") { events << EnterDying; global = true; }
    LimitScope getLimitScope() const override { return Limit_Turn; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        TriggerList result;
        ServerPlayer *dying = data.value<DyingStruct>().who;
        if (!dying || !dying->isAlive()) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (!owner->isKongcheng()) result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || !owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        ctx.targets << ctx.original_data->value<DyingStruct>().who; return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!isUsable(ctx) || owner->isKongcheng()) return false;
        addUsage(ctx); room->showAllCards(owner); return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        ctx.manual_effect = true; ctx.choice = "self";
        room->broadcastSkillInvoke(objectName()); room->doLightbox(objectName() + "$", 800);
        skillEffect(event, room, owner, ctx, owner);
        if (!ctx.extra_data.toBool()) return false;
        ctx.choice = "victim";
        for (ServerPlayer *target : ctx.targets) skillEffect(event, room, owner, ctx, target);
        return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "self") {
            room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), owner));
            ctx.extra_data = true;
        } else {
            // Commit the survivor's obligation before HP loss can re-enter dying resolution.
            QVariantList receipts = target->getTag("MishiReceipts").toList();
            receipts << QVariantMap{{"owner", ctx.sourceRef.ownerObjectName}, {"skill", ctx.sourceRef.key.skillName},
                {"instance", ctx.sourceRef.key.instanceID}, {"execution", ctx.executionID}};
            target->setTag("MishiReceipts", receipts);
            room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), owner));
        }
        return false;
    }
};

class MishiReturn : public TriggerSkillV2
{
public:
    MishiReturn() : TriggerSkillV2("#mishi-return") { events << QuitDying << Death; global = true; frequency = Compulsory; }
    bool recordEvent(TriggerEvent event, Room *, ServerPlayer *, QVariant &data) const override
    {
        if (event == Death && data.value<DeathStruct>().who) data.value<DeathStruct>().who->removeTag("MishiReceipts");
        return false;
    }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != QuitDying) return true;
        ServerPlayer *survivor = data.value<DyingStruct>().who;
        if (!survivor || !survivor->isAlive()) return true;
        for (const QVariant &value : survivor->getTag("MishiReceipts").toList()) {
            const QVariantMap receipt = value.toMap();
            ServerPlayer *owner = room->findPlayerByObjectName(receipt.value("owner").toString(), true);
            if (!owner) continue;
            SkillContext ctx;
            ctx.skill_name = objectName(); ctx.owner = ctx.initiator = owner; ctx.invoker = survivor;
            ctx.instanceID = receipt.value("instance").toInt();
            ctx.sourceRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(receipt.value("skill").toString(), ctx.instanceID));
            ctx.extra_data = receipt; ctx.targets << survivor; ctx.original_data = &data; ctx.current_event = event; contexts << ctx;
        }
        return true;
    }
    ServerPlayer *triggerOrderPlayer(Room *, const SkillContext &ctx) const override { return ctx.invoker; }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return ctx.invoker && ctx.invoker->isAlive() && ctx.invoker->getTag("MishiReceipts").toList().contains(ctx.extra_data); }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        QVariantList receipts = ctx.invoker->getTag("MishiReceipts").toList(); receipts.removeOne(ctx.extra_data);
        ctx.invoker->setTag("MishiReceipts", receipts); return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        for (int i = 0; i < getEffectiveAmount(ctx) && owner->isAlive() && owner->canGet(target, "he"); ++i) {
            const int id = room->askForCardChosen(owner, target, "he", "mishi", false, Card::MethodGet);
            if (id < 0 || room->getCardOwner(id) != target || !owner->canGet(target, id)) break;
            room->obtainCard(owner, id, room->getCardPlace(id) != Player::PlaceHand);
        }
        return false;
    }
};

ZhufuCard::ZhufuCard()
{
    setSkillName("hayate_zhufu");
}

bool ZhufuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.length() < 4 && to_select != Self;
}

bool ZhufuCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    return targets.length() > 1 && targets.length() < 5;
}

void ZhufuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    source->loseAllMarks("@hayate_zhufu");
    int i = 0;
    while (!source->isKongcheng()){
        room->obtainCard(targets.at(i), room->askForCardChosen(targets.at(i), source, "h", "hayate_zhufu", true));
        i = (i == targets.count() - 1) ? 0 : i + 1;
    }
}

class ZhufuVS : public ViewAsSkillV2
{
public:
    ZhufuVS() : ViewAsSkillV2("hayate_zhufu") { setResponseOrUse(true); }
    LimitScope getLimitScope() const override { return Limit_Game; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }
    void project(ServerPlayer *owner) const
    {
        if (!owner) return;
        int available = 0;
        for (int id : owner->getSkillInstanceIds(objectName())) {
            SkillContext entry;
            entry.owner = entry.initiator = entry.invoker = owner;
            entry.instanceID = id;
            entry.sourceRef = entry.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(objectName(), id));
            if (isUsable(entry)) ++available;
        }
        // The public token describes remaining grants; it never authorizes a use.
        owner->getRoom()->setPlayerMark(owner, "@hayate_zhufu", available);
    }
    void addUsage(const SkillContext &ctx) const override
    { Skill::addUsage(ctx); project(ctx.initiator); }
    void resetUsage(const SkillContext &ctx) const override
    { Skill::resetUsage(ctx); project(ctx.initiator); }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && !request.initiator->isKongcheng() && request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
        && request.pattern == "@@hayate_zhufu"; }
    TargetMode targetMode() const override { return SelectTargets; }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *target) const override
    { return target && target->isAlive() && target != request.initiator && !selected.contains(target) && selected.size() < 4; }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &targets) const override
    {
        if (targets.size() < 2 || targets.size() > 4) return false;
        QList<const Player *> prefix;
        for (const Player *target : targets) { if (!canSelectTarget(request, prefix, target)) return false; prefix << target; }
        return true;
    }
    const Card *createCard(const ActiveSkillRequest &request) const override
    { return request.selectedCardIds.isEmpty() ? hayateProxy(this, request) : nullptr; }
    QString historyKey(const ActiveSkillRequest &) const override { return "ZhufuCard"; }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        if (!ctx.initiator) return FinishSkill;
        while (ctx.initiator->isAlive() && !ctx.initiator->isKongcheng()) {
            bool moved = false;
            for (ServerPlayer *target : targets) {
                if (!ctx.initiator->isAlive() || ctx.initiator->isKongcheng()) break;
                ctx.extra_data = false;
                skillEffect(ctx, target);
                moved |= ctx.extra_data.toBool();
            }
            if (!moved) break; // Dead or intercepted recipients must not trap the group in a busy loop.
        }
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.initiator->getRoom();
        const int id = room->askForCardChosen(target, ctx.initiator, "h", objectName(), true, Card::MethodNone);
        if (id < 0 || room->getCardOwner(id) != ctx.initiator || room->getCardPlace(id) != Player::PlaceHand) return ContinueEffects;
        room->obtainCard(target, id, false);
        ctx.extra_data = room->getCardOwner(id) != ctx.initiator || room->getCardPlace(id) != Player::PlaceHand;
        return ContinueEffects;
    }
};

class Zhufu : public TriggerSkillV2
{
public:
    Zhufu() : TriggerSkillV2("hayate_zhufu")
    { frequency = Limited; limit_mark = "@hayate_zhufu"; events << EventPhaseStart; view_as_skill = new ZhufuVS; }
    LimitScope getLimitScope() const override { return Limit_Game; }
    int getMaxUsageLimit(const SkillContext &) const override { return 1; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Draw
            && !player->isKongcheng() ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        const QString selector = ViewAsSkillV2::borrowedActivationMarkName(objectName());
        const int previous = owner->getMark(selector);
        auto restore = qScopeGuard([&] { room->setPlayerMark(owner, selector, previous); });
        room->setPlayerMark(owner, selector, ctx.activationRef.key.instanceID);
        room->askForUseCard(owner, "@@hayate_zhufu", "@hayate_zhufu-use", -1, Card::MethodNone);
        return false;
    }
};

class Vector : public TriggerSkillV2
{
public:
    Vector() : TriggerSkillV2("vector") { events << CardEffect; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && !player->isKongcheng()
            && player->canDiscard(player, "h") && effect.to == player && effect.card
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *target : room->getOtherPlayers(owner))
            if (!effect.from || !room->isProhibited(effect.from, target, effect.card)) candidates << target;
        if (candidates.isEmpty()) return false;
        const Card *card = room->askForCard(owner, ".|.|.|hand", QString("@vector-discard:%1::%2")
            .arg(effect.from ? effect.from->objectName() : "No Source", effect.card->objectName()),
            *ctx.original_data, Card::MethodNone, nullptr, false, objectName());
        if (!card || card->getEffectiveId() < 0) return false;
        ctx.extra_data = card->getEffectiveId();
        ctx.targets << room->askForPlayerChosen(owner, candidates, objectName(), QString("@vector-select:%1::%2")
            .arg(effect.from ? effect.from->objectName() : "No Source", effect.card->objectName()));
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        const int id = ctx.extra_data.toInt();
        if (room->getCardOwner(id) != owner || room->getCardPlace(id) != Player::PlaceHand || !owner->canDiscard(owner, id)) return false;
        room->throwCard(id, owner, owner);
        return room->getCardOwner(id) != owner || room->getCardPlace(id) != Player::PlaceHand;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardEffectStruct effect = ctx.original_data->value<CardEffectStruct>();
        if (effect.to != owner || !effect.card || (effect.from && room->isProhibited(effect.from, target, effect.card))) return false;
        // Redirect before CardEffected so the new recipient's ordinary defenses still run.
        effect.to = target;
        room->broadcastSkillInvoke(objectName()); room->doLightbox(objectName() + "$", 800);
        *ctx.original_data = QVariant::fromValue(effect);
        return false;
    }
};

class Juhe : public TriggerSkillV2
{
public:
    Juhe() : TriggerSkillV2("juhe") { events << EventPhaseChanging << Damaged << EventLoseSkill; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const override
    { if (event == EventLoseSkill && player) project(room, player); return false; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        if (event == EventPhaseChanging && data.value<PhaseChangeStruct>().to == Player::Play) return {{player, {objectName()}}};
        if (event == Damaged && data.value<DamageStruct>().card && data.value<DamageStruct>().damage > 0) return {{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (event == Damaged) { ctx.choice = "clear"; ctx.targets << owner; return true; }
        if (!owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        const int charge = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "charge").toInt();
        ctx.choice = owner->askForChoice(objectName(), charge > 0 ? "juhe_skip+juhe_lose" : "juhe_skip", *ctx.original_data);
        ctx.targets << (ctx.choice == "juhe_skip" ? owner : room->askForPlayerChosen(owner, room->getAlivePlayers(), objectName()));
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (ctx.choice != "juhe_lose") return true;
        const int charge = owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "charge").toInt();
        if (charge <= 0) return false;
        ctx.amount = charge;
        owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "charge", 0);
        project(room, owner);
        return true;
    }
    static void project(Room *room, ServerPlayer *owner)
    {
        int charge = 0;
        for (int id : owner->getSkillInstanceIds("juhe")) charge += owner->getSkillInstanceStateValue("juhe", id, "charge").toInt();
        room->setPlayerMark(owner, "@xuli", charge);
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "juhe_lose") room->damage(DamageStruct(objectName(), owner, target, getEffectiveAmount(ctx)));
        else if (target == owner) {
            const int charge = ctx.choice == "clear" ? 0
                : owner->getSkillInstanceStateValue(objectName(), ctx.instanceID, "charge").toInt() + getEffectiveAmount(ctx);
            if (ctx.choice == "juhe_skip") { owner->skip(Player::Play); room->broadcastSkillInvoke(objectName()); }
            owner->setSkillInstanceStateValue(objectName(), ctx.instanceID, "charge", charge);
            project(room, owner);
        }
        return false;
    }
};

class Qianggang : public TriggerSkillV2
{
public:
    Qianggang() : TriggerSkillV2("qianggang") { events << CardsMoveOneTime; global = true; setBaseAmount(3); }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        TriggerList result;
        if (!move.to || player != move.to || move.to_place != Player::PlaceHand || !move.from_places.contains(Player::DrawPile)
            || (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) != CardMoveReason::S_REASON_DRAW) return result;
        for (ServerPlayer *owner : room->findPlayersBySkillName(objectName()))
            if (owner != move.to) result.insert(owner, {objectName()});
        return result;
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        if (!owner->askForSkillInvoke(this, *ctx.original_data)) return false;
        int available = 0;
        for (int id : owner->handCards()) if (owner->canDiscard(owner, id)) ++available;
        QStringList choices;
        for (int n = 0; n <= qMin(2, available); ++n) choices << QString::number(n);
        const int count = room->askForChoice(owner, objectName(), choices.join('+'), *ctx.original_data).toInt();
        const QList<int> selected = count > 0 ? room->askForCardsChosen(owner, owner, "h", objectName(), count, count,
            false, Card::MethodDiscard, {}, false) : QList<int>();
        if (selected.size() != count) return false;
        QVariantList ids; for (int id : selected) ids << id;
        ctx.extra_data = QVariantMap{{"paid", ids}}; ctx.targets << owner;
        return true;
    }
    bool pay(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx) const override
    {
        QVariantMap receipt = ctx.extra_data.toMap();
        QList<int> ids; QVariantList colors;
        for (const QVariant &value : receipt.value("paid").toList()) {
            const int id = value.toInt();
            if (ids.contains(id) || room->getCardOwner(id) != owner || room->getCardPlace(id) != Player::PlaceHand
                || !owner->canDiscard(owner, id)) return false;
            ids << id; colors << int(Sanguosha->getCard(id)->getColor());
        }
        if (!ids.isEmpty()) room->throwCard(ids, objectName(), owner);
        for (int id : ids) if (room->getCardOwner(id) == owner && room->getCardPlace(id) == Player::PlaceHand) return false;
        receipt["colors"] = colors; ctx.extra_data = receipt;
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *owner, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target != owner) return false;
        const QVariantMap receipt = ctx.extra_data.toMap();
        const QVariantList paid = receipt.value("paid").toList();
        const QList<int> revealed = room->getNCards(qMax(0, getEffectiveAmount(ctx) - int(paid.size())), false);
        // Every removed draw-pile card is either obtained or explicitly discarded, including unwind.
        auto cleanup = qScopeGuard([&] {
            QList<int> stranded;
            for (int id : revealed) if (room->getCardPlace(id) == Player::PlaceTable) stranded << id;
            if (!stranded.isEmpty()) room->throwCard(stranded, objectName(), nullptr);
        });
        if (!revealed.isEmpty()) room->moveCardsAtomic(CardsMoveStruct(revealed, nullptr, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, owner->objectName(), objectName(), "")), true);
        QVariantList colors = receipt.value("colors").toList();
        for (int id : revealed) colors << int(Sanguosha->getCard(id)->getColor());
        if (colors.isEmpty()) return false;
        for (const QVariant &color : colors) if (color != colors.first()) return false;
        QList<int> obtainable;
        for (const QVariant &value : paid) if (room->getCardPlace(value.toInt()) == Player::DiscardPile) obtainable << value.toInt();
        for (int id : revealed) if (room->getCardPlace(id) == Player::PlaceTable) obtainable << id;
        if (!obtainable.isEmpty()) {
            DummyCard cards(obtainable);
            room->broadcastSkillInvoke(objectName()); room->obtainCard(target, &cards, true);
        }
        return false;
    }
};

HayatePackage::HayatePackage()
    : Package("hayate")
{
    General *hei = new General(this, "hayate_hei", "science", 3);
    skills << new Jiesha << new Gaokang << new Qiubang << new QiubangDis << new Youshui << new YoushuiReturn << new LonelinessInvalidity;
    hei->addSkill(new Yingdi);
    hei->addSkill(new YingdiClear);
    related_skills.insert("yingdi", "#yingdi-clear");
    hei->addSkill(new Diansuo);

    General * diarmuid = new General(this, "hayate_diarmuid", "magic", 4);
    diarmuid->addSkill(new Pomo);
    diarmuid->addSkill(new Bimie);
    diarmuid->addSkill(new BimieCurse);
    related_skills.insert("bimie", "#bimie-curse");

    General *tsukushi = new General(this, "hayate_tsukushi", "real", 3, false);
    tsukushi->addSkill(new Gangqu);
    tsukushi->addSkill(new GangquClear);
    related_skills.insert("gangqu", "#gangqu-clear");
    tsukushi->addSkill(new Tiaojiao);

    General *sheryl = new General(this, "hayate_sheryl", "diva", 3, false);
    sheryl->addSkill(new Yaojing);
    sheryl->addSkill(new YaojingMaxCards);
    sheryl->addSkill(new YaojingClear);
    sheryl->addSkill(new Gongming);
    related_skills.insert("yaojing", "#yaojing");
    related_skills.insert("yaojing", "#yaojing-clear");

    General *sugisaki = new General(this, "hayate_sugisaki", "real", 3);
    sugisaki->addSkill(new Kurimu);
    sugisaki->addSkill(new Minatsu);
    sugisaki->addSkill(new Chizuru);
    sugisaki->addSkill(new Mafuyu);
    sugisaki->addSkill(new MafuyuEffect);
    related_skills.insert("mafuyu", "#mafuyu-effect");
    sugisaki->addSkill(new Haremu);
    sugisaki->addSkill(new HaremuMaxCards);
    related_skills.insert("haremu", "#haremu");

    General *eugeo = new General(this, "hayate_eugeo", "science", 3);
    eugeo->addSkill(new Rennai);
    eugeo->addSkill(new Zhanfang);
    eugeo->addSkill(new ZhanfangNoRespond);
    related_skills.insert("zhanfang", "#zhanfang-no-respond");
    eugeo->addSkill(new Huajian);
    eugeo->addSkill(new HuajianReturn);
    related_skills.insert("huajian", "#huajian-return");

    General *k1 = new General(this, "hayate_k1", "real", 4);
    k1->addSkill(new Guiyin);
    k1->addSkill(new GuiyinDis);
    related_skills.insert("youshui", "#youshui-return");
    related_skills.insert("guiyin", "#guiyin");
    related_skills.insert("qiubang", "#qiubang");

    General *yuri = new General(this, "hayate_yuri", "real", 3, false);
    yuri->addSkill(new Zuozhan);
    yuri->addSkill(new ZuozhanOrder);
    related_skills.insert("zuozhan", "#zuozhan-order");
    yuri->addSkill(new Nishen);
    yuri->addSkill(new NishenClub);
    related_skills.insert("nishen", "#nishen-club");

    General *haruhi = new General(this, "hayate_haruhi", "real", 3, false);
    haruhi->addSkill(new Mengxian);
    haruhi->addSkill(new MengxianJudge);
    related_skills.insert("mengxian", "#mengxian-judge");
    haruhi->addSkill(new Yuanwang);
    haruhi->addSkill(new YuanwangEffects);
    related_skills.insertMulti("yuanwang", "#yuanwang-effects");

    General *hakaze = new General(this, "hayate_hakaze", "magic", 3, false);
    hakaze->addSkill(new Moju);
    hakaze->addSkill(new HayateJiejie);

    General *nagase = new General(this, "hayate_nagase", "real", 3, false);
    nagase->addSkill(new Qifen);
    nagase->addSkill(new Mishi);
    nagase->addSkill(new MishiReturn);
    related_skills.insert("mishi", "#mishi-return");
    nagase->addSkill(new Zhufu);

    General *acc = new General(this, "hayate_acc", "science", 3);
    acc->addSkill(new Vector);
    acc->addSkill(new Juhe);

    General *yumi = new General(this, "hayate_yumi", "real", 4, false, true);
    yumi->addSkill(new Qianggang);

    addMetaObject<TiaojiaoCard>();
    addMetaObject<HaremuCard>();
    addMetaObject<YoushuiCard>();
    addMetaObject<MojuCard>();
    addMetaObject<ZhufuCard>();
}

ADD_PACKAGE(Hayate)
