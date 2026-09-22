#include "original-hegemony-compat.h"

#include "engine.h"
#include "general.h"
#include "room.h"
#include "roomthread.h"
#include "serverplayer.h"
#include "settings.h"
#include "skill-instance-utils.h"

#include <QDebug>
#include <algorithm>

namespace {

class CostFlagGuard
{
public:
    CostFlagGuard(ServerPlayer *player, bool enabled)
        : m_player(player), m_added(enabled && player
              && !player->hasFlag("Global_askForSkillCost"))
    {
        if (m_added) m_player->setFlags("Global_askForSkillCost");
    }
    ~CostFlagGuard()
    {
        if (m_added) m_player->setFlags("-Global_askForSkillCost");
    }
private:
    ServerPlayer *m_player;
    bool m_added;
};

struct DonorCandidate
{
    const HegemonyTriggerSkill *skill = nullptr;
    ServerPlayer *askWho = nullptr;
    ServerPlayer *target = nullptr;
    SkillContext context;
    QString countKey;
    QString orderedTarget;
};

QString responseName(const SkillContext &ctx, const ServerPlayer *askWho)
{
    QString name = SkillInstanceUtils::formatName(ctx.skill_name, ctx.instanceID);
    if (ctx.owner && ctx.owner != askWho)
        name += ":" + ctx.owner->objectName();
    return name;
}

QList<SkillInstanceRef> sourceInstances(Room *room,
    const HegemonyTriggerSkill *skill, ServerPlayer *askWho,
    ServerPlayer *explicitOwner, int specifiedId)
{
    QList<SkillInstanceRef> sources;
    ServerPlayer *owner = explicitOwner ? explicitOwner : askWho;
    QString sourceName = skill->objectName();

    // Yongjue is an aura: the Slash user decides, while the first friendly
    // owner supplies the skill. Its donor cost chooses the same owner.
    if (!explicitOwner && sourceName == QLatin1String("heg_yongjue")) {
        owner = nullptr;
        for (ServerPlayer *candidate : room->findPlayersBySkillName(sourceName)) {
            if (askWho->isFriendWith(candidate)) {
                owner = candidate;
                break;
            }
        }
    }
    if (!owner) return sources;

    // Global helper definitions can be registered without a helper instance.
    // Attribute those to their real parent instead of inventing an instance.
    if (!owner->ownsSkill(sourceName) && skill->isGlobal()) {
        const Skill *parent = Sanguosha->getMainSkill(sourceName);
        if (parent && parent != skill && owner->ownsSkill(parent->objectName()))
            sourceName = parent->objectName();
    }
    for (int id : owner->getValidSkillInstanceIds(sourceName)) {
        if (specifiedId > 0 && id != specifiedId) continue;
        const SkillInstanceRef ref(owner->objectName(), SkillInstanceKey(sourceName, id));
        if (room->canShowGeneralForSkill(ref) && room->isSkillPreshownForTrigger(ref)) sources << ref;
    }
    return sources;
}

TriggerList collect(TriggerEvent event, Room *room, ServerPlayer *target,
    QVariant &data, const QList<const HegemonyTriggerSkill *> &skills)
{
    TriggerList result;
    for (const HegemonyTriggerSkill *skill : skills) {
        room->throwIfStopRequested();
        // These selectors also contain donor record side effects. Do not
        // pre-filter by target ownership, alive state, or nonempty results.
        const TriggerList selected = skill->triggerable(event, room, target, data);
        for (auto it = selected.cbegin(); it != selected.cend(); ++it)
            result[it.key()].append(it.value());
    }
    return result;
}

QList<DonorCandidate> candidatesFor(Room *room, ServerPlayer *eventTarget,
    ServerPlayer *askWho, const QStringList &selected, QVariant &data,
    TriggerEvent event, const QMap<QString, int> &consumed,
    const QMap<QString, QStringList> &consumedTargets,
    const General *removedGeneral)
{
    QList<DonorCandidate> result;
    QMap<QString, int> occurrences;
    for (const QString &token : selected) {
        QString name = token;
        ServerPlayer *explicitOwner = nullptr;
        const int ownerSeparator = name.indexOf('\'');
        if (ownerSeparator >= 0) {
            explicitOwner = room->findPlayerByObjectName(name.left(ownerSeparator), true);
            if (!explicitOwner) continue;
            name = name.mid(ownerSeparator + 1);
        }
        QStringList targets;
        const int targetSeparator = name.indexOf("->");
        if (targetSeparator >= 0) {
            targets = name.mid(targetSeparator + 2).split('+', Qt::SkipEmptyParts);
            name = name.left(targetSeparator);
        }
        QString baseName;
        const int specifiedId = SkillInstanceUtils::parseName(name, baseName);
        const auto *skill = dynamic_cast<const HegemonyTriggerSkill *>(
            Sanguosha->getTriggerSkill(baseName));
        if (!skill) {
            qWarning() << "Unresolved original hegemony trigger" << token;
            continue;
        }
        // GeneralRemoved is emitted after its source instances have retired.
        // A callback selected by that public removed definition belongs to the
        // removal event, never to a same-named surviving deputy/acquired copy.
        const bool retiredDefinition = removedGeneral && askWho == eventTarget
            && (!explicitOwner || explicitOwner == eventTarget) && specifiedId == 0
            && removedGeneral->hasSkill(baseName, true);
        QList<SkillInstanceRef> sources;
        if (retiredDefinition)
            sources << SkillInstanceRef();
        else
            sources = sourceInstances(room, skill, askWho, explicitOwner, specifiedId);
        if (sources.isEmpty()) {
            // Genuine global rules need no fabricated player-owned instance.
            // Owned-but-invalid or blocked sources must never use this path.
            const Skill *parent = Sanguosha->getMainSkill(baseName);
            if (skill->isEquipSkill()
                || dynamic_cast<const OriginalHegemonyDetachEffectSkill *>(skill)
                || (skill->isGlobal() && !askWho->ownsSkill(baseName)
                    && (!parent || parent == skill)))
                sources << SkillInstanceRef();
            else
                continue;
        }
        for (const SkillInstanceRef &ref : sources) {
            ServerPlayer *owner = ref.isValid()
                ? room->findPlayerByObjectName(ref.ownerObjectName, true) : askWho;
            const QString key = askWho->objectName() + '|' + baseName + '|'
                + ref.ownerObjectName + '|'
                + SkillInstanceUtils::formatName(ref.key.skillName, ref.key.instanceID);
            const bool ordered = targetSeparator >= 0;
            const int occurrence = ++occurrences[key];
            if (!ordered && occurrence <= consumed.value(key)) continue;
            int firstTarget = 0;
            if (ordered) {
                // Choosing a later target declines the earlier targets, just
                // as donor "skill->target&index" ordering does.
                for (const QString &used : consumedTargets.value(key)) {
                    const int index = targets.indexOf(used);
                    if (index >= 0) firstTarget = qMax(firstTarget, index + 1);
                }
            } else {
                targets = QStringList(QString());
            }
            for (int i = firstTarget; i < targets.size(); ++i) {
                DonorCandidate candidate;
                candidate.skill = skill;
                candidate.askWho = askWho;
                candidate.target = ordered
                    ? room->findPlayerByObjectName(targets.at(i), true)
                    : (explicitOwner ? explicitOwner : eventTarget);
                if (ordered && !candidate.target) continue;
                candidate.countKey = key;
                candidate.orderedTarget = ordered ? targets.at(i) : QString();
                SkillContext &ctx = candidate.context;
                ctx.skill_name = baseName;
                if (ordered)
                    ctx.skill_name += "->" + targets.at(i) + '&' + QString::number(i + 1);
                else if (explicitOwner)
                    ctx.skill_name = explicitOwner->objectName() + '\'' + baseName;
                ctx.owner = owner;
                ctx.invoker = askWho;
                ctx.initiator = eventTarget;
                ctx.instanceID = ref.key.instanceID;
                ctx.sourceRef = ref;
                ctx.activationRef = ref;
                ctx.original_data = &data;
                ctx.current_event = event;
                if (retiredDefinition)
                    ctx.extra_data = QVariantMap{{QStringLiteral("removed_general"),
                                                  removedGeneral->objectName()}};
                ctx.preferredTarget = candidate.target;
                if (candidate.target) {
                    ctx.targets << candidate.target;
                    ctx.preferredTargetSeat = candidate.target->getSeat();
                }
                result << candidate;
                const Skill::Frequency frequency = skill->getFrequency(askWho);
                if (ordered && askWho->hasShownSkill(skill)
                    && (frequency == Skill::Compulsory || frequency == Skill::Wake))
                    break;
            }
        }
    }
    return result;
}

bool sourceAvailable(Room *room, const SkillInstanceRef &ref)
{
    if (!ref.isValid()) return true;
    const ServerPlayer *owner = room->findPlayerByObjectName(ref.ownerObjectName, true);
    return owner && owner->hasSkillInstance(ref.key.skillName, ref.key.instanceID)
        && !owner->isSkillInvalid(ref.key.skillName, ref.key.instanceID)
        && room->canShowGeneralForSkill(ref) && room->isSkillPreshownForTrigger(ref);
}

class OriginalArraySummonSkill : public ZeroCardViewAsSkill
{
public:
    OriginalArraySummonSkill(const QString &name, const QString &type)
        : ZeroCardViewAsSkill(name), m_type(type) {}

