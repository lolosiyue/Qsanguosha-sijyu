#ifndef TOUHOU_UTILS_H
#define TOUHOU_UTILS_H

// Small helpers shared by the TouhouTripleSha ports (touhou-*.cpp).

#include "engine.h"
#include "room.h"
#include "serverplayer.h"
#include "skill.h"
#include "skill-instance-types.h"
#include "skill-instance-utils.h"

namespace TouhouUtils {

// A hand card the player may still commit (not already part of a pending use).
inline bool ownsHandCard(const Player *player, const Card *card)
{
    if (!player || !card || card->hasFlag("using")) return false;
    const int id = card->getEffectiveId();
    return id >= 0 && player->handCards().contains(id);
}

// Upstream Room::isSomeonesTurn(player).
inline bool isOwnTurn(const Player *player)
{
    return player && player->getPhase() != Player::NotActive;
}

// The card is still in the discard pile, so a skill may take it.
inline bool inDiscardPile(Room *room, const Card *card)
{
    return room && card && room->getCardPlace(card->getEffectiveId()) == Player::DiscardPile;
}

// Mark suffix the game rule clears when the current phase ends ("each phase once").
// Skill::Limit_Phase without a phase name uses "-PhaseClear", which nothing clears.
inline QString currentPhaseClearSuffix(Room *room)
{
    const ServerPlayer *current = room ? room->getCurrent() : nullptr;
    switch (current ? current->getPhase() : Player::NotActive) {
    case Player::RoundStart: return QStringLiteral("-RoundStartClear");
    case Player::Start: return QStringLiteral("-StartClear");
    case Player::Judge: return QStringLiteral("-JudgeClear");
    case Player::Draw: return QStringLiteral("-DrawClear");
    case Player::Play: return QStringLiteral("-PlayClear");
    case Player::Discard: return QStringLiteral("-DiscardClear");
    case Player::Finish: return QStringLiteral("-FinishClear");
    default: return QStringLiteral("-Clear");
    }
}

inline bool usedThisPhase(Room *room, const Player *player, const QString &skill)
{
    return player && player->getMark(skill + "_used" + currentPhaseClearSuffix(room)) > 0;
}

inline void markUsedThisPhase(Room *room, ServerPlayer *player, const QString &skill)
{
    if (room && player)
        room->addPlayerMark(player, skill + "_used" + currentPhaseClearSuffix(room));
}

// Keeps one `child` attached to every other living character for each instance of
// `parent` an owner holds (the Huangtian registry). Call it from recordEvent.
inline void syncAttachedChildren(Room *room, const QString &parent, const QString &child, bool lordOnly)
{
    QList<SkillInstanceRef> parents;
    foreach (ServerPlayer *owner, room->getAlivePlayers()) {
        if (lordOnly ? !owner->hasLordSkill(parent, true) : !owner->hasSkill(parent, true))
            continue;
        foreach (int id, owner->getSkillInstanceIds(parent))
            parents << SkillInstanceRef(owner->objectName(), SkillInstanceKey(parent, id));
    }
    foreach (ServerPlayer *donor, room->getAllPlayers(true)) {
        foreach (const SkillInstance &instance, donor->getSkillInstances()) {
            if (instance.skillName != child || instance.source != SourceAttached
                || instance.parentRef.key.skillName != parent)
                continue;
            if (!donor->isAlive() || !parents.contains(instance.parentRef)
                || instance.parentRef.ownerObjectName == donor->objectName())
                room->detachAttachedSkill(SkillInstanceRef(donor->objectName(), instance.key()));
        }
        if (!donor->isAlive())
            continue;
        foreach (const SkillInstanceRef &ref, parents)
            if (ref.ownerObjectName != donor->objectName())
                room->attachSkillToPlayer(donor, child, ref);
    }
}

// The living owner of the exact parent instance behind an attached activation.
inline const Player *attachedParentOwner(const ActiveSkillRequest &request, const QString &parent)
{
    if (!request.initiator)
        return nullptr;
    const SkillInstance *instance = request.initiator->findSkillInstance(request.activationRef.key.skillName,
                                                                        request.activationRef.key.instanceID);
    if (!instance || instance->parentRef.key.skillName != parent)
        return nullptr;
    foreach (const Player *p, request.initiator->getAliveSiblings())
        if (p->objectName() == instance->parentRef.ownerObjectName)
            return p;
    return nullptr;
}

// Upstream "专属技" (owner-only) skills: no skill may copy them. The engine has no
// such attribute, so the upstream list is kept here by name.
inline bool isOwnerOnlySkill(const QString &name)
{
    static const QSet<QString> names = {
        "ikhuoshou", "ikjingnie", "iklinglong", "iklingqi", "iklixin", "ikmoshan", "iksheluo", "ikyouji",
        "ikzuiyan", "thdaojian", "thdongxi", "thfanhun", "thfenlang", "thhouzhi", "thkuangmo", "thmicai",
        "thsanjie", "thshouye", "thsilian", "thsisui", "thtianbao", "thyinbi", "thyoushang", "thyuexiang",
    };
    return names.contains(name);
}

// canWake() consumes the grant and logs it; eligibility checks may only look.
inline bool hasWakeGrant(const Player *player, const QString &skill)
{
    return player && !player->getTag(skill + "_SKILLCANWAKE").toStringList().isEmpty();
}

// Drops the instance of `name` that a skill effect granted, leaving innate copies alone.
inline void detachAcquiredSkill(Room *room, ServerPlayer *player, const QString &name)
{
    if (!room || !player)
        return;
    for (int id : player->getSkillInstanceIds(name)) {
        const SkillInstance *instance = player->findSkillInstance(name, id);
        if (instance && instance->source == SourceAcquired) {
            room->detachSkillFromPlayer(player, SkillInstanceUtils::formatName(name, id), false, true, true);
            return;
        }
    }
}

// Grants `name` unless the player already has it, remembering the instance under the
// tag `key` so revokeTracked() removes exactly that grant.
inline void grantTracked(Room *room, ServerPlayer *player, const QString &key, const QString &name)
{
    if (!room || !player || player->hasSkill(name, true))
        return;
    QVariantMap map = player->getTag(key).toMap();
    if (map.contains(name))
        return;
    const int id = room->acquireSkill(player, name);
    if (id > 0) {
        map.insert(name, id);
        player->setTag(key, map);
    }
}

// Removes the grants recorded under `key`; all of them when `name` is empty.
inline void revokeTracked(Room *room, ServerPlayer *player, const QString &key, const QString &name = QString())
{
    if (!room || !player)
        return;
    QVariantMap map = player->getTag(key).toMap();
    foreach (const QString &skill, map.keys()) {
        if (!name.isEmpty() && skill != name)
            continue;
        const int id = map.take(skill).toInt();
        if (player->findSkillInstance(skill, id))
            room->detachSkillFromPlayer(player, SkillInstanceUtils::formatName(skill, id), false, true, true);
    }
    if (map.isEmpty())
        player->removeTag(key);
    else
        player->setTag(key, map);
}

// A skill whose upstream mechanic needs an engine decision first. It carries the
// name and description only; its translation says it is not implemented here.
class PendingSkill : public Skill
{
public:
    explicit PendingSkill(const QString &name) : Skill(name, NotFrequent) {}
};

// A wake skill: once per game per instance, at a timing the subclass names.
// Subclasses supply the timing, the natural condition and the awakening.
class WakeSkill : public TriggerSkillV2
{
public:
    explicit WakeSkill(const QString &name) : TriggerSkillV2(name)
    {
        events << EventSkillInvoking;
        frequency = Wake;
    }

