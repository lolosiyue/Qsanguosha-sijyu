#include "ai-decision-coordinator.h"
#include "ai-runtime.h"
#include "card-lifetime-manager.h"

#include "engine.h"
#include "room.h"
#include "server-info.h"
#include "settings.h"
#include "skill-runtime-coordinator.h"
#include "standard.h"

#include <cmath>
#include <QJsonArray>
#include "ai-probe.h"
#include "skill-set-generation.h"
#include <QElapsedTimer>
#include <QLoggingCategory>

namespace {
// The event log is bounded so a long game cannot grow the snapshot without limit.
const int AiEventLogLimit = 64;


static QString aiMarkVisibilityKey(const ServerPlayer *owner, const QString &mark)
{
    return owner->objectName() + QString(QChar(0x1f)) + mark;
}

static const int AiStateMaxDepth = 8;
static const int AiStateMaxValues = 1024;
static const int AiStateMaxStringBytes = 64 * 1024;
static const qint64 AiStateMaxExactInteger = Q_INT64_C(9007199254740991);

static bool makeAIStateValue(const QVariant &source, QJsonValue &target,
                             int depth, int &remaining)
{
    if (depth > AiStateMaxDepth || remaining <= 0)
        return false;
    --remaining;
    if (!source.isValid() || source.isNull()) {
        target = QJsonValue::Null;
        return true;
    }

    switch (source.userType()) {
    case QMetaType::Bool:
        target = source.toBool();
        return true;
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::Short:
    case QMetaType::Int:
    case QMetaType::Long:
    case QMetaType::LongLong: {
        const qint64 value = source.toLongLong();
        if (value < -AiStateMaxExactInteger || value > AiStateMaxExactInteger)
            return false;
        target = double(value);
        return true;
    }
    case QMetaType::UChar:
    case QMetaType::UShort:
    case QMetaType::UInt:
    case QMetaType::ULong:
    case QMetaType::ULongLong: {
        const quint64 value = source.toULongLong();
        if (value > quint64(AiStateMaxExactInteger))
            return false;
        target = double(value);
        return true;
    }
    case QMetaType::Float:
    case QMetaType::Double: {
        const double value = source.toDouble();
        if (!std::isfinite(value))
            return false;
        target = value;
        return true;
    }
    case QMetaType::QString: {
        const QString value = source.toString();
        if (value.toUtf8().size() > AiStateMaxStringBytes)
            return false;
        target = value;
        return true;
    }
    case QMetaType::QStringList: {
        QJsonArray array;
        foreach (const QString &value, source.toStringList()) {
            if (remaining <= 0)
                break;
            QJsonValue item;
            if (makeAIStateValue(value, item, depth + 1, remaining))
                array.append(item);
        }
        target = array;
        return true;
    }
    case QMetaType::QVariantList: {
        QJsonArray array;
        foreach (const QVariant &value, source.toList()) {
            if (remaining <= 0)
                break;
            QJsonValue item;
            if (makeAIStateValue(value, item, depth + 1, remaining))
                array.append(item);
            else
                array.append(QJsonValue::Null);
        }
        target = array;
        return true;
    }
    case QMetaType::QVariantMap: {
        QJsonObject object;
        const QVariantMap values = source.toMap();
        for (auto value = values.constBegin(); value != values.constEnd(); ++value) {
            if (remaining <= 0)
                break;
            if (value.key().toUtf8().size() > AiStateMaxStringBytes)
                continue;
            QJsonValue item;
            if (makeAIStateValue(value.value(), item, depth + 1, remaining))
                object.insert(value.key(), item);
        }
        target = object;
        return true;
    }
    case QMetaType::QJsonArray:
        return makeAIStateValue(source.toJsonArray().toVariantList(), target,
                                depth, remaining);
    case QMetaType::QJsonObject:
        return makeAIStateValue(source.toJsonObject().toVariantMap(), target,
                                depth, remaining);
    default:
        return false;
    }
}

static QJsonObject makeAIStateObject(const QVariantMap &source)
{
    int remaining = AiStateMaxValues;
    QJsonValue value;
    if (!makeAIStateValue(source, value, 0, remaining) || !value.isObject())
        return QJsonObject();
    return value.toObject();
}

// Legal candidates for one card of the asking player. Everything here is computed once,
// on the authority, for this request only: the isolated side gets values, not a query
// channel back into the Engine.
// Who this card may be aimed at, and whether that list is the whole story. Physical
// candidates and authorized conversions are described by this one function: a card the
// authority converted is still a card, and giving it a second target algorithm is how
// the two would quietly drift apart.
static void describeCardTargets(Room &room, ServerPlayer *player, const Card *card,
                                bool &targetFixed, bool &feasibleWithNoTarget,
                                bool &completeCoverage, QStringList &legalTargets,
                                QMap<QString, int> &maxVotes,
                                QList<QStringList> &combinations, int &requestBudget)
{
    targetFixed = card->targetFixed();
    completeCoverage = false;
    legalTargets.clear();
    combinations.clear();
    // Operators/tests may lower the budget; the hard ceiling cannot be raised.
    int probesLeft = qBound(1, Config.value(QStringLiteral("AiTargetProjectionBudget"), 2048).toInt(), 2048);
    const auto spend = [&]() { return --probesLeft >= 0 && --requestBudget >= 0; };
    if (!spend()) return;
    feasibleWithNoTarget = card->targetsFeasible({}, player);
    if (targetFixed) {
        if (feasibleWithNoTarget) combinations << QStringList();
        completeCoverage = true;
        return;
    }
    const auto alive = room.getAlivePlayers();
    QList<const Player *> prefix;
    QStringList names;
    // Preserve order. Two individually legal first targets need not form a legal pair.
    // Current submit protocol rejects repeated names, so vote-repeating cards stay
    // unsupported instead of claiming their distinct-name subset is complete.
    std::function<bool()> visit = [&]() {
        if (!spend()) return false;
        if (card->targetsFeasible(prefix, player)) {
            if (combinations.size() >= 128) return false;
            combinations << names;
        }
        foreach (ServerPlayer *target, alive) {
            if (!spend()) return false;
            int votes = 0;
            card->targetFilter(prefix, target, player, votes);
            if (votes <= prefix.count(target)) continue;
            if (!spend()) return false;
            if (room.isProhibited(player, target, card, prefix)) continue;
            if (names.isEmpty()) {
                legalTargets << target->objectName();
                if (votes > 1) maxVotes.insert(target->objectName(), votes);
            }
            if (votes > 1 || prefix.contains(target) || prefix.size() >= 8) return false;
            prefix << target;
            names << target->objectName();
            if (!visit()) return false;
            prefix.removeLast();
            names.removeLast();
        }
        return true;
    };
    completeCoverage = visit();
    if (!completeCoverage) combinations.clear();
}

static AICardCandidateView makeAICardCandidate(Room &room, ServerPlayer *player,
                                               const Card *card, Card::HandlingMethod method,
                                               AIRequest::DecisionKind kind,
                                               const QString &pattern, int &projectionBudget)
{
    AICardCandidateView candidate;
    if (!player || !card)
        return candidate;
    candidate.cardId = card->getEffectiveId();
    // Play asks "may this card be played now"; a response asks "does this card answer
    // the pattern I was given". Using Card::isAvailable for both is what silently
    // hides every legal response, so the question decides which gate applies.
    if (kind == AIRequest::Activate) {
        candidate.available = card->isAvailable(player);
    } else {
        // The dispatcher keeps the trailing "!" of a compulsory pattern in its registry
        // key; the pattern itself never carries it.
        QString matched = pattern;
        if (matched.endsWith(QLatin1Char('!')))
            matched.chop(1);
        candidate.available = matched.isEmpty()
            || Sanguosha->matchPattern(matched, player, card);
    }
    candidate.limited = player->isCardLimited(card, method);
    candidate.jilei = player->isJilei(card);
    if (!candidate.available || candidate.limited) {
        // An unavailable card has a known empty action space; do not spend the
        // combinatorial target budget describing an action this request cannot use.
        candidate.completeCoverage = true;
        return candidate;
    }
    if (kind == AIRequest::RespondCard) {
        // askForCard/nullification/peach select a response, not play-phase targets.
        // A Slash response must not inherit Slash's play target requirement.
        candidate.targetFixed = true;
        candidate.feasibleWithNoTarget = true;
        candidate.completeCoverage = true;
        return candidate;
    }
    describeCardTargets(room, player, card, candidate.targetFixed,
                        candidate.feasibleWithNoTarget, candidate.completeCoverage,
                        candidate.legalTargets, candidate.maxVotes,
                        candidate.targetCombinations, projectionBudget);
    // Match the standard AOE/GlobalEffect::onUse auto-target contract without
    // executing a card or a trigger. Custom onUse implementations stay unknown.
    const QString className = card->getClassName();
    const bool standardAoe = className == QStringLiteral("SavageAssault")
        || className == QStringLiteral("ArcheryAttack");
    const bool standardGlobal = className == QStringLiteral("AmazingGrace")
        || className == QStringLiteral("GodSalvation");
    if (standardAoe || standardGlobal) {
        candidate.affectedTargetsKnown = true;
        const auto targets = standardAoe ? room.getOtherPlayers(player) : room.getAllPlayers();
        for (ServerPlayer *target : targets) {
            if (--projectionBudget < 0) {
                candidate.affectedTargetsKnown = false;
                candidate.affectedTargets.clear();
                break;
            }
            if (!room.isProhibited(player, target, card))
                candidate.affectedTargets << target->objectName();
        }
    }
    return candidate;
}


static AICardView makeAICardView(const Card *card)
{
    AICardView view;
    if (!card)
        return view;
    view.cardId = card->getId();
    view.effectiveId = card->getEffectiveId();
    view.objectName = card->objectName();
    view.className = card->getClassName();
    view.suit = int(card->getSuit());
    view.number = card->getNumber();
    view.skillName = card->getSkillName(false);
    view.red = card->isRed();
    view.black = card->isBlack();
    view.kindOfNames = card->getKindOfNames();
    view.typeId = int(card->getTypeId());
    if (const auto *equip = dynamic_cast<const EquipCard *>(card))
        view.equipSlot = int(equip->location());
    if (const auto *weapon = dynamic_cast<const Weapon *>(card))
        view.weaponRange = weapon->getRange();
    view.handlingMethod = int(card->getHandlingMethod());
    view.virtualCard = card->isVirtualCard();
    view.targetFixed = card->targetFixed();
    view.damageCard = card->isDamageCard();
    view.subcardIds = card->getSubcards();
    return view;
}

static QJsonObject publicDecisionCard(const Card *card)
{
    if (!card) return {};
    // A declared/judgement card is public; its backing IDs and subcards are not
    // needed by a policy and must never expose a hidden payment card.
    QJsonObject result{{"name", card->objectName()}, {"class_name", card->getClassName()},
        {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
        {"type_id", int(card->getTypeId())}, {"suit", int(card->getSuit())},
        {"damage_card", card->isDamageCard()},
        {"skill_name", card->getSkillName(false)},
        {"number", card->getNumber()}, {"red", card->isRed()}, {"black", card->isBlack()}};
    if (const auto *weapon = dynamic_cast<const Weapon *>(card))
        result.insert("weapon_range", weapon->getRange());
    return result;
}

}

AiDecisionCoordinator::AiDecisionCoordinator(Room &room,
                                             SkillRuntimeCoordinator &skillRuntime)
    : m_room(room), m_skillRuntime(skillRuntime)
{
}

bool AiDecisionCoordinator::isMarkVisibleTo(const ServerPlayer *owner, const QString &mark,
                                            const ServerPlayer *viewer) const
{
    if (!owner || owner == viewer)
        return true;
    const QString key = aiMarkVisibilityKey(owner, mark);
    if (!m_markViewers.contains(key))
        return false;
    const QSet<QString> viewers = m_markViewers.value(key);
    return viewers.isEmpty() || viewers.contains(viewer ? viewer->objectName() : QString());
}

void AiDecisionCoordinator::setMarkVisibility(const ServerPlayer *owner, const QString &mark,
                                              int value,
                                              const QList<ServerPlayer *> &viewers)
{
    const QString key = aiMarkVisibilityKey(owner, mark);
    if (value == 0) {
        m_markViewers.remove(key);
        return;
    }

    QSet<QString> viewerNames;
    foreach (ServerPlayer *viewer, viewers) {
        if (viewer)
            viewerNames.insert(viewer->objectName());
    }
    m_markViewers.insert(key, viewerNames);
}

void AiDecisionCoordinator::recordEvent(int triggerEvent, ServerPlayer *target,
                                        const QVariant &data)
{
    // One bounded, value-only log per Room. Nothing here keeps a pointer: the structs
    // are read once, while they are still valid, and only names and ids are kept.
    AIEventView event;
    event.sequence = ++m_eventSequence;
    event.revision = m_room.roomRuntime()->stateRevision();
    event.triggerEvent = triggerEvent;
    event.kind = QStringLiteral("other");
    if (target) {
        event.to = target->objectName();
        event.details.insert("player", target->objectName());
    }
    const auto describeCard = [&event](const Card *card) {
        if (!card) return;
        event.cardClass = card->getClassName();
        event.cardSkill = card->getSkillName(false);
        event.details.insert("card", publicDecisionCard(card));
        event.details.insert("card_skills", QJsonArray::fromStringList(card->getSkillNames()));
        // These flags describe the published action, not a player's private flags.
        event.intentionSuppressed = card->hasFlag("AIGlobal_ComboFallback")
            || card->hasFlag("meihuomoyan") || card->hasFlag("sgkgodshunshi")
            || card->hasFlag("kenewmieyao");
    };
    if (data.canConvert<DamageStruct>()) {
        const DamageStruct damage = data.value<DamageStruct>();
        event.kind = QStringLiteral("damage");
        event.from = damage.from ? damage.from->objectName() : QString();
        event.to = damage.to ? damage.to->objectName() : QString();
        event.amount = damage.damage;
        event.nature = int(damage.nature);
        event.reason = damage.reason;
        event.chain = damage.chain;
        event.transfer = damage.transfer;
        event.byUser = damage.by_user;
        event.details.insert("prevented", damage.prevented);
        describeCard(damage.card);
        if (damage.card) {
            event.cardName = damage.card->objectName();
            if (!damage.card->isVirtualCard())
                event.cardIds << damage.card->getEffectiveId();
        }
    } else if (data.canConvert<CardsMoveOneTimeStruct>()) {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        event.kind = QStringLiteral("card_move");
        event.from = move.from ? move.from->objectName() : QString();
        event.to = move.to ? move.to->objectName() : QString();
        event.place = int(move.to_place);
        event.amount = move.card_ids.size();
        // Cards that land in a hidden hand are known to their new owner only.
        const bool publicDestination = move.to_place != Player::PlaceHand
            && move.to_place != Player::PlaceSpecial;
        if (publicDestination) {
            event.cardIds = move.card_ids;
        } else {
            event.privateCardIds = move.card_ids;
            event.privateViewer = event.to;
        }
    } else if (data.canConvert<CardEffectStruct>()) {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        event.kind = QStringLiteral("card_effect");
        event.from = effect.from ? effect.from->objectName() : QString();
        event.to = effect.to ? effect.to->objectName() : QString();
        if (effect.card) {
            describeCard(effect.card);
            event.cardName = effect.card->objectName();
            if (!effect.card->isVirtualCard())
                event.cardIds << effect.card->getEffectiveId();
        }
    } else if (data.canConvert<JudgeStruct *>()) {
        const JudgeStruct *judge = data.value<JudgeStruct *>();
        if (judge) {
            event.kind = QStringLiteral("judge");
            event.to = judge->who ? judge->who->objectName() : QString();
            event.reason = judge->reason;
            event.good = judge->good;
            if (judge->card) {
                describeCard(judge->card);
                event.cardName = judge->card->objectName();
                event.cardIds << judge->card->getEffectiveId();
            }
        }
    } else if (data.canConvert<RecoverStruct>()) {
        const RecoverStruct recover = data.value<RecoverStruct>();
        event.kind = QStringLiteral("recover");
        event.from = recover.who ? recover.who->objectName() : QString();
        event.amount = recover.recover;
        event.reason = recover.reason;
        if (recover.card) event.cardName = recover.card->objectName();
        describeCard(recover.card);
    } else if (data.canConvert<DeathStruct>()) {
        const DeathStruct death = data.value<DeathStruct>();
        event.kind = QStringLiteral("death");
        event.to = death.who ? death.who->objectName() : QString();
        if (death.damage) {
            event.from = death.damage->from ? death.damage->from->objectName() : QString();
            event.reason = death.damage->reason;
            if (death.damage->card) event.cardName = death.damage->card->objectName();
            describeCard(death.damage->card);
        }
    } else if (data.canConvert<DyingStruct>()) {
        const DyingStruct dying = data.value<DyingStruct>();
        event.kind = QStringLiteral("dying");
        event.to = dying.who ? dying.who->objectName() : QString();
    } else if (data.canConvert<CardUseStruct>()) {
        const CardUseStruct use = data.value<CardUseStruct>();
        event.kind = QStringLiteral("card_use");
        event.from = use.from ? use.from->objectName() : QString();
        describeCard(use.card);
        foreach (ServerPlayer *player, use.to) {
            if (player)
                event.targets << player->objectName();
        }
        if (use.card) {
            event.cardName = use.card->objectName();
            if (!use.card->isVirtualCard())
                event.cardIds << use.card->getEffectiveId();
        }
    }
    if (triggerEvent == ChoiceMade && data.metaType().id() == QMetaType::QString) {
        // Admit public choices only. Serialized response/Yiji card IDs may be
        // hidden, so never copy the raw ChoiceMade string into a public event.
        const QStringList choice = data.toString().split(QChar(':'));
        const QString kind = choice.value(0);
        if (kind == QStringLiteral("playerChosen") || kind == QStringLiteral("Yiji")) {
            event.kind = QStringLiteral("choice");
            event.from = target ? target->objectName() : QString();
            event.reason = choice.value(1);
            event.details.insert("choice_kind", kind);
            for (const QString &name : choice.value(2).split(QChar('+'), Qt::SkipEmptyParts)) {
                if (m_room.findPlayerByObjectName(name, true)) event.targets << name;
            }
        } else if (kind == QStringLiteral("skillInvoke") || kind == QStringLiteral("skillChoice")) {
            event.kind = QStringLiteral("choice");
            event.from = target ? target->objectName() : QString();
            // This internal notification does not prove the answer was broadcast.
            event.privateEvent = true;
            event.privateViewer = event.from;
            event.reason = choice.value(1);
            event.details.insert("choice_kind", kind);
            event.details.insert("answer", choice.value(2));
        }
    }
    while (m_events.size() >= AiEventLogLimit)
        m_events.removeFirst();
    m_events << event;

    // Consume at the event boundary, never by replaying the truncated diagnostic
    // log. One canonical stage per action prevents duplicate intention updates.
    const bool canonical = triggerEvent == TargetSpecified || triggerEvent == DamageInflicted
        || triggerEvent == HpRecover || triggerEvent == Death
        || (triggerEvent == ChoiceMade && event.kind == QStringLiteral("choice"));
    if (!canonical || !Config.EnableAI || !m_room.roomRuntime()->ai().lua().rawState()) return;
    for (ServerPlayer *observer : m_room.getAlivePlayers()) {
        if (event.privateEvent && event.privateViewer != observer->objectName()) continue;
        // Keep each observer's mind ready for takeover, including human seats.
        // Each snapshot scans players and visible state once; no all-pairs
        // geometry or historical-event replay belongs in this path.
        AIWorldView world = buildWorldView(observer, true, true);
        AIEventView visible = event;
        if (visible.privateViewer != observer->objectName()) visible.privateCardIds.clear();
        visible.privateViewer.clear();
        QString error;
        if (!m_room.roomRuntime()->ai().processEvent(world, visible,
                                                     m_room.roomRuntime()->lua(), &error))
            qWarning().noquote() << "Isolated AI event rejected:" << event.sequence
                                  << observer->objectName() << error;
    }
}

AIWorldView AiDecisionCoordinator::buildWorldView(ServerPlayer *viewer, bool compactPolicy,
                                                 bool eventOnly) const
{
    EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
    // Native skill queries must use the same Room's gameplay callbacks.
    LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
    AIWorldView world;
    world.modeId = m_room.getMode();
    world.revision = m_room.roomRuntime()->stateRevision();
    if (!viewer || viewer->getRoom() != &m_room) return world;
    ServerPlayer *current = m_room.getCurrent();
    world.currentPlayer = current ? current->objectName() : QString();
    world.currentPhase = current ? int(current->getPhase()) : int(Player::NotActive);
    foreach (ServerPlayer *player, m_room.getPlayers())
        world.playerOrder << player->objectName();
    foreach (ServerPlayer *player, m_room.getAlivePlayers())
        world.alivePlayerOrder << player->objectName();

    const auto allPlayers = m_room.getAllPlayers(true);
    QHash<QString, ServerPlayer *> byName;
    QHash<QString, QString> controllerRoots;
    for (ServerPlayer *player : allPlayers)
        byName.insert(player->objectName(), player);
    // Resolve each control edge once. Cycles keep their entry identity, matching the
    // old read-only walk; resolving a snapshot must never repair gameplay state.
    for (ServerPlayer *player : allPlayers) {
        QString currentName = player->objectName();
        QStringList path;
        QHash<QString, int> positions;
        while (!controllerRoots.contains(currentName) && !positions.contains(currentName)) {
            positions.insert(currentName, path.size());
            path << currentName;
            const QString next = byName.value(currentName)->getTag("Controller_Name").toString();
            if (next.isEmpty() || !byName.contains(next)) {
                controllerRoots.insert(currentName, currentName);
                break;
            }
            currentName = next;
        }
        const QString root = controllerRoots.value(currentName, currentName);
        const int cycleStart = controllerRoots.contains(currentName) ? path.size()
            : positions.value(currentName);
        for (int index = 0; index < path.size(); ++index)
            controllerRoots.insert(path.at(index), index >= cycleStart ? path.at(index) : root);
    }
    const bool hegemony = ServerInfo.EnableHegemony;
    foreach (ServerPlayer *player, allPlayers) {
        AIPlayerView playerView;
        playerView.objectName = player->objectName();
        // Resolve control links without getActualController's repair/mutation path.
        playerView.controller = controllerRoots.value(player->objectName());
        playerView.seat = player->getSeat();
        playerView.hp = player->getHp();
        playerView.maxHp = player->getMaxHp();
        playerView.handcardCount = player->getHandcardNum();
        playerView.phase = int(player->getPhase());
        playerView.alive = player->isAlive();
        playerView.dead = player->isDead();
        playerView.removed = player->isRemoved();
        playerView.kongcheng = player->isKongcheng();
        playerView.wounded = player->isWounded();
        playerView.faceUp = player->faceUp();
        playerView.chained = player->isChained();
        playerView.maxCards = player->getMaxCards();
        playerView.hujia = player->getHujia();
        playerView.attackRange = player->getAttackRange();
        playerView.gender = int(player->getGender());
        playerView.lord = player->isLord();
        // Query the equipped armor only. A virtual armor skill needs its own
        // visible projection; do not clone hypothetical equipment for the AI.
        playerView.armorEffectKnown = (player == viewer || !hegemony)
            && player->property("View_As_Equips_List").toString().isEmpty();
        for (const SkillInstance &instance : player->getSkillInstances()) {
            if (Sanguosha->getViewAsEquipSkill(instance.skillName)
                && !player->isSkillInvalid(instance.skillName, instance.instanceID)) {
                playerView.armorEffectKnown = false;
                break;
            }
        }
        if (playerView.armorEffectKnown) {
            const EquipCard *armor = player->getArmor();
            // Mirror the physical branch of hasArmorEffect only. Its fallback
            // invokes viewAsEquip callbacks (even before checking skill validity)
            // and can clone virtual cards when the physical armor is nullified.
            if (armor && player->isAlive() && player->getMark("Armor_Nullified") <= 0
                && (player->getMark("IgnoreArea1") > 0 || player->hasEquipArea(1))
                && !player->isEquipsNullified(armor))
                playerView.activeArmorName = armor->objectName();
        }
        for (int slot = 0; slot < 5; ++slot) {
            if (const EquipCard *equip = player->getEquip(slot))
                playerView.equipSlots.insert(slot, equip->getEffectiveId());
        }

        const bool seesIdentity = player == viewer || !hegemony
            || player->hasShownOneGeneral() || player->isDead();
        if (seesIdentity)
            playerView.kingdom = hegemony && player == viewer && !player->hasShownOneGeneral()
                && player->getActualGeneral1() ? player->getHegemonyKingdom() : player->getKingdom();
        world.customRoles = world.customRoles || player->getRoleEnum() == Player::UnknownRole;
        playerView.roleRevealed = m_room.isRoleRevealed(player);
        playerView.roleVisible = m_room.canSeeRole(viewer, player);
        if (playerView.roleVisible)
            playerView.role = player->getRole();
        if (player == viewer || !hegemony || player->hasShownGeneral() || player->isDead())
            playerView.generalName = hegemony && player == viewer
                ? player->getActualGeneral1Name() : player->getGeneralName();
        if (player == viewer || !hegemony || player->hasShownGeneral2() || player->isDead())
            playerView.general2Name = hegemony && player == viewer
                ? player->getActualGeneral2Name() : player->getGeneral2Name();

        foreach (const Card *card, player->getEquips())
            playerView.equips << makeAICardView(card);
        foreach (const Card *card, player->getJudgingArea())
            playerView.judgingArea << makeAICardView(card);
        foreach (const QString &mark, player->getMarkNames()) {
            if (isMarkVisibleTo(player, mark, viewer))
                playerView.publicMarks.insert(mark, player->getMark(mark));
        }

        // Card zones. Only what this viewer may see crosses, and an open-but-empty zone
        // stays distinguishable from one that is simply not visible.
        playerView.handVisible = player == viewer || viewer->canSeeHandcard(player);
        if (player != viewer) {
            const QString visibleFlag = QStringLiteral("visible_%1_%2")
                .arg(viewer->objectName(), player->objectName());
            foreach (const Card *card, player->getHandcards()) {
                if (!card) continue;
                if (playerView.handVisible || card->hasFlag(QStringLiteral("visible"))
                    || card->hasFlag(visibleFlag))
                    playerView.knownCards << makeAICardView(card);
            }
        }
        foreach (const QString &pileName, player->getPileNames()) {
            AICardPileView pileView;
            pileView.name = pileName;
            const QList<int> pileCards = player->getPile(pileName);
            pileView.count = pileCards.size();
            pileView.handPile = pileName == QStringLiteral("wooden_ox")
                || pileName.startsWith(QChar('&'));
            pileView.open = player == viewer
                || player->pileOpen(pileName, viewer->objectName());
            if (pileView.open)
                pileView.cardIds = pileCards;
            playerView.piles << pileView;
        }
        // Display cards are shown to the table, so their ids are public.
        const QString displayProperty = player->property("display_cards").toString();
        if (!displayProperty.isEmpty()) {
            foreach (const QString &idText, displayProperty.split(QChar('+'))) {
                bool parsed = false;
                const int cardId = idText.toInt(&parsed);
                if (parsed && player->handCards().contains(cardId))
                    playerView.displayCards << cardId;
            }
        }

        foreach (const SkillInstance &instance, player->getSkillInstances()) {
            const Skill *skill = Sanguosha->getSkill(instance.skillName);
            if (!instance.visible || !skill || !skill->isVisible())
                continue;
            const bool visibleToViewer = !hegemony
                || SkillRuntimeCoordinator::canReceiveSkillInstance(m_room, viewer, player, instance);
            if (!visibleToViewer)
                continue;
            AISkillView skillView;
            skillView.skillName = instance.skillName;
            skillView.instanceId = instance.instanceID;
            skillView.source = int(instance.source);
            skillView.invalid = player->isSkillInvalid(instance.skillName, instance.instanceID);
            skillView.hasAmountOverride = instance.hasAmountOverride;
            skillView.amount = instance.hasAmountOverride ? instance.amountOverride : 0;
            skillView.hasPrivateState = player == viewer;
            if (skillView.hasPrivateState) {
                skillView.state = makeAIStateObject(player->getSkillInstanceState(
                    instance.skillName, instance.instanceID));
            }
            for (const QMetaObject *meta = skill->metaObject(); meta; meta = meta->superClass())
                skillView.skillClasses << QString::fromLatin1(meta->className());
            skillView.frequency = int(skill->getFrequency(player));
            skillView.lordSkill = skill->isLordSkill();
            skillView.attachedLordSkill = skill->isAttachedLordSkill();
            // A lord skill only works while its owner is the lord of a played kingdom.
            skillView.lordSkillEffective = skill->isLordSkill()
                && player->hasLordSkill(skill->objectName());
            skillView.correctState = makeAIStateObject(instance.correctState);
            playerView.skills << skillView;
        }

        if (player == viewer) {
            playerView.privateFlagsVisible = true;
            playerView.privateFlags = player->getFlagList();
            for (int phase = int(Player::RoundStart); phase <= int(Player::Finish); ++phase)
                playerView.skippedPhases.insert(phase, player->isSkipped(Player::Phase(phase)));
            world.self = playerView;
            foreach (const Card *card, player->getHandcards())
                world.handCards << makeAICardView(card);
        } else {
            world.players << playerView;
        }
    }
    const auto distancePlayers = m_room.getAlivePlayers();
    const auto distanceSkills = Sanguosha->getDistanceSkills();
    const quint64 distanceRevision = m_room.roomRuntime()->stateRevision();
    const quint64 skillGeneration = SkillSet::generation();
    if (eventOnly) {
        world.distanceScope = QStringLiteral("none");
    } else if (compactPolicy) {
        // Isolated decisions need viewer-relative geometry. Keep both directions:
        // distance modifiers can be asymmetric. Other pairs remain explicitly absent.
        world.distanceScope = QStringLiteral("viewer");
        QMap<QString, int> outgoing;
        for (ServerPlayer *other : distancePlayers) {
            if (other == viewer) continue;
            outgoing.insert(other->objectName(), viewer->distanceTo(other));
            world.distances.insert(other->objectName(),
                QMap<QString, int>{{viewer->objectName(), other->distanceTo(viewer)}});
        }
        world.distances.insert(viewer->objectName(), outgoing);
    } else if (m_distanceCacheValid && m_distanceRevision == distanceRevision
        && m_distanceSkillGeneration == skillGeneration
        && m_distancePlayers == distancePlayers && m_distanceSkills == distanceSkills) {
        world.distances = m_worldDistances;
    } else {
        foreach (ServerPlayer *from, distancePlayers) {
            QMap<QString, int> row;
            foreach (ServerPlayer *to, distancePlayers) {
                if (from != to)
                    row.insert(to->objectName(), from->distanceTo(to));
            }
            world.distances.insert(from->objectName(), row);
        }
        // A callback that mutates gameplay must never publish a reusable stale table.
        m_distanceCacheValid = distanceRevision == m_room.roomRuntime()->stateRevision()
            && skillGeneration == SkillSet::generation();
        if (m_distanceCacheValid) {
            m_distanceRevision = distanceRevision;
            m_distanceSkillGeneration = skillGeneration;
            m_distancePlayers = distancePlayers;
            m_distanceSkills = distanceSkills;
            m_worldDistances = world.distances;
        }
    }
    if (!eventOnly) foreach (const int cardId, m_room.getDiscardPile()) {
        if (const Card *card = Sanguosha->getCard(cardId))
            world.discardPile << makeAICardView(card);
    }
    if (!eventOnly) foreach (const AIEventView &event, m_events) {
        if (event.privateEvent && event.privateViewer != viewer->objectName()) continue;
        AIEventView visible = event;
        if (visible.privateViewer != viewer->objectName())
            visible.privateCardIds.clear();
        visible.privateViewer.clear();
        world.events << visible;
    }
    AiLuaRuntime::evaluateModePolicy(m_room.roomRuntime()->lua(), world, compactPolicy);
    return world;
}

AIRequest AiDecisionCoordinator::makeRequest(ServerPlayer *player,
                                             AIRequest::DecisionKind kind,
                                             CardUseStruct::CardUseReason reason,
                                             const QString &pattern,
                                             const QString &prompt,
                                             Card::HandlingMethod method) const
{
    AIRequest request;
    request.kind = kind;
    request.decisionId = m_room.roomRuntime()->nextDecisionId();
    request.stateRevision = m_room.roomRuntime()->stateRevision();
    request.viewerObjectName = player ? player->objectName() : QString();
    request.reason = reason;
    request.pattern = pattern;
    request.prompt = prompt;
    request.handlingMethod = method;
    // --ai off selects the native fallback. It must not build the quadratic
    // isolated snapshot or allow an isolated Lua handler to override that AI.
    if (!Config.EnableAI)
        return request;
    request.worldView = buildWorldView(player, true);
    if (player && (kind == AIRequest::Activate || kind == AIRequest::UseCard
                   || kind == AIRequest::RespondCard)) {
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
        foreach (const SkillInstance &instance, player->getSkillInstances()) {
            AiSkillActionContext actionContext;
            if (buildSkillActionContext(player, instance, reason, pattern, actionContext))
                request.skillActions << actionContext;
        }
        int projectionBudget = 16384;
        foreach (const Card *card, player->getHandcards())
            request.cardCandidates << makeAICardCandidate(m_room, player, card, method,
                                                          kind, pattern, projectionBudget);
        foreach (const Card *card, player->getEquips())
            request.cardCandidates << makeAICardCandidate(m_room, player, card, method,
                                                          kind, pattern, projectionBudget);
        // The authorization tickets are handed out last, so every candidate this
        // request offered carries one and nothing outside the list can claim one.
        for (int index = 0; index < request.cardCandidates.size(); ++index)
            request.cardCandidates[index].candidateId = index + 1;
        buildCardConversions(player, request, projectionBudget);
    }
    return request;
}

// Conversions this request authorizes. Only skills the player actually holds are
// asked, only the skill's own createCard() names the produced card, and the cost is
// whatever the skill accepted - never what the AI will later claim it paid.
//
// Zero/one-card costs are concrete tickets. Wider costs require the explicit
// independent-selection contract and become one parameterized ticket per instance;
// never enumerate hand subsets. Unknown skill families leave coverage incomplete.
bool AiDecisionCoordinator::buildCardConversions(ServerPlayer *player,
                                                 AIRequest &request, int &projectionBudget) const
{
    request.cardConversions.clear();
    request.conversionsEnumerated = false;
    if (!player) return false;
    EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
    LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
    bool enumerated = true;
    int probesLeft = 512;

    // V1 view-as skills are not represented by skillActions. Their absence from
    // that list cannot prove that no conversion exists for this response/play.
    for (const SkillInstance &instance : player->getSkillInstances()) {
        const ViewAsSkill *skill = Sanguosha->getViewAsSkill(instance.skillName);
        if (skill && !dynamic_cast<const ViewAsSkillV2 *>(skill)
            && !player->isSkillInvalid(instance.skillName, instance.instanceID)
            // V1 response availability has separate nullification hooks and legacy
            // pattern conventions. Treat it as unknown without duplicating them.
            && (request.reason != CardUseStruct::CARD_USE_REASON_PLAY
                || skill->isAvailable(player, request.reason, request.pattern)))
            enumerated = false;
    }

    QList<int> payable = player->handCards();
    foreach (const Card *equip, player->getEquips())
        if (equip) payable << equip->getEffectiveId();

    foreach (const AiSkillActionContext &action, request.skillActions) {
        const ViewAsSkillV2 *skill = dynamic_cast<const ViewAsSkillV2 *>(
            Sanguosha->getViewAsSkill(action.getActivationSkillName()));
        // A skill this side cannot ask is a skill whose conversions are unknown, not
        // one that has none.
        if (!skill || !player->hasSkillInstance(action.getActivationSkillName(),
                                                action.getActivationInstanceId())) {
            enumerated = false;
            continue;
        }
        const int cost = skill->getN();
        const bool parameterized = cost >= 2 && skill->hasIndependentAIConversion();
        if (cost < 0 || cost > 8 || (cost > 1 && !parameterized)) {
            enumerated = false;
            continue;
        }
        QList<QList<int> > selections;
        QList<int> eligible;
        if (cost == 0) {
            selections << QList<int>();
        } else {
            foreach (int cardId, payable) {
                if (--probesLeft < 0 || --projectionBudget < 0) { enumerated = false; break; }
                if (parameterized && !player->handCards().contains(cardId)) continue;
                const Card *payment = Sanguosha->getCard(cardId);
                if (!payment) continue;
                ActiveSkillRequest probe;
                probe.reason = request.reason;
                probe.pattern = request.pattern;
                probe.initiator = player;
                probe.activationRef = action.activationRef;
                if (!skill->canSelectCard(probe, payment)) continue;
                if (parameterized) eligible << cardId;
                else selections << (QList<int>() << cardId);
            }
        }
        if (parameterized) {
            // The explicit invariant contract authorizes all distinct choices; build
            // only one representative to describe the cost-independent output.
            if (probesLeft < 0 || projectionBudget < 0) continue;
            if (eligible.size() < cost) continue;
            selections << eligible.mid(0, cost);
        }
        foreach (const QList<int> &selection, selections) {
            if (request.cardConversions.size() >= 64) { enumerated = false; break; }
            ActiveSkillRequest built;
            built.reason = request.reason;
            built.pattern = request.pattern;
            built.initiator = player;
            built.activationRef = action.activationRef;
            built.selectedCardIds = selection;
            if (!skill->cardSelectionFeasible(built)) continue;
            const Card *produced = skill->createCard(built);
            if (!produced) { enumerated = false; continue; }
            // A skill that produces a bare proxy is a skill action, not a card
            // conversion: it is already carried by the skill action path and its
            // selected cards. Skipping it is classification, not a coverage gap.
            if (produced->objectName().isEmpty()) {
                const_cast<Card *>(produced)->deleteLater();
                continue;
            }
            AICardConversionView view;
            view.conversionId = request.cardConversions.size() + 1;
            view.name = produced->objectName();
            view.className = produced->getClassName();
            view.kindOfNames = produced->getKindOfNames();
            view.suit = int(produced->getSuit());
            view.number = produced->getNumber();
            view.activationRef = action.activationRef;
            view.sourceRef = action.sourceRef;
            view.activationQuotaAvailable = action.activationQuotaAvailable;
            view.sourceQuotaAvailable = action.sourceQuotaAvailable;
            view.subcardIds = selection;
            view.costCount = parameterized ? cost : 0;
            view.eligibleSubcardIds = eligible;
            const AICardCandidateView candidate = makeAICardCandidate(m_room, player,
                produced, request.handlingMethod, request.kind, request.pattern, projectionBudget);
            view.available = candidate.available && !candidate.limited;
            if (!view.available) { const_cast<Card *>(produced)->deleteLater(); continue; }
            view.targetFixed = candidate.targetFixed;
            view.feasibleWithNoTarget = candidate.feasibleWithNoTarget;
            view.completeCoverage = candidate.completeCoverage;
            view.legalTargets = candidate.legalTargets;
            view.maxVotes = candidate.maxVotes;
            view.targetCombinations = candidate.targetCombinations;
            view.affectedTargetsKnown = candidate.affectedTargetsKnown;
            view.affectedTargets = candidate.affectedTargets;
            // The projection is a value. Nothing native survives this function, so a
            // conversion can never be a handle the AI holds onto.
            const_cast<Card *>(produced)->deleteLater();
            request.cardConversions << view;
        }
    }
    request.conversionsEnumerated = enumerated;
    return enumerated;
}

bool AiDecisionCoordinator::buildSkillActionContext(
    ServerPlayer *player, const SkillInstance &instance,
    CardUseStruct::CardUseReason reason, const QString &pattern,
    AiSkillActionContext &actionContext) const
{
    if (!player || !player->hasSkillInstance(instance.skillName, instance.instanceID)) return false;
    const ViewAsSkillV2 *skill = dynamic_cast<const ViewAsSkillV2 *>(
        Sanguosha->getViewAsSkill(instance.skillName));
    if (!skill) return false;

    ActiveSkillRequest request;
    request.reason = reason;
    request.pattern = pattern;
    request.initiator = player;
    request.activationRef = SkillInstanceRef(player->objectName(), instance.key());
    if (!skill->canActivate(request)) return false;

    SkillContext context;
    context.initiator = player;
    context.invoker = player;
    context.owner = player;
    context.activationRef = request.activationRef;
    context.sourceRef = m_skillRuntime.resolveSkillInstanceRootRef(request.activationRef);
    if (!context.sourceRef.isValid()) return false;
    context.instanceID = instance.instanceID;
    bool amountOk = false;
    context.amount = m_skillRuntime.getSkillInstanceAmount(skill->getAmountRef(context),
                                                           &amountOk);
    if (!amountOk) context.amount = skill->getBaseAmount();
    const bool quotaAvailable = skill->isUsable(context);
    if (!quotaAvailable) return false;

    actionContext.activationRef = request.activationRef;
    actionContext.sourceRef = context.sourceRef;
    actionContext.activationQuotaAvailable = quotaAvailable;
    actionContext.sourceQuotaAvailable = quotaAvailable;
    return true;
}

bool AiDecisionCoordinator::buildSkillActionRequest(
    ServerPlayer *player, const SkillInstance &instance,
    CardUseStruct::CardUseReason reason, const QString &pattern,
    const QString &prompt, Card::HandlingMethod method, AIRequest &aiRequest) const
{
    AiSkillActionContext actionContext;
    if (!buildSkillActionContext(player, instance, reason, pattern, actionContext))
        return false;
    aiRequest = makeRequest(player, AIRequest::UseCard, reason, pattern, prompt, method);
    aiRequest.hasSkillActionContext = true;
    aiRequest.skillActionContext = actionContext;
    return true;
}

Card *AiDecisionCoordinator::buildSpecCard(ServerPlayer *player, const AIRequest &request,
                                           const AICardSpec &spec) const
{
    // Authorization comes from one place: a conversion ticket this very request issued.
    //
    // It used to come from the spec itself, and three forgeries went through. Any
    // globally registered view-as skill name passed, whether or not the player held it;
    // any engine card name was cloned, whatever the named skill actually produces; and
    // the skill was addressed by bare name, so neither the instance nor its quota was
    // ever checked. A name is a request. It is not a permission.
    if (!player || !spec.isValid()) return nullptr;
    const AICardConversionView *authorized = nullptr;
    foreach (const AICardConversionView &conversion, request.cardConversions) {
        if (conversion.conversionId == spec.conversionId) {
            authorized = &conversion;
            break;
        }
    }
    if (!authorized || !authorized->available || !authorized->activationQuotaAvailable
        || !authorized->sourceQuotaAvailable) return nullptr;

    // Everything the author wrote is compared against the authority's own record. A
    // disagreement is a forged name, suit, number, skill or cost - never something to
    // reconcile in the author's favour. An unset suit or number means "as issued".
    if (spec.name != authorized->name) return nullptr;
    if (spec.suit != int(Card::SuitToBeDecided) && spec.suit != authorized->suit)
        return nullptr;
    if (spec.number != 0 && spec.number != authorized->number) return nullptr;
    if (!spec.skillName.isEmpty()
        && spec.skillName != authorized->activationRef.key.skillName)
        return nullptr;
    if (authorized->costCount == 0) {
        if (spec.subcardIds != authorized->subcardIds) return nullptr;
    } else {
        if (spec.subcardIds.size() != authorized->costCount) return nullptr;
        QSet<int> unique;
        foreach (int id, spec.subcardIds) {
            if (unique.contains(id) || !authorized->eligibleSubcardIds.contains(id)
                || !player->handCards().contains(id)) return nullptr;
            unique.insert(id);
        }
    }

    // The ticket was issued when the request was built; the answer arrives after the AI
    // has had its turn to think, so the instance and its quota are checked again here.
    EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
    LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
    const ViewAsSkillV2 *skill = dynamic_cast<const ViewAsSkillV2 *>(
        Sanguosha->getViewAsSkill(authorized->activationRef.key.skillName));
    if (!skill) return nullptr;
    if (!player->hasSkillInstance(authorized->activationRef.key.skillName,
                                  authorized->activationRef.key.instanceID))
        return nullptr;
    ActiveSkillRequest activation;
    activation.reason = request.reason;
    activation.pattern = request.pattern;
    activation.initiator = player;
    activation.activationRef = authorized->activationRef;
    if (!skill->canActivate(activation)) return nullptr;
    if (authorized->costCount > 0 && !skill->hasIndependentAIConversion()) return nullptr;
    // Revalidate each prefix, not just the final number of cards.
    foreach (int id, spec.subcardIds) {
        const Card *payment = Sanguosha->getCard(id);
        if (!payment || !skill->canSelectCard(activation, payment)) return nullptr;
        activation.selectedCardIds << id;
    }
    if (!skill->cardSelectionFeasible(activation)) return nullptr;

    SkillContext context;
    context.initiator = player;
    context.invoker = player;
    context.owner = player;
    context.activationRef = authorized->activationRef;
    context.sourceRef = m_skillRuntime.resolveSkillInstanceRootRef(authorized->activationRef);
    // A borrowed entry must still resolve to the very root it was issued against,
    // because that root is what pays.
    if (!context.sourceRef.isValid() || context.sourceRef != authorized->sourceRef)
        return nullptr;
    context.instanceID = authorized->activationRef.key.instanceID;
    bool amountOk = false;
    context.amount = m_skillRuntime.getSkillInstanceAmount(skill->getAmountRef(context),
                                                           &amountOk);
    if (!amountOk) context.amount = skill->getBaseAmount();
    // Quota is spent against the source, so an exhausted root refuses every borrowed
    // entry sharing it, not only the entry that spent it.
    if (!skill->isUsable(context)) return nullptr;

    // The authority builds the card, from the skill, again. The spec was only ever used
    // to pick which authorized conversion the AI meant.
    const Card *produced = skill->createCard(activation);
    if (!produced) return nullptr;
    if (produced->objectName() != authorized->name
        || int(produced->getSuit()) != authorized->suit
        || produced->getNumber() != authorized->number) {
        const_cast<Card *>(produced)->deleteLater();
        return nullptr;
    }
    Card *card = const_cast<Card *>(produced);
    // Identity is common to play and response. A serialized skill name never
    // chooses the instance or the borrowed root that pays its quota.
    card->setActivationSkill(authorized->activationRef.key.skillName,
                              authorized->activationRef.key.instanceID);
    card->setSourceSkill(authorized->sourceRef.key.skillName,
                          authorized->sourceRef.key.instanceID);
    return card;
}

bool AiDecisionCoordinator::applyResult(ServerPlayer *player, const AIRequest &request,
                                        const AIResult &result,
                                        CardUseStruct &cardUse) const
{
    if (!player || !result.handled || !result.errorCode.isEmpty()
        || result.decisionId != request.decisionId
        || result.stateRevision != request.stateRevision)
        return false;
    if (request.stateRevision != m_room.roomRuntime()->stateRevision()) return false;

    CardUseStruct candidate = cardUse;
    candidate.from = player;
    candidate.card = nullptr;
    candidate.to.clear();
    // Every target below was chosen by an AI, so Room::useCard must re-run the ordered
    // targetFilter/targetsFeasible/prohibit contract on it. CardUseStruct::parse sets
    // this for the legacy string answer; the value-typed card_id answer had no such
    // path, so an out-of-range or prohibited target reached gameplay unchecked.
    candidate.m_validateTargets = true;
    if (result.kind == AIResult::Pass) {
        cardUse = candidate;
        return true;
    }

    QSet<QString> targetNames;
    foreach (const QString &targetName, result.action.selectedTargetNames) {
        if (targetNames.contains(targetName)) return false;
        ServerPlayer *target = m_room.findPlayerByObjectName(targetName);
        if (!target) return false;
        targetNames.insert(targetName);
        candidate.to << target;
    }

    if (result.action.useCardId >= 0) {
        // Playing one concrete card: the player must actually hold it, and the card
        // itself comes from the Engine, not from anything the AI built.
        const int useCardId = result.action.useCardId;
        bool owned = player->handCards().contains(useCardId);
        if (!owned) {
            foreach (const Card *equip, player->getEquips()) {
                if (equip && equip->getEffectiveId() == useCardId) {
                    owned = true;
                    break;
                }
            }
        }
        const Card *played = owned ? Sanguosha->getCard(useCardId) : nullptr;
        if (!played) return false;
        // Owning the card is not the same as having been offered it for this question.
        // A request that never listed this card as a candidate never authorized it, so
        // a candidate list that does carry one must be matched against.
        if (!request.cardCandidates.isEmpty()) {
            bool offered = false;
            foreach (const AICardCandidateView &offeredCandidate, request.cardCandidates) {
                if (offeredCandidate.cardId != useCardId)
                    continue;
                // A ticket naming a different candidate is not this candidate's ticket.
                if (result.action.candidateId >= 0
                    && result.action.candidateId != offeredCandidate.candidateId)
                    continue;
                offered = true;
                break;
            }
            if (!offered) return false;
        }
        candidate.card = played;
        cardUse = candidate;
        return true;
    }
    if (result.action.hasCardSpec) {
        Card *built = buildSpecCard(player, request, result.action.cardSpec);
        if (!built) return false;
        // The instance identity comes from the authorized conversion, never from the
        // spec: that is what makes two instances of one skill name distinguishable and
        // what keeps a borrowed entry pointing at the root that pays for it.
        const AICardConversionView *authorized = nullptr;
        foreach (const AICardConversionView &conversion, request.cardConversions) {
            if (conversion.conversionId == result.action.cardSpec.conversionId) {
                authorized = &conversion;
                break;
            }
        }
        if (!authorized) {
            built->deleteLater();
            return false;
        }
        candidate.hasSkillActivationRequest = true;
        candidate.activationRef = authorized->activationRef;
        candidate.sourceRef = authorized->sourceRef;
        candidate.setOwnedCard(built);
        cardUse = candidate;
        return true;
    }
    if (!result.action.legacyCardString.isEmpty()) {
        candidate.parse(result.action.legacyCardString, &m_room);
        if (!candidate.card) return false;
        if (!result.action.selectedTargetNames.isEmpty()) {
            candidate.to.clear();
            foreach (const QString &targetName, result.action.selectedTargetNames)
                candidate.to << m_room.findPlayerByObjectName(targetName);
        }
        if (request.hasSkillActionContext) {
            const AiSkillActionContext &context = request.skillActionContext;
            const QString skillName = candidate.card->getSkillName();
            if (skillName != context.getActivationSkillName()
                && skillName != context.getSourceSkillName())
                return false;
            candidate.hasSkillActivationRequest = true;
            candidate.activationRef = context.activationRef;
            candidate.sourceRef = context.sourceRef;
            Card *mutableCard = const_cast<Card *>(candidate.card);
            mutableCard->setActivationSkill(context.getActivationSkillName(),
                context.getActivationInstanceId());
            mutableCard->setSourceSkill(context.getSourceSkillName(),
                context.getSourceInstanceID());
        }
        cardUse = candidate;
        return true;
    }

    if (!result.action.hasSkillActionContext)
        return false;
    AIRequest playRequest;
    if (!request.hasSkillActionContext) {
        // 出牌階段的 Activate 請求不帶技能上下文; AI 選了 V2 主動技時由結果指明 instance,
        // 這裡按該 instance 重新建立上下文 (歸屬、canActivate、次數都重驗) 再造 proxy。
        SkillInstanceRef claimed = result.action.skillActionContext.activationRef;
        if (claimed.ownerObjectName.isEmpty())
            claimed.ownerObjectName = player->objectName();
        if (claimed.ownerObjectName != player->objectName())
            return false;
        if (request.kind != AIRequest::Activate) {
            bool offered = false;
            foreach (const AiSkillActionContext &candidate, request.skillActions) {
                if (candidate.activationRef == claimed) {
                    offered = true;
                    break;
                }
            }
            if (!offered)
                return false;
        }
        const SkillInstance *instance = player->findSkillInstance(claimed.key.skillName,
                                                                  claimed.key.instanceID);
        if (!instance
            || !buildSkillActionRequest(player, *instance, request.reason, request.pattern,
                                        request.prompt, request.handlingMethod, playRequest)
            || playRequest.skillActionContext.activationRef != claimed)
            return false;
    } else if (result.action.skillActionContext.activationRef != request.skillActionContext.activationRef
        || result.action.skillActionContext.sourceRef != request.skillActionContext.sourceRef) {
        return false;
    }
    const AiSkillActionContext &context = request.hasSkillActionContext
        ? request.skillActionContext : playRequest.skillActionContext;
    const ViewAsSkillV2 *skill = dynamic_cast<const ViewAsSkillV2 *>(
        Sanguosha->getViewAsSkill(context.getActivationSkillName()));
    if (!skill) return false;
    QSet<int> selectedCards;
    foreach (int cardId, result.action.selectedCardIds) {
        if (selectedCards.contains(cardId)) return false;
        selectedCards.insert(cardId);
    }
    candidate.hasSkillActivationRequest = true;
    candidate.activationRef = context.activationRef;
    candidate.sourceRef = context.sourceRef;
    ActiveSkillCard *proxy = new ActiveSkillCard;
    proxy->setActiveSkill(skill);
    proxy->setSkillName(skill->objectName());
    proxy->setActivationSkill(context.getActivationSkillName(), context.getActivationInstanceId());
    proxy->setSourceSkill(context.getSourceSkillName(), context.getSourceInstanceID());
    proxy->addSubcards(result.action.selectedCardIds);
    proxy->setUserString(result.action.userString);
    candidate.setOwnedCard(proxy);
    cardUse = candidate;
    return true;
}

AIRequest AiDecisionCoordinator::makeChoiceRequest(ServerPlayer *player,
                                                   AIRequest::DecisionKind kind,
                                                   const AIChoiceOptions &options) const
{
    AIRequest request = makeRequest(player, kind, CardUseStruct::CARD_USE_REASON_UNKNOWN,
                                    QString(), QString(), Card::MethodNone);
    request.choiceOptions = options;
    // These questions reveal their offered cards to this viewer. CardChosen is
    // deliberately excluded: another player's hidden hand is count-only.
    if (kind == AIRequest::Discard || kind == AIRequest::AmazingGrace
        || kind == AIRequest::Yiji || kind == AIRequest::Guanxing) {
        request.choiceOptions.candidatesComplete = true;
        for (int cardId : options.cardIds) {
            const Card *card = Sanguosha->getCard(cardId);
            if (card) request.choiceOptions.cards << makeAICardView(card);
            else request.choiceOptions.candidatesComplete = false;
        }
    }
    return request;
}

AIResult AiDecisionCoordinator::legacyAnswerResult(const AIRequest &request,
                                                   const QString &answer)
{
    AIResult result;
    result.decisionId = request.decisionId;
    result.stateRevision = request.stateRevision;
    result.handled = true;
    // An empty legacy answer is a refusal, not an answer of "".
    if (answer.isEmpty())
        return result;
    result.kind = AIResult::Answer;
    result.action.userString = answer;
    return result;
}

bool AiDecisionCoordinator::runAnswer(ServerPlayer *player, const AIRequest &request,
                                      const QString &callbackName, const LegacyAnswer &legacy,
                                      AIResult &result, bool *fromIsolated) const
{
    if (fromIsolated)
        *fromIsolated = false;
    if (!player || !player->getAI()) return false;
    const AiRoute route = Config.EnableAI
        ? m_room.roomRuntime()->ai().routes().routeFor(
            request.kind, callbackName, request.choiceOptions.reason)
        : AiRouteLegacyDirect;
    bool isolatedAnswer = false;
    if (route == AiRouteIsolated) {
        result = m_room.roomRuntime()->ai().decideIsolated(request);
        // An isolated answer counts only while it still belongs to this decision and
        // this board state. A stale one is dropped and the legacy AI answers instead;
        // the stamp is never rewritten to make an old answer acceptable.
        isolatedAnswer = result.handled && result.errorCode.isEmpty()
            && result.kind != AIResult::UseCard
            && result.decisionId == request.decisionId
            && result.stateRevision == request.stateRevision
            && request.stateRevision == m_room.roomRuntime()->stateRevision();
        if (!isolatedAnswer)
            result = legacy(request);
    } else {
        result = legacy(request);
    }
    if (!result.handled || !result.errorCode.isEmpty())
        return false;
    if (result.kind == AIResult::UseCard)
        return false; // A card action is not an answer to a value-typed question.
    if (isolatedAnswer) {
        if (fromIsolated)
            *fromIsolated = true;
        return true;
    }
    // The legacy callback ran live on this Room, so its answer belongs to the revision
    // it produced, exactly like the LegacyAdapted card path.
    result.decisionId = request.decisionId;
    result.stateRevision = m_room.roomRuntime()->stateRevision();
    return true;
}

void AiDecisionCoordinator::projectDecisionContext(ServerPlayer *viewer, const QVariant &data,
                                                   AIRequest &request) const
{
    if (!viewer || !Config.EnableAI) return;
    EngineRuntimeContextScope scope(*Sanguosha, &m_room);
    LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
    QJsonObject &context = request.choiceOptions.context;
    if (data.canConvert<JudgeStruct *>()) {
        const JudgeStruct *judge = data.value<JudgeStruct *>();
        if (!judge || !judge->who) return;
        const auto hasActiveFilter = [](const ServerPlayer *player) {
            for (const SkillInstance &instance : player->getSkillInstances()) {
                const auto *filter = dynamic_cast<const FilterSkill *>(
                    Sanguosha->getViewAsSkill(instance.skillName));
                if (filter && !player->isSkillInvalid(instance.skillName, instance.instanceID))
                    return true;
            }
            return false;
        };
        // A judged player's filter can change a retrial card. Do not run CardFilter,
        // rewrite engine cards, or speculate through callbacks while taking a view.
        bool complete = !hasActiveFilter(judge->who);
        int lightningHolders = 0;
        bool lightningReserveComplete = true;
        if (judge->reason != QStringLiteral("lightning")) {
            for (ServerPlayer *holder : m_room.getAllPlayers()) {
                if (!holder->containsTrick(QStringLiteral("lightning"))) continue;
                ++lightningHolders;
                if (hasActiveFilter(holder)) lightningReserveComplete = false;
            }
        }
        QList<int> owned = viewer->handCards();
        for (const Card *equip : viewer->getEquips())
            if (equip) owned << equip->getEffectiveId();
        // Response candidates already carry the authority's pattern/method gates.
        // Invocation/choice outcomes describe owned cards, not skill-cost legality.
        const QSet<int> ownedSet(owned.cbegin(), owned.cend());
        const QList<int> candidates = request.kind == AIRequest::RespondCard
            ? request.choiceOptions.cardIds : owned;
        QJsonObject outcomes;
        QJsonObject lightningCandidates;
        for (int id : candidates) {
            if (!ownedSet.contains(id)) continue;
            const Card *card = Sanguosha->getCard(id);
            if (!card) {
                complete = false;
                lightningReserveComplete = false;
                break;
            }
            if (viewer->isCardLimited(card, request.handlingMethod)) continue;
            if (complete) outcomes.insert(QString::number(id), judge->isGood(card));
            if (lightningHolders > 0 && lightningReserveComplete
                && card->getSuit() == Card::Spade && card->getNumber() >= 2
                && card->getNumber() <= 9)
                lightningCandidates.insert(QString::number(id), true);
        }
        if (!complete) outcomes = QJsonObject();
        if (!lightningReserveComplete) lightningCandidates = QJsonObject();
        // Lua first applies its retrial policy, then reserves one eligible card
        // from the end per holder. Reserving here would use the wrong candidate order.
        context.insert("judge", QJsonObject{{"who", judge->who->objectName()},
            {"reason", judge->reason}, {"pattern", judge->pattern}, {"good", judge->isGood()},
            {"negative", judge->negative}, {"card", publicDecisionCard(judge->card)},
            {"outcome_by_id", outcomes}, {"outcomes_complete", complete},
            {"lightning_candidate_ids", lightningCandidates},
            {"lightning_holder_count", lightningHolders},
            {"lightning_reserve_complete", lightningReserveComplete}});
    } else if (data.canConvert<DamageStruct>()) {
        const DamageStruct damage = data.value<DamageStruct>();
        context.insert("damage", QJsonObject{
            {"from", damage.from ? damage.from->objectName() : QString()},
            {"to", damage.to ? damage.to->objectName() : QString()},
            {"amount", damage.damage}, {"nature", int(damage.nature)}, {"reason", damage.reason},
            {"card", publicDecisionCard(damage.card)},
            {"card_name", damage.card ? damage.card->objectName() : QString()}});
    } else if (data.canConvert<CardEffectStruct>()) {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        context.insert("effect", QJsonObject{
            {"from", effect.from ? effect.from->objectName() : QString()},
            {"to", effect.to ? effect.to->objectName() : QString()},
            {"card", publicDecisionCard(effect.card)},
            {"card_name", effect.card ? effect.card->objectName() : QString()}});
    }
}

bool AiDecisionCoordinator::decideSkillInvoke(ServerPlayer *player, const QString &skillName,
                                              const QVariant &data, bool &invoked) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.reason = skillName;
    options.choices << QStringLiteral("yes") << QStringLiteral("no");
    options.optional = true;
    options.defaultChoice = QStringLiteral("no");
    options.hasDefaultChoice = true;
    // Skill frequency is public definition metadata, not an invocation decision.
    // Preserve SmartAI's normalized lookup without leaking native Skill objects.
    QString normalizedSkill = skillName;
    normalizedSkill.replace(QLatin1Char('-'), QLatin1Char('_'));
    const Skill *skill = Sanguosha->getSkill(normalizedSkill);
    options.context.insert(QStringLiteral("skill_frequency"),
                           skill ? static_cast<int>(skill->getFrequency()) : -1);
    AIRequest request = makeChoiceRequest(player, AIRequest::SkillInvoke, options);
    projectDecisionContext(player, data, request);
    AI *ai = player->getAI();
    AIResult result;
    if (!runAnswer(player, request,
                   QStringLiteral("askForSkillInvoke"),
                   [ai, &skillName, &data](const AIRequest &request) {
                       return legacyAnswerResult(request,
                           ai->askForSkillInvoke(skillName, data) ? QStringLiteral("yes")
                                                                  : QString());
                   }, result))
        return false;
    // Pass is the refusal; only an explicit "yes" invokes the skill.
    invoked = result.kind == AIResult::Answer
        && result.action.userString == QStringLiteral("yes");
    return true;
}

