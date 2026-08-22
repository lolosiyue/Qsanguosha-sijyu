#include "ai-decision-coordinator.h"

#include "engine.h"
#include "room.h"
#include "server-info.h"
#include "skill-runtime-coordinator.h"

#include <cmath>
#include <QJsonArray>

namespace {

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

    switch (source.metaType().id()) {
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

AIWorldView AiDecisionCoordinator::buildWorldView(ServerPlayer *viewer) const
{
    AIWorldView world;
    world.revision = m_room.roomRuntime()->stateRevision();
    ServerPlayer *current = m_room.getCurrent();
    world.currentPlayer = current ? current->objectName() : QString();
    world.currentPhase = current ? int(current->getPhase()) : int(Player::NotActive);

    const bool hegemony = ServerInfo.EnableHegemony;
    foreach (ServerPlayer *player, m_room.getAllPlayers(true)) {
        AIPlayerView playerView;
        playerView.objectName = player->objectName();
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

        const bool seesIdentity = player == viewer || !hegemony
            || player->hasShownOneGeneral() || player->isDead();
        if (seesIdentity)
            playerView.kingdom = player->getKingdom();
        if (player == viewer || player->hasShownRole() || player->isLord() || player->isDead())
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
    return request;
}

bool AiDecisionCoordinator::buildSkillActionRequest(
    ServerPlayer *player, const SkillInstance &instance,
    CardUseStruct::CardUseReason reason, const QString &pattern,
    const QString &prompt, Card::HandlingMethod method, AIRequest &aiRequest) const
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

    aiRequest = makeRequest(player, AIRequest::UseCard, reason, pattern, prompt, method);
    aiRequest.hasSkillActionContext = true;
    aiRequest.skillActionContext.activationRef = request.activationRef;
    aiRequest.skillActionContext.sourceRef = context.sourceRef;
    aiRequest.skillActionContext.activationQuotaAvailable = quotaAvailable;
    aiRequest.skillActionContext.sourceQuotaAvailable = quotaAvailable;
    return true;
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

    if (!request.hasSkillActionContext || !result.action.hasSkillActionContext)
        return false;
    const AiSkillActionContext &context = request.skillActionContext;
    if (result.action.skillActionContext.activationRef != context.activationRef
        || result.action.skillActionContext.sourceRef != context.sourceRef)
        return false;
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

bool AiDecisionCoordinator::decide(ServerPlayer *player, const AIRequest &request,
                                   CardUseStruct &cardUse) const
{
    if (!player || !player->getAI()) return false;
    const QString callbackName = request.kind == AIRequest::Activate
        ? QStringLiteral("activate") : QStringLiteral("askForUseCard");
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
        if (!result.handled || !result.errorCode.isEmpty())
            result = player->getAI()->decide(request);
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
