#include "ai-decision-coordinator.h"
#include "ai-runtime.h"

#include "engine.h"
#include "room.h"
#include "server-info.h"
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
static AICardCandidateView makeAICardCandidate(Room &room, ServerPlayer *player,
                                               const Card *card, Card::HandlingMethod method)
{
    AICardCandidateView candidate;
    if (!player || !card)
        return candidate;
    candidate.cardId = card->getEffectiveId();
    candidate.available = card->isAvailable(player);
    candidate.limited = player->isCardLimited(card, method);
    candidate.jilei = player->isJilei(card);
    candidate.targetFixed = card->targetFixed();
    QList<const Player *> selected;
    foreach (ServerPlayer *target, room.getAlivePlayers()) {
        int maxVotes = 0;
        if (!card->targetFilter(selected, target, player, maxVotes))
            continue;
        if (room.isProhibited(player, target, card))
            continue;
        candidate.legalTargets << target->objectName();
        if (maxVotes > candidate.maxTargets)
            candidate.maxTargets = maxVotes;
    }
    if (candidate.maxTargets < 1 && !candidate.legalTargets.isEmpty())
        candidate.maxTargets = 1;
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
    view.handlingMethod = int(card->getHandlingMethod());
    view.virtualCard = card->isVirtualCard();
    view.targetFixed = card->targetFixed();
    view.damageCard = card->isDamageCard();
    view.subcardIds = card->getSubcards();
    return view;
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
    if (target)
        event.to = target->objectName();
    if (data.canConvert<DamageStruct>()) {
        const DamageStruct damage = data.value<DamageStruct>();
        event.kind = QStringLiteral("damage");
        event.from = damage.from ? damage.from->objectName() : QString();
        event.to = damage.to ? damage.to->objectName() : QString();
        event.amount = damage.damage;
        event.nature = int(damage.nature);
        event.reason = damage.reason;
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
                event.cardName = judge->card->objectName();
                event.cardIds << judge->card->getEffectiveId();
            }
        }
    } else if (data.canConvert<DyingStruct>()) {
        const DyingStruct dying = data.value<DyingStruct>();
        event.kind = QStringLiteral("dying");
        event.to = dying.who ? dying.who->objectName() : QString();
    } else if (data.canConvert<CardUseStruct>()) {
        const CardUseStruct use = data.value<CardUseStruct>();
        event.kind = QStringLiteral("card_use");
        event.from = use.from ? use.from->objectName() : QString();
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
    while (m_events.size() >= AiEventLogLimit)
        m_events.removeFirst();
    m_events << event;
}

AIWorldView AiDecisionCoordinator::buildWorldView(ServerPlayer *viewer) const
{
    EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
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

    const bool hegemony = ServerInfo.EnableHegemony;
    foreach (ServerPlayer *player, m_room.getAllPlayers(true)) {
        AIPlayerView playerView;
        playerView.objectName = player->objectName();
        // Resolve control links without getActualController's repair/mutation path.
        QSet<const ServerPlayer *> controllers;
        ServerPlayer *controller = player;
        while (controller && !controllers.contains(controller)) {
            controllers.insert(controller);
            const QString name = controller->getTag("Controller_Name").toString();
            if (name.isEmpty()) break;
            ServerPlayer *next = m_room.findPlayerByObjectName(name, true);
            if (!next) break;
            controller = next;
        }
        playerView.controller = controller ? controller->objectName() : player->objectName();
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
        for (int slot = 0; slot < 5; ++slot) {
            if (const EquipCard *equip = player->getEquip(slot))
                playerView.equipSlots.insert(slot, equip->getEffectiveId());
        }

        const bool seesIdentity = player == viewer || !hegemony
            || player->hasShownOneGeneral() || player->isDead();
        if (seesIdentity)
            playerView.kingdom = player->getKingdom();
        world.customRoles = world.customRoles || player->getRoleEnum() == Player::UnknownRole;
        playerView.roleRevealed = m_room.isRoleRevealed(player);
        playerView.roleVisible = m_room.canSeeRole(viewer, player);
        if (playerView.roleVisible)
            playerView.role = player->getRole();
        if (player == viewer || !hegemony || player->hasShownGeneral() || player->isDead())
            playerView.generalName = player->getGeneralName();
        if (player == viewer || !hegemony || player->hasShownGeneral2() || player->isDead())
            playerView.general2Name = player->getGeneral2Name();

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
            const bool visibleToViewer = player == viewer || !hegemony
                || instance.source == SourceAcquired || instance.source == SourceAttached
                || player->hasShownSkill(instance.skillName);
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
            world.self = playerView;
            foreach (const Card *card, player->getHandcards())
                world.handCards << makeAICardView(card);
        } else {
            world.players << playerView;
        }
    }
    foreach (ServerPlayer *from, m_room.getAlivePlayers()) {
        QMap<QString, int> row;
        foreach (ServerPlayer *to, m_room.getAlivePlayers()) {
            if (from != to)
                row.insert(to->objectName(), from->distanceTo(to));
        }
        world.distances.insert(from->objectName(), row);
    }
    foreach (const int cardId, m_room.getDiscardPile()) {
        if (const Card *card = Sanguosha->getCard(cardId))
            world.discardPile << makeAICardView(card);
    }
    foreach (const AIEventView &event, m_events) {
        AIEventView visible = event;
        if (visible.privateViewer != viewer->objectName())
            visible.privateCardIds.clear();
        visible.privateViewer.clear();
        world.events << visible;
    }
    AiLuaRuntime::evaluateModePolicy(m_room.roomRuntime()->lua(), world);
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
    request.worldView = buildWorldView(player);
    if (player && (kind == AIRequest::Activate || kind == AIRequest::UseCard
                   || kind == AIRequest::RespondCard)) {
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        foreach (const SkillInstance &instance, player->getSkillInstances()) {
            AiSkillActionContext actionContext;
            if (buildSkillActionContext(player, instance, reason, pattern, actionContext))
                request.skillActions << actionContext;
        }
        foreach (const Card *card, player->getHandcards())
            request.cardCandidates << makeAICardCandidate(m_room, player, card, method);
        foreach (const Card *card, player->getEquips())
            request.cardCandidates << makeAICardCandidate(m_room, player, card, method);
    }
    return request;
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