bool AiDecisionCoordinator::decideChoice(ServerPlayer *player, const QString &skillName,
                                         const QString &choices, const QVariant &data,
                                         QString &answer) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.reason = skillName;
    options.choices = choices.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    AIRequest request = makeChoiceRequest(player, AIRequest::Choice, options);
    projectDecisionContext(player, data, request);
    AI *ai = player->getAI();
    AIResult result;
    if (!runAnswer(player, request,
                   QStringLiteral("askForChoice"),
                   [ai, &skillName, &choices, &data](const AIRequest &request) {
                       return legacyAnswerResult(request,
                           ai->askForChoice(skillName, choices, data));
                   }, result))
        return false;
    if (result.kind != AIResult::Answer || result.action.userString.isEmpty())
        return false; // A refusal leaves the call site's own default in place.
    answer = result.action.userString;
    return true;
}

bool AiDecisionCoordinator::decideSuit(ServerPlayer *player, const QString &reason,
                                       Card::Suit &suit) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.reason = reason;
    for (int index = 0; index < 4; ++index)
        options.choices << Card::Suit2String(Card::AllSuits[index]);
    AI *ai = player->getAI();
    AIResult result;
    if (!runAnswer(player, makeChoiceRequest(player, AIRequest::Suit, options),
                   QStringLiteral("askForSuit"),
                   [ai, &reason](const AIRequest &request) {
                       return legacyAnswerResult(request, Card::Suit2String(ai->askForSuit(reason)));
                   }, result))
        return false;
    if (result.kind != AIResult::Answer) return false;
    // Suits cross as their names; an unknown name is refused, never guessed.
    for (int index = 0; index < 4; ++index) {
        if (Card::Suit2String(Card::AllSuits[index]) == result.action.userString) {
            suit = Card::AllSuits[index];
            return true;
        }
    }
    return false;
}

