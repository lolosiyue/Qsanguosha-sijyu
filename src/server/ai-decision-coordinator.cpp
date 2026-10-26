#include "external-agent.h"
#include "ai-decision-coordinator.h"
#include "qt-collection-utils.h"
#include "ai-runtime.h"
#include "card-lifetime-manager.h"
#include "lua.hpp"

#include "engine.h"
#include "hegemony-mode.h"
#include "room.h"
#include "roomthread.h"
#include "server-info.h"
#include "settings.h"
#include "skill-runtime-coordinator.h"
#include "standard.h"
#include "maneuvering.h"
#include "wrapped-card.h"

#include <cmath>
#include <typeinfo>
#include <QJsonArray>
#include "ai-probe.h"
#include "skill-set-generation.h"
#include <QElapsedTimer>
#include <QLoggingCategory>

namespace {
// The event log is bounded so a long game cannot grow the snapshot without limit.
const int AiEventLogLimit = 64;
// Above this many alive players isolated faults are contained instead of handed to SmartAI.
const int AiLargeRoomAlive = 16;
}

// Hegemony decisions must expose the distinction between an isolated envelope
// that passed its stamp checks and the authority domain that accepted it.  Keep
// this audit local to the coordinator so no legacy/native callback can be
// mistaken for an isolated success by the observation stream.
class AiDecisionAudit
{
public:
    AiDecisionAudit(const Room &room, const ServerPlayer *player,
                    const AIRequest &request, const QString &callback)
        : m_enabled(Config.EnableHegemony), m_request(request), m_callback(callback),
          m_player(player ? player->objectName() : QStringLiteral("?")),
          m_source(room.externalAgent(m_player) ? QStringLiteral("external_or_isolated")
                                                : QStringLiteral("isolated")),
          m_route(Config.EnableAI
              ? routeName(const_cast<Room &>(room).roomRuntime()->ai().routes().routeFor(
                  request.kind, callback, request.choiceOptions.reason))
              : QStringLiteral("legacy_direct"))
    {
    }

    ~AiDecisionAudit()
    {
        if (!m_enabled) return;
        if (!m_accepted && m_error.isEmpty()) {
            m_stage = QStringLiteral("authority");
            m_error = QStringLiteral("authority_rejected");
        }
        qInfo().noquote() << QString("[AI_HEGEMONY_DECISION] decision=%1 revision=%2 kind=%3 "
                                    "callback=%4 reason=%5 route=%6 source=%7 stage=%8 result=%9 "
                                    "error=%10 accepted=%11")
            .arg(m_request.decisionId)
            .arg(m_request.stateRevision)
            .arg(int(m_request.kind))
            .arg(m_callback)
            .arg(m_request.choiceOptions.reason)
            .arg(m_route)
            .arg(m_source)
            .arg(m_stage)
            .arg(m_result)
            .arg(m_error)
            .arg(m_accepted ? QStringLiteral("true") : QStringLiteral("false"));
    }

    void setSource(const QString &source) { m_source = source; }

    void envelope(const AIResult &result)
    {
        m_stage = QStringLiteral("envelope");
        m_result = QString::number(int(result.kind));
        m_error = result.errorCode;
    }

    void reject(const QString &error)
    {
        m_stage = QStringLiteral("authority");
        m_error = error;
        m_accepted = false;
    }

    void accept(const QString &result)
    {
        m_stage = QStringLiteral("authority");
        m_result = result;
        m_error.clear();
        m_accepted = true;
    }

private:
    static QString routeName(AiRoute route)
    {
        switch (route) {
        case AiRouteLegacyDirect: return QStringLiteral("legacy_direct");
        case AiRouteLegacyAdapted: return QStringLiteral("legacy_adapted");
        case AiRouteIsolated: return QStringLiteral("isolated");
        }
        return QStringLiteral("unknown");
    }

    bool m_enabled;
    const AIRequest &m_request;
    QString m_callback;
    QString m_player;
    QString m_route;
    QString m_source;
    QString m_stage = QStringLiteral("authority");
    QString m_result = QStringLiteral("none");
    QString m_error;
    bool m_accepted = false;
};

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
    const auto projectionRevision = room.roomRuntime()->stateRevision();
    const auto projectionSkillGeneration = SkillSet::generation();
    const auto projectionDefinitions = room.roomRuntime()->definitions().skillDefinitionVersion();
    const auto alive = room.getAlivePlayers();
    const auto sourceFlags = player->getFlagList();
    QList<QStringList> aliveFlags;
    for (const auto *target : alive) aliveFlags << target->getFlagList();
    const Card *real = card->getRealCard();
    const auto cardFlags = card->getFlags();
    const auto realFlags = real ? real->getFlags() : QStringList();
    const auto stableProjection = [&]() {
        if (room.roomRuntime()->stateRevision() != projectionRevision
            || SkillSet::generation() != projectionSkillGeneration
            || room.roomRuntime()->definitions().skillDefinitionVersion() != projectionDefinitions
            || room.getAlivePlayers() != alive || player->getFlagList() != sourceFlags
            || card->getRealCard() != real || card->getFlags() != cardFlags
            || (real && real->getFlags() != realFlags)) return false;
        for (int i = 0; i < alive.size(); ++i)
            if (alive.at(i)->getFlagList() != aliveFlags.at(i)) return false;
        return true;
    };
    targetFixed = card->targetFixed();
    completeCoverage = false;
    legalTargets.clear();
    maxVotes.clear();
    combinations.clear();
    // Operators/tests may lower the budget; the hard ceiling cannot be raised.
    int probesLeft = qBound(1, Config.value(QStringLiteral("AiTargetProjectionBudget"), 2048).toInt(), 2048);
    const auto spend = [&]() { return --probesLeft >= 0 && --requestBudget >= 0; };
    if (!spend()) return;
    feasibleWithNoTarget = card->targetsFeasible({}, player);
    if (!stableProjection()) return;
    if (targetFixed) {
        if (feasibleWithNoTarget) combinations << QStringList();
        completeCoverage = true;
        return;
    }
    // Only these exact native implementations share Slash::targetFilter. A
    // subclass can override it while retaining the printed name and QObject
    // metadata, so neither name nor qobject_cast alone proves a terminal prefix.
    const bool nativeWrapper = card == real || typeid(*card) == typeid(WrappedCard);
    const bool nativeSlash = nativeWrapper && real && (typeid(*real) == typeid(Slash)
        || typeid(*real) == typeid(NatureSlash) || typeid(*real) == typeid(FireSlash)
        || typeid(*real) == typeid(ThunderSlash));
    qint64 terminalCount = -1;
    if (nativeSlash) {
        // Slash may gain global or candidate-specific V2 extra targets. Query
        // the same authority for every possible next target before pruning;
        // prefix-dependent prohibitions and ordinary filters still run below.
        if (!spend()) return;
        const qint64 globalCount = qint64(1)
            + Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, player, real);
        if (!stableProjection()) return;
        terminalCount = qMax(qint64(0),globalCount);
        for (ServerPlayer *target : alive) {
            if (!spend()) return;
            const qint64 candidateCount = qint64(1)
                + Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, player, real, target);
            terminalCount = qMax(terminalCount,candidateCount);
            if (!stableProjection()) return;
        }
    }
    QList<const Player *> prefix;
    QStringList names;
    // Preserve order. Two individually legal first targets need not form a legal pair.
    // Current submit protocol rejects repeated names, so vote-repeating cards stay
    // unsupported instead of claiming their distinct-name subset is complete.
    std::function<bool()> visit = [&]() {
        if (!stableProjection()) return false;
        if (!spend()) return false;
        if (card->targetsFeasible(prefix, player)) {
            if (combinations.size() >= 128) return false;
            combinations << names;
        }
        // Beyond this exact native upper bound, every next target is rejected.
        // Avoid rescanning all 50 seats at each otherwise terminal leaf.
        if (terminalCount >= 0 && prefix.size() >= terminalCount) return true;
        // Scan the whole level before descending, so the first level always lists
        // every legal target. Each next target's subtree costs at least its own visit
        // plus, below the native bound, one filter probe per seat; a level that cannot
        // fit the remaining budget fails now instead of after spending it.
        QList<ServerPlayer *> next;
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
            next << target;
        }
        const qint64 childScan = terminalCount >= 0 && prefix.size() + 1 >= terminalCount
            ? 0 : alive.size();
        if (qint64(next.size()) * (1 + childScan) > qMin(probesLeft, requestBudget)) return false;
        for (ServerPlayer *target : next) {
            prefix << target;
            names << target->objectName();
            if (!visit()) return false;
            prefix.removeLast();
            names.removeLast();
        }
        return true;
    };
    completeCoverage = visit() && stableProjection();
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
    candidate.extraTargetKnown = true;
    candidate.extraTargetCount = Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, player, card);
    for (ServerPlayer *target : room.getAlivePlayers()) {
        candidate.extraTargetByPlayer.insert(target->objectName(),
            Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, player, card, target));
        candidate.targetFilterEmpty.insert(target->objectName(), card->targetFilter({}, target, player));
    }
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

// Whether viewer may see card id where it lies now, by the world view's rules.
static bool cardVisibleTo(const Room &room, int id, const ServerPlayer *viewer)
{
    const ServerPlayer *owner = room.getCardOwner(id);
    if (owner == viewer) return true;
    switch (room.getCardPlace(id)) {
    case Player::PlaceEquip: case Player::PlaceDelayedTrick:
    case Player::DiscardPile: case Player::PlaceTable:
        return true;
    case Player::PlaceSpecial:
        return owner && owner->pileOpen(owner->getPileName(id), viewer->objectName());
    case Player::PlaceHand: {
        const Card *card = Sanguosha->getCard(id);
        return owner && (viewer->canSeeHandcard(owner) || (card && (card->hasFlag(QStringLiteral("visible"))
            || card->hasFlag(QStringLiteral("visible_%1_%2").arg(viewer->objectName(), owner->objectName())))));
    }
    default:
        return false;
    }
}

// Material a V2 skill may take beyond the player's own "he", as Room accepts it from
// a human: the hand pile when the skill answers a response or use, and the expand
// pile. A card the viewer cannot see is left out and clears visible: a ticket would
// disclose it.
static QList<int> extraConversionMaterialIds(const Room &room, const ServerPlayer *player,
                                             const ViewAsSkill *skill, bool &visible)
{
    QList<int> ids;
    if (skill->isResponseOrUse()) ids << player->getHandPile();
    ids << skill->getExpandPileCardIds(player);
    // A "#pile" also offers the cards its owner was shown, kept in "<pile>ForAI".
    QSet<int> shown;
    foreach (const QString &pile, skill->getExpandPile().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        if (!pile.startsWith(QLatin1Char('#'))) continue;
        foreach (const QVariant &id, player->getTag(pile.mid(1) + QStringLiteral("ForAI")).toList())
            shown.insert(id.toInt());
    }
    QList<int> result;
    for (int id : ids) {
        if (result.contains(id)) continue;
        if (shown.contains(id) || cardVisibleTo(room, id, player)) result << id;
        else visible = false;
    }
    return result;
}

// SmartAI's askForCard discovers response classes from the viewer's own cards: the
// pattern's single hand/equipped place (else "he"), then hand piles, before any
// limit or availability filter. A Slash subclass counts as Slash. The fallback scan
// of askForUseCard (use) always reads "he" and matches with matchExpPattern.
static void projectResponseClassOrder(ServerPlayer *player, AIRequest &request, bool use)
{
    QString pattern = request.pattern;
    if (pattern.endsWith(QLatin1Char('!')))
        pattern.chop(1);
    const auto matches = [&](const Card *card) {
        return use ? Sanguosha->matchExpPattern(pattern, player, card)
                   : Sanguosha->matchPattern(pattern, player, card);
    };
    QString source = QStringLiteral("he");
    const QStringList fields = pattern.split(QLatin1Char('|'));
    if (!use && fields.size() >= 4) {
        const QStringList places = fields.at(3).split(QLatin1Char(','));
        if (places.size() == 1 && places.first() == QStringLiteral("hand"))
            source = QStringLiteral("h");
        else if (places.size() == 1 && places.first() == QStringLiteral("equipped"))
            source = QStringLiteral("e");
    }
    QList<const Card *> cards = player->getCards(source);
    for (int id : player->getHandPile())
        cards << Sanguosha->getCard(id);
    request.physicalClassOrderComplete = true;
    for (const Card *card : cards) {
        if (!card) {
            request.physicalClassOrderComplete = false;
            continue;
        }
        const QString className = card->isKindOf("Slash") ? QStringLiteral("Slash") : card->getClassName();
        if (!request.physicalClassOrder.contains(className)
            && (pattern.contains(card->objectName()) || matches(card)))
            request.physicalClassOrder << className;
    }
    // Then patterns(true): each engine card class under its last registered name,
    // kept when the name is in the pattern or its prototype matches it. Only a
    // conversion can answer from this pass.
    if (request.cardConversions.isEmpty())
        return;
    QStringList classes;
    QHash<QString, QString> names;
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (!card) continue;
        if (!names.contains(card->getClassName())) classes << card->getClassName();
        names.insert(card->getClassName(), card->objectName());
    }
    for (const QString &className : classes) {
        const QString name = names.value(className);
        bool matched = !name.isEmpty() && pattern.contains(name);
        if (!matched) {
            if (Card *prototype = Sanguosha->cloneCard(name)) {
                matched = matches(prototype);
                prototype->deleteLater();
            }
        }
        if (matched)
            request.catalogClassOrder.append(QJsonObject{{"class_name", className},
                {"card_name", name}, {"matches_response_pattern", true}});
    }
    request.catalogClassOrderComplete = true;
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
    if (!card->isVirtualCard())
        view.legacyStringKey = card->toString();
    view.nameLength = card->nameLength();
    view.sourceSkillName = card->getSourceSkillName();
    view.canRecast = card->canRecast();
    view.willThrow = card->willThrow();
    view.singleTarget = card->isSingleTargetCard();
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
    view.flags = card->getFlags();
    return view;
}

// The owner's rules for a card it holds: availability, target corrections, limits
// (inHand also in the hand) and prohibitions.
static void projectOwnCardRules(AICardView &view, const ServerPlayer *owner, const Card *card, bool inHand)
{
    view.ownerRulesKnown = true;
    view.available = card->isAvailable(owner);
    view.targetModResidue = Sanguosha->correctCardTarget(TargetModSkill::Residue, owner, card);
    view.targetModDistance = Sanguosha->correctCardTarget(TargetModSkill::DistanceLimit, owner, card);
    view.targetModExtra = Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, owner, card);
    for (int method = int(Card::MethodUse); method <= int(Card::MethodIgnore); ++method) {
        view.limits.insert(method, owner->isCardLimited(card, Card::HandlingMethod(method), false));
        if (inHand)
            view.limitsHand.insert(method, owner->isCardLimited(card, Card::HandlingMethod(method), true));
    }
    for (const ServerPlayer *target : owner->getRoom()->getAlivePlayers())
        view.prohibited.insert(target->objectName(),
                               owner->getRoom()->isProhibited(owner, target, card) != nullptr);
    if ((card->targetFixed() && card->getTypeId() < Card::TypeTrick) || card->isDamageCard()) {
        const QList<ServerPlayer *> targets = card->targetFixed()
            ? QList<ServerPlayer *>{const_cast<ServerPlayer *>(owner)}
            : owner->getRoom()->getAlivePlayers();
        for (const ServerPlayer *target : targets)
            view.targetResidue.insert(target->objectName(),
                Sanguosha->correctCardTarget(TargetModSkill::Residue, owner, card, target));
    }
}

// What only the owner knows about one of its own physical cards. asHandCard also
// projects the in-hand limits of a card the owner is about to take. withRules false
// leaves out the owner rules, which cost about one rule query per alive player.
static void projectOwnCardState(AICardView &view, const ServerPlayer *owner, bool asHandCard = false,
                                bool withRules = true)
{
    const Card *card = Sanguosha->getCard(view.effectiveId);
    if (!card || card->isVirtualCard())
        return;
    view.ownerStateKnown = true;
    view.skillNames = card->getSkillNames();
    view.locked = owner->isLocked(card);
    view.jilei = owner->isJilei(card);
    view.usingCard = card->hasFlag(QStringLiteral("using"));
    if (const Card *engineCard = Sanguosha->getEngineCard(view.effectiveId)) {
        view.engineClassName = engineCard->getClassName();
        view.gift = engineCard->isGift() && engineCard->objectName() == card->objectName();
    }
    if (withRules)
        projectOwnCardRules(view, owner, card, asHandCard || owner->handCards().contains(view.effectiveId));
}