    const Card *viewAs() const override
    {
        QString name = objectName().mid(QStringLiteral("heg_").size()); // Remaining legacy arrays keep their registered summon cards.
        if (name.isEmpty()) return nullptr;
        name[0] = name.at(0).toUpper();
        Card *card = Sanguosha->cloneSkillCard("H" + name + "Summon");
        if (card) card->setShowSkill(objectName());
        return card;
    }

    bool isEnabledAtPlay(const Player *player) const override
    {
        return canSummonOriginalHegemonyArray(player, objectName(), m_type);
    }

private:
    QString m_type;
};

} // namespace

bool canSummonOriginalHegemonyArray(const Player *player, const QString &skillName, const QString &arrayType)
{
    if (!player) return false;
    if (player->getAliveSiblings().size() < 3
        || player->hasFlag("Global_SummonFailed")) return false;
    bool canReveal = false;
    for (const SkillInstance &instance : player->getSkillInstances()) {
        if (instance.skillName != skillName
            || player->isSkillInvalid(instance.skillName, instance.instanceID)) continue;
        if (instance.visible || instance.bindHead == 0
            || player->canShowGeneral(instance.bindHead == 1 ? "h" : "d")) {
            canReveal = true;
            break;
        }
    }
    if (!canReveal) return false;
    if (arrayType == QLatin1String("Siege")) {
        if (player->willBeFriendWith(player->getNextAlive())
            && player->willBeFriendWith(player->getLastAlive())) return false;
        if (!player->willBeFriendWith(player->getNextAlive())
            && !player->getNextAlive(2)->hasShownOneGeneral()
            && player->getNextAlive()->hasShownOneGeneral()) return true;
        if (!player->willBeFriendWith(player->getLastAlive()))
            return !player->getLastAlive(2)->hasShownOneGeneral()
                && player->getLastAlive()->hasShownOneGeneral();
    } else if (arrayType == QLatin1String("Formation")) {
        int count = player->aliveCount(false);
        int asked = count;
        for (int i = 1; i < count; ++i) {
            const Player *target = player->getNextAlive(i);
            if (player->isFriendWith(target)) continue;
            if (!target->hasShownOneGeneral()) return true;
            asked = i;
            break;
        }
        count -= asked;
        for (int i = 1; i < count; ++i) {
            const Player *target = player->getLastAlive(i);
            if (player->isFriendWith(target)) continue;
            return !target->hasShownOneGeneral();
        }
    }
    return false;
}