bool AiDecisionCoordinator::decideKingdom(ServerPlayer *player, const QString &reason,
                                          const QStringList &kingdoms, QString &answer) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.reason = reason;
    options.choices = kingdoms;
    AI *ai = player->getAI();
    // The legacy split between askForKingdom and askForChoice is the call site's rule;
    // it is reproduced here so the route change alone cannot alter which one runs.
    const bool useKingdomCallback = reason.isEmpty() || reason.contains(QStringLiteral("gamerule_"));
    AIResult result;
    if (!runAnswer(player, makeChoiceRequest(player, AIRequest::Kingdom, options),
                   QStringLiteral("askForKingdom"),
                   [ai, &reason, &kingdoms, useKingdomCallback](const AIRequest &request) {
                       return legacyAnswerResult(request, useKingdomCallback
                           ? ai->askForKingdom(kingdoms)
                           : ai->askForChoice(reason, kingdoms.join(QStringLiteral("+")), QVariant()));
                   }, result))
        return false;
    if (result.kind != AIResult::Answer || result.action.userString.isEmpty())
        return false;
    answer = result.action.userString;
    return true;
}

bool AiDecisionCoordinator::decideGeneral(ServerPlayer *player, const QStringList &generals,
                                          const QString &defaultChoice, const QString &reason,
                                          QString &answer) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.reason = reason;
    options.choices = generals;
    options.defaultChoice = defaultChoice;
    options.hasDefaultChoice = !defaultChoice.isEmpty();
    AI *ai = player->getAI();
    AIResult result;
    if (!runAnswer(player, makeChoiceRequest(player, AIRequest::General, options),
                   QStringLiteral("askForGeneral"),
                   [ai, &generals, &defaultChoice, &reason](const AIRequest &request) {
                       return legacyAnswerResult(request,
                           ai->askForGeneral(generals, defaultChoice, reason));
                   }, result))
        return false;
    if (result.kind != AIResult::Answer || result.action.userString.isEmpty())
        return false;
    answer = result.action.userString;
    return true;
}