static QJsonObject publicDecisionCard(const Card *card)
{
    if (!card) return {};
    // A declared/judgement card is public; its backing IDs and subcards are not
    // needed by a policy and must never expose a hidden payment card.
    // A physical card being used or judged is on the table; its id is public.
    const int publicId = card->isVirtualCard() ? -1 : card->getEffectiveId();
    QJsonObject result{{"name", card->objectName()}, {"class_name", card->getClassName()},
        {"id", publicId}, {"effective_id", publicId},
        {"target_fixed", card->targetFixed()}, {"virtual_card", card->isVirtualCard()},
        {"kind_of", QJsonArray::fromStringList(card->getKindOfNames())},
        {"type_id", int(card->getTypeId())}, {"suit", int(card->getSuit())},
        {"subtype", card->getSubtype()},
        {"damage_card", card->isDamageCard()},
        {"skill_name", card->getSkillName(false)},
        {"skill_names", QJsonArray::fromStringList(card->getSkillNames())},
        {"skill_names_complete", true},
        {"flags", QJsonArray::fromStringList(card->getFlags())}, // As makeAICardView.
        {"number", card->getNumber()}, {"red", card->isRed()}, {"black", card->isBlack()}};
    if (const auto *weapon = dynamic_cast<const Weapon *>(card))
        result.insert("weapon_range", weapon->getRange());
    // Public card flags (for example zhashicardflag) are part of the declared
    // card, not a hidden hand. Empty means no flag, not an unknown set.
    QJsonArray flags;
    for (const QString &flag : card->getFlags())
        flags.append(flag);
    if (!flags.isEmpty())
        result.insert(QStringLiteral("flags"), flags);
    return result;
}

static QJsonValue playerName(const ServerPlayer *player)
{
    return player ? QJsonValue(player->objectName()) : QJsonValue(QString());
}

// Tags outlive what their raw pointers name. Follow a pointer only while the room
// still owns it: a seated player, a physical card, or a card the lifetime manager
// tracks as live. Nothing can vouch for a JudgeStruct* or DamageStruct*.
struct AIStateRefs
{
    QSet<const void *> players;
    mutable QSet<const void *> cards; // Physical cards, collected on first use.
    bool player(const void *value) const { return !value || players.contains(value); }
    bool card(const Card *value) const
    {
        if (!value || globalCardLifetimeManager().isLive(value)) return true;
        if (cards.isEmpty()) {
            for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
                cards.insert(Sanguosha->getCard(id));
                cards.insert(Sanguosha->getEngineCard(id));
            }
        }
        return cards.contains(value);
    }
};

static AIStateRefs liveStateRefs(const Room &room)
{
    AIStateRefs refs;
    for (const ServerPlayer *player : room.getPlayers()) refs.players.insert(player);
    return refs;
}

static QJsonObject damageValue(const DamageStruct &damage)
{
    return QJsonObject{{"from", playerName(damage.from)}, {"to", playerName(damage.to)},
        {"amount", damage.damage}, {"nature", int(damage.nature)}, {"reason", damage.reason},
        {"card", publicDecisionCard(damage.card)},
        {"card_name", damage.card ? damage.card->objectName() : QString()}};
}