HegemonyTriggerSkill::HegemonyTriggerSkill(const QString &name)
    : TriggerSkill(name)
{
}

int HegemonyTriggerSkill::getPriority() const { return 3; }
int HegemonyTriggerSkill::getPriority(TriggerEvent) const { return getPriority(); }

bool HegemonyTriggerSkill::triggerable(const ServerPlayer *target) const
{
    return target && target->isAlive() && target->hasSkill(objectName());
}

TriggerList HegemonyTriggerSkill::triggerable(TriggerEvent event, Room *room,
    ServerPlayer *target, QVariant &data) const
{
    ServerPlayer *askWho = target;
    const QStringList names = triggerable(event, room, target, data, askWho);
    TriggerList result;
    if (!names.isEmpty()) result.insert(askWho, names);
    return result;
}

QStringList HegemonyTriggerSkill::triggerable(TriggerEvent, Room *,
    ServerPlayer *target, QVariant &, ServerPlayer *&) const
{
    return triggerable(target) ? QStringList(objectName()) : QStringList();
}

bool HegemonyTriggerSkill::cost(TriggerEvent, Room *, ServerPlayer *,
    QVariant &, ServerPlayer *) const { return true; }

bool HegemonyTriggerSkill::effect(TriggerEvent, Room *, ServerPlayer *,
    QVariant &, ServerPlayer *) const { return false; }