bool AiDecisionCoordinator::decideDiscard(ServerPlayer *player, const QString &reason,
                                          int discardNum, int minNum, bool optional,
                                          bool includeEquip, const QString &pattern,
                                          const QList<int> &candidates, QList<int> &cards) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.reason = reason;
    // The call site already filtered out jilei and limited cards, so this list is the
    // authoritative candidate set; equips are in it only when the question allows them.
    options.cardIds = candidates;
    options.optional = optional;
    options.minCount = minNum;
    options.maxCount = discardNum;
    options.context = QJsonObject{{"include_equip", includeEquip}, {"pattern", pattern},
                                  {"reason", reason},
                                  {"exchange", player->hasFlag(QStringLiteral("Global_AIDiscardExchanging"))}};
    AIRequest request = makeChoiceRequest(player, AIRequest::Discard, options);
    request.pattern = pattern;
    AI *ai = player->getAI();
    AIResult result;
    bool fromIsolated = false;
    if (!runAnswer(player, request, QStringLiteral("askForDiscard"),
                   [ai, &reason, discardNum, minNum, optional, includeEquip, &pattern]
                   (const AIRequest &legacyRequest) {
                       AIResult legacyResult;
                       legacyResult.decisionId = legacyRequest.decisionId;
                       legacyResult.stateRevision = legacyRequest.stateRevision;
                       legacyResult.handled = true;
                       const QList<int> discarded = ai->askForDiscard(reason, discardNum, minNum,
                           optional, includeEquip, pattern);
                       if (discarded.isEmpty())
                           return legacyResult;
                       legacyResult.kind = AIResult::Answer;
                       legacyResult.action.selectedCardIds = discarded;
                       return legacyResult;
                   }, result, &fromIsolated))
        return false;
    if (fromIsolated && result.kind == AIResult::Pass && optional) {
        cards.clear();
        return true;
    }
    if (result.kind != AIResult::Answer) return false;
    if (fromIsolated) {
        // Only cards the question offered may come back, in the amount it allows.
        const QSet<int> offered(candidates.cbegin(), candidates.cend());
        QSet<int> seen;
        foreach (const int cardId, result.action.selectedCardIds) {
            if (!offered.contains(cardId) || seen.contains(cardId))
                return false;
            seen.insert(cardId);
        }
        const int picked = result.action.selectedCardIds.size();
        if (picked > discardNum || (picked < minNum && !(optional && picked == 0)))
            return false;
    }
    cards = result.action.selectedCardIds;
    return true;
}