Card *AiDecisionCoordinator::buildSpecCard(ServerPlayer *player, const AICardSpec &spec) const
{
    // Card construction is the authority's job. The AI only names an engine card, the
    // suit and number to clone it with, the view-as skill and the cards it pays.
    if (!player || !spec.isValid()) return nullptr;
    if (spec.suit < int(Card::SuitToBeDecided) || spec.suit > int(Card::NoSuit))
        return nullptr;
    if (spec.number < 0 || spec.number > 13) return nullptr;
    if (!spec.skillName.isEmpty() && !player->hasSkill(spec.skillName)
        && !Sanguosha->getViewAsSkill(spec.skillName))
        return nullptr;
    // Every paid card must be one this player actually holds right now.
    const QList<int> handCards = player->handCards();
    foreach (const int cardId, spec.subcardIds) {
        if (handCards.contains(cardId))
            continue;
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
    Card *built = Sanguosha->cloneCard(spec.name, Card::Suit(spec.suit), spec.number);
    if (!built)
        return nullptr;
    if (!spec.skillName.isEmpty())
        built->setSkillName(spec.skillName);
    if (!spec.subcardIds.isEmpty())
        built->addSubcards(spec.subcardIds);
    return built;
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
        candidate.card = played;
        cardUse = candidate;
        return true;
    }
    if (result.action.hasCardSpec) {
        Card *built = buildSpecCard(player, result.action.cardSpec);
        if (!built) return false;
        if (request.hasSkillActionContext) {
            const AiSkillActionContext &context = request.skillActionContext;
            candidate.hasSkillActivationRequest = true;
            candidate.activationRef = context.activationRef;
            candidate.sourceRef = context.sourceRef;
            built->setActivationSkill(context.getActivationSkillName(),
                context.getActivationInstanceId());
            built->setSourceSkill(context.getSourceSkillName(),
                context.getSourceInstanceID());
        }
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
    const AiRoute route = m_room.roomRuntime()->ai().routes().routeFor(
        request.kind, callbackName, request.choiceOptions.reason);
    bool isolatedAnswer = false;
    if (route == AiRouteIsolated) {
        result = m_room.roomRuntime()->ai().decideShadow(request);
        // An isolated answer counts only while it still belongs to this decision and
        // this board state. A stale one is dropped and the legacy AI answers instead;
        // the stamp is never rewritten to make an old answer acceptable.
        isolatedAnswer = result.handled && result.errorCode.isEmpty()
            && result.kind != AIResult::UseCard
            && result.decisionId == request.decisionId
            && result.stateRevision == request.stateRevision
            && request.stateRevision == m_room.roomRuntime()->stateRevision();
        if (!isolatedAnswer) {
            m_room.roomRuntime()->ai().recordLegacyFallback(callbackName);
            result = legacy(request);
        }
    } else {
        result = legacy(request);
        if (route == AiRouteShadow) {
            const AIResult shadowResult = m_room.roomRuntime()->ai().decideShadow(request);
            m_room.roomRuntime()->ai().recordShadowAudit(request, callbackName,
                request.choiceOptions.reason, result, shadowResult);
        }
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
    AI *ai = player->getAI();
    AIResult result;
    if (!runAnswer(player, makeChoiceRequest(player, AIRequest::SkillInvoke, options),
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
    AI *ai = player->getAI();
    AIResult result;
    if (!runAnswer(player, makeChoiceRequest(player, AIRequest::Choice, options),
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
    if (result.kind != AIResult::Answer) return false;
    if (fromIsolated) {
        // Only cards the question offered may come back, in the amount it allows.
        foreach (const int cardId, result.action.selectedCardIds) {
            if (!candidates.contains(cardId))
                return false;
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
    // The owner and the zone letters cross; the ids do not, because the hand of
    // another player is not projected. Candidates land with the card zone batch.
    options.playerNames << who->objectName();
    options.choices << flags;
    AIRequest request = makeChoiceRequest(player, AIRequest::CardChosen, options);
    request.handlingMethod = method;
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
    if (result.kind != AIResult::Answer) return false;
    QList<ServerPlayer *> picked;
    foreach (const QString &name, result.action.selectedTargetNames) {
        ServerPlayer *match = nullptr;
        foreach (ServerPlayer *target, targets) {
            if (target && target->objectName() == name) {
                match = target;
                break;
            }
        }
        if (!match || picked.contains(match))
            return false; // Unknown or repeated targets are refused, not trimmed.
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
    foreach (const int cardId, cards) {
        if (!seen.contains(cardId))
            return false;
    }
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
    const AiRoute route = m_room.roomRuntime()->ai().routes().routeFor(
        request.kind, callbackName, request.choiceOptions.reason);
    if (route == AiRouteIsolated) {
        const AIResult result = m_room.roomRuntime()->ai().decideShadow(request);
        const Card *answered = responseCard(player, request, result);
        if (answered)
            return answered;
        // Unhandled, stale or unowned answers keep the legacy card, pointer and all.
        m_room.roomRuntime()->ai().recordLegacyFallback(callbackName);
        return legacy();
    }
    const Card *official = legacy();
    if (route == AiRouteShadow) {
        AIResult officialResult;
        officialResult.decisionId = request.decisionId;
        officialResult.stateRevision = request.stateRevision;
        officialResult.handled = true;
        if (official) {
            officialResult.kind = AIResult::Answer;
            // Audit only: the identity is the effective id, virtual cards keep their string.
            if (official->isVirtualCard())
                officialResult.action.userString = official->toString();
            else
                officialResult.action.selectedCardIds << official->getEffectiveId();
        }
        const AIResult shadowResult = m_room.roomRuntime()->ai().decideShadow(request);
        m_room.roomRuntime()->ai().recordShadowAudit(request, callbackName,
            request.choiceOptions.reason, officialResult, shadowResult);
    }
    return official;
}

const Card *AiDecisionCoordinator::responseCard(ServerPlayer *player, const AIRequest &request,
                                                const AIResult &result) const
{
    if (!player || !result.handled || !result.errorCode.isEmpty()) return nullptr;
    if (result.kind != AIResult::Answer || result.action.selectedCardIds.size() != 1)
        return nullptr;
    if (result.decisionId != request.decisionId
        || result.stateRevision != request.stateRevision
        || request.stateRevision != m_room.roomRuntime()->stateRevision())
        return nullptr;
    // A response answers with one card the player actually holds. View-as conversions
    // need the value-typed card builder, so they are not accepted here yet.
    const int cardId = result.action.selectedCardIds.first();
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
    return Sanguosha->getCard(cardId);
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
    options.optional = true;
    // The viewer's own cards are already in the world view; only their ids may return.
    if (player)
        options.cardIds = player->handCards();
    AIRequest request = makeChoiceRequest(player, AIRequest::RespondCard, options);
    request.pattern = pattern;
    request.prompt = prompt;
    request.handlingMethod = method;
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
    return decideResponse(player,
        makeResponseRequest(player, QStringLiteral("askForCard"), pattern, pattern, prompt,
                            method),
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
    const AiRoute route = m_room.roomRuntime()->ai().routes().routeFor(request.kind,
        callbackName, skillName);
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
    if (route == AiRouteIsolated) {
        result = m_room.roomRuntime()->ai().decideShadow(request);
        if (!result.handled || !result.errorCode.isEmpty()) {
            m_room.roomRuntime()->ai().recordLegacyFallback(callbackName);
            result = player->getAI()->decide(request);
        }
    } else {
        result = player->getAI()->decide(request);
        if (route == AiRouteShadow) {
            const AIResult shadowResult = m_room.roomRuntime()->ai().decideShadow(request);
            m_room.roomRuntime()->ai().recordShadowAudit(request, callbackName, skillName,
                result, shadowResult);
        }
    }
    if (route == AiRouteLegacyAdapted) {
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
    if (!player || !player->getAI()) return false;
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