bool HegemonyTriggerSkill::trigger(TriggerEvent event, Room *room,
    ServerPlayer *target, QVariant &data) const
{
    return dispatchOriginalHegemonySkills(event, room, target, data, {this});
}

OriginalHegemonyMasochismSkill::OriginalHegemonyMasochismSkill(const QString &name)
    : HegemonyTriggerSkill(name) { events << Damaged; }

bool OriginalHegemonyMasochismSkill::effect(TriggerEvent, Room *, ServerPlayer *target,
    QVariant &data, ServerPlayer *) const
{
    onDamaged(target, data.value<DamageStruct>());
    return false;
}

OriginalHegemonyPhaseChangeSkill::OriginalHegemonyPhaseChangeSkill(const QString &name)
    : HegemonyTriggerSkill(name) { events << EventPhaseStart; }

bool OriginalHegemonyPhaseChangeSkill::effect(TriggerEvent, Room *, ServerPlayer *target,
    QVariant &, ServerPlayer *) const { return onPhaseChange(target); }

OriginalHegemonyDrawCardsSkill::OriginalHegemonyDrawCardsSkill(const QString &name)
    : HegemonyTriggerSkill(name) { events << DrawNCards; }

bool OriginalHegemonyDrawCardsSkill::effect(TriggerEvent, Room *, ServerPlayer *target,
    QVariant &data, ServerPlayer *) const
{
    DrawStruct draw = data.value<DrawStruct>();
    draw.num = getDrawNum(target, draw.num);
    data = QVariant::fromValue(draw);
    return false;
}

OriginalHegemonyGameStartSkill::OriginalHegemonyGameStartSkill(const QString &name)
    : HegemonyTriggerSkill(name) { events << GameStart; }

bool OriginalHegemonyGameStartSkill::effect(TriggerEvent, Room *, ServerPlayer *target,
    QVariant &, ServerPlayer *) const
{
    onGameStart(target);
    return false;
}

OriginalHegemonyBattleArraySkill::OriginalHegemonyBattleArraySkill(
    const QString &name, const QString &arrayType)
    : HegemonyTriggerSkill(name), m_arrayType(arrayType)
{
    view_as_skill = new OriginalArraySummonSkill(name, arrayType);
}

bool OriginalHegemonyBattleArraySkill::triggerable(const ServerPlayer *target) const
{
    return HegemonyTriggerSkill::triggerable(target) && target->aliveCount() >= 4;
}

void OriginalHegemonyBattleArraySkill::summonFriends(ServerPlayer *player) const
{
    player->summonFriends(m_arrayType);
}

OriginalHegemonyDetachEffectSkill::OriginalHegemonyDetachEffectSkill(
    const QString &skillName, const QString &pileName)
    : HegemonyTriggerSkill(QString("#%1-clear").arg(skillName)),
      m_skillName(skillName), m_pileName(pileName)
{
    events << EventLoseSkill;
}

QStringList OriginalHegemonyDetachEffectSkill::triggerable(TriggerEvent, Room *,
    ServerPlayer *target, QVariant &data, ServerPlayer *&) const
{
    const QString lostName = data.canConvert<SkillChangeStruct>()
        ? data.value<SkillChangeStruct>().skillName : data.toString();
    // These donor effects clear shared piles/marks. A surviving instance still
    // owns that shared effect; only loss of the last instance cleans it up.
    return target && lostName == m_skillName && !target->ownsSkill(m_skillName)
        ? QStringList(objectName()) : QStringList();
}

bool OriginalHegemonyDetachEffectSkill::effect(TriggerEvent, Room *room,
    ServerPlayer *target, QVariant &, ServerPlayer *) const
{
    if (!m_pileName.isEmpty()) target->clearOnePrivatePile(m_pileName);
    else onSkillDetached(room, target);
    return false;
}

void OriginalHegemonyDetachEffectSkill::onSkillDetached(Room *, ServerPlayer *) const
{
    // Optional donor callback: the default implementation only clears a pile.
}