bool AiDecisionCoordinator::decideAmazingGrace(ServerPlayer *player, const QList<int> &cardIds,
                                               bool refusable, const QString &reason,
                                               int &cardId) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.reason = reason;
    options.cardIds = cardIds;
    options.optional = refusable;
    AI *ai = player->getAI();
    AIResult result;
    bool fromIsolated = false;
    int legacyCardId = -1;
    if (!runAnswer(player, makeChoiceRequest(player, AIRequest::AmazingGrace, options),
                   QStringLiteral("askForAG"),
                   [ai, &cardIds, refusable, &reason, &legacyCardId](const AIRequest &legacyRequest) {
                       AIResult legacyResult;
                       legacyResult.decisionId = legacyRequest.decisionId;
                       legacyResult.stateRevision = legacyRequest.stateRevision;
                       legacyResult.handled = true;
                       const int chosen = ai->askForAG(cardIds, refusable, reason);
                       legacyCardId = chosen;
                       if (chosen < 0)
                           return legacyResult;
                       legacyResult.kind = AIResult::Answer;
                       legacyResult.action.selectedCardIds << chosen;
                       return legacyResult;
                   }, result, &fromIsolated))
        return false;
    if (!fromIsolated) {
        if (legacyCardId < 0) return false;
        cardId = legacyCardId;
        return true;
    }
    if (result.kind == AIResult::Pass && refusable) {
        cardId = -1;
        return true;
    }
    if (result.kind != AIResult::Answer || result.action.selectedCardIds.size() != 1)
        return false;
    if (!cardIds.contains(result.action.selectedCardIds.first()))
        return false;
    cardId = result.action.selectedCardIds.first();
    return true;
}