    LimitScope getLimitScope() const override { return Limit_Game; }

    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        TriggerList result;
        if (event == EventSkillInvoking || !player)
            return result;
        for (ServerPlayer *owner : wakeOwners(event, room, player, data)) {
            if (!owner || !owner->isAlive() || !ownsWake(owner)
                || !(canAwaken(room, owner) || hasWakeGrant(owner, objectName())))
                continue;
            for (int id : owner->getValidSkillInstanceIds(objectName())) {
                SkillContext eligibility;
                eligibility.owner = owner;
                eligibility.invoker = owner;
                eligibility.skill_name = objectName();
                eligibility.instanceID = id;
                eligibility.activationRef = SkillInstanceRef(owner->objectName(), SkillInstanceKey(objectName(), id));
                if (isUsable(eligibility)) result[owner] << SkillInstanceUtils::formatName(objectName(), id);
            }
        }
        return result;
    }

    void record(TriggerEvent event, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event != EventSkillInvoking || !ctx.original_data) return;
        const SkillContext accepted = ctx.original_data->value<SkillContext>();
        if (accepted.activationRef == ctx.activationRef && accepted.bypass_cost) addUsage(accepted);
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { return isUsable(ctx); }

    bool pay(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!isUsable(ctx)) return false;
        addUsage(ctx);
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *player = ctx.owner;
        if (!room || !player || !player->isAlive()) return false;
        if (!canAwaken(room, player) && !player->canWake(objectName())) return false;
        room->sendCompulsoryTriggerLog(player, objectName());
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox(player, objectName());
        room->setPlayerMark(player, objectName(), 1);
        awaken(room, player, ctx);
        return false;
    }

protected:
    virtual bool ownsWake(const ServerPlayer *player) const
    {
        return isLordSkill() ? player->hasLordSkill(objectName()) : player->hasSkill(objectName());
    }
    // Characters whose wake this event may fire; by default the event's own player at its timing.
    virtual QList<ServerPlayer *> wakeOwners(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        return atTiming(event, room, player, data) ? QList<ServerPlayer *>{player} : QList<ServerPlayer *>();
    }
    virtual bool atTiming(TriggerEvent, Room *, ServerPlayer *, QVariant &) const { return false; }
    virtual bool canAwaken(Room *room, ServerPlayer *player) const = 0;
    virtual void awaken(Room *room, ServerPlayer *player, SkillContext &ctx) const = 0;
};

}

#endif