bool dispatchOriginalHegemonySkills(TriggerEvent event, Room *room,
    ServerPlayer *eventTarget, QVariant &data,
    const QList<const HegemonyTriggerSkill *> &samePriority)
{
    // The adapter preserves donor selector semantics in either game mode.
    // Reveal policy belongs to Room::showGeneralForSkill, not this dispatcher.
    if (samePriority.isEmpty()) return false;
    // Donor draw hooks were emitted only by the Draw phase. The current engine
    // emits a DrawStruct for every draw, including rewards and nested skill draws.
    if ((event == DrawNCards || event == AfterDrawNCards)
        && data.value<DrawStruct>().reason != QLatin1String("draw_phase")) return false;
    // Freeze only the event's public removal identity before donor selectors
    // run; callbacks continue sharing the original mutable QVariant itself.
    const QString removedName = event == GeneralRemoved ? data.toString() : QString();
    const General *removedGeneral = removedName.startsWith(QLatin1String("heg_"))
        ? Sanguosha->getGeneral(removedName) : nullptr;
    TriggerList pending = collect(event, room, eventTarget, data, samePriority);
    // Donor records tested definitions by prepending them, so subsequent
    // refreshes visit this priority group in reverse collection order.
    QList<const HegemonyTriggerSkill *> refreshOrder = samePriority;
    std::reverse(refreshOrder.begin(), refreshOrder.end());
    QMap<QString, int> consumed;
    QMap<QString, QStringList> consumedTargets;

    // Seat order belongs to decision makers. The owner and effect target are
    // carried independently and never substituted for askWho.
    for (ServerPlayer *askWho : room->getPlayers()) {
        for (;;) {
            room->throwIfStopRequested();
            if (event == EnterDying || event == Dying || event == AskForPeaches) {
                const ServerPlayer *dying = data.value<DyingStruct>().who;
                // A prior callback in this same priority group may finish the
                // rescue; preserve RoomThread's stop condition between skills.
                if (!dying || !dying->hasFlag("Global_Dying")) return false;
            }
            const QList<DonorCandidate> candidates = candidatesFor(room, eventTarget,
                askWho, pending.value(askWho), data, event, consumed, consumedTargets,
                removedGeneral);
            if (candidates.isEmpty()) break;
            bool compulsory = false;
            int automatic = -1;
            QList<SkillContext> contexts;
            for (int i = 0; i < candidates.size(); ++i) {
                const DonorCandidate &candidate = candidates.at(i);
                contexts << candidate.context;
                const Skill::Frequency frequency = candidate.skill->getFrequency(askWho);
                if (askWho->hasShownSkill(candidate.skill)
                    && (frequency == Skill::Compulsory || frequency == Skill::Wake))
                    compulsory = true;
                if (automatic < 0 && candidate.skill->isGlobal()
                    && frequency == Skill::Compulsory
                    && candidate.context.skill_name == candidate.skill->objectName())
                    automatic = i;
            }
            int selectedIndex = automatic;
            if (selectedIndex < 0) {
                CostFlagGuard flag(askWho, !askWho->hasShownAllGenerals());
                const QString answer = room->askForTriggerOrder(askWho,
                    "GameRule:TriggerOrder", contexts, !compulsory, data);
                if (answer.isEmpty() || answer == QLatin1String("cancel")) break;
                for (int i = 0; i < candidates.size(); ++i) {
                    if (responseName(candidates.at(i).context, askWho) == answer) {
                        selectedIndex = i;
                        break;
                    }
                }
                if (selectedIndex < 0) break;
            }

            const DonorCandidate candidate = candidates.at(selectedIndex);
            if (candidate.orderedTarget.isEmpty())
                ++consumed[candidate.countKey];
            else
                consumedTargets[candidate.countKey] << candidate.orderedTarget;
            const SkillInstanceRef ref = candidate.context.activationRef;
            if (sourceAvailable(room, ref)) {
                Room::ResolutionScope resolution(*room, candidate.skill->objectName());
                bool paid = false;
                {
                    CostFlagGuard flag(askWho, !askWho->hasShownSkill(candidate.skill));
                    paid = candidate.skill->cost(event, room, candidate.target, data, askWho);
                    if (paid && ref.isValid()) {
                        // Cost may remove/disable its own source; never reveal
                        // a surviving sibling instance as a fallback.
                        ServerPlayer *owner = room->findPlayerByObjectName(ref.ownerObjectName, true);
                        const bool wasAlive = owner && owner->isAlive();
                        paid = sourceAvailable(room, ref) && room->showGeneralForSkill(ref)
                            && (!wasAlive || owner->isAlive()) && sourceAvailable(room, ref);
                    }
                }
                if (paid && candidate.skill->effect(event, room, candidate.target, data, askWho))
                    return true;
            }

            // Recollect with the same shared QVariant after each paid or
            // declined cost. Consumed entries are filtered per exact source;
            // duplicate donor entries remain independent trigger repetitions.
            pending = collect(event, room, eventTarget, data, refreshOrder);
        }
    }
    return false;
}