bool AiDecisionCoordinator::decideCardChosen(ServerPlayer *player, ServerPlayer *who,
                                             const QString &flags, const QString &reason,
                                             Card::HandlingMethod method, int &cardId) const
{
    if (!player || !player->getAI() || !who) return false;
    AIChoiceOptions options;
    options.reason = reason;
    // Public zones and viewer-visible hand members may be offered below. Hidden
    // hand identities stay out of both metadata and the candidate set.
    options.playerNames << who->objectName();
    options.choices << flags;
    AIRequest request = makeChoiceRequest(player, AIRequest::CardChosen, options);
    request.handlingMethod = method;
    // A face-down hand is not an enumerable set of card identities. Offer only
    // visible members of the requested zones and say when the set is incomplete.
    request.choiceOptions.candidatesComplete = true;
    request.choiceOptions.context = QJsonObject{{"target", who->objectName()},
        {"who", who->objectName()}, {"flags", flags}, {"method", int(method)}, {"reason", reason}};
    const AIPlayerView *targetView = who == player ? &request.worldView.self : nullptr;
    if (!targetView) {
        for (const AIPlayerView &view : request.worldView.players) {
            if (view.objectName == who->objectName()) { targetView = &view; break; }
        }
    }
    if (targetView) {
        QList<AICardView> visible;
        if (flags.contains(QLatin1Char('e'))) visible << targetView->equips;
        if (flags.contains(QLatin1Char('j'))) visible << targetView->judgingArea;
        if (flags.contains(QLatin1Char('h'))) {
            visible << (who == player ? request.worldView.handCards : targetView->knownCards);
            if (!targetView->handVisible) request.choiceOptions.candidatesComplete = false;
        }
        for (const AICardView &card : visible) {
            if (method == Card::MethodDiscard && !player->canDiscard(who, card.effectiveId)) continue;
            request.choiceOptions.cardIds << card.effectiveId;
            request.choiceOptions.cards << card;
        }
    } else {
        request.choiceOptions.candidatesComplete = false;
    }
    AI *ai = player->getAI();
    AIResult result;
    bool fromIsolated = false;
    int legacyCardId = -1;
    if (!runAnswer(player, request, QStringLiteral("askForCardChosen"),
                   [ai, who, &flags, &reason, method, &legacyCardId](const AIRequest &legacyRequest) {
                       AIResult legacyResult;
                       legacyResult.decisionId = legacyRequest.decisionId;
                       legacyResult.stateRevision = legacyRequest.stateRevision;
                       legacyResult.handled = true;
                       const int chosen = ai->askForCardChosen(who, flags, reason, method);
                       legacyCardId = chosen;
                       if (chosen < 0)
                           return legacyResult;
                       legacyResult.kind = AIResult::Answer;
                       legacyResult.action.selectedCardIds << chosen;
                       return legacyResult;
                   }, result, &fromIsolated))
        return false;
    if (!fromIsolated) {
        if (legacyCardId < 0) return false;
        cardId = legacyCardId;
        return true;
    }
    if (result.kind != AIResult::Answer || result.action.selectedCardIds.size() != 1)
        return false;
    cardId = result.action.selectedCardIds.first();
    if (!request.choiceOptions.cardIds.contains(cardId)) return false;
    if (method == Card::MethodDiscard && !player->canDiscard(who, cardId)) return false;
    return true;
}