// One tag or property value. Gameplay structs become the same value objects the
// decision context uses; anything else goes through the JSON-safe state rules.
static bool makeAIOwnStateValue(const QVariant &source, QJsonValue &target, int &remaining,
                                const AIStateRefs &refs)
{
    if (remaining <= 0) return false;
    // Inspect exact-type payloads in place: copying a history struct can register
    // stale auxiliary Card pointers before refs validates the projected fields.
    if (source.userType() == qMetaTypeId<CardUseStruct>()) {
        const CardUseStruct &use = *static_cast<const CardUseStruct *>(source.constData());
        if (!refs.player(use.from) || !refs.card(use.card)) return false;
        for (const ServerPlayer *to : use.to) if (!refs.player(to)) return false;
        QJsonArray targets;
        for (const ServerPlayer *to : use.to) targets.append(playerName(to));
        target = QJsonObject{{"from", playerName(use.from)}, {"to", targets},
            {"card", publicDecisionCard(use.card)},
            {"nullified_list", QJsonArray::fromStringList(use.nullified_list)}};
    } else if (source.userType() == qMetaTypeId<DamageStruct>()) {
        const DamageStruct &damage = *static_cast<const DamageStruct *>(source.constData());
        if (!refs.player(damage.from) || !refs.player(damage.to) || !refs.card(damage.card)) return false;
        target = damageValue(damage);
    } else if (source.userType() == qMetaTypeId<CardEffectStruct>()) {
        const CardEffectStruct &effect = *static_cast<const CardEffectStruct *>(source.constData());
        if (!refs.player(effect.from) || !refs.player(effect.to) || !refs.card(effect.card)) return false;
        target = QJsonObject{{"from", playerName(effect.from)}, {"to", playerName(effect.to)},
            {"card", publicDecisionCard(effect.card)},
            {"card_name", effect.card ? effect.card->objectName() : QString()},
            {"offset_num", effect.offset_num}, {"nullified", effect.nullified},
            {"no_respond", effect.no_respond}, {"no_offset", effect.no_offset}};
    } else if (source.userType() == qMetaTypeId<DyingStruct>()) {
        const DyingStruct dying = source.value<DyingStruct>();
        if (!refs.player(dying.who)) return false;
        target = QJsonObject{{"who", playerName(dying.who)}};
    } else if (source.userType() == qMetaTypeId<RecoverStruct>()) {
        const RecoverStruct &recover = *static_cast<const RecoverStruct *>(source.constData());
        if (!refs.player(recover.who) || !refs.card(recover.card)) return false;
        target = QJsonObject{{"who", playerName(recover.who)}, {"recover", recover.recover},
            {"reason", recover.reason}, {"card", publicDecisionCard(recover.card)}};
    } else if (source.userType() == qMetaTypeId<CardsMoveOneTimeStruct>()) {
        const CardsMoveOneTimeStruct move = source.value<CardsMoveOneTimeStruct>();
        if (!refs.player(move.from) || !refs.player(move.to)) return false;
        // Card ids only for a public destination, as in the event log.
        const bool publicDestination = move.to_place == Player::PlaceEquip
            || move.to_place == Player::PlaceDelayedTrick
            || move.to_place == Player::DiscardPile || move.to_place == Player::PlaceTable;
        QJsonArray ids;
        if (publicDestination) for (int id : move.card_ids) ids.append(id);
        target = QJsonObject{{"from", playerName(qobject_cast<const ServerPlayer *>(move.from))},
            {"to", playerName(qobject_cast<const ServerPlayer *>(move.to))},
            {"to_place", int(move.to_place)}, {"card_ids", ids},
            {"reason", QJsonObject{{"reason", move.reason.m_reason},
                {"skill_name", move.reason.m_skillName}}}};
    } else if (source.userType() == qMetaTypeId<ServerPlayer *>()) {
        if (!refs.player(source.value<ServerPlayer *>())) return false;
        target = playerName(source.value<ServerPlayer *>());
    } else if (source.userType() == qMetaTypeId<QList<int>>()) {
        QJsonArray ids;
        for (int id : source.value<QList<int>>()) ids.append(id);
        target = ids;
    } else if (source.userType() == qMetaTypeId<const Card *>()) {
        if (!refs.card(source.value<const Card *>())) return false;
        target = publicDecisionCard(source.value<const Card *>());
    } else if (source.userType() == qMetaTypeId<JudgeStruct *>()) {
        return false;
    } else {
        return makeAIStateValue(source, target, 0, remaining);
    }
    --remaining;
    return true;
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
        const bool publicDestination = move.to_place == Player::PlaceEquip
            || move.to_place == Player::PlaceDelayedTrick
            || move.to_place == Player::DiscardPile || move.to_place == Player::PlaceTable;
        if (publicDestination) {
            event.cardIds = move.card_ids;
        } else if (move.to_place == Player::PlaceHand || move.to_place == Player::PlaceSpecial) {
            event.privateCardIds = move.card_ids;
            event.privateViewer = event.to;
        }
    } else if (data.canConvert<CardEffectStruct>()) {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        event.kind = QStringLiteral("card_effect");
        event.from = effect.from ? effect.from->objectName() : QString();
        event.to = effect.to ? effect.to->objectName() : QString();
        event.details.insert("offset_num", effect.offset_num);
        event.details.insert("no_respond", effect.no_respond);
        event.details.insert("no_offset", effect.no_offset);
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
    if (triggerEvent == ChoiceMade && data.userType() == QMetaType::QString) {
        // Admit public choices only. Serialized response/Yiji card IDs may be
        // hidden, so never copy the raw ChoiceMade string into a public event.
        const QStringList choice = data.toString().split(QChar(':'));
        const QString kind = choice.value(0);
        if (kind == QStringLiteral("playerChosen") || kind == QStringLiteral("Yiji")) {
            event.kind = QStringLiteral("choice");
            event.from = target ? target->objectName() : QString();
            event.reason = choice.value(1);
            event.details.insert("choice_kind", kind);
            // playerChosen carries only player names; Yiji also carries card IDs.
            if (kind == QStringLiteral("playerChosen"))
                event.details.insert("prompt_list", QJsonArray::fromStringList(choice));
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
            const auto *observation = m_room.m_aiChoiceContext;
            if (kind == QStringLiteral("skillChoice") && event.reason == QStringLiteral("known_both")
                && observation && observation->payload == &data && observation->input
                && observation->player == target && observation->reason == event.reason
                && observation->answer == choice.value(2)
                && observation->input->canConvert<ServerPlayer *>()) {
                const auto *revealed = observation->input->value<ServerPlayer *>();
                if (revealed && liveStateRefs(m_room).player(revealed))
                    event.details.insert(QStringLiteral("original_known_both_target"), revealed->objectName());
            }
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
    // Share the same revision-scoped public facts with decision requests.
    for (ServerPlayer *observer : m_room.getAlivePlayers()) {
        if (event.privateEvent && event.privateViewer != observer->objectName()) continue;
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

AiDecisionCoordinator::PublicBoard AiDecisionCoordinator::publicBoard() const
{
    static const bool probe = qEnvironmentVariableIntValue("QSAN_AI_WV_PROBE") > 0;
    const quint64 revision = m_room.roomRuntime()->stateRevision();
    const quint64 generation = SkillSet::generation();
    const quint64 definitions = m_room.roomRuntime()->definitions().skillDefinitionVersion();
    const quint64 eventSequence = m_eventSequence;
    const auto players = m_room.getPlayers();
    const auto alivePlayers = m_room.getAlivePlayers();
    if (m_publicBoardValid && m_publicBoard.revision == revision
        && m_publicBoardGeneration == generation
        && m_publicBoardDefinitions == definitions
        && m_publicBoardPlayers == players && m_publicBoardAlivePlayers == alivePlayers) {
        if (probe) qInfo() << "[AI_WV] public_board hit revision=" << revision
                           << "event=" << eventSequence;
        return m_publicBoard;
    }

    // Invalidate before calling gameplay hooks. Keep a local value: a nested
    // request must not overwrite the snapshot an outer projection is using.
    m_publicBoardValid = false;
    QElapsedTimer timer;
    if (probe) timer.start();
    const PublicBoard board = buildPublicBoard();
    m_publicBoardValid = revision == m_room.roomRuntime()->stateRevision()
        && generation == SkillSet::generation()
        && definitions == m_room.roomRuntime()->definitions().skillDefinitionVersion()
        && players == m_room.getPlayers()
        && alivePlayers == m_room.getAlivePlayers();
    if (m_publicBoardValid) {
        m_publicBoard = board;
        m_publicBoardGeneration = generation;
        m_publicBoardDefinitions = definitions;
        m_publicBoardPlayers = players;
        m_publicBoardAlivePlayers = alivePlayers;
        ++m_publicBoardStamp;
    }
    if (probe) qInfo() << "[AI_WV] public_board build revision=" << revision
                       << "event=" << eventSequence << "ms=" << timer.elapsed()
                       << "reusable=" << m_publicBoardValid;
    return board;
}

bool AiDecisionCoordinator::publicBoardCurrent() const
{
    return m_publicBoardValid
        && m_publicBoard.revision == m_room.roomRuntime()->stateRevision()
        && m_publicBoardGeneration == SkillSet::generation()
        && m_publicBoardDefinitions == m_room.roomRuntime()->definitions().skillDefinitionVersion()
        && m_publicBoardPlayers == m_room.getPlayers()
        && m_publicBoardAlivePlayers == m_room.getAlivePlayers();
}

QHash<QString, QJsonObject> AiDecisionCoordinator::prototypeRules() const
{
    // Reused for exactly as long as the cached board it was built with.
    if (m_prototypeRulesValid && m_prototypeRulesStamp == m_publicBoardStamp && publicBoardCurrent())
        return m_prototypeRules;
    m_prototypeRulesValid = false;
    const quint64 stamp = m_publicBoardStamp;
    const QHash<QString, QJsonObject> rules = buildPrototypeRules();
    if (stamp == m_publicBoardStamp && publicBoardCurrent()) {
        m_prototypeRules = rules;
        m_prototypeRulesStamp = stamp;
        m_prototypeRulesValid = true;
    }
    return rules;
}

QMap<QString, QMap<QString, int>> AiDecisionCoordinator::lordDistances() const
{
    if (m_lordDistancesValid && m_lordDistancesStamp == m_publicBoardStamp && publicBoardCurrent())
        return m_lordDistances;
    m_lordDistancesValid = false;
    const quint64 stamp = m_publicBoardStamp;
    QMap<QString, QMap<QString, int>> rows;
    const auto alive = m_room.getAlivePlayers();
    for (ServerPlayer *lord : alive) {
        if (!lord->isLord()) continue;
        for (ServerPlayer *player : alive)
            if (player != lord) rows[player->objectName()].insert(lord->objectName(), player->distanceTo(lord));
    }
    if (stamp == m_publicBoardStamp && publicBoardCurrent()) {
        m_lordDistances = rows;
        m_lordDistancesStamp = stamp;
        m_lordDistancesValid = true;
    }
    return rows;
}

QHash<QString, QJsonObject> AiDecisionCoordinator::buildPrototypeRules() const
{
    EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
    LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
    // One clone of each basic prototype, released with this function.
    QList<const Card *> prototypes;
    for (const char *name : {"slash", "jink", "peach", "analeptic", "nullification"}) {
        if (Card *prototype = Sanguosha->cloneCard(QString::fromLatin1(name))) {
            prototype->deleteLater();
            prototypes << prototype;
        }
    }
    QHash<QString, QJsonObject> result;
    for (ServerPlayer *player : m_room.getAllPlayers(true)) {
        QJsonObject byClass;
        for (const Card *prototype : prototypes) {
            byClass.insert(prototype->getClassName(), QJsonObject{
                {"use_limited", player->isCardLimited(prototype, Card::MethodUse)},
                {"response_limited", player->isCardLimited(prototype, Card::MethodResponse)},
                {"available", prototype->isAvailable(player)},
                {"target_mod_residue", Sanguosha->correctCardTarget(TargetModSkill::Residue, player, prototype)},
                // Public sources only (hegemony): no use limit, as opposed to a large residue.
                {"residue_unlimited", Sanguosha->hasResidueUnlimited(player, prototype)},
                {"target_mod_distance", Sanguosha->correctCardTarget(TargetModSkill::DistanceLimit, player, prototype)},
                {"target_mod_extra", Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, player, prototype)}});
        }
        result.insert(player->objectName(), byClass);
    }
    return result;
}

AiDecisionCoordinator::PublicBoard AiDecisionCoordinator::buildPublicBoard() const
{
    EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
    // Native skill queries must use the same Room's gameplay callbacks.
    LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
    PublicBoard board;
    board.modeId = m_room.getMode();
    board.revision = m_room.roomRuntime()->stateRevision();
    ServerPlayer *current = m_room.getCurrent();
    board.currentPlayer = current ? current->objectName() : QString();
    board.currentPhase = current ? int(current->getPhase()) : int(Player::NotActive);
    foreach (ServerPlayer *player, m_room.getPlayers())
        board.playerOrder << player->objectName();
    foreach (ServerPlayer *player, m_room.getAlivePlayers())
        board.alivePlayerOrder << player->objectName();

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
    foreach (ServerPlayer *player, allPlayers) {
        PublicPlayer row;
        row.player = player;
        AIPlayerView &playerView = row.view;
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
        playerView.inYinniState = player->inYinniState();
        playerView.turnOpen = player->isTurnOpen();
        playerView.playOpen = player->isPlayOpen();
        // Room::setPlayerFlag broadcasts every flag change to all clients.
        playerView.privateFlagsVisible = true;
        playerView.privateFlags = player->getFlagList();
        for (int phase = int(Player::RoundStart); phase <= int(Player::Finish); ++phase)
            playerView.skippedPhases.insert(phase, player->isSkipped(Player::Phase(phase)));
        playerView.attackRangeWithoutWeapon = player->getAttackRange(false);
        playerView.hasJudgeArea = player->hasJudgeArea();
        for (int area = 0; area < 5; ++area)
            if (player->hasEquipArea(area)) playerView.equipAreas << area;
        for (const QByteArray &name : player->dynamicPropertyNames()) {
            const QString key = QString::fromUtf8(name);
            if (key.startsWith(QStringLiteral("ChangeSkill_")) && key.endsWith(QStringLiteral("_State")))
                playerView.changeSkillStates.insert(key.mid(12, key.size() - 18),
                                                    player->property(name.constData()).toInt());
        }
        playerView.kongcheng = player->isKongcheng();
        playerView.wounded = player->isWounded();
        playerView.faceUp = player->faceUp();
        playerView.chained = player->isChained();
        playerView.playerState = player->getState();
        playerView.maxCards = player->getMaxCards();
        playerView.hujia = player->getHujia();
        playerView.attackRange = player->getAttackRange();
        playerView.gender = int(player->getGender());
        playerView.lord = player->isLord();
        // Armor visibility still depends on the viewer in hegemony. The blocking
        // scan and the physical armor do not, so they are resolved once.
        bool armorBlocked = false;
        // Same 50p cost trap as Player::viewAsEquipSources: scan the small
        // ViewAsEquipSkill list instead of every attached skill instance.
        foreach (const ViewAsEquipSkill *skill, Sanguosha->getViewAsEquipSkills()) {
            if (!skill) continue;
            const QString skillName = skill->objectName();
            if (!player->hasSkill(skillName)) continue;
            for (int id : player->getSkillInstanceIds(skillName)) {
                if (!player->isSkillInvalid(skillName, id)) {
                    armorBlocked = true;
                    break;
                }
            }
            if (armorBlocked) break;
        }
        row.armorBasis = !armorBlocked
            && player->property("View_As_Equips_List").toString().isEmpty();
        if (row.armorBasis) {
            const EquipCard *armor = player->getArmor();
            // Mirror the physical branch of hasArmorEffect only. Its fallback
            // invokes viewAsEquip callbacks (even before checking skill validity)
            // and can clone virtual cards when the physical armor is nullified.
            if (armor && player->isAlive() && player->getMark("Armor_Nullified") <= 0
                && (player->getMark("IgnoreArea1") > 0 || player->hasEquipArea(1))
                && !player->isEquipsNullified(armor))
                row.activeArmorName = armor->objectName();
        } else {
            // A view-as equipment source: ask the full query for every armor name
            // the player could have (physical, or granted by property or skill).
            QStringList names;
            if (const EquipCard *armor = player->getArmor()) names << armor->objectName();
            names << player->property("View_As_Equips_List").toString().split('+', Qt::SkipEmptyParts);
            foreach (const ViewAsEquipSkill *skill, Sanguosha->getViewAsEquipSkills())
                if (skill && player->hasSkill(skill->objectName()))
                    names << skill->viewAsEquip(player).split(',', Qt::SkipEmptyParts);
            names.removeDuplicates();
            row.armorEffectNamesKnown = true;
            for (const QString &name : names)
                if (player->hasArmorEffect(name)) row.armorEffectNames << name;
        }
        for (int slot = 0; slot < 5; ++slot) {
            if (const EquipCard *equip = player->getEquip(slot))
                playerView.equipSlots.insert(slot, equip->getEffectiveId());
        }

        row.kingdom = player->getKingdom();
        row.hegemonyKingdom = player->getHegemonyKingdom();
        row.hasActualGeneral1 = player->getActualGeneral1() != nullptr;
        row.shownOneGeneral = player->hasShownOneGeneral();
        row.shownGeneral = player->hasShownGeneral();
        row.shownGeneral2 = player->hasShownGeneral2();
        row.generalName = player->getGeneralName();
        row.general2Name = player->getGeneral2Name();
        row.actualGeneral1Name = player->getActualGeneral1Name();
        row.actualGeneral2Name = player->getActualGeneral2Name();
        row.role = player->getRole();
        row.roleRevealed = m_room.isRoleRevealed(player);
        board.customRoles = board.customRoles || player->getRoleEnum() == Player::UnknownRole;

        foreach (const Card *card, player->getEquips())
            playerView.equips << makeAICardView(card);
        foreach (const Card *card, player->getJudgingArea())
            playerView.judgingArea << makeAICardView(card);
        foreach (const QString &mark, player->getMarkNames())
            row.marks.insert(mark, player->getMark(mark));

        foreach (const QString &pileName, player->getPileNames()) {
            PublicPile pile;
            pile.name = pileName;
            pile.cardIds = player->getPile(pileName);
            pile.handPile = pileName == QStringLiteral("wooden_ox")
                || pileName.startsWith(QChar('&'));
            foreach (const int cardId, pile.cardIds) {
                if (const Card *card = Sanguosha->getCard(cardId))
                    pile.cards << makeAICardView(card);
            }
            row.piles << pile;
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
            PublicSkill cached;
            cached.instance = instance;
            AISkillView &skillView = cached.view;
            skillView.skillName = instance.skillName;
            skillView.instanceId = instance.instanceID;
            skillView.source = int(instance.source);
            skillView.invalid = player->isSkillInvalid(instance.skillName, instance.instanceID);
            skillView.hasAmountOverride = instance.hasAmountOverride;
            skillView.amount = instance.hasAmountOverride ? instance.amountOverride : 0;
            skillView.hasViewAsSkill = ViewAsSkill::parseViewAsSkill(skill) != nullptr;
            if (const ViewAsEquipSkill *equipSkill = Sanguosha->getViewAsEquipSkill(instance.skillName)) {
                skillView.viewAsEquipKnown = true;
                skillView.viewAsEquipNames = equipSkill->viewAsEquip(player).split(',', Qt::SkipEmptyParts);
            }
            skillView.description = skill->getDescription();
            skillView.limitMark = skill->getLimitMark();
            if (const TriggerSkill *trigger = Sanguosha->getTriggerSkill(instance.skillName))
                for (TriggerEvent event : trigger->getTriggerEvents())
                    skillView.triggerEvents << int(event);
            for (const QMetaObject *meta = skill->metaObject(); meta; meta = meta->superClass())
                skillView.skillClasses << QString::fromLatin1(meta->className());
            skillView.frequency = int(skill->getFrequency(player));
            skillView.lordSkill = skill->isLordSkill();
            skillView.attachedLordSkill = skill->isAttachedLordSkill();
            // A lord skill only works while its owner is the lord of a played kingdom.
            skillView.lordSkillEffective = skill->isLordSkill()
                && player->hasLordSkill(skill->objectName());
            skillView.correctState = makeAIStateObject(instance.correctState);
            row.skills << cached;
        }
        board.players << row;
    }
    return board;
}

static QJsonObject makeAIHegemonyFacts(const Room &room, const ServerPlayer *viewer)
{
    QJsonObject facts{{"enabled", true},
        {"first_show_reward", Config.value("RewardTheFirstShowingPlayer", true).toBool()},
        {"enable_lord_conversion", Config.value("EnableLordConvertion", false).toBool()},
        {"owner_can_recover", viewer->canRecover()},
        {"owner_slash_count", viewer->getSlashCount()},
        {"owner_extra_slash_specific_assignee", viewer->property("extra_slash_specific_assignee").toString()}};
    QString ownKingdom = viewer->getSeemingKingdom();
    if (ownKingdom.isEmpty()) {
        ownKingdom = viewer->getRole().startsWith("careerist")
            ? viewer->getRole() : viewer->getHegemonyKingdom();
    }
    facts.insert("own_kingdom", ownKingdom);
    const General *ownHead = viewer->getActualGeneral1();
    facts.insert("owner_head_kingdom", ownHead ? ownHead->getKingdom() : QString());
    const Player *ownLord = viewer->getLord();
    facts.insert("owner_hegemony_lord", ownLord ? ownLord->objectName() : QString());
    const int uses = room.countHistoryCards(viewer);
    const int responses = room.countHistoryCards(viewer, QStringLiteral("turn"), QString(), true);
    // An incomplete history stays absent rather than being turned into zero.
    if (uses >= 0) facts.insert("owner_card_used_times", uses);
    if (responses >= 0) facts.insert("owner_card_responded_times", responses);

    QJsonObject players, publicLords, friendships, jadeSeal, generalPiles, definitions, souls;
    const QList<ServerPlayer *> roster = room.getPlayers();
    for (const QString &kingdom : Sanguosha->getKingdoms())
        publicLords.insert(kingdom, QString());
    QJsonObject cardCounts = const_cast<Room &>(room).roomRuntime()->ai().publicCardCounts();
    cardCounts.insert(QStringLiteral("ban_packages"),
                      QJsonArray::fromStringList(Sanguosha->getBanPackages()));
    QJsonObject discardEngineKinds;
    for (const int cardId : const_cast<Room &>(room).getDiscardPile()) {
        const Card *engineCard = Sanguosha->getEngineCard(cardId);
        if (engineCard)
            discardEngineKinds.insert(QString::number(cardId),
                QJsonArray::fromStringList(engineCard->getKindOfNames()));
    }
    cardCounts.insert(QStringLiteral("discard_engine_kinds"), discardEngineKinds);

    // getKnownCard(..., "h") observes hand and Wooden Ox only. Preserve that
    // domain and its plain class/suit/color keys; public equips are not hand cards.
    auto incrementVisibleCard = [](QJsonObject &counts, const Card *card) {
        if (!card) return;
        QSet<QString> kinds;
        for (const QString &name : card->getKindOfNames()) kinds.insert(name);
        kinds.insert(card->getSuitString());
        const QString color = card->isRed() ? QStringLiteral("red")
            : card->isBlack() ? QStringLiteral("black") : QString();
        if (!color.isEmpty()) {
            kinds.insert(color);
        }
        for (const QString &key : kinds)
            counts.insert(key, counts.value(key).toInt() + 1);
    };
    QJsonObject visibleCounts;
    for (const ServerPlayer *player : roster) {
        QSet<int> seen;
        QJsonObject counts;
        int handVisibleCount = 0;
        int woodenOxVisibleCount = 0;
        const auto addCard = [&](int cardId, bool woodenOx) {
            if (seen.contains(cardId) || !cardVisibleTo(room, cardId, viewer)) return;
            const Card *card = Sanguosha->getCard(cardId);
            if (!card) return;
            seen.insert(cardId);
            if (woodenOx) ++woodenOxVisibleCount;
            else ++handVisibleCount;
            incrementVisibleCard(counts, card);
        };
        for (const Card *card : player->getHandcards()) {
            if (!card || !cardVisibleTo(room, card->getEffectiveId(), viewer)) continue;
            addCard(card->getEffectiveId(), false);
        }
        for (const int cardId : player->getPile(QStringLiteral("wooden_ox")))
            addCard(cardId, true);
        visibleCounts.insert(player->objectName(), QJsonObject{
            {"hand_visible_count", handVisibleCount},
            {"wooden_ox_visible_count", woodenOxVisibleCount},
            {"counts", counts}});
    }
    cardCounts.insert(QStringLiteral("visible_counts"), visibleCounts);
    facts.insert(QStringLiteral("card_counts"), cardCounts);

    const General *actualHead = viewer->getActualGeneral1();
    const General *actualDeputy = viewer->getActualGeneral2();
    const auto generalGender = [](const General *general) -> QJsonValue {
        return general ? QJsonValue(QJsonObject{{"male", general->isMale()},
                                                {"female", general->isFemale()}})
                       : QJsonValue(false);
    };
    // Raw means Player::getGeneral[2], including its concealed placeholder;
    // actual means the owner's private selected general. They are not aliases.
    facts.insert(QStringLiteral("owner_raw_head"), generalGender(viewer->getGeneral()));
    facts.insert(QStringLiteral("owner_raw_deputy"), generalGender(viewer->getGeneral2()));
    facts.insert(QStringLiteral("owner_actual_head"), generalGender(actualHead));
    facts.insert(QStringLiteral("owner_actual_deputy"), generalGender(actualDeputy));
    const auto addGeneral = [&definitions](const QString &name) {
        if (name.isEmpty() || definitions.contains(name)) return;
        const General *general = Sanguosha->getGeneral(name);
        if (!general) return;
        QJsonArray skills;
        for (const Skill *skill : general->getVisibleSkillList())
            skills.append(QJsonObject{{"name", skill->objectName()},
                {"description", skill->getDescription()}, {"frequency", int(skill->getFrequency())}});
        definitions.insert(name, QJsonObject{{"exists", true}, {"kingdom", general->getKingdom()},
            {"kingdoms", general->getKingdoms()}, {"double_kingdoms", general->isDoubleKingdoms()},
            {"male", general->isMale()}, {"female", general->isFemale()}, {"skills", skills}});
    };
    addGeneral(QStringLiteral("anjiang"));
    for (const ServerPlayer *player : roster) {
        const bool owner = player == viewer;
        const bool headShown = player->hasShownGeneral();
        const bool deputyShown = player->hasShownGeneral2();
        const QString head = owner ? player->getActualGeneral1Name()
            : headShown ? player->getGeneralName() : QStringLiteral("anjiang");
        const QString deputy = owner ? player->getActualGeneral2Name()
            : deputyShown ? player->getGeneral2Name() : QStringLiteral("anjiang");
        const QString publicHead = headShown ? player->getGeneralName() : QStringLiteral("anjiang");
        const QString publicDeputy = deputyShown ? player->getGeneral2Name() : QStringLiteral("anjiang");
        addGeneral(head);
        addGeneral(deputy);
        const General *knownHead = Sanguosha->getGeneral(head);
        QJsonObject row{{"shown_head", headShown}, {"shown_deputy", deputyShown},
            {"shown_one", player->hasShownOneGeneral()}, {"shown_all", player->hasShownAllGenerals()},
            {"public_kingdom", player->getSeemingKingdom()},
            {"public_careerist", player->hasShownRole() && player->getRole() == "careerist"},
            {"head", head}, {"deputy", deputy}, {"public_head", publicHead}, {"public_deputy", publicDeputy},
            {"head_kingdom", knownHead ? knownHead->getKingdom() : QString()}};
        // Duanchang announces these slots publicly without revealing their generals.
        row.insert("duanchang", QJsonArray::fromStringList(
            player->property("Duanchang").toString().split(',', Qt::SkipEmptyParts)));
        QJsonObject shownSkills, skillSlots;
        for (const SkillInstance &instance : player->getSkillInstances()) {
            const Skill *skill = Sanguosha->getSkill(instance.skillName);
            if (!instance.visible || !skill || !skill->isVisible()) continue;
            if (!SkillRuntimeCoordinator::canReceiveSkillInstance(room, viewer, player, instance)) continue;
            QJsonObject slot = skillSlots.value(instance.skillName).toObject();
            slot.insert("head", slot.value("head").toBool() || instance.bindHead == 1);
            slot.insert("deputy", slot.value("deputy").toBool() || instance.bindHead == 2);
            skillSlots.insert(instance.skillName, slot);
            // The owner receives concealed instances too. Only publicly shown
            // roots enter shown_skills, preserving same-name slot identities.
            const Player *sourceOwner = player;
            const SkillInstance *source = &instance;
            QSet<QString> visited;
            while (source && source->source == SourceAttached) {
                const QString ref = sourceOwner->objectName() + QChar(0x1f) + source->key().toString();
                if (visited.contains(ref)) { source = nullptr; break; }
                visited.insert(ref);
                sourceOwner = room.findPlayerByObjectName(source->parentRef.ownerObjectName, true);
                source = sourceOwner ? sourceOwner->findSkillInstance(source->parentRef.key.skillName,
                    source->parentRef.key.instanceID) : nullptr;
            }
            if (!source || source->source == SourceHelper || !source->visible) continue;
            if (source->source == SourceInnate && !HegemonyMode::innateGeneralShown(sourceOwner, source->bindHead)) continue;
            const QString general = source->bindHead == 1 ? sourceOwner->getGeneralName()
                : source->bindHead == 2 ? sourceOwner->getGeneral2Name() : QString();
            QJsonObject entry{{"general", general}, {"head", source->bindHead == 1},
                {"bind_head", source->bindHead}, {"instance_id", instance.instanceID},
                {"source_owner", sourceOwner->objectName()}};
            QJsonObject previous = shownSkills.value(instance.skillName).toObject();
            QJsonArray instances = previous.value("instances").toArray();
            instances.append(entry);
            const bool boundHere = sourceOwner == player && (source->bindHead == 1 || source->bindHead == 2);
            const int previousSlot = previous.value("bind_head").toInt();
            const bool previouslyBoundHere = previous.value("source_owner").toString() == player->objectName()
                && (previousSlot == 1 || previousSlot == 2);
            // An unbound acquired/foreign attached instance must not replace a
            // same-name general source. Exact consumers retain every instance.
            if (!previous.isEmpty() && (!boundHere || (previouslyBoundHere && previousSlot <= source->bindHead)))
                entry = previous;
            entry.insert("instances", instances);
            shownSkills.insert(instance.skillName, entry);
        }
        row.insert("shown_skills", shownSkills);
        row.insert("slots", skillSlots);
        if (owner) row.insert("can_show", QJsonObject{{"head", player->canShowGeneral("h")},
            {"deputy", player->canShowGeneral("d")}, {"h", player->canShowGeneral("h")}, {"d", player->canShowGeneral("d")}});
        QJsonObject friends, siege;
        for (const ServerPlayer *other : roster) {
            friends.insert(other->objectName(), player->isFriendWith(other));
            siege.insert(other->objectName(), player->isAlive() && other->isAlive()
                && player->inSiegeRelation(player, other));
        }
        row.insert("siege_targets", siege);
        friendships.insert(player->objectName(), friends);
        players.insert(player->objectName(), row);
        jadeSeal.insert(player->objectName(), player->hasShownOneGeneral() && player->hasTreasure("JadeSeal"));
        if (headShown && player->isAlive() && player->isHegemonyLord())
            publicLords.insert(player->getKingdom(), player->objectName());

        QJsonObject open, piles, lookup;
        QJsonArray names;
        for (const QString &name : player->getGeneralPileNames()) {
            const bool visible = owner || player->generalPileOpen(name, viewer->objectName());
            if (!visible) continue;
            // Closed general-pile names and contents remain private too.
            names.append(name);
            open.insert(name, true);
            const QStringList contents = player->getGeneralPile(name);
            piles.insert(name, QJsonArray::fromStringList(contents));
            for (const QString &general : contents) {
                addGeneral(general);
                // A private duplicate must not change this viewer's answer.
                if (!lookup.contains(general)) lookup.insert(general, name);
            }
        }
        generalPiles.insert(player->objectName(), QJsonObject{{"complete", true},
            {"open", open}, {"names", names}, {"piles", piles}, {"lookup", lookup}});
        const bool soulVisible = owner || player->generalPileOpen("soul", viewer->objectName());
        QJsonObject soul{{"known", soulVisible}, {"visible", soulVisible}};
        if (soulVisible) soul.insert("names", QJsonArray::fromStringList(player->getGeneralPile("soul")));
        souls.insert(player->objectName(), soul);
    }
    facts.insert("players", players);
    facts.insert("public_friends", friendships);
    facts.insert("public_lords", publicLords);
    facts.insert("jade_seal", jadeSeal);
    facts.insert("kingdoms", QJsonArray::fromStringList(Sanguosha->getKingdoms()));
    facts.insert("general_piles", generalPiles);
    facts.insert("general_definitions", definitions);
    facts.insert("soul_piles", souls);
    facts.insert("soul_general_definitions", definitions);
    return facts;
}

AIWorldView AiDecisionCoordinator::projectWorldView(const PublicBoard &board, ServerPlayer *viewer,
                                                   bool compactPolicy, bool eventOnly) const
{
    EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
    LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
    AIWorldView world;
    world.modeId = board.modeId;
    world.revision = board.revision;
    world.currentPlayer = board.currentPlayer;
    world.currentPhase = board.currentPhase;
    world.playerOrder = board.playerOrder;
    world.alivePlayerOrder = board.alivePlayerOrder;
    world.customRoles = board.customRoles;
    world.turnCount = m_room.getTag(QStringLiteral("TurnLengthCount")).toInt();
    world.drawPileCount = m_room.getDrawPile().size();
    if (const ServerPlayer *dying = m_room.getCurrentDyingPlayer())
        world.currentDying = dying->objectName();
    if (!eventOnly) {
        // A card use is public once announced. Skip a use whose pointers the room no
        // longer owns.
        const AIStateRefs refs = liveStateRefs(m_room);
        int remaining = AiStateMaxValues;
        const QVariantMap tags = m_room.getAllTags();
        for (auto it = tags.cbegin(); it != tags.cend(); ++it) {
            if (!it.key().startsWith(QStringLiteral("UseHistory"))
                || it.value().userType() != qMetaTypeId<CardUseStruct>()) continue;
            QJsonValue value;
            if (makeAIOwnStateValue(it.value(), value, remaining, refs))
                world.useHistory.insert(it.key().mid(10), value);
        }
    }
    world.enableHegemony = ServerInfo.EnableHegemony;
    world.aiHumanized = board.modeId != QStringLiteral("50p")
        && Config.value(QStringLiteral("AIHumanized"), true).toBool();
    if (!viewer || viewer->getRoom() != &m_room) return world;
    world.aiChatEnabled = viewer->getState() == QStringLiteral("robot")
        && Config.OriginAIDelay > 0 && world.aiHumanized;

    // The mode's role composition is public; subtract the dead whose roles this
    // viewer can see. Any hidden dead role leaves the living counts unknown.
    static const QMap<QChar, QString> roleNames{{'Z', "lord"}, {'C', "loyalist"},
                                                {'F', "rebel"}, {'N', "renegade"}};
    QJsonObject roleCounts{{"lord", 0}, {"loyalist", 0}, {"rebel", 0}, {"renegade", 0}};
    const QString roles = Sanguosha->getRoles(board.modeId);
    bool roleCountsKnown = !board.customRoles && roles.size() == board.players.size();
    for (const QChar role : roles) {
        if (!roleNames.contains(role)) { roleCountsKnown = false; break; }
        const QString name = roleNames.value(role);
        roleCounts.insert(name, roleCounts.value(name).toInt() + 1);
    }
    for (const PublicPlayer &row : board.players) {
        if (!roleCountsKnown) break;
        if (!row.view.dead) continue;
        if (!m_room.canSeeRole(viewer, row.player) || !roleCounts.contains(row.role)) {
            roleCountsKnown = false;
            break;
        }
        roleCounts.insert(row.role, roleCounts.value(row.role).toInt() - 1);
    }
    world.roleCountsKnown = roleCountsKnown;
    if (roleCountsKnown) world.roleCounts = roleCounts;

    const bool hegemony = ServerInfo.EnableHegemony;
    // Basic prototype rules cost several skill scans per player; events skip them.
    const QHash<QString, QJsonObject> prototypes = eventOnly
        ? QHash<QString, QJsonObject>() : prototypeRules();
    for (const PublicPlayer &row : board.players) {
        ServerPlayer *player = row.player;
        if (!player) continue;
        AIPlayerView playerView = row.view;
        playerView.prototypeRules = prototypes.value(player->objectName());
        // Query the equipped armor only. A virtual armor skill needs its own
        // visible projection; do not clone hypothetical equipment for the AI.
        playerView.armorEffectKnown = row.armorBasis && (player == viewer || !hegemony);
        if (playerView.armorEffectKnown)
            playerView.activeArmorName = row.activeArmorName;
        playerView.armorEffectNamesKnown = row.armorEffectNamesKnown && (player == viewer || !hegemony);
        if (playerView.armorEffectNamesKnown)
            playerView.armorEffectNames = row.armorEffectNames;

        const bool seesIdentity = player == viewer || !hegemony
            || row.shownOneGeneral || playerView.dead;
        if (seesIdentity)
            playerView.kingdom = hegemony && player == viewer && !row.shownOneGeneral
                && row.hasActualGeneral1 ? row.hegemonyKingdom : row.kingdom;
        playerView.roleRevealed = row.roleRevealed;
        playerView.roleVisible = m_room.canSeeRole(viewer, player);
        if (playerView.roleVisible)
            playerView.role = row.role;
        if (player == viewer || !hegemony || row.shownGeneral || playerView.dead)
            playerView.generalName = hegemony && player == viewer
                ? row.actualGeneral1Name : row.generalName;
        if (player == viewer || !hegemony || row.shownGeneral2 || playerView.dead)
            playerView.general2Name = hegemony && player == viewer
                ? row.actualGeneral2Name : row.general2Name;

        for (auto mark = row.marks.cbegin(); mark != row.marks.cend(); ++mark) {
            if (isMarkVisibleTo(player, mark.key(), viewer))
                playerView.publicMarks.insert(mark.key(), mark.value());
        }

        // Card zones. Only what this viewer may see crosses, and an open-but-empty zone
        // stays distinguishable from one that is simply not visible. Hand flags are
        // read live: card flags do not bump the room revision.
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
        for (const PublicPile &pile : row.piles) {
            AICardPileView pileView;
            pileView.name = pile.name;
            pileView.count = pile.cardIds.size();
            pileView.handPile = pile.handPile;
            pileView.open = player == viewer
                || player->pileOpen(pile.name, viewer->objectName());
            if (pileView.open) {
                pileView.cardIds = pile.cardIds;
                pileView.cards = pile.cards;
            }
            playerView.piles << pileView;
        }

        playerView.marksComplete = player == viewer;
        for (const PublicSkill &cached : row.skills) {
            if (hegemony && !SkillRuntimeCoordinator::canReceiveSkillInstance(
                    m_room, viewer, player, cached.instance)) {
                playerView.skillsComplete = false;
                continue;
            }
            AISkillView skillView = cached.view;
            skillView.hasPrivateState = player == viewer;
            skillView.state = makeAIStateObject(skillView.hasPrivateState
                ? player->getSkillInstanceState(cached.instance.skillName, cached.instance.instanceID)
                : player->getSkillInstancePublicState(cached.instance.skillName, cached.instance.instanceID));
            if (skillView.hasPrivateState) {
                // Play/response availability is decision data; an attached Slash
                // view-as runs targetFilter on every alive player.
                const ViewAsSkill *viewAs = eventOnly ? nullptr
                    : Sanguosha->getViewAsSkill(cached.instance.skillName);
                if (viewAs) {
                    skillView.viewAsKnown = true;
                    skillView.viewAsV2 = dynamic_cast<const ViewAsSkillV2 *>(viewAs) != nullptr;
                    skillView.enabledAtPlay = viewAs->isEnabledAtPlay(player);
                    static const QStringList responsePatterns{QStringLiteral("slash"),
                        QStringLiteral("jink"), QStringLiteral("peach"), QStringLiteral("analeptic"),
                        QStringLiteral("nullification"), QStringLiteral("peach+analeptic")};
                    for (const QString &pattern : responsePatterns)
                        skillView.enabledAtResponse.insert(pattern, viewAs->isEnabledAtResponse(player, pattern));
                }
            }
            playerView.skills << skillView;
        }

        if (player == viewer) {
            // Only the viewer's own Slash evaluation reads the empty-prefix filter.
            // It costs one targetFilter per alive player, so events skip it.
            if (!eventOnly && player->isAlive()
                && playerView.prototypeRules.contains(QStringLiteral("Slash"))) {
                if (Card *slash = Sanguosha->cloneCard(QStringLiteral("slash"))) {
                    slash->deleteLater();
                    QJsonObject rules = playerView.prototypeRules.value(QStringLiteral("Slash")).toObject();
                    QJsonObject filters;
                    for (ServerPlayer *target : m_room.getAlivePlayers())
                        filters.insert(target->objectName(), slash->targetFilter({}, target, player));
                    rules.insert("target_filter_empty", filters);
                    rules.insert("extra_target", rules.value("target_mod_extra"));
                    playerView.prototypeRules.insert(QStringLiteral("Slash"), rules);
                }
            }
            // Own-card rules are decision data; events skip them like prototype rules.
            for (AICardView &card : playerView.equips)
                projectOwnCardState(card, viewer, false, !eventOnly);
            for (AICardPileView &pile : playerView.piles)
                for (AICardView &card : pile.cards)
                    projectOwnCardState(card, viewer, false, !eventOnly);
            const QHash<QString, int> history = player->getHistory();
            for (auto entry = history.cbegin(); entry != history.cend(); ++entry)
                playerView.usedHistory.insert(entry.key(), player->usedTimes(entry.key()));
            playerView.usedHistoryVisible = true;
            int remaining = AiStateMaxValues;
            const AIStateRefs refs = liveStateRefs(m_room);
            const QVariantMap tags = player->getAllTags();
            for (auto entry = tags.cbegin(); entry != tags.cend(); ++entry) {
                QJsonValue value;
                if (makeAIOwnStateValue(entry.value(), value, remaining, refs))
                    playerView.tags.insert(entry.key(), value);
            }
            for (const QByteArray &name : player->dynamicPropertyNames()) {
                QJsonValue value;
                if (makeAIOwnStateValue(player->property(name.constData()), value, remaining, refs))
                    playerView.properties.insert(QString::fromUtf8(name), value);
            }
            playerView.ownStateVisible = true;
            playerView.canSlashWithoutCrossbow = player->canSlashWithoutCrossbow();
            static const QStringList zones{"h", "e", "j", "he", "hj", "ej", "hej"};
            for (const ServerPlayer *other : m_room.getAllPlayers()) {
                QMap<QString, bool> row;
                for (const QString &zone : zones) row.insert(zone, player->canDiscard(other, zone));
                playerView.canDiscard.insert(other->objectName(), row);
            }
            world.self = playerView;
            foreach (const Card *card, player->getHandcards()) {
                AICardView view = makeAICardView(card);
                projectOwnCardState(view, viewer, false, !eventOnly);
                world.handCards << view;
            }
        } else {
            world.players << playerView;
        }
    }
    const auto distancePlayers = m_room.getAlivePlayers();
    const auto distanceSkills = !eventOnly && !compactPolicy
        ? Sanguosha->getDistanceSkills() : QList<const DistanceSkill *>();
    const quint64 distanceRevision = m_room.roomRuntime()->stateRevision();
    const quint64 skillGeneration = SkillSet::generation();
    if (eventOnly) {
        // Events skip the matrix, but the identity game process reads every known
        // rebel's distance to the lord; without it each such event failed its hook.
        world.distanceScope = QStringLiteral("lord");
        world.distances = lordDistances();
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
    if (world.enableHegemony)
        world.hegemonyFacts = makeAIHegemonyFacts(m_room, viewer);
    AiLuaRuntime::evaluateModePolicy(m_room.roomRuntime()->lua(), world, compactPolicy);
    return world;
}

AIWorldView AiDecisionCoordinator::buildWorldView(ServerPlayer *viewer, bool compactPolicy,
                                                 bool eventOnly) const
{
    AIWorldView world;
    world.modeId = m_room.getMode();
    world.revision = m_room.roomRuntime()->stateRevision();
    if (!viewer || viewer->getRoom() != &m_room) return world;
    return projectWorldView(publicBoard(), viewer, compactPolicy, eventOnly);
}

AIRequest AiDecisionCoordinator::makeRequestHeader(ServerPlayer *player,
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
    return request;
}

AIRequest AiDecisionCoordinator::makeRequest(ServerPlayer *player,
                                             AIRequest::DecisionKind kind,
                                             CardUseStruct::CardUseReason reason,
                                             const QString &pattern,
                                             const QString &prompt,
                                             Card::HandlingMethod method) const
{
    AIRequest request = makeRequestHeader(player, kind, reason, pattern, prompt, method);
    AiDecisionPhase requestPhase(request, "make_request");
    // --ai off selects the native fallback. It must not build the quadratic
    // isolated snapshot or allow an isolated Lua handler to override that AI.
    if (!Config.EnableAI && (!player || !m_room.externalAgent(player->objectName())))
        return request;
    // Build the request from a read-only board snapshot; within one revision, evaluate matching distance, attack-range, and target-modifier queries once.
    TargetModMemoScope targetModMemo([this]() { return m_room.roomRuntime()->stateRevision(); }, &m_targetModCarry);
    // Temporary 10P diagnosis: bracket request construction, which AI_PROBE excludes.
    const bool lagProbe = qgetenv("QSAN_10P_LAG_PROBE") == "1";
    QElapsedTimer lagTimer;
    if (lagProbe) {
        lagTimer.start();
        qWarning().noquote() << "[LAG_PROBE] begin request" << request.decisionId
                             << request.viewerObjectName << int(kind);
    }
    // Trigger ordering offers skill names, not card/target actions. Use the
    // existing event snapshot contract: public/private facts and mode policy,
    // without eagerly evaluating every viewer distance or copying card history.
    // Unsupported isolated callbacks still fall back through runAnswer.
    const auto endpoint = player ? m_room.externalAgent(player->objectName()) : nullptr;
    if (endpoint && !endpoint->requiresWorldView(kind)) {
        // An explicitly opted-in local adapter needs the decision's authority
        // tickets, not a board observation. Native fallback reads the live Room.
        // Ordinary external and isolated requests retain their complete projection.
        request.worldView.modeId = m_room.getMode();
        request.worldView.revision = request.stateRevision;
        request.worldView.self.objectName = request.viewerObjectName;
        request.worldView.distanceScope = QStringLiteral("none");
        request.worldView.modePolicy.insert(QStringLiteral("projection_scope"),
                                            QStringLiteral("local_decision"));
    } else {
        request.worldView = buildWorldView(player, true, kind == AIRequest::TriggerOrder);
    }
    if (lagProbe)
        qWarning().noquote() << "[LAG_PROBE] world request" << request.decisionId
                             << lagTimer.elapsed() << "ms";
    if (player && (kind == AIRequest::Activate || kind == AIRequest::UseCard
                   || kind == AIRequest::RespondCard)) {
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
        foreach (const SkillInstance &instance, player->getSkillInstances()) {
            AiSkillActionContext actionContext;
            if (buildSkillActionContext(player, instance, reason, pattern, actionContext))
                request.skillActions << actionContext;
        }
        // The same native equipment source resolver serves physical and granted
        // equipment buttons. A zero-ID entry carries this receipt, never ownership
        // of an invented SkillInstance.
        QMap<QString, const ViewAsSkill *> equipmentEntries;
        for (const Card *card : player->getEquips()) {
            const auto *equip = card ? qobject_cast<const EquipCard *>(card->getRealCard()) : nullptr;
            const Skill *source = equip ? Sanguosha->getSkill(equip) : nullptr;
            const ViewAsSkill *entry = source ? ViewAsSkill::parseViewAsSkill(source) : nullptr;
            if (entry) equipmentEntries.insert(entry->objectName(), entry);
        }
        QSet<QString> equipmentNames;
        for (const QString &name : player->property("View_As_Equips_List").toString().split('+', Qt::SkipEmptyParts))
            equipmentNames.insert(name);
        for (const ViewAsEquipSkill *provider : Sanguosha->getViewAsEquipSkills()) {
            if (!provider || !player->hasSkill(provider->objectName())) continue;
            for (const QString &name : provider->viewAsEquip(player).split(',', Qt::SkipEmptyParts))
                equipmentNames.insert(name);
        }
        for (const QString &name : equipmentNames) {
            const ViewAsSkill *entry = Sanguosha->getViewAsSkill(name);
            if (entry) equipmentEntries.insert(entry->objectName(), entry);
            else request.equipmentActionsComplete = false;
        }
        for (const ViewAsSkill *viewAs : equipmentEntries) {
            const auto *skill = dynamic_cast<const ViewAsSkillV2 *>(viewAs);
            if (!skill || !skill->isEquipSkill()) {
                if (viewAs) request.equipmentActionsComplete = false;
                continue;
            }
            ActiveSkillRequest activation;
            activation.reason = reason;
            activation.pattern = pattern;
            activation.initiator = player;
            activation.activationRef = SkillInstanceRef(player->objectName(), SkillInstanceKey(skill->objectName(), 0));
            if (!skill->canActivateRequest(activation)) continue;
            SkillContext source;
            source.owner = source.invoker = source.initiator = player;
            if (!skill->prepareEquipSource(&m_room, source)) {
                request.equipmentActionsComplete = false;
                continue;
            }
            // A virtual equipment source can be represented by both its real
            // positive-ID skill instance and the zero-ID equipment button. A
            // physical source has a different identity and must remain beside
            // a virtual grant of the same equipment/skill name. Compare the
            // receipt/source, never only the display name or instance zero.
            bool duplicateSource = false;
            if (source.physicalEquipSource.isValid()) {
                for (const AiSkillActionContext &existing : request.skillActions) {
                    if (existing.physicalEquipSource.isValid()
                        && existing.physicalEquipSource == source.physicalEquipSource) {
                        duplicateSource = true;
                        break;
                    }
                }
            } else if (source.sourceRef.isValid()) {
                for (const AiSkillActionContext &existing : request.skillActions) {
                    // A positive-ID action names the admitted instance in
                    // activationRef, while its sourceRef may already be
                    // canonicalized to the quota root. Either exact witness
                    // denotes this same virtual equipment source.
                    if (existing.getActivationSkillName() == viewAs->objectName()
                        && ((existing.sourceRef.isValid() && existing.sourceRef == source.sourceRef)
                            || (existing.activationRef.isValid()
                                && existing.activationRef == source.sourceRef))) {
                        duplicateSource = true;
                        break;
                    }
                }
            } else {
                // Property grants intentionally have no source ref. If their
                // attached positive-ID action is present, it is the precise
                // action and the zero-ID probe would be invalid/ambiguous.
                for (const AiSkillActionContext &existing : request.skillActions) {
                    if (existing.getActivationSkillName() == viewAs->objectName()
                        && existing.sourceRef.isValid()) {
                        duplicateSource = true;
                        break;
                    }
                }
            }
            if (duplicateSource) continue;
            AiSkillActionContext action;
            action.activationRef = source.activationRef;
            action.sourceRef = source.sourceRef;
            action.equipmentActivation = true;
            action.physicalEquipSource = source.physicalEquipSource;
            if (!action.isValid()) {
                request.equipmentActionsComplete = false;
                continue;
            }
            activation.activationRef = source.activationRef;
            if (!skill->isUsable(source)) continue;
            action.activationQuotaAvailable = action.sourceQuotaAvailable = true;
            request.skillActions << action;
        }
        int projectionBudget = 16384;
        foreach (const Card *card, player->getHandcards())
            request.cardCandidates << makeAICardCandidate(m_room, player, card, method,
                                                          kind, pattern, projectionBudget);
        foreach (const Card *card, player->getEquips())
            request.cardCandidates << makeAICardCandidate(m_room, player, card, method,
                                                          kind, pattern, projectionBudget);
        // Hand-pile cards play and answer like hand cards. One the viewer cannot see is
        // not offered; buildCardConversions reports that gap.
        foreach (int id, player->getHandPile()) {
            const Card *card = Sanguosha->getCard(id);
            if (card && cardVisibleTo(m_room, id, player))
                request.cardCandidates << makeAICardCandidate(m_room, player, card, method,
                                                              kind, pattern, projectionBudget);
        }
        // The authorization tickets are handed out last, so every candidate this
        // request offered carries one and nothing outside the list can claim one.
        for (int index = 0; index < request.cardCandidates.size(); ++index)
            request.cardCandidates[index].candidateId = index + 1;
        buildCardConversions(player, request, projectionBudget);
        if (kind == AIRequest::RespondCard && !pattern.isEmpty())
            projectResponseClassOrder(player, request, false);
        // askForUseCard reaches its class scan only for a use pattern that is not an
        // "@" prompt.
        else if (kind == AIRequest::UseCard && method == Card::MethodUse
                 && !pattern.isEmpty() && !pattern.startsWith(QLatin1Char('@')))
            projectResponseClassOrder(player, request, true);
    }
    if (lagProbe)
        qWarning().noquote() << "[LAG_PROBE] end request" << request.decisionId
                             << lagTimer.elapsed() << "ms";
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
    request.stagedActionsComplete = false;
    if (!player) return false;
    EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
    LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
    bool enumerated = request.equipmentActionsComplete;
    bool staged = !request.hasSkillActionContext;
    int probesLeft = 512;

    // A hand-pile card the viewer cannot see is neither candidate nor material.
    foreach (int id, player->getHandPile())
        if (!cardVisibleTo(m_room, id, player)) enumerated = false;
    QSet<QString> instanceSkills;
    for (const SkillInstance &instance : player->getSkillInstances())
        instanceSkills.insert(instance.skillName);
    foreach (const Card *equip, player->getEquips()) {
        const auto *realEquip = equip ? qobject_cast<const EquipCard *>(equip->getRealCard()) : nullptr;
        const Skill *equipSkill = realEquip ? Sanguosha->getSkill(realEquip) : nullptr;
        const ViewAsSkill *viewAs = equipSkill ? ViewAsSkill::parseViewAsSkill(equipSkill) : nullptr;
        if (viewAs && !dynamic_cast<const ViewAsSkillV2 *>(viewAs)
            && !instanceSkills.contains(equipSkill->objectName())) enumerated = false;
    }

    // A view-as skill matters only where the engine would offer it here: a V2 skill
    // has an action in this request; a V1 skill is enabled by isEnabledAtPlay for
    // play or isEnabledAtResponse(pattern) for a response, nullification included.
    // Any other reason stays unknown. V1 skills are not ticketed.
    const bool reasonKnown = request.reason == CardUseStruct::CARD_USE_REASON_PLAY
        || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE
        || request.reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE;
    for (const SkillInstance &instance : player->getSkillInstances()) {
        const ViewAsSkill *skill = Sanguosha->getViewAsSkill(instance.skillName);
        if (!skill) continue;
        const bool v2 = dynamic_cast<const ViewAsSkillV2 *>(skill) != nullptr;
        bool offered = false;
        if (v2) {
            for (const AiSkillActionContext &action : request.skillActions)
                offered = offered || (action.getActivationSkillName() == instance.skillName
                                      && action.getActivationInstanceId() == instance.instanceID);
        } else {
            offered = !player->isSkillInvalid(instance.skillName, instance.instanceID)
                && (!reasonKnown || skill->isAvailable(player, request.reason, request.pattern));
        }
        if (offered && !v2)
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
        if (!skill || (!action.equipmentActivation
            && !player->hasSkillInstance(action.getActivationSkillName(), action.getActivationInstanceId()))) {
            enumerated = false;
            continue;
        }
        const int cost = skill->getN();
        // External staging groups complete tickets; it does not drive skill selection.
        // Declarations and parameterized costs still need a richer native projection.
        if (cost < 0 || cost > 1 || skill->declaresCardName())
            staged = false;
        const bool parameterized = cost >= 2 && skill->hasIndependentAIConversion();
        if (cost < 0 || cost > 8 || (cost > 1 && !parameterized)) {
            enumerated = false;
            continue;
        }
        // Hand-pile and expand-pile material. A variable cost is ticketed over hand
        // cards only, so material beyond them leaves that skill incomplete.
        const QList<int> extra = extraConversionMaterialIds(m_room, player, skill, enumerated);
        if (parameterized && !extra.isEmpty()) enumerated = false;
        QList<QList<int> > selections;
        QList<int> eligible;
        if (cost == 0) {
            selections << QList<int>();
        } else {
            foreach (int cardId, parameterized ? payable : payable + extra) {
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
                staged = false;
                const_cast<Card *>(produced)->deleteLater();
                continue;
            }
            if (produced->getTypeId() == Card::TypeSkill || produced->getSubcards() != selection)
                staged = false;
            AICardConversionView view;
            view.conversionId = request.cardConversions.size() + 1;
            view.name = produced->objectName();
            view.className = produced->getClassName();
            view.skillName = produced->getSkillName(false);
            view.kindOfNames = produced->getKindOfNames();
            view.suit = int(produced->getSuit());
            view.number = produced->getNumber();
            view.activationRef = action.activationRef;
            view.sourceRef = action.sourceRef;
            view.equipmentActivation = action.equipmentActivation;
            view.physicalEquipSource = action.physicalEquipSource;
            view.activationQuotaAvailable = action.activationQuotaAvailable;
            view.sourceQuotaAvailable = action.sourceQuotaAvailable;
            view.subcardIds = selection;
            view.costCount = parameterized ? cost : 0;
            view.eligibleSubcardIds = eligible;
            // An "@@skill" prompt asks for that skill's product, which canActivate and
            // createCard have already accepted; it is not a card pattern to match.
            const QString cardPattern = request.pattern.startsWith(QLatin1String("@@"))
                ? QString() : request.pattern;
            const AICardCandidateView candidate = makeAICardCandidate(m_room, player,
                produced, request.handlingMethod, request.kind, cardPattern, projectionBudget);
            view.available = candidate.available && !candidate.limited;
            // Outside play a candidate's availability is its pattern match.
            view.matchesResponsePattern = request.kind != AIRequest::Activate && candidate.available;
            view.legacyStringKey = produced->toString();
            view.locked = player->isLocked(produced);
            if (!view.available) { const_cast<Card *>(produced)->deleteLater(); continue; }
            view.targetFixed = candidate.targetFixed;
            view.feasibleWithNoTarget = candidate.feasibleWithNoTarget;
            view.completeCoverage = candidate.completeCoverage;
            if (!view.completeCoverage) staged = false;
            view.legalTargets = candidate.legalTargets;
            view.maxVotes = candidate.maxVotes;
            view.extraTargetKnown = candidate.extraTargetKnown;
            view.extraTargetCount = candidate.extraTargetCount;
            view.extraTargetByPlayer = candidate.extraTargetByPlayer;
            view.targetFilterEmpty = candidate.targetFilterEmpty;
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
    request.stagedActionsComplete = enumerated && staged;
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
    if (!skill->canActivateRequest(request)) return false;

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
    const QString &prompt, Card::HandlingMethod method, AIRequest &aiRequest, bool project) const
{
    AiSkillActionContext actionContext;
    if (!buildSkillActionContext(player, instance, reason, pattern, actionContext))
        return false;
    aiRequest = project
        ? makeRequest(player, AIRequest::UseCard, reason, pattern, prompt, method)
        : makeRequestHeader(player, AIRequest::UseCard, reason, pattern, prompt, method);
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
    if (!authorized->equipmentActivation
        && !player->hasSkillInstance(authorized->activationRef.key.skillName,
                                      authorized->activationRef.key.instanceID))
        return nullptr;
    ActiveSkillRequest activation;
    activation.reason = request.reason;
    activation.pattern = request.pattern;
    activation.initiator = player;
    activation.activationRef = authorized->activationRef;
    if (!skill->canActivateRequest(activation)) return nullptr;
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
    if (authorized->equipmentActivation) {
        if (!skill->isEquipSkill() || !skill->prepareEquipSource(&m_room, context)
            || context.activationRef != authorized->activationRef
            || context.sourceRef != authorized->sourceRef
            || context.physicalEquipSource != authorized->physicalEquipSource)
            return nullptr;
    } else {
        context.sourceRef = m_skillRuntime.resolveSkillInstanceRootRef(authorized->activationRef);
        // A borrowed entry must still resolve to the root that pays its quota.
        if (!context.sourceRef.isValid() || context.sourceRef != authorized->sourceRef)
            return nullptr;
    }
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
        bool owned = player->handCards().contains(useCardId)
            || player->getHandPile().contains(useCardId);
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
        candidate.physicalEquipSource = authorized->physicalEquipSource;
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
    if (result.action.skillActionContext.equipmentActivation) {
        // The reply selects an offered equipment button. Only the request holds
        // its native receipt; the reply cannot supply a new equipment/source ID.
        SkillInstanceRef claimed = result.action.skillActionContext.activationRef;
        if (claimed.ownerObjectName.isEmpty()) claimed.ownerObjectName = player->objectName();
        const AiSkillActionContext *offered = nullptr;
        for (const AiSkillActionContext &action : request.skillActions)
            if (action.equipmentActivation && action.activationRef == claimed) { offered = &action; break; }
        if (!offered || !offered->isValid()) return false;
        const auto *skill = dynamic_cast<const ViewAsSkillV2 *>(Sanguosha->getViewAsSkill(claimed.key.skillName));
        SkillContext source;
        source.owner = source.invoker = source.initiator = player;
        if (!skill || !skill->isEquipSkill() || !skill->prepareEquipSource(&m_room, source)
            || source.activationRef != offered->activationRef || source.sourceRef != offered->sourceRef
            || source.physicalEquipSource != offered->physicalEquipSource || !skill->isUsable(source))
            return false;
        ActiveSkillRequest activation;
        activation.reason = request.reason;
        activation.pattern = request.pattern;
        activation.initiator = player;
        activation.activationRef = offered->activationRef;
        activation.selectedCardIds = result.action.selectedCardIds;
        activation.selectedTargetNames = result.action.selectedTargetNames;
        activation.userString = result.action.userString;
        const Card *built = m_room.resolveActiveSkillRequest(player, skill, activation);
        if (!built) return false;
        candidate.hasSkillActivationRequest = true;
        candidate.activationRef = offered->activationRef;
        candidate.sourceRef = offered->sourceRef;
        candidate.physicalEquipSource = offered->physicalEquipSource;
        candidate.setOwnedCard(const_cast<Card *>(built));
        cardUse = candidate;
        return true;
    }
    AIRequest playRequest;
    if (!request.hasSkillActionContext) {
        // Activate requests during play do not carry skill context. For a V2 active skill, the AI result identifies the instance,
        // so rebuild and revalidate its ownership, canActivate, and usage count before creating the proxy.
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
                                        request.prompt, request.handlingMethod, playRequest, false)
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
    AiDecisionPhase choicePhase(request, "choice_projection");
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
    // A card this viewer may take is evaluated as the viewer would hold it, with the
    // same rules and play candidate its own hand cards get. An AG may also name a
    // card to use, so each offered name's printed card is evaluated the same way.
    // Guanxing's disclosed cards may be drawn by the viewer next, so they get the
    // same held-card view, but no printed-card evaluation.
    if ((kind == AIRequest::AmazingGrace || kind == AIRequest::Guanxing) && player
        && (Config.EnableAI || m_room.externalAgent(player->objectName()))) {
        TargetModMemoScope targetModMemo([this]() { return m_room.roomRuntime()->stateRevision(); }, &m_targetModCarry);
        EngineRuntimeContextScope contextScope(*Sanguosha, &m_room);
        LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
        int projectionBudget = 16384;
        QSet<QString> names;
        for (int cardId : options.cardIds) {
            const Card *card = Sanguosha->getCard(cardId);
            if (!card) continue;
            AIChoiceCardEvaluation acquired;
            acquired.card = makeAICardView(card);
            projectOwnCardState(acquired.card, player, true);
            acquired.candidate = makeAICardCandidate(m_room, player, card, Card::MethodUse,
                                                     AIRequest::Activate, QString(), projectionBudget);
            request.choiceOptions.acquiredCards << acquired;
            if (kind != AIRequest::AmazingGrace || names.contains(card->objectName())) continue;
            names.insert(card->objectName());
            const std::unique_ptr<Card> printed(Sanguosha->cloneCard(card->objectName()));
            if (!printed) continue;
            AIChoiceCardEvaluation evaluation;
            evaluation.card = makeAICardView(printed.get());
            projectOwnCardRules(evaluation.card, player, printed.get(), true);
            evaluation.candidate = makeAICardCandidate(m_room, player, printed.get(), Card::MethodUse,
                                                       AIRequest::Activate, QString(), projectionBudget);
            request.choiceOptions.printedCards << evaluation;
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

// The isolated shared defaults (false, random, pass) answer every value question,
// which would leave no refusal for SmartAI's per-reason hooks to take over. Report
// whether this room VM's SmartAI keeps a hook for the reason, using the same table
// and key spelling SmartAI's own lookup uses.
static bool hasLegacyReasonHook(LuaRuntime &runtime, const QString &callbackName,
                                const QString &reason)
{
    static const struct {
        const char *callback;
        const char *table;
        bool normalizeDash;
    } hooks[] = {
        {"askForSkillInvoke", "ai_skill_invoke", true},
        {"askForChoice", "ai_skill_choice", false},
        {"askForSuit", "ai_skill_suit", false},
        {"askForGeneral", "ai_general_choice", false},
        {"askForDiscard", "ai_skill_discard", false},
        {"askForAG", "ai_skill_askforag", true},
        {"askForCardChosen", "ai_skill_cardchosen", true},
        {"askForYiji", "ai_skill_askforyiji", true},
        {"askForPlayerChosen", "ai_skill_playerchosen", true},
        {"askForPlayersChosen", "ai_skill_playerschosen", false},
    };
    if (reason.isEmpty() || !runtime.rawState()) return false;
    for (const auto &hook : hooks) {
        if (callbackName != QLatin1String(hook.callback)) continue;
        QString key = reason;
        if (hook.normalizeDash) key.replace(QLatin1Char('-'), QLatin1Char('_'));
        LuaRuntime::Binding binding(runtime, false);
        lua_State *state = runtime.state();
        const int top = lua_gettop(state);
        bool found = false;
        lua_getglobal(state, "sgs");
        if (lua_istable(state, -1)) {
            lua_getfield(state, -1, hook.table);
            if (lua_istable(state, -1)) {
                lua_pushstring(state, key.toUtf8().constData());
                lua_rawget(state, -2);
                found = !lua_isnil(state, -1);
            }
        }
        lua_settop(state, top);
        return found;
    }
    return false;
}

static void logExternalNativeDuration(const ServerPlayer *player, const AIRequest &request,
                                     const QElapsedTimer &timer)
{
    qInfo() << "[EXTERNAL_AGENT] native callback done" << player->objectName()
            << "decision" << request.decisionId << "kind" << int(request.kind)
            << "elapsed_ns" << timer.nsecsElapsed();
}

bool AiDecisionCoordinator::externalAnswer(ServerPlayer *player, const AIRequest &request,
    AIResult &result, const std::function<bool(const AIResult &)> &validate) const
{
    auto endpoint = m_room.externalAgent(player->objectName());
    if (!endpoint) return false;
    endpoint->begin(request);
    while (true) {
        const auto outcome = endpoint->awaitReply(result);
        if (outcome == ExternalAgentEndpoint::Fallback) {
            endpoint->finish();
            qInfo() << (Config.EnableHegemony
                ? "[EXTERNAL_AGENT] disconnect; isolated route remains required"
                : "[EXTERNAL_AGENT] disconnect SmartAI fallback") << player->objectName()
                    << "decision" << request.decisionId << "kind" << int(request.kind);
            return false;
        }
        if (outcome == ExternalAgentEndpoint::Cancelled) {
            endpoint->finish();
            m_room.requestStopGameThreads();
            throw GameFinished;
        }
        if (request.stateRevision != m_room.roomRuntime()->stateRevision()) {
            // Never restamp a snapshot or silently choose for an obsolete prompt.
            endpoint->reject(QStringLiteral("stale-world"));
            if (endpoint->boundedHybrid()) {
                endpoint->disconnect();
                endpoint->finish();
                return false;
            }
            endpoint->cancel();
            continue;
        }
        if (outcome == ExternalAgentEndpoint::Local) {
            endpoint->finish();
            qInfo() << (Config.EnableHegemony
                ? "[EXTERNAL_AGENT] explicit local route rejected in isolated mode"
                : "[EXTERNAL_AGENT] explicit local SmartAI route") << player->objectName()
                    << "decision" << request.decisionId << "kind" << int(request.kind)
                    << "revision" << request.stateRevision;
            return false; // non-Hegemony callers retain their existing native callback
        }
        if (validate(result)) {
            endpoint->finish();
            return true;
        }
        endpoint->reject(QStringLiteral("illegal-action"));
        if (endpoint->boundedHybrid()) {
            endpoint->disconnect();
            endpoint->finish();
            return false;
        }
    }
}

bool AiDecisionCoordinator::runAnswer(ServerPlayer *player, const AIRequest &request,
                                      const QString &callbackName, const LegacyAnswer &legacy,
                                      AIResult &result, AiDecisionAudit &audit, bool *fromIsolated) const
{
    AiDecisionPhase answerPhase(request, "answer_route");
    if (fromIsolated)
        *fromIsolated = false;
    if (!player || !player->getAI()) return false;
    if (externalAnswer(player, request, result, [](const AIResult &) { return true; })) {
        audit.setSource(QStringLiteral("external"));
        audit.envelope(result);
        if (fromIsolated) *fromIsolated = true; // strict candidate/count gates
        return true;
    }
    // Hegemony never crosses into a native or SmartAI callback after an
    // external answer fails.  Its only local source is the isolated route;
    // malformed, stale, unsupported, or fault-contained results reject here.
    if (Config.EnableHegemony) {
        audit.setSource(QStringLiteral("isolated"));
        const AiRoute strictRoute = Config.EnableAI
            ? m_room.roomRuntime()->ai().routes().routeFor(
                request.kind, callbackName, request.choiceOptions.reason)
            : AiRouteLegacyDirect;
        if (strictRoute != AiRouteIsolated) {
            audit.reject(QStringLiteral("strict_route_not_isolated"));
            return false;
        }
        AIRequest isolated = request;
        projectDecisionContext(player, QVariant(), isolated);
        bool faultContained = false;
        result = decideIsolatedContained(player, isolated, faultContained);
        audit.envelope(result);
        if (faultContained) {
            audit.reject(result.errorCode.isEmpty() ? QStringLiteral("isolated_fault_contained") : result.errorCode);
            return false;
        }
        const bool isolatedAnswer = result.handled && result.errorCode.isEmpty()
            && result.kind != AIResult::UseCard
            && result.decisionId == request.decisionId
            && result.stateRevision == request.stateRevision
            && request.stateRevision == m_room.roomRuntime()->stateRevision();
        if (!isolatedAnswer) {
            audit.reject(result.errorCode.isEmpty() ? QStringLiteral("isolated_envelope_rejected") : result.errorCode);
            return false;
        }
        if (fromIsolated)
            *fromIsolated = true;
        return true;
    }
    const AiRoute route = Config.EnableAI
        ? m_room.roomRuntime()->ai().routes().routeFor(
            request.kind, callbackName, request.choiceOptions.reason)
        : AiRouteLegacyDirect;
    bool isolatedAnswer = false;
    if (m_room.externalAgent(player->objectName())) {
        QElapsedTimer nativeTimer;
        nativeTimer.start();
        result = legacy(request);
        logExternalNativeDuration(player, request, nativeTimer);
    } else if (route == AiRouteIsolated) {
        // Isolated reason handlers still answer first; only its shared default
        // steps aside, so the refusal reaches SmartAI's hook below.
        const bool deferDefault = qobject_cast<LuaAI *>(player->getAI())
            && hasLegacyReasonHook(m_room.roomRuntime()->lua(), callbackName,
                                   request.choiceOptions.reason);
        AIRequest hinted;
        if (deferDefault) {
            hinted = request;
            hinted.choiceOptions.context.insert(QStringLiteral("legacy_hook"), true);
        }
        AIRequest isolated = deferDefault ? hinted : request;
        projectDecisionContext(player, QVariant(), isolated);
        bool faultContained = false;
        result = decideIsolatedContained(player, isolated, faultContained);
        if (faultContained)
            return false;
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

AIResult AiDecisionCoordinator::decideIsolatedContained(ServerPlayer *player,
                                                        const AIRequest &request,
                                                        bool &faultContained) const
{
    AiDecisionPhase decisionPhase(request, "isolated_with_retry");
    faultContained = false;
    AiLuaRuntime &ai = m_room.roomRuntime()->ai();
    AIResult result = ai.decideIsolated(request);
    if (result.errorCode.isEmpty() || m_room.alivePlayerCount() <= AiLargeRoomAlive)
        return result;
    if (result.errorCode == QStringLiteral("AI_INSTRUCTION_LIMIT")) {
        AiDecisionPhase retryPhase(request, "instruction_retry", 2);
        result = ai.decideIsolated(request, 2);
    }
    if (!result.errorCode.isEmpty()) {
        faultContained = true;
        qWarning().noquote() << "Isolated AI fault contained:" << player->objectName()
                             << "kind" << int(request.kind) << request.choiceOptions.reason
                             << result.errorCode;
    }
    return result;
}

void AiDecisionCoordinator::projectDecisionContext(ServerPlayer *viewer, const QVariant &data,
                                                   AIRequest &request) const
{
    AiDecisionPhase contextPhase(request, "decision_context");
    if (!viewer || (!Config.EnableAI && !m_room.externalAgent(viewer->objectName()))) return;
    EngineRuntimeContextScope scope(*Sanguosha, &m_room);
    LuaRuntime::Binding luaBinding(m_room.roomRuntime()->lua(), false);
    QJsonObject &context = request.choiceOptions.context;
    QString formationPattern = request.pattern;
    if (formationPattern.endsWith('!')) formationPattern.chop(1);
    if (formationPattern == QStringLiteral("@@heg_ziliang")) {
        const QVariant tagged = viewer->getTag(QStringLiteral("ziliang_aidata"));
        if (tagged.canConvert<DamageStruct>()) {
            const DamageStruct damage = tagged.value<DamageStruct>();
            if (damage.to && m_room.getAllPlayers().contains(damage.to)) {
                QJsonObject formation = context.value("original_formation").toObject();
                formation.insert("ziliang_damage", QJsonObject{{"to", damage.to->objectName()}});
                context.insert("original_formation", formation);
            }
        }
    } else if (formationPattern == QStringLiteral("@@heg_qianhuan")) {
        const QVariant tagged = viewer->getTag(QStringLiteral("qianhuan_data"));
        QJsonObject formation = context.value("original_formation").toObject();
        if (tagged.canConvert<CardUseStruct>()) {
            const CardUseStruct use = tagged.value<CardUseStruct>();
            const AIStateRefs refs = liveStateRefs(m_room);
            bool live = refs.card(use.card) && refs.player(use.from);
            QJsonArray targets;
            for (const ServerPlayer *target : use.to) {
                live = live && target && refs.player(target);
                if (target) targets.append(target->objectName());
            }
            if (live) formation.insert("qianhuan_use", QJsonObject{
                {"from", use.from ? use.from->objectName() : QString()},
                {"to", targets}, {"card", publicDecisionCard(use.card)}});
        } else if (tagged.metaType().id() == QMetaType::QVariantList) {
            const QVariantList moves = tagged.toList();
            if (moves.size() == 1 && moves.first().canConvert<CardsMoveOneTimeStruct>()) {
                const CardsMoveOneTimeStruct move = moves.first().value<CardsMoveOneTimeStruct>();
                if (move.to && liveStateRefs(m_room).player(move.to)
                    && move.to_place == Player::PlaceDelayedTrick && move.card_ids.size() == 1)
                    formation.insert("qianhuan_use", false);
            }
        }
        if (!formation.isEmpty()) context.insert("original_formation", formation);
    }
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
        const QSet<int> ownedSet = qsanToSet(owned);
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
            {"amount", damage.damage}, {"nature", int(damage.nature)},
            {"chain", damage.chain}, {"transfer", damage.transfer},
            {"by_user", damage.by_user}, {"reason", damage.reason},
            {"card", publicDecisionCard(damage.card)},
            {"card_name", damage.card ? damage.card->objectName() : QString()}});
    } else if (data.canConvert<DyingStruct>()) {
        const DyingStruct dying = data.value<DyingStruct>();
        QJsonObject dyingObject;
        if (dying.who)
            dyingObject.insert(QStringLiteral("who"), dying.who->objectName());
        if (dying.damage) {
            const DamageStruct &damage = *dying.damage;
            dyingObject.insert(QStringLiteral("damage"), QJsonObject{
                {"from", damage.from ? damage.from->objectName() : QString()},
                {"to", damage.to ? damage.to->objectName() : QString()},
                {"amount", damage.damage}, {"nature", int(damage.nature)},
                {"chain", damage.chain}, {"transfer", damage.transfer},
                {"by_user", damage.by_user},
                {"reason", damage.reason}, {"card", publicDecisionCard(damage.card)},
                {"card_name", damage.card ? damage.card->objectName() : QString()}});
        }
        context.insert(QStringLiteral("dying"), dyingObject);
    } else if (data.canConvert<CardEffectStruct>()) {
        const CardEffectStruct effect = data.value<CardEffectStruct>();
        context.insert("effect", QJsonObject{
            {"from", effect.from ? effect.from->objectName() : QString()},
            {"to", effect.to ? effect.to->objectName() : QString()},
            {"card", publicDecisionCard(effect.card)},
            {"card_name", effect.card ? effect.card->objectName() : QString()},
            {"offset_num", effect.offset_num}, {"no_respond", effect.no_respond},
            {"no_offset", effect.no_offset}});
        if (Config.EnableHegemony
            && (request.choiceOptions.reason == QStringLiteral("heg_nullification")
                || request.choiceOptions.reason == QStringLiteral("nullification"))
            && effect.card) {
            const AIStateRefs refs = liveStateRefs(m_room);
            const auto *chain = m_room.m_aiNullificationContext;
            const bool witnessed = chain && chain->complete && chain->trick == effect.card
                && chain->from == effect.from && chain->to == effect.to
                && chain->positive == effect.nullified && chain->rootEffect.card
                && refs.card(chain->rootEffect.card) && chain->rootEffect.to
                && refs.player(chain->rootEffect.to)
                && (!chain->rootEffect.from || refs.player(chain->rootEffect.from));
            const Card *rootCard = witnessed ? chain->rootEffect.card : effect.card;
            QJsonObject nullification;
            if (witnessed) {
                nullification = QJsonObject{
                    {"source", QStringLiteral("native_nullification_chain")}, {"chain_known", true},
                    {"times", chain->times}, {"type", chain->all},
                    {"root_effect", QJsonObject{
                        {"from", chain->rootEffect.from ? chain->rootEffect.from->objectName() : QString()},
                        {"to", chain->rootEffect.to->objectName()},
                        {"card", publicDecisionCard(rootCard)}}}};
            }
            // The root trick owns the target list. A counter-nullification's
            // own use/history describes a different action and has no such list.
            const QVariant tagged = m_room.getTag(QStringLiteral("UseHistory") + rootCard->toString());
            if (tagged.canConvert<CardUseStruct>()) {
                const CardUseStruct use = tagged.value<CardUseStruct>();
                bool live = use.card == rootCard && refs.card(use.card);
                QJsonArray targets;
                for (const ServerPlayer *target : use.to) {
                    live = live && target && refs.player(target);
                    if (target) targets.append(target->objectName());
                }
                if (live) {
                    context.insert("heg_nullification", QJsonObject{
                        {"source", QStringLiteral("current_trick_use")},
                        {"targets_present", true}, {"targets", targets}});
                    nullification.insert(QStringLiteral("targets"), targets);
                    if (rootCard->isKindOf("HAwaitExhausted")) {
                        QJsonObject choiceBody = context.value(QStringLiteral("choice_body")).toObject();
                        choiceBody.insert(QStringLiteral("await_exhausted"),
                            QJsonObject{{"targets", targets}});
                        context.insert(QStringLiteral("choice_body"), choiceBody);
                    }
                }
            }
            if (!nullification.isEmpty())
                context.insert(QStringLiteral("original26_nullification"), nullification);
        }
    } else if (data.userType() == qMetaTypeId<CardsMoveOneTimeStruct>()) {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        // Ids only for a public destination, as in the event log.
        const bool publicDestination = move.to_place == Player::PlaceEquip
            || move.to_place == Player::PlaceDelayedTrick
            || move.to_place == Player::DiscardPile || move.to_place == Player::PlaceTable;
        QJsonArray ids;
        if (publicDestination) for (int id : move.card_ids) ids.append(id);
        context.insert("move", QJsonObject{
            {"from", move.from ? move.from->objectName() : QString()},
            {"to", move.to ? move.to->objectName() : QString()},
            {"to_place", int(move.to_place)}, {"count", move.card_ids.size()}, {"card_ids", ids},
            {"reason", QJsonObject{{"reason", move.reason.m_reason},
                {"skill_name", move.reason.m_skillName}}}});
    } else if (data.canConvert<CardUseStruct>()) {
        const CardUseStruct use = data.value<CardUseStruct>();
        QJsonArray targets;
        for (const ServerPlayer *target : use.to)
            if (target) targets.append(target->objectName());
        // The question exposes the declared use, never its hidden payment IDs.
        context.insert("use", QJsonObject{
            {"from", use.from ? use.from->objectName() : QString()},
            {"to", targets}, {"card", publicDecisionCard(use.card)}});
    } else if (data.canConvert<ServerPlayer *>()) {
        const ServerPlayer *target = data.value<ServerPlayer *>();
        // Only reference the player in the viewer's existing world snapshot.
        if (target) context.insert("player", target->objectName());
    } else if (data.userType() == QMetaType::QString) {
        context.insert("value", data.toString());
    } else if (data.userType() == QMetaType::QStringList) {
        context.insert("string_list", QJsonArray::fromStringList(data.toStringList()));
    } else if (data.userType() == QMetaType::Int || data.userType() == QMetaType::Bool) {
        context.insert("value", QJsonValue::fromVariant(data));
    }
    // Some asks pass only a player, and keep the surrounding struct in a room tag
    // for the duration of that question. Project those tags as the same value
    // objects; do not invent a struct the tag does not currently hold.
    // Extensions may leave these tags behind; follow only pointers the room owns.
    const AIStateRefs refs = liveStateRefs(m_room);
    if (!context.contains(QStringLiteral("damage"))) {
        const QVariant tagged = m_room.getTag(QStringLiteral("CurrentDamageStruct"));
        const DamageStruct damage = tagged.canConvert<DamageStruct>() ? tagged.value<DamageStruct>() : DamageStruct();
        if (tagged.canConvert<DamageStruct>() && refs.player(damage.from) && refs.player(damage.to)) {
            context.insert(QStringLiteral("damage"), QJsonObject{
                {"from", damage.from ? damage.from->objectName() : QString()},
                {"to", damage.to ? damage.to->objectName() : QString()},
                {"amount", damage.damage}, {"nature", int(damage.nature)},
                {"chain", damage.chain}, {"transfer", damage.transfer},
                {"by_user", damage.by_user},
                {"reason", damage.reason}, {"card", publicDecisionCard(damage.card)},
                {"card_name", damage.card ? damage.card->objectName() : QString()}});
        }
    }
    if (!context.contains(QStringLiteral("use"))) {
        const QVariant tagged = m_room.getTag(QStringLiteral("CurrentUseStruct"));
        const CardUseStruct use = tagged.canConvert<CardUseStruct>() ? tagged.value<CardUseStruct>() : CardUseStruct();
        bool live = tagged.canConvert<CardUseStruct>() && refs.player(use.from) && refs.card(use.card);
        for (const ServerPlayer *target : use.to) live = live && refs.player(target);
        if (live) {
            QJsonArray targets;
            for (const ServerPlayer *target : use.to)
                if (target) targets.append(target->objectName());
            context.insert(QStringLiteral("use"), QJsonObject{
                {"from", use.from ? use.from->objectName() : QString()},
                {"to", targets}, {"card", publicDecisionCard(use.card)}});
        }
    }

    // Hegemony's old Lua policies consume a small, named set of surrounding
    // values.  Project only those exact native equivalents; never serialize
    // the player's private tag map or manufacture an absent value.  The
    // current V2 skills still expose three of these receipts while the rest
    // were removed with their legacy prompt paths.
    const QString reason = request.choiceOptions.reason;
    const auto putR25 = [&context](const QString &name, const QJsonValue &value) {
        QJsonObject frame = context.value(QStringLiteral("original25")).toObject();
        QJsonObject tags = frame.value(QStringLiteral("tags")).toObject();
        tags.insert(name, value);
        frame.insert(QStringLiteral("tags"), tags);
        context.insert(QStringLiteral("original25"), frame);
    };
    const auto putR25Player = [&context](const QString &name, const QString &objectName) {
        if (objectName.isEmpty()) return;
        QJsonObject frame = context.value(QStringLiteral("original25")).toObject();
        QJsonObject players = frame.value(QStringLiteral("players")).toObject();
        players.insert(name, objectName);
        frame.insert(QStringLiteral("players"), players);
        context.insert(QStringLiteral("original25"), frame);
    };
    const auto putR26 = [&context](const QString &name, const QJsonValue &value) {
        QJsonObject frame = context.value(QStringLiteral("original26")).toObject();
        QJsonObject tags = frame.value(QStringLiteral("tags")).toObject();
        tags.insert(name, value);
        frame.insert(QStringLiteral("tags"), tags);
        context.insert(QStringLiteral("original26"), frame);
    };
    const auto exactTag = [&viewer](const QString &name) {
        return viewer ? viewer->getTag(name) : QVariant();
    };
    const auto projectUse = [this](const QVariant &value) -> QJsonValue {
        if (!value.canConvert<CardUseStruct>()) return QJsonValue();
        const CardUseStruct use = value.value<CardUseStruct>();
        const AIStateRefs refs = liveStateRefs(m_room);
        if (!use.from || !use.card || !refs.player(use.from) || !refs.card(use.card))
            return QJsonValue();
        QJsonArray targets;
        for (const ServerPlayer *target : use.to) {
            if (!target || !refs.player(target)) return QJsonValue();
            targets.append(target->objectName());
        }
        return QJsonObject{{"from", use.from->objectName()}, {"to", targets},
            {"card", publicDecisionCard(use.card)}};
    };
    const auto projectDamage = [this](const QVariant &value) -> QJsonValue {
        if (!value.canConvert<DamageStruct>()) return QJsonValue();
        const DamageStruct damage = value.value<DamageStruct>();
        const AIStateRefs refs = liveStateRefs(m_room);
        if (!refs.player(damage.from) || !refs.player(damage.to) || !refs.card(damage.card))
            return QJsonValue();
        return QJsonObject{{"from", damage.from ? damage.from->objectName() : QString()},
            {"to", damage.to ? damage.to->objectName() : QString()}, {"damage", damage.damage},
            {"nature", int(damage.nature)}, {"chain", damage.chain},
            {"transfer", damage.transfer}, {"by_user", damage.by_user},
            {"reason", damage.reason},
            {"card", publicDecisionCard(damage.card)},
            {"card_name", damage.card ? damage.card->objectName() : QString()}};
    };
    const auto useFromContext = [&context]() -> QJsonValue {
        return context.value(QStringLiteral("use"));
    };
    const bool dataIsUse = data.canConvert<CardUseStruct>();
    const bool dataIsDamage = data.canConvert<DamageStruct>();
    const SkillContext *inquiry = Config.EnableHegemony ? m_room.m_aiSkillInquiryContext : nullptr;
    // A surrounding callback is usable only for its own question and asker.
    // This is a borrowed live frame, not a lookup of a potentially stale tag.
    const bool ownInquiry = inquiry && inquiry->skill_name == reason
        && (inquiry->owner == viewer || inquiry->invoker == viewer);
    const QVariant inquiryData = ownInquiry && inquiry->original_data
        ? *inquiry->original_data : QVariant();
    if (request.pattern == QStringLiteral("@@heg_Triblade") && inquiry
        && inquiry->skill_name == QStringLiteral("heg_Triblade")
        && (inquiry->owner == viewer || inquiry->invoker == viewer)) {
        // The weapon callback owns these exact offered targets and restores the
        // flags after its ask. Never infer this selection from a previous damage.
        QJsonArray targets;
        for (const ServerPlayer *target : m_room.getOtherPlayers(viewer)) {
            if (target->hasFlag(QStringLiteral("TribladeCanBeSelected")))
                targets.append(target->objectName());
        }
        context.insert(QStringLiteral("heg_triblade"), QJsonObject{{"targets", targets}});
    }
    const auto addR25Use = [&](const QString &tagName) {
        QJsonValue use = projectUse(exactTag(tagName));
        // A player-only ask must not inherit an unrelated CurrentUseStruct.
        if ((use.isUndefined() || use.isNull()) && dataIsUse) use = useFromContext();
        if (use.isUndefined() || use.isNull()) use = projectUse(inquiryData);
        if (use.isObject()) putR25(tagName, use);
    };
    const auto addR25Damage = [&](const QString &tagName) {
        QJsonValue damage = projectDamage(exactTag(tagName));
        // CurrentDamageStruct may be stale during DamageCaused/skill-cost asks;
        // only a typed DamageStruct supplied by this question may supplement a tag.
        if ((damage.isUndefined() || damage.isNull()) && dataIsDamage)
            damage = projectDamage(data);
        if (damage.isUndefined() || damage.isNull()) damage = projectDamage(inquiryData);
        if (damage.isObject()) putR25(tagName, damage);
    };
    if (reason == QStringLiteral("heg_zhente")) addR25Use(QStringLiteral("ZhenteUsedata"));
    else if (reason == QStringLiteral("heg_qiao")) addR25Use(QStringLiteral("QiaoUsedata"));
    else if (reason == QStringLiteral("heg_shejian")) addR25Use(QStringLiteral("ShejianUsedata"));
    else if (reason == QStringLiteral("heg_anyong")) addR25Damage(QStringLiteral("AnyongDamagedata"));
    else if (reason == QStringLiteral("heg_zhiman")) addR25Damage(QStringLiteral("zhiman_data"));
    else if (reason == QStringLiteral("heg_fudi")) {
        const QVariant target = exactTag(QStringLiteral("FudiTarget"));
        if (target.canConvert<ServerPlayer *>()) {
            const ServerPlayer *targetPlayer = target.value<ServerPlayer *>();
            if (liveStateRefs(m_room).player(targetPlayer))
                putR25Player(QStringLiteral("FudiTarget"), targetPlayer->objectName());
        }
    } else if (reason == QStringLiteral("heg_fengshix")) {
        QJsonValue use = projectUse(exactTag(QStringLiteral("FengshixUsedata")));
        if (use.isObject()) putR26(QStringLiteral("FengshixUsedata"), use);
    } else if (reason == QStringLiteral("known_both")) {
        const QJsonValue target = context.value(QStringLiteral("player"));
        if (target.isString()) context.insert(QStringLiteral("original_known_both_target"), target);
    } else if (reason == QStringLiteral("jinghe_skill") && inquiry
               && inquiry->skill_name == QStringLiteral("heg_jinghe")
               && inquiry->targets.contains(viewer)) {
        // The recipient may observe the accepted recipients of this very grant.
        // Do not recover a target list from another execution or from choices.
        const auto *execution = m_skillRuntime.findSkillExecution(inquiry->executionID);
        const AIStateRefs refs = liveStateRefs(m_room);
        bool live = execution && !execution->finished && inquiry->invoker
            && refs.player(inquiry->invoker);
        QStringList names;
        for (const ServerPlayer *target : inquiry->targets) {
            live = live && target && refs.player(target);
            if (target) names << target->objectName();
        }
        if (live) putR26(QStringLiteral("JingheTargets"), names.join(QLatin1Char('+')));
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
    AiDecisionAudit audit(m_room, player, request, QStringLiteral("askForSkillInvoke"));
    projectDecisionContext(player, data, request);
    AI *ai = player->getAI();
    AIResult result;
    if (!runAnswer(player, request,
                   QStringLiteral("askForSkillInvoke"),
                   [ai, &skillName, &data](const AIRequest &request) {
                       return legacyAnswerResult(request,
                           ai->askForSkillInvoke(skillName, data) ? QStringLiteral("yes")
                                                                  : QString());
                   }, result, audit))
        return false;
    audit.envelope(result);
    // Pass is the refusal; only an explicit "yes" invokes the skill.
    invoked = result.kind == AIResult::Answer
        && result.action.userString == QStringLiteral("yes");
    audit.accept(invoked ? QStringLiteral("yes") : QStringLiteral("no"));
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
    AiDecisionAudit audit(m_room, player, request, QStringLiteral("askForChoice"));
    projectDecisionContext(player, data, request);
    AI *ai = player->getAI();
    AIResult result;
    if (!runAnswer(player, request,
                   QStringLiteral("askForChoice"),
                   [ai, &skillName, &choices, &data](const AIRequest &request) {
                       return legacyAnswerResult(request,
                           ai->askForChoice(skillName, choices, data));
                   }, result, audit))
        return false;
    audit.envelope(result);
    if (result.kind != AIResult::Answer || result.action.userString.isEmpty())
        return false; // A refusal leaves the call site's own default in place.
    answer = result.action.userString;
    audit.accept(QStringLiteral("answer"));
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
    AIRequest request = makeChoiceRequest(player, AIRequest::Suit, options);
    AiDecisionAudit audit(m_room, player, request, QStringLiteral("askForSuit"));
    AI *ai = player->getAI();
    AIResult result;
    if (!runAnswer(player, request,
                   QStringLiteral("askForSuit"),
                   [ai, &reason](const AIRequest &request) {
                       return legacyAnswerResult(request, Card::Suit2String(ai->askForSuit(reason)));
                   }, result, audit))
        return false;
    audit.envelope(result);
    if (result.kind != AIResult::Answer) return false;
    // Suits cross as their names; an unknown name is refused, never guessed.
    for (int index = 0; index < 4; ++index) {
        if (Card::Suit2String(Card::AllSuits[index]) == result.action.userString) {
            suit = Card::AllSuits[index];
            audit.accept(QStringLiteral("suit"));
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
    AIRequest request = makeChoiceRequest(player, AIRequest::Kingdom, options);
    AiDecisionAudit audit(m_room, player, request, QStringLiteral("askForKingdom"));
    AI *ai = player->getAI();
    // The legacy split between askForKingdom and askForChoice is the call site's rule;
    // it is reproduced here so the route change alone cannot alter which one runs.
    const bool useKingdomCallback = reason.isEmpty() || reason.contains(QStringLiteral("gamerule_"));
    AIResult result;
    if (!runAnswer(player, request,
                   QStringLiteral("askForKingdom"),
                   [ai, &reason, &kingdoms, useKingdomCallback](const AIRequest &request) {
                       return legacyAnswerResult(request, useKingdomCallback
                           ? ai->askForKingdom(kingdoms)
                           : ai->askForChoice(reason, kingdoms.join(QStringLiteral("+")), QVariant()));
                   }, result, audit))
        return false;
    audit.envelope(result);
    if (result.kind != AIResult::Answer || result.action.userString.isEmpty())
        return false;
    answer = result.action.userString;
    audit.accept(QStringLiteral("kingdom"));
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
    // The offered generals' public definitions, and the shown lord's kingdom.
    QJsonObject catalog;
    for (const QString &name : generals) {
        const General *general = Sanguosha->getGeneral(name);
        QJsonObject row{{"exists", general != nullptr}};
        if (general) {
            row.insert("kingdom", general->getKingdom());
            row.insert("max_hp", general->getMaxHp());
            row.insert("skill_description", general->getSkillDescription(true));
        }
        catalog.insert(name, row);
    }
    options.context.insert("general_catalog", catalog);
    const ServerPlayer *lord = m_room.getLord();
    if (lord && lord->getGeneral())
        options.context.insert("lord_general_kingdom", lord->getGeneral()->getKingdom());
    AIRequest request = makeChoiceRequest(player, AIRequest::General, options);
    AiDecisionAudit audit(m_room, player, request, QStringLiteral("askForGeneral"));
    AI *ai = player->getAI();
    AIResult result;
    if (!runAnswer(player, request,
                   QStringLiteral("askForGeneral"),
                   [ai, &generals, &defaultChoice, &reason](const AIRequest &request) {
                       return legacyAnswerResult(request,
                           ai->askForGeneral(generals, defaultChoice, reason));
                   }, result, audit))
        return false;
    audit.envelope(result);
    if (result.kind != AIResult::Answer || result.action.userString.isEmpty())
        return false;
    answer = result.action.userString;
    audit.accept(QStringLiteral("general"));
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
    AiDecisionAudit audit(m_room, player, request, QStringLiteral("askForDiscard"));
    projectDecisionContext(player, QVariant(), request);
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
                   }, result, audit, &fromIsolated))
        return false;
    audit.envelope(result);
    if (fromIsolated && result.kind == AIResult::Pass && optional) {
        cards.clear();
        audit.accept(QStringLiteral("pass"));
        return true;
    }
    if (result.kind != AIResult::Answer) return false;
    if (fromIsolated) {
        // Only cards the question offered may come back, in the amount it allows.
        const QSet<int> offered = qsanToSet(candidates);
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
    audit.accept(QStringLiteral("discard"));
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
    AIRequest request = makeChoiceRequest(player, AIRequest::AmazingGrace, options);
    AiDecisionAudit audit(m_room, player, request, QStringLiteral("askForAG"));
    AI *ai = player->getAI();
    AIResult result;
    bool fromIsolated = false;
    int legacyCardId = -1;
    if (!runAnswer(player, request,
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
                   }, result, audit, &fromIsolated))
        return false;
    audit.envelope(result);
    if (!fromIsolated) {
        if (legacyCardId < 0) return false;
        cardId = legacyCardId;
        audit.accept(QStringLiteral("card"));
        return true;
    }
    if (result.kind == AIResult::Pass && refusable) {
        cardId = -1;
        audit.accept(QStringLiteral("pass"));
        return true;
    }
    if (result.kind != AIResult::Answer || result.action.selectedCardIds.size() != 1)
        return false;
    if (!cardIds.contains(result.action.selectedCardIds.first()))
        return false;
    cardId = result.action.selectedCardIds.first();
    audit.accept(QStringLiteral("card"));
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
    projectDecisionContext(player, QVariant(), request);
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
    AiDecisionAudit audit(m_room, player, request, QStringLiteral("askForCardChosen"));
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
                   }, result, audit, &fromIsolated))
        return false;
    audit.envelope(result);
    if (!fromIsolated) {
        if (legacyCardId < 0) return false;
        cardId = legacyCardId;
        audit.accept(QStringLiteral("card"));
        return true;
    }
    if (result.kind != AIResult::Answer || result.action.selectedCardIds.size() != 1)
        return false;
    cardId = result.action.selectedCardIds.first();
    if (!request.choiceOptions.cardIds.contains(cardId)) return false;
    if (method == Card::MethodDiscard && !player->canDiscard(who, cardId)) return false;
    audit.accept(QStringLiteral("card"));
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
    AIRequest request = makeChoiceRequest(player, AIRequest::Yiji, options);
    AiDecisionAudit audit(m_room, player, request, QStringLiteral("askForYiji"));
    AI *ai = player->getAI();
    AIResult result;
    bool fromIsolated = false;
    ServerPlayer *legacyReceiver = nullptr;
    int legacyCardId = -1;
    if (!runAnswer(player, request,
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
                   }, result, audit, &fromIsolated))
        return false;
    audit.envelope(result);
    if (!fromIsolated) {
        if (!legacyReceiver || legacyCardId < 0) return false;
        target = legacyReceiver;
        cardId = legacyCardId;
        audit.accept(QStringLiteral("card"));
        return true;
    }
    if (result.kind == AIResult::Pass) {
        target = nullptr;
        cardId = -1;
        audit.accept(QStringLiteral("pass"));
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
            audit.accept(QStringLiteral("card"));
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
    AIRequest request = makeChoiceRequest(player, AIRequest::PlayerChosen, options);
    AiDecisionAudit audit(m_room, player, request, QStringLiteral("askForPlayerChosen"));
    projectDecisionContext(player, QVariant(), request);
    AI *ai = player->getAI();
    AIResult result;
    bool fromIsolated = false;
    ServerPlayer *legacyChoice = nullptr;
    if (!runAnswer(player, request,
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
                   }, result, audit, &fromIsolated))
        return false;
    audit.envelope(result);
    if (!fromIsolated) {
        if (!legacyChoice) return false;
        choice = legacyChoice;
        audit.accept(QStringLiteral("player"));
        return true;
    }
    if (result.kind != AIResult::Answer || result.action.selectedTargetNames.size() != 1)
        return false;
    foreach (ServerPlayer *target, targets) {
        if (target && target->objectName() == result.action.selectedTargetNames.first()) {
            choice = target;
            audit.accept(QStringLiteral("player"));
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
    AIRequest request = makeChoiceRequest(player, AIRequest::PlayersChosen, options);
    AiDecisionAudit audit(m_room, player, request, QStringLiteral("askForPlayersChosen"));
    projectDecisionContext(player, QVariant(), request);
    AI *ai = player->getAI();
    AIResult result;
    bool fromIsolated = false;
    QList<ServerPlayer *> legacyPicked;
    if (!runAnswer(player, request,
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
                   }, result, audit, &fromIsolated))
        return false;
    audit.envelope(result);
    if (!fromIsolated) {
        if (legacyPicked.isEmpty()) return false;
        chosen = legacyPicked;
        return true;
    }
    if (result.kind == AIResult::Pass && minNum <= 0) {
        // Callers may prefill this list with every candidate. A deliberate decline
        // must clear it, rather than leave that default looking like an AI choice.
        chosen.clear();
        audit.accept(QStringLiteral("pass"));
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
    audit.accept(QStringLiteral("players"));
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
    AIRequest request = makeChoiceRequest(player, AIRequest::Guanxing, options);
    AiDecisionAudit audit(m_room, player, request, QStringLiteral("askForGuanxing"));
    AI *ai = player->getAI();
    AIResult result;
    if (!runAnswer(player, request,
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
                   }, result, audit))
        return false;
    audit.envelope(result);
    if (result.kind != AIResult::Answer) return false;
    // Both piles together must be exactly the cards the question offered, in some order.
    QList<int> seen = result.action.selectedCardIds;
    seen << result.action.bottomCardIds;
    if (seen.size() != cards.size())
        return false;
    const QSet<int> expected = qsanToSet(cards);
    const QSet<int> actual = qsanToSet(seen);
    if (actual.size() != seen.size() || actual != expected) return false;
    up = result.action.selectedCardIds;
    bottom = result.action.bottomCardIds;
    audit.accept(QStringLiteral("guanxing"));
    return true;
}

bool AiDecisionCoordinator::decideTriggerOrder(ServerPlayer *player, const QString &reason,
                                               const QStringList &candidates,
                                               QMap<ServerPlayer *, QStringList> &skills,
                                               bool optional, const QVariant &data,
                                               QString &answer) const
{
    if (!player || !player->getAI()) return false;
    // Initial dealing emits many nested trigger menus. Use the established
    // SmartAI policy here without constructing a snapshot/isolated facade for
    // each menu. The caller still normalizes candidates and gates hidden skills.
    if (const RoomThread *thread = m_room.getThread()) {
        for (const EventTriplet &event : *thread->getEventStack()) {
            if (event.event() == GameReady && !Config.EnableHegemony
                && !m_room.externalAgent(player->objectName())) {
                static const bool probe = qEnvironmentVariableIntValue("QSAN_AI_WV_PROBE") > 0;
                if (probe) qInfo() << "[AI_WV] trigger_order startup_legacy choices="
                                   << candidates.size() << "optional=" << optional;
                answer = player->getAI()->askForTriggerOrder(reason, skills, optional, data);
                return true;
            }
        }
    }
    AIChoiceOptions options;
    options.reason = reason;
    options.choices = candidates;
    options.optional = optional;
    AIRequest request = makeChoiceRequest(player, AIRequest::TriggerOrder, options);
    AiDecisionAudit audit(m_room, player, request, QStringLiteral("askForTriggerOrder"));
    AI *ai = player->getAI();
    AIResult result;
    if (!runAnswer(player, request,
                   QStringLiteral("askForTriggerOrder"),
                   [ai, &reason, &skills, optional, &data](const AIRequest &legacyRequest) {
                       return legacyAnswerResult(legacyRequest,
                           ai->askForTriggerOrder(reason, skills, optional, data));
                   }, result, audit))
        return false;
    audit.envelope(result);
    if (result.kind != AIResult::Answer || result.action.userString.isEmpty())
        return false;
    // The call site normalizes the skill/owner spelling, so the string passes through.
    answer = result.action.userString;
    audit.accept(QStringLiteral("trigger_order"));
    return true;
}

const Card *AiDecisionCoordinator::decideResponse(ServerPlayer *player, const AIRequest &request,
                                                  const QString &callbackName,
                                                  const LegacyCard &legacy) const
{
    if (!player || !player->getAI()) return nullptr;
    AiDecisionAudit audit(m_room, player, request, callbackName);
    AIResult externalResult;
    const Card *externalCard = nullptr;
    if (externalAnswer(player, request, externalResult, [&](const AIResult &answer) {
            if (answer.kind == AIResult::Pass) return request.choiceOptions.optional;
            externalCard = responseCard(player, request, answer);
            return externalCard != nullptr;
        })) {
        audit.setSource(QStringLiteral("external"));
        audit.accept(externalResult.kind == AIResult::Pass
            ? QStringLiteral("pass") : QStringLiteral("card"));
        return externalCard;
    }
    if (Config.EnableHegemony) {
        const AiRoute strictRoute = Config.EnableAI
            ? m_room.roomRuntime()->ai().routes().routeFor(
                request.kind, callbackName, request.choiceOptions.reason)
            : AiRouteLegacyDirect;
        audit.setSource(QStringLiteral("isolated"));
        if (strictRoute != AiRouteIsolated) {
            audit.reject(QStringLiteral("strict_route_not_isolated"));
            return nullptr;
        }
        AIRequest isolated = request;
        projectDecisionContext(player, QVariant(), isolated);
        bool faultContained = false;
        const AIResult result = decideIsolatedContained(player, isolated, faultContained);
        audit.envelope(result);
        if (faultContained) {
            audit.reject(result.errorCode.isEmpty()
                ? QStringLiteral("isolated_fault_contained") : result.errorCode);
            return nullptr;
        }
        if (result.handled && result.errorCode.isEmpty() && result.kind == AIResult::Pass
            && request.choiceOptions.optional && result.decisionId == request.decisionId
            && result.stateRevision == request.stateRevision
            && request.stateRevision == m_room.roomRuntime()->stateRevision()) {
            audit.accept(QStringLiteral("pass"));
            return nullptr;
        }
        const Card *answered = responseCard(player, request, result);
        if (answered) {
            audit.accept(QStringLiteral("card"));
            return answered;
        }
        audit.reject(QStringLiteral("authority_rejected"));
        return nullptr;
    }
    if (m_room.externalAgent(player->objectName())) {
        QElapsedTimer nativeTimer;
        nativeTimer.start();
        const Card *card = legacy();
        logExternalNativeDuration(player, request, nativeTimer);
        return card;
    }
    const AiRoute route = Config.EnableAI
        ? m_room.roomRuntime()->ai().routes().routeFor(
            request.kind, callbackName, request.choiceOptions.reason)
        : AiRouteLegacyDirect;
    if (route == AiRouteIsolated) {
        AIRequest isolated = request;
        projectDecisionContext(player, QVariant(), isolated);
        bool faultContained = false;
        const AIResult result = decideIsolatedContained(player, isolated, faultContained);
        if (faultContained)
            return nullptr;
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
        // An issued @@skill ticket was built by that skill's canActivate and
        // createCard for this exact request. Its product is not a card pattern.
        if (player->isCardLimited(card, request.handlingMethod)
            || (!pattern.isEmpty() && !request.pattern.startsWith(QLatin1String("@@"))
                && !Sanguosha->matchPattern(pattern, player, card))) {
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
    // Room accepts a hand-pile card as a response exactly like a hand card.
    if (!player->handCards().contains(cardId) && !player->getHandPile().contains(cardId)) {
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
    QList<int> hand = player ? player->handCards() : QList<int>();
    // A response may also take a hand-pile card, as Room accepts it; showing and
    // pindian take the hand only.
    if (player && !physicalOnly) hand << player->getHandPile();
    const QSet<int> handSet = qsanToSet(hand);
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
    // SmartAI reads this data as a CardEffect (data:toCardEffect()); other data is a
    // known absence of one, not an unprojected effect.
    request.choiceOptions.context.insert(QStringLiteral("effect_present"),
                                         data.canConvert<CardEffectStruct>());
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
    // _askForNullification supplies these exact endpoints while the response
    // is live.  Keep the typed effect in the request so Hegemony projections
    // can join it to UseHistory without reading a generic private tag dump.
    CardEffectStruct effect;
    effect.card = trick;
    effect.from = from;
    effect.to = to;
    effect.nullified = positive;
    projectDecisionContext(player, QVariant::fromValue(effect), request);
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
    AiDecisionAudit audit(m_room, player, request, callbackName);
    AIResult externalResult;
    const auto validateAction = [&](const AIResult &answer) {
            CardUseStruct candidate = cardUse;
            if (!applyResult(player, request, answer, candidate)) return false;
            if (candidate.card) {
                if (player->isCardLimited(candidate.card, request.handlingMethod)) return false;
                if (request.kind == AIRequest::Activate && !candidate.card->isAvailable(player)) return false;
                if (request.kind == AIRequest::UseCard && !candidate.hasSkillActivationRequest) {
                    QString pattern = request.pattern;
                    if (pattern.endsWith(QLatin1Char('!'))) pattern.chop(1);
                    if (!pattern.isEmpty() && !Sanguosha->matchPattern(pattern, player, candidate.card))
                        return false;
                }
                QList<const Player *> targets;
                for (const auto *target : candidate.to) {
                    if (m_room.isProhibited(player, target, candidate.card, targets)
                        || !candidate.card->targetFilter(targets, target, player)) return false;
                    targets << target;
                }
                if (!candidate.card->targetsFeasible(targets, player)) return false;
            }
            cardUse = candidate;
            return true;
        };
    if (externalAnswer(player, request, externalResult, validateAction)) {
        audit.setSource(QStringLiteral("external"));
        audit.accept(externalResult.kind == AIResult::Pass
            ? QStringLiteral("pass") : QStringLiteral("card"));
        return true;
    }
    if (Config.EnableHegemony) {
        const QString skillName = request.hasSkillActionContext
            ? request.skillActionContext.getActivationSkillName() : QString();
        const AiRoute strictRoute = Config.EnableAI
            ? m_room.roomRuntime()->ai().routes().routeFor(request.kind, callbackName, skillName)
            : AiRouteLegacyDirect;
        audit.setSource(QStringLiteral("isolated"));
        if (strictRoute != AiRouteIsolated) {
            audit.reject(QStringLiteral("strict_route_not_isolated"));
            return false;
        }
        AIRequest isolated = request;
        projectDecisionContext(player, QVariant(), isolated);
        bool faultContained = false;
        const AIResult result = decideIsolatedContained(player, isolated, faultContained);
        audit.envelope(result);
        if (faultContained) {
            audit.reject(result.errorCode.isEmpty()
                ? QStringLiteral("isolated_fault_contained") : result.errorCode);
            return false;
        }
        if (!result.handled || !result.errorCode.isEmpty()) {
            audit.reject(result.errorCode.isEmpty()
                ? QStringLiteral("isolated_envelope_rejected") : result.errorCode);
            return false;
        }
        if (!validateAction(result)) {
            audit.reject(QStringLiteral("authority_rejected"));
            return false;
        }
        audit.accept(result.kind == AIResult::Pass
            ? QStringLiteral("pass") : QStringLiteral("card"));
        return true;
    }
    if (qgetenv("QSAN_10P_LAG_PROBE") == "1")
        qWarning().noquote() << "[LAG_PROBE] begin decide" << request.decisionId
                             << player->objectName() << callbackName;
    // Diagnostic probe for AI-decision duration and hot-call counts; enabled only by QSAN_AI_PROBE=1.
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
    const AiRoute route = Config.EnableAI && !m_room.externalAgent(player->objectName())
        ? m_room.roomRuntime()->ai().routes().routeFor(request.kind, callbackName, skillName)
        : AiRouteLegacyDirect;
    if (route == AiRouteLegacyDirect) {
        if (request.hasSkillActionContext)
            return false;
        QElapsedTimer nativeTimer;
        if (m_room.externalAgent(player->objectName())) nativeTimer.start();
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
        if (nativeTimer.isValid()) logExternalNativeDuration(player, request, nativeTimer);
        return true;
    }

    AIResult result;
    bool liveLegacyResult = route == AiRouteLegacyAdapted;
    if (route == AiRouteIsolated) {
        AIRequest isolated = request;
        projectDecisionContext(player, QVariant(), isolated);
        bool faultContained = false;
        result = decideIsolatedContained(player, isolated, faultContained);
        if (faultContained) {
            // Decline: Pass ends the play phase, or refuses askForUseCard.
            result = AIResult();
            result.handled = true;
            result.kind = AIResult::Pass;
            result.decisionId = request.decisionId;
            result.stateRevision = request.stateRevision;
        } else if (!result.handled || !result.errorCode.isEmpty()) {
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
            QString(), QString(), Card::MethodUse, request, false))
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
        // Legacy Lua reads only the source instance and quota; it does not read the board or candidates.
        if (buildSkillActionRequest(player, instance, reason, pattern, prompt, method, request, false))
            return AiLegacyRequestView(request, player);
    }
    return AiLegacyRequestView();
}