bool AiDecisionCoordinator::decideYiji(ServerPlayer *player, const QList<int> &cards,
                                       const QString &reason,
                                       const QList<ServerPlayer *> &candidates,
                                       ServerPlayer *&target, int &cardId) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.reason = reason;
    options.cardIds = cards;
    foreach (ServerPlayer *candidate, candidates) {
        if (candidate)
            options.playerNames << candidate->objectName();
    }
    options.optional = true;
    AI *ai = player->getAI();
    AIResult result;
    bool fromIsolated = false;
    ServerPlayer *legacyReceiver = nullptr;
    int legacyCardId = -1;
    if (!runAnswer(player, makeChoiceRequest(player, AIRequest::Yiji, options),
                   QStringLiteral("askForYiji"),
                   [ai, &cards, &reason, &legacyReceiver, &legacyCardId](const AIRequest &legacyRequest) {
                       AIResult legacyResult;
                       legacyResult.decisionId = legacyRequest.decisionId;
                       legacyResult.stateRevision = legacyRequest.stateRevision;
                       legacyResult.handled = true;
                       ServerPlayer *receiver = ai->askForYiji(cards, reason, legacyCardId);
                       legacyReceiver = receiver;
                       if (!receiver || legacyCardId < 0)
                           return legacyResult;
                       legacyResult.kind = AIResult::Answer;
                       legacyResult.action.selectedCardIds << legacyCardId;
                       legacyResult.action.selectedTargetNames << receiver->objectName();
                       return legacyResult;
                   }, result, &fromIsolated))
        return false;
    if (!fromIsolated) {
        if (!legacyReceiver || legacyCardId < 0) return false;
        target = legacyReceiver;
        cardId = legacyCardId;
        return true;
    }
    if (result.kind == AIResult::Pass) {
        target = nullptr;
        cardId = -1;
        return true;
    }
    if (result.kind != AIResult::Answer || result.action.selectedCardIds.size() != 1
        || result.action.selectedTargetNames.size() != 1)
        return false;
    if (!cards.contains(result.action.selectedCardIds.first()))
        return false;
    // The receiver is resolved inside the offered list, never by a Room-wide lookup.
    foreach (ServerPlayer *candidate, candidates) {
        if (candidate && candidate->objectName() == result.action.selectedTargetNames.first()) {
            target = candidate;
            cardId = result.action.selectedCardIds.first();
            return true;
        }
    }
    return false;
}

bool AiDecisionCoordinator::decidePlayerChosen(ServerPlayer *player,
                                               const QList<ServerPlayer *> &targets,
                                               const QString &reason,
                                               ServerPlayer *&choice) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.reason = reason;
    foreach (ServerPlayer *target, targets) {
        if (target)
            options.playerNames << target->objectName();
    }
    AI *ai = player->getAI();
    AIResult result;
    bool fromIsolated = false;
    ServerPlayer *legacyChoice = nullptr;
    if (!runAnswer(player, makeChoiceRequest(player, AIRequest::PlayerChosen, options),
                   QStringLiteral("askForPlayerChosen"),
                   [ai, &targets, &reason, &legacyChoice](const AIRequest &legacyRequest) {
                       AIResult legacyResult;
                       legacyResult.decisionId = legacyRequest.decisionId;
                       legacyResult.stateRevision = legacyRequest.stateRevision;
                       legacyResult.handled = true;
                       ServerPlayer *chosen = ai->askForPlayerChosen(targets, reason);
                       legacyChoice = chosen;
                       if (!chosen)
                           return legacyResult;
                       legacyResult.kind = AIResult::Answer;
                       legacyResult.action.selectedTargetNames << chosen->objectName();
                       return legacyResult;
                   }, result, &fromIsolated))
        return false;
    if (!fromIsolated) {
        if (!legacyChoice) return false;
        choice = legacyChoice;
        return true;
    }
    if (result.kind != AIResult::Answer || result.action.selectedTargetNames.size() != 1)
        return false;
    foreach (ServerPlayer *target, targets) {
        if (target && target->objectName() == result.action.selectedTargetNames.first()) {
            choice = target;
            return true;
        }
    }
    return false;
}

bool AiDecisionCoordinator::decidePlayersChosen(ServerPlayer *player,
                                                const QList<ServerPlayer *> &targets,
                                                const QString &reason, int maxNum, int minNum,
                                                QList<ServerPlayer *> &chosen) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.reason = reason;
    foreach (ServerPlayer *target, targets) {
        if (target)
            options.playerNames << target->objectName();
    }
    options.minCount = minNum;
    options.maxCount = maxNum;
    options.optional = minNum <= 0;
    AI *ai = player->getAI();
    AIResult result;
    bool fromIsolated = false;
    QList<ServerPlayer *> legacyPicked;
    if (!runAnswer(player, makeChoiceRequest(player, AIRequest::PlayersChosen, options),
                   QStringLiteral("askForPlayersChosen"),
                   [ai, &targets, &reason, maxNum, minNum, &legacyPicked](const AIRequest &legacyRequest) {
                       AIResult legacyResult;
                       legacyResult.decisionId = legacyRequest.decisionId;
                       legacyResult.stateRevision = legacyRequest.stateRevision;
                       legacyResult.handled = true;
                       const QList<ServerPlayer *> picked = ai->askForPlayersChosen(targets,
                           reason, maxNum, minNum);
                       legacyPicked = picked;
                       if (picked.isEmpty())
                           return legacyResult;
                       legacyResult.kind = AIResult::Answer;
                       foreach (ServerPlayer *target, picked) {
                           if (target)
                               legacyResult.action.selectedTargetNames << target->objectName();
                       }
                       return legacyResult;
                   }, result, &fromIsolated))
        return false;
    if (!fromIsolated) {
        if (legacyPicked.isEmpty()) return false;
        chosen = legacyPicked;
        return true;
    }
    if (result.kind == AIResult::Pass && minNum <= 0) {
        // Callers may prefill this list with every candidate. A deliberate decline
        // must clear it, rather than leave that default looking like an AI choice.
        chosen.clear();
        return true;
    }
    if (result.kind != AIResult::Answer) return false;
    QList<ServerPlayer *> picked;
    QHash<QString, ServerPlayer *> offered;
    for (ServerPlayer *target : targets)
        if (target) offered.insert(target->objectName(), target);
    QSet<QString> seen;
    foreach (const QString &name, result.action.selectedTargetNames) {
        ServerPlayer *match = offered.value(name, nullptr);
        if (!match || seen.contains(name))
            return false; // Unknown or repeated targets are refused, not trimmed.
        seen.insert(name);
        picked << match;
    }
    if (picked.size() > maxNum || picked.size() < minNum)
        return false;
    chosen = picked;
    return true;
}

bool AiDecisionCoordinator::decideGuanxing(ServerPlayer *player, const QList<int> &cards,
                                           int guanxingType, QList<int> &up,
                                           QList<int> &bottom) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.reason = QStringLiteral("guanxing");
    options.question = QStringLiteral("askForGuanxing");
    options.cardIds = cards;
    options.minCount = 0;
    options.maxCount = cards.size();
    options.defaultChoice = QString::number(guanxingType);
    options.hasDefaultChoice = true;
    AI *ai = player->getAI();
    AIResult result;
    if (!runAnswer(player, makeChoiceRequest(player, AIRequest::Guanxing, options),
                   QStringLiteral("askForGuanxing"),
                   [ai, &cards, guanxingType](const AIRequest &legacyRequest) {
                       AIResult legacyResult;
                       legacyResult.decisionId = legacyRequest.decisionId;
                       legacyResult.stateRevision = legacyRequest.stateRevision;
                       legacyResult.handled = true;
                       QList<int> legacyUp, legacyBottom;
                       ai->askForGuanxing(cards, legacyUp, legacyBottom, guanxingType);
                       legacyResult.kind = AIResult::Answer;
                       legacyResult.action.selectedCardIds = legacyUp;
                       legacyResult.action.bottomCardIds = legacyBottom;
                       return legacyResult;
                   }, result))
        return false;
    if (result.kind != AIResult::Answer) return false;
    // Both piles together must be exactly the cards the question offered, in some order.
    QList<int> seen = result.action.selectedCardIds;
    seen << result.action.bottomCardIds;
    if (seen.size() != cards.size())
        return false;
    const QSet<int> expected(cards.cbegin(), cards.cend());
    const QSet<int> actual(seen.cbegin(), seen.cend());
    if (actual.size() != seen.size() || actual != expected) return false;
    up = result.action.selectedCardIds;
    bottom = result.action.bottomCardIds;
    return true;
}

bool AiDecisionCoordinator::decideTriggerOrder(ServerPlayer *player, const QString &reason,
                                               const QStringList &candidates,
                                               QMap<ServerPlayer *, QStringList> &skills,
                                               bool optional, const QVariant &data,
                                               QString &answer) const
{
    if (!player || !player->getAI()) return false;
    AIChoiceOptions options;
    options.reason = reason;
    options.choices = candidates;
    options.optional = optional;
    AI *ai = player->getAI();
    AIResult result;
    if (!runAnswer(player, makeChoiceRequest(player, AIRequest::TriggerOrder, options),
                   QStringLiteral("askForTriggerOrder"),
                   [ai, &reason, &skills, optional, &data](const AIRequest &legacyRequest) {
                       return legacyAnswerResult(legacyRequest,
                           ai->askForTriggerOrder(reason, skills, optional, data));
                   }, result))
        return false;
    if (result.kind != AIResult::Answer || result.action.userString.isEmpty())
        return false;
    // The call site normalizes the skill/owner spelling, so the string passes through.
    answer = result.action.userString;
    return true;
}

const Card *AiDecisionCoordinator::decideResponse(ServerPlayer *player, const AIRequest &request,
                                                  const QString &callbackName,
                                                  const LegacyCard &legacy) const
{
    if (!player || !player->getAI()) return nullptr;
    const AiRoute route = Config.EnableAI
        ? m_room.roomRuntime()->ai().routes().routeFor(
            request.kind, callbackName, request.choiceOptions.reason)
        : AiRouteLegacyDirect;
    if (route == AiRouteIsolated) {
        const AIResult result = m_room.roomRuntime()->ai().decideIsolated(request);
        // A deliberate, current decline is an isolated decision. The fault guard
        // below must never override it or count as successful isolated coverage.
        if (result.handled && result.errorCode.isEmpty() && result.kind == AIResult::Pass
            && request.choiceOptions.optional && result.decisionId == request.decisionId
            && result.stateRevision == request.stateRevision
            && request.stateRevision == m_room.roomRuntime()->stateRevision())
            return nullptr;
        const Card *answered = responseCard(player, request, result);
        if (answered)
            return answered;
        // Fault containment only: unhandled/stale/invalid answers are isolated
        // failures. Keeping the legacy pointer here does not complete that decision.
    }
    return legacy();
}

const Card *AiDecisionCoordinator::responseCard(ServerPlayer *player, const AIRequest &request,
                                                const AIResult &result) const
{
    if (!player || !result.handled || !result.errorCode.isEmpty()) return nullptr;
    if (request.kind != AIRequest::RespondCard || result.kind != AIResult::Answer
        || !result.action.selectedTargetNames.isEmpty()
        || !result.action.bottomCardIds.isEmpty()
        || !result.action.userString.isEmpty() || !result.action.legacyCardString.isEmpty()
        || result.action.useCardId >= 0)
        return nullptr;
    if (result.decisionId != request.decisionId
        || result.stateRevision != request.stateRevision
        || request.stateRevision != m_room.roomRuntime()->stateRevision())
        return nullptr;
    QString pattern = request.pattern;
    if (pattern.endsWith(QLatin1Char('!'))) pattern.chop(1);
    if (result.action.hasCardSpec) {
        if (!result.action.selectedCardIds.isEmpty()
            || request.choiceOptions.question == QStringLiteral("askForCardShow")
            || request.choiceOptions.question == QStringLiteral("askForPindian")) return nullptr;
        Card *card = buildSpecCard(player, request, result.action.cardSpec);
        if (!card) return nullptr;
        if (player->isCardLimited(card, request.handlingMethod)
            || (!pattern.isEmpty() && !Sanguosha->matchPattern(pattern, player, card))) {
            card->deleteLater();
            return nullptr;
        }
        // The response API returns a raw pointer. Queue reclamation without a drain
        // so the caller can first acquire its CardResponseStruct lease. Existing
        // decision/turn scopes reclaim it at the next safe point.
        CardLifetimeManager &lifetime = globalCardLifetimeManager();
        if (lifetime.mode() == CardLifetimeMode::ObserveOnly)
            card->deleteLater(); // ObserveOnly requires the ordinary QObject queue.
        else
            lifetime.requestNativeDelete(card);
        return card;
    }
    if (result.action.selectedCardIds.size() != 1) return nullptr;
    const int cardId = result.action.selectedCardIds.first();
    if (!request.choiceOptions.cardIds.contains(cardId)) return nullptr;
    if (!player->handCards().contains(cardId)) {
        bool equipped = false;
        foreach (const Card *equip, player->getEquips()) {
            if (equip && equip->getEffectiveId() == cardId) {
                equipped = true;
                break;
            }
        }
        if (!equipped)
            return nullptr;
    }
    const Card *card = Sanguosha->getCard(cardId);
    if (!card || player->isCardLimited(card, request.handlingMethod)) return nullptr;
    if (!pattern.isEmpty() && !Sanguosha->matchPattern(pattern, player, card)) return nullptr;
    return card;
}

AIRequest AiDecisionCoordinator::makeResponseRequest(ServerPlayer *player,
                                                     const QString &question,
                                                     const QString &reason,
                                                     const QString &pattern,
                                                     const QString &prompt,
                                                     Card::HandlingMethod method) const
{
    AIChoiceOptions options;
    options.question = question;
    options.reason = reason;
    options.optional = question != QStringLiteral("askForCardShow")
        && question != QStringLiteral("askForPindian");
    // Build candidates with the actual question, not a late-overwritten empty pattern.
    AIRequest request = makeRequest(player, AIRequest::RespondCard,
        method == Card::MethodUse ? CardUseStruct::CARD_USE_REASON_RESPONSE_USE
                                 : CardUseStruct::CARD_USE_REASON_RESPONSE,
        pattern, prompt, method);
    // Both physical cards and issued conversion tickets can answer a response.
    // Showing and pindian intentionally retain their physical-only contract.
    const bool physicalOnly = question == QStringLiteral("askForCardShow")
        || question == QStringLiteral("askForPindian");
    options.candidatesComplete = physicalOnly
        || request.conversionsEnumerated;
    const QList<int> hand = player ? player->handCards() : QList<int>();
    const QSet<int> handSet(hand.cbegin(), hand.cend());
    for (const AICardCandidateView &candidate : request.cardCandidates) {
        // Showing/pindian/physical responses choose the viewer's hand. Equipment is
        // still available as a conversion cost, never as an arbitrary shown card.
        if (!candidate.available || candidate.limited || !handSet.contains(candidate.cardId)) continue;
        const Card *card = Sanguosha->getCard(candidate.cardId);
        if (!card) { options.candidatesComplete = false; continue; }
        options.cardIds << candidate.cardId;
        options.cards << makeAICardView(card);
    }
    request.choiceOptions = options;
    return request;
}

const Card *AiDecisionCoordinator::decideResponseCard(ServerPlayer *player,
                                                      const QString &pattern,
                                                      const QString &prompt,
                                                      const QVariant &data,
                                                      Card::HandlingMethod method) const
{
    if (!player || !player->getAI()) return nullptr;
    AI *ai = player->getAI();
    AIRequest request = makeResponseRequest(player, QStringLiteral("askForCard"),
                                            pattern, pattern, prompt, method);
    projectDecisionContext(player, data, request);
    return decideResponse(player, request,
        QStringLiteral("askForCard"),
        [ai, &pattern, &prompt, &data, method]() {
            return ai->askForCard(pattern, prompt, data, method);
        });
}

const Card *AiDecisionCoordinator::decideNullification(ServerPlayer *player, const Card *trick,
                                                       ServerPlayer *from, ServerPlayer *to,
                                                       bool positive) const
{
    if (!player || !player->getAI()) return nullptr;
    AI *ai = player->getAI();
    AIRequest request = makeResponseRequest(player, QStringLiteral("askForNullification"),
                                            QStringLiteral("nullification"),
                                            QStringLiteral("nullification"), QString(),
                                            Card::MethodUse);
    // Only names cross: the trick and its endpoints are identified, never handed over.
    if (trick)
        request.choiceOptions.choices << trick->objectName();
    if (from)
        request.choiceOptions.playerNames << from->objectName();
    if (to)
        request.choiceOptions.playerNames << to->objectName();
    request.choiceOptions.context = QJsonObject{
        {"from", from ? from->objectName() : QString()},
        {"to", to ? to->objectName() : QString()},
        {"card_name", trick ? trick->objectName() : QString()},
        {"card", publicDecisionCard(trick)},
        {"positive", positive}};
    request.choiceOptions.defaultChoice = positive ? QStringLiteral("positive")
                                                   : QStringLiteral("negative");
    request.choiceOptions.hasDefaultChoice = true;
    return decideResponse(player, request, QStringLiteral("askForNullification"),
        [ai, trick, from, to, positive]() {
            return ai->askForNullification(trick, from, to, positive);
        });
}

const Card *AiDecisionCoordinator::decideCardShow(ServerPlayer *player, ServerPlayer *requestor,
                                                  const QString &reason) const
{
    if (!player || !player->getAI()) return nullptr;
    AI *ai = player->getAI();
    AIRequest request = makeResponseRequest(player, QStringLiteral("askForCardShow"), reason, QString(),
                                            QString(), Card::MethodNone);
    if (requestor)
        request.choiceOptions.playerNames << requestor->objectName();
    return decideResponse(player, request, QStringLiteral("askForCardShow"),
        [ai, requestor, &reason]() { return ai->askForCardShow(requestor, reason); });
}

const Card *AiDecisionCoordinator::decidePindian(ServerPlayer *player, ServerPlayer *requestor,
                                                 const QString &reason) const
{
    if (!player || !player->getAI()) return nullptr;
    AI *ai = player->getAI();
    AIRequest request = makeResponseRequest(player, QStringLiteral("askForPindian"), reason, QString(),
                                            QString(), Card::MethodPindian);
    if (requestor)
        request.choiceOptions.playerNames << requestor->objectName();
    return decideResponse(player, request, QStringLiteral("askForPindian"),
        [ai, requestor, &reason]() { return ai->askForPindian(requestor, reason); });
}

const Card *AiDecisionCoordinator::decideSinglePeach(ServerPlayer *player,
                                                     ServerPlayer *dying) const
{
    if (!player || !player->getAI()) return nullptr;
    AI *ai = player->getAI();
    AIRequest request = makeResponseRequest(player, QStringLiteral("askForSinglePeach"),
                                            QStringLiteral("single_peach"),
                                            player == dying
                                                ? QStringLiteral("peach+analeptic")
                                                : QStringLiteral("peach"),
                                            QString(), Card::MethodUse);
    if (dying)
        request.choiceOptions.playerNames << dying->objectName();
    return decideResponse(player, request, QStringLiteral("askForSinglePeach"),
        [ai, dying]() { return ai->askForSinglePeach(dying); });
}

bool AiDecisionCoordinator::decide(ServerPlayer *player, const AIRequest &request,
                                   CardUseStruct &cardUse) const
{
    if (!player || !player->getAI()) return false;
    const QString callbackName = request.kind == AIRequest::Activate
        ? QStringLiteral("activate") : QStringLiteral("askForUseCard");
    // 診斷插樁: 量測單次 AI 決策的耗時與熱點呼叫次數 (QSAN_AI_PROBE=1 才啟用)。
    QElapsedTimer probeTimer;
    if (AiProbe::enabled()) {
        AiProbe::reset();
        probeTimer.start();
    }
    struct ProbeGuard {
        const QElapsedTimer &timer;
        const ServerPlayer *who;
        const QString &callback;
        const AIRequest &req;
        ~ProbeGuard()
        {
            if (!AiProbe::enabled()) return;
            const qint64 ms = timer.elapsed();
            if (ms < AiProbe::reportThresholdMs()) return;
            qWarning().noquote() << QString("[AI_PROBE] %1 method=%2 pattern=%3 prompt=%4 elapsed=%5ms %6")
                .arg(who ? who->objectName() : QStringLiteral("?"))
                .arg(callback)
                .arg(req.pattern.isEmpty() ? QStringLiteral("-") : req.pattern)
                .arg(req.prompt.isEmpty() ? QStringLiteral("-") : req.prompt)
                .arg(ms)
                .arg(AiProbe::report() + QString(" skillgen=%1").arg(SkillSet::generation()));
        }
    } probeGuard{probeTimer, player, callbackName, request};
    const QString skillName = request.hasSkillActionContext
        ? request.skillActionContext.getActivationSkillName() : QString();
    const AiRoute route = Config.EnableAI
        ? m_room.roomRuntime()->ai().routes().routeFor(request.kind, callbackName, skillName)
        : AiRouteLegacyDirect;
    if (route == AiRouteLegacyDirect) {
        if (request.hasSkillActionContext)
            return false;
        CardUseStruct directUse = cardUse;
        directUse.from = player;
        directUse.card = nullptr;
        directUse.to.clear();
        if (request.kind == AIRequest::Activate) {
            player->getAI()->activate(directUse);
        } else {
            const QString answer = player->getAI()->askForUseCard(
                request.pattern, request.prompt, request.handlingMethod);
            if (!answer.isEmpty() && answer != QStringLiteral("."))
                directUse.parse(answer, &m_room);
        }
        cardUse = directUse;
        return true;
    }

    AIResult result;
    bool liveLegacyResult = route == AiRouteLegacyAdapted;
    if (route == AiRouteIsolated) {
        result = m_room.roomRuntime()->ai().decideIsolated(request);
        if (!result.handled || !result.errorCode.isEmpty()) {
            result = player->getAI()->decide(request);
            liveLegacyResult = true;
        }
    } else {
        result = player->getAI()->decide(request);
    }
    if (liveLegacyResult) {
        // Live Lua already ran on the current Room. getTurnUse / fillSkillCards
        // may bump stateRevision (marks, skill instance, card moves). Stamping the
        // pre-callback revision then fail-closes a filled turnUse as Pass, which
        // ends Play and goes straight to discard.
        result.decisionId = request.decisionId;
        result.stateRevision = m_room.roomRuntime()->stateRevision();
        AIRequest liveRequest = request;
        liveRequest.stateRevision = result.stateRevision;
        return applyResult(player, liveRequest, result, cardUse);
    }
    return applyResult(player, request, result, cardUse);
}

bool AiDecisionCoordinator::decideSkillAction(
    ServerPlayer *player, CardUseStruct::CardUseReason reason,
    const QString &pattern, const QString &prompt, Card::HandlingMethod method,
    CardUseStruct &cardUse) const
{
    if (!Config.EnableAI || !player || !player->getAI()) return false;
    foreach (const SkillInstance &instance, player->getSkillInstances()) {
        AIRequest request;
        if (!buildSkillActionRequest(player, instance, reason, pattern, prompt, method, request))
            continue;
        CardUseStruct candidate = cardUse;
        if (decide(player, request, candidate) && candidate.card) {
            cardUse = candidate;
            return true;
        }
    }
    return false;
}

int AiDecisionCoordinator::skillActionInstanceId(ServerPlayer *player,
                                                 const QString &skillName) const
{
    if (!player) return -1;
    const ViewAsSkillV2 *skill = dynamic_cast<const ViewAsSkillV2 *>(
        Sanguosha->getViewAsSkill(skillName));
    if (!skill) return 0; // Not a V2 skill: keep the legacy ai_fill_skill path unchanged.

    foreach (const SkillInstance &instance, player->getSkillInstances()) {
        if (instance.skillName != skillName) continue;
        AIRequest request;
        if (buildSkillActionRequest(player, instance, CardUseStruct::CARD_USE_REASON_PLAY,
            QString(), QString(), Card::MethodUse, request))
            return instance.instanceID;
    }
    return -1;
}

AiLegacyRequestView AiDecisionCoordinator::skillActionContext(
    ServerPlayer *player, const QString &skillName, CardUseStruct::CardUseReason reason,
    const QString &pattern, const QString &prompt, Card::HandlingMethod method) const
{
    AIRequest request;
    if (!player) return AiLegacyRequestView();
    foreach (const SkillInstance &instance, player->getSkillInstances()) {
        if (instance.skillName != skillName) continue;
        if (buildSkillActionRequest(player, instance, reason, pattern, prompt, method, request))
            return AiLegacyRequestView(request, player);
    }
    return AiLegacyRequestView();
}
